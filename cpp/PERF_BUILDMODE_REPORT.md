# `tsc -b` cold-build performance report

Task: `tscpp tsc -b` cold builds were ~2.1× slower than the Go oracle (`tsgo`) on a
500-file composite project while incremental builds and `tsc -p` were at parity.
Goal: find and remove the duplicated/serialized work without changing emitted
output (byte-identical vs Go).

## TL;DR

Two independent regressions, both in the workgroup plumbing vs `core.NewWorkGroup`
in the Go code:

1. **Thread-per-task spawn storm** — `parallelWorkGroup::Queue` spawned one
   detached `std::thread` per queued fn. Go queues each fn as a *goroutine*
   multiplexed over GOMAXPROCS OS threads (`tsc/internal/core/workgroup.go`).
   On this 8-core VM a cold `tsc -b` produced ~4,502 `clone3` calls, ~8,676
   futex waits (~6.5 s) and ~8,045 `madvise` calls (~1.3 s) — `sys` time 28.0 s
   vs Go's 5.5 s (`strace -c -f`).
2. **Workgroup-parallel loops flattened to serial iteration** — three loops
   Go runs through a `core.NewWorkGroup` had been ported as synchronous
   `Range`/`for` bodies:
   - `emitFilesIncremental` — per-file `program.Emit` for every
     `affectedFilesPendingEmit` entry (`emitfileshandler.go:124-167`);
   - `collectAllAffectedFiles` — `changedFilesSet` → `getFilesAffectedBy`,
     then `handleDtsMayChangeOfAffectedFile` (`affectedfileshandler.go:365-393`);
   - `computeProgramFileChanges` — per-file hash/references/diagnostics-diff
     (`programtosnapshot.go:91-160`).

   This is the direct cause of the cold-build-only gap: on a cold build all 500
   files are pending emit, so emit ran strictly serially.

## Evidence

`diagnostics: true` phase breakdown on the 500-file project (this VM, 8 cores):

| Phase | tsgo | tscpp before | tscpp after |
|---|---|---|---|
| Parse | 0.204 s | 0.070 s | 0.057 s |
| Check | 0.856 s | 0.720 s | 0.589 s |
| **Emit** | **0.644 s** | **3.687 s** | **0.726 s** |
| Changes compute | 0.065 s | 0.066 s | 0.077 s |
| **Total** | **1.782 s** | **4.558 s** | **1.460 s** |

The emit phase was 5.7× slower than Go while parse/check were already at or
under parity — pointing at serialized per-file emit, not duplicated semantic
work. (`tsc -p` emits all files inside a single `program.Emit` call — that call
was already parallel — which is why `-p` showed parity.)

## Changes

- `cpp/internal/core/utilities.h` — `parallelWorkGroup` is now a bounded
  worker pool capped at `std::thread::hardware_concurrency()` (Go's effective
  bound is GOMAXPROCS). Workers drain a shared deque; `RunAndWait` still waits
  for `pending.empty() && running == 0`; `Queue` keeps the may-run-inline
  semantics (caller drains inline if thread creation fails with `EAGAIN`).
  Threads are **joinable and joined in the destructor** — a worker's loop tail
  can re-lock `mu` after `RunAndWait` returns, so detached threads raced
  mutex destruction (`pthread_mutex_lock: mutex->__data.__owner == 0`
  assertion, seen as `project.TestRefCountingCaches` / `project.TestSession`
  crashes under the first pool draft).
- `cpp/internal/execute/incremental/emitfileshandler.cpp` —
  `emitFilesIncremental` queues each pending file's
  `program->program_->Emit` on `newWorkGroup(program->program_->SingleThreaded())`,
  faithfully to `emitfileshandler.go:124-167` (same per-task body, same
  `emitUpdates.Store`/`updateHasEmitDiagnostics` inside the queued fn).
- `cpp/internal/execute/incremental/affectedfileshandler.cpp` —
  `collectAllAffectedFiles` uses two workgroups like `affectedfileshandler.go:365-393`
  (`getFilesAffectedBy` fan-out, then `handleDtsMayChangeOfAffectedFile`;
  `getDtsMayChange` stays outside the queued fn, matching Go).
- `cpp/internal/execute/incremental/programtosnapshot.cpp` —
  `computeProgramFileChanges` queues the per-file body on a workgroup like
  `programtosnapshot.go:91-160`.

No Go-semantics changes: task granularity, `RunAndWait` wait-for-all
contract, `singleThreadedWorkGroup` behavior, and checker-pool exclusivity are
unchanged.

## Results (this VM, 8 cores)

Cold `tsc -b` wall-clock medians (5 interleaved runs, outputs + `*.tsbuildinfo`
deleted between runs):

| Compiler | Median | Runs |
|---|---|---|
| tscpp **before** | **7.21 s** (real); user 12.2 s / sys 28.0 s | baseline |
| tsgo | **1.57 s** | 1.543 / 1.580 / 1.510 / 1.541 / 1.570 / 1.581 / 1.586 / 1.528 |
| tscpp **after** | **1.54 s** | 1.560 / 1.539 / 1.535 / 1.526 / 1.537 / 1.537 / 1.546 / 1.549 |

- Cold build: **7.21 s → 1.54 s** (was ~4.6× slower than Go; now ≈0.98× — at parity, nominally faster).
- Incremental (no-change) `tsc -b`: tscpp 0.174 s vs tsgo 0.313 s — at parity or faster.
- `tsc -p`: tscpp 1.611 s vs tsgo 1.546 s — parity retained.

(The task's reference machine showed ~2,326 ms → the same fix should land it
near Go's ~1,085 ms; absolute numbers differ per host, the serialized-emit
regression and thread-spawn storm are host-independent.)

## Verification

- **Byte-identical output**: `diff -r` of the emitted tree (500 × `.js`, `.d.ts`,
  `.js.map`, `.d.ts.map`) plus `tsconfig_b.tsbuildinfo` between tscpp and tsgo
  cold builds — **0 differences**.
- `unittestrunner`: **1122/1122 pass** (includes `project.TestRefCountingCaches`,
  `project.TestSession`, which caught the first pool draft's mutex race).
- `tsctestrunner`: **117/117 pass**.
- `fourslashrunner`: **4145/4145 pass** (skips are the repo's known-failing set).
