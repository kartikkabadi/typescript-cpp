#!/usr/bin/env bash
# Sample N files from the conformance corpus with a fixed seed.
# Usage: sample_corpus.sh [N] [out_file]
set -euo pipefail
N="${1:-500}"
OUT="${2:-$(cd "$(dirname "$0")" && pwd)/corpus${N}.txt}"
REPO=$(git rev-parse --show-toplevel)
cd "$REPO"
# Deterministic shuffle: --random-source needs a byte stream; `yes 42` is fixed.
find tsc/testdata/tests/cases -name '*.ts' | sort \
  | shuf -n "$N" --random-source=<(yes 42) | sort > "$OUT"
echo "wrote $(wc -l < "$OUT") files to $OUT"
