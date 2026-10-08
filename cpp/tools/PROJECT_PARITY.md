# Multi-file project parity — `tscpp tsc` vs Go `tsc`

Conformance harness: `cpp/tools/project_parity.sh` (run with
`TSCPP=/path/to/tscpp`, `CORPUS=/tmp/projcorpus`, `REPORT=/tmp/projreport`).
For each project under `$CORPUS` it rsyncs the tree into two identical run
dirs (`/tmp/projrun/<proj>/{go,cpp}`), runs the Go oracle and `tscpp tsc` with
identical args, then diffs stdout, stderr, exit code, the whole emitted file
tree (`diff -r`), and `.tsbuildinfo` bytes. Sequential commands per project
(`a|b` in its `ARGS` manifest) exercise incremental/`--clean`/`--dry` runs.

**Result: 97 projects / 109 runs — 109 PASS, 0 DIFF** (final run of
`cpp/tools/project_parity.sh`).

| Family | Scenarios (all PASS) |
|---|---|
| cross-module imports/exports | basic-relative, nested-relative, reexport-chain, import-equals, export-assignment, circular-imports, dts-shadow, dir-index |
| paths/baseUrl/rootDirs/typeRoots | paths-star, paths-fallback, paths-exact, paths-nobaseurl, baseurl-resolve, rootdirs-merge, typeroots-custom, types-directive |
| `-b` build mode (project refs, composite, decl maps, dry/clean, cycles) | build-2proj (b/b2/bclean), build-3chain, build-declmap, build-dry, build-err, build-cycle, build-noncomposite, composite-p |
| emit options | outdir-flat, decl-suite, decl-emitonly, decl-decldir, inlinesourcemap, inlinesources, emitbom, newline-crlf, removecomments, incremental-tsbi, outfile-bundle, noemitonerror |
| module variants | mod-cjs, mod-amd, mod-umd, mod-system, mod-es2015, mod-es2022, mod-node16-cjs, mod-node16-esm, mod-nodenext, mod-bundler, mod-preserve, dynamic-import, import-meta, top-level-await |
| target / jsx | target-es5, target-es2015, target-es2022, jsx-react, jsx-runtime-pkg, jsx-preserve |
| js/ts mixing & syntax features | allowjs-mixed, allowjs-nocheck, checkjs-errors, resolvejson, json-missing-flag, isolated-ok, isolated-violation, verbatim-cjs, verbatim-esm, importhelpers, esdecorators, esdecorators-new, constenum-preserve, namespace-merge, triple-slash, ts-expect-error, using-downlevel |
| node_modules package resolution | pkg-types, pkg-main-dts, pkg-typesversions, pkg-scoped, pkg-deep-import, pkg-notypes |
| error scenarios (diagnostics must match) | err-type, err-missing-module, err-config-syntax, err-config-bad-value, err-config-unknown-option, err-files-missing, err-extends-missing, err-empty-inputs, err-no-tsconfig, err-rootdir, err-duplicate, err-syntax |
| misc CLI | extends-chain, listfiles (p/plist/pfiles), traceresolution, showconfig, multi-args-cli, noemit-check |

`.tsbuildinfo` files compare **byte-identical** on every incremental/build
scenario (XXH3 hashes match — e.g. `incremental-tsbi`, `composite-p`,
`build-*`).

Notes:

- `jsx-runtime-pkg/pdev` (`react-jsxdev`) emits absolute paths into
  `_jsxFileName` / source maps — byte-identical only after normalizing each
  side's own run-dir prefix (`tree-pathev` in the harness). PASS; not a
  divergence.
- `--watch` excluded per task scope. `importhelpers` was diffed normally: the
  corpus ships a hand-written `node_modules/tslib` so both sides resolve it
  identically.

## Divergences found and fixed (all small, attributable, cpp/-side only)

Repro for each: `/tmp/projcorpus/<scenario>/` + `/tmp/projrun/<scenario>/{go,cpp}`.

### 1. `-b` build mode crashed: `bad_alloc` / `vfs: path <garbage> is not absolute`

- **Repro:** `build-2proj` (`tsc -b .`) — flaky `std::bad_alloc`, sometimes
  `tsc internal error: vfs: path "<garbage>" is not absolute` then exit 2.
- **Root cause:** `cpp/internal/execute/build/orchestrator.cpp` `createBuildTasks`
  captured the range-for loop variable **by reference** into the
  `workGroup::Queue` closure:
  `wg->Queue([this, &config, oldTasks, wg](){ ... toPath(config) ... })`.
  Workers run after the loop advances (and after it returns for the tail
  element), so every queued task raced on a re-seated/freed string —
  Go's per-iteration loop-variable semantics were lost in the port
  (`tsc/internal/execute/build/orchestrator.go:174`).
- **Fix:** capture `config` by value (`[this, config, oldTasks, wg]`). All
  `-b` scenarios then passed, including byte-identical tsbuildinfo.

### 2. Config-file parse diagnostics silently dropped

- **Repro:** `err-config-unknown-option` (TS5023), `err-config-bad-value`
  (TS6046), `err-empty-inputs` (TS18002), `err-extends-missing` (TS5083).
  Go prints the diagnostic (with `tsconfig.json(L,C)` positions) and exits 2;
  C++ printed nothing and exited 0.
- **Root cause:** `SimpleProgram::GetConfigFileParsingDiagnostics()`
  (`cpp/internal/compiler/program.h`) returned a
  `configFileParsingDiagnostics` field that is only populated by the
  `UpdateProgram` reuse splice — never for a fresh `NewProgram`. Go reads it
  live: `p.opts.Config.GetConfigFileParsingDiagnostics()`
  (`tsc/internal/compiler/program.go:532`,
  `tsc/internal/tsoptions/parsedcommandline.go:393`).
- **Fix:** delegate to `commandLine_->GetConfigFileParsingDiagnostics()`;
  field kept as fallback for the reuse path.

### 3. Config-attached option diagnostics lost their `tsconfig.json(L,C)` location

- **Repro:** TS5011 (removed-option/baseUrl suggestion), rootDir messages —
  Go prints `tsconfig.json(4,19): error TS6046: ...` plus the
  `Visit https://aka.ms/ts6` chain; C++ printed the bare message.
- **Root cause:** `SimpleProgram::verifyCompilerOptions()`
  (`cpp/internal/compiler/program.cpp`, port of
  `tsc/internal/compiler/program.go:866+`) had stubbed the whole family of
  lambdas that attach diagnostics to their config-file nodes
  (`getCompilerOptionsPropertySyntax`,
  `createOptionDiagnosticInObjectLiteralSyntax`,
  `forEachOptionPathsSyntax`, `createDiagnosticForOptionPathKeyValue`,
  baseUrl removed-option `"paths"` suggestion).
- **Fix:** ported program.go:883-945 + 1097-1134 using existing
  `tsoptions::ForEachTsConfigPropArray`, `ForEachPropertyAssignment`,
  `createDiagnosticForNodeInSourceFile`, and `json::marshalString` for the
  `paths` suggestion text.

### 4. Wrong file-inclusion reason text: "Root file specified for compilation"

- **Repro:** `err-files-missing` (expected `Part of 'files' list in
  tsconfig.json`), `err-rootdir`/`extends-chain` (expected `Matched by
  default include pattern '**/*'`).
- **Root cause:** `FileIncludeReason::computeDiagnostic`
  (`cpp/internal/compiler/includeprocessor.cpp`, `RootFile` case) was a stub
  that always emitted `Root_file_specified_for_compilation`. Go consults
  `config.GetMatchedFileSpec` / `GetMatchedIncludeSpec` keyed by the
  reason's root-file index (`tsc/internal/compiler/fileInclude.go:174-191`).
- **Fix:** ported that branch using the already-ported
  `ParsedCommandLine::GetMatchedFileSpec/GetMatchedIncludeSpec`.

### 5. `jsx` emit crash: `Unhandled case in Node::text`

- **Repro:** `jsx-react` — Go emits; C++ printed `tsc internal error:
  Unhandled case in Node::text` and emitted nothing.
- **Root cause:** `parseIsolatedEntityName` (`cpp/internal/parser/parser.cpp`)
  returns an AST allocated in the pooled parser's arena, then `putParser()`
  destroys/reconstructs the parser — the returned node dangles (manifesting
  as a garbage `kind` → unreachable). `parseSourceFile` avoids this by
  handing `factory.arena()` to the `SourceFile`; the isolated-entity-name
  path has no `SourceFile` to own it. Crash site: entity name from
  `Checker::getJsxFactoryEntity` → `createJsxFactoryExpressionFromEntityName`
  (`cpp/internal/transformers/jsxtransforms/jsxtransforms.cpp:947`).
- **Fix:** retain the parser's arena in a thread-local arena list
  (`isolatedEntityNameArenas`) before `putParser` — tiny, rare allocations
  (one per jsx factory/fragment entity name).

### 6. `importHelpers` emit order: `const tslib_1 = require("tslib")` too early

- **Repro:** `importhelpers` — Go emits the helpers import after
  `__esModule` and `exports.<f> = <f>;`; C++ put it right after
  `"use strict"`.
- **Root cause:** inverted predicate in `NodeFactory::splitCustomPrologue`
  (`cpp/internal/printer/factory.cpp:498`): Go stops at
  `ast.IsPrologueDirective(stmt) || flags&EFCustomPrologue == 0`
  (`tsc/internal/printer/factory.go:472`), but the port wrote
  `!isPrologueDirective(...) || ...` — so custom-prologue statements were
  never recognized and the helpers import landed at the front of the file.
- **Fix:** removed the spurious `!`.

### 7. Diagnostic message texts double-escaped (`\"`, `\\`)

- **Repro:** `build-noncomposite` — Go prints `must have setting "composite":
  true.`; C++ printed `must have setting \"composite\": true.`
- **Root cause:** `cxxstr` in `cpp/tools/gencpp.py` ran a pre-escaping pass
  (`replace("\\","\\\\")`, `replace('"','\\"')`) before the byte-wise
  escaping loop, so quotes became literal `\"` and single backslashes
  became `\\` in `cpp/internal/diagnostics/messages_generated.h` — 34
  affected messages (TS1202, TS1443, TS1458-1460, TS1480-1483, TS1510-1531
  regex escapes, TS6306, help texts, ...).
- **Fix:** removed the pre-escaping pass; the byte loop already handles
  `"`, `\`, `\n`, `\t`, `\r`; switched the `\xNN` fallback to 3-digit octal
  (`\x0d` followed by an ASCII hex char would merge in a C++ literal). Ran
  `python3 cpp/tools/gencpp.py diagnostics` to regenerate the header.

### 8. Build fix (unrelated to parity): `testrunner_deps.cpp`

`combinePaths(dir, "tsconfig.json")` didn't compile — the second parameter
is `const std::vector<std::string_view>&`; wrapped in `{}`. Required to get
`libtsc.a` built at all.

## Known limitation on this branch

`cpp/build/tscpp` does not link (~18 `tsc::api::*` / `tsc::ls::*` symbols
from an in-flight slice). All runs used the probe binary
(`/tmp/tscpp_probe` = `main.cpp.o + sys.cpp.o + stub(runLSP,runAPI) +
libtsc.a -lz`), which is the real `tsc` CLI path
(`tsc::execute::CommandLine`).
