# Literal Pairing Audit 2 — complex pairing artifacts

Follow-up to the first `R"TS"` literal audit (which resynced 22 sites with dropped
blank lines). This pass pairwise-cleared the remaining sites that the simple 1:1
raw-string matcher could not pair: literals built via Go `+` concatenation,
`[]string{...}` expression lists, `strings.Join`, `map[string]*string`, and
C++-side `std::string(R"TS(..") + "`" + std::string(R"TS(..")` backtick-concat
chains.

## Method

Both sides were flattened to an ordered event stream of string literals per test
and compared as a **per-test multiset** (field reordering in the port makes
ordered diffs useless):

- Go side (`litaudit/goextract2.go`): walks each `func TestXxx` AST, emitting
  `str`/`list`/`unres` events for every string literal, composite literal
  element, and call argument. `map[string]*string` emits values only (matches
  Go semantics — keys are arg names); `&[]string{}` emits an empty list;
  identifiers resolve through `const`/local bindings.
- C++ side (`litaudit/cppextract.py`): tokenizer + evaluator for
  `R"TS(..)TS"`, `".."` quoted strings, `+` concat, `std::vector<std::string>`,
  `std::make_shared<std::vector<..>>(..)`, `gostr::join`, brace-inits
  (`.Field =`, `{"k", v}` map entries → values only), lambda bodies
  (`withRecoverAndFail` treated as `defer RecoverAndFail`), and
  `const auto x = ..` bindings.
- Comparator: multiset of emitted strings per test (empty list → `<EMPTY>`).

Coverage: 4,565 Go tests paired → **4,311 exact multiset matches**. The 254
non-matching tests were each classified below.

## Result counts

| Bucket | Count |
|---|---|
| Real divergence → **fixed** | **2** |
| Faithful adaptation / extraction artifact | **252** |
| Unpairable after effort | **0** |

## (a) Real divergences — fixed

| Test | Divergence | Fix |
|---|---|---|
| `TestAutoImportCssModule` | Port had silently reduced the fixture: `/augmentations.ts` with only `declare module "./styles.css"`, `/index.ts` with one marker, one `VerifyCompletions`. Go has `/types/augmentations.ts` with three declares (`./styles.css`, `./styles`, `/types/rooted.css`), `/src/index.ts` with markers `/**/`, `/*noExtension*/`, `/*rooted*/`, and three `VerifyCompletions`. | Restored full content literal and both missing `VerifyCompletions` blocks in `cpp/internal/fourslash/tests/tests_c_autoimport2.cpp`. **Exposes a C++ runtime gap**: `declare module "./styles"` resolves to ModuleSpecifier `"../types/styles.css"` instead of Go's `"../types/styles"` (ambient extension-tolerant resolution bug — the test now carries `t->Skip` per repo convention with a comment). |
| `TestJSDocSnippetCompletionPreservesCRLF` | The test's entire purpose is CRLF preservation, but `R"TS(..)"` literals are LF-only: `content`, `NewLineCharacter`, and the expected `NewText` all silently dropped `\r`. | Replaced the three literals with escaped `"\r\n"` C++ strings in `cpp/internal/fourslash/tests/tests_d_jsdoc.cpp`. Test now passes and actually asserts CRLF. |

Also in this change: `cpp/internal/fourslash/fourslash.cpp` — the
`"ModuleSpecifier mismatch"` error message now prints actual/expected values
(diagnostic improvement used to pin the CssModule runtime gap).

## (b) Faithful adaptations / extraction artifacts — 252

### `nil`/`unres` ↔ `{}`/`vector` equivalence (204 sites)

Per-test multiset differed only by `<EMPTY>` (empty-list) entries. These are
Go `nil`, `&[]string{}`, `[]string{}`, or unresolvable-expression arguments
ported to `{}`, `std::vector<std::string>{}`, or
`std::make_shared<std::vector<std::string>>(vec)` — empty-sequence
equivalents on both sides. Spot-verified (e.g. `TestCompletionImportKeywordNoCrash`:
Go `emptyCommitChars := []string{}` → `std::vector<std::string>{}`; semantics
identical). Full list: `litaudit/eonly.json` (see artifact files).

### `*_js` GOOS-gated note literals (14 sites)

`TestAutoImportPackageJsonImports{Pattern,_ts,Pattern_ts}_js`,
`TestAutoImportPackageJsonImports_js`, `TestDocCommentTemplateFunctionWithParameters_js`,
`TestFindAllRefsJsDocTemplateTag_{class,function}_js`, `TestFindAllRefsJsDocTypeDef_js`,
`TestFindAllRefs_importType_js`, `TestImportCompletionsPackageJsonImports{Pattern,_ts,Pattern_ts}_js`,
`TestImportCompletionsPackageJsonImports_js`, `TestImportNameCodeFix_all_js`,
`TestImportStatementCompletions_js`, `TestJSDocSnippetCompletionForFunction`:
C++ bodies carry the port annotation `"Go *_js_test.go file: GOOS js-gated, never
compiled on this platform"` — intentional, no Go counterpart exists.

### Go-side strings present but in unextractable position (34 sites)

For each site the Go-only (or C++-only) strings were verified **present** in
the counterpart source; the multiset diff is purely an extraction-position
artifact of one of these shapes:

| Test(s) | Artifact shape |
|---|---|
| `TestAllowRenameOfImportPath`, `TestContentMapperCompletions`, `TestContentMapperModulePathCompletions`, `TestRenameForDefaultExport01/02/03`, `TestRenameImportOfReExport2`, `TestRenamePrivateAccessor`, `TestRenamePrivateMethod`, `TestJsdocTypedefTagRename03`, `TestRenameInCommonFile`, `TestJsdocTypedefTagRename03` | Strings live inside nested call args (`GetRangesByText()->Get("…")`, `VerifyBaselineRenameAtRangesWithText(t,nullptr,{…})`, `markers` vector bound then passed) — emitted on both sides, different multiset position/multiplicity. |
| `TestAutoImportSymlinkedMonorepoSourceUpdate`, `TestAutoImportTransitiveLeak` | Go inline literal factored into shared C++ constant `TestAutoImportSymlinkedMonorepoSourceUpdateScenario` (file-scope `static const std::string`, outside the test body). |
| `TestCompletionsObjectPropertyName_quotePreference{Double,SingleEscaping}` | Same: shared `objectPropertyNameContent` constant. |
| `TestFormatSelectionEditAtEndOfRange` | Go string `"remove"` → C++ enum `lsutil::SemicolonPreferenceRemove` (typed-preferences idiom). |
| `TestQuickInfoOnUnionPropertiesWithIdenticalJSDocComments01` | Go single literal containing `` ` `` backticks → C++ `std::string(R"TS")+"`"+…` concat chain; concatenated content matches (same faithful pattern as `TestDocumentHighlightTemplateStrings`). |
| `TestGetOutliningSpansForTemplateLiteral`, `TestStringCompletionsVsEscaping` | Inverse: Go builds the literal via `` ` + "`" + ` `` concat; C++ has the joined form. |
| `TestLinkedEditingJsxTag1/2/4/5/6/7/9` | Go generates marker names programmatically (`fmt.Sprintf`/`itoa`); C++ port materialized them as literal names — extra digits/`Nstart`/`Nend` strings on C++ side only. |
| `TestOrganizeImports_sourcePhaseImportSorting` | Go table-driven slice of structs (non-string fields unres); C++ unrolled the cases with literal descriptions (`"case insensitive bindings"`, etc.). |
| `TestCompletionListInUnclosedTypeArguments`, `TestFindAllRefsDeclarationInOtherProject`, `TestImportHelpersAfterScriptBecomesDecoratedModule`, `TestAutoImportTypeOnlyPreferred1`, `TestCompletionsImport_named_didNotExistBefore`, `TestWorkspaceSymbolNewInferredProject`, `TestDeclarationMapsRename` | Strings verified present in the counterpart source (`.map` fixture heredoc, `f.MarkerByName(t,"insert")`, `Detail: new("…")` fields, `import "./e";` line, second `defer RecoverAndFail`); multiset position differs. |

## Verification

- `fourslashrunner -run 'TestAutoImportCssModule|TestJSDocSnippetCompletionPreservesCRLF'`:
  CRLF test PASS; CssModule SKIP (known-failing, documented runtime gap).
- `fourslashrunner -run 'TestAutoImport|TestJSDoc'`: **135/135 pass** (4 SKIP — pre-existing known-failures).
- `fourslashrunner -run 'TestFormat'`: **197/197 pass**.

## Follow-up flagged for parent session

`TestAutoImportCssModule` is faithful to Go but fails on a real runtime bug:
auto-import module-specifier resolution for ambient `declare module "./styles"`
in `/types/augmentations.ts` produces `"../types/styles.css"` (extension picked
up from the sibling `./styles.css` declare) instead of `"../types/styles"`.
Marked `t->Skip` with an explanatory comment — the autoImport resolver needs a
runtime fix before the skip can come off.
