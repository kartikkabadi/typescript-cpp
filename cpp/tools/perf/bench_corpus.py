#!/usr/bin/env python3
"""Whole-corpus sequential check over the sampled file list.

One fresh compiler process per file (xargs -n1 equivalent, sequential).
RUNS repetitions per side after a warmup pass; reports median totals.
Writes results/corpus.json.
"""
import json
import os
import statistics
import subprocess
import time

from lib import (CHECK_FLAGS, RESULTS, REPO, TSCPP, TSGO, hardware_info)

RUNS = int(os.environ.get("RUNS", "3"))
CORPUS = os.environ.get(
    "CORPUS", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "corpus500.txt"))


def run_list(files, side):
    t0 = time.monotonic()
    for rel in files:
        cmd = ([TSGO, rel] if side == "go" else [TSCPP, "tsc", rel])
        cmd += CHECK_FLAGS
        subprocess.run(cmd, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, cwd=REPO)
    return time.monotonic() - t0


def main():
    with open(CORPUS if os.path.isabs(CORPUS)
              else os.path.join(REPO, CORPUS)) as f:
        files = [l.strip() for l in f if l.strip()]
    out = {"files": len(files), "runs": RUNS, "flags": CHECK_FLAGS,
           "hardware": hardware_info()}
    for side in ("go", "cpp"):
        run_list(files, side)  # warmup
        times = [run_list(files, side) for _ in range(RUNS)]
        print(f"{side}: {[f'{t:.1f}' for t in times]}", flush=True)
        out[f"{side}_times_s"] = times
        out[f"{side}_median_s"] = statistics.median(times)
    out["ratio"] = out["cpp_median_s"] / out["go_median_s"]
    os.makedirs(RESULTS, exist_ok=True)
    with open(os.path.join(RESULTS, "corpus.json"), "w") as f:
        json.dump(out, f, indent=2)
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
