# Windows parity — typescript-cpp

Status of the C++23 port (`cpp/`) on Windows x64: what builds, what passes,
and every platform-specific divergence found vs the Go oracle's GOOS=windows
behavior.

## Toolchain

| Tool | Version | Notes |
|---|---|---|
| clang / clang++ | 22.1.8 | `clang++` (GNU-style driver, MSVC ABI) — **working compiler** |
| MSVC cl | 19.44 | Present via Build Tools but not usable for this codebase (heavy designated-initializer + GCC-extension usage) |
| CMake | 4.4.4 | `-G Ninja` |
| Ninja | 1.13.2 | |
| Go | 1.27.1 | oracles only (`tsc/cmd/{checkdump,parsedump,bindump}`) |
| zlib | 1.3.1 source | built via `-DTSCPP_ZLIB_SRC=<dir>` (no system zlib on the box) |

Configure + build:

```powershell
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  -DTSCPP_ZLIB_SRC=C:\Users\Administrator\zlib-src\zlib-1.3.1
ninja -C build
```

Produces `tscpp.exe`, `fourslashrunner.exe`, `tsctestrunner.exe`,
`unittestrunner.exe`.

## Suite tallies on Windows

| Suite | Result | Notes |
|---|---|---|
| `tscpp check` vs Go `checkdump.exe` (50-file corpus) | 50/50 byte-identical | raw-byte stdout compare incl. exit codes |
| `tsctestrunner` | 99/99 | full suite |
| `unittestrunner` | **449/449 pass** (3 skip) | 452 registered tests — see section 7 |
| `fourslashrunner` | **4130/4130 pass** (430 skip) | full suite, zero failures — see sections 2–6 |

## Build fixes (committed)

- `internal/win32/w32compat.{h,cpp}` — the POSIX compat layer:
  `w32_stat` (`#define stat` remap), `w32::setErrnoPair`,
  `w32::fileLength`, `w32::setBinaryStdio`, process helpers
  (`getpid`, `waitpid`, `kill`, `w32::spawnvp`, `w32::selfExePath`),
  `getcwdString`/`realpath(std::string)` exported at global scope.
  Header sets `_CRT_DECLARE_NONSTDC_NAMES=0` so MSVCRT's POSIX aliases
  (`write`, `read`, `close`, `strdup`, …) don't collide with ours.
- **WinAPI A/W macro trap** — `windows.h` maps string-taking APIs onto
  `*A`/`*W` macros, which also rewrote *our* same-named methods
  (`SimpleProgram::GetCurrentDirectory` → `GetCurrentDirectoryA` →
  undefined symbol). Fixed by `#undef`-ing the colliding macros after
  `windows.h` in `w32compat.h`; all compat code calls the explicit `*W`
  entry points.
- `internal/fswatch/` — `walkdir_windows.cpp` (Win32 directory walk),
  `windows.cpp` (ReadDirectoryChangesW watcher), and `_WIN32` stubs for
  `inotifyWatcher()`/`fanotifyWatcher()` that return a never-available
  watcher — matching GOOS=windows where those watcher `init()`s are
  Linux-gated.
- `internal/ipc/transport_windows.cpp` — named-pipe transport for the
  ipc layer.
- `tscpp`/`sys.cpp`, `vfs/osvfs`, `nativepath`, `fileclock` — `\\`-style
  path handling, case-insensitive compares, FILETIME-based file times.

## Runtime divergences found and fixed

### 1. CRT text-mode stdout translation (fixed)

The CRT default is text mode: `\n` → `\r\n` on fds 1/2. Go writes raw
bytes, so `tscpp check` output diffed from `checkdump.exe` on every line
ending (19/50 → crashes into byte mismatches). Fix: `w32::setBinaryStdio()`
(`_setmode(fd, _O_BINARY)` on fds 0–2) called at the top of every
executable's `main` and in fourslashrunner's `--test-child` path.
→ **50/50 byte-identical** after fix. `tsctestrunner` got the same fix.

### 2. fourslash mass crash — nil `WatchedFiles` dereference (fixed)

1018/4130 fourslash tests died with AccessViolation
(`[killed by signal 11]`) inside `InitOnceBeginInitialize` on
background-queue worker threads. ASan pinpointed it:
`Session::updateWatches` dereferenced `project->typingsWatch` /
`snapshot->autoImportsWatch` / `entry->rootFilesWatch` unconditionally
(`->ID()`, `->Clone()`), but `typingsWatch` is `nullptr` whenever
`SessionOptions.TypingsLocation` is empty (autoImportsWatch likewise).
`std::call_once` then read the `computeWatchersOnce` member at
`nullptr + offset` → AV.

In Go this is legal: `WatchedFiles.ID()`/`Clone()` are nil-receiver-safe
(`watch.go:266`, `watch.go:281`). The port already provided the same
contract via free helpers `watchedFilesID()`/`watchedFilesClone()`
(member `this == nullptr` guards don't work — clang folds them at -O2),
but `session.cpp`, `snapshot.cpp`, and `projectcollectionbuilder.cpp`
bypassed them at five call sites. All now use the helpers — matching Go
exactly. Post-fix spot checks of previously crashing tests
(`TestAugmentedTypesModule2`, `TestAutoImportPackageJsonImportsLength1`,
`TestCompletionsImport_fromAmbientModule`,
`TestImportFixWithMultipleModuleExportAssignment`,
`TestCompletionsImportYieldExpression`) all pass.

The same null deref is latent UB on Linux too; it simply went unhit
there (the watch-update path only runs under a watch-capable LSP
session, and Windows thread timing put it in the hot path).

### 3. fork → spawn for fourslash tests

Windows has no `fork()`. `fourslashrunner` on `_WIN32` respawns its own
image as `--test-child <name>` via `w32::spawnvp` (CreateProcess) with
stdout+stderr piped back — same per-test isolation as the POSIX fork
path, same exit-code and signal reporting (`[killed by signal N]`).

### 4. `std::regex` construction — escaping exception via inlining (fixed)

`IsExcludedByRegex` constructs `std::regex` inside a `try`/`catch`
(the Go original treats a bad pattern as a failed compile). At
clang -O3 the `stringToRegex` ctor call was inlined into
`IsExcludedByRegex` in a way that placed it in a terminate-scoped EH
state: a `regex_error` then bypassed the `catch` and `std::terminate`d
the process → `__fastfail` (`[killed by signal 22]`). Fixed by marking
the factory `__declspec(noinline)` under `_WIN32`, and catching
`regex_error` around the search itself for belt-and-suspenders.

Separately, MSVC's `std::regex` is *not* safe for concurrent searches
on a shared regex object (its NFA evaluation mutates per-search state
without locks), while Go's `regexp.Regexp` is explicitly documented
safe for concurrent use. Pattern-cache users race here on Windows
thread timing, so searches are serialized behind `regexSearchMu` —
same observable semantics as Go.

### 5. Go GC → C++ lifetime equivalence for detached threads (fixed)

The port maps Go goroutines onto detached `std::thread`s. Go's GC
keeps every captured object alive for as long as a goroutine can reach
it; a detached thread in C++ keeps running after the owner's
refcount drops to zero and dereferences freed memory → racy
`__fastfail`/`AV` (worse *without* a debugger — page heap serializes
the race to 0/20, a debugger 0/10). Fixes, all mirroring the GC
invariant "captured => alive":

- `Server` — `readLoop` and the per-request async-work thread capture
  `shared_from_this()` instead of raw `this` (`lsp_server.cpp`).
- `serverProgressReporter` — holds `std::weak_ptr<Server>`; the four
  reporter methods `lock()` and no-op when expired (`lsp_progress.cpp`).
  `projectLoadingProgress` is `enable_shared_from_this` and its
  detached `run()` loop captures `self` (`startRun()`).
- `Snapshot` lifetime — `Session::updateSnapshot` and
  `triggerATAForUpdatedProjects` `ref()` the snapshots their
  backgroundQueue workers capture before the session's own `Deref`,
  and the workers release via a `derefGuard`. Two previously bare
  escapes (watch-delete path, `sendPerformanceTelemetry`) got the
  same treatment (`session.cpp`).
- `background::Queue::Enqueue` — now returns `bool` (false on
  closed/cancelled) so callers can release refs they took for the
  worker. Go's `Enqueue` silently drops in the same situation
  (`background/queue.go`); the bool just lets C++ undo its ref()s.
- `Session` — `SessionInit` gained `ClientRef`/`NpmExecutorRef`
  shared-pins: session background workers call `client->PublishDiagnostics`
  / `npmExecutor->NpmInstall` on detached threads and must not see a
  torn-down `Server` (`sessiontypes.h`, `session.h`,
  `lsp_handlers.cpp:437-438`). This eliminates a residual racy
  `__fastfail` (~10% on `TestFormattingFatArrowFunctions`,
  `TestOrganizeImportsType9` → 0/30 each after fix).
- Post-`wait()` stack UAFs — the local `errgroup`/`waitGroup` helpers
  (`lsp_server.cpp`, `ata/ata.cpp`, `ipc/ipc.h`) and
  `throttleGroup::go` let detached workers decrement/notify stack-local
  `std::mutex`/`std::condition_variable`/counter state after `wait()`
  had already returned and the owner's frame was gone. All now share a
  heap `shared` block (`mutex`+`cv`+`pending`+`firstErr`) captured by
  value into the worker lambda, so the sync state outlives both the
  waiter and the workers.
- `fswatch::closeWatch` — freed the `windowsSubscription`
  (`w->state.reset()`) while both `closeWatch` and the run thread still
  touched it. Now holds a `shared_ptr` through reset+stop, and
  `subscribe` captures the `shared_ptr<windowsSubscription>` itself for
  the run thread (a weak_ptr that could expire left `doneFlag`
  un-signalled → hang).
- `at::errGroup`/`throttleGroup::go` — captured `this`; now capture the
  `semaphore`/`shared` state by value (`ata.cpp`).

Root cause verification: `std::set_terminate` never fired on these
crashes and WER/LocalDumps produced no dumps — proving raw
`__fastfail` (memory corruption), not an escaping exception. The
0/30-per-test result is vs a ~3/30 baseline before the fix.

### 6. Process teardown vs detached threads — `TerminateProcess` (fixed)

The POSIX child path `_exit()`s after the test body, skipping CRT
teardown entirely. Windows `_exit` → `ExitProcess` still runs
`DLL_PROCESS_DETACH` in every loaded DLL: the CRT/MSVCP then tear down
thread-shared state while the test's *leaked* detached threads are
inside mutex/CV/heap calls → racy `__fastfail` (0xC0000409,
`[killed by signal 22]`, 5–15% on a handful of tests).

Heartbeat instrumentation proved every residual crash landed after the
test body + stream flushes completed — i.e. inside teardown, never in
test code (ASan saw nothing because the corruption lives inside
uninstrumented MSVCP internals). Fix: the `--test-child` path and the
per-test alarm handler call `TerminateProcess(GetCurrentProcess(),
code)` — the kernel kills all threads immediately with zero user-mode
teardown, the true equivalent of the POSIX `_exit` contract (and of
what Go's `os.Exit`/`test` child death produces: Go has no C++ static
destructors racing stray threads). Residual fastfails → **0/160** on
the four previously-racy tests.

The per-test watchdog `kTestTimeoutSeconds` was raised 180 → 600:
`TestExcessivelyLargeArrayLiteralCompletions` legitimately needs ~9
minutes on this box (it passes — the previous 180 s cap misreported a
slow pass as a hang). Go's own default `-timeout 10m` is per binary;
600 s per test child is the analogous bound.

### 7. unittestrunner merge — test-file port fixes (fixed)

After merging `devin/cpp-port` (which added 452 registered unit tests),
eight Windows-specific breaks surfaced — all in test-harness code the
earlier port predated:

- `stderr` is an MSVCRT macro (`#define stderr &__iob_func()[2]`) —
  parameters named `stderr` in `tests_host.cpp`/`tests_mapper.cpp`
  mangled into `(&__iob_func()[2])`. Renamed to `stderr_`, the
  codebase's existing convention (`// stderr_: CRT macro dodge`).
- `tests_bundled.cpp` — `struct stat`/`::stat` → `w32compat.h`
  (POSIX-shaped `struct stat` + `::stat` are the compat layer's job).
- `tests_converters.cpp` — `unistd.h`/`sys/wait.h` gated to POSIX;
  `nodeAvailable()` uses `w32::lookPath` on Windows (PATHEXT + `;`
  PATH split = Go's `exec.LookPath`); the node-oracle spawn uses
  `w32::spawnvp` with pipe ends as child stdio instead of
  fork+`execlp`.
- `tests_mapper.cpp` (`contentmappertest.TestOutOfProcess`) —
  fork+`execv("/proc/self/exe")` → `w32::spawnvp({w32::selfExePath()})`
  with `SpawnStdio{stdinFd, stdoutFd}`; the helper child also needed
  `w32::setBinaryStdio()` before `Serve` — the mapper protocol is
  binary and CRT text mode corrupts it ("read header: io: read made
  no progress").
- `tests_realpath.cpp` — restored Go's `mklink` GOOS switch:
  dirs → `cmd /c mklink /J` (junction, no privilege); files →
  `create_symlink`, `Skipf` on `ERROR_PRIVILEGE_NOT_HELD`.
- `tests_os.cpp` (`UseCaseSensitiveFileNames`) — restored the
  `runtime.GOOS` switch the port dropped: asserts `!UseCaseSensitiveFileNames()`
  on `_WIN32`.
- `dbg_main.cpp` — pthread 64MB-stack debug runner → `CreateThread`
  with `STACK_SIZE` reserve on `_WIN32`.
- `unittestrunner/main.cpp` — same teardown contract as
  fourslashrunner: `--test-child` exits via `TerminateProcess`, the
  alarm handler too, watchdog 180→600 s.

Two non-platform bugs the Windows run exposed (also latent on Linux):

- **Mojibake test literals** — `tests_host.cpp` had cp1252
  double-encoded strings: `"Ã©x"` for `"éx"` (C3 A9) and `"ðŸ˜€"` for
  `"😀"` (F0 9F 98 80), plus `—` in comments. The position-encoding
  tests failed because the corrupted content shifted every
  code-point boundary. Byte-level corrected to match the Go source
  exactly.
- **`testGoexit` escaping a detached thread** —
  `TestHostClosesProcessWhenReadLoopFails`'s fake-server goroutine
  calls `assert::Assert` (→ `t->Fatal` → throws `testGoexit`). In Go,
  `FailNow` on a non-test goroutine marks the test failed and kills
  only that goroutine; on a detached `std::thread` the escaping throw
  is `std::terminate` → `__fastfail` (~60% crash rate). The thread now
  catches `testGoexit` at top level.

And one semantics gap in the test harness's `net.Pipe` port
(`pipeHalf::write`): a fully-consumed write still returned
`ErrClosedPipe` if the peer's close landed between drain and re-lock.
Go's `net.Pipe` returns success once the reader consumed the bytes —
fixed (returns n=size on empty buf; partial-transfer case returns
n + ErrClosedPipe like Go).

## Go-on-Windows behaviors verified matching

| Behavior | Go (GOOS=windows) | Port |
|---|---|---|
| stdout bytes | raw, no translation | `_O_BINARY` on 0–2 ✓ |
| FS case sensitivity | `isCaseSensitive() == false` | `UseCaseSensitiveFileNames() == false` ✓ |
| Path separators | `\\` separators in printed paths | matches oracle byte-for-byte ✓ |
| inotify/fanotify watchers | not registered | stubs return `Available()==false` ✓ |
| `WatchedFiles` nil receiver | `ID()==""`, `Clone()==nil` | via `watchedFilesID`/`watchedFilesClone` ✓ |
| `getpid`/spawn/waitpid | Go equivalents | `w32::` shims ✓ |

## Remaining divergences / known issues

- **MSVC `cl` cannot build this codebase** — designated initializers
  plus GCC extensions (statement expressions, `__attribute__`,
  computed StringTable inits) are rejected across the board. `clang++`
  with the MSVC ABI is the working toolchain; `clang-cl` also works
  but the Ninja build was validated with the GNU-style driver.
- **Per-test runtime is slower** — spawn-per-test plus Windows FS
  latency makes the fourslash suite ~2–4× slower than the Linux
  fork-per-test run; `TestExcessivelyLargeArrayLiteralCompletions`
  alone needs ~9 min (hence `kTestTimeoutSeconds = 600`).
- **Residual detached-thread lifetimes** — the section-5 fixes pin
  every captured object detached workers can outlive, and
  `TerminateProcess` absorbs teardown races in the test child. The
  long-lived `tscpp` server binary still relies on those pins; no
  residual crash has been observed in the suite, but any future
  detached-thread addition must follow the same capture-by-value /
  shared-state rule.
- **ASan is not usable for CRT-init teardown bugs** — Debug CRT trips
  a false-positive in `initterm_e` before `main`; Release ASan
  instruments our code but not MSVCP internals, exactly where the
  teardown corruption lives.
- **zlib** — built from source via `-DTSCPP_ZLIB_SRC` (no system zlib
  on Windows); not a behavior divergence.
