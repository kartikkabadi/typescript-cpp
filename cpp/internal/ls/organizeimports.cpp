// organizeimports.go — OrganizeImports: remove unused imports, coalesce
// imports per module specifier, sort import/export declarations.
#include "internal/ls/ls.h"

#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/ls/change/change.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/printer/printer.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

#include <algorithm>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace tsc::ls {

namespace {

// doneGuard — RAII for the checker-release callback from
// GetTypeCheckerForFileExclusive (Go `defer done()`).
struct doneGuard {
	std::function<void()> done;
	~doneGuard() {
		if (done) {
			done();
		}
	}
};

// organizeImportsComparerSettings — organizeimports.go:120.
struct organizeImportsComparerSettings {
	lsutil::StringComparer moduleSpecifierComparer;
	lsutil::StringComparer namedImportComparer;
	lsutil::OrganizeImportsTypeOrder typeOrder;
};

// importGroup — organizeimports.go:628.
struct importGroup {
	std::vector<Node*> defaultImports;
	std::vector<Node*> namespaceImports;
	std::vector<Node*> namedImports;

	bool isEmpty() const {
		return defaultImports.empty() && namespaceImports.empty() &&
		       namedImports.empty();
	}
};

// categorizedImports — organizeimports.go:623.
struct categorizedImports {
	Node* importWithoutClause = nullptr;
	importGroup typeOnlyImports;
	importGroup regularImports;
};

// categorizedExports — organizeimports.go:924.
struct categorizedExports {
	Node* exportWithoutClause = nullptr;
	std::vector<Node*> namedExports;
	std::vector<Node*> typeOnlyExports;
};

void organizeImportsWorker(
    std::vector<Node*> oldImportDecls,
    const organizeImportsComparerSettings& comparer, bool shouldSort,
    bool shouldCombine, bool shouldRemove, SourceFile* sourceFile,
    compiler::SimpleProgram* program, change::Tracker* changeTracker,
    const gostd::Context& ctx);
std::vector<std::vector<Node*>> groupByModuleSpecifier(
    const std::vector<Node*>& imports);
std::vector<Node*> removeUnusedImports(
    std::vector<Node*> oldImports, SourceFile* sourceFile,
    checker::Checker* typeChecker, compiler::SimpleProgram* program,
    change::Tracker* changeTracker);
std::vector<Node*> filterUsedImportSpecifiers(
    const std::vector<Node*>& elements, checker::Checker* typeChecker,
    SourceFile* sourceFile, bool jsxElementsPresent,
    bool jsxModeNeedsExplicitImport);
bool hasModuleDeclarationMatchingSpecifier(SourceFile* sourceFile,
                                           Node* moduleSpecifier);
std::string getImportAttributesKey(Node* attributes);
std::vector<std::vector<Node*>> groupByNewlineContiguous(
    SourceFile* sourceFile, const std::vector<Node*>& decls);
bool isNewGroup(SourceFile* sourceFile, Node* decl, Scanner* s);
std::vector<Node*> coalesceImportsWorker(
    std::vector<Node*> importDecls,
    const lsutil::StringComparer& comparer,
    const std::function<int(Node*, Node*)>& specifierComparer,
    SourceFile* sourceFile, change::Tracker* changeTracker);
categorizedImports getCategorizedImports(
    const std::vector<Node*>& importDecls);
std::vector<Node*> getNewImportSpecifiers(
    const std::vector<Node*>& namedImports, NodeFactory* factory);
std::vector<Node*> tryGetNamedBindingElements(Node* namedImport);
std::vector<std::vector<Node*>> getTopLevelExportGroups(
    SourceFile* sourceFile);
void organizeExportsWorker(
    const std::vector<Node*>& oldExportDecls,
    const organizeImportsComparerSettings& comparer,
    SourceFile* sourceFile, change::Tracker* changeTracker);
std::vector<Node*> coalesceExportsWorker(
    const std::vector<Node*>& exportGroup,
    const std::function<int(Node*, Node*)>& specifierComparer,
    const lsutil::StringComparer&
        moduleSpecifierComparer,
    SourceFile* sourceFile, change::Tracker* changeTracker);
categorizedExports getCategorizedExports(
    const std::vector<Node*>& exportGroup);

} // namespace

// OrganizeImports — organizeimports.go:21.
lsproto::Map<std::string,
             lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>
LanguageService::OrganizeImports(const gostd::Context& ctx,
                                 SourceFile* sourceFile,
                                 compiler::SimpleProgram* program,
                                 lsproto::CodeActionKind kind) {
	auto* changeTracker =
	    new change::Tracker(format::FormatRequestContext{},
	                        program->Options(), FormatOptions(), converters);
	bool shouldSort =
	    kind == lsproto::CodeActionKindSourceSortImportsTs ||
	    kind == lsproto::CodeActionKindSourceOrganizeImportsTs;
	bool shouldCombine = shouldSort;
	bool shouldRemove =
	    kind == lsproto::CodeActionKindSourceRemoveUnusedImportsTs ||
	    kind == lsproto::CodeActionKindSourceOrganizeImportsTs;
	auto topLevelImportDecls =
	    lsutil::FilterImportDeclarations(sourceFile->Statements->nodes);
	auto topLevelImportGroupDecls =
	    groupByNewlineContiguous(sourceFile, topLevelImportDecls);

	auto preferences = UserPreferences();
	auto [comparersToTest, typeOrdersToTest] =
	    lsutil::GetDetectionLists(preferences);
	auto& defaultComparer = comparersToTest[0];
	auto sort = lsutil::ResolveOrganizeImportsSort(preferences);

	lsutil::StringComparer moduleSpecifierComparer;
	lsutil::StringComparer namedImportComparer;
	if (sort != lsutil::OrganizeImportsSortAuto) {
		moduleSpecifierComparer = defaultComparer;
		namedImportComparer = defaultComparer;
	}
	auto typeOrder = preferences.OrganizeImportsTypeOrder;

	if (sort == lsutil::OrganizeImportsSortAuto) {
		auto [result, _] = lsutil::DetectModuleSpecifierCaseBySort(
		    topLevelImportGroupDecls, comparersToTest);
		moduleSpecifierComparer = result;
	}

	if (typeOrder == lsutil::OrganizeImportsTypeOrderAuto ||
	    sort == lsutil::OrganizeImportsSortAuto) {
		auto [namedImportComparer2, typeOrder2, found] =
		    lsutil::DetectNamedImportOrganizationBySort(
		        topLevelImportDecls, comparersToTest, typeOrdersToTest);
		if (found) {
			if (!namedImportComparer ||
			    sort == lsutil::OrganizeImportsSortAuto) {
				namedImportComparer = namedImportComparer2;
			}
			if (typeOrder == lsutil::OrganizeImportsTypeOrderAuto) {
				typeOrder = typeOrder2;
			}
		}
	}

	organizeImportsComparerSettings comparer{
	    .moduleSpecifierComparer = moduleSpecifierComparer,
	    .namedImportComparer = namedImportComparer,
	    .typeOrder = typeOrder,
	};

	for (auto& importGroupDecl : topLevelImportGroupDecls) {
		organizeImportsWorker(importGroupDecl, comparer, shouldSort,
		                      shouldCombine, shouldRemove, sourceFile,
		                      program, changeTracker, ctx);
	}

	if (kind != lsproto::CodeActionKindSourceRemoveUnusedImportsTs) {
		auto topLevelExportGroupDecls = getTopLevelExportGroups(sourceFile);
		for (auto& exportGroupDecl : topLevelExportGroupDecls) {
			organizeExportsWorker(exportGroupDecl, comparer, sourceFile,
			                      changeTracker);
		}
	}

	for (auto* stmt : sourceFile->Statements->nodes) {
		if (!isAmbientModule(stmt)) {
			continue;
		}

		auto* ambientModule = stmt->as<ModuleDeclaration>();
		if (ambientModule->Body == nullptr) {
			continue;
		}

		auto* moduleBody = ambientModule->Body->as<ModuleBlock>();

		auto ambientModuleImportDecls =
		    lsutil::FilterImportDeclarations(moduleBody->Statements->nodes);
		auto ambientModuleImportGroupDecls = groupByNewlineContiguous(
		    sourceFile, ambientModuleImportDecls);

		for (auto& importGroupDecl : ambientModuleImportGroupDecls) {
			organizeImportsWorker(importGroupDecl, comparer, shouldSort,
			                      shouldCombine, shouldRemove, sourceFile,
			                      program, changeTracker, ctx);
		}

		if (kind != lsproto::CodeActionKindSourceRemoveUnusedImportsTs) {
			std::vector<Node*> ambientModuleExportDecls;
			for (auto* s : moduleBody->Statements->nodes) {
				if (s->kind == Kind::ExportDeclaration) {
					ambientModuleExportDecls.push_back(s);
				}
			}
			organizeExportsWorker(ambientModuleExportDecls, comparer,
			                      sourceFile, changeTracker);
		}
	}

	// Unmappable files are dropped by GetChanges, so a content-mapped file
	// whose imports cannot be faithfully rewritten yields no edits rather
	// than a corrupting one.
	auto changes = changeTracker->GetChanges().first;
	lsproto::Map<std::string,
	             lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>
	    out;
	for (auto& [fileName, edits] : changes) {
		std::vector<std::shared_ptr<lsproto::TextEdit>> fileEdits;
		fileEdits.reserve(edits.size());
		for (auto& e : edits) {
			fileEdits.push_back(std::make_shared<lsproto::TextEdit>(e));
		}
		out[fileName] = lsproto::Slice<
		    std::shared_ptr<lsproto::TextEdit>>(std::move(fileEdits));
	}
	return out;
}

namespace {

// organizeImportsWorker — organizeimports.go:127.
void organizeImportsWorker(
    std::vector<Node*> oldImportDecls,
    const organizeImportsComparerSettings& comparer, bool shouldSort,
    bool shouldCombine, bool shouldRemove, SourceFile* sourceFile,
    compiler::SimpleProgram* program, change::Tracker* changeTracker,
    const gostd::Context& ctx) {
	if (oldImportDecls.empty()) {
		return;
	}

	// Header comment preservation is handled via
	// LeadingTriviaOptionExclude in the change tracker below

	auto processedImports = oldImportDecls;
	if (shouldRemove) {
		auto [typeChecker, done] =
		    program->GetTypeCheckerForFileExclusive(sourceFile);
		doneGuard doneGuard_{done};
		processedImports =
		    removeUnusedImports(processedImports, sourceFile, typeChecker,
		                        program, changeTracker);
	}

	std::vector<Node*> newImportDecls;
	if (shouldCombine) {
		auto grouped = groupByModuleSpecifier(processedImports);
		if (shouldSort) {
			std::sort(grouped.begin(), grouped.end(),
			          [&comparer](const std::vector<Node*>& a,
			                      const std::vector<Node*>& b) {
				          if (a.empty() || b.empty()) {
					          return false;
				          }
				          return lsutil::CompareModuleSpecifiers(
				                     a[0]->moduleSpecifier(),
				                     b[0]->moduleSpecifier(),
				                     comparer.moduleSpecifierComparer) < 0;
			          });
		}

		auto specifierComparer = lsutil::GetNamedImportSpecifierComparer(
		    lsutil::UserPreferences{
		        .OrganizeImportsTypeOrder = comparer.typeOrder},
		    comparer.namedImportComparer);

		for (auto& importGroup_ : grouped) {
			auto coalesced = coalesceImportsWorker(
			    importGroup_, comparer.moduleSpecifierComparer,
			    specifierComparer, sourceFile, changeTracker);
			if (shouldSort) {
				std::sort(coalesced.begin(), coalesced.end(),
				          [&comparer](Node* a, Node* b) {
					          return lsutil::
					                     CompareImportsOrRequireStatements(
					                         a, b,
					                         comparer
					                             .moduleSpecifierComparer) <
					                 0;
				          });
			}
			newImportDecls.insert(newImportDecls.end(), coalesced.begin(),
			                      coalesced.end());
		}
	} else {
		newImportDecls = processedImports;
	}

	if (shouldSort && !shouldCombine) {
		std::sort(newImportDecls.begin(), newImportDecls.end(),
		          [&comparer](Node* a, Node* b) {
			          return lsutil::CompareImportsOrRequireStatements(
			                     a, b, comparer.moduleSpecifierComparer) <
			                 0;
		          });
	}

	if (newImportDecls.empty()) {
		changeTracker->DeleteNodeRange(
		    sourceFile, oldImportDecls.front(),
		    oldImportDecls.back(),
		    change::LeadingTriviaOptionExclude, // Preserve header comment
		    change::TrailingTriviaOptionInclude);
	} else {
		for (auto* imp : newImportDecls) {
			changeTracker->emitContext->setEmitFlags(
			    imp, printer::EFNoLeadingComments);
		}

		change::NodeOptions options{
		    .leadingTrivia =
		        change::LeadingTriviaOptionExclude, // Preserve header comment
		    .trailingTrivia =
		        change::TrailingTriviaOptionInclude,
		    .Suffix = "\n",
		};

		std::vector<Node*> newNodes;
		newNodes.reserve(newImportDecls.size());
		for (auto* s : newImportDecls) {
			newNodes.push_back(s);
		}
		changeTracker->ReplaceNodeWithNodes(
		    sourceFile, oldImportDecls.front(), newNodes, &options);

		if (oldImportDecls.size() > 1) {
			for (size_t i = 1; i < oldImportDecls.size(); i++) {
				changeTracker->Delete(sourceFile, oldImportDecls[i]);
			}
		}
	}
}

// groupByModuleSpecifier — organizeimports.go:212.
std::vector<std::vector<Node*>> groupByModuleSpecifier(
    const std::vector<Node*>& imports) {
	std::map<std::string, std::vector<Node*>> groups_;
	std::unordered_map<std::string, std::vector<Node*>> groups;
	std::vector<std::string> order;

	for (auto* imp : imports) {
		auto specifier =
		    lsutil::GetExternalModuleName(imp->moduleSpecifier());
		if (groups.find(specifier) == groups.end()) {
			order.push_back(specifier);
		}
		groups[specifier].push_back(imp);
	}

	std::vector<std::vector<Node*>> result;
	result.reserve(order.size());
	for (auto& key : order) {
		result.push_back(groups[key]);
	}
	return result;
}

// removeUnusedImports — organizeimports.go:231.
std::vector<Node*> removeUnusedImports(
    std::vector<Node*> oldImports, SourceFile* sourceFile,
    checker::Checker* typeChecker, compiler::SimpleProgram* program,
    change::Tracker* changeTracker) {
	auto compilerOptions = program->Options();
	bool jsxElementsPresent =
	    (sourceFile->subtreeFacts() & SubtreeContainsJsx) != 0;
	bool jsxModeNeedsExplicitImport =
	    compilerOptions->Jsx == JsxEmit::React ||
	    compilerOptions->Jsx == JsxEmit::ReactNative;

	// Go allocates a fresh factory with empty hooks; its nodes are GC-kept.
	// Here the updated decls are returned and later printed by the change
	// tracker, so they must live in the emit context's arena (the tracker-
	// owned factory). Using it also gives updated decls the same
	// Synthesized/original tracking the emit pipeline expects.
	NodeFactory& factory = *changeTracker->nodeFactory;
	std::vector<Node*> usedImports;
	usedImports.reserve(oldImports.size());

	for (auto* importDecl : oldImports) {
		auto* importClause =
		    importDecl->as<ImportDeclaration>()->ImportClause;
		if (importClause == nullptr) {
			usedImports.push_back(importDecl);
			continue;
		}

		auto* clause = importClause->as<ImportClause>();
		auto* name = clause->Node::name();
		auto* namedBindings = clause->NamedBindings;

		if (name != nullptr &&
		    !typeChecker->IsDeclarationUsed(sourceFile,
		                                    name->as<Identifier>(),
		                                    jsxElementsPresent,
		                                    jsxModeNeedsExplicitImport)) {
			name = nullptr;
		}

		if (namedBindings != nullptr) {
			switch (namedBindings->kind) {
			case Kind::NamespaceImport: {
				auto* nsImport = namedBindings->as<NamespaceImport>();
				if (!typeChecker->IsDeclarationUsed(
				        sourceFile,
				        nsImport->Node::name()->as<Identifier>(),
				        jsxElementsPresent,
				        jsxModeNeedsExplicitImport)) {
					namedBindings = nullptr;
				}
				break;
			}
			case Kind::NamedImports: {
				auto* namedImports = namedBindings->as<NamedImports>();
				auto* originalBindings = namedBindings;
				auto newElements = filterUsedImportSpecifiers(
				    namedImports->Elements->nodes, typeChecker,
				    sourceFile, jsxElementsPresent,
				    jsxModeNeedsExplicitImport);
				if (newElements.empty()) {
					namedBindings = nullptr;
				} else if (newElements.size() <
				           namedImports->Elements->nodes.size()) {
					auto* newList = factory.newNodeList(newElements);
					auto* updatedNamedImports = factory.updateNamedImports(
					    namedImports, newList);
					namedBindings = updatedNamedImports;
				}
				if (namedBindings != nullptr &&
				    !nodeIsSynthesized(originalBindings) &&
				    !printer::RangeIsOnSingleLine(
				        originalBindings->loc, sourceFile)) {
					changeTracker->emitContext->setEmitFlags(
					    namedBindings, printer::EFMultiLine);
				}
				break;
			}
			default:
				break;
			}
		}

		if (name != nullptr || namedBindings != nullptr) {
			auto* importDeclNode = importDecl->as<ImportDeclaration>();
			auto* newClause = factory.updateImportClause(
			    clause, clause->PhaseModifier, name, namedBindings);
			auto* newImportDecl = factory.updateImportDeclaration(
			    importDeclNode, importDeclNode->Node::modifiers(),
			    newClause, importDeclNode->ModuleSpecifier,
			    importDeclNode->Attributes);
			usedImports.push_back(newImportDecl);
		} else {
			auto* moduleSpecifier = importDecl->moduleSpecifier();
			if (hasModuleDeclarationMatchingSpecifier(sourceFile,
			                                          moduleSpecifier)) {
				if (sourceFile->IsDeclarationFile) {
					auto* importDeclNode =
					    importDecl->as<ImportDeclaration>();
					auto* newImportDecl =
					    factory.updateImportDeclaration(
					        importDeclNode,
					        importDeclNode->Node::modifiers(),
					        nullptr, // no import clause
					        importDeclNode->ModuleSpecifier,
					        importDeclNode->Attributes);
					usedImports.push_back(newImportDecl);
				} else {
					usedImports.push_back(importDecl);
				}
			}
		}
	}

	return usedImports;
}

// filterUsedImportSpecifiers — organizeimports.go:322.
std::vector<Node*> filterUsedImportSpecifiers(
    const std::vector<Node*>& elements, checker::Checker* typeChecker,
    SourceFile* sourceFile, bool jsxElementsPresent,
    bool jsxModeNeedsExplicitImport) {
	std::vector<Node*> result;
	for (auto* elem : elements) {
		auto* spec = elem->as<ImportSpecifier>();
		if (typeChecker->IsDeclarationUsed(
		        sourceFile, spec->Node::name()->as<Identifier>(),
		        jsxElementsPresent, jsxModeNeedsExplicitImport)) {
			result.push_back(elem);
		}
	}
	return result;
}

// hasModuleDeclarationMatchingSpecifier — organizeimports.go:337.
bool hasModuleDeclarationMatchingSpecifier(SourceFile* sourceFile,
                                           Node* moduleSpecifier) {
	if (moduleSpecifier == nullptr ||
	    !isStringLiteral(moduleSpecifier)) {
		return false;
	}
	auto moduleSpecifierText = moduleSpecifier->text();

	for (auto* moduleName : sourceFile->ModuleAugmentations) {
		if (isStringLiteral(moduleName) &&
		    moduleName->text() == moduleSpecifierText) {
			return true;
		}
	}

	return false;
}

// getImportAttributesKey — organizeimports.go:351.
std::string getImportAttributesKey(Node* attributes) {
	if (attributes == nullptr) {
		return "";
	}

	auto* importAttrs = attributes->as<ImportAttributes>();
	std::string key;
	key += kindToString(importAttrs->Token);
	key += " ";

	auto attrNodes = importAttrs->Attributes->nodes;
	std::sort(attrNodes.begin(), attrNodes.end(),
	          [](Node* a, Node* b) {
		          auto aName = a->as<ImportAttribute>()->Node::name()->text();
		          auto bName = b->as<ImportAttribute>()->Node::name()->text();
		          return stringutil::CompareStringsCaseSensitive(aName,
		                                                         bName) < 0;
	          });

	for (auto* attrNode : attrNodes) {
		auto* attr = attrNode->as<ImportAttribute>();
		key += attr->Node::name()->text();
		key += ":";
		if (isStringLiteralLike(attr->Value)) {
			key += "\"";
			key += attr->Value->text();
			key += "\"";
		} else {
			key += attr->Value->text();
		}
		key += " ";
	}

	return key;
}

// groupByNewlineContiguous — organizeimports.go:384.
std::vector<std::vector<Node*>> groupByNewlineContiguous(
    SourceFile* sourceFile, const std::vector<Node*>& decls) {
	Scanner s;
	s.setSkipTrivia(false); // Must not skip trivia to detect newlines
	std::vector<std::vector<Node*>> groups;
	std::vector<Node*> currentGroup;

	for (auto* decl : decls) {
		if (!currentGroup.empty() && isNewGroup(sourceFile, decl, &s)) {
			groups.push_back(currentGroup);
			currentGroup.clear();
		}
		currentGroup.push_back(decl);
	}

	if (!currentGroup.empty()) {
		groups.push_back(currentGroup);
	}

	return groups;
}

// isNewGroup — organizeimports.go:405.
bool isNewGroup(SourceFile* sourceFile, Node* decl, Scanner* s) {
	auto fullStart = decl->pos();
	if (fullStart < 0) {
		return false;
	}

	auto& text = sourceFile->Text();
	auto textLen = (int)text.size();

	if ((int)fullStart >= textLen) {
		return false;
	}

	auto startPos = skipTrivia(text, fullStart);
	if (startPos <= fullStart) {
		return false;
	}

	auto triviaLen = startPos - fullStart;
	s->setText(std::string_view(text).substr(fullStart, triviaLen));

	int numberOfNewLines = 0;
	while (s->tokenStart() < triviaLen) {
		auto tokenKind = s->scan();
		if (tokenKind == Kind::NewLineTrivia) {
			numberOfNewLines++;
			if (numberOfNewLines >= 2) {
				return true;
			}
		}
	}

	return false;
}

// coalesceImportsWorker — organizeimports.go:432.
std::vector<Node*> coalesceImportsWorker(
    std::vector<Node*> importDecls,
    const lsutil::StringComparer& comparer,
    const std::function<int(Node*, Node*)>& specifierComparer,
    SourceFile* sourceFile, change::Tracker* changeTracker) {
	if (importDecls.empty()) {
		return importDecls;
	}

	std::unordered_map<std::string, std::vector<Node*>>
	    importGroupsByAttributes;
	std::vector<std::string> attributeKeys;

	for (auto* importDecl : importDecls) {
		auto key = getImportAttributesKey(
		    importDecl->as<ImportDeclaration>()->Attributes);
		if (importGroupsByAttributes.find(key) ==
		    importGroupsByAttributes.end()) {
			attributeKeys.push_back(key);
		}
		importGroupsByAttributes[key].push_back(importDecl);
	}

	std::vector<Node*> coalescedImports;

	for (auto& attributeKey : attributeKeys) {
		auto& importGroupSameAttrs = importGroupsByAttributes[attributeKey];
		auto categorized = getCategorizedImports(importGroupSameAttrs);

		if (categorized.importWithoutClause != nullptr) {
			coalescedImports.push_back(categorized.importWithoutClause);
		}

		// See removeUnusedImports: synthesized decls outlive this function
		// (returned for sorting and emitted by the change tracker), so they
		// must be allocated on the tracker-owned emit-context factory.
		NodeFactory& factory = *changeTracker->nodeFactory;

		for (int i = 0; i < 2; i++) {
			auto& group = i == 0 ? categorized.regularImports
			                     : categorized.typeOnlyImports;
			if (group.isEmpty()) {
				continue;
			}

			bool isTypeOnly = i == 1;

			if (!isTypeOnly && group.defaultImports.size() == 1 &&
			    group.namespaceImports.size() == 1 &&
			    group.namedImports.empty()) {
				auto* defaultImport = group.defaultImports[0];
				auto* namespaceImport = group.namespaceImports[0];

				auto* defaultClause =
				    defaultImport->as<ImportDeclaration>()
				        ->ImportClause->as<ImportClause>();
				auto* namespaceBindings =
				    namespaceImport->as<ImportDeclaration>()
				        ->ImportClause->as<ImportClause>()
				        ->NamedBindings;

				auto* newClause = factory.updateImportClause(
				    defaultClause, defaultClause->PhaseModifier,
				    defaultClause->Node::name(), namespaceBindings);
				auto* defaultDeclNode =
				    defaultImport->as<ImportDeclaration>();
				auto* newImportDecl = factory.updateImportDeclaration(
				    defaultDeclNode,
				    defaultDeclNode->Node::modifiers(), newClause,
				    defaultDeclNode->ModuleSpecifier,
				    defaultDeclNode->Attributes);
				coalescedImports.push_back(newImportDecl);
				continue;
			}

			std::sort(group.namespaceImports.begin(),
			          group.namespaceImports.end(),
			          [&comparer](Node* a, Node* b) {
				          auto* n1 = a->as<ImportDeclaration>()
				                         ->ImportClause->as<ImportClause>()
				                         ->NamedBindings
				                         ->as<NamespaceImport>()
				                         ->Node::name();
				          auto* n2 = b->as<ImportDeclaration>()
				                         ->ImportClause->as<ImportClause>()
				                         ->NamedBindings
				                         ->as<NamespaceImport>()
				                         ->Node::name();
				          return comparer(n1->text(), n2->text()) < 0;
			          });

			for (auto* nsImport : group.namespaceImports) {
				auto* nsImportDecl =
				    nsImport->as<ImportDeclaration>();
				auto* clause =
				    nsImportDecl->ImportClause->as<ImportClause>();
				auto* newClause = factory.updateImportClause(
				    clause, clause->PhaseModifier, nullptr,
				    clause->NamedBindings);
				auto* newImportDecl = factory.updateImportDeclaration(
				    nsImportDecl, nsImportDecl->Node::modifiers(),
				    newClause, nsImportDecl->ModuleSpecifier,
				    nsImportDecl->Attributes);
				coalescedImports.push_back(newImportDecl);
			}

			Node* firstDefaultImport = nullptr;
			Node* firstNamedImport = nullptr;

			if (!group.defaultImports.empty()) {
				firstDefaultImport = group.defaultImports[0];
			}
			if (!group.namedImports.empty()) {
				firstNamedImport = group.namedImports[0];
			}

			auto* importDecl = firstDefaultImport;
			if (importDecl == nullptr) {
				importDecl = firstNamedImport;
			}
			if (importDecl == nullptr) {
				continue;
			}

			Node* newDefaultImport = nullptr;
			std::vector<Node*> newImportSpecifiers;

			if (group.defaultImports.size() == 1) {
				newDefaultImport =
				    group.defaultImports[0]
				        ->as<ImportDeclaration>()
				        ->ImportClause->as<ImportClause>()
				        ->Node::name();
			} else {
				for (auto* defaultImport : group.defaultImports) {
					auto* defaultClause =
					    defaultImport->as<ImportDeclaration>()
					        ->ImportClause->as<ImportClause>();
					auto* defaultName =
					    defaultClause->Node::name();
					auto* propertyName =
					    factory.newIdentifier("default");
					auto* importSpec = factory.newImportSpecifier(
					    false, propertyName, defaultName);
					newImportSpecifiers.push_back(importSpec);
				}
			}

			auto namedSpecs = getNewImportSpecifiers(group.namedImports,
			                                         &factory);
			newImportSpecifiers.insert(newImportSpecifiers.end(),
			                           namedSpecs.begin(),
			                           namedSpecs.end());
			std::stable_sort(newImportSpecifiers.begin(),
			                 newImportSpecifiers.end(),
			                 [&specifierComparer](Node* a, Node* b) {
				                 return specifierComparer(a, b) < 0;
			                 });

			Node* newNamedImports = nullptr;
			if (newImportSpecifiers.empty()) {
				if (newDefaultImport != nullptr) {
					newNamedImports = nullptr;
				} else {
					newNamedImports = factory.newNamedImports(
					    factory.newNodeList({}));
				}
			} else {
				auto* sortedList =
				    factory.newNodeList(newImportSpecifiers);
				if (firstNamedImport != nullptr) {
					auto* firstNamedBindings =
					    firstNamedImport->as<ImportDeclaration>()
					        ->ImportClause->as<ImportClause>()
					        ->NamedBindings->as<NamedImports>();
					auto* originalElements =
					    firstNamedBindings->Elements;
					if (originalElements->hasTrailingComma()) {
						sortedList->loc = originalElements->loc;
					}
					newNamedImports = factory.updateNamedImports(
					    firstNamedBindings, sortedList);
				} else {
					newNamedImports =
					    factory.newNamedImports(sortedList);
				}
			}

			if (sourceFile != nullptr && newNamedImports != nullptr &&
			    firstNamedImport != nullptr) {
				auto* firstNamedBindings =
				    firstNamedImport->as<ImportDeclaration>()
				        ->ImportClause->as<ImportClause>()
				        ->NamedBindings;
				if (!nodeIsSynthesized(firstNamedBindings) &&
				    !printer::RangeIsOnSingleLine(
				        firstNamedBindings->loc, sourceFile)) {
					changeTracker->emitContext->setEmitFlags(
					    newNamedImports, printer::EFMultiLine);
				}
			}

			if (isTypeOnly && newDefaultImport != nullptr &&
			    newNamedImports != nullptr) {
				auto* importDeclNode =
				    importDecl->as<ImportDeclaration>();

				auto* defaultClause = factory.newImportClause(
				    importDeclNode->ImportClause
				        ->as<ImportClause>()
				        ->PhaseModifier,
				    newDefaultImport, nullptr);
				auto* defaultImportDecl =
				    factory.updateImportDeclaration(
				        importDeclNode,
				        importDeclNode->Node::modifiers(),
				        defaultClause,
				        importDeclNode->ModuleSpecifier,
				        importDeclNode->Attributes);
				coalescedImports.push_back(defaultImportDecl);

				auto* namedDeclNode = firstNamedImport;
				if (namedDeclNode == nullptr) {
					namedDeclNode = importDecl;
				}
				auto* namedImportDeclNode =
				    namedDeclNode->as<ImportDeclaration>();
				auto* namedClause = factory.newImportClause(
				    namedImportDeclNode->ImportClause
				        ->as<ImportClause>()
				        ->PhaseModifier,
				    nullptr, newNamedImports);
				auto* namedImportDecl =
				    factory.updateImportDeclaration(
				        namedImportDeclNode,
				        namedImportDeclNode->Node::modifiers(),
				        namedClause,
				        namedImportDeclNode->ModuleSpecifier,
				        namedImportDeclNode->Attributes);
				coalescedImports.push_back(namedImportDecl);
			} else {
				auto* importDeclNode =
				    importDecl->as<ImportDeclaration>();
				auto* clauseNode =
				    importDeclNode->ImportClause->as<ImportClause>();
				auto* newClause = factory.updateImportClause(
				    clauseNode, clauseNode->PhaseModifier,
				    newDefaultImport, newNamedImports);
				auto* newImportDecl = factory.updateImportDeclaration(
				    importDeclNode,
				    importDeclNode->Node::modifiers(), newClause,
				    importDeclNode->ModuleSpecifier,
				    importDeclNode->Attributes);
				coalescedImports.push_back(newImportDecl);
			}
		}
	}
	return coalescedImports;
}

// getCategorizedImports — organizeimports.go:647.
categorizedImports getCategorizedImports(
    const std::vector<Node*>& importDecls) {
	Node* importWithoutClause = nullptr;
	importGroup typeOnlyImports;
	importGroup regularImports;

	for (auto* importDecl : importDecls) {
		if (importDecl->as<ImportDeclaration>()->ImportClause == nullptr) {
			if (importWithoutClause == nullptr) {
				importWithoutClause = importDecl;
			}
			continue;
		}

		auto* clause = importDecl->as<ImportDeclaration>()
		                   ->ImportClause->as<ImportClause>();
		auto* group = &regularImports;
		if (clause->isTypeOnly()) {
			group = &typeOnlyImports;
		}

		auto* name = clause->Node::name();
		auto* namedBindings = clause->NamedBindings;

		if (name != nullptr) {
			group->defaultImports.push_back(importDecl);
		}

		if (namedBindings != nullptr) {
			switch (namedBindings->kind) {
			case Kind::NamespaceImport:
				group->namespaceImports.push_back(importDecl);
				break;
			case Kind::NamedImports:
				group->namedImports.push_back(importDecl);
				break;
			default:
				break;
			}
		}
	}

	return categorizedImports{
	    .importWithoutClause = importWithoutClause,
	    .typeOnlyImports = typeOnlyImports,
	    .regularImports = regularImports,
	};
}

// getNewImportSpecifiers — organizeimports.go:690.
std::vector<Node*> getNewImportSpecifiers(
    const std::vector<Node*>& namedImports, NodeFactory* factory) {
	std::vector<Node*> result;

	for (auto* namedImport : namedImports) {
		auto elements = tryGetNamedBindingElements(namedImport);
		if (elements.empty()) {
			continue;
		}

		for (auto* elem : elements) {
			auto* spec = elem->as<ImportSpecifier>();

			if (spec->PropertyName != nullptr &&
			    spec->Node::name() != nullptr) {
				auto propertyText = spec->PropertyName->text();
				auto nameText = spec->Node::name()->text();

				if (propertyText == nameText) {
					auto* normalized = factory->updateImportSpecifier(
					    spec, spec->IsTypeOnly, nullptr,
					    spec->Node::name());
					result.push_back(normalized);
					continue;
				}
			}

			result.push_back(elem);
		}
	}

	return result;
}

// tryGetNamedBindingElements — organizeimports.go:723.
std::vector<Node*> tryGetNamedBindingElements(Node* namedImport) {
	if (namedImport->kind != Kind::ImportDeclaration) {
		return {};
	}

	auto* importDecl = namedImport->as<ImportDeclaration>();
	if (importDecl->ImportClause == nullptr) {
		return {};
	}

	auto* clause = importDecl->ImportClause->as<ImportClause>();
	auto* namedBindings = clause->NamedBindings;

	if (namedBindings != nullptr &&
	    namedBindings->kind == Kind::NamedImports) {
		auto* namedImportsNode = namedBindings->as<NamedImports>();
		return namedImportsNode->Elements->nodes;
	}

	return {};
}

// getTopLevelExportGroups — organizeimports.go:743.
std::vector<std::vector<Node*>> getTopLevelExportGroups(
    SourceFile* sourceFile) {
	std::vector<std::vector<Node*>> topLevelExportGroups;
	auto& statements = sourceFile->Statements->nodes;
	auto statementsLen = statements.size();

	size_t i = 0;
	size_t groupIndex = 0;
	while (i < statementsLen) {
		if (statements[i]->kind == Kind::ExportDeclaration) {
			if (groupIndex >= topLevelExportGroups.size()) {
				topLevelExportGroups.push_back({});
			}
			auto* exportDecl = statements[i]->as<ExportDeclaration>();
			if (exportDecl->ModuleSpecifier != nullptr) {
				topLevelExportGroups[groupIndex].push_back(statements[i]);
				i++;
			} else {
				while (i < statementsLen &&
				       statements[i]->kind == Kind::ExportDeclaration) {
					topLevelExportGroups[groupIndex].push_back(
					    statements[i]);
					i++;
				}
				groupIndex++;
			}
		} else {
			i++;
			if (groupIndex < topLevelExportGroups.size() &&
			    !topLevelExportGroups[groupIndex].empty()) {
				groupIndex++;
			}
		}
	}

	std::vector<std::vector<Node*>> result;
	for (auto& exportGroup : topLevelExportGroups) {
		auto subGroups =
		    groupByNewlineContiguous(sourceFile, exportGroup);
		result.insert(result.end(), subGroups.begin(), subGroups.end());
	}

	return result;
}

// organizeExportsWorker — organizeimports.go:789.
void organizeExportsWorker(
    const std::vector<Node*>& oldExportDecls,
    const organizeImportsComparerSettings& comparer,
    SourceFile* sourceFile, change::Tracker* changeTracker) {
	if (oldExportDecls.empty()) {
		return;
	}

	auto specifierComparerFunc = lsutil::GetNamedImportSpecifierComparer(
	    lsutil::UserPreferences{
	        .OrganizeImportsTypeOrder = comparer.typeOrder},
	    comparer.namedImportComparer);

	auto newExportDecls = coalesceExportsWorker(
	    oldExportDecls, specifierComparerFunc,
	    comparer.moduleSpecifierComparer, sourceFile, changeTracker);

	if (!oldExportDecls.empty()) {
		if (newExportDecls.empty()) {
			changeTracker->DeleteNodeRange(
			    sourceFile, oldExportDecls.front(),
			    oldExportDecls.back(),
			    change::LeadingTriviaOptionExclude,
			    change::TrailingTriviaOptionInclude);
		} else {
			for (auto* exp : newExportDecls) {
				changeTracker->emitContext->addEmitFlags(
				    exp, printer::EFNoLeadingComments);
			}

			change::NodeOptions options{
			    .leadingTrivia =
			        change::LeadingTriviaOptionExclude,
			    .trailingTrivia =
			        change::TrailingTriviaOptionInclude,
			    .Suffix = "\n",
			};

			std::vector<Node*> newNodes;
			newNodes.reserve(newExportDecls.size());
			for (auto* s : newExportDecls) {
				newNodes.push_back(s);
			}
			changeTracker->ReplaceNodeWithNodes(
			    sourceFile, oldExportDecls.front(), newNodes, &options);

			if (oldExportDecls.size() > 1) {
				for (size_t i = 1; i < oldExportDecls.size(); i++) {
					changeTracker->Delete(sourceFile,
					                      oldExportDecls[i]);
				}
			}
		}
	}
}

// coalesceExportsWorker — organizeimports.go:833.
std::vector<Node*> coalesceExportsWorker(
    const std::vector<Node*>& exportGroup,
    const std::function<int(Node*, Node*)>& specifierComparer,
    const lsutil::StringComparer&
        moduleSpecifierComparer,
    SourceFile* sourceFile, change::Tracker* changeTracker) {
	if (exportGroup.empty()) {
		return exportGroup;
	}

	std::unordered_map<std::string, std::vector<Node*>>
	    exportsByModuleSpecifier;
	std::vector<std::string> moduleSpecifierOrder;

	for (auto* exportDecl : exportGroup) {
		auto* export_ = exportDecl->as<ExportDeclaration>();
		std::string moduleSpecifier;
		if (export_->ModuleSpecifier != nullptr) {
			moduleSpecifier = export_->ModuleSpecifier->text();
		}
		if (exportsByModuleSpecifier.find(moduleSpecifier) ==
		    exportsByModuleSpecifier.end()) {
			moduleSpecifierOrder.push_back(moduleSpecifier);
		}
		exportsByModuleSpecifier[moduleSpecifier].push_back(exportDecl);
	}

	std::stable_sort(moduleSpecifierOrder.begin(),
	                 moduleSpecifierOrder.end(),
	                 [&moduleSpecifierComparer](const std::string& a,
	                                            const std::string& b) {
		                 if (a.empty() && !b.empty()) {
			                 return false;
		                 }
		                 if (!a.empty() && b.empty()) {
			                 return true;
		                 }
		                 return moduleSpecifierComparer(a, b) < 0;
	                 });

	std::vector<Node*> coalescedExports;
	// Same lifetime rule as the import workers above: the tracker-owned
	// emit-context factory, since these decls are printed after return.
	NodeFactory& factory = *changeTracker->nodeFactory;

	for (auto& moduleSpecifier : moduleSpecifierOrder) {
		auto& group = exportsByModuleSpecifier[moduleSpecifier];

		auto categorized = getCategorizedExports(group);

		if (categorized.exportWithoutClause != nullptr) {
			coalescedExports.push_back(categorized.exportWithoutClause);
		}

		for (auto* subGroupPtr : std::vector<std::vector<Node*>*>{
		         &categorized.namedExports,
		         &categorized.typeOnlyExports}) {
			auto& subGroup = *subGroupPtr;
			if (subGroup.empty()) {
				continue;
			}

			std::vector<Node*> newExportSpecifiers;
			for (auto* exportDecl : subGroup) {
				auto* exportClause =
				    exportDecl->as<ExportDeclaration>()
				        ->ExportClause;
				if (exportClause != nullptr &&
				    exportClause->kind == Kind::NamedExports) {
					auto* namedExports =
					    exportClause->as<NamedExports>();
					newExportSpecifiers.insert(
					    newExportSpecifiers.end(),
					    namedExports->Elements->nodes.begin(),
					    namedExports->Elements->nodes.end());
				}
			}

			std::stable_sort(newExportSpecifiers.begin(),
			                 newExportSpecifiers.end(),
			                 [&specifierComparer](Node* a, Node* b) {
				                 return specifierComparer(a, b) < 0;
			                 });

			auto* exportDecl = subGroup[0]->as<ExportDeclaration>();

			Node* updatedExportClause = nullptr;
			if (exportDecl->ExportClause != nullptr) {
				if (exportDecl->ExportClause->kind ==
				    Kind::NamedExports) {
					auto* namedExports =
					    exportDecl->ExportClause->as<NamedExports>();
					auto* sortedList =
					    factory.newNodeList(newExportSpecifiers);
					updatedExportClause = factory.updateNamedExports(
					    namedExports, sortedList);

					if (sourceFile != nullptr &&
					    !nodeIsSynthesized(namedExports) &&
					    !printer::RangeIsOnSingleLine(
					        namedExports->loc, sourceFile)) {
						changeTracker->emitContext->setEmitFlags(
						    updatedExportClause,
						    printer::EFMultiLine);
					}
				} else {
					updatedExportClause = exportDecl->ExportClause;
				}
			}

			auto* newExportDecl = factory.updateExportDeclaration(
			    exportDecl, exportDecl->Node::modifiers(),
			    exportDecl->IsTypeOnly, updatedExportClause,
			    exportDecl->ModuleSpecifier, exportDecl->Attributes);
			coalescedExports.push_back(newExportDecl);
		}
	}

	return coalescedExports;
}

// getCategorizedExports — organizeimports.go:932.
categorizedExports getCategorizedExports(
    const std::vector<Node*>& exportGroup) {
	Node* exportWithoutClause = nullptr;
	std::vector<Node*> namedExports;
	std::vector<Node*> typeOnlyExports;

	for (auto* exportDecl : exportGroup) {
		auto* export_ = exportDecl->as<ExportDeclaration>();
		if (export_->ExportClause == nullptr) {
			if (exportWithoutClause == nullptr) {
				exportWithoutClause = exportDecl;
			}
		} else if (export_->IsTypeOnly) {
			typeOnlyExports.push_back(exportDecl);
		} else {
			namedExports.push_back(exportDecl);
		}
	}

	return categorizedExports{
	    .exportWithoutClause = exportWithoutClause,
	    .namedExports = namedExports,
	    .typeOnlyExports = typeOnlyExports,
	};
}

} // namespace

} // namespace tsc::ls
