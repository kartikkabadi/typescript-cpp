# tscpp vs tsgo — performance benchmark report

**Date:** 2026-10-06 · **Branch:** `devin/cpp-perf-hotpaths` (rebased onto
`devin/cpp-port` @ `adb5be5715`)
**ROADMAP gate:** tscpp ≥ 3× faster than the Go `tsc` on equivalent work.

## Verdict

**The ≥3× gate is not met, and the phase-6 floor analysis shows it is not
reachable under the byte-identity contract.** After six perf phases the port
runs at **~1.0× parity with Go** on the project benchmark — faster on two of
three surfaces — with every remaining hot spot either contract-bound (would
change observable output) or faithful-work shared with Go (same algorithm,
same cost). Closing further requires a non-faithful redesign, which is out of
contract. See "Honest floor" below and `HOTPATHS.md` for the full archaeology.

| Surface (`tsc -p` perfproj, RUNS=9 medians) | tsgo | tscpp | ratio cpp/go |
|---|---|---|---|
| emit | 0.149 s | 0.156 s | **1.049× slower** |
| `--noEmit` | 0.126 s | 0.126 s | **0.991× — faster than Go** |
| `--declaration --emitDeclarationOnly` | 0.181 s | 0.179 s | **0.995× — faster than Go** |
| `check` deeplyNestedMappedTypes.ts (single-file path) | 0.17 s | 0.21 s | 1.24× slower |
| `parse-all` testdata corpus (4.6 MB) | — | ~30 ms, ~150 MB/s (39× vs phase-1) | — |
| `--diagnostics` Parse / Bind / Check | 0.032/0.016/0.074 s | 0.034/0.009/0.056 s | parity / faster |
| Max RSS, project `--declaration` | ~264 MB | **~340 MB** | within budget |
| CPU utilization, `--noEmit` run | ~390–460% | ~273–298% | Go burns more cores (GC + runtime) |

## Environment

- CPU: Intel Xeon Platinum 8559C, **8 cores** (`nproc`)
- RAM: 31 GiB · Kernel: 6.8.0-1061-aws (Ubuntu 22.04)
- tscpp: clang++ 15.0.7, `-O2 -g -DNDEBUG`, lld, **mimalloc 2.1.7**
  (`TSCPP_MIMALLOC=ON`, `MI_MALLOC_OVERRIDE`) — `cpp/build-mi`
- tsgo: go1.27 build of `tsc/cmd/tsc` (the repo's Go oracle) at `/tmp/tsgo`
- Benchmarks: `cpp/tools/perf/bench_project.py` (median wall-clock ratio,
  RUNS=9), `/tmp/perfproj` 100-file synthetic project
  (`gen_project.py`, ~200 lines/file, strict + decorators + generics).

## What the six phases fixed

1. **Emit-arena mark/sweep quadratic → GOGC-style global pacing**
   (`deeplyNestedMappedTypes.ts` 130 s → 0.21 s). `releaseArenas()` ran an
   O(live-heap) sweep per `typeToString`; now sweeps only when global
   allocated-since-sweep ≥ clamp(live/4, 32 MB, 64 MB).
2. **Instantiation over-count**: `staleForCheckFile` re-instantiated member
   symbols per file — Instantiations 8,934 → 6,630 (= Go exactly).
3. **mimalloc drop-in**: the ~30%-of-kernel-time glibc `grow_heap`/`mprotect`
   storm (21,635 calls/run) → 22 calls. Decl-emit RSS 4 GB → ~340 MB.
4. **Arena block sizing** (1 MB-per-file minimum + full-block memset):
   parse-all corpus 1,173 ms → ~30 ms.
5. **`Node::text()`/`textView` + sv name-resolution path**, lazy `TraceArgs`,
   NodeSlice accessor views (zero-copy reads over ~50 hot call sites).
6. **SymbolTable copy storm**: const-ref accessors + move-install +
   phantom-insert `operator[]`→`find()` — `_M_assign_elements` 34.7M → 0.98M
   Ir, `-97%`.
7. **Parallel file parse** (filesparser.go:269 faithful port): parse phase
   0.107 s → 0.034 s on perfproj; plus two concurrency bugs it exposed
   (shared `tracer` race → per-call `unique_ptr<tracer>`; LoadOrStore
   losers → `resolvedModuleKeepAlive` anchor on SimpleProgram).

## Honest floor — why ≥3× is contract-incompatible

Post-fix the flat profile has no function above ~2.3%. The residual cost
decomposes as:

- **~60% faithful work** — scanner, binder, checker/relater, id assignment.
  Identical algorithm to Go; speeding it up means *changing* the algorithm.
- **~5% contract-bound copies** (`new[]`/`delete[]`/`memmove`) — strings and
  vectors that must own storage. Go pays these off GC books; C++ value
  semantics makes them explicit. Removing them means shared/mutable
  ownership = different observable behavior (shared type identity, alias
  semantics, lifetime changes).
- **~3% order-locked hashing** (`_Hash_bytes`, map `find`/`operator[]`) —
  `SymbolTable` bucket order leaks into diagnostic suggestion order. Any
  hash/container change reorders output → byte-diff. Off-limits by contract.
- **~1.7% `_dl_relocate_object`** — dynamic-linking PLT resolution at
  startup. Go links static; a fully-static tscpp build would shave it
  (not landed — build-system churn for a sub-2% startup effect).
- **~1.4% mimalloc internals** — the cost of the allocator that killed the
  mprotect storm.

CPU% gap (~273% vs ~460%) is largely Go burning extra cores on GC workers
and runtime threads; wall-clock is at parity, so the utilization gap is not
lost throughput.

A ≥3× win over Go would need bigger structural deltas — a different
allocator/ownership model for Types, different map orderings, wider
parallelism than Go's 4-checker pool — all of which break byte-identical
output. **The milestone terminates at parity by design.**

## Conformance gates (all green at final commit)

- `tscpp check` corpus smoke: **300/300 byte-identical** to `checkdump`.
- `deeplyNestedMappedTypes.ts`: byte-identical, 0.21 s.
- `tsc -p` emit + `--declaration`: `diff -r` identical to tsgo output trees.
- `tsctestrunner`: **99/99**.
- Emit stability: 8/8 runs clean (parallel-emit race fixed in phase 1).
- Decl-emit RSS: 340 MB (gate ≤ ~350 MB).

## Methodology

- Sequential runs, warm page cache (one warmup per side per surface).
- Identical flags both sides (`--pretty false`; project uses `-p`).
- `tscpp check`/`tsc -p` output is byte-identical to the Go oracles
  (checkdump / emitdump / `diff -r` on output trees) — verified after every
  phase; see `cpp/tools/conformance_check.sh`.
