#!/usr/bin/env python3
"""emit_triage.py — bucket mismatches between `tscpp emitdump` and the Go
`emitdump` oracle across the corpus.

Usage:
    emit_triage.py <listfile> <jobs>

Oracle output is precomputed into EMIT_ORACLE_DIR/<md5>.txt by
run_emit_oracle.sh (same md5 keying as conformance_triage.py). Writes tscpp
emit output to /tmp/tscpp_emit_out/<md5>.txt (skips existing) and prints a
summary grouped by failure signature:

    CRASH                — tscpp exited non-zero or printed "internal error"
    DIAG-DIFF            — G/F/T lines differ (any direction)
    MISSING-FILE         — oracle wrote a W section tscpp didn't
    EXTRA-FILE           — tscpp wrote a W section the oracle didn't
    CONTENT-DIFF <file>  — same file written, different bytes
"""

import hashlib
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TSCPP = os.environ.get("TSCPP", os.path.join(REPO, "cpp", "build", "tscpp"))
ORACLE_DIR = os.environ.get("EMIT_ORACLE_DIR", "/tmp/emit_oracle")
TSCPP_DIR = os.environ.get("TSCPP_EMIT_DIR", "/tmp/tscpp_emit_out")
EXTRA_FLAGS = os.environ.get("EMIT_FLAGS", "").split()
os.makedirs(TSCPP_DIR, exist_ok=True)


def key(path):
    return hashlib.md5(path.encode()).hexdigest()[:12]


def run_tscpp(f):
    k = key(f)
    out = os.path.join(TSCPP_DIR, k + ".txt")
    if not os.path.exists(out):
        try:
            r = subprocess.run(
                [TSCPP, "emitdump", f] + EXTRA_FLAGS,
                cwd=REPO, capture_output=True, timeout=120)
            data = r.stdout.decode("utf-8", errors="replace")
            if r.returncode != 0 and not data:
                data = "EXIT " + str(r.returncode) + " " + r.stderr.decode(
                    "utf-8", errors="replace")[:200]
        except subprocess.TimeoutExpired:
            data = "TIMEOUT"
        with open(out, "w") as fh:
            fh.write(data)
    with open(out) as fh:
        return fh.read()


def sections(text):
    """Split a dump into its diagnostic part (G/F/T/C lines) and W sections:
    {name: content}."""
    diags, w = [], {}
    cur = None
    for line in text.splitlines(keepends=True):
        m = re.match(r'^W (.+)$', line)
        if m:
            cur = m.group(1).rstrip("\n")
            w[cur] = ""
            continue
        if cur is not None and not re.match(r'^[GFTC] ', line):
            w[cur] += line
            continue
        cur = None
        diags.append(line)
    return diags, w


def classify(go, cpp):
    if cpp.startswith("EXIT") or cpp.startswith("TIMEOUT") or "internal error" in cpp:
        return "CRASH", ""
    gd, gw = sections(go)
    cd, cw = sections(cpp)
    if set(gw) - set(cw):
        return "MISSING-FILE", ",".join(sorted(set(gw) - set(cw))[:3])
    if set(cw) - set(gw):
        return "EXTRA-FILE", ",".join(sorted(set(cw) - set(gw))[:3])
    if gd != cd:
        return "DIAG-DIFF", ""
    for name in gw:
        if gw[name] != cw[name]:
            return "CONTENT-DIFF", name
    return None, ""


def one(f):
    f = f.strip()
    if not f:
        return None
    k = key(f)
    opath = os.path.join(ORACLE_DIR, k + ".txt")
    go = open(opath, errors="replace").read() if os.path.exists(opath) else "NO-ORACLE"
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
            fails.setdefault(sig, []).append((f, res[2] if len(res) > 2 else ""))
            done += 1
            if done % 500 == 0:
                print(f"... {done}/{len(files)}", file=sys.stderr)
    npass = len(files) - sum(buckets.values())
    print(f"PASS {npass}/{len(files)} ({100*npass/len(files):.1f}%)")
    for sig in sorted(buckets, key=lambda s: -buckets[s]):
        print(f"{buckets[sig]:6}  {sig}")
        for f, d in fails[sig][:5]:
            print(f"        {f}  {d}")


if __name__ == "__main__":
    main()
