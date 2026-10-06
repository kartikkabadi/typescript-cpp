# Fourslash test batch A — runner + 93 ported tests

## Summary

- Registered tests: **93** across 11 `tests_*.cpp` files (auto-import/import-fix,
  completions, find-all-refs, formatting, go-to-definition, document-highlights,
  outlining, semantic-classification, navigation/navto, occurrences, quickinfo,
  rename, signature-help — 14 feature areas).
- C++ runner: **83 PASS / 8 FAIL / 2 SKIP** (`83/91 pass` excluding skips).
- Go oracle (`go test ./tsc/internal/fourslash/tests/`): **91 PASS / 2 SKIP**.
- Outcome parity: **85/93 ≈ 91.4%** (≥90% bar met). The 2 SKIPs are Go's own
  `t.Skip("Known failing fourslash test")` — identical in C++.

## Per-test outcomes

### PASS (83 — match Go)

Auto-import / import-fix: TestAutoImportPackageJsonImportsLength1,
TestAutoImportPackageJsonImportsPattern, TestAutoImportPackageJsonImports_capsInPath1,
TestImportFixWithMultipleModuleExportAssignment, TestImportFixes_ambientCircularDefaultCrash

Completions: TestCompletions01, TestCompletionsBigIntShowNoCompletions,
TestCompletionsAfterAsyncInObjectLiteral, TestCompletionsNonExistentImport,
TestCompletionsDiscriminatedUnion, TestCompletionsOptionalMethod,
TestCompletionsUnion, TestCompletionsWithGenericStringLiteral,
TestCompletionsRecursiveNamespace

FindAllRefs: TestFindAllRefsBadImport, TestFindAllRefsDefinition,
TestFindAllRefsEnumAsNamespace, TestFindAllRefsEnumMember,
TestFindAllRefsForStringLiteralTypes, TestFindAllRefsImportEquals,
TestFindAllReferencesFilteringMappedTypeProperty, TestFindAllReferencesImportMeta,
TestFindAllReferencesUndefined

Formatting: TestFormattingHexLiteral, TestFormattingConditionalOperator,
TestFormattingDoubleLessThan, TestFormattingEqualsBeforeBracketInTypeAlias,
TestFormattingForIn, TestFormattingForOfKeyword, TestFormattingKeywordAsIdentifier,
TestFormattingOnSemiColon

GoToDefinition: TestGoToDefinitionAmbiants, TestGoToDefinitionAlias,
TestGoToDefinitionExternalModuleName2, TestGoToDefinitionDifferentFile,
TestGoToDefinitionBuiltInTypes, TestDefinition01, TestDefinitionNameOnEnumMember

DocumentHighlights: TestDocumentHighlights01, TestDocumentHighlights02

Outlining: TestOutliningSpansForFunction, TestOutliningSpansForArrowFunctionBody,
TestOutliningSpansForImportsAndExports

SemanticClassification: TestSemanticClassification1, TestSemanticClassification2,
TestSemanticClassificationAlias

Navigation: TestNavigationItemsExportDefaultExpression,
TestNavigationItemsExportEqualsExpression, TestNavigationItemsExportDefaultExpression2,
TestNavto_emptyPattern, TestNavto_excludeLib1

Occurrences: TestGetOccurrencesIsDefinitionOfArrowFunction,
TestGetOccurrencesIsDefinitionOfBindingPattern,
TestGetOccurrencesIsDefinitionOfNumberNamedProperty,
TestGetOccurrencesIsDefinitionOfStringNamedProperty,
TestGetOccurrencesIsDefinitionOfTypeAlias,
TestGetOccurrencesIsDefinitionOfFunction

QuickInfo: TestQuickInfoForConstDeclaration, TestQuickInfoForConstTypeReference,
TestQuickInfoForNamedTupleMember, TestQuickInfoFunctionCheckType,
TestQuickInfoNamedTupleMembers, TestQuickInfoRecursiveObjectLiteral,
TestQuickInfoDisplayPartsClassDefaultAnonymous,
TestQuickInfoDisplayPartsClassDefaultNamed, TestQuickInfoDisplayPartsClassIncomplete,
TestQuickInfoDisplayPartsTypeParameterInFunctionLikeInTypeAlias

Rename: TestRenameAlias, TestRenameAlias2, TestRenameAlias3,
TestRenameImportAndExport, TestRenameImportAndShorthand,
TestRenameImportNamespaceAndShorthand, TestRenameModuleExportsProperties2,
TestRenameNumericalIndex

SignatureHelp: TestSignatureHelpWithUnknown, TestSignatureHelpNegativeTests2,
TestSignatureHelpOnDeclaration, TestSignatureHelpSkippedArgs1,
TestSignatureHelpAnonymousType, TestSignatureHelpInference,
TestSignatureHelpNegativeTests, TestSignatureHelpOptionalCall

### SKIP (2 — match Go's own skips)

- TestCompletionsBeforeRestArg1 — `t.Skip("Known failing fourslash test")` in Go too.
- TestCompletionsImport_noSemicolons — same Go-level skip.

### FAIL (8 — all PASS in Go; divergences below)

## Divergences found and fixed

1. **`ls/symbols.cpp` mergeExpandos null-deref (Go range-copy semantics)**
   `auto& symbol = symbols[i]` aliased the slot; when the merge wrote
   `symbols[i] = nullptr`, later reads of `symbol` were a dangling ref → SEGV.
   Go `for i, x := range slice` copies each element. Fixed: `auto symbol = symbols[i]`.
   Also fixed disengaged-optional `Children` derefs in `mergeExpandos`/`mergeChildren`
   (Go nil-slice vs `shared_ptr<optional<vector>>` — must check `has_value()`).
   → TestNavigationItemsExportDefaultExpression2 PASS.

2. **`printer/syntheticfile.cpp` PrintAndPositionNode was a TSC_UNREACHABLE stub**
   Blocked all 8 auto-import tests ("changetrackerwriter slice" skip message).
   Implemented the faithful port: ChangeTrackerWriter + PrinterOptions{NewLine,
   NeverAsciiEscape, PreserveSourceNewlines, TerminateUnterminatedLiterals} +
   NewPrinter->Write + TrimSuffix(newLine) + AssignPositionsToNode.

3. **`printer/changetrackerwriter.cpp` assignPositionsToNodeArray infinite recursion**
   Called `v->visitNodesHooked(nodes)` which re-entered the same hook forever
   (stack-overflow). Go `v.VisitNodes(nodes)` is the raw element iteration; the
   hooked dispatch lives on the unexported `visitNodes`. Fixed: `v->visitNodes(nodes)`.
   → 4/8 auto-import tests PASS + TestGoToDefinitionBuiltInTypes PASS.

4. **`checker.cpp` symbolToStringEx interim stub → real printer.go:132 port**
   Stub returned the raw `symbol->name` (`"default"`) instead of running the
   nodebuilder+printer pipeline. Implemented the faithful port in
   `checker_printer.cpp`: builds nodeFlags/internalNodeFlags from
   SymbolFormatFlags, picks SymbolToNode (AllowAnyNodeKind) vs SymbolToEntityName,
   writes via the removeCommentsOmitTrailingSemicolon[NeverAsciiEscape] printer
   into a pooled single-line writer. → TestQuickInfoDisplayPartsClassDefaultNamed
   PASS (`class default` → `class C`).

## Open divergences (8 FAILs)

- **TestAutoImport_node12_node_modules1** — `bad_alloc` thrown inside
  `Printer::Write` while printing the large synthesized import node
  (PrintAndPositionNode path). Likely an unbounded structure or cycle in the
  synthesized AST; needs node-level tracing.
- **TestAutoImportTypedefMissingName, TestCompletionsImport_fromAmbientModule,
  TestCompletionsImportYieldExpression** — SEGV *after* PrintAndPositionNode
  completes. TSan traced one to a wild `shared_ptr` inside
  `TextEditOrInsertReplaceEdit::marshalJSONTo` → `tryField(shared_ptr<TextEdit>)`
  while marshalling a completion item's TextEdit produced by
  `importAdder->Edits()`. Suspected GC-lifetime vs `shared_ptr` ownership bug in
  the importAdder→completionItem edit path (edits outliving their producer).
- **TestFindAllRefsNoSubstitutionTemplateLiteralNoCrash1** — baseline span diff:
  the `/*FIND ALL REFS*/` marker lands inside the backtick literal instead of
  after it; result-range span mapping.
- **TestDocumentHighlightsExportEqualsInMergedNamespace** — baseline span diff:
  `[|C|]`/`export = [|C|]` highlight spans map onto the declaration instead of
  the export-assignment usage; document-highlight definition/span mapping.
- **TestQuickInfoCircularInstantiationExpression** — baseline diff
  `(t: string) => any` (C++) vs `=> ...` (Go). The elision placeholder path
  (`createElidedInformationPlaceholder`) emitted `any`; Go truncates to `...`.
  checker VC/flag propagation in the circular-instantiation path �� unresolved.
- **TestQuickInfoDisplayPartsTypeParameterInTypeAlias** — C++ emits extra
  `"canIncreaseVerbosity": true` on the `List`/`List2` alias-name hover items
  where Go leaves it unset. `NodeBuilder::shouldExpandType` /
  `checkTypeExpandability` are ported 1:1 and `canExpandSymbol` correctly
  excludes TypeAlias symbols, so the flag is set inside the nodebuilder
  alias-serialization path in a case Go does not reach — unresolved.

## Skipped candidates

Tests that would need unported helpers/features were not ported (the 93 above
are all that were selected). Tests using `Verify.*` methods absent from
fourslash.h or helpers absent from tests/util were left for a later batch.

## Harness notes

- `gostd::testing::T` is per-test fresh; `T::Run` propagates child
  skipped_/failed_. The runner forks a child per test (180 s alarm) and maps
  exit 0/1/2 → PASS/FAIL/SKIP, so a crash in one test can't take down the suite.
- `testutil::withRecoverAndFail` wraps each body so a C++ exception /
  TSC_UNREACHABLE marks the test failed via `t->Errorf` — mirrors
  `defer testutil.RecoverAndFail(t, "...")`.
- `t.Parallel()` is omitted by design (runner serializes tests).
