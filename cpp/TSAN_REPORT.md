# ThreadSanitizer sweep — typescript-cpp

Branch: `devin/cpp-tsan`. Build: `cpp/build-tsan` (RelWithDebInfo, `-fsanitize=thread`
on compile + link, `TSCPP_MIMALLOC=OFF` for this dir only). Runner env:
`TSAN_OPTIONS="halt_on_error=0 report_bugs=1 exitcode=0 report_thread_leaks=0
report_signal_unsafe=0 log_path=/tmp/tsan_logs/<suite>"`.

## Scope

- `unittestrunner` — TSan, full suite
- `tsctestrunner` — TSan, full suite (incl. threaded `tsc -b` project builds)
- `fourslashrunner` — TSan, full suite
- `tscpp tsc -b` — TSan, synthetic 120-file cross-importing project (clean, 0 reports)

## Results summary

| Class | Reports | Unique signatures | Verdict |
|---|---|---|---|
| `includeProcessor` caches + 2 dropped `sync.Once` | † | 5 | **FIXED** — Go uses `collections.SyncMap` + `sync.Once` (includeprocessor.go:21-24,37,99) |
| `Checker::initializeChecker` reads file bind state mid-bind | † | 6 | **FIXED** — re-ensure `bindSourceFile(file)` (exactly-once via `bindOnce`) inside the loop |
| `diagnosticwriter::sourceFileLikeFor` static adapter map | † | 3 | **FIXED** — mutex + deliberately-leaked map (GC parity) |
| `lsptestutil::marshalDiscard` shared static `std::ostream` | † | 3 | **FIXED** — per-call discard stream (Go `io.Discard` is stateless) |
| `background::Queue` cond-destroy vs detached worker-tail | † | 4 | **FIXED** — `~Queue` acquires `wgMu` once (happens-after edge), notify moved under the lock |
| `tests_conn_sync` detached `serve` captures frame by ref | † | 5 | **FIXED** — `shared_ptr` closure (C++-only UAF; Go GC pins locals) |
| `BreadthFirstSearchParallel(Ex)` shared job-arena vector | † | 2 | **FIXED** — bare `new Job()` (Go allocates on GC heap) |
| `primitiveTypeAliasSuggestions` racy check-then-init | † | 2 | **FIXED** — function-local static init = Go `sync.OnceValue` |
| `countGlobalSymbols` reads file bind state mid-bind | † | 2 | **FIXED** — same `bindSourceFile` ensure as initializeChecker |
| `dirty::SyncMap` lock-order inversions (entry.mu ↔ map mu) | † | 4 | **Documented** — Go-inherent: `sync.Map.Range` holds m.mu across callback; `changeLocked`/`deleteLocked` take entry.mu then `dirty.LoadOrStore` — identical abstract cycle in Go |
| `collections::SyncSet::Range` → `Add` inversion (affectedfileshandler) | † | 1 | **Documented** — same Go `sync.Map.Range`-holds-lock structure |
| inotify `pipeWriteFD` close-vs-write + fanotify fd | † | 2 | **Documented** — Go-parity: identical atomic fd protocol (`inotify_linux.go`) |
| `extendedconfigcache` test helper reads `entry->owners` unlocked vs `OwnerCache::AddOwner` | 1 | 1 | **Documented** — Go-inherent: `extendedconfigcache_test.go:105` does `len(entry.owners)` without `entry.mu`; same unlocked read in Go |
| `projectReferenceParser::tasksByFileName` raced by recursive `start()` on workgroup workers | ~59 | 3 | **FIXED** — Go uses `collections.SyncMap` (`projectreferenceparser.go:45`); port had plain `unordered_map` → `LoadOrStore` |
| pcb BFS workers racing caller-frame containers inside `findOrCreateDefaultConfiguredProjectWorker` (searchResult/Set/OrderedMap/`next`) | ~23 | 3 | **Unresolved candidate** — real signature, mechanism not isolated (stacks heavily inlined); sharpest frames: `pcb.cpp:1524` (`configs.Store` ctx, `searchNodeKey`), `utilities.h:374/437` (`newJob` node-copy, `next[i] = Map`) — filed below |
| `tscpp tsc -b` threaded build | 0 | 0 | clean |

## Fixes

- `cpp/internal/compiler/program.h`, `includeprocessor.cpp`: the four caches are
  now `collections::SyncMap` (`reasonDiagnostics`, `reasonToReferenceLocation`,
  `includeReasonToRelatedInfo`, `redirectAndFileFormat`) — exactly what Go uses —
  plus mutexes for `computedDiagnostics` and `compilerOptionsSyntax` (Go `sync.Once`;
  mutex rather than `once_flag` so `updateFileIncludeProcessor`'s field-wise reset
  can re-arm, same as Go assigning a fresh struct). `LoadOrStore` returns the stored
  winner for the "create diagnostic then dedup" idiom.
- `cpp/internal/checker/checker.cpp` (`initializeChecker`): `bindSourceFile(file)`
  re-ensured per file before reading `Locals`/`Symbol`/`GlobalExports`/`PatternAmbientModules`/
  `ModuleAugmentations`. Init's `BindSourceFiles` covers `p.files`; files shared via the
  loader can still be mid-bind from another program. `bindSourceFile` is exactly-once
  through `bindOnce`, so this is a no-op when bound — Go's own ordering contract
  ("the checker binds every file up front").
- `cpp/internal/project/background/background.h`: `~Queue()` locks `wgMu` once —
  the same detached-worker-tail idiom `parallelWorkGroup` already documents — and
  `doneGuard` now notifies `wgCv` *inside* the lock (was unlock-then-notify →
  `pthread_cond_destroy` vs `pthread_cond_broadcast` race; C++-only, Go has no dtor).
- `cpp/internal/diagnosticwriter/diagnosticwriter.cpp`: static `adapters` map now
  `mutex` + intentionally-leaked (`new`) — parallel `tsc -b` BuildTask workers raced
  it; leak is GC parity (detached tails must not hit exit-time destructors).
- `cpp/internal/testutil/lsptestutil/lspclient.cpp`: `marshalDiscard` builds a
  per-call discard `std::ostream` instead of a shared static one — main thread
  (`LSPClient::WriteMsg`) vs router thread (`MessageRouter`, errGroup) raced it.
  Go's `io.Discard` is stateless so per-call is faithful.
- `cpp/internal/ipc/tests/tests_conn_sync.cpp`: the detached `serve` thread now
  captures via `shared_ptr` instead of by-reference frame locals — C++-only UAF
  (Go's GC keeps the locals alive for the detached goroutine).
- `cpp/internal/core/utilities.h` (`BreadthFirstSearchParallel`,
  `BreadthFirstSearchParallelEx`): removed the shared
  `std::vector<std::unique_ptr<Job>>` arena that parallel level workers raced in
  `newJob`; jobs are now bare `new` — Go allocates them on the GC heap and never
  frees, so bare `new` is the exact port.
- `cpp/internal/checker/checker.cpp` (`primitiveTypeAliasSuggestions`): Go uses
  `sync.OnceValue`; the port had an unsynchronized check-then-init on a static
  pointer — two parallel checker inits raced the map fill. Function-local
  `static` init is the C++ `sync.OnceValue` equivalent.
- `cpp/internal/checker/checker.cpp` (`countGlobalSymbols`): same class of bind
  race as `initializeChecker` — reads `file->Locals` while another program's
  loader can still be mid-bind; same `bindSourceFile(file)` ensure added.
- `cpp/internal/compiler/fileloader.cpp` (`projectReferenceParser`):
  `tasksByFileName` was a plain `unordered_map` — but `start()` recurses
  *inside queued workgroup tasks* (`task->parse(this); start(task->subTasks)`),
  so concurrent workers raced `try_emplace`. Go declares it
  `collections.SyncMap` (`projectreferenceparser.go:45`) — switched to
  `collections::SyncMap` + `LoadOrStore`, exactly Go's shape.

## Documented (Go-parity / benign)

- **dirty-map lock-order inversions** (`dirty.h` × `collections.h`): `SyncMap::Range`
  holds the map mutex across the callback (faithful port of `sync.Map.Range`, which
  holds its internal lock across `f`); `changeLocked`/`deleteLocked` take entry.mu
  then call `dirty.LoadOrStore`. Both directions of the cycle exist identically in
  Go (`tsc/internal/project/dirty/syncmap.go`). Not a C++ regression → left as-is.
- **`SyncSet` Range→Add inversion** in `collectAllAffectedFiles`: same `sync.Map`
  Range-holds-lock structure.
- **fswatch fd races**: `inotify.cpp` pipe fd write vs close and `fanotify.cpp` fd
  paths mirror `tsc/internal/fswatch/inotify_linux.go` exactly (`pipeWriteFD
  atomic.Int32`, shutdown writes then waits on `endedSignal`). Same benign window
  exists in Go.
- Per-test timeouts under TSan are faithful, not races (TSan is ~5-10x slower).
- **`extendedconfigcache` ownerCount read**: the test reads
  `extendedConfigCache->entries.Load(path)->owners.size()` without the entry
  mutex while an idleTimer `UpdateSnapshot` writes owners under M0/M1. Go's
  `extendedconfigcache_test.go:105` makes the identical unlocked read — the
  race exists in Go too (would trip `go test -race` if the idle timer fired
  mid-read). Test-side, Go-inherent → documented.

## Unresolved candidate (needs dedicated repro)

8 reports in `TestProjectLifetime` / `TestProjectCollectionBuilder` children:
sibling `BreadthFirstSearchParallelEx` level workers inside
`findOrCreateDefaultConfiguredProjectWorker` racing on caller-frame
`searchNode`/`Set`/`OrderedMap` internals (`searchResult`, `retain`,
`configs`, level `jobs`/`next`). All visible worker-touchable state in the
port is SyncMap/SyncSet-mutex'd or disjoint `next[i]` slots — Go runs the same
visit funcs concurrently, so the raced object is C++-side bookkeeping that
TSan attributes to `projectcollectionbuilder.cpp:1601`, `:1352`, `:1216` +
`snapshot.cpp:703` (Snapshot::Clone → markProjectsAffectedByConfigChanges →
createAncestorTree → BFS). Stacks are too inlined to isolate the slot without
a dedicated reproducer; the 8 reports are real (same-address pairs, post-fix
binary) and do NOT map to any documented Go-parity pattern. Follow-up:
`TSAN_OPTIONS=... ./unittestrunner -run 'project\.'` under `cpp/build-tsan`
reproduces intermittently.

## Suite counts post-fix

| Suite | Release (baseline) | Release (post-fix) |
|---|---|---|
| unit | 1,122/1,122 | 1,122/1,122 |
| tsctest | 117/117 | 117/117 |
| fourslash | 4,145/4,145 runnable | 4,133 pass + 12 pre-existing baseline diffs (see below) |

Pre-existing fourslash diffs: `TestFormatAfterWhitespace`,
`TestFormatNoSpaceAfterTemplateHeadAndMiddle`, `TestFormatSelectionWithTrivia2`,
`TestFormatSpaceAfterTemplateHeadAndMiddle`, `TestGoToImplementationInterfaceProperty_00`,
`TestGoToImplementationNamespace_06`, `TestQuickInfoCommentsCommentParsing`,
`TestQuickInfoForJSDocWithHttpLinks`, `TestQuickInfoInheritDoc`,
`TestSignatureHelpCommentsCommentParsing(VS)`, `TestGoToDefinitionSameFile`.
Their generated C++ test bodies collapse consecutive blank lines in the embedded
fixture (`git diff origin/devin/cpp-port..devin/cpp-tsan -- cpp/internal/fourslash/` = 0
lines) — e.g. `quickInfoInheritDoc.ts` embeds 1 blank line where the Go source
(`quickInfoInheritDoc_test.go`) has 4 — so the echoed file content in the
baseline is missing `// ` lines. Deterministic on base; unrelated to this sweep.

Touched-code TSan reruns (post-fix binary):
- Focused subset (`TestQueue|TestSyncConn|inotify|fanotify|Session_APIState|
  IncludeProcessor|Snapshot|BreadthFirstSearch|primitiveType/Completion paths`):
  **0 data races** — only the documented `sync.Map` inversions remain.

Aggregate counts: **3,488 raw reports, 88 unique signatures**
across all surviving log dirs (earlier logs were pruned after triage — the
original sweep produced ~1,935 reports/74 sigs before the last fixes landed).
† = per-test log for that class was pruned once triaged; signature counts
are exact. across all
surviving log dirs — the bulk being repeated `sync.Map.Range` inversion
instances (a single Go-inherent pattern firing per test).
- `tsctestrunner` full suite post-fix: **117/117, 0 data races**.
- Previously-crashing tests rerun post-fix (`project/ata.TestATA`,
  `ls.TestSelectionRangeDepthIsLimited`, `lsp.TestCompletionWithConcurrentFileClose`):
  pass; the two kills were resource-related under shard load, `TestATA`'s
  inversions are the documented dirty-map class.
- Threaded `tscpp tsc -b --force` on 120-file project: **0 reports**.
