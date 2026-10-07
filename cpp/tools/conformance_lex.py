#!/usr/bin/env python3
"""Diff tscpp lex-json output against the Go oracle (tsc/cmd/lexdump)."""
import json
import subprocess
import sys
import os
import concurrent.futures

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TSCPP = os.path.join(ROOT, "cpp", "build", "tscpp")
ORACLE = os.environ.get("LEXDUMP", "/tmp/lexdump")


def run_one(path):
    try:
        a = subprocess.run([TSCPP, "lex-json", path], capture_output=True, timeout=60)
        b = subprocess.run([ORACLE, path], capture_output=True, timeout=60)
    except subprocess.TimeoutExpired:
        return path, "TIMEOUT", ""
    if a.returncode != 0 or b.returncode != 0:
        return path, f"rc={a.returncode}/{b.returncode}", ""
    ta, tb = a.stdout.strip(), b.stdout.strip()
    if ta == tb:
        return path, "MATCH", ""
    try:
        ja, jb = json.loads(ta), json.loads(tb)
    except Exception as e:
        return path, f"parse-fail {e}", ""
    n = min(len(ja), len(jb))
    for i in range(n):
        if ja[i] != jb[i]:
            return path, f"DIFF tok#{i}", f"  cpp : {ja[i]}\n  go  : {jb[i]}\n  ctx : {open(path,'rb').read()[max(0,ja[i]['pos']-40):ja[i]['end']+40]!r}"
    return path, f"LEN {len(ja)} vs {len(jb)}", f"  cpp-last: {ja[n-1] if n else '-'}\n  go-last : {jb[n-1] if n else '-'}"


def main():
    dirs = sys.argv[1:] or ["tsc/testdata/tests/cases"]
    files = []
    for d in dirs:
        d = os.path.join(ROOT, d)
        for base, _, names in os.walk(d):
            for n in names:
                if n.endswith((".ts", ".tsx", ".mts", ".cts", ".js", ".jsx")):
                    files.append(os.path.join(base, n))
    files.sort()
    print(f"{len(files)} files", file=sys.stderr)
    if not files:
        print("empty corpus — refusing to report success over nothing", file=sys.stderr)
        sys.exit(2)
    bad = 0
    shown = 0
    with concurrent.futures.ThreadPoolExecutor(16) as ex:
        for path, status, detail in ex.map(run_one, files):
            if status != "MATCH":
                bad += 1
                if shown < 25:
                    rel = os.path.relpath(path, ROOT)
                    print(f"{status:12} {rel}")
                    if detail:
                        print(detail)
                    shown += 1
    print(f"\n{bad}/{len(files)} mismatched", file=sys.stderr)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
