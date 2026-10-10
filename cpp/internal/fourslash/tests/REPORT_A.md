# Fourslash test batch A — runner + 93 ported tests

## Summary

- Registered tests: **93** across 11 `tests_*.cpp` files (auto-import/import-fix,
  completions, find-all-refs, formatting, go-to-definition, document-highlights,
  outlining, semantic-classification, navigation/navto, occurrences, quickinfo,
  rename, signature-help — 14 feature areas).
- C++ runner: **91 PASS / 2 SKIP** (`91/91 pass` excluding skips).
- Go oracle (`go test ./tsc/internal/fourslash/tests/`): **91 PASS / 2 SKIP**.
- Outcome parity: **93/93 = 100%**. The 2 SKIPs are Go's own
  `t.Skip("Known failing fourslash test")` — identical in C++.
  (Earlier batch write-ups below describe the intermediate 87-PASS state
  before the remaining divergences were fixed.)

## Per-test outcomes

### PASS (87 — match Go)

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
TestFindAllRefsNoSubstitutionTemplateLiteralNoCrash1,
TestFindAllReferencesFilteringMappedTypeProperty, TestFindAllReferencesImportMeta,
TestFindAllReferencesUndefined

Formatting: TestFormattingHexLiteral, TestFormattingConditionalOperator,
TestFormattingDoubleLessThan, TestFormattingEqualsBeforeBracketInTypeAlias,
TestFormattingForIn, TestFormattingForOfKeyword, TestFormattingKeywordAsIdentifier,
TestFormattingOnSemiColon

GoToDefinition: TestGoToDefinitionAmbiants, TestGoToDefinitionAlias,
TestGoToDefinitionExternalModuleName2, TestGoToDefinitionDifferentFile,
TestGoToDefinitionBuiltInTypes, TestDefinition01, TestDefinitionNameOnEnumMember

DocumentHighlights: TestDocumentHighlights01, TestDocumentHighlights02,
TestDocumentHighlightsExportEqualsInMergedNamespace

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
TestQuickInfoCircularInstantiationExpression,
TestQuickInfoDisplayPartsClassDefaultAnonymous,
TestQuickInfoDisplayPartsClassDefaultNamed, TestQuickInfoDisplayPartsClassIncomplete,
TestQuickInfoDisplayPartsTypeParameterInFunctionLikeInTypeAlias,
TestQuickInfoDisplayPartsTypeParameterInTypeAlias

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

### FAIL (0 remaining — all 8 divergences fixed; root causes below)

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

5. **`lsp/lsp_handlers.cpp` handleCompletionItemResolve double-owned the
   request CompletionItem → writer-thread UAF**
   `ResolveCompletionItem` returns the *same* `*CompletionItem` it was passed
   (every `getCompletionItemDetails` path returns `item`; Go relies on GC).
   The handler wrapped `r.first` (`params.get()`) in a fresh owning
   `shared_ptr`, creating a second control block over the request item. When
   the handler lambda's `{resp, rerr}`/`{params, err}` pairs destructed at
   lsp_server.cpp:1551, `params`' block deleted the CompletionItem while the
   queued response's `AnyValue::hold` still pointed at it → heap-use-after-free
   in `CompletionItem::marshalJSONTo` on the writer thread (TSan had earlier
   surfaced it inside `TextEditOrInsertReplaceEdit::marshalJSONTo`, since the
   item's AdditionalTextEdits marshal first). Fixed by aliasing `params`'
   ownership: `resp = shared_ptr<CompletionItem>(params, r.first)` — the
   response shares the request item's control block exactly like Go's GC
   keeps the shared object alive. → TestAutoImportTypedefMissingName,
   TestCompletionsImport_fromAmbientModule, TestCompletionsImportYieldExpression
   PASS. (Also removed a leaked `new autoimport::Fix` in
   `getCompletionItemDetails` — Go `&autoimport.Fix{}` is GC-managed; C++
   uses a stack object.)

6. **`ls/autoimport/registry.cpp` unique_ptr deleted a LogTree child still
   linked in its parent → UAF (surfaced as bad_alloc)**
   `updateIndexes` wrapped `logging::fork(logger, "Building node_modules
   indexes")` in a `unique_ptr` local. `Fork` already links the child into
   `logger->logs` as a `logEntry{child}` — the unique_ptr deleted the child
   when the function returned, leaving the parent `builderLogs` tree with a
   dangling `log->child`. `Session::adoptSnapshotChange` then called
   `newSnapshot->builderLogs->String()` → `writeLogsRecursive` recursed into
   the freed child (`vector<logEntry*>` gone, garbage `logEntry::message`
   length → huge `std::string` reserve → `std::bad_alloc` in RelWithDebInfo;
   ASan shows the UAF directly). Fixed: keep the raw `LogTree*` — Go's tree
   children are GC-owned by the parent, matching every other `logging::fork`
   call site. → TestAutoImport_node12_node_modules1 PASS.

7. **TestFindAllRefsNoSubstitutionTemplateLiteralNoCrash1 — ported test content
   literalized Go's concatenation idiom** (`tests_findallrefs.cpp`).
   Go's `const content = \`type Test = ` + "`" + `T/*1*/` + "`" + `;\`` builds the
   content by string concatenation; the C++ port pasted the whole expression
   inside a raw string literal, so the marker plus the literal
   ` + "`" + ` fragments became part of the source text and `/*1*/` landed inside
   the backtick literal instead of after it. Fixed the raw string to the
   evaluated content (`type Test = \`T/*1*/\`;`).

8. **TestDocumentHighlightsExportEqualsInMergedNamespace — doc highlights used
   the `referenceUseReferences` wrapper** (`ls/documenthighlights.cpp`).
   `getSemanticDocumentHighlights` called the exported `GetReferencedSymbolsForNode`
   wrapper, which hardcodes `refOptions{use: referenceUseReferences}`; that ran
   `getAdjustedLocation`, which remaps the `export` keyword of `export = C` to
   `SkipOuterExpressions(parent.Expression())` → the `C` expression →
   references-for-`C` (`class [|C|]`, `namespace [|C|]`, `export = [|C|]`).
   Go calls `getReferencedSymbolsForNode` directly with `use: referenceUseNone`
   (no adjustment): the `export` keyword resolves to the `export=` symbol
   (`parent->symbol()` on the ExportAssignment), which takes the
   `InternalSymbolNameExportEquals` → `getReferencedSymbolsForModule(C)` path →
   `namespace [|C|]` + `[|export|]`. Fixed to call `getReferencedSymbolsForNode`
   with `refOptions{use: referenceUseNone}` — dep-stub comment removed.

9. **TestQuickInfoCircularInstantiationExpression — `createElidedInformationPlaceholder`
   dropped the NoTruncation-off branch** (`checker/checker_nodebuilder.cpp`).
   The C++ port unconditionally returned `any` + `/*elided*/` comment (the
   comment is stripped by the removeComments printer → `any`). Go first does
   `approximateLength += 3`, then returns `NewTypeReferenceNode("...")` when
   `FlagsNoTruncation` is clear — `...` is what quickinfo shows for elided
   circular types. Restored the Go body verbatim.

10. **TestQuickInfoDisplayPartsTypeParameterInTypeAlias — `checker::Program` had
   a non-const `IsSourceFileDefaultLibrary` stub** (`checker/checker.h`).
   `checker::Program` declared two overloads: non-const
   `IsSourceFileDefaultLibrary(const std::string&)` with a `return false` stub
   and the const version that `SimpleProgram` overrides (`tspath::Path` IS
   `std::string`, so signatures match). The checker's `program` field is a
   non-const `Program*`, so every call bound to the non-const stub → always
   false → `IsLibSymbolForHoverVerbosity`/`IsLibTypeForHoverVerbosity` never
   recognized lib symbols → `shouldExpandType(T[])` treated `Array` as
   expandable → `canIncreaseExpansionDepth` → `vc.CanIncreaseVerbosity` on the
   `type List<T> = T[]` hover. In Go the single method hits the real
   `libFiles` check. Fixed: the non-const overload delegates to the const
   virtual (also fixes the same suppression check in `checker_relater.cpp`).

## Open divergences (none — all 8 batch-A divergences fixed)


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
