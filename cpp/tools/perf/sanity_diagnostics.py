#!/usr/bin/env python3
"""Diagnostic-equality sanity check: diff combined stdout/stderr of
`tsgo <f> --noEmit --pretty false` vs `tscpp tsc <f> --noEmit --pretty false`
on the first K files of the corpus list (default 20).

Writes results/sanity.json and prints a verdict. Exit 0 = all identical.
"""
import difflib
import json
import os
import sys

from lib import (RESULTS, REPO, run_capture, tscpp_cmd, tsgo_cmd)

K = int(sys.argv[1]) if len(sys.argv) > 1 else 20
CORPUS = os.environ.get(
    "CORPUS", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "corpus500.txt"))


def main():
    with open(CORPUS if os.path.isabs(CORPUS)
              else os.path.join(REPO, CORPUS)) as f:
        files = [l.strip() for l in f if l.strip()][:K]
    same, clean, diffs = 0, 0, []
    for rel in files:
        _, gc, gout = run_capture(tsgo_cmd(rel))
        _, cc, cout = run_capture(tscpp_cmd(rel))
        if gc == cc and gout == cout:
            if gout == b"":
                clean += 1
            else:
                same += 1
        else:
            diffs.append({
                "file": rel,
                "go_exit": gc, "cpp_exit": cc,
                "diff_head": "\n".join(list(difflib.unified_diff(
                    gout.decode(errors="replace").splitlines(),
                    cout.decode(errors="replace").splitlines(),
                    lineterm=""))[:12]),
            })
    out = {"checked": len(files), "identical_with_output": same,
           "identical_clean": clean, "divergent": diffs}
    os.makedirs(RESULTS, exist_ok=True)
    with open(os.path.join(RESULTS, "sanity.json"), "w") as f:
        json.dump(out, f, indent=2)
    print(f"{same + clean}/{len(files)} identical "
          f"({clean} clean, {same} with identical diagnostics), "
          f"{len(diffs)} divergent")
    for d in diffs:
        print("DIVERGENT:", d["file"])
    sys.exit(0 if not diffs else 1)


if __name__ == "__main__":
    main()
