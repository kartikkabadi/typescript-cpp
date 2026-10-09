// codeactions_importfixes.go — "fix missing import" quickfix provider:
// surfaces autoimport fixes for cannot-find-name diagnostics, including UMD
// globals, JSX namespace imports, and type-only promotion fixes.
#include "internal/ls/ls.h"

#include "internal/ast/ast.h"
#include "internal/astnav/tokens.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/locale/locale.h"
#include "internal/scanner/scanner.h"
#include "internal/tspath/tspath.h"

#include <algorithm>

namespace tsc::ls {

namespace {

const std::vector<int32_t> importFixErrorCodes{
    Cannot_find_name_0->code,
    Cannot_find_name_0_Did_you_mean_1->code,
    Cannot_find_name_0_Did_you_mean_the_instance_member_this_0->code,
    Cannot_find_name_0_Did_you_mean_the_static_member_1_0->code,
    Cannot_find_namespace_0->code,
    X_0_refers_to_a_UMD_global_but_the_current_file_is_a_module_Consider_adding_an_import_instead
        ->code,
    X_0_only_refers_to_a_type_but_is_being_used_as_a_value_here->code,
    No_value_exists_in_scope_for_the_shorthand_property_0_Either_declare_one_or_provide_an_initializer
        ->code,
    X_0_cannot_be_used_as_a_value_because_it_was_imported_using_import_type
        ->code,
    Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_jQuery_Try_npm_i_save_dev_types_Slashjquery
        ->code,
    Cannot_find_name_0_Do_you_need_to_change_your_target_library_Try_changing_the_lib_compiler_option_to_1_or_later
        ->code,
    Cannot_find_name_0_Do_you_need_to_change_your_target_library_Try_changing_the_lib_compiler_option_to_include_dom
        ->code,
    Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_a_test_runner_Try_npm_i_save_dev_types_Slashjest_or_npm_i_save_dev_types_Slashmocha_and_then_add_jest_or_mocha_to_the_types_field_in_your_tsconfig
        ->code,
    Cannot_find_name_0_Did_you_mean_to_write_this_in_an_async_function->code,
    Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_jQuery_Try_npm_i_save_dev_types_Slashjquery_and_then_add_jquery_to_the_types_field_in_your_tsconfig
        ->code,
    Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_a_test_runner_Try_npm_i_save_dev_types_Slashjest_or_npm_i_save_dev_types_Slashmocha
        ->code,
    Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_node_Try_npm_i_save_dev_types_Slashnode
        ->code,
    Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_node_Try_npm_i_save_dev_types_Slashnode_and_then_add_node_to_the_types_field_in_your_tsconfig
        ->code,
    Cannot_find_namespace_0_Did_you_mean_1->code,
    Cannot_extend_an_interface_0_Did_you_mean_implements->code,
    This_JSX_tag_requires_0_to_be_in_scope_but_it_could_not_be_found->code,
};

const std::string importFixID = "fixMissingImport";

// fixInfo — codeactions_importfixes.go:56.
struct fixInfo {
	std::unique_ptr<autoimport::Fix> fix;
	std::string symbolName;
	std::string errorIdentifierText;
	bool isJsxNamespaceFix = false;
};

// symbolNameInfo — codeactions_importfixes.go:375.
struct symbolNameInfo {
	std::string name;
	bool isTypeOnly =
	    false; // whether the symbol currently resolves to a type-only import
};

// isDynamicFileName — tspath/path.go:50 (not yet ported in cpp tspath).
bool isDynamicFileName(const std::string& fileName) {
	return fileName.starts_with("^/");
}

// slices.ContainsFunc
template <typename T, typename F>
bool containsFunc(const std::vector<T>& v, F&& pred) {
	return std::find_if(v.begin(), v.end(), std::forward<F>(pred)) !=
	       v.end();
}

// core.CompareBooleans
int compareBooleans(bool a, bool b) {
	if (a == b) {
		return 0;
	}
	return a ? 1 : -1;
}

// Forward declarations — Go package-level functions defined later in this
// file but referenced earlier.
std::pair<std::vector<fixInfo*>, gostd::Error> getFixInfos(
    checker::Checker* ch, CodeFixContext* fixContext, int32_t errorCode,
    int pos);
gostd::Error addImportFromDiagnostic(checker::Checker* ch,
                                     autoimport::ImportAdder* importAdder,
                                     Diagnostic* diag,
                                     CodeFixContext* fixContext);
std::vector<fixInfo*> getFixesInfoForUMDImport(Node* token,
                                               autoimport::View* view,
                                               checker::Checker* ch);
Symbol* getUmdSymbol(Node* token, checker::Checker* ch);
bool isUMDExportSymbol(Symbol* symbol);
std::vector<fixInfo*> getFixesInfoForNonUMDImport(
    CodeFixContext* fixContext, Node* symbolToken, autoimport::View* view,
    checker::Checker* ch);
autoimport::Fix* getTypeOnlyPromotionFix(SourceFile* sourceFile,
                                         Node* symbolToken,
                                         const std::string& symbolName,
                                         checker::Checker* ch);
std::vector<symbolNameInfo> getSymbolNamesToImport(
    SourceFile* sourceFile, checker::Checker* ch, Node* symbolToken,
    const CompilerOptions* compilerOptions);
bool needsJsxNamespaceFix(const std::string& jsxNamespace,
                          Node* symbolToken, checker::Checker* ch);
bool jsxModeNeedsExplicitImport(JsxEmit jsx);
std::vector<fixInfo*> sortFixInfo(std::vector<fixInfo*> fixes,
                                  CodeFixContext* fixContext,
                                  autoimport::View* view);

// getImportCodeActions — codeactions_importfixes.go:63.
std::pair<std::vector<CodeAction*>, gostd::Error> getImportCodeActions(
    const gostd::Context& ctx, CodeFixContext* fixContext) {
	auto [ch, done] =
	    fixContext->Program->GetTypeCheckerForFileExclusive(ctx, nullptr);
	struct doneGuard {
		std::function<void()> f;
		~doneGuard() { if (f) f(); }
	} _done{done};

	auto [info, err] = getFixInfos(ch, fixContext, fixContext->ErrorCode,
	                               fixContext->Span.pos());
	if (err != nullptr) {
		return {{}, err};
	}
	if (info.empty()) {
		return {{}, nullptr};
	}

	std::vector<CodeAction*> actions;
	for (auto* fixInfo_ : info) {
		auto [edits, description, ok] = fixInfo_->fix->Edits(
		    ctx, fixContext->SourceFile, fixContext->Program->Options(),
		    fixContext->LS->FormatOptions(), fixContext->LS->Converters(),
		    fixContext->LS->UserPreferences());

		if (ok) {
			actions.push_back(new CodeAction{
			    .Description = description,
			    .Changes = edits,
			    .FixID = importFixID,
			    .FixAllDescription =
			        ::tsc::localize(locale::fromContext(ctx),
			                        Add_all_missing_imports, "", {}),
			});
		}
	}
	return {actions, nullptr};
}

// getAllImportCodeActions — codeactions_importfixes.go:95.
std::pair<CombinedCodeActions*, gostd::Error> getAllImportCodeActions(
    const gostd::Context& ctx, CodeFixContext* fixContext) {
	if (isDynamicFileName(fixContext->SourceFile->FileName())) {
		return {nullptr, nullptr};
	}

	auto allDiagnostics = fixContext->Program->GetSemanticDiagnostics(
	    fixContext->SourceFile);

	std::vector<Diagnostic*> importDiags;
	for (auto* diag : allDiagnostics) {
		if (isFixableDiagnostic(diag, importFixErrorCodes)) {
			importDiags.push_back(diag);
		}
	}

	if (importDiags.empty()) {
		return {nullptr, nullptr};
	}

	auto [ch, done] =
	    fixContext->Program->GetTypeCheckerForFileExclusive(ctx, nullptr);
	struct doneGuard {
		std::function<void()> f;
		~doneGuard() { if (f) f(); }
	} _done{done};

	auto [view, err] = fixContext->LS->getPreparedAutoImportView(
	    fixContext->SourceFile, ch);
	if (err != nullptr) {
		return {nullptr, err};
	}
	if (view == nullptr) {
		view = fixContext->LS->getCurrentAutoImportView(
		    fixContext->SourceFile, ch);
	}

	auto importAdder = autoimport::NewImportAdder(
	    ctx, fixContext->Program, ch, fixContext->SourceFile, view,
	    fixContext->LS->FormatOptions(), fixContext->LS->Converters(),
	    fixContext->LS->UserPreferences());

	for (auto* diag : importDiags) {
		if (auto err2 =
		        addImportFromDiagnostic(ch, importAdder.get(), diag,
		                                fixContext);
		    err2 != nullptr) {
			return {nullptr, err2};
		}
	}

	if (!importAdder->HasFixes()) {
		return {nullptr, nullptr};
	}

	return {new CombinedCodeActions{
	            .Description = ::tsc::localize(
	                locale::fromContext(ctx), Add_all_missing_imports, "",
	                {}),
	            .Changes = importAdder->Edits()},
	        nullptr};
}

// addImportFromDiagnostic — codeactions_importfixes.go:152. Finds the best
// import fix for a diagnostic and adds it to the adder.
gostd::Error addImportFromDiagnostic(checker::Checker* ch,
                                     autoimport::ImportAdder* importAdder,
                                     Diagnostic* diag,
                                     CodeFixContext* fixContext) {
	auto* diagFixContext = new CodeFixContext{
	    .SourceFile = fixContext->SourceFile,
	    .Span = TextRange{diag->Pos(), diag->End()},
	    .ErrorCode = diag->Code(),
	    .Program = fixContext->Program,
	    .LS = fixContext->LS,
	};

	auto [infos, err] =
	    getFixInfos(ch, diagFixContext, diag->Code(), diag->Pos());
	if (err != nullptr) {
		return err;
	}
	if (infos.size() > 0) {
		importAdder->AddImportFix(std::move(infos[0]->fix));
	}
	return nullptr;
}

// getFixInfos — codeactions_importfixes.go:171.
std::pair<std::vector<fixInfo*>, gostd::Error> getFixInfos(
    checker::Checker* ch, CodeFixContext* fixContext, int32_t errorCode,
    int pos) {
	// Can't compute import fixes for dynamic/untitled files since they
	// don't have real file paths
	if (isDynamicFileName(fixContext->SourceFile->FileName())) {
		return {{}, nullptr};
	}

	auto* symbolToken =
	    astnav::getTokenAtPosition(fixContext->SourceFile, pos);
	if (errorCode !=
	        X_0_refers_to_a_UMD_global_but_the_current_file_is_a_module_Consider_adding_an_import_instead
	            ->code &&
	    !isIdentifier(symbolToken)) {
		return {{}, nullptr};
	}

	autoimport::View* view = nullptr;
	std::vector<fixInfo*> info;

	if (errorCode ==
	    X_0_refers_to_a_UMD_global_but_the_current_file_is_a_module_Consider_adding_an_import_instead
	        ->code) {
		view = fixContext->LS->getCurrentAutoImportView(
		    fixContext->SourceFile, ch);
		info = getFixesInfoForUMDImport(symbolToken, view, ch);
	} else if (
	    errorCode ==
	    X_0_cannot_be_used_as_a_value_because_it_was_imported_using_import_type
	        ->code) {
		auto* compilerOptions = fixContext->Program->Options();
		auto symbolNames = getSymbolNamesToImport(
		    fixContext->SourceFile, ch, symbolToken, compilerOptions);

		std::vector<fixInfo*> allTypeOnlyFixes;
		for (auto& sn : symbolNames) {
			if (!sn.isTypeOnly) {
				continue;
			}
			auto* fix = getTypeOnlyPromotionFix(fixContext->SourceFile,
			                                    symbolToken, sn.name, ch);
			if (fix != nullptr) {
				allTypeOnlyFixes.push_back(new fixInfo{
				    .fix = std::unique_ptr<autoimport::Fix>(fix),
				    .symbolName = sn.name,
				    .errorIdentifierText = symbolToken->text(),
				});
			}
		}

		// For JSX opening tags, there can be separate type-only errors
		// for both the tag name identifier and the JSX namespace
		// identifier. When both produce valid fixes, we disambiguate
		// using the diagnostic message, which quotes the symbol name in
		// single quotes (e.g., "'React' cannot be used as a value...").
		// If filtering yields nothing (e.g., due to localization), fall
		// back to returning all candidates.
		std::string diagnosticMessage;
		if (fixContext->Diagnostic != nullptr) {
			diagnosticMessage = fixContext->Diagnostic->Message.AsString();
		}
		if (allTypeOnlyFixes.size() > 1 && diagnosticMessage != "") {
			for (auto* fi : allTypeOnlyFixes) {
				if (diagnosticMessage.find("'" + fi->symbolName + "'") !=
				    std::string::npos) {
					info.push_back(fi);
				}
			}
		}
		if (info.empty()) {
			info = allTypeOnlyFixes;
		}
		return {info, nullptr};
	} else {
		auto [v, err] = fixContext->LS->getPreparedAutoImportView(
		    fixContext->SourceFile, ch);
		if (err != nullptr) {
			return {{}, err};
		}
		view = v;
		if (view != nullptr) {
			info = getFixesInfoForNonUMDImport(fixContext, symbolToken,
			                                 view, ch);
		}
	}

	// Sort fixes by preference
	if (view == nullptr) {
		view = fixContext->LS->getCurrentAutoImportView(
		    fixContext->SourceFile, ch);
	}
	return {sortFixInfo(info, fixContext, view), nullptr};
}

// getFixesInfoForUMDImport — codeactions_importfixes.go:244.
std::vector<fixInfo*> getFixesInfoForUMDImport(Node* token,
                                               autoimport::View* view,
                                               checker::Checker* ch) {
	auto* umdSymbol = getUmdSymbol(token, ch);
	if (umdSymbol == nullptr) {
		return {};
	}

	auto export_ = autoimport::SymbolToExport(umdSymbol, ch);
	auto isValidTypeOnlyUseSite =
	    isValidTypeOnlyAliasUseSite(token);

	std::vector<fixInfo*> result;
	for (auto& fix : view->GetFixes(export_.get(), false,
	                                isValidTypeOnlyUseSite, nullptr)) {
		std::string errorIdentifierText;
		if (isIdentifier(token)) {
			errorIdentifierText = token->text();
		}
		result.push_back(new fixInfo{
		    .fix = std::move(fix),
		    .symbolName = umdSymbol->data->name,
		    .errorIdentifierText = errorIdentifierText,
		});
	}
	return result;
}

// getUmdSymbol — codeactions_importfixes.go:268.
Symbol* getUmdSymbol(Node* token, checker::Checker* ch) {
	// try the identifier to see if it is the umd symbol
	Symbol* umdSymbol = nullptr;
	if (isIdentifier(token)) {
		umdSymbol = ch->GetResolvedSymbol(token);
	}
	if (isUMDExportSymbol(umdSymbol)) {
		return umdSymbol;
	}

	// The error wasn't for the symbolAtLocation, it was for the JSX tag
	// itself, which needs access to e.g. `React`.
	auto* parent = token->parent;
	if ((isJsxOpeningLikeElement(parent) && parent->tagName() == token) ||
	    isJsxOpeningFragment(parent)) {
		Node* location = nullptr;
		if (isJsxOpeningLikeElement(parent)) {
			location = token;
		} else {
			location = parent;
		}
		auto jsxNamespace = ch->GetJsxNamespace(parent);
		auto* parentSymbol = ch->ResolveName(
		    jsxNamespace, location, SymbolFlagsValue,
		    false /* excludeGlobals */);
		if (isUMDExportSymbol(parentSymbol)) {
			return parentSymbol;
		}
	}
	return nullptr;
}

// isUMDExportSymbol — codeactions_importfixes.go:297.
bool isUMDExportSymbol(Symbol* symbol) {
	return symbol != nullptr && symbol->data->declarations.size() > 0 &&
	       symbol->data->declarations[0] != nullptr &&
	       isNamespaceExportDeclaration(symbol->data->declarations[0]);
}

// getFixesInfoForNonUMDImport — codeactions_importfixes.go:303.
std::vector<fixInfo*> getFixesInfoForNonUMDImport(
    CodeFixContext* fixContext, Node* symbolToken, autoimport::View* view,
    checker::Checker* ch) {
	auto* compilerOptions = fixContext->Program->Options();

	auto isValidTypeOnlyUseSite =
	    isValidTypeOnlyAliasUseSite(symbolToken);
	auto symbolNames =
	    getSymbolNamesToImport(fixContext->SourceFile, ch, symbolToken,
	                           compilerOptions);
	std::vector<fixInfo*> allInfo;

	// Compute usage position for JSDoc import type fixes
	auto [usagePosition, fidelity] = fixContext->LS->Converters()->ToLSPPosition(
	    fixContext->SourceFile,
	    TextPos(getTokenPosOfNode(symbolToken, fixContext->SourceFile,
	                              false)));
	if (!fidelity.IsExact()) {
		return {};
	}

	for (auto& sn : symbolNames) {
		// Type-only imports are handled by the promotion code path, not
		// the auto-import path.
		if (sn.isTypeOnly) {
			continue;
		}

		auto& symbolName = sn.name;
		// "default" is a keyword and not a legal identifier for the
		// import
		if (symbolName == "default") {
			continue;
		}

		auto isJSXTagName = symbolName == symbolToken->text() &&
		                    isJsxTagName(symbolToken);
		auto queryKind = autoimport::QueryKindExactMatch;
		if (isJSXTagName) {
			queryKind = autoimport::QueryKindCaseInsensitiveMatch;
		}

		auto exports = view->Search(symbolName, queryKind);
		for (auto* export_ : exports) {
			if (isJSXTagName &&
			    !(export_->Name() == symbolName ||
			      export_->IsRenameable())) {
				continue;
			}

			auto fixes =
			    view->GetFixes(export_, isJSXTagName,
			                   isValidTypeOnlyUseSite, &usagePosition);
			for (auto& fix : fixes) {
				allInfo.push_back(new fixInfo{
				    .fix = std::move(fix),
				    .symbolName = symbolName,
				    .isJsxNamespaceFix =
				        symbolName != symbolToken->text(),
				});
			}
		}
	}

	return allInfo;
}

// getTypeOnlyPromotionFix — codeactions_importfixes.go:354.
autoimport::Fix* getTypeOnlyPromotionFix(SourceFile* sourceFile,
                                         Node* symbolToken,
                                         const std::string& symbolName,
                                         checker::Checker* ch) {
	// Get the symbol at the token location
	auto* symbol = ch->ResolveName(symbolName, symbolToken, SymbolFlagsValue,
	                               true /* excludeGlobals */);
	if (symbol == nullptr) {
		return nullptr;
	}

	// Get the type-only alias declaration
	auto* typeOnlyAliasDeclaration =
	    ch->GetTypeOnlyAliasDeclaration(symbol);
	if (typeOnlyAliasDeclaration == nullptr ||
	    getSourceFileOfNode(typeOnlyAliasDeclaration) != sourceFile) {
		return nullptr;
	}

	return new autoimport::Fix{
	    .AutoImportFix = new lsproto::AutoImportFix{
	        .Kind = lsproto::AutoImportFixKindPromoteTypeOnly,
	    },
	    .TypeOnlyAliasDeclaration = typeOnlyAliasDeclaration,
	};
}

// getSymbolNamesToImport — codeactions_importfixes.go:380.
std::vector<symbolNameInfo> getSymbolNamesToImport(
    SourceFile* sourceFile, checker::Checker* ch, Node* symbolToken,
    const CompilerOptions* compilerOptions) {
	auto* parent = symbolToken->parent;
	if ((isJsxOpeningLikeElement(parent) ||
	     isJsxClosingElement(parent)) &&
	    parent->tagName() == symbolToken &&
	    jsxModeNeedsExplicitImport(compilerOptions->Jsx)) {
		auto jsxNamespace = ch->GetJsxNamespace(sourceFile->asNode());
		if (needsJsxNamespaceFix(jsxNamespace, symbolToken, ch)) {
			std::vector<symbolNameInfo> result;
			if (!isIntrinsicJsxName(symbolToken->text())) {
				auto* compSymbol = ch->ResolveName(
				    symbolToken->text(), symbolToken,
				    SymbolFlagsValue,
				    false /* excludeGlobals */);
				if (compSymbol == nullptr) {
					result.push_back(
					    symbolNameInfo{.name =
					                       symbolToken->text()});
				} else if (
				    ch->GetTypeOnlyAliasDeclaration(compSymbol) !=
				    nullptr) {
					result.push_back(symbolNameInfo{
					    .name = symbolToken->text(),
					    .isTypeOnly = true});
				}
			}
			bool nsIsTypeOnly = false;
			if (auto* nsSymbol = ch->ResolveName(
			        jsxNamespace, symbolToken, SymbolFlagsValue,
			        true /* excludeGlobals */);
			    nsSymbol != nullptr) {
				nsIsTypeOnly =
				    ch->GetTypeOnlyAliasDeclaration(nsSymbol) !=
				    nullptr;
			}
			result.push_back(symbolNameInfo{.name = jsxNamespace,
			                                .isTypeOnly = nsIsTypeOnly});
			return result;
		}
	}
	bool tokenIsTypeOnly = false;
	if (auto* sym = ch->ResolveName(symbolToken->text(), symbolToken,
	                                SymbolFlagsValue,
	                                true /* excludeGlobals */);
	    sym != nullptr) {
		tokenIsTypeOnly = ch->GetTypeOnlyAliasDeclaration(sym) != nullptr;
	}
	return {symbolNameInfo{.name = symbolToken->text(),
	                       .isTypeOnly = tokenIsTypeOnly}};
}

// needsJsxNamespaceFix — codeactions_importfixes.go:411.
bool needsJsxNamespaceFix(const std::string& jsxNamespace,
                          Node* symbolToken, checker::Checker* ch) {
	if (isIntrinsicJsxName(symbolToken->text())) {
		return true;
	}
	auto* namespaceSymbol = ch->ResolveName(jsxNamespace, symbolToken,
	                                        SymbolFlagsValue,
	                                        true /* excludeGlobals */);
	if (namespaceSymbol == nullptr) {
		return true;
	}
	if (containsFunc(namespaceSymbol->data->declarations,
	                 isTypeOnlyImportOrExportDeclaration)) {
		return (namespaceSymbol->flags & SymbolFlagsValue) == 0;
	}
	return false;
}

// jsxModeNeedsExplicitImport — codeactions_importfixes.go:425.
bool jsxModeNeedsExplicitImport(JsxEmit jsx) {
	return jsx == JsxEmit::React || jsx == JsxEmit::ReactNative;
}

// sortFixInfo — codeactions_importfixes.go:429.
std::vector<fixInfo*> sortFixInfo(std::vector<fixInfo*> fixes,
                                  CodeFixContext* fixContext,
                                  autoimport::View* view) {
	if (fixes.empty()) {
		return fixes;
	}

	// Create a copy to avoid modifying the original
	std::vector<fixInfo*> sorted(fixes);

	// Sort by:
	// 1. JSX namespace fixes last
	// 2. Fix comparison using view.CompareFixes
	std::stable_sort(sorted.begin(), sorted.end(),
	          [&](fixInfo* a, fixInfo* b) {
		          // JSX namespace fixes should come last
		          if (int c = compareBooleans(a->isJsxNamespaceFix,
		                                      b->isJsxNamespaceFix);
		              c != 0) {
			          return c < 0;
		          }
		          return view->CompareFixesForSorting(a->fix.get(), b->fix.get()) < 0;
	          });

	return sorted;
}

} // namespace

// ImportFixProvider — codeactions_importfixes.go:49. The CodeFixProvider for
// import-related fixes.
CodeFixProvider* ImportFixProvider = new CodeFixProvider{
    .ErrorCodes = importFixErrorCodes,
    .GetCodeActions =
        [](const gostd::Context& ctx, CodeFixContext* fixContext) {
	        return getImportCodeActions(ctx, fixContext);
        },
    .FixIds = {importFixID},
    .GetAllCodeActions =
        [](const gostd::Context& ctx, CodeFixContext* fixContext) {
	        return getAllImportCodeActions(ctx, fixContext);
        },
};

} // namespace tsc::ls
