# TSan BFS family — fix report

Companion to `TSAN_REPORT.md`. Covers the one data-race family it left
unresolved: reports attributed to `findOrCreateDefaultConfiguredProjectWorker`
BFS level workers (`pcb:1524` / `utilities.h:374+437`).

## Root cause (verified from a live reproduction)

The "racing caller-frame containers" hypothesis in `TSAN_REPORT.md` was wrong.
The BFS scaffolding itself is sound — `BreadthFirstSearchParallelEx` joins every
level's `std::thread`s before returning, each worker writes only its own
`next[i]` slot, jobs are bare `new` (GC parity), `visited` is a `SyncSet`, and
the caller-frame `configs` is a `SyncMap`.

The raced object is **one `SimpleProgram`'s
`importHelpersImportSpecifiers` / `filesByPath` unordered_maps**, reached from
`SimpleProgram::ReuseProgram` (program.cpp:3269 et al.) inside
`Project::CreateProgram` ← `ProjectCollectionBuilder::updateProgram`:

- `SimpleProgram::UpdateProgram` is invoked on `p->Program` — the *old*
  program — which is shared across builders: `Project::Clone()`
  (project.cpp:224, `clone->Program = Program`) copies the pointer, and every
  `dirty::SyncMap` entry `Change` clones the project per-builder.
- `Snapshot::Clone` runs on at least two concurrent lanes in both Go and C++:
  the `snapshotMu`-serialized `updateSnapshot` path **and** the background
  `warmAutoImportCache` task (session.go:2127, a second `Clone` outside
  `snapshotMu`) — plus `BreadthFirstSearchParallelEx` level workers inside a
  `visit` → `updateProgram` (pcb:1521).
- Each concurrent `Clone` gets a *fresh* `ProjectCollectionBuilder`, so its
  `dirty::SyncMap` dirty-maps are distinct: ad-hoc entries for the same
  project key never `LoadOrStore`-converge to one winner entry, hence never
  share one mutex. Two builders therefore mutate the same shared
  `SimpleProgram` at once — exactly what the stacks show (BFS `visit` worker
  vs. `warmAutoImportCache`/`updateSnapshot` clone, different mutex sets on
  both sides).
- **The C++ divergence:** `filesByPath[changedFilePath]` and
  `importHelpersImportSpecifiers[oldFile->Path()]` used `operator[]`, which
  *inserts* a null entry (and can rehash) on a miss — a write on the shared
  old program. Go's `m[k]` (program.go:401,416) is a pure read that returns
  nil on a miss. With inserts eliminated the old program is effectively
  immutable post-construction, so concurrent `UpdateProgram`s on it become
  pure readers — matching the Go contract.

## Fix

`cpp/internal/compiler/program.cpp` (`SimpleProgram::ReuseProgram`, +14/−4):
replace the three `operator[]` lookups on `this` (`filesByPath`,
`importHelpersImportSpecifiers` ×2) with `find()`-based reads — the existing
`GetSourceFileByPath`/`GetImportHelpersImportSpecifier` style. No other
write to a live program exists: `result->*` writes are construction-local,
`initCheckerPool` runs on `result`, `lazyValue::tryReuse` is atomic-guarded
and only writes `result`'s slots, and `resolutionData_->Clone()` is a
read+clone of `this`. Mutex'd/`call_once` caches (SyncMap'd reason maps,
`computedDiagnosticsMu_`, `compilerOptionsSyntaxMu_`, `packagesMapOnce_`)
were already covered by the previous sweep.

## Signature counts

| | before | after |
|---|---|---|
| Reports in the assigned family (`unordered_map<string,Node*>` in `ReuseProgram`/`updateProgram`, incl. `filesByPath` variant) | 9 in one full-suite run (`api.TestGetCurrentLanguageServerSnapshotOpeningLSPFileEnsuresConfiguredProgram`; ~8–23 across sweeps) | **0** in the full-suite post-fix run |
| `unittestrunner -run 'project\.'` under TSan | intermittent repro | 57/57 pass, 0 data races (577 documented `sync.Map.Range` lock-order inversions only) |
| `api.TestGetCurrentLanguageServerSnapshot*` ×3 under TSan | — | 0 data races |
| Full `unittestrunner` under TSan | family reports present | **0 for the family**; 1,121/1,122 — the one FAIL is a pre-existing TSan-only crash, see below |
| Release `unittestrunner` | 1,122/1,122 | 1,122/1,122 |

## Out-of-scope observations (not this family)

The same runs also produced the already-**documented** Go-parity fd
signature from `TSAN_REPORT.md` ("inotify `pipeWriteFD` close-vs-write +
fanotify fd" — `inotify_linux.go` parity): `close(fd)` racing
`read`/`write(fd)` on the same descriptor — inotify `closeFDs`/
`deferCloseFDs::~deferCloseFDs` (inotify.cpp:208/150) vs `shutdown`'s
stop-byte `write` (inotify.cpp:234), the fanotify twins, and the
contentmapper test-harness `process::close()` vs `process::read` on the
child pipe (tests_mapper.cpp:97). Pre-fix: 4 reports; post-fix run: 4.
Go has the identical atomic fd protocol on the same goroutine topology —
the loser sees EBADF/EIO — so it stays documented, not fixed.

Separately, `ls.TestSelectionRangeDepthIsLimited` segfaults (SIGSEGV)
under TSan **on both the pre-fix and post-fix binaries** — deterministic,
survives `ulimit -s unlimited`, passes in the normal Release build — a
pre-existing TSan-only environmental failure unrelated to this change
(the test never reaches `ReuseProgram`; it's the depth-limited
selection-range recursion that TSan-instrumented frames overflow).

## Repro recipe

```
cmake -B cpp/build-tsan -S cpp -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTSCPP_MIMALLOC=OFF -DCMAKE_C_COMPILER=clang-15 -DCMAKE_CXX_COMPILER=clang++-15 \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread -fuse-ld=lld"
sudo sysctl kernel.randomize_va_space=0
cd cpp/build-tsan && ninja unittestrunner
TSAN_OPTIONS="halt_on_error=0 report_bugs=1 exitcode=0 report_thread_leaks=0 \
  log_path=/tmp/xx" ./unittestrunner -run 'api\.|project\.'
```

Intermittent: needs a file change landing while a warmAutoImport/snapshot
clone rebuilds programs — the api test above was the most reliable trigger.
