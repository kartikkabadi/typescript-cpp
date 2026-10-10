#!/usr/bin/env bash
# project_parity.sh — multi-file project conformance harness.
#
# For every project under CORPUS (each dir = a self-contained tsconfig
# project; an ARGS file lists named run variants as `name: args` with `|`
# separating sequential commands run in the same directory), copy the project
# into two identical run dirs, run the Go oracle (TSGO) and tscpp (TSCPP tsc)
# with identical arguments, and compare:
#   (a) stdout  (b) stderr  (c) exit code
#   (d) the emitted file tree, byte-for-byte (diff -r)
#   (e) .tsbuildinfo files byte-for-byte (XXH3 hashes were ported for parity;
#       identical inputs should produce identical bytes)
#
# Outputs land under REPORT/<project>/<run>/:
#   status.txt           one line per segment: name seg verdict detail
#   <run>.<seg>.{stdout,stderr}.diff, .tree.diff — raw diffs on mismatch
# Run dirs live under RUN/<project>/{go,cpp} for manual reproduction.
#
#   project_parity.sh [project ...]   (default: every dir in CORPUS)
# Env: TSGO=/tmp/tsgo TSCPP=<build/tscpp or /tmp/tscpp_probe>
#      CORPUS=/tmp/projcorpus RUN=/tmp/projrun REPORT=/tmp/projreport
#      TIMEOUT=180 JOBS=1
set -u

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TSGO="${TSGO:-/tmp/tsgo}"
if [ -z "${TSCPP:-}" ]; then
	if [ -x "$REPO_ROOT/cpp/build/tscpp" ]; then
		TSCPP="$REPO_ROOT/cpp/build/tscpp"
	else
		TSCPP=/tmp/tscpp_probe
	fi
fi
CORPUS="${CORPUS:-/tmp/projcorpus}"
RUN="${RUN:-/tmp/projrun}"
REPORT="${REPORT:-/tmp/projreport}"
TIMEOUT="${TIMEOUT:-180}"
JOBS="${JOBS:-1}"

# macOS parity runs need coreutils' gtimeout; with neither binary present,
# run without a timeout rather than failing to spawn.
if command -v timeout >/dev/null 2>&1; then
	TIMEOUT_BIN=timeout
elif command -v gtimeout >/dev/null 2>&1; then
	TIMEOUT_BIN=gtimeout
else
	TIMEOUT_BIN=""
fi

if [ ! -x "$TSGO" ]; then echo "missing oracle: $TSGO" >&2; exit 2; fi
if [ ! -x "$TSCPP" ]; then echo "missing tscpp: $TSCPP" >&2; exit 2; fi

mkdir -p "$RUN" "$REPORT"

# run_one <side-dir> <binary> <args-string> <out-prefix>
run_one() {
	local dir="$1" bin="$2" args="$3" prefix="$4"
	local args_clean="${args//|/ }"  # single segment here
	(
		cd "$dir" || exit 99
		# shellcheck disable=SC2086
		if [ -n "$TIMEOUT_BIN" ]; then
			"$TIMEOUT_BIN" "$TIMEOUT" $bin $args_clean >"$prefix.stdout" 2>"$prefix.stderr"
		else
			# No timeout(1)/gtimeout(1): keep the watchdog anyway — a
			# background sleeper kills the child at the deadline so a hung
			# oracle/compiler can't block parity forever.
			$bin $args_clean >"$prefix.stdout" 2>"$prefix.stderr" &
			local pid=$!
			( sleep "$TIMEOUT"; kill -9 "$pid" 2>/dev/null ) &
			local watchdog=$!
			wait "$pid"
			local rc=$?
			kill "$watchdog" 2>/dev/null
			wait "$watchdog" 2>/dev/null
			exit "$rc"
		fi
		echo "$?" >"$prefix.exit"
	)
}

# norm <file> <runroot>: replace absolute run-dir paths for cross-dir compare
norm() {
	sed -e "s|$2|__ROOT__|g" "$1" 2>/dev/null
}

run_project() {
	local proj="$1"
	local srcdir="$CORPUS/$proj"
	local meta="$REPORT/$proj"
	local godir="$RUN/$proj/go" cppdir="$RUN/$proj/cpp"
	local argsfile="$srcdir/ARGS"

	if [ ! -d "$srcdir" ]; then
		echo "SKIP $proj (no dir)" >&2
		return
	fi
	rm -rf "$RUN/$proj" "$meta"
	mkdir -p "$godir" "$cppdir" "$meta"
	cp -a "$srcdir"/. "$godir"/ 2>/dev/null
	cp -a "$srcdir"/. "$cppdir"/ 2>/dev/null
	rm -f "$godir/ARGS" "$cppdir/ARGS"

	if [ ! -f "$argsfile" ]; then argsfile=/dev/null; fi
	local saw_run=0
	while IFS= read -r line || [ -n "$line" ]; do
		[ -z "$line" ] && continue
		case "$line" in '#'*) continue ;; esac
		saw_run=1
		local name="${line%%:*}"
		local argstr="${line#*:}"
		# split sequential segments on |
		local seg=0
		local IFS_BAK="$IFS"
		IFS='|' read -ra segs <<<"$argstr"
		IFS="$IFS_BAK"
		local nseg=${#segs[@]}
		local runfail=""
		local s
		for ((s = 0; s < nseg; s++)); do
			local segname="${name}.$s"
			run_one "$godir"  "$TSGO"            "${segs[$s]}" "$meta/$segname.go"
			run_one "$cppdir" "$TSCPP tsc"       "${segs[$s]}" "$meta/$segname.cpp"
			local goexit cppexit
			goexit=$(cat "$meta/$segname.go.exit")
			cppexit=$(cat "$meta/$segname.cpp.exit")
			if [ "$goexit" != "$cppexit" ]; then
				runfail="$runfail exit($segname:$goexit!=$cppexit)"
			fi
			# stdout/stderr with run-root normalization
			norm "$meta/$segname.go.stdout"  "$godir"  > "$meta/$segname.go.stdout.n"
			norm "$meta/$segname.cpp.stdout" "$cppdir" > "$meta/$segname.cpp.stdout.n"
			norm "$meta/$segname.go.stderr"  "$godir"  > "$meta/$segname.go.stderr.n"
			norm "$meta/$segname.cpp.stderr" "$cppdir" > "$meta/$segname.cpp.stderr.n"
			if ! cmp -s "$meta/$segname.go.stdout.n" "$meta/$segname.cpp.stdout.n"; then
				diff -u "$meta/$segname.go.stdout.n" "$meta/$segname.cpp.stdout.n" \
					> "$meta/$segname.stdout.diff" 2>/dev/null || true
				runfail="$runfail stdout($segname)"
			fi
			if ! cmp -s "$meta/$segname.go.stderr.n" "$meta/$segname.cpp.stderr.n"; then
				diff -u "$meta/$segname.go.stderr.n" "$meta/$segname.cpp.stderr.n" \
					> "$meta/$segname.stderr.diff" 2>/dev/null || true
				runfail="$runfail stderr($segname)"
			fi
		done
		# (d)+(e) tree diff: whole emitted tree, byte-for-byte
		if ! diff -r --no-dereference "$godir" "$cppdir" > "$meta/$name.tree.diff" 2>&1; then
			# Emitted files may legitimately embed the absolute run-dir path
			# (e.g. jsx react-jsxdev _jsxFileName, source maps) — the two sides
			# run in different dirs by design. Re-compare a tree where every
			# file's contents are normalized for its own root; remaining diffs
			# are real.
			local ngo="$meta/$name.norm-go" ncpp="$meta/$name.norm-cpp"
			rm -rf "$ngo" "$ncpp"
			cp -a "$godir" "$ngo"; cp -a "$cppdir" "$ncpp"
			find "$ngo"  -type f -exec sed -i "s|$godir|__ROOT__|g"  {} +
			find "$ncpp" -type f -exec sed -i "s|$cppdir|__ROOT__|g" {} +
			if diff -r --no-dereference "$ngo" "$ncpp" > "$meta/$name.tree.norm.diff" 2>&1; then
				runfail="$runfail tree-pathev(path-embedded-only)"
			else
				local flagged
				flagged=$(grep -c "^diff\|^Only in\|^Binary" "$meta/$name.tree.norm.diff" || true)
				local binfo=""
				if grep -q "tsbuildinfo" "$meta/$name.tree.norm.diff"; then
					binfo=" tsbuildinfo"
				fi
				runfail="$runfail tree($flagged)$binfo"
			fi
		fi
		if [ -z "$runfail" ]; then
			echo "PASS $name" >> "$meta/status.txt"
			echo "PASS $proj/$name"
		elif [ "$runfail" = " tree-pathev(path-embedded-only)" ]; then
			echo "PASS $name (path-embedded outputs only)" >> "$meta/status.txt"
			echo "PASS $proj/$name (path-embedded)"
		else
			echo "DIFF $name$runfail" >> "$meta/status.txt"
			echo "DIFF $proj/$name$runfail"
		fi
	done < "$argsfile"
	[ "$saw_run" = 0 ] && echo "SKIP $proj (empty ARGS)" >&2
}

if [ $# -gt 0 ]; then
	projs=("$@")
else
	projs=()
	for d in "$CORPUS"/*/; do
		projs+=("$(basename "$d")")
	done
fi

if [ "$JOBS" -gt 1 ]; then
	export -f run_one norm run_project
	export TSGO TSCPP CORPUS RUN REPORT TIMEOUT TIMEOUT_BIN
	printf '%s\n' "${projs[@]}" | xargs -d '\n' -P "$JOBS" -I{} bash -c 'run_project "$@"' _ {}
else
	for p in "${projs[@]}"; do
		run_project "$p"
	done
fi
