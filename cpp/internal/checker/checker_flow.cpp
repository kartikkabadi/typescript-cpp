// checker_flow.cpp — flow.go:1-2761 "flow" slice.
// Control-flow type narrowing: flow graph traversal, narrowing predicates,
// evolving array types, exhaustiveness analysis, assignment/definite-use
// tracking. Ported faithfully from tsc/internal/checker/flow.go in file
// order; cross-slice callees are TSC_UNREACHABLE stubs at the bottom.

#include "internal/ast/ast.h"
#include "internal/binder/binder.h"
#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/checker/types.h"
#include "internal/core/types.h"
#include "internal/evaluator/evaluator.h"
#include "internal/scanner/scanner.h"
#include "internal/tracing/tracing.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>

namespace tsc::checker {

namespace flow_detail {
std::pair<std::string, bool> tryGetNameFromType(Type* t);
}

// Free functions in checker.cpp (extern linkage).

bool everyType(Type* t, const std::function<bool(Type*)>& f);
bool someType(Type* t, const std::function<bool(Type*)>& f);
bool containsType(const std::vector<Type*>& types, Type* t);
std::string entityNameToString(Node* name);
bool isFreshLiteralType(Type* t);

// getFlowNodeOfNode (flow.go:69) — extern linkage; also called from
// checker_signatures.cpp (its old copy there was removed in favor of this one).
FlowNode* getFlowNodeOfNode(Node* node) {
	FlowNode** flowNodeData = node->flowNodeData().flowNode;
	if (flowNodeData != nullptr) {
		return *flowNodeData;
	}
	return nullptr;
}

namespace {

// keyBuilder (checker.go) — verbatim copy of the per-TU helper in checker.cpp.
struct keyBuilder {
	std::string buf;

	void writeByte(uint8_t c) { buf.push_back(static_cast<char>(c)); }
	void writeString(const std::string& s) { buf += s; }
	void writeUint32(uint32_t v) {
		char b[4];
		std::memcpy(b, &v, 4);
		buf.append(b, 4);
	}
	void writeUint64(uint64_t v) {
		char b[8];
		std::memcpy(b, &v, 8);
		buf.append(b, 8);
	}
	void writeInt(int v) { writeUint64(static_cast<uint64_t>(v)); }
	void writeSymbol(Symbol* s) { writeUint64(static_cast<uint64_t>(getSymbolId(s))); }
	void writeType(Type* t) { writeUint32(static_cast<uint32_t>(t->id)); }
	void writeTypes(const std::vector<Type*>& types) {
		writeInt(static_cast<int>(types.size()));
		for (Type* t : types) {
			writeType(t);
		}
	}
	void writeAlias(TypeAlias* alias) {
		if (alias != nullptr) {
			writeByte(1);
			writeSymbol(alias->symbol);
			writeTypes(alias->typeArguments);
		} else {
			writeByte(0);
		}
	}
	void writeNodeId(NodeId id) { writeUint64(static_cast<uint64_t>(id)); }
	void writeNode(Node* node) {
		if (node != nullptr) {
			writeNodeId(getNodeId(node));
		}
	}
	CacheKey hash() {
		CacheKey key;
		uint64_t v = 0;
		int shift = 0;
		for (char ch : buf) {
			v |= static_cast<uint64_t>(static_cast<uint8_t>(ch)) << (shift * 8);
			if (++shift == 8) {
				key.w.push_back(v);
				v = 0;
				shift = 0;
			}
		}
		if (shift != 0) {
			key.w.push_back(v);
		}
		key.w.push_back(buf.size());
		return key;
	}
};

// Forward declarations for the flow.go free functions and file-local helpers
// so every definition can sit at its exact Go file position below.



FlowList* getBranchLabelAntecedents(FlowNode* flow,
									const std::vector<FlowReduceLabelData*>& reduceLabels);
Node* getCandidateVariableDeclarationInitializer(Node* decl);
bool isEvolvingArrayTypeList(const std::vector<Type*>& types);
bool isCoercibleUnderDoubleEquals(Type* source, Type* target);
bool writeFlowCacheKey(Checker* c, keyBuilder* b, Node* node, Type* declaredType,
					   Type* initialType, Node* flowContainer);

// AssignmentKind — shared enum in checker.h

// Operator predicates (utilities.go:796-839)
bool isExponentiationOperator(Kind kind) {
	return kind == Kind::AsteriskAsteriskToken;
}

bool isMultiplicativeOperator(Kind kind) {
	return kind == Kind::AsteriskToken || kind == Kind::SlashToken ||
		   kind == Kind::PercentToken;
}

bool isMultiplicativeOperatorOrHigher(Kind kind) {
	return isExponentiationOperator(kind) || isMultiplicativeOperator(kind);
}

bool isAdditiveOperator(Kind kind) {
	return kind == Kind::PlusToken || kind == Kind::MinusToken;
}

bool isAdditiveOperatorOrHigher(Kind kind) {
	return isAdditiveOperator(kind) || isMultiplicativeOperatorOrHigher(kind);
}

bool isShiftOperator(Kind kind) {
	return kind == Kind::LessThanLessThanToken ||
		   kind == Kind::GreaterThanGreaterThanToken ||
		   kind == Kind::GreaterThanGreaterThanGreaterThanToken;
}

bool isShiftOperatorOrHigher(Kind kind) {
	return isShiftOperator(kind) || isAdditiveOperatorOrHigher(kind);
}

bool isRelationalOperator(Kind kind) {
	return kind == Kind::LessThanToken || kind == Kind::LessThanEqualsToken ||
		   kind == Kind::GreaterThanToken || kind == Kind::GreaterThanEqualsToken ||
		   kind == Kind::InstanceOfKeyword || kind == Kind::InKeyword;
}

bool isRelationalOperatorOrHigher(Kind kind) {
	return isRelationalOperator(kind) || isShiftOperatorOrHigher(kind);
}

bool isEqualityOperator(Kind kind) {
	return kind == Kind::EqualsEqualsToken ||
		   kind == Kind::EqualsEqualsEqualsToken ||
		   kind == Kind::ExclamationEqualsToken ||
		   kind == Kind::ExclamationEqualsEqualsToken;
}

[[maybe_unused]] bool isEqualityOperatorOrHigher(Kind kind) {
	return isEqualityOperator(kind) || isRelationalOperatorOrHigher(kind);
}

[[maybe_unused]] bool isBitwiseOperator(Kind kind) {
	return kind == Kind::AmpersandToken || kind == Kind::BarToken ||
		   kind == Kind::CaretToken;
}

bool isCompoundLikeAssignment(Node* assignment);

// isInCompoundLikeAssignment / isCompoundLikeAssignment (utilities.go:117-124)
bool isInCompoundLikeAssignment(Node* node) {
	Node* target = getAssignmentTarget(node);
	return target != nullptr && isAssignmentExpression(target, true) &&
		   isCompoundLikeAssignment(target);
}

bool isCompoundLikeAssignment(Node* assignment) {
	Node* right = skipParentheses(assignment->as<BinaryExpression>()->Right);
	return right->kind == Kind::BinaryExpression &&
		   isShiftOperatorOrHigher(right->as<BinaryExpression>()->OperatorToken->kind);
}

// getAssignmentTargetKind (utilities.go:89-106)
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
	}
	TSC_UNREACHABLE("Unhandled case in getAssignmentTargetKind");
}

// hasOnlyExpressionInitializer (utilities.go:264-270)
bool hasOnlyExpressionInitializer(Node* node) {
	switch (node->kind) {
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::BindingElement:
	case Kind::PropertyDeclaration:
	case Kind::PropertyAssignment:
	case Kind::EnumMember:
		return true;
	}
	return false;
}

// hasDotDotDotToken (utilities.go:272-281)
bool hasDotDotDotToken(Node* node) {
	switch (node->kind) {
	case Kind::Parameter:
		return node->as<ParameterDeclaration>()->DotDotDotToken != nullptr;
	case Kind::BindingElement:
		return node->as<BindingElement>()->DotDotDotToken != nullptr;
	case Kind::NamedTupleMember:
		return node->as<NamedTupleMember>()->DotDotDotToken != nullptr;
	case Kind::JsxExpression:
		return node->as<JsxExpression>()->DotDotDotToken != nullptr;
	}
	return false;
}

// IsTypeAny (utilities.go:286-288)
bool IsTypeAny(Type* t) { return t != nullptr && (t->flags & TypeFlagsAny); }

// isEmptyArrayLiteral (utilities.go:4042) — canonical in ast.cpp

// isThisInTypeQuery — canonical def in ast.cpp
// isFunctionOrSourceFile (ast/utilities.go:527)
bool isFunctionOrSourceFile(Node* node) {
	return isFunctionLike(node) || isSourceFile(node);
}

// getSymbolNameForPrivateIdentifier (binder.go:374)
std::string getSymbolNameForPrivateIdentifier(Symbol* containingClassSymbol,
											  const std::string& description) {
	return std::string(1, kInternalSymbolNamePrefix) + "#" +
		   std::to_string(getSymbolId(containingClassSymbol)) + "@" + description;
}

// isNonNullAccess (utilities.go:1120-1122)
bool isNonNullAccess(Node* node) {
	return isAccessExpression(node) && isNonNullExpression(node->expression());
}

// getBindingElementPropertyName (utilities.go:1124-1126)
Node* getBindingElementPropertyName(Node* node) {
	return node->propertyNameOrName();
}

// isCallChain (utilities.go:1128-1130)
bool isCallChain(Node* node) {
	return isCallExpression(node) && (node->flags & NodeFlagsOptionalChain);
}

// isNeitherUnitTypeNorNever
bool isNeitherUnitTypeNorNever(Type* t) {
	return (t->flags & (TypeFlagsUnit | TypeFlagsNever)) == 0;
}

// Static helpers replicated per-TU (checker.cpp / utilities.go idiom).

std::string getStringLiteralValue(Type* t) {
	return std::get<std::string>(t->AsLiteralType()->value);
}

bool isUnitType(Type* t) {
	return (t->flags & TypeFlagsUnit) != 0;
}

bool isLiteralType(Type* t) {
	if (t->flags & TypeFlagsBoolean) {
		return true;
	}
	if (t->flags & TypeFlagsUnion) {
		if (t->flags & TypeFlagsEnumLiteral) {
			return true;
		}
		return everyType(t, isUnitType);
	}
	return isUnitType(t);
}

bool isTypeUsableAsPropertyName(Type* t) {
	return (t->flags & TypeFlagsStringOrNumberLiteralOrUnique) != 0;
}

// Gets the symbolic name for a member from its type.
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

// core.* equivalents used by this file.
template <class T, class F>
auto mapVec(const std::vector<T>& v, F&& f) -> std::vector<decltype(f(v[0]))> {
	std::vector<decltype(f(v[0]))> result;
	result.reserve(v.size());
	for (const T& x : v) {
		result.push_back(f(x));
	}
	return result;
}

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

template <class T>
T orElse(T a, T b) {
	return a != nullptr ? a : b;
}

template <class T>
void appendIfUnique(std::vector<T>& v, const T& item) {
	if (std::find(v.begin(), v.end(), item) == v.end()) {
		v.push_back(item);
	}
}

// typeofNEFacts (flow.go:533-540)
const std::unordered_map<std::string, TypeFacts> typeofNEFacts = {
	{"string", TypeFactsTypeofNEString},
	{"number", TypeFactsTypeofNENumber},
	{"bigint", TypeFactsTypeofNEBigInt},
	{"boolean", TypeFactsTypeofNEBoolean},
	{"symbol", TypeFactsTypeofNESymbol},
	{"undefined", TypeFactsNEUndefined},
	{"object", TypeFactsTypeofNEObject},
	{"function", TypeFactsTypeofNEFunction},
};

// nonDottedNameCacheKey (flow.go:1627-1631)
// Key for references that aren't rooted in a dotted name. Go uses the xxh3
// hash of "?"; we use a fixed single-word sentinel (CacheKey values produced
// by keyBuilder.hash() always carry >= 2 words).
const CacheKey nonDottedNameCacheKey{{0xffffffffffffff10ull}};

} // namespace

// === flow.go functions (in file order) ===

// newFlowType (flow.go:28-32)
FlowType Checker::newFlowType(Type* t, bool incomplete) {
	if (incomplete && (t->flags & TypeFlagsNever) != 0) {
		t = silentNeverType;
	}
	return FlowType{t, incomplete};
}

// getFlowState (flow.go:52-58)
FlowState* Checker::getFlowState() {
	FlowState* f = freeFlowState;
	if (f == nullptr) {
		f = new FlowState();
	}
	freeFlowState = f->next;
	return f;
}

// putFlowState (flow.go:60-65)
void Checker::putFlowState(FlowState* f) {
	// Go: *f = FlowState{reduceLabels: f.reduceLabels[:0], next: c.freeFlowState}
	// — the reduceLabels backing array is preserved across reuse.
	std::vector<FlowReduceLabelData*> keep = std::move(f->reduceLabels);
	keep.clear();
	*f = FlowState{};
	f->reduceLabels = std::move(keep);
	f->next = freeFlowState;
	freeFlowState = f;
}

// getFlowTypeOfReference (flow.go:75-77)
Type* Checker::getFlowTypeOfReference(Node* reference, Type* declaredType) {
	return getFlowTypeOfReferenceEx(reference, declaredType, declaredType, nullptr, nullptr);
}

// getFlowTypeOfReferenceEx (flow.go:79-116)
Type* Checker::getFlowTypeOfReferenceEx(Node* reference, Type* declaredType,
										Type* initialType, Node* flowContainer,
										FlowNode* flowNode) {
	if (flowAnalysisDisabled) {
		return errorType;
	}
	if (flowNode == nullptr) {
		flowNode = getFlowNodeOfNode(reference);
		if (flowNode == nullptr) {
			return declaredType;
		}
	}
	FlowState* f = getFlowState();
	f->reference = reference;
	f->declaredType = declaredType;
	f->initialType = initialType != nullptr ? initialType : declaredType;
	f->flowContainer = flowContainer;
	f->sharedFlowStart = static_cast<int>(sharedFlows.size());
	flowInvocationCount++;
	Type* evolvedType = getTypeAtFlowNode(f, flowNode).t;
	sharedFlows.resize(f->sharedFlowStart);
	putFlowState(f);
	// When the reference is 'x' in an 'x.length', 'x.push(value)', 'x.unshift(value)' or x[n] = value' operation,
	// we give type 'any[]' to 'x' instead of using the type determined by control flow analysis such that operations
	// on empty arrays are possible without implicit any errors and new element types can be inferred without
	// type mismatch errors.
	Type* resultType;
	if ((evolvedType->objectFlags & ObjectFlagsEvolvingArray) != 0 &&
		isEvolvingArrayOperationTarget(reference)) {
		resultType = autoArrayType;
	} else {
		resultType = finalizeEvolvingArrayType(evolvedType);
	}
	if (resultType == unreachableNeverType ||
		(reference->parent != nullptr && isNonNullExpression(reference->parent) &&
		 (resultType->flags & TypeFlagsNever) == 0 &&
		 (getTypeWithFacts(resultType, TypeFactsNEUndefinedOrNull)->flags & TypeFlagsNever) != 0)) {
		return declaredType;
	}
	return resultType;
}

// getTypeAtFlowNode (flow.go:118-199)
FlowType Checker::getTypeAtFlowNode(FlowState* f, FlowNode* flow) {
	if (f->depth == 2000) {
		// We have made 2000 recursive invocations. To avoid overflowing the call stack we report an error
		// and disable further control flow analysis in the containing function or module body.
		if (tracer != nullptr) {
			tracer->Instant(tsc::tracing::PhaseCheckTypes, "getTypeAtFlowNode_DepthLimit",
							{{"depth", f->depth}});
		}
		flowAnalysisDisabled = true;
		reportFlowControlError(f->reference);
		return FlowType{errorType};
	}
	f->depth++;
	FlowNode* sharedFlow = nullptr;
	for (;;) {
		FlowFlags flags = flow->flags;
		if ((flags & FlowFlagsShared) != 0) {
			// We cache results of flow type resolution for shared nodes that were previously visited in
			// the same getFlowTypeOfReference invocation. A node is considered shared when it is the
			// antecedent of more than one node.
			for (size_t i = f->sharedFlowStart; i < sharedFlows.size(); i++) {
				if (sharedFlows[i].flow == flow) {
					f->depth--;
					return sharedFlows[i].flowType;
				}
			}
			sharedFlow = flow;
		}
		FlowType t;
		// Go `switch {}` — if/else chain preserving case order.
		if ((flags & FlowFlagsAssignment) != 0) {
			t = getTypeAtFlowAssignment(f, flow);
			if (t.isNil()) {
				flow = flow->antecedent;
				continue;
			}
		} else if ((flags & FlowFlagsCall) != 0) {
			t = getTypeAtFlowCall(f, flow);
			if (t.isNil()) {
				flow = flow->antecedent;
				continue;
			}
		} else if ((flags & FlowFlagsCondition) != 0) {
			t = getTypeAtFlowCondition(f, flow);
		} else if ((flags & FlowFlagsSwitchClause) != 0) {
			t = getTypeAtSwitchClause(f, flow);
		} else if ((flags & FlowFlagsBranchLabel) != 0) {
			FlowList* antecedents = getBranchLabelAntecedents(flow, f->reduceLabels);
			if (antecedents->next == nullptr) {
				flow = antecedents->flow;
				continue;
			}
			t = getTypeAtFlowBranchLabel(f, flow, antecedents);
		} else if ((flags & FlowFlagsLoopLabel) != 0) {
			if (flow->antecedents->next == nullptr) {
				flow = flow->antecedents->flow;
				continue;
			}
			t = getTypeAtFlowLoopLabel(f, flow);
		} else if ((flags & FlowFlagsArrayMutation) != 0) {
			t = getTypeAtFlowArrayMutation(f, flow);
			if (t.isNil()) {
				flow = flow->antecedent;
				continue;
			}
		} else if ((flags & FlowFlagsReduceLabel) != 0) {
			f->reduceLabels.push_back(flow->node->as<FlowReduceLabelData>());
			t = getTypeAtFlowNode(f, flow->antecedent);
			f->reduceLabels.pop_back();
		} else if ((flags & FlowFlagsStart) != 0) {
			// Check if we should continue with the control flow of the containing function.
			Node* container = flow->node;
			if (container != nullptr && container != f->flowContainer &&
				!isPropertyAccessExpression(f->reference) &&
				!isElementAccessExpression(f->reference) &&
				!(f->reference->kind == Kind::ThisKeyword && !isArrowFunction(container))) {
				flow = *container->flowNodeData().flowNode;
				continue;
			}
			// At the top of the flow we have the initial type.
			t = FlowType{f->initialType};
		} else {
			// Unreachable code errors are reported in the binding phase. Here we
			// simply return the non-auto declared type to reduce follow-on errors.
			t = FlowType{convertAutoToAny(f->declaredType)};
		}
		if (sharedFlow != nullptr) {
			// Record visited node and the associated type in the cache.
			sharedFlows.push_back(SharedFlow{sharedFlow, t});
		}
		f->depth--;
		return t;
	}
}

// getBranchLabelAntecedents (flow.go:201-211)
namespace {
FlowList* getBranchLabelAntecedents(
	FlowNode* flow, const std::vector<FlowReduceLabelData*>& reduceLabels) {
	size_t i = reduceLabels.size();
	while (i != 0) {
		i--;
		FlowReduceLabelData* data = reduceLabels[i];
		if (data->target == flow) {
			return data->antecedents;
		}
	}
	return flow->antecedents;
}
} // namespace

// getTypeAtFlowAssignment (flow.go:213-255)
FlowType Checker::getTypeAtFlowAssignment(FlowState* f, FlowNode* flow) {
	Node* node = flow->node;
	// Assignments only narrow the computed type if the declared type is a union type. Thus, we
	// only need to evaluate the assigned type if the declared type is a union type.
	if (isMatchingReference(f->reference, node)) {
		if (!isReachableFlowNode(flow)) {
			return FlowType{unreachableNeverType};
		}
		if (getAssignmentTargetKind(node) == AssignmentKind::Compound) {
			FlowType flowType = getTypeAtFlowNode(f, flow->antecedent);
			return newFlowType(getBaseTypeOfLiteralType(flowType.t), flowType.incomplete);
		}
		if (f->declaredType == autoType || f->declaredType == autoArrayType) {
			if (isEmptyArrayAssignment(node)) {
				return FlowType{getEvolvingArrayType(neverType)};
			}
			Type* assignedType = getWidenedLiteralType(getInitialOrAssignedType(f, flow));
			if (isTypeAssignableTo(assignedType, f->declaredType)) {
				return FlowType{assignedType};
			}
			return FlowType{anyArrayType};
		}
		Type* t = f->declaredType;
		if (isInCompoundLikeAssignment(node)) {
			t = getBaseTypeOfLiteralType(t);
		}
		if ((t->flags & TypeFlagsUnion) != 0) {
			return FlowType{getAssignmentReducedType(t, getInitialOrAssignedType(f, flow))};
		}
		return FlowType{t};
	}
	// We didn't have a direct match. However, if the reference is a dotted name, this
	// may be an assignment to a left hand part of the reference. For example, for a
	// reference 'x.y.z', we may be at an assignment to 'x.y' or 'x'. In that case,
	// return the declared type.
	if (containsMatchingReference(f->reference, node)) {
		if (!isReachableFlowNode(flow)) {
			return FlowType{unreachableNeverType};
		}
		// A matching dotted name might also be an expando property on a function *expression*,
		// in which case we continue control flow analysis back to the function's declaration
		if (isVariableDeclaration(node) && (isInJSFile(node) || isVarConstLike(node))) {
			if (Node* init = node->initializer();
				init != nullptr && isFunctionExpressionOrArrowFunction(init)) {
				return getTypeAtFlowNode(f, flow->antecedent);
			}
		}
		return FlowType{f->declaredType};
	}
	// for (const _ in ref) acts as a nonnull on ref
	if (isVariableDeclaration(node) && isForInStatement(node->parent->parent) &&
		(isMatchingReference(f->reference, node->parent->parent->expression()) ||
		 optionalChainContainsReference(node->parent->parent->expression(), f->reference))) {
		return FlowType{getNonNullableTypeIfNeeded(
			finalizeEvolvingArrayType(getTypeAtFlowNode(f, flow->antecedent).t))};
	}
	// Assignment doesn't affect reference
	return FlowType{};
}

// getInitialOrAssignedType (flow.go:257-262)
Type* Checker::getInitialOrAssignedType(FlowState* f, FlowNode* flow) {
	if (isVariableDeclaration(flow->node) || isBindingElement(flow->node)) {
		return getNarrowableTypeForReference(getInitialType(flow->node), f->reference,
											 CheckModeNormal);
	}
	return getNarrowableTypeForReference(getAssignedType(flow->node), f->reference,
									   CheckModeNormal);
}

// isEmptyArrayAssignment (flow.go:264-267)
bool Checker::isEmptyArrayAssignment(Node* node) {
	return (isVariableDeclaration(node) && node->initializer() != nullptr &&
			isEmptyArrayLiteral(node->initializer())) ||
		   (!isBindingElement(node) && isBinaryExpression(node->parent) &&
			isEmptyArrayLiteral(node->parent->as<BinaryExpression>()->Right));
}

// getTypeAtFlowCall (flow.go:269-294)
FlowType Checker::getTypeAtFlowCall(FlowState* f, FlowNode* flow) {
	Signature* signature = getEffectsSignature(flow->node);
	if (signature != nullptr) {
		TypePredicate* predicate = getTypePredicateOfSignature(signature);
		if (predicate != nullptr &&
			(predicate->kind == TypePredicateKind::AssertsThis ||
			 predicate->kind == TypePredicateKind::AssertsIdentifier)) {
			FlowType flowType = getTypeAtFlowNode(f, flow->antecedent);
			Type* t = finalizeEvolvingArrayType(flowType.t);
			Type* narrowedType;
			// Go `switch {}` — if/else chain preserving case order.
			if (predicate->t != nullptr) {
				narrowedType =
					narrowTypeByTypePredicate(f, t, predicate, flow->node, true /*assumeTrue*/);
			} else if (predicate->kind == TypePredicateKind::AssertsIdentifier &&
					   predicate->parameterIndex >= 0 &&
					   predicate->parameterIndex <
						   static_cast<int32_t>(flow->node->arguments().size())) {
				narrowedType = narrowTypeByAssertion(
					f, t, flow->node->arguments()[predicate->parameterIndex]);
			} else {
				narrowedType = t;
			}
			if (narrowedType == t) {
				return flowType;
			}
			return newFlowType(narrowedType, flowType.incomplete);
		}
		if ((getReturnTypeOfSignature(signature)->flags & TypeFlagsNever) != 0) {
			return FlowType{unreachableNeverType};
		}
	}
	return FlowType{};
}

// narrowTypeByTypePredicate (flow.go:296-322)
Type* Checker::narrowTypeByTypePredicate(FlowState* f, Type* t, TypePredicate* predicate,
										 Node* callExpression, bool assumeTrue) {
	// Don't narrow from 'any' if the predicate type is exactly 'Object' or 'Function'
	if (predicate->t != nullptr &&
		!(IsTypeAny(t) &&
		  (predicate->t == globalObjectType || predicate->t == globalFunctionType))) {
		Node* predicateArgument = getTypePredicateArgument(predicate, callExpression);
		if (predicateArgument != nullptr) {
			if (isMatchingReference(f->reference, predicateArgument)) {
				return getNarrowedType(t, predicate->t, assumeTrue, false /*checkDerived*/);
			}
			if (strictNullChecks && optionalChainContainsReference(predicateArgument, f->reference) &&
				((assumeTrue && !hasTypeFacts(predicate->t, TypeFactsEQUndefined)) ||
				 (!assumeTrue && everyType(predicate->t, [this](Type* t2) {
					 return IsNullableType(t2);
				 })))) {
				t = getAdjustedTypeWithFacts(t, TypeFactsNEUndefinedOrNull);
			}
			Node* access = getDiscriminantPropertyAccess(f, predicateArgument, t);
			if (access != nullptr) {
				return narrowTypeByDiscriminant(t, access, [this, predicate, assumeTrue](Type* t2) {
					return getNarrowedType(t2, predicate->t, assumeTrue, false /*checkDerived*/);
				});
			}
		}
	}
	return t;
}

// narrowTypeByAssertion (flow.go:324-341)
Type* Checker::narrowTypeByAssertion(FlowState* f, Type* t, Node* expr) {
	Node* node = skipParentheses(expr);
	if (node->kind == Kind::FalseKeyword) {
		return unreachableNeverType;
	}
	if (node->kind == Kind::BinaryExpression) {
		if (node->as<BinaryExpression>()->OperatorToken->kind == Kind::AmpersandAmpersandToken) {
			return narrowTypeByAssertion(
				f,
				narrowTypeByAssertion(f, t, node->as<BinaryExpression>()->Left),
				node->as<BinaryExpression>()->Right);
		}
		if (node->as<BinaryExpression>()->OperatorToken->kind == Kind::BarBarToken) {
			return getUnionType({narrowTypeByAssertion(f, t, node->as<BinaryExpression>()->Left),
								 narrowTypeByAssertion(f, t, node->as<BinaryExpression>()->Right)});
		}
	}
	return narrowType(f, t, node, true /*assumeTrue*/);
}

// getTypeAtFlowCondition (flow.go:343-365)
FlowType Checker::getTypeAtFlowCondition(FlowState* f, FlowNode* flow) {
	FlowType flowType = getTypeAtFlowNode(f, flow->antecedent);
	if ((flowType.t->flags & TypeFlagsNever) != 0) {
		return flowType;
	}
	// If we have an antecedent type (meaning we're reachable in some way), we first
	// attempt to narrow the antecedent type. If that produces the never type, and if
	// the antecedent type is incomplete (i.e. a transient type in a loop), then we
	// take the type guard as an indication that control *could* reach here once we
	// have the complete type. We proceed by switching to the silent never type which
	// doesn't report errors when operators are applied to it. Note that this is the
	// *only* place a silent never type is ever generated.
	bool assumeTrue = (flow->flags & FlowFlagsTrueCondition) != 0;
	Type* nonEvolvingType = finalizeEvolvingArrayType(flowType.t);
	Type* narrowedType = narrowType(f, nonEvolvingType, flow->node, assumeTrue);
	if (narrowedType == nonEvolvingType) {
		return flowType;
	}
	return newFlowType(narrowedType, flowType.incomplete);
}

// Narrow the given type based on the given expression having the assumed boolean value. The returned type
// will be a subtype or the same type as the argument.
// narrowType (flow.go:369-406)
Type* Checker::narrowType(FlowState* f, Type* t, Node* expr, bool assumeTrue) {
	// for `a?.b`, we emulate a synthetic `a !== null && a !== undefined` condition for `a`
	if (isExpressionOfOptionalChainRoot(expr) ||
		(isBinaryExpression(expr->parent) &&
		 (expr->parent->as<BinaryExpression>()->OperatorToken->kind ==
			  Kind::QuestionQuestionToken ||
		  expr->parent->as<BinaryExpression>()->OperatorToken->kind ==
			  Kind::QuestionQuestionEqualsToken) &&
		 expr->parent->as<BinaryExpression>()->Left == expr)) {
		return narrowTypeByOptionality(f, t, expr, assumeTrue);
	}
	switch (expr->kind) {
	case Kind::Identifier:
		// When narrowing a reference to a const variable, non-assigned parameter, or readonly property, we inline
		// up to five levels of aliased conditional expressions that are themselves declared as const variables.
		if (!isMatchingReference(f->reference, expr) && inlineLevel < 5) {
			Symbol* symbol = getResolvedSymbol(expr);
			if (isConstantVariable(symbol)) {
				Node* declaration = symbol->data->valueDeclaration;
				if (declaration != nullptr && isVariableDeclaration(declaration) &&
					declaration->type() == nullptr && declaration->initializer() != nullptr &&
					isConstantReference(f->reference)) {
					inlineLevel++;
					Type* result = narrowType(f, t, declaration->initializer(), assumeTrue);
					inlineLevel--;
					return result;
				}
			}
		}
		[[fallthrough]];
	case Kind::ThisKeyword:
	case Kind::SuperKeyword:
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
		return narrowTypeByTruthiness(f, t, expr, assumeTrue);
	case Kind::CallExpression:
		return narrowTypeByCallExpression(f, t, expr, assumeTrue);
	case Kind::ParenthesizedExpression:
	case Kind::NonNullExpression:
	case Kind::SatisfiesExpression:
		return narrowType(f, t, expr->expression(), assumeTrue);
	case Kind::BinaryExpression:
		return narrowTypeByBinaryExpression(f, t, expr->as<BinaryExpression>(), assumeTrue);
	case Kind::PrefixUnaryExpression:
		if (expr->as<PrefixUnaryExpression>()->Operator == Kind::ExclamationToken) {
			return narrowType(f, t, expr->as<PrefixUnaryExpression>()->Operand, !assumeTrue);
		}
		break;
	}
	return t;
}

// narrowTypeByOptionality (flow.go:408-419)
Type* Checker::narrowTypeByOptionality(FlowState* f, Type* t, Node* expr, bool assumePresent) {
	if (isMatchingReference(f->reference, expr)) {
		return getAdjustedTypeWithFacts(t, assumePresent ? TypeFactsNEUndefinedOrNull
													   : TypeFactsEQUndefinedOrNull);
	}
	Node* access = getDiscriminantPropertyAccess(f, expr, t);
	if (access != nullptr) {
		return narrowTypeByDiscriminant(t, access, [this, assumePresent](Type* t2) {
			return getTypeWithFacts(t2, assumePresent ? TypeFactsNEUndefinedOrNull
													 : TypeFactsEQUndefinedOrNull);
		});
	}
	return t;
}

// narrowTypeByTruthiness (flow.go:421-434)
Type* Checker::narrowTypeByTruthiness(FlowState* f, Type* t, Node* expr, bool assumeTrue) {
	if (isMatchingReference(f->reference, expr)) {
		return getAdjustedTypeWithFacts(t, assumeTrue ? TypeFactsTruthy : TypeFactsFalsy);
	}
	if (strictNullChecks && assumeTrue && optionalChainContainsReference(expr, f->reference)) {
		t = getAdjustedTypeWithFacts(t, TypeFactsNEUndefinedOrNull);
	}
	Node* access = getDiscriminantPropertyAccess(f, expr, t);
	if (access != nullptr) {
		return narrowTypeByDiscriminant(t, access, [this, assumeTrue](Type* t2) {
			return getTypeWithFacts(t2, assumeTrue ? TypeFactsTruthy : TypeFactsFalsy);
		});
	}
	return t;
}

// narrowTypeByCallExpression (flow.go:436-459)
Type* Checker::narrowTypeByCallExpression(FlowState* f, Type* t, Node* callExpression,
										  bool assumeTrue) {
	if (hasMatchingArgument(callExpression, f->reference)) {
		TypePredicate* predicate = nullptr;
		if (assumeTrue || !isCallChain(callExpression)) {
			Signature* signature = getEffectsSignature(callExpression);
			if (signature != nullptr) {
				predicate = getTypePredicateOfSignature(signature);
			}
		}
		if (predicate != nullptr && (predicate->kind == TypePredicateKind::This ||
									 predicate->kind == TypePredicateKind::Identifier)) {
			return narrowTypeByTypePredicate(f, t, predicate, callExpression, assumeTrue);
		}
	}
	if (containsMissingType(t) && isAccessExpression(f->reference) &&
		isPropertyAccessExpression(callExpression->expression())) {
		Node* callAccess = callExpression->expression();
		if (isMatchingReference(f->reference->expression(),
								getReferenceCandidate(callAccess->expression())) &&
			isIdentifier(callAccess->name()) && callAccess->name()->text() == "hasOwnProperty" &&
			callExpression->arguments().size() == 1) {
			Node* argument = callExpression->arguments()[0];
			auto [accessedName, ok] = getAccessedPropertyName(f->reference);
			if (ok && isStringLiteralLike(argument) && accessedName == argument->text()) {
				return getTypeWithFacts(t, assumeTrue ? TypeFactsNEUndefined : TypeFactsEQUndefined);
			}
		}
	}
	return t;
}

// narrowTypeByBinaryExpression (flow.go:461-534)
Type* Checker::narrowTypeByBinaryExpression(FlowState* f, Type* t, BinaryExpression* expr,
											bool assumeTrue) {
	switch (expr->OperatorToken->kind) {
	case Kind::EqualsToken:
	case Kind::BarBarEqualsToken:
	case Kind::AmpersandAmpersandEqualsToken:
	case Kind::QuestionQuestionEqualsToken:
		return narrowTypeByTruthiness(f, narrowType(f, t, expr->Right, assumeTrue), expr->Left,
									  assumeTrue);
	case Kind::EqualsEqualsToken:
	case Kind::ExclamationEqualsToken:
	case Kind::EqualsEqualsEqualsToken:
	case Kind::ExclamationEqualsEqualsToken: {
		Kind operatorKind = expr->OperatorToken->kind;
		Node* left = getReferenceCandidate(expr->Left);
		Node* right = getReferenceCandidate(expr->Right);
		if (left->kind == Kind::TypeOfExpression && isStringLiteralLike(right)) {
			return narrowTypeByTypeof(f, t, left->as<TypeOfExpression>(), operatorKind, right,
									  assumeTrue);
		}
		if (right->kind == Kind::TypeOfExpression && isStringLiteralLike(left)) {
			return narrowTypeByTypeof(f, t, right->as<TypeOfExpression>(), operatorKind, left,
									  assumeTrue);
		}
		if (isMatchingReference(f->reference, left)) {
			return narrowTypeByEquality(t, operatorKind, right, assumeTrue);
		}
		if (isMatchingReference(f->reference, right)) {
			return narrowTypeByEquality(t, operatorKind, left, assumeTrue);
		}
		if (strictNullChecks) {
			if (optionalChainContainsReference(left, f->reference)) {
				t = narrowTypeByOptionalChainContainment(f, t, operatorKind, right, assumeTrue);
			} else if (optionalChainContainsReference(right, f->reference)) {
				t = narrowTypeByOptionalChainContainment(f, t, operatorKind, left, assumeTrue);
			}
		}
		Node* leftAccess = getDiscriminantPropertyAccess(f, left, t);
		if (leftAccess != nullptr) {
			return narrowTypeByDiscriminantProperty(t, leftAccess, operatorKind, right, assumeTrue);
		}
		Node* rightAccess = getDiscriminantPropertyAccess(f, right, t);
		if (rightAccess != nullptr) {
			return narrowTypeByDiscriminantProperty(t, rightAccess, operatorKind, left, assumeTrue);
		}
		if (isMatchingConstructorReference(f, left)) {
			return narrowTypeByConstructor(t, operatorKind, right, assumeTrue);
		}
		if (isMatchingConstructorReference(f, right)) {
			return narrowTypeByConstructor(t, operatorKind, left, assumeTrue);
		}
		if (isBooleanLiteral(right) && !isAccessExpression(left)) {
			return narrowTypeByBooleanComparison(f, t, left, right, operatorKind, assumeTrue);
		}
		if (isBooleanLiteral(left) && !isAccessExpression(right)) {
			return narrowTypeByBooleanComparison(f, t, right, left, operatorKind, assumeTrue);
		}
		break;
	}
	case Kind::InstanceOfKeyword:
		return narrowTypeByInstanceof(f, t, expr, assumeTrue);
	case Kind::InKeyword: {
		if (isPrivateIdentifier(expr->Left)) {
			return narrowTypeByPrivateIdentifierInInExpression(f, t, expr, assumeTrue);
		}
		Node* target = getReferenceCandidate(expr->Right);
		if (containsMissingType(t) && isAccessExpression(f->reference) &&
			isMatchingReference(f->reference->expression(), target)) {
			Type* leftType = getTypeOfExpression(expr->Left);
			if (isTypeUsableAsPropertyName(leftType)) {
				auto [accessedName, ok] = getAccessedPropertyName(f->reference);
				if (ok && accessedName == getPropertyNameFromType(leftType)) {
					return getTypeWithFacts(t, assumeTrue ? TypeFactsNEUndefined
														 : TypeFactsEQUndefined);
				}
			}
		}
		if (isMatchingReference(f->reference, target)) {
			Type* leftType = getTypeOfExpression(expr->Left);
			if (isTypeUsableAsPropertyName(leftType)) {
				return narrowTypeByInKeyword(f, t, leftType, assumeTrue);
			}
		}
		break;
	}
	case Kind::CommaToken:
		return narrowType(f, t, expr->Right, assumeTrue);
	case Kind::AmpersandAmpersandToken:
		// Ordinarily we won't see && and || expressions in control flow analysis because the Binder breaks those
		// expressions down to individual conditional control flows. However, we may encounter them when analyzing
		// aliased conditional expressions.
		if (assumeTrue) {
			return narrowType(f, narrowType(f, t, expr->Left, true /*assumeTrue*/), expr->Right,
							  true /*assumeTrue*/);
		}
		return getUnionType({narrowType(f, t, expr->Left, false /*assumeTrue*/),
							 narrowType(f, t, expr->Right, false /*assumeTrue*/)});
	case Kind::BarBarToken:
		if (assumeTrue) {
			return getUnionType({narrowType(f, t, expr->Left, true /*assumeTrue*/),
								 narrowType(f, t, expr->Right, true /*assumeTrue*/)});
		}
		return narrowType(f, narrowType(f, t, expr->Left, false /*assumeTrue*/), expr->Right,
						  false /*assumeTrue*/);
	}
	return t;
}

// narrowTypeByEquality (flow.go:536-596)
Type* Checker::narrowTypeByEquality(Type* t, Kind operatorKind, Node* value, bool assumeTrue) {
	if ((t->flags & TypeFlagsAny) != 0) {
		return t;
	}
	if (operatorKind == Kind::ExclamationEqualsToken ||
		operatorKind == Kind::ExclamationEqualsEqualsToken) {
		assumeTrue = !assumeTrue;
	}
	Type* valueType = getTypeOfExpression(value);
	bool doubleEquals =
		operatorKind == Kind::EqualsEqualsToken || operatorKind == Kind::ExclamationEqualsToken;
	if ((valueType->flags & TypeFlagsNullable) != 0) {
		if (!strictNullChecks) {
			return t;
		}
		TypeFacts facts;
		// Go `switch {}` — if/else chain preserving case order.
		if (doubleEquals) {
			facts = assumeTrue ? TypeFactsEQUndefinedOrNull : TypeFactsNEUndefinedOrNull;
		} else if ((valueType->flags & TypeFlagsNull) != 0) {
			facts = assumeTrue ? TypeFactsEQNull : TypeFactsNENull;
		} else {
			facts = assumeTrue ? TypeFactsEQUndefined : TypeFactsNEUndefined;
		}
		return getAdjustedTypeWithFacts(t, facts);
	}
	if (assumeTrue) {
		if (!doubleEquals &&
			((t->flags & TypeFlagsUnknown) != 0 ||
			 someType(t, [this](Type* t2) { return IsEmptyAnonymousObjectType(t2); }))) {
			if ((valueType->flags & (TypeFlagsPrimitive | TypeFlagsNonPrimitive)) != 0 ||
				IsEmptyAnonymousObjectType(valueType)) {
				return valueType;
			}
			if ((valueType->flags & TypeFlagsObject) != 0) {
				return nonPrimitiveType;
			}
		}
		if (!doubleEquals && (valueType->flags & TypeFlagsPrimitive) != 0 &&
			isUniformUnionType(t)) {
			Type* regularType = getRegularTypeOfLiteralType(valueType);
			if (unionContainsType(t, regularType, false /*matchSymbol*/)) {
				return regularType;
			}
		}
		Type* filteredType = filterType(t, [this, valueType, doubleEquals](Type* t2) {
			return areTypesComparable(t2, valueType) ||
				   (doubleEquals && isCoercibleUnderDoubleEquals(t2, valueType));
		});
		return replacePrimitivesWithLiterals(filteredType, valueType);
	}
	if (isUnitType(valueType)) {
		if (isUniformUnionType(t)) {
			Type* filteredType = removeType(t, getRegularTypeOfLiteralType(valueType));
			if (filteredType != t) {
				return filteredType;
			}
		}
		return filterType(t, [this, valueType](Type* t2) {
			return !(isUnitLikeType(t2) && areTypesComparable(t2, valueType));
		});
	}
	return t;
}

// narrowTypeByTypeof (flow.go:598-617)
Type* Checker::narrowTypeByTypeof(FlowState* f, Type* t, TypeOfExpression* typeOfExpr,
								  Kind operatorKind, Node* literal, bool assumeTrue) {
	// We have '==', '!=', '===', or !==' operator with 'typeof xxx' and string literal operands
	if (operatorKind == Kind::ExclamationEqualsToken ||
		operatorKind == Kind::ExclamationEqualsEqualsToken) {
		assumeTrue = !assumeTrue;
	}
	Node* target = getReferenceCandidate(typeOfExpr->Expression);
	if (!isMatchingReference(f->reference, target)) {
		if (strictNullChecks && optionalChainContainsReference(target, f->reference) &&
			assumeTrue == (literal->text() != "undefined")) {
			t = getAdjustedTypeWithFacts(t, TypeFactsNEUndefinedOrNull);
		}
		Node* propertyAccess = getDiscriminantPropertyAccess(f, target, t);
		if (propertyAccess != nullptr) {
			return narrowTypeByDiscriminant(t, propertyAccess,
										  [this, literal, assumeTrue](Type* t2) {
											  return narrowTypeByLiteralExpression(t2, literal,
																				   assumeTrue);
										  });
		}
		return t;
	}
	return narrowTypeByLiteralExpression(t, literal, assumeTrue);
}

// narrowTypeByLiteralExpression (flow.go:542-551 — defined after typeofNEFacts in Go;
// placed here in C++ file order)
Type* Checker::narrowTypeByLiteralExpression(Type* t, Node* literal, bool assumeTrue) {
	if (assumeTrue) {
		return narrowTypeByTypeName(t, literal->text());
	}
	TypeFacts facts = TypeFactsTypeofNEHostObject;
	if (auto it = typeofNEFacts.find(literal->text()); it != typeofNEFacts.end()) {
		facts = it->second;
	}
	return getAdjustedTypeWithFacts(t, facts);
}

// narrowTypeByTypeName (flow.go:553-579)
Type* Checker::narrowTypeByTypeName(Type* t, const std::string& typeName) {
	if (typeName == "string") {
		return narrowTypeByTypeFacts(t, stringType, TypeFactsTypeofEQString);
	}
	if (typeName == "number") {
		return narrowTypeByTypeFacts(t, numberType, TypeFactsTypeofEQNumber);
	}
	if (typeName == "bigint") {
		return narrowTypeByTypeFacts(t, bigintType, TypeFactsTypeofEQBigInt);
	}
	if (typeName == "boolean") {
		return narrowTypeByTypeFacts(t, booleanType, TypeFactsTypeofEQBoolean);
	}
	if (typeName == "symbol") {
		return narrowTypeByTypeFacts(t, esSymbolType, TypeFactsTypeofEQSymbol);
	}
	if (typeName == "object") {
		if ((t->flags & TypeFlagsAny) != 0) {
			return t;
		}
		return getUnionType({narrowTypeByTypeFacts(t, nonPrimitiveType, TypeFactsTypeofEQObject),
							 narrowTypeByTypeFacts(t, nullType, TypeFactsEQNull)});
	}
	if (typeName == "function") {
		if ((t->flags & TypeFlagsAny) != 0) {
			return t;
		}
		return narrowTypeByTypeFacts(t, globalFunctionType, TypeFactsTypeofEQFunction);
	}
	if (typeName == "undefined") {
		return narrowTypeByTypeFacts(t, undefinedType, TypeFactsEQUndefined);
	}
	return narrowTypeByTypeFacts(t, nonPrimitiveType, TypeFactsTypeofEQHostObject);
}

// narrowTypeByTypeFacts (flow.go:581-595)
Type* Checker::narrowTypeByTypeFacts(Type* t, Type* impliedType, TypeFacts facts) {
	return mapType(t, [this, impliedType, facts](Type* t2) -> Type* {
		if (isTypeRelatedTo(t2, impliedType, strictSubtypeRelation)) {
			if (hasTypeFacts(t2, facts)) {
				return t2;
			}
			return neverType;
		}
		if (isTypeSubtypeOf(impliedType, t2)) {
			return impliedType;
		}
		if (hasTypeFacts(t2, facts)) {
			return getIntersectionType({t2, impliedType});
		}
		return neverType;
	});
}

// narrowTypeByDiscriminantProperty (flow.go:~600-623)
Type* Checker::narrowTypeByDiscriminantProperty(Type* t, Node* access, Kind operatorKind,
												Node* value, bool assumeTrue) {
	if ((operatorKind == Kind::EqualsEqualsEqualsToken ||
		 operatorKind == Kind::ExclamationEqualsEqualsToken) &&
		(t->flags & TypeFlagsUnion) != 0) {
		std::string keyPropertyName = getKeyPropertyName(t);
		if (!keyPropertyName.empty()) {
			auto [accessedName, ok] = getAccessedPropertyName(access);
			if (ok && keyPropertyName == accessedName) {
				Type* candidate =
					getConstituentTypeForKeyType(t, getTypeOfExpression(value));
				if (candidate != nullptr) {
					if ((assumeTrue && operatorKind == Kind::EqualsEqualsEqualsToken) ||
						(!assumeTrue && operatorKind == Kind::ExclamationEqualsEqualsToken)) {
						return candidate;
					}
					if (Type* propType = getTypeOfPropertyOfType(candidate, keyPropertyName);
						propType != nullptr && isUnitType(propType)) {
						return removeType(t, candidate);
					}
					return t;
				}
			}
		}
	}
	return narrowTypeByDiscriminant(t, access, [this, operatorKind, value, assumeTrue](Type* t2) {
		return narrowTypeByEquality(t2, operatorKind, value, assumeTrue);
	});
}

// narrowTypeByDiscriminant (flow.go)
Type* Checker::narrowTypeByDiscriminant(Type* t, Node* access,
										const std::function<Type*(Type*)>& narrowTypeFn) {
	auto [propNameSB, ok] = getAccessedPropertyName(access);
	if (!ok) {
		return t;
	}
	std::string propName = propNameSB; // clang-15: structured bindings not capturable
	bool optionalChain = isOptionalChain(access);
	bool removeNullable = strictNullChecks && (optionalChain || isNonNullAccess(access)) &&
						  maybeTypeOfKind(t, TypeFlagsNullable);
	Type* nonNullType = t;
	if (removeNullable) {
		nonNullType = getTypeWithFacts(t, TypeFactsNEUndefinedOrNull);
	}
	Type* propType = getTypeOfPropertyOfType(nonNullType, propName);
	if (propType == nullptr) {
		return t;
	}
	if (removeNullable && optionalChain) {
		propType = getOptionalType(propType, false);
	}
	Type* narrowedPropType = narrowTypeFn(propType);
	return filterType(t, [this, &propName, narrowedPropType](Type* t2) {
		Type* discriminantType =
			orElse(getTypeOfPropertyOrIndexSignatureOfType(t2, propName), unknownType);
		return (discriminantType->flags & TypeFlagsNever) == 0 &&
			   (narrowedPropType->flags & TypeFlagsNever) == 0 &&
			   areTypesComparable(narrowedPropType, discriminantType);
	});
}

// isMatchingConstructorReference (flow.go)
bool Checker::isMatchingConstructorReference(FlowState* f, Node* expr) {
	Node* name = nullptr;
	if (isPropertyAccessExpression(expr)) {
		name = expr->as<PropertyAccessExpression>()->name;
	} else if (isElementAccessExpression(expr) &&
			   isStringLiteralLike(expr->as<ElementAccessExpression>()->ArgumentExpression)) {
		name = expr->as<ElementAccessExpression>()->ArgumentExpression;
	}
	return name != nullptr && name->text() == "constructor" &&
		   isMatchingReference(f->reference, expr->expression());
}

// narrowTypeByConstructor (flow.go)
Type* Checker::narrowTypeByConstructor(Type* t, Kind operatorKind, Node* identifier,
									   bool assumeTrue) {
	// Do not narrow when checking inequality.
	if ((assumeTrue && operatorKind != Kind::EqualsEqualsToken &&
		 operatorKind != Kind::EqualsEqualsEqualsToken) ||
		(!assumeTrue && operatorKind != Kind::ExclamationEqualsToken &&
		 operatorKind != Kind::ExclamationEqualsEqualsToken)) {
		return t;
	}
	// Get the type of the constructor identifier expression, if it is not a function then do not narrow.
	Type* identifierType = getTypeOfExpression(identifier);
	if (!isFunctionType(identifierType) && !isConstructorType(identifierType)) {
		return t;
	}
	// Get the prototype property of the type identifier so we can find out its type.
	Symbol* prototypeProperty = getPropertyOfType(identifierType, "prototype");
	if (prototypeProperty == nullptr) {
		return t;
	}
	// Get the type of the prototype, if it is undefined, or the global `Object` or `Function` types then do not narrow.
	Type* prototypeType = getTypeOfSymbol(prototypeProperty);
	Type* candidate = nullptr;
	if (!IsTypeAny(prototypeType)) {
		candidate = prototypeType;
	}
	if (candidate == nullptr || candidate == globalObjectType ||
		candidate == globalFunctionType) {
		return t;
	}
	// If the type that is being narrowed is `any` then just return the `candidate` type since every type is a subtype of `any`.
	if (IsTypeAny(t)) {
		return candidate;
	}
	// Filter out types that are not considered to be "constructed by" the `candidate` type.
	return filterType(t, [this, candidate](Type* t2) {
		return isConstructedBy(t2, candidate);
	});
}

// isConstructedBy (flow.go)
bool Checker::isConstructedBy(Type* source, Type* target) {
	// If either the source or target type are a class type then we need to check that they are the same exact type.
	// This is because you may have a class `A` that defines some set of properties, and another class `B`
	// that defines the same set of properties as class `A`, in that case they are structurally the same
	// type, but when you do something like `instanceOfA.constructor === B` it will return false.
	if (((source->flags & TypeFlagsObject) != 0 &&
		 (source->objectFlags & ObjectFlagsClass) != 0) ||
		((target->flags & TypeFlagsObject) != 0 &&
		 (target->objectFlags & ObjectFlagsClass) != 0)) {
		return source->symbol == target->symbol;
	}
	// For all other types just check that the `source` type is a subtype of the `target` type.
	return isTypeSubtypeOf(source, target);
}

// narrowTypeByBooleanComparison (flow.go)
Type* Checker::narrowTypeByBooleanComparison(FlowState* f, Type* t, Node* expr,
											 Node* boolValue, Kind operatorKind,
											 bool assumeTrue) {
	assumeTrue = (assumeTrue != (boolValue->kind == Kind::TrueKeyword)) !=
				 (operatorKind != Kind::ExclamationEqualsEqualsToken &&
				  operatorKind != Kind::ExclamationEqualsToken);
	return narrowType(f, t, expr, assumeTrue);
}

// narrowTypeByInstanceof (flow.go)
Type* Checker::narrowTypeByInstanceof(FlowState* f, Type* t, BinaryExpression* expr,
									  bool assumeTrue) {
	Node* left = getReferenceCandidate(expr->Left);
	if (!isMatchingReference(f->reference, left)) {
		if (assumeTrue && strictNullChecks &&
			optionalChainContainsReference(left, f->reference)) {
			return getAdjustedTypeWithFacts(t, TypeFactsNEUndefinedOrNull);
		}
		return t;
	}
	Node* right = expr->Right;
	Type* rightType = getTypeOfExpression(right);
	if (!isTypeDerivedFrom(rightType, globalObjectType)) {
		return t;
	}
	// if the right-hand side has an object type with a custom `[Symbol.hasInstance]` method, and that method
	// has a type predicate, use the type predicate to perform narrowing. This allows normal `object` types to
	// participate in `instanceof`, as per Step 2 of https://tc39.es/ecma262/#sec-instanceofoperator.
	TypePredicate* predicate = nullptr;
	if (Signature* signature = getEffectsSignature(expr); signature != nullptr) {
		predicate = getTypePredicateOfSignature(signature);
	}
	if (predicate != nullptr && predicate->kind == TypePredicateKind::Identifier &&
		predicate->parameterIndex == 0) {
		return getNarrowedType(t, predicate->t, assumeTrue, true /*checkDerived*/);
	}
	if (!isTypeDerivedFrom(rightType, globalFunctionType)) {
		return t;
	}
	Type* instanceType =
		mapType(rightType, [this](Type* t2) { return getInstanceType(t2); });
	// Don't narrow from `any` if the target type is exactly `Object` or `Function`, and narrow
	// in the false branch only if the target is a non-empty object type.
	if ((IsTypeAny(t) && (instanceType == globalObjectType ||
						  instanceType == globalFunctionType)) ||
		(!assumeTrue && !((instanceType->flags & TypeFlagsObject) != 0 &&
						  !IsEmptyAnonymousObjectType(instanceType)))) {
		return t;
	}
	return getNarrowedType(t, instanceType, assumeTrue, true /*checkDerived*/);
}

// getNarrowedType (flow.go)
Type* Checker::getNarrowedType(Type* t, Type* candidate, bool assumeTrue, bool checkDerived) {
	if ((t->flags & TypeFlagsUnion) == 0) {
		return getNarrowedTypeWorker(t, candidate, assumeTrue, checkDerived);
	}
	NarrowedTypeKey key{t, candidate, assumeTrue, checkDerived};
	if (auto it = narrowedTypes.find(key); it != narrowedTypes.end()) {
		return it->second;
	}
	Type* narrowedType = getNarrowedTypeWorker(t, candidate, assumeTrue, checkDerived);
	narrowedTypes[key] = narrowedType;
	return narrowedType;
}

// getNarrowedTypeWorker (flow.go)
Type* Checker::getNarrowedTypeWorker(Type* t, Type* candidate, bool assumeTrue,
									 bool checkDerived) {
	if (!assumeTrue) {
		if (t == candidate) {
			return neverType;
		}
		if (checkDerived) {
			return filterType(t, [this, candidate](Type* t2) {
				return !isTypeDerivedFrom(t2, candidate);
			});
		}
		if ((t->flags & TypeFlagsUnknown) != 0) {
			t = unknownUnionType;
		}
		Type* trueType =
			getNarrowedType(t, candidate, true /*assumeTrue*/, false /*checkDerived*/);
		return recombineUnknownType(filterType(t, [this, trueType](Type* t2) {
			return !isTypeSubsetOf(t2, trueType);
		}));
	}
	if ((t->flags & TypeFlagsAnyOrUnknown) != 0) {
		return candidate;
	}
	if (t == candidate) {
		return candidate;
	}
	// We first attempt to filter the current type, narrowing constituents as appropriate and removing
	// constituents that are unrelated to the candidate.
	std::string keyPropertyName;
	if ((t->flags & TypeFlagsUnion) != 0) {
		keyPropertyName = getKeyPropertyName(t);
	}
	Type* narrowedType = mapType(candidate, [this, &keyPropertyName, t, checkDerived](Type* n) {
		// If a discriminant property is available, use that to reduce the type.
		Type* matching = t;
		if (!keyPropertyName.empty()) {
			if (Type* discriminant = getTypeOfPropertyOfType(n, keyPropertyName);
				discriminant != nullptr) {
				if (Type* constituent = getConstituentTypeForKeyType(t, discriminant);
					constituent != nullptr) {
					matching = constituent;
				}
			}
		}
		// For each constituent t in the current type, if t and c are directly related, pick the most
		// specific of the two. When t and c are related in both directions, we prefer c for type predicates
		// because that is the asserted type, but t for `instanceof` because generics aren't reflected in
		// prototype object types.
		std::function<Type*(Type*)> mapTypeFn;
		if (checkDerived) {
			mapTypeFn = [this, n](Type* t2) -> Type* {
				if (isTypeDerivedFrom(t2, n)) {
					return t2;
				}
				if (isTypeDerivedFrom(n, t2)) {
					return n;
				}
				return neverType;
			};
		} else {
			mapTypeFn = [this, n](Type* t2) -> Type* {
				if (isTypeStrictSubtypeOf(t2, n)) {
					return t2;
				}
				if (isTypeStrictSubtypeOf(n, t2)) {
					return n;
				}
				if (isTypeSubtypeOf(t2, n)) {
					return t2;
				}
				if (isTypeSubtypeOf(n, t2)) {
					return n;
				}
				return neverType;
			};
		}
		Type* directlyRelated = mapType(matching, mapTypeFn);
		if ((directlyRelated->flags & TypeFlagsNever) == 0) {
			return directlyRelated;
		}
		// If no constituents are directly related, create intersections for any generic constituents that
		// are related by constraint.
		std::function<bool(Type*, Type*)> isRelated;
		if (checkDerived) {
			isRelated = [this](Type* a, Type* b) { return isTypeDerivedFrom(a, b); };
		} else {
			isRelated = [this](Type* a, Type* b) { return isTypeSubtypeOf(a, b); };
		}
		return mapType(t, [this, n, &isRelated](Type* t2) -> Type* {
			if (maybeTypeOfKind(t2, TypeFlagsInstantiable)) {
				Type* constraint = getBaseConstraintOfType(t2);
				if (constraint == nullptr || isRelated(n, constraint)) {
					return getIntersectionType({t2, n});
				}
			}
			return neverType;
		});
	});
	// If filtering produced a non-empty type, return that. Otherwise, pick the most specific of the two
	// based on assignability, or as a last resort produce an intersection.
	if ((narrowedType->flags & TypeFlagsNever) == 0) {
		return narrowedType;
	}
	if (isTypeSubtypeOf(candidate, t)) {
		return candidate;
	}
	if (isTypeAssignableTo(t, candidate)) {
		return t;
	}
	if (isTypeAssignableTo(candidate, t)) {
		return candidate;
	}
	return getIntersectionType({t, candidate});
}

// getInstanceType (flow.go)
Type* Checker::getInstanceType(Type* constructorType) {
	Type* prototypePropertyType = getTypeOfPropertyOfType(constructorType, "prototype");
	if (prototypePropertyType != nullptr && !IsTypeAny(prototypePropertyType)) {
		return prototypePropertyType;
	}
	std::vector<Signature*> constructSignatures =
		getSignaturesOfType(constructorType, SignatureKind::Construct);
	if (!constructSignatures.empty()) {
		return getUnionType(mapVec(constructSignatures, [this](Signature* signature) {
			return getReturnTypeOfSignature(getErasedSignature(signature));
		}));
	}
	// We use the empty object type to indicate we don't know the type of objects created by
	// this constructor function.
	return emptyObjectType;
}

// narrowTypeByPrivateIdentifierInInExpression (flow.go)
Type* Checker::narrowTypeByPrivateIdentifierInInExpression(FlowState* f, Type* t,
														   BinaryExpression* expr,
														   bool assumeTrue) {
	Node* target = getReferenceCandidate(expr->Right);
	if (!isMatchingReference(f->reference, target)) {
		return t;
	}
	Symbol* symbol = getSymbolForPrivateIdentifierExpression(expr->Left);
	if (symbol == nullptr) {
		return t;
	}
	Symbol* classSymbol = symbol->data->parent;
	Type* targetType;
	if (hasStaticModifier(symbol->data->valueDeclaration)) {
		targetType = getTypeOfSymbol(classSymbol);
	} else {
		targetType = getDeclaredTypeOfSymbol(classSymbol);
	}
	return getNarrowedType(t, targetType, assumeTrue, true /*checkDerived*/);
}

// narrowTypeByInKeyword (flow.go)
Type* Checker::narrowTypeByInKeyword(FlowState* f, Type* t, Type* nameType, bool assumeTrue) {
	std::string name = getPropertyNameFromType(nameType);
	bool isKnownProperty = someType(t, [this, &name](Type* t2) {
		return isTypePresencePossible(t2, name, true /*assumeTrue*/);
	});
	if (isKnownProperty) {
		// If the check is for a known property (i.e. a property declared in some constituent of
		// the target type), we filter the target type by presence of absence of the property.
		return filterType(t, [this, &name, assumeTrue](Type* t2) {
			return isTypePresencePossible(t2, name, assumeTrue);
		});
	}
	if (assumeTrue) {
		// If the check is for an unknown property, we intersect the target type with `Record<X, unknown>`,
		// where X is the name of the property.
		Symbol* recordSymbol = getGlobalRecordSymbol();
		if (recordSymbol != nullptr) {
			return getIntersectionType({t, getTypeAliasInstantiation(
											  recordSymbol, {nameType, unknownType}, nullptr)});
		}
	}
	return t;
}

// isTypePresencePossible (flow.go)
bool Checker::isTypePresencePossible(Type* t, const std::string& propName, bool assumeTrue) {
	Symbol* prop = getPropertyOfType(t, propName);
	if (prop != nullptr) {
		return (prop->flags & SymbolFlagsOptional) != 0 ||
			   (prop->checkFlags & CheckFlagsPartial) != 0 || assumeTrue;
	}
	return getApplicableIndexInfoForName(t, propName) != nullptr || !assumeTrue;
}

// narrowTypeByOptionalChainContainment (flow.go)
Type* Checker::narrowTypeByOptionalChainContainment(FlowState* f, Type* t, Kind operatorKind,
													Node* value, bool assumeTrue) {
	// We are in a branch of obj?.foo === value (or any one of the other equality operators). We narrow obj as follows:
	// When operator is === and type of value excludes undefined, null and undefined is removed from type of obj in true branch.
	// When operator is !== and type of value excludes undefined, null and undefined is removed from type of obj in false branch.
	// When operator is == and type of value excludes null and undefined, null and undefined is removed from type of obj in true branch.
	// When operator is != and type of value excludes null and undefined, null and undefined is removed from type of obj in false branch.
	// When operator is === and type of value is undefined, null and undefined is removed from type of obj in false branch.
	// When operator is !== and type of value is undefined, null and undefined is removed from type of obj in true branch.
	// When operator is == and type of value is null or undefined, null and undefined is removed from type of obj in false branch.
	// When operator is != and type of value is null or undefined, null and undefined is removed from type of obj in true branch.
	bool equalsOperator = operatorKind == Kind::EqualsEqualsToken ||
						  operatorKind == Kind::EqualsEqualsEqualsToken;
	TypeFlags nullableFlags;
	if (operatorKind == Kind::EqualsEqualsToken ||
		operatorKind == Kind::ExclamationEqualsToken) {
		nullableFlags = TypeFlagsNullable;
	} else {
		nullableFlags = TypeFlagsUndefined;
	}
	Type* valueType = getTypeOfExpression(value);
	// Note that we include any and unknown in the exclusion test because their domain includes null and undefined.
	bool removeNullable =
		(equalsOperator != assumeTrue &&
		 everyType(valueType, [nullableFlags](Type* t2) {
			 return (t2->flags & nullableFlags) != 0;
		 })) ||
		(equalsOperator == assumeTrue &&
		 everyType(valueType, [nullableFlags](Type* t2) {
			 return (t2->flags & (TypeFlagsAnyOrUnknown | nullableFlags)) == 0;
		 }));
	if (removeNullable) {
		return getAdjustedTypeWithFacts(t, TypeFactsNEUndefinedOrNull);
	}
	return t;
}

// getTypeAtSwitchClause (flow.go)
FlowType Checker::getTypeAtSwitchClause(FlowState* f, FlowNode* flow) {
	FlowSwitchClauseData* data = flow->node->as<FlowSwitchClauseData>();
	Node* expr = skipParentheses(data->switchStatement->expression());
	FlowType flowType = getTypeAtFlowNode(f, flow->antecedent);
	Type* t = flowType.t;
	if (isMatchingReference(f->reference, expr)) {
		t = narrowTypeBySwitchOnDiscriminant(t, data);
	} else if (expr->kind == Kind::TypeOfExpression &&
			   isMatchingReference(f->reference, expr->expression())) {
		t = narrowTypeBySwitchOnTypeOf(t, data);
	} else if (expr->kind == Kind::TrueKeyword) {
		t = narrowTypeBySwitchOnTrue(f, t, data);
	} else {
		if (strictNullChecks) {
			if (optionalChainContainsReference(expr, f->reference)) {
				t = narrowTypeBySwitchOptionalChainContainment(
					t, data, [](Type* t2) {
						return (t2->flags & (TypeFlagsUndefined | TypeFlagsNever)) == 0;
					});
			} else if (isTypeOfExpression(expr) &&
					   optionalChainContainsReference(expr->expression(), f->reference)) {
				t = narrowTypeBySwitchOptionalChainContainment(
					t, data, [](Type* t2) {
						return !((t2->flags & TypeFlagsNever) != 0 ||
								 ((t2->flags & TypeFlagsStringLiteral) != 0 &&
								  getStringLiteralValue(t2) == "undefined"));
					});
			}
		}
		Node* access = getDiscriminantPropertyAccess(f, expr, t);
		if (access != nullptr) {
			t = narrowTypeBySwitchOnDiscriminantProperty(t, access, data);
		}
	}
	return newFlowType(t, flowType.incomplete);
}

// narrowTypeBySwitchOnDiscriminant (flow.go)
Type* Checker::narrowTypeBySwitchOnDiscriminant(Type* t, FlowSwitchClauseData* data) {
	// We only narrow if all case expressions specify
	// values with unit types, except for the case where
	// `type` is unknown. In this instance we map object
	// types to the nonPrimitive type and narrow with that.
	std::vector<Type*> switchTypes = getSwitchClauseTypes(data->switchStatement);
	if (switchTypes.empty()) {
		return t;
	}
	std::vector<Type*> clauseTypes(switchTypes.begin() + data->clauseStart,
								   switchTypes.begin() + data->clauseEnd);
	bool hasDefaultClause = data->clauseStart == data->clauseEnd ||
							std::find(clauseTypes.begin(), clauseTypes.end(), neverType) !=
								clauseTypes.end();
	if ((t->flags & TypeFlagsUnknown) != 0 && !hasDefaultClause) {
		std::vector<Type*> groundClauseTypes;
		bool hasGround = false;
		for (size_t i = 0; i < clauseTypes.size(); i++) {
			Type* s = clauseTypes[i];
			if ((s->flags & (TypeFlagsPrimitive | TypeFlagsNonPrimitive)) != 0) {
				if (hasGround) {
					groundClauseTypes.push_back(s);
				}
			} else if ((s->flags & TypeFlagsObject) != 0) {
				if (!hasGround) {
					groundClauseTypes.assign(clauseTypes.begin(), clauseTypes.begin() + i);
					hasGround = true;
				}
				groundClauseTypes.push_back(nonPrimitiveType);
			} else {
				return t;
			}
		}
		return getUnionType(!hasGround ? clauseTypes : groundClauseTypes);
	}
	Type* discriminantType = getUnionType(clauseTypes);
	Type* caseType = nullptr;
	if ((discriminantType->flags & TypeFlagsNever) != 0) {
		caseType = neverType;
	} else {
		if ((discriminantType->flags & TypeFlagsPrimitive) != 0 && isUniformUnionType(t)) {
			Type* regularType = getRegularTypeOfLiteralType(discriminantType);
			if (unionContainsType(t, regularType, false /*matchSymbol*/)) {
				caseType = regularType;
			}
		}
		if (caseType == nullptr) {
			Type* filtered = filterType(t, [this, discriminantType](Type* t2) {
				return areTypesComparable(discriminantType, t2);
			});
			caseType = replacePrimitivesWithLiterals(filtered, discriminantType);
		}
	}
	if (!hasDefaultClause) {
		return caseType;
	}
	Type* defaultType = filterType(t, [this, &switchTypes](Type* t2) {
		if (!isUnitLikeType(t2)) {
			return true;
		}
		Type* u = undefinedType;
		if ((t2->flags & TypeFlagsUndefined) == 0) {
			u = getRegularTypeOfLiteralType(extractUnitType(t2));
		}
		return std::find_if(switchTypes.begin(), switchTypes.end(),
							[this, u](Type* st) {
								return isUnitType(st) && areTypesComparable(st, u);
							}) == switchTypes.end();
	});
	if ((caseType->flags & TypeFlagsNever) != 0) {
		return defaultType;
	}
	return getUnionType({caseType, defaultType});
}

// narrowTypeBySwitchOnTypeOf (flow.go)
Type* Checker::narrowTypeBySwitchOnTypeOf(Type* t, FlowSwitchClauseData* data) {
	auto witnesses = getSwitchClauseTypeOfWitnesses(data->switchStatement);
	if (!witnesses.has_value()) {
		return t;
	}
	const std::vector<Node*>& clauses =
		data->switchStatement->as<SwitchStatement>()->CaseBlock->as<CaseBlock>()
			->Clauses->nodes;
	// Equal start and end denotes implicit fallthrough; undefined marks explicit default clause.
	int defaultIndex = -1;
	for (size_t i = 0; i < clauses.size(); i++) {
		if (clauses[i]->kind == Kind::DefaultClause) {
			defaultIndex = static_cast<int>(i);
			break;
		}
	}
	int clauseStart = data->clauseStart;
	int clauseEnd = data->clauseEnd;
	bool hasDefaultClause =
		clauseStart == clauseEnd || (defaultIndex >= clauseStart && defaultIndex < clauseEnd);
	if (hasDefaultClause) {
		// In the default clause we filter constituents down to those that are not-equal to all handled cases.
		TypeFacts notEqualFacts =
			getNotEqualFactsFromTypeofSwitch(clauseStart, clauseEnd, *witnesses);
		return filterType(t, [this, notEqualFacts](Type* t2) {
			return getTypeFacts(t2, notEqualFacts) == notEqualFacts;
		});
	}
	// In the non-default cause we create a union of the type narrowed by each of the listed cases.
	return getUnionType(mapVec(
		std::vector<std::string>(witnesses->begin() + clauseStart,
								 witnesses->begin() + clauseEnd),
		[this, t](const std::string& text) -> Type* {
			if (!text.empty()) {
				return narrowTypeByTypeName(t, text);
			}
			return neverType;
		}));
}

// narrowTypeBySwitchOnTrue (flow.go)
Type* Checker::narrowTypeBySwitchOnTrue(FlowState* f, Type* t, FlowSwitchClauseData* data) {
	const std::vector<Node*>& clauses =
		data->switchStatement->as<SwitchStatement>()->CaseBlock->as<CaseBlock>()
			->Clauses->nodes;
	int defaultIndex = -1;
	for (size_t i = 0; i < clauses.size(); i++) {
		if (clauses[i]->kind == Kind::DefaultClause) {
			defaultIndex = static_cast<int>(i);
			break;
		}
	}
	int clauseStart = data->clauseStart;
	int clauseEnd = data->clauseEnd;
	bool hasDefaultClause =
		clauseStart == clauseEnd || (defaultIndex >= clauseStart && defaultIndex < clauseEnd);
	// First, narrow away all of the cases that preceded this set of cases.
	for (int i = 0; i < clauseStart; i++) {
		Node* clause = clauses[i];
		if (clause->kind == Kind::CaseClause) {
			t = narrowType(f, t, clause->expression(), false /*assumeTrue*/);
		}
	}
	// If our current set has a default, then none the other cases were hit either.
	// There's no point in narrowing by the other cases in the set, since we can
	// get here through other paths.
	if (hasDefaultClause) {
		for (size_t i = clauseEnd; i < clauses.size(); i++) {
			Node* clause = clauses[i];
			if (clause->kind == Kind::CaseClause) {
				t = narrowType(f, t, clause->expression(), false /*assumeTrue*/);
			}
		}
		return t;
	}
	// Now, narrow based on the cases in this set.
	return getUnionType(mapVec(
		std::vector<Node*>(clauses.begin() + clauseStart, clauses.begin() + clauseEnd),
		[this, f, t](Node* clause) -> Type* {
			if (clause->kind == Kind::CaseClause) {
				return narrowType(f, t, clause->expression(), true /*assumeTrue*/);
			}
			return neverType;
		}));
}

// narrowTypeBySwitchOptionalChainContainment (flow.go)
Type* Checker::narrowTypeBySwitchOptionalChainContainment(
	Type* t, FlowSwitchClauseData* data, const std::function<bool(Type*)>& clauseCheck) {
	std::vector<Type*> switchTypes = getSwitchClauseTypes(data->switchStatement);
	bool everyClauseChecks = data->clauseStart != data->clauseEnd &&
							 std::all_of(switchTypes.begin() + data->clauseStart,
										 switchTypes.begin() + data->clauseEnd, clauseCheck);
	if (everyClauseChecks) {
		return getTypeWithFacts(t, TypeFactsNEUndefinedOrNull);
	}
	return t;
}

// narrowTypeBySwitchOnDiscriminantProperty (flow.go)
Type* Checker::narrowTypeBySwitchOnDiscriminantProperty(Type* t, Node* access,
														FlowSwitchClauseData* data) {
	if (data->clauseStart < data->clauseEnd && (t->flags & TypeFlagsUnion) != 0) {
		auto [accessedName, _] = getAccessedPropertyName(access);
		if (!accessedName.empty() && getKeyPropertyName(t) == accessedName) {
			std::vector<Type*> switchClauseTypes = getSwitchClauseTypes(data->switchStatement);
			std::vector<Type*> clauseTypes(
				switchClauseTypes.begin() + data->clauseStart,
				switchClauseTypes.begin() + data->clauseEnd);
			Type* candidate = getUnionType(mapVec(clauseTypes, [this, t](Type* s) -> Type* {
				Type* result = getConstituentTypeForKeyType(t, s);
				if (result != nullptr) {
					return result;
				}
				return unknownType;
			}));
			if (candidate != unknownType) {
				return candidate;
			}
		}
	}
	return narrowTypeByDiscriminant(t, access, [this, data](Type* t2) {
		return narrowTypeBySwitchOnDiscriminant(t2, data);
	});
}

// getTypeAtFlowBranchLabel (flow.go)
FlowType Checker::getTypeAtFlowBranchLabel(FlowState* f, FlowNode* flow,
										   FlowList* antecedents) {
	size_t antecedentStart = antecedentTypes.size();
	bool subtypeReduction = false;
	bool seenIncomplete = false;
	FlowNode* bypassFlow = nullptr;
	for (FlowList* list = antecedents; list != nullptr; list = list->next) {
		FlowNode* antecedent = list->flow;
		if (bypassFlow == nullptr && (antecedent->flags & FlowFlagsSwitchClause) != 0 &&
			antecedent->node->as<FlowSwitchClauseData>()->isEmpty()) {
			// The antecedent is the bypass branch of a potentially exhaustive switch statement.
			bypassFlow = antecedent;
			continue;
		}
		FlowType flowType = getTypeAtFlowNode(f, antecedent);
		// If the type at a particular antecedent path is the declared type and the
		// reference is known to always be assigned (i.e. when declared and initial types
		// are the same), there is no reason to process more antecedents since the only
		// possible outcome is subtypes that will be removed in the final union type anyway.
		if (flowType.t == f->declaredType && f->declaredType == f->initialType) {
			antecedentTypes.resize(antecedentStart);
			return FlowType{flowType.t};
		}
		if (std::find(antecedentTypes.begin() + antecedentStart, antecedentTypes.end(),
					  flowType.t) == antecedentTypes.end()) {
			antecedentTypes.push_back(flowType.t);
		}
		// If an antecedent type is not a subset of the declared type, we need to perform
		// subtype reduction. This happens when a "foreign" type is injected into the control
		// flow using the instanceof operator or a user defined type predicate.
		if (!isTypeSubsetOf(flowType.t, f->initialType)) {
			subtypeReduction = true;
		}
		if (flowType.incomplete) {
			seenIncomplete = true;
		}
	}
	if (bypassFlow != nullptr) {
		FlowType flowType = getTypeAtFlowNode(f, bypassFlow);
		// If the bypass flow contributes a type we haven't seen yet and the switch statement
		// isn't exhaustive, process the bypass flow type. Since exhaustiveness checks increase
		// the risk of circularities, we only want to perform them when they make a difference.
		if ((flowType.t->flags & TypeFlagsNever) == 0 &&
			std::find(antecedentTypes.begin() + antecedentStart, antecedentTypes.end(),
					  flowType.t) == antecedentTypes.end() &&
			!isExhaustiveSwitchStatement(
				bypassFlow->node->as<FlowSwitchClauseData>()->switchStatement)) {
			if (flowType.t == f->declaredType && f->declaredType == f->initialType) {
				antecedentTypes.resize(antecedentStart);
				return FlowType{flowType.t};
			}
			antecedentTypes.push_back(flowType.t);
			if (!isTypeSubsetOf(flowType.t, f->initialType)) {
				subtypeReduction = true;
			}
			if (flowType.incomplete) {
				seenIncomplete = true;
			}
		}
	}
	FlowType result = newFlowType(
		getUnionOrEvolvingArrayType(
			f,
			std::vector<Type*>(antecedentTypes.begin() + antecedentStart,
							   antecedentTypes.end()),
			subtypeReduction ? UnionReductionSubtype : UnionReductionLiteral),
		seenIncomplete);
	antecedentTypes.resize(antecedentStart);
	return result;
}

// At flow control branch or loop junctions, if the type along every antecedent code path
// is an evolving array type, we construct a combined evolving array type. Otherwise we
// finalize all evolving array types.
// getUnionOrEvolvingArrayType (flow.go)
Type* Checker::getUnionOrEvolvingArrayType(FlowState* f, const std::vector<Type*>& types,
										   UnionReduction subtypeReduction) {
	if (isEvolvingArrayTypeList(types)) {
		return getEvolvingArrayType(getUnionType(
			mapVec(types, [this](Type* t2) { return getElementTypeOfEvolvingArrayType(t2); })));
	}
	Type* result = recombineUnknownType(getUnionTypeEx(
		sameMap(types, [this](Type* t2) { return finalizeEvolvingArrayType(t2); }),
		subtypeReduction, nullptr, nullptr));
	if (result != f->declaredType &&
		(result->flags & f->declaredType->flags & TypeFlagsUnion) != 0 &&
		result->AsUnionType()->types == f->declaredType->AsUnionType()->types) {
		return f->declaredType;
	}
	return result;
}

// getTypeAtFlowLoopLabel (flow.go)
FlowType Checker::getTypeAtFlowLoopLabel(FlowState* f, FlowNode* flow) {
	if (f->refKey.w.empty()) {
		f->refKey = getFlowReferenceKey(f);
	}
	if (f->refKey == nonDottedNameCacheKey) {
		// No cache key is generated when binding patterns are in unnarrowable situations
		return FlowType{f->declaredType};
	}
	FlowLoopKey key{flow, f->refKey};
	// If we have previously computed the control flow type for the reference at
	// this flow loop junction, return the cached type.
	if (auto it = flowLoopCache.find(key); it != flowLoopCache.end()) {
		return FlowType{it->second};
	}
	// If this flow loop junction and reference are already being processed, return
	// the union of the types computed for each branch so far, marked as incomplete.
	// It is possible to see an empty array in cases where loops are nested and the
	// back edge of the outer loop reaches an inner loop that is already being analyzed.
	// In such cases we restart the analysis of the inner loop, which will then see
	// a non-empty in-process array for the outer loop and eventually terminate because
	// the first antecedent of a loop junction is always the non-looping control flow
	// path that leads to the top.
	for (const FlowLoopInfo& loopInfo : flowLoopStack) {
		if (loopInfo.key == key && !loopInfo.types.empty()) {
			return newFlowType(
				getUnionOrEvolvingArrayType(f, loopInfo.types, UnionReductionLiteral),
				true /*incomplete*/);
		}
	}
	// Add the flow loop junction and reference to the in-process stack and analyze
	// each antecedent code path.
	std::vector<Type*> antecedentTypesLocal;
	antecedentTypesLocal.reserve(4);
	bool subtypeReduction = false;
	FlowType firstAntecedentType;
	for (FlowList* list = flow->antecedents; list != nullptr; list = list->next) {
		FlowType flowType;
		if (firstAntecedentType.isNil()) {
			// The first antecedent of a loop junction is always the non-looping control
			// flow path that leads to the top.
			firstAntecedentType = getTypeAtFlowNode(f, list->flow);
			flowType = firstAntecedentType;
		} else {
			// All but the first antecedent are the looping control flow paths that lead
			// back to the loop junction. We track these on the flow loop stack.
			flowLoopStack.push_back(FlowLoopInfo{key, antecedentTypesLocal});
			auto saveFlowTypeCache = flowTypeCache;
			flowTypeCache.clear();
			flowType = getTypeAtFlowNode(f, list->flow);
			flowTypeCache = saveFlowTypeCache;
			flowLoopStack.pop_back();
			// If we see a value appear in the cache it is a sign that control flow analysis
			// was restarted and completed by checkExpressionCached. We can simply pick up
			// the resulting type and bail out.
			if (auto it = flowLoopCache.find(key); it != flowLoopCache.end()) {
				return FlowType{it->second};
			}
		}
		appendIfUnique(antecedentTypesLocal, flowType.t);
		// If an antecedent type is not a subset of the declared type, we need to perform
		// subtype reduction. This happens when a "foreign" type is injected into the control
		// flow using the instanceof operator or a user defined type predicate.
		if (!isTypeSubsetOf(flowType.t, f->initialType)) {
			subtypeReduction = true;
		}
		// If the type at a particular antecedent path is the declared type there is no
		// reason to process more antecedents since the only possible outcome is subtypes
		// that will be removed in the final union type anyway.
		if (flowType.t == f->declaredType) {
			break;
		}
	}
	// The result is incomplete if the first antecedent (the non-looping control flow path)
	// is incomplete.
	Type* result = getUnionOrEvolvingArrayType(
		f, antecedentTypesLocal,
		subtypeReduction ? UnionReductionSubtype : UnionReductionLiteral);
	if (firstAntecedentType.incomplete) {
		return newFlowType(result, true /*incomplete*/);
	}
	flowLoopCache[key] = result;
	return FlowType{result};
}

// getTypeAtFlowArrayMutation (flow.go)
FlowType Checker::getTypeAtFlowArrayMutation(FlowState* f, FlowNode* flow) {
	if (f->declaredType == autoType || f->declaredType == autoArrayType) {
		Node* node = flow->node;
		Node* expr;
		if (isCallExpression(node)) {
			expr = node->expression()->expression();
		} else {
			expr = node->as<BinaryExpression>()->Left->expression();
		}
		if (isMatchingReference(f->reference, getReferenceCandidate(expr))) {
			FlowType flowType = getTypeAtFlowNode(f, flow->antecedent);
			if ((flowType.t->objectFlags & ObjectFlagsEvolvingArray) != 0) {
				Type* evolvedType = flowType.t;
				if (isCallExpression(node)) {
					for (Node* arg : node->arguments()) {
						evolvedType = addEvolvingArrayElementType(evolvedType, arg);
					}
				} else {
					// We must get the context free expression type so as to not recur in an uncached fashion on the LHS (which causes exponential blowup in compile time)
					Type* indexType = getContextFreeTypeOfExpression(
						node->as<BinaryExpression>()
							->Left->as<ElementAccessExpression>()
							->ArgumentExpression);
					if (isTypeAssignableToKind(indexType, TypeFlagsNumberLike)) {
						evolvedType = addEvolvingArrayElementType(
							evolvedType, node->as<BinaryExpression>()->Right);
					}
				}
				return newFlowType(evolvedType, flowType.incomplete);
			}
			return flowType;
		}
	}
	return FlowType{};
}

// getDiscriminantPropertyAccess (flow.go:1435-1455)
// As long as the computed type is a subset of the declared type, we use the full declared type to detect
// a discriminant property. In cases where the computed type isn't a subset, e.g because of a preceding type
// predicate narrowing, we use the actual computed type.
Node* Checker::getDiscriminantPropertyAccess(FlowState* f, Node* expr, Type* computedType) {
	if ((f->declaredType->flags & TypeFlagsUnion) != 0 ||
		(computedType->flags & TypeFlagsUnion) != 0) {
		Node* access = getCandidateDiscriminantPropertyAccess(f, expr);
		if (access != nullptr) {
			auto [name, ok] = getAccessedPropertyName(access);
			if (ok) {
				Type* t = computedType;
				if ((f->declaredType->flags & TypeFlagsUnion) != 0 &&
					isTypeSubsetOf(computedType, f->declaredType)) {
					t = f->declaredType;
				}
				if (isDiscriminantProperty(t, name)) {
					return access;
				}
			}
		}
	}
	return nullptr;
}

// getCandidateDiscriminantPropertyAccess (flow.go)
Node* Checker::getCandidateDiscriminantPropertyAccess(FlowState* f, Node* expr) {
	if (isBindingPattern(f->reference) || isFunctionExpressionOrArrowFunction(f->reference) ||
		isObjectLiteralMethod(f->reference)) {
		// When the reference is a binding pattern or function or arrow expression, we are narrowing a pseudo-reference in
		// getNarrowedTypeOfSymbol. An identifier for a destructuring variable declared in the same binding pattern or
		// parameter declared in the same parameter list is a candidate.
		if (isIdentifier(expr)) {
			Symbol* symbol = getResolvedSymbol(expr);
			Node* declaration = getExportSymbolOfValueSymbolIfExported(symbol)->data->valueDeclaration;
			if (declaration != nullptr &&
				(isBindingElement(declaration) || isParameterDeclaration(declaration)) &&
				f->reference == declaration->parent &&
				declaration->initializer() == nullptr && !hasDotDotDotToken(declaration)) {
				return declaration;
			}
		}
	} else if (isAccessExpression(expr)) {
		// An access expression is a candidate if the reference matches the left hand expression.
		if (isMatchingReference(f->reference, expr->expression())) {
			return expr;
		}
	} else if (isIdentifier(expr)) {
		Symbol* symbol = getResolvedSymbol(expr);
		if (isConstantVariable(symbol)) {
			Node* declaration = symbol->data->valueDeclaration;
			Node* initializer = getCandidateVariableDeclarationInitializer(declaration);
			// Given 'const x = obj.kind', allow 'x' as an alias for 'obj.kind'
			if (initializer != nullptr && isAccessExpression(initializer) &&
				isMatchingReference(f->reference, initializer->expression())) {
				return initializer;
			}
			// Given 'const { kind: x } = obj', allow 'x' as an alias for 'obj.kind'
			if (isBindingElement(declaration) && declaration->initializer() == nullptr) {
				initializer =
					getCandidateVariableDeclarationInitializer(declaration->parent->parent);
				if (initializer != nullptr &&
					(isIdentifier(initializer) || isAccessExpression(initializer)) &&
					isMatchingReference(f->reference, initializer)) {
					return declaration;
				}
			}
		}
	}
	return nullptr;
}

// getCandidateVariableDeclarationInitializer (flow.go)
namespace {
Node* getCandidateVariableDeclarationInitializer(Node* node) {
	if (isVariableDeclaration(node) && node->type() == nullptr) {
		if (Node* initializer = node->initializer(); initializer != nullptr) {
			return skipParentheses(initializer);
		}
	}
	return nullptr;
}
} // namespace

// An evolving array type tracks the element types that have so far been seen in an
// 'x.push(value)' or 'x[n] = value' operation along the control flow graph. Evolving
// array types are ultimately converted into manifest array types (using getFinalArrayType)
// and never escape the getFlowTypeOfReference function.
// getEvolvingArrayType (flow.go)
Type* Checker::getEvolvingArrayType(Type* elementType) {
	CachedTypeKey key{CachedTypeKind::EvolvingArrayType, elementType->id};
	Type* result = nullptr;
	if (auto it = cachedTypes.find(key); it != cachedTypes.end()) {
		result = it->second;
	}
	if (result == nullptr) {
		result = newObjectType(ObjectFlagsEvolvingArray, nullptr);
		result->AsEvolvingArrayType()->elementType = elementType;
		cachedTypes[key] = result;
	}
	return result;
}

// getElementTypeOfEvolvingArrayType (flow.go)
Type* Checker::getElementTypeOfEvolvingArrayType(Type* t) {
	if ((t->objectFlags & ObjectFlagsEvolvingArray) != 0) {
		return t->AsEvolvingArrayType()->elementType;
	}
	return neverType;
}

// isEvolvingArrayTypeList (flow.go)
namespace {
bool isEvolvingArrayTypeList(const std::vector<Type*>& types) {
	bool hasEvolvingArrayType = false;
	for (Type* t : types) {
		if ((t->flags & TypeFlagsNever) == 0) {
			if ((t->objectFlags & ObjectFlagsEvolvingArray) == 0) {
				return false;
			}
			hasEvolvingArrayType = true;
		}
	}
	return hasEvolvingArrayType;
}
} // namespace

// Return true if the given node is 'x' in an 'x.length', x.push(value)', 'x.unshift(value)' or
// 'x[n] = value' operation, where 'n' is an expression of type any, undefined, or a number-like type.
// isEvolvingArrayOperationTarget (flow.go)
bool Checker::isEvolvingArrayOperationTarget(Node* node) {
	Node* root = getReferenceRoot(node);
	Node* parent = root->parent;
	std::string scratch;
	bool isLengthPushOrUnshift =
		isPropertyAccessExpression(parent) &&
		(parent->name()->textView(scratch) == "length" ||
		 (isCallExpression(parent->parent) && isIdentifier(parent->name()) &&
		  isPushOrUnshiftIdentifier(parent->name())));
	bool isElementAssignment =
		isElementAccessExpression(parent) && parent->expression() == root &&
		isBinaryExpression(parent->parent) &&
		parent->parent->as<BinaryExpression>()->OperatorToken->kind == Kind::EqualsToken &&
		parent->parent->as<BinaryExpression>()->Left == parent &&
		!isAssignmentTarget(parent->parent) &&
		isTypeAssignableToKind(
			getTypeOfExpression(parent->as<ElementAccessExpression>()->ArgumentExpression),
			TypeFlagsNumberLike);
	return isLengthPushOrUnshift || isElementAssignment;
}

// When adding evolving array element types we do not perform subtype reduction. Instead,
// we defer subtype reduction until the evolving array type is finalized into a manifest
// array type.
// addEvolvingArrayElementType (flow.go)
Type* Checker::addEvolvingArrayElementType(Type* evolvingArrayType, Node* node) {
	Type* newElementType = getRegularTypeOfObjectLiteral(
		getBaseTypeOfLiteralType(getContextFreeTypeOfExpression(node)));
	Type* elementType = evolvingArrayType->AsEvolvingArrayType()->elementType;
	if (isTypeSubsetOf(newElementType, elementType)) {
		return evolvingArrayType;
	}
	return getEvolvingArrayType(getUnionType({elementType, newElementType}));
}

// finalizeEvolvingArrayType (flow.go)
Type* Checker::finalizeEvolvingArrayType(Type* t) {
	if ((t->objectFlags & ObjectFlagsEvolvingArray) != 0) {
		return getFinalArrayType(t->AsEvolvingArrayType());
	}
	return t;
}

// getFinalArrayType (flow.go)
Type* Checker::getFinalArrayType(EvolvingArrayType* t) {
	if (t->finalArrayType == nullptr) {
		t->finalArrayType = createFinalArrayType(t->elementType);
	}
	return t->finalArrayType;
}

// createFinalArrayType (flow.go)
Type* Checker::createFinalArrayType(Type* elementType) {
	if ((elementType->flags & TypeFlagsNever) != 0) {
		return autoArrayType;
	}
	if ((elementType->flags & TypeFlagsUnion) != 0) {
		return createArrayType(
			getUnionTypeEx(elementType->types(), UnionReductionSubtype, nullptr, nullptr));
	}
	return createArrayType(elementType);
}

// reportFlowControlError (flow.go)
void Checker::reportFlowControlError(Node* node) {
	Node* block = findAncestor(node, isFunctionOrModuleBlock);
	SourceFile* sourceFile = getSourceFileOfNode(node);
	TextRange span = getRangeOfTokenAtPosition(sourceFile, block->statementList()->pos());
	addDiagnostic(newDiagnostic(
		sourceFile, span,
		The_containing_function_or_module_body_is_too_large_for_control_flow_analysis));
}

// isMatchingReference (flow.go)
bool Checker::isMatchingReference(Node* source, Node* target) {
	switch (target->kind) {
	case Kind::ParenthesizedExpression:
	case Kind::NonNullExpression:
		return isMatchingReference(source, target->expression());
	case Kind::BinaryExpression:
		return (isAssignmentExpression(target, false) &&
				isMatchingReference(source, target->as<BinaryExpression>()->Left)) ||
			   (isBinaryExpression(target) &&
				target->as<BinaryExpression>()->OperatorToken->kind == Kind::CommaToken &&
				isMatchingReference(source, target->as<BinaryExpression>()->Right));
	}
	switch (source->kind) {
	case Kind::MetaProperty:
		return isMetaProperty(target) &&
			   source->as<MetaProperty>()->KeywordToken ==
				   target->as<MetaProperty>()->KeywordToken &&
			   source->name()->text() == target->name()->text();
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
		if (isThisInTypeQuery(source)) {
			return target->kind == Kind::ThisKeyword;
		}
		return (isIdentifier(target) &&
				getResolvedSymbol(source) == getResolvedSymbol(target)) ||
			   ((isVariableDeclaration(target) || isBindingElement(target)) &&
				getExportSymbolOfValueSymbolIfExported(getResolvedSymbol(source)) ==
					getSymbolOfDeclaration(target));
	case Kind::ThisKeyword:
		return target->kind == Kind::ThisKeyword;
	case Kind::SuperKeyword:
		return target->kind == Kind::SuperKeyword;
	case Kind::NonNullExpression:
	case Kind::ParenthesizedExpression:
	case Kind::SatisfiesExpression:
		return isMatchingReference(source->expression(), target);
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression: {
		auto [sourcePropertyName, sourceOk] = getAccessedPropertyName(source);
		if (sourceOk) {
			if (isAccessExpression(target)) {
				auto [targetPropertyName, targetOk] = getAccessedPropertyName(target);
				if (targetOk) {
					return targetPropertyName == sourcePropertyName &&
						   isMatchingReference(source->expression(), target->expression());
				}
			}
		}
		if (isElementAccessExpression(source) && isElementAccessExpression(target)) {
			Node* sourceArg = source->as<ElementAccessExpression>()->ArgumentExpression;
			Node* targetArg = target->as<ElementAccessExpression>()->ArgumentExpression;
			if (isIdentifier(sourceArg) && isIdentifier(targetArg)) {
				Symbol* symbol = getResolvedSymbol(sourceArg);
				if (symbol == getResolvedSymbol(targetArg) &&
					(isConstantVariable(symbol) ||
					 (isParameterOrMutableLocalVariable(symbol) &&
					  !isSymbolAssigned(symbol)))) {
					return isMatchingReference(source->expression(), target->expression());
				}
			}
		}
		break;
	}
	case Kind::QualifiedName:
		if (isAccessExpression(target)) {
			auto [targetPropertyName, targetOk] = getAccessedPropertyName(target);
			if (targetOk) {
				return source->as<QualifiedName>()->Right->text() == targetPropertyName &&
					   isMatchingReference(source->as<QualifiedName>()->Left,
										   target->expression());
			}
		}
		break;
	case Kind::BinaryExpression:
		return isBinaryExpression(source) &&
			   source->as<BinaryExpression>()->OperatorToken->kind == Kind::CommaToken &&
			   isMatchingReference(source->as<BinaryExpression>()->Right, target);
	}
	return false;
}

// Return the flow cache key for a "dotted name" (i.e. a sequence of identifiers
// separated by dots). The key consists of the id of the symbol referenced by the
// leftmost identifier followed by zero or more property names separated by dots.
// The result is nonDottedNameCacheKey if the reference isn't a dotted name.
// getFlowReferenceKey (flow.go)
CacheKey Checker::getFlowReferenceKey(FlowState* f) {
	keyBuilder b;
	if (writeFlowCacheKey(this, &b, f->reference, f->declaredType, f->initialType,
						  f->flowContainer)) {
		return b.hash();
	}
	return nonDottedNameCacheKey; // Reference isn't a dotted name
}

// writeFlowCacheKey (flow.go) — file-local because keyBuilder is a TU-local type.
namespace {
bool writeFlowCacheKey(Checker* c, keyBuilder* b, Node* node, Type* declaredType,
							  Type* initialType, Node* flowContainer) {
	switch (node->kind) {
	case Kind::Identifier:
		if (!isThisInTypeQuery(node)) {
			Symbol* symbol = c->getResolvedSymbol(node);
			if (symbol == c->unknownSymbol) {
				return false;
			}
			b->writeSymbol(symbol);
		}
		[[fallthrough]];
	case Kind::ThisKeyword:
		b->writeByte(':');
		b->writeType(declaredType);
		if (initialType != declaredType) {
			b->writeByte('=');
			b->writeType(initialType);
		}
		if (flowContainer != nullptr) {
			b->writeByte('@');
			b->writeNode(flowContainer);
		}
		return true;
	case Kind::NonNullExpression:
	case Kind::ParenthesizedExpression:
		return writeFlowCacheKey(c, b, node->expression(), declaredType, initialType,
								 flowContainer);
	case Kind::QualifiedName:
		if (!writeFlowCacheKey(c, b, node->as<QualifiedName>()->Left, declaredType,
							   initialType, flowContainer)) {
			return false;
		}
		b->writeByte('.');
		b->writeString(node->as<QualifiedName>()->Right->text());
		return true;
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression: {
		if (auto [propName, ok] = c->getAccessedPropertyName(node); ok) {
			if (!writeFlowCacheKey(c, b, node->expression(), declaredType, initialType,
								   flowContainer)) {
				return false;
			}
			b->writeByte('.');
			b->writeString(propName);
			return true;
		}
		if (isElementAccessExpression(node) &&
			isIdentifier(node->as<ElementAccessExpression>()->ArgumentExpression)) {
			Symbol* symbol =
				c->getResolvedSymbol(node->as<ElementAccessExpression>()->ArgumentExpression);
			if (c->isConstantVariable(symbol) ||
				(c->isParameterOrMutableLocalVariable(symbol) &&
				 !c->isSymbolAssigned(symbol))) {
				if (!writeFlowCacheKey(c, b, node->expression(), declaredType, initialType,
									   flowContainer)) {
					return false;
				}
				b->writeString(".@");
				b->writeSymbol(symbol);
				return true;
			}
		}
		break;
	}
	case Kind::ObjectBindingPattern:
	case Kind::ArrayBindingPattern:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::MethodDeclaration:
		b->writeNode(node);
		b->writeByte('#');
		b->writeType(declaredType);
		return true;
	}
	return false;
}
} // namespace

// getAccessedPropertyName (flow.go)
std::pair<std::string, bool> Checker::getAccessedPropertyName(Node* access) {
	if (isPropertyAccessExpression(access)) {
		return {access->name()->text(), true};
	}
	if (isElementAccessExpression(access)) {
		return tryGetElementAccessExpressionName(access->as<ElementAccessExpression>());
	}
	if (isBindingElement(access)) {
		return getDestructuringPropertyName(access);
	}
	if (isParameterDeclaration(access)) {
		const std::vector<Node*>& params = access->parent->parameters();
		auto it = std::find(params.begin(), params.end(), access);
		int index = it == params.end() ? -1 : static_cast<int>(it - params.begin());
		return {std::to_string(index), true};
	}
	return {"", false};
}

// tryGetElementAccessExpressionName (flow.go)
std::pair<std::string, bool> Checker::tryGetElementAccessExpressionName(
	ElementAccessExpression* node) {
	if (isStringOrNumericLiteralLike(node->ArgumentExpression)) {
		return {node->ArgumentExpression->text(), true};
	}
	if (isEntityNameExpression(node->ArgumentExpression)) {
		return tryGetNameFromEntityNameExpression(node->ArgumentExpression);
	}
	return {"", false};
}

// tryGetNameFromEntityNameExpression (flow.go)
std::pair<std::string, bool> Checker::tryGetNameFromEntityNameExpression(Node* node) {
	Symbol* symbol =
		resolveEntityName(node, SymbolFlagsValue, true /*ignoreErrors*/, false, nullptr);
	if (symbol == nullptr ||
		!(isConstantVariable(symbol) || (symbol->flags & SymbolFlagsEnumMember) != 0)) {
		return {"", false};
	}
	Node* declaration = symbol->data->valueDeclaration;
	if (declaration == nullptr) {
		return {"", false};
	}
	Type* t = tryGetTypeFromTypeNode(declaration);
	if (t != nullptr) {
		if (auto [name, ok] = flow_detail::tryGetNameFromType(t); ok) {
			return {name, true};
		}
	}
	// We exclude binding elements because their initializers don't solely determine their types and resolving
	// full types can cause circularities (see https://github.com/microsoft/TypeScript/issues/63192).
	if (hasOnlyExpressionInitializer(declaration) && !isBindingElement(declaration) &&
		isBlockScopedNameDeclaredBeforeUse(declaration, node)) {
		if (Node* initializer = declaration->initializer(); initializer != nullptr) {
			if (Type* initializerType = getTypeOfExpression(initializer);
				initializerType != nullptr) {
				return flow_detail::tryGetNameFromType(initializerType);
			}
		} else if (isEnumMember(declaration)) {
			std::string text;
			bool ok = tryGetTextOfPropertyName(declaration->name(), text);
			return {text, ok};
		}
	}
	return {"", false};
}

// tryGetNameFromType (flow.go:1782) — free fn in Go; distinct from the
// checker.go:19062 member of the same name. Kept in flow_detail to avoid the
// collision (unqualified lookup inside member fns would pick the member).
namespace flow_detail {
std::pair<std::string, bool> tryGetNameFromType(Type* t) {
	if ((t->flags & TypeFlagsUniqueESSymbol) != 0) {
		return {t->AsUniqueESSymbolType()->name, true};
	}
	if ((t->flags & TypeFlagsStringOrNumberLiteral) != 0) {
		return {evalAnyToString(t->AsLiteralType()->value), true};
	}
	return {"", false};
}
}  // namespace flow_detail

// getDestructuringPropertyName (flow.go)
std::pair<std::string, bool> Checker::getDestructuringPropertyName(Node* node) {
	Node* parent = node->parent;
	if (isBindingElement(node) && isObjectBindingPattern(parent)) {
		return getLiteralPropertyNameText(getBindingElementPropertyName(node));
	}
	if (isPropertyAssignment(node) || isShorthandPropertyAssignment(node)) {
		return getLiteralPropertyNameText(node->name());
	}
	if (isArrayLiteralExpression(parent) || isArrayBindingPattern(parent)) {
		const std::vector<Node*>& elements = parent->elements();
		auto it = std::find(elements.begin(), elements.end(), node);
		int index = it == elements.end() ? -1 : static_cast<int>(it - elements.begin());
		return {std::to_string(index), true};
	}
	return {"", false};
}

// getLiteralPropertyNameText (flow.go)
std::pair<std::string, bool> Checker::getLiteralPropertyNameText(Node* name) {
	Type* t = getLiteralTypeFromPropertyName(name);
	if ((t->flags & (TypeFlagsStringLiteral | TypeFlagsNumberLiteral)) != 0) {
		return {evalAnyToString(t->AsLiteralType()->value), true};
	}
	return {"", false};
}

// isConstantReference (flow.go)
bool Checker::isConstantReference(Node* node) {
	switch (node->kind) {
	case Kind::ThisKeyword:
		return true;
	case Kind::Identifier:
		if (!isThisInTypeQuery(node)) {
			Symbol* symbol = getResolvedSymbol(node);
			return isConstantVariable(symbol) ||
				   (isParameterOrMutableLocalVariable(symbol) && !isSymbolAssigned(symbol)) ||
				   (symbol->data->valueDeclaration != nullptr &&
					isFunctionExpression(symbol->data->valueDeclaration));
		}
		break;
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
		// The resolvedSymbol property is initialized by checkPropertyAccess or checkElementAccess before we get here.
		if (isConstantReference(node->expression())) {
			Symbol* symbol = getResolvedSymbolOrNil(node);
			if (symbol != nullptr) {
				return isReadonlySymbol(symbol);
			}
		}
		break;
	case Kind::ObjectBindingPattern:
	case Kind::ArrayBindingPattern: {
		Node* rootDeclaration = getRootDeclaration(node->parent);
		if (isParameterDeclaration(rootDeclaration) ||
			(isVariableDeclaration(rootDeclaration) &&
			 isCatchClause(rootDeclaration->parent))) {
			return !isSomeSymbolAssigned(rootDeclaration);
		}
		return isVariableDeclaration(rootDeclaration) && isVarConstLike(rootDeclaration);
	}
	}
	return false;
}

// containsMatchingReference (flow.go)
bool Checker::containsMatchingReference(Node* source, Node* target) {
	while (isAccessExpression(source)) {
		source = source->expression();
		if (isMatchingReference(source, target)) {
			return true;
		}
	}
	return false;
}

// optionalChainContainsReference (flow.go)
bool Checker::optionalChainContainsReference(Node* source, Node* target) {
	while (isOptionalChain(source)) {
		source = source->expression();
		if (isMatchingReference(source, target)) {
			return true;
		}
	}
	return false;
}

// getReferenceCandidate (flow.go)
Node* Checker::getReferenceCandidate(Node* node) {
	switch (node->kind) {
	case Kind::ParenthesizedExpression:
		return getReferenceCandidate(node->expression());
	case Kind::BinaryExpression:
		switch (node->as<BinaryExpression>()->OperatorToken->kind) {
		case Kind::EqualsToken:
		case Kind::BarBarEqualsToken:
		case Kind::AmpersandAmpersandEqualsToken:
		case Kind::QuestionQuestionEqualsToken:
			return getReferenceCandidate(node->as<BinaryExpression>()->Left);
		case Kind::CommaToken:
			return getReferenceCandidate(node->as<BinaryExpression>()->Right);
		}
	}
	return node;
}

// getReferenceRoot (flow.go)
Node* Checker::getReferenceRoot(Node* node) {
	Node* parent = node->parent;
	if (isParenthesizedExpression(parent) ||
		(isBinaryExpression(parent) &&
		 parent->as<BinaryExpression>()->OperatorToken->kind == Kind::EqualsToken &&
		 parent->as<BinaryExpression>()->Left == node) ||
		(isBinaryExpression(parent) &&
		 parent->as<BinaryExpression>()->OperatorToken->kind == Kind::CommaToken &&
		 parent->as<BinaryExpression>()->Right == node)) {
		return getReferenceRoot(parent);
	}
	return node;
}

// hasMatchingArgument (flow.go)
bool Checker::hasMatchingArgument(Node* expression, Node* reference) {
	for (Node* argument : expression->arguments()) {
		if (isOrContainsMatchingReference(reference, argument) ||
			optionalChainContainsReference(argument, reference)) {
			return true;
		}
	}
	if (isPropertyAccessExpression(expression->expression()) &&
		isOrContainsMatchingReference(reference,
									  expression->expression()->expression())) {
		return true;
	}
	return false;
}

// isOrContainsMatchingReference (flow.go)
bool Checker::isOrContainsMatchingReference(Node* source, Node* target) {
	return isMatchingReference(source, target) || containsMatchingReference(source, target);
}

// Return a new type in which occurrences of the string, number and bigint primitives and placeholder template
// literal types in typeWithPrimitives have been replaced with occurrences of compatible and more specific types
// from typeWithLiterals. This is essentially a limited form of intersection between the two types. We avoid a
// true intersection because it is more costly and, when applied to union types, generates a large number of
// types we don't actually care about.
// replacePrimitivesWithLiterals (flow.go)
Type* Checker::replacePrimitivesWithLiterals(Type* typeWithPrimitives,
											 Type* typeWithLiterals) {
	if (maybeTypeOfKind(typeWithPrimitives,
						TypeFlagsString | TypeFlagsTemplateLiteral | TypeFlagsNumber |
							TypeFlagsBigInt) &&
		maybeTypeOfKind(typeWithLiterals,
						TypeFlagsStringLiteral | TypeFlagsTemplateLiteral |
							TypeFlagsStringMapping | TypeFlagsNumberLiteral |
							TypeFlagsBigIntLiteral)) {
		return mapType(typeWithPrimitives, [this, typeWithLiterals](Type* t) -> Type* {
			if ((t->flags & TypeFlagsString) != 0) {
				return extractTypesOfKind(typeWithLiterals,
										  TypeFlagsString | TypeFlagsStringLiteral |
											  TypeFlagsTemplateLiteral |
											  TypeFlagsStringMapping);
			}
			if (isPatternLiteralType(t) &&
				!maybeTypeOfKind(typeWithLiterals, TypeFlagsString |
													   TypeFlagsTemplateLiteral |
													   TypeFlagsStringMapping)) {
				return extractTypesOfKind(typeWithLiterals, TypeFlagsStringLiteral);
			}
			if ((t->flags & TypeFlagsNumber) != 0) {
				return extractTypesOfKind(typeWithLiterals,
										  TypeFlagsNumber | TypeFlagsNumberLiteral);
			}
			if ((t->flags & TypeFlagsBigInt) != 0) {
				return extractTypesOfKind(typeWithLiterals,
										  TypeFlagsBigInt | TypeFlagsBigIntLiteral);
			}
			return t;
		});
	}
	return typeWithPrimitives;
}

// isCoercibleUnderDoubleEquals (flow.go)
namespace {
bool isCoercibleUnderDoubleEquals(Type* source, Type* target) {
	return (source->flags & (TypeFlagsNumber | TypeFlagsString | TypeFlagsBooleanLiteral)) !=
			   0 &&
		   (target->flags & (TypeFlagsNumber | TypeFlagsString | TypeFlagsBoolean)) != 0;
}
} // namespace

// isExhaustiveSwitchStatement (flow.go)
bool Checker::isExhaustiveSwitchStatement(Node* node) {
	SwitchStatementLinks* links = switchStatementLinks.Get(node);
	if (links->exhaustiveState == ExhaustiveState::Unknown) {
		// Indicate resolution is in process
		links->exhaustiveState = ExhaustiveState::Computing;
		bool isExhaustive = computeExhaustiveSwitchStatement(node);
		if (links->exhaustiveState == ExhaustiveState::Computing) {
			links->exhaustiveState =
				isExhaustive ? ExhaustiveState::True : ExhaustiveState::False;
		}
	} else if (links->exhaustiveState == ExhaustiveState::Computing) {
		// Resolve circularity to false
		links->exhaustiveState = ExhaustiveState::False;
	}
	return links->exhaustiveState == ExhaustiveState::True;
}

// computeExhaustiveSwitchStatement (flow.go)
bool Checker::computeExhaustiveSwitchStatement(Node* node) {
	if (isTypeOfExpression(node->expression())) {
		auto witnesses = getSwitchClauseTypeOfWitnesses(node);
		if (!witnesses.has_value()) {
			return false;
		}
		Type* operandConstraint = getBaseConstraintOrType(
			checkExpressionCached(node->expression()->expression()));
		// Get the not-equal flags for all handled cases.
		TypeFacts notEqualFacts = getNotEqualFactsFromTypeofSwitch(0, 0, *witnesses);
		if ((operandConstraint->flags & TypeFlagsAnyOrUnknown) != 0) {
			// We special case the top types to be exhaustive when all cases are handled.
			return (TypeFactsAllTypeofNE & notEqualFacts) == TypeFactsAllTypeofNE;
		}
		// A missing not-equal flag indicates that the type wasn't handled by some case.
		return !someType(operandConstraint, [this, notEqualFacts](Type* t) {
			return getTypeFacts(t, notEqualFacts) == notEqualFacts;
		});
	}
	Type* t = getBaseConstraintOrType(checkExpressionCached(node->expression()));
	if (!isLiteralType(t)) {
		return false;
	}
	std::vector<Type*> switchTypes = getSwitchClauseTypes(node);
	if (switchTypes.empty() ||
		std::any_of(switchTypes.begin(), switchTypes.end(), isNeitherUnitTypeNorNever)) {
		return false;
	}
	return eachTypeContainedIn(
		mapType(t, [this](Type* t2) { return getRegularTypeOfLiteralType(t2); }),
		switchTypes);
}

// eachTypeContainedIn (flow.go)
bool Checker::eachTypeContainedIn(Type* source, const std::vector<Type*>& types) {
	if ((source->flags & TypeFlagsUnion) != 0) {
		return std::all_of(source->AsUnionType()->types.begin(),
						   source->AsUnionType()->types.end(), [&types](Type* t) {
							   return std::find(types.begin(), types.end(), t) !=
									  types.end();
						   });
	}
	return std::find(types.begin(), types.end(), source) != types.end();
}

// Get the type names from all cases in a switch on `typeof`. The default clause and/or duplicate type names are
// represented as empty strings. Return nil if one or more case clause expressions are not string literals.
// getSwitchClauseTypeOfWitnesses (flow.go)
std::optional<std::vector<std::string>> Checker::getSwitchClauseTypeOfWitnesses(Node* node) {
	SwitchStatementLinks* links = switchStatementLinks.Get(node);
	if (!links->witnessesComputed) {
		const std::vector<Node*>& clauses =
			node->as<SwitchStatement>()->CaseBlock->as<CaseBlock>()->Clauses->nodes;
		std::vector<std::string> witnesses(clauses.size());
		bool ok = true;
		for (size_t i = 0; i < clauses.size(); i++) {
			Node* clause = clauses[i];
			if (clause->kind == Kind::CaseClause) {
				if (!isStringLiteralLike(clause->expression())) {
					ok = false;
					witnesses.clear();
					break;
				}
				std::string text = clause->expression()->text();
				if (std::find(witnesses.begin(), witnesses.end(), text) ==
					witnesses.end()) {
					witnesses[i] = text;
				}
			}
		}
		if (!ok) {
			links->witnessesAreNil = true;
			links->witnesses.clear();
		} else {
			links->witnesses = std::move(witnesses);
		}
		links->witnessesComputed = true;
	}
	if (links->witnessesAreNil) {
		return std::nullopt;
	}
	return links->witnesses;
}

// Return the combined not-equal type facts for all cases except those between the start and end indices.
// getNotEqualFactsFromTypeofSwitch (flow.go)
TypeFacts Checker::getNotEqualFactsFromTypeofSwitch(
	int start, int end, const std::vector<std::string>& witnesses) {
	TypeFacts facts = TypeFactsNone;
	for (size_t i = 0; i < witnesses.size(); i++) {
		const std::string& witness = witnesses[i];
		if ((static_cast<int>(i) < start || static_cast<int>(i) >= end) &&
			!witness.empty()) {
			TypeFacts f = TypeFactsTypeofNEHostObject;
			if (auto it = typeofNEFacts.find(witness); it != typeofNEFacts.end()) {
				f = it->second;
			}
			facts |= f;
		}
	}
	return facts;
}

// getSwitchClauseTypes (flow.go)
std::vector<Type*> Checker::getSwitchClauseTypes(Node* node) {
	SwitchStatementLinks* links = switchStatementLinks.Get(node);
	if (!links->switchTypesComputed) {
		const std::vector<Node*>& clauses =
			node->as<SwitchStatement>()->CaseBlock->as<CaseBlock>()->Clauses->nodes;
		std::vector<Type*> types(clauses.size());
		for (size_t i = 0; i < clauses.size(); i++) {
			types[i] = getTypeOfSwitchClause(clauses[i]);
		}
		links->switchTypes = types;
		links->switchTypesComputed = true;
	}
	return links->switchTypes;
}

// getTypeOfSwitchClause (flow.go)
Type* Checker::getTypeOfSwitchClause(Node* clause) {
	if (clause->kind == Kind::CaseClause) {
		return getRegularTypeOfLiteralType(getTypeOfExpression(clause->expression()));
	}
	return neverType;
}

// getEffectsSignature (flow.go)
Signature* Checker::getEffectsSignature(Node* node) {
	SignatureLinks* links = signatureLinks.Get(node);
	Signature* signature = links->effectsSignature;
	if (signature == nullptr) {
		// A call expression parented by an expression statement is a potential assertion. Other call
		// expressions are potential type predicate function calls. In order to avoid triggering
		// circularities in control flow analysis, we use getTypeOfDottedName when resolving the call
		// target expression of an assertion.
		Type* funcType = nullptr;
		if (isBinaryExpression(node)) {
			Type* rightType = checkNonNullExpression(node->as<BinaryExpression>()->Right);
			funcType = getSymbolHasInstanceMethodOfObjectType(rightType);
		} else if (isExpressionStatement(node->parent)) {
			funcType = getTypeOfDottedName(node->expression(), nullptr /*diagnostic*/);
		} else if (node->expression()->kind != Kind::SuperKeyword) {
			if (isOptionalChain(node)) {
				funcType = checkNonNullType(
					getOptionalExpressionType(checkExpression(node->expression()),
											  node->expression()),
					node->expression());
			} else {
				funcType = checkNonNullExpression(node->expression());
			}
		}
		Type* apparentType = nullptr;
		if (funcType != nullptr) {
			apparentType = getApparentType(funcType);
		}
		std::vector<Signature*> signatures =
			getSignaturesOfType(orElse(apparentType, unknownType), SignatureKind::Call);
		if (signatures.size() == 1 && signatures[0]->typeParameters.empty()) {
			signature = signatures[0];
		} else if (std::any_of(signatures.begin(), signatures.end(), [this](Signature* s) {
					   return hasTypePredicateOrNeverReturnType(s);
				   })) {
			signature = getResolvedSignature(node, nullptr, CheckModeNormal);
		}
		if (!(signature != nullptr && hasTypePredicateOrNeverReturnType(signature))) {
			signature = unknownSignature;
		}
		links->effectsSignature = signature;
	}
	if (signature == unknownSignature) {
		return nullptr;
	}
	return signature;
}

/**
 * Get the type of the `[Symbol.hasInstance]` method of an object type.
 */
// getSymbolHasInstanceMethodOfObjectType (flow.go)
Type* Checker::getSymbolHasInstanceMethodOfObjectType(Type* t) {
	std::string hasInstancePropertyName =
		getPropertyNameForKnownSymbolName("hasInstance");
	if (allTypesAssignableToKind(t, TypeFlagsNonPrimitive)) {
		Symbol* hasInstanceProperty = getPropertyOfType(t, hasInstancePropertyName);
		if (hasInstanceProperty != nullptr) {
			Type* hasInstancePropertyType = getTypeOfSymbol(hasInstanceProperty);
			if (hasInstancePropertyType != nullptr &&
				!getSignaturesOfType(hasInstancePropertyType, SignatureKind::Call).empty()) {
				return hasInstancePropertyType;
			}
		}
	}
	return nullptr;
}

// getPropertyNameForKnownSymbolName (flow.go)
std::string Checker::getPropertyNameForKnownSymbolName(const std::string& symbolName) {
	Symbol* ctorType = getGlobalESSymbolConstructorSymbolOrNil();
	if (ctorType != nullptr) {
		Type* uniqueType = getTypeOfPropertyOfType(getTypeOfSymbol(ctorType), symbolName);
		if (uniqueType != nullptr && isTypeUsableAsPropertyName(uniqueType)) {
			return getPropertyNameFromType(uniqueType);
		}
	}
	return std::string(1, kInternalSymbolNamePrefix) + "@" + symbolName;
}

// We require the dotted function name in an assertion expression to be comprised of identifiers
// that reference function, method, class or value module symbols; or variable, property or
// parameter symbols with declarations that have explicit type annotations. Such references are
// resolvable with no possibility of triggering circularities in control flow analysis.
// getTypeOfDottedName (flow.go)
Type* Checker::getTypeOfDottedName(Node* node, Diagnostic* diagnostic) {
	if ((node->flags & NodeFlagsInWithStatement) == 0) {
		switch (node->kind) {
		case Kind::Identifier: {
			Symbol* symbol =
				getExportSymbolOfValueSymbolIfExported(getResolvedSymbol(node));
			return getExplicitTypeOfSymbol(symbol, diagnostic);
		}
		case Kind::ThisKeyword:
			return getExplicitThisType(node);
		case Kind::SuperKeyword:
			return checkSuperExpression(node);
		case Kind::PropertyAccessExpression: {
			Type* t = getTypeOfDottedName(node->expression(), diagnostic);
			if (t != nullptr) {
				Node* name = node->name();
				Symbol* prop = nullptr;
				if (isPrivateIdentifier(name)) {
					if (t->symbol != nullptr) {
						prop = getPropertyOfType(
							t, getSymbolNameForPrivateIdentifier(t->symbol, name->text()));
					}
				} else {
					prop = getPropertyOfType(t, name->text());
				}
				if (prop != nullptr) {
					return getExplicitTypeOfSymbol(prop, diagnostic);
				}
			}
			break;
		}
		case Kind::ParenthesizedExpression:
			return getTypeOfDottedName(node->expression(), diagnostic);
		}
	}
	return nullptr;
}

// getExplicitTypeOfSymbol (flow.go)
Type* Checker::getExplicitTypeOfSymbol(Symbol* symbol, Diagnostic* diagnostic) {
	symbol = resolveSymbol(symbol);
	if (!resolvingExplicitTypeOfSymbol.insert(symbol).second) {
		return nullptr;
	}
	// Go: defer c.resolvingExplicitTypeOfSymbol.Delete(symbol)
	struct ResolvingGuard {
		std::unordered_set<Symbol*>& set;
		Symbol* symbol;
		~ResolvingGuard() { set.erase(symbol); }
	} guard{resolvingExplicitTypeOfSymbol, symbol};
	if ((symbol->flags & (SymbolFlagsFunction | SymbolFlagsMethod | SymbolFlagsClass |
						  SymbolFlagsValueModule)) != 0) {
		return getTypeOfSymbol(symbol);
	}
	if ((symbol->flags & (SymbolFlagsVariable | SymbolFlagsProperty)) != 0) {
		if ((symbol->checkFlags & CheckFlagsMapped) != 0) {
			Symbol* origin = mappedSymbolLinks.Get(symbol)->syntheticOrigin;
			if (origin != nullptr &&
				getExplicitTypeOfSymbol(origin, diagnostic) != nullptr) {
				return getTypeOfSymbol(symbol);
			}
		}
		Node* declaration = symbol->data->valueDeclaration;
		if (declaration != nullptr) {
			if (isDeclarationWithExplicitTypeAnnotation(declaration)) {
				return getTypeOfSymbol(symbol);
			}
			if (isVariableDeclaration(declaration) &&
				isForOfStatement(declaration->parent->parent)) {
				Node* statement = declaration->parent->parent;
				Type* expressionType =
					getTypeOfDottedName(statement->expression(), nullptr /*diagnostic*/);
				if (expressionType != nullptr) {
					IterationUse use;
					if (statement->as<ForInOrOfStatement>()->AwaitModifier != nullptr) {
						use = IterationUseForAwaitOf;
					} else {
						use = IterationUseForOf;
					}
					return checkIteratedTypeOrElementType(use, expressionType, undefinedType,
														  nullptr /*errorNode*/);
				}
			}
			if (diagnostic != nullptr) {
				diagnostic->AddRelatedInfo(createDiagnosticForNode(
					declaration, X_0_needs_an_explicit_type_annotation,
					{symbolToString(symbol)}));
			}
		}
	}
	return nullptr;
}

// isDeclarationWithExplicitTypeAnnotation (flow.go)
bool Checker::isDeclarationWithExplicitTypeAnnotation(Node* node) {
	return ((isVariableDeclaration(node) || isPropertyDeclaration(node) ||
			isPropertySignatureDeclaration(node) || isParameterDeclaration(node)) &&
			   node->type() != nullptr) ||
		   isExpandoPropertyFunctionWithReturnTypeAnnotation(node);
}

// isExpandoPropertyFunctionWithReturnTypeAnnotation (flow.go)
bool Checker::isExpandoPropertyFunctionWithReturnTypeAnnotation(Node* node) {
	if (isBinaryExpression(node)) {
		if (Node* expr = node->as<BinaryExpression>()->Right;
			isFunctionLike(expr) && expr->type() != nullptr) {
			return true;
		}
	}
	return false;
}

// hasTypePredicateOrNeverReturnType (flow.go)
bool Checker::hasTypePredicateOrNeverReturnType(Signature* sig) {
	return getTypePredicateOfSignature(sig) != nullptr ||
		   (sig->declaration != nullptr &&
			(orElse(getReturnTypeFromAnnotation(sig->declaration), unknownType)->flags &
			 TypeFlagsNever) != 0);
}

// getExplicitThisType (flow.go)
Type* Checker::getExplicitThisType(Node* node) {
	Node* container = getThisContainer(node, false /*includeArrowFunctions*/,
									   false /*includeClassComputedPropertyName*/);
	if (isFunctionLike(container)) {
		Signature* signature = getSignatureFromDeclaration(container);
		if (signature->thisParameter != nullptr) {
			return getExplicitTypeOfSymbol(signature->thisParameter, nullptr);
		}
	}
	if (container->parent != nullptr && isClassLike(container->parent)) {
		Symbol* symbol = getSymbolOfDeclaration(container->parent);
		if (isStatic(container)) {
			return getTypeOfSymbol(symbol);
		} else {
			return getDeclaredTypeOfSymbol(symbol)->AsInterfaceType()->thisType;
		}
	}
	return nullptr;
}

// getInitialType (flow.go)
Type* Checker::getInitialType(Node* node) {
	switch (node->kind) {
	case Kind::VariableDeclaration:
		return getInitialTypeOfVariableDeclaration(node);
	case Kind::BindingElement:
		return getInitialTypeOfBindingElement(node);
	}
	TSC_UNREACHABLE("Unhandled case in getInitialType");
}

// getInitialTypeOfVariableDeclaration (flow.go)
Type* Checker::getInitialTypeOfVariableDeclaration(Node* node) {
	if (node->initializer() != nullptr) {
		return getTypeOfInitializer(node->initializer());
	}
	if (isForInStatement(node->parent->parent)) {
		return stringType;
	}
	if (isForOfStatement(node->parent->parent)) {
		Type* t = checkRightHandSideOfForOf(node->parent->parent);
		if (t != nullptr) {
			return t;
		}
	}
	return errorType;
}

// getTypeOfInitializer (flow.go)
Type* Checker::getTypeOfInitializer(Node* node) {
	// Return the cached type if one is available. If the type of the variable was inferred
	// from its initializer, we'll already have cached the type. Otherwise we compute it now
	// without caching such that transient types are reflected.
	if (typeNodeLinks.Has(node)) {
		TypeNodeLinks* links = typeNodeLinks.Get(node);
		if (links->resolvedType != nullptr &&
			!staleForCheckFile(links->resolvedTypeCheckFile)) {
			return links->resolvedType;
		}
	}
	return getTypeOfExpression(node);
}

// getInitialTypeOfBindingElement (flow.go)
Type* Checker::getInitialTypeOfBindingElement(Node* node) {
	Node* pattern = node->parent;
	Type* parentType = getInitialType(pattern->parent);
	Type* t;
	if (isObjectBindingPattern(pattern)) {
		t = getTypeOfDestructuredProperty(parentType, getBindingElementPropertyName(node));
	} else if (!hasDotDotDotToken(node)) {
		const std::vector<Node*>& elements = pattern->elements();
		auto it = std::find(elements.begin(), elements.end(), node);
		t = getTypeOfDestructuredArrayElement(parentType,
											  static_cast<int>(it - elements.begin()));
	} else {
		t = getTypeOfDestructuredSpreadExpression(parentType);
	}
	return getTypeWithDefault(t, node->initializer());
}

// getAssignedType (flow.go)
Type* Checker::getAssignedType(Node* node) {
	Node* parent = node->parent;
	switch (parent->kind) {
	case Kind::ForInStatement:
		return stringType;
	case Kind::ForOfStatement: {
		Type* t = checkRightHandSideOfForOf(parent);
		if (t != nullptr) {
			return t;
		}
		break;
	}
	case Kind::BinaryExpression:
		return getAssignedTypeOfBinaryExpression(parent);
	case Kind::DeleteExpression:
		return undefinedType;
	case Kind::ArrayLiteralExpression:
		return getAssignedTypeOfArrayLiteralElement(parent, node);
	case Kind::SpreadElement:
		return getAssignedTypeOfSpreadExpression(parent);
	case Kind::PropertyAssignment:
		return getAssignedTypeOfPropertyAssignment(parent);
	case Kind::ShorthandPropertyAssignment:
		return getAssignedTypeOfShorthandPropertyAssignment(parent);
	}
	return errorType;
}

// getAssignedTypeOfBinaryExpression (flow.go)
Type* Checker::getAssignedTypeOfBinaryExpression(Node* node) {
	bool isDestructuringDefaultAssignment =
		(isArrayLiteralExpression(node->parent) &&
		 isDestructuringAssignmentTarget(node->parent)) ||
		(isPropertyAssignment(node->parent) &&
		 isDestructuringAssignmentTarget(node->parent->parent));
	if (isDestructuringDefaultAssignment) {
		return getTypeWithDefault(getAssignedType(node),
								  node->as<BinaryExpression>()->Right);
	}
	return getTypeOfExpression(node->as<BinaryExpression>()->Right);
}

// getAssignedTypeOfArrayLiteralElement (flow.go)
Type* Checker::getAssignedTypeOfArrayLiteralElement(Node* node, Node* element) {
	const std::vector<Node*>& elements = node->elements();
	auto it = std::find(elements.begin(), elements.end(), element);
	return getTypeOfDestructuredArrayElement(getAssignedType(node),
											 static_cast<int>(it - elements.begin()));
}

// getTypeOfDestructuredArrayElement (flow.go)
Type* Checker::getTypeOfDestructuredArrayElement(Type* t, int index) {
	if (everyType(t, [this](Type* t2) { return isTupleLikeType(t2); })) {
		if (Type* elementType = getTupleElementType(t, index); elementType != nullptr) {
			return elementType;
		}
	}
	if (Type* elementType = checkIteratedTypeOrElementType(
			IterationUseDestructuring, t, undefinedType, nullptr /*errorNode*/);
		elementType != nullptr) {
		return includeUndefinedInIndexSignature(elementType);
	}
	return errorType;
}

// includeUndefinedInIndexSignature (flow.go)
Type* Checker::includeUndefinedInIndexSignature(Type* t) {
	if (t == nullptr) {
		return nullptr;
	}
	if (compilerOptions->NoUncheckedIndexedAccess == Tristate::True) {
		return getUnionType({t, missingType});
	}
	return t;
}

// getAssignedTypeOfSpreadExpression (flow.go)
Type* Checker::getAssignedTypeOfSpreadExpression(Node* node) {
	return getTypeOfDestructuredSpreadExpression(getAssignedType(node->parent));
}

// getTypeOfDestructuredSpreadExpression (flow.go)
Type* Checker::getTypeOfDestructuredSpreadExpression(Type* t) {
	Type* elementType = checkIteratedTypeOrElementType(IterationUseDestructuring, t,
													   undefinedType, nullptr /*errorNode*/);
	if (elementType == nullptr) {
		elementType = errorType;
	}
	return createArrayType(elementType);
}

// getAssignedTypeOfPropertyAssignment (flow.go)
Type* Checker::getAssignedTypeOfPropertyAssignment(Node* node) {
	return getTypeOfDestructuredProperty(getAssignedType(node->parent), node->name());
}

// getTypeOfDestructuredProperty (flow.go)
Type* Checker::getTypeOfDestructuredProperty(Type* t, Node* name) {
	Type* nameType = getLiteralTypeFromPropertyName(name);
	if (!isTypeUsableAsPropertyName(nameType)) {
		return errorType;
	}
	std::string text = getPropertyNameFromType(nameType);
	if (Type* propType = getTypeOfPropertyOfType(t, text); propType != nullptr) {
		return propType;
	}
	if (IndexInfo* indexInfo = getApplicableIndexInfoForName(t, text);
		indexInfo != nullptr) {
		return includeUndefinedInIndexSignature(indexInfo->valueType);
	}
	return errorType;
}

// getAssignedTypeOfShorthandPropertyAssignment (flow.go)
Type* Checker::getAssignedTypeOfShorthandPropertyAssignment(Node* node) {
	return getTypeWithDefault(
		getAssignedTypeOfPropertyAssignment(node),
		node->as<ShorthandPropertyAssignment>()->ObjectAssignmentInitializer);
}

// isDestructuringAssignmentTarget (flow.go)
bool Checker::isDestructuringAssignmentTarget(Node* parent) {
	return (isBinaryExpression(parent->parent) &&
			parent->parent->as<BinaryExpression>()->Left == parent) ||
		   (isForOfStatement(parent->parent) &&
			parent->parent->initializer() == parent);
}

// getTypeWithDefault (flow.go)
Type* Checker::getTypeWithDefault(Type* t, Node* defaultExpression) {
	if (defaultExpression != nullptr) {
		return getUnionType({getNonUndefinedType(t), getTypeOfExpression(defaultExpression)});
	}
	return t;
}

// Remove those constituent types of declaredType to which no constituent type of assignedType is assignable.
// For example, when a variable of type number | string | boolean is assigned a value of type number | boolean,
// we remove type string.
// getAssignmentReducedType (flow.go)
Type* Checker::getAssignmentReducedType(Type* declaredType, Type* assignedType) {
	if (declaredType == assignedType) {
		return declaredType;
	}
	if ((assignedType->flags & TypeFlagsNever) != 0) {
		return assignedType;
	}
	AssignmentReducedKey key{declaredType->id, assignedType->id};
	Type* result = nullptr;
	if (auto it = assignmentReducedTypes.find(key); it != assignmentReducedTypes.end()) {
		result = it->second;
	}
	if (result == nullptr) {
		result = getAssignmentReducedTypeWorker(declaredType, assignedType);
		assignmentReducedTypes[key] = result;
	}
	return result;
}

// getAssignmentReducedTypeWorker (flow.go)
Type* Checker::getAssignmentReducedTypeWorker(Type* declaredType, Type* assignedType) {
	Type* filteredType = filterType(declaredType, [this, assignedType](Type* t) {
		return typeMaybeAssignableTo(assignedType, t);
	});
	// Ensure that we narrow to fresh types if the assignment is a fresh boolean literal type.
	Type* reducedType = filteredType;
	if ((assignedType->flags & TypeFlagsBooleanLiteral) != 0 &&
		isFreshLiteralType(assignedType)) {
		reducedType =
			mapType(filteredType, [this](Type* t) { return getFreshTypeOfLiteralType(t); });
	}
	// Our crude heuristic produces an invalid result in some cases: see GH#26130.
	// For now, when that happens, we give up and don't narrow at all.  (This also
	// means we'll never narrow for erroneous assignments where the assigned type
	// is not assignable to the declared type.)
	if (isTypeAssignableTo(assignedType, reducedType)) {
		return reducedType;
	}
	return declaredType;
}

// typeMaybeAssignableTo (flow.go)
bool Checker::typeMaybeAssignableTo(Type* source, Type* target) {
	if ((source->flags & TypeFlagsUnion) == 0) {
		return isTypeAssignableTo(source, target);
	}
	// Quick exit when source union contains the target type
	if (containsType(source->types(), target)) {
		return true;
	}
	// Otherwise, check if any constituent type of the source union is assignable to the target type
	for (Type* t : source->types()) {
		if (isTypeAssignableTo(t, target)) {
			return true;
		}
	}
	return false;
}

// getTypePredicateArgument (flow.go)
Node* Checker::getTypePredicateArgument(TypePredicate* predicate, Node* callExpression) {
	if (predicate->kind == TypePredicateKind::Identifier ||
		predicate->kind == TypePredicateKind::AssertsIdentifier) {
		std::vector<Node*> arguments = callExpression->arguments();
		if (predicate->parameterIndex >= 0 &&
			predicate->parameterIndex < static_cast<int32_t>(arguments.size())) {
			return arguments[predicate->parameterIndex];
		}
	} else {
		Node* invokedExpression = skipParentheses(callExpression->expression());
		if (isAccessExpression(invokedExpression)) {
			return skipParentheses(invokedExpression->expression());
		}
	}
	return nullptr;
}

// getFlowTypeInConstructor (flow.go)
Type* Checker::getFlowTypeInConstructor(Symbol* symbol, Node* constructor) {
	Node* accessName;
	std::string prefixHash = std::string(1, kInternalSymbolNamePrefix) + "#";
	if (symbol->data->name.rfind(prefixHash, 0) == 0) {
		accessName = factory.newPrivateIdentifier(
			symbol->data->name.substr(symbol->data->name.find('@') + 1));
	} else {
		accessName = factory.newIdentifier(symbol->data->name);
	}
	Node* reference = factory.newPropertyAccessExpression(
		factory.newKeywordExpression(Kind::ThisKeyword), nullptr, accessName,
		NodeFlagsNone);
	reference->expression()->parent = reference;
	reference->parent = constructor;
	*reference->flowNodeData().flowNode =
		constructor->as<ConstructorDeclaration>()->ReturnFlowNode;
	Type* flowType = getFlowTypeOfProperty(reference, symbol);
	if (noImplicitAny && (flowType == autoType || flowType == autoArrayType)) {
		error(symbol->data->valueDeclaration, Member_0_implicitly_has_an_1_type,
			  {symbolToString(symbol), TypeToString(flowType)});
	}
	// We don't infer a type if assignments are only null or undefined.
	if (everyType(flowType, [this](Type* t) { return IsNullableType(t); })) {
		return nullptr;
	}
	return convertAutoToAny(flowType);
}

// getFlowTypeInStaticBlocks (flow.go)
Type* Checker::getFlowTypeInStaticBlocks(Symbol* symbol,
										 const std::vector<Node*>& staticBlocks) {
	std::string prefixHash = std::string(1, kInternalSymbolNamePrefix) + "#";
	for (Node* staticBlock : staticBlocks) {
		Node* accessName;
		if (symbol->data->name.rfind(prefixHash, 0) == 0) {
			accessName = factory.newPrivateIdentifier(
				symbol->data->name.substr(symbol->data->name.find('@') + 1));
		} else {
			accessName = factory.newIdentifier(symbol->data->name);
		}
		Node* reference = factory.newPropertyAccessExpression(
			factory.newKeywordExpression(Kind::ThisKeyword), nullptr, accessName,
			NodeFlagsNone);
		reference->expression()->parent = reference;
		reference->parent = staticBlock;
		*reference->flowNodeData().flowNode =
			staticBlock->as<ClassStaticBlockDeclaration>()->ReturnFlowNode;
		Type* flowType = getFlowTypeOfProperty(reference, symbol);
		if (noImplicitAny && (flowType == autoType || flowType == autoArrayType)) {
			error(symbol->data->valueDeclaration, Member_0_implicitly_has_an_1_type,
				  {symbolToString(symbol), TypeToString(flowType)});
		}
		// We don't infer a type if assignments are only null or undefined.
		if (everyType(flowType, [this](Type* t) { return IsNullableType(t); })) {
			continue;
		}
		return convertAutoToAny(flowType);
	}
	return nullptr;
}

// isReachableFlowNode (flow.go)
bool Checker::isReachableFlowNode(FlowNode* flow) {
	FlowState* f = getFlowState();
	bool result = isReachableFlowNodeWorker(f, flow, false /*noCacheCheck*/);
	putFlowState(f);
	lastFlowNode = flow;
	lastFlowNodeReachable = result;
	return result;
}

// isReachableFlowNodeWorker (flow.go)
bool Checker::isReachableFlowNodeWorker(FlowState* f, FlowNode* flow, bool noCacheCheck) {
	for (;;) {
		if (flow == lastFlowNode) {
			return lastFlowNodeReachable;
		}
		FlowFlags flags = flow->flags;
		if ((flags & FlowFlagsShared) != 0) {
			if (!noCacheCheck && f->reduceLabels.empty()) {
				if (auto it = flowNodeReachable.find(flow); it != flowNodeReachable.end()) {
					return it->second;
				}
				bool reachable =
					isReachableFlowNodeWorker(f, flow, true /*noCacheCheck*/);
				flowNodeReachable[flow] = reachable;
				return reachable;
			}
			noCacheCheck = false;
		}
		if ((flags & (FlowFlagsAssignment | FlowFlagsCondition |
					  FlowFlagsArrayMutation)) != 0) {
			flow = flow->antecedent;
		} else if ((flags & FlowFlagsCall) != 0) {
			if (Signature* signature = getEffectsSignature(flow->node);
				signature != nullptr) {
				if (TypePredicate* predicate = getTypePredicateOfSignature(signature);
					predicate != nullptr &&
					predicate->kind == TypePredicateKind::AssertsIdentifier &&
					predicate->t == nullptr) {
					std::vector<Node*> arguments = flow->node->arguments();
					if (predicate->parameterIndex >= 0 &&
						predicate->parameterIndex <
							static_cast<int32_t>(arguments.size()) &&
						isFalseExpression(arguments[predicate->parameterIndex])) {
						return false;
					}
				}
				if ((getReturnTypeOfSignature(signature)->flags & TypeFlagsNever) != 0) {
					return false;
				}
			}
			flow = flow->antecedent;
		} else if ((flags & FlowFlagsBranchLabel) != 0) {
			// A branching point is reachable if any branch is reachable.
			for (FlowList* list = getBranchLabelAntecedents(flow, f->reduceLabels);
				 list != nullptr; list = list->next) {
				if (isReachableFlowNodeWorker(f, list->flow, false /*noCacheCheck*/)) {
					return true;
				}
			}
			return false;
		} else if ((flags & FlowFlagsLoopLabel) != 0) {
			if (flow->antecedents == nullptr) {
				return false;
			}
			// A loop is reachable if the control flow path that leads to the top is reachable.
			flow = flow->antecedents->flow;
		} else if ((flags & FlowFlagsSwitchClause) != 0) {
			// The control flow path representing an unmatched value in a switch statement with
			// no default clause is unreachable if the switch statement is exhaustive.
			FlowSwitchClauseData* data = flow->node->as<FlowSwitchClauseData>();
			if (data->clauseStart == data->clauseEnd &&
				isExhaustiveSwitchStatement(data->switchStatement)) {
				return false;
			}
			flow = flow->antecedent;
		} else if ((flags & FlowFlagsReduceLabel) != 0) {
			// Cache is unreliable once we start adjusting labels
			lastFlowNode = nullptr;
			f->reduceLabels.push_back(flow->node->as<FlowReduceLabelData>());
			bool result =
				isReachableFlowNodeWorker(f, flow->antecedent, false /*noCacheCheck*/);
			f->reduceLabels.pop_back();
			return result;
		} else {
			return (flags & FlowFlagsUnreachable) == 0;
		}
	}
}

// isFalseExpression (flow.go)
bool Checker::isFalseExpression(Node* expr) {
	Node* node = skipParentheses(expr);
	if (node->kind == Kind::FalseKeyword) {
		return true;
	}
	if (isBinaryExpression(node)) {
		BinaryExpression* binary = node->as<BinaryExpression>();
		return (binary->OperatorToken->kind == Kind::AmpersandAmpersandToken &&
				(isFalseExpression(binary->Left) || isFalseExpression(binary->Right))) ||
			   (binary->OperatorToken->kind == Kind::BarBarToken &&
				isFalseExpression(binary->Left) && isFalseExpression(binary->Right));
	}
	return false;
}

// Return true if the given flow node is preceded by a 'super(...)' call in every possible code path
// leading to the node.
// isPostSuperFlowNode (flow.go)
bool Checker::isPostSuperFlowNode(FlowNode* flow, bool noCacheCheck) {
	FlowState* f = getFlowState();
	bool result = isPostSuperFlowNodeWorker(f, flow, noCacheCheck);
	putFlowState(f);
	return result;
}

// isPostSuperFlowNodeWorker (flow.go)
bool Checker::isPostSuperFlowNodeWorker(FlowState* f, FlowNode* flow, bool noCacheCheck) {
	for (;;) {
		FlowFlags flags = flow->flags;
		if ((flags & FlowFlagsShared) != 0) {
			if (!noCacheCheck) {
				if (auto it = flowNodePostSuper.find(flow); it != flowNodePostSuper.end()) {
					return it->second;
				}
				bool postSuper = isPostSuperFlowNodeWorker(f, flow, true /*noCacheCheck*/);
				flowNodePostSuper[flow] = postSuper;
			}
			noCacheCheck = false;
		}
		if ((flags & (FlowFlagsAssignment | FlowFlagsCondition |
					  FlowFlagsArrayMutation | FlowFlagsSwitchClause)) != 0) {
			flow = flow->antecedent;
		} else if ((flags & FlowFlagsCall) != 0) {
			if (flow->node->expression()->kind == Kind::SuperKeyword) {
				return true;
			}
			flow = flow->antecedent;
		} else if ((flags & FlowFlagsBranchLabel) != 0) {
			for (FlowList* list = getBranchLabelAntecedents(flow, f->reduceLabels);
				 list != nullptr; list = list->next) {
				if (!isPostSuperFlowNodeWorker(f, list->flow, false /*noCacheCheck*/)) {
					return false;
				}
			}
			return true;
		} else if ((flags & FlowFlagsLoopLabel) != 0) {
			// A loop is post-super if the control flow path that leads to the top is post-super.
			flow = flow->antecedents->flow;
		} else if ((flags & FlowFlagsReduceLabel) != 0) {
			f->reduceLabels.push_back(flow->node->as<FlowReduceLabelData>());
			bool result =
				isPostSuperFlowNodeWorker(f, flow->antecedent, false /*noCacheCheck*/);
			f->reduceLabels.pop_back();
			return result;
		} else {
			// Unreachable nodes are considered post-super to silence errors
			return (flags & FlowFlagsUnreachable) != 0;
		}
	}
}

// Check if a parameter, catch variable, or mutable local variable is definitely assigned anywhere
// isSymbolAssignedDefinitely (flow.go)
bool Checker::isSymbolAssignedDefinitely(Symbol* symbol) {
	ensureAssignmentsMarked(symbol);
	return markedAssignmentSymbolLinks.Get(symbol)->hasDefiniteAssignment;
}

// Check if a parameter, catch variable, or mutable local variable is assigned anywhere
// isSymbolAssigned (flow.go)
bool Checker::isSymbolAssigned(Symbol* symbol) {
	ensureAssignmentsMarked(symbol);
	return markedAssignmentSymbolLinks.Get(symbol)->lastAssignmentPos != 0;
}

// Return true if there are no assignments to the given symbol or if the given location
// is past the last assignment to the symbol.
// isPastLastAssignment (flow.go)
bool Checker::isPastLastAssignment(Symbol* symbol, Node* location) {
	ensureAssignmentsMarked(symbol);
	int32_t lastAssignmentPos = markedAssignmentSymbolLinks.Get(symbol)->lastAssignmentPos;
	return lastAssignmentPos == 0 ||
		   (location != nullptr && lastAssignmentPos < location->pos());
}

// ensureAssignmentsMarked (flow.go)
void Checker::ensureAssignmentsMarked(Symbol* symbol) {
	Node* parent = findAncestor(symbol->data->valueDeclaration, isFunctionOrSourceFile);
	if (parent == nullptr) {
		return;
	}
	NodeLinks* links = nodeLinks.Get(parent);
	if ((links->flags & NodeCheckFlagsAssignmentsMarked) == 0) {
		links->flags |= NodeCheckFlagsAssignmentsMarked;
		if (!hasParentWithAssignmentsMarked(parent)) {
			markNodeAssignments(parent);
		}
	}
}

// hasParentWithAssignmentsMarked (flow.go)
bool Checker::hasParentWithAssignmentsMarked(Node* node) {
	return findAncestor(node->parent, [this](Node* node) {
			   return isFunctionOrSourceFile(node) &&
					  (nodeLinks.Get(node)->flags & NodeCheckFlagsAssignmentsMarked) != 0;
		   }) != nullptr;
}

// For all assignments within the given root node, record the last assignment source position for all
// referenced parameters and mutable local variables. When assignments occur in nested functions  or
// references occur in export specifiers, record math.MaxInt32 as the assignment position. When
// assignments occur in compound statements, record the ending source position of the compound statement
// as the assignment position (this is more conservative than full control flow analysis, but requires
// only a single walk over the AST).
// markNodeAssignmentsWorker (flow.go)
bool Checker::markNodeAssignmentsWorker(Node* node) {
	switch (node->kind) {
	case Kind::Identifier: {
		AssignmentKind assignmentKind = getAssignmentTargetKind(node);
		if (assignmentKind != AssignmentKind::None) {
			Symbol* symbol = getResolvedSymbol(node);
			if (isParameterOrMutableLocalVariable(symbol)) {
				MarkedAssignmentSymbolLinks* links =
					markedAssignmentSymbolLinks.Get(symbol);
				if (int32_t pos = links->lastAssignmentPos;
					pos == 0 || pos != INT32_MAX) {
					Node* referencingFunction =
						findAncestor(node, isFunctionOrSourceFile);
					Node* declaringFunction =
						findAncestor(symbol->data->valueDeclaration, isFunctionOrSourceFile);
					if (referencingFunction == declaringFunction) {
						links->lastAssignmentPos = static_cast<int32_t>(
							extendAssignmentPosition(node, symbol->data->valueDeclaration));
					} else {
						links->lastAssignmentPos = INT32_MAX;
					}
				}
				if (assignmentKind == AssignmentKind::Definite) {
					links->hasDefiniteAssignment = true;
				}
			}
		}
		return false;
	}
	case Kind::ExportSpecifier: {
		ExportDeclaration* exportDeclaration =
			node->as<ExportSpecifier>()->parent->parent->as<ExportDeclaration>();
		Node* name = node->propertyNameOrName();
		if (!node->isTypeOnly() && !exportDeclaration->IsTypeOnly &&
			exportDeclaration->ModuleSpecifier == nullptr && !isStringLiteral(name)) {
			Symbol* symbol = resolveEntityName(
				name, SymbolFlagsValue, true /*ignoreErrors*/,
				true /*dontResolveAlias*/, nullptr);
			if (symbol != nullptr && isParameterOrMutableLocalVariable(symbol)) {
				MarkedAssignmentSymbolLinks* links =
					markedAssignmentSymbolLinks.Get(symbol);
				links->lastAssignmentPos = INT32_MAX;
			}
		}
		return false;
	}
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::EnumDeclaration:
		return false;
	}
	if (isTypeNode(node)) {
		return false;
	}
	return node->forEachChild(markNodeAssignments);
}

// Extend the position of the given assignment target node to the end of any intervening variable statement,
// expression statement, compound statement, or class declaration occurring between the node and the given
// declaration node.
// extendAssignmentPosition (flow.go)
int32_t Checker::extendAssignmentPosition(Node* node, Node* declaration) {
	int32_t pos = node->pos();
	while (node != nullptr && node->pos() > declaration->pos()) {
		switch (node->kind) {
		case Kind::VariableStatement:
		case Kind::ExpressionStatement:
		case Kind::IfStatement:
		case Kind::DoStatement:
		case Kind::WhileStatement:
		case Kind::ForStatement:
		case Kind::ForInStatement:
		case Kind::ForOfStatement:
		case Kind::WithStatement:
		case Kind::SwitchStatement:
		case Kind::TryStatement:
		case Kind::ClassDeclaration:
			pos = node->end();
			break;
		}
		node = node->parent;
	}
	return pos;
}

// === dep stubs ===
// Callees owned by other slices; each resolves to a real definition once its
// owner slice lands. Declared in the "// === slice: flow ===" block of checker.h.

// (deduped: IsNullableType defined in the owning slice file)

// (deduped: allTypesAssignableToKind defined in the owning slice file)

// (deduped: checkIteratedTypeOrElementType defined in the owning slice file)

// (deduped: checkNonNullType defined in the owning slice file)

// (deduped: convertAutoToAny defined in the owning slice file)

// (deduped: extractTypesOfKind defined in the owning slice file)

// (deduped: getConstituentTypeForKeyType defined in the owning slice file)

// (deduped: getFlowTypeOfProperty defined in owning slice file)

// (deduped: getKeyPropertyName defined in the owning slice file)

// (deduped: getNarrowableTypeForReference defined in the owning slice file)

// (deduped: getNonUndefinedType defined in the owning slice file)

// (deduped: getRegularTypeOfObjectLiteral defined in the owning slice file)

// (deduped: getResolvedSignature defined in the owning slice file)

// (deduped: getTypeFacts defined in the owning slice file)

// (deduped: getTypeOfPropertyOrIndexSignatureOfType defined in the owning slice file)

// (deduped: getTypeWithFacts defined in the owning slice file)

// (deduped: getAdjustedTypeWithFacts defined in the owning slice file)

// (deduped: isConstructorType defined in the owning slice file)

// (deduped: isDiscriminantProperty defined in the owning slice file)

// (deduped: isFunctionType defined in the owning slice file)

// (deduped: isSomeSymbolAssigned defined in the owning slice file)

// isUniformUnionType defined in checker_typeops.cpp; recombineUnknownType
// defined in checker_contextual.cpp.

} // namespace tsc::checker
