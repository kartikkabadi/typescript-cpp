// codeactions_missingmemberfixer.go — shared helper that synthesizes missing
// class members (properties, accessors, methods, index signatures) for the
// "implement interface" / "declare missing member" quickfixes.
#include "internal/ls/ls.h"

#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/locale/locale.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/ls/change/change.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/nodebuilder/types.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tsc::ls {

namespace {

// core.FirstOrNil
template <typename T>
T* firstOrNil(const std::vector<T*>& v) {
	return v.empty() ? nullptr : v[0];
}

// core.LastOrNil
template <typename T>
T* lastOrNil(const std::vector<T*>& v) {
	return v.empty() ? nullptr : v.back();
}

// core.OrElse — returns the first non-nil value.
inline Node* orElse(Node* a, Node* b) {
	return a != nullptr ? a : b;
}

// core.FlatMap
template <typename T, typename F>
auto flatMap(const std::vector<T>& v, F&& f)
    -> std::vector<std::decay_t<decltype(*f(v[0]).data())>> {
	std::vector<std::decay_t<decltype(*f(v[0]).data())>> out;
	for (auto& e : v) {
		auto part = f(e);
		out.insert(out.end(), part.begin(), part.end());
	}
	return out;
}

// getSetAccessorValueParameter — checker/utilities.go:1914 (checker-internal in
// the C++ port; file-local replica).
Node* getSetAccessorValueParameter(Node* accessor) {
	auto parameters = accessor->parameters();
	if (!parameters.empty()) {
		bool hasThis = parameters.size() == 2 && isThisParameter(parameters[0]);
		return parameters[hasThis ? 1 : 0];
	}
	return nullptr;
}

// createModifiersFromModifierFlags — ast/utilities.go:3291 (not yet ported in
// the C++ ast package; file-local replica).
std::vector<Node*> createModifiersFromModifierFlags(
    ModifierFlags flags, NodeFactory& f) {
	auto newModifier = [&f](Kind kind) -> Node* { return f.newToken(kind); };
	std::vector<Node*> result;
	if (flags & ModifierFlagsExport) {
		result.push_back(newModifier(Kind::ExportKeyword));
	}
	if (flags & ModifierFlagsAmbient) {
		result.push_back(newModifier(Kind::DeclareKeyword));
	}
	if (flags & ModifierFlagsDefault) {
		result.push_back(newModifier(Kind::DefaultKeyword));
	}
	if (flags & ModifierFlagsConst) {
		result.push_back(newModifier(Kind::ConstKeyword));
	}
	if (flags & ModifierFlagsPublic) {
		result.push_back(newModifier(Kind::PublicKeyword));
	}
	if (flags & ModifierFlagsPrivate) {
		result.push_back(newModifier(Kind::PrivateKeyword));
	}
	if (flags & ModifierFlagsProtected) {
		result.push_back(newModifier(Kind::ProtectedKeyword));
	}
	if (flags & ModifierFlagsAbstract) {
		result.push_back(newModifier(Kind::AbstractKeyword));
	}
	if (flags & ModifierFlagsStatic) {
		result.push_back(newModifier(Kind::StaticKeyword));
	}
	if (flags & ModifierFlagsOverride) {
		result.push_back(newModifier(Kind::OverrideKeyword));
	}
	if (flags & ModifierFlagsReadonly) {
		result.push_back(newModifier(Kind::ReadonlyKeyword));
	}
	if (flags & ModifierFlagsAccessor) {
		result.push_back(newModifier(Kind::AccessorKeyword));
	}
	if (flags & ModifierFlagsAsync) {
		result.push_back(newModifier(Kind::AsyncKeyword));
	}
	if (flags & ModifierFlagsIn) {
		result.push_back(newModifier(Kind::InKeyword));
	}
	if (flags & ModifierFlagsOut) {
		result.push_back(newModifier(Kind::OutKeyword));
	}
	return result;
}

// createDummyParameters — codeactions_missingmemberfixer.go:451.
NodeList* createDummyParameters(NodeFactory* factory, int argCount,
                                const std::vector<std::string>& names,
                                const std::vector<Node*>& types,
                                int minArgumentCount, bool inJS) {
	std::vector<Node*> parameters;
	parameters.reserve(argCount);
	std::unordered_map<std::string, int> parameterNameCounts;

	for (int i = 0; i < argCount; i++) {
		std::string parameterName;
		if (i < (int)names.size() && names[i] != "") {
			parameterName = names[i];
		} else {
			parameterName = "arg" + std::to_string(i);
		}

		int count = parameterNameCounts[parameterName];
		parameterNameCounts[parameterName] = count + 1;

		if (count > 0) {
			parameterName += std::to_string(count);
		}

		Node* questionToken = nullptr;
		if (i >= minArgumentCount) {
			questionToken = factory->newToken(Kind::QuestionToken);
		}

		Node* typeNode = nullptr;
		if (inJS) {
			typeNode = nullptr;
		} else if (i < (int)types.size() && types[i] != nullptr) {
			typeNode = types[i];
		} else {
			typeNode = factory->newKeywordTypeNode(Kind::UnknownKeyword);
		}
		parameters.push_back(factory->newParameterDeclaration(
		    nullptr /*modifiers*/, nullptr /*dotDotDotToken*/,
		    factory->newIdentifier(parameterName), questionToken, typeNode,
		    nullptr /*initializer*/));
	}
	return factory->newNodeList(std::move(parameters));
}

// createDeclarationName — codeactions_missingmemberfixer.go:489.
Node* createDeclarationName(NodeFactory* factory, checker::Checker* typeChecker,
                            Symbol* symbol, Node* declaration) {
	if (symbol != nullptr &&
	    (symbol->checkFlags & CheckFlagsMapped) != 0) {
		auto* nameType = typeChecker->GetNameTypeOfSymbol(symbol);
		if (nameType != nullptr &&
		    checker::IsTypeUsableAsPropertyName(nameType)) {
			return factory->newIdentifier(
			    checker::GetPropertyNameFromType(nameType));
		}
	}
	if (declaration != nullptr && declaration->name() != nullptr) {
		return declaration->name()->clone(*factory);
	}
	if (symbol != nullptr) {
		return factory->newIdentifier(symbol->name);
	}
	return nullptr;
}

// createPropertyName — codeactions_missingmemberfixer.go:505.
Node* createPropertyName(NodeFactory* factory, Node* node,
                         lsutil::QuotePreference quotePreference) {
	if (isIdentifier(node) && node->text() == "constructor") {
		TokenFlags tokenFlags = TokenFlagsNone;
		if (quotePreference == lsutil::QuotePreferenceSingle) {
			tokenFlags = TokenFlagsSingleQuote;
		}
		return factory->newComputedPropertyName(
		    factory->newStringLiteral(node->text(), tokenFlags));
	}
	return deepCloneNode(*factory, node);
}

} // namespace

// newMissingMemberFixer — codeactions_missingmemberfixer.go:35.
missingMemberFixer* newMissingMemberFixer(
    change::Tracker* changeTracker, compiler::SimpleProgram* program,
    checker::Checker* typeChecker, const lsutil::UserPreferences& preferences,
    autoimport::ImportAdder* importAdder, const locale::Locale& loc) {
	return new missingMemberFixer{
	    .changeTracker = changeTracker,
	    .typeChecker = typeChecker,
	    .program = program,
	    .preferences = preferences,
	    .importAdder = importAdder,
	    .loc = loc,
	};
}

// createNodeBuilder — codeactions_missingmemberfixer.go:46.
std::pair<checker::NodeBuilder*, std::unordered_map<Node*, Symbol*>>
missingMemberFixer::createNodeBuilder() {
	auto idToSymbol = std::unordered_map<Node*, Symbol*>();
	auto* nodeBuilder = checker::NewNodeBuilderEx(
	    typeChecker, changeTracker->emitContext, &idToSymbol);
	return {nodeBuilder, idToSymbol};
}

// createMemberFromSymbol — codeactions_missingmemberfixer.go:52.
std::vector<Node*> missingMemberFixer::createMemberFromSymbol(
    Symbol* symbol, Node* enclosingDeclaration, SourceFile* sourceFile,
    Node* body, preserveOptionalFlags preserveOptional, bool abstract) {
	auto declarations = symbol->declarations;
	auto* declaration = firstOrNil(declarations);

	auto quotePreference =
	    lsutil::GetQuotePreference(sourceFile, preferences);
	bool ambient = (enclosingDeclaration->flags & NodeFlagsAmbient) != 0;
	bool signatureOnly = ambient || abstract;
	bool optional = (symbol->flags & SymbolFlagsOptional) != 0;
	Kind kind = Kind::PropertySignature;
	if (declaration != nullptr) {
		kind = declaration->kind;
	}
	auto* declarationName = createDeclarationName(
	    changeTracker->nodeFactory, typeChecker, symbol, declaration);
	auto* modifiers = createModifiers(symbol, declaration);

	nodebuilder::Flags flags = nodebuilder::FlagsNoTruncation;
	if (quotePreference == lsutil::QuotePreferenceSingle) {
		flags |= nodebuilder::FlagsUseSingleQuotesForStringLiteralType;
	}

	auto* t = typeChecker->GetWidenedType(
	    typeChecker->GetTypeOfSymbolAtLocation(symbol, enclosingDeclaration));
	std::vector<Node*> nodes;

	switch (kind) {
	case Kind::PropertySignature:
	case Kind::PropertyDeclaration: {
		auto [nodeBuilder, idToSymbol] = createNodeBuilder();
		auto* typeNode =
		    createTypeNode(t, enclosingDeclaration, flags, nodeBuilder,
		                   &idToSymbol);
		Node* questionToken = nullptr;
		if (optional &&
		    (preserveOptional & preserveOptionalFlagsProperty) != 0) {
			questionToken =
			    changeTracker->nodeFactory->newToken(Kind::QuestionToken);
		}
		nodes.push_back(changeTracker->nodeFactory->newPropertyDeclaration(
		    modifiers,
		    createPropertyName(changeTracker->nodeFactory, declarationName,
		                       quotePreference),
		    questionToken, typeNode, nullptr /*initializer*/));
		return nodes;
	}

	case Kind::GetAccessor:
	case Kind::SetAccessor: {
		auto [nodeBuilder, idToSymbol] = createNodeBuilder();
		auto accessors =
		    getAllAccessorDeclarations(symbol->declarations, declaration);
		std::vector<Node*> orderedAccessors;
		if (accessors.secondAccessor == nullptr) {
			orderedAccessors.push_back(accessors.firstAccessor);
		} else {
			orderedAccessors.push_back(accessors.firstAccessor);
			orderedAccessors.push_back(accessors.secondAccessor);
		}

		for (auto* accessor : orderedAccessors) {
			if (isGetAccessorDeclaration(accessor)) {
				nodes.push_back(
				    changeTracker->nodeFactory->newGetAccessorDeclaration(
				        modifiers,
				        createPropertyName(changeTracker->nodeFactory,
				                           declarationName,
				                           quotePreference),
				        nullptr /*typeParameters*/,
				        nullptr /*parameters*/,
				        createTypeNode(t, enclosingDeclaration, flags,
				                       nodeBuilder, &idToSymbol),
				        nullptr /*fullSignature*/,
				        createBody(body, quotePreference, signatureOnly)));
			}

			if (isSetAccessorDeclaration(accessor)) {
				auto* parameter = getSetAccessorValueParameter(accessor);
				if (parameter == nullptr) {
					TSC_UNREACHABLE(
					    "Expected set accessor to have a parameter.");
				}

				nodes.push_back(
				    changeTracker->nodeFactory->newSetAccessorDeclaration(
				        modifiers,
				        createPropertyName(changeTracker->nodeFactory,
				                           declarationName,
				                           quotePreference),
				        nullptr /*typeParameters*/,
				        createDummyParameters(
				            changeTracker->nodeFactory, 1,
				            {parameter->name()->text()},
				            {createTypeNode(t, enclosingDeclaration, flags,
				                            nodeBuilder, &idToSymbol)},
				            1, isInJSFile(enclosingDeclaration)),
				        nullptr /*type*/, nullptr /*fullSignature*/,
				        createBody(body, quotePreference, signatureOnly)));
			}
		}
		return nodes;
	}

	case Kind::MethodSignature:
	case Kind::MethodDeclaration: {
		auto signatures = getCallSignatures(t);
		bool preserveOptionalFlag =
		    optional &&
		    (preserveOptional & preserveOptionalFlagsMethod) != 0;
		if (signatures.empty()) {
			return {};
		}

		if (declarations.size() == 1) {
			auto* method = createSignatureDeclarationFromSignature(
			    firstOrNil(signatures), Kind::MethodDeclaration, sourceFile,
			    enclosingDeclaration,
			    createBody(body, quotePreference, signatureOnly), modifiers,
			    declarationName, preserveOptionalFlag);
			if (method != nullptr) {
				nodes.push_back(method);
			}
			return nodes;
		}

		for (auto* signature : signatures) {
			if (signature->declaration != nullptr &&
			    (signature->declaration->flags & NodeFlagsAmbient) != 0) {
				continue;
			}

			auto* method = createSignatureDeclarationFromSignature(
			    signature, Kind::MethodDeclaration, sourceFile,
			    enclosingDeclaration, nullptr /*body*/, modifiers,
			    declarationName, preserveOptionalFlag);
			if (method != nullptr) {
				nodes.push_back(method);
			}
		}

		if (signatureOnly) {
			return nodes;
		}

		if (declarations.size() > signatures.size()) {
			auto* signature = typeChecker->GetSignatureFromDeclaration(
			    lastOrNil(declarations));
			auto* method = createSignatureDeclarationFromSignature(
			    signature, Kind::MethodDeclaration, sourceFile,
			    enclosingDeclaration,
			    createBody(body, quotePreference,
			               false /*signatureOnly*/),
			    modifiers, declarationName, preserveOptionalFlag);
			if (method != nullptr) {
				nodes.push_back(method);
			}
		} else {
			auto* method = createSignatureDeclarationFromSignatures(
			    signatures, declarationName, preserveOptionalFlag,
			    modifiers, quotePreference, body, enclosingDeclaration);
			if (method != nullptr) {
				nodes.push_back(method);
			}
		}

		return nodes;
	}
	default: {
	}
	}
	return {};
}

// getCallSignatures — codeactions_missingmemberfixer.go:170.
std::vector<checker::Signature*> missingMemberFixer::getCallSignatures(
    checker::Type* t) {
	if (t->IsUnion()) {
		std::vector<checker::Signature*> out;
		for (auto* u : t->types()) {
			auto part = typeChecker->GetCallSignatures(u);
			out.insert(out.end(), part.begin(), part.end());
		}
		return out;
	}
	return typeChecker->GetCallSignatures(t);
}

// createTypeNode — codeactions_missingmemberfixer.go:177.
Node* missingMemberFixer::createTypeNode(
    checker::Type* t, Node* enclosingDeclaration, nodebuilder::Flags flags,
    checker::NodeBuilder* nodeBuilder,
    std::unordered_map<Node*, Symbol*>* idToSymbol) {
	return importTypeNode(
	    nodeBuilder->TypeToTypeNode(t, enclosingDeclaration, flags,
	                                nodebuilder::InternalFlagsNone,
	                                nullptr /*tracker*/),
	    idToSymbol);
}

// createModifiers — codeactions_missingmemberfixer.go:181.
ModifierList* missingMemberFixer::createModifiers(Symbol* symbol,
                                                Node* declaration) {
	ModifierFlags modifierFlags = ModifierFlagsNone;
	if (declaration != nullptr) {
		auto effective =
		    checker::GetDeclarationModifierFlagsFromSymbol(symbol);
		modifierFlags = effective & ModifierFlagsStatic;
		if (effective & ModifierFlagsPublic) {
			modifierFlags |= ModifierFlagsPublic;
		} else if (effective & ModifierFlagsProtected) {
			modifierFlags |= ModifierFlagsProtected;
		}
		if (isAutoAccessorPropertyDeclaration(declaration)) {
			modifierFlags |= ModifierFlagsAccessor;
		}
	}
	if (shouldAddOverrideKeyword(declaration)) {
		modifierFlags |= ModifierFlagsOverride;
	}
	if (modifierFlags == ModifierFlagsNone) {
		return nullptr;
	}
	return changeTracker->nodeFactory->newModifierList(
	    createModifiersFromModifierFlags(modifierFlags,
	                                     *changeTracker->nodeFactory));
}

// shouldAddOverrideKeyword — codeactions_missingmemberfixer.go:204.
bool missingMemberFixer::shouldAddOverrideKeyword(Node* declaration) {
	return declaration != nullptr &&
	       tristateIsTrue(program->Options()->NoImplicitOverride) &&
	       hasAbstractModifier(declaration);
}

// createSignatureDeclarationFromSignature —
// codeactions_missingmemberfixer.go:208.
Node* missingMemberFixer::createSignatureDeclarationFromSignature(
    checker::Signature* signature, Kind kind, SourceFile* sourceFile,
    Node* enclosingDeclaration, Node* body, ModifierList* modifiers,
    Node* name, bool optional) {
	auto quotePreference =
	    lsutil::GetQuotePreference(sourceFile, preferences);
	nodebuilder::Flags flags = nodebuilder::FlagsNoTruncation |
	                           nodebuilder::FlagsSuppressAnyReturnType |
	                           nodebuilder::FlagsAllowEmptyTuple;
	if (quotePreference == lsutil::QuotePreferenceSingle) {
		flags |= nodebuilder::FlagsUseSingleQuotesForStringLiteralType;
	}

	auto [nodeBuilder, idToSymbol] = createNodeBuilder();
	auto* signatureDeclaration =
	    nodeBuilder->SignatureToSignatureDeclaration(
	        signature, kind, enclosingDeclaration, flags,
	        nodebuilder::InternalFlagsAllowUnresolvedNames,
	        nullptr /*tracker*/);
	if (signatureDeclaration == nullptr) {
		return nullptr;
	}

	bool isJS = isInJSFile(enclosingDeclaration);
	auto* parameters = signatureDeclaration->parameterList();
	auto* typeParameters =
	    ifElse(isJS, (NodeList*)nullptr,
	           signatureDeclaration->typeParameterList());
	auto* typeNode =
	    ifElse(isJS, (Node*)nullptr, signatureDeclaration->type());

	if (typeParameters != nullptr && !typeParameters->nodes.empty()) {
		std::vector<Node*> nodes;
		nodes.reserve(typeParameters->nodes.size());
		for (auto* tp : typeParameters->nodes) {
			if (tp == nullptr) {
				continue;
			}

			if (isTypeParameterDeclaration(tp)) {
				auto* typeParameter = tp->as<TypeParameterDeclaration>();

				Node* constraint = typeParameter->Constraint;
				if (constraint != nullptr) {
					constraint =
					    importTypeNode(constraint, &idToSymbol);
				}

				Node* defaultType = typeParameter->DefaultType;
				if (defaultType != nullptr) {
					defaultType =
					    importTypeNode(defaultType, &idToSymbol);
				}

				nodes.push_back(
				    changeTracker->nodeFactory
				        ->updateTypeParameterDeclaration(
				            typeParameter,
				            typeParameter->Node::modifiers(),
				            typeParameter->Node::name(), constraint,
				            typeParameter->Expression, defaultType));
			} else {
				nodes.push_back(tp);
			}
		}
		typeParameters = changeTracker->nodeFactory->newNodeList(nodes);
	}

	if (parameters != nullptr) {
		std::vector<Node*> nodes;
		nodes.reserve(parameters->nodes.size());
		for (auto* p : parameters->nodes) {
			if (p == nullptr) {
				continue;
			}

			auto* parameter = p->as<ParameterDeclaration>();
			Node* parameterTypeNode = parameter->Type;
			if (parameterTypeNode != nullptr) {
				parameterTypeNode =
				    importTypeNode(parameterTypeNode, &idToSymbol);
			}

			nodes.push_back(
			    changeTracker->nodeFactory->updateParameterDeclaration(
			        parameter, parameter->Node::modifiers(),
			        parameter->DotDotDotToken, parameter->Node::name(),
			        ifElse(isJS, (Node*)nullptr,
			               parameter->QuestionToken),
			        parameterTypeNode, parameter->Initializer));
		}
		parameters = changeTracker->nodeFactory->newNodeList(nodes);
	}

	if (typeNode != nullptr) {
		typeNode = importTypeNode(typeNode, &idToSymbol);
	}

	Node* questionToken = nullptr;
	if (optional) {
		questionToken =
		    changeTracker->nodeFactory->newToken(Kind::QuestionToken);
	}

	switch (kind) {
	case Kind::FunctionExpression: {
		auto* fn = signatureDeclaration->as<FunctionExpression>();
		return changeTracker->nodeFactory->updateFunctionExpression(
		    fn, modifiers, fn->AsteriskToken,
		    ifElse(name != nullptr && isIdentifier(name), name,
		           (Node*)nullptr),
		    typeParameters, parameters, typeNode, fn->FullSignature,
		    orElse(body, fn->Body));
	}

	case Kind::ArrowFunction: {
		auto* fn = signatureDeclaration->as<ArrowFunction>();
		return changeTracker->nodeFactory->updateArrowFunction(
		    fn, modifiers, typeParameters, parameters, typeNode,
		    fn->FullSignature, fn->EqualsGreaterThanToken,
		    orElse(body, fn->Body));
	}

	case Kind::MethodDeclaration: {
		auto* method = signatureDeclaration->as<MethodDeclaration>();
		auto* methodName = ifElse(
		    name == nullptr,
		    changeTracker->nodeFactory->newIdentifier(""),
		    createPropertyName(changeTracker->nodeFactory, name,
		                       quotePreference));
		return changeTracker->nodeFactory->updateMethodDeclaration(
		    method, modifiers, method->AsteriskToken, methodName,
		    questionToken, typeParameters, parameters, typeNode,
		    method->FullSignature, body);
	}

	case Kind::FunctionDeclaration: {
		auto* fn = signatureDeclaration->as<FunctionDeclaration>();
		return changeTracker->nodeFactory->updateFunctionDeclaration(
		    fn, modifiers, fn->AsteriskToken,
		    ifElse(name != nullptr && isIdentifier(name), name,
		           (Node*)nullptr),
		    typeParameters, parameters, typeNode, fn->FullSignature,
		    orElse(body, fn->Body));
	}
	default: {
	}
	}

	return nullptr;
}

// createSignatureDeclarationFromSignatures —
// codeactions_missingmemberfixer.go:305.
Node* missingMemberFixer::createSignatureDeclarationFromSignatures(
    const std::vector<checker::Signature*>& signatures, Node* name,
    bool optional, ModifierList* modifiers,
    lsutil::QuotePreference quotePreference, Node* body,
    Node* enclosingDeclaration) {
	if (signatures.empty()) {
		return nullptr;
	}

	auto [nodeBuilder, idToSymbol] = createNodeBuilder();
	auto* maxArgsSignature = signatures[0];
	int minArgumentCount = signatures[0]->minArgumentCount;

	bool hasRestParameter = false;
	for (auto* signature : signatures) {
		minArgumentCount =
		    std::min(minArgumentCount, signature->minArgumentCount);
		if (signature->flags & checker::SignatureFlagsHasRestParameter) {
			hasRestParameter = true;
		}
		if (signature->parameters.size() >=
		        maxArgsSignature->parameters.size() &&
		    (!(signature->flags & checker::SignatureFlagsHasRestParameter) ||
		     (maxArgsSignature->flags &
		      checker::SignatureFlagsHasRestParameter))) {
			maxArgsSignature = signature;
		}
	}

	int maxNonRestArgs =
	    (int)maxArgsSignature->parameters.size() -
	    ifElse((maxArgsSignature->flags & checker::SignatureFlagsHasRestParameter)
	               != 0,
	           1, 0);
	std::vector<std::string> parameterNames;
	parameterNames.reserve(maxArgsSignature->parameters.size());
	for (auto* symbol : maxArgsSignature->parameters) {
		parameterNames.push_back(symbol->name);
	}
	auto* parameters = createDummyParameters(
	    changeTracker->nodeFactory, maxNonRestArgs, parameterNames,
	    {} /*types*/, minArgumentCount, isInJSFile(enclosingDeclaration));

	if (hasRestParameter) {
		std::string restParameterName = "rest";
		if (maxNonRestArgs < (int)parameterNames.size() &&
		    parameterNames[maxNonRestArgs] != "") {
			restParameterName = parameterNames[maxNonRestArgs];
		}

		Node* questionToken = nullptr;
		if (maxNonRestArgs >= minArgumentCount) {
			questionToken =
			    changeTracker->nodeFactory->newToken(Kind::QuestionToken);
		}

		parameters->nodes.push_back(
		    changeTracker->nodeFactory->newParameterDeclaration(
		        nullptr /*modifiers*/,
		        changeTracker->nodeFactory->newToken(
		            Kind::DotDotDotToken),
		        changeTracker->nodeFactory->newIdentifier(
		            restParameterName),
		        questionToken,
		        changeTracker->nodeFactory->newArrayTypeNode(
		            changeTracker->nodeFactory->newKeywordTypeNode(
		                Kind::UnknownKeyword)),
		        nullptr /*initializer*/));
	}

	auto* methodName = ifElse(
	    name == nullptr, changeTracker->nodeFactory->newIdentifier(""),
	    createPropertyName(changeTracker->nodeFactory, name,
	                       quotePreference));

	return changeTracker->nodeFactory->newMethodDeclaration(
	    modifiers, nullptr /*asteriskToken*/, methodName,
	    ifElse(optional,
	           changeTracker->nodeFactory->newToken(Kind::QuestionToken),
	           (Node*)nullptr),
	    nullptr /*typeParameters*/, parameters,
	    getReturnTypeFromSignatures(signatures, enclosingDeclaration,
	                                nodeBuilder, &idToSymbol),
	    nullptr /*fullSignature*/,
	    createBody(body, quotePreference, false /*signatureOnly*/));
}

// getReturnTypeFromSignatures —
// codeactions_missingmemberfixer.go:359.
Node* missingMemberFixer::getReturnTypeFromSignatures(
    const std::vector<checker::Signature*>& signatures,
    Node* enclosingDeclaration, checker::NodeBuilder* nodeBuilder,
    std::unordered_map<Node*, Symbol*>* idToSymbol) {
	if (signatures.empty()) {
		return nullptr;
	}

	std::vector<checker::Type*> returnTypes;
	returnTypes.reserve(signatures.size());
	for (auto* signature : signatures) {
		returnTypes.push_back(
		    typeChecker->GetReturnTypeOfSignature(signature));
	}

	auto* unionType = typeChecker->GetUnionType(returnTypes);
	return importTypeNode(
	    nodeBuilder->TypeToTypeNode(
	        unionType, enclosingDeclaration, nodebuilder::FlagsNoTruncation,
	        nodebuilder::InternalFlagsAllowUnresolvedNames,
	        nullptr /*typeArguments*/),
	    idToSymbol);
}

// importTypeNode — codeactions_missingmemberfixer.go:373.
Node* missingMemberFixer::importTypeNode(
    Node* typeNode, std::unordered_map<Node*, Symbol*>* idToSymbol) {
	if (typeNode == nullptr || importAdder == nullptr) {
		return typeNode;
	}

	auto [importedTypeNode, symbols] =
	    autoimport::TryGetAutoImportableReferenceFromTypeNode(typeNode,
	                                                        idToSymbol);
	if (importedTypeNode != nullptr) {
		for (auto* symbol : symbols) {
			auto* exportSymbol = getExportedSymbol(symbol);
			if (exportSymbol == nullptr) {
				continue;
			}
			importAdder->AddImportFromExportedSymbol(
			    exportSymbol, true /*isValidTypeOnlyUseSite*/);
		}
		return importedTypeNode;
	}

	std::unordered_set<Symbol*> seen;
	for (auto& [_, symbol] : *idToSymbol) {
		if (symbol == nullptr || seen.count(symbol)) {
			continue;
		}
		seen.insert(symbol);
		auto* exportSymbol = getExportedSymbol(symbol);
		if (exportSymbol == nullptr) {
			continue;
		}
		importAdder->AddImportFromExportedSymbol(
		    exportSymbol, true /*isValidTypeOnlyUseSite*/);
	}
	return typeNode;
}

// getExportedSymbol — codeactions_missingmemberfixer.go:405.
Symbol* missingMemberFixer::getExportedSymbol(Symbol* symbol) {
	symbol = typeChecker->GetExportSymbolOfSymbol(symbol);
	if (symbol == nullptr || symbol->parent == nullptr) {
		return nullptr;
	}
	return symbol;
}

// createIndexSignatureDeclarationFromType —
// codeactions_missingmemberfixer.go:413.
Node* missingMemberFixer::createIndexSignatureDeclarationFromType(
    Node* classDeclaration, checker::Type* implementedType,
    checker::Type* keyType) {
	auto* indexInfo =
	    typeChecker->GetIndexInfoOfType(implementedType, keyType);
	if (indexInfo == nullptr) {
		return nullptr;
	}

	auto* builder =
	    checker::NewNodeBuilder(typeChecker, changeTracker->emitContext);
	return builder->IndexInfoToIndexSignatureDeclaration(
	    indexInfo, classDeclaration, nodebuilder::FlagsNone,
	    nodebuilder::InternalFlagsNone, nullptr);
}

// createBody — codeactions_missingmemberfixer.go:423.
Node* missingMemberFixer::createBody(
    Node* body, lsutil::QuotePreference quotePreference, bool signatureOnly) {
	if (signatureOnly) {
		return nullptr;
	}
	body = deepCloneNode(*changeTracker->nodeFactory, body);
	if (body == nullptr) {
		return createStubbedMethodBody(quotePreference);
	}
	return body;
}

// createStubbedMethodBody — codeactions_missingmemberfixer.go:434.
Node* missingMemberFixer::createStubbedMethodBody(
    lsutil::QuotePreference quotePreference) {
	TokenFlags tokenFlags = TokenFlagsNone;
	if (quotePreference == lsutil::QuotePreferenceSingle) {
		tokenFlags = TokenFlagsSingleQuote;
	}

	return changeTracker->nodeFactory->newBlock(
	    changeTracker->nodeFactory->newNodeList(
	        {changeTracker->nodeFactory->newThrowStatement(
	            changeTracker->nodeFactory->newNewExpression(
	                changeTracker->nodeFactory->newIdentifier("Error"),
	                nullptr /*typeArguments*/,
	                changeTracker->nodeFactory->newNodeList(
	                    {changeTracker->nodeFactory->newStringLiteral(
	                        ::tsc::localize(loc, Method_not_implemented, "",
	                                        {}),
	                        tokenFlags)})))}),
	    true /*multiLine*/);
}

} // namespace tsc::ls
