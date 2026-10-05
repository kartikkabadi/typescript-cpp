// Port of tsc/internal/checker/checker.go — the type checker.
// Functions are ported in dependency order; unported bodies are marked TSC_UNREACHABLE.
#include "internal/checker/checker.h"

#include <algorithm>
#include <cstring>
#include <bit>
#include <mutex>

#include "internal/binder/binder.h"
#include "internal/binder/nameresolver.h"
#include "internal/checker/mapper.h"
#include "internal/core/linkstore.h"
#include "internal/core/nodemodules.h"
#include "internal/core/pattern.h"
#include "internal/jsnum/jsnum.h"
#include "internal/scanner/scanner.h"
#include "internal/core/spelling.h"
#include "internal/tspath/tspath.h"

namespace tsc {
namespace checker {

constexpr int maxSerializationLevel = 2;

// ---------------------------------------------------------------------------
// keyBuilder — port of Go's keyBuilder. We collect the encoded byte stream and
// chunk it into CacheKey words (content-keyed interning; equivalent to Go's
// xxh3-hashed keys since the keys never escape the checker).
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

static CacheKey getTypeListKey(const std::vector<Type*>& types) {
	keyBuilder b;
	b.writeTypes(types);
	return b.hash();
}

static CacheKey getAliasKey(TypeAlias* alias) {
	keyBuilder b;
	b.writeAlias(alias);
	return b.hash();
}

static CacheKey getUnionKey(const std::vector<Type*>& types, Type* origin, TypeAlias* alias) {
	keyBuilder b;
	if (origin == nullptr) {
		b.writeTypes(types);
	} else if (origin->flags & TypeFlagsUnion) {
		b.writeByte('|');
		b.writeTypes(origin->types());
	} else if (origin->flags & TypeFlagsIntersection) {
		b.writeByte('&');
		b.writeTypes(origin->types());
	} else if (origin->flags & TypeFlagsIndex) {
		// origin type id alone is insufficient, as `keyof x` may resolve to multiple WIP
		// values while `x` is still resolving
		b.writeByte('#');
		b.writeType(origin);
		b.writeByte('|');
		b.writeTypes(types);
	} else {
		TSC_UNREACHABLE("Unhandled case in getUnionKey");
	}
	b.writeAlias(alias);
	return b.hash();
}

static CacheKey getIntersectionKey(const std::vector<Type*>& types, IntersectionFlags flags,
	TypeAlias* alias) {
	keyBuilder b;
	b.writeTypes(types);
	if (!(flags & IntersectionFlagsNoConstraintReduction)) {
		b.writeAlias(alias);
	} else {
		b.writeByte('*');
	}
	return b.hash();
}

static CacheKey getTupleKey(const std::vector<TupleElementInfo>& elementInfos, bool readonly) {
	keyBuilder b;
	for (const TupleElementInfo& e : elementInfos) {
		if (e.flags & ElementFlagsRequired) {
			b.writeByte('#');
		} else if (e.flags & ElementFlagsOptional) {
			b.writeByte('?');
		} else if (e.flags & ElementFlagsRest) {
			b.writeByte('.');
		} else {
			b.writeByte('*');
		}
		if (e.labeledDeclaration != nullptr) {
			b.writeNode(e.labeledDeclaration);
		}
	}
	if (readonly) {
		b.writeByte('r');
	}
	return b.hash();
}

// ---------------------------------------------------------------------------
// Small helpers shared across the checker
// ---------------------------------------------------------------------------

int compareTypeIds(Type* t1, Type* t2) {
	return static_cast<int>(t1->id) - static_cast<int>(t2->id);
}

bool isFreshLiteralType(Type* t) {
	return (t->flags & TypeFlagsFreshable) != 0 && t->AsLiteralType()->freshType == t;
}

bool Checker::isErrorType(Type* t) {
	// The only 'any' types that have alias symbols are those manufactured by
	// getTypeFromTypeAliasReference for a reference to an unresolved symbol. We want
	// those to behave like the errorType.
	return t == errorType || (t->flags & TypeFlagsAny) != 0 && t->alias != nullptr;
}

bool maybeTypeOfKind(Type* t, TypeFlags flags) {
	return (t->flags & flags) != 0;
}

bool containsType(const std::vector<Type*>& types, Type* t) {
	return std::binary_search(types.begin(), types.end(), t,
		[](Type* a, Type* b) { return compareTypeIds(a, b) < 0; });
}

static bool insertType(std::vector<Type*>& types, Type* t) {
	auto it = std::lower_bound(types.begin(), types.end(), t,
		[](Type* a, Type* b) { return compareTypeIds(a, b) < 0; });
	if (it == types.end() || *it != t) {
		types.insert(it, t);
		return true;
	}
	return false;
}

int countTypes(Type* t) {
	if (t->flags & TypeFlagsUnion) {
		return static_cast<int>(t->types().size());
	}
	if (t->flags & TypeFlagsNever) {
		return 0;
	}
	return 1;
}

void forEachType(Type* t, const std::function<void(Type*)>& f) {
	if (t->flags & TypeFlagsUnion) {
		for (Type* u : t->types()) {
			f(u);
		}
	} else {
		f(t);
	}
}

template <class T, class F>
static bool everyList(const std::vector<T>& ts, F&& f) {
	for (const T& t : ts) {
		if (!f(t)) {
			return false;
		}
	}
	return true;
}
template <class T, class F>
static bool someList(const std::vector<T>& ts, F&& f) {
	for (const T& t : ts) {
		if (f(t)) {
			return true;
		}
	}
	return false;
}
static bool isUnionWithUndefined(Type* t);
static bool isUnionWithNull(Type* t);
static bool isNotUndefinedType(Type* t);
static bool isNotNullType(Type* t);
static bool isIntersectionType(Type* t);
static int getConstituentCountOfTypes(const std::vector<Type*>& types);

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

bool everyContainedType(Type* t, const std::function<bool(Type*)>& f) {
	if (t->flags & TypeFlagsUnionOrIntersection) {
		for (Type* u : t->types()) {
			if (!f(u)) {
				return false;
			}
		}
		return true;
	}
	return f(t);
}

Type* getNonDistributedTypeParameter(Type* t) {
	if ((t->flags & TypeFlagsTypeParameter) != 0 && t->AsTypeParameter()->isDistributed) {
		return t->AsTypeParameter()->constraint;
	}
	return t;
}

bool isThisTypeParameter(Type* t) {
	return (t->flags & TypeFlagsTypeParameter) != 0 && t->AsTypeParameter()->isThisType;
}

void clearCachedInferences(std::vector<InferenceInfo*>& inferences) {
	for (InferenceInfo* inference : inferences) {
		if (!inference->isFixed) {
			inference->inferredType = nullptr;
		}
	}
}

Node* getFirstDeclaration(Symbol* symbol) {
	if (!symbol->declarations.empty()) {
		return symbol->declarations[0];
	}
	return nullptr;
}

static SymbolFlags getExcludedSymbolFlags(SymbolFlags flags) {
	SymbolFlags result = 0;
	if (flags & SymbolFlagsBlockScopedVariable) {
		result |= SymbolFlagsBlockScopedVariableExcludes;
	}
	if (flags & SymbolFlagsFunctionScopedVariable) {
		result |= SymbolFlagsFunctionScopedVariableExcludes;
	}
	if (flags & SymbolFlagsProperty) {
		result |= SymbolFlagsPropertyExcludes;
	}
	if (flags & SymbolFlagsEnumMember) {
		result |= SymbolFlagsEnumMemberExcludes;
	}
	if (flags & SymbolFlagsFunction) {
		result |= SymbolFlagsFunctionExcludes;
	}
	if (flags & SymbolFlagsClass) {
		result |= SymbolFlagsClassExcludes;
	}
	if (flags & SymbolFlagsInterface) {
		result |= SymbolFlagsInterfaceExcludes;
	}
	if (flags & SymbolFlagsRegularEnum) {
		result |= SymbolFlagsRegularEnumExcludes;
	}
	if (flags & SymbolFlagsConstEnum) {
		result |= SymbolFlagsConstEnumExcludes;
	}
	if (flags & SymbolFlagsValueModule) {
		result |= SymbolFlagsValueModuleExcludes;
	}
	if (flags & SymbolFlagsMethod) {
		result |= SymbolFlagsMethodExcludes;
	}
	if (flags & SymbolFlagsGetAccessor) {
		result |= SymbolFlagsGetAccessorExcludes;
	}
	if (flags & SymbolFlagsSetAccessor) {
		result |= SymbolFlagsSetAccessorExcludes;
	}
	if (flags & SymbolFlagsTypeParameter) {
		result |= SymbolFlagsTypeParameterExcludes;
	}
	if (flags & SymbolFlagsTypeAlias) {
		result |= SymbolFlagsTypeAliasExcludes;
	}
	if (flags & SymbolFlagsAlias) {
		result |= SymbolFlagsAliasExcludes;
	}
	if (flags & SymbolFlagsReplaceableByMethod) {
		result &= ~SymbolFlagsMethod;
	}
	return result;
}

// ---------------------------------------------------------------------------
// Type constructors (checker.go)
// ---------------------------------------------------------------------------

Type* Checker::newType(TypeFlags flags, ObjectFlags objectFlags, TypeBase* data) {
	TypeCount++;
	Type* t = &data->type_;
	t->flags = flags;
	t->objectFlags = objectFlags &
		~(ObjectFlagsCouldContainTypeVariablesComputed | ObjectFlagsCouldContainTypeVariables |
			ObjectFlagsMembersResolved);
	t->id = static_cast<TypeId>(TypeCount);
	t->checker = this;
	t->data = data;
	return t;
}

Type* Checker::newIntrinsicType(TypeFlags flags, const std::string& intrinsicName) {
	return newIntrinsicTypeEx(flags, intrinsicName, ObjectFlagsNone);
}

Type* Checker::newIntrinsicTypeEx(TypeFlags flags, const std::string& intrinsicName,
	ObjectFlags objectFlags) {
	auto* data = new IntrinsicType();
	data->intrinsicName = intrinsicName;
	return newType(flags, objectFlags, data);
}

Type* Checker::createWideningType(Type* nonWideningType) {
	if (strictNullChecks) {
		return nonWideningType;
	}
	Type* t = newIntrinsicType(nonWideningType->flags,
		nonWideningType->AsIntrinsicType()->intrinsicName);
	t->objectFlags |= ObjectFlagsContainsWideningType;
	return t;
}

Type* Checker::createUnknownUnionType() {
	if (strictNullChecks) {
		return getUnionType({undefinedType, nullType, unknownEmptyObjectType});
	}
	return unknownType;
}

Type* Checker::newLiteralType(TypeFlags flags, const LiteralValue& value, Type* regularType) {
	auto* data = new LiteralType();
	data->value = value;
	Type* t = newType(flags, ObjectFlagsNone, data);
	if (regularType != nullptr) {
		data->regularType = regularType;
	} else {
		data->regularType = t;
	}
	return t;
}

Type* Checker::newUniqueESSymbolType(Symbol* symbol, const std::string& name) {
	auto* data = new UniqueESSymbolType();
	data->name = name;
	Type* t = newType(TypeFlagsUniqueESSymbol, ObjectFlagsNone, data);
	t->symbol = symbol;
	return t;
}

Type* Checker::newObjectType(ObjectFlags objectFlags, Symbol* symbol) {
	TypeBase* data;
	if (objectFlags & ObjectFlagsClassOrInterface) {
		data = new InterfaceType();
	} else if (objectFlags & ObjectFlagsTuple) {
		data = new TupleType();
	} else if (objectFlags & ObjectFlagsReference) {
		data = new TypeReference();
	} else if (objectFlags & ObjectFlagsMapped) {
		data = new MappedType();
	} else if (objectFlags & ObjectFlagsReverseMapped) {
		data = new ReverseMappedType();
	} else if (objectFlags & ObjectFlagsEvolvingArray) {
		data = new EvolvingArrayType();
	} else if (objectFlags & ObjectFlagsInstantiationExpressionType) {
		data = new InstantiationExpressionType();
	} else if (objectFlags & ObjectFlagsAnonymous) {
		data = new ObjectType();
	} else {
		TSC_UNREACHABLE("Unhandled case in newObjectType");
	}
	Type* t = newType(TypeFlagsObject, objectFlags, data);
	t->symbol = symbol;
	return t;
}

Type* Checker::newAnonymousType(Symbol* symbol, const SymbolTable& members,
	const std::vector<Signature*>& callSignatures,
	const std::vector<Signature*>& constructSignatures,
	const std::vector<IndexInfo*>& indexInfos) {
	Type* t = newObjectType(ObjectFlagsAnonymous, symbol);
	setStructuredTypeMembers(t, members, callSignatures, constructSignatures, indexInfos);
	return t;
}

Type* Checker::tryCreateTypeReference(Type* target, const std::vector<Type*>& typeArguments) {
	if (!typeArguments.empty() && target == emptyGenericType) {
		return unknownType;
	}
	return createTypeReference(target, typeArguments);
}

Type* Checker::createTypeReference(Type* target, const std::vector<Type*>& typeArguments) {
	return createTypeReferenceEx(target, typeArguments, ObjectFlagsNone);
}

Type* Checker::createTypeReferenceEx(Type* target, const std::vector<Type*>& typeArguments,
	ObjectFlags objectFlags) {
	CacheKey id = getTypeListKey(typeArguments);
	InterfaceType* intf = target->AsInterfaceType();
	auto it = intf->instantiations.find(id);
	if (it != intf->instantiations.end()) {
		return it->second;
	}
	Type* t = newObjectType(
		ObjectFlagsReference | objectFlags |
			getPropagatingFlagsOfTypes(typeArguments, TypeFlagsNone),
		target->symbol);
	TypeReference* d = t->AsTypeReference();
	d->target = target;
	d->resolvedTypeArguments = typeArguments;
	intf->instantiations[id] = t;
	return t;
}

Type* Checker::createDeferredTypeReference(Type* target, Node* node, TypeMapper* mapper,
	TypeAlias* alias) {
	if (alias == nullptr) {
		alias = getAliasForTypeNode(node);
		if (alias != nullptr && mapper != nullptr) {
			alias->typeArguments = instantiateTypes(alias->typeArguments, mapper);
		}
	}
	Type* t = newObjectType(ObjectFlagsReference, target->symbol);
	t->alias = alias;
	TypeReference* d = t->AsTypeReference();
	d->target = target;
	d->mapper = mapper;
	d->node = node;
	return t;
}

Type* Checker::cloneTypeReference(Type* source) {
	Type* t = newObjectType(ObjectFlagsReference, source->symbol);
	t->objectFlags = source->objectFlags & ~ObjectFlagsMembersResolved;
	t->AsTypeReference()->target = source->AsTypeReference()->target;
	t->AsTypeReference()->resolvedTypeArguments =
		source->AsTypeReference()->resolvedTypeArguments;
	return t;
}

void Checker::setStructuredTypeMembers(Type* t, const SymbolTable& members,
	const std::vector<Signature*>& callSignatures,
	const std::vector<Signature*>& constructSignatures,
	const std::vector<IndexInfo*>& indexInfos) {
	t->objectFlags |= ObjectFlagsMembersResolved;
	StructuredType* data = t->AsStructuredType();
	data->members = members;
	data->properties = getNamedMembers(members, t->symbol);
	if (!callSignatures.empty()) {
		if (!constructSignatures.empty()) {
			data->signatures = callSignatures;
			data->signatures.insert(data->signatures.end(), constructSignatures.begin(),
				constructSignatures.end());
		} else {
			data->signatures = callSignatures;
		}
		data->callSignatureCount = static_cast<int32_t>(callSignatures.size());
	} else {
		if (!constructSignatures.empty()) {
			data->signatures = constructSignatures;
		} else {
			data->signatures.clear();
		}
		data->callSignatureCount = 0;
	}
	data->indexInfos = indexInfos;
}

Type* Checker::newTypeParameter(Symbol* symbol) {
	Type* t = newType(TypeFlagsTypeParameter, ObjectFlagsNone, new TypeParameter());
	t->symbol = symbol;
	return t;
}

// This function is used to propagate certain flags when creating new object type references and
// union types. It is only necessary to do so if a constituent type might be the undefined type, the
// null type, the type of an object literal or a non-inferrable type.
ObjectFlags Checker::getPropagatingFlagsOfTypes(const std::vector<Type*>& types,
	TypeFlags excludeKinds) {
	ObjectFlags result = ObjectFlagsNone;
	for (Type* t : types) {
		if (!(t->flags & excludeKinds)) {
			result |= t->objectFlags;
		}
	}
	return result & ObjectFlagsPropagatingFlags;
}

Type* Checker::newUnionType(ObjectFlags objectFlags, const std::vector<Type*>& types) {
	auto* data = new UnionType();
	data->types = types;
	return newType(TypeFlagsUnion, objectFlags, data);
}

Type* Checker::newIntersectionType(ObjectFlags objectFlags, const std::vector<Type*>& types) {
	auto* data = new IntersectionType();
	data->types = types;
	return newType(TypeFlagsIntersection, objectFlags, data);
}

Type* Checker::newIndexedAccessType(Type* objectType, Type* indexType, AccessFlags accessFlags) {
	auto* data = new IndexedAccessType();
	data->objectType = objectType;
	data->indexType = indexType;
	data->accessFlags = accessFlags;
	return newType(TypeFlagsIndexedAccess, ObjectFlagsNone, data);
}

Type* Checker::newIndexType(Type* target, IndexFlags indexFlags) {
	auto* data = new IndexType();
	data->target = target;
	data->indexFlags = indexFlags;
	return newType(TypeFlagsIndex, ObjectFlagsNone, data);
}

Type* Checker::newTemplateLiteralType(const std::vector<std::string>& texts,
	const std::vector<Type*>& types) {
	auto* data = new TemplateLiteralType();
	data->texts = texts;
	data->types = types;
	return newType(TypeFlagsTemplateLiteral, ObjectFlagsNone, data);
}

Type* Checker::newStringMappingType(Symbol* symbol, Type* target) {
	auto* data = new StringMappingType();
	data->target = target;
	Type* t = newType(TypeFlagsStringMapping, ObjectFlagsNone, data);
	t->symbol = symbol;
	return t;
}

Type* Checker::newConditionalType(ConditionalRoot* root, TypeMapper* mapper,
	TypeMapper* combinedMapper) {
	auto* data = new ConditionalType();
	data->root = root;
	data->checkType = instantiateType(root->checkType, mapper);
	data->extendsType = instantiateType(root->extendsType, mapper);
	data->mapper = mapper;
	data->combinedMapper = combinedMapper;
	return newType(TypeFlagsConditional, ObjectFlagsNone, data);
}

Type* Checker::newSubstitutionType(Type* baseType, Type* constraint) {
	auto* data = new SubstitutionType();
	data->baseType = baseType;
	data->constraint = constraint;
	return newType(TypeFlagsSubstitution, ObjectFlagsNone, data);
}

Signature* Checker::newSignature(SignatureFlags flags, Node* declaration,
	const std::vector<Type*>& typeParameters, Symbol* thisParameter,
	const std::vector<Symbol*>& parameters, Type* resolvedReturnType,
	TypePredicate* resolvedTypePredicate, int minArgumentCount) {
	SignatureCount++;
	Signature* sig = signatureArena.alloc<Signature>();
	sig->id = static_cast<SignatureId>(SignatureCount);
	sig->flags = flags;
	sig->declaration = declaration;
	sig->typeParameters = typeParameters;
	sig->parameters = parameters;
	sig->thisParameter = thisParameter;
	sig->resolvedReturnType = resolvedReturnType;
	sig->resolvedTypePredicate = resolvedTypePredicate;
	sig->minArgumentCount = static_cast<int32_t>(minArgumentCount);
	sig->resolvedMinArgumentCount = -1;
	return sig;
}

IndexInfo* Checker::newIndexInfo(Type* keyType, Type* valueType, bool isReadonly,
	Node* declaration, const std::vector<Node*>& components) {
	IndexInfo* info = indexInfoArena.alloc<IndexInfo>();
	info->keyType = keyType;
	info->valueType = valueType;
	info->isReadonly = isReadonly;
	info->declaration = declaration;
	info->components = components;
	return info;
}


namespace {
const std::unordered_map<std::string, IntrinsicTypeKind> intrinsicTypeKinds = {
	{"Uppercase", IntrinsicTypeKind::Uppercase},
	{"Lowercase", IntrinsicTypeKind::Lowercase},
	{"Capitalize", IntrinsicTypeKind::Capitalize},
	{"Uncapitalize", IntrinsicTypeKind::Uncapitalize},
	{"NoInfer", IntrinsicTypeKind::NoInfer},
};
constexpr size_t maxTemplateLiteralTypeLength = 50'000'000;
constexpr int maxTemplateLiteralTypeSpans = 100'000;
}  // namespace

// ---------------------------------------------------------------------------
// Literal factories
// ---------------------------------------------------------------------------

// The widen slice (checker.go:25739-26019) moved to checker_widen.cpp:
// getRegularTypeOfLiteralType, getFreshTypeOfLiteralType, getStringLiteralType,
// getNumberLiteralType, getBigIntLiteralType, parseBigIntLiteralType,
// getEnumLiteralType, isUnitLikeType, extractUnitType, getBaseTypeOfLiteralType,
// getBaseTypeOfLiteralTypeForComparison, getBaseTypeOfEnumLikeType,
// getBaseTypeOfLiteralTypeUnion, getWidenedLiteralType,
// getWidenedUniqueESSymbolType, getWidenedLiteralLikeTypeForContextualType,
// isLiteralOfContextualType. The static literal helpers below stay here because
// remaining checker.cpp code still uses them (checker_widen.cpp has its own
// file-local copies).

static std::string anyToString(const LiteralValue& v) {
	if (const std::string* s = std::get_if<std::string>(&v)) {
		return *s;
	}
	if (const Number* n = std::get_if<Number>(&v)) {
		return n->string();
	}
	if (const bool* b = std::get_if<bool>(&v)) {
		return *b ? "true" : "false";
	}
	if (const PseudoBigInt* b = std::get_if<PseudoBigInt>(&v)) {
		return b->string();
	}
	TSC_UNREACHABLE("Unhandled case in anyToString");
}

static std::string getStringLiteralValue(Type* t) {
	return std::get<std::string>(t->AsLiteralType()->value);
}

static Number getNumberLiteralValue(Type* t) {
	return std::get<Number>(t->AsLiteralType()->value);
}

static PseudoBigInt getBigIntLiteralValue(Type* t) {
	return std::get<PseudoBigInt>(t->AsLiteralType()->value);
}

static bool getBooleanLiteralValue(Type* t) {
	return std::get<bool>(t->AsLiteralType()->value);
}

static bool isUnitType(Type* t) {
	return (t->flags & TypeFlagsUnit) != 0;
}

static bool isLiteralType(Type* t) {
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

// ---------------------------------------------------------------------------
// mapType / filterType / removeType
// ---------------------------------------------------------------------------

Type* Checker::mapTypeWithAlias(Type* t, const std::function<Type*(Type*)>& f, TypeAlias* alias) {
	if ((t->flags & TypeFlagsUnion) && alias != nullptr) {
		std::vector<Type*> mapped;
		mapped.reserve(t->types().size());
		for (Type* u : t->types()) {
			mapped.push_back(f(u));
		}
		return getUnionTypeEx(mapped, UnionReductionLiteral, alias, nullptr);
	}
	return mapType(t, f);
}

Type* Checker::mapType(Type* t, const std::function<Type*(Type*)>& f) {
	return mapTypeEx(t, f, false);
}

Type* Checker::mapTypeEx(Type* t, const std::function<Type*(Type*)>& f, bool noReductions) {
	if (t->flags & TypeFlagsNever) {
		return t;
	}
	if (!(t->flags & TypeFlagsUnion)) {
		return f(t);
	}
	UnionType* u = t->AsUnionType();
	const std::vector<Type*>* types = &u->types;
	if (u->origin != nullptr && (u->origin->flags & TypeFlagsUnion)) {
		types = &u->origin->types();
	}
	std::vector<Type*> mappedTypes;
	mappedTypes.reserve(16);
	bool changed = false;
	for (Type* s : *types) {
		Type* mapped;
		if (s->flags & TypeFlagsUnion) {
			mapped = mapTypeEx(s, f, noReductions);
		} else {
			mapped = f(s);
		}
		if (mapped != s) {
			changed = true;
		}
		if (mapped != nullptr) {
			mappedTypes.push_back(mapped);
		}
	}
	if (changed) {
		if (mappedTypes.empty()) {
			return nullptr;
		}
		return getUnionTypeEx(std::move(mappedTypes),
			noReductions ? UnionReductionNone : UnionReductionLiteral, nullptr, nullptr);
	}
	return t;
}

Type* Checker::filterType(Type* t, const std::function<bool(Type*)>& f) {
	if (t->flags & TypeFlagsUnion) {
		UnionType* u = t->AsUnionType();
		const std::vector<Type*>& types = u->types;
		if (u->origin != nullptr && (u->origin->flags & TypeFlagsUnion)) {
			// Fast path: origin is also a union and isn't modified by the filter,
			// so the result is this same union type.
			bool kept = true;
			for (Type* s : u->origin->types()) {
				if (s->flags & TypeFlagsUnion) {
					for (Type* s2 : s->types()) {
						if (!f(s2)) {
							kept = false;
							break;
						}
					}
				} else if (!f(s)) {
					kept = false;
				}
				if (!kept) {
					break;
				}
			}
			if (kept) {
				return t;
			}
		}
		std::vector<Type*> filtered;
		for (Type* s : types) {
			if (f(s)) {
				filtered.push_back(s);
			}
		}
		return getUnionType(std::move(filtered));
	}
	return f(t) ? t : neverType;
}

Type* Checker::removeType(Type* t, Type* targetType) {
	return filterType(t, [targetType](Type* s) { return s != targetType; });
}

// ---------------------------------------------------------------------------
// Union type construction
// ---------------------------------------------------------------------------

Type* Checker::getUnionOrIntersectionType(const std::vector<Type*>& types, bool isUnion,
	UnionReduction unionReduction) {
	if (isUnion) {
		return getUnionTypeEx(types, unionReduction, nullptr, nullptr);
	}
	return getIntersectionType(types);
}

Type* Checker::getUnionType(const std::vector<Type*>& types) {
	return getUnionTypeEx(types, UnionReductionLiteral, nullptr, nullptr);
}

Type* Checker::getUnionTypeEx(std::vector<Type*> types, UnionReduction unionReduction,
	TypeAlias* alias, Type* origin) {
	if (types.empty()) {
		return neverType;
	}
	if (types.size() == 1) {
		return types[0];
	}
	// Optimize for the common case of unioning a union type with some other type
	// (such as `undefined`).
	if (types.size() == 2 && origin == nullptr &&
		((types[0]->flags & TypeFlagsUnion) || (types[1]->flags & TypeFlagsUnion))) {
		TypeId id1 = types[0]->id;
		TypeId id2 = types[1]->id;
		if (id1 > id2) {
			std::swap(id1, id2);
		}
		UnionOfUnionKey key{id1, id2, unionReduction, getAliasKey(alias)};
		Type* t = unionOfUnionTypes[key];
		if (t == nullptr) {
			t = getUnionTypeWorker(types, unionReduction, alias, nullptr);
			unionOfUnionTypes[key] = t;
		}
		return t;
	}
	return getUnionTypeWorker(std::move(types), unionReduction, alias, origin);
}

Type* Checker::getUnionTypeWorker(std::vector<Type*> types, UnionReduction unionReduction,
	TypeAlias* alias, Type* origin) {
	auto [typeSet, includes] = addTypesToUnion(types);
	if (unionReduction != UnionReductionNone) {
		if (includes & TypeFlagsAnyOrUnknown) {
			if (includes & TypeFlagsAny) {
				if (includes & TypeFlagsIncludesWildcard) {
					return wildcardType;
				}
				if (includes & TypeFlagsIncludesError) {
					return errorType;
				}
				return anyType;
			}
			return unknownType;
		}
		if (includes & TypeFlagsUndefined) {
			// If type set contains both undefinedType and missingType, remove missingType
			if (typeSet.size() >= 2 && typeSet[0] == undefinedType && typeSet[1] == missingType) {
				typeSet.erase(typeSet.begin() + 1);
			}
		}
		if (includes & (TypeFlagsEnum | TypeFlagsLiteral | TypeFlagsUniqueESSymbol |
				 TypeFlagsTemplateLiteral | TypeFlagsStringMapping) ||
			(includes & TypeFlagsVoid) && (includes & TypeFlagsUndefined)) {
			typeSet = removeRedundantLiteralTypes(std::move(typeSet), includes,
				(unionReduction & UnionReductionSubtype) != 0);
		}
		if ((includes & TypeFlagsStringLiteral) &&
			(includes & (TypeFlagsTemplateLiteral | TypeFlagsStringMapping))) {
			typeSet = removeStringLiteralsMatchedByTemplateLiterals(std::move(typeSet));
		}
		if (includes & TypeFlagsIncludesConstrainedTypeVariable) {
			typeSet = removeConstrainedTypeVariables(std::move(typeSet));
		}
		if (unionReduction == UnionReductionSubtype) {
			typeSet = removeSubtypes(std::move(typeSet), (includes & TypeFlagsObject) != 0);
			if (typeSet.empty() && removedSubtypesFailed) {
				return errorType;
			}
		}
		if (typeSet.empty()) {
			if (includes & TypeFlagsNull) {
				if (includes & TypeFlagsIncludesNonWideningType) {
					return nullType;
				}
				return nullWideningType;
			}
			if (includes & TypeFlagsUndefined) {
				if (includes & TypeFlagsIncludesNonWideningType) {
					return undefinedType;
				}
				return undefinedWideningType;
			}
			return neverType;
		}
	}
	if (origin == nullptr && (includes & TypeFlagsUnion)) {
		std::vector<Type*> namedUnions = addNamedUnions({}, types);
		std::vector<Type*> reducedTypes;
		for (Type* t : typeSet) {
			bool inNamed = false;
			for (Type* u : namedUnions) {
				if (containsType(u->types(), t)) {
					inNamed = true;
					break;
				}
			}
			if (!inNamed) {
				reducedTypes.push_back(t);
			}
		}
		if (alias == nullptr && namedUnions.size() == 1 && reducedTypes.empty()) {
			return namedUnions[0];
		}
		// Create a denormalized origin type only when the union was created from one or more
		// named unions and there is no overlap between those named unions.
		size_t namedTypesCount = 0;
		for (Type* u : namedUnions) {
			namedTypesCount += u->types().size();
		}
		if (namedTypesCount + reducedTypes.size() == typeSet.size()) {
			for (Type* t : namedUnions) {
				insertType(reducedTypes, t);
			}
			origin = newUnionType(ObjectFlagsNone, reducedTypes);
		}
	}
	ObjectFlags objectFlags =
		(includes & TypeFlagsNotPrimitiveUnion) ? ObjectFlagsNone : ObjectFlagsPrimitiveUnion;
	if (includes & TypeFlagsIntersection) {
		objectFlags |= ObjectFlagsContainsIntersections;
	}
	return getUnionTypeFromSortedList(typeSet, objectFlags, alias, origin);
}

// This function assumes the constituent type list is sorted and deduplicated.
Type* Checker::getUnionTypeFromSortedList(std::vector<Type*> types, ObjectFlags objectFlags,
	TypeAlias* alias, Type* origin) {
	if (types.empty()) {
		return neverType;
	}
	if (types.size() == 1) {
		return types[0];
	}
	CacheKey key = getUnionKey(types, origin, alias);
	Type* t = unionTypes[key];
	if (t == nullptr) {
		t = newUnionType(objectFlags | getPropagatingFlagsOfTypes(types, TypeFlagsNullable), types);
		t->AsUnionType()->origin = origin;
		t->alias = alias;
		if (types.size() == 2 && (types[0]->flags & TypeFlagsBooleanLiteral) &&
			(types[1]->flags & TypeFlagsBooleanLiteral)) {
			t->flags |= TypeFlagsBoolean;
		}
		unionTypes[key] = t;
	}
	return t;
}

std::pair<std::vector<Type*>, TypeFlags> Checker::addTypesToUnion(
	const std::vector<Type*>& sourceTypes) {
	std::vector<Type*> types;
	types.reserve(sourceTypes.size());
	TypeFlags includes = 0;
	auto addType = [&](Type* t) {
		TypeFlags flags = t->flags;
		// We ignore 'never' types in unions
		if (flags & TypeFlagsNever) {
			return;
		}
		includes |= flags & TypeFlagsIncludesMask;
		if (flags & TypeFlagsInstantiable) {
			includes |= TypeFlagsIncludesInstantiable;
		}
		if ((flags & TypeFlagsIntersection) &&
			(t->objectFlags & ObjectFlagsIsConstrainedTypeVariable)) {
			includes |= TypeFlagsIncludesConstrainedTypeVariable;
		}
		if (t == wildcardType) {
			includes |= TypeFlagsIncludesWildcard;
		}
		if (isErrorType(t)) {
			includes |= TypeFlagsIncludesError;
		}
		if (!strictNullChecks && (flags & TypeFlagsNullable)) {
			if (!(t->objectFlags & ObjectFlagsContainsWideningType)) {
				includes |= TypeFlagsIncludesNonWideningType;
			}
			return;
		}
		types.push_back(t);
	};
	Type* lastType = nullptr;
	for (Type* t : sourceTypes) {
		if (t != lastType) {
			if (t->flags & TypeFlagsUnion) {
				UnionType* u = t->AsUnionType();
				if (t->alias != nullptr || u->origin != nullptr) {
					includes |= TypeFlagsUnion;
				}
				for (Type* s : u->types) {
					addType(s);
				}
			} else {
				addType(t);
			}
			lastType = t;
		}
	}
	if (types.size() >= 2) {
		// Sort and deduplicate types
		std::stable_sort(types.begin(), types.end(),
			[](Type* a, Type* b) { return compareTypeIds(a, b) < 0; });
		size_t unique = 1;
		for (size_t i = 1; i < types.size(); i++) {
			if (types[i] != types[unique - 1]) {
				types[unique] = types[i];
				unique++;
			}
		}
		types.resize(unique);
	}
	return {types, includes};
}

std::vector<Type*> Checker::addNamedUnions(std::vector<Type*> namedUnions,
	const std::vector<Type*>& types) {
	for (Type* t : types) {
		if (t->flags & TypeFlagsUnion) {
			UnionType* u = t->AsUnionType();
			if (t->alias != nullptr || (u->origin != nullptr && !(u->origin->flags & TypeFlagsUnion))) {
				if (std::find(namedUnions.begin(), namedUnions.end(), t) == namedUnions.end()) {
					namedUnions.push_back(t);
				}
			} else if (u->origin != nullptr && (u->origin->flags & TypeFlagsUnion)) {
				namedUnions = addNamedUnions(std::move(namedUnions), u->origin->types());
			}
		}
	}
	return namedUnions;
}

std::vector<Type*> Checker::removeRedundantLiteralTypes(std::vector<Type*> types,
	TypeFlags includes, bool reduceVoidUndefined) {
	for (size_t i = types.size(); i > 0; i--) {
		Type* t = types[i - 1];
		TypeFlags flags = t->flags;
		bool remove =
			(flags & (TypeFlagsStringLiteral | TypeFlagsTemplateLiteral |
				  TypeFlagsStringMapping)) && (includes & TypeFlagsString) ||
			(flags & TypeFlagsNumberLiteral) && (includes & TypeFlagsNumber) ||
			(flags & TypeFlagsBigIntLiteral) && (includes & TypeFlagsBigInt) ||
			(flags & TypeFlagsUniqueESSymbol) && (includes & TypeFlagsESSymbol) ||
			reduceVoidUndefined && (flags & TypeFlagsUndefined) && (includes & TypeFlagsVoid) ||
			isFreshLiteralType(t) && containsType(types, t->AsLiteralType()->regularType);
		if (remove) {
			types.erase(types.begin() + (i - 1));
		}
	}
	return types;
}

std::vector<Type*> Checker::removeStringLiteralsMatchedByTemplateLiterals(
	std::vector<Type*> types) {
	std::vector<Type*> templates;
	for (Type* t : types) {
		if (isPatternLiteralType(t)) {
			templates.push_back(t);
		}
	}
	if (!templates.empty()) {
		for (size_t i = types.size(); i > 0; i--) {
			Type* t = types[i - 1];
			if (!(t->flags & TypeFlagsStringLiteral)) {
				continue;
			}
			for (Type* tl : templates) {
				if (isTypeMatchedByTemplateLiteralOrStringMapping(t, tl)) {
					types.erase(types.begin() + (i - 1));
					break;
				}
			}
		}
	}
	return types;
}

bool Checker::isTypeMatchedByTemplateLiteralOrStringMapping(Type* t, Type* templateType) {
	if (templateType->flags & TypeFlagsTemplateLiteral) {
		return isTypeMatchedByTemplateLiteralType(t, templateType->AsTemplateLiteralType(),
			compareTypesAssignable);
	}
	return isMemberOfStringMapping(t, templateType);
}

std::vector<Type*> Checker::removeConstrainedTypeVariables(std::vector<Type*> types) {
	std::vector<Type*> typeVariables;
	// First collect a list of the type variables occurring in constraining intersections.
	for (Type* t : types) {
		if ((t->flags & TypeFlagsIntersection) &&
			(t->objectFlags & ObjectFlagsIsConstrainedTypeVariable)) {
			auto& members = t->AsIntersectionType()->types;
			size_t index = (members[0]->flags & TypeFlagsTypeVariable) ? 0 : 1;
			Type* tv = members[index];
			if (std::find(typeVariables.begin(), typeVariables.end(), tv) == typeVariables.end()) {
				typeVariables.push_back(tv);
			}
		}
	}
	// For each type variable, check if the constraining intersections for that type variable
	// fully cover the constraint; if so, remove the constraining intersections and substitute
	// the type variable.
	for (Type* typeVariable : typeVariables) {
		std::vector<Type*> primitives;
		for (Type* t : types) {
			if ((t->flags & TypeFlagsIntersection) &&
				(t->objectFlags & ObjectFlagsIsConstrainedTypeVariable)) {
				auto& members = t->AsIntersectionType()->types;
				size_t index = (members[0]->flags & TypeFlagsTypeVariable) ? 0 : 1;
				if (members[index] == typeVariable) {
					insertType(primitives, members[1 - index]);
				}
			}
		}
		Type* constraint = getBaseConstraintOfType(typeVariable);
		if (everyType(constraint,
				[&](Type* t) { return containsType(primitives, t); })) {
			for (size_t i = types.size(); i > 0; i--) {
				Type* t = types[i - 1];
				if ((t->flags & TypeFlagsIntersection) &&
					(t->objectFlags & ObjectFlagsIsConstrainedTypeVariable)) {
					auto& members = t->AsIntersectionType()->types;
					size_t index = (members[0]->flags & TypeFlagsTypeVariable) ? 0 : 1;
					if (members[index] == typeVariable &&
						containsType(primitives, members[1 - index])) {
						types.erase(types.begin() + (i - 1));
					}
				}
			}
			insertType(types, typeVariable);
		}
	}
	return types;
}

std::vector<Type*> Checker::removeSubtypes(std::vector<Type*> types, bool hasObjectTypes) {
	if (types.size() < 2) {
		return types;
	}
	removedSubtypesFailed = false;
	CacheKey key = getTypeListKey(types);
	if (auto it = subtypeReductionCache.find(key); it != subtypeReductionCache.end()) {
		return it->second;
	}
	// The only possible supertypes for primitive types are empty object types; if none are
	// present we can exclude primitive types from the subtype check.
	bool hasEmptyObject = hasObjectTypes;
	if (hasEmptyObject) {
		hasEmptyObject = false;
		for (Type* t : types) {
			if ((t->flags & TypeFlagsObject) && !isGenericMappedType(t) &&
				isEmptyResolvedType(resolveStructuredTypeMembers(t))) {
				hasEmptyObject = true;
				break;
			}
		}
	}
	size_t length = types.size();
	size_t count = 0;
	for (size_t i = length; i > 0; i--) {
		Type* source = types[i - 1];
		if (hasEmptyObject || (source->flags & TypeFlagsStructuredOrInstantiable)) {
			// A type parameter with a union constraint may be a subtype of some union, but not
			// a subtype of the individual constituents of that union.
			if ((source->flags & TypeFlagsTypeParameter) &&
				(getBaseConstraintOrType(source)->flags & TypeFlagsUnion)) {
				std::vector<Type*> rest;
				rest.reserve(types.size());
				for (Type* t : types) {
					rest.push_back(t == source ? neverType : t);
				}
				if (isTypeRelatedTo(source, getUnionType(rest), strictSubtypeRelation)) {
					types.erase(types.begin() + (i - 1));
				}
				continue;
			}
			Symbol* keyProperty = nullptr;
			Type* keyPropertyType = nullptr;
			if (source->flags &
				(TypeFlagsObject | TypeFlagsIntersection | TypeFlagsInstantiableNonPrimitive)) {
				for (Symbol* p : getPropertiesOfType(source)) {
					if (isUnitType(getTypeOfSymbol(p))) {
						keyProperty = p;
						break;
					}
				}
			}
			if (keyProperty != nullptr) {
				keyPropertyType = getRegularTypeOfLiteralType(getTypeOfSymbol(keyProperty));
			}
			for (Type* target : types) {
				if (source != target) {
					if (count == 100000) {
						size_t estimatedCount = (count / (length - i)) * length;
						if (estimatedCount > 1000000) {
							error(currentNode,
								Expression_produces_a_union_type_that_is_too_complex_to_represent);
							removedSubtypesFailed = true;
							return {};
						}
					}
					count++;
					if (keyProperty != nullptr &&
						(target->flags & (TypeFlagsObject | TypeFlagsIntersection |
							 TypeFlagsInstantiableNonPrimitive))) {
						Type* t = getTypeOfPropertyOfType(target, keyProperty->name);
						if (t != nullptr && isUnitType(t) &&
							getRegularTypeOfLiteralType(t) != keyPropertyType) {
							continue;
						}
					}
					if ((source == emptyObjectType || source == unknownEmptyObjectType) &&
						target->symbol != nullptr && IsEmptyAnonymousObjectType(target)) {
						continue;
					}
					if (isTypeRelatedTo(source, target, strictSubtypeRelation) &&
						(!(getTargetType(source)->objectFlags & ObjectFlagsClass) ||
							!(getTargetType(target)->objectFlags & ObjectFlagsClass) ||
							isTypeDerivedFrom(source, target))) {
						types.erase(types.begin() + (i - 1));
						break;
					}
				}
			}
		}
	}
	subtypeReductionCache[key] = types;
	return types;
}

// ---------------------------------------------------------------------------
// Intersection type construction
// ---------------------------------------------------------------------------

Type* Checker::getIntersectionType(const std::vector<Type*>& types) {
	return getIntersectionTypeEx(types, IntersectionFlagsNone, nullptr);
}

Type* Checker::getIntersectionTypeEx(std::vector<Type*> types, IntersectionFlags flags,
	TypeAlias* alias) {
	orderedSet<Type*> orderedTypes;
	orderedTypes.values.reserve(types.size());
	TypeFlags includes = addTypesToIntersection(orderedTypes, 0, types);
	std::vector<Type*> typeSet = orderedTypes.values;
	ObjectFlags objectFlags = ObjectFlagsNone;
	// An intersection type is considered empty if it contains
	// the type never, or
	// more than one unit type or,
	// an object type and a nullable type (null or undefined), or
	// a string-like type and a type known to be non-string-like, or
	// a number-like type and a type known to be non-number-like, or
	// a symbol-like type and a type known to be non-symbol-like, or
	// a void-like type and a type known to be non-void-like, or
	// a non-primitive type and a type known to be primitive.
	if (includes & TypeFlagsNever) {
		if (std::find(typeSet.begin(), typeSet.end(), silentNeverType) != typeSet.end()) {
			return silentNeverType;
		}
		return neverType;
	}
	if ((strictNullChecks && (includes & TypeFlagsNullable) &&
			(includes & (TypeFlagsObject | TypeFlagsNonPrimitive |
				 TypeFlagsIncludesEmptyObject))) ||
		((includes & TypeFlagsNonPrimitive) &&
			(includes & (TypeFlagsDisjointDomains & ~TypeFlagsNonPrimitive))) ||
		((includes & TypeFlagsStringLike) &&
			(includes & (TypeFlagsDisjointDomains & ~TypeFlagsStringLike))) ||
		((includes & TypeFlagsNumberLike) &&
			(includes & (TypeFlagsDisjointDomains & ~TypeFlagsNumberLike))) ||
		((includes & TypeFlagsBigIntLike) &&
			(includes & (TypeFlagsDisjointDomains & ~TypeFlagsBigIntLike))) ||
		((includes & TypeFlagsESSymbolLike) &&
			(includes & (TypeFlagsDisjointDomains & ~TypeFlagsESSymbolLike))) ||
		((includes & TypeFlagsVoidLike) &&
			(includes & (TypeFlagsDisjointDomains & ~TypeFlagsVoidLike)))) {
		return neverType;
	}
	if ((includes & (TypeFlagsTemplateLiteral | TypeFlagsStringMapping)) &&
		(includes & TypeFlagsStringLiteral)) {
		bool isEmptySet;
		std::tie(typeSet, isEmptySet) = extractRedundantTemplateLiterals(std::move(typeSet));
		if (isEmptySet) {
			return neverType;
		}
	}
	if (includes & TypeFlagsAny) {
		if (includes & TypeFlagsIncludesWildcard) {
			return wildcardType;
		}
		if (includes & TypeFlagsIncludesError) {
			return errorType;
		}
		return anyType;
	}
	if (!strictNullChecks && (includes & TypeFlagsNullable)) {
		if (includes & TypeFlagsIncludesEmptyObject) {
			return neverType;
		}
		if (includes & TypeFlagsUndefined) {
			return undefinedType;
		}
		return nullType;
	}
	if ((includes & TypeFlagsString) &&
			(includes & (TypeFlagsStringLiteral | TypeFlagsTemplateLiteral |
				 TypeFlagsStringMapping)) ||
		(includes & TypeFlagsNumber) && (includes & TypeFlagsNumberLiteral) ||
		(includes & TypeFlagsBigInt) && (includes & TypeFlagsBigIntLiteral) ||
		(includes & TypeFlagsESSymbol) && (includes & TypeFlagsUniqueESSymbol) ||
		(includes & TypeFlagsVoid) && (includes & TypeFlagsUndefined) ||
		(includes & TypeFlagsIncludesEmptyObject) &&
			(includes & TypeFlagsDefinitelyNonNullable)) {
		if (!(flags & IntersectionFlagsNoSupertypeReduction)) {
			typeSet = removeRedundantSupertypes(std::move(typeSet), includes);
		}
	}
	if (includes & TypeFlagsIncludesMissingType) {
		auto it = std::find(typeSet.begin(), typeSet.end(), undefinedType);
		*it = missingType;
	}
	if (typeSet.empty()) {
		return unknownType;
	}
	if (typeSet.size() == 1) {
		return typeSet[0];
	}
	if (typeSet.size() == 2 && !(flags & IntersectionFlagsNoConstraintReduction)) {
		size_t typeVarIndex = 0;
		if (!(typeSet[0]->flags & TypeFlagsTypeVariable)) {
			typeVarIndex = 1;
		}
		Type* typeVariable = typeSet[typeVarIndex];
		Type* primitiveType = typeSet[1 - typeVarIndex];
		if ((typeVariable->flags & TypeFlagsTypeVariable) &&
			(((primitiveType->flags & (TypeFlagsPrimitive | TypeFlagsNonPrimitive)) &&
				  !isGenericStringLikeType(primitiveType)) ||
				(includes & TypeFlagsIncludesEmptyObject))) {
			// We have an intersection T & P or P & T, where T is a type variable and P is a
			// primitive type, the object type, or {}.
			Type* constraint = getBaseConstraintOfType(typeVariable);
			// Check that T's constraint is similarly composed of primitive types, the object
			// type, or {}.
			if (constraint != nullptr &&
				everyType(constraint,
					[this](Type* t) { return isPrimitiveOrObjectOrEmptyType(t); })) {
				// If T's constraint is a subtype of P, simply return T.
				if (isTypeStrictSubtypeOf(constraint, primitiveType)) {
					return typeVariable;
				}
				bool someStrictSubtype = false;
				if (constraint->flags & TypeFlagsUnion) {
					someStrictSubtype = someType(constraint, [this, primitiveType](Type* n) {
						return isTypeStrictSubtypeOf(n, primitiveType);
					});
				}
				if (!someStrictSubtype) {
					// No constituent of T's constraint is a subtype of P. If P is also not a
					// subtype of T's constraint, then the constraint and P are unrelated, and
					// the intersection reduces to never.
					if (!isTypeStrictSubtypeOf(primitiveType, constraint)) {
						return neverType;
					}
				}
				// Some constituent of T's constraint is a subtype of P, or P is a subtype of
				// T's constraint. The intersection further constrains the type variable.
				objectFlags = ObjectFlagsIsConstrainedTypeVariable;
			}
		}
	}
	CacheKey key = getIntersectionKey(typeSet, flags, alias);
	Type* result = intersectionTypes[key];
	if (result == nullptr) {
		if (includes & TypeFlagsUnion) {
			bool reduced;
			std::tie(typeSet, reduced) = intersectUnionsOfPrimitiveTypes(std::move(typeSet));
			if (reduced) {
				// Once we have reduced we'll never reduce again, so this occurs at most once.
				result = getIntersectionTypeEx(typeSet, flags, alias);
			} else if (everyList(typeSet, isUnionWithUndefined)) {
				Type* containedUndefinedType = undefinedType;
				if (someList(typeSet, [this](Type* t) { return containsMissingType(t); })) {
					containedUndefinedType = missingType;
				}
				filterTypes(typeSet, isNotUndefinedType);
				result = getUnionTypeEx(
					{getIntersectionTypeEx(typeSet, flags, nullptr), containedUndefinedType},
					UnionReductionLiteral, alias, nullptr);
			} else if (everyList(typeSet, isUnionWithNull)) {
				filterTypes(typeSet, isNotNullType);
				result = getUnionTypeEx(
					{getIntersectionTypeEx(typeSet, flags, nullptr), nullType},
					UnionReductionLiteral, alias, nullptr);
			} else if (typeSet.size() >= 3 && types.size() > 2) {
				// When we have three or more constituents, some of which are unions, we
				// employ a "divide and conquer" strategy.
				size_t middle = typeSet.size() / 2;
				result = getIntersectionTypeEx(
					{getIntersectionTypeEx(std::vector<Type*>(typeSet.begin(),
											   typeSet.begin() + middle),
						 flags, nullptr),
						getIntersectionTypeEx(std::vector<Type*>(typeSet.begin() + middle,
											   typeSet.end()),
						 flags, nullptr)},
					flags, alias);
			} else {
				// Transform X & (A | B) & (C | D) into X & A & C | X & A & D | X & B & C | X & B & D
				if (!checkCrossProductUnion(typeSet)) {
					return errorType;
				}
				std::vector<Type*> constituents = getCrossProductIntersections(typeSet, flags);
				Type* originT = nullptr;
				if (std::any_of(constituents.begin(), constituents.end(), isIntersectionType) &&
					getConstituentCountOfTypes(constituents) >
						getConstituentCountOfTypes(typeSet)) {
					originT = newIntersectionType(ObjectFlagsNone, typeSet);
				}
				result = getUnionTypeEx(constituents, UnionReductionLiteral, alias, originT);
			}
		} else {
			result = newIntersectionType(
				objectFlags | getPropagatingFlagsOfTypes(types, TypeFlagsNullable), typeSet);
			result->alias = alias;
		}
		intersectionTypes[key] = result;
	}
	return result;
}

static bool isUnionWithUndefined(Type* t) {
	return (t->flags & TypeFlagsUnion) && (t->types()[0]->flags & TypeFlagsUndefined);
}

static bool isUnionWithNull(Type* t) {
	return (t->flags & TypeFlagsUnion) &&
		((t->types()[0]->flags & TypeFlagsNull) || (t->types()[1]->flags & TypeFlagsNull));
}

static bool isIntersectionType(Type* t) {
	return (t->flags & TypeFlagsIntersection) != 0;
}

static bool isPrimitiveUnion(Type* t) {
	return (t->objectFlags & ObjectFlagsPrimitiveUnion) != 0;
}

static bool isNotUndefinedType(Type* t) {
	return !(t->flags & TypeFlagsUndefined);
}

static bool isNotNullType(Type* t) {
	return !(t->flags & TypeFlagsNull);
}



// Add the given types to the given type set. Order is preserved, freshness is removed from
// literal types, duplicates are removed, and nested types of the given kind are flattened.
TypeFlags Checker::addTypesToIntersection(orderedSet<Type*>& typeSet, TypeFlags includes,
	const std::vector<Type*>& types) {
	for (Type* t : types) {
		includes = addTypeToIntersection(typeSet, includes, getRegularTypeOfLiteralType(t));
	}
	return includes;
}

TypeFlags Checker::addTypeToIntersection(orderedSet<Type*>& typeSet, TypeFlags includes, Type* t) {
	TypeFlags flags = t->flags;
	if (flags & TypeFlagsIntersection) {
		return addTypesToIntersection(typeSet, includes, t->types());
	}
	if (IsEmptyAnonymousObjectType(t)) {
		if (!(includes & TypeFlagsIncludesEmptyObject)) {
			includes |= TypeFlagsIncludesEmptyObject;
			typeSet.add(t);
		}
	} else {
		if (flags & TypeFlagsAnyOrUnknown) {
			if (t == wildcardType) {
				includes |= TypeFlagsIncludesWildcard;
			}
			if (isErrorType(t)) {
				includes |= TypeFlagsIncludesError;
			}
		} else if (strictNullChecks || !(flags & TypeFlagsNullable)) {
			if (t == missingType) {
				includes |= TypeFlagsIncludesMissingType;
				t = undefinedType;
			}
			if (!typeSet.contains(t)) {
				if ((t->flags & TypeFlagsUnit) && (includes & TypeFlagsUnit)) {
					// We have seen two distinct unit types which means we should reduce to an
					// empty intersection. Adding TypeFlagsNonPrimitive causes that to happen.
					includes |= TypeFlagsNonPrimitive;
				}
				typeSet.add(t);
			}
		}
		includes |= flags & TypeFlagsIncludesMask;
	}
	return includes;
}

std::vector<Type*> Checker::removeRedundantSupertypes(std::vector<Type*> types,
	TypeFlags includes) {
	for (size_t i = types.size(); i > 0; i--) {
		Type* t = types[i - 1];
		bool remove =
			(t->flags & TypeFlagsString) &&
				(includes & (TypeFlagsStringLiteral | TypeFlagsTemplateLiteral |
					 TypeFlagsStringMapping)) ||
			(t->flags & TypeFlagsNumber) && (includes & TypeFlagsNumberLiteral) ||
			(t->flags & TypeFlagsBigInt) && (includes & TypeFlagsBigIntLiteral) ||
			(t->flags & TypeFlagsESSymbol) && (includes & TypeFlagsUniqueESSymbol) ||
			(t->flags & TypeFlagsVoid) && (includes & TypeFlagsUndefined) ||
			IsEmptyAnonymousObjectType(t) && (includes & TypeFlagsDefinitelyNonNullable);
		if (remove) {
			types.erase(types.begin() + (i - 1));
		}
	}
	return types;
}

// Returns true if the intersection of the template literals and string literals is the empty
// set, for example `get${string}` & "setX", and should reduce to never.
std::pair<std::vector<Type*>, bool> Checker::extractRedundantTemplateLiterals(
	std::vector<Type*> types) {
	std::vector<Type*> literals;
	for (Type* t : types) {
		if (t->flags & TypeFlagsStringLiteral) {
			literals.push_back(t);
		}
	}
	for (size_t i = types.size(); i > 0; i--) {
		Type* t = types[i - 1];
		if (!(t->flags & (TypeFlagsTemplateLiteral | TypeFlagsStringMapping))) {
			continue;
		}
		for (Type* t2 : literals) {
			if (isTypeSubtypeOf(t2, t)) {
				// For example, `get${T}` & "getX" is just "getX"
				types.erase(types.begin() + (i - 1));
				break;
			}
			if (isPatternLiteralType(t)) {
				return {types, true};
			}
		}
	}
	return {types, false};
}

std::pair<std::vector<Type*>, bool> Checker::intersectUnionsOfPrimitiveTypes(
	std::vector<Type*> types) {
	auto indexIt = std::find_if(types.begin(), types.end(), isPrimitiveUnion);
	if (indexIt == types.end()) {
		return {types, false};
	}
	size_t index = static_cast<size_t>(indexIt - types.begin());
	// Remove all but the first union of primitive types and collect them in unionTypes.
	size_t i = index + 1;
	std::vector<Type*> unionTypes{types[index]};
	while (i < types.size()) {
		Type* t = types[i];
		if (t->objectFlags & ObjectFlagsPrimitiveUnion) {
			unionTypes.push_back(t);
			types.erase(types.begin() + i);
		} else {
			i++;
		}
	}
	// Return false if there was only one union of primitive types
	if (unionTypes.size() == 1) {
		return {types, false};
	}
	// We have more than one union of primitive types, now intersect them. For each type in
	// each union we check if the type is matched in every union and if so we include it in
	// the result.
	std::vector<Type*> checked;
	std::vector<Type*> result;
	for (Type* u : unionTypes) {
		for (Type* t : u->types()) {
			if (insertType(checked, t) && eachUnionContains(unionTypes, t)) {
				// undefinedType/missingType are always sorted first so we leverage that here
				if (t == undefinedType && !result.empty() && result[0] == missingType) {
					continue;
				}
				if (t == missingType && !result.empty() && result[0] == undefinedType) {
					result[0] = missingType;
					continue;
				}
				insertType(result, t);
			}
		}
	}
	// Finally replace the first union with the result
	types[index] =
		getUnionTypeFromSortedList(result, ObjectFlagsPrimitiveUnion, nullptr, nullptr);
	return {types, true};
}

// Check that the given type has a match in every union. A given type is matched by an
// identical type, and a literal type is additionally matched by its corresponding
// primitive type, and missingType is matched by undefinedType (and vice versa).
bool Checker::eachUnionContains(const std::vector<Type*>& unionTypes, Type* t) {
	for (Type* u : unionTypes) {
		if (!unionContainsType(u, t, true)) {
			return false;
		}
	}
	return true;
}

bool Checker::unionContainsType(Type* unionType, Type* t, bool matchSymbol) {
	const std::vector<Type*>& types = unionType->types();
	if (containsType(types, t)) {
		return true;
	}
	if (t == missingType) {
		return containsType(types, undefinedType);
	}
	if (t == undefinedType) {
		return containsType(types, missingType);
	}
	Type* primitive = nullptr;
	if (t->flags & TypeFlagsStringLiteral) {
		primitive = stringType;
	} else if (t->flags & (TypeFlagsEnum | TypeFlagsNumberLiteral)) {
		primitive = numberType;
	} else if (t->flags & TypeFlagsBigIntLiteral) {
		primitive = bigintType;
	} else if ((t->flags & TypeFlagsUniqueESSymbol) && matchSymbol) {
		primitive = esSymbolType;
	}
	return primitive != nullptr && containsType(types, primitive);
}

std::vector<Type*> Checker::getCrossProductIntersections(const std::vector<Type*>& types,
	IntersectionFlags flags) {
	size_t count = static_cast<size_t>(getCrossProductUnionSize(types));
	std::vector<Type*> intersections;
	for (size_t i = 0; i < count; i++) {
		std::vector<Type*> constituents = types;
		size_t n = i;
		for (size_t j = types.size(); j > 0; j--) {
			if (types[j - 1]->flags & TypeFlagsUnion) {
				const std::vector<Type*>& sourceTypes = types[j - 1]->types();
				size_t length = sourceTypes.size();
				constituents[j - 1] = sourceTypes[n % length];
				n = n / length;
			}
		}
		Type* t = getIntersectionTypeEx(constituents, flags, nullptr);
		if (!(t->flags & TypeFlagsNever)) {
			intersections.push_back(t);
		}
	}
	return intersections;
}

static int getConstituentCount(Type* t) {
	if (!(t->flags & TypeFlagsUnionOrIntersection) || t->alias != nullptr) {
		return 1;
	}
	if ((t->flags & TypeFlagsUnion) && t->AsUnionType()->origin != nullptr) {
		return getConstituentCount(t->AsUnionType()->origin);
	}
	int n = 0;
	for (Type* u : t->types()) {
		n += getConstituentCount(u);
	}
	return n;
}

static int getConstituentCountOfTypes(const std::vector<Type*>& types) {
	int n = 0;
	for (Type* t : types) {
		n += getConstituentCount(t);
	}
	return n;
}

void Checker::filterTypes(std::vector<Type*>& types,
	const std::function<bool(Type*)>& predicate) {
	for (size_t i = 0; i < types.size(); i++) {
		types[i] = filterType(types[i], predicate);
	}
}

bool Checker::IsEmptyAnonymousObjectType(Type* t) {
	return (t->objectFlags & ObjectFlagsAnonymous) &&
		(((t->objectFlags & ObjectFlagsMembersResolved) &&
		  isEmptyResolvedType(t->AsStructuredType())) ||
			(t->symbol != nullptr && (t->symbol->flags & SymbolFlagsTypeLiteral) &&
				getMembersOfSymbol(t->symbol).empty()));
}

bool Checker::isEmptyResolvedType(StructuredType* t) {
	return t->AsType() != anyFunctionType && t->properties.empty() &&
		t->signatures.empty() && t->indexInfos.empty();
}

bool Checker::isEmptyObjectType(Type* t) {
	if (t->flags & TypeFlagsObject) {
		return !isGenericMappedType(t) && isEmptyResolvedType(resolveStructuredTypeMembers(t));
	}
	if (t->flags & TypeFlagsNonPrimitive) {
		return true;
	}
	if (t->flags & TypeFlagsUnion) {
		return someType(t, [this](Type* u) { return isEmptyObjectType(u); });
	}
	if (t->flags & TypeFlagsIntersection) {
		return everyType(t, [this](Type* u) { return isEmptyObjectType(u); });
	}
	return false;
}

bool Checker::isPatternLiteralPlaceholderType(Type* t) {
	if (t->flags & TypeFlagsIntersection) {
		// Return true if the intersection consists of one or more placeholders and zero or
		// more object type tags.
		bool seenPlaceholder = false;
		for (Type* s : t->types()) {
			if ((s->flags & (TypeFlagsLiteral | TypeFlagsNullable)) ||
				isPatternLiteralPlaceholderType(s)) {
				seenPlaceholder = true;
			} else if (!(s->flags & TypeFlagsObject)) {
				return false;
			}
		}
		return seenPlaceholder;
	}
	return (t->flags & (TypeFlagsAny | TypeFlagsString | TypeFlagsNumber | TypeFlagsBigInt)) ||
		isPatternLiteralType(t);
}

bool Checker::isPatternLiteralType(Type* t) {
	// A pattern literal type is a template literal or a string mapping type that contains
	// only non-generic pattern literal placeholders.
	if ((t->flags & TypeFlagsTemplateLiteral) &&
		everyList(t->AsTemplateLiteralType()->types,
			[this](Type* u) { return isPatternLiteralPlaceholderType(u); })) {
		return true;
	}
	return (t->flags & TypeFlagsStringMapping) &&
		isPatternLiteralPlaceholderType(t->AsStringMappingType()->target);
}

bool Checker::isGenericStringLikeType(Type* t) {
	return (t->flags & (TypeFlagsTemplateLiteral | TypeFlagsStringMapping)) &&
		!isPatternLiteralType(t);
}

bool Checker::checkCrossProductUnion(const std::vector<Type*>& types) {
	bool ok = getCrossProductUnionSize(types) < 100000;
	if (!ok) {
		error(currentNode, Expression_produces_a_union_type_that_is_too_complex_to_represent);
	}
	return ok;
}

int Checker::getCrossProductUnionSize(const std::vector<Type*>& types) {
	size_t size = 1;
	for (Type* t : types) {
		if (t->flags & TypeFlagsUnion) {
			size *= t->types().size();
		}
	}
	return static_cast<int>(size);
}

// ---------------------------------------------------------------------------
// Template literal & string mapping types
// ---------------------------------------------------------------------------

static CacheKey getTemplateTypeKey(const std::vector<std::string>& texts,
	const std::vector<Type*>& types) {
	keyBuilder b;
	for (const std::string& t : texts) {
		b.writeString(t);
		b.writeByte(0);
	}
	b.writeTypes(types);
	return b.hash();
}

Type* Checker::getTemplateLiteralType(const std::vector<std::string>& texts,
	const std::vector<Type*>& types) {
	int unionIndex = -1;
	for (size_t i = 0; i < types.size(); i++) {
		if (types[i]->flags & (TypeFlagsNever | TypeFlagsUnion)) {
			unionIndex = static_cast<int>(i);
			break;
		}
	}
	if (unionIndex >= 0) {
		if (!checkCrossProductUnion(types)) {
			return errorType;
		}
		return mapType(types[unionIndex], [&](Type* t) {
			std::vector<Type*> replaced = types;
			replaced[unionIndex] = t;
			return getTemplateLiteralType(texts, replaced);
		});
	}
	if (std::find(types.begin(), types.end(), wildcardType) != types.end()) {
		return wildcardType;
	}
	std::vector<Type*> newTypes;
	std::vector<std::string> newTexts;
	std::string sb = texts[0];
	size_t textLength = 0; // combined length of the segments already moved into newTexts
	bool tooLarge = false;
	std::function<bool(const std::vector<std::string>&, const std::vector<Type*>&)> addSpans =
		[&](const std::vector<std::string>& texts,
			const std::vector<Type*>& types) -> bool {
		for (size_t i = 0; i < types.size(); i++) {
			Type* t = types[i];
			if (t->flags & (TypeFlagsLiteral | TypeFlagsNull | TypeFlagsUndefined)) {
				sb += getTemplateStringForType(t);
				sb += texts[i + 1];
			} else if (t->flags & TypeFlagsTemplateLiteral) {
				sb += t->AsTemplateLiteralType()->texts[0];
				if (!addSpans(t->AsTemplateLiteralType()->texts,
						t->AsTemplateLiteralType()->types)) {
					return false;
				}
				sb += texts[i + 1];
			} else if (isGenericIndexType(t) || isPatternLiteralPlaceholderType(t)) {
				newTypes.push_back(t);
				newTexts.push_back(stringutil::CombineSurrogatePairs(sb));
				textLength += sb.size();
				sb.clear();
				sb += texts[i + 1];
			} else {
				return false;
			}
			if (textLength + sb.size() > maxTemplateLiteralTypeLength ||
				(int)newTypes.size() > maxTemplateLiteralTypeSpans) {
				tooLarge = true;
				return false;
			}
		}
		return true;
	};
	if (!addSpans(texts, types)) {
		if (tooLarge) {
			error(currentNode, Type_instantiation_is_excessively_deep_and_possibly_infinite);
			return errorType;
		}
		return stringType;
	}
	if (newTypes.empty()) {
		return getStringLiteralType(stringutil::CombineSurrogatePairs(sb));
	}
	newTexts.push_back(stringutil::CombineSurrogatePairs(sb));
	if (std::all_of(newTexts.begin(), newTexts.end(),
			[](const std::string& t) { return t.empty(); })) {
		if (std::all_of(newTypes.begin(), newTypes.end(),
				[](Type* t) { return (t->flags & TypeFlagsString) != 0; })) {
			return stringType;
		}
		// Normalize `${Mapping<xxx>}` into Mapping<xxx>
		if (newTypes.size() == 1 && isPatternLiteralType(newTypes[0])) {
			return newTypes[0];
		}
	}
	CacheKey key = getTemplateTypeKey(newTexts, newTypes);
	Type* t = templateLiteralTypes[key];
	if (t == nullptr) {
		t = newTemplateLiteralType(newTexts, newTypes);
		templateLiteralTypes[key] = t;
	}
	return t;
}

std::string Checker::getTemplateStringForType(Type* t) {
	if (t->flags & (TypeFlagsStringLiteral | TypeFlagsNumberLiteral |
			TypeFlagsBooleanLiteral | TypeFlagsBigIntLiteral)) {
		return anyToString(t->AsLiteralType()->value);
	}
	if (t->flags & TypeFlagsNullable) {
		return t->AsIntrinsicType()->intrinsicName;
	}
	return "";
}

Type* Checker::getStringMappingType(Symbol* symbol, Type* t) {
	if (t->flags & (TypeFlagsUnion | TypeFlagsNever)) {
		return mapType(t, [&](Type* u) { return getStringMappingType(symbol, u); });
	}
	if (t->flags & TypeFlagsStringLiteral) {
		return getStringLiteralType(applyStringMapping(symbol, getStringLiteralValue(t)));
	}
	if (t->flags & TypeFlagsTemplateLiteral) {
		auto [newTexts, newTypes] = applyTemplateStringMapping(
			symbol, t->AsTemplateLiteralType()->texts, t->AsTemplateLiteralType()->types);
		return getTemplateLiteralType(newTexts, newTypes);
	}
	if ((t->flags & TypeFlagsStringMapping) && symbol == t->symbol) {
		return t;
	}
	if ((t->flags & (TypeFlagsAny | TypeFlagsString | TypeFlagsStringMapping)) ||
		isGenericIndexType(t)) {
		return getStringMappingTypeForGenericType(symbol, t);
	}
	if (isPatternLiteralPlaceholderType(t)) {
		return getStringMappingTypeForGenericType(symbol,
			getTemplateLiteralType({"", ""}, {t}));
	}
	return t;
}

std::string Checker::applyStringMapping(Symbol* symbol, const std::string& str) {
	auto it = intrinsicTypeKinds.find(symbol->name);
	if (it == intrinsicTypeKinds.end()) {
		return str;
	}
	switch (it->second) {
		case IntrinsicTypeKind::Uppercase:
			return stringutil::ToUpperJS(str);
		case IntrinsicTypeKind::Lowercase:
			return stringutil::ToLowerJS(str);
		case IntrinsicTypeKind::Capitalize: {
			size_t size = stringutil::DecodeJSStringRuneSize(str);
			return stringutil::ToUpperJS(str.substr(0, size)) + str.substr(size);
		}
		case IntrinsicTypeKind::Uncapitalize: {
			size_t size = stringutil::DecodeJSStringRuneSize(str);
			return stringutil::ToLowerJS(str.substr(0, size)) + str.substr(size);
		}
		default:
			return str;
	}
}

std::pair<std::vector<std::string>, std::vector<Type*>> Checker::applyTemplateStringMapping(
	Symbol* symbol, const std::vector<std::string>& texts, const std::vector<Type*>& types) {
	auto it = intrinsicTypeKinds.find(symbol->name);
	if (it == intrinsicTypeKinds.end()) {
		return {texts, types};
	}
	switch (it->second) {
		case IntrinsicTypeKind::Uppercase:
		case IntrinsicTypeKind::Lowercase: {
			std::vector<std::string> mappedTexts;
			mappedTexts.reserve(texts.size());
			for (const std::string& t : texts) {
				mappedTexts.push_back(applyStringMapping(symbol, t));
			}
			std::vector<Type*> mappedTypes;
			mappedTypes.reserve(types.size());
			for (Type* t : types) {
				mappedTypes.push_back(getStringMappingType(symbol, t));
			}
			return {mappedTexts, mappedTypes};
		}
		case IntrinsicTypeKind::Capitalize:
		case IntrinsicTypeKind::Uncapitalize:
			if (!texts[0].empty()) {
				std::vector<std::string> newTexts = texts;
				newTexts[0] = applyStringMapping(symbol, newTexts[0]);
				return {newTexts, types};
			}
			{
				std::vector<Type*> newTypes = types;
				newTypes[0] = getStringMappingType(symbol, newTypes[0]);
				return {texts, newTypes};
			}
		default:
			return {texts, types};
	}
}


// ---------------------------------------------------------------------------
// Diagnostics (checker.go:14220-14460 area)
// ---------------------------------------------------------------------------

Diagnostic* NewDiagnosticForNode(Node* node, const DiagnosticMessage* message,
							   const std::vector<std::string>& args = {}) {
	SourceFile* file = nullptr;
	TextRange loc;
	if (node != nullptr) {
		file = getSourceFileOfNode(node);
		loc = getErrorRangeForNode(file, node);
	}
	return newDiagnostic(file, loc, message, args);
}

Diagnostic* NewDiagnosticChainForNode(Diagnostic* chain, Node* node,
									const DiagnosticMessage* message,
									const std::vector<std::string>& args = {}) {
	if (chain != nullptr) {
		return newDiagnosticChain(chain, message, args);
	}
	return NewDiagnosticForNode(node, message, args);
}

Diagnostic* Checker::addDiagnostic(Diagnostic* diagnostic) {
	// Discard diagnostics created while at the maximum number of recursive TypeToString invocations.
	if (serializationLevel < maxSerializationLevel) {
		return diagnostics.Add(diagnostic);
	}
	return diagnostic;
}

Diagnostic* Checker::addSuggestionDiagnostic(Diagnostic* diagnostic) {
	if (serializationLevel < maxSerializationLevel) {
		return suggestionDiagnostics.Add(diagnostic);
	}
	return diagnostic;
}

Diagnostic* Checker::error(Node* location, const DiagnosticMessage* message,
						   std::vector<std::string> args) {
	return addDiagnostic(NewDiagnosticForNode(location, message, args));
}

Diagnostic* Checker::error(Node* location, const DiagnosticMessage* message, std::string arg) {
	return error(location, message, {std::move(arg)});
}

Diagnostic* Checker::errorSkippedOnNoEmit(Node* location, const DiagnosticMessage* message,
										std::vector<std::string> args) {
	Diagnostic* diagnostic = error(location, message, std::move(args));
	diagnostic->SetSkippedOnNoEmit();
	return diagnostic;
}

void Checker::errorOrSuggestion(bool isError, Node* location, const DiagnosticMessage* message,
								std::vector<std::string> args) {
	addErrorOrSuggestion(isError, NewDiagnosticForNode(location, message, std::move(args)));
}

Diagnostic* Checker::errorAndMaybeSuggestAwait(Node* location, bool maybeMissingAwait,
											 const DiagnosticMessage* message,
											 std::vector<std::string> args) {
	Diagnostic* diagnostic = error(location, message, std::move(args));
	if (maybeMissingAwait) {
		diagnostic->AddRelatedInfo(createDiagnosticForNode(location, Did_you_forget_to_use_await));
	}
	return diagnostic;
}

void Checker::addErrorOrSuggestion(bool isError, Diagnostic* diagnostic) {
	if (isError) {
		addDiagnostic(diagnostic);
	} else {
		addSuggestionDiagnostic(diagnostic);
	}
}

Diagnostic* Checker::lookupOrIssueError(Node* location, const DiagnosticMessage* message,
										const std::vector<std::string>& args) {
	return addDiagnostic(NewDiagnosticForNode(location, message, args));
}

static Node* getAdjustedNodeForError(Node* node) {
	if (Node* name = getNameOfDeclaration(node); name != nullptr) {
		return name;
	}
	return node;
}

void Checker::mergeSymbolTable(SymbolTable& target, const SymbolTable& source,
							   bool unidirectional, Symbol* mergedParent) {
	for (auto& [id, sourceSymbol] : source) {
		auto it = target.find(id);
		Symbol* targetSymbol = it == target.end() ? nullptr : it->second;
		Symbol* merged;
		if (targetSymbol != nullptr) {
			merged = mergeSymbol(targetSymbol, sourceSymbol, unidirectional);
		} else {
			merged = getMergedSymbol(sourceSymbol);
		}
		if (mergedParent != nullptr && targetSymbol != nullptr) {
			// If a merge was performed on the target symbol, set its parent to the merged parent that initiated the merge
			// of its exports. Otherwise, `merged` came only from `sourceSymbol` and can keep its parent:
			//
			// // a.ts
			// export interface A { x: number; }
			//
			// // b.ts
			// declare module "./a" {
			//   interface A { y: number; }
			//   interface B {}
			// }
			//
			// When merging the module augmentation into a.ts, the symbol for `A` will itself be merged, so its parent
			// should be the merged module symbol. But the symbol for `B` has only one declaration, so its parent should
			// be the module augmentation symbol, which contains its only declaration.
			if (merged->flags & SymbolFlagsTransient) {
				merged->parent = mergedParent;
			}
		}
		target[id] = merged;
	}
}

// Note: if target is transient, then it is mutable, and mergeSymbol with both mutate and return it.
// If target is not transient, mergeSymbol will produce a transient clone, mutate that and return it.
Symbol* Checker::mergeSymbol(Symbol* target, Symbol* source, bool unidirectional) {
	if ((target->flags & getExcludedSymbolFlags(source->flags)) == 0 ||
		(source->flags | target->flags) & SymbolFlagsAssignment) {
		if (source == target) {
			// This can happen when an export assigned namespace exports something also erroneously exported at the top level
			return target;
		}
		if (!(target->flags & SymbolFlagsTransient)) {
			Symbol* resolvedTarget = resolveSymbol(target);
			if (resolvedTarget == unknownSymbol) {
				return source;
			}
			if ((resolvedTarget->flags & getExcludedSymbolFlags(source->flags)) == 0 ||
				(source->flags | resolvedTarget->flags) & SymbolFlagsAssignment) {
				target = cloneSymbol(resolvedTarget);
			} else {
				reportMergeSymbolError(target, source);
				return source;
			}
		}
		// Javascript static-property-assignment declarations always merge, even though they are also values
		if ((source->flags & SymbolFlagsValueModule) && (target->flags & SymbolFlagsValueModule) &&
			(target->flags & SymbolFlagsConstEnumOnlyModule) &&
			!(source->flags & SymbolFlagsConstEnumOnlyModule)) {
			// reset flag when merging instantiated module into value module that has only const enums
			target->flags &= ~SymbolFlagsConstEnumOnlyModule;
		}
		SymbolFlags sourceFlags = source->flags;
		if (!(target->flags & SymbolFlagsConstEnumOnlyModule)) {
			sourceFlags &= ~SymbolFlagsConstEnumOnlyModule;
		}
		target->flags |= sourceFlags;
		if (source->valueDeclaration != nullptr) {
			setValueDeclaration(target, source->valueDeclaration);
		}
		target->declarations.insert(target->declarations.end(), source->declarations.begin(),
									source->declarations.end());
		if (!source->members.empty()) {
			mergeSymbolTable(getSymbolTable(target->members), source->members, unidirectional, nullptr);
		}
		if (!source->exports.empty()) {
			mergeSymbolTable(getSymbolTable(target->exports), source->exports, unidirectional, target);
		}
		if (!unidirectional) {
			recordMergedSymbol(target, source);
		}
	} else if (target->flags & SymbolFlagsNamespaceModule) {
		// Do not report an error when merging `var globalThis` with the built-in `globalThis`,
		// as we will already report a "Declaration name conflicts..." error, and this error
		// won't make much sense.
		if (target != globalThisSymbol) {
			error(getNameOfDeclaration(getFirstDeclaration(source)),
				  Cannot_augment_module_0_with_value_exports_because_it_resolves_to_a_non_module_entity,
				  {symbolToString(target)});
		}
	} else {
		reportMergeSymbolError(target, source);
	}
	return target;
}

void Checker::reportMergeSymbolError(Symbol* target, Symbol* source) {
	bool isEitherEnum = (target->flags & SymbolFlagsEnum) || (source->flags & SymbolFlagsEnum);
	bool isEitherBlockScoped =
		(target->flags & SymbolFlagsBlockScopedVariable) || (source->flags & SymbolFlagsBlockScopedVariable);
	const DiagnosticMessage* message;
	if (isEitherEnum) {
		message = Enum_declarations_can_only_merge_with_namespace_or_other_enum_declarations;
	} else if (isEitherBlockScoped) {
		message = Cannot_redeclare_block_scoped_variable_0;
	} else {
		message = Duplicate_identifier_0;
	}
	SourceFile* sourceSymbolFile = getSourceFileOfNode(getFirstDeclaration(source));
	SourceFile* targetSymbolFile = getSourceFileOfNode(getFirstDeclaration(target));
	bool isSourcePlainJS = isPlainJSFile(sourceSymbolFile, compilerOptions->CheckJs);
	bool isTargetPlainJS = isPlainJSFile(targetSymbolFile, compilerOptions->CheckJs);
	std::string symbolName = symbolToString(source);
	if (!isSourcePlainJS) {
		addDuplicateDeclarationErrorsForSymbols(source, message, symbolName, target);
	}
	if (!isTargetPlainJS) {
		addDuplicateDeclarationErrorsForSymbols(target, message, symbolName, source);
	}
}

void Checker::addDuplicateDeclarationErrorsForSymbols(Symbol* target, const DiagnosticMessage* message,
													  const std::string& symbolName, Symbol* source) {
	for (Node* node : target->declarations) {
		addDuplicateDeclarationError(node, message, symbolName, source->declarations);
	}
}

void Checker::addDuplicateDeclarationError(Node* node, const DiagnosticMessage* message,
										   const std::string& symbolName,
										   const std::vector<Node*>& relatedNodes) {
	Node* errorNode = getAdjustedNodeForError(node);
	if (errorNode == nullptr) {
		errorNode = node;
	}
	Diagnostic* err = lookupOrIssueError(errorNode, message, {symbolName});
	for (Node* relatedNode : relatedNodes) {
		Node* adjustedNode = getAdjustedNodeForError(relatedNode);
		if (adjustedNode == errorNode) {
			continue;
		}
		Diagnostic* leadingMessage =
			NewDiagnosticForNode(adjustedNode, X_0_was_also_declared_here, {symbolName});
		Diagnostic* followOnMessage = NewDiagnosticForNode(adjustedNode, X_and_here, {});
		if (err->relatedInformation.size() >= 5 ||
			someList(err->relatedInformation, [followOnMessage, leadingMessage](Diagnostic* d) {
				return CompareDiagnostics(d, followOnMessage) == 0 ||
					   CompareDiagnostics(d, leadingMessage) == 0;
			})) {
			continue;
		}
		if (err->relatedInformation.empty()) {
			err->AddRelatedInfo(leadingMessage);
		} else {
			err->AddRelatedInfo(followOnMessage);
		}
	}
}

Symbol* Checker::cloneSymbol(Symbol* symbol) {
	Symbol* result = newSymbol(symbol->flags, symbol->name);
	result->declarations = symbol->declarations;
	result->parent = symbol->parent;
	result->valueDeclaration = symbol->valueDeclaration;
	result->members = symbol->members;
	result->exports = symbol->exports;
	recordMergedSymbol(result, symbol);
	return result;
}

Symbol* Checker::getMergedSymbol(Symbol* symbol) {
	if (symbol != nullptr) {
		auto it = mergedSymbols.find(symbol);
		if (it != mergedSymbols.end() && it->second != nullptr) {
			return it->second;
		}
	}
	return symbol;
}

Symbol* Checker::getParentOfSymbol(Symbol* symbol) {
	if (symbol->parent != nullptr) {
		return getMergedSymbol(getLateBoundSymbol(symbol->parent));
	}
	return nullptr;
}

void Checker::recordMergedSymbol(Symbol* target, Symbol* source) {
	mergedSymbols[source] = target;
}

Symbol* Checker::getSymbolIfSameReference(Symbol* s1, Symbol* s2) {
	if (getMergedSymbol(resolveSymbol(getMergedSymbol(s1))) ==
		getMergedSymbol(resolveSymbol(getMergedSymbol(s2)))) {
		return s1;
	}
	return nullptr;
}

Symbol* Checker::getExportSymbolOfValueSymbolIfExported(Symbol* symbol) {
	if (symbol != nullptr && (symbol->flags & SymbolFlagsExportValue) && symbol->exportSymbol != nullptr) {
		symbol = symbol->exportSymbol;
	}
	return getMergedSymbol(symbol);
}

Symbol* Checker::getSymbolOfDeclaration(Node* node) {
	if (Symbol* symbol = node->symbol(); symbol != nullptr) {
		return getMergedSymbol(getLateBoundSymbol(symbol));
	}
	return nullptr;
}

Symbol* Checker::getSymbolOfNode(Node* node) {
	auto data = node->declarationData();
	if (data.symbol != nullptr && *data.symbol != nullptr) {
		return getMergedSymbol(getLateBoundSymbol(*data.symbol));
	}
	return nullptr;
}

static bool isTypeUsableAsPropertyName(Type* t);
static bool isLateBindableAST(Node* node);
static std::string getPropertyNameFromType(Type* t);
static std::vector<Node*> getMembersOfDeclaration(Node* node);
struct ExportCollision {
	std::string specifierText;
	std::vector<Node*> exportsWithDuplicate;
};
using ExportCollisionTable = std::unordered_map<std::string, ExportCollision*>;
static void extendExportSymbols(Checker* c, SymbolTable& target, const SymbolTable& source,
								ExportCollisionTable* lookupTable, Node* exportNode);

Symbol* Checker::getLateBoundSymbol(Symbol* symbol) {
	if (!(symbol->flags & SymbolFlagsClassMember) || symbol->name != InternalSymbolNameComputed) {
		return symbol;
	}
	LateBoundLinks* links = lateBoundLinks.Get(symbol);
	if (links->lateSymbol == nullptr &&
		someList(symbol->declarations, [this](Node* d) { return hasLateBindableName(d); })) {
		// force late binding of members/exports. This will set the late-bound symbol
		Symbol* parent = getMergedSymbol(symbol->parent);
		if (someList(symbol->declarations, hasStaticModifier)) {
			getExportsOfSymbol(parent);
		} else {
			getMembersOfSymbol(parent);
		}
	}
	if (links->lateSymbol == nullptr) {
		links->lateSymbol = symbol;
	}
	return links->lateSymbol;
}

Symbol* Checker::resolveSymbol(Symbol* symbol) {
	return resolveSymbolEx(symbol, false /*dontResolveAlias*/);
}

Symbol* Checker::resolveSymbolEx(Symbol* symbol, bool dontResolveAlias) {
	if (!dontResolveAlias &&
		isNonLocalAlias(symbol, SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace)) {
		return resolveAlias(symbol);
	}
	return symbol;
}

bool Checker::hasLateBindableName(Node* node) {
	Node* name = getNameOfDeclaration(node);
	return name != nullptr && isLateBindableName(name);
}

// Indicates whether a declaration name is definitely late-bindable.
// A declaration name is only late-bindable if:
// - It is a `ComputedPropertyName`.
// - Its expression is an `Identifier` or either a `PropertyAccessExpression` an
// `ElementAccessExpression` consisting only of these same three types of nodes.
// - The type of its expression is a string or numeric literal type, or is a `unique symbol` type.
bool Checker::isLateBindableName(Node* node) {
	if (!isLateBindableAST(node)) {
		return false;
	}
	if (isComputedPropertyName(node)) {
		return isTypeUsableAsPropertyName(checkComputedPropertyName(node));
	}
	return isTypeUsableAsPropertyName(
		checkExpressionCached(node->as<ElementAccessExpression>()->ArgumentExpression));
}

bool Checker::hasLateBindableIndexSignature(Node* node) {
	Node* name = getNameOfDeclaration(node);
	return name != nullptr && isLateBindableIndexSignature(name);
}

bool Checker::isLateBindableIndexSignature(Node* node) {
	if (!isLateBindableAST(node)) {
		return false;
	}
	if (isComputedPropertyName(node)) {
		return isTypeUsableAsIndexSignatureDeclaration(checkComputedPropertyName(node));
	}
	return isTypeUsableAsIndexSignatureDeclaration(
		checkExpressionCached(node->as<ElementAccessExpression>()->ArgumentExpression));
}

bool Checker::isTypeUsableAsIndexSignatureDeclaration(Type* t) {
	return isTypeAssignableTo(t, stringNumberSymbolType);
}

static bool isTypeUsableAsPropertyName(Type* t) {
	return t->flags & TypeFlagsStringOrNumberLiteralOrUnique;
}

static bool isLateBindableAST(Node* node) {
	Node* expr = nullptr;
	if (isComputedPropertyName(node)) {
		expr = node->expression();
	} else if (isElementAccessExpression(node)) {
		expr = node->as<ElementAccessExpression>()->ArgumentExpression;
	}
	return expr != nullptr && isEntityNameExpression(expr);
}

static std::vector<Node*> getMembersOfDeclaration(Node* node) {
	switch (node->kind) {
	case Kind::InterfaceDeclaration:
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::TypeLiteral:
		return node->members();
	case Kind::ObjectLiteralExpression:
		return node->properties();
	default:
		break;
	}
	return {};
}

// Gets the symbolic name for a member from its type.
static std::string getPropertyNameFromType(Type* t) {
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

SymbolTable Checker::getExportsOfSymbol(Symbol* symbol) {
	if (symbol->flags & SymbolFlagsLateBindingContainer) {
		return getResolvedMembersOrExportsOfSymbol(symbol, MembersOrExportsResolutionKindResolvedExports);
	}
	if (symbol->flags & SymbolFlagsModule) {
		return getExportsOfModule(symbol);
	}
	return symbol->exports;
}

SymbolTable Checker::getResolvedMembersOrExportsOfSymbol(Symbol* symbol,
														 MembersOrExportsResolutionKind resolutionKind) {
	auto* links = membersAndExportsLinks.Get(symbol);
	if (links->at(resolutionKind).empty()) {
		bool isStatic = resolutionKind == MembersOrExportsResolutionKindResolvedExports;
		SymbolTable earlySymbols = symbol->exports;
		if (!isStatic) {
			earlySymbols = symbol->members;
		} else if (symbol->flags & SymbolFlagsModule) {
			earlySymbols = std::get<0>(getExportsOfModuleWorker(symbol));
		}
		(*links)[resolutionKind] = earlySymbols;
		// fill in any as-yet-unresolved late-bound members.
		SymbolTable lateSymbols;
		for (Node* decl : symbol->declarations) {
			for (Node* member : getMembersOfDeclaration(decl)) {
				if (isStatic == static_cast<bool>(hasStaticModifier(member))) {
					if (hasLateBindableName(member)) {
						lateBindMember(symbol, earlySymbols, lateSymbols, member);
					} else if (hasLateBindableIndexSignature(member)) {
						lateBindIndexSignature(symbol, earlySymbols, lateSymbols, member);
					}
				}
			}
		}
		if (isStatic) {
			auto it = symbol->exports.find(InternalSymbolNameAssignmentDeclaration);
			if (it != symbol->exports.end()) {
				Symbol* assignmentSymbol = it->second;
				for (Node* member : assignmentSymbol->declarations) {
					if (hasLateBindableName(member)) {
						lateBindMember(symbol, earlySymbols, lateSymbols, member);
					}
				}
			}
		}
		(*links)[resolutionKind] = combineSymbolTables(earlySymbols, lateSymbols);
	}
	return links->at(resolutionKind);
}

// Performs late-binding of a dynamic member. This performs the same function for
// late-bound members that `declareSymbol` in binder.ts performs for early-bound
// members.
Symbol* Checker::lateBindMember(Symbol* parent, SymbolTable& earlySymbols, SymbolTable& lateSymbols,
								Node* decl) {
	TSC_ASSERT(decl->symbol() != nullptr, "The member is expected to have a symbol.");
	auto* links = symbolNodeLinks.Get(decl);
	if (links->resolvedSymbol == nullptr) {
		// In the event we attempt to resolve the late-bound name of this member recursively,
		// fall back to the early-bound name of this member.
		links->resolvedSymbol = decl->symbol();
		Node* declName;
		if (isBinaryExpression(decl)) {
			declName = decl->as<BinaryExpression>()->Left;
		} else {
			declName = decl->name();
		}
		Type* t;
		if (isElementAccessExpression(declName)) {
			t = checkExpressionCached(declName->as<ElementAccessExpression>()->ArgumentExpression);
		} else {
			t = checkComputedPropertyName(declName);
		}
		if (isTypeUsableAsPropertyName(t)) {
			std::string memberName = getPropertyNameFromType(t);
			SymbolFlags symbolFlags = decl->symbol()->flags;
			// Get or add a late-bound symbol for the member. This allows us to merge late-bound accessor declarations.
			Symbol* lateSymbol = nullptr;
			if (auto it = lateSymbols.find(memberName); it != lateSymbols.end()) {
				lateSymbol = it->second;
			}
			if (lateSymbol == nullptr) {
				lateSymbol = newSymbolEx(SymbolFlagsNone, memberName, CheckFlagsLate);
				lateSymbols[memberName] = lateSymbol;
			}
			// Report an error if there's a symbol declaration with the same name and conflicting flags.
			Symbol* earlySymbol = nullptr;
			if (auto it = earlySymbols.find(memberName); it != earlySymbols.end()) {
				earlySymbol = it->second;
			}
			if (lateSymbol->flags & getExcludedSymbolFlags(symbolFlags)) {
				// If we have an existing early-bound member, combine its declarations so that we can
				// report an error at each declaration.
				std::vector<Node*> declarations;
				if (earlySymbol != nullptr) {
					declarations = earlySymbol->declarations;
					declarations.insert(declarations.end(), lateSymbol->declarations.begin(),
										lateSymbol->declarations.end());
				} else {
					declarations = lateSymbol->declarations;
				}
				std::string name = memberName;
				if (t->flags & TypeFlagsUniqueESSymbol) {
					name = declarationNameToString(declName);
				}
				for (Node* d : declarations) {
					Node* errorNode = getNameOfDeclaration(d);
					error(errorNode != nullptr ? errorNode : d, Duplicate_identifier_0, {name});
				}
				error(declName != nullptr ? declName : decl, Duplicate_identifier_0, {name});
				if ((lateSymbol->flags & SymbolFlagsAccessor) &&
					(lateSymbol->flags & SymbolFlagsAccessor) != (symbolFlags & SymbolFlagsAccessor)) {
					lateSymbol->flags |= SymbolFlagsAccessor;
				}
				lateSymbol = newSymbolEx(SymbolFlagsNone, memberName, CheckFlagsLate);
			}
			valueSymbolLinks.Get(lateSymbol)->nameType = t;
			addDeclarationToLateBoundSymbol(lateSymbol, decl, symbolFlags);
			if (lateSymbol->parent == nullptr) {
				lateSymbol->parent = parent;
			}
			links->resolvedSymbol = lateSymbol;
		}
	}
	return links->resolvedSymbol;
}

void Checker::lateBindIndexSignature(Symbol* parent, SymbolTable& earlySymbols,
									 SymbolTable& lateSymbols, Node* decl) {
	(void)parent;
	// First, late bind the index symbol itself, if needed
	Symbol* indexSymbol = nullptr;
	if (auto it = lateSymbols.find(InternalSymbolNameIndex); it != lateSymbols.end()) {
		indexSymbol = it->second;
	}
	if (indexSymbol == nullptr) {
		Symbol* early = nullptr;
		if (auto it = earlySymbols.find(InternalSymbolNameIndex); it != earlySymbols.end()) {
			early = it->second;
		}
		if (early == nullptr) {
			indexSymbol = newSymbolEx(SymbolFlagsNone, InternalSymbolNameIndex, CheckFlagsLate);
		} else {
			indexSymbol = cloneSymbol(early);
			indexSymbol->checkFlags |= CheckFlagsLate;
		}
		lateSymbols[InternalSymbolNameIndex] = indexSymbol;
	}
	// Then just add the computed name as a late bound declaration
	// (note: unlike `addDeclarationToLateBoundSymbol` we do not set up a `.lateSymbol` on `decl`'s links,
	// since that would point at an index symbol and not a single property symbol, like most consumers would expect)
	if (indexSymbol->declarations.empty() ||
		!(decl->symbol()->flags & SymbolFlagsReplaceableByMethod)) {
		indexSymbol->declarations.push_back(decl);
	}
}

static bool isNotReplacableByMethod(Node* decl) {
	return !(decl->symbol()->flags & SymbolFlagsReplaceableByMethod);
}

// Adds a declaration to a late-bound dynamic member. This performs the same function for
// late-bound members that `addDeclarationToSymbol` in binder.ts performs for early-bound
// members.
void Checker::addDeclarationToLateBoundSymbol(Symbol* symbol, Node* member, SymbolFlags symbolFlags) {
	TSC_ASSERT(symbol->checkFlags & CheckFlagsLate, "Expected a late-bound symbol.");
	lateBoundLinks.Get(member->symbol())->lateSymbol = symbol;
	if (symbol->declarations.empty() ||
		!(member->symbol()->flags & SymbolFlagsReplaceableByMethod)) {
		symbol->flags |= symbolFlags;
		symbol->declarations.push_back(member);
	} else if ((symbol->flags & SymbolFlagsReplaceableByMethod) &&
			   (member->symbol()->flags & SymbolFlagsMethod)) {
		// Remove all replacable-by-method members, along with their flags.
		symbol->declarations.erase(
			std::remove_if(symbol->declarations.begin(), symbol->declarations.end(), isNotReplacableByMethod),
			symbol->declarations.end());
		symbol->declarations.push_back(member);
		SymbolFlags oldFlags = symbol->flags;
		symbol->flags = SymbolFlagsNone;
		for (Node* d : symbol->declarations) {
			symbol->flags |= d->symbol()->flags;
		}
		if (oldFlags & SymbolFlagsAccessor) {
			symbol->flags |= SymbolFlagsAccessor;
		}
	}
	if (symbolFlags & SymbolFlagsValue) {
		setValueDeclaration(symbol, member);
	}
}

// Gets a SymbolTable containing both the early- and late-bound members of a symbol.
SymbolTable Checker::getMembersOfSymbol(Symbol* symbol) {
	if (symbol->flags & SymbolFlagsLateBindingContainer) {
		return getResolvedMembersOrExportsOfSymbol(symbol, MembersOrExportsResolutionKindResolvedMembers);
	}
	return symbol->members;
}

SymbolTable Checker::getExportsOfModule(Symbol* moduleSymbol) {
	auto* links = moduleSymbolLinks.Get(moduleSymbol);
	if (links->resolvedExports.empty()) {
		auto [exports, typeOnlyExportStarMap] = getExportsOfModuleWorker(moduleSymbol);
		links->resolvedExports = exports;
		links->typeOnlyExportStarMap = typeOnlyExportStarMap;
	}
	return links->resolvedExports;
}


std::pair<SymbolTable, std::unordered_map<std::string, Node*>>
Checker::getExportsOfModuleWorker(Symbol* moduleSymbol) {
	std::vector<Symbol*> visitedSymbols;
	OrderedSet<std::string> nonTypeOnlyNames;
	// The ES6 spec permits export * declarations in a module to circularly reference the module itself. For example,
	// module 'a' can 'export * from "b"' and 'b' can 'export * from "a"' without error.
	std::unordered_map<std::string, Node*> typeOnlyExportStarMap;
	std::function<SymbolTable(Symbol*, Node*, bool)> visit =
		[&](Symbol* symbol, Node* exportStar, bool isTypeOnly) -> SymbolTable {
		if (!isTypeOnly && symbol != nullptr) {
			// Add non-type-only names before checking if we've visited this module,
			// because we might have visited it via an 'export type *', and visiting
			// again with 'export *' will override the type-onlyness of its exports.
			for (auto& [name, _] : symbol->exports) {
				nonTypeOnlyNames.Add(name);
			}
		}
		if (symbol == nullptr || symbol->exports.empty() ||
			std::find(visitedSymbols.begin(), visitedSymbols.end(), symbol) != visitedSymbols.end()) {
			return {};
		}
		visitedSymbols.push_back(symbol);
		SymbolTable symbols = symbol->exports;
		// All export * declarations are collected in an __export symbol by the binder
		Symbol* exportStars = nullptr;
		if (auto it = symbol->exports.find(InternalSymbolNameExportStar); it != symbol->exports.end()) {
			exportStars = it->second;
		}
		if (exportStars != nullptr) {
			SymbolTable nestedSymbols;
			ExportCollisionTable lookupTable;
			for (Node* node : exportStars->declarations) {
				Symbol* resolvedModule = resolveExternalModuleName(
					node, node->moduleSpecifier(), false /*ignoreErrors*/,
					getTypeFromImportAttributes(getImportAttributes(node)));
				SymbolTable exportedSymbols = visit(resolvedModule, node, isTypeOnly || node->isTypeOnly());
				extendExportSymbols(this, nestedSymbols, exportedSymbols, &lookupTable, node);
			}
			for (auto& [id, coll] : lookupTable) {
				// It's not an error if the file with multiple `export *`s with duplicate names exports a member with that name itself
				if (id == InternalSymbolNameExportEquals || coll->exportsWithDuplicate.empty() ||
					symbols.count(id) != 0) {
					continue;
				}
				for (Node* node : coll->exportsWithDuplicate) {
					addDiagnostic(NewDiagnosticForNode(
						node,
						Module_0_has_already_exported_a_member_named_1_Consider_explicitly_re_exporting_to_resolve_the_ambiguity,
						{coll->specifierText, id}));
				}
			}
			extendExportSymbols(this, symbols, nestedSymbols, nullptr, nullptr);
		}
		if (exportStar != nullptr && exportStar->isTypeOnly()) {
			for (auto& [name, _] : symbols) {
				typeOnlyExportStarMap[name] = exportStar;
			}
		}
		return symbols;
	};
	Symbol* originalModule = nullptr;
	if (moduleSymbol != nullptr) {
		auto it = moduleSymbol->exports.find(InternalSymbolNameExportEquals);
		if (it != moduleSymbol->exports.end() &&
			resolveSymbolEx(it->second, false /*dontResolveAlias*/) != nullptr) {
			originalModule = moduleSymbol;
		}
	}
	// A module defined by an 'export=' consists of one export that needs to be resolved
	moduleSymbol = resolveExternalModuleSymbol(moduleSymbol, false /*dontResolveAlias*/);
	SymbolTable exports = visit(moduleSymbol, nullptr, false);
	if (exports.empty()) {
		exports = SymbolTable{};
	}
	// A CommonJS module defined by an 'export=' might also export typedefs, stored on the original module
	if (originalModule != nullptr && originalModule->exports.size() > 1) {
		for (auto& [name, symbol] : originalModule->exports) {
			if (symbol->name == InternalSymbolNameExportEquals ||
				symbol->name == InternalSymbolNameExportStar) {
				continue;
			}
			SymbolFlags flags = getSymbolFlags(symbol);
			if ((flags & (SymbolFlagsType | SymbolFlagsNamespace)) &&
				!(flags & SymbolFlagsValue) && exports.count(symbol->name) == 0) {
				exports[symbol->name] = symbol;
			}
		}
	}
	for (auto& name : nonTypeOnlyNames.items) {
		typeOnlyExportStarMap.erase(name);
	}
	return {exports, typeOnlyExportStarMap};
}

// Extends one symbol table with another while collecting information on name collisions for error message generation into the `lookupTable` argument
// Not passing `lookupTable` and `exportNode` disables this collection, and just extends the tables
static void extendExportSymbols(Checker* c, SymbolTable& target, const SymbolTable& source,
								ExportCollisionTable* lookupTable, Node* exportNode) {
	for (auto& [id, sourceSymbol] : source) {
		if (id == InternalSymbolNameDefault) {
			continue;
		}
		Symbol* targetSymbol = nullptr;
		if (auto it = target.find(id); it != target.end()) {
			targetSymbol = it->second;
		}
		if (targetSymbol == nullptr) {
			target[id] = sourceSymbol;
			if (lookupTable != nullptr && exportNode != nullptr) {
				auto* coll = new ExportCollision();
				coll->specifierText = getTextOfNode(exportNode->moduleSpecifier());
				(*lookupTable)[id] = coll;
			}
		} else if (lookupTable != nullptr && exportNode != nullptr &&
				   c->resolveSymbol(targetSymbol) != c->resolveSymbol(sourceSymbol)) {
			(*lookupTable)[id]->exportsWithDuplicate.push_back(exportNode);
		}
	}
}

SymbolTable Checker::combineSymbolTables(const SymbolTable& first, const SymbolTable& second) {
	if (first.empty()) {
		return second;
	}
	if (second.empty()) {
		return first;
	}
	SymbolTable combined;
	mergeSymbolTable(combined, first, false, nullptr);
	mergeSymbolTable(combined, second, false, nullptr);
	return combined;
}

// Resolve an alias symbol to the first target symbol in the resolution chain that includes some other
// meaning. Pure aliases are eagerly resolved and any type-only markers are back-propagated to the original
// symbol. The function panics if the argument is not a symbol with an alias meaning.
Symbol* Checker::resolveAlias(Symbol* symbol) {
	TSC_ASSERT(symbol->flags & SymbolFlagsAlias, "Should only get alias here");
	auto* links = aliasSymbolLinks.Get(symbol);
	if (links->aliasTarget == nullptr) {
		if (!pushTypeResolution(symbol, TypeSystemPropertyName::AliasTarget)) {
			return unknownSymbol;
		}
		Node* node = getDeclarationOfAliasSymbol(symbol);
		TSC_ASSERT(node != nullptr, "Unexpected nil in resolveAlias for symbol: " + symbolToString(symbol));
		Symbol* target = getTargetOfAliasDeclaration(node);
		if (isNonLocalAlias(target, SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace)) {
			// When the target is a pure alias, we transitively resolve and propagate any typeOnlyDeclaration
			target = resolveIndirectionAlias(symbol, target);
		}
		links->aliasTarget = target != nullptr ? target : unknownSymbol;
		if (!popTypeResolution()) {
			error(node, Circular_definition_of_import_alias_0, {symbolToString(symbol)});
			links->aliasTarget = unknownSymbol;
		}
	}
	return links->aliasTarget;
}

Symbol* Checker::resolveIndirectionAlias(Symbol* source, Symbol* target) {
	Symbol* result = getMergedSymbol(resolveAlias(target));
	if (auto* targetLinks = aliasSymbolLinks.Get(target); targetLinks->typeOnlyDeclaration != nullptr) {
		if (auto* sourceLinks = aliasSymbolLinks.Get(source); sourceLinks->typeOnlyDeclaration == nullptr) {
			sourceLinks->typeOnlyDeclaration = targetLinks->typeOnlyDeclaration;
		}
	}
	return result;
}

Symbol* Checker::tryResolveAlias(Symbol* symbol) {
	auto* links = aliasSymbolLinks.Get(symbol);
	if (links->aliasTarget != nullptr ||
		findResolutionCycleStartIndex(symbol, TypeSystemPropertyName::AliasTarget) < 0) {
		return resolveAlias(symbol);
	}
	return nullptr;
}

Node* Checker::getDeclarationOfAliasSymbol(Symbol* symbol) {
	for (auto it = symbol->declarations.rbegin(); it != symbol->declarations.rend(); ++it) {
		if (isAliasSymbolDeclaration(*it)) {
			return *it;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Type-resolution cycle tracking (checker.go:19098-19175)
// ---------------------------------------------------------------------------

bool Checker::pushTypeResolution(TypeSystemEntity target, TypeSystemPropertyName propertyName) {
	int resolutionCycleStartIndex = findResolutionCycleStartIndex(target, propertyName);
	if (resolutionCycleStartIndex >= 0) {
		// A cycle was found
		for (int i = resolutionCycleStartIndex; i < (int)typeResolutions.size(); i++) {
			typeResolutions[i].result = false;
		}
		return false;
	}
	typeResolutions.push_back(TypeResolution{target, propertyName, true});
	return true;
}

// Pop an entry from the type resolution stack and return its associated result value. The result value will
// be true if no circularities were detected, or false if a circularity was found.
bool Checker::popTypeResolution() {
	size_t lastIndex = typeResolutions.size() - 1;
	bool result = typeResolutions[lastIndex].result;
	typeResolutions[lastIndex] = TypeResolution{};
	typeResolutions.pop_back();
	return result;
}

int Checker::findResolutionCycleStartIndex(TypeSystemEntity target, TypeSystemPropertyName propertyName) {
	for (int i = (int)typeResolutions.size() - 1; i >= resolutionStart; i--) {
		TypeResolution* resolution = &typeResolutions[i];
		if (typeResolutionHasProperty(resolution)) {
			return -1;
		}
		if (resolution->target == target && resolution->propertyName == propertyName) {
			return i;
		}
	}
	return -1;
}

bool Checker::typeResolutionHasProperty(TypeResolution* r) {
	switch (r->propertyName) {
	case TypeSystemPropertyName::Type:
		return valueSymbolLinks.Get(static_cast<Symbol*>(r->target))->resolvedType != nullptr;
	case TypeSystemPropertyName::DeclaredType:
		return typeAliasLinks.Get(static_cast<Symbol*>(r->target))->declaredType != nullptr;
	case TypeSystemPropertyName::ResolvedTypeArguments:
		return !static_cast<Type*>(r->target)->AsTypeReference()->resolvedTypeArguments.empty();
	case TypeSystemPropertyName::ResolvedBaseTypes:
		return static_cast<Type*>(r->target)->AsInterfaceType()->baseTypesResolved;
	case TypeSystemPropertyName::ResolvedBaseConstructorType:
		return static_cast<Type*>(r->target)->AsInterfaceType()->resolvedBaseConstructorType != nullptr;
	case TypeSystemPropertyName::ResolvedReturnType:
		return static_cast<Signature*>(r->target)->resolvedReturnType != nullptr;
	case TypeSystemPropertyName::ResolvedBaseConstraint:
		return static_cast<Type*>(r->target)->AsConstrainedType()->resolvedBaseConstraint != nullptr;
	case TypeSystemPropertyName::InitializerIsUndefined:
		return (nodeLinks.Get(static_cast<Node*>(r->target))->flags &
				NodeCheckFlagsInitializerIsUndefinedComputed) != 0;
	case TypeSystemPropertyName::WriteType:
		return valueSymbolLinks.Get(static_cast<Symbol*>(r->target))->writeType != nullptr;
	case TypeSystemPropertyName::AliasTarget:
		return aliasSymbolLinks.Get(static_cast<Symbol*>(r->target))->aliasTarget != nullptr;
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in typeResolutionHasProperty");
}

Type* Checker::reportCircularityError(Symbol* symbol) {
	Node* declaration = symbol->valueDeclaration;
	// Check if variable has type annotation that circularly references the variable itself
	if (declaration != nullptr) {
		if (declaration->type() != nullptr) {
			error(symbol->valueDeclaration,
				  X_0_is_referenced_directly_or_indirectly_in_its_own_type_annotation,
				  {symbolToString(symbol)});
			return errorType;
		}
		// Check if variable has initializer that circularly references the variable itself
		if (noImplicitAny && (!isParameterDeclaration(declaration) || declaration->initializer() != nullptr)) {
			error(symbol->valueDeclaration,
				  X_0_implicitly_has_type_any_because_it_does_not_have_a_type_annotation_and_is_referenced_directly_or_indirectly_in_its_own_initializer,
				  {symbolToString(symbol)});
		}
	} else if (symbol->flags & SymbolFlagsAlias) {
		Node* node = getDeclarationOfAliasSymbol(symbol);
		TSC_ASSERT(node != nullptr, "Unexpected nil in reportCircularityError for symbol: " +
									symbolToString(symbol));
		error(node, Circular_definition_of_import_alias_0, {symbolToString(symbol)});
	}
	return errorType;
}

// ---------------------------------------------------------------------------
// Deferred write-type machinery (checker.go:16720-16760)
// ---------------------------------------------------------------------------

Type* Checker::getTypeOfSymbolWithDeferredType(Symbol* symbol) {
	auto* links = valueSymbolLinks.Get(symbol);
	if (links->resolvedType == nullptr) {
		auto* deferred = deferredSymbolLinks.Get(symbol);
		if (deferred->parent->flags & TypeFlagsUnion) {
			links->resolvedType = getUnionType(deferred->constituents);
		} else {
			links->resolvedType = getIntersectionType(deferred->constituents);
		}
	}
	return links->resolvedType;
}

Type* Checker::getWriteTypeOfSymbolWithDeferredType(Symbol* symbol) {
	auto* links = valueSymbolLinks.Get(symbol);
	if (links->writeType == nullptr) {
		auto* deferred = deferredSymbolLinks.Get(symbol);
		if (!deferred->writeConstituents.empty()) {
			if (deferred->parent->flags & TypeFlagsUnion) {
				links->writeType = getUnionType(deferred->writeConstituents);
			} else {
				links->writeType = getIntersectionType(deferred->writeConstituents);
			}
		} else {
			links->writeType = getTypeOfSymbolWithDeferredType(symbol);
		}
	}
	return links->writeType;
}

// Distinct write types come only from set accessors, but synthetic union and intersection
// properties deriving from set accessors will either pre-compute or defer the union or
// intersection of the writeTypes of their constituents.
Type* Checker::getWriteTypeOfSymbol(Symbol* symbol) {
	if (symbol->checkFlags & CheckFlagsSyntheticProperty) {
		if (symbol->checkFlags & CheckFlagsDeferredType) {
			return getWriteTypeOfSymbolWithDeferredType(symbol);
		}
		auto* links = valueSymbolLinks.Get(symbol);
		return links->writeType != nullptr ? links->writeType : links->resolvedType;
	}
	if (symbol->flags & SymbolFlagsProperty) {
		return removeMissingType(getTypeOfSymbol(symbol), (symbol->flags & SymbolFlagsOptional) != 0);
	}
	if (symbol->flags & SymbolFlagsAccessor) {
		if (symbol->checkFlags & CheckFlagsInstantiated) {
			return getWriteTypeOfInstantiatedSymbol(symbol);
		}
		return getWriteTypeOfAccessors(symbol);
	}
	return getTypeOfSymbol(symbol);
}

// ---------------------------------------------------------------------------
// symbolToString — interim qualified-name implementation.
// TODO(nodebuilder): replace with the faithful nodebuilder+printer port
// (printer.go symbolToStringEx) once the nodebuilder lands.
// ---------------------------------------------------------------------------

std::string Checker::symbolToString(Symbol* symbol) {
	return symbolToStringEx(symbol, nullptr, SymbolFlagsAll, SymbolFormatFlagsAllowAnyNodeKind);
}

std::string Checker::symbolToStringEx(Symbol* symbol, Node* /*enclosingDeclaration*/,
									  SymbolFlags /*meaning*/, SymbolFormatFlags /*flags*/) {
	if (symbol == nullptr) {
		return "(unknown)";
	}
	std::string name = symbol->name;
	if (name.empty() || name == InternalSymbolNameComputed) {
		name = "<computed>";
	}
	// Qualify with the chain of parents that introduce a name (modules, enums,
	// classes, interfaces, functions get `.`, everything else gets skipped).
	std::string prefix;
	for (Symbol* parent = symbol->parent; parent != nullptr; parent = parent->parent) {
		if (parent->name.empty() || parent->name == InternalSymbolNameComputed ||
			parent->name.size() == 0) {
			continue;
		}
		prefix = parent->name + "." + prefix;
	}
	return prefix + name;
}


// checker.go:16055 getTargetOfAliasDeclaration
Symbol* Checker::getTargetOfAliasDeclaration(Node* node) {
	if (node == nullptr) {
		return nullptr;
	}
	switch (node->kind) {
	case Kind::ImportEqualsDeclaration:
	case Kind::VariableDeclaration:
		return getTargetOfImportEqualsDeclaration(node);
	case Kind::ImportClause:
		return getTargetOfImportClause(node);
	case Kind::NamespaceImport:
		return getTargetOfNamespaceImport(node);
	case Kind::NamespaceExport:
		return getTargetOfNamespaceExport(node);
	case Kind::ImportSpecifier:
	case Kind::BindingElement:
		return getTargetOfImportSpecifier(node);
	case Kind::ExportSpecifier:
		return getTargetOfExportSpecifier(
		    node, SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace,
		    /*dontRecursivelyResolve*/ true);
	case Kind::ExportAssignment:
		return getTargetOfExportAssignment(node);
	case Kind::BinaryExpression:
		return getTargetOfBinaryExpression(node);
	case Kind::NamespaceExportDeclaration:
		return getTargetOfNamespaceExportDeclaration(node);
	case Kind::ShorthandPropertyAssignment:
		return resolveEntityName(
		    node->as<ShorthandPropertyAssignment>()->name,
		    SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace,
		    /*ignoreErrors*/ true, /*dontResolveAlias*/ true,
		    /*location*/ nullptr);
	case Kind::PropertyAssignment:
		return getTargetOfAliasLikeExpression(node->initializer());
	case Kind::ElementAccessExpression:
	case Kind::PropertyAccessExpression:
		return getTargetOfAccessExpression(node);
	}
	TSC_UNREACHABLE("getTargetOfAliasDeclaration: unhandled node kind");
}


// ---------------------------------------------------------------------------
// checker.go: declared types, enum member values, block-scope use-before-declare
// ---------------------------------------------------------------------------

bool isNumericLiteralName(const std::string& name);
static bool isTypeAlias(Node* node);

Type* Checker::getDeclaredTypeOfSymbol(Symbol* symbol) {
	Type* result = tryGetDeclaredTypeOfSymbol(symbol);
	if (result == nullptr) {
		result = errorType;
	}
	return result;
}

Type* Checker::tryGetDeclaredTypeOfSymbol(Symbol* symbol) {
	if (symbol->flags & (SymbolFlagsClass | SymbolFlagsInterface)) {
		return getDeclaredTypeOfClassOrInterface(symbol);
	}
	if (symbol->flags & SymbolFlagsTypeParameter) {
		return getDeclaredTypeOfTypeParameter(symbol);
	}
	if (symbol->flags & SymbolFlagsTypeAlias) {
		return getDeclaredTypeOfTypeAlias(symbol);
	}
	if (symbol->flags & SymbolFlagsEnum) {
		return getDeclaredTypeOfEnum(symbol);
	}
	if (symbol->flags & SymbolFlagsEnumMember) {
		return getDeclaredTypeOfEnumMember(symbol);
	}
	if (symbol->flags & SymbolFlagsAlias) {
		return getDeclaredTypeOfAlias(symbol);
	}
	return nullptr;
}

static Node* getTypeReferenceName(Node* node) {
	switch (node->kind) {
	case Kind::TypeReference:
		return node->as<TypeReferenceNode>()->TypeName;
	case Kind::ExpressionWithTypeArguments:
		// We only support expressions that are simple qualified names. For other
		// expressions this produces nil
		if (Node* expr = node->expression(); isEntityNameExpression(expr)) {
			return expr;
		}
		break;
	default:
		break;
	}
	return nullptr;
}

TypeAlias* Checker::getAliasForTypeNode(Node* node) {
	Symbol* symbol = getAliasSymbolForTypeNode(node);
	if (symbol != nullptr) {
		return new TypeAlias{symbol, getTypeArgumentsForAliasSymbol(symbol)};
	}
	return nullptr;
}

Symbol* Checker::getAliasSymbolForTypeNode(Node* node) {
	Node* host = node->parent;
	while (isParenthesizedTypeNode(host) ||
		   (isTypeOperatorNode(host) &&
			host->as<TypeOperatorNode>()->Operator == Kind::ReadonlyKeyword)) {
		host = host->parent;
	}
	if (isTypeAlias(host)) {
		return getSymbolOfDeclaration(host);
	}
	return nullptr;
}

std::vector<Type*> Checker::getTypeArgumentsForAliasSymbol(Symbol* symbol) {
	if (symbol != nullptr) {
		return getLocalTypeParametersOfClassOrInterfaceOrTypeAlias(symbol);
	}
	return {};
}

Type* Checker::getDeclaredTypeOfClassOrInterface(Symbol* symbol) {
	DeclaredTypeLinks* links = declaredTypeLinks.Get(symbol);
	if (links->declaredType == nullptr) {
		ObjectFlags kind =
			(symbol->flags & SymbolFlagsClass) ? ObjectFlagsClass : ObjectFlagsInterface;
		Type* t = newObjectType(kind, symbol);
		links->declaredType = t;
		std::vector<Type*> outerTypeParameters =
			getOuterTypeParametersOfClassOrInterface(symbol);
		std::vector<Type*> typeParameters =
			appendLocalTypeParametersOfClassOrInterfaceOrTypeAlias(
				outerTypeParameters, symbol);
		// A class or interface is generic if it has type parameters or a "this" type.
		// We always give classes a "this" type because it is not feasible to analyze
		// all members to determine if the "this" type escapes the class (in particular,
		// property types inferred from initializers and method return types inferred
		// from return statements are very hard to exhaustively analyze). We give
		// interfaces a "this" type if we can't definitely determine that they are free
		// of "this" references.
		if (!typeParameters.empty() || kind == ObjectFlagsClass ||
			!isThislessInterface(symbol)) {
			t->objectFlags |= ObjectFlagsReference;
			InterfaceType* d = t->AsInterfaceType();
			d->thisType = newTypeParameter(symbol);
			d->thisType->AsTypeParameter()->isThisType = true;
			d->thisType->AsTypeParameter()->constraint = t;
			d->allTypeParameters = typeParameters;
			d->allTypeParameters.push_back(d->thisType);
			d->outerTypeParameterCount =
				static_cast<int32_t>(outerTypeParameters.size());
			d->resolvedTypeArguments = interfaceTypeTypeParameters(d);
			d->instantiations = CacheMap<Type*>{};
			d->instantiations[getTypeListKey(d->resolvedTypeArguments)] = t;
			d->target = t;
		}
	}
	return links->declaredType;
}

// Returns true if the interface given by the symbol is free of "this" references.
// Specifically, the result is true if the interface itself contains no references
// to "this" in its body, if all base types are interfaces, and if none of the base
// interfaces have a "this" type.
bool Checker::isThislessInterface(Symbol* symbol) {
	for (Node* declaration : symbol->declarations) {
		if (isInterfaceDeclaration(declaration)) {
			if (declaration->flags & NodeFlagsContainsThis) {
				return false;
			}
			for (Node* node : getExtendsHeritageClauseElements(declaration)) {
				Node* name = getHeritageClauseElementName(node);
				if (isEntityName(name) || isEntityNameExpression(name)) {
					Symbol* baseSymbol = resolveEntityName(
						name, SymbolFlagsType, /*ignoreErrors*/ true, false, nullptr);
					if (baseSymbol == nullptr ||
						!(baseSymbol->flags & SymbolFlagsInterface) ||
						getDeclaredTypeOfClassOrInterface(baseSymbol)
								->AsInterfaceType()
								->thisType != nullptr) {
						return false;
					}
				}
			}
		}
	}
	return true;
}

// Returns the declaration used to obtain a class, interface, or function symbol's
// outer type parameters.
Node* Checker::getClassOrInterfaceLikeDeclaration(Symbol* symbol) {
	if (symbol->flags & (SymbolFlagsClass | SymbolFlagsFunction)) {
		return symbol->valueDeclaration;
	}
	for (Node* d : symbol->declarations) {
		if (isInterfaceDeclaration(d)) {
			return d;
		}
		if (isVariableDeclaration(d)) {
			Node* initializer = d->initializer();
			if (initializer != nullptr &&
				isFunctionExpressionOrArrowFunction(initializer)) {
				return d;
			}
		}
	}
	return nullptr;
}

bool Checker::canGetTypeParametersOfClassOrInterface(Symbol* symbol) {
	return getClassOrInterfaceLikeDeclaration(symbol) != nullptr;
}

// Return the outer type parameters of a node or undefined if the node has no
// outer type parameters.
std::vector<Type*> Checker::getOuterTypeParameters(Node* node,
												   bool includeThisTypes) {
	for (;;) {
		node = node->parent;
		if (node == nullptr) {
			return {};
		}
		Kind kind = node->kind;
		switch (kind) {
		case Kind::ClassDeclaration:
		case Kind::ClassExpression:
		case Kind::InterfaceDeclaration:
		case Kind::CallSignature:
		case Kind::ConstructSignature:
		case Kind::MethodSignature:
		case Kind::FunctionType:
		case Kind::ConstructorType:
		case Kind::FunctionDeclaration:
		case Kind::MethodDeclaration:
		case Kind::FunctionExpression:
		case Kind::ArrowFunction:
		case Kind::TypeAliasDeclaration:
		case Kind::JSTypeAliasDeclaration:
		case Kind::MappedType:
		case Kind::ConditionalType: {
			std::vector<Type*> outerTypeParameters =
				getOuterTypeParameters(node, includeThisTypes);
			if ((kind == Kind::FunctionExpression || kind == Kind::ArrowFunction ||
				 isObjectLiteralMethod(node)) &&
				isContextSensitive(node)) {
				std::vector<Signature*> sigs = getSignaturesOfType(
					getTypeOfSymbol(getSymbolOfDeclaration(node)),
					SignatureKind::Call);
				Signature* signature = sigs.empty() ? nullptr : sigs.front();
				if (signature != nullptr && !signature->typeParameters.empty()) {
					outerTypeParameters.insert(outerTypeParameters.end(),
						signature->typeParameters.begin(),
						signature->typeParameters.end());
					return outerTypeParameters;
				}
			}
			if (kind == Kind::MappedType) {
				outerTypeParameters.push_back(getDeclaredTypeOfTypeParameter(
					getSymbolOfDeclaration(
						node->as<MappedTypeNode>()->TypeParameter)));
				return outerTypeParameters;
			}
			if (kind == Kind::ConditionalType) {
				std::vector<Type*> infer = getInferTypeParameters(node);
				outerTypeParameters.insert(
					outerTypeParameters.end(), infer.begin(), infer.end());
				return outerTypeParameters;
			}
			std::vector<Type*> outerAndOwnTypeParameters = appendTypeParameters(
				outerTypeParameters, node->typeParameters());
			Type* thisType = nullptr;
			if (includeThisTypes &&
				(kind == Kind::ClassDeclaration || kind == Kind::ClassExpression ||
				 kind == Kind::InterfaceDeclaration)) {
				thisType = getDeclaredTypeOfClassOrInterface(
							   getSymbolOfDeclaration(node))
							   ->AsInterfaceType()
							   ->thisType;
			}
			if (thisType != nullptr) {
				outerAndOwnTypeParameters.push_back(thisType);
			}
			return outerAndOwnTypeParameters;
		}
		default:
			break;
		}
	}
}

std::vector<Type*> Checker::getOuterTypeParametersOfClassOrInterface(
	Symbol* symbol) {
	Node* declaration = getClassOrInterfaceLikeDeclaration(symbol);
	TSC_ASSERT(declaration != nullptr,
			   "Class was missing valueDeclaration -OR- non-class had no interface "
			   "declarations");
	return getOuterTypeParameters(declaration, /*includeThisTypes*/ false);
}

std::vector<Type*> Checker::getInferTypeParameters(Node* node) {
	std::vector<Type*> result;
	for (auto& [name, symbol] : *node->locals()) {
		if (symbol->flags & SymbolFlagsTypeParameter) {
			result.push_back(getDeclaredTypeOfSymbol(symbol));
		}
	}
	return result;
}

// The local type parameters are the combined set of type parameters from all
// declarations of the class, interface, or type alias.
std::vector<Type*> Checker::getLocalTypeParametersOfClassOrInterfaceOrTypeAlias(
	Symbol* symbol) {
	return appendLocalTypeParametersOfClassOrInterfaceOrTypeAlias({}, symbol);
}

std::vector<Type*> Checker::appendLocalTypeParametersOfClassOrInterfaceOrTypeAlias(
	std::vector<Type*> types, Symbol* symbol) {
	for (Node* node : symbol->declarations) {
		if (nodeKindIs(node, Kind::InterfaceDeclaration, Kind::ClassDeclaration,
					   Kind::ClassExpression) ||
			isTypeAlias(node)) {
			types = appendTypeParameters(types, node->typeParameters());
		}
	}
	return types;
}

// Appends the type parameters given by a list of declarations to a set of type
// parameters and returns the resulting set.
std::vector<Type*> Checker::appendTypeParameters(
	std::vector<Type*> typeParameters, const std::vector<Node*>& declarations) {
	for (Node* declaration : declarations) {
		Type* tp = getDeclaredTypeOfTypeParameter(getSymbolOfDeclaration(declaration));
		if (std::find(typeParameters.begin(), typeParameters.end(), tp) ==
			typeParameters.end()) {
			typeParameters.push_back(tp);
		}
	}
	return typeParameters;
}

Type* Checker::getDeclaredTypeOfTypeParameter(Symbol* symbol) {
	DeclaredTypeLinks* links = declaredTypeLinks.Get(symbol);
	if (links->declaredType == nullptr) {
		links->declaredType = newTypeParameter(symbol);
	}
	return links->declaredType;
}

Type* Checker::getDeclaredTypeOfTypeAlias(Symbol* symbol) {
	TypeAliasLinks* links = typeAliasLinks.Get(symbol);
	if (links->declaredType == nullptr) {
		// Note that we use the links object as the target here because the symbol
		// object is used as the unique identity for resolution of the 'type' property
		// in SymbolLinks.
		if (!pushTypeResolution(symbol, TypeSystemPropertyName::DeclaredType)) {
			return errorType;
		}
		Node* declaration = nullptr;
		for (Node* d : symbol->declarations) {
			if (isTypeOrJSTypeAliasDeclaration(d)) {
				declaration = d;
				break;
			}
		}
		Node* typeNode = declaration->type();
		Type* t = getTypeFromTypeNode(typeNode);
		if (popTypeResolution()) {
			std::vector<Type*> typeParameters =
				getLocalTypeParametersOfClassOrInterfaceOrTypeAlias(symbol);
			if (!typeParameters.empty()) {
				// Initialize the instantiation cache for generic type aliases. The
				// declared type corresponds to an instantiation of the type alias
				// with the type parameters supplied as type arguments.
				links->typeParameters = typeParameters;
				links->instantiations = CacheMap<Type*>{};
				links->instantiations[getTypeListKey(typeParameters)] = t;
			}
			if (t == intrinsicMarkerType && symbol->name == "BuiltinIteratorReturn") {
				t = getBuiltinIteratorReturnType();
			}
		} else {
			Node* errorNode = declaration->name();
			if (errorNode == nullptr) {
				errorNode = declaration;
			}
			error(errorNode, Type_alias_0_circularly_references_itself,
				  {symbolToString(symbol)});
			t = errorType;
		}
		if (links->declaredType == nullptr) {
			links->declaredType = t;
		}
	}
	return links->declaredType;
}

Type* Checker::getDeclaredTypeOfEnum(Symbol* symbol) {
	DeclaredTypeLinks* links = declaredTypeLinks.Get(symbol);
	if (links->declaredType == nullptr) {
		std::vector<Type*> memberTypeList;
		for (Node* declaration : symbol->declarations) {
			if (declaration->kind == Kind::EnumDeclaration) {
				for (Node* member : declaration->members()) {
					if (!hasDynamicName(member)) {
						Symbol* memberSymbol = getSymbolOfDeclaration(member);
						EvalResult value = getEnumMemberValue(member);
						Type* memberType;
						if (!std::holds_alternative<std::monostate>(value.Value)) {
							memberType = getEnumLiteralType(
								value.Value, symbol, memberSymbol);
						} else {
							memberType = createComputedEnumType(memberSymbol);
						}
						declaredTypeLinks.Get(memberSymbol)->declaredType =
							getFreshTypeOfLiteralType(memberType);
						memberTypeList.push_back(memberType);
					}
				}
			}
		}
		Type* enumType;
		if (!memberTypeList.empty()) {
			enumType = getUnionTypeEx(memberTypeList, UnionReductionLiteral,
									  new TypeAlias{symbol, {}}, /*origin*/ nullptr);
		} else {
			enumType = createComputedEnumType(symbol);
		}
		if (enumType->flags & TypeFlagsUnion) {
			enumType->flags |= TypeFlagsEnumLiteral;
			enumType->symbol = symbol;
		}
		links->declaredType = enumType;
	}
	return links->declaredType;
}

EvalResult Checker::getEnumMemberValue(Node* node) {
	computeEnumMemberValues(node->parent);
	return enumMemberLinks.Get(node)->value;
}

Type* Checker::createComputedEnumType(Symbol* symbol) {
	Type* regularType = newLiteralType(TypeFlagsEnum, {}, nullptr);
	regularType->symbol = symbol;
	Type* freshType = newLiteralType(TypeFlagsEnum, {}, regularType);
	freshType->symbol = symbol;
	regularType->AsLiteralType()->freshType = freshType;
	freshType->AsLiteralType()->freshType = freshType;
	return regularType;
}

Type* Checker::getDeclaredTypeOfEnumMember(Symbol* symbol) {
	DeclaredTypeLinks* links = declaredTypeLinks.Get(symbol);
	if (links->declaredType == nullptr) {
		Type* enumType = getDeclaredTypeOfEnum(getParentOfSymbol(symbol));
		if (links->declaredType == nullptr) {
			links->declaredType = enumType;
		}
	}
	return links->declaredType;
}

void Checker::computeEnumMemberValues(Node* node) {
	NodeLinks* nodeLinks = this->nodeLinks.Get(node);
	if (!(nodeLinks->flags & NodeCheckFlagsEnumValuesComputed)) {
		nodeLinks->flags |= NodeCheckFlagsEnumValuesComputed;
		Number autoValue = Number(0);
		bool hasAutoValue = true;
		Node* previous = nullptr;
		for (Node* member : node->members()) {
			EvalResult result = computeEnumMemberValue(
				member, hasAutoValue ? &autoValue : nullptr, previous);
			enumMemberLinks.Get(member)->value = result;
			if (auto* value = std::get_if<Number>(&result.Value)) {
				autoValue = *value + Number(1);
				hasAutoValue = true;
			} else {
				hasAutoValue = false;
			}
			previous = member;
		}
	}
}

EvalResult Checker::computeEnumMemberValue(Node* member, Number* autoValue,
										   Node* previous) {
	if (isComputedNonLiteralName(member->name())) {
		error(member->name(), Computed_property_names_are_not_allowed_in_enums);
	} else if (isBigIntLiteral(member->name())) {
		error(member->name(), An_enum_member_cannot_have_a_numeric_name);
	} else {
		std::string text = getTextOfPropertyName(member->name());
		if (isNumericLiteralName(text) && !isInfinityOrNaNString(text)) {
			error(member->name(), An_enum_member_cannot_have_a_numeric_name);
		}
	}
	if (member->initializer() != nullptr) {
		return computeConstantEnumMemberValue(member);
	}
	// In ambient non-const numeric enum declarations, enum members without
	// initializers are considered computed members (as opposed to having
	// auto-incremented values).
	if (member->parent->flags & NodeFlagsAmbient && !isEnumConst(member->parent)) {
		return newEvalResult(std::monostate{}, false, false, false);
	}
	// If the member declaration specifies no value, the member is considered a
	// constant enum member. If the member is the first member in the enum
	// declaration, it is assigned the value zero. Otherwise, it is assigned the
	// value of the immediately preceding member plus one, and an error occurs if
	// the immediately preceding member is not a constant enum member.
	if (autoValue == nullptr) {
		error(member->name(), Enum_member_must_have_initializer);
		return newEvalResult(std::monostate{}, false, false, false);
	}
	if (compilerOptions->GetIsolatedModules() && previous != nullptr &&
		previous->initializer() != nullptr) {
		EvalResult prevValue = getEnumMemberValue(previous);
		if (!std::holds_alternative<Number>(prevValue.Value) ||
			prevValue.ResolvedOtherFiles) {
			error(member->name(),
				  Enum_member_following_a_non_literal_numeric_member_must_have_an_initializer_when_isolatedModules_is_enabled);
		}
	}
	return newEvalResult(*autoValue, false, false, false);
}

EvalResult Checker::computeConstantEnumMemberValue(Node* member) {
	bool isConstEnum = isEnumConst(member->parent);
	Node* initializer = member->initializer();
	EvalResult result = evaluate(initializer, member);
	if (!std::holds_alternative<std::monostate>(result.Value)) {
		if (isConstEnum) {
			if (auto* numValue = std::get_if<Number>(&result.Value);
				numValue != nullptr && (numValue->isInf() || numValue->isNaN())) {
				error(initializer,
					  numValue->isNaN()
						  ? X_const_enum_member_initializer_was_evaluated_to_disallowed_value_NaN
						  : X_const_enum_member_initializer_was_evaluated_to_a_non_finite_value);
			}
		}
		if (compilerOptions->GetIsolatedModules()) {
			if (std::holds_alternative<std::string>(result.Value) &&
				!result.IsSyntacticallyString) {
				std::string memberName = member->parent->name()->text() + "." +
										 member->name()->text();
				error(initializer,
					  X_0_has_a_string_type_but_must_have_syntactically_recognizable_string_syntax_when_isolatedModules_is_enabled,
					  std::vector<std::string>{memberName});
			}
		}
	} else if (isConstEnum) {
		error(initializer,
			  X_const_enum_member_initializers_must_be_constant_expressions);
	} else if (member->parent->flags & NodeFlagsAmbient) {
		error(initializer,
			  In_ambient_enum_declarations_member_initializer_must_be_constant_expression);
	} else {
		checkTypeAssignableTo(
			checkExpression(initializer), numberType, initializer,
			Type_0_is_not_assignable_to_type_1_as_required_for_computed_enum_member_values);
	}
	return result;
}

EvalResult Checker::evaluateEntity(Node* expr, Node* location) {
	switch (expr->kind) {
	case Kind::Identifier:
	case Kind::PropertyAccessExpression: {
		Symbol* symbol = resolveEntityName(expr, SymbolFlagsValue,
										 /*ignoreErrors*/ true, false, nullptr);
		if (symbol == nullptr) {
			return newEvalResult(std::monostate{}, false, false, false);
		}
		if (expr->kind == Kind::Identifier) {
			if (isInfinityOrNaNString(expr->text()) &&
				symbol == getGlobalSymbol(expr->text(), SymbolFlagsValue,
										  /*diagnostic*/ nullptr)) {
				// Technically we resolved a global lib file here, but the decision
				// to treat this as numeric is more predicated on the fact that the
				// single-file resolution *didn't* resolve to a different meaning of
				// `Infinity` or `NaN`. Transpilers handle this no problem.
				return newEvalResult(numberFromString(expr->text()), false, false,
									 false);
			}
		}
		if (symbol->flags & SymbolFlagsEnumMember) {
			if (location != nullptr) {
				return evaluateEnumMember(expr, symbol, location);
			}
			return getEnumMemberValue(symbol->valueDeclaration);
		}
		if (isConstantVariable(symbol)) {
			Node* declaration = symbol->valueDeclaration;
			if (declaration != nullptr && isVariableDeclaration(declaration) &&
				declaration->type() == nullptr &&
				declaration->initializer() != nullptr &&
				(location == nullptr ||
				 (declaration != location &&
				  isBlockScopedNameDeclaredBeforeUse(declaration, location)))) {
				EvalResult result = evaluate(declaration->initializer(), declaration);
				if (location != nullptr &&
					getSourceFileOfNode(location) != getSourceFileOfNode(declaration)) {
					return newEvalResult(result.Value, false, true, true);
				}
				return newEvalResult(result.Value, result.IsSyntacticallyString,
									 result.ResolvedOtherFiles,
									 /*hasExternalReferences*/ true);
			}
		}
		return newEvalResult(std::monostate{}, false, false, false);
	}
	case Kind::ElementAccessExpression: {
		Node* root = expr->expression();
		if (isEntityNameExpression(root) &&
			isStringLiteralLike(
				expr->as<ElementAccessExpression>()->ArgumentExpression)) {
			Symbol* rootSymbol = resolveEntityName(root, SymbolFlagsValue,
												   /*ignoreErrors*/ true, false,
												   nullptr);
			if (rootSymbol != nullptr &&
				rootSymbol->flags & SymbolFlagsEnum) {
				std::string name =
					expr->as<ElementAccessExpression>()->ArgumentExpression->text();
				auto it = rootSymbol->exports.find(name);
				if (it != rootSymbol->exports.end() && it->second != nullptr) {
					Symbol* member = it->second;
					if (location != nullptr) {
						return evaluateEnumMember(expr, member, location);
					}
					return getEnumMemberValue(member->valueDeclaration);
				}
			}
		}
		return newEvalResult(std::monostate{}, false, false, false);
	}
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in evaluateEntity");
}

EvalResult Checker::evaluateEnumMember(Node* expr, Symbol* symbol,
									   Node* location) {
	Node* declaration = symbol->valueDeclaration;
	if (declaration == nullptr || declaration == location) {
		error(expr, Property_0_is_used_before_being_assigned,
			  std::vector<std::string>{symbolToString(symbol)});
		return newEvalResult(std::monostate{}, false, false, false);
	}
	if (!isBlockScopedNameDeclaredBeforeUse(declaration, location)) {
		error(expr,
			  A_member_initializer_in_a_enum_declaration_cannot_reference_members_declared_after_it_including_members_defined_in_other_enums);
		return newEvalResult(Number(0), false, false, false);
	}
	EvalResult value = getEnumMemberValue(declaration);
	if (location->parent != declaration->parent) {
		return newEvalResult(value.Value, value.IsSyntacticallyString,
							 value.ResolvedOtherFiles,
							 /*hasExternalReferences*/ true);
	}
	return value;
}

Type* Checker::getDeclaredTypeOfAlias(Symbol* symbol) {
	DeclaredTypeLinks* links = declaredTypeLinks.Get(symbol);
	if (links->declaredType == nullptr) {
		links->declaredType = getDeclaredTypeOfSymbol(resolveAlias(symbol));
	}
	return links->declaredType;
}

// ---------------------------------------------------------------------------
// utilities.go: numeric literal names
// ---------------------------------------------------------------------------

bool isNumericLiteralName(const std::string& name) {
	// The intent of numeric names is that
	//     - they are names with text in a numeric form, and that
	//     - setting properties/indexing with them is always equivalent to doing so
	//       with the numeric literal 'numLit', acquired by applying the abstract
	//       'ToNumber' operation on the name's text.
	// The subtlety is in the latter portion, as we cannot reliably say that
	// anything that looks like a numeric literal is a numeric name. In fact, it is
	// the case that the text of the name must be equal to 'ToString(numLit)' for
	// this to hold.
	// Here, we test whether 'ToString(ToNumber(name))' is exactly equal to 'name'.
	// Note that this accepts the values 'Infinity', '-Infinity', and 'NaN', and
	// that this is intentional.
	return numberFromString(name).string() == name;
}

static bool isTypeAlias(Node* node) { return isTypeOrJSTypeAliasDeclaration(node); }

bool Checker::isConstantVariable(Symbol* symbol) {
	return (symbol->flags & SymbolFlagsVariable) &&
		   (getDeclarationNodeFlagsFromSymbol(symbol) & NodeFlagsConstant);
}

bool Checker::isParameterOrMutableLocalVariable(Symbol* symbol) {
	// Return true if symbol is a parameter, a catch clause variable, or a mutable
	// local variable
	if (symbol->valueDeclaration != nullptr) {
		Node* declaration = getRootDeclaration(symbol->valueDeclaration);
		return declaration != nullptr &&
			   (isParameterDeclaration(declaration) ||
				(isVariableDeclaration(declaration) &&
				 (isCatchClause(declaration->parent) ||
				  isMutableLocalVariableDeclaration(declaration))));
	}
	return false;
}

bool Checker::isMutableLocalVariableDeclaration(Node* declaration) {
	// Return true if symbol is a non-exported and non-global `let` variable
	return (declaration->parent->flags & NodeFlagsLet) &&
		   !((getCombinedModifierFlags(declaration) & ModifierFlagsExport) ||
			 (declaration->parent->parent->kind == Kind::VariableStatement &&
			  isGlobalSourceFile(declaration->parent->parent->parent)));
}

bool Checker::isInAmbientOrTypeNode(Node* node) {
	return (node->flags & NodeFlagsAmbient) ||
		   findAncestor(node, [](Node* n) {
			   return isInterfaceDeclaration(n) ||
					  isTypeOrJSTypeAliasDeclaration(n) || isTypeLiteralNode(n);
		   }) != nullptr;
}

static bool isThisProperty(Node* node) {
	return (isPropertyAccessExpression(node) || isElementAccessExpression(node)) &&
		   node->expression()->kind == Kind::ThisKeyword;
}

static bool isInTypeQuery(Node* node) {
	// TypeScript 1.0 spec (April 2014): 3.6.3
	// A type query consists of the keyword typeof followed by an expression. The
	// expression is restricted to a single identifier or a sequence of identifiers
	// separated by periods
	return findAncestorOrQuit(node, [](Node* n) -> FindAncestorResult {
		switch (n->kind) {
		case Kind::TypeQuery:
			return FindAncestorResult::True;
		case Kind::Identifier:
		case Kind::QualifiedName:
			return FindAncestorResult::False;
		default:
			return FindAncestorResult::Quit;
		}
	}) != nullptr;
}

NodeFlags Checker::getDeclarationNodeFlagsFromSymbol(Symbol* s) {
	if (s->valueDeclaration != nullptr) {
		return getCombinedNodeFlagsCached(s->valueDeclaration);
	}
	return NodeFlagsNone;
}

NodeFlags Checker::getCombinedNodeFlagsCached(Node* node) {
	// we hold onto the last node and result to speed up repeated lookups against
	// the same node.
	if (lastGetCombinedNodeFlagsNode == node) {
		return lastGetCombinedNodeFlagsResult;
	}
	lastGetCombinedNodeFlagsNode = node;
	lastGetCombinedNodeFlagsResult = getCombinedNodeFlags(node);
	return lastGetCombinedNodeFlagsResult;
}

bool Checker::isVarConstLike(Node* node) {
	NodeFlags blockScopeKind =
		getCombinedNodeFlagsCached(node) & NodeFlagsBlockScoped;
	return blockScopeKind == NodeFlagsConst || blockScopeKind == NodeFlagsUsing ||
		   blockScopeKind == NodeFlagsAwaitUsing;
}

// Starting from 'initial' node walk up the parent chain until 'stopAt' node is
// reached. If at any point current node is equal to 'parent' node - return true.
// If current node is an IIFE, continue walking up. Return false if 'stopAt' node
// is reached or isFunctionLike(current) === true.
static bool isSameScopeDescendentOf(Node* initial, Node* parent, Node* stopAt) {
	if (parent == nullptr) {
		return false;
	}
	for (Node* n = initial; n != nullptr; n = n->parent) {
		if (n == parent) {
			return true;
		}
		if (n == stopAt ||
			(isFunctionLike(n) &&
			 (getImmediatelyInvokedFunctionExpression(n) == nullptr ||
			  (getFunctionFlags(n) & FunctionFlagsAsyncGenerator)))) {
			return false;
		}
	}
	return false;
}

static bool isImmediatelyUsedInInitializerOfBlockScopedVariable(
	Node* declaration, Node* usage, Node* declContainer) {
	switch (declaration->parent->parent->kind) {
	case Kind::VariableStatement:
	case Kind::ForStatement:
	case Kind::ForOfStatement:
		// variable statement/for/for-of statement case, use site should not be
		// inside variable declaration (initializer of declaration or binding
		// element)
		if (isSameScopeDescendentOf(usage, declaration, declContainer)) {
			return true;
		}
		break;
	default:
		break;
	}
	// ForIn/ForOf case - use site should not be used in expression part
	Node* grandparent = declaration->parent->parent;
	return isForInOrOfStatement(grandparent) &&
		   isSameScopeDescendentOf(usage, grandparent->expression(), declContainer);
}

// isPropertyImmediatelyReferencedWithinDeclaration is used for detecting
// ES-standard class field use-before-def errors
static bool isPropertyImmediatelyReferencedWithinDeclaration(
	Node* declaration, Node* usage, bool stopAtAnyPropertyDeclaration) {
	// always legal if usage is after declaration
	if (usage->end() > declaration->end()) {
		return false;
	}
	// still might be legal if usage is deferred (e.g. x: any = () => this.x)
	// otherwise illegal if immediately referenced within the declaration (e.g.
	// x: any = this.x)
	for (Node* node = usage; node != nullptr && node != declaration;
		 node = node->parent) {
		switch (node->kind) {
		case Kind::ArrowFunction:
			return false;
		case Kind::PropertyDeclaration:
			// even when stopping at any property declaration, they need to come
			// from the same class
			return stopAtAnyPropertyDeclaration &&
				   isClassLike(node->parent) &&
				   node->parent == declaration->parent;
		case Kind::ClassStaticBlockDeclaration:
			return true;
		default:
			break;
		}
	}
	return false;
}

bool Checker::isUsedInFunctionOrInstanceProperty(Node* usage, Node* declaration,
												 Node* declContainer) {
	return findAncestorOrQuit(usage,
							  [&](Node* current) -> FindAncestorResult {
		if (current == declContainer) {
			return FindAncestorResult::Quit;
		}
		if (isFunctionLike(current)) {
			return toFindAncestorResult(
				getImmediatelyInvokedFunctionExpression(current) == nullptr);
		}
		if (isClassStaticBlockDeclaration(current)) {
			return toFindAncestorResult(declaration->pos() < usage->pos());
		}

		if (current->parent != nullptr &&
			isPropertyDeclaration(current->parent)) {
			Node* propertyDeclaration = current->parent;
			bool initializerOfProperty =
				propertyDeclaration->initializer() == current;
			if (initializerOfProperty) {
				if (isStatic(current->parent)) {
					if (isMethodDeclaration(declaration)) {
						return FindAncestorResult::True;
					}
					if (isPropertyDeclaration(declaration) &&
						getContainingClass(usage) ==
							getContainingClass(declaration)) {
						Node* propName = declaration->name();
						if (isIdentifier(propName) ||
							isPrivateIdentifier(propName)) {
							Type* t = getTypeOfSymbol(
								getSymbolOfDeclaration(declaration));
							std::vector<Node*> staticBlocks;
							for (Node* m : declaration->parent->members()) {
								if (isClassStaticBlockDeclaration(m)) {
									staticBlocks.push_back(m);
								}
							}
							if (isPropertyInitializedInStaticBlocks(
									propName, t, staticBlocks,
									declaration->parent->pos(),
									current->pos())) {
								return FindAncestorResult::True;
							}
						}
					}
				} else {
					bool isDeclarationInstanceProperty =
						isPropertyDeclaration(declaration) &&
						!isStatic(declaration);
					if (!isDeclarationInstanceProperty ||
						getContainingClass(usage) !=
							getContainingClass(declaration)) {
						return FindAncestorResult::True;
					}
				}
			}
		}

		if (current->parent != nullptr && isDecorator(current->parent)) {
			Decorator* decorator = current->parent->as<Decorator>();
			if (decorator->Expression == current) {
				if (isParameterDeclaration(decorator->parent)) {
					if (isUsedInFunctionOrInstanceProperty(
							decorator->parent->parent->parent, declaration,
							declContainer)) {
						return FindAncestorResult::True;
					}
					return FindAncestorResult::Quit;
				}
				if (isMethodDeclaration(decorator->parent)) {
					if (isUsedInFunctionOrInstanceProperty(
							decorator->parent->parent, declaration,
							declContainer)) {
						return FindAncestorResult::True;
					}
					return FindAncestorResult::Quit;
				}
			}
		}

		return FindAncestorResult::False;
	}) != nullptr;
}

bool Checker::isPropertyInitializedInStaticBlocks(
	Node* propName, Type* propType, const std::vector<Node*>& staticBlocks,
	int32_t startPos, int32_t endPos) {
	for (Node* staticBlock : staticBlocks) {
		// static block must be within the provided range as they are evaluated in
		// document order (unlike constructors)
		if (staticBlock->pos() >= startPos && staticBlock->pos() <= endPos) {
			Node* reference = factory.newPropertyAccessExpression(
				factory.newKeywordExpression(Kind::ThisKeyword), nullptr, propName,
				NodeFlagsNone);
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

bool Checker::isPropertyInitializedInConstructor(Node* propName, Type* propType,
												 Node* constructor) {
	Node* reference;
	if (isComputedPropertyName(propName)) {
		reference = factory.newElementAccessExpression(
			factory.newKeywordExpression(Kind::ThisKeyword), nullptr,
			propName->expression(), NodeFlagsNone);
	} else {
		reference = factory.newPropertyAccessExpression(
			factory.newKeywordExpression(Kind::ThisKeyword), nullptr, propName,
			NodeFlagsNone);
	}
	reference->expression()->parent = reference;
	reference->parent = constructor;
	*reference->flowNodeData().flowNode =
		constructor->as<ConstructorDeclaration>()->ReturnFlowNode;
	Type* flowType = getFlowTypeOfReferenceEx(
		reference, propType, getOptionalType(propType, false), nullptr, nullptr);
	return !containsUndefinedType(flowType);
}

bool Checker::isBlockScopedNameDeclaredBeforeUse(Node* declaration, Node* usage) {
	const SourceFile* declarationFile = getSourceFileOfNode(declaration);
	const SourceFile* useFile = getSourceFileOfNode(usage);
	Node* declContainer = getEnclosingBlockScopeContainer(declaration);
	if (declarationFile != useFile) {
		// nodes are in different files and order cannot be determined
		return true;
	}
	// deferred usage in a type context is always OK regardless of the usage
	// position:
	if (usage->flags & NodeFlagsJSDoc || isInTypeQuery(usage) ||
		isInAmbientOrTypeNode(usage)) {
		return true;
	}
	if (declaration->pos() <= usage->pos() &&
		!(isPropertyDeclaration(declaration) && isThisProperty(usage->parent) &&
		  declaration->initializer() == nullptr &&
		  !isExclamationToken(declaration->postfixToken()))) {
		// declaration is before usage
		if (declaration->kind == Kind::BindingElement) {
			// still might be illegal if declaration and usage are both binding
			// elements (eg var [a = b, b = b] = [1, 2])
			Node* errorBindingElement =
				findAncestorKind(usage, Kind::BindingElement);
			if (errorBindingElement != nullptr) {
				return findAncestor(errorBindingElement, isBindingElement) !=
						   findAncestor(declaration, isBindingElement) ||
					   declaration->pos() < errorBindingElement->pos();
			}
			// or it might be illegal if usage happens before parent variable is
			// declared (eg var [a] = a)
			return isBlockScopedNameDeclaredBeforeUse(
				findAncestorKind(declaration, Kind::VariableDeclaration), usage);
		}
		if (declaration->kind == Kind::VariableDeclaration) {
			// still might be illegal if usage is in the initializer of the
			// variable declaration (eg var a = a)
			return !isImmediatelyUsedInInitializerOfBlockScopedVariable(
				declaration, usage, declContainer);
		}
		if (isClassLike(declaration)) {
			// still might be illegal if the usage is within a computed property
			// name in the class (eg class A { static p = "a"; [A.p]() {} })
			// or when used within a decorator in the class (e.g. `@dec(A.x) class A
			// { static x = "x" }`), except when used in a function that is not an
			// IIFE (e.g., `@dec(() => A.x) class A { ... }`)
			Node* container = usage;
			while (container != nullptr && container != declaration) {
				if ((isComputedPropertyName(container) &&
					 container->parent->parent == declaration) ||
					(!legacyDecorators && isDecorator(container) &&
					 (container->parent == declaration ||
					  (isMethodDeclaration(container->parent) &&
					   container->parent->parent == declaration) ||
					  (isAccessor(container->parent) &&
					   container->parent->parent == declaration) ||
					  (isPropertyDeclaration(container->parent) &&
					   container->parent->parent == declaration) ||
					  (isParameterDeclaration(container->parent) &&
					   container->parent->parent->parent == declaration)))) {
					break;
				}
				container = container->parent;
			}
			if (container == nullptr || container == declaration) {
				return true;
			}
			if (!legacyDecorators && isDecorator(container)) {
				Node* n = usage;
				while (n != nullptr && n != container) {
					if (isFunctionLike(n) &&
						getImmediatelyInvokedFunctionExpression(n) == nullptr) {
						break;
					}
					n = n->parent;
				}
				return n != nullptr && n != container;
			}
			return false;
		}
		if (isPropertyDeclaration(declaration)) {
			// still might be illegal if a self-referencing property initializer (eg
			// private x = this.x)
			return !isPropertyImmediatelyReferencedWithinDeclaration(
				declaration, usage, /*stopAtAnyPropertyDeclaration*/ false);
		}
		if (isParameterPropertyDeclaration(declaration, declaration->parent)) {
			// foo = this.bar is illegal in emitStandardClassFields when bar is a
			// parameter property
			return !(emitStandardClassFields &&
					 getContainingClass(declaration) ==
						 getContainingClass(usage) &&
					 isUsedInFunctionOrInstanceProperty(usage, declaration,
													  declContainer));
		}
		return true;
	}
	// declaration is after usage, but it can still be legal if usage is deferred:
	// 1. inside an export specifier
	// 2. inside a function
	// 3. inside an instance property initializer, a reference to a non-instance
	//    property
	//    (except when emitStandardClassFields: true and the reference is to a
	//    parameter property)
	// 4. inside a static property initializer, a reference to a static method in
	//    the same class
	// 5. inside a TS export= declaration (since we will move the export statement
	//    during emit to avoid TDZ)
	if (isExportSpecifier(usage->parent) ||
		(isExportAssignment(usage->parent) &&
		 usage->parent->as<ExportAssignment>()->IsExportEquals)) {
		// export specifiers do not use the variable, they only make it available
		// for use
		return true;
	}
	// When resolving symbols for exports, the `usage` location passed in can be
	// the export site directly
	if (isExportAssignment(usage) &&
		usage->as<ExportAssignment>()->IsExportEquals) {
		return true;
	}
	if (isUsedInFunctionOrInstanceProperty(usage, declaration, declContainer)) {
		if (emitStandardClassFields && getContainingClass(declaration) != nullptr &&
			(isPropertyDeclaration(declaration) ||
			 isParameterPropertyDeclaration(declaration, declaration->parent))) {
			return !isPropertyImmediatelyReferencedWithinDeclaration(
				declaration, usage, /*stopAtAnyPropertyDeclaration*/ true);
		}
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Pending ports — bodies land with their file's slice.
// ---------------------------------------------------------------------------

// (deduped: getWidenedType defined in cpp/internal/checker/checker_decltypes.cpp)


// getSignaturesOfType is defined in checker_members.cpp (members slice).

// checker.go:7725 — checkExpression (walk slice)
Type* Checker::checkExpression(Node* node) {
	return checkExpressionEx(node, CheckModeNormal);
}

// getFlowTypeOfReferenceEx defined in checker_flow.cpp (flow slice).
// getOptionalType defined in checker_decltypes.cpp.

// containsUndefinedType is defined in checker_typeops.cpp.
// getOptionalType stub above (kept).

// getTypeWithThisArgument and getBaseTypes are defined in checker_members.cpp
// (members slice).

// (deduped: getBuiltinIteratorReturnType defined in cpp/internal/checker/checker_declchecks2.cpp)

// ---------------------------------------------------------------------------
// NewChecker bootstrap (checker.go:911-1500) — ported with the bootstrap slice
// ---------------------------------------------------------------------------

template <typename F>
static auto memoize(F&& create) -> std::function<decltype(create())()> {
	using T = decltype(create());
	return [create = std::forward<F>(create), cache = std::optional<T>{}]() mutable -> T {
		if (!cache.has_value()) {
			cache = create();
		}
		return *cache;
	};
}

static std::unordered_map<SourceFile*, int> createFileIndexMap(
	const std::vector<SourceFile*>& files) {
	std::unordered_map<SourceFile*, int> result;
	result.reserve(files.size());
	for (size_t i = 0; i < files.size(); i++) {
		result[files[i]] = static_cast<int>(i);
	}
	return result;
}

static int countGlobalSymbols(const std::vector<SourceFile*>& files) {
	int count = 0;
	for (SourceFile* file : files) {
		if (!isExternalOrCommonJSModule(file)) {
			count += static_cast<int>(file->Locals.size());
		}
	}
	return count;
}

Type* Checker::reportUnreliableWorker(Type* t) {
	if (t == markerSuperType || t == markerSubType || t == markerOtherType) {
		reliabilityFlags |= RelationComparisonResult::ReportsUnreliable;
	}
	return t;
}

Type* Checker::reportUnmeasurableWorker(Type* t) {
	if (t == markerSuperType || t == markerSubType || t == markerOtherType) {
		reliabilityFlags |= RelationComparisonResult::ReportsUnmeasurable;
	}
	return t;
}

Type* Checker::restrictiveMapperWorker(Type* t) {
	if (t->flags & TypeFlagsTypeParameter) {
		return getRestrictiveTypeParameter(t);
	}
	return t;
}

Type* Checker::permissiveMapperWorker(Type* t) {
	if (t->flags & TypeFlagsTypeParameter) {
		return wildcardType;
	}
	return t;
}

Type* Checker::getUniqueLiteralTypeForTypeParameter(Type* t) {
	if (t->flags & TypeFlagsTypeParameter) {
		return uniqueLiteralType;
	}
	return t;
}

// Resolve to the global class or interface by the given name and arity, or
// emptyObjectType/emptyGenericType otherwise
std::function<Type*()> Checker::getGlobalTypeResolver(const std::string& name,
                                                    int arity,
                                                    bool reportErrors) {
	return memoize([this, name, arity, reportErrors]() -> Type* {
		return getGlobalType(name, arity, reportErrors);
	});
}

// Resolve to the global type alias symbol by the given name and arity, or nil
// otherwise
std::function<Symbol*()> Checker::getGlobalTypeAliasResolver(
	const std::string& name, int arity, bool reportErrors) {
	return memoize([this, name, arity, reportErrors]() -> Symbol* {
		return getGlobalTypeAliasSymbol(name, arity, reportErrors);
	});
}

// Resolve to the global value symbol by the given name, or nil otherwise
std::function<Symbol*()> Checker::getGlobalValueSymbolResolver(
	const std::string& name, bool reportErrors) {
	return memoize([this, name, reportErrors]() -> Symbol* {
		return getGlobalSymbol(name, SymbolFlagsValue,
		                       reportErrors ? Cannot_find_global_value_0
		                                    : nullptr);
	});
}

std::function<Symbol*()> Checker::getGlobalTypeSymbolResolver(
	const std::string& name, bool reportErrors) {
	return memoize([this, name, reportErrors]() -> Symbol* {
		return getGlobalSymbol(name, SymbolFlagsType,
		                       reportErrors ? Cannot_find_global_type_0
		                                    : nullptr);
	});
}

std::function<std::vector<Type*>()> Checker::getGlobalTypesResolver(
	const std::vector<std::string>& names, int arity, bool reportErrors) {
	return memoize([this, names, arity, reportErrors]() -> std::vector<Type*> {
		std::vector<Type*> result;
		result.reserve(names.size());
		for (const std::string& name : names) {
			result.push_back(getGlobalType(name, arity, reportErrors));
		}
		return result;
	});
}

Symbol* Checker::getGlobalTypeAliasSymbol(const std::string& name, int arity,
                                          bool reportErrors) {
	Symbol* symbol = getGlobalSymbol(
		name, SymbolFlagsTypeAlias,
		reportErrors ? Cannot_find_global_type_0 : nullptr);
	if (symbol == nullptr) {
		return nullptr;
	}
	// Resolve the declared type of the symbol. This resolves type parameters for
	// the type alias so that we can check arity.
	getDeclaredTypeOfSymbol(symbol);
	if (static_cast<int>(typeAliasLinks.Get(symbol)->typeParameters.size()) !=
	    arity) {
		if (reportErrors) {
			Node* decl = nullptr;
			for (Node* d : symbol->declarations) {
				if (isTypeAliasDeclaration(d)) {
					decl = d;
					break;
				}
			}
			error(decl, Global_type_0_must_have_1_type_parameter_s,
			      std::vector<std::string>{symbolName(symbol),
			                               std::to_string(arity)});
		}
		return nullptr;
	}
	return symbol;
}

std::vector<Type*> Checker::getTypeAliasTypeParameters(Symbol* symbol) {
	if (!(symbol->flags & SymbolFlagsTypeAlias)) {
		TSC_UNREACHABLE(
			"Attempted to fetch type alias parameters for non-type-alias symbol");
	}
	getDeclaredTypeOfSymbol(symbol);
	return typeAliasLinks.Get(symbol)->typeParameters;
}

static Node* getGlobalTypeDeclaration(Symbol* symbol) {
	for (Node* declaration : symbol->declarations) {
		switch (declaration->kind) {
		case Kind::ClassDeclaration:
		case Kind::InterfaceDeclaration:
		case Kind::EnumDeclaration:
		case Kind::TypeAliasDeclaration:
			return declaration;
		default:
			break;
		}
	}
	return nullptr;
}

Type* Checker::getGlobalType(const std::string& name, int arity,
                             bool reportErrors) {
	Symbol* symbol = getGlobalSymbol(
		name, SymbolFlagsType,
		reportErrors ? Cannot_find_global_type_0 : nullptr);
	if (symbol != nullptr) {
		if (symbol->flags & (SymbolFlagsClass | SymbolFlagsInterface)) {
			Type* t = getDeclaredTypeOfSymbol(symbol);
			if (static_cast<int>(interfaceTypeTypeParameters(
			                        t->AsInterfaceType())
			                        .size()) == arity) {
				return t;
			}
			if (reportErrors) {
				error(getGlobalTypeDeclaration(symbol),
				      Global_type_0_must_have_1_type_parameter_s,
				      std::vector<std::string>{symbolName(symbol),
				                               std::to_string(arity)});
			}
		} else if (reportErrors) {
			error(getGlobalTypeDeclaration(symbol),
			      Global_type_0_must_be_a_class_or_interface_type,
			      symbolName(symbol));
		}
	}
	if (arity != 0) {
		return emptyGenericType;
	}
	return emptyObjectType;
}

Symbol* Checker::getGlobalSymbol(const std::string& name, SymbolFlags meaning,
                                 const DiagnosticMessage* diagnostic) {
	// Don't track references for global symbols anyway, so value if
	// `isReference` is arbitrary
	return resolveName(nullptr, name, meaning, diagnostic, false /*isUse*/,
	                   false /*excludeGlobals*/);
}

void Checker::initializeClosures() {
	isPrimitiveOrObjectOrEmptyType = [this](Type* t) {
		return t->flags & (TypeFlagsPrimitive | TypeFlagsNonPrimitive) ||
		       IsEmptyAnonymousObjectType(t);
	};
	containsMissingType = [this](Type* t) {
		return t == missingType ||
		       t->flags & TypeFlagsUnion && t->types()[0] == missingType;
	};
	couldContainTypeVariables = [this](Type* t) {
		return couldContainTypeVariablesWorker(t);
	};
	isStringIndexSignatureOnlyType = [this](Type* t) {
		return isStringIndexSignatureOnlyTypeWorker(t);
	};
	markNodeAssignments = [this](Node* node) {
		return markNodeAssignmentsWorker(node);
	};
	compareTypesAssignable = [this](Type* t1, Type* t2, bool reportErrors) {
		return compareTypesAssignableWorker(t1, t2, reportErrors);
	};
}

void Checker::initializeIterationResolvers() {
	syncIterationTypesResolver = linksArena.alloc<IterationTypesResolver>();
	syncIterationTypesResolver->iteratorSymbolName = "iterator";
	syncIterationTypesResolver->getGlobalIteratorType = getGlobalIteratorType;
	syncIterationTypesResolver->getGlobalIterableType = getGlobalIterableType;
	syncIterationTypesResolver->getGlobalIterableTypeChecked =
		getGlobalIterableTypeChecked;
	syncIterationTypesResolver->getGlobalIterableIteratorType =
		getGlobalIterableIteratorType;
	syncIterationTypesResolver->getGlobalIterableIteratorTypeChecked =
		getGlobalIterableIteratorTypeChecked;
	syncIterationTypesResolver->getGlobalIteratorObjectType =
		getGlobalIteratorObjectType;
	syncIterationTypesResolver->getGlobalGeneratorType = getGlobalGeneratorType;
	syncIterationTypesResolver->getGlobalBuiltinIteratorTypes =
		getGlobalTypesResolver({"ArrayIterator", "MapIterator", "SetIterator",
		                        "StringIterator"},
		                       1, false /*reportErrors*/);
	syncIterationTypesResolver->resolveIterationType =
		[](Type* t, Node* /*errorNode*/) { return t; };
	syncIterationTypesResolver->mustHaveANextMethodDiagnostic =
		An_iterator_must_have_a_next_method;
	syncIterationTypesResolver->mustBeAMethodDiagnostic =
		The_0_property_of_an_iterator_must_be_a_method;
	syncIterationTypesResolver->mustHaveAValueDiagnostic =
		The_type_returned_by_the_0_method_of_an_iterator_must_have_a_value_property;

	asyncIterationTypesResolver = linksArena.alloc<IterationTypesResolver>();
	asyncIterationTypesResolver->iteratorSymbolName = "asyncIterator";
	asyncIterationTypesResolver->getGlobalIteratorType =
		getGlobalAsyncIteratorType;
	asyncIterationTypesResolver->getGlobalIterableType =
		getGlobalAsyncIterableType;
	asyncIterationTypesResolver->getGlobalIterableTypeChecked =
		getGlobalAsyncIterableTypeChecked;
	asyncIterationTypesResolver->getGlobalIterableIteratorType =
		getGlobalAsyncIterableIteratorType;
	asyncIterationTypesResolver->getGlobalIterableIteratorTypeChecked =
		getGlobalAsyncIterableIteratorTypeChecked;
	asyncIterationTypesResolver->getGlobalIteratorObjectType =
		getGlobalAsyncIteratorObjectType;
	asyncIterationTypesResolver->getGlobalGeneratorType =
		getGlobalAsyncGeneratorType;
	asyncIterationTypesResolver->getGlobalBuiltinIteratorTypes =
		getGlobalTypesResolver({"ReadableStreamAsyncIterator"}, 1,
		                       false /*reportErrors*/);
	asyncIterationTypesResolver->resolveIterationType =
		[this](Type* t, Node* errorNode) {
			return getAwaitedTypeEx(
				t, errorNode,
				Type_of_await_operand_must_either_be_a_valid_promise_or_must_not_contain_a_callable_then_member);
		};
	asyncIterationTypesResolver->mustHaveANextMethodDiagnostic =
		An_async_iterator_must_have_a_next_method;
	asyncIterationTypesResolver->mustBeAMethodDiagnostic =
		The_0_property_of_an_async_iterator_must_be_a_method;
	asyncIterationTypesResolver->mustHaveAValueDiagnostic =
		The_type_returned_by_the_0_method_of_an_async_iterator_must_be_a_promise_for_a_type_with_a_value_property;
}

void Checker::initializeChecker() {
	// Initialize global symbol table
	std::vector<Symbol*> ambientModuleSymbols;
	std::vector<std::vector<Node*>> augmentations;
	augmentations.reserve(files.size());
	for (SourceFile* file : files) {
		if (!isExternalOrCommonJSModule(file)) {
			// It is an error for a non-external-module (i.e. script) to declare
			// its own `globalThis`.
			auto git = file->Locals.find("globalThis");
			if (git != file->Locals.end()) {
				for (Node* d : git->second->declarations) {
					addDiagnostic(NewDiagnosticForNode(
						d, Declaration_name_conflicts_with_built_in_global_identifier_0,
						std::vector<std::string>{"globalThis"}));
				}
			}
			for (auto& [name, symbol] : file->Locals) {
				// We defer merging of global ambient module declarations since
				// they may require other global symbols and types to be resolved.
				if (symbol->flags & SymbolFlagsModule &&
				    isAmbientModuleSymbolName(symbol->name)) {
					ambientModuleSymbols.push_back(symbol);
				} else {
					mergeGlobalSymbol(symbol);
				}
			}
		}
		for (PatternAmbientModule* m : file->PatternAmbientModules) {
			patternAmbientModules.push_back(*m);
		}
		augmentations.push_back(file->ModuleAugmentations);
		if (file->Symbol != nullptr) {
			// Merge in UMD exports with first-in-wins semantics (see #9771)
			for (auto& [name, symbol] : file->GlobalExports) {
				if (globals.find(name) == globals.end()) {
					globals[name] = symbol;  // UMD export
				}
			}
		}
	}
	// We do global augmentations separately from module augmentations (and before
	// creating global types) because they
	//  1. Affect global types. We won't have the correct global types until
	//     global augmentations are merged. Also,
	//  2. Module augmentation instantiation requires creating the type of a
	//     module, which, in turn, can require checking for an export or property
	//     on the module (if export=) which, in turn, can fall back to the
	//     apparent type of the module - either globalObjectType or
	//     globalFunctionType - which wouldn't exist if we did module
	//     augmentations prior to finalizing the global types.
	for (auto& list : augmentations) {
		for (Node* augmentation : list) {
			// Merge 'global' module augmentations. This needs to be done after
			// global symbol table is initialized to make sure that all ambient
			// modules are indexed
			if (isGlobalScopeAugmentation(augmentation->parent)) {
				mergeModuleAugmentation(augmentation);
			}
		}
	}
	addUndefinedToGlobalsOrErrorOnRedeclaration();
	valueSymbolLinks.Get(undefinedSymbol)->resolvedType = undefinedWideningType;
	valueSymbolLinks.Get(argumentsSymbol)->resolvedType =
		getGlobalType("IArguments", 0 /*arity*/, true /*reportErrors*/);
	valueSymbolLinks.Get(unknownSymbol)->resolvedType = errorType;
	valueSymbolLinks.Get(globalThisSymbol)->resolvedType =
		newObjectType(ObjectFlagsAnonymous, globalThisSymbol);
	// Initialize special types
	globalArrayType = getGlobalType("Array", 1, true);
	globalObjectType = getGlobalType("Object", 0, true);
	globalFunctionType = getGlobalType("Function", 0, true);
	globalCallableFunctionType = getGlobalStrictFunctionType("CallableFunction");
	globalNewableFunctionType = getGlobalStrictFunctionType("NewableFunction");
	globalStringType = getGlobalType("String", 0, true);
	globalNumberType = getGlobalType("Number", 0, true);
	globalBooleanType = getGlobalType("Boolean", 0, true);
	globalRegExpType = getGlobalType("RegExp", 0, true);
	anyArrayType = createArrayType(anyType);
	autoArrayType = createArrayType(autoType);
	if (autoArrayType == emptyObjectType) {
		// autoArrayType is used as a marker, so even if global Array type is not
		// defined, it needs to be a unique type
		autoArrayType = newAnonymousType(nullptr, {}, {}, {}, {});
	}
	globalReadonlyArrayType = getGlobalType("ReadonlyArray", 1, false);
	if (globalReadonlyArrayType == emptyGenericType) {
		globalReadonlyArrayType = globalArrayType;
	}
	anyReadonlyArrayType = createTypeFromGenericGlobalType(
		globalReadonlyArrayType, {anyType});
	globalThisType = getGlobalType("ThisType", 1, false);
	// Now merge global ambient module declarations
	for (Symbol* symbol : ambientModuleSymbols) {
		mergeGlobalSymbol(symbol);
	}
	mergePatternAmbientModules();
	// merge _nonglobal_ module augmentations.
	// this needs to be done after global symbol table is initialized to make
	// sure that all ambient modules are indexed
	for (auto& list : augmentations) {
		for (Node* augmentation : list) {
			if (!isGlobalScopeAugmentation(augmentation->parent)) {
				mergeModuleAugmentation(augmentation);
			}
		}
	}
}

void Checker::mergeGlobalSymbol(Symbol* symbol) {
	auto it = globals.find(symbol->name);
	Symbol* merged;
	if (it != globals.end()) {
		merged = mergeSymbol(it->second, symbol, false /*unidirectional*/);
	} else {
		merged = getMergedSymbol(symbol);
	}
	globals[symbol->name] = merged;
}

// Pattern ambient modules are merged together if they have the same pattern and
// identical import attributes type.
void Checker::mergePatternAmbientModules() {
	std::unordered_map<std::string, std::vector<int>> groupsByPattern;
	std::vector<PatternAmbientModule> grouped;
	grouped.reserve(patternAmbientModules.size());
	for (auto& module : patternAmbientModules) {
		Type* attributesType = getTypeOfModuleImportAttributes(module.symbol);
		int groupIndex = -1;
		for (int index : groupsByPattern[module.pattern]) {
			if (isTypeIdenticalTo(
			        attributesType,
			        getTypeOfModuleImportAttributes(grouped[index].symbol))) {
				groupIndex = index;
				break;
			}
		}
		if (groupIndex == -1) {
			groupsByPattern[module.pattern].push_back(
				static_cast<int>(grouped.size()));
			grouped.push_back(
				PatternAmbientModule{module.pattern, module.symbol});
		} else {
			grouped[groupIndex].symbol = mergeSymbol(
				grouped[groupIndex].symbol, module.symbol,
				false /*unidirectional*/);
		}
	}
	for (auto& module : patternAmbientModules) {
		if (globals.find(module.symbol->name) != globals.end()) {
			globals[module.symbol->name] = getMergedSymbol(module.symbol);
		}
	}
	patternAmbientModules = grouped;
}

void Checker::mergeModuleAugmentation(Node* moduleName) {
	Node* moduleNode = moduleName->parent;
	ModuleDeclaration* moduleAugmentation = moduleNode->as<ModuleDeclaration>();
	if (moduleAugmentation->Symbol->declarations[0] != moduleNode) {
		// this is a combined symbol for multiple augmentations within the same
		// file. its symbol already has accumulated information for all
		// declarations so we need to add it just once - do the work only for
		// first declaration
		return;
	}
	if (isGlobalScopeAugmentation(moduleNode)) {
		mergeSymbolTable(globals, moduleAugmentation->Symbol->exports,
		                 false /*unidirectional*/, nullptr /*parent*/);
	} else {
		// find a module that about to be augmented
		// do not validate names of augmentations that are defined in ambient
		// context
		const DiagnosticMessage* moduleNotFoundError = nullptr;
		if (!(moduleName->parent->parent->flags & NodeFlagsAmbient)) {
			moduleNotFoundError =
				Invalid_module_name_in_augmentation_module_0_cannot_be_found;
		}
		// We ban import attributes on module augmentation declarations.
		Symbol* mainModule = resolveExternalModuleNameWorker(
			moduleName, moduleName, moduleNotFoundError,
			false /*ignoreErrors*/, true /*isForAugmentation*/,
			nullptr /*importAttributesType*/);
		if (mainModule == nullptr) {
			return;
		}
		// obtain item referenced by 'export='
		mainModule = resolveExternalModuleSymbol(mainModule,
		                                         false /*dontResolveAlias*/);
		if (mainModule->flags & SymbolFlagsNamespace) {
			// If we're merging an augmentation to a pattern ambient module, we
			// want to perform the merge unidirectionally from the augmentation
			// ('a.foo') to the pattern ('*.foo'), so that 'getMergedSymbol()' on
			// a.foo gives you all the exports both from the pattern and from the
			// augmentation, but 'getMergedSymbol()' on *.foo only gives you
			// exports from *.foo.
			bool isPatternModule = someList(
				patternAmbientModules, [&](const PatternAmbientModule& m) {
					return mainModule == getMergedSymbol(m.symbol);
				});
			if (isPatternModule) {
				Symbol* merged = mergeSymbol(moduleAugmentation->Symbol,
				                             mainModule, true /*unidirectional*/);
				// moduleName will be a StringLiteral since this is not `declare
				// global`.
				patternAmbientModuleAugmentations[moduleName->text()] = merged;
				patternAmbientModuleAugmentationTargets[moduleName->text()] =
					mainModule;
			} else {
				if (mainModule->exports.find(InternalSymbolNameExportStar) !=
				        mainModule->exports.end() &&
				    !moduleAugmentation->Symbol->exports.empty()) {
					// We may need to merge the module augmentation's exports into
					// the target symbols of the resolved exports
					SymbolTable resolvedExports =
						getResolvedMembersOrExportsOfSymbol(
							mainModule,
							MembersOrExportsResolutionKindResolvedExports);
					for (auto& [key, value] :
					     moduleAugmentation->Symbol->exports) {
						if (resolvedExports.find(key) != resolvedExports.end() &&
						    mainModule->exports.find(key) ==
						        mainModule->exports.end()) {
							mergeSymbol(resolvedExports[key], value,
							            false /*unidirectional*/);
						}
					}
				}
				mergeSymbol(mainModule, moduleAugmentation->Symbol,
				            false /*unidirectional*/);
			}
		} else {
			// moduleName will be a StringLiteral since this is not `declare
			// global`.
			error(moduleName,
			      Cannot_augment_module_0_because_it_resolves_to_a_non_module_entity,
			      moduleName->text());
		}
	}
}

void Checker::addUndefinedToGlobalsOrErrorOnRedeclaration() {
	const std::string& name = undefinedSymbol->name;
	auto it = globals.find(name);
	if (it != globals.end()) {
		for (Node* declaration : it->second->declarations) {
			if (!isTypeDeclaration(declaration)) {
				addDiagnostic(createDiagnosticForNode(
					declaration,
					Declaration_name_conflicts_with_built_in_global_identifier_0,
					std::vector<std::string>{name}));
			}
		}
	} else {
		globals[name] = undefinedSymbol;
	}
}

// ---------------------------------------------------------------------------
// Name resolver wiring (checker.go:1507-2216)
// ---------------------------------------------------------------------------

static binder::NameResolver* newNameResolverImpl(
	Checker* c, bool forSuggestion) {
	binder::NameResolver* nr = c->linksArena.alloc<binder::NameResolver>();
	nr->compilerOptions = const_cast<CompilerOptions*>(c->compilerOptions);
	nr->getSymbolOfDeclaration = [c](Node* n) {
		return c->getSymbolOfDeclaration(n);
	};
	nr->error = [c](Node* n, const DiagnosticMessage* m,
	                const std::vector<std::string>& args) {
		return c->error(n, m, args);
	};
	nr->globals = &c->globals;
	nr->argumentsSymbol = c->argumentsSymbol;
	nr->requireSymbol = c->requireSymbol;
	if (forSuggestion) {
		nr->lookup = [c](SymbolTable* symbols, const std::string& name,
		               SymbolFlags meaning) {
			return c->getSuggestionForSymbolNameLookup(*symbols, name, meaning);
		};
	} else {
		nr->lookup = [c](SymbolTable* symbols, const std::string& name,
		               SymbolFlags meaning) {
			return c->getSymbol(*symbols, name, meaning);
		};
		nr->onPropertyWithInvalidInitializer =
			[c](Node* errorLocation, const std::string& name,
			   Node* propertyWithInvalidInitializer, Symbol* result) {
				return c->checkAndReportErrorForInvalidInitializer(
					errorLocation, name, propertyWithInvalidInitializer,
					result);
			};
		nr->onFailedToResolveSymbol =
			[c](Node* errorLocation, const std::string& name,
			   SymbolFlags meaning,
			   const DiagnosticMessage* nameNotFoundMessage) {
				c->onFailedToResolveSymbol(errorLocation, name, meaning,
				                           nameNotFoundMessage);
			};
		nr->onSuccessfullyResolvedSymbol =
			[c](Node* errorLocation, Symbol* result, SymbolFlags meaning,
			   Node* lastLocation,
			   Node* associatedDeclarationForContainingInitializerOrBindingName,
			   bool withinDeferredContext) {
				c->onSuccessfullyResolvedSymbol(
					errorLocation, result, meaning, lastLocation,
					associatedDeclarationForContainingInitializerOrBindingName,
					withinDeferredContext);
			};
	}
	nr->symbolReferenced = [c](Symbol* s, SymbolFlags meaning) {
		c->symbolReferenced(s, meaning);
	};
	nr->setRequiresScopeChangeCache = [c](Node* n, Tristate v) {
		c->setRequiresScopeChangeCache(n, v);
	};
	nr->getRequiresScopeChangeCache = [c](Node* n) {
		return c->getRequiresScopeChangeCache(n);
	};
	return nr;
}

void Checker::symbolReferenced(Symbol* symbol, SymbolFlags meaning) {
	symbolReferenceLinks.Get(symbol)->referenceKinds |= meaning;
}

Tristate Checker::getRequiresScopeChangeCache(Node* node) {
	return nodeLinks.Get(node)->declarationRequiresScopeChange;
}

void Checker::setRequiresScopeChangeCache(Node* node, Tristate value) {
	nodeLinks.Get(node)->declarationRequiresScopeChange = value;
}

bool isTypeReferenceIdentifier(Node* node);

// The invalid initializer error is needed in two situation:
// 1. When result is undefined, after checking for a missing "this."
// 2. When result is defined
bool Checker::checkAndReportErrorForInvalidInitializer(
	Node* errorLocation, const std::string& name,
	Node* propertyWithInvalidInitializer, Symbol* result) {
	if (!compilerOptions->GetEmitStandardClassFields()) {
		if (errorLocation != nullptr && result == nullptr &&
		    checkAndReportErrorForMissingPrefix(errorLocation, name)) {
			return true;
		}
		// We have a match, but the reference occurred within a property
		// initializer and the identifier also binds to a local variable in the
		// constructor where the code will be emitted. Note that this is actually
		// allowed with emitStandardClassFields because the scope semantics are
		// different.
		PropertyDeclaration* prop =
			propertyWithInvalidInitializer->as<PropertyDeclaration>();
		const DiagnosticMessage* message =
			errorLocation != nullptr && prop->Type != nullptr &&
			        prop->Type->loc.containsInclusive(errorLocation->pos())
			    ? Type_of_instance_member_variable_0_cannot_reference_identifier_1_declared_in_the_constructor
			    : Initializer_of_instance_member_variable_0_cannot_reference_identifier_1_declared_in_the_constructor;
		error(errorLocation, message,
		      std::vector<std::string>{
		          declarationNameToString(prop->name), name});
		return true;
	}
	return false;
}

bool Checker::checkAndReportErrorForMissingPrefix(Node* errorLocation,
                                                  const std::string& name) {
	if (!isIdentifier(errorLocation) || errorLocation->text() != name ||
	    isTypeReferenceIdentifier(errorLocation) ||
	    isInTypeQuery(errorLocation)) {
		return false;
	}
	Node* container = getThisContainer(errorLocation,
	                                   false /*includeArrowFunctions*/,
	                                   false /*includeClassComputedPropertyName*/);
	for (Node* location = container; location->parent != nullptr;
	     location = location->parent) {
		if (isClassLike(location->parent)) {
			Symbol* classSymbol = getSymbolOfDeclaration(location->parent);
			if (classSymbol == nullptr) {
				break;
			}
			// Check to see if a static member exists.
			Type* constructorType = getTypeOfSymbol(classSymbol);
			if (getPropertyOfType(constructorType, name) != nullptr) {
				error(errorLocation,
				      Cannot_find_name_0_Did_you_mean_the_static_member_1_0,
				      std::vector<std::string>{
				          name, symbolToString(classSymbol)});
				return true;
			}
			// No static member is present.
			// Check if we're in an instance method and look for a relevant
			// instance member.
			if (location == container && !isStatic(location)) {
				Type* instanceType =
					getDeclaredTypeOfSymbol(classSymbol)
						->AsInterfaceType()
						->thisType;
				// TODO: GH#18217
				if (getPropertyOfType(instanceType, name) != nullptr) {
					error(errorLocation,
					      Cannot_find_name_0_Did_you_mean_the_instance_member_this_0,
					      name);
					return true;
				}
			}
		}
	}
	return false;
}

bool isTypeReferenceIdentifier(Node* node);

static bool isPrimitiveTypeName(const std::string& s) {
	return s == "any" || s == "string" || s == "number" || s == "boolean" ||
	       s == "never" || s == "unknown";
}

static bool isES2015OrLaterConstructorName(const std::string& s) {
	return s == "Promise" || s == "Symbol" || s == "Map" || s == "WeakMap" ||
	       s == "Set" || s == "WeakSet";
}

bool isTypeReferenceIdentifier(Node* node) /* impl below */ {
	while (node->parent->kind == Kind::QualifiedName) {
		node = node->parent;
	}
	return isTypeReferenceNode(node->parent);
}

static bool isConstTypeReferenceName(Node* node) {
	return node != nullptr && isIdentifier(node) && node->parent != nullptr &&
	       isConstTypeReference(node->parent) &&
	       node->parent->parent != nullptr &&
	       isAssertionExpression(node->parent->parent);
}

static bool isExportAssignmentExpressionName(Node* node) {
	if (node == nullptr) {
		return false;
	}
	Node* current = node;
	while (current->parent != nullptr &&
	       isPropertyAccessOrQualifiedName(current->parent)) {
		current = current->parent;
	}
	return current->parent != nullptr &&
	       isExportAssignment(current->parent) &&
	       current->parent->expression() == current;
}

bool Checker::maybeMappedType(Node* node, Symbol* symbol) {
	for (;;) {
		node = node->parent;
		if (!(isComputedPropertyName(node) ||
		      isPropertySignatureDeclaration(node))) {
			break;
		}
	}
	if (isTypeLiteralNode(node) && node->members().size() == 1) {
		Type* t = getDeclaredTypeOfSymbol(symbol);
		return t->flags & TypeFlagsUnion &&
		       allTypesAssignableToKindEx(t, TypeFlagsStringOrNumberLiteral,
		                                  true /*strict*/);
	}
	return false;
}

Node* Checker::getEntityNameForExtendingInterface(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
	case Kind::QualifiedName:
	case Kind::PropertyAccessExpression:
		if (node->parent != nullptr) {
			return getEntityNameForExtendingInterface(node->parent);
		}
		break;
	case Kind::TypeReference:
		return node->as<TypeReferenceNode>()->TypeName;
	case Kind::ExpressionWithTypeArguments:
		if (isEntityNameExpression(node->expression())) {
			return node->expression();
		}
		break;
	default:
		break;
	}
	return nullptr;
}

bool Checker::checkAndReportErrorForExtendingInterface(Node* errorLocation) {
	Node* expression = getEntityNameForExtendingInterface(errorLocation);
	if (expression != nullptr &&
	    resolveEntityName(expression, SymbolFlagsInterface,
	                      true /*ignoreErrors*/, false, nullptr) != nullptr) {
		error(errorLocation, Cannot_extend_an_interface_0_Did_you_mean_implements,
		      getTextOfNode(expression));
		return true;
	}
	return false;
}

bool Checker::checkAndReportErrorForUsingTypeAsNamespace(
	Node* errorLocation, const std::string& name, SymbolFlags meaning) {
	if (meaning == SymbolFlagsNamespace) {
		Symbol* symbol = resolveSymbol(resolveName(
			errorLocation, name, SymbolFlagsType & ~SymbolFlagsNamespace,
			nullptr /*nameNotFoundMessage*/, false /*isUse*/,
			false /*excludeGlobals*/));
		if (symbol != nullptr) {
			Node* parent = errorLocation->parent;
			if (isQualifiedName(parent)) {
				TSC_ASSERT(parent->as<QualifiedName>()->Left == errorLocation,
				           "Should only be resolving left side of qualified "
				           "name as a namespace");
				std::string propName = parent->as<QualifiedName>()->Right->text();
				Symbol* propType = getPropertyOfType(
					getDeclaredTypeOfSymbol(symbol), propName);
				if (propType != nullptr) {
					error(parent,
					      Cannot_access_0_1_because_0_is_a_type_but_not_a_namespace_Did_you_mean_to_retrieve_the_type_of_the_property_1_in_0_with_0_1,
					      std::vector<std::string>{name, propName});
					return true;
				}
			}
			error(errorLocation,
			      X_0_only_refers_to_a_type_but_is_being_used_as_a_namespace_here,
			      name);
			return true;
		}
	}
	return false;
}

bool Checker::checkAndReportErrorForExportingPrimitiveType(
	Node* errorLocation, const std::string& name) {
	if (isPrimitiveTypeName(name) &&
	    errorLocation->parent->kind == Kind::ExportSpecifier) {
		error(errorLocation,
		      Cannot_export_0_Only_local_declarations_can_be_exported_from_a_module,
		      name);
		return true;
	}
	return false;
}

bool Checker::checkAndReportErrorForUsingNamespaceAsTypeOrValue(
	Node* errorLocation, const std::string& name, SymbolFlags meaning) {
	if (meaning & (SymbolFlagsValue & ~SymbolFlagsType)) {
		Symbol* symbol = resolveSymbol(resolveName(
			errorLocation, name, SymbolFlagsNamespaceModule,
			nullptr /*nameNotFoundMessage*/, false /*isUse*/,
			false /*excludeGlobals*/));
		if (symbol != nullptr) {
			// `export = ns` may legitimately reference a namespace;
			// checkExportAssignment decides whether that is an error, so don't
			// report "cannot use namespace as a value" here.
			if (!isExportAssignmentExpressionName(errorLocation)) {
				error(errorLocation, Cannot_use_namespace_0_as_a_value, name);
			}
			return true;
		}
	} else if (meaning & (SymbolFlagsType & ~SymbolFlagsValue)) {
		Symbol* symbol = resolveSymbol(resolveName(
			errorLocation, name, SymbolFlagsModule,
			nullptr /*nameNotFoundMessage*/, false /*isUse*/,
			false /*excludeGlobals*/));
		if (symbol != nullptr) {
			error(errorLocation, Cannot_use_namespace_0_as_a_type, name);
			return true;
		}
	}
	return false;
}

bool Checker::checkAndReportErrorForUsingTypeAsValue(
	Node* errorLocation, const std::string& name, SymbolFlags meaning) {
	if (meaning & SymbolFlagsValue) {
		if (isPrimitiveTypeName(name)) {
			Node* grandparent = errorLocation->parent->parent;
			if (grandparent != nullptr && grandparent->parent != nullptr &&
			    isHeritageClause(grandparent)) {
				Kind heritageKind = grandparent->as<HeritageClause>()->Token;
				Kind containerKind = grandparent->parent->kind;
				if (containerKind == Kind::InterfaceDeclaration &&
				    heritageKind == Kind::ExtendsKeyword) {
					error(errorLocation,
					      An_interface_cannot_extend_a_primitive_type_like_0_It_can_only_extend_other_named_object_types,
					      name);
				} else if (isClassLike(grandparent->parent) &&
				           heritageKind == Kind::ExtendsKeyword) {
					error(errorLocation,
					      A_class_cannot_extend_a_primitive_type_like_0_Classes_can_only_extend_constructable_values,
					      name);
				} else if (isClassLike(grandparent->parent) &&
				           heritageKind == Kind::ImplementsKeyword) {
					error(errorLocation,
					      A_class_cannot_implement_a_primitive_type_like_0_It_can_only_implement_other_named_object_types,
					      name);
				}
			} else {
				error(errorLocation,
				      X_0_only_refers_to_a_type_but_is_being_used_as_a_value_here,
				      name);
			}
			return true;
		}
		Symbol* symbol = resolveSymbol(resolveName(
			errorLocation, name, SymbolFlagsType & ~SymbolFlagsValue,
			nullptr /*nameNotFoundMessage*/, false /*isUse*/,
			false /*excludeGlobals*/));
		if (symbol != nullptr) {
			SymbolFlags allFlags = getSymbolFlags(symbol);
			if (!(allFlags & SymbolFlagsValue)) {
				// `export = SomeType` may legitimately reference a type-only
				// name; checkExportAssignment decides whether that is an error,
				// so don't report "used as a value" here.
				if (isExportAssignmentExpressionName(errorLocation)) {
					return true;
				}
				if (isES2015OrLaterConstructorName(name)) {
					error(errorLocation,
					      X_0_only_refers_to_a_type_but_is_being_used_as_a_value_here_Do_you_need_to_change_your_target_library_Try_changing_the_lib_compiler_option_to_es2015_or_later,
					      name);
				} else if (maybeMappedType(errorLocation, symbol)) {
					error(errorLocation,
					      X_0_only_refers_to_a_type_but_is_being_used_as_a_value_here_Did_you_mean_to_use_1_in_0,
					      std::vector<std::string>{
					          name, name == "K" ? "P" : "K"});
				} else {
					error(errorLocation,
					      X_0_only_refers_to_a_type_but_is_being_used_as_a_value_here,
					      name);
				}
				return true;
			}
		}
	}
	return false;
}

bool Checker::checkAndReportErrorForUsingValueAsType(
	Node* errorLocation, const std::string& name, SymbolFlags meaning) {
	if (meaning & (SymbolFlagsType & ~SymbolFlagsNamespace)) {
		Symbol* symbol = resolveSymbol(resolveName(
			errorLocation, name, ~SymbolFlagsType & SymbolFlagsValue,
			nullptr /*nameNotFoundMessage*/, false /*isUse*/,
			false /*excludeGlobals*/));
		if (symbol != nullptr && !(symbol->flags & SymbolFlagsNamespace)) {
			error(errorLocation,
			      X_0_refers_to_a_value_but_is_being_used_as_a_type_here_Did_you_mean_typeof_0,
			      name);
			return true;
		}
	}
	return false;
}

// utilities.go FeatureMapEntry table — ported verbatim.
struct FeatureMapEntry {
	const char* lib;
	std::vector<const char*> props;
};

static const std::unordered_map<std::string, std::vector<FeatureMapEntry>>&
getFeatureMap() {
	static const std::unordered_map<std::string, std::vector<FeatureMapEntry>>
		map = {
			{"Array",
			 {{"es2015",
			   {"find", "findIndex", "fill", "copyWithin", "entries", "keys",
			    "values"}},
			  {"es2016", {"includes"}},
			  {"es2019", {"flat", "flatMap"}},
			  {"es2022", {"at"}},
			  {"es2023",
			   {"findLastIndex", "findLast", "toReversed", "toSorted",
			    "toSpliced", "with"}}}},
			{"Iterator", {{"es2015", {}}}},
			{"IteratorConstructor", {{"es2026", {"concat"}}}},
			{"RawJSON", {{"es2026", {}}}},
			{"JSON", {{"es2026", {"isRawJSON", "rawJSON"}}}},
			{"AsyncIterator", {{"es2015", {}}}},
			{"ArrayBuffer",
			 {{"es2024",
			   {"maxByteLength", "resizable", "resize", "detached", "transfer",
			    "transferToFixedLength"}}}},
			{"Atomics",
			 {{"es2017",
			   {"add", "and", "compareExchange", "exchange", "isLockFree",
			    "load", "or", "store", "sub", "wait", "notify", "xor"}},
			  {"es2024", {"waitAsync"}}}},
			{"SharedArrayBuffer",
			 {{"es2017", {"byteLength", "slice"}},
			  {"es2024", {"growable", "maxByteLength", "grow"}}}},
			{"AsyncIterable", {{"es2018", {}}}},
			{"AsyncIterableIterator", {{"es2018", {}}}},
			{"AsyncGenerator", {{"es2018", {}}}},
			{"AsyncGeneratorFunction", {{"es2018", {}}}},
			{"RegExp",
			 {{"es2015", {"flags", "sticky", "unicode"}},
			  {"es2018", {"dotAll"}},
			  {"es2024", {"unicodeSets"}}}},
			{"RegExpConstructor", {{"es2025", {"escape"}}}},
			{"Reflect",
			 {{"es2015",
			   {"apply", "construct", "defineProperty", "deleteProperty", "get",
			    "getOwnPropertyDescriptor", "getPrototypeOf", "has",
			    "isExtensible", "ownKeys", "preventExtensions", "set",
			    "setPrototypeOf"}}}},
			{"ArrayConstructor",
			 {{"es2015", {"from", "of"}}, {"es2026", {"fromAsync"}}}},
			{"ObjectConstructor",
			 {{"es2015",
			   {"assign", "getOwnPropertySymbols", "keys", "is",
			    "setPrototypeOf"}},
			  {"es2017", {"values", "entries", "getOwnPropertyDescriptors"}},
			  {"es2019", {"fromEntries"}},
			  {"es2022", {"hasOwn"}},
			  {"es2024", {"groupBy"}}}},
			{"NumberConstructor",
			 {{"es2015",
			   {"isFinite", "isInteger", "isNaN", "isSafeInteger", "parseFloat",
			    "parseInt"}}}},
			{"Math",
			 {{"es2015",
			   {"clz32", "imul", "sign", "log10", "log2", "log1p", "expm1",
			    "cosh", "sinh", "tanh", "acosh", "asinh", "atanh", "hypot",
			    "trunc", "fround", "cbrt"}},
			  {"es2025", {"f16round"}},
			  {"es2026", {"sumPrecise"}}}},
			{"Map",
			 {{"es2015", {"entries", "keys", "values"}},
			  {"es2026", {"getOrInsert", "getOrInsertComputed"}}}},
			{"MapConstructor", {{"es2024", {"groupBy"}}}},
			{"Float64Array",
			 {{"es2022", {"at"}},
			  {"es2023",
			   {"findLastIndex", "findLast", "toReversed", "toSorted",
			    "toSpliced", "with"}}}},
			{"Int32Array",
			 {{"es2022", {"at"}}, {"es2024", {"groupBy"}}}},
			{"Set",
			 {{"es2015", {"entries", "keys", "values"}},
			  {"es2025",
			   {"union", "intersection", "difference", "symmetricDifference",
			    "isSubsetOf", "isSupersetOf", "isDisjointFrom"}}}},
			{"PromiseConstructor",
			 {{"es2015", {"all", "race", "reject", "resolve"}},
			  {"es2020", {"allSettled"}},
			  {"es2021", {"any"}},
			  {"es2024", {"withResolvers"}},
			  {"es2025", {"try"}}}},
			{"Symbol",
			 {{"es2015", {"for", "keyFor"}}, {"es2019", {"description"}}}},
			{"WeakMap",
			 {{"es2015", {}},
			  {"es2026", {"getOrInsert", "getOrInsertComputed"}}}},
			{"WeakSet", {{"es2015", {}}}},
			{"String",
			 {{"es2015",
			   {"codePointAt", "includes", "endsWith", "normalize", "repeat",
			    "startsWith", "anchor", "big", "blink", "bold", "fixed",
			    "fontcolor", "fontsize", "italics", "link", "small", "strike",
			    "sub", "sup"}},
			  {"es2017", {"padStart", "padEnd"}},
			  {"es2019", {"trimStart", "trimEnd", "trimLeft", "trimRight"}},
			  {"es2020", {"matchAll"}},
			  {"es2021", {"replaceAll"}},
			  {"es2022", {"at"}},
			  {"es2024", {"isWellFormed", "toWellFormed"}}}},
			{"StringConstructor",
			 {{"es2015", {"fromCodePoint", "raw"}}}},
			{"DateTimeFormat", {{"es2017", {"formatToParts"}}}},
			{"Promise", {{"es2015", {}}, {"es2018", {"finally"}}}},
			{"RegExpMatchArray", {{"es2018", {"groups"}}}},
			{"RegExpExecArray", {{"es2018", {"groups"}}}},
			{"Intl",
			 {{"es2018", {"PluralRules"}},
			  {"es2020", {"RelativeTimeFormat", "Locale", "DisplayNames"}},
			  {"es2021", {"ListFormat", "DateTimeFormat"}},
			  {"es2022", {"Segmenter"}},
			  {"es2025", {"DurationFormat"}}}},
			{"NumberFormat", {{"es2018", {"formatToParts"}}}},
			{"SymbolConstructor",
			 {{"es2020", {"matchAll"}},
			  {"esnext", {"metadata", "dispose", "asyncDispose"}}}},
			{"DataView",
			 {{"es2020",
			   {"setBigInt64", "setBigUint64", "getBigInt64", "getBigUint64"}},
			  {"es2025", {"setFloat16", "getFloat16"}}}},
			{"BigInt", {{"es2020", {}}}},
			{"RelativeTimeFormat",
			 {{"es2020", {"format", "formatToParts", "resolvedOptions"}}}},
			{"Int8Array",
			 {{"es2022", {"at"}},
			  {"es2023",
			   {"findLastIndex", "findLast", "toReversed", "toSorted",
			    "toSpliced", "with"}}}},
			{"Uint8Array",
			 {{"es2022", {"at"}},
			  {"es2023",
			   {"findLastIndex", "findLast", "toReversed", "toSorted",
			    "toSpliced", "with"}},
			  {"es2026", {"toBase64", "setFromBase64", "toHex", "setFromHex"}}}},
			{"Uint8ClampedArray",
			 {{"es2022", {"at"}},
			  {"es2023",
			   {"findLastIndex", "findLast", "toReversed", "toSorted",
			    "toSpliced", "with"}}}},
			{"Int16Array",
			 {{"es2022", {"at"}},
			  {"es2023",
			   {"findLastIndex", "findLast", "toReversed", "toSorted",
			    "toSpliced", "with"}}}},
			{"Uint16Array",
			 {{"es2022", {"at"}},
			  {"es2023",
			   {"findLastIndex", "findLast", "toReversed", "toSorted",
			    "toSpliced", "with"}}}},
		};
	return map;
}

std::string Checker::getSuggestedLibForNonExistentName(
	const std::string& name) {
	const auto& featureMap = getFeatureMap();
	auto it = featureMap.find(name);
	if (it != featureMap.end()) {
		return it->second[0].lib;
	}
	return "";
}

std::string Checker::getSuggestedLibForNonExistentProperty(
	const std::string& missingProperty, Type* containingType) {
	Symbol* container = getApparentType(containingType)->symbol;
	if (container != nullptr) {
		const auto& featureMap = getFeatureMap();
		auto it = featureMap.find(container->name);
		if (it != featureMap.end()) {
			for (const FeatureMapEntry& entry : it->second) {
				if (std::find(entry.props.begin(), entry.props.end(),
				              missingProperty) != entry.props.end()) {
					return entry.lib;
				}
			}
		}
	}
	return "";
}

Symbol* Checker::getSuggestedSymbolForNonexistentSymbol(
	Node* location, const std::string& outerName, SymbolFlags meaning) {
	return resolveNameForSymbolSuggestion(location, outerName, meaning,
	                                      nullptr /*nameNotFoundMessage*/,
	                                      false /*isUse*/,
	                                      false /*excludeGlobals*/);
}

// Except for candidates:
//   - With no name
//   - Whose meaning doesn't match the `meaning` parameter.
//   - Whose length differs from the target name by more than 0.34 of the length
//     of the name.
//   - Whose levenshtein distance is more than 0.4 of the length of the name (0.4
//     allows 1 substitution/transposition for every 5 characters, and 1
//     insertion/deletion at 3 characters)
Symbol* Checker::getSpellingSuggestionForName(
	const std::string& name, const std::vector<Symbol*>& symbols,
	SymbolFlags meaning) {
	return getSpellingSuggestion<Symbol*>(
		name, symbols,
		[this, meaning](Symbol* candidate) -> std::string {
			std::string candidateName = symbolName(candidate);
			if (candidateName.empty() || candidateName[0] == '"' ||
			    candidateName[0] == '\xFE') {
				return "";
			}
			if (candidate->flags & meaning) {
				return candidateName;
			}
			if (candidate->flags & SymbolFlagsAlias) {
				Symbol* alias = tryResolveAlias(candidate);
				if (alias != nullptr && alias->flags & meaning) {
					return candidateName;
				}
			}
			return "";
		},
		[this](Symbol* a, Symbol* b) { return compareSymbols(a, b); });
}

static std::unordered_map<std::string, Symbol*>&
primitiveTypeAliasSuggestions(Arena& arena) {
	static std::unordered_map<std::string, Symbol*>* result = nullptr;
	if (result == nullptr) {
		result = new std::unordered_map<std::string, Symbol*>();
		for (auto& e : std::vector<std::pair<const char*, const char*>>{
		         {"string", "String"}, {"number", "Number"},
		         {"boolean", "Boolean"}, {"object", "Object"},
		         {"bigint", "BigInt"}, {"symbol", "Symbol"}}) {
			Symbol* sym = arena.alloc<Symbol>();
			sym->flags = SymbolFlagsTypeAlias | SymbolFlagsTransient;
			sym->name = e.first;
			(*result)[e.second] = sym;
		}
	}
	return *result;
}

static std::vector<Symbol*> getPrimitiveTypeAliasSuggestions(
	SymbolTable& symbols, Arena& arena) {
	std::vector<Symbol*> out;
	for (auto& [builtinName, suggestion] :
	     primitiveTypeAliasSuggestions(arena)) {
		if (symbols.find(builtinName) != symbols.end()) {
			out.push_back(suggestion);
		}
	}
	return out;
}

Symbol* Checker::getSuggestionForSymbolNameLookup(SymbolTable& symbols,
                                                const std::string& name,
                                                SymbolFlags meaning) {
	Symbol* symbol = getSymbol(symbols, name, meaning);
	if (symbol != nullptr) {
		return symbol;
	}
	std::vector<Symbol*> candidates;
	candidates.reserve(symbols.size());
	for (auto& [k, v] : symbols) {
		candidates.push_back(v);
	}
	if (meaning & SymbolFlagsGlobalLookup) {
		for (Symbol* s :
		     getPrimitiveTypeAliasSuggestions(symbols, typeArena)) {
			candidates.push_back(s);
		}
	}
	return getSpellingSuggestionForName(name, candidates, meaning);
}

bool Checker::isUncheckedJSSuggestion(Node* node, Symbol* suggestion,
                                      bool excludeClasses) {
	SourceFile* file = getSourceFileOfNode(node);
	if (file != nullptr) {
		if (compilerOptions->CheckJs == Tristate::Unknown &&
		    file->CheckJsDirective == nullptr &&
		    (file->ScriptKind == ScriptKind::JS ||
		     file->ScriptKind == ScriptKind::JSX)) {
			SourceFile* declarationFile = nullptr;
			if (suggestion != nullptr) {
				Node* firstDeclaration =
					suggestion->declarations.empty()
					    ? nullptr
					    : suggestion->declarations[0];
				if (firstDeclaration != nullptr) {
					declarationFile = getSourceFileOfNode(firstDeclaration);
				}
			}
			bool suggestionHasNoExtendsOrDecorators =
				suggestion == nullptr ||
				suggestion->valueDeclaration == nullptr ||
				!isClassLike(suggestion->valueDeclaration) ||
				!getExtendsHeritageClauseElements(
				     suggestion->valueDeclaration)
				     .empty() ||
				classOrConstructorParameterIsDecorated(
					false, suggestion->valueDeclaration);
			return !(file != declarationFile && declarationFile != nullptr &&
			         isGlobalSourceFile(static_cast<Node*>(declarationFile))) &&
			       !(excludeClasses && suggestion != nullptr &&
			         suggestion->flags & SymbolFlagsClass &&
			         suggestionHasNoExtendsOrDecorators) &&
			       !(node != nullptr && excludeClasses &&
			         isPropertyAccessExpression(node) &&
			         node->expression()->kind == Kind::ThisKeyword &&
			         suggestionHasNoExtendsOrDecorators);
		}
	}
	return false;
}

void Checker::onFailedToResolveSymbol(
	Node* errorLocation, const std::string& name, SymbolFlags meaning,
	const DiagnosticMessage* nameNotFoundMessage) {
	// The `const` in a `const` assertion (`x as const`) is a syntactic marker,
	// not a real type reference, and must never be resolved or reported as an
	// unresolvable name.
	if (isConstTypeReferenceName(errorLocation)) {
		return;
	}
	if (errorLocation != nullptr &&
	    (errorLocation->parent->kind == Kind::JSDocLink ||
	     checkAndReportErrorForMissingPrefix(errorLocation, name) ||
	     checkAndReportErrorForExtendingInterface(errorLocation) ||
	     checkAndReportErrorForUsingTypeAsNamespace(errorLocation, name,
	                                                meaning) ||
	     checkAndReportErrorForExportingPrimitiveType(errorLocation, name) ||
	     checkAndReportErrorForUsingNamespaceAsTypeOrValue(errorLocation, name,
	                                                       meaning) ||
	     checkAndReportErrorForUsingTypeAsValue(errorLocation, name, meaning) ||
	     checkAndReportErrorForUsingValueAsType(errorLocation, name, meaning))) {
		return;
	}
	std::string declarationName = name;
	if (errorLocation != nullptr && isIdentifier(errorLocation) &&
	    errorLocation->text() == name) {
		declarationName = declarationNameToString(
			errorLocation);  // use escape sequences from original file
	}
	// Report missing lib first
	std::string suggestedLib = getSuggestedLibForNonExistentName(name);
	if (!suggestedLib.empty()) {
		error(errorLocation, nameNotFoundMessage,
		      std::vector<std::string>{declarationName, suggestedLib});
		return;
	}
	// Then spelling suggestions
	Symbol* suggestion =
		getSuggestedSymbolForNonexistentSymbol(errorLocation, name, meaning);
	if (suggestion != nullptr &&
	    !(suggestion->valueDeclaration != nullptr &&
	      isAmbientModule(suggestion->valueDeclaration) &&
	      isGlobalScopeAugmentation(suggestion->valueDeclaration))) {
		std::string suggestionName = symbolToString(suggestion);
		bool isUncheckedJS =
			isUncheckedJSSuggestion(errorLocation, suggestion,
			                        false /*excludeClasses*/);
		const DiagnosticMessage* message =
			meaning == SymbolFlagsNamespace
			    ? Cannot_find_namespace_0_Did_you_mean_1
			    : isUncheckedJS ? Could_not_find_name_0_Did_you_mean_1
			                    : Cannot_find_name_0_Did_you_mean_1;
		Diagnostic* diagnostic = NewDiagnosticForNode(
			errorLocation, message,
			std::vector<std::string>{declarationName, suggestionName});
		if (suggestion->valueDeclaration != nullptr) {
			diagnostic->AddRelatedInfo(NewDiagnosticForNode(
				suggestion->valueDeclaration, X_0_is_declared_here,
				{suggestionName}));
		}
		addErrorOrSuggestion(!isUncheckedJS, diagnostic);
		return;
	}
	// And then fall back to unspecified "not found"
	error(errorLocation, nameNotFoundMessage, declarationName);
}

void Checker::onSuccessfullyResolvedSymbol(
	Node* errorLocation, Symbol* result, SymbolFlags meaning,
	Node* lastLocation,
	Node* associatedDeclarationForContainingInitializerOrBindingName,
	bool withinDeferredContext) {
	std::string name = result->name;
	bool isInExternalModule =
		lastLocation != nullptr && isSourceFile(lastLocation) &&
		isExternalOrCommonJSModule(static_cast<SourceFile*>(lastLocation));
	// Only check for block-scoped variable if we have an error location and are
	// looking for the name with variable meaning
	//      For example,
	//          declare module foo {
	//              interface bar {}
	//          }
	//      const foo/*1*/: foo/*2*/.bar;
	// The foo at /*1*/ and /*2*/ will share same symbol with two meanings:
	// block-scoped variable and namespace module. However, only when we
	// try to resolve name in /*1*/ which is used in variable position,
	// we want to check for block-scoped
	if (errorLocation != nullptr &&
	    (meaning & SymbolFlagsBlockScopedVariable ||
	     meaning & (SymbolFlagsClass | SymbolFlagsEnum) &&
	         (meaning & SymbolFlagsValue) == SymbolFlagsValue)) {
		Symbol* exportOrLocalSymbol =
			getExportSymbolOfValueSymbolIfExported(result);
		if (exportOrLocalSymbol->flags &
		    (SymbolFlagsBlockScopedVariable | SymbolFlagsClass |
		     SymbolFlagsEnum)) {
			checkResolvedBlockScopedVariable(exportOrLocalSymbol,
			                                 errorLocation);
		}
	}
	// If we're in an external module, we can't reference value symbols created
	// from UMD export declarations
	if (isInExternalModule &&
	    (meaning & SymbolFlagsValue) == SymbolFlagsValue &&
	    !(errorLocation->flags & NodeFlagsJSDoc)) {
		Symbol* merged = getMergedSymbol(result);
		if (!merged->declarations.empty() &&
		    everyList(merged->declarations, [](Node* d) {
			    return isNamespaceExportDeclaration(d) ||
			           isSourceFile(d) &&
			               !static_cast<SourceFile*>(d)->GlobalExports.empty();
		    })) {
			errorOrSuggestion(
				compilerOptions->AllowUmdGlobalAccess != Tristate::True,
				errorLocation,
				X_0_refers_to_a_UMD_global_but_the_current_file_is_a_module_Consider_adding_an_import_instead,
				std::vector<std::string>{name});
		}
	}
	// If we're in a parameter initializer or binding name, we can't reference
	// the values of the parameter whose initializer we're within or parameters
	// to the right
	if (associatedDeclarationForContainingInitializerOrBindingName != nullptr &&
	    !withinDeferredContext &&
	    (meaning & SymbolFlagsValue) == SymbolFlagsValue) {
		Symbol* candidate = getMergedSymbol(getLateBoundSymbol(result));
		Node* root = getRootDeclaration(
			associatedDeclarationForContainingInitializerOrBindingName);
		// A parameter initializer or binding pattern initializer within a
		// parameter cannot refer to itself
		if (candidate ==
		    getSymbolOfDeclaration(
		        associatedDeclarationForContainingInitializerOrBindingName)) {
			error(errorLocation, Parameter_0_cannot_reference_itself,
			      declarationNameToString(
			          associatedDeclarationForContainingInitializerOrBindingName
			              ->name()));
		} else if (candidate->valueDeclaration != nullptr &&
		           candidate->valueDeclaration->pos() >
		               associatedDeclarationForContainingInitializerOrBindingName
		                   ->pos() &&
		           root->parent->locals() != nullptr &&
		           getSymbol(*root->parent->locals(), candidate->name, meaning) ==
		               candidate) {
			error(errorLocation,
			      Parameter_0_cannot_reference_identifier_1_declared_after_it,
			      std::vector<std::string>{
			          declarationNameToString(
			              associatedDeclarationForContainingInitializerOrBindingName
			                  ->name()),
			          declarationNameToString(errorLocation)});
		}
	}
	if (errorLocation != nullptr && meaning & SymbolFlagsValue &&
	    result->flags & SymbolFlagsAlias &&
	    !(result->flags & SymbolFlagsValue) &&
	    !isValidTypeOnlyAliasUseSite(errorLocation)) {
		Node* typeOnlyDeclaration =
			getTypeOnlyAliasDeclarationEx(result, SymbolFlagsValue);
		if (typeOnlyDeclaration != nullptr) {
			const DiagnosticMessage* message =
				nodeKindIs(typeOnlyDeclaration, Kind::ExportSpecifier,
				           Kind::ExportDeclaration, Kind::NamespaceExport)
				    ? X_0_cannot_be_used_as_a_value_because_it_was_exported_using_export_type
				    : X_0_cannot_be_used_as_a_value_because_it_was_imported_using_import_type;
			addTypeOnlyDeclarationRelatedInfo(
				error(errorLocation, message, name), typeOnlyDeclaration,
				name);
		}
	}
	// Look at 'compilerOptions.isolatedModules' and not 'getIsolatedModules(...)'
	// (which considers 'verbatimModuleSyntax') here because 'verbatimModuleSyntax'
	// will already have an error for importing a type without 'import type'.
	if (compilerOptions->IsolatedModules == Tristate::True && result != nullptr &&
	    isInExternalModule &&
	    (meaning & SymbolFlagsValue) == SymbolFlagsValue) {
		bool isGlobal = getSymbol(globals, name, meaning) == result;
		Symbol* nonValueSymbol = nullptr;
		if (isGlobal && isSourceFile(lastLocation)) {
			nonValueSymbol = getSymbol(*lastLocation->locals(), name,
			                           ~SymbolFlagsValue);
		}
		if (nonValueSymbol != nullptr) {
			Node* importDecl = nullptr;
			for (Node* d : nonValueSymbol->declarations) {
				if (nodeKindIs(d, {Kind::ImportSpecifier, Kind::ImportClause,
				                   Kind::NamespaceImport,
				                   Kind::ImportEqualsDeclaration})) {
					importDecl = d;
					break;
				}
			}
			if (importDecl != nullptr &&
			    !isTypeOnlyImportDeclaration(importDecl)) {
				error(importDecl,
				      Import_0_conflicts_with_global_value_used_in_this_file_so_must_be_declared_with_a_type_only_import_when_isolatedModules_is_enabled,
				      name);
			}
		}
	}
}

void Checker::checkResolvedBlockScopedVariable(Symbol* result,
                                               Node* errorLocation) {
	TSC_ASSERT(result->flags & SymbolFlagsBlockScopedVariable ||
	               result->flags & SymbolFlagsClass ||
	               result->flags & SymbolFlagsEnum,
	           "");
	if (result->flags &
	            (SymbolFlagsFunction | SymbolFlagsFunctionScopedVariable |
	             SymbolFlagsAssignment) &&
	    result->flags & SymbolFlagsClass) {
		// constructor functions aren't block scoped
		return;
	}
	// Block-scoped variables cannot be used before their definition
	Node* declaration = nullptr;
	for (Node* d : result->declarations) {
		if (isBlockOrCatchScoped(d) || isClassLike(d) ||
		    isEnumDeclaration(d)) {
			declaration = d;
			break;
		}
	}
	if (declaration == nullptr) {
		TSC_UNREACHABLE(
			"checkResolvedBlockScopedVariable could not find block-scoped declaration");
	}
	if (!(declaration->flags & NodeFlagsAmbient) &&
	    !isBlockScopedNameDeclaredBeforeUse(declaration, errorLocation)) {
		Diagnostic* diagnostic = nullptr;
		std::string declarationName =
			declarationNameToString(getNameOfDeclaration(declaration));
		if (result->flags & SymbolFlagsBlockScopedVariable) {
			diagnostic =
				error(errorLocation,
				      Block_scoped_variable_0_used_before_its_declaration,
				      declarationName);
		} else if (result->flags & SymbolFlagsClass) {
			diagnostic = error(errorLocation,
			                   Class_0_used_before_its_declaration,
			                   declarationName);
		} else if (result->flags & SymbolFlagsRegularEnum) {
			diagnostic = error(errorLocation,
			                   Enum_0_used_before_its_declaration,
			                   declarationName);
		} else {
			TSC_ASSERT(result->flags & SymbolFlagsConstEnum, "");
			if (compilerOptions->GetIsolatedModules()) {
				diagnostic = error(errorLocation,
				                   Enum_0_used_before_its_declaration,
				                   declarationName);
			}
		}
		if (diagnostic != nullptr) {
			diagnostic->AddRelatedInfo(createDiagnosticForNode(
				declaration, X_0_is_declared_here,
				std::vector<std::string>{declarationName}));
		}
	}
}

Symbol* Checker::getSymbol(SymbolTable& symbols, const std::string& name,
                           SymbolFlags meaning) {
	if (meaning & SymbolFlagsAll) {
		auto it = symbols.find(name);
		Symbol* symbol =
			it != symbols.end() ? getMergedSymbol(it->second) : nullptr;
		if (symbol != nullptr) {
			if (symbol->flags & meaning) {
				return symbol;
			}
			if (symbol->flags & SymbolFlagsAlias) {
				SymbolFlags targetFlags = getSymbolFlags(symbol);
				// `targetFlags` will be `SymbolFlags.All` if an error occurred
				// in alias resolution; this avoids cascading errors
				if (targetFlags & meaning) {
					return symbol;
				}
			}
		}
	}
	// return nil if we can't find a symbol
	return nullptr;
}

Node* Checker::getTypeOnlyAliasDeclaration(Symbol* symbol) {
	if (symbol->flags & SymbolFlagsAlias) {
		resolveAlias(symbol);
		return aliasSymbolLinks.Get(symbol)->typeOnlyDeclaration;
	}
	return nullptr;
}

// Return the first type-only alias declaration node (if any) in the resolution
// chain that affects the symbol for the given meaning
Node* Checker::getTypeOnlyAliasDeclarationEx(Symbol* symbol,
                                             SymbolFlags meaning) {
	while (symbol->flags & SymbolFlagsAlias &&
	       !(symbol->flags & meaning)) {
		Symbol* resolved = resolveAlias(symbol);
		AliasSymbolLinks* links = aliasSymbolLinks.Get(symbol);
		if (links->typeOnlyDeclaration != nullptr) {
			return links->typeOnlyDeclaration;
		}
		symbol = resolved;
	}
	return nullptr;
}

Symbol* Checker::getImmediateAliasedSymbol(Symbol* symbol) {
	TSC_ASSERT(symbol->flags & SymbolFlagsAlias, "Should only get Alias here.");
	AliasSymbolLinks* links = aliasSymbolLinks.Get(symbol);
	if (links->immediateTarget == nullptr) {
		Node* node = getDeclarationOfAliasSymbol(symbol);
		if (node == nullptr) {
			TSC_UNREACHABLE("Unexpected nil in getImmediateAliasedSymbol");
		}
		links->immediateTarget = getTargetOfAliasDeclaration(node);
	}
	return links->immediateTarget;
}

Diagnostic* Checker::addTypeOnlyDeclarationRelatedInfo(
	Diagnostic* diagnostic, Node* typeOnlyDeclaration,
	const std::string& name) {
	if (typeOnlyDeclaration == nullptr) {
		return diagnostic;
	}
	bool isExport = isExportSpecifier(typeOnlyDeclaration) ||
	                isExportDeclaration(typeOnlyDeclaration) ||
	                isNamespaceExport(typeOnlyDeclaration);
	return diagnostic->AddRelatedInfo(NewDiagnosticForNode(
		typeOnlyDeclaration,
		isExport ? X_0_was_exported_here : X_0_was_imported_here, {name}));
}

SymbolFlags Checker::getSymbolFlags(Symbol* symbol) {
	return getSymbolFlagsEx(symbol, false /*excludeTypeOnlyMeanings*/,
	                        false /*excludeLocalMeanings*/);
}

SymbolFlags Checker::getSymbolFlagsEx(Symbol* symbol,
                                      bool excludeTypeOnlyMeanings,
                                      bool excludeLocalMeanings) {
	std::unordered_set<Symbol*> seenSymbols;
	SymbolFlags flags = SymbolFlagsNone;
	if (!excludeLocalMeanings) {
		flags = symbol->flags;
	}
	while (symbol->flags & SymbolFlagsAlias) {
		if (excludeTypeOnlyMeanings &&
		    getTypeOnlyAliasDeclaration(symbol) != nullptr) {
			break;
		}
		Symbol* target =
			getExportSymbolOfValueSymbolIfExported(resolveAlias(symbol));
		if (target == unknownSymbol) {
			return SymbolFlagsAll;
		}
		if (target->flags & SymbolFlagsAlias) {
			// Optimization - try to avoid creating or adding to `seenSymbols`
			// if possible
			if (target == symbol || seenSymbols.count(target)) {
				break;
			}
			if (seenSymbols.empty()) {
				seenSymbols.insert(symbol);
			}
			seenSymbols.insert(target);
		}
		flags = static_cast<SymbolFlags>(flags | target->flags);
		symbol = target;
	}
	return flags;
}

Node* Checker::getThisContainer(Node* node,
                                bool includeArrowFunctions,
                                bool includeClassComputedPropertyName) {
	for (;;) {
		node = node->parent;
		if (node == nullptr) {
			// If we never pass in a SourceFile, this should be unreachable,
			// since we'll stop when we reach that.
			TSC_UNREACHABLE("No parent in getThisContainer");
		}
		switch (node->kind) {
		case Kind::ComputedPropertyName:
			// If the grandparent node is an object literal (as opposed to a
			// class), then the computed property is not a 'this' container.
			// A computed property name in a class needs to be a this container
			// so that we can error on it.
			if (includeClassComputedPropertyName &&
			    isClassLike(node->parent->parent)) {
				return node;
			}
			// If this is a computed property, then the parent should not
			// make it a this container. The parent might be a property
			// in an object literal, like a method or accessor. But in order
			// for such a parent to be a this container, the reference must
			// be in the *body* of the container.
			node = node->parent->parent;
			break;
		case Kind::Decorator:
			// Decorators are always applied outside of the body of a class or
			// method.
			if (node->parent->kind == Kind::Parameter &&
			    isClassElement(node->parent->parent)) {
				// If the decorator's parent is a Parameter, we resolve the this
				// container from the grandparent class declaration.
				node = node->parent->parent;
			} else if (isClassElement(node->parent)) {
				// If the decorator's parent is a class element, we resolve the
				// 'this' container from the parent class declaration.
				node = node->parent;
			}
			break;
		case Kind::ArrowFunction:
			if (!includeArrowFunctions) {
				continue;
			}
			[[fallthrough]];
		case Kind::FunctionDeclaration:
		case Kind::FunctionExpression:
		case Kind::ModuleDeclaration:
		case Kind::ClassStaticBlockDeclaration:
		case Kind::PropertyDeclaration:
		case Kind::PropertySignature:
		case Kind::MethodDeclaration:
		case Kind::MethodSignature:
		case Kind::Constructor:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
		case Kind::CallSignature:
		case Kind::ConstructSignature:
		case Kind::IndexSignature:
		case Kind::EnumDeclaration:
		case Kind::SourceFile:
			return node;
		default:
			break;
		}
	}
}

void Checker::sortSymbols(std::vector<Symbol*>& symbols) {
	std::sort(symbols.begin(), symbols.end(),
	          [this](Symbol* a, Symbol* b) {
		          return compareSymbols(a, b) < 0;
	          });
}

int Checker::compareSymbolsWorker(Symbol* s1, Symbol* s2) {
	if (s1 == s2) {
		return 0;
	}
	if (s1 == nullptr) {
		return 1;
	}
	if (s2 == nullptr) {
		return -1;
	}
	if (!s1->declarations.empty() && !s2->declarations.empty()) {
		if (int r = compareNodes(s1->declarations[0], s2->declarations[0]);
		    r != 0) {
			return r;
		}
	} else if (!s1->declarations.empty()) {
		return -1;
	} else if (!s2->declarations.empty()) {
		return 1;
	}
	if (int r = s1->name.compare(s2->name); r != 0) {
		return r < 0 ? -1 : 1;
	}
	// Fall back to symbol IDs. This is a last resort that should happen only
	// when symbols have no declaration and duplicate names.
	return static_cast<int>(getSymbolId(s1)) -
	       static_cast<int>(getSymbolId(s2));
}

int Checker::compareNodes(Node* n1, Node* n2) {
	if (n1 == n2) {
		return 0;
	}
	if (n1 == nullptr) {
		return 1;
	}
	if (n2 == nullptr) {
		return -1;
	}
	SourceFile* s1 = getSourceFileOfNode(n1);
	SourceFile* s2 = getSourceFileOfNode(n2);
	if (s1 != s2) {
		int f1 = fileIndexMap[s1];
		int f2 = fileIndexMap[s2];
		// Order by index of file in the containing program
		return f1 - f2;
	}
	// In the same file, order by source position
	return n1->pos() - n2->pos();
}

int Checker::compareSymbolChainsWorker(const std::vector<Symbol*>& a,
                                       const std::vector<Symbol*>& b) {
	int chainLen = static_cast<int>(a.size()) - static_cast<int>(b.size());
	if (chainLen != 0) {
		return chainLen;
	}
	size_t idx = 0;
	while (idx < a.size()) {
		int comparison = compareSymbols(a[idx], b[idx]);
		if (comparison != 0) {
			return comparison;
		}
		idx++;
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Checker::init — NewChecker (checker.go:911) minus program.BindSourceFiles
// ---------------------------------------------------------------------------

void Checker::init(Program* p) {
	static std::atomic<uint32_t> nextCheckerId{0};
	id = nextCheckerId.fetch_add(1) + 1;
	program = p;
	compilerOptions = p->Options();
	files = p->SourceFiles();
	fileIndexMap = createFileIndexMap(files);
	compareSymbols = [this](Symbol* a, Symbol* b) {
		return compareSymbolsWorker(a, b);
	};
	compareSymbolChains =
		[this](const std::vector<Symbol*>& a,
		       const std::vector<Symbol*>& b) {
			return compareSymbolChainsWorker(a, b);
		};
	languageVersion = compilerOptions->GetEmitScriptTarget();
	moduleKind = compilerOptions->GetEmitModuleKind();
	moduleResolutionKind = compilerOptions->GetModuleResolutionKind();
	legacyDecorators = compilerOptions->ExperimentalDecorators == Tristate::True;
	emitStandardClassFields = compilerOptions->GetEmitStandardClassFields();
	strictNullChecks =
		compilerOptions->GetStrictOptionValue(compilerOptions->StrictNullChecks);
	strictFunctionTypes = compilerOptions->GetStrictOptionValue(
		compilerOptions->StrictFunctionTypes);
	strictBindCallApply = compilerOptions->GetStrictOptionValue(
		compilerOptions->StrictBindCallApply);
	strictPropertyInitialization = compilerOptions->GetStrictOptionValue(
		compilerOptions->StrictPropertyInitialization);
	strictBuiltinIteratorReturn = compilerOptions->GetStrictOptionValue(
		compilerOptions->StrictBuiltinIteratorReturn);
	noImplicitAny =
		compilerOptions->GetStrictOptionValue(compilerOptions->NoImplicitAny);
	noImplicitThis =
		compilerOptions->GetStrictOptionValue(compilerOptions->NoImplicitThis);
	useUnknownInCatchVariables = compilerOptions->GetStrictOptionValue(
		compilerOptions->UseUnknownInCatchVariables);
	exactOptionalPropertyTypes =
		compilerOptions->ExactOptionalPropertyTypes == Tristate::True;
	canCollectSymbolAliasAccessibilityData = tristateIsFalseOrUnknown(
		compilerOptions->VerbatimModuleSyntax);
	arrayVariances = {VarianceFlagsCovariant};
	globals.reserve(countGlobalSymbols(files));
	evaluate = newEvaluator(
		[this](Node* expr, Node* location) {
			return evaluateEntity(expr, location);
		},
		OEKParentheses);
	undefinedSymbol = newSymbol(SymbolFlagsProperty, "undefined");
	argumentsSymbol = newSymbol(SymbolFlagsProperty, "arguments");
	requireSymbol = newSymbol(SymbolFlagsProperty, "require");
	unknownSymbol = newSymbol(SymbolFlagsProperty, "unknown");
	globalThisSymbol =
		newSymbolEx(SymbolFlagsModule, "globalThis", CheckFlagsReadonly);
	globalThisSymbol->exports = globals;
	globals[globalThisSymbol->name] = globalThisSymbol;
	binder::NameResolver* nr = newNameResolverImpl(this, false);
	resolveName = [nr](Node* location, std::string_view name,
	                   SymbolFlags meaning,
	                   const DiagnosticMessage* nameNotFoundMessage, bool isUse,
	                   bool excludeGlobals) {
		return nr->resolve(location, std::string(name), meaning,
		                   nameNotFoundMessage, isUse, excludeGlobals);
	};
	binder::NameResolver* nrs = newNameResolverImpl(this, true);
	resolveNameForSymbolSuggestion =
		[nrs](Node* location, std::string_view name, SymbolFlags meaning,
		      const DiagnosticMessage* nameNotFoundMessage, bool isUse,
		      bool excludeGlobals) {
			return nrs->resolve(location, std::string(name), meaning,
			                    nameNotFoundMessage, isUse, excludeGlobals);
		};
	anyType = newIntrinsicType(TypeFlagsAny, "any");
	autoType = newIntrinsicTypeEx(TypeFlagsAny, "any",
	                              ObjectFlagsNonInferrableType);
	wildcardType = newIntrinsicType(TypeFlagsAny, "any");
	blockedStringType = newIntrinsicType(TypeFlagsAny, "any");
	errorType = newIntrinsicType(TypeFlagsAny, "error");
	unresolvedType = newIntrinsicType(TypeFlagsAny, "unresolved");
	nonInferrableAnyType = newIntrinsicTypeEx(
		TypeFlagsAny, "any", ObjectFlagsContainsWideningType);
	intrinsicMarkerType = newIntrinsicType(TypeFlagsAny, "intrinsic");
	unknownType = newIntrinsicType(TypeFlagsUnknown, "unknown");
	undefinedType = newIntrinsicType(TypeFlagsUndefined, "undefined");
	undefinedWideningType = createWideningType(undefinedType);
	missingType = newIntrinsicType(TypeFlagsUndefined, "undefined");
	undefinedOrMissingType =
		exactOptionalPropertyTypes ? missingType : undefinedType;
	optionalType = newIntrinsicType(TypeFlagsUndefined, "undefined");
	nullType = newIntrinsicType(TypeFlagsNull, "null");
	nullWideningType = createWideningType(nullType);
	stringType = newIntrinsicType(TypeFlagsString, "string");
	numberType = newIntrinsicType(TypeFlagsNumber, "number");
	bigintType = newIntrinsicType(TypeFlagsBigInt, "bigint");
	regularFalseType = newLiteralType(TypeFlagsBooleanLiteral, false, nullptr);
	falseType =
		newLiteralType(TypeFlagsBooleanLiteral, false, regularFalseType);
	regularFalseType->AsLiteralType()->freshType = falseType;
	falseType->AsLiteralType()->freshType = falseType;
	regularTrueType = newLiteralType(TypeFlagsBooleanLiteral, true, nullptr);
	trueType = newLiteralType(TypeFlagsBooleanLiteral, true, regularTrueType);
	regularTrueType->AsLiteralType()->freshType = trueType;
	trueType->AsLiteralType()->freshType = trueType;
	booleanType = getUnionType({regularFalseType, regularTrueType});
	esSymbolType = newIntrinsicType(TypeFlagsESSymbol, "symbol");
	voidType = newIntrinsicType(TypeFlagsVoid, "void");
	neverType = newIntrinsicType(TypeFlagsNever, "never");
	silentNeverType = newIntrinsicTypeEx(TypeFlagsNever, "never",
	                                     ObjectFlagsNonInferrableType);
	implicitNeverType = newIntrinsicType(TypeFlagsNever, "never");
	unreachableNeverType = newIntrinsicType(TypeFlagsNever, "never");
	nonPrimitiveType = newIntrinsicType(TypeFlagsNonPrimitive, "object");
	stringOrNumberType = getUnionType({stringType, numberType});
	stringNumberSymbolType =
		getUnionType({stringType, numberType, esSymbolType});
	numberOrBigIntType = getUnionType({numberType, bigintType});
	numericStringType = getTemplateLiteralType({"", ""},
	                                           {numberType});  // The `${number}` type
	templateConstraintType =
		getUnionType({stringType, numberType, booleanType, bigintType,
	                  nullType, undefinedType});
	uniqueLiteralType = newIntrinsicType(
		TypeFlagsNever,
		"never");  // Special `never` flagged by union reduction to behave as a
		           // literal
	uniqueLiteralMapper = newFunctionTypeMapper(
		[this](Type* t) { return getUniqueLiteralTypeForTypeParameter(t); });
	reportUnreliableMapper =
		newFunctionTypeMapper([this](Type* t) { return reportUnreliableWorker(t); });
	reportUnmeasurableMapper = newFunctionTypeMapper(
		[this](Type* t) { return reportUnmeasurableWorker(t); });
	restrictiveMapper = newFunctionTypeMapper(
		[this](Type* t) { return restrictiveMapperWorker(t); });
	permissiveMapper = newFunctionTypeMapper(
		[this](Type* t) { return permissiveMapperWorker(t); });
	emptyObjectType = newAnonymousType(nullptr, {}, {}, {}, {});
	emptyJsxObjectType = newAnonymousType(nullptr, {}, {}, {}, {});
	emptyFreshJsxObjectType = newAnonymousType(nullptr, {}, {}, {}, {});
	emptyTypeLiteralType =
		newAnonymousType(newSymbol(SymbolFlagsTypeLiteral,
		                           InternalSymbolNameType),
		                 {}, {}, {}, {});
	unknownEmptyObjectType = newAnonymousType(nullptr, {}, {}, {}, {});
	unknownUnionType = createUnknownUnionType();
	emptyGenericType = newAnonymousType(nullptr, {}, {}, {}, {});
	emptyGenericType->AsObjectType()->instantiations = CacheMap<Type*>{};
	anyFunctionType = newAnonymousType(nullptr, {}, {}, {}, {});
	anyFunctionType->objectFlags |= ObjectFlagsNonInferrableType;
	noConstraintType = newAnonymousType(nullptr, {}, {}, {}, {});
	circularConstraintType = newAnonymousType(nullptr, {}, {}, {}, {});
	resolvingDefaultType = newAnonymousType(nullptr, {}, {}, {}, {});
	markerSuperType = newTypeParameter(nullptr);
	markerSubType = newTypeParameter(nullptr);
	markerSubType->AsTypeParameter()->constraint = markerSuperType;
	markerOtherType = newTypeParameter(nullptr);
	markerSuperTypeForCheck = newTypeParameter(nullptr);
	markerSubTypeForCheck = newTypeParameter(nullptr);
	markerSubTypeForCheck->AsTypeParameter()->constraint =
		markerSuperTypeForCheck;
	noTypePredicate = linksArena.alloc<TypePredicate>();
	noTypePredicate->kind = TypePredicateKind::Identifier;
	noTypePredicate->parameterIndex = 0;
	noTypePredicate->parameterName = "<<unresolved>>";
	noTypePredicate->t = anyType;
	anySignature = newSignature(SignatureFlagsNone, nullptr, {}, nullptr, {},
	                            anyType, nullptr, 0);
	unknownSignature = newSignature(SignatureFlagsNone, nullptr, {}, nullptr,
	                                {}, errorType, nullptr, 0);
	resolvingSignature = newSignature(SignatureFlagsNone, nullptr, {},
	                                  nullptr, {}, anyType, nullptr, 0);
	silentNeverSignature = newSignature(SignatureFlagsNone, nullptr, {},
	                                    nullptr, {}, silentNeverType, nullptr, 0);
	enumNumberIndexInfo = linksArena.alloc<IndexInfo>();
	enumNumberIndexInfo->keyType = numberType;
	enumNumberIndexInfo->valueType = stringType;
	enumNumberIndexInfo->isReadonly = true;
	anyBaseTypeIndexInfo = linksArena.alloc<IndexInfo>();
	anyBaseTypeIndexInfo->keyType = stringType;
	anyBaseTypeIndexInfo->valueType = anyType;
	anyBaseTypeIndexInfo->isReadonly = false;
	emptyStringType = getStringLiteralType("");
	zeroType = getNumberLiteralType(Number(0));
	zeroBigIntType = getBigIntLiteralType(PseudoBigInt{});
	{
		// typeofType = union of sorted typeofNEFacts keys as string literals
		static const char* const typeofNames[] = {
			"bigint", "boolean", "function", "number",
			"object", "string",  "symbol",   "undefined"};
		std::vector<Type*> typeofTypes;
		for (const char* n : typeofNames) {
			typeofTypes.push_back(getStringLiteralType(n));
		}
		typeofType = getUnionType(typeofTypes);
	}
	subtypeRelation = new Relation();
	strictSubtypeRelation = new Relation();
	assignableRelation = new Relation();
	comparableRelation = new Relation();
	identityRelation = new Relation();
	getGlobalESSymbolType = getGlobalTypeResolver("Symbol", 0, false);
	getGlobalBigIntType = getGlobalTypeResolver("BigInt", 0, false);
	getGlobalImportMetaType = getGlobalTypeResolver("ImportMeta", 0, true);
	getGlobalImportAttributesType =
		getGlobalTypeResolver("ImportAttributes", 0, false);
	getGlobalImportAttributesTypeChecked =
		getGlobalTypeResolver("ImportAttributes", 0, true);
	getGlobalNonNullableTypeAliasOrNil =
		getGlobalTypeAliasResolver("NonNullable", 1, false);
	getGlobalExtractSymbol = getGlobalTypeAliasResolver("Extract", 2, true);
	getGlobalDisposableType = getGlobalTypeResolver("Disposable", 0, true);
	getGlobalAsyncDisposableType =
		getGlobalTypeResolver("AsyncDisposable", 0, true);
	getGlobalAwaitedSymbol = getGlobalTypeAliasResolver("Awaited", 1, true);
	getGlobalAwaitedSymbolOrNil =
		getGlobalTypeAliasResolver("Awaited", 1, false);
	getGlobalNaNSymbolOrNil = getGlobalValueSymbolResolver("NaN", false);
	getGlobalRecordSymbol = getGlobalTypeAliasResolver("Record", 2, true);
	getGlobalTemplateStringsArrayType =
		getGlobalTypeResolver("TemplateStringsArray", 0, true);
	getGlobalESSymbolConstructorSymbolOrNil =
		getGlobalValueSymbolResolver("Symbol", false);
	getGlobalESSymbolConstructorTypeSymbolOrNil =
		getGlobalTypeSymbolResolver("SymbolConstructor", false);
	getGlobalImportCallOptionsType =
		getGlobalTypeResolver("ImportCallOptions", 0, false);
	getGlobalImportCallOptionsTypeChecked =
		getGlobalTypeResolver("ImportCallOptions", 0, true);
	getGlobalPromiseType = getGlobalTypeResolver("Promise", 1, false);
	getGlobalPromiseTypeChecked = getGlobalTypeResolver("Promise", 1, true);
	getGlobalPromiseLikeType = getGlobalTypeResolver("PromiseLike", 1, true);
	getGlobalPromiseConstructorSymbol =
		getGlobalValueSymbolResolver("Promise", true);
	getGlobalPromiseConstructorSymbolOrNil =
		getGlobalValueSymbolResolver("Promise", false);
	getGlobalOmitSymbol = getGlobalTypeAliasResolver("Omit", 2, true);
	getGlobalNoInferSymbolOrNil =
		getGlobalTypeAliasResolver("NoInfer", 1, false);
	getGlobalIteratorType = getGlobalTypeResolver("Iterator", 3, false);
	getGlobalIterableType = getGlobalTypeResolver("Iterable", 3, false);
	getGlobalIterableTypeChecked = getGlobalTypeResolver("Iterable", 3, true);
	getGlobalIterableIteratorType =
		getGlobalTypeResolver("IterableIterator", 3, false);
	getGlobalIterableIteratorTypeChecked =
		getGlobalTypeResolver("IterableIterator", 3, true);
	getGlobalIteratorObjectType =
		getGlobalTypeResolver("IteratorObject", 3, false);
	getGlobalGeneratorType = getGlobalTypeResolver("Generator", 3, false);
	getGlobalAsyncIteratorType =
		getGlobalTypeResolver("AsyncIterator", 3, false);
	getGlobalAsyncIterableType =
		getGlobalTypeResolver("AsyncIterable", 3, false);
	getGlobalAsyncIterableTypeChecked =
		getGlobalTypeResolver("AsyncIterable", 3, true);
	getGlobalAsyncIterableIteratorType =
		getGlobalTypeResolver("AsyncIterableIterator", 3, false);
	getGlobalAsyncIterableIteratorTypeChecked =
		getGlobalTypeResolver("AsyncIterableIterator", 3, true);
	getGlobalAsyncIteratorObjectType =
		getGlobalTypeResolver("AsyncIteratorObject", 3, false);
	getGlobalAsyncGeneratorType =
		getGlobalTypeResolver("AsyncGenerator", 3, false);
	getGlobalIteratorYieldResultType =
		getGlobalTypeResolver("IteratorYieldResult", 1, false);
	getGlobalIteratorReturnResultType =
		getGlobalTypeResolver("IteratorReturnResult", 1, false);
	getGlobalTypedPropertyDescriptorType =
		getGlobalTypeResolver("TypedPropertyDescriptor", 1, true);
	getGlobalClassDecoratorContextType =
		getGlobalTypeResolver("ClassDecoratorContext", 1, true);
	getGlobalClassMethodDecoratorContextType =
		getGlobalTypeResolver("ClassMethodDecoratorContext", 2, true);
	getGlobalClassGetterDecoratorContextType =
		getGlobalTypeResolver("ClassGetterDecoratorContext", 2, true);
	getGlobalClassSetterDecoratorContextType =
		getGlobalTypeResolver("ClassSetterDecoratorContext", 2, true);
	getGlobalClassAccessorDecoratorContextType =
		getGlobalTypeResolver("ClassAccessorDecoratorContext", 2, true);
	getGlobalClassAccessorDecoratorTargetType =
		getGlobalTypeResolver("ClassAccessorDecoratorTarget", 2, true);
	getGlobalClassAccessorDecoratorResultType =
		getGlobalTypeResolver("ClassAccessorDecoratorResult", 2, true);
	getGlobalClassFieldDecoratorContextType =
		getGlobalTypeResolver("ClassFieldDecoratorContext", 1, true);
	initializeClosures();
	initializeIterationResolvers();
	initializeChecker();
}

Type* Checker::getGlobalStrictFunctionType(const std::string& name) {
	if (strictBindCallApply) {
		return getGlobalType(name, 0 /*arity*/, true /*reportErrors*/);
	}
	return globalFunctionType;
}

Type* Checker::createTypeFromGenericGlobalType(
	Type* genericGlobalType, const std::vector<Type*>& typeArguments) {
	if (genericGlobalType != emptyGenericType) {
		return createTypeReference(genericGlobalType, typeArguments);
	}
	return emptyObjectType;
}

Type* Checker::createArrayType(Type* elementType) {
	return createArrayTypeEx(elementType, false /*readonly*/);
}

Type* Checker::createArrayTypeEx(Type* elementType, bool readonly) {
	return createTypeFromGenericGlobalType(
		readonly ? globalReadonlyArrayType : globalArrayType, {elementType});
}

Type* Checker::getTypeOfModuleImportAttributes(Symbol* symbol) {
	auto it = moduleImportAttributesTypes.find(symbol);
	if (it != moduleImportAttributesTypes.end()) {
		return it->second;
	}
	Type* result;
	Node* moduleDecl = nullptr;
	for (Node* d : symbol->declarations) {
		if (isModuleWithStringLiteralName(d)) {
			moduleDecl = d;
			break;
		}
	}
	if (moduleDecl == nullptr) {
		result = emptyObjectType;
	} else {
		result = getTypeOfModuleDeclarationImportAttributes(
			moduleDecl->as<ModuleDeclaration>()->Attributes);
	}
	moduleImportAttributesTypes[symbol] = result;
	return result;
}

Type* Checker::getTypeOfModuleDeclarationImportAttributes(Node* attributes) {
	if (attributes == nullptr) {
		return emptyObjectType;
	}
	return getTypeFromTypeNode(attributes);
}

// ---------------------------------------------------------------------------
// Interim stubs — filled by their owning slices
// ---------------------------------------------------------------------------

Symbol* Checker::resolveExternalModuleNameWorker(
	Node* location, Node* moduleReferenceExpression,
	const DiagnosticMessage* moduleNotFoundError, bool ignoreErrors,
	bool isForAugmentation, Type* importAttributesType) {
	if (isStringLiteralLike(moduleReferenceExpression)) {
		return resolveExternalModule(
		    location, std::string(moduleReferenceExpression->text()),
		    moduleNotFoundError,
		    !ignoreErrors ? moduleReferenceExpression : nullptr,
		    isForAugmentation, importAttributesType);
	}
	return nullptr;
}

// getPropertyOfType is defined in checker_members.cpp (members slice).

// allTypesAssignableToKindEx — ported in checker_typeops.cpp (typeops slice,
// checker.go:28098).

// checker.go:22087 — getApparentType (members slice owner)
Type* Checker::getApparentType(Type* t) {
	Type* originalType = t;
	if ((t->flags & TypeFlagsInstantiable) != 0) {
		t = getBaseConstraintOfType(t);
		if (t == nullptr) {
			t = unknownType;
		}
	}
	if ((t->objectFlags & ObjectFlagsMapped) != 0) {
		return getApparentTypeOfMappedType(t);
	}
	if ((t->objectFlags & ObjectFlagsReference) != 0 && t != originalType) {
		return getTypeWithThisArgument(t, originalType, /*needsApparentType*/ false);
	}
	if ((t->flags & TypeFlagsIntersection) != 0) {
		return getApparentTypeOfIntersectionType(t, originalType);
	}
	if ((t->flags & TypeFlagsStringLike) != 0) {
		return globalStringType;
	}
	if ((t->flags & TypeFlagsNumberLike) != 0) {
		return globalNumberType;
	}
	if ((t->flags & TypeFlagsBigIntLike) != 0) {
		return getGlobalBigIntType();
	}
	if ((t->flags & TypeFlagsBooleanLike) != 0) {
		return globalBooleanType;
	}
	if ((t->flags & TypeFlagsESSymbolLike) != 0) {
		return getGlobalESSymbolType();
	}
	if ((t->flags & TypeFlagsNonPrimitive) != 0) {
		return emptyObjectType;
	}
	if ((t->flags & TypeFlagsIndex) != 0) {
		return stringNumberSymbolType;
	}
	if ((t->flags & TypeFlagsUnknown) != 0 && !strictNullChecks) {
		return emptyObjectType;
	}
	return t;
}

// checker.go:24977 — getRestrictiveTypeParameter (decltypes slice owner)
Type* Checker::getRestrictiveTypeParameter(Type* t) {
	if ((t->AsTypeParameter()->constraint == nullptr &&
	     getConstraintDeclaration(t) == nullptr) ||
	    t->AsTypeParameter()->constraint == noConstraintType) {
		return t;
	}
	CachedTypeKey key{CachedTypeKind::RestrictiveTypeParameter, t->id};
	auto it = cachedTypes.find(key);
	if (it != cachedTypes.end()) {
		return it->second;
	}
	Type* result = newTypeParameter(t->symbol);
	result->AsTypeParameter()->constraint = noConstraintType;
	cachedTypes[key] = result;
	return result;
}

// getApparentType is defined in checker_members.cpp (members slice).
// couldContainTypeVariablesWorker is defined in checker_instantiate.cpp (instantiate slice).

// isStringIndexSignatureOnlyTypeWorker — ported in checker_typeops.cpp
// (typeops slice, checker.go:27831).


// ---------------------------------------------------------------------------
// External module name resolution — checker.go:15343-15990
// ---------------------------------------------------------------------------

Symbol* Checker::resolveExternalModuleName(Node* location,
                                           Node* moduleReferenceExpression,
                                           bool ignoreErrors,
                                           Type* importAttributesType) {
	const DiagnosticMessage* errorMessage =
	    getCannotResolveModuleNameErrorForSpecificModule(
	        moduleReferenceExpression);
	if (errorMessage == nullptr) {
		errorMessage = Cannot_find_module_0_or_its_corresponding_type_declarations;
	}
	ignoreErrors = ignoreErrors || compilerOptions->NoCheck == Tristate::True;
	return resolveExternalModuleNameWorker(
	    location, moduleReferenceExpression,
	    ignoreErrors ? nullptr : errorMessage, ignoreErrors,
	    false /*isForAugmentation*/, importAttributesType);
}

const DiagnosticMessage* Checker::
    getCannotResolveModuleNameErrorForSpecificModule(Node* moduleName) {
	if (isStringLiteral(moduleName)) {
		if (nodeCoreModulesContains(moduleName->text())) {
			if (compilerOptions->UsesWildcardTypes()) {
				return Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_node_Try_npm_i_save_dev_types_Slashnode;
			}
			return Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_node_Try_npm_i_save_dev_types_Slashnode_and_then_add_node_to_the_types_field_in_your_tsconfig;
		}
	}
	return nullptr;
}

SourceFile* Checker::getExternalModuleFileFromDeclaration(Node* declaration) {
	Node* specifier = nullptr;
	if (declaration->kind == Kind::ModuleDeclaration) {
		if (isStringLiteral(declaration->name())) {
			specifier = declaration->name();
		}
	} else {
		specifier = getExternalModuleName(declaration);
	}
	Type* importAttributesType = nullptr;
	if (hasImportAttributes(declaration)) {
		importAttributesType =
		    getTypeFromImportAttributes(getImportAttributes(declaration));
	}
	Symbol* moduleSymbol = resolveExternalModuleNameWorker(
	    specifier, specifier /*moduleNotFoundError*/, nullptr, false, false,
	    importAttributesType);  // TODO: GH#18217
	if (moduleSymbol == nullptr) {
		return nullptr;
	}
	Node* decl = getDeclarationOfKind(moduleSymbol, Kind::SourceFile);
	if (decl == nullptr) {
		return nullptr;
	}
	return static_cast<SourceFile*>(decl);
}

// module/util.go: GetResolutionDiagnostic
static const DiagnosticMessage* getResolutionDiagnostic(
    const CompilerOptions& options, const ResolvedModule& resolvedModule,
    SourceFile* file) {
	auto needJsx = [&]() -> const DiagnosticMessage* {
		if (options.Jsx != JsxEmit::None) {
			return nullptr;
		}
		return Module_0_was_resolved_to_1_but_jsx_is_not_set;
	};
	auto needAllowJs = [&]() -> const DiagnosticMessage* {
		if (options.GetAllowJS() ||
		    !options.DefaultIfUnknown(options.NoImplicitAny,
		                              options.Strict)) {
			return nullptr;
		}
		return Could_not_find_a_declaration_file_for_module_0_1_implicitly_has_an_any_type;
	};
	auto needResolveJsonModule = [&]() -> const DiagnosticMessage* {
		if (options.GetResolveJsonModule()) {
			return nullptr;
		}
		return Module_0_was_resolved_to_1_but_resolveJsonModule_is_not_used;
	};
	auto needAllowArbitraryExtensions = [&]() -> const DiagnosticMessage* {
		if (file->IsDeclarationFile ||
		    options.AllowArbitraryExtensions == Tristate::True) {
			return nullptr;
		}
		return Module_0_was_resolved_to_1_but_allowArbitraryExtensions_is_not_set;
	};

	if (resolvedModule.resolvedUsingExtraExtensions) {
		return nullptr;
	}

	std::string_view ext = resolvedModule.extension;
	if (ext == tspath::extensionTs || ext == tspath::extensionDts ||
	    ext == tspath::extensionMts || ext == tspath::extensionDmts ||
	    ext == tspath::extensionCts || ext == tspath::extensionDcts) {
		// These are always allowed.
		return nullptr;
	}
	if (ext == tspath::extensionTsx) {
		return needJsx();
	}
	if (ext == tspath::extensionJsx) {
		if (const DiagnosticMessage* message = needJsx()) {
			return message;
		}
		return needAllowJs();
	}
	if (ext == tspath::extensionJs || ext == tspath::extensionMjs ||
	    ext == tspath::extensionCjs) {
		return needAllowJs();
	}
	if (ext == tspath::extensionJson) {
		return needResolveJsonModule();
	}
	return needAllowArbitraryExtensions();
}

// checker.go: resolutionExtensionIsTSOrJson
static bool resolutionExtensionIsTSOrJson(std::string_view ext) {
	return tspath::extensionIsTs(ext) || ext == tspath::extensionJson;
}

// utilities.go: isSideEffectImport
static bool isSideEffectImport(Node* node) {
	Node* ancestor = findAncestor(node, isImportDeclaration);
	return ancestor != nullptr && ancestor->importClause() == nullptr;
}

// utilities.go: getAliasDeclarationFromName
static Node* getAliasDeclarationFromName(Node* node) {
	switch (node->parent->kind) {
	case Kind::ImportClause:
	case Kind::ImportSpecifier:
	case Kind::NamespaceImport:
	case Kind::ExportSpecifier:
	case Kind::ExportAssignment:
	case Kind::ImportEqualsDeclaration:
	case Kind::NamespaceExport:
		return node->parent;
	case Kind::QualifiedName:
		return getAliasDeclarationFromName(node->parent);
	default:
		return nullptr;
	}
}

// utilities.go: entityNameToString
std::string entityNameToString(Node* name) {
	Node* current = name;
	std::vector<std::string_view> parts;
	for (; current; current = current->kind == Kind::QualifiedName
	                              ? current->as<QualifiedName>()->Left
	                              : nullptr) {
		if (current->kind == Kind::QualifiedName) {
			parts.push_back(current->as<QualifiedName>()->Right->text());
		} else {
			parts.push_back(current->text());
		}
	}
	std::string result;
	for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
		if (!result.empty()) result += '.';
		result += *it;
	}
	return result;
}

// utilities.go: getContainingQualifiedNameNode
static Node* getContainingQualifiedNameNode(Node* node) {
	while (isQualifiedName(node->parent)) {
		node = node->parent;
	}
	return node;
}

Symbol* Checker::resolveExternalModule(
    Node* location, const std::string& moduleReference,
    const DiagnosticMessage* moduleNotFoundError, Node* errorNode,
    bool isForAugmentation, Type* importAttributesType) {
	if (errorNode != nullptr &&
	    moduleReference.compare(0, 7, "@types/") == 0) {
		std::string withoutAtTypePrefix = moduleReference.substr(7);
		error(errorNode,
		      Cannot_import_type_declaration_files_Consider_importing_0_instead_of_1,
		      {withoutAtTypePrefix, moduleReference});
	}
	if (importAttributesType == nullptr) {
		importAttributesType = emptyObjectType;
	}

	Symbol* ambientModule =
	    tryFindAmbientModule(moduleReference, true /*withAugmentations*/);
	if (ambientModule != nullptr) {
		return tryResolvePatternAmbientModule(ambientModule, moduleReference,
		                                      importAttributesType);
	}

	SourceFile* importingSourceFile = getSourceFileOfNode(location);
	Node* contextSpecifier = nullptr;
	ResolutionMode mode = ResolutionModeNone;

	if (isStringLiteralLike(location) ||
	    (location->parent != nullptr &&
	     isModuleDeclaration(location->parent) &&
	     location->parent->as<ModuleDeclaration>()->name == location)) {
		contextSpecifier = location;
	} else if (isModuleDeclaration(location)) {
		contextSpecifier = location->as<ModuleDeclaration>()->name;
	} else if (isLiteralImportTypeNode(location)) {
		contextSpecifier = location->as<ImportTypeNode>()
		                       ->Argument->as<LiteralTypeNode>()
		                       ->Literal;
	} else if (isVariableDeclarationInitializedToBareOrAccessedRequire(
	               location)) {
		contextSpecifier =
		    getModuleSpecifierOfBareOrAccessedRequire(location);
	} else {
		Node* ancestor = findAncestor(location, isImportCall);
		if (ancestor != nullptr) {
			contextSpecifier = ancestor->arguments()[0];
		}
		if (ancestor == nullptr) {
			ancestor = findAncestor(
			    location, isImportDeclarationOrJSImportDeclaration);
			if (ancestor != nullptr) {
				contextSpecifier = ancestor->moduleSpecifier();
			}
		}
		if (ancestor == nullptr) {
			ancestor = findAncestor(location, isExportDeclaration);
			if (ancestor != nullptr) {
				contextSpecifier = ancestor->moduleSpecifier();
			}
		}
		if (ancestor == nullptr) {
			ancestor = findAncestor(location, isImportEqualsDeclaration);
			if (ancestor != nullptr) {
				Node* moduleRef =
				    ancestor->as<ImportEqualsDeclaration>()->ModuleReference;
				if (moduleRef->kind == Kind::ExternalModuleReference) {
					contextSpecifier = moduleRef->expression();
				}
			}
		}
	}

	if (contextSpecifier != nullptr &&
	    isStringLiteralLike(contextSpecifier)) {
		mode = program->GetModeForUsageLocation(importingSourceFile,
		                                        contextSpecifier);
	} else {
		mode = program->GetDefaultResolutionModeForFile(importingSourceFile);
	}

	auto resolvedModuleOpt = program->GetResolvedModule(
	    importingSourceFile, moduleReference, mode);
	ResolvedModule resolvedModule =
	    resolvedModuleOpt ? *resolvedModuleOpt : ResolvedModule{};

	const DiagnosticMessage* resolutionDiagnostic = nullptr;
	if (errorNode != nullptr && resolvedModule.resolved) {
		resolutionDiagnostic = getResolutionDiagnostic(
		    *compilerOptions, resolvedModule, importingSourceFile);
	}

	SourceFile* sourceFile = nullptr;
	if (resolvedModule.resolved &&
	    (resolutionDiagnostic == nullptr ||
	     resolutionDiagnostic ==
	         Module_0_was_resolved_to_1_but_jsx_is_not_set)) {
		sourceFile = program->GetSourceFileForResolvedModule(
		    resolvedModule.resolvedFileName);
	}

	if (sourceFile != nullptr) {
		// If there's a resolutionDiagnostic we need to report it even if a
		// sourceFile is found.
		if (resolutionDiagnostic != nullptr) {
			error(errorNode, resolutionDiagnostic,
			      {moduleReference, resolvedModule.resolvedFileName});
		}

		if (errorNode != nullptr) {
			if (resolvedModule.resolvedUsingTsExtension &&
			    tspath::isDeclarationFileName(moduleReference)) {
				if (findAncestor(location, isEmittableImport) != nullptr) {
					std::string_view tsExtension =
					    tspath::tryExtractTSExtension(moduleReference);
					if (tsExtension.empty()) {
						tscUnreachable(
						    "should be able to extract TS extension "
						    "from string that passes "
						    "IsDeclarationFileName");
					}
					error(errorNode,
					      A_declaration_file_cannot_be_imported_without_import_type_Did_you_mean_to_import_an_implementation_file_0_instead,
					      {getSuggestedImportSource(
					          moduleReference, tsExtension, mode)});
				}
			} else if (
			    resolvedModule.resolvedUsingTsExtension &&
			    !compilerOptions->AllowImportingTsExtensionsFrom(
			        tspath::isDeclarationFileName(
			            importingSourceFile->FileName()))) {
				if (findAncestor(location, isEmittableImport) != nullptr) {
					std::string_view tsExtension =
					    tspath::tryExtractTSExtension(moduleReference);
					if (tsExtension.empty()) {
						// Fallback: best-effort extraction using
						// substring match. See checker.go for context.
						for (auto ext : tspath::supportedTSExtensionsFlat) {
							if (moduleReference.find(ext) !=
							    std::string::npos) {
								tsExtension = ext;
								break;
							}
						}
					}
					if (tsExtension.empty()) {
						tscUnreachable(
						    "should be able to extract TS extension "
						    "from string when resolvedUsingTsExtension "
						    "is true");
					}
					error(errorNode,
					      An_import_path_can_only_end_with_a_0_extension_when_allowImportingTsExtensions_is_enabled,
					      {std::string(tsExtension)});
				}
			} else if (
			    compilerOptions->RewriteRelativeImportExtensions ==
			        Tristate::True &&
			    !(location->flags & NodeFlagsAmbient) &&
			    !tspath::isDeclarationFileName(moduleReference) &&
			    !isLiteralImportTypeNode(location) &&
			    !isPartOfTypeOnlyImportOrExportDeclaration(location)) {
				bool shouldRewrite =
				    compilerOptions->RewriteRelativeImportExtensions ==
				        Tristate::True &&
				    tspath::pathIsRelative(moduleReference) &&
				    !tspath::isDeclarationFileName(moduleReference) &&
				    tspath::hasTSFileExtension(moduleReference);
				if (!resolvedModule.resolvedUsingTsExtension &&
				    shouldRewrite) {
					std::string relativeToSourceFile =
					    tspath::getRelativePathFromFile(
					        tspath::getNormalizedAbsolutePath(
					            importingSourceFile->FileName(),
					            program->GetCurrentDirectory()),
					        resolvedModule.resolvedFileName,
					        {program->UseCaseSensitiveFileNames(),
					         program->GetCurrentDirectory()});
					error(errorNode,
					      This_relative_import_path_is_unsafe_to_rewrite_because_it_looks_like_a_file_name_but_actually_resolves_to_0,
					      {relativeToSourceFile});
				} else if (
				    resolvedModule.resolvedUsingTsExtension &&
				    !shouldRewrite &&
				    program->SourceFileMayBeEmitted(sourceFile,
				                                    false)) {
					error(errorNode,
					      This_import_uses_a_0_extension_to_resolve_to_an_input_TypeScript_file_but_will_not_be_rewritten_during_emit_because_it_is_not_a_relative_path,
					      {std::string(tspath::getAnyExtensionFromPath(
					          moduleReference, nullptr, false))});
				} else if (resolvedModule.resolvedUsingTsExtension &&
				           shouldRewrite) {
					if (RedirectInfo* redirect =
					        program->GetRedirectForResolution(
					            sourceFile)) {
						std::string ownRootDir =
						    program->CommonSourceDirectory();
						std::string otherRootDir =
						    redirect->CommonSourceDirectory();
						tspath::ComparePathsOptions compareOptions{
						    program->UseCaseSensitiveFileNames(),
						    program->GetCurrentDirectory()};
						std::string rootDirPath =
						    tspath::getRelativePathFromDirectory(
						        ownRootDir, otherRootDir,
						        compareOptions);
						std::string ownOutDir =
						    compilerOptions->OutDir;
						if (ownOutDir.empty()) {
							ownOutDir = ownRootDir;
						}
						std::string otherOutDir =
						    redirect->CompilerOptions()->OutDir;
						if (otherOutDir.empty()) {
							otherOutDir = otherRootDir;
						}
						std::string outDirPath =
						    tspath::getRelativePathFromDirectory(
						        ownOutDir, otherOutDir,
						        compareOptions);
						if (rootDirPath != outDirPath) {
							error(errorNode,
							      This_import_path_is_unsafe_to_rewrite_because_it_resolves_to_another_project_and_the_relative_path_between_the_projects_output_files_is_not_the_same_as_the_relative_path_between_its_input_files,
							      std::vector<std::string>{});
						}
					}
				}
			}
		}

		if (sourceFile->Symbol != nullptr) {
			if (errorNode != nullptr) {
				if (resolvedModule.isExternalLibraryImport &&
				    !resolutionExtensionIsTSOrJson(
				        resolvedModule.extension)) {
					errorOnImplicitAnyModule(false /*isError*/, errorNode,
					                         mode, resolvedModule,
					                         moduleReference);
				}
				if (moduleKind == ModuleKind::Node16 ||
				    moduleKind == ModuleKind::Node18) {
					bool isSyncImport =
					    (program->GetDefaultResolutionModeForFile(
					         importingSourceFile) ==
					         ModuleKind::CommonJS &&
					     findAncestor(location, isImportCall) ==
					         nullptr) ||
					    findAncestor(location,
					                 isImportEqualsDeclaration) != nullptr;
					Node* overrideHost = findAncestor(
					    location, isResolutionModeOverrideHost);
					if (isSyncImport &&
					    program->GetDefaultResolutionModeForFile(
					        sourceFile) == ModuleKind::ESNext &&
					    !hasResolutionModeOverride(overrideHost)) {
						if (findAncestor(
						        location,
						        isImportEqualsDeclaration) != nullptr) {
							// ImportEquals in an ESM file resolving to
							// another ESM file
							error(errorNode,
							      Module_0_cannot_be_imported_using_this_construct_The_specifier_only_resolves_to_an_ES_module_which_cannot_be_imported_with_require_Use_an_ECMAScript_import_instead,
							      {moduleReference});
						} else {
							// CJS file resolving to an ESM file
							Diagnostic* diagnosticDetails = nullptr;
							std::string_view ext =
							    tspath::tryGetExtensionFromPath(
							        importingSourceFile->FileName());
							if (ext == tspath::extensionTs ||
							    ext == tspath::extensionJs ||
							    ext == tspath::extensionTsx ||
							    ext == tspath::extensionJsx) {
								diagnosticDetails =
								    createModeMismatchDetails(
								        importingSourceFile, errorNode);
							}
							const DiagnosticMessage* message;
							if (overrideHost != nullptr &&
							    overrideHost->kind ==
							        Kind::ImportDeclaration &&
							    overrideHost->importClause() !=
							        nullptr &&
							    overrideHost->importClause()
							        ->isTypeOnly()) {
								message =
								    Type_only_import_of_an_ECMAScript_module_from_a_CommonJS_module_must_have_a_resolution_mode_attribute;
							} else if (overrideHost != nullptr &&
							           overrideHost->kind ==
							               Kind::ImportType) {
								message =
								    Type_import_of_an_ECMAScript_module_from_a_CommonJS_module_must_have_a_resolution_mode_attribute;
							} else {
								message =
								    The_current_file_is_a_CommonJS_module_whose_imports_will_produce_require_calls_however_the_referenced_file_is_an_ECMAScript_module_and_cannot_be_imported_with_require_Consider_writing_a_dynamic_import_0_call_instead;
							}
							addDiagnostic(NewDiagnosticChainForNode(
							    diagnosticDetails, errorNode, message,
							    {moduleReference}));
						}
					}
				}
			}
			return tryResolvePatternAmbientModule(
			    getMergedSymbol(sourceFile->Symbol), moduleReference,
			    importAttributesType);
		}
		Symbol* patternAmbientModule = tryResolvePatternAmbientModule(
		    nullptr /*resolvedSymbol*/, moduleReference,
		    importAttributesType);
		if (patternAmbientModule != nullptr) {
			return patternAmbientModule;
		}
		if (errorNode != nullptr && moduleNotFoundError != nullptr &&
		    !isSideEffectImport(errorNode)) {
			error(errorNode, File_0_is_not_a_module,
			      {resolvedModule.resolvedFileName});
		}
		return nullptr;
	}

	Symbol* patternAmbientModule = tryResolvePatternAmbientModule(
	    nullptr /*resolvedSymbol*/, moduleReference, importAttributesType);
	if (patternAmbientModule != nullptr) {
		return patternAmbientModule;
	}

	if (errorNode == nullptr) {
		return nullptr;
	}

	if ((resolvedModule.resolved &&
	     !resolutionExtensionIsTSOrJson(resolvedModule.extension) &&
	     resolutionDiagnostic == nullptr) ||
	    resolutionDiagnostic ==
	        Could_not_find_a_declaration_file_for_module_0_1_implicitly_has_an_any_type) {
		if (isForAugmentation) {
			error(errorNode,
			      Invalid_module_name_in_augmentation_Module_0_resolves_to_an_untyped_module_at_1_which_cannot_be_augmented,
			      {moduleReference, resolvedModule.resolvedFileName});
		} else {
			errorOnImplicitAnyModule(noImplicitAny &&
			                             moduleNotFoundError != nullptr,
			                         errorNode, mode, resolvedModule,
			                         moduleReference);
		}
		return nullptr;
	}

	if (moduleNotFoundError != nullptr) {
		// See if this was possibly a projectReference redirect
		if (resolvedModule.resolved) {
			const ProjectReferenceRedirect* redirect =
			    program->GetProjectReferenceFromSource(tspath::toPath(
			        resolvedModule.resolvedFileName,
			        program->GetCurrentDirectory(),
			        program->UseCaseSensitiveFileNames()));
			if (redirect != nullptr && !redirect->outputDts.empty()) {
				error(errorNode,
				      Output_file_0_has_not_been_built_from_source_file_1,
				      {redirect->outputDts,
				       resolvedModule.resolvedFileName});
				return nullptr;
			}
		}

		if (resolutionDiagnostic != nullptr) {
			error(errorNode, resolutionDiagnostic,
			      {moduleReference, resolvedModule.resolvedFileName});
		} else {
			bool isExtensionlessRelativePathImport =
			    tspath::pathIsRelative(moduleReference) &&
			    !tspath::hasExtension(moduleReference);
			bool resolutionIsNode16OrNext =
			    moduleResolutionKind == ModuleResolutionKind::Node16 ||
			    moduleResolutionKind == ModuleResolutionKind::NodeNext;
			if (!compilerOptions->GetResolveJsonModule() &&
			    tspath::fileExtensionIs(moduleReference,
			                            tspath::extensionJson)) {
				error(errorNode,
				      Cannot_find_module_0_Consider_using_resolveJsonModule_to_import_module_with_json_extension,
				      {moduleReference});
			} else if (mode == ResolutionModeESM &&
			           resolutionIsNode16OrNext &&
			           isExtensionlessRelativePathImport) {
				std::string absoluteRef =
				    tspath::getNormalizedAbsolutePath(
				        moduleReference,
				        tspath::getDirectoryPath(
				            importingSourceFile->FileName()));
				std::string suggestedExt =
				    getSuggestedImportExtension(absoluteRef);
				if (!suggestedExt.empty()) {
					error(errorNode,
					      Relative_import_paths_need_explicit_file_extensions_in_ECMAScript_imports_when_moduleResolution_is_node16_or_nodenext_Did_you_mean_0,
					      {moduleReference + suggestedExt});
				} else {
					error(errorNode,
					      Relative_import_paths_need_explicit_file_extensions_in_ECMAScript_imports_when_moduleResolution_is_node16_or_nodenext_Consider_adding_an_extension_to_the_import_path,
					      std::vector<std::string>{});
				}
			} else if (!resolvedModule.alternateResult.empty()) {
				Diagnostic* errorInfo = createModuleNotFoundChain(
				    resolvedModule, errorNode, moduleReference, mode,
				    moduleReference);
				addDiagnostic(NewDiagnosticChainForNode(
				    errorInfo, errorNode, moduleNotFoundError,
				    {moduleReference}));
			} else {
				error(errorNode, moduleNotFoundError,
				      {moduleReference});
			}
		}
	}

	return nullptr;
}

// Resolves the module reference to a pattern ambient module, if one exists.
// If a resolved symbol from regular module resolution exists and we have an
// empty import attributes type, we prefer the resolved symbol.
Symbol* Checker::tryResolvePatternAmbientModule(
    Symbol* resolvedSymbol, const std::string& moduleReference,
    Type* importAttributesType) {
	if (isEmptyObjectType(importAttributesType) && resolvedSymbol != nullptr) {
		return resolvedSymbol;
	}
	if (!patternAmbientModules.empty()) {
		std::vector<PatternAmbientModule*> candidates;
		for (auto& v : patternAmbientModules) {
			Type* moduleAttributesType =
			    getTypeOfModuleImportAttributes(v.symbol);
			if (tryParsePattern(v.pattern).matches(moduleReference) &&
			    isTypeAssignableTo(importAttributesType,
			                       moduleAttributesType)) {
				candidates.push_back(&v);
			}
		}
		if (!candidates.empty()) {
			Symbol* augmentation = nullptr;
			if (auto it = patternAmbientModuleAugmentations.find(
			        moduleReference);
			    it != patternAmbientModuleAugmentations.end()) {
				augmentation = it->second;
			}
			Symbol* augmentationTarget = nullptr;
			if (auto it = patternAmbientModuleAugmentationTargets.find(
			        moduleReference);
			    it != patternAmbientModuleAugmentationTargets.end()) {
				augmentationTarget = it->second;
			}

			if (candidates.size() == 1) {
				Symbol* mergedCandidate =
				    getMergedSymbol(candidates[0]->symbol);
				if (augmentation != nullptr &&
				    augmentationTarget == mergedCandidate) {
					return getMergedSymbol(augmentation);
				}
				return mergedCandidate;
			}

			std::vector<PatternAmbientModule*> bestTypeCandidates;
			for (size_t i = 0; i < candidates.size(); i++) {
				PatternAmbientModule* candidate = candidates[i];
				Type* candidateType = getTypeOfModuleImportAttributes(
				    candidate->symbol);
				bool dominated = false;
				for (size_t j = 0; j < candidates.size(); j++) {
					Type* otherType = getTypeOfModuleImportAttributes(
					    candidates[j]->symbol);
					if (i != j &&
					    isTypeStrictSubtypeOf(otherType,
					                          candidateType) &&
					    !isTypeIdenticalTo(otherType, candidateType)) {
						dominated = true;
						break;
					}
				}
				if (!dominated) {
					bestTypeCandidates.push_back(candidate);
				}
			}
			if (bestTypeCandidates.size() == 1) {
				Symbol* mergedCandidate =
				    getMergedSymbol(bestTypeCandidates[0]->symbol);
				if (augmentation != nullptr &&
				    augmentationTarget == mergedCandidate) {
					return getMergedSymbol(augmentation);
				}
				return mergedCandidate;
			}
			PatternAmbientModule* pattern = *findBestPatternMatch(
			    bestTypeCandidates,
			    +[](PatternAmbientModule* const& v) -> Pattern {
				    return tryParsePattern(v->pattern);
			    },
			    moduleReference);
			Symbol* mergedCandidate = getMergedSymbol(pattern->symbol);
			if (augmentation != nullptr &&
			    augmentationTarget == mergedCandidate) {
				return getMergedSymbol(augmentation);
			}
			return mergedCandidate;
		}
	}
	return resolvedSymbol;
}

Symbol* Checker::tryFindAmbientModule(const std::string& moduleReference,
                                      bool withAugmentations) {
	if (tspath::isExternalModuleNameRelative(moduleReference)) {
		return nullptr;
	}
	Symbol* symbol = getSymbol(globals, "\"" + moduleReference + "\"",
	                           SymbolFlagsValueModule);
	// merged symbol is module declaration symbol combined with all
	// augmentations
	if (withAugmentations) {
		return getMergedSymbol(symbol);
	}
	return symbol;
}

bool Checker::isCommonJSRequire(Node* node) {
	if (!isRequireCall(node, true /*requireStringLiteralLikeArgument*/)) {
		return false;
	}
	if (!isIdentifier(node->expression())) {
		tscUnreachable("Expected identifier for require call");
	}
	// Make sure require is not a local function
	Symbol* resolvedRequire =
	    resolveName(node->expression(), node->expression()->text(),
	                SymbolFlagsValue, nullptr /*nameNotFoundMessage*/,
	                true /*isUse*/, false /*excludeGlobals*/);
	if (resolvedRequire == requireSymbol) {
		return true;
	}
	// project includes symbol named 'require' - make sure that it is
	// ambient and local non-alias
	if (resolvedRequire == nullptr ||
	    resolvedRequire->flags & SymbolFlagsAlias) {
		return false;
	}

	Kind targetDeclarationKind = Kind::Unknown;
	if (resolvedRequire->flags & SymbolFlagsFunction) {
		targetDeclarationKind = Kind::FunctionDeclaration;
	} else if (resolvedRequire->flags & SymbolFlagsVariable) {
		targetDeclarationKind = Kind::VariableDeclaration;
	}
	if (targetDeclarationKind != Kind::Unknown) {
		Node* decl =
		    getDeclarationOfKind(resolvedRequire, targetDeclarationKind);
		// function/variable declaration should be ambient
		return decl != nullptr && decl->flags & NodeFlagsAmbient;
	}
	return false;
}

void Checker::errorOnImplicitAnyModule(bool isError, Node* errorNode,
                                       ResolutionMode mode,
                                       const ResolvedModule& resolvedModule,
                                       const std::string& moduleReference) {
	if (isSideEffectImport(errorNode)) {
		return;
	}
	Diagnostic* errorInfo = nullptr;
	if (!tspath::isExternalModuleNameRelative(moduleReference) &&
	    !resolvedModule.packageId.name.empty()) {
		errorInfo = createModuleNotFoundChain(
		    resolvedModule, errorNode, moduleReference, mode,
		    resolvedModule.packageId.name);
	}
	addErrorOrSuggestion(
	    isError,
	    NewDiagnosticChainForNode(
	        errorInfo, errorNode,
	        Could_not_find_a_declaration_file_for_module_0_1_implicitly_has_an_any_type,
	        {moduleReference, resolvedModule.resolvedFileName}));
}

// checker.go:15816 createModuleNotFoundChain
Diagnostic* Checker::createModuleNotFoundChain(
    const ResolvedModule& resolvedModule, Node* errorNode,
    const std::string& moduleReference, ResolutionMode mode,
    const std::string& packageName) {
	// Store the original packageName for repopulateInfo before any modifications
	std::string storedPackageName = packageName;
	if (storedPackageName == moduleReference) {
		storedPackageName = "";
	}
	DiagnosticDetails details =
	    CreateModuleNotFoundChain(program, getSourceFileOfNode(errorNode),
	                              moduleReference, mode, packageName);
	Diagnostic* result =
	    NewDiagnosticForNode(errorNode, details.message, details.args);
	result->SetRepopulateInfo(new RepopulateDiagnosticInfo{
	    RepopulateDiagnosticKind::ModuleNotFound, moduleReference, mode,
	    storedPackageName});
	return result;
}

// checker.go:15834 createModeMismatchDetails
Diagnostic* Checker::createModeMismatchDetails(SourceFile* sourceFile,
                                               Node* errorNode) {
	DiagnosticDetails details = CreateModeMismatchDetails(program, sourceFile);
	Diagnostic* result =
	    NewDiagnosticForNode(errorNode, details.message, details.args);
	result->SetRepopulateInfo(new RepopulateDiagnosticInfo{
	    RepopulateDiagnosticKind::ModeMismatch, "", ResolutionModeNone, ""});
	return result;
}

std::string Checker::getSuggestedImportSource(
    const std::string& moduleReference, std::string_view tsExtension,
    ResolutionMode mode) {
	std::string importSourceWithoutExtension{ tspath::removeExtension(
	    moduleReference, tsExtension) };

	// Direct users to import source with .js extension if outputting an ES
	// module. https://github.com/microsoft/TypeScript/issues/42151
	if ((moduleKind == ModuleKind::ES2015 ||
	     moduleKind == ModuleKind::ES2020 ||
	     moduleKind == ModuleKind::ES2022 ||
	     moduleKind == ModuleKind::ESNext ||
	     moduleKind == ModuleKind::Node20 ||
	     moduleKind == ModuleKind::NodeNext ||
	     moduleKind == ModuleKind::Preserve) ||
	    mode == ModuleKind::ESNext) {
		bool preferTs =
		    tspath::isDeclarationFileName(moduleReference) &&
		    compilerOptions->GetAllowImportingTsExtensions();
		const char* ext;
		if (tsExtension == tspath::extensionMts ||
		    tsExtension == tspath::extensionDmts) {
			ext = preferTs ? ".mts" : ".mjs";
		} else if (tsExtension == tspath::extensionCts ||
		           tsExtension == tspath::extensionDcts) {
			ext = preferTs ? ".cts" : ".cjs";
		} else {
			ext = preferTs ? ".ts" : ".js";
		}
		return importSourceWithoutExtension + ext;
	}

	return importSourceWithoutExtension;
}

std::string Checker::getSuggestedImportExtension(
    const std::string& extensionlessImportPath) {
	if (program->FileExists(extensionlessImportPath + ".mts")) {
		return ".mjs";
	}
	if (program->FileExists(extensionlessImportPath + ".ts")) {
		return ".js";
	}
	if (program->FileExists(extensionlessImportPath + ".cts")) {
		return ".cjs";
	}
	if (program->FileExists(extensionlessImportPath + ".mjs")) {
		return ".mjs";
	}
	if (program->FileExists(extensionlessImportPath + ".js")) {
		return ".js";
	}
	if (program->FileExists(extensionlessImportPath + ".cjs")) {
		return ".cjs";
	}
	if (program->FileExists(extensionlessImportPath + ".tsx")) {
		return compilerOptions->Jsx == JsxEmit::Preserve ? ".jsx" : ".js";
	}
	if (program->FileExists(extensionlessImportPath + ".jsx")) {
		return ".jsx";
	}
	if (program->FileExists(extensionlessImportPath + ".json")) {
		return ".json";
	}
	return "";
}

// ---------------------------------------------------------------------------
// Entity-name resolution — checker.go:16091+
// ---------------------------------------------------------------------------

Symbol* Checker::resolveEntityName(Node* name, SymbolFlags meaning,
                                   bool ignoreErrors, bool dontResolveAlias,
                                   Node* location) {
	if (nodeIsMissing(name)) {
		return nullptr;
	}
	Symbol* symbol = nullptr;
	switch (name->kind) {
	case Kind::Identifier: {
		const DiagnosticMessage* message = nullptr;
		if (!ignoreErrors) {
			if (meaning == SymbolFlagsNamespace ||
			    nodeIsSynthesized(name)) {
				message = Cannot_find_namespace_0;
			} else {
				message =
				    getCannotFindNameDiagnosticForName(
				        getFirstIdentifier(name));
			}
		}
		Node* resolveLocation = location != nullptr ? location : name;
		if (meaning == SymbolFlagsNamespace) {
			symbol = getMergedSymbol(resolveName(
			    resolveLocation, name->text(), meaning, nullptr,
			    true /*isUse*/, false /*excludeGlobals*/));
			if (symbol == nullptr) {
				Symbol* alias = getMergedSymbol(resolveName(
				    resolveLocation, name->text(),
				    SymbolFlagsAlias, nullptr, true /*isUse*/,
				    false /*excludeGlobals*/));
				if (alias != nullptr &&
				    alias->name == InternalSymbolNameExportEquals) {
					// resolve typedefs exported from commonjs,
					// stored on the module symbol
					symbol = alias->parent;
				}
			}
			if (symbol == nullptr && message != nullptr) {
				resolveName(resolveLocation, name->text(), meaning,
				            message, true /*isUse*/,
				            false /*excludeGlobals*/);
			}
		} else {
			symbol = getMergedSymbol(resolveName(
			    resolveLocation, name->text(), meaning, message,
			    true /*isUse*/, false /*excludeGlobals*/));
		}
		break;
	}
	case Kind::QualifiedName: {
		QualifiedName* qualified = name->as<QualifiedName>();
		symbol = resolveQualifiedName(name, qualified->Left,
		                              qualified->Right, meaning,
		                              ignoreErrors, location);
		break;
	}
	case Kind::PropertyAccessExpression: {
		PropertyAccessExpression* access =
		    name->as<PropertyAccessExpression>();
		symbol = resolveQualifiedName(name, access->Expression,
		                              access->name, meaning, ignoreErrors,
		                              location);
		break;
	}
	default:
		tscUnreachable("Unknown entity name kind");
	}
	if (symbol != nullptr && symbol != unknownSymbol) {
		if (!nodeIsSynthesized(name) && isEntityName(name) &&
		    (symbol->flags & SymbolFlagsAlias ||
		     (name->parent != nullptr &&
		      name->parent->kind == Kind::ExportAssignment))) {
			markSymbolOfAliasDeclarationIfTypeOnly(
			    getAliasDeclarationFromName(name), nullptr);
		}
		// We know a symbol with the given meaning exists along the alias
		// chain, so resolve until we find it.
		while (!(symbol->flags & meaning) && !dontResolveAlias &&
		       symbol->flags & SymbolFlagsAlias) {
			symbol = resolveAlias(symbol);
		}
	}
	return symbol;
}

Symbol* Checker::resolveQualifiedName(Node* name, Node* left, Node* right,
                                      SymbolFlags meaning, bool ignoreErrors,
                                      Node* location) {
	Symbol* namespace_ =
	    resolveEntityName(left, SymbolFlagsNamespace, ignoreErrors,
	                      false /*dontResolveAlias*/, location);
	if (namespace_ == nullptr || nodeIsMissing(right)) {
		return nullptr;
	}
	if (namespace_ == unknownSymbol) {
		return namespace_;
	}
	if (namespace_->valueDeclaration != nullptr &&
	    isInJSFile(namespace_->valueDeclaration) &&
	    compilerOptions->GetModuleResolutionKind() !=
	        ModuleResolutionKind::Bundler &&
	    isVariableDeclaration(namespace_->valueDeclaration) &&
	    namespace_->valueDeclaration->initializer() != nullptr &&
	    isCommonJSRequire(
	        namespace_->valueDeclaration->initializer())) {
		Node* moduleName =
		    namespace_->valueDeclaration->initializer()->arguments()[0];
		Symbol* moduleSym = resolveExternalModuleName(
		    moduleName, moduleName, false /*ignoreErrors*/, nullptr);
		if (moduleSym != nullptr) {
			Symbol* resolvedModuleSymbol = resolveExternalModuleSymbol(
			    moduleSym, false /*dontResolveAlias*/);
			if (resolvedModuleSymbol != nullptr) {
				namespace_ = resolvedModuleSymbol;
			}
		}
	}
	std::string text{right->text()};
	SymbolTable exportsOfNamespace = getExportsOfSymbol(namespace_);
	Symbol* symbol = getMergedSymbol(
	    getSymbol(exportsOfNamespace, text, meaning));
	if (symbol == nullptr && namespace_->flags & SymbolFlagsAlias) {
		// `namespace` can be resolved further if there was a symbol merge
		// with a re-export
		SymbolTable exportsOfAlias =
		    getExportsOfSymbol(resolveAlias(namespace_));
		symbol =
		    getMergedSymbol(getSymbol(exportsOfAlias, text, meaning));
	}
	if (symbol == nullptr) {
		if (!ignoreErrors) {
			std::string namespaceName =
			    getFullyQualifiedName(namespace_,
			                          nullptr /*containingLocation*/);
			std::string declarationName =
			    declarationNameToString(right);
			Symbol* suggestionForNonexistentModule =
			    getSuggestedSymbolForNonexistentModule(right, namespace_);
			if (suggestionForNonexistentModule != nullptr) {
				error(right,
				      X_0_has_no_exported_member_named_1_Did_you_mean_2,
				      {namespaceName, declarationName,
				       symbolToString(suggestionForNonexistentModule)});
				return nullptr;
			}
			Node* containingQualifiedName = nullptr;
			if (isQualifiedName(name)) {
				containingQualifiedName =
				    getContainingQualifiedNameNode(name);
			}
			bool canSuggestTypeof =
			    globalObjectType != nullptr &&
			    meaning & SymbolFlagsType &&
			    containingQualifiedName != nullptr &&
			    !isTypeOfExpression(containingQualifiedName->parent) &&
			    tryGetQualifiedNameAsValue(containingQualifiedName) !=
			        nullptr;
			if (canSuggestTypeof) {
				error(containingQualifiedName,
				      X_0_refers_to_a_value_but_is_being_used_as_a_type_here_Did_you_mean_typeof_0,
				      {entityNameToString(
				          containingQualifiedName)});
				return nullptr;
			}
			if (meaning & SymbolFlagsNamespace) {
				if (isQualifiedName(name->parent)) {
					SymbolTable nsExports =
					    getExportsOfSymbol(namespace_);
					Symbol* exportedTypeSymbol = getMergedSymbol(
					    getSymbol(nsExports, text,
					              SymbolFlagsType));
					if (exportedTypeSymbol != nullptr) {
						QualifiedName* qualified =
						    name->parent->as<QualifiedName>();
						error(qualified->Right,
						      Cannot_access_0_1_because_0_is_a_type_but_not_a_namespace_Did_you_mean_to_retrieve_the_type_of_the_property_1_in_0_with_0_1,
						      {symbolToString(exportedTypeSymbol),
						       std::string(
						           qualified->Right->text())});
						return nullptr;
					}
				}
			}
			error(right, Namespace_0_has_no_exported_member_1,
			      {namespaceName, declarationName});
		}
	}
	return symbol;
}

Symbol* Checker::tryGetQualifiedNameAsValue(Node* node) {
	Node* id = getFirstIdentifier(node);
	Symbol* symbol =
	    resolveName(id, id->text(), SymbolFlagsValue,
	                nullptr /*nameNotFoundMessage*/, true /*isUse*/,
	                false /*excludeGlobals*/);
	if (symbol == nullptr) {
		return nullptr;
	}
	Node* n = id;
	while (isQualifiedName(n->parent)) {
		Type* t = getTypeOfSymbol(symbol);
		symbol = getPropertyOfType(
		    t, std::string(n->parent->as<QualifiedName>()->Right->text()));
		if (symbol == nullptr) {
			return nullptr;
		}
		n = n->parent;
	}
	return symbol;
}

Symbol* Checker::getSuggestedSymbolForNonexistentModule(
    Node* name, Symbol* targetModule) {
	std::vector<Symbol*> values;
	SymbolTable exports = getExportsOfModule(targetModule);
	for (auto& [k, v] : exports) {
		values.push_back(v);
	}
	return getSpellingSuggestionForName(std::string(name->text()), values,
	                                  SymbolFlagsModuleMember);
}

bool Checker::markSymbolOfAliasDeclarationIfTypeOnly(
    Node* aliasDeclaration, Node* exportStarDeclaration) {
	if (aliasDeclaration == nullptr ||
	    !isDeclarationNode(aliasDeclaration)) {
		return false;
	}
	// If the declaration itself is type-only, mark it and return. No need
	// to check what it resolves to.
	Symbol* sourceSymbol = getSymbolOfDeclaration(aliasDeclaration);
	AliasSymbolLinks* links = aliasSymbolLinks.Get(sourceSymbol);
	if (links->typeOnlyDeclaration == nullptr &&
	    isTypeOnlyImportOrExportDeclaration(aliasDeclaration)) {
		links->typeOnlyDeclaration = aliasDeclaration;
		return true;
	}
	if (links->typeOnlyDeclaration == nullptr &&
	    exportStarDeclaration != nullptr) {
		links->typeOnlyDeclaration = exportStarDeclaration;
		return true;
	}
	return links->typeOnlyDeclaration != nullptr;
}

const DiagnosticMessage* Checker::getCannotFindNameDiagnosticForName(
    Node* node) {
	std::string_view text = node->text();
	if (text == "document" || text == "console") {
		return Cannot_find_name_0_Do_you_need_to_change_your_target_library_Try_changing_the_lib_compiler_option_to_include_dom;
	}
	if (text == "$") {
		return compilerOptions->UsesWildcardTypes()
		           ? Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_jQuery_Try_npm_i_save_dev_types_Slashjquery
		           : Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_jQuery_Try_npm_i_save_dev_types_Slashjquery_and_then_add_jquery_to_the_types_field_in_your_tsconfig;
	}
	if (text == "beforeEach" || text == "describe" || text == "suite" ||
	    text == "it" || text == "test") {
		return compilerOptions->UsesWildcardTypes()
		           ? Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_a_test_runner_Try_npm_i_save_dev_types_Slashjest_or_npm_i_save_dev_types_Slashmocha
		           : Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_a_test_runner_Try_npm_i_save_dev_types_Slashjest_or_npm_i_save_dev_types_Slashmocha_and_then_add_jest_or_mocha_to_the_types_field_in_your_tsconfig;
	}
	if (text == "process" || text == "require" || text == "Buffer" ||
	    text == "module" || text == "NodeJS") {
		return compilerOptions->UsesWildcardTypes()
		           ? Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_node_Try_npm_i_save_dev_types_Slashnode
		           : Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_node_Try_npm_i_save_dev_types_Slashnode_and_then_add_node_to_the_types_field_in_your_tsconfig;
	}
	if (text == "Bun") {
		return compilerOptions->UsesWildcardTypes()
		           ? Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_Bun_Try_npm_i_save_dev_types_Slashbun
		           : Cannot_find_name_0_Do_you_need_to_install_type_definitions_for_Bun_Try_npm_i_save_dev_types_Slashbun_and_then_add_bun_to_the_types_field_in_your_tsconfig;
	}
	if (text == "Map" || text == "Set" || text == "Promise" ||
	    text == "Symbol" || text == "WeakMap" || text == "WeakSet" ||
	    text == "Iterator" || text == "AsyncIterator" ||
	    text == "SharedArrayBuffer" || text == "Atomics" ||
	    text == "AsyncIterable" || text == "AsyncIterableIterator" ||
	    text == "AsyncGenerator" || text == "AsyncGeneratorFunction" ||
	    text == "BigInt" || text == "Reflect" || text == "BigInt64Array" ||
	    text == "BigUint64Array") {
		return Cannot_find_name_0_Do_you_need_to_change_your_target_library_Try_changing_the_lib_compiler_option_to_1_or_later;
	}
	if (text == "await" && isCallExpression(node->parent)) {
		return Cannot_find_name_0_Did_you_mean_to_write_this_in_an_async_function;
	}
	if (node->parent->kind == Kind::ShorthandPropertyAssignment) {
		return No_value_exists_in_scope_for_the_shorthand_property_0_Either_declare_one_or_provide_an_initializer;
	}
	return Cannot_find_name_0;
}

std::string Checker::getFullyQualifiedName(Symbol* symbol,
                                           Node* containingLocation) {
	if (symbol->parent != nullptr) {
		return getFullyQualifiedName(symbol->parent, containingLocation) +
		       "." + symbolToString(symbol);
	}
	return symbolToStringEx(symbol, containingLocation, SymbolFlagsAll,
	                        SymbolFormatFlagsDoNotIncludeSymbolChain |
	                            SymbolFormatFlagsAllowAnyNodeKind);
}

// ---------------------------------------------------------------------------
// === slice: program ===
// Stubs for checker methods that are declared and referenced by already-ported
// bodies but belong to slices not yet ported (type relations, checking walker,
// declaration emit). Each throws via TSC_UNREACHABLE like Go's equivalents —
// never fake results.
// ---------------------------------------------------------------------------


// checker.go:7685 checkExpressionCached
Type* Checker::checkExpressionCached(Node* node) {
	return checkExpressionCachedEx(node, CheckModeNormal);
}

// checker.go:14492 createDiagnosticForNode — file-local free function in Go;
// kept as a Checker member to reuse the class declaration.
Diagnostic* Checker::createDiagnosticForNode(
    Node* node, const DiagnosticMessage* message,
    const std::vector<std::string>& args) {
	return NewDiagnosticForNode(node, message, args);
}


// checker.go:22479 isNamedMember
bool Checker::isNamedMember(Symbol* symbol, const std::string& id) {
	return !isReservedMemberName(id) && symbolIsValue(symbol);
}

// checker.go:22468 isDeclarationContainedBy
bool Checker::isDeclarationContainedBy(Symbol* symbol, Symbol* container) {
	if (Node* declaration = symbol->valueDeclaration;
	    declaration != nullptr) {
		for (Node* d : container->declarations) {
			// text.go:58 TextRange.ContainedBy
			if (d->loc.pos() <= declaration->loc.pos() &&
			    d->loc.end() >= declaration->loc.end()) {
				return true;
			}
		}
	}
	return false;
}

// checker.go:22483 symbolIsValue
bool Checker::symbolIsValue(Symbol* symbol) {
	return symbolIsValueEx(symbol, false /*includeTypeOnlyMembers*/);
}

// checker.go:22487 symbolIsValueEx
bool Checker::symbolIsValueEx(Symbol* symbol, bool includeTypeOnlyMembers) {
	return (symbol->flags & SymbolFlagsValue) ||
	       ((symbol->flags & SymbolFlagsAlias) &&
	        (getSymbolFlagsEx(symbol, !includeTypeOnlyMembers,
	                          false /*excludeLocalMeanings*/) &
	         SymbolFlagsValue));
}

// checker.go:22441 getNamedMembers
std::vector<Symbol*> Checker::getNamedMembers(const SymbolTable& members,
                                              Symbol* typeSymbol) {
	if (members.empty()) {
		return {};
	}
	// For classes and interfaces, we store explicitly declared members ahead
	// of inherited members. This ensures we process explicitly declared
	// members first in type relations, which is beneficial because explicitly
	// declared members are more likely to contain discriminating differences.
	// See https://github.com/microsoft/TypeScript/tsc/issues/1968.
	std::vector<Symbol*> result;
	result.reserve(members.size());
	size_t containedCount = 0;
	if (typeSymbol != nullptr &&
	    (typeSymbol->flags & (SymbolFlagsClass | SymbolFlagsInterface))) {
		for (auto& [id, symbol] : members) {
			if (isNamedMember(symbol, id) &&
			    isDeclarationContainedBy(symbol, typeSymbol)) {
				result.push_back(symbol);
			}
		}
		containedCount = result.size();
	}
	for (auto& [id, symbol] : members) {
		if (isNamedMember(symbol, id) &&
		    (typeSymbol == nullptr ||
		     !(typeSymbol->flags &
		       (SymbolFlagsClass | SymbolFlagsInterface)) ||
		     !isDeclarationContainedBy(symbol, typeSymbol))) {
			result.push_back(symbol);
		}
	}
	std::vector<Symbol*> first(result.begin(), result.begin() + containedCount);
	std::vector<Symbol*> rest(result.begin() + containedCount, result.end());
	sortSymbols(first);
	sortSymbols(rest);
	std::copy(first.begin(), first.end(), result.begin());
	std::copy(rest.begin(), rest.end(), result.begin() + containedCount);
	return result;
}

// (deduped: getTypeFromImportAttributes defined in cpp/internal/checker/checker_declchecks2.cpp)

// (deduped: getTypeOfSymbol defined in cpp/internal/checker/checker_decltypes.cpp)


// checker.go:25346 getGenericObjectFlags
ObjectFlags Checker::getGenericObjectFlags(Type* t) {
	ObjectFlags combinedFlags = 0;
	if (t->flags & (TypeFlagsUnionOrIntersection | TypeFlagsSubstitution)) {
		if (!(t->objectFlags & ObjectFlagsIsGenericTypeComputed)) {
			if (t->flags & TypeFlagsUnionOrIntersection) {
				for (Type* u : t->types()) {
					combinedFlags |= getGenericObjectFlags(u);
				}
			} else {
				combinedFlags =
				    getGenericObjectFlags(t->AsSubstitutionType()->baseType) |
				    getGenericObjectFlags(t->AsSubstitutionType()->constraint);
			}
			t->objectFlags |=
			    ObjectFlagsIsGenericTypeComputed | combinedFlags;
		}
		return t->objectFlags & ObjectFlagsIsGenericType;
	}
	if ((t->flags & TypeFlagsInstantiableNonPrimitive) ||
	    isGenericMappedType(t) || isGenericTupleType(t)) {
		combinedFlags |= ObjectFlagsIsGenericObjectType;
	}
	if ((t->flags & (TypeFlagsInstantiableNonPrimitive | TypeFlagsIndex)) ||
	    isGenericStringLikeType(t)) {
		combinedFlags |= ObjectFlagsIsGenericIndexType;
	}
	return combinedFlags;
}

// checker.go:23946 isTupleType (free function)
static bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) &&
	       (t->AsTypeReference()->target->objectFlags & ObjectFlagsTuple);
}

// checker.go:25370 isGenericTupleType
bool Checker::isGenericTupleType(Type* t) {
	return isTupleType(t) &&
	       (t->AsTypeReference()->target->AsTupleType()->combinedFlags &
	        ElementFlagsVariadic);
}

// checker.go:25342 isGenericIndexType
bool Checker::isGenericIndexType(Type* t) {
	return (getGenericObjectFlags(t) & ObjectFlagsIsGenericIndexType) != 0;
}

// checker.go:23091 getTypeParameterFromMappedType
Type* Checker::getTypeParameterFromMappedType(Type* t) {
	MappedType* m = t->AsMappedType();
	if (!m->typeParameter) {
		m->typeParameter = getDeclaredTypeOfTypeParameter(getSymbolOfDeclaration(
		    m->declaration->as<MappedTypeNode>()->TypeParameter));
	}
	return m->typeParameter;
}

// checker.go:23099 getConstraintTypeFromMappedType
Type* Checker::getConstraintTypeFromMappedType(Type* t) {
	MappedType* m = t->AsMappedType();
	if (!m->constraintType) {
		Type* c = getConstraintOfTypeParameter(getTypeParameterFromMappedType(t));
		m->constraintType = c ? c : errorType;
	}
	return m->constraintType;
}

// checker.go:23107 getNameTypeFromMappedType
Type* Checker::getNameTypeFromMappedType(Type* t) {
	MappedType* m = t->AsMappedType();
	if (!m->declaration->as<MappedTypeNode>()->NameType) {
		return nullptr;
	}
	if (!m->nameType) {
		m->nameType = instantiateType(
		    getTypeFromTypeNode(m->declaration->as<MappedTypeNode>()->NameType),
		    m->mapper);
	}
	return m->nameType;
}

// checker.go:25374 isGenericMappedType
bool Checker::isGenericMappedType(Type* t) {
	if (t->objectFlags & ObjectFlagsMapped) {
		Type* constraint = getConstraintTypeFromMappedType(t);
		if (isGenericIndexType(constraint)) {
			return true;
		}
		// A mapped type is generic if the 'as' clause references generic types
		// other than the iteration type. To determine this, we substitute the
		// constraint type (that we now know isn't generic) for the iteration
		// type and check whether the resulting type is generic.
		Type* nameType = getNameTypeFromMappedType(t);
		if (nameType != nullptr &&
		    isGenericIndexType(instantiateType(
		        nameType,
		        newSimpleTypeMapper(getTypeParameterFromMappedType(t),
		                            constraint)))) {
			return true;
		}
	}
	return false;
}

// checker.go:14304 newSymbol
Symbol* Checker::newSymbol(SymbolFlags flags, const std::string& name) {
	SymbolCount++;
	Symbol* result = symbolArena.alloc<Symbol>();
	result->flags = flags | SymbolFlagsTransient;
	result->name = name;
	return result;
}

// checker.go:14312 newSymbolEx
Symbol* Checker::newSymbolEx(SymbolFlags flags, const std::string& name,
                             CheckFlags checkFlags) {
	Symbol* result = newSymbol(flags, name);
	result->checkFlags = checkFlags;
	return result;
}

// checker.go:15875 resolveExternalModuleSymbol
Symbol* Checker::resolveExternalModuleSymbol(Symbol* moduleSymbol,
                                             bool dontResolveAlias) {
	if (moduleSymbol != nullptr) {
		Symbol* exportEquals =
		    resolveSymbolEx(moduleSymbol->exports[InternalSymbolNameExportEquals],
		                    dontResolveAlias);
		if (exportEquals != nullptr) {
			return getMergedSymbol(exportEquals);
		}
	}
	return moduleSymbol;
}

// checker.go:19402 — resolveStructuredTypeMembers (members slice owner)
StructuredType* Checker::resolveStructuredTypeMembers(Type* t) {
	if ((t->objectFlags & ObjectFlagsMembersResolved) == 0) {
		if ((t->flags & TypeFlagsObject) != 0) {
			if ((t->objectFlags & ObjectFlagsReference) != 0) {
				resolveTypeReferenceMembers(t);
			} else if ((t->objectFlags & ObjectFlagsClassOrInterface) != 0) {
				resolveClassOrInterfaceMembers(t);
			} else if ((t->objectFlags & ObjectFlagsReverseMapped) != 0) {
				resolveReverseMappedTypeMembers(t);
			} else if ((t->objectFlags & ObjectFlagsAnonymous) != 0) {
				resolveAnonymousTypeMembers(t);
			} else if ((t->objectFlags & ObjectFlagsMapped) != 0) {
				resolveMappedTypeMembers(t);
			} else {
				TSC_UNREACHABLE("Unhandled case in resolveStructuredTypeMembers");
			}
		} else if ((t->flags & TypeFlagsUnion) != 0) {
			resolveUnionTypeMembers(t);
		} else if ((t->flags & TypeFlagsIntersection) != 0) {
			resolveIntersectionTypeMembers(t);
		} else {
			TSC_UNREACHABLE("Unhandled case in resolveStructuredTypeMembers");
		}
	}
	return t->AsStructuredType();
}
}  // namespace checker
}  // namespace tsc

// === slice: program — additional stubs/hashes needed by program-link ===

namespace tsc {
namespace checker {
// Hash operators for checker.h key structs (infra for maps; Go hashes these
// natively). Any equal-keys→equal-hash function is faithful.
inline static uint64_t checkerHashStep(uint64_t h, uint64_t x) {
	h ^= x;
	h *= 1099511628211ull;
	h ^= h >> 33;
	return h;
}

size_t Checker::EnumLiteralKeyHash::operator()(const EnumLiteralKey& k) const noexcept {
	uint64_t h = 1469598103934665603ull;
	h = checkerHashStep(h, reinterpret_cast<uintptr_t>(k.enumSymbol));
	std::visit(
	    [&h](auto&& v) {
		    using T = std::decay_t<decltype(v)>;
		    if constexpr (std::is_same_v<T, std::string>) {
			    for (char c : v)
				    h = checkerHashStep(h, static_cast<uint64_t>(c));
		    } else if constexpr (std::is_same_v<T, Number>) {
			    uint64_t bits;
			    double d = v.v;
			    std::memcpy(&bits, &d, 8);
			    h = checkerHashStep(h, bits);
		    } else if constexpr (std::is_same_v<T, PseudoBigInt>) {
			    h = checkerHashStep(h, static_cast<uint64_t>(v.base10Value.size()) |
				                   (v.negative ? 0x8000000000000000ull : 0));
			    for (char c : v.base10Value)
				    h = checkerHashStep(h, static_cast<uint64_t>(c));
		    }
	    },
	    k.value);
	return static_cast<size_t>(h);
}

size_t Checker::CachedTypeKeyHash::operator()(const CachedTypeKey& k) const noexcept {
	uint64_t h = 1469598103934665603ull;
	h = checkerHashStep(h, static_cast<uint64_t>(k.kind));
	h = checkerHashStep(h, static_cast<uint64_t>(k.typeId));
	return static_cast<size_t>(h);
}


size_t Checker::SubstitutionTypeKeyHash::operator()(const SubstitutionTypeKey& k) const noexcept {
	uint64_t h = 1469598103934665603ull;
	h = checkerHashStep(h, static_cast<uint64_t>(k.baseId));
	h = checkerHashStep(h, static_cast<uint64_t>(k.constraintId));
	return static_cast<size_t>(h);
}

size_t Checker::UnionOfUnionKeyHash::operator()(const UnionOfUnionKey& k) const noexcept {
	uint64_t h = 1469598103934665603ull;
	h = checkerHashStep(h, static_cast<uint64_t>(k.id1));
	h = checkerHashStep(h, static_cast<uint64_t>(k.id2));
	h = checkerHashStep(h, static_cast<uint64_t>(k.r));
	h = checkerHashStep(h, CacheKeyHash{}(k.a));
	return static_cast<size_t>(h);
}

size_t Checker::CachedSignatureKeyHash::operator()(const CachedSignatureKey& k) const noexcept {
	uint64_t h = 1469598103934665603ull;
	h = checkerHashStep(h, reinterpret_cast<uintptr_t>(k.sig));
	h = checkerHashStep(h, CacheKeyHash{}(k.key));
	return static_cast<size_t>(h);
}

size_t Checker::StringMappingKeyHash::operator()(const StringMappingKey& k) const noexcept {
	uint64_t h = 1469598103934665603ull;
	h = checkerHashStep(h, reinterpret_cast<uintptr_t>(k.s));
	h = checkerHashStep(h, reinterpret_cast<uintptr_t>(k.t));
	return static_cast<size_t>(h);
}

size_t Checker::AssignmentReducedKeyHash::operator()(const AssignmentReducedKey& k) const noexcept {
	uint64_t h = 1469598103934665603ull;
	h = checkerHashStep(h, static_cast<uint64_t>(k.id1));
	h = checkerHashStep(h, static_cast<uint64_t>(k.id2));
	return static_cast<size_t>(h);
}

size_t Checker::NarrowedTypeKeyHash::operator()(const NarrowedTypeKey& k) const noexcept {
	uint64_t h = 1469598103934665603ull;
	h = checkerHashStep(h, reinterpret_cast<uintptr_t>(k.t));
	h = checkerHashStep(h, reinterpret_cast<uintptr_t>(k.candidate));
	h = checkerHashStep(h, static_cast<uint64_t>(k.assumeTrue));
	h = checkerHashStep(h, static_cast<uint64_t>(k.checkDerived));
	return static_cast<size_t>(h);
}

size_t Checker::PropertiesTypesKeyHash::operator()(const PropertiesTypesKey& k) const noexcept {
	uint64_t h = 1469598103934665603ull;
	h = checkerHashStep(h, static_cast<uint64_t>(k.typeId));
	h = checkerHashStep(h, static_cast<uint64_t>(k.include));
	h = checkerHashStep(h, static_cast<uint64_t>(k.includeOrigin));
	return static_cast<size_t>(h);
}

// checker.go:14318
Symbol* Checker::newParameter(const std::string& name, Type* t) {
	Symbol* symbol = newSymbol(SymbolFlagsFunctionScopedVariable, name);
	valueSymbolLinks.Get(symbol)->resolvedType = t;
	return symbol;
}

// checker.go:14324
Symbol* Checker::newProperty(const std::string& name, Type* t) {
	Symbol* symbol = newSymbol(SymbolFlagsProperty, name);
	valueSymbolLinks.Get(symbol)->resolvedType = t;
	return symbol;
}
// (deduped: instantiateType defined in checker_instantiate.cpp)
// (deduped: instantiateTypes defined in its owning slice file)
// (deduped: isTypeDerivedFrom defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: isTypeRelatedTo defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: isTypeSubtypeOf defined in cpp/internal/checker/checker_relater.cpp)

// (deduped: inferFromIntraExpressionSites, getInferredType — inference slice,
// defined in cpp/internal/checker/checker_inference.cpp)
}  // namespace checker
}  // namespace tsc
