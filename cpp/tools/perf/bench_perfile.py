#!/usr/bin/env python3
"""Per-file check benchmark: tsgo <f> --noEmit vs tscpp tsc <f> --noEmit.

For each file: 1 warmup run per side (also captures output for a
diagnostics-equality diff), then TIMED_RUNS timed runs per side; the
median is reported. Writes results/perfile.csv + results/perfile_summary.json.
"""
import csv
import json
import os
import sys

from lib import (CHECK_FLAGS, RESULTS, REPO, TSCPP, TSGO, hardware_info,
                 p95, run_capture, run_timed, stats, tscpp_cmd, tsgo_cmd)

TIMED_RUNS = int(os.environ.get("TIMED_RUNS", "3"))
CORPUS = os.environ.get(
    "CORPUS", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "corpus500.txt"))


def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else None
    with open(os.path.join(REPO, CORPUS) if not os.path.isabs(CORPUS)
              else CORPUS) as f:
        files = [l.strip() for l in f if l.strip()]
    if limit:
        files = files[:limit]

    rows = []
    for i, rel in enumerate(files):
        size = os.path.getsize(os.path.join(REPO, rel))
        # Warmup + output capture for the equality diff (untimed).
        _, gcode, gout = run_capture(tsgo_cmd(rel))
        _, ccode, cout = run_capture(tscpp_cmd(rel))
        diag_match = (gcode == ccode) and (gout == cout)

        gt = [run_timed(tsgo_cmd(rel)) for _ in range(TIMED_RUNS)]
        ct = [run_timed(tscpp_cmd(rel)) for _ in range(TIMED_RUNS)]
        row = {
            "file": rel,
            "bytes": size,
            "go_exit": gcode,
            "cpp_exit": ccode,
            "diag_match": int(diag_match),
            "go_median_s": sorted(gt)[len(gt) // 2],
            "cpp_median_s": sorted(ct)[len(ct) // 2],
        }
        row["ratio"] = row["cpp_median_s"] / row["go_median_s"]
        rows.append(row)
        print(f"[{i + 1}/{len(files)}] {rel}: go={row['go_median_s']:.3f}s "
              f"cpp={row['cpp_median_s']:.3f}s r={row['ratio']:.2f} "
              f"match={diag_match}", flush=True)

    os.makedirs(RESULTS, exist_ok=True)
    csv_path = os.path.join(RESULTS, "perfile.csv")
    with open(csv_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    gm = [r["go_median_s"] for r in rows]
    cm = [r["cpp_median_s"] for r in rows]
    ratios = [r["ratio"] for r in rows]
    summary = {
        "files": len(rows),
        "timed_runs_per_side": TIMED_RUNS,
        "flags": CHECK_FLAGS,
        "tsgo": TSGO,
        "tscpp": TSCPP,
        "hardware": hardware_info(),
        "diag_match_count": sum(r["diag_match"] for r in rows),
        "diag_mismatch_files": [r["file"] for r in rows
                                if not r["diag_match"]],
        "go_perfile_median_s": stats(gm)["median_s"],
        "go_perfile_mean_s": stats(gm)["mean_s"],
        "go_perfile_p95_s": stats(gm)["p95_s"],
        "cpp_perfile_median_s": stats(cm)["median_s"],
        "cpp_perfile_mean_s": stats(cm)["mean_s"],
        "cpp_perfile_p95_s": stats(cm)["p95_s"],
        "ratio_of_medians": stats(cm)["median_s"] / stats(gm)["median_s"],
        "median_ratio": sorted(ratios)[len(ratios) // 2],
        "p95_ratio": p95(ratios),
        "worst20": sorted(rows, key=lambda r: -r["ratio"])[:20],
    }
    with open(os.path.join(RESULTS, "perfile_summary.json"), "w") as f:
        json.dump(summary, f, indent=2)
    print(json.dumps({k: v for k, v in summary.items()
                      if k != "worst20"}, indent=2))


if __name__ == "__main__":
    main()
