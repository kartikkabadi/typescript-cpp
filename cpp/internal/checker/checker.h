// Port of tsc/internal/checker — the type checker.
// checker.h: key types, enums, small structs, and the Checker class.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <unordered_set>
#include <variant>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/diagnostics_util.h"
#include "internal/ast/flow.h"
#include "internal/ast/symbol.h"
#include "internal/checker/types.h"
#include "internal/tracing/tracing.h"
#include "internal/core/arena.h"
#include "internal/core/linkstore.h"
#include "internal/core/types.h"
#include "internal/jsnum/jsnum.h"
#include "internal/scanner/scanner.h"
#include "internal/nodebuilder/types.h"
#include "internal/printer/emitcontext.h"

namespace tsc::checker {


// TypeSystemEntity / TypeSystemPropertyName — Go uses `any`-shaped unions;
// C++ keeps them as the small tagged unions they always are in practice.
using TypeSystemEntity = void*; // *Type | *Symbol | *Signature
enum class TypeSystemPropertyName : int32_t {
	Type = 0,
	ResolvedBaseConstructorType = 1,
	DeclaredType = 2,
	ResolvedReturnType = 3,
	ResolvedBaseConstraint = 4,
	ResolvedTypeArguments = 5,
	ResolvedBaseTypes = 6,
	WriteType = 7,
	InitializerIsUndefined = 8,
	AliasTarget = 9,
};

// orderedSet — Go orderedSet[T] from utilities.go: insertion order, lazy dedup map.
template <class T>
struct orderedSet {
	std::vector<T> values;
	std::unordered_set<T> valuesByKey;
	bool hasMap = false;
	static constexpr int mapThreshold = 16;

	bool contains(T value) {
		if (!hasMap) {
			return std::find(values.begin(), values.end(), value) != values.end();
		}
		return valuesByKey.count(value) != 0;
	}
	void add(T value) {
		values.push_back(value);
		if (!hasMap) {
			if ((int)values.size() <= mapThreshold) {
				return;
			}
			hasMap = true;
			valuesByKey.insert(values.begin(), values.end() - 1);
		}
		valuesByKey.insert(value);
	}
};

// OrderedSet — port of collections.OrderedSet.
template <class T>
struct OrderedSet {
	std::vector<T> items;
	std::unordered_set<T> seen;

	bool Add(T v) {
		if (seen.insert(v).second) {
			items.push_back(v);
			return true;
		}
		return false;
	}
	bool Has(T v) const { return seen.count(v) != 0; }
	void Clear() {
		items.clear();
		seen.clear();
	}
	int Size() const { return static_cast<int>(items.size()); }
};

// TypeResolution

struct TypeResolution {
	TypeSystemEntity target{};
	TypeSystemPropertyName propertyName{};
	bool result{};
};

// ContextualInfo

struct ContextualInfo {
	Node* node{};
	Type* t{};
	bool isCache{};
};

// IntersectionFlags

using IntersectionFlags = uint32_t;
inline constexpr IntersectionFlags IntersectionFlagsNone = 0;
inline constexpr IntersectionFlags IntersectionFlagsNoSupertypeReduction = 1 << 0;
inline constexpr IntersectionFlags IntersectionFlagsNoConstraintReduction = 1 << 1;

// InferenceContextInfo

struct InferenceContext;

struct InferenceContextInfo {
	Node* node{};
	InferenceContext* context{};
};

// WideningKind

enum class WideningKind : int32_t {
	Normal,
	FunctionReturn,
	GeneratorNext,
	GeneratorYield,
};

// EnumLiteralKey

struct EnumLiteralKey {
	Symbol* enumSymbol{};
	// string | jsnum.Number | jsnum.PseudoBigInt | nil
	std::variant<std::monostate, std::string, Number, PseudoBigInt> value;
	bool operator==(const EnumLiteralKey&) const = default;
};

// EnumRelationKey

struct EnumRelationKey {
	SymbolId sourceId{};
	SymbolId targetId{};
	bool operator==(const EnumRelationKey&) const = default;
};

// TypeCacheKind

enum class CachedTypeKind : int32_t {
	LiteralUnionBaseType,
	IndexType,
	StringIndexType,
	EquivalentBaseType,
	ApparentType,
	AwaitedType,
	EvolvingArrayType,
	ArrayLiteralType,
	PermissiveInstantiation,
	RestrictiveInstantiation,
	RestrictiveTypeParameter,
	IndexedAccessForReading,
	IndexedAccessForWriting,
	Widened,
	RegularObjectLiteral,
	PromisedTypeOfPromise,
	DefaultOnlyType,
	SyntheticType,
	DecoratorContext,
	DecoratorContextStatic,
	DecoratorContextPrivate,
	DecoratorContextPrivateStatic,
};

struct CachedTypeKey {
	CachedTypeKind kind{};
	TypeId typeId{};
	bool operator==(const CachedTypeKey&) const = default;
};

// NarrowedTypeKey

struct NarrowedTypeKey {
	Type* t{};
	Type* candidate{};
	bool assumeTrue{};
	bool checkDerived{};
	bool operator==(const NarrowedTypeKey&) const = default;
};

// UnionReduction (forward decl of enum used by UnionOfUnionKey)

enum class UnionReduction : int32_t {
	None = 0,
	Literal = 1,
	Subtype = 2,
};
inline bool operator&(UnionReduction a, UnionReduction b) {
	return (static_cast<int32_t>(a) & static_cast<int32_t>(b)) != 0;
}
inline constexpr UnionReduction UnionReductionNone = UnionReduction::None;
inline constexpr UnionReduction UnionReductionLiteral = UnionReduction::Literal;
inline constexpr UnionReduction UnionReductionSubtype = UnionReduction::Subtype;

struct UnionOfUnionKey {
	TypeId id1{};
	TypeId id2{};
	UnionReduction r{};
	CacheKey a;
	bool operator==(const UnionOfUnionKey&) const = default;
};

// CachedSignatureKey

struct CachedSignatureKey {
	Signature* sig{};
	CacheKey key; // Type list key or one of the special keys below
	bool operator==(const CachedSignatureKey&) const = default;
};

// Special signature keys
inline const CacheKey SignatureKeyErased{std::vector<uint64_t>{0xffffffffffffff01ull}};
inline const CacheKey SignatureKeyCanonical{std::vector<uint64_t>{0xffffffffffffff02ull}};
inline const CacheKey SignatureKeyBase{std::vector<uint64_t>{0xffffffffffffff03ull}};
inline const CacheKey SignatureKeyInner{std::vector<uint64_t>{0xffffffffffffff04ull}};
inline const CacheKey SignatureKeyOuter{std::vector<uint64_t>{0xffffffffffffff05ull}};

// StringMappingKey

struct StringMappingKey {
	Symbol* s{};
	Type* t{};
	bool operator==(const StringMappingKey&) const = default;
};

// AssignmentReducedKey

struct AssignmentReducedKey {
	TypeId id1{};
	TypeId id2{};
	bool operator==(const AssignmentReducedKey&) const = default;
};

// DiscriminatedContextualTypeKey

struct DiscriminatedContextualTypeKey {
	NodeId nodeId{};
	TypeId typeId{};
	bool operator==(const DiscriminatedContextualTypeKey&) const = default;
};

// InstantiationExpressionKey

struct InstantiationExpressionKey {
	NodeId nodeId{};
	TypeId typeId{};
	bool operator==(const InstantiationExpressionKey&) const = default;
};

// SubstitutionTypeKey

struct SubstitutionTypeKey {
	TypeId baseId{};
	TypeId constraintId{};
	bool operator==(const SubstitutionTypeKey&) const = default;
};

// ReverseMappedTypeKey

struct ReverseMappedTypeKey {
	TypeId sourceId{};
	TypeId targetId{};
	TypeId constraintId{};
	bool operator==(const ReverseMappedTypeKey&) const = default;
};

// IterationUse

using IterationUse = uint32_t;
inline constexpr IterationUse IterationUseAllowsSyncIterablesFlag = 1 << 0;
inline constexpr IterationUse IterationUseAllowsAsyncIterablesFlag = 1 << 1;
inline constexpr IterationUse IterationUseAllowsStringInputFlag = 1 << 2;
inline constexpr IterationUse IterationUseForOfFlag = 1 << 3;
inline constexpr IterationUse IterationUseYieldStarFlag = 1 << 4;
inline constexpr IterationUse IterationUseSpreadFlag = 1 << 5;
inline constexpr IterationUse IterationUseDestructuringFlag = 1 << 6;
inline constexpr IterationUse IterationUsePossiblyOutOfBounds = 1 << 7;
inline constexpr IterationUse IterationUseElement = IterationUseAllowsSyncIterablesFlag;
inline constexpr IterationUse IterationUseSpread =
	IterationUseAllowsSyncIterablesFlag | IterationUseSpreadFlag;
inline constexpr IterationUse IterationUseDestructuring =
	IterationUseAllowsSyncIterablesFlag | IterationUseDestructuringFlag;
inline constexpr IterationUse IterationUseForOf =
	IterationUseAllowsSyncIterablesFlag | IterationUseAllowsStringInputFlag | IterationUseForOfFlag;
inline constexpr IterationUse IterationUseForAwaitOf =
	IterationUseAllowsSyncIterablesFlag | IterationUseAllowsAsyncIterablesFlag |
	IterationUseAllowsStringInputFlag | IterationUseForOfFlag;
inline constexpr IterationUse IterationUseYieldStar =
	IterationUseAllowsSyncIterablesFlag | IterationUseYieldStarFlag;
inline constexpr IterationUse IterationUseAsyncYieldStar =
	IterationUseAllowsSyncIterablesFlag | IterationUseAllowsAsyncIterablesFlag |
	IterationUseYieldStarFlag;
inline constexpr IterationUse IterationUseGeneratorReturnType = IterationUseAllowsSyncIterablesFlag;
inline constexpr IterationUse IterationUseAsyncGeneratorReturnType =
	IterationUseAllowsAsyncIterablesFlag;
inline constexpr IterationUse IterationUseCacheFlags =
	IterationUseAllowsSyncIterablesFlag | IterationUseAllowsAsyncIterablesFlag | IterationUseForOfFlag;

// IterationTypesKey

struct IterationTypesKey {
	TypeId typeId{};
	IterationUse use{};
	bool operator==(const IterationTypesKey&) const = default;
};

// PropertiesTypesKey

struct PropertiesTypesKey {
	TypeId typeId{};
	TypeFlags include{};
	bool includeOrigin{};
	bool operator==(const PropertiesTypesKey&) const = default;
};

// IterationTypes

struct IterationTypes {
	Type* yieldType{};
	Type* returnType{};
	Type* nextType{};
};

enum class IterationTypeKind : int32_t {
	Yield,
	Return,
	Next,
};

struct IterationTypesResolver {
	std::string iteratorSymbolName;
	std::function<Type*()> getGlobalIteratorType;
	std::function<Type*()> getGlobalIterableType;
	std::function<Type*()> getGlobalIterableTypeChecked;
	std::function<Type*()> getGlobalIterableIteratorType;
	std::function<Type*()> getGlobalIterableIteratorTypeChecked;
	std::function<Type*()> getGlobalIteratorObjectType;
	std::function<Type*()> getGlobalGeneratorType;
	std::function<std::vector<Type*>()> getGlobalBuiltinIteratorTypes;
	std::function<Type*(Type*, Node*)> resolveIterationType;
	const DiagnosticMessage* mustHaveANextMethodDiagnostic;
	const DiagnosticMessage* mustBeAMethodDiagnostic;
	const DiagnosticMessage* mustHaveAValueDiagnostic;
};

struct TypeParameterResolverBase;

// FlowLoopKey / FlowLoopInfo

struct FlowLoopKey {
	FlowNode* flowNode{};
	CacheKey refKey;
	bool operator==(const FlowLoopKey&) const = default;
};

struct FlowLoopInfo {
	FlowLoopKey key;
	std::vector<Type*> types;
};

// InferenceFlags

using InferenceFlags = uint32_t;
inline constexpr InferenceFlags InferenceFlagsNone = 0;
inline constexpr InferenceFlags InferenceFlagsNoDefault = 1 << 0;
inline constexpr InferenceFlags InferenceFlagsAnyDefault = 1 << 1;
inline constexpr InferenceFlags InferenceFlagsSkippedGenericFunction = 1 << 2;
inline constexpr InferenceFlags InferenceFlagsNoConstraintChecks = 1 << 3;

// InferencePriority

using InferencePriority = int32_t;
inline constexpr InferencePriority InferencePriorityNone = 0;
inline constexpr InferencePriority InferencePriorityNakedTypeVariable = 1 << 0;
inline constexpr InferencePriority InferencePrioritySpeculativeTuple = 1 << 1;
inline constexpr InferencePriority InferencePrioritySubstituteSource = 1 << 2;
inline constexpr InferencePriority InferencePriorityHomomorphicMappedType = 1 << 3;
inline constexpr InferencePriority InferencePriorityPartialHomomorphicMappedType = 1 << 4;
inline constexpr InferencePriority InferencePriorityMappedTypeConstraint = 1 << 5;
inline constexpr InferencePriority InferencePriorityContravariantConditional = 1 << 6;
inline constexpr InferencePriority InferencePriorityReturnType = 1 << 7;
inline constexpr InferencePriority InferencePriorityLiteralKeyof = 1 << 8;
inline constexpr InferencePriority InferencePriorityNoConstraints = 1 << 9;

// CheckMode (checker.go:34)

using CheckMode = uint32_t;
inline constexpr CheckMode CheckModeNormal = 0;               // Normal type checking
inline constexpr CheckMode CheckModeContextual = 1 << 0;      // Explicitly assigned contextual type, therefore not cacheable
inline constexpr CheckMode CheckModeInferential = 1 << 1;     // Inferential typing
inline constexpr CheckMode CheckModeSkipContextSensitive = 1 << 2; // Skip context sensitive function expressions
inline constexpr CheckMode CheckModeSkipGenericFunctions = 1 << 3; // Skip single signature generic functions
inline constexpr CheckMode CheckModeIsForSignatureHelp = 1 << 4;   // Call resolution for purposes of signature help
inline constexpr CheckMode CheckModeRestBindingElement = 1 << 5;   // Checking a type that is going to be used to determine the type of a rest binding element
inline constexpr CheckMode CheckModeTypeOnly = 1 << 6;        // Called from getTypeOfExpression, diagnostics may be omitted
inline constexpr CheckMode CheckModeForceTuple = 1 << 7;
inline constexpr InferencePriority InferencePriorityAlwaysStrict = 1 << 10;
inline constexpr InferencePriority InferencePriorityMaxValue = 1 << 11;
inline constexpr InferencePriority InferencePriorityCircularity = -1;
inline constexpr InferencePriority InferencePriorityPriorityImpliesCombination =
	InferencePriorityReturnType | InferencePriorityMappedTypeConstraint | InferencePriorityLiteralKeyof;

struct IntraExpressionInferenceSite {
	Node* node{};
	Type* t{};
};

struct InferenceInfo {
	Type* typeParameter{};
	std::vector<Type*> candidates;
	std::vector<Type*> contraCandidates;
	Type* inferredType{};
	InferencePriority priority{};
	bool topLevel{};
	bool isFixed{};
	int impliedArity{-1};
};

struct InferenceContext {
	std::vector<InferenceInfo*> inferences;
	Signature* signature{};
	InferenceFlags flags{};
	std::function<Ternary(Type*, Type*, bool)> compareTypes;
	TypeMapper* mapper{};
	TypeMapper* nonFixingMapper{};
	TypeMapper* returnMapper{};
	TypeMapper* outerReturnMapper{};
	std::vector<Type*> inferredTypeParameters;
	std::vector<IntraExpressionInferenceSite> intraExpressionInferenceSites;
};

// InferenceState (free-list pooled)

struct InferenceState {
	InferenceContext* context{};
	std::vector<InferenceInfo*> inferences;
	std::vector<Type*> typeParameters;
	std::vector<InferencePriority> priorities;
	std::vector<Type*> inferred;
	std::vector<InferenceInfo*> fixed;
	InferenceState* next{};
};

// DeclarationMeaning

using DeclarationMeaning = uint32_t;
inline constexpr DeclarationMeaning DeclarationMeaningGetAccessor = 1 << 0;
inline constexpr DeclarationMeaning DeclarationMeaningSetAccessor = 1 << 1;
inline constexpr DeclarationMeaning DeclarationMeaningPropertyAssignment = 1 << 2;
inline constexpr DeclarationMeaning DeclarationMeaningMethod = 1 << 3;
inline constexpr DeclarationMeaning DeclarationMeaningPrivateStatic = 1 << 4;
inline constexpr DeclarationMeaning DeclarationMeaningGetOrSetAccessor =
	DeclarationMeaningGetAccessor | DeclarationMeaningSetAccessor;
inline constexpr DeclarationMeaning DeclarationMeaningPropertyAssignmentOrMethod =
	DeclarationMeaningPropertyAssignment | DeclarationMeaningMethod;

using DeclarationSpaces = int32_t;
inline constexpr DeclarationSpaces DeclarationSpacesNone = 0;
inline constexpr DeclarationSpaces DeclarationSpacesExportValue = 1 << 0;
inline constexpr DeclarationSpaces DeclarationSpacesExportType = 1 << 1;
inline constexpr DeclarationSpaces DeclarationSpacesExportNamespace = 1 << 2;

// IntrinsicTypeKind

enum class IntrinsicTypeKind : int32_t {
	Unknown,
	Uppercase,
	Lowercase,
	Capitalize,
	Uncapitalize,
	NoInfer,
};

// MappedTypeModifiers

using MappedTypeModifiers = uint32_t;
inline constexpr MappedTypeModifiers MappedTypeModifiersIncludeReadonly = 1 << 0;
inline constexpr MappedTypeModifiers MappedTypeModifiersExcludeReadonly = 1 << 1;
inline constexpr MappedTypeModifiers MappedTypeModifiersIncludeOptional = 1 << 2;
inline constexpr MappedTypeModifiers MappedTypeModifiersExcludeOptional = 1 << 3;

enum class MappedTypeNameTypeKind : int32_t {
	None,
	Filtering,
	Remapping,
};

enum class ReferenceHint : int32_t {
	Unspecified,
	Identifier,
	Property,
	ExportAssignment,
	Jsx,
	ExportImportEquals,
	ExportSpecifier,
	Decorator,
};

// TypeFacts

using TypeFacts = uint32_t;
inline constexpr TypeFacts TypeFactsNone = 0;
inline constexpr TypeFacts TypeFactsTypeofEQString = 1 << 0;
inline constexpr TypeFacts TypeFactsTypeofEQNumber = 1 << 1;
inline constexpr TypeFacts TypeFactsTypeofEQBigInt = 1 << 2;
inline constexpr TypeFacts TypeFactsTypeofEQBoolean = 1 << 3;
inline constexpr TypeFacts TypeFactsTypeofEQSymbol = 1 << 4;
inline constexpr TypeFacts TypeFactsTypeofEQObject = 1 << 5;
inline constexpr TypeFacts TypeFactsTypeofEQFunction = 1 << 6;
inline constexpr TypeFacts TypeFactsTypeofEQHostObject = 1 << 7;
inline constexpr TypeFacts TypeFactsTypeofNEString = 1 << 8;
inline constexpr TypeFacts TypeFactsTypeofNENumber = 1 << 9;
inline constexpr TypeFacts TypeFactsTypeofNEBigInt = 1 << 10;
inline constexpr TypeFacts TypeFactsTypeofNEBoolean = 1 << 11;
inline constexpr TypeFacts TypeFactsTypeofNESymbol = 1 << 12;
inline constexpr TypeFacts TypeFactsTypeofNEObject = 1 << 13;
inline constexpr TypeFacts TypeFactsTypeofNEFunction = 1 << 14;
inline constexpr TypeFacts TypeFactsTypeofNEHostObject = 1 << 15;
inline constexpr TypeFacts TypeFactsEQUndefined = 1 << 16;
inline constexpr TypeFacts TypeFactsEQNull = 1 << 17;
inline constexpr TypeFacts TypeFactsEQUndefinedOrNull = 1 << 18;
inline constexpr TypeFacts TypeFactsNEUndefined = 1 << 19;
inline constexpr TypeFacts TypeFactsNENull = 1 << 20;
inline constexpr TypeFacts TypeFactsNEUndefinedOrNull = 1 << 21;
inline constexpr TypeFacts TypeFactsTruthy = 1 << 22;
inline constexpr TypeFacts TypeFactsFalsy = 1 << 23;
inline constexpr TypeFacts TypeFactsIsUndefined = 1 << 24;
inline constexpr TypeFacts TypeFactsIsNull = 1 << 25;
inline constexpr TypeFacts TypeFactsIsUndefinedOrNull = TypeFactsIsUndefined | TypeFactsIsNull;
inline constexpr TypeFacts TypeFactsAll = (1 << 27) - 1;
inline constexpr TypeFacts TypeFactsBaseStringStrictFacts =
	TypeFactsTypeofEQString | TypeFactsTypeofNENumber | TypeFactsTypeofNEBigInt |
	TypeFactsTypeofNEBoolean | TypeFactsTypeofNESymbol | TypeFactsTypeofNEObject |
	TypeFactsTypeofNEFunction | TypeFactsTypeofNEHostObject | TypeFactsNEUndefined |
	TypeFactsNENull | TypeFactsNEUndefinedOrNull;
inline constexpr TypeFacts TypeFactsBaseStringFacts =
	TypeFactsBaseStringStrictFacts | TypeFactsEQUndefined | TypeFactsEQNull |
	TypeFactsEQUndefinedOrNull | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsStringStrictFacts =
	TypeFactsBaseStringStrictFacts | TypeFactsTruthy | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsStringFacts = TypeFactsBaseStringFacts | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsEmptyStringStrictFacts =
	TypeFactsBaseStringStrictFacts | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsEmptyStringFacts = TypeFactsBaseStringFacts;
inline constexpr TypeFacts TypeFactsNonEmptyStringStrictFacts =
	TypeFactsBaseStringStrictFacts | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsNonEmptyStringFacts =
	TypeFactsBaseStringFacts | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsBaseNumberStrictFacts =
	TypeFactsTypeofEQNumber | TypeFactsTypeofNEString | TypeFactsTypeofNEBigInt |
	TypeFactsTypeofNEBoolean | TypeFactsTypeofNESymbol | TypeFactsTypeofNEObject |
	TypeFactsTypeofNEFunction | TypeFactsTypeofNEHostObject | TypeFactsNEUndefined |
	TypeFactsNENull | TypeFactsNEUndefinedOrNull;
inline constexpr TypeFacts TypeFactsBaseNumberFacts =
	TypeFactsBaseNumberStrictFacts | TypeFactsEQNull | TypeFactsEQUndefinedOrNull | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsNumberStrictFacts =
	TypeFactsBaseNumberStrictFacts | TypeFactsTruthy | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsNumberFacts = TypeFactsBaseNumberFacts | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsZeroNumberStrictFacts =
	TypeFactsBaseNumberStrictFacts | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsZeroNumberFacts = TypeFactsBaseNumberFacts;
inline constexpr TypeFacts TypeFactsNonZeroNumberStrictFacts =
	TypeFactsBaseNumberStrictFacts | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsNonZeroNumberFacts =
	TypeFactsBaseNumberFacts | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsBaseBigIntStrictFacts =
	TypeFactsTypeofEQBigInt | TypeFactsTypeofNEString | TypeFactsTypeofNENumber |
	TypeFactsTypeofNEBoolean | TypeFactsTypeofNESymbol | TypeFactsTypeofNEObject |
	TypeFactsTypeofNEFunction | TypeFactsTypeofNEHostObject | TypeFactsNEUndefined |
	TypeFactsNENull | TypeFactsNEUndefinedOrNull;
inline constexpr TypeFacts TypeFactsBaseBigIntFacts =
	TypeFactsBaseBigIntStrictFacts | TypeFactsEQNull | TypeFactsEQUndefinedOrNull | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsBigIntStrictFacts =
	TypeFactsBaseBigIntStrictFacts | TypeFactsTruthy | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsBigIntFacts = TypeFactsBaseBigIntFacts | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsZeroBigIntStrictFacts =
	TypeFactsBaseBigIntStrictFacts | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsZeroBigIntFacts = TypeFactsBaseBigIntFacts;
inline constexpr TypeFacts TypeFactsNonZeroBigIntStrictFacts =
	TypeFactsBaseBigIntStrictFacts | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsNonZeroBigIntFacts =
	TypeFactsBaseBigIntFacts | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsBaseBooleanStrictFacts =
	TypeFactsTypeofEQBoolean | TypeFactsTypeofNEString | TypeFactsTypeofNENumber |
	TypeFactsTypeofNEBigInt | TypeFactsTypeofNESymbol | TypeFactsTypeofNEObject |
	TypeFactsTypeofNEFunction | TypeFactsTypeofNEHostObject | TypeFactsNEUndefined |
	TypeFactsNENull | TypeFactsNEUndefinedOrNull;
inline constexpr TypeFacts TypeFactsBaseBooleanFacts =
	TypeFactsBaseBooleanStrictFacts | TypeFactsEQNull | TypeFactsEQUndefinedOrNull | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsBooleanStrictFacts =
	TypeFactsBaseBooleanStrictFacts | TypeFactsTruthy | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsBooleanFacts = TypeFactsBaseBooleanFacts | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsFalseStrictFacts =
	TypeFactsBaseBooleanStrictFacts | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsFalseFacts = TypeFactsBaseBooleanFacts;
inline constexpr TypeFacts TypeFactsTrueStrictFacts =
	TypeFactsBaseBooleanStrictFacts | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsTrueFacts = TypeFactsBaseBooleanFacts | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsSymbolStrictFacts =
	TypeFactsTypeofEQSymbol | TypeFactsTypeofNEString | TypeFactsTypeofNENumber |
	TypeFactsTypeofNEBigInt | TypeFactsTypeofNEBoolean | TypeFactsTypeofNEObject |
	TypeFactsTypeofNEFunction | TypeFactsTypeofNEHostObject | TypeFactsNEUndefined |
	TypeFactsNENull | TypeFactsNEUndefinedOrNull | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsSymbolFacts =
	TypeFactsSymbolStrictFacts | TypeFactsEQUndefined | TypeFactsEQNull |
	TypeFactsEQUndefinedOrNull | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsObjectStrictFacts =
	TypeFactsTypeofEQObject | TypeFactsTypeofEQHostObject | TypeFactsTypeofNEString |
	TypeFactsTypeofNENumber | TypeFactsTypeofNEBigInt | TypeFactsTypeofNEBoolean |
	TypeFactsTypeofNESymbol | TypeFactsTypeofNEFunction | TypeFactsNEUndefined |
	TypeFactsNENull | TypeFactsNEUndefinedOrNull | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsObjectFacts =
	TypeFactsObjectStrictFacts | TypeFactsEQUndefined | TypeFactsEQNull |
	TypeFactsEQUndefinedOrNull | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsFunctionStrictFacts =
	TypeFactsTypeofEQFunction | TypeFactsTypeofEQHostObject | TypeFactsTypeofNEString |
	TypeFactsTypeofNENumber | TypeFactsTypeofNEBigInt | TypeFactsTypeofNEBoolean |
	TypeFactsTypeofNESymbol | TypeFactsTypeofNEFunction | TypeFactsNEUndefined |
	TypeFactsNENull | TypeFactsNEUndefinedOrNull | TypeFactsTruthy;
inline constexpr TypeFacts TypeFactsFunctionFacts =
	TypeFactsFunctionStrictFacts | TypeFactsEQUndefined | TypeFactsEQNull |
	TypeFactsEQUndefinedOrNull | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsVoidFacts =
	TypeFactsTypeofNEString | TypeFactsTypeofNENumber | TypeFactsTypeofNEBigInt |
	TypeFactsTypeofNEBoolean | TypeFactsTypeofNESymbol | TypeFactsTypeofNEObject |
	TypeFactsTypeofNEFunction | TypeFactsTypeofNEHostObject | TypeFactsNENull | TypeFactsFalsy;
inline constexpr TypeFacts TypeFactsUndefinedFacts = TypeFactsVoidFacts | TypeFactsIsUndefined;
inline constexpr TypeFacts TypeFactsNullFacts =
	TypeFactsTypeofEQObject | TypeFactsTypeofNEString | TypeFactsTypeofNENumber |
	TypeFactsTypeofNEBigInt | TypeFactsTypeofNEBoolean | TypeFactsTypeofNESymbol |
	TypeFactsTypeofNEFunction | TypeFactsEQNull | TypeFactsEQUndefinedOrNull |
	TypeFactsNEUndefined | TypeFactsFalsy | TypeFactsIsNull;
inline constexpr TypeFacts TypeFactsEmptyObjectStrictFacts =
	TypeFactsAll & ~(TypeFactsEQUndefined | TypeFactsEQNull | TypeFactsEQUndefinedOrNull |
					 TypeFactsIsUndefinedOrNull);
inline constexpr TypeFacts TypeFactsEmptyObjectFacts = TypeFactsAll & ~TypeFactsIsUndefinedOrNull;
inline constexpr TypeFacts TypeFactsUnknownFacts = TypeFactsAll & ~TypeFactsIsUndefinedOrNull;
inline constexpr TypeFacts TypeFactsAllTypeofNE =
	TypeFactsTypeofNEString | TypeFactsTypeofNENumber | TypeFactsTypeofNEBigInt |
	TypeFactsTypeofNEBoolean | TypeFactsTypeofNESymbol | TypeFactsTypeofNEObject |
	TypeFactsTypeofNEFunction | TypeFactsNEUndefined;
inline constexpr TypeFacts TypeFactsOrFactsMask = TypeFactsTypeofEQFunction | TypeFactsTypeofNEObject;
inline constexpr TypeFacts TypeFactsAndFactsMask = TypeFactsAll & ~TypeFactsOrFactsMask;

// thisAssignmentDeclarationKind

enum thisAssignmentDeclarationKind : int32_t {
	thisAssignmentDeclarationNone,
	thisAssignmentDeclarationTyped,
	thisAssignmentDeclarationConstructor,
	thisAssignmentDeclarationMethod,
};

// AssignmentKind — checker.go:utilities.go:79
enum class AssignmentKind : int32_t {
	None = 0,
	Definite = 1,
	Compound = 2,
};
inline constexpr AssignmentKind AssignmentKindNone = AssignmentKind::None;
inline constexpr AssignmentKind AssignmentKindDefinite = AssignmentKind::Definite;
inline constexpr AssignmentKind AssignmentKindCompound = AssignmentKind::Compound;


// symbolTableID

using symbolTableID = uint64_t;

// ExpandingFlags

using ExpandingFlags = uint32_t;
inline constexpr ExpandingFlags ExpandingFlagsNone = 0;
inline constexpr ExpandingFlags ExpandingFlagsSource = 1 << 0;
inline constexpr ExpandingFlags ExpandingFlagsTarget = 1 << 1;
inline constexpr ExpandingFlags ExpandingFlagsBoth = ExpandingFlagsSource | ExpandingFlagsTarget;

// Relation kinds

enum class RelationKind : int32_t {
	Subtype,
	StrictSubtype,
	Assignable,
	Comparable,
	Identity,
	Enum,
};

enum class RelationComparisonResult : int32_t {
	None = 0,
	Failed = 1,
	Reported = 2,
	ReportsUnmeasurable = 4,
	ReportsUnreliable = 8,
	ReportsMask = Reported | ReportsUnmeasurable | ReportsUnreliable,
	Succeeded = 16, // Marker to distinguish success from failure (mask with this before checking)
};

inline constexpr RelationComparisonResult operator|(RelationComparisonResult a,
												  RelationComparisonResult b) {
	return static_cast<RelationComparisonResult>(static_cast<int32_t>(a) | static_cast<int32_t>(b));
}
inline constexpr RelationComparisonResult operator&(RelationComparisonResult a,
													RelationComparisonResult b) {
	return static_cast<RelationComparisonResult>(static_cast<int32_t>(a) & static_cast<int32_t>(b));
}
inline constexpr RelationComparisonResult& operator|=(RelationComparisonResult& a,
													 RelationComparisonResult b) {
	return a = a | b;
}
inline constexpr RelationComparisonResult operator&=(RelationComparisonResult& a,
													 RelationComparisonResult b) {
	return a = a & b;
}

// Relation

struct Relation;

// Evaluator — full port in internal/evaluator/evaluator.h.

// SharedFlow / FlowState (flow.go)

struct FlowType {
	Type* t{};
	bool incomplete{};

	bool isNil() const { return t == nullptr; }
};

struct SharedFlow {
	FlowNode* flow{};
	FlowType flowType{};
};

struct FlowState {
	Node* reference{};
	Type* declaredType{};
	Type* initialType{};
	Node* flowContainer{};
	CacheKey refKey;
	int depth{};
	int sharedFlowStart{};
	std::vector<FlowReduceLabelData*> reduceLabels;
	FlowState* next{};
};

// VarianceStackEntry

struct VarianceStackEntry {
	Symbol* symbol{};
	std::vector<Type*> typeParameters;
};

// === slice: tracer === — tracer.go:13. Records types and trace events during
// type checking. A null Checker::tracer is a valid no-op (Go nil *Tracer).
struct Tracer {
	tsc::tracing::Tracing* tracing{};
	tsc::tracing::Tracer* recorder{};
	int checkerIndex{};

	void RecordType(Type* typ);
	std::function<void()> Push(tsc::tracing::Phase phase, const std::string& name,
							   tsc::tracing::TraceArgs args, bool separateBeginAndEnd);
	void Instant(tsc::tracing::Phase phase, const std::string& name,
				 const tsc::tracing::TraceArgs& args);
	tsc::tracing::TraceArgs copyWithCheckerIndex(const tsc::tracing::TraceArgs& args);
	std::function<void()> temporarilyAddCheckerIndex(tsc::tracing::TraceArgs& args);
};

// EmitResolver — ported with emitresolver.go.

struct EmitResolver;

// Relater (relater.go)

struct Relater;

// module/resolver — port of module.ResolvedModule/PackageId as seen by the
// checker. The program returns these from GetResolvedModule; with no file
// system the fields after `resolved` are unset.
struct PackageId {
	std::string name{};
};

struct ResolvedModule {
	bool resolved{};
	std::string resolvedFileName{};
	bool resolvedUsingTsExtension{};
	bool isExternalLibraryImport{};
	std::string extension{};
	std::string alternateResult{};
	PackageId packageId{};
	bool resolvedUsingExtraExtensions{};
};

// DiagnosticDetails (utilities.go) — resolved diagnostic message + args,
// shared between checker and incremental diagnostic repopulation.
struct DiagnosticDetails {
	const DiagnosticMessage* message{};
	std::vector<std::string> args;
};

// Project-reference plumbing (program/projection redirects). Defaults return
// "no redirect" so a resolver-less program needs no override.
class RedirectInfo {
public:
	virtual ~RedirectInfo() = default;
	virtual std::string CommonSourceDirectory() = 0;
	virtual const CompilerOptions* CompilerOptions() = 0;
};

struct ProjectReferenceRedirect {
	std::string outputDts{};
};

// tsoptions.ProjectReference — only what the checker needs.
class ProjectReference {
public:
	virtual ~ProjectReference() = default;
	virtual const CompilerOptions* CompilerOptions() = 0;
};

// tsoptions.SourceOutputAndProjectReference
struct SourceOutputAndProjectReference {
	SourceFile* source{};
	std::string outputDts{};
	ProjectReference* resolved{};
};

// Program interface — port of checker's Program/Host. Only the members the
// checker actually calls are declared; a SimpleProgram implements them.
class Program {
public:
	virtual ~Program() = default;
	virtual const CompilerOptions* Options() = 0;
	virtual std::vector<SourceFile*> SourceFiles() = 0;
	virtual void BindSourceFiles() = 0;
	virtual bool FileExists(const std::string& fileName) = 0;
	virtual SourceFile* GetSourceFile(const std::string& fileName) = 0;
	virtual SourceFile* GetSourceFileForResolvedModule(const std::string& fileName) = 0;
	virtual ModuleKind GetEmitModuleFormatOfFile(SourceFile* sourceFile) = 0;
	virtual ResolutionMode GetEmitSyntaxForUsageLocation(SourceFile* sourceFile,
															 Node* usageLocation) = 0;
	virtual ModuleKind GetImpliedNodeFormatForEmit(SourceFile* sourceFile) = 0;
	virtual bool SourceFileMayBeEmitted(SourceFile* sourceFile, bool forceDtsEmit) = 0;
	virtual std::string CommonSourceDirectory() = 0;
	// Module resolution (checker.go resolveExternalModule). A program without
	// a module resolver returns nullopt / ResolutionModeNone.
	virtual std::optional<ResolvedModule> GetResolvedModule(
	    SourceFile* file, const std::string& moduleReference,
	    ResolutionMode mode) = 0;
	// program.go GetResolvedModules — flattened slim modules across all
	// files/modes (checker::ResolvedModule values).
	virtual std::vector<ResolvedModule> GetResolvedModules() { return {}; }
	// program.go GetPackagesMap — lazily-cached package-name → bundles-types map.
	virtual const std::unordered_map<std::string, bool>& GetPackagesMap() {
		static const std::unordered_map<std::string, bool> empty{};
		return empty;
	}
	// program.go GetSourceFileMetaData.
	virtual const SourceFileMetaData& GetSourceFileMetaData(
	    const std::string& /*path*/) const {
		static const SourceFileMetaData empty{};
		return empty;
	}
	virtual ResolutionMode GetModeForUsageLocation(SourceFile* file,
	                                               Node* location) = 0;
	virtual ResolutionMode GetDefaultResolutionModeForFile(
	    SourceFile* file) = 0;
	virtual std::string GetCurrentDirectory() = 0;
	virtual bool UseCaseSensitiveFileNames() = 0;
	virtual RedirectInfo* GetRedirectForResolution(SourceFile* sourceFile) {
		return nullptr;
	}
	virtual const ProjectReferenceRedirect* GetProjectReferenceFromSource(
	    const std::string& /*path*/) {
		return nullptr;
	}
	virtual const SourceOutputAndProjectReference* GetProjectReferenceFromOutputDts(
	    const std::string& /*path*/) {
		return nullptr;
	}
	// program.go GetImportHelpersImportSpecifier — the `import "tslib"` specifier
	// the program synthesized for this file, if any.
	virtual Node* GetImportHelpersImportSpecifier(const std::string& /*path*/) {
		return nullptr;
	}
};

// nodeLinkStore / symbolArenaLinkStore (links.go)

template <class V>
struct nodeLinkStore {
	PagedLinkStore<V> store;
	V* Get(Node* node) { return store.Get(static_cast<uint64_t>(getNodeId(node))); }
	bool Has(Node* node) { return store.Has(static_cast<uint64_t>(getNodeId(node))); }
	V* TryGet(Node* node) { return store.TryGet(static_cast<uint64_t>(getNodeId(node))); }
};

template <class V>
struct symbolArenaLinkStore {
	PagedLinkStore<V*> store;
	Arena* arena{};

	V* Get(Symbol* symbol) {
		V** link = store.Get(static_cast<uint64_t>(getSymbolId(symbol)));
		if (*link == nullptr) {
			*link = arena->alloc<V>();
		}
		return *link;
	}
	bool Has(Symbol* symbol) { return TryGet(symbol) != nullptr; }
	V* TryGet(Symbol* symbol) {
		if (V** link = store.TryGet(static_cast<uint64_t>(getSymbolId(symbol))); link != nullptr) {
			return *link;
		}
		return nullptr;
	}
};

// === slice: decltypes === (checker_decltypes.cpp)

// IntersectionState — relater.go:38
using IntersectionState = uint32_t;
inline constexpr IntersectionState IntersectionStateNone = 0;
inline constexpr IntersectionState IntersectionStateSource = 1 << 0;
inline constexpr IntersectionState IntersectionStateTarget = 1 << 1;

// WideningContext — checker.go:537-544. Go distinguishes nil from empty for the
// lazily computed fields, so they are wrapped in std::optional.
struct WideningContext {
	WideningContext* parent{};                                       // Parent context
	std::deque<WideningContext>* pool{};                             // Checker-owned pool for child contexts (C++-only plumbing)
	std::string propertyName;                                        // Name of property in parent
	std::optional<std::vector<Type*>> siblings;                      // Types of siblings
	std::optional<std::vector<Symbol*>> resolvedProperties;          // Properties occurring in sibling object literals
	std::optional<std::unordered_map<std::string, WideningContext*>> childContexts;
	std::optional<std::unordered_map<Type*, Type*>> widenedTypes;
	WideningContext* getChildContext(const std::string& propertyName);
};

// RecursionId — relater.go:89. Defined here (not just forward-declared)
// because call sites build std::vector<RecursionId> which needs a complete
// type. Go's `value any` carries a *Node | *Symbol | *Type pointer; a raw
// uintptr is the faithful equivalent for identity-only use.
struct RecursionId {
	uintptr_t value{};
	bool operator==(const RecursionId&) const = default;
};

// Checker

class Checker {
public:
	uint32_t id{};
	Program* program{};
	const CompilerOptions* compilerOptions{};
	std::vector<SourceFile*> files;
	std::unordered_map<SourceFile*, int> fileIndexMap;
	std::function<int(Symbol*, Symbol*)> compareSymbols;
	std::function<int(const std::vector<Symbol*>&, const std::vector<Symbol*>&)> compareSymbolChains;
	uint32_t TypeCount{};
	uint32_t SymbolCount{};
	uint32_t SignatureCount{};
	uint32_t TotalInstantiationCount{};
	uint32_t instantiationCount{};
	std::vector<Type*> instantiationStack;
	bool removedSubtypesFailed{};
	uint32_t conditionalConstraintDepth{};
	int inlineLevel{};
	int serializationLevel{};
	Node* currentNode{};
	Type* varianceTypeParameter{};
	ScriptTarget languageVersion{};
	ModuleKind moduleKind{};
	ModuleResolutionKind moduleResolutionKind{};
	bool isInferencePartiallyBlocked{};
	bool legacyDecorators{};
	bool emitStandardClassFields{};
	bool strictNullChecks{};
	bool strictFunctionTypes{};
	bool strictBindCallApply{};
	bool strictPropertyInitialization{};
	bool strictBuiltinIteratorReturn{};
	bool noImplicitAny{};
	bool noImplicitThis{};
	bool useUnknownInCatchVariables{};
	bool exactOptionalPropertyTypes{};
	bool canCollectSymbolAliasAccessibilityData{};
	bool wasCanceled{};
	std::vector<VarianceFlags> arrayVariances;
	SymbolTable globals;
	Evaluator evaluate;
	std::unordered_map<std::string, Type*> stringLiteralTypes;
	std::unordered_map<uint64_t, Type*> numberLiteralTypes; // keyed on Number bits
	std::unordered_map<std::string, Type*> bigintLiteralTypes; // keyed on pseudo bigint text
	// enumLiteralTypes keyed by (enumSymbol, value)
	struct EnumLiteralKeyHash {
		size_t operator()(const EnumLiteralKey& k) const noexcept;
	};
	std::unordered_map<EnumLiteralKey, Type*, EnumLiteralKeyHash> enumLiteralTypes;
	std::unordered_map<Symbol*, Type*> enumNaNLiteralTypes;
	CacheMap<Type*> indexedAccessTypes;
	CacheMap<Type*> templateLiteralTypes;
	struct StringMappingKeyHash {
		size_t operator()(const StringMappingKey& k) const noexcept;
	};
	std::unordered_map<StringMappingKey, Type*, StringMappingKeyHash> stringMappingTypes;
	std::unordered_map<Symbol*, Type*> uniqueESSymbolTypes;
	std::unordered_map<Symbol*, thisAssignmentDeclarationKind> thisExpandoKinds;
	std::unordered_map<Symbol*, Node*> thisExpandoLocations;
	CacheMap<std::vector<Type*>> subtypeReductionCache;
	struct CachedTypeKeyHash {
		size_t operator()(const CachedTypeKey& k) const noexcept;
	};
	std::unordered_map<CachedTypeKey, Type*, CachedTypeKeyHash> cachedTypes;
	struct CachedSignatureKeyHash {
		size_t operator()(const CachedSignatureKey& k) const noexcept;
	};
	std::unordered_map<CachedSignatureKey, Signature*, CachedSignatureKeyHash> cachedSignatures;
	std::unordered_map<std::string, Symbol*> undefinedProperties;
	struct NarrowedTypeKeyHash {
		size_t operator()(const NarrowedTypeKey& k) const noexcept;
	};
	std::unordered_map<NarrowedTypeKey, Type*, NarrowedTypeKeyHash> narrowedTypes;
	struct AssignmentReducedKeyHash {
		size_t operator()(const AssignmentReducedKey& k) const noexcept;
	};
	std::unordered_map<AssignmentReducedKey, Type*, AssignmentReducedKeyHash> assignmentReducedTypes;
	struct PairKeyHash {
		size_t operator()(const std::pair<NodeId, TypeId>& k) const noexcept {
			return static_cast<size_t>(k.first) ^ (static_cast<size_t>(k.second) << 21);
		}
		// === slice: contextual ===
		size_t operator()(const DiscriminatedContextualTypeKey& k) const noexcept {
			return static_cast<size_t>(k.nodeId) ^ (static_cast<size_t>(k.typeId) << 21);
		}
		size_t operator()(const InstantiationExpressionKey& k) const noexcept {
			return static_cast<size_t>(k.nodeId) ^ (static_cast<size_t>(k.typeId) << 21);
		}
		// === end slice: contextual ===
	};
	std::unordered_map<DiscriminatedContextualTypeKey, Type*, PairKeyHash>
		discriminatedContextualTypes;
	std::unordered_map<InstantiationExpressionKey, Type*, PairKeyHash> instantiationExpressionTypes;
	struct SubstitutionTypeKeyHash {
		size_t operator()(const SubstitutionTypeKey& k) const noexcept;
	};
	std::unordered_map<SubstitutionTypeKey, Type*, SubstitutionTypeKeyHash> substitutionTypes;
	struct ReverseMappedTypeKeyHash {
		size_t operator()(const ReverseMappedTypeKey& k) const noexcept;
	};
	std::unordered_map<ReverseMappedTypeKey, Type*, ReverseMappedTypeKeyHash> reverseMappedCache;
	std::unordered_map<ReverseMappedTypeKey, Type*, ReverseMappedTypeKeyHash>
		reverseHomomorphicMappedCache;
	struct IterationTypesKeyHash {
		size_t operator()(const IterationTypesKey& k) const noexcept;
	};
	std::unordered_map<IterationTypesKey, IterationTypes, IterationTypesKeyHash> iterationTypesCache;
	std::unordered_set<Type*> markerTypes;
	std::unordered_set<Symbol*> resolvingExplicitTypeOfSymbol;
	Symbol* undefinedSymbol{};
	Symbol* argumentsSymbol{};
	Symbol* requireSymbol{};
	Symbol* unknownSymbol{};
	std::unordered_map<std::string, Symbol*> unresolvedSymbols;
	CacheMap<Type*> errorTypes;
	std::unordered_map<Node*, Symbol*> moduleSymbols;
	Symbol* globalThisSymbol{};
	std::unordered_map<symbolTableID, std::vector<Symbol*>> symbolTableAliasCache;
	std::unordered_map<NodeId, SymbolTable> classExpressionNameTables;
	std::function<Symbol*(Node* location, std::string_view name, SymbolFlags meaning,
						 const DiagnosticMessage* nameNotFoundMessage, bool isUse,
						 bool excludeGlobals)>
		resolveName;
	std::function<Symbol*(Node* location, std::string_view name, SymbolFlags meaning,
						  const DiagnosticMessage* nameNotFoundMessage, bool isUse,
						  bool excludeGlobals)>
		resolveNameForSymbolSuggestion;
	CacheMap<Type*> tupleTypes;
	CacheMap<Type*> unionTypes;
	struct UnionOfUnionKeyHash {
		size_t operator()(const UnionOfUnionKey& k) const noexcept;
	};
	std::unordered_map<UnionOfUnionKey, Type*, UnionOfUnionKeyHash> unionOfUnionTypes;
	CacheMap<Type*> intersectionTypes;
	struct PropertiesTypesKeyHash {
		size_t operator()(const PropertiesTypesKey& k) const noexcept;
	};
	std::unordered_map<PropertiesTypesKey, Type*, PropertiesTypesKeyHash> propertiesTypes;
	DiagnosticsCollection diagnostics;
	DiagnosticsCollection suggestionDiagnostics;
	Arena symbolArena;
	Arena signatureArena;
	Arena indexInfoArena;
	Arena linksArena; // arena backing symbolArenaLinkStore + link stores
	Arena typeArena;  // arena backing all Type allocations (types never move)
	std::unordered_map<Symbol*, Symbol*> mergedSymbols;
	NodeFactory factory;
	nodeLinkStore<NodeLinks> nodeLinks;
	nodeLinkStore<SignatureLinks> signatureLinks;
	nodeLinkStore<SymbolNodeLinks> symbolNodeLinks;
	nodeLinkStore<TypeNodeLinks> typeNodeLinks;
	nodeLinkStore<EnumMemberLinks> enumMemberLinks;
	nodeLinkStore<AssertionLinks> assertionLinks;
	nodeLinkStore<ArrayLiteralLinks> arrayLiteralLinks;
	nodeLinkStore<SwitchStatementLinks> switchStatementLinks;
	nodeLinkStore<JsxElementLinks> jsxElementLinks;
	nodeLinkStore<ComputedNameNodeLinks> computedNameLinks;
	LinkStore<Symbol*, SymbolReferenceLinks> symbolReferenceLinks{&linksArena};
	symbolArenaLinkStore<ValueSymbolLinks> valueSymbolLinks{{}, &linksArena};
	LinkStore<Symbol*, MappedSymbolLinks> mappedSymbolLinks{&linksArena};
	LinkStore<Symbol*, DeferredSymbolLinks> deferredSymbolLinks{&linksArena};
	LinkStore<Symbol*, AliasSymbolLinks> aliasSymbolLinks{&linksArena};
	LinkStore<Symbol*, ModuleSymbolLinks> moduleSymbolLinks{&linksArena};
	LinkStore<Symbol*, LateBoundLinks> lateBoundLinks{&linksArena};
	LinkStore<Symbol*, ExportTypeLinks> exportTypeLinks{&linksArena};
	LinkStore<Symbol*, MembersAndExportsLinks> membersAndExportsLinks{&linksArena};
	LinkStore<Symbol*, TypeAliasLinks> typeAliasLinks{&linksArena};
	LinkStore<Symbol*, DeclaredTypeLinks> declaredTypeLinks{&linksArena};
	LinkStore<Symbol*, SpreadLinks> spreadLinks{&linksArena};
	LinkStore<Symbol*, VarianceLinks> varianceLinks{&linksArena};
	LinkStore<Symbol*, ReverseMappedSymbolLinks> reverseMappedSymbolLinks{&linksArena};
	LinkStore<Symbol*, MarkedAssignmentSymbolLinks> markedAssignmentSymbolLinks{&linksArena};
	LinkStore<Symbol*, ContainingSymbolLinks> symbolContainerLinks{&linksArena};
	LinkStore<SourceFile*, SourceFileLinks> sourceFileLinks{&linksArena};
	std::optional<Scanner> regExpScanner;
	std::unordered_map<Type*, Node*> patternForType;
	std::unordered_map<Node*, Type*> contextFreeTypes;
	Type* anyType{};
	Type* nanType{};
	Type* autoType{};
	Type* wildcardType{};
	Type* blockedStringType{};
	Type* errorType{};
	Type* unresolvedType{};
	Type* nonInferrableAnyType{};
	Type* intrinsicMarkerType{};
	Type* unknownType{};
	Type* undefinedType{};
	Type* undefinedWideningType{};
	Type* missingType{};
	Type* undefinedOrMissingType{};
	Type* optionalType{};
	Type* nullType{};
	Type* nullWideningType{};
	Type* stringType{};
	Type* numberType{};
	Type* bigintType{};
	Type* regularFalseType{};
	Type* falseType{};
	Type* regularTrueType{};
	Type* trueType{};
	Type* booleanType{};
	Type* esSymbolType{};
	Type* voidType{};
	Type* neverType{};
	Type* silentNeverType{};
	Type* implicitNeverType{};
	Type* unreachableNeverType{};
	Type* nonPrimitiveType{};
	Type* stringOrNumberType{};
	Type* stringNumberSymbolType{};
	Type* numberOrBigIntType{};
	Type* templateConstraintType{};
	Type* numericStringType{};
	Type* uniqueLiteralType{};
	TypeMapper* uniqueLiteralMapper{};
	RelationComparisonResult reliabilityFlags{};
	TypeMapper* reportUnreliableMapper{};
	TypeMapper* reportUnmeasurableMapper{};
	TypeMapper* restrictiveMapper{};
	TypeMapper* permissiveMapper{};
	Type* emptyObjectType{};
	Type* emptyJsxObjectType{};
	Type* emptyFreshJsxObjectType{};
	Type* emptyTypeLiteralType{};
	Type* unknownEmptyObjectType{};
	Type* unknownUnionType{};
	Type* emptyGenericType{};
	Type* anyFunctionType{};
	Type* noConstraintType{};
	Type* circularConstraintType{};
	Type* resolvingDefaultType{};
	Type* markerSuperType{};
	Type* markerSubType{};
	Type* markerOtherType{};
	Type* markerSuperTypeForCheck{};
	Type* markerSubTypeForCheck{};
	TypePredicate* noTypePredicate{};
	Signature* anySignature{};
	Signature* unknownSignature{};
	Signature* resolvingSignature{};
	Signature* silentNeverSignature{};
	std::unordered_map<Node*, bool> cachedArgumentsReferenced;
	IndexInfo* enumNumberIndexInfo{};
	IndexInfo* anyBaseTypeIndexInfo{};
	std::vector<PatternAmbientModule> patternAmbientModules;
	SymbolTable patternAmbientModuleAugmentations;
	SymbolTable patternAmbientModuleAugmentationTargets;
	std::unordered_map<Symbol*, Type*> moduleImportAttributesTypes;
	Type* globalObjectType{};
	Type* globalFunctionType{};
	Type* globalCallableFunctionType{};
	Type* globalNewableFunctionType{};
	Type* globalArrayType{};
	Type* globalReadonlyArrayType{};
	Type* globalStringType{};
	Type* globalNumberType{};
	Type* globalBooleanType{};
	Type* globalRegExpType{};
	Type* globalThisType{};
	Type* anyArrayType{};
	Type* autoArrayType{};
	Type* anyReadonlyArrayType{};
	Type* deferredGlobalImportMetaExpressionType{};
	std::vector<Node*> contextualBindingPatterns;
	Type* emptyStringType{};
	Type* zeroType{};
	Type* zeroBigIntType{};
	Type* typeofType{};
	std::vector<TypeResolution> typeResolutions;
	int resolutionStart{};
	std::vector<VarianceStackEntry> varianceStack;
	std::vector<Node*> callResolutionStack;
	int* apparentArgumentCount{};
	Node* lastGetCombinedNodeFlagsNode{};
	NodeFlags lastGetCombinedNodeFlagsResult{};
	Node* lastGetCombinedModifierFlagsNode{};
	ModifierFlags lastGetCombinedModifierFlagsResult{};
	InferenceState* freeinferenceState{};
	FlowState* freeFlowState{};
	struct FlowLoopKeyHash {
		size_t operator()(const FlowLoopKey& k) const noexcept {
			CacheKeyHash h;
			return h(k.refKey) ^ reinterpret_cast<uintptr_t>(k.flowNode);
		}
	};
	std::unordered_map<FlowLoopKey, Type*, FlowLoopKeyHash> flowLoopCache;
	std::vector<FlowLoopInfo> flowLoopStack;
	std::vector<SharedFlow> sharedFlows;
	std::vector<Type*> antecedentTypes;
	bool flowAnalysisDisabled{};
	int flowInvocationCount{};
	std::unordered_map<Node*, Type*> flowTypeCache;
	FlowNode* lastFlowNode{};
	bool lastFlowNodeReachable{};
	std::unordered_map<FlowNode*, bool> flowNodeReachable;
	std::unordered_map<FlowNode*, bool> flowNodePostSuper;
	std::vector<Node*> renamedBindingElementsInTypes;
	std::vector<ContextualInfo> contextualInfos;
	std::vector<InferenceContextInfo> inferenceContextInfos;
	std::vector<Type*> awaitedTypeStack;
	std::vector<Type*> reverseMappedSourceStack;
	std::vector<Type*> reverseMappedTargetStack;
	ExpandingFlags reverseExpandingFlags{};
	Relater* freeRelater{};
	Relation* subtypeRelation{};
	Relation* strictSubtypeRelation{};
	Relation* assignableRelation{};
	Relation* comparableRelation{};
	Relation* identityRelation{};
	struct EnumRelationKeyHash {
		size_t operator()(const EnumRelationKey& k) const noexcept {
			return static_cast<size_t>(k.sourceId) ^ (static_cast<size_t>(k.targetId) << 20);
		}
	};
	std::unordered_map<EnumRelationKey, RelationComparisonResult, EnumRelationKeyHash> enumRelation;
	std::function<Type*()> getGlobalESSymbolType;
	std::function<Type*()> getGlobalBigIntType;
	std::function<Type*()> getGlobalImportMetaType;
	std::function<Type*()> getGlobalImportAttributesType;
	std::function<Type*()> getGlobalImportAttributesTypeChecked;
	std::function<Symbol*()> getGlobalNonNullableTypeAliasOrNil;
	std::function<Symbol*()> getGlobalExtractSymbol;
	std::function<Type*()> getGlobalDisposableType;
	std::function<Type*()> getGlobalAsyncDisposableType;
	std::function<Symbol*()> getGlobalAwaitedSymbol;
	std::function<Symbol*()> getGlobalAwaitedSymbolOrNil;
	std::function<Symbol*()> getGlobalNaNSymbolOrNil;
	std::function<Symbol*()> getGlobalRecordSymbol;
	std::function<Type*()> getGlobalTemplateStringsArrayType;
	std::function<Symbol*()> getGlobalESSymbolConstructorSymbolOrNil;
	std::function<Symbol*()> getGlobalESSymbolConstructorTypeSymbolOrNil;
	std::function<Type*()> getGlobalImportCallOptionsType;
	std::function<Type*()> getGlobalImportCallOptionsTypeChecked;
	std::function<Type*()> getGlobalPromiseType;
	std::function<Type*()> getGlobalPromiseTypeChecked;
	std::function<Type*()> getGlobalPromiseLikeType;
	std::function<Symbol*()> getGlobalPromiseConstructorSymbol;
	std::function<Symbol*()> getGlobalPromiseConstructorSymbolOrNil;
	std::function<Symbol*()> getGlobalOmitSymbol;
	std::function<Symbol*()> getGlobalNoInferSymbolOrNil;
	std::function<Type*()> getGlobalIteratorType;
	std::function<Type*()> getGlobalIterableType;
	std::function<Type*()> getGlobalIterableTypeChecked;
	std::function<Type*()> getGlobalIterableIteratorType;
	std::function<Type*()> getGlobalIterableIteratorTypeChecked;
	std::function<Type*()> getGlobalIteratorObjectType;
	std::function<Type*()> getGlobalGeneratorType;
	std::function<Type*()> getGlobalAsyncIteratorType;
	std::function<Type*()> getGlobalAsyncIterableType;
	std::function<Type*()> getGlobalAsyncIterableTypeChecked;
	std::function<Type*()> getGlobalAsyncIterableIteratorType;
	std::function<Type*()> getGlobalAsyncIterableIteratorTypeChecked;
	std::function<Type*()> getGlobalAsyncIteratorObjectType;
	std::function<Type*()> getGlobalAsyncGeneratorType;
	std::function<Type*()> getGlobalIteratorYieldResultType;
	std::function<Type*()> getGlobalIteratorReturnResultType;
	std::function<Type*()> getGlobalTypedPropertyDescriptorType;
	std::function<Type*()> getGlobalClassDecoratorContextType;
	std::function<Type*()> getGlobalClassMethodDecoratorContextType;
	std::function<Type*()> getGlobalClassGetterDecoratorContextType;
	std::function<Type*()> getGlobalClassSetterDecoratorContextType;
	std::function<Type*()> getGlobalClassAccessorDecoratorContextType;
	std::function<Type*()> getGlobalClassAccessorDecoratorTargetType;
	std::function<Type*()> getGlobalClassAccessorDecoratorResultType;
	std::function<Type*()> getGlobalClassFieldDecoratorContextType;
	IterationTypesResolver* syncIterationTypesResolver{};
	IterationTypesResolver* asyncIterationTypesResolver{};
	std::function<bool(Type*)> isPrimitiveOrObjectOrEmptyType;
	std::function<bool(Type*)> containsMissingType;
	std::function<bool(Type*)> couldContainTypeVariables;
	std::function<bool(Type*)> isStringIndexSignatureOnlyType;
	std::function<bool(Node*)> markNodeAssignments;
	TypeComparer compareTypesAssignable{};
	EmitResolver* emitResolver{};
	std::once_flag emitResolverOnce;
	std::string _jsxNamespace;
	Node* _jsxFactoryEntity{};
	std::unordered_set<Node*> skipDirectInferenceNodes;
	bool withinUnreachableCode{};
	OrderedSet<Node*> reportedUnreachableNodes;
	std::optional<std::unordered_map<std::string, bool>> packagesMap;
	std::vector<TypeMapper*> activeMappers;
	std::vector<std::unordered_map<CacheKey, Type*, CacheKeyHash>*> activeTypeMappersCaches;
	std::once_flag ambientModulesOnce;
	std::vector<Symbol*> ambientModules;

	// ---- methods (ported from checker.go + friends) ----

	Checker() = default;
	void init(Program* program); // NewChecker body minus binder binding

	// Type creation (checker.go ~25469+)
	Type* newType(TypeFlags flags, ObjectFlags objectFlags, TypeBase* data);
	Type* newIntrinsicType(TypeFlags flags, const std::string& intrinsicName);
	Type* newIntrinsicTypeEx(TypeFlags flags, const std::string& intrinsicName, ObjectFlags objectFlags);
	Type* createWideningType(Type* nonWideningType);
	Type* createUnknownUnionType();
	Type* newLiteralType(TypeFlags flags, const LiteralValue& value, Type* regularType);
	Type* newUniqueESSymbolType(Symbol* symbol, const std::string& name);
	Type* newObjectType(ObjectFlags objectFlags, Symbol* symbol);
	Type* newAnonymousType(Symbol* symbol, const SymbolTable& members,
						 const std::vector<Signature*>& callSignatures,
						 const std::vector<Signature*>& constructSignatures,
						 const std::vector<IndexInfo*>& indexInfos);
	Type* tryCreateTypeReference(Type* target, const std::vector<Type*>& typeArguments);
	Type* createTypeReference(Type* target, const std::vector<Type*>& typeArguments);
	Type* createTypeReferenceEx(Type* target, const std::vector<Type*>& typeArguments,
							  ObjectFlags objectFlags);
	Type* createDeferredTypeReference(Type* target, Node* node, TypeMapper* mapper, TypeAlias* alias);
	Type* cloneTypeReference(Type* source);
	void setStructuredTypeMembers(Type* t, const SymbolTable& members,
								  const std::vector<Signature*>& callSignatures,
								  const std::vector<Signature*>& constructSignatures,
								  const std::vector<IndexInfo*>& indexInfos);
	Type* newTypeParameter(Symbol* symbol);
	ObjectFlags getPropagatingFlagsOfTypes(const std::vector<Type*>& types, TypeFlags excludeKinds);
	Type* newUnionType(ObjectFlags objectFlags, const std::vector<Type*>& types);
	Type* newIntersectionType(ObjectFlags objectFlags, const std::vector<Type*>& types);
	Type* newIndexedAccessType(Type* objectType, Type* indexType, AccessFlags accessFlags);
	Type* newIndexType(Type* target, IndexFlags indexFlags);
	Type* newTemplateLiteralType(const std::vector<std::string>& texts, const std::vector<Type*>& types);
	Type* newStringMappingType(Symbol* symbol, Type* t);
	Type* newIndexedAccessTypeOrUndefined(Type* objectType, Type* indexType, AccessFlags accessFlags,
										  Node* accessNode, AccessFlags checkFlags);

	// Symbol creation
	Symbol* newSymbol(SymbolFlags flags, const std::string& name);
	Symbol* newSymbolEx(SymbolFlags flags, const std::string& name, CheckFlags checkFlags);
	Symbol* newParameter(const std::string& name, Type* t);
	Symbol* newProperty(const std::string& name, Type* t);
	Signature* newSignature(SignatureFlags flags, Node* declaration,
		const std::vector<Type*>& typeParameters, Symbol* thisParameter,
		const std::vector<Symbol*>& parameters, Type* resolvedReturnType,
		TypePredicate* resolvedTypePredicate, int minArgumentCount);
	IndexInfo* newIndexInfo(Type* keyType, Type* valueType, bool isReadonly,
		Node* declaration, const std::vector<Node*>& components);

	// Literal & specialized type factories
	Type* getStringLiteralType(const std::string& value);
	Type* getNumberLiteralType(Number value);
	Type* getBigIntLiteralType(const PseudoBigInt& value);
	Type* getEnumLiteralType(const LiteralValue& value, Symbol* enumSymbol, Symbol* symbol);
	bool isUnitLikeType(Type* t);
	Type* extractUnitType(Type* t);
	Type* getRegularTypeOfLiteralType(Type* t);
	Type* getFreshTypeOfLiteralType(Type* t);
	Type* getTemplateLiteralType(const std::vector<std::string>& texts,
		const std::vector<Type*>& types);
	Type* getStringMappingType(Symbol* symbol, Type* type);
	Type* getIndexTypeEx(Type* t, IndexFlags indexFlags);
	Type* newConditionalType(ConditionalRoot* root, TypeMapper* mapper, TypeMapper* combinedMapper);
	Type* newSubstitutionType(Type* baseType, Type* constraint);

	// Union / intersection construction (checker.go)
	Type* getUnionOrIntersectionType(const std::vector<Type*>& types, bool isUnion,
		UnionReduction unionReduction);
	Type* getUnionType(const std::vector<Type*>& types);
	Type* getUnionTypeEx(std::vector<Type*> types, UnionReduction unionReduction,
		TypeAlias* alias, Type* origin);
	Type* getUnionTypeWorker(std::vector<Type*> types, UnionReduction unionReduction,
		TypeAlias* alias, Type* origin);
	Type* getUnionTypeFromSortedList(std::vector<Type*> types, ObjectFlags objectFlags,
		TypeAlias* alias, Type* origin);
	std::pair<std::vector<Type*>, TypeFlags> addTypesToUnion(const std::vector<Type*>& sourceTypes);
	std::vector<Type*> addNamedUnions(std::vector<Type*> namedUnions, const std::vector<Type*>& types);
	std::vector<Type*> removeRedundantLiteralTypes(std::vector<Type*> types, TypeFlags includes,
		bool reduceVoidUndefined);
	std::vector<Type*> removeStringLiteralsMatchedByTemplateLiterals(std::vector<Type*> types);
	bool isTypeMatchedByTemplateLiteralOrStringMapping(Type* t, Type* templateType);
	bool isTypeMatchedByTemplateLiteralType(Type* t, TemplateLiteralType* templateType,
		TypeComparer compareTypesAssignable);
	std::vector<Type*> removeConstrainedTypeVariables(std::vector<Type*> types);
	std::vector<Type*> removeSubtypes(std::vector<Type*> types, bool hasObjectTypes);
	Type* getIntersectionTypeEx(std::vector<Type*> types, IntersectionFlags flags,
							  TypeAlias* alias);
	Type* getIntersectionType(const std::vector<Type*>& types);
	TypeFlags addTypesToIntersection(orderedSet<Type*>& typeSet, TypeFlags includes,
		const std::vector<Type*>& types);
	TypeFlags addTypeToIntersection(orderedSet<Type*>& typeSet, TypeFlags includes, Type* t);
	std::vector<Type*> removeRedundantSupertypes(std::vector<Type*> types, TypeFlags includes);
	std::pair<std::vector<Type*>, bool> extractRedundantTemplateLiterals(std::vector<Type*> types);
	std::pair<std::vector<Type*>, bool> intersectUnionsOfPrimitiveTypes(std::vector<Type*> types);
	bool eachUnionContains(const std::vector<Type*>& unionTypes, Type* t);
	bool unionContainsType(Type* unionType, Type* t, bool matchSymbol);
	std::vector<Type*> getCrossProductIntersections(const std::vector<Type*>& types,
		IntersectionFlags flags);
	void filterTypes(std::vector<Type*>& types, const std::function<bool(Type*)>& predicate);
	Type* filterType(Type* t, const std::function<bool(Type*)>& f);
	Type* removeType(Type* t, Type* targetType);
	Type* mapType(Type* t, const std::function<Type*(Type*)>& f);
	Type* mapTypeEx(Type* t, const std::function<Type*(Type*)>& f, bool noReductions);
	Type* mapTypeWithAlias(Type* t, const std::function<Type*(Type*)>& f, TypeAlias* alias);
	Type* getBaseTypeOfLiteralType(Type* t);
	Type* getBaseTypeOfLiteralTypeForComparison(Type* t);
	Type* getBaseTypeOfEnumLikeType(Type* t);
	Type* getBaseTypeOfLiteralTypeUnion(Type* t);
	Type* getWidenedLiteralType(Type* t);
	Type* getWidenedUniqueESSymbolType(Type* t);
	Type* getWidenedLiteralLikeTypeForContextualType(Type* t, Type* contextualType);
	bool isLiteralOfContextualType(Type* candidateType, Type* contextualType);
	bool isPatternLiteralType(Type* t);
	bool isPatternLiteralPlaceholderType(Type* t);
	bool isGenericStringLikeType(Type* t);
	bool isGenericMappedType(Type* t);
	// === slice: program ===
	ObjectFlags getGenericObjectFlags(Type* t);
	bool isGenericTupleType(Type* t);
	Type* getTypeParameterFromMappedType(Type* t);
	Type* getConstraintTypeFromMappedType(Type* t);
	Type* getNameTypeFromMappedType(Type* t);
	bool isNamedMember(Symbol* symbol, const std::string& id);
	bool isDeclarationContainedBy(Symbol* symbol, Symbol* container);
	bool symbolIsValue(Symbol* symbol);
	bool symbolIsValueEx(Symbol* symbol, bool includeTypeOnlyMembers);
	// === end slice: program ===
	bool IsEmptyAnonymousObjectType(Type* t);
	bool isEmptyResolvedType(StructuredType* t);
	bool isEmptyObjectType(Type* t);
	bool checkCrossProductUnion(const std::vector<Type*>& types);
	int getCrossProductUnionSize(const std::vector<Type*>& types);
	Type* getTypeOfModuleImportAttributes(Symbol* symbol);
	Type* getTypeOfModuleDeclarationImportAttributes(Node* attributes);
	std::string getTemplateStringForType(Type* t);
	std::string applyStringMapping(Symbol* symbol, const std::string& str);
	std::pair<std::vector<std::string>, std::vector<Type*>> applyTemplateStringMapping(
		Symbol* symbol, const std::vector<std::string>& texts,
		const std::vector<Type*>& types);
	Type* getStringMappingTypeForGenericType(Symbol* symbol, Type* t);
	bool isMemberOfStringMapping(Type* source, Type* target);
	std::pair<Type*, Type*> applyTargetStringMappingToSource(Type* source, Type* target);
	std::vector<Type*> inferTypesFromTemplateLiteralType(Type* source,
		TemplateLiteralType* target, TypeComparer compareTypes);
	std::vector<Type*> inferFromLiteralPartsToTemplateLiteral(
		const std::vector<std::string>& sourceTexts,
		const std::vector<Type*>& sourceTypes, TemplateLiteralType* target);
	bool isValidTypeForTemplateLiteralPlaceholder(Type* source, Type* target,
		TypeComparer compareTypes);
	Type* getStringLikeTypeForType(Type* t);
	bool isGenericIndexType(Type* t);
	bool isTypeRelatedTo(Type* source, Type* target, Relation* relation);
	bool isTypeAssignableTo(Type* source, Type* target);
	bool isTypeDerivedFrom(Type* source, Type* target);
	Type* getTargetType(Type* t);
	Type* getBaseConstraintOrType(Type* t);
	StructuredType* resolveStructuredTypeMembers(Type* t);
	std::vector<Symbol*> getPropertiesOfType(Type* t);
	Type* getTypeOfPropertyOfType(Type* t, const std::string& name);
	Type* getTypeOfSymbol(Symbol* symbol);
	SymbolTable getMembersOfSymbol(Symbol* symbol);
	Type* getDeclaredTypeOfSymbol(Symbol* symbol);
	Type* tryGetDeclaredTypeOfSymbol(Symbol* symbol);
	Type* getDeclaredTypeOfClassOrInterface(Symbol* symbol);
	bool isThislessInterface(Symbol* symbol);
	Node* getClassOrInterfaceLikeDeclaration(Symbol* symbol);
	bool canGetTypeParametersOfClassOrInterface(Symbol* symbol);
	std::vector<Type*> getOuterTypeParameters(Node* node, bool includeThisTypes);
	std::vector<Type*> getOuterTypeParametersOfClassOrInterface(Symbol* symbol);
	std::vector<Type*> getInferTypeParameters(Node* node);
	std::vector<Type*> getLocalTypeParametersOfClassOrInterfaceOrTypeAlias(
		Symbol* symbol);
	std::vector<Type*> appendLocalTypeParametersOfClassOrInterfaceOrTypeAlias(
		std::vector<Type*> types, Symbol* symbol);
	std::vector<Type*> appendTypeParameters(
		std::vector<Type*> typeParameters, const std::vector<Node*>& declarations);
	Type* getDeclaredTypeOfTypeParameter(Symbol* symbol);
	Type* getDeclaredTypeOfTypeAlias(Symbol* symbol);
	Type* getDeclaredTypeOfEnum(Symbol* symbol);
	Type* getDeclaredTypeOfEnumMember(Symbol* symbol);
	Type* getDeclaredTypeOfAlias(Symbol* symbol);
	EvalResult getEnumMemberValue(Node* node);
	Type* createComputedEnumType(Symbol* symbol);
	void computeEnumMemberValues(Node* node);
	EvalResult computeEnumMemberValue(Node* member, Number* autoValue, Node* previous);
	EvalResult computeConstantEnumMemberValue(Node* member);
	EvalResult evaluateEntity(Node* expr, Node* location);
	EvalResult evaluateEnumMember(Node* expr, Symbol* symbol, Node* location);
	Symbol* getAliasSymbolForTypeNode(Node* node);
	std::vector<Type*> getTypeArgumentsForAliasSymbol(Symbol* symbol);
	NodeFlags getDeclarationNodeFlagsFromSymbol(Symbol* s);
	NodeFlags getCombinedNodeFlagsCached(Node* node);
	bool isVarConstLike(Node* node);
	bool isConstantVariable(Symbol* symbol);
	bool isParameterOrMutableLocalVariable(Symbol* symbol);
	bool isMutableLocalVariableDeclaration(Node* declaration);
	bool isInAmbientOrTypeNode(Node* node);
	bool isBlockScopedNameDeclaredBeforeUse(Node* declaration, Node* usage);
	bool isUsedInFunctionOrInstanceProperty(
		Node* usage, Node* declaration, Node* declContainer);
	bool isPropertyInitializedInStaticBlocks(Node* propName, Type* propType,
		const std::vector<Node*>& staticBlocks, int32_t startPos, int32_t endPos);
	bool isPropertyInitializedInConstructor(
		Node* propName, Type* propType, Node* constructor);
	// Pending ports — declared now, defined with their file's slice.
	Type* getWidenedType(Type* t);
	std::vector<Signature*> getSignaturesOfType(Type* t, SignatureKind kind);
	bool isContextSensitive(Node* node);
	Type* getTypeFromTypeNode(Node* node);
	Symbol* resolveEntityName(Node* name, SymbolFlags meaning, bool ignoreErrors,
							bool dontResolveAlias, Node* location);
	// NewChecker bootstrap & global resolution (checker.go)
	void initializeClosures();
	void initializeIterationResolvers();
	void initializeChecker();
	void mergeGlobalSymbol(Symbol* symbol);
	void mergePatternAmbientModules();
	void mergeModuleAugmentation(Node* moduleName);
	void addUndefinedToGlobalsOrErrorOnRedeclaration();
	std::function<Type*()> getGlobalTypeResolver(const std::string& name,
	                                           int arity, bool reportErrors);
	std::function<Symbol*()> getGlobalTypeAliasResolver(const std::string& name,
	                                                  int arity,
	                                                  bool reportErrors);
	std::function<Symbol*()> getGlobalValueSymbolResolver(const std::string& name,
	                                                    bool reportErrors);
	std::function<Symbol*()> getGlobalTypeSymbolResolver(const std::string& name,
	                                                   bool reportErrors);
	std::function<std::vector<Type*>()> getGlobalTypesResolver(
		const std::vector<std::string>& names, int arity, bool reportErrors);
	Symbol* getGlobalTypeAliasSymbol(const std::string& name, int arity,
	                                 bool reportErrors);
	std::vector<Type*> getTypeAliasTypeParameters(Symbol* symbol);
	Type* getGlobalType(const std::string& name, int arity, bool reportErrors);
	Type* getGlobalStrictFunctionType(const std::string& name);
	Type* createTypeFromGenericGlobalType(
		Type* genericGlobalType, const std::vector<Type*>& typeArguments);
	Type* createArrayType(Type* elementType);
	Type* createArrayTypeEx(Type* elementType, bool readonly);
	// Name resolution wiring (checker.go createNameResolver +
	// nameresolver hooks)
	Symbol* getSymbol(SymbolTable& symbols, const std::string& name,
	                SymbolFlags meaning);
	// aligned to Go signature getAwaitedTypeEx(t, errorNode, diagnosticMessage, args ...any)
	Type* getAwaitedTypeEx(Type* type, Node* errorNode,
	                       const DiagnosticMessage* diagnostic,
	                       const std::vector<std::string>& args = {});
	Symbol* resolveExternalModuleNameWorker(Node* location,
	                                        Node* moduleReferenceExpression,
	                                        const DiagnosticMessage* moduleNotFoundError,
	                                        bool ignoreErrors, bool isForAugmentation,
	                                        Type* importAttributesType);
	Symbol* getPropertyOfType(Type* type, const std::string& name);
	bool allTypesAssignableToKindEx(Type* source, TypeFlags kind, bool strict);
	Symbol* getImmediateAliasedSymbol(Symbol* symbol);
	Symbol* getSuggestionForSymbolNameLookup(SymbolTable& symbols,
	                                       const std::string& name,
	                                       SymbolFlags meaning);
	Symbol* getSpellingSuggestionForName(const std::string& name,
	                                     const std::vector<Symbol*>& symbols,
	                                     SymbolFlags meaning);
	void symbolReferenced(Symbol* symbol, SymbolFlags meaning);
	Tristate getRequiresScopeChangeCache(Node* node);
	void setRequiresScopeChangeCache(Node* node, Tristate value);
	bool checkAndReportErrorForInvalidInitializer(Node* errorLocation,
	                                            const std::string& name,
	                                            Node* propertyWithInvalidInitializer,
	                                            Symbol* result);
	bool checkAndReportErrorForMissingPrefix(Node* errorLocation,
	                                         const std::string& name);
	void onFailedToResolveSymbol(Node* errorLocation, const std::string& name,
	                             SymbolFlags meaning,
	                             const DiagnosticMessage* nameNotFoundMessage);
	void onSuccessfullyResolvedSymbol(Node* errorLocation, Symbol* result,
	                                  SymbolFlags meaning, Node* lastLocation,
	                                  Node* associatedDeclarationForContainingInitializerOrBindingName,
	                                  bool withinDeferredContext);
	void checkResolvedBlockScopedVariable(Symbol* result, Node* errorLocation);
	bool checkAndReportErrorForExtendingInterface(Node* errorLocation);
	Node* getEntityNameForExtendingInterface(Node* node);
	bool checkAndReportErrorForUsingTypeAsNamespace(Node* errorLocation,
	                                                const std::string& name,
	                                                SymbolFlags meaning);
	bool checkAndReportErrorForExportingPrimitiveType(Node* errorLocation,
	                                                  const std::string& name);
	bool checkAndReportErrorForUsingNamespaceAsTypeOrValue(
		Node* errorLocation, const std::string& name, SymbolFlags meaning);
	bool checkAndReportErrorForUsingTypeAsValue(Node* errorLocation,
	                                            const std::string& name,
	                                            SymbolFlags meaning);
	bool checkAndReportErrorForUsingValueAsType(Node* errorLocation,
	                                            const std::string& name,
	                                            SymbolFlags meaning);
	std::string getSuggestedLibForNonExistentName(const std::string& name);
	std::string getSuggestedLibForNonExistentProperty(
		const std::string& missingProperty, Type* containingType);
	Symbol* getSuggestedSymbolForNonexistentSymbol(Node* location,
	                                             const std::string& outerName,
	                                             SymbolFlags meaning);
	bool isUncheckedJSSuggestion(Node* node, Symbol* suggestion,
	                             bool excludeClasses);
	Node* getTypeOnlyAliasDeclaration(Symbol* symbol);
	Node* getTypeOnlyAliasDeclarationEx(Symbol* symbol, SymbolFlags meaning);
	Diagnostic* addTypeOnlyDeclarationRelatedInfo(Diagnostic* diagnostic,
	                                            Node* typeOnlyDeclaration,
	                                            const std::string& name);
	SymbolFlags getSymbolFlags(Symbol* symbol);
	SymbolFlags getSymbolFlagsEx(Symbol* symbol, bool excludeTypeOnlyMeanings,
	                            bool excludeLocalMeanings);
	Node* getThisContainer(Node* node, bool includeArrowFunctions,
	                       bool includeClassComputedPropertyName);
	void sortSymbols(std::vector<Symbol*>& symbols);
	int compareNodes(Node* n1, Node* n2);
	int compareSymbolsWorker(Symbol* s1, Symbol* s2);
	int compareSymbolChainsWorker(const std::vector<Symbol*>& a,
	                              const std::vector<Symbol*>& b);
	bool maybeMappedType(Node* node, Symbol* symbol);
	Type* reportUnreliableWorker(Type* t);
	Type* reportUnmeasurableWorker(Type* t);
	Type* restrictiveMapperWorker(Type* t);
	Type* permissiveMapperWorker(Type* t);
	Type* getUniqueLiteralTypeForTypeParameter(Type* t);
	Type* getRestrictiveTypeParameter(Type* t);
	Type* getApparentType(Type* t);
	bool couldContainTypeVariablesWorker(Type* t);
	bool isStringIndexSignatureOnlyTypeWorker(Type* t);
	bool markNodeAssignmentsWorker(Node* node);
	Ternary compareTypesAssignableWorker(Type* source, Type* target,
	                                     bool reportErrors);
	Symbol* getGlobalSymbol(const std::string& name, SymbolFlags meaning,
							const DiagnosticMessage* diagnostic);
	Type* checkExpression(Node* node);
	bool checkTypeAssignableTo(Type* source, Type* target, Node* errorNode,
							   const DiagnosticMessage* headMessage);
	Type* getFlowTypeOfReferenceEx(Node* reference, Type* declaredType,
		Type* initialType, Node* flowContainer, FlowNode* flowNode);
	Type* getOptionalType(Type* t, bool isProperty);
	bool containsUndefinedType(Type* t);
	Type* getTypeWithThisArgument(
		Type* t, Type* thisArgument, bool needApparentType);
	std::vector<Type*> getBaseTypes(Type* t);
	bool isDeferredTypeReferenceNode(Node* node, bool hasDefaultTypeArguments);
	Type* getArrayOrTupleTargetType(Node* node);
	Type* getBuiltinIteratorReturnType();
	Type* checkExpressionWithTypeArguments(Node* node);
	Symbol* getParentOfSymbol(Symbol* symbol);
	Symbol* getLateBoundSymbol(Symbol* symbol);
	Symbol* getSymbolOfDeclaration(Node* node);
	Symbol* getSymbolOfNode(Node* node);
	Symbol* getMergedSymbol(Symbol* symbol);
	void recordMergedSymbol(Symbol* target, Symbol* source);
	Symbol* mergeSymbol(Symbol* target, Symbol* source, bool unidirectional);
	void mergeSymbolTable(SymbolTable& target, const SymbolTable& source, bool unidirectional,
						  Symbol* mergedParent);
	Symbol* cloneSymbol(Symbol* symbol);
	void reportMergeSymbolError(Symbol* target, Symbol* source);
	void addDuplicateDeclarationErrorsForSymbols(Symbol* target, const DiagnosticMessage* message,
											   const std::string& symbolName, Symbol* source);
	void addDuplicateDeclarationError(Node* node, const DiagnosticMessage* message,
									  const std::string& symbolName,
									  const std::vector<Node*>& relatedNodes);
	Diagnostic* lookupOrIssueError(Node* location, const DiagnosticMessage* message,
								   const std::vector<std::string>& args = {});
	Diagnostic* addSuggestionDiagnostic(Diagnostic* diagnostic);
	Diagnostic* errorAndMaybeSuggestAwait(Node* location, bool maybeMissingAwait,
										  const DiagnosticMessage* message,
										  std::vector<std::string> args = {});
	void addErrorOrSuggestion(bool isError, Diagnostic* diagnostic);
	Symbol* resolveSymbol(Symbol* symbol);
	Symbol* resolveSymbolEx(Symbol* symbol, bool dontResolveAlias);
	Symbol* getSymbolIfSameReference(Symbol* s1, Symbol* s2);
	Symbol* getExportSymbolOfValueSymbolIfExported(Symbol* symbol);
	std::string symbolToString(Symbol* symbol);
	std::string symbolToStringEx(Symbol* symbol, Node* enclosingDeclaration,
								 SymbolFlags meaning, SymbolFormatFlags flags);
	bool hasLateBindableName(Node* node);
	bool isLateBindableName(Node* node);
	bool hasLateBindableIndexSignature(Node* node);
	bool isLateBindableIndexSignature(Node* node);
	bool isTypeUsableAsIndexSignatureDeclaration(Type* t);
	SymbolTable getExportsOfSymbol(Symbol* symbol);
	SymbolTable getResolvedMembersOrExportsOfSymbol(Symbol* symbol,
												  MembersOrExportsResolutionKind resolutionKind);
	Symbol* lateBindMember(Symbol* parent, SymbolTable& earlySymbols, SymbolTable& lateSymbols,
						   Node* decl);
	void lateBindIndexSignature(Symbol* parent, SymbolTable& earlySymbols, SymbolTable& lateSymbols,
								Node* decl);
	void addDeclarationToLateBoundSymbol(Symbol* symbol, Node* member, SymbolFlags symbolFlags);
	SymbolTable getExportsOfModule(Symbol* moduleSymbol);
	std::pair<SymbolTable, std::unordered_map<std::string, Node*>> getExportsOfModuleWorker(
		Symbol* moduleSymbol);
	SymbolTable combineSymbolTables(const SymbolTable& first, const SymbolTable& second);
	Symbol* resolveIndirectionAlias(Symbol* source, Symbol* target);
	Symbol* tryResolveAlias(Symbol* symbol);
	Node* getDeclarationOfAliasSymbol(Symbol* symbol);
	Symbol* getTargetOfAliasDeclaration(Node* node);
	bool pushTypeResolution(TypeSystemEntity target, TypeSystemPropertyName propertyName);
	bool popTypeResolution();
	int findResolutionCycleStartIndex(TypeSystemEntity target, TypeSystemPropertyName propertyName);
	bool typeResolutionHasProperty(TypeResolution* r);
	Type* reportCircularityError(Symbol* symbol);
	Type* getTypeOfSymbolWithDeferredType(Symbol* symbol);
	Type* getWriteTypeOfSymbolWithDeferredType(Symbol* symbol);
	Type* getWriteTypeOfSymbol(Symbol* symbol);
	Type* checkComputedPropertyName(Node* node);
	Type* checkExpressionCached(Node* node);
	Symbol* resolveExternalModuleName(Node* location, Node* moduleReference,
									  bool ignoreErrors, Type* importAttributesType);
	const DiagnosticMessage* getCannotResolveModuleNameErrorForSpecificModule(
	    Node* moduleName);
	Symbol* resolveExternalModule(Node* location,
	                              const std::string& moduleReference,
	                              const DiagnosticMessage* moduleNotFoundError,
	                              Node* errorNode, bool isForAugmentation,
	                              Type* importAttributesType);
	SourceFile* getExternalModuleFileFromDeclaration(Node* declaration);
	Symbol* tryResolvePatternAmbientModule(Symbol* resolvedSymbol,
	                                       const std::string& moduleReference,
	                                       Type* importAttributesType);
	Symbol* tryFindAmbientModule(const std::string& moduleReference,
	                             bool withAugmentations);
	bool isCommonJSRequire(Node* node);
	void errorOnImplicitAnyModule(bool isError, Node* errorNode,
	                              ResolutionMode mode,
	                              const ResolvedModule& resolvedModule,
	                              const std::string& moduleReference);
	Diagnostic* createModuleNotFoundChain(const ResolvedModule& resolvedModule,
	                                      Node* errorNode,
	                                      const std::string& moduleReference,
	                                      ResolutionMode mode,
	                                      const std::string& packageName);
	Diagnostic* createModeMismatchDetails(SourceFile* sourceFile,
	                                      Node* errorNode);
	std::string getSuggestedImportSource(const std::string& moduleReference,
	                                     std::string_view tsExtension,
	                                     ResolutionMode mode);
	std::string getSuggestedImportExtension(const std::string& fileName);
	Symbol* resolveQualifiedName(Node* name, Node* left, Node* right,
	                             SymbolFlags meaning, bool ignoreErrors,
	                             Node* location);
	Symbol* tryGetQualifiedNameAsValue(Node* name);
	Symbol* getSuggestedSymbolForNonexistentModule(Node* name,
	                                             Symbol* targetModule);
	bool markSymbolOfAliasDeclarationIfTypeOnly(Node* aliasDeclaration,
	                                            Node* exportStarDeclaration);
	const DiagnosticMessage* getCannotFindNameDiagnosticForName(Node* node);
	std::string getFullyQualifiedName(Symbol* symbol, Node* containingLocation);
	Type* getTypeFromImportAttributes(Node* node);
	Symbol* resolveExternalModuleSymbol(Symbol* moduleSymbol, bool dontResolveAlias);
	Symbol* resolveAlias(Symbol* symbol);
	Type* getReducedType(Type* t);
	bool isNoInferType(Type* t);
	Type* getNoInferType(Type* t);
	bool isNoInferTargetType(Type* t);
	Type* getSubstitutionType(Type* baseType, Type* constraint);
	Type* getOrCreateSubstitutionType(Type* baseType, Type* constraint);
	bool shouldDeferIndexType(Type* t, IndexFlags indexFlags);
	Type* getIndexTypeForGenericType(Type* t, IndexFlags indexFlags);
	Type* getIndexTypeForMappedType(Type* t, IndexFlags indexFlags);
	std::vector<IndexInfo*> getIndexInfosOfType(Type* t);
	bool isKeyTypeIncluded(Type* keyType, TypeFlags include);
	Type* getLiteralTypeFromProperties(Type* t, TypeFlags include, bool includeOrigin);
	Type* getLiteralTypeFromProperty(Symbol* prop, TypeFlags include, bool includeNonPublic);
	Type* getLiteralTypeFromPropertyName(Node* name);
	Type* getTypeAliasInstantiation(Symbol* typeAlias, const std::vector<Type*>& typeArguments,
		TypeAlias* alias);
	std::vector<Symbol*> getNamedMembers(const SymbolTable& members, Symbol* typeSymbol);
	std::vector<Type*> instantiateTypes(const std::vector<Type*>& types, TypeMapper* mapper);
	TypeAlias* getAliasForTypeNode(Node* node);
	Type* getBaseConstraintOfType(Type* t);
	bool isTypeStrictSubtypeOf(Type* source, Type* target);
	bool isTypeSubtypeOf(Type* source, Type* target);
	bool isTypeIdenticalTo(Type* source, Type* target);

	// Diagnostics
	Diagnostic* error(Node* location, const DiagnosticMessage* message,
					std::vector<std::string> args = {});
	Diagnostic* error(Node* location, const DiagnosticMessage* message, std::string arg);
	Diagnostic* addDiagnostic(Diagnostic* diagnostic);
	void errorOrSuggestion(bool isError, Node* location, const DiagnosticMessage* message,
						   std::vector<std::string> args = {});
	Diagnostic* addErrorOrSuggestionDiagnostic(bool isError, Diagnostic* diagnostic);
	Diagnostic* errorOrSuggestionForNodeAndChain(bool isError, Node* location,
												 const DiagnosticMessage* message,
												 Diagnostic* chainNode);
	bool errorFromChain(Node* chainNode, Node* location, const DiagnosticMessage* message,
						const std::vector<std::string>& args);
	Diagnostic* errorSkippedOnNoEmit(Node* location, const DiagnosticMessage* message,
										std::vector<std::string> args = {});
	std::string formatMessage(const DiagnosticMessage* message, const std::vector<std::string>& args);
	Diagnostic* createDiagnosticForNode(Node* node, const DiagnosticMessage* message,
										const std::vector<std::string>& args = {});
	Diagnostic* createDiagnosticForNodeFromMessageChain(Node* node, const DiagnosticMessage* message,
														const std::vector<std::string>& args);
	Diagnostic* createDiagnosticInObject(Node* object, const DiagnosticMessage* message,
										 const std::vector<std::string>& args);
	TextRange getErrorSpanFromNode(Node* node);
	int32_t getNodeCheckFlags(Node* node);

	// checkSourceFile entry points
	void checkSourceFile(SourceFile* sourceFile, bool checkUnused);
	void checkSourceElements(const std::vector<Node*>& nodes);
	bool checkSourceElement(Node* node);
	void checkSourceElementWorker(Node* node);
	bool checkSourceElementUnreachable(Node* node);
	void checkDeferredNodes(SourceFile* context);
	void checkDeferredNode(Node* node);
	void produceDeferredDiagnostics();
	void checkUnusedIdentifiers(const std::vector<Node*>& nodes);
	void checkUnusedRenamedBindingElements();
	void checkExternalModuleExports(Node* moduleNode);
	void registerForUnusedIdentifiersCheck(Node* node);
	void checkGrammarSourceFile(SourceFile* sourceFile);
	// Name resolution
	Symbol* getResolvedSymbol(Node* node);
	SymbolTable getExportsOfModuleSymbol(Symbol* moduleSymbol);
	bool isCanceled() const { return wasCanceled; }
	bool isErrorType(Type* t);

	// Mappers & instantiation (mapper.cpp)
	TypeMapper* combineTypeMappers(TypeMapper* m1, TypeMapper* m2);
	Type* mapTypeWithCompositeMapper(Type* t, TypeMapper* m1, TypeMapper* m2);
	TypeMapper* newBackreferenceMapper(InferenceContext* context, int index);
	TypeMapper* newInferenceTypeMapper(InferenceContext* n, bool fixing);
	Type* instantiateType(Type* t, TypeMapper* mapper);
	void inferFromIntraExpressionSites(InferenceContext* context);
	Type* getInferredType(InferenceContext* context, size_t index);

	// === slice: tracer ===
	Tracer* tracer{};
	std::string TypeToString(Type* t);                 // printer.go — defined in checker_tracer.cpp
	Type* getModifiersTypeFromMappedType(Type* t);     // checker.go:28593 — defined in checker_typeops.cpp

	// === slice: jsdoc ===
	void checkUnmatchedJSDocParameters(Node* node);
	bool containsArgumentsReference(Node* node);
	bool isArrayType(Type* t);
	bool IsArgumentsSymbol(Symbol* symbol);

	// === slice: walk === (checker_walk.cpp: diagnostics tail, check walker,
	// deferred checks, JSDoc comment pass, expression entry + dispatch)
	std::vector<std::function<void()>> deferredDiagnosticCallbacks;
	std::vector<Diagnostic*> GetDiagnostics(SourceFile* sourceFile);
	std::vector<Diagnostic*> GetSuggestionDiagnostics(SourceFile* sourceFile);
	std::vector<Diagnostic*> getDiagnostics(SourceFile* sourceFile, DiagnosticsCollection* collection);
	std::vector<Diagnostic*> GetGlobalDiagnostics();
	void addDeferredDiagnostic(std::function<void()> callback);
	void checkNotCanceled();
	bool isSourceElementUnreachable(Node* node);
	void checkNodeDeferred(Node* node);
	void checkJSDocComments(Node* node);
	void checkJSDocComment(Node* node);
	Symbol* resolveJSDocMemberName(Node* name);
	Type* getTypeOfExpression(Node* node);
	Type* getQuickTypeOfExpression(Node* node);
	Type* getReturnTypeOfSingleNonGenericSignature(Type* funcType, SignatureKind kind);
	Type* getReturnTypeOfSingleNonGenericSignatureOfCallChain(Node* expr);
	Type* checkExpressionWithContextualType(Node* node, Type* contextualType,
											InferenceContext* inferenceContext, CheckMode checkMode);
	Node* getContextNode(Node* node);
	Type* checkExpressionCachedEx(Node* node, CheckMode checkMode);
	Type* getContextFreeTypeOfExpression(Node* node);
	Type* checkExpressionEx(Node* node, CheckMode checkMode);
	void checkConstEnumAccess(Node* node, Type* t);
	Type* instantiateTypeWithSingleGenericCallSignature(Node* node, Type* t, CheckMode checkMode);
	std::vector<Type*> getOuterInferenceTypeParameters();
	std::vector<Type*> getUniqueTypeParameters(InferenceContext* context,
											 const std::vector<Type*>& typeParameters);
	Type* checkExpressionWorker(Node* node, CheckMode checkMode);
	Type* checkPrivateIdentifierExpression(Node* node);
	void skippedGenericFunction(Node* node, CheckMode checkMode);

	// Dep-stub decls (callees owned by other slices — see checker_walk.cpp bottom)
	bool isReachableFlowNode(FlowNode* flow);
	Type* getAwaitedType(Type* t);
	Type* getOptionalExpressionType(Type* exprType, Node* expression);
	Type* propagateOptionalTypeMarker(Type* t, Node* node, bool wasOptional);
	Type* getApparentTypeOfContextualType(Node* node, ContextFlags contextFlags);
	void pushContextualType(Node* node, Type* t, bool isCache);
	void popContextualType();
	void pushInferenceContext(Node* node, InferenceContext* inferenceContext);
	void popInferenceContext();
	InferenceContext* getInferenceContext(Node* node);
	Type* instantiateContextualType(Type* contextualType, Node* node, ContextFlags contextFlags);
	Type* GetNonNullableType(Type* t);
	Signature* getSingleSignature(Type* t, SignatureKind kind, bool allowMembers);
	Signature* getSingleCallOrConstructSignature(Type* t);
	Signature* getSignatureInstantiationWithoutFillingInTypeArguments(Signature* sig,
																const std::vector<Type*>& typeArguments);
	Type* getOrCreateTypeFromSignature(Signature* sig);
	Type* getReturnTypeOfSignature(Signature* sig);
	bool maybeTypeOfKind(Type* type, TypeFlags flags);
	void markPropertyAsReferenced(Symbol* symbol, Node* nodeForCheckWriteOnly, bool isSelfTypeAccess);
	bool isSkipDirectInferenceNode(Node* node);
	void inferTypes(std::vector<InferenceInfo*>& inferences, Type* originalSource, Type* originalTarget,
					InferencePriority priority, bool contravariant);
	void applyToParameterTypes(Signature* source, Signature* target,
							   const std::function<void(Type*, Type*)>& callback);
	void applyToReturnTypes(Signature* source, Signature* target,
							const std::function<void(Type*, Type*)>& callback);
	bool hasOverlappingInferences(std::vector<InferenceInfo*>& a, std::vector<InferenceInfo*>& b);
	void mergeInferences(std::vector<InferenceInfo*>& target, const std::vector<InferenceInfo*>& source);
	// `newInferenceInfo` / `hasInferenceCandidates` are free fns (inference.go:1626,1651).
	Type* checkIdentifier(Node* node, CheckMode checkMode);
	Type* checkThisExpression(Node* node);
	Type* checkSuperExpression(Node* node);
	Type* checkTemplateExpression(Node* node);
	Type* checkRegularExpressionLiteral(Node* node);
	Type* checkArrayLiteral(Node* node, CheckMode checkMode);
	Type* checkObjectLiteral(Node* node, CheckMode checkMode);
	Type* checkPropertyAccessExpression(Node* node, CheckMode checkMode, bool writeOnly);
	Type* checkQualifiedName(Node* node, CheckMode checkMode);
	Type* checkIndexedAccess(Node* node, CheckMode checkMode);
	Type* checkCallExpression(Node* node, CheckMode checkMode);
	Type* checkImportCallExpression(Node* node);
	Type* checkTaggedTemplateExpression(Node* node);
	Type* checkParenthesizedExpression(Node* node, CheckMode checkMode);
	Type* checkClassExpression(Node* node);
	Type* checkFunctionExpressionOrObjectLiteralMethod(Node* node, CheckMode checkMode);
	Type* checkAssertion(Node* node, CheckMode checkMode);
	Type* checkTypeOfExpression(Node* node);
	Type* checkNonNullAssertion(Node* node);
	Type* checkSatisfiesExpression(Node* node);
	Type* checkMetaProperty(Node* node);
	Type* checkDeleteExpression(Node* node);
	Type* checkVoidExpression(Node* node);
	Type* checkAwaitExpression(Node* node);
	Type* checkPrefixUnaryExpression(Node* node);
	Type* checkPostfixUnaryExpression(Node* node);
	Type* checkBinaryExpression(Node* node, CheckMode checkMode);
	Type* checkConditionalExpression(Node* node, CheckMode checkMode);
	Type* checkSpreadExpression(Node* node, CheckMode checkMode);
	Type* checkYieldExpression(Node* node);
	Type* checkSyntheticExpression(Node* node);
	Type* checkJsxExpression(Node* node, CheckMode checkMode);
	Type* checkJsxElement(Node* node, CheckMode checkMode);
	Type* checkJsxSelfClosingElement(Node* node, CheckMode checkMode);
	Type* checkJsxFragment(Node* node);
	Type* checkJsxAttributes(Node* node, CheckMode checkMode);
	Type* checkNonNullExpression(Node* node);
	Type* checkNonNullType(Type* t, Node* node);
	Type* checkNonNullTypeWithReporter(Type* t, Node* node,
	                                   void (*reportError)(Checker*, Node*, TypeFacts));
	Type* checkNonNullNonVoidType(Type* t, Node* node);
	void reportObjectPossiblyNullOrUndefinedError(Node* node, TypeFacts facts);
	bool isSymbolOrSymbolForCall(Node* node);
	Signature* resolveUntypedCall(Node* node);
	void checkFunctionExpressionOrObjectLiteralMethodDeferred(Node* node);
	void checkClassExpressionDeferred(Node* node);
	void checkTypeParameterDeferred(Node* node);
	void checkJsxSelfClosingElementDeferred(Node* node);
	void checkJsxElementDeferred(Node* node);
	void checkAssertionDeferred(Node* node);
	void checkContextualDeprecations(Node* node);
	void checkTypeParameter(Node* node);
	void checkParameter(Node* node);
	void checkPropertyDeclaration(Node* node);
	void checkPropertySignature(Node* node);
	void checkSignatureDeclaration(Node* node);
	void checkMethodDeclaration(Node* node);
	void checkClassStaticBlockDeclaration(Node* node);
	void checkConstructorDeclaration(Node* node);
	void checkAccessorDeclaration(Node* node);
	void checkTypeReferenceNode(Node* node);
	void checkTypePredicate(Node* node);
	void checkTypeQuery(Node* node);
	void checkTypeLiteral(Node* node);
	void checkArrayType(Node* node);
	void checkTupleType(Node* node);
	void checkUnionOrIntersectionType(Node* node);
	void checkThisType(Node* node);
	void checkTypeOperator(Node* node);
	void checkConditionalType(Node* node);
	void checkInferType(Node* node);
	void checkTemplateLiteralType(Node* node);
	void checkImportType(Node* node);
	void checkNamedTupleMember(Node* node);
	void checkIndexedAccessType(Node* node);
	void checkMappedType(Node* node);
	void checkFunctionDeclaration(Node* node);
	void checkBlock(Node* node);
	void checkVariableStatement(Node* node);
	void checkExpressionStatement(Node* node);
	void checkIfStatement(Node* node);
	void checkDoStatement(Node* node);
	void checkWhileStatement(Node* node);
	void checkForStatement(Node* node);
	void checkForInStatement(Node* node);
	void checkForOfStatement(Node* node);
	void checkBreakOrContinueStatement(Node* node);
	void checkReturnStatement(Node* node);
	void checkWithStatement(Node* node);
	void checkSwitchStatement(Node* node);
	void checkLabeledStatement(Node* node);
	void checkThrowStatement(Node* node);
	void checkTryStatement(Node* node);
	void checkVariableDeclaration(Node* node);
	void checkBindingElement(Node* node);
	void checkClassDeclaration(Node* node);
	void checkInterfaceDeclaration(Node* node);
	void checkTypeAliasDeclaration(Node* node);
	void checkEnumDeclaration(Node* node);
	void checkEnumMember(Node* node);
	void checkModuleDeclaration(Node* node);
	void checkImportDeclaration(Node* node);
	void checkImportEqualsDeclaration(Node* node);
	void checkExportDeclaration(Node* node);
	void checkExportAssignment(Node* node);
	void checkMissingDeclaration(Node* node);
	void checkJSDocType(Node* node);
	std::string getIsolatedModulesLikeFlagName();

	// === slice: grammarchecks (checker_grammar.cpp) ===
	bool grammarErrorOnFirstToken(Node* node, const DiagnosticMessage* message,
								  std::vector<std::string> args = {});
	bool grammarErrorAtPos(Node* nodeForSourceFile, int start, int length,
						   const DiagnosticMessage* message,
						   std::vector<std::string> args = {});
	bool grammarErrorOnNode(Node* node, const DiagnosticMessage* message,
							std::vector<std::string> args = {});
	bool grammarErrorOnNodeSkippedOnNoEmit(Node* node, const DiagnosticMessage* message,
										   std::vector<std::string> args = {});
	bool hasParseDiagnostics(SourceFile* sourceFile);
	bool checkGrammarRegularExpressionLiteral(RegularExpressionLiteral* node);
	bool checkGrammarPrivateIdentifierExpression(PrivateIdentifier* privId);
	bool checkGrammarMappedType(MappedTypeNode* node);
	bool checkGrammarDecorator(Decorator* decorator);
	bool checkGrammarExportDeclaration(ExportDeclaration* node);
	bool checkGrammarModuleElementContext(Node* node, const DiagnosticMessage* errorMessage);
	bool checkGrammarModifiers(Node* node);
	bool reportObviousModifierErrors(Node* node);
	Node* findFirstModifierExcept(Node* node, Kind allowedModifier);
	Node* findFirstIllegalModifier(Node* node);
	bool reportObviousDecoratorErrors(Node* node);
	Node* findFirstIllegalDecorator(Node* node);
	bool checkGrammarAsyncModifier(Node* node, Node* asyncModifier);
	bool checkGrammarForDisallowedTrailingComma(NodeList* list, const DiagnosticMessage* diag);
	bool checkGrammarTypeParameterList(NodeList* typeParameters, SourceFile* file);
	bool checkGrammarParameterList(NodeList* parameters);
	bool checkGrammarForUseStrictSimpleParameterList(Node* node);
	bool checkGrammarFunctionLikeDeclaration(Node* node);
	bool checkGrammarClassLikeDeclaration(Node* node);
	bool checkGrammarArrowFunction(Node* node, SourceFile* file);
	bool checkGrammarIndexSignatureParameters(IndexSignatureDeclaration* node);
	bool checkGrammarIndexSignature(IndexSignatureDeclaration* node);
	bool checkGrammarForAtLeastOneTypeArgument(Node* node, NodeList* typeArguments);
	bool checkGrammarTypeArguments(Node* node, NodeList* typeArguments);
	bool checkGrammarTaggedTemplateChain(TaggedTemplateExpression* node);
	bool checkGrammarHeritageClause(HeritageClause* node);
	bool checkGrammarExpressionWithTypeArguments(Node* node);
	bool checkGrammarClassDeclarationHeritageClauses(Node* node, SourceFile* file);
	bool checkGrammarInterfaceDeclaration(InterfaceDeclaration* node);
	bool checkGrammarComputedPropertyName(Node* node);
	bool checkGrammarForGenerator(Node* node);
	bool checkGrammarForInvalidQuestionMark(Node* postfixToken, const DiagnosticMessage* message);
	bool checkGrammarForInvalidExclamationToken(Node* postfixToken, const DiagnosticMessage* message);
	bool checkGrammarObjectLiteralExpression(ObjectLiteralExpression* node, bool inDestructuring);
	bool checkGrammarJsxElement(Node* node);
	bool checkGrammarJsxName(Node* node);
	bool checkGrammarJsxExpression(JsxExpression* node);
	bool checkGrammarForInOrForOfStatement(ForInOrOfStatement* forInOrOfStatement);
	bool checkGrammarAccessor(Node* accessor);
	bool doesAccessorHaveCorrectParameterCount(Node* accessor);
	bool checkGrammarTypeOperatorNode(TypeOperatorNode* node);
	bool checkGrammarForInvalidDynamicName(Node* node, const DiagnosticMessage* message);
	bool isNonBindableDynamicName(Node* node);
	bool checkGrammarMethod(Node* node);
	bool checkGrammarBreakOrContinueStatement(Node* node);
	bool checkGrammarBindingElement(BindingElement* node);
	bool checkGrammarVariableDeclaration(VariableDeclaration* node);
	bool checkGrammarForEsModuleMarkerInBindingName(Node* name);
	bool checkGrammarNameInLetOrConstDeclarations(Node* name);
	bool checkGrammarVariableDeclarationList(VariableDeclarationList* declarationList);
	bool checkGrammarAwaitOrAwaitUsing(Node* node);
	bool checkGrammarYieldExpression(Node* node);
	bool checkGrammarForDisallowedBlockScopedVariableStatement(VariableStatement* node);
	bool containerAllowsBlockScopedVariable(Node* parent);
	bool checkGrammarMetaProperty(MetaProperty* node);
	bool checkGrammarConstructorTypeParameters(ConstructorDeclaration* node);
	bool checkGrammarConstructorTypeAnnotation(ConstructorDeclaration* node);
	bool checkGrammarProperty(Node* node);
	bool checkAmbientInitializer(Node* node);
	bool isInitializerSimpleLiteralEnumReference(Node* expr);
	bool checkGrammarTopLevelElementForRequiredDeclareModifier(Node* node);
	bool checkGrammarTopLevelElementsForRequiredDeclareModifier(SourceFile* file);
	bool checkGrammarStatementInAmbientContext(Node* node);
	void checkGrammarNumericLiteral(NumericLiteral* node);
	bool checkGrammarBigIntLiteral(BigIntLiteral* node);
	bool checkGrammarImportClause(ImportClause* node);
	bool checkGrammarImportAttributeValues(ImportAttributes* node);
	bool checkGrammarTypeOnlyNamedImportsOrExports(Node* namedBindings);
	bool checkGrammarImportCallExpression(Node* node);
	bool checkGrammarImportAttributesType(TypeLiteralNode* attributes);
	// grammarchecks.go dependencies ported alongside the slice
	Symbol* getSymbolForPrivateIdentifierExpression(Node* node);
	Node* getAccessorThisParameter(Node* accessor);
	bool isInParameterInitializerBeforeContainingFunction(Node* node);
	std::pair<std::string, bool> getEffectivePropertyNameForPropertyNameNode(Node* node);
	bool isValidIndexKeyType(Type* t);
	bool isGenericType(Type* t);
	// grammarchecks.go dependencies stubbed — owned by other slices
	Symbol* lookupSymbolForPrivateIdentifierDeclaration(const std::string& propName, Node* location);
	std::pair<std::string, bool> tryGetNameFromType(Type* t);

	// === slice: declchecks === (checker_declchecks.cpp)
	// checker.go:2622-3830 — per-declaration and per-type-node check functions.
	void checkJSDocTypeIsInJsFile(Node* node);
	bool shouldCheckErasableSyntax(Node* node);
	void checkAsyncFunctionReturnType(Node* node, Node* returnTypeNode);
	Node* findFirstSuperCall(Node* node);
	void checkTypeReferenceOrImport(Node* node);
	bool checkTypeArgumentConstraints(Node* node, std::vector<Type*> typeParameters);
	Node* getDeprecatedSuggestionNode(Node* node);
	Node* getTypePredicateParent(Node* node);
	bool checkIfTypePredicateVariableIsDeclaredInBindingPattern(
	    Node* pattern, Node* predicateVariableNode, const std::string& predicateVariableName);
	void checkObjectTypeForDuplicateDeclarations(Node* node, bool checkPrivateNames);
	void reportDuplicateMemberErrors(Node* node, const std::string& name, bool checkStatic,
	                                 bool isStatic, const DiagnosticMessage* message);
	ResolutionMode getResolutionModeOverride(Node* node, bool reportErrors);
	void checkFunctionOrMethodDeclaration(Node* node);
	void checkFunctionOrConstructorSymbol(Symbol* symbol);
	void checkFunctionOrConstructorSymbolWorker(Symbol* symbol);
	ModifierFlags getEffectiveDeclarationFlags(Node* n, ModifierFlags flagsToCheck);
	bool isImplementationCompatibleWithOverload(Signature* implementation, Signature* overload);
	void checkAllCodePathsInNonVoidFunctionReturnOrThrow(Node* fn, Type* returnType);
	bool isUnwrappedReturnTypeUndefinedVoidOrAny(Node* fn, Type* returnType);

	// declchecks dep stubs — owned by other slices (see checker_declchecks.cpp
	// bottom for the single TSC_UNREACHABLE body each)
	void checkVariableLikeDeclaration(Node* node);
	void checkDecorators(Node* node);
	void checkTypeNameIsReserved(Node* name, const DiagnosticMessage* message);
	void checkTypeParameters(const std::vector<Node*>& typeParameterDeclarations);
	bool isReferenceToType(Type* t, Type* target);
	Type* getConstraintOfTypeParameter(Type* typeParameter);
	std::vector<Type*> getTypeParametersForTypeReferenceOrImport(Node* node);
	Type* getTypeOfAccessors(Symbol* symbol);
	void reportImplicitAny(Node* declaration, Type* t, WideningKind wideningKind);
	bool hasBindableName(Node* node);
	Signature* getSignatureFromDeclaration(Node* declaration);
	std::vector<Signature*> getSignaturesOfSymbol(Symbol* symbol);
	bool functionHasImplicitReturn(Node* fn);
	Type* unwrapReturnType(Type* returnType, FunctionFlags functionFlags);
	Signature* getErasedSignature(Signature* signature);
	Type* getReturnTypeFromAnnotation(Node* declaration);
	ModifierFlags getCombinedModifierFlagsCached(Node* node);
	Type* getDefaultFromTypeParameter(Type* t);
	Type* getResolvedTypeParameterDefault(Type* t);
	Type* getTypeFromThisTypeNode(Node* node);
	Type* getTypeFromTypeLiteralOrFunctionOrConstructorTypeNode(Node* node);
	Type* getTypeFromIndexedAccessTypeNode(Node* node);
	Type* getTypeFromTypeQueryNode(Node* node);
	Type* getTypeFromMappedTypeNode(Node* node);
	ElementFlags getTupleElementFlags(Node* node);
	bool isArrayLikeType(Type* t);
	Type* getNullableType(Type* t, TypeFlags flags);
	void checkIndexedAccessIndexType(Type* t, Node* node);
	void checkExternalEmitHelpers(Node* location, ExternalEmitHelpers helpers);
	Type* createMarkerType(Symbol* symbol, Type* source, Type* target);
	bool checkGeneratorInstantiationAssignabilityToReturnType(Type* returnType,
	                                                          FunctionFlags functionFlags,
	                                                          Node* errorNode);
	Type* checkAwaitedType(Type* t, bool withAlias, Node* errorNode,
	                       const DiagnosticMessage* diagnosticMessage);
	Type* getAwaitedTypeNoAlias(Type* t);
	Signature* getContextualCallSignature(Type* t, Node* node);
	bool checkTypeAssignableToEx(Type* source, Type* target, Node* errorNode,
	                           const DiagnosticMessage* headMessage,
	                           std::vector<Diagnostic*>* diagnosticOutput);
	bool isSignatureAssignableTo(Signature* source, Signature* target, bool ignoreReturnTypes);
	TypePredicate* getTypePredicateOfSignature(Signature* sig);
	void checkCollisionsForDeclarationName(Node* node, Node* name);
	void setNodeLinksForPrivateIdentifierScope(Node* node);
	bool classDeclarationExtendsNull(Node* classDecl);
	Symbol* getResolvedSymbolOrNil(Node* node);
	bool IsDeprecatedDeclaration(Node* declaration);
	Diagnostic* addDeprecatedSuggestion(Node* location, std::vector<Node*> declarations,
	                                  const std::string& deprecatedEntity);
	void checkImportAttributes(Node* node);
	ModifierFlags getTypeParameterModifiers(Type* typeParameter);

	// === slice: stmtclass === (checker_stmtclass.cpp)
	// checker.go:3830-5081 — statement checks + class-like declaration
	// machinery + member-override checks + index constraints.
	void checkCatchClause(Node* node);
	void checkClassLikeDeclaration(Node* node);
	void checkTestingKnownTruthyCallableOrAwaitableOrEnumMemberType(Node* condExpr,
	                                                              Type* condType,
	                                                              Node* body);
	void checkTestingKnownTruthyTypes(Node* condExpr, Type* condType, Node* body);
	void checkTestingKnownTruthyType(Node* condExpr, Type* condType, Node* body);
	bool isSymbolUsedInBinaryExpressionChain(Node* node, Symbol* testedSymbol);
	bool isSymbolUsedInConditionBody(Node* expr, Node* body, Node* testedNode,
	                                 Symbol* testedSymbol);
	Type* getIndexTypeOrString(Type* t);
	void checkReturnExpression(Node* container, Type* unwrappedReturnType, Node* node,
	                           Node* expr, Type* exprType, bool inConditionalExpression);
	void checkJSDocAugmentsTagMatchesExtends(Node* node,
	                                         ExpressionWithTypeArguments* baseTypeNode,
	                                         Type* baseType);
	void checkClassForStaticPropertyNameConflicts(Node* node);
	void checkTypeParameterListsIdentical(Symbol* symbol);
	std::vector<Node*> getClassOrInterfaceDeclarationsOfSymbol(Symbol* symbol);
	bool areTypeParametersIdentical(
	    const std::vector<Node*>& declarations, const std::vector<Type*>& targetParameters,
	    const std::function<std::vector<Node*>(Node*)>& getTypeParameterDeclarations);
	void checkBaseTypeAccessibility(Type* t, Node* node);
	void issueMemberSpecificError(Node* node, Type* typeWithThis, Type* baseWithThis,
	                              const DiagnosticMessage* broadDiag);
	Type* getTypeWithoutSignatures(Type* t);
	void checkKindsOfPropertyMemberOverrides(Type* t, Type* baseType);
	bool arePropertiesAbstractOrInterface(Symbol* base,
	                                      ModifierFlags baseDeclarationFlags);
	bool isPropertyAbstractOrInterface(Node* declaration,
	                                   ModifierFlags baseDeclarationFlags);
	void checkMembersForOverrideModifier(Node* node, Type* t, Type* typeWithThis,
	                                     Type* staticType);
	void checkMemberForOverrideModifier(Node* node, Type* staticType,
	                                    Type* baseStaticType, Type* baseWithThis,
	                                    Type* t, Type* typeWithThis, Node* member);
	MemberOverrideStatus getMemberOverrideModifierStatus(Node* node, Node* member,
	                                                     Symbol* memberSymbol);
	MemberOverrideStatus checkMemberForOverrideModifierWorker(
	    Node* node, Type* staticType, Type* baseStaticType, Type* baseWithThis,
	    Type* t, Type* typeWithThis, bool memberHasOverrideModifier,
	    bool memberHasAbstractModifier, bool memberIsStatic,
	    bool memberIsParameterProperty, Symbol* member, Node* errorNode);
	Symbol* getSuggestedSymbolForNonexistentClassMember(std::string name,
	                                                  Type* baseType);
	void checkIndexConstraints(Type* t, Symbol* symbol, bool isStaticIndex);
	void checkIndexConstraintForProperty(Type* t, Symbol* prop, Type* propNameType,
	                                     Type* propType);
	void checkIndexConstraintForIndexSignature(Type* t, IndexInfo* checkInfo);
	void checkClassOrInterfaceForDuplicateIndexSignatures(Node* node);
	void checkTypeForDuplicateIndexSignatures(Node* node);
	void checkPropertyInitialization(Node* node);
	bool isPropertyWithoutInitializer(Node* node);

	// stmtclass dep stubs — owned by other slices
	Type* checkTruthinessExpression(Node* node, CheckMode checkMode);
	void checkReferenceExpression(Node* node, const DiagnosticMessage* invalidReferenceType,
	                              const DiagnosticMessage* constantName);
	void checkDestructuringAssignment(Node* node, Type* sourceType, CheckMode checkMode,
	                                  bool checkResolvedType);
	bool isTypeEqualityComparableTo(Type* source, Type* target);
	Node* getEffectiveCheckNode(Node* node);
	std::string getTypeNameForErrorDisplay(Type* t);
	void checkVariableDeclarationList(Node* node);
	Type* checkRightHandSideOfForOf(Node* node);
	bool checkTypeAssignableToAndOptionallyElaborate(
	    Type* source, Type* target, Node* errorNode, Node* expr,
	    const DiagnosticMessage* headMessage, std::vector<Diagnostic*>* diagnosticOutput);
	bool checkTypeComparableTo(Type* source, Type* target, Node* errorNode,
	                           const DiagnosticMessage* headMessage);
	bool isTypeAssignableToKind(Type* source, TypeFlags kind);
	std::vector<Symbol*> getPropertiesOfObjectType(Type* t);
	Symbol* getPropertyOfObjectType(Type* t, const std::string& name);
	std::vector<Signature*> getConstructorsForTypeArguments(
	    Type* t, const std::vector<Node*>& typeArgumentNodes, Node* location);
	std::vector<Signature*> getInstantiatedConstructorsForTypeArguments(
	    Type* t, const std::vector<Node*>& typeArgumentNodes, Node* location);
	bool isValidBaseType(Type* t);
	Type* getIndexTypeOfType(Type* t, Type* keyType);
	IndexInfo* getIndexInfoOfType(Type* t, Type* keyType);
	Symbol* getIndexSymbol(Symbol* symbol);
	Symbol* getTargetSymbol(Symbol* s);
	ConstructorAccessibilityError* getConstructorAccessibilityError(
	    Node* node, const std::vector<Signature*>& signatures, ModifierFlags modifiers);
	Type* getNonMissingTypeOfSymbol(Symbol* symbol);
	Type* getNonNullableTypeIfNeeded(Type* t);
	bool isMixinConstructorType(Type* t);
	Type* getExtractStringType(Type* t);
	Type* getAwaitedTypeOfPromise(Type* t);
	Symbol* getSymbolAtLocation(Node* node, bool ignoreErrors);
	std::vector<IndexInfo*> getApplicableIndexInfos(Type* t, Type* keyType);
	void checkExportsOnMergedDeclarations(Node* node);
	Type* getBaseConstructorTypeOfClass(Type* t);
	bool hasTypeFacts(Type* t, TypeFacts mask);
	// === slice: nodecopy ===
	printer::SymbolAccessibilityResult isSymbolAccessible(
		Symbol* symbol, Node* enclosingDeclaration, SymbolFlags meaning,
		bool shouldComputeAliasesToMakeVisible);

	// === slice: signatures ===
	// Ported from checker.go:20143-20986 (bodies in checker_signatures.cpp).

	// CheckMode — checker.go:36-48
	using CheckMode = uint32_t;
	static constexpr CheckMode CheckModeNormal = 0;       // Normal type checking
	static constexpr CheckMode CheckModeContextual = 1 << 0; // Explicitly assigned contextual type, therefore not cacheable
	static constexpr CheckMode CheckModeInferential = 1 << 1; // Inferential typing
	static constexpr CheckMode CheckModeSkipContextSensitive = 1 << 2; // Skip context sensitive function expressions
	static constexpr CheckMode CheckModeSkipGenericFunctions = 1 << 3;   // Skip single signature generic functions
	static constexpr CheckMode CheckModeIsForSignatureHelp = 1 << 4;     // Call resolution for purposes of signature help
	static constexpr CheckMode CheckModeRestBindingElement = 1 << 5;     // Checking a type that is going to be used to determine the type of a rest binding element
	static constexpr CheckMode CheckModeTypeOnly = 1 << 6;   // Called from getTypeOfExpression, diagnostics may be omitted
	static constexpr CheckMode CheckModeForceTuple = 1 << 7;

	std::vector<Type*> getTypeParametersFromDeclaration(Node* declaration);
	Symbol* getAnnotatedAccessorThisParameter(Node* accessor);
	Type* getNonCircularReturnTypeOfSignature(Signature* sig);
	Signature* getSignatureOfFullSignatureType(Node* node);
	Type* getParameterTypeOfFullSignature(Node* node, Node* parameter);
	Type* getReturnTypeOfFullSignature(Node* node);
	Type* getAnnotatedAccessorType(Node* accessor);
	Node* getAnnotatedAccessorTypeNode(Node* accessor);
	Type* getReturnTypeFromBody(Node* fn, CheckMode checkMode);
	std::pair<std::vector<Type*>, bool> checkAndAggregateReturnExpressionTypes(Node* fn, CheckMode checkMode);
	std::pair<std::vector<Type*>, std::vector<Type*>> checkAndAggregateYieldOperandTypes(Node* fn, CheckMode checkMode);
	Type* createPromiseType(Type* promisedType);
	Type* createPromiseLikeType(Type* promisedType);
	Type* createPromiseReturnType(Node* fn, Type* promisedType);
	Type* getWidenedLiteralLikeTypeForContextualReturnTypeIfNeeded(Type* t, Type* contextualSignatureReturnType, bool isAsync);
	Type* getWidenedLiteralLikeTypeForContextualIterationTypeIfNeeded(Type* t, Type* contextualSignatureReturnType, IterationTypeKind kind, bool isAsyncGenerator);
	Type* createGeneratorType(Type* yieldType, Type* returnType, Type* nextType, bool isAsyncGenerator);
	void reportErrorsFromWidening(Node* declaration, Type* t, WideningKind wideningKind);
	bool shouldReportErrorsFromWideningWithContextualSignature(Node* declaration, WideningKind wideningKind);
	bool reportWideningErrorsInType(Type* t);
	TypePredicate* getTypePredicateFromBody(Node* fn);
	TypePredicate* checkIfExpressionRefinesAnyParameter(Node* fn, Node* expr);
	Type* checkIfExpressionRefinesParameter(Node* fn, Node* expr, Node* param, Type* initType);
	Type* addOptionalTypeMarker(Type* t);
	Signature* instantiateSignature(Signature* sig, TypeMapper* m);
	Signature* instantiateSignatureEx(Signature* sig, TypeMapper* m, bool eraseTypeParameters);
	IndexInfo* instantiateIndexInfo(IndexInfo* info, TypeMapper* m);

	// dep stubs (bodies at bottom of checker_signatures.cpp)
	Signature* getSingleCallSignature(Type* t);
	Type* getTypeAtPosition(Signature* signature, int pos);
	Type* getRestTypeAtPosition(Signature* source, int pos, bool readonly);
	TypePredicate* newTypePredicate(TypePredicateKind kind, const std::string& parameterName, int32_t parameterIndex, Type* t);
	bool isResolvingReturnTypeOfSignature(Signature* signature);
	Type* getYieldedTypeOfYieldExpression(Node* node, Type* expressionType, Type* sentType, bool isAsync);
	bool isConstContext(Node* node);
	Symbol* instantiateSymbol(Symbol* symbol, TypeMapper* m);
	std::vector<Type*> getTypeArguments(Type* t);
	Type* cloneTypeParameter(Type* tp);
	bool isArrayOrTupleType(Type* t);
	Type* GetPromisedTypeOfPromise(Type* t);
	Type* getContextualType(Node* node, ContextFlags contextFlags);
	Type* getContextualIterationType(IterationTypeKind kind, Node* functionDecl);
	Type* getContextualReturnType(Node* functionDecl, ContextFlags contextFlags);
	Signature* getContextualSignatureForFunctionLikeDeclaration(Node* node);
	Type* unwrapAwaitedType(Type* t);
	IterationTypes getIterationTypesOfIterable(Type* t, IterationUse use, Node* errorNode);
	Type* getIterationTypeOfGeneratorFunctionReturnType(IterationTypeKind typeKind, Type* returnType, bool isAsyncGenerator);
	bool isConstantReference(Node* node);
	bool isSymbolAssigned(Symbol* symbol);
	// === end slice: signatures ===

	// === slice: typenodes ===
	// Type-node -> Type resolution (checker_typenodes.cpp —
	// checker.go:23220-25738)
	Type* tryGetTypeFromTypeNode(Node* node);
	Type* getTypeFromTypeNodeWorker(Node* node);
	Type* getThisType(Node* node);
	Type* getTypeFromLiteralTypeNode(Node* node);
	Type* getTypeFromTypeOperatorNode(Node* node);
	Type* getESSymbolLikeTypeForNode(Node* node);
	Type* getTypeFromTypeReference(Node* node);
	Type* getDistributedTypeParameter(Node* node, Type* t);
	Type* getDistributedTypeFromTypeParameter(Type* t);
	Type* getIntendedTypeFromJSDocTypeReference(Node* node);
	Symbol* getSymbolFromTypeReference(Node* node);
	Symbol* resolveTypeReferenceName(Node* typeReference, SymbolFlags meaning,
								   bool ignoreErrors);
	Symbol* getUnresolvedSymbolForEntityName(Node* name);
	Type* getTypeReferenceType(Node* node, Symbol* symbol);
	Type* getTypeFromClassOrInterfaceReference(Node* node, Symbol* symbol);
	std::vector<Type*> getTypeArgumentsFromNode(Node* node);
	bool checkNoTypeArguments(Node* node, Symbol* symbol);
	bool isResolvedByTypeAlias(Node* node);
	bool mayResolveTypeAlias(Node* node);
	Type* createNormalizedTupleTypeEx(Type* target,
									  const std::vector<Type*>& elementTypes,
									  ObjectFlags objectFlags);
	Type* createNormalizedTupleType(Type* target,
									const std::vector<Type*>& elementTypes);
	std::vector<Type*> getElementTypes(Type* t);
	int getTypeReferenceArity(Type* t);
	bool isReadonlyArrayType(Type* t);
	bool isMutableArrayOrTuple(Type* t);
	Type* getElementTypeOfArrayType(Type* t);
	bool isMutableArrayLikeType(Type* t);
	bool isEmptyArrayLiteralType(Type* t);
	bool isEmptyLiteralType(Type* t);
	bool isTupleLikeType(Type* t);
	bool isArrayOrTupleLikeType(Type* t);
	bool isArrayOrTupleOrIntersection(Type* t);
	Type* getTupleElementType(Type* t, int index);
	Type* getTypeFromTypeAliasReference(Node* node, Symbol* symbol);
	Type* getTypeFromArrayOrTupleTypeNode(Node* node);
	bool isVariadicTupleElement(Node* node);
	bool isReadonlyTypeOperator(Node* node);
	Type* getTypeFromNamedTupleTypeNode(Node* node);
	Type* getTypeFromRestTypeNode(Node* node);
	Node* getArrayElementTypeNode(Node* node);
	Type* getTypeFromOptionalTypeNode(Node* node);
	Type* getTypeFromUnionTypeNode(Node* node);
	Type* getTypeFromIntersectionTypeNode(Node* node);
	Type* getTypeFromTemplateTypeNode(Node* node);
	Type* getTypeFromConditionalTypeNode(Node* node);
	Type* getConditionalType(ConditionalRoot* root, TypeMapper* mapper,
							 bool forConstraint, TypeAlias* alias);
	std::pair<ConditionalRoot*, TypeMapper*> getTailRecursionRoot(
		Type* newType, TypeMapper* newMapper);
	bool isSimpleTupleType(Node* node);
	bool isDeferredType(Type* t, bool checkTuples);
	Type* getPermissiveInstantiation(Type* t);
	Type* getRestrictiveInstantiation(Type* t);
	Type* getTrueTypeFromConditionalType(Type* t);
	Type* getFalseTypeFromConditionalType(Type* t);
	Type* getInferredTrueTypeFromConditionalType(Type* t);
	Type* getTypeFromInferTypeNode(Node* node);
	Type* getTypeFromImportTypeNode(Node* node);
	std::vector<Node*> getIdentifierChain(Node* node);
	Type* resolveImportSymbolType(Node* node, Symbol* symbol,
								  SymbolFlags meaning);
	Type* getGlobalImportMetaExpressionType();
	Type* createIterableType(Type* iteratedType);
	TupleElementInfo getTupleElementInfo(Node* node);
	Type* createTupleType(const std::vector<Type*>& elementTypes);
	Type* getTupleTargetType(
		const std::vector<TupleElementInfo>& elementInfos, bool readonly);
	Type* createTupleTargetType(
	    const std::vector<TupleElementInfo>& elementInfos, bool readonly);
	Type* getElementTypeOfSliceOfTupleType(Type* t, int index,
										   int endSkipCount, bool writing,
										   bool noReductions);
	Type* getRestTypeOfTupleType(Type* t);
	Type* getTupleElementTypeOutOfStartCount(Type* t, Number index,
											 Type* undefinedLikeType);
	bool isGenericObjectType(Type* t);
	bool isGenericReducibleType(Type* t);
	bool isReducibleIntersection(Type* t);
	Type* getConditionalFlowTypeOfType(Type* t, Node* node);
	Type* getImpliedConstraint(Type* t, Node* checkNode, Node* extendsNode);

	// typenodes dep stubs — removed when owner slice lands
	Type* addOptionality(Type* t);
	Type* addOptionalityEx(Type* t, bool isProperty, bool isOptional);
	Type* getIndexedAccessType(Type* objectType, Type* indexType);   // checker.go:27389
	Type* getIndexedAccessTypeEx(Type* objectType, Type* indexType,
								 AccessFlags accessFlags, Node* accessNode,
								 TypeAlias* alias);
	Type* getIndexType(Type* t);
	int getMinTypeArgumentCount(const std::vector<Type*>& typeParameters);
	std::vector<Type*> fillMissingTypeArguments(
		std::vector<Type*> typeArguments,
		const std::vector<Type*>& typeParameters, int minTypeArgumentCount,
		bool isJavaScriptImplicitAny);
	std::string TypeToStringEx(Type* t, Node* enclosingDeclaration,
							   TypeFormatFlags flags, void* verbosityContext);
	Symbol* getPropertyOfTypeEx(Type* type, const std::string& name,
								bool skipObjectFunctionPropertyAugment,
								bool includeTypeOnlyMembers);
	Type* instantiateTypeWithAlias(Type* t, TypeMapper* mapper,
								   TypeAlias* alias);
	TypeAlias* instantiateTypeAlias(TypeAlias* alias, TypeMapper* mapper);
	InferenceContext* newInferenceContext(
		const std::vector<Type*>& typeParameters, Signature* signature,
		InferenceFlags flags,
		std::function<Ternary(Type*, Type*, bool)> compareTypes);
	Type* getActualTypeVariable(Type* t);
	Node* getConstraintDeclaration(Type* type);
	bool isTypeParameterPossiblyReferenced(Type* tp, Node* node);
	Type* getHomomorphicTypeVariable(Type* type);
	Type* getInstantiationExpressionType(Type* exprType, Node* node);
	// === end slice: typenodes ===

	// === slice: members ===
	// checker.go:19186-20143 (getPropertiesOfType … getSignaturesOfSymbol)
	std::vector<Symbol*> getPropertiesOfUnionOrIntersectionType(Type* t);
	std::vector<Signature*> getSignaturesOfStructuredType(Type* t, SignatureKind kind);
	std::vector<IndexInfo*> getIndexInfosOfStructuredType(Type* t);
	Type* getIndexTypeOfTypeEx(Type* t, Type* keyType, Type* defaultType);
	IndexInfo* getApplicableIndexInfo(Type* t, Type* keyType);
	IndexInfo* getApplicableIndexInfoForName(Type* t, const std::string& name);
	IndexInfo* findApplicableIndexInfo(const std::vector<IndexInfo*>& indexInfos,
		Type* keyType);
	bool isApplicableIndexType(Type* source, Type* target);
	void resolveTypeReferenceMembers(Type* t);
	void resolveClassOrInterfaceMembers(Type* t);
	void resolveObjectTypeMembers(Type* t, Type* source,
		const std::vector<Type*>& typeParameters,
		const std::vector<Type*>& typeArguments);
	IndexInfo* findIndexInfo(const std::vector<IndexInfo*>& indexInfos,
	    Type* keyType);
	Type* getTupleBaseType(Type* t);
	void resolveBaseTypesOfClass(Type* t);
	void resolveBaseTypesOfInterface(Type* t);
	bool areAllOuterTypeParametersApplied(Type* t);
	void reportCircularBaseType(Node* node, Type* t);
	bool hasBaseType(Type* t, Type* checkBase);
	Signature* getSignatureInstantiation(Signature* sig,
		const std::vector<Type*>& typeArguments, bool isJavaScript,
		const std::vector<Type*>& inferredTypeParameters);
	Signature* cloneSignature(Signature* sig);
	Signature* createSignatureInstantiation(Signature* sig,
	    const std::vector<Type*>& typeArguments);
	TypeMapper* createSignatureTypeMapper(Signature* sig,
	    const std::vector<Type*>& typeArguments);
	std::vector<Type*> getTypeParametersForMapper(Signature* sig);
	Signature* getCanonicalSignature(Signature* signature);
	Signature* createCanonicalSignature(Signature* signature);
	Signature* getBaseSignature(Signature* signature);
	Signature* instantiateSignatureInContextOf(Signature* signature,
		Signature* contextualSignature, InferenceContext* inferenceContext,
		TypeComparer compareTypes);
	SymbolTable addInheritedMembers(SymbolTable symbols,
		const std::vector<Symbol*>& baseSymbols);
	InterfaceType* resolveDeclaredMembers(Type* t);
	std::vector<IndexInfo*> getIndexInfosOfSymbol(Symbol* symbol);
	std::vector<IndexInfo*> getIndexInfosOfIndexSymbol(
		Symbol* indexSymbol, const std::vector<Symbol*>& siblingSymbols);
	IndexInfo* getObjectLiteralIndexInfo(bool isReadonly,
		const std::vector<Symbol*>& properties, Type* keyType);
	bool isSymbolWithSymbolName(Symbol* symbol);
	bool isSymbolWithNumericName(Symbol* symbol);
	bool isSymbolWithComputedName(Symbol* symbol);
	bool isNumericName(Node* name);
	bool isNumericComputedName(Node* name);
	// checker.go:20987-22284 (resolveAnonymousTypeMembers … isConflictingPrivateProperty)
	void resolveAnonymousTypeMembers(Type* t);
	SymbolTable createInstantiatedSymbolTable(const std::vector<Symbol*>& symbols,
		TypeMapper* m);
	SymbolTable instantiateSymbolTable(const SymbolTable& symbols, TypeMapper* m);
	std::vector<Signature*> getDefaultConstructSignatures(Type* classType);
	void resolveMappedTypeMembers(Type* t);
	Type* getTypeOfMappedSymbol(Symbol* symbol);
	Type* getLowerBoundOfKeyType(Type* t);
	void resolveUnionTypeMembers(Type* t);
	std::vector<Signature*> getArrayMemberCallSignatures(Type* t);
	bool isArrayOrTupleSymbol(Symbol* symbol);
	bool isReadonlyArraySymbol(Symbol* symbol);
	std::vector<Signature*> getUnionSignatures(
		const std::vector<std::vector<Signature*>>& signatureLists);
	Signature* combineUnionOrIntersectionMemberSignatures(Signature* left,
		Signature* right, bool isUnion);
	std::vector<Symbol*> combineUnionOrIntersectionParameters(Signature* left,
		Signature* right, TypeMapper* mapper, bool isUnion);
	Symbol* combineUnionOrIntersectionThisParam(Symbol* left, Symbol* right,
		TypeMapper* mapper, bool isUnion);
	void resolveIntersectionTypeMembers(Type* t);
	std::vector<Signature*> appendSignatures(std::vector<Signature*> signatures,
		const std::vector<Signature*>& newSignatures);
	std::vector<IndexInfo*> appendIndexInfo(std::vector<IndexInfo*> indexInfos,
		IndexInfo* newInfo, bool isUnion);
	std::pair<std::vector<bool>, int> findMixins(const std::vector<Type*>& types);
	Type* includeMixinType(Type* t, const std::vector<Type*>& types,
		const std::vector<bool>& mixinFlags, int index);
	Symbol* getPropertyOfUnionOrIntersectionType(Type* t, const std::string& name,
		bool skipObjectFunctionPropertyAugment);
	Symbol* getUnionOrIntersectionProperty(Type* t, const std::string& name,
	    bool skipObjectFunctionPropertyAugment);
	Symbol* createUnionOrIntersectionProperty(Type* containingType,
		const std::string& name, bool skipObjectFunctionPropertyAugment);
	bool hasCommonDeclaration(OrderedSet<Symbol*>* symbols);
	Symbol* createSymbolWithType(Symbol* source, Type* t);
	bool isMappedTypeGenericIndexedAccess(Type* t);
	Type* getApparentTypeOfMappedType(Type* t);
	Type* getResolvedApparentTypeOfMappedType(Type* t);
	Type* getApparentTypeOfIntersectionType(Type* t, Type* thisArgument);
	Type* getReducedApparentType(Type* t);
	bool isMappingOfSameObjectType(const std::vector<Type*>& types);
	bool somePropertyReducesToNever(Type* t);
	Type* getReducedUnionType(Type* unionType);
	bool isNeverReducedProperty(Symbol* prop);
	Diagnostic* elaborateNeverIntersection(Diagnostic* chain, Node* node, Type* t);
	bool isDiscriminantWithNeverType(Symbol* prop);

	// members-slice dep stubs — defined TSC_UNREACHABLE in checker_members.cpp
	std::vector<Signature*> instantiateSignatures(
		const std::vector<Signature*>& signatures, TypeMapper* m);
	std::vector<IndexInfo*> instantiateIndexInfos(
		const std::vector<IndexInfo*>& indexInfos, TypeMapper* m);
	std::vector<Type*> getInferredTypes(InferenceContext* n);
	Type* getEffectiveRestType(Signature* signature);
	Type* getKnownKeysOfTupleType(Type* t);
	Type* getConditionalTypeInstantiation(Type* t, TypeMapper* mapper,
		bool forConstraint, TypeAlias* alias);
	MappedTypeNameTypeKind getMappedTypeNameTypeKind(Type* t);
	Type* getTemplateTypeFromMappedType(Type* t);
	bool isMappedTypeWithKeyofConstraintDeclaration(Type* t);
	void forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType(Type* t,
		TypeFlags include, bool stringsOnly, const std::function<void(Type*)>& cb);
	bool isReadonlySymbol(Symbol* symbol);
	Type* removeMissingOrUndefinedType(Type* t);
	std::vector<IndexInfo*> getUnionIndexInfos(const std::vector<Type*>& types);
	void resolveReverseMappedTypeMembers(Type* t);
	Signature* findMatchingSignature(const std::vector<Signature*>& signatureList,
		Signature* signature, bool partialMatch, bool ignoreThisTypes,
		bool ignoreReturnTypes);
	std::vector<Signature*> findMatchingSignatures(
		const std::vector<std::vector<Signature*>>& signatureLists,
		Signature* signature, int listIndex);
	Signature* createUnionSignature(Signature* sig,
		const std::vector<Signature*>& unionSignatures);
	bool compareTypeParametersIdentical(const std::vector<Type*>& sourceParams,
		const std::vector<Type*>& targetParams);
	Ternary compareSignaturesIdentical(Signature* source, Signature* target,
		bool partialMatch, bool ignoreThisTypes, bool ignoreReturnTypes,
		const std::function<Ternary(Type*, Type*)>& compareTypes);
	Ternary compareTypesIdentical(Type* source, Type* target);
	Ternary compareProperties(Symbol* sourceProp, Symbol* targetProp,
	    const std::function<Ternary(Type*, Type*)>& compareTypes);
	int getParameterCount(Signature* signature);
	bool hasEffectiveRestParameter(Signature* signature);
	Type* tryGetTypeAtPosition(Signature* signature, int pos);
	int getMinArgumentCount(Signature* signature);
	std::string getParameterNameAtPosition(Signature* signature, int pos);
	std::vector<Type*> getEffectiveTypeArguments(
		Node* node, const std::vector<Type*>& typeParameters);
	// === end slice: members ===

	// === slice: instantiate ===
	// checker.go:22285-23219 — instantiation machinery (checker_instantiate.cpp).
	// (getTypeArguments, getEffectiveTypeArguments, getMinTypeArgumentCount,
	// fillMissingTypeArguments, getNamedMembers, isNamedMember, symbolIsValue[Ex],
	// couldContainTypeVariablesWorker, getConditionalTypeInstantiation,
	// getHomomorphicTypeVariable, get{TypeParameter,ConstraintType,NameType,
	// TemplateType}FromMappedType, isMappedTypeWithKeyofConstraintDeclaration,
	// forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType, instantiateType,
	// instantiateTypes, instantiateSignatures, instantiateIndexInfos are declared
	// in the members block above.)

	// Spare cache maps for pushActiveMapper/popActiveMapper, mirroring Go's
	// cap-reuse of the activeTypeMappersCaches backing array.
	std::vector<CacheMap<Type*>*> freeTypeMappersCaches;

	bool hasTypeParameterDefault(Type* t);
	Type* getDefaultTypeArgumentType(bool isInJavaScriptFile);
	Type* getDefaultOrUnknownFromTypeParameter(Type* t);
	std::vector<std::string> getCircularTypeNames();
	void pushActiveMapper(TypeMapper* mapper);
	void popActiveMapper();
	int findActiveMapper(TypeMapper* mapper);
	void clearActiveMapperCaches();
	bool isNonGenericTopLevelType(Type* t);
	Type* instantiateTypeWorker(Type* t, TypeMapper* m, TypeAlias* alias);
	Type* getObjectTypeInstantiation(Type* t, TypeMapper* m, TypeAlias* alias);
	Type* instantiateAnonymousType(Type* t, TypeMapper* m, TypeAlias* alias);
	Type* instantiateMappedType(Type* t, TypeMapper* m, TypeAlias* alias);
	bool hasArrayOrTypeTypeConstraint(Type* typeVariable);
	Type* instantiateMappedArrayType(Type* arrayType, Type* mappedType, TypeMapper* m);
	Type* instantiateMappedTupleType(Type* tupleType, Type* mappedType,
									 Type* typeVariable, TypeMapper* m);
	Type* instantiateMappedTypeTemplate(Type* t, Type* key, bool isOptional,
										TypeMapper* m);
	Node* getConstraintDeclarationForMappedType(Type* t);
	Type* getApparentMappedTypeKeys(Type* nameType, Type* targetType);
	Type* instantiateReverseMappedType(Type* t, TypeMapper* m);
	std::vector<Symbol*> instantiateSymbols(const std::vector<Symbol*>& symbols,
											TypeMapper* m);
	template <typename T>
	std::vector<T> instantiateList(const std::vector<T>& values, TypeMapper* m,
								   T (Checker::*instantiator)(T, TypeMapper*));

	// Dep stubs — declared here, defined at the bottom of checker_instantiate.cpp.
	Type* createNormalizedTypeReference(Type* target,
										std::vector<Type*> typeArguments);
	Type* createTupleTypeEx(std::vector<Type*> elementTypes,
							std::vector<TupleElementInfo> elementInfos, bool readonly);
	Type* inferTypeForHomomorphicMappedType(Type* source, Type* target,
											Type* constraint);
	// === end slice: instantiate ===

	// === slice: widen ===
	// checker.go:25739-26019 — literal-type widening machinery. Bodies live in
	// checker_widen.cpp; all other in-range methods were already declared above.
	Type* parseBigIntLiteralType(const std::string& text);

	// === slice: markrefs ===
	// checker.go:28655-29384 — declaration reference marking (markLinkedReferences
	// and everything in the range). Bodies live in checker_markrefs.cpp.
	void markLinkedReferences(Node* location, ReferenceHint hint, Symbol* propSymbol,
		Type* parentType);
	void markIdentifierAliasReferenced(Node* location);
	void markPropertyAliasReferenced(Node* location, Symbol* propSymbol,
	    Type* parentType);
	void markExportAssignmentAliasReferenced(Node* location);
	void markJsxAliasReferenced(Node* node);
	void markImportEqualsAliasReferenced(Node* location);
	void markExportSpecifierAliasReferenced(Node* location);
	bool hasSignatureWithArityGreaterThan(Symbol* symbol, int arity);
	std::vector<std::string> getHelperNames(ExternalEmitHelpers helper);
	Symbol* resolveHelpersModule(SourceFile* file, Node* errorNode);
	void markDecoratorAliasReferenced(Node* node);
	Node* getParameterTypeNodeForDecoratorCheck(Node* node);
	void markDecoratorMedataDataTypeNodeAsReferenced(Node* node);
	Node* getEntityNameForDecoratorMetadata(Node* node);
	Node* getEntityNameForDecoratorMetadataFromTypeList(std::vector<Node*> typeNodes);
	void markAliasReferenced(Symbol* symbol, Node* location);
	void markAliasSymbolAsReferenced(Symbol* symbol);
	void markExportAsReferenced(Node* node);
	void markEntityNameOrEntityExpressionAsReference(Node* typeName,
		bool forDecoratorMetadata);
	void markTypeNodeAsReferenced(Node* node);

	// === dep decls — checker_markrefs.cpp callers, owned by other slices ===
	// owner: expressions slice (checker.go:8090-14185)
	bool isMethodAccessForCall(Node* node);
	Symbol* getPrivateIdentifierPropertyOfType(Type* leftType,
		Symbol* lexicallyScopedIdentifier);
	// owner: jsx slice (jsx.go)
	Symbol* getJsxNamespaceContainerForImplicitImport(Node* location);
	std::string getJsxNamespace(Node* location);
	Node* getJsxFactoryEntity(Node* location);
	// owner: relater slice (relater.go)
	// owner: signatures slice (checker.go:20143-20986)
	// owner: declchecks slice (checker.go:5081-5929)
	bool checkExternalImportOrExportDeclaration(Node* node);
	// owner: instantiate slice (checker.go:22285-23219)

	// === slice: decltypes === (checker_decltypes.cpp; checker.go:16720-19097)

	// Symbol type resolution — declared-type layer.
	Type* GetTypeOfSymbolAtLocation(Symbol* symbol, Node* location);
	Type* getTypeOfInstantiatedSymbol(Symbol* symbol);
	Type* getWriteTypeOfInstantiatedSymbol(Symbol* symbol);
	Type* getTypeOfVariableOrParameterOrProperty(Symbol* symbol);
	bool isParameterOfContextSensitiveSignature(Symbol* symbol);
	Type* getTypeOfVariableOrParameterOrPropertyWorker(Symbol* symbol);
	Type* getWidenedTypeForVariableLikeDeclaration(Node* declaration, bool reportErrors);
	Type* getTypeForVariableLikeDeclaration(Node* declaration, bool includeOptionality,
		CheckMode checkMode);
	Type* checkDeclarationInitializer(Node* declaration, CheckMode checkMode,
		Type* contextualType);
	Type* padObjectLiteralType(Type* t, Node* pattern);
	std::string getPropertyNameFromBindingElement(Node* e);
	Type* padTupleType(Type* t, Node* pattern);
	Type* widenTypeInferredFromInitializer(Node* declaration, Type* t);
	Type* getWidenedLiteralTypeForInitializer(Node* declaration, Type* t);
	Type* getTypeOfFuncClassEnumModule(Symbol* symbol);
	Type* getTypeOfFuncClassEnumModuleWorker(Symbol* symbol);
	Type* getBaseTypeVariableOfClass(Symbol* symbol);
	bool isFunctionType(Type* t);
	bool isConstructorType(Type* t);
	Type* getTypeOfParameter(Symbol* symbol);
	Type* getConstraintOfType(Type* t);
	bool hasNonCircularBaseConstraint(Type* t);
	Type* getConstraintFromTypeParameter(Type* t);
	Type* getConstraintOrUnknownFromTypeParameter(Type* t);
	Type* getInferredTypeParameterConstraint(Type* t, bool omitTypeReferences);
	std::vector<Type*> getTypeParametersForTypeAndSymbol(Type* t, Symbol* symbol);
	Type* getEffectiveTypeArgumentAtIndex(Node* node,
		const std::vector<Type*>& typeParameters, size_t index);
	Type* getConstraintOfIndexedAccess(Type* t);
	Type* getConstraintFromIndexedAccess(Type* t);
	Type* getConstraintOfConditionalType(Type* t);
	Type* getConstraintFromConditionalType(Type* t);
	Type* getDefaultConstraintOfConditionalType(Type* t);
	Type* getConstraintOfDistributiveConditionalType(Type* t);
	bool isNullOrUndefined(Node* node);
	Type* getTypeForBindingElement(Node* declaration);
	Type* getTypeForBindingElementParent(Node* node, CheckMode checkMode);
	Type* getBindingElementTypeFromParentType(Node* declaration, Type* parentType,
		bool noTupleBoundsCheck);
	Type* getRestType(Type* source, const std::vector<Node*>& properties, Symbol* symbol);
	Type* getFlowTypeOfDestructuring(Node* node, Type* declaredType);
	Node* getSyntheticElementAccess(Node* node);
	Node* getParentElementAccess(Node* node);
	Type* getTypeFromBindingPattern(Node* pattern, bool includePatternInType,
		bool reportErrors);
	Type* getTypeFromObjectBindingPattern(Node* pattern, bool includePatternInType,
		bool reportErrors);
	Type* getTypeFromArrayBindingPattern(Node* pattern, bool includePatternInType,
		bool reportErrors);
	Type* getTypeFromBindingElement(Node* element, bool includePatternInType,
		bool reportErrors);
	bool declarationBelongsToPrivateAmbientMember(Node* declaration);
	Type* getTypeOfPrototypeProperty(Symbol* prototype);
	Type* getWidenedTypeForAssignmentDeclaration(Symbol* symbol);
	Type* getAssignmentDeclarationInitializerType(Node* node);
	bool hasParentWithTypeAnnotation(Symbol* symbol);
	bool containsSameNamedThisProperty(Node* thisProperty, Node* expression);
	Type* getTypeFromPropertyDescriptor(Node* node);
	std::pair<thisAssignmentDeclarationKind, Node*> isConstructorDeclaredThisProperty(Symbol* symbol);
	bool isGlobalSymbolConstructor(Node* node);
	Type* widenTypeForVariableLikeDeclaration(Type* t, Node* declaration,
		bool reportErrors);
	Type* getWidenedTypeWithContext(Type* t, WideningContext* context);
	Type* getWidenedTypeOfObjectLiteral(Type* t, WideningContext* context);
	Symbol* getWidenedProperty(Symbol* prop, WideningContext* context);
	std::vector<Symbol*> getPropertiesOfContext(WideningContext* context);
	std::vector<Type*> getSiblingsOfContext(WideningContext* context);
	Symbol* getUndefinedProperty(Symbol* prop);
	Type* getTypeOfEnumMember(Symbol* symbol);
	Type* getWriteTypeOfAccessors(Symbol* symbol);
	Type* getTypeOfAlias(Symbol* symbol);
	bool IsNullableType(Type* t);

		std::deque<WideningContext> wideningContextPool;  // storage for WideningContext::getChildContext

// Owned elsewhere; declared here because checker_decltypes.cpp calls them.
	// Bodies are TSC_UNREACHABLE stubs in checker_decltypes.cpp until their slices land.
	Type* checkShorthandPropertyAssignment(Node* node, bool inDestructuringPattern,
		CheckMode checkMode);
	Type* getAdjustedTypeWithFacts(Type* t, TypeFacts facts);
	Type* getContextualThisParameterType(Node* fn);
	Type* getContextuallyTypedParameterType(Node* declaration);
	Node* getDeclaringConstructor(Symbol* symbol);
	std::pair<std::string, bool> getDestructuringPropertyName(Node* node);
	Type* checkExpressionForMutableLocation(Node* node, CheckMode checkMode);
	Type* checkIteratedTypeOrElementType(IterationUse use, Type* inputType,
		Type* sentType, Node* errorNode);
	Type* checkJsxAttribute(Node* node, CheckMode checkMode);
	Type* checkObjectLiteralMethod(Node* node, CheckMode checkMode);
	Type* checkPropertyAssignment(Node* node, CheckMode checkMode);
	Type* getFlowTypeInConstructor(Symbol* symbol, Node* constructor);
	Type* getFlowTypeInStaticBlocks(Symbol* symbol,
		const std::vector<Node*>& staticBlocks);
	Type* getFlowTypeOfReference(Node* reference, Type* declaredType);
	Type* getNonUndefinedType(Type* t);
	Type* getSimplifiedType(Type* t, bool writing);
	Type* getSimplifiedTypeOrConstraint(Type* t);
	Symbol* getSpreadSymbol(Symbol* prop, bool readonly);
	Type* getTypeOfFirstParameterOfSignature(Signature* sig);
	Type* getTypeOfInitializer(Node* declaration);
	Type* getTypeOfPropertyInBaseClass(Symbol* symbol);
	Type* getTypeOfReverseMappedSymbol(Symbol* symbol);
	Type* getTypeWithFacts(Type* t, TypeFacts include);
	bool hasDefaultValue(Node* node);
	bool isContextSensitiveFunctionOrObjectLiteralMethod(Node* node);
	bool isMatchingReference(Node* source, Node* target);
	bool isSpreadableProperty(Symbol* prop);
	Type* sliceTupleType(Type* t, int index, int endSkipCount);
	bool isValidSpreadType(Type* t);
	Type* removeMissingType(Type* t, bool isOptional);
	Type* removeOptionalTypeMarker(Type* t);
	Type* substituteIndexedMappedType(Type* objectType, Type* indexType);
	// === slice: contextual (checker.go:29385-32069) ===
	Type* getPromisedTypeOfPromiseEx(Type* t, Node* errorNode,
									 Type** thisTypeForErrorOut);
	Type* getTypeOfFirstParameterOfSignatureWithFallback(Signature* signature,
													   Type* fallbackType);
	int getCombinedMappedTypeOptionality(Type* t);
	Type* removeDefinitelyFalsyTypes(Type* t);
	Type* extractDefinitelyFalsyTypes(Type* t);
	Type* getDefinitelyFalsyPartOfType(Type* t);
	bool couldAccessOptionalProperty(Type* objectType, Type* indexType);
	Type* getTypeOfPropertyOrIndexSignatureOfType(Type* t,
												const std::string& name);
	Type* getContextualTypeForInitializerExpression(Node* node,
													ContextFlags contextFlags);
	Type* getContextualTypeForVariableLikeDeclaration(Node* declaration,
													  ContextFlags contextFlags);
	Type* getSpreadArgumentType(const std::vector<Node*>& args, int index,
								int argCount, Type* restType,
								InferenceContext* context, CheckMode checkMode);
	Type* getMutableArrayOrTupleType(Type* t);
	Type* getContextualTypeForBindingElement(Node* declaration,
											 ContextFlags contextFlags);
	Type* getContextualTypeForStaticPropertyDeclaration(Node* declaration,
														ContextFlags contextFlags);
	Type* getContextualTypeForReturnExpression(Node* node,
											   ContextFlags contextFlags);
	Type* getContextualTypeForYieldOperand(Node* node,
										   ContextFlags contextFlags);
	Type* getContextualTypeForAwaitOperand(Node* node,
										   ContextFlags contextFlags);
	Type* getContextualTypeForArgument(Node* callTarget, Node* arg);
	Type* getContextualTypeForArgumentAtIndex(Node* callTarget, int argIndex);
	Type* getContextualTypeForDecorator(Node* decorator);
	Type* getContextualTypeForBinaryOperand(Node* node,
											ContextFlags contextFlags);
	Type* getContextualTypeForAssignmentExpression(BinaryExpression* binary);
	Type* getContextualTypeForObjectLiteralElement(Node* element,
												   ContextFlags contextFlags);
	Type* getContextualTypeForObjectLiteralMethod(Node* node,
												  ContextFlags contextFlags);
	Type* getContextualTypeForElementExpression(Type* t, int index, int length,
												int firstSpreadIndex,
												int lastSpreadIndex);
	Type* getContextualTypeForConditionalOperand(Node* node,
												 ContextFlags contextFlags);
	Type* getContextualTypeForSubstitutionExpression(Node* templateNode,
													 Node* substitutionExpression);
	Type* getContextualImportAttributeType(Node* node);
	std::vector<Node*> getEffectiveCallArguments(Node* node);
	int getSpreadArgumentIndex(const std::vector<Node*>& args);
	Node* createSyntheticExpression(Node* parent, Type* t, bool isSpread,
									Node* tupleNameSource);
	std::pair<int, int> getSpreadIndices(Node* node);
	std::vector<Node*> getEffectiveDecoratorArguments(Node* node);
	Signature* getDecoratorCallSignature(Node* decorator);
	Signature* getLegacyDecoratorCallSignature(Node* decorator);
	Signature* getESDecoratorCallSignature(Node* decorator);
	Type* newClassDecoratorContextType(Type* classType);
	Type* newClassMethodDecoratorContextType(Type* classType, Type* valueType);
	Type* newClassGetterDecoratorContextType(Type* classType, Type* valueType);
	Type* newClassSetterDecoratorContextType(Type* classType, Type* valueType);
	Type* newClassAccessorDecoratorContextType(Type* thisType, Type* valueType);
	Type* newClassFieldDecoratorContextType(Type* thisType, Type* valueType);
	Type* getClassMemberDecoratorContextOverrideType(Type* nameType,
													 bool isPrivate,
													 bool isStatic);
	Type* newClassMemberDecoratorContextTypeForNode(Node* node, Type* thisType,
													Type* valueType);
	Type* newClassAccessorDecoratorTargetType(Type* thisType, Type* valueType);
	Type* newClassAccessorDecoratorResultType(Type* thisType, Type* valueType);
	Type* newClassFieldDecoratorInitializerMutatorType(Type* thisType,
													   Type* valueType);
	Signature* newESDecoratorCallSignature(Type* targetType, Type* contextType,
										   Type* nonOptionalReturnType);
	Type* newFunctionType(const std::vector<Type*>& typeParameters,
						  Symbol* thisParameter,
						  const std::vector<Symbol*>& parameters,
						  Type* returnType);
	Type* newGetterFunctionType(Type* t);
	Type* newSetterFunctionType(Type* t);
	Signature* newCallSignature(const std::vector<Type*>& typeParameters,
								Symbol* thisParameter,
								const std::vector<Symbol*>& parameters,
								Type* returnType);
	Type* newTypedPropertyDescriptorType(Type* propertyType);
	Type* getParentTypeOfClassElement(Node* node);
	Type* getClassElementPropertyKeyType(Node* element);
	Type* getTypeOfPropertyOfContextualType(Type* t, const std::string& name);
	Type* getTypeOfPropertyOfContextualTypeEx(Type* t, const std::string& name,
											  Type* nameType);
	Type* getIndexedMappedTypeSubstitutedTypeOfContextualType(
		Type* t, const std::string& name, Type* nameType);
	bool isExcludedMappedPropertyName(Type* t, Type* propertyNameType);
	Type* getTypeOfConcretePropertyOfContextualType(Type* t,
													const std::string& name);
	Type* getTypeFromIndexInfosOfContextualType(Type* t, const std::string& name,
												Type* nameType);
	bool isCircularMappedProperty(Symbol* symbol);
	std::vector<Type*> appendContextualPropertyTypeConstituent(
		std::vector<Type*> types, Type* t);
	Type* discriminateContextualTypeByObjectMembers(Node* node,
													Type* contextualType);
	Type* getMatchingUnionConstituentForObjectLiteral(Type* unionType,
													  Node* node);
	bool isPossiblyDiscriminantValue(Node* node);
	Type* instantiateInstantiableTypes(Type* t, TypeMapper* mapper);
	void pushCachedContextualType(Node* node);
	int findContextualNode(Node* node, bool includeCaches);
	bool isContextSensitiveFunctionLikeDeclaration(Node* node);
	bool hasContextSensitiveReturnExpression(Node* node);
	bool hasContextSensitiveYieldExpression(Node* node);
	TypeFacts getTypeFacts(Type* t, TypeFacts mask);
	TypeFacts getTypeFactsWorker(Type* t, TypeFacts callerOnlyNeeds);
	TypeFacts getIntersectionTypeFacts(Type* t, TypeFacts callerOnlyNeeds);
	bool isFunctionObjectType(Type* t);
	Type* removeNullableByIntersection(Type* t, TypeFacts targetFacts,
									   TypeFacts otherFacts,
									   TypeFacts otherIncludesFacts,
									   Type* otherType);
	Type* recombineUnknownType(Type* t);
	Type* getGlobalNonNullableTypeInstantiation(Type* t);
	Type* convertAutoToAny(Type* t);
	Type* getAwaitedTypeNoAliasEx(Type* t, Node* errorNode,
								  const DiagnosticMessage* diagnosticMessage,
								  const std::vector<std::string>& args = {});
	bool isAwaitedTypeInstantiation(Type* t);
	bool isAwaitedTypeNeeded(Type* t);
	Type* createAwaitedTypeIfNeeded(Type* t);
	Type* tryCreateAwaitedType(Type* t);
	bool isThenableType(Type* t);
	Type* getAwaitedTypeOfPromiseEx(Type* t, Node* errorNode,
									const DiagnosticMessage* diagnosticMessage,
									const std::vector<std::string>& args = {});
	bool isSomeSymbolAssigned(Node* rootDeclaration);
	bool isSomeSymbolAssignedWorker(Node* node);
	Type* getNarrowableTypeForReference(Type* t, Node* reference,
										CheckMode checkMode);
	bool isConstraintPosition(Type* t, Node* node);
	bool isGenericTypeWithUnionConstraint(Type* t);
	bool isGenericTypeWithoutNullableConstraint(Type* t);
	bool hasContextualTypeWithNoGenericTypes(Node* node, CheckMode checkMode);
	bool isGenericTypeWithUndefinedConstraint(Type* t);
	// contextual-slice dep stubs — defined TSC_UNREACHABLE in checker_contextual.cpp
	Signature* getResolvedSignature(Node* node,
									std::vector<Signature*>* candidatesOutArray,
									CheckMode checkMode);
	Signature* getContextualSignature(Node* node);
	Type* getTypeOfNode(Node* node);
	Type* getThisTypeOfSignature(Signature* signature);
	Type* getIteratedTypeOrElementType(IterationUse use, Type* inputType,
									   Type* sentType, Node* errorNode,
									   bool checkAssignability);
	IterationTypes getIterationTypesOfGeneratorFunctionReturnType(
		Type* t, bool isAsyncGenerator);
	std::string getKeyPropertyName(Type* t);
	Type* getConstituentTypeForKeyType(Type* t, Type* keyType);
	bool isDiscriminantProperty(Type* t, const std::string& name);
	Type* discriminateTypeByDiscriminableItems(Type* target,
											   Discriminator& discriminator);
	bool isConstTypeVariable(Type* t, int depth);
	Type* discriminateContextualTypeByJSXAttributes(Node* node,
													Type* contextualType);
	Type* getContextualJsxElementAttributesType(Node* node,
												ContextFlags contextFlags);
	Type* getContextualTypeForJsxAttribute(Node* attribute,
										   ContextFlags contextFlags);
	Type* getContextualTypeForJsxExpression(Node* node,
											ContextFlags contextFlags);
	Type* getEffectiveFirstArgumentForJsxSignature(Signature* signature,
												   Node* node);
	// === end slice: contextual ===
	// === slice: typeops === (checker_typeops.cpp — checker.go:26223-28654:
	// index/indexed-access machinery, base constraints, literal/enum helpers,
	// simplification, normalization, member transforms)
	std::vector<Type*> UnionTypes();
	Type* intersectTypes(Type* type1, Type* type2);
	// getLiteralTypeFromProperties / getLiteralTypeFromProperty /
	// getLiteralTypeFromPropertyName / isKeyTypeIncluded — declared above (1728-1731)
	// checkComputedPropertyName — declared above (1675)
	// isNoInferType — declared above (1722)
	Type* getSubstitutionIntersection(Type* t);
	// shouldDeferIndexType — declared above (1724)
	// getIndexTypeForGenericType / getIndexTypeForMappedType — declared above (1725-1726)
	Type* getIndexedAccessTypeOrUndefined(Type* objectType, Type* indexType,
	                                      AccessFlags accessFlags, Node* accessNode,
	                                      TypeAlias* alias);
	Type* getPropertyTypeForIndexType(Type* originalObjectType, Type* objectType,
	                                  Type* indexType, Type* fullIndexType,
	                                  Node* accessNode, AccessFlags accessFlags);
	bool typeHasStaticProperty(const std::string& propName, Type* containingType);
	std::string getSuggestionForNonexistentProperty(const std::string& name,
	                                                Type* containingType);
	std::string getSuggestionForNonexistentIndexSignature(Type* objectType, Node* expr,
	                                                      Type* keyedType);
	Type* getSuggestedTypeForNonexistentStringLiteralType(Type* source, Type* target);
	void errorIfWritingToReadonlyIndex(IndexInfo* indexInfo, Type* objectType,
	                                   Node* accessExpression);
	bool isSelfTypeAccess(Node* name, Symbol* parent);
	bool isAssignmentToReadonlyEntity(Node* expr, Symbol* symbol,
	                                  AssignmentKind assignmentKind);
	bool isThisPropertyAccessInConstructor(Node* node, Symbol* prop);
	bool isAutoTypedProperty(Symbol* symbol);
	std::string getPropertyNameFromIndex(Type* indexType, Node* accessNode);
	// isStringIndexSignatureOnlyTypeWorker — declared above (1596)
	bool shouldDeferIndexedAccessType(Type* objectType, Type* indexType,
	                                  Node* accessNode);
	// getNoInferType — declared above (1723)
	// getBaseConstraintOrType — declared above (1428)
	// getBaseConstraintOfType — declared above (1737)
	Type* getResolvedBaseConstraint(Type* t, std::vector<RecursionId> stack);
	Type* computeBaseConstraint(Type* t, std::vector<RecursionId> stack);
	Type* getNextBaseConstraint(Type* t, const std::vector<RecursionId>& stack);
	// maybeTypeOfKind — declared above (1861)
	bool maybeTypeOfKindConsideringBaseConstraint(Type* t, TypeFlags kind);
	bool allTypesAssignableToKind(Type* source, TypeFlags kind);
	// allTypesAssignableToKindEx — declared above (1524)
	bool isTypeAssignableToKindEx(Type* source, TypeFlags kind, bool strict);
	// markPropertyAsReferenced — declared above (1862)
	std::vector<Symbol*> expandSignatureParametersWithTupleMembers(
		Signature* signature, TypeReference* restType, int restIndex, Symbol* restSymbol);
	std::vector<std::string> getUniqAssociatedNamesFromTupleType(TypeReference* t,
	                                                           Symbol* restSymbol);
	bool isUnknownLikeUnionType(Type* t);
	bool isUniformUnionType(Type* t);
	bool computeIsUniformUnionType(const std::vector<Type*>& types);
	// containsUndefinedType — declared above (1608)
	bool typeHasCallOrConstructSignatures(Type* t);
	Type* getNormalizedType(Type* t, bool writing);
	Type* getSimplifiedIndexedAccessType(Type* t, bool writing);
	Type* getSimplifiedIndexedAccessTypeWorker(Type* t, bool writing);
	Type* distributeObjectOverIndexType(Type* objectType, Type* indexType, bool writing);
	Type* distributeIndexOverObjectType(Type* objectType, Type* indexType, bool writing);
	Type* getSimplifiedConditionalType(Type* t, bool writing);
	bool isIntersectionEmpty(Type* type1, Type* type2);
	Type* getNormalizedUnionOrIntersectionType(Type* t, bool writing);
	bool shouldNormalizeIntersection(Type* t);
	Type* getNormalizedTupleType(Type* t, bool writing);
	Type* getSingleBaseForNonAugmentingSubtype(Type* t);
	// getModifiersTypeFromMappedType — declared above (1799)
	Type* extractTypesOfKind(Type* t, TypeFlags kind);
	Type* getRegularTypeOfObjectLiteral(Type* t);
	SymbolTable transformTypeOfMembers(Type* t, const std::function<Type*(Type*)>& f);

	// typeops dep decls — owned by other slices; bodies stubbed in
	// checker_typeops.cpp under "dep stubs".
	std::string getTupleElementLabel(const TupleElementInfo& elementInfo,
	                               Symbol* restSymbol, int index);          // relater slice
	Node* getControlFlowContainer(Node* node);                              // expressions slice
	bool isUncalledFunctionReference(Node* node, Symbol* prop);             // expressions slice
	bool isJSLiteralType(Type* t);                                          // decltypes slice
	bool isDeprecatedSymbol(Symbol* symbol);                                // decltypes slice
	// === slice: utilities ===
	std::unordered_map<std::string, bool>& getPackagesMap();     // utilities.go:1722
	bool typesPackageExists(const std::string& packageName);     // utilities.go:1737
	bool packageBundlesTypes(const std::string& packageName);    // utilities.go:1743
	bool containsNonMissingUndefinedType(Type* t);               // utilities.go:1647
	Symbol* GetAliasedSymbol(Symbol* symbol);                    // checker.go:32660
	bool callLikeExpressionMayHaveTypeArguments(Node* node);     // utilities.go:1132
	// === end slice: utilities ===

	// === slice: services === — checker.go:32070-32660 (API-facing tail)
	Symbol* GetSymbolAtLocation(Node* node);
	std::vector<Node*> getIndexSignaturesAtLocation(Node* node);
	Symbol* getSymbolOfNameOrPropertyAccessExpression(Node* name);
	bool isThisPropertyAndThisTyped(Node* node);
	Type* getThisTypeOfObjectLiteralFromContextualType(Node* containingLiteral, Type* contextualType);
	Type* getThisTypeFromContextualType(Type* t);
	Type* getThisTypeArgument(Type* t);
	Symbol* getApplicableIndexSymbol(Type* t, Type* keyType);
	Type* getRegularTypeOfExpression(Node* expr);
	Type* GetTypeAtLocation(Node* node);
	EmitResolver* GetEmitResolver();
	Type* getImportAttributesTypeForModuleSpecifier(Node* moduleSpecifier);
	Symbol* getSymbolOfPartOfRightHandSideOfImportEquals(Node* entityName);
	// expr/jsx/emitresolver-owned deps (stubs until those slices land)
	Symbol* checkNewTargetMetaProperty(Node* node);
	Type* checkMetaPropertyKeyword(Node* node);
	Type* getSymbolHasInstanceMethodOfObjectType(Type* t);
	Symbol* getIntrinsicTagSymbol(Node* node);
	Type* checkImportAttributesExpression(Node* node);
	// === end slice: services ===
	// === slice: flow === (checker_flow.cpp)
	// flow.go — control-flow type narrowing. All functions in Go file order;
	// free functions are file-local in checker_flow.cpp.
	FlowType newFlowType(Type* t, bool incomplete);
	FlowState* getFlowState();
	void putFlowState(FlowState* f);

	FlowType getTypeAtFlowNode(FlowState* f, FlowNode* flow);
	FlowType getTypeAtFlowAssignment(FlowState* f, FlowNode* flow);
	Type* getInitialOrAssignedType(FlowState* f, FlowNode* flow);
	bool isEmptyArrayAssignment(Node* node);
	FlowType getTypeAtFlowCall(FlowState* f, FlowNode* flow);
	Type* narrowTypeByTypePredicate(FlowState* f, Type* t, TypePredicate* predicate,
									Node* callExpression, bool assumeTrue);
	Type* narrowTypeByAssertion(FlowState* f, Type* t, Node* expr);
	FlowType getTypeAtFlowCondition(FlowState* f, FlowNode* flow);
	Type* narrowType(FlowState* f, Type* t, Node* expr, bool assumeTrue);
	Type* narrowTypeByOptionality(FlowState* f, Type* t, Node* expr, bool assumePresent);
	Type* narrowTypeByTruthiness(FlowState* f, Type* t, Node* expr, bool assumeTrue);
	Type* narrowTypeByCallExpression(FlowState* f, Type* t, Node* callExpression,
									 bool assumeTrue);
	Type* narrowTypeByBinaryExpression(FlowState* f, Type* t, BinaryExpression* expr,
									   bool assumeTrue);
	Type* narrowTypeByEquality(Type* t, Kind operatorKind, Node* value, bool assumeTrue);
	Type* narrowTypeByTypeof(FlowState* f, Type* t, TypeOfExpression* typeOfExpr,
							 Kind operatorKind, Node* literal, bool assumeTrue);
	Type* narrowTypeByLiteralExpression(Type* t, Node* literal, bool assumeTrue);
	Type* narrowTypeByTypeName(Type* t, const std::string& typeName);
	Type* narrowTypeByTypeFacts(Type* t, Type* impliedType, TypeFacts facts);
	Type* narrowTypeByDiscriminantProperty(Type* t, Node* access, Kind operatorKind,
										   Node* value, bool assumeTrue);
	Type* narrowTypeByDiscriminant(Type* t, Node* access,
								   const std::function<Type*(Type*)>& narrowType);
	bool isMatchingConstructorReference(FlowState* f, Node* expr);
	Type* narrowTypeByConstructor(Type* t, Kind operatorKind, Node* identifier,
								  bool assumeTrue);
	bool isConstructedBy(Type* source, Type* target);
	Type* narrowTypeByBooleanComparison(FlowState* f, Type* t, Node* expr,
										Node* boolValue, Kind operatorKind, bool assumeTrue);
	Type* narrowTypeByInstanceof(FlowState* f, Type* t, BinaryExpression* expr,
								 bool assumeTrue);
	Type* getNarrowedType(Type* t, Type* candidate, bool assumeTrue, bool checkDerived);
	Type* getNarrowedTypeWorker(Type* t, Type* candidate, bool assumeTrue,
								bool checkDerived);
	Type* getInstanceType(Type* constructorType);
	Type* narrowTypeByPrivateIdentifierInInExpression(FlowState* f, Type* t,
													  BinaryExpression* expr, bool assumeTrue);
	Type* narrowTypeByInKeyword(FlowState* f, Type* t, Type* nameType, bool assumeTrue);
	bool isTypePresencePossible(Type* t, const std::string& propName, bool assumeTrue);
	Type* narrowTypeByOptionalChainContainment(FlowState* f, Type* t, Kind operatorKind,
											   Node* value, bool assumeTrue);
	FlowType getTypeAtSwitchClause(FlowState* f, FlowNode* flow);
	Type* narrowTypeBySwitchOnDiscriminant(Type* t, FlowSwitchClauseData* data);
	Type* narrowTypeBySwitchOnTypeOf(Type* t, FlowSwitchClauseData* data);
	Type* narrowTypeBySwitchOnTrue(FlowState* f, Type* t, FlowSwitchClauseData* data);
	Type* narrowTypeBySwitchOptionalChainContainment(
		Type* t, FlowSwitchClauseData* data,
		const std::function<bool(Type*)>& clauseCheck);
	Type* narrowTypeBySwitchOnDiscriminantProperty(Type* t, Node* access,
												   FlowSwitchClauseData* data);
	FlowType getTypeAtFlowBranchLabel(FlowState* f, FlowNode* flow, FlowList* antecedents);
	Type* getUnionOrEvolvingArrayType(FlowState* f, const std::vector<Type*>& types,
									  UnionReduction subtypeReduction);
	FlowType getTypeAtFlowLoopLabel(FlowState* f, FlowNode* flow);
	FlowType getTypeAtFlowArrayMutation(FlowState* f, FlowNode* flow);
	Node* getDiscriminantPropertyAccess(FlowState* f, Node* expr, Type* computedType);
	Node* getCandidateDiscriminantPropertyAccess(FlowState* f, Node* expr);
	Type* getEvolvingArrayType(Type* elementType);
	Type* getElementTypeOfEvolvingArrayType(Type* t);
	bool isEvolvingArrayOperationTarget(Node* node);
	Type* addEvolvingArrayElementType(Type* evolvingArrayType, Node* node);
	Type* finalizeEvolvingArrayType(Type* t);
	Type* getFinalArrayType(EvolvingArrayType* t);
	Type* createFinalArrayType(Type* elementType);
	void reportFlowControlError(Node* node);

	CacheKey getFlowReferenceKey(FlowState* f);
	std::pair<std::string, bool> getAccessedPropertyName(Node* access);
	std::pair<std::string, bool> tryGetElementAccessExpressionName(
		ElementAccessExpression* node);
	std::pair<std::string, bool> tryGetNameFromEntityNameExpression(Node* node);
	std::pair<std::string, bool> getLiteralPropertyNameText(Node* name);
	bool containsMatchingReference(Node* source, Node* target);
	bool optionalChainContainsReference(Node* source, Node* target);
	Node* getReferenceCandidate(Node* node);
	Node* getReferenceRoot(Node* node);
	bool hasMatchingArgument(Node* expression, Node* reference);
	bool isOrContainsMatchingReference(Node* source, Node* target);
	Type* replacePrimitivesWithLiterals(Type* typeWithPrimitives,
										Type* typeWithLiterals);
	bool isExhaustiveSwitchStatement(Node* node);
	bool computeExhaustiveSwitchStatement(Node* node);
	bool eachTypeContainedIn(Type* source, const std::vector<Type*>& types);
	std::optional<std::vector<std::string>> getSwitchClauseTypeOfWitnesses(Node* node);
	TypeFacts getNotEqualFactsFromTypeofSwitch(int start, int end,
											 const std::vector<std::string>& witnesses);
	std::vector<Type*> getSwitchClauseTypes(Node* node);
	Type* getTypeOfSwitchClause(Node* clause);
	Signature* getEffectsSignature(Node* node);

	std::string getPropertyNameForKnownSymbolName(const std::string& symbolName);
	Type* getTypeOfDottedName(Node* node, Diagnostic* diagnostic);
	Type* getExplicitTypeOfSymbol(Symbol* symbol, Diagnostic* diagnostic);
	bool isDeclarationWithExplicitTypeAnnotation(Node* node);
	bool isExpandoPropertyFunctionWithReturnTypeAnnotation(Node* node);
	bool hasTypePredicateOrNeverReturnType(Signature* sig);
	Type* getExplicitThisType(Node* node);
	Type* getInitialType(Node* node);
	Type* getInitialTypeOfVariableDeclaration(Node* node);

	Type* getInitialTypeOfBindingElement(Node* node);
	Type* getAssignedType(Node* node);
	Type* getAssignedTypeOfBinaryExpression(Node* node);
	Type* getAssignedTypeOfArrayLiteralElement(Node* node, Node* element);
	Type* getTypeOfDestructuredArrayElement(Type* t, int index);
	Type* includeUndefinedInIndexSignature(Type* t);
	Type* getAssignedTypeOfSpreadExpression(Node* node);
	Type* getTypeOfDestructuredSpreadExpression(Type* t);
	Type* getAssignedTypeOfPropertyAssignment(Node* node);
	Type* getTypeOfDestructuredProperty(Type* t, Node* name);
	Type* getAssignedTypeOfShorthandPropertyAssignment(Node* node);
	bool isDestructuringAssignmentTarget(Node* parent);
	Type* getTypeWithDefault(Type* t, Node* defaultExpression);
	Type* getAssignmentReducedType(Type* declaredType, Type* assignedType);
	Type* getAssignmentReducedTypeWorker(Type* declaredType, Type* assignedType);
	bool typeMaybeAssignableTo(Type* source, Type* target);
	Node* getTypePredicateArgument(TypePredicate* predicate, Node* callExpression);


	bool isReachableFlowNodeWorker(FlowState* f, FlowNode* flow, bool noCacheCheck);
	bool isFalseExpression(Node* expr);
	bool isPostSuperFlowNode(FlowNode* flow, bool noCacheCheck);
	bool isPostSuperFlowNodeWorker(FlowState* f, FlowNode* flow, bool noCacheCheck);
	bool isSymbolAssignedDefinitely(Symbol* symbol);
	bool isPastLastAssignment(Symbol* symbol, Node* location);
	void ensureAssignmentsMarked(Symbol* symbol);
	bool hasParentWithAssignmentsMarked(Node* node);
	int32_t extendAssignmentPosition(Node* node, Node* declaration);

	// flow.go dep stubs — defined TSC_UNREACHABLE at the bottom of checker_flow.cpp.


	bool areTypesComparable(Type* type1, Type* type2);

	Type* getFlowTypeOfProperty(Node* reference, Symbol* prop);
	bool isTypeSubsetOf(Type* source, Type* target);
};  // class Checker

// Free helpers used across checker translation units.
Diagnostic* NewDiagnosticForNode(Node* node, const DiagnosticMessage* message,
								 const std::vector<std::string>& args);
Type* getNonDistributedTypeParameter(Type* t);
bool isThisTypeParameter(Type* t);
void clearCachedInferences(std::vector<InferenceInfo*>& inferences);

// === slice: tracer ===
Tracer* newTracer(tsc::tracing::Tracing* tr, int checkerIndex);
std::vector<std::string> FormatTypeFlags(TypeFlags flags); // types.go:556

// === slice: jsdoc ===
std::vector<Node*> getAllJSDocTags(Node* node);
std::string entityNameToString(Node* name); // utilities.go — defined in checker.cpp
bool isTypeReferenceIdentifier(Node* node);              // utilities.go:161
bool isInRightSideOfImportOrExportAssignment(Node* node); // utilities.go:1150
bool nodeStartsNewLexicalEnvironment(Node* node);         // utilities.go:1763
bool isJsxIntrinsicTagName(Node* tagName);              // utilities.go:1159
Node* isImportTypeQualifierPart(Node* node);           // utilities.go:1174
bool isInNameOfExpressionWithTypeArgumentsOrHeritageTypeReference(Node* node);  // utilities.go:1188
Node* getContainingObjectLiteral(Node* f);              // utilities.go:1163
bool isTypeAny(Type* t);                              // utilities.go

// === slice: nodecopy ===
// NodeBuilder machinery shared by nodecopy.go + the future nodebuilderimpl
// slice (types from nodebuilderimpl.go, module/types.go, collections/cow.go).

// CopyOnWriteMap (collections/cow.go): shares its backing store with a parent
// scope and clones on first mutation. EnterScope returns a restore closure.
template <class K, class V>
struct CopyOnWriteMap {
	std::shared_ptr<std::unordered_map<K, V>> m =
		std::make_shared<std::unordered_map<K, V>>();
	bool owned = true;

	std::pair<V, bool> Get(const K& k) const {
		auto it = m->find(k);
		if (it == m->end()) {
			return {V{}, false};
		}
		return {it->second, true};
	}

	void Set(const K& k, const V& v) {
		ensureOwned();
		(*m)[k] = v;
	}
	void ensureOwned() {
		if (owned) {
			return;
		}
		m = std::make_shared<std::unordered_map<K, V>>(*m);
		owned = true;
	}
	std::function<void()> EnterScope() {
		CopyOnWriteMap saved = *this;
		owned = false;
		return [this, saved]() { *this = saved; };
	}
};

template <class K>
struct CopyOnWriteSet {
	CopyOnWriteMap<K, char> m;

	bool Has(const K& k) const { return m.Has(k); }
	void Add(const K& k) { m.Set(k, 0); }
	std::function<void()> EnterScope() { return m.EnterScope(); }
};

// moduleSpecifierResult (nodebuilderimpl.go).
struct moduleSpecifierResult {
	std::string specifier;
	Type* importAttributesType = nullptr;
};

// CompositeSymbolIdentity (nodebuilderimpl.go).
struct CompositeSymbolIdentity {
	bool isConstructorNode = false;
	SymbolId symbolId = 0;
	NodeId nodeId = 0;
	bool operator==(const CompositeSymbolIdentity&) const = default;
};

struct CompositeSymbolIdentityHash {
	size_t operator()(const CompositeSymbolIdentity& k) const {
		size_t h = static_cast<size_t>(k.isConstructorNode);
		h = h * 0x9E3779B97F4A7C15ull ^ static_cast<size_t>(k.symbolId);
		return h * 0x9E3779B97F4A7C15ull ^ static_cast<size_t>(k.nodeId);
	}
};

// TrackedSymbolArgs (nodebuilderimpl.go).
struct TrackedSymbolArgs {
	Symbol* symbol = nullptr;
	Node* enclosingDeclaration = nullptr;
	SymbolFlags meaning = 0;
};

// SerializedTypeEntry (nodebuilderimpl.go).
struct SerializedTypeEntry {
	Node* node = nullptr;
	bool truncating = false;
	int addedLength = 0;
	std::vector<TrackedSymbolArgs*> trackedSymbols;
};

// CompositeTypeCacheIdentity (nodebuilderimpl.go).
struct CompositeTypeCacheIdentity {
	TypeId typeId = 0;
	nodebuilder::Flags flags = 0;
	nodebuilder::InternalFlags internalFlags = 0;
	bool operator==(const CompositeTypeCacheIdentity&) const = default;
};

struct CompositeTypeCacheIdentityHash {
	size_t operator()(const CompositeTypeCacheIdentity& k) const {
		size_t h = static_cast<size_t>(k.typeId);
		h = h * 0x9E3779B97F4A7C15ull ^ static_cast<size_t>(k.flags);
		return h * 0x9E3779B97F4A7C15ull ^ static_cast<size_t>(k.internalFlags);
	}
};

// NodeBuilderLinks (nodebuilderimpl.go).
struct NodeBuilderLinks {
	// Collection of types serialized at this location
	std::unordered_map<CompositeTypeCacheIdentity, SerializedTypeEntry*,
	                   CompositeTypeCacheIdentityHash>
		serializedTypes;
	// If present, this is a fake scope injected into an enclosing declaration
	// chain.
	std::optional<std::string> fakeScopeForSignatureDeclaration;
};

// ModeAwareCacheKey (module/types.go).
struct ModeAwareCacheKey {
	std::string Name;
	ResolutionMode Mode = ResolutionModeNone;
	bool operator==(const ModeAwareCacheKey&) const = default;
};

struct ModeAwareCacheKeyHash {
	size_t operator()(const ModeAwareCacheKey& k) const {
		size_t h = std::hash<std::string>()(k.Name);
		return h * 0x9E3779B97F4A7C15ull ^ static_cast<size_t>(k.Mode);
	}
};

// ModeAwareCache (module/cache.go): map[ModeAwareCacheKey]T.
template <class T>
using ModeAwareCache =
	std::unordered_map<ModeAwareCacheKey, T, ModeAwareCacheKeyHash>;

// NodeBuilderSymbolLinks (nodebuilderimpl.go).
struct NodeBuilderSymbolLinks {
	ModeAwareCache<moduleSpecifierResult> specifierCache;
};

// NodeBuilderContext (nodebuilderimpl.go).
struct NodeBuilderContext {
	void* host = nullptr; // modulespecifiers Host — not ported yet
	nodebuilder::SymbolTracker* tracker = nullptr;
	int approximateLength = 0;
	int maxTruncationLength = 0;
	bool encounteredError = false;
	bool truncating = false;
	bool reportedDiagnostic = false;
	nodebuilder::Flags flags = 0;
	nodebuilder::InternalFlags internalFlags = 0;
	int depth = 0;
	int maxExpansionDepth = 0; // -1 means no expansion, 0+ = verbosity levels
	std::vector<Type*> typeStack;
	bool canIncreaseExpansionDepth = false;
	bool expansionTruncated = false;
	Node* enclosingDeclaration = nullptr;
	SourceFile* enclosingFile = nullptr;
	std::vector<Type*> inferTypeParameters;
	std::unordered_set<TypeId> visitedTypes;
	std::unordered_map<CompositeSymbolIdentity, int,
	                   CompositeSymbolIdentityHash>
		symbolDepth;
	std::vector<TrackedSymbolArgs*> trackedSymbols;
	TypeMapper* mapper = nullptr;
	std::vector<Symbol*> reverseMappedStack;
	std::unordered_map<SymbolId, Type*> enclosingSymbolTypes;
	bool suppressReportInferenceFallback = false;
	std::unordered_map<SymbolId, Symbol*> remappedSymbolReferences;

	// per signature scope state
	CopyOnWriteMap<TypeId, Node*> typeParameterNames;
	CopyOnWriteSet<std::string> typeParameterNamesByText;
	CopyOnWriteMap<std::string, int> typeParameterNamesByTextNextNameCount;
	CopyOnWriteSet<SymbolId> typeParameterSymbolList;
};

// propertyNameNodeKind (nodebuilderimpl.go).
enum class propertyNameNodeKind : int32_t {
	Identifier = 0,
	NumericLiteral = 1,
	StringLiteral = 2,
};

struct NodeBuilderImpl;
struct recoveryBoundary;
struct SymbolTrackerImpl;

// NodeBuilderImpl (nodebuilderimpl.go) — the node builder facade.
struct NodeBuilderImpl {
	// host members
	NodeFactory* f = nullptr;
	Checker* ch = nullptr;
	printer::EmitContext* e = nullptr;
	void* pc = nullptr; // *pseudochecker.PseudoChecker — not ported

	// cache
	Arena linksArena;
	Arena symbolLinksArena;
	LinkStore<Node*, NodeBuilderLinks> links{&linksArena};
	LinkStore<Symbol*, NodeBuilderSymbolLinks> symbolLinks{&symbolLinksArena};

	// state
	NodeBuilderContext* ctx = nullptr;

	// reusable visitor
	NodeVisitor* cloneBindingNameVisitor = nullptr;

	// symbols for synthesized identifiers, needed for e.g. inlay hints
	std::unordered_map<Node*, Symbol*> idToSymbol;

	// --- nodecopy.go ---
	Node* reuseNode(Node* node);
	Node* tryJSTypeNodeToTypeNode(Node* node);
	Node* reuseName(Node* node, bool isMethod);
	Node* reuseTypeNode(Node* node);
	void walkNodeForExpandability(Node* node);
	recoveryBoundary* createRecoveryBoundary();
	bool finalizeBoundary(recoveryBoundary* bound);
	Node* tryReuseExistingNodeHelper(Node* existing);
	std::string getModuleSpecifierOverride(Node* parent, Node* lit);
	Node* rewriteModuleSpecifier(Node* parent, Node* lit);
	Node* getEnclosingDeclarationIgnoringFakeScope();

	// --- nodebuilderimpl.go deps used by nodecopy.go (ported for real where
	// their own deps already exist, otherwise stubbed) ---
	Node* setTextRange(Node* range_, Node* location);
	Node* newIdentifier(const std::string& text, Symbol* symbol);
	Type* getTypeFromTypeNode(Node* node, bool noMappedTypes);
	Node* typeToTypeNode(Type* t);
	void checkTypeExpandability(Type* t);
	std::function<void()> enterNewScope(Node* declaration,
	                                  std::vector<Symbol*> expandedParams,
	                                  std::vector<Type*> typeParameters,
	                                  std::vector<Symbol*> originalParameters,
	                                  TypeMapper* mapper);
	Node* serializeTypeName(Node* node, bool isTypeOf, NodeList* typeArguments);
	bool canReuseExistingJSTypeNode(Node* existing, Type* t);
	Symbol* tryGetResolvedSymbolFromTypeNode(Node* node);
	std::vector<Symbol*> lookupSymbolChain(Symbol* symbol, SymbolFlags meaning,
	                                     bool yieldModuleSymbol);
	moduleSpecifierResult getSpecifierForModuleSymbol(
		Symbol* symbol, ResolutionMode overrideImportMode);
	Node* typeParameterToName(Type* typeParameter);
};

// originalRecoveryScopeState (nodecopy.go).
struct originalRecoveryScopeState {
	int trackedSymbolsTop = 0;
	int unreportedErrorsTop = 0;
	bool hadError = false;
};

// recoveryBoundary (nodecopy.go).
struct recoveryBoundary {
	NodeBuilderContext* ctx = nullptr;
	bool hadError = false;
	std::vector<std::function<void()>> deferredReports;
	nodebuilder::SymbolTracker* oldTracker = nullptr;
	std::vector<TrackedSymbolArgs*> oldTrackedSymbols;
	std::vector<TrackedSymbolArgs*> trackedSymbols;
	bool oldEncounteredError = false;
	int oldApproximateLength = 0;

	void markError(std::function<void()> f);
	originalRecoveryScopeState startRecoveryScope();
	void endRecoveryScope(originalRecoveryScopeState state);
};

// wrappingTracker (nodecopy.go) — defers diagnostic reports until the bound
// recovery boundary is finalized.
struct wrappingTracker : nodebuilder::SymbolTracker {
	nodebuilder::SymbolTracker* wrapped = nullptr;
	recoveryBoundary* bound = nullptr;

	void PopErrorFallbackNode() override;
	void PushErrorFallbackNode(Node* node) override;
	void ReportCyclicStructureError() override;
	void ReportInaccessibleThisError() override;
	void ReportInaccessibleUniqueSymbolError() override;
	void ReportInferenceFallback(Node* node) override;
	void ReportLikelyUnsafeImportRequiredError(
		const std::string& specifier, const std::string& symbolName) override;
	void ReportNonSerializableProperty(const std::string& propertyName) override;
	void ReportNonlocalAugmentation(SourceFile* containingFile,
	                                Symbol* parentSymbol,
	                                Symbol* augmentingSymbol) override;
	void ReportPrivateInBaseOfClassExpression(
		const std::string& propertyName) override;
	void ReportTruncationError() override;
	bool TrackSymbol(Symbol* symbol, Node* enclosingDeclaration,
	                 SymbolFlags meaning) override;
};

// SymbolTrackerImpl (symboltracker.go).
struct SymbolTrackerImpl : nodebuilder::SymbolTracker {
	NodeBuilderContext* context = nullptr;
	nodebuilder::SymbolTracker* inner = nullptr;
	bool DisableTrackSymbol = false;

	bool TrackSymbol(Symbol* symbol, Node* enclosingDeclaration,
	                 SymbolFlags meaning) override;
	void ReportInaccessibleThisError() override;
	void ReportPrivateInBaseOfClassExpression(
		const std::string& propertyName) override;
	void ReportInaccessibleUniqueSymbolError() override;
	void ReportCyclicStructureError() override;
	void ReportLikelyUnsafeImportRequiredError(
		const std::string& specifier, const std::string& symbolName) override;
	void ReportTruncationError() override;
	void ReportNonlocalAugmentation(SourceFile* containingFile,
	                                Symbol* parentSymbol,
	                                Symbol* augmentingSymbol) override;
	void ReportNonSerializableProperty(
		const std::string& propertyName) override;
	void ReportInferenceFallback(Node* node) override;
	void PushErrorFallbackNode(Node* node) override;
	void PopErrorFallbackNode() override;

	void onDiagnosticReported() { context->reportedDiagnostic = true; }
};

wrappingTracker* newWrappingTracker(nodebuilder::SymbolTracker* inner,
                                    recoveryBoundary* bound);
SymbolTrackerImpl* newSymbolTrackerImpl(
	NodeBuilderContext* context, nodebuilder::SymbolTracker* tracker);
NodeVisitor* getExistingNodeTreeVisitor(NodeBuilderImpl* b,
                                      recoveryBoundary* bound);

// property name classification (nodebuilderimpl.go) + emitresolver helpers.
propertyNameNodeKind classifyPropertyName(const std::string& name,
                                        bool stringNamed, bool isMethod);
bool isNumericLiteralName(const std::string& name);
bool isExternalModuleSymbol(Symbol* moduleSymbol);
SymbolFlags getMeaningOfEntityNameReference(Node* entityName);

// utilities.go free functions — canonical definitions in checker_utilities.cpp.
TextRange rangeOfTypeParameters(SourceFile* sourceFile, NodeList* typeParameters);
std::string tryGetPropertyAccessOrIdentifierToString(Node* expr);
bool allDeclarationsInSameSourceFile(Symbol* symbol);
Node* getAnyImportSyntax(Node* node);
bool isReservedMemberName(const std::string& name);
bool introducesArgumentsExoticObject(Node* node);
std::vector<Symbol*> symbolsToArray(const SymbolTable& symbols);
Symbol* SkipAlias(Symbol* symbol, Checker* checker);
std::string ValueToString(
	const std::variant<std::monostate, std::string, Number, bool, PseudoBigInt>& value);
DiagnosticDetails CreateModuleNotFoundChain(Program* program, SourceFile* file,
	const std::string& moduleReference, ResolutionMode mode,
	const std::string& packageName);
DiagnosticDetails CreateModeMismatchDetails(Program* program, SourceFile* file);
Node* walkUpOuterExpressions(Node* node);
std::string quotedAndCommaSeparated(const std::vector<std::string>& items);
int getSortOrderFlags(Type* t);
int compareTypeNames(Type* t1, Type* t2);
Symbol* getTypeNameSymbol(Type* t);
Symbol* getObjectTypeName(Type* t);
int compareTupleTypes(TupleType* t1, TupleType* t2);
int compareElementLabels(Node* n1, Node* n2);
int compareTypeLists(const std::vector<Type*>& s1, const std::vector<Type*>& s2);
int compareTypeMappers(TypeMapper* m1, TypeMapper* m2);
bool isCompoundLikeAssignment(Node* assignment);
bool isShorthandAmbientModuleSymbol(Symbol* moduleSymbol);
bool isShorthandAmbientModule(Node* node);
bool isExponentiationOperator(Kind kind);
bool isMultiplicativeOperator(Kind kind);
bool isMultiplicativeOperatorOrHigher(Kind kind);
bool isAdditiveOperator(Kind kind);
bool isAdditiveOperatorOrHigher(Kind kind);
bool isShiftOperator(Kind kind);
bool isShiftOperatorOrHigher(Kind kind);
int CompareTypes(Type* t1, Type* t2);

} // namespace tsc::checker
