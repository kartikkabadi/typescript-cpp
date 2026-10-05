// Port of tsc/internal/checker/checker.go — instantiate slice.
// Covers checker.go:22285-23219 (getTypeArguments … instantiateList).
// Dep-stubbed cross-slice calls live at the bottom under the dep-stub banner.
#include "internal/checker/checker.h"

#include <algorithm>
#include <cstring>

#include "internal/checker/mapper.h"

namespace tsc {
namespace checker {

// ---------------------------------------------------------------------------
// Free helpers defined in checker.cpp (extern linkage, no header home).
// ---------------------------------------------------------------------------

bool everyType(Type* t, const std::function<bool(Type*)>& f);
bool maybeTypeOfKind(Type* t, TypeFlags flags);

// ---------------------------------------------------------------------------
// Free functions owned by other slices (dep-stubbed in checker_members.cpp).
// ---------------------------------------------------------------------------

bool isTupleType(Type* t);
MappedTypeModifiers getMappedTypeModifiers(Type* t);

// ---------------------------------------------------------------------------
// Dep-stub declarations for free functions owned by other slices.
// Bodies live at the bottom of this file under the dep-stub banner.
// ---------------------------------------------------------------------------

CacheKey getTypeInstantiationKey(const std::vector<Type*>& typeArguments,
	TypeAlias* alias, bool singleSignature);
CacheKey getConditionalTypeKey(const std::vector<Type*>& typeArguments,
	TypeAlias* alias, bool forConstraint);

namespace {

// ---------------------------------------------------------------------------
// keyBuilder — replica of the file-local helper in checker.cpp/checker_members.cpp.
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

// ---------------------------------------------------------------------------
// Type.Target() / Type.Mapper() / Type.Types() — types.go accessors, not yet
// methods in C++ (same replicas as checker_members.cpp).
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

const std::vector<Type*>& typeTypes(Type* t) {
	if (t->flags & TypeFlagsUnionOrIntersection) {
		return t->AsUnionOrIntersectionType()->types;
	}
	if (t->flags & TypeFlagsTemplateLiteral) {
		return t->AsTemplateLiteralType()->types;
	}
	TSC_UNREACHABLE("Unhandled case in Type.Types");
}

// TypeAlias accessors are nil-safe in Go (methods on a nilable pointer).
Symbol* aliasSymbol(TypeAlias* alias) {
	return alias != nullptr ? alias->symbol : nullptr;
}

const std::vector<Type*>& aliasTypeArguments(TypeAlias* alias) {
	static const std::vector<Type*> empty;
	return alias != nullptr ? alias->typeArguments : empty;
}

// ---------------------------------------------------------------------------
// core.Some / Every / Map / Filter replicas.
// ---------------------------------------------------------------------------

template <typename T, typename F>
bool anyOf(const std::vector<T>& v, F&& f) {
	return std::any_of(v.begin(), v.end(), std::forward<F>(f));
}

template <typename T, typename F>
bool allOf(const std::vector<T>& v, F&& f) {
	return std::all_of(v.begin(), v.end(), std::forward<F>(f));
}

template <typename T, typename F>
auto mapVec(const std::vector<T>& v, F&& f) -> std::vector<decltype(f(v.front()))> {
	std::vector<decltype(f(v.front()))> out;
	out.reserve(v.size());
	for (const T& e : v) {
		out.push_back(f(e));
	}
	return out;
}

template <typename T, typename F>
std::vector<T> filterVec(const std::vector<T>& v, F&& f) {
	std::vector<T> out;
	for (const T& e : v) {
		if (f(e)) {
			out.push_back(e);
		}
	}
	return out;
}

// getModifiedReadonlyState — checker.go:23082-23090 (free function in range).
bool getModifiedReadonlyState(bool state, MappedTypeModifiers modifiers) {
	if (modifiers & MappedTypeModifiersIncludeReadonly) {
		return true;
	}
	if (modifiers & MappedTypeModifiersExcludeReadonly) {
		return false;
	}
	return state;
}

}  // namespace

// ---------------------------------------------------------------------------
// getTypeArguments — checker.go:22285-22323
// ---------------------------------------------------------------------------

std::vector<Type*> Checker::getTypeArguments(Type* t) {
	TypeReference* d = t->AsTypeReference();
	// std::vector has no nil state; an empty resolvedTypeArguments is treated as
	// uncomputed. Recomputation of the empty case is side-effect-free and yields
	// the same result (Go only ever stores a non-nil empty via the zero-parameter
	// slices.Repeat path, which returns here without diagnostics).
	if (d->resolvedTypeArguments.empty()) {
		InterfaceType* n = d->target->AsInterfaceType();
		if (!pushTypeResolution(t, TypeSystemPropertyName::ResolvedTypeArguments)) {
			return std::vector<Type*>(interfaceTypeTypeParameters(n).size(), errorType);
		}
		std::vector<Type*> typeArguments;
		Node* node = t->AsTypeReference()->node;
		if (node != nullptr) {
			switch (node->kind) {
				case Kind::TypeReference: {
					typeArguments = interfaceTypeOuterTypeParameters(n);
					std::vector<Type*> effective =
						getEffectiveTypeArguments(node, interfaceTypeLocalTypeParameters(n));
					typeArguments.insert(typeArguments.end(), effective.begin(), effective.end());
					break;
				}
				case Kind::ArrayType:
					typeArguments = {getTypeFromTypeNode(node->as<ArrayTypeNode>()->ElementType)};
					break;
				case Kind::TupleType:
					typeArguments =
						mapVec(node->elements(), [this](Node* e) { return getTypeFromTypeNode(e); });
					break;
				default:
					TSC_UNREACHABLE("Unhandled case in getTypeArguments");
			}
		}
		if (popTypeResolution()) {
			if (d->resolvedTypeArguments.empty()) {
				d->resolvedTypeArguments = instantiateTypes(typeArguments, d->mapper);
			}
		} else {
			if (d->resolvedTypeArguments.empty()) {
				d->resolvedTypeArguments =
					std::vector<Type*>(interfaceTypeTypeParameters(n).size(), errorType);
			}
			Node* errorNode = node != nullptr ? node : currentNode;
			if (d->target->symbol != nullptr) {
				error(errorNode, Type_arguments_for_0_circularly_reference_themselves,
					  symbolToString(d->target->symbol));
			} else {
				error(errorNode, Tuple_type_arguments_circularly_reference_themselves);
			}
		}
	}
	return d->resolvedTypeArguments;
}

// ---------------------------------------------------------------------------
// getEffectiveTypeArguments — checker.go:22325-22328
// ---------------------------------------------------------------------------

std::vector<Type*> Checker::getEffectiveTypeArguments(
	Node* node, const std::vector<Type*>& typeParameters) {
	return fillMissingTypeArguments(
		mapVec(node->typeArguments(), [this](Node* a) { return getTypeFromTypeNode(a); }),
		typeParameters, getMinTypeArgumentCount(typeParameters), isInJSFile(node));
}

// Gets the minimum number of type arguments needed to satisfy all non-optional type parameters.
// getMinTypeArgumentCount — checker.go:22330-22339

int Checker::getMinTypeArgumentCount(const std::vector<Type*>& typeParameters) {
	int minTypeArgumentCount = 0;
	for (size_t i = 0; i < typeParameters.size(); i++) {
		if (!hasTypeParameterDefault(typeParameters[i])) {
			minTypeArgumentCount = static_cast<int>(i) + 1;
		}
	}
	return minTypeArgumentCount;
}

// hasTypeParameterDefault — checker.go:22341-22345

bool Checker::hasTypeParameterDefault(Type* t) {
	return t->symbol != nullptr && anyOf(t->symbol->declarations, [](Node* d) {
		return isTypeParameterDeclaration(d) &&
			d->as<TypeParameterDeclaration>()->DefaultType != nullptr;
	});
}

// fillMissingTypeArguments — checker.go:22346-22383

std::vector<Type*> Checker::fillMissingTypeArguments(std::vector<Type*> typeArguments,
	const std::vector<Type*>& typeParameters, int minTypeArgumentCount,
	bool isJavaScriptImplicitAny) {
	size_t numTypeParameters = typeParameters.size();
	if (numTypeParameters == 0) {
		return {};
	}
	size_t numTypeArguments = typeArguments.size();
	if (isJavaScriptImplicitAny || numTypeArguments < numTypeParameters) {
		std::vector<Type*> result(numTypeParameters);
		std::copy(typeArguments.begin(), typeArguments.end(), result.begin());
		// Map invalid forward references in default types to the error type
		for (size_t i = numTypeArguments; i < numTypeParameters; i++) {
			result[i] = errorType;
		}
		Type* baseDefaultType = getDefaultTypeArgumentType(isJavaScriptImplicitAny);
		for (size_t i = numTypeArguments; i < numTypeParameters; i++) {
			Type* defaultType = getDefaultFromTypeParameter(typeParameters[i]);
			if (isJavaScriptImplicitAny && defaultType != nullptr &&
				(isTypeIdenticalTo(defaultType, unknownType) ||
				 isTypeIdenticalTo(defaultType, emptyObjectType))) {
				defaultType = anyType;
			}
			if (defaultType != nullptr) {
				result[i] = instantiateType(defaultType, newTypeMapper(typeParameters, result));
			} else {
				result[i] = baseDefaultType;
			}
		}
		return result;
	}
	return typeArguments;
}

// getDefaultTypeArgumentType — checker.go:22384-22389

Type* Checker::getDefaultTypeArgumentType(bool isInJavaScriptFile) {
	if (isInJavaScriptFile) {
		return anyType;
	}
	return unknownType;
}

// Gets the default type for a type parameter. If the type parameter is the result of an instantiation,
// this gets the instantiated default type of its target. If the type parameter has no default type or
// the default is circular, `undefined` is returned.
// getDefaultFromTypeParameter — checker.go:22390-22402

Type* Checker::getDefaultFromTypeParameter(Type* t) {
	if ((t->flags & TypeFlagsTypeParameter) == 0) {
		return nullptr;
	}
	Type* defaultType = getResolvedTypeParameterDefault(t);
	if (defaultType != noConstraintType && defaultType != circularConstraintType) {
		return defaultType;
	}
	return nullptr;
}

// getResolvedTypeParameterDefault — checker.go:22403-22437

Type* Checker::getResolvedTypeParameterDefault(Type* t) {
	TypeParameter* d = t->AsTypeParameter();
	if (d->resolvedDefaultType == nullptr) {
		if (d->target != nullptr) {
			Type* targetDefault = getResolvedTypeParameterDefault(d->target);
			if (targetDefault != nullptr) {
				d->resolvedDefaultType = instantiateType(targetDefault, d->mapper);
			} else {
				d->resolvedDefaultType = noConstraintType;
			}
		} else {
			// To block recursion, set the initial value to the resolvingDefaultType.
			d->resolvedDefaultType = resolvingDefaultType;
			Type* defaultType = noConstraintType;
			if (t->symbol != nullptr) {
				Node* defaultDeclaration = nullptr;
				for (Node* decl : t->symbol->declarations) {
					if (isTypeParameterDeclaration(decl) &&
						decl->as<TypeParameterDeclaration>()->DefaultType != nullptr) {
						defaultDeclaration = decl->as<TypeParameterDeclaration>()->DefaultType;
						break;
					}
				}
				if (defaultDeclaration != nullptr) {
					defaultType = getTypeFromTypeNode(defaultDeclaration);
				}
			}
			if (d->resolvedDefaultType == resolvingDefaultType) {
				// If we have not been called recursively, set the correct default type.
				d->resolvedDefaultType = defaultType;
			}
		}
	} else if (d->resolvedDefaultType == resolvingDefaultType) {
		// If we are called recursively for this type parameter, mark the default as circular.
		d->resolvedDefaultType = circularConstraintType;
	}
	return d->resolvedDefaultType;
}

// getDefaultOrUnknownFromTypeParameter — checker.go:22438-22441

Type* Checker::getDefaultOrUnknownFromTypeParameter(Type* t) {
	Type* result = getDefaultFromTypeParameter(t);
	return result != nullptr ? result : unknownType;
}

// getNamedMembers — checker.go:22443-22470

// isDeclarationContainedBy — checker.go:22471-22478

// isNamedMember — checker.go:22479-22482

// symbolIsValue — checker.go:22483-22486

// symbolIsValueEx — checker.go:22487-22491

// instantiateType — checker.go:22492-22494

// instantiateTypeWithAlias — checker.go:22496-22548

Type* Checker::instantiateTypeWithAlias(Type* t, TypeMapper* m, TypeAlias* alias) {
	// Check for type variables in the alias, so things like `type Brand<T> = number & {}` can potentially be copied with new alias type args, despite them being unreferenced.
	// This is the behavior most people using aliases expect, and prevents the cache from leaking type parameters outside their scope of validity.
	// tests/cases/compiler/declarationEmitArrowFunctionNoRenaming.ts contains an example of this, which previously only worked in strada via some input node reuse logic instead.
	if (t == nullptr || m == nullptr ||
		!(couldContainTypeVariables(t) ||
		  (t->alias != nullptr && !t->alias->typeArguments.empty() &&
		   anyOf(t->alias->typeArguments, couldContainTypeVariables)))) {
		return t;
	}
	if (instantiationStack.size() == 100 || instantiationCount >= 5'000'000) {
		// We have reached 100 recursive type instantiations, or 5M type instantiations caused by the same statement
		// or expression. There is a very high likelihood we're dealing with a combination of infinite generic types
		// that perpetually generate new type identities, so we stop the recursion here by yielding the error type.
		// TRACING: tr.Instant(tracing.PhaseCheckTypes, "instantiateType_DepthLimit",
		//   {"typeId": t->id, "instantiationDepth": instantiationStack.size(), "instantiationCount": instantiationCount})
		std::vector<std::string> circularTypeNames = getCircularTypeNames();
		if (circularTypeNames.size() == 1) {
			error(currentNode, Instantiations_of_type_0_appear_infinitely_circular,
				  circularTypeNames[0]);
		} else if (circularTypeNames.size() > 1) {
			error(currentNode,
				  Instantiations_of_the_following_types_appear_infinitely_circular_Colon_0,
				  quotedAndCommaSeparated(circularTypeNames));
		} else {
			error(currentNode, Type_instantiation_is_excessively_deep_and_possibly_infinite);
		}
		return errorType;
	}
	int index = findActiveMapper(m);
	if (index == -1) {
		pushActiveMapper(m);
	}
	keyBuilder b;
	b.writeType(t);
	b.writeAlias(alias);
	CacheKey key = b.hash();
	auto* cache =
		activeTypeMappersCaches[index != -1 ? static_cast<size_t>(index)
											: activeTypeMappersCaches.size() - 1];
	if (auto it = cache->find(key); it != cache->end()) {
		return it->second;
	}
	TotalInstantiationCount++;
	instantiationCount++;
	instantiationStack.push_back(t);
	Type* result = instantiateTypeWorker(t, m, alias);
	if (index == -1) {
		popActiveMapper();
	} else {
		(*cache)[key] = result;
	}
	instantiationStack[instantiationStack.size() - 1] = nullptr;
	instantiationStack.pop_back();
	return result;
}

// getCircularTypeNames — checker.go:22549-22566

std::vector<std::string> Checker::getCircularTypeNames() {
	std::unordered_map<Type*, int> typeCounts;
	std::vector<std::string> circularTypeNames;
	for (Type* t : instantiationStack) {
		typeCounts[t] = typeCounts[t] + 1;
		if (typeCounts[t] == 3) {
			Symbol* symbol = t->symbol;
			if (t->alias != nullptr) {
				symbol = t->alias->symbol;
			}
			if (symbol != nullptr && !symbol->name.empty() && symbol->name[0] != '\xFE') {
				std::string name = symbolToString(symbol);
				if (std::find(circularTypeNames.begin(), circularTypeNames.end(), name) ==
					circularTypeNames.end()) {
					circularTypeNames.push_back(name);
				}
			}
		}
	}
	return circularTypeNames;
}

// pushActiveMapper — checker.go:22567-22580

void Checker::pushActiveMapper(TypeMapper* mapper) {
	activeMappers.push_back(mapper);

	// Go reuses a cleared map still held in the backing array's capacity; here
	// popped maps are parked in freeTypeMappersCaches for the same effect.
	if (!freeTypeMappersCaches.empty()) {
		activeTypeMappersCaches.push_back(freeTypeMappersCaches.back());
		freeTypeMappersCaches.pop_back();
	} else {
		activeTypeMappersCaches.push_back(new CacheMap<Type*>());
	}
}

// popActiveMapper — checker.go:22581-22590

void Checker::popActiveMapper() {
	activeMappers[activeMappers.size() - 1] = nullptr;
	activeMappers.pop_back();

	// Clear the map, but keep it available for later reuse.
	CacheMap<Type*>* last = activeTypeMappersCaches.back();
	last->clear();
	activeTypeMappersCaches.pop_back();
	freeTypeMappersCaches.push_back(last);
}

// findActiveMapper — checker.go:22591-22593

int Checker::findActiveMapper(TypeMapper* mapper) {
	for (size_t i = activeMappers.size(); i-- > 0;) {
		if (activeMappers[i] == mapper) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

// clearActiveMapperCaches — checker.go:22595-22599

void Checker::clearActiveMapperCaches() {
	for (auto* cache : activeTypeMappersCaches) {
		cache->clear();
	}
}

// Return true if the given type could possibly reference a type parameter for which
// we perform type inference (i.e. a type parameter of a generic function). We cache
// results for union and intersection types for performance reasons.
// couldContainTypeVariablesWorker — checker.go:22605-22626

bool Checker::couldContainTypeVariablesWorker(Type* t) {
	if ((t->flags & TypeFlagsStructuredOrInstantiable) == 0) {
		return false;
	}
	ObjectFlags objectFlags = t->objectFlags;
	if ((objectFlags & ObjectFlagsCouldContainTypeVariablesComputed) != 0) {
		return (objectFlags & ObjectFlagsCouldContainTypeVariables) != 0;
	}
	bool result = (t->flags & TypeFlagsInstantiable) != 0 ||
		((t->flags & TypeFlagsObject) != 0 && !isNonGenericTopLevelType(t) &&
		 (((objectFlags & ObjectFlagsReference) != 0 &&
		   (t->AsTypeReference()->node != nullptr ||
			anyOf(getTypeArguments(t), couldContainTypeVariables))) ||
		  ((objectFlags & ObjectFlagsAnonymous) != 0 && t->symbol != nullptr &&
		   (t->symbol->flags &
			(SymbolFlagsFunction | SymbolFlagsMethod | SymbolFlagsClass |
			 SymbolFlagsTypeLiteral | SymbolFlagsObjectLiteral)) != 0 &&
		   !t->symbol->declarations.empty()) ||
		  (objectFlags & (ObjectFlagsMapped | ObjectFlagsReverseMapped |
						  ObjectFlagsObjectRestType |
						  ObjectFlagsInstantiationExpressionType)) != 0)) ||
		((t->flags & TypeFlagsUnionOrIntersection) != 0 &&
		 (t->flags & TypeFlagsEnumLiteral) == 0 && !isNonGenericTopLevelType(t) &&
		 anyOf(typeTypes(t), couldContainTypeVariables));
	t->objectFlags |= ObjectFlagsCouldContainTypeVariablesComputed |
		(result ? ObjectFlagsCouldContainTypeVariables : 0);
	return result;
}

// isNonGenericTopLevelType — checker.go:22628-22646

bool Checker::isNonGenericTopLevelType(Type* t) {
	if (t->alias != nullptr && t->alias->typeArguments.empty()) {
		Node* declaration = getDeclarationOfKind(t->alias->symbol, Kind::TypeAliasDeclaration);
		if (declaration == nullptr) {
			declaration = getDeclarationOfKind(t->alias->symbol, Kind::JSTypeAliasDeclaration);
		}
		return declaration != nullptr &&
			findAncestorOrQuit(declaration->parent, [](Node* n) -> FindAncestorResult {
				switch (n->kind) {
					case Kind::SourceFile:
						return FindAncestorResult::True;
					case Kind::ModuleDeclaration:
						return FindAncestorResult::False;
				}
				return FindAncestorResult::Quit;
			}) != nullptr;
	}
	return false;
}

// instantiateTypeWorker — checker.go:22648-22711

Type* Checker::instantiateTypeWorker(Type* t, TypeMapper* m, TypeAlias* alias) {
	TypeFlags flags = t->flags;
	if ((flags & TypeFlagsTypeParameter) != 0) {
		return getMappedType(t, m);
	}
	if ((flags & TypeFlagsObject) != 0) {
		ObjectFlags objectFlags = t->objectFlags;
		if ((objectFlags &
			 (ObjectFlagsReference | ObjectFlagsAnonymous | ObjectFlagsMapped)) != 0) {
			if ((objectFlags & ObjectFlagsReference) != 0 &&
				t->AsTypeReference()->node == nullptr) {
				const std::vector<Type*>& resolvedTypeArguments =
					t->AsTypeReference()->resolvedTypeArguments;
				std::vector<Type*> newTypeArguments =
					instantiateTypes(resolvedTypeArguments, m);
				if (newTypeArguments == resolvedTypeArguments) {
					return t;
				}
				return createNormalizedTypeReference(typeTarget(t), newTypeArguments);
			}
			if ((objectFlags & ObjectFlagsReverseMapped) != 0) {
				return instantiateReverseMappedType(t, m);
			}
			return getObjectTypeInstantiation(t, m, alias);
		}
		return t;
	}
	if ((flags & TypeFlagsUnionOrIntersection) != 0) {
		Type* source = t;
		if ((t->flags & TypeFlagsUnion) != 0) {
			Type* origin = t->AsUnionType()->origin;
			if (origin != nullptr && (origin->flags & TypeFlagsUnionOrIntersection) != 0) {
				source = origin;
			}
		}
		const std::vector<Type*>& types = typeTypes(source);
		std::vector<Type*> newTypes = instantiateTypes(types, m);
		if (newTypes == types && aliasSymbol(alias) == aliasSymbol(t->alias)) {
			return t;
		}
		if (alias == nullptr) {
			alias = instantiateTypeAlias(t->alias, m);
		}
		if ((source->flags & TypeFlagsIntersection) != 0) {
			return getIntersectionTypeEx(newTypes, IntersectionFlagsNone, alias);
		}
		return getUnionTypeEx(newTypes, UnionReductionLiteral, alias, nullptr /*origin*/);
	}
	if ((flags & TypeFlagsIndex) != 0) {
		return getIndexType(instantiateType(typeTarget(t), m));
	}
	if ((flags & TypeFlagsIndexedAccess) != 0) {
		if (alias == nullptr) {
			alias = instantiateTypeAlias(t->alias, m);
		}
		IndexedAccessType* d = t->AsIndexedAccessType();
		return getIndexedAccessTypeEx(instantiateType(d->objectType, m),
									  instantiateType(d->indexType, m), d->accessFlags,
									  nullptr /*accessNode*/, alias);
	}
	if ((flags & TypeFlagsTemplateLiteral) != 0) {
		return getTemplateLiteralType(t->AsTemplateLiteralType()->texts,
									  instantiateTypes(t->AsTemplateLiteralType()->types, m));
	}
	if ((flags & TypeFlagsStringMapping) != 0) {
		return getStringMappingType(t->symbol,
									instantiateType(t->AsStringMappingType()->target, m));
	}
	if ((flags & TypeFlagsConditional) != 0) {
		return getConditionalTypeInstantiation(
			t, combineTypeMappers(t->AsConditionalType()->mapper, m),
			false /*forConstraint*/, alias);
	}
	if ((flags & TypeFlagsSubstitution) != 0) {
		Type* newBaseType = instantiateType(t->AsSubstitutionType()->baseType, m);
		if (isNoInferType(t)) {
			return getNoInferType(newBaseType);
		}
		Type* newConstraint = instantiateType(t->AsSubstitutionType()->constraint, m);
		// A substitution type originates in the true branch of a conditional type and can be resolved
		// to just the base type in the same cases as the conditional type resolves to its true branch
		// (because the base type is then known to satisfy the constraint).
		if ((newBaseType->flags & TypeFlagsTypeVariable) != 0 && isGenericType(newConstraint)) {
			return getSubstitutionType(newBaseType, newConstraint);
		}
		if ((newConstraint->flags & TypeFlagsAnyOrUnknown) != 0 ||
			isTypeAssignableTo(getRestrictiveInstantiation(newBaseType),
							   getRestrictiveInstantiation(newConstraint))) {
			return newBaseType;
		}
		if ((newBaseType->flags & TypeFlagsTypeVariable) != 0) {
			return getSubstitutionType(newBaseType, newConstraint);
		}
		return getIntersectionType({newConstraint, newBaseType});
	}
	return t;
}

// Handles instantiation of the following object types:
// AnonymousType (ObjectFlagsAnonymous|ObjectFlagsSingleSignatureType)
// TypeReference with node != nil (ObjectFlagsReference)
// InstantiationExpressionType (ObjectFlagsInstantiationExpressionType)
// MappedType (ObjectFlagsMapped)
// getObjectTypeInstantiation — checker.go:22717-22824

Type* Checker::getObjectTypeInstantiation(Type* t, TypeMapper* m, TypeAlias* alias) {
	Node* declaration;
	Type* target;
	std::vector<Type*> typeParameters;
	if ((t->objectFlags & ObjectFlagsReference) != 0) {  // Deferred type reference
		declaration = t->AsTypeReference()->node;
	} else if ((t->objectFlags & ObjectFlagsInstantiationExpressionType) != 0) {
		declaration = t->AsInstantiationExpressionType()->node;
	} else {
		declaration = t->symbol->declarations[0];
	}
	TypeNodeLinks* links = typeNodeLinks.Get(declaration);
	if ((t->objectFlags & ObjectFlagsReference) != 0) {  // Deferred type reference
		target = links->resolvedType;
	} else if ((t->objectFlags & ObjectFlagsInstantiated) != 0) {
		target = typeTarget(t);
	} else {
		target = t;
	}
	typeParameters = links->outerTypeParameters;
	// An empty stored vector doubles as "not yet computed" — getOuterTypeParameters
	// is idempotent, so recomputing a stored empty set is equivalent.
	if (typeParameters.empty()) {
		// The first time an anonymous type is instantiated we compute and store a list of the type
		// parameters that are in scope (and therefore potentially referenced). For type literals that
		// aren't the right hand side of a generic type alias declaration we optimize by reducing the
		// set of type parameters to those that are possibly referenced in the literal.
		typeParameters = getOuterTypeParameters(declaration, true /*includeThisTypes*/);
		if (aliasTypeArguments(target->alias).empty()) {
			if ((t->objectFlags &
				 (ObjectFlagsReference | ObjectFlagsInstantiationExpressionType)) != 0) {
				typeParameters = filterVec(typeParameters, [this, declaration](Type* tp) {
					return isTypeParameterPossiblyReferenced(tp, declaration);
				});
			} else if ((target->symbol->flags &
						(SymbolFlagsMethod | SymbolFlagsTypeLiteral)) != 0) {
				typeParameters = filterVec(typeParameters, [this, t](Type* tp) {
					return anyOf(t->symbol->declarations, [this, tp](Node* d) {
						return isTypeParameterPossiblyReferenced(tp, d);
					});
				});
			}
		}
		// (Go normalizes a nil result to an empty slice before storing; the
		// std::vector is already empty.)
		links->outerTypeParameters = typeParameters;
	}
	if (typeParameters.empty()) {
		return t;
	}
	// We are instantiating an anonymous type that has one or more type parameters in scope. Apply the
	// mapper to the type parameters to produce the effective list of type arguments, and compute the
	// instantiation cache key from the type IDs of the type arguments.
	std::vector<Type*> typeArguments(typeParameters.size());
	for (size_t i = 0; i < typeParameters.size(); i++) {
		typeArguments[i] = mapTypeWithCompositeMapper(typeParameters[i], typeMapperOf(t), m);
	}
	TypeAlias* newAlias = alias;
	if (newAlias == nullptr) {
		newAlias = instantiateTypeAlias(t->alias, m);
	}
	ObjectType* data = target->AsObjectType();
	CacheKey key = getTypeInstantiationKey(typeArguments, newAlias,
										 (t->objectFlags & ObjectFlagsSingleSignatureType) != 0);
	if (data->instantiations.empty()) {
		data->instantiations[getTypeInstantiationKey(typeParameters, target->alias, false)] =
			target;
	}
	Type* result = data->instantiations[key];
	if (result == nullptr) {
		TypeMapper* newMapper = newTypeMapper(typeParameters, typeArguments);
		if ((target->objectFlags & ObjectFlagsSingleSignatureType) != 0 && m != nullptr) {
			newMapper = combineTypeMappers(newMapper, m);
		}
		if ((target->objectFlags & ObjectFlagsReference) != 0) {
			result = createDeferredTypeReference(typeTarget(t), t->AsTypeReference()->node,
												 newMapper, newAlias);
		} else if ((target->objectFlags & ObjectFlagsMapped) != 0) {
			result = instantiateMappedType(target, newMapper, newAlias);
		} else {
			result = instantiateAnonymousType(target, newMapper, newAlias);
		}
		data->instantiations[key] = result;
		if ((result->flags & TypeFlagsObjectFlagsType) != 0 &&
			(result->objectFlags & ObjectFlagsCouldContainTypeVariablesComputed) == 0) {
			// if `result` is one of the object types we tried to make (it may not be, due to how `instantiateMappedType` works), we can carry forward the type variable containment check from the input type arguments
			bool resultCouldContainObjectFlags = anyOf(typeArguments, couldContainTypeVariables);
			if ((result->objectFlags & ObjectFlagsCouldContainTypeVariablesComputed) == 0) {
				if ((result->objectFlags &
					 (ObjectFlagsMapped | ObjectFlagsAnonymous | ObjectFlagsReference)) != 0) {
					result->objectFlags |= ObjectFlagsCouldContainTypeVariablesComputed |
						(resultCouldContainObjectFlags ? ObjectFlagsCouldContainTypeVariables : 0);
				} else {
					// If none of the type arguments for the outer type parameters contain type variables, it follows
					// that the instantiated type doesn't reference type variables.
					// Intrinsics have `CouldContainTypeVariablesComputed` pre-set, so this should only cover unions and intersections resulting from `instantiateMappedType`
					result->objectFlags |=
						(!resultCouldContainObjectFlags ? ObjectFlagsCouldContainTypeVariablesComputed
													  : 0);
				}
			}
		}
	}
	return result;
}

// isTypeParameterPossiblyReferenced — checker.go:22825-22882

bool Checker::isTypeParameterPossiblyReferenced(Type* tp, Node* node) {
	std::function<bool(Node*)> containsReference = [&](Node* node) -> bool {
		switch (node->kind) {
			case Kind::ThisType:
				return tp->AsTypeParameter()->isThisType;
			case Kind::TypeReference:
				// use worker because we're looking for === equality
				if (!tp->AsTypeParameter()->isThisType && node->typeArguments().empty() &&
					getSymbolFromTypeReference(node) == tp->symbol) {
					return true;
				}
				break;
			case Kind::TypeQuery: {
				Node* entityName = node->as<TypeQueryNode>()->ExprName;
				Node* firstIdentifier = getFirstIdentifier(entityName);
				if (!isThisIdentifier(firstIdentifier)) {
					Symbol* firstIdentifierSymbol = getResolvedSymbol(firstIdentifier);
					Node* tpDeclaration = tp->symbol->declarations[0];  // There is exactly one declaration, otherwise `containsReference` is not called
					Node* tpScope = nullptr;
					if (isTypeParameterDeclaration(tpDeclaration)) {
						tpScope = tpDeclaration->parent;  // Type parameter is a regular type parameter, e.g. foo<T>
					} else if (tp->AsTypeParameter()->isThisType) {
						tpScope = tpDeclaration;  // Type parameter is the this type, and its declaration is the class declaration.
					}
					if (tpScope != nullptr) {
						return anyOf(firstIdentifierSymbol->declarations,
									 [&](Node* d) { return isNodeDescendantOf(d, tpScope); }) ||
							anyOf(node->typeArguments(), containsReference);
					}
				}
				return true;
			}
			case Kind::MethodDeclaration:
			case Kind::MethodSignature: {
				Node* returnType = node->type();
				return (returnType == nullptr && node->body() != nullptr) ||
					anyOf(node->typeParameters(), containsReference) ||
					anyOf(node->parameters(), containsReference) ||
					(returnType != nullptr && containsReference(returnType));
			}
		}
		return node->forEachChild(containsReference);
	};
	// If the type parameter doesn't have exactly one declaration, if there are intervening statement blocks
	// between the node and the type parameter declaration, if the node contains actual references to the
	// type parameter, or if the node contains type queries that we can't prove couldn't contain references to the type parameter,
	// we consider the type parameter possibly referenced.
	if (tp->symbol != nullptr && tp->symbol->declarations.size() == 1) {
		Node* container = tp->symbol->declarations[0]->parent;
		for (Node* n = node; n != container; n = n->parent) {
			if (n == nullptr || isBlock(n) ||
				(isConditionalTypeNode(n) &&
				 containsReference(n->as<ConditionalTypeNode>()->ExtendsType))) {
				return true;
			}
		}
		return containsReference(node);
	}
	return true;
}

// instantiateAnonymousType — checker.go:22883-22908

Type* Checker::instantiateAnonymousType(Type* t, TypeMapper* m, TypeAlias* alias) {
	Type* result = newObjectType(
		(t->objectFlags &
		 ~(ObjectFlagsCouldContainTypeVariablesComputed | ObjectFlagsCouldContainTypeVariables)) |
			ObjectFlagsInstantiated,
		t->symbol);
	if ((t->objectFlags & ObjectFlagsMapped) != 0) {
		result->AsMappedType()->declaration = t->AsMappedType()->declaration;
		// C.f. instantiateSignature
		Type* origTypeParameter = getTypeParameterFromMappedType(t);
		Type* freshTypeParameter = cloneTypeParameter(origTypeParameter);
		result->AsMappedType()->typeParameter = freshTypeParameter;
		m = combineTypeMappers(newSimpleTypeMapper(origTypeParameter, freshTypeParameter), m);
		freshTypeParameter->AsTypeParameter()->mapper = m;
	} else if ((t->objectFlags & ObjectFlagsInstantiationExpressionType) != 0) {
		result->AsInstantiationExpressionType()->node =
			t->AsInstantiationExpressionType()->node;
	}
	if (alias == nullptr) {
		alias = instantiateTypeAlias(t->alias, m);
	}
	result->alias = alias;
	if (alias != nullptr && !alias->typeArguments.empty()) {
		result->objectFlags |=
			getPropagatingFlagsOfTypes(result->alias->typeArguments, TypeFlagsNone);
	}
	ObjectType* d = result->AsObjectType();
	d->target = t;
	d->mapper = m;
	return result;
}

// getConditionalTypeInstantiation — checker.go:22909-22939

Type* Checker::getConditionalTypeInstantiation(Type* t, TypeMapper* mapper,
											   bool forConstraint, TypeAlias* alias) {
	ConditionalRoot* root = t->AsConditionalType()->root;
	if (!root->outerTypeParameters.empty()) {
		// We are instantiating a conditional type that has one or more type parameters in scope. Apply the
		// mapper to the type parameters to produce the effective list of type arguments, and compute the
		// instantiation cache key from the type IDs of the type arguments.
		std::vector<Type*> typeArguments =
			mapVec(root->outerTypeParameters, [mapper](Type* tp) { return mapper->map(tp); });
		CacheKey key = getConditionalTypeKey(typeArguments, alias, forConstraint);
		Type* result = root->instantiations[key];
		if (result == nullptr) {
			TypeMapper* newMapper = newTypeMapper(root->outerTypeParameters, typeArguments);
			Type* checkType = root->checkType;
			Type* distributionType = nullptr;
			if (root->isDistributive) {
				distributionType = getReducedType(getMappedType(checkType, newMapper));
			}
			// Distributive conditional types are distributed over union types. For example, when the
			// distributive conditional type T extends U ? X : Y is instantiated with A | B for T, the
			// result is (A extends U ? X : Y) | (B extends U ? X : Y).
			if (distributionType != nullptr && checkType != distributionType &&
				(distributionType->flags & (TypeFlagsUnion | TypeFlagsNever)) != 0) {
				result = mapTypeWithAlias(
					distributionType,
					[this, root, newMapper, checkType, forConstraint](Type* t) -> Type* {
						return getConditionalType(
							root, prependTypeMapping(checkType, t, newMapper), forConstraint,
							nullptr);
					},
					alias);
			} else {
				result = getConditionalType(root, newMapper, forConstraint, alias);
			}
			root->instantiations[key] = result;
		}
		return result;
	}
	return t;
}

// cloneTypeParameter — checker.go:22941-22944

Type* Checker::cloneTypeParameter(Type* tp) {
	Type* result = newTypeParameter(tp->symbol);
	result->AsTypeParameter()->target = tp;
	return result;
}

// getHomomorphicTypeVariable — checker.go:22945-22955

Type* Checker::getHomomorphicTypeVariable(Type* t) {
	Type* constraintType = getConstraintTypeFromMappedType(t);
	if ((constraintType->flags & TypeFlagsIndex) != 0) {
		Type* typeVariable = getActualTypeVariable(constraintType->AsIndexType()->target);
		if ((typeVariable->flags & TypeFlagsTypeParameter) != 0) {
			return typeVariable;
		}
	}
	return nullptr;
}

// instantiateMappedType — checker.go:22956-22995

Type* Checker::instantiateMappedType(Type* t, TypeMapper* m, TypeAlias* alias) {
	// For a homomorphic mapped type { [P in keyof T]: X }, where T is some type variable, the mapping
	// operation depends on T as follows:
	// * If T is a primitive type no mapping is performed and the result is simply T.
	// * If T is a union type we distribute the mapped type over the union.
	// * If T is an array we map to an array where the element type has been transformed.
	// * If T is a tuple we map to a tuple where the element types have been transformed.
	// * If T is an intersection of array or tuple types we map to an intersection of transformed array or tuple types.
	// * Otherwise we map to an object type where the type of each property has been transformed.
	// For example, when T is instantiated to a union type A | B, we produce { [P in keyof A]: X } |
	// { [P in keyof B]: X }, and when when T is instantiated to a union type A | undefined, we produce
	// { [P in keyof A]: X } | undefined.
	MappedType* d = t->AsMappedType();
	Type* typeVariable = getHomomorphicTypeVariable(t);
	std::function<Type*(Type*)> instantiateConstituent = [&](Type* s) -> Type* {
		if ((s->flags & (TypeFlagsAnyOrUnknown | TypeFlagsInstantiableNonPrimitive |
						 TypeFlagsObject | TypeFlagsIntersection)) == 0 ||
			s == wildcardType || isErrorType(s)) {
			return s;
		}
		if (d->declaration->as<MappedTypeNode>()->NameType == nullptr) {
			if (isArrayType(s) ||
				((s->flags & TypeFlagsAny) != 0 &&
				 findResolutionCycleStartIndex(
					 typeVariable, TypeSystemPropertyName::ResolvedBaseConstraint) < 0 &&
				 hasArrayOrTypeTypeConstraint(typeVariable))) {
				return instantiateMappedArrayType(s, t, prependTypeMapping(typeVariable, s, m));
			}
			if (isTupleType(s)) {
				return instantiateMappedTupleType(s, t, typeVariable, m);
			}
			if (isArrayOrTupleOrIntersection(s)) {
				return getIntersectionType(mapVec(typeTypes(s), instantiateConstituent));
			}
		}
		return instantiateAnonymousType(t, prependTypeMapping(typeVariable, s, m), nullptr);
	};
	if (typeVariable != nullptr) {
		Type* mappedTypeVariable = instantiateType(typeVariable, m);
		if (typeVariable != mappedTypeVariable) {
			return mapTypeWithAlias(getReducedType(mappedTypeVariable), instantiateConstituent,
									alias);
		}
	}
	// If the constraint type of the instantiation is the wildcard type, return the wildcard type.
	if (instantiateType(getConstraintTypeFromMappedType(t), m) == wildcardType) {
		return wildcardType;
	}
	return instantiateAnonymousType(t, m, alias);
}

// hasArrayOrTypeTypeConstraint — checker.go:22996-22999

bool Checker::hasArrayOrTypeTypeConstraint(Type* typeVariable) {
	Type* constraint = getConstraintOfTypeParameter(typeVariable);
	return constraint != nullptr && everyType(constraint, [this](Type* s) {
		return isArrayOrTupleType(s);
	});
}

// instantiateMappedArrayType — checker.go:23001-23007

Type* Checker::instantiateMappedArrayType(Type* arrayType, Type* mappedType, TypeMapper* m) {
	Type* elementType =
		instantiateMappedTypeTemplate(mappedType, numberType, true /*isOptional*/, m);
	if (isErrorType(elementType)) {
		return errorType;
	}
	return createArrayTypeEx(
		elementType,
		getModifiedReadonlyState(isReadonlyArrayType(arrayType),
								 getMappedTypeModifiers(mappedType)));
}

// instantiateMappedTupleType — checker.go:23008-23066

Type* Checker::instantiateMappedTupleType(Type* tupleType, Type* mappedType,
										  Type* typeVariable, TypeMapper* m) {
	// We apply the mapped type's template type to each of the fixed part elements. For variadic elements, we
	// apply the mapped type itself to the variadic element type. For other elements in the variable part of the
	// tuple, we surround the element type with an array type and apply the mapped type to that. This ensures
	// that we get sequential property key types for the fixed part of the tuple, and property key type number
	// for the remaining elements. For example
	//
	//   type Keys<T> = { [K in keyof T]: K };
	//   type Foo<T extends any[]> = Keys<[string, string, ...T, string]>; // ["0", "1", ...Keys<T>, number]
	//
	const std::vector<TupleElementInfo>& elementInfos =
		tupleType->AsTypeReference()->target->AsTupleType()->elementInfos;
	int32_t fixedLength = tupleType->AsTypeReference()->target->AsTupleType()->fixedLength;
	TypeMapper* fixedMapper = m;
	if (fixedLength != 0) {
		fixedMapper = prependTypeMapping(typeVariable, tupleType, m);
	}
	MappedTypeModifiers modifiers = getMappedTypeModifiers(mappedType);
	std::vector<Type*> elementTypes = getElementTypes(tupleType);
	std::vector<Type*> newElementTypes(elementTypes.size());
	std::vector<TupleElementInfo> newElementInfos = elementInfos;
	for (size_t i = 0; i < elementTypes.size(); i++) {
		Type* e = elementTypes[i];
		ElementFlags flags = elementInfos[i].flags;
		Type* mapped;
		if ((int32_t)i < fixedLength) {
			mapped = instantiateMappedTypeTemplate(mappedType,
												   getStringLiteralType(std::to_string(i)),
												   (flags & ElementFlagsOptional) != 0, fixedMapper);
		} else if ((flags & ElementFlagsVariadic) != 0) {
			mapped = instantiateType(mappedType, prependTypeMapping(typeVariable, e, m));
		} else {
			mapped = getElementTypeOfArrayType(
				instantiateType(mappedType,
								prependTypeMapping(typeVariable, createArrayType(e), m)));
			if (mapped == nullptr) {
				mapped = unknownType;
			}
		}
		if ((modifiers & MappedTypeModifiersIncludeOptional) != 0) {
			if ((flags & ElementFlagsRequired) != 0) {
				newElementInfos[i].flags = ElementFlagsOptional;
			}
		} else if ((modifiers & MappedTypeModifiersExcludeOptional) != 0) {
			if ((flags & ElementFlagsOptional) != 0) {
				newElementInfos[i].flags = ElementFlagsRequired;
			}
		}
		newElementTypes[i] = mapped;
	}
	bool newReadonly = getModifiedReadonlyState(
		tupleType->AsTypeReference()->target->AsTupleType()->readonly,
		getMappedTypeModifiers(mappedType));
	if (std::find(newElementTypes.begin(), newElementTypes.end(), errorType) !=
		newElementTypes.end()) {
		return errorType;
	}
	return createTupleTypeEx(newElementTypes, newElementInfos, newReadonly);
}

// instantiateMappedTypeTemplate — checker.go:23067-23081

Type* Checker::instantiateMappedTypeTemplate(Type* t, Type* key, bool isOptional,
											 TypeMapper* m) {
	TypeMapper* templateMapper =
		appendTypeMapping(m, getTypeParameterFromMappedType(t), key);
	MappedType* mapped = t->AsMappedType();
	Type* propType = instantiateType(
		getTemplateTypeFromMappedType(mapped->target != nullptr ? mapped->target : t),
		templateMapper);
	MappedTypeModifiers modifiers = getMappedTypeModifiers(t);
	if (strictNullChecks && (modifiers & MappedTypeModifiersIncludeOptional) != 0 &&
		!maybeTypeOfKind(propType, TypeFlagsUndefined | TypeFlagsVoid)) {
		return getOptionalType(propType, true /*isProperty*/);
	}
	if (strictNullChecks && (modifiers & MappedTypeModifiersExcludeOptional) != 0 &&
		isOptional) {
		return removeMissingOrUndefinedType(propType);
	}
	return propType;
}

// (getModifiedReadonlyState — checker.go:23082-23090 — is a free function; it
// lives in the anonymous namespace at the top of this file.)

// getTypeParameterFromMappedType — checker.go:23091-23097

// getConstraintTypeFromMappedType — checker.go:23099-23106

// getNameTypeFromMappedType — checker.go:23107-23116

// getTemplateTypeFromMappedType — checker.go:23118-23129

Type* Checker::getTemplateTypeFromMappedType(Type* t) {
	MappedType* m = t->AsMappedType();
	if (m->templateType == nullptr) {
		Node* templateTypeNode = m->declaration->as<MappedTypeNode>()->Type;
		if (templateTypeNode != nullptr) {
			m->templateType = instantiateType(
				addOptionalityEx(getTypeFromTypeNode(templateTypeNode) /*isProperty*/, true,
								 (getMappedTypeModifiers(t) &
								  MappedTypeModifiersIncludeOptional) != 0),
				m->mapper);
		} else {
			m->templateType = errorType;
		}
	}
	return m->templateType;
}

// isMappedTypeWithKeyofConstraintDeclaration — checker.go:23130-23134

bool Checker::isMappedTypeWithKeyofConstraintDeclaration(Type* t) {
	Node* constraintDeclaration = getConstraintDeclarationForMappedType(t);
	return isTypeOperatorNode(constraintDeclaration) &&
		constraintDeclaration->as<TypeOperatorNode>()->Operator == Kind::KeyOfKeyword;
}

// getConstraintDeclarationForMappedType — checker.go:23135-23138

Node* Checker::getConstraintDeclarationForMappedType(Type* t) {
	return t->AsMappedType()
		->declaration->as<MappedTypeNode>()
		->TypeParameter->as<TypeParameterDeclaration>()
		->Constraint;
}

// getApparentMappedTypeKeys — checker.go:23139-23147

Type* Checker::getApparentMappedTypeKeys(Type* nameType, Type* targetType) {
	Type* modifiersType = getApparentType(getModifiersTypeFromMappedType(targetType));
	std::vector<Type*> mappedKeys;
	forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType(
		modifiersType, TypeFlagsStringOrNumberLiteralOrUnique, false, [&](Type* t) {
			mappedKeys.push_back(instantiateType(
				nameType,
				appendTypeMapping(typeMapperOf(targetType),
								  getTypeParameterFromMappedType(targetType), t)));
		});
	return getUnionType(mappedKeys);
}

// forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType — checker.go:23148-23160

void Checker::forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType(
	Type* t, TypeFlags include, bool stringsOnly, const std::function<void(Type*)>& cb) {
	for (Symbol* prop : getPropertiesOfType(t)) {
		cb(getLiteralTypeFromProperty(prop, include, false));
	}
	if ((t->flags & TypeFlagsAny) != 0) {
		cb(stringType);
	} else {
		for (IndexInfo* info : getIndexInfosOfType(t)) {
			if (!stringsOnly ||
				(info->keyType->flags & (TypeFlagsString | TypeFlagsTemplateLiteral)) != 0) {
				cb(info->keyType);
			}
		}
	}
}

// instantiateReverseMappedType — checker.go:23161-23179

Type* Checker::instantiateReverseMappedType(Type* t, TypeMapper* m) {
	ReverseMappedType* r = t->AsReverseMappedType();
	Type* innerMappedType = instantiateType(r->mappedType, m);
	if ((innerMappedType->objectFlags & ObjectFlagsMapped) == 0) {
		return t;
	}
	Type* innerIndexType = instantiateType(r->constraintType, m);
	if ((innerIndexType->flags & TypeFlagsIndex) == 0) {
		return t;
	}
	Type* instantiated = inferTypeForHomomorphicMappedType(
		instantiateType(r->source, m), innerMappedType, innerIndexType);
	if (instantiated != nullptr) {
		return instantiated;
	}
	return t;
	// Nested invocation of `inferTypeForHomomorphicMappedType` or the `source` instantiated into something unmappable
}

// instantiateTypeAlias — checker.go:23180-23187

TypeAlias* Checker::instantiateTypeAlias(TypeAlias* alias, TypeMapper* m) {
	if (alias == nullptr) {
		return nullptr;
	}
	return new TypeAlias{alias->symbol, instantiateTypes(alias->typeArguments, m)};
}

// instantiateTypes — checker.go:23188-23191

// instantiateSymbols — checker.go:23192-23195

std::vector<Symbol*> Checker::instantiateSymbols(const std::vector<Symbol*>& symbols,
												 TypeMapper* m) {
	return instantiateList(symbols, m, &Checker::instantiateSymbol);
}

// instantiateSignatures — checker.go:23196-23199

std::vector<Signature*> Checker::instantiateSignatures(
	const std::vector<Signature*>& signatures, TypeMapper* m) {
	return instantiateList(signatures, m, &Checker::instantiateSignature);
}

// instantiateIndexInfos — checker.go:23200-23203

std::vector<IndexInfo*> Checker::instantiateIndexInfos(
	const std::vector<IndexInfo*>& indexInfos, TypeMapper* m) {
	return instantiateList(indexInfos, m, &Checker::instantiateIndexInfo);
}

// instantiateList — checker.go:23204-23219
// (Go generic — member template; only instantiated within this TU.)

template <typename T>
std::vector<T> Checker::instantiateList(const std::vector<T>& values, TypeMapper* m,
										T (Checker::*instantiator)(T, TypeMapper*)) {
	for (size_t i = 0; i < values.size(); i++) {
		T mapped = (this->*instantiator)(values[i], m);
		if (mapped != values[i]) {
			std::vector<T> result(values.size());
			std::copy(values.begin(), values.begin() + (ptrdiff_t)i, result.begin());
			result[i] = mapped;
			for (size_t j = i + 1; j < values.size(); j++) {
				result[j] = (this->*instantiator)(values[j], m);
			}
			return result;
		}
	}
	return values;
}

// ---------------------------------------------------------------------------
// === dep stubs — removed when owner slice lands ===
// ---------------------------------------------------------------------------

Type* Checker::createNormalizedTypeReference(Type* target,
											 std::vector<Type*> typeArguments) {
	TSC_UNREACHABLE("createNormalizedTypeReference — instantiate dep");
}
Type* Checker::createTupleTypeEx(std::vector<Type*> elementTypes,
								 std::vector<TupleElementInfo> elementInfos, bool readonly) {
	TSC_UNREACHABLE("createTupleTypeEx — instantiate dep");
}
// (deduped: addOptionalityEx defined in cpp/internal/checker/checker_decltypes.cpp)

Type* Checker::inferTypeForHomomorphicMappedType(Type* source, Type* target,
												 Type* constraint) {
	TSC_UNREACHABLE("inferTypeForHomomorphicMappedType — instantiate dep");
}
// (deduped: getActualTypeVariable defined in cpp/internal/checker/checker_contextual.cpp)


// Free-function dep stubs (checker package / utilities.go).

CacheKey getTypeInstantiationKey(const std::vector<Type*>& typeArguments, TypeAlias* alias,
								 bool singleSignature) {
	TSC_UNREACHABLE("getTypeInstantiationKey — instantiate dep");
}
CacheKey getConditionalTypeKey(const std::vector<Type*>& typeArguments, TypeAlias* alias,
							   bool forConstraint) {
	TSC_UNREACHABLE("getConditionalTypeKey — instantiate dep");
}
// checker.go:27288 isNoInferType + 27860 getNoInferType (real ports; their only
// callers landed with this slice).
// checker.go:27877-27894 getSubstitutionType / getOrCreateSubstitutionType +
// 27866 isNoInferTargetType.

}  // namespace checker
}  // namespace tsc
