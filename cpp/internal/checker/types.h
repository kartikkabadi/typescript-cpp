// Port of tsc/internal/checker/types.go (+ links.go + key types from checker.go).
#pragma once

#include "internal/evaluator/evaluator.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/flags.h"
#include "internal/ast/nodes_generated.h"
#include "internal/ast/symbol.h"
#include "internal/core/types.h"
#include "internal/jsnum/jsnum.h"

namespace tsc::checker {

class Checker;
struct Type;
struct Signature;
struct TypeMapper;
struct TypePredicate;
struct IndexInfo;
struct TypeAlias;
struct ConditionalRoot;
struct Relation;


// ParseFlags

using ParseFlags = uint32_t;
inline constexpr ParseFlags ParseFlagsNone = 0;
inline constexpr ParseFlags ParseFlagsYield = 1 << 0;
inline constexpr ParseFlags ParseFlagsAwait = 1 << 1;
inline constexpr ParseFlags ParseFlagsType = 1 << 2;
inline constexpr ParseFlags ParseFlagsIgnoreMissingOpenBrace = 1 << 4;
inline constexpr ParseFlags ParseFlagsJSDoc = 1 << 5;

enum class SignatureKind : int32_t {
	Call,
	Construct,
};

enum class IndexKind : int32_t {
	String,
	Number,
};

enum class MemberOverrideStatus : int32_t {
	None,
	NeedsOverride,
	HasInvalidOverride,
};

using ContextFlags = uint32_t;
inline constexpr ContextFlags ContextFlagsNone = 0;
inline constexpr ContextFlags ContextFlagsSignature = 1 << 0;
inline constexpr ContextFlags ContextFlagsNoConstraints = 1 << 1;
inline constexpr ContextFlags ContextFlagsIgnoreNodeInferences = 1 << 2;
inline constexpr ContextFlags ContextFlagsSkipBindingPatterns = 1 << 3;

using TypeFormatFlags = uint32_t;
inline constexpr TypeFormatFlags TypeFormatFlagsNone = 0;
inline constexpr TypeFormatFlags TypeFormatFlagsNoTruncation = 1 << 0;
inline constexpr TypeFormatFlags TypeFormatFlagsWriteArrayAsGenericType = 1 << 1;
inline constexpr TypeFormatFlags TypeFormatFlagsGenerateNamesForShadowedTypeParams = 1 << 2;
inline constexpr TypeFormatFlags TypeFormatFlagsUseStructuralFallback = 1 << 3;
inline constexpr TypeFormatFlags TypeFormatFlagsWriteTypeArgumentsOfSignature = 1 << 5;
inline constexpr TypeFormatFlags TypeFormatFlagsUseFullyQualifiedType = 1 << 6;
inline constexpr TypeFormatFlags TypeFormatFlagsSuppressAnyReturnType = 1 << 8;
inline constexpr TypeFormatFlags TypeFormatFlagsMultilineObjectLiterals = 1 << 10;
inline constexpr TypeFormatFlags TypeFormatFlagsWriteClassExpressionAsTypeLiteral = 1 << 11;
inline constexpr TypeFormatFlags TypeFormatFlagsUseTypeOfFunction = 1 << 12;
inline constexpr TypeFormatFlags TypeFormatFlagsOmitParameterModifiers = 1 << 13;
inline constexpr TypeFormatFlags TypeFormatFlagsUseAliasDefinedOutsideCurrentScope = 1 << 14;
inline constexpr TypeFormatFlags TypeFormatFlagsUseSingleQuotesForStringLiteralType = 1 << 28;
inline constexpr TypeFormatFlags TypeFormatFlagsNoTypeReduction = 1 << 29;
inline constexpr TypeFormatFlags TypeFormatFlagsUseInstantiationExpressions = 1 << 30;
inline constexpr TypeFormatFlags TypeFormatFlagsOmitThisParameter = 1 << 25;
inline constexpr TypeFormatFlags TypeFormatFlagsWriteCallStyleSignature = 1 << 27;
inline constexpr TypeFormatFlags TypeFormatFlagsAllowUniqueESSymbolType = 1 << 20;
inline constexpr TypeFormatFlags TypeFormatFlagsAddUndefined = 1 << 17;
inline constexpr TypeFormatFlags TypeFormatFlagsWriteArrowStyleSignature = 1 << 18;
inline constexpr TypeFormatFlags TypeFormatFlagsInArrayType = 1 << 19;
inline constexpr TypeFormatFlags TypeFormatFlagsInElementType = 1 << 21;
inline constexpr TypeFormatFlags TypeFormatFlagsInFirstTypeArgument = 1 << 22;
inline constexpr TypeFormatFlags TypeFormatFlagsInTypeAlias = 1 << 23;
inline constexpr TypeFormatFlags TypeFormatFlagsNodeBuilderFlagsMask =
	TypeFormatFlagsNoTruncation | TypeFormatFlagsWriteArrayAsGenericType |
	TypeFormatFlagsGenerateNamesForShadowedTypeParams | TypeFormatFlagsUseStructuralFallback |
	TypeFormatFlagsWriteTypeArgumentsOfSignature | TypeFormatFlagsUseFullyQualifiedType |
	TypeFormatFlagsSuppressAnyReturnType | TypeFormatFlagsMultilineObjectLiterals |
	TypeFormatFlagsWriteClassExpressionAsTypeLiteral | TypeFormatFlagsUseTypeOfFunction |
	TypeFormatFlagsOmitParameterModifiers | TypeFormatFlagsUseAliasDefinedOutsideCurrentScope |
	TypeFormatFlagsAllowUniqueESSymbolType | TypeFormatFlagsInTypeAlias |
	TypeFormatFlagsUseInstantiationExpressions | TypeFormatFlagsUseSingleQuotesForStringLiteralType |
	TypeFormatFlagsNoTypeReduction | TypeFormatFlagsOmitThisParameter;

using SymbolFormatFlags = uint32_t;
inline constexpr SymbolFormatFlags SymbolFormatFlagsNone = 0;
inline constexpr SymbolFormatFlags SymbolFormatFlagsWriteTypeParametersOrArguments = 1 << 0;
inline constexpr SymbolFormatFlags SymbolFormatFlagsUseOnlyExternalAliasing = 1 << 1;
inline constexpr SymbolFormatFlags SymbolFormatFlagsAllowAnyNodeKind = 1 << 2;
inline constexpr SymbolFormatFlags SymbolFormatFlagsUseAliasDefinedOutsideCurrentScope = 1 << 3;
inline constexpr SymbolFormatFlags SymbolFormatFlagsWriteComputedProps = 1 << 4;
inline constexpr SymbolFormatFlags SymbolFormatFlagsDoNotIncludeSymbolChain = 1 << 5;

using ExternalEmitHelpers = uint32_t;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersRest = 1 << 0;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersDecorate = 1 << 1;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersMetadata = 1 << 2;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersParam = 1 << 3;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersAwaiter = 1 << 4;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersAwait = 1 << 5;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersAsyncGenerator = 1 << 6;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersAsyncDelegator = 1 << 7;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersAsyncValues = 1 << 8;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersExportStar = 1 << 9;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersImportStar = 1 << 10;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersImportDefault = 1 << 11;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersMakeTemplateObject = 1 << 12;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersClassPrivateFieldGet = 1 << 13;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersClassPrivateFieldSet = 1 << 14;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersClassPrivateFieldIn = 1 << 15;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersSetFunctionName = 1 << 16;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersPropKey = 1 << 17;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersAddDisposableResourceAndDisposeResources = 1 << 18;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersRewriteRelativeImportExtension = 1 << 19;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersESDecorateAndRunInitializers = ExternalEmitHelpersDecorate;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersFirstEmitHelper = ExternalEmitHelpersRest;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersLastEmitHelper = ExternalEmitHelpersRewriteRelativeImportExtension;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersForAwaitOfIncludes = ExternalEmitHelpersAsyncValues;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersAsyncGeneratorIncludes =
	ExternalEmitHelpersAwait | ExternalEmitHelpersAsyncGenerator;
inline constexpr ExternalEmitHelpers ExternalEmitHelpersAsyncDelegatorIncludes =
	ExternalEmitHelpersAwait | ExternalEmitHelpersAsyncDelegator | ExternalEmitHelpersAsyncValues;

inline constexpr const char* externalHelpersModuleNameText = "tslib";

// Ids

using TypeId = uint32_t;
using SignatureId = uint32_t;

// Links for referenced symbols

struct SymbolReferenceLinks {
	SymbolFlags referenceKinds{}; // Flags for the meanings of the symbol that were referenced
};

// Links for value symbols

struct ValueSymbolLinks {
	Type* resolvedType{}; // Type of value symbol
	Type* writeType{};
	Symbol* target{};
	TypeMapper* mapper{};
	Type* nameType{};
	Type* containingType{}; // Mapped type for mapped type property, containing union or intersection type for synthetic property
	bool functionOrConstructorChecked{};
};

// Additional links for mapped symbols

struct MappedSymbolLinks {
	Type* keyType{};         // Key type for mapped type member
	Symbol* syntheticOrigin{}; // For a property on a mapped or spread type, points back to the original property
};

// Additional links for deferred type symbols

struct DeferredSymbolLinks {
	Type* parent{};                 // Source union/intersection of a deferred type
	std::vector<Type*> constituents; // Calculated list of constituents for a deferred type
	std::vector<Type*> writeConstituents; // Constituents of a deferred `writeType`
};

// Links for alias symbols

struct AliasSymbolLinks {
	Symbol* immediateTarget{}; // Immediate target of an alias. May be another alias.
	Symbol* aliasTarget{};     // Resolved (non-alias) target of an alias
	bool referenced{};         // True if alias symbol has been referenced as a value that can be emitted
	Node* typeOnlyDeclaration{}; // First resolved alias declaration that makes the symbol only usable in type constructs
};

// Links for module symbols

struct ModuleSymbolLinks {
	SymbolTable resolvedExports;                            // Resolved exports of module or combined early- and late-bound static members of a class.
	std::unordered_map<std::string, Node*> typeOnlyExportStarMap; // Set on a module symbol when some of its exports were resolved through a 'export type * from "mod"' declaration
	bool exportsChecked{};
};

struct ReverseMappedSymbolLinks {
	Type* propertyType{};
	Type* mappedType{};     // References a mapped type
	Type* constraintType{}; // References an index type
};

// Links for late-bound symbols

struct LateBoundLinks {
	Symbol* lateSymbol{};
};

// Links for export type symbols

struct ExportTypeLinks {
	Symbol* target{};            // Target symbol
	Node* originatingImport{};   // Import declaration which produced the symbol
};

// Links for type aliases

// Go keys caches by xxh3 128-bit digests of key content; semantically these are
// just content-keyed maps, so C++ keys on the raw content words instead.
struct CacheKey {
	std::vector<uint64_t> w; // key words: type ids, symbol ids, flags, counts
	bool operator==(const CacheKey&) const = default;

	// === slice: decltypes ===
	// CacheHashKey.IsZero — checker.go:17699
	bool IsZero() const { return w.empty(); }
};
struct CacheKeyHash {
	size_t operator()(const CacheKey& k) const noexcept {
		uint64_t h = 1469598103934665603ull;
		for (uint64_t x : k.w) {
			h ^= x;
			h *= 1099511628211ull;
			h ^= h >> 33;
		}
		return static_cast<size_t>(h);
	}
};
template <class V>
using CacheMap = std::unordered_map<CacheKey, V, CacheKeyHash>;

struct TypeAliasLinks {
	Type* declaredType{};
	std::vector<Type*> typeParameters;              // Type parameters of type alias (empty if non-generic)
	CacheMap<Type*> instantiations;                 // Instantiations of generic type alias
	bool isConstructorDeclaredProperty{};
};

// Links for declared types (type parameters, class types, interface types, enums)

struct DeclaredTypeLinks {
	Type* declaredType{};
	bool interfaceChecked{};
	bool indexSignaturesChecked{};
	bool typeParametersChecked{};
	bool enumChecked{};
};

// Links for switch clauses

enum class ExhaustiveState : uint8_t {
	Unknown,   // Exhaustive state not computed
	Computing, // Exhaustive state computation in progress
	False,     // Switch statement is not exhaustive
	True,      // Switch statement is exhaustive
};

struct SwitchStatementLinks {
	ExhaustiveState exhaustiveState{}; // Switch statement exhaustiveness
	bool switchTypesComputed{};
	bool witnessesComputed{};
	bool witnessesAreNil{}; // Go: links.witnesses == nil (case exprs not all string literals)
	std::vector<Type*> switchTypes;
	std::vector<std::string> witnesses;
};

struct ArrayLiteralLinks {
	bool indicesComputed{};
	int32_t firstSpreadIndex{-1}; // Index of first spread expression (or -1 if none)
	int32_t lastSpreadIndex{-1};  // Index of last spread expression (or -1 if none)
};

// Links for late-binding containers

enum MembersOrExportsResolutionKind : int32_t {
	MembersOrExportsResolutionKindResolvedExports = 0,
	MembersOrExportsResolutionKindResolvedMembers = 1,
};

using MembersAndExportsLinks = std::array<SymbolTable, 2>; // Indexed by MembersOrExportsResolutionKind

// Links for synthetic spread properties

struct SpreadLinks {
	Symbol* leftSpread{};  // Left source for synthetic spread property
	Symbol* rightSpread{}; // Right source for synthetic spread property
};

// Links for variances of type aliases and interface types

using VarianceFlags = uint32_t;
inline constexpr VarianceFlags VarianceFlagsInvariant = 0;
inline constexpr VarianceFlags VarianceFlagsCovariant = 1 << 0;
inline constexpr VarianceFlags VarianceFlagsContravariant = 1 << 1;
inline constexpr VarianceFlags VarianceFlagsBivariant = VarianceFlagsCovariant | VarianceFlagsContravariant;
inline constexpr VarianceFlags VarianceFlagsIndependent = 1 << 2;
inline constexpr VarianceFlags VarianceFlagsVarianceMask =
	VarianceFlagsInvariant | VarianceFlagsCovariant | VarianceFlagsContravariant | VarianceFlagsIndependent;
inline constexpr VarianceFlags VarianceFlagsUnmeasurable = 1 << 3;
inline constexpr VarianceFlags VarianceFlagsUnreliable = 1 << 4;
inline constexpr VarianceFlags VarianceFlagsAllowsStructuralFallback =
	VarianceFlagsUnmeasurable | VarianceFlagsUnreliable;

struct VarianceLinks {
	std::vector<VarianceFlags> variances;
};

struct MarkedAssignmentSymbolLinks {
	int32_t lastAssignmentPos{};
	bool hasDefiniteAssignment{}; // Symbol is definitely assigned somewhere
};

struct AccessibleChainCacheKey {
	bool useOnlyExternalAliasing{};
	Node* location{};
	SymbolFlags meaning{};
	bool operator==(const AccessibleChainCacheKey&) const = default;
};
struct AccessibleChainCacheKeyHash {
	size_t operator()(const AccessibleChainCacheKey& k) const noexcept {
		uint64_t h = reinterpret_cast<uintptr_t>(k.location);
		h = h * 1099511628211ull ^ (k.useOnlyExternalAliasing ? 1 : 0);
		h = h * 1099511628211ull ^ static_cast<uint64_t>(k.meaning);
		return static_cast<size_t>(h);
	}
};

struct ContainingSymbolLinks {
	std::unordered_map<NodeId, std::vector<Symbol*>> extendedContainersByFile;
	std::vector<Symbol*>* extendedContainers{};
	std::unordered_map<AccessibleChainCacheKey, std::vector<Symbol*>, AccessibleChainCacheKeyHash> accessibleChainCache;
};

using AccessFlags = uint32_t;
inline constexpr AccessFlags AccessFlagsNone = 0;
inline constexpr AccessFlags AccessFlagsIncludeUndefined = 1 << 0;
inline constexpr AccessFlags AccessFlagsNoIndexSignatures = 1 << 1;
inline constexpr AccessFlags AccessFlagsWriting = 1 << 2;
inline constexpr AccessFlags AccessFlagsCacheSymbol = 1 << 3;
inline constexpr AccessFlags AccessFlagsAllowMissing = 1 << 4;
inline constexpr AccessFlags AccessFlagsExpressionPosition = 1 << 5;
inline constexpr AccessFlags AccessFlagsReportDeprecated = 1 << 6;
inline constexpr AccessFlags AccessFlagsSuppressNoImplicitAnyError = 1 << 7;
inline constexpr AccessFlags AccessFlagsContextual = 1 << 8;
inline constexpr AccessFlags AccessFlagsPersistent = AccessFlagsIncludeUndefined;

using NodeCheckFlags = uint32_t;
inline constexpr NodeCheckFlags NodeCheckFlagsNone = 0;
inline constexpr NodeCheckFlags NodeCheckFlagsTypeChecked = 1 << 0;
inline constexpr NodeCheckFlags NodeCheckFlagsContextChecked = 1 << 6;
inline constexpr NodeCheckFlags NodeCheckFlagsEnumValuesComputed = 1 << 10;
inline constexpr NodeCheckFlags NodeCheckFlagsAssignmentsMarked = 1 << 17;
inline constexpr NodeCheckFlags NodeCheckFlagsContainsClassWithPrivateIdentifiers = 1 << 20;
inline constexpr NodeCheckFlags NodeCheckFlagsContainsSuperPropertyInStaticInitializer = 1 << 21;
inline constexpr NodeCheckFlags NodeCheckFlagsInCheckIdentifier = 1 << 22;
inline constexpr NodeCheckFlags NodeCheckFlagsInitializerIsUndefined = 1 << 24;
inline constexpr NodeCheckFlags NodeCheckFlagsInitializerIsUndefinedComputed = 1 << 25;

// Common links

struct NodeLinks {
	NodeCheckFlags flags{};
	Tristate declarationRequiresScopeChange{Tristate::Unknown};
	bool hasReportedStatementInAmbientContext{};
};

struct SymbolNodeLinks {
	Symbol* resolvedSymbol{}; // Resolved symbol associated with node
};

struct TypeNodeLinks {
	Type* resolvedType{};             // Resolved type associated with node
	std::vector<Type*> outerTypeParameters; // Outer type parameters of anonymous object type
};

struct ComputedNameNodeLinks {
	bool* hasName{};     // If the node has a computable name
	std::string name;    // Resolved name associated with the type of the node
};

// Links for enum members

struct EnumMemberLinks {
	EvalResult value; // Constant value of enum member
};

// Links for assertion expressions

struct AssertionLinks {
	Type* exprType{}; // Assertion expression type
};

// SourceFile links

struct SourceFileLinks {
	bool typeChecked{};
	bool unusedChecked{};
	Symbol* externalHelpersModule{};
	ExternalEmitHelpers requestedExternalEmitHelpers{};
	std::vector<Node*> deferredNodes;
	std::unordered_set<Node*> deferredNodesSet;
	std::vector<Node*> identifierCheckNodes;
	std::string localJsxNamespace;
	std::string localJsxFragmentNamespace;
	Node* localJsxFactory;
	Node* localJsxFragmentFactory;
	Type* jsxFragmentType{};
};

// Signature specific links

struct SignatureLinks {
	Signature* resolvedSignature{};
	Signature* effectsSignature{};
	Signature* decoratorSignature{};
};

// JsxElementLinks (from jsx.go)

struct JsxElementLinks {
	uint32_t jsxFlags{};
	Type* resolvedJsxElementAttributesType{};
	Symbol* jsxNamespace{};
	Symbol* jsxImplicitImportContainer{};
	Node* firstJSXTagInFile{};
};

// TypeFlags

using TypeFlags = uint32_t;
inline constexpr TypeFlags TypeFlagsNone = 0;
inline constexpr TypeFlags TypeFlagsAny = 1 << 0;
inline constexpr TypeFlags TypeFlagsUnknown = 1 << 1;
inline constexpr TypeFlags TypeFlagsUndefined = 1 << 2;
inline constexpr TypeFlags TypeFlagsNull = 1 << 3;
inline constexpr TypeFlags TypeFlagsVoid = 1 << 4;
inline constexpr TypeFlags TypeFlagsString = 1 << 5;
inline constexpr TypeFlags TypeFlagsNumber = 1 << 6;
inline constexpr TypeFlags TypeFlagsBigInt = 1 << 7;
inline constexpr TypeFlags TypeFlagsBoolean = 1 << 8;
inline constexpr TypeFlags TypeFlagsESSymbol = 1 << 9;
inline constexpr TypeFlags TypeFlagsStringLiteral = 1 << 10;
inline constexpr TypeFlags TypeFlagsNumberLiteral = 1 << 11;
inline constexpr TypeFlags TypeFlagsBooleanLiteral = 1 << 12;
inline constexpr TypeFlags TypeFlagsEnumLiteral = 1 << 13;
inline constexpr TypeFlags TypeFlagsBigIntLiteral = 1 << 14;
inline constexpr TypeFlags TypeFlagsUniqueESSymbol = 1 << 15;
inline constexpr TypeFlags TypeFlagsEnum = 1 << 16;
inline constexpr TypeFlags TypeFlagsNonPrimitive = 1 << 17;
inline constexpr TypeFlags TypeFlagsNever = 1 << 18;
inline constexpr TypeFlags TypeFlagsTypeParameter = 1 << 19;
inline constexpr TypeFlags TypeFlagsObject = 1 << 20;
inline constexpr TypeFlags TypeFlagsIndex = 1 << 21;
inline constexpr TypeFlags TypeFlagsTemplateLiteral = 1 << 22;
inline constexpr TypeFlags TypeFlagsStringMapping = 1 << 23;
inline constexpr TypeFlags TypeFlagsSubstitution = 1 << 24;
inline constexpr TypeFlags TypeFlagsIndexedAccess = 1 << 25;
inline constexpr TypeFlags TypeFlagsConditional = 1 << 26;
inline constexpr TypeFlags TypeFlagsUnion = 1 << 27;
inline constexpr TypeFlags TypeFlagsIntersection = 1 << 28;
inline constexpr TypeFlags TypeFlagsReserved1 = 1 << 29;
inline constexpr TypeFlags TypeFlagsReserved2 = 1 << 30;
inline constexpr TypeFlags TypeFlagsReserved3 = 1u << 31;

inline constexpr TypeFlags TypeFlagsAnyOrUnknown = TypeFlagsAny | TypeFlagsUnknown;
inline constexpr TypeFlags TypeFlagsNullable = TypeFlagsUndefined | TypeFlagsNull;
inline constexpr TypeFlags TypeFlagsLiteral =
	TypeFlagsStringLiteral | TypeFlagsNumberLiteral | TypeFlagsBigIntLiteral | TypeFlagsBooleanLiteral;
inline constexpr TypeFlags TypeFlagsUnit =
	TypeFlagsEnum | TypeFlagsLiteral | TypeFlagsUniqueESSymbol | TypeFlagsNullable;
inline constexpr TypeFlags TypeFlagsFreshable = TypeFlagsEnum | TypeFlagsLiteral;
inline constexpr TypeFlags TypeFlagsStringOrNumberLiteral =
	TypeFlagsStringLiteral | TypeFlagsNumberLiteral;
inline constexpr TypeFlags TypeFlagsStringOrNumberLiteralOrUnique =
	TypeFlagsStringLiteral | TypeFlagsNumberLiteral | TypeFlagsUniqueESSymbol;
inline constexpr TypeFlags TypeFlagsDefinitelyFalsy =
	TypeFlagsStringLiteral | TypeFlagsNumberLiteral | TypeFlagsBigIntLiteral |
	TypeFlagsBooleanLiteral | TypeFlagsVoid | TypeFlagsUndefined | TypeFlagsNull;
inline constexpr TypeFlags TypeFlagsPossiblyFalsy =
	TypeFlagsDefinitelyFalsy | TypeFlagsString | TypeFlagsNumber | TypeFlagsBigInt | TypeFlagsBoolean;
inline constexpr TypeFlags TypeFlagsIntrinsic =
	TypeFlagsAny | TypeFlagsUnknown | TypeFlagsString | TypeFlagsNumber | TypeFlagsBigInt |
	TypeFlagsESSymbol | TypeFlagsVoid | TypeFlagsUndefined | TypeFlagsNull | TypeFlagsNever |
	TypeFlagsNonPrimitive;
inline constexpr TypeFlags TypeFlagsStringLike =
	TypeFlagsString | TypeFlagsStringLiteral | TypeFlagsTemplateLiteral | TypeFlagsStringMapping;
inline constexpr TypeFlags TypeFlagsNumberLike = TypeFlagsNumber | TypeFlagsNumberLiteral | TypeFlagsEnum;
inline constexpr TypeFlags TypeFlagsBigIntLike = TypeFlagsBigInt | TypeFlagsBigIntLiteral;
inline constexpr TypeFlags TypeFlagsBooleanLike = TypeFlagsBoolean | TypeFlagsBooleanLiteral;
inline constexpr TypeFlags TypeFlagsEnumLike = TypeFlagsEnum | TypeFlagsEnumLiteral;
inline constexpr TypeFlags TypeFlagsESSymbolLike = TypeFlagsESSymbol | TypeFlagsUniqueESSymbol;
inline constexpr TypeFlags TypeFlagsVoidLike = TypeFlagsVoid | TypeFlagsUndefined;
inline constexpr TypeFlags TypeFlagsPrimitive =
	TypeFlagsStringLike | TypeFlagsNumberLike | TypeFlagsBigIntLike | TypeFlagsBooleanLike |
	TypeFlagsEnumLike | TypeFlagsESSymbolLike | TypeFlagsVoidLike | TypeFlagsNull;
inline constexpr TypeFlags TypeFlagsDefinitelyNonNullable =
	TypeFlagsStringLike | TypeFlagsNumberLike | TypeFlagsBigIntLike | TypeFlagsBooleanLike |
	TypeFlagsEnumLike | TypeFlagsESSymbolLike | TypeFlagsObject | TypeFlagsNonPrimitive;
inline constexpr TypeFlags TypeFlagsDisjointDomains =
	TypeFlagsNonPrimitive | TypeFlagsStringLike | TypeFlagsNumberLike | TypeFlagsBigIntLike |
	TypeFlagsBooleanLike | TypeFlagsESSymbolLike | TypeFlagsVoidLike | TypeFlagsNull;
inline constexpr TypeFlags TypeFlagsUnionOrIntersection = TypeFlagsUnion | TypeFlagsIntersection;
inline constexpr TypeFlags TypeFlagsStructuredType =
	TypeFlagsObject | TypeFlagsUnion | TypeFlagsIntersection;
inline constexpr TypeFlags TypeFlagsTypeVariable = TypeFlagsTypeParameter | TypeFlagsIndexedAccess;
inline constexpr TypeFlags TypeFlagsInstantiableNonPrimitive =
	TypeFlagsTypeVariable | TypeFlagsConditional | TypeFlagsSubstitution;
inline constexpr TypeFlags TypeFlagsInstantiablePrimitive =
	TypeFlagsIndex | TypeFlagsTemplateLiteral | TypeFlagsStringMapping;
inline constexpr TypeFlags TypeFlagsInstantiable =
	TypeFlagsInstantiableNonPrimitive | TypeFlagsInstantiablePrimitive;
inline constexpr TypeFlags TypeFlagsStructuredOrInstantiable =
	TypeFlagsStructuredType | TypeFlagsInstantiable;
inline constexpr TypeFlags TypeFlagsObjectFlagsType =
	TypeFlagsAny | TypeFlagsNullable | TypeFlagsNever | TypeFlagsObject | TypeFlagsUnion |
	TypeFlagsIntersection;
inline constexpr TypeFlags TypeFlagsSimplifiable =
	TypeFlagsIndexedAccess | TypeFlagsConditional | TypeFlagsIndex;
inline constexpr TypeFlags TypeFlagsSingleton =
	TypeFlagsAny | TypeFlagsUnknown | TypeFlagsString | TypeFlagsNumber | TypeFlagsBoolean |
	TypeFlagsBigInt | TypeFlagsESSymbol | TypeFlagsVoid | TypeFlagsUndefined | TypeFlagsNull |
	TypeFlagsNever | TypeFlagsNonPrimitive;
inline constexpr TypeFlags TypeFlagsNarrowable =
	TypeFlagsAny | TypeFlagsUnknown | TypeFlagsStructuredOrInstantiable | TypeFlagsStringLike |
	TypeFlagsNumberLike | TypeFlagsBigIntLike | TypeFlagsBooleanLike | TypeFlagsESSymbol |
	TypeFlagsUniqueESSymbol | TypeFlagsNonPrimitive;
inline constexpr TypeFlags TypeFlagsIncludesMask =
	TypeFlagsAny | TypeFlagsUnknown | TypeFlagsPrimitive | TypeFlagsNever | TypeFlagsObject |
	TypeFlagsUnion | TypeFlagsIntersection | TypeFlagsNonPrimitive | TypeFlagsTemplateLiteral |
	TypeFlagsStringMapping;
inline constexpr TypeFlags TypeFlagsIncludesMissingType = TypeFlagsTypeParameter;
inline constexpr TypeFlags TypeFlagsIncludesNonWideningType = TypeFlagsIndex;
inline constexpr TypeFlags TypeFlagsIncludesWildcard = TypeFlagsIndexedAccess;
inline constexpr TypeFlags TypeFlagsIncludesEmptyObject = TypeFlagsConditional;
inline constexpr TypeFlags TypeFlagsIncludesInstantiable = TypeFlagsSubstitution;
inline constexpr TypeFlags TypeFlagsIncludesConstrainedTypeVariable = TypeFlagsReserved1;
inline constexpr TypeFlags TypeFlagsIncludesError = TypeFlagsReserved2;
inline constexpr TypeFlags TypeFlagsNotPrimitiveUnion =
	TypeFlagsAny | TypeFlagsUnknown | TypeFlagsVoid | TypeFlagsNever | TypeFlagsObject |
	TypeFlagsIntersection | TypeFlagsIncludesInstantiable;

// ObjectFlags

using ObjectFlags = uint32_t;
inline constexpr ObjectFlags ObjectFlagsNone = 0;
inline constexpr ObjectFlags ObjectFlagsClass = 1 << 0;
inline constexpr ObjectFlags ObjectFlagsInterface = 1 << 1;
inline constexpr ObjectFlags ObjectFlagsReference = 1 << 2;
inline constexpr ObjectFlags ObjectFlagsTuple = 1 << 3;
inline constexpr ObjectFlags ObjectFlagsAnonymous = 1 << 4;
inline constexpr ObjectFlags ObjectFlagsMapped = 1 << 5;
inline constexpr ObjectFlags ObjectFlagsInstantiated = 1 << 6;
inline constexpr ObjectFlags ObjectFlagsObjectLiteral = 1 << 7;
inline constexpr ObjectFlags ObjectFlagsEvolvingArray = 1 << 8;
inline constexpr ObjectFlags ObjectFlagsObjectLiteralPatternWithComputedProperties = 1 << 9;
inline constexpr ObjectFlags ObjectFlagsReverseMapped = 1 << 10;
inline constexpr ObjectFlags ObjectFlagsJsxAttributes = 1 << 11;
inline constexpr ObjectFlags ObjectFlagsJSLiteral = 1 << 12;
inline constexpr ObjectFlags ObjectFlagsFreshLiteral = 1 << 13;
inline constexpr ObjectFlags ObjectFlagsArrayLiteral = 1 << 14;
inline constexpr ObjectFlags ObjectFlagsPrimitiveUnion = 1 << 15;
inline constexpr ObjectFlags ObjectFlagsContainsWideningType = 1 << 16;
inline constexpr ObjectFlags ObjectFlagsContainsObjectOrArrayLiteral = 1 << 17;
inline constexpr ObjectFlags ObjectFlagsNonInferrableType = 1 << 18;
inline constexpr ObjectFlags ObjectFlagsCouldContainTypeVariablesComputed = 1 << 19;
inline constexpr ObjectFlags ObjectFlagsCouldContainTypeVariables = 1 << 20;
inline constexpr ObjectFlags ObjectFlagsMembersResolved = 1 << 21;

inline constexpr ObjectFlags ObjectFlagsClassOrInterface = ObjectFlagsClass | ObjectFlagsInterface;
inline constexpr ObjectFlags ObjectFlagsRequiresWidening =
	ObjectFlagsContainsWideningType | ObjectFlagsContainsObjectOrArrayLiteral;
inline constexpr ObjectFlags ObjectFlagsPropagatingFlags =
	ObjectFlagsContainsWideningType | ObjectFlagsContainsObjectOrArrayLiteral | ObjectFlagsNonInferrableType;
inline constexpr ObjectFlags ObjectFlagsInstantiatedMapped = ObjectFlagsMapped | ObjectFlagsInstantiated;
inline constexpr ObjectFlags ObjectFlagsContainsSpread = 1 << 22;
inline constexpr ObjectFlags ObjectFlagsObjectRestType = 1 << 23;
inline constexpr ObjectFlags ObjectFlagsInstantiationExpressionType = 1 << 24;
inline constexpr ObjectFlags ObjectFlagsSingleSignatureType = 1 << 25;
inline constexpr ObjectFlags ObjectFlagsIsClassInstanceClone = 1 << 26;
// Flags that require TypeFlags.Object and ObjectFlags.Reference
inline constexpr ObjectFlags ObjectFlagsIdenticalBaseTypeCalculated = 1 << 27;
inline constexpr ObjectFlags ObjectFlagsIdenticalBaseTypeExists = 1 << 28;
inline constexpr ObjectFlags ObjectFlagsFromTypeNode = 1 << 29;
// Flags that require TypeFlags.UnionOrIntersection or TypeFlags.Substitution
inline constexpr ObjectFlags ObjectFlagsIsGenericTypeComputed = 1 << 22;
inline constexpr ObjectFlags ObjectFlagsIsGenericObjectType = 1 << 23;
inline constexpr ObjectFlags ObjectFlagsIsGenericIndexType = 1 << 24;
inline constexpr ObjectFlags ObjectFlagsIsGenericType =
	ObjectFlagsIsGenericObjectType | ObjectFlagsIsGenericIndexType;
// Flags that require TypeFlags.Union
inline constexpr ObjectFlags ObjectFlagsContainsIntersections = 1 << 25;
inline constexpr ObjectFlags ObjectFlagsIsUnknownLikeUnionComputed = 1 << 26;
inline constexpr ObjectFlags ObjectFlagsIsUnknownLikeUnion = 1 << 27;
inline constexpr ObjectFlags ObjectFlagsIsUniformEnumComputed = 1 << 28;
inline constexpr ObjectFlags ObjectFlagsIsUniformEnum = 1 << 29;
// Flags that require TypeFlags.Intersection
inline constexpr ObjectFlags ObjectFlagsIsNeverIntersectionComputed = 1 << 25;
inline constexpr ObjectFlags ObjectFlagsIsNeverIntersection = 1 << 26;
inline constexpr ObjectFlags ObjectFlagsIsConstrainedTypeVariable = 1 << 27;
// Object flags that uniquely identify the kind of ObjectType
inline constexpr ObjectFlags ObjectFlagsObjectTypeKindMask =
	ObjectFlagsClassOrInterface | ObjectFlagsReference | ObjectFlagsTuple | ObjectFlagsAnonymous |
	ObjectFlagsMapped | ObjectFlagsReverseMapped | ObjectFlagsEvolvingArray |
	ObjectFlagsInstantiationExpressionType | ObjectFlagsSingleSignatureType;

// TypeAlias

struct TypeAlias {
	Symbol* symbol{};
	std::vector<Type*> typeArguments;

	Symbol* SymbolOrNil() const { return symbol; }
	const std::vector<Type*>& TypeArguments() const { return typeArguments; }
};

struct TypeBase;
struct IntrinsicType;
struct LiteralType;
struct UniqueESSymbolType;
struct ConstrainedType;
struct StructuredType;
struct ObjectType;
struct TypeReference;
struct InterfaceType;
struct TupleType;
struct InstantiationExpressionType;
struct MappedType;
struct ReverseMappedType;
struct EvolvingArrayType;
struct IndexedAccessType;
struct IndexType;
struct TypeParameter;
struct UnionOrIntersectionType;
struct UnionType;
struct IntersectionType;
struct TemplateLiteralType;
struct StringMappingType;
struct SubstitutionType;
struct ConditionalType;

struct Type {
	TypeFlags flags{};
	ObjectFlags objectFlags{};
	TypeId id{};
	Symbol* symbol{};
	TypeAlias* alias{};
	Checker* checker{};
	TypeBase* data{};

	// Casts for concrete struct types (mirroring Go's AsXxx accessors).
	IntrinsicType* AsIntrinsicType();
	LiteralType* AsLiteralType();
	UniqueESSymbolType* AsUniqueESSymbolType();
	TupleType* AsTupleType();
	InstantiationExpressionType* AsInstantiationExpressionType();
	MappedType* AsMappedType();
	ReverseMappedType* AsReverseMappedType();
	EvolvingArrayType* AsEvolvingArrayType();
	TypeParameter* AsTypeParameter();
	UnionType* AsUnionType();
	IntersectionType* AsIntersectionType();
	IndexType* AsIndexType();
	IndexedAccessType* AsIndexedAccessType();
	TemplateLiteralType* AsTemplateLiteralType();
	StringMappingType* AsStringMappingType();
	SubstitutionType* AsSubstitutionType();
	ConditionalType* AsConditionalType();

	// Casts for embedded struct types — mirror Go's TypeData.AsX() which returns
	// nil when the concrete data struct doesn't embed that level.
	ConstrainedType* AsConstrainedType();
	StructuredType* AsStructuredType();
	ObjectType* AsObjectType();
	TypeReference* AsTypeReference();
	InterfaceType* AsInterfaceType();
	UnionOrIntersectionType* AsUnionOrIntersectionType();

	bool IsUnion() const { return flags & TypeFlagsUnion; }
	bool IsString() const { return flags & TypeFlagsString; }
	bool IsIntersection() const { return flags & TypeFlagsIntersection; }
	bool IsStringLiteral() const { return flags & TypeFlagsStringLiteral; }
	bool IsNumberLiteral() const { return flags & TypeFlagsNumberLiteral; }
	bool IsBigIntLiteral() const { return flags & TypeFlagsBigIntLiteral; }
	bool IsEnumLiteral() const { return flags & TypeFlagsEnumLiteral; }
	bool IsBooleanLike() const { return flags & TypeFlagsBooleanLike; }
	bool IsStringLike() const { return flags & TypeFlagsStringLike; }
	bool IsClass() const { return objectFlags & ObjectFlagsClass; }
	bool IsTypeParameter() const { return flags & TypeFlagsTypeParameter; }
	bool IsIndex() const { return flags & TypeFlagsIndex; }

	// Constituent list for union/intersection types.
	const std::vector<Type*>& types() const;
	std::vector<Type*>& types();

	// === slice: tracer ===
	Type* Target();

	// === slice: decltypes ===
	// types.go:740-798 — target accessors used by the declared-type layer.
	std::vector<Type*> Distributed();
	InterfaceType* TargetInterfaceType();
	TupleType* TargetTupleType();
};

// TypeData hierarchy — mirrors Go's embedded-struct chain. Each TypeData struct embeds
// its parent as the first member, so &derived == &base == &type.

struct TypeBase {
	Type type_; // The Type header — always the first member
	Type* AsType() { return &type_; }
};

struct IntrinsicType : TypeBase {
	std::string intrinsicName;
};

struct LiteralType : TypeBase {
	// string | jsnum.Number | bool | PseudoBigInt | nil (computed enum)
	std::variant<std::monostate, std::string, Number, bool, PseudoBigInt> value;
	Type* freshType{};
	Type* regularType{};
};

struct UniqueESSymbolType : TypeBase {
	std::string name;
};

using LiteralValue = std::variant<std::monostate, std::string, Number, bool,
								  PseudoBigInt>;

struct ConstrainedType : TypeBase {
	Type* resolvedBaseConstraint{};
};

struct StructuredType : ConstrainedType {
	SymbolTable members;
	std::vector<Symbol*> properties;
	std::vector<Signature*> signatures; // Signatures (call + construct)
	int32_t callSignatureCount{};
	std::vector<IndexInfo*> indexInfos;
	Type* objectTypeWithoutAbstractConstructSignatures{};
};

struct ObjectType : StructuredType {
	Type* target{};
	TypeMapper* mapper{};
	CacheMap<Type*> instantiations;
};

struct TypeReference : ObjectType {
	Node* node{};                      // TypeReferenceNode | ArrayTypeNode | TupleTypeNode when deferred, else nil
	std::vector<Type*> resolvedTypeArguments;
};

struct InterfaceType : TypeReference {
	std::vector<Type*> allTypeParameters; // Type parameters (outer + local + thisType)
	int32_t outerTypeParameterCount{};    // Count of outer type parameters
	Type* thisType{};
	bool baseTypesResolved{};
	bool declaredMembersResolved{};
	Type* resolvedBaseConstructorType{};
	std::vector<Type*> resolvedBaseTypes;
	SymbolTable declaredMembers;
	std::vector<Signature*> declaredCallSignatures;
	std::vector<Signature*> declaredConstructSignatures;
	std::vector<IndexInfo*> declaredIndexInfos;
};

// InterfaceType helpers (types.go:OuterTypeParameters/LocalTypeParameters/TypeParameters/ThisType)
inline std::vector<Type*> interfaceTypeOuterTypeParameters(const InterfaceType* t) {
	if (t->allTypeParameters.empty()) {
		return {};
	}
	return std::vector<Type*>(t->allTypeParameters.begin(),
							  t->allTypeParameters.begin() + t->outerTypeParameterCount);
}
inline std::vector<Type*> interfaceTypeLocalTypeParameters(const InterfaceType* t) {
	if (t->allTypeParameters.empty()) {
		return {};
	}
	return std::vector<Type*>(t->allTypeParameters.begin() + t->outerTypeParameterCount,
							  t->allTypeParameters.end() - 1);
}
inline std::vector<Type*> interfaceTypeTypeParameters(const InterfaceType* t) {
	if (t->allTypeParameters.empty()) {
		return {};
	}
	return std::vector<Type*>(t->allTypeParameters.begin(), t->allTypeParameters.end() - 1);
}

using ElementFlags = uint32_t;
inline constexpr ElementFlags ElementFlagsNone = 0;
inline constexpr ElementFlags ElementFlagsRequired = 1 << 0;
inline constexpr ElementFlags ElementFlagsOptional = 1 << 1;
inline constexpr ElementFlags ElementFlagsRest = 1 << 2;
inline constexpr ElementFlags ElementFlagsVariadic = 1 << 3;
inline constexpr ElementFlags ElementFlagsFixed = ElementFlagsRequired | ElementFlagsOptional;
inline constexpr ElementFlags ElementFlagsVariable = ElementFlagsRest | ElementFlagsVariadic;
inline constexpr ElementFlags ElementFlagsNonRequired =
	ElementFlagsOptional | ElementFlagsRest | ElementFlagsVariadic;
inline constexpr ElementFlags ElementFlagsNonRest =
	ElementFlagsRequired | ElementFlagsOptional | ElementFlagsVariadic;

struct TupleElementInfo {
	ElementFlags flags{};
	Node* labeledDeclaration{}; // NamedTupleMember | ParameterDeclaration | nil
};

struct TupleType : InterfaceType {
	std::vector<TupleElementInfo> elementInfos;
	int32_t minLength{};    // Number of required or variadic elements
	int32_t fixedLength{};  // Number of initial required or optional elements
	ElementFlags combinedFlags{};
	bool readonly{};
};

struct InstantiationExpressionType : ObjectType {
	Node* node{};
};

struct MappedType : ObjectType {
	Node* declaration{}; // MappedTypeNode
	Type* typeParameter{};
	Type* constraintType{};
	Type* nameType{};
	Type* templateType{};
	Type* modifiersType{};
	Type* resolvedApparentType{};
	bool containsError{};
};

struct ReverseMappedType : ObjectType {
	Type* source{};
	Type* mappedType{};
	Type* constraintType{};
};

struct EvolvingArrayType : ObjectType {
	Type* elementType{};
	Type* finalArrayType{};
};

struct UnionOrIntersectionType : StructuredType {
	std::vector<Type*> types;
	SymbolTable propertyCache;
	SymbolTable propertyCacheWithoutFunctionPropertyAugment;
	std::vector<Symbol*> resolvedProperties;
};

struct UnionType : UnionOrIntersectionType {
	Type* resolvedReducedType{};
	Type* regularType{};
	Type* origin{};                    // Denormalized union, intersection, or index type in which union originates
	std::string keyPropertyName;       // Property with unique unit type that exists in every object/intersection in union type
	std::unordered_map<Type*, Type*> constituentMap; // Constituents keyed by unit type discriminants
};

struct IntersectionType : UnionOrIntersectionType {
	Type* resolvedApparentType{};
	Type* uniqueLiteralFilledInstantiation{}; // Instantiation with type parameters mapped to never type
};

struct TypeParameter : ConstrainedType {
	Type* constraint{};
	Type* target{};
	TypeMapper* mapper{};
	bool isThisType{};
	bool isDistributed{};
	Type* resolvedDefaultType{};
	Type* distributedType{};
};

using IndexFlags = uint32_t;
inline constexpr IndexFlags IndexFlagsNone = 0;
inline constexpr IndexFlags IndexFlagsStringsOnly = 1 << 0;
inline constexpr IndexFlags IndexFlagsNoIndexSignatures = 1 << 1;
inline constexpr IndexFlags IndexFlagsNoReducibleCheck = 1 << 2;

struct IndexType : ConstrainedType {
	Type* target{};
	IndexFlags indexFlags{};
};

struct IndexedAccessType : ConstrainedType {
	Type* objectType{};
	Type* indexType{};
	AccessFlags accessFlags{}; // Only includes AccessFlags.Persistent
};

struct TemplateLiteralType : ConstrainedType {
	std::vector<std::string> texts; // Always one element longer than types
	std::vector<Type*> types;       // Always at least one element
};

struct StringMappingType : ConstrainedType {
	Type* target{};
};

struct SubstitutionType : ConstrainedType {
	Type* baseType{};   // Target type
	Type* constraint{}; // Constraint that target type is known to satisfy
};

struct ConditionalRoot {
	Node* node{}; // ConditionalTypeNode
	Type* checkType{};
	Type* extendsType{};
	bool isDistributive{};
	std::vector<Type*> inferTypeParameters;
	std::vector<Type*> outerTypeParameters;
	CacheMap<Type*> instantiations;
	TypeAlias* alias{};
};

struct ConditionalType : ConstrainedType {
	ConditionalRoot* root{};
	Type* checkType{};
	Type* extendsType{};
	Type* resolvedTrueType{};
	Type* resolvedFalseType{};
	Type* resolvedInferredTrueType{};
	Type* resolvedDefaultConstraint{};
	Type* resolvedConstraintOfDistributive{};
	TypeMapper* mapper{};
	TypeMapper* combinedMapper{};
};

// SignatureFlags

using SignatureFlags = uint32_t;
inline constexpr SignatureFlags SignatureFlagsNone = 0;
inline constexpr SignatureFlags SignatureFlagsHasRestParameter = 1 << 0;
inline constexpr SignatureFlags SignatureFlagsHasLiteralTypes = 1 << 1;
inline constexpr SignatureFlags SignatureFlagsConstruct = 1 << 2;
inline constexpr SignatureFlags SignatureFlagsAbstract = 1 << 3;
inline constexpr SignatureFlags SignatureFlagsIsInnerCallChain = 1 << 4;
inline constexpr SignatureFlags SignatureFlagsIsOuterCallChain = 1 << 5;
inline constexpr SignatureFlags SignatureFlagsIsUntypedSignatureInJSFile = 1 << 6;
inline constexpr SignatureFlags SignatureFlagsIsNonInferrable = 1 << 7;
inline constexpr SignatureFlags SignatureFlagsIsSignatureCandidateForOverloadFailure = 1 << 8;
inline constexpr SignatureFlags SignatureFlagsPropagatingFlags =
	SignatureFlagsHasRestParameter | SignatureFlagsHasLiteralTypes | SignatureFlagsConstruct |
	SignatureFlagsAbstract | SignatureFlagsIsUntypedSignatureInJSFile |
	SignatureFlagsIsSignatureCandidateForOverloadFailure;
inline constexpr SignatureFlags SignatureFlagsCallChainFlags =
	SignatureFlagsIsInnerCallChain | SignatureFlagsIsOuterCallChain;

struct CompositeSignature {
	bool isUnion{};                    // True for union, false for intersection
	std::vector<Signature*> signatures; // Individual signatures
};

struct Signature {
	SignatureId id{};
	SignatureFlags flags{};
	int32_t minArgumentCount{};
	int32_t resolvedMinArgumentCount{};
	Node* declaration{};
	std::vector<Type*> typeParameters;
	std::vector<Symbol*> parameters;
	Symbol* thisParameter{};
	Type* resolvedReturnType{};
	TypePredicate* resolvedTypePredicate{};
	Signature* target{};
	TypeMapper* mapper{};
	Type* isolatedSignatureType{};
	CompositeSignature* composite{};
};

enum class TypePredicateKind : int32_t {
	This,
	Identifier,
	AssertsThis,
	AssertsIdentifier,
};

struct TypePredicate {
	TypePredicateKind kind{};
	int32_t parameterIndex{};
	std::string parameterName;
	Type* t{};
};

// IndexInfo

// checker.go:8829
struct ConstructorAccessibilityError {
	ModifierFlags kind{};
	Type* declaringClass{};
};

struct IndexInfo {
	Type* keyType{};
	Type* valueType{};
	bool isReadonly{};
	Node* declaration{};          // IndexSignatureDeclaration
	Symbol* indexSymbol{};        // Synthetic property symbol for this index signature
	std::vector<Node*> components; // ElementWithComputedPropertyName
};

// Ternary — x & y picks the lesser in order False < Unknown < Maybe < True;
// x | y picks the greater.

enum class Ternary : int8_t {
	False = 0,
	Unknown = 1,
	Maybe = 3,
	True = -1,
};

using TypeComparer = std::function<Ternary(Type* s, Type* t, bool reportErrors)>;

struct LanguageFeatureMinimumTargetMap {
	ScriptTarget Exponentiation;
	ScriptTarget AsyncFunctions;
	ScriptTarget ForAwaitOf;
	ScriptTarget AsyncGenerators;
	ScriptTarget AsyncIteration;
	ScriptTarget ObjectSpreadRest;
	ScriptTarget RegularExpressionFlagsDotAll;
	ScriptTarget BindinglessCatch;
	ScriptTarget BigInt;
	ScriptTarget NullishCoalesce;
	ScriptTarget OptionalChaining;
	ScriptTarget LogicalAssignment;
	ScriptTarget TopLevelAwait;
	ScriptTarget ClassFields;
	ScriptTarget PrivateNamesAndClassStaticBlocks;
	ScriptTarget RegularExpressionFlagsHasIndices;
	ScriptTarget ShebangComments;
	ScriptTarget UsingAndAwaitUsing;
	ScriptTarget ClassAndClassElementDecorators;
	ScriptTarget RegularExpressionFlagsUnicodeSets;
};

// types.go:1471 — LanguageFeatureMinimumTarget
inline const LanguageFeatureMinimumTargetMap LanguageFeatureMinimumTarget{
	ScriptTarget::ES2016, // Exponentiation
	ScriptTarget::ES2017, // AsyncFunctions
	ScriptTarget::ES2018, // ForAwaitOf
	ScriptTarget::ES2018, // AsyncGenerators
	ScriptTarget::ES2018, // AsyncIteration
	ScriptTarget::ES2018, // ObjectSpreadRest
	ScriptTarget::ES2018, // RegularExpressionFlagsDotAll
	ScriptTarget::ES2019, // BindinglessCatch
	ScriptTarget::ES2020, // BigInt
	ScriptTarget::ES2020, // NullishCoalesce
	ScriptTarget::ES2020, // OptionalChaining
	ScriptTarget::ES2021, // LogicalAssignment
	ScriptTarget::ES2022, // TopLevelAwait
	ScriptTarget::ES2022, // ClassFields
	ScriptTarget::ES2022, // PrivateNamesAndClassStaticBlocks
	ScriptTarget::ES2022, // RegularExpressionFlagsHasIndices
	ScriptTarget::ESNext, // ShebangComments
	ScriptTarget::ESNext, // UsingAndAwaitUsing
	ScriptTarget::ESNext, // ClassAndClassElementDecorators
	ScriptTarget::ESNext, // RegularExpressionFlagsUnicodeSets
};

// Type — mirrors Go's Type struct. `data` points to the containing TypeData
// struct (whose first member is this Type).



using StringLiteralType = Type;

inline IntrinsicType* Type::AsIntrinsicType() { return static_cast<IntrinsicType*>(data); }
inline LiteralType* Type::AsLiteralType() { return static_cast<LiteralType*>(data); }
inline UniqueESSymbolType* Type::AsUniqueESSymbolType() { return static_cast<UniqueESSymbolType*>(data); }
inline TupleType* Type::AsTupleType() { return static_cast<TupleType*>(data); }
inline InstantiationExpressionType* Type::AsInstantiationExpressionType() {
		return static_cast<InstantiationExpressionType*>(data);
	}
inline MappedType* Type::AsMappedType() { return static_cast<MappedType*>(data); }
inline ReverseMappedType* Type::AsReverseMappedType() { return static_cast<ReverseMappedType*>(data); }
inline EvolvingArrayType* Type::AsEvolvingArrayType() { return static_cast<EvolvingArrayType*>(data); }
inline TypeParameter* Type::AsTypeParameter() { return static_cast<TypeParameter*>(data); }
inline UnionType* Type::AsUnionType() { return static_cast<UnionType*>(data); }
inline IntersectionType* Type::AsIntersectionType() { return static_cast<IntersectionType*>(data); }
inline IndexType* Type::AsIndexType() { return static_cast<IndexType*>(data); }
inline IndexedAccessType* Type::AsIndexedAccessType() { return static_cast<IndexedAccessType*>(data); }
inline TemplateLiteralType* Type::AsTemplateLiteralType() { return static_cast<TemplateLiteralType*>(data); }
inline StringMappingType* Type::AsStringMappingType() { return static_cast<StringMappingType*>(data); }
inline SubstitutionType* Type::AsSubstitutionType() { return static_cast<SubstitutionType*>(data); }
inline ConditionalType* Type::AsConditionalType() { return static_cast<ConditionalType*>(data); }
inline ConstrainedType* Type::AsConstrainedType() {
		// IntrinsicType, LiteralType (incl. Enum/EnumLiteral) and UniqueESSymbolType
		// do not embed ConstrainedType; every other kind does.
		if (flags & (TypeFlagsIntrinsic | TypeFlagsLiteral | TypeFlagsUniqueESSymbol |
					 TypeFlagsEnum | TypeFlagsEnumLiteral)) {
			return nullptr;
		}
		return static_cast<ConstrainedType*>(data);
	}
inline StructuredType* Type::AsStructuredType() {
		if (!(flags & TypeFlagsStructuredType)) {
			return nullptr;
		}
		return static_cast<StructuredType*>(data);
	}
inline ObjectType* Type::AsObjectType() {
		if (!(flags & TypeFlagsObject)) {
			return nullptr;
		}
		return static_cast<ObjectType*>(data);
	}
inline TypeReference* Type::AsTypeReference() {
		if (!(flags & TypeFlagsObject) ||
			!(objectFlags & (ObjectFlagsReference | ObjectFlagsClassOrInterface | ObjectFlagsTuple))) {
			return nullptr;
		}
		return static_cast<TypeReference*>(data);
	}
inline InterfaceType* Type::AsInterfaceType() {
		if (!(flags & TypeFlagsObject) ||
			!(objectFlags & (ObjectFlagsClassOrInterface | ObjectFlagsTuple))) {
			return nullptr;
		}
		return static_cast<InterfaceType*>(data);
	}
inline UnionOrIntersectionType* Type::AsUnionOrIntersectionType() {
		if (!(flags & TypeFlagsUnionOrIntersection)) {
			return nullptr;
		}
		return static_cast<UnionOrIntersectionType*>(data);
	}
inline const std::vector<Type*>& Type::types() const {
		return static_cast<const UnionOrIntersectionType*>(data)->types;
	}
inline std::vector<Type*>& Type::types() {
		return static_cast<UnionOrIntersectionType*>(data)->types;
	}

// === slice: tracer === — types.go:752
inline Type* Type::Target() {
	if (flags & TypeFlagsObject) {
		return AsObjectType()->target;
	}
	if (flags & TypeFlagsTypeParameter) {
		return AsTypeParameter()->target;
	}
	if (flags & TypeFlagsIndex) {
		return AsIndexType()->target;
	}
	if (flags & TypeFlagsStringMapping) {
		return AsStringMappingType()->target;
	}
	if ((flags & TypeFlagsObject) && (objectFlags & ObjectFlagsMapped)) {
		return AsMappedType()->target;
	}
	TSC_UNREACHABLE("Unhandled case in Type.Target");
}

// === slice: contextual (checker.go:29385-32069) ===
// relater.go:1199 — Discriminator interface used by
// Checker::discriminateTypeByDiscriminableItems.

struct Discriminator {
	virtual ~Discriminator() = default;
	virtual int len() = 0;                              // Number of discriminant properties
	virtual std::string name(int index) = 0;            // Property name of index-th discriminator
	virtual bool matches(int index, Type* t) = 0;       // True if index-th discriminator matches the given type
};

struct ObjectLiteralDiscriminator : Discriminator {
	Checker* c{};
	std::vector<Node*> props;
	std::vector<Symbol*> members;

	int len() override;
	std::string name(int index) override;
	bool matches(int index, Type* t) override;
};

// === end slice: contextual ===

} // namespace tsc::checker
