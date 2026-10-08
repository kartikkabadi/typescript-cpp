#!/usr/bin/env bash
# run_emit_oracle.sh — precompute Go `emitdump` oracle output for a corpus
# list (same md5-12 keying as conformance_triage.py).
#
#   run_emit_oracle.sh <listfile> <jobs>
#
# Requires ~/tools/emitdump (built from tsc/cmd/emitdump). Writes
# /tmp/emit_oracle/<md5>.txt per file (skips existing).
set -u
LIST="${1:?listfile}"
JOBS="${2:-16}"
ORACLE="${EMIT_ORACLE_DIR:-/tmp/emit_oracle}"
EMITDUMP="${EMITDUMP:-$HOME/tools/emitdump}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ORACLE"

export ORACLE EMITDUMP REPO EMIT_FLAGS
work() {
    f="$1"
    k=$(printf '%s' "$f" | md5sum | cut -c1-12)
    out="$ORACLE/$k.txt"
    [ -f "$out" ] && return 0
    (cd "$REPO" && "$EMITDUMP" "$f" ${EMIT_FLAGS:-} > "$out" 2>/dev/null) || \
        printf 'EXIT %s\n' "$?" > "$out"
}
export -f work
xargs -a "$LIST" -d '\n' -P "$JOBS" -I{} bash -c 'work "{}"'
