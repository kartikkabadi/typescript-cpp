// checker_expressions_a.cpp — checker.go:8090-10491 (slice "expr_a")
// Expression checking: template/array literals, qualified names, element/indexed
// access, import() calls, call/new/tagged-template/decorator/instanceof signature
// resolution (resolveCall + CallState + overload selection + type-argument
// checking + applicability + arity/type-argument error reporting + untyped
// calls + invocation diagnostics), class-expression helpers, and function
// expression / object literal method checking incl. contextual typing.
//
// Dep-stub convention (this file only): callees owned by other slices are
// defined at the bottom under "// === dep stubs ===" with TSC_UNREACHABLE
// bodies and an owner tag; each is deleted when the owner's real definition
// lands. Free helpers duplicated across TUs live in the anonymous namespace —
// internal linkage, so they cannot collide at merge.

#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/scanner/scanner.h"
#include <algorithm>
#include <climits>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tsc::checker {

// ---------------------------------------------------------------------------
// Extern declarations for free functions defined in other checker TUs.
// ---------------------------------------------------------------------------

Diagnostic* NewDiagnosticChainForNode(Diagnostic* chain, Node* node,
	const DiagnosticMessage* message, const std::vector<std::string>& args = {});
bool someType(Type* t, const std::function<bool(Type*)>& f);
bool everyType(Type* t, const std::function<bool(Type*)>& f);

namespace {

// ---------------------------------------------------------------------------
// core.* helpers
// ---------------------------------------------------------------------------

// (ifElse comes from ast.h:611 — tsc::ifElse)

template <class T>
T orElse(T v, T fallback) {
	return v != nullptr ? v : fallback;
}

template <class T, class F>
bool someOf(const std::vector<T>& v, F f) {
	for (const T& x : v) {
		if (f(x)) {
			return true;
		}
	}
	return false;
}

template <class T, class F>
bool everyOf(const std::vector<T>& v, F f) {
	for (const T& x : v) {
		if (!f(x)) {
			return false;
		}
	}
	return true;
}

template <class T, class F>
auto mapVec(const std::vector<T>& v, F f) -> std::vector<decltype(f(v[0]))> {
	using U = decltype(f(v[0]));
	std::vector<U> result;
	result.reserve(v.size());
	for (const T& x : v) {
		result.push_back(f(x));
	}
	return result;
}

// core.MapNonNil — maps each element, dropping nil results.
template <class T, class F>
auto mapNonNil(const std::vector<T>& v, F f)
	-> std::vector<std::decay_t<decltype(f(v[0]))>> {
	using U = std::decay_t<decltype(f(v[0]))>;
	std::vector<U> result;
	for (const T& x : v) {
		if (U r = f(x); r != nullptr) {
			result.push_back(r);
		}
	}
	return result;
}

template <class T, class F>
std::vector<T> filterVec(const std::vector<T>& v, F f) {
	std::vector<T> result;
	for (const T& x : v) {
		if (f(x)) {
			result.push_back(x);
		}
	}
	return result;
}

template <class T, class F>
T findIf(const std::vector<T>& v, F f) {
	for (const T& x : v) {
		if (f(x)) {
			return x;
		}
	}
	return T{};
}

// core.FindIndex — index of first match, -1 when absent.
template <class T, class F>
int findIndexOf(const std::vector<T>& v, F f) {
	for (size_t i = 0; i < v.size(); i++) {
		if (f(v[i])) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

template <class T>
T firstOrNil(const std::vector<T>& v) {
	return v.empty() ? T{} : v.front();
}

template <class T>
T lastOrNil(const std::vector<T>& v) {
	return v.empty() ? T{} : v.back();
}

// core.ElementOrNil
template <class T>
T elementOrNil(const std::vector<T>& v, int i) {
	return i >= 0 && i < static_cast<int>(v.size()) ? v[i] : T{};
}

template <class T>
bool containsVec(const std::vector<T>& v, const T& x) {
	return std::find(v.begin(), v.end(), x) != v.end();
}

// ---------------------------------------------------------------------------
// ast.* helpers missing from internal/ast (file-local copies)
// ---------------------------------------------------------------------------

// utilities.go:319 — IsLiteralKind
bool isLiteralKind(Kind kind) {
	return kind >= Kind::NumericLiteral && kind <= Kind::NoSubstitutionTemplateLiteral;
}

// utilities.go:3278 — IsTemplateLiteralKind
bool isTemplateLiteralKind(Kind kind) {
	return kind >= KindFirstTemplateToken && kind <= KindLastTemplateToken;
}

// utilities.go:3795 — IsUnterminatedLiteral
bool isUnterminatedLiteral(Node* node) {
	return isLiteralKind(node->kind) &&
			(*node->literalLikeData().tokenFlags & TokenFlagsUnterminated) != 0 ||
		isTemplateLiteralKind(node->kind) &&
			(*node->templateLiteralLikeData().templateFlags & TokenFlagsUnterminated) != 0;
}

// utilities.go:3801 — IsInitializedProperty
bool isInitializedProperty(Node* member) {
	return member->kind == Kind::PropertyDeclaration && member->initializer() != nullptr;
}

// utilities.go:4589 — IsSuperProperty
bool isSuperProperty(Node* node) {
	return (isPropertyAccessExpression(node) || isElementAccessExpression(node)) &&
		node->expression()->kind == Kind::SuperKeyword;
}

// utilities.go:3009 — IsJsxCallLike
bool isJsxCallLike(Node* node) {
	switch (node->kind) {
	case Kind::JsxOpeningElement:
	case Kind::JsxSelfClosingElement:
	case Kind::JsxOpeningFragment:
		return true;
	default:
		return false;
	}
}

// utilities.go:3764 — GetInvokedExpression
Node* getInvokedExpression(Node* node) {
	switch (node->kind) {
	case Kind::TaggedTemplateExpression:
		return node->as<TaggedTemplateExpression>()->Tag;
	case Kind::JsxOpeningElement:
	case Kind::JsxSelfClosingElement:
		return node->tagName();
	case Kind::BinaryExpression:
		return node->as<BinaryExpression>()->Right;
	case Kind::JsxOpeningFragment:
		return node;
	default:
		return node->expression();
	}
}

// utilities.go — IsProtoSetter
bool isProtoSetter(Node* node) {
	return (isIdentifier(node) || isStringLiteral(node)) && node->text() == "__proto__";
}

// utilities.go:4595 — IsNamedEvaluationSource
bool isNamedEvaluationSource(Node* node) {
	switch (node->kind) {
	case Kind::PropertyAssignment:
		return !isProtoSetter(node->as<PropertyAssignment>()->name);
	case Kind::ShorthandPropertyAssignment:
		return node->as<ShorthandPropertyAssignment>()->ObjectAssignmentInitializer != nullptr;
	case Kind::VariableDeclaration:
		return isIdentifier(node->as<VariableDeclaration>()->name) &&
			node->initializer() != nullptr;
	case Kind::Parameter:
		return isIdentifier(node->as<ParameterDeclaration>()->name) &&
			node->initializer() != nullptr &&
			node->as<ParameterDeclaration>()->DotDotDotToken == nullptr;
	case Kind::BindingElement:
		return isIdentifier(node->as<BindingElement>()->name) &&
			node->initializer() != nullptr &&
			node->as<BindingElement>()->DotDotDotToken == nullptr;
	case Kind::PropertyDeclaration:
		return node->initializer() != nullptr;
	case Kind::BinaryExpression:
		switch (node->as<BinaryExpression>()->OperatorToken->kind) {
		case Kind::EqualsToken:
		case Kind::AmpersandAmpersandEqualsToken:
		case Kind::BarBarEqualsToken:
		case Kind::QuestionQuestionEqualsToken:
			return isIdentifier(node->as<BinaryExpression>()->Left);
		}
		break;
	case Kind::ExportAssignment:
		return true;
	default:
		break;
	}
	return false;
}

// (deduped: local replica of walkUpOuterExpressions removed)

// utilities.go:2994 — getClassLikeDeclarationOfSymbol
Node* getClassLikeDeclarationOfSymbol(Symbol* symbol) {
	return findIf(symbol->declarations, isClassLike);
}

// ---------------------------------------------------------------------------
// checker-package helpers (file-local copies — see PORTING.md)
// ---------------------------------------------------------------------------

// utilities.go:1128 — isCallChain
bool isCallChain(Node* node) {
	return isCallExpression(node) && (node->flags & NodeFlagsOptionalChain) != 0;
}

// utilities.go:1136 — isSuperCall
bool isSuperCall(Node* n) {
	return isCallExpression(n) && n->expression()->kind == Kind::SuperKeyword;
}

// (deduped: local replica of isTypeAny removed)

// checker.go:28134 — isConstEnumSymbol
bool isConstEnumSymbol(Symbol* symbol) {
	return (symbol->flags & SymbolFlagsConstEnum) != 0;
}

// checker.go:28130 — isConstEnumObjectType
bool isConstEnumObjectType(Type* t) {
	return (t->objectFlags & ObjectFlagsAnonymous) != 0 && t->symbol != nullptr &&
		isConstEnumSymbol(t->symbol);
}

// utilities.go:63 — getSelectedModifierFlags
ModifierFlags getSelectedModifierFlags(Node* node, ModifierFlags flags) {
	return node->modifierFlags() & flags;
}

// utilities.go:761 — getDeclarationModifierFlagsFromSymbolEx
ModifierFlags getDeclarationModifierFlagsFromSymbolEx(Symbol* s, bool isWrite) {
	if ((s->checkFlags & CheckFlagsSynthetic) != 0) {
		ModifierFlags accessModifier{};
		if (!isWrite && (s->checkFlags & CheckFlagsContainsPublic) != 0 ||
			isWrite && (s->checkFlags & CheckFlagsContainsWritePublic) != 0) {
			accessModifier = ModifierFlagsPublic;
		} else if (!isWrite && (s->checkFlags & CheckFlagsContainsProtected) != 0 ||
				   isWrite && (s->checkFlags & CheckFlagsContainsWriteProtected) != 0) {
			accessModifier = ModifierFlagsProtected;
		} else if (!isWrite && (s->checkFlags & CheckFlagsContainsPrivate) != 0 ||
				   isWrite && (s->checkFlags & CheckFlagsContainsWritePrivate) != 0) {
			accessModifier = ModifierFlagsPrivate;
		}
		if ((s->checkFlags & CheckFlagsContainsStatic) != 0) {
			return accessModifier | ModifierFlagsStatic;
		}
		return accessModifier;
	}
	if (s->valueDeclaration != nullptr) {
		Node* declaration = nullptr;
		if (isWrite) {
			declaration = findIf(s->declarations, isSetAccessorDeclaration);
		}
		if (declaration == nullptr && (s->flags & SymbolFlagsGetAccessor) != 0) {
			declaration = findIf(s->declarations, isGetAccessorDeclaration);
		}
		if (declaration == nullptr) {
			declaration = s->valueDeclaration;
		}
		ModifierFlags flags = getCombinedModifierFlags(declaration);
		if (s->parent != nullptr && (s->parent->flags & SymbolFlagsClass) != 0) {
			return flags;
		}
		return flags & ~ModifierFlagsAccessibilityModifier;
	}
	return ModifierFlagsNone;
}

// utilities.go:757 — getDeclarationModifierFlagsFromSymbol
ModifierFlags getDeclarationModifierFlagsFromSymbol(Symbol* s) {
	return getDeclarationModifierFlagsFromSymbolEx(s, false /*isWrite*/);
}

// checker/utilities.go:89 — getAssignmentTargetKind
AssignmentKind getAssignmentTargetKind(Node* node) {
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
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in getAssignmentTargetKind");
}

// checker.go:17358 — signatureHasRestParameter
bool signatureHasRestParameter(Signature* sig) {
	return (sig->flags & SignatureFlagsHasRestParameter) != 0;
}

// checker.go:23946 — isTupleType
bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 &&
		(t->AsTypeReference()->target->objectFlags & ObjectFlagsTuple) != 0;
}

// checker.go:28258 — isRestParameter
bool isRestParameter(Node* param) {
	return param->as<ParameterDeclaration>()->DotDotDotToken != nullptr;
}

// utilities.go:298 — isOptionalDeclaration
bool isOptionalDeclaration(Node* declaration) {
	return hasQuestionToken(declaration);
}

// checker.go:30597 — isSpreadArgument
bool isSpreadArgument(Node* arg) {
	return isSpreadElement(arg) ||
		(isSyntheticExpression(arg) && arg->as<SyntheticExpression>()->IsSpread);
}

// checker.go:29478 — getMappedTypeModifiers
MappedTypeModifiers getMappedTypeModifiers(Type* t) {
	Node* declaration = t->AsMappedType()->declaration;
	MappedTypeModifiers modifiers{};
	if (declaration->as<MappedTypeNode>()->ReadonlyToken != nullptr) {
		modifiers |= declaration->as<MappedTypeNode>()->ReadonlyToken->kind ==
							 Kind::MinusToken
						 ? MappedTypeModifiersExcludeReadonly
						 : MappedTypeModifiersIncludeReadonly;
	}
	if (declaration->as<MappedTypeNode>()->QuestionToken != nullptr) {
		modifiers |= declaration->as<MappedTypeNode>()->QuestionToken->kind ==
							 Kind::MinusToken
						 ? MappedTypeModifiersExcludeOptional
						 : MappedTypeModifiersIncludeOptional;
	}
	return modifiers;
}

// (deduped: local replica of isPropertyName removed)

// (deduped: local replica of tryGetPropertyAccessOrIdentifierToString removed)

// utilities.go:1319 — minAndMax
template <class T, class F>
std::pair<int, int> minAndMax(const std::vector<T>& slice, F getValue) {
	int minValue = 0, maxValue = 0;
	for (size_t i = 0; i < slice.size(); i++) {
		int value = getValue(slice[i]);
		if (i == 0) {
			minValue = value;
			maxValue = value;
		} else {
			minValue = std::min(minValue, value);
			maxValue = std::max(maxValue, value);
		}
	}
	return {minValue, maxValue};
}

// utilities.go — getNonRestParameterCount
int getNonRestParameterCount(Signature* sig) {
	return static_cast<int>(sig->parameters.size()) -
		ifElse(signatureHasRestParameter(sig), 1, 0);
}

// inference.go:1651 — hasInferenceCandidates
bool hasInferenceCandidates(InferenceInfo* info) {
	return !info->candidates.empty() || !info->contraCandidates.empty();
}

// utilities.go:4238 — HasContextSensitiveParameters (ast pkg; file-local copy)
bool hasContextSensitiveParameters(Node* node) {
	if (node->typeParameterList() == nullptr) {
		if (someOf(node->parameters(), [](Node* p) { return p->type() == nullptr; })) {
			return true;
		}
		if (!isArrowFunction(node)) {
			Node* parameter = firstOrNil(node->parameters());
			if (parameter == nullptr || !isThisParameter(parameter)) {
				return (node->flags & NodeFlagsContainsThis) != 0;
			}
		}
	}
	return false;
}

// checker.go:8289 — isSpreadIntoCallOrNew
bool isSpreadIntoCallOrNew(Node* node) {
	Node* parent = walkUpParenthesizedExpressions(node->parent);
	return isSpreadElement(parent) && isCallOrNewExpression(parent->parent);
}

// checker.go:8894 — someSignature
bool someSignature(const std::vector<Signature*>& signatures,
				   const std::function<bool(Signature*)>& f) {
	for (Signature* sig : signatures) {
		if ((sig->composite != nullptr && sig->composite->isUnion &&
			 someOf(sig->composite->signatures, f)) ||
			(sig->composite == nullptr && f(sig))) {
			return true;
		}
	}
	return false;
}

// checker.go:9195 — signatureHasLiteralTypes
bool signatureHasLiteralTypes(Signature* s) {
	return (s->flags & SignatureFlagsHasLiteralTypes) != 0;
}

// checker.go:9371 — acceptsVoid
bool acceptsVoid(Type* t) {
	return (t->flags & TypeFlagsVoid) != 0;
}

// checker.go:10035 — getErrorNodeForCallNode
Node* getErrorNodeForCallNode(Node* node) {
	if (isCallExpression(node)) {
		node = node->expression();
		if (isPropertyAccessExpression(node)) {
			node = node->name();
		}
	}
	return node;
}

} // namespace

// ---------------------------------------------------------------------------
// checker.go:8090-10491 — slice "expr_a" ports
// ---------------------------------------------------------------------------

// checker.go:8136 — isInConstructorArgumentInitializer
bool Checker::isInConstructorArgumentInitializer(Node* node, Node* constructorDecl) {
	return findAncestorOrQuit(node, [constructorDecl](Node* n) -> FindAncestorResult {
		if (isFunctionLikeDeclaration(n)) {
			return FindAncestorResult::Quit;
		}
		if (isParameterDeclaration(n) && n->parent == constructorDecl) {
			return FindAncestorResult::True;
		}
		return FindAncestorResult::False;
	}) != nullptr;
}

// checker.go:8148 — checkTemplateExpression
Type* Checker::checkTemplateExpression(Node* node) {
	TemplateExpression* expr = node->as<TemplateExpression>();
	int length = static_cast<int>(expr->TemplateSpans->nodes.size());
	std::vector<std::string> texts(length + 1);
	std::vector<Type*> types(length);
	texts[0] = expr->Head->text();
	for (int i = 0; i < length; i++) {
		Node* span = expr->TemplateSpans->nodes[i];
		Type* t = checkExpression(span->expression());
		if (maybeTypeOfKindConsideringBaseConstraint(t, TypeFlagsESSymbolLike)) {
			error(span->expression(),
				  Implicit_conversion_of_a_symbol_to_a_string_will_fail_at_runtime_Consider_wrapping_this_expression_in_String);
		}
		texts[i + 1] = span->as<TemplateSpan>()->Literal->text();
		types[i] = ifElse(isTypeAssignableTo(t, templateConstraintType), t, stringType);
	}
	std::variant<std::monostate, std::string, Number, bool, PseudoBigInt> evaluated{};
	if (!isTaggedTemplateExpression(node->parent)) {
		evaluated = evaluate(node, node).Value;
	}
	if (auto* s = std::get_if<std::string>(&evaluated); s != nullptr) {
		return getFreshTypeOfLiteralType(getStringLiteralType(*s));
	}
	if (isConstContext(node) || isTemplateLiteralContext(node) ||
		someType(orElse(getContextualType(node, ContextFlagsNone), unknownType),
				 [this](Type* t) { return isTemplateLiteralContextualType(t); })) {
		return getTemplateLiteralType(texts, types);
	}
	return stringType;
}

// checker.go:8175 — isTemplateLiteralContext
bool Checker::isTemplateLiteralContext(Node* node) {
	Node* parent = node->parent;
	return (isParenthesizedExpression(parent) && isTemplateLiteralContext(parent)) ||
		(isElementAccessExpression(parent) &&
		 parent->as<ElementAccessExpression>()->ArgumentExpression == node);
}

// checker.go:8180 — isTemplateLiteralContextualType
bool Checker::isTemplateLiteralContextualType(Type* t) {
	return (t->flags & (TypeFlagsStringLiteral | TypeFlagsTemplateLiteral)) != 0 ||
		((t->flags & TypeFlagsInstantiableNonPrimitive) != 0 &&
		 maybeTypeOfKind(orElse(getBaseConstraintOfType(t), unknownType),
						 TypeFlagsStringLike));
}

// checker.go:8184 — checkRegularExpressionLiteral
Type* Checker::checkRegularExpressionLiteral(Node* node) {
	NodeLinks* links = nodeLinks.Get(node);
	if ((links->flags & NodeCheckFlagsTypeChecked) == 0) {
		links->flags |= NodeCheckFlagsTypeChecked;
		checkGrammarRegularExpressionLiteral(node->as<RegularExpressionLiteral>());
	}
	return globalRegExpType;
}

// checker.go:8193 — checkArrayLiteral
Type* Checker::checkArrayLiteral(Node* node, CheckMode checkMode) {
	const std::vector<Node*>& elements = node->elements();
	size_t n = elements.size();
	std::vector<Type*> elementTypes(n);
	std::vector<TupleElementInfo> elementInfos(n);
	pushCachedContextualType(node);
	bool inDestructuringPattern = isAssignmentTarget(node);
	bool inConstContext = isConstContext(node);
	Type* contextualType = getApparentTypeOfContextualType(node, ContextFlagsNone);
	bool inTupleContext =
		isSpreadIntoCallOrNew(node) ||
		(contextualType != nullptr && someType(contextualType, [this](Type* t) {
			return isTupleLikeType(t) ||
				(isGenericMappedType(t) &&
				 t->AsMappedType()->nameType == nullptr &&
				 getHomomorphicTypeVariable(orElse(t->AsMappedType()->target, t)) !=
					 nullptr);
		}));
	bool hasOmittedExpression = false;
	for (size_t i = 0; i < n; i++) {
		Node* e = elements[i];
		if (isSpreadElement(e)) {
			Type* spreadType = checkExpressionEx(e->expression(), checkMode);
			if (isArrayLikeType(spreadType)) {
				elementTypes[i] = spreadType;
				elementInfos[i] = TupleElementInfo{ElementFlagsVariadic, nullptr};
			} else if (inDestructuringPattern) {
				// Given the following situation:
				//    var c: {};
				//    [...c] = ["", 0];
				//
				// c is represented in the tree as a spread element in an array literal.
				// But c really functions as a rest element, and its purpose is to provide
				// a contextual type for the right hand side of the assignment. Therefore,
				// instead of calling checkExpression on "...c", which will give an error
				// if c is not iterable/array-like, we need to act as if we are trying to
				// get the contextual element type from it. So we do something similar to
				// getContextualTypeForElementExpression, which will crucially not error
				// if there is no index type / iterated type.
				Type* restElementType = getIndexTypeOfType(spreadType, numberType);
				if (restElementType == nullptr) {
					restElementType = getIteratedTypeOrElementType(
						IterationUseDestructuring, spreadType, undefinedType,
						nullptr /*errorNode*/, false /*checkAssignability*/);
					if (restElementType == nullptr) {
						restElementType = unknownType;
					}
				}
				elementTypes[i] = restElementType;
				elementInfos[i] = TupleElementInfo{ElementFlagsRest, nullptr};
			} else {
				elementTypes[i] = checkIteratedTypeOrElementType(
					IterationUseSpread, spreadType, undefinedType, e->expression());
				elementInfos[i] = TupleElementInfo{ElementFlagsRest, nullptr};
			}
		} else if (exactOptionalPropertyTypes && isOmittedExpression(e)) {
			hasOmittedExpression = true;
			elementTypes[i] = undefinedOrMissingType;
			elementInfos[i] = TupleElementInfo{ElementFlagsOptional, nullptr};
		} else {
			Type* t = checkExpressionForMutableLocation(e, checkMode);
			elementTypes[i] =
				addOptionalityEx(t, true /*isProperty*/, hasOmittedExpression);
			elementInfos[i] = TupleElementInfo{
				ifElse(hasOmittedExpression, ElementFlagsOptional,
					   ElementFlagsRequired),
				nullptr};
			if (inTupleContext && (checkMode & CheckModeInferential) != 0 &&
				(checkMode & CheckModeSkipContextSensitive) == 0 &&
				isContextSensitive(e)) {
				InferenceContext* inferenceContext = getInferenceContext(node);
				// In CheckMode.Inferential we should always have an inference context
				addIntraExpressionInferenceSite(inferenceContext, e, t);
			}
		}
	}
	popContextualType();
	if (inDestructuringPattern) {
		return createTupleTypeEx(elementTypes, elementInfos, false);
	}
	if ((checkMode & CheckModeForceTuple) != 0 || inConstContext || inTupleContext) {
		return createArrayLiteralType(createTupleTypeEx(
			elementTypes, elementInfos,
			inConstContext &&
				!(contextualType != nullptr &&
				  someType(contextualType,
						   [this](Type* t) { return isMutableArrayLikeType(t); })) /*readonly*/));
	}
	Type* elementType;
	if (!elementTypes.empty()) {
		for (size_t i = 0; i < elementTypes.size(); i++) {
			if ((elementInfos[i].flags & ElementFlagsVariadic) != 0) {
				elementTypes[i] = orElse(
					getIndexedAccessTypeOrUndefined(elementTypes[i], numberType,
													AccessFlagsNone, nullptr,
													nullptr),
					anyType);
			}
		}
		elementType = getUnionTypeEx(elementTypes, UnionReductionSubtype, nullptr, nullptr);
	} else {
		elementType = ifElse(strictNullChecks, implicitNeverType, undefinedWideningType);
	}
	return createArrayLiteralType(createArrayTypeEx(elementType, inConstContext));
}

// checker.go:8275 — createArrayLiteralType
Type* Checker::createArrayLiteralType(Type* t) {
	if ((t->objectFlags & ObjectFlagsReference) == 0) {
		return t;
	}
	CachedTypeKey key{CachedTypeKind::ArrayLiteralType, t->id};
	if (Type* cached = cachedTypes[key]; cached != nullptr) {
		return cached;
	}
	Type* literalType = cloneTypeReference(t);
	literalType->objectFlags |=
		ObjectFlagsArrayLiteral | ObjectFlagsContainsObjectOrArrayLiteral;
	cachedTypes[key] = literalType;
	return literalType;
}

// checker.go:8294 — checkQualifiedName
Type* Checker::checkQualifiedName(Node* node, CheckMode checkMode) {
	Node* left = node->as<QualifiedName>()->Left;
	Type* leftType;
	if (isPartOfTypeQuery(node) && isThisIdentifier(left)) {
		leftType = checkNonNullType(checkThisExpression(left), left);
	} else {
		leftType = checkNonNullExpression(left);
	}
	return checkPropertyAccessExpressionOrQualifiedName(
		node, left, leftType, node->as<QualifiedName>()->Right, checkMode, false);
}

// checker.go:8305 — checkIndexedAccess
Type* Checker::checkIndexedAccess(Node* node, CheckMode checkMode) {
	if ((node->flags & NodeFlagsOptionalChain) != 0) {
		return checkElementAccessChain(node, checkMode);
	}
	return checkElementAccessExpression(node, checkNonNullExpression(node->expression()),
										checkMode);
}

// checker.go:8312 — checkElementAccessChain
Type* Checker::checkElementAccessChain(Node* node, CheckMode checkMode) {
	Type* exprType = checkExpression(node->expression());
	Type* nonOptionalType = getOptionalExpressionType(exprType, node->expression());
	return propagateOptionalTypeMarker(
		checkElementAccessExpression(
			node, checkNonNullType(nonOptionalType, node->expression()), checkMode),
		node, nonOptionalType != exprType);
}

// checker.go:8318 — checkElementAccessExpression
Type* Checker::checkElementAccessExpression(Node* node, Type* exprType,
											CheckMode checkMode) {
	Type* objectType = exprType;
	if (getAssignmentTargetKind(node) != AssignmentKind::None ||
		isMethodAccessForCall(node)) {
		objectType = getWidenedType(objectType);
	}
	Node* indexExpression = node->as<ElementAccessExpression>()->ArgumentExpression;
	Type* indexType = checkExpression(indexExpression);
	if (isErrorType(objectType) || objectType == silentNeverType) {
		return objectType;
	}
	if (isConstEnumObjectType(objectType) && !isStringLiteralLike(indexExpression)) {
		error(indexExpression,
			  A_const_enum_member_can_only_be_accessed_using_a_string_literal);
		return errorType;
	}
	Type* effectiveIndexType = indexType;
	if (isForInVariableForNumericPropertyNames(indexExpression)) {
		effectiveIndexType = numberType;
	}
	AssignmentKind assignmentTargetKind = getAssignmentTargetKind(node);
	AccessFlags accessFlags;
	if (assignmentTargetKind == AssignmentKind::None) {
		accessFlags = AccessFlagsExpressionPosition;
	} else {
		accessFlags = AccessFlagsWriting |
			ifElse(assignmentTargetKind == AssignmentKind::Compound,
				   AccessFlagsExpressionPosition, AccessFlagsNone) |
			ifElse(isGenericObjectType(objectType) && !isThisTypeParameter(objectType),
				   AccessFlagsNoIndexSignatures, AccessFlagsNone);
	}
	Type* indexedAccessType =
		orElse(getIndexedAccessTypeOrUndefined(objectType, effectiveIndexType,
											   accessFlags, node, nullptr),
			   errorType);
	return checkIndexedAccessIndexType(
		getFlowTypeOfAccessExpression(node, getResolvedSymbolOrNil(node),
									  indexedAccessType, indexExpression,
									  checkMode),
		node);
}

// checker.go:8351 — isForInVariableForNumericPropertyNames
bool Checker::isForInVariableForNumericPropertyNames(Node* expr) {
	Node* e = skipParentheses(expr);
	if (isIdentifier(e)) {
		Symbol* symbol = getResolvedSymbol(e);
		if ((symbol->flags & SymbolFlagsVariable) != 0) {
			Node* child = expr;
			Node* node = expr->parent;
			while (node != nullptr) {
				if (isForInStatement(node) &&
					child == node->as<ForInOrOfStatement>()->Statement &&
					getForInVariableSymbol(node) == symbol &&
					hasNumericPropertyNames(getTypeOfExpression(node->expression()))) {
					return true;
				}
				child = node;
				node = node->parent;
			}
		}
	}
	return false;
}

// checker.go:8371 — getForInVariableSymbol
Symbol* Checker::getForInVariableSymbol(Node* node) {
	Node* initializer = node->initializer();
	if (isVariableDeclarationList(initializer)) {
		const std::vector<Node*>& declarations =
			initializer->as<VariableDeclarationList>()->Declarations->nodes;
		if (!declarations.empty()) {
			Node* variable = declarations[0];
			if (variable != nullptr && !isBindingPattern(variable->name())) {
				return getSymbolOfDeclaration(variable);
			}
		}
	} else if (isIdentifier(initializer)) {
		return getResolvedSymbol(initializer);
	}
	return nullptr;
}

// checker.go:8388 — hasNumericPropertyNames
bool Checker::hasNumericPropertyNames(Type* t) {
	return getIndexInfosOfType(t).size() == 1 &&
		getIndexInfoOfType(t, numberType) != nullptr;
}

// checker.go:8392 — checkIndexedAccessIndexType
Type* Checker::checkIndexedAccessIndexType(Type* t, Node* accessNode) {
	if ((t->flags & TypeFlagsIndexedAccess) == 0) {
		return t;
	}
	// Check if the index type is assignable to 'keyof T' for the object type.
	Type* objectType = t->AsIndexedAccessType()->objectType;
	Type* indexType = t->AsIndexedAccessType()->indexType;
	// skip index type deferral on remapping mapped types
	Type* objectIndexType;
	if (isGenericMappedType(objectType) &&
		getMappedTypeNameTypeKind(objectType) == MappedTypeNameTypeKind::Remapping) {
		objectIndexType = getIndexTypeForMappedType(objectType, IndexFlagsNone);
	} else {
		objectIndexType = getIndexTypeEx(objectType, IndexFlagsNone);
	}
	bool hasNumberIndexInfo = getIndexInfoOfType(objectType, numberType) != nullptr;
	if (everyType(indexType, [this, objectIndexType, hasNumberIndexInfo](Type* t) {
			return isTypeAssignableTo(t, objectIndexType) ||
				(hasNumberIndexInfo && isApplicableIndexType(t, numberType));
		})) {
		if (accessNode->kind == Kind::ElementAccessExpression &&
			isAssignmentTarget(accessNode) &&
			(objectType->objectFlags & ObjectFlagsMapped) != 0 &&
			(getMappedTypeModifiers(objectType) &
			 MappedTypeModifiersIncludeReadonly) != 0) {
			error(accessNode, Index_signature_in_type_0_only_permits_reading,
				  {TypeToString(objectType)});
		}
		return t;
	}
	if (isGenericObjectType(objectType)) {
		std::string propertyName = getPropertyNameFromIndex(indexType, accessNode);
		if (propertyName != InternalSymbolNameMissing) {
			Symbol* propertySymbol = getConstituentProperty(objectType, propertyName);
			if (propertySymbol != nullptr &&
				(getDeclarationModifierFlagsFromSymbol(propertySymbol) &
				 ModifierFlagsNonPublicAccessibilityModifier) != 0) {
				error(accessNode,
					  Private_or_protected_member_0_cannot_be_accessed_on_a_type_parameter,
					  {propertyName});
				return errorType;
			}
		}
	}
	error(accessNode, Type_0_cannot_be_used_to_index_type_1,
		  {TypeToString(indexType), TypeToString(objectType)});
	return errorType;
}

// checker.go:8429 — getConstituentProperty
Symbol* Checker::getConstituentProperty(Type* objectType,
										const std::string& propertyName) {
	for (Type* t : getApparentType(objectType)->Distributed()) {
		Symbol* prop = getPropertyOfType(t, propertyName);
		if (prop != nullptr) {
			return prop;
		}
	}
	return nullptr;
}

// checker.go:8439 — checkImportCallExpression
Type* Checker::checkImportCallExpression(Node* node) {
	// Check grammar of dynamic import
	checkGrammarImportCallExpression(node);
	const std::vector<Node*>& args = node->arguments();
	if (args.empty()) {
		// No call arguments exist, so there are no child expressions to check.
		return createPromiseReturnType(node, anyType); // no arguments to check
	}
	Node* specifier = args[0];
	Type* specifierType = checkExpressionCached(specifier);
	Type* optionsType = nullptr;
	if (args.size() > 1) {
		optionsType = checkExpressionCached(args[1]);
	}
	// Even though multiple arguments is grammatically incorrect, type-check extra arguments for completion
	for (size_t i = 2; i < args.size(); i++) {
		checkExpressionCached(args[i]);
	}
	if ((specifierType->flags & TypeFlagsNullable) != 0 ||
		!isTypeAssignableTo(specifierType, stringType)) {
		error(specifier,
			  Dynamic_import_s_specifier_must_be_of_type_string_but_here_has_type_0,
			  {TypeToString(specifierType)});
	}
	Type* importAttributesType = nullptr;
	if (optionsType != nullptr) {
		Type* importCallOptionsType = getGlobalImportCallOptionsTypeChecked();
		if (importCallOptionsType != emptyObjectType) {
			checkTypeAssignableTo(
				optionsType,
				getNullableType(importCallOptionsType, TypeFlagsUndefined),
				args[1], nullptr);
		}
		if (isObjectLiteralExpression(args[1])) {
			for (Node* prop :
				 args[1]->as<ObjectLiteralExpression>()->Properties->nodes) {
				if (isPropertyAssignment(prop) && isIdentifier(prop->name()) &&
					prop->name()->text() == "assert") {
					error(prop->name(),
						  Import_assertions_have_been_replaced_by_import_attributes_Use_with_instead_of_assert);
					break;
				}
			}
		}
		importAttributesType = getTypeOfPropertyOfType(optionsType, "with");
	}
	// resolveExternalModuleName will return undefined if the moduleReferenceExpression is not a string literal
	Symbol* moduleSymbol = resolveExternalModuleName(node, specifier,
												   false /*ignoreErrors*/,
												   importAttributesType);
	if (moduleSymbol != nullptr) {
		Symbol* esModuleSymbol =
			resolveExternalModuleSymbol(moduleSymbol, true /*dontResolveAlias*/);
		if (esModuleSymbol != nullptr) {
			Type* syntheticType = getTypeWithSyntheticDefaultOnly(
				getTypeOfSymbol(esModuleSymbol), esModuleSymbol, moduleSymbol,
				specifier, importAttributesType);
			if (syntheticType == nullptr) {
				syntheticType = getTypeWithSyntheticDefaultImportType(
					getTypeOfSymbol(esModuleSymbol), esModuleSymbol, moduleSymbol,
					specifier);
			}
			return createPromiseReturnType(node, syntheticType);
		}
	}
	return createPromiseReturnType(node, anyType);
}

// checker.go:8496 — checkCallExpression
Type* Checker::checkCallExpression(Node* node, CheckMode checkMode) {
	checkGrammarTypeArguments(node, node->typeArgumentList());
	Signature* signature =
		getResolvedSignature(node, nullptr /*candidatesOutArray*/, checkMode);
	if (signature == resolvingSignature) {
		// CheckMode.SkipGenericFunctions is enabled and this is a call to a generic function that
		// returns a function type. We defer checking and return silentNeverType.
		return silentNeverType;
	}
	checkDeprecatedSignature(signature, node);
	if (node->expression()->kind == Kind::SuperKeyword) {
		return voidType;
	}
	if (isNewExpression(node)) {
		Node* declaration = signature->declaration;
		if (declaration != nullptr && !isConstructorDeclaration(declaration) &&
			!isConstructSignatureDeclaration(declaration) &&
			!isConstructorTypeNode(declaration)) {
			// When resolved signature is a call signature (and not a construct signature) the result type is any
			if (noImplicitAny) {
				error(node,
					  X_new_expression_whose_target_lacks_a_construct_signature_implicitly_has_an_any_type);
			}
			return anyType;
		}
	}
	if (isInJSFile(node) && isCommonJSRequire(node)) {
		return resolveExternalModuleTypeByLiteral(node->arguments()[0]);
	}
	Type* returnType = getReturnTypeOfSignature(signature);
	// Treat any call to the global 'Symbol' function that is part of a const variable or readonly property
	// as a fresh unique symbol literal type.
	if ((returnType->flags & TypeFlagsESSymbolLike) != 0 &&
		isSymbolOrSymbolForCall(node)) {
		return getESSymbolLikeTypeForNode(walkUpParenthesizedExpressions(node->parent));
	}
	if (isCallExpression(node) && node->questionDotToken() == nullptr &&
		isExpressionStatement(node->parent) &&
		(returnType->flags & TypeFlagsVoid) != 0 &&
		getTypePredicateOfSignature(signature) != nullptr) {
		if (!isDottedName(node->expression())) {
			error(node->expression(),
				  Assertions_require_the_call_target_to_be_an_identifier_or_qualified_name);
		} else if (getEffectsSignature(node) == nullptr) {
			Diagnostic* diagnostic = error(
				node->expression(),
				Assertions_require_every_name_in_the_call_target_to_be_declared_with_an_explicit_type_annotation);
			getTypeOfDottedName(node->expression(), diagnostic);
		}
	}
	return returnType;
}

// (deduped: checkDeprecatedSignature defined in owning slice file)

// checker.go:8549 — addDeprecatedSuggestionWithSignature
Diagnostic* Checker::addDeprecatedSuggestionWithSignature(
	Node* location, Node* declaration, const std::string& deprecatedEntity,
	const std::string& signatureString) {
	const DiagnosticMessage* message =
		ifElse(!deprecatedEntity.empty(), The_signature_0_of_1_is_deprecated,
			   X_0_is_deprecated);
	Diagnostic* diagnostic = NewDiagnosticForNode(location, message,
												  {signatureString, deprecatedEntity});
	return addDeprecatedSuggestionWorker({declaration}, diagnostic);
}

// checker.go:8555 — isSymbolOrSymbolForCall
bool Checker::isSymbolOrSymbolForCall(Node* node) {
	if (!isCallExpression(node)) {
		return false;
	}
	Node* left = node->expression();
	if (isPropertyAccessExpression(left) && left->name()->text() == "for") {
		left = left->expression();
	}
	if (!isIdentifier(left) || left->text() != "Symbol") {
		return false;
	}
	// make sure `Symbol` is the global symbol
	Symbol* globalESSymbol = getGlobalESSymbolConstructorSymbolOrNil();
	if (globalESSymbol == nullptr) {
		return false;
	}
	return globalESSymbol == resolveName(left, "Symbol", SymbolFlagsValue,
										 nullptr /*nameNotFoundMessage*/,
										 false /*isUse*/, false);
}

// checker.go:8581 — getResolvedSignature
Signature* Checker::getResolvedSignature(Node* node,
										 std::vector<Signature*>* candidatesOutArray,
										 CheckMode checkMode) {
	SignatureLinks* links = signatureLinks.Get(node);
	// If getResolvedSignature has already been called, we will have cached the resolvedSignature.
	// However, it is possible that either candidatesOutArray was not passed in the first time,
	// or that a different candidatesOutArray was passed in. Therefore, we need to redo the work
	// to correctly fill the candidatesOutArray.
	Signature* cached = links->resolvedSignature;
	if (cached != nullptr && cached != resolvingSignature &&
		candidatesOutArray == nullptr) {
		return cached;
	}
	int saveResolutionStart = resolutionStart;
	if (cached == nullptr) {
		// If we haven't already done so, temporarily reset the resolution stack. This allows us to
		// handle "inverted" situations where, for example, an API client asks for the type of a symbol
		// containined in a function call argument whose contextual type depends on the symbol itself
		// through resolution of the containing function call. By resetting the resolution stack we'll
		// retry the symbol type resolution with the resolvingSignature marker in place to suppress
		// the contextual type circularity.
		resolutionStart = static_cast<int>(typeResolutions.size());
	}
	links->resolvedSignature = resolvingSignature;
	Signature* result = resolveSignature(node, candidatesOutArray, checkMode);
	resolutionStart = saveResolutionStart;
	// When CheckMode.SkipGenericFunctions is set we use resolvingSignature to indicate that call
	// resolution should be deferred.
	if (result != resolvingSignature) {
		// if the signature resolution originated on a node that itself depends on the contextual type
		// then it's possible that the resolved signature might not be the same as the one that would be computed in source order
		// since resolving such signature leads to resolving the potential outer signature, its arguments and thus the very same signature
		// it's possible that this inner resolution sets the resolvedSignature first.
		// In such a case we ignore the local result and reuse the correct one that was cached.
		if (links->resolvedSignature != resolvingSignature) {
			result = links->resolvedSignature;
		}
		// If signature resolution originated in control flow type analysis (for example to compute the
		// assigned type in a flow assignment) we don't cache the result as it may be based on temporary
		// types from the control flow analysis.
		if (flowLoopStack.empty()) {
			links->resolvedSignature = result;
		} else {
			links->resolvedSignature = cached;
		}
	}
	return result;
}

// checker.go:8627 — resolveSignature
Signature* Checker::resolveSignature(Node* node,
									 std::vector<Signature*>* candidatesOutArray,
									 CheckMode checkMode) {
	switch (node->kind) {
	case Kind::CallExpression:
		return resolveCallExpression(node, candidatesOutArray, checkMode);
	case Kind::NewExpression:
		return resolveNewExpression(node, candidatesOutArray, checkMode);
	case Kind::TaggedTemplateExpression:
		return resolveTaggedTemplateExpression(node, candidatesOutArray, checkMode);
	case Kind::Decorator:
		return resolveDecorator(node, candidatesOutArray, checkMode);
	case Kind::JsxOpeningFragment:
	case Kind::JsxOpeningElement:
	case Kind::JsxSelfClosingElement:
		return resolveJsxOpeningLikeElement(node, candidatesOutArray, checkMode);
	case Kind::BinaryExpression:
		return resolveInstanceofExpression(node, candidatesOutArray, checkMode);
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in resolveSignature");
}

// checker.go:8645 — resolveCallExpression
Signature* Checker::resolveCallExpression(Node* node,
										  std::vector<Signature*>* candidatesOutArray,
										  CheckMode checkMode) {
	if (node->expression()->kind == Kind::SuperKeyword) {
		Type* superType = checkSuperExpression(node->expression());
		if (isTypeAny(superType)) {
			for (Node* arg : node->arguments()) {
				// Still visit arguments so they get marked for visibility, etc
				checkExpression(arg);
			}
			return anySignature;
		}
		if (!isErrorType(superType)) {
			// In super call, the candidate signatures are the matching arity signatures of the base constructor function instantiated
			// with the type arguments specified in the extends clause.
			Node* baseTypeNode =
				getClassExtendsHeritageElement(getContainingClass(node));
			if (baseTypeNode != nullptr) {
				std::vector<Signature*> baseConstructors =
					getInstantiatedConstructorsForTypeArguments(
						superType, baseTypeNode->typeArguments(), baseTypeNode);
				return resolveCall(node, baseConstructors, candidatesOutArray,
								   checkMode, SignatureFlagsNone, nullptr);
			}
		}
		return resolveUntypedCall(node);
	}
	if (isImportCall(node)) {
		return resolveUntypedCall(node);
	}
	SignatureFlags callChainFlags;
	Type* funcType = checkExpression(node->expression());
	if (isCallChain(node)) {
		Type* nonOptionalType =
			getOptionalExpressionType(funcType, node->expression());
		if (nonOptionalType == funcType) {
			callChainFlags = SignatureFlagsNone;
		} else if (isOutermostOptionalChain(node)) {
			callChainFlags = SignatureFlagsIsOuterCallChain;
		} else {
			callChainFlags = SignatureFlagsIsInnerCallChain;
		}
		funcType = nonOptionalType;
	} else {
		callChainFlags = SignatureFlagsNone;
	}
	funcType = checkNonNullTypeWithReporter(
		funcType, node->expression(),
		[](Checker* c, Node* n, TypeFacts facts) {
			c->reportCannotInvokePossiblyNullOrUndefinedError(n, facts);
		});
	if (funcType == silentNeverType) {
		return silentNeverSignature;
	}
	Type* apparentType = getApparentType(funcType);
	if (isErrorType(apparentType)) {
		// Another error has already been reported
		return resolveErrorCall(node);
	}
	// Technically, this signatures list may be incomplete. We are taking the apparent type,
	// but we are not including call signatures that may have been added to the Object or
	// Function interface, since they have none by default. This is a bit of a leap of faith
	// that the user will not add any.
	std::vector<Signature*> callSignatures =
		getSignaturesOfType(apparentType, SignatureKind::Call);
	int numConstructSignatures = static_cast<int>(
		getSignaturesOfType(apparentType, SignatureKind::Construct).size());
	// TS 1.0 Spec: 4.12
	// In an untyped function call no TypeArgs are permitted, Args can be any argument list, no contextual
	// types are provided for the argument expressions, and the result is always of type Any.
	if (isUntypedFunctionCall(funcType, apparentType,
							  static_cast<int>(callSignatures.size()),
							  numConstructSignatures)) {
		// The unknownType indicates that an error already occurred (and was reported).  No
		// need to report another error in this case.
		if (!isErrorType(funcType) && !node->typeArguments().empty()) {
			error(node, Untyped_function_calls_may_not_accept_type_arguments);
		}
		return resolveUntypedCall(node);
	}
	// If FuncExpr's apparent type(section 3.8.1) is a function type, the call is a typed function call.
	// TypeScript employs overload resolution in typed function calls in order to support functions
	// with multiple call signatures.
	if (callSignatures.empty()) {
		if (numConstructSignatures != 0) {
			error(node,
				  Value_of_type_0_is_not_callable_Did_you_mean_to_include_new,
				  {TypeToString(funcType)});
		} else {
			Diagnostic* relatedInformation = nullptr;
			if (node->arguments().size() == 1) {
				const std::string& text = getSourceFileOfNode(node)->text;
				SkipTriviaOptions options{};
				options.stopAfterLineBreak = true;
				if (isLineBreak(
						text[skipTriviaEx(text, node->expression()->end(), options) -
							 1])) {
					relatedInformation = createDiagnosticForNode(
						node->expression(), Are_you_missing_a_semicolon);
				}
			}
			invocationError(node->expression(), apparentType, SignatureKind::Call,
							relatedInformation);
		}
		return resolveErrorCall(node);
	}
	// When a call to a generic function is an argument to an outer call to a generic function for which
	// inference is in process, we have a choice to make. If the inner call relies on inferences made from
	// its contextual type to its return type, deferring the inner call processing allows the best possible
	// contextual type to accumulate. But if the outer call relies on inferences made from the return type of
	// the inner call, the inner call should be processed early. There's no sure way to know which choice is
	// right (only a full unification algorithm can determine that), so we resort to the following heuristic:
	// If no type arguments are specified in the inner call and at least one call signature is generic and
	// returns a function type, we choose to defer processing. This narrowly permits function composition
	// operators to flow inferences through return types, but otherwise processes calls right away. We
	// use the resolvingSignature singleton to indicate that we deferred processing. This result will be
	// propagated out and eventually turned into silentNeverType (a type that is assignable to anything and
	// from which we never make inferences).
	if ((checkMode & CheckModeSkipGenericFunctions) != 0 &&
		node->typeArguments().empty() &&
		someOf(callSignatures, [this](Signature* s) {
			return isGenericFunctionReturningFunction(s);
		})) {
		skippedGenericFunction(node, checkMode);
		return resolvingSignature;
	}
	return resolveCall(node, callSignatures, candidatesOutArray, checkMode,
					   callChainFlags, nullptr);
}

// checker.go:8749 — resolveNewExpression
Signature* Checker::resolveNewExpression(Node* node,
										 std::vector<Signature*>* candidatesOutArray,
										 CheckMode checkMode) {
	Type* expressionType = checkNonNullExpression(node->expression());
	if (expressionType == silentNeverType) {
		return silentNeverSignature;
	}
	// If expressionType's apparent type(section 3.8.1) is an object type with one or
	// more construct signatures, the expression is processed in the same manner as a
	// function call, but using the construct signatures as the initial set of candidate
	// signatures for overload resolution. The result type of the function call becomes
	// the result type of the operation.
	expressionType = getApparentType(expressionType);
	if (isErrorType(expressionType)) {
		// Another error has already been reported
		return resolveErrorCall(node);
	}
	// TS 1.0 spec: 4.11
	// If expressionType is of type Any, Args can be any argument
	// list and the result of the operation is of type Any.
	if (isTypeAny(expressionType)) {
		if (!node->typeArguments().empty()) {
			error(node, Untyped_function_calls_may_not_accept_type_arguments);
		}
		return resolveUntypedCall(node);
	}
	// Technically, this signatures list may be incomplete. We are taking the apparent type,
	// but we are not including construct signatures that may have been added to the Object or
	// Function interface, since they have none by default. This is a bit of a leap of faith
	// that the user will not add any.
	std::vector<Signature*> constructSignatures =
		getSignaturesOfType(expressionType, SignatureKind::Construct);
	if (!constructSignatures.empty()) {
		ConstructorAccessibilityError* accessibilityError =
			getConstructorAccessibilityError(node, constructSignatures,
											 ModifierFlagsNonPublicAccessibilityModifier);
		if (accessibilityError != nullptr) {
			if ((accessibilityError->kind & ModifierFlagsPrivate) != 0) {
				error(node,
					  Constructor_of_class_0_is_private_and_only_accessible_within_the_class_declaration,
					  {TypeToString(accessibilityError->declaringClass)});
			}
			if ((accessibilityError->kind & ModifierFlagsProtected) != 0) {
				error(node,
					  Constructor_of_class_0_is_protected_and_only_accessible_within_the_class_declaration,
					  {TypeToString(accessibilityError->declaringClass)});
			}
			return resolveErrorCall(node);
		}
		// If the expression is a class of abstract type, or an abstract construct signature,
		// then it cannot be instantiated.
		// In the case of a merged class-module or class-interface declaration,
		// only the class declaration node will have the Abstract flag set.
		if (someSignature(constructSignatures, [](Signature* sig) {
				return (sig->flags & SignatureFlagsAbstract) != 0;
			})) {
			error(node, Cannot_create_an_instance_of_an_abstract_class);
			return resolveErrorCall(node);
		}
		if (expressionType->symbol != nullptr) {
			Node* valueDecl =
				getClassLikeDeclarationOfSymbol(expressionType->symbol);
			if (valueDecl != nullptr &&
				hasModifier(valueDecl, ModifierFlagsAbstract)) {
				error(node, Cannot_create_an_instance_of_an_abstract_class);
				return resolveErrorCall(node);
			}
		}
		return resolveCall(node, constructSignatures, candidatesOutArray,
						   checkMode, SignatureFlagsNone, nullptr);
	}
	// If expressionType's apparent type is an object type with no construct signatures but
	// one or more call signatures, the expression is processed as a function call. A compile-time
	// error occurs if the result of the function call is not Void. The type of the result of the
	// operation is Any. It is an error to have a Void this type.
	std::vector<Signature*> callSignatures =
		getSignaturesOfType(expressionType, SignatureKind::Call);
	if (!callSignatures.empty()) {
		Signature* signature = resolveCall(node, callSignatures,
										   candidatesOutArray, checkMode,
										   SignatureFlagsNone, nullptr);
		if (!noImplicitAny) {
			if (signature->declaration != nullptr &&
				getReturnTypeOfSignature(signature) != voidType) {
				error(node, Only_a_void_function_can_be_called_with_the_new_keyword);
			}
			if (getThisTypeOfSignature(signature) == voidType) {
				error(node,
					  A_function_that_is_called_with_the_new_keyword_cannot_have_a_this_type_that_is_void);
			}
		}
		return signature;
	}
	invocationError(node->expression(), expressionType, SignatureKind::Construct,
					nullptr);
	return resolveErrorCall(node);
}

// checker.go:8834 — getConstructorAccessibilityError
ConstructorAccessibilityError* Checker::getConstructorAccessibilityError(
	Node* node, const std::vector<Signature*>& signatures,
	ModifierFlags modifiersMask) {
	for (Signature* signature : signatures) {
		if (signature->declaration == nullptr) {
			continue;
		}
		Node* declaration = signature->declaration;
		ModifierFlags modifiers =
			getSelectedModifierFlags(declaration, modifiersMask);
		// (1) Public constructors and (2) constructor functions are always accessible.
		if (modifiers == 0 || !isConstructorDeclaration(declaration)) {
			continue;
		}
		Node* declaringClassDeclaration =
			getClassLikeDeclarationOfSymbol(declaration->parent->symbol());
		// A private or protected constructor can only be instantiated within its own class (or a subclass, for protected)
		if (!isNodeWithinClass(node, declaringClassDeclaration)) {
			Node* containingClass = getContainingClass(node);
			if (containingClass != nullptr &&
				(modifiers & ModifierFlagsProtected) != 0) {
				Type* containingType = getTypeOfNode(containingClass);
				if (typeHasProtectedAccessibleBase(declaration->parent->symbol(),
												   containingType)) {
					continue;
				}
			}
			return new ConstructorAccessibilityError{
				modifiers,
				getDeclaredTypeOfSymbol(declaration->parent->symbol())};
		}
	}
	return nullptr;
}

// checker.go:8864 — typeHasProtectedAccessibleBase
bool Checker::typeHasProtectedAccessibleBase(Symbol* target, Type* t) {
	std::vector<Type*> baseTypes = getBaseTypes(getTargetType(t));
	if (baseTypes.empty()) {
		return false;
	}
	Type* firstBase = baseTypes[0];
	if ((firstBase->flags & TypeFlagsIntersection) != 0) {
		std::vector<Type*>& types = firstBase->AsIntersectionType()->types;
		std::vector<bool> mixinFlags = findMixins(types).first;
		const std::vector<Type*>& baseTypesList = firstBase->types();
		for (size_t i = 0; i < baseTypesList.size(); i++) {
			Type* intersectionMember = baseTypesList[i];
			// We want to ignore mixin ctors
			if (!mixinFlags[i]) {
				if ((intersectionMember->objectFlags &
					 (ObjectFlagsClass | ObjectFlagsInterface)) != 0) {
					if (intersectionMember->symbol == target) {
						return true;
					}
					if (typeHasProtectedAccessibleBase(target, intersectionMember)) {
						return true;
					}
				}
			}
		}
		return false;
	}
	if (firstBase->symbol == target) {
		return true;
	}
	return typeHasProtectedAccessibleBase(target, firstBase);
}

// checker.go:8903 — resolveTaggedTemplateExpression
Signature* Checker::resolveTaggedTemplateExpression(
	Node* node, std::vector<Signature*>* candidatesOutArray, CheckMode checkMode) {
	Node* tag = node->as<TaggedTemplateExpression>()->Tag;
	Type* tagType = checkExpression(tag);
	Type* apparentType = getApparentType(tagType);
	if (isErrorType(apparentType)) {
		// Another error has already been reported
		return resolveErrorCall(node);
	}
	std::vector<Signature*> callSignatures =
		getSignaturesOfType(apparentType, SignatureKind::Call);
	int numConstructSignatures = static_cast<int>(
		getSignaturesOfType(apparentType, SignatureKind::Construct).size());
	if (isUntypedFunctionCall(tagType, apparentType,
							  static_cast<int>(callSignatures.size()),
							  numConstructSignatures)) {
		return resolveUntypedCall(node);
	}
	if (callSignatures.empty()) {
		if (isArrayLiteralExpression(node->parent)) {
			error(tag,
				  It_is_likely_that_you_are_missing_a_comma_to_separate_these_two_template_expressions_They_form_a_tagged_template_expression_which_cannot_be_invoked);
			return resolveErrorCall(node);
		}
		invocationError(tag, apparentType, SignatureKind::Call, nullptr);
		return resolveErrorCall(node);
	}
	return resolveCall(node, callSignatures, candidatesOutArray, checkMode,
					   SignatureFlagsNone, nullptr);
}

// checker.go:8927 — resolveDecorator
Signature* Checker::resolveDecorator(Node* node,
									 std::vector<Signature*>* candidatesOutArray,
									 CheckMode checkMode) {
	if (!canHaveDecorators(node->parent)) {
		return resolveErrorCall(node);
	}
	Type* funcType = checkExpression(node->expression());
	Type* apparentType = getApparentType(funcType);
	if (isErrorType(apparentType)) {
		return resolveErrorCall(node);
	}
	std::vector<Signature*> callSignatures =
		getSignaturesOfType(apparentType, SignatureKind::Call);
	int numConstructSignatures = static_cast<int>(
		getSignaturesOfType(apparentType, SignatureKind::Construct).size());
	if (isUntypedFunctionCall(funcType, apparentType,
							  static_cast<int>(callSignatures.size()),
							  numConstructSignatures)) {
		return resolveUntypedCall(node);
	}
	if (isPotentiallyUncalledDecorator(node, callSignatures) &&
		!isParenthesizedExpression(node->expression())) {
		std::string nodeStr = getTextOfNode(node->expression());
		error(node,
			  X_0_accepts_too_few_arguments_to_be_used_as_a_decorator_here_Did_you_mean_to_call_it_first_and_write_0,
			  {nodeStr});
		return resolveErrorCall(node);
	}
	const DiagnosticMessage* headMessage =
		getDiagnosticHeadMessageForDecoratorResolution(node);
	if (callSignatures.empty()) {
		Diagnostic* diag = newDiagnosticChain(
			invocationErrorDetails(node->expression(), apparentType,
								   SignatureKind::Call),
			headMessage);
		diag = addDiagnostic(diag);
		invocationErrorRecovery(apparentType, SignatureKind::Call, diag);
		return resolveErrorCall(node);
	}
	Signature* decoratorSignature = getDecoratorCallSignature(node);
	if (decoratorSignature == nullptr) {
		return resolveErrorCall(node);
	}
	return resolveCall(node, callSignatures, candidatesOutArray, checkMode,
					   SignatureFlagsNone, headMessage);
}

// checker.go:8963 — isPotentiallyUncalledDecorator
bool Checker::isPotentiallyUncalledDecorator(
	Node* decorator, const std::vector<Signature*>& signatures) {
	return !signatures.empty() && everyOf(signatures, [this, decorator](Signature* sig) {
		return sig->minArgumentCount == 0 && !signatureHasRestParameter(sig) &&
			static_cast<int>(sig->parameters.size()) <
				getDecoratorArgumentCount(decorator, sig);
	});
}

// checker.go:8970 — getDiagnosticHeadMessageForDecoratorResolution
const DiagnosticMessage* Checker::getDiagnosticHeadMessageForDecoratorResolution(
	Node* node) {
	switch (node->parent->kind) {
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
		return Unable_to_resolve_signature_of_class_decorator_when_called_as_an_expression;
	case Kind::Parameter:
		return Unable_to_resolve_signature_of_parameter_decorator_when_called_as_an_expression;
	case Kind::PropertyDeclaration:
		return Unable_to_resolve_signature_of_property_decorator_when_called_as_an_expression;
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		return Unable_to_resolve_signature_of_method_decorator_when_called_as_an_expression;
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in getDiagnosticHeadMessageForDecoratorResolution");
}

// checker.go:8984 — resolveInstanceofExpression
Signature* Checker::resolveInstanceofExpression(
	Node* node, std::vector<Signature*>* candidatesOutArray, CheckMode checkMode) {
	// if rightType is an object type with a custom `[Symbol.hasInstance]` method, then it is potentially
	// valid on the right-hand side of the `instanceof` operator. This allows normal `object` types to
	// participate in `instanceof`, as per Step 2 of https://tc39.es/ecma262/#sec-instanceofoperator.
	Node* right = node->as<BinaryExpression>()->Right;
	Type* rightType = checkExpression(right);
	if (!isTypeAny(rightType)) {
		Type* hasInstanceMethodType =
			getSymbolHasInstanceMethodOfObjectType(rightType);
		if (hasInstanceMethodType != nullptr) {
			Type* apparentType = getApparentType(hasInstanceMethodType);
			if (isErrorType(apparentType)) {
				return resolveErrorCall(node);
			}
			std::vector<Signature*> callSignatures =
				getSignaturesOfType(apparentType, SignatureKind::Call);
			std::vector<Signature*> constructSignatures =
				getSignaturesOfType(apparentType, SignatureKind::Construct);
			if (isUntypedFunctionCall(
					hasInstanceMethodType, apparentType,
					static_cast<int>(callSignatures.size()),
					static_cast<int>(constructSignatures.size()))) {
				return resolveUntypedCall(node);
			}
			if (!callSignatures.empty()) {
				return resolveCall(node, callSignatures, candidatesOutArray,
								   checkMode, SignatureFlagsNone, nullptr);
			}
		} else if (!(typeHasCallOrConstructSignatures(rightType) ||
					 isTypeSubtypeOf(rightType, globalFunctionType))) {
			error(right,
				  The_right_hand_side_of_an_instanceof_expression_must_be_either_of_type_any_a_class_function_or_other_type_assignable_to_the_Function_interface_type_or_an_object_type_with_a_Symbol_hasInstance_method);
			return resolveErrorCall(node);
		}
	}
	// fall back to a default signature
	return anySignature;
}

// checker.go:9028 — resolveCall
Signature* Checker::resolveCall(Node* node,
								std::vector<Signature*> signatures,
								std::vector<Signature*>* candidatesOutArray,
								CheckMode checkMode, SignatureFlags callChainFlags,
								const DiagnosticMessage* headMessage) {
	bool isTaggedTemplate = node->kind == Kind::TaggedTemplateExpression;
	bool isDecorator = node->kind == Kind::Decorator;
	bool isJsxOpeningOrSelfClosingElement = isJsxOpeningLikeElement(node);
	bool isInstanceof = node->kind == Kind::BinaryExpression;
	bool reportErrors = !isInferencePartiallyBlocked && candidatesOutArray == nullptr;
	CallState s{};
	s.node = node;
	if (!isDecorator && !isInstanceof && !isSuperCall(node) &&
		!isJsxOpeningFragment(node)) {
		s.typeArguments = node->typeArguments();
		// We already perform checking on the type arguments on the class declaration itself.
		if (isTaggedTemplate || isJsxOpeningOrSelfClosingElement ||
			node->expression()->kind != Kind::SuperKeyword) {
			checkSourceElements(s.typeArguments);
		}
	}
	s.candidates = reorderCandidates(signatures, callChainFlags);
	if (candidatesOutArray != nullptr) {
		*candidatesOutArray = s.candidates;
	}
	if (s.candidates.empty()) {
		// In Strada we would error here, but no known repro doesn't have at least
		// one other error in this codepath. Just return instead. See #54442
		return unknownSignature;
	}
	s.args = getEffectiveCallArguments(node);
	// The excludeArgument array contains true for each context sensitive argument (an argument
	// is context sensitive it is susceptible to a one-time permanent contextual typing).
	//
	// The idea is that we will perform type argument inference & assignability checking once
	// without using the susceptible parameters that are functions, and once more for those
	// parameters, contextually typing each as we go along.
	//
	// For a tagged template, then the first argument be 'undefined' if necessary because it
	// represents a TemplateStringsArray.
	//
	// For a decorator, no arguments are susceptible to contextual typing due to the fact
	// decorators are applied to a declaration by the emitter, and not to an expression.
	s.isSingleNonGenericCandidate =
		s.candidates.size() == 1 && s.candidates[0]->typeParameters.empty();
	if (!isDecorator && !s.isSingleNonGenericCandidate &&
		someOf(s.args, [this](Node* arg) { return isContextSensitive(arg); })) {
		s.argCheckMode = CheckModeSkipContextSensitive;
	} else {
		s.argCheckMode = CheckModeNormal;
	}
	// The following variables are captured and modified by calls to chooseOverload.
	// If overload resolution or type argument inference fails, we want to report the
	// best error possible. The best error is one which says that an argument was not
	// assignable to a parameter. This implies that everything else about the overload
	// was fine. So if there is any overload that is only incorrect because of an
	// argument, we will report an error on that one.
	//
	//     function foo(s: string): void;
	//     function foo(n: number): void; // Report argument error on this overload
	//     function foo(): void;
	//     foo(true);
	//
	// If none of the overloads even made it that far, there are two possibilities.
	// There was a problem with type arguments for some overload, in which case
	// report an error on that. Or none of the overloads even had correct arity,
	// in which case give an arity error.
	//
	//     function foo<T extends string>(x: T): void; // Report type argument error
	//     function foo(): void;
	//     foo<number>(0);
	//
	// If we are in signature help, a trailing comma indicates that we intend to provide another argument,
	// so we will only accept overloads with arity at least 1 higher than the current number of provided arguments.
	s.signatureHelpTrailingComma =
		(checkMode & CheckModeIsForSignatureHelp) != 0 && isCallExpression(node) &&
		node->argumentList()->hasTrailingComma();
	// Section 4.12.1:
	// if the candidate list contains one or more signatures for which the type of each argument
	// expression is a subtype of each corresponding parameter type, the return type of the first
	// of those signatures becomes the return type of the function call.
	// Otherwise, the return type of the first signature in the candidate list becomes the return
	// type of the function call.
	//
	// Whether the call is an error is determined by assignability of the arguments. The subtype pass
	// is just important for choosing the best signature. So in the case where there is only one
	// signature, the subtype pass is useless. So skipping it is an optimization.
	Signature* result = nullptr;
	s.recursiveResolution = containsVec(callResolutionStack, s.node);
	callResolutionStack.push_back(s.node);
	if (s.candidates.size() > 1) {
		result = chooseOverload(&s, subtypeRelation);
	}
	if (result == nullptr) {
		result = chooseOverload(&s, assignableRelation);
	}
	callResolutionStack.pop_back();
	if (result != nullptr) {
		return result;
	}
	result = getCandidateForOverloadFailure(s.node, s.candidates, s.args,
											candidatesOutArray != nullptr,
											checkMode);
	// Preemptively cache the result; getResolvedSignature will do this after we return, but
	// we need to ensure that the result is present for the error checks below so that if
	// this signature is encountered again, we handle the circularity (rather than producing a
	// different result which may produce no errors and assert). Callers of getResolvedSignature
	// don't hit this issue because they only observe this result after it's had a chance to
	// be cached, but the error reporting code below executes before getResolvedSignature sets
	// resolvedSignature.
	signatureLinks.Get(node)->resolvedSignature = result;
	// No signatures were applicable. Now report errors based on the last applicable signature with
	// no arguments excluded from assignability checks.
	// If candidate is undefined, it means that no candidates had a suitable arity. In that case,
	// skip the checkApplicableSignature check.
	if (reportErrors) {
		// If the call expression is a synthetic call to a `[Symbol.hasInstance]` method then we will produce a head
		// message when reporting diagnostics that explains how we got to `right[Symbol.hasInstance](left)` from
		// `left instanceof right`, as it pertains to "Argument" related messages reported for the call.
		if (headMessage == nullptr && isInstanceof) {
			headMessage =
				The_left_hand_side_of_an_instanceof_expression_must_be_assignable_to_the_first_argument_of_the_right_hand_side_s_Symbol_hasInstance_method;
		}
		reportCallResolutionErrors(node, &s, signatures, headMessage);
	}
	return result;
}

// checker.go:9145 — reorderCandidates
std::vector<Signature*> Checker::reorderCandidates(
	std::vector<Signature*> signatures, SignatureFlags callChainFlags) {
	Node* lastParent = nullptr;
	Symbol* lastSymbol = nullptr;
	int index = 0;
	int cutoffIndex = 0;
	int spliceIndex = 0;
	int specializedIndex = -1;
	std::vector<Signature*> result;
	result.reserve(signatures.size());
	for (Signature* signature : signatures) {
		Symbol* symbol = nullptr;
		Node* parent = nullptr;
		if (signature->declaration != nullptr) {
			symbol = getSymbolOfDeclaration(signature->declaration);
			parent = signature->declaration->parent;
		}
		if (lastSymbol == nullptr || symbol == lastSymbol) {
			if (lastParent != nullptr && parent == lastParent) {
				index = index + 1;
			} else {
				lastParent = parent;
				index = cutoffIndex;
			}
		} else {
			// current declaration belongs to a different symbol
			// set cutoffIndex so re-orderings in the future won't change result set from 0 to cutoffIndex
			index = static_cast<int>(result.size());
			cutoffIndex = static_cast<int>(result.size());
			lastParent = parent;
		}
		lastSymbol = symbol;
		// specialized signatures always need to be placed before non-specialized signatures regardless
		// of the cutoff position; see GH#1133
		if (signatureHasLiteralTypes(signature)) {
			specializedIndex++;
			spliceIndex = specializedIndex;
			// The cutoff index always needs to be greater than or equal to the specialized signature index
			// in order to prevent non-specialized signatures from being added before a specialized
			// signature.
			cutoffIndex++;
		} else {
			spliceIndex = index;
		}
		if (callChainFlags != 0) {
			signature = getOptionalCallSignature(signature, callChainFlags);
		}
		result.insert(result.begin() + spliceIndex, signature);
	}
	return result;
}

// checker.go:9199 — getOptionalCallSignature
Signature* Checker::getOptionalCallSignature(Signature* signature,
											 SignatureFlags callChainFlags) {
	if ((signature->flags & SignatureFlagsCallChainFlags) == callChainFlags) {
		return signature;
	}
	CachedSignatureKey key{
		signature,
		ifElse(callChainFlags == SignatureFlagsIsInnerCallChain, SignatureKeyInner,
			   SignatureKeyOuter)};
	auto it = cachedSignatures.find(key);
	if (it != cachedSignatures.end() && it->second != nullptr) {
		return it->second;
	}
	Signature* result = cloneSignature(signature);
	result->flags |= callChainFlags;
	cachedSignatures[key] = result;
	return result;
}

// checker.go:9213 — chooseOverload
Signature* Checker::chooseOverload(CallState* s, Relation* relation) {
	s->candidatesForArgumentError.clear();
	s->candidateForArgumentArityError = nullptr;
	s->candidateForTypeArgumentError = nullptr;
	if (s->isSingleNonGenericCandidate) {
		Signature* candidate = s->candidates[0];
		if (!s->typeArguments.empty() ||
			!hasCorrectArity(s->node, s->args, candidate,
							 s->signatureHelpTrailingComma)) {
			return nullptr;
		}
		if (!isSignatureApplicable(s->node, s->args, candidate, relation,
								   CheckModeNormal, false /*reportErrors*/,
								   nullptr /*diagnosticOutput*/)) {
			s->candidatesForArgumentError = {candidate};
			return nullptr;
		}
		return candidate;
	}
	for (size_t candidateIndex = 0; candidateIndex < s->candidates.size();
		 candidateIndex++) {
		Signature* candidate = s->candidates[candidateIndex];
		if (!hasCorrectTypeArgumentArity(candidate, s->typeArguments) ||
			!hasCorrectArity(s->node, s->args, candidate,
							 s->signatureHelpTrailingComma)) {
			continue;
		}
		Signature* checkCandidate;
		InferenceContext* inferenceContext = nullptr;
		std::vector<Type*> typeArgumentTypes;
		if (!candidate->typeParameters.empty()) {
			if (!s->typeArguments.empty()) {
				typeArgumentTypes =
					checkTypeArguments(candidate, s->typeArguments,
									   false /*reportErrors*/, nullptr);
				if (typeArgumentTypes.empty()) {
					s->candidateForTypeArgumentError = candidate;
					continue;
				}
			} else {
				// When we are recursively resolving a call with a single candidate, we skip constraints checks during
				// type inference to avoid circularity errors. For example, see #64192.
				InferenceFlags inferenceFlags =
					ifElse(s->recursiveResolution && s->candidates.size() == 1,
						   InferenceFlagsNoConstraintChecks, InferenceFlagsNone) |
					ifElse(isInJSFile(s->node), InferenceFlagsAnyDefault,
						   InferenceFlagsNone);
				inferenceContext = newInferenceContext(candidate->typeParameters,
													   candidate, inferenceFlags,
													   nullptr);
				typeArgumentTypes =
					inferTypeArguments(s->node, candidate, s->args,
									   s->argCheckMode |
										   CheckModeSkipGenericFunctions,
									   inferenceContext);
				if ((inferenceContext->flags &
					 InferenceFlagsSkippedGenericFunction) != 0) {
					s->argCheckMode |= CheckModeSkipGenericFunctions;
				}
			}
			std::vector<Type*> inferredTypeParameters;
			if (inferenceContext != nullptr) {
				inferredTypeParameters = inferenceContext->inferredTypeParameters;
			}
			checkCandidate = getSignatureInstantiation(
				candidate, typeArgumentTypes, isInJSFile(candidate->declaration),
				inferredTypeParameters);
			// If the original signature has a generic rest type, instantiation may produce a
			// signature with different arity and we need to perform another arity check.
			if (getNonArrayRestType(candidate) != nullptr &&
				!hasCorrectArity(s->node, s->args, checkCandidate,
								 s->signatureHelpTrailingComma)) {
				s->candidateForArgumentArityError = checkCandidate;
				continue;
			}
		} else {
			checkCandidate = candidate;
		}
		if (!isSignatureApplicable(s->node, s->args, checkCandidate, relation,
								   s->argCheckMode, false /*reportErrors*/,
								   nullptr /*diagnosticOutput*/)) {
			// Give preference to error candidates that have no rest parameters (as they are more specific)
			s->candidatesForArgumentError.push_back(checkCandidate);
			continue;
		}
		if (s->argCheckMode != 0) {
			// If one or more context sensitive arguments were excluded, we start including
			// them now (and keeping do so for any subsequent candidates) and perform a second
			// round of type inference and applicability checking for this particular candidate.
			s->argCheckMode = CheckModeNormal;
			if (inferenceContext != nullptr) {
				typeArgumentTypes = inferTypeArguments(s->node, candidate, s->args,
													   s->argCheckMode,
													   inferenceContext);
				checkCandidate = getSignatureInstantiation(
					candidate, typeArgumentTypes, isInJSFile(candidate->declaration),
					inferenceContext->inferredTypeParameters);
				// If the original signature has a generic rest type, instantiation may produce a
				// signature with different arity and we need to perform another arity check.
				if (getNonArrayRestType(candidate) != nullptr &&
					!hasCorrectArity(s->node, s->args, checkCandidate,
									 s->signatureHelpTrailingComma)) {
					s->candidateForArgumentArityError = checkCandidate;
					continue;
				}
			}
			if (!isSignatureApplicable(s->node, s->args, checkCandidate, relation,
									   s->argCheckMode, false /*reportErrors*/,
									   nullptr /*diagnosticOutput*/)) {
				// Give preference to error candidates that have no rest parameters (as they are more specific)
				s->candidatesForArgumentError.push_back(checkCandidate);
				continue;
			}
		}
		s->candidates[candidateIndex] = checkCandidate;
		return checkCandidate;
	}
	return nullptr;
}

// checker.go:9299 — hasCorrectArity
bool Checker::hasCorrectArity(Node* node, std::vector<Node*> args,
							  Signature* signature,
							  bool signatureHelpTrailingComma) {
	if (isJsxOpeningFragment(node)) {
		return true;
	}
	int argCount;
	bool callIsIncomplete = false;
	// In incomplete call we want to be lenient when we have too few arguments
	int effectiveParameterCount = getParameterCount(signature);
	int effectiveMinimumArguments = getMinArgumentCount(signature);
	if (isTaggedTemplateExpression(node)) {
		argCount = static_cast<int>(args.size());
		Node* template_ = node->as<TaggedTemplateExpression>()->Template;
		if (isTemplateExpression(template_)) {
			// If a tagged template expression lacks a tail literal, the call is incomplete.
			// Specifically, a template only can end in a TemplateTail or a Missing literal.
			Node* lastSpan = lastOrNil(
				template_->as<TemplateExpression>()->TemplateSpans->nodes);
			// we should always have at least one span.
			callIsIncomplete =
				nodeIsMissing(lastSpan->as<TemplateSpan>()->Literal) ||
				isUnterminatedLiteral(lastSpan->as<TemplateSpan>()->Literal);
		} else {
			// If the template didn't end in a backtick, or its beginning occurred right prior to EOF,
			// then this might actually turn out to be a TemplateHead in the future;
			// so we consider the call to be incomplete.
			callIsIncomplete = isUnterminatedLiteral(template_);
		}
	} else if (isDecorator(node)) {
		argCount = getDecoratorArgumentCount(node, signature);
	} else if (isBinaryExpression(node)) {
		argCount = 1;
	} else if (isJsxOpeningLikeElement(node)) {
		callIsIncomplete = node->attributes()->end() == node->end();
		if (callIsIncomplete) {
			return true;
		}
		argCount =
			ifElse(effectiveMinimumArguments == 0, static_cast<int>(args.size()),
				   1);
		effectiveParameterCount =
			ifElse(args.empty(), effectiveParameterCount,
				   1); // class may have argumentless ctor functions - still resolve ctor and compare vs props member type
		effectiveMinimumArguments =
			std::min(effectiveMinimumArguments,
					 1); // sfc may specify context argument - handled by framework and not typechecked
	} else if (isNewExpression(node) && node->argumentList() == nullptr) {
		// This only happens when we have something of the form: 'new C'
		return getMinArgumentCount(signature) == 0;
	} else {
		if (signatureHelpTrailingComma) {
			argCount = static_cast<int>(args.size()) + 1;
		} else {
			argCount = static_cast<int>(args.size());
		}
		// If we are missing the close parenthesis, the call is incomplete.
		callIsIncomplete = node->argumentList()->end() == node->end();
		// If a spread argument is present, check that it corresponds to a rest parameter or at least that it's in the valid range.
		int spreadArgIndex = getSpreadArgumentIndex(args);
		if (spreadArgIndex >= 0) {
			return spreadArgIndex >= getMinArgumentCount(signature) &&
				(hasEffectiveRestParameter(signature) ||
				 spreadArgIndex < getParameterCount(signature));
		}
	}
	// Too many arguments implies incorrect arity.
	if (!hasEffectiveRestParameter(signature) &&
		argCount > effectiveParameterCount) {
		return false;
	}
	// If the call is incomplete, we should skip the lower bound check.
	// JSX signatures can have extra parameters provided by the library which we don't check
	if (callIsIncomplete || argCount >= effectiveMinimumArguments) {
		return true;
	}
	for (int i = argCount; i < effectiveMinimumArguments; i++) {
		Type* t = getTypeAtPosition(signature, i);
		if ((filterType(t, acceptsVoid)->flags & TypeFlagsNever) != 0) {
			return false;
		}
	}
	return true;
}

// checker.go:9375 — getDecoratorArgumentCount
int Checker::getDecoratorArgumentCount(Node* node, Signature* signature) {
	if (compilerOptions->ExperimentalDecorators == Tristate::True) {
		return getLegacyDecoratorArgumentCount(node, signature);
	}
	return std::min(std::max(getParameterCount(signature), 1), 2);
}

// checker.go:9385 — getLegacyDecoratorArgumentCount
int Checker::getLegacyDecoratorArgumentCount(Node* node, Signature* signature) {
	switch (node->parent->kind) {
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
		return 1;
	case Kind::PropertyDeclaration:
		if (hasAccessorModifier(node->parent)) {
			return 3;
		}
		return 2;
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		// For decorators with only two parameters we supply only two arguments
		if (getParameterCount(signature) <= 2) {
			return 2;
		}
		return 3;
	case Kind::Parameter:
		return 3;
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in getLegacyDecoratorArgumentCount");
}

// checker.go:9406 — hasCorrectTypeArgumentArity
bool Checker::hasCorrectTypeArgumentArity(
	Signature* signature, const std::vector<Node*>& typeArguments) {
	// If the user supplied type arguments, but the number of type arguments does not match
	// the declared number of type parameters, the call has an incorrect arity.
	int numTypeParameters = static_cast<int>(signature->typeParameters.size());
	int minTypeArgumentCount = getMinTypeArgumentCount(signature->typeParameters);
	return typeArguments.empty() ||
		(static_cast<int>(typeArguments.size()) >= minTypeArgumentCount &&
		 static_cast<int>(typeArguments.size()) <= numTypeParameters);
}

// checker.go:9414 — checkTypeArguments
std::vector<Type*> Checker::checkTypeArguments(
	Signature* signature, const std::vector<Node*>& typeArgumentNodes, bool reportErrors,
	const DiagnosticMessage* headMessage) {
	bool isJavaScript = isInJSFile(signature->declaration);
	const std::vector<Type*>& typeParameters = signature->typeParameters;
	std::vector<Type*> typeArgumentTypes = fillMissingTypeArguments(
		mapVec(typeArgumentNodes,
			   [this](Node* n) { return getTypeFromTypeNode(n); }),
		typeParameters, getMinTypeArgumentCount(typeParameters), isJavaScript);
	TypeMapper* mapper = nullptr;
	for (size_t i = 0; i < typeArgumentNodes.size(); i++) {
		TSC_ASSERT(typeParameters[i] != nullptr,
				   "Should not call checkTypeArguments with too many type arguments");
		Type* constraint = getConstraintOfTypeParameter(typeParameters[i]);
		if (constraint != nullptr) {
			const DiagnosticMessage* typeArgumentHeadMessage =
				headMessage != nullptr
					? headMessage
					: Type_0_does_not_satisfy_the_constraint_1;
			if (mapper == nullptr) {
				mapper = newTypeMapper(typeParameters, typeArgumentTypes);
			}
			Type* typeArgument = typeArgumentTypes[i];
			Node* errorNode = nullptr;
			if (reportErrors) {
				errorNode = typeArgumentNodes[i];
			}
			std::vector<Diagnostic*> diags;
			if (!checkTypeAssignableToEx(
					typeArgument,
					getTypeWithThisArgument(instantiateType(constraint, mapper),
											typeArgument, false),
					errorNode, typeArgumentHeadMessage, &diags)) {
				if (!diags.empty()) {
					Diagnostic* diagnostic = diags[0];
					if (headMessage != nullptr) {
						diagnostic = newDiagnosticChain(
							diagnostic, Type_0_does_not_satisfy_the_constraint_1);
					}
					addDiagnostic(diagnostic);
				}
				return {};
			}
		}
	}
	return typeArgumentTypes;
}

// checker.go:9448 — isSignatureApplicable
bool Checker::isSignatureApplicable(Node* node, const std::vector<Node*>& args,
									Signature* signature, Relation* relation,
									CheckMode checkMode, bool reportErrors,
									std::vector<Diagnostic*>* diagnosticOutput) {
	if (isJsxCallLike(node)) {
		return checkApplicableSignatureForJsxCallLikeElement(
			node, signature, relation, checkMode, reportErrors, diagnosticOutput);
	}
	Type* thisType = getThisTypeOfSignature(signature);
	if (thisType != nullptr && thisType != voidType &&
		!(isNewExpression(node) ||
		  (isCallExpression(node) && isSuperProperty(node->expression())))) {
		// If the called expression is not of the form `x.f` or `x["f"]`, then sourceType = voidType
		// If the signature's 'this' type is voidType, then the check is skipped -- anything is compatible.
		// If the expression is a new expression or super call expression, then the check is skipped.
		Node* thisArgumentNode = getThisArgumentOfCall(node);
		Type* thisArgumentType = getThisArgumentType(thisArgumentNode);
		Node* errorNode = nullptr;
		if (reportErrors) {
			errorNode = thisArgumentNode;
			if (errorNode == nullptr) {
				errorNode = node;
			}
		}
		const DiagnosticMessage* headMessage =
			The_this_context_of_type_0_is_not_assignable_to_method_s_this_of_type_1;
		if (!checkTypeRelatedToEx(thisArgumentType, thisType, relation, errorNode,
								  headMessage, diagnosticOutput)) {
			return false;
		}
	}
	const DiagnosticMessage* headMessage =
		Argument_of_type_0_is_not_assignable_to_parameter_of_type_1;
	Type* restType = getNonArrayRestType(signature);
	int argCount;
	if (restType != nullptr) {
		argCount = std::min(getParameterCount(signature) - 1,
							static_cast<int>(args.size()));
	} else {
		argCount = static_cast<int>(args.size());
	}
	for (int i = 0; i < argCount; i++) {
		Node* arg = args[i];
		if (!isOmittedExpression(arg)) {
			Type* paramType = getTypeAtPosition(signature, i);
			Type* argType = checkExpressionWithContextualType(
				arg, paramType, nullptr /*inferenceContext*/, checkMode);
			// If one or more arguments are still excluded (as indicated by CheckMode.SkipContextSensitive),
			// we obtain the regular type of any object literal arguments because we may not have inferred complete
			// parameter types yet and therefore excess property checks may yield false positives (see #17041).
			Type* checkArgType;
			if ((checkMode & CheckModeSkipContextSensitive) != 0) {
				checkArgType = getRegularTypeOfObjectLiteral(argType);
			} else {
				checkArgType = argType;
			}
			Node* effectiveCheckArgumentNode = getEffectiveCheckNode(arg);
			if (!checkTypeRelatedToAndOptionallyElaborate(
					checkArgType, paramType, relation,
					ifElse(reportErrors, effectiveCheckArgumentNode,
						   (Node*)nullptr),
					effectiveCheckArgumentNode, headMessage, diagnosticOutput)) {
				maybeAddMissingAwaitInfo(arg, checkArgType, paramType, relation,
										 reportErrors, diagnosticOutput);
				return false;
			}
		}
	}
	if (restType != nullptr) {
		Type* spreadType = getSpreadArgumentType(
			args, argCount, static_cast<int>(args.size()), restType,
			nullptr /*context*/, checkMode);
		int restArgCount = static_cast<int>(args.size()) - argCount;
		Node* errorNode = nullptr;
		if (reportErrors) {
			switch (restArgCount) {
			case 0:
				errorNode = node;
				break;
			case 1:
				errorNode = getEffectiveCheckNode(args[argCount]);
				break;
			default:
				errorNode = createSyntheticExpression(node, spreadType, false,
													  nullptr);
				errorNode->loc =
					TextRange{args[argCount]->pos(), args.back()->end()};
				break;
			}
		}
		if (!checkTypeRelatedToEx(spreadType, restType, relation, errorNode,
								  headMessage, diagnosticOutput)) {
			maybeAddMissingAwaitInfo(errorNode, spreadType, restType, relation,
									 reportErrors, diagnosticOutput);
			return false;
		}
	}
	return true;
}

// checker.go:9523 — maybeAddMissingAwaitInfo
void Checker::maybeAddMissingAwaitInfo(
	Node* errorNode, Type* source, Type* target, Relation* relation,
	bool reportErrors, std::vector<Diagnostic*>* diagnosticOutput) {
	if (errorNode != nullptr && reportErrors && diagnosticOutput != nullptr &&
		!diagnosticOutput->empty()) {
		// Bail if target is Promise-like---something else is wrong
		if (getAwaitedTypeOfPromise(target) != nullptr) {
			return;
		}
		Type* awaitedTypeOfSource = getAwaitedTypeOfPromise(source);
		if (awaitedTypeOfSource != nullptr &&
			isTypeRelatedTo(awaitedTypeOfSource, target, relation)) {
			(*diagnosticOutput)[0]->AddRelatedInfo(NewDiagnosticForNode(
				errorNode, Did_you_forget_to_use_await, {}));
		}
	}
}

// checker.go:9537 — getThisArgumentOfCall
Node* Checker::getThisArgumentOfCall(Node* node) {
	if (isBinaryExpression(node)) {
		return node->as<BinaryExpression>()->Right;
	}
	Node* expression = nullptr;
	if (isCallExpression(node)) {
		expression = node->expression();
	} else if (isTaggedTemplateExpression(node)) {
		expression = node->as<TaggedTemplateExpression>()->Tag;
	} else if (isDecorator(node) && !legacyDecorators) {
		expression = node->expression();
	}
	if (expression != nullptr) {
		Node* callee = skipOuterExpressions(expression, OEKAll);
		if (isAccessExpression(callee)) {
			return callee->expression();
		}
	}
	return nullptr;
}

// checker.go:9559 — getThisArgumentType
Type* Checker::getThisArgumentType(Node* node) {
	if (node == nullptr) {
		return voidType;
	}
	Type* thisArgumentType = checkExpression(node);
	if (isOptionalChainRoot(node->parent)) {
		return GetNonNullableType(thisArgumentType);
	}
	if (isOptionalChain(node->parent)) {
		return removeOptionalTypeMarker(thisArgumentType);
	}
	return thisArgumentType;
}

// checker.go:9573 — getEffectiveCheckNode
Node* Checker::getEffectiveCheckNode(Node* node) {
	OuterExpressionKinds flags = ifElse(
		isInJSFile(node),
		OEKParentheses | OEKSatisfies | OEKExcludeJSDocTypeAssertion,
		OEKParentheses | OEKSatisfies);
	return skipOuterExpressions(node, flags);
}

// checker.go:9582 — inferTypeArguments
std::vector<Type*> Checker::inferTypeArguments(
	Node* node, Signature* signature, const std::vector<Node*>& args,
	CheckMode checkMode, InferenceContext* context) {
	if (isJsxOpeningLikeElement(node)) {
		return inferJsxTypeArguments(node, signature, checkMode, context);
	}
	// If a contextual type is available, infer from that type to the return type of the call expression. For
	// example, given a 'function wrap<T, U>(cb: (x: T) => U): (x: T) => U' and a call expression
	// 'let f: (x: string) => number = wrap(s => s.length)', we infer from the declared type of 'f' to the
	// return type of 'wrap'.
	if (!isDecorator(node) && !isBinaryExpression(node)) {
		bool skipBindingPatterns =
			everyOf(signature->typeParameters, [this](Type* p) {
				return getDefaultFromTypeParameter(p) != nullptr;
			});
		Type* contextualType =
			getContextualType(node, ifElse(skipBindingPatterns,
										 ContextFlagsSkipBindingPatterns,
										 ContextFlagsNone));
		if (contextualType != nullptr) {
			Type* inferenceTargetType = getReturnTypeOfSignature(signature);
			if (couldContainTypeVariables(inferenceTargetType)) {
				InferenceContext* outerContext = getInferenceContext(node);
				bool isFromBindingPattern =
					!skipBindingPatterns &&
					getContextualType(node, ContextFlagsSkipBindingPatterns) !=
						contextualType;
				// A return type inference from a binding pattern can be used in instantiating the contextual
				// type of an argument later in inference, but cannot stand on its own as the final return type.
				// It is incorporated into `context.returnMapper` which is used in `instantiateContextualType`,
				// but doesn't need to go into `context.inferences`. This allows a an array binding pattern to
				// produce a tuple for `T` in
				//   declare function f<T>(cb: () => T): T;
				//   const [e1, e2, e3] = f(() => [1, "hi", true]);
				// but does not produce any inference for `T` in
				//   declare function f<T>(): T;
				//   const [e1, e2, e3] = f();
				if (!isFromBindingPattern) {
					// We clone the inference context to avoid disturbing a resolution in progress for an
					// outer call expression. Effectively we just want a snapshot of whatever has been
					// inferred for any outer call expression so far.
					TypeMapper* outerMapper = getMapperFromContext(
						cloneInferenceContext(outerContext,
											  InferenceFlagsNoDefault));
					Type* instantiatedType =
						instantiateType(contextualType, outerMapper);
					// If the contextual type is a generic function type with a single call signature, we
					// instantiate the type with its own type parameters and type arguments. This ensures that
					// the type parameters are not erased to type any during type inference such that they can
					// be inferred as actual types from the contextual type. For example:
					//   declare function arrayMap<T, U>(f: (x: T) => U): (a: T[]) => U[];
					//   const boxElements: <A>(a: A[]) => { value: A }[] = arrayMap(value => ({ value }));
					// Above, the type of the 'value' parameter is inferred to be 'A'.
					Signature* contextualSignature =
						getSingleCallSignature(instantiatedType);
					Type* inferenceSourceType;
					if (contextualSignature != nullptr &&
						!contextualSignature->typeParameters.empty()) {
						inferenceSourceType = getOrCreateTypeFromSignature(
							getSignatureInstantiationWithoutFillingInTypeArguments(
								contextualSignature,
								contextualSignature->typeParameters));
					} else {
						inferenceSourceType = instantiatedType;
					}
					// Inferences made from return types have lower priority than all other inferences.
					inferTypes(context->inferences, inferenceSourceType,
							   inferenceTargetType, InferencePriorityReturnType,
							   false);
				}
				// Create a type mapper for instantiating generic contextual types using the inferences made
				// from the return type. We need a separate inference pass here because (a) instantiation of
				// the source type uses the outer context's return mapper (which excludes inferences made from
				// outer arguments), and (b) we don't want any further inferences going into this context.
				// We use `createOuterReturnMapper` to ensure that all occurrences of outer type parameters are
				// replaced with inferences produced from the outer return type or preceding outer arguments.
				// This protects against circular inferences, i.e. avoiding situations where inferences reference
				// type parameters for which the inferences are being made.
				InferenceContext* returnContext =
					newInferenceContext(signature->typeParameters, signature,
										context->flags, nullptr);
				TypeMapper* outerReturnMapper = nullptr;
				if (outerContext != nullptr) {
					outerReturnMapper = createOuterReturnMapper(outerContext);
				}
				Type* returnSourceType =
					instantiateType(contextualType, outerReturnMapper);
				inferTypes(returnContext->inferences, returnSourceType,
						   inferenceTargetType, InferencePriorityNone, false);
				if (someOf(returnContext->inferences, hasInferenceCandidates)) {
					context->returnMapper = getMapperFromContext(
						cloneInferredPartOfContext(returnContext));
				} else {
					context->returnMapper = nullptr;
				}
			}
		}
	}
	Type* restType = getNonArrayRestType(signature);
	int argCount = static_cast<int>(args.size());
	if (restType != nullptr) {
		argCount = std::min(getParameterCount(signature) - 1, argCount);
	}
	if (restType != nullptr && (restType->flags & TypeFlagsTypeParameter) != 0) {
		InferenceInfo* info = findIf(context->inferences,
									 [restType](InferenceInfo* info) {
										 return info->typeParameter == restType;
									 });
		if (info != nullptr) {
			bool hasSpread = false;
			for (size_t i = argCount; i < args.size(); i++) {
				if (isSpreadArgument(args[i])) {
					hasSpread = true;
					break;
				}
			}
			if (!hasSpread) {
				info->impliedArity = static_cast<int>(args.size()) - argCount;
			}
		}
	}
	Type* thisType = getThisTypeOfSignature(signature);
	if (thisType != nullptr && couldContainTypeVariables(thisType)) {
		Node* thisArgumentNode = getThisArgumentOfCall(node);
		inferTypes(context->inferences, getThisArgumentType(thisArgumentNode),
				   thisType, InferencePriorityNone, false);
	}
	for (int i = 0; i < argCount; i++) {
		Node* arg = args[i];
		if (arg->kind != Kind::OmittedExpression) {
			Type* paramType = getTypeAtPosition(signature, i);
			if (couldContainTypeVariables(paramType)) {
				Type* argType = checkExpressionWithContextualType(
					arg, paramType, context, checkMode);
				inferTypes(context->inferences, argType, paramType,
						   InferencePriorityNone, false);
			}
		}
	}
	if (restType != nullptr && couldContainTypeVariables(restType)) {
		Type* spreadType = getSpreadArgumentType(
			args, argCount, static_cast<int>(args.size()), restType, context,
			checkMode);
		inferTypes(context->inferences, spreadType, restType,
				   InferencePriorityNone, false);
	}
	return getInferredTypes(context);
}

// checker.go:9690 — getCandidateForOverloadFailure
Signature* Checker::getCandidateForOverloadFailure(
	Node* node, std::vector<Signature*> candidates, std::vector<Node*> args,
	bool hasCandidatesOutArray, CheckMode checkMode) {
	// Else should not have called this.
	checkNodeDeferred(node);
	// Normally we will combine overloads. Skip this if they have type parameters since that's hard to combine.
	// Don't do this if there is a `candidatesOutArray`,
	// because then we want the chosen best candidate to be one of the overloads, not a combination.
	if (hasCandidatesOutArray || candidates.size() == 1 ||
		someOf(candidates, [](Signature* s) {
			return !s->typeParameters.empty();
		})) {
		return pickLongestCandidateSignature(node, candidates, args, checkMode);
	}
	return createUnionOfSignaturesForOverloadFailure(candidates);
}

// checker.go:9702 — pickLongestCandidateSignature
Signature* Checker::pickLongestCandidateSignature(
	Node* node, std::vector<Signature*>& candidates, std::vector<Node*> args,
	CheckMode checkMode) {
	// Pick the longest signature. This way we can get a contextual type for cases like:
	//     declare function f(a: { xa: number; xb: number; }, b: number);
	//     f({ |
	// Also, use explicitly-supplied type arguments if they are provided, so we can get a contextual signature in cases like:
	//     declare function f<T>(k: keyof T);
	//     f<Foo>("
	int argCount = static_cast<int>(args.size());
	if (apparentArgumentCount != nullptr) {
		argCount = *apparentArgumentCount;
	}
	int bestIndex = getLongestCandidateIndex(candidates, argCount);
	Signature* candidate = candidates[bestIndex];
	const std::vector<Type*>& typeParameters = candidate->typeParameters;
	if (typeParameters.empty()) {
		return candidate;
	}
	std::vector<Node*> typeArgumentNodes;
	if (callLikeExpressionMayHaveTypeArguments(node)) {
		typeArgumentNodes = node->typeArguments();
	}
	Signature* instantiated;
	if (!typeArgumentNodes.empty()) {
		instantiated = createSignatureInstantiation(
			candidate, getTypeArgumentsFromNodes(typeArgumentNodes, typeParameters));
	} else {
		instantiated = inferSignatureInstantiationForOverloadFailure(
			node, typeParameters, candidate, args, checkMode);
	}
	candidates[bestIndex] = instantiated;
	return instantiated;
}

// checker.go:9733 — getLongestCandidateIndex
int Checker::getLongestCandidateIndex(
	const std::vector<Signature*>& candidates, int argsCount) {
	int maxParamsIndex = -1;
	int maxParams = -1;
	for (size_t i = 0; i < candidates.size(); i++) {
		Signature* candidate = candidates[i];
		int paramCount = getParameterCount(candidate);
		if (hasEffectiveRestParameter(candidate) || paramCount >= argsCount) {
			return static_cast<int>(i);
		}
		if (paramCount > maxParams) {
			maxParams = paramCount;
			maxParamsIndex = static_cast<int>(i);
		}
	}
	return maxParamsIndex;
}

// checker.go:9749 — getTypeArgumentsFromNodes
std::vector<Type*> Checker::getTypeArgumentsFromNodes(
	std::vector<Node*> typeArgumentNodes,
	const std::vector<Type*>& typeParameters) {
	if (typeArgumentNodes.size() > typeParameters.size()) {
		typeArgumentNodes.resize(typeParameters.size());
	}
	std::vector<Type*> typeArguments =
		mapVec(typeArgumentNodes, [this](Node* n) { return getTypeFromTypeNode(n); });
	while (typeArguments.size() < typeParameters.size()) {
		Type* t =
			getDefaultFromTypeParameter(typeParameters[typeArguments.size()]);
		if (t == nullptr) {
			t = getConstraintOfTypeParameter(
				typeParameters[typeArguments.size()]);
			if (t == nullptr) {
				t = unknownType;
			}
		}
		typeArguments.push_back(t);
	}
	return typeArguments;
}

// checker.go:9767 — inferSignatureInstantiationForOverloadFailure
Signature* Checker::inferSignatureInstantiationForOverloadFailure(
	Node* node, const std::vector<Type*>& typeParameters, Signature* candidate,
	std::vector<Node*> args, CheckMode checkMode) {
	InferenceContext* inferenceContext = newInferenceContext(
		typeParameters, candidate,
		ifElse(isInJSFile(node), InferenceFlagsAnyDefault, InferenceFlagsNone),
		nullptr);
	std::vector<Type*> typeArgumentTypes =
		inferTypeArguments(node, candidate, args,
						   checkMode | CheckModeSkipContextSensitive |
							   CheckModeSkipGenericFunctions,
						   inferenceContext);
	return createSignatureInstantiation(candidate, typeArgumentTypes);
}

// checker.go:9773 — createUnionOfSignaturesForOverloadFailure
Signature* Checker::createUnionOfSignaturesForOverloadFailure(
	const std::vector<Signature*>& candidates) {
	std::vector<Symbol*> thisParameters =
		mapNonNil(candidates, [](Signature* s) { return s->thisParameter; });
	Symbol* thisParameter = nullptr;
	if (!thisParameters.empty()) {
		thisParameter = createCombinedSymbolFromTypes(
			thisParameters,
			mapVec(thisParameters,
				   [this](Symbol* s) { return getTypeOfParameter(s); }));
	}
	auto [minArgumentCount, maxNonRestParam] =
		minAndMax(candidates, getNonRestParameterCount);
	std::vector<Symbol*> parameters;
	parameters.reserve(maxNonRestParam);
	for (int i = 0; i < maxNonRestParam; i++) {
		std::vector<Symbol*> symbols =
			mapNonNil(candidates, [i](Signature* s) -> Symbol* {
				if (signatureHasRestParameter(s)) {
					if (i < static_cast<int>(s->parameters.size()) - 1) {
						return s->parameters[i];
					}
					return lastOrNil(s->parameters);
				}
				if (i < static_cast<int>(s->parameters.size())) {
					return s->parameters[i];
				}
				return nullptr;
			});
		parameters.push_back(createCombinedSymbolFromTypes(
			symbols, mapNonNil(candidates,
							   [this, i](Signature* s) {
								   return tryGetTypeAtPosition(s, i);
							   })));
	}
	std::vector<Symbol*> restParameterSymbols =
		mapNonNil(candidates, [](Signature* s) -> Symbol* {
			if (signatureHasRestParameter(s)) {
				return lastOrNil(s->parameters);
			}
			return nullptr;
		});
	SignatureFlags flags = SignatureFlagsIsSignatureCandidateForOverloadFailure;
	if (!restParameterSymbols.empty()) {
		Type* t = createArrayType(getUnionTypeEx(
			mapNonNil(candidates,
					  [this](Signature* s) { return tryGetRestTypeOfSignature(s); }),
			UnionReductionSubtype, nullptr, nullptr));
		parameters.push_back(
			createCombinedSymbolForOverloadFailure(restParameterSymbols, t));
		flags |= SignatureFlagsHasRestParameter;
	}
	if (someOf(candidates, signatureHasLiteralTypes)) {
		flags |= SignatureFlagsHasLiteralTypes;
	}
	return newSignature(
		flags, candidates[0]->declaration, {}, thisParameter, parameters,
		getIntersectionType(mapVec(candidates, [this](Signature* s) {
			return getReturnTypeOfSignature(s);
		})),
		nullptr, minArgumentCount);
}

// checker.go:9814 — createCombinedSymbolFromTypes
Symbol* Checker::createCombinedSymbolFromTypes(
	const std::vector<Symbol*>& sources, const std::vector<Type*>& types) {
	return createCombinedSymbolForOverloadFailure(
		sources, getUnionTypeEx(types, UnionReductionSubtype, nullptr, nullptr));
}

// checker.go:9818 — createCombinedSymbolForOverloadFailure
Symbol* Checker::createCombinedSymbolForOverloadFailure(
	const std::vector<Symbol*>& sources, Type* t) {
	// This function is currently only used for erroneous overloads, so it's good enough to just use the first source.
	return createSymbolWithType(firstOrNil(sources), t);
}

// checker.go:9823 — getRestTypeOfSignature
Type* Checker::getRestTypeOfSignature(Signature* signature) {
	return orElse(tryGetRestTypeOfSignature(signature), anyType);
}

// checker.go:9827 — tryGetRestTypeOfSignature
Type* Checker::tryGetRestTypeOfSignature(Signature* signature) {
	if (!signatureHasRestParameter(signature)) {
		return nullptr;
	}
	Type* restType = getTypeOfSymbol(signature->parameters.back());
	if (isTupleType(restType)) {
		restType = getRestTypeOfTupleType(restType);
		if (restType == nullptr) {
			return nullptr;
		}
	}
	return getIndexTypeOfType(restType, numberType);
}

// checker.go:9841 — reportCallResolutionErrors
void Checker::reportCallResolutionErrors(
	Node* node, CallState* s, const std::vector<Signature*>& signatures,
	const DiagnosticMessage* headMessage) {
	if (!s->candidatesForArgumentError.empty()) {
		Signature* last = s->candidatesForArgumentError.back();
		std::vector<Diagnostic*> diags;
		isSignatureApplicable(s->node, s->args, last, assignableRelation,
							  CheckModeNormal, true /*reportErrors*/, &diags);
		for (Diagnostic* diagnostic : diags) {
			if (s->candidatesForArgumentError.size() > 1) {
				diagnostic = newDiagnosticChain(
					diagnostic, The_last_overload_gave_the_following_error);
				diagnostic =
					newDiagnosticChain(diagnostic, No_overload_matches_this_call);
			}
			if (headMessage != nullptr) {
				diagnostic = newDiagnosticChain(diagnostic, headMessage);
			}
			if (last->declaration != nullptr &&
				s->candidatesForArgumentError.size() > 1) {
				diagnostic->AddRelatedInfo(NewDiagnosticForNode(
					last->declaration, The_last_overload_is_declared_here, {}));
			}
			addImplementationSuccessElaboration(s, last, diagnostic);
			addDiagnostic(diagnostic);
		}
	} else if (s->candidateForArgumentArityError != nullptr) {
		addDiagnostic(getArgumentArityError(s->node,
											{s->candidateForArgumentArityError},
											s->args, headMessage));
	} else if (s->candidateForTypeArgumentError != nullptr) {
		checkTypeArguments(s->candidateForTypeArgumentError,
						   s->node->typeArguments(), true /*reportErrors*/,
						   headMessage);
	} else if (!isJsxOpeningFragment(node)) {
		std::vector<Signature*> signaturesWithCorrectTypeArgumentArity =
			filterVec(signatures, [this, s](Signature* sig) {
				return hasCorrectTypeArgumentArity(sig, s->typeArguments);
			});
		if (signaturesWithCorrectTypeArgumentArity.empty()) {
			addDiagnostic(getTypeArgumentArityError(s->node, signatures,
													s->typeArguments,
													headMessage));
		} else {
			addDiagnostic(getArgumentArityError(
				s->node, signaturesWithCorrectTypeArgumentArity, s->args,
				headMessage));
		}
	}
}

// checker.go:9877 — addImplementationSuccessElaboration
void Checker::addImplementationSuccessElaboration(CallState* s,
												  Signature* failed,
												  Diagnostic* diagnostic) {
	if (failed->declaration != nullptr &&
		failed->declaration->symbol() != nullptr) {
		const std::vector<Node*>& declarations =
			failed->declaration->symbol()->declarations;
		if (declarations.size() > 1) {
			Node* implementation = findIf(declarations, [](Node* d) {
				return isFunctionLikeDeclaration(d) && d->body() != nullptr;
			});
			if (implementation != nullptr) {
				Signature* candidate =
					getSignatureFromDeclaration(implementation);
				CallState localState = *s;
				localState.candidates = {candidate};
				localState.isSingleNonGenericCandidate =
					candidate->typeParameters.empty();
				if (chooseOverload(&localState, assignableRelation) != nullptr) {
					diagnostic->AddRelatedInfo(NewDiagnosticForNode(
						implementation,
						The_call_would_have_succeeded_against_this_implementation_but_implementation_signatures_of_overloads_are_not_externally_visible,
						{}));
				}
			}
		}
	}
}

// checker.go:9897 — getArgumentArityError
Diagnostic* Checker::getArgumentArityError(
	Node* node, const std::vector<Signature*>& signatures,
	const std::vector<Node*>& args, const DiagnosticMessage* headMessage) {
	int spreadIndex = getSpreadArgumentIndex(args);
	if (spreadIndex > -1) {
		return NewDiagnosticForNode(
			args[spreadIndex],
			A_spread_argument_must_either_have_a_tuple_type_or_be_passed_to_a_rest_parameter,
			{});
	}
	int minCount = INT_MAX; // smallest parameter count
	int maxCount = INT_MIN; // largest parameter count
	int maxBelow = INT_MIN; // largest parameter count that is smaller than the number of arguments
	int minAbove = INT_MAX; // smallest parameter count that is larger than the number of arguments
	Signature* closestSignature = nullptr;
	for (Signature* sig : signatures) {
		int minParameter = getMinArgumentCount(sig);
		int maxParameter = getParameterCount(sig);
		// smallest/largest parameter counts
		if (minParameter < minCount) {
			minCount = minParameter;
			closestSignature = sig;
		}
		maxCount = std::max(maxCount, maxParameter);
		// shortest parameter count *longer than the call*/longest parameter count *shorter than the call*
		if (minParameter < static_cast<int>(args.size()) &&
			minParameter > maxBelow) {
			maxBelow = minParameter;
		}
		if (static_cast<int>(args.size()) < maxParameter &&
			maxParameter < minAbove) {
			minAbove = maxParameter;
		}
	}
	bool hasRestParameter =
		someOf(signatures, [this](Signature* s) {
			return hasEffectiveRestParameter(s);
		});
	std::string parameterRange;
	if (hasRestParameter) {
		parameterRange = std::to_string(minCount);
	} else if (minCount < maxCount) {
		parameterRange =
			std::to_string(minCount) + "-" + std::to_string(maxCount);
	} else {
		parameterRange = std::to_string(minCount);
	}
	bool isVoidPromiseError = !hasRestParameter && parameterRange == "1" &&
		args.empty() && isPromiseResolveArityError(node);
	Node* errorNode = getErrorNodeForCallNode(node);
	if (isVoidPromiseError && isInJSFile(node)) {
		return NewDiagnosticForNode(
			errorNode,
			Expected_1_argument_but_got_0_new_Promise_needs_a_JSDoc_hint_to_produce_a_resolve_that_can_be_called_without_arguments,
			{});
	}
	const DiagnosticMessage* message;
	if (isDecorator(node)) {
		if (hasRestParameter) {
			message =
				The_runtime_will_invoke_the_decorator_with_1_arguments_but_the_decorator_expects_at_least_0;
		} else {
			message =
				The_runtime_will_invoke_the_decorator_with_1_arguments_but_the_decorator_expects_0;
		}
	} else if (hasRestParameter) {
		message = Expected_at_least_0_arguments_but_got_1;
	} else if (isVoidPromiseError) {
		message =
			Expected_0_arguments_but_got_1_Did_you_forget_to_include_void_in_your_type_argument_to_Promise;
	} else {
		message = Expected_0_arguments_but_got_1;
	}
	if (minCount < static_cast<int>(args.size()) &&
		static_cast<int>(args.size()) < maxCount) {
		// between min and max, but with no matching overload
		Diagnostic* diagnostic = NewDiagnosticForNode(
			errorNode,
			No_overload_expects_0_arguments_but_overloads_do_exist_that_expect_either_1_or_2_arguments,
			{std::to_string(args.size()), std::to_string(maxBelow),
			 std::to_string(minAbove)});
		if (headMessage != nullptr) {
			diagnostic = newDiagnosticChain(diagnostic, headMessage);
		}
		return diagnostic;
	} else if (static_cast<int>(args.size()) < minCount) {
		// too short: put the error span on the call expression, not any of the args
		Diagnostic* diagnostic = NewDiagnosticForNode(
			errorNode, message,
			{parameterRange, std::to_string(args.size())});
		if (headMessage != nullptr) {
			diagnostic = newDiagnosticChain(diagnostic, headMessage);
		}
		Node* parameter = nullptr;
		if (closestSignature != nullptr &&
			closestSignature->declaration != nullptr) {
			parameter = elementOrNil(
				closestSignature->declaration->parameters(),
				static_cast<int>(args.size()) +
					ifElse(closestSignature->thisParameter != nullptr, 1, 0));
		}
		if (parameter != nullptr) {
			Diagnostic* related;
			if (isBindingPattern(parameter->name())) {
				related = NewDiagnosticForNode(
					parameter,
					An_argument_matching_this_binding_pattern_was_not_provided, {});
			} else if (isRestParameter(parameter)) {
				related = NewDiagnosticForNode(
					parameter,
					Arguments_for_the_rest_parameter_0_were_not_provided,
					{parameter->name()->text()});
			} else {
				related = NewDiagnosticForNode(
					parameter, An_argument_for_0_was_not_provided,
					{parameter->name()->text()});
			}
			diagnostic->AddRelatedInfo(related);
		}
		return diagnostic;
	} else {
		// Guard against out-of-bounds access when maxCount >= len(args).
		// This can happen when we reach this fallback error path but the argument
		// count actually matches the parameter count (e.g., due to trailing commas
		// causing signature resolution to fail for other reasons).
		if (maxCount >= static_cast<int>(args.size())) {
			Diagnostic* diagnostic = NewDiagnosticForNode(
				errorNode, message,
				{parameterRange, std::to_string(args.size())});
			if (headMessage != nullptr) {
				diagnostic = newDiagnosticChain(diagnostic, headMessage);
			}
			return diagnostic;
		}
		SourceFile* sourceFile = getSourceFileOfNode(node);
		int pos = args[maxCount]->pos();
		int end = args[args.size() - 1]->end();
		if (end == pos) {
			end++;
		}
		pos = skipTrivia(sourceFile->text, pos);
		if (end < pos) {
			end = pos;
		}
		Diagnostic* diagnostic =
			newDiagnostic(sourceFile, TextRange{pos, end}, message,
						  {parameterRange, std::to_string(args.size())});
		if (headMessage != nullptr) {
			diagnostic = newDiagnosticChain(diagnostic, headMessage);
		}
		return diagnostic;
	}
}

// checker.go:10015 — isPromiseResolveArityError
bool Checker::isPromiseResolveArityError(Node* node) {
	if (!isCallExpression(node) || !isIdentifier(node->expression())) {
		return false;
	}
	Symbol* symbol =
		resolveName(node->expression(), node->expression()->text(),
					SymbolFlagsValue, nullptr /*nameNotFoundMessage*/,
					false /*isUse*/, false);
	if (symbol == nullptr) {
		return false;
	}
	Node* decl = symbol->valueDeclaration;
	if (decl == nullptr || !isParameterDeclaration(decl) ||
		!isFunctionExpressionOrArrowFunction(decl->parent) ||
		!isNewExpression(decl->parent->parent) ||
		!isIdentifier(decl->parent->parent->expression())) {
		return false;
	}
	Symbol* globalPromiseSymbol = getGlobalPromiseConstructorSymbolOrNil();
	if (globalPromiseSymbol == nullptr) {
		return false;
	}
	Symbol* constructorSymbol =
		getResolvedSymbol(decl->parent->parent->expression());
	return constructorSymbol == globalPromiseSymbol;
}

// checker.go:10045 — getTypeArgumentArityError
Diagnostic* Checker::getTypeArgumentArityError(
	Node* node, const std::vector<Signature*>& signatures,
	const std::vector<Node*>& typeArguments,
	const DiagnosticMessage* headMessage) {
	Diagnostic* diagnostic;
	int argCount = static_cast<int>(typeArguments.size());
	SourceFile* sourceFile = getSourceFileOfNode(node);
	NodeList* typeArgumentList = node->typeArgumentList();
	TextRange loc{skipTrivia(sourceFile->text, typeArgumentList->loc.pos()),
				  typeArgumentList->loc.end()};
	if (signatures.size() == 1) {
		// No overloads exist
		Signature* sig = signatures[0];
		int minCount = getMinTypeArgumentCount(sig->typeParameters);
		int maxCount = static_cast<int>(sig->typeParameters.size());
		std::string expected = std::to_string(minCount);
		if (minCount < maxCount) {
			expected = expected + "-" + std::to_string(maxCount);
		}
		diagnostic =
			newDiagnostic(sourceFile, loc, Expected_0_type_arguments_but_got_1,
						  {expected, std::to_string(argCount)});
	} else {
		// Overloads exist
		int belowArgCount = INT_MIN;
		int aboveArgCount = INT_MAX;
		for (Signature* sig : signatures) {
			int minCount = getMinTypeArgumentCount(sig->typeParameters);
			int maxCount = static_cast<int>(sig->typeParameters.size());
			if (minCount > argCount) {
				aboveArgCount = std::min(aboveArgCount, minCount);
			} else if (maxCount < argCount) {
				belowArgCount = std::max(belowArgCount, maxCount);
			}
		}
		if (belowArgCount != INT_MIN && aboveArgCount != INT_MAX) {
			diagnostic = newDiagnostic(
				sourceFile, loc,
				No_overload_expects_0_type_arguments_but_overloads_do_exist_that_expect_either_1_or_2_type_arguments,
				{std::to_string(argCount), std::to_string(belowArgCount),
				 std::to_string(aboveArgCount)});
		} else {
			diagnostic = newDiagnostic(
				sourceFile, loc, Expected_0_type_arguments_but_got_1,
				{std::to_string(ifElse(belowArgCount == INT_MIN, aboveArgCount,
									   belowArgCount)),
				 std::to_string(argCount)});
		}
	}
	if (headMessage != nullptr) {
		diagnostic = newDiagnosticChain(diagnostic, headMessage);
	}
	return diagnostic;
}

// checker.go:10086 — reportCannotInvokePossiblyNullOrUndefinedError
void Checker::reportCannotInvokePossiblyNullOrUndefinedError(Node* node,
															 TypeFacts facts) {
	error(node,
		  (facts & TypeFactsIsUndefined) != 0
			  ? ((facts & TypeFactsIsNull) != 0
					 ? Cannot_invoke_an_object_which_is_possibly_null_or_undefined
					 : Cannot_invoke_an_object_which_is_possibly_undefined)
			  : Cannot_invoke_an_object_which_is_possibly_null);
}

// checker.go:10094 — resolveUntypedCall
Signature* Checker::resolveUntypedCall(Node* node) {
	if (callLikeExpressionMayHaveTypeArguments(node)) {
		// Check type arguments even though we will give an error that untyped calls may not accept type arguments.
		// This gets us diagnostics for the type arguments and marks them as referenced.
		checkSourceElements(node->typeArguments());
	}
	switch (node->kind) {
	case Kind::TaggedTemplateExpression:
		checkExpression(node->as<TaggedTemplateExpression>()->Template);
		break;
	case Kind::JsxOpeningElement:
	case Kind::JsxSelfClosingElement:
		checkExpression(node->attributes());
		break;
	case Kind::BinaryExpression:
		checkExpression(node->as<BinaryExpression>()->Left);
		break;
	case Kind::CallExpression:
	case Kind::NewExpression:
		for (Node* argument : node->arguments()) {
			checkExpression(argument);
		}
		break;
	default:
		break;
	}
	return anySignature;
}

// checker.go:10115 — resolveErrorCall
Signature* Checker::resolveErrorCall(Node* node) {
	resolveUntypedCall(node);
	return unknownSignature;
}

// checker.go:10125 — isUntypedFunctionCall
bool Checker::isUntypedFunctionCall(Type* funcType, Type* apparentFuncType,
									int numCallSignatures,
									int numConstructSignatures) {
	// We exclude union types because we may have a union of function types that happen to have no common signatures.
	return isTypeAny(funcType) ||
		(isTypeAny(apparentFuncType) &&
		 (funcType->flags & TypeFlagsTypeParameter) != 0) ||
		(numCallSignatures == 0 && numConstructSignatures == 0 &&
		 (apparentFuncType->flags & TypeFlagsUnion) == 0 &&
		 (getReducedType(apparentFuncType)->flags & TypeFlagsNever) == 0 &&
		 isTypeAssignableTo(funcType, globalFunctionType));
}

// checker.go:10132 — invocationErrorDetails
Diagnostic* Checker::invocationErrorDetails(Node* errorTarget,
											Type* apparentType,
											SignatureKind kind) {
	Diagnostic* diagnostic = nullptr;
	bool isCall = kind == SignatureKind::Call;
	Type* awaitedType = getAwaitedType(apparentType);
	bool maybeMissingAwait =
		awaitedType != nullptr && !getSignaturesOfType(awaitedType, kind).empty();
	Node* target = errorTarget;
	if (isPropertyAccessExpression(errorTarget) &&
		isCallExpression(errorTarget->parent)) {
		target = errorTarget->name();
	}
	if ((apparentType->flags & TypeFlagsUnion) != 0) {
		const std::vector<Type*>& types = apparentType->types();
		bool hasSignatures = false;
		for (Type* constituent : types) {
			std::vector<Signature*> signatures =
				getSignaturesOfType(constituent, kind);
			if (!signatures.empty()) {
				hasSignatures = true;
				if (diagnostic != nullptr) {
					// Bail early if we already have an error, no chance of "No constituent of type is callable"
					break;
				}
			} else {
				// Error on the first non callable constituent only
				if (diagnostic == nullptr) {
					diagnostic = NewDiagnosticForNode(
						target,
						isCall ? Type_0_has_no_call_signatures
							   : Type_0_has_no_construct_signatures,
						{TypeToString(constituent)});
					diagnostic = NewDiagnosticChainForNode(
						diagnostic, target,
						isCall
							? Not_all_constituents_of_type_0_are_callable
							: Not_all_constituents_of_type_0_are_constructable,
						{TypeToString(apparentType)});
				}
				if (hasSignatures) {
					// Bail early if we already found a signature, no chance of "No constituent of type is callable"
					break;
				}
			}
		}
		if (!hasSignatures) {
			diagnostic = NewDiagnosticForNode(
				target,
				isCall ? No_constituent_of_type_0_is_callable
					   : No_constituent_of_type_0_is_constructable,
				{TypeToString(apparentType)});
		}
		if (diagnostic == nullptr) {
			diagnostic = NewDiagnosticForNode(
				target,
				isCall
					? Each_member_of_the_union_type_0_has_signatures_but_none_of_those_signatures_are_compatible_with_each_other
					: Each_member_of_the_union_type_0_has_construct_signatures_but_none_of_those_signatures_are_compatible_with_each_other,
				{TypeToString(apparentType)});
		}
	} else {
		diagnostic = NewDiagnosticChainForNode(
			diagnostic, target,
			isCall ? Type_0_has_no_call_signatures
				   : Type_0_has_no_construct_signatures,
			{TypeToString(apparentType)});
	}
	const DiagnosticMessage* headMessage =
		isCall ? This_expression_is_not_callable
			   : This_expression_is_not_constructable;
	// Diagnose get accessors incorrectly called as functions
	if (isCallExpression(errorTarget->parent) &&
		errorTarget->parent->arguments().empty()) {
		Symbol* resolvedSymbol = getResolvedSymbolOrNil(errorTarget);
		if (resolvedSymbol != nullptr &&
			(resolvedSymbol->flags & SymbolFlagsGetAccessor) != 0) {
			headMessage =
				This_expression_is_not_callable_because_it_is_a_get_accessor_Did_you_mean_to_use_it_without;
		}
	}
	diagnostic = NewDiagnosticChainForNode(diagnostic, target, headMessage);
	if (maybeMissingAwait) {
		diagnostic->AddRelatedInfo(NewDiagnosticForNode(
			errorTarget, Did_you_forget_to_use_await, {}));
	}
	return diagnostic;
}

// checker.go:10188 — invocationError
void Checker::invocationError(Node* errorTarget, Type* apparentType,
							  SignatureKind kind,
							  Diagnostic* relatedInformation) {
	Diagnostic* diagnostic =
		invocationErrorDetails(errorTarget, apparentType, kind);
	if (relatedInformation != nullptr) {
		diagnostic->AddRelatedInfo(relatedInformation);
	}
	diagnostic = addDiagnostic(diagnostic);
	invocationErrorRecovery(apparentType, kind, diagnostic);
}

// checker.go:10197 — invocationErrorRecovery
void Checker::invocationErrorRecovery(Type* apparentType, SignatureKind kind,
									  Diagnostic* diagnostic) {
	if (apparentType->symbol == nullptr) {
		return;
	}
	Node* importNode = exportTypeLinks.Get(apparentType->symbol)->originatingImport;
	// Create a diagnostic on the originating import if possible onto which we can attach a quickfix
	//  An import call expression cannot be rewritten into another form to correct the error - the only solution is to use `.default` at the use-site
	if (importNode != nullptr && !isImportCall(importNode)) {
		std::vector<Signature*> sigs = getSignaturesOfType(
			getTypeOfSymbol(exportTypeLinks.Get(apparentType->symbol)->target),
			kind);
		if (sigs.empty()) {
			return;
		}
		diagnostic->AddRelatedInfo(NewDiagnosticForNode(
			importNode,
			Type_originates_at_this_import_A_namespace_style_import_cannot_be_called_or_constructed_and_will_cause_a_failure_at_runtime_Consider_using_a_default_import_or_import_require_here_instead,
			{}));
	}
}

// checker.go:10213 — isGenericFunctionReturningFunction
bool Checker::isGenericFunctionReturningFunction(Signature* signature) {
	return !signature->typeParameters.empty() &&
		isFunctionType(getReturnTypeOfSignature(signature));
}

// checker.go:10217 — skippedGenericFunction
void Checker::skippedGenericFunction(Node* node, CheckMode checkMode) {
	if ((checkMode & CheckModeInferential) != 0) {
		// We have skipped a generic function during inferential typing. Obtain the inference context and
		// indicate this has occurred such that we know a second pass of inference is be needed.
		InferenceContext* context = getInferenceContext(node);
		context->flags |= InferenceFlagsSkippedGenericFunction;
	}
}

// checker.go:10226 — checkTaggedTemplateExpression
Type* Checker::checkTaggedTemplateExpression(Node* node) {
	if (!checkGrammarTaggedTemplateChain(
			node->as<TaggedTemplateExpression>())) {
		checkGrammarTypeArguments(node, node->typeArgumentList());
	}
	Signature* signature = getResolvedSignature(node, nullptr, CheckModeNormal);
	checkDeprecatedSignature(signature, node);
	return getReturnTypeOfSignature(signature);
}

// checker.go:10235 — checkParenthesizedExpression
Type* Checker::checkParenthesizedExpression(Node* node, CheckMode checkMode) {
	return checkExpressionEx(node->expression(), checkMode);
}

// checker.go:10239 — checkClassExpression
Type* Checker::checkClassExpression(Node* node) {
	checkClassLikeDeclaration(node);
	checkNodeDeferred(node);
	checkClassExpressionExternalHelpers(node->as<ClassExpression>());
	return getTypeOfSymbol(getSymbolOfDeclaration(node));
}

// checker.go:10246 — getFirstTransformableStaticClassElement
Node* Checker::getFirstTransformableStaticClassElement(Node* node) {
	bool willTransformStaticElementsOfDecoratedClass =
		!legacyDecorators &&
		languageVersion < LanguageFeatureMinimumTarget.ClassAndClassElementDecorators &&
		classOrConstructorParameterIsDecorated(false, node);
	bool willTransformPrivateElementsOrClassStaticBlocks =
		languageVersion <
			LanguageFeatureMinimumTarget.PrivateNamesAndClassStaticBlocks ||
		languageVersion <
			LanguageFeatureMinimumTarget.ClassAndClassElementDecorators;
	bool willTransformInitializers = !emitStandardClassFields;
	if (willTransformStaticElementsOfDecoratedClass ||
		willTransformPrivateElementsOrClassStaticBlocks) {
		for (Node* member : node->members()) {
			if (willTransformStaticElementsOfDecoratedClass &&
				classElementOrClassElementParameterIsDecorated(false, member,
															   node)) {
				if (Node* firstDecorator = firstOrNil(node->decorators());
					firstDecorator != nullptr) {
					return firstDecorator;
				}
				return node;
			} else if (willTransformPrivateElementsOrClassStaticBlocks) {
				if (isClassStaticBlockDeclaration(member)) {
					return member;
				} else if (isStatic(member) &&
						   (isPrivateIdentifierClassElementDeclaration(member) ||
							(willTransformInitializers &&
							 isInitializedProperty(member)))) {
					return member;
				}
			}
		}
	}
	return nullptr;
}

// checker.go:10273 — checkClassExpressionExternalHelpers
void Checker::checkClassExpressionExternalHelpers(ClassExpression* node) {
	if (node->name != nullptr) {
		return;
	}
	Node* parent = walkUpOuterExpressions(node);
	if (!isNamedEvaluationSource(parent)) {
		return;
	}
	bool willTransformESDecorators =
		!legacyDecorators &&
		languageVersion < LanguageFeatureMinimumTarget.ClassAndClassElementDecorators;
	Node* location;
	if (willTransformESDecorators &&
		classOrConstructorParameterIsDecorated(false, node)) {
		location = node;
		if (Node* firstDecorator = firstOrNil(node->decorators());
			firstDecorator != nullptr) {
			location = firstDecorator;
		}
	} else {
		location = getFirstTransformableStaticClassElement(node);
	}
	if (location != nullptr) {
		checkExternalEmitHelpers(location, ExternalEmitHelpersSetFunctionName);
		if ((isPropertyAssignment(parent) || isPropertyDeclaration(parent) ||
			 isBindingElement(parent)) &&
			isComputedPropertyName(parent->name())) {
			checkExternalEmitHelpers(location, ExternalEmitHelpersPropKey);
		}
	}
}

// checker.go:10301 — checkClassExpressionDeferred
void Checker::checkClassExpressionDeferred(Node* node) {
	checkSourceElements(node->members());
	registerForUnusedIdentifiersCheck(node);
}

// checker.go:10306 — checkFunctionExpressionOrObjectLiteralMethod
Type* Checker::checkFunctionExpressionOrObjectLiteralMethod(Node* node,
															CheckMode checkMode) {
	checkNodeDeferred(node);
	auto funcData = node->functionLikeData();
	if (funcData.fullSignature != nullptr && *funcData.fullSignature != nullptr) {
		checkSourceElement(*funcData.fullSignature);
	}
	if (isFunctionExpression(node)) {
		checkCollisionsForDeclarationName(node, node->name());
	}
	if ((checkMode & CheckModeSkipContextSensitive) != 0 &&
		isContextSensitive(node)) {
		// Skip parameters, return signature with return type that retains noncontextual parts so inferences can still be drawn in an early stage
		if (node->type() == nullptr && !hasContextSensitiveParameters(node)) {
			// Return plain anyFunctionType if there is no possibility we'll make inferences from the return type
			Signature* contextualSignature = getContextualSignature(node);
			if (contextualSignature != nullptr &&
				couldContainTypeVariables(
					getReturnTypeOfSignature(contextualSignature))) {
				auto it = contextFreeTypes.find(node);
				if (it != contextFreeTypes.end()) {
					return it->second;
				}
				Type* returnType = getReturnTypeFromBody(node, checkMode);
				Signature* returnOnlySignature = newSignature(
					SignatureFlagsIsNonInferrable, nullptr,
					{} /*typeParameters*/, nullptr /*thisParameter*/, {},
					returnType, nullptr /*resolvedTypePredicate*/, 0);
				Type* returnOnlyType =
					newAnonymousType(node->symbol(), {}, {returnOnlySignature},
									 {}, {});
				returnOnlyType->objectFlags |= ObjectFlagsNonInferrableType;
				contextFreeTypes[node] = returnOnlyType;
				return returnOnlyType;
			}
		}
		return anyFunctionType;
	}
	// Grammar checking
	bool hasGrammarError = checkGrammarFunctionLikeDeclaration(node);
	if (!hasGrammarError && isFunctionExpression(node)) {
		checkGrammarForGenerator(node);
	}
	if (funcData.fullSignature != nullptr && *funcData.fullSignature != nullptr) {
		if (getContextualCallSignature(
				getTypeFromTypeNode(*funcData.fullSignature), node) == nullptr) {
			error(*funcData.fullSignature,
				  A_JSDoc_type_tag_on_a_function_must_have_a_signature_with_the_correct_number_of_arguments);
		}
	}
	contextuallyCheckFunctionExpressionOrObjectLiteralMethod(node, checkMode);
	return getTypeOfSymbol(getSymbolOfDeclaration(node));
}

// checker.go:10347 — contextuallyCheckFunctionExpressionOrObjectLiteralMethod
void Checker::contextuallyCheckFunctionExpressionOrObjectLiteralMethod(
	Node* node, CheckMode checkMode) {
	NodeLinks* links = nodeLinks.Get(node);
	// Check if function expression is contextually typed and assign parameter types if so.
	if ((links->flags & NodeCheckFlagsContextChecked) == 0) {
		Signature* contextualSignature = getContextualSignature(node);
		// If a type check is started at a function expression that is an argument of a function call, obtaining the
		// contextual type may recursively get back to here during overload resolution of the call. If so, we will have
		// already assigned contextual types.
		if ((links->flags & NodeCheckFlagsContextChecked) == 0) {
			links->flags |= NodeCheckFlagsContextChecked;
			Signature* signature = firstOrNil(getSignaturesOfType(
				getTypeOfSymbol(getSymbolOfDeclaration(node)),
				SignatureKind::Call));
			if (signature == nullptr) {
				return;
			}
			if (isContextSensitive(node)) {
				if (contextualSignature != nullptr) {
					InferenceContext* inferenceContext = getInferenceContext(node);
					Signature* instantiatedContextualSignature = nullptr;
					if ((checkMode & CheckModeInferential) != 0) {
						inferFromAnnotatedParametersAndReturn(
							signature, contextualSignature, inferenceContext);
						Type* restType =
							getEffectiveRestType(contextualSignature);
						if (restType != nullptr &&
							(restType->flags & TypeFlagsTypeParameter) != 0) {
							instantiatedContextualSignature =
								instantiateSignature(
									contextualSignature,
									inferenceContext->nonFixingMapper);
						}
					}
					if (instantiatedContextualSignature == nullptr) {
						if (inferenceContext != nullptr) {
							instantiatedContextualSignature =
								instantiateSignature(contextualSignature,
													 inferenceContext->mapper);
						} else {
							instantiatedContextualSignature =
								contextualSignature;
						}
					}
					assignContextualParameterTypes(signature,
												   instantiatedContextualSignature);
				} else {
					// Force resolution of all parameter types such that the absence of a contextual type is consistently reflected.
					assignNonContextualParameterTypes(signature);
				}
			} else if (contextualSignature != nullptr &&
					   node->typeParameterList() == nullptr &&
					   contextualSignature->parameters.size() >
						   node->parameters().size()) {
				InferenceContext* inferenceContext = getInferenceContext(node);
				if ((checkMode & CheckModeInferential) != 0) {
					inferFromAnnotatedParametersAndReturn(
						signature, contextualSignature, inferenceContext);
				}
			}
			if (contextualSignature != nullptr &&
				getReturnTypeFromAnnotation(node) == nullptr &&
				signature->resolvedReturnType == nullptr) {
				// resolvedReturnType is cached indefinitely, so the return type here has to be computed without CheckModeSkipContextSensitive;
				// otherwise anyFunctionType could leak as part of the computed (and cached) return type.
				Type* returnType = getReturnTypeFromBody(
					node, checkMode & ~CheckModeSkipContextSensitive);
				if (signature->resolvedReturnType == nullptr) {
					signature->resolvedReturnType = returnType;
				}
			}
			checkSignatureDeclaration(node);
		}
	}
}

// checker.go:10403 — checkFunctionExpressionOrObjectLiteralMethodDeferred
void Checker::checkFunctionExpressionOrObjectLiteralMethodDeferred(Node* node) {
	FunctionFlags functionFlags = getFunctionFlags(node);
	Type* returnType = getReturnTypeFromAnnotation(node);
	checkAllCodePathsInNonVoidFunctionReturnOrThrow(node, returnType);
	Node* body = node->body();
	if (body != nullptr) {
		if (node->type() == nullptr) {
			// There are some checks that are only performed in getReturnTypeFromBody, that may produce errors
			// we need. An example is the noImplicitAny errors resulting from widening the return expression
			// of a function. Because checking of function expression bodies is deferred, there was never an
			// appropriate time to do this during the main walk of the file (see the comment at the top of
			// checkFunctionExpressionBodies). So it must be done now.
			getReturnTypeOfSignature(getSignatureFromDeclaration(node));
		}
		if (isBlock(body)) {
			checkSourceElement(body);
		} else {
			// From within an async function you can return either a non-promise value or a promise. Any
			// Promise/A+ compatible implementation will always assimilate any foreign promise, so we
			// should not be checking assignability of a promise to the return type. Instead, we need to
			// check assignability of the awaited type of the expression body against the promised type of
			// its return type annotation.
			Type* exprType = checkExpression(body);
			if (returnType != nullptr) {
				Type* returnOrPromisedType =
					unwrapReturnType(returnType, functionFlags);
				if (returnOrPromisedType != nullptr) {
					checkReturnExpression(node, returnOrPromisedType, body, body,
										  exprType, false);
				}
			}
		}
	}
}

// checker.go:10436 — inferFromAnnotatedParametersAndReturn
void Checker::inferFromAnnotatedParametersAndReturn(
	Signature* sig, Signature* context, InferenceContext* inferenceContext) {
	int length = static_cast<int>(sig->parameters.size()) -
		ifElse(signatureHasRestParameter(sig), 1, 0);
	for (int i = 0; i < length; i++) {
		Node* declaration = sig->parameters[i]->valueDeclaration;
		Node* typeNode = declaration->type();
		if (typeNode != nullptr) {
			Type* source = addOptionalityEx(
				getTypeFromTypeNode(typeNode), false /*isProperty*/,
				isOptionalDeclaration(declaration));
			Type* target = getTypeAtPosition(context, i);
			inferTypes(inferenceContext->inferences, source, target,
					   InferencePriorityNone, false);
		}
	}
	if (Node* declaration = sig->declaration; declaration != nullptr) {
		if (Node* returnTypeNode = declaration->type();
			returnTypeNode != nullptr) {
			Type* source = getTypeFromTypeNode(returnTypeNode);
			Type* target = getReturnTypeOfSignature(context);
			inferTypes(inferenceContext->inferences, source, target,
					   InferencePriorityNone, false);
		}
	}
}

// checker.go:10461 — getContextualSignature
Signature* Checker::getContextualSignature(Node* node) {
	Type* t = getApparentTypeOfContextualType(node, ContextFlagsSignature);
	if (t == nullptr) {
		return nullptr;
	}
	if ((t->flags & TypeFlagsUnion) == 0) {
		return getContextualCallSignature(t, node);
	}
	std::vector<Signature*> signatureList;
	const std::vector<Type*>& types = t->types();
	for (Type* current : types) {
		Signature* signature = getContextualCallSignature(current, node);
		if (signature != nullptr) {
			if (!signatureList.empty() &&
				compareSignaturesIdentical(
					signatureList[0], signature, false /*partialMatch*/,
					true /*ignoreThisTypes*/, true /*ignoreReturnTypes*/,
					[this](Type* a, Type* b) {
						return compareTypesIdentical(a, b);
					}) == Ternary::False) {
				// Signatures aren't identical, do not use
				return nullptr;
			}
			// Use this signature for contextual union signature
			signatureList.push_back(signature);
		}
	}
	switch (signatureList.size()) {
	case 0:
		return nullptr;
	case 1:
		return signatureList[0];
	default:
		break;
	}
	// Result is union of signatures collected (return type is union of return types of this signature set)
	return createUnionSignature(signatureList[0], signatureList);
}

// ---------------------------------------------------------------------------
// === dep stubs — removed when owner slice lands ===
// Callees owned by other slices. Never called successfully until then.
// ---------------------------------------------------------------------------

// (deduped: checkNonNullType defined in owning slice file)
// (deduped: checkNonNullTypeWithReporter defined in checker_utilities.cpp)

// (deduped: getFlowTypeOfAccessExpression defined in owning slice file)
bool Checker::isNodeWithinClass(Node* node, Node* classDeclaration) {
	TSC_UNREACHABLE("isNodeWithinClass — expressions-b slice dep");
}

// owner: relater slice (relater.go:358,428,1891)
bool Checker::checkTypeRelatedToEx(Type* source, Type* target,
								   Relation* relation, Node* errorNode,
								   const DiagnosticMessage* headMessage,
								   std::vector<Diagnostic*>* diagnosticOutput) {
	TSC_UNREACHABLE("checkTypeRelatedToEx — relater slice dep");
}
bool Checker::checkTypeRelatedToAndOptionallyElaborate(
	Type* source, Type* target, Relation* relation, Node* errorNode, Node* expr,
	const DiagnosticMessage* headMessage,
	std::vector<Diagnostic*>* diagnosticOutput) {
	TSC_UNREACHABLE("checkTypeRelatedToAndOptionallyElaborate — relater slice dep");
}
Type* Checker::getNonArrayRestType(Signature* signature) {
	TSC_UNREACHABLE("getNonArrayRestType — relater slice dep");
}

// (deduped: getTypeOfDottedName defined in owning slice file)

// (deduped: resolveExternalModuleTypeByLiteral defined in owning slice file)
// (deduped: getTypeWithSyntheticDefaultOnly defined in owning slice file)
// (deduped: getTypeWithSyntheticDefaultImportType defined in owning slice file)
// (deduped: addDeprecatedSuggestionWorker defined in owning slice file)

// owner: printer slice (printer.go:179)
std::string Checker::signatureToString(Signature* signature) {
	TSC_UNREACHABLE("signatureToString — printer slice dep");
}

// owner: jsx slice (jsx.go:198,545,591)
Signature* Checker::resolveJsxOpeningLikeElement(
	Node* node, std::vector<Signature*>* candidatesOutArray,
	CheckMode checkMode) {
	TSC_UNREACHABLE("resolveJsxOpeningLikeElement — jsx slice dep");
}
std::vector<Type*> Checker::inferJsxTypeArguments(Node* node,
												  Signature* signature,
												  CheckMode checkMode,
												  InferenceContext* context) {
	TSC_UNREACHABLE("inferJsxTypeArguments — jsx slice dep");
}
bool Checker::checkApplicableSignatureForJsxCallLikeElement(
	Node* node, Signature* signature, Relation* relation, CheckMode checkMode,
	bool reportErrors, std::vector<Diagnostic*>* diagnosticOutput) {
	TSC_UNREACHABLE(
		"checkApplicableSignatureForJsxCallLikeElement — jsx slice dep");
}

// (deduped: cloneInferredPartOfContext defined in owning slice file)
// (deduped: getMapperFromContext defined in owning slice file)
// (deduped: createOuterReturnMapper defined in owning slice file)

// (deduped: assignNonContextualParameterTypes defined in owning slice file)

// (deduped: callLikeExpressionMayHaveTypeArguments defined in owning slice file)

} // namespace tsc::checker
