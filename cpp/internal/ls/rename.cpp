// === slice: ls-coreC ===
// rename.cpp — rename.go: prepareRename validation + workspace-edit rename.
#include "internal/astnav/tokens.h"
#include "internal/ls/ls.h"

namespace tsc::ls {

namespace {

// core.NewTextRange
TextRange newTextRange(int pos, int end) {
	return TextRange{static_cast<TextPos>(pos), static_cast<TextPos>(end)};
}

// scope-bound `defer` — Go `defer done()`.
struct Deferred {
	std::function<void()> f;
	~Deferred() { if (f) f(); }
};

// core.Find
template <class T, class F>
T findFirst(const std::vector<T>& v, F f) {
	for (auto& x : v)
		if (f(x)) return x;
	return nullptr;
}

template <class T, class F>
bool everyOf(const std::vector<T>& v, F f) {
	for (auto& x : v)
		if (!f(x)) return false;
	return true;
}

template <class T, class F>
auto flatMapVec(const std::vector<T>& v, F f)
	-> std::vector<decltype(f(v[0]).value_type)> {
	using U = decltype(f(v[0]).value_type);
	std::vector<U> result;
	for (const T& x : v) {
		auto part = f(x);
		result.insert(result.end(), part.begin(), part.end());
	}
	return result;
}

template <class T>
bool containsVec(const std::vector<T>& v, const T& x) {
	return std::find(v.begin(), v.end(), x) != v.end();
}

// --- file-local replicas of ast/utilities.go helpers ---
// ast/utilities.go:2315 — IsLabelOfLabeledStatement
bool isLabelOfLabeledStatement(::tsc::Node* node) {
	if (!isIdentifier(node)) {
		return false;
	}
	if (!isLabeledStatement(node->parent)) {
		return false;
	}
	return node == node->parent->label();
}

// ast/utilities.go:2326 — IsJumpStatementTarget
bool isJumpStatementTarget(::tsc::Node* node) {
	if (!isIdentifier(node)) {
		return false;
	}
	if (!(isBreakStatement(node->parent) ||
		  isContinueStatement(node->parent))) {
		return false;
	}
	return node == node->parent->label();
}

// ast/utilities.go:2312 — IsLabelName
bool isLabelName(::tsc::Node* node) {
	return isLabelOfLabeledStatement(node) || isJumpStatementTarget(node);
}


// rename.go:34 — mappedRenameEdit
struct mappedRenameEdit {
	lsp::lsproto::DocumentUri uri;
	std::shared_ptr<lsp::lsproto::TextEdit> edit;
};

// rename.go:39 — renameEditKey
struct renameEditKey {
	lsp::lsproto::DocumentUri uri;
	lsp::lsproto::Range textRange;
	bool operator==(const renameEditKey&) const = default;
};

} // namespace
} // namespace tsc::ls

namespace std {
template <> struct hash<tsc::ls::renameEditKey> {
	size_t operator()(const tsc::ls::renameEditKey& k) const {
		return std::hash<std::string>{}(k.uri) ^
			   std::hash<tsc::lsp::lsproto::Range>{}(k.textRange) * 131;
	}
};
} // namespace std

namespace tsc::ls {
namespace {

// rename.go:44 — deduplicateRenameEdits
std::pair<std::map<lsp::lsproto::DocumentUri,
				   std::vector<std::shared_ptr<lsp::lsproto::TextEdit>>>,
		  bool>
deduplicateRenameEdits(std::vector<mappedRenameEdit>& mappedEdits) {
	std::unordered_map<renameEditKey, std::string> editTexts;
	std::vector<mappedRenameEdit> uniqueEdits;
	uniqueEdits.reserve(mappedEdits.size());
	for (auto& mappedEdit : mappedEdits) {
		renameEditKey key{mappedEdit.uri, mappedEdit.edit->Range};
		if (auto it = editTexts.find(key); it != editTexts.end()) {
			if (it->second != mappedEdit.edit->NewText) {
				return {std::map<lsp::lsproto::DocumentUri,
								 std::vector<
								     std::shared_ptr<lsp::lsproto::TextEdit>>>{},
						false};
			}
			continue;
		}
		editTexts[key] = mappedEdit.edit->NewText;
		uniqueEdits.push_back(mappedEdit);
	}
	std::map<lsp::lsproto::DocumentUri,
	         std::vector<std::shared_ptr<lsp::lsproto::TextEdit>>>
		changes;
	for (auto& mappedEdit : uniqueEdits) {
		changes[mappedEdit.uri].push_back(mappedEdit.edit);
	}
	return {changes, true};
}


// isDefinedInLibraryFile checks if a declaration is from a default library file (e.g., lib.d.ts).
// rename.go:249
bool isDefinedInLibraryFile(compiler::SimpleProgram* program,
							::tsc::Node* declaration) {
	SourceFile* declSourceFile = getSourceFileOfNode(declaration);
	return program->IsSourceFileDefaultLibrary(declSourceFile->Path()) &&
		   tspath::isDeclarationFileName(declSourceFile->FileName());
}

// wouldRenameInOtherNodeModules checks if renaming the symbol would affect node_modules.
// rename.go:255
const DiagnosticMessage* wouldRenameInOtherNodeModules(
	SourceFile* originalFile, Symbol* symbol, checker::Checker* ch,
	lsutil::UserPreferences preferences) {
	Symbol* sym = symbol;
	if (!tristateIsTrueOrUnknown(preferences.ProvidePrefixAndSuffixTextForRename) &&
		(sym->flags & SymbolFlagsAlias) != 0) {
		::tsc::Node* importSpecifier =
			findFirst(sym->declarations, &isImportSpecifier);
		if (importSpecifier != nullptr &&
			importSpecifier->as<ImportSpecifier>()->PropertyName == nullptr) {
			sym = ch->GetAliasedSymbol(sym);
		}
	}

	std::vector<::tsc::Node*> declarations = sym->declarations;
	if (declarations.empty()) {
		return nullptr;
	}

	std::string originalPackage = module::NodeModulePackageRootForFile(originalFile->FileName());
	if (originalPackage.empty()) {
		// Original source file is not in node_modules.
		for (auto* declaration : declarations) {
			if (isInsideNodeModules(
					getSourceFileOfNode(declaration)->FileName())) {
				return 
					You_cannot_rename_elements_that_are_defined_in_a_node_modules_folder;
			}
		}
		return nullptr;
	}

	// Original source file is in node_modules.
	for (auto* declaration : declarations) {
		std::string declPackage = module::NodeModulePackageRootForFile(
			getSourceFileOfNode(declaration)->FileName());
		if (!declPackage.empty() && declPackage != originalPackage) {
			return 
				You_cannot_rename_elements_that_are_defined_in_another_node_modules_folder;
		}
	}
	return nullptr;
}

// rename.go:430 — getRenameInfoError
RenameInfo getRenameInfoError(gostd::Context ctx,
							  const DiagnosticMessage* message) {
	return RenameInfo{
		/*.CanRename=*/false,
		/*.LocalizedErrorMessage=*/
		localize(locale::fromContext(tsc::ContextPtr{}), message, message->key,
							  {}),
	};
}

// rename.go:437 — getRenameInfoSuccess
RenameInfo getRenameInfoSuccess(::tsc::Node* node, SourceFile* sourceFile,
								std::string displayName,
								lsconv::Converters* converters) {
	int start = astnav::getStartOfNode(node, sourceFile, false /*includeJSDoc*/);
	int end = node->end();
	if (isStringLiteralLike(node)) {
		// Exclude the quotes
		start++;
		end--;
	}
	auto [triggerSpan, fidelity] =
		converters->ToLSPRange(sourceFile, newTextRange(start, end));
	if (!fidelity.IsExact()) {
		return RenameInfo{/*.CanRename=*/false};
	}
	return RenameInfo{
		/*.CanRename=*/true,
		/*.LocalizedErrorMessage=*/"",
		/*.DisplayName=*/displayName,
		/*.TriggerSpan=*/triggerSpan,
	};
}

// rename.go:423 — getQuoteFromPreference
std::string getQuoteFromPreference(lsutil::QuotePreference quotePreference) {
	if (quotePreference == lsutil::QuotePreferenceSingle) {
		return "'";
	}
	return "\"";
}

} // namespace

// rename.go:290 — ClientSupportsWillRenameFiles
bool ClientSupportsWillRenameFiles(gostd::Context ctx) {
	return lsp::lsproto::getClientCapabilities(ctx)
		->Workspace.FileOperations.WillRename;
}

// rename.go:294 — ClientSupportsDocumentChanges
bool ClientSupportsDocumentChanges(gostd::Context ctx) {
	return lsp::lsproto::getClientCapabilities(ctx)
		->Workspace.WorkspaceEdit.DocumentChanges;
}

// rename.go:209 — nodeIsEligibleForRename
bool nodeIsEligibleForRename(::tsc::Node* node) {
	if (node == nullptr) {
		return false;
	}
	switch (node->kind) {
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::ThisKeyword:
		return true;
	case Kind::NumericLiteral:
		return isLiteralNameOfPropertyDeclarationOrIndexAccess(node);
	default:
		return false;
	}
}

// rename.go:298 — ClientSupportsRenameResourceOperations
bool ClientSupportsRenameResourceOperations(gostd::Context ctx) {
	auto ops = lsp::lsproto::getClientCapabilities(ctx)
	               ->Workspace.WorkspaceEdit.ResourceOperations;
	return ops.has_value() &&
	       containsVec(*ops, lsp::lsproto::ResourceOperationKindRename);
}

// ============================================================================
// rename.go — ProvideRename / GetRenameInfo / symbolAndEntriesToRename
// ============================================================================
// rename.go:65
std::pair<lsp::lsproto::WorkspaceEditOrNull, gostd::Error>
LanguageService::ProvideRename(
	gostd::Context ctx, lsp::lsproto::RenameParams* params,
	CrossProjectOrchestrator* orchestrator) {
	return handleCrossProject<lsp::lsproto::RenameParams,
	                          lsp::lsproto::WorkspaceEditOrNull>(
		ctx,
		params,
		orchestrator,
		[](LanguageService* l, const gostd::Context& ctx,
		   lsp::lsproto::RenameParams* params, SymbolAndEntriesData data,
		   symbolEntryTransformOptions options) {
			return l->symbolAndEntriesToRename(ctx, params, data, options);
		},
		&combineRenameResponse,
		true,  /*isRename*/
		false, /*implementations*/
		symbolEntryTransformOptions{},
		nullptr /*defaultProjectData*/);
}

// rename.go:79
RenameInfo LanguageService::GetRenameInfo(
	gostd::Context ctx, std::string newName, lsp::lsproto::DocumentUri documentURI,
	lsp::lsproto::Position position) {
	auto [program, sourceFile] = getProgramAndFile(documentURI);
	auto positions = converters->FromLSPPositionForSourceFile(
		sourceFile, position, spanmap::FeatureRename);
	for (auto& mapped : positions) {
		if (!mapped.Fidelity.IsExact()) {
			continue;
		}
		sourceFile = mapped.Script;
		::tsc::Node* node =
			astnav::getTouchingPropertyName(sourceFile, int(mapped.Position));
		node = getAdjustedLocation(node, true /*forRename*/, sourceFile);
		if (nodeIsEligibleForRename(node)) {
			if (auto [renameInfo, ok] =
					getRenameInfoForNode(ctx, newName, node, sourceFile, program);
				ok) {
				return renameInfo;
			}
		}
	}
	return getRenameInfoError(ctx, You_cannot_rename_this_element);
}

// rename.go:98
std::pair<lsp::lsproto::WorkspaceEditOrNull, gostd::Error>
LanguageService::symbolAndEntriesToRename(gostd::Context ctx,
										  lsp::lsproto::RenameParams* params,
										  SymbolAndEntriesData data,
										  symbolEntryTransformOptions options) {
	if (!nodeIsEligibleForRename(data.OriginalNode)) {
		return {lsp::lsproto::WorkspaceEditOrNull{}, nullptr};
	}

	compiler::SimpleProgram* program = GetProgram();

	// Defense-in-depth: validate rename eligibility even if the client skipped prepareRename.
	// Use getRenameInfoForNode directly with the already-resolved node to avoid
	// re-resolving the position and polluting state baselines.
	SourceFile* sourceFile = getSourceFileOfNode(data.OriginalNode);
	if (auto [info, ok] = getRenameInfoForNode(ctx, params->NewName,
											  data.OriginalNode, sourceFile,
											  program);
		!ok || !info.CanRename) {
		return {lsp::lsproto::WorkspaceEditOrNull{}, nullptr};
	}

	std::vector<ReferenceEntry*> entries;
	for (auto* s : data.SymbolsAndEntries) {
		entries.insert(entries.end(), s->references.begin(),
					   s->references.end());
	}
	std::vector<mappedRenameEdit> mappedEdits;
	auto [ch, done] = program->GetTypeChecker(ctx);
	Deferred _done{done};

	lsutil::QuotePreference quotePreference =
		lsutil::GetQuotePreference(sourceFile, UserPreferences());
	bool useAliasesForRename =
		tristateIsTrueOrUnknown(UserPreferences().ProvidePrefixAndSuffixTextForRename);

	for (auto* entry : entries) {
		lsp::lsproto::DocumentUri uri = getFileNameOfEntry(entry);
		if (UserPreferences().AllowRenameOfImportPath != Tristate::True &&
			entry->node != nullptr && isStringLiteralLike(entry->node) &&
			tryGetImportFromModuleSpecifier(entry->node) != nullptr) {
			continue;
		}
		auto [rng, ok] = renameEditRange(entry);
		if (!ok) {
			// The occurrence lies outside a verbatim span of a content-mapped file, so it cannot be
			// written back to the original text. Skip it and keep renaming the remaining occurrences.
			continue;
		}
		auto textEdit = std::make_shared<lsp::lsproto::TextEdit>();
		textEdit->Range = rng;
		textEdit->NewText = getTextForRename(data.OriginalNode, entry,
											 params->NewName, ch, quotePreference,
											 useAliasesForRename);
		mappedEdits.push_back(mappedRenameEdit{uri, std::move(textEdit)});
	}
	auto [changes, ok] = deduplicateRenameEdits(mappedEdits);
	if (!ok) {
		return {lsp::lsproto::WorkspaceEditOrNull{}, nullptr};
	}
	lsp::lsproto::WorkspaceEditOrNull res;
	res.WorkspaceEdit = std::make_shared<lsp::lsproto::WorkspaceEdit>();
	lsp::lsproto::Map<lsp::lsproto::DocumentUri,
	                  lsp::lsproto::Slice<
	                      std::shared_ptr<lsp::lsproto::TextEdit>>>
	    changesMap;
	for (auto& [uri, edits] : changes) {
		changesMap[uri] = lsp::lsproto::Slice<
		    std::shared_ptr<lsp::lsproto::TextEdit>>(std::move(edits));
	}
	res.WorkspaceEdit->Changes =
	    std::make_shared<lsp::lsproto::Map<
	        lsp::lsproto::DocumentUri,
	        lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>>>>(
	        std::move(changesMap));
	return {res, nullptr};
}

// renameEditRange returns the LSP range at which a rename occurrence should be edited. For occurrences in
// content-mapped files it maps the transformed range strictly, returning ok=false when the occurrence is
// not fully within a single verbatim span, so the caller can skip an edit that cannot be applied to the
// original text.
// rename.go:153
std::pair<lsp::lsproto::Range, bool> LanguageService::renameEditRange(
	ReferenceEntry* entry) {
	resolveEntry(entry);
	if (entry->node == nullptr) {
		auto [location, fidelity] =
			sourceFileRangeToLSPLocation(entry->sourceFile, *entry->textRange);
		return {location.Range, fidelity.IsExact()};
	}
	SourceFile* sourceFile = getSourceFileOfNode(entry->node);
	if (sourceFile == nullptr || sourceFile->SpanMap() == nullptr) {
		return {getRangeOfEntry(entry), true};
	}
	auto [lspRange, fidelity] = converters->ToLSPRange(sourceFile, *entry->textRange);
	return {lspRange, fidelity.IsExact()};
}

// getRenameInfoForNode performs detailed validation for a rename operation on a specific node.
// rename.go:168
std::pair<RenameInfo, bool> LanguageService::getRenameInfoForNode(
	gostd::Context ctx, std::string newName, ::tsc::Node* node,
	SourceFile* sourceFile, compiler::SimpleProgram* program) {
	auto [ch, done] = program->GetTypeChecker(ctx);
	Deferred _done{done};

	Symbol* symbol = ch->GetSymbolAtLocation(node);
	if (symbol == nullptr) {
		if (isStringLiteralLike(node)) {
			// Allow renaming of string literal types with contextual string literal types
			checker::Type* typ =
				getContextualTypeFromParentOrAncestorTypeNode(node, ch);
			if (typ != nullptr &&
				(typ->IsStringLiteral() ||
				 (typ->IsUnion() &&
				  everyOf(typ->types(), [](checker::Type* t) {
					  return t->IsStringLiteral();
				  })))) {
				return {getRenameInfoSuccess(node, sourceFile, node->text(),
											 converters),
						true};
			}
		} else if (isLabelName(node)) {
			std::string name = node->text();
			return {getRenameInfoSuccess(node, sourceFile, name, converters), true};
		}
		return {RenameInfo{}, false};
	}

	// Only allow a symbol to be renamed if it actually has at least one declaration.
	if (symbol->declarations.empty()) {
		return {RenameInfo{}, false};
	}

	if (const DiagnosticMessage* msg =
			renameBlockedReason(sourceFile, node, symbol, ch, program);
		msg != nullptr) {
		return {getRenameInfoError(ctx, msg), true};
	}

	if (isStringLiteralLike(node) &&
		tryGetImportFromModuleSpecifier(node) != nullptr) {
		if (tristateIsTrue(UserPreferences().AllowRenameOfImportPath)) {
			return getRenameInfoForModule(ctx, newName, node, sourceFile, symbol);
		}
		return {RenameInfo{}, false};
	}

	return {getRenameInfoSuccess(node, sourceFile, ch->SymbolToString(symbol),
								 converters),
			true};
}

// renameBlockedReason returns a non-nil diagnostic message if the rename should be blocked
// because the symbol is a library definition, a default keyword, or would cross node_modules boundaries.
// rename.go:229
const DiagnosticMessage* LanguageService::renameBlockedReason(
	SourceFile* sourceFile, ::tsc::Node* node, Symbol* symbol,
	checker::Checker* ch, compiler::SimpleProgram* program) {
	for (auto* declaration : symbol->declarations) {
		if (isDefinedInLibraryFile(program, declaration)) {
			return 
				You_cannot_rename_elements_that_are_defined_in_the_standard_TypeScript_library;
		}
	}

	// Cannot rename `default` as in `import { default as foo } from "./someModule"`
	if (isIdentifier(node) && node->text() == "default" &&
		symbol->parent != nullptr &&
		(symbol->parent->flags & SymbolFlagsModule) != 0) {
		return You_cannot_rename_this_element;
	}

	if (const DiagnosticMessage* msg =
			wouldRenameInOtherNodeModules(sourceFile, symbol, ch,
										  UserPreferences());
		msg != nullptr) {
		return msg;
	}

	return nullptr;
}

// getRenameInfoForModule handles rename validation for module specifiers.
// rename.go:303
std::pair<RenameInfo, bool> LanguageService::getRenameInfoForModule(
	gostd::Context ctx, std::string newName, ::tsc::Node* specifier,
	SourceFile* sourceFile, Symbol* moduleSymbol) {
	if (!tspath::isExternalModuleNameRelative(specifier->text())) {
		return {getRenameInfoError(
					ctx, You_cannot_rename_a_module_via_a_global_import),
				true};
	}
	if (!ClientSupportsDocumentChanges(ctx) ||
		!ClientSupportsRenameResourceOperations(ctx)) {
		return {getRenameInfoError(
					ctx, File_rename_is_not_supported_by_the_editor),
				true};
	}

	::tsc::Node* moduleSourceFile =
		findFirst(moduleSymbol->declarations, &isSourceFile);
	if (moduleSourceFile == nullptr) {
		return {RenameInfo{}, false};
	}

	std::string fileName = moduleSourceFile->as<SourceFile>()->FileName();
	std::string withoutIndex;
	if (!tspath::endsWith(specifier->text(), "/index") &&
		!tspath::endsWith(specifier->text(), "/index.js")) {
		std::string candidate{tspath::removeFileExtension(fileName)};
		if (tspath::endsWith(candidate, "/index")) {
			withoutIndex = candidate.substr(0, candidate.size() - 6);
		}
	}

	std::string displayName = fileName;
	if (!withoutIndex.empty()) {
		displayName = withoutIndex;
	}
	std::string newFileName =
		getNewFileNameForModuleRename(displayName, specifier->text(), newName);

	// Span should only be the last component of the path. + 1 to account for the quote character.
	size_t lastSlash = specifier->text().rfind('/');
	int indexAfterLastSlash =
		(lastSlash == std::string::npos ? -1 : int(lastSlash)) + 1;
	int start = astnav::getStartOfNode(specifier, sourceFile,
									 false /*includeJSDoc*/) +
				1 + indexAfterLastSlash;
	int length = int(specifier->text().size()) - indexAfterLastSlash;

	auto [triggerSpan, fidelity] =
		converters->ToLSPRange(sourceFile, newTextRange(start, start + length));
	if (!fidelity.IsExact()) {
		return {RenameInfo{}, false};
	}
	return {RenameInfo{
				/*.CanRename=*/true,
				/*.LocalizedErrorMessage=*/"",
				/*.DisplayName=*/specifier->text().substr(indexAfterLastSlash),
				/*.TriggerSpan=*/triggerSpan,
				/*.FileToRename=*/displayName,
				/*.NewFileName=*/newFileName,
			},
			true};
}

// Adjust the new name based on the old path that an import specifier resolves to.
// For example, if specifier "a.js" resolves to file a.ts, renaming "a.js" -> "b.js" should mean file rename a.ts -> b.ts.
// rename.go:351
std::string LanguageService::getNewFileNameForModuleRename(
	std::string oldPath, std::string specifierText, std::string newName) {
	std::string newPath =
		tspath::combinePaths(tspath::getDirectoryPath(oldPath), {newName});
	bool ignoreCase = !host->UseCaseSensitiveFileNames();
	std::string oldExt;
	if (tspath::isDeclarationFileName(oldPath)) {
		oldExt = std::string{tspath::getDeclarationFileExtension(oldPath)};
	} else {
		oldExt = std::string{tspath::getAnyExtensionFromPath(
			oldPath, nullptr /*extensions*/, ignoreCase)};
	}
	if (!tspath::hasExtension(newPath)) {
		newPath = newPath + oldExt;
	} else if (tspath::getAnyExtensionFromPath(newPath, nullptr /*extensions*/,
											   ignoreCase) ==
			   tspath::getAnyExtensionFromPath(specifierText,
											   nullptr /*extensions*/,
											   ignoreCase)) {
		newPath = tspath::changeAnyExtension(newPath, oldExt,
											 {} /*extensions*/, ignoreCase);
	}
	return newPath;
}

// rename.go:368 — getTextForRename
std::string LanguageService::getTextForRename(::tsc::Node* originalNode,
											  ReferenceEntry* entry,
											  std::string newText,
											  checker::Checker* ch,
											  lsutil::QuotePreference quotePreference,
											  bool useAliasesForRename) {
	if (useAliasesForRename && entry->kind != entryKindRange &&
		(isIdentifier(originalNode) ||
		 isStringLiteralLike(originalNode))) {
		::tsc::Node* node = getReparsedNodeForNode(entry->node);
		entryKind kind = entry->kind;
		::tsc::Node* parent = node->parent;
		std::string name = originalNode->text();
		bool isShorthandAssignment = isShorthandPropertyAssignment(parent);
		if (isShorthandAssignment ||
			(isObjectBindingElementWithoutPropertyName(parent) &&
			 parent->name() == node &&
			 parent->as<BindingElement>()->DotDotDotToken == nullptr)) {
			if (kind == entryKindSearchedLocalFoundProperty) {
				return name + ": " + newText;
			}
			if (kind == entryKindSearchedPropertyFoundLocal) {
				return newText + ": " + name;
			}
			// In `const o = { x }; o.x`, symbolAtLocation at `x` in `{ x }` is the property symbol.
			// For a binding element `const { x } = o;`, symbolAtLocation at `x` is the property symbol.
			if (isShorthandAssignment) {
				::tsc::Node* grandParent = parent->parent;
				if (isObjectLiteralExpression(grandParent) &&
					isBinaryExpression(grandParent->parent) &&
					isModuleExportsAccessExpression(
						grandParent->parent->as<BinaryExpression>()->Left)) {
					return name + ": " + newText;
				}
				return newText + ": " + name;
			}
			return name + ": " + newText;
		} else if (isImportSpecifier(parent) && parent->propertyName() == nullptr) {
			// If the original symbol was using this alias, just rename the alias.
			Symbol* originalSymbol = nullptr;
			if (isExportSpecifier(originalNode->parent)) {
				originalSymbol = ch->GetExportSpecifierLocalTargetSymbol(
					originalNode->parent);
			} else {
				originalSymbol = ch->GetSymbolAtLocation(originalNode);
			}
			if (originalSymbol != nullptr &&
				containsVec(originalSymbol->declarations, parent)) {
				return name + " as " + newText;
			}
			return newText;
		} else if (isExportSpecifier(parent) &&
				   parent->propertyName() == nullptr) {
			// If the symbol for the node is same as declared node symbol use prefix text
			if (originalNode == entry->node ||
				ch->GetSymbolAtLocation(originalNode) ==
					ch->GetSymbolAtLocation(entry->node)) {
				return name + " as " + newText;
			}
			return newText + " as " + name;
		}
	}

	// If the node is a numerical indexing literal, then add quotes around the property access.
	if (entry->kind != entryKindRange && isNumericLiteral(entry->node) &&
		isAccessExpression(entry->node->parent)) {
		std::string quote = getQuoteFromPreference(quotePreference);
		return quote + newText + quote;
	}

	return newText;
}

} // namespace tsc::ls
