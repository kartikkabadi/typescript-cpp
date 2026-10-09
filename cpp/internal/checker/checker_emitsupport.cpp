// emitsupport.go — emit support workers on Checker (moved off EmitResolver per 253bcd86)
#include "checker.h"

namespace tsc {
namespace checker {

namespace {

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

// utilities.go:298 — isOptionalDeclaration
bool isOptionalDeclaration(Node* declaration) {
	return hasQuestionToken(declaration);
}

// noopAddVisibleAlias — emitresolver.go:376
void noopAddVisibleAlias(Node* /*declaration*/, Node* /*aliasingStatement*/) {}

}  // namespace

// Checker::isDeclarationVisible — emitresolver.go:112
bool Checker::isDeclarationVisible(Node* node) {
	// node = r.emitContext.ParseNode(node)
	if (!isParseTreeNode(node)) {
		return false;
	}
	if (node == nullptr) {
		return false;
	}

	DeclarationLinks* links = emitResolverLinks.declarationLinks.Get(node);
	if (links->isVisible == Tristate::Unknown) {
		if (determineIfDeclarationIsVisible(node)) {
			links->isVisible = Tristate::True;
		} else {
			links->isVisible = Tristate::False;
		}
	}
	return links->isVisible == Tristate::True;
}
// Checker::determineIfDeclarationIsVisible — emitresolver.go:131
bool Checker::determineIfDeclarationIsVisible(Node* node) {
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
		if ((getCombinedModifierFlagsCached(node) & ModifierFlagsExport) == 0 &&
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
		if (GetEffectiveDeclarationFlags(
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
// Checker::isEntityNameVisible — emitresolver.go:336
printer::SymbolAccessibilityResult Checker::isEntityNameVisible(
	Node* entityName, Node* enclosingDeclaration, bool shouldComputeAliasToMakeVisible) {
	// node = r.emitContext.ParseNode(entityName)
	if (!isParseTreeNode(entityName)) {
		return printer::SymbolAccessibilityResult{
			.Accessibility = printer::SymbolAccessibility::NotAccessible};
	}

	SymbolFlags meaning = getMeaningOfEntityNameReference(entityName);
	Node* firstIdentifier = getFirstIdentifier(entityName);

	Symbol* symbol = resolveName(enclosingDeclaration, firstIdentifier->text(),
										meaning, nullptr, false, false);

	if (symbol != nullptr && (symbol->flags & SymbolFlagsTypeParameter) != 0 &&
		(meaning & SymbolFlagsType) != 0) {
		return printer::SymbolAccessibilityResult{
			.Accessibility = printer::SymbolAccessibility::Accessible};
	}

	if (symbol == nullptr && isThisIdentifier(firstIdentifier)) {
		Symbol* sym = getSymbolOfDeclaration(
			getThisContainer(firstIdentifier, false, false));
		if (IsSymbolAccessible(sym, enclosingDeclaration, meaning, false).Accessibility ==
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
// Checker::hasVisibleDeclarations — emitresolver.go:379
printer::SymbolAccessibilityResult* Checker::hasVisibleDeclarations(
	Symbol* symbol, bool shouldComputeAliasToMakeVisible) {
	std::unordered_map<NodeId, Node*> aliasesToMakeVisibleSet;
	std::unordered_map<NodeId, Node*>* aliasesToMakeVisible = nullptr;

	std::function<void(Node*, Node*)> addVisibleAlias;
	if (shouldComputeAliasToMakeVisible) {
		aliasesToMakeVisible = &aliasesToMakeVisibleSet;
		addVisibleAlias = [this, aliasesToMakeVisible](Node* declaration,
													 Node* aliasingStatement) {
			emitResolverLinks.declarationLinks.Get(declaration)->isVisible = Tristate::True;
			(*aliasesToMakeVisible)[getNodeId(declaration)] = aliasingStatement;
		};
	} else {
		addVisibleAlias = noopAddVisibleAlias;
	}

	for (Node* declaration : symbol->data->declarations) {
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
// Checker::requiresAddingImplicitUndefined — emitresolver.go:562
bool Checker::requiresAddingImplicitUndefined(Node* declaration, Symbol* symbol,
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
			symbol = getSymbolOfDeclaration(declaration);
		}
		{
			Type* t = getTypeOfSymbol(symbol);
			mappedSymbolLinks.Has(symbol);
			return (symbol->flags & SymbolFlagsProperty) != 0 &&
				(symbol->flags & SymbolFlagsOptional) != 0 && isOptionalDeclaration(declaration) &&
				reverseMappedSymbolLinks.Has(symbol) &&
				reverseMappedSymbolLinks.Get(symbol)->mappedType != nullptr &&
				containsNonMissingUndefinedType(t);
		}
	case Kind::Parameter:
	case Kind::JSDocParameterTag:
		return requiresAddingImplicitUndefinedWorker(declaration, enclosingDeclaration);
	default:
		TSC_UNREACHABLE("Node cannot possibly require adding undefined");
	}
}
// Checker::requiresAddingImplicitUndefinedWorker — emitresolver.go:580
bool Checker::requiresAddingImplicitUndefinedWorker(Node* parameter,
														 Node* enclosingDeclaration) {
	return (isRequiredInitializedParameter(parameter, enclosingDeclaration) ||
			isOptionalUninitializedParameterProperty(parameter)) &&
		!declaredParameterTypeContainsUndefined(parameter);
}
// Checker::declaredParameterTypeContainsUndefined — emitresolver.go:585
bool Checker::declaredParameterTypeContainsUndefined(Node* parameter) {
	// typeNode := getNonlocalEffectiveTypeAnnotationNode(parameter); // !!! JSDoc Support
	Node* typeNode = parameter->type();
	if (typeNode == nullptr) {
		return false;
	}
	Type* t = getTypeFromTypeNode(typeNode);
	// allow error type here to avoid confusing errors that the annotation has to contain undefined when it does in cases like this:
	//
	// export function fn(x?: Unresolved | undefined): void {}
	return isErrorType(t) || containsUndefinedType(t);
}
// Checker::isOptionalUninitializedParameterProperty — emitresolver.go:599
bool Checker::isOptionalUninitializedParameterProperty(Node* parameter) {
	return strictNullChecks &&
		isOptionalParameter(parameter) &&
		( /*isJSDocParameterTag(parameter) ||*/ parameter->initializer() == nullptr) && // !!! TODO: JSDoc support
		hasSyntacticModifier(parameter, ModifierFlagsParameterPropertyModifier);
}
// Checker::isRequiredInitializedParameter — emitresolver.go:606
bool Checker::isRequiredInitializedParameter(Node* parameter,
												  Node* enclosingDeclaration) {
	if (!strictNullChecks || isOptionalParameter(parameter) || /*isJSDocParameterTag(parameter) ||*/
		parameter->initializer() == nullptr) { // !!! TODO: JSDoc Support
		return false;
	}
	if (hasSyntacticModifier(parameter, ModifierFlagsParameterPropertyModifier)) {
		return enclosingDeclaration != nullptr && isFunctionLikeDeclaration(enclosingDeclaration);
	}
	return true;
}
}  // namespace checker
}  // namespace tsc
