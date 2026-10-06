#!/usr/bin/env bash
# Max RSS on the 100-file project check (`tsc -p --noEmit`), both sides.
# Uses /usr/bin/time -v. 3 runs each, median RSS. Writes results/memory.txt
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TSGO=${TSGO:-/tmp/tsgo}
TSCPP=${TSCPP:-/tmp/tscpp_probe}
PROJ=${PROJ:-/tmp/perfproj}
RUNS=${RUNS:-3}
mkdir -p "$HERE/results"
OUT="$HERE/results/memory.txt"
cd "$PROJ"

{
  echo "max RSS (KB), tsc -p tsconfig.json --noEmit, $RUNS runs"
  for side in go cpp; do
    rss=()
    for r in $(seq 1 "$RUNS"); do
      if [ "$side" = go ]; then
        /usr/bin/time -v "$TSGO" -p tsconfig.json --noEmit \
          --pretty false 2>/tmp/time_$side.txt >/dev/null
      else
        /usr/bin/time -v "$TSCPP" tsc -p tsconfig.json --noEmit \
          --pretty false 2>/tmp/time_$side.txt >/dev/null
      fi
      kb=$(grep 'Maximum resident set size' /tmp/time_$side.txt | awk '{print $NF}')
      rss+=("$kb")
      echo "$side run$r: ${kb} KB"
    done
    med=$(printf '%s\n' "${rss[@]}" | sort -n | awk 'NR==int((n+1)/2)' n=$RUNS)
    echo "$side median RSS: ${med} KB"
  done
} | tee "$OUT"
