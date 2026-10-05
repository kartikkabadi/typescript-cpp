// Port of tsc/internal/checker/tracer.go — the type-checker tracer.
//
// A null `Checker::tracer` is a valid no-op, matching Go's `*Tracer` nil
// convention; call sites gate on `if (tr := c.tracer; tr != nullptr)`.
#include "internal/checker/checker.h"
#include "internal/tracing/tracing.h"

#include <any>
#include <functional>
#include <utility>

namespace tsc::checker {

// ---------------------------------------------------------------------------
// Tracer dependencies ported file-locally until their own slices land:
//   RecursionId / asRecursionId          — relater.go:89-96
//   getRecursionIdentity*                — relater.go:810-868
//   isTupleType                          — checker.go:23946
//   isObjectOrArrayLiteralType           — utilities.go:1033
//   typeFlagNames / FormatTypeFlags      — types.go:520-568
// ---------------------------------------------------------------------------

// relater.go:89 — type RecursionId struct{ value any }. The value is only ever
// *ast.Node, *ast.Symbol or *Type, so a void* carries it faithfully.
struct RecursionId {
	void* value{};
};

static RecursionId asRecursionId(Node* value) { return {value}; }
static RecursionId asRecursionId(Symbol* value) { return {value}; }
static RecursionId asRecursionId(Type* value) { return {value}; }

// checker.go:23946
static bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 &&
		(t->Target()->objectFlags & ObjectFlagsTuple) != 0;
}

// utilities.go:1033
static bool isObjectOrArrayLiteralType(Type* t) {
	return (t->objectFlags & (ObjectFlagsObjectLiteral | ObjectFlagsArrayLiteral)) != 0;
}

// relater.go:820 — Get the recursion identity target type from a type. Recursively (a) obtain the
// target object type of an indexed access (i.e. the T in T[K]), and (b) unwrap nested homomorphic
// mapped types and return the deepest target type that has a symbol. The unwrapping better
// preserves unique type identities for mapped types applied to explicitly written object literals.
static Type* getRecursionIdentityTarget(Type* t) {
	if (t->flags & TypeFlagsIndexedAccess) {
		return getRecursionIdentityTarget(t->AsIndexedAccessType()->objectType);
	}
	if ((t->objectFlags & ObjectFlagsInstantiatedMapped) == ObjectFlagsInstantiatedMapped) {
		Type* target = t->checker->getModifiersTypeFromMappedType(t);
		if (target != nullptr &&
			(target->symbol != nullptr ||
			 ((target->flags & TypeFlagsIntersection) != 0 &&
			  std::any_of(target->types().begin(), target->types().end(),
						  [](Type* t) { return t->symbol != nullptr; })))) {
			return getRecursionIdentityTarget(target);
		}
	}
	return t;
}

// relater.go:840 — The recursion identity of a type is an object identity that is shared among
// multiple instantiations of the type. We track recursion identities in order to identify deeply
// nested and possibly infinite type instantiations with the same origin.
static RecursionId getRecursionIdentityFromTarget(Type* t) {
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
static RecursionId getRecursionIdentity(Type* t) {
	return getRecursionIdentityFromTarget(getRecursionIdentityTarget(t));
}

// types.go:520 — var typeFlagNames
static const struct {
	TypeFlags flag;
	const char* name;
} typeFlagNames[] = {
	{TypeFlagsAny, "Any"},
	{TypeFlagsUnknown, "Unknown"},
	{TypeFlagsUndefined, "Undefined"},
	{TypeFlagsNull, "Null"},
	{TypeFlagsVoid, "Void"},
	{TypeFlagsString, "String"},
	{TypeFlagsNumber, "Number"},
	{TypeFlagsBigInt, "BigInt"},
	{TypeFlagsBoolean, "Boolean"},
	{TypeFlagsESSymbol, "ESSymbol"},
	{TypeFlagsStringLiteral, "StringLiteral"},
	{TypeFlagsNumberLiteral, "NumberLiteral"},
	{TypeFlagsBigIntLiteral, "BigIntLiteral"},
	{TypeFlagsBooleanLiteral, "BooleanLiteral"},
	{TypeFlagsUniqueESSymbol, "UniqueESSymbol"},
	{TypeFlagsEnumLiteral, "EnumLiteral"},
	{TypeFlagsEnum, "Enum"},
	{TypeFlagsNonPrimitive, "NonPrimitive"},
	{TypeFlagsNever, "Never"},
	{TypeFlagsTypeParameter, "TypeParameter"},
	{TypeFlagsObject, "Object"},
	{TypeFlagsIndex, "Index"},
	{TypeFlagsTemplateLiteral, "TemplateLiteral"},
	{TypeFlagsStringMapping, "StringMapping"},
	{TypeFlagsSubstitution, "Substitution"},
	{TypeFlagsIndexedAccess, "IndexedAccess"},
	{TypeFlagsConditional, "Conditional"},
	{TypeFlagsUnion, "Union"},
	{TypeFlagsIntersection, "Intersection"},
};

// types.go:556 — FormatTypeFlags returns the individual flag names as a slice of strings.
std::vector<std::string> FormatTypeFlags(TypeFlags flags) {
	std::vector<std::string> result;
	for (const auto& fn : typeFlagNames) {
		if (flags & fn.flag) {
			result.emplace_back(fn.name);
		}
	}
	if (result.empty()) {
		result.emplace_back("None");
	}
	return result;
}

// Forward decls — defined at the bottom of this file (tracer.go:350-366).
tsc::tracing::TracedType* wrapType(Type* t);
std::vector<tsc::tracing::TracedType*> wrapTypes(const std::vector<Type*>& types);

// ---------------------------------------------------------------------------
// Tracer — tracer.go:13-71
// ---------------------------------------------------------------------------

// tracer.go:21 — NewTracer creates a Tracer for the given checker index that records both
// type-creation events and trace events through the provided tracing session.
Tracer* newTracer(tsc::tracing::Tracing* tr, int checkerIndex) {
	auto* t = new Tracer(); // Go GC; owned by the tracing session's lifetime.
	t->tracing = tr;
	t->recorder = tr->NewTypeTracer(checkerIndex);
	t->checkerIndex = checkerIndex;
	return t;
}

// tracer.go:25
void Tracer::RecordType(Type* typ) {
	recorder->RecordType(wrapType(typ));
}

// tracer.go:29 — `args` is taken by value so the separateBeginAndEnd closure can
// safely mutate its own copy on pop, mirroring the Go map's lifetime.
std::function<void()> Tracer::Push(tsc::tracing::Phase phase, const std::string& name,
								   tsc::tracing::TraceArgs args, bool separateBeginAndEnd) {
	if (!separateBeginAndEnd) {
		return tracing->Push(std::move(phase), name, copyWithCheckerIndex(args),
							 separateBeginAndEnd);
	}

	std::function<void()> restore = temporarilyAddCheckerIndex(args);
	std::function<void()> pop =
		tracing->Push(std::move(phase), name, args, separateBeginAndEnd);
	restore();

	return [this, args = std::move(args), pop = std::move(pop)]() mutable {
		std::function<void()> restoreEndArgs = temporarilyAddCheckerIndex(args);
		pop();
		restoreEndArgs();
	};
}

// tracer.go:45
void Tracer::Instant(tsc::tracing::Phase phase, const std::string& name,
					 const tsc::tracing::TraceArgs& args) {
	tracing->Instant(std::move(phase), name, copyWithCheckerIndex(args));
}

// tracer.go:49 — maps.Copy into a fresh map sized len(args)+1.
tsc::tracing::TraceArgs Tracer::copyWithCheckerIndex(const tsc::tracing::TraceArgs& args) {
	tsc::tracing::TraceArgs withCheckerIndex;
	withCheckerIndex.reserve(args.size() + 1);
	withCheckerIndex.insert(args.begin(), args.end());
	withCheckerIndex["checkerId"] = checkerIndex;
	return withCheckerIndex;
}

// tracer.go:56 — The map is never nil here: callers pass it by value, so the
// Go `args == nil` guard is subsumed by the copy.
std::function<void()> Tracer::temporarilyAddCheckerIndex(tsc::tracing::TraceArgs& args) {
	std::any previous;
	bool hadPrevious = false;
	if (auto it = args.find("checkerId"); it != args.end()) {
		previous = it->second;
		hadPrevious = true;
	}
	args["checkerId"] = checkerIndex;

	return [&args, previous = std::move(previous), hadPrevious]() {
		if (hadPrevious) {
			args["checkerId"] = previous;
		} else {
			args.erase("checkerId");
		}
	};
}

// ---------------------------------------------------------------------------
// tracedTypeAdapter — tracer.go:73-348 — adapts a Type to tracing.TracedType
// ---------------------------------------------------------------------------

class tracedTypeAdapter final : public tsc::tracing::TracedType {
public:
	Type* t{};
	Checker* checker{};

	tracedTypeAdapter(Type* t, Checker* checker) : t(t), checker(checker) {}

	uint32_t Id() override { return static_cast<uint32_t>(t->id); }

	std::vector<std::string> FormatFlags() override { return FormatTypeFlags(t->flags); }

	bool IsConditional() override { return (t->flags & TypeFlagsConditional) != 0; }

	tsc::Symbol* Symbol() override { return t->symbol; }

	tsc::Symbol* AliasSymbol() override {
		if (t->alias == nullptr) {
			return nullptr;
		}
		return t->alias->SymbolOrNil();
	}

	std::vector<tsc::tracing::TracedType*> AliasTypeArguments() override {
		if (t->alias == nullptr) {
			return {};
		}
		return wrapTypes(t->alias->TypeArguments());
	}

	std::string IntrinsicName() override {
		if ((t->flags & TypeFlagsIntrinsic) == 0) {
			return "";
		}
		// Go type-asserts data.(*IntrinsicType); the flag implies the data kind.
		return static_cast<IntrinsicType*>(t->data)->intrinsicName;
	}

	std::vector<tsc::tracing::TracedType*> UnionTypes() override {
		if ((t->flags & TypeFlagsUnion) == 0) {
			return {};
		}
		return wrapTypes(t->AsUnionType()->types);
	}

	std::vector<tsc::tracing::TracedType*> IntersectionTypes() override {
		if ((t->flags & TypeFlagsIntersection) == 0) {
			return {};
		}
		return wrapTypes(t->AsIntersectionType()->types);
	}

	tsc::tracing::TracedType* IndexType() override {
		if ((t->flags & TypeFlagsIndex) == 0) {
			return nullptr;
		}
		Type* target = t->AsIndexType()->target;
		if (target == nullptr) {
			return nullptr;
		}
		return wrapType(target);
	}

	tsc::tracing::TracedType* IndexedAccessObjectType() override {
		if ((t->flags & TypeFlagsIndexedAccess) == 0) {
			return nullptr;
		}
		Type* objectType = t->AsIndexedAccessType()->objectType;
		if (objectType == nullptr) {
			return nullptr;
		}
		return wrapType(objectType);
	}

	tsc::tracing::TracedType* IndexedAccessIndexType() override {
		if ((t->flags & TypeFlagsIndexedAccess) == 0) {
			return nullptr;
		}
		Type* indexType = t->AsIndexedAccessType()->indexType;
		if (indexType == nullptr) {
			return nullptr;
		}
		return wrapType(indexType);
	}

	tsc::tracing::TracedType* ConditionalCheckType() override {
		if ((t->flags & TypeFlagsConditional) == 0) {
			return nullptr;
		}
		Type* checkType = t->AsConditionalType()->checkType;
		if (checkType == nullptr) {
			return nullptr;
		}
		return wrapType(checkType);
	}

	tsc::tracing::TracedType* ConditionalExtendsType() override {
		if ((t->flags & TypeFlagsConditional) == 0) {
			return nullptr;
		}
		Type* extendsType = t->AsConditionalType()->extendsType;
		if (extendsType == nullptr) {
			return nullptr;
		}
		return wrapType(extendsType);
	}

	tsc::tracing::TracedType* ConditionalTrueType() override {
		if ((t->flags & TypeFlagsConditional) == 0) {
			return nullptr;
		}
		Type* resolvedTrueType = t->AsConditionalType()->resolvedTrueType;
		if (resolvedTrueType == nullptr) {
			return nullptr;
		}
		return wrapType(resolvedTrueType);
	}

	tsc::tracing::TracedType* ConditionalFalseType() override {
		if ((t->flags & TypeFlagsConditional) == 0) {
			return nullptr;
		}
		Type* resolvedFalseType = t->AsConditionalType()->resolvedFalseType;
		if (resolvedFalseType == nullptr) {
			return nullptr;
		}
		return wrapType(resolvedFalseType);
	}

	tsc::tracing::TracedType* SubstitutionBaseType() override {
		if ((t->flags & TypeFlagsSubstitution) == 0) {
			return nullptr;
		}
		Type* baseType = t->AsSubstitutionType()->baseType;
		if (baseType == nullptr) {
			return nullptr;
		}
		return wrapType(baseType);
	}

	tsc::tracing::TracedType* SubstitutionConstraintType() override {
		if ((t->flags & TypeFlagsSubstitution) == 0) {
			return nullptr;
		}
		Type* constraint = t->AsSubstitutionType()->constraint;
		if (constraint == nullptr) {
			return nullptr;
		}
		return wrapType(constraint);
	}

	tsc::tracing::TracedType* ReferenceTarget() override {
		if ((t->flags & TypeFlagsObject) == 0 ||
			(t->objectFlags & ObjectFlagsReference) == 0) {
			return nullptr;
		}
		Type* target = t->AsTypeReference()->target;
		if (target == nullptr) {
			return nullptr;
		}
		return wrapType(target);
	}

	std::vector<tsc::tracing::TracedType*> ReferenceTypeArguments() override {
		if ((t->flags & TypeFlagsObject) == 0 ||
			(t->objectFlags & ObjectFlagsReference) == 0) {
			return {};
		}
		return wrapTypes(t->AsTypeReference()->resolvedTypeArguments);
	}

	tsc::Node* ReferenceNode() override {
		if ((t->flags & TypeFlagsObject) == 0 ||
			(t->objectFlags & ObjectFlagsReference) == 0) {
			return nullptr;
		}
		return t->AsTypeReference()->node;
	}

	tsc::tracing::TracedType* ReverseMappedSourceType() override {
		if ((t->flags & TypeFlagsObject) == 0 ||
			(t->objectFlags & ObjectFlagsReverseMapped) == 0) {
			return nullptr;
		}
		Type* source = t->AsReverseMappedType()->source;
		if (source == nullptr) {
			return nullptr;
		}
		return wrapType(source);
	}

	tsc::tracing::TracedType* ReverseMappedMappedType() override {
		if ((t->flags & TypeFlagsObject) == 0 ||
			(t->objectFlags & ObjectFlagsReverseMapped) == 0) {
			return nullptr;
		}
		Type* mappedType = t->AsReverseMappedType()->mappedType;
		if (mappedType == nullptr) {
			return nullptr;
		}
		return wrapType(mappedType);
	}

	tsc::tracing::TracedType* ReverseMappedConstraintType() override {
		if ((t->flags & TypeFlagsObject) == 0 ||
			(t->objectFlags & ObjectFlagsReverseMapped) == 0) {
			return nullptr;
		}
		Type* constraintType = t->AsReverseMappedType()->constraintType;
		if (constraintType == nullptr) {
			return nullptr;
		}
		return wrapType(constraintType);
	}

	tsc::tracing::TracedType* EvolvingArrayElementType() override {
		if ((t->flags & TypeFlagsObject) == 0 ||
			(t->objectFlags & ObjectFlagsEvolvingArray) == 0) {
			return nullptr;
		}
		Type* elementType = t->AsEvolvingArrayType()->elementType;
		if (elementType == nullptr) {
			return nullptr;
		}
		return wrapType(elementType);
	}

	tsc::tracing::TracedType* EvolvingArrayFinalType() override {
		if ((t->flags & TypeFlagsObject) == 0 ||
			(t->objectFlags & ObjectFlagsEvolvingArray) == 0) {
			return nullptr;
		}
		Type* finalArrayType = t->AsEvolvingArrayType()->finalArrayType;
		if (finalArrayType == nullptr) {
			return nullptr;
		}
		return wrapType(finalArrayType);
	}

	bool IsTuple() override { return (t->objectFlags & ObjectFlagsTuple) != 0; }

	tsc::Node* Pattern() override {
		if (checker == nullptr) {
			return nullptr;
		}
		auto it = checker->patternForType.find(t);
		return it != checker->patternForType.end() ? it->second : nullptr;
	}

	void* RecursionIdentity() override { return getRecursionIdentity(t).value; }

	std::string Display() override {
		// Compute display text for types where it's valuable for trace analysis.
		// TypeScript only does this for Anonymous|Literal types, but we extend to
		// unions, intersections, and template literals since they often lack
		// firstDeclaration and the display text helps identify them.
		// Incomplete types during tracing can cause panics, which we intentionally
		// suppress (returning ""), matching TypeScript's try/catch around typeToString.
		if (checker == nullptr) {
			return "";
		}
		if ((t->objectFlags & ObjectFlagsAnonymous) != 0 ||
			(t->flags & (TypeFlagsLiteral | TypeFlagsTemplateLiteral | TypeFlagsUnion |
						 TypeFlagsIntersection)) != 0) {
			// Go defers recover() here so a mid-resolution panic yields "".
			try {
				return checker->TypeToString(t);
			} catch (...) {
				return "";
			}
		}
		return "";
	}
};

// ---------------------------------------------------------------------------
// wrapType / wrapTypes — tracer.go:350-366
// ---------------------------------------------------------------------------

// tracer.go:350 — heap-allocated to match Go's GC'd adapter; the tracing
// session owns the resulting objects.
tsc::tracing::TracedType* wrapType(Type* t) {
	if (t == nullptr) {
		return nullptr;
	}
	return new tracedTypeAdapter(t, t->checker);
}

// tracer.go:357
std::vector<tsc::tracing::TracedType*> wrapTypes(const std::vector<Type*>& types) {
	if (types.empty()) {
		return {};
	}
	std::vector<tsc::tracing::TracedType*> result(types.size());
	for (size_t i = 0; i < types.size(); i++) {
		result[i] = wrapType(types[i]);
	}
	return result;
}

// ---------------------------------------------------------------------------
// Dependencies owned by other slices — stubbed until they land.
// ---------------------------------------------------------------------------

// printer.go:43
std::string Checker::TypeToString(Type* t) {
	TSC_UNREACHABLE("TypeToString — ported with the printer slice");
}

// checker.go:28593

} // namespace tsc::checker
