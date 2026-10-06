# tscpp per-CPU hot spots — findings and fixes

Branch `devin/cpp-perf-hotpaths`, base `a4120cad1d` (devin/cpp-port HEAD).
Workload: `/tmp/perfproj` (100-file synthetic project, `cpp/tools/perf/gen_project.py`),
`cpp/tools/perf/bench_project.py` (median wall-clock ratio vs Go `tsgo`, RUNS=7),
plus the pathological single file `tsc/testdata/tests/cases/compiler/deeplyNestedMappedTypes.ts`.

## TL;DR

| workload                              | before        | after         |
|---------------------------------------|---------------|---------------|
| `tscpp check deeplyNestedMappedTypes` | ~130 s        | **0.40–0.43 s** (byte-identical to checkdump oracle) |
| `tsc -p` emit (perfproj, med ratio)   | ~1.95× slower | ~1.6–1.8× slower (noisy) |
| `tsc -p` --noEmit                     | ~2.33× slower | **~0.9–1.0× (parity)** |
| `tsc -p` --declaration                | ~2.37× slower | ~1.5–2.0× slower (noisy) |

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

## Current top hot spots (`tsc -p --noEmit`, flat perf, ~1100 samples)

The run is now short enough (~0.4–0.5 s wall) that fixed startup cost and
machine noise dominate; ordering below is the profile, attribution is manual.

1. **~30% kernel mm-lock contention** — `osq_lock` 22.6% +
   `_raw_spin_lock`/`asm_exc_page_fault`/`rmqueue_bulk`/… under
   `do_mprotect_pkey` + page-fault paths. `strace -c -f` shows **21,244
   `mprotect` calls** in one 0.4 s run. These are glibc per-thread arena heaps
   growing in page-size increments (`grow_heap` → `mprotect(PROT_NONE→RW)`,
   no tunable controls the step — `glibc.malloc.top_pad` only affects the main
   arena; measured: barely moved). Every small `new` on a worker thread drizzles
   a few more pages. **Go attribution**: Go's allocator mmap's large spans once
   and sub-allocates per-P with no locks — effectively zero mprotect per run.
   **Fix direction (not done)**: route hot Node/Type/vector allocations through
   `tsc::Arena` (large-block bump) instead of `new`, or link a span allocator
   (mimalloc/tcmalloc). That's a structural porting decision, not a one-liner.
2. **~9% glibc malloc/free userspace** (`malloc`, `_int_free`, `_int_malloc`,
   `cfree`, memset) — same root cause as #1: per-object `new` for nodes, types,
   map nodes, strings. Go pays this too but per-P and lock-free.
3. **~4.4% hashing + key ops** — `std::_Hash_bytes` 1.45% +
   `memcmp`/`memmove` ~3%: `std::unordered_map` (chained buckets) vs Go 1.24+
   Swiss-table maps. Sites: `SymbolTable` (`std::string` keys — copies on
   insert), `CacheKey` maps, LinkStore. **Fix direction**: hash table with
   open addressing + `string_view` keys, or intern symbols once at bind.
4. **`getSourceFileOfNode` 0.95%, `Binder::bind` 0.89%, `Scanner::scan`
   0.71%, `Node::eagerJSDoc` 0.60%, `getIdentifierToken` 0.72%** — ordinary
   work, roughly matches Go's binder/parser/scanner CPU; no port-level
   pathology evident at this granularity.
5. **`_dl_relocate_object` 1.2%** — process startup; fixed ~10 ms cost that
   reads large only because runs are now sub-second.

## Known bugs found while profiling (not fixed here)

- **Parallel-emit data race (pre-existing)**: `tscpp tsc -p` and
  `tsctestrunner` intermittently SIGSEGV (~10–20% per emit invocation, same
  before and after this change). Signature: a worker inside
  `ImpliedModuleTransformer::visitSourceFile` calls
  `NewESModuleTransformer`/`NewCommonJSModuleTransformer`
  (`esmodule.cpp:64` / `commonjsmodule.cpp:179`) which copies
  `opts->GetEmitModuleFormatOfFile` (`std::function`); the source's invoker
  bytes get stomped mid-copy (observed functor target = truncated node
  pointer). `TransformOptions`/transformer `opts` lifetime is shared across
  emit workers — `emitContext->newNodeVisitor` registers visitors on the
  shared `EmitContext`, so a visitor bound to one worker's stack `opts` can
  run (or be copied) on another. Needs a dedicated fix: per-worker
  `TransformOptions` copies with deep-copied std::functions, or a mutex on
  registration.
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
  touches hundreds of sites.
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
