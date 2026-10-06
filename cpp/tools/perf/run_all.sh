#!/usr/bin/env bash
# Run the full tscpp-vs-tsgo benchmark suite, sequentially.
# Env overrides: TSGO, TSCPP, PROJ, TIMED_RUNS, RUNS, PROJ_FILES.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
export PYTHONPATH="$HERE"
PROJ=${PROJ:-/tmp/perfproj}

echo "=== [1/7] corpus sample ==="
[ -s "$HERE/corpus500.txt" ] || bash "$HERE/sample_corpus.sh" 500

echo "=== [2/7] synthetic project ==="
[ -d "$PROJ/src" ] || python3 "$HERE/gen_project.py" "$PROJ"

echo "=== [3/7] diagnostic sanity (20 files) ==="
python3 "$HERE/sanity_diagnostics.py" 20 || true

echo "=== [4/7] per-file benchmark ==="
python3 "$HERE/bench_perfile.py"

echo "=== [5/7] whole-corpus sequential ==="
python3 "$HERE/bench_corpus.py"

echo "=== [6/7] project (emit/noEmit/declaration) + memory ==="
python3 "$HERE/bench_project.py"
bash "$HERE/bench_memory.sh"

echo "=== [7/7] cold-start + scaling ==="
python3 "$HERE/bench_coldstart.py"
python3 "$HERE/bench_scaling.py"

echo "=== done; results in $HERE/results/ ==="
ls -la "$HERE/results/"
