# Fourslash test batch B — 761 ported tests

## Summary

- Registered batch-B tests: **761** across 13 new `tests_*.cpp` files:
  call-hierarchy (38), inlay-hints (64), smart-selection (36),
  linked-editing (12), get-edits-for-file-rename (33), organize-imports (90),
  refactors (4), go-to-source-definition (71), quick-info display-parts (41),
  code-fixes (275), import-fixes (10), tsx (64), jsx (23).
- Whole suite now: **854** registered (93 batch A + 761 batch B) —
  `537 PASS / 181 FAIL / 136 SKIP`. Batch-A results unchanged (83 pass, 8 fail,
  2 skip — the same failures documented in REPORT_A.md, no regressions).
- Batch-B runner: **454 PASS / 173 FAIL / 134 SKIP**
  (of the 134 skips: 52 are Go's own `t.Skip("Known failing fourslash test")`
  ported verbatim; **82 are port-side stubs** — the `newMissingMemberFixer`
  dependency at `ls/lsdeps.cpp` throws `"tsc internal error: newMissingMemberFixer
  — ls/codeactions_missingmemberfixer slice"`; tests hitting it report SKIP.
  These pass in Go — they are divergences, not parity-skips.)
- Outcome parity vs Go oracle: **506/761 ≈ 66.5%**
  (454 pass + 52 matching Go skips). 255 divergent: 173 FAIL + 82 stub-SKIP.
- Five Go tests were not ported (needed infra that does not exist on the C++
  side); see "Conversion skips" at the bottom.

## Per-file outcomes

| `tests_*.cpp` | ported | PASS | FAIL | SKIP(go) | SKIP(stub) |
|---|---|---|---|---|---|
| callhierarchy | 38 | 10 | 28 | 0 | 0 |
| inlayhints | 64 | 51 | 13 | 0 | 0 |
| smartselection | 36 | 34 | 2 | 0 | 0 |
| linkediting | 12 | 12 | 0 | 0 | 0 |
| getedits | 33 | 29 | 3 | 1 | 0 |
| organizeimports | 90 | 0 | 90 | 0 | 0 |
| refactor | 4 | 3 | 0 | 1 | 0 |
| gotosourcedef | 71 | 71 | 0 | 0 | 0 |
| quickinfodp | 41 | 34 | 6 | 1 | 0 |
| codefix | 275 | 142 | 8 | 43 | 82 |
| importfix | 10 | 4 | 5 | 1 | 0 |
| tsx | 64 | 56 | 4 | 4 | 0 |
| jsx | 23 | 8 | 14 | 1 | 0 |
| **total** | **761** | **454** | **173** | **52** | **82** |

## Per-test outcomes

### `callhierarchy` — 10 pass / 28 fail / 0 skip

PASS: TestCallHierarchyAnonymousClassNoCrash1, TestCallHierarchyAnonymousClassNoCrash2, TestCallHierarchyAnonymousFunctionNoCrash1, TestCallHierarchyAnonymousFunctionNoCrash2, TestCallHierarchyInPropDeclarationOfExportedDefaultClass1, TestCallHierarchyIncomingCallsNoCrashArrayPush, TestCallHierarchyIncomingCallsObjectLiteralMethodInExpressionComputedProperty, TestCallHierarchyIncomingCallsObjectLiteralMethodInIdentifierComputedProperty, TestCallHierarchyIncomingCallsObjectLiteralMethodInStringLiteralComputedProperty, TestCallHierarchyUnclosedTemplateExprNoCrash1

FAIL:
- TestCallHierarchyAccessor — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyAccessor.callHierarchy.txt has changed. (Run
- TestCallHierarchyAnonymousClassNoCrash3 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyAnonymousClassNoCrash3.callHierarchy.txt has
- TestCallHierarchyAnonymousFunctionNoCrash3 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyAnonymousFunctionNoCrash3.callHierarchy.txt 
- TestCallHierarchyCallExpressionByConstNamedFunctionExpression — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyCallExpressionByConstNamedFunctionExpression
- TestCallHierarchyClassPropertyArrowFunction — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyClassPropertyArrowFunction.callHierarchy.txt
- TestCallHierarchyClassStaticBlock2 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyClassStaticBlock2.callHierarchy.txt has chan
- TestCallHierarchyClassStaticBlock — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyClassStaticBlock.callHierarchy.txt has chang
- TestCallHierarchyClass — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyClass.callHierarchy.txt has changed. (Run `h
- TestCallHierarchyConstNamedArrowFunction — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyConstNamedArrowFunction.callHierarchy.txt ha
- TestCallHierarchyConstNamedClassExpression — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyConstNamedClassExpression.callHierarchy.txt 
- TestCallHierarchyConstNamedFunctionExpression — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyConstNamedFunctionExpression.callHierarchy.t
- TestCallHierarchyContainerNameServer — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyContainerNameServer.callHierarchy.txt has ch
- TestCallHierarchyContainerName — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyContainerName.callHierarchy.txt has changed.
- TestCallHierarchyCrossFile — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyCrossFile.callHierarchy.txt has changed. (Ru
- TestCallHierarchyDecorator — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyDecorator.callHierarchy.txt has changed. (Ru
- TestCallHierarchyExportDefaultClass — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyExportDefaultClass.callHierarchy.txt has cha
- TestCallHierarchyExportDefaultFunction — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyExportDefaultFunction.callHierarchy.txt has 
- TestCallHierarchyExportEqualsFunction — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyExportEqualsFunction.callHierarchy.txt has c
- TestCallHierarchyFile — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyFile.callHierarchy.txt has changed. (Run `he
- TestCallHierarchyFunctionAmbiguity1 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyFunctionAmbiguity.1.callHierarchy.txt has ch
- TestCallHierarchyFunctionAmbiguity2 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyFunctionAmbiguity.2.callHierarchy.txt has ch
- TestCallHierarchyFunctionAmbiguity3 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyFunctionAmbiguity.3.callHierarchy.txt has ch
- TestCallHierarchyFunctionAmbiguity4 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyFunctionAmbiguity.4.callHierarchy.txt has ch
- TestCallHierarchyFunctionAmbiguity5 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyFunctionAmbiguity.5.callHierarchy.txt has ch
- TestCallHierarchyFunction — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyFunction.callHierarchy.txt has changed. (Run
- TestCallHierarchyInterfaceMethod — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyInterfaceMethod.callHierarchy.txt has change
- TestCallHierarchyJsxElement — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyJsxElement.callHierarchy.txt has changed. (R
- TestCallHierarchyTaggedTemplate — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/callHierarchy/callHierarchyTaggedTemplate.callHierarchy.txt has changed

### `inlayhints` — 51 pass / 13 fail / 0 skip

PASS: TestInlayHintsCrash1, TestInlayHintsEnumMemberValue, TestInlayHintsFunctionParameterTypes2, TestInlayHintsFunctionParameterTypes3, TestInlayHintsFunctionParameterTypes4, TestInlayHintsImportType1, TestInlayHintsImportType2, TestInlayHintsInferredTypePredicate1, TestInlayHintsInteractiveAnyParameter1, TestInlayHintsInteractiveAnyParameter2, TestInlayHintsInteractiveFunctionParameterTypes2, TestInlayHintsInteractiveFunctionParameterTypes3, TestInlayHintsInteractiveFunctionParameterTypes4, TestInlayHintsInteractiveFunctionParameterTypes5, TestInlayHintsInteractiveImportType1, TestInlayHintsInteractiveImportType2, TestInlayHintsInteractiveInferredTypePredicate1, TestInlayHintsInteractiveJsDocParameterNames, TestInlayHintsInteractiveMultifileFunctionCalls, TestInlayHintsInteractiveOverloadCall, TestInlayHintsInteractiveParameterNamesInSpan1, TestInlayHintsInteractiveParameterNamesInSpan2, TestInlayHintsInteractiveParameterNamesWithComments, TestInlayHintsInteractiveParameterNames, TestInlayHintsInteractiveRestParameters1, TestInlayHintsInteractiveRestParameters2, TestInlayHintsInteractiveRestParameters3, TestInlayHintsInteractiveReturnType, TestInlayHintsInteractiveTemplateLiteralTypes, TestInlayHintsInteractiveWithClosures, TestInlayHintsJsDocParameterNames, TestInlayHintsNoHintWhenArgumentMatchesName, TestInlayHintsNoParameterHints, TestInlayHintsNoVariableTypeHints, TestInlayHintsOverloadCall1, TestInlayHintsOverloadCall2, TestInlayHintsParameterNames, TestInlayHintsPropertyDeclarationComputedName1, TestInlayHintsPropertyDeclarations2, TestInlayHintsPropertyDeclarations, TestInlayHintsQuotePreference1, TestInlayHintsQuotePreference2, TestInlayHintsReparsedNodeCrash, TestInlayHintsRestParameters1, TestInlayHintsRestParameters2, TestInlayHintsReturnType, TestInlayHintsThisParameter, TestInlayHintsTupleTypeCrash, TestInlayHintsTypeMatchesName, TestInlayHintsVariableTypes1, TestInlayHintsWithClosures

FAIL:
- TestInlayHintsElementAccess — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsElementAccess.baseline has changed. (Run `hereby b
- TestInlayHintsFunctionParameterTypes1 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsFunctionParameterTypes1.baseline has changed. (Run
- TestInlayHintsFunctionParameterTypes5 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsFunctionParameterTypes5.baseline has changed. (Run
- TestInlayHintsIdentifierLocation — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsIdentifierLocation.baseline has changed. (Run `her
- TestInlayHintsInteractiveFunctionParameterTypes1 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsInteractiveFunctionParameterTypes1.baseline has ch
- TestInlayHintsInteractiveMultifile1 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsInteractiveMultifile1.baseline has changed. (Run `
- TestInlayHintsInteractiveVariableTypes1 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsInteractiveVariableTypes1.baseline has changed. (R
- TestInlayHintsInteractiveVariableTypes2 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsInteractiveVariableTypes2.baseline has changed. (R
- TestInlayHintsMultifile1 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsMultifile1.baseline has changed. (Run `hereby base
- TestInlayHintsTypeParameterModifiers1 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsTypeParameterModifiers1.baseline has changed. (Run
- TestInlayHintsUsing — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsUsing.baseline has changed. (Run `hereby baseline-
- TestInlayHintsVariableTypes2 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsVariableTypes2.baseline has changed. (Run `hereby 
- TestInlayHintsVariableTypes3 — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/inlayHints/inlayHintsVariableTypes3.baseline has changed. (Run `hereby 

### `smartselection` — 34 pass / 2 fail / 0 skip

PASS: TestSmartSelection_JSDocTags10, TestSmartSelection_JSDocTags11, TestSmartSelection_JSDocTags12, TestSmartSelection_JSDocTags13, TestSmartSelection_JSDocTags1, TestSmartSelection_JSDocTags2, TestSmartSelection_JSDocTags3, TestSmartSelection_JSDocTags4, TestSmartSelection_JSDocTags5, TestSmartSelection_JSDocTags6, TestSmartSelection_JSDocTags7, TestSmartSelection_JSDocTags8, TestSmartSelection_JSDocTags9, TestSmartSelection_JSDoc, TestSmartSelection_behindCaret, TestSmartSelection_bindingPatterns, TestSmartSelection_comment1, TestSmartSelection_comment2, TestSmartSelection_emptyRanges, TestSmartSelection_function1, TestSmartSelection_function2, TestSmartSelection_function3, TestSmartSelection_functionParams1, TestSmartSelection_functionParams2, TestSmartSelection_imports, TestSmartSelection_lastBlankLine, TestSmartSelection_loneVariableDeclaration, TestSmartSelection_objectTypes, TestSmartSelection_punctuationPriority, TestSmartSelection_simple1, TestSmartSelection_simple2, TestSmartSelection_stringLiteral, TestSmartSelection_templateStrings2, TestSmartSelection_templateStrings

FAIL:
- TestSmartSelection_complex — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/smartSelection/smartSelection_complex.baseline has changed. (Run `hereb
- TestSmartSelection_mappedTypes — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/smartSelection/smartSelection_mappedTypes.baseline has changed. (Run `h

### `linkediting` — 12 pass / 0 fail / 0 skip

PASS: TestLinkedEditingJsxTag10, TestLinkedEditingJsxTag11, TestLinkedEditingJsxTag12, TestLinkedEditingJsxTag1, TestLinkedEditingJsxTag2, TestLinkedEditingJsxTag3, TestLinkedEditingJsxTag4, TestLinkedEditingJsxTag5, TestLinkedEditingJsxTag6, TestLinkedEditingJsxTag7, TestLinkedEditingJsxTag8, TestLinkedEditingJsxTag9

### `getedits` — 29 pass / 3 fail / 1 skip

PASS: TestGetEditsForFileRename_ambientModule, TestGetEditsForFileRename_caseInsensitive, TestGetEditsForFileRename_casing, TestGetEditsForFileRename_directory_down, TestGetEditsForFileRename_directory_noUpdateNodeModulesImport, TestGetEditsForFileRename_directory, TestGetEditsForFileRename_directory_up, TestGetEditsForFileRename_jsExtension, TestGetEditsForFileRename_jsRename, TestGetEditsForFileRename_js_simple, TestGetEditsForFileRename_keepFileExtensions, TestGetEditsForFileRename_nodeModuleDirectoryCase, TestGetEditsForFileRename_notAffectedByJsFile, TestGetEditsForFileRename_preferences, TestGetEditsForFileRename_preservePathEnding, TestGetEditsForFileRename_renameFromIndex, TestGetEditsForFileRename_renameToIndex, TestGetEditsForFileRename_resolveJsonModule, TestGetEditsForFileRename_shortenRelativePaths, TestGetEditsForFileRename_subDir, TestGetEditsForFileRename_symlink, TestGetEditsForFileRename, TestGetEditsForFileRename_tsconfig_empty_include, TestGetEditsForFileRename_tsconfig_include_add, TestGetEditsForFileRename_tsconfig_include_noChange, TestGetEditsForFileRename_tsconfig, TestGetEditsForFileRename_unaffectedNonRelativePath, TestGetEditsForFileRename_unresolvableImport, TestGetEditsForFileRename_unresolvableNodeModule

FAIL:
- TestGetEditsForFileRenameWithSolutionConfigFile — [killed by signal 11]
- TestGetEditsForFileRename_cssImport2 — Expected script info for /app2.css, but got nil
- TestGetEditsForFileRename_cssImport3 — assertion failed: declare const css: { cookieBanner: string; }; export default css; (actual) != .cookie-banner { display: none; } (expected). File content after

SKIP (Go `t.Skip` parity): TestGetEditsForFileRename_amd

### `organizeimports` — 0 pass / 90 fail / 0 skip

FAIL:
- TestOrganizeImports10 — At position /module.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "diagnostics"
- TestOrganizeImports11 — At position /test.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "diagnostics"
- TestOrganizeImports12 — At position /test.js(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "diagnostics"
- TestOrganizeImports13 — At position /organizeImports13.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "di
- TestOrganizeImports14 — At position /b.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "diagnostics"
- TestOrganizeImports15 — At position /b.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "diagnostics"
- TestOrganizeImports16 — At position /organizeImports16.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "di
- TestOrganizeImports17 — At position /organizeImports17.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "di
- TestOrganizeImports18 — At position /test.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "diagnostics"
- TestOrganizeImports19 — At position /organizeImports19.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "di
- TestOrganizeImports1 — At position /organizeImports1.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "dia
- TestOrganizeImports20 — At position /organizeImports20.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "di
- TestOrganizeImports21 — At position /b.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "diagnostics"
- TestOrganizeImports22 — At position /organizeImports22.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "di
- TestOrganizeImports23 — At position /organizeImports23.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "di
- TestOrganizeImports2 — At position /organizeImports2.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "dia
- TestOrganizeImports3 — At position /organizeImports3.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "dia
- TestOrganizeImports4 — At position /organizeImports4.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "dia
- TestOrganizeImports5 — At position /organizeImports5.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "dia
- TestOrganizeImports6 — At position /organizeImports6.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "dia
- TestOrganizeImports7 — At position /organizeImports7.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "dia
- TestOrganizeImports8 — At position /organizeImports8.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "dia
- TestOrganizeImports9 — At position /organizeImports9.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "dia
- TestOrganizeImportsAttributes2 — At position /organizeImportsAttributes2.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for 
- TestOrganizeImportsAttributes3 — At position /organizeImportsAttributes3.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for 
- TestOrganizeImportsAttributes4 — At position /organizeImportsAttributes4.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for 
- TestOrganizeImportsAttributes — At position /organizeImportsAttributes.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for f
- TestOrganizeImportsGroup_CommentInNewline — At position /organizeImportsGroup_CommentInNewline.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not a
- TestOrganizeImportsGroup_MultiNewlines — At position /organizeImportsGroup_MultiNewlines.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allo
- TestOrganizeImportsGroup_MultilineCommentInNewline — At position /organizeImportsGroup_MultilineCommentInNewline.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value
- TestOrganizeImportsGroup_Newline — At position /organizeImportsGroup_Newline.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed fo
- TestOrganizeImportsPathsUnicode1 — At position /organizeImportsPathsUnicode1.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed fo
- TestOrganizeImportsPathsUnicode2 — At position /organizeImportsPathsUnicode2.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed fo
- TestOrganizeImportsPathsUnicode3 — At position /organizeImportsPathsUnicode3.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed fo
- TestOrganizeImportsPathsUnicode4 — At position /organizeImportsPathsUnicode4.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed fo
- TestOrganizeImportsReactJsxDev — At position /test.tsx(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "diagnostics"
- TestOrganizeImportsReactJsx — At position /test.tsx(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "diagnostics"
- TestOrganizeImports_Shebang_PreserveAndSort — At position /organizeImports_Shebang_PreserveAndSort.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not
- TestOrganizeImportsType10 — At position /organizeImportsType10.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field
- TestOrganizeImportsType11 — At position /organizeImportsType11.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field
- TestOrganizeImportsType1 — At position /organizeImportsType1.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field 
- TestOrganizeImportsType2 — At position /organizeImportsType2.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field 
- TestOrganizeImportsType3 — At position /organizeImportsType3.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field 
- TestOrganizeImportsType4 — At position /organizeImportsType4.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field 
- TestOrganizeImportsType5 — At position /organizeImportsType5.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field 
- TestOrganizeImportsType6 — At position /organizeImportsType6.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field 
- TestOrganizeImportsType7 — At position /organizeImportsType7.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field 
- TestOrganizeImportsType8 — At position /organizeImportsType8.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field 
- TestOrganizeImportsType9 — At position /organizeImportsType9.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field 
- TestOrganizeImportsUnicode1 — At position /organizeImportsUnicode1.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for fie
- TestOrganizeImportsUnicode2 — At position /organizeImportsUnicode2.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for fie
- TestOrganizeImportsUnicode3 — At position /organizeImportsUnicode3.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for fie
- TestOrganizeImportsUnicode4 — At position /organizeImportsUnicode4.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for fie
- TestOrganizeImportsWithTraceResolution1 — terminate called after throwing an instance of 'std::bad_function_call' what():  bad_function_call [killed by signal 6]
- TestOrganizeImports_coalesceExports_sortSpecifiersCaseInsensitive — At position /organizeImports_coalesceExports_sortSpecifiersCaseInsensitive.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidPar
- TestOrganizeImports_coalesceExports_combineNamespaceReExports — At position /organizeImports_coalesceExports_combineNamespaceReExports.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams:
- TestOrganizeImports_coalesceExports_combinePropertyExports — At position /organizeImports_coalesceExports_combinePropertyExports.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: nu
- TestOrganizeImports_coalesceExports_combinePropertyReExports — At position /organizeImports_coalesceExports_combinePropertyReExports.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: 
- TestOrganizeImports_coalesceExports_namespaceWithPropertyReExport — At position /organizeImports_coalesceExports_namespaceWithPropertyReExport.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidPar
- TestOrganizeImports_coalesceExports_combineMany — At position /organizeImports_coalesceExports_combineMany.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is
- TestOrganizeImports_coalesceExports_combineManyReExports — At position /organizeImports_coalesceExports_combineManyReExports.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null
- TestOrganizeImports_coalesceExports_keepTypeOnlySeparate — At position /organizeImports_coalesceExports_keepTypeOnlySeparate.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null
- TestOrganizeImports_coalesceExports_combineTypeOnly — At position /organizeImports_coalesceExports_combineTypeOnly.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null valu
- TestOrganizeImports_coalesceImports_sortSpecifiersCaseInsensitive — At position /organizeImports_coalesceImports_sortSpecifiersCaseInsensitive.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidPar
- TestOrganizeImports_coalesceImports_combineSideEffectOnly — At position /organizeImports_coalesceImports_combineSideEffectOnly.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: nul
- TestOrganizeImports_coalesceImports_combineNamespaceImportsNotMerged — At position /organizeImports_coalesceImports_combineNamespaceImportsNotMerged.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: Invalid
- TestOrganizeImports_coalesceImports_combineDefaultImports — At position /organizeImports_coalesceImports_combineDefaultImports.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: nul
- TestOrganizeImports_coalesceImports_combinePropertyImports — At position /organizeImports_coalesceImports_combinePropertyImports.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: nu
- TestOrganizeImports_coalesceImports_sideEffectWithNamespace — At position /organizeImports_coalesceImports_sideEffectWithNamespace.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: n
- TestOrganizeImports_coalesceImports_sideEffectWithDefault — At position /organizeImports_coalesceImports_sideEffectWithDefault.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: nul
- TestOrganizeImports_coalesceImports_sideEffectWithProperty — At position /organizeImports_coalesceImports_sideEffectWithProperty.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: nu
- TestOrganizeImports_coalesceImports_namespaceWithDefault — At position /organizeImports_coalesceImports_namespaceWithDefault.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null
- TestOrganizeImports_coalesceImports_namespaceWithProperty — At position /organizeImports_coalesceImports_namespaceWithProperty.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: nul
- TestOrganizeImports_coalesceImports_defaultWithProperty — At position /organizeImports_coalesceImports_defaultWithProperty.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null 
- TestOrganizeImports_coalesceImports_combineMany — At position /organizeImports_coalesceImports_combineMany.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is
- TestOrganizeImports_coalesceImports_twoNamespacesOneDefault — At position /organizeImports_coalesceImports_twoNamespacesOneDefault.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: n
- TestOrganizeImports_coalesceImports_typeOnlySeparate — At position /organizeImports_coalesceImports_typeOnlySeparate.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null val
- TestOrganizeImports_coalesceImports_typeOnlyKindsNotCombined — At position /organizeImports_coalesceImports_typeOnlyKindsNotCombined.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: 
- TestOrganizeImports_coalesceImports_sortSpecifiersTypeOnlyInline — At position /organizeImports_coalesceImports_sortSpecifiersTypeOnlyInline.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidPara
- TestOrganizeImports_dtsUnusedImportWithAugmentation — At position /styled-patch.d.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "diagn
- TestOrganizeImports_removeOnly — At position /organizeImports_removeOnly.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for 
- TestOrganizeImports_removeUnused_preservesMultiline — At position /organizeImports_removeUnused_preservesMultiline.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null valu
- TestOrganizeImports_removeUnused_preservesMultilineWithRemoval — At position /organizeImports_removeUnused_preservesMultilineWithRemoval.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams
- TestOrganizeImports_sortModuleSpecifiers_nonRelativeVsNonRelative — At position /organizeImports_sortModuleSpecifiers_nonRelativeVsNonRelative.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidPar
- TestOrganizeImports_sortModuleSpecifiers_relativeVsRelative — At position /organizeImports_sortModuleSpecifiers_relativeVsRelative.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: n
- TestOrganizeImports_sortModuleSpecifiers_relativeVsNonRelative — At position /organizeImports_sortModuleSpecifiers_relativeVsNonRelative.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams
- TestOrganizeImports_sortModuleSpecifiers_caseInsensitive — At position /organizeImports_sortModuleSpecifiers_caseInsensitive.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null
- TestOrganizeImports_sortModuleSpecifiers_caseInsensitiveReverse — At position /organizeImports_sortModuleSpecifiers_caseInsensitiveReverse.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParam
- TestOrganizeImports_importKindOrder — At position /main.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "diagnostics"
- TestOrganizeImports_importKindOrderMultipleModules — At position /main.ts(Ln 0, Col 0): textDocument/codeAction request returned error: [-32602]: InvalidParams: null value is not allowed for field "diagnostics"

### `refactor` — 3 pass / 0 fail / 1 skip

PASS: TestRefactorConvertToEsModule_module_node12, TestRefactorConvertToEsModule_module_nodenext, TestRefactorConvertToEsModule_notAtTopLevel

SKIP (Go `t.Skip` parity): TestRefactorConvertToEsModule_notInCommonjsProject

### `gotosourcedef` — 71 pass / 0 fail / 0 skip

PASS: TestGoToSourceAliasedImportAtUsageSite, TestGoToSourceAliasedImportAtUsageSiteNamespaceImport, TestGoToSourceAliasedImportWithPrecedingExports, TestGoToSourceReExportAliasWithPrecedingExports, TestGoToSourceNamedAndDefaultExport, TestGoToSourceDefaultImportNotFirstStatement, TestGoToSourceUnnamedDefaultExport, TestGoToSourceEmptyNamesEntryFallback, TestGoToSourceExportAssignmentDefault, TestGoToSourceExportAssignment, TestGoToSourceExportAssignmentExpression, TestGoToSourceDefaultImportUsageSiteChecker, TestGoToSourceDefaultImportReExportUsage, TestGoToSourceDefinitionEmptyJsFile, TestGoToSourceDefaultImportNoDefaultInJs, TestGoToSourceDefinitionExtensionlessMappedSource, TestGoToSourceRequireCall, TestGoToSourceDynamicImport, TestGoToSourceAliasedImportExport, TestGoToSourceAliasedImportSpecifier, TestGoToSourceCallThroughImport, TestGoToSourceCallbackParam, TestGoToSourceReExportNames, TestGoToSourceReExportModuleSpecifier, TestGoToSourceReExportedImplementation, TestGoToSourceImportFilteredByExternalDeclaration, TestGoToSourceDtsReExport, TestGoToSourceBarrelReExportChain, TestGoToSourceCJSReExportViaDefineProperty, TestGoToSourceMergedDeclarationDedup, TestGoToSourceNestedNodeModules, TestGoToSourceNestedScopeShadowing, TestGoToSourceNestedClassShadowing, TestGoToSourceFindImplementationNonNodeModules, TestGoToSourceAtTypesPackage, TestGoToSourcePackageIndexDts, TestGoToSourcePackageRootThenSubpath, TestGoToSourcePackageRootFallsBackToSubpath, TestGoToSourceSubpathNotIndex, TestGoToSourceAccessExpressionProperty, TestGoToSourcePropertyOfAlias, TestGoToSourceIndexSignatureProperty, TestGoToSourceMappedTypeProperty, TestGoToSourceCommonJSAliasPrefersDeclaration, TestGoToSourcePropertyAccessNoDeclaration, TestGoToSourcePropertyAccessDeepChain, TestGoToSourcePropertyAccessNamespaceImport, TestGoToSourceMappedTypePropertyWithMatch, TestGoToSourceNamespaceImportProperty, TestGoToSourceForwardedReExportChain, TestGoToSourceScopedPackage, TestGoToSourceScopedAtTypesPackage, TestGoToSourceDefinitionUnresolvedTripleSlash, TestGoToSourceReferenceTypesToJS, TestGoToSourceReferencePathToDts, TestGoToSourceDefinitionTypeOnlyImportFallsBackToDeclaration, TestGoToSourceDefinitionTypeOnlyUsageFallsBackToDeclaration, TestGoToSourceDefinitionValueImportStillWorks, TestGoToSourceFallbacksToDefinitionForInterface, TestGoToSourceTypeOnlySymbolFallback, TestGoToSourceForwardedNonConcreteMerge, TestGoToSourceNodeModulesWithTypes, TestGoToSourceLocalJsBesideDts, TestGoToSourceNonDeclarationFile, TestGoToSourceNoImplementationFile, TestGoToSourceDeclarationMapSourceMap, TestGoToSourceDeclarationMapFallback, TestGoToSourceNamedExportsSpecifier, TestGoToSourceTripleSlashReference, TestGoToSourceFallbackToModuleSpecifier, TestGoToSourceFilterPreferredFallbackAll

### `quickinfodp` — 34 pass / 6 fail / 1 skip

PASS: TestQuickInfoDisplayPartsArrowFunctionExpression, TestQuickInfoDisplayPartsClassAccessors, TestQuickInfoDisplayPartsClassAutoAccessors, TestQuickInfoDisplayPartsClassConstructor, TestQuickInfoDisplayPartsClassDefaultAnonymous, TestQuickInfoDisplayPartsClassDefaultNamed, TestQuickInfoDisplayPartsClassIncomplete, TestQuickInfoDisplayPartsClassMethod, TestQuickInfoDisplayPartsClassProperty, TestQuickInfoDisplayPartsClass, TestQuickInfoDisplayPartsConst, TestQuickInfoDisplayPartsEnum1, TestQuickInfoDisplayPartsEnum2, TestQuickInfoDisplayPartsEnum3, TestQuickInfoDisplayPartsEnum4, TestQuickInfoDisplayPartsExternalModuleAlias, TestQuickInfoDisplayPartsExternalModules, TestQuickInfoDisplayPartsFunctionExpression, TestQuickInfoDisplayPartsFunctionIncomplete, TestQuickInfoDisplayPartsFunction, TestQuickInfoDisplayPartsInterfaceMembers, TestQuickInfoDisplayPartsInterface, TestQuickInfoDisplayPartsInternalModuleAlias, TestQuickInfoDisplayPartsLet, TestQuickInfoDisplayPartsLiteralLikeNames01, TestQuickInfoDisplayPartsLocalFunction, TestQuickInfoDisplayPartsModules, TestQuickInfoDisplayPartsTypeAlias, TestQuickInfoDisplayPartsTypeParameterInClass, TestQuickInfoDisplayPartsTypeParameterInFunctionLikeInTypeAlias, TestQuickInfoDisplayPartsTypeParameterInFunction, TestQuickInfoDisplayPartsTypeParameterInInterface, TestQuickInfoDisplayPartsVarWithStringTypes01, TestQuickInfoDisplayPartsVar

FAIL:
- TestQuickInfoDisplayPartsClassMethodVS — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/vSQuickInfo/quickInfoDisplayPartsClassMethodVS.baseline has changed. (R
- TestQuickInfoDisplayPartsClassPropertyVS — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/vSQuickInfo/quickInfoDisplayPartsClassPropertyVS.baseline has changed. 
- TestQuickInfoDisplayPartsFunctionVS — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/vSQuickInfo/quickInfoDisplayPartsFunctionVS.baseline has changed. (Run 
- TestQuickInfoDisplayPartsParameters — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/quickInfo/quickInfoDisplayPartsParameters.baseline has changed. (Run `h
- TestQuickInfoDisplayPartsTypeParameterInTypeAlias — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/quickInfo/quickInfoDisplayPartsTypeParameterInTypeAlias.baseline has ch
- TestQuickInfoDisplayPartsUsing — the baseline file /home/ubuntu/typescript-cpp/tsc/testdata/baselines/reference/fourslash/quickInfo/quickInfoDisplayPartsUsing.baseline has changed. (Run `hereby

SKIP (Go `t.Skip` parity): TestQuickInfoDisplayPartsIife

### `codefix` — 142 pass / 8 fail / 125 skip

PASS: TestCodeFixAddConvertToUnknownForNonOverlappingTypes9, TestCodeFixAddMissingAttributes10, TestCodeFixAddMissingAttributes5, TestCodeFixAddMissingAttributes6, TestCodeFixAddMissingAwait_notAvailableWithoutPromise, TestCodeFixAddMissingAwait_topLevel, TestCodeFixAddMissingConstToArrayDestructuring3, TestCodeFixAddMissingConstToCommaSeparatedInitializer4, TestCodeFixAddMissingEnumMember13, TestCodeFixAddMissingFunctionDeclaration16, TestCodeFixAddMissingFunctionDeclaration19, TestCodeFixAddMissingFunctionDeclaration20, TestCodeFixAddMissingMember21, TestCodeFixAddMissingMember8, TestCodeFixAddMissingParam15, TestCodeFixAddOptionalParam14, TestCodeFixAddOptionalParam15, TestCodeFixAddOptionalParam18, TestCodeFixAddVoidToPromise5, TestCodeFixAddVoidToPromiseJS5, TestCodeFixAwaitInSyncFunction3, TestCodeFixAwaitInSyncFunction4, TestCodeFixAwaitShouldNotCrashIfNotInFunction, TestCodeFixCannotFindModule_suggestion_falsePositive, TestCodeFixClassExtendAbstractPrivateProperty, TestCodeFixClassImplementInterfaceDuplicateMember2, TestCodeFixClassImplementInterfaceIndexSignaturesNoFix, TestCodeFixClassImplementInterfaceMultipleImplementsIntersection2, TestCodeFixClassImplementInterfaceTypeParamInstantiation, TestCodeFixClassSuperMustPrecedeThisAccess_callWithThisInside, TestCodeFixConvertToMappedObjectType13, TestCodeFixConvertToMappedObjectType5, TestCodeFixConvertToTypeOnlyImport1, TestCodeFixConvertToTypeOnlyImport2, TestCodeFixConvertToTypeOnlyImport3, TestCodeFixCorrectReturnValue27, TestCodeFixCorrectReturnValue4, TestCodeFixCorrectReturnValue5, TestCodeFixCorrectReturnValue6, TestCodeFixExpectedComma03, TestCodeFixForgottenThisPropertyAccess04, TestCodeFixImplicitThis_ts_cantFixNonFunction, TestCodeFixImportNonExportedMember4, TestCodeFixImportNonExportedMember5, TestCodeFixImportNonTextualSpecifierText, TestCodeFixInferFromUsageBindingElement, TestCodeFixInferFromUsageCallbackParameter6, TestCodeFixInferFromUsageCallbackParameter7, TestCodeFixInferFromUsageInaccessibleTypes, TestCodeFixInferFromUsage_noCrashOnMissingParens, TestCodeFixMissingTypeAnnotationOnExports10, TestCodeFixMissingTypeAnnotationOnExports11, TestCodeFixMissingTypeAnnotationOnExports12, TestCodeFixMissingTypeAnnotationOnExports13, TestCodeFixMissingTypeAnnotationOnExports14, TestCodeFixMissingTypeAnnotationOnExports15, TestCodeFixMissingTypeAnnotationOnExports17_unique_symbol, TestCodeFixMissingTypeAnnotationOnExports18, TestCodeFixMissingTypeAnnotationOnExports19, TestCodeFixMissingTypeAnnotationOnExports20, TestCodeFixMissingTypeAnnotationOnExports21_params_and_return, TestCodeFixMissingTypeAnnotationOnExports22_formatting, TestCodeFixMissingTypeAnnotationOnExports24_heritage_formatting_2, TestCodeFixMissingTypeAnnotationOnExports26_fn_in_object_literal, TestCodeFixMissingTypeAnnotationOnExports27_non_exported_bidings, TestCodeFixMissingTypeAnnotationOnExports29_inline, TestCodeFixMissingTypeAnnotationOnExports2, TestCodeFixMissingTypeAnnotationOnExports32_inline_short_hand, TestCodeFixMissingTypeAnnotationOnExports33_methods, TestCodeFixMissingTypeAnnotationOnExports34_object_spread, TestCodeFixMissingTypeAnnotationOnExports35_variable_releative, TestCodeFixMissingTypeAnnotationOnExports36_conditional_releative, TestCodeFixMissingTypeAnnotationOnExports37_array_spread, TestCodeFixMissingTypeAnnotationOnExports38_unique_symbol_return, TestCodeFixMissingTypeAnnotationOnExports39_extract_arr_to_variable, TestCodeFixMissingTypeAnnotationOnExports3, TestCodeFixMissingTypeAnnotationOnExports40_extract_other_to_variable, TestCodeFixMissingTypeAnnotationOnExports41_no_computed_enum_members, TestCodeFixMissingTypeAnnotationOnExports42_static_readonly_class_symbol, TestCodeFixMissingTypeAnnotationOnExports43_expando_functions_2, TestCodeFixMissingTypeAnnotationOnExports43_expando_functions_3, TestCodeFixMissingTypeAnnotationOnExports43_expando_functions_4, TestCodeFixMissingTypeAnnotationOnExports43_expando_functions_5, TestCodeFixMissingTypeAnnotationOnExports43_expando_functions, TestCodeFixMissingTypeAnnotationOnExports44_default_export, TestCodeFixMissingTypeAnnotationOnExports45_decorators, TestCodeFixMissingTypeAnnotationOnExports46_decorators_experimental, TestCodeFixMissingTypeAnnotationOnExports47, TestCodeFixMissingTypeAnnotationOnExports48, TestCodeFixMissingTypeAnnotationOnExports49_private_name, TestCodeFixMissingTypeAnnotationOnExports4, TestCodeFixMissingTypeAnnotationOnExports50_generics_with_default, TestCodeFixMissingTypeAnnotationOnExports51_slightly_more_complex_generics_with_default, TestCodeFixMissingTypeAnnotationOnExports52_generics_oversimplification, TestCodeFixMissingTypeAnnotationOnExports53_nested_generic_types, TestCodeFixMissingTypeAnnotationOnExports54_generator_generics, TestCodeFixMissingTypeAnnotationOnExports55_generator_return, TestCodeFixMissingTypeAnnotationOnExports57_generics_doesnt_drop_trailing_unknown, TestCodeFixMissingTypeAnnotationOnExports58_genercs_doesnt_drop_trailing_unknown_2, TestCodeFixMissingTypeAnnotationOnExports59_drops_unneeded_after_unknown, TestCodeFixMissingTypeAnnotationOnExports5, TestCodeFixMissingTypeAnnotationOnExports60_drops_unneeded_non_trailing_unknown, TestCodeFixMissingTypeAnnotationOnExports6, TestCodeFixMissingTypeAnnotationOnExports7, TestCodeFixMissingTypeAnnotationOnExports8, TestCodeFixMissingTypeAnnotationOnExports9, TestCodeFixMissingTypeAnnotationOnExportsTypePredicate1, TestCodeFixMissingTypeAnnotationOnExports_arrowParensParamOnly, TestCodeFixMissingTypeAnnotationOnExports_arrowParens, TestCodeFixMissingTypeAnnotationOnExports_expandoNoDuplicates, TestCodeFixMissingTypeAnnotationOnExports_jsxWhitespaceText, TestCodeFixMissingTypeAnnotationOnExports, TestCodeFixNegativeReplaceQualifiedNameWithIndexedAccessType01, TestCodeFixOverrideModifier18, TestCodeFixPromoteTypeOnlyImportJsxTag, TestCodeFixPromoteTypeOnlyImportJsxTagBothTypeOnly, TestCodeFixPromoteTypeOnlyOrderingCrash, TestCodeFixPropertyOverrideAccess4, TestCodeFixRemoveUnnecessaryAwait_mixedUnion, TestCodeFixRemoveUnnecessaryAwait_notAvailableOnReturn, TestCodeFixRequireInTs3, TestCodeFixRequireInTs5, TestCodeFixSpellingJs5, TestCodeFixSpellingJs6, TestCodeFixSpellingJs7, TestCodeFixSpellingShortName2, TestCodeFixTopLevelAwait_module_blankCompilerOptionsInTsConfig, TestCodeFixTopLevelAwait_module_compatibleCompilerOptionsInTsConfig, TestCodeFixTopLevelAwait_module_missingCompilerOptionsInTsConfig, TestCodeFixTopLevelAwait_module_noTsConfig, TestCodeFixTopLevelAwait_target_compatibleCompilerOptionsInTsConfig, TestCodeFixTopLevelAwait_target_noTsConfig, TestCodeFixTopLevelForAwait_module_blankCompilerOptionsInTsConfig, TestCodeFixTopLevelForAwait_module_compatibleCompilerOptionsInTsConfig, TestCodeFixTopLevelForAwait_module_missingCompilerOptionsInTsConfig, TestCodeFixTopLevelForAwait_module_noTsConfig, TestCodeFixTopLevelForAwait_target_compatibleCompilerOptionsInTsConfig, TestCodeFixTopLevelForAwait_target_noTsConfig, TestCodeFixUnreachableCode_noSuggestionIfDisabled, TestCodeFixUnusedIdentifier_parameter1, TestCodeFixUnusedLabel_noSuggestionIfDisabled, TestCodeFixUseBigIntLiteralWithNumericSeparators

FAIL:
- TestCodeFixAddMissingImportForReactJsx1 — terminate called after throwing an instance of 'std::bad_alloc' what():  std::bad_alloc [killed by signal 6]
- TestCodeFixAddMissingImportForReactJsx2 — terminate called after throwing an instance of 'std::bad_alloc' what():  std::bad_alloc [killed by signal 6]
- TestCodeFixGenerateDefinitions — [killed by signal 11]
- TestCodeFixMissingTypeAnnotationOnExports30_inline_import — No code fix with description "Add satisfies and an inline type assertion with 'Person'" at index 1 found. Available fixes: Add annotation of type '{ person: imp
- TestCodeFixMissingTypeAnnotationOnExports31_inline_import_default — Expected code fix with description "Add satisfies and an inline type assertion with 'Person'" not found. Available fixes: Extract default export to variable, Ad
- TestCodeFixMissingTypeAnnotationOnExports56_toplevel_import — No code fix with description "Add return type '{ person: Person; }'" at index 0 found. Available fixes: Add return type '{ person: import("./person-code").Perso
- TestCodeFixSpellingJs3 — Expected no errors but found 1 in /a.js: Property 'none' may not exist on type 'Classe'. Did you mean 'non'?
- TestCodeFixSpellingJs8 — Expected no errors but found 2 in /a.js: Unused '@ts-expect-error' directive., Could not find name 'locale'. Did you mean 'locals'?

SKIP (Go `t.Skip` parity): TestCodeFixAddParameterNames1, TestCodeFixAddParameterNames2, TestCodeFixAddParameterNames3, TestCodeFixClassExtendAbstractSomePropertiesPresent, TestCodeFixClassImplementInterfaceNoTruncation, TestCodeFixClassSuperMustPrecedeThisAccess, TestCodeFixCorrectQualifiedNameToIndexedAccessType01, TestCodeFixInferFromCallInAssignment, TestCodeFixInferFromExpressionStatement, TestCodeFixInferFromFunctionThisUsageObjectPropertyParameter, TestCodeFixInferFromFunctionThisUsageObjectPropertyShorthandParameter, TestCodeFixInferFromFunctionThisUsageObjectPropertyShorthand, TestCodeFixInferFromFunctionThisUsageObjectProperty, TestCodeFixInferFromFunctionUsage, TestCodeFixInferFromPrimitiveUsage, TestCodeFixInferFromUsageCall, TestCodeFixInferFromUsageMember2, TestCodeFixInferFromUsageMember3, TestCodeFixInferFromUsageMember, TestCodeFixInferFromUsageOptionalParam2, TestCodeFixInferFromUsageOptionalParam, TestCodeFixInferFromUsageRestParam2, TestCodeFixInferFromUsageRestParam3, TestCodeFixInferFromUsageRestParam, TestCodeFixInferFromUsageVariable3JS, TestCodeFixMissingTypeAnnotationOnExports16, TestCodeFixMissingTypeAnnotationOnExports23_heritage_formatting, TestCodeFixMissingTypeAnnotationOnExports25_heritage_formatting_3, TestCodeFixMissingTypeAnnotationOnExports28_long_types, TestCodeFixSpelling4, TestCodeFixSpelling5, TestCodeFixSpellingCaseSensitive1, TestCodeFixSpellingCaseSensitive2, TestCodeFixSpellingCaseSensitive3, TestCodeFixSpellingCaseWeight1, TestCodeFixSpellingCaseWeight2, TestCodeFixSpellingShortName1, TestCodeFixTopLevelAwait_module_targetES2017CompilerOptionsInTsConfig, TestCodeFixTopLevelForAwait_module_targetES2017CompilerOptionsInTsConfig, TestCodeFixUndeclaredPropertyAccesses, TestCodeFixUnusedIdentifier_suggestion, TestCodeFixUnusedInterfaceInNamespace1, TestCodeFixUnusedInterfaceInNamespace2

SKIP (port stub `newMissingMemberFixer`): TestCodeFixAmbientClassImplementClassAbstractGettersAndSetters, TestCodeFixAmbientClassImplementClassMethodViaHeritage, TestCodeFixClassExprClassImplementClassFunctionVoidInferred, TestCodeFixClassImplementClassAbstractGettersAndSetters, TestCodeFixClassImplementClassFunctionVoidInferred, TestCodeFixClassImplementClassMemberAnonymousClass, TestCodeFixClassImplementClassMethodViaHeritage, TestCodeFixClassImplementClassMultipleSignatures1, TestCodeFixClassImplementClassMultipleSignatures2, TestCodeFixClassImplementClassPropertyModifiers, TestCodeFixClassImplementClassPropertyTypeQuery, TestCodeFixClassImplementDeepInheritance, TestCodeFixClassImplementDefaultClass, TestCodeFixClassImplementInterfaceArrayTuple, TestCodeFixClassImplementInterfaceAutoImportsReExports, TestCodeFixClassImplementInterfaceAutoImports, TestCodeFixClassImplementInterfaceAutoImports_typeOnly, TestCodeFixClassImplementInterfaceCallSignature, TestCodeFixClassImplementInterfaceCallback, TestCodeFixClassImplementInterfaceClassExpression, TestCodeFixClassImplementInterfaceComments, TestCodeFixClassImplementInterfaceComputedPropertyLiterals, TestCodeFixClassImplementInterfaceComputedPropertyNameWellKnownSymbols, TestCodeFixClassImplementInterfaceConstructSignature, TestCodeFixClassImplementInterfaceConstructorName1, TestCodeFixClassImplementInterfaceConstructorName2, TestCodeFixClassImplementInterfaceDuplicateMember1, TestCodeFixClassImplementInterfaceEmptyMultilineBody, TestCodeFixClassImplementInterfaceEmptyTypeLiteral, TestCodeFixClassImplementInterfaceGlobal, TestCodeFixClassImplementInterfaceHeritageClauseAlreadyHaveMember, TestCodeFixClassImplementInterfaceInNamespace, TestCodeFixClassImplementInterfaceIndexSignaturesBoth, TestCodeFixClassImplementInterfaceIndexSignaturesNumber, TestCodeFixClassImplementInterfaceIndexSignaturesString, TestCodeFixClassImplementInterfaceIndexType, TestCodeFixClassImplementInterfaceInheritsAbstractMethod, TestCodeFixClassImplementInterfaceMappedType1, TestCodeFixClassImplementInterfaceMappedType2, TestCodeFixClassImplementInterfaceMappedTypeIndirectKeys, TestCodeFixClassImplementInterfaceMemberNestedTypeAlias, TestCodeFixClassImplementInterfaceMemberOrdering, TestCodeFixClassImplementInterfaceMemberTypeAlias, TestCodeFixClassImplementInterfaceMethodThisAndSelfReference, TestCodeFixClassImplementInterfaceMethodTypePredicate, TestCodeFixClassImplementInterfaceMultipleImplements1, TestCodeFixClassImplementInterfaceMultipleImplements2, TestCodeFixClassImplementInterfaceMultipleImplementsIntersection1, TestCodeFixClassImplementInterfaceMultipleMembersAndPunctuation, TestCodeFixClassImplementInterfaceMultipleSignaturesRest1, TestCodeFixClassImplementInterfaceMultipleSignaturesRest2, TestCodeFixClassImplementInterfaceMultipleSignatures, TestCodeFixClassImplementInterfaceNamespaceConflict, TestCodeFixClassImplementInterfaceNoBody, TestCodeFixClassImplementInterfaceNoTruncationProperties, TestCodeFixClassImplementInterfaceObjectLiteral, TestCodeFixClassImplementInterfaceOptionalProperty, TestCodeFixClassImplementInterfacePropertyFromParentConstructorFunction, TestCodeFixClassImplementInterfacePropertySignatures, TestCodeFixClassImplementInterfaceProperty, TestCodeFixClassImplementInterfaceQualifiedName, TestCodeFixClassImplementInterfaceSomePropertiesPresent, TestCodeFixClassImplementInterfaceTypeLiterals, TestCodeFixClassImplementInterfaceTypeParamInstantiateDeeply, TestCodeFixClassImplementInterfaceTypeParamInstantiateError, TestCodeFixClassImplementInterfaceTypeParamInstantiateNumber, TestCodeFixClassImplementInterfaceTypeParamInstantiateT, TestCodeFixClassImplementInterfaceTypeParamInstantiateU, TestCodeFixClassImplementInterfaceTypeParamMethod, TestCodeFixClassImplementInterfaceUndeclaredSymbol, TestCodeFixClassImplementInterfaceWithAmbientSignatures1, TestCodeFixClassImplementInterfaceWithAmbientSignatures2, TestCodeFixClassImplementInterfaceWithAmbientSignatures3, TestCodeFixClassImplementInterfaceWithNegativeNumber, TestCodeFixClassImplementInterface_all, TestCodeFixClassImplementInterface_noUndefinedOnOptionalParameter, TestCodeFixClassImplementInterface_order, TestCodeFixClassImplementInterface_quotePreferenceAuto1, TestCodeFixClassImplementInterface_quotePreferenceAuto2, TestCodeFixClassImplementInterface_quotePreferenceDouble, TestCodeFixClassImplementInterface_quotePreferenceSingle, TestCodeFixClassImplementInterface_typeInOtherFile

### `importfix` — 4 pass / 5 fail / 1 skip

PASS: TestImportFixWithMultipleModuleExportAssignment, TestImportFixes_ambientCircularDefaultCrash, TestImportFixes_quotePreferenceDouble_importHelpers, TestImportFixes_quotePreferenceSingle_importHelpers

FAIL:
- TestImportFixFromAtTypesWithRealPackage — [killed by signal 11]
- TestImportFixFromAtTypesWithRealPackageExports — [killed by signal 11]
- TestImportFixBeforeIndentedImport — [killed by signal 11]
- TestImportFixAfterIndentedImport — [killed by signal 11]
- TestImportFixBeforeIndentedImportWithCarriageReturns — [killed by signal 11]

SKIP (Go `t.Skip` parity): TestImportFixesGlobalTypingsCache

### `tsx` — 56 pass / 4 fail / 4 skip

PASS: TestTsxCompletion10, TestTsxCompletion11, TestTsxCompletion14, TestTsxCompletion15, TestTsxCompletion1, TestTsxCompletion2, TestTsxCompletion3, TestTsxCompletion4, TestTsxCompletion5, TestTsxCompletion6, TestTsxCompletion8, TestTsxCompletionInFunctionExpressionOfChildrenCallback1, TestTsxCompletionInFunctionExpressionOfChildrenCallback, TestTsxCompletionOnClosingTag1, TestTsxCompletionOnClosingTag2, TestTsxCompletionOnClosingTagWithoutJSX1, TestTsxCompletionOnClosingTagWithoutJSX2, TestTsxCompletionsGenericComponent, TestTsxFindAllReferences10, TestTsxFindAllReferences11, TestTsxFindAllReferences1VS, TestTsxFindAllReferences1, TestTsxFindAllReferences2, TestTsxFindAllReferences3, TestTsxFindAllReferences4, TestTsxFindAllReferences5, TestTsxFindAllReferences6, TestTsxFindAllReferences7, TestTsxFindAllReferences8, TestTsxFindAllReferences9, TestTsxFindAllReferencesUnionElementType1, TestTsxFindAllReferencesUnionElementType2, TestTsxGoToDefinitionClassInDifferentFile, TestTsxGoToDefinitionClasses, TestTsxGoToDefinitionIntrinsics, TestTsxGoToDefinitionStatelessFunction1, TestTsxGoToDefinitionStatelessFunction2, TestTsxGoToDefinitionUnionElementType1, TestTsxGoToDefinitionUnionElementType2, TestTsxIncrementalServer, TestTsxIncremental, TestTsxParsing, TestTsxQuickInfo1, TestTsxQuickInfo2, TestTsxQuickInfo3, TestTsxRename1, TestTsxRename2, TestTsxRename3, TestTsxRename4, TestTsxRename5, TestTsxRename6, TestTsxRename7, TestTsxRename8, TestTsxRename9, TestTsxSignatureHelp1, TestTsxSignatureHelp2

FAIL:
- TestTsxCompletion12 — At marker '1': Completion item mismatch for label optional?: (-actual +expected): - { "label": "optional?", "kind": 5, "sortText": "12", "filterText": "optional
- TestTsxCompletion13 — At marker '1': Completion item mismatch for label children?: (-actual +expected): - { "label": "children?", "kind": 5, "sortText": "12", "filterText": "children
- TestTsxCompletion7 — At marker '': Completion item mismatch for label TWO: (-actual +expected): - { "label": "TWO", "kind": 5, "sortText": "11", "data": { "fileName": "/file.tsx", "
- TestTsxCompletionNonTagLessThan — At marker 'a': Includes completion item mismatch for label number: (-actual +expected): - { "label": "number", "kind": 14, "sortText": "15", "data": { "fileName

SKIP (Go `t.Skip` parity): TestTsxQuickInfo4, TestTsxQuickInfo5, TestTsxQuickInfo6, TestTsxQuickInfo7

### `jsx` — 8 pass / 14 fail / 1 skip

PASS: TestJsxElementExtendsNoCrash1, TestJsxElementExtendsNoCrash2, TestJsxElementExtendsNoCrash3, TestJsxElementMissingOpeningTagNoCrash, TestJsxFindAllReferencesOnRuntimeImportWithPaths1, TestJsxGenericQuickInfo, TestJsxQualifiedTagCompletion, TestJsxSpreadReference

FAIL:
- TestJsxAriaLikeCompletions — At marker '1': Completion item mismatch for label aria-whatever?: (-actual +expected): - { "label": "aria-whatever?", "kind": 5, "sortText": "12", "filterText":
- TestJsxAttributeCompletionStyleAuto — At marker '': Completion item mismatch for label prop_a: (-actual +expected): - { "label": "prop_a", "kind": 5, "sortText": "11", "data": { "fileName": "/foo.ts
- TestJsxAttributeCompletionStyleBraces — At marker '': Completion item mismatch for label prop_a: (-actual +expected): - { "label": "prop_a", "kind": 5, "sortText": "11", "filterText": "prop_a={$1}", "
- TestJsxAttributeCompletionStyleDefault — At marker '': Completion item mismatch for label prop_a: (-actual +expected): - { "label": "prop_a", "kind": 5, "sortText": "11", "data": { "fileName": "/foo.ts
- TestJsxAttributeCompletionStyleNoSnippet — At marker '': Completion item mismatch for label prop_a: (-actual +expected): - { "label": "prop_a", "kind": 5, "sortText": "11", "data": { "fileName": "/foo.ts
- TestJsxAttributeCompletionStyleNone — At marker '': Completion item mismatch for label prop_a: (-actual +expected): - { "label": "prop_a", "kind": 5, "sortText": "11", "data": { "fileName": "/foo.ts
- TestJsxAttributeSnippetCompletionAfterTypeArgs — [killed by signal 11]
- TestJsxAttributeSnippetCompletionClosed — [killed by signal 11]
- TestJsxAttributeSnippetCompletionUnclosed — [killed by signal 11]
- TestJsxTagNameCompletionClosed — [killed by signal 11]
- TestJsxTagNameCompletionUnclosed — [killed by signal 11]
- TestJsxTagNameCompletionUnderElementClosed — [killed by signal 11]
- TestJsxTagNameCompletionUnderElementUnclosed — [killed by signal 11]
- TestJsxTagNameCompletionWithExistingJsxInitializer — [killed by signal 11]

SKIP (Go `t.Skip` parity): TestJsxWithTypeParametershasInstantiatedSignatureHelp

## Divergences

All batch-B failures are faithful ports exercising real C++-side
divergences from the Go oracle — documented, not fixed, per the task rules.

### 1. organizeImports — 89/90 fail: `diagnostics: null` on wire
Every `VerifyOrganizeImports*` call fails at request dispatch:
`textDocument/codeAction request returned error: [-32602]: InvalidParams:
null value is not allowed for field "diagnostics"`. The C++ LSP marshalling
emits `"diagnostics": null` for an absent/empty slice instead of omitting the
field (Go: `omitempty`), and the params validator then rejects the request.
Hypothesis: `CodeActionContext`/`CodeActionParams` marshal lacks omitempty
handling for `Diagnostics` in `lsproto_generated` (or emits `null` for an
empty `Slice<T>`). One fix should flip all 89.

- `TestOrganizeImportsWithTraceResolution1` — `std::bad_function_call`:
  a trace-resolution callback (`std::function`) is invoked unassigned —
  the module-resolution trace hook is not wired for this path.

### 2. callHierarchy — 28/38 fail: calls enumerate as none
Local baseline shows `incoming: none / outgoing: none` where the Go
reference has full call entries. The prepareCallHierarchy* items resolve,
but the incoming/outgoing call enumeration returns empty. Hypothesis:
`getIncomingCalls`/`getOutgoingCalls` (references-driven caller walk) is
stubbed or unimplemented on the C++ side.

### 3. inlayHints — 13/64 fail: label-part `location` missing
Baselines differ only in the `location` link on inlay-hint label parts —
Go emits `{value, location:{uri,range}}` linking the hint to the
declaration site; C++ emits `{value}` only. Hypothesis: the
declaration-location link in `convertTypeToInlayHintParts` (or the
InlayHintLabelPart location plumbing) is not populated.

### 4. quickInfoDisplayParts (6) & smartSelection (2) — baseline diffs
- vSQuickInfo baselines: `SymbolDisplayPart.Id` values differ (`0x1880` vs
  `0x758`) — the C++ AST node-Id allocation sequence drifts from Go;
  cosmetic unless downstream depends on exact ids.
- quickInfo baselines: response JSON lacks `"canIncreaseVerbosity": true`
  — the verbosity flag is not populated on the quickinfo result.
- smartSelection_complex: also a missing-type-line diff — a very long
  `IsExactlyAny<...>` conditional type line present in the Go reference
  is absent locally.

### 5. jsx (14 fail) — 8 crashes + 6 completion mismatches
- 8 SEGVs across `TestJsxTagNameCompletion*` and
  `TestJsxAttributeSnippetCompletion*`. Sampled
  `TestJsxTagNameCompletionClosed` under gdb: `double free or corruption`
  in `lsproto::ResponseMessage::~ResponseMessage` — a
  `ResponseMessage`/`AnyValue` is released through two independent
  `shared_ptr` control blocks. Ownership bug in the LSP response path,
  triggered from the JSX completion request.
- 6 `Completion item mismatch` diffs (`prop_a`, `aria-whatever?` etc.):
  `filterText`/`data` fields differ on auto-import JSX-attribute
  completions (e.g. `filterText: "prop_a={$1}"` vs expected `prop_a`,
  plus `data.fileName` payload differences).

### 6. codefix — 8 fail: 3 crashes + description/exact-match diffs
- `TestCodeFixAddMissingImportForReactJsx1/2` — `std::bad_alloc`:
  unbounded allocation in the react-jsx missing-import path (same
  failure family as batch-A `TestAutoImport_node12_node_modules1`).
- `TestCodeFixGenerateDefinitions` — SEGV in the generate-definitions
  fixer.
- `TestCodeFixMissingTypeAnnotationOnExports{30,31,56}` — the fix list
  differs in descriptions/order: e.g. expected `Add satisfies and an
  inline type assertion with 'Person'` absent; `Add return type`
  uses `import("./person-code").Person` vs expected `Person`.
- `TestCodeFixSpellingJs3/8` — unexpected diagnostics surface
  (`Property 'none' may not exist`, `Unused '@ts-expect-error'`) —
  js-diagnostics gate diverges.

### 7. getEditsForFileRename — 3 fail
- `TestGetEditsForFileRenameWithSolutionConfigFile` — SEGV.
- `TestGetEditsForFileRename_cssImport2` — `.css` is not tracked as a
  script info (`Expected script info for /app2.css, but got nil`).
- `TestGetEditsForFileRename_cssImport3` — css module rename produces
  raw css text instead of the synthesized `.d.ts` declaration
  (`declare const css: {...}; export default css;`).

### 8. importfix — 5 fail, all SEGV
`TestImportFixFromAtTypesWithRealPackage{,Exports}` and
`TestImportFix{Before,After}IndentedImport{,WithCarriageReturns}` die in
the import-fix path (auto-import resolution / new-file-content).

### 9. tsx — 4 fail: completion-item field diffs
`TestTsxCompletion7/12/13`, `TestTsxCompletionNonTagLessThan`: same
`filterText`/`data` field divergence family as jsx (5b).

### 10. Port-side stub skips — 82
`ls/lsdeps.cpp: newMissingMemberFixer` is a stub panic
(`tsc internal error: ... ls/codeactions_missingmemberfixer slice`);
the fourslash recover harness reports it as SKIP. Affects every
"implement member/interface" fixer test (codefix). In Go these pass.

## Conversion skips (not ported)
- `TestGetEditsForFileRenameLoadsUnopenedCompositeProject` — uses
  `lsconv.FileNameToDocumentURI`, not ported.
- `TestGetEditsForFileRename_cssImport4` — uses
  `capabilities.Workspace.FileOperations.WillRename`, not ported.
- `TestGetEditsForFileRename_duplicateUnresolvedImports` — builds
  content via `strings.Builder` (non-literal).
- `TestOrganizeImports_exportLeadingComment_notDuplicated` — uses
  `t.Run` subtests.
- `TestOrganizeImports_removeUnusedUsesLanguageServiceFormatOptions` —
  uses `ParseUserPreferences` on `map[string]any`.
## Divergences fixed (batch-B follow-up)

25 of the baseline/field-diff failures above are now fixed; suite result
**597/718 pass** (baseline was 554/718 at branch point — cross-cutting
fixes also recovered ~18 tests sharing the same helpers). Root causes:

### §5b/§9 tsx ×4 + jsx ×6 — `pathIgnored` field-name casing
`fourslash_deps.h::pathIgnored` looked up ignored paths as `.fieldName`
(lowercase) only, while baseline JSON keys for completion-item fields use
the capitalized `.FieldName` form, so `filterText`/`data.fileName` diffs
were never filtered. Now tries `.fieldName` then `.FieldName`.
Fixes: `TestTsxCompletion7/12/13`, `TestTsxCompletionNonTagLessThan`,
and the 6 jsx `Completion item mismatch` diffs.

### §6b codefix ×5 — two root causes
- **SpellingJs3/8** (js-diagnostics gate): `checker.cpp::addErrorOrSuggestion`
  appended the original error diagnostic verbatim instead of cloning it
  into `suggestionDiagnostics` with `CategorySuggestion` (Go
  checker.go:14258), and `program.cpp` ran the plainJS error filter
  *after* the jsdoc-append/diagnostic merge instead of early-returning
  the filtered list (Go program.go:1518-1523). Stray
  `Property 'none' may not exist` / `Unused '@ts-expect-error'`
  diagnostics are gone.
- **MissingTypeAnnotationOnExports30/31/56** (arena lifetime +
  idToSymbol): `TypeToTypeNodeEx` shares `idToSymbol` with the caller by
  reference in Go; C++ copied the map by value so it looked empty, and
  `TryGetAutoImportableReferenceFromTypeNode` rebuilt the type node with
  a stack `NodeFactory` whose arena died on return — the escaped
  `TypeReferenceNode` dangled into garbage (`kind==Unknown`) and the
  printer panicked in `typeToStringForDiag`. `autoimport.h` signatures
  now take a `NodeFactory*`; callers pass `&c->factory` /
  `changeTracker->nodeFactory`. NOTE (shared-helper edit):
  `codeactions_missingmemberfixer.cpp` needed the same extra argument at
  its `TryGetAutoImportableReferenceFromTypeNode` call site.

### §4 quickinfodp ×6 — `%x` formatting + idToSymbol
`SymbolDisplayPart.Id` prints node ids as hex via `%X`; `gostd::fmtArg`
only knew strings, so int ids byte-hexed as garbage. `fmtArg` now carries
`num`/`isInt` and `%x`/`%X` format ints in hex (string handling
unchanged). Together with the `idToSymbol` reference-semantics fix
(`checker.h` + `checker_nodebuilder.cpp`, 6 sites), all 6 vSQuickInfo
baselines match including `canIncreaseVerbosity`.

### §4 smartSelection ×2 — `forEachChild` missing `SyntaxList` case
`gencpp.py` parsed `visit(v, node.X)`/`visitNodeList`/`visitModifiers`
in Go `ForEachChild` bodies but not `visitNodes` over `[]*Node` fields —
so `SyntaxList::Children` and `JSDocTypeLiteral::JSDocPropertyTags`
emitted no switch case and `forEachChild` fell through to `default:` →
`false`. The synthesized SyntaxList levels selectionranges builds for
mapped-type children were therefore invisible, dropping the long
`IsExactlyAny<...>` conditional line. Fixed gencpp (regex +
`visitNodes`→`visitChildList` + `deepCloneNodeVec` emit), added a
`visitChildList` overload for `std::vector<Node*>` in `ast.h`, and
hand-inserted the two generated-format cases into `nodes_generated.h`
(the checked-in file carries hand customizations — full regen reverts
them, so the two cases were inserted in generated form only).

### §7 getEdits css ×2 — extension-helper ownership + nil-vs-list
- **cssImport2** (`/app2.css` never tracked):
  `tspath::getPossibleOriginalInputExtensionForExtension` returned
  `std::vector<std::string_view>`, but the `.d.x.ts` branch synthesized
  `"." + inner` as a `std::string` **temporary** — the returned view
  dangled, the twin-candidate extension was garbage bytes,
  `host->FileExists(oldOriginalPath)` was false, and the
  `RenameFile{app.css→app2.css}` op was never emitted. Return type is
  now `std::vector<std::string>` (Go's escaping `[]string` equivalent);
  `string_completions.cpp` local updated to match.
- **cssImport3** (raw css over declaration):
  `getNewFileNameForModuleRename` passed `extensionsToRemove` to
  `changeAnyExtension` where Go passes `nil` — `.css` isn't in
  `extensionsToRemove`, so `ChangeAnyExtension("/app2.css", ".d.css.ts")`
  was a no-op and the module rename targeted `/app2.css` instead of
  `/app2.d.css.ts`. Now passes an empty extensions list (Go `nil`
  semantics: last-dot extension).
