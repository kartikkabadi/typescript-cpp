// checker_relater.cpp — relater.go "relater" slice.
// The type-relation engine: the Relation/RelationKey machinery and relation
// cache, the compareTypes*/checkType* workers, the isRelatedTo recursive driver
// with its stack/depth bookkeeping, structuredTypeRelatedTo (properties, index
// signatures, call/construct signatures, variances), unionOrIntersectionRelatedTo,
// typeArgumentsRelatedTo, signatureRelatedTo/compareSignatures, type-predicate
// comparisons, mapped-type relation, getVariances/variance measurement, and the
// marker/result types. Ported faithfully from tsc/internal/checker/relater.go in
// file order; cross-slice callees are TSC_UNREACHABLE stubs at the bottom under
// "dep stubs".

#include "internal/ast/ast.h"
#include "internal/binder/binder.h"
#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/checker/types.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/evaluator/evaluator.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tracing/tracing.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace tsc::checker {

// relater_detail — free Go functions in relater.go that share their name with a
// Checker member function live here (call sites are qualified
// relater_detail::name). Populated near the bottom of the file.
namespace relater_detail {}

// Free functions in checker.cpp / checker_decltypes.cpp (extern linkage).

bool isFreshLiteralType(Type* t);
bool maybeTypeOfKind(Type* t, TypeFlags flags);
bool containsType(const std::vector<Type*>& types, Type* t);
bool someType(Type* t, const std::function<bool(Type*)>& f);
bool everyType(Type* t, const std::function<bool(Type*)>& f);
bool everyContainedType(Type* t, const std::function<bool(Type*)>& f);
bool isNumericLiteralName(const std::string& name);
bool isLateBoundName(const std::string& name);              // checker_members.cpp dep stub
bool isStaticPrivateIdentifierProperty(Symbol* s);          // checker_members.cpp dep stub
bool hasType(Node* node);                                   // checker_utilities.cpp
bool isValidNumberString(const std::string& s, bool roundTripOnly); // checker_utilities.cpp
bool isValidBigIntString(const std::string& s, bool roundTripOnly); // checker_utilities.cpp

struct ErrorChain;

// Ternary `&=` — Go `result &= related` accumulation (checker.go uses plain
// int8 arithmetic on Ternary).
inline Ternary& operator&=(Ternary& lhs, Ternary rhs) {
	lhs = static_cast<Ternary>(static_cast<int8_t>(lhs) &
							   static_cast<int8_t>(rhs));
	return lhs;
}
inline Ternary operator&(Ternary lhs, Ternary rhs) {
	return static_cast<Ternary>(static_cast<int8_t>(lhs) &
								static_cast<int8_t>(rhs));
}

namespace members_detail {
bool isConflictingPrivateProperty(Symbol* prop); // checker_members.cpp
}

static Diagnostic* createDiagnosticChainFromErrorChain(
	ErrorChain* chain, Node* errorNode,
	const std::vector<Diagnostic*>& relatedInfo);

// exports.go — free Go function (TSC has no such Checker member).
static bool isDistributedTypeParameter(Type* t) {
	return (t->flags & TypeFlagsTypeParameter) != 0 &&
		t->AsTypeParameter()->isDistributed;
}

namespace {

// File-local replicas (declared file-local in other TUs too — internal linkage).
// utilities.go:1033 — isObjectOrArrayLiteralType
bool isObjectOrArrayLiteralType(Type* t) {
	return (t->objectFlags & (ObjectFlagsObjectLiteral | ObjectFlagsArrayLiteral)) != 0;
}

// checker.go:17982 — isNonDeferredTypeReference
bool isNonDeferredTypeReference(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 && t->AsTypeReference()->node == nullptr;
}

// checker.go:17976 — isTypeReferenceWithGenericArguments
bool isTypeReferenceWithGenericArguments(Type* t) {
	if (!isNonDeferredTypeReference(t)) {
		return false;
	}
	for (Type* u : t->checker->getTypeArguments(t)) {
		if ((u->flags & TypeFlagsTypeParameter) != 0 || isTypeReferenceWithGenericArguments(u)) {
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// keyBuilder (checker.go) — verbatim copy of the per-TU helper in checker.cpp,
// plus writeGenericTypeReferences which only relater.go's getRelationKey uses.
// ---------------------------------------------------------------------------

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
	// checker.go:17774
	bool writeGenericTypeReferences(Type* source, Type* target, bool ignoreConstraints) {
		bool constrained = false;
		std::vector<Type*> typeParameters;
		std::function<void(Type*, int)> writeTypeReference = [&](Type* ref, int depth) {
			writeType(ref->Target());
			for (Type* t : ref->AsTypeReference()->resolvedTypeArguments) {
				if (t->flags & TypeFlagsTypeParameter) {
					if (ignoreConstraints || t->checker->getConstraintOfTypeParameter(t) == nullptr) {
						int index = static_cast<int>(std::find(typeParameters.begin(),
															   typeParameters.end(), t) -
													 typeParameters.begin());
						if (index == static_cast<int>(typeParameters.size())) {
							typeParameters.push_back(t);
						}
						writeByte('=');
						writeInt(index);
						continue;
					}
					constrained = true;
				} else if (depth < 4 && isTypeReferenceWithGenericArguments(t)) {
					writeByte('<');
					writeTypeReference(t, depth + 1);
					writeByte('>');
					continue;
				}
				writeByte('-');
				writeType(t);
			}
		};
		writeTypeReference(source, 0);
		writeByte(',');
		writeTypeReference(target, 0);
		return constrained;
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

// getTypeListKey (checker.go)
CacheKey getTypeListKey(const std::vector<Type*>& types) {
	keyBuilder b;
	b.writeTypes(types);
	return b.hash();
}

// checker.go:17949 — getRelationKey. Returns {key, constrained}.
std::pair<CacheKey, bool> getRelationKey(Type* source, Type* target,
									   IntersectionState intersectionState,
									   bool isIdentity, bool ignoreConstraints) {
	if (isIdentity && source->id > target->id) {
		std::swap(source, target);
	}
	keyBuilder b;
	bool constrained = false;
	if (isTypeReferenceWithGenericArguments(source) &&
		isTypeReferenceWithGenericArguments(target)) {
		b.writeByte('g');
		constrained = b.writeGenericTypeReferences(source, target, ignoreConstraints);
	} else {
		b.writeByte('s');
		b.writeType(source);
		b.writeType(target);
	}
	b.writeUint32(static_cast<uint32_t>(intersectionState));
	return {b.hash(), constrained};
}

// checker.cpp — literal value accessors (file-local copies)

std::string getStringLiteralValue(Type* t) {
	return std::get<std::string>(t->AsLiteralType()->value);
}

Number getNumberLiteralValue(Type* t) {
	return std::get<Number>(t->AsLiteralType()->value);
}

bool isUnitType(Type* t) {
	return (t->flags & TypeFlagsUnit) != 0;
}

// checker.go:25859 — isLiteralType (file-local copy)
bool isLiteralType(Type* t) {
	if ((t->flags & TypeFlagsBoolean) != 0) {
		return true;
	}
	if ((t->flags & TypeFlagsUnion) != 0) {
		if ((t->flags & TypeFlagsEnumLiteral) != 0) {
			return true;
		}
		return everyType(t, isUnitType);
	}
	return isUnitType(t);
}

// checker.cpp:2666 — isTypeUsableAsPropertyName (file-local copy)
bool isTypeUsableAsPropertyName(Type* t) {
	return (t->flags & TypeFlagsStringOrNumberLiteralOrUnique) != 0;
}

// checker.cpp:2696 — Gets the symbolic name for a member from its type.
// (file-local copy)
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

// ---------------------------------------------------------------------------
// core.* / slices.* equivalents used by this file
// ---------------------------------------------------------------------------

template <typename T, typename F>
auto mapVec(const std::vector<T>& values, F f)
	-> std::vector<decltype(f(std::declval<T>()))> {
	std::vector<decltype(f(std::declval<T>()))> result;
	result.reserve(values.size());
	for (const T& v : values) {
		result.push_back(f(v));
	}
	return result;
}

// core.MapIndex — like mapVec but also passes the index.
template <typename T, typename F>
auto mapVecIndex(const std::vector<T>& values, F f)
	-> std::vector<decltype(f(std::declval<T>(), 0))> {
	std::vector<decltype(f(std::declval<T>(), 0))> result;
	result.reserve(values.size());
	for (int i = 0; i < static_cast<int>(values.size()); i++) {
		result.push_back(f(values[i], i));
	}
	return result;
}

template <typename R, typename F>
bool someOf(R&& values, F f) {
	return std::any_of(values.begin(), values.end(), f);
}

template <typename R, typename F>
bool allOf(R&& values, F f) {
	return std::all_of(values.begin(), values.end(), f);
}

// core.Find — first element satisfying f, or T{} (nullptr for pointers).
template <typename T, typename F>
T findPtr(const std::vector<T>& values, F f) {
	auto it = std::find_if(values.begin(), values.end(), f);
	return it == values.end() ? T{} : *it;
}

// core.Find — returns a copy or nullptr for pointer element types.
template <typename T, typename F>
T findOr(std::vector<T> values, F f, T fallback) {
	auto it = std::find_if(values.begin(), values.end(), f);
	return it == values.end() ? fallback : *it;
}

template <typename T, typename F>
int findIndexOf(const std::vector<T>& values, F f) {
	auto it = std::find_if(values.begin(), values.end(), f);
	return it == values.end() ? -1 : static_cast<int>(it - values.begin());
}

template <typename T, typename F>
int countWhere(const std::vector<T>& values, F f) {
	return static_cast<int>(std::count_if(values.begin(), values.end(), f));
}

template <typename T>
void appendIfUnique(std::vector<T>& values, T element) {
	if (std::find(values.begin(), values.end(), element) == values.end()) {
		values.push_back(element);
	}
}

// core.Same — sequence equality.
template <typename T>
bool sameVec(const std::vector<T>& a, const std::vector<T>& b) {
	return a == b;
}

// core.SameMap — like mapVec but returns the original vector when nothing
// changed.
template <typename T, typename F>
std::vector<T> sameMapVec(const std::vector<T>& values, F f) {
	std::vector<T> result;
	result.reserve(values.size());
	bool same = true;
	for (const T& v : values) {
		T r = f(v);
		same = same && r == v;
		result.push_back(r);
	}
	return same ? values : result;
}

// core.AppendIfUnique for pointer vectors (returns true when appended).
template <typename T>
bool appendUniquePtr(std::vector<T*>& values, T* element) {
	if (std::find(values.begin(), values.end(), element) != values.end()) {
		return false;
	}
	values.push_back(element);
	return true;
}

}  // namespace

// RecursionFlags — relater.go:46
using RecursionFlags = uint32_t;
inline constexpr RecursionFlags RecursionFlagsNone = 0;
inline constexpr RecursionFlags RecursionFlagsSource = 1 << 0;
inline constexpr RecursionFlags RecursionFlagsTarget = 1 << 1;
inline constexpr RecursionFlags RecursionFlagsBoth =
	RecursionFlagsSource | RecursionFlagsTarget;

// DiagnosticAndArguments — relater.go:77
struct DiagnosticAndArguments {
	const DiagnosticMessage* message{};
	std::vector<std::string> arguments;
};

// ErrorOutputContainer — relater.go:81. The .errors list is the diagnostic
// accumulator; skipLogging short-circuits appenders.
struct ErrorOutputContainer {
	std::vector<Diagnostic*> errors;
	bool skipLogging{};
};

struct ErrorChain;

// errorState — relater.go:2569
struct errorState {
	ErrorChain* errorChain{};
	std::vector<Diagnostic*> relatedInfo;
};

// ErrorChain — relater.go:2574
struct ErrorChain {
	ErrorChain* next{};
	const DiagnosticMessage* message{};
	std::vector<std::string> args;
};

// Relater — relater.go:2580. Pooled per-comparison worker state (getRelater /
// putRelater on Checker).
struct Relater {
	Checker* c{};
	Relation* relation{};
	Node* errorNode{};
	ErrorChain* errorChain{};
	std::vector<Diagnostic*> relatedInfo;
	std::vector<CacheKey> maybeKeys;
	std::unordered_set<CacheKey, CacheKeyHash> maybeKeysSet;
	std::vector<Type*> sourceStack;
	std::vector<Type*> targetStack;
	int maybeCount{};
	int sourceDepth{};
	int targetDepth{};
	ExpandingFlags expandingFlags{};
	bool overflow{};
	int relationCount{};
	Relater* next{};

	Ternary isRelatedToSimple(Type* source, Type* target);
	Ternary isRelatedToWorker(Type* source, Type* target, bool reportErrors);
	Ternary isRelatedTo(Type* source, Type* target, RecursionFlags recursionFlags,
					  bool reportErrors);
	Ternary isRelatedToEx(Type* originalSource, Type* originalTarget,
						RecursionFlags recursionFlags, bool reportErrors,
						const DiagnosticMessage* headMessage,
						IntersectionState intersectionState);
	bool hasExcessProperties(Type* source, Type* target, bool reportErrors);
	Ternary unionOrIntersectionRelatedTo(Type* source, Type* target, bool reportErrors,
										 IntersectionState intersectionState);
	Ternary someTypeRelatedToType(Type* source, Type* target, bool reportErrors,
								  IntersectionState intersectionState);
	Ternary eachTypeRelatedToType(Type* source, Type* target, bool reportErrors,
								  IntersectionState intersectionState);
	Type* getUndefinedStrippedTargetIfNeeded(Type* source, Type* target);
	Ternary typeRelatedToSomeType(Type* source, Type* target, bool reportErrors,
								  IntersectionState intersectionState);
	Ternary typeRelatedToEachType(Type* source, Type* target, bool reportErrors,
								  IntersectionState intersectionState);
	Ternary eachTypeRelatedToSomeType(Type* source, Type* target);
	Ternary recursiveTypeRelatedTo(Type* source, Type* target, bool reportErrors,
								   IntersectionState intersectionState,
								   RecursionFlags recursionFlags);
	void resetMaybeStack(int maybeStart,
						 RelationComparisonResult propagatingVarianceFlags,
						 bool markAllAsSucceeded);
	errorState getErrorState();
	void restoreErrorState(errorState e);
	Ternary structuredTypeRelatedTo(Type* source, Type* target, bool reportErrors,
									IntersectionState intersectionState);
	bool isSourceIntersectionNeedingExtraCheck(Type* source, Type* target);
	Ternary structuredTypeRelatedToWorker(Type* source, Type* target, bool reportErrors,
										  IntersectionState intersectionState);
	Ternary typeArgumentsRelatedTo(const std::vector<Type*>& sources,
								   const std::vector<Type*>& targets,
								   const std::vector<VarianceFlags>& variances,
								   bool reportErrors, IntersectionState intersectionState);
	Ternary mappedTypeRelatedTo(Type* source, Type* target, bool reportErrors);
	Ternary typeRelatedToDiscriminatedType(Type* source, Type* target);
	Ternary propertiesRelatedTo(Type* source, Type* target, bool reportErrors,
								const std::unordered_set<std::string>& excludedProperties,
								bool optionalsOnly, IntersectionState intersectionState);
	Ternary propertyRelatedTo(Type* source, Type* target, Symbol* sourceProp,
							  Symbol* targetProp,
							  const std::function<Type*(Symbol*)>& getTypeOfSourceProperty,
							  bool reportErrors, IntersectionState intersectionState,
							  bool skipOptional);
	Ternary isPropertySymbolTypeRelated(
		Symbol* sourceProp, Symbol* targetProp,
		const std::function<Type*(Symbol*)>& getTypeOfSourceProperty, bool reportErrors,
		IntersectionState intersectionState);
	void reportUnmatchedProperty(Type* source, Type* target, Symbol* unmatchedProperty,
								 bool requireOptionalProperties);
	bool tryElaborateArrayLikeErrors(Type* source, Type* target, bool reportErrors);
	void tryElaborateErrorsForPrimitivesAndObjects(Type* source, Type* target);
	Ternary propertiesIdenticalTo(Type* source, Type* target,
								  const std::unordered_set<std::string>& excludedProperties);
	Ternary signaturesRelatedTo(Type* source, Type* target, SignatureKind kind,
								bool reportErrors, IntersectionState intersectionState);
	bool constructorVisibilitiesAreCompatible(Signature* sourceSignature,
											  Signature* targetSignature, bool reportErrors);
	Ternary signatureRelatedTo(Signature* source, Signature* target, bool erase,
							   bool reportErrors, IntersectionState intersectionState);
	Ternary signaturesIdenticalTo(Type* source, Type* target, SignatureKind kind);
	Ternary indexSignaturesRelatedTo(Type* source, Type* target, bool sourceIsPrimitive,
									 bool reportErrors, IntersectionState intersectionState);
	Ternary typeRelatedToIndexInfo(Type* source, IndexInfo* targetInfo, bool reportErrors,
								   IntersectionState intersectionState);
	Ternary membersRelatedToIndexInfo(Type* source, IndexInfo* targetInfo, bool reportErrors,
									  IntersectionState intersectionState);
	Ternary indexInfoRelatedTo(IndexInfo* sourceInfo, IndexInfo* targetInfo,
							   bool reportErrors, IntersectionState intersectionState);
	Ternary indexSignaturesIdenticalTo(Type* source, Type* target);
	void reportErrorResults(Type* originalSource, Type* originalTarget, Type* source,
							Type* target, const DiagnosticMessage* headMessage);
	void reportRelationError(const DiagnosticMessage* message, Type* source, Type* target);
	void reportError(const DiagnosticMessage* message,
					 const std::vector<std::string>& args = {});
	const DiagnosticMessage* getChainMessage(int index);
	// Go `chainArgsMatch(args ...any)` — nullopt element = wildcard (Go nil).
	bool chainArgsMatch(
		const std::vector<std::optional<std::string>>& args);
	void traceUnionsOrIntersectionsTooLarge(Type* source, Type* target);
};

// TypeDiscriminator — relater.go:1030. Implements the Discriminator interface
// (types.h) for a list of discriminant properties.
struct TypeDiscriminator : Discriminator {
	Checker* c{};
	std::vector<Symbol*> props;
	std::function<Ternary(Type*, Type*)> isRelatedTo;

	TypeDiscriminator(Checker* c, std::vector<Symbol*> props,
					  const std::function<Ternary(Type*, Type*)>& isRelatedTo)
		: c(c), props(std::move(props)), isRelatedTo(isRelatedTo) {}

	int len() override { return static_cast<int>(props.size()); }
	std::string name(int index) override { return props[index]->name; }
	bool matches(int index, Type* t) override {
		Type* propType = c->getTypeOfSymbol(props[index]);
		for (Type* s : propType->Distributed()) {
			if (isRelatedTo(s, t) != Ternary::False) {
				return true;
			}
		}
		return false;
	}
};

// ---------------------------------------------------------------------------
// relater.go:117 — relation entry points
// ---------------------------------------------------------------------------

bool Checker::isTypeIdenticalTo(Type* source, Type* target) {
	return isTypeRelatedTo(source, target, identityRelation);
}

Ternary Checker::compareTypesIdentical(Type* source, Type* target) {
	if (isTypeRelatedTo(source, target, identityRelation)) {
		return Ternary::True;
	}
	return Ternary::False;
}

Ternary Checker::compareTypesAssignableSimple(Type* source, Type* target) {
	if (isTypeRelatedTo(source, target, assignableRelation)) {
		return Ternary::True;
	}
	return Ternary::False;
}

Ternary Checker::compareTypesAssignableWorker(Type* source, Type* target,
											  bool reportErrors) {
	if (isTypeRelatedTo(source, target, assignableRelation)) {
		return Ternary::True;
	}
	return Ternary::False;
}

Ternary Checker::compareTypesSubtypeOf(Type* source, Type* target) {
	if (isTypeRelatedTo(source, target, subtypeRelation)) {
		return Ternary::True;
	}
	return Ternary::False;
}

bool Checker::isTypeAssignableTo(Type* source, Type* target) {
	return isTypeRelatedTo(source, target, assignableRelation);
}

bool Checker::isTypeSubtypeOf(Type* source, Type* target) {
	return isTypeRelatedTo(source, target, subtypeRelation);
}

bool Checker::isTypeStrictSubtypeOf(Type* source, Type* target) {
	return isTypeRelatedTo(source, target, strictSubtypeRelation);
}

bool Checker::isTypeComparableTo(Type* source, Type* target) {
	return isTypeRelatedTo(source, target, comparableRelation);
}

bool Checker::areTypesComparable(Type* type1, Type* type2) {
	return isTypeComparableTo(type1, type2) || isTypeComparableTo(type2, type1);
}

bool Checker::isTypeRelatedTo(Type* source, Type* target, Relation* relation) {
	if (isFreshLiteralType(source)) {
		source = source->AsLiteralType()->regularType;
	}
	if (isFreshLiteralType(target)) {
		target = target->AsLiteralType()->regularType;
	}
	if (source == target) {
		return true;
	}
	if (relation != identityRelation) {
		if (relation == comparableRelation && !(target->flags & TypeFlagsNever) &&
				isSimpleTypeRelatedTo(target, source, relation, nullptr) ||
			isSimpleTypeRelatedTo(source, target, relation, nullptr)) {
			return true;
		}
	} else if (!((source->flags | target->flags) &
				 (TypeFlagsUnionOrIntersection | TypeFlagsIndexedAccess |
				  TypeFlagsConditional | TypeFlagsSubstitution))) {
		// We have excluded types that may simplify to other forms, so types must
		// have identical flags
		if (source->flags != target->flags) {
			return false;
		}
		if (source->flags & TypeFlagsSingleton) {
			return true;
		}
	}
	if ((source->flags & TypeFlagsObject) && (target->flags & TypeFlagsObject)) {
		auto [id, _] = getRelationKey(source, target, IntersectionStateNone,
									  relation == identityRelation, false);
		RelationComparisonResult related = relation->get(id);
		if (related != RelationComparisonResult::None) {
			return (related & RelationComparisonResult::Succeeded) !=
				   RelationComparisonResult::None;
		}
	}
	if ((source->flags & TypeFlagsStructuredOrInstantiable) ||
		(target->flags & TypeFlagsStructuredOrInstantiable)) {
		return checkTypeRelatedTo(source, target, relation, nullptr /*errorNode*/);
	}
	return false;
}

bool Checker::isSimpleTypeRelatedTo(Type* source, Type* target, Relation* relation,
									ErrorReporter errorReporter) {
	TypeFlags s = source->flags;
	TypeFlags t = target->flags;
	if ((t & TypeFlagsAny) || (s & TypeFlagsNever) || source == wildcardType) {
		return true;
	}
	if ((t & TypeFlagsUnknown) &&
		!(relation == strictSubtypeRelation && (s & TypeFlagsAny))) {
		return true;
	}
	if (t & TypeFlagsNever) {
		return false;
	}
	if ((s & TypeFlagsStringLike) && (t & TypeFlagsString)) {
		return true;
	}
	if ((s & TypeFlagsStringLiteral) && (s & TypeFlagsEnumLiteral) &&
		(t & TypeFlagsStringLiteral) && !(t & TypeFlagsEnumLiteral) &&
		source->AsLiteralType()->value == target->AsLiteralType()->value) {
		return true;
	}
	if ((s & TypeFlagsNumberLike) && (t & TypeFlagsNumber)) {
		return true;
	}
	if ((s & TypeFlagsNumberLiteral) && (s & TypeFlagsEnumLiteral) &&
		(t & TypeFlagsNumberLiteral) && !(t & TypeFlagsEnumLiteral) &&
		source->AsLiteralType()->value == target->AsLiteralType()->value) {
		return true;
	}
	if ((s & TypeFlagsBigIntLike) && (t & TypeFlagsBigInt)) {
		return true;
	}
	if ((s & TypeFlagsBooleanLike) && (t & TypeFlagsBoolean)) {
		return true;
	}
	if ((s & TypeFlagsESSymbolLike) && (t & TypeFlagsESSymbol)) {
		return true;
	}
	if ((s & TypeFlagsEnum) && (t & TypeFlagsEnum) &&
		source->symbol->name == target->symbol->name &&
		isEnumTypeRelatedTo(source->symbol, target->symbol, errorReporter)) {
		return true;
	}
	if ((s & TypeFlagsEnumLiteral) && (t & TypeFlagsEnumLiteral)) {
		if ((s & TypeFlagsUnion) && (t & TypeFlagsUnion) &&
			isEnumTypeRelatedTo(source->symbol, target->symbol, errorReporter)) {
			return true;
		}
		if ((s & TypeFlagsLiteral) && (t & TypeFlagsLiteral) &&
			source->AsLiteralType()->value == target->AsLiteralType()->value &&
			isEnumTypeRelatedTo(source->symbol, target->symbol, errorReporter)) {
			return true;
		}
	}
	// In non-strictNullChecks mode, `undefined` and `null` are assignable to
	// anything except `never`. Since unions and intersections may reduce to
	// `never`, we exclude them here.
	if ((s & TypeFlagsUndefined) &&
		(!strictNullChecks && !(t & TypeFlagsUnionOrIntersection) ||
		 (t & (TypeFlagsUndefined | TypeFlagsVoid)))) {
		return true;
	}
	if ((s & TypeFlagsNull) &&
		(!strictNullChecks && !(t & TypeFlagsUnionOrIntersection) ||
		 (t & TypeFlagsNull))) {
		return true;
	}
	if ((s & TypeFlagsObject) && (t & TypeFlagsNonPrimitive) &&
		!(relation == strictSubtypeRelation && IsEmptyAnonymousObjectType(source) &&
		  !(source->objectFlags & ObjectFlagsFreshLiteral))) {
		return true;
	}
	if (relation == assignableRelation || relation == comparableRelation) {
		if (s & TypeFlagsAny) {
			return true;
		}
		// Type number is assignable to any computed numeric enum type or any
		// numeric enum literal type, and a numeric literal type is assignable any
		// computed numeric enum type or any numeric enum literal type with a
		// matching value. These rules exist such that enums can be used for
		// bit-flag purposes.
		if ((s & TypeFlagsNumber) &&
			((t & TypeFlagsEnum) ||
			 ((t & TypeFlagsNumberLiteral) && (t & TypeFlagsEnumLiteral)))) {
			return true;
		}
		if ((s & TypeFlagsNumberLiteral) && !(s & TypeFlagsEnumLiteral) &&
			((t & TypeFlagsEnum) ||
			 ((t & TypeFlagsNumberLiteral) && (t & TypeFlagsEnumLiteral) &&
			  source->AsLiteralType()->value == target->AsLiteralType()->value))) {
			return true;
		}
		// Anything is assignable to a union containing undefined, null, and {}
		if (isUnknownLikeUnionType(target)) {
			return true;
		}
	}
	return false;
}

bool Checker::isEnumTypeRelatedTo(Symbol* source, Symbol* target,
								  ErrorReporter errorReporter) {
	Symbol* sourceSymbol = (source->flags & SymbolFlagsEnumMember)
							   ? getParentOfSymbol(source)
							   : source;
	Symbol* targetSymbol = (target->flags & SymbolFlagsEnumMember)
							   ? getParentOfSymbol(target)
							   : target;
	if (sourceSymbol == targetSymbol) {
		return true;
	}
	if (sourceSymbol->name != targetSymbol->name ||
		!(sourceSymbol->flags & SymbolFlagsRegularEnum) ||
		!(targetSymbol->flags & SymbolFlagsRegularEnum)) {
		return false;
	}
	EnumRelationKey key{getSymbolId(sourceSymbol), getSymbolId(targetSymbol)};
	// Go `enumRelation[key]` returns zero on miss without inserting; find()
	// matches that (operator[] would insert a phantom None entry per miss).
	RelationComparisonResult entry = RelationComparisonResult::None;
	if (auto eit = enumRelation.find(key); eit != enumRelation.end()) {
		entry = eit->second;
	}
	if (entry != RelationComparisonResult::None &&
		!((entry & RelationComparisonResult::Failed) != RelationComparisonResult::None &&
		  errorReporter)) {
		return (entry & RelationComparisonResult::Succeeded) !=
			   RelationComparisonResult::None;
	}
	Type* targetEnumType = getTypeOfSymbol(targetSymbol);
	for (Symbol* sourceProperty :
		 getPropertiesOfType(getTypeOfSymbol(sourceSymbol))) {
		if (sourceProperty->flags & SymbolFlagsEnumMember) {
			Symbol* targetProperty =
				getPropertyOfType(targetEnumType, sourceProperty->name);
			if (targetProperty == nullptr ||
				!(targetProperty->flags & SymbolFlagsEnumMember)) {
				if (errorReporter) {
					errorReporter(Property_0_is_missing_in_type_1,
								  {symbolToString(sourceProperty),
								   TypeToStringEx(getDeclaredTypeOfSymbol(targetSymbol),
												  nullptr /*enclosingDeclaration*/,
												  TypeFormatFlagsUseFullyQualifiedType,
												  nullptr)});
				}
				enumRelation[key] = RelationComparisonResult::Failed;
				return false;
			}
			auto sourceValue =
				getEnumMemberValue(getDeclarationOfKind(sourceProperty, Kind::EnumMember))
					.Value;
			auto targetValue =
				getEnumMemberValue(getDeclarationOfKind(targetProperty, Kind::EnumMember))
					.Value;
			if (sourceValue != targetValue) {
				// If we have 2 enums with *known* values that differ, they are
				// incompatible.
				if (!std::holds_alternative<std::monostate>(sourceValue) &&
					!std::holds_alternative<std::monostate>(targetValue)) {
					if (errorReporter) {
						errorReporter(
							
								Each_declaration_of_0_1_differs_in_its_value_where_2_was_expected_but_3_was_given,
							{symbolToString(targetSymbol),
							 symbolToString(targetProperty), ValueToString(targetValue),
							 ValueToString(sourceValue)});
					}
					enumRelation[key] = RelationComparisonResult::Failed;
					return false;
				}
				// At this point we know that at least one of the values is
				// 'undefined'. This may mean that we have an opaque member from
				// an ambient enum declaration, or that we were not able to
				// calculate it (which is basically an error).
				//
				// Either way, we can assume that it's numeric.
				// If the other is a string, we have a mismatch in types.
				bool sourceIsString = std::holds_alternative<std::string>(sourceValue);
				bool targetIsString = std::holds_alternative<std::string>(targetValue);
				if (sourceIsString || targetIsString) {
					if (errorReporter) {
						auto knownStringValue =
							!std::holds_alternative<std::monostate>(sourceValue)
								? sourceValue
								: targetValue;
						errorReporter(
							
								One_value_of_0_1_is_the_string_2_and_the_other_is_assumed_to_be_an_unknown_numeric_value,
							{symbolToString(targetSymbol), symbolToString(targetProperty),
							 ValueToString(knownStringValue)});
					}
					enumRelation[key] = RelationComparisonResult::Failed;
					return false;
				}
			}
		}
	}
	enumRelation[key] = RelationComparisonResult::Succeeded;
	return true;
}

bool Checker::checkTypeAssignableTo(Type* source, Type* target, Node* errorNode,
									const DiagnosticMessage* headMessage) {
	return checkTypeRelatedToEx(source, target, assignableRelation, errorNode,
								headMessage, nullptr);
}

bool Checker::checkTypeAssignableToEx(Type* source, Type* target, Node* errorNode,
									  const DiagnosticMessage* headMessage,
									  std::vector<Diagnostic*>* diagnosticOutput) {
	return checkTypeRelatedToEx(source, target, assignableRelation, errorNode,
								headMessage, diagnosticOutput);
}

bool Checker::checkTypeComparableTo(Type* source, Type* target, Node* errorNode,
									const DiagnosticMessage* headMessage) {
	return checkTypeRelatedToEx(source, target, comparableRelation, errorNode,
								headMessage, nullptr);
}

bool Checker::checkTypeRelatedTo(Type* source, Type* target, Relation* relation,
								 Node* errorNode) {
	return checkTypeRelatedToEx(source, target, relation, errorNode, nullptr, nullptr);
}

// Check that source is related to target according to the given relation. When
// errorNode is non-null, errors are reported to the checker's diagnostic
// collection or through diagnosticOutput when non-null. Callers can assume that
// this function only reports zero or one error to diagnosticOutput (unlike
// checkTypeRelatedToAndOptionallyElaborate).
bool Checker::checkTypeRelatedToEx(Type* source, Type* target, Relation* relation,
								   Node* errorNode,
								   const DiagnosticMessage* headMessage,
								   std::vector<Diagnostic*>* diagnosticOutput) {
	Relater* r = getRelater();
	r->relation = relation;
	r->errorNode = errorNode;
	r->relationCount = (16000000 - relation->size()) / 8;
	Ternary result =
		r->isRelatedToEx(source, target, RecursionFlagsBoth, errorNode != nullptr,
						 headMessage, IntersectionStateNone);
	if (r->overflow) {
		// Record this relation as having failed such that we don't attempt the
		// overflowing operation again.
		auto [id, _] = getRelationKey(source, target, IntersectionStateNone,
									  relation == identityRelation,
									  false /*ignoreConstraints*/);
		relation->set(id, RelationComparisonResult::Failed |
							RelationComparisonResult::ComplexityOverflow);
		if (Tracer* tr = tracer; tr != nullptr) {
			tr->Instant(tracing::PhaseCheckTypes, "checkTypeRelatedTo_DepthLimit",
						{{"sourceId", source->id},
						 {"targetId", target->id},
						 {"depth", static_cast<int>(r->sourceStack.size())},
						 {"targetDepth", static_cast<int>(r->targetStack.size())}});
		}
		if (errorNode == nullptr) {
			errorNode = currentNode;
		}
		reportDiagnostic(
			NewDiagnosticForNode(errorNode,
								 
									 Excessive_complexity_comparing_types_0_and_1,
								 {TypeToString(source), TypeToString(target)}),
			diagnosticOutput);
	} else if (r->errorChain != nullptr) {
		// Check if we should issue an extra diagnostic to produce a quickfix for
		// a slightly incorrect import statement
		if (headMessage != nullptr && errorNode != nullptr && result == Ternary::False &&
			source->symbol != nullptr && exportTypeLinks.Has(source->symbol)) {
			ExportTypeLinks* links = exportTypeLinks.Get(source->symbol);
			if (links->originatingImport != nullptr &&
				!isImportCall(links->originatingImport)) {
				bool helpfulRetry =
					checkTypeRelatedTo(getTypeOfSymbol(links->target), target, relation,
									   nullptr /*errorNode*/);
				if (helpfulRetry) {
					// Likely an incorrect import. Issue a helpful diagnostic to
					// produce a quickfix to change the import
					r->relatedInfo.push_back(createDiagnosticForNode(
						links->originatingImport,
						
							Type_originates_at_this_import_A_namespace_style_import_cannot_be_called_or_constructed_and_will_cause_a_failure_at_runtime_Consider_using_a_default_import_or_import_require_here_instead,
						{}));
				}
			}
		}
		reportDiagnostic(
			createDiagnosticChainFromErrorChain(r->errorChain, r->errorNode,
												r->relatedInfo),
			diagnosticOutput);
	}
	putRelater(r);
	return result != Ternary::False;
}

// relater.go:400
static Diagnostic* createDiagnosticChainFromErrorChain(
	ErrorChain* chain, Node* errorNode,
	const std::vector<Diagnostic*>& relatedInfo) {
	while (chain != nullptr && chain->message->ElidedInCompatibilityPyramid()) {
		chain = chain->next;
	}
	if (chain == nullptr) {
		return nullptr;
	}
	Diagnostic* next =
		createDiagnosticChainFromErrorChain(chain->next, errorNode, relatedInfo);
	if (next == nullptr) {
		return NewDiagnosticForNode(errorNode, chain->message, chain->args)
			->SetRelatedInfo(relatedInfo);
	}
	return newDiagnosticChain(next, chain->message, chain->args);
}

void Checker::reportDiagnostic(Diagnostic* diagnostic,
							   std::vector<Diagnostic*>* diagnosticOutput) {
	if (diagnostic != nullptr) {
		if (diagnosticOutput != nullptr) {
			diagnosticOutput->push_back(diagnostic);
		} else {
			addDiagnostic(diagnostic);
		}
	}
}

bool Checker::checkTypeAssignableToAndOptionallyElaborate(
	Type* source, Type* target, Node* errorNode, Node* expr,
	const DiagnosticMessage* headMessage,
	std::vector<Diagnostic*>* diagnosticOutput) {
	return checkTypeRelatedToAndOptionallyElaborate(source, target, assignableRelation,
												  errorNode, expr, headMessage,
												  diagnosticOutput);
}

bool Checker::checkTypeRelatedToAndOptionallyElaborate(
	Type* source, Type* target, Relation* relation, Node* errorNode, Node* expr,
	const DiagnosticMessage* headMessage,
	std::vector<Diagnostic*>* diagnosticOutput) {
	if (isTypeRelatedTo(source, target, relation)) {
		return true;
	}
	if (errorNode != nullptr &&
		!elaborateError(expr, source, target, relation, headMessage, diagnosticOutput)) {
		return checkTypeRelatedToEx(source, target, relation, errorNode, headMessage,
									diagnosticOutput);
	}
	return false;
}

bool Checker::elaborateError(Node* node, Type* source, Type* target, Relation* relation,
							 const DiagnosticMessage* headMessage,
							 std::vector<Diagnostic*>* diagnosticOutput) {
	if (node == nullptr || isOrHasGenericConditional(target)) {
		return false;
	}
	if (compilerOptions->NoCheck == Tristate::True) {
		return false;
	}
	if (elaborateDidYouMeanToCallOrConstruct(node, source, target, relation,
										   SignatureKind::Construct, headMessage,
										   diagnosticOutput) ||
		elaborateDidYouMeanToCallOrConstruct(node, source, target, relation,
										   SignatureKind::Call, headMessage,
										   diagnosticOutput)) {
		return true;
	}
	switch (node->kind) {
	case Kind::AsExpression:
		if (!isConstAssertion(node)) {
			break;
		}
		[[fallthrough]];
	case Kind::JsxExpression:
	case Kind::ParenthesizedExpression:
		return elaborateError(node->expression(), source, target, relation, headMessage,
							  diagnosticOutput);
	case Kind::BinaryExpression:
		switch (node->as<BinaryExpression>()->OperatorToken->kind) {
		case Kind::EqualsToken:
		case Kind::CommaToken:
			return elaborateError(node->as<BinaryExpression>()->Right, source, target,
								  relation, headMessage, diagnosticOutput);
		default:
			break;
		}
		break;
	case Kind::ObjectLiteralExpression:
		return elaborateObjectLiteral(node, source, target, relation, diagnosticOutput);
	case Kind::ArrayLiteralExpression:
		return elaborateArrayLiteral(node, source, target, relation, diagnosticOutput);
	case Kind::ArrowFunction:
		return elaborateArrowFunction(node, source, target, relation, diagnosticOutput);
	case Kind::JsxAttributes:
		return elaborateJsxComponents(node, source, target, relation, diagnosticOutput);
	default:
		break;
	}
	return false;
}

bool Checker::isOrHasGenericConditional(Type* t) {
	return (t->flags & TypeFlagsConditional) ||
		   ((t->flags & TypeFlagsIntersection) &&
			someOf(t->types(), [this](Type* u) { return isOrHasGenericConditional(u); }));
}

bool Checker::elaborateDidYouMeanToCallOrConstruct(
	Node* node, Type* source, Type* target, Relation* relation, SignatureKind kind,
	const DiagnosticMessage* headMessage,
	std::vector<Diagnostic*>* diagnosticOutput) {
	if (someOf(getSignaturesOfType(source, kind), [&](Signature* s) {
			Type* returnType = getReturnTypeOfSignature(s);
			return !(returnType->flags & (TypeFlagsAny | TypeFlagsNever)) &&
				   checkTypeRelatedTo(returnType, target, relation, nullptr /*errorNode*/);
		})) {
		std::vector<Diagnostic*> diags;
		if (!checkTypeRelatedToEx(source, target, relation, node, headMessage, &diags)) {
			Diagnostic* diagnostic = diags[0];
			const DiagnosticMessage* message =
				kind == SignatureKind::Construct
					? Did_you_mean_to_use_new_with_this_expression
					: Did_you_mean_to_call_this_expression;
			reportDiagnostic(
				diagnostic->AddRelatedInfo(createDiagnosticForNode(node, message, {})),
				diagnosticOutput);
			return true;
		}
	}
	return false;
}

bool Checker::elaborateObjectLiteral(Node* node, Type* source, Type* target,
									 Relation* relation,
									 std::vector<Diagnostic*>* diagnosticOutput) {
	if (target->flags & (TypeFlagsPrimitive | TypeFlagsNever)) {
		return false;
	}
	bool reportedError = false;
	for (Node* prop : node->properties()) {
		if (isSpreadAssignment(prop)) {
			continue;
		}
		Type* nameType = getLiteralTypeFromProperty(
			getSymbolOfDeclaration(prop), TypeFlagsStringOrNumberLiteralOrUnique, false);
		if (nameType == nullptr || (nameType->flags & TypeFlagsNever)) {
			continue;
		}
		switch (prop->kind) {
		case Kind::SetAccessor:
		case Kind::GetAccessor:
		case Kind::MethodDeclaration:
		case Kind::ShorthandPropertyAssignment:
			reportedError = elaborateElement(source, target, relation, prop->name(),
										   nullptr, nameType, nullptr, nullptr,
										   diagnosticOutput) ||
							reportedError;
			break;
		case Kind::PropertyAssignment: {
			const DiagnosticMessage* message =
				isComputedNonLiteralName(prop->name())
					? 
						  Type_of_computed_property_s_value_is_0_which_is_not_assignable_to_type_1
					: nullptr;
			reportedError =
				elaborateElement(source, target, relation, prop->name(),
								 prop->as<PropertyAssignment>()->Initializer, nameType,
								 message, nullptr, diagnosticOutput) ||
				reportedError;
			break;
		}
		default:
			break;
		}
	}
	return reportedError;
}

bool Checker::elaborateArrayLiteral(Node* node, Type* source, Type* target,
									Relation* relation,
									std::vector<Diagnostic*>* diagnosticOutput) {
	if (target->flags & (TypeFlagsPrimitive | TypeFlagsNever)) {
		return false;
	}
	if (!isTupleLikeType(source)) {
		pushContextualType(node, target, false /*isCache*/);
		source = checkArrayLiteral(node, CheckModeContextual | CheckModeForceTuple);
		popContextualType();
		if (!isTupleLikeType(source)) {
			return false;
		}
	}
	bool reportedError = false;
	int i = 0;
	for (Node* element : node->elements()) {
		if (isOmittedExpression(element) ||
			(isTupleLikeType(target) &&
			 getPropertyOfType(target, Number(i).string()) == nullptr)) {
			i++;
			continue;
		}
		Type* nameType = getNumberLiteralType(Number(i));
		Node* checkNode = getEffectiveCheckNode(element);
		reportedError = elaborateElement(source, target, relation, checkNode, checkNode,
									   nameType, nullptr, nullptr,
									   diagnosticOutput) ||
						reportedError;
		i++;
	}
	return reportedError;
}

bool Checker::elaborateElement(
	Type* source, Type* target, Relation* relation, Node* prop, Node* next,
	Type* nameType, const DiagnosticMessage* errorMessage,
	std::function<Diagnostic*(Node*)> diagnosticFactory,
	std::vector<Diagnostic*>* diagnosticOutput) {
	Type* targetPropType =
		getBestMatchIndexedAccessTypeOrUndefined(source, target, nameType);
	if (targetPropType == nullptr || (targetPropType->flags & TypeFlagsIndexedAccess)) {
		// Don't elaborate on indexes on generic variables
		return false;
	}
	Type* sourcePropType = getIndexedAccessTypeOrUndefined(
		source, nameType, AccessFlagsNone, nullptr, nullptr);
	if (sourcePropType == nullptr ||
		checkTypeRelatedTo(sourcePropType, targetPropType, relation,
						   nullptr /*errorNode*/)) {
		// Don't elaborate on indexes on generic variables or when types match
		return false;
	}
	if (next != nullptr && elaborateError(next, sourcePropType, targetPropType, relation,
										  nullptr /*headMessage*/, diagnosticOutput)) {
		return true;
	}
	// Issue error on the prop itself, since the prop couldn't elaborate the error
	std::vector<Diagnostic*> diags;
	// Use the expression type, if available
	Type* specificSource = sourcePropType;
	if (next != nullptr) {
		specificSource =
			checkExpressionForMutableLocationWithContextualType(next, sourcePropType);
	}
	if (diagnosticFactory != nullptr) {
		// Use the custom diagnostic factory if provided (e.g., for JSX text
		// children with dynamic error messages)
		diags.push_back(diagnosticFactory(prop));
	} else if (exactOptionalPropertyTypes &&
			   isExactOptionalPropertyMismatch(specificSource, targetPropType)) {
		diags.push_back(createDiagnosticForNode(
			prop,
			
				Type_0_is_not_assignable_to_type_1_with_exactOptionalPropertyTypes_Colon_true_Consider_adding_undefined_to_the_type_of_the_target,
			{TypeToString(specificSource), TypeToString(targetPropType)}));
	} else {
		std::string propName = getPropertyNameFromIndex(nameType, nullptr /*accessNode*/);
		bool targetIsOptional =
			(getPropertyOfType(target, propName) != nullptr
				 ? getPropertyOfType(target, propName)
				 : unknownSymbol)
					->flags &
			SymbolFlagsOptional;
		bool sourceIsOptional =
			(getPropertyOfType(source, propName) != nullptr
				 ? getPropertyOfType(source, propName)
				 : unknownSymbol)
					->flags &
			SymbolFlagsOptional;
		targetPropType = removeMissingType(targetPropType, targetIsOptional);
		sourcePropType =
			removeMissingType(sourcePropType, targetIsOptional && sourceIsOptional);
		bool result = checkTypeRelatedToEx(specificSource, targetPropType, relation, prop,
										   errorMessage, &diags);
		if (result && specificSource != sourcePropType) {
			// If for whatever reason the expression type doesn't yield an error,
			// make sure we still issue an error on the sourcePropType
			checkTypeRelatedToEx(sourcePropType, targetPropType, relation, prop,
								 errorMessage, &diags);
		}
	}
	if (diags.empty()) {
		return false;
	}
	Diagnostic* diagnostic = diags[0];
	std::string propertyName;
	Symbol* targetProp = nullptr;
	if (isTypeUsableAsPropertyName(nameType)) {
		propertyName = getPropertyNameFromType(nameType);
		targetProp = getPropertyOfType(target, propertyName);
	}
	bool issuedElaboration = false;
	if (targetProp == nullptr) {
		IndexInfo* indexInfo = getApplicableIndexInfo(target, nameType);
		if (indexInfo != nullptr && indexInfo->declaration != nullptr &&
			!program->IsSourceFileDefaultLibrary(
				getSourceFileOfNode(indexInfo->declaration)->Path())) {
			issuedElaboration = true;
			diagnostic->AddRelatedInfo(createDiagnosticForNode(
				indexInfo->declaration,
				The_expected_type_comes_from_this_index_signature, {}));
		}
	}
	if (!issuedElaboration &&
		((targetProp != nullptr && !targetProp->declarations.empty()) ||
		 (target->symbol != nullptr && !target->symbol->declarations.empty()))) {
		Node* targetNode = nullptr;
		if (targetProp != nullptr && !targetProp->declarations.empty()) {
			targetNode = targetProp->declarations[0];
		} else {
			targetNode = target->symbol->declarations[0];
		}
		if (propertyName.empty() || (nameType->flags & TypeFlagsUniqueESSymbol)) {
			propertyName = TypeToString(nameType);
		}
		if (!program->IsSourceFileDefaultLibrary(
				getSourceFileOfNode(targetNode)->Path())) {
			diagnostic->AddRelatedInfo(createDiagnosticForNode(
				targetNode,
				
					The_expected_type_comes_from_property_0_which_is_declared_here_on_type_1,
				{propertyName, TypeToString(target)}));
		}
	}
	reportDiagnostic(diagnostic, diagnosticOutput);
	return true;
}

Type* Checker::getBestMatchIndexedAccessTypeOrUndefined(Type* source, Type* target,
														Type* nameType) {
	Type* idx = getIndexedAccessTypeOrUndefined(target, nameType, AccessFlagsNone,
											  nullptr, nullptr);
	if (idx != nullptr) {
		return idx;
	}
	if (target->flags & TypeFlagsUnion) {
		Type* best = getBestMatchingType(
			source, target,
			[this](Type* s, Type* t) { return compareTypesAssignableSimple(s, t); });
		if (best != nullptr) {
			return getIndexedAccessTypeOrUndefined(best, nameType, AccessFlagsNone,
												   nullptr, nullptr);
		}
	}
	return nullptr;
}

Type* Checker::checkExpressionForMutableLocationWithContextualType(
	Node* next, Type* sourcePropType) {
	pushContextualType(next, sourcePropType, false /*isCache*/);
	Type* result = checkExpressionForMutableLocation(next, CheckModeContextual);
	popContextualType();
	return result;
}

bool Checker::elaborateArrowFunction(Node* node, Type* source, Type* target,
									 Relation* relation,
									 std::vector<Diagnostic*>* diagnosticOutput) {
	// Don't elaborate blocks or functions with annotated parameter types
	if (isBlock(node->body()) ||
		someOf(node->parameters(), [](Node* p) { return hasType(p); })) {
		return false;
	}
	Signature* sourceSig = getSingleCallSignature(source);
	if (sourceSig == nullptr) {
		return false;
	}
	std::vector<Signature*> targetSignatures =
		getSignaturesOfType(target, SignatureKind::Call);
	if (targetSignatures.empty()) {
		return false;
	}
	Node* returnExpression = node->body();
	Type* sourceReturn = getReturnTypeOfSignature(sourceSig);
	Type* targetReturn = getUnionType(mapVec(targetSignatures, [this](Signature* s) {
		return getReturnTypeOfSignature(s);
	}));
	if (checkTypeRelatedTo(sourceReturn, targetReturn, relation,
						   nullptr /*errorNode*/)) {
		return false;
	}
	if (returnExpression != nullptr &&
		elaborateError(returnExpression, sourceReturn, targetReturn, relation,
					   nullptr /*headMessage*/, diagnosticOutput)) {
		return true;
	}
	std::vector<Diagnostic*> diags;
	checkTypeRelatedToEx(sourceReturn, targetReturn, relation, returnExpression,
						 nullptr /*headMessage*/, &diags);
	if (!diags.empty()) {
		Diagnostic* diagnostic = diags[0];
		if (target->symbol != nullptr && !target->symbol->declarations.empty()) {
			diagnostic->AddRelatedInfo(createDiagnosticForNode(
				target->symbol->declarations[0],
				
					The_expected_type_comes_from_the_return_type_of_this_signature,
				{}));
		}
		if (!(getFunctionFlags(node) & FunctionFlagsAsync) &&
			getTypeOfPropertyOfType(sourceReturn, "then") == nullptr &&
			checkTypeRelatedTo(createPromiseType(sourceReturn), targetReturn, relation,
							   nullptr /*errorNode*/)) {
			diagnostic->AddRelatedInfo(createDiagnosticForNode(
				node, Did_you_mean_to_mark_this_function_as_async, {}));
		}
		reportDiagnostic(diagnostic, diagnosticOutput);
		return true;
	}
	return false;
}

// A type is 'weak' if it is an object type with at least one optional property
// and no required properties, call/construct signatures or index signatures
bool Checker::isWeakType(Type* t) {
	if (t->flags & TypeFlagsObject) {
		StructuredType* resolved = resolveStructuredTypeMembers(t);
		return resolved->signatures.empty() && resolved->indexInfos.empty() &&
			   !resolved->properties.empty() &&
			   allOf(resolved->properties,
					 [](Symbol* p) { return (p->flags & SymbolFlagsOptional) != 0; });
	}
	if (t->flags & TypeFlagsSubstitution) {
		return isWeakType(t->AsSubstitutionType()->baseType);
	}
	if (t->flags & TypeFlagsIntersection) {
		return allOf(t->types(), [this](Type* u) { return isWeakType(u); });
	}
	return false;
}

bool Checker::hasCommonProperties(Type* source, Type* target,
								  bool isComparingJsxAttributes) {
	for (Symbol* prop : getPropertiesOfType(source)) {
		if (isKnownProperty(target, prop->name, isComparingJsxAttributes)) {
			return true;
		}
	}
	return false;
}

static bool isHyphenatedJsxName(const std::string& name);
static bool isExcessPropertyCheckTarget(Type* t);

// Check if a property with the given name is known anywhere in the given type.
// In an object type, a property is considered known if
// 1. the object type is empty and the check is for assignability, or
// 2. if the object type has index signatures, or
// 3. if the property is actually declared in the object type
//    (this means that 'toString', for example, is not usually a known property).
// 4. In a union or intersection type,
//    a property is considered known if it is known in any constituent type.
bool Checker::isKnownProperty(Type* targetType, const std::string& name,
							  bool isComparingJsxAttributes) {
	if (targetType->flags & TypeFlagsObject) {
		// For backwards compatibility a symbol-named property is satisfied by a
		// string index signature. This is incorrect and inconsistent with element
		// access expressions, where it is an error, so eventually we should
		// remove this exception.
		if (getPropertyOfObjectType(targetType, name) != nullptr ||
			getApplicableIndexInfoForName(targetType, name) != nullptr ||
			(isLateBoundName(name) &&
			 getIndexInfoOfType(targetType, stringType) != nullptr) ||
			(isComparingJsxAttributes && isHyphenatedJsxName(name))) {
			// For JSXAttributes, if the attribute has a hyphenated name, consider
			// that the attribute to be known.
			return true;
		}
	}
	if (targetType->flags & TypeFlagsSubstitution) {
		return isKnownProperty(targetType->AsSubstitutionType()->baseType, name,
							   isComparingJsxAttributes);
	}
	if ((targetType->flags & TypeFlagsUnionOrIntersection) &&
		isExcessPropertyCheckTarget(targetType)) {
		for (Type* t : targetType->types()) {
			if (isKnownProperty(t, name, isComparingJsxAttributes)) {
				return true;
			}
		}
	}
	return false;
}

// relater.go:743
static bool isHyphenatedJsxName(const std::string& name) {
	return name.find('-') != std::string::npos;
}

// relater.go:747
static bool isExcessPropertyCheckTarget(Type* t) {
	return ((t->flags & TypeFlagsObject) &&
			!(t->objectFlags & ObjectFlagsObjectLiteralPatternWithComputedProperties)) ||
		   (t->flags & TypeFlagsNonPrimitive) ||
		   ((t->flags & TypeFlagsSubstitution) &&
			isExcessPropertyCheckTarget(t->AsSubstitutionType()->baseType)) ||
		   ((t->flags & TypeFlagsUnion) &&
			someOf(t->types(), isExcessPropertyCheckTarget)) ||
		   ((t->flags & TypeFlagsIntersection) &&
			allOf(t->types(), isExcessPropertyCheckTarget));
}

// Return true if the given type is deeply nested. We consider this to be the
// case when the given stack contains maxDepth or more occurrences of types with
// the same recursion identity as the given type. The recursion identity provides
// a shared identity for type instantiations that repeat in some (possibly
// infinite) pattern. For example, in `type Deep<T> = { next: Deep<Deep<T>> }`,
// repeatedly referencing the `next` property leads to an infinite sequence of
// ever deeper instantiations with the same recursion identity (in this case the
// symbol associated with the object type literal).
// A homomorphic mapped type is considered deeply nested if its target type is
// deeply nested, and an intersection is considered deeply nested if any
// constituent of the intersection is deeply nested.
namespace {

// ---------------------------------------------------------------------------
// RecursionId helpers — copies of the file-local helpers in
// checker_typeops.cpp (relater.go:797-870). RecursionId is declared in
// checker.h.
// ---------------------------------------------------------------------------

// checker.go:23946 isTupleType (free function) — the extern-visible def is a
// dep stub in checker_members.cpp, so we keep a TU-local copy like the other
// slices do.
bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) &&
	       (t->AsTypeReference()->target->objectFlags & ObjectFlagsTuple);
}

// checker.go:17358 — signatureHasRestParameter (free function)
bool signatureHasRestParameter(Signature* sig) {
	return (sig->flags & SignatureFlagsHasRestParameter) != 0;
}

// utilities.go:272 — hasDotDotDotToken
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

RecursionId asRecursionId(Node* value) {
	return {reinterpret_cast<uintptr_t>(value)};
}
RecursionId asRecursionId(Symbol* value) {
	return {reinterpret_cast<uintptr_t>(value)};
}
RecursionId asRecursionId(Type* value) {
	return {reinterpret_cast<uintptr_t>(value)};
}

// relater.go:820 — Get the recursion identity target type from a type.
// Recursively (a) obtain the target object type of an indexed access (i.e. the
// T in T[K]), and (b) unwrap nested homomorphic mapped types and return the
// deepest target type that has a symbol. The unwrapping better preserves unique
// type identities for mapped types applied to explicitly written object
// literals. For example in `Mapped<{ x: Mapped<{ x: Mapped<{ x: string }>}>}>`,
// each of the mapped type applications will have a unique recursion identity
// (that of their target object type literal) and thus avoid appearing deeply
// nested.
Type* getRecursionIdentityTarget(Type* t, Checker* c) {
	if (t->flags & TypeFlagsIndexedAccess) {
		return getRecursionIdentityTarget(t->AsIndexedAccessType()->objectType, c);
	}
	if ((t->objectFlags & ObjectFlagsInstantiatedMapped) ==
		ObjectFlagsInstantiatedMapped) {
		Type* target = c->getModifiersTypeFromMappedType(t);
		if (target != nullptr &&
			(target->symbol != nullptr ||
			 ((target->flags & TypeFlagsIntersection) != 0 &&
			  std::any_of(target->types().begin(), target->types().end(),
						  [](Type* t) { return t->symbol != nullptr; })))) {
			return getRecursionIdentityTarget(target, c);
		}
	}
	return t;
}

// relater.go:840 — The recursion identity of a type is an object identity that
// is shared among multiple instantiations of the type. We track recursion
// identities in order to identify deeply nested and possibly infinite type
// instantiations with the same origin. For example, when type parameters are in
// scope in an object type such as { x: T }, all instantiations of that type
// have the same recursion identity. The default recursion identity is the
// object identity of the type, meaning that every type is unique. Generally,
// types with constituents that could circularly reference the type have a
// recursion identity that differs from the object identity.
RecursionId getRecursionIdentityFromTarget(Type* t, Checker* c) {
	// Object and array literals are known not to contain recursive references
	// and don't need a recursion identity.
	if ((t->flags & TypeFlagsObject) != 0 && !isObjectOrArrayLiteralType(t)) {
		if ((t->objectFlags & ObjectFlagsReference) != 0 &&
			t->AsTypeReference()->node != nullptr) {
			// Deferred type references are tracked through their associated AST
			// node. This gives us finer granularity than using their associated
			// target because each manifest type reference has a unique AST node.
			return asRecursionId(t->AsTypeReference()->node);
		}
		if (t->symbol != nullptr &&
			!((t->objectFlags & ObjectFlagsAnonymous) != 0 &&
			  (t->symbol->flags & SymbolFlagsClass) != 0) &&
			(t->objectFlags & ObjectFlagsFromTypeNode) == 0) {
			// We track object types that have a symbol by that symbol
			// (representing the origin of the type), but exclude the static
			// sides of classes (since they share their symbols with the
			// instance sides) and type references that originate in resolution
			// of AST type nodes (since such type nodes cannot be the source of
			// generative recursion without first being instantiated).
			return asRecursionId(t->symbol);
		}
		if (isTupleType(t) && (t->objectFlags & ObjectFlagsFromTypeNode) == 0) {
			return asRecursionId(t->Target());
		}
	}
	if ((t->flags & TypeFlagsTypeParameter) != 0 && t->symbol != nullptr) {
		// We use the symbol of the type parameter such that all "fresh"
		// instantiations of that type parameter have the same recursion
		// identity.
		return asRecursionId(t->symbol);
	}
	if ((t->flags & TypeFlagsConditional) != 0) {
		// The root object represents the origin of the conditional type
		return asRecursionId(t->AsConditionalType()->root->node);
	}
	return asRecursionId(t);
}

// relater.go:810
RecursionId getRecursionIdentity(Type* t, Checker* c) {
	return getRecursionIdentityFromTarget(getRecursionIdentityTarget(t, c), c);
}

// relater.go:797
bool hasMatchingRecursionIdentity(Type* t, RecursionId identity) {
	Type* target = getRecursionIdentityTarget(t, t->checker);
	if ((target->flags & TypeFlagsIntersection) != 0) {
		for (Type* u : target->types()) {
			if (hasMatchingRecursionIdentity(u, identity)) {
				return true;
			}
		}
		return false;
	}
	return getRecursionIdentityFromTarget(target, t->checker) == identity;
}

// relater.go:1148
bool isObjectOrInstantiableNonPrimitive(Type* t) {
	return (t->flags & (TypeFlagsObject | TypeFlagsInstantiableNonPrimitive)) != 0;
}

// relater.go:1266
bool isNonPrimitiveType(Type* t) {
	return (t->flags & TypeFlagsPrimitive) == 0;
}

// relater.go:2559
std::string visibilityToString(ModifierFlags flags) {
	if (flags == ModifierFlagsPrivate) {
		return "private";
	}
	if (flags == ModifierFlagsProtected) {
		return "protected";
	}
	return "public";
}

}  // namespace

// relater.go:1008
static std::vector<Symbol*> excludeProperties(
	const std::vector<Symbol*>& properties,
	const std::unordered_set<std::string>& excludedProperties) {
	if (excludedProperties.empty() || properties.empty()) {
		return properties;
	}
	std::vector<Symbol*> reduced;
	bool excluded = false;
	for (size_t i = 0; i < properties.size(); i++) {
		Symbol* prop = properties[i];
		if (!excludedProperties.count(prop->name)) {
			if (excluded) {
				reduced.push_back(prop);
			}
		} else if (!excluded) {
			reduced.assign(properties.begin(), properties.begin() + i);
			excluded = true;
		}
	}
	if (excluded) {
		return reduced;
	}
	return properties;
}

// Return true if the given type is deeply nested. We consider this to be the
// case when the given stack contains maxDepth or more occurrences of types with
// the same recursion identity as the given type. The recursion identity provides
// a shared identity for type instantiations that repeat in some (possibly
// infinite) pattern. For example, in `type Deep<T> = { next: Deep<Deep<T>> }`,
// repeatedly referencing the `next` property leads to an infinite sequence of
// ever deeper instantiations with the same recursion identity (in this case the
// symbol associated with the object type literal).
// A homomorphic mapped type is considered deeply nested if its target type is
// deeply nested, and an intersection is considered deeply nested if any
// constituent of the intersection is deeply nested.
// It is possible, though highly unlikely, for the deeply nested check to be true
// in a situation where a chain of instantiations is not infinitely expanding.
// Effectively, we will generate a false positive when two types are structurally
// equal to at least maxDepth levels, but unequal at some level beyond that.
bool Checker::isDeeplyNestedType(Type* t, const std::vector<Type*>& stack,
								 int maxDepth) {
	if (static_cast<int>(stack.size()) >= maxDepth) {
		Type* target = getRecursionIdentityTarget(t, this);
		if (target->flags & TypeFlagsIntersection) {
			for (Type* u : target->types()) {
				if (isDeeplyNestedType(u, stack, maxDepth)) {
					return true;
				}
			}
		} else {
			RecursionId identity =
				getRecursionIdentityFromTarget(target, this);
			int count = 0;
			TypeId lastTypeId = 0;
			for (Type* u : stack) {
				if (hasMatchingRecursionIdentity(u, identity)) {
					// We only count occurrences with a higher type id than the
					// previous occurrence, since higher type ids are an
					// indicator of newer instantiations caused by recursion.
					if (u->id >= lastTypeId) {
						count++;
						if (count >= maxDepth) {
							return true;
						}
					}
					lastTypeId = u->id;
				}
			}
		}
	}
	return false;
}

// relater.go:872
Type* Checker::getBestMatchingType(Type* source, Type* target,
								   const std::function<Ternary(Type*, Type*)>& isRelatedTo) {
	if (Type* t = findMatchingDiscriminantType(source, target, isRelatedTo); t != nullptr) {
		return t;
	}
	if (Type* t = findMatchingTypeReferenceOrTypeAliasReference(source, target); t != nullptr) {
		return t;
	}
	if (Type* t = findBestTypeForObjectLiteral(source, target); t != nullptr) {
		return t;
	}
	if (Type* t = findBestTypeForInvokable(source, target, SignatureKind::Call); t != nullptr) {
		return t;
	}
	if (Type* t = findBestTypeForInvokable(source, target, SignatureKind::Construct); t != nullptr) {
		return t;
	}
	return findMostOverlappyType(source, target);
}

// relater.go:891
Type* Checker::findMatchingTypeReferenceOrTypeAliasReference(Type* source,
															 Type* unionTarget) {
	ObjectFlags sourceObjectFlags = source->objectFlags;
	if ((sourceObjectFlags & (ObjectFlagsReference | ObjectFlagsAnonymous)) != 0 &&
		(unionTarget->flags & TypeFlagsUnion) != 0) {
		for (Type* target : unionTarget->types()) {
			if (target->flags & TypeFlagsObject) {
				ObjectFlags overlapObjFlags = sourceObjectFlags & target->objectFlags;
				if ((overlapObjFlags & ObjectFlagsReference) != 0 &&
					source->Target() == target->Target()) {
					return target;
				}
				if ((overlapObjFlags & ObjectFlagsAnonymous) != 0 &&
					source->alias != nullptr && target->alias != nullptr &&
					source->alias->symbol == target->alias->symbol) {
					return target;
				}
			}
		}
	}
	return nullptr;
}

// relater.go:909
Type* Checker::findBestTypeForInvokable(Type* source, Type* unionTarget,
										SignatureKind kind) {
	if (!getSignaturesOfType(source, kind).empty()) {
		return findPtr(unionTarget->types(), [this, kind](Type* t) {
			return !getSignaturesOfType(t, kind).empty();
		});
	}
	return nullptr;
}

// relater.go:916
Type* Checker::findMostOverlappyType(Type* source, Type* unionTarget) {
	Type* bestMatch = nullptr;
	if ((source->flags & (TypeFlagsPrimitive | TypeFlagsInstantiablePrimitive)) == 0) {
		int matchingCount = 0;
		for (Type* target : unionTarget->types()) {
			if ((target->flags &
				 (TypeFlagsPrimitive | TypeFlagsInstantiablePrimitive)) == 0) {
				Type* overlap = getIntersectionType({getIndexType(source), getIndexType(target)});
				if (overlap->flags & TypeFlagsIndex) {
					// perfect overlap of keys
					return target;
				} else if (isUnitType(overlap) || (overlap->flags & TypeFlagsUnion) != 0) {
					// We only want to account for literal types otherwise.
					// If we have a union of index types, it seems likely that we
					// needed to elaborate between two generic mapped types anyway.
					int length = 1;
					if (overlap->flags & TypeFlagsUnion) {
						length = countWhere(overlap->types(),
											[](Type* t) { return isUnitType(t); });
					}
					if (length >= matchingCount) {
						bestMatch = target;
						matchingCount = length;
					}
				}
			}
		}
	}
	return bestMatch;
}

// relater.go:945
Type* Checker::findBestTypeForObjectLiteral(Type* source, Type* unionTarget) {
	if ((source->objectFlags & ObjectFlagsObjectLiteral) != 0 &&
		someType(unionTarget, [this](Type* t) { return isArrayLikeType(t); })) {
		return findPtr(unionTarget->types(),
					   [this](Type* t) { return !isArrayLikeType(t); });
	}
	return nullptr;
}

// relater.go:952
bool Checker::shouldReportUnmatchedPropertyError(Type* source, Type* target) {
	std::vector<Signature*> typeCallSignatures =
		getSignaturesOfStructuredType(source, SignatureKind::Call);
	std::vector<Signature*> typeConstructSignatures =
		getSignaturesOfStructuredType(source, SignatureKind::Construct);
	std::vector<Symbol*> typeProperties = getPropertiesOfObjectType(source);
	if ((!typeCallSignatures.empty() || !typeConstructSignatures.empty()) &&
		typeProperties.empty()) {
		if ((!getSignaturesOfType(target, SignatureKind::Call).empty() &&
			 !typeCallSignatures.empty()) ||
			(!getSignaturesOfType(target, SignatureKind::Construct).empty() &&
			 !typeConstructSignatures.empty())) {
			// target has similar signature kinds to source, still focus on the
			// unmatched property
			return true;
		}
		return false;
	}
	return true;
}

// relater.go:967
Symbol* Checker::getUnmatchedProperty(Type* source, Type* target,
									  bool requireOptionalProperties,
									  bool matchDiscriminantProperties) {
	return getUnmatchedPropertiesWorker(source, target, requireOptionalProperties,
										matchDiscriminantProperties, nullptr);
}

// relater.go:971
std::vector<Symbol*> Checker::getUnmatchedProperties(
	Type* source, Type* target, bool requireOptionalProperties,
	bool matchDiscriminantProperties) {
	std::vector<Symbol*> props;
	getUnmatchedPropertiesWorker(source, target, requireOptionalProperties,
								 matchDiscriminantProperties, &props);
	return props;
}

// relater.go:977
Symbol* Checker::getUnmatchedPropertiesWorker(Type* source, Type* target,
											  bool requireOptionalProperties,
											  bool matchDiscriminantProperties,
											  std::vector<Symbol*>* propsOut) {
	std::vector<Symbol*> properties = getPropertiesOfType(target);
	for (Symbol* targetProp : properties) {
		// TODO: remove this when we support static private identifier fields
		// and find other solutions to get privateNamesAndStaticFields test to
		// pass
		if (isStaticPrivateIdentifierProperty(targetProp)) {
			continue;
		}
		if (requireOptionalProperties ||
			((targetProp->flags & SymbolFlagsOptional) == 0 &&
			 (targetProp->checkFlags & CheckFlagsPartial) == 0)) {
			Symbol* sourceProp = getPropertyOfType(source, targetProp->name);
			if (sourceProp == nullptr) {
				if (propsOut == nullptr) {
					return targetProp;
				}
				propsOut->push_back(targetProp);
			} else if (matchDiscriminantProperties) {
				Type* targetType = getTypeOfSymbol(targetProp);
				if (targetType->flags & TypeFlagsUnit) {
					Type* sourceType = getTypeOfSymbol(sourceProp);
					if (!((sourceType->flags & TypeFlagsAny) != 0 ||
						  getRegularTypeOfLiteralType(sourceType) ==
							  getRegularTypeOfLiteralType(targetType))) {
						if (propsOut == nullptr) {
							return targetProp;
						}
						propsOut->push_back(targetProp);
					}
				}
			}
		}
	}
	return nullptr;
}

// Keep this up-to-date with the same logic within
// `getApparentTypeOfContextualType`, since they should behave similarly
// relater.go:1055
Type* Checker::findMatchingDiscriminantType(
	Type* source, Type* target,
	const std::function<Ternary(Type*, Type*)>& isRelatedTo) {
	if ((target->flags & TypeFlagsUnion) != 0 &&
		(source->flags & (TypeFlagsIntersection | TypeFlagsObject)) != 0) {
		if (Type* match = getMatchingUnionConstituentForType(target, source); match != nullptr) {
			return match;
		}
		if (std::vector<Symbol*> discriminantProperties =
				findDiscriminantProperties(getPropertiesOfType(source), target);
			!discriminantProperties.empty()) {
			TypeDiscriminator discriminator{this, discriminantProperties, isRelatedTo};
			if (Type* discriminated =
					discriminateTypeByDiscriminableItems(target, discriminator);
				discriminated != target) {
				return discriminated;
			}
		}
	}
	return nullptr;
}

// relater.go:1070
std::vector<Symbol*> Checker::findDiscriminantProperties(
	const std::vector<Symbol*>& sourceProperties, Type* target) {
	std::vector<Symbol*> result;
	for (Symbol* sourceProperty : sourceProperties) {
		if (isDiscriminantProperty(target, sourceProperty->name)) {
			result.push_back(sourceProperty);
		}
	}
	return result;
}

// relater.go:1080
bool Checker::isDiscriminantProperty(Type* t, const std::string& name) {
	if (t != nullptr && (t->flags & TypeFlagsUnion) != 0) {
		Symbol* prop = getUnionOrIntersectionProperty(
			t, name, false /*skipObjectFunctionPropertyAugment*/);
		if (prop != nullptr && (prop->checkFlags & CheckFlagsSyntheticProperty) != 0) {
			if ((prop->checkFlags & CheckFlagsIsDiscriminantComputed) == 0) {
				prop->checkFlags |= CheckFlagsIsDiscriminantComputed;
				if ((prop->checkFlags & CheckFlagsNonUniformAndLiteral) ==
						CheckFlagsNonUniformAndLiteral &&
					!isGenericType(getTypeOfSymbol(prop))) {
					prop->checkFlags |= CheckFlagsIsDiscriminant;
				}
			}
			return (prop->checkFlags & CheckFlagsIsDiscriminant) != 0;
		}
	}
	return false;
}

// relater.go:1096
Type* Checker::getMatchingUnionConstituentForType(Type* unionType, Type* t) {
	std::string keyPropertyName = getKeyPropertyName(unionType);
	if (keyPropertyName.empty()) {
		return nullptr;
	}
	Type* propType = getTypeOfPropertyOfType(t, keyPropertyName);
	if (propType == nullptr) {
		return nullptr;
	}
	return getConstituentTypeForKeyType(unionType, propType);
}

// Return the name of a discriminant property for which it was possible and
// feasible to construct a map of constituent types keyed by the literal types
// of the property by that name in each constituent type. Return an empty
// string if no such discriminant property exists.
// relater.go:1111
std::string Checker::getKeyPropertyName(Type* t) {
	UnionType* u = t->AsUnionType();
	if (u->keyPropertyName.empty()) {
		auto computed = computeKeyPropertyNameAndMap(t);
		u->keyPropertyName = computed.first;
		u->constituentMap = std::move(computed.second);
	}
	if (u->keyPropertyName == InternalSymbolNameMissing) {
		return "";
	}
	return u->keyPropertyName;
}

// Given a union type for which getKeyPropertyName returned a non-empty string,
// return the constituent that corresponds to the given key type for that
// property name.
// relater.go:1124
Type* Checker::getConstituentTypeForKeyType(Type* t, Type* keyType) {
	auto& constituentMap = t->AsUnionType()->constituentMap;
	auto it = constituentMap.find(getRegularTypeOfLiteralType(keyType));
	Type* result = it != constituentMap.end() ? it->second : nullptr;
	if (result != unknownType) {
		return result;
	}
	return nullptr;
}

// relater.go:1132
std::pair<std::string, std::unordered_map<Type*, Type*>>
Checker::computeKeyPropertyNameAndMap(Type* t) {
	const std::vector<Type*>& types = t->types();
	if (types.size() < 10 || (t->objectFlags & ObjectFlagsPrimitiveUnion) != 0 ||
		countWhere(types, [](Type* u) {
			return isObjectOrInstantiableNonPrimitive(u);
		}) < 10) {
		return {InternalSymbolNameMissing, {}};
	}
	std::string keyPropertyName = getKeyPropertyCandidateName(types);
	if (keyPropertyName.empty()) {
		return {InternalSymbolNameMissing, {}};
	}
	auto mapByKeyProperty = mapTypesByKeyProperty(types, keyPropertyName);
	if (!mapByKeyProperty.has_value()) {
		return {InternalSymbolNameMissing, {}};
	}
	return {keyPropertyName, std::move(*mapByKeyProperty)};
}

// relater.go:1152
std::string Checker::getKeyPropertyCandidateName(const std::vector<Type*>& types) {
	for (Type* t : types) {
		if ((t->flags & (TypeFlagsObject | TypeFlagsInstantiableNonPrimitive)) != 0) {
			for (Symbol* p : getPropertiesOfType(t)) {
				if (isUnitType(getTypeOfSymbol(p))) {
					return p->name;
				}
			}
		}
	}
	return "";
}

// Given a set of constituent types and a property name, create and return a map
// keyed by the literal types of the property by that name in each constituent
// type. No map is returned if some key property has a non-literal type or if
// less than 10 or less than 50% of the constituents have a unique key.
// Entries with duplicate keys have unknownType as the value.
// relater.go:1169
std::optional<std::unordered_map<Type*, Type*>> Checker::mapTypesByKeyProperty(
	const std::vector<Type*>& types, const std::string& keyPropertyName) {
	std::unordered_map<Type*, Type*> typesByKey;
	int count = 0;
	for (Type* t : types) {
		if ((t->flags &
			 (TypeFlagsObject | TypeFlagsIntersection |
			  TypeFlagsInstantiableNonPrimitive)) != 0) {
			Type* discriminant = getTypeOfPropertyOfType(t, keyPropertyName);
			if (discriminant == nullptr || !isLiteralType(discriminant)) {
				return std::nullopt;
			}
			bool duplicate = false;
			for (Type* d : discriminant->Distributed()) {
				Type* key = getRegularTypeOfLiteralType(d);
				auto it = typesByKey.find(key);
				if (it == typesByKey.end()) {
					typesByKey[key] = t;
				} else if (it->second != unknownType) {
					typesByKey[key] = unknownType;
					duplicate = true;
				}
			}
			if (!duplicate) {
				count++;
			}
		}
	}
	if (count >= 10 && count * 2 >= static_cast<int>(types.size())) {
		return typesByKey;
	}
	return std::nullopt;
}

// relater.go:1205
Type* Checker::discriminateTypeByDiscriminableItems(Type* target,
													Discriminator& discriminator) {
	const std::vector<Type*>& types = target->types();
	std::vector<Ternary> include(types.size(), Ternary::False);
	for (size_t i = 0; i < types.size(); i++) {
		Type* t = types[i];
		if ((t->flags & TypeFlagsPrimitive) == 0 &&
			(getReducedType(t)->flags & TypeFlagsNever) == 0) {
			include[i] = Ternary::True;
		}
	}
	for (int n = 0; n < discriminator.len(); n++) {
		// If the remaining target types include at least one with a matching
		// discriminant, eliminate those that have non-matching discriminants.
		// This ensures that we ignore erroneous discriminators and gradually
		// refine the target set without eliminating every constituent (which
		// would lead to `never`).
		bool matched = false;
		for (size_t i = 0; i < types.size(); i++) {
			if (include[i] != Ternary::False) {
				Type* targetType = getTypeOfPropertyOrIndexSignatureOfType(
					types[i], discriminator.name(n));
				if (targetType != nullptr) {
					if (discriminator.matches(n, targetType)) {
						matched = true;
					} else {
						include[i] = Ternary::Maybe;
					}
				}
			}
		}
		// Turn each Ternary.Maybe into Ternary.False if there was a match.
		// Otherwise, revert to Ternary.True.
		for (size_t i = 0; i < types.size(); i++) {
			if (include[i] == Ternary::Maybe) {
				if (matched) {
					include[i] = Ternary::False;
				} else {
					include[i] = Ternary::True;
				}
			}
		}
	}
	if (std::find(include.begin(), include.end(), Ternary::False) != include.end()) {
		std::vector<Type*> filteredTypes;
		for (size_t i = 0; i < types.size(); i++) {
			if (include[i] == Ternary::True) {
				filteredTypes.push_back(types[i]);
			}
		}
		Type* filtered = getUnionTypeEx(std::move(filteredTypes),
										UnionReductionNone, nullptr, nullptr);
		if ((filtered->flags & TypeFlagsNever) == 0) {
			return filtered;
		}
	}
	return target;
}

// relater.go:1256
Type* Checker::filterPrimitivesIfContainsNonPrimitive(Type* unionType) {
	if (maybeTypeOfKind(unionType, TypeFlagsNonPrimitive)) {
		Type* result = filterType(unionType,
								  [](Type* t) { return isNonPrimitiveType(t); });
		if ((result->flags & TypeFlagsNever) == 0) {
			return result;
		}
	}
	return unionType;
}

// relater.go:1270
std::pair<std::string, std::string> Checker::getTypeNamesForErrorDisplay(
	Type* left, Type* right) {
	std::string leftStr;
	if (symbolValueDeclarationIsContextSensitive(left->symbol)) {
		leftStr = typeToString(left, left->symbol->valueDeclaration);
	} else {
		leftStr = TypeToString(left);
	}
	std::string rightStr;
	if (symbolValueDeclarationIsContextSensitive(right->symbol)) {
		rightStr = typeToString(right, right->symbol->valueDeclaration);
	} else {
		rightStr = TypeToString(right);
	}
	if (leftStr == rightStr) {
		leftStr = getTypeNameForErrorDisplay(left);
		rightStr = getTypeNameForErrorDisplay(right);
	}
	return {leftStr, rightStr};
}

// relater.go:1290
std::string Checker::getTypeNameForErrorDisplay(Type* t) {
	return typeToStringEx(t, nullptr /*enclosingDeclaration*/,
						  TypeFormatFlagsUseFullyQualifiedType, nullptr);
}

// relater.go:1294
bool Checker::symbolValueDeclarationIsContextSensitive(Symbol* symbol) {
	return symbol != nullptr && symbol->valueDeclaration != nullptr &&
		   isExpression(symbol->valueDeclaration) &&
		   !isContextSensitive(symbol->valueDeclaration);
}

// relater.go:1298
bool Checker::typeCouldHaveTopLevelSingletonTypes(Type* t) {
	// Okay, yes, 'boolean' is a union of 'true | false', but that's not useful
	// in error reporting scenarios. If you need to use this function but that
	// detail matters, feel free to add a flag.
	if (t->flags & TypeFlagsBoolean) {
		return false;
	}
	if (t->flags & TypeFlagsUnionOrIntersection) {
		return std::any_of(t->types().begin(), t->types().end(),
						   [this](Type* u) {
							   return typeCouldHaveTopLevelSingletonTypes(u);
						   });
	}
	if (t->flags & TypeFlagsInstantiable) {
		Type* constraint = getConstraintOfType(t);
		if (constraint != nullptr && constraint != t) {
			return typeCouldHaveTopLevelSingletonTypes(constraint);
		}
	}
	return isUnitType(t) || (t->flags & TypeFlagsTemplateLiteral) != 0 ||
		   (t->flags & TypeFlagsStringMapping) != 0;
}

// relater.go:1317
std::vector<VarianceFlags> Checker::getVariances(Type* t) {
	// Arrays and tuples are known to be covariant, no need to spend time
	// computing this.
	if (t == globalArrayType || t == globalReadonlyArrayType ||
		(t->objectFlags & ObjectFlagsTuple) != 0) {
		return arrayVariances;
	}
	return getVariancesWorker(t->symbol,
							  interfaceTypeTypeParameters(t->AsInterfaceType()));
}

// relater.go:1325
std::vector<VarianceFlags> Checker::getAliasVariances(Symbol* symbol) {
	return getVariancesWorker(symbol, typeAliasLinks.Get(symbol)->typeParameters);
}

// Return an array containing the variance of each type parameter. The variance
// is effectively a digest of the type comparisons that occur for each type
// argument when instantiations of the generic type are structurally compared.
// We infer the variance information by comparing instantiations of the generic
// type for type arguments with known relations. The function returns an empty
// slice when invoked recursively for the given generic type.
// relater.go:1334
std::vector<VarianceFlags> Checker::getVariancesWorker(
	Symbol* symbol, const std::vector<Type*>& typeParameters) {
	VarianceLinks* links = varianceLinks.Get(symbol);
	if (!links->variances.has_value()) {
		std::shared_ptr<tsc::tracing::TraceArgs> traceArgs;
		std::function<void()> popFn;
		if (Tracer* tr = tracer; tr != nullptr) {
			traceArgs = std::make_shared<tsc::tracing::TraceArgs>(tsc::tracing::TraceArgs{
				{"arity", static_cast<int>(typeParameters.size())},
				{"id", getDeclaredTypeOfSymbol(symbol)->id}});
			popFn = tr->Push(tsc::tracing::PhaseCheckTypes, "getVariancesWorker",
							 traceArgs, true);
		}
		// relater.go:1342-1348 — `defer func() { traceArgs["variances"] =
		// formatted; popFn() }()`: the shared args map means the mutation
		// reaches the "E" event.
		struct TracePopGuard {
			std::shared_ptr<tsc::tracing::TraceArgs> traceArgs;
			std::function<void()> popFn;
			VarianceLinks* links;
			~TracePopGuard() {
				if (popFn == nullptr) {
					return;
				}
				std::vector<std::string> formatted;
				if (links->variances.has_value()) {
					formatted.reserve(links->variances->size());
					for (VarianceFlags v : *links->variances) {
						formatted.push_back(VarianceFlagsString(v));
					}
				}
				(*traceArgs)["variances"] = std::move(formatted);
				popFn();
			}
		} tracePopGuard{traceArgs, popFn, links};
		(void)tracePopGuard;
		int stackIndex = getVarianceStackIndex(symbol);
		if (stackIndex < 0) {
			int saveResolutionStart = resolutionStart;
			if (varianceStack.empty()) {
				resolutionStart = static_cast<int>(typeResolutions.size());
			}
			varianceStack.push_back(VarianceStackEntry{symbol, typeParameters});
			std::vector<VarianceFlags> variances(typeParameters.size(), 0);
			for (size_t i = 0; i < typeParameters.size(); i++) {
				Type* tp = typeParameters[i];
				ModifierFlags modifiers = getTypeParameterModifiers(tp);
				VarianceFlags variance;
				if ((modifiers & ModifierFlagsOut) != 0) {
					if ((modifiers & ModifierFlagsIn) != 0) {
						variance = VarianceFlagsInvariant;
					} else {
						variance = VarianceFlagsCovariant;
					}
				} else if ((modifiers & ModifierFlagsIn) != 0) {
					variance = VarianceFlagsContravariant;
				} else {
					RelationComparisonResult saveReliabilityFlags =
						reliabilityFlags;
					reliabilityFlags = RelationComparisonResult::None;
					// We first compare instantiations where the type parameter
					// is replaced with marker types that have a known subtype
					// relationship. From this we can infer invariance,
					// covariance, contravariance or bivariance.
					Type* typeWithSuper =
						createMarkerType(symbol, tp, markerSuperType);
					Type* typeWithSub =
						createMarkerType(symbol, tp, markerSubType);
					variance =
						(isTypeAssignableTo(typeWithSub, typeWithSuper)
							 ? VarianceFlagsCovariant
							 : 0) |
						(isTypeAssignableTo(typeWithSuper, typeWithSub)
							 ? VarianceFlagsContravariant
							 : 0);
					// If the instantiations appear to be related bivariantly it
					// may be because the type parameter is independent (i.e. it
					// isn't witnessed anywhere in the generic type). To
					// determine this we compare instantiations where the type
					// parameter is replaced with marker types that are known to
					// be unrelated.
					if (variance == VarianceFlagsBivariant &&
						isTypeAssignableTo(
							createMarkerType(symbol, tp, markerOtherType),
							typeWithSuper)) {
						variance = VarianceFlagsIndependent;
					}
					if ((reliabilityFlags &
						 RelationComparisonResult::ReportsUnmeasurable) !=
						RelationComparisonResult::None) {
						variance |= VarianceFlagsUnmeasurable;
					}
					if ((reliabilityFlags &
						 RelationComparisonResult::ReportsUnreliable) !=
						RelationComparisonResult::None) {
						variance |= VarianceFlagsUnreliable;
					}
					reliabilityFlags = saveReliabilityFlags;
				}
				// If variance computation was restarted due to a circularity we
				// may have already computed variances for this generic type. If
				// so, we exit early.
				// Go: len(links.variances) != 0 — the empty-slice circularity
				// marker (has_value but empty) does NOT stop the outer frame
				// from finishing its own computation.
				if (links->variances.has_value() &&
					!links->variances->empty()) {
					break;
				}
				variances[i] = variance;
			}
			// Store the results unless a restarted computation has already
			// stored them.
			// Go: len(links.variances) == 0 — also true for the empty-slice
			// circularity marker, so a frame that was mid-computation when
			// it got marked still stores its real variances afterwards.
			if (!links->variances.has_value() ||
				links->variances->empty()) {
				links->variances = variances;
			}
			varianceStack.pop_back();
			if (varianceStack.empty()) {
				resolutionStart = saveResolutionStart;
			}
		} else {
			// We've detected a circularity. Since we may compute different
			// variances depending on where we enter a circularity, we find the
			// generic type with the "smallest" symbol in the circular region of
			// the variance stack and restart the computation from there if
			// necessary. This ensures stable results for circular generic types.
			int minIndex = stackIndex;
			for (size_t i = stackIndex + 1; i < varianceStack.size(); i++) {
				if (compareSymbols(varianceStack[i].symbol,
								   varianceStack[minIndex].symbol) < 0) {
					minIndex = static_cast<int>(i);
				}
			}
			if (minIndex > stackIndex) {
				std::vector<VarianceStackEntry> saveVarianceStack =
					std::move(varianceStack);
				varianceStack.clear();
				getVariancesWorker(saveVarianceStack[minIndex].symbol,
								   saveVarianceStack[minIndex].typeParameters);
				varianceStack = std::move(saveVarianceStack);
			}
			// Store an empty slice to mark that we can't compute variances for
			// this type. We treat type parameters as co-variant in this case.
			// Go: len(links.variances) == 0 — skip only when a restarted
			// computation already stored REAL variances.
			if (!links->variances.has_value() ||
				links->variances->empty()) {
				links->variances = std::vector<VarianceFlags>{};
			}
		}
	}
	return *links->variances;
}

// relater.go:1437
int Checker::getVarianceStackIndex(Symbol* symbol) {
	for (size_t i = 0; i < varianceStack.size(); i++) {
		if (varianceStack[i].symbol == symbol) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

// relater.go:1446
Type* Checker::createMarkerType(Symbol* symbol, Type* source, Type* target) {
	TypeMapper* mapper = newSimpleTypeMapper(source, target);
	Type* t = getDeclaredTypeOfSymbol(symbol);
	if (isErrorType(t)) {
		return t;
	}
	Type* result;
	if ((symbol->flags & SymbolFlagsTypeAlias) != 0) {
		result = getTypeAliasInstantiation(
			symbol,
			instantiateTypes(typeAliasLinks.Get(symbol)->typeParameters, mapper),
			nullptr);
	} else {
		result = createTypeReference(
			t,
			instantiateTypes(interfaceTypeTypeParameters(t->AsInterfaceType()),
							 mapper));
	}
	markerTypes.insert(result);
	return result;
}

// relater.go:1462
bool Checker::isMarkerType(Type* t) {
	return markerTypes.count(t) != 0;
}

// relater.go:1466
ModifierFlags Checker::getTypeParameterModifiers(Type* tp) {
	ModifierFlags flags = 0;
	if (tp->symbol != nullptr) {
		for (Node* d : tp->symbol->declarations) {
			flags |= d->modifierFlags();
		}
	}
	return flags & (ModifierFlagsIn | ModifierFlagsOut | ModifierFlagsConst);
}

// Return true if the given type reference has a 'void' type argument for a
// covariant type parameter. See comment at call in recursiveTypeRelatedTo for
// when this case matters.
// relater.go:1478
bool Checker::hasCovariantVoidArgument(const std::vector<Type*>& typeArguments,
									   const std::vector<VarianceFlags>& variances) {
	for (size_t i = 0; i < variances.size(); i++) {
		if ((variances[i] & VarianceFlagsVarianceMask) ==
				VarianceFlagsCovariant &&
			(typeArguments[i]->flags & TypeFlagsVoid) != 0) {
			return true;
		}
	}
	return false;
}

// relater.go:1487
bool Checker::isSignatureAssignableTo(Signature* source, Signature* target,
									  bool ignoreReturnTypes) {
	return compareSignaturesRelated(source, target,
									ignoreReturnTypes
										? SignatureCheckModeIgnoreReturnTypes
										: SignatureCheckModeNone,
									false /*reportErrors*/, nullptr,
									compareTypesAssignable,
									nullptr /*reportUnreliableMarkers*/) !=
		   Ternary::False;
}

// relater.go:1491
Ternary Checker::compareSignaturesRelated(
	Signature* source, Signature* target, SignatureCheckMode checkMode,
	bool reportErrors, ErrorReporter errorReporter,
	TypeComparer compareTypes, TypeMapper* reportUnreliableMarkers) {
	if (source == target) {
		return Ternary::True;
	}
	if (!((checkMode & SignatureCheckModeStrictTopSignature) != 0 &&
		  isTopSignature(source)) &&
		isTopSignature(target)) {
		return Ternary::True;
	}
	if ((checkMode & SignatureCheckModeStrictTopSignature) != 0 &&
		isTopSignature(source) && !isTopSignature(target)) {
		return Ternary::False;
	}
	int targetCount = getParameterCount(target);
	bool sourceHasMoreParameters = false;
	if (!hasEffectiveRestParameter(target)) {
		if ((checkMode & SignatureCheckModeStrictArity) != 0) {
			sourceHasMoreParameters = hasEffectiveRestParameter(source) ||
									  getParameterCount(source) > targetCount;
		} else {
			sourceHasMoreParameters = getMinArgumentCount(source) > targetCount;
		}
	}
	if (sourceHasMoreParameters) {
		if (reportErrors && (checkMode & SignatureCheckModeStrictArity) == 0) {
			// the second condition should be redundant, because there is no
			// error reporting when comparing signatures by strict arity since
			// it is only done for subtype reduction
			errorReporter(
				
					Target_signature_provides_too_few_arguments_Expected_0_or_more_but_got_1,
				{std::to_string(getMinArgumentCount(source)),
				 std::to_string(targetCount)});
		}
		return Ternary::False;
	}
	if (!source->typeParameters.empty() &&
		!sameVec(source->typeParameters, target->typeParameters)) {
		target = getCanonicalSignature(target);
		source = instantiateSignatureInContextOf(source, target /*inferenceContext*/,
												 nullptr, compareTypes);
	}
	int sourceCount = getParameterCount(source);
	Type* sourceRestType = getNonArrayRestType(source);
	Type* targetRestType = getNonArrayRestType(target);
	if (sourceRestType != nullptr || targetRestType != nullptr) {
		instantiateType(sourceRestType != nullptr ? sourceRestType
												: targetRestType,
						reportUnreliableMarkers);
	}
	Kind kind = Kind::Unknown;
	if (target->declaration != nullptr) {
		kind = target->declaration->kind;
	}
	bool strictVariance = (checkMode & SignatureCheckModeCallback) == 0 &&
						  strictFunctionTypes && kind != Kind::MethodDeclaration &&
						  kind != Kind::MethodSignature && kind != Kind::Constructor;
	Ternary result = Ternary::True;
	Type* sourceThisType = getThisTypeOfSignature(source);
	if (sourceThisType != nullptr && sourceThisType != voidType) {
		Type* targetThisType = getThisTypeOfSignature(target);
		if (targetThisType != nullptr) {
			// void sources are assignable to anything.
			Ternary related = Ternary::False;
			if (!strictVariance) {
				related = compareTypes(sourceThisType, targetThisType,
									   false /*reportErrors*/);
			}
			if (related == Ternary::False) {
				related = compareTypes(targetThisType, sourceThisType,
									   reportErrors);
			}
			if (related == Ternary::False) {
				if (reportErrors) {
					errorReporter(
						
							The_this_types_of_each_signature_are_incompatible,
						{});
				}
				return Ternary::False;
			}
			result &= related;
		}
	}
	int paramCount;
	if (sourceRestType != nullptr || targetRestType != nullptr) {
		paramCount = std::min(sourceCount, targetCount);
	} else {
		paramCount = std::max(sourceCount, targetCount);
	}
	int restIndex;
	if (sourceRestType != nullptr || targetRestType != nullptr) {
		restIndex = paramCount - 1;
	} else {
		restIndex = -1;
	}
	for (int i = 0; i < paramCount; i++) {
		Type* sourceType;
		if (i == restIndex) {
			sourceType = getRestOrAnyTypeAtPosition(source, i);
		} else {
			sourceType = tryGetTypeAtPosition(source, i);
		}
		Type* targetType;
		if (i == restIndex) {
			targetType = getRestOrAnyTypeAtPosition(target, i);
		} else {
			targetType = tryGetTypeAtPosition(target, i);
		}
		if (sourceType != nullptr && targetType != nullptr &&
			(sourceType != targetType ||
			 (checkMode & SignatureCheckModeStrictArity) != 0)) {
			// In order to ensure that any generic type Foo<T> is at least
			// co-variant with respect to T no matter how Foo uses T, we need to
			// relate parameters bi-variantly (given that parameters are input
			// positions, they naturally relate only contra-variantly). However,
			// if the source and target parameters both have function types with
			// a single call signature, we know we are relating two callback
			// parameters. In that case it is sufficient to only relate the
			// parameters of the signatures co-variantly because, similar to
			// return values, callback parameters are output positions. This
			// means that a Promise<T>, where T is used only in callback
			// parameter positions, will be co-variant (as opposed to
			// bi-variant) with respect to T.
			Signature* sourceSig = nullptr;
			Signature* targetSig = nullptr;
			if ((checkMode & SignatureCheckModeCallback) == 0 &&
				!isInstantiatedGenericParameter(source, i)) {
				sourceSig = getSingleCallSignature(GetNonNullableType(sourceType));
			}
			if ((checkMode & SignatureCheckModeCallback) == 0 &&
				!isInstantiatedGenericParameter(target, i)) {
				targetSig = getSingleCallSignature(GetNonNullableType(targetType));
			}
			bool callbacks = sourceSig != nullptr && targetSig != nullptr &&
							 getTypePredicateOfSignature(sourceSig) == nullptr &&
							 getTypePredicateOfSignature(targetSig) == nullptr &&
							 getTypeFacts(sourceType, TypeFactsIsUndefinedOrNull) ==
								 getTypeFacts(targetType, TypeFactsIsUndefinedOrNull);
			Ternary related = Ternary::False;
			if (callbacks) {
				related = compareSignaturesRelated(
					targetSig, sourceSig,
					(checkMode & SignatureCheckModeStrictArity) |
						(strictVariance ? SignatureCheckModeStrictCallback
										: SignatureCheckModeBivariantCallback),
					reportErrors, errorReporter, compareTypes,
					reportUnreliableMarkers);
			} else {
				if ((checkMode & SignatureCheckModeCallback) == 0 &&
					!strictVariance) {
					related = compareTypes(sourceType, targetType,
										   false /*reportErrors*/);
				}
				if (related == Ternary::False) {
					related = compareTypes(targetType, sourceType, reportErrors);
				}
			}
			// With strict arity, (x: number | undefined) => void is a subtype of
			// (x?: number | undefined) => void
			if (related != Ternary::False &&
				(checkMode & SignatureCheckModeStrictArity) != 0 &&
				i >= getMinArgumentCount(source) &&
				i < getMinArgumentCount(target) &&
				compareTypes(sourceType, targetType, false /*reportErrors*/) !=
					Ternary::False) {
				related = Ternary::False;
			}
			if (related == Ternary::False) {
				if (reportErrors) {
					errorReporter(
						Types_of_parameters_0_and_1_are_incompatible,
						{getParameterNameAtPosition(source, i),
						 getParameterNameAtPosition(target, i)});
				}
				return Ternary::False;
			}
			result &= related;
		}
	}
	if ((checkMode & SignatureCheckModeIgnoreReturnTypes) == 0) {
		// If a signature resolution is already in-flight, skip issuing a
		// circularity error here and just use the `any` type directly
		Type* targetReturnType = getNonCircularReturnTypeOfSignature(target);
		if (targetReturnType == voidType || targetReturnType == anyType) {
			return result;
		}
		Type* sourceReturnType = getNonCircularReturnTypeOfSignature(source);
		// The following block preserves behavior forbidding boolean returning
		// functions from being assignable to type guard returning functions
		TypePredicate* targetTypePredicate = getTypePredicateOfSignature(target);
		if (targetTypePredicate != nullptr) {
			TypePredicate* sourceTypePredicate =
				getTypePredicateOfSignature(source);
			if (sourceTypePredicate != nullptr) {
				result &= compareTypePredicateRelatedTo(
					sourceTypePredicate, targetTypePredicate, reportErrors,
					errorReporter, compareTypes);
			} else if (targetTypePredicate->kind == TypePredicateKind::Identifier ||
					   targetTypePredicate->kind == TypePredicateKind::This) {
				if (reportErrors) {
					errorReporter(
						Signature_0_must_be_a_type_predicate,
						{signatureToString(source)});
				}
				return Ternary::False;
			}
		} else {
			// When relating callback signatures, we still need to relate return
			// types bi-variantly as otherwise the containing type wouldn't be
			// co-variant. For example, interface Foo<T> { add(cb: () => T):
			// void } wouldn't be co-variant for T without this rule.
			Ternary related = Ternary::False;
			if ((checkMode & SignatureCheckModeBivariantCallback) != 0) {
				related = compareTypes(targetReturnType, sourceReturnType,
									   false /*reportErrors*/);
			}
			if (related == Ternary::False) {
				related = compareTypes(sourceReturnType, targetReturnType,
									   reportErrors);
			}
			result &= related;
			if (result == Ternary::False && reportErrors) {
				// The errors reported here serve as markers that trigger error
				// chain reduction in the Relater::reportError method. The
				// markers are elided in the final diagnostic chain and never
				// actually reported.
				const DiagnosticMessage* message;
				if (source->parameters.empty() && target->parameters.empty()) {
					message = (source->flags & SignatureFlagsConstruct) != 0
								  ? 
									  Construct_signatures_with_no_arguments_have_incompatible_return_types_0_and_1
								  : 
									  Call_signatures_with_no_arguments_have_incompatible_return_types_0_and_1;
				} else {
					message =
						(source->flags & SignatureFlagsConstruct) != 0
							? 
								Construct_signature_return_types_0_and_1_are_incompatible
							: 
								Call_signature_return_types_0_and_1_are_incompatible;
				}
				errorReporter(message, {TypeToString(sourceReturnType),
										TypeToString(targetReturnType)});
			}
		}
	}
	return result;
}

// relater.go:1675
Ternary Checker::compareTypePredicateRelatedTo(
	TypePredicate* source, TypePredicate* target, bool reportErrors,
	ErrorReporter errorReporter, TypeComparer compareTypes) {
	if (source->kind != target->kind) {
		if (reportErrors) {
			errorReporter(
				
					A_this_based_type_guard_is_not_compatible_with_a_parameter_based_type_guard,
				{});
			errorReporter(Type_predicate_0_is_not_assignable_to_1,
						  {typePredicateToString(source),
						   typePredicateToString(target)});
		}
				return Ternary::False;
	}
	if (source->kind == TypePredicateKind::Identifier ||
		source->kind == TypePredicateKind::AssertsIdentifier) {
		if (source->parameterIndex != target->parameterIndex) {
			if (reportErrors) {
				errorReporter(
					
						Parameter_0_is_not_in_the_same_position_as_parameter_1,
					{source->parameterName, target->parameterName});
				errorReporter(
					Type_predicate_0_is_not_assignable_to_1,
					{typePredicateToString(source),
					 typePredicateToString(target)});
			}
			return Ternary::False;
		}
	}
	Ternary related = Ternary::False;
	if (source->t == target->t) {
		related = Ternary::True;
	} else if (source->t != nullptr && target->t != nullptr) {
		related = compareTypes(source->t, target->t, reportErrors);
	} else {
		related = Ternary::False;
	}
	if (related == Ternary::False && reportErrors) {
		errorReporter(Type_predicate_0_is_not_assignable_to_1,
					  {typePredicateToString(source),
					   typePredicateToString(target)});
	}
	return related;
}

// Returns true if `s` is `(...args: A) => R` where `A` is `any`, `any[]`,
// `never`, or `never[]`, and `R` is `any` or `unknown`.
// relater.go:1708
bool Checker::isTopSignature(Signature* s) {
	if (s->typeParameters.empty() &&
		(s->thisParameter == nullptr ||
		 isTypeAny(getTypeOfParameter(s->thisParameter))) &&
		s->parameters.size() == 1 && signatureHasRestParameter(s)) {
		Type* paramType = getTypeOfParameter(s->parameters[0]);
		Type* restType;
		if (isArrayType(paramType)) {
			restType = getTypeArguments(paramType)[0];
		} else {
			restType = paramType;
		}
		return (restType->flags & (TypeFlagsAny | TypeFlagsNever)) != 0 &&
			   (getReturnTypeOfSignature(s)->flags & TypeFlagsAnyOrUnknown) != 0;
	}
	return false;
}

// Return the number of parameters in a signature. The rest parameter, if
// present, counts as one parameter. For example, the parameter count of
// (x: number, y: number, ...z: string[]) is 3 and the parameter count of
// (x: number, ...args: [number, ...string[], boolean])) is also 3. In the
// latter example, the effective rest type is [...string[], boolean].
// relater.go:1726
int Checker::getParameterCount(Signature* signature) {
	int length = static_cast<int>(signature->parameters.size());
	if (signatureHasRestParameter(signature)) {
		Type* restType = getTypeOfSymbol(signature->parameters[length - 1]);
		if (isTupleType(restType)) {
			TupleType* restTuple = restType->TargetTupleType();
			return length + restTuple->fixedLength -
				   (((restTuple->combinedFlags & ElementFlagsVariable) != 0) ? 0
																		   : 1);
		}
	}
	return length;
}

// relater.go:1737
int Checker::getMinArgumentCount(Signature* signature) {
	return getMinArgumentCountEx(signature, MinArgumentCountFlagsNone);
}

// relater.go:1741
int Checker::getMinArgumentCountEx(Signature* signature,
								   MinArgumentCountFlags flags) {
	MinArgumentCountFlags strongArityForUntypedJS =
		flags & MinArgumentCountFlagsStrongArityForUntypedJS;
	MinArgumentCountFlags voidIsNonOptional =
		flags & MinArgumentCountFlagsVoidIsNonOptional;
	if (voidIsNonOptional != 0 || signature->resolvedMinArgumentCount == -1) {
		int minArgumentCount = -1;
		if (signatureHasRestParameter(signature)) {
			Type* restType = getTypeOfSymbol(
				signature->parameters[signature->parameters.size() - 1]);
			if (isTupleType(restType)) {
				TupleType* restTuple = restType->TargetTupleType();
				int firstOptionalIndex = findIndexOf(
					restTuple->elementInfos, [](const TupleElementInfo& info) {
						return (info.flags & ElementFlagsRequired) == 0;
					});
				int requiredCount = firstOptionalIndex;
				if (firstOptionalIndex < 0) {
					requiredCount = restTuple->fixedLength;
				}
				if (requiredCount > 0) {
					minArgumentCount =
						static_cast<int>(signature->parameters.size()) - 1 +
						requiredCount;
				}
			}
		}
		if (minArgumentCount == -1) {
			if (strongArityForUntypedJS == 0 &&
				(signature->flags &
				 SignatureFlagsIsUntypedSignatureInJSFile) != 0) {
				return 0;
			}
			minArgumentCount = signature->minArgumentCount;
		}
		if (voidIsNonOptional != 0) {
			return minArgumentCount;
		}
		for (int i = minArgumentCount - 1; i >= 0; i--) {
			Type* t = getTypeAtPosition(signature, i);
			if (!someType(t, [](Type* t) {
					return (t->flags & TypeFlagsVoid) != 0;
				})) {
				break;
			}
			minArgumentCount = i;
		}
		signature->resolvedMinArgumentCount = minArgumentCount;
	}
	return signature->resolvedMinArgumentCount;
}

// relater.go:1782
bool Checker::hasEffectiveRestParameter(Signature* signature) {
	if (signatureHasRestParameter(signature)) {
		Type* restType = getTypeOfSymbol(
			signature->parameters[signature->parameters.size() - 1]);
		return !isTupleType(restType) ||
			   (restType->TargetTupleType()->combinedFlags &
				ElementFlagsVariable) != 0;
	}
	return false;
}

// relater.go:1790
Type* Checker::getTypeAtPosition(Signature* signature, int pos) {
	Type* t = tryGetTypeAtPosition(signature, pos);
	if (t != nullptr) {
		return t;
	}
	return anyType;
}

// relater.go:1798
Type* Checker::tryGetTypeAtPosition(Signature* signature, int pos) {
	int paramCount = static_cast<int>(signature->parameters.size()) -
					 (signatureHasRestParameter(signature) ? 1 : 0);
	if (pos < paramCount) {
		return getTypeOfParameter(signature->parameters[pos]);
	}
	if (signatureHasRestParameter(signature)) {
		// We want to return the value undefined for an out of bounds parameter
		// position, so we need to check bounds here before calling
		// getIndexedAccessType (which otherwise would return the type
		// 'undefined').
		Type* restType = getTypeOfSymbol(signature->parameters[paramCount]);
		int index = pos - paramCount;
		if (!isTupleType(restType) ||
			(restType->TargetTupleType()->combinedFlags &
			 ElementFlagsVariable) != 0 ||
			index < restType->TargetTupleType()->fixedLength) {
			return getIndexedAccessType(
				restType, getNumberLiteralType(Number(index)));
		}
	}
	return nullptr;
}

// Return the rest type at the given position, transforming `any[]` into just
// `any`. We do this because in signatures we want `any[]` in a rest position to
// be compatible with anything, but `any[]` isn't assignable to tuple types
// with required elements.
// relater.go:1819
Type* Checker::getRestOrAnyTypeAtPosition(Signature* source, int pos) {
	Type* restType = getRestTypeAtPosition(source, pos, false);
	if (restType != nullptr) {
		if (Type* elementType = getElementTypeOfArrayType(restType);
			elementType != nullptr && isTypeAny(elementType)) {
			return anyType;
		}
	}
	return restType;
}

// relater.go:1829
Type* Checker::getRestTypeAtPosition(Signature* source, int pos, bool readonly) {
	int parameterCount = getParameterCount(source);
	int minArgumentCount = getMinArgumentCount(source);
	Type* restType = getEffectiveRestType(source);
	if (restType != nullptr && pos >= parameterCount - 1) {
		if (pos == parameterCount - 1) {
			return restType;
		} else {
			return createArrayType(
				getIndexedAccessType(restType, numberType));
		}
	}
	int length = parameterCount - pos;
	if (length <= 0) {
		return createTupleTypeEx({}, {}, readonly);
	}
	std::vector<Type*> types(length);
	std::vector<TupleElementInfo> infos(length);
	for (int i = 0; i < length; i++) {
		ElementFlags flags;
		if (restType == nullptr || i < static_cast<int>(types.size()) - 1) {
			types[i] = getTypeAtPosition(source, i + pos);
			flags = i + pos < minArgumentCount ? ElementFlagsRequired
											 : ElementFlagsOptional;
		} else {
			types[i] = restType;
			flags = ElementFlagsVariadic;
		}
		infos[i] = TupleElementInfo{flags,
									getNameableDeclarationAtPosition(source,
																	 i + pos)};
	}
	return createTupleTypeEx(std::move(types), std::move(infos), readonly);
}

// relater.go:1860
Node* Checker::getNameableDeclarationAtPosition(Signature* signature, int pos) {
	int paramCount = static_cast<int>(signature->parameters.size()) -
					 (signatureHasRestParameter(signature) ? 1 : 0);
	if (pos < paramCount) {
		Node* decl = signature->parameters[pos]->valueDeclaration;
		if (decl != nullptr && isValidDeclarationForTupleLabel(decl)) {
			return decl;
		}
		return nullptr;
	}
	if (signatureHasRestParameter(signature)) {
		Symbol* restParameter = signature->parameters[paramCount];
		Type* restType = getTypeOfSymbol(restParameter);
		if (isTupleType(restType)) {
			const std::vector<TupleElementInfo>& elementInfos =
				restType->TargetTupleType()->elementInfos;
			int index = pos - paramCount;
			if (index < static_cast<int>(elementInfos.size())) {
				return elementInfos[index].labeledDeclaration;
			}
			return nullptr;
		}
		if (restParameter->valueDeclaration != nullptr &&
			isValidDeclarationForTupleLabel(restParameter->valueDeclaration)) {
			return restParameter->valueDeclaration;
		}
	}
	return nullptr;
}

// relater.go:1887
bool Checker::isValidDeclarationForTupleLabel(Node* d) {
	return isNamedTupleMember(d) ||
		   (isParameterDeclaration(d) && d->name() != nullptr &&
			isIdentifier(d->name()));
}

// relater.go:1891
Type* Checker::getNonArrayRestType(Signature* signature) {
	Type* restType = getEffectiveRestType(signature);
	if (restType != nullptr && !isArrayType(restType) && !isTypeAny(restType)) {
		return restType;
	}
	return nullptr;
}

// relater.go:1899
Type* Checker::getEffectiveRestType(Signature* signature) {
	if (signatureHasRestParameter(signature)) {
		Type* restType = getTypeOfSymbol(
			signature->parameters[signature->parameters.size() - 1]);
		if (!isTupleType(restType)) {
			if (isTypeAny(restType)) {
				return anyArrayType;
			}
			return restType;
		}
		if ((restType->TargetTupleType()->combinedFlags &
			 ElementFlagsVariable) != 0) {
			return sliceTupleType(restType,
								  restType->TargetTupleType()->fixedLength, 0);
		}
	}
	return nullptr;
}

// relater.go:1915
Type* Checker::sliceTupleType(Type* t, int index, int endSkipCount) {
	TupleType* target = t->TargetTupleType();
	int endIndex = getTypeReferenceArity(t) - std::max(endSkipCount, 0);
	if (index > target->fixedLength) {
		if (Type* restArrayType = getRestArrayTypeOfTupleType(t);
			restArrayType != nullptr) {
			return restArrayType;
		}
		return createTupleType({});
	}
	if (index >= endIndex) {
		return createTupleType({});
	}
	std::vector<Type*> typeArguments = getTypeArguments(t);
	std::vector<Type*> slicedTypes(typeArguments.begin() + index,
								   typeArguments.begin() + endIndex);
	std::vector<TupleElementInfo> slicedInfos(target->elementInfos.begin() + index,
											  target->elementInfos.begin() +
												  endIndex);
	return createTupleTypeEx(std::move(slicedTypes), std::move(slicedInfos),
							 false /*readonly*/);
}

// relater.go:1930
Type* Checker::getKnownKeysOfTupleType(Type* t) {
	int fixedLength = t->TargetTupleType()->fixedLength;
	std::vector<Type*> keys(fixedLength + 1);
	for (int i = 0; i < fixedLength; i++) {
		keys[i] = getStringLiteralType(std::to_string(i));
	}
	keys[fixedLength] = getIndexType(t->TargetTupleType()->readonly
										 ? globalReadonlyArrayType
										 : globalArrayType);
	return getUnionType(keys);
}

// relater.go:1940
Type* Checker::getRestArrayTypeOfTupleType(Type* t) {
	if (Type* restType = getRestTypeOfTupleType(t); restType != nullptr) {
		return createArrayType(restType);
	}
	return nullptr;
}

// relater.go:1947
Type* Checker::getThisTypeOfSignature(Signature* signature) {
	if (signature->thisParameter != nullptr) {
		return getTypeOfSymbol(signature->thisParameter);
	}
	return nullptr;
}

// relater.go:1954
bool Checker::isInstantiatedGenericParameter(Signature* signature, int pos) {
	if (signature->target == nullptr) {
		return false;
	}
	Type* t = tryGetTypeAtPosition(signature->target, pos);
	return t != nullptr && isGenericType(t);
}

// relater.go:1962
std::string Checker::getParameterNameAtPosition(Signature* signature, int pos) {
	int paramCount = static_cast<int>(signature->parameters.size()) -
					 (signatureHasRestParameter(signature) ? 1 : 0);
	if (pos < paramCount) {
		return signature->parameters[pos]->name;
	}
	Symbol* restParameter = signature->parameters[paramCount];
	Type* restType = getTypeOfSymbol(restParameter);
	if (isTupleType(restType)) {
		int index = pos - paramCount;
		return getTupleElementLabel(
			restType->TargetTupleType()->elementInfos[index], restParameter,
			index);
	}
	return restParameter->name;
}

// relater.go:1976
std::string Checker::getTupleElementLabel(const TupleElementInfo& elementInfo,
										  Symbol* restSymbol, int index) {
	if (elementInfo.labeledDeclaration != nullptr) {
		return elementInfo.labeledDeclaration->name()->text();
	}
	if (restSymbol != nullptr && restSymbol->valueDeclaration != nullptr &&
		isParameterDeclaration(restSymbol->valueDeclaration)) {
		return getTupleElementLabelFromBindingElement(
			restSymbol->valueDeclaration, index, elementInfo.flags);
	}
	std::string rootName;
	if (restSymbol != nullptr) {
		rootName = restSymbol->name;
	} else {
		rootName = "arg";
	}
	return rootName + "_" + std::to_string(index);
}

// relater.go:1992
std::string Checker::getTupleElementLabelFromBindingElement(
	Node* node, int index, ElementFlags elementFlags) {
	if (node->name() != nullptr) {
		switch (node->name()->kind) {
		case Kind::Identifier: {
			std::string name = node->name()->text();
			if (hasDotDotDotToken(node)) {
				// given
				//   (...[x, y, ...z]: [number, number, ...number[]]) => ...
				// this produces
				//   (x: number, y: number, ...z: number[]) => ...
				// which preserves rest elements of 'z'
				//
				// given
				//   (...[x, y, ...z]: [number, number, ...[...number[], number]]) => ...
				// this produces
				//   (x: number, y: number, ...z: number[], z_1: number) => ...
				// which preserves rest elements of z but gives distinct numbers
				// to fixed elements of 'z'
				if ((elementFlags & ElementFlagsVariable) != 0) {
					return name;
				}
				return name + "_" + std::to_string(index);
			}
			// given
			//   (...[x]: [number]) => ...
			// this produces
			//   (x: number) => ...
			// which preserves fixed elements of 'x'
			//
			// given
			//   (...[x]: ...number[]) => ...
			// this produces
			//   (x_0: number) => ...
			// which which numbers fixed elements of 'x' whose tuple element
			// type is variable
			if ((elementFlags & ElementFlagsFixed) != 0) {
				return name;
			}
			return name + "_n";
		}
		case Kind::ArrayBindingPattern:
			if (hasDotDotDotToken(node)) {
				const std::vector<Node*>& elements = node->name()->elements();
				Node* lastElement =
					elements.empty() ? nullptr : elements.back();
				bool lastElementIsBindingElementRest =
					lastElement != nullptr && isBindingElement(lastElement) &&
					hasDotDotDotToken(lastElement);
				int elementCount = static_cast<int>(elements.size()) -
								   (lastElementIsBindingElementRest ? 1 : 0);
				if (index < elementCount) {
					Node* element = elements[index];
					if (isBindingElement(element)) {
						return getTupleElementLabelFromBindingElement(
							element, index, elementFlags);
					}
				} else if (lastElementIsBindingElementRest) {
					return getTupleElementLabelFromBindingElement(
						lastElement, index - elementCount, elementFlags);
				}
			}
			break;
		default:
			break;
		}
	}
	return "arg_" + std::to_string(index);
}

// relater.go:2049
TypePredicate* Checker::getTypePredicateOfSignature(Signature* sig) {
	if (sig->resolvedTypePredicate == nullptr) {
		if (sig->target != nullptr) {
			TypePredicate* targetTypePredicate =
				getTypePredicateOfSignature(sig->target);
			if (targetTypePredicate != nullptr) {
				sig->resolvedTypePredicate =
					instantiateTypePredicate(targetTypePredicate, sig->mapper);
			}
		} else if (sig->composite != nullptr) {
			sig->resolvedTypePredicate = getUnionOrIntersectionTypePredicate(
				sig->composite->signatures, sig->composite->isUnion);
		} else {
			if (sig->declaration != nullptr) {
				Node* typeNode = sig->declaration->type();
				if (typeNode != nullptr) {
					if (isTypePredicateNode(typeNode)) {
						sig->resolvedTypePredicate =
							createTypePredicateFromTypePredicateNode(typeNode,
																	 sig);
					}
				} else if (isFunctionLikeDeclaration(sig->declaration) &&
						   (sig->resolvedReturnType == nullptr ||
							(sig->resolvedReturnType->flags &
							 TypeFlagsBoolean) != 0) &&
						   getParameterCount(sig) > 0) {
					sig->resolvedTypePredicate =
						noTypePredicate;  // avoid infinite loop
					sig->resolvedTypePredicate =
						getTypePredicateFromBody(sig->declaration);
				}
			}
		}
		if (sig->resolvedTypePredicate == nullptr) {
			sig->resolvedTypePredicate = noTypePredicate;
		}
	}
	if (sig->resolvedTypePredicate == noTypePredicate) {
		return nullptr;
	}
	return sig->resolvedTypePredicate;
}

// relater.go:2083
TypePredicate* Checker::getUnionOrIntersectionTypePredicate(
	const std::vector<Signature*>& signatures, bool isUnion) {
	TypePredicate* last = nullptr;
	std::vector<Type*> types;
	for (Signature* sig : signatures) {
		TypePredicate* pred = getTypePredicateOfSignature(sig);
		if (pred != nullptr) {
			// Constituent type predicates must all have matching kinds. We
			// don't create composite type predicates for assertions.
			if ((pred->kind != TypePredicateKind::This &&
				 pred->kind != TypePredicateKind::Identifier) ||
				(last != nullptr && !typePredicateKindsMatch(last, pred))) {
				return nullptr;
			}
			last = pred;
			types.push_back(pred->t);
		} else {
			// In composite union signatures we permit and ignore signatures
			// with a return type `false`.
			Type* returnType = nullptr;
			if (isUnion) {
				returnType = getReturnTypeOfSignature(sig);
			}
			if (returnType != falseType && returnType != regularFalseType) {
				return nullptr;
			}
		}
	}
	if (last == nullptr) {
		return nullptr;
	}
	Type* compositeType =
		getUnionOrIntersectionType(types, isUnion, UnionReductionLiteral);
	return newTypePredicate(last->kind, last->parameterName,
							last->parameterIndex, compositeType);
}

// relater.go:2113
bool Checker::typePredicateKindsMatch(TypePredicate* a, TypePredicate* b) {
	return a->kind == b->kind && a->parameterIndex == b->parameterIndex;
}

// relater.go:2117
TypePredicate* Checker::createTypePredicateFromTypePredicateNode(
	Node* node, Signature* signature) {
	TypePredicateNode* predicateNode = node->as<TypePredicateNode>();
	Type* t = nullptr;
	if (predicateNode->Type != nullptr) {
		t = getTypeFromTypeNode(predicateNode->Type);
	}
	if (isThisTypeNode(predicateNode->ParameterName)) {
		TypePredicateKind kind = predicateNode->AssertsModifier != nullptr
									 ? TypePredicateKind::AssertsThis
									 : TypePredicateKind::This;
		return newTypePredicate(kind, "" /*parameterName*/,
								0 /*parameterIndex*/, t);
	}
	TypePredicateKind kind = predicateNode->AssertsModifier != nullptr
								 ? TypePredicateKind::AssertsIdentifier
								 : TypePredicateKind::Identifier;
	std::string name = predicateNode->ParameterName->text();
	int index = findIndexOf(signature->parameters, [&](Symbol* p) {
		return p->name == name;
	});
	return newTypePredicate(kind, name, index, t);
}

// relater.go:2133
TypePredicate* Checker::instantiateTypePredicate(TypePredicate* predicate,
												 TypeMapper* mapper) {
	Type* t = instantiateType(predicate->t, mapper);
	if (t == predicate->t) {
		return predicate;
	}
	return newTypePredicate(predicate->kind, predicate->parameterName,
							predicate->parameterIndex, t);
}

// relater.go:2141
TypePredicate* Checker::newTypePredicate(TypePredicateKind kind,
										 const std::string& parameterName,
										 int32_t parameterIndex, Type* t) {
	return new TypePredicate{kind, parameterIndex, parameterName, t};
}

// relater.go:2145
bool Checker::isResolvingReturnTypeOfSignature(Signature* signature) {
	if (signature->composite != nullptr &&
		std::any_of(signature->composite->signatures.begin(),
					signature->composite->signatures.end(),
					[this](Signature* s) {
						return isResolvingReturnTypeOfSignature(s);
					})) {
		return true;
	}
	return signature->resolvedReturnType == nullptr &&
		   findResolutionCycleStartIndex(
			   signature,
			   TypeSystemPropertyName::ResolvedReturnType) >= 0;
}

// relater.go:2152
std::vector<Signature*> Checker::findMatchingSignatures(
	const std::vector<std::vector<Signature*>>& signatureLists,
	Signature* signature, int listIndex) {
	if (!signature->typeParameters.empty()) {
		// We require an exact match for generic signatures, so we only return
		// signatures from the first signature list and only if they have exact
		// matches in the other signature lists.
		if (listIndex > 0) {
			return {};
		}
		for (size_t i = 1; i < signatureLists.size(); i++) {
			if (findMatchingSignature(signatureLists[i], signature,
									  false /*partialMatch*/,
									  false /*ignoreThisTypes*/,
									  false /*ignoreReturnTypes*/) == nullptr) {
				return {};
			}
		}
		return {signature};
	}
	std::vector<Signature*> result;
	for (size_t i = 0; i < signatureLists.size(); i++) {
		// Allow matching non-generic signatures to have excess parameters (as
		// a fallback if exact parameter match is not found) and different
		// return types. Prefer matching this types if possible.
		Signature* match;
		if (static_cast<int>(i) == listIndex) {
			match = signature;
		} else {
			match = findMatchingSignature(signatureLists[i], signature,
										  false /*partialMatch*/,
										  false /*ignoreThisTypes*/,
										  true /*ignoreReturnTypes*/);
			if (match == nullptr) {
				match = findMatchingSignature(signatureLists[i], signature,
											  true /*partialMatch*/,
											  false /*ignoreThisTypes*/,
											  true /*ignoreReturnTypes*/);
			}
		}
		if (match == nullptr) {
			return {};
		}
		appendIfUnique(result, match);
	}
	return result;
}

// relater.go:2187
Signature* Checker::findMatchingSignature(
	const std::vector<Signature*>& signatureList, Signature* signature,
	bool partialMatch, bool ignoreThisTypes, bool ignoreReturnTypes) {
	std::function<Ternary(Type*, Type*)> compareTypes =
		partialMatch ? std::function<Ternary(Type*, Type*)>(
							   [this](Type* s, Type* t) {
								   return compareTypesSubtypeOf(s, t);
							   })
					 : std::function<Ternary(Type*, Type*)>(
							   [this](Type* s, Type* t) {
								   return compareTypesIdentical(s, t);
							   });
	for (Signature* s : signatureList) {
		if (compareSignaturesIdentical(s, signature, partialMatch,
									   ignoreThisTypes, ignoreReturnTypes,
									   compareTypes) != Ternary::False) {
			return s;
		}
	}
	return nullptr;
}

/**
 * See signatureRelatedTo, compareSignaturesIdentical
 */
// relater.go:2200
Ternary Checker::compareSignaturesIdentical(
	Signature* source, Signature* target, bool partialMatch,
	bool ignoreThisTypes, bool ignoreReturnTypes,
	const std::function<Ternary(Type*, Type*)>& compareTypes) {
	if (source == target) {
		return Ternary::True;
	}
	if (!isMatchingSignature(source, target, partialMatch)) {
		return Ternary::False;
	}
	// Check that the two signatures have the same number of type parameters.
	if (source->typeParameters.size() != target->typeParameters.size()) {
		return Ternary::False;
	}
	// Check that type parameter constraints and defaults match. If they do,
	// instantiate the source signature with the type parameters of the target
	// signature and continue the comparison.
	if (!target->typeParameters.empty()) {
		TypeMapper* mapper =
			newTypeMapper(source->typeParameters, target->typeParameters);
		for (size_t i = 0; i < target->typeParameters.size(); i++) {
			Type* s = source->typeParameters[i];
			Type* t = target->typeParameters[i];
			if (!(s == t ||
				  (compareTypes(
						   instantiateType(
							   getConstraintOrUnknownFromTypeParameter(s),
							   mapper),
						   getConstraintOrUnknownFromTypeParameter(t)) !=
					   Ternary::False &&
				   compareTypes(
					   instantiateType(
						   getDefaultOrUnknownFromTypeParameter(s), mapper),
					   getDefaultOrUnknownFromTypeParameter(t)) !=
					   Ternary::False))) {
				return Ternary::False;
			}
		}
		source = instantiateSignatureEx(source, mapper,
										true /*eraseTypeParameters*/);
	}
	Ternary result = Ternary::True;
	if (!ignoreThisTypes) {
		Type* sourceThisType = getThisTypeOfSignature(source);
		if (sourceThisType != nullptr) {
			Type* targetThisType = getThisTypeOfSignature(target);
			if (targetThisType != nullptr) {
				Ternary related = compareTypes(sourceThisType, targetThisType);
				if (related == Ternary::False) {
					return Ternary::False;
				}
				result &= related;
			}
		}
	}
	for (int i = 0; i < getParameterCount(target); i++) {
		Type* s = getTypeAtPosition(source, i);
		Type* t = getTypeAtPosition(target, i);
		Ternary related = compareTypes(t, s);
		if (related == Ternary::False) {
			return Ternary::False;
		}
		result &= related;
	}
	if (!ignoreReturnTypes) {
		TypePredicate* sourceTypePredicate = getTypePredicateOfSignature(source);
		TypePredicate* targetTypePredicate = getTypePredicateOfSignature(target);
		if (sourceTypePredicate != nullptr || targetTypePredicate != nullptr) {
			result &= compareTypePredicatesIdentical(sourceTypePredicate,
												   targetTypePredicate,
												   compareTypes);
		} else {
			result &= compareTypes(getReturnTypeOfSignature(source),
								   getReturnTypeOfSignature(target));
		}
	}
	return result;
}

// relater.go:2260
bool Checker::isMatchingSignature(Signature* source, Signature* target,
								  bool partialMatch) {
	int sourceParameterCount = getParameterCount(source);
	int targetParameterCount = getParameterCount(target);
	int sourceMinArgumentCount = getMinArgumentCount(source);
	int targetMinArgumentCount = getMinArgumentCount(target);
	bool sourceHasRestParameter = hasEffectiveRestParameter(source);
	bool targetHasRestParameter = hasEffectiveRestParameter(target);
	// A source signature matches a target signature if the two signatures have
	// the same number of required, optional, and rest parameters.
	if (sourceParameterCount == targetParameterCount &&
		sourceMinArgumentCount == targetMinArgumentCount &&
		sourceHasRestParameter == targetHasRestParameter) {
		return true;
	}
	// A source signature partially matches a target signature if the target
	// signature has no fewer required parameters
	if (partialMatch && sourceMinArgumentCount <= targetMinArgumentCount) {
		return true;
	}
	return false;
}

// relater.go:2280
bool Checker::compareTypeParametersIdentical(
	const std::vector<Type*>& sourceParams,
	const std::vector<Type*>& targetParams) {
	if (sourceParams.size() != targetParams.size()) {
		return false;
	}
	TypeMapper* mapper = newTypeMapper(targetParams, sourceParams);
	for (size_t i = 0; i < sourceParams.size(); i++) {
		Type* source = sourceParams[i];
		Type* target = targetParams[i];
		if (source == target) {
			continue;
		}
		// We instantiate the target type parameter constraints into the source
		// types so we can recognize `<T, U extends T>` as the same as
		// `<A, B extends A>`
		Type* sourceConstraint = getConstraintFromTypeParameter(source);
		if (sourceConstraint == nullptr) {
			sourceConstraint = unknownType;
		}
		Type* targetConstraint = getConstraintFromTypeParameter(target);
		if (targetConstraint == nullptr) {
			targetConstraint = unknownType;
		}
		if (!isTypeIdenticalTo(sourceConstraint,
							   instantiateType(targetConstraint, mapper))) {
			return false;
		}
		// We don't compare defaults - we just use the type parameter defaults
		// from the first signature that seems to match. It might make sense to
		// combine these defaults in the future, but doing so intelligently
		// requires knowing if the parameter is used covariantly or
		// contravariantly (so we intersect if it's used like a parameter or
		// union if used like a return type) and, since it's just an inference
		// _default_, just picking one arbitrarily works OK.
	}
	return true;
}

// relater.go:2303
Ternary Checker::compareTypePredicatesIdentical(
	TypePredicate* source, TypePredicate* target,
	const std::function<Ternary(Type*, Type*)>& compareTypes) {
	if (source == nullptr || target == nullptr ||
		!typePredicateKindsMatch(source, target)) {
		return Ternary::False;
	}
	if (source->t == target->t) {
		return Ternary::True;
	}
	if (source->t != nullptr && target->t != nullptr) {
		return compareTypes(source->t, target->t);
	}
	return Ternary::False;
}

// relater.go:2315
Type* Checker::getEffectiveConstraintOfIntersection(
	const std::vector<Type*>& types, bool targetIsUnion) {
	std::vector<Type*> constraints;
	bool hasDisjointDomainType = false;
	for (Type* t : types) {
		if (t->flags & TypeFlagsInstantiable) {
			// We keep following constraints as long as we have an instantiable
			// type that is known not to be circular or infinite (hence we stop
			// on index access types).
			Type* constraint = getConstraintOfType(t);
			while (constraint != nullptr &&
				   (constraint->flags &
					(TypeFlagsTypeParameter | TypeFlagsIndex |
					 TypeFlagsConditional)) != 0) {
				constraint = getConstraintOfType(constraint);
			}
			if (constraint != nullptr) {
				constraints.push_back(constraint);
				if (targetIsUnion) {
					constraints.push_back(t);
				}
			}
		} else if ((t->flags & TypeFlagsDisjointDomains) != 0 ||
				   IsEmptyAnonymousObjectType(t)) {
			hasDisjointDomainType = true;
		}
	}
	// If the target is a union type or if we are intersecting with types
	// belonging to one of the disjoint domains, we may end up producing a
	// constraint that hasn't been examined before.
	if (!constraints.empty() && (targetIsUnion || hasDisjointDomainType)) {
		if (hasDisjointDomainType) {
			// We add any types belong to one of the disjoint domains because
			// they might cause the final intersection operation to reduce the
			// union constraints.
			for (Type* t : types) {
				if ((t->flags & TypeFlagsDisjointDomains) != 0 ||
					IsEmptyAnonymousObjectType(t)) {
					constraints.push_back(t);
				}
			}
		}
		// The source types were normalized; ensure the result is normalized too.
		return getNormalizedType(
			getIntersectionTypeEx(constraints,
								  IntersectionFlagsNoConstraintReduction,
								  nullptr),
			false /*writing*/);
	}
	return nullptr;
}

// relater.go:2354
bool Checker::templateLiteralTypesDefinitelyUnrelated(TemplateLiteralType* source,
													  TemplateLiteralType* target) {
	// Two template literal types with differences in their starting or ending
	// text spans are definitely unrelated.
	const std::string& sourceStart = source->texts[0];
	const std::string& targetStart = target->texts[0];
	const std::string& sourceEnd = source->texts[source->texts.size() - 1];
	const std::string& targetEnd = target->texts[target->texts.size() - 1];
	size_t startLen = std::min(sourceStart.size(), targetStart.size());
	size_t endLen = std::min(sourceEnd.size(), targetEnd.size());
	return sourceStart.compare(0, startLen, targetStart, 0, startLen) != 0 ||
		   sourceEnd.compare(sourceEnd.size() - endLen, endLen, targetEnd,
							 targetEnd.size() - endLen, endLen) != 0;
}

// relater.go:2365
bool Checker::isTypeMatchedByTemplateLiteralType(
	Type* source, TemplateLiteralType* target,
	TypeComparer compareTypes) {
	std::vector<Type*> inferences =
		inferTypesFromTemplateLiteralType(source, target, compareTypes);
	if (!inferences.empty()) {
		for (size_t i = 0; i < inferences.size(); i++) {
			if (!isValidTypeForTemplateLiteralPlaceholder(
					inferences[i], target->types[i], compareTypes)) {
				return false;
			}
		}
		return true;
	}
	return false;
}

// relater.go:2378
std::vector<Type*> Checker::inferTypesFromTemplateLiteralType(
	Type* source, TemplateLiteralType* target,
	TypeComparer compareTypes) {
	if (source->flags & TypeFlagsStringLiteral) {
		return inferFromLiteralPartsToTemplateLiteral(
			{getStringLiteralValue(source)}, {}, target);
	}
	if (source->flags & TypeFlagsTemplateLiteral) {
		TemplateLiteralType* sourceTlt = source->AsTemplateLiteralType();
		if (sourceTlt->texts == target->texts) {
			return mapVecIndex(
				sourceTlt->types, [&](Type* s, size_t i) -> Type* {
					if (compareTypes(getBaseConstraintOrType(s),
									 getBaseConstraintOrType(target->types[i]),
									 false /*partialMatch*/) !=
						Ternary::False) {
						return s;
					}
					return getStringLikeTypeForType(s);
				});
		}
		return inferFromLiteralPartsToTemplateLiteral(sourceTlt->texts,
													  sourceTlt->types, target);
	}
	return {};
}

// This function infers from the text parts and type parts of a source literal
// to a target template literal. The number of text parts is always one more
// than the number of type parts, and a source string literal is treated as a
// source with one text part and zero type parts. The function returns an array
// of inferred string or template literal types corresponding to the
// placeholders in the target template literal, or undefined if the source
// doesn't match the target.
//
// We first check that the starting source text part matches the starting
// target text part, and that the ending source text part ends matches the
// ending target text part. We then iterate through the remaining target text
// parts, finding a match for each in the source and inferring string or
// template literal types created from the segments of the source that occur
// between the matches. During this iteration, seg holds the index of the
// current text part in the sourceTexts array and pos holds the current
// character position in the current text part.
//
// Consider inference from type `<<${string}>.<${number}-${number}>>` to type
// `<${string}.${string}>`, i.e.
//
//	sourceTexts = ['<<', '>.<', '-', '>>']
//	sourceTypes = [string, number, number]
//	target.texts = ['<', '.', '>']
//
// We first match '<' in the target to the start of '<<' in the source and '>'
// in the target to the end of '>>' in the source. The first match for the '.'
// in target occurs at character 1 in the source text part at index 1, and thus
// the first inference is the template literal type `<${string}>`. The
// remainder of the source makes up the second inference, the template literal
// type `<${number}-${number}>`.
// relater.go:2418
std::vector<Type*> Checker::inferFromLiteralPartsToTemplateLiteral(
	const std::vector<std::string>& sourceTexts,
	const std::vector<Type*>& sourceTypes, TemplateLiteralType* target) {
	int lastSourceIndex = static_cast<int>(sourceTexts.size()) - 1;
	const std::string& sourceStartText = sourceTexts[0];
	const std::string& sourceEndText = sourceTexts[lastSourceIndex];
	const std::vector<std::string>& targetTexts = target->texts;
	int lastTargetIndex = static_cast<int>(targetTexts.size()) - 1;
	const std::string& targetStartText = targetTexts[0];
	const std::string& targetEndText = targetTexts[lastTargetIndex];
	if ((lastSourceIndex == 0 &&
		 sourceStartText.size() <
			 targetStartText.size() + targetEndText.size()) ||
		!stringutil::HasPrefix(sourceStartText, targetStartText,
							   true /*caseSensitive*/) ||
		!stringutil::HasSuffix(sourceEndText, targetEndText,
							   true /*caseSensitive*/)) {
		return {};
	}
	std::string remainingEndText =
		sourceEndText.substr(0, sourceEndText.size() - targetEndText.size());
	int seg = 0;
	size_t pos = targetStartText.size();
	std::vector<Type*> matches;
	auto getSourceText = [&](int index) -> const std::string& {
		if (index < lastSourceIndex) {
			return sourceTexts[index];
		}
		return remainingEndText;
	};
	auto addMatch = [&](int s, size_t p) {
		Type* matchType;
		if (s == seg) {
			matchType = getStringLiteralType(stringutil::CombineSurrogatePairs(
				getSourceText(s).substr(pos, p - pos)));
		} else {
			std::vector<std::string> matchTexts(s - seg + 1);
			matchTexts[0] = sourceTexts[seg].substr(pos);
			std::copy(sourceTexts.begin() + seg + 1, sourceTexts.begin() + s,
					  matchTexts.begin() + 1);
			matchTexts[s - seg] = getSourceText(s).substr(0, p);
			matchType = getTemplateLiteralType(
				matchTexts,
				std::vector<Type*>(sourceTypes.begin() + seg,
								   sourceTypes.begin() + s));
		}
		matches.push_back(matchType);
		seg = s;
		pos = p;
	};
	for (int i = 1; i < lastTargetIndex; i++) {
		const std::string& delim = targetTexts[i];
		if (!delim.empty()) {
			int s = seg;
			size_t p = pos;
			for (;;) {
				size_t d = getSourceText(s).find(delim, p);
				if (d != std::string::npos) {
					p = d;
					break;
				}
				s++;
				if (s == static_cast<int>(sourceTexts.size())) {
					return {};
				}
				p = 0;
			}
			addMatch(s, p);
			pos += delim.size();
		} else if (const std::string& sourceText = getSourceText(seg);
				   pos < sourceText.size()) {
			// Consume one code point at a time, matching the string iterator
			// (`[x, ..._] = s`) rather than UTF-16 code-unit indexing (`s[0]`).
			// DecodeJSStringRuneSize is required rather than
			// utf8.DecodeRuneInString because a lone surrogate is stored as an
			// invalid-UTF-8 sentinel; utf8 would treat that as an error and
			// advance a single byte, breaking the sentinel into stray bytes,
			// whereas DecodeJSStringRune pulls the whole sentinel off as one
			// code point.
			//
			// This intentionally diverges from Strada, which advances one
			// UTF-16 code unit at a time (`s[0]` semantics) and therefore
			// splits a supplementary code point such as an emoji into its
			// surrogate halves. If we ever need to match that, expand
			// sourceTexts and targetTexts into code-unit space up front with a
			// SplitSurrogatePairs helper (the inverse of CombineSurrogatePairs)
			// and decode by code unit here; the CombineSurrogatePairs call in
			// addMatch already recombines captured halves back into canonical
			// form.
			size_t size = stringutil::DecodeJSStringRuneSize(
				std::string_view(sourceText).substr(pos));
			addMatch(seg, pos + size);
		} else if (seg < lastSourceIndex) {
			addMatch(seg + 1, 0);
		} else {
			return {};
		}
	}
	addMatch(lastSourceIndex, getSourceText(lastSourceIndex).size());
	return matches;
}

// relater.go:2502
Type* Checker::getStringLikeTypeForType(Type* t) {
	if ((t->flags & (TypeFlagsAny | TypeFlagsStringLike)) != 0) {
		return t;
	}
	return getTemplateLiteralType({"", ""}, {t});
}

// relater.go:2509
bool Checker::isValidTypeForTemplateLiteralPlaceholder(
	Type* source, Type* target, TypeComparer compareTypes) {
	if (target->flags & TypeFlagsIntersection) {
		return std::all_of(
			target->types().begin(), target->types().end(),
			[&](Type* t) {
				return t == emptyTypeLiteralType ||
					   isValidTypeForTemplateLiteralPlaceholder(source, t,
															  compareTypes);
			});
	}
	if ((target->flags & TypeFlagsString) != 0 ||
		compareTypes(source, target, false) != Ternary::False) {
		return true;
	}
	if (source->flags & TypeFlagsStringLiteral) {
		const std::string& value = getStringLiteralValue(source);
		return ((target->flags & TypeFlagsNumber) != 0 &&
				isValidNumberString(value, false /*roundTripOnly*/)) ||
			   ((target->flags & TypeFlagsBigInt) != 0 &&
				isValidBigIntString(value, false /*roundTripOnly*/)) ||
			   ((target->flags & (TypeFlagsBooleanLiteral | TypeFlagsNullable)) !=
					0 &&
				value == target->AsIntrinsicType()->intrinsicName) ||
			   ((target->flags & TypeFlagsStringMapping) != 0 &&
				isMemberOfStringMapping(source, target)) ||
			   ((target->flags & TypeFlagsTemplateLiteral) != 0 &&
				isTypeMatchedByTemplateLiteralType(
					source, target->AsTemplateLiteralType(), compareTypes));
	}
	if (source->flags & TypeFlagsTemplateLiteral) {
		TemplateLiteralType* sourceTlt = source->AsTemplateLiteralType();
		const std::vector<std::string>& texts = sourceTlt->texts;
		return texts.size() == 2 && texts[0].empty() && texts[1].empty() &&
			   compareTypes(sourceTlt->types[0], target, false) !=
				   Ternary::False;
	}
	return false;
}

// relater.go:2531
bool Checker::isMemberOfStringMapping(Type* source, Type* target) {
	if (target->flags & TypeFlagsAny) {
		return true;
	}
	if (target->flags & (TypeFlagsString | TypeFlagsTemplateLiteral)) {
		return isTypeAssignableTo(source, target);
	}
	if (target->flags & TypeFlagsStringMapping) {
		// We need to see whether applying the same mappings of the target onto
		// the source would produce an identical type *and* that it's compatible
		// with the inner-most non-string-mapped type.
		//
		// The intuition here is that if same mappings don't affect the source
		// at all, and the source is compatible with the unmapped target, then
		// they must still reside in the same domain.
		auto [mapped, inner] = applyTargetStringMappingToSource(source, target);
		return mapped == source && isMemberOfStringMapping(source, inner);
	}
	return false;
}

// relater.go:2551
std::pair<Type*, Type*> Checker::applyTargetStringMappingToSource(Type* source,
																Type* target) {
	Type* inner = target->AsStringMappingType()->target;
	if (inner->flags & TypeFlagsStringMapping) {
		auto applied = applyTargetStringMappingToSource(source, inner);
		source = applied.first;
		inner = applied.second;
	}
	return {getStringMappingType(target->symbol, source), inner};
}

// relater.go:2599
Relater* Checker::getRelater() {
	Relater* r = freeRelater;
	if (r == nullptr) {
		r = new Relater{};
		r->c = this;
	}
	freeRelater = r->next;
	return r;
}

// relater.go:2608
void Checker::putRelater(Relater* r) {
	// Go: *r = Relater{c, maybeKeys[:0], maybeKeysSet, sourceStack[:0],
	// targetStack[:0], freeRelater} — reset scalars and clear containers while
	// reusing their backing buffers (unordered_set::clear keeps buckets).
	r->relation = nullptr;
	r->errorNode = nullptr;
	r->errorChain = nullptr;
	r->relatedInfo.clear();
	r->maybeKeys.clear();
	r->maybeKeysSet.clear();
	r->sourceStack.clear();
	r->targetStack.clear();
	r->maybeCount = 0;
	r->sourceDepth = 0;
	r->targetDepth = 0;
	r->expandingFlags = ExpandingFlagsNone;
	r->overflow = false;
	r->relationCount = 0;
	r->next = freeRelater;
	freeRelater = r;
}

// checker.go:23902/23912
// checker.go:27094
extern int countTypes(Type* t);
// checker.go:29478 — def in checker_contextual.cpp
extern MappedTypeModifiers getMappedTypeModifiers(Type* t);
// utilities.go:757 — def in checker_decltypes.cpp
extern ModifierFlags getDeclarationModifierFlagsFromSymbol(Symbol* s);

// checker.go:23953 — isGenericTupleType (file-local replica; free function —
// the member overload wins inside Checker methods, matching Go)
bool isGenericTupleType(Type* t) {
	return isTupleType(t) &&
	       (t->TargetTupleType()->combinedFlags & ElementFlagsVariadic) != 0;
}

// checker.go:23950 — isMutableTupleType (file-local replica)
bool isMutableTupleType(Type* t) {
	return isTupleType(t) && !t->TargetTupleType()->readonly;
}

// checker.go:23958 — isSingleElementGenericTupleType (file-local replica)
bool isSingleElementGenericTupleType(Type* t) {
	return isGenericTupleType(t) && t->TargetTupleType()->elementInfos.size() == 1;
}

// checker.go:23902 — getStartElementCount (file-local replica)
int getStartElementCount(TupleType* t, ElementFlags flags) {
	for (size_t i = 0; i < t->elementInfos.size(); i++) {
		if (!(t->elementInfos[i].flags & flags)) {
			return static_cast<int>(i);
		}
	}
	return static_cast<int>(t->elementInfos.size());
}

// checker.go:23912 — getEndElementCount (file-local replica)
int getEndElementCount(TupleType* t, ElementFlags flags) {
	for (size_t i = t->elementInfos.size(); i > 0; i--) {
		if (!(t->elementInfos[i - 1].flags & flags)) {
			return static_cast<int>(t->elementInfos.size() - i);
		}
	}
	return static_cast<int>(t->elementInfos.size());
}

// checker.go:29526 — isPartialMappedType (file-local replica)
bool isPartialMappedType(Type* t) {
	return (t->objectFlags & ObjectFlagsMapped) != 0 &&
	       (getMappedTypeModifiers(t) & MappedTypeModifiersIncludeOptional) != 0;
}

// binder.go:374 — GetSymbolNameForPrivateIdentifier (file-local replica)
std::string getSymbolNameForPrivateIdentifier(Symbol* containingClassSymbol,
	                                         const std::string& description) {
	return std::string(1, kInternalSymbolNamePrefix) + "#" +
	       std::to_string(static_cast<uint64_t>(getSymbolId(containingClassSymbol))) +
	       "@" + description;
}

// binder.go:374 — def in checker_contextual.cpp
// checker.go:23950/23958/29528 — defs in checker_typenodes.cpp / checker_contextual.cpp

namespace {

// utilities.go:867 — file-local copy
bool isObjectLiteralType(Type* t) {
	return (t->objectFlags & ObjectFlagsObjectLiteral) != 0;
}

// ast/utilities.go:587 — file-local copy
bool isObjectLiteralElement(Node* node) {
	switch (node->kind) {
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::SpreadAssignment:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		return true;
	default:
		return false;
	}
}

// relater.go:2855
bool shouldCheckAsExcessProperty(Symbol* prop, Symbol* container) {
	return prop->valueDeclaration != nullptr &&
		   container->valueDeclaration != nullptr &&
		   prop->valueDeclaration->parent == container->valueDeclaration;
}

// relater.go:2859
bool isIgnoredJsxProperty(Type* source, Symbol* sourceProp) {
	return (source->objectFlags & ObjectFlagsJsxAttributes) != 0 &&
		   isHyphenatedJsxName(sourceProp->name);
}

// relater.go:4912
std::string addToDottedName(std::string_view head, std::string_view tail) {
	std::string headStr(head);
	if (stringutil::HasPrefix(headStr, "new ", true)) {
		headStr = "(" + headStr + ")";
	}
	size_t pos = 0;
	for (;;) {
		if (stringutil::HasPrefix(tail.substr(pos), "(", true)) {
			pos++;
		} else if (stringutil::HasPrefix(tail.substr(pos), "new ", true)) {
			pos += 4;
		} else {
			break;
		}
	}
	std::string prefix(tail.substr(0, pos));
	std::string suffix(tail.substr(pos));
	if (stringutil::HasPrefix(suffix, "[", true)) {
		return prefix + headStr + suffix;
	}
	return prefix + headStr + "." + suffix;
}

// relater.go:4959
std::string getPropertyNameArg(const std::string& arg) {
	if (!arg.empty() &&
		(arg[0] == '"' || arg[0] == '\'' || arg[0] == '`')) {
		return "[" + arg + "]";
	}
	return arg;
}

// relater.go:4967
bool isConversionOrInterfaceImplementationMessage(const DiagnosticMessage* message) {
	return message ==
			   Class_0_incorrectly_implements_interface_1 ||
		   message ==
			   
				   Class_0_incorrectly_implements_class_1_Did_you_mean_to_extend_1_and_inherit_its_members_as_a_subclass ||
		   message ==
			   
				   Conversion_of_type_0_to_type_1_may_be_a_mistake_because_neither_type_sufficiently_overlaps_with_the_other_If_this_was_intentional_convert_the_expression_to_unknown_first ||
		   message == Its_instance_type_0_is_not_a_valid_JSX_element ||
		   message == Its_return_type_0_is_not_a_valid_JSX_element ||
		   message == Its_element_type_0_is_not_a_valid_JSX_element;
}

// relater.go:4976
int chainDepth(ErrorChain* chain) {
	int depth = 0;
	for (; chain != nullptr; chain = chain->next) {
		depth++;
	}
	return depth;
}

std::string joinStrings(const std::vector<std::string>& items,
						const std::string& sep) {
	std::string result;
	for (size_t i = 0; i < items.size(); i++) {
		if (i != 0) {
			result += sep;
		}
		result += items[i];
	}
	return result;
}

}  // namespace

// relater.go:2621
Ternary Relater::isRelatedToSimple(Type* source, Type* target) {
	return isRelatedToEx(source, target, RecursionFlagsBoth,
						 false /*reportErrors*/, nullptr /*headMessage*/,
						 IntersectionStateNone);
}

// relater.go:2625
Ternary Relater::isRelatedToWorker(Type* source, Type* target, bool reportErrors) {
	return isRelatedToEx(source, target, RecursionFlagsBoth, reportErrors,
						 nullptr /*headMessage*/, IntersectionStateNone);
}

// relater.go:2629
Ternary Relater::isRelatedTo(Type* source, Type* target,
							 RecursionFlags recursionFlags, bool reportErrors) {
	return isRelatedToEx(source, target, recursionFlags, reportErrors,
						 nullptr /*headMessage*/, IntersectionStateNone);
}

// relater.go:2633
Ternary Relater::isRelatedToEx(Type* originalSource, Type* originalTarget,
							   RecursionFlags recursionFlags, bool reportErrors,
							   const DiagnosticMessage* headMessage,
							   IntersectionState intersectionState) {
	if (originalSource == originalTarget) {
		return Ternary::True;
	}
	// Before normalization: if `source` is type an object type, and `target` is primitive,
	// skip all the checks we don't need and just return `isSimpleTypeRelatedTo` result
	if ((originalSource->flags & TypeFlagsObject) != 0 &&
		(originalTarget->flags & TypeFlagsPrimitive) != 0) {
		if ((relation == c->comparableRelation &&
			 (originalTarget->flags & TypeFlagsNever) == 0 &&
			 c->isSimpleTypeRelatedTo(originalTarget, originalSource, relation,
									  nullptr)) ||
			c->isSimpleTypeRelatedTo(
				originalSource, originalTarget, relation,
				reportErrors
					? ErrorReporter{[this](const DiagnosticMessage* m,
										   const std::vector<std::string>& a) {
						reportError(m, a);
					}}
					: ErrorReporter{})) {
			return Ternary::True;
		}
		if (reportErrors) {
			reportErrorResults(originalSource, originalTarget, originalSource,
							   originalTarget, headMessage);
		}
		return Ternary::False;
	}
	// Normalize the source and target types: Turn fresh literal types into regular literal types,
	// turn deferred type references into regular type references, simplify indexed access and
	// conditional types, and resolve substitution types to either the substitution (on the source
	// side) or the type variable (on the target side).
	Type* source = c->getNormalizedType(originalSource, false /*writing*/);
	Type* target = c->getNormalizedType(originalTarget, true /*writing*/);
	if (source == target) {
		return Ternary::True;
	}
	if (relation == c->identityRelation) {
		if (source->flags != target->flags) {
			return Ternary::False;
		}
		if ((source->flags & TypeFlagsSingleton) != 0) {
			return Ternary::True;
		}
		traceUnionsOrIntersectionsTooLarge(source, target);
		return recursiveTypeRelatedTo(source, target, false /*reportErrors*/,
									  IntersectionStateNone, recursionFlags);
	}
	// We fastpath comparing a type parameter to exactly its constraint, as this is _super_ common,
	// and otherwise, for type parameters in large unions, causes us to need to compare the union to itself,
	// as we break down the _target_ union first, _then_ get the source constraint - so for every
	// member of the target, we attempt to find a match in the source. This avoids that in cases where
	// the target is exactly the constraint.
	if ((source->flags & TypeFlagsTypeParameter) != 0 &&
		c->getConstraintOfType(source) == target) {
		return Ternary::True;
	}
	// See if we're relating a definitely non-nullable type to a union that includes null and/or undefined
	// plus a single non-nullable type. If so, remove null and/or undefined from the target type.
	if ((source->flags & TypeFlagsDefinitelyNonNullable) != 0 &&
		(target->flags & TypeFlagsUnion) != 0) {
		const std::vector<Type*>& types = target->types();
		Type* candidate = nullptr;
		if (types.size() == 2 &&
			(types[0]->flags & TypeFlagsNullable) != 0) {
			candidate = types[1];
		} else if (types.size() == 3 &&
				   (types[0]->flags & TypeFlagsNullable) != 0 &&
				   (types[1]->flags & TypeFlagsNullable) != 0) {
			candidate = types[2];
		}
		if (candidate != nullptr &&
			(candidate->flags & TypeFlagsNullable) == 0) {
			target = c->getNormalizedType(candidate, true /*writing*/);
			if (source == target) {
				return Ternary::True;
			}
		}
	}
	if ((relation == c->comparableRelation &&
		 (target->flags & TypeFlagsNever) == 0 &&
		 c->isSimpleTypeRelatedTo(target, source, relation, nullptr)) ||
		c->isSimpleTypeRelatedTo(
			source, target, relation,
			reportErrors
				? ErrorReporter{[this](const DiagnosticMessage* m,
									   const std::vector<std::string>& a) {
					reportError(m, a);
				}}
				: ErrorReporter{})) {
		return Ternary::True;
	}
	if ((source->flags & TypeFlagsStructuredOrInstantiable) != 0 ||
		(target->flags & TypeFlagsStructuredOrInstantiable) != 0) {
		bool isPerformingExcessPropertyChecks =
			(intersectionState & IntersectionStateTarget) == 0 &&
			isObjectLiteralType(source) &&
			(source->objectFlags & ObjectFlagsFreshLiteral) != 0;
		if (isPerformingExcessPropertyChecks) {
			if (hasExcessProperties(source, target, reportErrors)) {
				if (reportErrors) {
					reportRelationError(headMessage, source,
										originalTarget->alias != nullptr
											? originalTarget
											: target);
				}
				return Ternary::False;
			}
		}
		bool isPerformingCommonPropertyChecks =
			(relation != c->comparableRelation || isUnitType(source)) &&
			(intersectionState & IntersectionStateTarget) == 0 &&
			(source->flags & (TypeFlagsPrimitive | TypeFlagsObject |
							  TypeFlagsIntersection)) != 0 &&
			source != c->globalObjectType &&
			(target->flags & (TypeFlagsObject | TypeFlagsIntersection)) != 0 &&
			c->isWeakType(target) &&
			(!c->getPropertiesOfType(source).empty() ||
			 c->typeHasCallOrConstructSignatures(source));
		bool isComparingJsxAttributes =
			(source->objectFlags & ObjectFlagsJsxAttributes) != 0;
		if (isPerformingCommonPropertyChecks &&
			!c->hasCommonProperties(source, target, isComparingJsxAttributes)) {
			if (reportErrors) {
				std::string sourceString = c->TypeToString(
					originalSource->alias != nullptr ? originalSource : source);
				std::string targetString = c->TypeToString(
					originalTarget->alias != nullptr ? originalTarget : target);
				const std::vector<Signature*>& calls =
					c->getSignaturesOfType(source, SignatureKind::Call);
				const std::vector<Signature*>& constructs =
					c->getSignaturesOfType(source, SignatureKind::Construct);
				if ((!calls.empty() &&
					 isRelatedTo(c->getReturnTypeOfSignature(calls[0]), target,
								 RecursionFlagsSource,
								 false /*reportErrors*/) != Ternary::False) ||
					(!constructs.empty() &&
					 isRelatedTo(c->getReturnTypeOfSignature(constructs[0]),
								 target, RecursionFlagsSource,
								 false /*reportErrors*/) != Ternary::False)) {
					reportError(
									Value_of_type_0_has_no_properties_in_common_with_type_1_Did_you_mean_to_call_it,
								{sourceString, targetString});
				} else {
					reportError(
									Type_0_has_no_properties_in_common_with_type_1,
								{sourceString, targetString});
				}
			}
			return Ternary::False;
		}
		traceUnionsOrIntersectionsTooLarge(source, target);
		bool skipCaching =
			((source->flags & TypeFlagsUnion) != 0 &&
			 source->types().size() < 4 &&
			 (target->flags & TypeFlagsUnion) == 0) ||
			((target->flags & TypeFlagsUnion) != 0 &&
			 target->types().size() < 4 &&
			 (source->flags & TypeFlagsStructuredOrInstantiable) == 0);
		Ternary result = Ternary::False;
		if (skipCaching) {
			result = unionOrIntersectionRelatedTo(source, target, reportErrors,
												  intersectionState);
		} else {
			result = recursiveTypeRelatedTo(source, target, reportErrors,
											intersectionState, recursionFlags);
		}
		if (result != Ternary::False) {
			return result;
		}
	}
	if (reportErrors) {
		reportErrorResults(originalSource, originalTarget, source, target,
						   headMessage);
	}
	return Ternary::False;
}

// relater.go:2747
bool Relater::hasExcessProperties(Type* source, Type* target, bool reportErrors) {
	if (!isExcessPropertyCheckTarget(target) ||
		(!c->noImplicitAny &&
		 (target->objectFlags & ObjectFlagsJSLiteral) != 0)) {
		// Disable excess property checks on JS literals to simulate having an implicit "index signature" - but only outside of noImplicitAny
		return false;
	}
	bool isComparingJsxAttributes =
		(source->objectFlags & ObjectFlagsJsxAttributes) != 0;
	if ((relation == c->assignableRelation ||
		 relation == c->comparableRelation) &&
		(c->isTypeSubsetOf(c->globalObjectType, target) ||
		 (!isComparingJsxAttributes && c->isEmptyObjectType(target)))) {
		return false;
	}
	Type* reducedTarget = target;
	std::vector<Type*> checkTypes;
	if ((target->flags & TypeFlagsUnion) != 0) {
		reducedTarget = c->findMatchingDiscriminantType(
			source, target, [this](Type* s, Type* t) { return isRelatedToSimple(s, t); });
		if (reducedTarget == nullptr) {
			reducedTarget = c->filterPrimitivesIfContainsNonPrimitive(target);
		}
		checkTypes = reducedTarget->Distributed();
	}
	for (Symbol* prop : c->getPropertiesOfType(source)) {
		if (shouldCheckAsExcessProperty(prop, source->symbol) &&
			!isIgnoredJsxProperty(source, prop)) {
			if (!c->isKnownProperty(reducedTarget, prop->name,
									isComparingJsxAttributes)) {
				if (reportErrors) {
					// Report error in terms of object types in the target as those are the only ones
					// we check in isKnownProperty.
					Type* errorTarget = c->filterType(
						reducedTarget, [this](Type* t) {
							return isExcessPropertyCheckTarget(t);
						});
					// We know *exactly* where things went wrong when comparing the types.
					// Use this property as the error node as this will be more helpful in
					// reasoning about what went wrong.
					if (errorNode == nullptr) {
						TSC_UNREACHABLE("No errorNode in hasExcessProperties");
					}
					if (isJsxAttributes(errorNode) ||
						isJsxOpeningLikeElement(errorNode) ||
						isJsxOpeningLikeElement(errorNode->parent)) {
						// JsxAttributes has an object-literal flag and undergo same type-assignablity check as normal object-literal.
						// However, using an object-literal error message will be very confusing to the users so we give different a message.
						if (prop->valueDeclaration != nullptr &&
							isJsxAttribute(prop->valueDeclaration) &&
							getSourceFileOfNode(errorNode) ==
								getSourceFileOfNode(
									prop->valueDeclaration->name())) {
							// Note that extraneous children (as in `<NoChild>extra</NoChild>`) don't pass this check,
							// since `children` is a Kind::PropertySignature instead of a Kind::JsxAttribute.
							errorNode = prop->valueDeclaration->name();
						}
						std::string propName = c->symbolToString(prop);
						Symbol* suggestionSymbol =
							c->getSuggestedSymbolForNonexistentJSXAttribute(
								propName, errorTarget);
						if (suggestionSymbol != nullptr) {
							reportError(
								
									Property_0_does_not_exist_on_type_1_Did_you_mean_2,
								{propName, c->TypeToString(errorTarget),
								 c->symbolToString(suggestionSymbol)});
						} else {
							reportError(
								Property_0_does_not_exist_on_type_1,
								{propName, c->TypeToString(errorTarget)});
						}
					} else {
						// use the property's value declaration if the property is assigned inside the literal itself
						Node* objectLiteralDeclaration = nullptr;
						if (source->symbol != nullptr) {
							objectLiteralDeclaration =
								source->symbol->declarations.empty()
									? nullptr
									: source->symbol->declarations[0];
						}
						std::string suggestion;
						if (prop->valueDeclaration != nullptr &&
							isObjectLiteralElement(prop->valueDeclaration) &&
							findAncestor(
								prop->valueDeclaration,
								[objectLiteralDeclaration](Node* d) {
									return d == objectLiteralDeclaration;
								}) != nullptr &&
							getSourceFileOfNode(objectLiteralDeclaration) ==
								getSourceFileOfNode(errorNode)) {
							Node* name = prop->valueDeclaration->name();
							errorNode = name;
							if (isIdentifier(name)) {
								suggestion =
									c->getSuggestionForNonexistentProperty(
										name->text(), errorTarget);
							}
						}
						if (!suggestion.empty()) {
							reportError(
								
									Object_literal_may_only_specify_known_properties_but_0_does_not_exist_in_type_1_Did_you_mean_to_write_2,
								{c->symbolToString(prop),
								 c->TypeToString(errorTarget), suggestion});
						} else {
							reportError(
								
									Object_literal_may_only_specify_known_properties_and_0_does_not_exist_in_type_1,
								{c->symbolToString(prop),
								 c->TypeToString(errorTarget)});
						}
					}
				}
				return true;
			}
			if (!checkTypes.empty() &&
				isRelatedTo(c->getTypeOfSymbol(prop),
							c->getTypeOfPropertyInTypes(checkTypes, prop->name),
							RecursionFlagsBoth,
							reportErrors) == Ternary::False) {
				if (reportErrors) {
					reportError(Types_of_property_0_are_incompatible,
								{c->symbolToString(prop)});
				}
				return true;
			}
		}
	}
	return false;
}

// relater.go:2829
Type* Checker::getTypeOfPropertyInTypes(const std::vector<Type*>& types,
										const std::string& name) {
	std::vector<Type*> propTypes;
	for (Type* t : types) {
		propTypes.push_back(getTypeOfPropertyInType(t, name));
	}
	return getUnionType(propTypes);
}

// relater.go:2837
Type* Checker::getTypeOfPropertyInType(Type* t, const std::string& name) {
	t = getApparentType(t);
	Symbol* prop = nullptr;
	if ((t->flags & TypeFlagsUnionOrIntersection) != 0) {
		prop = getPropertyOfUnionOrIntersectionType(t, name, false);
	} else {
		prop = getPropertyOfObjectType(t, name);
	}
	if (prop != nullptr) {
		return getTypeOfSymbol(prop);
	}
	IndexInfo* indexInfo = getApplicableIndexInfoForName(t, name);
	if (indexInfo != nullptr) {
		return indexInfo->valueType;
	}
	return undefinedType;
}

// relater.go:2863
bool Checker::isTypeSubsetOf(Type* source, Type* target) {
	return source == target || (source->flags & TypeFlagsNever) != 0 ||
		   ((target->flags & TypeFlagsUnion) != 0 &&
			isTypeSubsetOfUnion(source, target));
}

// relater.go:2867
bool Checker::isTypeSubsetOfUnion(Type* source, Type* target) {
	if ((source->flags & TypeFlagsUnion) != 0) {
		for (Type* t : source->types()) {
			if (!containsType(target->types(), t)) {
				return false;
			}
		}
		return true;
	}
	if ((source->flags & TypeFlagsEnumLike) != 0 &&
		getBaseTypeOfEnumLikeType(source) == target) {
		return true;
	}
	return containsType(target->types(), source);
}

// relater.go:2882
Ternary Relater::unionOrIntersectionRelatedTo(Type* source, Type* target,
											  bool reportErrors,
											  IntersectionState intersectionState) {
	// Note that these checks are specifically ordered to produce correct results. In particular,
	// we need to deconstruct unions before intersections (because unions are always at the top),
	// and we need to handle "each" relations before "some" relations for the same kind of type.
	if ((source->flags & TypeFlagsUnion) != 0) {
		if ((target->flags & TypeFlagsUnion) != 0) {
			// Intersections of union types are normalized into unions of intersection types, and such normalized
			// unions can get very large and expensive to relate. The following fast path checks if the source union
			// originated in an intersection. If so, and if that intersection contains the target type, then we know
			// the result to be true (for any two types A and B, A & B is related to both A and B).
			Type* sourceOrigin = source->AsUnionType()->origin;
			if (sourceOrigin != nullptr &&
				(sourceOrigin->flags & TypeFlagsIntersection) != 0 &&
				target->alias != nullptr &&
				containsType(sourceOrigin->types(), target)) {
				return Ternary::True;
			}
			// Similarly, in unions of unions the we preserve the original list of unions. This original list is often
			// much shorter than the normalized result, so we scan it in the following fast path.
			Type* targetOrigin = target->AsUnionType()->origin;
			if (targetOrigin != nullptr &&
				(targetOrigin->flags & TypeFlagsUnion) != 0 &&
				source->alias != nullptr &&
				containsType(targetOrigin->types(), source)) {
				return Ternary::True;
			}
		}
		if (relation == c->comparableRelation) {
			return someTypeRelatedToType(
				source, target,
				reportErrors && (source->flags & TypeFlagsPrimitive) == 0,
				intersectionState);
		}
		return eachTypeRelatedToType(
			source, target,
			reportErrors && (source->flags & TypeFlagsPrimitive) == 0,
			intersectionState);
	}
	if ((target->flags & TypeFlagsUnion) != 0) {
		return typeRelatedToSomeType(
			c->getRegularTypeOfObjectLiteral(source), target,
			reportErrors && (source->flags & TypeFlagsPrimitive) == 0 &&
				(target->flags & TypeFlagsPrimitive) == 0,
			intersectionState);
	}
	if ((target->flags & TypeFlagsIntersection) != 0) {
		return typeRelatedToEachType(source, target, reportErrors,
									 IntersectionStateTarget);
	}
	// Source is an intersection. For the comparable relation, if the target is a primitive type we hoist the
	// constraints of all non-primitive types in the source into a new intersection. We do this because the
	// intersection may further constrain the constraints of the non-primitive types. For example, given a type
	// parameter 'T extends 1 | 2', the intersection 'T & 1' should be reduced to '1' such that it doesn't
	// appear to be comparable to '2'.
	if (relation == c->comparableRelation &&
		(target->flags & TypeFlagsPrimitive) != 0) {
		std::vector<Type*> constraints =
			sameMapVec(source->types(), [this](Type* t) -> Type* {
				if ((t->flags & TypeFlagsInstantiable) != 0) {
					Type* constraint = c->getBaseConstraintOfType(t);
					if (constraint != nullptr) {
						return constraint;
					}
					return c->unknownType;
				}
				return t;
			});
		if (constraints != source->types()) {
			source = c->getIntersectionType(constraints);
			if ((source->flags & TypeFlagsNever) != 0) {
				return Ternary::False;
			}
			if ((source->flags & TypeFlagsIntersection) == 0) {
				Ternary result = isRelatedTo(source, target,
											 RecursionFlagsSource,
											 false /*reportErrors*/);
				if (result != Ternary::False) {
					return result;
				}
				return isRelatedTo(target, source, RecursionFlagsSource,
								   false /*reportErrors*/);
			}
		}
	}
	// Check to see if any constituents of the intersection are immediately related to the target.
	// Don't report errors though. Elaborating on whether a source constituent is related to the target is
	// not actually useful and leads to some confusing error messages. Instead, we rely on the caller
	// checking whether the full intersection viewed as an object is related to the target.
	return someTypeRelatedToType(source, target, false /*reportErrors*/,
								 IntersectionStateSource);
}

// relater.go:2951
Ternary Relater::someTypeRelatedToType(Type* source, Type* target,
									   bool reportErrors,
									   IntersectionState intersectionState) {
	const std::vector<Type*>& sourceTypes = source->types();
	if ((source->flags & TypeFlagsUnion) != 0 &&
		containsType(sourceTypes, target)) {
		return Ternary::True;
	}
	for (size_t i = 0; i < sourceTypes.size(); i++) {
		Ternary related = isRelatedToEx(sourceTypes[i], target,
										RecursionFlagsSource,
										reportErrors && i == sourceTypes.size() - 1,
										nullptr /*headMessage*/,
										intersectionState);
		if (related != Ternary::False) {
			return related;
		}
	}
	return Ternary::False;
}

// relater.go:2965
Ternary Relater::eachTypeRelatedToType(Type* source, Type* target,
									   bool reportErrors,
									   IntersectionState intersectionState) {
	Ternary result = Ternary::True;
	const std::vector<Type*>& sourceTypes = source->types();
	// We strip `undefined` from the target if the `source` trivially doesn't contain it for our correspondence-checking fastpath
	// since `undefined` is frequently added by optionality and would otherwise spoil a potentially useful correspondence
	Type* strippedTarget = getUndefinedStrippedTargetIfNeeded(source, target);
	std::vector<Type*> strippedTypes;
	if ((strippedTarget->flags & TypeFlagsUnion) != 0) {
		strippedTypes = strippedTarget->types();
	}
	for (size_t i = 0; i < sourceTypes.size(); i++) {
		Type* sourceType = sourceTypes[i];
		if ((strippedTarget->flags & TypeFlagsUnion) != 0 &&
			sourceTypes.size() >= strippedTypes.size() &&
			sourceTypes.size() % strippedTypes.size() == 0) {
			// many unions are mappings of one another; in such cases, simply comparing members at the same index can shortcut the comparison
			// such unions will have identical lengths, and their corresponding elements will match up. Another common scenario is where a large
			// union has a union of objects intersected with it. In such cases, if the input was, eg `("a" | "b" | "c") & (string | boolean | {} | {whatever})`,
			// the result will have the structure `"a" | "b" | "c" | "a" & {} | "b" & {} | "c" & {} | "a" & {whatever} | "b" & {whatever} | "c" & {whatever}`
			// - the resulting union has a length which is a multiple of the original union, and the elements correspond modulo the length of the original union
			Ternary related =
				isRelatedToEx(sourceType,
							  strippedTypes[i % strippedTypes.size()],
							  RecursionFlagsBoth, false /*reportErrors*/,
							  nullptr /*headMessage*/, intersectionState);
			if (related != Ternary::False) {
				result &= related;
				continue;
			}
		}
		Ternary related =
			isRelatedToEx(sourceType, target, RecursionFlagsSource,
						  reportErrors, nullptr /*headMessage*/,
						  intersectionState);
		if (related == Ternary::False) {
			return Ternary::False;
		}
		result &= related;
	}
	return result;
}

// relater.go:2997
Type* Relater::getUndefinedStrippedTargetIfNeeded(Type* source, Type* target) {
	if ((source->flags & TypeFlagsUnion) != 0 &&
		(target->flags & TypeFlagsUnion) != 0 &&
		(source->types()[0]->flags & TypeFlagsUndefined) == 0 &&
		(target->types()[0]->flags & TypeFlagsUndefined) != 0) {
		return c->extractTypesOfKind(target, ~TypeFlagsUndefined);
	}
	return target;
}

// relater.go:3004
Ternary Relater::typeRelatedToSomeType(Type* source, Type* target,
									   bool reportErrors,
									   IntersectionState intersectionState) {
	const std::vector<Type*>& targetTypes = target->types();
	if ((target->flags & TypeFlagsUnion) != 0) {
		if (containsType(targetTypes, source)) {
			return Ternary::True;
		}
		if (relation != c->comparableRelation &&
			(target->objectFlags & ObjectFlagsPrimitiveUnion) != 0 &&
			(source->flags & TypeFlagsEnumLiteral) == 0 &&
			((source->flags & (TypeFlagsStringLiteral |
							   TypeFlagsBooleanLiteral |
							   TypeFlagsBigIntLiteral)) != 0 ||
			 ((relation == c->subtypeRelation ||
			   relation == c->strictSubtypeRelation) &&
			  (source->flags & TypeFlagsNumberLiteral) != 0))) {
			// When relating a literal type to a union of primitive types, we know the relation is false unless
			// the union contains the base primitive type or the literal type in one of its fresh/regular forms.
			// We exclude numeric literals for non-subtype relations because numeric literals are assignable to
			// numeric enum literals with the same value. Similarly, we exclude enum literal types because
			// identically named enum types are related (see isEnumTypeRelatedTo). We exclude the comparable
			// relation in entirety because it needs to be checked in both directions.
			Type* alternateForm = nullptr;
			if (source == source->AsLiteralType()->regularType) {
				alternateForm = source->AsLiteralType()->freshType;
			} else {
				alternateForm = source->AsLiteralType()->regularType;
			}
			Type* primitive = nullptr;
			if ((source->flags & TypeFlagsStringLiteral) != 0) {
				primitive = c->stringType;
			} else if ((source->flags & TypeFlagsNumberLiteral) != 0) {
				primitive = c->numberType;
			} else if ((source->flags & TypeFlagsBigIntLiteral) != 0) {
				primitive = c->bigintType;
			}
			if ((primitive != nullptr && containsType(targetTypes, primitive)) ||
				(alternateForm != nullptr &&
				 containsType(targetTypes, alternateForm))) {
				return Ternary::True;
			}
			return Ternary::False;
		}
		Type* match = c->getMatchingUnionConstituentForType(target, source);
		if (match != nullptr) {
			Ternary related = isRelatedToEx(source, match, RecursionFlagsTarget,
											false /*reportErrors*/,
											nullptr /*headMessage*/,
											intersectionState);
			if (related != Ternary::False) {
				return related;
			}
		}
	}
	for (Type* t : targetTypes) {
		Ternary related = isRelatedToEx(source, t, RecursionFlagsTarget,
										false /*reportErrors*/,
										nullptr /*headMessage*/,
										intersectionState);
		if (related != Ternary::False) {
			return related;
		}
	}
	if (reportErrors) {
		// Elaborate only if we can find a best matching type in the target union
		Type* bestMatchingType = c->getBestMatchingType(
			source, target, [this](Type* s, Type* t) { return isRelatedToSimple(s, t); });
		if (bestMatchingType != nullptr) {
			isRelatedToEx(source, bestMatchingType, RecursionFlagsTarget,
						  true /*reportErrors*/, nullptr /*headMessage*/,
						  intersectionState);
		}
	}
	return Ternary::False;
}

// relater.go:3063
Ternary Relater::typeRelatedToEachType(Type* source, Type* target,
									   bool reportErrors,
									   IntersectionState intersectionState) {
	Ternary result = Ternary::True;
	for (Type* targetType : target->types()) {
		Ternary related = isRelatedToEx(source, targetType, RecursionFlagsTarget,
										reportErrors,
										nullptr /*headMessage*/,
										intersectionState);
		if (related == Ternary::False) {
			return Ternary::False;
		}
		result &= related;
	}
	return result;
}

// relater.go:3076
Ternary Relater::eachTypeRelatedToSomeType(Type* source, Type* target) {
	Ternary result = Ternary::True;
	for (Type* sourceType : source->types()) {
		Ternary related =
			typeRelatedToSomeType(sourceType, target, false /*reportErrors*/,
								  IntersectionStateNone);
		if (related == Ternary::False) {
			return Ternary::False;
		}
		result &= related;
	}
	return result;
}

// relater.go:3094
Ternary Relater::recursiveTypeRelatedTo(Type* source, Type* target,
										bool reportErrors,
										IntersectionState intersectionState,
										RecursionFlags recursionFlags) {
	if (overflow) {
		// Note that stack depth overflows can cause _any_ relation involving structured types to become false, so it is
		// important to have well-defined behavior even in cases that shouldn't normally occur.
		return Ternary::False;
	}
	auto keyAndConstrained =
		getRelationKey(source, target, intersectionState,
					   relation == c->identityRelation,
					   false /*ignoreConstraints*/);
	CacheKey id = keyAndConstrained.first;
	bool constrained = keyAndConstrained.second;
	RelationComparisonResult entry = relation->get(id);
	if (entry != RelationComparisonResult::None) {
		if (!(reportErrors &&
			  (entry & RelationComparisonResult::Failed) !=
			   RelationComparisonResult::None &&
			  (entry & RelationComparisonResult::Overflow) ==
			   RelationComparisonResult::None)) {
			c->reliabilityFlags |=
				entry & (RelationComparisonResult::ReportsUnmeasurable |
						 RelationComparisonResult::ReportsUnreliable);
			if (reportErrors &&
				(entry & RelationComparisonResult::Overflow) !=
				RelationComparisonResult::None) {
				reportError(
					Excessive_complexity_comparing_types_0_and_1,
					{c->TypeToString(source), c->TypeToString(target)});
			}
			if ((entry & RelationComparisonResult::Succeeded) !=
				RelationComparisonResult::None) {
				return Ternary::True;
			}
			return Ternary::False;
		}
		// We are elaborating errors and the cached result is a failure not due to a comparison overflow,
		// so we will do the comparison again to generate an error message.
	}
	if (relationCount <= 0) {
		overflow = true;
		return Ternary::False;
	}
	// If source and target are already being compared, consider them related with assumptions
	if (maybeKeysSet.count(id) != 0) {
		return Ternary::Maybe;
	}
	// A constrained key indicates that we have type references that reference constrained
	// type parameters. For such keys we also check against the key we would have gotten if all type parameters
	// were unconstrained.
	if (constrained) {
		CacheKey broadestEquivalentId =
			getRelationKey(source, target, intersectionState,
						   relation == c->identityRelation,
						   true /*ignoreConstraints*/)
				.first;
		if (maybeKeysSet.count(broadestEquivalentId) != 0) {
			return Ternary::Maybe;
		}
	}
	if (sourceStack.size() == 100 || targetStack.size() == 100) {
		// We stop relating if we reach 100 levels of nesting. This is a backstop to catch infinite recursion
		// that wasn't caught by isDeeplyNestedType. It will also stop relating types that truly are over 100
		// levels deep, but those are exceedingly rare.
		return Ternary::Maybe;
	}
	size_t maybeStart = maybeKeys.size();
	maybeKeys.push_back(id);
	maybeKeysSet.insert(id);
	ExpandingFlags saveExpandingFlags = expandingFlags;
	if ((recursionFlags & RecursionFlagsSource) != 0) {
		sourceStack.push_back(source);
		if ((expandingFlags & ExpandingFlagsSource) == 0 &&
			c->isDeeplyNestedType(source, sourceStack, 3)) {
			expandingFlags |= ExpandingFlagsSource;
		}
	}
	if ((recursionFlags & RecursionFlagsTarget) != 0) {
		targetStack.push_back(target);
		if ((expandingFlags & ExpandingFlagsTarget) == 0 &&
			c->isDeeplyNestedType(target, targetStack, 3)) {
			expandingFlags |= ExpandingFlagsTarget;
		}
	}
	RelationComparisonResult saveReliabilityFlags = c->reliabilityFlags;
	c->reliabilityFlags = RelationComparisonResult::None;
	Ternary result = Ternary::False;
	if (expandingFlags == ExpandingFlagsBoth) {
		if (Tracer* tr = c->tracer; tr != nullptr) {
			tr->Instant(tsc::tracing::PhaseCheckTypes,
						"recursiveTypeRelatedTo_DepthLimit",
						{{"sourceId", source->id},
						 {"targetId", target->id},
						 {"depth", static_cast<int>(sourceStack.size())},
						 {"targetDepth", static_cast<int>(targetStack.size())}});
		}
		result = Ternary::Maybe;
	} else {
		std::function<void()> popFn;
		if (Tracer* tr = c->tracer; tr != nullptr) {
			popFn = tr->Push(tsc::tracing::PhaseCheckTypes,
							 "structuredTypeRelatedTo",
							 {{"sourceId", source->id},
							  {"targetId", target->id}},
							 false);
		}
		struct PopGuard {
			std::function<void()>& f;
			~PopGuard() {
				if (f != nullptr) {
					f();
				}
			}
		} popGuard{popFn};
		result = structuredTypeRelatedTo(source, target, reportErrors,
										 intersectionState);
	}
	RelationComparisonResult propagatingVarianceFlags = c->reliabilityFlags;
	c->reliabilityFlags |= saveReliabilityFlags;
	if ((recursionFlags & RecursionFlagsSource) != 0) {
		sourceStack.pop_back();
	}
	if ((recursionFlags & RecursionFlagsTarget) != 0) {
		targetStack.pop_back();
	}
	expandingFlags = saveExpandingFlags;
	if (result != Ternary::False) {
		if (result == Ternary::True ||
			(sourceStack.empty() && targetStack.empty())) {
			if (result == Ternary::True || result == Ternary::Maybe) {
				// If result is definitely true, record all maybe keys as having succeeded. Also, record Ternary::Maybe
				// results as having succeeded once we reach depth 0, but never record Ternary::Unknown results.
				resetMaybeStack(maybeStart, propagatingVarianceFlags, true);
			} else {
				resetMaybeStack(maybeStart, propagatingVarianceFlags, false);
			}
		}
		// Note: it's intentional that we don't reset in the else case;
		// we leave them on the stack such that when we hit depth zero
		// above, we can report all of them as successful.
	} else {
		// A false result goes straight into global cache (when something is false under
		// assumptions it will also be false without assumptions)
		relation->set(id,
					  RelationComparisonResult::Failed | propagatingVarianceFlags);
		relationCount--;
		resetMaybeStack(maybeStart, propagatingVarianceFlags, false);
	}
	return result;
}

// relater.go:3201
void Relater::resetMaybeStack(int maybeStart,
							  RelationComparisonResult propagatingVarianceFlags,
							  bool markAllAsSucceeded) {
	for (size_t i = maybeStart; i < maybeKeys.size(); i++) {
		maybeKeysSet.erase(maybeKeys[i]);
		if (markAllAsSucceeded) {
			relation->set(maybeKeys[i],
						  RelationComparisonResult::Succeeded |
							  propagatingVarianceFlags);
			relationCount--;
		}
	}
	maybeKeys.resize(maybeStart);
}

// relater.go:3212
errorState Relater::getErrorState() {
	return errorState{errorChain, relatedInfo};
}

// relater.go:3219
void Relater::restoreErrorState(errorState e) {
	errorChain = e.errorChain;
	relatedInfo = e.relatedInfo;
}

// relater.go:3224
Ternary Relater::structuredTypeRelatedTo(Type* source, Type* target,
										 bool reportErrors,
										 IntersectionState intersectionState) {
	errorState saveErrorState = getErrorState();
	Ternary result = structuredTypeRelatedToWorker(source, target, reportErrors,
												   intersectionState);
	if (relation != c->identityRelation) {
		// The combined constraint of an intersection type is the intersection of the constraints of
		// the constituents. When an intersection type contains instantiable types with union type
		// constraints, there are situations where we need to examine the combined constraint. One is
		// when the target is a union type. Another is when the intersection contains types belonging
		// to one of the disjoint domains. For example, given type variables T and U, each with the
		// constraint 'string | number', the combined constraint of 'T & U' is 'string | number' and
		// we need to check this constraint against a union on the target side. Also, given a type
		// variable V constrained to 'string | number', 'V & number' has a combined constraint of
		// 'string & number | number & number' which reduces to just 'number'.
		// This also handles type parameters, as a type parameter with a union constraint compared against a union
		// needs to have its constraint hoisted into an intersection with said type parameter, this way
		// the type param can be compared with itself in the target (with the influence of its constraint to match other parts)
		// For example, if `T extends 1 | 2` and `U extends 2 | 3` and we compare `T & U` to `T & U & (1 | 2 | 3)`
		if (result == Ternary::False &&
			((source->flags & TypeFlagsIntersection) != 0 ||
			 ((source->flags & TypeFlagsTypeParameter) != 0 &&
			  (target->flags & TypeFlagsUnion) != 0))) {
			std::vector<Type*> sourceTypes;
			if ((source->flags & TypeFlagsIntersection) != 0) {
				sourceTypes = source->types();
			} else {
				sourceTypes = {source};
			}
			Type* constraint = c->getEffectiveConstraintOfIntersection(
				sourceTypes, (target->flags & TypeFlagsUnion) != 0);
			if (constraint != nullptr &&
				everyType(constraint,
						  [source](Type* cn) { return cn != source; })) {
				// TODO: Stack errors so we get a pyramid for the "normal" comparison above, _and_ a second for this
				result = isRelatedToEx(constraint, target, RecursionFlagsSource,
									   false /*reportErrors*/,
									   nullptr /*headMessage*/,
									   intersectionState);
			}
		}
		// When the target is an intersection we need an extra property check in order to detect nested excess
		// properties and nested weak types.
		if (result != Ternary::False &&
			(intersectionState & IntersectionStateTarget) == 0 &&
			(target->flags & TypeFlagsIntersection) != 0 &&
			!c->isGenericObjectType(target) &&
			(source->flags &
			 (TypeFlagsObject | TypeFlagsIntersection)) != 0) {
			result &=
				propertiesRelatedTo(source, target, reportErrors,
									{} /*excludedProperties*/,
									false /*optionalsOnly*/,
									IntersectionStateNone);
			if (result != Ternary::False && isObjectLiteralType(source) &&
				(source->objectFlags & ObjectFlagsFreshLiteral) != 0) {
				result &= indexSignaturesRelatedTo(
					source, target, false /*sourceIsPrimitive*/,
					reportErrors, IntersectionStateNone);
			}
		} else if (result != Ternary::False &&
				   c->isNonGenericObjectType(target) &&
				   !c->isArrayOrTupleType(target) &&
				   isSourceIntersectionNeedingExtraCheck(source, target)) {
			// When the source is an intersection we need an extra check of any optional properties in the target to
			// detect possible mismatched property types. For example:
			//
			//   function foo<T extends object>(x: { a?: string }, y: T & { a: boolean }) {
			//     x = y;  // Mismatched property in source intersection
			//   }
			result &= propertiesRelatedTo(source, target, reportErrors,
										  {} /*excludedProperties*/,
										  true /*optionalsOnly*/,
										  intersectionState);
		}
	}
	if (result != Ternary::False) {
		restoreErrorState(saveErrorState);
	}
	return result;
}

// relater.go:3286
bool Relater::isSourceIntersectionNeedingExtraCheck(Type* source, Type* target) {
	return (source->flags & TypeFlagsIntersection) != 0 &&
		   (c->getApparentType(source)->flags & TypeFlagsStructuredType) != 0 &&
		   !someOf(source->types(), [target](Type* t) {
			   return t == target ||
					  (t->objectFlags & ObjectFlagsNonInferrableType) != 0;
		   });
}

// relater.go:3293
Ternary Relater::structuredTypeRelatedToWorker(Type* source, Type* target,
											   bool reportErrors,
											   IntersectionState intersectionState) {
	Ternary result = Ternary::False;
	bool varianceCheckFailed = false;
	ErrorChain* originalErrorChain = nullptr;
	errorState saveErrorState = getErrorState();
	// Go: relateVariances is a closure returning (Ternary, ok bool); ok=false
	// means "fall through to the structural comparison below".
	auto relateVariances =
		[this, &result, &varianceCheckFailed, &originalErrorChain,
		 &saveErrorState,
		 reportErrors](const std::vector<Type*>& sourceTypeArguments,
					   const std::vector<Type*>& targetTypeArguments,
					   const std::vector<VarianceFlags>& variances,
					   IntersectionState intersectionState)
		-> std::pair<Ternary, bool> {
		result = typeArgumentsRelatedTo(sourceTypeArguments, targetTypeArguments,
										variances, reportErrors,
										intersectionState);
		if (result != Ternary::False) {
			return {result, true};
		}
		if (someOf(variances, [](VarianceFlags v) {
				return (v & VarianceFlagsAllowsStructuralFallback) != 0;
			})) {
			// If some type parameter was `Unmeasurable` or `Unreliable`, and we couldn't pass by assuming it was identical, then we
			// have to allow a structural fallback check
			// We elide the variance-based error elaborations, since those might not be too helpful, since we'll potentially
			// be assuming identity of the type parameter.
			originalErrorChain = nullptr;
			restoreErrorState(saveErrorState);
			return {Ternary::False, false};
		}
		bool allowStructuralFallback =
			c->hasCovariantVoidArgument(targetTypeArguments, variances);
		varianceCheckFailed = !allowStructuralFallback;
		// The type arguments did not relate appropriately, but it may be because we have no variance
		// information (in which case typeArgumentsRelatedTo defaulted to covariance for all type
		// arguments). It might also be the case that the target type has a 'void' type argument for
		// a covariant type parameter that is only used in return positions within the generic type
		// (in which case any type argument is permitted on the source side). In those cases we proceed
		// with a structural comparison. Otherwise, we know for certain the instantiations aren't
		// related and we can return here.
		if (!variances.empty() && !allowStructuralFallback) {
			// In some cases generic types that are covariant in regular type checking mode become
			// invariant in --strictFunctionTypes mode because one or more type parameters are used in
			// both co- and contravariant positions. In order to make it easier to diagnose *why* such
			// types are invariant, if any of the type parameters are invariant we reset the reported
			// errors and instead force a structural comparison (which will include elaborations that
			// reveal the reason).
			// We can switch on `reportErrors` here, since varianceCheckFailed guarantees we return `False`,
			// we can return `False` early here to skip calculating the structural error message we don't need.
			if (varianceCheckFailed &&
				!(reportErrors &&
				  someOf(variances, [](VarianceFlags v) {
					  return (v & VarianceFlagsVarianceMask) ==
							 VarianceFlagsInvariant;
				  }))) {
				return {Ternary::False, true};
			}
			// We remember the original error information so we can restore it in case the structural
			// comparison unexpectedly succeeds. This can happen when the structural comparison result
			// is a Ternary.Maybe for example caused by the recursion depth limiter.
			originalErrorChain = errorChain;
			restoreErrorState(saveErrorState);
		}
		return {Ternary::False, false};
	};
	if (relation == c->identityRelation) {
		// We've already checked that source.flags and target.flags are identical
		if ((source->flags & TypeFlagsUnionOrIntersection) != 0) {
			result = eachTypeRelatedToSomeType(source, target);
			if (result != Ternary::False) {
				result &= eachTypeRelatedToSomeType(target, source);
			}
			return result;
		}
		if ((source->flags & TypeFlagsIndex) != 0) {
			return isRelatedTo(source->Target(), target->Target(),
							   RecursionFlagsBoth, false /*reportErrors*/);
		}
		if ((source->flags & TypeFlagsIndexedAccess) != 0) {
			result = isRelatedTo(source->AsIndexedAccessType()->objectType,
								 target->AsIndexedAccessType()->objectType,
								 RecursionFlagsBoth, false /*reportErrors*/);
			if (result != Ternary::False) {
				result &= isRelatedTo(source->AsIndexedAccessType()->indexType,
									  target->AsIndexedAccessType()->indexType,
									  RecursionFlagsBoth,
									  false /*reportErrors*/);
				if (result != Ternary::False) {
					return result;
				}
			}
		} else if ((source->flags & TypeFlagsConditional) != 0) {
			if (source->AsConditionalType()->root->isDistributive ==
				target->AsConditionalType()->root->isDistributive) {
				result = isRelatedTo(source->AsConditionalType()->checkType,
									 target->AsConditionalType()->checkType,
									 RecursionFlagsBoth,
									 false /*reportErrors*/);
				if (result != Ternary::False) {
					result &= isRelatedTo(
						source->AsConditionalType()->extendsType,
						target->AsConditionalType()->extendsType,
						RecursionFlagsBoth, false /*reportErrors*/);
					if (result != Ternary::False) {
						result &= isRelatedTo(
							c->getTrueTypeFromConditionalType(source),
							c->getTrueTypeFromConditionalType(target),
							RecursionFlagsBoth, false /*reportErrors*/);
						if (result != Ternary::False) {
							result &= isRelatedTo(
								c->getFalseTypeFromConditionalType(source),
								c->getFalseTypeFromConditionalType(target),
								RecursionFlagsBoth, false /*reportErrors*/);
							if (result != Ternary::False) {
								return result;
							}
						}
					}
				}
			}
		} else if ((source->flags & TypeFlagsSubstitution) != 0) {
			result = isRelatedTo(source->AsSubstitutionType()->baseType,
								 target->AsSubstitutionType()->baseType,
								 RecursionFlagsBoth, false /*reportErrors*/);
			if (result != Ternary::False) {
				result &= isRelatedTo(source->AsSubstitutionType()->constraint,
									  target->AsSubstitutionType()->constraint,
									  RecursionFlagsBoth,
									  false /*reportErrors*/);
				if (result != Ternary::False) {
					return result;
				}
			}
		} else if ((source->flags & TypeFlagsTemplateLiteral) != 0) {
			if (source->AsTemplateLiteralType()->texts ==
				target->AsTemplateLiteralType()->texts) {
				result = Ternary::True;
				for (size_t i = 0;
					 i < source->AsTemplateLiteralType()->types.size(); i++) {
					Type* sourceType =
						source->AsTemplateLiteralType()->types[i];
					Type* targetType =
						target->AsTemplateLiteralType()->types[i];
					result &= isRelatedTo(sourceType, targetType,
										  RecursionFlagsBoth,
										  false /*reportErrors*/);
					if (result == Ternary::False) {
						return result;
					}
				}
				return result;
			}
		} else if ((source->flags & TypeFlagsStringMapping) != 0) {
			if (source->symbol ==
				target->symbol) {
				return isRelatedTo(source->AsStringMappingType()->target,
								   target->AsStringMappingType()->target,
								   RecursionFlagsBoth,
								   false /*reportErrors*/);
			}
		}
		if ((source->flags & TypeFlagsObject) == 0) {
			return Ternary::False;
		}
	} else if ((source->flags & TypeFlagsUnionOrIntersection) != 0 ||
			   (target->flags & TypeFlagsUnionOrIntersection) != 0) {
		result = unionOrIntersectionRelatedTo(source, target, reportErrors,
											  intersectionState);
		if (result != Ternary::False) {
			return result;
		}
		// The ordered decomposition above doesn't handle all cases. Specifically, we also need to handle:
		// Source is instantiable (e.g. source has union or intersection constraint).
		// Source is an object, target is a union (e.g. { a, b: boolean } <=> { a, b: true } | { a, b: false }).
		// Source is an intersection, target is an object (e.g. { a } & { b } <=> { a, b }).
		// Source is an intersection, target is a union (e.g. { a } & { b: boolean } <=> { a, b: true } | { a, b: false }).
		// Source is an intersection, target instantiable (e.g. string & { tag } <=> T["a"] constrained to string & { tag }).
		if (!((source->flags & TypeFlagsInstantiable) != 0 ||
			  ((source->flags & TypeFlagsObject) != 0 &&
			   (target->flags & TypeFlagsUnion) != 0) ||
			  ((source->flags & TypeFlagsIntersection) != 0 &&
			   (target->flags &
				(TypeFlagsObject | TypeFlagsUnion |
				 TypeFlagsInstantiable)) != 0))) {
			return Ternary::False;
		}
	}
	// We limit alias variance probing to only object and conditional types since their alias behavior
	// is more predictable than other, interned types, which may or may not have an alias depending on
	// the order in which things were checked.
	if ((source->flags & (TypeFlagsObject | TypeFlagsConditional)) != 0 &&
		source->alias != nullptr && !source->alias->typeArguments.empty() &&
		target->alias != nullptr &&
		source->alias->symbol == target->alias->symbol &&
		!(c->isMarkerType(source) || c->isMarkerType(target))) {
		std::vector<VarianceFlags> variances =
			c->getAliasVariances(source->alias->symbol);
		if (variances.empty()) {
			return Ternary::Unknown;
		}
		const std::vector<Type*>& params =
			c->typeAliasLinks.Get(source->alias->symbol)->typeParameters;
		int minParams = c->getMinTypeArgumentCount(params);
		bool nodeIsInJsFile =
			isInJSFile(source->alias->symbol->valueDeclaration);
		std::vector<Type*> sourceTypes = c->fillMissingTypeArguments(
			source->alias->typeArguments, params, minParams, nodeIsInJsFile);
		std::vector<Type*> targetTypes = c->fillMissingTypeArguments(
			target->alias->typeArguments, params, minParams, nodeIsInJsFile);
		auto varianceResult = relateVariances(sourceTypes, targetTypes,
											  variances, intersectionState);
		if (varianceResult.second) {
			return varianceResult.first;
		}
	}
	// For a generic type T and a type U that is assignable to T, [...U] is assignable to T, U is assignable to readonly [...T],
	// and U is assignable to [...T] when U is constrained to a mutable array or tuple type.
	if (isSingleElementGenericTupleType(source) &&
		!source->TargetTupleType()->readonly) {
		result = isRelatedTo(c->getTypeArguments(source)[0], target,
							 RecursionFlagsSource, false /*reportErrors*/);
		if (result != Ternary::False) {
			return result;
		}
	}
	if (isSingleElementGenericTupleType(target) &&
		(target->TargetTupleType()->readonly ||
		 c->isMutableArrayOrTuple(c->getBaseConstraintOrType(source)))) {
		result = isRelatedTo(source, c->getTypeArguments(target)[0],
							 RecursionFlagsTarget, false /*reportErrors*/);
		if (result != Ternary::False) {
			return result;
		}
	}
	if ((target->flags & TypeFlagsTypeParameter) != 0) {
		// A source type { [P in Q]: X } is related to a target type T if keyof T is related to Q and X is related to T[Q].
		if ((source->objectFlags & ObjectFlagsMapped) != 0 &&
			source->AsMappedType()->declaration->as<MappedTypeNode>()
					->NameType == nullptr &&
			isRelatedTo(c->getIndexType(target),
						c->getConstraintTypeFromMappedType(source),
						RecursionFlagsBoth, false) != Ternary::False) {
			if ((getMappedTypeModifiers(source) &
				 MappedTypeModifiersIncludeOptional) == 0) {
				Type* templateType =
					c->getTemplateTypeFromMappedType(source);
				Type* indexedAccessType = c->getIndexedAccessType(
					target, c->getTypeParameterFromMappedType(source));
				result = isRelatedTo(templateType, indexedAccessType,
									 RecursionFlagsBoth, reportErrors);
				if (result != Ternary::False) {
					return result;
				}
			}
		}
		if (relation == c->comparableRelation &&
			(source->flags & TypeFlagsTypeParameter) != 0) {
			// This is a carve-out in comparability to essentially forbid comparing a type parameter with another type parameter
			// unless one extends the other. (Remember: comparability is mostly bidirectional!)
			Type* constraint = c->getConstraintOfTypeParameter(source);
			if (constraint != nullptr &&
				someType(constraint, [](Type* cn) {
					return (cn->flags & TypeFlagsTypeParameter) != 0;
				})) {
				return isRelatedTo(constraint, target, RecursionFlagsSource,
								   false /*reportErrors*/);
			}
			return Ternary::False;
		}
	} else if ((target->flags & TypeFlagsIndexedAccess) != 0) {
		if ((source->flags & TypeFlagsIndexedAccess) != 0) {
			// Relate components directly before falling back to constraint relationships
			// A type S[K] is related to a type T[J] if S is related to T and K is related to J.
			result = isRelatedTo(source->AsIndexedAccessType()->objectType,
								 target->AsIndexedAccessType()->objectType,
								 RecursionFlagsBoth, reportErrors);
			if (result != Ternary::False) {
				result &= isRelatedTo(source->AsIndexedAccessType()->indexType,
									  target->AsIndexedAccessType()->indexType,
									  RecursionFlagsBoth, reportErrors);
			}
			if (result != Ternary::False) {
				return result;
			}
			if (reportErrors) {
				originalErrorChain = errorChain;
			}
		}
		// A type S is related to a type T[K] if S is related to C, where C is the base
		// constraint of T[K] for writing.
		if (relation == c->assignableRelation ||
			relation == c->comparableRelation) {
			Type* objectType = target->AsIndexedAccessType()->objectType;
			Type* indexType = target->AsIndexedAccessType()->indexType;
			Type* baseObjectType = c->getBaseConstraintOrType(objectType);
			Type* baseIndexType = c->getBaseConstraintOrType(indexType);
			if (!c->isGenericObjectType(baseObjectType) &&
				!c->isGenericIndexType(baseIndexType)) {
				AccessFlags accessFlags =
					AccessFlagsWriting |
					(baseObjectType != objectType
						 ? AccessFlagsNoIndexSignatures
						 : 0);
				Type* constraint = c->getIndexedAccessTypeOrUndefined(
					baseObjectType, baseIndexType, accessFlags,
					nullptr, nullptr);
				if (constraint != nullptr) {
					if (reportErrors && originalErrorChain != nullptr) {
						// create a new chain for the constraint error
						restoreErrorState(saveErrorState);
					}
					result = isRelatedToEx(source, constraint,
										   RecursionFlagsTarget, reportErrors,
										   nullptr /*headMessage*/,
										   intersectionState);
					if (result != Ternary::False) {
						return result;
					}
					// prefer the shorter chain of the constraint comparison chain, and the direct comparison chain
					if (reportErrors && originalErrorChain != nullptr &&
						errorChain != nullptr) {
						if (chainDepth(originalErrorChain) <=
							chainDepth(errorChain)) {
							errorChain = originalErrorChain;
						}
					}
				}
			}
		}
		if (reportErrors) {
			originalErrorChain = nullptr;
		}
	} else if ((target->flags & TypeFlagsIndex) != 0) {
		Type* targetType = target->AsIndexType()->target;
		// A keyof S is related to a keyof T if T is related to S.
		if ((source->flags & TypeFlagsIndex) != 0) {
			result = isRelatedTo(targetType, source->AsIndexType()->target,
								 RecursionFlagsBoth, false /*reportErrors*/);
			if (result != Ternary::False) {
				return result;
			}
		}
		if (isTupleType(targetType)) {
			// An index type can have a tuple type target when the tuple type contains variadic elements.
			// Check if the source is related to the known keys of the tuple type.
			result = isRelatedTo(source, c->getKnownKeysOfTupleType(targetType),
								 RecursionFlagsTarget, reportErrors);
			if (result != Ternary::False) {
				return result;
			}
		} else {
			// A type S is assignable to keyof T if S is assignable to keyof C, where C is the
			// simplified form of T or, if T doesn't simplify, the constraint of T.
			Type* constraint = c->getSimplifiedTypeOrConstraint(targetType);
			if (constraint != nullptr) {
				// We require Ternary.True here such that circular constraints don't cause
				// false positives. For example, given 'T extends { [K in keyof T]: string }',
				// 'keyof T' has itself as its constraint and produces a Ternary.Maybe when
				// related to other types.
				if (isRelatedTo(source,
								c->getIndexTypeEx(
									constraint,
									target->AsIndexType()->indexFlags |
										IndexFlagsNoReducibleCheck),
								RecursionFlagsTarget,
								reportErrors) == Ternary::True) {
					return Ternary::True;
				}
			} else if (c->isGenericMappedType(targetType)) {
				// generic mapped types that don't simplify or have a constraint still have a very simple set of keys we can compare against
				// - their nameType or constraintType.
				// In many ways, this comparison is a deferred version of what `getIndexTypeForMappedType` does to actually resolve the keys for _non_-generic types
				Type* nameType = c->getNameTypeFromMappedType(targetType);
				Type* constraintType =
					c->getConstraintTypeFromMappedType(targetType);
				Type* targetKeys;
				if (nameType != nullptr &&
					c->isMappedTypeWithKeyofConstraintDeclaration(targetType)) {
					// we need to get the apparent mappings and union them with the generic mappings, since some properties may be
					// missing from the `constraintType` which will otherwise be mapped in the object
					Type* mappedKeys =
						c->getApparentMappedTypeKeys(nameType, targetType);
					// We still need to include the non-apparent (and thus still generic) keys in the target side of the comparison (in case they're in the source side)
					targetKeys = c->getUnionType({mappedKeys, nameType});
				} else if (nameType != nullptr) {
					targetKeys = nameType;
				} else {
					targetKeys = constraintType;
				}
				if (isRelatedTo(source, targetKeys, RecursionFlagsTarget,
								reportErrors) == Ternary::True) {
					return Ternary::True;
				}
			}
		}
	} else if ((target->flags & TypeFlagsConditional) != 0) {
		// If we reach 10 levels of nesting for the same conditional type, assume it is an infinitely expanding recursive
		// conditional type and bail out with a Ternary.Maybe result.
		if (c->isDeeplyNestedType(target, targetStack, 10)) {
			return Ternary::Maybe;
		}
		ConditionalType* ct = target->AsConditionalType();
		// We check for a relationship to a conditional type target only when the conditional type has no
		// 'infer' positions, is not distributive or is distributive but doesn't reference the check type
		// parameter in either of the result types, and the source isn't an instantiation of the same
		// conditional type (as happens when computing variance).
		if (ct->root->inferTypeParameters.empty() &&
			!c->isDistributionDependent(ct->root) &&
			!((source->flags & TypeFlagsConditional) != 0 &&
			  source->AsConditionalType()->root == ct->root)) {
			// Check if the conditional is always true or always false but still deferred for distribution purposes.
			bool skipTrue = !c->isTypeAssignableTo(
				c->getPermissiveInstantiation(ct->checkType),
				c->getPermissiveInstantiation(ct->extendsType));
			bool skipFalse = !skipTrue &&
							 c->isTypeAssignableTo(
								 c->getRestrictiveInstantiation(ct->checkType),
								 c->getRestrictiveInstantiation(
									 ct->extendsType));
			// TODO: Find a nice way to include potential conditional type breakdowns in error output, if they seem good (they usually don't)
			if (skipTrue) {
				result = Ternary::True;
			} else {
				result = isRelatedToEx(
					source, c->getTrueTypeFromConditionalType(target),
					RecursionFlagsTarget, false /*reportErrors*/,
					nullptr /*headMessage*/, intersectionState);
			}
			if (result != Ternary::False) {
				if (skipFalse) {
					result &= Ternary::True;
				} else {
					result &= isRelatedToEx(
						source, c->getFalseTypeFromConditionalType(target),
						RecursionFlagsTarget, false /*reportErrors*/,
						nullptr /*headMessage*/, intersectionState);
				}
				if (result != Ternary::False) {
					return result;
				}
			}
		}
	} else if ((target->flags & TypeFlagsTemplateLiteral) != 0) {
		if ((source->flags & TypeFlagsTemplateLiteral) != 0) {
			if (relation == c->comparableRelation) {
				if (c->templateLiteralTypesDefinitelyUnrelated(
						source->AsTemplateLiteralType(),
						target->AsTemplateLiteralType())) {
					return Ternary::False;
				}
				return Ternary::True;
			}
			// Report unreliable variance for type variables referenced in template literal type placeholders.
			// For example, `foo-${number}` is related to `foo-${string}` even though number isn't related to string.
			c->instantiateType(source, c->reportUnreliableMapper);
		}
		if (c->isTypeMatchedByTemplateLiteralType(
				source, target->AsTemplateLiteralType(),
				[this](Type* s, Type* t, bool rpt) {
					return isRelatedToWorker(s, t, rpt);
				})) {
			return Ternary::True;
		}
	} else if ((target->flags & TypeFlagsStringMapping) != 0) {
		if ((source->flags & TypeFlagsStringMapping) == 0) {
			if (c->isMemberOfStringMapping(source, target)) {
				return Ternary::True;
			}
		}
	} else if (c->isGenericMappedType(target) &&
			   relation != c->identityRelation) {
		// Check if source type `S` is related to target type `{ [P in Q]: T }` or `{ [P in Q as R]: T}`.
		bool keysRemapped =
			target->AsMappedType()->declaration->as<MappedTypeNode>()
					->NameType != nullptr;
		Type* templateType = c->getTemplateTypeFromMappedType(target);
		MappedTypeModifiers modifiers = getMappedTypeModifiers(target);
		if ((modifiers & MappedTypeModifiersExcludeOptional) == 0) {
			// If the mapped type has shape `{ [P in Q]: T[P] }`,
			// source `S` is related to target if `T` = `S`, i.e. `S` is related to `{ [P in Q]: S[P] }`.
			if (!keysRemapped &&
				(templateType->flags & TypeFlagsIndexedAccess) != 0 &&
				templateType->AsIndexedAccessType()->objectType == source &&
				templateType->AsIndexedAccessType()->indexType ==
					c->getTypeParameterFromMappedType(target)) {
				return Ternary::True;
			}
			if (!c->isGenericMappedType(source)) {
				// If target has shape `{ [P in Q as R]: T}`, then its keys have type `R`.
				// If target has shape `{ [P in Q]: T }`, then its keys have type `Q`.
				Type* targetKeys;
				if (keysRemapped) {
					targetKeys = c->getNameTypeFromMappedType(target);
				} else {
					targetKeys = c->getConstraintTypeFromMappedType(target);
				}
				// Type of the keys of source type `S`, i.e. `keyof S`.
				Type* sourceKeys =
					c->getIndexTypeEx(source, IndexFlagsNoIndexSignatures);
				bool includeOptional =
					(modifiers & MappedTypeModifiersIncludeOptional) != 0;
				Type* filteredByApplicability = nullptr;
				if (includeOptional) {
					filteredByApplicability =
						c->intersectTypes(targetKeys, sourceKeys);
				}
				// A source type `S` is related to a target type `{ [P in Q]: T }` if `Q` is related to `keyof S` and `S[Q]` is related to `T`.
				// A source type `S` is related to a target type `{ [P in Q as R]: T }` if `R` is related to `keyof S` and `S[R]` is related to `T.
				// A source type `S` is related to a target type `{ [P in Q]?: T }` if some constituent `Q'` of `Q` is related to `keyof S` and `S[Q']` is related to `T`.
				// A source type `S` is related to a target type `{ [P in Q as R]?: T }` if some constituent `R'` of `R` is related to `keyof S` and `S[R']` is related to `T`.
				if ((includeOptional &&
					 (filteredByApplicability->flags & TypeFlagsNever) == 0) ||
					(!includeOptional &&
					 isRelatedTo(targetKeys, sourceKeys, RecursionFlagsBoth,
								 false) != Ternary::False)) {
					Type* templateTypeInner =
						c->getTemplateTypeFromMappedType(target);
					Type* typeParameter =
						c->getTypeParameterFromMappedType(target);
					// Fastpath: When the template type has the form `Obj[P]` where `P` is the mapped type parameter, directly compare source `S` with `Obj`
					// to avoid creating the (potentially very large) number of new intermediate types made by manufacturing `S[P]`.
					Type* nonNullComponent = c->extractTypesOfKind(
						templateTypeInner, ~TypeFlagsNullable);
					if (!keysRemapped &&
						(nonNullComponent->flags & TypeFlagsIndexedAccess) != 0 &&
						nonNullComponent->AsIndexedAccessType()->indexType ==
							typeParameter) {
						result = isRelatedTo(
							source,
							nonNullComponent->AsIndexedAccessType()->objectType,
							RecursionFlagsTarget, reportErrors);
						if (result != Ternary::False) {
							return result;
						}
					} else {
						// We need to compare the type of a property on the source type `S` to the type of the same property on the target type,
						// so we need to construct an indexing type representing a property, and then use indexing type to index the source type for comparison.
						// If the target type has shape `{ [P in Q]: T }`, then a property of the target has type `P`.
						// If the target type has shape `{ [P in Q]?: T }`, then a property of the target has type `P`,
						// but the property is optional, so we only want to compare properties `P` that are common between `keyof S` and `Q`.
						// If the target type has shape `{ [P in Q as R]: T }`, then a property of the target has type `R`.
						// If the target type has shape `{ [P in Q as R]?: T }`, then a property of the target has type `R`,
						// but the property is optional, so we only want to compare properties `R` that are common between `keyof S` and `R`.
						Type* indexingType = typeParameter;
						if (keysRemapped) {
							indexingType = filteredByApplicability != nullptr
											   ? filteredByApplicability
											   : targetKeys;
						} else if (filteredByApplicability != nullptr) {
							indexingType = c->getIntersectionType(
								{filteredByApplicability, typeParameter});
						}
						Type* indexedAccessType = c->getIndexedAccessType(
							source, indexingType);
						// Compare `S[indexingType]` to `T`, where `T` is the type of a property of the target type.
						result = isRelatedTo(indexedAccessType, templateType,
											 RecursionFlagsBoth, reportErrors);
						if (result != Ternary::False) {
							return result;
						}
					}
				}
				originalErrorChain = errorChain;
				restoreErrorState(saveErrorState);
			}
		}
	}
	if ((source->flags & TypeFlagsTypeVariable) != 0) {
		// IndexedAccess comparisons are handled above in the `target.flags&TypeFlagsIndexedAccess` branch
		if ((source->flags & TypeFlagsIndexedAccess) == 0 ||
			(target->flags & TypeFlagsIndexedAccess) == 0) {
			Type* constraint = c->getConstraintOfType(source);
			if (constraint == nullptr) {
				constraint = c->unknownType;
			}
			// hi-speed no-this-instantiation check (less accurate, but avoids costly `this`-instantiation when the constraint will suffice), see #28231 for report on why this is needed
			result = isRelatedToEx(constraint, target, RecursionFlagsSource,
								   false /*reportErrors*/,
								   nullptr /*headMessage*/, intersectionState);
			if (result != Ternary::False) {
				return result;
			}
			Type* constraintWithThis = c->getTypeWithThisArgument(
				constraint, source, false /*needApparentType*/);
			result = isRelatedToEx(
				constraintWithThis, target, RecursionFlagsSource,
				reportErrors && constraint != c->unknownType &&
					(target->flags & source->flags & TypeFlagsTypeParameter) == 0,
				nullptr /*headMessage*/, intersectionState);
			if (result != Ternary::False) {
				return result;
			}
			if (c->isMappedTypeGenericIndexedAccess(source)) {
				// For an indexed access type { [P in K]: E}[X], above we have already explored an instantiation of E with X
				// substituted for P. We also want to explore type { [P in K]: E }[C], where C is the constraint of X.
				Type* indexConstraint = c->getConstraintOfType(
					source->AsIndexedAccessType()->indexType);
				if (indexConstraint != nullptr) {
					result = isRelatedTo(
						c->getIndexedAccessType(
							source->AsIndexedAccessType()->objectType,
							indexConstraint),
						target, RecursionFlagsSource, reportErrors);
					if (result != Ternary::False) {
						return result;
					}
				}
			}
		}
	} else if ((source->flags & TypeFlagsIndex) != 0) {
		bool isDeferredMappedIndex =
			c->shouldDeferIndexType(source->AsIndexType()->target,
									source->AsIndexType()->indexFlags) &&
			(source->AsIndexType()->target->objectFlags &
			 ObjectFlagsMapped) != 0;
		result = isRelatedTo(c->stringNumberSymbolType, target,
							 RecursionFlagsSource,
							 reportErrors && !isDeferredMappedIndex);
		if (result != Ternary::False) {
			return result;
		}
		if (isDeferredMappedIndex) {
			Type* mappedType = source->AsIndexType()->target;
			Type* nameType = c->getNameTypeFromMappedType(mappedType);
			// Unlike on the target side, on the source side we do *not* include the generic part of the `nameType`, since that comes from a
			// (potentially anonymous) mapped type local type parameter, so that'd never assign outside the mapped type body, but we still want to
			// allow assignments of index types of identical (or similar enough) mapped types.
			// eg, `keyof {[X in keyof A]: Obj[X]}` should be assignable to `keyof {[Y in keyof A]: Tup[Y]}` because both map over the same set of keys (`keyof A`).
			// Without this source-side breakdown, a `keyof {[X in keyof A]: Obj[X]}` style type won't be assignable to anything except itself, which is much too strict.
			Type* sourceMappedKeys;
			if (nameType != nullptr &&
				c->isMappedTypeWithKeyofConstraintDeclaration(mappedType)) {
				sourceMappedKeys =
					c->getApparentMappedTypeKeys(nameType, mappedType);
			} else if (nameType != nullptr) {
				sourceMappedKeys = nameType;
			} else {
				sourceMappedKeys =
					c->getConstraintTypeFromMappedType(mappedType);
			}
			result = isRelatedTo(sourceMappedKeys, target, RecursionFlagsSource,
								 reportErrors);
			if (result != Ternary::False) {
				return result;
			}
		}
	} else if ((source->flags & TypeFlagsConditional) != 0) {
		// If we reach 10 levels of nesting for the same conditional type, assume it is an infinitely expanding recursive
		// conditional type and bail out with a Ternary.Maybe result.
		if (c->isDeeplyNestedType(source, sourceStack, 10)) {
			return Ternary::Maybe;
		}
		if ((target->flags & TypeFlagsConditional) != 0) {
			// Two conditional types 'T1 extends U1 ? X1 : Y1' and 'T2 extends U2 ? X2 : Y2' are related if
			// one of T1 and T2 is related to the other, U1 and U2 are identical types, X1 is related to X2,
			// and Y1 is related to Y2.
			const std::vector<Type*>& sourceParams =
				source->AsConditionalType()->root->inferTypeParameters;
			Type* sourceExtends = source->AsConditionalType()->extendsType;
			TypeMapper* mapper = nullptr;
			if (!sourceParams.empty()) {
				// If the source has infer type parameters, we instantiate them in the context of the target
				InferenceContext* ctx = c->newInferenceContext(
					sourceParams, nullptr /*signature*/, InferenceFlagsNone,
					[this](Type* s, Type* t, bool rpt) {
						return isRelatedToWorker(s, t, rpt);
					});
				c->inferTypes(ctx->inferences,
							  target->AsConditionalType()->extendsType,
							  sourceExtends,
							  InferencePriorityNoConstraints |
								  InferencePriorityAlwaysStrict,
							  false);
				sourceExtends = c->instantiateType(sourceExtends, ctx->mapper);
				mapper = ctx->mapper;
			}
			if (c->isTypeIdenticalTo(sourceExtends,
									 target->AsConditionalType()->extendsType) &&
				(isRelatedTo(source->AsConditionalType()->checkType,
							 target->AsConditionalType()->checkType,
							 RecursionFlagsBoth, false) != Ternary::False ||
				 isRelatedTo(target->AsConditionalType()->checkType,
							 source->AsConditionalType()->checkType,
							 RecursionFlagsBoth, false) != Ternary::False)) {
				result = isRelatedTo(
					c->instantiateType(
						c->getTrueTypeFromConditionalType(source), mapper),
					c->getTrueTypeFromConditionalType(target),
					RecursionFlagsBoth, reportErrors);
				if (result != Ternary::False) {
					result &= isRelatedTo(
						c->getFalseTypeFromConditionalType(source),
						c->getFalseTypeFromConditionalType(target),
						RecursionFlagsBoth, reportErrors);
				}
				if (result != Ternary::False) {
					return result;
				}
			}
		}
		// conditionals can be related to one another via normal constraint, as, eg, `A extends B ? O : never` should be assignable to `O`
		// when `O` is a conditional (`never` is trivially assignable to `O`, as is `O`!).
		Type* defaultConstraint =
			c->getDefaultConstraintOfConditionalType(source);
		if (defaultConstraint != nullptr) {
			result = isRelatedTo(defaultConstraint, target,
								 RecursionFlagsSource, reportErrors);
			if (result != Ternary::False) {
				return result;
			}
		}
		// conditionals aren't related to one another via distributive constraint as it is much too inaccurate and allows way
		// more assignments than are desirable (since it maps the source check type to its constraint, it loses information).
		if ((target->flags & TypeFlagsConditional) == 0 &&
			c->hasNonCircularBaseConstraint(source)) {
			Type* distributiveConstraint =
				c->getConstraintOfDistributiveConditionalType(source);
			if (distributiveConstraint != nullptr) {
				restoreErrorState(saveErrorState);
				result = isRelatedTo(distributiveConstraint, target,
									 RecursionFlagsSource, reportErrors);
				if (result != Ternary::False) {
					return result;
				}
			}
		}
	} else if ((source->flags & TypeFlagsTemplateLiteral) != 0 &&
			   (target->flags & TypeFlagsObject) == 0) {
		if ((target->flags & TypeFlagsTemplateLiteral) == 0) {
			Type* constraint = c->getBaseConstraintOfType(source);
			if (constraint != nullptr && constraint != source) {
				result = isRelatedTo(constraint, target, RecursionFlagsSource,
									 reportErrors);
				if (result != Ternary::False) {
					return result;
				}
			}
		}
	} else if ((source->flags & TypeFlagsStringMapping) != 0) {
		if ((target->flags & TypeFlagsStringMapping) != 0) {
			if (source->symbol !=
				target->symbol) {
				return Ternary::False;
			}
			result = isRelatedTo(source->AsStringMappingType()->target,
								 target->AsStringMappingType()->target,
								 RecursionFlagsBoth, reportErrors);
			if (result != Ternary::False) {
				return result;
			}
		} else {
			Type* constraint = c->getBaseConstraintOfType(source);
			if (constraint != nullptr) {
				result = isRelatedTo(constraint, target, RecursionFlagsSource,
									 reportErrors);
				if (result != Ternary::False) {
					return result;
				}
			}
		}
	} else {
		// An empty object type is related to any mapped type that includes a '?' modifier.
		if (relation != c->subtypeRelation &&
			relation != c->strictSubtypeRelation &&
			isPartialMappedType(target) && c->isEmptyObjectType(source)) {
			return Ternary::True;
		}
		if (c->isGenericMappedType(target)) {
			if (c->isGenericMappedType(source)) {
				result = mappedTypeRelatedTo(source, target, reportErrors);
				if (result != Ternary::False) {
					return result;
				}
			}
			return Ternary::False;
		}
		bool sourceIsPrimitive = (source->flags & TypeFlagsPrimitive) != 0;
		if (relation != c->identityRelation) {
			source = c->getApparentType(source);
		} else if (c->isGenericMappedType(source)) {
			return Ternary::False;
		}
		if ((source->objectFlags & ObjectFlagsReference) != 0 &&
			(target->objectFlags & ObjectFlagsReference) != 0 &&
			source->Target() == target->Target() && !isTupleType(source) &&
			!c->isMarkerType(source) && !c->isMarkerType(target)) {
			// When strictNullChecks is disabled, the element type of the empty array literal is undefinedWideningType,
			// and an empty array literal wouldn't be assignable to a `never[]` without this check.
			if (c->isEmptyArrayLiteralType(source)) {
				return Ternary::True;
			}
			// We have type references to the same generic type, and the type references are not marker
			// type references (which are intended by be compared structurally). Obtain the variance
			// information for the type parameters and relate the type arguments accordingly.
			std::vector<VarianceFlags> variances =
				c->getVariances(source->Target());
			// We return Ternary.Maybe for a recursive invocation of getVariances (signaled by emptyArray). This
			// effectively means we measure variance only from type parameter occurrences that aren't nested in
			// recursive instantiations of the generic type.
			if (variances.empty()) {
				return Ternary::Unknown;
			}
			auto varianceResult = relateVariances(
				c->getTypeArguments(source), c->getTypeArguments(target),
				variances, intersectionState);
			if (varianceResult.second) {
				return varianceResult.first;
			}
		} else if (c->isArrayType(target) &&
				   ((c->isReadonlyArrayType(target) &&
					 everyType(source, [this](Type* t) {
						 return c->isArrayOrTupleType(t);
					 })) ||
					everyType(source, [](Type* t) {
						return isMutableTupleType(t);
					}))) {
			if (relation != c->identityRelation) {
				return isRelatedTo(
					c->getIndexTypeOfTypeEx(source, c->numberType, c->anyType),
					c->getIndexTypeOfTypeEx(target, c->numberType, c->anyType),
					RecursionFlagsBoth, reportErrors);
			}
			// By flags alone, we know that the `target` is a readonly array while the source is a normal array or tuple
			// or `target` is an array and source is a tuple - in both cases the types cannot be identical, by construction
			return Ternary::False;
		} else if (c->isGenericTupleType(source) && isTupleType(target) &&
				   !c->isGenericTupleType(target)) {
			Type* constraint = c->getBaseConstraintOrType(source);
			if (constraint != source) {
				return isRelatedTo(constraint, target, RecursionFlagsSource,
								   reportErrors);
			}
		} else if ((relation == c->subtypeRelation ||
					relation == c->strictSubtypeRelation) &&
				   c->isEmptyObjectType(target) &&
				   (target->objectFlags & ObjectFlagsFreshLiteral) != 0 &&
				   !c->isEmptyObjectType(source)) {
			return Ternary::False;
		}
		// Even if relationship doesn't hold for unions, intersections, or generic type references,
		// it may hold in a structural comparison.
		// In a check of the form X = A & B, we will have previously checked if A relates to X or B relates
		// to X. Failing both of those we want to check if the aggregation of A and B's members structurally
		// relates to X. Thus, we include intersection types on the source side here.
		if ((source->flags & (TypeFlagsObject | TypeFlagsIntersection)) != 0 &&
			(target->flags & TypeFlagsObject) != 0) {
			// Report structural errors only if we haven't reported any errors yet
			bool reportStructuralErrors =
				reportErrors &&
				errorChain == saveErrorState.errorChain && !sourceIsPrimitive;
			result = propertiesRelatedTo(source, target,
										 reportStructuralErrors,
										 {} /*excludedProperties*/,
										 false /*optionalsOnly*/,
										 intersectionState);
			if (result != Ternary::False) {
				result &= signaturesRelatedTo(source, target,
											  SignatureKind::Call,
											  reportStructuralErrors,
											  intersectionState);
				if (result != Ternary::False) {
					result &= signaturesRelatedTo(source, target,
												  SignatureKind::Construct,
												  reportStructuralErrors,
												  intersectionState);
					if (result != Ternary::False) {
						result &= indexSignaturesRelatedTo(
							source, target, sourceIsPrimitive,
							reportStructuralErrors, intersectionState);
					}
				}
			}
			if (result != Ternary::False) {
				if (!varianceCheckFailed) {
					return result;
				}
				if (originalErrorChain != nullptr) {
					errorChain = originalErrorChain;
				} else if (errorChain == nullptr) {
					errorChain = saveErrorState.errorChain;
				}
				// Use variance error (there is no structural one) and return false
			}
		}
		// If S is an object type and T is a discriminated union, S may be related to T if
		// there exists a constituent of T for every combination of the discriminants of S
		// with respect to T. We do not report errors here, as we will use the existing
		// error result from checking each constituent of the union.
		if ((source->flags & (TypeFlagsObject | TypeFlagsIntersection)) != 0 &&
			(target->flags & TypeFlagsUnion) != 0) {
			Type* objectOnlyTarget =
				c->extractTypesOfKind(target, TypeFlagsObject |
												  TypeFlagsIntersection |
												  TypeFlagsSubstitution);
			if ((objectOnlyTarget->flags & TypeFlagsUnion) != 0) {
				Ternary discResult =
					typeRelatedToDiscriminatedType(source, objectOnlyTarget);
				if (discResult != Ternary::False) {
					return discResult;
				}
			}
		}
	}
	return Ternary::False;
}

// relater.go:3935
Ternary Relater::typeArgumentsRelatedTo(
	const std::vector<Type*>& sources, const std::vector<Type*>& targets,
	const std::vector<VarianceFlags>& variances, bool reportErrors,
	IntersectionState intersectionState) {
	if (sources.size() != targets.size() && relation == c->identityRelation) {
		return Ternary::False;
	}
	size_t length = std::min(sources.size(), targets.size());
	Ternary result = Ternary::True;
	for (size_t i = 0; i < length; i++) {
		// When variance information isn't available we default to covariance. This happens
		// in the process of computing variance information for recursive types and when
		// comparing 'this' type arguments.
		VarianceFlags varianceFlags = VarianceFlagsCovariant;
		if (i < variances.size()) {
			varianceFlags = variances[i];
		}
		VarianceFlags variance = varianceFlags & VarianceFlagsVarianceMask;
		// We ignore arguments for independent type parameters (because they're never witnessed).
		if (variance != VarianceFlagsIndependent) {
			Type* s = sources[i];
			Type* t = targets[i];
			Ternary related = Ternary::False;
			if ((varianceFlags & VarianceFlagsUnmeasurable) != 0) {
				// Even an `Unmeasurable` variance works out without a structural check if the source and target are _identical_.
				// We can't simply assume invariance, because `Unmeasurable` marks nonlinear relations, for example, a relation tainted by
				// the `-?` modifier in a mapped type (where, no matter how the inputs are related, the outputs still might not be)
				if (relation == c->identityRelation) {
					related = isRelatedTo(s, t, RecursionFlagsBoth,
										  false /*reportErrors*/);
				} else {
					related = c->compareTypesIdentical(s, t);
				}
			} else {
				// Propagate unreliable variance flag in variance computations
				if (!c->varianceStack.empty() &&
					(varianceFlags & VarianceFlagsUnreliable) != 0) {
					c->instantiateType(s, c->reportUnreliableMapper);
				}
				if (variance == VarianceFlagsCovariant) {
					related = isRelatedToEx(s, t, RecursionFlagsBoth,
											reportErrors,
											nullptr /*headMessage*/,
											intersectionState);
				} else if (variance == VarianceFlagsContravariant) {
					related = isRelatedToEx(t, s, RecursionFlagsBoth,
											reportErrors,
											nullptr /*headMessage*/,
											intersectionState);
				} else if (variance == VarianceFlagsBivariant) {
					// In the bivariant case we first compare contravariantly without reporting
					// errors. Then, if that doesn't succeed, we compare covariantly with error
					// reporting. Thus, error elaboration will be based on the covariant check,
					// which is generally easier to reason about.
					related = isRelatedTo(t, s, RecursionFlagsBoth,
										  false /*reportErrors*/);
					if (related == Ternary::False) {
						related = isRelatedToEx(s, t, RecursionFlagsBoth,
												reportErrors,
												nullptr /*headMessage*/,
												intersectionState);
					}
				} else {
					// In the invariant case we first compare covariantly, and only when that
					// succeeds do we proceed to compare contravariantly. Thus, error elaboration
					// will typically be based on the covariant check.
					related = isRelatedToEx(s, t, RecursionFlagsBoth,
											reportErrors,
											nullptr /*headMessage*/,
											intersectionState);
					if (related != Ternary::False) {
						related &= isRelatedToEx(t, s, RecursionFlagsBoth,
												 reportErrors,
												 nullptr /*headMessage*/,
												 intersectionState);
					}
				}
			}
			if (related == Ternary::False) {
				return Ternary::False;
			}
			result &= related;
		}
	}
	return result;
}

// A type [P in S]: X is related to a type [Q in T]: Y if T is related to S and X' is
// related to Y, where X' is an instantiation of X in which P is replaced with Q. Notice
// that S and T are contra-variant whereas X and Y are co-variant.
// relater.go:4004
Ternary Relater::mappedTypeRelatedTo(Type* source, Type* target,
									 bool reportErrors) {
	bool modifiersRelated =
		relation == c->comparableRelation ||
		(relation == c->identityRelation &&
		 getMappedTypeModifiers(source) == getMappedTypeModifiers(target)) ||
		(relation != c->identityRelation &&
		 c->getCombinedMappedTypeOptionality(source) <=
			 c->getCombinedMappedTypeOptionality(target));
	if (modifiersRelated) {
		Type* targetConstraint = c->getConstraintTypeFromMappedType(target);
		Type* sourceConstraint = c->instantiateType(
			c->getConstraintTypeFromMappedType(source),
			c->getCombinedMappedTypeOptionality(source) < 0
				? c->reportUnmeasurableMapper
				: c->reportUnreliableMapper);
		Ternary result = isRelatedTo(targetConstraint, sourceConstraint,
									 RecursionFlagsBoth, reportErrors);
		if (result != Ternary::False) {
			TypeMapper* mapper = newSimpleTypeMapper(
				c->getTypeParameterFromMappedType(source),
				c->getTypeParameterFromMappedType(target));
			if (c->instantiateType(c->getNameTypeFromMappedType(source),
								   mapper) ==
				c->instantiateType(c->getNameTypeFromMappedType(target),
								   mapper)) {
				return result & isRelatedTo(
									c->instantiateType(
										c->getTemplateTypeFromMappedType(source),
										mapper),
									c->getTemplateTypeFromMappedType(target),
									RecursionFlagsBoth, reportErrors);
			}
		}
	}
	return Ternary::False;
}

// relater.go:4021
Ternary Relater::typeRelatedToDiscriminatedType(Type* source, Type* target) {
	// 1. Generate the combinations of discriminant properties & types 'source' can satisfy.
	//    a. If the number of combinations is above a set limit, the comparison is too complex.
	// 2. Filter 'target' to the subset of types whose discriminants exist in the matrix.
	//    a. If 'target' does not satisfy all discriminants in the matrix, 'source' is not related.
	// 3. For each type in the filtered 'target', determine if all non-discriminant properties of
	//    'target' are related to a property in 'source'.
	//
	// NOTE: See ~/tests/cases/conformance/types/typeRelationships/assignmentCompatibility/assignmentCompatWithDiscriminatedUnion.ts
	//       for examples.
	std::vector<Symbol*> sourceProperties = c->getPropertiesOfType(source);
	std::vector<Symbol*> sourcePropertiesFiltered =
		c->findDiscriminantProperties(sourceProperties, target);
	if (sourcePropertiesFiltered.empty()) {
		return Ternary::False;
	}
	// Though we could compute the number of combinations as we generate
	// the matrix, this would incur additional memory overhead due to
	// array allocations. To reduce this overhead, we first compute
	// the number of combinations to ensure we will not surpass our
	// fixed limit before incurring the cost of any allocations:
	int numCombinations = 1;
	for (Symbol* sourceProperty : sourcePropertiesFiltered) {
		numCombinations *=
			countTypes(c->getNonMissingTypeOfSymbol(sourceProperty));
		if (numCombinations > 25) {
			if (Tracer* tr = c->tracer; tr != nullptr) {
				tr->Instant(tsc::tracing::PhaseCheckTypes,
							"typeRelatedToDiscriminatedType_DepthLimit",
							{{"sourceId", source->id},
							 {"targetId", target->id},
							 {"numCombinations", numCombinations}});
			}
			return Ternary::False;
		}
		if (numCombinations == 0) {
			return Ternary::False;
		}
	}
	// Compute the set of types for each discriminant property.
	std::vector<std::vector<Type*>> sourceDiscriminantTypes(
		sourcePropertiesFiltered.size());
	std::unordered_set<std::string> excludedProperties;
	for (size_t i = 0; i < sourcePropertiesFiltered.size(); i++) {
		Symbol* sourceProperty = sourcePropertiesFiltered[i];
		Type* sourcePropertyType =
			c->getNonMissingTypeOfSymbol(sourceProperty);
		sourceDiscriminantTypes[i] = sourcePropertyType->Distributed();
		excludedProperties.insert(sourceProperty->name);
	}
	// Build the cartesian product
	std::vector<std::vector<Type*>> discriminantCombinations(numCombinations);
	for (int i = 0; i < numCombinations; i++) {
		std::vector<Type*> combination(sourceDiscriminantTypes.size());
		int n = i;
		for (int j = static_cast<int>(sourceDiscriminantTypes.size()) - 1;
			 j >= 0; j--) {
			const std::vector<Type*>& sourceTypes = sourceDiscriminantTypes[j];
			int length = static_cast<int>(sourceTypes.size());
			combination[j] = sourceTypes[n % length];
			n = n / length;
		}
		discriminantCombinations[i] = combination;
	}
	// Match each combination of the cartesian product of discriminant properties to one or more
	// constituents of 'target'. If any combination does not have a match then 'source' is not relatable.
	std::vector<Type*> matchingTypes;
	for (const std::vector<Type*>& combination : discriminantCombinations) {
		bool hasMatch = false;
		for (Type* t : target->types()) {
			bool tMatched = true;
			for (size_t i = 0; i < sourcePropertiesFiltered.size(); i++) {
				Symbol* sourceProperty = sourcePropertiesFiltered[i];
				Symbol* targetProperty =
					c->getPropertyOfType(t, sourceProperty->name);
				if (targetProperty == nullptr) {
					tMatched = false;
					break;
				}
				if (sourceProperty == targetProperty) {
					continue;
				}
				// We compare the source property to the target in the context of a single discriminant type.
				Ternary related = propertyRelatedTo(
					source, target, sourceProperty, targetProperty,
					[&combination, i](Symbol*) -> Type* {
						return combination[i];
					},
					false /*reportErrors*/, IntersectionStateNone,
					c->strictNullChecks ||
						relation == c->comparableRelation /*skipOptional*/);
				// If the target property could not be found, or if the properties were not related,
				// then this constituent is not a match.
				if (related == Ternary::False) {
					tMatched = false;
					break;
				}
			}
			if (!tMatched) {
				continue;
			}
			appendIfUnique(matchingTypes, t);
			hasMatch = true;
		}
		if (!hasMatch) {
			// We failed to match any type for this combination.
			return Ternary::False;
		}
	}
	// Compare the remaining non-discriminant properties of each match.
	Ternary result = Ternary::True;
	for (Type* t : matchingTypes) {
		result &= propertiesRelatedTo(source, t, false /*reportErrors*/,
									  excludedProperties,
									  false /*optionalsOnly*/,
									  IntersectionStateNone);
		if (result != Ternary::False) {
			result &= signaturesRelatedTo(source, t, SignatureKind::Call,
										  false /*reportErrors*/,
										  IntersectionStateNone);
			if (result != Ternary::False) {
				result &= signaturesRelatedTo(source, t, SignatureKind::Construct,
											  false /*reportErrors*/,
											  IntersectionStateNone);
				if (result != Ternary::False &&
					!(isTupleType(source) && isTupleType(t))) {
					// Comparing numeric index types when both `source` and `type` are tuples is unnecessary as the
					// element types should be sufficiently covered by `propertiesRelatedTo`. It also causes problems
					// with index type assignability as the types for the excluded discriminants are still included
					// in the index type.
					result &= indexSignaturesRelatedTo(
						source, t, false /*sourceIsPrimitive*/,
						false /*reportErrors*/, IntersectionStateNone);
				}
			}
		}
		if (result == Ternary::False) {
			return result;
		}
	}
	return result;
}

// relater.go:4132
Ternary Relater::propertiesRelatedTo(
	Type* source, Type* target, bool reportErrors,
	const std::unordered_set<std::string>& excludedProperties,
	bool optionalsOnly, IntersectionState intersectionState) {
	if (relation == c->identityRelation) {
		return propertiesIdenticalTo(source, target, excludedProperties);
	}
	Ternary result = Ternary::True;
	if (isTupleType(target)) {
		if (c->isArrayOrTupleType(source)) {
			if (!target->TargetTupleType()->readonly &&
				(c->isReadonlyArrayType(source) ||
				 (isTupleType(source) &&
				  source->TargetTupleType()->readonly))) {
				return Ternary::False;
			}
			int sourceArity = c->getTypeReferenceArity(source);
			int targetArity = c->getTypeReferenceArity(target);
			bool sourceRest;
			if (isTupleType(source)) {
				sourceRest =
					(source->TargetTupleType()->combinedFlags &
					 ElementFlagsRest) != 0;
			} else {
				sourceRest = true;
			}
			bool targetHasRestElement =
				(target->TargetTupleType()->combinedFlags &
				 ElementFlagsRest) != 0;
			bool targetHasVariableElement =
				(target->TargetTupleType()->combinedFlags &
				 ElementFlagsVariable) != 0;
			int sourceMinLength;
			if (isTupleType(source)) {
				sourceMinLength = source->TargetTupleType()->minLength;
			} else {
				sourceMinLength = 0;
			}
			int targetMinLength = target->TargetTupleType()->minLength;
			if (!sourceRest && sourceArity < targetMinLength) {
				if (reportErrors) {
					reportError(
						Source_has_0_element_s_but_target_requires_1,
						{std::to_string(sourceArity),
						 std::to_string(targetMinLength)});
				}
				return Ternary::False;
			}
			if (!targetHasVariableElement &&
				targetArity < sourceMinLength) {
				if (reportErrors) {
					reportError(
						
							Source_has_0_element_s_but_target_allows_only_1,
						{std::to_string(sourceMinLength),
						 std::to_string(targetArity)});
				}
				return Ternary::False;
			}
			if (!targetHasVariableElement &&
				(sourceRest || targetArity < sourceArity)) {
				if (reportErrors) {
					if (sourceMinLength < targetMinLength) {
						reportError(
							
								Target_requires_0_element_s_but_source_may_have_fewer,
							{std::to_string(targetMinLength)});
					} else {
						reportError(
							
								Target_allows_only_0_element_s_but_source_may_have_more,
							{std::to_string(targetArity)});
					}
				}
				return Ternary::False;
			}
			const std::vector<Type*>& sourceTypeArguments =
				c->getTypeArguments(source);
			const std::vector<Type*>& targetTypeArguments =
				c->getTypeArguments(target);
			int targetStartCount = getStartElementCount(
				target->TargetTupleType(), ElementFlagsNonRest);
			int targetEndCount = getEndElementCount(
				target->TargetTupleType(), ElementFlagsNonRest);
			bool canExcludeDiscriminants = !excludedProperties.empty();
			for (int sourcePosition = 0; sourcePosition < sourceArity;
				 sourcePosition++) {
				ElementFlags sourceFlags;
				if (isTupleType(source)) {
					sourceFlags = source->TargetTupleType()
									  ->elementInfos[sourcePosition]
									  .flags;
				} else {
					sourceFlags = ElementFlagsRest;
				}
				int sourcePositionFromEnd = sourceArity - 1 - sourcePosition;
				int targetPosition;
				if (targetHasRestElement && sourcePosition >= targetStartCount) {
					targetPosition = targetArity - 1 -
									 std::min(sourcePositionFromEnd,
											  targetEndCount);
				} else {
					if (sourcePosition >= targetArity) {
						if (reportErrors) {
							reportError(
								
									Target_allows_only_0_element_s_but_source_may_have_more,
								{std::to_string(targetArity)});
						}
						return Ternary::False;
					}
					targetPosition = sourcePosition;
				}
				ElementFlags targetFlags = ElementFlagsNone;
				if (targetPosition >= 0) {
					targetFlags = target->TargetTupleType()
									  ->elementInfos[targetPosition]
									  .flags;
				}
				if ((targetFlags & ElementFlagsVariadic) != 0 &&
					(sourceFlags & ElementFlagsVariadic) == 0) {
					if (reportErrors) {
						reportError(
							
								Source_provides_no_match_for_variadic_element_at_position_0_in_target,
							{std::to_string(targetPosition)});
					}
					return Ternary::False;
				}
				if ((sourceFlags & ElementFlagsVariadic) != 0 &&
					(targetFlags & ElementFlagsVariable) == 0) {
					if (reportErrors) {
						reportError(
							
								Variadic_element_at_position_0_in_source_does_not_match_element_at_position_1_in_target,
							{std::to_string(sourcePosition),
							 std::to_string(targetPosition)});
					}
					return Ternary::False;
				}
				if ((targetFlags & ElementFlagsRequired) != 0 &&
					(sourceFlags & ElementFlagsRequired) == 0) {
					if (reportErrors) {
						reportError(
							
								Source_provides_no_match_for_required_element_at_position_0_in_target,
							{std::to_string(targetPosition)});
					}
					return Ternary::False;
				}
				// We can only exclude discriminant properties if we have not yet encountered a variable-length element.
				if (canExcludeDiscriminants) {
					if ((sourceFlags & ElementFlagsVariable) != 0 ||
						(targetFlags & ElementFlagsVariable) != 0) {
						canExcludeDiscriminants = false;
					}
					if (canExcludeDiscriminants &&
						excludedProperties.count(
							std::to_string(sourcePosition)) != 0) {
						continue;
					}
				}
				Type* sourceType = c->removeMissingType(
					sourceTypeArguments[sourcePosition],
					(sourceFlags & targetFlags & ElementFlagsOptional) != 0);
				Type* targetType = targetTypeArguments[targetPosition];
				Type* targetCheckType;
				if ((sourceFlags & ElementFlagsVariadic) != 0 &&
					(targetFlags & ElementFlagsRest) != 0) {
					targetCheckType = c->createArrayType(targetType);
				} else {
					targetCheckType = c->removeMissingType(
						targetType,
						(targetFlags & ElementFlagsOptional) != 0);
				}
				Ternary related = isRelatedToEx(sourceType, targetCheckType,
												RecursionFlagsBoth, reportErrors,
												nullptr /*headMessage*/,
												intersectionState);
				if (related == Ternary::False) {
					if (reportErrors && (targetArity > 1 || sourceArity > 1)) {
						if (targetHasRestElement &&
							sourcePosition >= targetStartCount &&
							sourcePositionFromEnd >= targetEndCount &&
							targetStartCount !=
								sourceArity - targetEndCount - 1) {
							reportError(
								
									Type_at_positions_0_through_1_in_source_is_not_compatible_with_type_at_position_2_in_target,
								{std::to_string(targetStartCount),
								 std::to_string(sourceArity - targetEndCount -
												1),
								 std::to_string(targetPosition)});
						} else {
							reportError(
								
									Type_at_position_0_in_source_is_not_compatible_with_type_at_position_1_in_target,
								{std::to_string(sourcePosition),
								 std::to_string(targetPosition)});
						}
					}
					return Ternary::False;
				}
				result &= related;
			}
			return result;
		}
		if ((target->TargetTupleType()->combinedFlags &
			 ElementFlagsVariable) != 0) {
			return Ternary::False;
		}
	}
	bool requireOptionalProperties =
		(relation == c->subtypeRelation ||
		 relation == c->strictSubtypeRelation) &&
		!isObjectLiteralType(source) && !c->isEmptyArrayLiteralType(source) &&
		!isTupleType(source);
	Symbol* unmatchedProperty =
		c->getUnmatchedProperty(source, target, requireOptionalProperties,
								false /*matchDiscriminantProperties*/);
	if (unmatchedProperty != nullptr) {
		if (reportErrors &&
			c->shouldReportUnmatchedPropertyError(source, target)) {
			reportUnmatchedProperty(source, target, unmatchedProperty,
									requireOptionalProperties);
		}
		return Ternary::False;
	}
	if (isObjectLiteralType(target)) {
		for (Symbol* sourceProp :
			 excludeProperties(c->getPropertiesOfType(source),
							   excludedProperties)) {
			if (c->getPropertyOfObjectType(target, sourceProp->name) ==
				nullptr) {
				if (reportErrors) {
					reportError(
						Property_0_does_not_exist_on_type_1,
						{c->symbolToString(sourceProp), c->TypeToString(target)});
				}
				return Ternary::False;
			}
		}
	}
	// We only call this for union target types when we're attempting to do excess property checking - in those cases, we want to get _all possible props_
	// from the target union, across all members
	std::vector<Symbol*> properties = c->getPropertiesOfType(target);
	bool numericNamesOnly = isTupleType(source) && isTupleType(target);
	for (Symbol* targetProp :
		 excludeProperties(properties, excludedProperties)) {
		const std::string& name = targetProp->name;
		if ((targetProp->flags & SymbolFlagsPrototype) == 0 &&
			(!numericNamesOnly || isNumericLiteralName(name) ||
			 name == "length") &&
			(!optionalsOnly ||
			 (targetProp->flags & SymbolFlagsOptional) != 0)) {
			Symbol* sourceProp = c->getPropertyOfType(source, name);
			if (sourceProp != nullptr && sourceProp != targetProp) {
				Ternary related = propertyRelatedTo(
					source, target, sourceProp, targetProp,
					[this](Symbol* s) {
						return c->getNonMissingTypeOfSymbol(s);
					},
					reportErrors, intersectionState,
					relation == c->comparableRelation);
				if (related == Ternary::False) {
					return Ternary::False;
				}
				result &= related;
			}
		}
	}
	return result;
}

// relater.go:4302
Ternary Relater::propertyRelatedTo(
	Type* source, Type* target, Symbol* sourceProp, Symbol* targetProp,
	const std::function<Type*(Symbol*)>& getTypeOfSourceProperty,
	bool reportErrors, IntersectionState intersectionState, bool skipOptional) {
	ModifierFlags sourcePropFlags =
		getDeclarationModifierFlagsFromSymbol(sourceProp);
	ModifierFlags targetPropFlags =
		getDeclarationModifierFlagsFromSymbol(targetProp);
	if ((sourcePropFlags & ModifierFlagsPrivate) != 0 ||
		(targetPropFlags & ModifierFlagsPrivate) != 0) {
		if (sourceProp->valueDeclaration != targetProp->valueDeclaration) {
			if (reportErrors) {
				if ((sourcePropFlags & ModifierFlagsPrivate) != 0 &&
					(targetPropFlags & ModifierFlagsPrivate) != 0) {
					reportError(
						
							Types_have_separate_declarations_of_a_private_property_0,
						{c->symbolToString(targetProp)});
				} else {
					reportError(
						
							Property_0_is_private_in_type_1_but_not_in_type_2,
						{c->symbolToString(targetProp),
						 c->TypeToString(
							 (sourcePropFlags & ModifierFlagsPrivate) != 0
								 ? source
								 : target),
						 c->TypeToString(
							 (sourcePropFlags & ModifierFlagsPrivate) != 0
								 ? target
								 : source)});
				}
			}
			return Ternary::False;
		}
	} else if ((targetPropFlags & ModifierFlagsProtected) != 0) {
		if (!c->isValidOverrideOf(sourceProp, targetProp)) {
			if (reportErrors) {
				Type* sourceType = c->getDeclaringClass(sourceProp) != nullptr
									   ? c->getDeclaringClass(sourceProp)
									   : source;
				Type* targetType = c->getDeclaringClass(targetProp) != nullptr
									   ? c->getDeclaringClass(targetProp)
									   : target;
				reportError(
					
						Property_0_is_protected_but_type_1_is_not_a_class_derived_from_2,
					{c->symbolToString(targetProp),
					 c->TypeToString(sourceType), c->TypeToString(targetType)});
			}
			return Ternary::False;
		}
	} else if ((sourcePropFlags & ModifierFlagsProtected) != 0) {
		if (reportErrors) {
			reportError(
				
					Property_0_is_protected_in_type_1_but_public_in_type_2,
				{c->symbolToString(targetProp), c->TypeToString(source),
				 c->TypeToString(target)});
		}
		return Ternary::False;
	}
	// Ensure {readonly a: whatever} is not a subtype of {a: whatever},
	// while {a: whatever} is a subtype of {readonly a: whatever}.
	// This ensures the subtype relationship is ordered, and preventing declaration order
	// from deciding which type "wins" in union subtype reduction.
	// They're still assignable to one another, since `readonly` doesn't affect assignability.
	// This is only applied during the strictSubtypeRelation -- currently used in subtype reduction
	if (relation == c->strictSubtypeRelation &&
		c->isReadonlySymbol(sourceProp) &&
		!c->isReadonlySymbol(targetProp)) {
		return Ternary::False;
	}
	// If the target comes from a partial union prop, allow `undefined` in the target type
	Ternary related =
		isPropertySymbolTypeRelated(sourceProp, targetProp,
									getTypeOfSourceProperty, reportErrors,
									intersectionState);
	if (related == Ternary::False) {
		if (reportErrors) {
			reportError(Types_of_property_0_are_incompatible,
						{c->symbolToString(targetProp)});
		}
		return Ternary::False;
	}
	// When checking for comparability, be more lenient with optional properties.
	if (!skipOptional && (sourceProp->flags & SymbolFlagsOptional) != 0 &&
		(targetProp->flags & SymbolFlagsClassMember) != 0 &&
		(targetProp->flags & SymbolFlagsOptional) == 0) {
		// TypeScript 1.0 spec (April 2014): 3.8.3
		// S is a subtype of a type T, and T is a supertype of S if ...
		// S' and T are object types and, for each member M in T..
		// M is a property and S' contains a property N where
		// if M is a required property, N is also a required property
		// (M - property in T)
		// (N - property in S)
		if (reportErrors) {
			reportError(
				
					Property_0_is_optional_in_type_1_but_required_in_type_2,
				{c->symbolToString(targetProp), c->TypeToString(source),
				 c->TypeToString(target)});
		}
		return Ternary::False;
	}
	return related;
}

// relater.go:4366
Ternary Relater::isPropertySymbolTypeRelated(
	Symbol* sourceProp, Symbol* targetProp,
	const std::function<Type*(Symbol*)>& getTypeOfSourceProperty,
	bool reportErrors, IntersectionState intersectionState) {
	bool targetIsOptional =
		c->strictNullChecks &&
		(targetProp->checkFlags & CheckFlagsPartial) != 0;
	Type* effectiveTarget = c->addOptionalityEx(
		c->getNonMissingTypeOfSymbol(targetProp), false /*isProperty*/,
		targetIsOptional);
	// source could resolve to `any` and that's not related to `unknown` target under strict subtype relation
	if ((effectiveTarget->flags &
		 (relation == c->strictSubtypeRelation ? TypeFlagsAny
											   : TypeFlagsAnyOrUnknown)) != 0) {
		return Ternary::True;
	}
	Type* effectiveSource = getTypeOfSourceProperty(sourceProp);
	return isRelatedToEx(effectiveSource, effectiveTarget, RecursionFlagsBoth,
						 reportErrors, nullptr /*headMessage*/,
						 intersectionState);
}

// relater.go:4377
void Relater::reportUnmatchedProperty(Type* source, Type* target,
									  Symbol* unmatchedProperty,
									  bool requireOptionalProperties) {
	// give specific error in case where private names have the same description
	if (unmatchedProperty->valueDeclaration != nullptr &&
		unmatchedProperty->valueDeclaration->name() != nullptr &&
		isPrivateIdentifier(unmatchedProperty->valueDeclaration->name()) &&
		source->symbol != nullptr &&
		(source->symbol->flags & SymbolFlagsClass) != 0) {
		std::string privateIdentifierDescription =
			unmatchedProperty->valueDeclaration->name()->text();
		std::string symbolTableKey = getSymbolNameForPrivateIdentifier(
			source->symbol, privateIdentifierDescription);
		if (c->getPropertyOfType(source, symbolTableKey) != nullptr) {
			reportError(
				
					Property_0_in_type_1_refers_to_a_different_member_that_cannot_be_accessed_from_within_type_2,
				{privateIdentifierDescription,
				 c->symbolToString(source->symbol),
				 c->symbolToString(target->symbol)});
			return;
		}
	}
	std::vector<Symbol*> props = c->getUnmatchedProperties(
		source, target, requireOptionalProperties,
		false /*matchDiscriminantProperties*/);
	if (props.size() == 1) {
		auto names = c->getTypeNamesForErrorDisplay(source, target);
		std::string sourceType = names.first;
		std::string targetType = names.second;
		std::string propName = c->symbolToString(unmatchedProperty);
		reportError(
			Property_0_is_missing_in_type_1_but_required_in_type_2,
			{propName, sourceType, targetType});
		if (!unmatchedProperty->declarations.empty()) {
			relatedInfo.push_back(c->createDiagnosticForNode(
				unmatchedProperty->declarations[0],
				X_0_is_declared_here, {propName}));
		}
	} else if (tryElaborateArrayLikeErrors(source, target,
										   false /*reportErrors*/)) {
		auto names = c->getTypeNamesForErrorDisplay(source, target);
		std::string sourceType = names.first;
		std::string targetType = names.second;
		if (props.size() > 5) {
			std::vector<std::string> names4;
			for (size_t i = 0; i < 4; i++) {
				names4.push_back(c->symbolToString(props[i]));
			}
			std::string propNames = joinStrings(names4, ", ");
			reportError(
				
					Type_0_is_missing_the_following_properties_from_type_1_Colon_2_and_3_more,
				{sourceType, targetType, propNames,
				 std::to_string(props.size() - 4)});
		} else {
			std::vector<std::string> namesAll =
				mapVec(props, [this](Symbol* p) { return c->symbolToString(p); });
			std::string propNames = joinStrings(namesAll, ", ");
			reportError(
				
					Type_0_is_missing_the_following_properties_from_type_1_Colon_2,
				{sourceType, targetType, propNames});
		}
	}
}

// relater.go:4411
bool Relater::tryElaborateArrayLikeErrors(Type* source, Type* target,
										  bool reportErrors) {
	/**
	 * The spec for elaboration is:
	 * - If the source is a readonly tuple and the target is a mutable array or tuple, elaborate on mutability and skip property elaborations.
	 * - If the source is a tuple then skip property elaborations if the target is an array or tuple.
	 * - If the source is a readonly array and the target is a mutable array or tuple, elaborate on mutability and skip property elaborations.
	 * - If the source an array then skip property elaborations if the target is a tuple.
	 */
	if (isTupleType(source)) {
		if (source->TargetTupleType()->readonly &&
			c->isMutableArrayOrTuple(target)) {
			if (reportErrors) {
				reportError(
					
						The_type_0_is_readonly_and_cannot_be_assigned_to_the_mutable_type_1,
					{c->TypeToString(source), c->TypeToString(target)});
			}
			return false;
		}
		return c->isArrayOrTupleType(target);
	}
	if (c->isReadonlyArrayType(source) && c->isMutableArrayOrTuple(target)) {
		if (reportErrors) {
			reportError(
				
					The_type_0_is_readonly_and_cannot_be_assigned_to_the_mutable_type_1,
				{c->TypeToString(source), c->TypeToString(target)});
		}
		return false;
	}
	if (isTupleType(target)) {
		return c->isArrayType(source);
	}
	return true;
}

// relater.go:4440
void Relater::tryElaborateErrorsForPrimitivesAndObjects(Type* source,
														Type* target) {
	if ((source == c->globalStringType && target == c->stringType) ||
		(source == c->globalNumberType && target == c->numberType) ||
		(source == c->globalBooleanType && target == c->booleanType) ||
		(source == c->getGlobalESSymbolType() && target == c->esSymbolType)) {
		reportError(
			
				X_0_is_a_primitive_but_1_is_a_wrapper_object_Prefer_using_0_when_possible,
			{c->TypeToString(target), c->TypeToString(source)});
	}
}

// relater.go:4449
Ternary Relater::propertiesIdenticalTo(
	Type* source, Type* target,
	const std::unordered_set<std::string>& excludedProperties) {
	if ((source->flags & TypeFlagsObject) == 0 ||
		(target->flags & TypeFlagsObject) == 0) {
		return Ternary::False;
	}
	std::vector<Symbol*> sourceProperties =
		excludeProperties(c->getPropertiesOfObjectType(source),
						  excludedProperties);
	std::vector<Symbol*> targetProperties =
		excludeProperties(c->getPropertiesOfObjectType(target),
						  excludedProperties);
	if (sourceProperties.size() != targetProperties.size()) {
		return Ternary::False;
	}
	Ternary result = Ternary::True;
	for (Symbol* sourceProp : sourceProperties) {
		Symbol* targetProp =
			c->getPropertyOfObjectType(target, sourceProp->name);
		if (targetProp == nullptr) {
			return Ternary::False;
		}
		Ternary related = c->compareProperties(
			sourceProp, targetProp, [this](Type* s, Type* t) { return isRelatedToSimple(s, t); });
		if (related == Ternary::False) {
			return Ternary::False;
		}
		result &= related;
	}
	return result;
}

// relater.go:4473
Ternary Relater::signaturesRelatedTo(Type* source, Type* target,
									 SignatureKind kind, bool reportErrors,
									 IntersectionState intersectionState) {
	if (relation == c->identityRelation) {
		return signaturesIdenticalTo(source, target, kind);
	}
	// With respect to signatures, the anyFunctionType wildcard is a subtype of every other function type.
	if (source == c->anyFunctionType) {
		return Ternary::True;
	}
	if (target == c->anyFunctionType) {
		return Ternary::False;
	}
	const std::vector<Signature*>& sourceSignatures =
		c->getSignaturesOfType(source, kind);
	const std::vector<Signature*>& targetSignatures =
		c->getSignaturesOfType(target, kind);
	if (kind == SignatureKind::Construct && !sourceSignatures.empty() &&
		!targetSignatures.empty()) {
		bool sourceIsAbstract =
			(sourceSignatures[0]->flags & SignatureFlagsAbstract) != 0;
		bool targetIsAbstract =
			(targetSignatures[0]->flags & SignatureFlagsAbstract) != 0;
		if (sourceIsAbstract && !targetIsAbstract) {
			// An abstract constructor type is not assignable to a non-abstract constructor type
			// as it would otherwise be possible to new an abstract class. Note that the assignability
			// check we perform for an extends clause excludes construct signatures from the target,
			// so this check never proceeds.
			if (reportErrors) {
				reportError(
					
						Cannot_assign_an_abstract_constructor_type_to_a_non_abstract_constructor_type);
			}
			return Ternary::False;
		}
		if (!constructorVisibilitiesAreCompatible(sourceSignatures[0],
												  targetSignatures[0],
												  reportErrors)) {
			return Ternary::False;
		}
	}
	Ternary result = Ternary::True;
	if (((source->objectFlags & ObjectFlagsInstantiated) != 0 &&
		 (target->objectFlags & ObjectFlagsInstantiated) != 0 &&
		 source->symbol == target->symbol) ||
		((source->objectFlags & ObjectFlagsReference) != 0 &&
		 (target->objectFlags & ObjectFlagsReference) != 0 &&
		 source->Target() == target->Target())) {
		// We have instantiations of the same anonymous type (which typically will be the type of a
		// method). Simply do a pairwise comparison of the signatures in the two signature lists instead
		// of the much more expensive N * M comparison matrix we explore below. We erase type parameters
		// as they are known to always be the same.
		for (size_t i = 0; i < targetSignatures.size(); i++) {
			Ternary related =
				signatureRelatedTo(sourceSignatures[i], targetSignatures[i],
								   true /*erase*/, reportErrors,
								   intersectionState);
			if (related == Ternary::False) {
				return Ternary::False;
			}
			result &= related;
		}
	} else if (sourceSignatures.size() == 1 && targetSignatures.size() == 1) {
		// For simple functions (functions with a single signature) we only erase type parameters for
		// the comparable relation. Otherwise, if the source signature is generic, we instantiate it
		// in the context of the target signature before checking the relationship. Ideally we'd do
		// this regardless of the number of signatures, but the potential costs are prohibitive due
		// to the quadratic nature of the logic below.
		bool eraseGenerics = relation == c->comparableRelation;
		result = signatureRelatedTo(sourceSignatures[0], targetSignatures[0],
									eraseGenerics, reportErrors,
									intersectionState);
	} else {
		for (Signature* t : targetSignatures) {
			errorState saveErrorState = getErrorState();
			// Only elaborate errors from the first failure
			bool shouldElaborateErrors = reportErrors;
			bool matched = false;
			for (Signature* s : sourceSignatures) {
				Ternary related =
					signatureRelatedTo(s, t, true /*erase*/,
									   shouldElaborateErrors,
									   intersectionState);
				if (related != Ternary::False) {
					result &= related;
					restoreErrorState(saveErrorState);
					matched = true;
					break;
				}
				shouldElaborateErrors = false;
			}
			if (matched) {
				continue;
			}
			if (shouldElaborateErrors) {
				reportError(
					Type_0_provides_no_match_for_the_signature_1,
					{c->TypeToString(source), c->signatureToString(t)});
			}
			return Ternary::False;
		}
	}
	return result;
}

// relater.go:4550
bool Relater::constructorVisibilitiesAreCompatible(Signature* sourceSignature,
												   Signature* targetSignature,
												   bool reportErrors) {
	if (sourceSignature->declaration == nullptr ||
		targetSignature->declaration == nullptr) {
		return true;
	}
	ModifierFlags sourceAccessibility =
		sourceSignature->declaration->modifierFlags() &
		ModifierFlagsNonPublicAccessibilityModifier;
	ModifierFlags targetAccessibility =
		targetSignature->declaration->modifierFlags() &
		ModifierFlagsNonPublicAccessibilityModifier;
	// A public, protected and private signature is assignable to a private signature.
	if (targetAccessibility == ModifierFlagsPrivate) {
		return true;
	}
	// A public and protected signature is assignable to a protected signature.
	if (targetAccessibility == ModifierFlagsProtected &&
		sourceAccessibility != ModifierFlagsPrivate) {
		return true;
	}
	// Only a public signature is assignable to public signature.
	if (targetAccessibility != ModifierFlagsProtected &&
		sourceAccessibility == 0) {
		return true;
	}
	if (reportErrors) {
		reportError(
			Cannot_assign_a_0_constructor_type_to_a_1_constructor_type,
			{visibilityToString(sourceAccessibility),
			 visibilityToString(targetAccessibility)});
	}
	return false;
}

// See signatureAssignableTo, compareSignaturesIdentical
// relater.go:4575
Ternary Relater::signatureRelatedTo(Signature* source, Signature* target,
									bool erase, bool reportErrors,
									IntersectionState intersectionState) {
	SignatureCheckMode checkMode = SignatureCheckModeNone;
	if (relation == c->subtypeRelation) {
		checkMode = SignatureCheckModeStrictTopSignature;
	} else if (relation == c->strictSubtypeRelation) {
		checkMode = SignatureCheckModeStrictTopSignature |
					SignatureCheckModeStrictArity;
	}
	if (erase) {
		source = c->getErasedSignature(source);
		target = c->getErasedSignature(target);
	}
	return c->compareSignaturesRelated(
		source, target, checkMode, reportErrors,
		[this](const DiagnosticMessage* m,
			   const std::vector<std::string>& a) { reportError(m, a); },
		[this, intersectionState](Type* s, Type* t, bool rpt) {
			return isRelatedToEx(s, t, RecursionFlagsBoth, rpt,
								 nullptr /*headMessage*/, intersectionState);
		},
		c->reportUnreliableMapper);
}

// relater.go:4593
Ternary Relater::signaturesIdenticalTo(Type* source, Type* target,
									   SignatureKind kind) {
	const std::vector<Signature*>& sourceSignatures =
		c->getSignaturesOfType(source, kind);
	const std::vector<Signature*>& targetSignatures =
		c->getSignaturesOfType(target, kind);
	if (sourceSignatures.size() != targetSignatures.size()) {
		return Ternary::False;
	}
	Ternary result = Ternary::True;
	for (size_t i = 0; i < sourceSignatures.size(); i++) {
		Ternary related = c->compareSignaturesIdentical(
			sourceSignatures[i], targetSignatures[i],
			false /*partialMatch*/, false /*ignoreThisTypes*/,
			false /*ignoreReturnTypes*/, [this](Type* s, Type* t) {
				return isRelatedToSimple(s, t);
			});
		if (related == Ternary::False) {
			return Ternary::False;
		}
		result &= related;
	}
	return result;
}

// relater.go:4610
Ternary Relater::indexSignaturesRelatedTo(Type* source, Type* target,
										  bool sourceIsPrimitive,
										  bool reportErrors,
										  IntersectionState intersectionState) {
	if (relation == c->identityRelation) {
		return indexSignaturesIdenticalTo(source, target);
	}
	const std::vector<IndexInfo*>& indexInfos = c->getIndexInfosOfType(target);
	bool targetHasStringIndex =
		someOf(indexInfos, [this](IndexInfo* info) {
			return info->keyType == c->stringType;
		});
	Ternary result = Ternary::True;
	for (IndexInfo* targetInfo : indexInfos) {
		Ternary related = Ternary::False;
		if (relation != c->strictSubtypeRelation && !sourceIsPrimitive &&
			targetHasStringIndex &&
			(targetInfo->valueType->flags & TypeFlagsAny) != 0) {
			related = Ternary::True;
		} else if (c->isGenericMappedType(source) && targetHasStringIndex) {
			related =
				isRelatedTo(c->getTemplateTypeFromMappedType(source),
							targetInfo->valueType, RecursionFlagsBoth,
							reportErrors);
		} else {
			related = typeRelatedToIndexInfo(source, targetInfo, reportErrors,
											 intersectionState);
		}
		if (related == Ternary::False) {
			return Ternary::False;
		}
		result &= related;
	}
	return result;
}

// relater.go:4635
Ternary Relater::typeRelatedToIndexInfo(Type* source, IndexInfo* targetInfo,
										bool reportErrors,
										IntersectionState intersectionState) {
	IndexInfo* sourceInfo =
		c->getApplicableIndexInfo(source, targetInfo->keyType);
	if (sourceInfo != nullptr) {
		return indexInfoRelatedTo(sourceInfo, targetInfo, reportErrors,
								  intersectionState);
	}
	// Intersection constituents are never considered to have an inferred index signature. Also, in the strict subtype relation,
	// only fresh object literals are considered to have inferred index signatures. This ensures { [x: string]: xxx } <: {} but
	// not vice-versa. Without this rule, those types would be mutual strict subtypes.
	if ((intersectionState & IntersectionStateSource) == 0 &&
		(relation != c->strictSubtypeRelation ||
		 (source->objectFlags & ObjectFlagsFreshLiteral) != 0) &&
		c->isObjectTypeWithInferableIndex(source)) {
		return membersRelatedToIndexInfo(source, targetInfo, reportErrors,
										 intersectionState);
	}
	if (reportErrors) {
		reportError(Index_signature_for_type_0_is_missing_in_type_1,
					{c->TypeToString(targetInfo->keyType),
					 c->TypeToString(source)});
	}
	return Ternary::False;
}

// Return true if the type was inferred from
//   - an object literal, object type literal, enum type, or a value module and has no call or construct signatures, or
//   - a JS expando object literal or a rest type, or
//   - a reverse mapped type with a source for which one of the above is true.
// relater.go:4656
bool Checker::isObjectTypeWithInferableIndex(Type* t) {
	if ((t->flags & TypeFlagsIntersection) != 0) {
		// Go: core.Every(t.Types(), ...) — iterates members (everyType only
		// unwraps unions).
		for (Type* u : t->types()) {
			if (!isObjectTypeWithInferableIndex(u)) {
				return false;
			}
		}
		return true;
	}
	return (t->symbol != nullptr &&
			(t->symbol->flags &
			 (SymbolFlagsObjectLiteral | SymbolFlagsTypeLiteral |
			  SymbolFlagsEnum | SymbolFlagsValueModule)) != 0 &&
			(t->symbol->flags & SymbolFlagsClass) == 0 &&
			!typeHasCallOrConstructSignatures(t)) ||
		   (t->objectFlags &
			(ObjectFlagsJSLiteral | ObjectFlagsObjectRestType)) != 0 ||
		   ((t->objectFlags & ObjectFlagsReverseMapped) != 0 &&
			isObjectTypeWithInferableIndex(
				t->AsReverseMappedType()->source));
}

// relater.go:4666
Ternary Relater::membersRelatedToIndexInfo(Type* source, IndexInfo* targetInfo,
										   bool reportErrors,
										   IntersectionState intersectionState) {
	Ternary result = Ternary::True;
	Type* keyType = targetInfo->keyType;
	std::vector<Symbol*> props;
	if ((source->flags & TypeFlagsIntersection) != 0) {
		props = c->getPropertiesOfUnionOrIntersectionType(source);
	} else {
		props = c->getPropertiesOfObjectType(source);
	}
	for (Symbol* prop : props) {
		// Skip over ignored JSX and symbol-named members
		if (isIgnoredJsxProperty(source, prop)) {
			continue;
		}
		if (c->isApplicableIndexType(
				c->getLiteralTypeFromProperty(
					prop, TypeFlagsStringOrNumberLiteralOrUnique, false),
				keyType)) {
			Type* propType = c->getNonMissingTypeOfSymbol(prop);
			Type* t;
			if (c->exactOptionalPropertyTypes ||
				(propType->flags & TypeFlagsUndefined) != 0 ||
				keyType == c->numberType ||
				(prop->flags & SymbolFlagsOptional) == 0) {
				t = propType;
			} else {
				t = c->getTypeWithFacts(propType, TypeFactsNEUndefined);
			}
			Ternary related =
				isRelatedToEx(t, targetInfo->valueType, RecursionFlagsBoth,
							  reportErrors, nullptr /*headMessage*/,
							  intersectionState);
			if (related == Ternary::False) {
				if (reportErrors) {
					reportError(
						
							Property_0_is_incompatible_with_index_signature,
						{c->symbolToString(prop)});
				}
				return Ternary::False;
			}
			result &= related;
		}
	}
	for (IndexInfo* info : c->getIndexInfosOfType(source)) {
		if (c->isApplicableIndexType(info->keyType, keyType)) {
			Ternary related = indexInfoRelatedTo(info, targetInfo, reportErrors,
												 intersectionState);
			if (related == Ternary::False) {
				return Ternary::False;
			}
			result &= related;
		}
	}
	return result;
}

// relater.go:4710
Ternary Relater::indexInfoRelatedTo(IndexInfo* sourceInfo, IndexInfo* targetInfo,
									bool reportErrors,
									IntersectionState intersectionState) {
	Ternary related = isRelatedToEx(sourceInfo->valueType, targetInfo->valueType,
									RecursionFlagsBoth, reportErrors,
									nullptr /*headMessage*/, intersectionState);
	if (related == Ternary::False && reportErrors) {
		if (sourceInfo->keyType == targetInfo->keyType) {
			reportError(X_0_index_signatures_are_incompatible,
						{c->TypeToString(sourceInfo->keyType)});
		} else {
			reportError(
				X_0_and_1_index_signatures_are_incompatible,
				{c->TypeToString(sourceInfo->keyType),
				 c->TypeToString(targetInfo->keyType)});
		}
	}
	return related;
}

// relater.go:4722
Ternary Relater::indexSignaturesIdenticalTo(Type* source, Type* target) {
	const std::vector<IndexInfo*>& sourceInfos = c->getIndexInfosOfType(source);
	const std::vector<IndexInfo*>& targetInfos = c->getIndexInfosOfType(target);
	if (sourceInfos.size() != targetInfos.size()) {
		return Ternary::False;
	}
	for (IndexInfo* targetInfo : targetInfos) {
		IndexInfo* sourceInfo =
			c->getIndexInfoOfType(source, targetInfo->keyType);
		if (!(sourceInfo != nullptr &&
			  isRelatedTo(sourceInfo->valueType, targetInfo->valueType,
						  RecursionFlagsBoth, false) != Ternary::False &&
			  sourceInfo->isReadonly == targetInfo->isReadonly)) {
			return Ternary::False;
		}
	}
	return Ternary::True;
}

// relater.go:4737
void Relater::reportErrorResults(Type* originalSource, Type* originalTarget,
								 Type* source, Type* target,
								 const DiagnosticMessage* headMessage) {
	bool sourceHasBase =
		c->getSingleBaseForNonAugmentingSubtype(originalSource) != nullptr;
	bool targetHasBase =
		c->getSingleBaseForNonAugmentingSubtype(originalTarget) != nullptr;
	if (originalSource->alias != nullptr || sourceHasBase) {
		source = originalSource;
	}
	if (originalTarget->alias != nullptr || targetHasBase) {
		target = originalTarget;
	}
	if ((source->flags & TypeFlagsObject) != 0 &&
		(target->flags & TypeFlagsObject) != 0) {
		tryElaborateArrayLikeErrors(source, target, true /*reportErrors*/);
	}
	if ((source->flags & TypeFlagsObject) != 0 &&
		(target->flags & TypeFlagsPrimitive) != 0) {
		tryElaborateErrorsForPrimitivesAndObjects(source, target);
	} else if (source->symbol != nullptr &&
			   (source->flags & TypeFlagsObject) != 0 &&
			   c->globalObjectType == source) {
		reportError(
			
				The_Object_type_is_assignable_to_very_few_other_types_Did_you_mean_to_use_the_any_type_instead);
	} else if ((source->objectFlags & ObjectFlagsJsxAttributes) != 0 &&
			   (target->flags & TypeFlagsIntersection) != 0) {
		const std::vector<Type*>& targetTypes = target->types();
		Type* intrinsicAttributes =
			c->getJsxType("IntrinsicAttributes", errorNode);
		Type* intrinsicClassAttributes =
			c->getJsxType("IntrinsicClassAttributes", errorNode);
		if (!c->isErrorType(intrinsicAttributes) &&
			!c->isErrorType(intrinsicClassAttributes) &&
			(std::find(targetTypes.begin(), targetTypes.end(),
					   intrinsicAttributes) != targetTypes.end() ||
			 std::find(targetTypes.begin(), targetTypes.end(),
					   intrinsicClassAttributes) != targetTypes.end())) {
			return;
		}
	} else if ((originalTarget->flags & TypeFlagsIntersection) != 0 &&
			   (originalTarget->objectFlags &
				ObjectFlagsIsNeverIntersection) != 0) {
		const DiagnosticMessage* message =
			
				The_intersection_0_was_reduced_to_never_because_property_1_has_conflicting_types_in_some_constituents;
		Symbol* prop =
			findOr(c->getPropertiesOfUnionOrIntersectionType(originalTarget),
				   [this](Symbol* p) {
					   return c->isDiscriminantWithNeverType(p);
				   },
				   (Symbol*) nullptr);
		if (prop == nullptr) {
			message =
				
					The_intersection_0_was_reduced_to_never_because_property_1_exists_in_multiple_constituents_and_is_private_in_some;
			prop = findOr(
				c->getPropertiesOfUnionOrIntersectionType(originalTarget),
				[this](Symbol* p) {
					return members_detail::isConflictingPrivateProperty(p);
				},
				(Symbol*) nullptr);
		}
		if (prop != nullptr) {
			reportError(message,
						{c->typeToStringEx(originalTarget,
										   nullptr /*enclosingDeclaration*/,
										   TypeFormatFlagsNoTypeReduction,
										   nullptr /*writer*/),
						 c->symbolToString(prop)});
		}
	}
	reportRelationError(headMessage, source, target);
	if ((source->flags & TypeFlagsTypeParameter) != 0 &&
		source->symbol != nullptr && !source->symbol->declarations.empty() &&
		c->getConstraintOfType(source) == nullptr) {
		Type* syntheticParam = c->cloneTypeParameter(source);
		syntheticParam->AsTypeParameter()->constraint = c->instantiateType(
			target, newSimpleTypeMapper(source, syntheticParam));
		if (c->hasNonCircularBaseConstraint(syntheticParam)) {
			std::string targetConstraintString = c->TypeToString(target);
			relatedInfo.push_back(NewDiagnosticForNode(
				source->symbol->declarations[0],
				
					This_type_parameter_might_need_an_extends_0_constraint,
				{targetConstraintString}));
		}
	}
}

// relater.go:4783
void Relater::reportRelationError(const DiagnosticMessage* message,
								  Type* source, Type* target) {
	auto names = c->getTypeNamesForErrorDisplay(source, target);
	std::string sourceType = names.first;
	std::string targetType = names.second;
	Type* generalizedSource = source;
	std::string generalizedSourceType = sourceType;
	// Don't generalize on 'never' - we really want the original type
	// to be displayed for use-cases like 'assertNever'.
	if ((target->flags & TypeFlagsNever) == 0 && isLiteralType(source) &&
		!c->typeCouldHaveTopLevelSingletonTypes(target)) {
		generalizedSource = c->getBaseTypeOfLiteralType(source);
		generalizedSourceType =
			c->getTypeNameForErrorDisplay(generalizedSource);
	}
	// If `target` is of indexed access type (and `source` it is not), we use the object type of `target` for better error reporting
	TypeFlags targetFlags;
	if ((target->flags & TypeFlagsIndexedAccess) != 0 &&
		(source->flags & TypeFlagsIndexedAccess) == 0) {
		targetFlags = target->AsIndexedAccessType()->objectType->flags;
	} else {
		targetFlags = target->flags;
	}
	if ((targetFlags & TypeFlagsTypeParameter) != 0 &&
		target != c->markerSuperTypeForCheck &&
		target != c->markerSubTypeForCheck) {
		Type* constraint = c->getBaseConstraintOfType(target);
		if (isDistributedTypeParameter(target) &&
			c->isTypeAssignableTo(
				generalizedSource,
				target->AsTypeParameter()->constraint)) {
			reportError(
				
					X_0_is_only_assignable_to_the_non_distributed_1_but_1_has_been_distributed_here,
				{generalizedSourceType, targetType});
		} else if (constraint != nullptr &&
				   c->isTypeAssignableTo(generalizedSource, constraint)) {
			reportError(
				
					X_0_is_assignable_to_the_constraint_of_type_1_but_1_could_be_instantiated_with_a_different_subtype_of_constraint_2,
				{generalizedSourceType, targetType,
				 c->TypeToString(constraint)});
		} else if (constraint != nullptr &&
				   c->isTypeAssignableTo(source, constraint)) {
			reportError(
				
					X_0_is_assignable_to_the_constraint_of_type_1_but_1_could_be_instantiated_with_a_different_subtype_of_constraint_2,
				{sourceType, targetType, c->TypeToString(constraint)});
		} else {
			errorChain = nullptr; // Only report this error once
			reportError(
				
					X_0_could_be_instantiated_with_an_arbitrary_type_which_could_be_unrelated_to_1,
				{targetType, generalizedSourceType});
		}
	}
	if (message == nullptr) {
		if (relation == c->comparableRelation) {
			message = Type_0_is_not_comparable_to_type_1;
		} else if (sourceType == targetType) {
			message =
				
					Type_0_is_not_assignable_to_type_1_Two_different_types_with_this_name_exist_but_they_are_unrelated;
		} else if (c->exactOptionalPropertyTypes &&
				   !c->getExactOptionalUnassignableProperties(source, target)
						.empty()) {
			message =
				
					Type_0_is_not_assignable_to_type_1_with_exactOptionalPropertyTypes_Colon_true_Consider_adding_undefined_to_the_types_of_the_target_s_properties;
		} else {
			if ((source->flags & TypeFlagsStringLiteral) != 0 &&
				(target->flags & TypeFlagsUnion) != 0) {
				Type* suggestedType =
					c->getSuggestedTypeForNonexistentStringLiteralType(source,
																	 target);
				if (suggestedType != nullptr) {
					reportError(
						
							Type_0_is_not_assignable_to_type_1_Did_you_mean_2,
						{generalizedSourceType, targetType,
						 c->TypeToString(suggestedType)});
					return;
				}
			}
			message = Type_0_is_not_assignable_to_type_1;
		}
	} else if (message ==
				   
					   Argument_of_type_0_is_not_assignable_to_parameter_of_type_1 &&
			   c->exactOptionalPropertyTypes &&
			   !c->getExactOptionalUnassignableProperties(source, target)
					.empty()) {
		message =
			
				Argument_of_type_0_is_not_assignable_to_parameter_of_type_1_with_exactOptionalPropertyTypes_Colon_true_Consider_adding_undefined_to_the_types_of_the_target_s_properties;
	}
	const DiagnosticMessage* chainMessage = getChainMessage(0);
	// Suppress if next message is an excess property error
	if (chainMessage ==
			
				Object_literal_may_only_specify_known_properties_and_0_does_not_exist_in_type_1 ||
		chainMessage ==
			
				Object_literal_may_only_specify_known_properties_but_0_does_not_exist_in_type_1_Did_you_mean_to_write_2) {
		return;
	}
	// Suppress if next message is an excessive complexity/stack depth message for source and target or a readonly
	// vs. mutable error for source and target
	if (chainMessage ==
			Excessive_complexity_comparing_types_0_and_1 ||
		chainMessage ==
			
				The_type_0_is_readonly_and_cannot_be_assigned_to_the_mutable_type_1) {
		if (chainArgsMatch({generalizedSourceType, targetType})) {
			return;
		}
	}
	// Suppress if next message is a missing property message for source and target and we're not
	// reporting on conversion or interface implementation
	if (chainMessage ==
		Property_0_is_missing_in_type_1_but_required_in_type_2) {
		if (!isConversionOrInterfaceImplementationMessage(message) &&
			chainArgsMatch(
				{std::nullopt, generalizedSourceType, targetType})) {
			return;
		}
	}
	// Suppress if next message is a missing property message for source and target and we're not
	// reporting on conversion or interface implementation
	if (chainMessage ==
			
				Type_0_is_missing_the_following_properties_from_type_1_Colon_2_and_3_more ||
		chainMessage ==
			
				Type_0_is_missing_the_following_properties_from_type_1_Colon_2) {
		if (!isConversionOrInterfaceImplementationMessage(message) &&
			chainArgsMatch({generalizedSourceType, targetType})) {
			return;
		}
	}
	reportError(message, {generalizedSourceType, targetType});
}

// relater.go:4864
void Relater::reportError(const DiagnosticMessage* message,
						  const std::vector<std::string>& args) {
	if (message == Types_of_property_0_are_incompatible) {
		// Suppress if next message is an excess property error
		const DiagnosticMessage* chainMessage0 = getChainMessage(0);
		if (chainMessage0 ==
				
					Object_literal_may_only_specify_known_properties_and_0_does_not_exist_in_type_1 ||
			chainMessage0 ==
				
					Object_literal_may_only_specify_known_properties_but_0_does_not_exist_in_type_1_Did_you_mean_to_write_2) {
			return;
		}
		// Transform a property incompatibility message for property 'x' followed by some elaboration message
		// followed by a signature return type incompatibility message into a single return type incompatibility
		// message for 'x()' or 'x(...)'
		std::string arg;
		std::vector<std::string> newArgs = args;
		const DiagnosticMessage* chainMessage1 = getChainMessage(1);
		if (chainMessage1 ==
			
				Call_signatures_with_no_arguments_have_incompatible_return_types_0_and_1) {
			arg = getPropertyNameArg(args[0]) + "()";
		} else if (chainMessage1 ==
				   
					   Construct_signatures_with_no_arguments_have_incompatible_return_types_0_and_1) {
			arg = "new " + getPropertyNameArg(args[0]) + "()";
		} else if (chainMessage1 ==
				   
					   Call_signature_return_types_0_and_1_are_incompatible) {
			arg = getPropertyNameArg(args[0]) + "(...)";
		} else if (chainMessage1 ==
				   
					   Construct_signature_return_types_0_and_1_are_incompatible) {
			arg = "new " + getPropertyNameArg(args[0]) + "(...)";
		}
		if (!arg.empty()) {
			message =
				
					The_types_returned_by_0_are_incompatible_between_these_types;
			newArgs[0] = arg;
			errorChain = errorChain->next->next;
		}
		// Transform a property incompatibility message for property 'x' followed by some elaboration message
		// followed by a property incompatibility message for property 'y' into a single property incompatibility
		// message for 'x.y'
		chainMessage1 = getChainMessage(1);
		if (chainMessage1 ==
				Types_of_property_0_are_incompatible ||
			chainMessage1 ==
				
					The_types_of_0_are_incompatible_between_these_types ||
			chainMessage1 ==
				
					The_types_returned_by_0_are_incompatible_between_these_types) {
			std::string head = getPropertyNameArg(newArgs[0]);
			std::string tail =
				getPropertyNameArg(errorChain->next->args[0]);
			std::string dotted = addToDottedName(head, tail);
			errorChain = errorChain->next->next;
			if (message ==
				Types_of_property_0_are_incompatible) {
				message =
					
						The_types_of_0_are_incompatible_between_these_types;
			}
			reportError(message, {dotted});
			return;
		}
		errorChain =
			new ErrorChain{errorChain, message, std::move(newArgs)};
		return;
	}
	errorChain = new ErrorChain{errorChain, message, args};
}

// relater.go:4934
const DiagnosticMessage* Relater::getChainMessage(int index) {
	ErrorChain* e = errorChain;
	for (;;) {
		if (e == nullptr) {
			return nullptr;
		}
		if (index == 0) {
			return e->message;
		}
		e = e->next;
		index--;
	}
}

// Return true if the arguments of the first entry on the error chain match the
// given arguments (where nil acts as a wildcard).
// relater.go:4950
bool Relater::chainArgsMatch(
	const std::vector<std::optional<std::string>>& args) {
	for (size_t i = 0; i < args.size(); i++) {
		if (args[i].has_value() && args[i].value() != errorChain->args[i]) {
			return false;
		}
	}
	return true;
}

// An object type S is considered to be derived from an object type T if
// S is a union type and every constituent of S is derived from T,
// T is a union type and S is derived from at least one constituent of T, or
// S is an intersection type and some constituent of S is derived from T, or
// S is a type variable with a base constraint that is derived from T, or
// T is {} and S is an object-like type (ensuring {} is less derived than Object), or
// T is one of the global types Object and Function and S is a subtype of T, or
// T occurs directly or indirectly in an 'extends' clause of S.
// Note that this check ignores type parameters and only considers the
// inheritance hierarchy.
// relater.go:4995
bool Checker::isTypeDerivedFrom(Type* source, Type* target) {
	if ((source->flags & TypeFlagsUnion) != 0) {
		return everyType(source, [this, target](Type* t) {
			return isTypeDerivedFrom(t, target);
		});
	}
	if ((target->flags & TypeFlagsUnion) != 0) {
		return someOf(target->types(), [this, source](Type* t) {
			return isTypeDerivedFrom(source, t);
		});
	}
	if ((source->flags & TypeFlagsIntersection) != 0) {
		return someOf(source->types(), [this, target](Type* t) {
			return isTypeDerivedFrom(t, target);
		});
	}
	if ((source->flags & TypeFlagsInstantiableNonPrimitive) != 0) {
		Type* constraint = getBaseConstraintOfType(source);
		if (constraint == nullptr) {
			constraint = unknownType;
		}
		return isTypeDerivedFrom(constraint, target);
	}
	if (IsEmptyAnonymousObjectType(target)) {
		return (source->flags & (TypeFlagsObject | TypeFlagsNonPrimitive)) != 0;
	}
	if (target == globalObjectType) {
		return (source->flags & (TypeFlagsObject | TypeFlagsNonPrimitive)) !=
				   0 &&
			   !IsEmptyAnonymousObjectType(source);
	}
	if (target == globalFunctionType) {
		return (source->flags & TypeFlagsObject) != 0 &&
			   isFunctionObjectType(source);
	}
	return hasBaseType(source, getTargetType(target)) ||
		   (isArrayType(target) && !isReadonlyArrayType(target) &&
			isTypeDerivedFrom(source, globalReadonlyArrayType));
}

// relater.go:5026
bool Checker::isDistributionDependent(ConditionalRoot* root) {
	return root->isDistributive &&
		   (isTypeParameterPossiblyReferenced(
				root->checkType,
				root->node->as<ConditionalTypeNode>()->TrueType) ||
			isTypeParameterPossiblyReferenced(
				root->checkType,
				root->node->as<ConditionalTypeNode>()->FalseType));
}

// relater.go:5030
void Relater::traceUnionsOrIntersectionsTooLarge(Type* source, Type* target) {
	Tracer* tr = c->tracer;
	if (tr == nullptr) {
		return;
	}
	if ((source->flags & TypeFlagsUnionOrIntersection) != 0 &&
		(target->flags & TypeFlagsUnionOrIntersection) != 0) {
		if ((source->objectFlags & target->objectFlags &
			 ObjectFlagsPrimitiveUnion) != 0) {
			// There's a fast path for comparing primitive unions
			return;
		}
		size_t sourceSize = source->types().size();
		size_t targetSize = target->types().size();
		if (sourceSize * targetSize > 1'000'000) {
			tr->Instant(
				tracing::PhaseCheckTypes,
				"traceUnionsOrIntersectionsTooLarge_DepthLimit",
				{{"sourceId", source->id},
				 {"sourceSize", sourceSize},
				 {"targetId", target->id},
				 {"targetSize", targetSize}});
		}
	}
}

}  // namespace tsc::checker
