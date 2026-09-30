#!/bin/bash
# conformance_bind.sh — diff `tscpp bind` output vs Go `bindump` oracle for
# one file. Requires: (cd tsc && go build -o bindump ./cmd/bindump)
#   ./conformance_bind.sh <file>          -> PASS/FAIL
#   ./conformance_bind.sh <list> <jobs>   -> run over a file list in parallel
set -u
set -o pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BINDUMP=${BINDUMP:-"$REPO_ROOT/tsc/bindump"}
TSCPP=${TSCPP:-"$REPO_ROOT/cpp/build/tscpp"}

run_one() {
  local F="$1" go_out cpp_out go_rc cpp_rc
  case "$F" in
    /*) ;;
    *) F="$REPO_ROOT/$F" ;;
  esac
  go_out=$(mktemp /tmp/bind_go.XXXXXX) || return 2
  cpp_out=$(mktemp /tmp/bind_cpp.XXXXXX) || { rm -f "$go_out"; return 2; }
  "$BINDUMP" "$F" > "$go_out" 2>/dev/null
  go_rc=$?
  "$TSCPP" bind "$F" > "$cpp_out" 2>/dev/null
  cpp_rc=$?
  if [ "$go_rc" -eq 0 ] && [ "$cpp_rc" -eq 0 ] && cmp -s "$go_out" "$cpp_out"; then
    echo "PASS $F"
    rm -f "$go_out" "$cpp_out"
  else
    echo "FAIL $F"
    echo "dumps: $cpp_out $go_out (exit $cpp_rc/$go_rc)" >&2
    return 1
  fi
}

if [ $# -eq 1 ]; then
  cd "$REPO_ROOT"
  run_one "$1"
elif [ $# -eq 2 ]; then
  if [ ! -s "$1" ] || [[ ! "$2" =~ ^[1-9][0-9]*$ ]]; then
    echo "a non-empty file list and a positive job count are required" >&2
    exit 2
  fi
  list="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")" || exit 2
  cd "$REPO_ROOT"
  export -f run_one
  export REPO_ROOT BINDUMP TSCPP
  while IFS= read -r F || [ -n "$F" ]; do
    if [ -z "$F" ]; then
      echo "empty file list entry" >&2
      exit 2
    fi
    printf '%s\0' "$F"
  done < "$list" \
    | xargs -0 -r -P "$2" -I{} bash -c 'run_one "$@"' _ {}
else
  echo "usage: $0 <file> | <listfile> <jobs>" >&2
  exit 2
fi
