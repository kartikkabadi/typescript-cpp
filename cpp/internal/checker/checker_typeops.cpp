// checker_typeops.cpp — type-operation slice of the checker.
// Ports checker.go:26020-28654 — the type-operation middle: index/indexed-access
// machinery (getIndexType*, getIndexedAccessType*, getPropertyTypeForIndexType),
// literal-type extraction, computed-property checking, NoInfer/substitution
// types, base-constraint resolution, maybeTypeOfKind/isTypeAssignableToKind
// predicates, property marking, rest-parameter/tuple member expansion,
// uniform/unknown-like union predicates, type simplification and
// normalization, mapped-type modifiers, and member transforms.
//
// Functions already ported in checker.cpp (union/intersection construction,
// removeRedundantLiteralTypes, removeSubtypes, isEmptyObjectType, filterType,
// removeType, etc.) are NOT duplicated here — this file covers the remaining
// in-range functions, in file order.
//
// Dep-stub convention (this file only): callees owned by other slices/files are
// defined at the bottom under "// === dep stubs ===" with TSC_UNREACHABLE bodies
// and an owner tag; each is deleted from here when the owner's real definition
// lands. Free functions owned by other files are given static definitions here —
// internal linkage, so they cannot collide with the owner's definition at merge.

#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/scanner/scanner.h"
#include "internal/jsnum/jsnum.h"
#include "internal/core/spelling.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace tsc::checker {

// Free fns defined (non-static) in checker.cpp — forward declarations.
bool isFreshLiteralType(Type* t);
bool someType(Type* t, const std::function<bool(Type*)>& f);
bool everyType(Type* t, const std::function<bool(Type*)>& f);
void forEachType(Type* t, const std::function<void(Type*)>& f);
bool everyContainedType(Type* t, const std::function<bool(Type*)>& f);
bool containsType(const std::vector<Type*>& types, Type* t);
Diagnostic* NewDiagnosticChainForNode(Diagnostic* chain, Node* node,
									const DiagnosticMessage* message,
									const std::vector<std::string>& args);

// checker.cpp:737 — anyToString (file-local copy; used for literal-type
// diagnostic arguments)
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

// ---------------------------------------------------------------------------
// File-local copies of free helpers owned by other files (internal linkage —
// cannot collide with the owners' definitions at merge).
// ---------------------------------------------------------------------------

// keyBuilder — port of Go's keyBuilder (copied from checker.cpp). We collect
// the encoded byte stream and chunk it into CacheKey words (content-keyed
// interning; equivalent to Go's xxh3-hashed keys since the keys never escape
// the checker).
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

// checker.cpp — getTypeListKey (file-local copy)
static CacheKey getTypeListKey(const std::vector<Type*>& types) {
	keyBuilder b;
	b.writeTypes(types);
	return b.hash();
}

// checker.go:17916 — getIndexedAccessKey
static CacheKey getIndexedAccessKey(Type* objectType, Type* indexType,
									AccessFlags accessFlags, TypeAlias* alias) {
	keyBuilder b;
	b.writeType(objectType);
	b.writeType(indexType);
	b.writeUint32(static_cast<uint32_t>(accessFlags));
	b.writeAlias(alias);
	return b.hash();
}

// checker.cpp — literal value helpers (file-local copies)
static std::string getStringLiteralValue(Type* t) {
	return std::get<std::string>(t->AsLiteralType()->value);
}

static PseudoBigInt getBigIntLiteralValue(Type* t) {
	return std::get<PseudoBigInt>(t->AsLiteralType()->value);
}

// checker.cpp:2666 — isTypeUsableAsPropertyName
static bool isTypeUsableAsPropertyName(Type* t) {
	return t->flags & TypeFlagsStringOrNumberLiteralOrUnique;
}

// checker.cpp:2696 — getPropertyNameFromType
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

// checker.cpp:3980 — isThisProperty
static bool isThisProperty(Node* node) {
	return (isPropertyAccessExpression(node) || isElementAccessExpression(node)) &&
		   node->expression()->kind == Kind::ThisKeyword;
}

// checker_declchecks.cpp — isTupleType (checker.go:23946)
static bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 &&
		(t->Target()->objectFlags & ObjectFlagsTuple) != 0;
}

// checker_declchecks.cpp — targetTupleType (TargetTupleType)
static TupleType* targetTupleType(Type* t) {
	return t->AsTypeReference()->target->AsTupleType();
}

// checker.go:23912 — getEndElementCount
static int getEndElementCount(TupleType* t, ElementFlags flags) {
	for (int i = static_cast<int>(t->elementInfos.size()); i > 0; i--) {
		if ((t->elementInfos[i - 1].flags & flags) == 0) {
			return static_cast<int>(t->elementInfos.size()) - i;
		}
	}
	return static_cast<int>(t->elementInfos.size());
}

// checker.go:23921 — getTotalFixedElementCount
static int getTotalFixedElementCount(TupleType* t) {
	return t->fixedLength + getEndElementCount(t, ElementFlagsFixed);
}

// isTypeAny — canonical def in checker_utilities.cpp

// utilities.go:867 — isObjectLiteralType
static bool isObjectLiteralType(Type* t) {
	return t->objectFlags & ObjectFlagsObjectLiteral;
}

// utilities.go:1033 — isObjectOrArrayLiteralType
static bool isObjectOrArrayLiteralType(Type* t) {
	return (t->objectFlags & (ObjectFlagsObjectLiteral | ObjectFlagsArrayLiteral)) != 0;
}

// utilities.go:109 — isDeleteTarget
static bool isDeleteTarget(Node* node) {
	if (!isAccessExpression(node)) {
		return false;
	}
	node = walkUpParenthesizedExpressions(node->parent);
	return node != nullptr && node->kind == Kind::DeleteExpression;
}

// utilities.go:89 — getAssignmentTargetKind
static AssignmentKind getAssignmentTargetKind(Node* node) {
	Node* target = getAssignmentTarget(node);
	if (target == nullptr) {
		return AssignmentKindNone;
	}
	switch (target->kind) {
	case Kind::BinaryExpression: {
		Kind binaryOperator = target->as<BinaryExpression>()->OperatorToken->kind;
		if (binaryOperator == Kind::EqualsToken ||
			isLogicalOrCoalescingAssignmentOperator(binaryOperator)) {
			return AssignmentKindDefinite;
		}
		return AssignmentKindCompound;
	}
	case Kind::PrefixUnaryExpression:
	case Kind::PostfixUnaryExpression:
		return AssignmentKindCompound;
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		return AssignmentKindDefinite;
	}
	TSC_UNREACHABLE("Unhandled case in getAssignmentTargetKind");
}

// isPropertyName lives in ast.h.
// isUnaryExpressionKind/isExpressionKind/isExpression live in ast.h.

// utilities.go:1029 — isLateBoundName
static bool isLateBoundName(const std::string& name) {
	return name.size() >= 2 && name[0] == '\xfe' && name[1] == '@';
}

// utilities.go:1018 — IsKnownSymbol
static bool IsKnownSymbol(Symbol* symbol) {
	return isLateBoundName(symbol->name);
}

// tryGetPropertyAccessOrIdentifierToString is shared (checker_utilities.cpp).

// utilities.go:761 — getDeclarationModifierFlagsFromSymbolEx
static ModifierFlags getDeclarationModifierFlagsFromSymbolEx(Symbol* s, bool isWrite) {
	if (s->checkFlags & CheckFlagsSynthetic) {
		ModifierFlags accessModifier;
		if ((!isWrite && (s->checkFlags & CheckFlagsContainsPublic)) ||
			(isWrite && (s->checkFlags & CheckFlagsContainsWritePublic))) {
			accessModifier = ModifierFlagsPublic;
		} else if ((!isWrite && (s->checkFlags & CheckFlagsContainsProtected)) ||
				   (isWrite && (s->checkFlags & CheckFlagsContainsWriteProtected))) {
			accessModifier = ModifierFlagsProtected;
		} else if ((!isWrite && (s->checkFlags & CheckFlagsContainsPrivate)) ||
				   (isWrite && (s->checkFlags & CheckFlagsContainsWritePrivate))) {
			accessModifier = ModifierFlagsPrivate;
		} else {
			accessModifier = ModifierFlagsNone;
		}
		if (s->checkFlags & CheckFlagsContainsStatic) {
			return accessModifier | ModifierFlagsStatic;
		}
		return accessModifier;
	}
	if (s->valueDeclaration != nullptr) {
		Node* declaration = nullptr;
		if (isWrite) {
			for (Node* d : s->declarations) {
				if (isSetAccessorDeclaration(d)) {
					declaration = d;
					break;
				}
			}
		}
		if (declaration == nullptr && (s->flags & SymbolFlagsGetAccessor)) {
			for (Node* d : s->declarations) {
				if (isGetAccessorDeclaration(d)) {
					declaration = d;
					break;
				}
			}
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

// utilities.go:757 — getDeclarationModifierFlagsFromSymbol
static ModifierFlags getDeclarationModifierFlagsFromSymbol(Symbol* s) {
	return getDeclarationModifierFlagsFromSymbolEx(s, false /*isWrite*/);
}

// ast/utilities.go:2994 — getClassLikeDeclarationOfSymbol
static Node* getClassLikeDeclarationOfSymbol(Symbol* symbol) {
	for (Node* d : symbol->declarations) {
		if (isClassLike(d)) {
			return d;
		}
	}
	return nullptr;
}

// checker.go:19604 — getBaseTypeNodeOfClass
static Node* getBaseTypeNodeOfClass(Type* t) {
	Node* decl = getClassLikeDeclarationOfSymbol(t->symbol);
	if (decl != nullptr) {
		return getClassExtendsHeritageElement(decl);
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// RecursionId helpers — copies of the file-local helpers in checker_tracer.cpp
// (relater.go:810-868). RecursionId itself is declared in checker.h.
// ---------------------------------------------------------------------------

static RecursionId asRecursionId(Node* value) {
	return {reinterpret_cast<uintptr_t>(value)};
}
static RecursionId asRecursionId(Symbol* value) {
	return {reinterpret_cast<uintptr_t>(value)};
}
static RecursionId asRecursionId(Type* value) {
	return {reinterpret_cast<uintptr_t>(value)};
}

// relater.go:820 — Get the recursion identity target type from a type. Recursively (a) obtain the
// target object type of an indexed access (i.e. the T in T[K]), and (b) unwrap nested homomorphic
// mapped types and return the deepest target type that has a symbol. The unwrapping better
// preserves unique type identities for mapped types applied to explicitly written object literals.
static Type* getRecursionIdentityTarget(Type* t, Checker* c) {
	if (t->flags & TypeFlagsIndexedAccess) {
		return getRecursionIdentityTarget(t->AsIndexedAccessType()->objectType, c);
	}
	if ((t->objectFlags & ObjectFlagsInstantiatedMapped) == ObjectFlagsInstantiatedMapped) {
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

// relater.go:840 — The recursion identity of a type is an object identity that is shared among
// multiple instantiations of the type. We track recursion identities in order to identify deeply
// nested and possibly infinite type instantiations with the same origin.
static RecursionId getRecursionIdentityFromTarget(Type* t, Checker* c) {
	// Object and array literals are known not to contain recursive references and don't need a
	// recursion identity.
	if ((t->flags & TypeFlagsObject) != 0 && !isObjectOrArrayLiteralType(t)) {
		if ((t->objectFlags & ObjectFlagsReference) != 0 && t->AsTypeReference()->node != nullptr) {
			// Deferred type references are tracked through their associated AST node. This gives us
			// finer granularity than using their associated target because each manifest type
			// reference has a unique AST node.
			return asRecursionId(t->AsTypeReference()->node);
		}
		if (t->symbol != nullptr &&
			!((t->objectFlags & ObjectFlagsAnonymous) != 0 &&
			  (t->symbol->flags & SymbolFlagsClass) != 0) &&
			(t->objectFlags & ObjectFlagsFromTypeNode) == 0) {
			// We track object types that have a symbol by that symbol (representing the origin of
			// the type), but exclude the static sides of classes (since they share their symbols
			// with the instance sides) and type references that originate in resolution of AST type
			// nodes (since such type nodes cannot be the source of generative recursion without
			// first being instantiated).
			return asRecursionId(t->symbol);
		}
		if (isTupleType(t) && (t->objectFlags & ObjectFlagsFromTypeNode) == 0) {
			return asRecursionId(t->Target());
		}
	}
	if ((t->flags & TypeFlagsTypeParameter) != 0 && t->symbol != nullptr) {
		// We use the symbol of the type parameter such that all "fresh" instantiations of that
		// type parameter have the same recursion identity.
		return asRecursionId(t->symbol);
	}
	if ((t->flags & TypeFlagsConditional) != 0) {
		// The root object represents the origin of the conditional type
		return asRecursionId(t->AsConditionalType()->root->node);
	}
	return asRecursionId(t);
}

// relater.go:810
static RecursionId getRecursionIdentity(Type* t, Checker* c) {
	return getRecursionIdentityFromTarget(getRecursionIdentityTarget(t, c), c);
}

// ---------------------------------------------------------------------------
// Forward declarations of file-local helpers defined below (Go file order puts
// some helpers after their first caller).
// ---------------------------------------------------------------------------
static bool isConstEnumSymbol(Symbol* symbol);
static bool isConstEnumObjectType(Type* t);
static bool isRestParameter(Node* param);
static Node* getIndexNodeForAccessExpression(Node* accessNode);
static bool indexTypeLessThan(Type* indexType, int limit);

// ---------------------------------------------------------------------------
// checker.go — in-range functions, in file order.
// ---------------------------------------------------------------------------

// checker.go:26223
std::vector<Type*> Checker::UnionTypes() {
	std::vector<Type*> result;
	result.reserve(unionTypes.size());
	for (const auto& [key, value] : unionTypes) {
		result.push_back(value);
	}
	return result;
}

// checker.go:26490
Type* Checker::intersectTypes(Type* type1, Type* type2) {
	if (type1 == nullptr) {
		return type2;
	}
	if (type2 == nullptr) {
		return type1;
	}
	return getIntersectionType({type1, type2});
}

// checker.go:27146
Type* Checker::getIndexType(Type* t) {
	return getIndexTypeEx(t, IndexFlagsNone);
}

// checker.go:27150
Type* Checker::getIndexTypeEx(Type* t, IndexFlags indexFlags) {
	t = getReducedType(t);
	if (isNoInferType(t)) {
		return getNoInferType(getIndexTypeEx(t->AsSubstitutionType()->baseType, indexFlags));
	}
	if (shouldDeferIndexType(t, indexFlags)) {
		return getIndexTypeForGenericType(t, indexFlags);
	}
	if (t->flags & TypeFlagsUnion) {
		std::vector<Type*> mapped;
		mapped.reserve(t->types().size());
		for (Type* u : t->types()) {
			mapped.push_back(getIndexTypeEx(u, indexFlags));
		}
		return getIntersectionType(mapped);
	}
	if (t->flags & TypeFlagsIntersection) {
		std::vector<Type*> mapped;
		mapped.reserve(t->types().size());
		for (Type* u : t->types()) {
			mapped.push_back(getIndexTypeEx(u, indexFlags));
		}
		return getUnionType(mapped);
	}
	if (t->objectFlags & ObjectFlagsMapped) {
		return getIndexTypeForMappedType(t, indexFlags);
	}
	if (t == wildcardType) {
		return wildcardType;
	}
	if (t->flags & TypeFlagsUnknown) {
		return neverType;
	}
	if (t->flags & (TypeFlagsAny | TypeFlagsNever)) {
		return stringNumberSymbolType;
	}
	TypeFlags include = ((indexFlags & IndexFlagsNoIndexSignatures) ? TypeFlagsStringLiteral
																	: TypeFlagsStringLike) |
		((indexFlags & IndexFlagsStringsOnly) ? TypeFlagsNone
											  : (TypeFlagsNumberLike | TypeFlagsESSymbolLike));
	return getLiteralTypeFromProperties(t, include, indexFlags == IndexFlagsNone);
}

// checker.go:27175
Type* Checker::getExtractStringType(Type* t) {
	Symbol* extractTypeAlias = getGlobalExtractSymbol();
	if (extractTypeAlias != nullptr) {
		return getTypeAliasInstantiation(extractTypeAlias, {t, stringType}, nullptr);
	}
	return stringType;
}

// checker.go:27183
Type* Checker::getLiteralTypeFromProperties(Type* t, TypeFlags include,
											bool includeOrigin) {
	PropertiesTypesKey key{t->id, include, includeOrigin};
	if (auto it = propertiesTypes.find(key); it != propertiesTypes.end()) {
		return it->second;
	}
	Type* origin = nullptr;
	if ((includeOrigin &&
		 (t->objectFlags & (ObjectFlagsClassOrInterface | ObjectFlagsReference))) ||
		t->alias != nullptr) {
		origin = newIndexType(t, IndexFlagsNone);
	}
	std::vector<Symbol*> props = getPropertiesOfType(t);
	std::vector<IndexInfo*> indexInfos = getIndexInfosOfType(t);
	std::vector<Type*> types;
	types.reserve(props.size() + indexInfos.size());
	for (Symbol* prop : props) {
		types.push_back(getLiteralTypeFromProperty(prop, include, false));
	}
	for (IndexInfo* info : indexInfos) {
		if (info != enumNumberIndexInfo && isKeyTypeIncluded(info->keyType, include)) {
			if (info->keyType == stringType && (include & TypeFlagsNumber)) {
				types.push_back(stringOrNumberType);
			} else {
				types.push_back(info->keyType);
			}
		}
	}
	Type* result = getUnionTypeEx(types, UnionReductionLiteral, nullptr, origin);
	propertiesTypes[key] = result;
	return result;
}

// checker.go:27212
Type* Checker::getLiteralTypeFromProperty(Symbol* prop, TypeFlags include,
										  bool includeNonPublic) {
	if (includeNonPublic ||
		(getDeclarationModifierFlagsFromSymbol(prop) &
		 ModifierFlagsNonPublicAccessibilityModifier) == 0) {
		Type* t = valueSymbolLinks.Get(getLateBoundSymbol(prop))->nameType;
		if (t == nullptr) {
			if (prop->name == InternalSymbolNameDefault) {
				t = getStringLiteralType("default");
			} else {
				Node* name = getNameOfDeclaration(prop->valueDeclaration);
				if (name != nullptr) {
					t = getLiteralTypeFromPropertyName(name);
				}
				if (t == nullptr && !IsKnownSymbol(prop)) {
					t = getStringLiteralType(symbolName(prop));
				}
			}
		}
		if (t != nullptr && (t->flags & include)) {
			return t;
		}
	}
	return neverType;
}

// checker.go:27235
Type* Checker::getLiteralTypeFromPropertyName(Node* name) {
	if (isPrivateIdentifier(name)) {
		return neverType;
	}
	if (isNumericLiteral(name)) {
		return getRegularTypeOfLiteralType(checkExpression(name));
	}
	if (isComputedPropertyName(name)) {
		return getRegularTypeOfLiteralType(checkComputedPropertyName(name));
	}
	std::string propertyName = getPropertyNameForPropertyNameNode(name);
	if (propertyName != InternalSymbolNameMissing) {
		return getStringLiteralType(propertyName);
	}
	if (isExpression(name)) {
		return getRegularTypeOfLiteralType(checkExpression(name));
	}
	return neverType;
}

// checker.go:27255
bool Checker::isKeyTypeIncluded(Type* keyType, TypeFlags include) {
	if (keyType->flags & include) {
		return true;
	}
	if (keyType->flags & TypeFlagsIntersection) {
		for (Type* t : keyType->types()) {
			if (isKeyTypeIncluded(t, include)) {
				return true;
			}
		}
	}
	return false;
}

// checker.go:27262
static bool isInvalidComputedPropertyName(Node* node) {
	return (isTypeLiteralNode(node->parent->parent) ||
			isClassLike(node->parent->parent) ||
			isInterfaceDeclaration(node->parent->parent)) &&
		isBinaryExpression(node->expression()) &&
		node->expression()->as<BinaryExpression>()->OperatorToken->kind ==
			Kind::InKeyword &&
		!isAccessor(node->parent);
}

// checker.go:27268
Type* Checker::checkComputedPropertyName(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr ||
		staleForCheckFile(links->resolvedTypeCheckFile)) {
		// Go: fresh per-checker cache — recompute under this file.
		links->resolvedType = nullptr;
		links->resolvedTypeCheckFile = activeCheckFile;
		links->resolvedType = circularConstraintType;
		if (isInvalidComputedPropertyName(node)) {
			links->resolvedType = errorType;
			return links->resolvedType;
		}
		links->resolvedType = checkExpression(node->expression());
		// This will allow types number, string, symbol or any. It will also allow enums, the unknown
		// type, and any union of these types (like string | number).
		if ((links->resolvedType->flags & TypeFlagsNullable) ||
			(!isTypeAssignableToKind(links->resolvedType,
									 TypeFlagsStringLike | TypeFlagsNumberLike |
										 TypeFlagsESSymbolLike) &&
			 !isTypeAssignableTo(links->resolvedType, stringNumberSymbolType))) {
			error(node, A_computed_property_name_must_be_of_type_string_number_symbol_or_any);
		}
	}
	return links->resolvedType;
}

// checker.go:27288
bool Checker::isNoInferType(Type* t) {
	// A NoInfer<T> type is represented as a substitution type with a TypeFlags.Unknown constraint.
	return (t->flags & TypeFlagsSubstitution) &&
		(t->AsSubstitutionType()->constraint->flags & TypeFlagsUnknown);
}

// checker.go:27293
Type* Checker::getSubstitutionIntersection(Type* t) {
	if (isNoInferType(t)) {
		return t->AsSubstitutionType()->baseType;
	}
	return getIntersectionType(
		{t->AsSubstitutionType()->constraint, t->AsSubstitutionType()->baseType});
}

// checker.go:27300
bool Checker::shouldDeferIndexType(Type* t, IndexFlags indexFlags) {
	return (t->flags & TypeFlagsInstantiableNonPrimitive) ||
		isGenericTupleType(t) ||
		(isGenericMappedType(t) && getNameTypeFromMappedType(t) != nullptr) ||
		((t->flags & TypeFlagsUnion) && !(indexFlags & IndexFlagsNoReducibleCheck) &&
		 isGenericReducibleType(t)) ||
		((t->flags & TypeFlagsIntersection) &&
		 maybeTypeOfKind(t, TypeFlagsInstantiable) &&
		 [&] {
			 for (Type* u : t->types()) {
				 if (IsEmptyAnonymousObjectType(u)) {
					 return true;
				 }
			 }
			 return false;
		 }());
}

// checker.go:27308
MappedTypeNameTypeKind Checker::getMappedTypeNameTypeKind(Type* t) {
	Type* nameType = getNameTypeFromMappedType(t);
	if (nameType == nullptr) {
		return MappedTypeNameTypeKind::None;
	}
	if (isTypeAssignableTo(nameType, getTypeParameterFromMappedType(t))) {
		return MappedTypeNameTypeKind::Filtering;
	}
	return MappedTypeNameTypeKind::Remapping;
}

// checker.go:27319
Type* Checker::getIndexTypeForGenericType(Type* t, IndexFlags indexFlags) {
	CachedTypeKey key{
		(indexFlags & IndexFlagsStringsOnly) ? CachedTypeKind::StringIndexType
											 : CachedTypeKind::IndexType,
		t->id};
	if (Type* indexType = cachedTypes[key]; indexType != nullptr) {
		return indexType;
	}
	Type* indexType = newIndexType(t, indexFlags & IndexFlagsStringsOnly);
	cachedTypes[key] = indexType;
	return indexType;
}

// This roughly mirrors `resolveMappedTypeMembers` in the nongeneric case, except only reports a union of the keys calculated,
// rather than manufacturing the properties. We can't just fetch the `constraintType` since that would ignore mappings
// and mapping the `constraintType` directly ignores how mapped types map _properties_ and not keys (thus ignoring subtype
// reduction in the constraintType) when possible.
// @param noIndexSignatures Indicates if _string_ index signatures should be elided. (other index signatures are always reported)
// checker.go:27337
Type* Checker::getIndexTypeForMappedType(Type* t, IndexFlags indexFlags) {
	Type* typeParameter = getTypeParameterFromMappedType(t);
	Type* constraintType = getConstraintTypeFromMappedType(t);
	MappedType* mt = t->AsMappedType();
	Type* nameTypeSource = mt->target != nullptr ? mt->target : t;
	Type* nameType = getNameTypeFromMappedType(nameTypeSource);
	if (nameType == nullptr && !(indexFlags & IndexFlagsNoIndexSignatures)) {
		// no mapping and no filtering required, just quickly bail to returning the constraint in the common case
		return constraintType;
	}
	std::vector<Type*> keyTypes;
	auto addMemberForKeyType = [&](Type* keyType) {
		Type* propNameType = keyType;
		if (nameType != nullptr) {
			propNameType = instantiateType(
				nameType, appendTypeMapping(mt->mapper, typeParameter, keyType));
		}
		// `keyof` currently always returns `string | number` for concrete `string` index signatures - the below ternary keeps that behavior for mapped types
		// See `getLiteralTypeFromProperties` where there's a similar ternary to cause the same behavior.
		keyTypes.push_back(propNameType == stringType ? stringOrNumberType : propNameType);
	};
	// Calling getApparentType on the `T` of a `keyof T` in the constraint type of a generic mapped type can
	// trigger a circularity. For example, `T extends { [P in keyof T & string as Captitalize<P>]: any }` is
	// a circular definition. For this reason, we only eagerly manifest the keys if the constraint is non-generic.
	if (isGenericIndexType(constraintType)) {
		if (isMappedTypeWithKeyofConstraintDeclaration(t)) {
			// We have a generic index and a homomorphic mapping and a key remapping - we need to defer
			// the whole `keyof whatever` for later since it's not safe to resolve the shape of modifier type.
			return getIndexTypeForGenericType(t, indexFlags);
		}
		// Include the generic component in the resulting type.
		forEachType(constraintType, addMemberForKeyType);
	} else if (isMappedTypeWithKeyofConstraintDeclaration(t)) {
		Type* modifiersType = getApparentType(getModifiersTypeFromMappedType(t));
		// The 'T' in 'keyof T'
		forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType(
			modifiersType, TypeFlagsStringOrNumberLiteralOrUnique,
			(indexFlags & IndexFlagsStringsOnly) != 0, addMemberForKeyType);
	} else {
		forEachType(getLowerBoundOfKeyType(constraintType), addMemberForKeyType);
	}
	// We had to pick apart the constraintType to potentially map/filter it - compare the final resulting list with the
	// original constraintType, so we can return the union that preserves aliases/origin data if possible.
	Type* result;
	if (indexFlags & IndexFlagsNoIndexSignatures) {
		result = filterType(getUnionType(keyTypes), [](Type* t) {
			return (t->flags & (TypeFlagsAny | TypeFlagsString)) == 0;
		});
	} else {
		result = getUnionType(keyTypes);
	}
	if ((result->flags & TypeFlagsUnion) && (constraintType->flags & TypeFlagsUnion) &&
		getTypeListKey(result->types()) == getTypeListKey(constraintType->types())) {
		return constraintType;
	}
	return result;
}

// checker.go:27389
Type* Checker::getIndexedAccessType(Type* objectType, Type* indexType) {
	return getIndexedAccessTypeEx(objectType, indexType, AccessFlagsNone, nullptr,
								  nullptr);
}

// checker.go:27393
Type* Checker::getIndexedAccessTypeEx(Type* objectType, Type* indexType,
									  AccessFlags accessFlags, Node* accessNode,
									  TypeAlias* alias) {
	Type* result =
		getIndexedAccessTypeOrUndefined(objectType, indexType, accessFlags, accessNode, alias);
	if (result == nullptr) {
		result = accessNode != nullptr ? errorType : unknownType;
	}
	return result;
}

// checker.go:27401
Type* Checker::getIndexedAccessTypeOrUndefined(Type* objectType, Type* indexType,
											   AccessFlags accessFlags,
											   Node* accessNode, TypeAlias* alias) {
	if (objectType == wildcardType || indexType == wildcardType) {
		return wildcardType;
	}
	objectType = getReducedType(objectType);
	// If the object type has a string index signature and no other members we know that the result will
	// always be the type of that index signature and we can simplify accordingly.
	if (isStringIndexSignatureOnlyType(objectType) &&
		!(indexType->flags & TypeFlagsNullable) &&
		isTypeAssignableToKind(indexType, TypeFlagsString | TypeFlagsNumber)) {
		indexType = stringType;
	}
	// In noUncheckedIndexedAccess mode, indexed access operations that occur in an expression in a read position and resolve to
	// an index signature have 'undefined' included in their type.
	if (tristateIsTrue(compilerOptions->NoUncheckedIndexedAccess) &&
		(accessFlags & AccessFlagsExpressionPosition)) {
		accessFlags |= AccessFlagsIncludeUndefined;
	}
	// If the index type is generic, or if the object type is generic and doesn't originate in an expression and
	// the operation isn't exclusively indexing the fixed (non-variadic) portion of a tuple type, we are performing
	// a higher-order index access where we cannot meaningfully access the properties of the object type. Note that
	// for a generic T and a non-generic K, we eagerly resolve T[K] if it originates in an expression. This is to
	// preserve backwards compatibility. For example, an element access 'this["foo"]' has always been resolved
	// eagerly using the constraint type of 'this' at the given location.
	if (shouldDeferIndexedAccessType(objectType, indexType, accessNode)) {
		if (objectType->flags & TypeFlagsAnyOrUnknown) {
			return objectType;
		}
		// Defer the operation by creating an indexed access type.
		AccessFlags persistentAccessFlags = accessFlags & AccessFlagsPersistent;
		CacheKey key = getIndexedAccessKey(objectType, indexType, accessFlags, alias);
		Type* t = indexedAccessTypes[key];
		if (t == nullptr) {
			t = newIndexedAccessType(objectType, indexType, persistentAccessFlags);
			t->alias = alias;
			indexedAccessTypes[key] = t;
		}
		return t;
	}
	// In the following we resolve T[K] to the type of the property in T selected by K.
	// We treat boolean as different from other unions to improve errors;
	// skipping straight to getPropertyTypeForIndexType gives errors with 'boolean' instead of 'true'.
	Type* apparentObjectType = getReducedApparentType(objectType);
	if ((indexType->flags & TypeFlagsUnion) &&
		!(indexType->flags & TypeFlagsBoolean)) {
		std::vector<Type*> propTypes;
		bool wasMissingProp = false;
		for (Type* t : indexType->types()) {
			Type* propType = getPropertyTypeForIndexType(
				objectType, apparentObjectType, t, indexType, accessNode,
				accessFlags |
					(wasMissingProp ? AccessFlagsSuppressNoImplicitAnyError
									: AccessFlagsNone));
			if (propType != nullptr) {
				propTypes.push_back(propType);
			} else if (accessNode == nullptr) {
				// If there's no error node, we can immediately stop, since error reporting is off
				return nullptr;
			} else {
				// Otherwise we set a flag and return at the end of the loop so we still mark all errors
				wasMissingProp = true;
			}
		}
		if (wasMissingProp) {
			return nullptr;
		}
		if (accessFlags & AccessFlagsWriting) {
			return getIntersectionTypeEx(propTypes, IntersectionFlagsNone, alias);
		}
		return getUnionTypeEx(propTypes, UnionReductionLiteral, alias, nullptr);
	}
	return getPropertyTypeForIndexType(objectType, apparentObjectType, indexType,
									 indexType, accessNode,
									 accessFlags | AccessFlagsCacheSymbol |
										 AccessFlagsReportDeprecated);
}

// checker.go:27467
Type* Checker::getPropertyTypeForIndexType(Type* originalObjectType,
										   Type* objectType, Type* indexType,
										   Type* fullIndexType, Node* accessNode,
										   AccessFlags accessFlags) {
	Node* accessExpression = nullptr;
	if (accessNode != nullptr && isElementAccessExpression(accessNode)) {
		accessExpression = accessNode;
	}
	std::string propName;
	bool hasPropName = false;
	if (!(accessNode != nullptr && isPrivateIdentifier(accessNode))) {
		propName = getPropertyNameFromIndex(indexType, accessNode);
		hasPropName = propName != InternalSymbolNameMissing;
	}
	if (hasPropName) {
		if (accessFlags & AccessFlagsContextual) {
			Type* t = getTypeOfPropertyOfContextualType(objectType, propName);
			if (t == nullptr) {
				t = anyType;
			}
			return t;
		}
		Symbol* prop = getPropertyOfType(objectType, propName);
		if (prop != nullptr) {
			if ((accessFlags & AccessFlagsReportDeprecated) && accessNode != nullptr &&
				!prop->declarations.empty() && isDeprecatedSymbol(prop) &&
				isUncalledFunctionReference(accessNode, prop)) {
				Node* deprecatedNode;
				if (accessExpression != nullptr) {
					deprecatedNode =
						accessExpression->as<ElementAccessExpression>()->ArgumentExpression;
				} else if (isIndexedAccessTypeNode(accessNode)) {
					deprecatedNode = accessNode->as<IndexedAccessTypeNode>()->IndexType;
				} else {
					deprecatedNode = accessNode;
				}
				addDeprecatedSuggestion(deprecatedNode, prop->declarations, propName);
			}
			if (accessExpression != nullptr) {
				markPropertyAsReferenced(
					prop, accessExpression,
					isSelfTypeAccess(accessExpression->expression(), objectType->symbol));
				if (isAssignmentToReadonlyEntity(accessExpression, prop,
												 getAssignmentTargetKind(accessExpression))) {
					error(accessExpression->as<ElementAccessExpression>()->ArgumentExpression,
						  Cannot_assign_to_0_because_it_is_a_read_only_property,
						  {symbolToString(prop)});
					return nullptr;
				}
				if (accessFlags & AccessFlagsCacheSymbol) {
					auto* accessLinks = symbolNodeLinks.Get(accessNode);
					accessLinks->resolvedSymbol = prop;
					accessLinks->resolvedSymbolCheckFile = activeCheckFile;
				}
				if (isThisPropertyAccessInConstructor(accessExpression, prop)) {
					return autoType;
				}
			}
			Type* propType;
			if (accessFlags & AccessFlagsWriting) {
				propType = getWriteTypeOfSymbol(prop);
			} else {
				propType = getTypeOfSymbol(prop);
			}
			if (accessExpression != nullptr &&
				getAssignmentTargetKind(accessExpression) != AssignmentKindDefinite) {
				return getFlowTypeOfReference(accessExpression, propType);
			}
			if (accessNode != nullptr && isIndexedAccessTypeNode(accessNode) &&
				containsMissingType(propType)) {
				return getUnionType({propType, undefinedType});
			}
			return propType;
		}
		if (everyType(objectType, isTupleType) && isNumericLiteralName(propName)) {
			Number index = numberFromString(propName);
			if (accessNode != nullptr &&
				everyType(objectType, [](Type* t) {
					return (targetTupleType(t)->combinedFlags & ElementFlagsVariable) == 0;
				}) &&
				!(accessFlags & AccessFlagsAllowMissing)) {
				Node* indexNode = getIndexNodeForAccessExpression(accessNode);
				if (isTupleType(objectType)) {
					if (index.v < 0) {
						error(indexNode, A_tuple_type_cannot_be_indexed_with_a_negative_value);
						return undefinedType;
					}
					error(indexNode, Tuple_type_0_of_length_1_has_no_element_at_index_2,
						  {TypeToString(objectType),
						   std::to_string(getTypeReferenceArity(objectType)), propName});
				} else {
					error(indexNode, Property_0_does_not_exist_on_type_1,
						  {propName, TypeToString(objectType)});
				}
			}
			if (index.v >= 0) {
				errorIfWritingToReadonlyIndex(
					getIndexInfoOfType(objectType, numberType), objectType,
					accessExpression);
				return getTupleElementTypeOutOfStartCount(
					objectType, index,
					(accessFlags & AccessFlagsIncludeUndefined) ? missingType : nullptr);
			}
		}
	}
	if (!(indexType->flags & TypeFlagsNullable) &&
		isTypeAssignableToKind(indexType, TypeFlagsStringLike | TypeFlagsNumberLike |
										  TypeFlagsESSymbolLike)) {
		if (objectType->flags & (TypeFlagsAny | TypeFlagsNever)) {
			return objectType;
		}
		// If no index signature is applicable, we default to the string index signature. In effect, this means the string
		// index signature applies even when accessing with a symbol-like type.
		IndexInfo* indexInfo = getApplicableIndexInfo(objectType, indexType);
		if (indexInfo == nullptr) {
			indexInfo = getIndexInfoOfType(objectType, stringType);
		}
		if (indexInfo != nullptr) {
			if ((accessFlags & AccessFlagsNoIndexSignatures) &&
				indexInfo->keyType != numberType) {
				if (accessExpression != nullptr) {
					if (accessFlags & AccessFlagsWriting) {
						error(accessExpression,
							  Type_0_is_generic_and_can_only_be_indexed_for_reading,
							  {TypeToString(originalObjectType)});
					} else {
						error(accessExpression, Type_0_cannot_be_used_to_index_type_1,
							  {TypeToString(indexType), TypeToString(originalObjectType)});
					}
				}
				return nullptr;
			}
			if (accessNode != nullptr && indexInfo->keyType == stringType &&
				!isTypeAssignableToKind(indexType, TypeFlagsString | TypeFlagsNumber)) {
				Node* indexNode = getIndexNodeForAccessExpression(accessNode);
				error(indexNode, Type_0_cannot_be_used_as_an_index_type,
					  {TypeToString(indexType)});
				if (accessFlags & AccessFlagsIncludeUndefined) {
					return getUnionType({indexInfo->valueType, missingType});
				} else {
					return indexInfo->valueType;
				}
			}
			errorIfWritingToReadonlyIndex(indexInfo, objectType, accessExpression);
			// When accessing an enum object with its own type,
			// e.g. E[E.A] for enum E { A }, undefined shouldn't
			// be included in the result type
			if ((accessFlags & AccessFlagsIncludeUndefined) &&
				!(objectType->symbol != nullptr &&
				  (objectType->symbol->flags &
				   (SymbolFlagsRegularEnum | SymbolFlagsConstEnum)) &&
				  (indexType->symbol != nullptr &&
				   (indexType->flags & TypeFlagsEnumLiteral) &&
				   getParentOfSymbol(indexType->symbol) == objectType->symbol))) {
				return getUnionType({indexInfo->valueType, missingType});
			}
			return indexInfo->valueType;
		}
		if (indexType->flags & TypeFlagsNever) {
			return neverType;
		}
		if (isJSLiteralType(objectType)) {
			return anyType;
		}
		if (accessExpression != nullptr && !isConstEnumObjectType(objectType)) {
			if (isObjectLiteralType(objectType)) {
				if (noImplicitAny &&
					(indexType->flags & (TypeFlagsStringLiteral | TypeFlagsNumberLiteral))) {
					addDiagnostic(createDiagnosticForNode(
						accessExpression, Property_0_does_not_exist_on_type_1,
						{anyToString(indexType->AsLiteralType()->value),
						 TypeToString(objectType)}));
					return undefinedType;
				} else if (indexType->flags & (TypeFlagsNumber | TypeFlagsString)) {
					std::vector<Type*> types;
					for (Symbol* p : objectType->AsStructuredType()->properties) {
						types.push_back(getTypeOfSymbol(p));
					}
					types.push_back(undefinedType);
					return getUnionType(types);
				}
			}
			auto globalIt = (objectType->symbol == globalThisSymbol && hasPropName)
								? globalThisSymbol->exports.find(propName)
								: globalThisSymbol->exports.end();
			if (globalIt != globalThisSymbol->exports.end() &&
				(globalIt->second->flags & SymbolFlagsBlockScoped)) {
				error(accessExpression, Property_0_does_not_exist_on_type_1,
					  {propName, TypeToString(objectType)});
			} else if (noImplicitAny &&
					   !(accessFlags & AccessFlagsSuppressNoImplicitAnyError)) {
				if (hasPropName && typeHasStaticProperty(propName, objectType)) {
					std::string typeName = TypeToString(objectType);
					error(accessExpression,
						  Property_0_does_not_exist_on_type_1_Did_you_mean_to_access_the_static_member_2_instead,
						  {propName, typeName,
						   typeName + "[" +
							   getTextOfNode(
								   accessExpression->as<ElementAccessExpression>()
									   ->ArgumentExpression) +
							   "]"});
				} else if (getIndexTypeOfType(objectType, numberType) != nullptr) {
					error(accessExpression->as<ElementAccessExpression>()->ArgumentExpression,
						  Element_implicitly_has_an_any_type_because_index_expression_is_not_of_type_number);
				} else {
					std::string suggestion;
					if (hasPropName) {
						suggestion = getSuggestionForNonexistentProperty(propName, objectType);
					}
					if (!suggestion.empty()) {
						error(accessExpression->as<ElementAccessExpression>()->ArgumentExpression,
							  Property_0_does_not_exist_on_type_1_Did_you_mean_2,
							  {propName, TypeToString(objectType), suggestion});
					} else {
						suggestion = getSuggestionForNonexistentIndexSignature(
							objectType, accessExpression, indexType);
						if (!suggestion.empty()) {
							error(accessExpression,
								  Element_implicitly_has_an_any_type_because_type_0_has_no_index_signature_Did_you_mean_to_call_1,
								  {TypeToString(objectType), suggestion});
						} else {
							Diagnostic* diagnostic = nullptr;
							if (indexType->flags & TypeFlagsEnumLiteral) {
								diagnostic = NewDiagnosticForNode(
									accessExpression, Property_0_does_not_exist_on_type_1,
									{"[" + TypeToString(indexType) + "]",
									 TypeToString(objectType)});
							} else if (indexType->flags & TypeFlagsUniqueESSymbol) {
								std::string symName =
									getFullyQualifiedName(indexType->symbol, accessExpression);
								diagnostic = NewDiagnosticForNode(
									accessExpression, Property_0_does_not_exist_on_type_1,
									{"[" + symName + "]", TypeToString(objectType)});
							} else if (indexType->flags & TypeFlagsStringLiteral) {
								diagnostic = NewDiagnosticForNode(
									accessExpression, Property_0_does_not_exist_on_type_1,
									{anyToString(indexType->AsLiteralType()->value),
									 TypeToString(objectType)});
							} else if (indexType->flags & TypeFlagsNumberLiteral) {
								diagnostic = NewDiagnosticForNode(
									accessExpression, Property_0_does_not_exist_on_type_1,
									{anyToString(indexType->AsLiteralType()->value),
									 TypeToString(objectType)});
							} else if (indexType->flags & (TypeFlagsNumber | TypeFlagsString)) {
								diagnostic = NewDiagnosticForNode(
									accessExpression,
									No_index_signature_with_a_parameter_of_type_0_was_found_on_type_1,
									{TypeToString(indexType), TypeToString(objectType)});
							}
							addDiagnostic(NewDiagnosticChainForNode(
								diagnostic, accessExpression,
								Element_implicitly_has_an_any_type_because_expression_of_type_0_can_t_be_used_to_index_type_1,
								{TypeToString(fullIndexType), TypeToString(objectType)}));
						}
					}
				}
			}
			return nullptr;
		}
	}
	if ((accessFlags & AccessFlagsAllowMissing) && isObjectLiteralType(objectType)) {
		return undefinedType;
	}
	if (isJSLiteralType(objectType)) {
		return anyType;
	}
	if (accessNode != nullptr) {
		Node* indexNode = getIndexNodeForAccessExpression(accessNode);
		if (indexNode->kind != Kind::BigIntLiteral &&
			(indexType->flags & (TypeFlagsStringLiteral | TypeFlagsNumberLiteral))) {
			error(indexNode, Property_0_does_not_exist_on_type_1,
				  {anyToString(indexType->AsLiteralType()->value), TypeToString(objectType)});
		} else if (indexType->flags & (TypeFlagsString | TypeFlagsNumber)) {
			error(indexNode, Type_0_has_no_matching_index_signature_for_type_1,
				  {TypeToString(objectType), TypeToString(indexType)});
		} else {
			std::string typeString;
			if (indexNode->kind == Kind::BigIntLiteral) {
				typeString = "bigint";
			} else {
				typeString = TypeToString(indexType);
			}
			error(indexNode, Type_0_cannot_be_used_as_an_index_type, {typeString});
		}
	}
	if (isTypeAny(indexType)) {
		return indexType;
	}
	return nullptr;
}

// checker.go:27681
bool Checker::typeHasStaticProperty(const std::string& propName, Type* containingType) {
	if (containingType->symbol != nullptr) {
		Symbol* prop = getPropertyOfType(getTypeOfSymbol(containingType->symbol), propName);
		return prop != nullptr && prop->valueDeclaration != nullptr &&
			isStatic(prop->valueDeclaration);
	}
	return false;
}

// checker.go:27689
std::string Checker::getSuggestionForNonexistentProperty(const std::string& name,
														 Type* containingType) {
	Symbol* symbol = getSpellingSuggestionForName(name, getPropertiesOfType(containingType),
												SymbolFlagsValue);
	if (symbol != nullptr) {
		return symbol->name;
	}
	return "";
}

// checker.go:27697
std::string Checker::getSuggestionForNonexistentIndexSignature(Type* objectType,
															   Node* expr, Type* keyedType) {
	// check if object type has setter or getter
	auto hasProp = [&](const std::string& name) -> bool {
		Symbol* prop = getPropertyOfObjectType(objectType, name);
		if (prop != nullptr) {
			Signature* s = getSingleCallSignature(getTypeOfSymbol(prop));
			return s != nullptr && getMinArgumentCount(s) >= 1 &&
				isTypeAssignableTo(keyedType, getTypeAtPosition(s, 0));
		}
		return false;
	};
	std::string suggestedMethod = isAssignmentTarget(expr) ? "set" : "get";
	if (!hasProp(suggestedMethod)) {
		return "";
	}
	std::string suggestion = tryGetPropertyAccessOrIdentifierToString(expr->expression());
	if (suggestion.empty()) {
		return suggestedMethod;
	}
	return suggestion + "." + suggestedMethod;
}

// checker.go:27718
Type* Checker::getSuggestedTypeForNonexistentStringLiteralType(Type* source,
															   Type* target) {
	std::vector<Type*> candidates;
	for (Type* t : target->types()) {
		if (t->flags & TypeFlagsStringLiteral) {
			candidates.push_back(t);
		}
	}
	return getSpellingSuggestion(getStringLiteralValue(source), candidates,
								 [](Type* t) { return getStringLiteralValue(t); },
								 [](Type* a, Type* b) { return CompareTypes(a, b); },
								 1000);
}

// checker.go:27723
static Node* getIndexNodeForAccessExpression(Node* accessNode) {
	switch (accessNode->kind) {
	case Kind::ElementAccessExpression:
		return accessNode->as<ElementAccessExpression>()->ArgumentExpression;
	case Kind::IndexedAccessType:
		return accessNode->as<IndexedAccessTypeNode>()->IndexType;
	case Kind::ComputedPropertyName:
		return accessNode->expression();
	default:
		break;
	}
	return accessNode;
}

// checker.go:27735
void Checker::errorIfWritingToReadonlyIndex(IndexInfo* indexInfo, Type* objectType,
											Node* accessExpression) {
	if (indexInfo != nullptr && indexInfo->isReadonly && accessExpression != nullptr &&
		(isAssignmentTarget(accessExpression) || isDeleteTarget(accessExpression))) {
		error(accessExpression, Index_signature_in_type_0_only_permits_reading,
			  {TypeToString(objectType)});
	}
}

// checker.go:27741
bool Checker::isSelfTypeAccess(Node* name, Symbol* parent) {
	return name->kind == Kind::ThisKeyword ||
		(parent != nullptr && isEntityNameExpression(name) &&
		 parent == getResolvedSymbol(getFirstIdentifier(name)));
}

// checker.go:27745
bool Checker::isAssignmentToReadonlyEntity(Node* expr, Symbol* symbol,
										   AssignmentKind assignmentKind) {
	if (assignmentKind == AssignmentKindNone) {
		// no assignment means it doesn't matter whether the entity is readonly
		return false;
	}
	if (isAccessExpression(expr)) {
		Node* node = skipParentheses(expr->expression());
		if (isIdentifier(node)) {
			Symbol* expressionSymbol = getResolvedSymbol(node);
			// CommonJS module.exports is never readonly
			if (expressionSymbol->flags & SymbolFlagsModuleExports) {
				return false;
			}
		}
	}
	if (isReadonlySymbol(symbol)) {
		// Allow assignments to readonly properties within constructors of the same class declaration.
		if ((symbol->flags & SymbolFlagsProperty) && isAccessExpression(expr) &&
			expr->expression()->kind == Kind::ThisKeyword) {
			// Look for if this is the constructor for the class that `symbol` is a property of.
			Node* ctor = getControlFlowContainer(expr);
			if (ctor == nullptr || !isConstructorDeclaration(ctor)) {
				return true;
			}
			if (symbol->valueDeclaration != nullptr) {
				bool isAssignmentDeclaration = isBinaryExpression(symbol->valueDeclaration);
				bool isLocalPropertyDeclaration =
					ctor->parent == symbol->valueDeclaration->parent;
				bool isLocalParameterProperty = ctor == symbol->valueDeclaration->parent;
				bool isLocalThisPropertyAssignment =
					isAssignmentDeclaration && symbol->parent->valueDeclaration == ctor->parent;
				bool isLocalThisPropertyAssignmentConstructorFunction =
					isAssignmentDeclaration && symbol->parent->valueDeclaration == ctor;
				bool isWriteableSymbol = isLocalPropertyDeclaration ||
					isLocalParameterProperty || isLocalThisPropertyAssignment ||
					isLocalThisPropertyAssignmentConstructorFunction;
				return !isWriteableSymbol;
			}
		}
		return true;
	}
	if (isAccessExpression(expr)) {
		// references through namespace import should be readonly
		Node* node = skipParentheses(expr->expression());
		if (isIdentifier(node)) {
			Symbol* expressionSymbol = getResolvedSymbol(node);
			if (expressionSymbol->flags & SymbolFlagsAlias) {
				Node* declaration = getDeclarationOfAliasSymbol(expressionSymbol);
				return declaration != nullptr && isNamespaceImport(declaration);
			}
		}
	}
	return false;
}

// checker.go:27794
bool Checker::isThisPropertyAccessInConstructor(Node* node, Symbol* prop) {
	Node* constructor = nullptr;
	auto [kind, location] = isConstructorDeclaredThisProperty(prop);
	if (kind == thisAssignmentDeclarationConstructor) {
		constructor = location;
	} else if (isThisProperty(node) && isAutoTypedProperty(prop)) {
		constructor = getDeclaringConstructor(prop);
	}
	return tsc::getThisContainer(node, true /*includeArrowFunctions*/,
								   false /*includeClassComputedPropertyName*/) == constructor;
}

// checker.go:27804
bool Checker::isAutoTypedProperty(Symbol* symbol) {
	// A property is auto-typed when its declaration has no type annotation or initializer and we're in
	// noImplicitAny mode or a .js file.
	Node* declaration = symbol->valueDeclaration;
	return declaration != nullptr && isPropertyDeclaration(declaration) &&
		declaration->type() == nullptr && declaration->initializer() == nullptr &&
		noImplicitAny;
}

// checker.go:27811
Node* Checker::getDeclaringConstructor(Symbol* symbol) {
	for (Node* declaration : symbol->declarations) {
		Node* container = tsc::getThisContainer(declaration, false /*includeArrowFunctions*/,
												  false /*includeClassComputedPropertyName*/);
		if (container != nullptr && isConstructorDeclaration(container)) {
			return container;
		}
	}
	return nullptr;
}

// checker.go:27821
std::string Checker::getPropertyNameFromIndex(Type* indexType, Node* accessNode) {
	if (isTypeUsableAsPropertyName(indexType)) {
		return getPropertyNameFromType(indexType);
	}
	if (accessNode != nullptr && isPropertyName(accessNode)) {
		return getPropertyNameForPropertyNameNode(accessNode);
	}
	return InternalSymbolNameMissing;
}

// checker.go:27831
bool Checker::isStringIndexSignatureOnlyTypeWorker(Type* t) {
	return ((t->flags & TypeFlagsObject) && !isGenericMappedType(t) &&
			getPropertiesOfType(t).empty() && getIndexInfosOfType(t).size() == 1 &&
			getIndexInfoOfType(t, stringType) != nullptr) ||
		((t->flags & TypeFlagsUnionOrIntersection) != 0 &&
		 everyContainedType(t, isStringIndexSignatureOnlyType));
}

// checker.go:27836
bool Checker::shouldDeferIndexedAccessType(Type* objectType, Type* indexType,
										   Node* accessNode) {
	if (isGenericIndexType(indexType)) {
		return true;
	}
	if (accessNode != nullptr && !isIndexedAccessTypeNode(accessNode)) {
		return isGenericTupleType(objectType) &&
			!indexTypeLessThan(indexType,
							   getTotalFixedElementCount(targetTupleType(objectType)));
	}
	return (isGenericObjectType(objectType) &&
			!(isTupleType(objectType) &&
			  indexTypeLessThan(indexType,
								getTotalFixedElementCount(targetTupleType(objectType))))) ||
		isGenericReducibleType(objectType);
}

// checker.go:27847
static bool indexTypeLessThan(Type* indexType, int limit) {
	return everyType(indexType, [limit](Type* t) {
		if (t->flags & TypeFlagsStringOrNumberLiteral) {
			std::string propName = getPropertyNameFromType(t);
			if (isNumericLiteralName(propName)) {
				Number index = numberFromString(propName);
				return index.v >= 0 && index.v < static_cast<double>(limit);
			}
		}
		return false;
	});
}

// checker.go:27860
Type* Checker::getNoInferType(Type* t) {
	if (isNoInferTargetType(t)) {
		return getOrCreateSubstitutionType(t, unknownType);
	}
	return t;
}

// checker.go:27867
bool Checker::isNoInferTargetType(Type* t) {
	// This is effectively a more conservative and predictable form of couldContainTypeVariables. We want to
	// preserve NoInfer<T> only for types that could contain type variables, but we don't want to exhaustively
	// examine all object type members.
	bool unionOrIntersectionHasTarget = false;
	if (t->flags & TypeFlagsUnionOrIntersection) {
		for (Type* u : t->AsUnionOrIntersectionType()->types) {
			if (isNoInferTargetType(u)) {
				unionOrIntersectionHasTarget = true;
				break;
			}
		}
	}
	return unionOrIntersectionHasTarget ||
		((t->flags & TypeFlagsSubstitution) && !isNoInferType(t) &&
		 isNoInferTargetType(t->AsSubstitutionType()->baseType)) ||
		((t->flags & TypeFlagsObject) && !IsEmptyAnonymousObjectType(t)) ||
		((t->flags & (TypeFlagsInstantiable & ~TypeFlagsSubstitution)) &&
		 !isPatternLiteralType(t));
}

// checker.go:27877
Type* Checker::getSubstitutionType(Type* baseType, Type* constraint) {
	if ((constraint->flags & TypeFlagsAnyOrUnknown) || constraint == baseType ||
		(baseType->flags & TypeFlagsAny)) {
		return baseType;
	}
	return getOrCreateSubstitutionType(baseType, constraint);
}

// checker.go:27884
Type* Checker::getOrCreateSubstitutionType(Type* baseType, Type* constraint) {
	SubstitutionTypeKey key{baseType->id, constraint->id};
	if (auto it = substitutionTypes.find(key); it != substitutionTypes.end()) {
		return it->second;
	}
	Type* result = newSubstitutionType(baseType, constraint);
	substitutionTypes[key] = result;
	return result;
}

// checker.go:27894
Type* Checker::getBaseConstraintOrType(Type* t) {
	Type* constraint = getBaseConstraintOfType(t);
	if (constraint != nullptr) {
		return constraint;
	}
	return t;
}

// checker.go:27902
Type* Checker::getBaseConstraintOfType(Type* t) {
	if ((t->flags & (TypeFlagsInstantiableNonPrimitive | TypeFlagsUnionOrIntersection |
					 TypeFlagsTemplateLiteral | TypeFlagsStringMapping |
					 TypeFlagsIndex)) ||
		isGenericTupleType(t)) {
		Type* constraint = getResolvedBaseConstraint(t, {});
		if (constraint != noConstraintType && constraint != circularConstraintType) {
			return constraint;
		}
		return nullptr;
	}
	return nullptr;
}

// checker.go:27913
Type* Checker::getResolvedBaseConstraint(Type* t, std::vector<RecursionId> stack) {
	ConstrainedType* constrained = t->AsConstrainedType();
	if (constrained == nullptr) {
		return t;
	}
	if (constrained->resolvedBaseConstraint != nullptr) {
		return constrained->resolvedBaseConstraint;
	}
	if (!pushTypeResolution(t, TypeSystemPropertyName::ResolvedBaseConstraint)) {
		return circularConstraintType;
	}
	Type* constraint = nullptr;
	// We always explore at least 10 levels of nested constraints. Thereafter, we continue to explore
	// up to 50 levels of nested constraints provided there are no "deeply nested" types on the stack
	// (i.e. no types for which five instantiations have been recorded on the stack). If we reach 50
	// levels of nesting, we are presumably exploring a repeating pattern with a long cycle that hasn't
	// yet triggered the deeply nested limiter. We have no test cases that actually get to 50 levels of
	// nesting, so it is effectively just a safety stop.
	RecursionId identity = getRecursionIdentity(t, this);
	if (stack.size() < 10 ||
		(stack.size() < 50 &&
		 std::find(stack.begin(), stack.end(), identity) == stack.end())) {
		stack.push_back(identity);
		constraint = computeBaseConstraint(getSimplifiedType(t, false /*writing*/), stack);
	}
	if (!popTypeResolution()) {
		if (t->flags & TypeFlagsTypeParameter) {
			Node* errorNode = getConstraintDeclaration(t);
			if (errorNode != nullptr) {
				Diagnostic* diagnostic =
					error(errorNode, Type_parameter_0_has_a_circular_constraint,
						  {TypeToString(t)});
				if (currentNode != nullptr &&
					!isNodeDescendantOf(errorNode, currentNode) &&
					!isNodeDescendantOf(currentNode, errorNode)) {
					diagnostic->AddRelatedInfo(NewDiagnosticForNode(
						currentNode, Circularity_originates_in_type_at_this_location, {}));
				}
			}
		}
		constraint = circularConstraintType;
	}
	if (constraint == nullptr) {
		constraint = noConstraintType;
	}
	if (constrained->resolvedBaseConstraint == nullptr) {
		constrained->resolvedBaseConstraint = constraint;
	}
	return constraint;
}

// checker.go:27956
Type* Checker::computeBaseConstraint(Type* t, std::vector<RecursionId> stack) {
	if (t->flags & TypeFlagsTypeParameter) {
		Type* constraint = getConstraintFromTypeParameter(t);
		if (t->AsTypeParameter()->isThisType) {
			return constraint;
		}
		return getNextBaseConstraint(constraint, stack);
	}
	if (t->flags & TypeFlagsUnionOrIntersection) {
		const std::vector<Type*>& types = t->types();
		std::vector<Type*> constraints;
		constraints.reserve(types.size());
		bool different = false;
		for (Type* s : types) {
			Type* constraint = getNextBaseConstraint(s, stack);
			if (constraint != nullptr) {
				if (constraint != s) {
					different = true;
				}
				constraints.push_back(constraint);
			} else {
				different = true;
			}
		}
		if (!different) {
			return t;
		}
		if ((t->flags & TypeFlagsUnion) && constraints.size() == types.size()) {
			return getUnionType(constraints);
		}
		if ((t->flags & TypeFlagsIntersection) && !constraints.empty()) {
			return getIntersectionType(constraints);
		}
		return nullptr;
	}
	if (t->flags & TypeFlagsIndex) {
		if (isGenericMappedType(t->AsIndexType()->target)) {
			Type* mappedType = t->AsIndexType()->target;
			if (getNameTypeFromMappedType(mappedType) != nullptr &&
				!isMappedTypeWithKeyofConstraintDeclaration(mappedType)) {
				return getNextBaseConstraint(
					getIndexTypeForMappedType(mappedType, IndexFlagsNone), stack);
			}
		}
		return stringNumberSymbolType;
	}
	if (t->flags & TypeFlagsTemplateLiteral) {
		const std::vector<Type*>& types = t->types();
		std::vector<Type*> constraints;
		constraints.reserve(types.size());
		for (Type* s : types) {
			Type* constraint = getNextBaseConstraint(s, stack);
			if (constraint != nullptr) {
				constraints.push_back(constraint);
			}
		}
		if (constraints.size() == types.size()) {
			return getTemplateLiteralType(t->AsTemplateLiteralType()->texts, constraints);
		}
		return stringType;
	}
	if (t->flags & TypeFlagsStringMapping) {
		Type* constraint = getNextBaseConstraint(t->Target(), stack);
		if (constraint != nullptr && constraint != t->Target()) {
			return getStringMappingType(t->symbol, constraint);
		}
		return stringType;
	}
	if (t->flags & TypeFlagsIndexedAccess) {
		if (isMappedTypeGenericIndexedAccess(t)) {
			// For indexed access types of the form { [P in K]: E }[X], where K is non-generic and X is generic,
			// we substitute an instantiation of E where P is replaced with X.
			return getNextBaseConstraint(
				substituteIndexedMappedType(t->AsIndexedAccessType()->objectType,
											t->AsIndexedAccessType()->indexType),
				stack);
		}
		Type* baseObjectType =
			getNextBaseConstraint(t->AsIndexedAccessType()->objectType, stack);
		Type* baseIndexType =
			getNextBaseConstraint(t->AsIndexedAccessType()->indexType, stack);
		if (baseObjectType == nullptr || baseIndexType == nullptr) {
			return nullptr;
		}
		return getNextBaseConstraint(
			getIndexedAccessTypeOrUndefined(baseObjectType, baseIndexType,
											t->AsIndexedAccessType()->accessFlags,
											nullptr, nullptr),
			stack);
	}
	if (t->flags & TypeFlagsConditional) {
		if (conditionalConstraintDepth >= 100) {
			return nullptr;
		}
		conditionalConstraintDepth++;
		Type* constraint = getConstraintFromConditionalType(t);
		conditionalConstraintDepth--;
		return getNextBaseConstraint(constraint, stack);
	}
	if (t->flags & TypeFlagsSubstitution) {
		return getNextBaseConstraint(getSubstitutionIntersection(t), stack);
	}
	if (isGenericTupleType(t)) {
		// We substitute constraints for variadic elements only when the constraints are array types or
		// non-variadic tuple types as we want to avoid further (possibly unbounded) recursion.
		std::vector<Type*> elementTypes = getElementTypes(t);
		const std::vector<TupleElementInfo>& elementInfos = targetTupleType(t)->elementInfos;
		std::vector<Type*> newElements;
		newElements.reserve(elementTypes.size());
		for (size_t i = 0; i < elementTypes.size(); i++) {
			Type* v = elementTypes[i];
			Type* newElement = v;
			if ((v->flags & TypeFlagsTypeParameter) &&
				(elementInfos[i].flags & ElementFlagsVariadic)) {
				Type* constraint = getNextBaseConstraint(v, stack);
				if (constraint != nullptr && constraint != v &&
					everyType(constraint, [this](Type* n) {
						return isArrayOrTupleType(n) && !isGenericTupleType(n);
					})) {
					newElement = constraint;
				}
			}
			newElements.push_back(newElement);
		}
		return createTupleTypeEx(newElements, elementInfos, targetTupleType(t)->readonly);
	}
	return t;
}

// checker.go:28059
Type* Checker::getNextBaseConstraint(Type* t, const std::vector<RecursionId>& stack) {
	if (t == nullptr) {
		return nullptr;
	}
	Type* constraint = getResolvedBaseConstraint(t, stack);
	if (constraint == noConstraintType || constraint == circularConstraintType) {
		return nullptr;
	}
	return constraint;
}

// Return true if type might be of the given kind. A union or intersection type might be of a given
// kind if at least one constituent type is of the given kind.
// checker.go:28072
bool Checker::maybeTypeOfKind(Type* t, TypeFlags kind) {
	if (t->flags & kind) {
		return true;
	}
	if (t->flags & TypeFlagsUnionOrIntersection) {
		for (Type* u : t->types()) {
			if (maybeTypeOfKind(u, kind)) {
				return true;
			}
		}
	}
	return false;
}

// checker.go:28086
bool Checker::maybeTypeOfKindConsideringBaseConstraint(Type* t, TypeFlags kind) {
	if (maybeTypeOfKind(t, kind)) {
		return true;
	}
	Type* baseConstraint = getBaseConstraintOrType(t);
	return baseConstraint != nullptr && maybeTypeOfKind(baseConstraint, kind);
}

// checker.go:28094
bool Checker::allTypesAssignableToKind(Type* source, TypeFlags kind) {
	return allTypesAssignableToKindEx(source, kind, false);
}

// checker.go:28098
bool Checker::allTypesAssignableToKindEx(Type* source, TypeFlags kind, bool strict) {
	if (source->flags & TypeFlagsUnion) {
		for (Type* subType : source->types()) {
			if (!allTypesAssignableToKindEx(subType, kind, strict)) {
				return false;
			}
		}
		return true;
	}
	return isTypeAssignableToKindEx(source, kind, strict);
}

// checker.go:28107
bool Checker::isTypeAssignableToKind(Type* source, TypeFlags kind) {
	return isTypeAssignableToKindEx(source, kind, false);
}

// checker.go:28111
bool Checker::isTypeAssignableToKindEx(Type* source, TypeFlags kind, bool strict) {
	if (source->flags & kind) {
		return true;
	}
	if (strict &&
		(source->flags &
		 (TypeFlagsAnyOrUnknown | TypeFlagsVoid | TypeFlagsUndefined |
		  TypeFlagsNull))) {
		return false;
	}
	return ((kind & TypeFlagsNumberLike) && isTypeAssignableTo(source, numberType)) ||
		((kind & TypeFlagsBigIntLike) && isTypeAssignableTo(source, bigintType)) ||
		((kind & TypeFlagsStringLike) && isTypeAssignableTo(source, stringType)) ||
		((kind & TypeFlagsBooleanLike) && isTypeAssignableTo(source, booleanType)) ||
		((kind & TypeFlagsVoid) && isTypeAssignableTo(source, voidType)) ||
		((kind & TypeFlagsNever) && isTypeAssignableTo(source, neverType)) ||
		((kind & TypeFlagsNull) && isTypeAssignableTo(source, nullType)) ||
		((kind & TypeFlagsUndefined) && isTypeAssignableTo(source, undefinedType)) ||
		((kind & TypeFlagsESSymbol) && isTypeAssignableTo(source, esSymbolType)) ||
		((kind & TypeFlagsNonPrimitive) && isTypeAssignableTo(source, nonPrimitiveType));
}

// checker.go:28130
static bool isConstEnumObjectType(Type* t) {
	return (t->objectFlags & ObjectFlagsAnonymous) && t->symbol != nullptr &&
		isConstEnumSymbol(t->symbol);
}

// checker.go:28134
static bool isConstEnumSymbol(Symbol* symbol) {
	return symbol->flags & SymbolFlagsConstEnum;
}

// checker.go:28138
Ternary Checker::compareProperties(Symbol* sourceProp, Symbol* targetProp,
								   const std::function<Ternary(Type*, Type*)>& compareTypes) {
	// Two members are considered identical when
	// - they are public properties with identical names, optionality, and types,
	// - they are private or protected properties originating in the same declaration and having identical types
	if (sourceProp == targetProp) {
		return Ternary::True;
	}
	ModifierFlags sourcePropAccessibility =
		getDeclarationModifierFlagsFromSymbol(sourceProp) &
		ModifierFlagsNonPublicAccessibilityModifier;
	ModifierFlags targetPropAccessibility =
		getDeclarationModifierFlagsFromSymbol(targetProp) &
		ModifierFlagsNonPublicAccessibilityModifier;
	if (sourcePropAccessibility != targetPropAccessibility) {
		return Ternary::False;
	}
	if (sourcePropAccessibility != ModifierFlagsNone) {
		if (getTargetSymbol(sourceProp) != getTargetSymbol(targetProp)) {
			return Ternary::False;
		}
	} else {
		if ((sourceProp->flags & SymbolFlagsOptional) !=
			(targetProp->flags & SymbolFlagsOptional)) {
			return Ternary::False;
		}
	}
	if (isReadonlySymbol(sourceProp) != isReadonlySymbol(targetProp)) {
		return Ternary::False;
	}
	return compareTypes(getNonMissingTypeOfSymbol(sourceProp),
						getNonMissingTypeOfSymbol(targetProp));
}

// checker.go:28165
[[maybe_unused]] static Ternary compareTypesEqual(Type* s, Type* t) {
	if (s == t) {
		return Ternary::True;
	}
	return Ternary::False;
}

// checker.go:28172
void Checker::markPropertyAsReferenced(Symbol* prop, Node* nodeForCheckWriteOnly,
									   bool isSelfTypeAccess) {
	if (!(prop->flags & SymbolFlagsClassMember) || prop->valueDeclaration == nullptr) {
		return;
	}
	bool hasPrivateModifier = hasModifier(prop->valueDeclaration, ModifierFlagsPrivate);
	bool hasPrivateIdentifier = prop->valueDeclaration->name() != nullptr &&
		isPrivateIdentifier(prop->valueDeclaration->name());
	if (!hasPrivateModifier && !hasPrivateIdentifier) {
		return;
	}
	if (nodeForCheckWriteOnly != nullptr && isWriteOnlyAccess(nodeForCheckWriteOnly) &&
		!(prop->flags & SymbolFlagsSetAccessor)) {
		return;
	}
	if (isSelfTypeAccess) {
		// Find any FunctionLikeDeclaration because those create a new 'this' binding. But this should only matter for methods (or getters/setters).
		Node* containingMethod = findAncestor(nodeForCheckWriteOnly,
											  [](Node* n) { return isFunctionLikeDeclaration(n); });
		if (containingMethod != nullptr && containingMethod->symbol() == prop) {
			return;
		}
	}
	Symbol* target = prop;
	if (prop->checkFlags & CheckFlagsInstantiated) {
		target = valueSymbolLinks.Get(prop)->target;
	}
	symbolReferenceLinks.Get(target)->referenceKinds |= SymbolFlagsAll;
}

// checker.go:28198
std::vector<Symbol*> Checker::expandSignatureParametersWithTupleMembers(
	Signature* signature, TypeReference* restType, int restIndex, Symbol* restSymbol) {
	std::vector<Type*> elementTypes = getTypeArguments(restType->AsType());
	const std::vector<TupleElementInfo>& elementInfos =
		targetTupleType(restType->AsType())->elementInfos;
	std::vector<std::string> associatedNames =
		getUniqAssociatedNamesFromTupleType(restType, restSymbol);
	std::vector<Symbol*> expanded;
	expanded.reserve(restIndex + elementTypes.size());
	expanded.insert(expanded.end(), signature->parameters.begin(),
					signature->parameters.begin() + restIndex);
	for (size_t i = 0; i < elementTypes.size(); i++) {
		ElementFlags flags = elementInfos[i].flags;
		CheckFlags checkFlags = CheckFlagsNone;
		if (flags & ElementFlagsVariable) {
			checkFlags = CheckFlagsRestParameter;
		} else if (flags & ElementFlagsOptional) {
			checkFlags = CheckFlagsOptionalParameter;
		}
		Symbol* symbol = newSymbolEx(SymbolFlagsFunctionScopedVariable,
									 associatedNames[i], checkFlags);
		auto* links = valueSymbolLinks.Get(symbol);
		if (flags & ElementFlagsRest) {
			links->resolvedType = createArrayType(elementTypes[i]);
		} else {
			links->resolvedType = elementTypes[i];
		}
		expanded.push_back(symbol);
	}
	return expanded;
}

// checker.go:28224
std::vector<std::string> Checker::getUniqAssociatedNamesFromTupleType(TypeReference* t,
																	Symbol* restSymbol) {
	const std::vector<TupleElementInfo>& elementInfos =
		targetTupleType(t->AsType())->elementInfos;
	std::vector<std::string> names(elementInfos.size());
	std::unordered_map<std::string, int> counters;
	for (size_t i = 0; i < elementInfos.size(); i++) {
		names[i] = getTupleElementLabel(elementInfos[i], restSymbol, static_cast<int>(i));
		// count duplicates using negative values
		counters[names[i]]--;
	}
	for (size_t i = 0; i < names.size(); i++) {
		const std::string& name = names[i];
		if (counters[name] == -1) {
			continue;
		}
		for (;;) {
			if (counters[name] < 0) {
				// switch to a positive suffix counter
				counters[name] = 0;
			}
			counters[name]++;
			std::string candidateName = name + "_" + std::to_string(counters[name]);
			if (counters[candidateName] == 0) {
				names[i] = candidateName;
				break;
			}
		}
	}
	return names;
}

// checker.go:28253
[[maybe_unused]] static bool hasRestParameter(Node* signature) {
	std::vector<Node*> params = signature->parameters();
	Node* last = params.empty() ? nullptr : params.back();
	return last != nullptr && isRestParameter(last);
}

// checker.go:28258
static bool isRestParameter(Node* param) {
	return param->as<ParameterDeclaration>()->DotDotDotToken != nullptr;
}

// checker.go:28262
[[maybe_unused]] static std::string getNameFromIndexInfo(IndexInfo* info) {
	if (info->declaration != nullptr) {
		return declarationNameToString(info->declaration->parameters()[0]->name());
	}
	return "x";
}

// checker.go:28269
bool Checker::isUnknownLikeUnionType(Type* t) {
	if (strictNullChecks && (t->flags & TypeFlagsUnion)) {
		if (!(t->objectFlags & ObjectFlagsIsUnknownLikeUnionComputed)) {
			t->objectFlags |= ObjectFlagsIsUnknownLikeUnionComputed;
			const std::vector<Type*>& types = t->types();
			if (types.size() >= 3 && (types[0]->flags & TypeFlagsUndefined) &&
				(types[1]->flags & TypeFlagsNull)) {
				for (Type* u : types) {
					if (IsEmptyAnonymousObjectType(u)) {
						t->objectFlags |= ObjectFlagsIsUnknownLikeUnion;
						break;
					}
				}
			}
		}
		return (t->objectFlags & ObjectFlagsIsUnknownLikeUnion) != 0;
	}
	return false;
}

// Return true the given type is a primitive union type where no two literal type constituents are
// comparable. Specifically, that means (a) the union doesn't contain literals from different enum
// types, and (b) the union doesn't contain both enum literals and string or number literals.
// checker.go:28286
bool Checker::isUniformUnionType(Type* t) {
	if (t->objectFlags & ObjectFlagsPrimitiveUnion) {
		if (!(t->objectFlags & ObjectFlagsIsUniformEnumComputed)) {
			t->objectFlags |= ObjectFlagsIsUniformEnumComputed |
				(computeIsUniformUnionType(t->types()) ? ObjectFlagsIsUniformEnum
													   : ObjectFlagsNone);
		}
		return (t->objectFlags & ObjectFlagsIsUniformEnum) != 0;
	}
	return false;
}

// checker.go:28296
bool Checker::computeIsUniformUnionType(const std::vector<Type*>& types) {
	Symbol* enumSymbol = nullptr;
	bool hasStringOrNumberLiteral = false;
	for (Type* t : types) {
		if (t->flags & TypeFlagsEnumLike) {
			if (hasStringOrNumberLiteral) {
				return false;
			}
			Symbol* parent = getParentOfSymbol(t->symbol);
			if (enumSymbol == nullptr) {
				enumSymbol = parent;
			} else if (enumSymbol != parent) {
				return false;
			}
		} else if (t->flags & TypeFlagsStringOrNumberLiteral) {
			if (enumSymbol != nullptr) {
				return false;
			}
			hasStringOrNumberLiteral = true;
		}
	}
	return true;
}

// checker.go:28320
bool Checker::containsUndefinedType(Type* t) {
	if (t->flags & TypeFlagsUnion) {
		t = t->types()[0];
	}
	return (t->flags & TypeFlagsUndefined) != 0;
}

// checker.go:28327
bool Checker::typeHasCallOrConstructSignatures(Type* t) {
	return (t->flags & TypeFlagsStructuredType) &&
		!resolveStructuredTypeMembers(t)->signatures.empty();
}

// checker.go:28331
Type* Checker::getNormalizedType(Type* t, bool writing) {
	for (;;) {
		Type* n;
		if (isFreshLiteralType(t)) {
			n = t->AsLiteralType()->regularType;
		} else if (isGenericTupleType(t)) {
			n = getNormalizedTupleType(t, writing);
		} else if (t->objectFlags & ObjectFlagsReference) {
			if (t->AsTypeReference()->node != nullptr) {
				n = createTypeReference(t->Target(), getTypeArguments(t));
			} else {
				n = getSingleBaseForNonAugmentingSubtype(t);
				if (n == nullptr) {
					n = t;
				}
			}
		} else if (t->flags & TypeFlagsUnionOrIntersection) {
			n = getNormalizedUnionOrIntersectionType(t, writing);
		} else if (t->flags & TypeFlagsSubstitution) {
			if (writing) {
				n = t->AsSubstitutionType()->baseType;
			} else {
				n = getSubstitutionIntersection(t);
			}
		} else if (t->flags & TypeFlagsSimplifiable) {
			n = getSimplifiedType(t, writing);
		} else {
			return t;
		}
		if (n == t) {
			return n;
		}
		t = n;
	}
}

// checker.go:28368
Type* Checker::getSimplifiedType(Type* t, bool writing) {
	if (t->flags & TypeFlagsIndexedAccess) {
		return getSimplifiedIndexedAccessType(t, writing);
	}
	if (t->flags & TypeFlagsConditional) {
		return getSimplifiedConditionalType(t, writing);
	}
	return t;
}

// Transform an indexed access to a simpler form, if possible. Return the simpler form, or return
// the type itself if no transformation is possible. The writing flag indicates that the type is
// the target of an assignment.
// checker.go:28381
Type* Checker::getSimplifiedIndexedAccessType(Type* t, bool writing) {
	CachedTypeKey key{writing ? CachedTypeKind::IndexedAccessForWriting
							  : CachedTypeKind::IndexedAccessForReading,
					  t->id};
	if (Type* cached = cachedTypes[key]; cached != nullptr) {
		return cached == circularConstraintType ? t : cached;
	}
	cachedTypes[key] = t;
	Type* result = getSimplifiedIndexedAccessTypeWorker(t, writing);
	if (result != t) {
		// If the simplification is a union type that includes t, remove t from the type.
		result = removeType(result, t);
		cachedTypes[key] = result;
	}
	return result;
}

// checker.go:28396
Type* Checker::getSimplifiedIndexedAccessTypeWorker(Type* t, bool writing) {
	// We recursively simplify the object type as it may in turn be an indexed access type. For example, with
	// '{ [P in T]: { [Q in U]: number } }[T][U]' we want to first simplify the inner indexed access type.
	Type* objectType = getSimplifiedType(t->AsIndexedAccessType()->objectType, writing);
	Type* indexType = getSimplifiedType(t->AsIndexedAccessType()->indexType, writing);
	// T[A | B] -> T[A] | T[B] (reading)
	// T[A | B] -> T[A] & T[B] (writing)
	Type* distributedOverIndex = distributeObjectOverIndexType(objectType, indexType, writing);
	if (distributedOverIndex != nullptr) {
		return distributedOverIndex;
	}
	// Only do the inner distributions if the index can no longer be instantiated to cause index distribution again
	if (!(indexType->flags & TypeFlagsInstantiable)) {
		// (T | U)[K] -> T[K] | U[K] (reading)
		// (T | U)[K] -> T[K] & U[K] (writing)
		// (T & U)[K] -> T[K] & U[K]
		Type* distributedOverObject =
			distributeIndexOverObjectType(objectType, indexType, writing);
		if (distributedOverObject != nullptr) {
			return distributedOverObject;
		}
	}
	// So ultimately (reading):
	// ((A & B) | C)[K1 | K2] -> ((A & B) | C)[K1] | ((A & B) | C)[K2] -> (A & B)[K1] | C[K1] | (A & B)[K2] | C[K2] -> (A[K1] & B[K1]) | C[K1] | (A[K2] & B[K2]) | C[K2]
	// A generic tuple type indexed by a number exists only when the index type doesn't select a
	// fixed element. We simplify to either the combined type of all elements (when the index type
	// the actual number type) or to the combined type of all non-fixed elements.
	if (isGenericTupleType(objectType) && (indexType->flags & TypeFlagsNumberLike)) {
		Type* elementType = getElementTypeOfSliceOfTupleType(
			objectType,
			(indexType->flags & TypeFlagsNumber) ? 0 : targetTupleType(objectType)->fixedLength,
			0 /*endSkipCount*/, writing, false);
		if (elementType != nullptr) {
			return elementType;
		}
	}
	// If the object type is a mapped type { [P in K]: E }, where K is generic, or { [P in K as N]: E }, where
	// K is generic and N is assignable to P, instantiate E using a mapper that substitutes the index type for P.
	// For example, for an index access { [P in K]: Box<T[P]> }[X], we construct the type Box<T[X]>.
	if (isGenericMappedType(objectType)) {
		if (getMappedTypeNameTypeKind(objectType) != MappedTypeNameTypeKind::Remapping) {
			return mapType(substituteIndexedMappedType(objectType, t->AsIndexedAccessType()->indexType),
						   [&](Type* t) { return getSimplifiedType(t, writing); });
		}
	}
	return t;
}

// checker.go:28441
Type* Checker::distributeObjectOverIndexType(Type* objectType, Type* indexType, bool writing) {
	// T[A | B] -> T[A] | T[B] (reading)
	// T[A | B] -> T[A] & T[B] (writing)
	if (indexType->flags & TypeFlagsUnion) {
		std::vector<Type*> types;
		types.reserve(indexType->types().size());
		for (Type* u : indexType->types()) {
			types.push_back(getSimplifiedType(getIndexedAccessType(objectType, u), writing));
		}
		if (writing) {
			return getIntersectionType(types);
		}
		return getUnionType(types);
	}
	return nullptr;
}

// checker.go:28456
Type* Checker::distributeIndexOverObjectType(Type* objectType, Type* indexType, bool writing) {
	// (T | U)[K] -> T[K] | U[K] (reading)
	// (T | U)[K] -> T[K] & U[K] (writing)
	// (T & U)[K] -> T[K] & U[K]
	if ((objectType->flags & TypeFlagsUnion) ||
		((objectType->flags & TypeFlagsIntersection) &&
		 !shouldDeferIndexType(objectType, IndexFlagsNone))) {
		std::vector<Type*> types;
		types.reserve(objectType->types().size());
		for (Type* u : objectType->types()) {
			types.push_back(getSimplifiedType(getIndexedAccessType(u, indexType), writing));
		}
		if ((objectType->flags & TypeFlagsIntersection) || writing) {
			return getIntersectionType(types);
		}
		return getUnionType(types);
	}
	return nullptr;
}

// checker.go:28472
Type* Checker::getSimplifiedConditionalType(Type* t, bool writing) {
	Type* checkType = t->AsConditionalType()->checkType;
	Type* extendsType = t->AsConditionalType()->extendsType;
	Type* trueType = getTrueTypeFromConditionalType(t);
	Type* falseType = getFalseTypeFromConditionalType(t);
	// Simplifications for types of the form `T extends U ? T : never` and `T extends U ? never : T`.
	if ((falseType->flags & TypeFlagsNever) &&
		getActualTypeVariable(trueType) == getActualTypeVariable(checkType)) {
		if ((checkType->flags & TypeFlagsAny) ||
			isTypeAssignableTo(getRestrictiveInstantiation(checkType),
							   getRestrictiveInstantiation(extendsType))) {
			return getSimplifiedType(trueType, writing);
		} else if (isIntersectionEmpty(checkType, extendsType)) {
			return neverType;
		}
	} else if ((trueType->flags & TypeFlagsNever) &&
			   getActualTypeVariable(falseType) == getActualTypeVariable(checkType)) {
		if (!(checkType->flags & TypeFlagsAny) &&
			isTypeAssignableTo(getRestrictiveInstantiation(checkType),
							   getRestrictiveInstantiation(extendsType))) {
			return neverType;
		} else if ((checkType->flags & TypeFlagsAny) ||
				   isIntersectionEmpty(checkType, extendsType)) {
			return getSimplifiedType(falseType, writing);
		}
	}
	return t;
}

// Invokes union simplification logic to determine if an intersection is considered empty as a union constituent
// checker.go:28495
bool Checker::isIntersectionEmpty(Type* type1, Type* type2) {
	return (getUnionType({intersectTypes(type1, type2), neverType})->flags &
			TypeFlagsNever) != 0;
}

// checker.go:28499
Type* Checker::getSimplifiedTypeOrConstraint(Type* t) {
	if (Type* simplified = getSimplifiedType(t, false /*writing*/); simplified != t) {
		return simplified;
	}
	return getConstraintOfType(t);
}

// checker.go:28506
Type* Checker::getNormalizedUnionOrIntersectionType(Type* t, bool writing) {
	if (Type* reduced = getReducedType(t); reduced != t) {
		return reduced;
	}
	if ((t->flags & TypeFlagsIntersection) && shouldNormalizeIntersection(t)) {
		// Normalization handles cases like
		// Partial<T>[K] & ({} | null) ==>
		// Partial<T>[K] & {} | Partial<T>[K} & null ==>
		// (T[K] | undefined) & {} | (T[K] | undefined) & null ==>
		// T[K] & {} | undefined & {} | T[K] & null | undefined & null ==>
		// T[K] & {} | T[K] & null
		const std::vector<Type*>& types = t->types();
		std::vector<Type*> normalizedTypes;
		normalizedTypes.reserve(types.size());
		bool same = true;
		for (Type* u : types) {
			Type* n = getNormalizedType(u, writing);
			if (n != u) {
				same = false;
			}
			normalizedTypes.push_back(n);
		}
		if (!same) {
			return getIntersectionType(normalizedTypes);
		}
	}
	return t;
}

// checker.go:28526
bool Checker::shouldNormalizeIntersection(Type* t) {
	bool hasInstantiable = false;
	bool hasNullableOrEmpty = false;
	for (Type* u : t->types()) {
		hasInstantiable = hasInstantiable || (u->flags & TypeFlagsInstantiable);
		hasNullableOrEmpty = hasNullableOrEmpty || (u->flags & TypeFlagsNullable) ||
			IsEmptyAnonymousObjectType(u);
		if (hasInstantiable && hasNullableOrEmpty) {
			return true;
		}
	}
	return false;
}

// checker.go:28539
Type* Checker::getNormalizedTupleType(Type* t, bool writing) {
	std::vector<Type*> elements = getElementTypes(t);
	std::vector<Type*> normalizedElements;
	normalizedElements.reserve(elements.size());
	bool same = true;
	for (Type* e : elements) {
		Type* n = e;
		if (e->flags & TypeFlagsSimplifiable) {
			n = getSimplifiedType(e, writing);
		}
		if (n != e) {
			same = false;
		}
		normalizedElements.push_back(n);
	}
	if (!same) {
		return createNormalizedTupleType(t->Target(), normalizedElements);
	}
	return t;
}

// checker.go:28553
Type* Checker::getSingleBaseForNonAugmentingSubtype(Type* t) {
	if (!(t->objectFlags & ObjectFlagsReference) ||
		!(t->Target()->objectFlags & ObjectFlagsClassOrInterface)) {
		return nullptr;
	}
	CachedTypeKey key{CachedTypeKind::EquivalentBaseType, t->id};
	if (t->objectFlags & ObjectFlagsIdenticalBaseTypeCalculated) {
		return cachedTypes[key];
	}
	t->objectFlags |= ObjectFlagsIdenticalBaseTypeCalculated;
	Type* target = t->Target();
	if (target->objectFlags & ObjectFlagsClass) {
		Node* baseTypeNode = getBaseTypeNodeOfClass(target);
		// A base type expression may circularly reference the class itself (e.g. as an argument to function call), so we only
		// check for base types specified as simple qualified names.
		if (baseTypeNode != nullptr && !isIdentifier(baseTypeNode->expression()) &&
			!isPropertyAccessExpression(baseTypeNode->expression())) {
			return nullptr;
		}
	}
	std::vector<Type*> bases = getBaseTypes(target);
	if (bases.size() != 1) {
		return nullptr;
	}
	if (!getMembersOfSymbol(t->symbol).empty()) {
		// If the interface has any members, they may subtype members in the base, so we should do a full structural comparison
		return nullptr;
	}
	Type* instantiatedBase;
	std::vector<Type*> typeParameters = interfaceTypeTypeParameters(target->AsInterfaceType());
	std::vector<Type*> typeArguments = getTypeArguments(t);
	if (typeParameters.empty()) {
		instantiatedBase = bases[0];
	} else {
		instantiatedBase = instantiateType(
			bases[0],
			newTypeMapper(typeParameters,
						  std::vector<Type*>(typeArguments.begin(),
											 typeArguments.begin() + typeParameters.size())));
	}
	if (typeArguments.size() > typeParameters.size()) {
		instantiatedBase =
			getTypeWithThisArgument(instantiatedBase, typeArguments.back(), false);
	}
	cachedTypes[key] = instantiatedBase;
	return instantiatedBase;
}

// checker.go:28593
Type* Checker::getModifiersTypeFromMappedType(Type* t) {
	MappedType* m = t->AsMappedType();
	if (m->modifiersType == nullptr) {
		if (isMappedTypeWithKeyofConstraintDeclaration(t)) {
			// If the constraint declaration is a 'keyof T' node, the modifiers type is T. We check
			// AST nodes here because, when T is a non-generic type, the logic below eagerly resolves
			// 'keyof T' to a literal union type and we can't recover T from that type.
			m->modifiersType = instantiateType(
				getTypeFromTypeNode(getConstraintDeclarationForMappedType(t)->type()),
				m->mapper);
		} else {
			// Otherwise, get the declared constraint type, and if the constraint type is a type parameter,
			// get the constraint of that type parameter. If the resulting type is an indexed type 'keyof T',
			// the modifiers type is T. Otherwise, the modifiers type is unknown.
			Type* declaredType = getTypeFromMappedTypeNode(m->declaration);
			Type* constraint = getConstraintTypeFromMappedType(declaredType);
			Type* extendedConstraint = constraint;
			if (constraint != nullptr && (constraint->flags & TypeFlagsTypeParameter)) {
				extendedConstraint =
					getConstraintOfTypeParameter(getNonDistributedTypeParameter(constraint));
			}
			if (extendedConstraint != nullptr && (extendedConstraint->flags & TypeFlagsIndex)) {
				m->modifiersType =
					instantiateType(extendedConstraint->AsIndexType()->target, m->mapper);
			} else {
				m->modifiersType = unknownType;
			}
		}
	}
	return m->modifiersType;
}

// checker.go:28621
Type* Checker::extractTypesOfKind(Type* t, TypeFlags kind) {
	return filterType(t, [kind](Type* t) { return (t->flags & kind) != 0; });
}

// checker.go:28625
Type* Checker::getRegularTypeOfObjectLiteral(Type* t) {
	if (!(isObjectLiteralType(t) && (t->objectFlags & ObjectFlagsFreshLiteral))) {
		return t;
	}
	CachedTypeKey key{CachedTypeKind::RegularObjectLiteral, t->id};
	if (Type* cached = cachedTypes[key]; cached != nullptr) {
		return cached;
	}
	StructuredType* resolved = resolveStructuredTypeMembers(t);
	SymbolTable members = transformTypeOfMembers(
		t, [this](Type* t) { return getRegularTypeOfObjectLiteral(t); });
	std::vector<Signature*> callSignatures(
		resolved->signatures.begin(),
		resolved->signatures.begin() + resolved->callSignatureCount);
	std::vector<Signature*> constructSignatures(
		resolved->signatures.begin() + resolved->callSignatureCount,
		resolved->signatures.end());
	Type* regular = newAnonymousType(t->symbol, members, callSignatures,
									 constructSignatures, resolved->indexInfos);
	regular->flags = resolved->type_.flags;
	regular->objectFlags |= resolved->type_.objectFlags & ~ObjectFlagsFreshLiteral;
	cachedTypes[key] = regular;
	return regular;
}

// checker.go:28642
SymbolTable Checker::transformTypeOfMembers(Type* t,
											const std::function<Type*(Type*)>& f) {
	SymbolTable members;
	for (Symbol* property : getPropertiesOfObjectType(t)) {
		Type* original = getTypeOfSymbol(property);
		Type* updated = f(original);
		if (updated != original) {
			property = createSymbolWithType(property, updated);
		}
		members[property->name] = property;
	}
	return members;
}

// ---------------------------------------------------------------------------
// === dep stubs — removed when owner slice lands ===
// Callees owned by other slices: ONE TSC_UNREACHABLE body each, tagged with the
// owner. Delete the stub (not the decl) when the owner's real definition lands.
// ---------------------------------------------------------------------------

// owner: members slice

// owner: relater slice

// owner: contextual slice


// utilities.go:1807 isJSLiteralType
bool Checker::isJSLiteralType(Type* t) {
	if (noImplicitAny) {
		// Flag is meaningless under `noImplicitAny` mode
		return false;
	}
	if (t->objectFlags & ObjectFlagsJSLiteral) {
		return true;
	}
	if (t->flags & TypeFlagsUnion) {
		return std::all_of(t->AsUnionType()->types.begin(),
						   t->AsUnionType()->types.end(),
						   [this](Type* u) { return isJSLiteralType(u); });
	}
	if (t->flags & TypeFlagsIntersection) {
		return std::any_of(t->AsIntersectionType()->types.begin(),
						   t->AsIntersectionType()->types.end(),
						   [this](Type* u) { return isJSLiteralType(u); });
	}
	if (t->flags & TypeFlagsInstantiable) {
		Type* constraint = getResolvedBaseConstraint(t, {});
		return constraint != t && isJSLiteralType(constraint);
	}
	return false;
}
// checker.go:14288 — isDeprecatedSymbol (symboltype slice owner)
bool Checker::isDeprecatedSymbol(Symbol* symbol) {
	auto isDeprecated = [](Node* d, Checker* c) { return c->IsDeprecatedDeclaration(d); };
	Symbol* parentSymbol = getParentOfSymbol(symbol);
	if (parentSymbol != nullptr && symbol->declarations.size() > 1) {
		if ((parentSymbol->flags & SymbolFlagsInterface) != 0) {
			return std::any_of(symbol->declarations.begin(), symbol->declarations.end(),
							   [&](Node* d) { return IsDeprecatedDeclaration(d); });
		}
		return std::all_of(symbol->declarations.begin(), symbol->declarations.end(),
						   [&](Node* d) { return IsDeprecatedDeclaration(d); });
	}
	return (symbol->valueDeclaration != nullptr && IsDeprecatedDeclaration(symbol->valueDeclaration)) ||
		(!symbol->declarations.empty() &&
		 std::all_of(symbol->declarations.begin(), symbol->declarations.end(),
					 [&](Node* d) { return IsDeprecatedDeclaration(d); }));
}

// owner: flow slice

// owner: typenodes slice

// owner: instantiate slice

// owner: genericity slice

// owner: inference slice

} // namespace tsc::checker
