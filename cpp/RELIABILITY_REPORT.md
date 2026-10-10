# Reliability report — `devin/cpp-rel`

Base: `devin/cpp-port` tip `be2eae305a`. Oracle pin: microsoft/TypeScript@`2d8450f9b5`.
Scope: hardening/reliability wave — no new porting. All behavior changes verified
byte-identical-equivalent vs the Go oracle (only dead/uncalled code removed).

## 1. `[[maybe_unused]]` helper audit

Every site flagged in `cpp/CLEANUP_AUDIT.md` was checked against `tsc/internal/`
(greped for the Go counterpart, including generic decls and differently-named
helpers). Result: **11 dead scaffolding functions deleted** across 5 files,
**9 file-level keeps annotated** with the Go function each ports.

### Deleted — dead port scaffolding (no Go counterpart needed, zero callers)

| File | Function(s) | Why deleted |
|---|---|---|
| `cpp/internal/lsp/lspwatcher/lspwatcher.cpp` | `stringsHasPrefix`, `stringsCut`, `stringsCutPrefix`, `stringsSplit`, `stringsJoin`, `urlPathEscape`, `extraEscapeReplace` (~98 lines) | C++-only string shims added during porting; Go lspwatcher uses `strings`/`net/url` directly. Zero callers. `stringsTrimRight`/`stringsContains` kept — used at :625/:658. |
| `cpp/internal/sourcemap/source_mapper.cpp` | `unmarshalRawSourceMap` (~32 lines) | Go inlines the member loop inside `tryParseRawSourceMap` (`source_mapper.go:331`); the C++ code already mirrors that inline loop. Helper was dead scaffolding. |
| `cpp/internal/testutil/harnessutil/harnessutil.cpp` | `cut(s, sep)` | Go harness uses `strings.CutPrefix`/`CutSuffix` which exist here; `cut` had no callers. Header comment corrected to name the real counterparts. |
| `cpp/internal/checker/checker_decltypes.cpp` | `decltypesDepUnreachable` (anon-ns) | Port scaffolding; unreachable instrumentation shim with no Go counterpart and no callers. |
| `cpp/internal/ls/change/tracker.cpp` | `collectCommentRanges` (anon-ns) | Dead duplicate — the live copy is `trackerimpl.cpp:32` (called at :456+). |

### Kept + annotated — faithful ports or intentional C++-only adapters

| File | Helper | Go counterpart / verdict |
|---|---|---|
| `cpp/internal/binder/binder.cpp` | `isSignedNumericLiteralBinder` | Ports `binder.go:2747` — dead in Go too (dead upstream); live body is `tsc::isSignedNumericLiteral` (`ast.cpp:1840`). Kept for parity, annotated. |
| `cpp/internal/format/rulecontext.cpp` | (flagged helper) | Ports `ast_generated.go:9862`. Annotated. |
| `cpp/internal/transformers/estransforms/esdecorator.cpp` | two helpers | `ast.utilities.go:587` (`IsObjectLiteralElement`) and `:151` (`IsArrayBindingOrAssignmentElement`). Annotated; also fixed a misplaced comment. |
| `cpp/internal/transformers/estransforms/classfields.cpp` | (flagged helper) | `utilities.go:140`. Annotated. |
| `cpp/internal/checker/checker_emitresolver.cpp` | two helpers | `ast/utilities.go:2670` (`GetDeclarationContainer`) and `:1286` (`WalkUpBindingElementsAndPatterns`). Annotated. |
| `cpp/internal/checker/checker_nodebuilder.cpp` | (flagged helper) | `ast/utilities.go:1907`. Annotated. |
| `cpp/internal/compiler/program.cpp` | `ScriptTarget` stringer | `core/scripttarget_stringer_generated.go:41` (`ScriptTarget.String`). Annotated. |
| `cpp/internal/tspath/typed_paths.cpp` | `asSv` et al. | C++-only adapters by design — no oracle counterpart exists; annotated to say so. |
| `cpp/internal/fourslash/tests/tests_c_{codefix2,autoimport2,quickinfo2,gotoimpl,statemaps,comments,formatting2,references,sighelp}.cpp` | `newContentMapperFourslash` | Ports `fourslash/tests/contentMapper_test.go` (9 files, same one-line comment). |

Audit caveat found during review: two audit entries were false positives —
`stringsSplit` in `api.cpp` is a different same-named live function, and
`collectCommentRanges` in `tracker.cpp` was dead only because the live twin
exists in `trackerimpl.cpp` (deleted anyway as a dup).

## 2. CI reliability (`.github/workflows/cpp.yml`)

Changes:

- Conformance job `timeout-minutes: 60` → `120` (successful runs ~46 min; one
  known timeout on a slow runner). Comment added noting the observed ~46 min.
- Added fail-closed guards after the `|| true` result-parsing steps:
  `test -s` on `parse_results.txt`, `bind_results.txt`, `check_results.txt`
  so a wiped/empty results file fails the job instead of silently passing.

Audited and **deliberately NOT touched**:

- `pipefail`: already present where pipes are used.
- `continue-on-error`: none present.
- Action versions: already pinned to commit SHAs.
- Cache keys: scoped to lockfiles/branch; no cross-branch poisoning vector found.

## 3. `nodes_generated.h` full regen eval — **NOT committed**

`cpp/tools/gencpp.py` `nodes` run into `/tmp/genscratch` against the pinned
oracle (with `tsc/` symlinked). 4 of 5 emitted outputs are byte-identical to
committed; only `nodes_generated.h` differs (96-line diff —
`/tmp/diff_nodes_generated.h.txt`). The diff is **not** clean codegen; it mixes:

1. **Oracle/type-map drift** — emits `tspath.RootedFilePath` literally and an
   `IsContentMapped` member that the committed file lacks: the generator's view
   of the oracle differs from the pin as materialized in the committed file.
2. **Lost hand-written case** — regen emits `return nullptr` for the
   `SourceFile` `deepClone` case that the committed file carries by design.
3. **Precedence-parens regressions** and constness churn — cosmetic but
   unrelated to the `:?=` fix.

Verdict: regen mixes the fixed codegen with unrelated oracle drift and drops a
hand-written special case. Per the "NEVER hand-mix a partial regen" rule, it was
documented here and **not committed**. To make regen reproducible, `gencpp.py`
needs its type map/extra_members updated for `RootedFilePath`/`IsContentMapped`
and a `SourceFile` deepClone override — left for a dedicated generator-sync pass.

## 4. Tooling hardening (`cpp/tools/`)

All scripts pass `bash -n`; the harnesses run under `set -u`, the
corpus-list pipelines and the fourslash/tsctest CI steps are
`pipefail`-protected, and per-worker exit status is propagated (xargs
status → script exit) so upstream failures fail closed.

| Script | Fix |
|---|---|
| `conformance_bind.sh`, `conformance_check.sh` | Fixed `/tmp/<name>_$ID.txt` paths → `mktemp "${TMPDIR:-/tmp}/...${ID}.XXXXXX"` for `go_out`/`cpp_out`; `cmp -s` now uses the temp paths; dumps `rm -f`'d on PASS, paths echoed on FAIL. Removes stale-file collision and mid-run wipe reliance on fixed names. |
| `conformance_parse.sh` | Same: `mktemp` for `cppout`/`goout`. |
| `conformance_corpus.sh` | Fail-closed: exits 2 when `RESULTDIR/tscpp_results.txt` is missing/empty instead of silently comparing nothing. |
| `project_parity.sh` | `TIMEOUT_BIN` detection (`timeout` → `gtimeout` → none; runs unbounded if absent), exported for `run_one`; removed dead `verdicts=()`. |

Note: scripts still place temps under `${TMPDIR:-/tmp}` (correct — mktemp files
are per-invocation, not relied on for persistence). The earlier /tmp wipe
incident came from *fixed-name* files surviving between runs, which this fixes.

## 5. Verification

Rebuild green: `tscpp`, `unittestrunner`, `tsctestrunner`, `fourslashrunner`
all linked (zig 0.16.0, RelWithDebInfo).

| Suite | Result |
|---|---|
| unittestrunner | **1122/1122 pass** |
| tsctestrunner | **117/117 pass, 0 fail, 0 skip** |
| fourslashrunner | **4133 pass, 12 FAIL, 428 faithful skips** |

The 12 fourslash FAILs (`TestFormatAfterWhitespace`, `TestFormat{NoSpace,Space}AfterTemplateHeadAndMiddle`, `TestFormatSelectionWithTrivia2`, `TestGoToImplementation{InterfaceProperty_00,Namespace_06}`, `TestQuickInfo{CommentsCommentParsing,ForJSDocWithHttpLinks,InheritDoc}`, `TestSignatureHelpCommentsCommentParsing{,VS}`, `TestGoToDefinitionSameFile`) were **reproduced identically on the unmodified base** (stashed this wave, rebuilt, `-run` filtered): pre-existing on `be2eae305a` in this environment, not regressions — most show invisible whitespace/env-sensitive diffs (e.g. JSDoc comment-parsing baselines). No suite regressed; no `tscUnreachable` (exit-2) crashes observed.
