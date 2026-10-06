#!/usr/bin/env python3
"""Realistic-project benchmark on the generated ~100-file project.

Surfaces: `tsc -p` (js emit), `tsc -p --noEmit`, `tsc -p --declaration`.
RUNS timed runs per side after 1 warmup; reports median wall time.
Writes results/project.json.
"""
import json
import os
import shutil
import statistics
import subprocess
import sys
import time

from lib import (RESULTS, TSCPP, TSGO, hardware_info)

RUNS = int(os.environ.get("RUNS", "5"))
PROJ = os.environ.get("PROJ", "/tmp/perfproj")


def run_one(side, extra):
    outdir = f"/tmp/perfproj/out_{side}"
    if os.path.isdir(outdir):
        shutil.rmtree(outdir)
    cmd = ([TSGO] if side == "go" else [TSCPP, "tsc"])
    cmd += ["-p", "tsconfig.json", "--pretty", "false",
            "--outDir", outdir] + extra
    t0 = time.monotonic()
    p = subprocess.run(cmd, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, cwd=PROJ)
    return time.monotonic() - t0, p.returncode


def main():
    variants = {
        "emit": [],
        "noemit": ["--noEmit"],
        "decl": ["--declaration", "--emitDeclarationOnly"],
    }
    out = {"proj": PROJ, "runs": RUNS, "hardware": hardware_info(),
           "variants": {}}
    for name, extra in variants.items():
        entry = {}
        for side in ("go", "cpp"):
            rc = run_one(side, extra)[1]  # warmup
            if rc != 0:
                print(f"WARN {side} {name} warmup exit={rc}")
            times = [run_one(side, extra)[0] for _ in range(RUNS)]
            print(f"{name} {side}: {[f'{t:.2f}' for t in times]}",
                  flush=True)
            entry[f"{side}_times_s"] = times
            entry[f"{side}_median_s"] = statistics.median(times)
        entry["ratio"] = entry["cpp_median_s"] / entry["go_median_s"]
        out["variants"][name] = entry
    os.makedirs(RESULTS, exist_ok=True)
    with open(os.path.join(RESULTS, "project.json"), "w") as f:
        json.dump(out, f, indent=2)
    print(json.dumps({k: v["ratio"] for k, v in
                      out["variants"].items()}, indent=2))


if __name__ == "__main__":
    main()
