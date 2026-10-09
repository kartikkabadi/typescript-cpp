// checker_services.cpp — services-facing tail of checker.go (32070-32660):
// GetSymbolAtLocation/getSymbolAtLocation, getIndexSignaturesAtLocation,
// getSymbolOfNameOrPropertyAccessExpression, isThisPropertyAndThisTyped,
// getTypeOfNode, getThisType* helpers, getApplicableIndexInfos/Symbol,
// getRegularTypeOfExpression, containsArgumentsReference, GetTypeAtLocation,
// GetEmitResolver. GetAliasedSymbol lives in checker_utilities.cpp.

#include "internal/checker/checker.h"
#include "internal/compiler/program.h"

#include <algorithm>
#include <functional>
#include <mutex>

namespace tsc {
namespace checker {

// (deduped: newEmitResolver defined in cpp/internal/checker/checker_emitresolver.cpp)

// checker.go:32070 — GetSymbolAtLocation
Symbol* Checker::GetSymbolAtLocation(Node* node) {
	// set ignoreErrors: true because any lookups invoked by the API shouldn't
	// cause any new errors
	return getSymbolAtLocation(getReparsedNodeForNode(node), true);
}

// checker.go:32083 — getSymbolAtLocation. API-facing "fuzzy" symbol lookup; do
// not use inside the checker itself.
Symbol* Checker::getSymbolAtLocation(Node* node, bool ignoreErrors) {
	if (isSourceFile(node)) {
		if (isExternalOrCommonJSModule(static_cast<SourceFile*>(node))) {
			return getMergedSymbol(node->symbol());
		}
		return nullptr;
	}
	Node* parent = node->parent;
	Node* grandParent = parent->parent;

	if ((node->flags & NodeFlagsInWithStatement) != 0) {
		// We cannot answer semantic questions within a with block, do not
		// proceed any further
		return nullptr;
	}

	if (isDeclarationNameOrImportPropertyName(node)) {
		// This is a declaration, call getSymbolOfNode
		Symbol* parentSymbol = getSymbolOfDeclaration(parent);
		if (isImportOrExportSpecifier(parent) && parent->propertyName() == node) {
			return getImmediateAliasedSymbol(parentSymbol);
		}
		return parentSymbol;
	} else if (isLiteralComputedPropertyDeclarationName(node)) {
		return getSymbolOfDeclaration(grandParent);
	}

	if (isIdentifier(node)) {
		if (isInRightSideOfImportOrExportAssignment(node)) {
			return getSymbolOfNameOrPropertyAccessExpression(node);
		} else if (isBindingElement(parent) &&
		           isObjectBindingPattern(grandParent) &&
		           node == parent->propertyName()) {
			Type* typeOfPattern = getTypeOfNode(grandParent);
			if (Symbol* propertyDeclaration =
			        getPropertyOfType(typeOfPattern,
			                          std::string(node->text()))) {
				return propertyDeclaration;
			}
		} else if (isMetaProperty(parent) && parent->name() == node) {
			auto* metaProp = parent->as<MetaProperty>();
			if (metaProp->KeywordToken == Kind::NewKeyword &&
			    node->text() == "target") {
				// `target` in `new.target`
				return checkNewTargetMetaProperty(parent)->symbol;
			}
			// The `meta` in `import.meta` could be given
			// `getTypeOfNode(parent)->symbol` (the `ImportMeta` interface
			// symbol), but we have a fake expression type made for other
			// reasons already, whose transient `meta` member should more
			// exactly be the kind of (declarationless) symbol we want.
			// (See #44364 and #45031 for relevant implementation PRs)
			if (metaProp->KeywordToken == Kind::ImportKeyword &&
			    node->text() == "meta") {
				return getSymbolFromTable(
					getGlobalImportMetaExpressionType()->AsObjectType()->members,
					"meta");
			}
			// no other meta properties are valid syntax, thus no others should
			// have symbols
			return nullptr;
		} else if (isJSDocParameterTag(parent) && parent->name() == node) {
			if (Node* fn = getNodeAtPosition(getSourceFileOfNode(node),
			                                 node->pos(), false);
			    fn != nullptr && isFunctionLike(fn)) {
				for (Node* param : fn->parameters()) {
					if (isIdentifier(param->name()) &&
					    param->name()->text() == node->text()) {
						return getSymbolOfNode(param);
					}
				}
			}
		}
	}

	switch (node->kind) {
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
	case Kind::PropertyAccessExpression:
	case Kind::QualifiedName:
		if (!isThisInTypeQuery(node)) {
			return getSymbolOfNameOrPropertyAccessExpression(node);
		}
		[[fallthrough]];
	case Kind::ThisKeyword: {
		Node* container = getThisContainer(
		    node, false /*includeArrowFunctions*/,
		    false /*includeClassComputedPropertyName*/);
		if (isFunctionLike(container)) {
			Signature* sig = getSignatureFromDeclaration(container);
			if (sig->thisParameter != nullptr) {
				return sig->thisParameter;
			}
		}
		if (isInExpressionContext(node)) {
			return checkExpression(node)->symbol;
		}
		[[fallthrough]];
	}
	case Kind::ThisType:
		return getTypeFromThisTypeNode(node)->symbol;
	case Kind::SuperKeyword:
		return checkExpression(node)->symbol;
	case Kind::ConstructorKeyword: {
		// constructor keyword for an overload, should take us to the definition
		// if it exists
		Node* constructorDeclaration = parent;
		if (constructorDeclaration != nullptr &&
		    constructorDeclaration->kind == Kind::Constructor) {
			return constructorDeclaration->parent->symbol();
		}
		return nullptr;
	}
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
		// 1). import x = require("./mo/*gotToDefinitionHere*/d")
		// 2). External module name in an import declaration
		// 3). Require in Javascript
		// 4). type A = import("./f/*gotToDefinitionHere*/oo")
		if ((isExternalModuleImportEqualsDeclaration(grandParent) &&
		     getExternalModuleImportEqualsDeclarationExpression(grandParent) ==
		         node) ||
		    ((parent->kind == Kind::ImportDeclaration ||
		      parent->kind == Kind::JSImportDeclaration ||
		      parent->kind == Kind::ExportDeclaration) &&
		     getExternalModuleName(parent) == node) ||
		    isVariableDeclarationInitializedToRequire(grandParent) ||
		    isImportCall(parent) ||
		    (isLiteralTypeNode(parent) &&
		     isLiteralImportTypeNode(grandParent) &&
		     grandParent->as<ImportTypeNode>()->Argument == parent)) {
			return resolveExternalModuleName(
			    node, node, ignoreErrors,
			    getImportAttributesTypeForModuleSpecifier(node));
		}
		if (isCallExpression(parent) &&
		    isBindableObjectDefinePropertyCall(parent) &&
		    parent->arguments()[1] == node) {
			return getSymbolOfDeclaration(parent);
		}
		[[fallthrough]];
	case Kind::NumericLiteral: {
		// index access
		Type* objectType = nullptr;
		if (isElementAccessExpression(parent)) {
			if (parent->as<ElementAccessExpression>()->ArgumentExpression ==
			    node) {
				objectType = getTypeOfExpression(parent->expression());
			}
		} else if (isLiteralTypeNode(parent) &&
		           isIndexedAccessTypeNode(grandParent)) {
			objectType = getTypeFromTypeNode(
			    grandParent->as<IndexedAccessTypeNode>()->ObjectType);
		}

		if (objectType != nullptr) {
			return getPropertyOfType(objectType, std::string(node->text()));
		}
		return nullptr;
	}
	case Kind::DefaultKeyword:
	case Kind::FunctionKeyword:
	case Kind::EqualsGreaterThanToken:
	case Kind::ClassKeyword:
		return getSymbolOfNode(node->parent);
	case Kind::ImportType:
		if (isLiteralImportTypeNode(node)) {
			return getSymbolAtLocation(
			    node->as<ImportTypeNode>()
			        ->Argument->as<LiteralTypeNode>()
			        ->Literal,
			    ignoreErrors);
		}
		return nullptr;
	case Kind::ExportKeyword:
		if (isExportAssignment(parent)) {
			TSC_ASSERT(parent->symbol() != nullptr,
			           "Symbol should be defined");
			return parent->symbol();
		}
		return nullptr;
	case Kind::ImportKeyword:
		if (isImportPhaseMetaProperty(node->parent)) {
			return nullptr;
		}
		[[fallthrough]];
	case Kind::NewKeyword:
		if (isMetaProperty(parent)) {
			return checkMetaPropertyKeyword(parent)->symbol;
		}
		return nullptr;
	case Kind::InstanceOfKeyword:
		if (isBinaryExpression(parent)) {
			Type* t = getTypeOfExpression(
			    parent->as<BinaryExpression>()->Right);
			Type* hasInstanceMethodType =
			    getSymbolHasInstanceMethodOfObjectType(t);
			if (hasInstanceMethodType != nullptr &&
			    hasInstanceMethodType->symbol != nullptr) {
				return hasInstanceMethodType->symbol;
			}
			return t->symbol;
		}
		return nullptr;
	case Kind::MetaProperty:
		return checkExpression(node)->symbol;
	case Kind::JsxNamespacedName:
		if (isJsxTagName(node) && isJsxIntrinsicTagName(node)) {
			Symbol* symbol = getIntrinsicTagSymbol(node->parent);
			if (symbol == unknownSymbol) {
				return nullptr;
			}
			return symbol;
		}
		[[fallthrough]];
	default:
		return nullptr;
	}
}

// checker.go:32253 — getIndexSignaturesAtLocation
std::vector<Node*> Checker::getIndexSignaturesAtLocation(Node* node) {
	std::vector<Node*> signatures;
	if (isIdentifier(node) && isPropertyAccessExpression(node->parent) &&
	    node->parent->name() == node) {
		Type* keyType = getLiteralTypeFromPropertyName(node);
		Type* objectType = getTypeOfExpression(node->parent->expression());
		for (Type* t : objectType->Distributed()) {
			for (IndexInfo* info : getApplicableIndexInfos(t, keyType)) {
				if (info->declaration != nullptr) {
					if (std::find(signatures.begin(), signatures.end(),
					              info->declaration) ==
					    signatures.end()) {
						signatures.push_back(info->declaration);
					}
				}
			}
		}
	}
	return signatures;
}

// checker.go:32269 — getSymbolOfNameOrPropertyAccessExpression
Symbol* Checker::getSymbolOfNameOrPropertyAccessExpression(Node* name) {
	if (isDeclarationName(name)) {
		return getSymbolOfNode(name->parent);
	}
	if (name->parent->kind == Kind::ExportAssignment &&
	    isEntityNameExpression(name)) {
		// Even an entity name expression that doesn't resolve as an entityname
		// may still typecheck as a property access expression
		Symbol* success = resolveEntityName(
		    name,
		    /*all meanings*/
		    SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace |
		        SymbolFlagsAlias,
		    true /*ignoreErrors*/, false /*dontResolveAlias*/,
		    nullptr /*location*/);
		if (success != nullptr && success != unknownSymbol) {
			return success;
		}
	} else if (isEntityName(name) &&
	           isInRightSideOfImportOrExportAssignment(name)) {
		// Since we already checked for ExportAssignment, this really could only
		// be an Import
		[[maybe_unused]] Node* importEqualsDeclaration =
		    findAncestorKind(name, Kind::ImportEqualsDeclaration);
		TSC_ASSERT(importEqualsDeclaration != nullptr,
		           "ImportEqualsDeclaration should be defined");
		return getSymbolOfPartOfRightHandSideOfImportEquals(name);
	}

	if (isEntityName(name)) {
		Node* possibleImportNode = isImportTypeQualifierPart(name);
		if (possibleImportNode != nullptr) {
			getTypeFromTypeNode(possibleImportNode);
			Symbol* sym = getResolvedSymbolOrNil(name);
			return sym == unknownSymbol ? nullptr : sym;
		}
	}

	while (isRightSideOfQualifiedNameOrPropertyAccess(name)) {
		name = name->parent;
	}

	if (isInNameOfExpressionWithTypeArgumentsOrHeritageTypeReference(name)) {
		SymbolFlags meaning{};
		if (name->parent->kind == Kind::ExpressionWithTypeArguments ||
		    name->parent->kind == Kind::TypeReference) {
			// A heritage element name may appear in type space, value space, or
			// both; ensure the meaning matches its context.
			meaning = isPartOfTypeNode(name) ? SymbolFlagsType
			                               : SymbolFlagsValue;

			// In a class 'extends' clause we are also looking for a value.
			if (isExpressionWithTypeArgumentsInClassExtendsClause(
			        name->parent)) {
				meaning = meaning | SymbolFlagsValue;
			}
		} else {
			meaning = SymbolFlagsNamespace;
		}

		meaning = meaning | SymbolFlagsAlias;
		Symbol* entityNameSymbol = nullptr;
		if (isEntityNameExpression(name)) {
			entityNameSymbol =
			    resolveEntityName(name, meaning, true /*ignoreErrors*/,
			                      false /*dontResolveAlias*/,
			                      nullptr /*location*/);
		}
		if (entityNameSymbol != nullptr) {
			return entityNameSymbol;
		}
	}

	if (isExpressionNode(name)) {
		if (nodeIsMissing(name)) {
			// Missing entity name.
			return nullptr;
		}
		bool isJSDocRef = isJSDocNameReferenceContext(name);
		if (isIdentifier(name)) {
			if (isJsxTagName(name) && isJsxIntrinsicTagName(name)) {
				Symbol* symbol = getIntrinsicTagSymbol(name->parent);
				return symbol == unknownSymbol ? nullptr : symbol;
			}
			SymbolFlags meaning =
			    isJSDocRef
			        ? (SymbolFlagsValue | SymbolFlagsType |
			           SymbolFlagsNamespace)
			        : SymbolFlagsValue;
			Node* location = nullptr;
			if (isJSDocRef) {
				location = getHostSignatureFromJSDoc(name);
			}
			Symbol* result =
			    resolveEntityName(name, meaning, true /*ignoreErrors*/,
			                      true /*dontResolveAlias*/, location);
			if (result == nullptr && isJSDocRef) {
				if (Node* container =
				        findAncestor(name, isClassOrInterfaceLike)) {
					Symbol* symbol = getSymbolOfDeclaration(container);
					// Handle unqualified references to class static members
					// and class or interface instance members
					const auto& exports = getExportsOfSymbol(symbol);
					result = getMergedSymbol(getSymbol(
					    exports, std::string(name->text()), meaning));
					if (result == nullptr) {
						result = getPropertyOfType(
						    getDeclaredTypeOfSymbol(symbol),
						    std::string(name->text()));
					}
				}
			}
			return result;
		} else if (isPrivateIdentifier(name)) {
			return getSymbolForPrivateIdentifierExpression(name);
		} else if (isPropertyAccessExpression(name) ||
		           isQualifiedName(name)) {
			auto* links = symbolNodeLinks.Get(name);
			if (links->resolvedSymbol != nullptr &&
				!staleForCheckFile(links->resolvedSymbolCheckFile)) {
				return links->resolvedSymbol;
			}
			links->resolvedSymbol = nullptr;
			links->resolvedSymbolCheckFile = checkFileTag();
			if (isPropertyAccessExpression(name)) {
				checkPropertyAccessExpression(name, CheckModeNormal,
				                              false /*writeOnly*/);
				if (links->resolvedSymbol == nullptr &&
				    !isPrivateIdentifier(name->name())) {
					links->resolvedSymbol = getApplicableIndexSymbol(
					    checkExpressionCached(name->expression()),
					    getLiteralTypeFromPropertyName(name->name()));
				}
			} else {
				checkQualifiedName(name, CheckModeNormal);
			}
			if (links->resolvedSymbol == nullptr && isJSDocRef &&
			    isQualifiedName(name)) {
				return resolveJSDocMemberName(name);
			}
			return links->resolvedSymbol;
		}
	} else if (isEntityName(name) && isTypeReferenceIdentifier(name)) {
		SymbolFlags meaning = name->parent->kind == Kind::TypeReference
		                          ? SymbolFlagsType
		                          : SymbolFlagsNamespace;
		Symbol* symbol =
		    resolveEntityName(name, meaning, true /*ignoreErrors*/,
		                      true /*dontResolveAlias*/, nullptr /*location*/);
		if (symbol != nullptr && symbol != unknownSymbol) {
			return symbol;
		}
		if (isNameOfHeritageClauseTypeReference(name)) {
			return nullptr;
		}
		return getUnresolvedSymbolForEntityName(name);
	}

	if (name->parent->kind == Kind::TypePredicate) {
		return resolveEntityName(
		    name,
		    SymbolFlagsFunctionScopedVariable, /*meaning*/
		    true,                              /*ignoreErrors*/
		    false,                             /*dontResolveAlias*/
		    nullptr                            /*location*/
		);
	}
	return nullptr;
}

// checker.go:32404 — isThisPropertyAndThisTyped
bool Checker::isThisPropertyAndThisTyped(Node* node) {
	if (node->expression()->kind == Kind::ThisKeyword) {
		Node* container = getThisContainer(
		    node, false /*includeArrowFunctions*/,
		    false /*includeClassComputedPropertyName*/);
		if (isFunctionLike(container)) {
			Node* containingLiteral = getContainingObjectLiteral(container);
			if (containingLiteral != nullptr) {
				Type* contextualType = getApparentTypeOfContextualType(
				    containingLiteral, ContextFlagsNone);
				Type* t = getThisTypeOfObjectLiteralFromContextualType(
				    containingLiteral, contextualType);
				return t != nullptr && !isTypeAny(t);
			}
		}
	}
	return false;
}

// checker.go:32419 — getTypeOfNode
// (deduped: getTypeOfNode defined in the owning slice file)

// checker.go:32531 — getThisTypeOfObjectLiteralFromContextualType
Type* Checker::getThisTypeOfObjectLiteralFromContextualType(
    Node* containingLiteral, Type* contextualType) {
	Node* literal = containingLiteral;
	Type* t = contextualType;
	while (t != nullptr) {
		Type* thisType = getThisTypeFromContextualType(t);
		if (thisType != nullptr) {
			return thisType;
		}
		if (literal->parent->kind != Kind::PropertyAssignment) {
			break;
		}
		literal = literal->parent->parent;
		t = getApparentTypeOfContextualType(literal, ContextFlagsNone);
	}
	return nullptr;
}

// checker.go:32548 — getThisTypeFromContextualType
Type* Checker::getThisTypeFromContextualType(Type* t) {
	return mapType(t, [this](Type* t) -> Type* {
		if ((t->flags & TypeFlagsIntersection) != 0) {
			for (Type* t2 : t->AsIntersectionType()->types) {
				Type* typeArg = getThisTypeArgument(t2);
				if (typeArg != nullptr) {
					return typeArg;
				}
			}
			return nullptr;
		}
		return getThisTypeArgument(t);
	});
}

// checker.go:32564 — getThisTypeArgument
Type* Checker::getThisTypeArgument(Type* t) {
	if ((t->objectFlags & ObjectFlagsReference) != 0 &&
	    t->AsTypeReference()->target == globalThisType) {
		return getTypeArguments(t)[0];
	}
	return nullptr;
}

// checker.go:32571 — getApplicableIndexInfos
std::vector<IndexInfo*> Checker::getApplicableIndexInfos(Type* t,
                                                         Type* keyType) {
	auto infos = getIndexInfosOfType(t);
	std::vector<IndexInfo*> result;
	for (IndexInfo* info : infos) {
		if (isApplicableIndexType(keyType, info->keyType)) {
			result.push_back(info);
		}
	}
	return result;
}

// checker.go:32575 — getApplicableIndexSymbol
Symbol* Checker::getApplicableIndexSymbol(Type* t, Type* keyType) {
	IndexInfo* info = getApplicableIndexInfo(t, keyType);
	if (info != nullptr && info != anyBaseTypeIndexInfo) {
		if (info->indexSymbol == nullptr) {
			std::vector<Node*> declarations;
			if (info->declaration != nullptr) {
				declarations = {info->declaration};
			} else {
				for (IndexInfo* i : getIndexInfosOfType(t)) {
					if (i->declaration != nullptr &&
					    isApplicableIndexType(keyType, i->keyType)) {
						declarations.push_back(i->declaration);
					}
				}
			}
			if (!declarations.empty()) {
				Symbol* symbol = newSymbol(SymbolFlagsProperty,
				                           InternalSymbolNameIndex);
				symbol->checkFlags |= CheckFlagsIndexSymbol;
				symbol->data->declarations = declarations;
				symbol->data->valueDeclaration = declarations[0];
				symbol->data->parent = t->symbol;
				auto* links = valueSymbolLinks.Get(symbol);
				links->resolvedType = info->valueType;
				info->indexSymbol = symbol;
			}
		}
		return info->indexSymbol;
	}
	return nullptr;
}

// checker.go:32604 — getRegularTypeOfExpression
Type* Checker::getRegularTypeOfExpression(Node* expr) {
	if (isRightSideOfQualifiedNameOrPropertyAccess(expr)) {
		expr = expr->parent;
	}
	return getRegularTypeOfLiteralType(getTypeOfExpression(expr));
}

// checker.go:32419 — getTypeOfNode (services tail owner)
Type* Checker::getTypeOfNode(Node* node) {
	if (isSourceFile(node) && !isExternalOrCommonJSModule(node->as<SourceFile>())) {
		return errorType;
	}
	if ((node->flags & NodeFlagsInWithStatement) != 0) {
		return errorType;
	}
	bool isImplements = false;
	Node* classDecl = tryGetClassImplementingOrExtendingHeritageClauseElement(node, &isImplements);
	Type* classType = nullptr;
	if (classDecl != nullptr) {
		classType = getDeclaredTypeOfClassOrInterface(getSymbolOfDeclaration(classDecl));
	}
	if (isPartOfTypeNode(node)) {
		Type* typeFromTypeNode = getTypeFromTypeNode(node);
		if (classType != nullptr) {
			return getTypeWithThisArgument(typeFromTypeNode,
										   classType->AsInterfaceType()->thisType,
										   false /*needApparentType*/);
		}
		return typeFromTypeNode;
	}
	if (isExpressionNode(node)) {
		return getRegularTypeOfExpression(node);
	}
	if (classType != nullptr && !isImplements) {
		// An ExpressionWithTypeArguments is a type node except in a class's
		// extends clause; handled here (checker.go:32456).
		Type* baseType = getBaseTypes(classType).empty() ? nullptr
													 : getBaseTypes(classType)[0];
		if (baseType != nullptr) {
			return getTypeWithThisArgument(baseType,
										   classType->AsInterfaceType()->thisType,
										   false /*needApparentType*/);
		}
		return errorType;
	}
	if (isTypeDeclaration(node)) {
		Symbol* symbol = getSymbolOfDeclaration(node);
		return getDeclaredTypeOfSymbol(symbol);
	}
	if (isTypeDeclarationName(node)) {
		Symbol* symbol = getSymbolAtLocation(node, false /*ignoreErrors*/);
		if (symbol != nullptr) {
			return getDeclaredTypeOfSymbol(symbol);
		}
		return errorType;
	}
	if (isBindingElement(node)) {
		Type* t = getTypeForVariableLikeDeclaration(node, true /*includeOptionality*/,
													CheckModeNormal);
		if (t != nullptr) {
			return t;
		}
		return errorType;
	}
	if (isDeclaration(node)) {
		Symbol* symbol = getSymbolOfDeclaration(node);
		if (symbol != nullptr) {
			return getTypeOfSymbol(symbol);
		}
		return errorType;
	}
	if (isDeclarationNameOrImportPropertyName(node)) {
		Symbol* symbol = getSymbolAtLocation(node, false /*ignoreErrors*/);
		if (symbol != nullptr) {
			return getTypeOfSymbol(symbol);
		}
		return errorType;
	}
	if (isBindingPattern(node)) {
		Type* t = getTypeForVariableLikeDeclaration(node->parent,
													true /*includeOptionality*/,
													CheckModeNormal);
		if (t != nullptr) {
			return t;
		}
		return errorType;
	}
	if (isInRightSideOfImportOrExportAssignment(node)) {
		Symbol* symbol = getSymbolAtLocation(node, false /*ignoreErrors*/);
		if (symbol != nullptr) {
			Type* declaredType = getDeclaredTypeOfSymbol(symbol);
			if (!isErrorType(declaredType)) {
				return declaredType;
			}
			return getTypeOfSymbol(symbol);
		}
	}
	if (node->parent != nullptr && isMetaProperty(node->parent) &&
		node->parent->as<MetaProperty>()->KeywordToken == node->kind) {
		return checkMetaPropertyKeyword(node->parent);
	}
	if (isImportAttributes(node)) {
		return checkImportAttributesExpression(node);
	}
	return errorType;
}

// checker.go:32611 — containsArgumentsReference (services tail owner)
bool Checker::containsArgumentsReference(Node* node) {
	if (node->body() == nullptr) {
		return false;
	}
	auto cached = cachedArgumentsReferenced.find(node);
	if (cached != cachedArgumentsReferenced.end()) {
		return cached->second;
	}
	std::function<bool(Node*)> visit = [&](Node* node) -> bool {
		if (node == nullptr) {
			return false;
		}
		switch (node->kind) {
		case Kind::Identifier:
			return node->text() == argumentsSymbol->data->name &&
				IsArgumentsSymbol(getResolvedSymbol(node));
		case Kind::PropertyDeclaration:
		case Kind::MethodDeclaration:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
			if (isComputedPropertyName(node->name())) {
				return visit(node->name());
			}
			break;
		case Kind::PropertyAccessExpression:
		case Kind::ElementAccessExpression:
			return visit(node->expression());
		case Kind::PropertyAssignment:
			return visit(node->initializer());
		default:
			break;
		}
		if (nodeStartsNewLexicalEnvironment(node) || isPartOfTypeNode(node)) {
			return false;
		}
		return node->forEachChild(visit);
	};
	bool containsArguments = visit(node->body());
	cachedArgumentsReferenced[node] = containsArguments;
	return containsArguments;
}

// checker.go:32648 — GetTypeAtLocation
Type* Checker::GetTypeAtLocation(Node* node) {
	return getTypeOfNode(getReparsedNodeForNode(node));
}

// checker.go:32652 — NewEmitResolver
EmitResolver* Checker::NewEmitResolver(printer::EmitContext* emitContext) {
	return newEmitResolver(this, emitContext);
}

// === dep stubs — removed when owner slice lands ===
// Declared in the services decl block (checker.h ~3176) and called from this
// TU, but never defined by any landed slice; stubbed here so the TU links.

// (deduped: getImportAttributesTypeForModuleSpecifier + checkImportAttributesExpression defined in checker_declchecks2.cpp; checkMetaPropertyKeyword in checker_expressions_b.cpp)

}  // namespace checker
}  // namespace tsc
