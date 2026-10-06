// import_adder.go — ImportAdder: accumulates import fixes into edits.
#include <algorithm>
#include <set>

#include "internal/ast/visitor.h"
#include "internal/debug/debug.h"
#include "internal/ls/change/change.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/locale/locale.h"

namespace tsc::ls::autoimport {

// newImportsKey — import_adder.go:46
std::string newImportsKey(const std::string& moduleSpecifier,
                          bool topLevelTypeOnly) {
	if (topLevelTypeOnly) {
		return "1|" + moduleSpecifier;
	}
	return "0|" + moduleSpecifier;
}

// NewImportAdder — import_adder.go:70
std::unique_ptr<importAdder> NewImportAdder(
    gostd::Context ctx, compiler::SimpleProgram* program, checker::Checker* ch,
    SourceFile* file, View* view,
    const lsutil::FormatCodeSettings& formatOptions,
    lsconv::Converters* converters,
    const lsutil::UserPreferences& preferences) {
	(void)program;
	(void)file;
	auto adder = std::make_unique<importAdder>();
	adder->ctx = ctx;
	adder->checker = ch;
	adder->view = view;
	adder->formatOptions = formatOptions;
	adder->converters = converters;
	adder->preferences = preferences;
	return adder;
}

// importAdder::HasFixes — import_adder.go:94
bool importAdder::HasFixes() {
	return !addToNamespace.empty() || !importType.empty() ||
	       !addToExisting.empty() || !newImports.empty();
}

// importAdder::AddImportFromExportedSymbol — import_adder.go:102
void importAdder::AddImportFromExportedSymbol(Symbol* exportedSymbol,
                                              bool isValidTypeOnlyUseSite) {
	Symbol* symbol =
	    checker->GetMergedSymbol(checker->SkipAlias(exportedSymbol));
	std::vector<Export*> exportInfos = getAllExportsForSymbol(symbol);
	if (exportInfos.empty()) {
		// If no exportInfo is found, this means export could not be resolved when we have filtered for autoImportFileExcludePatterns,
		//     so we should not generate an import.
		// debug.Assert(len(adder.ls.UserPreferences().AutoImportFileExcludePatterns) > 0)
		return;
	}
	if (auto fix =
	        getImportFixForSymbol(view, exportInfos, isValidTypeOnlyUseSite)) {
		// !!! referenceImport -> propertyName
		AddImportFix(std::move(fix));
	}
}

// importAdder::Edits — import_adder.go:118
std::vector<lsp::lsproto::TextEdit*> importAdder::Edits() {
	// !!! organize imports?
	std::unique_ptr<ls::change::Tracker> tracker =
	    std::make_unique<ls::change::Tracker>(
	        format::FormatRequestContext{}, view->program->Options(),
	        formatOptions, converters);
	lsutil::QuotePreference quotePreference =
	    lsutil::GetQuotePreference(view->importingFile, preferences);
	for (Fix* fix : addToNamespace) {
		addNamespaceQualifier(fix, tracker.get(), view->importingFile,
		                      locale::Default);
	}
	for (Fix* fix : importType) {
		addImportType(fix, view->importingFile, preferences, tracker.get(),
		              locale::Default);
	}
	for (auto& [clauseOrPattern, entry] : addToExisting) {
		addToExistingImport(tracker.get(), view->importingFile, clauseOrPattern,
		                    entry->defaultImport.get(),
		                    sortedNamedImports(entry->namedImports), preferences);
	}

	std::vector<Node*> newDeclarations;
	for (auto& [key, newImport] : newImports) {
		std::string moduleSpecifier = key.substr(2); // From `${0 | 1}|${moduleSpecifier}` format
		std::vector<Node*> declarations;
		if (newImport->useRequire) {
			declarations = getNewRequires(
			    tracker.get(), moduleSpecifier, quotePreference,
			    newImport->defaultImport.get(),
			    sortedNamedImports(newImport->namedImports),
			    newImport->namespaceLikeImport.get(),
			    view->program->Options());
		} else {
			declarations = getNewImports(
			    tracker.get(), moduleSpecifier, quotePreference,
			    newImport->defaultImport.get(),
			    sortedNamedImports(newImport->namedImports),
			    newImport->namespaceLikeImport.get(),
			    view->program->Options(), preferences);
		}
		newDeclarations.insert(newDeclarations.end(), declarations.begin(),
		                       declarations.end());
	}

	if (!newDeclarations.empty()) {
		insertImports(tracker.get(), view->importingFile, newDeclarations,
		              true /*blankLineBetween*/, preferences);
	}

	// Unmappable files are dropped by GetChanges, so a content-mapped importing file that cannot be
	// faithfully rewritten yields no edits rather than a corrupting one.
	auto changes = tracker->GetChanges().first;
	auto it = changes.find(view->importingFile->OriginalFileName());
	if (it == changes.end()) {
		return {};
	}
	// Go shares heap []*lsproto.TextEdit (GC lifetime); materialize the same
	// ownership here — the tracker is discarded after GetChanges.
	std::vector<lsp::lsproto::TextEdit*> edits;
	edits.reserve(it->second.size());
	for (auto& e : it->second) {
		edits.push_back(new lsp::lsproto::TextEdit(std::move(e)));
	}
	return edits;
}

// sortedNamedImports — import_adder.go:178
std::vector<newImportBinding*> sortedNamedImports(
    const std::unordered_map<std::string, std::unique_ptr<newImportBinding>>&
        m) {
	std::vector<std::string> keys;
	keys.reserve(m.size());
	for (auto& kv : m) {
		keys.push_back(kv.first);
	}
	std::sort(keys.begin(), keys.end());
	std::vector<newImportBinding*> result;
	result.reserve(keys.size());
	for (const auto& k : keys) {
		result.push_back(m.at(k).get());
	}
	return result;
}

// importAdder::AddImportFix — import_adder.go:189. Adds a fix to the import
// adder, accumulating it with other fixes so that multiple imports from the
// same module are coalesced into a single import statement.
void importAdder::AddImportFix(std::unique_ptr<Fix> fix) {
	Fix* f = fix.get();
	ownedFixes.push_back(std::move(fix));
	const std::string& symbolName = f->AutoImportFix->Name;
	const CompilerOptions* compilerOptions = view->program->Options();

	switch (f->AutoImportFix->Kind) {
		case lsp::lsproto::AutoImportFixKindUseNamespace:
			addToNamespace.push_back(f);
			break;
		case lsp::lsproto::AutoImportFixKindJsdocTypeImport:
			importType.push_back(f);
			break;
		case lsp::lsproto::AutoImportFixKindAddToExisting: {
			std::unique_ptr<addToExistingImportFix> existingFix =
			    getAddToExistingImportFix(view->importingFile, f);
			addToExistingState* entry = nullptr;
			if (auto it = addToExisting.find(
			        existingFix->importClauseOrBindingPattern);
			    it != addToExisting.end()) {
				entry = it->second.get();
			} else {
				auto e = std::make_unique<addToExistingState>();
				e->importClauseOrBindingPattern =
				    existingFix->importClauseOrBindingPattern;
				entry = e.get();
				addToExisting.emplace(
				    existingFix->importClauseOrBindingPattern, std::move(e));
			}

			if (f->AutoImportFix->ImportKind == lsp::lsproto::ImportKindNamed) {
				lsp::lsproto::AddAsTypeOnly prevTypeOnly{};
				if (auto it2 = entry->namedImports.find(symbolName);
				    it2 != entry->namedImports.end()) {
					prevTypeOnly = it2->second->addAsTypeOnly;
				}
				auto binding = std::make_unique<newImportBinding>();
				binding->kind = lsp::lsproto::ImportKindNamed;
				binding->name = symbolName;
				binding->addAsTypeOnly = reduceAddAsTypeOnlyValues(
				    prevTypeOnly, f->AutoImportFix->AddAsTypeOnly);
				binding->propertyName = existingFix->namedImport->propertyName;
				entry->namedImports[symbolName] = std::move(binding);
			} else {
				// Default import
				debug::assert(
				    entry->defaultImport == nullptr ||
				        entry->defaultImport->name == symbolName,
				    "(Add to Existing) Default import should be missing or match "
				    "symbolName");
				lsp::lsproto::AddAsTypeOnly prevTypeOnly{};
				if (entry->defaultImport != nullptr) {
					prevTypeOnly = entry->defaultImport->addAsTypeOnly;
				}
				auto binding = std::make_unique<newImportBinding>();
				binding->kind = lsp::lsproto::ImportKindDefault;
				binding->name = symbolName;
				binding->addAsTypeOnly = reduceAddAsTypeOnlyValues(
				    prevTypeOnly, f->AutoImportFix->AddAsTypeOnly);
				entry->defaultImport = std::move(binding);
			}
			break;
		}
		case lsp::lsproto::AutoImportFixKindAddNew: {
			importsCollection* entry = getNewImportEntry(
			    f->AutoImportFix->ModuleSpecifier, f->AutoImportFix->ImportKind,
			    f->AutoImportFix->UseRequire, f->AutoImportFix->AddAsTypeOnly);
			debug::assert(
			    entry->useRequire == f->AutoImportFix->UseRequire,
			    "(Add new) Tried to add an `import` and a `require` for the "
			    "same module");

			switch (f->AutoImportFix->ImportKind) {
				case lsp::lsproto::ImportKindDefault: {
					debug::assert(
					    entry->defaultImport == nullptr ||
					        entry->defaultImport->name == symbolName,
					    "(Add new) Default import should be missing or match "
					    "symbolName");
					lsp::lsproto::AddAsTypeOnly prevTypeOnly{};
					if (entry->defaultImport != nullptr) {
						prevTypeOnly = entry->defaultImport->addAsTypeOnly;
					}
					auto binding = std::make_unique<newImportBinding>();
					binding->kind = lsp::lsproto::ImportKindDefault;
					binding->name = symbolName;
					binding->addAsTypeOnly = reduceAddAsTypeOnlyValues(
					    prevTypeOnly, f->AutoImportFix->AddAsTypeOnly);
					entry->defaultImport = std::move(binding);
					break;
				}
				case lsp::lsproto::ImportKindNamed: {
					lsp::lsproto::AddAsTypeOnly prevTypeOnly{};
					if (auto it = entry->namedImports.find(symbolName);
					    it != entry->namedImports.end()) {
						prevTypeOnly = it->second->addAsTypeOnly;
					}
					auto binding = std::make_unique<newImportBinding>();
					binding->kind = lsp::lsproto::ImportKindNamed;
					binding->name = symbolName;
					binding->addAsTypeOnly = reduceAddAsTypeOnlyValues(
					    prevTypeOnly, f->AutoImportFix->AddAsTypeOnly);
					// !!! propertyName
					entry->namedImports[symbolName] = std::move(binding);
					break;
				}
				case lsp::lsproto::ImportKindCommonJS: {
					if (compilerOptions->VerbatimModuleSyntax ==
					    Tristate::True) {
						lsp::lsproto::AddAsTypeOnly prevTypeOnly{};
						if (auto it = entry->namedImports.find(symbolName);
						    it != entry->namedImports.end()) {
							prevTypeOnly = it->second->addAsTypeOnly;
						}
						auto binding = std::make_unique<newImportBinding>();
						binding->kind = lsp::lsproto::ImportKindCommonJS;
						binding->name = symbolName;
						binding->addAsTypeOnly = reduceAddAsTypeOnlyValues(
						    prevTypeOnly, f->AutoImportFix->AddAsTypeOnly);
						// !!! propertyName
						entry->namedImports[symbolName] = std::move(binding);
					} else {
						debug::assert(
						    entry->namespaceLikeImport == nullptr ||
						        entry->namespaceLikeImport->name == symbolName,
						    "Namespacelike import should be missing or match "
						    "symbolName");
						auto binding = std::make_unique<newImportBinding>();
						binding->kind = lsp::lsproto::ImportKindCommonJS;
						binding->name = symbolName;
						binding->addAsTypeOnly =
						    f->AutoImportFix->AddAsTypeOnly;
						entry->namespaceLikeImport = std::move(binding);
					}
					break;
				}
				case lsp::lsproto::ImportKindNamespace: {
					debug::assert(
					    entry->namespaceLikeImport == nullptr ||
					        entry->namespaceLikeImport->name == symbolName,
					    "Namespacelike import should be missing or match "
					    "symbolName");
					auto binding = std::make_unique<newImportBinding>();
					binding->kind = lsp::lsproto::ImportKindNamespace;
					binding->name = symbolName;
					binding->addAsTypeOnly = f->AutoImportFix->AddAsTypeOnly;
					entry->namespaceLikeImport = std::move(binding);
					break;
				}
			}
			break;
		}
		case lsp::lsproto::AutoImportFixKindPromoteTypeOnly:
			// Excluding from fix-all
			break;
		default:
			debug::fail("Unexpected fix kind: " +
			            std::to_string(static_cast<int>(
			                f->AutoImportFix->Kind)));
	}
}

// `NotAllowed` overrides `Required` because one addition of a new import might be required to be type-only
// because of `--importsNotUsedAsValues=error`, but if a second addition of the same import is `NotAllowed`
// to be type-only, the reason the first one was `Required` - the unused runtime dependency - is now moot.
// Alternatively, if one addition is `Required` because it has no value meaning under `--preserveValueImports`
// and `--isolatedModules`, it should be impossible for another addition to be `NotAllowed` since that would
// mean a type is being referenced in a value location.
// reduceAddAsTypeOnlyValues — import_adder.go:330
lsp::lsproto::AddAsTypeOnly reduceAddAsTypeOnlyValues(
    lsp::lsproto::AddAsTypeOnly prevValue, lsp::lsproto::AddAsTypeOnly newValue) {
	if (static_cast<int>(newValue) > static_cast<int>(prevValue)) {
		return newValue;
	}
	return prevValue;
}

// importAdder::getNewImportEntry — import_adder.go:337
importsCollection* importAdder::getNewImportEntry(
    const std::string& moduleSpecifier, lsp::lsproto::ImportKind importKind,
    bool useRequire, lsp::lsproto::AddAsTypeOnly addAsTypeOnly) {
	// A default import that requires type-only makes the whole import type-only.
	// (We could add `default` as a named import, but that style seems undesirable.)
	// Under `--preserveValueImports` and `--importsNotUsedAsValues=error`, if a
	// module default-exports a type but named-exports some values (weird), you would
	// have to use a type-only default import and non-type-only named imports. These
	// require two separate import declarations, so we build this into the map key.
	std::string typeOnlyKey = newImportsKey(moduleSpecifier, true);
	std::string nonTypeOnlyKey = newImportsKey(moduleSpecifier, false);
	importsCollection* typeOnlyEntry = nullptr;
	if (auto it = newImports.find(typeOnlyKey); it != newImports.end()) {
		typeOnlyEntry = it->second.get();
	}
	importsCollection* nonTypeOnlyEntry = nullptr;
	if (auto it = newImports.find(nonTypeOnlyKey); it != newImports.end()) {
		nonTypeOnlyEntry = it->second.get();
	}
	auto newEntry = std::make_unique<importsCollection>();
	newEntry->useRequire = useRequire;

	if (importKind == lsp::lsproto::ImportKindDefault &&
	    addAsTypeOnly == lsp::lsproto::AddAsTypeOnlyRequired) {
		if (typeOnlyEntry != nullptr) {
			return typeOnlyEntry;
		}
		importsCollection* result = newEntry.get();
		newImports[typeOnlyKey] = std::move(newEntry);
		return result;
	}

	if (addAsTypeOnly == lsp::lsproto::AddAsTypeOnlyAllowed &&
	    (typeOnlyEntry != nullptr || nonTypeOnlyEntry != nullptr)) {
		if (typeOnlyEntry != nullptr) {
			return typeOnlyEntry;
		}
		return nonTypeOnlyEntry;
	}

	if (nonTypeOnlyEntry != nullptr) {
		return nonTypeOnlyEntry;
	}

	importsCollection* result = newEntry.get();
	newImports[nonTypeOnlyKey] = std::move(newEntry);
	return result;
}

// importAdder::getAllExportsForSymbol — import_adder.go:375
std::vector<Export*> importAdder::getAllExportsForSymbol(Symbol* symbol) {
	if (auto e = SymbolToExport(symbol, checker)) {
		return view->SearchByExportID(e->exportID);
	}
	return {};
}

// TypeToAutoImportableTypeNode — import_adder.go:385
Node* TypeToAutoImportableTypeNode(checker::Checker* c,
                                   ImportAdder* importAdder, checker::Type* t,
                                   Node* contextNode /* !!! flags */) {
	std::unordered_map<Node*, Symbol*> idToSymbol;
	Node* typeNode = c->TypeToTypeNode(t, contextNode, nodebuilder::FlagsNone,
	                                 &idToSymbol);
	if (typeNode == nullptr) {
		return nullptr;
	}
	return TypeNodeToAutoImportableTypeNode(typeNode, importAdder, &idToSymbol);
}

// TypeNodeToAutoImportableTypeNode — import_adder.go:398. Converts import type
// references in a type node to simple type references and registers needed
// imports with the import adder.
Node* TypeNodeToAutoImportableTypeNode(
    Node* typeNode, ImportAdder* importAdder,
    std::unordered_map<Node*, Symbol*>* idToSymbol) {
	auto [referenceTypeNode, importableSymbols] =
	    TryGetAutoImportableReferenceFromTypeNode(typeNode, idToSymbol);
	if (referenceTypeNode != nullptr) {
		if (importAdder != nullptr) {
			importSymbols(importAdder, importableSymbols);
		}
		typeNode = referenceTypeNode;
	}

	// !!! handle type node reuse: nodes needs to be fresh here but also preserve symbols
	return typeNode;
}

// importSymbols — import_adder.go:417
void importSymbols(ImportAdder* importAdder,
                   const std::vector<Symbol*>& symbols) {
	for (Symbol* symbol : symbols) {
		importAdder->AddImportFromExportedSymbol(
		    symbol, true /*isValidTypeOnlyUseSite*/);
	}
}

// TryGetAutoImportableReferenceFromTypeNode — import_adder.go:423.
// Given a type node containing 'import("./a").SomeType<import("./b").OtherType<...>>',
// returns an equivalent type reference node with any nested ImportTypeNodes also replaced
// with type references, and a list of symbols that must be imported to use the type reference.
std::pair<Node*, std::vector<Symbol*>>
TryGetAutoImportableReferenceFromTypeNode(
    Node* importTypeNode, std::unordered_map<Node*, Symbol*>* idToSymbol) {
	std::vector<Symbol*> symbols;
	NodeVisitor* visitor = nullptr;
	NodeFactory factory;
	auto visit = [&](Node* node) -> Node* {
		if (isLiteralImportTypeNode(node) &&
		    node->as<ImportTypeNode>()->Qualifier != nullptr) {
			ImportTypeNode* importTypeNode = node->as<ImportTypeNode>();
			// Symbol for the left-most thing after the dot
			Node* firstIdentifier = getFirstIdentifier(importTypeNode->Qualifier);
			Symbol* symbol = nullptr;
			if (auto it = idToSymbol->find(firstIdentifier);
			    it != idToSymbol->end()) {
				symbol = it->second;
			}
			if (symbol == nullptr) {
				// if symbol is missing then this doesn't come from a synthesized import type node
				// it has to be an import type node authored by the user and thus it has to be valid
				// it can't refer to reserved internal symbol names and such
				return node->visitEachChild(*visitor);
			}
			std::string name = getNameForExportedSymbol(
			    symbol, false /*preferCapitalized*/);
			Node* qualifier;
			if (name != firstIdentifier->text()) {
				qualifier = replaceFirstIdentifierOfEntityName(
				    &factory, importTypeNode->Qualifier,
				    factory.newIdentifier(name));
			} else {
				qualifier = importTypeNode->Qualifier;
			}
			symbols.push_back(symbol);
			NodeList* typeArguments =
			    visitor->visitNodes(importTypeNode->TypeArguments);
			return factory.newTypeReferenceNode(qualifier, typeArguments);
		}
		return visitor->visitEachChild(node);
	};
	visitor = newNodeVisitor(visit, &factory, NodeVisitorHooks{});
	std::unique_ptr<NodeVisitor> visitorOwner(visitor);

	Node* typeNode = visitor->visitNode(importTypeNode);
	debug::assert(typeNode == nullptr || isTypeNode(typeNode),
	              "expected a type node");
	return {typeNode, symbols};
}

// If a type checker and multiple files are available, consider using `forEachNameOfDefaultExport`
// instead, which searches for names of re-exported defaults/namespaces in target files.
// getNameForExportedSymbol — import_adder.go:464
std::string getNameForExportedSymbol(Symbol* symbol, bool preferCapitalized) {
	if (symbol->name == InternalSymbolNameExportEquals ||
	    symbol->name == InternalSymbolNameDefault) {
		// Names for default exports:
		// - export default foo => foo
		// - export { foo as default } => foo
		// - export default 0 => filename converted to camelCase
		std::string name = getDefaultLikeExportNameFromDeclaration(symbol);
		if (!name.empty()) {
			return name;
		}
		debug::assert(symbol->parent != nullptr,
	                  "Expected exported symbol to have module symbol as parent");
		return lsutil::ModuleSymbolToValidIdentifier(symbol->parent,
	                                                 preferCapitalized);
	}
	return symbol->name;
}

// replaceFirstIdentifierOfEntityName — import_adder.go:485
Node* replaceFirstIdentifierOfEntityName(NodeFactory* factory, Node* name,
                                         Node* newIdentifier) {
	if (name->kind == Kind::Identifier) {
		return newIdentifier;
	}
	return factory->newQualifiedName(
	    replaceFirstIdentifierOfEntityName(
	        factory, name->as<QualifiedName>()->Left, newIdentifier),
	    name->as<QualifiedName>()->Right);
}

// importAdder::getImportFixForSymbol — import_adder.go:491
std::unique_ptr<Fix> importAdder::getImportFixForSymbol(
    View* view, const std::vector<Export*>& exports,
    bool isValidTypeOnlyUseSite) {
	// core.FlatMap
	std::vector<std::unique_ptr<Fix>> fixes;
	for (Export* e : exports) {
		auto fs = view->GetFixes(e, false /*forJSX*/, isValidTypeOnlyUseSite,
		                         nullptr /*usagePosition*/);
		for (auto& f : fs) {
			fixes.push_back(std::move(f));
		}
	}
	std::sort(fixes.begin(), fixes.end(),
	          [&](const std::unique_ptr<Fix>& a, const std::unique_ptr<Fix>& b) {
		          return view->CompareFixesForRanking(a.get(), b.get()) < 0;
	          });
	if (!fixes.empty()) {
		return std::move(fixes[0]);
	}
	return nullptr;
}

}  // namespace tsc::ls::autoimport
