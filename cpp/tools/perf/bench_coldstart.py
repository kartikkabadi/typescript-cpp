#!/usr/bin/env python3
"""Cold-start overhead: `tsc --version` and a 10-line file check.

RUNS (default 20) fresh process spawns per side, median wall time.
Writes results/coldstart.json.
"""
import json
import os
import statistics
import subprocess
import tempfile
import time

from lib import (RESULTS, TSCPP, TSGO, hardware_info)

RUNS = int(os.environ.get("RUNS", "20"))

TINY = """\
const x: number = 1;
function id<T>(v: T): T { return v; }
class A { m(): string { return "a"; } }
const y = id<number>(x);
enum E { A, B }
interface I { a: number }
const z: I = { a: y };
type U = string | number;
let w: U = z.a;
console.log(w, new A().m(), E.B);
"""


def timed(cmd):
    t0 = time.monotonic()
    subprocess.run(cmd, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    return time.monotonic() - t0


def main():
    with tempfile.NamedTemporaryFile("w", suffix=".ts", delete=False) as f:
        f.write(TINY)
        tiny = f.name

    out = {"runs": RUNS, "hardware": hardware_info()}
    surfaces = {
        "version": lambda side: ([TSGO] if side == "go"
                                 else [TSCPP, "tsc"]) + ["--version"],
        "tiny_file": lambda side: (([TSGO, tiny] if side == "go"
                                    else [TSCPP, "tsc", tiny])
                                   + ["--noEmit", "--pretty", "false"]),
    }
    for name, mk in surfaces.items():
        entry = {}
        for side in ("go", "cpp"):
            cmd = mk(side)
            timed(cmd)  # warmup
            times = [timed(cmd) for _ in range(RUNS)]
            print(f"{name} {side}: median={statistics.median(times)*1000:.1f}ms",
                  flush=True)
            entry[f"{side}_times_s"] = times
            entry[f"{side}_median_ms"] = statistics.median(times) * 1000
            entry[f"{side}_mean_ms"] = statistics.fmean(times) * 1000
        entry["ratio"] = entry["cpp_median_ms"] / entry["go_median_ms"]
        out[name] = entry
    os.makedirs(RESULTS, exist_ok=True)
    with open(os.path.join(RESULTS, "coldstart.json"), "w") as f:
        json.dump(out, f, indent=2)
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
