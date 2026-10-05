// checker_stmtclass.cpp — checker.go:3830-5081 "stmtclass" slice.
// Statements (if/do/while/for/for-in/for-of/break/continue/return/with/
// switch/labeled/throw/try/catch/binding-element) + class-like declaration
// checking + member override machinery + index constraints + property
// initialization analysis. Ported faithfully from tsc/internal/checker/
// checker.go; cross-slice callees are TSC_UNREACHABLE stubs at the bottom.

#include "internal/ast/ast.h"
#include "internal/binder/binder.h"
#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/checker/types.h"
#include "internal/core/types.h"
#include "internal/evaluator/evaluator.h"
#include "internal/scanner/scanner.h"

#include <algorithm>
#include <unordered_map>

namespace tsc::checker {

// ---------------------------------------------------------------------------
// File-local helpers (free functions owned by other files — internal linkage)
// ---------------------------------------------------------------------------

// utilities.go:346
static bool isTypeAssertion(Node* node) {
	return isAssertionExpression(skipParentheses(node));
}

// utilities.go:908
static Node* getContainingFunctionOrClassStaticBlock(Node* node) {
	return findAncestor(node->parent, isFunctionLikeOrClassStaticBlockDeclaration);
}

// grammarchecks.go: getIdentifierFromEntityNameExpression
static Node* getIdentifierFromEntityNameExpression(Node* node) {
	Node* n = skipParentheses(node);
	if (n != nullptr && isIdentifier(n)) {
		return n;
	}
	if (n != nullptr && isPropertyAccessExpression(n)) {
		return getIdentifierFromEntityNameExpression(n->expression());
	}
	return nullptr;
}

// utilities.go:1923
static std::string quotedAndCommaSeparated(const std::vector<std::string>& items) {
	std::string result;
	for (size_t i = 0; i < items.size(); i++) {
		if (i != 0) {
			result += ", ";
		}
		result += "'" + items[i] + "'";
	}
	return result;
}

// checker.go:22033 — isPrototypeProperty (free fn; owned by members slice)
static bool isPrototypeProperty(Symbol* symbol) {
	return (symbol->flags & SymbolFlagsMethod) != 0 ||
	       (symbol->checkFlags & CheckFlagsSyntheticMethod) != 0;
}

// core.ElementOrNil / core.Find
template <class T>
static T* elementOrNil(const std::vector<T*>& v, size_t i) {
	return i < v.size() ? v[i] : nullptr;
}

template <class T, class Pred>
static T* findFirstOrNil(const std::vector<T*>& v, Pred pred) {
	for (T* x : v) {
		if (pred(x)) {
			return x;
		}
	}
	return nullptr;
}

// ast/utilities.go:2994
static Node* getClassLikeDeclarationOfSymbol(Symbol* symbol) {
	return findFirstOrNil(symbol->declarations, [](Node* d) { return isClassLike(d); });
}

// utilities.go:757 + :760
static ModifierFlags getDeclarationModifierFlagsFromSymbolEx(Symbol* s, bool isWrite) {
	if (s->checkFlags & CheckFlagsSynthetic) {
		ModifierFlags accessModifier{};
		if ((!isWrite && (s->checkFlags & CheckFlagsContainsPublic)) ||
		    (isWrite && (s->checkFlags & CheckFlagsContainsWritePublic))) {
			accessModifier = ModifierFlagsPublic;
		} else if ((!isWrite && (s->checkFlags & CheckFlagsContainsProtected)) ||
		           (isWrite && (s->checkFlags & CheckFlagsContainsWriteProtected))) {
			accessModifier = ModifierFlagsProtected;
		} else if ((!isWrite && (s->checkFlags & CheckFlagsContainsPrivate)) ||
		           (isWrite && (s->checkFlags & CheckFlagsContainsWritePrivate))) {
			accessModifier = ModifierFlagsPrivate;
		}
		if (s->checkFlags & CheckFlagsContainsStatic) {
			return accessModifier | ModifierFlagsStatic;
		}
		return accessModifier;
	}
	if (s->valueDeclaration != nullptr) {
		Node* declaration = nullptr;
		if (isWrite) {
			declaration = findFirstOrNil(s->declarations, isSetAccessorDeclaration);
		}
		if (declaration == nullptr && (s->flags & SymbolFlagsGetAccessor)) {
			declaration = findFirstOrNil(s->declarations, isGetAccessorDeclaration);
		}
		if (declaration == nullptr) {
			declaration = s->valueDeclaration;
		}
		ModifierFlags flags = getCombinedModifierFlags(declaration);
		if (s->parent != nullptr && (s->parent->flags & SymbolFlagsClass)) {
			return flags;
		}
		return flags & ~ModifierFlagsAccessibilityModifier;
	}
	if (s->flags & SymbolFlagsPrototype) {
		return ModifierFlagsPublic | ModifierFlagsStatic;
	}
	return ModifierFlagsNone;
}

static ModifierFlags getDeclarationModifierFlagsFromSymbol(Symbol* s) {
	return getDeclarationModifierFlagsFromSymbolEx(s, false /*isWrite*/);
}

// types.go:740 — Type.Distributed
static std::vector<Type*> distributed(Type* t) {
	if (t->flags & TypeFlagsUnion) {
		return t->AsUnionType()->types;
	}
	if (t->flags & TypeFlagsNever) {
		return {};
	}
	return {t};
}

// ---------------------------------------------------------------------------
// Statement checking — checker.go:3830-4305
// ---------------------------------------------------------------------------

// checker.go:3830
void Checker::checkIfStatement(Node* node) {
	checkGrammarStatementInAmbientContext(node);
	Type* t = checkTruthinessExpression(node->expression(), CheckModeNormal);
	auto* data = node->as<IfStatement>();
	checkTestingKnownTruthyCallableOrAwaitableOrEnumMemberType(node->expression(), t,
	                                                           data->ThenStatement);
	checkSourceElement(data->ThenStatement);
	if (isEmptyStatement(data->ThenStatement)) {
		error(data->ThenStatement,
		      The_body_of_an_if_statement_cannot_be_the_empty_statement);
	}
	checkSourceElement(data->ElseStatement);
}

// checker.go:3842
void Checker::checkTestingKnownTruthyCallableOrAwaitableOrEnumMemberType(Node* condExpr,
                                                                         Type* condType,
                                                                         Node* body) {
	if (!strictNullChecks) {
		return;
	}
	checkTestingKnownTruthyTypes(condExpr, condType, body);
}

// checker.go:3849
void Checker::checkTestingKnownTruthyTypes(Node* condExpr, Type* condType, Node* body) {
	condExpr = skipParentheses(condExpr);
	checkTestingKnownTruthyType(condExpr, condType, body);
	while (isBinaryExpression(condExpr) &&
	       (condExpr->as<BinaryExpression>()->OperatorToken->kind == Kind::BarBarToken ||
	        condExpr->as<BinaryExpression>()->OperatorToken->kind == Kind::QuestionQuestionToken)) {
		condExpr = skipParentheses(condExpr->as<BinaryExpression>()->Left);
		checkTestingKnownTruthyType(condExpr, condType, body);
	}
}

// checker.go:3858
void Checker::checkTestingKnownTruthyType(Node* condExpr, Type* condType, Node* body) {
	Node* location = condExpr;
	if (isLogicalOrCoalescingBinaryExpression(condExpr)) {
		location = skipParentheses(condExpr->as<BinaryExpression>()->Right);
	}
	if (isModuleExportsAccessExpression(location)) {
		return;
	}
	if (isLogicalOrCoalescingBinaryExpression(location)) {
		checkTestingKnownTruthyTypes(location, condType, body);
		return;
	}
	Type* t = condType;
	if (location != condExpr) {
		t = checkExpression(location);
	}
	if ((t->flags & TypeFlagsEnumLiteral) != 0 && isPropertyAccessExpression(location) &&
	    (((getResolvedSymbolOrNil(location->expression()) != nullptr
	           ? getResolvedSymbolOrNil(location->expression())
	           : unknownSymbol)
	              ->flags &
	      SymbolFlagsEnum) != 0)) {
		// EnumLiteral type at condition with known value is always truthy or always falsy, likely an error
		error(location, This_condition_will_always_return_0,
		      {std::string(tsc::evalIsTruthy(t->AsLiteralType()->value) ? "true"
		                                                            : "false")});
		return;
	}
	bool isPropertyExpressionCast =
	    isPropertyAccessExpression(location) &&
	    isTypeAssertion(location->expression());
	if (!hasTypeFacts(t, TypeFactsTruthy) || isPropertyExpressionCast) {
		return;
	}
	// While it technically should be invalid for any known-truthy value
	// to be tested, we de-scope to functions and Promises unreferenced in
	// the block as a heuristic to identify the most common bugs. There
	// are too many false positives for values sourced from type
	// definitions without strictNullChecks otherwise.
	std::vector<Signature*> callSignatures = getSignaturesOfType(t, SignatureKind::Call);
	bool isPromise = getAwaitedTypeOfPromise(t) != nullptr;
	if (callSignatures.empty() && !isPromise) {
		return;
	}
	Node* testedNode = nullptr;
	if (isIdentifier(location)) {
		testedNode = location;
	} else if (isPropertyAccessExpression(location)) {
		testedNode = location->name();
	}
	Symbol* testedSymbol = nullptr;
	if (testedNode != nullptr) {
		testedSymbol = getSymbolAtLocation(testedNode, false);
	}
	if (testedSymbol == nullptr && !isPromise) {
		return;
	}
	bool isUsed = (testedSymbol != nullptr && isBinaryExpression(condExpr->parent) &&
	               isSymbolUsedInBinaryExpressionChain(condExpr->parent, testedSymbol)) ||
	              (testedSymbol != nullptr && body != nullptr &&
	               isSymbolUsedInConditionBody(condExpr, body, testedNode, testedSymbol));
	if (!isUsed) {
		if (isPromise) {
			errorAndMaybeSuggestAwait(
			    location, true,
			    This_condition_will_always_return_true_since_this_0_is_always_defined,
			    {getTypeNameForErrorDisplay(t)});
		} else {
			error(location,
			      This_condition_will_always_return_true_since_this_function_is_always_defined_Did_you_mean_to_call_it_instead);
		}
	}
}

// checker.go:3918
bool Checker::isSymbolUsedInBinaryExpressionChain(Node* node, Symbol* testedSymbol) {
	std::function<bool(Node*)> visit;
	visit = [&](Node* child) -> bool {
		if (isIdentifier(child)) {
			Symbol* symbol = getSymbolAtLocation(child, false);
			if (symbol != nullptr && symbol == testedSymbol) {
				return true;
			}
		}
		return child->forEachChild(visit);
	};
	while (isBinaryExpression(node) &&
	       node->as<BinaryExpression>()->OperatorToken->kind == Kind::AmpersandAmpersandToken) {
		bool isUsed = node->as<BinaryExpression>()->Right->forEachChild(visit);
		if (isUsed) {
			return true;
		}
		node = node->parent;
	}
	return false;
}

// checker.go:3939
bool Checker::isSymbolUsedInConditionBody(Node* expr, Node* body, Node* testedNode,
                                          Symbol* testedSymbol) {
	std::function<bool(Node*)> visit;
	visit = [&](Node* childNode) -> bool {
		if (isIdentifier(childNode)) {
			Symbol* childSymbol = getSymbolAtLocation(childNode, false);
			if (childSymbol != nullptr && childSymbol == testedSymbol) {
				// If the test was a simple identifier, the above check is sufficient
				if (isIdentifier(expr) ||
				    (isIdentifier(testedNode) && isBinaryExpression(testedNode->parent))) {
					return true;
				}
				// Otherwise we need to ensure the symbol is called on the same target
				Node* testedExpression = testedNode->parent;
				Node* childExpression = childNode->parent;
				while (testedExpression != nullptr && childExpression != nullptr) {
					if ((isIdentifier(testedExpression) && isIdentifier(childExpression)) ||
					    (testedExpression->kind == Kind::ThisKeyword &&
					     childExpression->kind == Kind::ThisKeyword)) {
						return getSymbolAtLocation(testedExpression, false) ==
						       getSymbolAtLocation(childExpression, false);
					} else if (isPropertyAccessExpression(testedExpression) &&
					           isPropertyAccessExpression(childExpression)) {
						if (getSymbolAtLocation(testedExpression->name(), false) !=
						    getSymbolAtLocation(childExpression->name(), false)) {
							return false;
						}
						childExpression = childExpression->expression();
						testedExpression = testedExpression->expression();
					} else if (isCallExpression(testedExpression) &&
					           isCallExpression(childExpression)) {
						childExpression = childExpression->expression();
						testedExpression = testedExpression->expression();
					} else {
						return false;
					}
				}
			}
		}
		return childNode->forEachChild(visit);
	};
	return body->forEachChild(visit);
}

// checker.go:3975
void Checker::checkDoStatement(Node* node) {
	checkGrammarStatementInAmbientContext(node);
	checkSourceElement(node->statement());
	checkTruthinessExpression(node->expression(), CheckModeNormal);
}

// checker.go:3981
void Checker::checkWhileStatement(Node* node) {
	checkGrammarStatementInAmbientContext(node);
	checkTruthinessExpression(node->expression(), CheckModeNormal);
	checkSourceElement(node->statement());
}

// checker.go:3987
void Checker::checkForStatement(Node* node) {
	if (!checkGrammarStatementInAmbientContext(node)) {
		Node* init = node->initializer();
		if (init != nullptr && init->kind == Kind::VariableDeclarationList) {
			checkGrammarVariableDeclarationList(init->as<VariableDeclarationList>());
		}
	}
	auto* data = node->as<ForStatement>();
	if (data->Initializer != nullptr) {
		if (isVariableDeclarationList(data->Initializer)) {
			checkVariableDeclarationList(data->Initializer);
		} else {
			checkExpression(data->Initializer);
		}
	}
	if (data->Condition != nullptr) {
		checkTruthinessExpression(data->Condition, CheckModeNormal);
	}
	if (data->Incrementor != nullptr) {
		checkExpression(data->Incrementor);
	}
	checkSourceElement(data->Statement);
	if (node->locals() != nullptr) {
		registerForUnusedIdentifiersCheck(node);
	}
}

// checker.go:4013
void Checker::checkForInStatement(Node* node) {
	auto* data = node->as<ForInOrOfStatement>();
	checkGrammarForInOrForOfStatement(data);
	Type* rightType = getNonNullableTypeIfNeeded(checkExpression(data->Expression));
	// TypeScript 1.0 spec (April 2014): 5.4
	// In a 'for-in' statement of the form
	// for (let VarDecl in Expr) Statement
	//   VarDecl must be a variable declaration without a type annotation that declares a variable of type Any,
	//   and Expr must be an expression of type Any, an object type, or a type parameter type.
	if (isVariableDeclarationList(data->Initializer)) {
		auto& declarations = data->Initializer->as<VariableDeclarationList>()->Declarations->nodes;
		if (!declarations.empty() && isBindingPattern(declarations[0]->name())) {
			error(declarations[0]->name(),
			      The_left_hand_side_of_a_for_in_statement_cannot_be_a_destructuring_pattern);
		}
		checkVariableDeclarationList(data->Initializer);
	} else {
		// In a 'for-in' statement of the form
		// for (Var in Expr) Statement
		//   Var must be an expression classified as a reference of type Any or the String primitive type,
		//   and Expr must be an expression of type Any, an object type, or a type parameter type.
		Node* varExpr = data->Initializer;
		Type* leftType = checkExpression(varExpr);
		if (isArrayLiteralExpression(varExpr) || isObjectLiteralExpression(varExpr)) {
			error(varExpr,
			      The_left_hand_side_of_a_for_in_statement_cannot_be_a_destructuring_pattern);
		} else if (!isTypeAssignableTo(getIndexTypeOrString(rightType), leftType)) {
			error(varExpr,
			      The_left_hand_side_of_a_for_in_statement_must_be_of_type_string_or_any);
		} else {
			// run check only former check succeeded to avoid cascading errors
			checkReferenceExpression(
			    varExpr,
			    The_left_hand_side_of_a_for_in_statement_must_be_a_variable_or_a_property_access,
			    The_left_hand_side_of_a_for_in_statement_may_not_be_an_optional_property_access);
		}
	}
	// unknownType is returned i.e. if node.expression is identifier whose name cannot be resolved
	// in this case error about missing name is already reported - do not report extra one
	if (rightType == neverType ||
	    !isTypeAssignableToKind(rightType, TypeFlagsNonPrimitive | TypeFlagsInstantiableNonPrimitive)) {
		error(data->Expression,
		      The_right_hand_side_of_a_for_in_statement_must_be_of_type_any_an_object_type_or_a_type_parameter_but_here_has_type_0,
		      {TypeToString(rightType)});
	}
	checkSourceElement(data->Statement);
	if (node->locals() != nullptr) {
		registerForUnusedIdentifiersCheck(node);
	}
}

// checker.go:4055
Type* Checker::getIndexTypeOrString(Type* t) {
	Type* indexType = getExtractStringType(getIndexTypeEx(t, IndexFlagsNone));
	return (indexType->flags & TypeFlagsNever) != 0 ? stringType : indexType;
}

// checker.go:4060
void Checker::checkForOfStatement(Node* node) {
	auto* data = node->as<ForInOrOfStatement>();
	checkGrammarForInOrForOfStatement(data);
	Node* container = getContainingFunctionOrClassStaticBlock(node);
	if (data->AwaitModifier != nullptr) {
		if (container != nullptr && isClassStaticBlockDeclaration(container)) {
			grammarErrorOnNode(data->AwaitModifier,
			                   X_for_await_loops_cannot_be_used_inside_a_class_static_block);
		} else {
			FunctionFlags functionFlags = getFunctionFlags(container);
			if ((functionFlags & (FunctionFlagsInvalid | FunctionFlagsAsync)) == FunctionFlagsAsync &&
			    languageVersion < LanguageFeatureMinimumTarget.ForAwaitOf) {
				// for..await..of in an async function or async generator function prior to ESNext requires the __asyncValues helper
				checkExternalEmitHelpers(node, ExternalEmitHelpersForAwaitOfIncludes);
			}
		}
	} // Check the LHS and RHS
	// If the LHS is a declaration, just check it as a variable declaration, which will in turn check the RHS
	// via checkRightHandSideOfForOf.
	// If the LHS is an expression, check the LHS, as a destructuring assignment or as a reference.
	// Then check that the RHS is assignable to it.
	if (isVariableDeclarationList(data->Initializer)) {
		checkVariableDeclarationList(data->Initializer);
	} else {
		Node* varExpr = data->Initializer;
		Type* iteratedType = checkRightHandSideOfForOf(node);
		// There may be a destructuring assignment on the left side
		if (isArrayLiteralExpression(varExpr) || isObjectLiteralExpression(varExpr)) {
			// iteratedType may be undefined. In this case, we still want to check the structure of
			// varExpr, in particular making sure it's a valid LeftHandSideExpression. But we'd like
			// to short circuit the type relation checking as much as possible, so we pass the unknownType.
			checkDestructuringAssignment(varExpr,
			                             iteratedType != nullptr ? iteratedType : errorType,
			                             CheckModeNormal, false);
		} else {
			Type* leftType = checkExpression(varExpr);
			checkReferenceExpression(
			    varExpr,
			    The_left_hand_side_of_a_for_of_statement_must_be_a_variable_or_a_property_access,
			    The_left_hand_side_of_a_for_of_statement_may_not_be_an_optional_property_access);
			// iteratedType will be undefined if the rightType was missing properties/signatures
			// required to get its iteratedType (like [Symbol.iterator] or next). This may be
			// because we accessed properties from anyType, or it may have led to an error inside
			// getElementTypeOfIterable.
			if (iteratedType != nullptr) {
				checkTypeAssignableToAndOptionallyElaborate(iteratedType, leftType, varExpr,
				                                          data->Expression, nullptr, nullptr);
			}
		}
	}
	checkSourceElement(data->Statement);
	if (node->locals() != nullptr) {
		registerForUnusedIdentifiersCheck(node);
	}
}

// checker.go:4108
void Checker::checkBreakOrContinueStatement(Node* node) {
	if (!checkGrammarStatementInAmbientContext(node)) {
		checkGrammarBreakOrContinueStatement(node);
	}
}

// checker.go:4114
void Checker::checkReturnStatement(Node* node) {
	// Always check the return expression so its identifiers are resolved even when the
	// return statement is misplaced (grammar error), keeping diagnostics stable
	// regardless of traversal order.
	Node* exprNode = node->expression();
	Type* exprType = undefinedType;
	if (exprNode != nullptr) {
		exprType = checkExpressionCached(exprNode);
	}
	if (checkGrammarStatementInAmbientContext(node)) {
		return;
	}
	Node* container = getContainingFunctionOrClassStaticBlock(node);
	if (container != nullptr && isClassStaticBlockDeclaration(container)) {
		grammarErrorOnFirstToken(
		    node, A_return_statement_cannot_be_used_inside_a_class_static_block);
		return;
	}
	if (container == nullptr) {
		grammarErrorOnFirstToken(
		    node, A_return_statement_can_only_be_used_within_a_function_body);
		return;
	}
	Signature* signature = getSignatureFromDeclaration(container);
	Type* returnType = getReturnTypeOfSignature(signature);
	FunctionFlags functionFlags = getFunctionFlags(container);
	if (strictNullChecks || exprNode != nullptr || (returnType->flags & TypeFlagsNever)) {
		if (isSetAccessorDeclaration(container)) {
			if (exprNode != nullptr) {
				error(node, Setters_cannot_return_a_value);
			}
		} else if (isConstructorDeclaration(container)) {
			if (exprNode != nullptr &&
			    !checkTypeAssignableToAndOptionallyElaborate(exprType, returnType, node,
			                                               exprNode, nullptr, nullptr)) {
				error(node,
				      Return_type_of_constructor_signature_must_be_assignable_to_the_instance_type_of_the_class);
			}
		} else if (getReturnTypeFromAnnotation(container) != nullptr) {
			Type* unwrappedReturnType = unwrapReturnType(returnType, functionFlags) != nullptr
			                                ? unwrapReturnType(returnType, functionFlags)
			                                : returnType;
			checkReturnExpression(container, unwrappedReturnType, node, node->expression(),
			                      exprType, false);
		}
	} else if (!isConstructorDeclaration(container) &&
	           tristateIsTrue(compilerOptions->NoImplicitReturns) &&
	           !isUnwrappedReturnTypeUndefinedVoidOrAny(container, returnType)) {
		// The function has a return type, but the return statement doesn't have an expression.
		error(node, Not_all_code_paths_return_a_value);
	}
}

// When checking an arrow expression such as `(x) => exp`, then `node` is the expression `exp`.
// Otherwise, `node` is a return statement.
// checker.go:4159
void Checker::checkReturnExpression(Node* container, Type* unwrappedReturnType, Node* node,
                                    Node* expr, Type* exprType, bool inConditionalExpression) {
	Type* unwrappedExprType = exprType;
	FunctionFlags functionFlags = getFunctionFlags(container);
	if (expr != nullptr) {
		Node* unwrappedExpr = skipParentheses(expr);
		if (isConditionalExpression(unwrappedExpr)) {
			Node* whenTrue = unwrappedExpr->as<ConditionalExpression>()->WhenTrue;
			Node* whenFalse = unwrappedExpr->as<ConditionalExpression>()->WhenFalse;
			checkReturnExpression(container, unwrappedReturnType, node, whenTrue,
			                      checkExpression(whenTrue), true /*inConditionalExpression*/);
			checkReturnExpression(container, unwrappedReturnType, node, whenFalse,
			                      checkExpression(whenFalse), true /*inConditionalExpression*/);
			return;
		}
	}
	bool inReturnStatement = node->kind == Kind::ReturnStatement;
	if (functionFlags & FunctionFlagsAsync) {
		unwrappedExprType = checkAwaitedType(
		    exprType, false /*withAlias*/, node,
		    The_return_type_of_an_async_function_must_either_be_a_valid_promise_or_must_not_contain_a_callable_then_member);
	}
	Node* effectiveExpr = expr; // The effective expression for diagnostics purposes.
	if (expr != nullptr) {
		effectiveExpr = getEffectiveCheckNode(expr);
	}
	Node* errorNode =
	    inReturnStatement && !inConditionalExpression ? node : effectiveExpr;
	checkTypeAssignableToAndOptionallyElaborate(unwrappedExprType, unwrappedReturnType,
	                                            errorNode, effectiveExpr, nullptr, nullptr);
}

// checker.go:4184
void Checker::checkWithStatement(Node* node) {
	if (!checkGrammarStatementInAmbientContext(node)) {
		if (node->flags & NodeFlagsAwaitContext) {
			grammarErrorOnFirstToken(
			    node, X_with_statements_are_not_allowed_in_an_async_function_block);
		}
	}
	checkExpression(node->expression());
	SourceFile* sourceFile = getSourceFileOfNode(node);
	if (!hasParseDiagnostics(sourceFile)) {
		int32_t start = skipTrivia(sourceFile->text, node->pos());
		int32_t end = node->statement()->pos();
		grammarErrorAtPos(
		    sourceFile->asNode(), start, end - start,
		    The_with_statement_is_not_supported_All_symbols_in_a_with_block_will_have_type_any);
	}
}

// checker.go:4199
void Checker::checkSwitchStatement(Node* node) {
	// Grammar checking
	checkGrammarStatementInAmbientContext(node);
	Node* firstDefaultClause = nullptr;
	bool hasDuplicateDefaultClause = false;
	Type* expressionType = checkExpression(node->expression());
	Node* caseBlock = node->as<SwitchStatement>()->CaseBlock;
	for (Node* clause : caseBlock->as<CaseBlock>()->Clauses->nodes) {
		// Grammar check for duplicate default clauses, skip if we already report duplicate default clause
		if (isDefaultClause(clause) && !hasDuplicateDefaultClause) {
			if (firstDefaultClause == nullptr) {
				firstDefaultClause = clause;
			} else {
				grammarErrorOnNode(
				    clause,
				    A_default_clause_cannot_appear_more_than_once_in_a_switch_statement);
				hasDuplicateDefaultClause = true;
			}
		}
		if (isCaseClause(clause)) {
			Type* caseType = checkExpression(clause->expression());
			if (!isTypeEqualityComparableTo(expressionType, caseType)) {
				// expressionType is not comparable to caseType, try the reversed check and report errors if it fails
				checkTypeComparableTo(caseType, expressionType, clause->expression(),
				                      nullptr /*headMessage*/);
			}
		}
		checkSourceElements(clause->statements());
		if (tristateIsTrue(compilerOptions->NoFallthroughCasesInSwitch)) {
			FlowNode* flowNode = clause->as<CaseOrDefaultClause>()->FallthroughFlowNode;
			if (flowNode != nullptr && isReachableFlowNode(flowNode)) {
				error(clause, Fallthrough_case_in_switch);
			}
		}
	}
	if (caseBlock->locals() != nullptr) {
		registerForUnusedIdentifiersCheck(caseBlock);
	}
}

// checker.go:4235
void Checker::checkLabeledStatement(Node* node) {
	auto* labeledStatement = node->as<LabeledStatement>();
	Node* labelNode = labeledStatement->Label;
	std::string labelText = labelNode->text();
	if (!checkGrammarStatementInAmbientContext(node)) {
		for (Node* current = node->parent;
		     current != nullptr && !isFunctionLike(current);
		     current = current->parent) {
			if (isLabeledStatement(current) && current->label()->text() == labelText) {
				grammarErrorOnNode(labelNode, Duplicate_label_0, {labelText});
				break;
			}
		}
	}
	if ((labelNode->flags & NodeFlagsUnreachable) &&
	    compilerOptions->AllowUnusedLabels != Tristate::True) {
		errorOrSuggestion(compilerOptions->AllowUnusedLabels == Tristate::False, labelNode,
		                  Unused_label);
	}
	checkSourceElement(labeledStatement->Statement);
}

// checker.go:4253
void Checker::checkThrowStatement(Node* node) {
	Node* throwExpr = node->expression();
	if (!checkGrammarStatementInAmbientContext(node)) {
		if (isIdentifier(throwExpr) && throwExpr->text().empty()) {
			grammarErrorAtPos(node, throwExpr->pos(), 0 /*length*/,
			                  Line_break_not_permitted_here);
		}
	}
	checkExpression(throwExpr);
}

// checker.go:4263
void Checker::checkTryStatement(Node* node) {
	checkGrammarStatementInAmbientContext(node);
	auto* data = node->as<TryStatement>();
	checkBlock(data->TryBlock);
	if (data->CatchClause != nullptr) {
		checkCatchClause(data->CatchClause);
	}
	if (data->FinallyBlock != nullptr) {
		checkBlock(data->FinallyBlock);
	}
}

// checker.go:4275
void Checker::checkCatchClause(Node* node) {
	Node* declaration = node->as<CatchClause>()->VariableDeclaration;
	if (declaration != nullptr) {
		checkVariableLikeDeclaration(declaration);
		Node* typeNode = declaration->type();
		if (typeNode != nullptr) {
			Type* t = getTypeFromTypeNode(typeNode);
			if (t != nullptr && !(t->flags & TypeFlagsAnyOrUnknown)) {
				grammarErrorOnFirstToken(
				    typeNode,
				    Catch_clause_variable_type_annotation_must_be_any_or_unknown_if_specified);
			}
		} else if (declaration->initializer() != nullptr) {
			grammarErrorOnFirstToken(
			    declaration->initializer(),
			    Catch_clause_variable_cannot_have_an_initializer);
		} else {
			SymbolTable* blockLocals = node->as<CatchClause>()->Block->locals();
			if (blockLocals != nullptr) {
				for (auto& [caughtName, _sym] : *node->locals()) {
					Symbol* blockLocal = nullptr;
					auto it = blockLocals->find(caughtName);
					if (it != blockLocals->end()) {
						blockLocal = it->second;
					}
					if (blockLocal != nullptr && blockLocal->valueDeclaration != nullptr &&
					    (blockLocal->flags & SymbolFlagsBlockScopedVariable)) {
						grammarErrorOnNode(
						    blockLocal->valueDeclaration,
						    Cannot_redeclare_identifier_0_in_catch_clause, {caughtName});
					}
				}
			}
		}
	}
	checkBlock(node->as<CatchClause>()->Block);
}

// checker.go:4301
void Checker::checkBindingElement(Node* node) {
	checkGrammarBindingElement(node->as<BindingElement>());
	checkVariableLikeDeclaration(node);
}

// checker.go:4306
void Checker::checkClassDeclaration(Node* node) {
	Node* firstDecorator = findFirstOrNil(node->modifierNodes(),
	                                      [](Node* n) { return isDecorator(n); });
	if (legacyDecorators && firstDecorator != nullptr) {
		bool hasStaticPrivate = false;
		for (Node* p : node->members()) {
			if (hasStaticModifier(p) && isPrivateIdentifierClassElementDeclaration(p)) {
				hasStaticPrivate = true;
				break;
			}
		}
		if (hasStaticPrivate) {
			grammarErrorOnNode(
			    firstDecorator,
			    Class_decorators_can_t_be_used_with_static_private_identifier_Consider_removing_the_experimental_decorator);
		}
	}
	if (node->name() == nullptr &&
	    !hasSyntacticModifier(node, ModifierFlagsDefault)) {
		grammarErrorOnFirstToken(
		    node, A_class_declaration_without_the_default_modifier_must_have_a_name);
	}
	checkClassLikeDeclaration(node);
	checkSourceElements(node->members());
	registerForUnusedIdentifiersCheck(node);
}

// ---------------------------------------------------------------------------
// Class machinery — checker.go:4321-5081
// ---------------------------------------------------------------------------

// checker.go:4321
void Checker::checkClassLikeDeclaration(Node* node) {
	checkGrammarClassLikeDeclaration(node);
	checkDecorators(node);
	checkCollisionsForDeclarationName(node, node->name());
	checkTypeParameters(node->typeParameters());
	checkExportsOnMergedDeclarations(node);
	Symbol* symbol = getSymbolOfDeclaration(node);
	Type* classType = getDeclaredTypeOfSymbol(symbol);
	auto* classTypeData = classType->AsInterfaceType();
	Type* typeWithThis = getTypeWithThisArgument(classType, nullptr, false);
	Type* staticType = getTypeOfSymbol(symbol);
	checkTypeParameterListsIdentical(symbol);
	checkFunctionOrConstructorSymbol(symbol);
	checkObjectTypeForDuplicateDeclarations(node, true /*checkPrivateNames*/);

	// Only check for reserved static identifiers on non-ambient context.
	bool nodeInAmbientContext = (node->flags & NodeFlagsAmbient) != 0;
	if (!nodeInAmbientContext) {
		checkClassForStaticPropertyNameConflicts(node);
	}

	Node* baseTypeNode = getClassExtendsHeritageElement(node);
	if (baseTypeNode != nullptr) {
		checkSourceElements(baseTypeNode->typeArguments());
		std::vector<Type*> baseTypes = getBaseTypes(classType);
		if (!baseTypes.empty()) {
			Type* baseType = baseTypes[0];
			checkJSDocAugmentsTagMatchesExtends(
			    node, baseTypeNode->as<ExpressionWithTypeArguments>(), baseType);
			Type* baseConstructorType = getBaseConstructorTypeOfClass(classType);
			Type* staticBaseType = getApparentType(baseConstructorType);
			checkBaseTypeAccessibility(staticBaseType, baseTypeNode);
			checkSourceElement(baseTypeNode->expression());
			if (!baseTypeNode->typeArguments().empty()) {
				checkSourceElements(baseTypeNode->typeArguments());
				for (Signature* constructor :
				     getConstructorsForTypeArguments(staticBaseType,
				                                     baseTypeNode->typeArguments(), baseTypeNode)) {
					if (!checkTypeArgumentConstraints(baseTypeNode,
					                                  constructor->typeParameters)) {
						break;
					}
				}
			}
			Type* baseWithThis =
			    getTypeWithThisArgument(baseType, classTypeData->thisType, false);
			if (!checkTypeAssignableTo(typeWithThis, baseWithThis, nullptr, nullptr)) {
				issueMemberSpecificError(
				    node, typeWithThis, baseWithThis,
				    Class_0_incorrectly_extends_base_class_1);
			} else {
				// Report static side error only when instance type is assignable
				checkTypeAssignableTo(
				    staticType, getTypeWithoutSignatures(staticBaseType),
				    node->name() != nullptr ? node->name() : node,
				    Class_static_side_0_incorrectly_extends_base_class_static_side_1);
			}
			if (baseConstructorType->flags & TypeFlagsTypeVariable) {
				if (!isMixinConstructorType(staticType)) {
					error(node->name() != nullptr ? node->name() : node,
					      A_mixin_class_must_have_a_constructor_with_a_single_rest_parameter_of_type_any);
				} else {
					std::vector<Signature*> constructSignatures =
					    getSignaturesOfType(baseConstructorType, SignatureKind::Construct);
					bool anyAbstract = false;
					for (Signature* signature : constructSignatures) {
						if (signature->flags & SignatureFlagsAbstract) {
							anyAbstract = true;
							break;
						}
					}
					if (anyAbstract &&
					    !hasSyntacticModifier(node, ModifierFlagsAbstract)) {
						error(node->name() != nullptr ? node->name() : node,
						      A_mixin_class_that_extends_from_a_type_variable_containing_an_abstract_construct_signature_must_also_be_declared_abstract);
					}
				}
			}
			if (!(staticBaseType->symbol != nullptr &&
			      (staticBaseType->symbol->flags & SymbolFlagsClass)) &&
			    !(baseConstructorType->flags & TypeFlagsTypeVariable)) {
				// When the static base type is a "class-like" constructor function (but not actually a class), we verify
				// that all instantiated base constructor signatures return the same type.
				std::vector<Signature*> constructors =
				    getInstantiatedConstructorsForTypeArguments(
				        staticBaseType, baseTypeNode->typeArguments(), baseTypeNode);
				bool allIdentical = true;
				for (Signature* sig : constructors) {
					if (!isTypeIdenticalTo(getReturnTypeOfSignature(sig), baseType)) {
						allIdentical = false;
						break;
					}
				}
				if (!allIdentical) {
					error(baseTypeNode->expression(),
					      Base_constructors_must_all_have_the_same_return_type);
				}
			}
			checkKindsOfPropertyMemberOverrides(classType, baseType);
		}
	}
	checkMembersForOverrideModifier(node, classType, typeWithThis, staticType);
	std::vector<Node*> implementedTypeNodes = getHeritageElements(node, Kind::ImplementsKeyword);
	for (Node* typeRefNode : implementedTypeNodes) {
		if (isExpressionWithTypeArguments(typeRefNode)) {
			Node* expr = typeRefNode->expression();
			if (!isEntityNameExpression(expr) || isOptionalChain(expr)) {
				error(expr,
				      A_class_can_only_implement_an_identifier_Slashqualified_name_with_optional_type_arguments);
			}
		}
		checkTypeReferenceNode(typeRefNode);
		Type* t = getReducedType(getTypeFromTypeNode(typeRefNode));
		if (!isErrorType(t)) {
			if (isValidBaseType(t)) {
				const DiagnosticMessage* genericDiag =
				    (t->symbol != nullptr && (t->symbol->flags & SymbolFlagsClass))
				        ? Class_0_incorrectly_implements_class_1_Did_you_mean_to_extend_1_and_inherit_its_members_as_a_subclass
				        : Class_0_incorrectly_implements_interface_1;
				Type* baseWithThis = getTypeWithThisArgument(
				    t, classType->AsInterfaceType()->thisType, false);
				if (!checkTypeAssignableTo(typeWithThis, baseWithThis, nullptr, nullptr)) {
					issueMemberSpecificError(node, typeWithThis, baseWithThis, genericDiag);
				}
			} else {
				error(typeRefNode,
				      A_class_can_only_implement_an_object_type_or_intersection_of_object_types_with_statically_known_members);
			}
		}
	}
	checkIndexConstraints(classType, symbol, false /*isStaticIndex*/);
	checkIndexConstraints(staticType, symbol, true /*isStaticIndex*/);
	checkClassOrInterfaceForDuplicateIndexSignatures(node);
	checkPropertyInitialization(node);
}

// checker.go:4424
void Checker::checkJSDocAugmentsTagMatchesExtends(Node* node,
                                                  ExpressionWithTypeArguments* baseTypeNode,
                                                  Type* baseType) {
	if (!isInJSFile(node)) {
		return;
	}
	SourceFile* file = getSourceFileOfNode(node);
	for (Node* j : node->eagerJSDoc(file)) {
		if (j->as<JSDoc>()->Tags == nullptr) {
			continue;
		}
		for (Node* tag : j->as<JSDoc>()->Tags->nodes) {
			if (tag->kind != Kind::JSDocAugmentsTag) {
				continue;
			}
			Node* sourceTypeNode = tag->className();
			if (isTypeIdenticalTo(getTypeFromTypeNode(sourceTypeNode), baseType)) {
				continue;
			}
			Node* targetName =
			    getIdentifierFromEntityNameExpression(baseTypeNode->Expression);
			Node* sourceName =
			    getIdentifierFromEntityNameExpression(sourceTypeNode->expression());
			if (targetName != nullptr && sourceName != nullptr) {
				error(sourceName,
				      JSDoc_0_1_does_not_match_the_extends_2_clause,
				      {tag->tagName()->text(), sourceName->text(),
				       targetName->text()});
			}
		}
	}
}

// checker.go:4450
void Checker::checkClassForStaticPropertyNameConflicts(Node* node) {
	if (compilerOptions->GetUseDefineForClassFields()) {
		return;
	}
	for (Node* member : node->members()) {
		Node* memberNameNode = member->name();
		bool isStaticMember = tsc::isStatic(member);
		if (isStaticMember && memberNameNode != nullptr) {
			auto [memberName, _ignore] =
			    getEffectivePropertyNameForPropertyNameNode(memberNameNode);
			if (memberName == "name" || memberName == "length" ||
			    memberName == "caller" || memberName == "arguments") {
				error(memberNameNode,
				      Static_property_0_conflicts_with_built_in_property_Function_0_of_constructor_function_1,
				      {memberName,
				       symbolToString(getSymbolOfDeclaration(node))});
			}
		}
	}
}

// Check that type parameter lists are identical across multiple declarations
// checker.go:4473
void Checker::checkTypeParameterListsIdentical(Symbol* symbol) {
	if (symbol->declarations.size() == 1) {
		return;
	}
	auto* links = declaredTypeLinks.Get(symbol);
	if (!links->typeParametersChecked) {
		links->typeParametersChecked = true;
		std::vector<Node*> declarations = getClassOrInterfaceDeclarationsOfSymbol(symbol);
		if (declarations.size() <= 1) {
			return;
		}
		Type* t = getDeclaredTypeOfSymbol(symbol);
		if (!areTypeParametersIdentical(
		        declarations, interfaceTypeLocalTypeParameters(t->AsInterfaceType()),
		        [](Node* n) { return n->typeParameters(); })) {
			// Report an error on every conflicting declaration.
			std::string name = symbolToString(symbol);
			for (Node* declaration : declarations) {
				error(declaration->name(),
				      All_declarations_of_0_must_have_identical_type_parameters,
				      {name});
			}
		}
	}
}

// checker.go:4495
std::vector<Node*> Checker::getClassOrInterfaceDeclarationsOfSymbol(Symbol* symbol) {
	std::vector<Node*> result;
	for (Node* d : symbol->declarations) {
		if (isClassDeclaration(d) || isInterfaceDeclaration(d)) {
			result.push_back(d);
		}
	}
	return result;
}

// checker.go:4501
bool Checker::areTypeParametersIdentical(
    const std::vector<Node*>& declarations, const std::vector<Type*>& targetParameters,
    const std::function<std::vector<Node*>(Node*)>& getTypeParameterDeclarations) {
	size_t maxTypeArgumentCount = targetParameters.size();
	size_t minTypeArgumentCount = static_cast<size_t>(getMinTypeArgumentCount(targetParameters));
	for (Node* declaration : declarations) {
		// If this declaration has too few or too many type parameters, we report an error
		std::vector<Node*> sourceParameters = getTypeParameterDeclarations(declaration);
		if (sourceParameters.size() < minTypeArgumentCount ||
		    sourceParameters.size() > maxTypeArgumentCount) {
			return false;
		}
		for (size_t i = 0; i < sourceParameters.size(); i++) {
			Node* source = sourceParameters[i];
			Type* target = targetParameters[i];
			// If the type parameter node does not have the same name as the resolved type
			// parameter at this position, we report an error.
			if (source->name()->text() != target->symbol->name) {
				return false;
			}
			// If the type parameter node does not have an identical constraintNode as the resolved
			// type parameter at this position, we report an error.
			Node* constraintNode = source->as<TypeParameterDeclaration>()->Constraint;
			Type* targetConstraint = getConstraintOfTypeParameter(target);
			// relax check if later interface augmentation has no constraint, it's more broad and is OK to merge with
			// a more constrained interface (this could be generalized to a full hierarchy check, but that's maybe overkill)
			if (constraintNode != nullptr && targetConstraint != nullptr &&
			    !isTypeIdenticalTo(getTypeFromTypeNode(constraintNode), targetConstraint)) {
				return false;
			}
			// If the type parameter node has a default and it is not identical to the default
			// for the type parameter at this position, we report an error.
			Node* defaultNode = source->as<TypeParameterDeclaration>()->DefaultType;
			Type* targetDefault = getDefaultFromTypeParameter(target);
			if (defaultNode != nullptr && targetDefault != nullptr &&
			    !isTypeIdenticalTo(getTypeFromTypeNode(defaultNode), targetDefault)) {
				return false;
			}
		}
	}
	return true;
}

// checker.go:4538
void Checker::checkBaseTypeAccessibility(Type* t, Node* node) {
	std::vector<Signature*> signatures = getSignaturesOfType(t, SignatureKind::Construct);
	ConstructorAccessibilityError* accessibilityError =
	    getConstructorAccessibilityError(node, signatures, ModifierFlagsPrivate);
	if (accessibilityError != nullptr) {
		error(node,
		      Cannot_extend_a_class_0_Class_constructor_is_marked_as_private,
		      {getFullyQualifiedName(accessibilityError->declaringClass->symbol,
		                             nullptr)});
	}
}

// checker.go:4546
void Checker::issueMemberSpecificError(Node* node, Type* typeWithThis,
                                       Type* baseWithThis,
                                       const DiagnosticMessage* broadDiag) {
	// iterate over all implemented properties and issue errors on each one which isn't compatible, rather than the class as a whole, if possible
	bool issuedMemberError = false;
	for (Node* member : node->members()) {
		if (tsc::isStatic(member)) {
			continue;
		}
		Symbol* declaredProp = getSymbolOfDeclaration(member);
		if (declaredProp != nullptr && declaredProp->name != InternalSymbolNameComputed) {
			Symbol* prop = getPropertyOfType(typeWithThis, declaredProp->name);
			Symbol* baseProp = getPropertyOfType(baseWithThis, declaredProp->name);
			if (prop != nullptr && baseProp != nullptr) {
				std::vector<Diagnostic*> diags;
				if (!checkTypeAssignableToEx(
				        getTypeOfSymbol(prop), getTypeOfSymbol(baseProp),
				        member->name() != nullptr ? member->name() : member,
				        nullptr /*headMessage*/, &diags)) {
					addDiagnostic(newDiagnosticChain(
					    diags[0],
					    Property_0_in_type_1_is_not_assignable_to_the_same_property_in_base_type_2,
					    {symbolToString(declaredProp), TypeToString(typeWithThis),
					     TypeToString(baseWithThis)}));
					issuedMemberError = true;
				}
			}
		}
	}
	if (!issuedMemberError) {
		// check again with diagnostics to generate a less-specific error
		checkTypeAssignableTo(typeWithThis, baseWithThis,
		                      node->name() != nullptr ? node->name() : node,
		                      broadDiag);
	}
}

// checker.go:4571
Type* Checker::getTypeWithoutSignatures(Type* t) {
	if (t->flags & TypeFlagsObject) {
		StructuredType* resolved = resolveStructuredTypeMembers(t);
		if (!resolved->signatures.empty()) {
			Type* result = newObjectType(ObjectFlagsAnonymous, t->symbol);
			result->objectFlags |= ObjectFlagsMembersResolved;
			result->AsObjectType()->members = resolved->members;
			result->AsObjectType()->properties = resolved->properties;
			return result;
		}
	} else if (t->flags & TypeFlagsIntersection) {
		std::vector<Type*> mapped;
		mapped.reserve(t->AsIntersectionType()->types.size());
		for (Type* it : t->AsIntersectionType()->types) {
			mapped.push_back(getTypeWithoutSignatures(it));
		}
		return getIntersectionType(mapped);
	}
	return t;
}

// checker.go:4588
void Checker::checkKindsOfPropertyMemberOverrides(Type* t, Type* baseType) {
	// TypeScript 1.0 spec (April 2014): 8.2.3
	// A derived class inherits all members from its base class it doesn't override.
	// Inheritance means that a derived class implicitly contains all non - overridden members of the base class.
	// Both public and private property members are inherited, but only public property members can be overridden.
	// A property member in a derived class is said to override a property member in a base class
	// when the derived class property member has the same name and kind(instance or static)
	// as the base class property member.
	// The type of an overriding property member must be assignable(section 3.8.4)
	// to the type of the overridden property member, or otherwise a compile - time error occurs.
	// Base class instance member functions can be overridden by derived class instance member functions,
	// but not by other kinds of members.
	// Base class instance member variables and accessors can be overridden by
	// derived class instance member variables and accessors, but not by other kinds of members.
	// NOTE: assignability is checked in checkClassDeclaration
	struct MemberInfo {
		std::vector<std::string> missedProperties;
		std::string baseTypeName;
		std::string typeName;
	};
	std::unordered_map<Node*, MemberInfo> notImplementedInfo;
	bool continueOuter = false;
	for (Symbol* baseProperty : getPropertiesOfType(baseType)) {
		continueOuter = false;
		Symbol* base = getTargetSymbol(baseProperty);
		if (base->flags & SymbolFlagsPrototype) {
			continue;
		}
		Symbol* baseSymbol = getPropertyOfObjectType(t, base->name);
		if (baseSymbol == nullptr) {
			continue;
		}
		Symbol* derived = getTargetSymbol(baseSymbol);
		ModifierFlags baseDeclarationFlags = getDeclarationModifierFlagsFromSymbol(base);
		// In order to resolve whether the inherited method was overridden in the base class or not,
		// we compare the Symbols obtained. Since getTargetSymbol returns the symbol on the *uninstantiated*
		// type declaration, derived and base resolve to the same symbol even in the case of generic classes.
		if (derived == base) {
			// derived class inherits base without override/redeclaration.
			if (baseDeclarationFlags & ModifierFlagsAbstract) {
				// It is an error to inherit an abstract member without implementing it or being declared abstract.
				// If there is no declaration for the derived class (as in the case of class expressions),
				// then the class cannot be declared abstract.
				Node* derivedClassDecl = getClassLikeDeclarationOfSymbol(t->symbol);
				if (derivedClassDecl == nullptr ||
				    !hasSyntacticModifier(derivedClassDecl, ModifierFlagsAbstract)) {
					// Searches other base types for a declaration that would satisfy the inherited abstract member.
					// (The class may have more than one base type via declaration merging with an interface with the
					// same name.)
					for (Type* otherBaseType : getBaseTypes(t)) {
						if (otherBaseType == baseType) {
							continue;
						}
						Symbol* otherBaseSymbol =
						    getPropertyOfObjectType(otherBaseType, base->name);
						if (otherBaseSymbol != nullptr &&
						    base != getTargetSymbol(otherBaseSymbol)) {
							// Derived property exists elsewhere.
							continueOuter = true;
							break;
						}
					}
					if (continueOuter) {
						continue;
					}
					std::string baseTypeName = TypeToString(baseType);
					std::string typeName = TypeToString(t);
					std::vector<std::string> missedProperties =
					    notImplementedInfo[derivedClassDecl].missedProperties;
					missedProperties.push_back(symbolToString(baseProperty));
					notImplementedInfo[derivedClassDecl] = MemberInfo{
						.missedProperties = missedProperties,
						.baseTypeName = baseTypeName,
						.typeName = typeName,
					};
				}
			}
		} else {
			// derived overrides base.
			ModifierFlags derivedDeclarationFlags =
			    getDeclarationModifierFlagsFromSymbol(derived);
			if ((baseDeclarationFlags & ModifierFlagsPrivate) ||
			    (derivedDeclarationFlags & ModifierFlagsPrivate)) {
				// either base or derived property is private - not override, skip it
				continue;
			}
			const DiagnosticMessage* errorMessage = nullptr;
			SymbolFlags basePropertyFlags = base->flags & SymbolFlagsPropertyOrAccessor;
			SymbolFlags derivedPropertyFlags =
			    derived->flags & SymbolFlagsPropertyOrAccessor;
			if (basePropertyFlags != 0 && derivedPropertyFlags != 0) {
				// property/accessor is overridden with property/accessor
				if ((base->checkFlags & CheckFlagsMapped) ||
				    (derived->valueDeclaration != nullptr &&
				     isBinaryExpression(derived->valueDeclaration)) ||
				    arePropertiesAbstractOrInterface(base, baseDeclarationFlags)) {
					// when the base property is abstract or from an interface, base/derived flags don't need to match
					// for intersection properties, this must be true of *any* of the declarations, for others it must be true of *all*
					// same when the derived property is from an assignment
					continue;
				}
				bool overriddenInstanceProperty =
				    basePropertyFlags != SymbolFlagsProperty &&
				    derivedPropertyFlags == SymbolFlagsProperty;
				bool overriddenInstanceAccessor =
				    basePropertyFlags == SymbolFlagsProperty &&
				    derivedPropertyFlags != SymbolFlagsProperty;
				if (overriddenInstanceProperty || overriddenInstanceAccessor) {
					const DiagnosticMessage* msg = overriddenInstanceProperty
					    ? X_0_is_defined_as_an_accessor_in_class_1_but_is_overridden_here_in_2_as_an_instance_property
					    : X_0_is_defined_as_a_property_in_class_1_but_is_overridden_here_in_2_as_an_accessor;
					error(derived->valueDeclaration != nullptr &&
					              getNameOfDeclaration(derived->valueDeclaration) != nullptr
					          ? getNameOfDeclaration(derived->valueDeclaration)
					          : derived->valueDeclaration,
					      msg, {symbolToString(base), TypeToString(baseType),
					            TypeToString(t)});
				} else if (compilerOptions->GetUseDefineForClassFields()) {
					Node* uninitialized = findFirstOrNil(
					    derived->declarations, [](Node* d) {
						    return isPropertyDeclaration(d) &&
						           d->initializer() == nullptr;
					    });
					bool anyAmbient = false;
					for (Node* d : derived->declarations) {
						if (d->flags & NodeFlagsAmbient) {
							anyAmbient = true;
							break;
						}
					}
					if (uninitialized != nullptr &&
					    !(derived->flags & SymbolFlagsTransient) &&
					    !(baseDeclarationFlags & ModifierFlagsAbstract) &&
					    !(derivedDeclarationFlags & ModifierFlagsAbstract) &&
					    !anyAmbient) {
						Node* constructor =
						    findConstructorDeclaration(
						        getClassLikeDeclarationOfSymbol(t->symbol));
						Node* propName = uninitialized->name();
						if (isExclamationToken(uninitialized->postfixToken()) ||
						    constructor == nullptr || !isIdentifier(propName) ||
						    !strictNullChecks ||
						    !isPropertyInitializedInConstructor(propName, t,
						                                        constructor)) {
							error(derived->valueDeclaration != nullptr &&
							              getNameOfDeclaration(
							                  derived->valueDeclaration) != nullptr
							          ? getNameOfDeclaration(derived->valueDeclaration)
							          : derived->valueDeclaration,
							      Property_0_will_overwrite_the_base_property_in_1_If_this_is_intentional_add_an_initializer_Otherwise_add_a_declare_modifier_or_remove_the_redundant_declaration,
							      {symbolToString(base), TypeToString(baseType)});
						}
					}
				}
				// correct case
				continue;
			} else if (isPrototypeProperty(base)) {
				if (isPrototypeProperty(derived) ||
				    (derived->flags & SymbolFlagsProperty)) {
					// method is overridden with method or property -- correct case
					continue;
				} else {
					errorMessage =
					    Class_0_defines_instance_member_function_1_but_extended_class_2_defines_it_as_instance_member_accessor;
				}
			} else if (base->flags & SymbolFlagsAccessor) {
				errorMessage =
				    Class_0_defines_instance_member_accessor_1_but_extended_class_2_defines_it_as_instance_member_function;
			} else {
				errorMessage =
				    Class_0_defines_instance_member_property_1_but_extended_class_2_defines_it_as_instance_member_function;
			}
			error(derived->valueDeclaration != nullptr &&
			              getNameOfDeclaration(derived->valueDeclaration) != nullptr
			          ? getNameOfDeclaration(derived->valueDeclaration)
			          : derived->valueDeclaration,
			      errorMessage,
			      {TypeToString(baseType), symbolToString(base), TypeToString(t)});
		}
	}
	for (auto& [errorNode, memberInfo] : notImplementedInfo) {
		if (memberInfo.missedProperties.size() == 1) {
			std::string missedProperty = memberInfo.missedProperties[0];
			if (isClassExpression(errorNode)) {
				error(errorNode,
				      Non_abstract_class_expression_does_not_implement_inherited_abstract_member_0_from_class_1,
				      {missedProperty, memberInfo.baseTypeName});
			} else {
				error(errorNode,
				      Non_abstract_class_0_does_not_implement_inherited_abstract_member_1_from_class_2,
				      {memberInfo.typeName, missedProperty,
				       memberInfo.baseTypeName});
			}
		} else if (memberInfo.missedProperties.size() > 5) {
			std::vector<std::string> first4(memberInfo.missedProperties.begin(),
			                                memberInfo.missedProperties.begin() + 4);
			std::string missedProperties = quotedAndCommaSeparated(first4);
			size_t remainingMissedProperties =
			    memberInfo.missedProperties.size() - 4;
			if (isClassExpression(errorNode)) {
				error(errorNode,
				      Non_abstract_class_expression_is_missing_implementations_for_the_following_members_of_0_Colon_1_and_2_more,
				      {memberInfo.baseTypeName, missedProperties,
				       std::to_string(remainingMissedProperties)});
			} else {
				error(errorNode,
				      Non_abstract_class_0_is_missing_implementations_for_the_following_members_of_1_Colon_2_and_3_more,
				      {memberInfo.typeName, memberInfo.baseTypeName,
				       missedProperties,
				       std::to_string(remainingMissedProperties)});
			}
		} else {
			std::string missedProperties =
			    quotedAndCommaSeparated(memberInfo.missedProperties);
			if (isClassExpression(errorNode)) {
				error(errorNode,
				      Non_abstract_class_expression_is_missing_implementations_for_the_following_members_of_0_Colon_1,
				      {memberInfo.baseTypeName, missedProperties});
			} else {
				error(errorNode,
				      Non_abstract_class_0_is_missing_implementations_for_the_following_members_of_1_Colon_2,
				      {memberInfo.typeName, memberInfo.baseTypeName,
				       missedProperties});
			}
		}
	}
}

// checker.go:4744
bool Checker::arePropertiesAbstractOrInterface(Symbol* base,
                                               ModifierFlags baseDeclarationFlags) {
	if (base->checkFlags & CheckFlagsSynthetic) {
		for (Node* d : base->declarations) {
			if (isPropertyAbstractOrInterface(d, baseDeclarationFlags)) {
				return true;
			}
		}
		return false;
	}
	for (Node* d : base->declarations) {
		if (!isPropertyAbstractOrInterface(d, baseDeclarationFlags)) {
			return false;
		}
	}
	return true;
}

// checker.go:4751
bool Checker::isPropertyAbstractOrInterface(Node* declaration,
                                            ModifierFlags baseDeclarationFlags) {
	return isInterfaceDeclaration(declaration->parent) ||
	       ((baseDeclarationFlags & ModifierFlagsAbstract) &&
	        (!isPropertyDeclaration(declaration) ||
	         declaration->initializer() == nullptr));
}

// checker.go:4756
void Checker::checkMembersForOverrideModifier(Node* node, Type* t,
                                              Type* typeWithThis,
                                              Type* staticType) {
	Type* baseWithThis = nullptr;
	Node* baseTypeNode = getClassExtendsHeritageElement(node);
	if (baseTypeNode != nullptr) {
		std::vector<Type*> baseTypes = getBaseTypes(t);
		if (!baseTypes.empty()) {
			baseWithThis = getTypeWithThisArgument(
			    baseTypes[0], t->AsInterfaceType()->thisType, false);
		}
	}
	Type* baseStaticType = getBaseConstructorTypeOfClass(t);
	for (Node* member : node->members()) {
		if (!hasAmbientModifier(member)) {
			if (isConstructorDeclaration(member)) {
				for (Node* param : member->parameters()) {
					if (isParameterPropertyDeclaration(param, member)) {
						checkMemberForOverrideModifier(node, staticType,
						                               baseStaticType, baseWithThis,
						                               t, typeWithThis, param);
					}
				}
			} else {
				checkMemberForOverrideModifier(node, staticType, baseStaticType,
				                               baseWithThis, t, typeWithThis, member);
			}
		}
	}
}

// checker.go:4781
void Checker::checkMemberForOverrideModifier(Node* node, Type* staticType,
                                             Type* baseStaticType, Type* baseWithThis,
                                             Type* t, Type* typeWithThis,
                                             Node* member) {
	Symbol* symbol = getSymbolOfDeclaration(member);
	if (symbol == nullptr) {
		return;
	}
	checkMemberForOverrideModifierWorker(
	    node, staticType, baseStaticType, baseWithThis, t, typeWithThis,
	    hasSyntacticModifier(member, ModifierFlagsOverride), hasAbstractModifier(member),
	    tsc::isStatic(member), isParameterDeclaration(member), symbol, member);
}

// checker.go:4790
MemberOverrideStatus Checker::getMemberOverrideModifierStatus(Node* node,
                                                              Node* member,
                                                              Symbol* memberSymbol) {
	if (member->name() == nullptr || memberSymbol == nullptr) {
		return MemberOverrideStatus::None;
	}
	Symbol* classSymbol = getSymbolOfDeclaration(node);
	if (classSymbol == nullptr) {
		return MemberOverrideStatus::None;
	}
	Type* t = getDeclaredTypeOfSymbol(classSymbol);
	Type* typeWithThis = getTypeWithThisArgument(t, nullptr, false);
	Type* staticType = getTypeOfSymbol(classSymbol);

	Type* baseWithThis = nullptr;
	if (getClassExtendsHeritageElement(node) != nullptr) {
		std::vector<Type*> baseTypes = getBaseTypes(t);
		if (!baseTypes.empty()) {
			baseWithThis = getTypeWithThisArgument(
			    baseTypes[0], t->AsInterfaceType()->thisType, false);
		}
	}
	return checkMemberForOverrideModifierWorker(
	    node, staticType, getBaseConstructorTypeOfClass(t), baseWithThis, t,
	    typeWithThis, hasSyntacticModifier(member, ModifierFlagsOverride),
	    hasAbstractModifier(member), tsc::isStatic(member),
	    false /*memberIsParameterProperty*/, memberSymbol, nullptr /*errorNode*/);
}

// checker.go:4815
MemberOverrideStatus Checker::checkMemberForOverrideModifierWorker(
    Node* node, Type* staticType, Type* baseStaticType, Type* baseWithThis, Type* t,
    Type* typeWithThis, bool memberHasOverrideModifier,
    bool memberHasAbstractModifier, bool memberIsStatic,
    bool memberIsParameterProperty, Symbol* member, Node* errorNode) {
	bool isJs = isInJSFile(node);
	if (memberHasOverrideModifier && member->valueDeclaration != nullptr &&
	    isClassElement(member->valueDeclaration) &&
	    member->valueDeclaration->name() != nullptr &&
	    isNonBindableDynamicName(member->valueDeclaration->name())) {
		if (errorNode != nullptr) {
			error(errorNode,
			      isJs ? This_member_cannot_have_a_JSDoc_comment_with_an_override_tag_because_its_name_is_dynamic
			           : This_member_cannot_have_an_override_modifier_because_its_name_is_dynamic);
		}
		return MemberOverrideStatus::HasInvalidOverride;
	}

	if (baseWithThis != nullptr &&
	    (memberHasOverrideModifier ||
	     tristateIsTrue(compilerOptions->NoImplicitOverride))) {
		Type* thisType = memberIsStatic ? staticType : typeWithThis;
		Type* baseType = memberIsStatic ? baseStaticType : baseWithThis;
		Symbol* prop = getPropertyOfType(thisType, member->name);
		Symbol* baseProp = getPropertyOfType(baseType, member->name);

		if (prop != nullptr && baseProp == nullptr && memberHasOverrideModifier) {
			if (errorNode != nullptr) {
				Symbol* suggestion =
				    getSuggestedSymbolForNonexistentClassMember(
				        member->name, baseType);
				if (suggestion != nullptr) {
					error(errorNode,
					      isJs ? This_member_cannot_have_a_JSDoc_comment_with_an_override_tag_because_it_is_not_declared_in_the_base_class_0_Did_you_mean_1
					           : This_member_cannot_have_an_override_modifier_because_it_is_not_declared_in_the_base_class_0_Did_you_mean_1,
					      {TypeToString(baseWithThis),
					       symbolToString(suggestion)});
				} else {
					error(errorNode,
					      isJs ? This_member_cannot_have_a_JSDoc_comment_with_an_override_tag_because_it_is_not_declared_in_the_base_class_0
					           : This_member_cannot_have_an_override_modifier_because_it_is_not_declared_in_the_base_class_0,
					      {TypeToString(baseWithThis)});
				}
			}
			return MemberOverrideStatus::HasInvalidOverride;
		}

		if (prop != nullptr && baseProp != nullptr &&
		    !baseProp->declarations.empty() &&
		    tristateIsTrue(compilerOptions->NoImplicitOverride) &&
		    !(node->flags & NodeFlagsAmbient)) {
			bool baseHasAbstract = false;
			for (Node* d : baseProp->declarations) {
				if (hasAbstractModifier(d)) {
					baseHasAbstract = true;
					break;
				}
			}
			if (memberHasOverrideModifier) {
				return MemberOverrideStatus::None;
			}
			if (!baseHasAbstract) {
				if (errorNode != nullptr) {
					const DiagnosticMessage* message =
					    memberIsParameterProperty
					        ? (isJs ? This_parameter_property_must_have_a_JSDoc_comment_with_an_override_tag_because_it_overrides_a_member_in_the_base_class_0
					                : This_parameter_property_must_have_an_override_modifier_because_it_overrides_a_member_in_base_class_0)
					        : (isJs ? This_member_must_have_a_JSDoc_comment_with_an_override_tag_because_it_overrides_a_member_in_the_base_class_0
					                : This_member_must_have_an_override_modifier_because_it_overrides_a_member_in_the_base_class_0);
					error(errorNode, message, {TypeToString(baseWithThis)});
				}
				return MemberOverrideStatus::NeedsOverride;
			}
			if (memberHasAbstractModifier) {
				if (errorNode != nullptr) {
					error(errorNode,
					      This_member_must_have_an_override_modifier_because_it_overrides_an_abstract_method_that_is_declared_in_the_base_class_0,
					      {TypeToString(baseWithThis)});
				}
				return MemberOverrideStatus::NeedsOverride;
			}
		}
	} else if (memberHasOverrideModifier) {
		if (errorNode != nullptr) {
			error(errorNode,
			      isJs ? This_member_cannot_have_a_JSDoc_comment_with_an_override_tag_because_its_containing_class_0_does_not_extend_another_class
			           : This_member_cannot_have_an_override_modifier_because_its_containing_class_0_does_not_extend_another_class,
			      {TypeToString(t)});
		}
		return MemberOverrideStatus::HasInvalidOverride;
	}

	return MemberOverrideStatus::None;
}

// checker.go:4873
Symbol* Checker::getSuggestedSymbolForNonexistentClassMember(std::string name,
                                                           Type* baseType) {
	return getSpellingSuggestionForName(name, getPropertiesOfType(baseType),
	                                  SymbolFlagsClassMember);
}

// checker.go:4877
void Checker::checkIndexConstraints(Type* t, Symbol* symbol, bool isStaticIndex) {
	std::vector<IndexInfo*> indexInfos = getIndexInfosOfType(t);
	if (indexInfos.empty()) {
		return;
	}
	for (Symbol* prop : getPropertiesOfObjectType(t)) {
		if (!(isStaticIndex && (prop->flags & SymbolFlagsPrototype))) {
			checkIndexConstraintForProperty(
			    t, prop,
			    getLiteralTypeFromProperty(prop,
			                               TypeFlagsStringOrNumberLiteralOrUnique,
			                               true /*includeNonPublic*/),
			    getNonMissingTypeOfSymbol(prop));
		}
	}
	Node* typeDeclaration = symbol->valueDeclaration;
	if (typeDeclaration != nullptr && isClassLike(typeDeclaration)) {
		for (Node* member : typeDeclaration->members()) {
			// Only process instance properties against instance index signatures and static properties against static index signatures
			if ((tsc::isStatic(member) == isStaticIndex) && !hasBindableName(member)) {
				Symbol* sym = getSymbolOfDeclaration(member);
				checkIndexConstraintForProperty(
				    t, sym, getTypeOfExpression(member->name()->expression()),
				    getNonMissingTypeOfSymbol(sym));
			}
		}
	}
	if (indexInfos.size() > 1) {
		for (IndexInfo* info : indexInfos) {
			checkIndexConstraintForIndexSignature(t, info);
		}
	}
}

// checker.go:4904
void Checker::checkIndexConstraintForProperty(Type* t, Symbol* prop,
                                              Type* propNameType, Type* propType) {
	Node* declaration = prop->valueDeclaration;
	Node* name = getNameOfDeclaration(declaration);
	if (name != nullptr && isPrivateIdentifier(name)) {
		return;
	}
	std::vector<IndexInfo*> indexInfos = getApplicableIndexInfos(t, propNameType);
	if (indexInfos.empty()) {
		return;
	}
	Node* interfaceDeclaration = nullptr;
	if (t->objectFlags & ObjectFlagsInterface) {
		interfaceDeclaration =
		    getDeclarationOfKind(t->symbol, Kind::InterfaceDeclaration);
	}
	Node* propDeclaration = nullptr;
	if ((declaration != nullptr && isBinaryExpression(declaration)) ||
	    (name != nullptr && isComputedPropertyName(name))) {
		propDeclaration = declaration;
	}
	Node* localPropDeclaration = nullptr;
	if (getParentOfSymbol(prop) == t->symbol) {
		localPropDeclaration = declaration;
	}
	for (IndexInfo* info : indexInfos) {
		Node* localIndexDeclaration = nullptr;
		if (info->declaration != nullptr &&
		    getParentOfSymbol(getSymbolOfDeclaration(info->declaration)) ==
		        t->symbol) {
			localIndexDeclaration = info->declaration;
		}
		// We check only when (a) the property is declared in the containing type, or (b) the applicable index signature is declared
		// in the containing type, or (c) the containing type is an interface and no base interface contains both the property and
		// the index signature (i.e. property and index signature are declared in separate inherited interfaces).
		Node* errorNode = localPropDeclaration != nullptr ? localPropDeclaration
		                                                : localIndexDeclaration;
		if (errorNode == nullptr && interfaceDeclaration != nullptr) {
			bool anyBaseHasBoth = false;
			for (Type* base : getBaseTypes(t)) {
				if (getPropertyOfObjectType(base, prop->name) != nullptr &&
				    getIndexTypeOfType(base, info->keyType) != nullptr) {
					anyBaseHasBoth = true;
					break;
				}
			}
			if (!anyBaseHasBoth) {
				errorNode = interfaceDeclaration;
			}
		}
		if (errorNode != nullptr &&
		    !isTypeAssignableTo(propType, info->valueType)) {
			Diagnostic* diagnostic = NewDiagnosticForNode(
			    errorNode,
			    Property_0_of_type_1_is_not_assignable_to_2_index_type_3,
			    {symbolToString(prop), TypeToString(propType),
			     TypeToString(info->keyType), TypeToString(info->valueType)});
			if (propDeclaration != nullptr && errorNode != propDeclaration) {
				diagnostic->AddRelatedInfo(
				    NewDiagnosticForNode(propDeclaration,
				                         X_0_is_declared_here,
				                         {symbolToString(prop)}));
			}
			addDiagnostic(diagnostic);
		}
	}
}

// checker.go:4950
void Checker::checkIndexConstraintForIndexSignature(Type* t, IndexInfo* checkInfo) {
	Node* declaration = checkInfo->declaration;
	std::vector<IndexInfo*> indexInfos =
	    getApplicableIndexInfos(t, checkInfo->keyType);
	if (indexInfos.empty()) {
		return;
	}
	Node* interfaceDeclaration = nullptr;
	if (t->objectFlags & ObjectFlagsInterface) {
		interfaceDeclaration =
		    getDeclarationOfKind(t->symbol, Kind::InterfaceDeclaration);
	}
	Node* localCheckDeclaration = nullptr;
	if (declaration != nullptr &&
	    getParentOfSymbol(getSymbolOfDeclaration(declaration)) == t->symbol) {
		localCheckDeclaration = declaration;
	}
	for (IndexInfo* info : indexInfos) {
		if (info == checkInfo) {
			continue;
		}
		Node* localIndexDeclaration = nullptr;
		if (info->declaration != nullptr &&
		    getParentOfSymbol(getSymbolOfDeclaration(info->declaration)) ==
		        t->symbol) {
			localIndexDeclaration = info->declaration;
		}
		// We check only when (a) the check index signature is declared in the containing type, or (b) the applicable index
		// signature is declared in the containing type, or (c) the containing type is an interface and no base interface contains
		// both index signatures (i.e. the index signatures are declared in separate inherited interfaces).
		Node* errorNode = localCheckDeclaration != nullptr ? localCheckDeclaration
		                                                 : localIndexDeclaration;
		if (errorNode == nullptr && interfaceDeclaration != nullptr) {
			bool anyBaseHasBoth = false;
			for (Type* base : getBaseTypes(t)) {
				if (getIndexInfoOfType(base, checkInfo->keyType) != nullptr &&
				    getIndexTypeOfType(base, info->keyType) != nullptr) {
					anyBaseHasBoth = true;
					break;
				}
			}
			if (!anyBaseHasBoth) {
				errorNode = interfaceDeclaration;
			}
		}
		if (errorNode != nullptr &&
		    !isTypeAssignableTo(checkInfo->valueType, info->valueType)) {
			error(errorNode,
			      X_0_index_type_1_is_not_assignable_to_2_index_type_3,
			      {TypeToString(checkInfo->keyType),
			       TypeToString(checkInfo->valueType),
			       TypeToString(info->keyType),
			       TypeToString(info->valueType)});
		}
	}
}

// checker.go:4987
void Checker::checkClassOrInterfaceForDuplicateIndexSignatures(Node* node) {
	// Only check the type once
	auto* links = declaredTypeLinks.Get(getSymbolOfDeclaration(node));
	if (!links->indexSignaturesChecked) {
		links->indexSignaturesChecked = true;
		checkTypeForDuplicateIndexSignatures(node);
	}
}

// checker.go:4995
void Checker::checkTypeForDuplicateIndexSignatures(Node* node) {
	// TypeScript 1.0 spec (April 2014)
	// 3.7.4: An object type can contain at most one string index signature and one numeric index signature.
	// 8.5: A class declaration can have at most one string index member declaration and one numeric index member declaration
	Symbol* indexSymbol = getIndexSymbol(getSymbolOfDeclaration(node));
	if (indexSymbol == nullptr || indexSymbol->declarations.size() <= 1) {
		return;
	}
	std::unordered_map<Type*, std::vector<Node*>> indexSignatureMap;
	for (Node* declaration : indexSymbol->declarations) {
		if (isIndexSignatureDeclaration(declaration)) {
			auto parameters = declaration->parameters();
			if (parameters.size() == 1 && parameters[0]->type() != nullptr) {
				for (Type* t : distributed(
				         getTypeFromTypeNode(parameters[0]->type()))) {
					indexSignatureMap[t].push_back(declaration);
				}
			}
		}
		// Do nothing for late-bound index signatures: allow these to duplicate one another and explicit indexes
	}
	for (auto& [t, declarations] : indexSignatureMap) {
		if (declarations.size() > 1) {
			for (Node* declaration : declarations) {
				error(declaration, Duplicate_index_signature_for_type_0,
				      {TypeToString(t)});
			}
		}
	}
}

// checker.go:5024
void Checker::checkPropertyInitialization(Node* node) {
	if (!strictNullChecks || !strictPropertyInitialization ||
	    (node->flags & NodeFlagsAmbient)) {
		return;
	}
	Node* constructor = findConstructorDeclaration(node);
	for (Node* member : node->members()) {
		if (member->modifierFlags() & ModifierFlagsAmbient) {
			continue;
		}
		if (!tsc::isStatic(member) && isPropertyWithoutInitializer(member)) {
			Node* propName = member->name();
			if (isIdentifier(propName) || isPrivateIdentifier(propName) ||
			    isComputedPropertyName(propName)) {
				Type* t = getTypeOfSymbol(getSymbolOfDeclaration(member));
				if (!((t->flags & TypeFlagsAnyOrUnknown) ||
				      containsUndefinedType(t))) {
					if (constructor == nullptr ||
					    !isPropertyInitializedInConstructor(propName, t,
					                                        constructor)) {
						error(member->name(),
						      Property_0_has_no_initializer_and_is_not_definitely_assigned_in_the_constructor,
						      {declarationNameToString(propName)});
					}
				}
			}
		}
	}
}

// checker.go:5047
bool Checker::isPropertyWithoutInitializer(Node* node) {
	return isPropertyDeclaration(node) && !hasAbstractModifier(node) &&
	       !isExclamationToken(node->postfixToken()) &&
	       node->initializer() == nullptr;
}

// checker.go:5051
bool Checker::isPropertyInitializedInStaticBlocks(Node* propName, Type* propType,
                                                  const std::vector<Node*>& staticBlocks,
                                                  int32_t startPos, int32_t endPos) {
	for (Node* staticBlock : staticBlocks) {
		// static block must be within the provided range as they are evaluated in document order (unlike constructors)
		if (staticBlock->pos() >= startPos && staticBlock->pos() <= endPos) {
			Node* reference = factory.newPropertyAccessExpression(
			    factory.newKeywordExpression(Kind::ThisKeyword), nullptr,
			    propName, NodeFlagsNone);
			reference->expression()->parent = reference;
			reference->parent = staticBlock;
			*reference->flowNodeData().flowNode =
			    staticBlock->as<ClassStaticBlockDeclaration>()->ReturnFlowNode;
			Type* flowType = getFlowTypeOfReferenceEx(
			    reference, propType, getOptionalType(propType, false), nullptr,
			    nullptr);
			if (!containsUndefinedType(flowType)) {
				return true;
			}
		}
	}
	return false;
}

// checker.go:5068
bool Checker::isPropertyInitializedInConstructor(Node* propName, Type* propType,
                                                 Node* constructor) {
	Node* reference;
	if (isComputedPropertyName(propName)) {
		reference = factory.newElementAccessExpression(
		    factory.newKeywordExpression(Kind::ThisKeyword), nullptr,
		    propName->expression(), NodeFlagsNone);
	} else {
		reference = factory.newPropertyAccessExpression(
		    factory.newKeywordExpression(Kind::ThisKeyword), nullptr,
		    propName, NodeFlagsNone);
	}
	reference->expression()->parent = reference;
	reference->parent = constructor;
	*reference->flowNodeData().flowNode =
	    constructor->as<ConstructorDeclaration>()->ReturnFlowNode;
	Type* flowType = getFlowTypeOfReferenceEx(
	    reference, propType, getOptionalType(propType, false), nullptr, nullptr);
	return !containsUndefinedType(flowType);
}

// ---------------------------------------------------------------------------
// Dep stubs — callees owned by other slices (one TSC_UNREACHABLE body each;
// deleted by the owning slice's merge)
// ---------------------------------------------------------------------------

// owner: expressions slice (checker.go:8090-14185)
Type* Checker::checkTruthinessExpression(Node* node, CheckMode checkMode) {
	TSC_UNREACHABLE("checkTruthinessExpression — expressions slice");
}
void Checker::checkReferenceExpression(Node* node,
                                       const DiagnosticMessage* invalidReferenceType,
                                       const DiagnosticMessage* constantName) {
	TSC_UNREACHABLE("checkReferenceExpression — expressions slice");
}
void Checker::checkDestructuringAssignment(Node* node, Type* sourceType,
                                           CheckMode checkMode, bool checkResolvedType) {
	TSC_UNREACHABLE("checkDestructuringAssignment — expressions slice");
}
bool Checker::isTypeEqualityComparableTo(Type* source, Type* target) {
	TSC_UNREACHABLE("isTypeEqualityComparableTo — expressions slice");
}
Node* Checker::getEffectiveCheckNode(Node* node) {
	TSC_UNREACHABLE("getEffectiveCheckNode — expressions slice");
}
std::string Checker::getTypeNameForErrorDisplay(Type* t) {
	TSC_UNREACHABLE("getTypeNameForErrorDisplay — expressions slice");
}

// owner: varchecks slice (checker.go:5929-6264)
void Checker::checkVariableDeclarationList(Node* node) {
	TSC_UNREACHABLE("checkVariableDeclarationList — varchecks slice");
}

// owner: iterations/decltypes slice (checker.go:18014)
Type* Checker::checkRightHandSideOfForOf(Node* node) {
	TSC_UNREACHABLE("checkRightHandSideOfForOf — decltypes slice");
}

// owner: relater.go
bool Checker::checkTypeAssignableToAndOptionallyElaborate(
    Type* source, Type* target, Node* errorNode, Node* expr,
    const DiagnosticMessage* headMessage, std::vector<Diagnostic*>* diagnosticOutput) {
	TSC_UNREACHABLE(
	    "checkTypeAssignableToAndOptionallyElaborate — relater slice");
}
bool Checker::checkTypeComparableTo(Type* source, Type* target, Node* errorNode,
                                    const DiagnosticMessage* headMessage) {
	TSC_UNREACHABLE("checkTypeComparableTo — relater slice");
}
bool Checker::isTypeAssignableToKind(Type* source, TypeFlags kind) {
	TSC_UNREACHABLE("isTypeAssignableToKind — typeops slice");
}

// owner: members slice (checker.go:19186-20143 + 20987-22284)
std::vector<Symbol*> Checker::getPropertiesOfObjectType(Type* t) {
	TSC_UNREACHABLE("getPropertiesOfObjectType — members slice");
}
Symbol* Checker::getPropertyOfObjectType(Type* t, const std::string& name) {
	TSC_UNREACHABLE("getPropertyOfObjectType — members slice");
}
std::vector<Signature*> Checker::getConstructorsForTypeArguments(
    Type* t, const std::vector<Node*>& typeArgumentNodes, Node* location) {
	TSC_UNREACHABLE("getConstructorsForTypeArguments — members slice");
}
std::vector<Signature*> Checker::getInstantiatedConstructorsForTypeArguments(
    Type* t, const std::vector<Node*>& typeArgumentNodes, Node* location) {
	TSC_UNREACHABLE(
	    "getInstantiatedConstructorsForTypeArguments — members slice");
}
bool Checker::isValidBaseType(Type* t) {
	TSC_UNREACHABLE("isValidBaseType — members slice");
}
Type* Checker::getIndexTypeOfType(Type* t, Type* keyType) {
	TSC_UNREACHABLE("getIndexTypeOfType — members slice");
}
Symbol* Checker::getIndexSymbol(Symbol* symbol) {
	TSC_UNREACHABLE("getIndexSymbol — members slice");
}
Symbol* Checker::getTargetSymbol(Symbol* s) {
	TSC_UNREACHABLE("getTargetSymbol — members slice");
}
ConstructorAccessibilityError* Checker::getConstructorAccessibilityError(
    Node* node, const std::vector<Signature*>& signatures,
    ModifierFlags modifiers) {
	TSC_UNREACHABLE("getConstructorAccessibilityError — members slice");
}


// owner: decltypes slice (checker.go:16720-19097)
Type* Checker::getNonMissingTypeOfSymbol(Symbol* symbol) {
	TSC_UNREACHABLE("getNonMissingTypeOfSymbol — decltypes slice");
}
Type* Checker::getNonNullableTypeIfNeeded(Type* t) {
	TSC_UNREACHABLE("getNonNullableTypeIfNeeded — decltypes slice");
}
bool Checker::isMixinConstructorType(Type* t) {
	TSC_UNREACHABLE("isMixinConstructorType — decltypes slice");
}
Type* Checker::getLiteralTypeFromProperty(Symbol* prop, TypeFlags include,
                                          bool includeNonPublic) {
	TSC_UNREACHABLE("getLiteralTypeFromProperty — decltypes slice");
}

// owner: typeops slice (checker.go:26020-28654)
Type* Checker::getExtractStringType(Type* t) {
	TSC_UNREACHABLE("getExtractStringType — typeops slice");
}

// owner: contextual slice (checker.go:29385-32069)
Type* Checker::getAwaitedTypeOfPromise(Type* t) {
	TSC_UNREACHABLE("getAwaitedTypeOfPromise — contextual slice");
}

// owner: services tail (checker.go:32070-32660)
Symbol* Checker::getSymbolAtLocation(Node* node, bool ignoreErrors) {
	TSC_UNREACHABLE("getSymbolAtLocation — services tail");
}
std::vector<IndexInfo*> Checker::getApplicableIndexInfos(Type* t,
                                                         Type* keyType) {
	TSC_UNREACHABLE("getApplicableIndexInfos — services tail");
}

// owner: modulechecks slice (checker.go:5082-5928 region; fn at 7082 is
// in aliasunused range)
void Checker::checkExportsOnMergedDeclarations(Node* node) {
	TSC_UNREACHABLE("checkExportsOnMergedDeclarations — aliasunused slice");
}

// owner: contextual slice (checker.go:31463)
bool Checker::hasTypeFacts(Type* t, TypeFacts mask) {
	TSC_UNREACHABLE("hasTypeFacts — contextual slice");
}

// owner: decltypes slice (checker.go:17277)
Type* Checker::getBaseConstructorTypeOfClass(Type* t) {
	TSC_UNREACHABLE("getBaseConstructorTypeOfClass — decltypes slice");
}

} // namespace tsc::checker
