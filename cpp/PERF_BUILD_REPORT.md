# PERF_BUILD_REPORT — contract-safe build optimizations (devin/cpp-perf-build)

Evaluated three build-level optimizations for `tscpp`, each gated on "doesn't
change emitted bytes" + a measurable speed win. Medians below are **wall-clock
seconds** on this VM (interleaved runs; same-machine comparison).

## Configurations

| id | config |
|----|--------|
| A  | `RelWithDebInfo` zig c++ (clang 21.1): `-O2 -g`, no LTO — the previously-documented recipe |
| B  | `Release` zig c++: `-O3 -DNDEBUG` + `CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON` (ThinLTO) |
| C  | B + `-DTSCPP_PGO=<merged.profdata>` (`-fprofile-use`) |
| S  | A statically linked (manual `zig ld.lld` link: system `crt1.o` + `-no-pie` + `libc.a`/`libm.a`/`libz.a` + `--allow-multiple-definition`) |

Profile for C: 304 `.profraw` (~3.9 GB) → 45 MB `.profdata`, collected from an
instrumented **clang-21** build running perfproj emit/noEmit/decl + 150-file
conformance `check`+`emitdump` + a fourslashrunner slice.

## Results — 100-file project (`/tmp/perfproj`, 15 interleaved runs)

| mode   | A (s) | B (s) | C (s) | B vs A | C vs B | C vs A |
|--------|-------|-------|-------|--------|--------|--------|
| emit   | 0.196 | 0.181 | 0.169 | −7.7%  | −6.6%  | −13.8% |
| noEmit | 0.150 | 0.136 | 0.126 | −9.3%  | −7.4%  | −16.0% |
| decl   | 0.224 | 0.211 | 0.190 | −5.8%  | −10.0% | −15.2% |
| **total** | **0.570** | **0.528** | **0.485** | **−7.4%** | **−8.1%** | **−14.9%** |

## Results — 500-file project (`/tmp/perfproj_big`, 9 interleaved runs)

| mode   | A (s) | B (s) | C (s) | B vs A | C vs B | C vs A |
|--------|-------|-------|-------|--------|--------|--------|
| emit   | 0.850 | 0.806 | 0.730 | −5.2%  | −9.4%  | −14.1% |
| noEmit | 0.974 | 0.915 | 0.846 | −6.1%  | −7.5%  | −13.1% |
| decl   | 1.150 | 0.989 | 0.891 | −14.0% | −9.9%  | −22.5% |
| **total** | **2.974** | **2.710** | **2.467** | **−8.9%** | **−9.0%** | **−17.0%** |

Harness sanity check vs Go oracle (`bench_project.py`, RUNS=9, cpp/go ratio):
A: emit 0.92 / noEmit 0.80 / decl 0.81 · B: 0.85 / 0.73 / 0.79 ·
C: 0.69 noEmit / 0.72 decl (emit contaminated by concurrent link — see
interleaved table for the clean C numbers).

## Static linking — S vs A (15 interleaved runs)

| project | emit | noEmit | decl |
|---------|------|--------|------|
| 100f    | −0.5% | +0.1% | +2.3% |
| 500f    | −2.6% | +0.8% | −5.3% |

No consistent win — noise in both directions. Direct measurement
(`LD_DEBUG=statistics`) shows only ~269 relocations ≈28k cycles at startup;
the earlier ~1.7% `_dl_relocate_object` attribution was startup-phase noise.
Fully-static also carries real cost: `zig c++ -static` fails outright
(mimalloc's malloc interposers collide with `libc.a`; the zig driver refuses
`--allow-multiple-definition`/`-z muldefs`/`-Xlinker`), and zig's own `crt1.o`
yields a static-PIE binary that SIGSEGVs in `_dl_relocate_static_pie` — only
system `crt1.o` + `-no-pie` produced a working binary. **Not landed.**

## Decisions

1. **`-O3` + ThinLTO (Release) — KEEP.** ~8% over the -O2 recipe. Already the
   effective default (`CMAKE_BUILD_TYPE` defaults to `Release` when unset);
   no code change needed. CI's `cmake -DCMAKE_BUILD_TYPE=Release` gets it too.
2. **PGO — LAND.** −8.1% / −9.0% vs B on total time, well above the ≥3% gate.
   Landed as opt-in `-DTSCPP_PGO=<file>` (CMakeLists) + `tools/perf/gen_pgo.sh`
   (instrument→workload→merge→rebuild). **Off by default**; CI unaffected.
3. **Static — DON'T LAND.** No measurable win on either project size; the
   toolchain friction (manual ld.lld link) isn't justified. Documented above.

## Toolchain notes (why the pipeline looks this way)

- zig c++ **silently drops `-fprofile-generate`** — compiles and links but
  emits no `__llvm_profile_*` and no `.profraw`. The instrumented stage needs
  real clang (clang-21 used here; `gen_pgo.sh` self-checks this with a probe).
- `-fprofile-use=<profdata>` **works under zig** (bogus path → link error),
  so the final PGO build reuses the normal zig toolchain.
- `LLVM_PROFILE_FILE` pattern `%c` (continuous mode) produces 0-byte files on
  this setup — use `%p.profraw`.
- fourslashrunner children `fork()`+`_exit()` → profile flush never runs;
  child coverage is lost (documented in gen_pgo.sh). LS paths still get
  indirect coverage via shared parser/checker code from 2a/2b.
- Instrumented build with clang needs `lld` (`-fuse-ld=lld` or default);
  unrelated to zig.

## Byte-identity verification (on the PGO build)

| check | result |
|-------|--------|
| `diff -r out_cpp out_go`, perfproj_big emit | **500/500 files identical** |
| conformance `emitdump` sample vs Go oracle (`/tmp/emit_oracle`) | **150/150 identical** |
| `unittestrunner` | **1122/1122 pass** |

Artifacts: `cpp/tools/perf/gen_pgo.sh`, `TSCPP_PGO` option in
`cpp/CMakeLists.txt`. Bench scripts used: `tools/perf/bench_project.py`
(RUNS=9) + an interleaved 3-way timing harness (15/9 runs, medians above).
