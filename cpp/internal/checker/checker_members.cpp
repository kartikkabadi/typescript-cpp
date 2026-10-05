// Port of tsc/internal/checker/checker.go — member-resolution slice.
// Covers checker.go:19186-20143 (getPropertiesOfType … getSignaturesOfSymbol)
// and checker.go:20987-22284 (resolveAnonymousTypeMembers … isConflictingPrivateProperty).
// Dep-stubbed cross-slice calls live at the bottom under the dep-stub banner.
#include "internal/checker/checker.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>

#include "internal/checker/mapper.h"
#include "internal/jsnum/jsnum.h"

namespace tsc {
namespace checker {

// ---------------------------------------------------------------------------
// Free helpers defined in checker.cpp (extern linkage, no header home).
// ---------------------------------------------------------------------------

void forEachType(Type* t, const std::function<void(Type*)>& f);
bool someType(Type* t, const std::function<bool(Type*)>& f);
bool everyType(Type* t, const std::function<bool(Type*)>& f);
bool maybeTypeOfKind(Type* t, TypeFlags flags);
bool containsType(const std::vector<Type*>& types, Type* t);
Diagnostic* NewDiagnosticChainForNode(Diagnostic* chain, Node* node,
	const DiagnosticMessage* message, const std::vector<std::string>& args = {});

// ---------------------------------------------------------------------------
// Dep-stub declarations for free functions owned by other slices.
// Bodies live at the bottom of this file under the dep-stub banner.
// ---------------------------------------------------------------------------

bool isLateBoundName(const std::string& name);
bool IsKnownSymbol(Symbol* symbol);
bool isObjectLiteralType(Type* t);
bool isTupleType(Type* t);
bool hasReadonlyModifier(Node* node);
bool isStaticPrivateIdentifierProperty(Symbol* s);
ModifierFlags getDeclarationModifierFlagsFromSymbol(Symbol* s);
ModifierFlags getDeclarationModifierFlagsFromSymbolEx(Symbol* s, bool isWrite);
MappedTypeModifiers getMappedTypeModifiers(Type* t);
Ternary compareTypesEqual(Type* s, Type* t);

namespace {

// ---------------------------------------------------------------------------
// keyBuilder — replica of the static helper in checker.cpp (file-local there).
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

CacheKey getTypeListKey(const std::vector<Type*>& types) {
	keyBuilder b;
	b.writeTypes(types);
	return b.hash();
}

// ---------------------------------------------------------------------------
// Type.Target() / Type.Mapper() — types.go accessors, not yet methods in C++.
// ---------------------------------------------------------------------------

Type* typeTarget(Type* t) {
	if (t->flags & TypeFlagsObject) {
		return t->AsObjectType()->target;
	}
	if (t->flags & TypeFlagsTypeParameter) {
		return t->AsTypeParameter()->target;
	}
	if (t->flags & TypeFlagsIndex) {
		return t->AsIndexType()->target;
	}
	if (t->flags & TypeFlagsStringMapping) {
		return t->AsStringMappingType()->target;
	}
	TSC_UNREACHABLE("Unhandled case in Type.Target");
}

TypeMapper* typeMapperOf(Type* t) {
	if (t->flags & TypeFlagsObject) {
		return t->AsObjectType()->mapper;
	}
	if (t->flags & TypeFlagsTypeParameter) {
		return t->AsTypeParameter()->mapper;
	}
	if (t->flags & TypeFlagsConditional) {
		return t->AsConditionalType()->mapper;
	}
	TSC_UNREACHABLE("Unhandled case in Type.Mapper");
}

// StructuredType.CallSignatures() / ConstructSignatures() — views over
// signatures split at callSignatureCount.
std::vector<Signature*> callSignaturesOf(const StructuredType* t) {
	return std::vector<Signature*>(t->signatures.begin(),
		t->signatures.begin() + t->callSignatureCount);
}
std::vector<Signature*> constructSignaturesOf(const StructuredType* t) {
	return std::vector<Signature*>(t->signatures.begin() + t->callSignatureCount,
		t->signatures.end());
}

// ast.GetClassLikeDeclarationOfSymbol — not yet ported in internal/ast.
Node* getClassLikeDeclarationOfSymbol(Symbol* symbol) {
	for (Node* d : symbol->declarations) {
		if (isClassLike(d)) {
			return d;
		}
	}
	return nullptr;
}

// slices.Collect(maps.Values(table)).
std::vector<Symbol*> symbolTableValues(const SymbolTable& table) {
	std::vector<Symbol*> result;
	result.reserve(table.size());
	for (const auto& kv : table) {
		result.push_back(kv.second);
	}
	return result;
}

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

template <class T>
T* lastOrNil(const std::vector<T*>& v) {
	return v.empty() ? nullptr : v.back();
}

// core.OrElse for pointer-like types.
template <class T>
T orElse(T a, T b) {
	return a != nullptr ? a : b;
}

// core.FirstNonNil — first non-null mapped value.
template <class T, class F>
auto firstNonNil(const std::vector<T>& v, F&& f)
	-> decltype(f(std::declval<T>())) {
	for (const T& x : v) {
		auto r = f(x);
		if (r != nullptr) {
			return r;
		}
	}
	return nullptr;
}

// core.MapNonNil — map dropping null results.
template <class T, class F>
auto mapNonNil(const std::vector<T>& v, F&& f)
	-> std::vector<decltype(f(std::declval<T>()))> {
	std::vector<decltype(f(std::declval<T>()))> result;
	for (const T& x : v) {
		auto r = f(x);
		if (r != nullptr) {
			result.push_back(r);
		}
	}
	return result;
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

template <class T>
void appendIfUnique(std::vector<T>& v, T x) {
	if (std::find(v.begin(), v.end(), x) == v.end()) {
		v.push_back(x);
	}
}

// collections.OrderedMap[string, int] used by somePropertyReducesToNever.
struct orderedStringIntMap {
	std::vector<std::pair<std::string, int>> entries;
	std::unordered_map<std::string, size_t> index;

	void add(const std::string& key) {
		auto it = index.find(key);
		if (it == index.end()) {
			index.emplace(key, entries.size());
			entries.emplace_back(key, 1);
		} else {
			entries[it->second].second++;
		}
	}
};

// ---------------------------------------------------------------------------
// Static helpers replicated from checker.cpp / utilities.go.
// ---------------------------------------------------------------------------

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

} // anonymous namespace

// ---------------------------------------------------------------------------
// Free functions ported from checker.go (package-level helpers). Kept in a
// named namespace so member functions can call them explicitly — several names
// collide with Checker methods (getTargetType, findIndexInfo).
// ---------------------------------------------------------------------------

namespace members_detail {

IndexInfo* findIndexInfo(const std::vector<IndexInfo*>& indexInfos, Type* keyType) {
	for (IndexInfo* info : indexInfos) {
		if (info->keyType == keyType) {
			return info;
		}
	}
	return nullptr;
}

// getBaseTypeNodeOfClass — checker.go:19604
Node* getBaseTypeNodeOfClass(Type* t) {
	Node* decl = getClassLikeDeclarationOfSymbol(t->symbol);
	if (decl != nullptr) {
		std::vector<Node*> heritageElements = getExtendsHeritageClauseElements(decl);
		if (!heritageElements.empty()) {
			return heritageElements[0];
		}
	}
	return nullptr;
}

// getTargetType — checker.go:19903
Type* getTargetType(Type* t) {
	if (t->objectFlags & ObjectFlagsReference) {
		return typeTarget(t);
	}
	return t;
}

// isThisless family — checker.go:21061-21117

bool isThislessVariableLikeDeclaration(Node* node);
bool isThislessType(Node* node);
bool isThislessFunctionLikeDeclaration(Node* node);
bool isThislessTypeParameter(Node* node);

// Returns true if the parameter or class/interface member given by the symbol is free of "this" references. The
// function may return false for symbols that are actually free of "this" references because it is not
// feasible to perform a complete analysis in all cases. In particular, property members with types
// inferred from their initializers and function members with inferred return types are conservatively
// assumed not to be free of "this" references.
bool isThisless(Symbol* symbol) {
	if (symbol->declarations.size() == 1) {
		Node* declaration = symbol->declarations[0];
		if (declaration != nullptr) {
			switch (declaration->kind) {
			case Kind::Parameter:
				return isThislessVariableLikeDeclaration(declaration);
			case Kind::PropertyDeclaration:
			case Kind::PropertySignature:
				return isThislessVariableLikeDeclaration(declaration);
			case Kind::MethodDeclaration:
			case Kind::MethodSignature:
			case Kind::Constructor:
			case Kind::GetAccessor:
			case Kind::SetAccessor:
				return isThislessFunctionLikeDeclaration(declaration);
			default:
				break;
			}
		}
	}
	return false;
}

// A variable-like declaration is free of this references if it has a type annotation
// that is thisless, or if it has no type annotation and no initializer (and is thus of type any).
bool isThislessVariableLikeDeclaration(Node* node) {
	Node* typeNode = node->type();
	if (typeNode != nullptr) {
		return isThislessType(typeNode);
	}
	return node->initializer() == nullptr;
}

// A type is free of this references if it's the any, string, number, boolean, symbol, or void keyword, a string
// literal type, an array with an element type that is free of this references, or a type reference that is
// free of this references.
bool isThislessType(Node* node) {
	switch (node->kind) {
	case Kind::AnyKeyword:
	case Kind::UnknownKeyword:
	case Kind::StringKeyword:
	case Kind::NumberKeyword:
	case Kind::BigIntKeyword:
	case Kind::BooleanKeyword:
	case Kind::SymbolKeyword:
	case Kind::ObjectKeyword:
	case Kind::VoidKeyword:
	case Kind::UndefinedKeyword:
	case Kind::NeverKeyword:
	case Kind::LiteralType:
		return true;
	case Kind::ArrayType:
		return isThislessType(node->as<ArrayTypeNode>()->ElementType);
	case Kind::TypeReference:
		return everyList(node->typeArguments(), isThislessType);
	default:
		break;
	}
	return false;
}

// A function-like declaration is considered free of `this` references if it has a return type
// annotation that is free of this references and if each parameter is thisless and if
// each type parameter (if present) is thisless.
bool isThislessFunctionLikeDeclaration(Node* node) {
	Node* returnType = node->type();
	return (isConstructorDeclaration(node) || (returnType != nullptr && isThislessType(returnType))) &&
		everyList(node->parameters(), isThislessVariableLikeDeclaration) &&
		everyList(node->typeParameters(), isThislessTypeParameter);
}

// A type parameter is thisless if its constraint is thisless, or if it has no constraint. */
bool isThislessTypeParameter(Node* node) {
	Node* constraint = node->as<TypeParameterDeclaration>()->Constraint;
	return constraint == nullptr || isThislessType(constraint);
}

// Return whether this symbol is a member of a prototype somewhere
// Note that this is not tracked well within the compiler, so the answer may be incorrect.
bool isPrototypeProperty(Symbol* symbol) {
	return (symbol->flags & SymbolFlagsMethod) != 0 ||
		(symbol->checkFlags & CheckFlagsSyntheticMethod) != 0;
}

// Return true for a synthetic property with multiple declarations, at least one of which is private.
bool isConflictingPrivateProperty(Symbol* prop) {
	return prop->valueDeclaration == nullptr &&
		(prop->checkFlags & CheckFlagsContainsPrivate) != 0;
}

} // namespace members_detail

// ---------------------------------------------------------------------------
// Property lookup — checker.go:19186-19397
// ---------------------------------------------------------------------------

std::vector<Signature*> Checker::getSignaturesOfType(Type* t, SignatureKind kind) {
	return getSignaturesOfStructuredType(getReducedApparentType(t), kind);
}

std::vector<Symbol*> Checker::getPropertiesOfType(Type* t) {
	t = getReducedApparentType(t);
	if (t->flags & TypeFlagsUnionOrIntersection) {
		return getPropertiesOfUnionOrIntersectionType(t);
	}
	return getPropertiesOfObjectType(t);
}

std::vector<Symbol*> Checker::getPropertiesOfObjectType(Type* t) {
	if (t->flags & TypeFlagsObject) {
		return resolveStructuredTypeMembers(t)->properties;
	}
	return {};
}

std::vector<Symbol*> Checker::getPropertiesOfUnionOrIntersectionType(Type* t) {
	UnionOrIntersectionType* d = t->AsUnionOrIntersectionType();
	if (d->resolvedProperties.empty()) {
		std::unordered_set<std::string> checked;
		std::vector<Symbol*> props;
		for (Type* current : d->types) {
			for (Symbol* prop : getPropertiesOfType(current)) {
				if (!checked.count(prop->name)) {
					checked.insert(prop->name);
					Symbol* combinedProp = getPropertyOfUnionOrIntersectionType(t, prop->name,
						(t->flags & TypeFlagsIntersection) != 0 /*skipObjectFunctionPropertyAugment*/);
					if (combinedProp != nullptr) {
						props.push_back(combinedProp);
					}
				}
			}
			// The properties of a union type are those that are present in all constituent types, so
			// we only need to check the properties of the first type without index signature
			if ((t->flags & TypeFlagsUnion) && getIndexInfosOfType(current).empty()) {
				break;
			}
		}
		d->resolvedProperties = props;
	}
	return d->resolvedProperties;
}

Symbol* Checker::getPropertyOfTypeEx(Type* t, const std::string& name,
	bool skipObjectFunctionPropertyAugment, bool includeTypeOnlyMembers) {
	t = getReducedApparentType(t);
	if (t->flags & TypeFlagsObject) {
		StructuredType* resolved = resolveStructuredTypeMembers(t);
		auto it = resolved->members.find(name);
		Symbol* symbol = it != resolved->members.end() ? it->second : nullptr;
		if (symbol != nullptr) {
			if (!includeTypeOnlyMembers && t->symbol != nullptr &&
				(t->symbol->flags & SymbolFlagsValueModule) != 0 &&
				moduleSymbolLinks.Get(t->symbol)->typeOnlyExportStarMap.count(name) != 0) {
				// If this is the type of a module, `resolved.members.get(name)` might have effectively skipped over
				// an `export type * from './foo'`, leaving `symbolIsValue` unable to see that the symbol is being
				// viewed through a type-only export.
				return nullptr;
			}
			if (symbolIsValueEx(symbol, includeTypeOnlyMembers)) {
				return symbol;
			}
		}
		if (skipObjectFunctionPropertyAugment) {
			return nullptr;
		}
		Type* functionType = nullptr;
		if (t == anyFunctionType) {
			functionType = globalFunctionType;
		} else if (!callSignaturesOf(resolved).empty()) {
			functionType = globalCallableFunctionType;
		} else if (!constructSignaturesOf(resolved).empty()) {
			functionType = globalNewableFunctionType;
		}
		if (functionType != nullptr) {
			symbol = getPropertyOfObjectType(functionType, name);
			if (symbol != nullptr) {
				return symbol;
			}
		}
		return getPropertyOfObjectType(globalObjectType, name);
	}
	if (t->flags & TypeFlagsIntersection) {
		Symbol* prop = getPropertyOfUnionOrIntersectionType(t, name,
			true /*skipObjectFunctionPropertyAugment*/);
		if (prop != nullptr) {
			return prop;
		}
		if (!skipObjectFunctionPropertyAugment) {
			return getPropertyOfUnionOrIntersectionType(t, name, skipObjectFunctionPropertyAugment);
		}
		return nullptr;
	}
	if (t->flags & TypeFlagsUnion) {
		return getPropertyOfUnionOrIntersectionType(t, name, skipObjectFunctionPropertyAugment);
	}
	return nullptr;
}

// Return the type of the given property in the given type, or nil if no such property exists
Type* Checker::getTypeOfPropertyOfType(Type* t, const std::string& name) {
	Symbol* prop = getPropertyOfType(t, name);
	if (prop != nullptr) {
		return getTypeOfSymbol(prop);
	}
	return nullptr;
}

std::vector<Signature*> Checker::getSignaturesOfStructuredType(Type* t, SignatureKind kind) {
	if (!(t->flags & TypeFlagsStructuredType)) {
		return {};
	}
	StructuredType* resolved = resolveStructuredTypeMembers(t);
	if (kind == SignatureKind::Call) {
		return callSignaturesOf(resolved);
	}
	return constructSignaturesOf(resolved);
}

std::vector<IndexInfo*> Checker::getIndexInfosOfType(Type* t) {
	return getIndexInfosOfStructuredType(getReducedApparentType(t));
}

std::vector<IndexInfo*> Checker::getIndexInfosOfStructuredType(Type* t) {
	if (t->flags & TypeFlagsStructuredType) {
		return resolveStructuredTypeMembers(t)->indexInfos;
	}
	return {};
}

// Return the indexing info of the given kind in the given type. Creates synthetic union index types when necessary and
// maps primitive types and type parameters are to their apparent types.
IndexInfo* Checker::getIndexInfoOfType(Type* t, Type* keyType) {
	return members_detail::findIndexInfo(getIndexInfosOfType(t), keyType);
}

// Return the index type of the given kind in the given type. Creates synthetic union index types when necessary and
// maps primitive types and type parameters are to their apparent types.
Type* Checker::getIndexTypeOfType(Type* t, Type* keyType) {
	IndexInfo* info = getIndexInfoOfType(t, keyType);
	if (info != nullptr) {
		return info->valueType;
	}
	return nullptr;
}

Type* Checker::getIndexTypeOfTypeEx(Type* t, Type* keyType, Type* defaultType) {
	if (Type* result = getIndexTypeOfType(t, keyType); result != nullptr) {
		return result;
	}
	return defaultType;
}

IndexInfo* Checker::getApplicableIndexInfo(Type* t, Type* keyType) {
	return findApplicableIndexInfo(getIndexInfosOfType(t), keyType);
}

IndexInfo* Checker::getApplicableIndexInfoForName(Type* t, const std::string& name) {
	if (isLateBoundName(name)) {
		return getApplicableIndexInfo(t, esSymbolType);
	}
	return getApplicableIndexInfo(t, getStringLiteralType(name));
}

IndexInfo* Checker::findApplicableIndexInfo(const std::vector<IndexInfo*>& indexInfos,
	Type* keyType) {
	// Index signatures for type 'string' are considered only when no other index signatures apply.
	IndexInfo* stringIndexInfo = nullptr;
	std::vector<IndexInfo*> applicableInfos;
	applicableInfos.reserve(8);
	for (IndexInfo* info : indexInfos) {
		if (info->keyType == stringType) {
			stringIndexInfo = info;
		} else if (isApplicableIndexType(keyType, info->keyType)) {
			applicableInfos.push_back(info);
		}
	}
	// When more than one index signature is applicable we create a synthetic IndexInfo. Instead of computing
	// the intersected key type, we just use unknownType for the key type as nothing actually depends on the
	// keyType property of the returned IndexInfo.
	switch (applicableInfos.size()) {
	case 0:
		if (stringIndexInfo != nullptr && isApplicableIndexType(keyType, stringType)) {
			return stringIndexInfo;
		}
		return nullptr;
	case 1:
		return applicableInfos[0];
	default: {
		bool isReadonly = true;
		std::vector<Type*> types(applicableInfos.size());
		for (size_t i = 0; i < applicableInfos.size(); i++) {
			types[i] = applicableInfos[i]->valueType;
			if (!applicableInfos[i]->isReadonly) {
				isReadonly = false;
			}
		}
		return newIndexInfo(unknownType, getIntersectionType(types), isReadonly, nullptr, {});
	}
	}
}

bool Checker::isApplicableIndexType(Type* source, Type* target) {
	// A 'string' index signature applies to types assignable to 'string' or 'number', and a 'number' index
	// signature applies to types assignable to 'number', `${number}` and numeric string literal types.
	return isTypeAssignableTo(source, target) ||
		(target == stringType && isTypeAssignableTo(source, numberType)) ||
		(target == numberType && (source == numericStringType ||
			((source->flags & TypeFlagsStringLiteral) && isNumericLiteralName(getStringLiteralValue(source)))));
}

// Return the symbol for the property with the given name in the given type. Creates synthetic union properties when
// necessary, maps primitive types and type parameters are to their apparent types, and augments with properties from
// Object and 'Function' interface types as needed.
Symbol* Checker::getPropertyOfType(Type* type, const std::string& name) {
	return getPropertyOfTypeEx(type, name,
		false /*skipObjectFunctionPropertyAugment*/, false /*includeTypeOnlyMembers*/);
}

// ---------------------------------------------------------------------------
// resolveStructuredTypeMembers — checker.go:19407-19495
// ---------------------------------------------------------------------------

StructuredType* Checker::resolveStructuredTypeMembers(Type* t) {
	if (!(t->objectFlags & ObjectFlagsMembersResolved)) {
		if (t->flags & TypeFlagsObject) {
			if (t->objectFlags & ObjectFlagsReference) {
				resolveTypeReferenceMembers(t);
			} else if (t->objectFlags & ObjectFlagsClassOrInterface) {
				resolveClassOrInterfaceMembers(t);
			} else if (t->objectFlags & ObjectFlagsReverseMapped) {
				resolveReverseMappedTypeMembers(t);
			} else if (t->objectFlags & ObjectFlagsAnonymous) {
				resolveAnonymousTypeMembers(t);
			} else if (t->objectFlags & ObjectFlagsMapped) {
				resolveMappedTypeMembers(t);
			} else {
				TSC_UNREACHABLE("Unhandled case in resolveStructuredTypeMembers");
			}
		} else if (t->flags & TypeFlagsUnion) {
			resolveUnionTypeMembers(t);
		} else if (t->flags & TypeFlagsIntersection) {
			resolveIntersectionTypeMembers(t);
		} else {
			TSC_UNREACHABLE("Unhandled case in resolveStructuredTypeMembers");
		}
	}
	return t->AsStructuredType();
}

void Checker::resolveClassOrInterfaceMembers(Type* t) {
	resolveObjectTypeMembers(t, t, {}, {});
}

void Checker::resolveTypeReferenceMembers(Type* t) {
	Type* source = typeTarget(t);
	const std::vector<Type*>& typeParameters = source->AsInterfaceType()->allTypeParameters;
	std::vector<Type*> typeArguments = getTypeArguments(t);
	std::vector<Type*> paddedTypeArguments = typeArguments;
	if (typeArguments.size() == typeParameters.size() - 1) {
		paddedTypeArguments = concatenate(typeArguments, {t});
	}
	resolveObjectTypeMembers(t, source, typeParameters, paddedTypeArguments);
}

void Checker::resolveObjectTypeMembers(Type* t, Type* source,
	const std::vector<Type*>& typeParameters, const std::vector<Type*>& typeArguments) {
	TypeMapper* mapper = nullptr;
	SymbolTable members;
	std::vector<Signature*> callSignatures;
	std::vector<Signature*> constructSignatures;
	std::vector<IndexInfo*> indexInfos;
	bool instantiated = false;
	InterfaceType* resolved = resolveDeclaredMembers(source);
	if (typeParameters == typeArguments) {
		members = resolved->declaredMembers;
		callSignatures = resolved->declaredCallSignatures;
		constructSignatures = resolved->declaredConstructSignatures;
		indexInfos = resolved->declaredIndexInfos;
	} else {
		instantiated = true;
		mapper = newTypeMapper(typeParameters, typeArguments);
		members = instantiateSymbolTable(resolved->declaredMembers, mapper);
		callSignatures = instantiateSignatures(resolved->declaredCallSignatures, mapper);
		constructSignatures = instantiateSignatures(resolved->declaredConstructSignatures, mapper);
		indexInfos = instantiateIndexInfos(resolved->declaredIndexInfos, mapper);
	}
	std::vector<Type*> baseTypes = getBaseTypes(source);
	if (!baseTypes.empty()) {
		if (!instantiated) {
			// maps.Clone — in C++ the SymbolTable assignment above already produced a copy.
			members = SymbolTable(members);
		}
		Type* thisArgument = lastOrNil(typeArguments);
		for (Type* baseType : baseTypes) {
			Type* instantiatedBaseType = baseType;
			if (thisArgument != nullptr) {
				instantiatedBaseType = getTypeWithThisArgument(instantiateType(baseType, mapper),
					thisArgument, false /*needsApparentType*/);
			}
			members = addInheritedMembers(members, getPropertiesOfType(instantiatedBaseType));
			callSignatures = concatenate(callSignatures,
				getSignaturesOfType(instantiatedBaseType, SignatureKind::Call));
			constructSignatures = concatenate(constructSignatures,
				getSignaturesOfType(instantiatedBaseType, SignatureKind::Construct));
			std::vector<IndexInfo*> inheritedIndexInfos;
			if (instantiatedBaseType != anyType) {
				inheritedIndexInfos = getIndexInfosOfType(instantiatedBaseType);
			} else {
				inheritedIndexInfos = {anyBaseTypeIndexInfo};
			}
			std::vector<IndexInfo*> filtered = filterVec(inheritedIndexInfos,
				[&](IndexInfo* info) {
					return members_detail::findIndexInfo(indexInfos, info->keyType) == nullptr;
				});
			indexInfos = concatenate(indexInfos, filtered);
		}
	}
	setStructuredTypeMembers(t, members, callSignatures, constructSignatures, indexInfos);
}

IndexInfo* Checker::findIndexInfo(const std::vector<IndexInfo*>& indexInfos, Type* keyType) {
	return members_detail::findIndexInfo(indexInfos, keyType);
}

// Return the list of base types of a class or interface. The return value is empty if the type is
// not a class or interface.
std::vector<Type*> Checker::getBaseTypes(Type* t) {
	if (!(t->objectFlags & (ObjectFlagsClassOrInterface | ObjectFlagsTuple))) {
		return {};
	}
	InterfaceType* data = t->AsInterfaceType();
	if (!data->baseTypesResolved) {
		if (!pushTypeResolution(t, TypeSystemPropertyName::ResolvedBaseTypes)) {
			return data->resolvedBaseTypes;
		}
		if (t->objectFlags & ObjectFlagsTuple) {
			data->resolvedBaseTypes = {getTupleBaseType(t)};
		} else if (t->symbol->flags & (SymbolFlagsClass | SymbolFlagsInterface)) {
			if (t->symbol->flags & SymbolFlagsClass) {
				resolveBaseTypesOfClass(t);
			}
			if (t->symbol->flags & SymbolFlagsInterface) {
				resolveBaseTypesOfInterface(t);
			}
		} else {
			TSC_UNREACHABLE("Unhandled case in getBaseTypes");
		}
		if (!popTypeResolution() && !t->symbol->declarations.empty()) {
			for (Node* declaration : t->symbol->declarations) {
				if (isClassDeclaration(declaration) || isInterfaceDeclaration(declaration)) {
					reportCircularBaseType(declaration, t);
				}
			}
		}
		// In general, base type resolution always precedes member resolution. A circularity in
		// member resolution is therefore indicated by the absence of the ObjectFlagsMembersResolved
		// flag on a resolved base type, so we mark such types to ensure we don't repeatedly report
		// the same circularity error.
		t->objectFlags &= ~ObjectFlagsMembersResolved;
		data->baseTypesResolved = true;
	}
	return data->resolvedBaseTypes;
}

Type* Checker::getTupleBaseType(Type* t) {
	std::vector<Type*> typeParameters =
		interfaceTypeTypeParameters(t->AsTupleType());
	const std::vector<TupleElementInfo>& elementInfos = t->AsTupleType()->elementInfos;
	std::vector<Type*> elementTypes(typeParameters.size());
	for (size_t i = 0; i < typeParameters.size(); i++) {
		Type* tp = typeParameters[i];
		if (elementInfos[i].flags & ElementFlagsVariadic) {
			elementTypes[i] = getIndexedAccessType(tp, numberType);
		} else {
			elementTypes[i] = tp;
		}
	}
	return createArrayTypeEx(getUnionType(elementTypes), t->AsTupleType()->readonly);
}

void Checker::resolveBaseTypesOfClass(Type* t) {
	Type* baseConstructorType = getApparentType(getBaseConstructorTypeOfClass(t));
	if (!(baseConstructorType->flags & (TypeFlagsObject | TypeFlagsIntersection | TypeFlagsAny))) {
		return;
	}
	Node* baseTypeNode = members_detail::getBaseTypeNodeOfClass(t);
	Type* baseType = nullptr;
	Type* originalBaseType = nullptr;
	if (baseConstructorType->symbol != nullptr) {
		originalBaseType = getDeclaredTypeOfSymbol(baseConstructorType->symbol);
	}
	if (baseConstructorType->symbol != nullptr &&
		(baseConstructorType->symbol->flags & SymbolFlagsClass) != 0 &&
		areAllOuterTypeParametersApplied(originalBaseType)) {
		// When base constructor type is a class with no captured type arguments we know that the constructors all have the same type parameters as the
		// class and all return the instance type of the class. There is no need for further checks and we can apply the
		// type arguments in the same manner as a type reference to get the same error reporting experience.
		baseType = getTypeFromClassOrInterfaceReference(baseTypeNode, baseConstructorType->symbol);
	} else if (baseConstructorType->flags & TypeFlagsAny) {
		baseType = baseConstructorType;
	} else {
		// The class derives from a "class-like" constructor function, check that we have at least one construct signature
		// with a matching number of type parameters and use the return type of the first instantiated signature. Elsewhere
		// we check that all instantiated signatures return the same type.
		std::vector<Signature*> constructors = getInstantiatedConstructorsForTypeArguments(
			baseConstructorType, baseTypeNode->typeArguments(), baseTypeNode);
		if (constructors.empty()) {
			error(baseTypeNode->expression(),
				No_base_constructor_has_the_specified_number_of_type_arguments);
			return;
		}
		baseType = getReturnTypeOfSignature(constructors[0]);
	}
	if (isErrorType(baseType)) {
		return;
	}
	Type* reducedBaseType = getReducedType(baseType);
	if (!isValidBaseType(reducedBaseType)) {
		Node* errorNode = baseTypeNode->expression();
		Diagnostic* diagnostic = elaborateNeverIntersection(nullptr, errorNode, baseType);
		diagnostic = NewDiagnosticChainForNode(diagnostic, errorNode,
			Base_constructor_return_type_0_is_not_an_object_type_or_intersection_of_object_types_with_statically_known_members,
			{TypeToString(reducedBaseType)});
		addDiagnostic(diagnostic);
		return;
	}
	if (t == reducedBaseType || hasBaseType(reducedBaseType, t)) {
		error(t->symbol->valueDeclaration,
			Type_0_recursively_references_itself_as_a_base_type, TypeToString(t));
		return;
	}
	t->AsInterfaceType()->resolvedBaseTypes = {reducedBaseType};
}

std::vector<Signature*> Checker::getInstantiatedConstructorsForTypeArguments(
	Type* t, const std::vector<Node*>& typeArgumentNodes, Node* location) {
	std::vector<Signature*> signatures =
		getConstructorsForTypeArguments(t, typeArgumentNodes, location);
	std::vector<Type*> typeArguments = mapVec(typeArgumentNodes,
		[this](Node* node) { return getTypeFromTypeNode(node); });
	return sameMap(signatures, [&](Signature* sig) {
		if (!sig->typeParameters.empty()) {
			return getSignatureInstantiation(sig, typeArguments, isInJSFile(location), {});
		}
		return sig;
	});
}

std::vector<Signature*> Checker::getConstructorsForTypeArguments(
	Type* t, const std::vector<Node*>& typeArgumentNodes, Node* location) {
	int typeArgCount = static_cast<int>(typeArgumentNodes.size());
	return filterVec(getSignaturesOfType(t, SignatureKind::Construct), [&](Signature* sig) {
		return typeArgCount >= getMinTypeArgumentCount(sig->typeParameters) &&
			typeArgCount <= static_cast<int>(sig->typeParameters.size());
	});
}

Signature* Checker::getSignatureInstantiation(Signature* sig,
	const std::vector<Type*>& typeArguments, bool isJavaScript,
	const std::vector<Type*>& inferredTypeParameters) {
	Signature* instantiatedSignature = getSignatureInstantiationWithoutFillingInTypeArguments(
		sig, fillMissingTypeArguments(typeArguments, sig->typeParameters,
			getMinTypeArgumentCount(sig->typeParameters), isJavaScript));
	if (!inferredTypeParameters.empty()) {
		Signature* returnSignature =
			getSingleCallOrConstructSignature(getReturnTypeOfSignature(instantiatedSignature));
		if (returnSignature != nullptr) {
			Signature* newReturnSignature = cloneSignature(returnSignature);
			newReturnSignature->typeParameters = inferredTypeParameters;
			Type* newReturnType = getOrCreateTypeFromSignature(newReturnSignature);
			newReturnType->AsObjectType()->mapper = instantiatedSignature->mapper;
			Signature* newInstantiatedSignature = cloneSignature(instantiatedSignature);
			newInstantiatedSignature->resolvedReturnType = newReturnType;
			return newInstantiatedSignature;
		}
	}
	return instantiatedSignature;
}

Signature* Checker::cloneSignature(Signature* sig) {
	Signature* result = newSignature(sig->flags & SignatureFlagsPropagatingFlags,
		sig->declaration, sig->typeParameters, sig->thisParameter, sig->parameters,
		nullptr, nullptr, sig->minArgumentCount);
	result->target = sig->target;
	result->mapper = sig->mapper;
	result->composite = sig->composite;
	return result;
}

Signature* Checker::getSignatureInstantiationWithoutFillingInTypeArguments(
	Signature* sig, const std::vector<Type*>& typeArguments) {
	CachedSignatureKey key{sig, getTypeListKey(typeArguments)};
	Signature* instantiation = nullptr;
	if (auto it = cachedSignatures.find(key); it != cachedSignatures.end()) {
		instantiation = it->second;
	}
	if (instantiation == nullptr) {
		instantiation = createSignatureInstantiation(sig, typeArguments);
		cachedSignatures[key] = instantiation;
	}
	return instantiation;
}

Signature* Checker::createSignatureInstantiation(Signature* sig,
	const std::vector<Type*>& typeArguments) {
	return instantiateSignatureEx(sig, createSignatureTypeMapper(sig, typeArguments),
		true /*eraseTypeParameters*/);
}

TypeMapper* Checker::createSignatureTypeMapper(Signature* sig,
	const std::vector<Type*>& typeArguments) {
	return newTypeMapper(getTypeParametersForMapper(sig), typeArguments);
}

std::vector<Type*> Checker::getTypeParametersForMapper(Signature* sig) {
	return sameMap(sig->typeParameters,
		[this](Type* tp) { return instantiateType(tp, typeMapperOf(tp)); });
}

// If type has a single call signature and no other members, return that signature. Otherwise, return nil.
Signature* Checker::getSingleCallSignature(Type* t) {
	return getSingleSignature(t, SignatureKind::Call, false /*allowMembers*/);
}

Signature* Checker::getSingleCallOrConstructSignature(Type* t) {
	Signature* callSig = getSingleSignature(t, SignatureKind::Call, false /*allowMembers*/);
	if (callSig != nullptr) {
		return callSig;
	}
	return getSingleSignature(t, SignatureKind::Construct, false /*allowMembers*/);
}

Signature* Checker::getSingleSignature(Type* t, SignatureKind kind, bool allowMembers) {
	if (t->flags & TypeFlagsObject) {
		StructuredType* resolved = resolveStructuredTypeMembers(t);
		if (allowMembers || (resolved->properties.empty() && resolved->indexInfos.empty())) {
			if (kind == SignatureKind::Call && callSignaturesOf(resolved).size() == 1 &&
				constructSignaturesOf(resolved).empty()) {
				return callSignaturesOf(resolved)[0];
			}
			if (kind == SignatureKind::Construct && constructSignaturesOf(resolved).size() == 1 &&
				callSignaturesOf(resolved).empty()) {
				return constructSignaturesOf(resolved)[0];
			}
		}
	}
	return nullptr;
}

Type* Checker::getOrCreateTypeFromSignature(Signature* sig) {
	// There are two ways to declare a construct signature, one is by declaring a class constructor
	// using the constructor keyword, and the other is declaring a bare construct signature in an
	// object type literal or interface (using the new keyword). Each way of declaring a constructor
	// will result in a different declaration kind.
	if (sig->isolatedSignatureType == nullptr) {
		Kind kind = Kind::Unknown;
		if (sig->declaration != nullptr) {
			kind = sig->declaration->kind;
		}
		// If declaration is undefined, it is likely to be the signature of the default constructor.
		bool isConstructor = kind == Kind::Unknown || kind == Kind::Constructor ||
			kind == Kind::ConstructSignature || kind == Kind::ConstructorType;

		Symbol* symbol = nullptr;
		if (sig->declaration != nullptr) {
			symbol = sig->declaration->symbol();
		}
		Type* t = newObjectType(ObjectFlagsAnonymous | ObjectFlagsSingleSignatureType, symbol);
		if (isConstructor) {
			setStructuredTypeMembers(t, {}, {}, {sig}, {});
		} else {
			setStructuredTypeMembers(t, {}, {sig}, {}, {});
		}
		sig->isolatedSignatureType = t;
	}
	return sig->isolatedSignatureType;
}

Signature* Checker::getErasedSignature(Signature* signature) {
	if (signature->typeParameters.empty()) {
		return signature;
	}
	CachedSignatureKey key{signature, SignatureKeyErased};
	Signature* erased = nullptr;
	if (auto it = cachedSignatures.find(key); it != cachedSignatures.end()) {
		erased = it->second;
	}
	if (erased == nullptr) {
		erased = instantiateSignatureEx(signature,
			newArrayToSingleTypeMapper(signature->typeParameters, anyType),
			true /*eraseTypeParameters*/);
		cachedSignatures[key] = erased;
	}
	return erased;
}

Signature* Checker::getCanonicalSignature(Signature* signature) {
	if (signature->typeParameters.empty()) {
		return signature;
	}
	CachedSignatureKey key{signature, SignatureKeyCanonical};
	Signature* canonical = nullptr;
	if (auto it = cachedSignatures.find(key); it != cachedSignatures.end()) {
		canonical = it->second;
	}
	if (canonical == nullptr) {
		canonical = createCanonicalSignature(signature);
		cachedSignatures[key] = canonical;
	}
	return canonical;
}

Signature* Checker::createCanonicalSignature(Signature* signature) {
	// Create an instantiation of the signature where each unconstrained type parameter is replaced with
	// its original. When a generic class or interface is instantiated, each generic method in the class or
	// interface is instantiated with a fresh set of cloned type parameters (which we need to handle scenarios
	// where different generations of the same type parameter are in scope). This leads to a lot of new type
	// identities, and potentially a lot of work comparing those identities, so here we create an instantiation
	// that uses the original type identities for all unconstrained type parameters.
	std::vector<Type*> typeArguments = mapVec(signature->typeParameters, [this](Type* tp) {
		if (typeTarget(tp) != nullptr && getConstraintOfTypeParameter(typeTarget(tp)) == nullptr) {
			return typeTarget(tp);
		}
		return tp;
	});
	return getSignatureInstantiation(signature, typeArguments,
		isInJSFile(signature->declaration), {} /*inferredTypeParameters*/);
}

Signature* Checker::getBaseSignature(Signature* signature) {
	const std::vector<Type*>& typeParameters = signature->typeParameters;
	if (typeParameters.empty()) {
		return signature;
	}
	CachedSignatureKey key{signature, SignatureKeyBase};
	if (auto it = cachedSignatures.find(key); it != cachedSignatures.end() &&
		it->second != nullptr) {
		return it->second;
	}
	TypeMapper* baseConstraintMapper = newTypeMapper(typeParameters,
		mapVec(typeParameters, [this](Type* tp) {
			return orElse(getConstraintOfTypeParameter(tp), unknownType);
		}));
	std::vector<Type*> baseConstraints = mapVec(typeParameters, [&](Type* tp) {
		return instantiateType(tp, baseConstraintMapper);
	});
	// Run the immediate constraint mapper N-1 times so non-circular interdependent type parameters
	// resolve to their external dependencies without adding an extra expansion step for self-recursive constraints.
	for (size_t i = 0; i + 1 < typeParameters.size(); i++) {
		baseConstraints = instantiateTypes(baseConstraints, baseConstraintMapper);
	}
	// and then apply a type eraser to remove any remaining circularly dependent type parameters
	baseConstraints = instantiateTypes(baseConstraints,
		newArrayToSingleTypeMapper(typeParameters, anyType));
	Signature* result = instantiateSignatureEx(signature,
		newTypeMapper(typeParameters, baseConstraints), true /*eraseTypeParameters*/);
	cachedSignatures[key] = result;
	return result;
}

/**
 * Return a type or type parameter reference instance with the specified this type, or the
 * actual type or type parameter if no this type is specified. Note that the resulting type
 * is not an identical reference in the union case.
 */
Type* Checker::getTypeWithThisArgument(Type* t, Type* thisArgument,
									   bool needApparentType) {
	if (t->objectFlags & ObjectFlagsReference) {
		Type* target = typeTarget(t);
		std::vector<Type*> typeArguments = getTypeArguments(t);
		if (interfaceTypeTypeParameters(target->AsInterfaceType()).size() ==
			typeArguments.size()) {
			if (thisArgument == nullptr) {
				thisArgument = target->AsInterfaceType()->thisType;
			}
			return createTypeReference(target,
				concatenate(typeArguments, {thisArgument}));
		}
		return t;
	}
	if (t->flags & TypeFlagsIntersection) {
		const std::vector<Type*>& types = t->types();
		std::vector<Type*> newTypes = sameMap(types, [&](Type* u) {
			return getTypeWithThisArgument(u, thisArgument, needApparentType);
		});
		if (newTypes == types) {
			return t;
		}
		return getIntersectionType(newTypes);
	}
	if (needApparentType) {
		return getApparentType(t);
	}
	return t;
}

// Instantiate a generic signature in the context of a non-generic signature (section 3.8.5 in TypeScript spec)
Signature* Checker::instantiateSignatureInContextOf(Signature* signature,
	Signature* contextualSignature, InferenceContext* inferenceContext,
	TypeComparer compareTypes) {
	InferenceContext* context = newInferenceContext(getTypeParametersForMapper(signature),
		signature, InferenceFlagsNone, compareTypes);
	// We clone the inferenceContext to avoid fixing. For example, when the source signature is <T>(x: T) => T[] and
	// the contextual signature is (...args: A) => B, we want to infer the element type of A's constraint (say 'any')
	// for T but leave it possible to later infer '[any]' back to A.
	Type* restType = getEffectiveRestType(contextualSignature);
	TypeMapper* mapper = nullptr;
	if (inferenceContext != nullptr) {
		if (restType != nullptr && (restType->flags & TypeFlagsTypeParameter)) {
			mapper = inferenceContext->nonFixingMapper;
		} else {
			mapper = inferenceContext->mapper;
		}
	}
	Signature* sourceSignature;
	if (mapper != nullptr) {
		sourceSignature = instantiateSignature(contextualSignature, mapper);
	} else {
		sourceSignature = contextualSignature;
	}
	applyToParameterTypes(sourceSignature, signature, [&](Type* source, Type* target) {
		// Type parameters from outer context referenced by source type are fixed by instantiation of the source type
		inferTypes(context->inferences, source, target, InferencePriorityNone, false);
	});
	if (inferenceContext == nullptr) {
		applyToReturnTypes(contextualSignature, signature, [&](Type* source, Type* target) {
			inferTypes(context->inferences, source, target, InferencePriorityReturnType, false);
		});
	}
	return getSignatureInstantiation(signature, getInferredTypes(context),
		isInJSFile(contextualSignature->declaration), {} /*inferredTypeParameters*/);
}

void Checker::resolveBaseTypesOfInterface(Type* t) {
	InterfaceType* data = t->AsInterfaceType();
	for (Node* declaration : t->symbol->declarations) {
		if (isInterfaceDeclaration(declaration)) {
			for (Node* node : getExtendsHeritageClauseElements(declaration)) {
				Type* baseType = getReducedType(getTypeFromTypeNode(node));
				if (!isErrorType(baseType)) {
					if (isValidBaseType(baseType)) {
						if (t != baseType && !hasBaseType(baseType, t)) {
							data->resolvedBaseTypes.push_back(baseType);
						} else {
							reportCircularBaseType(declaration, t);
						}
					} else {
						error(node,
							An_interface_can_only_extend_an_object_type_or_intersection_of_object_types_with_statically_known_members);
					}
				}
			}
		}
	}
}

bool Checker::areAllOuterTypeParametersApplied(Type* t) {
	// An unapplied type parameter has its symbol still the same as the matching argument symbol.
	// Since parameters are applied outer-to-inner, only the last outer parameter needs to be checked.
	std::vector<Type*> outerTypeParameters =
		interfaceTypeOuterTypeParameters(t->AsInterfaceType());
	if (!outerTypeParameters.empty()) {
		size_t last = outerTypeParameters.size() - 1;
		std::vector<Type*> typeArguments = getTypeArguments(t);
		return outerTypeParameters[last]->symbol != typeArguments[last]->symbol;
	}
	return true;
}

void Checker::reportCircularBaseType(Node* node, Type* t) {
	error(node, Type_0_recursively_references_itself_as_a_base_type,
		TypeToStringEx(t, nullptr, TypeFormatFlagsWriteArrayAsGenericType, nullptr));
}

// A valid base type is `any`, an object type or intersection of object types.
bool Checker::isValidBaseType(Type* t) {
	if (t->flags & TypeFlagsTypeParameter) {
		Type* constraint = getBaseConstraintOfType(t);
		if (constraint != nullptr) {
			return isValidBaseType(constraint);
		}
	}
	// TODO: Given that we allow type parameters here now, is this `!isGenericMappedType(type)` check really needed?
	// There's no reason a `T` should be allowed while a `Readonly<T>` should not.
	return ((t->flags & (TypeFlagsObject | TypeFlagsNonPrimitive | TypeFlagsAny)) != 0 &&
			!isGenericMappedType(t)) ||
		((t->flags & TypeFlagsIntersection) != 0 &&
			everyList(t->types(), [this](Type* u) { return isValidBaseType(u); }));
}

// TODO: GH#18217 If `checkBase` is undefined, we should not call this because this will always return false.
bool Checker::hasBaseType(Type* t, Type* checkBase) {
	std::function<bool(Type*)> check = [&](Type* u) -> bool {
		if (u->objectFlags & (ObjectFlagsClassOrInterface | ObjectFlagsReference)) {
			Type* target = members_detail::getTargetType(u);
			return target == checkBase || someList(getBaseTypes(target), check);
		}
		if (u->flags & TypeFlagsIntersection) {
			return someList(u->types(), check);
		}
		return false;
	};
	return check(t);
}

SymbolTable Checker::addInheritedMembers(SymbolTable symbols,
	const std::vector<Symbol*>& baseSymbols) {
	for (Symbol* base : baseSymbols) {
		if (!isStaticPrivateIdentifierProperty(base)) {
			auto it = symbols.find(base->name);
			if (it == symbols.end() || !(it->second->flags & SymbolFlagsValue)) {
				symbols[base->name] = base;
			}
		}
	}
	return symbols;
}

InterfaceType* Checker::resolveDeclaredMembers(Type* t) {
	InterfaceType* d = t->AsInterfaceType();
	if (!d->declaredMembersResolved) {
		SymbolTable members = getMembersOfSymbol(t->symbol);
		d->declaredMembersResolved = true;
		d->declaredMembers = members;
		d->declaredCallSignatures =
			getSignaturesOfSymbol(d->declaredMembers[InternalSymbolNameCall]);
		d->declaredConstructSignatures =
			getSignaturesOfSymbol(d->declaredMembers[InternalSymbolNameNew]);
		d->declaredIndexInfos = getIndexInfosOfSymbol(t->symbol);
	}
	return d;
}

std::vector<IndexInfo*> Checker::getIndexInfosOfSymbol(Symbol* symbol) {
	Symbol* indexSymbol = getIndexSymbol(symbol);
	if (indexSymbol != nullptr) {
		return getIndexInfosOfIndexSymbol(indexSymbol,
			symbolTableValues(getMembersOfSymbol(symbol)));
	}
	return {};
}

// note intentional similarities to index signature building in `checkObjectLiteral` for parity
std::vector<IndexInfo*> Checker::getIndexInfosOfIndexSymbol(Symbol* indexSymbol,
	const std::vector<Symbol*>& siblingSymbols) {
	std::vector<IndexInfo*> indexInfos;
	bool hasComputedStringProperty = false;
	bool hasComputedNumberProperty = false;
	bool hasComputedSymbolProperty = false;
	bool readonlyComputedStringProperty = true;
	bool readonlyComputedNumberProperty = true;
	bool readonlyComputedSymbolProperty = true;
	std::vector<Symbol*> propertySymbols;
	for (Node* declaration : indexSymbol->declarations) {
		if (isIndexSignatureDeclaration(declaration)) {
			std::vector<Node*> parameters = declaration->parameters();
			Node* returnTypeNode = declaration->type();
			if (parameters.size() == 1) {
				Node* typeNode = parameters[0]->type();
				if (typeNode != nullptr) {
					Type* valueType = anyType;
					if (returnTypeNode != nullptr) {
						valueType = getTypeFromTypeNode(returnTypeNode);
					}
					forEachType(getTypeFromTypeNode(typeNode), [&](Type* keyType) {
						if (isValidIndexKeyType(keyType) &&
							members_detail::findIndexInfo(indexInfos, keyType) == nullptr) {
							IndexInfo* indexInfo = newIndexInfo(keyType, valueType,
								(declaration->modifierFlags() & ModifierFlagsReadonly) != 0,
								declaration, {});
							indexInfos.push_back(indexInfo);
						}
					});
				}
			}
		} else if (hasLateBindableIndexSignature(declaration)) {
			Node* declName;
			if (isBinaryExpression(declaration)) {
				declName = declaration->as<BinaryExpression>()->Left;
			} else {
				declName = declaration->name();
			}
			Type* keyType;
			if (isElementAccessExpression(declName)) {
				keyType = checkExpressionCached(
					declName->as<ElementAccessExpression>()->ArgumentExpression);
			} else {
				keyType = checkComputedPropertyName(declName);
			}
			if (members_detail::findIndexInfo(indexInfos, keyType) != nullptr) {
				continue;
				// Explicit index for key type takes priority
			}
			if (isTypeAssignableTo(keyType, stringNumberSymbolType)) {
				if (isTypeAssignableTo(keyType, numberType)) {
					hasComputedNumberProperty = true;
					if (!hasReadonlyModifier(declaration)) {
						readonlyComputedNumberProperty = false;
					}
				} else if (isTypeAssignableTo(keyType, esSymbolType)) {
					hasComputedSymbolProperty = true;
					if (!hasReadonlyModifier(declaration)) {
						readonlyComputedSymbolProperty = false;
					}
				} else {
					hasComputedStringProperty = true;
					if (!hasReadonlyModifier(declaration)) {
						readonlyComputedStringProperty = false;
					}
				}
				propertySymbols.push_back(declaration->symbol());
			}
		}
	}
	if (hasComputedStringProperty || hasComputedNumberProperty || hasComputedSymbolProperty) {
		for (Symbol* sym : siblingSymbols) {
			if (sym != indexSymbol) {
				propertySymbols.push_back(sym);
			}
		}
		// aggregate similar index infos implied to be the same key to the same combined index info
		if (hasComputedStringProperty &&
			members_detail::findIndexInfo(indexInfos, stringType) == nullptr) {
			indexInfos.push_back(getObjectLiteralIndexInfo(
				readonlyComputedStringProperty, propertySymbols, stringType));
		}
		if (hasComputedNumberProperty &&
			members_detail::findIndexInfo(indexInfos, numberType) == nullptr) {
			indexInfos.push_back(getObjectLiteralIndexInfo(
				readonlyComputedNumberProperty, propertySymbols, numberType));
		}
		if (hasComputedSymbolProperty &&
			members_detail::findIndexInfo(indexInfos, esSymbolType) == nullptr) {
			indexInfos.push_back(getObjectLiteralIndexInfo(
				readonlyComputedSymbolProperty, propertySymbols, esSymbolType));
		}
	}
	return indexInfos;
}

// NOTE: currently does not make pattern literal indexers, eg `${number}px`
IndexInfo* Checker::getObjectLiteralIndexInfo(bool isReadonly,
	const std::vector<Symbol*>& properties, Type* keyType) {
	std::vector<Type*> propTypes;
	std::vector<Node*> components;
	for (Symbol* prop : properties) {
		if ((keyType == stringType && !isSymbolWithSymbolName(prop)) ||
			(keyType == numberType && isSymbolWithNumericName(prop)) ||
			(keyType == esSymbolType && isSymbolWithSymbolName(prop))) {
			propTypes.push_back(getTypeOfSymbol(prop));
			if (isSymbolWithComputedName(prop)) {
				components.push_back(prop->declarations[0]);
			}
		}
	}
	Type* unionType = undefinedType;
	if (!propTypes.empty()) {
		unionType = getUnionTypeEx(propTypes, UnionReductionSubtype, nullptr, nullptr);
	}
	return newIndexInfo(keyType, unionType, isReadonly, nullptr /*declaration*/, components);
}

bool Checker::isSymbolWithSymbolName(Symbol* symbol) {
	if (IsKnownSymbol(symbol)) {
		return true;
	}
	if (!symbol->declarations.empty()) {
		Node* name = symbol->declarations[0]->name();
		return name != nullptr && isComputedPropertyName(name) &&
			isTypeAssignableToKind(checkComputedPropertyName(name), TypeFlagsESSymbol);
	}
	return false;
}

bool Checker::isSymbolWithNumericName(Symbol* symbol) {
	if (isNumericLiteralName(symbol->name)) {
		return true;
	}
	if (!symbol->declarations.empty()) {
		Node* name = symbol->declarations[0]->name();
		return name != nullptr && isNumericName(name);
	}
	return false;
}

bool Checker::isSymbolWithComputedName(Symbol* symbol) {
	if (!symbol->declarations.empty()) {
		Node* name = symbol->declarations[0]->name();
		return name != nullptr && isComputedPropertyName(name);
	}
	return false;
}

bool Checker::isNumericName(Node* name) {
	switch (name->kind) {
	case Kind::ComputedPropertyName:
		return isNumericComputedName(name);
	case Kind::Identifier:
	case Kind::NumericLiteral:
	case Kind::StringLiteral:
		return isNumericLiteralName(name->text());
	default:
		break;
	}
	return false;
}

bool Checker::isNumericComputedName(Node* name) {
	// It seems odd to consider an expression of type Any to result in a numeric name,
	// but this behavior is consistent with checkIndexedAccess
	return isTypeAssignableToKind(checkComputedPropertyName(name), TypeFlagsNumberLike);
}

bool Checker::isValidIndexKeyType(Type* t) {
	return (t->flags & (TypeFlagsString | TypeFlagsNumber | TypeFlagsESSymbol)) != 0 ||
		isPatternLiteralType(t) ||
		((t->flags & TypeFlagsIntersection) != 0 && !isGenericType(t) &&
			someList(t->types(), [this](Type* u) { return isValidIndexKeyType(u); }));
}

Symbol* Checker::getIndexSymbol(Symbol* symbol) {
	SymbolTable members = getMembersOfSymbol(symbol);
	auto it = members.find(InternalSymbolNameIndex);
	return it != members.end() ? it->second : nullptr;
}

std::vector<Signature*> Checker::getSignaturesOfSymbol(Symbol* symbol) {
	if (symbol == nullptr) {
		return {};
	}
	std::vector<Signature*> result;
	for (size_t i = 0; i < symbol->declarations.size(); i++) {
		Node* decl = symbol->declarations[i];
		if (!isFunctionLike(decl)) {
			continue;
		}
		// Don't include signature if node is the implementation of an overloaded function. A node is considered
		// an implementation node if it has a body and the previous node is of the same kind and immediately
		// precedes the implementation node (i.e. has the same parent and ends where the implementation starts).
		if (i > 0 && decl->body() != nullptr) {
			Node* previous = symbol->declarations[i - 1];
			if (decl->parent == previous->parent && decl->kind == previous->kind &&
				(decl->pos() == previous->end() || (previous->flags & NodeFlagsReparsed))) {
				continue;
			}
		}
		// If this is a function or method declaration, get the signature from the @type tag for the sake of optional parameters.
		// Exclude contextually-typed kinds because we already apply the @type tag to the context, plus applying it here to the initializer would suppress checks that the two are compatible.
		Signature* sig = getSignatureOfFullSignatureType(decl);
		if (sig == nullptr) {
			sig = getSignatureFromDeclaration(decl);
		}
		result.push_back(sig);
	}
	return result;
}

// ---------------------------------------------------------------------------
// resolveAnonymousTypeMembers — checker.go:20987-21061
// ---------------------------------------------------------------------------

void Checker::resolveAnonymousTypeMembers(Type* t) {
	ObjectType* d = t->AsObjectType();
	if (d->target != nullptr) {
		setStructuredTypeMembers(t, {}, {}, {}, {});
		SymbolTable members =
			createInstantiatedSymbolTable(getPropertiesOfObjectType(d->target), d->mapper);
		std::vector<Signature*> callSignatures = instantiateSignatures(
			getSignaturesOfType(d->target, SignatureKind::Call), d->mapper);
		std::vector<Signature*> constructSignatures = instantiateSignatures(
			getSignaturesOfType(d->target, SignatureKind::Construct), d->mapper);
		std::vector<IndexInfo*> indexInfos =
			instantiateIndexInfos(getIndexInfosOfType(d->target), d->mapper);
		setStructuredTypeMembers(t, members, callSignatures, constructSignatures, indexInfos);
		return;
	}
	Symbol* symbol = getMergedSymbol(t->symbol);
	if (symbol->flags & SymbolFlagsTypeLiteral) {
		setStructuredTypeMembers(t, {}, {}, {}, {});
		SymbolTable members = getMembersOfSymbol(symbol);
		std::vector<Signature*> callSignatures =
			getSignaturesOfSymbol(members[InternalSymbolNameCall]);
		std::vector<Signature*> constructSignatures =
			getSignaturesOfSymbol(members[InternalSymbolNameNew]);
		std::vector<IndexInfo*> indexInfos = getIndexInfosOfSymbol(symbol);
		setStructuredTypeMembers(t, members, callSignatures, constructSignatures, indexInfos);
		return;
	}
	// Combinations of function, class, enum and module
	SymbolTable members = getExportsOfSymbol(symbol);
	std::vector<IndexInfo*> indexInfos;
	if (symbol == globalThisSymbol) {
		SymbolTable varsOnly;
		for (const auto& kv : members) {
			Symbol* p = kv.second;
			if (!(p->flags & SymbolFlagsBlockScoped) &&
				!((p->flags & SymbolFlagsValueModule) && !p->declarations.empty() &&
					everyList(p->declarations, [](Node* n) { return isAmbientModule(n); }))) {
				varsOnly[p->name] = p;
			}
		}
		members = varsOnly;
	}
	IndexInfo* baseConstructorIndexInfo = nullptr;
	setStructuredTypeMembers(t, members, {}, {}, {});
	if (symbol->flags & SymbolFlagsClass) {
		Type* classType = getDeclaredTypeOfClassOrInterface(symbol);
		Type* baseConstructorType = getBaseConstructorTypeOfClass(classType);
		if (baseConstructorType->flags &
			(TypeFlagsObject | TypeFlagsIntersection | TypeFlagsTypeVariable)) {
			// maps.Clone — C++ SymbolTable is a value type; members is already a copy.
			members = SymbolTable(members);
			members = addInheritedMembers(members, getPropertiesOfType(baseConstructorType));
			setStructuredTypeMembers(t, members, {}, {}, {});
		} else if (baseConstructorType == anyType) {
			baseConstructorIndexInfo = anyBaseTypeIndexInfo;
		}
	}
	Symbol* indexSymbol = members[InternalSymbolNameIndex];
	if (indexSymbol != nullptr) {
		indexInfos = getIndexInfosOfIndexSymbol(indexSymbol, symbolTableValues(members));
	} else {
		if (baseConstructorIndexInfo != nullptr) {
			indexInfos.push_back(baseConstructorIndexInfo);
		}
		if ((symbol->flags & SymbolFlagsEnum) &&
			((getDeclaredTypeOfSymbol(symbol)->flags & TypeFlagsEnum) ||
				someList(d->properties, [this](Symbol* prop) {
					return (getTypeOfSymbol(prop)->flags & TypeFlagsNumberLike) != 0;
				}))) {
			indexInfos.push_back(enumNumberIndexInfo);
		}
	}
	d->indexInfos = indexInfos;
	// We resolve the members before computing the signatures because a signature may use
	// typeof with a qualified name expression that circularly references the type we are
	// in the process of resolving (see issue #6072). The temporarily empty signature list
	// will never be observed because a qualified name can't reference signatures.
	if (symbol->flags & (SymbolFlagsFunction | SymbolFlagsMethod)) {
		d->signatures = getSignaturesOfSymbol(symbol);
		d->callSignatureCount = static_cast<int32_t>(d->signatures.size());
	}
	// And likewise for construct signatures for classes
	if (symbol->flags & SymbolFlagsClass) {
		Type* classType = getDeclaredTypeOfClassOrInterface(symbol);
		std::vector<Signature*> constructSignatures =
			getSignaturesOfSymbol(symbol->members[InternalSymbolNameConstructor]);
		if (constructSignatures.empty()) {
			constructSignatures = getDefaultConstructSignatures(classType);
		}
		d->signatures.insert(d->signatures.end(), constructSignatures.begin(),
			constructSignatures.end());
	}
}

SymbolTable Checker::createInstantiatedSymbolTable(const std::vector<Symbol*>& symbols,
	TypeMapper* m) {
	if (symbols.empty()) {
		return {};
	}
	SymbolTable result;
	result.reserve(symbols.size());
	for (Symbol* symbol : symbols) {
		result[symbol->name] = instantiateSymbol(symbol, m);
	}
	return result;
}

SymbolTable Checker::instantiateSymbolTable(const SymbolTable& symbols, TypeMapper* m) {
	if (symbols.empty()) {
		return {};
	}
	SymbolTable result;
	result.reserve(symbols.size());
	for (const auto& kv : symbols) {
		const std::string& id = kv.first;
		Symbol* symbol = kv.second;
		if (isNamedMember(symbol, id)) {
			result[id] = instantiateSymbol(symbol, m);
		}
	}
	return result;
}

Symbol* Checker::instantiateSymbol(Symbol* symbol, TypeMapper* m) {
	if (symbol == nullptr) {
		return nullptr;
	}
	ValueSymbolLinks* links = valueSymbolLinks.Get(symbol);
	if (m != nullptr && m->mapsThisOnly() && members_detail::isThisless(symbol)) {
		return symbol;
	}
	// If the type of the symbol is already resolved, and if that type could not possibly
	// be affected by instantiation, simply return the symbol itself.
	if (links->resolvedType != nullptr && !couldContainTypeVariables(links->resolvedType)) {
		if (!(symbol->flags & SymbolFlagsSetAccessor)) {
			return symbol;
		}
		// If we're a setter, check writeType.
		if (links->writeType != nullptr && !couldContainTypeVariables(links->writeType)) {
			return symbol;
		}
	}
	if (symbol->checkFlags & CheckFlagsInstantiated) {
		// If symbol being instantiated is itself a instantiation, fetch the original target and combine the
		// type mappers. This ensures that original type identities are properly preserved and that aliases
		// always reference a non-aliases.
		symbol = links->target;
		m = combineTypeMappers(links->mapper, m);
	}
	// Keep the flags from the symbol we're instantiating.  Mark that is instantiated, and
	// also transient so that we can just store data on it directly.
	Symbol* result = newSymbol(symbol->flags, symbol->name);
	result->checkFlags = CheckFlagsInstantiated |
		(symbol->checkFlags & (CheckFlagsReadonly | CheckFlagsLate |
			CheckFlagsOptionalParameter | CheckFlagsRestParameter));
	result->declarations = symbol->declarations;
	result->parent = symbol->parent;
	result->valueDeclaration = symbol->valueDeclaration;
	ValueSymbolLinks* resultLinks = valueSymbolLinks.Get(result);
	resultLinks->target = symbol;
	resultLinks->mapper = m;
	resultLinks->nameType = links->nameType;
	return result;
}

std::vector<Signature*> Checker::getDefaultConstructSignatures(Type* classType) {
	Type* baseConstructorType = getBaseConstructorTypeOfClass(classType);
	std::vector<Signature*> baseSignatures =
		getSignaturesOfType(baseConstructorType, SignatureKind::Construct);
	Node* declaration = getClassLikeDeclarationOfSymbol(classType->symbol);
	bool isAbstract = declaration != nullptr &&
		hasSyntacticModifier(declaration, ModifierFlagsAbstract);
	if (baseSignatures.empty()) {
		SignatureFlags flags = isAbstract
			? (SignatureFlagsConstruct | SignatureFlagsAbstract)
			: SignatureFlagsConstruct;
		return {newSignature(flags, nullptr,
			interfaceTypeLocalTypeParameters(classType->AsInterfaceType()), nullptr, {},
			classType, nullptr, 0)};
	}
	Node* baseTypeNode = members_detail::getBaseTypeNodeOfClass(classType);
	bool isJavaScript = declaration != nullptr && isInJSFile(declaration);
	std::vector<Type*> typeArguments = getTypeArgumentsFromNode(baseTypeNode);
	int typeArgCount = static_cast<int>(typeArguments.size());
	std::vector<Signature*> result;
	for (Signature* baseSig : baseSignatures) {
		int minTypeArgumentCount = getMinTypeArgumentCount(baseSig->typeParameters);
		int typeParamCount = static_cast<int>(baseSig->typeParameters.size());
		if (isJavaScript ||
			(typeArgCount >= minTypeArgumentCount && typeArgCount <= typeParamCount)) {
			Signature* sig;
			if (typeParamCount != 0) {
				sig = createSignatureInstantiation(baseSig,
					fillMissingTypeArguments(typeArguments, baseSig->typeParameters,
						minTypeArgumentCount, isJavaScript));
			} else {
				sig = cloneSignature(baseSig);
			}
			sig->typeParameters =
				interfaceTypeLocalTypeParameters(classType->AsInterfaceType());
			sig->resolvedReturnType = classType;
			if (isAbstract) {
				sig->flags |= SignatureFlagsAbstract;
			} else {
				sig->flags &= ~SignatureFlagsAbstract;
			}
			result.push_back(sig);
		}
	}
	return result;
}

// ---------------------------------------------------------------------------
// Mapped type members — checker.go:21136-21368
// ---------------------------------------------------------------------------

void Checker::resolveMappedTypeMembers(Type* t) {
	SymbolTable members;
	std::vector<IndexInfo*> indexInfos;
	// Resolve upfront such that recursive references see an empty object type.
	setStructuredTypeMembers(t, {}, {}, {}, {});
	// In { [P in K]: T }, we refer to P as the type parameter type, K as the constraint type,
	// and T as the template type.
	Type* typeParameter = getTypeParameterFromMappedType(t);
	Type* constraintType = getConstraintTypeFromMappedType(t);
	Type* mappedType = t->AsMappedType()->target != nullptr ? t->AsMappedType()->target : t;
	Type* nameType = getNameTypeFromMappedType(mappedType);
	bool shouldLinkPropDeclarations =
		getMappedTypeNameTypeKind(mappedType) != MappedTypeNameTypeKind::Remapping;
	Type* templateType = getTemplateTypeFromMappedType(mappedType);
	Type* modifiersType = getApparentType(getModifiersTypeFromMappedType(t));
	// The 'T' in 'keyof T'
	MappedTypeModifiers templateModifiers = getMappedTypeModifiers(t);
	TypeFlags include = TypeFlagsStringOrNumberLiteralOrUnique;
	std::function<void(Type*, Type*)> addMemberForKeyTypeWorker =
		[&](Type* keyType, Type* propNameType) {
			// If the current iteration type constituent is a string literal type, create a property.
			// Otherwise, for type string create a string index signature.
			if (isTypeUsableAsPropertyName(propNameType)) {
				std::string propName = getPropertyNameFromType(propNameType);
				// String enum members from separate enums with identical values
				// are distinct types with the same property name. Make the resulting
				// property symbol's name type be the union of those enum member types.
				auto existingIt = members.find(propName);
				if (existingIt != members.end() && existingIt->second != nullptr) {
					Symbol* existingProp = existingIt->second;
					ValueSymbolLinks* valueLinks = valueSymbolLinks.Get(existingProp);
					valueLinks->nameType =
						getUnionType({valueLinks->nameType, propNameType});
					MappedSymbolLinks* mappedLinks = mappedSymbolLinks.Get(existingProp);
					mappedLinks->keyType =
						getUnionType({mappedLinks->keyType, keyType});
				} else {
					Symbol* modifiersProp = nullptr;
					if (isTypeUsableAsPropertyName(keyType)) {
						modifiersProp = getPropertyOfType(modifiersType,
							getPropertyNameFromType(keyType));
					}
					bool isOptional =
						(templateModifiers & MappedTypeModifiersIncludeOptional) != 0 ||
						((templateModifiers & MappedTypeModifiersExcludeOptional) == 0 &&
							modifiersProp != nullptr &&
							(modifiersProp->flags & SymbolFlagsOptional) != 0);
					bool isReadonly =
						(templateModifiers & MappedTypeModifiersIncludeReadonly) != 0 ||
						((templateModifiers & MappedTypeModifiersExcludeReadonly) == 0 &&
							modifiersProp != nullptr &&
							isReadonlySymbol(modifiersProp));
					bool stripOptional = strictNullChecks && !isOptional &&
						modifiersProp != nullptr &&
						(modifiersProp->flags & SymbolFlagsOptional) != 0;
					CheckFlags lateFlag = CheckFlagsNone;
					if (modifiersProp != nullptr) {
						lateFlag = modifiersProp->checkFlags & CheckFlagsLate;
					}
					Symbol* prop = newSymbol(
						SymbolFlagsProperty |
							ifElse(isOptional, SymbolFlagsOptional, SymbolFlagsNone),
						propName);
					prop->checkFlags = lateFlag | CheckFlagsMapped |
						ifElse(isReadonly, CheckFlagsReadonly, CheckFlagsNone) |
						ifElse(stripOptional, CheckFlagsStripOptional, CheckFlagsNone);
					ValueSymbolLinks* valueLinks = valueSymbolLinks.Get(prop);
					valueLinks->containingType = t;
					valueLinks->nameType = propNameType;
					MappedSymbolLinks* mappedLinks = mappedSymbolLinks.Get(prop);
					mappedLinks->keyType = keyType;
					if (modifiersProp != nullptr) {
						mappedLinks->syntheticOrigin = modifiersProp;
						if (shouldLinkPropDeclarations) {
							prop->declarations = modifiersProp->declarations;
						}
					}
					members[propName] = prop;
				}
			} else if (isValidIndexKeyType(propNameType) ||
				(propNameType->flags & (TypeFlagsAny | TypeFlagsEnum)) != 0) {
				Type* indexKeyType = propNameType;
				if (propNameType->flags & (TypeFlagsAny | TypeFlagsString)) {
					indexKeyType = stringType;
				} else if (propNameType->flags & (TypeFlagsNumber | TypeFlagsEnum)) {
					indexKeyType = numberType;
				}
				Type* propType = instantiateType(templateType,
					appendTypeMapping(t->AsMappedType()->mapper, typeParameter, keyType));
				IndexInfo* modifiersIndexInfo =
					getApplicableIndexInfo(modifiersType, propNameType);
				bool isReadonly =
					(templateModifiers & MappedTypeModifiersIncludeReadonly) != 0 ||
					((templateModifiers & MappedTypeModifiersExcludeReadonly) == 0 &&
						modifiersIndexInfo != nullptr && modifiersIndexInfo->isReadonly);
				IndexInfo* indexInfo =
					newIndexInfo(indexKeyType, propType, isReadonly, nullptr, {});
				indexInfos = appendIndexInfo(indexInfos, indexInfo, true /*union*/);
			}
		};
	std::function<void(Type*)> addMemberForKeyType = [&](Type* keyType) {
		Type* propNameType = keyType;
		if (nameType != nullptr) {
			propNameType = instantiateType(nameType,
				appendTypeMapping(t->AsMappedType()->mapper, typeParameter, keyType));
		}
		forEachType(propNameType, [&](Type* u) {
			addMemberForKeyTypeWorker(keyType, u);
		});
	};
	if (isMappedTypeWithKeyofConstraintDeclaration(t)) {
		// We have a { [P in keyof T]: X }
		forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType(modifiersType, include,
			false /*stringsOnly*/, addMemberForKeyType);
	} else {
		forEachType(getLowerBoundOfKeyType(constraintType), addMemberForKeyType);
	}
	setStructuredTypeMembers(t, members, {}, {}, indexInfos);
}

Type* Checker::getTypeOfMappedSymbol(Symbol* symbol) {
	ValueSymbolLinks* links = valueSymbolLinks.Get(symbol);
	if (links->resolvedType == nullptr) {
		Type* mappedType = links->containingType;
		if (!pushTypeResolution(symbol, TypeSystemPropertyName::Type)) {
			mappedType->AsMappedType()->containsError = true;
			return errorType;
		}
		Type* target = mappedType->AsMappedType()->target != nullptr
			? mappedType->AsMappedType()->target
			: mappedType;
		Type* templateType = getTemplateTypeFromMappedType(target);
		TypeMapper* mapper = appendTypeMapping(mappedType->AsMappedType()->mapper,
			getTypeParameterFromMappedType(mappedType),
			mappedSymbolLinks.Get(symbol)->keyType);
		Type* propType = instantiateType(templateType, mapper);
		// When creating an optional property in strictNullChecks mode, if 'undefined' isn't assignable to the
		// type, we include 'undefined' in the type. Similarly, when creating a non-optional property in strictNullChecks
		// mode, if the underlying property is optional we remove 'undefined' from the type.
		if (strictNullChecks && (symbol->flags & SymbolFlagsOptional) &&
			!maybeTypeOfKind(propType, TypeFlagsUndefined | TypeFlagsVoid)) {
			propType = getOptionalType(propType, true /*isProperty*/);
		} else if (symbol->checkFlags & CheckFlagsStripOptional) {
			propType = removeMissingOrUndefinedType(propType);
		}
		if (popTypeResolution()) {
			if (links->resolvedType == nullptr) {
				links->resolvedType = propType;
			}
		} else {
			if (links->resolvedType == nullptr) {
				links->resolvedType = errorType;
			}
			error(currentNode,
				Type_of_property_0_circularly_references_itself_in_mapped_type_1,
				{symbolToString(symbol), TypeToString(mappedType)});
		}
	}
	return links->resolvedType;
}

// Return the lower bound of the key type in a mapped type. Intuitively, the lower
// bound includes those keys that are known to always be present, for example because
// because of constraints on type parameters (e.g. 'keyof T' for a constrained T).
Type* Checker::getLowerBoundOfKeyType(Type* t) {
	if (t->flags & TypeFlagsIndex) {
		Type* target = getApparentType(t->AsIndexType()->target);
		if (isGenericTupleType(target)) {
			return getKnownKeysOfTupleType(target);
		}
		return getIndexType(target);
	}
	if (t->flags & TypeFlagsConditional) {
		if (t->AsConditionalType()->root->isDistributive) {
			Type* checkType = t->AsConditionalType()->checkType;
			Type* constraint = getLowerBoundOfKeyType(checkType);
			if (constraint != checkType) {
				return getConditionalTypeInstantiation(t,
					prependTypeMapping(t->AsConditionalType()->root->checkType,
						constraint, t->AsConditionalType()->mapper),
					false /*forConstraint*/, nullptr);
			}
		}
		return t;
	}
	if (t->flags & TypeFlagsUnion) {
		return mapTypeEx(t,
			[this](Type* u) { return getLowerBoundOfKeyType(u); },
			true /*noReductions*/);
	}
	if (t->flags & TypeFlagsIntersection) {
		// Similarly to getTypeFromIntersectionTypeNode, we preserve the special string & {}, number & {},
		// and bigint & {} intersections that are used to prevent subtype reduction in union types.
		const std::vector<Type*>& types = t->types();
		if (types.size() == 2 &&
			(types[0]->flags & (TypeFlagsString | TypeFlagsNumber | TypeFlagsBigInt)) != 0 &&
			types[1] == emptyTypeLiteralType) {
			return t;
		}
		return getIntersectionType(sameMap(types,
			[this](Type* u) { return getLowerBoundOfKeyType(u); }));
	}
	return t;
}

// ---------------------------------------------------------------------------
// Union/intersection members — checker.go:21369-21617
// ---------------------------------------------------------------------------

void Checker::resolveUnionTypeMembers(Type* t) {
	// The members and properties collections are empty for union types. To get all properties of a union
	// type use getPropertiesOfType (only the language service uses this).
	std::vector<Signature*> callSignatures =
		getUnionSignatures(mapVec(t->types(), [this](Type* u) -> std::vector<Signature*> {
			if (u == globalFunctionType) {
				return {unknownSignature};
			}
			return getSignaturesOfType(u, SignatureKind::Call);
		}));
	if (callSignatures.empty()) {
		callSignatures = getArrayMemberCallSignatures(t);
	}
	std::vector<Signature*> constructSignatures =
		getUnionSignatures(mapVec(t->types(), [this](Type* u) {
			return getSignaturesOfType(u, SignatureKind::Construct);
		}));
	std::vector<IndexInfo*> indexInfos = getUnionIndexInfos(t->types());
	setStructuredTypeMembers(t, {}, callSignatures, constructSignatures, indexInfos);
}

std::vector<Signature*> Checker::getArrayMemberCallSignatures(Type* t) {
	// Check if union is exclusively instantiations of a member of the global Array or ReadonlyArray type.
	std::string memberName;
	bool first = true;
	for (Type* u : t->types()) {
		if (!(u->objectFlags & ObjectFlagsInstantiated) || u->symbol == nullptr ||
			u->symbol->parent == nullptr || !isArrayOrTupleSymbol(u->symbol->parent)) {
			return {};
		}
		if (first) {
			memberName = u->symbol->name;
			first = false;
		} else if (memberName != u->symbol->name) {
			return {};
		}
	}
	// Transform the type from `(A[] | B[])["member"]` to `(A | B)[]["member"]` (since we pretend array is covariant anyway).
	Type* arrayArg = mapType(t, [this](Type* u) {
		return getMappedType(
			interfaceTypeTypeParameters(
				ifElse(isReadonlyArraySymbol(u->symbol->parent), globalReadonlyArrayType,
					globalArrayType)
					->AsInterfaceType())[0],
			typeMapperOf(u));
	});
	Type* arrayType = createArrayTypeEx(arrayArg, someType(t, [this](Type* u) {
		return isReadonlyArraySymbol(u->symbol->parent);
	}));
	return getSignaturesOfType(getTypeOfPropertyOfType(arrayType, memberName),
		SignatureKind::Call);
}

bool Checker::isArrayOrTupleSymbol(Symbol* symbol) {
	if (symbol == nullptr || globalArrayType->symbol == nullptr ||
		globalReadonlyArrayType->symbol == nullptr) {
		return false;
	}
	return getSymbolIfSameReference(symbol, globalArrayType->symbol) != nullptr ||
		getSymbolIfSameReference(symbol, globalReadonlyArrayType->symbol) != nullptr;
}

bool Checker::isReadonlyArraySymbol(Symbol* symbol) {
	if (symbol == nullptr || globalReadonlyArrayType->symbol == nullptr) {
		return false;
	}
	return getSymbolIfSameReference(symbol, globalReadonlyArrayType->symbol) != nullptr;
}

// The signatures of a union type are those signatures that are present in each of the constituent types.
// Generic signatures must match exactly, but non-generic signatures are allowed to have extra optional
// parameters and may differ in return types. When signatures differ in return types, the resulting return
// type is the union of the constituent return types.
std::vector<Signature*> Checker::getUnionSignatures(
	const std::vector<std::vector<Signature*>>& signatureLists) {
	std::vector<Signature*> result;
	int indexWithLengthOverOne = 0;
	int countLengthOverOne = 0;
	for (size_t i = 0; i < signatureLists.size(); i++) {
		if (signatureLists[i].empty()) {
			return {};
		}
		if (signatureLists[i].size() > 1) {
			indexWithLengthOverOne = static_cast<int>(i);
			countLengthOverOne++;
		}
		for (Signature* signature : signatureLists[i]) {
			// Only process signatures with parameter lists that aren't already in the result list
			if (result.empty() ||
				findMatchingSignature(result, signature, false /*partialMatch*/,
					false /*ignoreThisTypes*/, true /*ignoreReturnTypes*/) == nullptr) {
				std::vector<Signature*> unionSignatures =
					findMatchingSignatures(signatureLists, signature, static_cast<int>(i));
				if (!unionSignatures.empty()) {
					Signature* s = signature;
					// Union the result types when more than one signature matches
					if (unionSignatures.size() > 1) {
						Symbol* thisParameter = signature->thisParameter;
						Symbol* firstThisParameterOfUnionSignatures =
							firstNonNil(unionSignatures,
								[](Signature* sig) { return sig->thisParameter; });
						if (firstThisParameterOfUnionSignatures != nullptr) {
							Type* thisType = getIntersectionType(mapNonNil(
								unionSignatures, [this](Signature* sig) -> Type* {
									if (sig->thisParameter != nullptr) {
										return getTypeOfSymbol(sig->thisParameter);
									}
									return nullptr;
								}));
							thisParameter = createSymbolWithType(
								firstThisParameterOfUnionSignatures, thisType);
						}
						s = createUnionSignature(signature, unionSignatures);
						s->thisParameter = thisParameter;
					}
					result.push_back(s);
				}
			}
		}
	}
	if (result.empty() && countLengthOverOne <= 1) {
		// No sufficiently similar signature existed to subsume all the other signatures in the union - time to see if we can make a single
		// signature that handles all of them. We only do this when there are overloads in only one constituent. (Overloads are conditional in
		// nature and having overloads in multiple constituents would necessitate making a power set of signatures from the type, whose
		// ordering would be non-obvious)
		const std::vector<Signature*>& masterList =
			signatureLists[indexWithLengthOverOne];
		std::vector<Signature*> results = masterList; // slices.Clone
		for (const std::vector<Signature*>& signatures : signatureLists) {
			if (signatures != masterList) {
				Signature* signature = signatures[0];
				TSC_ASSERT(signature != nullptr,
					"getUnionSignatures bails early on empty signature lists and should not have empty lists on second pass");
				if (!signature->typeParameters.empty() &&
					someList(results, [&](Signature* s) {
						return !s->typeParameters.empty() &&
							!compareTypeParametersIdentical(signature->typeParameters,
								s->typeParameters);
					})) {
					results.clear(); // mirror Go's `results = nil`
				} else {
					results = mapVec(results, [&](Signature* sig) {
						return combineUnionOrIntersectionMemberSignatures(sig, signature,
							true /*isUnion*/);
					});
				}
				if (results.empty()) {
					break;
				}
			}
		}
		result = results;
	}
	return result;
}

Signature* Checker::combineUnionOrIntersectionMemberSignatures(Signature* left,
	Signature* right, bool isUnion) {
	std::vector<Type*> typeParams = left->typeParameters;
	if (typeParams.empty()) {
		typeParams = right->typeParameters;
	}
	TypeMapper* paramMapper = nullptr;
	if (!left->typeParameters.empty() && !right->typeParameters.empty()) {
		// We just use the type parameter defaults from the first signature
		paramMapper = newTypeMapper(right->typeParameters, left->typeParameters);
	}
	SignatureFlags flags =
		(left->flags | right->flags) &
		(SignatureFlagsPropagatingFlags & ~SignatureFlagsHasRestParameter);
	Node* declaration = left->declaration;
	std::vector<Symbol*> params =
		combineUnionOrIntersectionParameters(left, right, paramMapper, isUnion);
	Symbol* lastParam = lastOrNil(params);
	if (lastParam != nullptr && (lastParam->checkFlags & CheckFlagsRestParameter)) {
		flags |= SignatureFlagsHasRestParameter;
	}
	Symbol* thisParam = combineUnionOrIntersectionThisParam(left->thisParameter,
		right->thisParameter, paramMapper, isUnion);
	int minArgCount = std::max(left->minArgumentCount, right->minArgumentCount);
	Signature* result = newSignature(flags, declaration, typeParams, thisParam, params,
		nullptr, nullptr, minArgCount);
	std::vector<Signature*> leftSignatures;
	if (left->composite != nullptr && left->composite->isUnion) {
		leftSignatures = left->composite->signatures;
	} else {
		leftSignatures = {left};
	}
	CompositeSignature* composite = new CompositeSignature();
	composite->isUnion = isUnion;
	composite->signatures = leftSignatures;
	composite->signatures.push_back(right);
	result->composite = composite;
	if (paramMapper != nullptr) {
		if (left->composite != nullptr && left->composite->isUnion == isUnion &&
			left->mapper != nullptr) {
			result->mapper = combineTypeMappers(left->mapper, paramMapper);
		} else {
			result->mapper = paramMapper;
		}
	} else if (left->composite != nullptr && left->composite->isUnion == isUnion) {
		result->mapper = left->mapper;
	}
	return result;
}

std::vector<Symbol*> Checker::combineUnionOrIntersectionParameters(Signature* left,
	Signature* right, TypeMapper* mapper, bool isUnion) {
	int leftCount = getParameterCount(left);
	int rightCount = getParameterCount(right);
	int longestCount;
	Signature* longest;
	Signature* shorter;
	if (leftCount >= rightCount) {
		longestCount = leftCount;
		longest = left;
		shorter = right;
	} else {
		longestCount = rightCount;
		longest = right;
		shorter = left;
	}
	bool eitherHasEffectiveRest =
		hasEffectiveRestParameter(left) || hasEffectiveRestParameter(right);
	bool needsExtraRestElement =
		eitherHasEffectiveRest && !hasEffectiveRestParameter(longest);
	std::vector<Symbol*> params(
		longestCount + (needsExtraRestElement ? 1 : 0), nullptr);
	for (int i = 0; i < longestCount; i++) {
		Type* longestParamType = tryGetTypeAtPosition(longest, i);
		if (longest == right) {
			longestParamType = instantiateType(longestParamType, mapper);
		}
		Type* shorterParamType =
			orElse(tryGetTypeAtPosition(shorter, i), unknownType);
		if (shorter == right) {
			shorterParamType = instantiateType(shorterParamType, mapper);
		}
		Type* combinedParamType = getUnionOrIntersectionType(
			{longestParamType, shorterParamType}, !isUnion, UnionReductionLiteral);
		bool isRestParam =
			eitherHasEffectiveRest && !needsExtraRestElement && i == (longestCount - 1);
		bool isOptional = i >= getMinArgumentCount(longest) &&
			i >= getMinArgumentCount(shorter);
		std::string leftName;
		std::string rightName;
		if (i < leftCount) {
			leftName = getParameterNameAtPosition(left, i);
		}
		if (i < rightCount) {
			rightName = getParameterNameAtPosition(right, i);
		}
		std::string paramName;
		if (leftName == rightName) {
			paramName = leftName;
		} else if (leftName.empty()) {
			paramName = rightName;
		} else if (rightName.empty()) {
			paramName = leftName;
		}
		if (paramName.empty()) {
			paramName = "arg" + std::to_string(i);
		}
		Symbol* paramSymbol = newSymbolEx(
			SymbolFlagsFunctionScopedVariable |
				ifElse(isOptional && !isRestParam, SymbolFlagsOptional, SymbolFlagsNone),
			paramName,
			ifElse(isRestParam, CheckFlagsRestParameter,
				ifElse(isOptional, CheckFlagsOptionalParameter, CheckFlagsNone)));
		ValueSymbolLinks* links = valueSymbolLinks.Get(paramSymbol);
		if (isRestParam) {
			links->resolvedType = createArrayType(combinedParamType);
		} else {
			links->resolvedType = combinedParamType;
		}
		params[i] = paramSymbol;
	}
	if (needsExtraRestElement) {
		Symbol* restParamSymbol = newSymbolEx(SymbolFlagsFunctionScopedVariable, "args",
			CheckFlagsRestParameter);
		ValueSymbolLinks* links = valueSymbolLinks.Get(restParamSymbol);
		links->resolvedType =
			createArrayType(getTypeAtPosition(shorter, longestCount));
		if (shorter == right) {
			links->resolvedType = instantiateType(links->resolvedType, mapper);
		}
		params[longestCount] = restParamSymbol;
	}
	return params;
}

Symbol* Checker::combineUnionOrIntersectionThisParam(Symbol* left, Symbol* right,
	TypeMapper* mapper, bool isUnion) {
	if (left == nullptr) {
		return right;
	}
	if (right == nullptr) {
		return left;
	}
	// A signature `this` type might be a read or a write position... It's very possible that it should be invariant
	// and we should refuse to merge signatures if there are `this` types and they do not match. However, so as to be
	// permissive when calling, for now, we'll intersect the `this` types just like we do for param types in union signatures.
	Type* thisType = getUnionOrIntersectionType(
		{getTypeOfSymbol(left), instantiateType(getTypeOfSymbol(right), mapper)},
		!isUnion, UnionReductionLiteral);
	return createSymbolWithType(left, thisType);
}

void Checker::resolveIntersectionTypeMembers(Type* t) {
	// The members and properties collections are empty for intersection types. To get all properties of an
	// intersection type use getPropertiesOfType (only the language service uses this).
	std::vector<Signature*> callSignatures;
	std::vector<Signature*> constructSignatures;
	std::vector<IndexInfo*> indexInfos;
	const std::vector<Type*>& types = t->types();
	std::pair<std::vector<bool>, int> mixinResult = findMixins(types);
	const std::vector<bool>& mixinFlags = mixinResult.first;
	int mixinCount = mixinResult.second;
	for (size_t i = 0; i < types.size(); i++) {
		// When an intersection type contains mixin constructor types, the construct signatures from
		// those types are discarded and their return types are mixed into the return types of all
		// other construct signatures in the intersection type. For example, the intersection type
		// '{ new(...args: any[]) => A } & { new(s: string) => B }' has a single construct signature
		// 'new(s: string) => A & B'.
		if (!mixinFlags[i]) {
			std::vector<Signature*> signatures =
				getSignaturesOfType(types[i], SignatureKind::Construct);
			if (!signatures.empty() && mixinCount > 0) {
				signatures = mapVec(signatures, [&](Signature* s) {
					Signature* clone = cloneSignature(s);
					clone->resolvedReturnType = includeMixinType(
						getReturnTypeOfSignature(s), types, mixinFlags,
						static_cast<int>(i));
					return clone;
				});
			}
			constructSignatures = appendSignatures(constructSignatures, signatures);
		}
		callSignatures = appendSignatures(callSignatures,
			getSignaturesOfType(types[i], SignatureKind::Call));
		for (IndexInfo* info : getIndexInfosOfType(types[i])) {
			indexInfos = appendIndexInfo(indexInfos, info, false /*union*/);
		}
	}
	setStructuredTypeMembers(t, {}, callSignatures, constructSignatures, indexInfos);
}

std::vector<Signature*> Checker::appendSignatures(std::vector<Signature*> signatures,
	const std::vector<Signature*>& newSignatures) {
	for (Signature* sig : newSignatures) {
		if (signatures.empty() ||
			everyList(signatures, [&](Signature* s) {
				return compareSignaturesIdentical(s, sig, false /*partialMatch*/,
						   false /*ignoreThisTypes*/, false /*ignoreReturnTypes*/,
						   [this](Type* a, Type* b) {
							   return compareTypesIdentical(a, b);
						   }) == Ternary::False;
			})) {
			signatures.push_back(sig);
		}
	}
	return signatures;
}

std::vector<IndexInfo*> Checker::appendIndexInfo(std::vector<IndexInfo*> indexInfos,
	IndexInfo* newInfo, bool isUnion) {
	for (size_t i = 0; i < indexInfos.size(); i++) {
		if (indexInfos[i]->keyType == newInfo->keyType) {
			Type* valueType;
			bool isReadonly;
			if (isUnion) {
				valueType =
					getUnionType({indexInfos[i]->valueType, newInfo->valueType});
				isReadonly = indexInfos[i]->isReadonly || newInfo->isReadonly;
			} else {
				valueType =
					getIntersectionType({indexInfos[i]->valueType, newInfo->valueType});
				isReadonly = indexInfos[i]->isReadonly && newInfo->isReadonly;
			}
			indexInfos[i] = newIndexInfo(indexInfos[i]->keyType, valueType, isReadonly,
				nullptr, {});
			return indexInfos;
		}
	}
	indexInfos.push_back(newInfo);
	return indexInfos;
}

std::pair<std::vector<bool>, int> Checker::findMixins(
	const std::vector<Type*>& types) {
	std::vector<bool> mixinFlags = mapVec(types,
		[this](Type* u) { return isMixinConstructorType(u); });
	int constructorTypeCount = 0;
	int mixinCount = 0;
	int firstMixinIndex = -1;
	for (size_t i = 0; i < types.size(); i++) {
		if (!getSignaturesOfType(types[i], SignatureKind::Construct).empty()) {
			constructorTypeCount++;
		}
		if (mixinFlags[i]) {
			if (firstMixinIndex < 0) {
				firstMixinIndex = static_cast<int>(i);
			}
			mixinCount++;
		}
	}
	if (constructorTypeCount > 0 && constructorTypeCount == mixinCount) {
		mixinFlags[firstMixinIndex] = false;
		mixinCount--;
	}
	return {mixinFlags, mixinCount};
}

Type* Checker::includeMixinType(Type* t, const std::vector<Type*>& types,
	const std::vector<bool>& mixinFlags, int index) {
	std::vector<Type*> mixedTypes;
	for (size_t i = 0; i < types.size(); i++) {
		if (static_cast<int>(i) == index) {
			mixedTypes.push_back(t);
		} else if (mixinFlags[i]) {
			mixedTypes.push_back(getReturnTypeOfSignature(
				getSignaturesOfType(types[i], SignatureKind::Construct)[0]));
		}
	}
	return getIntersectionType(mixedTypes);
}

// ---------------------------------------------------------------------------
// getPropertyOfType family — checker.go:21618-22034
// ---------------------------------------------------------------------------

/**
 * If the given type is an object type and that type has a property by the given name,
 * return the symbol for that property. Otherwise return undefined.
 */
Symbol* Checker::getPropertyOfObjectType(Type* t, const std::string& name) {
	if (t->flags & TypeFlagsObject) {
		StructuredType* resolved = resolveStructuredTypeMembers(t);
		auto it = resolved->members.find(name);
		Symbol* symbol = it != resolved->members.end() ? it->second : nullptr;
		if (symbol != nullptr && symbolIsValue(symbol)) {
			return symbol;
		}
	}
	return nullptr;
}

Symbol* Checker::getPropertyOfUnionOrIntersectionType(Type* t, const std::string& name,
	bool skipObjectFunctionPropertyAugment) {
	Symbol* prop = getUnionOrIntersectionProperty(t, name, skipObjectFunctionPropertyAugment);
	// We need to filter out partial properties in union types
	if (prop != nullptr && (prop->checkFlags & CheckFlagsReadPartial)) {
		return nullptr;
	}
	return prop;
}

// Return the symbol for a given property in a union or intersection type, or undefined if the property
// does not exist in any constituent type. Note that the returned property may only be present in some
// constituents, in which case the isPartial flag is set when the containing type is union type. We need
// these partial properties when identifying discriminant properties, but otherwise they are filtered out
// and do not appear to be present in the union type.
Symbol* Checker::getUnionOrIntersectionProperty(Type* t, const std::string& name,
	bool skipObjectFunctionPropertyAugment) {
	UnionOrIntersectionType* d = t->AsUnionOrIntersectionType();
	SymbolTable& cache = skipObjectFunctionPropertyAugment
		? getSymbolTable(d->propertyCacheWithoutFunctionPropertyAugment)
		: getSymbolTable(d->propertyCache);
	if (Symbol* prop = cache[name]; prop != nullptr) {
		return prop;
	}
	Symbol* prop =
		createUnionOrIntersectionProperty(t, name, skipObjectFunctionPropertyAugment);
	if (prop != nullptr) {
		cache[name] = prop;
		// Propagate an entry from the non-augmented cache to the augmented cache unless the property is partial.
		if (skipObjectFunctionPropertyAugment &&
			!(prop->checkFlags & CheckFlagsPartial)) {
			SymbolTable& augmentedCache = getSymbolTable(d->propertyCache);
			if (augmentedCache[name] == nullptr) {
				augmentedCache[name] = prop;
			}
		}
	}
	return prop;
}

Symbol* Checker::createUnionOrIntersectionProperty(Type* containingType,
	const std::string& name, bool skipObjectFunctionPropertyAugment) {
	SymbolFlags propFlags = SymbolFlagsNone;
	Symbol* singleProp = nullptr;
	OrderedSet<Symbol*> propSet;
	std::vector<Type*> indexTypes;
	bool isUnion = (containingType->flags & TypeFlagsUnion) != 0;
	// Flags we want to propagate to the result if they exist in all source symbols
	CheckFlags checkFlags = CheckFlagsNone;
	SymbolFlags optionalFlag = SymbolFlagsNone;
	if (!isUnion) {
		checkFlags = CheckFlagsReadonly;
		optionalFlag = SymbolFlagsOptional;
	}
	CheckFlags syntheticFlag = CheckFlagsSyntheticMethod;
	bool mergedInstantiations = false;
	for (Type* current : containingType->types()) {
		Type* t = getApparentType(current);
		if (!isErrorType(t) && !(t->flags & TypeFlagsNever)) {
			Symbol* prop = getPropertyOfTypeEx(t, name,
				skipObjectFunctionPropertyAugment, false);
			ModifierFlags modifiers = ModifierFlagsNone;
			if (prop != nullptr) {
				modifiers = getDeclarationModifierFlagsFromSymbol(prop);
				ModifierFlags writeModifiers =
					getDeclarationModifierFlagsFromSymbolEx(prop, true /*isWrite*/);
				if (prop->flags & SymbolFlagsClassMember) {
					if (isUnion) {
						optionalFlag |= prop->flags & SymbolFlagsOptional;
					} else {
						optionalFlag &= prop->flags;
					}
				}
				if (singleProp == nullptr) {
					singleProp = prop;
					propFlags = (prop->flags & SymbolFlagsAccessor) != 0
						? (prop->flags & SymbolFlagsAccessor)
						: SymbolFlagsProperty;
				} else if (prop != singleProp) {
					bool isInstantiation =
						getTargetSymbol(prop) == getTargetSymbol(singleProp);
					// If the symbols are instances of one another with identical types - consider the symbols
					// equivalent and just use the first one, which thus allows us to avoid eliding private
					// members when intersecting a (this-)instantiations of a class with its raw base or another instance
					if (isInstantiation &&
						compareProperties(singleProp, prop,
							[](Type* s, Type* t2) {
								return compareTypesEqual(s, t2);
							}) == Ternary::True) {
						// If we merged instantiations of a generic type, we replicate the symbol parent resetting behavior we used
						// to do when we recorded multiple distinct symbols so that we still get, eg, `Array<T>.length` printed
						// back and not `Array<string>.length` when we're looking at a `.length` access on a `string[] | number[]`
						mergedInstantiations = singleProp->parent != nullptr &&
							!getLocalTypeParametersOfClassOrInterfaceOrTypeAlias(
								singleProp->parent)
								 .empty();
					} else {
						if (propSet.Size() == 0) {
							propSet.Add(singleProp);
						}
						propSet.Add(prop);
					}
					// classes created by mixins are represented as intersections
					// and overriding a property in a derived class redefines it completely at runtime
					// so a get accessor can't be merged with a set accessor in a base class,
					// for that reason the accessor flags are only used when they are the same in all constituents
					if ((propFlags & SymbolFlagsAccessor) != 0 &&
						(prop->flags & SymbolFlagsAccessor) !=
							(propFlags & SymbolFlagsAccessor)) {
						propFlags =
							(propFlags & ~SymbolFlagsAccessor) | SymbolFlagsProperty;
					}
				}
				if (isUnion && isReadonlySymbol(prop)) {
					checkFlags |= CheckFlagsReadonly;
				} else if (!isUnion && !isReadonlySymbol(prop)) {
					checkFlags &= ~CheckFlagsReadonly;
				}
				if ((modifiers & ModifierFlagsProtected) != 0 &&
					(modifiers & ModifierFlagsPublic) == 0) {
					checkFlags |= CheckFlagsContainsProtected;
				} else if ((modifiers & ModifierFlagsPrivate) != 0 &&
					(modifiers & ModifierFlagsPublic) == 0) {
					checkFlags |= CheckFlagsContainsPrivate;
				} else {
					checkFlags |= CheckFlagsContainsPublic;
				}
				if ((writeModifiers & ModifierFlagsProtected) != 0 &&
					(writeModifiers & ModifierFlagsPublic) == 0) {
					checkFlags |= CheckFlagsContainsWriteProtected;
				} else if ((writeModifiers & ModifierFlagsPrivate) != 0 &&
					(writeModifiers & ModifierFlagsPublic) == 0) {
					checkFlags |= CheckFlagsContainsWritePrivate;
				} else {
					checkFlags |= CheckFlagsContainsWritePublic;
				}
				if (modifiers & ModifierFlagsStatic) {
					checkFlags |= CheckFlagsContainsStatic;
				}
				if (!members_detail::isPrototypeProperty(prop)) {
					syntheticFlag = CheckFlagsSyntheticProperty;
				}
			} else if (isUnion) {
				IndexInfo* indexInfo = nullptr;
				if (!isLateBoundName(name)) {
					indexInfo = getApplicableIndexInfoForName(t, name);
				}
				if (indexInfo != nullptr) {
					propFlags =
						(propFlags & ~SymbolFlagsAccessor) | SymbolFlagsProperty;
					checkFlags |= CheckFlagsWritePartial |
						ifElse(indexInfo->isReadonly, CheckFlagsReadonly, CheckFlagsNone);
					if (isTupleType(t)) {
						Type* indexType = getRestTypeOfTupleType(t);
						if (indexType == nullptr) {
							indexType = undefinedType;
						}
						indexTypes.push_back(indexType);
					} else {
						indexTypes.push_back(indexInfo->valueType);
					}
				} else if (isObjectLiteralType(t) &&
					!(t->objectFlags & ObjectFlagsContainsSpread)) {
					checkFlags |= CheckFlagsWritePartial;
					indexTypes.push_back(undefinedType);
				} else {
					checkFlags |= CheckFlagsReadPartial;
				}
			}
		}
	}
	if (singleProp == nullptr) {
		// No property was found
		return nullptr;
	}
	if (isUnion && (propSet.Size() != 0 || (checkFlags & CheckFlagsPartial) != 0) &&
		(checkFlags & (CheckFlagsContainsPrivate | CheckFlagsContainsProtected |
						  CheckFlagsContainsWritePrivate |
						  CheckFlagsContainsWriteProtected)) != 0 &&
		!(propSet.Size() != 0 && hasCommonDeclaration(&propSet))) {
		// A property in a union has a private or protected declaration in one constituent, but is missing
		// or has a different declaration in another constituent. If the private or protected declaration is
		// for reading, we don't create a property.
		if (checkFlags & (CheckFlagsContainsPrivate | CheckFlagsContainsProtected)) {
			return nullptr;
		}
		// Otherwise, if the private or protected declaration is for writing, reduce accessibility to that of
		// the most restricted constituent.
		if (checkFlags & CheckFlagsContainsWritePrivate) {
			checkFlags &=
				~(CheckFlagsContainsWritePublic | CheckFlagsContainsWriteProtected);
		} else if (checkFlags & CheckFlagsContainsWriteProtected) {
			checkFlags &= ~CheckFlagsContainsWritePublic;
		}
	}
	if (propSet.Size() == 0 && !(checkFlags & CheckFlagsReadPartial) &&
		indexTypes.empty()) {
		if (!mergedInstantiations) {
			return singleProp;
		}
		// No symbol from a union/intersection should have a `.parent` set (since unions/intersections don't act as symbol parents)
		// Unless that parent is "reconstituted" from the "first value declaration" on the symbol (which is likely different than its instantiated parent!)
		// They also have a `.containingType` set, which affects some services endpoints behavior, like `getRootSymbol`
		Type* singlePropType = nullptr;
		TypeMapper* singlePropMapper = nullptr;
		if (singleProp->flags & SymbolFlagsTransient) {
			ValueSymbolLinks* links = valueSymbolLinks.Get(singleProp);
			singlePropType = links->resolvedType;
			singlePropMapper = links->mapper;
		}
		Symbol* clone = createSymbolWithType(singleProp, singlePropType);
		if (singleProp->valueDeclaration != nullptr) {
			clone->parent = singleProp->valueDeclaration->symbol()->parent;
		}
		ValueSymbolLinks* links = valueSymbolLinks.Get(clone);
		links->containingType = containingType;
		links->mapper = singlePropMapper;
		links->writeType = getWriteTypeOfSymbol(singleProp);
		return clone;
	}
	if (propSet.Size() == 0) {
		propSet.Add(singleProp);
	}
	std::vector<Node*> declarations;
	Type* firstType = nullptr;
	Type* nameType = nullptr;
	std::vector<Type*> propTypes;
	std::vector<Type*> writeTypes;
	bool hasWriteTypes = false;
	Node* firstValueDeclaration = nullptr;
	bool hasNonUniformValueDeclaration = false;
	for (Symbol* prop : propSet.items) {
		if (firstValueDeclaration == nullptr) {
			firstValueDeclaration = prop->valueDeclaration;
		} else if (prop->valueDeclaration != nullptr &&
			prop->valueDeclaration != firstValueDeclaration) {
			hasNonUniformValueDeclaration = true;
		}
		for (Node* declaration : prop->declarations) {
			appendIfUnique(declarations, declaration);
		}
		Type* t = getTypeOfSymbol(prop);
		if (firstType == nullptr) {
			firstType = t;
			nameType = valueSymbolLinks.Get(prop)->nameType;
		}
		Type* writeType = getWriteTypeOfSymbol(prop);
		if (hasWriteTypes || writeType != t) {
			if (!hasWriteTypes) {
				writeTypes = propTypes; // slices.Clone(propTypes)
				hasWriteTypes = true;
			}
			writeTypes.push_back(writeType);
		}
		if (t != firstType) {
			checkFlags |= CheckFlagsHasNonUniformType;
		}
		if (isLiteralType(t) || isPatternLiteralType(t)) {
			checkFlags |= CheckFlagsHasLiteralType;
		}
		if ((t->flags & TypeFlagsNever) != 0 && t != uniqueLiteralType) {
			checkFlags |= CheckFlagsHasNeverType;
		}
		propTypes.push_back(t);
	}
	propTypes.insert(propTypes.end(), indexTypes.begin(), indexTypes.end());
	Symbol* result = newSymbolEx(propFlags | optionalFlag, name,
		checkFlags | syntheticFlag);
	result->declarations = declarations;
	if (!hasNonUniformValueDeclaration && firstValueDeclaration != nullptr) {
		result->valueDeclaration = firstValueDeclaration;
		// Inherit information about parent type.
		result->parent = firstValueDeclaration->symbol()->parent;
	}
	ValueSymbolLinks* links = valueSymbolLinks.Get(result);
	links->containingType = containingType;
	links->nameType = nameType;
	if (propTypes.size() > 2) {
		// When `propTypes` has the potential to explode in size when normalized, defer normalization until absolutely needed
		result->checkFlags |= CheckFlagsDeferredType;
		DeferredSymbolLinks* deferred = deferredSymbolLinks.Get(result);
		deferred->parent = containingType;
		deferred->constituents = propTypes;
		deferred->writeConstituents = writeTypes;
		return result;
	}
	if (isUnion) {
		links->resolvedType = getUnionType(propTypes);
	} else {
		links->resolvedType = getIntersectionType(propTypes);
	}
	if (hasWriteTypes) {
		if (isUnion) {
			links->writeType = getUnionType(writeTypes);
		} else {
			links->writeType = getIntersectionType(writeTypes);
		}
	}
	return result;
}

Symbol* Checker::getTargetSymbol(Symbol* s) {
	// if symbol is instantiated its flags are not copied from the 'target'
	// so we'll need to get back original 'target' symbol to work with correct set of flags
	if (s != nullptr && (s->checkFlags & CheckFlagsInstantiated) != 0) {
		return valueSymbolLinks.Get(s)->target;
	}
	return s;
}

bool Checker::hasCommonDeclaration(OrderedSet<Symbol*>* symbols) {
	std::unordered_set<Node*> commonDeclarations;
	for (Symbol* symbol : symbols->items) {
		if (symbol->declarations.empty()) {
			return false;
		}
		if (commonDeclarations.empty()) {
			for (Node* d : symbol->declarations) {
				commonDeclarations.insert(d);
			}
			continue;
		}
		std::vector<Node*> keys(commonDeclarations.begin(), commonDeclarations.end());
		for (Node* d : keys) {
			if (std::find(symbol->declarations.begin(), symbol->declarations.end(),
					d) == symbol->declarations.end()) {
				commonDeclarations.erase(d);
			}
		}
		if (commonDeclarations.empty()) {
			return false;
		}
	}
	return !commonDeclarations.empty();
}

Symbol* Checker::createSymbolWithType(Symbol* source, Type* t) {
	Symbol* symbol =
		newSymbolEx(source->flags, source->name, source->checkFlags & CheckFlagsReadonly);
	symbol->declarations = source->declarations;
	symbol->parent = source->parent;
	symbol->valueDeclaration = source->valueDeclaration;
	ValueSymbolLinks* links = valueSymbolLinks.Get(symbol);
	links->resolvedType = t;
	links->target = source;
	links->nameType = valueSymbolLinks.Get(source)->nameType;
	return symbol;
}

bool Checker::isMappedTypeGenericIndexedAccess(Type* t) {
	if (t->flags & TypeFlagsIndexedAccess) {
		Type* objectType = t->AsIndexedAccessType()->objectType;
		return (objectType->objectFlags & ObjectFlagsMapped) != 0 &&
			!isGenericMappedType(objectType) &&
			isGenericIndexType(t->AsIndexedAccessType()->indexType) &&
			(getMappedTypeModifiers(objectType) &
				MappedTypeModifiersExcludeOptional) == 0 &&
			objectType->AsMappedType()->declaration->as<MappedTypeNode>()->NameType ==
				nullptr;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Apparent/reduced types — checker.go:22035-22284
// ---------------------------------------------------------------------------

/**
 * For a type parameter, return the base constraint of the type parameter. For the string, number,
 * boolean, and symbol primitive types, return the corresponding object types. Otherwise return the
 * type itself.
 */
Type* Checker::getApparentType(Type* t) {
	Type* originalType = t;
	if (t->flags & TypeFlagsInstantiable) {
		t = getBaseConstraintOfType(t);
		if (t == nullptr) {
			t = unknownType;
		}
	}
	if (t->objectFlags & ObjectFlagsMapped) {
		return getApparentTypeOfMappedType(t);
	}
	if ((t->objectFlags & ObjectFlagsReference) && t != originalType) {
		return getTypeWithThisArgument(t, originalType, false /*needsApparentType*/);
	}
	if (t->flags & TypeFlagsIntersection) {
		return getApparentTypeOfIntersectionType(t, originalType);
	}
	if (t->flags & TypeFlagsStringLike) {
		return globalStringType;
	}
	if (t->flags & TypeFlagsNumberLike) {
		return globalNumberType;
	}
	if (t->flags & TypeFlagsBigIntLike) {
		return getGlobalBigIntType();
	}
	if (t->flags & TypeFlagsBooleanLike) {
		return globalBooleanType;
	}
	if (t->flags & TypeFlagsESSymbolLike) {
		return getGlobalESSymbolType();
	}
	if (t->flags & TypeFlagsNonPrimitive) {
		return emptyObjectType;
	}
	if (t->flags & TypeFlagsIndex) {
		return stringNumberSymbolType;
	}
	if ((t->flags & TypeFlagsUnknown) && !strictNullChecks) {
		return emptyObjectType;
	}
	return t;
}

Type* Checker::getApparentTypeOfMappedType(Type* t) {
	MappedType* m = t->AsMappedType();
	if (m->resolvedApparentType == nullptr) {
		m->resolvedApparentType = getResolvedApparentTypeOfMappedType(t);
	}
	return m->resolvedApparentType;
}

Type* Checker::getResolvedApparentTypeOfMappedType(Type* t) {
	Type* target =
		t->AsMappedType()->target != nullptr ? t->AsMappedType()->target : t;
	Type* typeVariable = getHomomorphicTypeVariable(target);
	if (typeVariable != nullptr &&
		target->AsMappedType()->declaration->as<MappedTypeNode>()->NameType ==
			nullptr) {
		// We have a homomorphic mapped type or an instantiation of a homomorphic mapped type, i.e. a type
		// of the form { [P in keyof T]: X }. Obtain the modifiers type (the T of the keyof T), and if it is
		// another generic mapped type, recursively obtain its apparent type. Otherwise, obtain its base
		// constraint. Then, if every constituent of the base constraint is an array or tuple type, apply
		// this mapped type to the base constraint. It is safe to recurse when the modifiers type is a
		// mapped type because we protect again circular constraints in getTypeFromMappedTypeNode.
		Type* modifiersType = getModifiersTypeFromMappedType(t);
		Type* baseConstraint = nullptr;
		if (isGenericMappedType(modifiersType)) {
			baseConstraint = getApparentTypeOfMappedType(modifiersType);
		} else {
			baseConstraint = getBaseConstraintOfType(modifiersType);
		}
		if (baseConstraint != nullptr &&
			everyType(baseConstraint, [this](Type* u) {
				return isArrayOrTupleType(u) || isArrayOrTupleOrIntersection(u);
			})) {
			return instantiateType(target,
				prependTypeMapping(typeVariable, baseConstraint,
					t->AsMappedType()->mapper));
		}
	}
	return t;
}

Type* Checker::getApparentTypeOfIntersectionType(Type* t, Type* thisArgument) {
	if (t == thisArgument) {
		IntersectionType* d = t->AsIntersectionType();
		if (d->resolvedApparentType == nullptr) {
			d->resolvedApparentType =
				getTypeWithThisArgument(t, thisArgument, true /*needApparentType*/);
		}
		return d->resolvedApparentType;
	}
	CachedTypeKey key{CachedTypeKind::ApparentType, thisArgument->id};
	Type* result = nullptr;
	if (auto it = cachedTypes.find(key); it != cachedTypes.end()) {
		result = it->second;
	}
	if (result == nullptr) {
		result = getTypeWithThisArgument(t, thisArgument, true /*needApparentType*/);
		cachedTypes[key] = result;
	}
	return result;
}

/**
 * Return the reduced form of the given type. For a union type, it is a union of the normalized constituent types.
 * For an intersection of types containing one or more mututally exclusive discriminant properties, it is 'never'.
 * For all other types, it is simply the type itself. Discriminant properties are considered mutually exclusive when
 * no constituent property has type 'never', but the intersection of the constituent property types is 'never'.
 */
Type* Checker::getReducedType(Type* t) {
	if (t->flags & TypeFlagsUnion) {
		if (t->objectFlags & ObjectFlagsContainsIntersections) {
			if (Type* reducedType = t->AsUnionType()->resolvedReducedType;
				reducedType != nullptr) {
				return reducedType;
			}
			Type* reducedType = getReducedUnionType(t);
			t->AsUnionType()->resolvedReducedType = reducedType;
			return reducedType;
		}
	} else if (t->flags & TypeFlagsIntersection) {
		if (!(t->objectFlags & ObjectFlagsIsNeverIntersectionComputed)) {
			t->objectFlags |= ObjectFlagsIsNeverIntersectionComputed;
			if (!isMappingOfSameObjectType(t->types()) &&
				somePropertyReducesToNever(t)) {
				t->objectFlags |= ObjectFlagsIsNeverIntersection;
			}
		}
		if (t->objectFlags & ObjectFlagsIsNeverIntersection) {
			return neverType;
		}
	}
	return t;
}

bool Checker::isMappingOfSameObjectType(const std::vector<Type*>& types) {
	if (!types.empty() && (types[0]->objectFlags & ObjectFlagsMapped) != 0) {
		Type* firstType = getModifiersTypeFromMappedType(types[0]);
		if (firstType->flags & TypeFlagsObject) {
			for (size_t i = 1; i < types.size(); i++) {
				if (!(types[i]->objectFlags & ObjectFlagsMapped) ||
					getModifiersTypeFromMappedType(types[i]) != firstType) {
					return false;
				}
			}
			return true;
		}
	}
	return false;
}

bool Checker::somePropertyReducesToNever(Type* t) {
	// Collect declaration counts for each property across all constituent types of the intersection.
	orderedStringIntMap counts;
	for (Type* u : t->types()) {
		for (Symbol* prop : getPropertiesOfType(u)) {
			counts.add(prop->name);
		}
	}
	// Check if any property appears in more than one constituent type and reduces to 'never'.
	// Go in the order the properties were found so the combined properties are created in the same order every time.
	for (const auto& entry : counts.entries) {
		if (entry.second > 1) {
			if (Symbol* prop =
					getPropertyOfUnionOrIntersectionType(t, entry.first,
						true /*skipObjectFunctionPropertyAugment*/);
				prop != nullptr && isNeverReducedProperty(prop)) {
				return true;
			}
		}
	}
	return false;
}

Type* Checker::getReducedUnionType(Type* unionType) {
	std::vector<Type*> reducedTypes = sameMap(unionType->types(),
		[this](Type* u) { return getReducedType(u); });
	if (reducedTypes == unionType->types()) {
		return unionType;
	}
	Type* reduced = getUnionType(reducedTypes);
	if (reduced->flags & TypeFlagsUnion) {
		reduced->AsUnionType()->resolvedReducedType = reduced;
	}
	return reduced;
}

bool Checker::isNeverReducedProperty(Symbol* prop) {
	return isDiscriminantWithNeverType(prop) ||
		members_detail::isConflictingPrivateProperty(prop);
}

Type* Checker::getReducedApparentType(Type* t) {
	// Since getApparentType may return a non-reduced union or intersection type, we need to perform
	// type reduction both before and after obtaining the apparent type. For example, given a type parameter
	// 'T extends A | B', the type 'T & X' becomes 'A & X | B & X' after obtaining the apparent type, and
	// that type may need further reduction to remove empty intersections.
	return getReducedType(getApparentType(getReducedType(t)));
}

Diagnostic* Checker::elaborateNeverIntersection(Diagnostic* chain, Node* node, Type* t) {
	if ((t->flags & TypeFlagsIntersection) != 0 &&
		(t->objectFlags & ObjectFlagsIsNeverIntersection) != 0) {
		Symbol* neverProp = findOrNull(getPropertiesOfUnionOrIntersectionType(t),
			[this](Symbol* prop) { return isDiscriminantWithNeverType(prop); });
		if (neverProp != nullptr) {
			return NewDiagnosticChainForNode(chain, node,
				The_intersection_0_was_reduced_to_never_because_property_1_has_conflicting_types_in_some_constituents,
				{TypeToStringEx(t, nullptr, TypeFormatFlagsNoTypeReduction, nullptr),
					symbolToString(neverProp)});
		}
		Symbol* privateProp = findOrNull(getPropertiesOfUnionOrIntersectionType(t),
			[](Symbol* prop) {
				return members_detail::isConflictingPrivateProperty(prop);
			});
		if (privateProp != nullptr) {
			return NewDiagnosticChainForNode(chain, node,
				The_intersection_0_was_reduced_to_never_because_property_1_exists_in_multiple_constituents_and_is_private_in_some,
				{TypeToStringEx(t, nullptr, TypeFormatFlagsNoTypeReduction, nullptr),
					symbolToString(privateProp)});
		}
	}
	return chain;
}

bool Checker::isDiscriminantWithNeverType(Symbol* prop) {
	// Return true for a synthetic non-optional property with non-uniform types, where at least one is
	// a literal type and none is never, that reduces to never.
	return (prop->flags & SymbolFlagsOptional) == 0 &&
		(prop->checkFlags & (CheckFlagsNonUniformAndLiteral | CheckFlagsHasNeverType)) ==
			CheckFlagsNonUniformAndLiteral &&
		(getTypeOfSymbol(prop)->flags & TypeFlagsNever) != 0;
}

// ---------------------------------------------------------------------------
// === dep stubs — removed when owner slice lands ===
// ---------------------------------------------------------------------------

Type* Checker::getBaseConstructorTypeOfClass(Type* t) {
	TSC_UNREACHABLE("getBaseConstructorTypeOfClass — owned by checker.go:17277 slice");
}
Type* Checker::getTypeFromClassOrInterfaceReference(Node* node, Symbol* symbol) {
	TSC_UNREACHABLE(
		"getTypeFromClassOrInterfaceReference — owned by checker.go:23626 slice");
}
Type* Checker::getReturnTypeOfSignature(Signature* sig) {
	TSC_UNREACHABLE("getReturnTypeOfSignature — owned by signatures slice");
}
Signature* Checker::getSignatureFromDeclaration(Node* declaration) {
	TSC_UNREACHABLE("getSignatureFromDeclaration — owned by signatures slice");
}
Signature* Checker::getSignatureOfFullSignatureType(Node* node) {
	TSC_UNREACHABLE("getSignatureOfFullSignatureType — owned by signatures slice");
}
Signature* Checker::instantiateSignature(Signature* sig, TypeMapper* m) {
	TSC_UNREACHABLE("instantiateSignature — owned by signatures slice");
}
Signature* Checker::instantiateSignatureEx(Signature* sig, TypeMapper* m,
	bool eraseTypeParameters) {
	TSC_UNREACHABLE("instantiateSignatureEx — owned by signatures slice");
}
InferenceContext* Checker::newInferenceContext(
	const std::vector<Type*>& typeParameters, Signature* signature,
	InferenceFlags flags, TypeComparer compareTypes) {
	TSC_UNREACHABLE("newInferenceContext — owned by inference.go slice");
}
std::vector<Type*> Checker::getInferredTypes(InferenceContext* n) {
	TSC_UNREACHABLE("getInferredTypes — owned by inference.go slice");
}
void Checker::inferTypes(std::vector<InferenceInfo*>& inferences,
	Type* originalSource, Type* originalTarget, InferencePriority priority,
	bool contravariant) {
	TSC_UNREACHABLE("inferTypes — owned by inference.go slice");
}
void Checker::applyToParameterTypes(Signature* source, Signature* target,
	const std::function<void(Type*, Type*)>& callback) {
	TSC_UNREACHABLE("applyToParameterTypes — owned by inference.go slice");
}
void Checker::applyToReturnTypes(Signature* source, Signature* target,
	const std::function<void(Type*, Type*)>& callback) {
	TSC_UNREACHABLE("applyToReturnTypes — owned by inference.go slice");
}
Type* Checker::getEffectiveRestType(Signature* signature) {
	TSC_UNREACHABLE("getEffectiveRestType — owned by relater.go slice");
}
Type* Checker::getConstraintOfTypeParameter(Type* typeParameter) {
	TSC_UNREACHABLE("getConstraintOfTypeParameter — owned by checker.go:17379 slice");
}
bool Checker::isTypeAssignableToKind(Type* source, TypeFlags kind) {
	TSC_UNREACHABLE("isTypeAssignableToKind — owned by checker.go:28107 slice");
}
bool Checker::isGenericType(Type* t) {
	TSC_UNREACHABLE("isGenericType — owned by checker.go:25334 slice");
}
bool Checker::isGenericTupleType(Type* t) {
	TSC_UNREACHABLE("isGenericTupleType — owned by checker.go:25370 slice");
}
Type* Checker::getKnownKeysOfTupleType(Type* t) {
	TSC_UNREACHABLE("getKnownKeysOfTupleType — owned by relater.go slice");
}
Type* Checker::getRestTypeOfTupleType(Type* t) {
	TSC_UNREACHABLE("getRestTypeOfTupleType — owned by checker.go:25317 slice");
}
bool Checker::isArrayOrTupleType(Type* t) {
	TSC_UNREACHABLE("isArrayOrTupleType — owned by checker.go:23962 slice");
}
bool Checker::isArrayOrTupleOrIntersection(Type* t) {
	TSC_UNREACHABLE("isArrayOrTupleOrIntersection — owned by checker.go:24017 slice");
}
MappedTypeNameTypeKind Checker::getMappedTypeNameTypeKind(Type* t) {
	TSC_UNREACHABLE("getMappedTypeNameTypeKind — owned by checker.go:27308 slice");
}
Type* Checker::getTemplateTypeFromMappedType(Type* t) {
	TSC_UNREACHABLE("getTemplateTypeFromMappedType — owned by checker.go:23118 slice");
}
bool Checker::isMappedTypeWithKeyofConstraintDeclaration(Type* t) {
	TSC_UNREACHABLE(
		"isMappedTypeWithKeyofConstraintDeclaration — owned by checker.go:23130 slice");
}
void Checker::forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType(
	Type* t, TypeFlags include, bool stringsOnly,
	const std::function<void(Type*)>& cb) {
	TSC_UNREACHABLE(
		"forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType — owned by checker.go:23148 slice");
}
Type* Checker::getModifiersTypeFromMappedType(Type* t) {
	TSC_UNREACHABLE("getModifiersTypeFromMappedType — owned by checker.go:28593 slice");
}
bool Checker::isReadonlySymbol(Symbol* symbol) {
	TSC_UNREACHABLE("isReadonlySymbol — owned by checker.go:14083 slice");
}
Type* Checker::removeMissingOrUndefinedType(Type* t) {
	TSC_UNREACHABLE("removeMissingOrUndefinedType — owned by checker.go:29565 slice");
}
std::vector<IndexInfo*> Checker::getUnionIndexInfos(const std::vector<Type*>& types) {
	TSC_UNREACHABLE("getUnionIndexInfos — owned by checker.go:13727 slice");
}
void Checker::resolveReverseMappedTypeMembers(Type* t) {
	TSC_UNREACHABLE("resolveReverseMappedTypeMembers — owned by inference.go slice");
}
Signature* Checker::findMatchingSignature(const std::vector<Signature*>& signatureList,
	Signature* signature, bool partialMatch, bool ignoreThisTypes,
	bool ignoreReturnTypes) {
	TSC_UNREACHABLE("findMatchingSignature — owned by relater.go slice");
}
std::vector<Signature*> Checker::findMatchingSignatures(
	const std::vector<std::vector<Signature*>>& signatureLists, Signature* signature,
	int listIndex) {
	TSC_UNREACHABLE("findMatchingSignatures — owned by relater.go slice");
}
Signature* Checker::createUnionSignature(Signature* sig,
	const std::vector<Signature*>& unionSignatures) {
	TSC_UNREACHABLE("createUnionSignature — owned by checker.go:10492 slice");
}
bool Checker::compareTypeParametersIdentical(
	const std::vector<Type*>& sourceParams, const std::vector<Type*>& targetParams) {
	TSC_UNREACHABLE("compareTypeParametersIdentical — owned by relater.go slice");
}
Ternary Checker::compareSignaturesIdentical(Signature* source, Signature* target,
	bool partialMatch, bool ignoreThisTypes, bool ignoreReturnTypes,
	const std::function<Ternary(Type*, Type*)>& compareTypes) {
	TSC_UNREACHABLE("compareSignaturesIdentical — owned by relater.go slice");
}
Ternary Checker::compareTypesIdentical(Type* source, Type* target) {
	TSC_UNREACHABLE("compareTypesIdentical — owned by relater.go slice");
}
Ternary Checker::compareProperties(Symbol* sourceProp, Symbol* targetProp,
	const std::function<Ternary(Type*, Type*)>& compareTypes) {
	TSC_UNREACHABLE("compareProperties — owned by checker.go:28138 slice");
}
bool Checker::isMixinConstructorType(Type* t) {
	TSC_UNREACHABLE("isMixinConstructorType — owned by checker.go:17346 slice");
}
int Checker::getParameterCount(Signature* signature) {
	TSC_UNREACHABLE("getParameterCount — owned by relater.go slice");
}
bool Checker::hasEffectiveRestParameter(Signature* signature) {
	TSC_UNREACHABLE("hasEffectiveRestParameter — owned by relater.go slice");
}
Type* Checker::tryGetTypeAtPosition(Signature* signature, int pos) {
	TSC_UNREACHABLE("tryGetTypeAtPosition — owned by relater.go slice");
}
Type* Checker::getTypeAtPosition(Signature* signature, int pos) {
	TSC_UNREACHABLE("getTypeAtPosition — owned by relater.go slice");
}
int Checker::getMinArgumentCount(Signature* signature) {
	TSC_UNREACHABLE("getMinArgumentCount — owned by relater.go slice");
}
std::string Checker::getParameterNameAtPosition(Signature* signature, int pos) {
	TSC_UNREACHABLE("getParameterNameAtPosition — owned by relater.go slice");
}
std::vector<Type*> Checker::getTypeArgumentsFromNode(Node* node) {
	TSC_UNREACHABLE("getTypeArgumentsFromNode — owned by checker.go:23673 slice");
}
Type* Checker::getIndexedAccessType(Type* objectType, Type* indexType) {
	TSC_UNREACHABLE("getIndexedAccessType — owned by checker.go:27389 slice");
}
Type* Checker::getIndexType(Type* t) {
	TSC_UNREACHABLE("getIndexType — owned by checker.go:27146 slice");
}

// Free-function dep stubs (checker package / utilities.go).

bool isLateBoundName(const std::string& name) {
	TSC_UNREACHABLE("isLateBoundName — owned by utilities.go slice");
}
bool IsKnownSymbol(Symbol* symbol) {
	TSC_UNREACHABLE("IsKnownSymbol — owned by utilities.go slice");
}
bool isObjectLiteralType(Type* t) {
	TSC_UNREACHABLE("isObjectLiteralType — owned by utilities.go slice");
}
bool isTupleType(Type* t) {
	TSC_UNREACHABLE("isTupleType — owned by checker.go:23946 slice");
}
bool hasReadonlyModifier(Node* node) {
	TSC_UNREACHABLE("hasReadonlyModifier — owned by utilities.go slice");
}
bool isStaticPrivateIdentifierProperty(Symbol* s) {
	TSC_UNREACHABLE("isStaticPrivateIdentifierProperty — owned by utilities.go slice");
}
ModifierFlags getDeclarationModifierFlagsFromSymbol(Symbol* s) {
	TSC_UNREACHABLE(
		"getDeclarationModifierFlagsFromSymbol — owned by utilities.go slice");
}
ModifierFlags getDeclarationModifierFlagsFromSymbolEx(Symbol* s, bool isWrite) {
	TSC_UNREACHABLE(
		"getDeclarationModifierFlagsFromSymbolEx — owned by utilities.go slice");
}
MappedTypeModifiers getMappedTypeModifiers(Type* t) {
	TSC_UNREACHABLE("getMappedTypeModifiers — owned by checker.go:29478 slice");
}
Ternary compareTypesEqual(Type* s, Type* t) {
	TSC_UNREACHABLE("compareTypesEqual — owned by checker.go:28165 slice");
}

} // namespace checker
} // namespace tsc


