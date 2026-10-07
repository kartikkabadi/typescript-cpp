// Port of tsc/internal/checker/checker.go:29385-32069 — contextual typing,
// type facts, awaited types (slice: contextual).
// Every function in the Go range, in Go order.
#include "internal/checker/checker.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "internal/ast/precedence.h"
#include "internal/checker/mapper.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/jsnum/jsnum.h"
#include "internal/scanner/scanner.h"

namespace tsc::checker {

// Free helpers used across checker translation units (defined in checker.cpp).
Diagnostic* NewDiagnosticChainForNode(Diagnostic* chain, Node* node,
									const DiagnosticMessage* message,
									const std::vector<std::string>& args = {});

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

// core.Some
template <class R, class F>
bool someRange(R&& v, F f) {
	using T = std::decay_t<std::ranges::range_value_t<R>>;
	return std::any_of(v.begin(), v.end(), f);
}

// core.Every
template <class R, class F>
bool everyRange(R&& v, F f) {
	using T = std::decay_t<std::ranges::range_value_t<R>>;
	return std::all_of(v.begin(), v.end(), f);
}

// core.Map
template <class R, class F>
auto mapRange(R&& v, F f) -> std::vector<std::invoke_result_t<F, std::decay_t<std::ranges::range_value_t<R>>>> {
	using T = std::decay_t<std::ranges::range_value_t<R>>;
	std::vector<std::invoke_result_t<F, T>> out;
	out.reserve(v.size());
	for (const T& x : v) {
		out.push_back(f(x));
	}
	return out;
}

// core.Filter
template <class R, class F>
auto filterRange(R&& v, F f)
	-> std::vector<std::decay_t<std::ranges::range_value_t<R>>> {
	using T = std::decay_t<std::ranges::range_value_t<R>>;
	std::vector<T> out;
	for (const T& x : v) {
		if (f(x)) {
			out.push_back(x);
		}
	}
	return out;
}

// core.Find — first element satisfying pred, or nullptr.
template <class R, class F>
auto findRange(R&& v, F f)
	-> std::decay_t<std::ranges::range_value_t<R>> {
	using T = std::decay_t<std::ranges::range_value_t<R>>;
	auto it = std::find_if(v.begin(), v.end(), f);
	return it != v.end() ? *it : T{};
}

// core.FindIndex — index of first element satisfying pred, or -1.
template <class R, class F>
int findIndexRange(R&& v, F f) {
	using T = std::decay_t<std::ranges::range_value_t<R>>;
	auto it = std::find_if(v.begin(), v.end(), f);
	return it != v.end() ? static_cast<int>(it - v.begin()) : -1;
}

// slices.Index — index of the first occurrence of value, or -1.
template <class R>
int indexOfRange(R&& v, const auto& value) {
	using T = std::decay_t<std::ranges::range_value_t<R>>;
	auto it = std::find(v.begin(), v.end(), value);
	return it != v.end() ? static_cast<int>(it - v.begin()) : -1;
}

// core.FirstOrNil
template <class R>
auto firstOrNil(R&& v)
	-> std::decay_t<std::ranges::range_value_t<R>> {
	using T = std::decay_t<std::ranges::range_value_t<R>>;
	return v.empty() ? T{} : v.front();
}

// core.LastOrNil
template <class R>
auto lastOrNil(R&& v)
	-> std::decay_t<std::ranges::range_value_t<R>> {
	using T = std::decay_t<std::ranges::range_value_t<R>>;
	return v.empty() ? T{} : v.back();
}

// slices.Contains
template <class R>
bool containsElement(R&& v, const auto& value) {
	using T = std::decay_t<std::ranges::range_value_t<R>>;
	return std::find(v.begin(), v.end(), value) != v.end();
}

// core.ReplaceElement
template <class R>
auto replaceElement(R&& v, size_t index, std::decay_t<std::ranges::range_value_t<R>> element)
	-> std::vector<std::decay_t<std::ranges::range_value_t<R>>> {
	using T = std::decay_t<std::ranges::range_value_t<R>>;
	std::vector<T> out = v;
	out[index] = element;
	return out;
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

// containsType — utilities.go
bool containsType(const std::vector<Type*>& list, Type* t) {
	return std::find(list.begin(), list.end(), t) != list.end();
}

// types.go:740 — Type.Distributed
std::vector<Type*> distributed(Type* t) {
	if (t->flags & TypeFlagsUnion) {
		return t->AsUnionType()->types;
	}
	if (t->flags & TypeFlagsNever) {
		return {};
	}
	return {t};
}

// checker.go:17358 — signatureHasRestParameter
bool signatureHasRestParameter(Signature* sig) {
	return (sig->flags & SignatureFlagsHasRestParameter) != 0;
}

// checker/utilities.go:272 — hasDotDotDotToken
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

// utilities.go — isTupleType (file-local copy; also file-local in typenodes)
bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 &&
		   (t->AsTypeReference()->target->objectFlags & ObjectFlagsTuple) != 0;
}

// declchecks — targetTupleType
TupleType* targetTupleType(Type* t) {
	return t->AsTypeReference()->target->AsTupleType();
}

// typenodes — getEndElementCount: count of ending consecutive tuple elements of
// the given kind(s)
int getEndElementCount(TupleType* t, ElementFlags flags) {
	for (size_t i = t->elementInfos.size(); i > 0; i--) {
		if (!(t->elementInfos[i - 1].flags & flags)) {
			return static_cast<int>(t->elementInfos.size() - i);
		}
	}
	return static_cast<int>(t->elementInfos.size());
}

// ast.IndexOfNode — utilities.go:3783; nodes are sorted by position
int indexOfNode(const std::vector<Node*>& nodes, Node* node) {
	auto it = std::lower_bound(nodes.begin(), nodes.end(), node,
		[](Node* a, Node* b) {
			return compareTextRanges(a->loc, b->loc) < 0;
		});
	if (it != nodes.end() && compareTextRanges((*it)->loc, node->loc) == 0) {
		return static_cast<int>(it - nodes.begin());
	}
	return -1;
}

// isUnaryExpressionKind / isExpressionKind / isExpression — ast.h (utilities.go:415-451).

// ast.HasContextSensitiveParameters — utilities.go:4238
bool hasContextSensitiveParameters(Node* node) {
	// Functions with type parameters are not context sensitive.
	if (node->typeParameterList() == nullptr) {
		// Functions with any parameters that lack type annotations are context sensitive.
		if (someRange(node->parameters(),
					  [](Node* p) { return p->type() == nullptr; })) {
			return true;
		}
		if (!isArrowFunction(node)) {
			// If the first parameter is not an explicit 'this' parameter, then the function has
			// an implicit 'this' parameter which is subject to contextual typing.
			Node* parameter = firstOrNil(node->parameters());
			if (parameter == nullptr || !isThisParameter(parameter)) {
				return (node->flags & NodeFlagsContainsThis) != 0;
			}
		}
	}
	return false;
}

// inference.go:1651
bool hasInferenceCandidates(InferenceInfo* info) {
	return !info->candidates.empty() || !info->contraCandidates.empty();
}

// inference.go:1659
bool hasTypeParameterDefault(Type* tp) {
	if (tp->symbol != nullptr) {
		for (Node* d : tp->symbol->declarations) {
			if (isTypeParameterDeclaration(d) &&
				d->as<TypeParameterDeclaration>()->DefaultType != nullptr) {
				return true;
			}
		}
	}
	return false;
}

// inference.go:1655
bool hasInferenceCandidatesOrDefault(InferenceInfo* info) {
	return hasInferenceCandidates(info) || hasTypeParameterDefault(info->typeParameter);
}

// binder.go:374 — GetSymbolNameForPrivateIdentifier
std::string getSymbolNameForPrivateIdentifier(Symbol* containingClassSymbol,
											 const std::string& description) {
	return std::string(1, kInternalSymbolNamePrefix) + "#" +
		   std::to_string(static_cast<uint64_t>(getSymbolId(containingClassSymbol))) +
		   "@" + description;
}

// utilities.go:286 — IsTypeAny
// isTypeAny — canonical def in checker_utilities.cpp

// checker.cpp — literal value accessors (file-local copies)
std::string getStringLiteralValue(Type* t) {
	return std::get<std::string>(t->AsLiteralType()->value);
}

Number getNumberLiteralValue(Type* t) {
	return std::get<Number>(t->AsLiteralType()->value);
}

PseudoBigInt getBigIntLiteralValue(Type* t) {
	return std::get<PseudoBigInt>(t->AsLiteralType()->value);
}

// checker.cpp:2666 — isTypeUsableAsPropertyName
bool isTypeUsableAsPropertyName(Type* t) {
	return t->flags & TypeFlagsStringOrNumberLiteralOrUnique;
}

// checker.cpp:2696 — Gets the symbolic name for a member from its type.
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

// checker.go:31621 — isZeroBigInt (defined later in this file, in Go order)
bool isZeroBigInt(Type* t);

// checker.go:30631 — isSpreadArgument
bool isSpreadArgument(Node* arg) {
	return isSpreadElement(arg) ||
		   (isSyntheticExpression(arg) && arg->as<SyntheticExpression>()->IsSpread);
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
		default:
			break;
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

} // namespace

// checker.go:29385 — GetPromisedTypeOfPromise
Type* Checker::GetPromisedTypeOfPromise(Type* t) {
	return getPromisedTypeOfPromiseEx(t, nullptr, nullptr);
}

// Gets the "promised type" of a promise.
// @param type The type of the promise.
// @remarks The "promised type" of a type is the type of the "value" parameter of the "onfulfilled" callback.
// checker.go:29389
Type* Checker::getPromisedTypeOfPromiseEx(Type* t, Node* errorNode,
										  Type** thisTypeForErrorOut) {
	//  { // type
	//      then( // thenFunction
	//          onfulfilled: ( // onfulfilledParameterType
	//              value: T // valueParameterType
	//          ) => any
	//      ): any;
	//  }
	if (isTypeAny(t)) {
		return nullptr;
	}
	CachedTypeKey key{CachedTypeKind::PromisedTypeOfPromise, t->id};
	Type* cached = nullptr;
	if (auto it = cachedTypes.find(key); it != cachedTypes.end()) { cached = it->second; }
	if (cached != nullptr) {
		return cached;
	}
	if (isReferenceToType(t, getGlobalPromiseType())) {
		Type* result = getTypeArguments(t)[0];
		cachedTypes[key] = result;
		return result;
	}
	// primitives with a `{ then() }` won't be unwrapped/adopted.
	if (allTypesAssignableToKindEx(getBaseConstraintOrType(t),
								   TypeFlagsPrimitive | TypeFlagsNever, false)) {
		return nullptr;
	}
	Type* thenFunction = getTypeOfPropertyOfType(t, "then");
	// TODO: GH#18217
	if (isTypeAny(thenFunction)) {
		return nullptr;
	}
	std::vector<Signature*> thenSignatures;
	if (thenFunction != nullptr) {
		thenSignatures = getSignaturesOfType(thenFunction, SignatureKind::Call);
	}
	if (thenSignatures.empty()) {
		if (errorNode != nullptr) {
			error(errorNode, A_promise_must_have_a_then_method);
		}
		return nullptr;
	}
	Type* thisTypeForError = nullptr;
	std::vector<Signature*> candidates;
	for (Signature* thenSignature : thenSignatures) {
		Type* thisType = getThisTypeOfSignature(thenSignature);
		if (thisType != nullptr && thisType != voidType &&
			!isTypeRelatedTo(t, thisType, subtypeRelation)) {
			thisTypeForError = thisType;
		} else {
			candidates.push_back(thenSignature);
		}
	}
	if (candidates.empty()) {
		TSC_ASSERT(thisTypeForError != nullptr, "thisTypeForError != nullptr");
		if (thisTypeForErrorOut != nullptr) {
			*thisTypeForErrorOut = thisTypeForError;
		}
		if (errorNode != nullptr) {
			error(errorNode,
				  The_this_context_of_type_0_is_not_assignable_to_method_s_this_of_type_1,
				  {TypeToString(t), TypeToString(thisTypeForError)});
		}
		return nullptr;
	}
	Type* onfulfilledParameterType = getTypeWithFacts(
		getUnionType(mapRange(candidates,
							  [this](Signature* s) {
								  return getTypeOfFirstParameterOfSignature(s);
							  })),
		TypeFactsNEUndefinedOrNull);
	if (isTypeAny(onfulfilledParameterType)) {
		return nullptr;
	}
	std::vector<Signature*> onfulfilledParameterSignatures =
		getSignaturesOfType(onfulfilledParameterType, SignatureKind::Call);
	if (onfulfilledParameterSignatures.empty()) {
		if (errorNode != nullptr) {
			error(errorNode,
				  The_first_parameter_of_the_then_method_of_a_promise_must_be_a_callback);
		}
		return nullptr;
	}
	Type* result = getUnionTypeEx(
		mapRange(onfulfilledParameterSignatures,
				 [this](Signature* s) {
					 return getTypeOfFirstParameterOfSignature(s);
				 }),
		UnionReduction::Subtype, nullptr, nullptr);
	cachedTypes[key] = result;
	return result;
}

// checker.go:29468 — getTypeOfFirstParameterOfSignature
Type* Checker::getTypeOfFirstParameterOfSignature(Signature* signature) {
	return getTypeOfFirstParameterOfSignatureWithFallback(signature, neverType);
}

// checker.go:29472 — getTypeOfFirstParameterOfSignatureWithFallback
Type* Checker::getTypeOfFirstParameterOfSignatureWithFallback(
	Signature* signature, Type* fallbackType) {
	if (!signature->parameters.empty()) {
		return getTypeAtPosition(signature, 0);
	}
	return fallbackType;
}

// checker.go:29478 — getMappedTypeModifiers (extern; decl lives in
// checker_members.cpp, def was a stub there — moved here)
MappedTypeModifiers getMappedTypeModifiers(Type* t) {
	Node* declaration = t->AsMappedType()->declaration;
	MappedTypeModifiers modifiers = 0;
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

namespace {

// Return -1, 0, or 1, where -1 means optionality is stripped (i.e. -?), 0 means optionality is unchanged, and 1 means
// optionality is added (i.e. +?).
// checker.go:29492
int getMappedTypeOptionality(Type* t) {
	MappedTypeModifiers modifiers = getMappedTypeModifiers(t);
	if (modifiers & MappedTypeModifiersExcludeOptional) {
		return -1;
	}
	if (modifiers & MappedTypeModifiersIncludeOptional) {
		return 1;
	}
	return 0;
}

} // namespace

// Return -1, 0, or 1, for stripped, unchanged, or added optionality respectively. When a homomorphic mapped type doesn't
// modify optionality, recursively consult the optionality of the type being mapped over to see if it strips or adds optionality.
// For intersections, return -1 or 1 when all constituents strip or add optionality, otherwise return 0.
// checker.go:29509
int Checker::getCombinedMappedTypeOptionality(Type* t) {
	if (t->objectFlags & ObjectFlagsMapped) {
		int optionality = getMappedTypeOptionality(t);
		if (optionality != 0) {
			return optionality;
		}
		return getCombinedMappedTypeOptionality(getModifiersTypeFromMappedType(t));
	}
	if (t->flags & TypeFlagsIntersection) {
		int optionality = getCombinedMappedTypeOptionality(t->types()[0]);
		for (Type* u : std::vector<Type*>(t->types().begin() + 1, t->types().end())) {
			if (getCombinedMappedTypeOptionality(u) != optionality) {
				return 0;
			}
		}
		return optionality;
	}
	return 0;
}

namespace {

// checker.go:29528 — isPartialMappedType
bool isPartialMappedType(Type* t) {
	return (t->objectFlags & ObjectFlagsMapped) != 0 &&
		   (getMappedTypeModifiers(t) & MappedTypeModifiersIncludeOptional) != 0;
}

} // namespace

// checker.go:29532 — getOptionalExpressionType
Type* Checker::getOptionalExpressionType(Type* exprType, Node* expression) {
	if (isExpressionOfOptionalChainRoot(expression)) {
		return GetNonNullableType(exprType);
	}
	if (isOptionalChain(expression)) {
		return removeOptionalTypeMarker(exprType);
	}
	return exprType;
}

// checker.go:29543 — removeOptionalTypeMarker
Type* Checker::removeOptionalTypeMarker(Type* t) {
	if (strictNullChecks) {
		return removeType(t, optionalType);
	}
	return t;
}

// checker.go:29550 — propagateOptionalTypeMarker
Type* Checker::propagateOptionalTypeMarker(Type* t, Node* node, bool wasOptional) {
	if (wasOptional) {
		if (isOutermostOptionalChain(node)) {
			return getOptionalType(t, false);
		}
		return addOptionalTypeMarker(t);
	}
	return t;
}

// checker.go:29560 — removeMissingType
Type* Checker::removeMissingType(Type* t, bool isOptional) {
	if (exactOptionalPropertyTypes && isOptional) {
		return removeType(t, missingType);
	}
	return t;
}

// checker.go:29567 — removeMissingOrUndefinedType
Type* Checker::removeMissingOrUndefinedType(Type* t) {
	if (exactOptionalPropertyTypes) {
		return removeType(t, missingType);
	}
	return getTypeWithFacts(t, TypeFactsNEUndefined);
}

// checker.go:29574 — removeDefinitelyFalsyTypes
Type* Checker::removeDefinitelyFalsyTypes(Type* t) {
	return filterType(t, [this](Type* t) { return hasTypeFacts(t, TypeFactsTruthy); });
}

// checker.go:29578 — extractDefinitelyFalsyTypes
Type* Checker::extractDefinitelyFalsyTypes(Type* t) {
	return mapType(t, [this](Type* t) { return getDefinitelyFalsyPartOfType(t); });
}

// checker.go:29582 — getDefinitelyFalsyPartOfType
Type* Checker::getDefinitelyFalsyPartOfType(Type* t) {
	if (t->flags & TypeFlagsString) {
		return emptyStringType;
	}
	if (t->flags & TypeFlagsNumber) {
		return zeroType;
	}
	if (t->flags & TypeFlagsBigInt) {
		return zeroBigIntType;
	}
	if (t == regularFalseType || t == falseType ||
		(t->flags &
		 (TypeFlagsVoid | TypeFlagsUndefined | TypeFlagsNull |
		  TypeFlagsAnyOrUnknown)) != 0 ||
		((t->flags & TypeFlagsStringLiteral) != 0 &&
		 getStringLiteralValue(t).empty()) ||
		((t->flags & TypeFlagsNumberLiteral) != 0 &&
		 getNumberLiteralValue(t) == Number(0)) ||
		((t->flags & TypeFlagsBigIntLiteral) != 0 && isZeroBigInt(t))) {
		return t;
	}
	return neverType;
}

// checker.go:29600 — getConstraintDeclaration
Node* Checker::getConstraintDeclaration(Type* t) {
	if (t->symbol != nullptr) {
		for (Node* d : t->symbol->declarations) {
			if (isTypeParameterDeclaration(d)) {
				if (Node* constraint =
						d->as<TypeParameterDeclaration>()->Constraint;
					constraint != nullptr) {
					return constraint;
				}
			}
		}
	}
	return nullptr;
}

// NOTE: getTemplateLiteralType, getTemplateStringForType, getStringMappingType,
// applyStringMapping, applyTemplateStringMapping (checker.go:29615-29786) are
// already ported in checker.cpp — not duplicated here.

// checker.go:29788 — getStringMappingTypeForGenericType
Type* Checker::getStringMappingTypeForGenericType(Symbol* symbol, Type* t) {
	StringMappingKey key{symbol, t};
	Type* result = nullptr;
	if (auto it = stringMappingTypes.find(key); it != stringMappingTypes.end()) { result = it->second; }
	if (result == nullptr) {
		result = newStringMappingType(symbol, t);
		stringMappingTypes[key] = result;
	}
	return result;
}

// Given an indexed access on a mapped type of the form { [P in K]: E }[X], return an instantiation of E where P is
// replaced with X. Since this simplification doesn't account for mapped type modifiers, add 'undefined' to the
// resulting type if the mapped type includes a '?' modifier or if the modifiers type indicates that some properties
// are optional. If the modifiers type is generic, conservatively estimate optionality by recursively looking for
// mapped types that include '?' modifiers.
// checker.go:29801
Type* Checker::substituteIndexedMappedType(Type* objectType, Type* index) {
	TypeMapper* mapper =
		newSimpleTypeMapper(getTypeParameterFromMappedType(objectType), index);
	TypeMapper* templateMapper =
		combineTypeMappers(objectType->AsMappedType()->mapper, mapper);
	Type* instantiatedTemplateType = instantiateType(
		getTemplateTypeFromMappedType(
			orElse(objectType->AsMappedType()->target, objectType)),
		templateMapper);
	bool isOptional = getMappedTypeOptionality(objectType) > 0;
	if (!isOptional) {
		if (isGenericType(objectType)) {
			isOptional =
				getCombinedMappedTypeOptionality(
					getModifiersTypeFromMappedType(objectType)) > 0;
		} else {
			isOptional = couldAccessOptionalProperty(objectType, index);
		}
	}
	return addOptionalityEx(instantiatedTemplateType, true /*isProperty*/,
							isOptional);
}

// Return true if an indexed access with the given object and index types could access an optional property.
// checker.go:29824
bool Checker::couldAccessOptionalProperty(Type* objectType, Type* indexType) {
	Type* indexConstraint = getBaseConstraintOfType(indexType);
	return indexConstraint != nullptr &&
		   someRange(getPropertiesOfType(objectType), [this,
													   indexConstraint](Symbol* p) {
			   return (p->flags & SymbolFlagsOptional) != 0 &&
					  isTypeAssignableTo(
						  getLiteralTypeFromProperty(
							  p, TypeFlagsStringOrNumberLiteralOrUnique, false),
						  indexConstraint);
		   });
}

// checker.go:29831 — getTypeOfPropertyOrIndexSignatureOfType
Type* Checker::getTypeOfPropertyOrIndexSignatureOfType(
	Type* t, const std::string& name) {
	Type* propType = getTypeOfPropertyOfType(t, name);
	if (propType != nullptr) {
		return propType;
	}
	IndexInfo* indexInfo = getApplicableIndexInfoForName(t, name);
	if (indexInfo != nullptr) {
		return addOptionalityEx(indexInfo->valueType, true /*isProperty*/,
								true /*isOptional*/);
	}
	return nullptr;
}

/**
 * Whoa! Do you really want to use this function?
 *
 * Unless you're trying to get the *non-apparent* type for a
 * value-literal type or you're authoring relevant portions of this algorithm,
 * you probably meant to use 'getApparentTypeOfContextualType'.
 * Otherwise this may not be very useful.
 *
 * In cases where you *are* working on this function, you should understand
 * when it is appropriate to use 'getContextualType' and 'getApparentTypeOfContextualType'.
 *
 *   - Use 'getContextualType' when you are simply going to propagate the result to the expression.
 *   - Use 'getApparentTypeOfContextualType' when you're going to need the members of the type.
 *
 * @param node the expression whose contextual type will be returned.
 * @returns the contextual type of an expression.
 */
// checker.go:29841
Type* Checker::getContextualType(Node* node, ContextFlags contextFlags) {
	if (node->flags & NodeFlagsInWithStatement) {
		// We cannot answer semantic questions within a with block, do not proceed any further
		return nullptr;
	}
	// Cached contextual types are obtained with no ContextFlags, so we can only consult them for
	// requests with no ContextFlags.
	int index = findContextualNode(node, contextFlags == ContextFlagsNone /*includeCaches*/);
	if (index >= 0) {
		return contextualInfos[index].t;
	}
	Node* parent = node->parent;
	switch (parent->kind) {
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::BindingElement:
		return getContextualTypeForInitializerExpression(node, contextFlags);
	case Kind::ArrowFunction:
	case Kind::ReturnStatement:
		return getContextualTypeForReturnExpression(node, contextFlags);
	case Kind::YieldExpression:
		return getContextualTypeForYieldOperand(parent, contextFlags);
	case Kind::AwaitExpression:
		return getContextualTypeForAwaitOperand(parent, contextFlags);
	case Kind::CallExpression:
	case Kind::NewExpression:
		return getContextualTypeForArgument(parent, node);
	case Kind::Decorator:
		return getContextualTypeForDecorator(parent);
	case Kind::TypeAssertionExpression:
	case Kind::AsExpression:
		if (isConstAssertion(parent)) {
			return getContextualType(parent, contextFlags);
		}
		return getTypeFromTypeNode(parent->type());
	case Kind::BinaryExpression:
		return getContextualTypeForBinaryOperand(node, contextFlags);
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
		return getContextualTypeForObjectLiteralElement(parent, contextFlags);
	case Kind::SpreadAssignment:
		return getContextualType(parent->parent, contextFlags);
	case Kind::ArrayLiteralExpression: {
		Type* t = getApparentTypeOfContextualType(parent, contextFlags);
		int elementIndex = indexOfNode(parent->elements(), node);
		if (elementIndex < 0) {
			return nullptr;
		}
		auto [firstSpreadIndex, lastSpreadIndex] = getSpreadIndices(parent);
		return getContextualTypeForElementExpression(t, elementIndex,
													 static_cast<int>(parent->elements().size()),
													 firstSpreadIndex,
													 lastSpreadIndex);
	}
	case Kind::ConditionalExpression:
		return getContextualTypeForConditionalOperand(node, contextFlags);
	case Kind::TemplateSpan:
		return getContextualTypeForSubstitutionExpression(parent->parent, node);
	case Kind::ParenthesizedExpression:
		return getContextualType(parent, contextFlags);
	case Kind::NonNullExpression:
		return getContextualType(parent, contextFlags);
	case Kind::SatisfiesExpression:
		return getTypeFromTypeNode(parent->type());
	case Kind::ExportAssignment:
		return tryGetTypeFromTypeNode(parent);
	case Kind::JsxExpression:
		return getContextualTypeForJsxExpression(parent, contextFlags);
	case Kind::JsxAttribute:
	case Kind::JsxSpreadAttribute:
		return getContextualTypeForJsxAttribute(parent, contextFlags);
	case Kind::JsxOpeningElement:
	case Kind::JsxSelfClosingElement:
		return getContextualJsxElementAttributesType(parent, contextFlags);
	case Kind::ImportAttribute:
		return getContextualImportAttributeType(parent);
	default:
		break;
	}
	return nullptr;
}

// In a variable, parameter or property declaration with a type annotation,
// the contextual type of an initializer expression is the type of the variable, parameter or property.
//
// Otherwise, in a parameter declaration of a contextually typed function expression,
// the contextual type of an initializer expression is the contextual type of the parameter.
//
// Otherwise, in a variable or parameter declaration with a binding pattern name,
// the contextual type of an initializer expression is the type implied by the binding pattern.
//
// Otherwise, in a binding pattern inside a variable or parameter declaration,
// the contextual type of an initializer expression is the type annotation of the containing declaration, if present.
// checker.go:29953
Type* Checker::getContextualTypeForInitializerExpression(
	Node* node, ContextFlags contextFlags) {
	Node* declaration = node->parent;
	Node* initializer = declaration->initializer();
	if (node == initializer) {
		Type* result =
			getContextualTypeForVariableLikeDeclaration(declaration, contextFlags);
		if (result != nullptr) {
			return result;
		}
		if ((contextFlags & ContextFlagsSkipBindingPatterns) == 0 &&
			isBindingPattern(declaration->name()) &&
			!declaration->name()->elements().empty()) {
			return getTypeFromBindingPattern(declaration->name(),
											 true /*includePatternInType*/,
											 false /*reportErrors*/);
		}
	}
	return nullptr;
}

// checker.go:29968
Type* Checker::getContextualTypeForVariableLikeDeclaration(
	Node* declaration, ContextFlags contextFlags) {
	Node* typeNode = declaration->type();
	if (typeNode != nullptr) {
		return getTypeFromTypeNode(typeNode);
	}
	switch (declaration->kind) {
	case Kind::Parameter:
		return getContextuallyTypedParameterType(declaration);
	case Kind::BindingElement:
		return getContextualTypeForBindingElement(declaration, contextFlags);
	case Kind::PropertyDeclaration:
		if (isStatic(declaration)) {
			return getContextualTypeForStaticPropertyDeclaration(declaration,
															   contextFlags);
		}
	default:
		break;
	}
	// By default, do nothing and return nil - only the above cases have context implied by a parent
	return nullptr;
}

// Return contextual type of parameter or undefined if no contextual type is available
// checker.go:29988
Type* Checker::getContextuallyTypedParameterType(Node* parameter) {
	Node* fn = parameter->parent;
	if (!isContextSensitiveFunctionOrObjectLiteralMethod(fn)) {
		return nullptr;
	}
	Node* iife = getImmediatelyInvokedFunctionExpression(fn);
	if (iife != nullptr) {
		std::vector<Node*> args = getEffectiveCallArguments(iife);
		int indexOfParameter = indexOfRange(fn->parameters(), parameter);
		if (hasDotDotDotToken(parameter)) {
			return getSpreadArgumentType(args, indexOfParameter,
										 static_cast<int>(args.size()), anyType,
										 nullptr /*context*/, CheckModeNormal);
		}
		SignatureLinks* links = signatureLinks.Get(iife);
		Signature* cached = links->resolvedSignature;
		links->resolvedSignature = anySignature;
		Type* t;
		if (indexOfParameter < static_cast<int>(args.size())) {
			t = getWidenedLiteralType(checkExpression(args[indexOfParameter]));
		} else if (parameter->initializer() != nullptr) {
			t = nullptr;
		} else {
			t = undefinedWideningType;
		}
		links->resolvedSignature = cached;
		return t;
	}
	Signature* contextualSignature = getContextualSignature(fn);
	if (contextualSignature != nullptr) {
		int index = indexOfRange(fn->parameters(), parameter) -
					(getThisParameter(fn) != nullptr ? 1 : 0);
		if (hasDotDotDotToken(parameter) &&
			lastOrNil(fn->parameters()) == parameter) {
			return getRestTypeAtPosition(contextualSignature, index, false);
		}
		return tryGetTypeAtPosition(contextualSignature, index);
	}
	return nullptr;
}

// checker.go:30024
bool Checker::isContextSensitiveFunctionOrObjectLiteralMethod(Node* fn) {
	return (isFunctionExpressionOrArrowFunction(fn) ||
			isObjectLiteralMethod(fn)) &&
		   isContextSensitiveFunctionLikeDeclaration(fn);
}

// checker.go:30028 — getSpreadArgumentType
Type* Checker::getSpreadArgumentType(const std::vector<Node*>& args, int index,
									 int argCount, Type* restType,
									 InferenceContext* context,
									 CheckMode checkMode) {
	bool inConstContext = isConstTypeVariable(restType, 0);
	if (argCount > 0 && index >= argCount - 1) {
		Node* arg = args[argCount - 1];
		if (isSpreadArgument(arg)) {
			// We are inferring from a spread expression in the last argument position, i.e. both the parameter
			// and the argument are ...x forms.
			Type* spreadType;
			if (isSyntheticExpression(arg)) {
				spreadType = static_cast<Type*>(arg->as<SyntheticExpression>()->Type);
			} else {
				spreadType = checkExpressionWithContextualType(
					arg->expression(), restType, context, checkMode);
			}
			if (isArrayLikeType(spreadType)) {
				return getMutableArrayOrTupleType(spreadType);
			}
			if (isSpreadElement(arg)) {
				arg = arg->expression();
			}
			return createArrayTypeEx(
				checkIteratedTypeOrElementType(IterationUseSpread, spreadType,
											   undefinedType, arg),
				inConstContext);
		}
	}
	std::vector<Type*> types;
	std::vector<TupleElementInfo> infos;
	for (int i = index; i < argCount; i++) {
		Node* arg = args[i];
		Type* t;
		TupleElementInfo info{};
		if (isSpreadArgument(arg)) {
			Type* spreadType;
			if (isSyntheticExpression(arg)) {
				spreadType = static_cast<Type*>(arg->as<SyntheticExpression>()->Type);
			} else {
				spreadType = checkExpression(arg->expression());
			}
			if (isArrayLikeType(spreadType)) {
				t = spreadType;
				info.flags = ElementFlagsVariadic;
			} else {
				if (isSpreadElement(arg)) {
					t = checkIteratedTypeOrElementType(IterationUseSpread,
													   spreadType, undefinedType,
													   arg->expression());
				} else {
					t = checkIteratedTypeOrElementType(IterationUseSpread,
													   spreadType, undefinedType,
													   arg);
				}
				info.flags = ElementFlagsRest;
			}
		} else {
			Type* contextualType;
			if (isTupleType(restType)) {
				contextualType = orElse(
					getContextualTypeForElementExpression(restType, i - index,
														  argCount - index, -1, -1),
					unknownType);
			} else {
				contextualType = getIndexedAccessTypeEx(
					restType, getNumberLiteralType(Number(i - index)),
					AccessFlagsContextual, nullptr, nullptr);
			}
			Type* argType = checkExpressionWithContextualType(
				arg, contextualType, context, checkMode);
			bool hasPrimitiveContextualType =
				inConstContext ||
				maybeTypeOfKind(contextualType,
								TypeFlagsPrimitive | TypeFlagsIndex |
									TypeFlagsTemplateLiteral |
									TypeFlagsStringMapping);
			if (hasPrimitiveContextualType) {
				t = getRegularTypeOfLiteralType(argType);
			} else {
				t = getWidenedLiteralType(argType);
			}
			info.flags = ElementFlagsRequired;
		}
		if (isSyntheticExpression(arg) &&
			arg->as<SyntheticExpression>()->TupleNameSource != nullptr) {
			info.labeledDeclaration =
				arg->as<SyntheticExpression>()->TupleNameSource;
		}
		types.push_back(t);
		infos.push_back(info);
	}
	return createTupleTypeEx(
		types, infos,
		inConstContext &&
			!someType(restType,
					  [this](Type* t) { return isMutableArrayLikeType(t); }));
}

// checker.go:30099 — getMutableArrayOrTupleType
Type* Checker::getMutableArrayOrTupleType(Type* t) {
	if (t->flags & TypeFlagsUnion) {
		return mapType(t, [this](Type* u) { return getMutableArrayOrTupleType(u); });
	}
	if ((t->flags & TypeFlagsAny) != 0 ||
		isMutableArrayOrTuple(getBaseConstraintOrType(t))) {
		return t;
	}
	if (isTupleType(t)) {
		return createTupleTypeEx(getElementTypes(t),
								 targetTupleType(t)->elementInfos,
								 false /*readonly*/);
	}
	return createTupleTypeEx({t}, {TupleElementInfo{ElementFlagsVariadic, nullptr}},
							 false);
}

// checker.go:30111 — getContextualTypeForBindingElement
Type* Checker::getContextualTypeForBindingElement(Node* declaration,
												  ContextFlags contextFlags) {
	Node* name = declaration->propertyNameOrName();
	if (isBindingPattern(name) || isComputedNonLiteralName(name)) {
		return nullptr;
	}
	Node* parent = declaration->parent->parent;
	Type* parentType =
		getContextualTypeForVariableLikeDeclaration(parent, contextFlags);
	if (parentType == nullptr) {
		if (!isBindingElement(parent) && parent->initializer() != nullptr) {
			parentType = checkDeclarationInitializer(
				parent,
				hasDotDotDotToken(declaration) ? CheckModeRestBindingElement
											 : CheckModeNormal,
				nullptr);
		}
	}
	if (parentType == nullptr) {
		return nullptr;
	}
	if (isArrayBindingPattern(parent->name())) {
		int index = indexOfRange(declaration->parent->elements(), declaration);
		if (index < 0) {
			return nullptr;
		}
		return getContextualTypeForElementExpression(parentType, index, -1, -1, -1);
	}
	Type* nameType = getLiteralTypeFromPropertyName(name);
	if (isTypeUsableAsPropertyName(nameType)) {
		return getTypeOfPropertyOfType(parentType,
									   getPropertyNameFromType(nameType));
	}
	return nullptr;
}

// checker.go:30140 — getContextualTypeForStaticPropertyDeclaration
Type* Checker::getContextualTypeForStaticPropertyDeclaration(
	Node* declaration, ContextFlags contextFlags) {
	if (isExpression(declaration->parent)) {
		if (Type* parentType =
				getContextualType(declaration->parent, contextFlags);
			parentType != nullptr) {
			return getTypeOfPropertyOfContextualType(
				parentType, getSymbolOfDeclaration(declaration)->name);
		}
	}
	return nullptr;
}

// checker.go:30149 — getContextualTypeForReturnExpression
Type* Checker::getContextualTypeForReturnExpression(Node* node,
													ContextFlags contextFlags) {
	Node* fn = getContainingFunction(node);
	if (fn != nullptr) {
		Type* contextualReturnType = getContextualReturnType(fn, contextFlags);
		if (contextualReturnType != nullptr) {
			FunctionFlags functionFlags = getFunctionFlags(fn);
			if (functionFlags & FunctionFlagsGenerator) {
				bool isAsyncGenerator =
					(functionFlags & FunctionFlagsAsync) != 0;
				if (contextualReturnType->flags & TypeFlagsUnion) {
					contextualReturnType = filterType(
						contextualReturnType,
						[this, isAsyncGenerator](Type* t) {
							return getIterationTypeOfGeneratorFunctionReturnType(
									   IterationTypeKind::Return, t,
									   isAsyncGenerator) != nullptr;
						});
				}
				Type* iterationReturnType =
					getIterationTypeOfGeneratorFunctionReturnType(
						IterationTypeKind::Return, contextualReturnType,
						(functionFlags & FunctionFlagsAsync) != 0);
				if (iterationReturnType == nullptr) {
					return nullptr;
				}
				contextualReturnType = iterationReturnType;
				// falls through to unwrap Promise for AsyncGenerators
			}
			if (functionFlags & FunctionFlagsAsync) {
				// Get the awaited type without the `Awaited<T>` alias
				Type* contextualAwaitedType = mapType(
					contextualReturnType,
					[this](Type* t) { return getAwaitedTypeNoAlias(t); });
				if (contextualAwaitedType == nullptr) {
					return nullptr;
				}
				return getUnionType({contextualAwaitedType,
									 createPromiseLikeType(contextualAwaitedType)});
			}
			// Regular function or Generator function
			return contextualReturnType;
		}
	}
	return nullptr;
}

// checker.go:30185 — getContextualIterationType
Type* Checker::getContextualIterationType(IterationTypeKind kind,
										  Node* functionDecl) {
	bool isAsync = (getFunctionFlags(functionDecl) & FunctionFlagsAsync) != 0;
	Type* contextualReturnType =
		getContextualReturnType(functionDecl, ContextFlagsNone);
	if (contextualReturnType != nullptr) {
		return getIterationTypeOfGeneratorFunctionReturnType(
			kind, contextualReturnType, isAsync);
	}
	return nullptr;
}

// checker.go:30194 — getContextualReturnType
Type* Checker::getContextualReturnType(Node* functionDecl,
									   ContextFlags contextFlags) {
	// If the containing function has a return type annotation, is a constructor, or is a get accessor whose
	// corresponding set accessor has a type annotation, return statements in the function are contextually typed
	Type* returnType = getReturnTypeFromAnnotation(functionDecl);
	if (returnType != nullptr) {
		return returnType;
	}
	// Otherwise, if the containing function is contextually typed by a function type with exactly one call signature
	// and that call signature is non-generic, return statements are contextually typed by the return type of the signature
	Signature* signature =
		getContextualSignatureForFunctionLikeDeclaration(functionDecl);
	if (signature != nullptr && !isResolvingReturnTypeOfSignature(signature)) {
		returnType = getReturnTypeOfSignature(signature);
		FunctionFlags functionFlags = getFunctionFlags(functionDecl);
		if (functionFlags & FunctionFlagsGenerator) {
			return filterType(returnType, [this, functionFlags](Type* t) {
				return (t->flags & (TypeFlagsAnyOrUnknown | TypeFlagsVoid |
									TypeFlagsInstantiableNonPrimitive)) != 0 ||
					   checkGeneratorInstantiationAssignabilityToReturnType(
						   t, functionFlags, nullptr /*errorNode*/);
			});
		}
		if (functionFlags & FunctionFlagsAsync) {
			return filterType(returnType, [this](Type* t) {
				return (t->flags & (TypeFlagsAnyOrUnknown | TypeFlagsVoid |
									TypeFlagsInstantiableNonPrimitive)) != 0 ||
					   getAwaitedTypeOfPromise(t) != nullptr;
			});
		}
		return returnType;
	}
	Node* iife = getImmediatelyInvokedFunctionExpression(functionDecl);
	if (iife != nullptr) {
		return getContextualType(iife, contextFlags);
	}
	return nullptr;
}

// checker.go:30226 — checkGeneratorInstantiationAssignabilityToReturnType
bool Checker::checkGeneratorInstantiationAssignabilityToReturnType(
	Type* returnType, FunctionFlags functionFlags, Node* errorNode) {
	// Naively, one could check that Generator<any, any, any> is assignable to the return type annotation.
	// However, that would not catch the error in the following case.
	//
	//    interface BadGenerator extends Iterable<number>, Iterator<string> { }
	//    function* g(): BadGenerator { } // Iterable and Iterator have different types!
	//
	Type* generatorYieldType = orElse(
		getIterationTypeOfGeneratorFunctionReturnType(
			IterationTypeKind::Yield, returnType,
			(functionFlags & FunctionFlagsAsync) != 0),
		anyType);
	Type* generatorReturnType = orElse(
		getIterationTypeOfGeneratorFunctionReturnType(
			IterationTypeKind::Return, returnType,
			(functionFlags & FunctionFlagsAsync) != 0),
		generatorYieldType);
	Type* generatorNextType = orElse(
		getIterationTypeOfGeneratorFunctionReturnType(
			IterationTypeKind::Next, returnType,
			(functionFlags & FunctionFlagsAsync) != 0),
		unknownType);
	Type* generatorInstantiation =
		createGeneratorType(generatorYieldType, generatorReturnType,
							generatorNextType,
							(functionFlags & FunctionFlagsAsync) != 0);
	return checkTypeAssignableTo(generatorInstantiation, returnType, errorNode,
								 nullptr);
}

// checker.go:30241 — getContextualSignatureForFunctionLikeDeclaration
Signature* Checker::getContextualSignatureForFunctionLikeDeclaration(Node* node) {
	// Only function expressions, arrow functions, and object literal methods are contextually typed.
	if (isFunctionExpressionOrArrowFunction(node) ||
		isObjectLiteralMethod(node)) {
		return getContextualSignature(node);
	}
	return nullptr;
}

// checker.go:30249 — getContextualTypeForYieldOperand
Type* Checker::getContextualTypeForYieldOperand(Node* node,
												ContextFlags contextFlags) {
	Node* fn = getContainingFunction(node);
	if (fn != nullptr) {
		FunctionFlags functionFlags = getFunctionFlags(fn);
		Type* contextualReturnType = getContextualReturnType(fn, contextFlags);
		if (contextualReturnType != nullptr) {
			bool isAsyncGenerator = (functionFlags & FunctionFlagsAsync) != 0;
			bool isYieldStar =
				node->as<YieldExpression>()->AsteriskToken != nullptr;
			if (!isYieldStar &&
				(contextualReturnType->flags & TypeFlagsUnion) != 0) {
				contextualReturnType = filterType(
					contextualReturnType,
					[this, isAsyncGenerator](Type* t) {
						return getIterationTypeOfGeneratorFunctionReturnType(
								   IterationTypeKind::Return, t,
								   isAsyncGenerator) != nullptr;
					});
			}
			if (isYieldStar) {
				IterationTypes iterationTypes =
					getIterationTypesOfGeneratorFunctionReturnType(
						contextualReturnType, isAsyncGenerator);
				Type* yieldType = orElse(iterationTypes.yieldType, silentNeverType);
				Type* returnType =
					orElse(getContextualType(node, contextFlags), silentNeverType);
				Type* nextType = orElse(iterationTypes.nextType, unknownType);
				Type* generatorType =
					createGeneratorType(yieldType, returnType, nextType,
										false /*isAsyncGenerator*/);
				if (isAsyncGenerator) {
					Type* asyncGeneratorType =
						createGeneratorType(yieldType, returnType, nextType,
											true /*isAsyncGenerator*/);
					return getUnionType({generatorType, asyncGeneratorType});
				}
				return generatorType;
			}
			return getIterationTypeOfGeneratorFunctionReturnType(
				IterationTypeKind::Yield, contextualReturnType, isAsyncGenerator);
		}
	}
	return nullptr;
}

// checker.go:30280 — getContextualTypeForAwaitOperand
Type* Checker::getContextualTypeForAwaitOperand(Node* node,
												ContextFlags contextFlags) {
	Type* contextualType = getContextualType(node, contextFlags);
	if (contextualType != nullptr) {
		Type* contextualAwaitedType = getAwaitedTypeNoAlias(contextualType);
		if (contextualAwaitedType != nullptr) {
			return getUnionType({contextualAwaitedType,
								 createPromiseLikeType(contextualAwaitedType)});
		}
	}
	return nullptr;
}

// In a typed function call, an argument or substitution expression is contextually typed by the type of the corresponding parameter.
// checker.go:30297
Type* Checker::getContextualTypeForArgument(Node* callTarget, Node* arg) {
	std::vector<Node*> args = getEffectiveCallArguments(callTarget);
	int argIndex = indexOfRange(args, arg);
	// -1 for e.g. the expression of a CallExpression, or the tag of a TaggedTemplateExpression
	if (argIndex == -1) {
		return nullptr;
	}
	return getContextualTypeForArgumentAtIndex(callTarget, argIndex);
}

// checker.go:30305
Type* Checker::getContextualTypeForArgumentAtIndex(Node* callTarget,
												 int argIndex) {
	if (isImportCall(callTarget)) {
		if (argIndex == 0) {
			return stringType;
		}
		if (argIndex == 1) {
			return getGlobalImportCallOptionsType();
		}
		return anyType;
	}
	// If we're already in the process of resolving the given signature, don't resolve again as
	// that could cause infinite recursion. Instead, return anySignature.
	Signature* signature;
	if (signatureLinks.Get(callTarget)->resolvedSignature ==
		resolvingSignature) {
		signature = resolvingSignature;
	} else {
		signature = getResolvedSignature(callTarget, nullptr, CheckModeNormal);
	}
	if (isJsxOpeningLikeElement(callTarget) && argIndex == 0) {
		return getEffectiveFirstArgumentForJsxSignature(signature, callTarget);
	}
	int restIndex = static_cast<int>(signature->parameters.size()) - 1;
	if (signatureHasRestParameter(signature) && argIndex >= restIndex) {
		return getIndexedAccessTypeEx(
			getTypeOfSymbol(signature->parameters[restIndex]),
			getNumberLiteralType(Number(argIndex - restIndex)),
			AccessFlagsContextual, nullptr, nullptr);
	}
	return getTypeAtPosition(signature, argIndex);
}

// checker.go:30331
Type* Checker::getContextualTypeForDecorator(Node* decorator) {
	Signature* signature = getDecoratorCallSignature(decorator);
	if (signature != nullptr) {
		return getOrCreateTypeFromSignature(signature);
	}
	return nullptr;
}

// checker.go:30338
Type* Checker::getContextualTypeForBinaryOperand(Node* node,
												 ContextFlags contextFlags) {
	BinaryExpression* binary = node->parent->as<BinaryExpression>();
	if (Node* t = binary->Type; t != nullptr) {
		return getTypeFromTypeNode(t);
	}
	switch (binary->OperatorToken->kind) {
	case Kind::EqualsToken:
	case Kind::AmpersandAmpersandEqualsToken:
	case Kind::BarBarEqualsToken:
	case Kind::QuestionQuestionEqualsToken:
		// In an assignment expression, the right operand is contextually typed by the type of the left operand
		// unless it's an assignment declaration.
		if (node == binary->Right) {
			Node* target = getLeftmostExpression(binary->Left, false);
			if (!(isIdentifier(target) &&
				  (getResolvedSymbol(target)->flags &
				   SymbolFlagsModuleExports) != 0)) {
				return getContextualTypeForAssignmentExpression(binary);
			}
		}
		break;
	case Kind::BarBarToken:
	case Kind::QuestionQuestionToken: {
		// When an || expression has a contextual type, the operands are contextually typed by that type, except
		// when that type originates in a binding pattern, the right operand is contextually typed by the type of
		// the left operand. When an || expression has no contextual type, the right operand is contextually typed
		// by the type of the left operand, except for the special case of Javascript declarations of the form
		// `namespace.prop = namespace.prop || {}`.
		Type* t = getContextualType(binary->asNode(), contextFlags);
		if (node == binary->Right &&
			(t == nullptr || patternForType[t] != nullptr)) {
			return getTypeOfExpression(binary->Left);
		}
		return t;
	}
	case Kind::AmpersandAmpersandToken:
	case Kind::CommaToken:
		if (node == binary->Right) {
			return getContextualType(binary->asNode(), contextFlags);
		}
		break;
	default:
		break;
	}
	return nullptr;
}

// checker.go:30374
Type* Checker::getContextualTypeForAssignmentExpression(BinaryExpression* binary) {
	Node* left = binary->Left;
	if (isAccessExpression(left)) {
		Node* expr = left->expression();
		switch (expr->kind) {
		case Kind::Identifier: {
			Symbol* symbol =
				getExportSymbolOfValueSymbolIfExported(getResolvedSymbol(expr));
			if ((symbol->flags & SymbolFlagsModuleExports) != 0) {
				// No contextual type for an expression of the form 'module.exports = expr'.
				return nullptr;
			}
			if (binary->Symbol != nullptr) {
				// We have an assignment declaration (a binary expression with a symbol assigned by the binder) of the form
				// 'F.id = expr' or 'F[xxx] = expr'. If 'F' is declared as a variable with a type annotation, we can obtain a
				// contextual type from the annotated type without triggering a circularity. Otherwise, the assignment
				// declaration has no contextual type.
				if (symbol->valueDeclaration != nullptr &&
					isVariableDeclaration(symbol->valueDeclaration)) {
					if (Node* typeNode =
							symbol->valueDeclaration->type();
						typeNode != nullptr) {
						if (isPropertyAccessExpression(left)) {
							return getTypeOfPropertyOfContextualType(
								getTypeFromTypeNode(typeNode),
								left->name()->text());
						}
						Type* nameType = checkExpressionCached(
							left->as<ElementAccessExpression>()
								->ArgumentExpression);
						if (isTypeUsableAsPropertyName(nameType)) {
							return getTypeOfPropertyOfContextualTypeEx(
								getTypeFromTypeNode(typeNode),
								getPropertyNameFromType(nameType), nameType);
						}
						return getTypeOfExpression(left);
					}
				}
				return nullptr;
			}
			break;
		}
		case Kind::PropertyAccessExpression:
		case Kind::ElementAccessExpression:
			if (binary->Symbol != nullptr) {
				return nullptr;
			}
			break;
		case Kind::ThisKeyword: {
			Symbol* symbol = nullptr;
			Type* thisType = getTypeOfExpression(expr);
			if (isPropertyAccessExpression(left)) {
				Node* name = left->name();
				if (isPrivateIdentifier(name)) {
					if (thisType->symbol != nullptr) {
						symbol = getPropertyOfType(
							thisType,
							getSymbolNameForPrivateIdentifier(thisType->symbol,
															  name->text()));
					}
				} else {
					symbol = getPropertyOfType(thisType, name->text());
				}
			} else {
				Type* propType = checkExpressionCached(
					left->as<ElementAccessExpression>()->ArgumentExpression);
				if (isTypeUsableAsPropertyName(propType)) {
					symbol = getPropertyOfType(
						thisType, getPropertyNameFromType(propType));
				}
			}
			if (symbol != nullptr) {
				if (Node* d = symbol->valueDeclaration;
					d != nullptr &&
					(isPropertyDeclaration(d) ||
					 isPropertySignatureDeclaration(d)) &&
					d->type() == nullptr && d->initializer() == nullptr) {
					// No contextual type for 'this.xxx = expr', where xxx is declared as a property with no type annotation or initializer.
					return nullptr;
				}
			}
			if (binary->Symbol != nullptr &&
				binary->Symbol->valueDeclaration != nullptr &&
				binary->Symbol->valueDeclaration->type() == nullptr) {
				// We have an assignment declaration 'this.xxx = expr' with no (synthetic) type annotation
				if (!isObjectLiteralMethod(getThisContainer(expr, false, false))) {
					return nullptr;
				}
				// and now for one single case of object literal methods
				Node* name = getElementOrPropertyAccessName(left);
				if (name == nullptr) {
					return nullptr;
				} else {
					// !!! contextual typing for `this` in object literals
					return nullptr;
				}
			}
			break;
		}
		default:
			break;
		}
	}
	return getTypeOfExpression(left);
}

// checker.go:30439
Type* Checker::getContextualTypeForObjectLiteralElement(
	Node* element, ContextFlags contextFlags) {
	if (Node* t = element->type();
		t != nullptr && !isObjectLiteralMethod(element)) {
		return getTypeFromTypeNode(t);
	}
	Node* objectLiteral = element->parent;
	Type* t = getApparentTypeOfContextualType(objectLiteral, contextFlags);
	if (t != nullptr) {
		if (hasBindableName(element)) {
			// For a (non-symbol) computed property, there is no reason to look up the name
			// in the type. It will just be "__computed", which does not appear in any
			// SymbolTable.
			Symbol* symbol = getSymbolOfDeclaration(element);
			return getTypeOfPropertyOfContextualTypeEx(
				t, symbol->name, valueSymbolLinks.Get(symbol)->nameType);
		}
		if (hasDynamicName(element)) {
			Node* name = getNameOfDeclaration(element);
			if (name != nullptr && isComputedPropertyName(name)) {
				Type* exprType = checkExpression(name->expression());
				if (isTypeUsableAsPropertyName(exprType)) {
					Type* propType = getTypeOfPropertyOfContextualType(
						t, getPropertyNameFromType(exprType));
					if (propType != nullptr) {
						return propType;
					}
				}
			}
		}
		if (element->name() != nullptr) {
			Type* nameType = getLiteralTypeFromPropertyName(element->name());
			// We avoid calling getApplicableIndexInfo here because it performs potentially expensive intersection reduction.
			return mapTypeEx(
				t,
				[this, nameType](Type* t) -> Type* {
					IndexInfo* indexInfo = findApplicableIndexInfo(
						getIndexInfosOfStructuredType(t), nameType);
					if (indexInfo == nullptr) {
						return nullptr;
					}
					return indexInfo->valueType;
				},
				true /*noReductions*/);
		}
	}
	return nullptr;
}

// In an object literal contextually typed by a type T, the contextual type of a property assignment is the type of
// the matching property in T, if one exists. Otherwise, it is the type of the numeric index signature in T, if one
// exists. Otherwise, it is the type of the string index signature in T, if one exists.
// checker.go:30483
Type* Checker::getContextualTypeForObjectLiteralMethod(
	Node* node, ContextFlags contextFlags) {
	if (node->flags & NodeFlagsInWithStatement) {
		// We cannot answer semantic questions within a with block, do not proceed any further
		return nullptr;
	}
	return getContextualTypeForObjectLiteralElement(node, contextFlags);
}

// checker.go:30491
Type* Checker::getContextualTypeForElementExpression(Type* t, int index,
													 int length,
													 int firstSpreadIndex,
													 int lastSpreadIndex) {
	if (t == nullptr) {
		return nullptr;
	}
	return mapTypeEx(
		t,
		[this, index, length, firstSpreadIndex, lastSpreadIndex](
			Type* t) -> Type* {
			if (isTupleType(t)) {
				// If index is before any spread element and within the fixed part of the contextual tuple type, return
				// the type of the contextual tuple element.
				if ((firstSpreadIndex < 0 || index < firstSpreadIndex) &&
					index < targetTupleType(t)->fixedLength) {
					return removeMissingType(
						getTypeArguments(t)[index],
						(targetTupleType(t)->elementInfos[index].flags &
						 ElementFlagsOptional) != 0);
				}
				// When the length is known and the index is after all spread elements we compute the offset from the element
				// to the end and the number of ending fixed elements in the contextual tuple type.
				int offset = 0;
				if (length >= 0 &&
					(lastSpreadIndex < 0 || index > lastSpreadIndex)) {
					offset = length - index;
				}
				int fixedEndLength = 0;
				if (offset > 0 &&
					(targetTupleType(t)->combinedFlags & ElementFlagsVariable) != 0) {
					fixedEndLength = getEndElementCount(targetTupleType(t),
														ElementFlagsFixed);
				}
				// If the offset is within the ending fixed part of the contextual tuple type, return the type of the contextual
				// tuple element.
				if (offset > 0 && offset <= fixedEndLength) {
					return getTypeArguments(
						t)[getTypeReferenceArity(t) - offset];
				}
				// Return a union of the possible contextual element types with no subtype reduction.
				int tupleIndex = targetTupleType(t)->fixedLength;
				if (firstSpreadIndex >= 0) {
					tupleIndex = std::min(tupleIndex, firstSpreadIndex);
				}
				int endSkipCount = fixedEndLength;
				if (length >= 0 && lastSpreadIndex >= 0) {
					endSkipCount = std::min(fixedEndLength,
											length - lastSpreadIndex);
				}
				return getElementTypeOfSliceOfTupleType(
					t, tupleIndex, endSkipCount, false /*writing*/,
					true /*noReductions*/);
			}
			// If element index is known and a contextual property with that name exists, return it. Otherwise return the
			// iterated or element type of the contextual type.
			if (firstSpreadIndex < 0 || index < firstSpreadIndex) {
				Type* propType = getTypeOfPropertyOfContextualType(
					t, std::to_string(index));
				if (propType != nullptr) {
					return propType;
				}
			}
			return getIteratedTypeOrElementType(IterationUseElement, t,
												undefinedType,
												nullptr /*errorNode*/,
												false /*checkAssignability*/);
		},
		true /*noReductions*/);
}

// In a contextually typed conditional expression, the true/false expressions are contextually typed by the same type.
// checker.go:30541
Type* Checker::getContextualTypeForConditionalOperand(
	Node* node, ContextFlags contextFlags) {
	ConditionalExpression* conditional = node->parent->as<ConditionalExpression>();
	if (node == conditional->WhenTrue || node == conditional->WhenFalse) {
		return getContextualType(node->parent, contextFlags);
	}
	return nullptr;
}

// checker.go:30549
Type* Checker::getContextualTypeForSubstitutionExpression(
	Node* templateNode, Node* substitutionExpression) {
	if (isTaggedTemplateExpression(templateNode->parent)) {
		return getContextualTypeForArgument(templateNode->parent,
										  substitutionExpression);
	}
	return nullptr;
}

// checker.go:30556
Type* Checker::getContextualImportAttributeType(Node* node) {
	return getTypeOfPropertyOfContextualType(getGlobalImportAttributesType(),
										   node->name()->text());
}

// Returns the effective arguments for an expression that works like a function invocation.
// checker.go:30560
std::vector<Node*> Checker::getEffectiveCallArguments(Node* node) {
	if (isJsxOpeningFragment(node)) {
		// This attributes Type does not include a children property yet, the same way a fragment created with <React.Fragment> does not at this stage
		return {createSyntheticExpression(node, emptyFreshJsxObjectType, false,
										  nullptr)};
	}
	if (isTaggedTemplateExpression(node)) {
		Node* template_ = node->as<TaggedTemplateExpression>()->Template;
		Node* firstArg = createSyntheticExpression(
			template_, getGlobalTemplateStringsArrayType(), false, nullptr);
		if (!isTemplateExpression(template_)) {
			return {firstArg};
		}
		const std::vector<Node*>& spans =
			template_->as<TemplateExpression>()->TemplateSpans->nodes;
		std::vector<Node*> args(spans.size() + 1);
		args[0] = firstArg;
		for (size_t i = 0; i < spans.size(); i++) {
			args[i + 1] = spans[i]->expression();
		}
		return args;
	}
	if (isDecorator(node)) {
		return getEffectiveDecoratorArguments(node);
	}
	if (isBinaryExpression(node)) {
		// Handles instanceof operator
		return {node->as<BinaryExpression>()->Left};
	}
	if (isJsxOpeningLikeElement(node)) {
		if (!node->attributes()->properties().empty() ||
			(isJsxOpeningElement(node) &&
			 !node->parent->children()->nodes.empty())) {
			return {node->attributes()};
		}
		return {};
	}
	std::vector<Node*> args = node->arguments();
	int spreadIndex = getSpreadArgumentIndex(args);
	if (spreadIndex >= 0) {
		// Create synthetic arguments from spreads of tuple types.
		std::vector<Node*> effectiveArgs(args.begin(), args.begin() + spreadIndex);
		for (size_t i = spreadIndex; i < args.size(); i++) {
			Node* arg = args[i];
			Type* spreadType = nullptr;
			// We can call checkExpressionCached because spread expressions never have a contextual type.
			if (isSpreadElement(arg)) {
				if (!flowLoopStack.empty()) {
					spreadType = checkExpression(arg->expression());
				} else {
					spreadType = checkExpressionCached(arg->expression());
				}
			}
			if (spreadType != nullptr && isTupleType(spreadType)) {
				const std::vector<Type*>& elementTypes =
					getElementTypes(spreadType);
				for (size_t j = 0; j < elementTypes.size(); j++) {
					Type* t = elementTypes[j];
					const std::vector<TupleElementInfo>& elementInfos =
						targetTupleType(spreadType)->elementInfos;
					ElementFlags flags = elementInfos[j].flags;
					Type* syntheticType = t;
					if (flags & ElementFlagsRest) {
						syntheticType = createArrayType(t);
					}
					Node* syntheticArg = createSyntheticExpression(
						arg, syntheticType,
						(flags & ElementFlagsVariable) != 0,
						elementInfos[j].labeledDeclaration);
					effectiveArgs.push_back(syntheticArg);
				}
			} else {
				effectiveArgs.push_back(arg);
			}
		}
		return effectiveArgs;
	}
	return args;
}

// checker.go:30628
int Checker::getSpreadArgumentIndex(const std::vector<Node*>& args) {
	return findIndexRange(args, isSpreadArgument);
}

// checker.go:30635 — createSyntheticExpression
Node* Checker::createSyntheticExpression(Node* parent, Type* t, bool isSpread,
										 Node* tupleNameSource) {
	Node* result =
		factory.newSyntheticExpression(t, isSpread, tupleNameSource);
	result->loc = parent->loc;
	result->parent = parent;
	return result;
}

// checker.go:30642 — getSpreadIndices
std::pair<int, int> Checker::getSpreadIndices(Node* node) {
	ArrayLiteralLinks* links = arrayLiteralLinks.Get(node);
	if (!links->indicesComputed) {
		int first = -1, last = -1;
		const std::vector<Node*>& elements = node->elements();
		for (size_t i = 0; i < elements.size(); i++) {
			if (isSpreadElement(elements[i])) {
				if (first < 0) {
					first = static_cast<int>(i);
				}
				last = static_cast<int>(i);
			}
		}
		links->firstSpreadIndex = first;
		links->lastSpreadIndex = last;
		links->indicesComputed = true;
	}
	return {links->firstSpreadIndex, links->lastSpreadIndex};
}

// Returns the synthetic argument list for a decorator invocation.
// checker.go:30660
std::vector<Node*> Checker::getEffectiveDecoratorArguments(Node* node) {
	Node* expr = node->expression();
	Signature* signature = getDecoratorCallSignature(node);
	if (signature != nullptr) {
		std::vector<Node*> args(signature->parameters.size());
		for (size_t i = 0; i < signature->parameters.size(); i++) {
			args[i] = createSyntheticExpression(
				expr, getTypeOfSymbol(signature->parameters[i]), false, nullptr);
		}
		return args;
	}
	TSC_UNREACHABLE("Decorator signature not found");
}

// checker.go:30675
Signature* Checker::getDecoratorCallSignature(Node* decorator) {
	if (legacyDecorators) {
		return getLegacyDecoratorCallSignature(decorator);
	}
	return getESDecoratorCallSignature(decorator);
}

// checker.go:30682 — getLegacyDecoratorCallSignature
Signature* Checker::getLegacyDecoratorCallSignature(Node* decorator) {
	Node* node = decorator->parent;
	SignatureLinks* links = signatureLinks.Get(node);
	if (links->decoratorSignature == nullptr) {
		links->decoratorSignature = anySignature;
		switch (node->kind) {
		case Kind::ClassDeclaration:
		case Kind::ClassExpression: {
			// For a class decorator, the `target` is the type of the class (e.g. the
			// "static" or "constructor" side of the class).
			Type* targetType = getTypeOfSymbol(getSymbolOfDeclaration(node));
			Symbol* targetParam = newParameter("target", targetType);
			links->decoratorSignature = newCallSignature(
				{}, nullptr, {targetParam},
				getUnionType({targetType, voidType}));
			break;
		}
		case Kind::Parameter: {
			if (!isConstructorDeclaration(node->parent) &&
				!((isMethodDeclaration(node->parent) ||
				   isSetAccessorDeclaration(node->parent)) &&
				  isClassLike(node->parent->parent))) {
				break;
			}
			if (getThisParameter(node->parent) == node) {
				break;
			}
			int index =
				indexOfRange(node->parent->parameters(), node) -
				(getThisParameter(node->parent) != nullptr ? 1 : 0);
			TSC_ASSERT(index >= 0, "index >= 0");
			// A parameter declaration decorator will have three arguments (see `ParameterDecorator` in
			// core.d.ts).
			Type* targetType;
			Type* keyType;
			if (isConstructorDeclaration(node->parent)) {
				targetType =
					getTypeOfSymbol(getSymbolOfDeclaration(node->parent->parent));
				keyType = undefinedType;
			} else {
				targetType = getParentTypeOfClassElement(node->parent);
				keyType = getClassElementPropertyKeyType(node->parent);
			}
			Type* indexType = getNumberLiteralType(Number(index));
			Symbol* targetParam = newParameter("target", targetType);
			Symbol* keyParam = newParameter("propertyKey", keyType);
			Symbol* indexParam = newParameter("parameterIndex", indexType);
			links->decoratorSignature =
				newCallSignature({}, nullptr,
								 {targetParam, keyParam, indexParam}, voidType);
			break;
		}
		case Kind::MethodDeclaration:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
		case Kind::PropertyDeclaration: {
			if (!isClassLike(node->parent)) {
				break;
			}
			// A method or accessor declaration decorator will have either two or three arguments (see
			// `PropertyDecorator` and `MethodDecorator` in core.d.ts).
			Type* targetType = getParentTypeOfClassElement(node);
			Symbol* targetParam = newParameter("target", targetType);
			Type* keyType = getClassElementPropertyKeyType(node);
			Symbol* keyParam = newParameter("propertyKey", keyType);
			Type* returnType = voidType;
			if (!isPropertyDeclaration(node)) {
				returnType =
					newTypedPropertyDescriptorType(getTypeOfNode(node));
			}
			bool hasPropDesc = !isPropertyDeclaration(node) ||
							   hasAccessorModifier(node);
			if (hasPropDesc) {
				Type* descriptorType =
					newTypedPropertyDescriptorType(getTypeOfNode(node));
				Symbol* descriptorParam =
					newParameter("descriptor", descriptorType);
				links->decoratorSignature = newCallSignature(
					{}, nullptr, {targetParam, keyParam, descriptorParam},
					getUnionType({returnType, voidType}));
			} else {
				links->decoratorSignature = newCallSignature(
					{}, nullptr, {targetParam, keyParam},
					getUnionType({returnType, voidType}));
			}
			break;
		}
		default:
			break;
		}
	}
	if (links->decoratorSignature == anySignature) {
		return nullptr;
	}
	return links->decoratorSignature;
}

// checker.go:30733 — getESDecoratorCallSignature
Signature* Checker::getESDecoratorCallSignature(Node* decorator) {
	// We are considering a future change that would allow the type of a decorator to affect the type of the
	// class and its members, such as a `@Stringify` decorator changing the type of a `number` field to `string`, or
	// a `@Callable` decorator adding a call signature to a `class`. The type arguments for the various context
	// types may eventually change to reflect such mutations.
	//
	// In some cases we describe such potential mutations as coming from a "prior decorator application". It is
	// important to note that, while decorators are *evaluated* left to right, they are *applied* right to left
	// to preserve f ∘ g -> f(g(x)) application order. In these cases, a "prior" decorator usually means the
	// next decorator following this one in document order.
	//
	// The "original type" of a class or member is the type it was declared as, or the type we infer from
	// initializers, before _any_ decorators are applied.
	//
	// The type of a class or member that is a result of a prior decorator application represents the
	// "current type", i.e., the type for the declaration at the time the decorator is _applied_.
	//
	// The type of a class or member that is the result of the application of *all* relevant decorators is the
	// "final type".
	//
	// Any decorator that allows mutation or replacement will also refer to an "input type" and an
	// "output type". The "input type" corresponds to the "current type" of the declaration, while the
	// "output type" will become either the "input type/current type" for a subsequent decorator application,
	// or the "final type" for the decorated declaration.
	//
	// It is important to understand decorator application order as it relates to how the "current", "input",
	// "output", and "final" types will be determined:
	//
	//  @E2 @E1 class SomeClass {
	//      @A2 @A1 static f() {}
	//      @B2 @B1 g() {}
	//      @C2 @C1 static x;
	//      @D2 @D1 y;
	//  }
	//
	// Per [the specification][1], decorators are applied in the following order:
	//
	// 1. For each static method (incl. get/set methods and `accessor` fields), in document order:
	//    a. Apply each decorator for that method, in reverse order (`A1`, `A2`).
	// 2. For each instance method (incl. get/set methods and `accessor` fields), in document order:
	//    a. Apply each decorator for that method, in reverse order (`B1`, `B2`).
	// 3. For each static field (excl. auto-accessors), in document order:
	//    a. Apply each decorator for that field, in reverse order (`C1`, `C2`).
	// 4. For each instance field (excl. auto-accessors), in document order:
	//    a. Apply each decorator for that field, in reverse order (`D1`, `D2`).
	// 5. Apply each decorator for the class, in reverse order (`E1`, `E2`).
	//
	// As a result, "current" types at each decorator application are as follows:
	// - For `A1`, the "current" types of the class and method are their "original" types.
	// - For `A2`, the "current type" of the method is the "output type" of `A1`, and the "current type" of the
	//   class is the type of `SomeClass` where `f` is the "output type" of `A1`. This becomes the "final type"
	//   of `f`.
	// - For `B1`, the "current type" of the method is its "original type", and the "current type" of the class
	//   is the type of `SomeClass` where `f` now has its "final type".
	// - etc.
	//
	// [1]: https://arai-a.github.io/ecma262-compare/?pr=2417&id=sec-runtime-semantics-classdefinitionevaluation
	//
	// This seems complicated at first glance, but is not unlike our existing inference for functions:
	//
	//  declare function pipe<Original, A1, A2, B1, B2, C1, C2, D1, D2, E1, E2>(
	//      original: Original,
	//      a1: (input: Original, context: Context<E2>) => A1,
	//      a2: (input: A1, context: Context<E2>) => A2,
	//      b1: (input: A2, context: Context<E2>) => B1,
	//      b2: (input: B1, context: Context<E2>) => B2,
	//      c1: (input: B2, context: Context<E2>) => C1,
	//      c2: (input: C1, context: Context<E2>) => C2,
	//      d1: (input: B2, context: Context<E2>) => D1,
	//      d2: (input: D1, context: Context<E2>) => D2,
	//      e1: (input: D1, context: Context<E2>) => E1,
	//      e2: (input: E1, context: Context<E2>) => E2,
	//  ): E2;
	//
	// When a decorator is applied, it is passed two arguments: "target", which is a value representing the
	// thing being decorated (constructors for classes, functions for methods/accessors, `undefined` for fields,
	// and a `{ get, set }` object for auto-accessors), and "context", which is an object that provides
	// reflection information about the decorated element, as well as the ability to add additional "extra"
	// initializers. In most cases, the "target" argument corresponds to the "input type" in some way, and the
	// return value similarly corresponds to the "output type" (though if the "output type" is `void` or
	// `undefined` then the "output type" is the "input type").
	Node* node = decorator->parent;
	SignatureLinks* links = signatureLinks.Get(node);
	if (links->decoratorSignature == nullptr) {
		links->decoratorSignature = anySignature;
		switch (node->kind) {
		case Kind::ClassDeclaration:
		case Kind::ClassExpression: {
			// Class decorators have a `context` of `ClassDecoratorContext<Class>`, where the `Class` type
			// argument will be the "final type" of the class after all decorators are applied.
			Type* targetType = getTypeOfSymbol(getSymbolOfDeclaration(node));
			Type* contextType = newClassDecoratorContextType(targetType);
			links->decoratorSignature = newESDecoratorCallSignature(
				targetType, contextType, targetType);
			break;
		}
		case Kind::MethodDeclaration:
		case Kind::GetAccessor:
		case Kind::SetAccessor: {
			if (!isClassLike(node->parent)) {
				break;
			}
			// Method decorators have a `context` of `ClassMethodDecoratorContext<This, Value>`, where the
			// `Value` type argument corresponds to the "final type" of the method.
			//
			// Getter decorators have a `context` of `ClassGetterDecoratorContext<This, Value>`, where the
			// `Value` type argument corresponds to the "final type" of the value returned by the getter.
			//
			// Setter decorators have a `context` of `ClassSetterDecoratorContext<This, Value>`, where the
			// `Value` type argument corresponds to the "final type" of the parameter of the setter.
			//
			// In all three cases, the `This` type argument is the "final type" of either the class or
			// instance, depending on whether the member was `static`.
			Type* valueType;
			if (isMethodDeclaration(node)) {
				valueType = getOrCreateTypeFromSignature(
					getSignatureFromDeclaration(node));
			} else {
				valueType = getTypeOfNode(node);
			}
			Type* thisType;
			if (hasStaticModifier(node)) {
				thisType =
					getTypeOfSymbol(getSymbolOfDeclaration(node->parent));
			} else {
				thisType = getDeclaredTypeOfClassOrInterface(
					getSymbolOfDeclaration(node->parent));
			}
			// We wrap the "input type", if necessary, to match the decoration target. For getters this is
			// something like `() => inputType`, for setters it's `(value: inputType) => void` and for
			// methods it is just the input type.
			Type* targetType;
			if (isGetAccessorDeclaration(node)) {
				targetType = newGetterFunctionType(valueType);
			} else if (isSetAccessorDeclaration(node)) {
				targetType = newSetterFunctionType(valueType);
			} else {
				targetType = valueType;
			}
			Type* contextType = newClassMemberDecoratorContextTypeForNode(
				node, thisType, valueType);
			links->decoratorSignature = newESDecoratorCallSignature(
				targetType, contextType, targetType);
			break;
		}
		case Kind::PropertyDeclaration: {
			if (!isClassLike(node->parent)) {
				break;
			}
			// Field decorators have a `context` of `ClassFieldDecoratorContext<This, Value>` and
			// auto-accessor decorators have a `context` of `ClassAccessorDecoratorContext<This, Value>. In
			// both cases, the `This` type argument is the "final type" of either the class or instance,
			// depending on whether the member was `static`, and the `Value` type argument corresponds to
			// the "final type" of the value stored in the field.
			Type* valueType = getTypeOfNode(node);
			Type* thisType;
			if (hasStaticModifier(node)) {
				thisType =
					getTypeOfSymbol(getSymbolOfDeclaration(node->parent));
			} else {
				thisType = getDeclaredTypeOfClassOrInterface(
					getSymbolOfDeclaration(node->parent));
			}
			// The `target` of an auto-accessor decorator is a `{ get, set }` object, representing the
			// runtime-generated getter and setter that are added to the class/prototype. The `target` of a
			// regular field decorator is always `undefined` as it isn't installed until it is initialized.
			Type* targetType;
			if (hasAccessorModifier(node)) {
				targetType =
					newClassAccessorDecoratorTargetType(thisType, valueType);
			} else {
				targetType = undefinedType;
			}
			// We wrap the "output type" depending on the declaration. For auto-accessors, we wrap the
			// "output type" in a `ClassAccessorDecoratorResult<This, In, Out>` type, which allows for
			// mutation of the runtime-generated getter and setter, as well as the injection of an
			// initializer mutator. For regular fields, we wrap the "output type" in an initializer mutator.
			Type* returnType;
			if (hasAccessorModifier(node)) {
				returnType =
					newClassAccessorDecoratorResultType(thisType, valueType);
			} else {
				returnType = newClassFieldDecoratorInitializerMutatorType(
					thisType, valueType);
			}
			Type* contextType = newClassMemberDecoratorContextTypeForNode(
				node, thisType, valueType);
			links->decoratorSignature = newESDecoratorCallSignature(
				targetType, contextType, returnType);
			break;
		}
		default:
			break;
		}
	}
	if (links->decoratorSignature == anySignature) {
		return nullptr;
	}
	return links->decoratorSignature;
}

// checker.go:30927 — newClassDecoratorContextType
Type* Checker::newClassDecoratorContextType(Type* classType) {
	return tryCreateTypeReference(getGlobalClassDecoratorContextType(),
								  {classType});
}

// checker.go:30931 — newClassMethodDecoratorContextType
Type* Checker::newClassMethodDecoratorContextType(Type* classType,
												  Type* valueType) {
	return tryCreateTypeReference(getGlobalClassMethodDecoratorContextType(),
								  {classType, valueType});
}

// checker.go:30935 — newClassGetterDecoratorContextType
Type* Checker::newClassGetterDecoratorContextType(Type* classType,
												  Type* valueType) {
	return tryCreateTypeReference(getGlobalClassGetterDecoratorContextType(),
								  {classType, valueType});
}

// checker.go:30939 — newClassSetterDecoratorContextType
Type* Checker::newClassSetterDecoratorContextType(Type* classType,
												  Type* valueType) {
	return tryCreateTypeReference(getGlobalClassSetterDecoratorContextType(),
								  {classType, valueType});
}

// checker.go:30943 — newClassAccessorDecoratorContextType
Type* Checker::newClassAccessorDecoratorContextType(Type* thisType,
													Type* valueType) {
	return tryCreateTypeReference(getGlobalClassAccessorDecoratorContextType(),
								  {thisType, valueType});
}

// checker.go:30947 — newClassFieldDecoratorContextType
Type* Checker::newClassFieldDecoratorContextType(Type* thisType,
												 Type* valueType) {
	return tryCreateTypeReference(getGlobalClassFieldDecoratorContextType(),
								  {thisType, valueType});
}

// Gets a type like `{ name: "foo", private: false, static: true }` that is used to provided member-specific
// details that will be intersected with a decorator context type.
// checker.go:30953
Type* Checker::getClassMemberDecoratorContextOverrideType(Type* nameType,
														  bool isPrivate,
														  bool isStatic) {
	CachedTypeKind kind =
		isPrivate ? (isStatic ? CachedTypeKind::DecoratorContextPrivateStatic
							  : CachedTypeKind::DecoratorContextPrivate)
				  : (isStatic ? CachedTypeKind::DecoratorContextStatic
							  : CachedTypeKind::DecoratorContext);
	CachedTypeKey key{kind, nameType->id};
	if (auto it = cachedTypes.find(key);
		it != cachedTypes.end() && it->second != nullptr) {
		return it->second;
	}
	SymbolTable members;
	members["name"] = newProperty("name", nameType);
	members["private"] =
		newProperty("private", isPrivate ? trueType : falseType);
	members["static"] =
		newProperty("static", isStatic ? trueType : falseType);
	Type* overrideType = newAnonymousType(nullptr, members, {}, {}, {});
	cachedTypes[key] = overrideType;
	return overrideType;
}

// checker.go:30971 — newClassMemberDecoratorContextTypeForNode
Type* Checker::newClassMemberDecoratorContextTypeForNode(Node* node,
														 Type* thisType,
														 Type* valueType) {
	bool isStatic = hasStaticModifier(node);
	bool isPrivate = isPrivateIdentifier(node->name());
	Type* nameType;
	if (isPrivate) {
		nameType = getStringLiteralType(node->name()->text());
	} else {
		nameType = getLiteralTypeFromPropertyName(node->name());
	}
	Type* contextType;
	if (isMethodDeclaration(node)) {
		contextType = newClassMethodDecoratorContextType(thisType, valueType);
	} else if (isGetAccessorDeclaration(node)) {
		contextType = newClassGetterDecoratorContextType(thisType, valueType);
	} else if (isSetAccessorDeclaration(node)) {
		contextType = newClassSetterDecoratorContextType(thisType, valueType);
	} else if (isAutoAccessorPropertyDeclaration(node)) {
		contextType = newClassAccessorDecoratorContextType(thisType, valueType);
	} else if (isPropertyDeclaration(node)) {
		contextType = newClassFieldDecoratorContextType(thisType, valueType);
	} else {
		TSC_UNREACHABLE("Unhandled case in createClassMemberDecoratorContextTypeForNode");
	}
	Type* overrideType =
		getClassMemberDecoratorContextOverrideType(nameType, isPrivate, isStatic);
	return getIntersectionType({contextType, overrideType});
}

// checker.go:31000 — newClassAccessorDecoratorTargetType
Type* Checker::newClassAccessorDecoratorTargetType(Type* thisType,
												   Type* valueType) {
	return tryCreateTypeReference(getGlobalClassAccessorDecoratorTargetType(),
								  {thisType, valueType});
}

// checker.go:31004 — newClassAccessorDecoratorResultType
Type* Checker::newClassAccessorDecoratorResultType(Type* thisType,
												   Type* valueType) {
	return tryCreateTypeReference(getGlobalClassAccessorDecoratorResultType(),
								  {thisType, valueType});
}

// checker.go:31008 — newClassFieldDecoratorInitializerMutatorType
Type* Checker::newClassFieldDecoratorInitializerMutatorType(Type* thisType,
															Type* valueType) {
	Symbol* thisParam = newParameter("this", thisType);
	Symbol* valueParam = newParameter("value", valueType);
	return newFunctionType({}, thisParam, {valueParam}, valueType);
}

// Creates a call signature for an ES Decorator. This method is used by the semantics of
// `getESDecoratorCallSignature`, which you should probably be using instead.
// checker.go:31016
Signature* Checker::newESDecoratorCallSignature(Type* targetType,
												Type* contextType,
												Type* nonOptionalReturnType) {
	Symbol* targetParam = newParameter("target", targetType);
	Symbol* contextParam = newParameter("context", contextType);
	Type* returnType = getUnionType({nonOptionalReturnType, voidType});
	return newCallSignature({}, nullptr /*thisParameter*/,
							{targetParam, contextParam}, returnType);
}

// Creates a synthetic `FunctionType`
// checker.go:31024
Type* Checker::newFunctionType(const std::vector<Type*>& typeParameters,
							   Symbol* thisParameter,
							   const std::vector<Symbol*>& parameters,
							   Type* returnType) {
	Signature* signature =
		newCallSignature(typeParameters, thisParameter, parameters, returnType);
	return getOrCreateTypeFromSignature(signature);
}

// checker.go:31030 — newGetterFunctionType
Type* Checker::newGetterFunctionType(Type* t) {
	return newFunctionType({}, nullptr /*thisParameter*/, {}, t);
}

// checker.go:31034 — newSetterFunctionType
Type* Checker::newSetterFunctionType(Type* t) {
	Symbol* valueParam = newParameter("value", t);
	return newFunctionType({}, nullptr /*thisParameter*/, {valueParam},
						 voidType);
}

// Creates a synthetic `Signature` corresponding to a call signature.
// checker.go:31040
Signature* Checker::newCallSignature(const std::vector<Type*>& typeParameters,
									 Symbol* thisParameter,
									 const std::vector<Symbol*>& parameters,
									 Type* returnType) {
	Node* decl = factory.newFunctionTypeNode(
		nullptr, nullptr, factory.newKeywordTypeNode(Kind::AnyKeyword));
	return newSignature(SignatureFlagsNone, decl, typeParameters, thisParameter,
						parameters, returnType, nullptr,
						static_cast<int>(parameters.size()));
}

// checker.go:31045 — newTypedPropertyDescriptorType
Type* Checker::newTypedPropertyDescriptorType(Type* propertyType) {
	return createTypeFromGenericGlobalType(getGlobalTypedPropertyDescriptorType(),
										   {propertyType});
}

// checker.go:31049 — getParentTypeOfClassElement
Type* Checker::getParentTypeOfClassElement(Node* node) {
	Symbol* classSymbol = getSymbolOfNode(node->parent);
	if (isStatic(node)) {
		return getTypeOfSymbol(classSymbol);
	}
	return getDeclaredTypeOfSymbol(classSymbol);
}

// checker.go:31057 — getClassElementPropertyKeyType
Type* Checker::getClassElementPropertyKeyType(Node* element) {
	Node* name = element->name();
	switch (name->kind) {
	case Kind::Identifier:
	case Kind::NumericLiteral:
	case Kind::StringLiteral:
		return getStringLiteralType(name->text());
	case Kind::ComputedPropertyName: {
		Type* nameType = checkComputedPropertyName(name);
		if (isTypeAssignableToKind(nameType, TypeFlagsESSymbolLike)) {
			return nameType;
		}
		return stringType;
	}
	default:
		break;
	}
	return errorType;
}

// checker.go:31073 — getTypeOfPropertyOfContextualType
Type* Checker::getTypeOfPropertyOfContextualType(Type* t,
												 const std::string& name) {
	return getTypeOfPropertyOfContextualTypeEx(t, name, nullptr);
}

// checker.go:31077 — getTypeOfPropertyOfContextualTypeEx
Type* Checker::getTypeOfPropertyOfContextualTypeEx(Type* t,
												   const std::string& name,
												   Type* nameType) {
	return mapTypeEx(
		t,
		[this, &name, nameType](Type* t) -> Type* {
			if (t->flags & TypeFlagsIntersection) {
				std::vector<Type*> types;
				std::vector<Type*> indexInfoCandidates;
				bool ignoreIndexInfos = false;
				for (Type* constituentType : t->types()) {
					if ((constituentType->flags & TypeFlagsObject) == 0) {
						continue;
					}
					if (isGenericMappedType(constituentType) &&
						getMappedTypeNameTypeKind(constituentType) !=
							MappedTypeNameTypeKind::Remapping) {
						Type* substitutedType =
							getIndexedMappedTypeSubstitutedTypeOfContextualType(
								constituentType, name, nameType);
						types = appendContextualPropertyTypeConstituent(
							std::move(types), substitutedType);
						continue;
					}
					Type* propertyType =
						getTypeOfConcretePropertyOfContextualType(
							constituentType, name);
					if (propertyType == nullptr) {
						if (!ignoreIndexInfos) {
							indexInfoCandidates.push_back(constituentType);
						}
						continue;
					}
					ignoreIndexInfos = true;
					indexInfoCandidates.clear();
					types = appendContextualPropertyTypeConstituent(
						std::move(types), propertyType);
				}
				for (Type* candidate : indexInfoCandidates) {
					Type* indexInfoType =
						getTypeFromIndexInfosOfContextualType(candidate, name,
															  nameType);
					types = appendContextualPropertyTypeConstituent(
						std::move(types), indexInfoType);
				}
				if (types.empty()) {
					return nullptr;
				}
				if (types.size() == 1) {
					return types[0];
				}
				return getIntersectionType(types);
			}
			if ((t->flags & TypeFlagsObject) == 0) {
				return nullptr;
			}
			if (isGenericMappedType(t) &&
				getMappedTypeNameTypeKind(t) !=
					MappedTypeNameTypeKind::Remapping) {
				return getIndexedMappedTypeSubstitutedTypeOfContextualType(
					t, name, nameType);
			}
			Type* result = getTypeOfConcretePropertyOfContextualType(t, name);
			if (result != nullptr) {
				return result;
			}
			return getTypeFromIndexInfosOfContextualType(t, name, nameType);
		},
		true /*noReductions*/);
}

// checker.go:31131 — getIndexedMappedTypeSubstitutedTypeOfContextualType
Type* Checker::getIndexedMappedTypeSubstitutedTypeOfContextualType(
	Type* t, const std::string& name, Type* nameType) {
	Type* propertyNameType = nameType;
	if (propertyNameType == nullptr) {
		propertyNameType = getStringLiteralType(name);
	}
	Type* constraint = getConstraintTypeFromMappedType(t);
	// special case for conditional types pretending to be negated types
	if ((t->AsMappedType()->nameType != nullptr &&
		 isExcludedMappedPropertyName(t->AsMappedType()->nameType,
									  propertyNameType)) ||
		isExcludedMappedPropertyName(constraint, propertyNameType)) {
		return nullptr;
	}
	Type* constraintOfConstraint = getBaseConstraintOrType(constraint);
	if (!isTypeAssignableTo(propertyNameType, constraintOfConstraint)) {
		return nullptr;
	}
	return substituteIndexedMappedType(t, propertyNameType);
}

// checker.go:31148 — isExcludedMappedPropertyName
bool Checker::isExcludedMappedPropertyName(Type* t, Type* propertyNameType) {
	if (t->flags & TypeFlagsConditional) {
		return (getReducedType(getTrueTypeFromConditionalType(t))->flags &
				TypeFlagsNever) != 0 &&
			   getActualTypeVariable(getFalseTypeFromConditionalType(t)) ==
				   getActualTypeVariable(t->AsConditionalType()->checkType) &&
			   isTypeAssignableTo(propertyNameType,
								  t->AsConditionalType()->extendsType);
	}
	if (t->flags & TypeFlagsIntersection) {
		return someRange(t->types(), [this, propertyNameType](Type* t) {
			return isExcludedMappedPropertyName(t, propertyNameType);
		});
	}
	return false;
}

// checker.go:31162 — getTypeOfConcretePropertyOfContextualType
Type* Checker::getTypeOfConcretePropertyOfContextualType(
	Type* t, const std::string& name) {
	Symbol* prop = getPropertyOfType(t, name);
	if (prop == nullptr || isCircularMappedProperty(prop)) {
		return nullptr;
	}
	return removeMissingType(getTypeOfSymbol(prop),
							 (prop->flags & SymbolFlagsOptional) != 0);
}

// checker.go:31170 — getTypeFromIndexInfosOfContextualType
Type* Checker::getTypeFromIndexInfosOfContextualType(Type* t,
													 const std::string& name,
													 Type* nameType) {
	if (isTupleType(t) && isNumericLiteralName(name) &&
		numberFromString(name).v >= 0) {
		Type* restType = getElementTypeOfSliceOfTupleType(
			t, targetTupleType(t)->fixedLength, 0 /*endSkipCount*/,
			false /*writing*/, true /*noReductions*/);
		if (restType != nullptr) {
			return restType;
		}
	}
	if (nameType == nullptr) {
		nameType = getStringLiteralType(name);
	}
	IndexInfo* indexInfo =
		findApplicableIndexInfo(getIndexInfosOfStructuredType(t), nameType);
	if (indexInfo == nullptr) {
		return nullptr;
	}
	return indexInfo->valueType;
}

// checker.go:31187 — isCircularMappedProperty
bool Checker::isCircularMappedProperty(Symbol* symbol) {
	if (symbol->checkFlags & CheckFlagsMapped) {
		ValueSymbolLinks* links = valueSymbolLinks.Get(symbol);
		return (links->resolvedType == nullptr ||
				staleForCheckFile(links->resolvedTypeCheckFile)) &&
			   findResolutionCycleStartIndex(
				   symbol, TypeSystemPropertyName::Type) >= 0;
	}
	return false;
}

// checker.go:31195 — appendContextualPropertyTypeConstituent
std::vector<Type*> Checker::appendContextualPropertyTypeConstituent(
	std::vector<Type*> types, Type* t) {
	// any doesn't provide any contextual information but could spoil the overall result by nullifying contextual information
	// provided by other intersection constituents so it gets replaced with `unknown` as `T & unknown` is just `T` and all
	// types computed based on the contextual information provided by other constituens are still assignable to any
	if (t == nullptr) {
		return types;
	}
	if (t->flags & TypeFlagsAny) {
		types.push_back(unknownType);
		return types;
	}
	types.push_back(t);
	return types;
}

// Return the contextual type for a given expression node. During overload resolution, a contextual type may temporarily
// be "pushed" onto a node using the contextualType property.
// checker.go:31208
Type* Checker::getApparentTypeOfContextualType(Node* node,
											   ContextFlags contextFlags) {
	Type* contextualType;
	if (isObjectLiteralMethod(node)) {
		contextualType =
			getContextualTypeForObjectLiteralMethod(node, contextFlags);
	} else {
		contextualType = getContextualType(node, contextFlags);
	}
	Type* instantiatedType =
		instantiateContextualType(contextualType, node, contextFlags);
	if (instantiatedType != nullptr &&
		!((contextFlags & ContextFlagsNoConstraints) != 0 &&
		  (instantiatedType->flags & TypeFlagsTypeVariable) != 0)) {
		Type* apparentType = mapTypeEx(
			instantiatedType,
			[this](Type* t) -> Type* {
				if (t->objectFlags & ObjectFlagsMapped) {
					return t;
				}
				return getApparentType(t);
			},
			true);
		if ((apparentType->flags & TypeFlagsUnion) != 0 &&
			isObjectLiteralExpression(node)) {
			return discriminateContextualTypeByObjectMembers(node,
															 apparentType);
		}
		if ((apparentType->flags & TypeFlagsUnion) != 0 &&
			isJsxAttributes(node)) {
			return discriminateContextualTypeByJSXAttributes(node,
															 apparentType);
		}
		return apparentType;
	}
	return nullptr;
}

// ObjectLiteralDiscriminator — checker.go:31236
int ObjectLiteralDiscriminator::len() {
	return static_cast<int>(props.size() + members.size());
}

std::string ObjectLiteralDiscriminator::name(int index) {
	if (index < static_cast<int>(props.size())) {
		return props[index]->symbol()->name;
	}
	return members[index - static_cast<int>(props.size())]->name;
}

bool ObjectLiteralDiscriminator::matches(int index, Type* t) {
	Type* propType;
	if (index < static_cast<int>(props.size())) {
		Node* prop = props[index];
		if (isPropertyAssignment(prop) || isJsxAttribute(prop)) {
			Node* initializer = prop->initializer();
			if (initializer != nullptr) {
				propType = c->getContextFreeTypeOfExpression(initializer);
			} else {
				propType = c->trueType; // JsxAttribute without initializer is always true
			}
		} else {
			propType = c->getContextFreeTypeOfExpression(prop->name());
		}
	} else {
		propType = c->undefinedType;
	}
	for (Type* s : distributed(propType)) {
		if (c->isTypeAssignableTo(s, t)) {
			return true;
		}
	}
	return false;
}

// checker.go:31257 — discriminateContextualTypeByObjectMembers
Type* Checker::discriminateContextualTypeByObjectMembers(Node* node,
														 Type* contextualType) {
	DiscriminatedContextualTypeKey key{getNodeId(node), contextualType->id};
	Type* discriminatedCached = nullptr;
	if (auto it = discriminatedContextualTypes.find(key);
	    it != discriminatedContextualTypes.end()) {
		discriminatedCached = it->second;
	}
	if (discriminatedCached != nullptr) {
		return discriminatedCached;
	}
	Type* discriminated =
		getMatchingUnionConstituentForObjectLiteral(contextualType, node);
	if (discriminated == nullptr) {
		std::vector<Node*> discriminantProperties =
			filterRange(node->properties(), [this, contextualType](Node* p) {
				Symbol* symbol = p->symbol();
				if (symbol == nullptr) {
					return false;
				}
				if (isPropertyAssignment(p)) {
					return isPossiblyDiscriminantValue(p->initializer()) &&
						   isDiscriminantProperty(contextualType, symbol->name);
				}
				if (isShorthandPropertyAssignment(p)) {
					return isDiscriminantProperty(contextualType, symbol->name);
				}
				return false;
			});
		std::vector<Symbol*> discriminantMembers = filterRange(
			getPropertiesOfType(contextualType),
			[this, contextualType, node](Symbol* s) {
				return (s->flags & SymbolFlagsOptional) != 0 &&
					   getSymbolFromTable(node->symbol()->members, s->name) == nullptr &&
					   isDiscriminantProperty(contextualType, s->name);
			});
		ObjectLiteralDiscriminator discriminator;
		discriminator.c = this;
		discriminator.props = discriminantProperties;
		discriminator.members = discriminantMembers;
		discriminated = discriminateTypeByDiscriminableItems(contextualType,
															 discriminator);
	}
	discriminatedContextualTypes[key] = discriminated;
	return discriminated;
}

// checker.go:31284 — getMatchingUnionConstituentForObjectLiteral
Type* Checker::getMatchingUnionConstituentForObjectLiteral(Type* unionType,
														   Node* node) {
	std::string keyPropertyName = getKeyPropertyName(unionType);
	if (!keyPropertyName.empty()) {
		Node* propNode = findRange(
			node->properties(), [this, &keyPropertyName](Node* p) {
				return p->symbol() != nullptr && isPropertyAssignment(p) &&
					   p->symbol()->name == keyPropertyName &&
					   isPossiblyDiscriminantValue(p->initializer());
			});
		if (propNode != nullptr) {
			Type* propType =
				getContextFreeTypeOfExpression(propNode->initializer());
			return getConstituentTypeForKeyType(unionType, propType);
		}
	}
	return nullptr;
}

// Return true if the given expression is possibly a discriminant value. We limit the kinds of
// expressions we check to those that don't depend on their contextual type in order not to cause
// recursive (and possibly infinite) invocations of getContextualType.
// checker.go:31300
bool Checker::isPossiblyDiscriminantValue(Node* node) {
	switch (node->kind) {
	case Kind::StringLiteral:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::TemplateExpression:
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::NullKeyword:
	case Kind::Identifier:
	case Kind::UndefinedKeyword:
		return true;
	case Kind::PropertyAccessExpression:
	case Kind::ParenthesizedExpression:
		return isPossiblyDiscriminantValue(node->expression());
	case Kind::JsxExpression:
		return node->expression() == nullptr ||
			   isPossiblyDiscriminantValue(node->expression());
	default:
		break;
	}
	return false;
}

// If the given contextual type contains instantiable types and if a mapper representing
// return type inferences is available, instantiate those types using that mapper.
// checker.go:31318
Type* Checker::instantiateContextualType(Type* contextualType, Node* node,
										 ContextFlags contextFlags) {
	if (contextualType != nullptr &&
		maybeTypeOfKind(contextualType, TypeFlagsInstantiable)) {
		InferenceContext* inferenceContext = getInferenceContext(node);
		// If no inferences have been made, and none of the type parameters for which we are inferring
		// specify default types, nothing is gained from instantiating as type parameters would just be
		// replaced with their constraints similar to the apparent type.
		if (inferenceContext != nullptr) {
			if ((contextFlags & ContextFlagsSignature) != 0 &&
				someRange(inferenceContext->inferences,
						  hasInferenceCandidatesOrDefault)) {
				// For contextual signatures we incorporate all inferences made so far, e.g. from return
				// types as well as arguments to the left in a function call.
				Type* t = instantiateInstantiableTypes(
					contextualType, inferenceContext->nonFixingMapper);
				if ((t->flags & TypeFlagsAnyOrUnknown) == 0) {
					return t;
				}
			}
			if (inferenceContext->returnMapper != nullptr) {
				// For other purposes (e.g. determining whether to produce literal types) we only
				// incorporate inferences made from the return type in a function call. We remove
				// the 'boolean' type from the contextual type such that contextually typed boolean
				// literals actually end up widening to 'boolean' (see #48363).
				Type* t = instantiateInstantiableTypes(
					contextualType, inferenceContext->returnMapper);
				if ((t->flags & TypeFlagsAnyOrUnknown) == 0) {
					if ((t->flags & TypeFlagsUnion) != 0 &&
						containsType(t->types(), regularFalseType) &&
						containsType(t->types(), regularTrueType)) {
						return filterType(t, [this](Type* t) {
							return t != regularFalseType &&
								   t != regularTrueType;
						});
					}
					return t;
				}
			}
		}
	}
	return contextualType;
}

// This function is similar to instantiateType, except that (a) it only instantiates types that
// are classified as instantiable (i.e. it doesn't instantiate object types), and (b) it performs
// no reductions on instantiated union types.
// checker.go:31348
Type* Checker::instantiateInstantiableTypes(Type* t, TypeMapper* mapper) {
	if (t->flags & TypeFlagsInstantiable) {
		return instantiateType(t, mapper);
	}
	if (t->flags & TypeFlagsUnion) {
		return getUnionTypeEx(
			mapRange(t->types(),
					 [this, mapper](Type* t) {
						 return instantiateInstantiableTypes(t, mapper);
					 }),
			UnionReduction::None, nullptr, nullptr);
	}
	if (t->flags & TypeFlagsIntersection) {
		return getIntersectionType(
			mapRange(t->types(), [this, mapper](Type* t) {
				return instantiateInstantiableTypes(t, mapper);
			}));
	}
	return t;
}

// checker.go:31366 — pushCachedContextualType
void Checker::pushCachedContextualType(Node* node) {
	pushContextualType(node, getContextualType(node, ContextFlagsNone),
					 true /*isCache*/);
}

// checker.go:31370 — pushContextualType
void Checker::pushContextualType(Node* node, Type* t, bool isCache) {
	contextualInfos.push_back(ContextualInfo{node, t, isCache});
}

// checker.go:31374 — popContextualType
void Checker::popContextualType() {
	size_t lastIndex = contextualInfos.size() - 1;
	contextualInfos[lastIndex] = ContextualInfo{};
	contextualInfos.pop_back();
}

// checker.go:31380 — findContextualNode
int Checker::findContextualNode(Node* node, bool includeCaches) {
	for (size_t i = 0; i < contextualInfos.size(); i++) {
		const ContextualInfo& info = contextualInfos[i];
		if (node == info.node && (includeCaches || !info.isCache)) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

// Returns true if the given expression contains (at any level of nesting) a function or arrow expression
// that is subject to contextual typing.
// checker.go:31394
bool Checker::isContextSensitive(Node* node) {
	switch (node->kind) {
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::MethodDeclaration:
	case Kind::FunctionDeclaration:
		return isContextSensitiveFunctionLikeDeclaration(node);
	case Kind::ObjectLiteralExpression:
		return someRange(node->properties(),
						 [this](Node* p) { return isContextSensitive(p); });
	case Kind::ArrayLiteralExpression:
		return someRange(node->elements(),
						 [this](Node* e) { return isContextSensitive(e); });
	case Kind::ConditionalExpression:
		return isContextSensitive(node->as<ConditionalExpression>()->WhenTrue) ||
			   isContextSensitive(node->as<ConditionalExpression>()->WhenFalse);
	case Kind::BinaryExpression: {
		BinaryExpression* binary = node->as<BinaryExpression>();
		return (binary->OperatorToken->kind == Kind::BarBarToken ||
				binary->OperatorToken->kind == Kind::QuestionQuestionToken) &&
			   (isContextSensitive(binary->Left) ||
				isContextSensitive(binary->Right));
	}
	case Kind::PropertyAssignment:
		return isContextSensitive(node->initializer());
	case Kind::ParenthesizedExpression:
		return isContextSensitive(node->expression());
	case Kind::JsxAttributes:
		return someRange(node->properties(),
						 [this](Node* p) { return isContextSensitive(p); }) ||
			   (isJsxOpeningElement(node->parent) &&
				someRange(node->parent->parent->children()->nodes,
						  [this](Node* c) { return isContextSensitive(c); }));
	case Kind::JsxAttribute: {
		// If there is no initializer, JSX attribute has a boolean value of true which is not context sensitive.
		Node* initializer = node->initializer();
		return initializer != nullptr && isContextSensitive(initializer);
	}
	case Kind::JsxExpression: {
		// It is possible to that node.expression is undefined (e.g <div x={} />)
		Node* expression = node->expression();
		return expression != nullptr && isContextSensitive(expression);
	}
	case Kind::YieldExpression: {
		Node* expression = node->expression();
		return expression != nullptr && isContextSensitive(expression);
	}
	default:
		break;
	}
	return false;
}

// checker.go:31432
bool Checker::isContextSensitiveFunctionLikeDeclaration(Node* node) {
	return hasContextSensitiveParameters(node) ||
		   hasContextSensitiveReturnExpression(node) ||
		   hasContextSensitiveYieldExpression(node);
}

// checker.go:31436
bool Checker::hasContextSensitiveReturnExpression(Node* node) {
	if (node->typeParameterList() != nullptr || node->type() != nullptr) {
		return false;
	}
	Node* body = node->body();
	if (body == nullptr) {
		return false;
	}
	if (!isBlock(body)) {
		return isContextSensitive(body);
	}
	return forEachReturnStatement(body, [this](Node* statement) {
		return statement->expression() != nullptr &&
			   isContextSensitive(statement->expression());
	});
}

// checker.go:31452
bool Checker::hasContextSensitiveYieldExpression(Node* node) {
	return (getFunctionFlags(node) & FunctionFlagsGenerator) != 0 &&
		   node->body() != nullptr &&
		   forEachYieldExpression(
			   node->body(),
			   [this](Node* yieldExpr) { return isContextSensitive(yieldExpr); });
}

// checker.go:31456 — pushInferenceContext
void Checker::pushInferenceContext(Node* node, InferenceContext* context) {
	inferenceContextInfos.push_back(InferenceContextInfo{node, context});
}

// checker.go:31460 — popInferenceContext
void Checker::popInferenceContext() {
	size_t lastIndex = inferenceContextInfos.size() - 1;
	inferenceContextInfos[lastIndex] = InferenceContextInfo{};
	inferenceContextInfos.pop_back();
}

// checker.go:31466 — getInferenceContext
InferenceContext* Checker::getInferenceContext(Node* node) {
	for (auto it = inferenceContextInfos.rbegin();
		 it != inferenceContextInfos.rend(); ++it) {
		if (isNodeDescendantOf(node, it->node)) {
			return it->context;
		}
	}
	return nullptr;
}

// checker.go:31474 — getTypeFacts
TypeFacts Checker::getTypeFacts(Type* t, TypeFacts mask) {
	return getTypeFactsWorker(t, mask) & mask;
}

// checker.go:31478 — hasTypeFacts
bool Checker::hasTypeFacts(Type* t, TypeFacts mask) {
	return getTypeFacts(t, mask) != 0;
}

// checker.go:31482 — getTypeFactsWorker
TypeFacts Checker::getTypeFactsWorker(Type* t, TypeFacts callerOnlyNeeds) {
	if (t->flags & (TypeFlagsIntersection | TypeFlagsInstantiable)) {
		t = getBaseConstraintOfType(t);
		if (t == nullptr) {
			t = unknownType;
		}
	}
	TypeFlags flags = t->flags;
	if (flags & (TypeFlagsString | TypeFlagsStringMapping)) {
		if (strictNullChecks) {
			return TypeFactsStringStrictFacts;
		}
		return TypeFactsStringFacts;
	}
	if (flags & (TypeFlagsStringLiteral | TypeFlagsTemplateLiteral)) {
		bool isEmpty = (flags & TypeFlagsStringLiteral) != 0 &&
					   getStringLiteralValue(t).empty();
		if (strictNullChecks) {
			if (isEmpty) {
				return TypeFactsEmptyStringStrictFacts;
			}
			return TypeFactsNonEmptyStringStrictFacts;
		}
		if (isEmpty) {
			return TypeFactsEmptyStringFacts;
		}
		return TypeFactsNonEmptyStringFacts;
	}
	if (flags & (TypeFlagsNumber | TypeFlagsEnum)) {
		if (strictNullChecks) {
			return TypeFactsNumberStrictFacts;
		}
		return TypeFactsNumberFacts;
	}
	if (flags & TypeFlagsNumberLiteral) {
		bool isZero = getNumberLiteralValue(t) == Number(0);
		if (strictNullChecks) {
			if (isZero) {
				return TypeFactsZeroNumberStrictFacts;
			}
			return TypeFactsNonZeroNumberStrictFacts;
		}
		if (isZero) {
			return TypeFactsZeroNumberFacts;
		}
		return TypeFactsNonZeroNumberFacts;
	}
	if (flags & TypeFlagsBigInt) {
		if (strictNullChecks) {
			return TypeFactsBigIntStrictFacts;
		}
		return TypeFactsBigIntFacts;
	}
	if (flags & TypeFlagsBigIntLiteral) {
		bool isZero = isZeroBigInt(t);
		if (strictNullChecks) {
			if (isZero) {
				return TypeFactsZeroBigIntStrictFacts;
			}
			return TypeFactsNonZeroBigIntStrictFacts;
		}
		if (isZero) {
			return TypeFactsZeroBigIntFacts;
		}
		return TypeFactsNonZeroBigIntFacts;
	}
	if (flags & TypeFlagsBoolean) {
		if (strictNullChecks) {
			return TypeFactsBooleanStrictFacts;
		}
		return TypeFactsBooleanFacts;
	}
	if (flags & TypeFlagsBooleanLike) {
		bool isFalse = t == falseType || t == regularFalseType;
		if (strictNullChecks) {
			if (isFalse) {
				return TypeFactsFalseStrictFacts;
			}
			return TypeFactsTrueStrictFacts;
		}
		if (isFalse) {
			return TypeFactsFalseFacts;
		}
		return TypeFactsTrueFacts;
	}
	if (flags & TypeFlagsObject) {
		TypeFacts possibleFacts;
		if (strictNullChecks) {
			possibleFacts = TypeFactsEmptyObjectStrictFacts |
							TypeFactsFunctionStrictFacts |
							TypeFactsObjectStrictFacts;
		} else {
			possibleFacts = TypeFactsEmptyObjectFacts |
							TypeFactsFunctionFacts | TypeFactsObjectFacts;
		}
		if ((callerOnlyNeeds & possibleFacts) == 0) {
			// If the caller doesn't care about any of the facts that we could possibly produce,
			// return zero so we can skip resolving members.
			return TypeFactsNone;
		}
		if ((t->objectFlags & ObjectFlagsAnonymous) != 0 &&
			isEmptyObjectType(t)) {
			if (strictNullChecks) {
				return TypeFactsEmptyObjectStrictFacts;
			}
			return TypeFactsEmptyObjectFacts;
		}
		if (isFunctionObjectType(t)) {
			if (strictNullChecks) {
				return TypeFactsFunctionStrictFacts;
			}
			return TypeFactsFunctionFacts;
		}
		if (strictNullChecks) {
			return TypeFactsObjectStrictFacts;
		}
		return TypeFactsObjectFacts;
	}
	if (flags & TypeFlagsVoid) {
		return TypeFactsVoidFacts;
	}
	if (flags & TypeFlagsUndefined) {
		return TypeFactsUndefinedFacts;
	}
	if (flags & TypeFlagsNull) {
		return TypeFactsNullFacts;
	}
	if (flags & TypeFlagsESSymbolLike) {
		if (strictNullChecks) {
			return TypeFactsSymbolStrictFacts;
		}
		return TypeFactsSymbolFacts;
	}
	if (flags & TypeFlagsNonPrimitive) {
		if (strictNullChecks) {
			return TypeFactsObjectStrictFacts;
		}
		return TypeFactsObjectFacts;
	}
	if (flags & TypeFlagsNever) {
		return TypeFactsNone;
	}
	if (flags & TypeFlagsUnion) {
		TypeFacts facts = TypeFactsNone;
		for (Type* u : t->types()) {
			facts |= getTypeFactsWorker(u, callerOnlyNeeds);
		}
		return facts;
	}
	if (flags & TypeFlagsIntersection) {
		return getIntersectionTypeFacts(t, callerOnlyNeeds);
	}
	return TypeFactsUnknownFacts;
}

// checker.go:31618 — getIntersectionTypeFacts
TypeFacts Checker::getIntersectionTypeFacts(Type* t, TypeFacts callerOnlyNeeds) {
	// When an intersection contains a primitive type we ignore object type constituents as they are
	// presumably type tags. For example, in string & { __kind__: "name" } we ignore the object type.
	bool ignoreObjects = maybeTypeOfKind(t, TypeFlagsPrimitive);
	// When computing the type facts of an intersection type, certain type facts are computed as `and`
	// and others are computed as `or`.
	TypeFacts oredFacts = TypeFactsNone;
	TypeFacts andedFacts = TypeFactsAll;
	for (Type* u : t->types()) {
		if (!(ignoreObjects && (u->flags & TypeFlagsObject) != 0)) {
			TypeFacts f = getTypeFactsWorker(u, callerOnlyNeeds);
			oredFacts |= f;
			andedFacts &= f;
		}
	}
	return (oredFacts & TypeFactsOrFactsMask) |
		   (andedFacts & TypeFactsAndFactsMask);
}

namespace {

// checker.go:31621 — isZeroBigInt
bool isZeroBigInt(Type* t) {
	return getBigIntLiteralValue(t) == PseudoBigInt{};
}

} // namespace

// checker.go:31625 — isFunctionObjectType
bool Checker::isFunctionObjectType(Type* t) {
	if (t->objectFlags & ObjectFlagsEvolvingArray) {
		return false;
	}
	// We do a quick check for a "bind" property before performing the more expensive subtype
	// check. This gives us a quicker out in the common case where an object type is not a function.
	StructuredType* resolved = resolveStructuredTypeMembers(t);
	return !resolved->signatures.empty() ||
		   (getSymbolFromTable(resolved->members, "bind") != nullptr &&
			isTypeSubtypeOf(t, globalFunctionType));
}

// checker.go:31637 — getTypeWithFacts
Type* Checker::getTypeWithFacts(Type* t, TypeFacts include) {
	return filterType(t,
					  [this, include](Type* t) {
						  return hasTypeFacts(t, include);
					  });
}

// This function is similar to getTypeWithFacts, except that in strictNullChecks mode it replaces type
// unknown with the union {} | null | undefined (and reduces that accordingly), and it intersects remaining
// instantiable types with {}, {} | null, or {} | undefined in order to remove null and/or undefined.
// checker.go:31645
Type* Checker::getAdjustedTypeWithFacts(Type* t, TypeFacts facts) {
	Type* reduced = recombineUnknownType(getTypeWithFacts(
		(strictNullChecks && (t->flags & TypeFlagsUnknown) != 0)
			? unknownUnionType
			: t,
		facts));
	if (strictNullChecks) {
		switch (facts) {
		case TypeFactsNEUndefined:
			return removeNullableByIntersection(reduced, TypeFactsEQUndefined,
												TypeFactsEQNull,
												TypeFactsIsNull, nullType);
		case TypeFactsNENull:
			return removeNullableByIntersection(reduced, TypeFactsEQNull,
												TypeFactsEQUndefined,
												TypeFactsIsUndefined,
												undefinedType);
		case TypeFactsNEUndefinedOrNull:
		case TypeFactsTruthy:
			return mapType(reduced, [this](Type* t) {
				if (hasTypeFacts(t, TypeFactsEQUndefinedOrNull)) {
					return getGlobalNonNullableTypeInstantiation(t);
				}
				return t;
			});
		default:
			break;
		}
	}
	return reduced;
}

// checker.go:31664 — removeNullableByIntersection
Type* Checker::removeNullableByIntersection(Type* t, TypeFacts targetFacts,
											TypeFacts otherFacts,
											TypeFacts otherIncludesFacts,
											Type* otherType) {
	TypeFacts facts = getTypeFacts(t, TypeFactsEQUndefined | TypeFactsEQNull |
										  TypeFactsIsUndefined |
										  TypeFactsIsNull);
	// Simply return the type if it never compares equal to the target nullable.
	if ((facts & targetFacts) == 0) {
		return t;
	}
	// By default we intersect with a union of {} and the opposite nullable.
	Type* emptyAndOtherUnion = getUnionType({emptyObjectType, otherType});
	// For each constituent type that can compare equal to the target nullable, intersect with the above union
	// if the type doesn't already include the opposite nullable and the constituent can compare equal to the
	// opposite nullable; otherwise, just intersect with {}.
	return mapType(t, [this, targetFacts, facts, otherIncludesFacts, otherFacts,
					   emptyAndOtherUnion](Type* t) {
		if (hasTypeFacts(t, targetFacts)) {
			if ((facts & otherIncludesFacts) == 0 &&
				hasTypeFacts(t, otherFacts)) {
				return getIntersectionType({t, emptyAndOtherUnion});
			}
			return getIntersectionType({t, emptyObjectType});
		}
		return t;
	});
}

// checker.go:31686 — recombineUnknownType
Type* Checker::recombineUnknownType(Type* t) {
	if (t == unknownUnionType) {
		return unknownType;
	}
	return t;
}

// checker.go:31693 — getGlobalNonNullableTypeInstantiation
Type* Checker::getGlobalNonNullableTypeInstantiation(Type* t) {
	Symbol* alias = getGlobalNonNullableTypeAliasOrNil();
	if (alias != nullptr) {
		return getTypeAliasInstantiation(alias, {t}, nullptr);
	}
	return getIntersectionType({t, emptyObjectType});
}

// checker.go:31701 — convertAutoToAny
Type* Checker::convertAutoToAny(Type* t) {
	if (t == autoType) {
		return anyType;
	}
	if (t == autoArrayType) {
		return anyArrayType;
	}
	return t;
}

// Gets the "awaited type" of a type.
// @param type The type to await.
// @param withAlias When `true`, wraps the "awaited type" in `Awaited<T>` if needed.
// @remarks The "awaited type" of an expression is its "promised type" if the expression is a
// Promise-like type; otherwise, it is the type of the expression. This is used to reflect
// The runtime behavior of the `await` keyword.
// checker.go:31713
Type* Checker::checkAwaitedType(Type* t, bool withAlias, Node* errorNode,
								const DiagnosticMessage* diagnosticMessage) {
	Type* awaitedType;
	if (withAlias) {
		awaitedType = getAwaitedTypeEx(t, errorNode, diagnosticMessage);
	} else {
		awaitedType = getAwaitedTypeNoAliasEx(t, errorNode, diagnosticMessage);
	}
	if (awaitedType != nullptr) {
		return awaitedType;
	}
	return errorType;
}

// Gets the "awaited type" of a type.
//
// The "awaited type" of an expression is its "promised type" if the expression is a
// Promise-like type; otherwise, it is the type of the expression. If the "promised
// type" is itself a Promise-like, the "promised type" is recursively unwrapped until a
// non-promise type is found.
//
// This is used to reflect the runtime behavior of the `await` keyword.
// checker.go:31738
Type* Checker::getAwaitedType(Type* t) {
	return getAwaitedTypeEx(t, nullptr, nullptr);
}

// checker.go:31742 — getAwaitedTypeEx
Type* Checker::getAwaitedTypeEx(Type* t, Node* errorNode,
								const DiagnosticMessage* diagnosticMessage,
								const std::vector<std::string>& args) {
	Type* awaitedType =
		getAwaitedTypeNoAliasEx(t, errorNode, diagnosticMessage, args);
	if (awaitedType != nullptr) {
		return createAwaitedTypeIfNeeded(awaitedType);
	}
	return nullptr;
}

// Gets the "awaited type" of a type without introducing an `Awaited<T>` wrapper.
// checker.go:31750
Type* Checker::getAwaitedTypeNoAlias(Type* t) {
	return getAwaitedTypeNoAliasEx(t, nullptr, nullptr);
}

// checker.go:31754 — getAwaitedTypeNoAliasEx
Type* Checker::getAwaitedTypeNoAliasEx(
	Type* t, Node* errorNode, const DiagnosticMessage* diagnosticMessage,
	const std::vector<std::string>& args) {
	if (isTypeAny(t)) {
		return t;
	}
	// If this is already an `Awaited<T>`, just return it. This avoids `Awaited<Awaited<T>>` in higher-order
	if (isAwaitedTypeInstantiation(t)) {
		return t;
	}
	// If we've already cached an awaited type, return a possible `Awaited<T>` for it.
	CachedTypeKey key{CachedTypeKind::AwaitedType, t->id};
	Type* awaitedType = nullptr;
	if (auto it = cachedTypes.find(key); it != cachedTypes.end()) { awaitedType = it->second; }
	if (awaitedType != nullptr) {
		return awaitedType;
	}
	// For a union, get a union of the awaited types of each constituent.
	if (t->flags & TypeFlagsUnion) {
		if (containsElement(awaitedTypeStack, t)) {
			if (errorNode != nullptr) {
				error(errorNode,
					  Type_is_referenced_directly_or_indirectly_in_the_fulfillment_callback_of_its_own_then_method);
			}
			return nullptr;
		}
		awaitedTypeStack.push_back(t);
		Type* mapped = mapType(t, [this, errorNode, diagnosticMessage,
								   &args](Type* t) {
			return getAwaitedTypeNoAliasEx(t, errorNode, diagnosticMessage,
										   args);
		});
		awaitedTypeStack.pop_back();
		cachedTypes[key] = mapped;
		return mapped;
	}
	// If `type` is generic and should be wrapped in `Awaited<T>`, return it.
	if (isAwaitedTypeNeeded(t)) {
		cachedTypes[key] = t;
		return t;
	}
	Type* thisTypeForError = nullptr;
	Type* promisedType =
		getPromisedTypeOfPromiseEx(t, nullptr /*errorNode*/, &thisTypeForError);
	if (promisedType != nullptr) {
		if (t == promisedType || containsElement(awaitedTypeStack, promisedType)) {
			// Verify that we don't have a bad actor in the form of a promise whose
			// promised type is the same as the promise type, or a mutually recursive
			// promise. If so, we return undefined as we cannot guess the shape. If this
			// were the actual case in the JavaScript, this Promise would never resolve.
			//
			// An example of a bad actor with a singly-recursive promise type might
			// be:
			//
			//  interface BadPromise {
			//      then(
			//          onfulfilled: (value: BadPromise) => any,
			//          onrejected: (error: any) => any): BadPromise;
			//  }
			//
			// The above interface will pass the PromiseLike check, and return a
			// promised type of `BadPromise`. Since this is a self reference, we
			// don't want to keep recursing ad infinitum.
			//
			// An example of a bad actor in the form of a mutually-recursive
			// promise type might be:
			//
			//  interface BadPromiseA {
			//      then(
			//          onfulfilled: (value: BadPromiseB) => any,
			//          onrejected: (error: any) => any): BadPromiseB;
			//  }
			//
			//  interface BadPromiseB {
			//      then(
			//          onfulfilled: (value: BadPromiseA) => any,
			//          onrejected: (error: any) => any): BadPromiseA;
			//  }
			//
			if (errorNode != nullptr) {
				error(errorNode,
					  Type_is_referenced_directly_or_indirectly_in_the_fulfillment_callback_of_its_own_then_method);
			}
			return nullptr;
		}
		// Keep track of the type we're about to unwrap to avoid bad recursive promise types.
		// See the comments above for more information.
		awaitedTypeStack.push_back(t);
		Type* awaitedType = getAwaitedTypeNoAliasEx(promisedType, errorNode,
													diagnosticMessage, args);
		awaitedTypeStack.pop_back();
		if (awaitedType == nullptr) {
			return nullptr;
		}
		cachedTypes[key] = awaitedType;
		return awaitedType;
	}
	// The type was not a promise, so it could not be unwrapped any further.
	// As long as the type does not have a callable "then" property, it is
	// safe to return the type; otherwise, an error is reported and we return
	// undefined.
	//
	// An example of a non-promise "thenable" might be:
	//
	//  await { then(): void {} }
	//
	// The "thenable" does not match the minimal definition for a promise. When
	// a Promise/A+-compatible or ES6 promise tries to adopt this value, the promise
	// will never settle. We treat this as an error to help flag an early indicator
	// of a runtime problem. If the user wants to return this value from an async
	// function, they would need to wrap it in some other value. If they want it to
	// be treated as a promise, they can cast to <any>.
	if (isThenableType(t)) {
		if (errorNode != nullptr) {
			Diagnostic* diagnostic = nullptr;
			if (thisTypeForError != nullptr) {
				diagnostic = NewDiagnosticForNode(
					errorNode,
					The_this_context_of_type_0_is_not_assignable_to_method_s_this_of_type_1,
					{TypeToString(t), TypeToString(thisTypeForError)});
			}
			addDiagnostic(NewDiagnosticChainForNode(diagnostic, errorNode,
													diagnosticMessage, args));
		}
		return nullptr;
	}
	cachedTypes[key] = t;
	return t;
}

// checker.go:31869 — isAwaitedTypeInstantiation
bool Checker::isAwaitedTypeInstantiation(Type* t) {
	if (t->flags & TypeFlagsConditional) {
		Symbol* awaitedSymbol = getGlobalAwaitedSymbolOrNil();
		return awaitedSymbol != nullptr && t->alias != nullptr &&
			   t->alias->symbol == awaitedSymbol &&
			   t->alias->typeArguments.size() == 1;
	}
	return false;
}

// checker.go:31877 — isAwaitedTypeNeeded
bool Checker::isAwaitedTypeNeeded(Type* t) {
	// If this is already an `Awaited<T>`, we shouldn't wrap it. This helps to avoid `Awaited<Awaited<T>>` in higher-order.
	if (isTypeAny(t) || isAwaitedTypeInstantiation(t)) {
		return false;
	}
	// We only need `Awaited<T>` if `T` contains possibly non-primitive types.
	if (isGenericObjectType(t)) {
		Type* baseConstraint = getBaseConstraintOfType(t);
		// We only need `Awaited<T>` if `T` is a type variable that has no base constraint, or the base constraint of `T` is `any`, `unknown`, `{}`, `object`,
		// or is promise-like.
		if (baseConstraint != nullptr) {
			return (baseConstraint->flags & TypeFlagsAnyOrUnknown) != 0 ||
				   isEmptyObjectType(baseConstraint) ||
				   someType(baseConstraint,
							[this](Type* t) { return isThenableType(t); });
		}
		return maybeTypeOfKind(t, TypeFlagsTypeVariable);
	}
	return false;
}

// checker.go:31895 — createAwaitedTypeIfNeeded
Type* Checker::createAwaitedTypeIfNeeded(Type* t) {
	// We wrap type `T` in `Awaited<T>` based on the following conditions:
	// - `T` is not already an `Awaited<U>`, and
	// - `T` is generic, and
	// - One of the following applies:
	//   - `T` has no base constraint, or
	//   - The base constraint of `T` is `any`, `unknown`, `object`, or `{}`, or
	//   - The base constraint of `T` is an object type with a callable `then` method.
	if (isAwaitedTypeNeeded(t)) {
		Type* awaitedType = tryCreateAwaitedType(t);
		if (awaitedType != nullptr) {
			return awaitedType;
		}
	}
	return t;
}

// checker.go:31912 — tryCreateAwaitedType
Type* Checker::tryCreateAwaitedType(Type* t) {
	// Nothing to do if `Awaited<T>` doesn't exist
	Symbol* awaitedSymbol = getGlobalAwaitedSymbol();
	if (awaitedSymbol != nullptr) {
		// Unwrap unions that may contain `Awaited<T>`, otherwise its possible to manufacture an `Awaited<Awaited<T> | U>` where
		// an `Awaited<T | U>` would suffice.
		return getTypeAliasInstantiation(awaitedSymbol, {unwrapAwaitedType(t)},
										 nullptr);
	}
	return nullptr;
}

// For a generic `Awaited<T>`, gets `T`.
// checker.go:31924
Type* Checker::unwrapAwaitedType(Type* t) {
	if (t->flags & TypeFlagsUnion) {
		return mapType(t, [this](Type* u) { return unwrapAwaitedType(u); });
	}
	if (isAwaitedTypeInstantiation(t)) {
		return t->alias->typeArguments[0];
	}
	return t;
}

// checker.go:31934 — isThenableType
bool Checker::isThenableType(Type* t) {
	if (allTypesAssignableToKindEx(getBaseConstraintOrType(t),
								   TypeFlagsPrimitive | TypeFlagsNever,
								   false)) {
		// primitive types cannot be considered "thenable" since they are not objects.
		return false;
	}
	Type* thenFunction = getTypeOfPropertyOfType(t, "then");
	return thenFunction != nullptr &&
		   !getSignaturesOfType(
				getTypeWithFacts(thenFunction, TypeFactsNEUndefinedOrNull),
				SignatureKind::Call)
				.empty();
}

// checker.go:31943 — getAwaitedTypeOfPromise
Type* Checker::getAwaitedTypeOfPromise(Type* t) {
	return getAwaitedTypeOfPromiseEx(t, nullptr, nullptr);
}

// checker.go:31947 — getAwaitedTypeOfPromiseEx
Type* Checker::getAwaitedTypeOfPromiseEx(
	Type* t, Node* errorNode, const DiagnosticMessage* diagnosticMessage,
	const std::vector<std::string>& args) {
	Type* promisedType =
		getPromisedTypeOfPromiseEx(t, errorNode, nullptr);
	if (promisedType != nullptr) {
		return getAwaitedTypeEx(promisedType, errorNode, diagnosticMessage,
								args);
	}
	return nullptr;
}

// Check if a parameter or catch variable (or their bindings elements) is assigned anywhere
// checker.go:31956
bool Checker::isSomeSymbolAssigned(Node* rootDeclaration) {
	return isSomeSymbolAssignedWorker(rootDeclaration->name());
}

// checker.go:31960 — isSomeSymbolAssignedWorker
bool Checker::isSomeSymbolAssignedWorker(Node* node) {
	if (node->kind == Kind::Identifier) {
		return isSymbolAssigned(getSymbolOfDeclaration(node->parent));
	}
	return someRange(node->elements(), [this](Node* e) {
		return e->name() != nullptr && isSomeSymbolAssignedWorker(e->name());
	});
}

// checker.go:31969 — getTargetType
Type* Checker::getTargetType(Type* t) {
	if (t->objectFlags & ObjectFlagsReference) {
		return t->AsTypeReference()->target;
	}
	return t;
}

// checker.go:31976 — getNarrowableTypeForReference
Type* Checker::getNarrowableTypeForReference(Type* t, Node* reference,
											 CheckMode checkMode) {
	if (isNoInferType(t)) {
		t = t->AsSubstitutionType()->baseType;
	}
	// When the type of a reference is or contains an instantiable type with a union type constraint, and
	// when the reference is in a constraint position (where it is known we'll obtain the apparent type) or
	// has a contextual type containing no top-level instantiables (meaning constraints will determine
	// assignability), we substitute constraints for all instantiables in the type of the reference to give
	// control flow analysis an opportunity to narrow it further. For example, for a reference of a type
	// parameter type 'T extends string | undefined' with a contextual type 'string', we substitute
	// 'string | undefined' to give control flow analysis the opportunity to narrow to type 'string'.
	bool substituteConstraints =
		(checkMode & CheckModeInferential) == 0 &&
		someType(t, [this](Type* t) {
			return isGenericTypeWithUnionConstraint(t);
		}) &&
		(isConstraintPosition(t, reference) ||
		 hasContextualTypeWithNoGenericTypes(reference, checkMode));
	if (substituteConstraints) {
		return mapType(t, [this](Type* t) { return getBaseConstraintOrType(t); });
	}
	return t;
}

// checker.go:31994 — isConstraintPosition
bool Checker::isConstraintPosition(Type* t, Node* node) {
	Node* parent = node->parent;
	// In an element access obj[x], we consider obj to be in a constraint position, except when obj is of
	// a generic type without a nullable constraint and x is a generic type. This is because when both obj
	// and x are of generic types T and K, we want the resulting type to be T[K].
	return isPropertyAccessExpression(parent) || isQualifiedName(parent) ||
		   ((isCallExpression(parent) || isNewExpression(parent)) &&
			parent->expression() == node) ||
		   (isElementAccessExpression(parent) && parent->expression() == node &&
			!(someType(t, [this](Type* t) {
				return isGenericTypeWithoutNullableConstraint(t);
			}) &&
			  isGenericIndexType(getTypeOfExpression(
				  parent->as<ElementAccessExpression>()->ArgumentExpression))));
}

// checker.go:32004 — isGenericTypeWithUnionConstraint
bool Checker::isGenericTypeWithUnionConstraint(Type* t) {
	if (t->flags & TypeFlagsIntersection) {
		return someRange(t->AsIntersectionType()->types,
						 [this](Type* u) {
							 return isGenericTypeWithUnionConstraint(u);
						 });
	}
	return (t->flags & TypeFlagsInstantiable) != 0 &&
		   (getBaseConstraintOrType(t)->flags &
			(TypeFlagsNullable | TypeFlagsUnion)) != 0;
}

// checker.go:32011 — isGenericTypeWithoutNullableConstraint
bool Checker::isGenericTypeWithoutNullableConstraint(Type* t) {
	if (t->flags & TypeFlagsIntersection) {
		return someRange(t->AsIntersectionType()->types,
						 [this](Type* u) {
							 return isGenericTypeWithoutNullableConstraint(u);
						 });
	}
	return (t->flags & TypeFlagsInstantiable) != 0 &&
		   !maybeTypeOfKind(getBaseConstraintOrType(t), TypeFlagsNullable);
}

// checker.go:32018 — hasContextualTypeWithNoGenericTypes
bool Checker::hasContextualTypeWithNoGenericTypes(Node* node,
												  CheckMode checkMode) {
	// Computing the contextual type for a child of a JSX element involves resolving the type of the
	// element's tag name, so we exclude that here to avoid circularities.
	// If check mode has `CheckMode.RestBindingElement`, we skip binding pattern contextual types,
	// as we want the type of a rest element to be generic when possible.
	if ((isIdentifier(node) || isPropertyAccessExpression(node) ||
		 isElementAccessExpression(node)) &&
		!((isJsxOpeningElement(node->parent) ||
		   isJsxSelfClosingElement(node->parent)) &&
		  node->parent->tagName() == node)) {
		Type* contextualType = getContextualType(
			node,
			(checkMode & CheckModeRestBindingElement) != 0
				? ContextFlagsSkipBindingPatterns
				: ContextFlagsNone);
		if (contextualType != nullptr) {
			return !isGenericType(contextualType);
		}
	}
	return false;
}

// checker.go:32033 — getNonUndefinedType
Type* Checker::getNonUndefinedType(Type* t) {
	Type* typeOrConstraint = t;
	if (someType(t, [this](Type* t) {
			return isGenericTypeWithUndefinedConstraint(t);
		})) {
		typeOrConstraint = mapType(t, [this](Type* t) {
			if (t->flags & TypeFlagsInstantiable) {
				return getBaseConstraintOrType(t);
			}
			return t;
		});
	}
	return getTypeWithFacts(typeOrConstraint, TypeFactsNEUndefined);
}

// checker.go:32046 — isGenericTypeWithUndefinedConstraint
bool Checker::isGenericTypeWithUndefinedConstraint(Type* t) {
	if (t->flags & TypeFlagsInstantiable) {
		Type* constraint = getBaseConstraintOfType(t);
		if (constraint != nullptr) {
			return maybeTypeOfKind(constraint, TypeFlagsUndefined);
		}
	}
	return false;
}

// checker.go:32056 — getActualTypeVariable
Type* Checker::getActualTypeVariable(Type* t) {
	if (t->flags & TypeFlagsSubstitution) {
		return getActualTypeVariable(t->AsSubstitutionType()->baseType);
	}
	if (t->flags & TypeFlagsIndexedAccess) {
		Type* objectType =
			getActualTypeVariable(t->AsIndexedAccessType()->objectType);
		Type* indexType =
			getActualTypeVariable(t->AsIndexedAccessType()->indexType);
		if (objectType != t->AsIndexedAccessType()->objectType ||
			indexType != t->AsIndexedAccessType()->indexType) {
			return getIndexedAccessType(objectType, indexType);
		}
	}
	return getNonDistributedTypeParameter(t);
}

// === dep stubs — removed when owner slice lands ===

// (getResolvedSignature + getContextualSignature landed — expr_a slice
// defines them in checker_expressions_a.cpp.)

// (deduped: getTypeOfNode real def in checker_services.cpp)

// (deduped: getTypeFromBindingPattern defined in cpp/internal/checker/checker_decltypes.cpp)


// (deduped: checkDeclarationInitializer defined in cpp/internal/checker/checker_decltypes.cpp)


// (deduped: getIteratedTypeOrElementType, checkIteratedTypeOrElementType, getIterationTypesOfGeneratorFunctionReturnType defined in cpp/internal/checker/checker_declchecks2.cpp)

// (deduped: getKeyPropertyName defined in its owning slice file)
// (deduped: getConstituentTypeForKeyType defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: isDiscriminantProperty defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: discriminateTypeByDiscriminableItems defined in cpp/internal/checker/checker_relater.cpp)

// (deduped: isConstTypeVariable defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: getKeyPropertyName defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: getConstituentTypeForKeyType defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: isDiscriminantProperty defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: discriminateTypeByDiscriminableItems defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: isConstTypeVariable defined in cpp/internal/checker/checker_relater.cpp)

// (jsx-owned dep stubs moved to checker_jsx.cpp — real defs there)

} // namespace tsc::checker
