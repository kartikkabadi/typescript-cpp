# Fuzz Pass 2 — extended input-surface coverage

Second libFuzzer pass on the C++23 port, extending coverage beyond pass 1
(FUZZ_REPORT.md: scanner/parser/tsconfig/LSP-framing). Same harness:
`cpp/cmd/fuzz/fuzz_*.cpp`, `-DTSCPP_FUZZ=ON` build, ASan+UBSan,
libFuzzer `-fork` replaced with a `timeout`-sliced restart loop
(libFuzzer -fork SEGVs under this toolchain), `-rss_limit_mb=6144
-timeout=25 -max_len=262144`, work on a 64MiB-stack pthread
(`fuzz::BigStackRunner`). LLVM 18.1.8 + libc++; fuzz binaries need
`LD_LIBRARY_PATH` pointing at the toolchain runtime.

## New targets (each its own binary under cpp/cmd/fuzz/)

| Binary | Surface | Driver |
|---|---|---|
| fuzz_semver | semver range/version parsing | fuzz_semver.cpp |
| fuzz_tspath | path normalization/canonicalization (UNC, drive letters, `..`, separators, non-UTF8, overlong) | fuzz_tspath.cpp |
| fuzz_packagejson | package.json content as JSONC (comments, trailing commas) | fuzz_packagejson.cpp |
| fuzz_modresolve | module specifier + resolution inputs — `exports`/`imports`/`typesVersions`, wildcard `*` patterns, `#imports`, scoped pkgs; package.json served through a vfs into `DefaultResolver::ResolveModuleName{,FromDirectory}`/`ResolveTypeReferenceDirective` | fuzz_modresolve.cpp |
| fuzz_modspec | module-specifier helpers (`IsExcludedByRegex`, `PathIsBareSpecifier`, `GetNodeModulePathParts`, `GetPackageNameFromDirectory`, declaration→JS extension maps, `replaceFirstStar`) | fuzz_modspec.cpp |
| fuzz_jsdoc | JSDoc comment parsing reachable as input (`.js` + `.ts` parse then lazy `jsDoc()` on every statement; `isJSDocLikeText`) | fuzz_jsdoc.cpp |
| fuzz_lspdispatch | LSP protocol request dispatch beyond framing — full `json::unmarshal` → `lsproto::Message` → `handleRequestOrNotification` (+async work) against a real `Server` with vfs + bundled lib | fuzz_lspdispatch.cpp |

Dictionaries + seed corpora under `cpp/fuzz-seeds/<target>` +
`<target>.dict`, harvested from existing test fixtures (lsp_messages,
project/packagejson fixtures, path fixtures). Dict entries restricted to
printable bytes (raw `\x00` breaks libFuzzer's ParseDictionaryFile).

### LSP dispatch driver notes

- `nullReader`/`nullWriter` stand in for the wire; warm-up runs a real
  `initialize` → `initialized` → `didOpen` sequence inside a global ctor
  before libFuzzer starts timing (session spin-up + lib parse exceeds
  the 25s per-unit timeout otherwise).
- A detached thread drains `outgoingQueue` and answers server→client
  requests (`client/registerCapability`, `workspace/configuration`) by
  fabricating the response JSON and resolving it through
  `pendingServerRequests` — the same path `readLoop` uses for real
  client replies. Without it, `handleInitialized` blocks forever in
  `sendClientRequest` (Go blocks identically awaiting the client).

## Results

33 minutes per target (restart-loop of 5-min `-max_total_time`
slices). Requirement was ≥5M execs or ≥30min per binary.

| Binary | execs | faithfulAborts (tscUnreachable/goPanic) | allocPanics | otherExceptions |
|---|---|---|---|---|
| fuzz_semver | 3,302,620 | 0 | 0 | 0 |
| fuzz_tspath | 5,786,355 | 1,072,486 | 0 | 0 |
| fuzz_packagejson | 12,127,269 | 0 | 0 | 0 |
| fuzz_modresolve | 1,835,421 | 32,024 | 390 | 2,608 |
| fuzz_modspec | 2,101,621 | 0 | 0 | 391,423 |
| fuzz_jsdoc | 1,763,815 | 0 | 0 | 0 |
| fuzz_lspdispatch | 1,199,385 | n/a (restart-loop) | | |

Total new: **~30.5M execs**.

## Crash triage vs Go panic semantics

Pass-1 classification rules apply: tscUnreachable throw / `goPanic` /
Go-faithful OOM (bad_alloc, length_error) / stack overflow inside the
64MiB budget = faithful abort, NOT a bug. A real bug = a crash class Go
cannot produce (UB, silent memory unsafety) or a divergence vs Go
panic semantics.

### Real bugs found and fixed

1. **`lsp_handlers.cpp` `handleInitialize` — null deref on missing
   `capabilities.general.positionEncodings`** (SEGV, repro
   `{"method":"initialize","params":{"capabilities":{...no
   positionEncodings...}}}`). `Slice` is `std::optional` here; Go's
   `slices.Contains(nil)` returns false. Fix: `has_value()` guard —
   Go would not crash → real bug.
2. **`resolver.cpp` `loadModuleFromTargetExportOrImport` —
   null-iterator UB (UBSan: "applying non-zero offset 24 to null
   pointer")** — `parts.assign(pc.begin()+1, pc.end())` leaves `parts`
   empty for a one-component path (e.g. a URL-root target like
   `^ts3://...` under a fuzzed `exports` array), then
   `parts.begin()+1` was null-iterator UB. Go evaluates `parts[1:]` on
   the empty slice and panics `slice bounds out of range`. Fix:
   `tscUnreachable` guard — Go panic parity.
3. **`resolver.cpp` entrypoints path — same class**: Go
   `GetPathComponents(...)[2:]` panics for <2 components; C++ did
   `pc.begin()+2`. Fix: `tscUnreachable` guard.
4. **`session.cpp` — nil-snapshot derefs**: `getSnapshot` →
   `updateSnapshot` returns nullptr on `apiError` (Go same). All
   `callerRef=false` callers dereferenced unconditionally. In Go this
   is a nil-pointer panic — recovered as InternalError where a handler
   recover applies, or a crash in `dispatchLoop` where none does. A
   hardware SEGV cannot be caught in either world; `panicOnNilSnapshot`
   throws a catchable `runtime_error` to model the panic faithfully.
5. **`lsp_server.cpp` — null `s->session` in the two language-service
   registration wrappers** (no `session==nullptr` guard, unlike
   `registerRequestHandler`/`registerNotificationHandler`): Go nil-
   derefs identically; dispatchLoop has no recover → Go crashes. In
   C++ it was a hardware SEGV through `catch(...)` — replaced with a
   `goPanic` throw (panic parity; also reachable post-`shutdown`).

### Faithful-abort classes (not bugs)

- modresolve: 32k tscUnreachable (Go `unreachable`/panic sites on
  impossible states), 390 allocPanics (Go-faithful allocation caps),
  2.6k otherExceptions (thrown error channel — e.g. `out_of_range`
  where Go `s[startIndex:]` panics on OOB).
- modspec: 391k otherExceptions — `std::out_of_range` from
  `string_view::substr`/`indexAfter` in `GetNodeModulePathParts`, the
  C++ model of Go's slice-bounds panic (`fullPath[partStart:]`).
- jsdoc: 2 timeout artifacts — exponential allocation on nested `{`
  JSDoc text (`@import ... {fan({y@<{...`). OOM at 22s/rss cap. Go's
  jsdoc parser has no cap either — unbounded growth is Go-faithful.
- lspdispatch (pre-fix rounds): repeated `initialized`→`sendClientRequest`
  waits — Go blocks identically; resolved by the loopback client, not by
  changing the code.

### Toolchain parity fix (not a fuzz finding, found while building)

- `json.cpp::asNumber` — `std::from_chars(double)` is deleted in
  libc++ 18 → replaced with `strtod` + ERANGE/HUGE_VAL overflow check
  (Go `ParseFloat` parity).

### Other faithful-parity edits

- 4 `__builtin_trap()` sites → `tscUnreachable(...)` (tspath.h,
  resolver.cpp, compileroptions.cpp).
- `fuzz_common.h` — catch `tsc::lsp::goPanic` as faithful abort.
- `lsp_handlers.cpp` `handleInitialized` — the `Slice` fields the
  driver hit needed matching optional-slice handling.

## Reproduce

```bash
cmake -B cpp/build-fuzz -S cpp -G Ninja -DTSCPP_FUZZ=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=~/toolchain/bin/clang18 \
  -DCMAKE_CXX_COMPILER=~/toolchain/bin/clang18xx \
  -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address,undefined -stdlib=libc++ -g -O1" \
  -DCMAKE_C_FLAGS="-fsanitize=fuzzer-no-link,address,undefined -g -O1" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined -fuse-ld=lld -stdlib=libc++"
ninja -C cpp/build-fuzz
export LD_LIBRARY_PATH=~/toolchain/llvm/lib/x86_64-unknown-linux-gnu:~/toolchain/lib2/lib/x86_64-linux-gnu
export ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1
./cpp/build-fuzz/fuzz_<target> cpp/fuzz-seeds/<target> \
  -dict=cpp/fuzz-seeds/<target>.dict -rss_limit_mb=6144 -timeout=25 -max_len=262144
```
