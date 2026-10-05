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
  ID=$(printf '%s' "$F" | md5sum | cut -c1-12)
  (cd "$REPO_ROOT" && "$CHECKDUMP" "$F") > "/tmp/check_go_$ID.txt" 2>/dev/null
  (cd "$REPO_ROOT" && "$TSCPP" check "$F") > "/tmp/check_cpp_$ID.txt" 2>/dev/null
  if cmp -s "/tmp/check_go_$ID.txt" "/tmp/check_cpp_$ID.txt"; then
    echo "PASS $F"
  else
    echo "FAIL $F"
  fi
}

if [ $# -eq 1 ] && [ -f "$1" ]; then
  cd "$REPO_ROOT"
  run_one "$1"
elif [ $# -eq 2 ]; then
  cd "$REPO_ROOT"
  export -f run_one 2>/dev/null || true
  export REPO_ROOT CHECKDUMP TSCPP
  while IFS= read -r F; do printf '%s\0' "$F"; done < "$1" \
    | xargs -0 -P "$2" -I{} bash -c 'run_one "$@"' _ {}
else
  echo "usage: $0 <file> | <listfile> <jobs>" >&2
  exit 2
fi
