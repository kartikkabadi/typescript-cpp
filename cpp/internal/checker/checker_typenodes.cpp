// ---------------------------------------------------------------------------
// checker_typenodes.cpp — port of tsc/internal/checker/checker.go:23220-25738
//
// === slice: typenodes ===
//
// The type-node -> Type resolution layer: getTypeFromTypeNodeWorker and every
// getTypeFrom*Node function, tuple-type machinery, conditional/infer type
// machinery, mapped/generic type predicates, import-type resolution, and the
// small global-type constructors at the tail of the range.
//
// Conventions per PORTING.md: Go identifiers kept, `c.foo()` -> `foo()`,
// `*Type` -> `Type*`, `nil` -> `nullptr`. Functions already ported with real
// bodies in checker.cpp (the declared-type block, type constructors, and
// tail helpers) are NOT duplicated here. Callees owned by other slices get
// TSC_UNREACHABLE dep stubs at the bottom.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/checker/types.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/jsnum/jsnum.h"
#include "internal/scanner/scanner.h"

namespace tsc {
namespace checker {

// Declarations for checker-package free functions already defined (external
// linkage) in checker.cpp — declared here so this TU can call them.
bool someType(Type* t, const std::function<bool(Type*)>& f);
bool everyType(Type* t, const std::function<bool(Type*)>& f);
Type* getNonDistributedTypeParameter(Type* t);

namespace {

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

CacheKey getTypeListKey(const std::vector<Type*>& types) {
	keyBuilder b;
	b.writeTypes(types);
	return b.hash();
}

CacheKey getAliasKey(TypeAlias* alias) {
	keyBuilder b;
	b.writeAlias(alias);
	return b.hash();
}

CacheKey getTypeInstantiationKey(const std::vector<Type*>& typeArguments,
								 TypeAlias* alias, bool singleSignature) {
	keyBuilder b;
	b.writeTypes(typeArguments);
	b.writeAlias(alias);
	if (singleSignature) {
		b.writeByte('!');
	}
	return b.hash();
}

CacheKey getTypeAliasInstantiationKey(const std::vector<Type*>& typeArguments,
									  TypeAlias* alias) {
	return getTypeInstantiationKey(typeArguments, alias, false);
}

CacheKey getConditionalTypeKey(const std::vector<Type*>& typeArguments,
							   TypeAlias* alias, bool forConstraint) {
	keyBuilder b;
	b.writeTypes(typeArguments);
	b.writeAlias(alias);
	if (forConstraint) {
		b.writeByte('!');
	}
	return b.hash();
}

CacheKey getTupleKey(const std::vector<TupleElementInfo>& elementInfos,
					 bool readonly) {
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
// List helpers — per-file equivalents of core.Map/Filter/Some/Every/Find etc.
// ---------------------------------------------------------------------------

template <class T, class F>
auto mapList(const std::vector<T>& v, F&& f)
	-> std::vector<decltype(f(v.front()))> {
	using R = decltype(f(v.front()));
	std::vector<R> result;
	result.reserve(v.size());
	for (const T& t : v) {
		result.push_back(f(t));
	}
	return result;
}

template <class T, class F>
auto mapIndexList(const std::vector<T>& v, F&& f)
	-> std::vector<decltype(f(v.front(), 0))> {
	using R = decltype(f(v.front(), 0));
	std::vector<R> result;
	result.reserve(v.size());
	for (size_t i = 0; i < v.size(); i++) {
		result.push_back(f(v[i], static_cast<int>(i)));
	}
	return result;
}

template <class T, class F>
std::vector<T> filterList(const std::vector<T>& v, F&& f) {
	std::vector<T> result;
	for (const T& t : v) {
		if (f(t)) {
			result.push_back(t);
		}
	}
	return result;
}

template <class T, class F>
bool someList(const std::vector<T>& v, F&& f) {
	for (const T& t : v) {
		if (f(t)) {
			return true;
		}
	}
	return false;
}

template <class T, class F>
bool everyList(const std::vector<T>& v, F&& f) {
	for (const T& t : v) {
		if (!f(t)) {
			return false;
		}
	}
	return true;
}

template <class T, class F>
T findList(const std::vector<T>& v, F&& f) {
	for (const T& t : v) {
		if (f(t)) {
			return t;
		}
	}
	return T{};
}

template <class T, class F>
int countWhereList(const std::vector<T>& v, F&& f) {
	int n = 0;
	for (const T& t : v) {
		if (f(t)) {
			n++;
		}
	}
	return n;
}

template <class T>
std::vector<T> appendIfUniqueList(std::vector<T> v, T value) {
	if (std::find(v.begin(), v.end(), value) == v.end()) {
		v.push_back(value);
	}
	return v;
}

template <class T>
std::vector<T> replaceElementList(const std::vector<T>& v, size_t i, T t) {
	std::vector<T> result = v;
	result[i] = t;
	return result;
}

// ---------------------------------------------------------------------------
// Type helpers — mirror Type methods in types.go not yet ported as methods.
// ---------------------------------------------------------------------------

// Type.Target — checker.go/types.go:752
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

InterfaceType* targetInterfaceType(Type* t) {
	return t->AsTypeReference()->target->AsInterfaceType();
}

TupleType* targetTupleType(Type* t) {
	return t->AsTypeReference()->target->AsTupleType();
}

// ---------------------------------------------------------------------------
// Small ast/checker helpers used by this slice.
// ---------------------------------------------------------------------------

// isTypeAlias — checker/utilities.go:260
bool isTypeAlias(Node* node) { return isTypeOrJSTypeAliasDeclaration(node); }

// createSymbolTable — checker/utilities.go:350
SymbolTable createSymbolTable(const std::vector<Symbol*>& symbols) {
	SymbolTable result;
	for (Symbol* symbol : symbols) {
		result[symbol->name] = symbol;
	}
	return result;
}

// isNumericLiteralName — checker/utilities.go:941
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

// isVarConst — ast.IsVarConst
bool isVarConst(Node* node) {
	return (getCombinedNodeFlags(node) & NodeFlagsBlockScoped) == NodeFlagsConst;
}

// isVariableDeclarationInVariableStatement — checker/utilities.go:1011
bool isVariableDeclarationInVariableStatement(Node* node) {
	return isVariableDeclarationList(node->parent) &&
		   isVariableStatement(node->parent->parent);
}

// hasReadonlyModifier — checker/utilities.go
bool hasReadonlyModifier(Node* node) {
	return hasSyntacticModifier(node, ModifierFlagsReadonly);
}

// isValidESSymbolDeclaration — checker/utilities.go:1004
bool isValidESSymbolDeclaration(Node* node) {
	if (isVariableDeclaration(node)) {
		return isVarConst(node) && node->name() != nullptr &&
			   isIdentifier(node->name()) &&
			   isVariableDeclarationInVariableStatement(node);
	}
	if (isPropertyDeclaration(node)) {
		return hasReadonlyModifier(node) && hasStaticModifier(node);
	}
	return isPropertySignatureDeclaration(node) && hasReadonlyModifier(node);
}

// WalkUpParenthesizedTypes — ast/utilities.go:853
Node* walkUpParenthesizedTypes(Node* node) {
	while (node != nullptr && node->kind == Kind::ParenthesizedType) {
		node = node->parent;
	}
	return node;
}

// IsTypeReferenceType — ast/utilities.go:3073
bool isTypeReferenceType(Node* node) {
	return node->kind == Kind::TypeReference ||
		   node->kind == Kind::ExpressionWithTypeArguments;
}

// GetContainingFunction — ast/utilities.go:4191
Node* getContainingFunction(Node* node) {
	return findAncestor(node->parent, [](Node* n) { return isFunctionLike(n); });
}

// intrinsicTypeKinds — checker.go:360
const std::unordered_map<std::string, IntrinsicTypeKind> intrinsicTypeKinds = {
	{"Uppercase", IntrinsicTypeKind::Uppercase},
	{"Lowercase", IntrinsicTypeKind::Lowercase},
	{"Capitalize", IntrinsicTypeKind::Capitalize},
	{"Uncapitalize", IntrinsicTypeKind::Uncapitalize},
	{"NoInfer", IntrinsicTypeKind::NoInfer},
};

// ---------------------------------------------------------------------------
// File-local free functions from the range (Go package-level funcs).
// ---------------------------------------------------------------------------

bool isSimpleIdentifierTypeReference(Node* node) {
	return isTypeReferenceNode(node) &&
		   isIdentifier(node->as<TypeReferenceNode>()->TypeName) &&
		   node->typeArgumentList() == nullptr;
}

std::string getSymbolPath(Symbol* symbol) {
	if (symbol->parent != nullptr) {
		return getSymbolPath(symbol->parent) + "." + symbol->name;
	}
	return symbol->name;
}

// getTypeReferenceName — checker.go:24150 (also ported statically in
// checker.cpp; duplicated here so this TU stays self-contained)
Node* getTypeReferenceName(Node* node) {
	switch (node->kind) {
	case Kind::TypeReference:
		return node->as<TypeReferenceNode>()->TypeName;
	case Kind::ExpressionWithTypeArguments:
		// We only support expressions that are simple qualified names. For
		// other expressions this produces nil
		if (Node* expr = node->expression(); isEntityNameExpression(expr)) {
			return expr;
		}
		break;
	default:
		break;
	}
	return nullptr;
}

bool isLocalTypeAlias(Symbol* symbol) {
	Node* declaration = findList(symbol->declarations,
		[](Node* d) { return isTypeAlias(d); });
	return declaration != nullptr && getContainingFunction(declaration) != nullptr;
}

bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) &&
		   (typeTarget(t)->objectFlags & ObjectFlagsTuple);
}

bool isMutableTupleType(Type* t) {
	return isTupleType(t) && !targetTupleType(t)->readonly;
}

// Free-function isGenericTupleType (checker.go:23953) — inside Checker member
// functions the member overload wins, matching Go's c.isGenericTupleType.
bool isGenericTupleType(Type* t) {
	return isTupleType(t) &&
		   (targetTupleType(t)->combinedFlags & ElementFlagsVariadic);
}

bool isSingleElementGenericTupleType(Type* t) {
	return isGenericTupleType(t) &&
		   targetTupleType(t)->elementInfos.size() == 1;
}

bool isUnaryTupleTypeNode(Node* node) {
	return isTupleTypeNode(node) && node->elements().size() == 1;
}

// Return count of starting consecutive tuple elements of the given kind(s)
int getStartElementCount(TupleType* t, ElementFlags flags) {
	for (size_t i = 0; i < t->elementInfos.size(); i++) {
		if (!(t->elementInfos[i].flags & flags)) {
			return static_cast<int>(i);
		}
	}
	return static_cast<int>(t->elementInfos.size());
}

// Return count of ending consecutive tuple elements of the given kind(s)
int getEndElementCount(TupleType* t, ElementFlags flags) {
	for (size_t i = t->elementInfos.size(); i > 0; i--) {
		if (!(t->elementInfos[i - 1].flags & flags)) {
			return static_cast<int>(t->elementInfos.size() - i);
		}
	}
	return static_cast<int>(t->elementInfos.size());
}

int getTotalFixedElementCount(TupleType* t) {
	return t->fixedLength + getEndElementCount(t, ElementFlagsFixed);
}

}  // namespace

// ---------------------------------------------------------------------------
// Type-node -> Type resolution — checker.go:23220-25738, in file order.
// ---------------------------------------------------------------------------

Type* Checker::tryGetTypeFromTypeNode(Node* node) {
	Node* typeNode = node->type();
	if (typeNode != nullptr) {
		return getTypeFromTypeNode(typeNode);
	}
	return nullptr;
}

Type* Checker::getTypeFromTypeNode(Node* node) {
	return getConditionalFlowTypeOfType(getTypeFromTypeNodeWorker(node), node);
}

Type* Checker::getTypeFromTypeNodeWorker(Node* node) {
	switch (node->kind) {
	case Kind::AnyKeyword:
	case Kind::JSDocAllType:
		return anyType;
	case Kind::JSDocNonNullableType:
		return getTypeFromTypeNode(node->type());
	case Kind::JSDocNullableType: {
		Type* t = getTypeFromTypeNode(node->type());
		if (strictNullChecks) {
			return getNullableType(t, TypeFlagsNull);
		}
		return t;
	}
	case Kind::JSDocVariadicType:
		return createArrayType(
			getTypeFromTypeNode(node->as<JSDocVariadicType>()->Type));
	case Kind::JSDocOptionalType:
		return addOptionality(getTypeFromTypeNode(node->type()));
	case Kind::UnknownKeyword:
		return unknownType;
	case Kind::StringKeyword:
		return stringType;
	case Kind::NumberKeyword:
		return numberType;
	case Kind::BigIntKeyword:
		return bigintType;
	case Kind::BooleanKeyword:
		return booleanType;
	case Kind::SymbolKeyword:
		return esSymbolType;
	case Kind::VoidKeyword:
		return voidType;
	case Kind::UndefinedKeyword:
		return undefinedType;
	case Kind::NullKeyword:
		return nullType;
	case Kind::NeverKeyword:
		return neverType;
	case Kind::ObjectKeyword:
		return nonPrimitiveType;
	case Kind::IntrinsicKeyword:
		return intrinsicMarkerType;
	case Kind::ThisType:
	case Kind::ThisKeyword:
		return getTypeFromThisTypeNode(node);
	case Kind::LiteralType:
		return getTypeFromLiteralTypeNode(node);
	case Kind::TypeReference:
	case Kind::ExpressionWithTypeArguments:
		return getTypeFromTypeReference(node);
	case Kind::TypePredicate:
		if (node->as<TypePredicateNode>()->AssertsModifier != nullptr) {
			return voidType;
		}
		return booleanType;
	case Kind::TypeQuery:
		return getTypeFromTypeQueryNode(node);
	case Kind::ArrayType:
	case Kind::TupleType:
		return getTypeFromArrayOrTupleTypeNode(node);
	case Kind::OptionalType:
		return getTypeFromOptionalTypeNode(node);
	case Kind::UnionType:
		return getTypeFromUnionTypeNode(node);
	case Kind::IntersectionType:
		return getTypeFromIntersectionTypeNode(node);
	case Kind::NamedTupleMember:
		return getTypeFromNamedTupleTypeNode(node);
	case Kind::ParenthesizedType:
		return getTypeFromTypeNode(node->type());
	case Kind::RestType:
		return getTypeFromRestTypeNode(node);
	case Kind::FunctionType:
	case Kind::ConstructorType:
	case Kind::TypeLiteral:
		return getTypeFromTypeLiteralOrFunctionOrConstructorTypeNode(node);
	case Kind::TypeOperator:
		return getTypeFromTypeOperatorNode(node);
	case Kind::IndexedAccessType:
		return getTypeFromIndexedAccessTypeNode(node);
	case Kind::TemplateLiteralType:
		return getTypeFromTemplateTypeNode(node);
	case Kind::MappedType:
		return getTypeFromMappedTypeNode(node);
	case Kind::ConditionalType:
		return getTypeFromConditionalTypeNode(node);
	case Kind::InferType:
		return getTypeFromInferTypeNode(node);
	case Kind::ImportType:
		return getTypeFromImportTypeNode(node);
	default:
		return errorType;
	}
}

Type* Checker::getTypeFromThisTypeNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		links->resolvedType = getThisType(node);
	}
	return links->resolvedType;
}

Type* Checker::getThisType(Node* node) {
	Node* container = getThisContainer(node, /*includeArrowFunctions*/ false,
									   /*includeClassComputedPropertyName*/ false);
	if (container != nullptr) {
		Node* parent = container->parent;
		if (parent != nullptr &&
			(isClassLike(parent) || isInterfaceDeclaration(parent))) {
			if (!isStatic(container) &&
				(!isConstructorDeclaration(container) ||
				 isNodeDescendantOf(node, container->body()))) {
				Type* thisType = getDeclaredTypeOfClassOrInterface(
									 getSymbolOfDeclaration(parent))
									 ->AsInterfaceType()
									 ->thisType;
				return thisType != nullptr ? thisType : errorType;
			}
		}
	}
	error(node,
		  A_this_type_is_available_only_in_a_non_static_member_of_a_class_or_interface);
	return errorType;
}

Type* Checker::getTypeFromLiteralTypeNode(Node* node) {
	if (node->as<LiteralTypeNode>()->Literal->kind == Kind::NullKeyword) {
		return nullType;
	}
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		links->resolvedType = getRegularTypeOfLiteralType(
			checkExpression(node->as<LiteralTypeNode>()->Literal));
	}
	return links->resolvedType;
}

Type* Checker::getTypeFromTypeLiteralOrFunctionOrConstructorTypeNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		// Deferred resolution of members is handled by resolveObjectTypeMembers
		TypeAlias* alias = getAliasForTypeNode(node);
		Symbol* sym = node->symbol();
		if (sym == nullptr ||
			(getMembersOfSymbol(sym).empty() && alias == nullptr)) {
			links->resolvedType = emptyTypeLiteralType;
		} else {
			Type* t = newObjectType(ObjectFlagsAnonymous, node->symbol());
			t->alias = alias;
			links->resolvedType = t;
		}
	}
	return links->resolvedType;
}

Type* Checker::getTypeFromIndexedAccessTypeNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		Type* objectType = getTypeFromTypeNode(
			node->as<IndexedAccessTypeNode>()->ObjectType);
		Type* indexType =
			getTypeFromTypeNode(node->as<IndexedAccessTypeNode>()->IndexType);
		TypeAlias* potentialAlias = getAliasForTypeNode(node);
		links->resolvedType = getIndexedAccessTypeEx(
			objectType, indexType, AccessFlagsNone, node, potentialAlias);
	}
	return links->resolvedType;
}

Type* Checker::getTypeFromTypeOperatorNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		Node* argType = node->type();
		switch (node->as<TypeOperatorNode>()->Operator) {
		case Kind::KeyOfKeyword:
			links->resolvedType = getIndexType(getTypeFromTypeNode(argType));
			break;
		case Kind::UniqueKeyword:
			if (argType->kind == Kind::SymbolKeyword) {
				links->resolvedType =
					getESSymbolLikeTypeForNode(
						walkUpParenthesizedTypes(node->parent));
			} else {
				links->resolvedType = errorType;
			}
			break;
		case Kind::ReadonlyKeyword:
			links->resolvedType = getTypeFromTypeNode(argType);
			break;
		default:
			TSC_UNREACHABLE("Unhandled case in getTypeFromTypeOperatorNode");
		}
	}
	return links->resolvedType;
}

Type* Checker::getESSymbolLikeTypeForNode(Node* node) {
	if (isValidESSymbolDeclaration(node)) {
		Symbol* symbol = getSymbolOfNode(node);
		if (symbol != nullptr) {
			Type* uniqueType = nullptr;
			auto it = uniqueESSymbolTypes.find(symbol);
			if (it != uniqueESSymbolTypes.end()) {
				uniqueType = it->second;
			}
			if (uniqueType == nullptr) {
				std::string name;
				name += kInternalSymbolNamePrefix;
				name += '@';
				name += symbol->name;
				name += '@';
				name += std::to_string(getSymbolId(symbol));
				uniqueType = newUniqueESSymbolType(symbol, name);
				uniqueESSymbolTypes[symbol] = uniqueType;
			}
			return uniqueType;
		}
	}
	return esSymbolType;
}

Type* Checker::getTypeFromTypeReference(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		// Cache both the resolved symbol and the resolved type. The resolved
		// symbol is needed when we check the type reference in
		// checkTypeReferenceNode.
		// handle LS queries on the `const` in `x as const` by resolving to the
		// type of `x`
		if (isConstTypeReference(node) && isAssertionExpression(node->parent)) {
			links->resolvedType = checkExpressionCached(node->parent->expression());
		} else if (Type* t = getIntendedTypeFromJSDocTypeReference(node);
				   t != nullptr) {
			links->resolvedType = t;
		} else {
			links->resolvedType = getDistributedTypeParameter(
				node,
				getTypeReferenceType(node, getSymbolFromTypeReference(node)));
		}
	}
	return links->resolvedType;
}

Type* Checker::getDistributedTypeParameter(Node* node, Type* t) {
	if (t->flags & TypeFlagsTypeParameter &&
		!t->AsTypeParameter()->isDistributed) {
		for (Node* n = node->parent; n != nullptr && !isStatement(n);
			 n = n->parent) {
			if (isConditionalTypeNode(n)) {
				Node* checkTypeNode = n->as<ConditionalTypeNode>()->CheckType;
				if (isSimpleIdentifierTypeReference(checkTypeNode) &&
					getSymbolFromTypeReference(checkTypeNode) == t->symbol) {
					// If node is contained in a distributive conditional type for
					// the given type parameter, return the distributed form of
					// the type parameter.
					return getDistributedTypeFromTypeParameter(t);
				}
			}
		}
	}
	return t;
}

Type* Checker::getDistributedTypeFromTypeParameter(Type* t) {
	TypeParameter* tp = t->AsTypeParameter();
	if (tp->distributedType == nullptr) {
		tp->distributedType = newTypeParameter(t->symbol);
		tp->distributedType->AsTypeParameter()->isDistributed = true;
		tp->distributedType->AsTypeParameter()->constraint = t;
	}
	return tp->distributedType;
}

Type* Checker::getIntendedTypeFromJSDocTypeReference(Node* node) {
	if ((node->flags & NodeFlagsJSDoc) && isTypeReferenceNode(node)) {
		Node* typeName = node->as<TypeReferenceNode>()->TypeName;
		if (isIdentifier(typeName)) {
			std::vector<Node*> typeArgs = node->typeArguments();
			const std::string& text = typeName->text();
			if (text == "String") {
				checkNoTypeArguments(node, nullptr);
				return stringType;
			}
			if (text == "Number") {
				checkNoTypeArguments(node, nullptr);
				return numberType;
			}
			if (text == "BigInt") {
				checkNoTypeArguments(node, nullptr);
				return bigintType;
			}
			if (text == "Boolean") {
				checkNoTypeArguments(node, nullptr);
				return booleanType;
			}
			if (text == "Void") {
				checkNoTypeArguments(node, nullptr);
				return voidType;
			}
			if (text == "Undefined") {
				checkNoTypeArguments(node, nullptr);
				return undefinedType;
			}
			if (text == "Null") {
				checkNoTypeArguments(node, nullptr);
				return nullType;
			}
			if (text == "Function" || text == "function") {
				checkNoTypeArguments(node, nullptr);
				return globalFunctionType;
			}
			if (text == "array") {
				if (typeArgs.empty() && !noImplicitAny) {
					return anyArrayType;
				}
			}
			if (text == "promise") {
				if (typeArgs.empty() && !noImplicitAny) {
					return createPromiseType(anyType);
				}
			}
			if (text == "Object") {
				if (typeArgs.size() == 2) {
					if (Symbol* recordSymbol = getGlobalRecordSymbol();
						recordSymbol != nullptr) {
						Type* indexType = getTypeFromTypeNode(typeArgs[0]);
						if (isValidIndexKeyType(indexType)) {
							return getTypeAliasInstantiation(
								recordSymbol,
								{indexType, getTypeFromTypeNode(typeArgs[1])},
								nullptr);
						}
					}
					return anyType;
				}
				if (!noImplicitAny) {
					checkNoTypeArguments(node, nullptr);
					return anyType;
				}
			}
		}
	}
	return nullptr;
}

Symbol* Checker::getSymbolFromTypeReference(Node* node) {
	auto* links = symbolNodeLinks.Get(node);
	if (links->resolvedSymbol == nullptr) {
		// The `const` in a `const` assertion resolves to nothing; resolveName
		// knows not to report an error for it, so no special-casing is needed
		// here.
		links->resolvedSymbol = resolveTypeReferenceName(
			node, SymbolFlagsType, /*ignoreErrors*/ false);
	}
	return links->resolvedSymbol;
}

Symbol* Checker::resolveTypeReferenceName(Node* typeReference,
										  SymbolFlags meaning,
										  bool ignoreErrors) {
	Node* name = getTypeReferenceName(typeReference);
	if (name == nullptr) {
		return unknownSymbol;
	}
	Symbol* symbol = resolveEntityName(name, meaning, ignoreErrors,
									 /*dontResolveAlias*/ false,
									 /*location*/ nullptr);
	if (symbol != nullptr && symbol != unknownSymbol) {
		return symbol;
	}
	if (ignoreErrors) {
		return unknownSymbol;
	}
	return getUnresolvedSymbolForEntityName(name);
}

Symbol* Checker::getUnresolvedSymbolForEntityName(Node* name) {
	Node* identifier = nullptr;
	switch (name->kind) {
	case Kind::QualifiedName:
		identifier = name->as<QualifiedName>()->Right;
		break;
	case Kind::PropertyAccessExpression:
		identifier = name->name();
		break;
	default:
		identifier = name;
		break;
	}
	std::string text = identifier->text();
	if (!text.empty()) {
		Symbol* parentSymbol = nullptr;
		switch (name->kind) {
		case Kind::QualifiedName:
			parentSymbol = getUnresolvedSymbolForEntityName(
				name->as<QualifiedName>()->Left);
			break;
		case Kind::PropertyAccessExpression:
			parentSymbol =
				getUnresolvedSymbolForEntityName(name->expression());
			break;
		default:
			break;
		}
		std::string path;
		if (parentSymbol != nullptr) {
			path = getSymbolPath(parentSymbol) + "." + text;
		} else {
			path = text;
		}
		Symbol* result = nullptr;
		auto it = unresolvedSymbols.find(path);
		if (it != unresolvedSymbols.end()) {
			result = it->second;
		}
		if (result == nullptr) {
			result = newSymbolEx(SymbolFlagsTypeAlias, text,
								 CheckFlagsUnresolved);
			unresolvedSymbols[path] = result;
			result->parent = parentSymbol;
			typeAliasLinks.Get(result)->declaredType = unresolvedType;
		}
		return result;
	}
	return unknownSymbol;
}


Type* Checker::getTypeReferenceType(Node* node, Symbol* symbol) {
	if (symbol == unknownSymbol) {
		return errorType;
	}
	if (symbol->flags & (SymbolFlagsClass | SymbolFlagsInterface)) {
		return getTypeFromClassOrInterfaceReference(node, symbol);
	}
	if (symbol->flags & SymbolFlagsTypeAlias) {
		return getTypeFromTypeAliasReference(node, symbol);
	}
	// Get type from reference to named type that cannot be generic (enum or
	// type parameter)
	Type* res = tryGetDeclaredTypeOfSymbol(symbol);
	if (res != nullptr && checkNoTypeArguments(node, symbol)) {
		return getRegularTypeOfLiteralType(res);
	}

	// !!! Resolving values as types for JS
	return errorType;
}

// Get type from type-reference that reference to class or interface
Type* Checker::getTypeFromClassOrInterfaceReference(Node* node,
													Symbol* symbol) {
	Type* t = getDeclaredTypeOfClassOrInterface(getMergedSymbol(symbol));
	InterfaceType* d = t->AsInterfaceType();
	std::vector<Type*> typeParameters = interfaceTypeLocalTypeParameters(d);
	if (!typeParameters.empty()) {
		int numTypeArguments = static_cast<int>(node->typeArguments().size());
		int minTypeArgumentCount = getMinTypeArgumentCount(typeParameters);
		bool isJs = isInJSFile(node);
		bool isJsImplicitAny = !noImplicitAny && isJs;
		if (!isJsImplicitAny &&
			(numTypeArguments < minTypeArgumentCount ||
			 numTypeArguments > static_cast<int>(typeParameters.size()))) {
			const DiagnosticMessage* message;

			bool missingAugmentsTag = isJs &&
									  isExpressionWithTypeArguments(node) &&
									  !isJSDocAugmentsTag(node->parent);
			if (missingAugmentsTag) {
				message =
					Expected_0_type_arguments_provide_these_with_an_extends_tag;
				if (minTypeArgumentCount <
					static_cast<int>(typeParameters.size())) {
					message =
						Expected_0_1_type_arguments_provide_these_with_an_extends_tag;
				}
			} else {
				message = Generic_type_0_requires_1_type_argument_s;
				if (minTypeArgumentCount <
					static_cast<int>(typeParameters.size())) {
					message =
						Generic_type_0_requires_between_1_and_2_type_arguments;
				}
			}
			std::string typeStr = TypeToStringEx(
				t, /*enclosingDeclaration*/ nullptr,
				TypeFormatFlagsWriteArrayAsGenericType, nullptr);
			error(node, message,
				  {typeStr, std::to_string(minTypeArgumentCount),
				   std::to_string(typeParameters.size())});
			if (!isJs) {
				// TODO: Adopt same permissive behavior in TS as in JS to reduce
				// follow-on editing experience failures (requires editing
				// fillMissingTypeArguments)
				return errorType;
			}
		}
		if (node->kind == Kind::TypeReference &&
			isDeferredTypeReferenceNode(
				node,
				numTypeArguments != static_cast<int>(typeParameters.size()))) {
			return createDeferredTypeReference(t, node, /*mapper*/ nullptr,
											   /*alias*/ nullptr);
		}
		// In a type reference, the outer type parameters of the referenced
		// class or interface are automatically supplied as type arguments and
		// the type reference only specifies arguments for the local type
		// parameters of the class or interface.
		std::vector<Type*> localTypeArguments = fillMissingTypeArguments(
			getTypeArgumentsFromNode(node), typeParameters,
			minTypeArgumentCount, isJs);
		std::vector<Type*> typeArguments = interfaceTypeOuterTypeParameters(d);
		typeArguments.insert(typeArguments.end(), localTypeArguments.begin(),
							 localTypeArguments.end());
		return createTypeReferenceEx(t, typeArguments, ObjectFlagsFromTypeNode);
	}
	if (checkNoTypeArguments(node, symbol)) {
		return t;
	}
	return errorType;
}

std::vector<Type*> Checker::getTypeArgumentsFromNode(Node* node) {
	return mapList(node->typeArguments(),
				   [this](Node* n) { return getTypeFromTypeNode(n); });
}

bool Checker::checkNoTypeArguments(Node* node, Symbol* symbol) {
	if (!node->typeArguments().empty()) {
		std::string typeName;
		if (symbol != nullptr) {
			typeName = symbolToString(symbol);
		} else {
			typeName = declarationNameToString(
				node->as<TypeReferenceNode>()->TypeName);
		}
		error(node, Type_0_is_not_generic, {typeName});
		return false;
	}
	return true;
}

// Return true if the given type reference node is directly aliased or if it
// needs to be deferred because it is possibly contained in a circular chain of
// eagerly resolved types.
bool Checker::isDeferredTypeReferenceNode(Node* node,
										  bool hasDefaultTypeArguments) {
	if (getAliasSymbolForTypeNode(node) != nullptr) {
		return true;
	}
	if (isResolvedByTypeAlias(node)) {
		switch (node->kind) {
		case Kind::ArrayType:
			return mayResolveTypeAlias(node->as<ArrayTypeNode>()->ElementType);
		case Kind::TupleType:
			return someList(node->elements(),
							[this](Node* e) { return mayResolveTypeAlias(e); });
		case Kind::TypeReference:
			return hasDefaultTypeArguments ||
				   someList(node->typeArguments(),
							[this](Node* e) { return mayResolveTypeAlias(e); });
		default:
			break;
		}
		TSC_UNREACHABLE("Unhandled case in isDeferredTypeReferenceNode");
	}
	return false;
}

// Return true when the given node is transitively contained in type constructs
// that eagerly resolve their constituent types. We include
// SyntaxKind.TypeReference because type arguments of type aliases are eagerly
// resolved.
bool Checker::isResolvedByTypeAlias(Node* node) {
	Node* parent = node->parent;
	switch (parent->kind) {
	case Kind::ParenthesizedType:
	case Kind::NamedTupleMember:
	case Kind::TypeReference:
	case Kind::UnionType:
	case Kind::IntersectionType:
	case Kind::IndexedAccessType:
	case Kind::ConditionalType:
	case Kind::TypeOperator:
	case Kind::ArrayType:
	case Kind::TupleType:
		return isResolvedByTypeAlias(parent);
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
		return true;
	default:
		break;
	}
	return false;
}

// Return true if resolving the given node (i.e. getTypeFromTypeNode) possibly
// causes resolution of a type alias.
bool Checker::mayResolveTypeAlias(Node* node) {
	switch (node->kind) {
	case Kind::TypeReference:
		return (resolveTypeReferenceName(node, SymbolFlagsType,
										 /*ignoreErrors*/ false)
					->flags & SymbolFlagsTypeAlias) != 0;
	case Kind::TypeQuery:
		return true;
	case Kind::TypeOperator:
		return node->as<TypeOperatorNode>()->Operator != Kind::UniqueKeyword &&
			   mayResolveTypeAlias(node->type());
	case Kind::ParenthesizedType:
	case Kind::OptionalType:
	case Kind::NamedTupleMember:
		return mayResolveTypeAlias(node->type());
	case Kind::RestType:
		return node->type()->kind != Kind::ArrayType ||
			   mayResolveTypeAlias(node->type()->as<ArrayTypeNode>()->ElementType);
	case Kind::UnionType:
		return someList(node->as<UnionTypeNode>()->Types->nodes,
						[this](Node* e) { return mayResolveTypeAlias(e); });
	case Kind::IntersectionType:
		return someList(node->as<IntersectionTypeNode>()->Types->nodes,
						[this](Node* e) { return mayResolveTypeAlias(e); });
	case Kind::IndexedAccessType:
		return mayResolveTypeAlias(
				   node->as<IndexedAccessTypeNode>()->ObjectType) ||
			   mayResolveTypeAlias(
				   node->as<IndexedAccessTypeNode>()->IndexType);
	case Kind::ConditionalType: {
		auto* c = node->as<ConditionalTypeNode>();
		return mayResolveTypeAlias(c->CheckType) ||
			   mayResolveTypeAlias(c->ExtendsType) ||
			   mayResolveTypeAlias(c->TrueType) ||
			   mayResolveTypeAlias(c->FalseType);
	}
	default:
		break;
	}
	return false;
}

namespace {

// TupleNormalizer — checker.go:23812
struct TupleNormalizer {
	Checker* c{};
	std::vector<Type*> types;
	std::vector<TupleElementInfo> infos;
	int lastRequiredIndex{};
	int firstRestIndex{};
	int lastOptionalOrRestIndex{};

	bool normalize(Checker* c, const std::vector<Type*>& elementTypes,
				   const std::vector<TupleElementInfo>& elementInfos) {
		this->c = c;
		lastRequiredIndex = -1;
		firstRestIndex = -1;
		lastOptionalOrRestIndex = -1;
		for (size_t i = 0; i < elementTypes.size(); i++) {
			Type* t = elementTypes[i];
			TupleElementInfo info = elementInfos[i];
			if (info.flags & ElementFlagsVariadic) {
				if (t->flags & TypeFlagsAny) {
					add(t, TupleElementInfo{ElementFlagsRest,
											info.labeledDeclaration});
				} else if ((t->flags & TypeFlagsInstantiableNonPrimitive) ||
						   c->isGenericMappedType(t)) {
					// Generic variadic elements stay as they are.
					add(t, info);
				} else if (isTupleType(t)) {
					std::vector<Type*> spreadTypes = c->getElementTypes(t);
					if (spreadTypes.size() + types.size() >= 10000) {
						const DiagnosticMessage* message =
							isPartOfTypeNode(c->currentNode)
								? Type_produces_a_tuple_type_that_is_too_large_to_represent
								: Expression_produces_a_tuple_type_that_is_too_large_to_represent;
						c->error(c->currentNode, message);
						return false;
					}
					// Spread variadic elements with tuple types into the
					// resulting tuple.
					const std::vector<TupleElementInfo>& spreadInfos =
						targetTupleType(t)->elementInfos;
					for (size_t j = 0; j < spreadTypes.size(); j++) {
						add(spreadTypes[j], spreadInfos[j]);
					}
				} else {
					// Treat everything else as an array type and create a rest
					// element.
					Type* s = nullptr;
					if (c->isArrayLikeType(t)) {
						s = c->getIndexTypeOfType(t, c->numberType);
					}
					if (s == nullptr) {
						s = c->errorType;
					}
					add(s,
						TupleElementInfo{ElementFlagsRest, info.labeledDeclaration});
				}
			} else {
				// Copy other element kinds with no change.
				add(t, info);
			}
		}
		// Turn optional elements preceding the last required element into
		// required elements
		for (int i = 0; i < lastRequiredIndex; i++) {
			if (infos[i].flags & ElementFlagsOptional) {
				infos[i].flags = ElementFlagsRequired;
			}
		}
		if (firstRestIndex >= 0 && firstRestIndex < lastOptionalOrRestIndex) {
			// Turn elements between first rest and last optional/rest into a
			// single rest element
			std::vector<Type*> restTypes;
			for (int i = firstRestIndex; i <= lastOptionalOrRestIndex; i++) {
				Type* t = types[i];
				if (infos[i].flags & ElementFlagsVariadic) {
					t = c->getIndexedAccessType(t, c->numberType);
				}
				restTypes.push_back(t);
			}
			types[firstRestIndex] = c->getUnionType(restTypes);
			types.erase(types.begin() + firstRestIndex + 1,
						types.begin() + lastOptionalOrRestIndex + 1);
			infos.erase(infos.begin() + firstRestIndex + 1,
						infos.begin() + lastOptionalOrRestIndex + 1);
		}
		return true;
	}

	void add(Type* t, TupleElementInfo info) {
		if (info.flags & ElementFlagsRequired) {
			lastRequiredIndex = static_cast<int>(types.size());
		}
		if ((info.flags & ElementFlagsRest) && firstRestIndex < 0) {
			firstRestIndex = static_cast<int>(types.size());
		}
		if (info.flags & (ElementFlagsOptional | ElementFlagsRest)) {
			lastOptionalOrRestIndex = static_cast<int>(types.size());
		}
		types.push_back(c->addOptionalityEx(t, /*isProperty*/ true,
										 (info.flags & ElementFlagsOptional) != 0));
		infos.push_back(info);
	}
};

}  // namespace

Type* Checker::createNormalizedTypeReference(
	Type* target, const std::vector<Type*>& typeArguments) {
	if (target->objectFlags & ObjectFlagsTuple) {
		return createNormalizedTupleType(target, typeArguments);
	}
	return createTypeReference(target, typeArguments);
}

Type* Checker::createNormalizedTupleTypeEx(
	Type* target, const std::vector<Type*>& elementTypes,
	ObjectFlags objectFlags) {
	TupleType* d = target->AsTupleType();
	if (!(d->combinedFlags & ElementFlagsNonRequired)) {
		// No need to normalize when we only have regular required elements
		return createTypeReferenceEx(target, elementTypes, objectFlags);
	}
	if (d->combinedFlags & ElementFlagsVariadic) {
		for (size_t i = 0; i < elementTypes.size(); i++) {
			Type* e = elementTypes[i];
			if (i < d->elementInfos.size() &&
				(d->elementInfos[i].flags & ElementFlagsVariadic) &&
				(e->flags & (TypeFlagsNever | TypeFlagsUnion))) {
				// Transform [A, ...(X | Y | Z)] into
				// [A, ...X] | [A, ...Y] | [A, ...Z]
				std::vector<Type*> checkTypes = mapIndexList(
					elementTypes, [this, d](Type* t, int j) -> Type* {
						if (j < static_cast<int>(d->elementInfos.size()) &&
							(d->elementInfos[j].flags & ElementFlagsVariadic)) {
							return t;
						}
						return unknownType;
					});
				if (checkCrossProductUnion(checkTypes)) {
					return mapType(e, [this, target, elementTypes, i,
									   objectFlags](Type* t) {
						return createNormalizedTupleTypeEx(
							target, replaceElementList(elementTypes, i, t),
							objectFlags);
					});
				}
			}
		}
	}
	// We have optional, rest, or variadic elements that may need normalizing.
	// Normalization ensures that all variadic elements are generic and that the
	// tuple type has one of the following layouts, disregarding variadic
	// elements:
	// (1) Zero or more required elements, followed by zero or more optional
	//     elements, followed by zero or one rest element.
	// (2) Zero or more required elements, followed by a rest element, followed
	//     by zero or more required elements.
	// In either layout, zero or more generic variadic elements may be present
	// at any location.
	// Note that the element types may contain an extra 'this' type argument
	// that we want to ignore during normalization and then just append to the
	// normalized element types.
	TupleNormalizer n;
	std::vector<Type*> head(
		elementTypes.begin(),
		elementTypes.begin() +
			std::min(elementTypes.size(), d->elementInfos.size()));
	if (!n.normalize(this, head, d->elementInfos)) {
		return errorType;
	}
	if (elementTypes.size() > d->elementInfos.size()) {
		n.types.push_back(elementTypes[d->elementInfos.size()]);
	}
	Type* tupleTarget = getTupleTargetType(n.infos, d->readonly);
	if (tupleTarget == emptyGenericType) {
		return emptyObjectType;
	}
	if (!n.types.empty()) {
		return createTypeReferenceEx(tupleTarget, n.types, objectFlags);
	}
	return tupleTarget;
}

Type* Checker::createNormalizedTupleType(
	Type* target, const std::vector<Type*>& elementTypes) {
	return createNormalizedTupleTypeEx(target, elementTypes, ObjectFlagsNone);
}

std::vector<Type*> Checker::getElementTypes(Type* t) {
	std::vector<Type*> typeArguments = getTypeArguments(t);
	int arity = getTypeReferenceArity(t);
	if (static_cast<int>(typeArguments.size()) == arity) {
		return typeArguments;
	}
	return std::vector<Type*>(typeArguments.begin(),
							  typeArguments.begin() + arity);
}

int Checker::getTypeReferenceArity(Type* t) {
	return static_cast<int>(
		interfaceTypeTypeParameters(targetInterfaceType(t)).size());
}

bool Checker::isArrayType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) &&
		   (typeTarget(t) == globalArrayType ||
			typeTarget(t) == globalReadonlyArrayType);
}

bool Checker::isReadonlyArrayType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) &&
		   typeTarget(t) == globalReadonlyArrayType;
}

bool Checker::isArrayOrTupleType(Type* t) {
	return isArrayType(t) || isTupleType(t);
}

bool Checker::isMutableArrayOrTuple(Type* t) {
	return (isArrayType(t) && !isReadonlyArrayType(t)) ||
		   (isTupleType(t) && !targetTupleType(t)->readonly);
}

Type* Checker::getElementTypeOfArrayType(Type* t) {
	if (isArrayType(t)) {
		return getTypeArguments(t)[0];
	}
	return nullptr;
}

bool Checker::isArrayLikeType(Type* t) {
	// A type is array-like if it is a reference to the global Array or global
	// ReadonlyArray type, or if it is not the undefined or null type and if it
	// is assignable to ReadonlyArray<any>
	return isArrayType(t) || (!(t->flags & TypeFlagsNullable) &&
							  isTypeAssignableTo(t, anyReadonlyArrayType));
}

bool Checker::isMutableArrayLikeType(Type* t) {
	// A type is mutable-array-like if it is a reference to the global Array
	// type, or if it is not the any, undefined, null or never type and if it is
	// assignable to Array<any>
	return isMutableArrayOrTuple(t) ||
		   (!(t->flags & (TypeFlagsAny | TypeFlagsNullable | TypeFlagsNever)) &&
			isTypeAssignableTo(t, anyArrayType));
}

bool Checker::isEmptyArrayLiteralType(Type* t) {
	Type* elementType = getElementTypeOfArrayType(t);
	return elementType != nullptr && isEmptyLiteralType(elementType);
}

bool Checker::isEmptyLiteralType(Type* t) {
	if (strictNullChecks) {
		return t == implicitNeverType;
	}
	return t == undefinedWideningType;
}

bool Checker::isTupleLikeType(Type* t) {
	if (isTupleType(t) || getPropertyOfType(t, "0") != nullptr) {
		return true;
	}
	if (isArrayLikeType(t)) {
		if (Type* lengthType = getTypeOfPropertyOfType(t, "length");
			lengthType != nullptr) {
			return everyType(lengthType, [](Type* u) {
				return (u->flags & TypeFlagsNumberLiteral) != 0;
			});
		}
	}
	return false;
}

bool Checker::isArrayOrTupleLikeType(Type* t) {
	return isArrayLikeType(t) || isTupleLikeType(t);
}

bool Checker::isArrayOrTupleOrIntersection(Type* t) {
	return (t->flags & TypeFlagsIntersection) &&
		   everyList(t->types(),
					 [this](Type* u) { return isArrayOrTupleType(u); });
}

Type* Checker::getTupleElementType(Type* t, int index) {
	Type* propType = getTypeOfPropertyOfType(t, std::to_string(index));
	if (propType != nullptr) {
		return propType;
	}
	if (everyType(t, [](Type* u) { return isTupleType(u); })) {
		return getTupleElementTypeOutOfStartCount(
			t, Number(index),
			compilerOptions->NoUncheckedIndexedAccess == Tristate::True
				? undefinedType
				: nullptr);
	}
	return nullptr;
}

// Get type from reference to type alias. When a type alias is generic, the
// declared type of the type alias may include references to the type
// parameters of the alias. We replace those with the actual type arguments by
// instantiating the declared type. Instantiations are cached using the type
// identities of the type arguments as the key.
Type* Checker::getTypeFromTypeAliasReference(Node* node, Symbol* symbol) {
	std::vector<Node*> typeArguments = node->typeArguments();
	if (symbol->checkFlags & CheckFlagsUnresolved) {
		TypeAlias* alias = new TypeAlias{
			symbol, mapList(typeArguments,
							[this](Node* n) { return getTypeFromTypeNode(n); })};
		CacheKey key = getAliasKey(alias);
		Type* errorType = nullptr;
		auto it = errorTypes.find(key);
		if (it != errorTypes.end()) {
			errorType = it->second;
		}
		if (errorType == nullptr) {
			errorType = newIntrinsicType(TypeFlagsAny, "error");
			errorType->alias = alias;
			errorTypes[key] = errorType;
		}
		return errorType;
	}
	Type* t = getDeclaredTypeOfSymbol(symbol);
	std::vector<Type*> typeParameters = typeAliasLinks.Get(symbol)->typeParameters;
	if (!typeParameters.empty()) {
		int numTypeArguments = static_cast<int>(typeArguments.size());
		int minTypeArgumentCount = getMinTypeArgumentCount(typeParameters);
		if (numTypeArguments < minTypeArgumentCount ||
			numTypeArguments > static_cast<int>(typeParameters.size())) {
			const DiagnosticMessage* message =
				minTypeArgumentCount == static_cast<int>(typeParameters.size())
					? Generic_type_0_requires_1_type_argument_s
					: Generic_type_0_requires_between_1_and_2_type_arguments;
			error(node, message,
				  {symbolToString(symbol), std::to_string(minTypeArgumentCount),
				   std::to_string(typeParameters.size())});
			return errorType;
		}
		// We refrain from associating a local type alias with an instantiation
		// of a top-level type alias because the local alias may end up being
		// referenced in an inferred return type where it is not accessible--which
		// in turn may lead to a large structural expansion of the type when
		// generating a .d.ts file. See #43622 for an example.
		Symbol* aliasSymbol = getAliasSymbolForTypeNode(node);
		Symbol* newAliasSymbol = nullptr;
		if (aliasSymbol != nullptr &&
			(isLocalTypeAlias(symbol) || !isLocalTypeAlias(aliasSymbol))) {
			newAliasSymbol = aliasSymbol;
		}
		std::vector<Type*> aliasTypeArguments;
		if (newAliasSymbol != nullptr) {
			aliasTypeArguments = getTypeArgumentsForAliasSymbol(newAliasSymbol);
		} else if (isTypeReferenceType(node)) {
			aliasSymbol = resolveTypeReferenceName(node, SymbolFlagsAlias,
												   /*ignoreErrors*/ true);
			// refers to an alias import/export/reexport - by making sure we use
			// the target as an aliasSymbol, we ensure the exported symbol is
			// used to refer to the type when it is reserialized later
			if (aliasSymbol != nullptr && aliasSymbol != unknownSymbol) {
				Symbol* resolved = resolveAlias(aliasSymbol);
				if (resolved != nullptr &&
					(resolved->flags & SymbolFlagsTypeAlias)) {
					newAliasSymbol = resolved;
					aliasTypeArguments = getTypeArgumentsFromNode(node);
				}
			}
		}
		TypeAlias* newAlias = nullptr;
		if (newAliasSymbol != nullptr) {
			newAlias = new TypeAlias{newAliasSymbol, aliasTypeArguments};
		}
		return getTypeAliasInstantiation(symbol, getTypeArgumentsFromNode(node),
										 newAlias);
	}
	if (checkNoTypeArguments(node, symbol)) {
		return t;
	}
	return errorType;
}

Type* Checker::getTypeAliasInstantiation(
	Symbol* symbol, const std::vector<Type*>& typeArguments,
	TypeAlias* alias) {
	Type* t = getDeclaredTypeOfSymbol(symbol);
	if (t == intrinsicMarkerType) {
		auto it = intrinsicTypeKinds.find(symbol->name);
		if (it != intrinsicTypeKinds.end() && typeArguments.size() == 1) {
			switch (it->second) {
			case IntrinsicTypeKind::NoInfer:
				return getNoInferType(typeArguments[0]);
			default:
				return getStringMappingType(symbol, typeArguments[0]);
			}
		}
	}
	auto* links = typeAliasLinks.Get(symbol);
	std::vector<Type*> typeParameters = links->typeParameters;
	CacheKey key = getTypeAliasInstantiationKey(typeArguments, alias);
	Type* instantiation = nullptr;
	auto instIt = links->instantiations.find(key);
	if (instIt != links->instantiations.end()) {
		instantiation = instIt->second;
	}
	if (instantiation == nullptr) {
		TypeMapper* mapper = newTypeMapper(
			typeParameters,
			fillMissingTypeArguments(
				typeArguments, typeParameters,
				getMinTypeArgumentCount(typeParameters),
				isInJSFile(symbol->valueDeclaration)));
		instantiation = instantiateTypeWithAlias(t, mapper, alias);
		links->instantiations[key] = instantiation;
	}
	return instantiation;
}

Type* Checker::getTypeFromTypeQueryNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		// TypeScript 1.0 spec (April 2014): 3.6.3
		// The expression is processed as an identifier expression (section 4.3)
		// or property access expression(section 4.10),
		// the widened type(section 3.9) of which becomes the result.
		Type* t = checkExpressionWithTypeArguments(node);
		links->resolvedType =
			getRegularTypeOfLiteralType(getWidenedType(t));
	}
	return links->resolvedType;
}

Type* Checker::getTypeFromArrayOrTupleTypeNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		Type* target = getArrayOrTupleTargetType(node);
		if (target == emptyGenericType) {
			links->resolvedType = emptyObjectType;
		} else if (!(node->kind == Kind::TupleType &&
					 someList(node->elements(),
							  [this](Node* e) { return isVariadicTupleElement(e); })) &&
				   isDeferredTypeReferenceNode(node,
											   /*hasDefaultTypeArguments*/ false)) {
			if (node->kind == Kind::TupleType && node->elements().empty()) {
				links->resolvedType = target;
			} else {
				links->resolvedType = createDeferredTypeReference(
					target, node, /*mapper*/ nullptr, /*alias*/ nullptr);
			}
		} else {
			std::vector<Type*> elementTypes;
			if (node->kind == Kind::ArrayType) {
				elementTypes = {getTypeFromTypeNode(
					node->as<ArrayTypeNode>()->ElementType)};
			} else {
				elementTypes = mapList(node->elements(), [this](Node* n) {
					return getTypeFromTypeNode(n);
				});
			}
			if (target->objectFlags & ObjectFlagsTuple) {
				links->resolvedType = createNormalizedTupleTypeEx(
					target, elementTypes, ObjectFlagsFromTypeNode);
			} else {
				links->resolvedType = createTypeReferenceEx(
					target, elementTypes, ObjectFlagsFromTypeNode);
			}
		}
	}
	return links->resolvedType;
}

bool Checker::isVariadicTupleElement(Node* node) {
	return (getTupleElementFlags(node) & ElementFlagsVariadic) != 0;
}

Type* Checker::getArrayOrTupleTargetType(Node* node) {
	bool readonly = isReadonlyTypeOperator(node->parent);
	Node* elementType = getArrayElementTypeNode(node);
	if (elementType != nullptr) {
		if (readonly) {
			return globalReadonlyArrayType;
		}
		return globalArrayType;
	}
	return getTupleTargetType(
		mapList(node->elements(),
				[this](Node* e) { return getTupleElementInfo(e); }),
		readonly);
}

bool Checker::isReadonlyTypeOperator(Node* node) {
	return isTypeOperatorNode(node) &&
		   node->as<TypeOperatorNode>()->Operator == Kind::ReadonlyKeyword;
}

Type* Checker::getTypeFromNamedTupleTypeNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		if (node->as<NamedTupleMember>()->DotDotDotToken != nullptr) {
			links->resolvedType = getTypeFromRestTypeNode(node);
		} else {
			links->resolvedType = addOptionalityEx(
				getTypeFromTypeNode(node->type()), /*isProperty*/ true,
				node->questionToken() != nullptr);
		}
	}
	return links->resolvedType;
}

Type* Checker::getTypeFromRestTypeNode(Node* node) {
	Node* typeNode = node->type();
	Node* elementTypeNode = getArrayElementTypeNode(typeNode);
	if (elementTypeNode != nullptr) {
		typeNode = elementTypeNode;
	}
	return getTypeFromTypeNode(typeNode);
}

Node* Checker::getArrayElementTypeNode(Node* node) {
	switch (node->kind) {
	case Kind::ParenthesizedType:
		return getArrayElementTypeNode(node->type());
	case Kind::TupleType:
		if (node->elements().size() == 1) {
			node = node->elements()[0];
			if (node->kind == Kind::RestType) {
				return getArrayElementTypeNode(node->type());
			}
			if (node->kind == Kind::NamedTupleMember &&
				node->as<NamedTupleMember>()->DotDotDotToken != nullptr) {
				return getArrayElementTypeNode(node->type());
			}
		}
		[[fallthrough]];
	case Kind::ArrayType:
		return node->as<ArrayTypeNode>()->ElementType;
	default:
		break;
	}
	return nullptr;
}

Type* Checker::getTypeFromOptionalTypeNode(Node* node) {
	return addOptionalityEx(getTypeFromTypeNode(node->type()),
							/*isProperty*/ true, /*isOptional*/ true);
}

Type* Checker::getTypeFromUnionTypeNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		TypeAlias* alias = getAliasForTypeNode(node);
		links->resolvedType = getUnionTypeEx(
			mapList(node->as<UnionTypeNode>()->Types->nodes,
					[this](Node* n) { return getTypeFromTypeNode(n); }),
			UnionReduction::Literal, alias, /*origin*/ nullptr);
	}
	return links->resolvedType;
}

Type* Checker::getTypeFromIntersectionTypeNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		TypeAlias* alias = getAliasForTypeNode(node);
		std::vector<Type*> types = mapList(
			node->as<IntersectionTypeNode>()->Types->nodes,
			[this](Node* n) { return getTypeFromTypeNode(n); });
		// We perform no supertype reduction for X & {} or {} & X, where X is
		// one of string, number, bigint, or a pattern literal template type.
		// This enables union types like "a" | "b" | string & {} or
		// "aa" | "ab" | `a${string}` which preserve the literal types for
		// purposes of statement completion.
		bool noSupertypeReduction = false;
		if (types.size() == 2) {
			auto it = std::find(types.begin(), types.end(), emptyTypeLiteralType);
			if (it != types.end()) {
				size_t emptyIndex = static_cast<size_t>(it - types.begin());
				Type* t = types[1 - emptyIndex];
				noSupertypeReduction =
					(t->flags &
					 (TypeFlagsString | TypeFlagsNumber | TypeFlagsBigInt)) ||
					((t->flags & TypeFlagsTemplateLiteral) &&
					 isPatternLiteralType(t));
			}
		}
		links->resolvedType = getIntersectionTypeEx(
			types,
			noSupertypeReduction ? IntersectionFlagsNoSupertypeReduction
								 : IntersectionFlagsNone,
			alias);
	}
	return links->resolvedType;
}

Type* Checker::getTypeFromTemplateTypeNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		NodeList* spans = node->as<TemplateLiteralTypeNode>()->TemplateSpans;
		std::vector<std::string> texts(spans->nodes.size() + 1);
		std::vector<Type*> types(spans->nodes.size());
		texts[0] = node->as<TemplateLiteralTypeNode>()->Head->text();
		for (size_t i = 0; i < spans->nodes.size(); i++) {
			Node* span = spans->nodes[i];
			texts[i + 1] = span->as<TemplateLiteralTypeSpan>()->Literal->text();
			types[i] = getTypeFromTypeNode(span->type());
		}
		links->resolvedType = getTemplateLiteralType(texts, types);
	}
	return links->resolvedType;
}

Type* Checker::getTypeFromMappedTypeNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		Type* t = newObjectType(ObjectFlagsMapped, node->symbol());
		t->AsMappedType()->declaration = node->as<MappedTypeNode>();
		t->alias = getAliasForTypeNode(node);
		links->resolvedType = t;
		// Eagerly resolve the constraint type which forces an error if the
		// constraint type circularly references itself through one or more type
		// aliases.
		getConstraintTypeFromMappedType(t);
	}
	return links->resolvedType;
}

Type* Checker::getTypeFromConditionalTypeNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		Type* checkType = getTypeFromTypeNode(
			node->as<ConditionalTypeNode>()->CheckType);
		TypeAlias* alias = getAliasForTypeNode(node);
		std::vector<Type*> allOuterTypeParameters =
			getOuterTypeParameters(node, /*includeThisTypes*/ true);
		std::vector<Type*> outerTypeParameters;
		if (alias != nullptr && !alias->typeArguments.empty()) {
			outerTypeParameters = allOuterTypeParameters;
		} else {
			outerTypeParameters = filterList(
				allOuterTypeParameters, [this, node](Type* tp) {
					return isTypeParameterPossiblyReferenced(tp, node);
				});
		}
		auto* root = new ConditionalRoot();
		root->node = node->as<ConditionalTypeNode>();
		root->checkType = checkType;
		root->extendsType = getTypeFromTypeNode(
			node->as<ConditionalTypeNode>()->ExtendsType);
		root->isDistributive = (checkType->flags & TypeFlagsTypeParameter) != 0;
		root->inferTypeParameters = getInferTypeParameters(node);
		root->outerTypeParameters = outerTypeParameters;
		root->alias = alias;
		links->resolvedType = getConditionalType(root, /*mapper*/ nullptr,
											   /*forConstraint*/ false,
											   /*alias*/ nullptr);
		if (!outerTypeParameters.empty()) {
			root->instantiations[getConditionalTypeKey(
				outerTypeParameters, /*alias*/ nullptr,
				/*forConstraint*/ false)] = links->resolvedType;
		}
	}
	return links->resolvedType;
}

Type* Checker::getConditionalType(ConditionalRoot* root, TypeMapper* mapper,
								  bool forConstraint, TypeAlias* alias) {
	Type* result = nullptr;
	std::vector<Type*> extraTypes;
	int tailCount = 0;
	// We loop here for an immediately nested conditional type in the false
	// position, effectively treating types of the form
	// 'A extends B ? X : C extends D ? Y : E extends F ? Z : ...' as a single
	// construct for purposes of resolution. We also loop here when resolution
	// of a conditional type ends in resolution of another (or, through
	// recursion, possibly the same) conditional type. In the potentially
	// tail-recursive cases we increment the tail recursion counter and stop
	// after 1000 iterations.
	for (;;) {
		if (tailCount == 1000) {
			error(currentNode,
				  Type_instantiation_is_excessively_deep_and_possibly_infinite);
			return errorType;
		}
		Type* checkType =
			instantiateType(getActualTypeVariable(root->checkType), mapper);
		Type* extendsType = instantiateType(root->extendsType, mapper);
		if (checkType == errorType || extendsType == errorType) {
			return errorType;
		}
		if (checkType == wildcardType || extendsType == wildcardType) {
			return wildcardType;
		}
		Node* checkTypeNode = skipTypeParentheses(root->node->as<ConditionalTypeNode>()->CheckType);
		Node* extendsTypeNode = skipTypeParentheses(root->node->as<ConditionalTypeNode>()->ExtendsType);
		// When the check and extends types are simple tuple types of the same
		// arity, we defer resolution of the conditional type when any tuple
		// elements are generic. This is such that non-distributable conditional
		// types can be written `[X] extends [Y] ? ...` and be deferred
		// similarly to `X extends Y ? ...`.
		bool checkTuples =
			isSimpleTupleType(checkTypeNode) &&
			isSimpleTupleType(extendsTypeNode) &&
			checkTypeNode->elements().size() ==
				extendsTypeNode->elements().size();
		bool checkTypeDeferred = isDeferredType(checkType, checkTuples);
		TypeMapper* combinedMapper = nullptr;
		if (!root->inferTypeParameters.empty()) {
			// When we're looking at making an inference for an infer type, when
			// we get its constraint, it'll automagically be instantiated with
			// the context, so it doesn't need the mapper for the inference
			// context - however the constraint may refer to another _root_,
			// _uncloned_ `infer` type parameter [1], or to something mapped by
			// `mapper` [2].
			// [1] Eg, if we have `Foo<T, U extends T>` and `Foo<number, infer B>`
			// - `B` is constrained to `T`, which, in turn, has been instantiated
			// as `number`
			// Conversely, if we have `Foo<infer A, infer B>`, `B` is still
			// constrained to `T` and `T` is instantiated as `A`
			// [2] Eg, if we have `Foo<T, U extends T>` and `Foo<Q, infer B>`
			// where `Q` is mapped by `mapper` into `number` - `B` is constrained
			// to `T` which is in turn instantiated as `Q`, which is in turn
			// instantiated as `number`.
			// So we need to:
			//    * combine `context.nonFixingMapper` with `mapper` so their
			//      constraints can be instantiated in the context of `mapper`
			//      (otherwise they'd only get inference context information)
			//    * incorporate all of the component mappers into the combined
			//      mapper for the true and false members
			// This means we have two mappers that need applying:
			//    * The original `mapper` used to create this conditional
			//    * The mapper that maps the infer type parameter to its
			//      inference result (`context.mapper`)
			InferenceContext* context = newInferenceContext(
				root->inferTypeParameters, /*signature*/ nullptr,
				InferenceFlagsNone, /*compareTypes*/ {});
			if (mapper != nullptr) {
				context->nonFixingMapper =
					combineTypeMappers(context->nonFixingMapper, mapper);
			}
			if (!checkTypeDeferred) {
				// We don't want inferences from constraints as they may cause
				// us to eagerly resolve the conditional type instead of
				// deferring resolution. Also, we always want strict function
				// types rules (i.e. proper contravariance) for inferences.
				inferTypes(context->inferences, checkType, extendsType,
						   InferencePriorityNoConstraints |
							   InferencePriorityAlwaysStrict,
						   false);
			}
			// It's possible for 'infer T' type parameters to be given
			// uninstantiated constraints when the those type parameters are
			// used in type references (see getInferredTypeParameterConstraint).
			// For that reason we need context.mapper to be first in the
			// combined mapper. See #42636 for examples.
			if (mapper != nullptr) {
				combinedMapper =
					combineTypeMappers(context->mapper, mapper);
			} else {
				combinedMapper = context->mapper;
			}
		}
		// Instantiate the extends type including inferences for 'infer T' type
		// parameters
		Type* inferredExtendsType = nullptr;
		if (combinedMapper != nullptr) {
			inferredExtendsType =
				instantiateType(root->extendsType, combinedMapper);
		} else {
			inferredExtendsType = extendsType;
		}
		// We attempt to resolve the conditional type only when the check and
		// extends types are non-generic
		if (!checkTypeDeferred &&
			!isDeferredType(inferredExtendsType, checkTuples)) {
			// Return falseType for a definitely false extends check. We check
			// an instantiations of the two types with type parameters mapped to
			// the wildcard type, the most permissive instantiations possible
			// (the wildcard type is assignable to and from all types). If those
			// are not related, then no instantiations will be and we can just
			// return the false branch type.
			if (!(inferredExtendsType->flags & TypeFlagsAnyOrUnknown) &&
				((checkType->flags & TypeFlagsAny) ||
				 !isTypeAssignableTo(
					 getPermissiveInstantiation(checkType),
					 getPermissiveInstantiation(inferredExtendsType)))) {
				// Return union of trueType and falseType for 'any' since it
				// matches anything. Furthermore, for a distributive conditional
				// type applied to the constraint of a type variable, include
				// trueType if there are possible values of the check type that
				// are also possible values of the extends type. We use a
				// reverse assignability check as it is less expensive than the
				// comparable relationship and avoids false positives of a
				// non-empty intersection check.
				if ((checkType->flags & TypeFlagsAny) ||
					(forConstraint &&
					 !(inferredExtendsType->flags & TypeFlagsNever) &&
					 someType(
						 getPermissiveInstantiation(inferredExtendsType),
						 [this, checkType](Type* t) {
							 return isTypeAssignableTo(
								 t, getPermissiveInstantiation(checkType));
						 }))) {
					extraTypes.push_back(instantiateType(
						getTypeFromTypeNode(root->node->as<ConditionalTypeNode>()->TrueType),
						combinedMapper != nullptr ? combinedMapper : mapper));
				}
				// If falseType is an immediately nested conditional type that
				// isn't distributive or has an identical checkType, switch to
				// that type and loop.
				Type* falseType =
					getTypeFromTypeNode(root->node->as<ConditionalTypeNode>()->FalseType);
				if (falseType->flags & TypeFlagsConditional) {
					ConditionalRoot* newRoot =
						falseType->AsConditionalType()->root;
					if (newRoot->node->parent == root->node &&
						(!newRoot->isDistributive ||
						 newRoot->checkType == root->checkType)) {
						root = newRoot;
						continue;
					}
					if (auto [newRoot2, newRootMapper] =
							getTailRecursionRoot(falseType, mapper);
						newRoot2 != nullptr) {
						root = newRoot2;
						mapper = newRootMapper;
						alias = nullptr;
						if (newRoot2->alias != nullptr) {
							tailCount++;
						}
						continue;
					}
				}
				result = instantiateType(falseType, mapper);
				break;
			}
			// Return trueType for a definitely true extends check. We check
			// instantiations of the two types with type parameters mapped to
			// their restrictive form, i.e. a form of the type parameter that
			// has no constraint. This ensures that, for example, the type
			//   type Foo<T extends { x: any }> = T extends { x: string } ?
			//   string : number
			// doesn't immediately resolve to 'string' instead of being
			// deferred.
			if ((inferredExtendsType->flags & TypeFlagsAnyOrUnknown) ||
				isTypeAssignableTo(
					getRestrictiveInstantiation(checkType),
					getRestrictiveInstantiation(inferredExtendsType))) {
				Type* trueType =
					getTypeFromTypeNode(root->node->as<ConditionalTypeNode>()->TrueType);
				TypeMapper* trueMapper =
					combinedMapper != nullptr ? combinedMapper : mapper;
				if (auto [newRoot, newRootMapper] =
						getTailRecursionRoot(trueType, trueMapper);
					newRoot != nullptr) {
					root = newRoot;
					mapper = newRootMapper;
					alias = nullptr;
					if (newRoot->alias != nullptr) {
						tailCount++;
					}
					continue;
				}
				result = instantiateType(trueType, trueMapper);
				break;
			}
		}
		// Return a deferred type for a check that is neither definitely true
		// nor definitely false
		result = newConditionalType(root, mapper, combinedMapper);
		if (alias != nullptr) {
			result->alias = alias;
		} else {
			result->alias = instantiateTypeAlias(root->alias, mapper);
		}
		break;
	}
	if (!extraTypes.empty()) {
		extraTypes.push_back(result);
		return getUnionType(extraTypes);
	}
	return result;
}

// We tail-recurse for generic conditional types that (a) have not already been
// evaluated and cached, and (b) are non distributive, have a check type that
// is unaffected by instantiation, or have a non-union check type. Note that
// recursion is possible only through aliased conditional types, so we only
// increment the tail recursion counter for those.
std::pair<ConditionalRoot*, TypeMapper*> Checker::getTailRecursionRoot(
	Type* newType, TypeMapper* newMapper) {
	if ((newType->flags & TypeFlagsConditional) && newMapper != nullptr) {
		ConditionalRoot* newRoot = newType->AsConditionalType()->root;
		if (!newRoot->outerTypeParameters.empty()) {
			TypeMapper* typeParamMapper = combineTypeMappers(
				newType->AsConditionalType()->mapper, newMapper);
			std::vector<Type*> typeArguments = mapList(
				newRoot->outerTypeParameters,
				[typeParamMapper](Type* t) { return typeParamMapper->map(t); });
			TypeMapper* newRootMapper = newTypeMapper(
				newRoot->outerTypeParameters, typeArguments);
			Type* newCheckType = nullptr;
			if (newRoot->isDistributive) {
				newCheckType =
					getMappedType(newRoot->checkType, newRootMapper);
			}
			if (newCheckType == nullptr ||
				newCheckType == newRoot->checkType ||
				!(newCheckType->flags & (TypeFlagsUnion | TypeFlagsNever))) {
				return {newRoot, newRootMapper};
			}
		}
	}
	return {nullptr, nullptr};
}

bool Checker::isSimpleTupleType(Node* node) {
	return isTupleTypeNode(node) && !node->elements().empty() &&
		   !someList(node->elements(), [](Node* e) {
			   return isOptionalTypeNode(e) || isRestTypeNode(e) ||
					  (isNamedTupleMember(e) &&
					   (e->questionToken() != nullptr ||
						e->as<NamedTupleMember>()->DotDotDotToken != nullptr));
		   });
}

bool Checker::isDeferredType(Type* t, bool checkTuples) {
	return isGenericType(t) ||
		   (checkTuples && isTupleType(t) &&
			someList(getElementTypes(t),
					 [this](Type* e) { return isGenericType(e); }));
}

Type* Checker::getPermissiveInstantiation(Type* t) {
	if (t->flags &
		(TypeFlagsPrimitive | TypeFlagsAnyOrUnknown | TypeFlagsNever)) {
		return t;
	}
	CachedTypeKey key{CachedTypeKind::PermissiveInstantiation, t->id};
	auto it = cachedTypes.find(key);
	if (it != cachedTypes.end()) {
		return it->second;
	}
	Type* result = instantiateType(t, permissiveMapper);
	cachedTypes[key] = result;
	return result;
}

Type* Checker::getRestrictiveInstantiation(Type* t) {
	if (t->flags &
		(TypeFlagsPrimitive | TypeFlagsAnyOrUnknown | TypeFlagsNever)) {
		return t;
	}
	CachedTypeKey key{CachedTypeKind::RestrictiveInstantiation, t->id};
	auto it = cachedTypes.find(key);
	if (it != cachedTypes.end()) {
		return it->second;
	}
	Type* result = instantiateType(t, restrictiveMapper);
	cachedTypes[key] = result;
	// We set the following so we don't attempt to set the restrictive instance
	// of a restrictive instance which is redundant - we'll produce new type
	// identities, but all type params have already been mapped. This also
	// gives us a way to detect restrictive instances upon comparisons and
	// _disable_ the "distributeive constraint" assignability check for them,
	// which is distinctly unsafe, as once you have a restrctive instance, all
	// the type parameters are constrained to `unknown` and produce tons of
	// false positives/negatives!
	cachedTypes[CachedTypeKey{CachedTypeKind::RestrictiveInstantiation,
							  result->id}] = result;
	return result;
}

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

// NOTE: restrictiveMapperWorker and permissiveMapperWorker are already ported
// in checker.cpp (~4488, ~4495) — not duplicated here.

Type* Checker::getTrueTypeFromConditionalType(Type* t) {
	ConditionalType* d = t->AsConditionalType();
	if (d->resolvedTrueType == nullptr) {
		d->resolvedTrueType = instantiateType(
			getTypeFromTypeNode(d->root->node->as<ConditionalTypeNode>()->TrueType), d->mapper);
	}
	return d->resolvedTrueType;
}

Type* Checker::getFalseTypeFromConditionalType(Type* t) {
	ConditionalType* d = t->AsConditionalType();
	if (d->resolvedFalseType == nullptr) {
		d->resolvedFalseType = instantiateType(
			getTypeFromTypeNode(d->root->node->as<ConditionalTypeNode>()->FalseType), d->mapper);
	}
	return d->resolvedFalseType;
}

Type* Checker::getInferredTrueTypeFromConditionalType(Type* t) {
	ConditionalType* d = t->AsConditionalType();
	if (d->resolvedInferredTrueType == nullptr) {
		if (d->combinedMapper != nullptr) {
			d->resolvedInferredTrueType = instantiateType(
				getTypeFromTypeNode(d->root->node->as<ConditionalTypeNode>()->TrueType),
				d->combinedMapper);
		} else {
			d->resolvedInferredTrueType = getTrueTypeFromConditionalType(t);
		}
	}
	return d->resolvedInferredTrueType;
}

Type* Checker::getTypeFromInferTypeNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		links->resolvedType = getDeclaredTypeOfTypeParameter(
			getSymbolOfDeclaration(
				node->as<InferTypeNode>()->TypeParameter));
	}
	return links->resolvedType;
}

Type* Checker::getTypeFromImportTypeNode(Node* node) {
	auto* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		ImportTypeNode* n = node->as<ImportTypeNode>();
		if (!isLiteralImportTypeNode(node)) {
			error(n->Argument, String_literal_expected);
			symbolNodeLinks.Get(node)->resolvedSymbol = unknownSymbol;
			links->resolvedType = errorType;
			return links->resolvedType;
		}
		SymbolFlags targetMeaning =
			n->IsTypeOf ? SymbolFlagsValue : SymbolFlagsType;
		// TODO: Future work: support unions/generics/whatever via a deferred
		// import-type
		Symbol* innerModuleSymbol = resolveExternalModuleName(
			node, n->Argument->as<LiteralTypeNode>()->Literal,
			/*ignoreErrors*/ false,
			getTypeFromImportAttributes(getImportAttributes(node)));
		if (innerModuleSymbol == nullptr) {
			symbolNodeLinks.Get(node)->resolvedSymbol = unknownSymbol;
			links->resolvedType = errorType;
			return links->resolvedType;
		}
		Symbol* moduleSymbol = resolveExternalModuleSymbol(
			innerModuleSymbol, /*dontResolveAlias*/ false);
		if (!nodeIsMissing(n->Qualifier)) {
			std::vector<Node*> nameChain = getIdentifierChain(n->Qualifier);
			Symbol* currentNamespace = moduleSymbol;
			for (size_t i = 0; i < nameChain.size(); i++) {
				Node* current = nameChain[i];
				SymbolFlags meaning = SymbolFlagsNamespace;
				if (i == nameChain.size() - 1) {
					meaning = targetMeaning;
				}
				// typeof a.b.c is normally resolved using `checkExpression`
				// which in turn defers to `checkQualifiedName`. That, in turn,
				// ultimately uses `getPropertyOfType` on the type of the
				// symbol, which differs slightly from the `exports` lookup
				// process that only looks up namespace members which is used
				// for most type references
				Symbol* mergedResolvedSymbol =
					getMergedSymbol(resolveSymbol(currentNamespace));
				Symbol* symbolFromVariable = nullptr;
				Symbol* symbolFromModule = nullptr;
				if (n->IsTypeOf) {
					symbolFromVariable = getPropertyOfTypeEx(
						getTypeOfSymbol(mergedResolvedSymbol), current->text(),
						/*skipObjectFunctionPropertyAugment*/ false,
						/*includeTypeOnlyMembers*/ true);
				} else {
					SymbolTable mergedExports =
						getExportsOfSymbol(mergedResolvedSymbol);
					symbolFromModule = getSymbol(
						mergedExports, current->text(), meaning);
					if (symbolFromModule == nullptr) {
						// a CommonJS module might have typedefs exported
						// alongside an export=
						Symbol* immediateModuleSymbol =
							resolveExternalModuleSymbol(
								innerModuleSymbol,
								/*dontResolveAlias*/ true);
						if (immediateModuleSymbol != nullptr &&
							someList(
								immediateModuleSymbol->declarations,
								[](Node* d) {
									return getAssignmentDeclarationKind(d) ==
										   JSDeclarationKind::ModuleExports;
								})) {
							SymbolTable parentExports =
								getExportsOfSymbol(
									immediateModuleSymbol->parent);
							symbolFromModule = getSymbol(
								parentExports, current->text(), meaning);
						}
					}
				}
				Symbol* next = symbolFromModule != nullptr ? symbolFromModule
														 : symbolFromVariable;
				if (next == nullptr) {
					error(current, Namespace_0_has_no_exported_member_1,
						  {getFullyQualifiedName(currentNamespace, nullptr),
						   declarationNameToString(current)});
					links->resolvedType = errorType;
					return links->resolvedType;
				}
				symbolNodeLinks.Get(current)->resolvedSymbol = next;
				symbolNodeLinks.Get(current->parent)->resolvedSymbol = next;
				currentNamespace = next;
			}
			links->resolvedType =
				resolveImportSymbolType(node, currentNamespace, targetMeaning);
		} else {
			if (getSymbolFlags(moduleSymbol) & targetMeaning) {
				links->resolvedType =
					resolveImportSymbolType(node, moduleSymbol, targetMeaning);
			} else {
				const DiagnosticMessage* message =
					(targetMeaning == SymbolFlagsValue)
						? Module_0_does_not_refer_to_a_value_but_is_used_as_a_value_here
						: Module_0_does_not_refer_to_a_type_but_is_used_as_a_type_here_Did_you_mean_typeof_import_0;
				error(node, message,
					  {n->Argument->as<LiteralTypeNode>()->Literal->text()});
				symbolNodeLinks.Get(node)->resolvedSymbol = unknownSymbol;
				links->resolvedType = errorType;
			}
		}
	}
	return links->resolvedType;
}

std::vector<Node*> Checker::getIdentifierChain(Node* node) {
	if (isIdentifier(node)) {
		return {node};
	}
	std::vector<Node*> result =
		getIdentifierChain(node->as<QualifiedName>()->Left);
	result.push_back(node->as<QualifiedName>()->Right);
	return result;
}

Type* Checker::resolveImportSymbolType(Node* node, Symbol* symbol,
									   SymbolFlags meaning) {
	Symbol* resolvedSymbol = resolveSymbol(symbol);
	symbolNodeLinks.Get(node)->resolvedSymbol = resolvedSymbol;
	if (meaning == SymbolFlagsValue) {
		// intentionally doesn't use resolved symbol so type is cached as
		// expected on the alias
		return getInstantiationExpressionType(getTypeOfSymbol(symbol), node);
	}
	// getTypeReferenceType doesn't handle aliases - it must get the resolved
	// symbol
	return getTypeReferenceType(node, resolvedSymbol);
}

// NOTE: createTypeFromGenericGlobalType, getGlobalStrictFunctionType,
// createArrayType and createArrayTypeEx are already ported in checker.cpp
// (~6530-6549) — not duplicated here.

Type* Checker::getGlobalImportMetaExpressionType() {
	if (deferredGlobalImportMetaExpressionType == nullptr) {
		// Create a synthetic type `ImportMetaExpression { meta: MetaProperty }`
		Symbol* symbol =
			newSymbol(SymbolFlagsNone, "ImportMetaExpression");
		Type* importMetaType = getGlobalImportMetaType();
		Symbol* metaPropertySymbol = newSymbolEx(
			SymbolFlagsProperty, "meta", CheckFlagsReadonly);
		metaPropertySymbol->parent = symbol;
		valueSymbolLinks.Get(metaPropertySymbol)->resolvedType =
			importMetaType;
		SymbolTable members = createSymbolTable({metaPropertySymbol});
		symbol->members = members;
		deferredGlobalImportMetaExpressionType = newAnonymousType(
			symbol, members, {}, {}, {});
	}
	return deferredGlobalImportMetaExpressionType;
}

Type* Checker::createIterableType(Type* iteratedType) {
	return createTypeFromGenericGlobalType(
		getGlobalIterableTypeChecked(),
		{iteratedType, voidType, undefinedType});
}

ElementFlags Checker::getTupleElementFlags(Node* node) {
	switch (node->kind) {
	case Kind::OptionalType:
		return ElementFlagsOptional;
	case Kind::RestType:
		return getArrayElementTypeNode(node->type()) != nullptr
				   ? ElementFlagsRest
				   : ElementFlagsVariadic;
	case Kind::NamedTupleMember: {
		NamedTupleMember* named = node->as<NamedTupleMember>();
		if (named->QuestionToken != nullptr) {
			return ElementFlagsOptional;
		}
		if (named->DotDotDotToken != nullptr) {
			return getArrayElementTypeNode(named->Type) != nullptr
					   ? ElementFlagsRest
					   : ElementFlagsVariadic;
		}
		return ElementFlagsRequired;
	}
	default:
		break;
	}
	return ElementFlagsRequired;
}

TupleElementInfo Checker::getTupleElementInfo(Node* node) {
	return TupleElementInfo{
		getTupleElementFlags(node),
		(isNamedTupleMember(node) || isParameterDeclaration(node)) ? node
																 : nullptr};
}

Type* Checker::createTupleType(const std::vector<Type*>& elementTypes) {
	std::vector<TupleElementInfo> elementInfos = mapList(
		elementTypes, [](Type*) { return TupleElementInfo{ElementFlagsRequired}; });
	return createTupleTypeEx(elementTypes, elementInfos, /*readonly*/ false);
}

Type* Checker::createTupleTypeEx(const std::vector<Type*>& elementTypes,
								 const std::vector<TupleElementInfo>& elementInfos,
								 bool readonly) {
	Type* tupleTarget = getTupleTargetType(elementInfos, readonly);
	if (tupleTarget == emptyGenericType) {
		return emptyObjectType;
	}
	if (!elementTypes.empty()) {
		return createNormalizedTypeReference(tupleTarget, elementTypes);
	}
	return tupleTarget;
}

Type* Checker::getTupleTargetType(
	const std::vector<TupleElementInfo>& elementInfos, bool readonly) {
	if (elementInfos.size() == 1 &&
		(elementInfos[0].flags & ElementFlagsRest)) {
		// [...X[]] is equivalent to just X[]
		if (readonly) {
			return globalReadonlyArrayType;
		}
		return globalArrayType;
	}
	CacheKey key = getTupleKey(elementInfos, readonly);
	Type* t = nullptr;
	auto it = tupleTypes.find(key);
	if (it != tupleTypes.end()) {
		t = it->second;
	}
	if (t == nullptr) {
		t = createTupleTargetType(elementInfos, readonly);
		tupleTypes[key] = t;
	}
	return t;
}

// We represent tuple types as type references to synthesized generic
// interface types created by this function. The types are of the form:
//
//	interface Tuple<T0, T1, T2, ...> extends Array<T0 | T1 | T2 | ...> {
//		0: T0, 1: T1, 2: T2, ...
//	}
//
// Note that the generic type created by this function has no symbol
// associated with it. The same is true for each of the synthesized type
// parameters.
Type* Checker::createTupleTargetType(
	const std::vector<TupleElementInfo>& elementInfos, bool readonly) {
	int arity = static_cast<int>(elementInfos.size());
	int minLength = countWhereList(elementInfos, [](const TupleElementInfo& e) {
		return (e.flags & (ElementFlagsRequired | ElementFlagsVariadic)) != 0;
	});
	std::vector<Type*> typeParameters;
	SymbolTable members;
	ElementFlags combinedFlags = ElementFlagsNone;
	if (arity != 0) {
		typeParameters.reserve(arity);
		for (int i = 0; i < arity; i++) {
			Type* typeParameter = newTypeParameter(nullptr);
			typeParameters.push_back(typeParameter);
			ElementFlags flags = elementInfos[i].flags;
			combinedFlags |= flags;
			if (!(combinedFlags & ElementFlagsVariable)) {
				Symbol* property = newSymbolEx(
					SymbolFlagsProperty |
						(flags & ElementFlagsOptional ? SymbolFlagsOptional
													  : SymbolFlagsNone),
					std::to_string(i),
					readonly ? CheckFlagsReadonly : CheckFlagsNone);
				valueSymbolLinks.Get(property)->resolvedType = typeParameter;
				// valueSymbolLinks.Get(property).tupleLabelDeclaration =
				//     elementInfos[i].labeledDeclaration
				members[property->name] = property;
			}
		}
	}
	int fixedLength = static_cast<int>(members.size());
	Symbol* lengthSymbol =
		newSymbolEx(SymbolFlagsProperty, "length",
					readonly ? CheckFlagsReadonly : CheckFlagsNone);
	if (combinedFlags & ElementFlagsVariable) {
		valueSymbolLinks.Get(lengthSymbol)->resolvedType = numberType;
	} else {
		std::vector<Type*> literalTypes;
		for (int i = minLength; i <= arity; i++) {
			literalTypes.push_back(
				getNumberLiteralType(static_cast<Number>(i)));
		}
		valueSymbolLinks.Get(lengthSymbol)->resolvedType =
			getUnionType(literalTypes);
	}
	members[lengthSymbol->name] = lengthSymbol;
	Type* t =
		newObjectType(ObjectFlagsTuple | ObjectFlagsReference, nullptr);
	TupleType* d = t->AsTupleType();
	d->thisType = newTypeParameter(nullptr);
	d->thisType->AsTypeParameter()->isThisType = true;
	d->thisType->AsTypeParameter()->constraint = t;
	d->allTypeParameters = typeParameters;
	d->allTypeParameters.push_back(d->thisType);
	d->instantiations = CacheMap<Type*>{};
	d->instantiations[getTypeListKey(interfaceTypeTypeParameters(d))] = t;
	d->target = t;
	d->resolvedTypeArguments = interfaceTypeTypeParameters(d);
	d->declaredMembersResolved = true;
	d->declaredMembers = members;
	d->elementInfos = elementInfos;
	d->minLength = minLength;
	d->fixedLength = fixedLength;
	d->combinedFlags = combinedFlags;
	d->readonly = readonly;
	return t;
}

Type* Checker::getElementTypeOfSliceOfTupleType(Type* t, int index,
												int endSkipCount, bool writing,
												bool noReductions) {
	int length = getTypeReferenceArity(t) - endSkipCount;
	const std::vector<TupleElementInfo>& elementInfos =
		targetTupleType(t)->elementInfos;
	if (index < length) {
		std::vector<Type*> typeArguments = getTypeArguments(t);
		std::vector<Type*> elementTypes;
		for (int i = index; i < length; i++) {
			Type* e = typeArguments[i];
			if (elementInfos[i].flags & ElementFlagsVariadic) {
				e = getIndexedAccessType(e, numberType);
			}
			elementTypes.push_back(e);
		}
		if (writing) {
			return getIntersectionType(elementTypes);
		}
		return getUnionTypeEx(elementTypes,
							  noReductions ? UnionReduction::None
										   : UnionReduction::Literal,
							  nullptr, nullptr);
	}
	return nullptr;
}

Type* Checker::getRestTypeOfTupleType(Type* t) {
	return getElementTypeOfSliceOfTupleType(
		t, targetTupleType(t)->fixedLength, 0, false, false);
}

Type* Checker::getTupleElementTypeOutOfStartCount(Type* t, Number index,
												  Type* undefinedLikeType) {
	return mapType(t, [this, index, undefinedLikeType](Type* t) -> Type* {
		Type* restType = getRestTypeOfTupleType(t);
		if (restType == nullptr) {
			return undefinedType;
		}
		if (undefinedLikeType != nullptr &&
			!(index < static_cast<Number>(
				  getTotalFixedElementCount(targetTupleType(t))))) {
			return getUnionType({restType, undefinedLikeType});
		}
		return restType;
	});
}

bool Checker::isGenericType(Type* t) {
	return getGenericObjectFlags(t) != 0;
}

bool Checker::isGenericObjectType(Type* t) {
	return (getGenericObjectFlags(t) & ObjectFlagsIsGenericObjectType) != 0;
}

bool Checker::isGenericIndexType(Type* t) {
	return (getGenericObjectFlags(t) & ObjectFlagsIsGenericIndexType) != 0;
}

ObjectFlags Checker::getGenericObjectFlags(Type* t) {
	ObjectFlags combinedFlags = ObjectFlagsNone;
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
			t->objectFlags |= ObjectFlagsIsGenericTypeComputed | combinedFlags;
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

bool Checker::isGenericTupleType(Type* t) {
	return isTupleType(t) &&
		   (targetTupleType(t)->combinedFlags & ElementFlagsVariadic);
}

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
				nameType, newSimpleTypeMapper(
							  getTypeParameterFromMappedType(t), constraint)))) {
			return true;
		}
	}
	return false;
}

/**
 * A union type which is reducible upon instantiation (meaning some members
 * are removed under certain instantiations) must be kept generic, as that
 * instantiation information needs to flow through the type system. By
 * replacing all type parameters in the union with a special never type that
 * is treated as a literal in `getReducedType`, we can cause the
 * `getReducedType` logic to reduce the resulting type if possible (since only
 * intersections with conflicting literal-typed properties are reducible).
 */
bool Checker::isGenericReducibleType(Type* t) {
	return ((t->flags & TypeFlagsUnion) &&
			(t->objectFlags & ObjectFlagsContainsIntersections) &&
			someList(t->types(),
					 [this](Type* u) { return isGenericReducibleType(u); })) ||
		   ((t->flags & TypeFlagsIntersection) && isReducibleIntersection(t));
}

bool Checker::isReducibleIntersection(Type* t) {
	IntersectionType* d = t->AsIntersectionType();
	if (d->uniqueLiteralFilledInstantiation == nullptr) {
		d->uniqueLiteralFilledInstantiation =
			instantiateType(t, uniqueLiteralMapper);
	}
	return getReducedType(d->uniqueLiteralFilledInstantiation) !=
		   d->uniqueLiteralFilledInstantiation;
}

// NOTE: getUniqueLiteralTypeForTypeParameter is already ported in checker.cpp
// (~4502) — not duplicated here.

Type* Checker::getConditionalFlowTypeOfType(Type* t, Node* node) {
	std::vector<Type*> constraints;
	bool covariant = true;
	while (node != nullptr && !isStatement(node) &&
		   node->kind != Kind::JSDoc) {
		Node* parent = node->parent;
		// only consider variance flipped by parameter locations - `keyof` types
		// would usually be considered variance inverting, but often get used in
		// indexed accesses where they behave sortof invariantly, but our
		// checking is lax
		if (isParameterDeclaration(parent)) {
			covariant = !covariant;
		}
		// Always substitute on type parameters, regardless of variance, since
		// even in contravariant positions, they may rely on substituted
		// constraints to be valid
		if ((covariant || (t->flags & TypeFlagsTypeVariable)) &&
			isConditionalTypeNode(parent) &&
			node == parent->as<ConditionalTypeNode>()->TrueType) {
			Type* constraint = getImpliedConstraint(
				t, parent->as<ConditionalTypeNode>()->CheckType,
				parent->as<ConditionalTypeNode>()->ExtendsType);
			if (constraint != nullptr) {
				constraints.push_back(constraint);
			}
		} else if ((t->flags & TypeFlagsTypeParameter) &&
				   isMappedTypeNode(parent) &&
				   parent->as<MappedTypeNode>()->NameType == nullptr &&
				   node == parent->type()) {
			Type* mappedType = getTypeFromTypeNode(parent);
			if (getTypeParameterFromMappedType(mappedType) ==
				getActualTypeVariable(t)) {
				Type* typeParameter =
					getHomomorphicTypeVariable(mappedType);
				if (typeParameter != nullptr) {
					Type* constraint =
						getConstraintOfTypeParameter(typeParameter);
					if (constraint != nullptr &&
						everyType(constraint, [this](Type* c) {
							return isArrayOrTupleType(c);
						})) {
						constraints.push_back(
							getUnionType({numberType, numericStringType}));
					}
				}
			}
		}
		node = parent;
	}
	if (!constraints.empty()) {
		return getSubstitutionType(t, getIntersectionType(constraints));
	}
	return t;
}

Type* Checker::getImpliedConstraint(Type* t, Node* checkNode,
									Node* extendsNode) {
	if (isUnaryTupleTypeNode(checkNode) && isUnaryTupleTypeNode(extendsNode)) {
		return getImpliedConstraint(t, checkNode->elements()[0],
									extendsNode->elements()[0]);
	}
	if (getActualTypeVariable(getTypeFromTypeNode(checkNode)) ==
		getActualTypeVariable(t)) {
		return getTypeFromTypeNode(extendsNode);
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// === dep stubs — removed when owner slice lands ===
//
// Faithful signatures from Go. Owners: typeops, widen, instantiate, inference,
// decltypes, members, signatures, printer, flow.
// ---------------------------------------------------------------------------

Type* Checker::getNullableType(Type* t, TypeFlags flags) {
	TSC_UNREACHABLE("getNullableType — typenodes dep");
}

Type* Checker::addOptionality(Type* t) {
	TSC_UNREACHABLE("addOptionality — typenodes dep");
}

Type* Checker::addOptionalityEx(Type* t, bool isProperty, bool isOptional) {
	TSC_UNREACHABLE("addOptionalityEx — typenodes dep");
}

Type* Checker::getIndexedAccessType(Type* objectType, Type* indexType) {
	TSC_UNREACHABLE("getIndexedAccessType — typenodes dep");
}

Type* Checker::getIndexedAccessTypeEx(Type* objectType, Type* indexType,
									  AccessFlags accessFlags,
									  Node* accessNode, TypeAlias* alias) {
	TSC_UNREACHABLE("getIndexedAccessTypeEx — typenodes dep");
}

Type* Checker::getIndexType(Type* t) {
	TSC_UNREACHABLE("getIndexType — typenodes dep");
}

Type* Checker::getIndexTypeOfType(Type* t, Type* keyType) {
	TSC_UNREACHABLE("getIndexTypeOfType — typenodes dep");
}

std::vector<Type*> Checker::getTypeArguments(Type* t) {
	TSC_UNREACHABLE("getTypeArguments — typenodes dep");
}

int Checker::getMinTypeArgumentCount(
	const std::vector<Type*>& typeParameters) {
	TSC_UNREACHABLE("getMinTypeArgumentCount — typenodes dep");
}

std::vector<Type*> Checker::fillMissingTypeArguments(
	const std::vector<Type*>& typeArguments,
	const std::vector<Type*>& typeParameters, int minTypeArgumentCount,
	bool isJs) {
	TSC_UNREACHABLE("fillMissingTypeArguments — typenodes dep");
}

std::string Checker::TypeToStringEx(Type* t, Node* enclosingDeclaration,
									TypeFormatFlags flags,
									void* verbosityContext) {
	TSC_UNREACHABLE("TypeToStringEx — typenodes dep");
}

Type* Checker::createPromiseType(Type* type) {
	TSC_UNREACHABLE("createPromiseType — typenodes dep");
}

bool Checker::isValidIndexKeyType(Type* type) {
	TSC_UNREACHABLE("isValidIndexKeyType — typenodes dep");
}

Symbol* Checker::getPropertyOfTypeEx(Type* type, const std::string& name,
									 bool skipObjectFunctionPropertyAugment,
									 bool includeTypeOnlyMembers) {
	TSC_UNREACHABLE("getPropertyOfTypeEx — typenodes dep");
}

Type* Checker::instantiateTypeWithAlias(Type* t, TypeMapper* mapper,
										TypeAlias* alias) {
	TSC_UNREACHABLE("instantiateTypeWithAlias — typenodes dep");
}

TypeAlias* Checker::instantiateTypeAlias(TypeAlias* alias, TypeMapper* mapper) {
	TSC_UNREACHABLE("instantiateTypeAlias — typenodes dep");
}

InferenceContext* Checker::newInferenceContext(
	const std::vector<Type*>& typeParameters, Signature* signature,
	InferenceFlags flags,
	std::function<Ternary(Type*, Type*, bool)> compareTypes) {
	TSC_UNREACHABLE("newInferenceContext — typenodes dep");
}

void Checker::inferTypes(std::vector<InferenceInfo*>& inferences,
						 Type* originalSource, Type* originalTarget,
						 InferencePriority priority, bool contravariant) {
	TSC_UNREACHABLE("inferTypes — typenodes dep");
}

Type* Checker::getActualTypeVariable(Type* t) {
	TSC_UNREACHABLE("getActualTypeVariable — typenodes dep");
}

Node* Checker::getConstraintDeclaration(Type* type) {
	TSC_UNREACHABLE("getConstraintDeclaration — typenodes dep");
}

bool Checker::isTypeParameterPossiblyReferenced(Type* tp, Node* node) {
	TSC_UNREACHABLE("isTypeParameterPossiblyReferenced — typenodes dep");
}

Type* Checker::getHomomorphicTypeVariable(Type* type) {
	TSC_UNREACHABLE("getHomomorphicTypeVariable — typenodes dep");
}

Type* Checker::getTypeParameterFromMappedType(Type* type) {
	TSC_UNREACHABLE("getTypeParameterFromMappedType — typenodes dep");
}

Type* Checker::getConstraintTypeFromMappedType(Type* type) {
	TSC_UNREACHABLE("getConstraintTypeFromMappedType — typenodes dep");
}

Type* Checker::getNameTypeFromMappedType(Type* type) {
	TSC_UNREACHABLE("getNameTypeFromMappedType — typenodes dep");
}

Type* Checker::getConstraintOfTypeParameter(Type* typeParameter) {
	TSC_UNREACHABLE("getConstraintOfTypeParameter — typenodes dep");
}

Type* Checker::getSubstitutionType(Type* baseType, Type* constraint) {
	TSC_UNREACHABLE("getSubstitutionType — typenodes dep");
}

Type* Checker::getInstantiationExpressionType(Type* exprType, Node* node) {
	TSC_UNREACHABLE("getInstantiationExpressionType — typenodes dep");
}

}  // namespace checker
}  // namespace tsc
