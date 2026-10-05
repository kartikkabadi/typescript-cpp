// checker_markrefs.cpp — declaration reference marking for tree-shaking/emit.
// Ports checker.go:28655-29384 (markLinkedReferences and everything in the
// range). Functions are ported in file order.
//
// Dep-stub convention (this file only): callees owned by other slices/files are
// defined at the bottom under "// === dep stubs ===" with TSC_UNREACHABLE bodies
// and an owner tag; each is deleted from here when the owner's real definition
// lands. Free functions owned by other files are given static definitions here —
// internal linkage, so they cannot collide with the owner's definition at merge.

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/scanner/scanner.h"

namespace tsc::checker {

// Forward declarations for in-range free functions defined below (Go order).
static bool isExportOrExportExpression(Node* location);
static bool shouldMarkIdentifierAliasReferenced(Node* node);
static bool isInternalModuleImportEqualsDeclaration(Node* node);
static bool isPartOfImportEqualsModuleReference(Node* location);
static Node* getEntityNameFromTypeNode(Node* node);

// ---------------------------------------------------------------------------
// Small free helpers (owned by other files — static copies here)
// ---------------------------------------------------------------------------

// utilities.go:168 — owner: utilities slice (static here; internal linkage, no conflict)
static bool isInTypeQuery(Node* node) {
	// TypeScript 1.0 spec (April 2014): 3.6.3
	// A type query consists of the keyword typeof followed by an expression. The
	// expression is restricted to a single identifier or a sequence of identifiers
	// separated by periods
	return findAncestorOrQuit(node, [](Node* n) -> FindAncestorResult {
		switch (n->kind) {
		case Kind::TypeQuery:
			return FindAncestorResult::True;
		case Kind::Identifier:
		case Kind::QualifiedName:
			return FindAncestorResult::False;
		default:
			return FindAncestorResult::Quit;
		}
	}) != nullptr;
}

// utilities.go:286 — owner: utilities slice (static here; internal linkage, no conflict)
static bool IsTypeAny(Type* t) {
	return t != nullptr && (t->flags & TypeFlagsAny) != 0;
}

// AssignmentKind is declared in checker.h.

static AssignmentKind getAssignmentTargetKind(Node* node) {
	Node* target = getAssignmentTarget(node);
	if (target == nullptr) {
		return AssignmentKind::None;
	}
	switch (target->kind) {
	case Kind::BinaryExpression: {
		Kind binaryOperator = target->as<BinaryExpression>()->OperatorToken->kind;
		if (binaryOperator == Kind::EqualsToken ||
			isLogicalOrCoalescingAssignmentOperator(binaryOperator)) {
			return AssignmentKind::Definite;
		}
		return AssignmentKind::Compound;
	}
	case Kind::PrefixUnaryExpression:
	case Kind::PostfixUnaryExpression:
		return AssignmentKind::Compound;
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		return AssignmentKind::Definite;
	}
	TSC_UNREACHABLE("Unhandled case in getAssignmentTargetKind");
}

// ast/utilities.go:3032 — owner: ast utilities (static; internal linkage, no conflict)
// isThisInTypeQuery — canonical def in ast.cpp
// ast/utilities.go:921 — owner: ast utilities (static; internal linkage, no conflict)
template <typename... F>
static std::array<Node*, sizeof...(F)> findManyAncestors(Node* node, F... callbacks) {
	std::array<Node*, sizeof...(F)> ancestors{};
	std::array<std::function<bool(Node*)>, sizeof...(F)> cbs{std::function<bool(Node*)>(callbacks)...};
	size_t found = 0;
	while (node != nullptr) {
		for (size_t i = 0; i < cbs.size(); i++) {
			if (ancestors[i] == nullptr && cbs[i](node)) {
				ancestors[i] = node;
				found++;
				if (found == cbs.size()) {
					return ancestors;
				}
				break;
			}
		}
		node = node->parent;
	}
	return ancestors;
}

// ast/utilities.go:2639 — owner: ast utilities (static; internal linkage, no conflict)
static Node* getDeclarationContainer(Node* node) {
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

// ast/utilities.go:4493 — owner: ast utilities (static; internal linkage, no conflict)
static Node* getRestParameterElementType(Node* node) {
	if (node == nullptr) {
		return node;
	}
	if (node->kind == Kind::ArrayType) {
		return node->as<ArrayTypeNode>()->ElementType;
	}
	if (node->kind == Kind::TypeReference && node->as<TypeReferenceNode>()->TypeArguments != nullptr) {
		auto& nodes = node->as<TypeReferenceNode>()->TypeArguments->nodes;
		return nodes.empty() ? nullptr : nodes[0];
	}
	return nullptr;
}

// emitresolver.go:693 — owner: emitresolver slice (static here; internal linkage, no conflict)
static bool isConstEnumSymbol(Symbol* s) {
	return (s->flags & SymbolFlagsConstEnum) != 0;
}
static bool isConstEnumOrConstEnumOnlyModule(Symbol* s) {
	return isConstEnumSymbol(s) || (s->flags & SymbolFlagsConstEnumOnlyModule) != 0;
}

// checker.go:27262 — owner: typeops slice (static here; internal linkage, no conflict)
static bool isInvalidComputedPropertyName(Node* node) {
	return (isTypeLiteralNode(node->parent->parent) || isClassLike(node->parent->parent) ||
			isInterfaceDeclaration(node->parent->parent)) &&
		isBinaryExpression(node->expression()) &&
		node->expression()->as<BinaryExpression>()->OperatorToken->kind == Kind::InKeyword &&
		!isAccessor(node->parent);
}

// utilities.go:1159 — owner: utilities slice (static here; internal linkage, no conflict)
// isJsxIntrinsicTagName — canonical def in checker_utilities.cpp
// ---------------------------------------------------------------------------
// markLinkedReferences — checker.go:28655-29018
// ---------------------------------------------------------------------------

void Checker::markLinkedReferences(Node* location, ReferenceHint hint, Symbol* propSymbol, Type* parentType) {
	if (!canCollectSymbolAliasAccessibilityData) {
		return;
	}
	if ((location->flags & NodeFlagsAmbient) && !isPropertySignatureDeclaration(location) &&
		!isPropertyDeclaration(location)) {
		// References within types and declaration files are never going to contribute to retaining a JS import,
		// except for properties (which can be decorated).
		return;
	}
	switch (hint) {
	case ReferenceHint::Identifier:
		markIdentifierAliasReferenced(location);
		break;
	case ReferenceHint::Property:
		markPropertyAliasReferenced(location, propSymbol, parentType);
		break;
	case ReferenceHint::ExportAssignment:
		markExportAssignmentAliasReferenced(location);
		break;
	case ReferenceHint::Jsx:
		markJsxAliasReferenced(location);
		break;
	case ReferenceHint::ExportImportEquals:
		markImportEqualsAliasReferenced(location);
		break;
	case ReferenceHint::ExportSpecifier:
		markExportSpecifierAliasReferenced(location);
		break;
	case ReferenceHint::Decorator:
		markDecoratorAliasReferenced(location);
		break;
	case ReferenceHint::Unspecified: {
		if (location->flags & NodeFlagsInWithStatement) {
			// We cannot answer semantic questions within a with block, do not proceed any further
			return;
		}
		if (isJsxTagName(location) && isJsxIntrinsicTagName(location)) {
			return; // builtin JSX tag names aren't real type refs by most metrics, but are expressions, so must be filtered
		}
		if (isIdentifier(location)) {
			// A shorthand property with an object-assignment-initializer (e.g. `{ s = 5 }`) is only valid inside a
			// destructuring assignment target. When it appears in an ordinary object literal expression, the checker
			// checks the initializer and never resolves the property name, so resolving it here would report a spurious
			// "No value exists in scope for the shorthand property" diagnostic. Skip such names to match checking.
			if (Node* parent = location->parent; isShorthandPropertyAssignment(parent) &&
				parent->name() == location &&
				parent->as<ShorthandPropertyAssignment>()->ObjectAssignmentInitializer != nullptr &&
				!isAssignmentTarget(parent->parent)) {
				return;
			}
			auto res = findManyAncestors(location, isMetaProperty, isDecorator, isForInOrOfStatement,
				isComputedPropertyName, isHeritageClause);
			Node* metaProperty = res[0];
			Node* decorator = res[1];
			Node* forNode = res[2];
			Node* computedName = res[3];
			Node* heritageClause = res[4];
			if (metaProperty != nullptr) {
				return; // identifiers in meta properties shouldn't be resolved, but are expressions, so must be filtered
			}
			if (decorator != nullptr) {
				// Decorators on nodes that cannot be decorated (e.g. class expressions, static blocks,
				// `this` parameters) are never resolved during normal checking, so resolving them here would
				// report spurious diagnostics. Only bail out for such invalid-position decorators; valid
				// decorator expressions must still be resolved and marked for emit.
				Node* decorated = decorator->parent;
				if (decorated != nullptr &&
					!nodeCanBeDecorated(legacyDecorators, decorated, decorated->parent, decorated->parent->parent)) {
					return;
				}
			}
			// The right-hand side of a 'for-in'/'for-of' statement whose initializer is an empty variable
			// declaration list (a grammar error, e.g. `for (var of X)`) is never checked, because the RHS
			// is only checked while inferring the type of a variable declaration and there is none here.
			// Resolving identifiers in the RHS here would report spurious diagnostics.
			if (forNode != nullptr) {
				ForInOrOfStatement* data = forNode->as<ForInOrOfStatement>();
				if (isVariableDeclarationList(data->Initializer) &&
					data->Initializer->as<VariableDeclarationList>()->Declarations->nodes.empty() &&
					data->Expression != nullptr &&
					(location == data->Expression || isNodeDescendantOf(location, data->Expression))) {
					return;
				}
			}
			// Computed property names on enum members are a grammar error and are never checked
			// (checkEnumMember only checks the member initializer, not the name), so resolving
			// identifiers in them here would report a spurious "Cannot find name" diagnostic.
			if (computedName != nullptr) {
				if (isEnumMember(computedName->parent)) {
					return;
				}
				if (isInvalidComputedPropertyName(computedName)) {
					return;
				}
			}
			if (heritageClause != nullptr) {
				// extends heritage clauses on interfaces are not expressions and are unchecked if they are
				if (isInterfaceDeclaration(heritageClause->parent)) {
					return;
				}
				// On a class, only the first `extends` type is resolved as a value (the base class); any
				// additional `extends` types are grammar errors (e.g. `class C extends A extends B` or
				// `class C extends A, B`) and are never resolved during checking.
				if (isClassLike(heritageClause->parent) &&
					heritageClause->as<HeritageClause>()->Token == Kind::ExtendsKeyword) {
					if (Node* firstExtends = getClassExtendsHeritageElement(heritageClause->parent);
						firstExtends != nullptr && location != firstExtends &&
						!isNodeDescendantOf(location, firstExtends)) {
						return;
					}
				}
			}
			// Identifiers in expression contexts are emitted, so we need to follow their referenced aliases and mark them as used
			// Some non-expression identifiers are also treated as expression identifiers for this purpose, eg, `a` in `b = {a}` or `q` in `import r = q`
			// This is the exception, rather than the rule - most non-expression identifiers are declaration names.
			if ((isExpressionNode(location) ||
				isShorthandPropertyAssignment(location->parent)) &&
				shouldMarkIdentifierAliasReferenced(location)) {
				if (isPropertyAccessOrQualifiedName(location->parent)) {
					Node* left;
					if (isPropertyAccessExpression(location->parent)) {
						left = location->parent->expression();
					} else {
						left = location->parent->as<QualifiedName>()->Left;
					}
					if (left != location) {
						return; // Only mark the LHS (the RHS is a property lookup)
					}
				}
				markIdentifierAliasReferenced(location);
				return;
			}
		}
		if (isPropertyAccessOrQualifiedName(location)) {
			Node* topProp = location;
			while (isPropertyAccessOrQualifiedName(topProp)) {
				if (isPartOfTypeNode(topProp)) {
					return;
				}
				topProp = topProp->parent;
			}
			markPropertyAliasReferenced(location, nullptr /*propSymbol*/, nullptr /*parentType*/);
			return;
		}
		if (isExportAssignment(location)) {
			markExportAssignmentAliasReferenced(location);
			return;
		}
		if (isJsxOpeningLikeElement(location) || isJsxOpeningFragment(location)) {
			markJsxAliasReferenced(location);
			return;
		}
		if (isImportEqualsDeclaration(location)) {
			if (isInternalModuleImportEqualsDeclaration(location) || checkExternalImportOrExportDeclaration(location)) {
				markImportEqualsAliasReferenced(location);
				return;
			}
			return;
		}
		if (isExportSpecifier(location)) {
			markExportSpecifierAliasReferenced(location);
			return;
		}
		if (!tristateIsTrue(compilerOptions->EmitDecoratorMetadata)) {
			return;
		}
		if (!canHaveDecorators(location) || !hasDecorators(location) || location->modifiers() == nullptr ||
			!nodeCanBeDecorated(legacyDecorators, location, location->parent, location->parent->parent)) {
			return;
		}

		markDecoratorAliasReferenced(location);
		return;
	}
	default:
		TSC_UNREACHABLE("Unhandled reference hint");
	}
}

// checker.go:29021
static bool isExportOrExportExpression(Node* location) {
	return findAncestor(location, [](Node* n) -> bool {
		Node* parent = n->parent;
		if (parent != nullptr) {
			if (isAnyExportAssignment(parent)) {
				return parent->expression() == n && isEntityNameExpression(n);
			}
			if (isExportSpecifier(parent)) {
				return parent->as<ExportSpecifier>()->name == n || parent->propertyName() == n;
			}
		}
		return false;
	}) != nullptr;
}

// checker.go:29036
static bool shouldMarkIdentifierAliasReferenced(Node* node /*Identifier*/) {
	Node* parent = node->parent;
	if (parent != nullptr) {
		// A property access expression LHS? checkPropertyAccessExpression will handle that.
		if (isPropertyAccessExpression(parent) && parent->expression() == node) {
			return false;
		}
		// Next two check for an identifier inside a type only export.
		if (isExportSpecifier(parent) && parent->isTypeOnly()) {
			return false;
		}
		if (parent->parent != nullptr) {
			Node* greatGrandparent = parent->parent->parent;
			if (greatGrandparent != nullptr && isExportDeclaration(greatGrandparent) &&
				greatGrandparent->isTypeOnly()) {
				return false;
			}
		}
	}
	return true;
}

// checker.go:29057
static bool isInternalModuleImportEqualsDeclaration(Node* node) {
	return node->kind == Kind::ImportEqualsDeclaration &&
		node->as<ImportEqualsDeclaration>()->ModuleReference->kind != Kind::ExternalModuleReference;
}

void Checker::markIdentifierAliasReferenced(Node* location /*Identifier*/) {
	if (isThisInTypeQuery(location)) {
		return;
	}
	Symbol* symbol = getResolvedSymbol(location);
	if (symbol != nullptr && symbol != argumentsSymbol && symbol != unknownSymbol) {
		markAliasReferenced(symbol, location);
	}
}

void Checker::markPropertyAliasReferenced(Node* location /*PropertyAccessExpression | QualifiedName*/,
	Symbol* propSymbol, Type* parentType) {
	if (isPartOfImportEqualsModuleReference(location)) {
		return;
	}
	Node* left;
	if (isPropertyAccessExpression(location)) {
		left = location->expression();
	} else {
		left = location->as<QualifiedName>()->Left;
	}
	if (isThisIdentifier(left) || !isIdentifier(left)) {
		return;
	}
	Symbol* parentSymbol = getResolvedSymbol(left);
	if (parentSymbol == nullptr || parentSymbol == unknownSymbol) {
		return;
	}
	// In `Foo.Bar.Baz`, 'Foo' is not referenced if 'Bar' is a const enum or a module containing only const enums.
	// `Foo` is also not referenced in `enum FooCopy { Bar = Foo.Bar }`, because the enum member value gets inlined
	// here even if `Foo` is not a const enum.
	//
	// The exceptions are:
	//   1. if 'isolatedModules' is enabled, because the const enum value will not be inlined, and
	//   2. if 'preserveConstEnums' is enabled and the expression is itself an export, e.g. `export = Foo.Bar.Baz`.
	//
	// The property lookup is deferred as much as possible, in as many situations as possible, to avoid alias marking
	// pulling on types/symbols it doesn't strictly need to.
	if (compilerOptions->GetIsolatedModules() ||
		(compilerOptions->ShouldPreserveConstEnums() && isExportOrExportExpression(location))) {
		markAliasReferenced(parentSymbol, location);
		return;
	}
	// Hereafter, this relies on type checking - but every check prior to this only used symbol information
	Type* leftType = parentType;
	if (leftType == nullptr) {
		leftType = checkExpressionCached(left);
	}
	if (IsTypeAny(leftType) || leftType == silentNeverType) {
		markAliasReferenced(parentSymbol, location);
		return;
	}
	Symbol* prop = propSymbol;
	if (prop == nullptr && parentType == nullptr) {
		Node* right;
		if (isPropertyAccessExpression(location)) {
			right = location->as<PropertyAccessExpression>()->name;
		} else {
			right = location->as<QualifiedName>()->Right;
		}
		Symbol* lexicallyScopedSymbol = nullptr;
		if (isPrivateIdentifier(right)) {
			lexicallyScopedSymbol = lookupSymbolForPrivateIdentifierDeclaration(right->text(), right);
		}
		AssignmentKind assignmentKind = getAssignmentTargetKind(location);
		Type* apparentType;
		if (assignmentKind != AssignmentKind::None || isMethodAccessForCall(location)) {
			apparentType = getApparentType(getWidenedType(leftType));
		} else {
			apparentType = getApparentType(leftType);
		}
		if (isPrivateIdentifier(right)) {
			if (lexicallyScopedSymbol != nullptr) {
				prop = getPrivateIdentifierPropertyOfType(apparentType, lexicallyScopedSymbol);
			}
		} else {
			prop = getPropertyOfType(apparentType, right->text());
		}
	}
	if (!(prop != nullptr && (isConstEnumOrConstEnumOnlyModule(prop) ||
			((prop->flags & SymbolFlagsEnumMember) != 0 && location->parent->kind == Kind::EnumMember)))) {
		markAliasReferenced(parentSymbol, location);
	}
}

// checker.go:29144
static bool isPartOfImportEqualsModuleReference(Node* location) {
	Node* importEquals = findAncestorKind(location, Kind::ImportEqualsDeclaration);
	if (importEquals == nullptr) {
		return false;
	}
	for (Node* node = location; node != nullptr && node != importEquals; node = node->parent) {
		if (node == importEquals->as<ImportEqualsDeclaration>()->ModuleReference) {
			return true;
		}
	}
	return false;
}

void Checker::markExportAssignmentAliasReferenced(Node* location /*ExportAssignment*/) {
	Node* id = location->expression();
	if (isIdentifier(id)) {
		Symbol* sym = getExportSymbolOfValueSymbolIfExported(
			resolveEntityName(id, SymbolFlagsAll, true /*ignoreErrors*/, true /*dontResolveAlias*/, location));
		if (sym != nullptr) {
			markAliasReferenced(sym, id);
		}
	}
}

void Checker::markJsxAliasReferenced(Node* node /*JsxOpeningLikeElement | JsxOpeningFragment*/) {
	if (getJsxNamespaceContainerForImplicitImport(node) != nullptr) {
		return;
	}
	// The reactNamespace/jsxFactory's root symbol should be marked as 'used' so we don't incorrectly elide its import.
	// And if there is no reactNamespace/jsxFactory's symbol in scope when targeting React emit, we should issue an error.
	const DiagnosticMessage* jsxFactoryRefErr =
		compilerOptions->Jsx == JsxEmit::React ? This_JSX_tag_requires_0_to_be_in_scope_but_it_could_not_be_found : nullptr;
	std::string jsxFactoryNamespace = getJsxNamespace(node);
	Node* jsxFactoryLocation = node;
	if (isJsxOpeningLikeElement(node)) {
		jsxFactoryLocation = node->tagName();
	}
	bool shouldFactoryRefErr = compilerOptions->Jsx != JsxEmit::Preserve && compilerOptions->Jsx != JsxEmit::ReactNative;
	// #38720/60122, allow null as jsxFragmentFactory
	Symbol* jsxFactorySym = nullptr;
	if (!(isJsxOpeningFragment(node) && jsxFactoryNamespace == "null")) {
		SymbolFlags flags = SymbolFlagsValue;
		if (!shouldFactoryRefErr) {
			flags &= ~SymbolFlagsEnum;
		}
		jsxFactorySym = resolveName(jsxFactoryLocation, jsxFactoryNamespace, flags, jsxFactoryRefErr,
			true /*isUse*/, false /*excludeGlobals*/);
	}
	if (jsxFactorySym != nullptr) {
		// Mark local symbol as referenced here because it might not have been marked
		// if jsx emit was not jsxFactory as there wont be error being emitted
		symbolReferenced(jsxFactorySym, SymbolFlagsAll);
		// If react/jsxFactory symbol is alias, mark it as referenced
		if (canCollectSymbolAliasAccessibilityData && (jsxFactorySym->flags & SymbolFlagsAlias) &&
			getTypeOnlyAliasDeclaration(jsxFactorySym) == nullptr) {
			markAliasSymbolAsReferenced(jsxFactorySym);
		}
	}
	// if JsxFragment, additionally mark jsx pragma as referenced, since `getJsxNamespace` above would have resolved to only the fragment factory if they are distinct
	if (isJsxOpeningFragment(node)) {
		SourceFile* file = getSourceFileOfNode(node);
		Node* entity = getJsxFactoryEntity(file);
		if (entity != nullptr) {
			std::string localJsxNamespace = getFirstIdentifier(entity)->text();
			SymbolFlags flags = SymbolFlagsValue;
			if (!shouldFactoryRefErr) {
				flags &= ~SymbolFlagsEnum;
			}
			resolveName(jsxFactoryLocation, localJsxNamespace, flags, jsxFactoryRefErr,
				true /*isUse*/, false /*excludeGlobals*/);
		}
	}
}

void Checker::markImportEqualsAliasReferenced(Node* location /*ImportEqualsDeclaration*/) {
	if (hasSyntacticModifier(location, ModifierFlagsExport)) {
		markExportAsReferenced(location);
	}
}

void Checker::markExportSpecifierAliasReferenced(Node* location /*ExportSpecifier*/) {
	if (location->parent->parent->moduleSpecifier() == nullptr && !location->isTypeOnly() &&
		!location->parent->parent->isTypeOnly()) {
		Node* exportedName = location->propertyNameOrName();
		if (exportedName->kind == Kind::StringLiteral) {
			return; // Skip for invalid syntax like this: export { "x" }
		}
		Symbol* symbol = resolveName(exportedName, exportedName->text(),
			SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace | SymbolFlagsAlias,
			nullptr /*nameNotFoundMessage*/, true /*isUse*/, false /*excludeGlobals*/);
		if (symbol != nullptr &&
			(symbol == undefinedSymbol || symbol == globalThisSymbol ||
				(!symbol->declarations.empty() &&
					isGlobalSourceFile(getDeclarationContainer(symbol->declarations[0]))))) {
			// Do nothing, non-local symbol
		} else {
			Symbol* target = symbol;
			if (target != nullptr && (target->flags & SymbolFlagsAlias)) {
				target = resolveAlias(target);
			}
			if (target == nullptr || (getSymbolFlags(target) & SymbolFlagsValue) != 0) {
				markExportAsReferenced(location);            // marks export as used
				markIdentifierAliasReferenced(exportedName); // marks target of export as used
			}
		}
	}
}

void Checker::checkExternalEmitHelpers(Node* location, ExternalEmitHelpers helpers) {
	if (!tristateIsTrue(compilerOptions->ImportHelpers)) {
		return;
	}
	SourceFile* sourceFile = getSourceFileOfNode(location);
	if (!isEffectiveExternalModule(sourceFile, compilerOptions) || (location->flags & NodeFlagsAmbient)) {
		return;
	}
	Symbol* helpersModule = resolveHelpersModule(sourceFile, location);
	if (helpersModule == unknownSymbol) {
		return;
	}
	SourceFileLinks* links = sourceFileLinks.Get(sourceFile);
	if ((links->requestedExternalEmitHelpers & helpers) != helpers) {
		ExternalEmitHelpers uncheckedHelpers = helpers & ~links->requestedExternalEmitHelpers;
		for (ExternalEmitHelpers helper = ExternalEmitHelpersFirstEmitHelper;
			 helper <= ExternalEmitHelpersLastEmitHelper; helper <<= 1) {
			if ((uncheckedHelpers & helper) == 0) {
				continue;
			}
			for (const std::string& name : getHelperNames(helper)) {
				SymbolTable exports = getExportsOfModule(helpersModule);
				Symbol* symbol = resolveSymbol(getSymbol(exports, name, SymbolFlagsValue));
				if (symbol == nullptr) {
					error(location,
						This_syntax_requires_an_imported_helper_named_1_which_does_not_exist_in_0_Consider_upgrading_your_version_of_0,
						{externalHelpersModuleNameText, name});
				} else if (helper & ExternalEmitHelpersClassPrivateFieldGet) {
					if (!hasSignatureWithArityGreaterThan(symbol, 3)) {
						error(location,
							This_syntax_requires_an_imported_helper_named_1_with_2_parameters_which_is_not_compatible_with_the_one_in_0_Consider_upgrading_your_version_of_0,
							{externalHelpersModuleNameText, name, "4"});
					}
				} else if (helper & ExternalEmitHelpersClassPrivateFieldSet) {
					if (!hasSignatureWithArityGreaterThan(symbol, 4)) {
						error(location,
							This_syntax_requires_an_imported_helper_named_1_with_2_parameters_which_is_not_compatible_with_the_one_in_0_Consider_upgrading_your_version_of_0,
							{externalHelpersModuleNameText, name, "5"});
					}
				}
			}
		}
	}
	links->requestedExternalEmitHelpers |= helpers;
}

bool Checker::hasSignatureWithArityGreaterThan(Symbol* symbol, int arity) {
	for (Signature* signature : getSignaturesOfSymbol(symbol)) {
		if (getParameterCount(signature) > arity) {
			return true;
		}
	}
	return false;
}

std::vector<std::string> Checker::getHelperNames(ExternalEmitHelpers helper) {
	switch (helper) {
	case ExternalEmitHelpersRest:
		return {"__rest"};
	case ExternalEmitHelpersDecorate:
		if (legacyDecorators) {
			return {"__decorate"};
		}
		return {"__esDecorate", "__runInitializers"};
	case ExternalEmitHelpersMetadata:
		return {"__metadata"};
	case ExternalEmitHelpersParam:
		return {"__param"};
	case ExternalEmitHelpersAwaiter:
		return {"__awaiter"};
	case ExternalEmitHelpersAwait:
		return {"__await"};
	case ExternalEmitHelpersAsyncGenerator:
		return {"__asyncGenerator"};
	case ExternalEmitHelpersAsyncDelegator:
		return {"__asyncDelegator"};
	case ExternalEmitHelpersAsyncValues:
		return {"__asyncValues"};
	case ExternalEmitHelpersExportStar:
		return {"__exportStar"};
	case ExternalEmitHelpersImportStar:
		return {"__importStar"};
	case ExternalEmitHelpersImportDefault:
		return {"__importDefault"};
	case ExternalEmitHelpersMakeTemplateObject:
		return {"__makeTemplateObject"};
	case ExternalEmitHelpersClassPrivateFieldGet:
		return {"__classPrivateFieldGet"};
	case ExternalEmitHelpersClassPrivateFieldSet:
		return {"__classPrivateFieldSet"};
	case ExternalEmitHelpersClassPrivateFieldIn:
		return {"__classPrivateFieldIn"};
	case ExternalEmitHelpersSetFunctionName:
		return {"__setFunctionName"};
	case ExternalEmitHelpersPropKey:
		return {"__propKey"};
	case ExternalEmitHelpersAddDisposableResourceAndDisposeResources:
		return {"__addDisposableResource", "__disposeResources"};
	case ExternalEmitHelpersRewriteRelativeImportExtension:
		return {"__rewriteRelativeImportExtension"};
	default:
		TSC_UNREACHABLE("Unrecognized helper");
	}
}

Symbol* Checker::resolveHelpersModule(SourceFile* file, Node* errorNode) {
	SourceFileLinks* links = sourceFileLinks.Get(file);
	if (links->externalHelpersModule == nullptr) {
		Node* location = program->GetImportHelpersImportSpecifier(file->Path());
		Symbol* helpersModule = resolveExternalModule(location, externalHelpersModuleNameText,
			This_syntax_requires_an_imported_helper_but_module_0_cannot_be_found, errorNode,
			false /*isForAugmentation*/, nullptr /*importAttributesType*/);
		if (helpersModule == nullptr) {
			helpersModule = unknownSymbol;
		}
		links->externalHelpersModule = helpersModule;
	}
	return links->externalHelpersModule;
}

void Checker::markDecoratorAliasReferenced(Node* node /*HasDecorators*/) {
	if (tristateIsFalseOrUnknown(compilerOptions->EmitDecoratorMetadata)) {
		return;
	}
	auto decorators = node->decorators();
	Node* firstDecorator = decorators.empty() ? nullptr : decorators[0];
	if (firstDecorator == nullptr) {
		return;
	}

	checkExternalEmitHelpers(firstDecorator, ExternalEmitHelpersMetadata);

	// we only need to perform these checks if we are emitting serialized type metadata for the target of a decorator.
	switch (node->kind) {
	case Kind::ClassDeclaration:
		if (Node* ctor = getFirstConstructorWithBody(node); ctor != nullptr) {
			for (Node* p : ctor->parameters()) {
				markDecoratorMedataDataTypeNodeAsReferenced(getParameterTypeNodeForDecoratorCheck(p));
			}
		}
		break;
	case Kind::GetAccessor:
	case Kind::SetAccessor: {
		Kind otherKind = Kind::SetAccessor;
		if (node->kind == Kind::SetAccessor) {
			otherKind = Kind::GetAccessor;
		}
		Node* otherAccessor = getDeclarationOfKind(getSymbolOfDeclaration(node), otherKind);
		Node* annotation = getAnnotatedAccessorTypeNode(node);
		if (annotation == nullptr && otherAccessor != nullptr) {
			annotation = getAnnotatedAccessorTypeNode(otherAccessor);
		}
		markDecoratorMedataDataTypeNodeAsReferenced(annotation);
		break;
	}
	case Kind::MethodDeclaration:
		for (Node* p : node->parameters()) {
			markDecoratorMedataDataTypeNodeAsReferenced(getParameterTypeNodeForDecoratorCheck(p));
		}
		markDecoratorMedataDataTypeNodeAsReferenced(node->type());
		break;
	case Kind::PropertyDeclaration:
		markDecoratorMedataDataTypeNodeAsReferenced(node->type());
		break;
	case Kind::Parameter: {
		markDecoratorMedataDataTypeNodeAsReferenced(getParameterTypeNodeForDecoratorCheck(node));
		Node* containingSignature = node->parent;
		for (Node* p : containingSignature->parameters()) {
			markDecoratorMedataDataTypeNodeAsReferenced(getParameterTypeNodeForDecoratorCheck(p));
		}
		markDecoratorMedataDataTypeNodeAsReferenced(containingSignature->type());
		break;
	}
	default:
		break;
	}
}

Node* Checker::getParameterTypeNodeForDecoratorCheck(Node* node /*ParameterDeclaration*/) {
	Node* typeNode = node->type();
	if (node->as<ParameterDeclaration>()->DotDotDotToken != nullptr) {
		return getRestParameterElementType(typeNode);
	}
	return typeNode;
}

void Checker::markDecoratorMedataDataTypeNodeAsReferenced(Node* node /*TypeNode*/) {
	Node* entityName = getEntityNameForDecoratorMetadata(node);
	if (entityName != nullptr && isEntityName(entityName)) {
		markEntityNameOrEntityExpressionAsReference(entityName, true);
	}
}

Node* Checker::getEntityNameForDecoratorMetadata(Node* node) {
	if (node == nullptr) {
		return node;
	}
	switch (node->kind) {
	case Kind::IntersectionType:
		return getEntityNameForDecoratorMetadataFromTypeList(node->as<IntersectionTypeNode>()->Types->nodes);
	case Kind::UnionType:
		return getEntityNameForDecoratorMetadataFromTypeList(node->as<UnionTypeNode>()->Types->nodes);
	case Kind::ConditionalType:
		return getEntityNameForDecoratorMetadataFromTypeList(
			{node->as<ConditionalTypeNode>()->TrueType, node->as<ConditionalTypeNode>()->FalseType});
	case Kind::ParenthesizedType:
		return getEntityNameForDecoratorMetadata(node->as<ParenthesizedTypeNode>()->Type);
	case Kind::NamedTupleMember:
		return getEntityNameForDecoratorMetadata(node->as<NamedTupleMember>()->Type);
	case Kind::TypeReference:
		return node->as<TypeReferenceNode>()->TypeName;
	}
	return nullptr;
}

Node* Checker::getEntityNameForDecoratorMetadataFromTypeList(std::vector<Node*> typeNodes) {
	Node* commonEntityName = nullptr;
	for (Node* typeNode : typeNodes) {
		if (typeNode->kind == Kind::NeverKeyword) {
			continue; // Always elide `never` from the union/intersection if possible
		}
		if (!strictNullChecks &&
			((typeNode->kind == Kind::LiteralType &&
				 typeNode->as<LiteralTypeNode>()->Literal->kind == Kind::NullKeyword) ||
				typeNode->kind == Kind::UndefinedKeyword)) {
			continue; // Elide null and undefined from unions for metadata, just like what we did prior to the implementation of strict null checks
		}
		Node* individualEntityName = getEntityNameForDecoratorMetadata(typeNode);
		if (individualEntityName == nullptr) {
			// Individual is something like string number
			// So it would be serialized to either that type or object
			// Safe to return here
			return nullptr;
		}

		if (commonEntityName == nullptr) {
			commonEntityName = individualEntityName;
		} else {
			// Note this is in sync with the transformation that happens for type node.
			// Keep this in sync with serializeUnionOrIntersectionType
			// Verify if they refer to same entity and is identifier
			// return undefined if they dont match because we would emit object
			if (!isIdentifier(commonEntityName) || !isIdentifier(individualEntityName) ||
				commonEntityName->as<Identifier>()->Text != individualEntityName->as<Identifier>()->Text) {
				return nullptr;
			}
		}
	}
	return commonEntityName;
}

void Checker::markAliasReferenced(Symbol* symbol, Node* location) {
	if (!canCollectSymbolAliasAccessibilityData) {
		return;
	}
	if (isNonLocalAlias(symbol, SymbolFlagsValue /*excludes*/) && !isInTypeQuery(location)) {
		Symbol* target = resolveAlias(symbol);
		if ((getSymbolFlagsEx(symbol, true /*excludeTypeOnlyMeanings*/, false /*excludeLocalMeanings*/) &
				(SymbolFlagsValue | SymbolFlagsExportValue)) != 0) {
			// An alias resolving to a const enum cannot be elided if (1) 'isolatedModules' is enabled
			// (because the const enum value will not be inlined), or if (2) the alias is an export
			// of a const enum declaration that will be preserved.
			if (compilerOptions->GetIsolatedModules() ||
				(compilerOptions->ShouldPreserveConstEnums() && isExportOrExportExpression(location)) ||
				!isConstEnumOrConstEnumOnlyModule(getExportSymbolOfValueSymbolIfExported(target))) {
				markAliasSymbolAsReferenced(symbol);
			}
		}
	}
}

// When an alias symbol is referenced, we need to mark the entity it references as referenced and in turn repeat that until
// we reach a non-alias or an exported entity (which is always considered referenced). We do this by checking the target of
// the alias as an expression (which recursively takes us back here if the target references another alias).
void Checker::markAliasSymbolAsReferenced(Symbol* symbol) {
	AliasSymbolLinks* links = aliasSymbolLinks.Get(symbol);
	if (!links->referenced) {
		links->referenced = true;
		Node* node = getDeclarationOfAliasSymbol(symbol);
		if (node == nullptr) {
			TSC_UNREACHABLE("Unexpected nil in markAliasSymbolAsReferenced");
		}
		// We defer checking of the reference of an `import =` until the import itself is referenced,
		// This way a chain of imports can be elided if ultimately the final input is only used in a type
		// position.
		if (isImportEqualsDeclaration(node) &&
			node->as<ImportEqualsDeclaration>()->ModuleReference->kind != Kind::ExternalModuleReference) {
			if ((getSymbolFlags(resolveSymbol(symbol)) & SymbolFlagsValue) != 0) {
				// import foo = <symbol>
				Node* left = getFirstIdentifier(node->as<ImportEqualsDeclaration>()->ModuleReference);
				markIdentifierAliasReferenced(left);
			}
		}
	}
}

void Checker::markExportAsReferenced(Node* node /*ImportEqualsDeclaration | ExportSpecifier*/) {
	Symbol* symbol = getSymbolOfDeclaration(node);
	Symbol* target = resolveAlias(symbol);
	if (target != nullptr) {
		bool markAlias = target == unknownSymbol ||
			((getSymbolFlagsEx(symbol, true /*excludeTypeOnlyMeanings*/, false /*excludeLocalMeanings*/) &
				 SymbolFlagsValue) != 0 &&
				!isConstEnumOrConstEnumOnlyModule(target));
		if (markAlias) {
			markAliasSymbolAsReferenced(symbol);
		}
	}
}

void Checker::markEntityNameOrEntityExpressionAsReference(Node* typeName /*EntityNameOrEntityNameExpression | nil*/,
	bool forDecoratorMetadata) {
	if (typeName == nullptr) {
		return;
	}

	Node* rootName = getFirstIdentifier(typeName);
	SymbolFlags meaning =
		(typeName->kind == Kind::Identifier ? SymbolFlagsType : SymbolFlagsNamespace) | SymbolFlagsAlias;
	Symbol* rootSymbol = resolveName(rootName, rootName->text(), meaning, nullptr /*nameNotFoundMessage*/,
		true /*isUse*/, false /*excludeGlobals*/);

	if (rootSymbol != nullptr && (rootSymbol->flags & SymbolFlagsAlias)) {
		if (canCollectSymbolAliasAccessibilityData &&
			symbolIsValue(rootSymbol) &&
			!isConstEnumOrConstEnumOnlyModule(resolveAlias(rootSymbol)) &&
			getTypeOnlyAliasDeclaration(rootSymbol) == nullptr) {
			markAliasSymbolAsReferenced(rootSymbol);
		} else if (forDecoratorMetadata &&
			compilerOptions->GetIsolatedModules() &&
			compilerOptions->GetEmitModuleKind() >= ModuleKind::ES2015 &&
			!symbolIsValue(rootSymbol) &&
			[&] {
				for (Node* decl : rootSymbol->declarations) {
					if (isTypeOnlyImportOrExportDeclaration(decl)) {
						return false;
					}
				}
				return true;
			}()) {
			Diagnostic* diag = error(typeName,
				A_type_referenced_in_a_decorated_signature_must_be_imported_with_import_type_or_a_namespace_import_when_isolatedModules_and_emitDecoratorMetadata_are_enabled);
			Node* aliasDeclaration = nullptr;
			for (Node* decl : rootSymbol->declarations) {
				if (isAliasSymbolDeclaration(decl)) {
					aliasDeclaration = decl;
					break;
				}
			}
			if (aliasDeclaration != nullptr) {
				diag->SetRelatedInfo({createDiagnosticForNode(aliasDeclaration, X_0_was_imported_here,
					{rootName->text()})});
			}
		}
	}
}

// checker.go:29362
static Node* getEntityNameFromTypeNode(Node* node /*TypeNode*/) {
	switch (node->kind) {
	case Kind::TypeReference:
		return node->as<TypeReferenceNode>()->TypeName;

	case Kind::ExpressionWithTypeArguments:
		if (isEntityNameExpression(node->expression())) {
			return node->expression();
		}
		return nullptr;

	// These aren't valid TypeNodes, but we treat them as such because of `isPartOfTypeNode`, which returns `true` for things that aren't `TypeNode`s.
	case Kind::Identifier:
	case Kind::QualifiedName:
		return node;
	}

	return nullptr;
}

// If a TypeNode can be resolved to a value symbol imported from an external module, it is
// marked as referenced to prevent import elision.
void Checker::markTypeNodeAsReferenced(Node* node /*TypeNode*/) {
	if (node != nullptr) {
		markEntityNameOrEntityExpressionAsReference(getEntityNameFromTypeNode(node), false /*forDecoratorMetadata*/);
	}
}

// ---------------------------------------------------------------------------
// === dep stubs — removed when owner slice lands ===
// ---------------------------------------------------------------------------

// owner: expressions slice (checker.go:8090-14185)
// (deduped: getResolvedSymbol defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: isMethodAccessForCall defined in the owning slice file)
// (deduped: getPrivateIdentifierPropertyOfType defined in the owning slice file)

// (deduped: getResolvedSymbol defined in the owning slice file)
// owner: jsx slice (jsx.go)
Symbol* Checker::getJsxNamespaceContainerForImplicitImport(Node* location) {
	TSC_UNREACHABLE("getJsxNamespaceContainerForImplicitImport — jsx slice");
}
std::string Checker::getJsxNamespace(Node* location) { TSC_UNREACHABLE("getJsxNamespace — jsx slice"); }
Node* Checker::getJsxFactoryEntity(Node* location) { TSC_UNREACHABLE("getJsxFactoryEntity — jsx slice"); }
// owner: relater slice (relater.go)
int Checker::getParameterCount(Signature* signature) { TSC_UNREACHABLE("getParameterCount — relater slice"); }
// owner: signatures slice (checker.go:20143-20986)
// (deduped: checkExternalImportOrExportDeclaration defined in cpp/internal/checker/checker_declchecks2.cpp)
// owner: instantiate slice (checker.go:22285-23219)

}  // namespace tsc::checker
