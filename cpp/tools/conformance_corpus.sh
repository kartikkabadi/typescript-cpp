#!/bin/bash
# conformance_corpus.sh — build the test-file corpus list and run
# conformance_parse.sh across it in parallel.
#
# Usage:
#   ./conformance_corpus.sh [jobs]
#
# Produces (under RESULTDIR, default /tmp):
#   tscpp_corpus.list     all candidate files
#   tscpp_results.txt     PASS/FAIL per file
#   tscpp_fails.txt       the failing subset
set -u
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
JOBS="${1:-8}"
RESULTDIR="${RESULTDIR:-/tmp}"
mkdir -p "$RESULTDIR" || exit 2
RESULTDIR="$(cd "$RESULTDIR" && pwd)" || exit 2
cd "$REPO_ROOT"

find tsc/testdata -type f \( -name '*.ts' -o -name '*.tsx' -o -name '*.js' \
  -o -name '*.jsx' -o -name '*.mts' -o -name '*.cts' -o -name '*.mjs' \
  -o -name '*.cjs' \) \
  | grep -v '/node_modules/' \
  | grep -v '^tsc/testdata/baselines/reference/project/nodeModules' \
  | grep -v '^tsc/tools/' \
  | sort > "$RESULTDIR/tscpp_corpus.list"

if [ ! -s "$RESULTDIR/tscpp_corpus.list" ]; then
  echo "empty corpus list — refusing to report success over nothing" >&2
  exit 2
fi

echo "corpus: $(wc -l < "$RESULTDIR/tscpp_corpus.list" | tr -d ' ') files"
xargs_rc=0
xargs -P "$JOBS" -I{} "$REPO_ROOT/cpp/tools/conformance_parse.sh" {} \
  < "$RESULTDIR/tscpp_corpus.list" > "$RESULTDIR/tscpp_results.txt" || xargs_rc=$?
grep FAIL "$RESULTDIR/tscpp_results.txt" > "$RESULTDIR/tscpp_fails.txt" || true
echo "PASS: $(grep -c PASS "$RESULTDIR/tscpp_results.txt")  FAIL: $(wc -l < "$RESULTDIR/tscpp_fails.txt" | tr -d ' ')"
cat "$RESULTDIR/tscpp_fails.txt"
if [ "$xargs_rc" -ne 0 ]; then
  echo "worker(s) exited without reporting (xargs status $xargs_rc) — infrastructure failure, not a clean corpus" >&2
  exit 2
fi
[ ! -s "$RESULTDIR/tscpp_fails.txt" ]
