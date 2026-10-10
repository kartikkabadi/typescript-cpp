#!/bin/bash
# conformance_check.sh — diff `tscpp check` output vs Go `checkdump` oracle
# for one file. Requires: go build -o /tmp/checkdump tsc/cmd/checkdump
#   ./conformance_check.sh <file>          -> PASS/FAIL
#   ./conformance_check.sh <list> <jobs>   -> run over a file list in parallel
#
# Output format (both sides): canonical diagnostic dump —
#   G <code>                     program/global diagnostics with no file
#   F <fileName>                 per source file that has diagnostics
#   T <code> <pos> <end>         diagnostics located in that file
set -u
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CHECKDUMP=${CHECKDUMP:-/tmp/checkdump}
TSCPP=${TSCPP:-"$REPO_ROOT/cpp/build/tscpp"}

run_one() {
  F="$1"
  case "$F" in /*) ;; *) F="$REPO_ROOT/$F" ;; esac
  if [ ! -f "$F" ]; then echo "FAIL $F (missing)"; return 1; fi
  ID=$(printf '%s' "$F" | md5sum | cut -c1-12)
  # mktemp-named scratch files: fixed /tmp names collide when two corpus
  # runs overlap, and identical PASS dumps are just litter over 12k files.
  go_out="$(mktemp "${TMPDIR:-/tmp}/check_go_${ID}.XXXXXX")" || return 2
  cpp_out="$(mktemp "${TMPDIR:-/tmp}/check_cpp_${ID}.XXXXXX")" || { rm -f "$go_out"; return 2; }
  (cd "$REPO_ROOT" && "$CHECKDUMP" "$F") > "$go_out" 2>/dev/null
  go_rc=$?
  (cd "$REPO_ROOT" && "$TSCPP" check "$F") > "$cpp_out" 2>/dev/null
  cpp_rc=$?
  # Fail closed: rc >= 126 means the tool never ran (not found / crashed) —
  # never compare two empty dumps and call it a match.
  # Exit codes must match too — a tool crashing with status 2 must not
  # pair an empty dump against a clean exit-2 oracle run.
  if [ "$cpp_rc" -lt 126 ] && [ "$go_rc" -lt 126 ] && [ "$cpp_rc" -eq "$go_rc" ] && cmp -s "$go_out" "$cpp_out"; then
    rm -f "$go_out" "$cpp_out"
    echo "PASS $F"
  else
    echo "FAIL $F"
    echo "dumps: $cpp_out vs $go_out (exit $cpp_rc/$go_rc)" >&2
    return 1
  fi
}

if [ $# -eq 1 ]; then
  cd "$REPO_ROOT"
  run_one "$1"
elif [ $# -eq 2 ]; then
  if [ ! -f "$1" ] || ! [ "$2" -gt 0 ] 2>/dev/null; then
    echo "a non-empty file list and a positive job count are required" >&2
    exit 2
  fi
  list="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")" || exit 2
  if grep -q '^$' "$list"; then
    echo "empty file list entry" >&2
    exit 2
  fi
  cd "$REPO_ROOT"
  export -f run_one 2>/dev/null || true
  export REPO_ROOT CHECKDUMP TSCPP
  # `|| [ -n "$F" ]` keeps the final line when the list lacks a trailing
  # newline.
  while IFS= read -r F || [ -n "$F" ]; do printf '%s\0' "$F"; done < "$list" \
    | xargs -0 -r -P "$2" -I{} bash -c 'run_one "$@"' _ {}
else
  echo "usage: $0 <file> | <listfile> <jobs>" >&2
  exit 2
fi
