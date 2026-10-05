// Port of tsc/internal/checker/checker.go:20143-20986 — signature creation, return-type
// and yield/return aggregation machinery (slice: signatures).
#include "internal/checker/checker.h"

#include <algorithm>

#include "internal/checker/mapper.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/scanner/scanner.h"

namespace tsc {
namespace checker {

namespace {

// --- core.* / file-local helpers (PORTING.md: anonymous-namespace copies per TU) ---

// core.AppendIfUnique
template <class T>
void appendIfUnique(std::vector<T>& list, T value) {
	if (std::find(list.begin(), list.end(), value) == list.end()) {
		list.push_back(value);
	}
}

// core.OrElse — returns the first non-nil value.
template <class T>
T orElse(T a, T b) {
	return a != nullptr ? a : b;
}

// isUnitType — checker.go:25878
bool isUnitType(Type* t) {
	return (t->flags & TypeFlagsUnit) != 0;
}

// someType — checker.go:27003
bool someType(Type* t, const std::function<bool(Type*)>& f) {
	if (t->flags & TypeFlagsUnion) {
		for (Type* u : t->types()) {
			if (f(u)) {
				return true;
			}
		}
		return false;
	}
	return f(t);
}

// isObjectLiteralType — utilities.go:867
bool isObjectLiteralType(Type* t) {
	return (t->objectFlags & ObjectFlagsObjectLiteral) != 0;
}

// isOptionalDeclaration — utilities.go:298
bool isOptionalDeclaration(Node* declaration) {
	return hasQuestionToken(declaration);
}

// hasRestParameter — checker.go:28253
bool hasRestParameter(Node* signature) {
	auto params = signature->parameters();
	Node* last = params.empty() ? nullptr : params.back();
	return last != nullptr && last->as<ParameterDeclaration>()->DotDotDotToken != nullptr;
}

// isRestParameter — checker.go:28258
bool isRestParameter(Node* param) {
	return param->as<ParameterDeclaration>()->DotDotDotToken != nullptr;
}

// GetSetAccessorValueParameter — utilities.go:1914
Node* GetSetAccessorValueParameter(Node* accessor) {
	auto parameters = accessor->parameters();
	if (!parameters.empty()) {
		bool hasThis = parameters.size() == 2 && isThisParameter(parameters[0]);
		return parameters[hasThis ? 1 : 0];
	}
	return nullptr;
}

// ForEachReturnStatement — ast/utilities.go:1157.
// Warning: This has the same semantics as the forEach family of functions in that traversal terminates
// in the event that 'visitor' returns true.
bool forEachReturnStatement(Node* body, const std::function<bool(Node*)>& visitor) {
	std::function<bool(Node*)> traverse;
	traverse = [&](Node* node) -> bool {
		switch (node->kind) {
			case Kind::ReturnStatement:
				return visitor(node);
			case Kind::CaseBlock:
			case Kind::Block:
			case Kind::IfStatement:
			case Kind::DoStatement:
			case Kind::WhileStatement:
			case Kind::ForStatement:
			case Kind::ForInStatement:
			case Kind::ForOfStatement:
			case Kind::WithStatement:
			case Kind::SwitchStatement:
			case Kind::CaseClause:
			case Kind::DefaultClause:
			case Kind::LabeledStatement:
			case Kind::TryStatement:
			case Kind::CatchClause:
				return node->forEachChild(traverse);
		}
		return false;
	};
	return traverse(body);
}

// forEachYieldExpression — utilities.go:1264
bool forEachYieldExpression(Node* body, const std::function<bool(Node*)>& visitor) {
	std::function<bool(Node*)> traverse;
	traverse = [&](Node* node) -> bool {
		switch (node->kind) {
			case Kind::YieldExpression: {
				if (visitor(node)) {
					return true;
				}
				Node* operand = node->expression();
				if (operand == nullptr) {
					return false;
				}
				return traverse(operand);
			}
			case Kind::EnumDeclaration:
			case Kind::InterfaceDeclaration:
			case Kind::ModuleDeclaration:
			case Kind::TypeAliasDeclaration:
				// These are not allowed inside a generator now, but eventually they may be allowed
				// as local types. Regardless, skip them to avoid the work.
				break;
			default:
				if (isFunctionLike(node)) {
					if (node->name() != nullptr && isComputedPropertyName(node->name())) {
						// Note that we will not include methods/accessors of a class because they would require
						// first descending into the class. This is by design.
						return traverse(node->name()->expression());
					}
				} else if (!isPartOfTypeNode(node)) {
					// This is the general case, which should include mostly expressions and statements.
					// Also includes NodeArrays.
					return node->forEachChild(traverse);
				}
		}
		return false;
	};
	return traverse(body);
}

// getFlowNodeOfNode — flow.go:69
FlowNode* getFlowNodeOfNode(Node* node) {
	FlowNode** flowNodeData = node->flowNodeData().flowNode;
	if (flowNodeData != nullptr) {
		return *flowNodeData;
	}
	return nullptr;
}

// getEffectiveSetAccessorTypeAnnotationNode — checker.go:20459
Node* getEffectiveSetAccessorTypeAnnotationNode(Node* node) {
	Node* param = GetSetAccessorValueParameter(node);
	if (param != nullptr) {
		return param->type();
	}
	return nullptr;
}

// mayReturnNever — checker.go:20653
bool mayReturnNever(Node* fn) {
	switch (fn->kind) {
		case Kind::FunctionExpression:
		case Kind::ArrowFunction:
			return true;
		case Kind::MethodDeclaration:
			return isObjectLiteralExpression(fn->parent);
	}
	return false;
}

} // namespace

// getSignaturesOfSymbol — checker.go:20143

// getSignatureFromDeclaration — checker.go:20174
Signature* Checker::getSignatureFromDeclaration(Node* declaration) {
	SignatureLinks* links = signatureLinks.Get(declaration);
	if (links->resolvedSignature != nullptr) {
		return links->resolvedSignature;
	}
	std::vector<Symbol*> parameters;
	SignatureFlags flags = 0;
	Symbol* thisParameter = nullptr;
	int minArgumentCount = 0;
	bool hasThisParameter = false;
	Node* iife = getImmediatelyInvokedFunctionExpression(declaration);
	auto declarationParameters = declaration->parameters();
	bool isUntypedSignatureInJSFile = iife == nullptr &&
		isInJSFile(declaration) &&
		(isFunctionExpression(declaration) || isArrowFunction(declaration) || isMethodOrAccessor(declaration) || isFunctionDeclaration(declaration) || isConstructorDeclaration(declaration)) &&
		std::all_of(declarationParameters.begin(), declarationParameters.end(), [](Node* param) { return param->type() == nullptr; }) &&
		getContextualType(declaration, ContextFlagsSignature) == nullptr;
	if (isUntypedSignatureInJSFile) {
		flags |= SignatureFlagsIsUntypedSignatureInJSFile;
	}
	for (size_t i = 0; i < declarationParameters.size(); i++) {
		Node* param = declarationParameters[i];
		Symbol* paramSymbol = param->symbol();
		Node* typeNode = param->type();
		// Include parameter symbol instead of property symbol in the signature
		if (paramSymbol != nullptr && (paramSymbol->flags & SymbolFlagsProperty) != 0 && !isBindingPattern(param->name())) {
			Symbol* resolvedSymbol = resolveName(param, paramSymbol->name, SymbolFlagsValue, nullptr /*nameNotFoundMessage*/, false /*isUse*/, false /*excludeGlobals*/);
			paramSymbol = resolvedSymbol;
		}
		if (i == 0 && paramSymbol->name == InternalSymbolNameThis) {
			hasThisParameter = true;
			thisParameter = param->symbol();
		} else {
			parameters.push_back(paramSymbol);
		}
		if (typeNode != nullptr && typeNode->kind == Kind::LiteralType) {
			flags |= SignatureFlagsHasLiteralTypes;
		}
		// Record a new minimum argument count if this is not an optional parameter
		bool isOptionalParameter = isOptionalDeclaration(param) ||
			param->initializer() != nullptr ||
			isRestParameter(param) ||
			(iife != nullptr && parameters.size() > iife->arguments().size() && typeNode == nullptr);
		if (!isOptionalParameter) {
			minArgumentCount = static_cast<int>(parameters.size());
		}
	}
	// If only one accessor includes a this-type annotation, the other behaves as if it had the same type annotation
	if ((isGetAccessorDeclaration(declaration) || isSetAccessorDeclaration(declaration)) && hasBindableName(declaration) && (!hasThisParameter || thisParameter == nullptr)) {
		Kind otherKind = isGetAccessorDeclaration(declaration) ? Kind::SetAccessor : Kind::GetAccessor;
		Node* other = getDeclarationOfKind(getSymbolOfDeclaration(declaration), otherKind);
		if (other != nullptr) {
			thisParameter = getAnnotatedAccessorThisParameter(other);
		}
	}
	Type* classType = nullptr;
	if (isConstructorDeclaration(declaration)) {
		classType = getDeclaredTypeOfClassOrInterface(getMergedSymbol(declaration->parent->symbol()));
	}
	std::vector<Type*> typeParameters;
	if (classType != nullptr) {
		typeParameters = interfaceTypeLocalTypeParameters(classType->AsInterfaceType());
	} else {
		typeParameters = getTypeParametersFromDeclaration(declaration);
	}
	if (hasRestParameter(declaration)) {
		flags |= SignatureFlagsHasRestParameter;
	}
	if (isConstructorTypeNode(declaration) || isConstructorDeclaration(declaration) || isConstructSignatureDeclaration(declaration)) {
		flags |= SignatureFlagsConstruct;
	}
	if ((isConstructorTypeNode(declaration) && hasSyntacticModifier(declaration, ModifierFlagsAbstract)) || (isConstructorDeclaration(declaration) && hasSyntacticModifier(declaration->parent, ModifierFlagsAbstract))) {
		flags |= SignatureFlagsAbstract;
	}
	links->resolvedSignature = newSignature(flags, declaration, typeParameters, thisParameter, parameters, nullptr /*resolvedReturnType*/, nullptr /*resolvedTypePredicate*/, minArgumentCount);
	return links->resolvedSignature;
}

// getTypeParametersFromDeclaration — checker.go:20254
std::vector<Type*> Checker::getTypeParametersFromDeclaration(Node* declaration) {
	if (Signature* sig = getSignatureOfFullSignatureType(declaration); sig != nullptr) {
		return sig->typeParameters;
	}
	std::vector<Type*> result;
	for (Node* node : declaration->typeParameters()) {
		appendIfUnique(result, getDeclaredTypeOfTypeParameter(node->symbol()));
	}
	return result;
}

// getAnnotatedAccessorThisParameter — checker.go:20264
Symbol* Checker::getAnnotatedAccessorThisParameter(Node* accessor) {
	Node* parameter = getAccessorThisParameter(accessor);
	if (parameter != nullptr) {
		return parameter->symbol();
	}
	return nullptr;
}

// getAccessorThisParameter — checker.go:20272

// hasBindableName — checker.go:20282
/**
 * Indicates whether a declaration has an early-bound name or a dynamic name that can be late-bound.
 */
bool Checker::hasBindableName(Node* node) {
	return !hasDynamicName(node) || hasLateBindableName(node);
}

// getReturnTypeOfSignature — checker.go:20346
Type* Checker::getReturnTypeOfSignature(Signature* sig) {
	if (sig->resolvedReturnType != nullptr) {
		return sig->resolvedReturnType;
	}
	if (!pushTypeResolution(sig, TypeSystemPropertyName::ResolvedReturnType)) {
		return errorType;
	}
	Type* t = nullptr;
	if (sig->target != nullptr) {
		t = instantiateType(getReturnTypeOfSignature(sig->target), sig->mapper);
	} else if (sig->composite != nullptr) {
		std::vector<Type*> compositeReturnTypes;
		for (Signature* s : sig->composite->signatures) {
			compositeReturnTypes.push_back(getReturnTypeOfSignature(s));
		}
		t = instantiateType(getUnionOrIntersectionType(compositeReturnTypes, sig->composite->isUnion, UnionReductionSubtype), sig->mapper);
	} else {
		t = getReturnTypeFromAnnotation(sig->declaration);
		if (t == nullptr) {
			if (!nodeIsMissing(sig->declaration->body())) {
				t = getReturnTypeFromBody(sig->declaration, CheckModeNormal);
			} else {
				t = anyType;
			}
		}
	}
	if ((sig->flags & SignatureFlagsIsInnerCallChain) != 0) {
		t = addOptionalTypeMarker(t);
	} else if ((sig->flags & SignatureFlagsIsOuterCallChain) != 0) {
		t = getOptionalType(t, false /*isProperty*/);
	}
	if (!popTypeResolution()) {
		if (sig->declaration != nullptr) {
			Node* typeNode = sig->declaration->type();
			if (typeNode != nullptr) {
				error(typeNode, Return_type_annotation_circularly_references_itself);
			} else if (noImplicitAny) {
				Node* name = getNameOfDeclaration(sig->declaration);
				if (name != nullptr) {
					error(name, X_0_implicitly_has_return_type_any_because_it_does_not_have_a_return_type_annotation_and_is_referenced_directly_or_indirectly_in_one_of_its_return_expressions, declarationNameToString(name));
				} else {
					error(sig->declaration, Function_implicitly_has_return_type_any_because_it_does_not_have_a_return_type_annotation_and_is_referenced_directly_or_indirectly_in_one_of_its_return_expressions);
				}
			}
		}
		t = anyType;
	}
	if (sig->resolvedReturnType == nullptr) {
		sig->resolvedReturnType = t;
	}
	return sig->resolvedReturnType;
}

// getNonCircularReturnTypeOfSignature — checker.go:20392
Type* Checker::getNonCircularReturnTypeOfSignature(Signature* sig) {
	if (isResolvingReturnTypeOfSignature(sig)) {
		return anyType;
	}
	return getReturnTypeOfSignature(sig);
}

// getReturnTypeFromAnnotation — checker.go:20399
Type* Checker::getReturnTypeFromAnnotation(Node* declaration) {
	if (isConstructorDeclaration(declaration)) {
		return getDeclaredTypeOfClassOrInterface(getMergedSymbol(declaration->parent->symbol()));
	}
	Node* returnType = declaration->type();
	if (returnType != nullptr) {
		return getTypeFromTypeNode(returnType);
	}
	if (isGetAccessorDeclaration(declaration) && hasBindableName(declaration)) {
		return getAnnotatedAccessorType(getDeclarationOfKind(getSymbolOfDeclaration(declaration), Kind::SetAccessor));
	}
	return getReturnTypeOfFullSignature(declaration);
}

// getSignatureOfFullSignatureType — checker.go:20413
Signature* Checker::getSignatureOfFullSignatureType(Node* node) {
	Node** fullSignatureSlot = node->functionLikeData().fullSignature;
	if (isInJSFile(node) && (isFunctionDeclaration(node) || isMethodDeclaration(node) || isFunctionExpressionOrArrowFunction(node)) && fullSignatureSlot != nullptr && *fullSignatureSlot != nullptr) {
		return getSingleCallSignature(getTypeFromTypeNode(*fullSignatureSlot));
	}
	return nullptr;
}

// getParameterTypeOfFullSignature — checker.go:20420
Type* Checker::getParameterTypeOfFullSignature(Node* node, Node* parameter) {
	if (Signature* signature = getSignatureOfFullSignatureType(node); signature != nullptr) {
		std::vector<Node*> params = node->parameters();
		int pos = static_cast<int>(std::find(params.begin(), params.end(), parameter) - params.begin());
		if (parameter->as<ParameterDeclaration>()->DotDotDotToken != nullptr) {
			return getRestTypeAtPosition(signature, pos, false /*readonly*/);
		} else {
			return getTypeAtPosition(signature, pos);
		}
	}
	return nullptr;
}

// getReturnTypeOfFullSignature — checker.go:20432
Type* Checker::getReturnTypeOfFullSignature(Node* node) {
	if (Signature* signature = getSignatureOfFullSignatureType(node); signature != nullptr) {
		return getReturnTypeOfSignature(signature);
	}
	return nullptr;
}

// getAnnotatedAccessorType — checker.go:20439
Type* Checker::getAnnotatedAccessorType(Node* accessor) {
	Node* node = getAnnotatedAccessorTypeNode(accessor);
	if (node != nullptr) {
		return getTypeFromTypeNode(node);
	}
	return nullptr;
}

// getAnnotatedAccessorTypeNode — checker.go:20447
Node* Checker::getAnnotatedAccessorTypeNode(Node* accessor) {
	if (accessor != nullptr) {
		switch (accessor->kind) {
			case Kind::GetAccessor:
			case Kind::PropertyDeclaration:
				return accessor->type();
			case Kind::SetAccessor:
				return getEffectiveSetAccessorTypeAnnotationNode(accessor);
		}
	}
	return nullptr;
}

// getReturnTypeFromBody — checker.go:20467
Type* Checker::getReturnTypeFromBody(Node* fn, CheckMode checkMode) {
	Node* body = fn->body();
	if (body == nullptr) {
		return errorType;
	}
	FunctionFlags functionFlags = getFunctionFlags(fn);
	bool isAsync = (functionFlags & FunctionFlagsAsync) != 0;
	bool isGenerator = (functionFlags & FunctionFlagsGenerator) != 0;
	Type* returnType = nullptr;
	Type* yieldType = nullptr;
	Type* nextType = nullptr;
	Type* fallbackReturnType = voidType;
	if (!isBlock(body)) {
		returnType = checkExpressionCachedEx(body, checkMode & ~CheckModeSkipGenericFunctions);
		if (isConstContext(body)) {
			returnType = getRegularTypeOfLiteralType(returnType);
		}
		if (isAsync) {
			// From within an async function you can return either a non-promise value or a promise. Any
			// Promise/A+ compatible implementation will always assimilate any foreign promise, so the
			// return type of the body should be unwrapped to its awaited type, which we will wrap in
			// the native Promise<T> type later in this function.
			returnType = unwrapAwaitedType(checkAwaitedType(returnType, false /*withAlias*/, fn /*errorNode*/, The_return_type_of_an_async_function_must_either_be_a_valid_promise_or_must_not_contain_a_callable_then_member));
		}
	} else if (isGenerator) {
		auto [returnTypes, isNeverReturning] = checkAndAggregateReturnExpressionTypes(fn, checkMode);
		if (isNeverReturning) {
			fallbackReturnType = neverType;
		} else if (!returnTypes.empty()) {
			returnType = getUnionTypeEx(returnTypes, UnionReductionSubtype, nullptr, nullptr);
		}
		auto [yieldTypes, nextTypes] = checkAndAggregateYieldOperandTypes(fn, checkMode);
		if (!yieldTypes.empty()) {
			yieldType = getUnionTypeEx(yieldTypes, UnionReductionSubtype, nullptr, nullptr);
		}
		if (!nextTypes.empty()) {
			nextType = getIntersectionType(nextTypes);
		}
	} else {
		auto [types, isNeverReturning] = checkAndAggregateReturnExpressionTypes(fn, checkMode);
		if (isNeverReturning) {
			// For an async function, the return type will not be never, but rather a Promise for never.
			if ((functionFlags & FunctionFlagsAsync) != 0) {
				return createPromiseReturnType(fn, neverType);
			}
			// Normal function
			return neverType;
		}
		if (types.empty()) {
			// For an async function, the return type will not be void/undefined, but rather a Promise for void/undefined.
			Type* contextualReturnType = getContextualReturnType(fn, ContextFlagsNone);
			Type* returnType = nullptr;
			if (contextualReturnType != nullptr && someType(orElse(unwrapReturnType(contextualReturnType, functionFlags), voidType), [](Type* t) { return (t->flags & TypeFlagsUndefined) != 0; })) {
				returnType = undefinedType;
			} else {
				returnType = voidType;
			}
			if ((functionFlags & FunctionFlagsAsync) != 0) {
				return createPromiseReturnType(fn, returnType);
			}
			// Normal function
			return returnType;
		}
		// Return a union of the return expression types.
		returnType = getUnionTypeEx(types, UnionReductionSubtype, nullptr, nullptr);
	}
	if (returnType != nullptr || yieldType != nullptr || nextType != nullptr) {
		if (yieldType != nullptr) {
			reportErrorsFromWidening(fn, yieldType, WideningKind::GeneratorYield);
		}
		if (returnType != nullptr) {
			reportErrorsFromWidening(fn, returnType, WideningKind::FunctionReturn);
		}
		if (nextType != nullptr) {
			reportErrorsFromWidening(fn, nextType, WideningKind::GeneratorNext);
		}
		if ((returnType != nullptr && isUnitType(returnType)) || (yieldType != nullptr && isUnitType(yieldType)) || (nextType != nullptr && isUnitType(nextType))) {
			Signature* contextualSignature = getContextualSignatureForFunctionLikeDeclaration(fn);
			Type* contextualType = nullptr;
			if (contextualSignature == nullptr) {
				// No contextual type
			} else if (contextualSignature == getSignatureFromDeclaration(fn)) {
				if (!isGenerator) {
					contextualType = returnType;
				}
			} else {
				contextualType = instantiateContextualType(getReturnTypeOfSignature(contextualSignature), fn, ContextFlagsNone);
			}
			if (isGenerator) {
				yieldType = getWidenedLiteralLikeTypeForContextualIterationTypeIfNeeded(yieldType, contextualType, IterationTypeKind::Yield, isAsync);
				returnType = getWidenedLiteralLikeTypeForContextualIterationTypeIfNeeded(returnType, contextualType, IterationTypeKind::Return, isAsync);
				nextType = getWidenedLiteralLikeTypeForContextualIterationTypeIfNeeded(nextType, contextualType, IterationTypeKind::Next, isAsync);
			} else {
				returnType = getWidenedLiteralLikeTypeForContextualReturnTypeIfNeeded(returnType, contextualType, isAsync);
			}
		}
		if (yieldType != nullptr) {
			yieldType = getWidenedType(yieldType);
		}
		if (returnType != nullptr) {
			returnType = getWidenedType(returnType);
		}
		if (nextType != nullptr) {
			nextType = getWidenedType(nextType);
		}
	}
	if (returnType == nullptr) {
		returnType = fallbackReturnType;
	}
	if (isGenerator) {
		if (yieldType == nullptr) {
			yieldType = neverType;
		}
		if (nextType == nullptr) {
			nextType = getContextualIterationType(IterationTypeKind::Next, fn);
			if (nextType == nullptr) {
				nextType = unknownType;
			}
		}
		return createGeneratorType(yieldType, returnType, nextType, isAsync);
	}
	// From within an async function you can return either a non-promise value or a promise. Any
	// Promise/A+ compatible implementation will always assimilate any foreign promise, so the
	// return type of the body is awaited type of the body, wrapped in a native Promise<T> type.
	if (isAsync) {
		return createPromiseType(returnType);
	}
	return returnType;
}

// checkAndAggregateReturnExpressionTypes — checker.go:20599
// Returns the aggregated list of return types, plus a bool indicating a never-returning function.
std::pair<std::vector<Type*>, bool> Checker::checkAndAggregateReturnExpressionTypes(Node* fn, CheckMode checkMode) {
	FunctionFlags functionFlags = getFunctionFlags(fn);
	std::vector<Type*> aggregatedTypes;
	bool hasReturnWithNoExpression = functionHasImplicitReturn(fn);
	bool hasReturnOfTypeNever = false;
	forEachReturnStatement(fn->body(), [&](Node* returnStatement) -> bool {
		Node* expr = returnStatement->expression();
		if (expr == nullptr) {
			hasReturnWithNoExpression = true;
			return false;
		}
		expr = skipParentheses(expr);
		// Bare calls to this same function don't contribute to inference
		// and `return await` is also safe to unwrap here
		if ((functionFlags & FunctionFlagsAsync) != 0 && isAwaitExpression(expr)) {
			expr = skipParentheses(expr->expression());
		}
		if (isCallExpression(expr) && isIdentifier(expr->expression()) && checkExpressionCached(expr->expression())->symbol == getMergedSymbol(fn->symbol()) &&
			(!isFunctionExpressionOrArrowFunction(fn->symbol()->valueDeclaration) || isConstantReference(expr->expression()))) {
			hasReturnOfTypeNever = true;
			return false;
		}
		Type* t = checkExpressionCachedEx(expr, checkMode & ~CheckModeSkipGenericFunctions);
		if ((functionFlags & FunctionFlagsAsync) != 0) {
			// From within an async function you can return either a non-promise value or a promise. Any
			// Promise/A+ compatible implementation will always assimilate any foreign promise, so the
			// return type of the body should be unwrapped to its awaited type, which should be wrapped in
			// the native Promise<T> type by the caller.
			t = unwrapAwaitedType(checkAwaitedType(t, false /*withAlias*/, fn, The_return_type_of_an_async_function_must_either_be_a_valid_promise_or_must_not_contain_a_callable_then_member));
		}
		if ((t->flags & TypeFlagsNever) != 0) {
			hasReturnOfTypeNever = true;
		}
		if (isConstContext(expr)) {
			t = getRegularTypeOfLiteralType(t);
		}
		appendIfUnique(aggregatedTypes, t);
		return false;
	});
	if (aggregatedTypes.empty() && !hasReturnWithNoExpression && (hasReturnOfTypeNever || mayReturnNever(fn))) {
		return {{}, true};
	}
	if (strictNullChecks && !aggregatedTypes.empty() && hasReturnWithNoExpression) {
		appendIfUnique(aggregatedTypes, undefinedType);
	}
	return {aggregatedTypes, false};
}

// functionHasImplicitReturn — checker.go:20648
bool Checker::functionHasImplicitReturn(Node* fn) {
	FlowNode** endFlowNodeSlot = fn->bodyData().endFlowNode;
	FlowNode* endFlowNode = endFlowNodeSlot != nullptr ? *endFlowNodeSlot : nullptr;
	return endFlowNode != nullptr && isReachableFlowNode(endFlowNode);
}

// checkAndAggregateYieldOperandTypes — checker.go:20663
std::pair<std::vector<Type*>, std::vector<Type*>> Checker::checkAndAggregateYieldOperandTypes(Node* fn, CheckMode checkMode) {
	std::vector<Type*> yieldTypes;
	std::vector<Type*> nextTypes;
	bool isAsync = (getFunctionFlags(fn) & FunctionFlagsAsync) != 0;
	forEachYieldExpression(fn->body(), [&](Node* yieldExpr) -> bool {
		Type* yieldExprType = undefinedWideningType;
		if (yieldExpr->expression() != nullptr) {
			yieldExprType = checkExpressionEx(yieldExpr->expression(), checkMode & ~CheckModeSkipGenericFunctions);
		}
		if (yieldExpr->expression() != nullptr && isConstContext(yieldExpr->expression())) {
			yieldExprType = getRegularTypeOfLiteralType(yieldExprType);
		}
		appendIfUnique(yieldTypes, getYieldedTypeOfYieldExpression(yieldExpr, yieldExprType, anyType, isAsync));
		Type* nextType = nullptr;
		if (yieldExpr->as<YieldExpression>()->AsteriskToken != nullptr) {
			IterationTypes iterationTypes = getIterationTypesOfIterable(yieldExprType, isAsync ? IterationUseAsyncYieldStar : IterationUseYieldStar, yieldExpr->expression());
			nextType = iterationTypes.nextType;
		} else {
			nextType = getContextualType(yieldExpr, ContextFlagsNone);
		}
		if (nextType != nullptr) {
			appendIfUnique(nextTypes, nextType);
		}
		return false;
	});
	return {yieldTypes, nextTypes};
}

// createPromiseType — checker.go:20689
Type* Checker::createPromiseType(Type* promisedType) {
	// creates a `Promise<T>` type where `T` is the promisedType argument
	Type* globalPromiseType = getGlobalPromiseTypeChecked();
	if (globalPromiseType != emptyGenericType) {
		// if the promised type is itself a promise, get the underlying type; otherwise, fallback to the promised type
		// Unwrap an `Awaited<T>` to `T` to improve inference.
		promisedType = orElse(getAwaitedTypeNoAlias(unwrapAwaitedType(promisedType)), unknownType);
		return createTypeReference(globalPromiseType, {promisedType});
	}
	return unknownType;
}

// createPromiseLikeType — checker.go:20701
Type* Checker::createPromiseLikeType(Type* promisedType) {
	// creates a `PromiseLike<T>` type where `T` is the promisedType argument
	Type* globalPromiseLikeType = getGlobalPromiseLikeType();
	if (globalPromiseLikeType != emptyGenericType) {
		// if the promised type is itself a promise, get the underlying type; otherwise, fallback to the promised type
		// Unwrap an `Awaited<T>` to `T` to improve inference.
		promisedType = orElse(getAwaitedTypeNoAlias(unwrapAwaitedType(promisedType)), unknownType);
		return createTypeReference(globalPromiseLikeType, {promisedType});
	}
	return unknownType;
}

// createPromiseReturnType — checker.go:20713
Type* Checker::createPromiseReturnType(Node* fn, Type* promisedType) {
	Type* promiseType = createPromiseType(promisedType);
	if (promiseType == unknownType) {
		error(fn, isImportCall(fn) ?
			A_dynamic_import_call_returns_a_Promise_Make_sure_you_have_a_declaration_for_Promise_or_include_ES2015_in_your_lib_option :
			An_async_function_or_method_must_return_a_Promise_Make_sure_you_have_a_declaration_for_Promise_or_include_ES2015_in_your_lib_option);
		return errorType;
	}
	if (getGlobalPromiseConstructorSymbol() == nullptr) {
		error(fn, isImportCall(fn) ?
			A_dynamic_import_call_in_ES5_requires_the_Promise_constructor_Make_sure_you_have_a_declaration_for_the_Promise_constructor_or_include_ES2015_in_your_lib_option :
			An_async_function_or_method_in_ES5_requires_the_Promise_constructor_Make_sure_you_have_a_declaration_for_the_Promise_constructor_or_include_ES2015_in_your_lib_option);
	}
	return promiseType;
}

// unwrapReturnType — checker.go:20729
Type* Checker::unwrapReturnType(Type* returnType, FunctionFlags functionFlags) {
	bool isGenerator = (functionFlags & FunctionFlagsGenerator) != 0;
	bool isAsync = (functionFlags & FunctionFlagsAsync) != 0;
	if (isGenerator) {
		Type* returnIterationType = getIterationTypeOfGeneratorFunctionReturnType(IterationTypeKind::Return, returnType, isAsync);
		if (returnIterationType == nullptr) {
			return errorType;
		}
		if (isAsync) {
			return getAwaitedTypeNoAlias(unwrapAwaitedType(returnIterationType));
		}
		return returnIterationType;
	}
	if (isAsync) {
		return orElse(getAwaitedTypeNoAlias(returnType), errorType);
	}
	return returnType;
}

// getWidenedLiteralLikeTypeForContextualReturnTypeIfNeeded — checker.go:20748
Type* Checker::getWidenedLiteralLikeTypeForContextualReturnTypeIfNeeded(Type* t, Type* contextualSignatureReturnType, bool isAsync) {
	if (t != nullptr && isUnitType(t)) {
		Type* contextualType = nullptr;
		if (contextualSignatureReturnType == nullptr) {
			// No contextual type
		} else if (isAsync) {
			contextualType = GetPromisedTypeOfPromise(contextualSignatureReturnType);
		} else {
			contextualType = contextualSignatureReturnType;
		}
		t = getWidenedLiteralLikeTypeForContextualType(t, contextualType);
	}
	return t;
}

// getWidenedLiteralLikeTypeForContextualIterationTypeIfNeeded — checker.go:20764
Type* Checker::getWidenedLiteralLikeTypeForContextualIterationTypeIfNeeded(Type* t, Type* contextualSignatureReturnType, IterationTypeKind kind, bool isAsyncGenerator) {
	if (t != nullptr && isUnitType(t)) {
		Type* contextualType = nullptr;
		if (contextualSignatureReturnType != nullptr) {
			contextualType = getIterationTypeOfGeneratorFunctionReturnType(kind, contextualSignatureReturnType, isAsyncGenerator);
		}
		t = getWidenedLiteralLikeTypeForContextualType(t, contextualType);
	}
	return t;
}

// createGeneratorType — checker.go:20775
Type* Checker::createGeneratorType(Type* yieldType, Type* returnType, Type* nextType, bool isAsyncGenerator) {
	IterationTypesResolver* resolver = isAsyncGenerator ? asyncIterationTypesResolver : syncIterationTypesResolver;
	Type* globalGeneratorType = resolver->getGlobalGeneratorType();
	yieldType = orElse(resolver->resolveIterationType(yieldType, nullptr /*errorNode*/), unknownType);
	returnType = orElse(resolver->resolveIterationType(returnType, nullptr /*errorNode*/), unknownType);
	if (globalGeneratorType == emptyGenericType) {
		// Fall back to the global IterableIterator type.
		Type* globalIterableIteratorType = resolver->getGlobalIterableIteratorType();
		if (globalIterableIteratorType != emptyGenericType) {
			return createTypeFromGenericGlobalType(globalIterableIteratorType, {yieldType, returnType, nextType});
		}
		// The global Generator type doesn't exist, so report an error
		resolver->getGlobalIterableIteratorTypeChecked();
		return emptyObjectType;
	}
	return createTypeFromGenericGlobalType(globalGeneratorType, {yieldType, returnType, nextType});
}

// reportErrorsFromWidening — checker.go:20793
void Checker::reportErrorsFromWidening(Node* declaration, Type* t, WideningKind wideningKind) {
	if (noImplicitAny && (t->objectFlags & ObjectFlagsContainsWideningType) != 0) {
		if (wideningKind == WideningKind::Normal || (isFunctionLikeDeclaration(declaration) && shouldReportErrorsFromWideningWithContextualSignature(declaration, wideningKind))) {
			// Report implicit any error within type if possible, otherwise report error on declaration
			if (!reportWideningErrorsInType(t)) {
				reportImplicitAny(declaration, t, wideningKind);
			}
		}
	}
}

// shouldReportErrorsFromWideningWithContextualSignature — checker.go:20804
bool Checker::shouldReportErrorsFromWideningWithContextualSignature(Node* declaration, WideningKind wideningKind) {
	Signature* signature = getContextualSignatureForFunctionLikeDeclaration(declaration);
	if (signature == nullptr) {
		return true;
	}
	Type* returnType = getReturnTypeOfSignature(signature);
	FunctionFlags flags = getFunctionFlags(declaration);
	switch (wideningKind) {
		case WideningKind::FunctionReturn:
			if ((flags & FunctionFlagsGenerator) != 0) {
				returnType = orElse(getIterationTypeOfGeneratorFunctionReturnType(IterationTypeKind::Return, returnType, (flags & FunctionFlagsAsync) != 0), returnType);
			} else if ((flags & FunctionFlagsAsync) != 0) {
				returnType = orElse(getAwaitedTypeNoAlias(returnType), returnType);
			}
			return isGenericType(returnType);
		case WideningKind::GeneratorYield: {
			Type* yieldType = getIterationTypeOfGeneratorFunctionReturnType(IterationTypeKind::Yield, returnType, (flags & FunctionFlagsAsync) != 0);
			return yieldType != nullptr && isGenericType(yieldType);
		}
		case WideningKind::GeneratorNext: {
			Type* nextType = getIterationTypeOfGeneratorFunctionReturnType(IterationTypeKind::Next, returnType, (flags & FunctionFlagsAsync) != 0);
			return nextType != nullptr && isGenericType(nextType);
		}
	}
	return false;
}

// reportWideningErrorsInType — checker.go:20838
// Reports implicit any errors that occur as a result of widening 'null' and 'undefined'
// to 'any'. A call to reportWideningErrorsInType is normally accompanied by a call to
// getWidenedType. But in some cases getWidenedType is called without reporting errors
// (type argument inference is an example).
//
// The return value indicates whether an error was in fact reported. The particular circumstances
// are on a best effort basis. Currently, if the null or undefined that causes widening is inside
// an object literal property (arbitrarily deeply), this function reports an error. If no error is
// reported, reportImplicitAnyError is a suitable fallback to report a general error.
bool Checker::reportWideningErrorsInType(Type* t) {
	bool errorReported = false;
	if ((t->objectFlags & ObjectFlagsContainsWideningType) != 0) {
		if ((t->flags & TypeFlagsUnion) != 0) {
			bool someEmptyObject = false;
			for (Type* u : t->types()) {
				if (isEmptyObjectType(u)) {
					someEmptyObject = true;
					break;
				}
			}
			if (someEmptyObject) {
				errorReported = true;
			} else {
				for (Type* s : t->types()) {
					errorReported = errorReported || reportWideningErrorsInType(s);
				}
			}
		} else if (isArrayOrTupleType(t)) {
			for (Type* s : getTypeArguments(t)) {
				errorReported = errorReported || reportWideningErrorsInType(s);
			}
		} else if (isObjectLiteralType(t)) {
			for (Symbol* p : getPropertiesOfObjectType(t)) {
				Type* s = getTypeOfSymbol(p);
				if ((s->objectFlags & ObjectFlagsContainsWideningType) != 0) {
					errorReported = reportWideningErrorsInType(s);
					if (!errorReported) {
						// we need to account for property types coming from object literal type normalization in unions
						Node* valueDeclaration = nullptr;
						for (Node* d : p->declarations) {
							Node* vd = d->symbol() != nullptr ? d->symbol()->valueDeclaration : nullptr;
							if (vd != nullptr && vd->parent == t->symbol->valueDeclaration) {
								valueDeclaration = d;
								break;
							}
						}
						if (valueDeclaration != nullptr) {
							error(valueDeclaration, Object_literal_s_property_0_implicitly_has_an_1_type, {symbolToString(p), TypeToString(getWidenedType(s))});
							errorReported = true;
						}
					}
				}
			}
		}
	}
	return errorReported;
}

// getTypePredicateFromBody — checker.go:20876
TypePredicate* Checker::getTypePredicateFromBody(Node* fn) {
	switch (fn->kind) {
		case Kind::Constructor:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
			return nullptr;
	}
	FunctionFlags functionFlags = getFunctionFlags(fn);
	if (functionFlags != FunctionFlagsNormal) {
		return nullptr;
	}
	// Only attempt to infer a type predicate if there's exactly one return.
	Node* singleReturn = nullptr;
	Node* body = fn->body();
	if (body != nullptr && !isBlock(body)) {
		// arrow function
		singleReturn = body;
	} else {
		bool bailedEarly = forEachReturnStatement(body, [&](Node* returnStatement) -> bool {
			if (singleReturn != nullptr || returnStatement->expression() == nullptr) {
				return true;
			}
			singleReturn = returnStatement->expression();
			return false;
		});
		if (bailedEarly || singleReturn == nullptr || functionHasImplicitReturn(fn)) {
			return nullptr;
		}
	}
	return checkIfExpressionRefinesAnyParameter(fn, singleReturn);
}

// checkIfExpressionRefinesAnyParameter — checker.go:20906
TypePredicate* Checker::checkIfExpressionRefinesAnyParameter(Node* fn, Node* expr) {
	expr = skipParentheses(expr);
	Type* returnType = checkExpressionCached(expr);
	if ((returnType->flags & TypeFlagsBoolean) == 0) {
		return nullptr;
	}
	std::vector<Node*> params = fn->parameters();
	for (size_t i = 0; i < params.size(); i++) {
		Node* param = params[i];
		Type* initType = getTypeOfSymbol(param->symbol());
		if (initType == nullptr || (initType->flags & TypeFlagsBoolean) != 0 || !isIdentifier(param->name()) || isSymbolAssigned(param->symbol()) || isRestParameter(param)) {
			// Refining "x: boolean" to "x is true" or "x is false" isn't useful.
			continue;
		}
		Type* trueType = checkIfExpressionRefinesParameter(fn, expr, param, initType);
		if (trueType != nullptr) {
			return newTypePredicate(TypePredicateKind::Identifier, param->name()->text(), static_cast<int32_t>(i), trueType);
		}
	}
	return nullptr;
}

// checkIfExpressionRefinesParameter — checker.go:20926
Type* Checker::checkIfExpressionRefinesParameter(Node* fn, Node* expr, Node* param, Type* initType) {
	FlowNode* antecedent = getFlowNodeOfNode(expr);
	if (antecedent == nullptr && isReturnStatement(expr->parent)) {
		antecedent = getFlowNodeOfNode(expr->parent);
	}
	if (antecedent == nullptr) {
		antecedent = linksArena.alloc<FlowNode>();
		antecedent->flags = FlowFlagsStart;
	}
	FlowNode* trueCondition = linksArena.alloc<FlowNode>();
	trueCondition->flags = FlowFlagsTrueCondition;
	trueCondition->node = expr;
	trueCondition->antecedent = antecedent;
	Type* trueType = getFlowTypeOfReferenceEx(param->name(), initType, initType, fn, trueCondition);
	if (trueType == initType) {
		return nullptr;
	}
	// "x is T" means that x is T if and only if it returns true. If it returns false then x is not T.
	// This means that if the function is called with an argument of type trueType, there can't be anything left in the `else` branch. It must reduce to `never`.
	FlowNode* falseCondition = linksArena.alloc<FlowNode>();
	falseCondition->flags = FlowFlagsFalseCondition;
	falseCondition->node = expr;
	falseCondition->antecedent = antecedent;
	Type* falseSubtype = getReducedType(getFlowTypeOfReferenceEx(param->name(), initType, trueType, fn, falseCondition));
	if ((falseSubtype->flags & TypeFlagsNever) != 0) {
		return trueType;
	}
	return nullptr;
}

// addOptionalTypeMarker — checker.go:20949
Type* Checker::addOptionalTypeMarker(Type* t) {
	if (strictNullChecks) {
		return getUnionType({t, optionalType});
	}
	return t;
}

// instantiateSignature — checker.go:20956
Signature* Checker::instantiateSignature(Signature* sig, TypeMapper* m) {
	return instantiateSignatureEx(sig, m, m == permissiveMapper /*eraseTypeParameters*/);
}

// instantiateSignatureEx — checker.go:20960
Signature* Checker::instantiateSignatureEx(Signature* sig, TypeMapper* m, bool eraseTypeParameters) {
	std::vector<Type*> freshTypeParameters;
	if (!sig->typeParameters.empty() && !eraseTypeParameters) {
		// First create a fresh set of type parameters, then include a mapping from the old to the
		// new type parameters in the mapper function. Finally store this mapper in the new type
		// parameters such that we can use it when instantiating constraints.
		for (Type* tp : sig->typeParameters) {
			freshTypeParameters.push_back(cloneTypeParameter(tp));
		}
		m = combineTypeMappers(newTypeMapper(sig->typeParameters, freshTypeParameters), m);
		for (Type* tp : freshTypeParameters) {
			tp->AsTypeParameter()->mapper = m;
		}
	}
	// Don't compute resolvedReturnType and resolvedTypePredicate now,
	// because using `mapper` now could trigger inferences to become fixed. (See `createInferenceContext`.)
	// See GH#17600.
	Signature* result = newSignature(sig->flags & SignatureFlagsPropagatingFlags, sig->declaration, freshTypeParameters,
		instantiateSymbol(sig->thisParameter, m), instantiateSymbols(sig->parameters, m),
		nullptr /*resolvedReturnType*/, nullptr /*resolvedTypePredicate*/, static_cast<int>(sig->minArgumentCount));
	result->target = sig;
	result->mapper = m;
	return result;
}

// instantiateIndexInfo — checker.go:20983
IndexInfo* Checker::instantiateIndexInfo(IndexInfo* info, TypeMapper* m) {
	Type* newValueType = instantiateType(info->valueType, m);
	if (newValueType == info->valueType) {
		return info;
	}
	return newIndexInfo(info->keyType, newValueType, info->isReadonly, info->declaration, info->components);
}

// === dep stubs — removed when owner slice lands ===
Type* Checker::getRestTypeAtPosition(Signature* /*source*/, int /*pos*/, bool /*readonly*/) { TSC_UNREACHABLE("getRestTypeAtPosition — owned by typeops/relater"); }
TypePredicate* Checker::newTypePredicate(TypePredicateKind /*kind*/, const std::string& /*parameterName*/, int32_t /*parameterIndex*/, Type* /*t*/) { TSC_UNREACHABLE("newTypePredicate — owned by typeops/relater"); }
bool Checker::isResolvingReturnTypeOfSignature(Signature* /*signature*/) { TSC_UNREACHABLE("isResolvingReturnTypeOfSignature — owned by typeops/relater"); }
Type* Checker::getYieldedTypeOfYieldExpression(Node* /*node*/, Type* /*expressionType*/, Type* /*sentType*/, bool /*isAsync*/) { TSC_UNREACHABLE("getYieldedTypeOfYieldExpression — signatures dep"); }
bool Checker::isConstContext(Node* /*node*/) { TSC_UNREACHABLE("isConstContext — signatures dep"); }















// (deduped: GetPromisedTypeOfPromise defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: getContextualType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: getContextualIterationType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: getContextualReturnType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: getContextualSignatureForFunctionLikeDeclaration defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: instantiateContextualType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: checkAwaitedType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: getAwaitedTypeNoAlias defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: unwrapAwaitedType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: reportImplicitAny defined in cpp/internal/checker/checker_decltypes.cpp)

// (deduped: getPropertiesOfObjectType defined in cpp/internal/checker/checker_members.cpp)

// (deduped: instantiateSymbol defined in cpp/internal/checker/checker_members.cpp)

// (deduped: getTypeArguments defined in cpp/internal/checker/checker_instantiate.cpp)

// (deduped: cloneTypeParameter defined in cpp/internal/checker/checker_instantiate.cpp)

// (deduped: isArrayOrTupleType defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: isGenericType defined in cpp/internal/checker/checker_grammar.cpp)

IterationTypes Checker::getIterationTypesOfIterable(Type* /*t*/, IterationUse /*use*/, Node* /*errorNode*/) { TSC_UNREACHABLE("getIterationTypesOfIterable — owned by iteration"); }
Type* Checker::getIterationTypeOfGeneratorFunctionReturnType(IterationTypeKind /*typeKind*/, Type* /*returnType*/, bool /*isAsyncGenerator*/) { TSC_UNREACHABLE("getIterationTypeOfGeneratorFunctionReturnType — owned by iteration"); }
bool Checker::isConstantReference(Node* /*node*/) { TSC_UNREACHABLE("isConstantReference — owned by flow"); }
bool Checker::isReachableFlowNode(FlowNode* /*flow*/) { TSC_UNREACHABLE("isReachableFlowNode — owned by flow"); }
bool Checker::isSymbolAssigned(Symbol* /*symbol*/) { TSC_UNREACHABLE("isSymbolAssigned — owned by flow"); }

} // namespace checker
} // namespace tsc
