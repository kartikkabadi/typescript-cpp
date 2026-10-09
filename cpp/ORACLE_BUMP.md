# Oracle bump: `69ac1647` → `fed0bf24` (2026-10-08)

Vendored Go oracle advanced across the full 61-commit upstream range
(`git log 69ac1647..fed0bf24 -- tsc/`). Verified byte-identical:
`git diff fed0bf24 -- tsc/` is empty except the repo-local oracle drivers
`tsc/cmd/{bindump,checkdump,emitdump,lexdump,parsebench,parsedump,scanbench,transpiledump}`
(which upstream does not have).

Original task scope (18 commits) was expanded mid-flight to the full range
so the vendored tree matches a real upstream pin instead of a cherry-picked
hybrid. All 61 commits vendored in upstream chronological order, each as
`tsc: vendor <sha-prefix> <title>`; each behavioral delta ported into `cpp/`
as `cpp: port <sha-prefix> ...` commits.

## Final gate results

| Gate | Result |
|---|---|
| `./unittestrunner` | **1110/1110 pass** |
| `./tsctestrunner` | **117/117 pass** |
| `./fourslashrunner` slice (doc-symbols, navTo/workspaceSymbol, signatureHelp, declarationMap/state baselines, incrementalJsDoc, goToTypeDefinition — 168 tests) | **168/168 pass** |
| `cpp/tools/conformance_check.sh` 50-file random conformance corpus vs `/tmp/checkdump` (Go oracle rebuilt at fed0bf24) | **50/50 PASS** |

## Per-commit port table

| Upstream | Title (abbrev.) | C++ port commit | Status |
|---|---|---|---|
| 09d49665 | [api] project-ref diagnostics w/o config | ac20e497b2 | ported (nil ConfigFile guard + new api test) |
| 1a78786c | flaky diag: `typeof import()` qualifiers | d22dad4e32 | ported (markLinkedReferences treats qualifier parts as non-refs) |
| 21b260b4 | exclude top-level imports from doc symbols | 66ecbc69d3 | ported + TestDocumentSymbolTopLevelImports |
| 253bcd86 | one nodebuilder per emit resolver | 237e3625c0 | ported (emit context scoped per resolver) |
| afd02f14 | JSDocParameterTag in getTypeAnnotationNode | a0e54eafaf | ported |
| c3f14c2f | decorator metadata emit crash (object literal) | 4e9212a2b3 | ported (MetadataTransformer visitObjectLiteralExpression) |
| d61a7d23 | [api] skip disk-layout import diags on custom res | 6e7aaf53fc | ported (IsCustomResolution guard) |
| de61e696 | Update DOM types | 921687e2ad | ported (bundled libs regenerated via genbundled) |
| fc636a69 | [api] stop dropping dispose Promises | 814910f1d2 | ported (asyncDispose hint on using-decl init failure) |
| 0ba5d992 | emit crash: duplicate private names | c8c2f2017b | ported (untransformed private identifiers invalid for transform) |
| 0bd74f77 | CJS export assignments, shadowed names in loops | 706b95b182 | ported (for/for-in-of bodies use main visitor) |
| 54f05ae2 | malformed destructuring emit crashes | 4453d3bd2a | ported (relaxed asserts in destructuring visits) |
| 9055b588 | workspace/symbol crash on inferred project | 1910bf37bc | ported (inferred project refreshed on project-tree request) |
| 91521cf2 | filter Go compiler test files | d201385b27 + 086ce56daa | ported (compiler_test_filter in runner build) |
| a501b11f | index re-exporting modules for decl emit | 29fc545f7a | ported (external-module-container index) |
| bea2e849 | reject deferred imports w/o namespace binding | 80e476ba25 | ported; SourceKeyword arm completed under 2f9fd09a |
| ec47d33c | computed property names always checked | d30efe4a08 | ported |
| fed0bf24 | getDocumentationForSymbol docstring move | 5a649d0c53 | no C++ delta (Go comment relocation) |
| 192ede8dcf | [api] improve callback FS | a008251274 + 19ef26e09e | ported (kinded responses, stat, :error, caseSensitivity flag) |
| d3eed5a611 | TS2565 regression on inherited class fields | fd1a3c4960 | ported |
| f34fb210f7 | getTypeAtLocation crash on type-only import | fd1a3c4960 | ported + checker.TestGetTypeAtLocationOfTypeOnlyImportClause |
| 11bd5190e6 | nested rest bindings → assignment targets | 1b18ff2ae2 | ported |
| 0f66f2bfbf | decl-emit panic, unnameable export default | 1b18ff2ae2 | ported |
| a5c43c4d54 | Program options lifetimes / resolution data | 8d843be548 | ported (ResolutionData owns packageJsonInfoCache; projectReferenceFileMapperBuilder) |
| 65a8caeef8 | avoid cloning cached decl types post-truncation | e99a401020 | ported |
| 65425b7a52 | decl maps for export-assignment expressions | eb521ab723 | ported (AssignSourceMapRange on synthetic export assignments) |
| 85f9dfb992 | watch program files near filesystem root | c3846aa306 | ported (watch at any depth + disk-path guard) |
| 299a555c3a | flaky diag: decl emit on untyped module imports | 9895670fe2 | ported (ignoreErrors decl-emit specifier resolution; TrackFlakyDiagnostics) |
| e8c1ac1603 | [api] binder symbols owned by SourceFiles | 006f93979e | ported |
| b85298b6a8 | exclusive checker acquisition | d27e9cea7d | ported |
| 697b847e11 | export modifiers on expando namespace exports | df68688403 | ported |
| 81d3f3f5e8 | flaky diag: MarkLinkedReferencesRecursively | e6f570baa7 | ported |
| 834e7862c0 | cyclic structures + truncation in decl emit | 4dcb8a2c35 | ported |
| 619d485a63 | no contextual typing of static props by own class | d3761b0947 | ported |
| 792ffccb90 | disallow NoSubstitutionTemplate in import attrs | d2e4f0a1bd | ported |
| c4d731aae1 | pure return-type inference filtered by constraint | f448e71b61 | ported |
| 64c5fa0b92 | customConditions test applies both conditions | — | corpus/baselines only; vendored files auto-globbed by runner |
| c8e9b7d259 | cache inferences from type arguments | 263d2caee7 | ported |
| 43521c80ec | [api] completion symbols from current snapshot | d09c830ec2 | ported |
| edf7da4e93 | preserve reverse mapped types in decl emit | 04f2d41178 | ported |
| 02c1c9a6ab | fix `with` statement crash | 5955c69c71 | ported |
| 237b14a5d9 | merged-declaration diagnostic ownership | 10f09f569e | ported |
| 688d86d7f6 | [api] add getSymbol(decl) | 361da96f0e | ported |
| e8fd69fc1e | option definitions codegen + JSON schema | e9f7ceb4da + 939f36d139 | ported (watch options removed; option Equals; reject-watch-interval baselines) |
| 15dee00a1c | cycles in array/tuple serialization | 0aa28ceb16 | ported |
| af36d532b0 | export= class visibility + top-level export type | a9b4a7fe68 | ported |
| bf00f213ff | esnext Promise.allKeyed / allSettledKeyed | 453d0879ea | ported (bundled libs regenerated) |
| 59f5b0233f | typed-path prep bugfixes | fc5ecbbf67 | ported |
| 09b1db0617 | elide empty named imports under verbatimModuleSyntax | 63a8b927f6 | ported |
| a4b1410230 | Array.at docstring | — | lib text only; verified already in vendored lib/regenerated bundle |
| 0ad3777160 | VS Code ext LSP middleware hooks | — | no cpp surface (VS Code client plumbing; Go lsp middleware deletions mirrored at vendoring) |
| d2f9dd68cf | [api] merged-symbol checker methods | 89629ba51a | ported |
| 7e5d1c1c1d | fsevents test retries | — | no cpp surface (Go darwin test infra; cpp fswatch tests don't run fsevents) |
| f9f8d01292 | TokensAreOnSameLine misport fixup | e6662a794d | ported (context.cpp Pos fix; 2 fourslash tests un-skipped) |
| 50d70a3f5f | stable getInferTypeParameters ordering | folded into fba953cb31 | ported (checker.cpp stable_sort) |
| bcd7c42255 | idle cache clean timer on Session | — | already in tree via 59f5b0233f port; Go synctest-based test has no cpp surface (documented skip) |
| ca197b7b85 | restart file imports on lowered node_modules depth | folded into fba953cb31 | ported (fileloader.cpp lowered-hoist) |
| a1ef42b9ea | flaky diag: JS constructor-defined properties | folded into fba953cb31 | ported (checkConstructorDeclaredProperties in stmtclass+expressions_a) |
| cb72b2e17b | update localization files | folded into fba953cb31 | ported (loc_generated regenerated from vendored *.generated.json; fixes upstream `]5D;` corruption; locale .gz files are not embedded in cpp) |
| 2f9fd09a | source phase imports | fba953cb31 | ported (SourceKeyword, phase-modifier parser machinery, AbstractModuleSource, organizeimports bucket, proto wire field, corpus vendored) |
| ed480721 | strongly type file paths | f8ea7f17c6 + 14687d63c2 + f03c004ddb + 939f36d139 | ported (typed_paths.{h,cpp} rooted/pathkey types; CaseSensitivity; api proto `caseSensitivity`+`currentDirectory`; vfsmatch encoded-dynamic glob roots; ParsedCommandLine::baseDirectory; state-baseline `CaseSensitivity:` label) |

## Surfaces intentionally not ported

- **Go test-infra internals** (`7e5d1c1c1d` fsevents retry harness, `bcd7c42255`
  synctest session-timer test): exercise Go-only test scaffolding; the
  production deltas they cover are ported.
- **`a4b1410230`, `fed0bf24`**: docstring/comment changes inside Go source
  or lib text already regenerated through `genbundled`/`gencpp` pipelines.
- **`0ad3777160`**: LSP-middleware installer API is a VS Code client surface;
  the vendored deletions were mirrored but there is no cpp counterpart.
- **`cb72b2e17b` locale `.gz` payloads**: cpp embeds regenerated
  `loc_generated.h` tables (done), not the compressed files.

## Notes

- `git diff fed0bf24 -- tsc/` residual: only `tsc/cmd/*` repo-local oracle
  tools (updated for the typed-path API; `go build ./...` green).
- Interleaved commits (`a5c43c4d54` × `ed480721` × `237b14a5d9` around
  FileIncludeReason/fileloader/resolver) were ported to net `fed0bf24`
  semantics and the grouped commit messages list all contributing SHAs.
- Go `assertPanics` does not match panic text; `testutil::AssertPanics`
  does — tspath test expectations were set to Go's exact panic strings.
