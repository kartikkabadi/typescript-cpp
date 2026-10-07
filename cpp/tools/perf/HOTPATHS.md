# tscpp per-CPU hot spots — findings and fixes

Branch `devin/cpp-perf-hotpaths`, base `a4120cad1d` (devin/cpp-port HEAD).
Workload: `/tmp/perfproj` (100-file synthetic project, `cpp/tools/perf/gen_project.py`),
`cpp/tools/perf/bench_project.py` (median wall-clock ratio vs Go `tsgo`, RUNS=7),
plus the pathological single file `tsc/testdata/tests/cases/compiler/deeplyNestedMappedTypes.ts`.

## TL;DR

| workload                              | before        | latest        |
|---------------------------------------|---------------|---------------|
| `tscpp check deeplyNestedMappedTypes` | ~130 s        | **0.40–0.43 s** (byte-identical to checkdump oracle) |
| `tsc -p` emit (perfproj, med ratio)   | ~1.95× slower | **~1.16–1.42× slower** (very noisy VM; latest RUNS=9 medians 1.19×) |
| `tsc -p` --noEmit                     | ~2.33× slower | **~1.17–1.44×** (latest 1.25×) |
| `tsc -p` --declaration                | ~2.37× slower | **~0.995–1.25×** (latest 1.11×; hit parity on one RUNS=7 set) |
| `tsc -p` parse phase                  | 0.107 s vs Go 0.035 s | **~0.063–0.066 s** |
| `mprotect` syscalls per `tsc -p` run  | 21,635        | **22** (mimalloc) |
| decl-emit peak RSS                    | ~460 MB gate  | **323 MB** |
| `--diagnostics` Instantiations        | 8,934 (+35% vs Go) | **6,630 = Go exactly** |
| `parse-all` testdata corpus (4.6MB)   | 1,173 ms, 3.9 MB/s | **~30 ms, ~150 MB/s (39×)** |

## Fixed: amortize the tracked-arena mark/sweep (GOGC-style pacing)

**Symptom.** `deeplyNestedMappedTypes.ts` took ~130 s vs Go's 0.24 s (~545×).
Flat `perf record` on it: `Arena::markPointer` 76.3%, `EmitContext::releaseArenas`
10.7%, `beginMark` sort helpers ~9%, `traceArenaNode` ~1% — ~98% in GC.

**Mechanism.** `Checker::typeToStringEx` → `getNodeBuilder()` registers a
`releaseNodes` lambda calling `EmitContext()->releaseArenas()` after EVERY
TypeToString (`checker_nodebuilder.cpp` ~8060). `releaseArenas` ran a full
O(live-heap) mark+sweep per call. Deep `Id<Id<...>>` elaboration → thousands of
NodeBuilder instantiations → thousands of whole-heap sweeps = quadratic.
Go's `ReleaseArenas` is O(1): it nils the arena slice headers and lets the GC
reclaim garbage on its own pacing.

**Fix.** `Arena::globalShouldSweep(floor, cap)` + two process-wide atomics
(`trackedBytesAllocdGlobal_`, `trackedBytesLiveGlobal_`) updated on alloc /
sweep / clear. `releaseArenas()` now sweeps only when
`allocd >= clamp(live/4, 32MB, 64MB)` — a global GOGC-style trigger: garbage is
bounded by ~25% of the surviving tracked heap (floor 32 MB for small heaps,
cap 64 MB so garbage never adds more than ~64 MB to RSS no matter how big the
live set is). Each arena is still swept only by its own owner's release call —
the GC-assist model — so no cross-thread marking.

Also fixed ownership bugs the pacing exposed:

- `~Arena()` now calls `clear()` (tracked objects used to leak on ctx death).
- Move ctor / move-assign now carry `tracked_`, `trackedAllocs_`, and the
  byte counters; moved-from arenas zero theirs so destruction stays exact.
- `clear()` and `sweep()` withdraw/fold the arena's contribution from the
  global counters.

**Verification.**
- `tscpp check deeplyNestedMappedTypes.ts`: 130 s → 0.43 s, output
  byte-identical to the live `checkdump` oracle.
- 300-file check smoke (`head -300` of tests/cases corpus via
  `cpp/tools/conformance_check.sh`, P=12): **300/300 PASS**.
- Decl-emit RSS: unchanged within run-to-run noise (457 MB–1.5 GB across
  identical runs — dominated by parallel-emit worker scheduling, not the gate;
  the gate can add at most ~64 MB global).
- Debug knob: `TSCPP_GC_DEBUG=1` prints `[gc] allocd=… live=… threshold=…`
  per release call.

## Fixed: arena blocks — 1MB-per-file minimum + full-block memset

**Symptom.** Project `--diagnostics` Parse time 0.107 s vs Go 0.035 s (~3×).
`massif` heap-tree on `parse-all`: **97.9% of the parse heap was
`Arena::raw`** — 209 MB for 1.5 MB of source (arena ≈ 1 MB × file count;
testdata corpus: 4.6 MB source → 6.87 GB arena). `callgrind` then showed
`__memset_avx2` at **80.8% of all instructions**.

**Mechanism (three compounding bugs in `Arena::raw`).**
1. `NodeFactory`'s arena used the 1 MB default block — every SourceFile,
   even a 2 KB test, grabbed ≥1 MB up front. ≥128 KB blocks come from
   `mmap`, so each file paid an mmap + ~256 eager page faults.
2. `std::make_unique<char[]>(n)` value-initializes — a memset over the
   whole block, so we wrote every page twice (once for zeroes, once for
   real data). This alone was ~80% of parse instructions.
3. Fixed block size meant a file needing 1.2 MB allocated a second 1 MB
   block regardless.

**Fix.** `raw()` now allocates with `make_unique_for_overwrite` (no
memset) and grows `blockSize_` geometrically up to `maxBlockSize_`
(= max(initial, 1 MB)); `Parser::initializeState` seeds the node arena's
first block at `clamp(sourceSize*8, 64KB, 1MB)` (AST bytes ≈ 8× source),
so small files stay small while big files still reach 1 MB blocks in a
few steps. Go analogue: its GC arena hands out 8 KB spans that grow.

**Verification.**
- testdata `parse-all` (6,840 files, 4.6 MB): **1,173 ms → ~30 ms**,
  arena 6,865 MB → 480 MB.
- perfproj `parse-all` (200 files, 1.5 MB): ~33 ms → ~15 ms,
  arena 200 MB → 72 MB.
- `--diagnostics` Parse time: 0.107 s → ~0.066 s (Go 0.035 s; remaining
  gap is scanner/string CPU, not allocation).
- Emit + decl-emit outputs `diff -r` byte-identical to Go; 300/300
  check smoke; tsctestrunner 99/99 ×3.

## Fixed: two parallel-emit crashes (was ~10–20% SIGSEGV per emit)

`tsctestrunner` intermittently died and `tscpp tsc -p … --outDir` exited
2 silently (SIGSEGV caught by the crash handler → `exit_group(2)`).
Two independent bugs, both in `cpp/internal/compiler/emitter.cpp`:

1. **Stack-use-after-return.** `getScriptTransformers` built a
   stack-local `TransformOptions opts` and passed `&opts` to every
   transformer. `ImpliedModuleTransformer::visitSourceFile` lazily calls
   `NewESModuleTransformer(opts)` inside the visit — i.e. *after*
   `getScriptTransformers` returned — reading `opts->GetEmitModuleFormatOfFile`
   from dead stack. Go escapes `opts` to the heap automatically.
   Fix: `auto* opts = new TransformOptions; … emitContext->addCleanup(
   [opts]{ delete opts; });`
2. **Non-exclusive checker checkout.** `newEmitHost` used
   `GetTypeCheckerForFile` (comment: "only safe when ... read-only"),
   but emit workers re-enter the checker via
   `MarkLinkedReferencesRecursively` → `checkExpressionCached` → flow
   analysis, which mutates `getFlowState` free-lists / `cachedTypes`
   concurrently → SIGSEGV inside `checker_flow.cpp`.
   Fix: `GetTypeCheckerForFileExclusive` (mutex checkout).

After: `tscpp tsc -p … --outDir` and `--declaration` exit 0 across 8/8
runs with `diff -r` identical to `tsgo` output; tsctestrunner 99/99 ×3
consecutive.

## Fixed: mimalloc drop-in allocator (replaces the mprotect storm)

**Symptom.** `strace -c -f` counted **21,635 `mprotect` calls** in one 0.4 s
`tsc -p --noEmit` run — glibc per-thread arena heaps growing page-by-page
(`grow_heap`); ~30% of perf samples were kernel mm-lock/page-fault paths.

**Fix.** Vendored mimalloc 2.1.7 (`cpp/third_party/mimalloc/`, src+include+
LICENSE), built as `tsc_mimalloc` static lib from `src/static.c` (the unity TU
that already contains `alloc-override.c`). `MI_MALLOC_OVERRIDE` interposes the
C malloc family inside the binary; `cmd/malloc_override.cpp` adds
`<mimalloc-new-delete.h>` for global `operator new`/`delete`. `project()` now
declares `C` so `static.c` compiles as C11. `TSCPP_MIMALLOC=ON` (default)
prepends the lib before `libtsc.a` on `tscpp`/`tsctestrunner` link lines.
Works unchanged under the fork-per-test runners and clang-15 RelWithDebInfo.

**Verified.** mprotect 21,635 → **22**; emit ~1.16–1.42×, noEmit ~1.18–1.44×,
decl ~1.16–1.25× (VM noise ±20%); decl-emit RSS **323 MB** (glibc: 321 MB) —
460 MB gate holds; 300/300 check smoke byte-identical; tsctestrunner 99/99;
emit+decl output `diff -r` identical to `tsgo`. `mallinfo2()` in
`execute/tsc/emit.cpp` still feeds `Alloc` (mimalloc populates it); the
`Mallocs` field was never populated and stays 0.

Consequence: the custom span-allocator work from the previous "fix direction"
is **not needed** — the drop-in captured the whole mprotect/pooling win.

## Fixed: `Node::text()` copies + literal compares in hot diagnostic paths

Post-mimalloc callgrind (`tsc -p --noEmit`, 2.04G instr): `std::operator==`
(string vs `char*`) 2.54% + `__strlen` 1.19%, dominated by
`Checker::getCannotFindNameDiagnosticForName` (1.53M calls — `node->text()`
by-value copy plus ~30 `== "literal"` compares each) and
`checkTypeNameIsReserved` (127K calls, same pattern). Go does the same work
but `node.Text()` is a string view and literal compares are size-gated.

**Fix.** New `Node::textView(std::string& scratch)` (ast.h/ast.cpp): returns a
`string_view` into the node's stored `Text` member for all name-bearing kinds
(identifiers, literals, template parts, `MetaProperty` recursion); composed
names (`JsxNamespacedName`, JSDoc text) fall back to `text()` into scratch —
identical content, no copy on the hot path. Both functions now compare
`text` against `"...sv` literals (size-check + memcmp, no per-call `strlen`).

**Scope note.** `Node::text()` has ~400 call sites; only the two hottest
switched (per "top-3 hottest" guidance). The accessor is available for
whichever sites profile hot next.

## Skipped: `unordered_map` hashing (~4.4%)

`_Hash_bytes` 3.28% + node alloc/lookup ~2% post-mimalloc — but `SymbolTable`
iteration order leaks into diagnostic suggestion ordering. Any hash/container
swap changes bucket order and breaks byte-identity. Left alone deliberately;
Go attribution noted for the record (Swiss-table + memhash vs chained
`unordered_map` + Murmur).

## Current top hot spots (`tsc -p --noEmit`, post-mimalloc callgrind, 2.04G instr)

Flat attribution after the allocator swap (pre-mimalloc kernel-side ~30% +
glibc malloc ~9% are gone; mimalloc's own cost shows up as `operator new[]`
3.50% + `free` 2.92%):

1. **`operator new[]` 3.50% + `memcpy_avx` 3.29% + `free` 2.92%** — vector and
   `std::string` growth/copies: `Parser::rewind` state saves, `newIdentifier`,
   `newSymbol`, `declareSymbolEx` name copies. Go pays equivalent work but
   slice-header strings and per-P bump allocation make each op cheaper.
2. **`std::_Hash_bytes` 3.28% + `_Hashtable` node alloc/find/`operator[]`
   ~3% + `memcmp` 2.15%** — `SymbolTable`/`CacheKey` churn. Skipped: bucket
   order leaks into diagnostic ordering (see "Skipped" above).
3. **`Scanner::scan` 2.83% + `scanIdentifier` 2.38% +
   `ScannerState` copies 1.34%** — faithful port of Go's scanner; the
   `ScannerState` copy carries `std::string tokenValue` +
   `vector<CommentDirective>` (SSO keeps most small).
4. **`std::operator==`(string,char*) 2.54% + `__strlen` 1.19%** — was
   dominated by `getCannotFindNameDiagnosticForName` (1.53M calls) +
   `checkTypeNameIsReserved`; both now use `textView` + `"sv` literals.
5. **`Checker::compareNodes` 2.21% + `getSourceFileOfNode` 1.90%** —
   `compareSymbolsWorker` sorts compare nodes across files; mirrors Go's
   parent-walk (`ast/utilities.go:861`) exactly — faithful, no port gap.

## Phase 3: vector-by-value accessors → NodeSlice views; sv name-resolution path

Post-mimalloc profile showed `new[]`+`memcpy`+`free` ~9.7% — Go returns slice
headers where we returned `std::vector<Node*>` by value. Converted the hottest
accessor family (`Node::{modifierNodes,arguments,typeArguments,typeParameters,
members,statements,elements,properties,comments,parameters}` +
`getElementsOfBindingOrAssignmentPattern`) to return **`NodeSlice`**: a
`std::span<Node* const>` wrapper that also implicitly converts to
`std::vector<Node*>` at legacy mutation/`const vector&` boundaries — read
sites get zero-copy views, write sites materialize exactly the same copy as
before. ~50 file-local `const std::vector<T>` helper templates were widened to
generic `R&&` ranges (~25 files) so views reach leaf code. Result: accessor
copies eliminated from the profile (`typeArguments` 0.37%→0.24% flat, no
vector-copy inside); `new[]` 3.71%→3.23%.

Also converted the name-resolution path to `std::string_view` end to end:
`resolveName`/`NameResolver::resolve`/`lookupOrDefault`/`getSymbol`/
`getSuggestionForSymbolNameLookup` now take `std::string_view`, with
`getSymbolFromTableView` (symbol.h) doing heterogeneous bucket-walk finds —
libstdc++ hashes `string_view` via the same `_Hash_bytes` digest as `string`
and buckets as `h % bucket_count()`, verified equal to `find()` on 200K keys.
Previously every `resolveName(loc, x->text(), …)` materialized the temp twice
(`text()` copy + `std::string(name)` at the resolve boundary). Top compare-only
`text()` callers converted to `textView`: `checkContextualIdentifier` (90.8K
calls), `isConstTypeReference` (88.1K), `needCollisionCheckForIdentifier`
(56K), `resolveEntityName` (44K), `isThisInTypeQuery`/`isThisIdentifier`
(24.5K+4.7K), `getResolvedSymbol` (12.8K), `isPushOrUnshiftIdentifier`,
`checkPrivateIdentifier`, grammar checks, `checkParameter`.

Also kept from earlier phase-3 work: lazy `TraceScope` args — `tracing.h`
gains a `MakeArgs` factory overload so `TraceArgs{...}` only materializes
when a tracer is attached (call sites in checker_walk/declchecks2/emitter/
fileloader/program wrap args in `[&]{ return TraceArgs{...}; }`). Previously
the eager `TraceArgs` (an `unordered_map<string,any>` with `new[]` allocs)
was built on every traced call including the null-tracer case — the top
`operator new[]` caller at 299,849 allocations.

Skipped: `Node::text()` itself (1,437 call sites — most store or pass the
string onward; mass conversion is out of scope), `getDeclarationName`
(returns `std::string` by contract), scanner `tokenValue` string_view
(attempted — heap-corruption SIGSEGV from dangling views into moved
ScannerState; reverted permanently).

Gates re-verified: deep file byte-identical, 300/300 check smoke, emit + decl
`diff -r` identical to tsgo, decl RSS 326 MB, tsctestrunner 99/99.

## Known bugs found while profiling (not fixed here)

- **Instantiation over-count: ROOT-CAUSED AND FIXED.** `--diagnostics`
  showed Instantiations 8,934 vs Go 6,630 (+35%) and Types +5%.
  Instrumenting both binaries with per-flag histograms + per-caller
  attribution (Go `runtime.Callers`, C++ scoped `thread_local` site tags)
  pinned it to `getTypeOfInstantiatedSymbol`/`getWriteTypeOfInstantiatedSymbol`
  (908 → 2960): the file-scoped `staleForCheckFile` invalidation forced
  every instantiated member symbol to re-instantiate once per file.
  Go caches `links.resolvedType`/`links.writeType` for the checker's
  whole lifetime (checker.go:16849-16861) — the entries hold a pure
  instantiated type with no diagnostics to re-fire. Removed the file
  staleness check in those two functions only (the other ~66
  `staleForCheckFile` sites guard real diagnostic-attribution paths and
  stay). Instantiations now **6,630 = Go exactly**; Types 59,779 →
  58,819.
- **`getNodeBuilderEx` tracked arenas are never released**
  (`checker_printer.cpp` `TypeToTypeNode`/`TypeToTypeNodeEx`/
  `TypePredicateToTypePredicateNode` paths create
  `new EmitContext(trackFactoryArena=true)` with no release callback — Go's
  equivalent keeps the arena for the emit ctx; ours leaks the whole arena and
  all its nodes until process exit. This is the residual decl-emit RSS driver
  (and makes `live` in the pacing model reflect retained-by-design bytes, not
  garbage). Fix: hand these contexts the same release lambda (or reset()) the
  typeToString builder gets — needs care because callers cache
  `serializedTypes`/`idToSymbol` across the release boundary.
- **`Node::members()/statements()/…` return `std::vector<Node*>` by value**
  (ast.h ~185, ~455 call sites): a heap copy per access. Go returns the slice
  header (O(1) view). Doesn't dominate this workload (<1%) but is a broad
  death-by-cuts cost; converting to `span`/iterator pairs is mechanical but
  touches hundreds of sites. `text()` variant: `Node::textView()` added and
  applied to the two hottest diagnostic paths.
- **`tscpp tsc -p … --outDir` exits 2** on some invocations (CLI arg quirk,
  pre-existing, unrelated to emit correctness).

## Reproduction

```bash
# build (RelWithDebInfo, clang-15, lld)
CC=clang-15 CXX=clang++-15 cmake -B cpp/build -S cpp -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo && ninja -C cpp/build tscpp tsctestrunner

# pathological file
time cpp/build/tscpp check tsc/testdata/tests/cases/compiler/deeplyNestedMappedTypes.ts
TSCPP_GC_DEBUG=1 cpp/build/tscpp check … 2>&1 | tail   # watch the gate

# project bench
TSCPP=$PWD/cpp/build/tscpp RUNS=7 python3 cpp/tools/perf/bench_project.py

# profiles
perf record -F 997 -g --call-graph fp -o /tmp/p.data -- \
  taskset -c 0-7 cpp/build/tscpp tsc -p /tmp/perfproj/tsconfig.json --noEmit --pretty false
perf report -i /tmp/p.data --stdio --no-children
strace -c -f cpp/build/tscpp tsc -p /tmp/perfproj/tsconfig.json --noEmit --pretty false

# gates
head -300 ~/conformance/corpus.txt | grep tests/cases > /tmp/smoke.txt
bash cpp/tools/conformance_check.sh /tmp/smoke.txt 12
./cpp/build/tsctestrunner            # 99 scenarios (flaky SIGSEGV = race above)
```
