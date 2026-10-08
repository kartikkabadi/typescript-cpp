# macOS parity — typescript-cpp

Status of the C++23 port (`cpp/`) on macOS (Darwin, arm64): what builds,
what passes, and every platform-specific divergence found vs the Go
oracle's GOOS=darwin behavior.

## Toolchain

| Tool | Version | Notes |
|---|---|---|
| Apple clang / clang++ | 21.0.0 | Xcode default toolchain — `cc`/`c++` work as-is; no brew LLVM needed |
| CMake | 4.4.4 | `-G Ninja` (via brew) |
| Ninja | 1.13.2 | via brew |
| Node.js | v24.21.0 | via brew — required to un-skip the node-gated unit tests |

Configure + build:

```sh
mkdir -p cpp/build && cd cpp/build
cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..
ninja tscpp fourslashrunner tsctestrunner unittestrunner
```

Produces `tscpp`, `fourslashrunner`, `tsctestrunner`, `unittestrunner`.

## Suite tallies on macOS

| Suite | Result | Notes |
|---|---|---|
| `unittestrunner` | **1004/1004 pass** | full suite |
| `tsctestrunner` | **116/116 pass** | full suite |
| `fourslashrunner` | **4560/4560 pass** | full suite, zero failures, zero crashes |

## Build fixes (committed)

- `internal/fswatch/fsevents_darwin.cpp` (new) — FSEvents watcher
  backend, matching Go's `fswatch.Default()` → `FSEvents()` dispatch on
  GOOS=darwin (`watcher.go:230`).
- `internal/fswatch/kqueue_darwin.cpp` (new) — kqueue/kevent watcher
  backend.
- `internal/fswatch/inotify.cpp`, `fanotify.cpp` — Linux-only
  implementations now `#ifdef __linux__` gated (matching Go build-tag
  gating; they return unavailable watchers elsewhere).
- `internal/fswatch/watcher.cpp` — `Default()` returns the FSEvents
  watcher under `__APPLE__` (Go `GOOS=="darwin"` case).
- `internal/fswatch/fswatch.h`, `CMakeLists.txt` — declare/register the
  new backends; link `CoreServices`/`CoreFoundation` under `if(APPLE)`.
- `internal/fswatch/walkdir.cpp`, `util.cpp` — directory enumeration via
  `syscall(SYS_getdirentries64, ...)` — see section 2.
- `internal/osutil/osutil.cpp` — `args()` via `_NSGetArgc()`/
  `_NSGetArgv()` (`<crt_externs.h>`) and `executable()` via
  `_NSGetExecutablePath` + `realpath` — macOS has no `/proc`; Go reads
  argv off the kernel startup frame and returns the kernel-recorded
  executable path.
- `cmd/tscpp/sys.cpp` — `pipeCloexec` port (no `pipe2`/`O_CLOEXEC`
  shortcut on darwin).
- `internal/ipc/transport_unix.cpp` — `SO_NOSIGPIPE` on sockets (darwin
  has no `SO_NOSIGPIPE`-free send; MSG_NOSIGNAL doesn't exist).
- `internal/execute/tsc/emit.cpp`, `internal/project/session.cpp` —
  `malloc_zone_statistics(malloc_default_zone(), &ms)` — the 2-arg
  darwin form of the heap stat call.
- `internal/gostd/regexp.cpp` — explicit `<algorithm>` include
  (`std::all_of` not transitively provided by libc++).
- `internal/gostd/gostd.h` — `fmtArg` overloads for `long`/`unsigned
  long` (LP64 arm64 integral types diverge from Linux LLP assumptions).
- `internal/fileclock/fileclock.h`, `tests_emit.cpp` —
  `system_clock::duration` is microseconds on libc++ (nanoseconds on
  Linux libstdc++): explicit `duration_cast`/`time_point_cast` where the
  port assumed a specific period.

## Runtime divergences found and fixed

### 1. `getdirentries` syscall trap — use `SYS_getdirentries64` (fixed)

libc's `getdirentries(2)` on modern macOS is a link-time trap shim for
the legacy 32-bit-inode ABI; calling it yields garbage/dirent errors
(directory walk → test failures). `syscall(SYS_getdirentries64, ...)`
(syscall 344) is what Go's `golang.org/x/sys/unix.ReadDirent` calls —
switched both fswatch enumeration sites to it (walkdir.cpp, util.cpp).
Legacy `SYS_getdirentries` (196) returns corrupt entries — do not use.

### 2. `FSEventStreamRef` is not a CFType (fixed)

`CFRelease` on an `FSEventStreamRef` SEGVs — FSEventStream is an opaque
C type with its own `FSEventStreamRelease`. Same class of trap as
`watchedFiles` on Windows: opaque Apple API handles are not CFTypes.

### 3. mimalloc / system-zone `free` mismatch (fixed)

The binary's `free` resolves to mimalloc, but libc-internal allocations
(`realpath(path, NULL)` return, `backtrace_symbols` output) come from
the system malloc zone — `free()`ing them cross-zone is an immediate
SIGSEGV. Fixes:
- `osvfs.cpp` — caller-provided `realpath(path, buf)` buffer instead of
  `realpath(path, NULL)` + `free`.
- `ipc/conn_async.cpp` (`debugStack`) — `dladdr`-based symbolization
  instead of `backtrace_symbols` + `free`.

(Audit rule going forward: never `free()` a buffer that libc allocated
internally on macOS.)

### 4. Go GC lifetime → C++ lifetime, `conn_async` test UAF (fixed)

`TestAsyncConnRunWaitsForRequestAfterPeerCloses` crashed with
`system_error: mutex lock failed: EINVAL` → `std::terminate` → SIGABRT.
Two Go-GC-idiom divergences, same class as Windows section 5:

- A `t->Cleanup` lambda touched `handler->release.mu_` — but
  `new blockingHandler()` wrapped in `std::shared_ptr<Handler>` was
  deleted at scope end; the cleanup ran against a destroyed object. In
  Go the GC keeps the handler alive as long as the closure exists. Fix:
  hold `std::make_shared<blockingHandler>()` and let the cleanup capture
  the shared_ptr.
- Detached `std::thread` lambdas captured `t` and evaluated
  `t->Context()` after the test function returned — reading a destroyed
  `T` is UB. Fix: hoist `auto ctx = t->Context();` (named `runCtx` at
  the site that shadows `ctx`) above every `.detach()` site so the
  context (`contextBackground()`) is captured eagerly.

### 5. `context.WithCancel` dropped the parent value chain (fixed — root cause of the last fourslash failure)

`gostd.h`'s `contextWithCancel`/`contextWithCancelCause` never set
`c->parent = parent`. `ctxValue` walks the parent chain for value
lookups, so every context derived via `context.WithCancel` silently lost
parent values — most importantly `core::requestIDKey` set by the LSP
server (`lsp_server.cpp`, `core.WithRequestID(requestCtx, req->ID)`).

Downstream effect: `project::checkerPool::GetChecker` saw `reqid=""` →
request affinity disabled (`tryReacquireForRequest` can't reacquire
"") → a *nested* `GetTypeChecker` acquisition inside the same LSP
request (the outer checker still held via `done`) created a second query
checker → `mergedSymbols` is per-checker, so `getSymbolAtLocation`
returned a different transient merge clone on the second checker →
`findModuleReferences`' `moduleSymbol == searchModuleSymbol` pointer
compare failed → the `import "foo"` specifier never counted as a module
reference → `TestFindAllRefsForModuleGlobal` baseline missed one `[||]`.

Fix (2 lines): link `c->parent = parent` in both `contextWithCancel`
and `contextWithCancelCause` — Go's `cancelCtx` delegates `Value()` to
its parent; values are now visible to all descendants.

Also in `ls/findallreferences.cpp`: `GetTypeCheckerForFileExclusive(nullptr)`
→ `GetTypeChecker(ctx)` at the four sites (lines ~918/1498/1855/2537)
matching Go's `program.GetTypeChecker(ctx)` at findallreferences.go
510/999/1280/1738 — the one-arg overload fabricated a `gostd::Context{}`
that discarded the request ctx, bypassing request affinity even once
parent linking worked. (On the built-in pool both resolve to
`checkers[0]`; on the external `CheckerPool` the ctx carries the
request ID the pool needs for same-request reacquisition.)

### 6. `osvfs` test gate — `UseCaseSensitiveFileNames` assert (fixed)

`os_test.go`'s assertion switch has cases for windows and linux only —
`runtime.GOOS == "darwin"` hits no case (default macOS FS is
case-insensitive, but the test deliberately doesn't assert it). Added
the matching `__APPLE__` hole in `tests_os.cpp`.

## Go-on-darwin behaviors verified matching

| Behavior | Go (GOOS=darwin) | Port |
|---|---|---|
| `fswatch.Default()` | FSEvents | FSEvents watcher (`__APPLE__`) ✓ |
| Directory enumeration | `unix.ReadDirent` → `SYS_getdirentries64` | `syscall(SYS_getdirentries64)` ✓ |
| `os.Args` / `os.Executable` | kernel argv / kernel path | `_NSGetArgc`/`_NSGetArgv` / `_NSGetExecutablePath` + `realpath` ✓ |
| Heap stats | `malloc_zone_statistics` 2-arg | same ✓ |
| `isFileSystemCaseSensitive` probe | stat case-swapped exe path | faithful — not forced ✓ |
| inotify/fanotify | not registered (GOOS-gated) | `__linux__`-gated stubs ✓ |
| Request affinity (nested checker acquire) | same checker via request ID | `WithCancel` parent-link restored ✓ |

## Faithful skips

- None on the final run — all 4560 fourslash tests pass. (Skips present
  only if Node.js is absent: the node-gated unit tests. With `brew
  install node`, everything runs.)

## Remaining divergences / known issues

- **lldb is unreliable on this box** for C++ debugging (hangs on
  symbol-rich binaries); diagnosed via `fprintf` instrumentation +
  `otool -tV`/`atos` instead. Not a product issue.
- **Checker-identity invariant**: merged/transient symbols are
  per-checker clones. Any code path comparing symbol pointers across
  `GetTypeChecker`/`GetTypeCheckerForFileExclusive` acquisitions must be
  on the same request ctx (`WithRequestID` + cancelable), or clones
  diverge. This is a real Go-semantic constraint, now enforced by the
  section-5 fixes.
- The `tscpp check`/emit corpus was already byte-identical on Linux; the
  macOS build produces the same binaries' behavior — no darwin-specific
  emit divergences were found.
