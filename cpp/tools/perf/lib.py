"""Shared helpers for tscpp-vs-tsgo perf benchmarks."""
import os
import statistics
import subprocess
import time

REPO = os.environ.get(
    "REPO",
    subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True, text=True, check=True,
    ).stdout.strip(),
)
TSGO = os.environ.get("TSGO", "/tmp/tsgo")
TSCPP = os.environ.get("TSCPP", "/tmp/tscpp_probe")
RESULTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results")

# The exact flag set used for every per-file check on both compilers.
CHECK_FLAGS = ["--noEmit", "--pretty", "false"]


def tsgo_cmd(file):
    return [TSGO, file] + CHECK_FLAGS


def tscpp_cmd(file):
    return [TSCPP, "tsc", file] + CHECK_FLAGS


def run_timed(cmd, cwd=None):
    """Run cmd once, return wall-clock seconds. stdout/stderr discarded."""
    t0 = time.monotonic()
    subprocess.run(
        cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        cwd=cwd or REPO,
    )
    return time.monotonic() - t0


def run_capture(cmd, cwd=None):
    """Run cmd once, return (wall_s, exit_code, combined_output_bytes)."""
    t0 = time.monotonic()
    p = subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        cwd=cwd or REPO,
    )
    return time.monotonic() - t0, p.returncode, p.stdout


def median(xs):
    return statistics.median(xs)


def p95(xs):
    xs = sorted(xs)
    if not xs:
        return float("nan")
    k = (len(xs) - 1) * 0.95
    lo = int(k)
    hi = min(lo + 1, len(xs) - 1)
    return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)


def stats(xs):
    return {
        "n": len(xs),
        "median_s": median(xs),
        "mean_s": statistics.fmean(xs),
        "p95_s": p95(xs),
        "min_s": min(xs),
        "max_s": max(xs),
    }


def hardware_info():
    info = {}
    info["nproc"] = os.cpu_count()
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemTotal"):
                    info["mem_total_kb"] = int(line.split()[1])
                    break
    except OSError:
        pass
    for name, cmd in (
        ("clang", ["clang++-15", "--version"]),
        ("go", ["go", "version"]),
        ("kernel", ["uname", "-r"]),
        ("cpu_model", None),
    ):
        if cmd is None:
            try:
                with open("/proc/cpuinfo") as f:
                    for line in f:
                        if line.startswith("model name"):
                            info["cpu_model"] = line.split(":", 1)[1].strip()
                            break
            except OSError:
                pass
            continue
        try:
            info[name] = subprocess.run(
                cmd, capture_output=True, text=True
            ).stdout.splitlines()[0]
        except (OSError, IndexError):
            pass
    return info


def repo_rel(path):
    return os.path.relpath(path, REPO) if os.path.isabs(path) else path
