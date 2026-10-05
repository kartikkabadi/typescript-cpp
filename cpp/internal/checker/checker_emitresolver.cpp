// Port of tsc/internal/checker/emitresolver.go — the emit resolver.
// Tracks which declarations are visible to declaration emit, resolves
// alias/entity references for emit, and creates type nodes via the node
// builder. Go's checkerMu locking is elided: the C++ checker is
// single-threaded (see the EmitResolver note in checker.h).

#include "checker.h"

#include <unordered_set>

namespace tsc::checker {

// ---------------------------------------------------------------------------
// Dep-stub declarations for free functions owned by other slices.
// Bodies live at the bottom of this file under the dep-stub banner.
// ---------------------------------------------------------------------------


namespace {

// isCommonJSModuleExports — emitresolver.go:249
bool isCommonJSModuleExports(Node* node) {
	if (isBinaryExpression(node) && isExpressionStatement(node->parent) &&
		isSourceFile(node->parent->parent) &&
		node->parent->parent->as<SourceFile>()->CommonJSModuleIndicator != nullptr) {
		switch (getAssignmentDeclarationKind(node)) {
		case JSDeclarationKind::ModuleExports:
		case JSDeclarationKind::ExportsProperty:
			return true;
		default:
			break;
		}
	}
	return false;
}

// noopAddVisibleAlias — emitresolver.go:376
void noopAddVisibleAlias(Node* /*declaration*/, Node* /*aliasingStatement*/) {}

// isConstEnumSymbol — emitresolver.go:693 (local replica)
bool isConstEnumSymbol(Symbol* s) {
	return (s->flags & SymbolFlagsConstEnum) != 0;
}

// isConstEnumOrConstEnumOnlyModule — emitresolver.go:696
bool isConstEnumOrConstEnumOnlyModule(Symbol* s) {
	return isConstEnumSymbol(s) || (s->flags & SymbolFlagsConstEnumOnlyModule) != 0;
}

// File-local replicas of helpers whose owning TUs keep per-TU copies (same
// convention as checker_declchecks2.cpp / checker_utilities.cpp).

// utilities.go:298 — isOptionalDeclaration
bool isOptionalDeclaration(Node* declaration) {
	return hasQuestionToken(declaration);
}

// ast/utilities.go:2639 — getDeclarationContainer
Node* getDeclarationContainer(Node* node) {
	return findAncestor(getRootDeclaration(node), [](Node* n) -> bool {
		switch (n->kind) {
		case Kind::VariableDeclaration:
		case Kind::VariableDeclarationList:
		case Kind::ImportSpecifier:
		case Kind::NamedImports:
		case Kind::NamespaceImport:
		case Kind::ImportClause:
			return false;
		default:
			return true;
		}
	})->parent;
}

// ast/utilities.go:1286 — walkUpBindingElementsAndPatterns
Node* walkUpBindingElementsAndPatterns(Node* node) {
	node = node->parent;
	while (isBindingElement(node->parent)) {
		node = node->parent->parent;
	}
	return node->parent;
}

// ast/utilities.go:2510 — isInternalModuleImportEqualsDeclaration
bool isInternalModuleImportEqualsDeclaration(Node* node) {
	return node->kind == Kind::ImportEqualsDeclaration &&
		node->as<ImportEqualsDeclaration>()->ModuleReference->kind !=
			Kind::ExternalModuleReference;
}

// utilities.go:871 — isDeclarationReadonly
bool isDeclarationReadonly(Node* declaration) {
	return (getCombinedModifierFlags(declaration) & ModifierFlagsReadonly) != 0 &&
		!isParameterPropertyDeclaration(declaration, declaration->parent);
}

// checker.go:23946 — isTupleType
bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 &&
		(t->Target()->objectFlags & ObjectFlagsTuple) != 0;
}

// ast/utilities.go:1261 — isVarConst
bool isVarConst(Node* node) {
	return (getCombinedNodeFlags(node) & NodeFlagsBlockScoped) == NodeFlagsConst;
}

// checker.cpp:169 — isFreshLiteralType
bool isFreshLiteralType(Type* t) {
	return (t->flags & TypeFlagsFreshable) != 0 && t->AsLiteralType()->freshType == t;
}

// utilities.go:1228 — pseudoBigIntToString
std::string pseudoBigIntToString(const PseudoBigInt& value) {
	return value.string();
}

}  // namespace

// getAnyImportSyntax — utilities.go:1657
// (deduped: canonical external def lives in checker_utilities.cpp)

// newEmitResolver — emitresolver.go:45
EmitResolver* newEmitResolver(Checker* c) {
	// (deduped: this stub lived in checker_services.cpp)
	EmitResolver* e = new EmitResolver();
	e->checker = c;
	e->isValueAliasDeclaration = [e](Node* node) -> bool {
		return e->isValueAliasDeclarationWorker(node);
	};
	e->aliasMarkingVisitor = [e](Node* node) -> bool {
		return e->aliasMarkingVisitorWorker(node);
	};
	return e;
}

// EmitResolver::GetJsxFactoryEntity — emitresolver.go:54
Node* EmitResolver::GetJsxFactoryEntity(Node* location) {
	return checker->getJsxFactoryEntity(location);
}

// EmitResolver::GetJsxFragmentFactoryEntity — emitresolver.go:60
Node* EmitResolver::GetJsxFragmentFactoryEntity(Node* location) {
	return checker->getJsxFragmentFactoryEntity(location);
}

// EmitResolver::IsOptionalParameter — emitresolver.go:66
bool EmitResolver::IsOptionalParameter(Node* node) {
	return isOptionalParameter(node);
}

// EmitResolver::IsLateBound — emitresolver.go:72
bool EmitResolver::IsLateBound(Node* node) {
	// TODO: Require an emitContext to construct an EmitResolver, remove all emitContext arguments
	// node = r.emitContext.ParseNode(node)
	if (node == nullptr) {
		return false;
	}
	if (!isParseTreeNode(node)) {
		return false;
	}
	Symbol* symbol = checker->getSymbolOfDeclaration(node);
	if (symbol == nullptr) {
		return false;
	}
	return (symbol->checkFlags & CheckFlagsLate) != 0;
}

// EmitResolver::GetEnumMemberValue — emitresolver.go:88
EvalResult EmitResolver::GetEnumMemberValue(Node* node) {
	// node = r.emitContext.ParseNode(node)
	if (!isParseTreeNode(node)) {
		return newEvalResult(std::monostate{}, false, false, false);
	}

	checker->computeEnumMemberValues(node->parent);
	if (!checker->enumMemberLinks.Has(node)) {
		return newEvalResult(std::monostate{}, false, false, false);
	}
	return checker->enumMemberLinks.Get(node)->value;
}

// EmitResolver::IsDeclarationVisible — emitresolver.go:105
bool EmitResolver::IsDeclarationVisible(Node* node) {
	// Only lock on external API func to prevent deadlocks
	return isDeclarationVisible(node);
}

// EmitResolver::isDeclarationVisible — emitresolver.go:112
bool EmitResolver::isDeclarationVisible(Node* node) {
	// node = r.emitContext.ParseNode(node)
	if (!isParseTreeNode(node)) {
		return false;
	}
	if (node == nullptr) {
		return false;
	}

	DeclarationLinks* links = declarationLinks.Get(node);
	if (links->isVisible == Tristate::Unknown) {
		if (determineIfDeclarationIsVisible(node)) {
			links->isVisible = Tristate::True;
		} else {
			links->isVisible = Tristate::False;
		}
	}
	return links->isVisible == Tristate::True;
}

// EmitResolver::determineIfDeclarationIsVisible — emitresolver.go:131
bool EmitResolver::determineIfDeclarationIsVisible(Node* node) {
	switch (node->kind) {
	case Kind::JSDocCallbackTag:
		// ast.KindJSDocEnumTag, // !!! TODO: JSDoc @enum support?
	case Kind::JSDocTypedefTag:
		// Top-level jsdoc type aliases are considered exported
		// First parent is comment node, second is hosting declaration or token; we only care about those tokens or declarations whose parent is a source file
		return node->parent != nullptr && node->parent->parent != nullptr &&
			node->parent->parent->parent != nullptr &&
			isSourceFile(node->parent->parent->parent);
	case Kind::BindingElement:
		return isDeclarationVisible(node->parent->parent);
	case Kind::VariableDeclaration:
	case Kind::ModuleDeclaration:
	case Kind::ClassDeclaration:
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::FunctionDeclaration:
	case Kind::EnumDeclaration:
	case Kind::ImportEqualsDeclaration: {
		if (isVariableDeclaration(node)) {
			if (isBindingPattern(node->name()) &&
				node->name()->elements().empty()) {
				// If the binding pattern is empty, this variable declaration is not visible
				return false;
			}
			// falls through
		}
		// External module augmentation is always visible
		// A @typedef at top-level in an external module is always visible
		if (isExternalModuleAugmentation(node) || isImplicitlyExportedJSDocDeclaration(node)) {
			return true;
		}
		Node* parent = getDeclarationContainer(node);
		// If the node is not exported or it is not ambient module element (except import declaration)
		if ((checker->getCombinedModifierFlagsCached(node) & ModifierFlagsExport) == 0 &&
			!(node->kind != Kind::ImportEqualsDeclaration && parent->kind != Kind::SourceFile &&
			  (parent->flags & NodeFlagsAmbient) != 0)) {
			return isGlobalSourceFile(parent);
		}
		// Exported members/ambient module elements (exception import declaration) are visible if parent is visible
		return isDeclarationVisible(parent);
	}

	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
		if (checker->GetEffectiveDeclarationFlags(
				node, ModifierFlagsPrivate | ModifierFlagsProtected) != 0) {
			// Private/protected properties/methods are not visible
			return false;
		}
		// Public properties/methods are visible if its parents are visible, so:
		return isDeclarationVisible(node->parent);

	case Kind::Constructor:
	case Kind::ConstructSignature:
	case Kind::CallSignature:
	case Kind::IndexSignature:
	case Kind::Parameter:
	case Kind::ModuleBlock:
	case Kind::FunctionType:
	case Kind::ConstructorType:
	case Kind::TypeLiteral:
	case Kind::TypeReference:
	case Kind::ArrayType:
	case Kind::TupleType:
	case Kind::UnionType:
	case Kind::IntersectionType:
	case Kind::ParenthesizedType:
	case Kind::NamedTupleMember:
		return isDeclarationVisible(node->parent);

	// Default binding, import specifier and namespace import is visible
	// only on demand so by default it is not visible
	case Kind::ImportClause:
	case Kind::NamespaceImport:
	case Kind::ImportSpecifier:
		return false;

	// Type parameters are always visible
	case Kind::TypeParameter:
		return true;
	// Source file and namespace export are always visible
	case Kind::SourceFile:
	case Kind::NamespaceExportDeclaration:
		return true;

	// Export assignments do not create name bindings outside the module
	case Kind::ExportAssignment:
		return false;

	// An `export {X}` (without a module specifier) is itself a visible re-export of
	// the named binding; it contributes to the symbol's external visibility.
	case Kind::ExportSpecifier: {
		Node* exportDecl = node->parent->parent;
		if (isExportDeclaration(exportDecl) &&
			exportDecl->as<ExportDeclaration>()->ModuleSpecifier == nullptr) {
			return isDeclarationVisible(exportDecl->parent);
		}
		return false;
	}

	default:
		return false;
	}
}

// EmitResolver::PrecalculateDeclarationEmitVisibility — emitresolver.go:241
void EmitResolver::PrecalculateDeclarationEmitVisibility(SourceFile* file) {
	if (declarationFileLinks.Get(file->asNode())->aliasesMarked) {
		return;
	}
	declarationFileLinks.Get(file->asNode())->aliasesMarked = true;
	// TODO: Does this even *have* to be an upfront walk? If it's not possible for a
	// import a = a.b.c statement to chain into exposing a statement in a sibling scope,
	// it could at least be pushed into scope entry -  then it wouldn't need to be recursive.
	file->asNode()->forEachChild(aliasMarkingVisitor);
}

// EmitResolver::aliasMarkingVisitorWorker — emitresolver.go:260
bool EmitResolver::aliasMarkingVisitorWorker(Node* node) {
	switch (node->kind) {
	case Kind::BinaryExpression:
		if (isCommonJSModuleExports(node) && isIdentifier(node->as<BinaryExpression>()->Right)) {
			markLinkedAliases(node->as<BinaryExpression>()->Right);
		}
		break;
	case Kind::ExportAssignment:
		if (node->expression()->kind == Kind::Identifier) {
			markLinkedAliases(node->expression());
		}
		break;
	case Kind::ExportSpecifier:
		markLinkedAliases(node->propertyNameOrName());
		break;
	default:
		break;
	}
	return node->forEachChild(aliasMarkingVisitor);
}

// EmitResolver::markLinkedAliases — emitresolver.go:278
// Sets the isVisible link on statements the Identifier or ExportName node points at
// Follows chains of import d = a.b.c
void EmitResolver::markLinkedAliases(Node* node) {
	Symbol* exportSymbol = nullptr;
	if (node->kind != Kind::StringLiteral && node->parent != nullptr &&
		(isExportAssignment(node->parent) || isCommonJSModuleExports(node->parent))) {
		exportSymbol = checker->resolveName(
			node, node->text(),
			SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace | SymbolFlagsAlias,
			nullptr /*nameNotFoundMessage*/, false /*isUse*/, false);
	} else if (node->parent->kind == Kind::ExportSpecifier) {
		exportSymbol = checker->getTargetOfExportSpecifier(
			node->parent,
			SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace | SymbolFlagsAlias,
			false);
	}

	std::unordered_set<SymbolId> visited; // guard against circular imports
	while (exportSymbol != nullptr) {
		SymbolId id = getSymbolId(exportSymbol);
		if (visited.count(id) != 0) {
			break;
		}
		visited.insert(id);

		Symbol* nextSymbol = nullptr;
		for (Node* declaration : exportSymbol->declarations) {
			declarationLinks.Get(declaration)->isVisible = Tristate::True;

			if (isInternalModuleImportEqualsDeclaration(declaration)) {
				// Add the referenced top container visible
				Node* internalModuleReference =
					declaration->as<ImportEqualsDeclaration>()->ModuleReference;
				Node* firstIdentifier = getFirstIdentifier(internalModuleReference);
				nextSymbol = checker->resolveName(
					declaration, firstIdentifier->text(),
					SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace | SymbolFlagsAlias,
					nullptr /*nameNotFoundMessage*/, false /*isUse*/, false);
			}
		}

		exportSymbol = nextSymbol;
	}
}

// getMeaningOfEntityNameReference — emitresolver.go:307
// (deduped: defined canonically in checker_nodecopy.cpp)

// EmitResolver::IsEntityNameVisible — emitresolver.go:329
printer::SymbolAccessibilityResult EmitResolver::IsEntityNameVisible(
	Node* entityName, Node* enclosingDeclaration) {
	return isEntityNameVisible(entityName, enclosingDeclaration, true);
}

// EmitResolver::isEntityNameVisible — emitresolver.go:336
printer::SymbolAccessibilityResult EmitResolver::isEntityNameVisible(
	Node* entityName, Node* enclosingDeclaration, bool shouldComputeAliasToMakeVisible) {
	// node = r.emitContext.ParseNode(entityName)
	if (!isParseTreeNode(entityName)) {
		return printer::SymbolAccessibilityResult{
			.Accessibility = printer::SymbolAccessibility::NotAccessible};
	}

	SymbolFlags meaning = getMeaningOfEntityNameReference(entityName);
	Node* firstIdentifier = getFirstIdentifier(entityName);

	Symbol* symbol = checker->resolveName(enclosingDeclaration, firstIdentifier->text(),
										meaning, nullptr, false, false);

	if (symbol != nullptr && (symbol->flags & SymbolFlagsTypeParameter) != 0 &&
		(meaning & SymbolFlagsType) != 0) {
		return printer::SymbolAccessibilityResult{
			.Accessibility = printer::SymbolAccessibility::Accessible};
	}

	if (symbol == nullptr && isThisIdentifier(firstIdentifier)) {
		Symbol* sym = checker->getSymbolOfDeclaration(
			checker->getThisContainer(firstIdentifier, false, false));
		if (isSymbolAccessible(sym, enclosingDeclaration, meaning, false).Accessibility ==
			printer::SymbolAccessibility::Accessible) {
			return printer::SymbolAccessibilityResult{
				.Accessibility = printer::SymbolAccessibility::Accessible};
		}
	}

	if (symbol == nullptr) {
		return printer::SymbolAccessibilityResult{
			.Accessibility = printer::SymbolAccessibility::NotResolved,
			.ErrorSymbolName = firstIdentifier->text(),
			.ErrorNode = firstIdentifier,
		};
	}

	printer::SymbolAccessibilityResult* visible =
		hasVisibleDeclarations(symbol, shouldComputeAliasToMakeVisible);
	if (visible != nullptr) {
		return *visible;
	}

	return printer::SymbolAccessibilityResult{
		.Accessibility = printer::SymbolAccessibility::NotAccessible,
		.ErrorSymbolName = firstIdentifier->text(),
		.ErrorNode = firstIdentifier,
	};
}

// EmitResolver::hasVisibleDeclarations — emitresolver.go:379
printer::SymbolAccessibilityResult* EmitResolver::hasVisibleDeclarations(
	Symbol* symbol, bool shouldComputeAliasToMakeVisible) {
	std::unordered_map<NodeId, Node*> aliasesToMakeVisibleSet;
	std::unordered_map<NodeId, Node*>* aliasesToMakeVisible = nullptr;

	std::function<void(Node*, Node*)> addVisibleAlias;
	if (shouldComputeAliasToMakeVisible) {
		aliasesToMakeVisible = &aliasesToMakeVisibleSet;
		addVisibleAlias = [this, aliasesToMakeVisible](Node* declaration,
													 Node* aliasingStatement) {
			declarationLinks.Get(declaration)->isVisible = Tristate::True;
			(*aliasesToMakeVisible)[getNodeId(declaration)] = aliasingStatement;
		};
	} else {
		addVisibleAlias = noopAddVisibleAlias;
	}

	for (Node* declaration : symbol->declarations) {
		if (isIdentifier(declaration)) {
			continue;
		}
		if (!isDeclarationVisible(declaration)) {
			// Mark the unexported alias as visible if its parent is visible
			// because these kind of aliases can be used to name types in declaration file
			Node* anyImportSyntax = getAnyImportSyntax(declaration);
			if (anyImportSyntax != nullptr &&
				!hasSyntacticModifier(anyImportSyntax, ModifierFlagsExport) && // import clause without export
				isDeclarationVisible(anyImportSyntax->parent)) {
				addVisibleAlias(declaration, anyImportSyntax);
				continue;
			}
			if (isVariableDeclaration(declaration) && isVariableStatement(declaration->parent->parent) &&
				!hasSyntacticModifier(declaration->parent->parent, ModifierFlagsExport) && // unexported variable statement
				isDeclarationVisible(declaration->parent->parent->parent)) {
				addVisibleAlias(declaration, declaration->parent->parent);
				continue;
			}
			if (isLateVisibilityPaintedStatement(declaration) && // unexported top-level statement
				!hasSyntacticModifier(declaration, ModifierFlagsExport) &&
				isDeclarationVisible(declaration->parent)) {
				addVisibleAlias(declaration, declaration);
				continue;
			}
			if (isBindingElement(declaration)) {
				if ((symbol->flags & SymbolFlagsAlias) != 0 && isInJSFile(declaration) &&
					declaration->parent != nullptr && declaration->parent->parent != nullptr && // exported import-like top-level JS require statement
					isVariableDeclaration(declaration->parent->parent) &&
					declaration->parent->parent->parent->parent != nullptr &&
					isVariableStatement(declaration->parent->parent->parent->parent) &&
					!hasSyntacticModifier(declaration->parent->parent->parent->parent,
										  ModifierFlagsExport) &&
					declaration->parent->parent->parent->parent->parent != nullptr && // check if the thing containing the variable statement is visible (ie, the file)
					isDeclarationVisible(declaration->parent->parent->parent->parent->parent)) {
					addVisibleAlias(declaration, declaration->parent->parent->parent->parent);
					continue;
				}
				if ((symbol->flags & SymbolFlagsBlockScopedVariable) != 0) {
					Node* rootDeclaration = walkUpBindingElementsAndPatterns(declaration);
					if (isParameterDeclaration(rootDeclaration)) {
						return nullptr;
					}
					Node* variableStatement = rootDeclaration->parent->parent;
					if (!isVariableStatement(variableStatement)) {
						return nullptr;
					}
					if (hasSyntacticModifier(variableStatement, ModifierFlagsExport)) {
						continue; // no alias to add, already exported
					}
					if (!isDeclarationVisible(variableStatement->parent)) {
						return nullptr; // not visible
					}
					addVisibleAlias(declaration, variableStatement);
					continue;
				}
			}

			// Declaration is not visible
			return nullptr;
		}
	}

	std::vector<Node*> aliases;
	if (aliasesToMakeVisible != nullptr) {
		for (auto& kv : *aliasesToMakeVisible) {
			aliases.push_back(kv.second);
		}
	}
	return new printer::SymbolAccessibilityResult{
		.Accessibility = printer::SymbolAccessibility::Accessible,
		.AliasesToMakeVisible = std::move(aliases),
	};
}

// EmitResolver::IsImplementationOfOverload — emitresolver.go:454
bool EmitResolver::IsImplementationOfOverload(Node* node) {
	// node = r.emitContext.ParseNode(node)
	if (!isParseTreeNode(node)) {
		return false;
	}
	if (nodeIsPresent(node->body())) {
		if (isGetAccessorDeclaration(node) || isSetAccessorDeclaration(node)) {
			return false; // Get or set accessors can never be overload implementations, but can have up to 2 signatures
		}
		Symbol* symbol = checker->getSymbolOfDeclaration(node);
		std::vector<Signature*> signaturesOfSymbol = checker->getSignaturesOfSymbol(symbol);
		// If this function body corresponds to function with multiple signature, it is implementation of overload
		// e.g.: function foo(a: string): string;
		//       function foo(a: number): number;
		//       function foo(a: any) { // This is implementation of the overloads
		//           return a;
		//       }
		if (signaturesOfSymbol.size() > 1) {
			return true;
		}
		// If there is single signature for the symbol, it is overload if that signature isn't coming from the node
		// e.g.: function foo(a: string): string;
		//       function foo(a: any) { // This is implementation of the overloads
		//           return a;
		//       }
		if (signaturesOfSymbol.size() == 1) {
			Signature* signature = signaturesOfSymbol[0];
			if (signature == checker->getSignatureOfFullSignatureType(node)) {
				return false;
			}
			Node* declaration = signature->declaration;
			if (declaration != node && (declaration->flags & NodeFlagsJSDoc) == 0) {
				return true;
			}
		}
	}
	return false;
}

// EmitResolver::IsImportRequiredByAugmentation — emitresolver.go:489
bool EmitResolver::IsImportRequiredByAugmentation(Node* decl) {
	// node = r.emitContext.ParseNode(node)
	if (!isParseTreeNode(decl)) {
		return false;
	}
	SourceFile* file = getSourceFileOfNode(decl);
	if (file->Symbol == nullptr) {
		// script file
		return false;
	}
	SourceFile* importTarget = GetExternalModuleFileFromDeclaration(decl);
	if (importTarget == nullptr) {
		return false;
	}
	if (importTarget == file) {
		return false;
	}
	SymbolTable exports = checker->getExportsOfModule(file->Symbol);
	for (auto& kv : exports) {
		Symbol* s = kv.second;
		Symbol* merged = checker->getMergedSymbol(s);
		if (merged != s) {
			if (!merged->declarations.empty()) {
				for (Node* d : merged->declarations) {
					SourceFile* declFile = getSourceFileOfNode(d);
					if (declFile == importTarget) {
						return true;
					}
				}
			}
		}
	}
	return false;
}

// EmitResolver::IsDefinitelyReferenceToGlobalSymbolObject — emitresolver.go:524
bool EmitResolver::IsDefinitelyReferenceToGlobalSymbolObject(Node* node) {
	if (!isPropertyAccessExpression(node) ||
		!isIdentifier(node->name()) ||
		(!isPropertyAccessExpression(node->expression()) && !isIdentifier(node->expression()))) {
		return false;
	}
	if (node->expression()->kind == Kind::Identifier) {
		if (node->expression()->text() != "Symbol") {
			return false;
		}
		// Exactly `Symbol.something` and `Symbol` either does not resolve or definitely resolves to the global Symbol
		return checker->getResolvedSymbol(node->expression()) ==
			checker->getGlobalSymbol("Symbol", SymbolFlagsValue | SymbolFlagsExportValue,
								   nullptr /*diagnostic*/);
	}
	if (node->expression()->expression()->kind != Kind::Identifier ||
		node->expression()->expression()->text() != "globalThis" ||
		node->expression()->name()->text() != "Symbol") {
		return false;
	}
	// Exactly `globalThis.Symbol.something` and `globalThis` resolves to the global `globalThis`
	return checker->getResolvedSymbol(node->expression()->expression()) ==
		checker->globalThisSymbol;
}

// EmitResolver::RequiresAddingImplicitUndefined — emitresolver.go:546
bool EmitResolver::RequiresAddingImplicitUndefined(Node* declaration, Symbol* symbol,
												   Node* enclosingDeclaration) {
	if (!isParseTreeNode(declaration)) {
		return false;
	}
	return requiresAddingImplicitUndefined(declaration, symbol, enclosingDeclaration);
}

// EmitResolver::RequiresAddingImplicitUndefinedUnsafe — emitresolver.go:554
bool EmitResolver::RequiresAddingImplicitUndefinedUnsafe(Node* declaration, Symbol* symbol,
														 Node* enclosingDeclaration) {
	if (!isParseTreeNode(declaration)) {
		return false;
	}
	// NO LOCKING - only should be called in contexts that already have a checker lock
	return requiresAddingImplicitUndefined(declaration, symbol, enclosingDeclaration);
}

// EmitResolver::requiresAddingImplicitUndefined — emitresolver.go:562
bool EmitResolver::requiresAddingImplicitUndefined(Node* declaration, Symbol* symbol,
												   Node* enclosingDeclaration) {
	// node = r.emitContext.ParseNode(node)
	if (!isParseTreeNode(declaration)) {
		return false;
	}
	switch (declaration->kind) {
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::JSDocPropertyTag:
		if (symbol == nullptr) {
			symbol = checker->getSymbolOfDeclaration(declaration);
		}
		{
			Type* t = checker->getTypeOfSymbol(symbol);
			checker->mappedSymbolLinks.Has(symbol);
			return (symbol->flags & SymbolFlagsProperty) != 0 &&
				(symbol->flags & SymbolFlagsOptional) != 0 && isOptionalDeclaration(declaration) &&
				checker->reverseMappedSymbolLinks.Has(symbol) &&
				checker->reverseMappedSymbolLinks.Get(symbol)->mappedType != nullptr &&
				checker->containsNonMissingUndefinedType(t);
		}
	case Kind::Parameter:
	case Kind::JSDocParameterTag:
		return requiresAddingImplicitUndefinedWorker(declaration, enclosingDeclaration);
	default:
		TSC_UNREACHABLE("Node cannot possibly require adding undefined");
	}
}

// EmitResolver::requiresAddingImplicitUndefinedWorker — emitresolver.go:580
bool EmitResolver::requiresAddingImplicitUndefinedWorker(Node* parameter,
														 Node* enclosingDeclaration) {
	return (isRequiredInitializedParameter(parameter, enclosingDeclaration) ||
			isOptionalUninitializedParameterProperty(parameter)) &&
		!declaredParameterTypeContainsUndefined(parameter);
}

// EmitResolver::declaredParameterTypeContainsUndefined — emitresolver.go:585
bool EmitResolver::declaredParameterTypeContainsUndefined(Node* parameter) {
	// typeNode := getNonlocalEffectiveTypeAnnotationNode(parameter); // !!! JSDoc Support
	Node* typeNode = parameter->type();
	if (typeNode == nullptr) {
		return false;
	}
	Type* t = checker->getTypeFromTypeNode(typeNode);
	// allow error type here to avoid confusing errors that the annotation has to contain undefined when it does in cases like this:
	//
	// export function fn(x?: Unresolved | undefined): void {}
	return checker->isErrorType(t) || checker->containsUndefinedType(t);
}

// EmitResolver::isOptionalUninitializedParameterProperty — emitresolver.go:599
bool EmitResolver::isOptionalUninitializedParameterProperty(Node* parameter) {
	return checker->strictNullChecks &&
		isOptionalParameter(parameter) &&
		( /*isJSDocParameterTag(parameter) ||*/ parameter->initializer() == nullptr) && // !!! TODO: JSDoc support
		hasSyntacticModifier(parameter, ModifierFlagsParameterPropertyModifier);
}

// EmitResolver::isRequiredInitializedParameter — emitresolver.go:606
bool EmitResolver::isRequiredInitializedParameter(Node* parameter,
												  Node* enclosingDeclaration) {
	if (!checker->strictNullChecks || isOptionalParameter(parameter) || /*isJSDocParameterTag(parameter) ||*/
		parameter->initializer() == nullptr) { // !!! TODO: JSDoc Support
		return false;
	}
	if (hasSyntacticModifier(parameter, ModifierFlagsParameterPropertyModifier)) {
		return enclosingDeclaration != nullptr && isFunctionLikeDeclaration(enclosingDeclaration);
	}
	return true;
}

// EmitResolver::isOptionalParameter — emitresolver.go:614
bool EmitResolver::isOptionalParameter(Node* node) {
	return checker->isOptionalParameter(node);
}

// EmitResolver::IsLiteralConstDeclaration — emitresolver.go:618
bool EmitResolver::IsLiteralConstDeclaration(Node* node) {
	// node = r.emitContext.ParseNode(node)
	if (!isParseTreeNode(node)) {
		return false;
	}
	if (isDeclarationReadonly(node) || (isVariableDeclaration(node) && isVarConst(node))) {
		Symbol* s = checker->getSymbolOfDeclaration(node);
		if (s == nullptr) {
			return false;
		}
		return isFreshLiteralType(checker->getTypeOfSymbol(s));
	}
	return false;
}

// EmitResolver::IsExpandoFunctionDeclarationUnsafe — emitresolver.go:635
bool EmitResolver::IsExpandoFunctionDeclarationUnsafe(Node* node) {
	// node = r.emitContext.ParseNode(node)
	if (!isParseTreeNode(node)) {
		return false;
	}
	// this is substantially different from strada, but so is expando property checking
	std::vector<Symbol*> props = GetPropertiesOfContainerFunction(node);
	for (Symbol* p : props) {
		if (isExpandoPropertyDeclaration(p->valueDeclaration)) {
			return true;
		}
	}
	return false;
}

// EmitResolver::IsExpandoFunctionDeclaration — emitresolver.go:651
bool EmitResolver::IsExpandoFunctionDeclaration(Node* node) {
	return IsExpandoFunctionDeclarationUnsafe(node);
}

// EmitResolver::isSymbolAccessible — emitresolver.go:657
printer::SymbolAccessibilityResult EmitResolver::isSymbolAccessible(
	Symbol* symbol, Node* enclosingDeclaration, SymbolFlags meaning,
	bool shouldComputeAliasToMarkVisible) {
	return checker->IsSymbolAccessible(symbol, enclosingDeclaration, meaning,
									   shouldComputeAliasToMarkVisible);
}

// EmitResolver::IsSymbolAccessible — emitresolver.go:681
printer::SymbolAccessibilityResult EmitResolver::IsSymbolAccessible(
	Symbol* symbol, Node* enclosingDeclaration, SymbolFlags meaning,
	bool shouldComputeAliasToMarkVisible) {
	// TODO: Split into locking and non-locking API methods - only current usage is the symbol tracker, which is non-locking,
	// as all tracker calls happen within a CreateX call below, which already holds a lock
	// r.checkerMu.Lock()
	// defer r.checkerMu.Unlock()
	return isSymbolAccessible(symbol, enclosingDeclaration, meaning,
							  shouldComputeAliasToMarkVisible);
}

// EmitResolver::IsReferencedAliasDeclaration — emitresolver.go:700
bool EmitResolver::IsReferencedAliasDeclaration(Node* node) {
	Checker* c = checker;
	if (!c->canCollectSymbolAliasAccessibilityData || !isParseTreeNode(node)) {
		return true;
	}

	if (isAliasSymbolDeclaration(node)) {
		if (Symbol* symbol = c->getSymbolOfDeclaration(node); symbol != nullptr) {
			AliasSymbolLinks* aliasLinks = c->aliasSymbolLinks.Get(symbol);
			if (aliasLinks->referenced) {
				return true;
			}
			Symbol* target = aliasLinks->aliasTarget;
			if (target != nullptr && (node->modifierFlags() & ModifierFlagsExport) != 0 &&
				(c->getSymbolFlags(target) & SymbolFlagsValue) != 0 &&
				(c->compilerOptions->ShouldPreserveConstEnums() ||
				 !isConstEnumOrConstEnumOnlyModule(target))) {
				return true;
			}
		}
	}
	return false;
}

// EmitResolver::IsValueAliasDeclaration — emitresolver.go:725
bool EmitResolver::IsValueAliasDeclaration(Node* node) {
	Checker* c = checker;
	if (!c->canCollectSymbolAliasAccessibilityData || !isParseTreeNode(node)) {
		return true;
	}

	return isValueAliasDeclarationWorker(node);
}

// EmitResolver::isValueAliasDeclarationWorker — emitresolver.go:736
bool EmitResolver::isValueAliasDeclarationWorker(Node* node) {
	Checker* c = checker;

	switch (node->kind) {
	case Kind::ImportEqualsDeclaration:
		return isAliasResolvedToValue(c->getSymbolOfDeclaration(node),
									  false /*excludeTypeOnlyValues*/);
	case Kind::ImportClause:
	case Kind::NamespaceImport:
	case Kind::ImportSpecifier:
	case Kind::ExportSpecifier: {
		Symbol* symbol = c->getSymbolOfDeclaration(node);
		return symbol != nullptr && isAliasResolvedToValue(symbol, true /*excludeTypeOnlyValues*/);
	}
	case Kind::ExportDeclaration: {
		Node* exportClause = node->as<ExportDeclaration>()->ExportClause;
		return exportClause != nullptr &&
			(isNamespaceExport(exportClause) ||
			 std::any_of(exportClause->elements().begin(), exportClause->elements().end(),
						 isValueAliasDeclaration));
	}
	case Kind::ExportAssignment:
		if (node->expression() != nullptr && node->expression()->kind == Kind::Identifier) {
			return isAliasResolvedToValue(c->getSymbolOfDeclaration(node),
										  true /*excludeTypeOnlyValues*/);
		}
		return true;
	case Kind::BinaryExpression:
		if (isCommonJSModuleExports(node) && isIdentifier(node->as<BinaryExpression>()->Right)) {
			return isAliasResolvedToValue(c->getSymbolOfDeclaration(node),
										  true /*excludeTypeOnlyValues*/);
		}
		return false;
	default:
		return false;
	}
}

// EmitResolver::isAliasResolvedToValue — emitresolver.go:766
bool EmitResolver::isAliasResolvedToValue(Symbol* symbol, bool excludeTypeOnlyValues) {
	Checker* c = checker;
	if (symbol == nullptr) {
		return false;
	}
	if (symbol->valueDeclaration != nullptr) {
		if (SourceFile* container = getSourceFileOfNode(symbol->valueDeclaration);
			container != nullptr) {
			Symbol* fileSymbol = c->getSymbolOfDeclaration(container->asNode());
			// Ensures cjs export assignment is setup, since this symbol may point at, and merge with, the file itself.
			// If we don't, the merge may not have yet occurred, and the flags check below will be missing flags that
			// are added as a result of the merge.
			c->resolveExternalModuleSymbol(fileSymbol, false /*dontResolveAlias*/);
		}
	}
	Symbol* target = c->getExportSymbolOfValueSymbolIfExported(c->resolveAlias(symbol));
	if (target == c->unknownSymbol) {
		return !excludeTypeOnlyValues || c->getTypeOnlyAliasDeclaration(symbol) == nullptr;
	}
	// const enums and modules that contain only const enums are not considered values from the emit perspective
	// unless 'preserveConstEnums' option is set to true
	return (c->getSymbolFlagsEx(symbol, excludeTypeOnlyValues, true /*excludeLocalMeanings*/) &
			SymbolFlagsValue) != 0 &&
		(c->compilerOptions->ShouldPreserveConstEnums() ||
		 !isConstEnumOrConstEnumOnlyModule(target));
}

// EmitResolver::IsTopLevelValueImportEqualsWithEntityName — emitresolver.go:791
bool EmitResolver::IsTopLevelValueImportEqualsWithEntityName(Node* node) {
	Checker* c = checker;
	if (!c->canCollectSymbolAliasAccessibilityData) {
		return true;
	}
	if (!isParseTreeNode(node) || node->kind != Kind::ImportEqualsDeclaration ||
		node->parent->kind != Kind::SourceFile) {
		return false;
	}
	if (isImportEqualsDeclaration(node) &&
		(nodeIsMissing(node->as<ImportEqualsDeclaration>()->ModuleReference) ||
		 node->as<ImportEqualsDeclaration>()->ModuleReference->kind ==
			 Kind::ExternalModuleReference)) {
		return false;
	}

	return isAliasResolvedToValue(c->getSymbolOfDeclaration(node),
								  false /*excludeTypeOnlyValues*/);
}

// EmitResolver::MarkLinkedReferencesRecursively — emitresolver.go:808
void EmitResolver::MarkLinkedReferencesRecursively(SourceFile* file) {
	if (!isParseTreeNode(file)) {
		return;
	}

	if (file != nullptr) {
		std::function<bool(Node*)> visit = [&](Node* n) -> bool {
			if (isImportEqualsDeclaration(n) && (n->modifierFlags() & ModifierFlagsExport) == 0) {
				return false; // These are deferred and marked in a chain when referenced
			}
			if (isImportDeclaration(n)) {
				return false; // likewise, these are ultimately what get marked by calls on other nodes - we want to skip them
			}
			checker->markLinkedReferences(n, ReferenceHint::Unspecified,
										  nullptr /*propSymbol*/, nullptr /*parentType*/);
			n->forEachChild(visit);
			return false;
		};
		file->forEachChild(visit);
	}
}

// EmitResolver::GetExternalModuleFileFromDeclaration — emitresolver.go:831
SourceFile* EmitResolver::GetExternalModuleFileFromDeclaration(Node* declaration) {
	if (!isParseTreeNode(declaration)) {
		return nullptr;
	}

	return checker->getExternalModuleFileFromDeclaration(declaration);
}

// EmitResolver::getReferenceResolver — emitresolver.go:842
binder::ReferenceResolver* EmitResolver::getReferenceResolver() {
	if (referenceResolver == nullptr) {
		Checker* c = checker;
		binder::ReferenceResolverHooks hooks;
		hooks.ResolveName = c->resolveName;
		hooks.GetResolvedSymbol = [c](Node* n) { return c->getResolvedSymbolOrNil(n); };
		hooks.GetMergedSymbol = [c](Symbol* s) { return c->getMergedSymbol(s); };
		hooks.GetParentOfSymbol = [c](Symbol* s) { return c->getParentOfSymbol(s); };
		hooks.GetSymbolOfDeclaration = [c](Node* n) { return c->getSymbolOfDeclaration(n); };
		hooks.GetTypeOnlyAliasDeclaration = [c](Symbol* s, SymbolFlags meaning) {
			return c->getTypeOnlyAliasDeclarationEx(s, meaning);
		};
		hooks.GetExportSymbolOfValueSymbolIfExported = [c](Symbol* s) {
			return c->getExportSymbolOfValueSymbolIfExported(s);
		};
		hooks.GetElementAccessExpressionName = [c](ElementAccessExpression* n) {
			return c->tryGetElementAccessExpressionName(n);
		};
		referenceResolver = binder::NewReferenceResolver(c->compilerOptions, hooks);
	}
	return referenceResolver;
}

// EmitResolver::GetReferencedExportContainer — emitresolver.go:860
Node* EmitResolver::GetReferencedExportContainer(Node* node, bool prefixLocals) {
	if (!isParseTreeNode(node)) {
		return nullptr;
	}

	return getReferenceResolver()->GetReferencedExportContainer(node, prefixLocals);
}

// EmitResolver::SetReferencedImportDeclaration — emitresolver.go:870
void EmitResolver::SetReferencedImportDeclaration(Node* node, Node* ref) {
	jsxLinks.Get(node)->importRef = ref;
}

// EmitResolver::GetReferencedImportDeclaration — emitresolver.go:875
Node* EmitResolver::GetReferencedImportDeclaration(Node* node) {
	if (!isParseTreeNode(node)) {
		return jsxLinks.Get(node)->importRef;
	}

	Symbol* symbol = checker->getReferencedValueOrAliasSymbol(node);
	if (isNonLocalAlias(symbol, SymbolFlagsValue) &&
		checker->getTypeOnlyAliasDeclarationEx(symbol, SymbolFlagsValue) == nullptr) {
		return checker->getDeclarationOfAliasSymbol(symbol);
	}
	return nullptr;
}

// EmitResolver::GetReferencedValueDeclaration — emitresolver.go:890
Node* EmitResolver::GetReferencedValueDeclaration(Node* node) {
	if (!isParseTreeNode(node)) {
		return nullptr;
	}

	return getReferenceResolver()->GetReferencedValueDeclaration(node);
}

// EmitResolver::GetReferencedValueDeclarationUnsafe — emitresolver.go:900
Node* EmitResolver::GetReferencedValueDeclarationUnsafe(Node* node) {
	return getReferenceResolver()->GetReferencedValueDeclaration(node);
}

// EmitResolver::GetReferencedValueDeclarations — emitresolver.go:904
std::vector<Node*> EmitResolver::GetReferencedValueDeclarations(Node* node) {
	if (!isParseTreeNode(node)) {
		return {};
	}

	return getReferenceResolver()->GetReferencedValueDeclarations(node);
}

// EmitResolver::IsNameResolvable — emitresolver.go:915
// IsNameResolvable returns `true` if the given `name` resolves to any symbol at `location`
bool EmitResolver::IsNameResolvable(Node* location, const std::string& name) {
	Symbol* symbol = checker->resolveName(
		location, name, SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace,
		nullptr /*nameNotFoundMessage*/, false /*isUse*/, false /*excludeGlobals*/);
	return symbol != nullptr;
}

// EmitResolver::GetElementAccessExpressionName — emitresolver.go:923
std::string EmitResolver::GetElementAccessExpressionName(ElementAccessExpression* expression) {
	if (!isParseTreeNode(expression)) {
		return "";
	}

	return getReferenceResolver()->GetElementAccessExpressionName(expression);
}

// EmitResolver::GetReferencedMemberValueDeclaration — emitresolver.go:933
Node* EmitResolver::GetReferencedMemberValueDeclaration(Node* node) {
	if (!isParseTreeNode(node)) {
		return nullptr;
	}

	return getReferenceResolver()->GetReferencedMemberValueDeclaration(node);
}

// TODO: the emit resolver being responsible for some amount of node construction is a very leaky abstraction,
// and requires giving it access to a lot of context it's otherwise not required to have, which also further complicates the API
// and likely reduces performance. There's probably some refactoring that could be done here to simplify this.

// EmitResolver::CreateReturnTypeOfSignatureDeclaration — emitresolver.go:946
Node* EmitResolver::CreateReturnTypeOfSignatureDeclaration(
	printer::EmitContext* emitContext, Node* signatureDeclaration,
	Node* enclosingDeclaration, nodebuilder::Flags flags,
	nodebuilder::InternalFlags internalFlags, nodebuilder::SymbolTracker* tracker) {
	Node* original = emitContext->parseNode(signatureDeclaration);
	if (original == nullptr) {
		return emitContext->factory.newKeywordTypeNode(Kind::AnyKeyword);
	}

	NodeBuilder* requestNodeBuilder = NewNodeBuilder(checker, emitContext); // TODO: cache per-context
	return requestNodeBuilder->SerializeReturnTypeForSignature(original, enclosingDeclaration,
															   flags, internalFlags, tracker);
}

// EmitResolver::CreateTypeParametersOfSignatureDeclaration — emitresolver.go:958
std::vector<Node*> EmitResolver::CreateTypeParametersOfSignatureDeclaration(
	printer::EmitContext* emitContext, Node* signatureDeclaration,
	Node* enclosingDeclaration, nodebuilder::Flags flags,
	nodebuilder::InternalFlags internalFlags, nodebuilder::SymbolTracker* tracker) {
	Node* original = emitContext->parseNode(signatureDeclaration);
	if (original == nullptr) {
		return {};
	}

	NodeBuilder* requestNodeBuilder = NewNodeBuilder(checker, emitContext); // TODO: cache per-context
	return requestNodeBuilder->SerializeTypeParametersForSignature(original, enclosingDeclaration,
																   flags, internalFlags, tracker);
}

// EmitResolver::CreateTypeOfDeclaration — emitresolver.go:970
Node* EmitResolver::CreateTypeOfDeclaration(
	printer::EmitContext* emitContext, Node* declaration, Node* enclosingDeclaration,
	nodebuilder::Flags flags, nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	Node* original = emitContext->parseNode(declaration);
	if (original == nullptr) {
		return emitContext->factory.newKeywordTypeNode(Kind::AnyKeyword);
	}

	NodeBuilder* requestNodeBuilder = NewNodeBuilder(checker, emitContext); // TODO: cache per-context
	// // Get type of the symbol if this is the valid symbol otherwise get type at location
	Symbol* symbol = checker->getSymbolOfDeclaration(declaration);
	return requestNodeBuilder->SerializeTypeForDeclaration(
		declaration, symbol, enclosingDeclaration,
		flags | nodebuilder::FlagsMultilineObjectLiterals, internalFlags, tracker);
}

// EmitResolver::CreateLiteralConstValue — emitresolver.go:983
Node* EmitResolver::CreateLiteralConstValue(printer::EmitContext* emitContext, Node* node,
											nodebuilder::SymbolTracker* tracker) {
	node = emitContext->parseNode(node);
	Type* t = checker->getTypeOfSymbol(checker->getSymbolOfDeclaration(node));
	if (t == nullptr) {
		return nullptr; // TODO: How!? Maybe this should be a panic. All symbols should have a type.
	}

	Node* enumResult = nullptr;
	if ((t->flags & TypeFlagsEnumLike) != 0) {
		NodeBuilder* requestNodeBuilder = NewNodeBuilder(checker, emitContext); // TODO: cache per-context
		enumResult = requestNodeBuilder->SymbolToExpression(
			t->symbol, SymbolFlagsValue, node, nodebuilder::FlagsNone,
			nodebuilder::InternalFlagsNone, tracker);
		// What about regularTrueType/regularFalseType - since those aren't fresh, we never make initializers from them
		// TODO: handle those if this function is ever used for more than initializers in declaration emit
	} else if (t == checker->trueType) {
		enumResult = emitContext->factory.newKeywordExpression(Kind::TrueKeyword);
	} else if (t == checker->falseType) {
		enumResult = emitContext->factory.newKeywordExpression(Kind::FalseKeyword);
	}
	if (enumResult != nullptr) {
		return enumResult;
	}
	if ((t->flags & TypeFlagsLiteral) == 0) {
		return nullptr; // non-literal type
	}
	const LiteralValue& value = t->AsLiteralType()->value;
	if (std::holds_alternative<std::string>(value)) {
		return emitContext->factory.newStringLiteral(std::get<std::string>(value),
												   TokenFlagsNone);
	}
	if (std::holds_alternative<Number>(value)) {
		const Number& num = std::get<Number>(value);
		if (num.isInf()) {
			if (num.v > 0) {
				return emitContext->factory.newIdentifier("Infinity");
			}
			return emitContext->factory.newPrefixUnaryExpression(
				Kind::MinusToken, emitContext->factory.newIdentifier("Infinity"));
		}
		if (num.isNaN()) {
			return emitContext->factory.newIdentifier("NaN");
		}
		if (num.abs() != num) {
			// negative
			return emitContext->factory.newPrefixUnaryExpression(
				Kind::MinusToken,
				emitContext->factory.newNumericLiteral(num.string().substr(1),
													   TokenFlagsNone));
		}
		return emitContext->factory.newNumericLiteral(num.string(), TokenFlagsNone);
	}
	if (std::holds_alternative<PseudoBigInt>(value)) {
		return emitContext->factory.newBigIntLiteral(
			pseudoBigIntToString(std::get<PseudoBigInt>(value)) + "n", TokenFlagsNone);
	}
	if (std::holds_alternative<bool>(value)) {
		Kind kind = Kind::FalseKeyword;
		if (std::get<bool>(value)) {
			kind = Kind::TrueKeyword;
		}
		return emitContext->factory.newKeywordExpression(kind);
	}
	TSC_UNREACHABLE("unhandled literal const value kind");
}

// EmitResolver::CreateTypeOfExpression — emitresolver.go:1034
Node* EmitResolver::CreateTypeOfExpression(
	printer::EmitContext* emitContext, Node* expression, Node* enclosingDeclaration,
	nodebuilder::Flags flags, nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	expression = emitContext->parseNode(expression);
	if (expression == nullptr) {
		return emitContext->factory.newKeywordTypeNode(Kind::AnyKeyword);
	}

	NodeBuilder* requestNodeBuilder = NewNodeBuilder(checker, emitContext); // TODO: cache per-context
	return requestNodeBuilder->SerializeTypeForExpression(
		expression, enclosingDeclaration,
		flags | nodebuilder::FlagsMultilineObjectLiterals, internalFlags, tracker);
}

// EmitResolver::CreateLateBoundIndexSignatures — emitresolver.go:1046
std::vector<Node*> EmitResolver::CreateLateBoundIndexSignatures(
	printer::EmitContext* emitContext, Node* container, Node* enclosingDeclaration,
	nodebuilder::Flags flags, nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	container = emitContext->parseNode(container);

	Symbol* sym = container->symbol();
	std::vector<IndexInfo*> staticInfos =
		checker->getIndexInfosOfType(checker->getTypeOfSymbol(sym));
	Symbol* instanceIndexSymbol = checker->getIndexSymbol(sym);
	std::vector<IndexInfo*> instanceInfos;
	if (instanceIndexSymbol != nullptr) {
		SymbolTable members = checker->getMembersOfSymbol(sym);
		std::vector<Symbol*> siblingSymbols;
		siblingSymbols.reserve(members.size());
		for (auto& kv : members) {
			siblingSymbols.push_back(kv.second);
		}
		instanceInfos = checker->getIndexInfosOfIndexSymbol(instanceIndexSymbol, siblingSymbols);
	}

	NodeBuilder* requestNodeBuilder = NewNodeBuilder(checker, emitContext); // TODO: cache per-context

	std::vector<Node*> result;
	for (int i = 0; i < 2; i++) {
		const std::vector<IndexInfo*>& infoList = i == 0 ? staticInfos : instanceInfos;
		bool isStatic = true;
		if (i > 0) {
			isStatic = false;
		}
		if (infoList.empty()) {
			continue;
		}
		for (IndexInfo* info : infoList) {
			if (info->declaration != nullptr) {
				continue;
			}
			if (info == checker->anyBaseTypeIndexInfo) {
				continue; // inherited, but looks like a late-bound signature because it has no declarations
			}
			if (!info->components.empty()) {
				// !!! TODO: Complete late-bound index info support - getObjectLiteralIndexInfo does not yet add late bound components to index signatures
				bool allComponentComputedNamesSerializable =
					enclosingDeclaration != nullptr &&
					std::all_of(info->components.begin(), info->components.end(),
								[this, enclosingDeclaration](Node* c) {
						return c->name() != nullptr &&
							isComputedPropertyName(c->name()) &&
							isEntityNameExpression(c->name()->expression()) &&
							isEntityNameVisible(c->name()->expression(), enclosingDeclaration, false)
									.Accessibility == printer::SymbolAccessibility::Accessible;
					});
				if (allComponentComputedNamesSerializable) {
					for (Node* c : info->components) {
						if (checker->hasLateBindableName(c)) {
							// skip late bound props that contribute to the index signature - they'll be preserved via other means
							continue;
						}

						Node* firstIdentifier = getFirstIdentifier(c->name()->expression());
						Symbol* name = checker->resolveName(
							firstIdentifier, firstIdentifier->text(),
							SymbolFlagsValue | SymbolFlagsExportValue,
							nullptr /*nameNotFoundMessage*/, true /*isUse*/,
							false /*excludeGlobals*/);
						if (name != nullptr) {
							tracker->TrackSymbol(name, enclosingDeclaration, SymbolFlagsValue);
						}

						std::vector<Node*> mods = ifElse(
							isStatic,
							std::vector<Node*>{emitContext->factory.newToken(Kind::StaticKeyword)},
							std::vector<Node*>{});
						if (info->isReadonly) {
							mods.push_back(emitContext->factory.newToken(Kind::ReadonlyKeyword));
						}

						Node* decl = emitContext->factory.newPropertyDeclaration(
							ifElse(!mods.empty(), emitContext->factory.newModifierList(mods),
								   static_cast<ModifierList*>(nullptr)),
							c->name(),
							c->questionToken(),
							requestNodeBuilder->TypeToTypeNode(
								checker->getTypeOfSymbol(c->symbol()), enclosingDeclaration,
								flags, internalFlags, tracker),
							nullptr);
						result.push_back(decl);
					}
					continue;
				}
			}
			Node* node = requestNodeBuilder->IndexInfoToIndexSignatureDeclaration(
				info, enclosingDeclaration, flags, internalFlags, tracker);
			if (node != nullptr && isStatic) {
				std::vector<Node*> modNodes{emitContext->factory.newToken(Kind::StaticKeyword)};
				std::vector<Node*> existing = node->modifierNodes();
				modNodes.insert(modNodes.end(), existing.begin(), existing.end());
				ModifierList* mods = emitContext->factory.newModifierList(modNodes);
				node = emitContext->factory.updateIndexSignatureDeclaration(
					node->as<IndexSignatureDeclaration>(),
					mods,
					node->parameterList(),
					node->type());
			}
			if (node != nullptr) {
				result.push_back(node);
			}
		}
	}
	return result;
}

// EmitResolver::GetEffectiveDeclarationFlags — emitresolver.go:1157
ModifierFlags EmitResolver::GetEffectiveDeclarationFlags(Node* node, ModifierFlags flags) {
	// node = emitContext.ParseNode(node)
	return checker->GetEffectiveDeclarationFlags(node, flags);
}

// EmitResolver::GetConstantValue — emitresolver.go:1164
LiteralValue EmitResolver::GetConstantValue(Node* node) {
	// node = emitContext.ParseNode(node)
	return checker->GetConstantValue(node);
}

// EmitResolver::GetTypeReferenceSerializationKind — emitresolver.go:1171
printer::TypeReferenceSerializationKind EmitResolver::GetTypeReferenceSerializationKind(
	Node* typeName, Node* location) {
	// typeName = emitContext.ParseNode(typeName)
	// location = emitContext.ParseNode(location)

	if (typeName == nullptr || location == nullptr) {
		return printer::TypeReferenceSerializationKind::Unknown;
	}

	// Resolve the symbol as a value to ensure the type can be reached at runtime during emit.
	bool isTypeOnly = false;
	if (isQualifiedName(typeName)) {
		Symbol* rootValueSymbol = checker->resolveEntityName(
			getFirstIdentifier(typeName), SymbolFlagsValue, true, true, location);

		if (rootValueSymbol != nullptr && !rootValueSymbol->declarations.empty()) {
			isTypeOnly = std::all_of(rootValueSymbol->declarations.begin(),
									 rootValueSymbol->declarations.end(),
									 isTypeOnlyImportOrExportDeclaration);
		}
	}
	Symbol* valueSymbol =
		checker->resolveEntityName(typeName, SymbolFlagsValue, true, true, location);
	Symbol* resolvedValueSymbol = valueSymbol;
	if (valueSymbol != nullptr && (valueSymbol->flags & SymbolFlagsAlias) != 0) {
		resolvedValueSymbol = checker->resolveAlias(valueSymbol);
	}

	isTypeOnly = isTypeOnly ||
		(valueSymbol != nullptr &&
		 checker->getTypeOnlyAliasDeclarationEx(valueSymbol, SymbolFlagsValue) != nullptr);

	// Resolve the symbol as a type so that we can provide a more useful hint for the type serializer.
	Symbol* typeSymbol =
		checker->resolveEntityName(typeName, SymbolFlagsType, true, true, location);
	Symbol* resolvedTypeSymbol = typeSymbol;
	if (typeSymbol != nullptr && (typeSymbol->flags & SymbolFlagsAlias) != 0) {
		resolvedTypeSymbol = checker->resolveAlias(typeSymbol);
	}
	// In case the value symbol can't be resolved (e.g. because of missing declarations), use type symbol for reachability check.
	isTypeOnly = isTypeOnly ||
		(typeSymbol != nullptr &&
		 checker->getTypeOnlyAliasDeclarationEx(typeSymbol, SymbolFlagsType) != nullptr);

	if (resolvedValueSymbol != nullptr && resolvedValueSymbol == resolvedTypeSymbol) {
		Symbol* globalPromiseSymbol = checker->getGlobalPromiseConstructorSymbol();
		if (globalPromiseSymbol != nullptr && resolvedValueSymbol == globalPromiseSymbol) {
			return printer::TypeReferenceSerializationKind::Promise;
		}

		Type* constructorType = checker->getTypeOfSymbol(resolvedValueSymbol);
		if (constructorType != nullptr && checker->isConstructorType(constructorType)) {
			if (isTypeOnly) {
				return printer::TypeReferenceSerializationKind::TypeWithCallSignature;
			}
			return printer::TypeReferenceSerializationKind::TypeWithConstructSignatureAndValue;
		}
	}

	// We might not be able to resolve type symbol so use unknown type in that case (eg error case)
	if (resolvedTypeSymbol == nullptr) {
		if (isTypeOnly) {
			return printer::TypeReferenceSerializationKind::ObjectType;
		}
		return printer::TypeReferenceSerializationKind::Unknown;
	}

	Type* type_ = checker->getDeclaredTypeOfSymbol(resolvedTypeSymbol);
	if (checker->isErrorType(type_)) {
		if (isTypeOnly) {
			return printer::TypeReferenceSerializationKind::ObjectType;
		}
		return printer::TypeReferenceSerializationKind::Unknown;
	}

	if ((type_->flags & TypeFlagsAnyOrUnknown) != 0) {
		return printer::TypeReferenceSerializationKind::ObjectType;
	} else if (checker->isTypeAssignableToKind(
				   type_, TypeFlagsVoid | TypeFlagsNullable | TypeFlagsNever)) {
		return printer::TypeReferenceSerializationKind::VoidNullableOrNeverType;
	} else if (checker->isTypeAssignableToKind(type_, TypeFlagsBooleanLike)) {
		return printer::TypeReferenceSerializationKind::BooleanType;
	} else if (checker->isTypeAssignableToKind(type_, TypeFlagsNumberLike)) {
		return printer::TypeReferenceSerializationKind::NumberLikeType;
	} else if (checker->isTypeAssignableToKind(type_, TypeFlagsBigIntLike)) {
		return printer::TypeReferenceSerializationKind::BigIntLikeType;
	} else if (checker->isTypeAssignableToKind(type_, TypeFlagsStringLike)) {
		return printer::TypeReferenceSerializationKind::StringLikeType;
	} else if (isTupleType(type_)) {
		return printer::TypeReferenceSerializationKind::ArrayLikeType;
	} else if (checker->isTypeAssignableToKind(type_, TypeFlagsESSymbolLike)) {
		return printer::TypeReferenceSerializationKind::ESSymbolType;
	} else if (checker->isFunctionType(type_)) {
		return printer::TypeReferenceSerializationKind::TypeWithCallSignature;
	} else if (checker->isArrayType(type_)) {
		return printer::TypeReferenceSerializationKind::ArrayLikeType;
	} else {
		return printer::TypeReferenceSerializationKind::ObjectType;
	}
}

// EmitResolver::GetPropertiesOfContainerFunction — emitresolver.go:1259
std::vector<Symbol*> EmitResolver::GetPropertiesOfContainerFunction(Node* node) {
	// This is explicitly _not locked_ because it is only called via error reporters invoked via node builder calls
	// to the symbol tracker already within locked contexts.
	// r.checkerMu.Lock()
	// defer r.checkerMu.Unlock()
	if (node == nullptr) {
		return {};
	}
	Symbol* s = checker->getSymbolOfDeclaration(node);
	if (s == nullptr) {
		return {};
	}
	return checker->getPropertiesOfType(checker->getTypeOfSymbol(s));
}

// EmitResolver::TryJSTypeNodeToTypeNode — emitresolver.go:1273
Node* EmitResolver::TryJSTypeNodeToTypeNode(
	printer::EmitContext* emitContext, Node* typeNode, Node* enclosingDeclaration,
	nodebuilder::Flags flags, nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	typeNode = emitContext->parseNode(typeNode);

	NodeBuilder* requestNodeBuilder = NewNodeBuilder(checker, emitContext); // TODO: cache per-context
	return requestNodeBuilder->TryJSTypeNodeToTypeNode(typeNode, enclosingDeclaration, flags,
													   internalFlags, tracker);
}

// EmitResolver::IsThisPropertyAssignmentDeclarationRedundant — emitresolver.go:1292
// IsThisPropertyAssignmentDeclarationRedundant reports whether a JS `this.<name> = ...` expando
// assignment should be omitted from declaration emit because the member it would synthesize is
// already provided by an `extends` base type. This mirrors the skip condition in the checker's
// serializePropertySymbol: an inherited member is redundant when it is identical to the assigned
// one (same readonly-ness, optionality and type). Inherited accessors and methods are always
// treated as redundant here, since accessors merge oddly with value assignments (and run via the
// accessor at runtime), and `this`-expando props carry the ReplaceableByMethod contract, so a
// rebind such as `this.method = this.method.bind(this)` must not override the base method.
//
// Only `extends` base types are considered. Members coming from `implements` clauses are not
// inherited, so the class must redeclare them, and they are always emitted.
bool EmitResolver::IsThisPropertyAssignmentDeclarationRedundant(Node* node) {
	if (node == nullptr) {
		return false;
	}

	Symbol* s = checker->getSymbolOfDeclaration(node);
	if (s == nullptr || s->parent == nullptr) {
		return false;
	}
	Type* parentType = checker->getDeclaredTypeOfSymbol(s->parent);
	if (parentType == nullptr) {
		return false;
	}
	for (Type* base : checker->getBaseTypes(parentType)) {
		Symbol* baseProp = checker->getPropertyOfType(base, s->name);
		if (baseProp == nullptr) {
			continue;
		}
		if ((baseProp->flags & (SymbolFlagsAccessor | SymbolFlagsMethod | SymbolFlagsFunction)) != 0) {
			return true;
		}
		if (checker->isReadonlySymbol(baseProp) == checker->isReadonlySymbol(s) &&
			(s->flags & SymbolFlagsOptional) == (baseProp->flags & SymbolFlagsOptional) &&
			checker->isTypeIdenticalTo(checker->getTypeOfSymbol(s),
									   checker->getTypeOfSymbol(baseProp))) {
			return true;
		}
	}
	return false;
}

// ===========================================================================
// dep stubs — removed when owner slice lands
// ===========================================================================

// (deduped: getJsxFragmentFactoryEntity defined in checker_jsx.cpp)



// (deduped: NewNodeBuilder + all NodeBuilder::* dep-stub bodies — real
// defs live in checker_printer.cpp)

}  // namespace tsc::checker

// binder/referenceresolver.go — dep-stub until the referenceresolver slice lands.

namespace tsc::binder {

ReferenceResolver* NewReferenceResolver(
	const CompilerOptions* /*options*/, ReferenceResolverHooks /*hooks*/) {
	TSC_UNREACHABLE("binder::NewReferenceResolver — referenceresolver slice");
}

}  // namespace tsc::binder
