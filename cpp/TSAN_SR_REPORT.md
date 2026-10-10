# TSan SEGFAULT — `ls.TestSelectionRangeDepthIsLimited`: diagnosis and fix

## Symptom

`./unittestrunner -run TestSelectionRangeDepthIsLimited` under the canonical
clang-15 TSan build (`cpp/build-tsan-c15`, `-fsanitize=thread`,
`TSCPP_MIMALLOC=OFF`, RelWithDebInfo `-O2 -g`) crashed 100% deterministically:

```
FAIL ls.TestSelectionRangeDepthIsLimited
    [crash backtrace]
    [killed by signal 11]
```

The same test passes in the Release build and under the zig/clang-21 TSan
build (`cpp/build-tsan`), 5/5 runs.

## What the test does

`tests_selectionranges.cpp` builds a source with `nestingDepth = 12000`
parentheses and asserts `getSmartSelectionRange` honours
`maxSelectionRangeDepth = 1000`. The selection-range walk itself is
iterative (a capped ring buffer); the deep structure is created by
`parseSourceFile`, whose recursive-descent keeps ~11–13 instrumented
frames live per nesting level — ≈ 156K concurrently live instrumented
frames for this input.

## Root cause — not a stack overflow, not a race, not a port bug

Clang-15's TSan runtime gives each thread a **fixed-size shadow-stack
buffer** (~768KB ≈ 96K 8-byte slots). `__tsan_func_entry` appends the
return PC on every instrumented call and, in the release runtime, performs
**no bounds check** (only a `DCHECK` in debug builds of the runtime). With
~156K live frames the buffer overflows: `__tsan_func_entry` keeps writing
return PCs past the end of the mapping into whatever VMA is adjacent —
clang's own `MetaMap`/`DenseSlabAlloc` index pages — and the next
`Refill`/`GetSync` dereferences the corrupted freelist, producing a
deterministic near-NULL SEGV *inside the sanitizer runtime* (observed
fault addresses `0x270`/`0x710`/`0xf270`/`0xfb70`, varying with layout).

Proven by hardware watchpoints under `kernel.randomize_va_space=0`:
`__tsan_func_entry` was caught storing a return PC at
`thr->shadow_stack_pos` inside the `mapidx_[23]` index page (~1.2K
entries past the buffer end). Go sidesteps the problem entirely —
goroutine stacks grow dynamically and Go's race instrumentation is a
different runtime — so there is no Go-side depth counter to port; the
limit is purely the clang runtime's fixed capacity.

Falsified along the way:

- **Stack overflow**: crash `rsp` is ~9KB below the 64MB test-stack top;
  the same test uses only ~9MB of stack under the zig build; pthread
  stack sizes 4–128MB all crash identically.
- **A data race**: fully deterministic, single-CPU-reproducible.
- **A port bug**: identical pre/post sibling fixes; test logic is
  iterative and correct.
- **Running on the main stack** (setrlimit + no 64MB pthread): the
  overflow still happens; only the corrupted victim VMA moves
  (crash in `MetaMap::GetSync`/`DDMutexInit`).
- **`no_sanitize` attributes** (`__attribute__((no_sanitize("thread")))`,
  `no_sanitize_thread`, `#pragma clang attribute push … apply_to =
  function`): verified per-object — clang-15 honours them by suppressing
  `__tsan_read*`/`__tsan_write*` but **still emits
  `__tsan_func_entry`/`__tsan_func_exit`**, so shadow-stack pressure is
  unchanged.

## Fix

The recursion cycle lives entirely inside `internal/parser/parser.cpp`.
Compiling that one TU **without TSan instrumentation**
(`-fno-sanitize=thread`) removes every `__tsan_func_entry` push from the
deep recursion, bounding shadow-stack depth regardless of input. The
parse is single-threaded, so no race coverage is lost.

`cpp/CMakeLists.txt` now applies the flag to `parser.cpp` whenever the
configured compile flags contain `fsanitize[:=]…thread` under a Clang
compiler; non-TSan builds are unaffected.

## Verification

| Build | Command | Result |
|---|---|---|
| `cpp/build-tsan-c15` (clang-15 TSan) | `unittestrunner -run TestSelectionRangeDepthIsLimited` | 1/1 PASS |
| `cpp/build-tsan-c15` (clang-15 TSan) | full `unittestrunner` | 1122/1122 pass |
| `cpp/build-tsan` (zig/clang-21 TSan) | `unittestrunner -run …` | PASS (was already) |
| `cpp/build` (Release) | full `unittestrunner` | 1122/1122 pass |

## Reproduction recipe

```sh
sudo sysctl kernel.randomize_va_space=0   # clang-15 TSan FATALs with ASLR on
cmake -S cpp -B cpp/build-tsan-c15 -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTSCPP_MIMALLOC=OFF \
  -DCMAKE_C_COMPILER=clang-15 -DCMAKE_CXX_COMPILER=clang++-15 \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread -fuse-ld=lld"
ninja -C cpp/build-tsan-c15 unittestrunner
TSAN_OPTIONS="halt_on_error=0 report_bugs=1 exitcode=0 \
  report_thread_leaks=0 log_path=/tmp/tsan" \
  ./cpp/build-tsan-c15/unittestrunner -run TestSelectionRangeDepthIsLimited
```
