# Oracle delta: `fed0bf24` → `2d8450f9` (2026-10-09)

Second oracle advance (see `ORACLE_BUMP.md` for the 61-commit method from
`69ac1647` → `fed0bf24`). Upstream `microsoft/TypeScript` `main` had **5 new
commits** touching `tsc/` past `fed0bf24` (`git log fed0bf24..2d8450f9 --
tsc/`). Each was vendored in order as `tsc: vendor <sha-prefix> <title>` and
ported to `cpp/` as `cpp: port <sha-prefix>` commits on `devin/cpp-oracle2`.

Vendored tree verified byte-identical: `git diff upstream/main -- tsc/` is
empty except the repo-local oracle drivers `tsc/cmd/*`.

## Per-commit port table

| Upstream | Title | Vendor commit | C++ port commit | Notes |
|---|---|---|---|---|
| c0ca4744c9 | [api] Prepare for beta release (#64681) | ed79d9ddbe | 389616362d | Dropped `ProjectResponse.RootFiles`/`CompilerOptions` (`parsedCommandLine.fileNames`/`options`); `formatNodeForInsertion` moved into the Language service methods section. |
| 6ad8c56f9b | Shared data symbols (#64691) | 62dfee16d5 | fcd0406acc | Largest delta: Go `Symbol` split into unique `flags`/`checkFlags`/`id` + shared `symbolData` (`name`/`declarations`/`valueDeclaration`/`members`/`exports`/`parent`/`exportSymbol`). C++ mirrors with `SymbolData`/`SymbolWithData` in `ast/symbol.h`, `newSharedDataSymbol` + `symbolWithDataArena` in `Checker`, `setSymbolData`, and `x->field` → `x->data->field` across ~78 files. `instantiateSymbol` now shares the symbol data (was a partial shallow copy). |
| aad4c72bf2 | [api] Answer nested requests on the sync connection in stack order (#64639) | 9a403b6df6 | 4fa1432713 | `SyncConn` gains `calls`/`reading`/`turn` + `lockTurn(depth)`; `handleRequest` takes a depth and locks turns before each protocol write. New tests `TestSyncConnAnswersNestedRequestsInStackOrder` + `TestSyncConnNotificationHandlerCanCall` ported (synctest `Wait()` approximated by handler-entered signals + sleeps over the in-memory `netPipe()`). |
| fff11d8a7c | Update localization files (#64664) | d6628a00b0 | (genloc) | 26 `loc/*.generated.json` + gzip files; `cpp/internal/diagnostics/loc_generated.h` regenerated via new `cpp/tools/genloc.py` (committed with the port). |
| 2d8450f9b5 | feat: add iterator methods to esnext (#64095) | 6b67721444 | c59a38fd70 | New `lib.esnext.iterator.d.ts` embedded via `genbundled.py`; `LibMap` `esnext.iterator` → `lib.esnext.iterator.d.ts`; `esnextLibs` test list +1; new compiler cases (`iteratorChunks`, `iteratorConstructorAugmentation`, `iteratorIncludes`, `iteratorJoin`, `iteratorZip`) auto-globbed. |

## Gate results

| Gate | Result |
|---|---|
| `git diff upstream/main -- tsc/` | only repo-local `tsc/cmd/*` |
| `go build ./tsc/...` | green |
| `ninja tscpp unittestrunner tsctestrunner fourslashrunner` | green |
| `./unittestrunner` | 1114/1114 pass (was 1110; +2 conn_sync tests, +2 ast symbol tests) |
| `./tsctestrunner` | 117/117 pass |
| `./fourslashrunner -run 'Test[A-M]'` + `-run 'Test[N-Z]'` | 2899 + 1246 = 4145 pass, 0 regressions |

Regressions found and fixed during the bump: none. The only test-side
divergence is documented in `tests_symbol.cpp` — Go's slice-identity
assertions (`declarations[0] = x` visible through `symbol.Declarations()`)
do not translate since the port stores `std::vector` (copy semantics);
whole-data sharing via `setSymbolData` is what `instantiateSymbol` relies
on and is covered by the suite.
