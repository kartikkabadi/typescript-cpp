// ---------------------------------------------------------------------------
// checker_inference.cpp — port of tsc/internal/checker/inference.go
//
// === slice: inference ===
//
// Type inference: InferenceState free-list pool, inferTypes/inferFromTypes and
// the recursive inferFrom*/inferTo* drivers, signature/property/index inference,
// reverse-mapped types, InferenceContext construction/cloning, and the
// getInferredType candidate-resolution tail.
//
// Conventions per PORTING.md: Go identifiers kept, `c.foo()` -> `foo()`,
// `*Type` -> `Type*`, `nil` -> `nullptr`. Declarations hoisted from stubs in
// checker_members.cpp, checker.cpp, checker_walk.cpp, checker_instantiate.cpp
// and checker_decltypes.cpp now have their real bodies here.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/checker/types.h"
#include "internal/jsnum/jsnum.h"
#include "internal/scanner/scanner.h"

namespace tsc {
namespace checker {

// ---------------------------------------------------------------------------
// Free helpers defined in other TUs (extern linkage, no header home).
// ---------------------------------------------------------------------------

bool someType(Type* t, const std::function<bool(Type*)>& f);               // checker.cpp
bool everyType(Type* t, const std::function<bool(Type*)>& f);              // checker.cpp
MappedTypeModifiers getMappedTypeModifiers(Type* t);                       // checker_contextual.cpp
bool isTupleType(Type* t);                                                 // checker_contextual.cpp
bool isObjectLiteralType(Type* t);                                         // checker_decltypes.cpp

// inference.go file-local helpers used before their definitions below.
static int compareTypesAndDepth(Type* t1, Type* t2);
static int getTypeDepth(Type* t, int maxDepth);
static int getTypeListDepth(const std::vector<Type*>& types, int maxDepth);
static Type* getSingleTypeVariableFromIntersectionTypes(
	InferenceState* n, const std::vector<Type*>& types);
static bool tupleTypesDefinitelyUnrelated(Type* source, Type* target);

// TupleElementInfo equality for sameMap (declared early: two-phase lookup).
namespace {

// typenodes — getEndElementCount: count of ending consecutive tuple elements of
// the given kind(s) (file-local replica; also file-local in checker_typenodes.cpp)
int getEndElementCount(TupleType* t, ElementFlags flags) {
	for (size_t i = t->elementInfos.size(); i > 0; i--) {
		if (!(t->elementInfos[i - 1].flags & flags)) {
			return static_cast<int>(t->elementInfos.size() - i);
		}
	}
	return static_cast<int>(t->elementInfos.size());
}
bool operator==(const TupleElementInfo& a, const TupleElementInfo& b) {
	return a.flags == b.flags && a.labeledDeclaration == b.labeledDeclaration;
}
}  // namespace

namespace {

// ---------------------------------------------------------------------------
// core.* helpers with no shared equivalent — local versions for this slice.
// ---------------------------------------------------------------------------

template <class T>
std::vector<T> concatenate(std::vector<T> a, const std::vector<T>& b) {
	a.insert(a.end(), b.begin(), b.end());
	return a;
}
template <class T>
std::vector<T> concatenate(std::vector<T> a, std::initializer_list<T> b) {
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
		if (!(y == x)) {
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

template <class T, class F>
bool someList(const std::vector<T>& v, F&& f) {
	for (const T& x : v) {
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

// core.Same — same length and pairwise equality.
template <class T>
bool sameVec(const std::vector<T>& a, const std::vector<T>& b) {
	return a == b;
}

// core.OrElse — first value if non-zero, otherwise the fallback.
template <class T>
T orElse(T a, T b) {
	if constexpr (std::is_pointer_v<T>) {
		return a != nullptr ? a : b;
	} else {
		return a ? a : b;
	}
}

// core.IfElse.
template <class T>
T ifElse(bool cond, T a, T b) {
	return cond ? a : b;
}

template <class T>
void appendIfUnique(std::vector<T>& v, T x) {
	if (std::find(v.begin(), v.end(), x) == v.end()) {
		v.push_back(x);
	}
}

// slices.Contains.
template <class T>
bool containsVec(const std::vector<T>& v, T x) {
	return std::find(v.begin(), v.end(), x) != v.end();
}

// ---------------------------------------------------------------------------
// File-local replicas (file-static in other TUs too — internal linkage, no clash).
// ---------------------------------------------------------------------------

// utilities.go:1033 — isObjectOrArrayLiteralType
bool isObjectOrArrayLiteralType(Type* t) {
	return (t->objectFlags & (ObjectFlagsObjectLiteral | ObjectFlagsArrayLiteral)) != 0;
}

// checker.go:25824 — getBooleanLiteralValue
bool getBooleanLiteralValue(Type* t) {
	return std::get<bool>(t->AsLiteralType()->value);
}

// utilities.go:286 — IsTypeAny
bool IsTypeAny(Type* t) {
	return t != nullptr && (t->flags & TypeFlagsAny) != 0;
}

// Literal value accessors (checker.cpp — file-local copies).
std::string getStringLiteralValue(Type* t) {
	return std::get<std::string>(t->AsLiteralType()->value);
}
Number getNumberLiteralValue(Type* t) {
	return std::get<Number>(t->AsLiteralType()->value);
}
PseudoBigInt getBigIntLiteralValue(Type* t) {
	return std::get<PseudoBigInt>(t->AsLiteralType()->value);
}

// utilities.go:716 — isUnitType
bool isUnitType(Type* t) {
	return (t->flags & TypeFlagsUnit) != 0;
}

// utilities.go:715 — isLiteralType
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

// utilities.go:970 — isValidNumberString
bool isValidNumberString(const std::string& s, bool roundTripOnly) {
	if (s.empty()) {
		return false;
	}
	Number n = numberFromString(s);
	return !n.isNaN() && !n.isInf() && (!roundTripOnly || n.string() == s);
}

// utilities.go:978 — isValidBigIntString
bool isValidBigIntString(const std::string& s, bool roundTripOnly) {
	if (s.empty()) {
		return false;
	}
	Scanner scanner;
	scanner.setSkipTrivia(false);
	bool success = true;
	scanner.setOnError([&success](const DiagnosticMessage*, int, int, const std::vector<std::string>&) {
		success = false;
	});
	scanner.setText(s + "n");
	Kind result = scanner.scan();
	bool negative = result == Kind::MinusToken;
	if (negative) {
		result = scanner.scan();
	}
	TokenFlags flags = scanner.tokenFlags();
	// validate that
	// * scanning proceeded without error
	// * a bigint can be scanned, and that when it is scanned, it is
	// * the full length of the input string (so the scanner is one character beyond the augmented input length)
	// * it does not contain a numeric separator (the `BigInt` constructor does not accept a numeric separator in its input)
	return success && result == Kind::BigIntLiteral &&
		scanner.tokenEnd() == static_cast<int>(s.size()) + 1 &&
		!(flags & TokenFlagsContainsSeparator) &&
		(!roundTripOnly ||
		 s == PseudoBigInt::create(parsePseudoBigInt(scanner.tokenValue()), negative).string());
}

// utilities.go:1228 — pseudoBigIntToString
std::string pseudoBigIntToString(const PseudoBigInt& value) {
	return value.string();
}

// inference.go:1651 — hasInferenceCandidates
bool hasInferenceCandidates(InferenceInfo* info) {
	return !info->candidates.empty() || !info->contraCandidates.empty();
}

// inference.go:1659 — hasTypeParameterDefault
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

// inference.go:1655 — hasInferenceCandidatesOrDefault
[[maybe_unused]] bool hasInferenceCandidatesOrDefault(InferenceInfo* info) {
	return hasInferenceCandidates(info) || hasTypeParameterDefault(info->typeParameter);
}

// mapper.go:256 — newMergedTypeMapper
TypeMapper* newMergedTypeMapper(TypeMapper* m1, TypeMapper* m2) {
	auto* m = new MergedTypeMapper();
	m->m1 = m1;
	m->m2 = m2;
	return m;
}

// Type comparison helpers (CompareTypes/compareTypeLists/compareTypeMappers/
// getTypeNameSymbol/getSortOrderFlags/compareTypeNames/compareTupleTypes) live in
// checker_utilities.cpp — declared in checker.h, no local replicas needed.

}  // namespace

// ---------------------------------------------------------------------------
// inference.go free functions (file-local helpers)
// ---------------------------------------------------------------------------

// getInferenceInfoForType — inference.go:1519
static InferenceInfo* getInferenceInfoForType(InferenceState* n, Type* t) {
	if (t->flags & TypeFlagsTypeVariable) {
		t = getNonDistributedTypeParameter(t);
		for (InferenceInfo* inference : n->inferences) {
			if (t == inference->typeParameter) {
				return inference;
			}
		}
	}
	return nullptr;
}

// newInferenceInfo — inference.go:1626
static InferenceInfo* newInferenceInfo(Type* typeParameter) {
	auto* info = new InferenceInfo();
	info->typeParameter = typeParameter;
	info->priority = InferencePriorityMaxValue;
	info->topLevel = true;
	info->impliedArity = -1;
	return info;
}

// cloneInferenceInfo — inference.go:1630
static InferenceInfo* cloneInferenceInfo(InferenceInfo* info) {
	auto* clone = new InferenceInfo();
	clone->typeParameter = info->typeParameter;
	clone->candidates = info->candidates;
	clone->contraCandidates = info->contraCandidates;
	clone->inferredType = info->inferredType;
	clone->priority = info->priority;
	clone->topLevel = info->topLevel;
	clone->isFixed = info->isFixed;
	clone->impliedArity = info->impliedArity;
	return clone;
}

// ---------------------------------------------------------------------------
// InferenceState pool — inference.go:32-51
// ---------------------------------------------------------------------------

InferenceState* Checker::getInferenceState() {
	InferenceState* n = freeinferenceState;
	if (n == nullptr) {
		n = new InferenceState();
	}
	freeinferenceState = n->next;
	return n;
}

void Checker::putInferenceState(InferenceState* n) {
	// Go: clear(n.visited); *n = InferenceState{inferences: n.inferences[:0],
	// visited: n.visited, sourceStack: n.sourceStack[:0], targetStack:
	// n.targetStack[:0], next: c.freeinferenceState} — backing stores are kept.
	auto inferences = std::move(n->inferences);
	auto visited = std::move(n->visited);
	auto sourceStack = std::move(n->sourceStack);
	auto targetStack = std::move(n->targetStack);
	inferences.clear();
	visited.clear();
	sourceStack.clear();
	targetStack.clear();
	*n = InferenceState{};
	n->inferences = std::move(inferences);
	n->visited = std::move(visited);
	n->sourceStack = std::move(sourceStack);
	n->targetStack = std::move(targetStack);
	n->next = freeinferenceState;
	freeinferenceState = n;
}

// ---------------------------------------------------------------------------
// inferTypes / inferFromTypes — inference.go:53-282
// ---------------------------------------------------------------------------

void Checker::inferTypes(std::vector<InferenceInfo*>& inferences,
						 Type* originalSource, Type* originalTarget,
						 InferencePriority priority, bool contravariant) {
	InferenceState* n = getInferenceState();
	n->inferences = inferences;
	n->originalSource = originalSource;
	n->originalTarget = originalTarget;
	n->priority = priority;
	n->inferencePriority = InferencePriorityMaxValue;
	n->contravariant = contravariant;
	inferFromTypes(n, originalSource, originalTarget);
	putInferenceState(n);
}

void Checker::inferFromTypes(InferenceState* n, Type* source, Type* target) {
	if (!couldContainTypeVariables(target) || isNoInferType(target)) {
		return;
	}
	if (source == wildcardType || source == blockedStringType) {
		// We are inferring from an 'any' type. We want to infer this type for every type parameter
		// referenced in the target type, so we record it as the propagation type and infer from the
		// target to itself. Then, as we find candidates we substitute the propagation type.
		Type* savePropagationType = n->propagationType;
		n->propagationType = source;
		inferFromTypes(n, target, target);
		n->propagationType = savePropagationType;
		return;
	}
	if (source->alias != nullptr && target->alias != nullptr &&
		source->alias->symbol == target->alias->symbol) {
		if (!source->alias->typeArguments.empty() || !target->alias->typeArguments.empty()) {
			// Source and target are types originating in the same generic type alias declaration.
			// Simply infer from source type arguments to target type arguments, with defaults applied.
			invokeOnce(n, source, target, &Checker::inferFromAliasTypeArguments);
		}
		// And if there weren't any type arguments, there's no reason to run inference as the types must be the same.
		return;
	}
	if (source == target && (source->flags & TypeFlagsUnionOrIntersection) != 0) {
		// When source and target are the same union or intersection type, just relate each constituent
		// type to itself.
		for (Type* t : source->types()) {
			inferFromTypes(n, t, t);
		}
		return;
	}
	if (target->flags & TypeFlagsUnion) {
		std::vector<Type*> sourceTypes;
		if (source->flags & TypeFlagsUnion) {
			sourceTypes = source->types();
		} else {
			sourceTypes = {source};
		}
		// First, infer between identically matching source and target constituents and remove the
		// matching types.
		auto temp = inferFromMatchingTypes(n, sourceTypes, target->types(),
										   &Checker::isTypeOrBaseIdenticalTo, false /*sort*/);
		// Next, infer between closely matching source and target constituents and remove
		// the matching types. Types closely match when they are instantiations of the same
		// object type or instantiations of the same type alias.
		auto rest = inferFromMatchingTypes(n, temp.first, temp.second,
										   &Checker::isTypeCloselyMatchedBy, true /*sort*/);
		std::vector<Type*>& sources = rest.first;
		std::vector<Type*>& targets = rest.second;
		if (targets.empty()) {
			return;
		}
		target = getUnionType(targets);
		if (sources.empty()) {
			// All source constituents have been matched and there is nothing further to infer from.
			// However, simply making no inferences is undesirable because it could ultimately mean
			// inferring a type parameter constraint. Instead, make a lower priority inference from
			// the full source to whatever remains in the target. For example, when inferring from
			// string to 'string | T', make a lower priority inference of string for T.
			inferWithPriority(n, source, target, InferencePriorityNakedTypeVariable);
			return;
		}
		source = getUnionType(sources);
	} else if ((target->flags & TypeFlagsIntersection) != 0 &&
			   !everyList(target->types(),
						  [this](Type* t) { return isNonGenericObjectType(t); })) {
		// We reduce intersection types unless they're simple combinations of object types. For example,
		// when inferring from 'string[] & { extra: any }' to 'string[] & T' we want to remove string[] and
		// infer { extra: any } for T. But when inferring to 'string[] & Iterable<T>' we want to keep the
		// string[] on the source side and infer string for T.
		if ((source->flags & TypeFlagsUnion) == 0) {
			std::vector<Type*> sourceTypes;
			if (source->flags & TypeFlagsIntersection) {
				sourceTypes = source->types();
			} else {
				sourceTypes = {source};
			}
			// Infer between identically matching source and target constituents and remove the matching types.
			auto rest = inferFromMatchingTypes(n, sourceTypes, target->types(),
											   &Checker::isTypeIdenticalTo, false /*sort*/);
			std::vector<Type*>& sources = rest.first;
			std::vector<Type*>& targets = rest.second;
			if (sources.empty() || targets.empty()) {
				return;
			}
			source = getIntersectionType(sources);
			target = getIntersectionType(targets);
		}
	}
	if ((target->flags & (TypeFlagsIndexedAccess | TypeFlagsSubstitution)) != 0) {
		if (isNoInferType(target)) {
			return;
		}
		target = getActualTypeVariable(target);
	}
	if (target->flags & TypeFlagsTypeVariable) {
		// Skip inference if the source is "blocked", which is used by the language service to
		// prevent inference on nodes currently being edited.
		if (isFromInferenceBlockedSource(source)) {
			return;
		}
		InferenceInfo* inference = getInferenceInfoForType(n, target);
		if (inference != nullptr) {
			// If target is a type parameter, make an inference, unless the source type contains
			// a "non-inferrable" type. Types with this flag set are markers used to prevent inference.
			//
			// For example:
			//     - anyFunctionType is a wildcard type that's used to avoid contextually typing functions;
			//       it's internal, so should not be exposed to the user by adding it as a candidate.
			//     - autoType (and autoArrayType) is a special "any" used in control flow; like anyFunctionType,
			//       it's internal and should not be observable.
			//     - silentNeverType is returned by getInferredType when instantiating a generic function for
			//       inference (and a type variable has no mapping).
			//
			// This flag is infectious; if we produce Box<never> (where never is silentNeverType), Box<never> is
			// also non-inferrable.
			//
			// As a special case, also ignore nonInferrableAnyType, which is a special form of the any type
			// used as a stand-in for binding elements when they are being inferred.
			if ((source->objectFlags & ObjectFlagsNonInferrableType) != 0 ||
				source == nonInferrableAnyType) {
				return;
			}
			if (!inference->isFixed) {
				Type* candidate = orElse(n->propagationType, source);
				if (candidate == blockedStringType) {
					return;
				}
				if (n->priority < inference->priority) {
					inference->candidates.clear();
					inference->contraCandidates.clear();
					inference->topLevel = true;
					inference->priority = n->priority;
				}
				if (n->priority == inference->priority) {
					// We make contravariant inferences only if we are in a pure contravariant position,
					// i.e. only if we have not descended into a bivariant position.
					if (n->contravariant && !n->bivariant) {
						if (!containsVec(inference->contraCandidates, candidate)) {
							inference->contraCandidates.push_back(candidate);
							clearCachedInferences(n->inferences);
						}
					} else if (!containsVec(inference->candidates, candidate)) {
						inference->candidates.push_back(candidate);
						clearCachedInferences(n->inferences);
					}
				}
				if ((n->priority & InferencePriorityReturnType) == 0 &&
					(target->flags & TypeFlagsTypeParameter) != 0 && inference->topLevel &&
					!isTypeParameterAtTopLevel(n->originalTarget, target, 0)) {
					inference->topLevel = false;
					clearCachedInferences(n->inferences);
				}
			}
			n->inferencePriority = std::min(n->inferencePriority, n->priority);
			return;
		}
		// Infer to the simplified version of an indexed access, if possible, to (hopefully) expose more bare type parameters to the inference engine
		Type* simplified = getSimplifiedType(target, false /*writing*/);
		if (simplified != target) {
			inferFromTypes(n, source, simplified);
		} else if (target->flags & TypeFlagsIndexedAccess) {
			Type* indexType = getSimplifiedType(target->AsIndexedAccessType()->indexType,
												false /*writing*/);
			// Generally simplifications of instantiable indexes are avoided to keep relationship checking correct, however if our target is an access, we can consider
			// that key of that access to be "instantiated", since we're looking to find the infernce goal in any way we can.
			if (indexType->flags & TypeFlagsInstantiable) {
				simplified = distributeIndexOverObjectType(
					getSimplifiedType(target->AsIndexedAccessType()->objectType,
									  false /*writing*/),
					indexType, false /*writing*/);
				if (simplified != nullptr && simplified != target) {
					inferFromTypes(n, source, simplified);
				}
			}
		}
	}
	// Go `switch {}` — an ordered if/else-if chain.
	if ((source->objectFlags & ObjectFlagsReference) != 0 &&
		(target->objectFlags & ObjectFlagsReference) != 0 &&
		(source->AsTypeReference()->target == target->AsTypeReference()->target ||
		 (isArrayType(source) && isArrayType(target))) &&
		!(source->AsTypeReference()->node != nullptr &&
		  target->AsTypeReference()->node != nullptr)) {
		// If source and target are references to the same generic type, infer from type arguments
		invokeOnce(n, source, target, &Checker::inferFromReferenceTypeArguments);
	} else if ((source->flags & TypeFlagsIndex) != 0 &&
			   (target->flags & TypeFlagsIndex) != 0) {
		inferFromContravariantTypes(n, source->AsIndexType()->target,
									target->AsIndexType()->target);
	} else if ((isLiteralType(source) || (source->flags & TypeFlagsString) != 0) &&
			   (target->flags & TypeFlagsIndex) != 0) {
		Type* empty = createEmptyObjectTypeFromStringLiteral(source);
		inferFromContravariantTypesWithPriority(n, empty, target->AsIndexType()->target,
												InferencePriorityLiteralKeyof);
	} else if ((source->flags & TypeFlagsIndexedAccess) != 0 &&
			   (target->flags & TypeFlagsIndexedAccess) != 0) {
		inferFromTypes(n, source->AsIndexedAccessType()->objectType,
					   target->AsIndexedAccessType()->objectType);
		inferFromTypes(n, source->AsIndexedAccessType()->indexType,
					   target->AsIndexedAccessType()->indexType);
	} else if ((source->flags & TypeFlagsStringMapping) != 0 &&
			   (target->flags & TypeFlagsStringMapping) != 0) {
		if (source->symbol == target->symbol) {
			inferFromTypes(n, source->AsStringMappingType()->target,
						   target->AsStringMappingType()->target);
		}
	} else if (source->flags & TypeFlagsSubstitution) {
		inferFromTypes(n, source->AsSubstitutionType()->baseType, target);
		// Make substitute inference at a lower priority
		inferWithPriority(n, getSubstitutionIntersection(source), target,
						  InferencePrioritySubstituteSource);
	} else if (target->flags & TypeFlagsConditional) {
		invokeOnce(n, source, target, &Checker::inferToConditionalType);
	} else if (target->flags & TypeFlagsUnionOrIntersection) {
		inferToMultipleTypes(n, source, target->types(), target->flags);
	} else if (source->flags & TypeFlagsUnion) {
		// Source is a union or intersection type, infer from each constituent type
		for (Type* sourceType : source->types()) {
			inferFromTypes(n, sourceType, target);
		}
	} else if (target->flags & TypeFlagsTemplateLiteral) {
		inferToTemplateLiteralType(n, source, target->AsTemplateLiteralType());
	} else {
		source = getReducedType(source);
		if (isGenericMappedType(source) && isGenericMappedType(target)) {
			invokeOnce(n, source, target, &Checker::inferFromGenericMappedTypes);
		}
		if (!((n->priority & InferencePriorityNoConstraints) != 0 &&
			  (source->flags & (TypeFlagsIntersection | TypeFlagsInstantiable)) != 0)) {
			Type* apparentSource = getApparentType(source);
			// getApparentType can return _any_ type, since an indexed access or conditional may simplify to any other type.
			// If that occurs and it doesn't simplify to an object or intersection, we'll need to restart `inferFromTypes`
			// with the simplified source.
			if (apparentSource != source &&
				(apparentSource->flags & (TypeFlagsObject | TypeFlagsIntersection)) == 0) {
				inferFromTypes(n, apparentSource, target);
				return;
			}
			source = apparentSource;
		}
		if ((source->flags & (TypeFlagsObject | TypeFlagsIntersection)) != 0) {
			invokeOnce(n, source, target, &Checker::inferFromObjectTypes);
		}
	}
}

// inferFromAliasTypeArguments — inference.go:280
void Checker::inferFromAliasTypeArguments(InferenceState* n, Type* source,
										Type* target) {
	// Source and target are types originating in the same generic type alias declaration.
	// Simply infer from source type arguments to target type arguments, with defaults applied.
	std::vector<Type*> params =
		typeAliasLinks.Get(source->alias->symbol)->typeParameters;
	int minParams = getMinTypeArgumentCount(params);
	bool nodeIsInJsFile = isInJSFile(source->alias->symbol->valueDeclaration);
	std::vector<Type*> sourceTypes = fillMissingTypeArguments(
		source->alias->typeArguments, params, minParams, nodeIsInJsFile);
	std::vector<Type*> targetTypes = fillMissingTypeArguments(
		target->alias->typeArguments, params, minParams, nodeIsInJsFile);
	inferFromTypeArguments(n, sourceTypes, targetTypes,
						   getAliasVariances(source->alias->symbol));
}

// inferFromReferenceTypeArguments — inference.go:293
void Checker::inferFromReferenceTypeArguments(InferenceState* n, Type* source,
											  Type* target) {
	inferFromTypeArguments(n, getTypeArguments(source), getTypeArguments(target),
						   getVariances(source->AsTypeReference()->target));
}

// inferFromTypeArguments — inference.go:297
void Checker::inferFromTypeArguments(InferenceState* n,
									 const std::vector<Type*>& sourceTypes,
									 const std::vector<Type*>& targetTypes,
									 const std::vector<VarianceFlags>& variances) {
	size_t count = std::min(sourceTypes.size(), targetTypes.size());
	for (size_t i = 0; i < count; i++) {
		if (i < variances.size() &&
			(variances[i] & VarianceFlagsVarianceMask) == VarianceFlagsContravariant) {
			inferFromContravariantTypes(n, sourceTypes[i], targetTypes[i]);
		} else {
			inferFromTypes(n, sourceTypes[i], targetTypes[i]);
		}
	}
}

// inferWithPriority — inference.go:294
void Checker::inferWithPriority(InferenceState* n, Type* source, Type* target,
								InferencePriority newPriority) {
	InferencePriority savePriority = n->priority;
	n->priority |= newPriority;
	inferFromTypes(n, source, target);
	n->priority = savePriority;
}

// inferFromContravariantTypesWithPriority — inference.go:301
void Checker::inferFromContravariantTypesWithPriority(InferenceState* n, Type* source,
													  Type* target,
													  InferencePriority newPriority) {
	InferencePriority savePriority = n->priority;
	n->priority |= newPriority;
	inferFromContravariantTypes(n, source, target);
	n->priority = savePriority;
}

// inferFromContravariantTypes — inference.go:308
void Checker::inferFromContravariantTypes(InferenceState* n, Type* source, Type* target) {
	n->contravariant = !n->contravariant;
	inferFromTypes(n, source, target);
	n->contravariant = !n->contravariant;
}

// inferFromContravariantTypesIfStrictFunctionTypes — inference.go:314
void Checker::inferFromContravariantTypesIfStrictFunctionTypes(InferenceState* n,
															   Type* source, Type* target) {
	if (strictFunctionTypes || (n->priority & InferencePriorityAlwaysStrict) != 0) {
		inferFromContravariantTypes(n, source, target);
	} else {
		inferFromTypes(n, source, target);
	}
}

// Ensure an inference action is performed only once for the given source and target types.
// This includes two things:
// Avoiding inferring between the same pair of source and target types,
// and avoiding circularly inferring between source and target types.
// For an example of the last, consider if we are inferring between source type
// `type Deep<T> = { next: Deep<Deep<T>> }` and target type `type Loop<U> = { next: Loop<U> }`.
// We would then infer between the types of the `next` property: `Deep<Deep<T>>` = `{ next: Deep<Deep<Deep<T>>> }` and `Loop<U>` = `{ next: Loop<U> }`.
// We will then infer again between the types of the `next` property:
// `Deep<Deep<Deep<T>>>` and `Loop<U>`, and so on, such that we would be forever inferring
// between instantiations of the same types `Deep` and `Loop`.
// In particular, we would be inferring from increasingly deep instantiations of `Deep` to `Loop`,
// such that we would go on inferring forever, even though we would never infer
// between the same pair of types.
//
// invokeOnce — inference.go:335
void Checker::invokeOnce(InferenceState* n, Type* source, Type* target,
						 void (Checker::*action)(InferenceState*, Type*, Type*)) {
	InferenceKey key{source->id, target->id, n->priority, n->contravariant,
					 n->bivariant};
	if (auto it = n->visited.find(key); it != n->visited.end()) {
		n->inferencePriority = std::min(n->inferencePriority, it->second);
		return;
	}
	n->visited[key] = InferencePriorityCircularity;
	InferencePriority saveInferencePriority = n->inferencePriority;
	n->inferencePriority = InferencePriorityMaxValue;
	// We stop inferring and report a circularity if we encounter duplicate recursion identities on both
	// the source side and the target side.
	ExpandingFlags saveExpandingFlags = n->expandingFlags;
	n->sourceStack.push_back(source);
	n->targetStack.push_back(target);
	if (isDeeplyNestedType(source, n->sourceStack, 2)) {
		n->expandingFlags |= ExpandingFlagsSource;
	}
	if (isDeeplyNestedType(target, n->targetStack, 2)) {
		n->expandingFlags |= ExpandingFlagsTarget;
	}
	if (n->expandingFlags != ExpandingFlagsBoth) {
		(this->*action)(n, source, target);
	} else {
		n->inferencePriority = InferencePriorityCircularity;
	}
	n->targetStack.pop_back();
	n->sourceStack.pop_back();
	n->expandingFlags = saveExpandingFlags;
	n->visited[key] = n->inferencePriority;
	n->inferencePriority = std::min(n->inferencePriority, saveInferencePriority);
}

// inferFromMatchingTypes — inference.go:370
std::pair<std::vector<Type*>, std::vector<Type*>> Checker::inferFromMatchingTypes(
	InferenceState* n, std::vector<Type*> sources, std::vector<Type*> targets,
	bool (Checker::*matches)(Type*, Type*), bool sort) {
	std::vector<Type*> matchedSources;
	std::vector<Type*> matchedTargets;
	for (Type* t : targets) {
		for (Type* s : sources) {
			if ((this->*matches)(s, t)) {
				if (!sort) {
					inferFromTypes(n, s, t);
				}
				appendIfUnique(matchedSources, s);
				appendIfUnique(matchedTargets, t);
			}
		}
	}
	if (sort) {
		// Sort target types by decreasing depth of generic instantiations. Intuitively, a successful
		// inference from a type argument with deeper nesting is of higher quality because we've stripped
		// away more layers of type instantiations that otherwise might skew the results. For example,
		// when inferring from string[] | string[][] to T[] | T[][], the inference of string we make from
		// relating string[][] to T[][] is of higher quality than the inference of string[] we make relating
		// string[][] to T[].
		std::sort(matchedTargets.begin(), matchedTargets.end(),
				  [](Type* a, Type* b) { return compareTypesAndDepth(a, b) < 0; });
		for (Type* t : matchedTargets) {
			for (Type* s : matchedSources) {
				if ((this->*matches)(s, t)) {
					inferFromTypes(n, s, t);
				}
			}
		}
	}
	if (!matchedSources.empty()) {
		sources = filterVec(sources, [&matchedSources](Type* t) {
			return !containsVec(matchedSources, t);
		});
	}
	if (!matchedTargets.empty()) {
		targets = filterVec(targets, [&matchedTargets](Type* t) {
			return !containsVec(matchedTargets, t);
		});
	}
	return {sources, targets};
}

// Compare two types first by depth and then by the regular type ordering.
// compareTypesAndDepth — inference.go:410
static int compareTypesAndDepth(Type* t1, Type* t2) {
	int d1 = getTypeDepth(t1, 3);
	int d2 = getTypeDepth(t2, 3);
	if (d1 != d2) {
		return d2 - d1; // Largest depth sorts first
	}
	return CompareTypes(t1, t2);
}

// Return the depth of the given type up to the given maximum depth. For generic aliased types
// and type references, the depth is one plus the largest type argument depth. For union and
// intersection types, the depth is the largest constituent type depth. For all other types,
// the depth is zero. The maximum depth limits infinite recursion of circular types.
//
// getTypeDepth — inference.go:423
static int getTypeDepth(Type* t, int maxDepth) {
	if (maxDepth != 0) {
		if (t->alias != nullptr && !t->alias->typeArguments.empty()) {
			return getTypeListDepth(t->alias->typeArguments, maxDepth - 1) + 1;
		}
		if (t->objectFlags & ObjectFlagsReference) {
			std::vector<Type*> typeArguments = t->checker->getTypeArguments(t);
			if (!typeArguments.empty()) {
				return getTypeListDepth(typeArguments, maxDepth - 1) + 1;
			}
		}
		if (t->flags & TypeFlagsUnionOrIntersection) {
			return getTypeListDepth(t->types(), maxDepth);
		}
	}
	return 0;
}

// getTypeListDepth — inference.go:440
static int getTypeListDepth(const std::vector<Type*>& types, int maxDepth) {
	int depth = 0;
	for (Type* t : types) {
		depth = std::max(depth, getTypeDepth(t, maxDepth));
	}
	return depth;
}

// inferToMultipleTypes — inference.go:448
void Checker::inferToMultipleTypes(InferenceState* n, Type* source,
								   const std::vector<Type*>& targets,
								   TypeFlags targetFlags) {
	int typeVariableCount = 0;
	if (targetFlags & TypeFlagsUnion) {
		Type* nakedTypeVariable = nullptr;
		std::vector<Type*> sources;
		if (source->flags & TypeFlagsUnion) {
			sources = source->types();
		} else {
			sources = {source};
		}
		std::vector<bool> matched(sources.size(), false);
		bool inferenceCircularity = false;
		// First infer to types that are not naked type variables. For each source type we
		// track whether inferences were made from that particular type to some target with
		// equal priority (i.e. of equal quality) to what we would infer for a naked type
		// parameter.
		for (Type* t : targets) {
			if (getInferenceInfoForType(n, t) != nullptr) {
				nakedTypeVariable = t;
				typeVariableCount++;
			} else {
				for (size_t i = 0; i < sources.size(); i++) {
					InferencePriority saveInferencePriority = n->inferencePriority;
					n->inferencePriority = InferencePriorityMaxValue;
					inferFromTypes(n, sources[i], t);
					if (n->inferencePriority == n->priority) {
						matched[i] = true;
					}
					inferenceCircularity =
						inferenceCircularity ||
						n->inferencePriority == InferencePriorityCircularity;
					n->inferencePriority =
						std::min(n->inferencePriority, saveInferencePriority);
				}
			}
		}
		if (typeVariableCount == 0) {
			// If every target is an intersection of types containing a single naked type variable,
			// make a lower priority inference to that type variable. This handles inferring from
			// 'A | B' to 'T & (X | Y)' where we want to infer 'A | B' for T.
			Type* intersectionTypeVariable =
				getSingleTypeVariableFromIntersectionTypes(n, targets);
			if (intersectionTypeVariable != nullptr) {
				inferWithPriority(n, source, intersectionTypeVariable,
								  InferencePriorityNakedTypeVariable);
			}
			return;
		}
		// If the target has a single naked type variable and no inference circularities were
		// encountered above (meaning we explored the types fully), create a union of the source
		// types from which no inferences have been made so far and infer from that union to the
		// naked type variable.
		if (typeVariableCount == 1 && !inferenceCircularity) {
			std::vector<Type*> unmatched;
			for (size_t i = 0; i < sources.size(); i++) {
				if (!matched[i]) {
					unmatched.push_back(sources[i]);
				}
			}
			if (!unmatched.empty()) {
				inferFromTypes(n, getUnionType(unmatched), nakedTypeVariable);
				return;
			}
		}
	} else {
		// We infer from types that are not naked type variables first so that inferences we
		// make from nested naked type variables and given slightly higher priority by virtue
		// of being first in the candidates array.
		for (Type* t : targets) {
			if (getInferenceInfoForType(n, t) != nullptr) {
				typeVariableCount++;
			} else {
				inferFromTypes(n, source, t);
			}
		}
	}
	// Inferences directly to naked type variables are given lower priority as they are
	// less specific. For example, when inferring from Promise<string> to T | Promise<T>,
	// we want to infer string for T, not Promise<string> | string. For intersection types
	// we only infer to single naked type variables.
	if (((targetFlags & TypeFlagsIntersection) != 0 && typeVariableCount == 1) ||
		((targetFlags & TypeFlagsIntersection) == 0 && typeVariableCount > 0)) {
		for (Type* t : targets) {
			if (getInferenceInfoForType(n, t) != nullptr) {
				inferWithPriority(n, source, t, InferencePriorityNakedTypeVariable);
			}
		}
	}
}

// getSingleTypeVariableFromIntersectionTypes — inference.go:532
static Type* getSingleTypeVariableFromIntersectionTypes(
	InferenceState* n, const std::vector<Type*>& types) {
	Type* typeVariable = nullptr;
	for (Type* t : types) {
		if ((t->flags & TypeFlagsIntersection) == 0) {
			return nullptr;
		}
		Type* v = findOrNull(t->types(), [n](Type* t) {
			return getInferenceInfoForType(n, t) != nullptr;
		});
		if (v == nullptr || (typeVariable != nullptr && v != typeVariable)) {
			return nullptr;
		}
		typeVariable = v;
	}
	return typeVariable;
}

// inferToMultipleTypesWithPriority — inference.go:547
void Checker::inferToMultipleTypesWithPriority(InferenceState* n, Type* source,
											   const std::vector<Type*>& targets,
											   TypeFlags targetFlags,
											   InferencePriority newPriority) {
	InferencePriority savePriority = n->priority;
	n->priority |= newPriority;
	inferToMultipleTypes(n, source, targets, targetFlags);
	n->priority = savePriority;
}

// inferToConditionalType — inference.go:554
void Checker::inferToConditionalType(InferenceState* n, Type* source, Type* target) {
	if (source->flags & TypeFlagsConditional) {
		inferFromTypes(n,
					   getNonDistributedTypeParameter(source->AsConditionalType()->checkType),
					   target->AsConditionalType()->checkType);
		inferFromTypes(n,
					   getNonDistributedTypeParameter(
						   source->AsConditionalType()->extendsType),
					   target->AsConditionalType()->extendsType);
		inferFromTypes(n,
					   getNonDistributedTypeParameter(getTrueTypeFromConditionalType(source)),
					   getTrueTypeFromConditionalType(target));
		inferFromTypes(n,
					   getNonDistributedTypeParameter(getFalseTypeFromConditionalType(source)),
					   getFalseTypeFromConditionalType(target));
	} else {
		std::vector<Type*> targetTypes = {getTrueTypeFromConditionalType(target),
										  getFalseTypeFromConditionalType(target)};
		inferToMultipleTypesWithPriority(
			n, source, targetTypes, target->flags,
			ifElse(n->contravariant, InferencePriorityContravariantConditional,
				   InferencePriorityNone));
	}
}

// inferToTemplateLiteralType — inference.go:566
void Checker::inferToTemplateLiteralType(InferenceState* n, Type* source,
										 TemplateLiteralType* target) {
	std::vector<Type*> matches =
		inferTypesFromTemplateLiteralType(source, target, compareTypesAssignable);
	const std::vector<Type*>& types = target->types;
	// When the target template literal contains only placeholders (meaning that inference is intended to extract
	// single characters and remainder strings) and inference fails to produce matches, we want to infer 'never' for
	// each placeholder such that instantiation with the inferred value(s) produces 'never', a type for which an
	// assignment check will fail. If we make no inferences, we'll likely end up with the constraint 'string' which,
	// upon instantiation, would collapse all the placeholders to just 'string', and an assignment check might
	// succeed. That would be a pointless and confusing outcome.
	if (!matches.empty() || everyList(target->texts, [](const std::string& s) {
			return s.empty();
		})) {
		for (size_t i = 0; i < types.size(); i++) {
			Type* target = types[i];
			Type* source;
			if (!matches.empty()) {
				source = matches[i];
			} else {
				source = neverType;
			}
			// If we are inferring from a string literal type to a type variable whose constraint includes one of the
			// allowed template literal placeholder types, infer from a literal type corresponding to the constraint.
			if ((source->flags & TypeFlagsStringLiteral) != 0 &&
				(target->flags & TypeFlagsTypeVariable) != 0) {
				if (InferenceInfo* inferenceContext =
						getInferenceInfoForType(n, target);
					inferenceContext != nullptr) {
					if (Type* constraint =
							getBaseConstraintOfType(inferenceContext->typeParameter);
						constraint != nullptr && !IsTypeAny(constraint)) {
						TypeFlags allTypeFlags = TypeFlagsNone;
						for (Type* t : constraint->Distributed()) {
							allTypeFlags |= t->flags;
						}
						// If the constraint contains `string`, we don't need to look for a more preferred type
						if ((allTypeFlags & TypeFlagsString) == 0) {
							std::string str = getStringLiteralValue(source);
							// If the type contains `number` or a number literal and the string isn't a valid number, exclude numbers
							if ((allTypeFlags & TypeFlagsNumberLike) != 0 &&
								!isValidNumberString(str, true /*roundTripOnly*/)) {
								allTypeFlags &= ~TypeFlagsNumberLike;
							}
							// If the type contains `bigint` or a bigint literal and the string isn't a valid bigint, exclude bigints
							if ((allTypeFlags & TypeFlagsBigIntLike) != 0 &&
								!isValidBigIntString(str, true /*roundTripOnly*/)) {
								allTypeFlags &= ~TypeFlagsBigIntLike;
							}
							auto choose = [&](Type* left, Type* right) -> Type* {
								if ((right->flags & allTypeFlags) == 0) {
									return left;
								}
								if (left->flags & TypeFlagsString) {
									return left;
								}
								if (right->flags & TypeFlagsString) {
									return source;
								}
								if (left->flags & TypeFlagsTemplateLiteral) {
									return left;
								}
								if ((right->flags & TypeFlagsTemplateLiteral) != 0 &&
									isTypeMatchedByTemplateLiteralType(
										source, right->AsTemplateLiteralType(),
										compareTypesAssignable)) {
									return source;
								}
								if (left->flags & TypeFlagsStringMapping) {
									return left;
								}
								if ((right->flags & TypeFlagsStringMapping) != 0 &&
									str == applyStringMapping(right->symbol, str)) {
									return source;
								}
								if (left->flags & TypeFlagsStringLiteral) {
									return left;
								}
								if ((right->flags & TypeFlagsStringLiteral) != 0 &&
									getStringLiteralValue(right) == str) {
									return right;
								}
								if (left->flags & TypeFlagsNumber) {
									return left;
								}
								if (right->flags & TypeFlagsNumber) {
									return getNumberLiteralType(numberFromString(str));
								}
								if (left->flags & TypeFlagsEnum) {
									return left;
								}
								if (right->flags & TypeFlagsEnum) {
									return getNumberLiteralType(numberFromString(str));
								}
								if (left->flags & TypeFlagsNumberLiteral) {
									return left;
								}
								if ((right->flags & TypeFlagsNumberLiteral) != 0 &&
									getNumberLiteralValue(right) == numberFromString(str)) {
									return right;
								}
								if (left->flags & TypeFlagsBigInt) {
									return left;
								}
								if (right->flags & TypeFlagsBigInt) {
									return parseBigIntLiteralType(str);
								}
								if (left->flags & TypeFlagsBigIntLiteral) {
									return left;
								}
								if ((right->flags & TypeFlagsBigIntLiteral) != 0 &&
									pseudoBigIntToString(getBigIntLiteralValue(right)) ==
										str) {
									return right;
								}
								if (left->flags & TypeFlagsBoolean) {
									return left;
								}
								if (right->flags & TypeFlagsBoolean) {
									if (str == "true") {
										return trueType;
									}
									if (str == "false") {
										return falseType;
									}
									return booleanType;
								}
								if (left->flags & TypeFlagsBooleanLiteral) {
									return left;
								}
								if ((right->flags & TypeFlagsBooleanLiteral) != 0 &&
									std::string(getBooleanLiteralValue(right) ? "true"
																			  : "false") ==
										str) {
									return right;
								}
								if (left->flags & TypeFlagsUndefined) {
									return left;
								}
								if ((right->flags & TypeFlagsUndefined) != 0 &&
									right->AsIntrinsicType()->intrinsicName == str) {
									return right;
								}
								if (left->flags & TypeFlagsNull) {
									return left;
								}
								if ((right->flags & TypeFlagsNull) != 0 &&
									right->AsIntrinsicType()->intrinsicName == str) {
									return right;
								}
								return left;
							};
							Type* matchingType = neverType;
							for (Type* t : constraint->Distributed()) {
								matchingType = choose(matchingType, t);
							}
							if ((matchingType->flags & TypeFlagsNever) == 0) {
								inferFromTypes(n, matchingType, target);
								continue;
							}
						}
					}
				}
			}
			inferFromTypes(n, source, target);
		}
	}
}

// inferFromGenericMappedTypes — inference.go:687
void Checker::inferFromGenericMappedTypes(InferenceState* n, Type* source, Type* target) {
	// The source and target types are generic types { [P in S]: X } and { [P in T]: Y }, so we infer
	// from S to T and from X to Y.
	inferFromTypes(n, getConstraintTypeFromMappedType(source),
				   getConstraintTypeFromMappedType(target));
	inferFromTypes(n, getTemplateTypeFromMappedType(source),
				   getTemplateTypeFromMappedType(target));
	Type* sourceNameType = getNameTypeFromMappedType(source);
	Type* targetNameType = getNameTypeFromMappedType(target);
	if (sourceNameType != nullptr && targetNameType != nullptr) {
		inferFromTypes(n, sourceNameType, targetNameType);
	}
}

// inferFromObjectTypes — inference.go:699
void Checker::inferFromObjectTypes(InferenceState* n, Type* source, Type* target) {
	if ((source->objectFlags & ObjectFlagsReference) != 0 &&
		(target->objectFlags & ObjectFlagsReference) != 0 &&
		(source->Target() == target->Target() ||
		 (isArrayType(source) && isArrayType(target)))) {
		// If source and target are references to the same generic type, infer from type arguments
		inferFromReferenceTypeArguments(n, source, target);
		return;
	}
	if (isGenericMappedType(source) && isGenericMappedType(target)) {
		inferFromGenericMappedTypes(n, source, target);
	}
	if ((target->objectFlags & ObjectFlagsMapped) != 0 &&
		target->AsMappedType()->declaration->as<MappedTypeNode>()->NameType == nullptr) {
		Type* constraintType = getConstraintTypeFromMappedType(target);
		if (inferToMappedType(n, source, target, constraintType)) {
			return;
		}
	}
	// Infer from the members of source and target only if the two types are possibly related
	if (typesDefinitelyUnrelated(source, target)) {
		return;
	}
	if (isArrayOrTupleType(source)) {
		if (isTupleType(target)) {
			int sourceArity = getTypeReferenceArity(source);
			int targetArity = getTypeReferenceArity(target);
			std::vector<Type*> elementTypes = getTypeArguments(target);
			const std::vector<TupleElementInfo>& elementInfos =
				target->TargetTupleType()->elementInfos;
			// When source and target are tuple types with the same structure (fixed, variadic, and rest are matched
			// to the same kind in each position), simply infer between the element types.
			if (isTupleType(source) && isTupleTypeStructureMatching(source, target)) {
				for (int i = 0; i < targetArity; i++) {
					inferFromTypes(n, getTypeArguments(source)[i], elementTypes[i]);
				}
				return;
			}
			int startLength = 0;
			int endLength = 0;
			if (isTupleType(source)) {
				startLength = std::min(source->TargetTupleType()->fixedLength,
									   target->TargetTupleType()->fixedLength);
				if ((target->TargetTupleType()->combinedFlags & ElementFlagsVariable) != 0) {
					endLength = std::min(
						getEndElementCount(source->TargetTupleType(), ElementFlagsFixed),
						getEndElementCount(target->TargetTupleType(), ElementFlagsFixed));
				}
			}
			// Infer between starting fixed elements.
			for (int i = 0; i < startLength; i++) {
				inferFromTypes(n, getTypeArguments(source)[i], elementTypes[i]);
			}
			if (!isTupleType(source) ||
				(sourceArity - startLength - endLength == 1 &&
				 (source->TargetTupleType()->elementInfos[startLength].flags &
				  ElementFlagsRest) != 0)) {
				// Single rest element remains in source, infer from that to every element in target
				Type* restType = getTypeArguments(source)[startLength];
				for (int i = startLength; i < targetArity - endLength; i++) {
					Type* t = restType;
					if ((elementInfos[i].flags & ElementFlagsVariadic) != 0) {
						t = createArrayType(t);
					}
					inferFromTypes(n, t, elementTypes[i]);
				}
			} else {
				int middleLength = targetArity - startLength - endLength;
				if (middleLength == 2) {
					if ((elementInfos[startLength].flags &
						 elementInfos[startLength + 1].flags & ElementFlagsVariadic) != 0) {
						// Middle of target is [...T, ...U] and source is tuple type
						InferenceInfo* targetInfo =
							getInferenceInfoForType(n, elementTypes[startLength]);
						if (targetInfo != nullptr && targetInfo->impliedArity >= 0) {
							// Infer slices from source based on implied arity of T.
							inferFromTypes(
								n,
								sliceTupleType(source, startLength,
											   endLength + sourceArity -
												   targetInfo->impliedArity),
								elementTypes[startLength]);
							inferFromTypes(n,
										   sliceTupleType(source,
														  startLength +
															  targetInfo->impliedArity,
														  endLength),
										   elementTypes[startLength + 1]);
						}
					} else if ((elementInfos[startLength].flags & ElementFlagsVariadic) != 0 &&
							   (elementInfos[startLength + 1].flags & ElementFlagsRest) != 0) {
						// Middle of target is [...T, ...rest] and source is tuple type
						// if T is constrained by a fixed-size tuple we might be able to use its arity to infer T
						if (InferenceInfo* info =
								getInferenceInfoForType(n, elementTypes[startLength]);
							info != nullptr) {
							Type* constraint = getBaseConstraintOfType(info->typeParameter);
							if (constraint != nullptr && isTupleType(constraint) &&
								(constraint->TargetTupleType()->combinedFlags &
								 ElementFlagsVariable) == 0) {
								int impliedArity =
									constraint->TargetTupleType()->fixedLength;
								inferFromTypes(
									n,
									sliceTupleType(source, startLength,
												   sourceArity - (startLength + impliedArity)),
									elementTypes[startLength]);
								if (Type* restType = getElementTypeOfSliceOfTupleType(
										source, startLength + impliedArity, endLength,
										false, false);
									restType != nullptr) {
									inferFromTypes(n, restType,
												   elementTypes[startLength + 1]);
								}
							}
						}
					} else if ((elementInfos[startLength].flags & ElementFlagsRest) != 0 &&
							   (elementInfos[startLength + 1].flags &
								ElementFlagsVariadic) != 0) {
						// Middle of target is [...rest, ...T] and source is tuple type
						// if T is constrained by a fixed-size tuple we might be able to use its arity to infer T
						if (InferenceInfo* info =
								getInferenceInfoForType(n, elementTypes[startLength + 1]);
							info != nullptr) {
							Type* constraint = getBaseConstraintOfType(info->typeParameter);
							if (constraint != nullptr && isTupleType(constraint) &&
								(constraint->TargetTupleType()->combinedFlags &
								 ElementFlagsVariable) == 0) {
								int impliedArity =
									constraint->TargetTupleType()->fixedLength;
								int endIndex =
									sourceArity -
									getEndElementCount(target->TargetTupleType(),
													   ElementFlagsFixed);
								int startIndex = endIndex - impliedArity;
								if (startIndex >= startLength) {
									std::vector<Type*> sourceArgs =
										getTypeArguments(source);
									std::vector<Type*> argSlice(
										sourceArgs.begin() + startIndex,
										sourceArgs.begin() + endIndex);
									const std::vector<TupleElementInfo>& srcInfos =
										source->TargetTupleType()->elementInfos;
									std::vector<TupleElementInfo> infoSlice(
										srcInfos.begin() + startIndex,
										srcInfos.begin() + endIndex);
									Type* trailingSlice = createTupleTypeEx(
										argSlice, infoSlice, false /*readonly*/);
									if (Type* restType = getElementTypeOfSliceOfTupleType(
											source, startLength, endLength + impliedArity,
											false, false);
										restType != nullptr) {
										inferFromTypes(n, restType,
													   elementTypes[startLength]);
									}
									inferFromTypes(n, trailingSlice,
												   elementTypes[startLength + 1]);
								}
							}
						}
					}
				} else if (middleLength == 1 &&
						   (elementInfos[startLength].flags & ElementFlagsVariadic) != 0) {
					// Middle of target is exactly one variadic element. Infer the slice between the fixed parts in the source.
					// If target ends in optional element(s), make a lower priority a speculative inference.
					InferencePriority priority = ifElse(
						(elementInfos[targetArity - 1].flags & ElementFlagsOptional) != 0,
						InferencePrioritySpeculativeTuple, InferencePriorityNone);
					Type* sourceSlice = sliceTupleType(source, startLength, endLength);
					inferWithPriority(n, sourceSlice, elementTypes[startLength], priority);
				} else if (middleLength == 1 &&
						   (elementInfos[startLength].flags & ElementFlagsRest) != 0) {
					// Middle of target is exactly one rest element. If middle of source is not empty, infer union of middle element types.
					Type* restType = getElementTypeOfSliceOfTupleType(
						source, startLength, endLength, false, false);
					if (restType != nullptr) {
						inferFromTypes(n, restType, elementTypes[startLength]);
					}
				}
			}
			// Infer between ending fixed elements
			for (int i = 0; i < endLength; i++) {
				inferFromTypes(n, getTypeArguments(source)[sourceArity - i - 1],
							   elementTypes[targetArity - i - 1]);
			}
			return;
		}
		if (isArrayType(target)) {
			inferFromIndexTypes(n, source, target);
			return;
		}
	}
	inferFromProperties(n, source, target);
	inferFromSignatures(n, source, target, SignatureKind::Call);
	inferFromSignatures(n, source, target, SignatureKind::Construct);
	inferFromIndexTypes(n, source, target);
}

// inferFromProperties — inference.go:828
void Checker::inferFromProperties(InferenceState* n, Type* source, Type* target) {
	std::vector<Symbol*> properties = getPropertiesOfObjectType(target);
	for (Symbol* targetProp : properties) {
		Symbol* sourceProp = getPropertyOfType(source, targetProp->name);
		if (sourceProp != nullptr &&
			!someList(sourceProp->declarations,
					  [this](Node* d) { return isSkipDirectInferenceNode(d); })) {
			inferFromTypes(
				n,
				removeMissingType(getTypeOfSymbol(sourceProp),
								  (sourceProp->flags & SymbolFlagsOptional) != 0),
				removeMissingType(getTypeOfSymbol(targetProp),
								  (targetProp->flags & SymbolFlagsOptional) != 0));
		}
	}
}

// inferFromSignatures — inference.go:838
void Checker::inferFromSignatures(InferenceState* n, Type* source, Type* target,
								  SignatureKind kind) {
	std::vector<Signature*> sourceSignatures = getSignaturesOfType(source, kind);
	int sourceLen = static_cast<int>(sourceSignatures.size());
	if (sourceLen > 0) {
		// We match source and target signatures from the bottom up, and if the source has fewer signatures
		// than the target, we infer from the first source signature to the excess target signatures.
		std::vector<Signature*> targetSignatures = getSignaturesOfType(target, kind);
		int targetLen = static_cast<int>(targetSignatures.size());
		for (int i = 0; i < targetLen; i++) {
			int sourceIndex = std::max(sourceLen - targetLen + i, 0);
			inferFromSignature(n, getBaseSignature(sourceSignatures[sourceIndex]),
							   getErasedSignature(targetSignatures[i]));
		}
	}
}

// inferFromSignature — inference.go:853
void Checker::inferFromSignature(InferenceState* n, Signature* source, Signature* target) {
	if ((source->flags & SignatureFlagsIsNonInferrable) == 0) {
		bool saveBivariant = n->bivariant;
		Kind kind = Kind::Unknown;
		if (target->declaration != nullptr) {
			kind = target->declaration->kind;
		}
		// Once we descend into a bivariant signature we remain bivariant for all nested inferences
		n->bivariant = n->bivariant || kind == Kind::MethodDeclaration ||
			kind == Kind::MethodSignature || kind == Kind::Constructor;
		applyToParameterTypes(source, target, [this, n](Type* s, Type* t) {
			inferFromContravariantTypesIfStrictFunctionTypes(n, s, t);
		});
		n->bivariant = saveBivariant;
	}
	applyToReturnTypes(source, target,
					 [this, n](Type* s, Type* t) { inferFromTypes(n, s, t); });
}

// applyToParameterTypes — inference.go:868
void Checker::applyToParameterTypes(Signature* source, Signature* target,
									const std::function<void(Type*, Type*)>& callback) {
	int sourceCount = getParameterCount(source);
	int targetCount = getParameterCount(target);
	Type* sourceRestType = getEffectiveRestType(source);
	Type* targetRestType = getEffectiveRestType(target);
	int targetNonRestCount = targetCount;
	if (targetRestType != nullptr) {
		targetNonRestCount--;
	}
	int paramCount = targetNonRestCount;
	if (sourceRestType == nullptr) {
		paramCount = std::min(sourceCount, targetNonRestCount);
	}
	Type* sourceThisType = getThisTypeOfSignature(source);
	if (sourceThisType != nullptr) {
		Type* targetThisType = getThisTypeOfSignature(target);
		if (targetThisType != nullptr) {
			callback(sourceThisType, targetThisType);
		}
	}
	for (int i = 0; i < paramCount; i++) {
		callback(getTypeAtPosition(source, i), getTypeAtPosition(target, i));
	}
	if (targetRestType != nullptr) {
		callback(getRestTypeAtPosition(
					 source, paramCount,
					 isConstTypeVariable(targetRestType, 0) &&
						 !someType(targetRestType, [this](Type* t) {
							 return isMutableArrayLikeType(t);
						 }) /*readonly*/),
				 targetRestType);
	}
}

// applyToReturnTypes — inference.go:896
void Checker::applyToReturnTypes(Signature* source, Signature* target,
								 const std::function<void(Type*, Type*)>& callback) {
	TypePredicate* targetTypePredicate = getTypePredicateOfSignature(target);
	if (targetTypePredicate != nullptr) {
		TypePredicate* sourceTypePredicate = getTypePredicateOfSignature(source);
		if (sourceTypePredicate != nullptr &&
			typePredicateKindsMatch(sourceTypePredicate, targetTypePredicate) &&
			sourceTypePredicate->t != nullptr && targetTypePredicate->t != nullptr) {
			callback(sourceTypePredicate->t, targetTypePredicate->t);
			return;
		}
	}
	Type* targetReturnType = getReturnTypeOfSignature(target);
	if (couldContainTypeVariables(targetReturnType)) {
		callback(getReturnTypeOfSignature(source), targetReturnType);
	}
}

// inferFromIndexTypes — inference.go:911
void Checker::inferFromIndexTypes(InferenceState* n, Type* source, Type* target) {
	// Inferences across mapped type index signatures are pretty much the same a inferences to homomorphic variables
	InferencePriority priority = InferencePriorityNone;
	if ((source->objectFlags & target->objectFlags & ObjectFlagsMapped) != 0) {
		priority = InferencePriorityHomomorphicMappedType;
	}
	std::vector<IndexInfo*> indexInfos = getIndexInfosOfType(target);
	if (isObjectTypeWithInferableIndex(source)) {
		for (IndexInfo* targetInfo : indexInfos) {
			std::vector<Type*> propTypes;
			for (Symbol* prop : getPropertiesOfType(source)) {
				if (isApplicableIndexType(
						getLiteralTypeFromProperty(
							prop, TypeFlagsStringOrNumberLiteralOrUnique, false),
						targetInfo->keyType)) {
					Type* propType = getTypeOfSymbol(prop);
					if ((prop->flags & SymbolFlagsOptional) != 0) {
						propType = removeMissingOrUndefinedType(propType);
					}
					propTypes.push_back(propType);
				}
			}
			for (IndexInfo* info : getIndexInfosOfType(source)) {
				if (isApplicableIndexType(info->keyType, targetInfo->keyType)) {
					propTypes.push_back(info->valueType);
				}
			}
			if (!propTypes.empty()) {
				inferWithPriority(n, getUnionType(propTypes), targetInfo->valueType,
								  priority);
			}
		}
	}
	for (IndexInfo* targetInfo : indexInfos) {
		IndexInfo* sourceInfo = getApplicableIndexInfo(source, targetInfo->keyType);
		if (sourceInfo != nullptr) {
			inferWithPriority(n, sourceInfo->valueType, targetInfo->valueType, priority);
		}
	}
}

// inferToMappedType — inference.go:948
bool Checker::inferToMappedType(InferenceState* n, Type* source, Type* target,
								Type* constraintType) {
	if ((constraintType->flags & TypeFlagsUnion) != 0 ||
		(constraintType->flags & TypeFlagsIntersection) != 0) {
		bool result = false;
		for (Type* t : constraintType->types()) {
			result = orElse(inferToMappedType(n, source, target, t), result);
		}
		return result;
	}
	if (constraintType->flags & TypeFlagsIndex) {
		// We're inferring from some source type S to a homomorphic mapped type { [P in keyof T]: X },
		// where T is a type variable. Use inferTypeForHomomorphicMappedType to infer a suitable source
		// type and then make a secondary inference from that type to T. We make a secondary inference
		// such that direct inferences to T get priority over inferences to Partial<T>, for example.
		InferenceInfo* inference =
			getInferenceInfoForType(n, constraintType->AsIndexType()->target);
		if (inference != nullptr && !inference->isFixed &&
			!isFromInferenceBlockedSource(source)) {
			Type* inferredType =
				inferTypeForHomomorphicMappedType(source, target, constraintType);
			if (inferredType != nullptr) {
				// We assign a lower priority to inferences made from types containing non-inferrable
				// types because we may only have a partial result (i.e. we may have failed to make
				// reverse inferences for some properties).
				inferWithPriority(
					n, inferredType, inference->typeParameter,
					ifElse((source->objectFlags & ObjectFlagsNonInferrableType) != 0,
						   InferencePriorityPartialHomomorphicMappedType,
						   InferencePriorityHomomorphicMappedType));
			}
		}
		return true;
	}
	if (constraintType->flags & TypeFlagsTypeParameter) {
		// We're inferring from some source type S to a mapped type { [P in K]: X }, where K is a type
		// parameter. First infer from 'keyof S' to K.
		inferWithPriority(n,
						  getIndexTypeEx(source,
										 ifElse(patternForType.find(source) !=
													patternForType.end(),
												IndexFlagsNoIndexSignatures,
												IndexFlagsNone)),
						  constraintType, InferencePriorityMappedTypeConstraint);
		// If K is constrained to a type C, also infer to C. Thus, for a mapped type { [P in K]: X },
		// where K extends keyof T, we make the same inferences as for a homomorphic mapped type
		// { [P in keyof T]: X }. This enables us to make meaningful inferences when the target is a
		// Pick<T, K>.
		Type* extendedConstraint = getConstraintOfType(constraintType);
		if (extendedConstraint != nullptr &&
			inferToMappedType(n, source, target, extendedConstraint)) {
			return true;
		}
		// If no inferences can be made to K's constraint, infer from a union of the property types
		// in the source to the template type X.
		std::vector<Type*> propTypes = mapVec(
			getPropertiesOfType(source), [this](Symbol* s) { return getTypeOfSymbol(s); });
		std::vector<Type*> indexTypes =
			mapVec(getIndexInfosOfType(source), [this](IndexInfo* info) {
				if (info != enumNumberIndexInfo) {
					return info->valueType;
				}
				return neverType;
			});
		inferFromTypes(n, getUnionType(concatenate(propTypes, indexTypes)),
					   getTemplateTypeFromMappedType(target));
		return true;
	}
	return false;
}

// Infer a suitable input type for a homomorphic mapped type { [P in keyof T]: X }. We construct
// an object type with the same set of properties as the source type, where the type of each
// property is computed by inferring from the source property type to X for the type
// variable T[P] (i.e. we treat the type T[P] as the type variable we're inferring for).
//
// inferTypeForHomomorphicMappedType — inference.go:1004
Type* Checker::inferTypeForHomomorphicMappedType(Type* source, Type* target,
												 Type* constraint) {
	ReverseMappedTypeKey key{source->id, target->id, constraint->id};
	if (auto it = reverseHomomorphicMappedCache.find(key);
		it != reverseHomomorphicMappedCache.end()) {
		return it->second;
	}
	Type* t = createReverseMappedType(source, target, constraint);
	reverseHomomorphicMappedCache[key] = t;
	return t;
}

// createReverseMappedType — inference.go:1014
Type* Checker::createReverseMappedType(Type* source, Type* target, Type* constraint) {
	// We consider a source type reverse mappable if it has a string index signature or if
	// it has one or more properties and is of a partially inferable type.
	if (!(getIndexInfoOfType(source, stringType) != nullptr ||
		  (!getPropertiesOfType(source).empty() && isPartiallyInferableType(source)))) {
		return nullptr;
	}
	// For arrays and tuples we infer new arrays and tuples where the reverse mapping has been
	// applied to the element type(s).
	if (isArrayType(source)) {
		Type* elementType =
			inferReverseMappedType(getTypeArguments(source)[0], target, constraint);
		if (elementType == nullptr) {
			return nullptr;
		}
		return createArrayTypeEx(elementType, isReadonlyArrayType(source));
	}
	if (isTupleType(source)) {
		std::vector<Type*> elementTypes =
			mapVec(getElementTypes(source), [this, target, constraint](Type* t) {
				return inferReverseMappedType(t, target, constraint);
			});
		if (!everyList(elementTypes, [](Type* t) { return t != nullptr; })) {
			return nullptr;
		}
		std::vector<TupleElementInfo> elementInfos =
			source->TargetTupleType()->elementInfos;
		if ((getMappedTypeModifiers(target) & MappedTypeModifiersIncludeOptional) != 0) {
			elementInfos = sameMap(elementInfos, [](TupleElementInfo info) {
				if ((info.flags & ElementFlagsOptional) != 0) {
					return TupleElementInfo{ElementFlagsRequired,
											info.labeledDeclaration};
				}
				return info;
			});
		}
		return createTupleTypeEx(elementTypes, elementInfos,
								 source->TargetTupleType()->readonly);
	}
	// For all other object types we infer a new object type where the reverse mapping has been
	// applied to the type of each property.
	Type* reversed =
		newObjectType(ObjectFlagsReverseMapped | ObjectFlagsAnonymous, nullptr /*symbol*/);
	reversed->AsReverseMappedType()->source = source;
	reversed->AsReverseMappedType()->mappedType = target;
	reversed->AsReverseMappedType()->constraintType = constraint;
	return reversed;
}

// We consider a type to be partially inferable if it isn't marked non-inferable or if it is
// an object literal type with at least one property of an inferable type. For example, an object
// literal { a: 123, b: x => true } is marked non-inferable because it contains a context sensitive
// arrow function, but is considered partially inferable because property 'a' has an inferable type.
//
// isPartiallyInferableType — inference.go:1060
bool Checker::isPartiallyInferableType(Type* t) {
	return (t->objectFlags & ObjectFlagsNonInferrableType) == 0 ||
		(isObjectLiteralType(t) &&
		 someList(getPropertiesOfType(t), [this](Symbol* prop) {
			 return isPartiallyInferableType(getTypeOfSymbol(prop));
		 })) ||
		(isTupleType(t) &&
		 someList(getElementTypes(t),
				  [this](Type* t2) { return isPartiallyInferableType(t2); }));
}

// inferReverseMappedType — inference.go:1066
Type* Checker::inferReverseMappedType(Type* source, Type* target, Type* constraint) {
	ReverseMappedTypeKey key{source->id, target->id, constraint->id};
	if (auto it = reverseMappedCache.find(key); it != reverseMappedCache.end()) {
		return orElse(it->second, unknownType);
	}
	reverseMappedSourceStack.push_back(source);
	reverseMappedTargetStack.push_back(target);
	ExpandingFlags saveExpandingFlags = reverseExpandingFlags;
	if (isDeeplyNestedType(source, reverseMappedSourceStack, 2)) {
		reverseExpandingFlags |= ExpandingFlagsSource;
	}
	if (isDeeplyNestedType(target, reverseMappedTargetStack, 2)) {
		reverseExpandingFlags |= ExpandingFlagsTarget;
	}
	Type* t = nullptr;
	if (reverseExpandingFlags != ExpandingFlagsBoth) {
		t = inferReverseMappedTypeWorker(source, target, constraint);
	}
	reverseMappedSourceStack.pop_back();
	reverseMappedTargetStack.pop_back();
	reverseExpandingFlags = saveExpandingFlags;
	reverseMappedCache[key] = t;
	return t;
}

// inferReverseMappedTypeWorker — inference.go:1091
Type* Checker::inferReverseMappedTypeWorker(Type* source, Type* target,
											Type* constraint) {
	Type* typeParameter = getIndexedAccessType(constraint->AsIndexType()->target,
											   getTypeParameterFromMappedType(target));
	Type* templateType = getTemplateTypeFromMappedType(target);
	InferenceInfo* inference = newInferenceInfo(typeParameter);
	std::vector<InferenceInfo*> inferences{inference};
	inferTypes(inferences, source, templateType, InferencePriorityNone, false);
	return getWidenedType(orElse(getTypeFromInference(inference), unknownType));
}

// resolveReverseMappedTypeMembers — inference.go:1099
void Checker::resolveReverseMappedTypeMembers(Type* t) {
	ReverseMappedType* r = t->AsReverseMappedType();
	IndexInfo* indexInfo = getIndexInfoOfType(r->source, stringType);
	MappedTypeModifiers modifiers = getMappedTypeModifiers(r->mappedType);
	bool readonlyMask = (modifiers & MappedTypeModifiersIncludeReadonly) == 0;
	SymbolFlags optionalMask = ifElse(
		(modifiers & MappedTypeModifiersIncludeOptional) != 0, SymbolFlagsNone,
		SymbolFlagsOptional);
	std::vector<IndexInfo*> indexInfos;
	if (indexInfo != nullptr) {
		indexInfos = {newIndexInfo(
			stringType,
			orElse(inferReverseMappedType(indexInfo->valueType, r->mappedType,
										  r->constraintType),
				   unknownType),
			readonlyMask && indexInfo->isReadonly, nullptr, {})};
	}
	SymbolTable members;
	Type* limitedConstraint = getLimitedConstraint(t);
	for (Symbol* prop : getPropertiesOfType(r->source)) {
		// In case of a reverse mapped type with an intersection constraint, if we were able to
		// extract the filtering type literals we skip those properties that are not assignable to them,
		// because the extra properties wouldn't get through the application of the mapped type anyway
		if (limitedConstraint != nullptr) {
			Type* propertyNameType = getLiteralTypeFromProperty(
				prop, TypeFlagsStringOrNumberLiteralOrUnique, false);
			if (!isTypeAssignableTo(propertyNameType, limitedConstraint)) {
				continue;
			}
		}
		CheckFlags checkFlags = CheckFlagsReverseMapped |
			ifElse(readonlyMask && isReadonlySymbol(prop), CheckFlagsReadonly,
				   CheckFlagsNone);
		Symbol* inferredProp = newSymbolEx(
			SymbolFlagsProperty | (prop->flags & optionalMask), prop->name, checkFlags);
		inferredProp->declarations = prop->declarations;
		valueSymbolLinks.Get(inferredProp)->nameType =
			valueSymbolLinks.Get(prop)->nameType;
		ReverseMappedSymbolLinks* links = reverseMappedSymbolLinks.Get(inferredProp);
		links->propertyType = getTypeOfSymbol(prop);
		Type* constraintTarget = r->constraintType->AsIndexType()->target;
		if ((constraintTarget->flags & TypeFlagsIndexedAccess) != 0 &&
			(constraintTarget->AsIndexedAccessType()->objectType->flags &
			 TypeFlagsTypeParameter) != 0 &&
			(constraintTarget->AsIndexedAccessType()->indexType->flags &
			 TypeFlagsTypeParameter) != 0) {
			// A reverse mapping of `{[K in keyof T[K_1]]: T[K_1]}` is the same as that of `{[K in keyof T]: T}`, since all we care about is
			// inferring to the "type parameter" (or indexed access) shared by the constraint and template. So, to reduce the number of
			// type identities produced, we simplify such indexed access occurrences
			Type* newTypeParam = constraintTarget->AsIndexedAccessType()->objectType;
			Type* newMappedType =
				replaceIndexedAccess(r->mappedType, constraintTarget, newTypeParam);
			links->mappedType = newMappedType;
			links->constraintType = getIndexType(newTypeParam);
		} else {
			links->mappedType = r->mappedType;
			links->constraintType = r->constraintType;
		}
		members[prop->name] = inferredProp;
	}
	setStructuredTypeMembers(t, std::move(members), {}, {}, indexInfos);
}

// getTypeOfReverseMappedSymbol — inference.go:1145
Type* Checker::getTypeOfReverseMappedSymbol(Symbol* symbol) {
	ValueSymbolLinks* links = valueSymbolLinks.Get(symbol);
	if (links->resolvedType == nullptr ||
		staleForCheckFile(links->resolvedTypeCheckFile)) {
		// Go: fresh per-checker cache — recompute under this file.
		links->resolvedType = nullptr;
		links->resolvedTypeCheckFile = checkFileTag();
		ReverseMappedSymbolLinks* reverseLinks = reverseMappedSymbolLinks.Get(symbol);
		links->resolvedType = orElse(
			inferReverseMappedType(reverseLinks->propertyType, reverseLinks->mappedType,
								   reverseLinks->constraintType),
			unknownType);
	}
	return links->resolvedType;
}

// If the original mapped type had an intersection constraint we extract its components,
// and we make an attempt to do so even if the intersection has been reduced to a union.
// This entire process allows us to possibly retrieve the filtering type literals.
// e.g. { [K in keyof U & ("a" | "b") ] } -> "a" | "b"
//
// getLimitedConstraint — inference.go:1158
Type* Checker::getLimitedConstraint(Type* t) {
	Type* constraint =
		getConstraintTypeFromMappedType(t->AsReverseMappedType()->mappedType);
	if (!((constraint->flags & TypeFlagsUnion) != 0 ||
		  (constraint->flags & TypeFlagsIntersection) != 0)) {
		return nullptr;
	}
	Type* origin = constraint;
	if (constraint->flags & TypeFlagsUnion) {
		origin = constraint->AsUnionType()->origin;
	}
	if (origin == nullptr || (origin->flags & TypeFlagsIntersection) == 0) {
		return nullptr;
	}
	Type* constraintType = t->AsReverseMappedType()->constraintType;
	Type* limitedConstraint = getIntersectionType(filterVec(
		origin->types(), [constraintType](Type* t) { return t != constraintType; }));
	if (limitedConstraint != neverType) {
		return limitedConstraint;
	}
	return nullptr;
}

// replaceIndexedAccess — inference.go:1178
Type* Checker::replaceIndexedAccess(Type* instantiable, Type* t, Type* replacement) {
	// map type.indexType to 0
	// map type.objectType to `[TReplacement]`
	// thus making the indexed access `[TReplacement][0]` or `TReplacement`
	return instantiateType(
		instantiable,
		newTypeMapper(
			{t->AsIndexedAccessType()->indexType,
			 t->AsIndexedAccessType()->objectType},
			{getNumberLiteralType(0), createTupleType({replacement})}));
}

// typesDefinitelyUnrelated — inference.go:1185
bool Checker::typesDefinitelyUnrelated(Type* source, Type* target) {
	// Two tuple types with incompatible arities are definitely unrelated.
	// Two object types that each have a property that is unmatched in the other are definitely unrelated.
	if (isTupleType(source) && isTupleType(target)) {
		return tupleTypesDefinitelyUnrelated(source, target);
	}
	return getUnmatchedProperty(source, target, false /*requireOptionalProperties*/,
							  true /*matchDiscriminantProperties*/) != nullptr &&
		getUnmatchedProperty(target, source, false /*requireOptionalProperties*/,
							 false /*matchDiscriminantProperties*/) != nullptr;
}

// tupleTypesDefinitelyUnrelated — inference.go:1195
static bool tupleTypesDefinitelyUnrelated(Type* source, Type* target) {
	TupleType* s = source->TargetTupleType();
	TupleType* t = target->TargetTupleType();
	return ((t->combinedFlags & ElementFlagsVariadic) == 0 &&
			t->minLength > s->minLength) ||
		((t->combinedFlags & ElementFlagsVariable) == 0 &&
		 ((s->combinedFlags & ElementFlagsVariable) != 0 ||
		  t->fixedLength < s->fixedLength));
}

// isTupleTypeStructureMatching — inference.go:1202
bool Checker::isTupleTypeStructureMatching(Type* t1, Type* t2) {
	if (getTypeReferenceArity(t1) != getTypeReferenceArity(t2)) {
		return false;
	}
	for (size_t i = 0; i < t1->TargetTupleType()->elementInfos.size(); i++) {
		TupleElementInfo& e = t1->TargetTupleType()->elementInfos[i];
		if ((e.flags & ElementFlagsVariable) !=
			(t2->TargetTupleType()->elementInfos[i].flags & ElementFlagsVariable)) {
			return false;
		}
	}
	return true;
}

// isTypeOrBaseIdenticalTo — inference.go:1214
bool Checker::isTypeOrBaseIdenticalTo(Type* s, Type* t) {
	if (t == missingType) {
		return s == t;
	}
	return isTypeIdenticalTo(s, t) ||
		((t->flags & TypeFlagsString) != 0 &&
		 (s->flags & TypeFlagsStringLiteral) != 0) ||
		((t->flags & TypeFlagsNumber) != 0 &&
		 (s->flags & TypeFlagsNumberLiteral) != 0);
}

// isTypeCloselyMatchedBy — inference.go:1223
bool Checker::isTypeCloselyMatchedBy(Type* s, Type* t) {
	return ((s->flags & TypeFlagsObject) != 0 && (t->flags & TypeFlagsObject) != 0 &&
			s->symbol != nullptr && s->symbol == t->symbol) ||
		(s->alias != nullptr && t->alias != nullptr &&
		 !s->alias->typeArguments.empty() && s->alias->symbol == t->alias->symbol);
}

// Create an object with properties named in the string literal type. Every property has type `any`.
//
// createEmptyObjectTypeFromStringLiteral — inference.go:1229
Type* Checker::createEmptyObjectTypeFromStringLiteral(Type* t) {
	SymbolTable members;
	for (Type* t2 : t->Distributed()) {
		if ((t2->flags & TypeFlagsStringLiteral) == 0) {
			continue;
		}
		std::string name = getStringLiteralValue(t2);
		Symbol* literalProp = newSymbol(SymbolFlagsProperty, name);
		valueSymbolLinks.Get(literalProp)->resolvedType = anyType;
		if (t2->symbol != nullptr) {
			literalProp->declarations = t2->symbol->declarations;
			literalProp->valueDeclaration = t2->symbol->valueDeclaration;
		}
		members[name] = literalProp;
	}
	std::vector<IndexInfo*> indexInfos;
	if (t->flags & TypeFlagsString) {
		indexInfos = {newIndexInfo(stringType, emptyObjectType, false /*isReadonly*/,
								   nullptr, {})};
	}
	return newAnonymousType(nullptr, members, {}, {}, indexInfos);
}

// newInferenceContext — inference.go:1251
InferenceContext* Checker::newInferenceContext(
	const std::vector<Type*>& typeParameters, Signature* signature,
	InferenceFlags flags, TypeComparer compareTypes) {
	if (!compareTypes) {
		compareTypes = compareTypesAssignable;
	}
	return newInferenceContextWorker(mapVec(typeParameters, newInferenceInfo), signature,
								   flags, compareTypes);
}

// cloneInferenceContext — inference.go:1258
InferenceContext* Checker::cloneInferenceContext(InferenceContext* n,
												 InferenceFlags extraFlags) {
	if (n == nullptr) {
		return nullptr;
	}
	return newInferenceContextWorker(mapVec(n->inferences, cloneInferenceInfo),
								   n->signature, n->flags | extraFlags, n->compareTypes);
}

// cloneInferredPartOfContext — inference.go:1265
InferenceContext* Checker::cloneInferredPartOfContext(InferenceContext* n) {
	std::vector<InferenceInfo*> inferences =
		filterVec(n->inferences, hasInferenceCandidates);
	if (inferences.empty()) {
		return nullptr;
	}
	return newInferenceContextWorker(mapVec(inferences, cloneInferenceInfo), n->signature,
								   n->flags, n->compareTypes);
}

// newInferenceContextWorker — inference.go:1273
InferenceContext* Checker::newInferenceContextWorker(
	std::vector<InferenceInfo*> inferences, Signature* signature, InferenceFlags flags,
	TypeComparer compareTypes) {
	auto* n = new InferenceContext();
	n->inferences = inferences;
	n->signature = signature;
	n->flags = flags;
	n->compareTypes = compareTypes;
	n->mapper = newInferenceTypeMapper(n, true /*fixing*/);
	n->nonFixingMapper = newInferenceTypeMapper(n, false /*fixing*/);
	return n;
}

// addIntraExpressionInferenceSite — inference.go:1285
void Checker::addIntraExpressionInferenceSite(InferenceContext* n, Node* node, Type* t) {
	n->intraExpressionInferenceSites.push_back(IntraExpressionInferenceSite{node, t});
}

// We collect intra-expression inference sites within object and array literals to handle cases where
// inferred types flow between context sensitive element expressions. For example:
//
//	declare function foo<T>(arg: [(n: number) => T, (x: T) => void]): void;
//	foo([_a => 0, n => n.toFixed()]);
//
// Above, both arrow functions in the tuple argument are context sensitive, thus both are omitted from the
// pass that collects inferences from the non-context sensitive parts of the arguments. In the subsequent
// pass where nothing is omitted, we need to commit to an inference for T in order to contextually type the
// parameter in the second arrow function, but we want to first infer from the return type of the first
// arrow function. This happens automatically when the arrow functions are discrete arguments (because we
// infer from each argument before processing the next), but when the arrow functions are elements of an
// object or array literal, we need to perform intra-expression inferences early.
//
// inferFromIntraExpressionSites — inference.go:1302
void Checker::inferFromIntraExpressionSites(InferenceContext* n) {
	for (IntraExpressionInferenceSite& site : n->intraExpressionInferenceSites) {
		Type* contextualType;
		if (isMethodDeclaration(site.node)) {
			contextualType =
				getContextualTypeForObjectLiteralMethod(site.node, ContextFlagsNoConstraints);
		} else {
			contextualType = getContextualType(site.node, ContextFlagsNoConstraints);
		}
		if (contextualType != nullptr) {
			inferTypes(n->inferences, site.t, contextualType, InferencePriorityNone, false);
		}
	}
	n->intraExpressionInferenceSites.clear();
}

// getInferredType — inference.go:1317
Type* Checker::getInferredType(InferenceContext* n, size_t index) {
	InferenceInfo* inference = n->inferences[index];
	if (inference->inferredType == nullptr) {
		if (inference->typeParameter == errorType) {
			return inference->typeParameter;
		}
		Type* inferredType = nullptr;
		Type* fallbackType = nullptr;
		if (n->signature != nullptr) {
			Type* inferredCovariantType = nullptr;
			if (!inference->candidates.empty()) {
				inferredCovariantType = getCovariantInference(inference, n->signature);
			}
			Type* inferredContravariantType = nullptr;
			if (!inference->contraCandidates.empty()) {
				inferredContravariantType = getContravariantInference(inference);
			}
			if (inferredCovariantType != nullptr || inferredContravariantType != nullptr) {
				// If we have both co- and contra-variant inferences, we prefer the co-variant inference if it is not 'never',
				// all co-variant inferences are assignable to it (i.e. it isn't one of a conflicting set of candidates), it is
				// assignable to some contra-variant inference, and no other type parameter is constrained to this type parameter
				// and has inferences that would conflict. Otherwise, we prefer the contra-variant inference.
				// Similarly ignore co-variant `any` inference when both are available as almost everything is assignable to it
				// and it would spoil the overall inference.
				bool preferCovariantType =
					inferredCovariantType != nullptr &&
					(inferredContravariantType == nullptr ||
					 ((inferredCovariantType->flags & (TypeFlagsNever | TypeFlagsAny)) ==
						  0 &&
					  someList(inference->contraCandidates,
							   [this, inferredCovariantType](Type* t) {
								   return isTypeAssignableTo(inferredCovariantType, t);
							   }) &&
					  everyList(n->inferences,
								[this, inference, inferredCovariantType](
									InferenceInfo* other) {
									return (other != inference &&
											getConstraintOfTypeParameter(
												other->typeParameter) !=
												inference->typeParameter) ||
										everyList(other->candidates,
												  [this, inferredCovariantType](Type* t) {
													  return isTypeAssignableTo(
														  t, inferredCovariantType);
												  });
								})));
				if (preferCovariantType) {
					inferredType = inferredCovariantType;
					fallbackType = inferredContravariantType;
				} else {
					inferredType = inferredContravariantType;
					fallbackType = inferredCovariantType;
				}
			} else if ((n->flags & InferenceFlagsNoDefault) != 0) {
				// We use silentNeverType as the wildcard that signals no inferences.
				inferredType = silentNeverType;
			} else {
				// Infer either the default or the empty object type when no inferences were
				// made. It is important to remember that in this case, inference still
				// succeeds, meaning there is no error for not having inference candidates. An
				// inference error only occurs when there are *conflicting* candidates, i.e.
				// candidates with no common supertype.
				Type* defaultType = getDefaultFromTypeParameter(inference->typeParameter);
				if (defaultType != nullptr) {
					// Instantiate the default type. Any forward reference to a type
					// parameter should be instantiated to the empty object type.
					inferredType = instantiateType(
						defaultType,
						mergeTypeMappers(newBackreferenceMapper(n, static_cast<int>(index)),
										 n->nonFixingMapper));
				}
			}
		} else {
			inferredType = getTypeFromInference(inference);
		}
		inference->inferredType = inferredType;
		if (inference->inferredType == nullptr) {
			inference->inferredType = ifElse(
				(n->flags & InferenceFlagsAnyDefault) != 0, anyType, unknownType);
		}
		Type* constraint = getConstraintOfTypeParameter(inference->typeParameter);
		if (constraint != nullptr) {
			Type* instantiatedConstraint = instantiateType(constraint, n->nonFixingMapper);
			// A pure return type inference is still filtered in a recursive
			// call resolution, whose result can become the type of the
			// enclosing declaration.
			if (inferredType != nullptr &&
				((n->flags & InferenceFlagsNoConstraintChecks) == 0 ||
				 inference->priority == InferencePriorityReturnType)) {
				Type* constraintWithThis = getTypeWithThisArgument(
					instantiatedConstraint, inferredType, false);
				if (n->compareTypes(inferredType, constraintWithThis, false) ==
					Ternary::False) {
					Type* filteredByConstraint = nullptr;
					if (inference->priority == InferencePriorityReturnType) {
						// If we have a pure return type inference, we may succeed by removing constituents of the inferred type
						// that aren't assignable to the constraint type (pure return type inferences are speculation anyway).
						filteredByConstraint =
							mapType(inferredType, [this, n, constraintWithThis](Type* t) {
								return ifElse(n->compareTypes(t, constraintWithThis, false) !=
												  Ternary::False,
											  t, neverType);
							});
					}
					inferredType =
						ifElse(filteredByConstraint != nullptr &&
								   (filteredByConstraint->flags & TypeFlagsNever) == 0,
							   filteredByConstraint, static_cast<Type*>(nullptr));
				}
			}
			if (inferredType == nullptr) {
				// If the fallback type satisfies the constraint, we pick it. Otherwise, we pick the constraint.
				inferredType = ifElse(
					fallbackType != nullptr &&
						n->compareTypes(
							fallbackType,
							getTypeWithThisArgument(instantiatedConstraint, fallbackType,
													false),
							false) != Ternary::False,
					fallbackType, instantiatedConstraint);
			}
			inference->inferredType = inferredType;
		}
		clearActiveMapperCaches();
	}
	return inference->inferredType;
}

// getInferredTypes — inference.go:1406
std::vector<Type*> Checker::getInferredTypes(InferenceContext* n) {
	std::vector<Type*> result(n->inferences.size());
	for (size_t i = 0; i < n->inferences.size(); i++) {
		result[i] = getInferredType(n, i);
	}
	return result;
}

// getMapperFromContext — inference.go:1414
TypeMapper* Checker::getMapperFromContext(InferenceContext* n) {
	if (n == nullptr) {
		return nullptr;
	}
	return n->mapper;
}

// Return a type mapper that combines the context's return mapper with a mapper that erases any additional type parameters
// to their inferences at the time of creation.
//
// createOuterReturnMapper — inference.go:1423
TypeMapper* Checker::createOuterReturnMapper(InferenceContext* context) {
	if (context->outerReturnMapper == nullptr) {
		TypeMapper* mapper =
			cloneInferenceContext(context, InferenceFlagsNone)->mapper;
		if (context->returnMapper != nullptr) {
			mapper = newMergedTypeMapper(context->returnMapper, mapper);
		}
		context->outerReturnMapper = mapper;
	}
	return context->outerReturnMapper;
}

// getCovariantInference — inference.go:1434
Type* Checker::getCovariantInference(InferenceInfo* inference, Signature* signature) {
	// Extract all object and array literal types and replace them with a single widened and normalized type.
	std::vector<Type*> candidates =
		unionObjectAndArrayLiteralCandidates(inference->candidates);
	// We widen inferred literal types if
	// all inferences were made to top-level occurrences of the type parameter, and
	// the type parameter has no constraint or its constraint includes no primitive or literal types, and
	// the type parameter was fixed during inference or does not occur at top-level in the return type.
	bool primitiveConstraint = hasPrimitiveConstraint(inference->typeParameter) ||
		isConstTypeVariable(inference->typeParameter, 0);
	bool widenLiteralTypes = !primitiveConstraint && inference->topLevel &&
		(inference->isFixed ||
		 !isTypeParameterAtTopLevelInReturnType(signature, inference->typeParameter));
	std::vector<Type*> baseCandidates;
	if (primitiveConstraint) {
		baseCandidates = sameMap(candidates, [this](Type* t) {
			return getRegularTypeOfLiteralType(t);
		});
	} else if (widenLiteralTypes) {
		baseCandidates = sameMap(candidates, [this](Type* t) {
			return getWidenedLiteralType(t);
		});
	} else {
		baseCandidates = candidates;
	}
	// If all inferences were made from a position that implies a combined result, infer a union type.
	// Otherwise, infer a common supertype.
	Type* unwidenedType;
	if ((inference->priority & InferencePriorityPriorityImpliesCombination) != 0) {
		unwidenedType =
			getUnionTypeEx(baseCandidates, UnionReductionSubtype, nullptr, nullptr);
	} else {
		unwidenedType = getCommonSupertype(baseCandidates);
	}
	return getWidenedType(unwidenedType);
}

// getContravariantInference — inference.go:1463
Type* Checker::getContravariantInference(InferenceInfo* inference) {
	if ((inference->priority & InferencePriorityPriorityImpliesCombination) != 0) {
		return getIntersectionType(inference->contraCandidates);
	}
	return getCommonSubtype(inference->contraCandidates);
}

// unionObjectAndArrayLiteralCandidates — inference.go:1470
std::vector<Type*> Checker::unionObjectAndArrayLiteralCandidates(
	const std::vector<Type*>& candidates) {
	if (candidates.size() > 1) {
		std::vector<Type*> objectLiterals =
			filterVec(candidates, [](Type* t) { return isObjectOrArrayLiteralType(t); });
		if (!objectLiterals.empty()) {
			Type* literalsType =
				getUnionTypeEx(objectLiterals, UnionReductionSubtype, nullptr, nullptr);
			std::vector<Type*> nonLiteralTypes = filterVec(
				candidates, [](Type* t) { return !isObjectOrArrayLiteralType(t); });
			return concatenate(nonLiteralTypes, {literalsType});
		}
	}
	return candidates;
}

// hasPrimitiveConstraint — inference.go:1482
bool Checker::hasPrimitiveConstraint(Type* t) {
	Type* constraint = getConstraintOfTypeParameter(t);
	if (constraint != nullptr) {
		if (constraint->flags & TypeFlagsConditional) {
			constraint = getDefaultConstraintOfConditionalType(constraint);
		}
		return maybeTypeOfKind(constraint,
							   TypeFlagsPrimitive | TypeFlagsIndex |
								   TypeFlagsTemplateLiteral | TypeFlagsStringMapping);
	}
	return false;
}

// isTypeParameterAtTopLevel — inference.go:1493
bool Checker::isTypeParameterAtTopLevel(Type* t, Type* tp, int depth) {
	return t == tp ||
		((t->flags & TypeFlagsUnionOrIntersection) != 0 &&
		 someList(t->types(),
				  [this, tp, depth](Type* t2) {
					  return isTypeParameterAtTopLevel(t2, tp, depth);
				  })) ||
		(depth < 3 && (t->flags & TypeFlagsConditional) != 0 &&
		 (isTypeParameterAtTopLevel(getTrueTypeFromConditionalType(t), tp, depth + 1) ||
		  isTypeParameterAtTopLevel(getFalseTypeFromConditionalType(t), tp, depth + 1)));
}

// isTypeParameterAtTopLevelInReturnType — inference.go:1501
bool Checker::isTypeParameterAtTopLevelInReturnType(Signature* signature,
													Type* typeParameter) {
	TypePredicate* typePredicate = getTypePredicateOfSignature(signature);
	if (typePredicate != nullptr) {
		return typePredicate->t != nullptr &&
			isTypeParameterAtTopLevel(typePredicate->t, typeParameter, 0);
	}
	return isTypeParameterAtTopLevel(getReturnTypeOfSignature(signature), typeParameter,
								   0);
}

// getTypeFromInference — inference.go:1509
Type* Checker::getTypeFromInference(InferenceInfo* inference) {
	if (!inference->candidates.empty()) {
		return getUnionTypeEx(inference->candidates, UnionReductionSubtype, nullptr,
							  nullptr);
	}
	if (!inference->contraCandidates.empty()) {
		return getIntersectionType(inference->contraCandidates);
	}
	return nullptr;
}

// getCommonSupertype — inference.go:1531
Type* Checker::getCommonSupertype(std::vector<Type*> types) {
	if (types.size() == 1) {
		return types[0];
	}
	// Remove nullable types from each of the candidates.
	std::vector<Type*> primaryTypes = types;
	if (strictNullChecks) {
		primaryTypes = sameMap(types, [this](Type* t) {
			return filterType(t, [](Type* u) {
				return (u->flags & TypeFlagsNullable) == 0;
			});
		});
	}
	// When the candidate types are all literal types with the same base type, return a union
	// of those literal types. Otherwise, return the leftmost type for which no type to the
	// right is a supertype.
	Type* supertype;
	if (literalTypesWithSameBaseType(primaryTypes)) {
		supertype = getUnionType(primaryTypes);
	} else {
		supertype = getSingleCommonSupertype(primaryTypes);
	}
	// Add any nullable types that occurred in the candidates back to the result.
	if (sameVec(primaryTypes, types)) {
		return supertype;
	}
	return getNullableType(supertype, getCombinedTypeFlags(types) & TypeFlagsNullable);
}

// getSingleCommonSupertype — inference.go:1558
Type* Checker::getSingleCommonSupertype(std::vector<Type*> types) {
	// First, find the leftmost type for which no type to the right is a strict supertype, and if that
	// type is a strict supertype of all other candidates, return it. Otherwise, return the leftmost type
	// for which no type to the right is a (regular) supertype.
	Type* candidate = findLeftmostType(types, &Checker::isTypeStrictSubtypeOf);
	if (everyList(types, [this, candidate](Type* t) {
			return t == candidate || isTypeStrictSubtypeOf(t, candidate);
		})) {
		return candidate;
	}
	return findLeftmostType(types, &Checker::isTypeSubtypeOf);
}

// findLeftmostType — inference.go:1569
Type* Checker::findLeftmostType(const std::vector<Type*>& types,
								bool (Checker::*f)(Type*, Type*)) {
	Type* candidate = nullptr;
	for (Type* t : types) {
		if (candidate == nullptr || (this->*f)(candidate, t)) {
			candidate = t;
		}
	}
	return candidate;
}

// Return the leftmost type for which no type to the right is a subtype.
//
// getCommonSubtype — inference.go:1580
Type* Checker::getCommonSubtype(const std::vector<Type*>& types) {
	Type* subtype = nullptr;
	for (Type* t : types) {
		if (subtype == nullptr || isTypeSubtypeOf(t, subtype)) {
			subtype = t;
		}
	}
	return subtype;
}

// getCombinedTypeFlags — inference.go:1590
TypeFlags Checker::getCombinedTypeFlags(const std::vector<Type*>& types) {
	TypeFlags flags = TypeFlagsNone;
	for (Type* t : types) {
		if (t->flags & TypeFlagsUnion) {
			flags |= getCombinedTypeFlags(t->types());
		} else {
			flags |= t->flags;
		}
	}
	return flags;
}

// literalTypesWithSameBaseType — inference.go:1602
bool Checker::literalTypesWithSameBaseType(const std::vector<Type*>& types) {
	Type* commonBaseType = nullptr;
	for (Type* t : types) {
		if ((t->flags & TypeFlagsNever) == 0) {
			Type* baseType = getBaseTypeOfLiteralType(t);
			if (commonBaseType == nullptr) {
				commonBaseType = baseType;
			}
			if (baseType == t || baseType != commonBaseType) {
				return false;
			}
		}
	}
	return true;
}

// isFromInferenceBlockedSource — inference.go:1618
bool Checker::isFromInferenceBlockedSource(Type* t) {
	return t->symbol != nullptr &&
		someList(t->symbol->declarations,
				 [this](Node* d) { return isSkipDirectInferenceNode(d); });
}

// isSkipDirectInferenceNode — inference.go:1622
bool Checker::isSkipDirectInferenceNode(Node* node) {
	return skipDirectInferenceNodes.find(node) != skipDirectInferenceNodes.end();
}

// hasOverlappingInferences — inference.go:1670
bool Checker::hasOverlappingInferences(std::vector<InferenceInfo*>& a,
									   std::vector<InferenceInfo*>& b) {
	for (size_t i = 0; i < a.size(); i++) {
		if (hasInferenceCandidates(a[i]) && hasInferenceCandidates(b[i])) {
			return true;
		}
	}
	return false;
}

// mergeInferences — inference.go:1679
void Checker::mergeInferences(std::vector<InferenceInfo*>& target,
							  const std::vector<InferenceInfo*>& source) {
	for (size_t i = 0; i < target.size(); i++) {
		if (!hasInferenceCandidates(target[i]) && hasInferenceCandidates(source[i])) {
			target[i] = source[i];
		}
	}
}

// inference.go keeps no member of this — the hash is declared inside
// `class Checker` (checker.h) but the definition lives with the slice that
// introduced the caches it serves.
size_t Checker::ReverseMappedTypeKeyHash::operator()(
	const ReverseMappedTypeKey& k) const noexcept {
	uint64_t h = 1469598103934665603ull;
	h = (h ^ static_cast<uint64_t>(k.sourceId)) * 1099511628211ull;
	h = (h ^ static_cast<uint64_t>(k.targetId)) * 1099511628211ull;
	h = (h ^ static_cast<uint64_t>(k.constraintId)) * 1099511628211ull;
	return static_cast<size_t>(h);
}

// ---------------------------------------------------------------------------
// === dep stubs — removed when owner slice lands ===
// ---------------------------------------------------------------------------

// (deduped: inferTypesFromTemplateLiteralType defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: getVariances defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: getAliasVariances defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: isDeeplyNestedType defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: getUnmatchedProperty defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: typePredicateKindsMatch defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: isObjectTypeWithInferableIndex defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: isNonGenericObjectType defined in checker_expressions_c.cpp)

// (deduped: inferTypesFromTemplateLiteralType defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: getVariances defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: getAliasVariances defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: isDeeplyNestedType defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: getUnmatchedProperty defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: typePredicateKindsMatch defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: isObjectTypeWithInferableIndex defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: isNonGenericObjectType defined in cpp/internal/checker/checker_relater.cpp)

}  // namespace checker
}  // namespace tsc
