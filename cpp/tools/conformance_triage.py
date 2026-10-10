#!/usr/bin/env python3
"""conformance_triage.py — bucket mismatches between `tscpp check` and the
Go `checkdump` oracle across the corpus.

Usage:
    conformance_triage.py <listfile> <jobs>

Assumes oracle output was precomputed into /tmp/oracle/<md5>.txt by
run_oracle.sh (same md5 keying). Writes tscpp output to /tmp/tscpp_out/<md5>.txt
(skip existing) and prints a summary grouped by failure signature:

    CRASH                — tscpp exited non-zero or printed "internal error"
    MISSING-DIAG <code>  — oracle has T/C lines the cpp side lacks
    EXTRA-DIAG <code>    — cpp side has lines the oracle lacks
    POS-DIFF <code>      — same code but different pos/end
    COUNT-DIFF <code>    — same code different cardinality
    OTHER                — file/global line diffs not covered above
"""

import hashlib
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TSCPP = os.environ.get("TSCPP", os.path.join(REPO, "cpp", "build", "tscpp"))
ORACLE_DIR = os.environ.get("ORACLE_DIR", "/tmp/oracle")
TSCPP_DIR = os.environ.get("TSCPP_DIR", "/tmp/tscpp_out")
os.makedirs(TSCPP_DIR, exist_ok=True)


def key(path):
    return hashlib.md5(path.encode()).hexdigest()[:12]


def run_tscpp(f):
    k = key(f)
    out = os.path.join(TSCPP_DIR, k + ".txt")
    if not os.path.exists(out):
        try:
            r = subprocess.run(
                [TSCPP, "check", f],
                cwd=REPO, capture_output=True, text=True, timeout=120)
            data = r.stdout
            if r.returncode != 0 and not data:
                data = "EXIT " + str(r.returncode) + " " + r.stderr[:200]
        except subprocess.TimeoutExpired:
            data = "TIMEOUT"
        with open(out, "w") as fh:
            fh.write(data)
    with open(out) as fh:
        return fh.read()


def classify(go, cpp):
    if cpp.startswith("EXIT") or cpp.startswith("TIMEOUT") or \
       "internal error" in cpp:
        return "CRASH", cpp[:160]
    gl = [l for l in go.splitlines() if l[:1] in "GTCF"]
    cl = [l for l in cpp.splitlines() if l[:1] in "GTCF"]
    if gl == cl:
        return None, ""
    missing = [l for l in gl if l not in cl]
    extra = [l for l in cl if l not in gl]
    if missing and not extra:
        codes = {l.split()[1] for l in missing if l.startswith("T ")}
        return "MISSING-DIAG " + ",".join(sorted(codes)[:3]), ""
    if extra and not missing:
        codes = {l.split()[1] for l in extra if l.startswith("T ")}
        return "EXTRA-DIAG " + ",".join(sorted(codes)[:3]), ""
    gcodes = {}
    ccodes = {}
    for l in gl:
        if l.startswith("T "):
            gcodes.setdefault(l.split()[1], []).append(l)
    for l in cl:
        if l.startswith("T "):
            ccodes.setdefault(l.split()[1], []).append(l)
    if set(gcodes) == set(ccodes):
        diff = [c for c in gcodes if sorted(gcodes[c]) != sorted(ccodes[c])]
        return "POS-DIFF " + ",".join(sorted(diff)[:3]), ""
    return "OTHER", ""


def one(f):
    f = f.strip()
    if not f:
        return None
    k = key(f)
    opath = os.path.join(ORACLE_DIR, k + ".txt")
    go = open(opath).read() if os.path.exists(opath) else "NO-ORACLE"
    cpp = run_tscpp(f)
    sig, detail = classify(go, cpp)
    if sig is None:
        return "PASS", f
    return sig, f, detail


def main():
    listfile, jobs = sys.argv[1], int(sys.argv[2])
    files = [l.strip() for l in open(listfile) if l.strip()]
    buckets = {}
    fails = {}
    done = 0
    with ThreadPoolExecutor(jobs) as ex:
        for res in ex.map(one, files):
            if res is None:
                continue
            if res[0] == "PASS":
                done += 1
                continue
            sig, f = res[0], res[1]
            buckets[sig] = buckets.get(sig, 0) + 1
            fails.setdefault(sig, []).append(f)
            done += 1
            if done % 500 == 0:
                print(f"... {done}/{len(files)}", file=sys.stderr)
    npass = len(files) - sum(buckets.values())
    print(f"PASS {npass}/{len(files)} ({100*npass/len(files):.1f}%)")
    for sig in sorted(buckets, key=lambda s: -buckets[sig]):
        print(f"{buckets[sig]:6}  {sig}")
        for f in fails[sig][:5]:
            print(f"        {f}")


if __name__ == "__main__":
    main()
