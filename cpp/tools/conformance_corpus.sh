#!/bin/bash
# conformance_corpus.sh — build the test-file corpus list and run
# conformance_parse.sh across it in parallel.
#
# Usage:
#   ./conformance_corpus.sh [jobs]
#
# Produces:
#   $RESULTDIR/tscpp_corpus.list     all candidate files
#   $RESULTDIR/tscpp_results.txt     PASS/FAIL per file
#   $RESULTDIR/tscpp_fails.txt       the failing subset
set -u
set -o pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
JOBS="${1:-8}"
RESULTDIR="${RESULTDIR:-$(mktemp -d /tmp/tscpp-conformance.XXXXXX)}"
mkdir -p "$RESULTDIR" || exit 2
cd "$REPO_ROOT"

find tsc/testdata -type f \( -name '*.ts' -o -name '*.tsx' -o -name '*.js' \
  -o -name '*.jsx' -o -name '*.mts' -o -name '*.cts' -o -name '*.mjs' \
  -o -name '*.cjs' \) \
  | grep -v '/node_modules/' \
  | grep -v '^tsc/testdata/baselines/reference/project/nodeModules' \
  | grep -v '^tsc/tools/' \
  | sort > "$RESULTDIR/tscpp_corpus.list" || exit 2

count=$(wc -l < "$RESULTDIR/tscpp_corpus.list" | tr -d ' ')
if [ "$count" -eq 0 ]; then
  echo "empty corpus" >&2
  exit 2
fi
echo "corpus: $count files; results: $RESULTDIR"
xargs -P "$JOBS" -I{} "$REPO_ROOT/cpp/tools/conformance_parse.sh" {} \
  < "$RESULTDIR/tscpp_corpus.list" > "$RESULTDIR/tscpp_results.txt"
rc=$?
grep '^FAIL ' "$RESULTDIR/tscpp_results.txt" > "$RESULTDIR/tscpp_fails.txt" || true
passed=$(grep -c '^PASS ' "$RESULTDIR/tscpp_results.txt")
echo "PASS: $passed  FAIL: $(wc -l < "$RESULTDIR/tscpp_fails.txt" | tr -d ' ')"
cat "$RESULTDIR/tscpp_fails.txt"
[ "$rc" -eq 0 ] && [ "$passed" -eq "$count" ]
