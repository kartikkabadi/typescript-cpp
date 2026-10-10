#!/usr/bin/env bash
# gen_pgo.sh — produce a PGO-optimized build of tscpp.
#
# Three stages:
#   1. instrumented build (INSTR_DIR, Release + -fprofile-generate=<WORK>/profraw)
#   2. representative workload — perfproj emit + --noEmit + declaration emit,
#      a conformance corpus sample via `tscpp check`/`tscpp emitdump`, and a
#      fourslashrunner slice — writing <WORK>/profraw/*.profraw
#   3. llvm-profdata merge -> <WORK>/tscpp.profdata, then the configured
#      build dir BUILD_DIR is reconfigured with -DTSCPP_PGO=<profdata> and
#      rebuilt (the final compiler may be zig: -fprofile-use is a pure
#      compile-time consumer that zig forwards correctly, verified — a bogus
#      profdata path errors out under zig c++).
#
# The instrumented stage needs a clang that really instruments. zig c++
# accepts -fprofile-generate but silently emits no profile runtime (no
# __llvm_profile_* symbols, no .profraw). Point CC/CXX at a real clang:
#
#   CC=clang-21 CXX=clang++-21 LLVM_PROFDATA=llvm-profdata-21 \
#       BUILD_DIR=cpp/build bash cpp/tools/perf/gen_pgo.sh
#
# Env overrides:
#   BUILD_DIR=cpp/build              final PGO build dir (reuses its cached
#                                    compiler; must already be configured)
#   INSTR_DIR=cpp/build-pgo-instr    instrumented build dir
#   WORK=/tmp/tscpp-pgo              profraw + merged profdata + scratch
#   LLVM_PROFDATA=llvm-profdata      profdata merger to use
#   PROJ=/tmp/perfproj               existing generated project; regenerated
#                                    under WORK if absent
#   CONFORMANCE_N=150                testdata files to check + emitdump
#   FOURSLASH_RUN='TestQuickInfo|TestCompletions|TestFormatting|TestFindAllRefs'
#   EXTRA_CMAKE_FLAGS='...'          extra flags for the instrumented configure
#                                    (e.g. '-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld')
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-cpp/build}"
INSTR_DIR="${INSTR_DIR:-cpp/build-pgo-instr}"
WORK="${WORK:-/tmp/tscpp-pgo}"
PROFRAW="$WORK/profraw"
PROFDATA="$WORK/tscpp.profdata"
LLVM_PROFDATA="${LLVM_PROFDATA:-llvm-profdata}"
if ! command -v "$LLVM_PROFDATA" >/dev/null 2>&1; then
	for v in 21 20 19 18; do
		if command -v "llvm-profdata-$v" >/dev/null 2>&1; then
			LLVM_PROFDATA="llvm-profdata-$v"; break
		fi
	done
fi
PROJ="${PROJ:-/tmp/perfproj}"
CONFORMANCE_N="${CONFORMANCE_N:-150}"
FOURSLASH_RUN="${FOURSLASH_RUN:-TestQuickInfo|TestCompletions|TestFormatting|TestFindAllRefs}"
CC="${CC:-clang}"
CXX="${CXX:-clang++}"

case "$BUILD_DIR" in /*) ;; *) BUILD_DIR="$REPO/$BUILD_DIR";; esac
case "$INSTR_DIR" in /*) ;; *) INSTR_DIR="$REPO/$INSTR_DIR";; esac

echo "== gen_pgo: work=$WORK instr=$INSTR_DIR final=$BUILD_DIR"
mkdir -p "$PROFRAW"

# --- sanity: the instrumented compiler must really instrument -------------
cat > "$WORK/probe.cpp" <<'EOF'
int main(){ long s=0; for(long i=0;i<10;i++) s+=i; return (int)s; }
EOF
rm -rf "$WORK/probe-prof" && mkdir -p "$WORK/probe-prof"
if ! "$CXX" -O1 -fprofile-generate="$WORK/probe-prof" "$WORK/probe.cpp" \
		-o "$WORK/probe" 2>/dev/null; then
	echo "FATAL: $CXX cannot compile -fprofile-generate" >&2; exit 1
fi
LLVM_PROFILE_FILE="$WORK/probe-prof/p.profraw" "$WORK/probe"
if ! ls "$WORK/probe-prof"/*.profraw >/dev/null 2>&1; then
	echo "FATAL: $CXX accepts -fprofile-generate but emits no .profraw" >&2
	echo "       (zig c++ drops instrumentation — use a real clang," >&2
	echo "        e.g. CC=clang-21 CXX=clang++-21)" >&2
	exit 1
fi
echo "== instrumented compiler ok: $CXX"

# --- stage 1: instrumented build ------------------------------------------
cmake -B "$INSTR_DIR" -S "$REPO/cpp" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_CXX_FLAGS="-fprofile-generate=$PROFRAW" \
	-DCMAKE_EXE_LINKER_FLAGS="-fprofile-generate=$PROFRAW" \
	${EXTRA_CMAKE_FLAGS:-}
cmake --build "$INSTR_DIR" --target tscpp fourslashrunner -j "$(nproc)"
TSCPP="$INSTR_DIR/tscpp"
FSRUN="$INSTR_DIR/fourslashrunner"

export LLVM_PROFILE_FILE="$PROFRAW/%p.profraw"

# --- stage 2: representative workload --------------------------------------
# 2a. perfproj project mode: emit, --noEmit, declaration-only emit.
if [ ! -f "$PROJ/tsconfig.json" ]; then
	PROJ="$WORK/perfproj"
	python3 "$REPO/cpp/tools/perf/gen_project.py" "$PROJ"
fi
for mode in emit noemit decl; do
	case "$mode" in
		emit)  extra=() ;;
		noemit) extra=(--noEmit) ;;
		decl)  extra=(--declaration --emitDeclarationOnly) ;;
	esac
	rm -rf "$PROJ/out_pgo_$mode"
	(cd "$PROJ" && "$TSCPP" tsc -p tsconfig.json --pretty false \
		--outDir "out_pgo_$mode" "${extra[@]}") >/dev/null 2>&1 || true
done
echo "== perfproj emit/noEmit/decl profiled"

# 2b. conformance corpus sample: `tscpp check` + `tscpp emitdump`.
LIST="$WORK/conformance.list"
find "$REPO/tsc/testdata/tests/cases" -type f \( -name '*.ts' -o -name '*.tsx' \) \
	| grep -v '/node_modules/' | sort | head -"$CONFORMANCE_N" > "$LIST"
while IFS= read -r f; do
	(cd "$REPO" && "$TSCPP" check "$f")    >/dev/null 2>&1 || true
	(cd "$REPO" && "$TSCPP" emitdump "$f") >/dev/null 2>&1 || true
done < "$LIST"
echo "== conformance sample profiled: $(wc -l < "$LIST") files"

# 2c. fourslashrunner slice (language-service surface).
# Caveat: the runner forks per test and the child _exit()s, which skips the
# profile atexit flush — child counts are lost; only the parent's merged
# in-process counters land in the profraw. LS code still benefits indirectly
# via shared parser/checker paths covered by 2a/2b.
"$FSRUN" -run "$FOURSLASH_RUN" >/dev/null 2>&1 || true
echo "== fourslash slice profiled ($FOURSLASH_RUN)"

# --- stage 3: merge + PGO rebuild ------------------------------------------
n=$(find "$PROFRAW" -name '*.profraw' | wc -l)
echo "== merging $n profraw files"
"$LLVM_PROFDATA" merge "$PROFRAW" -o "$PROFDATA"
ls -la "$PROFDATA"

cmake -B "$BUILD_DIR" -DTSCPP_PGO="$PROFDATA"
cmake --build "$BUILD_DIR" -j "$(nproc)"
echo "== done: PGO build at $BUILD_DIR (profile: $PROFDATA)"
