#!/bin/bash
# conformance_corpus.sh — build the test-file corpus list and run
# conformance_parse.sh across it in parallel.
#
# Usage:
#   ./conformance_corpus.sh [jobs]
#
# Produces:
#   /tmp/tscpp_corpus.list     all candidate files
#   /tmp/tscpp_results.txt     PASS/FAIL per file
#   /tmp/tscpp_fails.txt       the failing subset
set -u
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
JOBS="${1:-8}"
cd "$REPO_ROOT"

find tsc/testdata -type f \( -name '*.ts' -o -name '*.tsx' -o -name '*.js' \
  -o -name '*.jsx' -o -name '*.mts' -o -name '*.cts' -o -name '*.mjs' \
  -o -name '*.cjs' \) \
  | grep -v '/node_modules/' \
  | grep -v '^tsc/testdata/baselines/reference/project/nodeModules' \
  | grep -v '^tsc/tools/' \
  | sort > /tmp/tscpp_corpus.list

echo "corpus: $(wc -l < /tmp/tscpp_corpus.list | tr -d ' ') files"
xargs -P "$JOBS" -I{} "$REPO_ROOT/cpp/tools/conformance_parse.sh" {} \
  < /tmp/tscpp_corpus.list > /tmp/tscpp_results.txt
grep FAIL /tmp/tscpp_results.txt > /tmp/tscpp_fails.txt || true
echo "PASS: $(grep -c PASS /tmp/tscpp_results.txt)  FAIL: $(wc -l < /tmp/tscpp_fails.txt | tr -d ' ')"
cat /tmp/tscpp_fails.txt
