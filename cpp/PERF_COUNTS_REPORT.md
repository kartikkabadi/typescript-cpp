# PERF_COUNTS_REPORT — Symbol/Type overcount vs Go oracle

## Verdict

All `--diagnostics` counters on `perfproj_big` (500-file generated project,
`PROJ_FILES=500 python3 cpp/tools/perf/gen_project.py /tmp/perfproj_big`,
`tsc -p tsconfig.json --noEmit --pretty false --diagnostics`) now match the Go
oracle **exactly**. The over-count was wasted work inside `staleForCheckFile`
gating of per-checker link caches: tscpp invalidated `links->resolvedType`
entries produced while a different file was being checked, but Go never does —
its `valueSymbolLinks`/`typeNodeLinks` live for the whole checker lifetime.

| Counter        | tsgo      | tscpp before | tscpp after | delta |
|----------------|-----------|--------------|-------------|-------|
| Symbols        | 1,001,307 | 1,020,156    | 1,001,307   | **0** |
| Types          |   275,351 |   285,876    |   275,351   | **0** |
| Instantiations |    31,430 |    31,430    |    31,430   | 0     |
| Identifiers    |   609,466 |   609,466    |   609,466   | 0     |

## Method

Per-callsite stack histograms on both sides: a temporary `TSC_ALLOC_HIST`
env-gated recorder in `newType`/`newSymbol`/`newSharedDataSymbol`
(`_Unwind_Backtrace` in C++, `runtime.Callers` in Go) plus named reason
counters on every return path of `instantiateSymbol`. The histograms were
bucketed by first semantic caller and diffed. All instrumentation was removed
before landing; only the semantics change remains.

## Root cause

`staleForCheckFile` (checker.h) is a C++-only mechanism added in `69c3a1fd03`:
it treats a link-cache entry produced while checking file A as stale while the
checker is inside file B, forcing re-resolution so diagnostics re-fire under
the "right" file. **Go has no such invalidation** — `checkSourceFile`
(checker.go:2241) never resets `valueSymbolLinks`/`typeNodeLinks`; entries live
for the checker's whole lifetime.

Three read sites therefore diverged:

1. `Checker::instantiateSymbol` (checker_members.cpp) — the short-circuit that
   returns `symbol` itself when `links->resolvedType` can't contain type
   variables. C++ gated the read with
   `staleForCheckFile(links->resolvedTypeCheckFile)` (and `writeTypeCheckFile`
   for setters); Go (checker.go:21177) reads unconditionally. Reason counters:
   identical call volume (667,488), but Go took `resolvedReused` 21,010 times
   vs C++ 2,161 — the stale gate intercepted ~594k calls. Each miss created a
   fresh transient shared-data symbol (+18,849 Symbols, the entire delta).

2. `Checker::getTypeOfFuncClassEnumModule` (checker_decltypes.cpp) — same
   `resolvedTypeCheckFile` gate on `valueSymbolLinks`; Go (checker.go:17304)
   caches unconditionally. Each miss re-created the declared object type for
   func/class/enum/module symbols (+3,581 Types).

3. `Checker::getTypeFromTypeLiteralOrFunctionOrConstructorTypeNode`
   (checker_typenodes.cpp) — same gate on `typeNodeLinks->resolvedType`; Go
   (checker.go:23445) caches unconditionally (+992 Types, ~2× the 1,008 Go
   makes).

The remaining +5,952 Types were a downstream cascade, not a fourth bug:
duplicate declared types from (2)+(3) have different object identities, so
type-identity-keyed caches missed — `getUnionTypeFromSortedList` +2,976 and
`getIntersectionTypeEx` +2,976 (cross-product intersections inside
`createUnionOrIntersectionProperty`/`getReducedType`/`getNormalizedType`).
Once declared-type identity stabilized, these vanished on their own.

Precedent: `51caecaece` removed the identical gate from
`getTypeOfInstantiatedSymbol`/`getWriteTypeOfInstantiatedSymbol` ("the entries
hold a pure instantiated type with no diagnostics to re-fire") — this change
applies the same reasoning to the three sites above. Other `staleForCheckFile`
sites that guard diagnostic-bearing caches (e.g. `resolveTypeReferenceName`
re-fire at checker_typenodes.cpp:720) were left untouched.

## Changes

- `cpp/internal/checker/checker_members.cpp` — `instantiateSymbol`: drop both
  `staleForCheckFile` guards on `resolvedType`/`writeType`.
- `cpp/internal/checker/checker_decltypes.cpp` — `getTypeOfFuncClassEnumModule`:
  drop `staleForCheckFile` guard on `resolvedType`.
- `cpp/internal/checker/checker_typenodes.cpp` —
  `getTypeFromTypeLiteralOrFunctionOrConstructorTypeNode`: drop
  `staleForCheckFile` guard on `resolvedType`.

## Verification

- Counters: table above — exact parity on all four.
- `perfproj_big` full check output vs tsgo: byte-identical.
- check conformance (`conformance_check.sh` vs `/tmp/checkdump` oracle):
  500/500 PASS on `corpus500.txt`.
- emit conformance (`emit_triage.py` vs `emitdump` oracle): 500/500 PASS.
- decl-emit (`EMIT_FLAGS="--declaration --emitDeclarationOnly"`): 500/500 PASS.
- tsctestrunner: 117/117. unittestrunner: 1,122/1,122.
- fourslashrunner: see commit message / session for sample result.
