// === slice: ls-foundation ===
// lsutil/symbol_display.go — ScriptElementKind classification for symbols.

#include "internal/ls/lsutil/lsutil.h"

#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/collections/collections.h"

namespace tsc::ls::lsutil {

namespace {
// core.Some
template <class T, class Pred>
bool some(const std::vector<T>& v, Pred pred) {
	for (const auto& e : v) {
		if (pred(e)) return true;
	}
	return false;
}
} // namespace

// Unexported helpers (symbol_display.go) — forward decls for use before
// definition.
static ScriptElementKind getSymbolKindOfConstructorPropertyMethodAccessorFunctionOrVar(
	checker::Checker* typeChecker, Symbol* symbol, Node* location);
static bool isFirstDeclarationOfSymbolParameter(Symbol* symbol);
static bool isLocalVariableOrFunction(Symbol* symbol);
static ScriptElementKindModifier getNormalizedSymbolModifiers(checker::Checker* typeChecker, Symbol* symbol);
static bool isDeprecatedDeclaration(checker::Checker* typeChecker, Node* declaration);
static ScriptElementKindModifier getNodeModifiers(checker::Checker* typeChecker, Node* node, ModifierFlags excludeFlags);

// scriptElementKindModifierNames — symbol_display.go:104
static const struct {
	ScriptElementKindModifier flag;
	const char* name;
} scriptElementKindModifierNames[] = {
	{ScriptElementKindModifierPublic, "public"},
	{ScriptElementKindModifierPrivate, "private"},
	{ScriptElementKindModifierProtected, "protected"},
	{ScriptElementKindModifierExported, "export"},
	{ScriptElementKindModifierAmbient, "declare"},
	{ScriptElementKindModifierStatic, "static"},
	{ScriptElementKindModifierAbstract, "abstract"},
	{ScriptElementKindModifierOptional, "optional"},
	{ScriptElementKindModifierDeprecated, "deprecated"},
	{ScriptElementKindModifierDts, ".d.ts"},
	{ScriptElementKindModifierTs, ".ts"},
	{ScriptElementKindModifierTsx, ".tsx"},
	{ScriptElementKindModifierJs, ".js"},
	{ScriptElementKindModifierJsx, ".jsx"},
	{ScriptElementKindModifierJson, ".json"},
	{ScriptElementKindModifierDmts, ".d.mts"},
	{ScriptElementKindModifierMts, ".mts"},
	{ScriptElementKindModifierMjs, ".mjs"},
	{ScriptElementKindModifierDcts, ".d.cts"},
	{ScriptElementKindModifierCts, ".cts"},
	{ScriptElementKindModifierCjs, ".cjs"},
};

// Strings — symbol_display.go:127
collections::Set<std::string> ScriptElementKindModifierStrings(ScriptElementKindModifier m) {
	collections::Set<std::string> result;
	for (const auto& entry : scriptElementKindModifierNames) {
		if ((m & entry.flag) != ScriptElementKindModifierNone) {
			result.Add(entry.name);
		}
	}
	return result;
}

// GetSymbolKind — symbol_display.go:151
ScriptElementKind GetSymbolKind(checker::Checker* typeChecker, Symbol* symbol, Node* location) {
	ScriptElementKind result = getSymbolKindOfConstructorPropertyMethodAccessorFunctionOrVar(typeChecker, symbol, location);
	if (result != ScriptElementKindUnknown) {
		return result;
	}
	SymbolFlags flags = symbol->combinedLocalAndExportSymbolFlags();
	if ((flags & SymbolFlagsClass) != 0) {
		Node* decl = getDeclarationOfKind(symbol, Kind::ClassExpression);
		if (decl != nullptr) {
			return ScriptElementKindLocalClassElement;
		}
		return ScriptElementKindClassElement;
	}
	if ((flags & SymbolFlagsEnum) != 0) {
		return ScriptElementKindEnumElement;
	}
	if ((flags & SymbolFlagsTypeAlias) != 0) {
		return ScriptElementKindTypeElement;
	}
	if ((flags & SymbolFlagsInterface) != 0) {
		return ScriptElementKindInterfaceElement;
	}
	if ((flags & SymbolFlagsTypeParameter) != 0) {
		return ScriptElementKindTypeParameterElement;
	}
	if ((flags & SymbolFlagsEnumMember) != 0) {
		return ScriptElementKindEnumMemberElement;
	}
	if ((flags & SymbolFlagsAlias) != 0) {
		return ScriptElementKindAlias;
	}
	if ((flags & SymbolFlagsModule) != 0) {
		return ScriptElementKindModuleElement;
	}

	return ScriptElementKindUnknown;
}

// getSymbolKindOfConstructorPropertyMethodAccessorFunctionOrVar —
// symbol_display.go:190
ScriptElementKind getSymbolKindOfConstructorPropertyMethodAccessorFunctionOrVar(
	checker::Checker* typeChecker, Symbol* symbol, Node* location) {
	std::vector<Symbol*> roots;
	if (typeChecker != nullptr) {
		roots = typeChecker->GetRootSymbols(symbol);
	} else {
		roots = {symbol};
	}

	// If this is a method from a mapped type, leave as a method so long as it still has a call signature, as opposed to e.g.
	// `{ [K in keyof I]: number }`.
	if (roots.size() == 1 &&
		(roots[0]->flags & SymbolFlagsMethod) != 0 &&
		(typeChecker == nullptr ||
		 !typeChecker->GetCallSignatures(
			 typeChecker->GetNonNullableType(
				 typeChecker->GetTypeOfSymbolAtLocation(symbol, location))).empty())) {
		return ScriptElementKindMemberFunctionElement;
	}

	if (typeChecker != nullptr) {
		if (typeChecker->IsUndefinedSymbol(symbol)) {
			return ScriptElementKindVariableElement;
		}
		if (typeChecker->IsArgumentsSymbol(symbol)) {
			return ScriptElementKindLocalVariableElement;
		}
		if ((location->kind == Kind::ThisKeyword && isExpression(location)) ||
			isThisInTypeQuery(location)) {
			return ScriptElementKindParameterElement;
		}
	}

	SymbolFlags flags = symbol->combinedLocalAndExportSymbolFlags();
	if ((flags & SymbolFlagsVariable) != 0) {
		if (isFirstDeclarationOfSymbolParameter(symbol)) {
			return ScriptElementKindParameterElement;
		} else if (symbol->data->valueDeclaration != nullptr && isVarConst(symbol->data->valueDeclaration)) {
			return ScriptElementKindConstElement;
		} else if (symbol->data->valueDeclaration != nullptr && isVarUsing(symbol->data->valueDeclaration)) {
			return ScriptElementKindVariableUsingElement;
		} else if (symbol->data->valueDeclaration != nullptr && isVarAwaitUsing(symbol->data->valueDeclaration)) {
			return ScriptElementKindVariableAwaitUsingElement;
		} else if (some(symbol->data->declarations, detail::isLet)) {
			return ScriptElementKindLetElement;
		}
		if (isLocalVariableOrFunction(symbol)) {
			return ScriptElementKindLocalVariableElement;
		}
		return ScriptElementKindVariableElement;
	}
	if ((flags & SymbolFlagsFunction) != 0) {
		if (isLocalVariableOrFunction(symbol)) {
			return ScriptElementKindLocalFunctionElement;
		}
		return ScriptElementKindFunctionElement;
	}
	// FIXME: getter and setter use the same symbol. And it is rare to use only setter without getter, so in most cases the symbol always has getter flag.
	// So, even when the location is just on the declaration of setter, this function returns getter.
	if ((flags & SymbolFlagsGetAccessor) != 0) {
		return ScriptElementKindMemberGetAccessorElement;
	}
	if ((flags & SymbolFlagsSetAccessor) != 0) {
		return ScriptElementKindMemberSetAccessorElement;
	}
	if ((flags & SymbolFlagsMethod) != 0) {
		return ScriptElementKindMemberFunctionElement;
	}
	if ((flags & SymbolFlagsConstructor) != 0) {
		return ScriptElementKindConstructorImplementationElement;
	}
	if ((flags & SymbolFlagsSignature) != 0) {
		return ScriptElementKindIndexSignatureElement;
	}

	if ((flags & SymbolFlagsProperty) != 0) {
		if (typeChecker != nullptr && (flags & SymbolFlagsTransient) != 0 &&
			(symbol->checkFlags & CheckFlagsSynthetic) != 0) {
			// If union property is result of union of non method (property/accessors/variables), it is labeled as property
			ScriptElementKind unionPropertyKind = ScriptElementKindUnknown;
			for (Symbol* rootSymbol : roots) {
				if ((rootSymbol->flags & (SymbolFlagsProperty | SymbolFlagsAccessor | SymbolFlagsVariable)) != 0) {
					unionPropertyKind = ScriptElementKindMemberVariableElement;
					break;
				}
			}
			if (unionPropertyKind == ScriptElementKindUnknown) {
				// If this was union of all methods,
				// make sure it has call signatures before we can label it as method.
				checker::Type* typeOfUnionProperty = typeChecker->GetTypeOfSymbolAtLocation(symbol, location);
				if (!typeChecker->GetCallSignatures(typeOfUnionProperty).empty()) {
					return ScriptElementKindMemberFunctionElement;
				}
				return ScriptElementKindMemberVariableElement;
			}
			return unionPropertyKind;
		}

		return ScriptElementKindMemberVariableElement;
	}

	return ScriptElementKindUnknown;
}

// isFirstDeclarationOfSymbolParameter — symbol_display.go:291
static bool isFirstDeclarationOfSymbolParameter(Symbol* symbol) {
	Node* declaration = nullptr;
	if (!symbol->data->declarations.empty()) {
		declaration = symbol->data->declarations[0];
	}
	Node* result = findAncestorOrQuit(declaration, [](Node* n) -> FindAncestorResult {
		if (n->kind == Kind::Parameter) {
			return FindAncestorResult::True;
		}
		if (n->kind == Kind::BindingElement || n->kind == Kind::ObjectBindingPattern ||
			n->kind == Kind::ArrayBindingPattern) {
			return FindAncestorResult::False;
		}
		return FindAncestorResult::Quit;
	});

	return result != nullptr;
}

// isLocalVariableOrFunction — symbol_display.go:307
static bool isLocalVariableOrFunction(Symbol* symbol) {
	if (symbol->data->parent != nullptr) {
		return false; // This is exported symbol
	}

	for (Node* decl : symbol->data->declarations) {
		// Function expressions are local
		if (decl->kind == Kind::FunctionExpression) {
			return true;
		}

		if (decl->kind != Kind::VariableDeclaration && decl->kind != Kind::FunctionDeclaration) {
			continue;
		}

		// If the parent is not source file or module block, it is a local variable.
		Node* parent = decl->parent;
		for (; !detail::isFunctionBlock(parent); parent = parent->parent) {
			// Reached source file or module block
			if (parent->kind == Kind::SourceFile || parent->kind == Kind::ModuleBlock) {
				break;
			}
		}

		if (detail::isFunctionBlock(parent)) {
			// Parent is in function block.
			return true;
		}
	}
	return false;
}

// GetSymbolModifiers — symbol_display.go:334
ScriptElementKindModifier GetSymbolModifiers(checker::Checker* typeChecker, Symbol* symbol) {
	if (symbol == nullptr) {
		return ScriptElementKindModifierNone;
	}

	ScriptElementKindModifier modifiers = getNormalizedSymbolModifiers(typeChecker, symbol);
	if ((symbol->flags & SymbolFlagsAlias) != 0 && typeChecker != nullptr) {
		Symbol* resolvedSymbol = typeChecker->GetAliasedSymbol(symbol);
		if (resolvedSymbol != symbol) {
			modifiers |= getNormalizedSymbolModifiers(typeChecker, resolvedSymbol);
		}
	}
	if ((symbol->flags & SymbolFlagsOptional) != 0) {
		modifiers |= ScriptElementKindModifierOptional;
	}

	return modifiers;
}

// getNormalizedSymbolModifiers — symbol_display.go:351
static ScriptElementKindModifier getNormalizedSymbolModifiers(checker::Checker* typeChecker, Symbol* symbol) {
	ScriptElementKindModifier modifierSet = ScriptElementKindModifierNone;
	if (!symbol->data->declarations.empty()) {
		Node* declaration = symbol->data->declarations[0];
		std::vector<Node*> declarations(symbol->data->declarations.begin() + 1, symbol->data->declarations.end());
		// omit deprecated flag if some declarations are not deprecated
		ModifierFlags excludeFlags;
		if (!declarations.empty() &&
			isDeprecatedDeclaration(typeChecker, declaration) && // !!! include jsdoc node flags
			some(declarations, [typeChecker](Node* d) { return !isDeprecatedDeclaration(typeChecker, d); })) {
			excludeFlags = ModifierFlagsDeprecated;
		} else {
			excludeFlags = ModifierFlagsNone;
		}
		modifierSet = getNodeModifiers(typeChecker, declaration, excludeFlags);
	}

	return modifierSet;
}

// isDeprecatedDeclaration — symbol_display.go:370
static bool isDeprecatedDeclaration(checker::Checker* typeChecker, Node* declaration) {
	if (typeChecker != nullptr) {
		return typeChecker->IsDeprecatedDeclaration(declaration);
	}
	return detail::isDeprecatedDeclaration(declaration);
}

// getNodeModifiers — symbol_display.go:378
static ScriptElementKindModifier getNodeModifiers(checker::Checker* typeChecker, Node* node, ModifierFlags excludeFlags) {
	ScriptElementKindModifier result = ScriptElementKindModifierNone;
	ModifierFlags flags = ModifierFlagsNone;
	if (isDeclaration(node)) {
		flags = getCombinedModifierFlags(node);
		if (isDeprecatedDeclaration(typeChecker, node)) {
			flags |= ModifierFlagsDeprecated;
		}
		flags &= ~excludeFlags;
	}

	if ((flags & ModifierFlagsPrivate) != 0) {
		result |= ScriptElementKindModifierPrivate;
	}
	if ((flags & ModifierFlagsProtected) != 0) {
		result |= ScriptElementKindModifierProtected;
	}
	if ((flags & ModifierFlagsPublic) != 0) {
		result |= ScriptElementKindModifierPublic;
	}
	if ((flags & ModifierFlagsStatic) != 0) {
		result |= ScriptElementKindModifierStatic;
	}
	if ((flags & ModifierFlagsAbstract) != 0) {
		result |= ScriptElementKindModifierAbstract;
	}
	if ((flags & ModifierFlagsExport) != 0) {
		result |= ScriptElementKindModifierExported;
	}
	if ((flags & ModifierFlagsDeprecated) != 0) {
		result |= ScriptElementKindModifierDeprecated;
	}
	if ((flags & ModifierFlagsAmbient) != 0) {
		result |= ScriptElementKindModifierAmbient;
	}
	if ((node->flags & NodeFlagsAmbient) != 0) {
		result |= ScriptElementKindModifierAmbient;
	}
	if (node->kind == Kind::ExportAssignment) {
		result |= ScriptElementKindModifierExported;
	}

	return result;
}

} // namespace tsc::ls::lsutil
