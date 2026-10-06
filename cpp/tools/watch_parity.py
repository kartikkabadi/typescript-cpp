#!/usr/bin/env python3
"""Watch-mode parity harness: drive `tsc -w` / `tsc -b -w` on both the Go
oracle (tsgo) and the C++ port (tscpp_probe) over identical scratch project
copies, apply the same mutation sequence to each, and diff the results.

Per step the harness compares:
  - normalized stdout (timestamps + absolute project dir scrubbed)
  - the emitted file tree (relative path -> sha256), which covers .js/.d.ts
    outputs and .tsbuildinfo bytes
  - the process's ability to keep watching (a hang/timeout is a DIFF)

Usage:
    watch_parity.py [--go /tmp/tsgo] [--cpp /tmp/tscpp_probe] \
                    [--work /tmp/watch_parity] [--scenario NAME] [-v]

Requires no third-party packages. All watch runs are time-capped.
"""
import argparse
import difflib
import hashlib
import os
import re
import shutil
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

IDLE_MARKER = "Watching for file changes."
STEP_TIMEOUT = 45          # seconds to wait for the next idle marker
PROC_TIMEOUT = 180         # absolute cap per watch run

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]")
TS_RE = re.compile(r"\d{1,2}:\d{2}:\d{2} [AP]M - ")


def norm(text, proj):
    """Normalize a raw stdout chunk for cross-binary comparison."""
    text = ANSI_RE.sub("", text)
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = TS_RE.sub("<TS> - ", text)
    text = text.replace(proj, "<PROJ>")
    # Trim leading/trailing blank lines per segment.
    lines = [l for l in text.split("\n")]
    return "\n".join(lines).strip("\n")


def tree_map(d):
    """{relpath: sha256} for every file under d."""
    out = {}
    for base, _dirs, files in os.walk(d):
        for f in files:
            p = os.path.join(base, f)
            rel = os.path.relpath(p, d)
            with open(p, "rb") as fh:
                out[rel] = hashlib.sha256(fh.read()).hexdigest()[:16]
    return out


def tree_diff(a, b):
    diffs = []
    for k in sorted(set(a) | set(b)):
        if k not in a:
            diffs.append(f"  +cpp {k}")
        elif k not in b:
            diffs.append(f"  -cpp {k}")
        elif a[k] != b[k]:
            diffs.append(f"  ~cpp {k}")
    return diffs


class WatchProc:
    """A tsc watch run; stdout drained on a thread into a bytes buffer."""

    def __init__(self, argv, cwd):
        self.argv = argv
        self.cwd = cwd
        self.proc = subprocess.Popen(
            argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL, bufsize=0)
        self.buf = bytearray()
        self.done = threading.Event()
        self.th = threading.Thread(target=self._drain, daemon=True)
        self.th.start()

    def _drain(self):
        fd = self.proc.stdout.fileno()
        try:
            while True:
                chunk = os.read(fd, 65536)
                if not chunk:
                    break
                self.buf.extend(chunk)
        except OSError:
            pass
        self.done.set()

    def text(self):
        return self.buf.decode("utf-8", "replace")

    def marker_count(self):
        return self.buf.count(IDLE_MARKER.encode())

    def wait_marker(self, count, timeout=STEP_TIMEOUT):
        """Block until the idle marker has appeared `count` times."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.marker_count() >= count:
                return True
            if self.proc.poll() is not None and self.done.is_set():
                return False
            time.sleep(0.1)
        return False

    def kill(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.done.wait(timeout=5)


# ---------------------------------------------------------------------------
# project templates
# ---------------------------------------------------------------------------

SIMPLE_TSCONFIG = (
    '{"compilerOptions":{"rootDir":"src","outDir":"dist"},'
    '"include":["src"]}\n')
MAIN_TS = 'export function greet(): string { return "hi"; }\nconsole.log(greet());\n'

BPROJ_COMMON_TSCONFIG = ('{"compilerOptions":{"rootDir":"src","outDir":"dist",'
                         '"declaration":true,"composite":true},"include":["src"]}\n')
BPROJ_APP_TSCONFIG = ('{"compilerOptions":{"rootDir":"src","outDir":"dist",'
                      '"composite":true},"include":["src"],'
                      '"references":[{"path":"../common"}]}\n')
BPROJ_LIB = 'export function add(a: number, b: number): number { return a + b; }\n'
BPROJ_MAIN = ('import { add } from "../../common/dist/lib";\n'
              'console.log(add(1, 2));\n')


def write(root, rel, content):
    p = os.path.join(root, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w") as f:
        f.write(content)


def append(root, rel, content):
    with open(os.path.join(root, rel), "a") as f:
        f.write(content)


def remove(root, rel):
    os.remove(os.path.join(root, rel))


def setup_simple(root):
    write(root, "tsconfig.json", SIMPLE_TSCONFIG)
    write(root, "src/main.ts", MAIN_TS)


def setup_bproj(root):
    write(root, "common/tsconfig.json", BPROJ_COMMON_TSCONFIG)
    write(root, "common/src/lib.ts", BPROJ_LIB)
    write(root, "app/tsconfig.json", BPROJ_APP_TSCONFIG)
    write(root, "app/src/main.ts", BPROJ_MAIN)


# ---------------------------------------------------------------------------
# scenarios: (name, argv_tail, setup, mutations)
# argv_tail is the arg list after the tsc subcommand (tscpp adds "tsc").
# ---------------------------------------------------------------------------

SCENARIOS = []


def scenario(name):
    def deco(fn):
        SCENARIOS.append((name, fn))
        return fn
    return deco


@scenario("edit-source")
def s_edit_source():
    return {
        "args": ["-w", "--pretty", "false"],
        "setup": setup_simple,
        "steps": [
            ("edit src/main.ts",
             lambda d: write(d, "src/main.ts",
                             'export function greet(): string { return "hello"; }\n'
                             'console.log(greet());\n')),
        ],
    }


@scenario("type-error-add-clear")
def s_type_error():
    return {
        "args": ["-w", "--pretty", "false"],
        "setup": setup_simple,
        "steps": [
            ("introduce type error",
             lambda d: write(d, "src/main.ts",
                             'export function greet(): string { return "hi"; }\n'
                             'const n: number = greet();\n')),
            ("fix type error",
             lambda d: write(d, "src/main.ts", MAIN_TS)),
        ],
    }


@scenario("add-file-and-import")
def s_add_file():
    return {
        "args": ["-w", "--pretty", "false"],
        "setup": setup_simple,
        "steps": [
            ("add src/util.ts + import",
             lambda d: (write(d, "src/util.ts",
                              'export const answer: number = 42;\n'),
                        write(d, "src/main.ts",
                              'import { answer } from "./util";\n'
                              'export function greet(): string { return "hi" + answer; }\n'
                              'console.log(greet());\n'))),
        ],
    }


@scenario("delete-imported-file")
def s_delete_file():
    def setup(d):
        setup_simple(d)
        write(d, "src/util.ts", 'export const answer: number = 42;\n')
        write(d, "src/main.ts",
              'import { answer } from "./util";\nconsole.log(answer);\n')
    return {
        "args": ["-w", "--pretty", "false"],
        "setup": setup,
        "steps": [
            ("delete src/util.ts",
             lambda d: remove(d, "src/util.ts")),
        ],
    }


@scenario("edit-node-modules-dts")
def s_dts():
    def setup(d):
        write(d, "tsconfig.json",
              '{"compilerOptions":{"rootDir":"src","outDir":"dist",'
              '"module":"commonjs"},"include":["src"]}\n')
        write(d, "node_modules/mydep/package.json",
              '{"name":"mydep","version":"1.0.0","types":"index.d.ts",'
              '"main":"index.js"}\n')
        write(d, "node_modules/mydep/index.js",
              'module.exports = { depFn: function(x) { return x; } };\n')
        write(d, "node_modules/mydep/index.d.ts",
              'export declare function depFn(x: number): number;\n')
        write(d, "src/main.ts",
              'import { depFn } from "mydep";\nconsole.log(depFn(1));\n')
    return {
        "args": ["-w", "--pretty", "false"],
        "setup": setup,
        "steps": [
            ("edit node_modules/mydep/index.d.ts signature",
             lambda d: write(d, "node_modules/mydep/index.d.ts",
                             'export declare function depFn(x: string): string;\n')),
        ],
    }


@scenario("tsconfig-edit")
def s_tsconfig():
    return {
        "args": ["-w", "--pretty", "false"],
        "setup": setup_simple,
        "steps": [
            ("edit tsconfig.json (add strict)",
             lambda d: write(d, "tsconfig.json",
                             '{"compilerOptions":{"rootDir":"src","outDir":"dist",'
                             '"strict":true},"include":["src"]}\n')),
        ],
    }


@scenario("build-watch-2proj")
def s_bwatch():
    return {
        "args": ["-b", "app", "-w"],
        "setup": setup_bproj,
        "steps": [
            ("edit upstream common/src/lib.ts",
             lambda d: write(d, "common/src/lib.ts",
                             'export function add(a: number, b: number): number {'
                             ' return a + b + 1; }\n')),
            ("edit downstream app/src/main.ts",
             lambda d: write(d, "app/src/main.ts",
                             'import { add } from "../../common/dist/lib";\n'
                             'console.log(add(10, 20));\n')),
        ],
    }


@scenario("config-option-diag-location")
def s_cfg_diag():
    """A removed-option diagnostic must render with tsconfig file+position
    (`tsconfig.json(1,92): error TS5108`), not a bare compiler diagnostic."""
    def setup(d):
        write(d, "tsconfig.json",
              '{"compilerOptions":{"rootDir":"src","outDir":"dist",'
              '"module":"commonjs","moduleResolution":"node10"},'
              '"include":["src"]}\n')
        write(d, "src/main.ts", MAIN_TS)
    return {
        "args": ["-w", "--pretty", "false"],
        "setup": setup,
        "steps": [],
    }


@scenario("rapid-successive-edits")
def s_rapid():
    def burst(d):
        # 5 appends ~250ms apart: exercises debounce/coalescing.
        for i in range(5):
            append(d, "src/main.ts", f"export const k{i}: number = {i};\n")
            time.sleep(0.25)
    return {
        "args": ["-w", "--pretty", "false"],
        "setup": setup_simple,
        "steps": [("5 rapid appends", burst)],
        "compare_output": False,  # cycle counts legitimately differ
    }


# ---------------------------------------------------------------------------
# driver
# ---------------------------------------------------------------------------

def run_side(bin_kind, binary, args, spec, workdir):
    """Run one binary through one scenario. Returns
    (segments, tree_per_step, transcript, status)."""
    proj = os.path.join(workdir, bin_kind)
    shutil.rmtree(proj, ignore_errors=True)
    os.makedirs(proj)
    spec["setup"](proj)

    argv = [binary] + (["tsc"] if bin_kind == "cpp" else []) + args
    proc = WatchProc(argv, cwd=proj)
    t0 = time.monotonic()

    segments = []     # normalized output per marker-bounded step
    trees = []        # tree snapshot per step
    status = "OK"
    prev_end = 0

    def take_segment():
        nonlocal prev_end
        raw = proc.text()[prev_end:]
        # segment ends at the last marker occurrence in raw
        idx = raw.rfind(IDLE_MARKER)
        seg = raw[:idx + len(IDLE_MARKER)] if idx >= 0 else raw
        prev_end = proc.text().find(seg) + len(seg) if seg else len(proc.text())
        return norm(seg, proj)

    ok = proc.wait_marker(1, timeout=STEP_TIMEOUT)
    segments.append(take_segment() if ok else norm(proc.text(), proj))
    trees.append(tree_map(proj))
    if not ok:
        status = f"TIMEOUT initial marker ({args})"
    else:
        for si, (label, mutate) in enumerate(spec["steps"], start=1):
            before = proc.marker_count()
            mutate(proj)
            ok = proc.wait_marker(before + 1, timeout=STEP_TIMEOUT)
            segments.append(take_segment() if ok
                            else norm(proc.text(), proj))
            trees.append(tree_map(proj))
            if not ok:
                status = f"TIMEOUT step {si} ({label})"
                break

    elapsed = time.monotonic() - t0
    proc.kill()
    return segments, trees, proc.text(), status, elapsed


def run_scenario(name, spec, go_bin, cpp_bin, workdir, verbose):
    res = {"name": name, "steps": [], "status": "PASS"}
    for side, binary in (("go", go_bin), ("cpp", cpp_bin)):
        segs, trees, transcript, status, elapsed = run_side(
            side, binary, spec["args"], spec, workdir)
        res[side] = {"segs": segs, "trees": trees,
                     "transcript": transcript, "status": status,
                     "elapsed": elapsed}
    go, cpp = res["go"], res["cpp"]

    nsteps = max(len(go["segs"]), len(cpp["segs"]))
    for i in range(nsteps):
        entry = {"step": i, "ok": True, "detail": []}
        gseg = go["segs"][i] if i < len(go["segs"]) else "<missing>"
        cseg = cpp["segs"][i] if i < len(cpp["segs"]) else "<missing>"
        if spec.get("compare_output", True) and gseg != cseg:
            entry["ok"] = False
            entry["detail"].append("output differs")
            entry["gseg"] = gseg
            entry["cseg"] = cseg
        if i < len(go["trees"]) and i < len(cpp["trees"]):
            td = tree_diff(go["trees"][i], cpp["trees"][i])
            if td:
                entry["ok"] = False
                entry["detail"].append("tree differs")
                entry["tree_diff"] = td
        if not entry["ok"]:
            res["status"] = "DIFF"
        res["steps"].append(entry)

    for side in ("go", "cpp"):
        if res[side]["status"] != "OK":
            res["status"] = "DIFF"
    if verbose:
        for st in res["steps"]:
            if "gseg" in st:
                sys.stdout.write(
                    "\n".join(difflib.unified_diff(
                        st["gseg"].split("\n"), st["cseg"].split("\n"),
                        "go", "cpp", lineterm="")))
                sys.stdout.write("\n")
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--go", default="/tmp/tsgo")
    ap.add_argument("--cpp", default="/tmp/tscpp_probe")
    ap.add_argument("--work", default="/tmp/watch_parity")
    ap.add_argument("--scenario", default=None)
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    for b in (a.go, a.cpp):
        if not os.path.exists(b):
            sys.exit(f"missing binary: {b}")

    os.makedirs(a.work, exist_ok=True)
    results = []
    for name, fn in SCENARIOS:
        if a.scenario and a.scenario != name:
            continue
        spec = fn()
        res = run_scenario(name, spec, a.go, a.cpp,
                           os.path.join(a.work, name), a.verbose)
        results.append(res)
        ok_steps = sum(1 for s in res["steps"] if s["ok"])
        print(f"{res['status']:4} {name:24} steps {ok_steps}/{len(res['steps'])}"
              f" go={res['go']['status']} cpp={res['cpp']['status']}")
        for st in res["steps"]:
            if not st["ok"]:
                for d in st["detail"]:
                    print(f"     step {st['step']}: {d}")
                for d in st.get("tree_diff", []):
                    print(f"       {d}")

    npass = sum(1 for r in results if r["status"] == "PASS")
    print(f"\n{npass}/{len(results)} scenarios PASS")
    return 0 if npass == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
