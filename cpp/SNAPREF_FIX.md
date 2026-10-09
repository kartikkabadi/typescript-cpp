# Fix: flaky `snapshot ref on disposed snapshot` panic (TestUpdateToClassStatics)

## Symptom

A full fourslash recount (4 parallel shards — real memory/load pressure) produced:

```
FAIL TestUpdateToClassStatics
    tsc internal error: snapshot ref on disposed snapshot
```

The test then passed 5/5 sequential reruns — nondeterministic.

## Root cause

`Snapshot::ref()` (snapshot.h, faithful to snapshot.go:754) panics when the
refcount has already reached zero and `dispose()` has run. In Go this panic is
unreachable by construction: every `ref()` site acts on a snapshot the caller
provably owns (the canonical `s.snapshot` under `snapshotMu`, or a fresh clone
whose refcount starts at 1).

The C++ port added one ref site Go does not have: `SnapshotLSHost`'s
constructor. Go's `*project.Snapshot` satisfies `ls.Host` structurally, so
`ls.NewLanguageService(projectID, program, snapshot, activeFile)` stores the
snapshot **raw** — no ref. The GC keeps it reachable for the service's
lifetime, and session.go documents that "language services continue to work
even after their backing snapshot has been disposed."

The C++ adapter instead did `s->ref()` in its ctor and `snapshot->Deref()` in
its dtor. All LS-producing session entry points obtain the snapshot via
`getSnapshot(ctx, request, /*callerRef=*/false)` — i.e. the canonical
`Session::snapshot` captured **without** a caller ref. Between `getSnapshot`
returning and `new SnapshotLSHost(snapshot)` running, a concurrent snapshot
adoption (another request's file-change flush through `updateSnapshot`, the
debounced `scheduledSnapshotUpdate`, or a queued `adoptSnapshotChange`) can
install a new canonical snapshot and `oldSnapshot->Deref()` it to 0 —
`dispose()` runs — and the in-flight `ref()` panics.

Same window for `GetLanguageServiceWithAutoImports`: `newSnapshot` is a fresh
clone (rc=1), but `tryAdoptSnapshotChangeInBackground` enqueues adoption
asynchronously — if it rejects (session moved on) and `Deref`s before the
`SnapshotLSHost` ctor runs, `ref()` hits a disposed snapshot.

Under single-threaded sequential runs the window (~GetDefaultProject + arg
eval) is rarely preempted; under parallel-shard load the background queue task
lands inside it probabilistically — hence the flake.

## Why the ref was wrong twice (not just racy)

LanguageService objects are leak-tolerant like the rest of the snapshot graph
(`~LanguageService` is only reached via explicit `delete`, used in tests; LSP
handlers never delete). So `~SnapshotLSHost`'s `Deref()` never ran in
production paths — every `GetLanguageService` permanently pinned its snapshot,
deferred `dispose()` indefinitely, and over-counted every store-owned resource
the snapshot holds (programCounter, parseCache, extendedConfigCache refs) —
diverging from Go, where `dispose()` releases them as soon as the session's
ref drops.

## Fix — capture raw like Go (same rule as 59b01c11a87)

Per the GC-parity convention, objects are never deleted, only disposed — a
disposed Snapshot stays addressable exactly like a GC-kept Go one, and
`dispose()` releases store-owned resources without touching the snapshot's
fields (verified: `checkerPool::Discard` only stops idle cleanup; the pool
stays functional, matching checkerpool.go).

- `cpp/internal/project/snapshot.h` — `SnapshotLSHost` now captures the
  snapshot by raw pointer; `ref()`/`Deref()` removed. Matches Go's
  `NewLanguageService` one-for-one. This was the only `ref()` site reachable
  on a snapshot the caller does not own.
- `cpp/internal/project/session.cpp` (extensionless-delete watch check) —
  dropped `snapshot->ref()` + deref guard; Go reads `s.snapshot` raw under
  `RLock` at session.go:463 and reads `fs.cacheDirectories` post-unlock.
- `cpp/internal/project/session.cpp` `sendPerformanceTelemetry` — same:
  Go reads `s.snapshot` raw under `RLock` at session.go:768.

Left untouched (Go-faithful, all under `snapshotMu` on the canonical snapshot
whose session ref keeps rc>=1, or on caller-owned clones): `getSnapshot`
callerRef, `updateSnapshot` newSnapshot ref, `publishGlobalDiagnostics`,
`RetainSnapshot` (TryAdoptSnapshotInBackground callers pass owned clones),
`warmAutoImports` `tryRef`. The assert itself is **not** silenced — Go keeps
the same panic.

## Evidence

- Baseline (d6ba9d0491, unmodified) stress: 5 rounds x 20 parallel
  `fourslashrunner -run TestUpdateToClassStatics` = **1 fail / 100 runs**
  (`tsc internal error: snapshot ref on disposed snapshot`) — reproduces the
  flake on demand.
- Fixed build, identical loop: **0 fail / 100 runs**.
- `./fourslashrunner -run 'Test[T-Z]'` shard recount: **116/116, 0 fails**.
- `unittestrunner` **1110/1110**, `tsctestrunner` **117/117**.
- `go build ./tsc/...` green.
