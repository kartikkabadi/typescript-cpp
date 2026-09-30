#!/bin/bash
# conformance_parse.sh — byte-identical AST/diagnostics conformance check
# for a single file: runs `tscpp parse` (C++ port) and `parsedump` (Go
# oracle) on the same file and diffs the full dumps.
#
# Usage:
#   ./conformance_parse.sh <repo-relative-file>
#   cat filelist | xargs -P 8 -I{} ./conformance_parse.sh {} > results.txt
#
# Output: "PASS <file>" or "FAIL <file>". On failure the two dumps are
# kept at $DIFFDIR/{cpp,go}_<basename> for inspection.
set -u
if [ "$#" -ne 1 ]; then
  echo "usage: $0 <file>" >&2
  exit 2
fi
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TSCPP="${TSCPP:-$REPO_ROOT/cpp/build/tscpp}"
PARSEDUMP="${PARSEDUMP:-$REPO_ROOT/tsc/parsedump}"
DIFFDIR="${DIFFDIR:-/tmp}"
TIMEOUT="${TIMEOUT:-60}"

f="$1"
cpp_out="$(mktemp /tmp/cppout.XXXXXX)" || exit 2
go_out="$(mktemp /tmp/goout.XXXXXX)" || { rm -f "$cpp_out"; exit 2; }
trap 'rm -f "$cpp_out" "$go_out"' EXIT
case "$f" in
  /*) input="$f" ;;
  *) input="$REPO_ROOT/$f" ;;
esac

if command -v timeout >/dev/null 2>&1; then
  TO=timeout
elif command -v gtimeout >/dev/null 2>&1; then
  TO=gtimeout
else
  TO=""
fi

if [ -n "$TO" ]; then
  "$TO" "$TIMEOUT" "$TSCPP" parse "$input" >"$cpp_out" 2>/dev/null
  cpp_rc=$?
  "$TO" "$TIMEOUT" "$PARSEDUMP" "$input" >"$go_out" 2>/dev/null
  go_rc=$?
else
  "$TSCPP" parse "$input" >"$cpp_out" 2>/dev/null
  cpp_rc=$?
  "$PARSEDUMP" "$input" >"$go_out" 2>/dev/null
  go_rc=$?
fi

if [ "$cpp_rc" -eq 0 ] && [ "$go_rc" -eq 0 ] && cmp -s "$cpp_out" "$go_out"; then
  echo "PASS $f"
else
  echo "FAIL $f"
  cp "$cpp_out" "$DIFFDIR/diff_cpp_$(basename "$f")" 2>/dev/null || true
  cp "$go_out" "$DIFFDIR/diff_go_$(basename "$f")" 2>/dev/null || true
  exit 1
fi
