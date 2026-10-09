// checker_expressions_c.cpp — checker.go:12390-14184 "expr_c" slice.
// getThisContainer … getCannotFindNameDiagnosticForName (the range ends at the
// last complete function before GetDiagnostics at 14185). Ported faithfully
// from tsc/internal/checker/checker.go: every branch, every early return,
// every panic is preserved (panics → TSC_UNREACHABLE).
//
// Already ported elsewhere in this range (left in place, not duplicated here):
//   - getThisContainer                       (checker.cpp)
//   - isInParameterInitializerBeforeContainingFunction (checker_grammar.cpp)
//   - getCannotFindNameDiagnosticForName     (checker.cpp)
//
// Cross-slice callees without a declaration get one TSC_UNREACHABLE definition
// each at the bottom under "// === dep stubs ===".

#include "internal/ast/ast.h"
#include "internal/binder/binder.h"
#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/checker/types.h"
#include "internal/core/types.h"
#include "internal/evaluator/evaluator.h"
#include "internal/scanner/scanner.h"
#include "internal/tspath/tspath.h"

#include <algorithm>
#include <unordered_set>

namespace tsc::checker {

// ---------------------------------------------------------------------------
// Declarations for free functions defined (real bodies) in other checker TUs.
// ---------------------------------------------------------------------------

// utilities.go:757 — real definition in checker_decltypes.cpp.
ModifierFlags getDeclarationModifierFlagsFromSymbol(Symbol* s);

namespace {

// ---------------------------------------------------------------------------
// core/collections helpers (file-local, per PORTING.md)
// ---------------------------------------------------------------------------

template <class T>
std::vector<T> concatenate(std::vector<T> a, const std::vector<T>& b) {
	a.insert(a.end(), b.begin(), b.end());
	return a;
}

template <class T, class F>
auto mapVec(const std::vector<T>& v, F&& f)
	-> std::vector<decltype(f(std::declval<T>()))> {
	std::vector<decltype(f(std::declval<T>()))> result;
	result.reserve(v.size());
	for (const T& x : v) {
		result.push_back(f(x));
	}
	return result;
}

// core.SameMap — returns the input unchanged when every element maps to itself.
template <class T, class F>
std::vector<T> sameMap(const std::vector<T>& v, F&& f) {
	std::vector<T> result;
	result.reserve(v.size());
	bool same = true;
	for (const T& x : v) {
		T y = f(x);
		if (y != x) {
			same = false;
		}
		result.push_back(y);
	}
	return same ? v : result;
}

template <class T, class F>
std::vector<T> filterVec(const std::vector<T>& v, F&& f) {
	std::vector<T> result;
	for (const T& x : v) {
		if (f(x)) {
			result.push_back(x);
		}
	}
	return result;
}

template <class R, class F>
bool someList(R&& v, F&& f) {
	for (auto& x : v) {
		if (f(x)) {
			return true;
		}
	}
	return false;
}

template <class T, class F>
bool everyList(const std::vector<T>& v, F&& f) {
	for (const T& x : v) {
		if (!f(x)) {
			return false;
		}
	}
	return true;
}

// core.Find — first element satisfying pred, or nullptr.
template <class T, class F>
T* findOrNull(const std::vector<T*>& v, F&& f) {
	for (T* x : v) {
		if (f(x)) {
			return x;
		}
	}
	return nullptr;
}

// core.OrElse for pointer-like types.
template <class T>
T orElse(T a, T b) {
	return a != nullptr ? a : b;
}

// slices.Index — index of the first occurrence of value, or -1.
template <class R>
int indexOf(R&& values, const auto& v) {
	for (size_t i = 0; i < values.size(); i++) {
		if (values[i] == v) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

// ---------------------------------------------------------------------------
// checker.go / utilities.go free helpers (file-local copies)
// ---------------------------------------------------------------------------

// utilities.go:1116 — canHaveFlowNode
[[maybe_unused]] bool canHaveFlowNode(Node* node) {
	return node->flowNodeData().flowNode != nullptr;
}

// flow.go:69 — getFlowNodeOfNode
FlowNode* getFlowNodeOfNode(Node* node) {
	auto data = node->flowNodeData();
	return data.flowNode != nullptr ? *data.flowNode : nullptr;
}

// utilities.go:286 — IsTypeAny
// isTypeAny lives in checker_utilities.cpp (declared in checker.h).

// utilities.go — isTupleType (file-local copy)
bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 &&
		   (t->AsTypeReference()->target->objectFlags & ObjectFlagsTuple) != 0;
}

// utilities.go:57 — signatureHasRestParameter
bool signatureHasRestParameter(Signature* sig) {
	return (sig->flags & SignatureFlagsHasRestParameter) != 0;
}

// utilities.go:875 — isLiteralExpressionOfObject
bool isLiteralExpressionOfObject(Node* node) {
	switch (node->kind) {
		case Kind::ObjectLiteralExpression:
		case Kind::ArrayLiteralExpression:
		case Kind::RegularExpressionLiteral:
		case Kind::FunctionExpression:
		case Kind::ClassExpression:
			return true;
		default:
			return false;
	}
}

// utilities.go:346 — isTypeAssertion
bool isTypeAssertion(Node* node) {
	return isAssertionExpression(skipParentheses(node));
}

// utilities.go — hasDotDotDotToken
bool hasDotDotDotToken(Node* node) {
	switch (node->kind) {
	case Kind::Parameter:
		return node->as<ParameterDeclaration>()->DotDotDotToken != nullptr;
	case Kind::BindingElement:
		return node->as<BindingElement>()->DotDotDotToken != nullptr;
	case Kind::NamedTupleMember:
		return node->as<NamedTupleMember>()->DotDotDotToken != nullptr;
	default:
		return false;
	}
}

// checker.cpp — getBooleanLiteralValue (file-static there too).
bool getBooleanLiteralValue(Type* t) {
	return std::get<bool>(t->AsLiteralType()->value);
}

// checker.cpp — isTypeUsableAsPropertyName
bool isTypeUsableAsPropertyName(Type* t) {
	return (t->flags & TypeFlagsStringOrNumberLiteralOrUnique) != 0;
}

// checker.cpp — getPropertyNameFromType: the symbolic name for a member from
// its type.
std::string getPropertyNameFromType(Type* t) {
	if (t->flags & TypeFlagsStringLiteral) {
		return std::get<std::string>(t->AsLiteralType()->value);
	}
	if (t->flags & TypeFlagsNumberLiteral) {
		return std::get<Number>(t->AsLiteralType()->value).string();
	}
	if (t->flags & TypeFlagsUniqueESSymbol) {
		return t->AsUniqueESSymbolType()->name;
	}
	TSC_UNREACHABLE("Unhandled case in getPropertyNameFromType");
}

// checker.go — someType / everyType (file-local copies)
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

bool everyType(Type* t, const std::function<bool(Type*)>& f) {
	if (t->flags & TypeFlagsUnion) {
		for (Type* u : t->types()) {
			if (!f(u)) {
				return false;
			}
		}
		return true;
	}
	return f(t);
}

// ast.IsCompoundAssignment — utilities.go
bool isCompoundAssignment(Kind kind) {
	return kind >= KindFirstCompoundAssignment && kind <= KindLastCompoundAssignment;
}

} // namespace

// ---------------------------------------------------------------------------
// checker.go:12456 — checkThisInStaticClassFieldInitializerInDecoratedClass
// ---------------------------------------------------------------------------

void Checker::checkThisInStaticClassFieldInitializerInDecoratedClass(Node* thisExpression, Node* container) {
	if (isPropertyDeclaration(container) && hasStaticModifier(container) && legacyDecorators) {
		Node* initializer = container->initializer();
		if (initializer != nullptr && initializer->loc.containsInclusive(thisExpression->pos()) && hasDecorators(container->parent)) {
			error(thisExpression, Cannot_use_this_in_a_static_property_initializer_of_a_decorated_class);
		}
	}
}

// ---------------------------------------------------------------------------
// checker.go:12465 — checkThisBeforeSuper
// ---------------------------------------------------------------------------

void Checker::checkThisBeforeSuper(Node* node, Node* container, const DiagnosticMessage* diagnosticMessage) {
	Node* containingClassDecl = container->parent;
	Node* baseTypeNode = getClassExtendsHeritageElement(containingClassDecl);
	// If a containing class does not have extends clause or the class extends null
	// skip checking whether super statement is called before "this" accessing.
	if (baseTypeNode != nullptr && !classDeclarationExtendsNull(containingClassDecl)) {
		auto data = node->flowNodeData();
		if (data.flowNode != nullptr && !isPostSuperFlowNode(*data.flowNode, false /*noCacheCheck*/)) {
			error(node, diagnosticMessage);
		}
	}
}

// ---------------------------------------------------------------------------
// checker.go:12482 — classDeclarationExtendsNull
// ---------------------------------------------------------------------------

// Check if the given class-declaration extends null then return true.
// Otherwise, return false
bool Checker::classDeclarationExtendsNull(Node* classDecl) {
	Symbol* classSymbol = getSymbolOfDeclaration(classDecl);
	Type* classInstanceType = getDeclaredTypeOfSymbol(classSymbol);
	Type* baseConstructorType = getBaseConstructorTypeOfClass(classInstanceType);
	return baseConstructorType == nullWideningType;
}

// ---------------------------------------------------------------------------
// checker.go:12489 — checkAssertion
// ---------------------------------------------------------------------------

Type* Checker::checkAssertion(Node* node, CheckMode checkMode) {
	if (node->kind == Kind::TypeAssertionExpression) {
		SourceFile* file = getSourceFileOfNode(node);
		if (file != nullptr && tspath::fileExtensionIsOneOf(file->FileName(), {tspath::extensionMts, tspath::extensionCts})) {
			grammarErrorOnNode(node, This_syntax_is_reserved_in_files_with_the_mts_or_cts_extension_Use_an_as_expression_instead);
		}
		if (shouldCheckErasableSyntax(node)) {
			addDiagnostic(newDiagnostic(getSourceFileOfNode(node),
				TextRange{skipTrivia(getSourceFileOfNode(node)->text, node->pos()), node->expression()->pos()},
				This_syntax_is_not_allowed_when_erasableSyntaxOnly_is_enabled));
		}
	}
	Node* typeNode = node->type();
	Type* exprType = checkExpressionEx(node->expression(), checkMode);
	// Always check the type node so its identifiers are resolved. resolveName knows not
	// to resolve (or report an error for) the `const` in a `const` assertion, so this is
	// safe even for `x as const` and keeps diagnostics stable regardless of traversal order.
	checkSourceElement(typeNode);
	if (isConstTypeReference(typeNode)) {
		if (!isValidConstAssertionArgument(node->expression())) {
			error(node->expression(), A_const_assertion_can_only_be_applied_to_references_to_enum_members_or_string_number_boolean_array_or_object_literals);
		}
		return getRegularTypeOfLiteralType(exprType);
	}
	AssertionLinks* links = assertionLinks.Get(node);
	links->exprType = exprType;
	checkNodeDeferred(node);
	return getTypeFromTypeNode(typeNode);
}

// ---------------------------------------------------------------------------
// checker.go:12517 — checkAssertionDeferred
// ---------------------------------------------------------------------------

void Checker::checkAssertionDeferred(Node* node) {
	Node* typeNode = node->type();
	Type* exprType = getRegularTypeOfObjectLiteral(getBaseTypeOfLiteralType(assertionLinks.Get(node)->exprType));
	Type* targetType = getTypeFromTypeNode(typeNode);
	if (!isErrorType(targetType)) {
		Type* widenedType = getWidenedType(exprType);
		if (!isTypeComparableTo(targetType, widenedType)) {
			Node* errNode = node;
			if ((typeNode->flags & NodeFlagsReparsed) != 0) {
				errNode = typeNode;
			}
			checkTypeComparableTo(exprType, targetType, errNode, Conversion_of_type_0_to_type_1_may_be_a_mistake_because_neither_type_sufficiently_overlaps_with_the_other_If_this_was_intentional_convert_the_expression_to_unknown_first);
		}
	}
}

// ---------------------------------------------------------------------------
// checker.go:12533 — checkBinaryExpression
// ---------------------------------------------------------------------------

Type* Checker::checkBinaryExpression(Node* node, CheckMode checkMode) {
	BinaryExpression* binary = node->as<BinaryExpression>();
	return checkBinaryLikeExpression(binary->Left, binary->OperatorToken, binary->Right, checkMode, node);
}

// ---------------------------------------------------------------------------
// checker.go:12538 — checkBinaryLikeExpression
// ---------------------------------------------------------------------------

Type* Checker::checkBinaryLikeExpression(Node* left, Node* operatorToken, Node* right, CheckMode checkMode, Node* errorNode) {
	Kind operatorKind = operatorToken->kind;
	if (operatorKind == Kind::EqualsToken && (left->kind == Kind::ObjectLiteralExpression || left->kind == Kind::ArrayLiteralExpression)) {
		return checkDestructuringAssignment(left, checkExpressionEx(right, checkMode), checkMode, right->kind == Kind::ThisKeyword);
	}
	Type* leftType = checkExpressionEx(left, checkMode);
	Type* rightType = checkExpressionEx(right, checkMode);
	if (isLogicalOrCoalescingBinaryOperator(operatorKind)) {
		Node* parent = left->parent->parent;
		while (isParenthesizedExpression(parent) || isLogicalOrCoalescingBinaryExpression(parent)) {
			parent = parent->parent;
		}
		if (operatorKind == Kind::AmpersandAmpersandToken || isIfStatement(parent)) {
			Node* body = nullptr;
			if (isIfStatement(parent)) {
				body = parent->as<IfStatement>()->ThenStatement;
			}
			checkTestingKnownTruthyCallableOrAwaitableOrEnumMemberType(left, leftType, body);
		}
		if (isLogicalBinaryOperator(operatorKind)) {
			checkTruthinessOfType(leftType, left);
		}
	}
	switch (operatorKind) {
	case Kind::AsteriskToken:
	case Kind::AsteriskAsteriskToken:
	case Kind::AsteriskEqualsToken:
	case Kind::AsteriskAsteriskEqualsToken:
	case Kind::SlashToken:
	case Kind::SlashEqualsToken:
	case Kind::PercentToken:
	case Kind::PercentEqualsToken:
	case Kind::MinusToken:
	case Kind::MinusEqualsToken:
	case Kind::LessThanLessThanToken:
	case Kind::LessThanLessThanEqualsToken:
	case Kind::GreaterThanGreaterThanToken:
	case Kind::GreaterThanGreaterThanEqualsToken:
	case Kind::GreaterThanGreaterThanGreaterThanToken:
	case Kind::GreaterThanGreaterThanGreaterThanEqualsToken:
	case Kind::BarToken:
	case Kind::BarEqualsToken:
	case Kind::CaretToken:
	case Kind::CaretEqualsToken:
	case Kind::AmpersandToken:
	case Kind::AmpersandEqualsToken: {
		if (leftType == silentNeverType || rightType == silentNeverType) {
			return silentNeverType;
		}
		leftType = checkNonNullType(leftType, left);
		rightType = checkNonNullType(rightType, right);
		// if a user tries to apply a bitwise operator to 2 boolean operands
		// try and return them a helpful suggestion
		if ((leftType->flags & TypeFlagsBooleanLike) != 0 && (rightType->flags & TypeFlagsBooleanLike) != 0) {
			Kind suggestedOperator = getSuggestedBooleanOperator(operatorKind);
			if (suggestedOperator != Kind::Unknown) {
				error(operatorToken, The_0_operator_is_not_allowed_for_boolean_types_Consider_using_1_instead,
					{std::string(tokenToString(operatorToken->kind)), std::string(tokenToString(suggestedOperator))});
				return numberType;
			}
		}
		// otherwise just check each operand separately and report errors as normal
		bool leftOk = checkArithmeticOperandType(left, leftType, The_left_hand_side_of_an_arithmetic_operation_must_be_of_type_any_number_bigint_or_an_enum_type, true /*isAwaitValid*/);
		bool rightOk = checkArithmeticOperandType(right, rightType, The_right_hand_side_of_an_arithmetic_operation_must_be_of_type_any_number_bigint_or_an_enum_type, true /*isAwaitValid*/);
		Type* resultType;
		// If both are any or unknown, allow operation; assume it will resolve to number
		if ((isTypeAssignableToKind(leftType, TypeFlagsAnyOrUnknown) && isTypeAssignableToKind(rightType, TypeFlagsAnyOrUnknown)) ||
			(!maybeTypeOfKind(leftType, TypeFlagsBigIntLike) && !maybeTypeOfKind(rightType, TypeFlagsBigIntLike))) {
			resultType = numberType;
		} else if (bothAreBigIntLike(leftType, rightType)) {
			switch (operatorKind) {
			case Kind::GreaterThanGreaterThanGreaterThanToken:
			case Kind::GreaterThanGreaterThanGreaterThanEqualsToken:
				reportOperatorError(leftType, operatorKind, rightType, errorNode, nullptr);
				break;
			case Kind::AsteriskAsteriskToken:
			case Kind::AsteriskAsteriskEqualsToken:
				if (languageVersion < ScriptTarget::ES2016) {
					error(errorNode, Exponentiation_cannot_be_performed_on_bigint_values_unless_the_target_option_is_set_to_es2016_or_later);
				}
				break;
			}
			resultType = bigintType;
		} else {
			reportOperatorError(leftType, operatorKind, rightType, errorNode, [this](Type* l, Type* r) {
				return bothAreBigIntLike(l, r);
			});
			resultType = errorType;
		}
		if (leftOk && rightOk) {
			checkAssignmentOperator(left, operatorKind, right, leftType, resultType);
			switch (operatorKind) {
			case Kind::LessThanLessThanToken:
			case Kind::LessThanLessThanEqualsToken:
			case Kind::GreaterThanGreaterThanToken:
			case Kind::GreaterThanGreaterThanEqualsToken:
			case Kind::GreaterThanGreaterThanGreaterThanToken:
			case Kind::GreaterThanGreaterThanGreaterThanEqualsToken: {
				EvalResult rhsEval = evaluate(right, right);
				if (auto* numValue = std::get_if<Number>(&rhsEval.Value); numValue != nullptr && numValue->abs().v >= 32) {
					// Elevate from suggestion to error within an enum member
					errorOrSuggestion(isEnumMember(walkUpParenthesizedExpressions(right->parent->parent)), errorNode,
						This_operation_can_be_simplified_This_shift_is_identical_to_0_1_2,
						{getTextOfNode(left), std::string(tokenToString(operatorKind)), numValue->remainder(Number(32)).string()});
				}
				break;
			}
			}
		}
		return resultType;
	}
	case Kind::PlusToken:
	case Kind::PlusEqualsToken: {
		if (leftType == silentNeverType || rightType == silentNeverType) {
			return silentNeverType;
		}
		if (!isTypeAssignableToKind(leftType, TypeFlagsStringLike) && !isTypeAssignableToKind(rightType, TypeFlagsStringLike)) {
			leftType = checkNonNullType(leftType, left);
			rightType = checkNonNullType(rightType, right);
		}
		Type* resultType = nullptr;
		if (isTypeAssignableToKindEx(leftType, TypeFlagsNumberLike, true /*strict*/) && isTypeAssignableToKindEx(rightType, TypeFlagsNumberLike, true /*strict*/)) {
			// Operands of an enum type are treated as having the primitive type Number.
			// If both operands are of the Number primitive type, the result is of the Number primitive type.
			resultType = numberType;
		} else if (isTypeAssignableToKindEx(leftType, TypeFlagsBigIntLike, true /*strict*/) && isTypeAssignableToKindEx(rightType, TypeFlagsBigIntLike, true /*strict*/)) {
			// If both operands are of the BigInt primitive type, the result is of the BigInt primitive type.
			resultType = bigintType;
		} else if (isTypeAssignableToKindEx(leftType, TypeFlagsStringLike, true /*strict*/) || isTypeAssignableToKindEx(rightType, TypeFlagsStringLike, true /*strict*/)) {
			// If one or both operands are of the String primitive type, the result is of the String primitive type.
			resultType = stringType;
		} else if (isTypeAny(leftType) || isTypeAny(rightType)) {
			// Otherwise, the result is of type Any.
			// NOTE: unknown type here denotes error type. Old compiler treated this case as any type so do we.
			if (isErrorType(leftType) || isErrorType(rightType)) {
				resultType = errorType;
			} else {
				resultType = anyType;
			}
		}
		// Symbols are not allowed at all in arithmetic expressions
		if (resultType != nullptr && !checkForDisallowedESSymbolOperand(left, right, leftType, rightType, operatorKind)) {
			return resultType;
		}
		if (resultType == nullptr) {
			// Types that have a reasonably good chance of being a valid operand type.
			// If both types have an awaited type of one of these, we'll assume the user
			// might be missing an await without doing an exhaustive check that inserting
			// await(s) will actually be a completely valid binary expression.
			TypeFlags closeEnoughKind = TypeFlagsNumberLike | TypeFlagsBigIntLike | TypeFlagsStringLike | TypeFlagsAnyOrUnknown;
			reportOperatorError(leftType, operatorKind, rightType, errorNode, [this, closeEnoughKind](Type* l, Type* r) {
				return isTypeAssignableToKind(l, closeEnoughKind) && isTypeAssignableToKind(r, closeEnoughKind);
			});
			return anyType;
		}
		if (operatorKind == Kind::PlusEqualsToken) {
			checkAssignmentOperator(left, operatorKind, right, leftType, resultType);
		}
		return resultType;
	}
	case Kind::LessThanToken:
	case Kind::GreaterThanToken:
	case Kind::LessThanEqualsToken:
	case Kind::GreaterThanEqualsToken:
		if (checkForDisallowedESSymbolOperand(left, right, leftType, rightType, operatorKind)) {
			leftType = getBaseTypeOfLiteralTypeForComparison(checkNonNullType(leftType, left));
			rightType = getBaseTypeOfLiteralTypeForComparison(checkNonNullType(rightType, right));
			reportOperatorErrorUnless(leftType, operatorKind, rightType, errorNode, [this](Type* l, Type* r) {
				if (isTypeAny(l) || isTypeAny(r)) {
					return true;
				}
				bool leftAssignableToNumber = isTypeAssignableTo(l, numberOrBigIntType);
				bool rightAssignableToNumber = isTypeAssignableTo(r, numberOrBigIntType);
				return (leftAssignableToNumber && rightAssignableToNumber) ||
					(!leftAssignableToNumber && !rightAssignableToNumber && areTypesComparable(l, r));
			});
		}
		return booleanType;
	case Kind::EqualsEqualsToken:
	case Kind::ExclamationEqualsToken:
	case Kind::EqualsEqualsEqualsToken:
	case Kind::ExclamationEqualsEqualsToken:
		// We suppress errors in CheckMode.TypeOnly (meaning the invocation came from getTypeOfExpression). During
		// control flow analysis it is possible for operands to temporarily have narrower types, and those narrower
		// types may cause the operands to not be comparable. We don't want such errors reported (see #46475).
		if ((checkMode & CheckModeTypeOnly) == 0) {
			if ((isLiteralExpressionOfObject(left) || isLiteralExpressionOfObject(right)) &&
				// only report for === and !== in JS, not == or !=
				(!isInJSFile(left) || (operatorKind == Kind::EqualsEqualsEqualsToken || operatorKind == Kind::ExclamationEqualsEqualsToken))) {
				bool eqType = operatorKind == Kind::EqualsEqualsToken || operatorKind == Kind::EqualsEqualsEqualsToken;
				error(errorNode, This_condition_will_always_return_0_since_JavaScript_compares_objects_by_reference_not_value, std::vector<std::string>{eqType ? "false" : "true"});
			}
			checkNaNEquality(errorNode, operatorKind, left, right);
			reportOperatorErrorUnless(leftType, operatorKind, rightType, errorNode, [this](Type* l, Type* r) {
				return isTypeEqualityComparableTo(l, r) || isTypeEqualityComparableTo(r, l);
			});
		}
		return booleanType;
	case Kind::InstanceOfKeyword:
		return checkInstanceOfExpression(left, right, leftType, rightType, checkMode);
	case Kind::InKeyword:
		return checkInExpression(left, right, leftType, rightType);
	case Kind::AmpersandAmpersandToken:
	case Kind::AmpersandAmpersandEqualsToken: {
		Type* resultType = leftType;
		if (hasTypeFacts(leftType, TypeFactsTruthy)) {
			Type* t = leftType;
			if (!strictNullChecks) {
				t = getBaseTypeOfLiteralType(rightType);
			}
			resultType = getUnionType({extractDefinitelyFalsyTypes(t), rightType});
		}
		if (operatorKind == Kind::AmpersandAmpersandEqualsToken) {
			checkAssignmentOperator(left, operatorKind, right, leftType, rightType);
		}
		return resultType;
	}
	case Kind::BarBarToken:
	case Kind::BarBarEqualsToken: {
		Type* resultType = leftType;
		if (hasTypeFacts(leftType, TypeFactsFalsy)) {
			resultType = getUnionTypeEx({GetNonNullableType(removeDefinitelyFalsyTypes(leftType)), rightType}, UnionReductionSubtype, nullptr, nullptr);
		}
		if (operatorKind == Kind::BarBarEqualsToken) {
			checkAssignmentOperator(left, operatorKind, right, leftType, rightType);
		}
		return resultType;
	}
	case Kind::QuestionQuestionToken:
	case Kind::QuestionQuestionEqualsToken: {
		if (operatorKind == Kind::QuestionQuestionToken) {
			checkNullishCoalesceOperands(left, right);
		}
		Type* resultType = leftType;
		if (hasTypeFacts(leftType, TypeFactsEQUndefinedOrNull)) {
			resultType = getUnionTypeEx({GetNonNullableType(leftType), rightType}, UnionReductionSubtype, nullptr, nullptr);
		}
		if (operatorKind == Kind::QuestionQuestionEqualsToken) {
			checkAssignmentOperator(left, operatorKind, right, leftType, rightType);
		}
		return resultType;
	}
	case Kind::EqualsToken:
		checkAssignmentOperator(left, operatorKind, right, leftType, rightType);
		return rightType;
	case Kind::CommaToken:
		if (compilerOptions->AllowUnreachableCode != Tristate::True && isSideEffectFree(left) && !isIndirectCall(left->parent)) {
			SourceFile* sf = getSourceFileOfNode(left);
			int start = skipTrivia(sf->text, left->pos());
			bool isInDiag2657 = false;
			for (Diagnostic* d : sf->diagnostics) {
				if (d->Code() != JSX_expressions_must_have_one_parent_element->code) {
					continue;
				}
				if (d->Loc().contains(start)) {
					isInDiag2657 = true;
					break;
				}
			}
			if (!isInDiag2657) {
				error(left, Left_side_of_comma_operator_is_unused_and_has_no_side_effects);
			}
		}
		return rightType;
	}
	TSC_UNREACHABLE("Unhandled case in checkBinaryLikeExpression");
}

// ---------------------------------------------------------------------------
// checker.go:12754 — checkDestructuringAssignment
// ---------------------------------------------------------------------------

Type* Checker::checkDestructuringAssignment(Node* node, Type* sourceType, CheckMode checkMode, bool rightIsThis) {
	Node* target;
	if (isShorthandPropertyAssignment(node)) {
		Node* initializer = node->as<ShorthandPropertyAssignment>()->ObjectAssignmentInitializer;
		if (initializer != nullptr) {
			// In strict null checking mode, if a default value of a non-undefined type is specified, remove
			// undefined from the final type.
			if (strictNullChecks && !hasTypeFacts(checkExpression(initializer), TypeFactsIsUndefined)) {
				sourceType = getTypeWithFacts(sourceType, TypeFactsNEUndefined);
			}
			checkBinaryLikeExpression(node->name(), node->as<ShorthandPropertyAssignment>()->EqualsToken, initializer, checkMode, nullptr);
		}
		target = node->name();
	} else {
		target = node;
	}
	if (isBinaryExpression(target) && target->as<BinaryExpression>()->OperatorToken->kind == Kind::EqualsToken) {
		checkBinaryExpression(target, checkMode);
		target = target->as<BinaryExpression>()->Left;
		// A default value is specified, so remove undefined from the final type.
		if (strictNullChecks) {
			sourceType = getTypeWithFacts(sourceType, TypeFactsNEUndefined);
		}
	}
	if (isObjectLiteralExpression(target)) {
		return checkObjectLiteralAssignment(target, sourceType, rightIsThis);
	}
	if (isArrayLiteralExpression(target)) {
		return checkArrayLiteralAssignment(target, sourceType, checkMode);
	}
	return checkReferenceAssignment(target, sourceType, checkMode);
}

// ---------------------------------------------------------------------------
// checker.go:12787 — checkObjectLiteralAssignment
// ---------------------------------------------------------------------------

Type* Checker::checkObjectLiteralAssignment(Node* node, Type* sourceType, bool rightIsThis) {
	NodeList* properties = node->propertyList();
	if (strictNullChecks && properties->nodes.empty()) {
		return checkNonNullType(sourceType, node);
	}
	for (size_t i = 0; i < properties->nodes.size(); i++) {
		checkObjectLiteralDestructuringPropertyAssignment(node, sourceType, static_cast<int>(i), properties, rightIsThis);
	}
	return sourceType;
}

// ---------------------------------------------------------------------------
// checker.go:12799 — checkObjectLiteralDestructuringPropertyAssignment
// Note: If property cannot be a SpreadAssignment, then allProperties does not
// need to be provided
// ---------------------------------------------------------------------------

Type* Checker::checkObjectLiteralDestructuringPropertyAssignment(Node* node, Type* objectLiteralType, int propertyIndex, NodeList* allProperties, bool rightIsThis) {
	const std::vector<Node*>& properties = node->properties();
	Node* property = properties[propertyIndex];
	if (isPropertyAssignment(property) || isShorthandPropertyAssignment(property)) {
		Node* name = property->name();

		if (name != nullptr && isPrivateIdentifier(name)) {
			grammarErrorOnNode(name, Private_identifiers_cannot_be_used_in_destructuring_patterns);
		}

		Type* exprType = getLiteralTypeFromPropertyName(name);
		if (isTypeUsableAsPropertyName(exprType)) {
			std::string text = getPropertyNameFromType(exprType);
			Symbol* prop = getPropertyOfType(objectLiteralType, text);
			if (prop != nullptr) {
				markPropertyAsReferenced(prop, property, rightIsThis);
				checkPropertyAccessibility(property, false /*isSuper*/, true /*writing*/, objectLiteralType, prop);
			}
		}
		Type* elementType = getIndexedAccessTypeEx(objectLiteralType, exprType,
			AccessFlagsExpressionPosition | ifElse(hasDefaultValue(property), AccessFlagsAllowMissing, AccessFlagsNone), name, nullptr);
		Type* t = getFlowTypeOfDestructuring(property, elementType);
		Node* expr = property;
		if (isPropertyAssignment(property)) {
			expr = property->initializer();
		}
		return checkDestructuringAssignment(expr, t, CheckModeNormal, false);
	}
	if (isSpreadAssignment(property)) {
		if (propertyIndex < static_cast<int>(properties.size()) - 1) {
			error(property, A_rest_element_must_be_last_in_a_destructuring_pattern);
			return nullptr;
		}
		if (languageVersion < LanguageFeatureMinimumTarget.ObjectSpreadRest) {
			checkExternalEmitHelpers(property, ExternalEmitHelpersRest);
		}
		std::vector<Node*> nonRestNames;
		if (allProperties != nullptr) {
			for (Node* otherProperty : allProperties->nodes) {
				if (!isSpreadAssignment(otherProperty)) {
					nonRestNames.push_back(otherProperty->name());
				}
			}
		}
		Type* t = getRestType(objectLiteralType, nonRestNames, objectLiteralType->symbol);
		checkGrammarForDisallowedTrailingComma(allProperties, A_rest_parameter_or_binding_pattern_may_not_have_a_trailing_comma);
		return checkDestructuringAssignment(property->expression(), t, CheckModeNormal, false);
	}
	error(property, Property_assignment_expected);
	return nullptr;
}

// ---------------------------------------------------------------------------
// checker.go:12850 — checkArrayLiteralAssignment
// ---------------------------------------------------------------------------

Type* Checker::checkArrayLiteralAssignment(Node* node, Type* sourceType, CheckMode checkMode) {
	const std::vector<Node*>& elements = node->elements();
	// This elementType will be used if the specific property corresponding to this index is not
	// present (aka the tuple element property). This call also checks that the parentType is in
	// fact an iterable or array (depending on target language).
	Type* possiblyOutOfBoundsType = orElse(checkIteratedTypeOrElementType(IterationUseDestructuring | IterationUsePossiblyOutOfBounds, sourceType, undefinedType, node), errorType);
	Type* inBoundsType = compilerOptions->NoUncheckedIndexedAccess == Tristate::True ? nullptr : possiblyOutOfBoundsType;
	for (size_t i = 0; i < elements.size(); i++) {
		Type* t = possiblyOutOfBoundsType;
		if (elements[i]->kind == Kind::SpreadElement) {
			if (inBoundsType == nullptr) {
				inBoundsType = orElse(checkIteratedTypeOrElementType(IterationUseDestructuring, sourceType, undefinedType, node), errorType);
			}
			t = inBoundsType;
		}
		checkArrayLiteralDestructuringElementAssignment(node, sourceType, static_cast<int>(i), t, checkMode);
	}
	return sourceType;
}

// ---------------------------------------------------------------------------
// checker.go:12870 — checkArrayLiteralDestructuringElementAssignment
// ---------------------------------------------------------------------------

Type* Checker::checkArrayLiteralDestructuringElementAssignment(Node* node, Type* sourceType, int elementIndex, Type* elementType, CheckMode checkMode) {
	NodeList* elements = node->elementList();
	Node* element = elements->nodes[elementIndex];
	if (!isOmittedExpression(element)) {
		if (!isSpreadElement(element)) {
			Type* indexType = getNumberLiteralType(Number(static_cast<double>(elementIndex)));
			if (isArrayLikeType(sourceType)) {
				// We create a synthetic expression so that getIndexedAccessType doesn't get confused
				// when the element is a SyntaxKind.ElementAccessExpression.
				AccessFlags accessFlags = AccessFlagsExpressionPosition | ifElse(hasDefaultValue(element), AccessFlagsAllowMissing, AccessFlagsNone);
				Type* elementType2 = orElse(getIndexedAccessTypeOrUndefined(sourceType, indexType, accessFlags, createSyntheticExpression(element, indexType, false, nullptr), nullptr), errorType);
				Type* assignedType = elementType2;
				if (hasDefaultValue(element)) {
					assignedType = getTypeWithFacts(elementType2, TypeFactsNEUndefined);
				}
				Type* t = getFlowTypeOfDestructuring(element, assignedType);
				return checkDestructuringAssignment(element, t, checkMode, false);
			}
			return checkDestructuringAssignment(element, elementType, checkMode, false);
		}
		if (elementIndex < static_cast<int>(elements->nodes.size()) - 1) {
			error(element, A_rest_element_must_be_last_in_a_destructuring_pattern);
		} else {
			Node* restExpression = element->expression();
			if (isBinaryExpression(restExpression) && restExpression->as<BinaryExpression>()->OperatorToken->kind == Kind::EqualsToken) {
				error(restExpression->as<BinaryExpression>()->OperatorToken, A_rest_element_cannot_have_an_initializer);
			} else {
				checkGrammarForDisallowedTrailingComma(elements, A_rest_parameter_or_binding_pattern_may_not_have_a_trailing_comma);
				Type* t;
				if (everyType(sourceType, isTupleType)) {
					t = mapType(sourceType, [this, elementIndex](Type* u) { return sliceTupleType(u, elementIndex, 0); });
				} else {
					t = createArrayType(elementType);
				}
				return checkDestructuringAssignment(restExpression, t, checkMode, false);
			}
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// checker.go:12911 — checkReferenceAssignment
// ---------------------------------------------------------------------------

Type* Checker::checkReferenceAssignment(Node* target, Type* sourceType, CheckMode checkMode) {
	Type* targetType = checkExpressionEx(target, checkMode);
	const DiagnosticMessage* message = ifElse(isSpreadAssignment(target->parent),
		The_target_of_an_object_rest_assignment_must_be_a_variable_or_a_property_access,
		The_left_hand_side_of_an_assignment_expression_must_be_a_variable_or_a_property_access);
	const DiagnosticMessage* optionalMessage = ifElse(isSpreadAssignment(target->parent),
		The_target_of_an_object_rest_assignment_may_not_be_an_optional_property_access,
		The_left_hand_side_of_an_assignment_expression_may_not_be_an_optional_property_access);
	if (checkReferenceExpression(target, message, optionalMessage)) {
		checkTypeAssignableToAndOptionallyElaborate(sourceType, targetType, target, target, nullptr, nullptr);
	}
	return sourceType;
}

// ---------------------------------------------------------------------------
// checker.go:12925 — reportOperatorError
// ---------------------------------------------------------------------------

void Checker::reportOperatorError(Type* leftType, Kind operatorKind, Type* rightType, Node* errorNode, const std::function<bool(Type*, Type*)>& isRelated) {
	bool wouldWorkWithAwait = false;
	if (isRelated != nullptr) {
		Type* awaitedLeftType = getAwaitedTypeNoAlias(leftType);
		Type* awaitedRightType = getAwaitedTypeNoAlias(rightType);
		wouldWorkWithAwait = !(awaitedLeftType == leftType && awaitedRightType == rightType) && awaitedLeftType != nullptr && awaitedRightType != nullptr && isRelated(awaitedLeftType, awaitedRightType);
	}
	Type* effectiveLeft = leftType;
	Type* effectiveRight = rightType;
	if (!wouldWorkWithAwait && isRelated != nullptr) {
		auto [l, r] = getBaseTypesIfUnrelated(leftType, rightType, isRelated);
		effectiveLeft = l;
		effectiveRight = r;
	}
	auto [leftStr, rightStr] = getTypeNamesForErrorDisplay(effectiveLeft, effectiveRight);
	switch (operatorKind) {
	case Kind::EqualsEqualsEqualsToken:
	case Kind::EqualsEqualsToken:
	case Kind::ExclamationEqualsEqualsToken:
	case Kind::ExclamationEqualsToken:
		errorAndMaybeSuggestAwait(errorNode, wouldWorkWithAwait, This_comparison_appears_to_be_unintentional_because_the_types_0_and_1_have_no_overlap, {leftStr, rightStr});
		break;
	default:
		errorAndMaybeSuggestAwait(errorNode, wouldWorkWithAwait, Operator_0_cannot_be_applied_to_types_1_and_2, {std::string(tokenToString(operatorKind)), leftStr, rightStr});
		break;
	}
}

// ---------------------------------------------------------------------------
// checker.go:12946 — reportOperatorErrorUnless
// ---------------------------------------------------------------------------

void Checker::reportOperatorErrorUnless(Type* leftType, Kind operatorKind, Type* rightType, Node* errorNode, const std::function<bool(Type*, Type*)>& typesAreCompatible) {
	if (!typesAreCompatible(leftType, rightType)) {
		reportOperatorError(leftType, operatorKind, rightType, errorNode, typesAreCompatible);
	}
}

// ---------------------------------------------------------------------------
// checker.go:12952 — getBaseTypesIfUnrelated
// ---------------------------------------------------------------------------

std::pair<Type*, Type*> Checker::getBaseTypesIfUnrelated(Type* leftType, Type* rightType, const std::function<bool(Type*, Type*)>& isRelated) {
	Type* effectiveLeft = leftType;
	Type* effectiveRight = rightType;
	Type* leftBase = getBaseTypeOfLiteralType(leftType);
	Type* rightBase = getBaseTypeOfLiteralType(rightType);
	if (!isRelated(leftBase, rightBase)) {
		effectiveLeft = leftBase;
		effectiveRight = rightBase;
	}
	return {effectiveLeft, effectiveRight};
}

// ---------------------------------------------------------------------------
// checker.go:12964 — checkAssignmentOperator
// ---------------------------------------------------------------------------

void Checker::checkAssignmentOperator(Node* left, Kind operatorKind, Node* right, Type* leftType, Type* rightType) {
	if (isAssignmentOperator(operatorKind)) {
		// We ignore assignments of undefined to CommonJS exports when there are multiple assignment declarations
		if (isDeclarationNode(left->parent) && getAssignmentDeclarationKind(left->parent) == JSDeclarationKind::ExportsProperty) {
			SymbolNodeLinks* leftLinks = symbolNodeLinks.Get(left);
			if (Symbol* symbol = staleForCheckFile(leftLinks->resolvedSymbolCheckFile)
									 ? nullptr : leftLinks->resolvedSymbol;
				symbol != nullptr && symbol->declarations.size() > 1 && (rightType->flags & TypeFlagsUndefined) != 0) {
				return;
			}
		}
		// getters can be a subtype of setters, so to check for assignability we use the setter's type instead
		if (isCompoundAssignment(operatorKind) && isPropertyAccessExpression(left)) {
			leftType = checkPropertyAccessExpression(left, CheckModeNormal, true /*writeOnly*/);
		}
		if (checkReferenceExpression(left, The_left_hand_side_of_an_assignment_expression_must_be_a_variable_or_a_property_access, The_left_hand_side_of_an_assignment_expression_may_not_be_an_optional_property_access)) {
			const DiagnosticMessage* headMessage = nullptr;
			if (exactOptionalPropertyTypes && isPropertyAccessExpression(left) && maybeTypeOfKind(rightType, TypeFlagsUndefined)) {
				Type* target = getTypeOfPropertyOfType(getTypeOfExpression(left->expression()), left->name()->text());
				if (isExactOptionalPropertyMismatch(rightType, target)) {
					headMessage = Type_0_is_not_assignable_to_type_1_with_exactOptionalPropertyTypes_Colon_true_Consider_adding_undefined_to_the_type_of_the_target;
				}
			}
			// to avoid cascading errors check assignability only if 'isReference' check succeeded and no errors were reported
			checkTypeAssignableToAndOptionallyElaborate(rightType, leftType, left, right, headMessage, nullptr);
		}
	}
}

// ---------------------------------------------------------------------------
// checker.go:12990 — bothAreBigIntLike
// ---------------------------------------------------------------------------

bool Checker::bothAreBigIntLike(Type* left, Type* right) {
	return isTypeAssignableToKind(left, TypeFlagsBigIntLike) && isTypeAssignableToKind(right, TypeFlagsBigIntLike);
}

// ---------------------------------------------------------------------------
// checker.go:12994 — getSuggestedBooleanOperator
// ---------------------------------------------------------------------------

Kind Checker::getSuggestedBooleanOperator(Kind operatorKind) {
	switch (operatorKind) {
	case Kind::BarToken:
	case Kind::BarEqualsToken:
		return Kind::BarBarToken;
	case Kind::CaretToken:
	case Kind::CaretEqualsToken:
		return Kind::ExclamationEqualsEqualsToken;
	case Kind::AmpersandToken:
	case Kind::AmpersandEqualsToken:
		return Kind::AmpersandAmpersandToken;
	}
	return Kind::Unknown;
}

// ---------------------------------------------------------------------------
// checker.go:13006 — checkArithmeticOperandType
// ---------------------------------------------------------------------------

bool Checker::checkArithmeticOperandType(Node* operand, Type* t, const DiagnosticMessage* diagnostic, bool isAwaitValid) {
	if (!isTypeAssignableTo(t, numberOrBigIntType)) {
		Type* awaitedType = nullptr;
		if (isAwaitValid) {
			awaitedType = getAwaitedTypeOfPromise(t);
		}
		errorAndMaybeSuggestAwait(operand, awaitedType != nullptr && isTypeAssignableTo(awaitedType, numberOrBigIntType), diagnostic);
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// checker.go:13019 — checkForDisallowedESSymbolOperand
// Return true if there was no error, false if there was an error.
// ---------------------------------------------------------------------------

bool Checker::checkForDisallowedESSymbolOperand(Node* left, Node* right, Type* leftType, Type* rightType, Kind operatorKind) {
	Node* offendingSymbolOperand = nullptr;
	if (maybeTypeOfKindConsideringBaseConstraint(leftType, TypeFlagsESSymbolLike)) {
		offendingSymbolOperand = left;
	} else if (maybeTypeOfKindConsideringBaseConstraint(rightType, TypeFlagsESSymbolLike)) {
		offendingSymbolOperand = right;
	}
	if (offendingSymbolOperand != nullptr) {
		error(offendingSymbolOperand, The_0_operator_cannot_be_applied_to_type_symbol, {std::string(tokenToString(operatorKind))});
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// checker.go:13034 — checkNaNEquality
// ---------------------------------------------------------------------------

void Checker::checkNaNEquality(Node* errorNode, Kind operatorKind, Node* left, Node* right) {
	bool isLeftNaN = isGlobalNaN(skipParentheses(left));
	bool isRightNaN = isGlobalNaN(skipParentheses(right));
	if (isLeftNaN || isRightNaN) {
		Diagnostic* err = error(errorNode, This_condition_will_always_return_0,
			{std::string(tokenToString(operatorKind == Kind::EqualsEqualsEqualsToken || operatorKind == Kind::EqualsEqualsToken ? Kind::FalseKeyword : Kind::TrueKeyword))});
		if (isLeftNaN && isRightNaN) {
			return;
		}
		std::string operatorString;
		if (operatorKind == Kind::ExclamationEqualsEqualsToken || operatorKind == Kind::ExclamationEqualsToken) {
			operatorString = tokenToString(Kind::ExclamationToken);
		}
		Node* location = left;
		if (isLeftNaN) {
			location = right;
		}
		Node* expression = skipParentheses(location);
		std::string entityName = "...";
		if (isEntityNameExpression(expression)) {
			entityName = entityNameToString(expression);
		}
		std::string suggestion = operatorString + "Number.isNaN(" + entityName + ")";
		err->AddRelatedInfo(createDiagnosticForNode(location, Did_you_mean_0, {suggestion}));
	}
}

// ---------------------------------------------------------------------------
// checker.go:13060 — isGlobalNaN
// ---------------------------------------------------------------------------

bool Checker::isGlobalNaN(Node* expr) {
	if (isIdentifier(expr) && expr->text() == "NaN") {
		Symbol* globalNaNSymbol = getGlobalNaNSymbolOrNil();
		return globalNaNSymbol != nullptr && globalNaNSymbol == getResolvedSymbol(expr);
	}
	return false;
}

// ---------------------------------------------------------------------------
// checker.go:13068 — isTypeEqualityComparableTo
// ---------------------------------------------------------------------------

bool Checker::isTypeEqualityComparableTo(Type* source, Type* target) {
	return (target->flags & TypeFlagsNullable) != 0 || isTypeComparableTo(source, target);
}

// ---------------------------------------------------------------------------
// checker.go:13072 — checkTruthinessOfType
// ---------------------------------------------------------------------------

Type* Checker::checkTruthinessOfType(Type* t, Node* node) {
	if ((t->flags & TypeFlagsVoid) != 0) {
		error(node, An_expression_of_type_void_cannot_be_tested_for_truthiness);
		return t;
	}
	PredicateSemantics semantics = getSyntacticTruthySemantics(node);
	if (semantics != PredicateSemanticsSometimes) {
		error(node, ifElse(semantics == PredicateSemanticsAlways, This_kind_of_expression_is_always_truthy, This_kind_of_expression_is_always_falsy));
	}
	return t;
}

// ---------------------------------------------------------------------------
// checker.go:13093 — getSyntacticTruthySemantics
// ---------------------------------------------------------------------------

PredicateSemantics Checker::getSyntacticTruthySemantics(Node* node) {
	node = skipOuterExpressions(node, OEKAll);
	switch (node->kind) {
	case Kind::NumericLiteral:
		// Allow `while(0)` or `while(1)`
		if (node->text() == "0" || node->text() == "1") {
			return PredicateSemanticsSometimes;
		}
		return PredicateSemanticsAlways;
	case Kind::ArrayLiteralExpression:
	case Kind::ArrowFunction:
	case Kind::BigIntLiteral:
	case Kind::ClassExpression:
	case Kind::FunctionExpression:
	case Kind::JsxElement:
	case Kind::JsxSelfClosingElement:
	case Kind::ObjectLiteralExpression:
	case Kind::RegularExpressionLiteral:
		return PredicateSemanticsAlways;
	case Kind::VoidExpression:
	case Kind::NullKeyword:
		return PredicateSemanticsNever;
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::StringLiteral:
		if (!node->text().empty()) {
			return PredicateSemanticsAlways;
		}
		return PredicateSemanticsNever;
	case Kind::ConditionalExpression:
		return getSyntacticTruthySemantics(node->as<ConditionalExpression>()->WhenTrue) |
			getSyntacticTruthySemantics(node->as<ConditionalExpression>()->WhenFalse);
	case Kind::Identifier:
		if (getResolvedSymbol(node) == undefinedSymbol) {
			return PredicateSemanticsNever;
		}
		break;
	}
	return PredicateSemanticsSometimes;
}

// ---------------------------------------------------------------------------
// checker.go:13122 — checkNullishCoalesceOperands
// ---------------------------------------------------------------------------

void Checker::checkNullishCoalesceOperands(Node* left, Node* right) {
	if (isBinaryExpression(left->parent->parent)) {
		Node* grandparentLeft = left->parent->parent->as<BinaryExpression>()->Left;
		Node* grandparentOperatorToken = left->parent->parent->as<BinaryExpression>()->OperatorToken;
		if (isBinaryExpression(grandparentLeft) && grandparentOperatorToken->kind == Kind::BarBarToken) {
			grammarErrorOnNode(grandparentLeft, X_0_and_1_operations_cannot_be_mixed_without_parentheses,
				{std::string(tokenToString(Kind::QuestionQuestionToken)), std::string(tokenToString(grandparentOperatorToken->kind))});
		}
	} else if (isBinaryExpression(left)) {
		Node* operatorToken = left->as<BinaryExpression>()->OperatorToken;
		if (operatorToken->kind == Kind::BarBarToken || operatorToken->kind == Kind::AmpersandAmpersandToken) {
			grammarErrorOnNode(left, X_0_and_1_operations_cannot_be_mixed_without_parentheses,
				{std::string(tokenToString(operatorToken->kind)), std::string(tokenToString(Kind::QuestionQuestionToken))});
		}
	} else if (isBinaryExpression(right)) {
		Node* operatorToken = right->as<BinaryExpression>()->OperatorToken;
		if (operatorToken->kind == Kind::AmpersandAmpersandToken) {
			grammarErrorOnNode(right, X_0_and_1_operations_cannot_be_mixed_without_parentheses,
				{std::string(tokenToString(Kind::QuestionQuestionToken)), std::string(tokenToString(operatorToken->kind))});
		}
	}
	checkNullishCoalesceOperandLeft(left);
}

// ---------------------------------------------------------------------------
// checker.go:13143 — checkNullishCoalesceOperandLeft
// ---------------------------------------------------------------------------

void Checker::checkNullishCoalesceOperandLeft(Node* left) {
	Node* leftTarget = skipOuterExpressions(left, OEKAll);
	PredicateSemantics nullishSemantics = getSyntacticNullishnessSemantics(leftTarget);
	if (nullishSemantics != PredicateSemanticsSometimes) {
		if (nullishSemantics == PredicateSemanticsAlways) {
			error(leftTarget, This_expression_is_always_nullish);
		} else {
			error(leftTarget, Right_operand_of_is_unreachable_because_the_left_operand_is_never_nullish);
		}
	}
}

// ---------------------------------------------------------------------------
// checker.go:13155 — getSyntacticNullishnessSemantics
// ---------------------------------------------------------------------------

PredicateSemantics Checker::getSyntacticNullishnessSemantics(Node* node) {
	node = skipOuterExpressions(node, OEKAll);
	switch (node->kind) {
	case Kind::AwaitExpression:
	case Kind::CallExpression:
	case Kind::TaggedTemplateExpression:
	case Kind::ElementAccessExpression:
	case Kind::MetaProperty:
	case Kind::NewExpression:
	case Kind::PropertyAccessExpression:
	case Kind::YieldExpression:
	case Kind::ThisKeyword:
		return PredicateSemanticsSometimes;
	case Kind::BinaryExpression:
		// List of operators that can produce null/undefined:
		// || ||= && &&= ?? ??=
		switch (node->as<BinaryExpression>()->OperatorToken->kind) {
		case Kind::BarBarToken:
		case Kind::BarBarEqualsToken:
		case Kind::AmpersandAmpersandToken:
		case Kind::AmpersandAmpersandEqualsToken:
			return PredicateSemanticsSometimes;
		// For these operator kinds, the right operand is effectively controlling
		case Kind::CommaToken:
		case Kind::EqualsToken:
			return getSyntacticNullishnessSemantics(node->as<BinaryExpression>()->Right);
		// For nullish coalescing: result is the left operand when left is non-null,
		// or the right operand when left is null/undefined. The nullishness of the
		// result combines both paths: the left's non-null path contributes Never,
		// and when left can be null, the right's semantics are also included.
		case Kind::QuestionQuestionToken:
		case Kind::QuestionQuestionEqualsToken: {
			PredicateSemantics leftSemantics = getSyntacticNullishnessSemantics(node->as<BinaryExpression>()->Left);
			// The non-null path (left is non-null): left branch taken, result has Never bit
			PredicateSemantics result = leftSemantics & PredicateSemanticsNever;
			// The null path (left is null/undefined): right branch taken, result inherits right's semantics
			if ((leftSemantics & PredicateSemanticsAlways) != 0) {
				result |= getSyntacticNullishnessSemantics(node->as<BinaryExpression>()->Right);
			}
			return result;
		}
		}
		return PredicateSemanticsNever;
	case Kind::ConditionalExpression:
		return getSyntacticNullishnessSemantics(node->as<ConditionalExpression>()->WhenTrue) |
			getSyntacticNullishnessSemantics(node->as<ConditionalExpression>()->WhenFalse);
	case Kind::NullKeyword:
		return PredicateSemanticsAlways;
	case Kind::Identifier:
		if (getResolvedSymbol(node) == undefinedSymbol) {
			return PredicateSemanticsAlways;
		}
		return PredicateSemanticsSometimes;
	}
	return PredicateSemanticsNever;
}

// ---------------------------------------------------------------------------
// checker.go:13218 — isSideEffectFree
// ---------------------------------------------------------------------------

// This is a *shallow* check: An expression is side-effect-free if the
// evaluation of the expression *itself* cannot produce side effects.
// For example, x++ / 3 is side-effect free because the / operator
// does not have side effects.
// The intent is to "smell test" an expression for correctness in positions where
// its value is discarded (e.g. the left side of the comma operator).
bool Checker::isSideEffectFree(Node* node) {
	node = skipParentheses(node);
	switch (node->kind) {
	case Kind::Identifier:
	case Kind::StringLiteral:
	case Kind::RegularExpressionLiteral:
	case Kind::TaggedTemplateExpression:
	case Kind::TemplateExpression:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::NullKeyword:
	case Kind::UndefinedKeyword:
	case Kind::FunctionExpression:
	case Kind::ClassExpression:
	case Kind::ArrowFunction:
	case Kind::ArrayLiteralExpression:
	case Kind::ObjectLiteralExpression:
	case Kind::TypeOfExpression:
	case Kind::NonNullExpression:
	case Kind::JsxSelfClosingElement:
	case Kind::JsxElement:
		return true;
	case Kind::ConditionalExpression:
		return isSideEffectFree(node->as<ConditionalExpression>()->WhenTrue) && isSideEffectFree(node->as<ConditionalExpression>()->WhenFalse);
	case Kind::BinaryExpression:
		if (isAssignmentOperator(node->as<BinaryExpression>()->OperatorToken->kind)) {
			return false;
		}
		return isSideEffectFree(node->as<BinaryExpression>()->Left) && isSideEffectFree(node->as<BinaryExpression>()->Right);
	case Kind::PrefixUnaryExpression:
		// Unary operators ~, !, +, and - have no side effects.
		// The rest do.
		switch (node->as<PrefixUnaryExpression>()->Operator) {
		case Kind::ExclamationToken:
		case Kind::PlusToken:
		case Kind::MinusToken:
		case Kind::TildeToken:
			return true;
		}
		break;
	}
	return false;
}

// ---------------------------------------------------------------------------
// checker.go:13246 — isIndirectCall
// Return true for "indirect calls", (i.e. `(0, x.f)(...)` or `(0, eval)(...)`),
// which prevents passing `this`.
// ---------------------------------------------------------------------------

bool Checker::isIndirectCall(Node* node) {
	Node* left = node->as<BinaryExpression>()->Left;
	Node* right = node->as<BinaryExpression>()->Right;
	return isParenthesizedExpression(node->parent) && isNumericLiteral(left) && left->text() == "0" &&
		((isCallExpression(node->parent->parent) && node->parent->parent->expression() == node->parent) ||
		 isTaggedTemplateExpression(node->parent->parent)) &&
		(isAccessExpression(right) || (isIdentifier(right) && right->text() == "eval"));
}

// ---------------------------------------------------------------------------
// checker.go:13254 — checkInstanceOfExpression
// ---------------------------------------------------------------------------

Type* Checker::checkInstanceOfExpression(Node* left, Node* right, Type* leftType, Type* rightType, CheckMode checkMode) {
	if (leftType == silentNeverType || rightType == silentNeverType) {
		return silentNeverType;
	}
	// TypeScript 1.0 spec (April 2014): 4.15.4
	// The instanceof operator requires the left operand to be of type Any, an object type, or a type parameter type,
	// and the right operand to be of type Any, a subtype of the 'Function' interface type, or have a call or construct signature.
	// The result is always of the Boolean primitive type.
	// NOTE: do not raise error if leftType is unknown as related error was already reported
	if (!isTypeAny(leftType) && allTypesAssignableToKind(leftType, TypeFlagsPrimitive)) {
		error(left, The_left_hand_side_of_an_instanceof_expression_must_be_of_type_any_an_object_type_or_a_type_parameter);
	}
	Signature* signature = getResolvedSignature(left->parent, nullptr /*candidatesOutArray*/, checkMode);
	if (signature == resolvingSignature) {
		// CheckMode.SkipGenericFunctions is enabled and this is a call to a generic function that
		// returns a function type. We defer checking and return silentNeverType.
		return silentNeverType;
	}
	// If rightType has a `[Symbol.hasInstance]` method that is not `(value: unknown) => boolean`, we
	// must check the expression as if it were a call to `right[Symbol.hasInstance](left)`. The call to
	// `getResolvedSignature`, below, will check that leftType is assignable to the type of the first
	// parameter.
	Type* returnType = getReturnTypeOfSignature(signature);
	// We also verify that the return type of the `[Symbol.hasInstance]` method is assignable to
	// `boolean`. According to the spec, the runtime will actually perform `ToBoolean` on the result,
	// but this is more type-safe.
	checkTypeAssignableTo(returnType, booleanType, right, An_object_s_Symbol_hasInstance_method_must_return_a_boolean_value_for_it_to_be_used_on_the_right_hand_side_of_an_instanceof_expression);
	return booleanType;
}

// ---------------------------------------------------------------------------
// checker.go:13284 — checkInExpression
// ---------------------------------------------------------------------------

Type* Checker::checkInExpression(Node* left, Node* right, Type* leftType, Type* rightType) {
	if (leftType == silentNeverType || rightType == silentNeverType) {
		return silentNeverType;
	}
	if (isPrivateIdentifier(left)) {
		if (languageVersion < LanguageFeatureMinimumTarget.PrivateNamesAndClassStaticBlocks ||
			languageVersion < LanguageFeatureMinimumTarget.ClassAndClassElementDecorators ||
			!compilerOptions->GetUseDefineForClassFields()) {
			checkExternalEmitHelpers(left, ExternalEmitHelpersClassPrivateFieldIn);
		}
		// Unlike in 'checkPrivateIdentifierExpression' we now have access to the RHS type
		// which provides us with the opportunity to emit more detailed errors
		if ((symbolNodeLinks.Get(left)->resolvedSymbol == nullptr ||
		 staleForCheckFile(symbolNodeLinks.Get(left)->resolvedSymbolCheckFile)) &&
		getContainingClass(left) != nullptr) {
			bool isUncheckedJS = isUncheckedJSSuggestion(left, rightType->symbol, true /*excludeClasses*/);
			reportNonexistentProperty(left, rightType, isUncheckedJS);
		}
	} else {
		// The type of the left operand must be assignable to string, number, or symbol.
		checkTypeAssignableTo(checkNonNullType(leftType, left), stringNumberSymbolType, left, nullptr);
	}
	// The type of the right operand must be assignable to 'object'.
	if (checkTypeAssignableTo(checkNonNullType(rightType, right), nonPrimitiveType, right, nullptr)) {
		// The {} type is assignable to the object type, yet {} might represent a primitive type. Here we
		// detect and error on {} that results from narrowing the unknown type, as well as intersections
		// that include {} (we know that the other types in such intersections are assignable to object
		// since we already checked for that).
		if (hasEmptyObjectIntersection(rightType)) {
			error(right, Type_0_may_represent_a_primitive_value_which_is_not_permitted_as_the_right_operand_of_the_in_operator, {TypeToString(rightType)});
		}
	}
	// The result is always of the Boolean primitive type.
	return booleanType;
}

// ---------------------------------------------------------------------------
// checker.go:13318 — hasEmptyObjectIntersection
// ---------------------------------------------------------------------------

bool Checker::hasEmptyObjectIntersection(Type* t) {
	return someType(t, [this](Type* u) {
		return u == unknownEmptyObjectType ||
			((u->flags & TypeFlagsIntersection) != 0 && IsEmptyAnonymousObjectType(getBaseConstraintOrType(u)));
	});
}

// ---------------------------------------------------------------------------
// checker.go:13324 — getExactOptionalUnassignableProperties
// ---------------------------------------------------------------------------

std::vector<Symbol*> Checker::getExactOptionalUnassignableProperties(Type* source, Type* target) {
	if (isTupleType(source) && isTupleType(target)) {
		return {};
	}
	return filterVec(getPropertiesOfType(target), [this, source](Symbol* targetProp) {
		return isExactOptionalPropertyMismatch(getTypeOfPropertyOfType(source, targetProp->name), getTypeOfSymbol(targetProp));
	});
}

// ---------------------------------------------------------------------------
// checker.go:13333 — isExactOptionalPropertyMismatch
// ---------------------------------------------------------------------------

bool Checker::isExactOptionalPropertyMismatch(Type* source, Type* target) {
	return source != nullptr && target != nullptr && maybeTypeOfKind(source, TypeFlagsUndefined) && containsMissingType(target);
}

// ---------------------------------------------------------------------------
// checker.go:13337 — checkReferenceExpression
// ---------------------------------------------------------------------------

bool Checker::checkReferenceExpression(Node* expr, const DiagnosticMessage* invalidReferenceMessage, const DiagnosticMessage* invalidOptionalChainMessage) {
	// References are combinations of identifiers, parentheses, and property accesses.
	Node* node = skipOuterExpressions(expr, OEKAssertions | OEKParentheses);
	if (node->kind != Kind::Identifier && !isAccessExpression(node)) {
		error(expr, invalidReferenceMessage);
		return false;
	}
	if ((node->flags & NodeFlagsOptionalChain) != 0) {
		error(expr, invalidOptionalChainMessage);
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// checker.go:13351 — checkObjectLiteral
// ---------------------------------------------------------------------------

Type* Checker::checkObjectLiteral(Node* node, CheckMode checkMode) {
	// Expando object literals have empty properties but filled exports
	if (node->properties().empty() && node->symbol() != nullptr && !node->symbol()->exports.empty()) {
		Type* result = newAnonymousType(node->symbol(), node->symbol()->exports, {}, {}, {});
		if (isInJSFile(node) && !isInJsonFile(node)) {
			result->objectFlags |= ObjectFlagsJSLiteral;
		}
		// An expando object literal has no property children (len == 0), so there
		// is nothing to check here.
		return result;
	}
	checkNodeDeferred(node);
	bool inDestructuringPattern = isAssignmentTarget(node);
	// Grammar checking
	checkGrammarObjectLiteralExpression(node->as<ObjectLiteralExpression>(), inDestructuringPattern);
	SymbolTable allPropertiesTable;
	if (strictNullChecks) {
		allPropertiesTable = SymbolTable{};
	}
	SymbolTable propertiesTable;
	std::vector<Symbol*> propertiesArray;
	Type* spread = emptyObjectType;
	pushCachedContextualType(node);
	Type* contextualType = getApparentTypeOfContextualType(node, ContextFlagsNone);
	bool contextualTypeHasPattern = false;
	if (contextualType != nullptr) {
		auto it = patternForType.find(contextualType);
		Node* pattern = it != patternForType.end() ? it->second : nullptr;
		if (pattern != nullptr && (isObjectBindingPattern(pattern) || isObjectLiteralExpression(pattern))) {
			contextualTypeHasPattern = true;
		}
	}
	bool inConstContext = isConstContext(node);
	CheckFlags checkFlags = CheckFlagsNone;
	if (inConstContext) {
		checkFlags = CheckFlagsReadonly;
	}
	ObjectFlags objectFlags = ObjectFlagsFreshLiteral;
	bool patternWithComputedProperties = false;
	bool hasComputedStringProperty = false;
	bool hasComputedNumberProperty = false;
	bool hasComputedSymbolProperty = false;
	// Spreads may cause an early bail; ensure computed names are always checked (this is cached)
	// As otherwise they may not be checked until exports for the type at this position are retrieved,
	// which may never occur.
	for (Node* elem : node->properties()) {
		if (elem->name() != nullptr && isComputedPropertyName(elem->name())) {
			checkComputedPropertyName(elem->name());
		}
	}
	int offset = 0;
	auto createObjectLiteralType = [this, node, contextualType, inDestructuringPattern, &propertiesTable, &propertiesArray, &offset, &objectFlags, &patternWithComputedProperties, &hasComputedStringProperty, &hasComputedNumberProperty, &hasComputedSymbolProperty]() -> Type* {
		std::vector<IndexInfo*> indexInfos;
		bool isReadonly = isConstContext(node);
		if (hasComputedStringProperty) {
			indexInfos.push_back(getObjectLiteralIndexInfo(isReadonly, std::vector<Symbol*>(propertiesArray.begin() + offset, propertiesArray.end()), stringType));
		}
		if (hasComputedNumberProperty) {
			indexInfos.push_back(getObjectLiteralIndexInfo(isReadonly, std::vector<Symbol*>(propertiesArray.begin() + offset, propertiesArray.end()), numberType));
		}
		if (hasComputedSymbolProperty) {
			indexInfos.push_back(getObjectLiteralIndexInfo(isReadonly, std::vector<Symbol*>(propertiesArray.begin() + offset, propertiesArray.end()), esSymbolType));
		}
		Type* result = newAnonymousType(node->symbol(), propertiesTable, {}, {}, indexInfos);
		result->objectFlags |= objectFlags | ObjectFlagsObjectLiteral | ObjectFlagsContainsObjectOrArrayLiteral;
		if (contextualType == nullptr && isInJSFile(node) && !isInJsonFile(node)) {
			result->objectFlags |= ObjectFlagsJSLiteral;
		}
		if (patternWithComputedProperties) {
			result->objectFlags |= ObjectFlagsObjectLiteralPatternWithComputedProperties;
		}
		if (inDestructuringPattern) {
			patternForType[result] = node;
		}
		return result;
	};
	for (Node* memberDecl : node->properties()) {
		Symbol* member = getSymbolOfDeclaration(memberDecl);
		Type* computedNameType = nullptr;
		if (memberDecl->name() != nullptr && memberDecl->name()->kind == Kind::ComputedPropertyName) {
			computedNameType = checkComputedPropertyName(memberDecl->name());
		}
		if (isPropertyAssignment(memberDecl) || isShorthandPropertyAssignment(memberDecl) || isObjectLiteralMethod(memberDecl)) {
			Type* t;
			switch (memberDecl->kind) {
			case Kind::PropertyAssignment:
				t = checkPropertyAssignment(memberDecl, checkMode);
				break;
			case Kind::ShorthandPropertyAssignment:
				t = checkShorthandPropertyAssignment(memberDecl, inDestructuringPattern, checkMode);
				break;
			default:
				t = checkObjectLiteralMethod(memberDecl, checkMode);
				break;
			}
			objectFlags |= t->objectFlags & ObjectFlagsPropagatingFlags;
			Type* nameType = nullptr;
			if (computedNameType != nullptr && isTypeUsableAsPropertyName(computedNameType)) {
				nameType = computedNameType;
			}
			Symbol* prop;
			if (nameType != nullptr) {
				prop = newSymbolEx(SymbolFlagsProperty | member->flags, getPropertyNameFromType(nameType), checkFlags | CheckFlagsLate);
			} else {
				prop = newSymbolEx(SymbolFlagsProperty | member->flags, member->name, checkFlags);
			}
			ValueSymbolLinks* links = valueSymbolLinks.Get(prop);
			if (nameType != nullptr) {
				links->nameType = nameType;
			}
			if (inDestructuringPattern && hasDefaultValue(memberDecl)) {
				// If object literal is an assignment pattern and if the assignment pattern specifies a default value
				// for the property, make the property optional.
				prop->flags |= SymbolFlagsOptional;
			} else if (contextualTypeHasPattern && (contextualType->objectFlags & ObjectFlagsObjectLiteralPatternWithComputedProperties) == 0) {
				// If object literal is contextually typed by the implied type of a binding pattern, and if the
				// binding pattern specifies a default value for the property, make the property optional.
				Symbol* impliedProp = getPropertyOfType(contextualType, member->name);
				if (impliedProp != nullptr) {
					prop->flags |= impliedProp->flags & SymbolFlagsOptional;
				} else if (getIndexInfoOfType(contextualType, stringType) == nullptr) {
					error(memberDecl->name(), Object_literal_may_only_specify_known_properties_and_0_does_not_exist_in_type_1, {symbolToString(member), TypeToString(contextualType)});
				}
			}
			prop->declarations = member->declarations;
			prop->parent = member->parent;
			prop->valueDeclaration = member->valueDeclaration;
			links->resolvedType = t;
			links->target = member;
			member = prop;
			if (strictNullChecks) {
				allPropertiesTable[prop->name] = prop;
			}
			if (contextualType != nullptr && (checkMode & CheckModeInferential) != 0 && (checkMode & CheckModeSkipContextSensitive) == 0 &&
				(isPropertyAssignment(memberDecl) || isMethodDeclaration(memberDecl)) && isContextSensitive(memberDecl)) {
				InferenceContext* inferenceContext = getInferenceContext(node);
				// In CheckMode.Inferential we should always have an inference context
				Node* inferenceNode = memberDecl;
				if (isPropertyAssignment(memberDecl)) {
					inferenceNode = memberDecl->initializer();
				}
				addIntraExpressionInferenceSite(inferenceContext, inferenceNode, t);
			}
		} else if (memberDecl->kind == Kind::SpreadAssignment) {
			if (!propertiesArray.empty()) {
				spread = getSpreadType(spread, createObjectLiteralType(), node->symbol(), objectFlags, inConstContext);
				propertiesArray.clear();
				propertiesTable.clear();
				hasComputedStringProperty = false;
				hasComputedNumberProperty = false;
				hasComputedSymbolProperty = false;
			}
			Type* t = getReducedType(checkExpressionEx(memberDecl->expression(), checkMode & CheckModeInferential));
			if (isValidSpreadType(t)) {
				Type* mergedType = tryMergeUnionOfObjectTypeAndEmptyObject(t, inConstContext);
				if (strictNullChecks) {
					checkSpreadPropOverrides(mergedType, allPropertiesTable, memberDecl);
				}
				offset = static_cast<int>(propertiesArray.size());
				if (isErrorType(spread)) {
					continue;
				}
				spread = getSpreadType(spread, mergedType, node->symbol(), objectFlags, inConstContext);
			} else {
				error(memberDecl, Spread_types_may_only_be_created_from_object_types);
				spread = errorType;
			}
			continue;
		} else {
			// TypeScript 1.0 spec (April 2014)
			// A get accessor declaration is processed in the same manner as
			// an ordinary function declaration(section 6.1) with no parameters.
			// A set accessor declaration is processed in the same manner
			// as an ordinary function declaration with a single parameter and a Void return type.
			TSC_ASSERT(memberDecl->kind == Kind::GetAccessor || memberDecl->kind == Kind::SetAccessor, "expected get/set accessor");
			checkNodeDeferred(memberDecl);
		}
		if (computedNameType != nullptr && (computedNameType->flags & TypeFlagsStringOrNumberLiteralOrUnique) == 0) {
			if (isTypeAssignableTo(computedNameType, stringNumberSymbolType)) {
				if (isTypeAssignableTo(computedNameType, numberType)) {
					hasComputedNumberProperty = true;
				} else if (isTypeAssignableTo(computedNameType, esSymbolType)) {
					hasComputedSymbolProperty = true;
				} else {
					hasComputedStringProperty = true;
				}
				if (inDestructuringPattern) {
					patternWithComputedProperties = true;
				}
			}
		} else {
			propertiesTable[member->name] = member;
		}
		propertiesArray.push_back(member);
	}
	popContextualType();
	if (isErrorType(spread)) {
		return errorType;
	}
	if (spread != emptyObjectType) {
		if (!propertiesArray.empty()) {
			spread = getSpreadType(spread, createObjectLiteralType(), node->symbol(), objectFlags, inConstContext);
			propertiesArray.clear();
			propertiesTable.clear();
			hasComputedStringProperty = false;
			hasComputedNumberProperty = false;
		}
		// remap the raw emptyObjectType fed in at the top into a fresh empty object literal type, unique to this use site
		return mapType(spread, [this, &createObjectLiteralType](Type* t) -> Type* {
			if (t == emptyObjectType) {
				return createObjectLiteralType();
			}
			return t;
		});
	}
	return createObjectLiteralType();
}

// ---------------------------------------------------------------------------
// checker.go:13563 — checkContextualDeprecations
// ---------------------------------------------------------------------------

void Checker::checkContextualDeprecations(Node* node) {
	Type* contextualType = getApparentTypeOfContextualType(node, ContextFlagsNone);
	for (Node* property : node->properties()) {
		if (isCanceled()) {
			return;
		}
		if (property->name() != nullptr && !isComputedPropertyName(property->name())) {
			checkDeprecatedProperty(property->name(), contextualType);
		}
	}
}

// ---------------------------------------------------------------------------
// checker.go:13575 — checkDeprecatedProperty
// ---------------------------------------------------------------------------

void Checker::checkDeprecatedProperty(Node* name, Type* contextualType) {
	if (contextualType == nullptr) {
		return;
	}
	Symbol* prop = getPropertyOfType(contextualType, name->text());
	if (prop == nullptr || prop->declarations.empty()) {
		return;
	}
	if (isDeprecatedSymbol(prop)) {
		addDeprecatedSuggestion(name, prop->declarations, name->text());
	}
}

// ---------------------------------------------------------------------------
// checker.go:13588 — checkSpreadPropOverrides
// ---------------------------------------------------------------------------

void Checker::checkSpreadPropOverrides(Type* t, const SymbolTable& props, Node* spread) {
	for (Symbol* right : getPropertiesOfType(t)) {
		if ((right->flags & SymbolFlagsOptional) == 0 && (right->checkFlags & CheckFlagsPartial) == 0) {
			auto it = props.find(right->name);
			Symbol* left = it != props.end() ? it->second : nullptr;
			if (left != nullptr) {
				Diagnostic* diagnostic = error(left->valueDeclaration, X_0_is_specified_more_than_once_so_this_usage_will_be_overwritten, {left->name});
				diagnostic->AddRelatedInfo(NewDiagnosticForNode(spread, This_spread_always_overwrites_this_property, {}));
			}
		}
	}
}

// ---------------------------------------------------------------------------
// checker.go:13604 — getSpreadType
// ---------------------------------------------------------------------------

// Since the source of spread types are object literals, which are not binary,
// this function should be called in a left folding style, with left = previous result of getSpreadType
// and right = the new element to be spread.
Type* Checker::getSpreadType(Type* left, Type* right, Symbol* symbol, ObjectFlags objectFlags, bool readonly) {
	if ((left->flags & TypeFlagsAny) != 0 || (right->flags & TypeFlagsAny) != 0) {
		return anyType;
	}
	if ((left->flags & TypeFlagsUnknown) != 0 || (right->flags & TypeFlagsUnknown) != 0) {
		return unknownType;
	}
	if ((left->flags & TypeFlagsNever) != 0) {
		return right;
	}
	if ((right->flags & TypeFlagsNever) != 0) {
		return left;
	}
	left = tryMergeUnionOfObjectTypeAndEmptyObject(left, readonly);
	if ((left->flags & TypeFlagsUnion) != 0) {
		if (checkCrossProductUnion({left, right})) {
			return mapType(left, [this, right, symbol, objectFlags, readonly](Type* t) {
				return getSpreadType(t, right, symbol, objectFlags, readonly);
			});
		}
		return errorType;
	}
	right = tryMergeUnionOfObjectTypeAndEmptyObject(right, readonly);
	if ((right->flags & TypeFlagsUnion) != 0) {
		if (checkCrossProductUnion({left, right})) {
			return mapType(right, [this, left, symbol, objectFlags, readonly](Type* t) {
				return getSpreadType(left, t, symbol, objectFlags, readonly);
			});
		}
		return errorType;
	}
	if ((right->flags & (TypeFlagsBooleanLike | TypeFlagsNumberLike | TypeFlagsBigIntLike | TypeFlagsStringLike | TypeFlagsEnumLike | TypeFlagsNonPrimitive | TypeFlagsIndex)) != 0) {
		return left;
	}
	if (isGenericObjectType(left) || isGenericObjectType(right)) {
		if (isEmptyObjectType(left)) {
			return right;
		}
		// When the left type is an intersection, we may need to merge the last constituent of the
		// intersection with the right type. For example when the left type is 'T & { a: string }'
		// and the right type is '{ b: string }' we produce 'T & { a: string, b: string }'.
		if ((left->flags & TypeFlagsIntersection) != 0) {
			const std::vector<Type*>& types = left->types();
			Type* lastLeft = types[types.size() - 1];
			if (isNonGenericObjectType(lastLeft) && isNonGenericObjectType(right)) {
				std::vector<Type*> newTypes = types;
				newTypes[newTypes.size() - 1] = getSpreadType(lastLeft, right, symbol, objectFlags, readonly);
				return getIntersectionType(newTypes);
			}
		}
		return getIntersectionType({left, right});
	}
	SymbolTable members;
	std::unordered_set<std::string> skippedPrivateMembers;
	std::vector<IndexInfo*> indexInfos;
	if (left == emptyObjectType) {
		indexInfos = getIndexInfosOfType(right);
	} else {
		indexInfos = getUnionIndexInfos({left, right});
	}
	for (Symbol* rightProp : getPropertiesOfType(right)) {
		if ((getDeclarationModifierFlagsFromSymbol(rightProp) & (ModifierFlagsPrivate | ModifierFlagsProtected)) != 0) {
			skippedPrivateMembers.insert(rightProp->name);
		} else if (isSpreadableProperty(rightProp)) {
			members[rightProp->name] = getSpreadSymbol(rightProp, readonly);
		}
	}

	for (Symbol* leftProp : getPropertiesOfType(left)) {
		if (skippedPrivateMembers.count(leftProp->name) != 0 || !isSpreadableProperty(leftProp)) {
			continue;
		}
		auto it = members.find(leftProp->name);
		if (it != members.end() && it->second != nullptr) {
			Symbol* rightProp = it->second;
			Type* rightType = getTypeOfSymbol(rightProp);
			if ((rightProp->flags & SymbolFlagsOptional) != 0) {
				std::vector<Node*> declarations = concatenate(leftProp->declarations, rightProp->declarations);
				SymbolFlags flags = SymbolFlagsProperty | (leftProp->flags & SymbolFlagsOptional);
				Symbol* result = newSymbol(flags, leftProp->name);
				ValueSymbolLinks* links = valueSymbolLinks.Get(result);
				// Optimization: avoid calculating the union type if spreading into the exact same type.
				// This is common, e.g. spreading one options bag into another where the bags have the
				// same type, or have properties which overlap. If the unions are large, it may turn out
				// to be expensive to perform subtype reduction.
				Type* leftType = getTypeOfSymbol(leftProp);
				Type* leftTypeWithoutUndefined = removeMissingOrUndefinedType(leftType);
				Type* rightTypeWithoutUndefined = removeMissingOrUndefinedType(rightType);
				if (leftTypeWithoutUndefined == rightTypeWithoutUndefined) {
					links->resolvedType = leftType;
				} else {
					links->resolvedType = getUnionTypeEx({leftType, rightTypeWithoutUndefined}, UnionReductionSubtype, nullptr, nullptr);
				}
				spreadLinks.Get(result)->leftSpread = leftProp;
				spreadLinks.Get(result)->rightSpread = rightProp;
				result->declarations = declarations;
				links->nameType = valueSymbolLinks.Get(leftProp)->nameType;
				members[leftProp->name] = result;
			}
		} else {
			members[leftProp->name] = getSpreadSymbol(leftProp, readonly);
		}
	}
	std::vector<IndexInfo*> spreadIndexInfos = sameMap(indexInfos, [this, readonly](IndexInfo* info) {
		return getIndexInfoWithReadonly(info, readonly);
	});
	Type* spread = newAnonymousType(symbol, members, {}, {}, spreadIndexInfos);
	spread->objectFlags |= ObjectFlagsObjectLiteral | ObjectFlagsContainsObjectOrArrayLiteral | ObjectFlagsContainsSpread | objectFlags;
	return spread;
}

// ---------------------------------------------------------------------------
// checker.go:13714 — getIndexInfoWithReadonly
// ---------------------------------------------------------------------------

IndexInfo* Checker::getIndexInfoWithReadonly(IndexInfo* info, bool readonly) {
	if (info->isReadonly != readonly) {
		return newIndexInfo(info->keyType, info->valueType, readonly, info->declaration, info->components);
	}
	return info;
}

// ---------------------------------------------------------------------------
// checker.go:13721 — isValidSpreadType
// ---------------------------------------------------------------------------

bool Checker::isValidSpreadType(Type* t) {
	Type* s = removeDefinitelyFalsyTypes(mapType(t, [this](Type* u) { return getBaseConstraintOrType(u); }));
	return (s->flags & (TypeFlagsAny | TypeFlagsNonPrimitive | TypeFlagsObject | TypeFlagsInstantiableNonPrimitive)) != 0 ||
		((s->flags & TypeFlagsUnionOrIntersection) != 0 && everyList(s->types(), [this](Type* u) { return isValidSpreadType(u); }));
}

// ---------------------------------------------------------------------------
// checker.go:13727 — getUnionIndexInfos
// ---------------------------------------------------------------------------

std::vector<IndexInfo*> Checker::getUnionIndexInfos(const std::vector<Type*>& types) {
	std::vector<IndexInfo*> sourceInfos = getIndexInfosOfType(types[0]);
	std::vector<IndexInfo*> result;
	for (IndexInfo* info : sourceInfos) {
		Type* indexType = info->keyType;
		if (everyList(types, [this, indexType](Type* t) { return getIndexInfoOfType(t, indexType) != nullptr; })) {
			Type* valueType = getUnionType(mapVec(types, [this, indexType](Type* t) {
				return getIndexTypeOfType(t, indexType);
			}));
			bool isReadonly = someList(types, [this, indexType](Type* t) { return getIndexInfoOfType(t, indexType)->isReadonly; });
			result.push_back(newIndexInfo(indexType, valueType, isReadonly, nullptr, {}));
		}
	}
	return result;
}

// ---------------------------------------------------------------------------
// checker.go:13743 — isNonGenericObjectType
// ---------------------------------------------------------------------------

bool Checker::isNonGenericObjectType(Type* t) {
	return (t->flags & TypeFlagsObject) != 0 && !isGenericMappedType(t);
}

// ---------------------------------------------------------------------------
// checker.go:13747 — tryMergeUnionOfObjectTypeAndEmptyObject
// ---------------------------------------------------------------------------

Type* Checker::tryMergeUnionOfObjectTypeAndEmptyObject(Type* t, bool readonly) {
	if ((t->flags & TypeFlagsUnion) == 0) {
		return t;
	}
	if (everyList(t->types(), [this](Type* u) { return isEmptyObjectTypeOrSpreadsIntoEmptyObject(u); })) {
		Type* empty = findOrNull(t->types(), [this](Type* u) { return isEmptyObjectType(u); });
		if (empty != nullptr) {
			return empty;
		}
		return emptyObjectType;
	}
	Type* firstType = findOrNull(t->types(), [this](Type* u) {
		return !isEmptyObjectTypeOrSpreadsIntoEmptyObject(u);
	});
	if (firstType == nullptr) {
		return t;
	}
	Type* secondType = findOrNull(t->types(), [this, firstType](Type* u) {
		return u != firstType && !isEmptyObjectTypeOrSpreadsIntoEmptyObject(u);
	});
	if (secondType != nullptr) {
		return t;
	}
	// gets the type as if it had been spread, but where everything in the spread is made optional
	SymbolTable members;
	for (Symbol* prop : getPropertiesOfType(firstType)) {
		if ((getDeclarationModifierFlagsFromSymbol(prop) & (ModifierFlagsPrivate | ModifierFlagsProtected)) != 0) {
			// do nothing, skip privates
		} else if (isSpreadableProperty(prop)) {
			bool isSetonlyAccessor = (prop->flags & SymbolFlagsSetAccessor) != 0 && (prop->flags & SymbolFlagsGetAccessor) == 0;
			SymbolFlags flags = SymbolFlagsProperty | SymbolFlagsOptional;
			Symbol* result = newSymbolEx(flags, prop->name, (prop->checkFlags & CheckFlagsLate) | ifElse(readonly, CheckFlagsReadonly, CheckFlagsNone));
			ValueSymbolLinks* links = valueSymbolLinks.Get(result);
			if (isSetonlyAccessor) {
				links->resolvedType = undefinedType;
			} else {
				links->resolvedType = addOptionalityEx(getTypeOfSymbol(prop), true /*isProperty*/, true /*isOptional*/);
			}
			result->declarations = prop->declarations;
			links->nameType = valueSymbolLinks.Get(prop)->nameType;
			mappedSymbolLinks.Get(result)->syntheticOrigin = prop;
			members[prop->name] = result;
		}
	}
	Type* spread = newAnonymousType(firstType->symbol, members, {}, {}, getIndexInfosOfType(firstType));
	spread->objectFlags |= ObjectFlagsObjectLiteral | ObjectFlagsContainsObjectOrArrayLiteral;
	return spread;
}

// ---------------------------------------------------------------------------
// checker.go:13797 — isSpreadableProperty
// We approximate own properties as non-methods plus methods that are inside
// the object literal
// ---------------------------------------------------------------------------

bool Checker::isSpreadableProperty(Symbol* prop) {
	return (!someList(prop->declarations, isPrivateIdentifierClassElementDeclaration) &&
			(prop->flags & (SymbolFlagsMethod | SymbolFlagsGetAccessor | SymbolFlagsSetAccessor)) == 0) ||
		!someList(prop->declarations, [](Node* d) { return d->parent != nullptr && isClassLike(d->parent); });
}

// ---------------------------------------------------------------------------
// checker.go:13802 — getSpreadSymbol
// ---------------------------------------------------------------------------

Symbol* Checker::getSpreadSymbol(Symbol* prop, bool readonly) {
	bool isSetonlyAccessor = (prop->flags & SymbolFlagsSetAccessor) != 0 && (prop->flags & SymbolFlagsGetAccessor) == 0;
	if (!isSetonlyAccessor && readonly == isReadonlySymbol(prop)) {
		return prop;
	}
	SymbolFlags flags = SymbolFlagsProperty | (prop->flags & SymbolFlagsOptional);
	Symbol* result = newSymbolEx(flags, prop->name, (prop->checkFlags & CheckFlagsLate) | ifElse(readonly, CheckFlagsReadonly, CheckFlagsNone));
	ValueSymbolLinks* links = valueSymbolLinks.Get(result);
	if (isSetonlyAccessor) {
		links->resolvedType = undefinedType;
	} else {
		links->resolvedType = getTypeOfSymbol(prop);
	}
	result->declarations = prop->declarations;
	links->nameType = valueSymbolLinks.Get(prop)->nameType;
	mappedSymbolLinks.Get(result)->syntheticOrigin = prop;
	return result;
}

// ---------------------------------------------------------------------------
// checker.go:13821 — isEmptyObjectTypeOrSpreadsIntoEmptyObject
// ---------------------------------------------------------------------------

bool Checker::isEmptyObjectTypeOrSpreadsIntoEmptyObject(Type* t) {
	return isEmptyObjectType(t) || (t->flags & (TypeFlagsNull | TypeFlagsUndefined | TypeFlagsBooleanLike | TypeFlagsNumberLike | TypeFlagsBigIntLike | TypeFlagsStringLike | TypeFlagsEnumLike | TypeFlagsNonPrimitive | TypeFlagsIndex)) != 0;
}

// ---------------------------------------------------------------------------
// checker.go:13825 — hasDefaultValue
// ---------------------------------------------------------------------------

bool Checker::hasDefaultValue(Node* node) {
	return (isBindingElement(node) && node->initializer() != nullptr) ||
		(isPropertyAssignment(node) && hasDefaultValue(node->initializer())) ||
		(isShorthandPropertyAssignment(node) && node->as<ShorthandPropertyAssignment>()->ObjectAssignmentInitializer != nullptr) ||
		(isBinaryExpression(node) && node->as<BinaryExpression>()->OperatorToken->kind == Kind::EqualsToken);
}

// ---------------------------------------------------------------------------
// checker.go:13832 — isConstContext
// ---------------------------------------------------------------------------

bool Checker::isConstContext(Node* node) {
	Node* parent = node->parent;
	return isConstAssertion(parent) ||
		isInlineImportAttributes(node) ||
		(isValidConstAssertionArgument(node) && isConstTypeVariable(getContextualType(node, ContextFlagsNone), 0)) ||
		((isParenthesizedExpression(parent) || isArrayLiteralExpression(parent) || isSpreadElement(parent)) && isConstContext(parent)) ||
		((isPropertyAssignment(parent) || isShorthandPropertyAssignment(parent) || isTemplateSpan(parent)) && isConstContext(parent->parent));
}

// ---------------------------------------------------------------------------
// checker.go:13841 — isInlineImportAttributes
// ---------------------------------------------------------------------------

bool Checker::isInlineImportAttributes(Node* node) {
	if (!isObjectLiteralExpression(node) || !isPropertyAssignment(node->parent) || node->parent->initializer() != node) {
		return false;
	}
	Node* property = node->parent;
	if ((!isIdentifier(property->name()) && !isStringLiteralLike(property->name())) || property->name()->text() != "with") {
		return false;
	}
	Node* options = property->parent;
	if (!isObjectLiteralExpression(options)) {
		return false;
	}
	Node* importCall = findAncestor(options, isImportCall);
	return importCall != nullptr && importCall->arguments().size() > 1 && skipParentheses(importCall->arguments()[1]) == options;
}

// ---------------------------------------------------------------------------
// checker.go:13857 — isValidConstAssertionArgument
// ---------------------------------------------------------------------------

bool Checker::isValidConstAssertionArgument(Node* node) {
	switch (node->kind) {
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::ArrayLiteralExpression:
	case Kind::ObjectLiteralExpression:
	case Kind::TemplateExpression:
		return true;
	case Kind::ParenthesizedExpression:
		return isValidConstAssertionArgument(node->expression());
	case Kind::PrefixUnaryExpression: {
		Kind op = node->as<PrefixUnaryExpression>()->Operator;
		Node* arg = node->as<PrefixUnaryExpression>()->Operand;
		return (op == Kind::MinusToken && (arg->kind == Kind::NumericLiteral || arg->kind == Kind::BigIntLiteral)) ||
			(op == Kind::PlusToken && arg->kind == Kind::NumericLiteral);
	}
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression: {
		Node* expr = skipParentheses(node->expression());
		Symbol* symbol = nullptr;
		if (isEntityNameExpression(expr)) {
			symbol = resolveEntityName(expr, SymbolFlagsValue, true /*ignoreErrors*/, false, nullptr);
		}
		return symbol != nullptr && (symbol->flags & SymbolFlagsEnum) != 0;
	}
	}
	return false;
}

// ---------------------------------------------------------------------------
// checker.go:13879 — isConstTypeVariable
// ---------------------------------------------------------------------------

bool Checker::isConstTypeVariable(Type* t, int depth) {
	if (depth >= 5 || t == nullptr) {
		return false;
	}
	if ((t->flags & TypeFlagsTypeParameter) != 0) {
		return t->symbol != nullptr && someList(t->symbol->declarations, [](Node* d) { return hasSyntacticModifier(d, ModifierFlagsConst); });
	}
	if ((t->flags & TypeFlagsUnionOrIntersection) != 0) {
		return someList(t->types(), [this, depth](Type* s) { return isConstTypeVariable(s, depth); });
	}
	if ((t->flags & TypeFlagsIndexedAccess) != 0) {
		return isConstTypeVariable(t->AsIndexedAccessType()->objectType, depth + 1);
	}
	if ((t->flags & TypeFlagsConditional) != 0) {
		return isConstTypeVariable(getConstraintOfConditionalType(t), depth + 1);
	}
	if ((t->flags & TypeFlagsSubstitution) != 0) {
		return isConstTypeVariable(t->AsSubstitutionType()->baseType, depth);
	}
	if ((t->objectFlags & ObjectFlagsMapped) != 0) {
		Type* typeVariable = getHomomorphicTypeVariable(t);
		return typeVariable != nullptr && isConstTypeVariable(typeVariable, depth);
	}
	if (isGenericTupleType(t)) {
		const std::vector<Type*>& elementTypes = getElementTypes(t);
		for (size_t i = 0; i < elementTypes.size(); i++) {
			if ((t->TargetTupleType()->elementInfos[i].flags & ElementFlagsVariadic) != 0 && isConstTypeVariable(elementTypes[i], depth)) {
				return true;
			}
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// checker.go:13907 — checkPropertyAssignment
// ---------------------------------------------------------------------------

Type* Checker::checkPropertyAssignment(Node* node, CheckMode checkMode) {
	// Do not use hasDynamicName here, because that returns false for well known symbols.
	// We want to perform checkComputedPropertyName for all computed properties, including
	// well known symbols.
	if (isComputedPropertyName(node->name())) {
		checkComputedPropertyName(node->name());
	}
	Type* initializerType = checkExpressionForMutableLocation(node->initializer(), checkMode);
	if (node->type() != nullptr) {
		Type* t = getTypeFromTypeNode(node->type());
		checkTypeAssignableToAndOptionallyElaborate(initializerType, t, node, node->initializer(), nullptr /*headMessage*/, nullptr);
		return t;
	}
	return initializerType;
}

// ---------------------------------------------------------------------------
// checker.go:13923 — checkShorthandPropertyAssignment
// ---------------------------------------------------------------------------

Type* Checker::checkShorthandPropertyAssignment(Node* node, bool inDestructuringPattern, CheckMode checkMode) {
	Node* expr = nullptr;
	if (!inDestructuringPattern) {
		expr = node->as<ShorthandPropertyAssignment>()->ObjectAssignmentInitializer;
	}
	if (expr == nullptr) {
		expr = node->name();
	}
	Type* expressionType = checkExpressionForMutableLocation(expr, checkMode);
	if (node->type() != nullptr) {
		Type* t = getTypeFromTypeNode(node->type());
		checkTypeAssignableToAndOptionallyElaborate(expressionType, t, node, expr, nullptr /*headMessage*/, nullptr);
		return t;
	}
	return expressionType;
}

// ---------------------------------------------------------------------------
// checker.go:13940 — isInPropertyInitializerOrClassStaticBlock
// ---------------------------------------------------------------------------

bool Checker::isInPropertyInitializerOrClassStaticBlock(Node* node, bool ignoreArrowFunctions) {
	return findAncestorOrQuit(node, [ignoreArrowFunctions](Node* node) -> FindAncestorResult {
		switch (node->kind) {
		case Kind::PropertyDeclaration:
		case Kind::ClassStaticBlockDeclaration:
			return FindAncestorResult::True;
		case Kind::TypeQuery:
		case Kind::JsxClosingElement:
			return FindAncestorResult::Quit;
		case Kind::ArrowFunction:
			return ifElse(ignoreArrowFunctions, FindAncestorResult::False, FindAncestorResult::Quit);
		case Kind::Block:
			return ifElse(isFunctionLikeDeclaration(node->parent) && node->parent->kind != Kind::ArrowFunction, FindAncestorResult::Quit, FindAncestorResult::False);
		default:
			return FindAncestorResult::False;
		}
	}) != nullptr;
}

// ---------------------------------------------------------------------------
// checker.go:13957 — getNarrowedTypeOfSymbol
// ---------------------------------------------------------------------------

Type* Checker::getNarrowedTypeOfSymbol(Symbol* symbol, Node* location) {
	Type* t = getTypeOfSymbol(symbol);
	Node* declaration = symbol->valueDeclaration;
	if (declaration != nullptr) {
		// If we have a non-rest binding element with no initializer declared as a const variable or a const-like
		// parameter (a parameter for which there are no assignments in the function body), and if the parent type
		// for the destructuring is a union type, one or more of the binding elements may represent discriminant
		// properties, and we want the effects of conditional checks on such discriminants to affect the types of
		// other binding elements from the same destructuring.
		if (isBindingElement(declaration) && declaration->initializer() == nullptr && !hasDotDotDotToken(declaration) && declaration->parent->elements().size() >= 2) {
			Node* rootDeclaration = getRootDeclaration(declaration);
			Node* rootInitializer = rootDeclaration->initializer();
			// Avoid declaration circularity without blocking binding defaults or nested callbacks.
			if (!(rootInitializer != nullptr &&
				isNodeDescendantOf(location, rootInitializer) &&
				getControlFlowContainer(declaration) == getControlFlowContainer(location))) {
				Node* parent = declaration->parent->parent;
				if ((isVariableDeclaration(rootDeclaration) && (getCombinedNodeFlagsCached(rootDeclaration) & NodeFlagsConstant) != 0) || isParameterDeclaration(rootDeclaration)) {
					NodeLinks* links = nodeLinks.Get(parent);
					if ((links->flags & NodeCheckFlagsInCheckIdentifier) == 0) {
						links->flags |= NodeCheckFlagsInCheckIdentifier;
						Type* parentType = getTypeForBindingElementParent(parent, CheckModeNormal);
						Type* parentTypeConstraint = nullptr;
						if (parentType != nullptr) {
							parentTypeConstraint = mapType(parentType, [this](Type* u) { return getBaseConstraintOrType(u); });
						}
						// Guard parent-type resolution only; flow analysis should allow re-entrant narrowing
						links->flags &= ~NodeCheckFlagsInCheckIdentifier;
						if (parentTypeConstraint != nullptr && (parentTypeConstraint->flags & TypeFlagsUnion) != 0 && !(isParameterDeclaration(rootDeclaration) && isSomeSymbolAssigned(rootDeclaration))) {
							Node* pattern = declaration->parent;
							Type* narrowedType = getFlowTypeOfReferenceEx(pattern, parentTypeConstraint, parentTypeConstraint, nullptr /*flowContainer*/, getFlowNodeOfNode(location));
							if ((narrowedType->flags & TypeFlagsNever) != 0) {
								t = neverType;
							} else {
								// Destructurings are validated against the parent type elsewhere. Here we disable tuple bounds
								// checks because the narrowed type may have lower arity than the full parent type. For example,
								// for the declaration [x, y]: [1, 2] | [3], we may have narrowed the parent type to just [3].
								t = getBindingElementTypeFromParentType(declaration, narrowedType, true /*noTupleBoundsCheck*/);
							}
						}
					}
				}
			}
		// If we have a const-like parameter with no type annotation or initializer, and if the parameter is contextually
		// typed by a signature with a single rest parameter of a union of tuple types, one or more of the parameters may
		// represent discriminant tuple elements, and we want the effects of conditional checks on such discriminants to
		// affect the types of other parameters in the same parameter list.
		} else if (isParameterDeclaration(declaration) && declaration->type() == nullptr && declaration->initializer() == nullptr && !hasDotDotDotToken(declaration)) {
			Node* fn = declaration->parent;
			if (fn->parameters().size() >= 2 && isContextSensitiveFunctionOrObjectLiteralMethod(fn)) {
				Signature* contextualSignature = getContextualSignature(fn);
				if (contextualSignature != nullptr && contextualSignature->parameters.size() == 1 && signatureHasRestParameter(contextualSignature)) {
					TypeMapper* mapper = nullptr;
					InferenceContext* context = getInferenceContext(fn);
					if (context != nullptr) {
						mapper = context->nonFixingMapper;
					}
					Type* restType = getReducedApparentType(instantiateType(getTypeOfSymbol(contextualSignature->parameters[0]), mapper));
					if ((restType->flags & TypeFlagsUnion) != 0 && everyType(restType, isTupleType) && !someList(fn->parameters(), [this](Node* p) { return isSomeSymbolAssigned(p); })) {
						Type* narrowedType = getFlowTypeOfReferenceEx(fn, restType, restType, nullptr /*flowContainer*/, getFlowNodeOfNode(location));
						int index = indexOf(fn->parameters(), declaration) - ifElse(getThisParameter(fn) != nullptr, 1, 0);
						t = getIndexedAccessType(narrowedType, getNumberLiteralType(Number(static_cast<double>(index))));
					}
				}
			}
		}
	}
	return t;
}

// ---------------------------------------------------------------------------
// checker.go:14063 — isReadonlyAssignmentDeclaration
// ---------------------------------------------------------------------------

bool Checker::isReadonlyAssignmentDeclaration(Node* node) {
	if (!isCallExpression(node)) {
		return false;
	}
	Type* propertyDescriptorType = checkExpressionCached(node->arguments()[2]);
	if (Type* valueType = getTypeOfPropertyOfType(propertyDescriptorType, "value"); valueType != nullptr) {
		if (Symbol* writableProp = getPropertyOfType(propertyDescriptorType, "writable"); writableProp != nullptr) {
			Type* writableType;
			if (writableProp->valueDeclaration != nullptr && isPropertyAssignment(writableProp->valueDeclaration)) {
				writableType = checkExpression(writableProp->valueDeclaration->initializer());
			} else {
				writableType = getTypeOfSymbol(writableProp);
			}
			return (writableType->flags & TypeFlagsBooleanLiteral) != 0 && getBooleanLiteralValue(writableType) == false;
		}
		return true;
	}
	return getTypeOfPropertyOfType(propertyDescriptorType, "set") == nullptr;
}

// ---------------------------------------------------------------------------
// checker.go:14083 — isReadonlySymbol
// ---------------------------------------------------------------------------

// The following symbols are considered read-only:
// Properties with a 'readonly' modifier
// Variables declared with 'const'
// Get accessors without matching set accessors
// Enum members
// Object.defineProperty assignments with writable false or no setter
// Unions and intersections of the above (unions and intersections eagerly set isReadonly on creation)
bool Checker::isReadonlySymbol(Symbol* symbol) {
	return (symbol->checkFlags & CheckFlagsReadonly) != 0 ||
		((symbol->flags & SymbolFlagsProperty) != 0 && (getDeclarationModifierFlagsFromSymbol(symbol) & ModifierFlagsReadonly) != 0) ||
		((symbol->flags & SymbolFlagsVariable) != 0 && (getDeclarationNodeFlagsFromSymbol(symbol) & NodeFlagsConstant) != 0) ||
		((symbol->flags & SymbolFlagsAccessor) != 0 && (symbol->flags & SymbolFlagsSetAccessor) == 0) ||
		(symbol->flags & SymbolFlagsEnumMember) != 0 ||
		someList(symbol->declarations, [this](Node* d) { return isReadonlyAssignmentDeclaration(d); });
}

// ---------------------------------------------------------------------------
// checker.go:14099 — checkObjectLiteralMethod
// ---------------------------------------------------------------------------

Type* Checker::checkObjectLiteralMethod(Node* node, CheckMode checkMode) {
	// Grammar checking
	checkGrammarMethod(node);
	// Do not use hasDynamicName here, because that returns false for well known symbols.
	// We want to perform checkComputedPropertyName for all computed properties, including
	// well known symbols.
	if (isComputedPropertyName(node->name())) {
		checkComputedPropertyName(node->name());
	}
	Type* uninstantiatedType = checkFunctionExpressionOrObjectLiteralMethod(node, checkMode);
	return instantiateTypeWithSingleGenericCallSignature(node, uninstantiatedType, checkMode);
}

// ---------------------------------------------------------------------------
// checker.go:14112 — checkExpressionForMutableLocation
// ---------------------------------------------------------------------------

Type* Checker::checkExpressionForMutableLocation(Node* node, CheckMode checkMode) {
	Type* t = checkExpressionEx(node, checkMode);
	if (isConstContext(node)) {
		return getRegularTypeOfLiteralType(t);
	}
	if (isTypeAssertion(node)) {
		return t;
	}
	return getWidenedLiteralLikeTypeForContextualType(t, instantiateContextualType(getContextualType(node, ContextFlagsNone), node, ContextFlagsNone));
}

// ---------------------------------------------------------------------------
// checker.go:14124 — getResolvedSymbol
// ---------------------------------------------------------------------------

Symbol* Checker::getResolvedSymbol(Node* node) {
	SymbolNodeLinks* links = symbolNodeLinks.Get(node);
	if (links->resolvedSymbol == nullptr ||
		staleForCheckFile(links->resolvedSymbolCheckFile)) {
		// Go: fresh per-checker cache — re-resolve under this file.
		links->resolvedSymbol = nullptr;
		links->resolvedSymbolCheckFile = checkFileTag();
		Symbol* symbol = nullptr;
		if (!nodeIsMissing(node)) {
			std::string scratch;
			symbol = resolveName(node, node->textView(scratch), SymbolFlagsValue | SymbolFlagsExportValue,
				getCannotFindNameDiagnosticForName(node), !isWriteOnlyAccess(node), false /*excludeGlobals*/);
		}
		links->resolvedSymbol = orElse(symbol, unknownSymbol);
	}
	return links->resolvedSymbol;
}

// ---------------------------------------------------------------------------
// checker.go:14137 — getResolvedSymbolOrNil
// ---------------------------------------------------------------------------

Symbol* Checker::getResolvedSymbolOrNil(Node* node) {
	SymbolNodeLinks* links = symbolNodeLinks.Get(node);
	return staleForCheckFile(links->resolvedSymbolCheckFile)
			   ? nullptr : links->resolvedSymbol;
}

// ---------------------------------------------------------------------------
// checker.go:14141 — getReferencedValueOrAliasSymbol
// ---------------------------------------------------------------------------

Symbol* Checker::getReferencedValueOrAliasSymbol(Node* reference) {
	SymbolNodeLinks* links = symbolNodeLinks.Get(reference);
	Symbol* resolvedSymbol = staleForCheckFile(links->resolvedSymbolCheckFile)
								? nullptr : links->resolvedSymbol;
	if (resolvedSymbol != nullptr && resolvedSymbol != unknownSymbol) {
		return resolvedSymbol;
	}
	return resolveName(reference, reference->text(), SymbolFlagsValue | SymbolFlagsExportValue | SymbolFlagsAlias, nullptr, false /*isUse*/, false /*excludeGlobals*/);
}
// (deduped: isTypeComparableTo defined in checker_relater.cpp)

// (deduped: getTypeNamesForErrorDisplay defined in checker_relater.cpp)

// inference.go:1285 — inference slice
// (deduped: addIntraExpressionInferenceSite defined in checker_inference.cpp)

} // namespace tsc::checker
