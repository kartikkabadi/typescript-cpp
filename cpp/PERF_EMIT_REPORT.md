# PERF_EMIT_REPORT — emit-phase perf gap (`tscpp tsc -p` vs tsgo)

Branch: `devin/cpp-perf-emit` (base `devin/cpp-port` tip `f7ddd5eefe`).

## Symptom

On a 500-file synthetic project (`PROJ_FILES=500 python3
cpp/tools/perf/gen_project.py /tmp/perfproj_big`), the emit phase was the only
phase slower than Go: emit ~0.19s vs tsgo ~0.12s (reported on the task window
as 0.159 vs 0.129, +23%).

## Root cause

`tsc/internal/compiler/program.go` `Emit` queues one task per file on a
`WorkGroup`; each task calls `newEmitHost` → `getCheckerForFileNonExclusive`
— **no lock** — so up to `GOMAXPROCS` files emit in parallel even inside one
checker's set.

The C++ port cannot share a checker non-exclusively: emit workers call back
into the checker (`MarkLinkedReferencesRecursively` → `markLinkedReferences`
→ `checkExpressionCached` → flow analysis), which mutates per-checker state;
an earlier fix therefore took an **exclusive** checkout per task. Combined
with `parallelWorkGroup` spawning **one detached `std::thread` per task**, emit
on 500 files degenerated to:

- 500 thread spawns (~10ms total),
- 500 threads convoying on 4 checker mutexes for the *whole* emit body
  (cumulative lock wait 42–92s across tasks; ~84–180ms of sleeping per task),
- a wake-latency tax on every mutex handoff.

Net effect: ~4-way serialization plus contention overhead — the observed
~+60ms over Go.

## Fix

Verified against `tsc/internal/{emitter,printer}`: the emit resolver is
invoked **only during the transform phase** (`getScriptTransformers` /
`runDeclarationTransformers` pass it to transformers; `printer.go` has zero
resolver references). Printing, sourcemap generation and `writeFile` never
touch checker state.

So the checkout was split at the real boundary:

- `emitter::emit()` → `prepareEmit()` (emit context + resolver +
  `transformJSFile`/`transformDeclarationFile`) and `flushEmit()`
  (`printJSFile`/`printDeclarationFile` + result collection). `emit()`
  remains as the composite; observable ordering (early-outs, `EmitSkipped`
  flags, diagnostic ordering, emitted-file order) is unchanged.
- `SimpleProgram::Emit` runs a **bounded worker pool**
  (`hardware_concurrency` workers pulling per-file tasks off an atomic index
  — the faithful equivalent of the Go runtime multiplexing the per-file queue
  onto GOMAXPROCS lanes). Per file: exclusive checkout → `prepareEmit` →
  release → `flushEmit`. Checker serialization is preserved exactly where the
  checker is touched; the print tail overlaps across all lanes.
  `SingleThreaded()` keeps Go's inline in-order execution.

  This also removes the special-case difference between the built-in checker
  pool and external `CheckerPool`s — Go's emit path is uniform; now the port
  is too (both go through `newEmitHost`/`GetTypeCheckerForFileExclusive`).

`cpp/cmd/tscpp/main.cpp`'s emitdump `written`-vector mutex was ruled out: it
is only held for the append and does not serialize emit work.

## Instrumentation kept (debug aid, zero-cost when off)

`TSCPP_EMIT_PROFILING=1 tscpp tsc -p …` prints one `EMITPROF` block to stderr:
wall time, cumulative checker-lock wait, cumulative locked (transform) and
unlocked (print+write) time, plus per-phase resolver/js-transform/
decl-transform/js-print totals.

Post-fix profile on perfproj_big: `wall_ms=146.5 lockwait_total=482.7
locked_total=385.7 print_total=292.7` — waits now overlap print work as
designed (pre-fix: `lockwait_total` 42,000–92,000ms).

## Results — emit-phase medians (`--diagnostics` "Emit time", RUNS=9, quiet window)

| Project | tsgo | tscpp before | tscpp after | before/go | after/go |
|---|---|---|---|---|---|
| perfproj_big (500 files) | 0.126s | 0.195s | **0.123s** | 1.55× | **0.98×** |
| perfproj (100 files)     | 0.028s | 0.035s | **0.020s** | 1.25× | **0.71×** |

Whole-command wall medians (`bench_project.py`, RUNS=9, `cpp/go` ratio):

| Project | emit | noEmit | declaration |
|---|---|---|---|
| perfproj_big | 0.887 (0.61s vs 0.69s) | 0.890 | 0.901 |
| perfproj     | 0.841 (0.14s vs 0.17s) | 0.763 | 0.883 |

Emit is now at parity or faster than Go on both projects; all other phases
remain ≤ Go.

## Verification

| Check | Result |
|---|---|
| `diff -r out_cpp out_go` (500-file project emit) | byte-identical |
| Emit conformance corpus sample (`emit_triage.py`, corpus500, Go `emitdump` oracle) | 500/500 (100%) |
| `unittestrunner` | 1122/1122 |
| `tsctestrunner` | 117/117 |
| `fourslashrunner -run 'Emit\|QuickInfo\|Signature'` slice | 479/479 (+ earlier `Emit`-only 1/1, `QuickInfo\|DocumentHigh\|Rename` 541/541) |

Note: this VM's timings fluctuate 2–4× under noisy-neighbor steal; all medians
above were taken in quiet windows in a single measurement session. Ratios are
stable across windows.
