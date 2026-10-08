// Port of tsc/internal/transformers/declarations/util.go
#include "internal/transformers/declarations/declarations.h"

namespace tsc::transformers::declarations {

// util.go:9 needsScopeMarker
bool needsScopeMarker(Node* result) {
	return !isAnyImportOrReExport(result) && !isExportAssignment(result) &&
	       !hasSyntacticModifier(result, ModifierFlagsExport) &&
	       !isAmbientModule(result);
}

// util.go:13 canHaveLiteralInitializer
bool canHaveLiteralInitializer(printer::EmitResolver* resolver, Node* node) {
	switch (node->kind) {
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
		return resolver->GetEffectiveDeclarationFlags(
		           node, ModifierFlagsPrivate) == 0;
	case Kind::Parameter:
	case Kind::VariableDeclaration:
		return true;
	}
	return false;
}

// util.go:25 canProduceDiagnostics
bool canProduceDiagnostics(Node* node) {
	return isVariableDeclaration(node) ||
	       isPropertyDeclaration(node) ||
	       isPropertySignatureDeclaration(node) ||
	       isBindingElement(node) ||
	       isSetAccessorDeclaration(node) ||
	       isGetAccessorDeclaration(node) ||
	       isConstructSignatureDeclaration(node) ||
	       isCallSignatureDeclaration(node) ||
	       isMethodDeclaration(node) ||
	       isMethodSignatureDeclaration(node) ||
	       isFunctionDeclaration(node) ||
	       isParameterDeclaration(node) ||
	       isTypeParameterDeclaration(node) ||
	       isExpressionWithTypeArguments(node) ||
	       isImportEqualsDeclaration(node) ||
	       isTypeAliasDeclaration(node) ||
	       isJSTypeAliasDeclaration(node) ||
	       isConstructorDeclaration(node) ||
	       isIndexSignatureDeclaration(node) ||
	       isPropertyAccessExpression(node) ||
	       isElementAccessExpression(node) ||
	       isBinaryExpression(node) ||
	       isCallExpression(node);  // || // !!! TODO: JSDoc support
	/* isJSDocTypeAlias(node); */
}

// util.go:52 canReuseModifierNodes
bool canReuseModifierNodes(const std::vector<Node*>& nodes) {
	for (Node* node : nodes) {
		if (isModifier(node) && (node->flags & NodeFlagsReparsed) != 0) {
			return false;
		}
	}
	return true;
}

// util.go:61 isDeclarationAndNotVisible
bool isDeclarationAndNotVisible(printer::EmitContext* emitContext,
                                printer::EmitResolver* resolver, Node* node) {
	node = emitContext->parseNode(node);
	switch (node->kind) {
	case Kind::FunctionDeclaration:
	case Kind::ModuleDeclaration:
	case Kind::InterfaceDeclaration:
	case Kind::ClassDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::EnumDeclaration:
		return !resolver->IsDeclarationVisible(node);
	// The following should be doing their own visibility checks based on filtering their members
	case Kind::VariableDeclaration:
		return !getBindingNameVisible(resolver, node);
	case Kind::ImportEqualsDeclaration:
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::ExportDeclaration:
	case Kind::ExportAssignment:
		return false;
	case Kind::ClassStaticBlockDeclaration:
		return true;
	}
	return false;
}

// util.go:87 getBindingNameVisible
bool getBindingNameVisible(printer::EmitResolver* resolver, Node* elem) {
	if (isOmittedExpression(elem)) {
		return false;
	}
	// TODO: parseArrayBindingElement _never_ parses out an OmittedExpression anymore, instead producing a nameless binding element
	// Audit if OmittedExpression should be removed
	if (elem->name() == nullptr) {
		return false;
	}
	if (isBindingPattern(elem->name())) {
		// If any child binding pattern element has been marked visible (usually by collect linked aliases), then this is visible
		for (Node* elem : elem->name()->elements()) {
			if (getBindingNameVisible(resolver, elem)) {
				return true;
			}
		}
		return false;
	} else {
		return resolver->IsDeclarationVisible(elem);
	}
}

// util.go:109 isEnclosingDeclaration
bool isEnclosingDeclaration(Node* node) {
	return isSourceFile(node) ||
	       isTypeAliasDeclaration(node) ||
	       isJSTypeAliasDeclaration(node) ||
	       isModuleDeclaration(node) ||
	       isClassDeclaration(node) ||
	       isInterfaceDeclaration(node) ||
	       isFunctionLike(node) ||
	       isIndexSignatureDeclaration(node) ||
	       isMappedTypeNode(node) ||
	       isVariableDeclaration(node);
}

// util.go:122 isAlwaysType
bool isAlwaysType(Node* node) {
	if (node->kind == Kind::InterfaceDeclaration) {
		return true;
	}
	return false;
}

// util.go:129 maskModifierFlags
ModifierFlags maskModifierFlags(Node* node, ModifierFlags modifierMask,
                                ModifierFlags modifierAdditions) {
	ModifierFlags flags =
	    (getCombinedModifierFlags(node) & modifierMask) | modifierAdditions;
	if ((flags & ModifierFlagsDefault) != 0 &&
	    (flags & ModifierFlagsExport) == 0) {
		// A non-exported default is a nonsequitor - we usually try to remove all export modifiers
		// from statements in ambient declarations; but a default export must retain its export modifier to be syntactically valid
		flags ^= ModifierFlagsExport;
	}
	if ((flags & ModifierFlagsDefault) != 0 &&
	    (flags & ModifierFlagsAmbient) != 0) {
		flags ^= ModifierFlagsAmbient;  // `declare` is never required alongside `default` (and would be an error if printed)
	}
	return flags;
}

// util.go:142 unwrapParenthesizedExpression
Node* unwrapParenthesizedExpression(Node* o) {
	while (o->kind == Kind::ParenthesizedExpression) {
		o = o->expression();
	}
	return o;
}

// util.go:149 isPrivateMethodTypeParameter
bool isPrivateMethodTypeParameter(printer::EmitResolver* resolver,
                                  TypeParameterDeclaration* node) {
	return node->asNode()->parent->kind == Kind::MethodDeclaration &&
	       resolver->GetEffectiveDeclarationFlags(node->asNode()->parent,
	                                          ModifierFlagsPrivate) != 0;
}

// util.go:153 shouldEmitFunctionProperties
// Returns true if expando properties should be emitted for this function.
// Properties are emitted if any overload in the symbol has a body
// (implementation).
bool shouldEmitFunctionProperties(FunctionDeclaration* input) {
	if (input->Body != nullptr) {
		return true;
	}
	// core.Every
	for (Node* decl : input->Symbol->declarations) {
		if (isFunctionDeclaration(decl) &&
		    decl->as<FunctionDeclaration>()->Body != nullptr) {
			return true;
		}
	}
	return false;
}

// util.go:164 getEffectiveBaseTypeNode
Node* getEffectiveBaseTypeNode(Node* node) {
	Node* baseType = getClassExtendsHeritageElement(node);
	// !!! TODO: JSDoc support
	// if (baseType && isInJSFile(node)) {
	//     // Prefer an @augments tag because it may have type parameters.
	//     const tag = getJSDocAugmentsTag(node);
	//     if (tag) {
	//         return tag.class;
	//     }
	// }
	return baseType;
}

// util.go:177 isScopeMarker
bool isScopeMarker(Node* node) {
	return isExportAssignment(node) || isExportDeclaration(node);
}

// util.go:181 hasScopeMarker
bool hasScopeMarker(NodeList* statements) {
	if (statements == nullptr) {
		return false;
	}
	// core.Some
	for (Node* node : statements->nodes) {
		if (isScopeMarker(node)) {
			return true;
		}
	}
	return false;
}

}  // namespace tsc::transformers::declarations
