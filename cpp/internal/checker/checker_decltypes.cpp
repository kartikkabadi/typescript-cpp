// Port of tsc/internal/checker/checker.go lines 16720-19097 — the declared-type
// layer: symbol type resolution, declared types, constraints, key building, binding
// element/pattern types, widening, accessor types, and nullability helpers.
//
// === slice: decltypes ===
//
// Dep stubs for callees owned by other slices live at the bottom of this file in
// the "// === dep stubs ===" section. Small free functions (Go package-level or
// utilities.go helpers) are duplicated as file-local statics below; when their
// owning slices land, the copies here can be deleted.
#include "internal/checker/checker.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>

#include "internal/binder/binder.h"
#include "internal/binder/nameresolver.h"
#include "internal/checker/mapper.h"
#include "internal/jsnum/jsnum.h"
#include "internal/scanner/scanner.h"

namespace tsc {
namespace checker {

namespace {

// ---------------------------------------------------------------------------
// Small generic helpers — core package equivalents (slices/core utilities).
// ---------------------------------------------------------------------------

template <typename T>
T orElse(const T& a, const T& b) {
	return a ? a : b;
}

// core.SameMap — returns the input unchanged when f is the identity on every element.
template <typename T, typename F>
std::vector<T> sameMap(const std::vector<T>& values, F f) {
	std::vector<T> result;
	result.reserve(values.size());
	bool same = true;
	for (const T& v : values) {
		T mapped = f(v);
		result.push_back(mapped);
		if (!(mapped == v)) {
			same = false;
		}
	}
	return same ? values : result;
}

template <typename T, typename F>
auto mapVec(const std::vector<T>& values, F f) -> std::vector<decltype(f(std::declval<T>()))> {
	std::vector<decltype(f(std::declval<T>()))> result;
	result.reserve(values.size());
	for (const T& v : values) {
		result.push_back(f(v));
	}
	return result;
}

template <typename T, typename F>
auto mapIndex(const std::vector<T>& values, F f)
	-> std::vector<decltype(f(std::declval<T>(), 0))> {
	using R = decltype(f(std::declval<T>(), 0));
	std::vector<R> result;
	result.reserve(values.size());
	for (size_t i = 0; i < values.size(); i++) {
		result.push_back(f(values[i], static_cast<int>(i)));
	}
	return result;
}

template <typename T>
void appendIfUnique(std::vector<T>& values, const T& value) {
	if (std::find(values.begin(), values.end(), value) == values.end()) {
		values.push_back(value);
	}
}

template <typename T, typename F>
T* findOrNull(const std::vector<T*>& values, F f) {
	auto it = std::find_if(values.begin(), values.end(), f);
	return it == values.end() ? nullptr : *it;
}

template <typename F>
int findLastIndex(const std::vector<Node*>& values, F f) {
	for (int i = static_cast<int>(values.size()) - 1; i >= 0; i--) {
		if (f(values[i])) {
			return i;
		}
	}
	return -1;
}

template <typename T, typename F>
bool someOf(const std::vector<T>& values, F f) {
	return std::any_of(values.begin(), values.end(), f);
}

template <typename T, typename F>
bool everyOf(const std::vector<T>& values, F f) {
	return std::all_of(values.begin(), values.end(), f);
}

template <typename T>
bool containsElem(const std::vector<T>& values, const T& v) {
	return std::find(values.begin(), values.end(), v) != values.end();
}

template <typename T>
int indexOf(const std::vector<T>& values, const T& v) {
	auto it = std::find(values.begin(), values.end(), v);
	return it == values.end() ? -1 : static_cast<int>(it - values.begin());
}

// ---------------------------------------------------------------------------
// utilities.go / flow.go / checker.go helpers used by this slice
// ---------------------------------------------------------------------------

// everyType — checker.go:27010 (non-static copy lives in checker.cpp)
template <typename F>
bool everyType(Type* t, F f) {
	if (t->flags & TypeFlagsUnion) {
		return std::all_of(t->types().begin(), t->types().end(), f);
	}
	return f(t);
}

// isTupleType — checker.go:23946
bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 &&
		(t->Target()->objectFlags & ObjectFlagsTuple) != 0;
}

// isShorthandAmbientModule — utilities.go:201
bool isShorthandAmbientModule(Node* node) {
	// The only kind of module that can be missing a body is a shorthand ambient module.
	return node != nullptr && node->kind == Kind::ModuleDeclaration && node->body() == nullptr;
}


// isRightSideOfAccessExpression — utilities.go:240
bool isRightSideOfAccessExpression(Node* node) {
	return node->parent != nullptr &&
		(isPropertyAccessExpression(node->parent) && node->parent->name() == node ||
		 isElementAccessExpression(node->parent) &&
			 node->parent->as<ElementAccessExpression>()->ArgumentExpression == node);
}

// isRightSideOfQualifiedNameOrPropertyAccess — canonical def in ast.cpp
// isDeclarationName — ast.IsDeclarationName. isDeclarationNode is currently a
// generated stub returning false; the port stays faithful so it becomes live
// when the real predicate lands.

// walkUpBindingElementsAndPatterns — ast.WalkUpBindingElementsAndPatterns
Node* walkUpBindingElementsAndPatterns(Node* node) {
	while (node != nullptr && (isBindingElement(node) || isBindingPattern(node))) {
		node = node->parent;
	}
	return node;
}

// hasDotDotDotToken — utilities.go:272
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

// IsTypeAny — utilities.go:286
bool IsTypeAny(Type* t) {
	return t != nullptr && (t->flags & TypeFlagsAny) != 0;
}

// isOptionalDeclaration — utilities.go:298
bool isOptionalDeclaration(Node* declaration) {
	return hasQuestionToken(declaration);
}

// isEmptyArrayLiteral (utilities.go:4042) — canonical in ast.cpp

// isPrivateWithinAmbient — utilities.go:342
bool isPrivateWithinAmbient(Node* node) {
	return (hasSyntacticModifier(node, ModifierFlagsPrivate) ||
			isPrivateIdentifierClassElementDeclaration(node)) &&
		(node->flags & NodeFlagsAmbient) != 0;
}

// isObjectLiteralType — utilities.go:867
bool isObjectLiteralType(Type* t) {
	return (t->objectFlags & ObjectFlagsObjectLiteral) != 0;
}

// isDeclarationReadonly — utilities.go:871
bool isDeclarationReadonly(Node* declaration) {
	return (getCombinedModifierFlags(declaration) & ModifierFlagsReadonly) != 0 &&
		!isParameterPropertyDeclaration(declaration, declaration->parent);
}

// isTypeUsableAsPropertyName — utilities.go:922
bool isTypeUsableAsPropertyName(Type* t) {
	return (t->flags & TypeFlagsStringOrNumberLiteralOrUnique) != 0;
}

// getStringLiteralValue / getNumberLiteralValue — literal value extraction
// (identical static copies live in checker.cpp).
std::string getStringLiteralValue(Type* t) {
	return std::get<std::string>(t->AsLiteralType()->value);
}
Number getNumberLiteralValue(Type* t) {
	return std::get<Number>(t->AsLiteralType()->value);
}

// getPropertyNameFromType — utilities.go:929
std::string getPropertyNameFromType(Type* t) {
	if (t->flags & TypeFlagsStringLiteral) {
		return getStringLiteralValue(t);
	}
	if (t->flags & TypeFlagsNumberLiteral) {
		return getNumberLiteralValue(t).string();
	}
	if (t->flags & TypeFlagsUniqueESSymbol) {
		return t->AsUniqueESSymbolType()->name;
	}
	TSC_UNREACHABLE("Unhandled case in getPropertyNameFromType");
}

// getFlowNodeOfNode — flow.go:69
FlowNode* getFlowNodeOfNode(Node* node) {
	auto data = node->flowNodeData();
	return data.flowNode != nullptr ? *data.flowNode : nullptr;
}

// getClassLikeDeclarationOfSymbol — ast.GetClassLikeDeclarationOfSymbol
Node* getClassLikeDeclarationOfSymbol(Symbol* symbol) {
	return findOrNull(symbol->declarations, isClassLike);
}


// getBaseTypeNodeOfClass — checker.go:19604
Node* getBaseTypeNodeOfClass(Type* t) {
	Node* decl = getClassLikeDeclarationOfSymbol(t->symbol);
	if (decl != nullptr) {
		return getClassExtendsHeritageElement(decl);
	}
	return nullptr;
}

// isCheckJSEnabledForFile — ast.IsCheckJSEnabledForFile

// getDeclarationModifierFlagsFromSymbol / Ex — utilities.go:757-797
ModifierFlags getDeclarationModifierFlagsFromSymbolEx(Symbol* s, bool isWrite);
ModifierFlags getDeclarationModifierFlagsFromSymbol(Symbol* s) {
	return getDeclarationModifierFlagsFromSymbolEx(s, false /*isWrite*/);
}

ModifierFlags getDeclarationModifierFlagsFromSymbolEx(Symbol* s, bool isWrite) {
	if (s->checkFlags & CheckFlagsSynthetic) {
		ModifierFlags accessModifier = ModifierFlagsNone;
		if (!isWrite && (s->checkFlags & CheckFlagsContainsPublic) ||
			isWrite && (s->checkFlags & CheckFlagsContainsWritePublic)) {
			accessModifier = ModifierFlagsPublic;
		} else if (!isWrite && (s->checkFlags & CheckFlagsContainsProtected) ||
				   isWrite && (s->checkFlags & CheckFlagsContainsWriteProtected)) {
			accessModifier = ModifierFlagsProtected;
		} else if (!isWrite && (s->checkFlags & CheckFlagsContainsPrivate) ||
				   isWrite && (s->checkFlags & CheckFlagsContainsWritePrivate)) {
			accessModifier = ModifierFlagsPrivate;
		}
		if (s->checkFlags & CheckFlagsContainsStatic) {
			return accessModifier | ModifierFlagsStatic;
		}
		return accessModifier;
	}
	if (s->valueDeclaration != nullptr) {
		Node* declaration = nullptr;
		if (isWrite) {
			declaration = findOrNull(s->declarations, isSetAccessorDeclaration);
		}
		if (declaration == nullptr && (s->flags & SymbolFlagsGetAccessor)) {
			declaration = findOrNull(s->declarations, isGetAccessorDeclaration);
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

// forward decls for mutually recursive helpers
bool isTypeReferenceWithGenericArguments(Type* t);
bool isNonDeferredTypeReference(Type* t);
bool isUnconstrainedTypeParameter(Type* tp);

// ---------------------------------------------------------------------------
// keyBuilder — content-keyed interning. Mirrors the byte-buffer design in
// checker.cpp (a deliberate divergence from Go's xxh3 128-bit hash — the keys
// never escape the checker, so keying on raw content words is equivalent).
// This copy adds writeGenericTypeReferences (checker.go:17787).
// ---------------------------------------------------------------------------

struct keyBuilder {
	std::string buf;

	// hash — checker.go:17709
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

	// writeByte — checker.go:17723
	void writeByte(uint8_t c) { buf.push_back(static_cast<char>(c)); }
	// writeString — checker.go:17731
	void writeString(const std::string& s) { buf += s; }
	// writeUint32 — checker.go:17742
	void writeUint32(uint32_t v) {
		char b[4];
		std::memcpy(b, &v, 4);
		buf.append(b, 4);
	}
	// writeUint64 — checker.go:17750
	void writeUint64(uint64_t v) {
		char b[8];
		std::memcpy(b, &v, 8);
		buf.append(b, 8);
	}
	// writeInt — checker.go:17758
	void writeInt(int v) { writeUint64(static_cast<uint64_t>(v)); }
	// writeSymbol — checker.go:17762
	void writeSymbol(Symbol* s) { writeUint64(static_cast<uint64_t>(getSymbolId(s))); }
	// writeType — checker.go:17766
	void writeType(Type* t) { writeUint32(static_cast<uint32_t>(t->id)); }
	// writeTypes — checker.go:17770
	void writeTypes(const std::vector<Type*>& types) {
		writeInt(static_cast<int>(types.size()));
		for (Type* t : types) {
			writeType(t);
		}
	}
	// writeAlias — checker.go:17777
	void writeAlias(TypeAlias* alias) {
		if (alias != nullptr) {
			writeByte(1);
			writeSymbol(alias->symbol);
			writeTypes(alias->typeArguments);
		} else {
			writeByte(0);
		}
	}
	// writeGenericTypeReferences — checker.go:17787
	[[maybe_unused]] bool writeGenericTypeReferences(Type* source, Type* target,
													 bool ignoreConstraints) {
		bool constrained = false;
		std::vector<Type*> typeParameters;
		typeParameters.reserve(8);
		std::function<void(Type*, int)> writeTypeReference = [&](Type* ref, int depth) {
			writeType(ref->Target());
			for (Type* t : ref->AsTypeReference()->resolvedTypeArguments) {
				if (t->flags & TypeFlagsTypeParameter) {
					if (ignoreConstraints || t->checker->getConstraintOfTypeParameter(t) == nullptr) {
						int index = indexOf(typeParameters, t);
						if (index < 0) {
							index = static_cast<int>(typeParameters.size());
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
	// writeNodeId — checker.go:17822
	void writeNodeId(NodeId id) { writeUint64(static_cast<uint64_t>(id)); }
	// writeNode — checker.go:17826
	void writeNode(Node* node) {
		if (node != nullptr) {
			writeNodeId(getNodeId(node));
		}
	}
};

// getTypeListKey — checker.go:17832 (static copy in checker.cpp)
[[maybe_unused]] CacheKey getTypeListKey(const std::vector<Type*>& types) {
	keyBuilder b;
	b.writeTypes(types);
	return b.hash();
}

// getAliasKey — checker.go:17838 (static copy in checker.cpp)
[[maybe_unused]] CacheKey getAliasKey(TypeAlias* alias) {
	keyBuilder b;
	b.writeAlias(alias);
	return b.hash();
}

// getUnionKey — checker.go:17844 (static copy in checker.cpp)
[[maybe_unused]] CacheKey getUnionKey(const std::vector<Type*>& types, Type* origin,
									  TypeAlias* alias) {
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
		// origin type id alone is insufficient, as `keyof x` may resolve to multiple
		// WIP values while `x` is still resolving
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

// getIntersectionKey — checker.go:17868 (static copy in checker.cpp)
[[maybe_unused]] CacheKey getIntersectionKey(const std::vector<Type*>& types,
											 IntersectionFlags flags, TypeAlias* alias) {
	keyBuilder b;
	b.writeTypes(types);
	if (!(flags & IntersectionFlagsNoConstraintReduction)) {
		b.writeAlias(alias);
	} else {
		b.writeByte('*');
	}
	return b.hash();
}

// getTupleKey — checker.go:17879 (static copy in checker.cpp)
[[maybe_unused]] CacheKey getTupleKey(const std::vector<TupleElementInfo>& elementInfos,
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
		b.writeByte('!');
	}
	return b.hash();
}

// getTypeInstantiationKey — checker.go:17906
[[maybe_unused]] CacheKey getTypeInstantiationKey(const std::vector<Type*>& typeArguments,
												TypeAlias* alias, bool singleSignature) {
	keyBuilder b;
	b.writeTypes(typeArguments);
	b.writeAlias(alias);
	if (singleSignature) {
		b.writeByte('!');
	}
	return b.hash();
}

// getTypeAliasInstantiationKey — checker.go:17902
[[maybe_unused]] CacheKey getTypeAliasInstantiationKey(const std::vector<Type*>& typeArguments,
													   TypeAlias* alias) {
	return getTypeInstantiationKey(typeArguments, alias, false);
}

// getIndexedAccessKey — checker.go:17916
[[maybe_unused]] CacheKey getIndexedAccessKey(Type* objectType, Type* indexType,
											  AccessFlags accessFlags, TypeAlias* alias) {
	keyBuilder b;
	b.writeType(objectType);
	b.writeType(indexType);
	b.writeUint32(static_cast<uint32_t>(accessFlags));
	b.writeAlias(alias);
	return b.hash();
}

// getTemplateTypeKey — checker.go:17925
[[maybe_unused]] CacheKey getTemplateTypeKey(const std::vector<std::string>& texts,
											 const std::vector<Type*>& types) {
	keyBuilder b;
	b.writeTypes(types);
	b.writeByte('|');
	for (const std::string& s : texts) {
		b.writeInt(static_cast<int>(s.size()));
	}
	b.writeByte('|');
	for (const std::string& s : texts) {
		b.writeString(s);
	}
	return b.hash();
}

// getConditionalTypeKey — checker.go:17939
[[maybe_unused]] CacheKey getConditionalTypeKey(const std::vector<Type*>& typeArguments,
												TypeAlias* alias, bool forConstraint) {
	keyBuilder b;
	b.writeTypes(typeArguments);
	b.writeAlias(alias);
	if (forConstraint) {
		b.writeByte('!');
	}
	return b.hash();
}

// getRelationKey — checker.go:17949
[[maybe_unused]] std::pair<CacheKey, bool> getRelationKey(Type* source, Type* target,
														IntersectionState intersectionState,
														bool isIdentity,
														bool ignoreConstraints) {
	if (isIdentity && source->id > target->id) {
		std::swap(source, target);
	}
	keyBuilder b;
	bool constrained = false;
	if (isTypeReferenceWithGenericArguments(source) && isTypeReferenceWithGenericArguments(target)) {
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

// getNodeListKey — checker.go:17967
[[maybe_unused]] CacheKey getNodeListKey(const std::vector<Node*>& nodes) {
	keyBuilder b;
	b.writeInt(static_cast<int>(nodes.size()));
	for (Node* n : nodes) {
		b.writeNode(n);
	}
	return b.hash();
}

// isTypeReferenceWithGenericArguments — checker.go:17976
bool isTypeReferenceWithGenericArguments(Type* t) {
	return isNonDeferredTypeReference(t) &&
		someOf(t->checker->getTypeArguments(t), [](Type* u) {
			return (u->flags & TypeFlagsTypeParameter) != 0 || isTypeReferenceWithGenericArguments(u);
		});
}

// isNonDeferredTypeReference — checker.go:17982
bool isNonDeferredTypeReference(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 && t->AsTypeReference()->node == nullptr;
}

// isUnconstrainedTypeParameter — checker.go:17987. Return true if the type
// parameter originates in an unconstrained declaration in a type parameter list.
bool isUnconstrainedTypeParameter(Type* tp) {
	Type* target = tp->Target();
	if (target == nullptr) {
		target = tp;
	}
	if (target->symbol == nullptr) {
		return false;
	}
	for (Node* d : target->symbol->declarations) {
		if (isTypeParameterDeclaration(d) &&
			(d->as<TypeParameterDeclaration>()->Constraint != nullptr ||
			 isMappedTypeNode(d->parent) || isInferTypeNode(d->parent))) {
			return false;
		}
	}
	return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Type methods — types.go:740-798 (declared in the slice block on struct Type)
// ---------------------------------------------------------------------------

// Distributed — types.go:740
std::vector<Type*> Type::Distributed() {
	if (flags & TypeFlagsUnion) {
		return AsUnionType()->types;
	}
	if (flags & TypeFlagsNever) {
		return {};
	}
	return {this};
}


// TargetInterfaceType — types.go:790
InterfaceType* Type::TargetInterfaceType() {
	return AsTypeReference()->target->AsInterfaceType();
}

// TargetTupleType — types.go:794
TupleType* Type::TargetTupleType() {
	return AsTypeReference()->target->AsTupleType();
}

// ---------------------------------------------------------------------------
// checker.go:16720-17638 — symbol type resolution, variable/parameter/property
// types, class/func/enum/module worker types, constraint machinery.
// ---------------------------------------------------------------------------

// getTypeOfSymbolWithDeferredType / getWriteTypeOfSymbolWithDeferredType /
// getWriteTypeOfSymbol — checker.go:16720-16772: already ported in checker.cpp
// (getWriteTypeOfSymbol's missing Property/Accessor branches were completed there
// by this slice).

// GetTypeOfSymbolAtLocation — checker.go:16773
Type* Checker::GetTypeOfSymbolAtLocation(Symbol* symbol, Node* location) {
	symbol = getExportSymbolOfValueSymbolIfExported(symbol);
	if (location != nullptr) {
		// If we have an identifier or a property access at the given location, if the location is
		// an dotted name expression, and if the location is not an assignment target, obtain the type
		// of the expression (which will reflect control flow analysis). If the expression indeed
		// resolved to the given symbol, return the narrowed type.
		if ((isIdentifier(location) || isPrivateIdentifier(location)) &&
			!(isJsxTagName(location) || isJsxAttribute(location->parent) ||
			  isJsxNamespacedName(location->parent))) {
			if (isRightSideOfQualifiedNameOrPropertyAccess(location)) {
				location = location->parent;
			}
			if (isExpressionNode(location) && (!isAssignmentTarget(location) || isWriteAccess(location))) {
				Type* t;
				if (isWriteAccess(location) && location->kind == Kind::PropertyAccessExpression) {
					t = checkPropertyAccessExpression(location, CheckModeNormal, true /*writeOnly*/);
				} else {
					t = getTypeOfExpression(location);
				}
				if (getExportSymbolOfValueSymbolIfExported(symbolNodeLinks.Get(location)->resolvedSymbol) ==
					symbol) {
					return removeOptionalTypeMarker(t);
				}
			}
		}
		if (isDeclarationName(location) && isSetAccessorDeclaration(location->parent) &&
			getAnnotatedAccessorTypeNode(location->parent) != nullptr) {
			return getWriteTypeOfAccessors(location->parent->symbol());
		}
		// The location isn't a reference to the given symbol, meaning we're being asked
		// a hypothetical question of what type the symbol would have if there was a reference
		// to it at the given location. Since we have no control flow information for the
		// hypothetical reference (control flow information is created and attached by the
		// binder), we simply return the declared type of the symbol.
		if (isRightSideOfAccessExpression(location) && isWriteAccess(location->parent)) {
			return getWriteTypeOfSymbol(symbol);
		}
	}
	return getNonMissingTypeOfSymbol(symbol);
}

// getTypeOfSymbol — checker.go:16812
Type* Checker::getTypeOfSymbol(Symbol* symbol) {
	if (symbol->checkFlags & CheckFlagsDeferredType) {
		return getTypeOfSymbolWithDeferredType(symbol);
	}
	if (symbol->checkFlags & CheckFlagsInstantiated) {
		return getTypeOfInstantiatedSymbol(symbol);
	}
	if (symbol->checkFlags & CheckFlagsMapped) {
		return getTypeOfMappedSymbol(symbol);
	}
	if (symbol->checkFlags & CheckFlagsReverseMapped) {
		return getTypeOfReverseMappedSymbol(symbol);
	}
	if (symbol->flags & SymbolFlagsAccessor) {
		return getTypeOfAccessors(symbol);
	}
	if (symbol->flags & (SymbolFlagsVariable | SymbolFlagsProperty)) {
		return getTypeOfVariableOrParameterOrProperty(symbol);
	}
	if (symbol->flags &
		(SymbolFlagsFunction | SymbolFlagsMethod | SymbolFlagsClass | SymbolFlagsEnum |
		 SymbolFlagsValueModule)) {
		return getTypeOfFuncClassEnumModule(symbol);
	}
	if (symbol->flags & SymbolFlagsEnumMember) {
		return getTypeOfEnumMember(symbol);
	}
	if (symbol->flags & SymbolFlagsAlias) {
		return getTypeOfAlias(symbol);
	}
	return errorType;
}

// getNonMissingTypeOfSymbol — checker.go:16843
Type* Checker::getNonMissingTypeOfSymbol(Symbol* symbol) {
	return removeMissingType(getTypeOfSymbol(symbol), (symbol->flags & SymbolFlagsOptional) != 0);
}

// getTypeOfInstantiatedSymbol — checker.go:16847
Type* Checker::getTypeOfInstantiatedSymbol(Symbol* symbol) {
	auto* links = valueSymbolLinks.Get(symbol);
	if (links->resolvedType == nullptr) {
		links->resolvedType = instantiateType(getTypeOfSymbol(links->target), links->mapper);
	}
	return links->resolvedType;
}

// getWriteTypeOfInstantiatedSymbol — checker.go:16855
Type* Checker::getWriteTypeOfInstantiatedSymbol(Symbol* symbol) {
	auto* links = valueSymbolLinks.Get(symbol);
	if (links->writeType == nullptr) {
		links->writeType = instantiateType(getWriteTypeOfSymbol(links->target), links->mapper);
	}
	return links->writeType;
}

// getTypeOfVariableOrParameterOrProperty — checker.go:16863
Type* Checker::getTypeOfVariableOrParameterOrProperty(Symbol* symbol) {
	auto* links = valueSymbolLinks.Get(symbol);
	// Go's per-checker link store means a type resolved while a different
	// file was being checked (or outside any check) must not short-circuit
	// this file's own resolution: drop the foreign-context entry so the
	// worker re-runs and its diagnostics (e.g. reportImplicitAny) re-fire,
	// attributed here via Diagnostic::producedDuringCheckOf. The entry must
	// be cleared during re-resolution — typeResolutionHasProperty reads
	// resolvedType != nullptr to detect cycles.
	if (links->resolvedType != nullptr && checkFileTagStale(links->resolvedTypeCheckFile)) {
		links->resolvedType = nullptr;
	}
	if (links->resolvedType == nullptr) {
		Type* t = getTypeOfVariableOrParameterOrPropertyWorker(symbol);
		if (t == nullptr) {
			TSC_UNREACHABLE("Unexpected nil type");
		}
		// For a contextually typed parameter it is possible that a type has already
		// been assigned (in assignTypeToParameterAndFixTypeParameters), and we want
		// to preserve this type. In fact, we need to _prefer_ that type, but it won't
		// be assigned until contextual typing is complete, so we need to defer in
		// cases where contextual typing may take place.
		if (links->resolvedType == nullptr && !isParameterOfContextSensitiveSignature(symbol)) {
			links->resolvedType = t;
		}
		links->resolvedTypeCheckFile = checkFileTag();
		return t;
	}
	return links->resolvedType;
}

// isParameterOfContextSensitiveSignature — checker.go:16883
bool Checker::isParameterOfContextSensitiveSignature(Symbol* symbol) {
	Node* decl = symbol->valueDeclaration;
	if (decl == nullptr) {
		return false;
	}
	if (isBindingElement(decl)) {
		decl = walkUpBindingElementsAndPatterns(decl);
	}
	if (isParameterDeclaration(decl)) {
		return isContextSensitiveFunctionOrObjectLiteralMethod(decl->parent);
	}
	return false;
}

// getTypeOfVariableOrParameterOrPropertyWorker — checker.go:16897
Type* Checker::getTypeOfVariableOrParameterOrPropertyWorker(Symbol* symbol) {
	// Handle prototype property
	if (symbol->flags & SymbolFlagsPrototype) {
		return getTypeOfPrototypeProperty(symbol);
	}
	// CommonsJS require and module both have type any.
	if (symbol == requireSymbol) {
		return anyType;
	}
	TSC_ASSERT(symbol->valueDeclaration != nullptr, "symbol->valueDeclaration != nullptr");
	Node* declaration = symbol->valueDeclaration;
	if (isSourceFile(declaration) && isJsonSourceFile(declaration->as<SourceFile>())) {
		auto statements = declaration->statements();
		if (statements.empty()) {
			return emptyObjectType;
		}
		return getWidenedType(getWidenedLiteralType(checkExpression(statements[0]->expression())));
	}
	// Handle variable, parameter or property
	if (!pushTypeResolution(symbol, TypeSystemPropertyName::Type)) {
		return reportCircularityError(symbol);
	}
	if (symbol->flags & SymbolFlagsModuleExports) {
		if (symbol->name == "exports") {
			return getTypeOfSymbol(
				resolveExternalModuleSymbol(symbol->valueDeclaration->symbol(), false /*dontResolveAlias*/));
		}
		return newAnonymousType(symbol, symbol->members, {}, {}, {});
	}
	Type* result;
	switch (declaration->kind) {
	case Kind::Parameter:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::VariableDeclaration:
	case Kind::BindingElement:
		// only report diagnostics for context-insensitive parameters - context-sensitive ones may
		// have their type fixed to something else
		result = getWidenedTypeForVariableLikeDeclaration(
			declaration, !isParameterOfContextSensitiveSignature(symbol));
		break;
	case Kind::PropertyAssignment:
		result = checkPropertyAssignment(declaration, CheckModeNormal);
		break;
	case Kind::ShorthandPropertyAssignment:
		result = checkShorthandPropertyAssignment(declaration, true /*inDestructuringPattern*/,
												  CheckModeNormal);
		break;
	case Kind::MethodDeclaration:
		result = checkObjectLiteralMethod(declaration, CheckModeNormal);
		break;
	case Kind::ExportAssignment:
		if (declaration->type() != nullptr) {
			result = getTypeFromTypeNode(declaration->type());
		} else {
			result = widenTypeForVariableLikeDeclaration(
				checkExpressionCached(declaration->expression()), declaration, false /*reportErrors*/);
		}
		break;
	case Kind::BinaryExpression:
	case Kind::CallExpression:
		result = getWidenedTypeForAssignmentDeclaration(symbol);
		break;
	case Kind::JsxAttribute:
		result = checkJsxAttribute(declaration, CheckModeNormal);
		break;
	case Kind::EnumMember:
		result = getTypeOfEnumMember(symbol);
		break;
	default:
		TSC_UNREACHABLE("Unhandled case in getTypeOfVariableOrParameterOrPropertyWorker");
	}
	if (!popTypeResolution()) {
		return reportCircularityError(symbol);
	}
	return result;
}

// Return the type associated with a variable, parameter, or property declaration. In the simple case
// this is the type specified in a type annotation or inferred from an initializer. However, in the
// case of a destructuring declaration it is a bit more involved. For example:
//
//	var [x, s = ""] = [1, "one"];
//
// Here, the array literal [1, "one"] is contextually typed by the type [any, string], which is the
// implied type of the binding pattern [x, s = ""]. Because the contextual type is a tuple type, the
// resulting type of [1, "one"] is the tuple type [number, string]. Thus, the type inferred for 'x'
// is number and the type inferred for 's' is string.
// getWidenedTypeForVariableLikeDeclaration — checker.go:16966
Type* Checker::getWidenedTypeForVariableLikeDeclaration(Node* declaration, bool reportErrors) {
	return widenTypeForVariableLikeDeclaration(
		getTypeForVariableLikeDeclaration(declaration, /*includeOptionality*/ true, CheckModeNormal),
		declaration, reportErrors);
}

// Return the inferred type for a variable, parameter, or property declaration
// getTypeForVariableLikeDeclaration — checker.go:16971
Type* Checker::getTypeForVariableLikeDeclaration(Node* declaration, bool includeOptionality,
												 CheckMode checkMode) {
	// A variable declared in a for..in statement is of type string, or of type keyof T when the
	// right hand expression is of a type parameter type.
	if (isVariableDeclaration(declaration)) {
		Node* grandParent = declaration->parent->parent;
		switch (grandParent->kind) {
		case Kind::ForInStatement: {
			Type* indexType = getIndexType(
				getNonNullableTypeIfNeeded(
					checkExpressionEx(grandParent->expression(), checkMode /*checkMode*/)));
			if (indexType->flags & (TypeFlagsTypeParameter | TypeFlagsIndex)) {
				return getExtractStringType(indexType);
			}
			return stringType;
		}
		case Kind::ForOfStatement:
			// checkRightHandSideOfForOf will return undefined if the for-of expression type was
			// missing properties/signatures required to get its iteratedType (like
			// [Symbol.iterator] or next). This may be because we accessed properties from anyType,
			// or it may have led to an error inside getElementTypeOfIterable.
			return checkRightHandSideOfForOf(grandParent);
		default:
			break;
		}
	} else if (isBindingElement(declaration)) {
		return getTypeForBindingElement(declaration);
	}
	bool isProperty =
		isPropertyDeclaration(declaration) && !hasAccessorModifier(declaration) ||
		isPropertySignatureDeclaration(declaration);
	bool isOptional = includeOptionality && isOptionalDeclaration(declaration);
	// Use type from type annotation if one is present
	Type* declaredType = tryGetTypeFromTypeNode(declaration);
	if (isCatchClauseVariableDeclarationOrBindingElement(declaration)) {
		if (declaredType != nullptr) {
			// If the catch clause is explicitly annotated with any or unknown, accept it, otherwise error.
			if (declaredType->flags & TypeFlagsAnyOrUnknown) {
				return declaredType;
			}
			return errorType;
		}
		// If the catch clause is not explicitly annotated, treat it as though it were explicitly
		// annotated with unknown or any, depending on useUnknownInCatchVariables.
		if (useUnknownInCatchVariables) {
			return unknownType;
		} else {
			return anyType;
		}
	}
	if (declaredType != nullptr) {
		return addOptionalityEx(declaredType, isProperty, isOptional);
	}
	if (noImplicitAny && isVariableDeclaration(declaration) &&
		!isBindingPattern(declaration->name()) &&
		(getCombinedModifierFlagsCached(declaration) & ModifierFlagsExport) == 0 &&
		(declaration->flags & NodeFlagsAmbient) == 0) {
		// If --noImplicitAny is on or the declaration is in a Javascript file,
		// use control flow tracked 'any' type for non-ambient, non-exported var or let variables with no
		// initializer or a 'null' or 'undefined' initializer.
		Node* initializer = declaration->initializer();
		if ((getCombinedNodeFlagsCached(declaration) & NodeFlagsConstant) == 0 &&
			(initializer == nullptr || isNullOrUndefined(initializer))) {
			return autoType;
		}
		// Use control flow tracked 'any[]' type for non-ambient, non-exported variables with an empty
		// array literal initializer.
		if (initializer != nullptr && isEmptyArrayLiteral(initializer)) {
			return autoArrayType;
		}
	}
	if (isParameterDeclaration(declaration)) {
		if (declaration->symbol() == nullptr) {
			// parameters of function types defined in JSDoc in TS files don't have symbols
			return nullptr;
		}
		Node* fn = declaration->parent;
		// For a parameter of a set accessor, use the type of the get accessor if one is present
		if (isSetAccessorDeclaration(fn) && hasBindableName(fn)) {
			Node* getter = getDeclarationOfKind(getSymbolOfDeclaration(declaration->parent),
											  Kind::GetAccessor);
			if (getter != nullptr) {
				Signature* getterSignature = getSignatureFromDeclaration(getter);
				Node* thisParameter = getAccessorThisParameter(fn);
				if (thisParameter != nullptr && declaration == thisParameter) {
					// Use the type from the *getter*
					TSC_ASSERT(thisParameter->type() == nullptr, "thisParameter->type() == nullptr");
					return getTypeOfSymbol(getterSignature->thisParameter);
				}
				return getReturnTypeOfSignature(getterSignature);
			}
		}
		if (Type* t = getParameterTypeOfFullSignature(fn, declaration); t != nullptr) {
			return t;
		}
		// Use contextual parameter type if one is available
		Type* t;
		if (declaration->symbol()->name == InternalSymbolNameThis) {
			t = getContextualThisParameterType(fn);
		} else {
			t = getContextuallyTypedParameterType(declaration);
		}
		if (t != nullptr) {
			return addOptionalityEx(t, false /*isProperty*/, isOptional);
		}
	}
	// Use the type of the initializer expression if one is present and the declaration is
	// not a parameter of a contextually typed function
	if (declaration->initializer() != nullptr) {
		Type* t = widenTypeInferredFromInitializer(
			declaration,
			checkDeclarationInitializer(declaration, checkMode, nullptr /*contextualType*/));
		return addOptionalityEx(t, isProperty, isOptional);
	}
	if (noImplicitAny && isPropertyDeclaration(declaration)) {
		// We have a property declaration with no type annotation or initializer, in noImplicitAny
		// mode or a .js file. Use control flow analysis of this.xxx assignments in the constructor or
		// static block to determine the type of the property.
		if (!hasStaticModifier(declaration)) {
			Node* constructor = findConstructorDeclaration(declaration->parent);
			Type* t;
			if (constructor != nullptr) {
				t = getFlowTypeInConstructor(declaration->symbol(), constructor);
			} else if (declaration->modifierFlags() & ModifierFlagsAmbient) {
				t = getTypeOfPropertyInBaseClass(declaration->symbol());
			} else {
				t = nullptr;
			}
			if (t == nullptr) {
				return nullptr;
			}
			return addOptionalityEx(t, true /*isProperty*/, isOptional);
		} else {
			std::vector<Node*> staticBlocks;
			for (Node* member : declaration->parent->members()) {
				if (isClassStaticBlockDeclaration(member)) {
					staticBlocks.push_back(member);
				}
			}
			Type* t;
			if (!staticBlocks.empty()) {
				t = getFlowTypeInStaticBlocks(declaration->symbol(), staticBlocks);
			} else if (declaration->modifierFlags() & ModifierFlagsAmbient) {
				t = getTypeOfPropertyInBaseClass(declaration->symbol());
			} else {
				t = nullptr;
			}
			if (t == nullptr) {
				return nullptr;
			}
			return addOptionalityEx(t, true /*isProperty*/, isOptional);
		}
	}
	if (isJsxAttribute(declaration)) {
		// if JSX attribute doesn't have initializer, by default the attribute will have boolean
		// value of true. I.e <Elem attr /> is sugar for <Elem attr={true} />
		return trueType;
	}
	// If the declaration specifies a binding pattern and is not a parameter of a contextually
	// typed function, use the type implied by the binding pattern
	if (isBindingPattern(declaration->name())) {
		return getTypeFromBindingPattern(declaration->name(), /*includePatternInType*/ false,
										 /*reportErrors*/ true);
	}
	// No type specified and nothing can be inferred
	return nullptr;
}

// checkDeclarationInitializer — checker.go:17116
Type* Checker::checkDeclarationInitializer(Node* declaration, CheckMode checkMode,
										   Type* contextualType) {
	Node* initializer = declaration->initializer();
	Type* t = getQuickTypeOfExpression(initializer);
	if (t == nullptr) {
		if (contextualType != nullptr) {
			t = checkExpressionWithContextualType(initializer, contextualType,
												nullptr /*inferenceContext*/, checkMode);
		} else {
			t = checkExpressionCachedEx(initializer, checkMode);
		}
	}
	if (isParameterDeclaration(getRootDeclaration(declaration))) {
		Node* name = declaration->name();
		switch (name->kind) {
		case Kind::ObjectBindingPattern:
			if (isObjectLiteralType(t)) {
				return padObjectLiteralType(t, name);
			}
			break;
		case Kind::ArrayBindingPattern:
			if (isTupleType(t)) {
				return padTupleType(t, name);
			}
			break;
		default:
			break;
		}
	}
	return t;
}

// padObjectLiteralType — checker.go:17142
Type* Checker::padObjectLiteralType(Type* t, Node* pattern) {
	std::vector<Node*> missingElements;
	for (Node* e : pattern->elements()) {
		if (hasDotDotDotToken(e)) {
			continue;
		}
		std::string name = getPropertyNameFromBindingElement(e);
		if (name != InternalSymbolNameMissing && getPropertyOfType(t, name) == nullptr) {
			missingElements.push_back(e);
		}
	}
	if (missingElements.empty()) {
		return t;
	}
	SymbolTable members;
	for (Symbol* prop : getPropertiesOfObjectType(t)) {
		members[prop->name] = prop;
	}
	for (Node* e : missingElements) {
		Symbol* symbol = newSymbol(SymbolFlagsProperty | SymbolFlagsOptional,
								   getPropertyNameFromBindingElement(e));
		valueSymbolLinks.Get(symbol)->resolvedType =
			getTypeFromBindingElement(e, false /*includePatternInType*/, true /*reportErrors*/);
		members[symbol->name] = symbol;
	}
	Type* result = newAnonymousType(t->symbol, members, {}, {}, getIndexInfosOfType(t));
	result->objectFlags = t->objectFlags;
	return result;
}

// getPropertyNameFromBindingElement — checker.go:17170
std::string Checker::getPropertyNameFromBindingElement(Node* e) {
	Type* exprType = getLiteralTypeFromPropertyName(e->propertyNameOrName());
	if (isTypeUsableAsPropertyName(exprType)) {
		return getPropertyNameFromType(exprType);
	}
	return InternalSymbolNameMissing;
}

// padTupleType — checker.go:17178
Type* Checker::padTupleType(Type* t, Node* pattern) {
	auto patternElements = pattern->elements();
	if ((t->TargetTupleType()->combinedFlags & ElementFlagsVariable) != 0 ||
		getTypeReferenceArity(t) >= static_cast<int>(patternElements.size())) {
		return t;
	}
	std::vector<Type*> elementTypes = getElementTypes(t);
	std::vector<TupleElementInfo> elementInfos = t->TargetTupleType()->elementInfos;
	for (int i = getTypeReferenceArity(t); i < static_cast<int>(patternElements.size()); i++) {
		Node* e = patternElements[i];
		if (i < static_cast<int>(patternElements.size()) - 1 ||
			!(isBindingElement(e) && hasDotDotDotToken(e))) {
			Type* elementType = anyType;
			if (!isOmittedExpression(e) && hasDefaultValue(e)) {
				elementType = getTypeFromBindingElement(e, false /*includePatternInType*/,
														false /*reportErrors*/);
			}
			elementTypes.push_back(elementType);
			elementInfos.push_back(TupleElementInfo{ElementFlagsOptional, nullptr});
			if (!isOmittedExpression(e) && !hasDefaultValue(e)) {
				reportImplicitAny(e, anyType, WideningKind::Normal);
			}
		}
	}
	return createTupleTypeEx(elementTypes, elementInfos, t->TargetTupleType()->readonly);
}

// widenTypeInferredFromInitializer — checker.go:17202
Type* Checker::widenTypeInferredFromInitializer(Node* declaration, Type* t) {
	Type* widened = getWidenedLiteralTypeForInitializer(declaration, t);
	if (isInJSFile(declaration)) {
		if (isEmptyLiteralType(widened)) {
			reportImplicitAny(declaration, anyType, WideningKind::Normal);
			return anyType;
		}
		if (isEmptyArrayLiteralType(widened)) {
			reportImplicitAny(declaration, anyArrayType, WideningKind::Normal);
			return anyArrayType;
		}
	}
	return widened;
}

// getWidenedLiteralTypeForInitializer — checker.go:17217
Type* Checker::getWidenedLiteralTypeForInitializer(Node* declaration, Type* t) {
	if ((getCombinedNodeFlagsCached(declaration) & NodeFlagsConstant) != 0 ||
		isDeclarationReadonly(declaration)) {
		return t;
	}
	return getWidenedLiteralType(t);
}

// getTypeOfFuncClassEnumModule — checker.go:17224
Type* Checker::getTypeOfFuncClassEnumModule(Symbol* symbol) {
	auto* links = valueSymbolLinks.Get(symbol);
	if (links->resolvedType == nullptr) {
		links->resolvedType = getTypeOfFuncClassEnumModuleWorker(symbol);
	}
	return links->resolvedType;
}

// getTypeOfFuncClassEnumModuleWorker — checker.go:17232
Type* Checker::getTypeOfFuncClassEnumModuleWorker(Symbol* symbol) {
	if ((symbol->flags & SymbolFlagsModule) && isShorthandAmbientModuleSymbol(symbol)) {
		return anyType;
	} else if ((symbol->flags & SymbolFlagsValueModule) && symbol->valueDeclaration != nullptr &&
			   isSourceFile(symbol->valueDeclaration) &&
			   symbol->valueDeclaration->as<SourceFile>()->CommonJSModuleIndicator != nullptr) {
		Symbol* resolvedModule = resolveExternalModuleSymbol(symbol, false /*dontResolveAlias*/);
		if (resolvedModule != symbol) {
			return getTypeOfSymbol(resolvedModule);
		}
	}
	Type* t = newObjectType(ObjectFlagsAnonymous, symbol);
	if (symbol->flags & SymbolFlagsClass) {
		Type* baseTypeVariable = getBaseTypeVariableOfClass(symbol);
		if (baseTypeVariable != nullptr) {
			return getIntersectionType({t, baseTypeVariable});
		}
		return t;
	}
	if (strictNullChecks && (symbol->flags & SymbolFlagsOptional)) {
		return getOptionalType(t, /*isProperty*/ true);
	}
	return t;
}

// getBaseTypeVariableOfClass — checker.go:17256
Type* Checker::getBaseTypeVariableOfClass(Symbol* symbol) {
	Type* baseConstructorType =
		getBaseConstructorTypeOfClass(getDeclaredTypeOfClassOrInterface(symbol));
	if (baseConstructorType->flags & TypeFlagsTypeVariable) {
		return baseConstructorType;
	}
	if (baseConstructorType->flags & TypeFlagsIntersection) {
		return findOrNull(baseConstructorType->types(),
						  [](Type* t) { return (t->flags & TypeFlagsTypeVariable) != 0; });
	}
	return nullptr;
}

/**
 * The base constructor of a class can resolve to
 * * undefinedType if the class has no extends clause,
 * * errorType if an error occurred during resolution of the extends expression,
 * * nullType if the extends expression is the null value,
 * * anyType if the extends expression has type any, or
 * * an object type with at least one construct signature.
 */
// getBaseConstructorTypeOfClass — checker.go:17277
Type* Checker::getBaseConstructorTypeOfClass(Type* t) {
	auto* data = t->AsInterfaceType();
	if (data->resolvedBaseConstructorType != nullptr) {
		return data->resolvedBaseConstructorType;
	}
	Node* baseTypeNode = getBaseTypeNodeOfClass(t);
	if (baseTypeNode == nullptr) {
		data->resolvedBaseConstructorType = undefinedType;
		return data->resolvedBaseConstructorType;
	}
	if (!pushTypeResolution(t, TypeSystemPropertyName::ResolvedBaseConstructorType)) {
		return errorType;
	}
	Type* baseConstructorType = checkExpression(baseTypeNode->expression());
	if (baseConstructorType->flags & (TypeFlagsObject | TypeFlagsIntersection)) {
		// Resolving the members of a class requires us to resolve the base class of that class.
		// We force resolution here such that we catch circularities now.
		resolveStructuredTypeMembers(baseConstructorType);
	}
	if (!popTypeResolution()) {
		error(t->symbol->valueDeclaration,
			  X_0_is_referenced_directly_or_indirectly_in_its_own_base_expression,
			  {symbolToString(t->symbol)});
		if (data->resolvedBaseConstructorType == nullptr) {
			data->resolvedBaseConstructorType = errorType;
		}
		return data->resolvedBaseConstructorType;
	}
	if ((baseConstructorType->flags & TypeFlagsAny) == 0 &&
		baseConstructorType != nullWideningType && !isConstructorType(baseConstructorType)) {
		Diagnostic* err =
			error(baseTypeNode->expression(),
				  Type_0_is_not_a_constructor_function_type,
				  {TypeToString(baseConstructorType)});
		if (baseConstructorType->flags & TypeFlagsTypeParameter) {
			Type* constraint = getConstraintFromTypeParameter(baseConstructorType);
			Type* ctorReturn = unknownType;
			if (constraint != nullptr) {
				auto ctorSigs = getSignaturesOfType(constraint, SignatureKind::Construct);
				if (!ctorSigs.empty()) {
					ctorReturn = getReturnTypeOfSignature(ctorSigs[0]);
				}
			}
			if (!baseConstructorType->symbol->declarations.empty()) {
				err->AddRelatedInfo(createDiagnosticForNode(
					baseConstructorType->symbol->declarations[0],
					Did_you_mean_for_0_to_be_constrained_to_type_new_args_Colon_any_1,
					{symbolToString(baseConstructorType->symbol), TypeToString(ctorReturn)}));
			}
		}
		if (data->resolvedBaseConstructorType == nullptr) {
			data->resolvedBaseConstructorType = errorType;
		}
		return data->resolvedBaseConstructorType;
	}
	if (data->resolvedBaseConstructorType == nullptr) {
		data->resolvedBaseConstructorType = baseConstructorType;
	}
	return data->resolvedBaseConstructorType;
}

// signatureHasRestParameter — checker.go:17358
namespace {
bool signatureHasRestParameter(Signature* sig) {
	return (sig->flags & SignatureFlagsHasRestParameter) != 0;
}
} // namespace

// isFunctionType — checker.go:17329
bool Checker::isFunctionType(Type* t) {
	return (t->flags & TypeFlagsObject) != 0 &&
		!getSignaturesOfType(t, SignatureKind::Call).empty();
}

// isConstructorType — checker.go:17333
bool Checker::isConstructorType(Type* t) {
	if (!getSignaturesOfType(t, SignatureKind::Construct).empty()) {
		return true;
	}
	if (t->flags & TypeFlagsTypeVariable) {
		Type* constraint = getBaseConstraintOfType(t);
		return constraint != nullptr && isMixinConstructorType(constraint);
	}
	return false;
}

// A type is a mixin constructor if it has a single construct signature taking no
// type parameters and a single rest parameter of type any[].
// isMixinConstructorType — checker.go:17346
bool Checker::isMixinConstructorType(Type* t) {
	auto signatures = getSignaturesOfType(t, SignatureKind::Construct);
	if (signatures.size() == 1) {
		Signature* s = signatures[0];
		if (s->typeParameters.empty() && s->parameters.size() == 1 &&
			signatureHasRestParameter(s)) {
			Type* paramType = getTypeOfParameter(s->parameters[0]);
			return IsTypeAny(paramType) || getElementTypeOfArrayType(paramType) == anyType;
		}
	}
	return false;
}

// getTypeOfParameter — checker.go:17362
Type* Checker::getTypeOfParameter(Symbol* symbol) {
	Node* declaration = symbol->valueDeclaration;
	return addOptionalityEx(
		getTypeOfSymbol(symbol), false,
		declaration != nullptr &&
			(declaration->initializer() != nullptr || isOptionalDeclaration(declaration)));
}

// getConstraintOfType — checker.go:17367
Type* Checker::getConstraintOfType(Type* t) {
	if (t->flags & TypeFlagsTypeParameter) {
		return getConstraintOfTypeParameter(t);
	}
	if (t->flags & TypeFlagsIndexedAccess) {
		return getConstraintOfIndexedAccess(t);
	}
	if (t->flags & TypeFlagsConditional) {
		return getConstraintOfConditionalType(t);
	}
	return getBaseConstraintOfType(t);
}

// getConstraintOfTypeParameter — checker.go:17379
Type* Checker::getConstraintOfTypeParameter(Type* typeParameter) {
	if (hasNonCircularBaseConstraint(typeParameter)) {
		return getConstraintFromTypeParameter(typeParameter);
	}
	return nullptr;
}

// hasNonCircularBaseConstraint — checker.go:17386
bool Checker::hasNonCircularBaseConstraint(Type* t) {
	return getResolvedBaseConstraint(t, {}) != circularConstraintType;
}

// This is a worker function. Use getConstraintOfTypeParameter which guards against circular
// constraints.
// getConstraintFromTypeParameter — checker.go:17391
Type* Checker::getConstraintFromTypeParameter(Type* t) {
	if ((t->flags & TypeFlagsTypeParameter) == 0) {
		return nullptr;
	}

	auto* tp = t->AsTypeParameter();
	if (tp->constraint == nullptr) {
		Type* constraint;
		if (tp->target != nullptr) {
			constraint = instantiateType(getConstraintOfTypeParameter(tp->target), tp->mapper);
		} else {
			Node* constraintDeclaration = getConstraintDeclaration(t);
			if (constraintDeclaration != nullptr) {
				constraint = getTypeFromTypeNode(constraintDeclaration);
				if ((constraint->flags & TypeFlagsAny) && !isErrorType(constraint)) {
					// use stringNumberSymbolType as the base constraint for mapped type key
					// constraints (unknown isn't assignable to that, but `any` was), use unknown
					// otherwise
					if (isMappedTypeNode(constraintDeclaration->parent->parent)) {
						constraint = stringNumberSymbolType;
					} else {
						constraint = unknownType;
					}
				}
			} else {
				constraint = getInferredTypeParameterConstraint(t, false);
			}
		}
		if (constraint == nullptr) {
			constraint = noConstraintType;
		}
		tp->constraint = constraint;
	}
	if (tp->constraint != noConstraintType) {
		return tp->constraint;
	}
	return nullptr;
}

// getConstraintOrUnknownFromTypeParameter — checker.go:17429
Type* Checker::getConstraintOrUnknownFromTypeParameter(Type* t) {
	Type* result = getConstraintFromTypeParameter(t);
	return result != nullptr ? result : unknownType;
}

// getInferredTypeParameterConstraint — checker.go:17434
Type* Checker::getInferredTypeParameterConstraint(Type* t, bool omitTypeReferences) {
	std::vector<Type*> inferences;
	if (t->symbol != nullptr && !t->symbol->declarations.empty()) {
		for (Node* declaration : t->symbol->declarations) {
			if (isInferTypeNode(declaration->parent)) {
				// When an 'infer T' declaration is immediately contained in a type reference node
				// (such as 'Foo<infer T>'), T's constraint is inferred from the constraint of the
				// corresponding type parameter in 'Foo'. When multiple 'infer T' declarations are
				// present, we form an intersection of the inferred constraint types.
				Node* child = declaration->parent;
				Node* parent = child->parent;
				while (parent != nullptr && isParenthesizedTypeNode(parent)) {
					child = parent;
					parent = child->parent;
				}
				if (isTypeReferenceNode(parent) && !omitTypeReferences) {
					auto typeParameters = getTypeParametersForTypeReferenceOrImport(parent);
					if (!typeParameters.empty()) {
						auto typeArguments = parent->typeArguments();
						int index = indexOf(typeArguments, child);
						if (index >= 0 && index < static_cast<int>(typeParameters.size())) {
							Type* declaredConstraint =
								getConstraintOfTypeParameter(typeParameters[index]);
							if (declaredConstraint != nullptr) {
								// Type parameter constraints can reference other type parameters so
								// constraints need to be instantiated. If instantiation produces the
								// type parameter itself, we discard that inference. For example, in
								//   type Foo<T extends string, U extends T> = [T, U];
								//   type Bar<T> = T extends Foo<infer X, infer X> ? Foo<X, X> : T;
								// the instantiated constraint for U is X, so we discard that
								// inference.
								TypeMapper* mapper = newDeferredTypeMapper(
									typeParameters,
									mapIndex(typeParameters,
											 [this, parent, &typeParameters](Type*, int index)
												 -> std::function<Type*()> {
												 return [this, parent, &typeParameters, index]() {
													 return getEffectiveTypeArgumentAtIndex(
														 parent, typeParameters, index);
												 };
											 }));
								Type* constraint = instantiateType(declaredConstraint, mapper);
								if (constraint != t) {
									inferences.push_back(constraint);
								}
							}
						}
					}
				} else if ((isParameterDeclaration(parent) &&
							parent->as<ParameterDeclaration>()->DotDotDotToken != nullptr) ||
						   isRestTypeNode(parent) ||
						   (isNamedTupleMember(parent) &&
							parent->as<NamedTupleMember>()->DotDotDotToken != nullptr)) {
					inferences.push_back(createArrayType(unknownType));
				} else if (isTemplateLiteralTypeSpan(parent)) {
					inferences.push_back(stringType);
				} else if (isTypeParameterDeclaration(parent) &&
						   isMappedTypeNode(parent->parent)) {
					inferences.push_back(stringNumberSymbolType);
				} else if (isMappedTypeNode(parent) && parent->type() != nullptr &&
						   skipParentheses(parent->type()) == declaration->parent &&
						   isConditionalTypeNode(parent->parent) &&
						   parent->parent->as<ConditionalTypeNode>()->ExtendsType == parent &&
						   isMappedTypeNode(parent->parent->as<ConditionalTypeNode>()->CheckType) &&
						   parent->parent->as<ConditionalTypeNode>()->CheckType->type() != nullptr) {
					Node* checkMappedType = parent->parent->as<ConditionalTypeNode>()->CheckType;
					Type* nodeType = getTypeFromTypeNode(checkMappedType->type());
					Node* checkMappedTypeParameter =
						checkMappedType->as<MappedTypeNode>()->TypeParameter;
					Node* constraintNode =
						checkMappedTypeParameter->as<TypeParameterDeclaration>()->Constraint;
					TypeMapper* mapper = newSimpleTypeMapper(
						getDeclaredTypeOfTypeParameter(
							getSymbolOfDeclaration(checkMappedTypeParameter)),
						constraintNode != nullptr ? getTypeFromTypeNode(constraintNode)
												  : stringNumberSymbolType);
					inferences.push_back(instantiateType(nodeType, mapper));
				}
			}
		}
	}
	if (!inferences.empty()) {
		return getIntersectionType(inferences);
	}
	return nullptr;
}

// getTypeParametersForTypeReferenceOrImport — checker.go:17507
std::vector<Type*> Checker::getTypeParametersForTypeReferenceOrImport(Node* node) {
	Type* t = getTypeFromTypeNode(node);
	if (!isErrorType(t)) {
		Symbol* symbol = getResolvedSymbolOrNil(node);
		if (symbol != nullptr) {
			return getTypeParametersForTypeAndSymbol(t, symbol);
		}
	}
	return {};
}

// getTypeParametersForTypeAndSymbol — checker.go:17518
std::vector<Type*> Checker::getTypeParametersForTypeAndSymbol(Type* t, Symbol* symbol) {
	if (!isErrorType(t)) {
		if (symbol->flags & SymbolFlagsTypeAlias) {
			auto typeParameters = typeAliasLinks.Get(symbol)->typeParameters;
			if (!typeParameters.empty()) {
				return typeParameters;
			}
		}
		if (t->objectFlags & ObjectFlagsReference) {
			return interfaceTypeLocalTypeParameters(t->Target()->AsInterfaceType());
		}
	}
	return {};
}

// getEffectiveTypeArgumentAtIndex — checker.go:17532
Type* Checker::getEffectiveTypeArgumentAtIndex(Node* node,
											 const std::vector<Type*>& typeParameters,
											 size_t index) {
	auto typeArguments = node->typeArguments();
	if (index < typeArguments.size()) {
		return getTypeFromTypeNode(typeArguments[index]);
	}
	return getEffectiveTypeArguments(node, typeParameters)[index];
}

// getConstraintOfIndexedAccess — checker.go:17540
Type* Checker::getConstraintOfIndexedAccess(Type* t) {
	if (hasNonCircularBaseConstraint(t)) {
		return getConstraintFromIndexedAccess(t);
	}
	return nullptr;
}

// getConstraintFromIndexedAccess — checker.go:17547
Type* Checker::getConstraintFromIndexedAccess(Type* t) {
	auto* d = t->AsIndexedAccessType();
	if (isMappedTypeGenericIndexedAccess(t)) {
		// For indexed access types of the form { [P in K]: E }[X], where K is non-generic and X
		// is generic, we substitute an instantiation of E where P is replaced with X.
		return substituteIndexedMappedType(d->objectType, d->indexType);
	}
	Type* indexConstraint = getSimplifiedTypeOrConstraint(d->indexType);
	if (indexConstraint != nullptr && indexConstraint != d->indexType) {
		Type* indexedAccess = getIndexedAccessTypeOrUndefined(d->objectType, indexConstraint,
															  d->accessFlags, nullptr, nullptr);
		if (indexedAccess != nullptr) {
			return indexedAccess;
		}
	}
	Type* objectConstraint = getSimplifiedTypeOrConstraint(d->objectType);
	if (objectConstraint != nullptr && objectConstraint != d->objectType) {
		return getIndexedAccessTypeOrUndefined(objectConstraint, d->indexType, d->accessFlags,
											   nullptr, nullptr);
	}
	return nullptr;
}

// getConstraintOfConditionalType — checker.go:17568
Type* Checker::getConstraintOfConditionalType(Type* t) {
	if (hasNonCircularBaseConstraint(t)) {
		return getConstraintFromConditionalType(t);
	}
	return nullptr;
}

// getConstraintFromConditionalType — checker.go:17575
Type* Checker::getConstraintFromConditionalType(Type* t) {
	Type* constraint = getConstraintOfDistributiveConditionalType(t);
	if (constraint != nullptr) {
		return constraint;
	}
	return getDefaultConstraintOfConditionalType(t);
}

// getDefaultConstraintOfConditionalType — checker.go:17583
Type* Checker::getDefaultConstraintOfConditionalType(Type* t) {
	auto* d = t->AsConditionalType();
	if (d->resolvedDefaultConstraint == nullptr) {
		// An `any` branch of a conditional type would normally be viral - specifically, without
		// special handling here, a conditional type with a single branch of type `any` would be
		// assignable to anything, since it's constraint would simplify to just `any`. This result
		// is _usually_ unwanted - so instead here we elide an `any` branch from the constraint
		// type, in effect treating `any` like `never` rather than `unknown` in this location.
		Type* trueConstraint = getInferredTrueTypeFromConditionalType(t);
		Type* falseConstraint = getFalseTypeFromConditionalType(t);
		if (IsTypeAny(trueConstraint)) {
			d->resolvedDefaultConstraint = falseConstraint;
		} else if (IsTypeAny(falseConstraint)) {
			d->resolvedDefaultConstraint = trueConstraint;
		} else {
			d->resolvedDefaultConstraint = getUnionType({trueConstraint, falseConstraint});
		}
	}
	return d->resolvedDefaultConstraint;
}

// getConstraintOfDistributiveConditionalType — checker.go:17604
Type* Checker::getConstraintOfDistributiveConditionalType(Type* t) {
	auto* d = t->AsConditionalType();
	if (d->resolvedConstraintOfDistributive == nullptr) {
		// Check if we have a conditional type of the form 'T extends U ? X : Y', where T is a
		// constrained type parameter. If so, create an instantiation of the conditional type where
		// T is replaced with its constraint. We do this because if the constraint is a union type
		// it will be distributed over the conditional type and possibly reduced. For example,
		// 'T extends undefined ? never : T' removes 'undefined' from T.
		// We skip returning a distributive constraint for a restrictive instantiation of a
		// conditional type as the constraint for all type params (check type included) have been
		// replace with `unknown`, which is going to produce even more false positive/negative
		// results than the distribute constraint already does.
		// Please note: the distributive constraint is a kludge for emulating what a negated type
		// could to do filter a union - once negated types exist and are applied to the conditional
		// false branch, this "constraint" likely doesn't need to exist.
		auto key = CachedTypeKey{CachedTypeKind::RestrictiveInstantiation, t->id};
		auto it = cachedTypes.find(key);
		if (d->root->isDistributive && (it == cachedTypes.end() || it->second != t)) {
			Type* constraint = getSimplifiedType(d->checkType, false /*writing*/);
			if (constraint == d->checkType) {
				constraint = getConstraintOfType(constraint);
			}
			if (constraint != nullptr && constraint != d->checkType) {
				Type* instantiated = getConditionalTypeInstantiation(
					t, prependTypeMapping(d->root->checkType, constraint, d->mapper),
					true /*forConstraint*/, nullptr);
				if ((instantiated->flags & TypeFlagsNever) == 0) {
					d->resolvedConstraintOfDistributive = instantiated;
					return instantiated;
				}
			}
		}
		d->resolvedConstraintOfDistributive = noConstraintType;
	}
	if (d->resolvedConstraintOfDistributive != noConstraintType) {
		return d->resolvedConstraintOfDistributive;
	}
	return nullptr;
}

// getDeclaredTypeOfClassOrInterface / isThislessInterface — checker.go:17639-17698:
// already ported in checker.cpp.

// ---------------------------------------------------------------------------
// checker.go:18003-18390 — binding elements, destructuring, binding patterns.
// ---------------------------------------------------------------------------

// isNullOrUndefined — checker.go:18003
bool Checker::isNullOrUndefined(Node* node) {
	Node* expr = skipParentheses(node);
	switch (expr->kind) {
	case Kind::NullKeyword:
		return true;
	case Kind::Identifier:
		return getResolvedSymbol(expr) == undefinedSymbol;
	}
	return false;
}

// checkRightHandSideOfForOf is defined in checker_stmtclass.cpp.

// Return the inferred type for a binding element
// getTypeForBindingElement — checker.go:18020
Type* Checker::getTypeForBindingElement(Node* declaration) {
	CheckMode checkMode = hasDotDotDotToken(declaration) ? CheckModeRestBindingElement : CheckModeNormal;
	Type* parentType = getTypeForBindingElementParent(declaration->parent->parent, checkMode);
	if (parentType != nullptr) {
		return getBindingElementTypeFromParentType(declaration, parentType,
												   false /*noTupleBoundsCheck*/);
	}
	return nullptr;
}

// Return the type of a binding element parent. We check SymbolLinks first to see if a type has been
// assigned by contextual typing.
// getTypeForBindingElementParent — checker.go:18031
Type* Checker::getTypeForBindingElementParent(Node* node, CheckMode checkMode) {
	if (checkMode == CheckModeNormal) {
		// We can use a cached resolved type if no optionality was included in that type.
		if (Symbol* symbol = getSymbolOfDeclaration(node); symbol != nullptr) {
			if (Type* resolvedType = valueSymbolLinks.Get(symbol)->resolvedType;
				resolvedType != nullptr && !(strictNullChecks && isOptionalDeclaration(node))) {
				return resolvedType;
			}
		}
	}
	return getTypeForVariableLikeDeclaration(node, false /*includeOptionality*/, checkMode);
}

// getBindingElementTypeFromParentType — checker.go:18043
Type* Checker::getBindingElementTypeFromParentType(Node* declaration, Type* parentType,
												   bool noTupleBoundsCheck) {
	// If an any type was inferred for parent, infer that for the binding element
	if (IsTypeAny(parentType)) {
		return parentType;
	}
	Node* pattern = declaration->parent;
	// Relax null check on ambient destructuring parameters, since the parameters have no
	// implementation and are just documentation
	if (strictNullChecks && (declaration->flags & NodeFlagsAmbient) != 0 &&
		isPartOfParameterDeclaration(declaration)) {
		parentType = GetNonNullableType(parentType);
	} else if (strictNullChecks && pattern->parent->initializer() != nullptr &&
			   !hasTypeFacts(getTypeOfInitializer(pattern->parent->initializer()),
							 TypeFactsEQUndefined)) {
		parentType = getTypeWithFacts(parentType, TypeFactsNEUndefined);
	}
	AccessFlags accessFlags =
		AccessFlagsExpressionPosition |
		(noTupleBoundsCheck || hasDefaultValue(declaration) ? AccessFlagsAllowMissing
														  : AccessFlagsNone);
	Type* t;
	switch (pattern->kind) {
	case Kind::ObjectBindingPattern:
		if (hasDotDotDotToken(declaration)) {
			parentType = getReducedType(parentType);
			if ((parentType->flags & TypeFlagsUnknown) != 0 ||
				!isValidSpreadType(parentType)) {
				error(declaration, Rest_types_may_only_be_created_from_object_types);
				return errorType;
			}
			auto elements = pattern->elements();
			std::vector<Node*> literalMembers;
			literalMembers.reserve(elements.size());
			for (Node* element : elements) {
				if (!hasDotDotDotToken(element)) {
					Node* name = element->propertyNameOrName();
					literalMembers.push_back(name);
				}
			}
			t = getRestType(parentType, literalMembers, declaration->symbol());
		} else {
			// Use explicitly specified property name ({ p: xxx } form), or otherwise the implied
			// name ({ p } form)
			Node* name = declaration->propertyNameOrName();
			Type* indexType = getLiteralTypeFromPropertyName(name);
			Type* declaredType = getIndexedAccessTypeEx(parentType, indexType, accessFlags, name,
													  nullptr);
			t = getFlowTypeOfDestructuring(declaration, declaredType);
		}
		break;
	case Kind::ArrayBindingPattern: {
		// This elementType will be used if the specific property corresponding to this index is not
		// present (aka the tuple element property). This call also checks that the parentType is in
		// fact an iterable or array (depending on target language).
		Type* elementType = checkIteratedTypeOrElementType(
			IterationUseDestructuring | (hasDotDotDotToken(declaration) ? IterationUse{0}
																	  : IterationUsePossiblyOutOfBounds),
			parentType, undefinedType, pattern);
		auto patternElements = pattern->elements();
		int index = indexOf(patternElements, declaration);
		if (hasDotDotDotToken(declaration)) {
			// If the parent is a tuple type, the rest element has a tuple type of the
			// remaining tuple element types. Otherwise, the rest element has an array type with same
			// element type as the parent type.
			Type* baseConstraint = mapType(parentType, [this](Type* t) -> Type* {
				if (t->flags & TypeFlagsInstantiableNonPrimitive) {
					return getBaseConstraintOrType(t);
				}
				return t;
			});
			if (everyType(baseConstraint, isTupleType)) {
				t = mapType(baseConstraint,
							[this, index](Type* t) { return sliceTupleType(t, index, 0); });
			} else {
				t = createArrayType(elementType);
			}
		} else if (isArrayLikeType(parentType)) {
			Type* indexType = getNumberLiteralType(Number(index));
			Type* declaredType = orElse(
				getIndexedAccessTypeOrUndefined(parentType, indexType, accessFlags,
												declaration->name(), nullptr),
				errorType);
			t = getFlowTypeOfDestructuring(declaration, declaredType);
		} else {
			t = elementType;
		}
		break;
	}
	default:
		TSC_UNREACHABLE("Unhandled case in getBindingElementTypeFromParentType");
	}
	if (declaration->initializer() == nullptr) {
		return t;
	}
	if (walkUpBindingElementsAndPatterns(declaration)->type() != nullptr) {
		// In strict null checking mode, if a default value of a non-undefined type is specified,
		// remove undefined from the final type.
		if (strictNullChecks &&
			!hasTypeFacts(checkDeclarationInitializer(declaration, CheckModeNormal, nullptr),
						  TypeFactsIsUndefined)) {
			return getNonUndefinedType(t);
		}
		return t;
	}
	return widenTypeInferredFromInitializer(
		declaration,
		getUnionTypeEx(
			{getNonUndefinedType(t),
			 checkDeclarationInitializer(declaration, CheckModeNormal, nullptr)},
			UnionReductionSubtype, nullptr, nullptr));
}

// getRestType — checker.go:18128
Type* Checker::getRestType(Type* source, const std::vector<Node*>& properties, Symbol* symbol) {
	source = filterType(source, [](Type* t) { return (t->flags & TypeFlagsNullable) == 0; });
	if (source->flags & TypeFlagsNever) {
		return emptyObjectType;
	}
	if (source->flags & TypeFlagsUnion) {
		return mapType(source, [this, &properties, symbol](Type* t) {
			return getRestType(t, properties, symbol);
		});
	}
	Type* omitKeyType = getUnionType(mapVec(properties, [this](Node* p) {
		return getLiteralTypeFromPropertyName(p);
	}));
	std::vector<Symbol*> spreadableProperties;
	std::vector<Type*> unspreadableToRestKeys;
	for (Symbol* prop : getPropertiesOfType(source)) {
		Type* literalTypeFromProperty =
			getLiteralTypeFromProperty(prop, TypeFlagsStringOrNumberLiteralOrUnique, false);
		if (!isTypeAssignableTo(literalTypeFromProperty, omitKeyType) &&
			(getDeclarationModifierFlagsFromSymbol(prop) &
			 (ModifierFlagsPrivate | ModifierFlagsProtected)) == 0 &&
			isSpreadableProperty(prop)) {
			spreadableProperties.push_back(prop);
		} else {
			unspreadableToRestKeys.push_back(literalTypeFromProperty);
		}
	}
	if (isGenericObjectType(source) || isGenericIndexType(omitKeyType)) {
		if (!unspreadableToRestKeys.empty()) {
			// If the type we're spreading from has properties that cannot
			// be spread into the rest type (e.g. getters, methods), ensure
			// they are explicitly omitted, as they would in the non-generic case.
			std::vector<Type*> keys{omitKeyType};
			keys.insert(keys.end(), unspreadableToRestKeys.begin(), unspreadableToRestKeys.end());
			omitKeyType = getUnionType(keys);
		}
		if (omitKeyType->flags & TypeFlagsNever) {
			return source;
		}
		Symbol* omitTypeAlias = getGlobalOmitSymbol();
		if (omitTypeAlias == nullptr) {
			return errorType;
		}
		return getTypeAliasInstantiation(omitTypeAlias,
										 std::vector<Type*>{source, omitKeyType}, nullptr);
	}
	SymbolTable members;
	for (Symbol* prop : spreadableProperties) {
		members[prop->name] = getSpreadSymbol(prop, false /*readonly*/);
	}
	Type* result = newAnonymousType(symbol, members, {}, {}, getIndexInfosOfType(source));
	result->objectFlags |= ObjectFlagsObjectRestType;
	return result;
}

// Determine the control flow type associated with a destructuring declaration or assignment. The
// following forms of destructuring are possible:
//
//	let { x } = obj;  // BindingElement
//	let [ x ] = obj;  // BindingElement
//	{ x } = obj;      // ShorthandPropertyAssignment
//	{ x: v } = obj;   // PropertyAssignment
//	[ x ] = obj;      // Expression
//
// We construct a synthetic element access expression corresponding to 'obj.x' such that the control
// flow analyzer doesn't have to handle all the different syntactic forms.
// getFlowTypeOfDestructuring — checker.go:18185
Type* Checker::getFlowTypeOfDestructuring(Node* node, Type* declaredType) {
	Node* reference = getSyntheticElementAccess(node);
	if (reference != nullptr) {
		return getFlowTypeOfReference(reference, declaredType);
	}
	return declaredType;
}

// getSyntheticElementAccess — checker.go:18193
Node* Checker::getSyntheticElementAccess(Node* node) {
	Node* parentAccess = getParentElementAccess(node);
	if (parentAccess != nullptr && getFlowNodeOfNode(parentAccess) != nullptr) {
		auto [propName, ok] = getDestructuringPropertyName(node);
		if (ok) {
			Node* literal = factory.newStringLiteral(propName, TokenFlagsNone);
			literal->loc = node->loc;
			Node* lhsExpr = parentAccess;
			if (!isLeftHandSideExpression(parentAccess)) {
				lhsExpr = factory.newParenthesizedExpression(parentAccess);
				lhsExpr->loc = node->loc;
			}
			Node* result =
				factory.newElementAccessExpression(lhsExpr, nullptr, literal, NodeFlagsNone);
			result->loc = node->loc;
			literal->parent = result;
			result->parent = node;
			if (lhsExpr != parentAccess) {
				lhsExpr->parent = result;
			}
			*result->flowNodeData().flowNode = getFlowNodeOfNode(parentAccess);
			return result;
		}
	}
	return nullptr;
}

// getParentElementAccess — checker.go:18218
Node* Checker::getParentElementAccess(Node* node) {
	Node* ancestor = node->parent->parent;
	switch (ancestor->kind) {
	case Kind::BindingElement:
	case Kind::PropertyAssignment:
		return getSyntheticElementAccess(ancestor);
	case Kind::ArrayLiteralExpression:
		return getSyntheticElementAccess(node->parent);
	case Kind::VariableDeclaration:
		return ancestor->initializer();
	case Kind::BinaryExpression:
		return ancestor->as<BinaryExpression>()->Right;
	}
	return nullptr;
}

// Return the type implied by a binding pattern. This is the type implied purely by the binding
// pattern itself and without regard to its context (i.e. without regard any type annotation or
// initializer associated with the declaration in which the binding pattern is contained). For
// example, the implied type of [x, y] is [any, any] and the implied type of { x, y: z = 1 } is
// { x: any; y: number; }. The type implied by a binding pattern is used as the contextual type of an
// initializer associated with the binding pattern. Also, for a destructuring parameter with no type
// annotation or initializer, the type implied by the binding pattern becomes the type of the
// parameter.
// getTypeFromBindingPattern — checker.go:18240
Type* Checker::getTypeFromBindingPattern(Node* pattern, bool includePatternInType,
										 bool reportErrors) {
	if (includePatternInType) {
		contextualBindingPatterns.push_back(pattern);
	}
	Type* result;
	if (isObjectBindingPattern(pattern)) {
		result = getTypeFromObjectBindingPattern(pattern, includePatternInType, reportErrors);
	} else {
		result = getTypeFromArrayBindingPattern(pattern, includePatternInType, reportErrors);
	}
	if (includePatternInType) {
		contextualBindingPatterns.pop_back();
	}
	return result;
}

// Return the type implied by an object binding pattern
// getTypeFromObjectBindingPattern — checker.go:18257
Type* Checker::getTypeFromObjectBindingPattern(Node* pattern, bool includePatternInType,
											   bool reportErrors) {
	SymbolTable members;
	IndexInfo* stringIndexInfo = nullptr;
	ObjectFlags objectFlags = ObjectFlagsObjectLiteral | ObjectFlagsContainsObjectOrArrayLiteral;
	for (Node* e : pattern->elements()) {
		Node* name = e->propertyNameOrName();
		if (hasDotDotDotToken(e)) {
			stringIndexInfo = newIndexInfo(stringType, anyType, false /*isReadonly*/, nullptr, {});
			continue;
		}
		Type* exprType = getLiteralTypeFromPropertyName(name);
		if (!isTypeUsableAsPropertyName(exprType)) {
			// do not include computed properties in the implied type
			objectFlags |= ObjectFlagsObjectLiteralPatternWithComputedProperties;
			continue;
		}
		std::string text = getPropertyNameFromType(exprType);
		SymbolFlags flags =
			SymbolFlagsProperty | (e->initializer() != nullptr ? SymbolFlagsOptional : SymbolFlagsNone);
		Symbol* symbol = newSymbol(flags, text);
		valueSymbolLinks.Get(symbol)->resolvedType =
			getTypeFromBindingElement(e, includePatternInType, reportErrors);
		members[symbol->name] = symbol;
	}
	std::vector<IndexInfo*> indexInfos;
	if (stringIndexInfo != nullptr) {
		indexInfos = {stringIndexInfo};
	}
	Type* result = newAnonymousType(nullptr, members, {}, {}, indexInfos);
	result->objectFlags |= objectFlags;
	if (includePatternInType) {
		patternForType[result] = pattern;
		result->objectFlags |= ObjectFlagsContainsObjectOrArrayLiteral;
	}
	return result;
}

// Return the type implied by an array binding pattern
// getTypeFromArrayBindingPattern — checker.go:18293
Type* Checker::getTypeFromArrayBindingPattern(Node* pattern, bool includePatternInType,
											  bool reportErrors) {
	auto elements = pattern->elements();
	Node* lastElement = elements.empty() ? nullptr : elements.back();
	Node* restElement = nullptr;
	if (lastElement != nullptr && isBindingElement(lastElement) && hasDotDotDotToken(lastElement)) {
		restElement = lastElement;
	}
	if (elements.empty() || (elements.size() == 1 && restElement != nullptr)) {
		// TODO: remove ScriptTarget::ES2015
		if (languageVersion >= ScriptTarget::ES2015) {
			return createIterableType(anyType);
		}
		return anyArrayType;
	}
	int minLength =
		findLastIndex(elements, [restElement, this](Node* e) {
			return !(e == restElement || e->name() == nullptr || hasDefaultValue(e));
		}) +
		1;
	std::vector<Type*> elementTypes(elements.size());
	std::vector<TupleElementInfo> elementInfos(elements.size());
	for (size_t i = 0; i < elements.size(); i++) {
		Node* e = elements[i];
		Type* t;
		if (e->name() == nullptr) {
			t = anyType;
		} else {
			t = getTypeFromBindingElement(e, includePatternInType, reportErrors);
		}
		ElementFlags flags;
		if (e == restElement) {
			flags = ElementFlagsRest;
		} else if (static_cast<int>(i) >= minLength) {
			flags = ElementFlagsOptional;
		} else {
			flags = ElementFlagsRequired;
		}
		elementTypes[i] = t;
		elementInfos[i] = TupleElementInfo{flags, nullptr};
	}
	Type* result = createTupleTypeEx(elementTypes, elementInfos, false);
	if (includePatternInType) {
		result = cloneTypeReference(result);
		patternForType[result] = pattern;
		result->objectFlags |= ObjectFlagsContainsObjectOrArrayLiteral;
	}
	return result;
}

// Return the type implied by a binding pattern element. This is the type of the initializer of the
// element if one is present. Otherwise, if the element is itself a binding pattern, it is the type
// implied by the binding pattern. Otherwise, it is the type any.
// getTypeFromBindingElement — checker.go:18342
Type* Checker::getTypeFromBindingElement(Node* element, bool includePatternInType,
										 bool reportErrors) {
	if (element->initializer() != nullptr) {
		// The type implied by a binding pattern is independent of context, so we check the
		// initializer with no contextual type or, if the element itself is a binding pattern, with
		// the type implied by that binding pattern.
		Type* contextualType = unknownType;
		if (isBindingPattern(element->name())) {
			contextualType = getTypeFromBindingPattern(element->name(),
													   true /*includePatternInType*/,
													   false /*reportErrors*/);
		}
		return addOptionality(getWidenedLiteralTypeForInitializer(
			element, checkDeclarationInitializer(element, CheckModeNormal, contextualType)));
	}
	if (isBindingPattern(element->name())) {
		return getTypeFromBindingPattern(element->name(), includePatternInType, reportErrors);
	}
	if (reportErrors && !declarationBelongsToPrivateAmbientMember(element)) {
		reportImplicitAny(element, anyType, WideningKind::Normal);
	}
	// When we're including the pattern in the type (an indication we're obtaining a contextual
	// type), we use a non-inferrable any type. Inference will never directly infer this type, but it
	// is possible to infer a type that contains it, e.g. for a binding pattern like [foo] or
	// { foo }. In such cases, widening of the binding pattern type substitutes a regular any for the
	// non-inferrable any.
	if (includePatternInType) {
		return nonInferrableAnyType;
	}
	return anyType;
}

// declarationBelongsToPrivateAmbientMember — checker.go:18369
bool Checker::declarationBelongsToPrivateAmbientMember(Node* declaration) {
	Node* memberDeclaration = getRootDeclaration(declaration);
	if (isParameterDeclaration(memberDeclaration)) {
		memberDeclaration = memberDeclaration->parent;
	}
	return isPrivateWithinAmbient(memberDeclaration);
}

// getTypeOfPrototypeProperty — checker.go:18377
Type* Checker::getTypeOfPrototypeProperty(Symbol* prototype) {
	// TypeScript 1.0 spec (April 2014): 8.4
	// Every class automatically contains a static property member named 'prototype', the type of
	// which is an instantiation of the class type with type Any supplied as a type argument for
	// each type parameter. It is an error to explicitly declare a static property member with the
	// name 'prototype'.
	Type* classType = getDeclaredTypeOfSymbol(getParentOfSymbol(prototype));
	auto typeParameters = interfaceTypeTypeParameters(classType->AsInterfaceType());
	if (!typeParameters.empty()) {
		return createTypeReference(classType,
								   mapVec(typeParameters, [this](Type*) { return anyType; }));
	}
	return classType;
}

// ---------------------------------------------------------------------------
// checker.go:18399-19097 — this.xxx assignment types, widening, accessor types,
// alias types, optionality/nullability helpers, cached combined flags.
// ---------------------------------------------------------------------------

// thisAssignmentDeclarationKind — checker.go:18390: the enum is declared in the
// "// === slice: decltypes ===" block in checker.h.

// getWidenedTypeForAssignmentDeclaration — checker.go:18399
Type* Checker::getWidenedTypeForAssignmentDeclaration(Symbol* symbol) {
	Type* t = nullptr;
	auto [kind, location] = isConstructorDeclaredThisProperty(symbol);
	switch (kind) {
	case thisAssignmentDeclarationTyped:
		if (location == nullptr) {
			TSC_UNREACHABLE("location should not be nil when this assignment has a type.");
		}
		t = getTypeFromTypeNode(location);
		break;
	case thisAssignmentDeclarationConstructor:
		if (location == nullptr) {
			TSC_UNREACHABLE(
				"constructor should not be nil when this assignment is in a constructor.");
		}
		t = getFlowTypeInConstructor(symbol, location);
		break;
	case thisAssignmentDeclarationMethod:
		t = getTypeOfPropertyInBaseClass(symbol);
		break;
	default:
		break;
	}
	if (t == nullptr) {
		std::vector<Type*> types;
		for (size_t i = 0; i < symbol->declarations.size(); i++) {
			Node* declaration = symbol->declarations[i];
			if (isBinaryExpression(declaration) && declaration->type() != nullptr) {
				t = getTypeFromTypeNode(declaration->type());
				break;
			}
			if (Type* assignedType = getAssignmentDeclarationInitializerType(declaration);
				assignedType != nullptr) {
				// We ignore initial assignments of undefined to CommonJS exports when there are
				// multiple assignment declarations
				if (getAssignmentDeclarationKind(declaration) != JSDeclarationKind::ExportsProperty ||
					i != 0 || symbol->declarations.size() == 1 ||
					(assignedType->flags & TypeFlagsUndefined) == 0) {
					appendIfUnique(types, assignedType);
				}
			}
		}
		if (kind == thisAssignmentDeclarationMethod && !types.empty()) {
			if (strictNullChecks) {
				appendIfUnique(types, undefinedOrMissingType);
			}
		}
		if (t == nullptr) {
			t = anyType;
			if (!types.empty()) {
				t = getUnionType(types);
			}
		}
	}
	t = getWidenedType(t);
	// report an all-nullable or empty union as an implicit any in JS files
	if (symbol->valueDeclaration != nullptr && isInJSFile(symbol->valueDeclaration) &&
		filterType(t, [](Type* t) { return (t->flags & ~TypeFlagsNullable) != 0; }) == neverType) {
		reportImplicitAny(symbol->valueDeclaration, anyType, WideningKind::Normal);
		return anyType;
	}
	return t;
}

// getAssignmentDeclarationInitializerType — checker.go:18452
Type* Checker::getAssignmentDeclarationInitializerType(Node* node) {
	if (isBinaryExpression(node)) {
		Type* t;
		switch (getAssignmentDeclarationKind(node)) {
		case JSDeclarationKind::ModuleExports:
		case JSDeclarationKind::ExportsProperty:
			t = getRegularTypeOfLiteralType(
				checkExpressionCached(getRightMostAssignedExpression(node)));
			break;
		case JSDeclarationKind::ThisProperty:
			if (containsSameNamedThisProperty(node->as<BinaryExpression>()->Left,
											  node->as<BinaryExpression>()->Right)) {
				return nullptr;
			}
			[[fallthrough]];
		default:
			t = checkExpressionForMutableLocation(node->as<BinaryExpression>()->Right,
												  CheckModeNormal);
			break;
		}
		if (isEmptyArrayLiteralType(t) && !hasParentWithTypeAnnotation(node->symbol())) {
			reportImplicitAny(node, anyArrayType, WideningKind::Normal);
			return anyArrayType;
		}
		return t;
	}
	if (isCallExpression(node)) {
		return getTypeFromPropertyDescriptor(node->arguments()[2]);
	}
	return nullptr;
}

// Return true if the parent symbol of the given assignment declaration symbol has declaration with
// a type annotation. For example, returns true for the symbol associated with `f.a` below:
//
//	const f: { (): void, a: string[] } = () => {};
//	f.a = [];
// hasParentWithTypeAnnotation — checker.go:18483
bool Checker::hasParentWithTypeAnnotation(Symbol* symbol) {
	if (symbol->parent != nullptr && symbol->parent->valueDeclaration != nullptr &&
		isFunctionExpressionOrArrowFunction(symbol->parent->valueDeclaration)) {
		if (Symbol* possiblyAnnotatedSymbol =
				getSymbolOfNode(symbol->parent->valueDeclaration->parent);
			possiblyAnnotatedSymbol != nullptr &&
			possiblyAnnotatedSymbol->valueDeclaration != nullptr) {
			return possiblyAnnotatedSymbol->valueDeclaration->type() != nullptr;
		}
	}
	return false;
}

// containsSameNamedThisProperty — checker.go:18492
bool Checker::containsSameNamedThisProperty(Node* thisProperty, Node* expression) {
	std::function<bool(Node*)> visit = [&](Node* node) -> bool {
		if (isMatchingReference(thisProperty, node)) {
			return true;
		}
		if (isFunctionLike(node)) {
			return false;
		}
		return node->forEachChild(visit);
	};
	return visit(expression);
}

// getTypeFromPropertyDescriptor — checker.go:18506
Type* Checker::getTypeFromPropertyDescriptor(Node* node) {
	Type* objectLiteralType = checkExpressionCached(node);
	if (Type* valueType = getTypeOfPropertyOfType(objectLiteralType, "value");
		valueType != nullptr) {
		return valueType;
	}
	if (Type* getFunc = getTypeOfPropertyOfType(objectLiteralType, "get"); getFunc != nullptr) {
		if (Signature* getSig = getSingleCallSignature(getFunc); getSig != nullptr) {
			return getReturnTypeOfSignature(getSig);
		}
	}
	if (Type* setFunc = getTypeOfPropertyOfType(objectLiteralType, "set"); setFunc != nullptr) {
		if (Signature* setSig = getSingleCallSignature(setFunc); setSig != nullptr) {
			return getTypeOfFirstParameterOfSignature(setSig);
		}
	}
	return anyType;
}

// A property is considered a constructor declared property when all declaration sites are this.xxx
// assignments, when no declaration sites have JSDoc type annotations, and when at least one
// declaration site is in the body of a class constructor.
// isConstructorDeclaredThisProperty — checker.go:18527
std::pair<thisAssignmentDeclarationKind, Node*>
Checker::isConstructorDeclaredThisProperty(Symbol* symbol) {
	if (symbol->valueDeclaration == nullptr ||
		!isBinaryExpression(symbol->valueDeclaration)) {
		return {thisAssignmentDeclarationNone, nullptr};
	}
	if (auto it = thisExpandoKinds.find(symbol); it != thisExpandoKinds.end()) {
		auto itl = thisExpandoLocations.find(symbol);
		if (itl == thisExpandoLocations.end()) {
			TSC_UNREACHABLE(
				"location should be cached whenever this expando symbol is cached");
		}
		return {it->second, itl->second};
	}
	bool allThis = true;
	Node* typeAnnotation = nullptr;
	for (Node* declaration : symbol->declarations) {
		if (!isBinaryExpression(declaration)) {
			allThis = false;
			break;
		}
		auto* bin = declaration->as<BinaryExpression>();
		if (getAssignmentDeclarationKind(declaration) == JSDeclarationKind::ThisProperty &&
			(bin->Left->kind != Kind::ElementAccessExpression ||
			 isStringOrNumericLiteralLike(
				 bin->Left->as<ElementAccessExpression>()->ArgumentExpression))) {
			if (bin->type() != nullptr) {
				typeAnnotation = bin->type();
			}
		} else {
			allThis = false;
			break;
		}
	}
	Node* location = nullptr;
	auto kind = thisAssignmentDeclarationNone;
	if (allThis) {
		if (typeAnnotation != nullptr) {
			location = typeAnnotation;
			kind = thisAssignmentDeclarationTyped;
		} else {
			location = getDeclaringConstructor(symbol);
			kind = location == nullptr ? thisAssignmentDeclarationMethod
									   : thisAssignmentDeclarationConstructor;
		}
	}
	thisExpandoKinds[symbol] = kind;
	thisExpandoLocations[symbol] = location;
	return {kind, location};
}

// isGlobalSymbolConstructor — checker.go:18572
bool Checker::isGlobalSymbolConstructor(Node* node) {
	Symbol* symbol = getSymbolOfNode(node);
	Symbol* globalSymbol = getGlobalESSymbolConstructorTypeSymbolOrNil();
	return globalSymbol != nullptr && symbol == globalSymbol;
}

// widenTypeForVariableLikeDeclaration — checker.go:18578
Type* Checker::widenTypeForVariableLikeDeclaration(Type* t, Node* declaration,
												   bool reportErrors) {
	if (t != nullptr) {
		// This special case is required for backwards compatibility with libraries that merge a
		// `symbol` property into `SymbolConstructor`.
		// See https://github.com/microsoft/TypeScript/tsc/issues/1212
		if ((t->flags & TypeFlagsESSymbol) != 0 && isGlobalSymbolConstructor(declaration->parent)) {
			t = getESSymbolLikeTypeForNode(declaration);
		}

		if (reportErrors) {
			reportErrorsFromWidening(declaration, t, WideningKind::Normal);
		}

		// always widen a 'unique symbol' type if the type was created for a different declaration.
		if ((t->flags & TypeFlagsUniqueESSymbol) != 0 &&
			(isBindingElement(declaration) || declaration->type() == nullptr) &&
			t->symbol != getSymbolOfDeclaration(declaration)) {
			t = esSymbolType;
		}
		return getWidenedType(t);
	}
	// Rest parameters default to type any[], other parameters default to type any
	if (isParameterDeclaration(declaration) &&
		declaration->as<ParameterDeclaration>()->DotDotDotToken != nullptr) {
		t = anyArrayType;
	} else {
		t = anyType;
	}
	// Report implicit any errors unless this is a private property within an ambient declaration
	if (reportErrors) {
		if (!declarationBelongsToPrivateAmbientMember(declaration)) {
			reportImplicitAny(declaration, t, WideningKind::Normal);
		}
	}
	return t;
}

// reportImplicitAny — checker.go:18611
void Checker::reportImplicitAny(Node* declaration, Type* t, WideningKind wideningKind) {
	if (isInJSFile(declaration) &&
		!isCheckJSEnabledForFile(getSourceFileOfNode(declaration), compilerOptions)) {
		// Only report implicit any errors/suggestions in TS and ts-check JS files
		return;
	}
	std::string typeAsString = TypeToString(getWidenedType(t));
	const DiagnosticMessage* diagnostic;
	switch (declaration->kind) {
	case Kind::BinaryExpression:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
		diagnostic =
			noImplicitAny
				? Member_0_implicitly_has_an_1_type
				: 					  Member_0_implicitly_has_an_1_type_but_a_better_type_may_be_inferred_from_usage;
		break;
	case Kind::Parameter: {
		auto* param = declaration->as<ParameterDeclaration>();
		if (isIdentifier(param->name)) {
			Node* name = param->name;
			Kind originalKeywordKind = identifierToKeywordKind(name->as<Identifier>());
			if ((isCallSignatureDeclaration(declaration->parent) ||
				 isMethodSignatureDeclaration(declaration->parent) ||
				 isFunctionTypeNode(declaration->parent)) &&
				containsElem(declaration->parent->parameters(), declaration) &&
				(isTypeNodeKind(originalKeywordKind) ||
				 resolveName(declaration, name->text(), SymbolFlagsType,
							 nullptr /*nameNotFoundMessage*/, true /*isUse*/,
							 false /*excludeGlobals*/) != nullptr)) {
				std::string newName =
					"arg" +
					std::to_string(indexOf(declaration->parent->parameters(), declaration));
				std::string typeName =
					declarationNameToString(param->name) +
					(param->DotDotDotToken != nullptr ? "[]" : "");
				errorOrSuggestion(noImplicitAny, declaration,
								  									  Parameter_has_a_name_but_no_type_Did_you_mean_0_Colon_1,
								  {newName, typeName});
				return;
			}
		}
		if (param->DotDotDotToken != nullptr) {
			if (noImplicitAny) {
				diagnostic = Rest_parameter_0_implicitly_has_an_any_type;
			} else {
				diagnostic = 					Rest_parameter_0_implicitly_has_an_any_type_but_a_better_type_may_be_inferred_from_usage;
			}
		} else if (noImplicitAny) {
			diagnostic = Parameter_0_implicitly_has_an_1_type;
		} else {
			diagnostic = 				Parameter_0_implicitly_has_an_1_type_but_a_better_type_may_be_inferred_from_usage;
		}
		break;
	}
	case Kind::BindingElement:
		diagnostic = Binding_element_0_implicitly_has_an_1_type;
		if (!noImplicitAny) {
			// Don't issue a suggestion for binding elements since the codefix doesn't yet support
			// them.
			return;
		}
		break;
	case Kind::FunctionDeclaration:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
		if (noImplicitAny && declaration->name() == nullptr) {
			if (wideningKind == WideningKind::GeneratorYield) {
				error(declaration,
					  						  Generator_implicitly_has_yield_type_0_Consider_supplying_a_return_type_annotation,
					  {typeAsString});
			} else {
				error(declaration,
					  						  Function_expression_which_lacks_return_type_annotation_implicitly_has_an_0_return_type,
					  {typeAsString});
			}
			return;
		}
		if (!noImplicitAny) {
			diagnostic = 				X_0_implicitly_has_an_1_return_type_but_a_better_type_may_be_inferred_from_usage;
		} else if (declaration->flags & NodeFlagsReparsed) {
			std::string name = declarationNameToString(getNameOfDeclaration(declaration));
			if (!name.empty()) {
				error(declaration,
					  						  X_0_which_lacks_return_type_annotation_implicitly_has_an_1_return_type,
					  {name, typeAsString});
			} else {
				error(declaration,
					  						  This_overload_implicitly_returns_the_type_0_because_it_lacks_a_return_type_annotation,
					  {typeAsString});
			}
			return;
		} else if (wideningKind == WideningKind::GeneratorYield) {
			diagnostic = 				X_0_which_lacks_return_type_annotation_implicitly_has_an_1_yield_type;
		} else {
			diagnostic = 				X_0_which_lacks_return_type_annotation_implicitly_has_an_1_return_type;
		}
		break;
	case Kind::MappedType:
		if (noImplicitAny) {
			error(declaration, Mapped_object_type_implicitly_has_an_any_template_type);
		}
		return;
	default:
		if (noImplicitAny) {
			diagnostic = Variable_0_implicitly_has_an_1_type;
		} else {
			diagnostic = 				Variable_0_implicitly_has_an_1_type_but_a_better_type_may_be_inferred_from_usage;
		}
		break;
	}
	errorOrSuggestion(
		noImplicitAny, declaration, diagnostic,
		{declarationNameToString(getNameOfDeclaration(declaration)), typeAsString});
}

// getWidenedType — checker.go:18695
Type* Checker::getWidenedType(Type* t) {
	return getWidenedTypeWithContext(t, nullptr /*context*/);
}

// getWidenedTypeWithContext — checker.go:18699
Type* Checker::getWidenedTypeWithContext(Type* t, WideningContext* context) {
	if (t->objectFlags & ObjectFlagsRequiresWidening) {
		if (context == nullptr) {
			auto key = CachedTypeKey{CachedTypeKind::Widened, t->id};
			if (auto it = cachedTypes.find(key); it != cachedTypes.end() && it->second != nullptr) {
				return it->second;
			}
		}
		Type* result = nullptr;
		if (t->flags & (TypeFlagsAny | TypeFlagsNullable)) {
			result = anyType;
		} else if (isObjectLiteralType(t)) {
			result = getWidenedTypeOfObjectLiteral(t, context);
		} else if (t->flags & TypeFlagsUnion) {
			WideningContext* unionContext = context;
			WideningContext local;
			if (unionContext == nullptr) {
				local.pool = &wideningContextPool;
				local.siblings = t->types();
				unionContext = &local;
			}
			auto widenedTypes = sameMap(t->types(), [&](Type* t) {
				if (t->flags & TypeFlagsNullable) {
					return t;
				}
				return getWidenedTypeWithContext(t, unionContext);
			});
			// Widening an empty object literal transitions from a highly restrictive type to
			// a highly inclusive one. For that reason we perform subtype reduction here if the
			// union includes empty object types (e.g. reducing {} | string to just {}).
			result = getUnionTypeEx(
				widenedTypes,
				someOf(widenedTypes,
					   [this](Type* t) { return isEmptyObjectType(t); })
					? UnionReductionSubtype
					: UnionReductionLiteral,
				nullptr, nullptr);
		} else if (t->flags & TypeFlagsIntersection) {
			result =
				getIntersectionType(sameMap(t->types(), [this](Type* t) { return getWidenedType(t); }));
		} else if (isArrayOrTupleType(t)) {
			result = createTypeReference(
				t->Target(),
				sameMap(getTypeArguments(t), [this](Type* t) { return getWidenedType(t); }));
		}
		if (result != nullptr && context == nullptr) {
			cachedTypes[CachedTypeKey{CachedTypeKind::Widened, t->id}] = result;
		}
		return orElse(result, t);
	}
	return t;
}

// getWidenedTypeOfObjectLiteral — checker.go:18740
Type* Checker::getWidenedTypeOfObjectLiteral(Type* t, WideningContext* context) {
	if (context != nullptr) {
		if (context->widenedTypes.has_value()) {
			if (auto it = context->widenedTypes->find(t);
				it != context->widenedTypes->end() && it->second != nullptr) {
				return it->second;
			}
		}
	}
	SymbolTable members;
	for (Symbol* prop : getPropertiesOfObjectType(t)) {
		members[prop->name] = getWidenedProperty(prop, context);
	}
	if (context != nullptr) {
		for (Symbol* prop : getPropertiesOfContext(context)) {
			if (members.find(prop->name) == members.end()) {
				members[prop->name] = getUndefinedProperty(prop);
			}
		}
	}
	Type* result = newAnonymousType(
		t->symbol, members, {}, {},
		sameMap(getIndexInfosOfType(t),
				[this](IndexInfo* info) {
					return newIndexInfo(info->keyType, getWidenedType(info->valueType),
										info->isReadonly, info->declaration, info->components);
				}));
	// Retain js literal flag through widening
	result->objectFlags |= t->objectFlags & (ObjectFlagsJSLiteral | ObjectFlagsNonInferrableType);
	// Only cache in child contexts since the root context never widens a particular object literal
	// type more than once
	if (context != nullptr && context->parent != nullptr) {
		if (!context->widenedTypes.has_value()) {
			context->widenedTypes.emplace();
		}
		(*context->widenedTypes)[t] = result;
	}
	return result;
}

// getWidenedProperty — checker.go:18772
Symbol* Checker::getWidenedProperty(Symbol* prop, WideningContext* context) {
	if ((prop->flags & SymbolFlagsProperty) == 0) {
		// Since get accessors already widen their return value there is no need to
		// widen accessor based properties here.
		return prop;
	}
	Type* original = getTypeOfSymbol(prop);
	WideningContext* propContext = nullptr;
	if (context != nullptr) {
		propContext = context->getChildContext(prop->name);
	}
	Type* widened = getWidenedTypeWithContext(original, propContext);
	if (widened == original) {
		return prop;
	}
	return createSymbolWithType(prop, widened);
}

// WideningContext::getChildContext — checker.go:18790
WideningContext* WideningContext::getChildContext(const std::string& propertyName) {
	if (childContexts.has_value()) {
		if (auto it = childContexts->find(propertyName);
			it != childContexts->end() && it->second != nullptr) {
			return it->second;
		}
	}
	WideningContext* result;
	if (pool != nullptr) {
		result = &pool->emplace_back();
		result->pool = pool;
	} else {
		result = new WideningContext();
	}
	result->parent = this;
	result->propertyName = propertyName;
	if (!childContexts.has_value()) {
		childContexts.emplace();
	}
	(*childContexts)[propertyName] = result;
	return result;
}

// getPropertiesOfContext — checker.go:18802
std::vector<Symbol*> Checker::getPropertiesOfContext(WideningContext* context) {
	if (!context->resolvedProperties.has_value()) {
		std::vector<Symbol*> names;
		std::unordered_set<std::string> seen;
		for (Type* t : getSiblingsOfContext(context)) {
			if (isObjectLiteralType(t) &&
				(t->objectFlags & ObjectFlagsContainsSpread) == 0) {
				for (Symbol* prop : getPropertiesOfType(t)) {
					if (seen.insert(prop->name).second) {
						names.push_back(prop);
					}
				}
			}
		}
		context->resolvedProperties = std::move(names);
	}
	return *context->resolvedProperties;
}

// getSiblingsOfContext — checker.go:18817
std::vector<Type*> Checker::getSiblingsOfContext(WideningContext* context) {
	if (!context->siblings.has_value()) {
		std::vector<Type*> siblings;
		for (Type* t : getSiblingsOfContext(context->parent)) {
			if (isObjectLiteralType(t)) {
				Symbol* prop = getPropertyOfObjectType(t, context->propertyName);
				if (prop != nullptr) {
					auto distributed = getTypeOfSymbol(prop)->Distributed();
					siblings.insert(siblings.end(), distributed.begin(), distributed.end());
				}
			}
		}
		context->siblings = std::move(siblings);
	}
	return *context->siblings;
}

// getUndefinedProperty — checker.go:18833
Symbol* Checker::getUndefinedProperty(Symbol* prop) {
	if (auto it = undefinedProperties.find(prop->name);
		it != undefinedProperties.end() && it->second != nullptr) {
		return it->second;
	}
	Symbol* result = createSymbolWithType(prop, undefinedOrMissingType);
	result->flags |= SymbolFlagsOptional;
	undefinedProperties[prop->name] = result;
	return result;
}

// getTypeOfEnumMember — checker.go:18843
Type* Checker::getTypeOfEnumMember(Symbol* symbol) {
	auto* links = valueSymbolLinks.Get(symbol);
	if (links->resolvedType == nullptr) {
		links->resolvedType = getDeclaredTypeOfEnumMember(symbol);
	}
	return links->resolvedType;
}

// getTypeOfAccessors — checker.go:18851
Type* Checker::getTypeOfAccessors(Symbol* symbol) {
	auto* links = valueSymbolLinks.Get(symbol);
	if (links->resolvedType == nullptr) {
		if (!pushTypeResolution(symbol, TypeSystemPropertyName::Type)) {
			return errorType;
		}
		Node* getter = getDeclarationOfKind(symbol, Kind::GetAccessor);
		Node* setter = getDeclarationOfKind(symbol, Kind::SetAccessor);
		Node* accessor = findOrNull(symbol->declarations, isAutoAccessorPropertyDeclaration);
		// We try to resolve a getter type annotation, a setter type annotation, or a getter
		// function body return type inference, in that order.
		Type* t = getAnnotatedAccessorType(getter);
		if (t == nullptr) {
			t = getAnnotatedAccessorType(setter);
		}
		if (t == nullptr) {
			t = getAnnotatedAccessorType(accessor);
		}
		if (t == nullptr && getter != nullptr) {
			if (getter->body() != nullptr) {
				t = getReturnTypeFromBody(getter, CheckModeNormal);
			}
		}
		if (t == nullptr && accessor != nullptr) {
			t = getWidenedTypeForVariableLikeDeclaration(accessor, true /*reportErrors*/);
		}
		if (t == nullptr) {
			if (setter != nullptr && !isPrivateWithinAmbient(setter)) {
				errorOrSuggestion(noImplicitAny, setter,
								  Property_0_implicitly_has_type_any_because_its_set_accessor_lacks_a_parameter_type_annotation,
								  {symbolToString(symbol)});
			} else if (getter != nullptr && !isPrivateWithinAmbient(getter)) {
				errorOrSuggestion(noImplicitAny, getter,
								  Property_0_implicitly_has_type_any_because_its_get_accessor_lacks_a_return_type_annotation,
								  {symbolToString(symbol)});
			} else if (accessor != nullptr && !isPrivateWithinAmbient(accessor)) {
				errorOrSuggestion(noImplicitAny, accessor,
								  Member_0_implicitly_has_an_1_type,
								  {symbolToString(symbol), "any"});
			}
			t = anyType;
		}
		if (!popTypeResolution()) {
			if (getAnnotatedAccessorTypeNode(getter) != nullptr) {
				error(getter,
					  						  X_0_is_referenced_directly_or_indirectly_in_its_own_type_annotation,
					  symbolToString(symbol));
			} else if (getAnnotatedAccessorTypeNode(setter) != nullptr) {
				error(setter,
					  						  X_0_is_referenced_directly_or_indirectly_in_its_own_type_annotation,
					  symbolToString(symbol));
			} else if (getAnnotatedAccessorTypeNode(accessor) != nullptr) {
				error(setter,
					  						  X_0_is_referenced_directly_or_indirectly_in_its_own_type_annotation,
					  symbolToString(symbol));
			} else if (getter != nullptr && noImplicitAny) {
				error(getter,
					  						  X_0_implicitly_has_return_type_any_because_it_does_not_have_a_return_type_annotation_and_is_referenced_directly_or_indirectly_in_one_of_its_return_expressions,
					  symbolToString(symbol));
			}
			t = anyType;
		}
		if (links->resolvedType == nullptr) {
			links->resolvedType = t;
		}
	}
	return links->resolvedType;
}

// getWriteTypeOfAccessors — checker.go:18906
Type* Checker::getWriteTypeOfAccessors(Symbol* symbol) {
	auto* links = valueSymbolLinks.Get(symbol);
	if (links->writeType == nullptr) {
		if (!pushTypeResolution(symbol, TypeSystemPropertyName::WriteType)) {
			return errorType;
		}
		Node* setter = getDeclarationOfKind(symbol, Kind::SetAccessor);
		if (setter == nullptr) {
			Node* propDeclaration = getDeclarationOfKind(symbol, Kind::PropertyDeclaration);
			if (propDeclaration != nullptr &&
				isAutoAccessorPropertyDeclaration(propDeclaration)) {
				setter = propDeclaration;
			}
		}
		Type* writeType = getAnnotatedAccessorType(setter);
		if (!popTypeResolution()) {
			if (getAnnotatedAccessorTypeNode(setter) != nullptr) {
				error(setter,
					  						  X_0_is_referenced_directly_or_indirectly_in_its_own_type_annotation,
					  symbolToString(symbol));
			}
			writeType = anyType;
		}
		// Absent an explicit setter type annotation we use the read type of the accessor.
		if (links->writeType == nullptr) {
			if (writeType != nullptr) {
				links->writeType = writeType;
			} else {
				links->writeType = getTypeOfAccessors(symbol);
			}
		}
	}
	return links->writeType;
}

// getTypeOfAlias — checker.go:18938
Type* Checker::getTypeOfAlias(Symbol* symbol) {
	auto* links = valueSymbolLinks.Get(symbol);
	if (links->resolvedType == nullptr) {
		if (!pushTypeResolution(symbol, TypeSystemPropertyName::Type)) {
			return errorType;
		}
		Symbol* targetSymbol = resolveAlias(symbol);
		Symbol* exportSymbol = getTargetOfAliasDeclaration(getDeclarationOfAliasSymbol(symbol));
		// It only makes sense to get the type of a value symbol. If the result of resolving
		// the alias is not a value, then it has no type. To get the type associated with a
		// type symbol, call getDeclaredTypeOfSymbol.
		// This check is important because without it, a call to getTypeOfSymbol could end
		// up recursively calling getTypeOfAlias, causing a stack overflow.
		if (links->resolvedType == nullptr) {
			if ((getSymbolFlags(targetSymbol) & SymbolFlagsValue) != 0) {
				links->resolvedType = getTypeOfSymbol(targetSymbol);
			} else {
				links->resolvedType = errorType;
			}
		}
		if (!popTypeResolution()) {
			reportCircularityError(orElse(exportSymbol, symbol));
			if (links->resolvedType == nullptr) {
				links->resolvedType = errorType;
			}
			return links->resolvedType;
		}
	}
	return links->resolvedType;
}

// addOptionality — checker.go:18969
Type* Checker::addOptionality(Type* t) {
	return addOptionalityEx(t, false /*isProperty*/, true /*isOptional*/);
}

// addOptionalityEx — checker.go:18973
Type* Checker::addOptionalityEx(Type* t, bool isProperty, bool isOptional) {
	if (strictNullChecks && isOptional) {
		return getOptionalType(t, isProperty);
	}
	return t;
}

// getOptionalType — checker.go:18980
Type* Checker::getOptionalType(Type* t, bool isProperty) {
	TSC_ASSERT(strictNullChecks, "strictNullChecks");
	Type* missingOrUndefined = isProperty ? undefinedOrMissingType : undefinedType;
	if (t == missingOrUndefined ||
		((t->flags & TypeFlagsUnion) != 0 && t->types()[0] == missingOrUndefined)) {
		return t;
	}
	return getUnionType({t, missingOrUndefined});
}

// Add undefined or null or both to a type if they are missing.
// getNullableType — checker.go:18990
Type* Checker::getNullableType(Type* t, TypeFlags flags) {
	TypeFlags missing = (flags & ~t->flags) & (TypeFlagsUndefined | TypeFlagsNull);
	if (missing == 0) {
		return t;
	}
	if (missing == TypeFlagsUndefined) {
		return getUnionType({t, undefinedType});
	}
	if (missing == TypeFlagsNull) {
		return getUnionType({t, nullType});
	}
	return getUnionType({t, undefinedType, nullType});
}

// GetNonNullableType — checker.go:19003
Type* Checker::GetNonNullableType(Type* t) {
	if (strictNullChecks) {
		return getAdjustedTypeWithFacts(t, TypeFactsNEUndefinedOrNull);
	}
	return t;
}

// IsNullableType — checker.go:19010
bool Checker::IsNullableType(Type* t) {
	return hasTypeFacts(t, TypeFactsIsUndefinedOrNull);
}

// getNonNullableTypeIfNeeded — checker.go:19014
Type* Checker::getNonNullableTypeIfNeeded(Type* t) {
	if (IsNullableType(t)) {
		return GetNonNullableType(t);
	}
	return t;
}

// getDeclarationNodeFlagsFromSymbol — checker.go:19021,
// getCombinedNodeFlagsCached — checker.go:19028,
// isVarConstLike — checker.go:19038: already ported in checker.cpp.

// getEffectivePropertyNameForPropertyNameNode is defined in checker_grammar.cpp.

// tryGetNameFromType — checker.go:19062
std::pair<std::string, bool> Checker::tryGetNameFromType(Type* t) {
	if (t->flags & TypeFlagsUniqueESSymbol) {
		return {t->AsUniqueESSymbolType()->name, true};
	}
	if (t->flags & TypeFlagsStringLiteral) {
		return {getStringLiteralValue(t), true};
	}
	if (t->flags & TypeFlagsNumberLiteral) {
		return {getNumberLiteralValue(t).string(), true};
	}
	return {"", false};
}

// getLiteralTypeFromPropertyName — checker.go:27235

// getCombinedModifierFlagsCached — checker.go:19077
ModifierFlags Checker::getCombinedModifierFlagsCached(Node* node) {
	// we hold onto the last node and result to speed up repeated lookups against the same node.
	if (lastGetCombinedModifierFlagsNode == node) {
		return lastGetCombinedModifierFlagsResult;
	}
	lastGetCombinedModifierFlagsNode = node;
	lastGetCombinedModifierFlagsResult = getCombinedModifierFlags(node);
	return lastGetCombinedModifierFlagsResult;
}


// ---------------------------------------------------------------------------
// === dep stubs ===
// Callees owned by other slices. Each is declared in the "// === slice:
// decltypes ===" block of checker.h and gets exactly ONE TSC_UNREACHABLE
// definition here; the owning slice replaces the body when it lands.
// ---------------------------------------------------------------------------

namespace {
[[noreturn]] void decltypesDepUnreachable(const char* name) {
	TSC_UNREACHABLE(name);
}
} // namespace

// (deduped: checkExpressionCachedEx defined in cpp/internal/checker/checker_walk.cpp)

// (deduped: checkExpressionEx defined in cpp/internal/checker/checker_walk.cpp)

// (deduped: checkExpressionWithContextualType defined in cpp/internal/checker/checker_walk.cpp)

// (deduped: checkExpressionForMutableLocation defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: checkIteratedTypeOrElementType defined in cpp/internal/checker/checker_contextual.cpp)

// (checkJsxAttribute moved to checker_jsx.cpp — real def there)
// (deduped: checkNonNullExpression defined in cpp/internal/checker/checker_walk.cpp)

// (deduped: checkObjectLiteralMethod defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: checkPropertyAccessExpression defined in cpp/internal/checker/checker_walk.cpp)

// (deduped: checkPropertyAssignment defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: checkShorthandPropertyAssignment defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: getAdjustedTypeWithFacts defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: getConditionalTypeInstantiation defined in cpp/internal/checker/checker_instantiate.cpp)

// (deduped: getContextuallyTypedParameterType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: getDestructuringPropertyName defined in checker_flow.cpp)
// (deduped: getElementTypeOfArrayType defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: getElementTypes defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: getESSymbolLikeTypeForNode defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: getExtractStringType defined in cpp/internal/checker/checker_stmtclass.cpp)

// (deduped: getFalseTypeFromConditionalType defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: getFlowTypeInConstructor defined in checker_flow.cpp)
// (deduped: getFlowTypeInStaticBlocks defined in checker_flow.cpp)
// (deduped: getFlowTypeOfReference defined in checker_flow.cpp)
// (deduped: getInferredTrueTypeFromConditionalType defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: getNonUndefinedType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: getPropertiesOfObjectType defined in cpp/internal/checker/checker_members.cpp)

// (deduped: getPropertyOfObjectType defined in cpp/internal/checker/checker_members.cpp)

// (deduped: getQuickTypeOfExpression defined in cpp/internal/checker/checker_walk.cpp)

// (deduped: getResolvedSymbolOrNil defined in cpp/internal/checker/checker_expressions_c.cpp)

// (deduped: getReturnTypeFromBody defined in cpp/internal/checker/checker_signatures.cpp)

// (deduped: getReturnTypeOfSignature defined in cpp/internal/checker/checker_signatures.cpp)

// (deduped: getSingleCallSignature defined in cpp/internal/checker/checker_members.cpp)

// (deduped: getSpreadSymbol defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: getTypeArguments defined in cpp/internal/checker/checker_instantiate.cpp)

// (deduped: getTypeOfExpression defined in cpp/internal/checker/checker_walk.cpp)

// (deduped: getTypeOfFirstParameterOfSignature defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: getTypeOfInitializer defined in checker_flow.cpp)
// (deduped: getTypeOfMappedSymbol defined in cpp/internal/checker/checker_members.cpp)

// (deduped: getTypeOfPropertyInBaseClass defined in the owning slice file)
// (deduped: getTypeOfReverseMappedSymbol — inference slice, defined in
// cpp/internal/checker/checker_inference.cpp)

// (deduped: getTypeOfReverseMappedSymbol defined in the owning slice file)
// (deduped: getTypeReferenceArity defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: getTypeWithFacts defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: hasBindableName defined in cpp/internal/checker/checker_signatures.cpp)

// (deduped: hasDefaultValue defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: isArrayLikeType defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: isArrayOrTupleType defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: isContextSensitiveFunctionOrObjectLiteralMethod defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: isEmptyArrayLiteralType defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: isEmptyLiteralType defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: isGenericObjectType defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: isMappedTypeGenericIndexedAccess defined in cpp/internal/checker/checker_members.cpp)

// (deduped: isMatchingReference defined in checker_flow.cpp)
// (deduped: isSpreadableProperty defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: sliceTupleType defined in cpp/internal/checker/checker_relater.cpp)
// (deduped: isValidSpreadType defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: isSpreadableProperty defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: removeMissingType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: removeOptionalTypeMarker defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: reportErrorsFromWidening defined in cpp/internal/checker/checker_signatures.cpp)

// (deduped: substituteIndexedMappedType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: tryGetTypeFromTypeNode defined in cpp/internal/checker/checker_typenodes.cpp)

// (deduped: TypeToString stub kept in checker_tracer.cpp)

} // namespace checker
} // namespace tsc
