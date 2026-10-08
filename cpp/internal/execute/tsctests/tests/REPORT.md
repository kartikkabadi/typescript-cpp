# tsctestrunner — scenario port report

Branch: `devin/cpp-tsctest-runner` (off `2408468a20`, no merge/rebase onto `devin/cpp-port`).

## Outcome

**99 / 99 scenarios PASS** — every registered case is outcome-identical with the Go
oracle (`go test ./tsc/internal/execute/tsctests/ -run ...` passes the same subtests,
and each scenario's produced baseline is byte-identical to the committed
`tsc/testdata/baselines/reference/tsctest/**` baselines the runner compares against).

Runner: `cpp/cmd/tsctestrunner/main.cpp` (`-run <regex>` filter, `PASS`/`FAIL <name>`
per scenario, exit 0 iff all selected scenarios pass).
Registry: `tests/registry.h` + `REGISTER_TSCTEST("Name::subScenario", fn)` static
self-registration into `std::vector<TscTestCase>`; names mirror
`<GoTestFunc>::<subScenario>` plus `[b -v]` / `[watch]` / `[tsc]` disambiguators.

### Ported coverage (from `tsc_test.go`, `tscbuild_test.go`, `tscwatch_test.go`)

| Group | Cases | Covers |
| --- | --- | --- |
| `TestTscCommandline` | 31 | tsconfig parse/showConfig, `--help`/`--help all`, `NO_COLOR`/`FORCE_COLOR`/`TERM`, `--lib`, non-emit command-line scenarios |
| `TestWatch` | 16 | `-w` watch with `edits` sequences (multi-edit, config edits, file add/remove) |
| `TestTscDeclarationEmit` | 12 | declaration emit incl. `Sub_base` `_base` synthesis, import-type references, Windows paths + `ignoreCase` |
| `TestTscComposite` | 7 | composite project emit |
| `TestTscNoEmit` / `TestTscNoEmitOnError` / `TestTscNoEmitWatch` | 15 | `--noEmit`, `--noEmitOnError` incl. exit-status semantics and watch variants |
| `TestTscMissingFiles` | 5 | missing-file diagnostics + tsconfig `files` globs (dedent-sensitive contents) |
| `TestBuildDemoProject` | 6 | `-b` demo project: clean build, circular refs, bad refs, verbose `[b -v]` variants |
| `TestBuildConfigFileErrors` | 5 | `-b` config syntax/reference errors incl. watch variant |
| `TestBuildClean` | 2 | `-b --clean`, file/output name clashing, tsx+dts |

## Divergences found while porting — root causes + fixes

All were mis-ports in `cpp/` (never in `tsc/`); each was diagnosed by diffing our
runner's produced baseline text against the committed reference.

1. **Case-insensitive file lookup miss** (`TestTscDeclarationEmit::when using Windows
   paths and uppercase letters`): `filesLoader::toPath` hardcoded
   `useCaseSensitiveFileNames = true` in `program.cpp`, so source files were stored in
   `filesByPath` under case-sensitive keys while `GetSourceFile` lowercased lookups
   (`d:/...` vs stored `D:/...`). Module resolution succeeded, but
   `GetSourceFileForResolvedModule` returned nil → nil module symbol → named-import
   alias targets unresolved → `PartialType(Common)` typed `any` → emit `declare const
   Sub_base: any` instead of `import("./utils/type-helpers").MyReturnType`. Fix:
   `loader.useCaseSensitiveFileNames = host->FS()->UseCaseSensitiveFileNames()`
   (fileloader.go:168).

2. **`fileTime` zero-value sentinel** (`[b -v]` verbose scenarios): C++
   `fileTime{}` = epoch 0, which is *newer* than the in-memory FS's pre-epoch fake
   mtimes; Go `time.Time{}` = year 1. `newestInputFileAndTime` therefore stayed the
   empty sentinel and the build report claimed output '' was older than a phantom
   input. Fix: initialize with `fileTime::min()` in `buildtask.cpp`. Same site also
   had the `inputOutputName{input, output}` constructor args swapped — fixed.

3. **`--help` option ordering**: `std::sort` is unstable and swapped the two
   `--help` decls (`-h` before `-?`); Go `slices.SortFunc` preserves declaration
   order. Fix: `std::stable_sort` in `tsc/help.cpp`.

4. **Localized diagnostics**: `getLocalizedMessages` was stubbed → `locale`-related
   baselines fell back to English. Ported all 13 `loc/*.generated.json` tables to
   generated `cpp/internal/diagnostics/loc_generated.h` plus a simplified
   `language.Matcher` (same-langID +4 region / +2 script, `-1` for zh-Hant requests
   matching zh-CN) in `localize.cpp`.

5. **Tsconfig `paths` string arrays ignored**: JSON string arrays land in
   `JsonStrList` (Go `[]any` of strings) but `ParseStringArray` only accepted
   `JsonArray` → `paths` values empty → spurious TS5066. Fix: `JsonStrList` arm in
   `tsoptions/parsinghelpers.cpp`.

6. **Resolution trace arg splat**: `getTraceFunc` pushed each collected
   `vector<string>` argument as a single `std::any` instead of splatting, so traces
   printed `''` (e.g. `'typesVersions'`). Fix in `module/resolver.cpp`.

7. **`fileInfos:[]` dropped in readable buildinfo**: Go marshals a non-nil empty
   slice as `[]` but nil as absent. Added `fileInfosAssigned` flags on `BuildInfo` /
   `readableBuildInfo` so the writer distinguishes assigned-empty from nil.

8. **`Dedent` + `R"TS(...)"` interaction**: Go backtick literals whose `{` sits at
   column 0 keep inner (e.g. 20-space) indentation; uniformly indented C++ raw
   literals dedent to 4 spaces and byte-diverge. Rule applied everywhere: write
   `Dedent(R"TS({` with `{` immediately after `R"TS(` at column 0, or keep the exact
   bytes when there is no `{` (e.g. a literal that is only a trailing `\t` after
   `{` — see `tsc_test.go:894`).

9. **Missing-file tsconfig contents**: same dedent issue — fixed 4
   `TestTscMissingFiles` scenarios by placing `{` at column 0 inside the raw literal.

## Skipped candidates

- No scenario was skipped for a blocking port gap — the `tscInput` fields needed
  (`subScenario`, `commandLineArgs`, `files`, `cwd`, `edits`, `env`, `outputIsTTY`,
  `ignoreCase`, `windowsStyleRoot`; `tscEdit{caption, commandLineArgs, edit,
  expectedDiff}`; `noChange` / `noChangeOnlyEdit`) all exist in `tsctests.h`.
- The ~25-scenario target was exceeded (99 cases); remaining unported entries from
  the three Go files are larger `tscwatch_test.go` edit sequences and deep
  incremental `.tsbuildinfo` reuse variants — candidates for a follow-up batch.

## Build note

`tscpp` (the other `add_executable`) fails to link on the branch base `2408468a20`
with duplicate `tsc::ls::*` symbols (and fails to compile `testrunner_deps.cpp` on
the unmodified base) — pre-existing, unrelated to this task; `tsctestrunner` builds
and runs clean.

## Verify

```bash
ninja -C cpp/build tsctestrunner
./cpp/build/tsctestrunner            # 99 PASS / 0 FAIL
./cpp/build/tsctestrunner -run 'DeclarationEmit'
```
