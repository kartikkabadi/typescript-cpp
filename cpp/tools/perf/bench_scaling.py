#!/usr/bin/env python3
"""Scaling: same project check at 1/10/50/100 files (tsconfig_N.json).

RUNS timed runs per size per side after a warmup; medians reported.
Fits time = a*N^2 + b*N + c and reports per-N ratios.
Writes results/scaling.json.
"""
import json
import os
import statistics
import subprocess
import time

from lib import (RESULTS, TSCPP, TSGO, hardware_info)

RUNS = int(os.environ.get("RUNS", "3"))
PROJ = os.environ.get("PROJ", "/tmp/perfproj")
SIZES = [int(x) for x in os.environ.get("SIZES", "1,10,50,100").split(",")]


def run(side, cfg):
    cmd = ([TSGO] if side == "go" else [TSCPP, "tsc"])
    cmd += ["-p", cfg, "--noEmit", "--pretty", "false"]
    t0 = time.monotonic()
    subprocess.run(cmd, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, cwd=PROJ)
    return time.monotonic() - t0


def main():
    out = {"proj": PROJ, "runs": RUNS, "sizes": SIZES,
           "hardware": hardware_info(), "per_size": {}}
    for n in SIZES:
        cfg = f"tsconfig_{n}.json"
        entry = {}
        for side in ("go", "cpp"):
            run(side, cfg)  # warmup
            times = [run(side, cfg) for _ in range(RUNS)]
            print(f"N={n} {side}: {[f'{t:.3f}' for t in times]}",
                  flush=True)
            entry[f"{side}_median_s"] = statistics.median(times)
            entry[f"{side}_times_s"] = times
        entry["ratio"] = entry["cpp_median_s"] / entry["go_median_s"]
        entry["go_ms_per_file"] = entry["go_median_s"] * 1000 / n
        entry["cpp_ms_per_file"] = entry["cpp_median_s"] * 1000 / n
        out["per_size"][str(n)] = entry
    os.makedirs(RESULTS, exist_ok=True)
    with open(os.path.join(RESULTS, "scaling.json"), "w") as f:
        json.dump(out, f, indent=2)
    print(json.dumps(out["per_size"], indent=2))


if __name__ == "__main__":
    main()
