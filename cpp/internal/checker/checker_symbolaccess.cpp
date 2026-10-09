// Port of tsc/internal/checker/symbolaccessibility.go
// Slice: symbolaccess — the symbol-accessibility checker used by diagnostics
// and emit (isSymbolAccessibleWorker, getAccessibleSymbolChain,
// getExternalModuleContainer, accessibility helpers).

#include <algorithm>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/checker/types.h"
#include "internal/printer/emitcontext.h"

namespace tsc::checker {

// canHaveLocals — defined in checker_utilities.cpp (utilities.go:183).
bool canHaveLocals(Node* node);

namespace {

// Go core.Some — `static` here; checker.cpp keeps its own TU-local copy.
template <class T, class F>
bool someList(const std::vector<T>& ts, F&& f) {
	for (const T& t : ts) {
		if (f(t)) {
			return true;
		}
	}
	return false;
}

// Go core.MapNonNil
template <class T, class F>
std::vector<std::invoke_result_t<F, T>> mapNonNil(const std::vector<T>& ts,
                                                  F&& f) {
	std::vector<std::invoke_result_t<F, T>> result;
	for (const T& t : ts) {
		if (auto v = f(t); v != nullptr) {
			result.push_back(v);
		}
	}
	return result;
}

// Go core.FirstNonNil
template <class T, class F>
std::invoke_result_t<F, T> firstNonNil(const std::vector<T>& ts, F&& f) {
	for (const T& t : ts) {
		if (auto v = f(t); v != nullptr) {
			return v;
		}
	}
	return nullptr;
}

// checker/utilities.go:1304 — getDeclarationsOfKind (deduped copy; the
// declchecks slice keeps a `static` one).
std::vector<Node*> getDeclarationsOfKind(Symbol* symbol, Kind kind) {
	std::vector<Node*> result;
	for (Node* d : symbol->data->declarations) {
		if (d->kind == kind) {
			result.push_back(d);
		}
	}
	return result;
}

// --- hasNonGlobalAugmentationExternalModuleSymbol — symbolaccessibility.go:105
bool hasNonGlobalAugmentationExternalModuleSymbol(Node* declaration) {
	return isModuleWithStringLiteralName(declaration) ||
	       (declaration->kind == Kind::SourceFile &&
	        isExternalOrCommonJSModule(declaration->as<SourceFile>()));
}

// --- getQualifiedLeftMeaning — symbolaccessibility.go:109 --------------------
SymbolFlags getQualifiedLeftMeaning(SymbolFlags rightMeaning) {
	// If we are looking in value space, the parent meaning is value, other wise it is namespace
	if (rightMeaning == SymbolFlagsValue) {
		return SymbolFlagsValue;
	}
	return SymbolFlagsNamespace;
}

// --- hasExternalModuleSymbol — symbolaccessibility.go:249 --------------------
bool hasExternalModuleSymbol(Node* declaration) {
	return isAmbientModule(declaration) ||
	       (declaration->kind == Kind::SourceFile &&
	        isExternalOrCommonJSModule(declaration->as<SourceFile>()));
}

// --- isUMDExportSymbol — symbolaccessibility.go:612 --------------------------
bool isUMDExportSymbol(Symbol* symbol) {
	return symbol != nullptr && !symbol->data->declarations.empty() &&
	       symbol->data->declarations[0] != nullptr &&
	       isNamespaceExportDeclaration(symbol->data->declarations[0]);
}

// --- isNamespaceReexportDeclaration — symbolaccessibility.go:616 -------------
bool isNamespaceReexportDeclaration(Node* node) {
	return isNamespaceExport(node) && node->parent->moduleSpecifier() != nullptr;
}

// --- isPropertyOrMethodDeclarationSymbol — symbolaccessibility.go:728 --------
bool isPropertyOrMethodDeclarationSymbol(Symbol* symbol) {
	if (!symbol->data->declarations.empty()) {
		for (Node* declaration : symbol->data->declarations) {
			switch (declaration->kind) {
			case Kind::PropertyDeclaration:
			case Kind::MethodDeclaration:
			case Kind::GetAccessor:
			case Kind::SetAccessor:
				continue;
			default:
				return false;
			}
		}
		return true;
	}
	return false;
}

// symbolTableID uniquely identifies a symbol table by encoding its source.
// The high 3 bits encode the kind, and the remaining bits encode the
// NodeId or SymbolId of the source.
// (symbolaccessibility.go:402-415)
constexpr symbolTableID stKindShift = 61;
constexpr symbolTableID stKindLocals = 0ull << stKindShift;
constexpr symbolTableID stKindExports = 1ull << stKindShift;
constexpr symbolTableID stKindMembers = 2ull << stKindShift;
constexpr symbolTableID stKindGlobals = 3ull << stKindShift;
constexpr symbolTableID stKindResolvedExports =
	4ull << stKindShift; // resolved/derived exports from getExportsOfSymbol, distinct from raw sym.Exports

// stKindMask extracts the kind bits from a symbolTableID.
// Go: stKindMask symbolTableID = (iota - 1) << stKindShift — iota is 5 there,
// so the mask is 4 << stKindShift (only bit 63; members/globals/etc. never
// match the mask checks — faithfully replicated quirk).
constexpr symbolTableID stKindMask = 4ull << stKindShift;

// --- symbolTableIDFromLocals — symbolaccessibility.go:417 --------------------
symbolTableID symbolTableIDFromLocals(Node* node) {
	return stKindLocals | symbolTableID(getNodeId(node));
}

// --- symbolTableIDFromExports — symbolaccessibility.go:421 -------------------
symbolTableID symbolTableIDFromExports(Symbol* sym) {
	return stKindExports | symbolTableID(getSymbolId(sym));
}

// symbolTableIDFromResolvedExports returns an ID for resolved/derived export tables
// (e.g. from getExportsOfSymbol/getExportsOfModule which may include export * resolution
// and late-bound members). This is distinct from symbolTableIDFromExports to prevent
// cache collisions with raw sym.Exports tables passed by someSymbolTableInScope.
// (symbolaccessibility.go:429)
symbolTableID symbolTableIDFromResolvedExports(Symbol* sym) {
	return stKindResolvedExports | symbolTableID(getSymbolId(sym));
}

// --- symbolTableIDFromMembers — symbolaccessibility.go:433 -------------------
symbolTableID symbolTableIDFromMembers(Symbol* sym) {
	return stKindMembers | symbolTableID(getSymbolId(sym));
}

// --- symbolTableIDFromGlobals — symbolaccessibility.go:437 -------------------
symbolTableID symbolTableIDFromGlobals() { return stKindGlobals; }

} // namespace

// --- IsTypeSymbolAccessible — symbolaccessibility.go:11 ----------------------
bool Checker::IsTypeSymbolAccessible(Symbol* typeSymbol,
                                     Node* enclosingDeclaration) {
	printer::SymbolAccessibilityResult access = isSymbolAccessibleWorker(
		typeSymbol, enclosingDeclaration, SymbolFlagsType,
		/*shouldComputeAliasesToMakeVisible*/ false, /*allowModules*/ true);
	return access.Accessibility == printer::SymbolAccessibility::Accessible;
}

// --- IsValueSymbolAccessible — symbolaccessibility.go:16 ---------------------
bool Checker::IsValueSymbolAccessible(Symbol* symbol,
                                      Node* enclosingDeclaration) {
	printer::SymbolAccessibilityResult access = isSymbolAccessibleWorker(
		symbol, enclosingDeclaration, SymbolFlagsValue,
		/*shouldComputeAliasesToMakeVisible*/ false, /*allowModules*/ true);
	return access.Accessibility == printer::SymbolAccessibility::Accessible;
}

// --- IsSymbolAccessibleByFlags — symbolaccessibility.go:21 -------------------
bool Checker::IsSymbolAccessibleByFlags(Symbol* symbol,
                                        Node* enclosingDeclaration,
                                        SymbolFlags flags) {
	printer::SymbolAccessibilityResult access = isSymbolAccessibleWorker(
		symbol, enclosingDeclaration, flags,
		/*shouldComputeAliasesToMakeVisible*/ false,
		/*allowModules*/ false); // TODO: Strada bug? Why is this allowModules: false?
	return access.Accessibility == printer::SymbolAccessibility::Accessible;
}

// --- IsAnySymbolAccessible — symbolaccessibility.go:26 -----------------------
printer::SymbolAccessibilityResult* Checker::IsAnySymbolAccessible(
	const std::vector<Symbol*>& symbols, Node* enclosingDeclaration,
	Symbol* initialSymbol, SymbolFlags meaning,
	bool shouldComputeAliasesToMakeVisible, bool allowModules) {
	if (symbols.empty()) {
		return nullptr;
	}

	Symbol* hadAccessibleChain = nullptr;
	bool earlyModuleBail = false;
	for (Symbol* symbol : symbols) {
		// Symbol is accessible if it by itself is accessible
		std::vector<Symbol*> accessibleSymbolChain = getAccessibleSymbolChain(
			symbol, enclosingDeclaration, meaning, /*useOnlyExternalAliasing*/ false);
		if (!accessibleSymbolChain.empty()) {
			hadAccessibleChain = symbol;
			printer::SymbolAccessibilityResult* hasAccessibleDeclarations =
				hasVisibleDeclarations(
					accessibleSymbolChain[0], shouldComputeAliasesToMakeVisible);
			if (hasAccessibleDeclarations != nullptr) {
				return hasAccessibleDeclarations;
			}
		}
		if (allowModules) {
			if (someList(symbol->data->declarations,
			             hasNonGlobalAugmentationExternalModuleSymbol)) {
				if (shouldComputeAliasesToMakeVisible) {
					earlyModuleBail = true;
					// Generally speaking, we want to use the aliases that already exist to refer to a module, if present
					// In order to do so, we need to find those aliases in order to retain them in declaration emit; so
					// if we are in declaration emit, we cannot use the fast path for module visibility until we've exhausted
					// all other visibility options (in order to capture the possible aliases used to reference the module)
					continue;
				}
				// Any meaning of a module symbol is always accessible via an `import` type
				return new printer::SymbolAccessibilityResult{
					.Accessibility = printer::SymbolAccessibility::Accessible,
				};
			}
		}

		// If we haven't got the accessible symbol, it doesn't mean the symbol is actually inaccessible.
		// It could be a qualified symbol and hence verify the path
		// e.g.:
		// module m {
		//     export class c {
		//     }
		// }
		// const x: typeof m.c
		// In the above example when we start with checking if typeof m.c symbol is accessible,
		// we are going to see if c can be accessed in scope directly.
		// But it can't, hence the accessible is going to be undefined, but that doesn't mean m.c is inaccessible
		// It is accessible if the parent m is accessible because then m.c can be accessed through qualification

		std::vector<Symbol*> containers =
			getContainersOfSymbol(symbol, enclosingDeclaration, meaning);
		SymbolFlags nextMeaning = meaning;
		if (initialSymbol == symbol) {
			nextMeaning = getQualifiedLeftMeaning(meaning);
		}
		printer::SymbolAccessibilityResult* parentResult = IsAnySymbolAccessible(
			containers, enclosingDeclaration, initialSymbol, nextMeaning,
			shouldComputeAliasesToMakeVisible, allowModules);
		if (parentResult != nullptr) {
			return parentResult;
		}
	}

	if (earlyModuleBail) {
		return new printer::SymbolAccessibilityResult{
			.Accessibility = printer::SymbolAccessibility::Accessible,
		};
	}

	if (hadAccessibleChain != nullptr) {
		std::string moduleName;
		if (hadAccessibleChain != initialSymbol) {
			moduleName = symbolToStringEx(hadAccessibleChain, enclosingDeclaration,
			                            SymbolFlagsNamespace,
			                            SymbolFormatFlagsAllowAnyNodeKind);
		}
		return new printer::SymbolAccessibilityResult{
			.Accessibility = printer::SymbolAccessibility::NotAccessible,
			.ErrorSymbolName = symbolToStringEx(initialSymbol, enclosingDeclaration,
			                                   meaning,
			                                   SymbolFormatFlagsAllowAnyNodeKind),
			.ErrorModuleName = moduleName,
		};
	}
	return nullptr;
}

// --- getWithAlternativeContainers — symbolaccessibility.go:117 ---------------
std::vector<Symbol*> Checker::getWithAlternativeContainers(
	Symbol* container, Symbol* symbol, Node* enclosingDeclaration,
	SymbolFlags meaning) {
	std::vector<Symbol*> additionalContainers = mapNonNil(
		container->data->declarations, [this, container](Node* d) -> Symbol* {
			return getFileSymbolIfFileSymbolExportEqualsContainer(d, container);
		});
	std::vector<Symbol*> reexportContainers;
	if (enclosingDeclaration != nullptr) {
		reexportContainers =
			getAlternativeContainingModules(symbol, enclosingDeclaration);
	}
	Symbol* objectLiteralContainer =
		getVariableDeclarationOfObjectLiteral(container, meaning);
	SymbolFlags leftMeaning = getQualifiedLeftMeaning(meaning);
	if (enclosingDeclaration != nullptr &&
	    (container->flags & leftMeaning) != 0 &&
	    !getAccessibleSymbolChain(container, enclosingDeclaration,
	                              SymbolFlagsNamespace,
	                              /*useOnlyExternalAliasing*/ false)
	         .empty()) {
		// This order expresses a preference for the real container if it is in scope
		std::vector<Symbol*> res{container};
		res.insert(res.end(), additionalContainers.begin(),
		           additionalContainers.end());
		res.insert(res.end(), reexportContainers.begin(),
		           reexportContainers.end());
		if (objectLiteralContainer != nullptr) {
			res.push_back(objectLiteralContainer);
		}
		return res;
	}
	// we potentially have a symbol which is a member of the instance side of something - look for a variable in scope with the container's type
	// which may be acting like a namespace (eg, `Symbol` acts like a namespace when looking up `Symbol.toStringTag`)
	std::vector<Symbol*> variableMatches;
	if ((meaning == SymbolFlagsValue && (container->flags & leftMeaning) == 0) &&
	    (container->flags & SymbolFlagsType) != 0 &&
	    (getDeclaredTypeOfSymbol(container)->flags & TypeFlagsObject) != 0) {
		someSymbolTableInScope(
			enclosingDeclaration,
			[this, &variableMatches, leftMeaning, container](
				const SymbolTable& t, symbolTableID, bool, bool,
				Node*) -> bool {
				bool found = false;
				for (const auto& [_, s] : t) {
					if ((s->flags & leftMeaning) != 0 &&
					    getTypeOfSymbol(s) == getDeclaredTypeOfSymbol(container)) {
						variableMatches.push_back(s);
						found = true;
					}
				}
				return found;
			});
		sortSymbols(variableMatches);
	}

	std::vector<Symbol*> res;
	res.insert(res.end(), variableMatches.begin(), variableMatches.end());
	res.insert(res.end(), additionalContainers.begin(),
	           additionalContainers.end());
	res.push_back(container);
	if (objectLiteralContainer != nullptr) {
		res.push_back(objectLiteralContainer);
	}
	res.insert(res.end(), reexportContainers.begin(), reexportContainers.end());
	return res;
}

// --- getAlternativeContainingModules — symbolaccessibility.go:168 ------------
std::vector<Symbol*> Checker::getAlternativeContainingModules(
	Symbol* symbol, Node* enclosingDeclaration) {
	if (enclosingDeclaration == nullptr) {
		return {};
	}
	SourceFile* containingFile = getSourceFileOfNode(enclosingDeclaration);
	NodeId id = getNodeId(containingFile->asNode());
	ContainingSymbolLinks* links = symbolContainerLinks.Get(symbol);
	if (auto it = links->extendedContainersByFile.find(id);
	    it != links->extendedContainersByFile.end() && !it->second.empty()) {
		return it->second;
	}
	std::vector<Symbol*> results;
	if (!containingFile->imports.empty()) {
		// Try to make an import using an import already in the enclosing file, if possible
		for (Node* importRef : containingFile->imports) {
			if (nodeIsSynthesized(importRef)) {
				// Synthetic names can't be resolved by `resolveExternalModuleName` - they'll cause a debug assert if they error
				continue;
			}
			Symbol* resolvedModule = resolveExternalModuleName(
				enclosingDeclaration, importRef, /*ignoreErrors*/ true,
				getImportAttributesTypeForModuleSpecifier(importRef));
			if (resolvedModule == nullptr) {
				continue;
			}
			Symbol* ref = getAliasForSymbolInContainer(resolvedModule, symbol);
			if (ref == nullptr) {
				continue;
			}
			results.push_back(resolvedModule);
		}
		if (!results.empty()) {
			links->extendedContainersByFile[id] = results;
			return results;
		}
	}

	if (links->extendedContainers != nullptr) {
		return *links->extendedContainers;
	}
	// No results from files already being imported by this file - expand search (not location-specific, so cached)
	results = getExternalModuleContainers(symbol);
	links->extendedContainers = new std::vector<Symbol*>(results);
	return results;
}

// getExternalModuleContainers — symbolaccessibility.go:220
std::vector<Symbol*> Checker::getExternalModuleContainers(Symbol* symbol) {
	if (externalModuleContainers == nullptr) {
		buildExternalModuleContainerIndex();
	}
	auto* index = externalModuleContainers;
	if (!index->complete) {
		// Re-entered from an alias resolved while building the index; answer this query without it.
		return scanExternalModuleContainers(symbol);
	}
	auto it = index->containersByTarget.find(getResolvedTarget(symbol));
	std::vector<Symbol*> containers;
	if (it != index->containersByTarget.end()) {
		containers = it->second;
	}
	Symbol* parent = getParentOfSymbol(symbol);
	auto moIt = index->moduleOrder.find(parent);
	if (moIt == index->moduleOrder.end()) {
		return containers;
	}
	int parentOrder = moIt->second;
	// The parent module contains the symbol even when the symbol is absent from its exports.
	auto at = std::lower_bound(containers.begin(), containers.end(), parentOrder,
		[index](Symbol* container, int order) { return index->moduleOrder[container] < order; });
	if (at == containers.end() || index->moduleOrder[*at] != parentOrder) {
		containers.insert(at, parent);
	}
	return containers;
}

// buildExternalModuleContainerIndex — symbolaccessibility.go:241
void Checker::buildExternalModuleContainerIndex() {
	auto* index = new externalModuleContainerIndex();
	externalModuleContainers = index;
	for (SourceFile* file : program->SourceFiles()) {
		if (!isExternalModule(file)) {
			continue;
		}
		Symbol* container = getSymbolOfDeclaration(file->asNode());
		index->moduleOrder[container] = (int)index->moduleOrder.size();
		for (auto& [name, exported] : getExportsOfSymbol(container)) {
			index->add(getResolvedTarget(exported), container);
		}
		if (Symbol* exportEquals = getSymbolFromTable(container->data->exports, InternalSymbolNameExportEquals)) {
			index->add(getResolvedTarget(exportEquals), container);
		}
	}
	index->complete = true;
}

// scanExternalModuleContainers — symbolaccessibility.go:259
std::vector<Symbol*> Checker::scanExternalModuleContainers(Symbol* symbol) {
	std::vector<Symbol*> containers;
	for (SourceFile* file : program->SourceFiles()) {
		if (!isExternalModule(file)) {
			continue;
		}
		if (Symbol* container = getSymbolOfDeclaration(file->asNode());
		    getAliasForSymbolInContainer(container, symbol) != nullptr) {
			containers.push_back(container);
		}
	}
	return containers;
}

// --- getVariableDeclarationOfObjectLiteral — symbolaccessibility.go:226 ------
Symbol* Checker::getVariableDeclarationOfObjectLiteral(Symbol* symbol,
                                                     SymbolFlags meaning) {
	// If we're trying to reference some object literal in, eg `var a = { x: 1 }`, the symbol for the literal, `__object`, is distinct
	// from the symbol of the declaration it is being assigned to. Since we can use the declaration to refer to the literal, however,
	// we'd like to make that connection here - potentially causing us to paint the declaration's visibility, and therefore the literal.
	if ((meaning & SymbolFlagsValue) == 0) {
		return nullptr;
	}
	if (symbol->data->declarations.empty()) {
		return nullptr;
	}
	Node* firstDecl = symbol->data->declarations[0];
	if (firstDecl->parent == nullptr) {
		return nullptr;
	}
	if (!isVariableDeclaration(firstDecl->parent)) {
		return nullptr;
	}
	if ((isObjectLiteralExpression(firstDecl) &&
	     firstDecl == firstDecl->parent->initializer()) ||
	    (isTypeLiteralNode(firstDecl) &&
	     firstDecl == firstDecl->parent->type())) {
		return getSymbolOfDeclaration(firstDecl->parent);
	}
	return nullptr;
}

// --- getExternalModuleContainer — symbolaccessibility.go:253 -----------------
Symbol* Checker::getExternalModuleContainer(Node* declaration) {
	Node* node = findAncestor(declaration, hasExternalModuleSymbol);
	if (node == nullptr) {
		return nullptr;
	}
	return getSymbolOfDeclaration(node);
}

// --- getFileSymbolIfFileSymbolExportEqualsContainer — symbolaccessibility.go:261
Symbol* Checker::getFileSymbolIfFileSymbolExportEqualsContainer(Node* d,
                                                                Symbol* container) {
	Symbol* fileSymbol = getExternalModuleContainer(d);
	if (fileSymbol == nullptr) {
		return nullptr;
	}
	auto it = fileSymbol->data->exports.find(InternalSymbolNameExportEquals);
	if (it == fileSymbol->data->exports.end() || it->second == nullptr) {
		return nullptr;
	}
	if (getSymbolIfSameReference(it->second, container) != nullptr) {
		return fileSymbol;
	}
	return nullptr;
}

/**
* Attempts to find the symbol corresponding to the container a symbol is in - usually this
* is just its' `.parent`, but for locals, this value is `undefined`
 */
// --- getContainersOfSymbol — symbolaccessibility.go:280 ----------------------
std::vector<Symbol*> Checker::getContainersOfSymbol(Symbol* symbol,
                                                  Node* enclosingDeclaration,
                                                  SymbolFlags meaning) {
	Symbol* container = getParentOfSymbol(symbol);
	// Type parameters end up in the `members` lists but are not externally visible
	if (container != nullptr &&
	    (symbol->flags & SymbolFlagsTypeParameter) == 0) {
		return getWithAlternativeContainers(container, symbol,
		                                    enclosingDeclaration, meaning);
	}
	std::vector<Symbol*> candidates;
	for (Node* d : symbol->data->declarations) {
		if (!isAmbientModule(d) && d->parent != nullptr) {
			// direct children of a module
			if (hasNonGlobalAugmentationExternalModuleSymbol(d->parent)) {
				Symbol* sym = getSymbolOfDeclaration(d->parent);
				if (sym != nullptr &&
				    std::find(candidates.begin(), candidates.end(), sym) ==
				        candidates.end()) {
					candidates.push_back(sym);
				}
				continue;
			}
			// export ='d member of an ambient module
			if (isModuleBlock(d->parent) && d->parent->parent != nullptr &&
			    resolveExternalModuleSymbol(
			        getSymbolOfDeclaration(d->parent->parent), false) == symbol) {
				Symbol* sym = getSymbolOfDeclaration(d->parent->parent);
				if (sym != nullptr &&
				    std::find(candidates.begin(), candidates.end(), sym) ==
				        candidates.end()) {
					candidates.push_back(sym);
				}
				continue;
			}
		}
		if (isClassExpression(d) && isBinaryExpression(d->parent) &&
		    d->parent->as<BinaryExpression>()->OperatorToken->kind ==
		        Kind::EqualsToken &&
		    isAccessExpression(d->parent->as<BinaryExpression>()->Left) &&
		    isEntityNameExpression(
		        d->parent->as<BinaryExpression>()->Left->expression())) {
			if (isModuleExportsAccessExpression(
			        d->parent->as<BinaryExpression>()->Left) ||
			    isExportsIdentifier(
			        d->parent->as<BinaryExpression>()->Left->expression())) {
				Symbol* sym =
					getSymbolOfDeclaration(getSourceFileOfNode(d)->asNode());
				if (sym != nullptr &&
				    std::find(candidates.begin(), candidates.end(), sym) ==
				        candidates.end()) {
					candidates.push_back(sym);
				}
				continue;
			}
			checkExpressionCached(
				d->parent->as<BinaryExpression>()->Left->expression());
			Symbol* sym =
				symbolNodeLinks
					.Get(d->parent->as<BinaryExpression>()->Left->expression())
					->resolvedSymbol;
			if (sym != nullptr &&
			    std::find(candidates.begin(), candidates.end(), sym) ==
			        candidates.end()) {
				candidates.push_back(sym);
			}
			continue;
		}
	}
	if (candidates.empty()) {
		return {};
	}

	std::vector<Symbol*> bestContainers;
	std::vector<Symbol*> alternativeContainers;
	for (Symbol* container : candidates) {
		if (getAliasForSymbolInContainer(container, symbol) == nullptr) {
			continue;
		}
		std::vector<Symbol*> allAlts = getWithAlternativeContainers(
			container, symbol, enclosingDeclaration, meaning);
		if (allAlts.empty()) {
			continue;
		}
		bestContainers.push_back(allAlts[0]);
		alternativeContainers.insert(alternativeContainers.end(),
		                             allAlts.begin() + 1, allAlts.end());
	}
	bestContainers.insert(bestContainers.end(), alternativeContainers.begin(),
	                      alternativeContainers.end());
	return bestContainers;
}

// --- getAliasForSymbolInContainer — symbolaccessibility.go:342 ---------------
Symbol* Checker::getAliasForSymbolInContainer(Symbol* container, Symbol* symbol) {
	if (container == getParentOfSymbol(symbol)) {
		// fast path, `symbol` is either already the alias or isn't aliased
		return symbol;
	}
	// Check if container is a thing with an `export=` which points directly at `symbol`, and if so, return
	// the container itself as the alias for the symbol
	if (auto it = container->data->exports.find(InternalSymbolNameExportEquals);
	    it != container->data->exports.end() && it->second != nullptr &&
	    getSymbolIfSameReference(it->second, symbol) != nullptr) {
		return container;
	}
	const SymbolTable& exports = getExportsOfSymbol(container);
	if (auto it = exports.find(symbol->data->name);
	    it != exports.end() && it->second != nullptr &&
	    getSymbolIfSameReference(it->second, symbol) != nullptr) {
		return it->second;
	}
	std::vector<Symbol*> candidates;
	for (const auto& [_, exported] : exports) {
		if (getSymbolIfSameReference(exported, symbol) != nullptr) {
			candidates.push_back(exported);
		}
	}
	if (!candidates.empty()) {
		sortSymbols(candidates); // _must_ sort exports for stable results - symbol table is randomly iterated
		return candidates[0];
	}
	return nullptr;
}

// --- getAccessibleSymbolChain — symbolaccessibility.go:373 -------------------
std::vector<Symbol*> Checker::getAccessibleSymbolChain(
	Symbol* symbol, Node* enclosingDeclaration, SymbolFlags meaning,
	bool useOnlyExternalAliasing) {
	return getAccessibleSymbolChainEx(accessibleSymbolChainContext{
		symbol,
		enclosingDeclaration,
		meaning,
		useOnlyExternalAliasing,
		std::make_shared<std::unordered_map<
			SymbolId, std::unordered_set<symbolTableID>>>()});
}

// --- GetAccessibleSymbolChain — symbolaccessibility.go:382 -------------------
std::vector<Symbol*> Checker::GetAccessibleSymbolChain(
	Symbol* symbol, Node* enclosingDeclaration, SymbolFlags meaning,
	bool useOnlyExternalAliasing) {
	return getAccessibleSymbolChain(symbol, enclosingDeclaration, meaning,
	                                useOnlyExternalAliasing);
}

// --- getAccessibleSymbolChainEx — symbolaccessibility.go:441 -----------------
std::vector<Symbol*> Checker::getAccessibleSymbolChainEx(
	accessibleSymbolChainContext ctx) {
	if (ctx.symbol == nullptr) {
		return {};
	}
	if (isPropertyOrMethodDeclarationSymbol(ctx.symbol)) {
		return {};
	}
	// Go from enclosingDeclaration to the first scope we check, so the cache is keyed off the scope and thus shared more
	Node* firstRelevantLocation = nullptr;
	someSymbolTableInScope(
		ctx.enclosingDeclaration,
		[&firstRelevantLocation](const SymbolTable&, symbolTableID, bool, bool,
		                         Node* node) -> bool {
			firstRelevantLocation = node;
			return true;
		});
	ContainingSymbolLinks* links = symbolContainerLinks.Get(ctx.symbol);
	AccessibleChainCacheKey linkKey{ctx.useOnlyExternalAliasing,
	                                firstRelevantLocation, ctx.meaning};
	if (auto it = links->accessibleChainCache.find(linkKey);
	    it != links->accessibleChainCache.end()) {
		return it->second;
	}

	std::vector<Symbol*> result;

	someSymbolTableInScope(
		ctx.enclosingDeclaration,
		[this, &ctx, &result](const SymbolTable& t, symbolTableID tableId,
		                      bool ignoreQualification, bool isLocalNameLookup,
		                      Node*) -> bool {
			std::vector<Symbol*> res = getAccessibleSymbolChainFromSymbolTable(
				ctx, t, tableId, ignoreQualification, isLocalNameLookup);
			if (!res.empty()) {
				result = res;
				return true;
			}
			return false;
		});
	links->accessibleChainCache[linkKey] = result;
	return result;
}

/**
* @param {ignoreQualification} boolean Set when a symbol is being looked for through the exports of another symbol (meaning we have a route to qualify it already)
 */
// --- getAccessibleSymbolChainFromSymbolTable — symbolaccessibility.go:481 ----
std::vector<Symbol*> Checker::getAccessibleSymbolChainFromSymbolTable(
	accessibleSymbolChainContext ctx, const SymbolTable& t, symbolTableID tableId,
	bool ignoreQualification, bool isLocalNameLookup) {
	SymbolId symId = getSymbolId(ctx.symbol);
	auto& visitedSymbolTables = (*ctx.visitedSymbolTablesMap)[symId];

	if (visitedSymbolTables.count(tableId) != 0) {
		return {};
	}
	visitedSymbolTables.insert(tableId);

	std::vector<Symbol*> res =
		trySymbolTable(ctx, t, tableId, ignoreQualification, isLocalNameLookup);

	visitedSymbolTables.erase(tableId);
	return res;
}

// getSymbolTableAliases returns only the alias symbols from a symbol table,
// caching the result by tableId to avoid repeated iteration over large tables.
// Members tables are skipped entirely since someSymbolTableInScope filters them
// to SymbolFlagsType & ~SymbolFlagsAssignment, which never includes aliases.
// --- getSymbolTableAliases — symbolaccessibility.go:505 ----------------------
std::vector<Symbol*> Checker::getSymbolTableAliases(const SymbolTable& symbols,
                                                  symbolTableID tableId) {
	symbolTableID kind = tableId & stKindMask;
	// Members tables never contain alias symbols; skip entirely.
	if (kind == stKindMembers) {
		return {};
	}
	// Cache globals and exports tables (which are large and revisited often).
	// Locals tables are small and per-scope, so they are filtered but not cached.
	if (kind == stKindGlobals || kind == stKindExports ||
	    kind == stKindResolvedExports) {
		if (auto it = symbolTableAliasCache.find(tableId);
		    it != symbolTableAliasCache.end()) {
			return it->second;
		}
	}
	std::vector<Symbol*> aliases;
	for (const auto& [_, sym] : symbols) {
		if ((sym->flags & SymbolFlagsAlias) != 0) {
			aliases.push_back(sym);
		}
	}
	if (kind == stKindGlobals || kind == stKindExports ||
	    kind == stKindResolvedExports) {
		symbolTableAliasCache[tableId] = aliases;
	}
	return aliases;
}

// --- trySymbolTable — symbolaccessibility.go:535 ------------------------------
std::vector<Symbol*> Checker::trySymbolTable(accessibleSymbolChainContext ctx,
                                           const SymbolTable& symbols,
                                           symbolTableID tableId,
                                           bool ignoreQualification,
                                           bool isLocalNameLookup) {
	bool isGlobals = tableId == stKindGlobals;
	// If symbol is directly available by its name in the symbol table
	auto nameIt = symbols.find(ctx.symbol->data->name);
	bool ok = nameIt != symbols.end();
	Symbol* res = ok ? nameIt->second : nullptr;
	if (ok && res != nullptr &&
	    isAccessible(ctx, res, /*resolvedAliasSymbol*/ nullptr,
	                 ignoreQualification)) {
		return {ctx.symbol};
	}

	std::vector<std::vector<Symbol*>> candidateChains;

	// Check for ExportSymbol by direct name lookup rather than discovering it during
	// the alias iteration below (where it would never match, since only alias-flagged
	// symbols are iterated).
	if (ok && res != nullptr && res->data->exportSymbol != nullptr) {
		if (isAccessible(ctx, getMergedSymbol(res->data->exportSymbol),
		                 /*resolvedAliasSymbol*/ nullptr, ignoreQualification)) {
			candidateChains.push_back({ctx.symbol});
		}
	}

	// Iterate only alias symbols from the table (cached per tableId).
	// This avoids iterating thousands of non-alias symbols in large tables like globals.
	for (Symbol* symbolFromSymbolTable :
	     getSymbolTableAliases(symbols, tableId)) {
		// for every non-default, non-export= alias symbol in scope, check if it refers to or can chain to the target symbol
		if (symbolFromSymbolTable->data->name != InternalSymbolNameExportEquals &&
		    symbolFromSymbolTable->data->name != InternalSymbolNameDefault &&
		    !(isUMDExportSymbol(symbolFromSymbolTable) &&
		      ctx.enclosingDeclaration != nullptr &&
		      isExternalModule(getSourceFileOfNode(ctx.enclosingDeclaration))) &&
		    // If `!useOnlyExternalAliasing`, we can use any type of alias to get the name
		    (!ctx.useOnlyExternalAliasing ||
		     someList(symbolFromSymbolTable->data->declarations,
		              isExternalModuleImportEqualsDeclaration)) &&
		    // If we're looking up a local name to reference directly, omit namespace reexports, otherwise when we're trawling through an export list to make a dotted name, we can keep it
		    ((isLocalNameLookup &&
		      !someList(symbolFromSymbolTable->data->declarations,
		                isNamespaceReexportDeclaration)) ||
		     !isLocalNameLookup) &&
		    // While exports are generally considered to be in scope, export-specifier declared symbols are _not_
		    // See similar comment in `resolveName` for details
		    (ignoreQualification ||
		     getDeclarationsOfKind(symbolFromSymbolTable, Kind::ExportSpecifier)
		         .empty())) {
			Symbol* resolvedImportedSymbol =
				resolveAlias(symbolFromSymbolTable);
			std::vector<Symbol*> candidate = getCandidateListForSymbol(
				ctx, symbolFromSymbolTable, resolvedImportedSymbol,
				ignoreQualification);
			if (!candidate.empty()) {
				candidateChains.push_back(std::move(candidate));
			}
		}
	}

	if (!candidateChains.empty()) {
		// pick first, shortest
		std::stable_sort(candidateChains.begin(), candidateChains.end(),
		                 [this](const std::vector<Symbol*>& a,
		                        const std::vector<Symbol*>& b) {
			                 return compareSymbolChains(a, b) < 0;
		                 });
		return candidateChains[0];
	}

	// If there's no result and we're looking at the global symbol table, treat `globalThis` like an alias and try to lookup thru that
	if (isGlobals) {
		return getCandidateListForSymbol(ctx, globalThisSymbol, globalThisSymbol,
		                               ignoreQualification);
	}
	return {};
}

// --- getCandidateListForSymbol — symbolaccessibility.go:620 ------------------
std::vector<Symbol*> Checker::getCandidateListForSymbol(
	accessibleSymbolChainContext ctx, Symbol* symbolFromSymbolTable,
	Symbol* resolvedImportedSymbol, bool ignoreQualification) {
	if (isAccessible(ctx, symbolFromSymbolTable, resolvedImportedSymbol,
	                 ignoreQualification)) {
		return {symbolFromSymbolTable};
	}

	// Look in the exported members, if we can find accessibleSymbolChain, symbol is accessible using this chain
	// but only if the symbolFromSymbolTable can be qualified
	const SymbolTable& candidateTable = getExportsOfSymbol(resolvedImportedSymbol);
	if (candidateTable.empty()) {
		return {};
	}
	symbolTableID candidateTableId =
		symbolTableIDFromResolvedExports(resolvedImportedSymbol);
	std::vector<Symbol*> accessibleSymbolsFromExports =
		getAccessibleSymbolChainFromSymbolTable(
			ctx, candidateTable, candidateTableId, /*ignoreQualification*/ true,
			/*isLocalNameLookup*/ false);
	if (accessibleSymbolsFromExports.empty()) {
		return {};
	}
	if (!canQualifySymbol(ctx, symbolFromSymbolTable,
	                      getQualifiedLeftMeaning(ctx.meaning))) {
		return {};
	}
	std::vector<Symbol*> result{symbolFromSymbolTable};
	result.insert(result.end(), accessibleSymbolsFromExports.begin(),
	              accessibleSymbolsFromExports.end());
	return result;
}

// --- isAccessible — symbolaccessibility.go:647 --------------------------------
bool Checker::isAccessible(accessibleSymbolChainContext ctx,
                           Symbol* symbolFromSymbolTable,
                           Symbol* resolvedAliasSymbol,
                           bool ignoreQualification) {
	bool likeSymbols = false;
	if (ctx.symbol == resolvedAliasSymbol) {
		likeSymbols = true;
	}
	if (ctx.symbol == symbolFromSymbolTable) {
		likeSymbols = true;
	}
	Symbol* symbol = getMergedSymbol(ctx.symbol);
	if (symbol == getMergedSymbol(resolvedAliasSymbol)) {
		likeSymbols = true;
	}
	if (symbol == getMergedSymbol(symbolFromSymbolTable)) {
		likeSymbols = true;
	}
	if (!likeSymbols && resolvedAliasSymbol != nullptr &&
	    (resolvedAliasSymbol->flags & SymbolFlagsAlias) != 0) {
		// Follow the alias chain in case a merged alias points back at the
		// symbol through an intermediate alias (symbolaccessibility.go).
		std::unordered_set<Symbol*> seenAliases;
		while ((resolvedAliasSymbol->flags & SymbolFlagsAlias) != 0 &&
		       seenAliases.count(resolvedAliasSymbol) == 0) {
			seenAliases.insert(resolvedAliasSymbol);
			resolvedAliasSymbol =
			    getMergedSymbol(resolveAlias(resolvedAliasSymbol));
			if (symbol == resolvedAliasSymbol) {
				likeSymbols = true;
				break;
			}
		}
	}
	if (!likeSymbols) {
		return false;
	}
	// if the symbolFromSymbolTable is not external module (it could be if it was determined as ambient external module and would be in globals table)
	// and if symbolFromSymbolTable or alias resolution matches the symbol,
	// check the symbol can be qualified, it is only then this symbol is accessible
	return !someList(symbolFromSymbolTable->data->declarations,
	                 hasNonGlobalAugmentationExternalModuleSymbol) &&
	       (ignoreQualification ||
	        canQualifySymbol(ctx, getMergedSymbol(symbolFromSymbolTable),
	                         ctx.meaning));
}

// --- canQualifySymbol — symbolaccessibility.go:677 ----------------------------
bool Checker::canQualifySymbol(accessibleSymbolChainContext ctx,
                              Symbol* symbolFromSymbolTable,
                              SymbolFlags meaning) {
	// If the symbol is equivalent and doesn't need further qualification, this symbol is accessible
	return !needsQualification(symbolFromSymbolTable, ctx.enclosingDeclaration,
	                           meaning) ||
	       // If symbol needs qualification, make sure that parent is accessible, if it is then this symbol is accessible too
	       !getAccessibleSymbolChainEx(accessibleSymbolChainContext{
	            symbolFromSymbolTable->data->parent, ctx.enclosingDeclaration,
	            getQualifiedLeftMeaning(meaning), ctx.useOnlyExternalAliasing,
	            ctx.visitedSymbolTablesMap})
	            .empty();
}

// --- needsQualification — symbolaccessibility.go:688 --------------------------
bool Checker::needsQualification(Symbol* symbol, Node* enclosingDeclaration,
                                 SymbolFlags meaning) {
	bool qualify = false;
	someSymbolTableInScope(
		enclosingDeclaration,
		[this, symbol, meaning, &qualify](const SymbolTable& symbolTable,
		                                  symbolTableID, bool, bool,
		                                  Node*) -> bool {
			// If symbol of this name is not available in the symbol table we are ok
			auto it = symbolTable.find(symbol->data->name);
			if (it == symbolTable.end() || it->second == nullptr) {
				return false;
			}
			Symbol* symbolFromSymbolTable = getMergedSymbol(it->second);
			if (symbolFromSymbolTable == nullptr) {
				// Continue to the next symbol table
				return false;
			}
			// If the symbol with this name is present it should refer to the symbol
			if (symbolFromSymbolTable == symbol) {
				// No need to qualify
				return true;
			}

			// Qualify if the symbol from symbol table has same meaning as expected
			bool shouldResolveAlias =
				(symbolFromSymbolTable->flags & SymbolFlagsAlias) != 0 &&
				getDeclarationOfKind(symbolFromSymbolTable,
				                     Kind::ExportSpecifier) == nullptr;
			if (shouldResolveAlias) {
				symbolFromSymbolTable = resolveAlias(symbolFromSymbolTable);
			}
			SymbolFlags flags = symbolFromSymbolTable->flags;
			if (shouldResolveAlias) {
				flags = getSymbolFlags(symbolFromSymbolTable);
			}
			if ((flags & meaning) != 0) {
				qualify = true;
				return true;
			}

			// Continue to the next symbol table
			return false;
		});

	return qualify;
}

// --- someSymbolTableInScope — symbolaccessibility.go:746 ----------------------
bool Checker::someSymbolTableInScope(
	Node* enclosingDeclaration,
	const std::function<bool(const SymbolTable&, symbolTableID, bool, bool, Node*)>&
		callback) {
	for (Node* location = enclosingDeclaration; location != nullptr;
	     location = location->parent) {
		// Locals of a source file are not in scope (because they get merged into the global symbol table)
		if (canHaveLocals(location) && location->locals() != nullptr &&
		    !isGlobalSourceFile(location)) {
			if (callback(*location->locals(),
			             symbolTableIDFromLocals(location->asNode()), false, true,
			             location)) {
				return true;
			}
		}
		switch (location->kind) {
		case Kind::SourceFile:
		case Kind::ModuleDeclaration:
			if (isSourceFile(location) &&
			    !isExternalOrCommonJSModule(location->as<SourceFile>())) {
				break;
			}
			{
				Symbol* sym =
					getSymbolOfDeclaration(getReparsedNodeForNode(location));
				if (callback(sym->data->exports, symbolTableIDFromExports(sym), false,
				             true, location)) {
					return true;
				}
			}
			break;
		case Kind::ClassDeclaration:
		case Kind::ClassExpression:
		case Kind::InterfaceDeclaration: {
			// Type parameters are bound into `members` lists so they can merge across declarations
			// This is troublesome, since in all other respects, they behave like locals :cries:
			// TODO: the below is shared with similar code in `resolveName` - in fact, rephrasing all this symbol
			// lookup logic in terms of `resolveName` would be nice
			// The below is used to lookup type parameters within a class or interface, as they are added to the class/interface locals
			// These can never be latebound, so the symbol's raw members are sufficient. `getMembersOfNode` cannot be used, as it would
			// trigger resolving late-bound names, which we may already be in the process of doing while we're here!
			SymbolTable table;
			Symbol* sym = getSymbolOfDeclaration(location);
			// TODO: Should this filtered table be cached in some way?
			for (const auto& [key, memberSymbol] : sym->data->members) {
				if ((memberSymbol->flags &
				     (SymbolFlagsType & ~SymbolFlagsAssignment)) != 0) {
					table[key] = memberSymbol;
				}
			}
			if (!table.empty() &&
			    callback(table, symbolTableIDFromMembers(sym), false, false,
			             location)) {
				return true;
			}
			// Class expression names (e.g., `B` in `class B {}`) are not stored in any
			// scope table — the binder uses bindAnonymousDeclaration. Expose the name
			// binding here so getAccessibleSymbolChain can resolve self-references.
			// This mirrors the special casing of class expression names in
			// (*NameResolver).Resolve; if class names are ever bound differently
			// (e.g., via class-local type aliases), both sites should be updated.
			if (isClassExpression(location) &&
			    location->as<ClassExpression>()->name != nullptr) {
				SymbolTable* nameTable = getClassExpressionNameTable(location);
				if (nameTable != nullptr &&
				    callback(*nameTable,
				             symbolTableIDFromLocals(location->asNode()), false,
				             true, location)) {
					return true;
				}
			}
		} break;
		default:
			break;
		}
	}

	return callback(globals, symbolTableIDFromGlobals(), false, true, nullptr);
}

// getClassExpressionNameTable returns a cached symbol table containing the class
// expression's name binding. Class expression names are bound via
// bindAnonymousDeclaration and aren't stored in any container's locals, so this
// synthesized table lets someSymbolTableInScope expose them during accessibility checks.
// --- getClassExpressionNameTable — symbolaccessibility.go:810 -----------------
SymbolTable* Checker::getClassExpressionNameTable(Node* location) {
	NodeId nodeId = getNodeId(location);
	if (auto it = classExpressionNameTables.find(nodeId);
	    it != classExpressionNameTables.end()) {
		return &it->second;
	}
	Symbol* classSymbol = getSymbolOfDeclaration(location);
	std::string nameText = location->as<ClassExpression>()->name->text();
	if (nameText.empty() || classSymbol == nullptr) {
		return nullptr;
	}
	auto [it, _] = classExpressionNameTables.emplace(
		nodeId, SymbolTable{{nameText, classSymbol}});
	return &it->second;
}

/**
 * Check if the given symbol in given enclosing declaration is accessible and mark all associated alias to be visible if requested
 *
 * @param symbol a Symbol to check if accessible
 * @param enclosingDeclaration a Node containing reference to the symbol
 * @param meaning a SymbolFlags to check if such meaning of the symbol is accessible
 * @param shouldComputeAliasToMakeVisible a boolean value to indicate whether to return aliases to be mark visible in case the symbol is accessible
 */

// --- IsSymbolAccessible — symbolaccessibility.go:839 --------------------------
printer::SymbolAccessibilityResult Checker::IsSymbolAccessible(
	Symbol* symbol, Node* enclosingDeclaration, SymbolFlags meaning,
	bool shouldComputeAliasesToMakeVisible) {
	return isSymbolAccessibleWorker(symbol, enclosingDeclaration, meaning,
	                                shouldComputeAliasesToMakeVisible,
	                                /*allowModules*/ true);
}

// --- isSymbolAccessibleWorker — symbolaccessibility.go:843 --------------------
printer::SymbolAccessibilityResult Checker::isSymbolAccessibleWorker(
	Symbol* symbol, Node* enclosingDeclaration, SymbolFlags meaning,
	bool shouldComputeAliasesToMakeVisible, bool allowModules) {
	if (symbol != nullptr && enclosingDeclaration != nullptr) {
		printer::SymbolAccessibilityResult* result = IsAnySymbolAccessible(
			{symbol}, enclosingDeclaration, symbol, meaning,
			shouldComputeAliasesToMakeVisible, allowModules);
		if (result != nullptr) {
			return *result;
		}

		// This could be a symbol that is not exported in the external module
		// or it could be a symbol from different external module that is not aliased and hence cannot be named
		Symbol* symbolExternalModule = firstNonNil(
			symbol->data->declarations,
			[this](Node* d) -> Symbol* { return getExternalModuleContainer(d); });
		if (symbolExternalModule != nullptr) {
			Symbol* enclosingExternalModule =
				getExternalModuleContainer(enclosingDeclaration);
			if (symbolExternalModule != enclosingExternalModule) {
				// name from different external module that is not visible
				return printer::SymbolAccessibilityResult{
					.Accessibility =
						printer::SymbolAccessibility::CannotBeNamed,
					.ErrorSymbolName = symbolToStringEx(
						symbol, enclosingDeclaration, meaning,
						SymbolFormatFlagsAllowAnyNodeKind),
					.ErrorNode =
						ifElse<Node*>(isInJSFile(enclosingDeclaration),
						              enclosingDeclaration, nullptr),
					.ErrorModuleName = symbolToString(symbolExternalModule),
				};
			}
		}

		// Just a local name that is not accessible
		return printer::SymbolAccessibilityResult{
			.Accessibility = printer::SymbolAccessibility::NotAccessible,
			.ErrorSymbolName =
				symbolToStringEx(symbol, enclosingDeclaration, meaning,
				                 SymbolFormatFlagsAllowAnyNodeKind),
		};
	}

	return printer::SymbolAccessibilityResult{
		.Accessibility = printer::SymbolAccessibility::Accessible,
	};
}

// === dep stubs — removed when owner slice lands ===

// (deduped: EmitResolver::hasVisibleDeclarations defined in checker_emitresolver.cpp)

} // namespace tsc::checker
