# tscpp vs tsgo — performance benchmark report

**Date:** 2026-10-06 · **Branch:** `devin/cpp-perf` @ `2408468a20`
**ROADMAP gate:** tscpp ≥ 3× faster than the Go `tsc` on equivalent work.

## Verdict

**The ≥3× gate is NOT met on any surface.** tscpp is currently **2.2×–10.5×
slower** than tsgo everywhere measured. The gap is structural, not
per-function slowness: per-CPU work is comparable (±15%) but the port is
fully serial while Go's checker pool parallelizes, and declaration emit has
a real memory pathology (~4 GB RSS on a 1 MB project).

| Surface | tsgo | tscpp | Ratio (cpp/go) | ≥3× gate |
|---|---|---|---|---|
| Per-file check, median (500 files) | 179 ms | 388 ms | 2.16× slower | FAIL |
| Whole-corpus sequential (500 files) | 88.6 s | 193.2 s | 2.18× slower | FAIL |
| Project `tsc -p` emit (100 files) | 160 ms | 530 ms | 3.35× slower | FAIL |
| Project `tsc -p --noEmit` | 135 ms | 420 ms | 3.26× slower | FAIL |
| Project `tsc -p --declaration` | 210 ms | 2 170 ms | 10.5× slower | FAIL |
| Cold start `tsc --version` | 3.1 ms | 12.2 ms | 3.98× slower | FAIL |
| Tiny 10-line file check | 186 ms | 399 ms | 2.15× slower | FAIL |
| Max RSS, project `--noEmit` | 160 MB | 359 MB | 2.24× more | FAIL |
| Max RSS, project `--declaration` | 264 MB | **4 002 MB** | 15.2× more | FAIL |

Speedup needed to hit the gate at today's measured wall times is 6.5×–31× —
i.e., the port must both fix the parallelism gap and the declaration-emit
allocation blow-up before "3× faster" is on the table.

## Environment

- CPU: Intel Xeon Platinum 8559C, **8 cores** (`nproc`)
- RAM: 31 GiB
- Kernel: 6.8.0-1061-aws (Ubuntu 22.04)
- tscpp: clang++ 15.0.7, `-O3 -flto=thin`, lld — `cpp/build` Release
- tsgo: `go1.27.0` build of `tsc/cmd/tsc` (the repo's Go oracle)
- Both report `Version 7.1.0-dev`.

**Build note:** `tscpp` does not link at this commit — ~6 duplicate
`tsc::ls::*` symbol definitions across `ls/` slice TUs (e.g.
`getDocumentationFromDeclaration` in `jsdoc.cpp.o` **and** `hover.cpp.o`),
plus `testrunner_deps.cpp` needs `{...}` braces on a
`combinePaths(sv, vector<sv>)` call (fixed on this branch). Benchmarks used
the documented probe recipe: `main.cpp.o + sys.cpp.o + libtsc.a` with
`runLSP`/`runAPI` stubbed — `tsc`/`check`/`emit` paths are unaffected.

## Methodology

- Same machine, **sequential runs only**, warm page cache (one warmup pass
  per side before every timed surface).
- Identical flags on both compilers: `--noEmit --pretty false` for check
  surfaces; `-p tsconfig.json` variants for the project.
- Per-file: 500 files, `find tsc/testdata/tests/cases -name '*.ts' | shuf
  -n 500` (seeded), median of 3 timed runs each.
- Project: synthetic 100-file interdependent project
  (`gen_project.py`, ~200 lines/file, ~1 MB; classes, generics,
  decorators, enums, async, mapped/conditional types; `strict`,
  `experimentalDecorators`).
- Corpus sample and generated project are fully deterministic; reproduce
  with `cpp/tools/perf/run_all.sh`.

## Diagnostic-equality caveat (affects interpretation, not timing)

Per the task protocol, stdout+stderr of both compilers was diffed on a
sample before benchmarking. On all 500 corpus files:

- **451/500 (90.2%) produce byte-identical output** (including identical
  diagnostic text); 49/500 (9.8%) diverge.
- Every divergence observed is in **diagnostic message rendering only** —
  same TS codes, same file/line/col positions, same exit codes. The
  checker pipeline itself is faithful (consistent with the 12 734/12 734
  dump-conformance status); the bugs live in the display layer.

Distinct bug classes observed (reported as bugs; not perf-blocking):

1. **Qualified-name display** — `'C.X'` instead of `'X'`, `'"file".Album.artist'`
   instead of `'"artist"'`, `'�object.subkind'` instead of `'subkind'`
   (with a mojibake `�` prefix). Root cause is known in-tree:
   `cpp/internal/checker/checker.cpp:3111-3140` —
   `Checker::symbolToStringEx` is an *interim* implementation that
   prepends every named `symbol->parent` with `.` (TODO: replace with the
   faithful nodebuilder+printer port).
2. **Over-escaped quotes** — `'import * as ns from \"mod\"'` with literal
   backslashes. `cpp/internal/diagnostics/messages_generated.h:164` bakes
   `\\\"` into the message string: the messages-table generator
   double-escapes `"`. One-line generator fix + regen.
3. **Config-file diagnostics lose their file position** —
   `error TS5108: Option 'moduleResolution=node10'...` has no
   `tsconfig.json(6,25):` prefix on the tscpp side. `diagnostic.file()` is
   unset for config-parse diagnostics on this path
   (`cpp/internal/execute/tsc/diagnostics.cpp` →
   `diagnosticwriter::writeFormatDiagnostic`).

The generated benchmark project compiles **byte-identically clean** on
both sides (zero diagnostics; `--declaration` emit output verified
`diff -r`-identical), so all project-level results below compare
equivalent work.

## Results

### 1. Per-file check — 500 corpus files, `--noEmit --pretty false`

| Metric | tsgo | tscpp |
|---|---|---|
| Median per file | 179.3 ms | 387.5 ms |
| Mean per file | 174.5 ms | 375.9 ms |
| p95 per file | 199.6 ms | 422.9 ms |

- **Ratio of medians: 2.16× slower** · median per-file ratio 2.16 · p95 ratio 3.13.
- Worst-20 files (all small files where the fixed invocation floor
  dominates — most bail out before full checking):

| ratio | tsgo | tscpp | size | file |
|---|---|---|---|---|
| 3.54× | 23 ms | 83 ms | 163 B | taggedTemplatesWithIncompleteTemplateExpressions5.ts |
| 3.53× | 23 ms | 82 ms | 173 B | errorRecoveryWithDotFollowedByNamespaceKeyword.ts |
| 3.51× | 23 ms | 80 ms | 134 B | declarationSingleFileHasErrorsReported.ts |
| 3.49× | 23 ms | 79 ms | 1.7 kB | declarationEmitReexportedSymlinkReference3.ts |
| 3.47× | 23 ms | 78 ms | 306 B | unclosedExportClause02.ts |
| 3.43× | 23 ms | 78 ms | 317 B | verbatimModuleSyntaxReactReference.ts |
| 3.43× | 22 ms | 77 ms | 161 B | taggedTemplatesWithIncompleteTemplateExpressions4.ts |
| 3.43× | 23 ms | 77 ms | 533 B | aliasErrors.ts |
| 3.43× | 24 ms | 82 ms | 377 B | typingsLookup3.ts |
| 3.41× | 23 ms | 77 ms | 1.7 kB | declarationEmitMonorepoBaseUrl.ts |
| 3.40× | 23 ms | 77 ms | 128 B | staticsInAFunction.ts |
| 3.39× | 23 ms | 77 ms | 244 B | createArray.ts |
| 3.37× | 23 ms | 77 ms | 1.7 kB | superInLambdas.ts |
| 3.36× | 24 ms | 79 ms | 970 B | allowJsCrossMonorepoPackage.ts |
| 3.35× | 23 ms | 78 ms | 170 B | manyCompilerErrorsInTheTwoFiles.ts |
| 3.33× | 23 ms | 77 ms | 101 B | classMemberWithMissingIdentifier2.ts |
| 3.30× | 24 ms | 78 ms | 213 B | bundlerOptionsCompat.ts |
| 3.25× | 24 ms | 77 ms | 693 B | declarationEmitUsingTypeAlias1.ts |
| 3.24× | 24 ms | 79 ms | 9.5 kB | nodeModulesAllowJs1.ts |
| 3.22× | 24 ms | 78 ms | 436 B | moduleResolutionWithSuffixes_threeLastIsBlank3.ts |

Per-file time is nearly flat vs file size on both sides — the conformance
corpus is small files, so this surface measures ~fixed invocation cost:
~77 ms floor (cpp) vs ~23 ms (go) on early-exit files, ~388 vs ~179 ms on
fully-checked files.

### 2. Whole-corpus sequential — `xargs -n1`, 500 files

| | run 1 | run 2 | run 3 | median |
|---|---|---|---|---|
| tsgo | 88.9 s | 87.5 s | 88.6 s | **88.6 s** |
| tscpp | 195.1 s | 193.2 s | 193.2 s | **193.2 s** |

**2.18× slower** — matches the per-file median (the corpus surface is the
same workload amortized differently).

### 3. Realistic project — 100 files, ~1 MB, `strict`, `tsc -p`

| Variant | tsgo median | tscpp median | ratio |
|---|---|---|---|
| emit (`.js`) | 160 ms | 530 ms | **3.35× slower** |
| `--noEmit` | 135 ms | 420 ms | **3.26× slower** |
| `--declaration --emitDeclarationOnly` | 210 ms | 2 170 ms | **10.5× slower** |

CPU split (`/usr/bin/time -v`, `--noEmit`):

| | user+sys CPU | CPU % | wall |
|---|---|---|---|
| tsgo | 0.58 s | **435 %** | 0.13 s |
| tscpp | 0.42 s | **99 %** | 0.43 s |

**tscpp burns less total CPU than tsgo yet loses 3.3× on wall time** —
pure parallelism gap.

### 4. Cold start — 20-run median

| Surface | tsgo | tscpp | ratio |
|---|---|---|---|
| `tsc --version` (process init) | 3.1 ms | 12.2 ms | **3.98× slower** |
| 10-line file `--noEmit` | 185.7 ms | 398.5 ms | **2.15× slower** |

### 5. Memory — `/usr/bin/time -v` max RSS, 100-file project

| Surface | tsgo | tscpp |
|---|---|---|
| `--noEmit` | 160 MB | 359 MB (2.24×) |
| `--declaration --emitDeclarationOnly` | 264 MB | **4 002 MB (15.2×)** |
| minor page faults (decl emit) | 72 090 | **992 186** |
| kernel (sys) time (decl emit) | 0.24 s | **1.48 s** |

### 6. Scaling — same project at 1 / 10 / 50 / 100 files (`--noEmit`, median)

| N | tsgo | tscpp | ratio | go marginal/file | cpp marginal/file |
|---|---|---|---|---|---|
| 1 | 35 ms | 80 ms | 2.31× | — | — |
| 10 | 46 ms | 106 ms | 2.30× | ~1.2 ms | ~2.9 ms |
| 50 | 81 ms | 260 ms | 3.21× | ~0.9 ms | ~3.9 ms |
| 100 | 135 ms | 420 ms | 3.10× | ~1.1 ms | ~3.2 ms |

Both sides grow ~linearly, but tscpp's marginal slope is ~3× steeper —
and tsgo's slope is already flattened by parallel checking, so the
work-gap per added file is even larger than the slope ratio suggests.
Fixed floor (N=1, mostly lib parse+bind): ~80 ms cpp vs ~35 ms go.

## Pathology analysis & hypotheses

### A. Serial everything — the dominant wall-clock cause

Measured CPU utilization on the 100-file `--noEmit` check: **tsgo 435 %,
tscpp 99 %** (decl emit: 527 % vs 100 %). Go fans parse+bind+check across
a `checkerPool` (`tsc/internal/compiler/program.go`, `initCheckerPool`)
scaled to GOMAXPROCS; the port creates **one lazy checker** —
`cpp/internal/compiler/program.cpp:296` ("checker pool (lazy single
checker here)"), `getChecker()` at `program.cpp:437`, single-threaded
`BindSourceFiles` at `program.cpp:421`.

Total CPU per invocation is already comparable (0.42 s vs 0.58 s on the
project check; 0.28 s vs 0.25 s on a single file) — **once a checker pool
lands, project-level wall times should reach rough parity immediately**;
the remaining gap is fixed-cost.

### B. Fixed per-invocation cost ~2× — startup + lib processing

- `--version`: 12.2 ms vs 3.1 ms. Candidate: static-init of the ~3.9 MB
  `cpp/internal/bundled/bundled_generated.cpp` `embeddedContents` map (and
  the generated messages table) before `main` runs.
- Full-check floor: ~388 ms vs ~179 ms on small files; ~77 ms vs ~23 ms
  on early-exit files. The fixed phase both sides share is default-lib
  parse+bind+checker init; Go additionally parallelizes lib parsing
  (162 % CPU on single-file checks vs cpp's 99 %).

### C. Declaration emit — real work-pathology, not just parallelism

`--declaration` on the 100-file project: **2.17 s wall, 0.65 s user,
1.48 s sys, 4 002 MB RSS, 992 186 minor faults** (~40 MB retained per
source file; js-only emit is a normal 392 MB / 0.15 s sys). Two compounding
causes:

1. `cpp/internal/printer/emitcontext.h:804-815` — `releaseArenas()` is a
   documented no-op: the bump arena can't distinguish reachable nodes, so
   Go's `Factory.ReleaseArenas()` reclamation is skipped and **every
   serialized type node lives until program end**. `checker_nodebuilder.cpp:7979-7984`
   calls it expecting reclamation.
2. Type→node serialization (`cpp/internal/checker/checker_nodebuilder.cpp`,
   `transformers/declarations/transform.cpp`) runs serially per file
   (`program.cpp:1381` emit loop) and allocates fresh nodes + context
   maps per call; with `strict` + mapped/conditional types on every
   module the retained set balloons. The ~1.5 s of kernel time is
   page-fault servicing of that growth.

### D. Diagnostic-display bugs (from the equality check; fix independently)

Listed in the caveat section: interim `symbolToStringEx` qualified names
(`checker.cpp:3111`), `\\\"` escaping in `messages_generated.h`, missing
file position on config diagnostics.

## What this means for the ≥3× goal

In order of expected payoff:

1. **Checker pool / parallel program work** — closes most of the gap on
   multi-file surfaces; CPU work is already at parity. (Biggest single win.)
2. **Declaration-emit memory release** — an arena free-list or per-file
   context teardown in `emitcontext.h`/`checker_nodebuilder.cpp`; kills
   the 4 GB/1 M-fault pathology and the 10.5× ratio.
3. **Startup shaving** — lazy-init the embedded lib/message tables
   (matters most on per-file/CLI surfaces: every ms × 500 invocations).
4. **RSS discipline on the check path** — 359 MB vs 160 MB: arenas keep
   all parse/bind output live; fine for correctness but will hurt on
   bigger programs.

## Reproduce

```bash
# Build (probe recipe — full tscpp link has pre-existing dup-symbol breakage)
CC=clang-15 CXX=clang++-15 cmake -B cpp/build -S cpp -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld
ninja -C cpp/build CMakeFiles/tscpp.dir/cmd/tscpp/main.cpp.o \
  CMakeFiles/tscpp.dir/cmd/tscpp/sys.cpp.o libtsc.a
# stub runLSP/runAPI (const vector<string>&) → link libtsc.a -lz → /tmp/tscpp_probe
go build -o /tmp/tsgo ./tsc/cmd/tsc
TSGO=/tmp/tsgo TSCPP=/tmp/tscpp_probe bash cpp/tools/perf/run_all.sh
```

Scripts: `sample_corpus.sh`, `gen_project.py`, `sanity_diagnostics.py`,
`bench_perfile.py`, `bench_corpus.py`, `bench_project.py`,
`bench_coldstart.py`, `bench_memory.sh`, `bench_scaling.py`, `lib.py`,
`run_all.sh`. Raw outputs in `results/` (`*.json`, `perfile.csv`).
