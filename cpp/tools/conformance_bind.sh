#!/bin/bash
# conformance_bind.sh — diff `tscpp bind` output vs Go `bindump` oracle for
# one file. Requires: go build -o /tmp/bindump tsc/cmd/bindump
#   ./conformance_bind.sh <file>          -> PASS/FAIL
#   ./conformance_bind.sh <list> <jobs>   -> run over a file list in parallel
set -u
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BINDUMP=${BINDUMP:-/tmp/bindump}
TSCPP=${TSCPP:-"$REPO_ROOT/cpp/build/tscpp"}

run_one() {
  F="$1"
  ID=$(printf '%s' "$F" | md5 | cut -c1-12)
  "$BINDUMP" "$F" > "/tmp/bind_go_$ID.txt" 2>/dev/null
  "$TSCPP" bind "$F" > "/tmp/bind_cpp_$ID.txt" 2>/dev/null
  if cmp -s "/tmp/bind_go_$ID.txt" "/tmp/bind_cpp_$ID.txt"; then
    echo "PASS $F"
  else
    echo "FAIL $F"
  fi
}

if [ $# -eq 1 ] && [ -f "$1" ]; then
  cd "$REPO_ROOT"
  run_one "$REPO_ROOT/$1"
elif [ $# -eq 2 ]; then
  cd "$REPO_ROOT"
  export -f run_one 2>/dev/null || true
  while IFS= read -r F; do printf '%s\0' "$F"; done < "$1" \
    | xargs -0 -P "$2" -I{} bash -c 'run_one "$@"' _ {}
else
  echo "usage: $0 <file> | <listfile> <jobs>" >&2
  exit 2
fi
