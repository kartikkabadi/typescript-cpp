# Watch-mode parity report

Verification of `tscpp` watch mode (`tsc -w`, `tsc -b -w`) against the Go
oracle (`tsc/cmd/tsc`, built as `/tmp/tsgo`). Harness: `watch_parity.py`
(this directory) — it runs both binaries over identical scratch projects,
applies the same mutations while watching, and diffs normalized stdout,
the full emitted file tree (including `.tsbuildinfo` bytes), per step.

Run: `python3 cpp/tools/watch_parity.py` (binaries via `--go`/`--cpp`).

## Results — 9/9 PASS

| scenario | steps | result |
|---|---|---|
| edit-source | 2 | PASS |
| type-error-add-clear | 3 | PASS |
| add-file-and-import | 2 | PASS |
| delete-imported-file | 2 | PASS |
| edit-node-modules-dts | 2 | PASS |
| tsconfig-edit | 2 | PASS |
| build-watch-2proj (`-b -w`, 2-project graph) | 3 | PASS |
| config-option-diag-location | 1 | PASS |
| rapid-successive-edits (debounce burst) | 2 | PASS |

Per-step stdout is identical after normalizing `HH:MM:SS AM/PM - `
timestamps, ANSI screen clears, and the per-side project directory
(mapped to `<PROJ>`). Emitted trees are byte-identical, including
`app/dist/*.tsbuildinfo` in the `-b -w` scenario. `rapid-successive-edits`
compares output/trees only (cycle counts may legitimately differ under
debounce timing; in practice both produced 5 cycles for 5 spaced appends).

## Bugs found and fixed in cpp/

All six were genuine porting defects; `tsc/` was never modified.

1. **Buffered status writer never flushed** — `tsc -w` printed nothing on a
   pipe. `cpp/internal/execute/tsc/diagnostics.cpp`
   (`CreateBuilderStatusReporter` ~L204, `CreateWatchStatusReporter` ~L235)
   write into a buffered `sys::Writer`; Go's `tsc/internal/execute/tsc/
   diagnostics.go` reporters are effectively line-flushed. Fix: `flush()`
   after the formatted write in both reporters.

2. **WatchManager::RunLoop self-deadlock** — `cpp/internal/execute/
   watchmanager/watchmanager.cpp`: `RunLoop` held `mu` while calling a
   callback path that re-locked `mu`, so after the first cycle the watch
   loop froze (Go's `watchmanager.go` drains events outside the lock).
   Fix: narrow the critical section to the flag/list updates only.

3. **`createBuildTasks` captured `config` by reference** —
   `cpp/internal/execute/build/orchestrator.cpp` (~L251):
   `[this, &config, ...]` captured (a) the shared loop variable — Go 1.22+
   gives each range iteration its own binding — and (b) a `config` bound
   to a *temporary* returned by `ResolvedProjectReferencePaths()`, so the
   capture dangled after the loop → SIGSEGV on multi-project `-b`. Fix:
   capture by value (`[this, config, ...]`), matching
   `orchestrator.go` `createBuildTasks` semantics.

4. **`fileTime{}` is not Go's `time.Time{}` zero value** —
   `cpp/internal/execute/build/build.h`, `buildtask.cpp`,
   `cpp/internal/execute/incremental/host.cpp`. `fileTime{}` is the
   `file_clock` *epoch* — on libstdc++ that's 2174, i.e. far *future* —
   while Go's zero `time.Time` is year 1, the *minimum*. Every
   "oldest input / newest input" comparison in `buildtask.go`'s
   up-to-date checks inverted: verbose mode printed `newest input ''`
   with wrong out-of-date reasons, and the `.tsbuildinfo` mtime
   short-circuit misfired. Fix: `fileTimeZero = fileTime::min()`
   used everywhere Go relies on the zero sentinel (incl. `GetMTime`'s
   stat-failure return).

5. **Debounce lost-wakeup ate every file event after the first** —
   `cpp/internal/fswatch/debounce.cpp` `latchWait` waited on a
   generation counter (`waitGen != gen`). A `trigger()` landing while
   the loop was still inside `fireCallbacks`/`coalesceWait` bumped the
   generation before `latchWait` snapshotted `gen`, so the wakeup was
   permanently eaten — observed symptom: in `tsc -b -w`, `strace` showed
   fanotify events reaching `dirWatch::notify` (`db->trigger()` called)
   but `triggerCallbacks`/`onWatchEvents`/`DoCycle` never ran. Go's
   `debounce.go` `latchWait` is `<-waitCh` — a receive on the channel's
   *closed state*, not a delta since wait-entry. Fix: wait on
   `notified` (the closed state).

6. **`verifyCompilerOptions` dropped config-file locations** —
   `cpp/internal/compiler/program.cpp` (~L1487) stubbed
   `sourceFile()`/`getCompilerOptionsPropertySyntax()`/
   `getCompilerOptionsObjectLiteralSyntax()`/`forEachOptionPathsSyntax`
   as permanently nil under a "no config file in this mode" assumption.
   Wrong: the function runs after config parse, so option diagnostics
   (e.g. TS5108 `Option 'moduleResolution=node10' has been removed`)
   rendered bare `error TS5108:` instead of `tsconfig.json(1,92): ...`,
   and `paths`-per-key diagnostics lost their value-node spans; the
   baseUrl→`"paths"` `useInstead` suggestion chain was also dropped.
   Ported for real from `tsc/internal/compiler/program.go` ~L888-1134.

## Pre-existing issues (documented, not introduced here)

- **`tscpp` full binary does not link**: 6 duplicate `tsc::api::*`/`ls::`
  symbol definitions in `libtsc.a` (owned by another slice). All runs use
  `/tmp/tscpp_probe` = `main.cpp.o + sys.cpp.o + probe_stub.o + libtsc.a`
  (recipe at the top of `watch_parity.py`'s session docs / task
  description). This is a build-graph issue, not watch-mode logic.
- `testrunner_deps.cpp`: braced-initializer overload fix needed for the
  tree to compile (unrelated to watch mode; already on this branch).

## Coverage notes / residual risk

- Covered: single-project `-w` (edit, type error cycle, add/delete file,
  node_modules `.d.ts`, tsconfig edit, rapid-edit burst) and 2-project
  `-b -w` (upstream + downstream edits). Emit content, diagnostics text,
  `.tsbuildinfo` bytes, and cycle ordering all compare equal.
- Not covered: >2-project diamond graphs, `preserveWatchOutput`,
  symlinks, fanotify `FAN_RENAME` pairing on exotic rename sequences
  (sed-style temp+rename is covered implicitly), polling backend, and
  `--watchFile` strategies other than the defaults.
- A `tscpp` hang where Go proceeds would itself be a divergence; none
  remain in the covered scenarios after fix 5.
