# Fuzzing pass — C++23 TypeScript port input surfaces

Branch: `devin/cpp-fuzz` (base `devin/cpp-port`). Date: 2026-10-08.

Harness: libFuzzer + AddressSanitizer + UndefinedBehaviorSanitizer, `-g -O1`, clang-15.
Build: `cpp/build-fuzz` via `-DTSCPP_FUZZ=ON` (defines `TSC_FUZZ_UNREACHABLE_THROW`,
which turns `tscUnreachable` — the port's Go-panic mapping — into a catchable
`tsc::tscUnreachableThrown` so intended aborts are counted in-process instead of
SIGABRTing the target; also force-disables mimalloc which would blind ASan).
Runners: 10-min `timeout` slices in a restart loop (libFuzzer `-fork` mode SEGVs at
exec 0 on this build), `ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1`,
`-rss_limit_mb=6144 -timeout=25 -max_len=262144`.
(`detect_leaks=0`: the parser has by-design GC-parity leaks — backtrack diagnostics
are not all released per parse; ~170KB/exec worst case. Restart loop bounds RSS.)

## Targets and results

| Target | Surface | Execs | Crashes (artifacts) | Verdict |
|---|---|---|---|---|
| `fuzz_scanner` | raw bytes → Scanner tokenize (per `tscpp` scan path) | ~24.9M | 0 | clean |
| `fuzz_parser` | raw bytes → `parseSourceFile` on a persistent 64MiB-stack worker (matches the runners' thread stacks — `cmd/tscpp/main.cpp`) | ~2.4M | 0 | clean |
| `fuzz_tsconfig` | bytes → `NewTsconfigSourceFileFromFilePath` + `ParseJsonSourceFileConfigFileContent` over a bounded fake FS | ~1.2M | 61 | 1 real bug fixed (35 artifacts), rest faithful/driver-artifact |
| `fuzz_lspframing` | bytes → `jsonrpc::Reader::Read` Content-Length/JSON-RPC framing | ~9.0M | 698 | all faithful (giant `Content-Length` OOM) |

Corpora: seeded from `tsc/testdata/tests/cases/**` (~400 files for
scanner/parser), hand-written tsconfig/JSON seeds, and valid Content-Length–framed
LSP messages; corpora grew under fuzzing to /tmp/corpus/{scanner 190MB,
parser 154MB, tsconfig 49MB, lsp 16MB}.

## Real bug found and fixed

### tsconfig: null `node` dereference in `CreateDiagnosticForNodeInSourceFile`

35 artifacts, one signature. UBSan: `errors.cpp: member call on null pointer of
type 'tsc::Node'` from `tsconfigparsing.cpp` (~line 1795) — `ForEachTsConfigPropArray`
returns nullptr for `nodeValue`, then `CreateDiagnosticForNodeInSourceFile`
derefs it unconditionally.

Minimized repro: `""]"files"[` (11 bytes).

Go spec: `tsc/internal/tsoptions/errors.go:92` derefs `node.Loc.Pos()`
unconditionally — Go panics (nil pointer). In the C++ port the nil deref was an
uninstrumented SIGSEGV that could not be caught/countable by the harness.

Fix (`cpp/internal/tsoptions/errors.cpp`): nil-check `sourceFile`/`node` and route
to `tscUnreachable(...)` — the port's own Go-panic mechanism. Under
`TSC_FUZZ_UNREACHABLE_THROW` this is a catchable intended abort; in the normal
build it aborts with the unreachable diagnostic, matching Go's panic semantics
(exactly one panic either way). All 35 artifacts re-verified: each now counts as
`faithfulAborts` instead of a SEGV.

Regression: cannot be a `REGISTER_UNIT_TEST` — `unittestrunner` forks per test and
`tscUnreachable` is a death that can't be asserted in-process; repro lives here
instead (per the task's fallback). `./build-fuzz/fuzz_tsconfig repro -runs=1` →
`faithfulAborts=2`.

## Faithful-Go-behavior verdicts (not bugs)

- **LSP framing OOM (698 artifacts, all same class).** A crafted
  `Content-Length` header makes `jsonrpc::Reader::Read` do
  `std::string data(contentLength,'\0')` (jsonrpc.cpp:~300). Byte-identical to
  Go `baseproto.go:70` `data := make([]byte, contentLength)` — Go would panic
  identically (out of memory). No amplification.
- **tsconfig OOM slices.** `extends`-chain recursion (`parseConfig`/`getExtendedConfig`,
  `tsconfigparsing.go:1183` `resolutionStack = append(...)`) — Go has no depth cap
  either; early instances were also amplified by the fake FS claiming every path
  exists (fixed in the driver, see below). Remaining OOMs are Go-faithful.
- **Nondeterministic boundary SEGV** on `oom-75f14be…`: deep `extends` recursion
  landing within the 64MiB thread-stack budget near the edge — same as Go's
  goroutine stack overflow at its own limit. Faithful.
- **Parser deep nesting:** 100k nested parens exhausts the documented 64MiB
  thread stack — Go relies on goroutine growth (~1GB); the port documents 64MiB
  threads. Expected/faithful within the documented budget.

## Driver artifacts fixed (not product bugs)

- `fuzz_tsconfig` `FuzzFS` initially reported every path existing → infinite fake
  `extends`/`include` recursion. Now `plausibleExistingPath` bounds path length/depth,
  `GetAccessibleEntries` returns no fake directories. Two driver-only fixes.

## Surfaces not covered

- Type checker / binder on fuzzed-but-valid programs (parser fuzz covers grammar
  but checking semantic combinations needs a different harness).
- `tscwatch`/incremental builds, `jsdoc` arenas on doc-heavy input only partially
  exercised via parser fuzz.
- LSP *dispatch* after framing (post-decode request handling) — framing target
  only covers the decode path.
- Source-map/emitter outputs (emit is invoked in the runners but not fuzzed as
  an input surface).

## How to reproduce

```
cmake -S cpp -B cpp/build-fuzz -GNinja -DTSCPP_FUZZ=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer,address,undefined -g -O1" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=fuzzer,address,undefined"
ninja -C cpp/build-fuzz
ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1 \
  ./cpp/build-fuzz/fuzz_<target> <corpus> -rss_limit_mb=6144 -timeout=25
```
