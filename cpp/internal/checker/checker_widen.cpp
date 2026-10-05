// checker_widen.cpp — literal-type widening machinery.
// Ports checker.go:25739-26019 (getRegularTypeOfLiteralType through
// isLiteralOfContextualType). Functions are ported in file order.
//
// Most of this range was already ported in-place in checker.cpp while earlier
// slices needed it; those bodies moved here with the slice. The dep stubs that
// checker_walk.cpp kept for this slice were removed when this file landed.

#include "internal/checker/checker.h"
#include "internal/jsnum/jsnum.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace tsc::checker {

// Forward declarations for free helpers defined (non-static) in checker.cpp.
bool everyType(Type* t, const std::function<bool(Type*)>& f);
bool someType(Type* t, const std::function<bool(Type*)>& f);

// ---------------------------------------------------------------------------
// Fresh/regular literal types — checker.go:25739-25830
// ---------------------------------------------------------------------------

Type* Checker::getRegularTypeOfLiteralType(Type* t) {
	if (t->flags & TypeFlagsFreshable) {
		return t->AsLiteralType()->regularType;
	}
	if (t->flags & TypeFlagsUnion) {
		UnionType* u = t->AsUnionType();
		if (u->regularType == nullptr) {
			u->regularType = mapType(t, [this](Type* s) { return getRegularTypeOfLiteralType(s); });
		}
		return u->regularType;
	}
	return t;
}

Type* Checker::getFreshTypeOfLiteralType(Type* t) {
	if (t->flags & TypeFlagsFreshable) {
		LiteralType* d = t->AsLiteralType();
		if (d->freshType == nullptr) {
			Type* f = newLiteralType(t->flags, d->value, t);
			f->symbol = t->symbol;
			f->AsLiteralType()->freshType = f;
			d->freshType = f;
		}
		return d->freshType;
	}
	return t;
}

static bool isFreshLiteralType(Type* t) {
	return (t->flags & TypeFlagsFreshable) != 0 && t->AsLiteralType()->freshType == t;
}

Type* Checker::getStringLiteralType(const std::string& value) {
	Type* t = stringLiteralTypes[value];
	if (t == nullptr) {
		t = newLiteralType(TypeFlagsStringLiteral, value, nullptr);
		stringLiteralTypes[value] = t;
	}
	return t;
}

Type* Checker::getNumberLiteralType(Number value) {
	// NaN is not a usable map key (NaN != NaN), so cache the NaN type separately.
	if (value.isNaN()) {
		if (nanType == nullptr) {
			nanType = newLiteralType(TypeFlagsNumberLiteral, value, nullptr);
		}
		return nanType;
	}
	uint64_t bits = std::bit_cast<uint64_t>(value.v);
	Type* t = numberLiteralTypes[bits];
	if (t == nullptr) {
		t = newLiteralType(TypeFlagsNumberLiteral, value, nullptr);
		numberLiteralTypes[bits] = t;
	}
	return t;
}

Type* Checker::getBigIntLiteralType(const PseudoBigInt& value) {
	std::string key = value.string();
	Type* t = bigintLiteralTypes[key];
	if (t == nullptr) {
		t = newLiteralType(TypeFlagsBigIntLiteral, value, nullptr);
		bigintLiteralTypes[key] = t;
	}
	return t;
}

// text is a valid bigint string excluding a trailing `n`, but including a possible prefix `-`.
// Use `isValidBigIntString(text, roundTripOnly)` before calling this function.
Type* Checker::parseBigIntLiteralType(const std::string& text) {
	return getBigIntLiteralType(parseValidBigInt(text));
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

Type* Checker::getEnumLiteralType(const LiteralValue& value, Symbol* enumSymbol, Symbol* symbol) {
	TypeFlags flags;
	if (std::holds_alternative<std::string>(value)) {
		flags = TypeFlagsEnumLiteral | TypeFlagsStringLiteral;
	} else if (std::holds_alternative<Number>(value)) {
		flags = TypeFlagsEnumLiteral | TypeFlagsNumberLiteral;
		// NaN is not a usable map key (NaN != NaN), so cache NaN enum types
		// separately by enum symbol.
		if (std::get<Number>(value).isNaN()) {
			Type* t = enumNaNLiteralTypes[enumSymbol];
			if (t == nullptr) {
				t = newLiteralType(flags, value, nullptr);
				t->symbol = symbol;
				enumNaNLiteralTypes[enumSymbol] = t;
			}
			return t;
		}
	} else {
		TSC_UNREACHABLE("Unhandled case in getEnumLiteralType");
	}
	EnumLiteralKey key{enumSymbol};
	if (const std::string* sv = std::get_if<std::string>(&value)) {
		key.value = *sv;
	} else if (const Number* nv = std::get_if<Number>(&value)) {
		key.value = *nv;
	} else {
		key.value = std::get<PseudoBigInt>(value);
	}
	Type* t = enumLiteralTypes[key];
	if (t == nullptr) {
		t = newLiteralType(flags, value, nullptr);
		t->symbol = symbol;
		enumLiteralTypes[key] = t;
	}
	return t;
}

// ---------------------------------------------------------------------------
// Unit types — checker.go:25830-25900
// ---------------------------------------------------------------------------

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

static bool isNeitherUnitTypeNorNever(Type* t) {
	return (t->flags & (TypeFlagsUnit | TypeFlagsNever)) == 0;
}

bool Checker::isUnitLikeType(Type* t) {
	// Intersections that reduce to 'never' (e.g. 'T & null' where 'T extends {}') are not unit types.
	t = getBaseConstraintOrType(t);
	// Scan intersections such that tagged literal types are considered unit types.
	if (t->flags & TypeFlagsIntersection) {
		return someType(t, isUnitType);
	}
	return isUnitType(t);
}

Type* Checker::extractUnitType(Type* t) {
	if (t->flags & TypeFlagsIntersection) {
		for (Type* u : t->types()) {
			if (isUnitType(u)) {
				return u;
			}
		}
	}
	return t;
}

// ---------------------------------------------------------------------------
// Base types of literals — checker.go:25900-26019
// ---------------------------------------------------------------------------

Type* Checker::getBaseTypeOfLiteralType(Type* t) {
	if (t->flags & TypeFlagsEnumLike) {
		return getBaseTypeOfEnumLikeType(t);
	}
	if (t->flags & (TypeFlagsStringLiteral | TypeFlagsTemplateLiteral | TypeFlagsStringMapping)) {
		return stringType;
	}
	if (t->flags & TypeFlagsNumberLiteral) {
		return numberType;
	}
	if (t->flags & TypeFlagsBigIntLiteral) {
		return bigintType;
	}
	if (t->flags & TypeFlagsBooleanLiteral) {
		return booleanType;
	}
	if (t->flags & TypeFlagsUnion) {
		return getBaseTypeOfLiteralTypeUnion(t);
	}
	return t;
}

// This like getBaseTypeOfLiteralType, but instead treats enum literals as strings/numbers instead
// of returning their enum base type (which depends on the types of other literals in the enum).
Type* Checker::getBaseTypeOfLiteralTypeForComparison(Type* t) {
	if (t->flags & (TypeFlagsStringLiteral | TypeFlagsTemplateLiteral | TypeFlagsStringMapping)) {
		return stringType;
	}
	if (t->flags & (TypeFlagsNumberLiteral | TypeFlagsEnum)) {
		return numberType;
	}
	if (t->flags & TypeFlagsBigIntLiteral) {
		return bigintType;
	}
	if (t->flags & TypeFlagsBooleanLiteral) {
		return booleanType;
	}
	if (t->flags & TypeFlagsUnion) {
		return mapType(t,
			[this](Type* u) { return getBaseTypeOfLiteralTypeForComparison(u); });
	}
	return t;
}

Type* Checker::getBaseTypeOfEnumLikeType(Type* t) {
	if ((t->flags & TypeFlagsEnumLike) && (t->symbol->flags & SymbolFlagsEnumMember)) {
		return getDeclaredTypeOfSymbol(getParentOfSymbol(t->symbol));
	}
	return t;
}

Type* Checker::getBaseTypeOfLiteralTypeUnion(Type* t) {
	CachedTypeKey key{CachedTypeKind::LiteralUnionBaseType, t->id};
	if (auto it = cachedTypes.find(key); it != cachedTypes.end()) {
		return it->second;
	}
	Type* result = mapType(t, [this](Type* u) { return getBaseTypeOfLiteralType(u); });
	cachedTypes[key] = result;
	return result;
}

Type* Checker::getWidenedLiteralType(Type* t) {
	if ((t->flags & TypeFlagsEnumLike) && isFreshLiteralType(t)) {
		return getBaseTypeOfEnumLikeType(t);
	}
	if ((t->flags & TypeFlagsStringLiteral) && isFreshLiteralType(t)) {
		return stringType;
	}
	if ((t->flags & TypeFlagsNumberLiteral) && isFreshLiteralType(t)) {
		return numberType;
	}
	if ((t->flags & TypeFlagsBigIntLiteral) && isFreshLiteralType(t)) {
		return bigintType;
	}
	if ((t->flags & TypeFlagsBooleanLiteral) && isFreshLiteralType(t)) {
		return booleanType;
	}
	if (t->flags & TypeFlagsUnion) {
		return mapType(t, [this](Type* u) { return getWidenedLiteralType(u); });
	}
	return t;
}

Type* Checker::getWidenedUniqueESSymbolType(Type* t) {
	if (t->flags & TypeFlagsUniqueESSymbol) {
		return esSymbolType;
	}
	if (t->flags & TypeFlagsUnion) {
		return mapType(t, [this](Type* u) { return getWidenedUniqueESSymbolType(u); });
	}
	return t;
}

Type* Checker::getWidenedLiteralLikeTypeForContextualType(Type* t, Type* contextualType) {
	if (!isLiteralOfContextualType(t, contextualType)) {
		t = getWidenedUniqueESSymbolType(getWidenedLiteralType(t));
	}
	return getRegularTypeOfLiteralType(t);
}

bool Checker::isLiteralOfContextualType(Type* candidateType, Type* contextualType) {
	if (contextualType != nullptr) {
		if (contextualType->flags & TypeFlagsUnionOrIntersection) {
			return someType(contextualType, [this, candidateType](Type* t) {
				return isLiteralOfContextualType(candidateType, t);
			});
		}
		if (contextualType->flags & TypeFlagsInstantiableNonPrimitive) {
			// If the contextual type is a type variable constrained to a primitive type, consider
			// this a literal context for literals of that primitive type. For example, given a
			// type parameter 'T extends string', infer string literal types for T.
			Type* constraint = getBaseConstraintOfType(contextualType);
			if (constraint == nullptr) {
				constraint = unknownType;
			}
			return maybeTypeOfKind(constraint, TypeFlagsString) &&
					maybeTypeOfKind(candidateType, TypeFlagsStringLiteral) ||
				maybeTypeOfKind(constraint, TypeFlagsNumber) &&
					maybeTypeOfKind(candidateType, TypeFlagsNumberLiteral) ||
				maybeTypeOfKind(constraint, TypeFlagsBigInt) &&
					maybeTypeOfKind(candidateType, TypeFlagsBigIntLiteral) ||
				maybeTypeOfKind(constraint, TypeFlagsESSymbol) &&
					maybeTypeOfKind(candidateType, TypeFlagsUniqueESSymbol) ||
				isLiteralOfContextualType(candidateType, constraint);
		}
		// If the contextual type is a literal of a particular primitive type, we consider this a
		// literal context for all literals of that primitive type.
		return (contextualType->flags & (TypeFlagsStringLiteral | TypeFlagsIndex |
					TypeFlagsTemplateLiteral | TypeFlagsStringMapping)) &&
				maybeTypeOfKind(candidateType, TypeFlagsStringLiteral) ||
			(contextualType->flags & TypeFlagsNumberLiteral) &&
				maybeTypeOfKind(candidateType, TypeFlagsNumberLiteral) ||
			(contextualType->flags & TypeFlagsBigIntLiteral) &&
				maybeTypeOfKind(candidateType, TypeFlagsBigIntLiteral) ||
			(contextualType->flags & TypeFlagsBooleanLiteral) &&
				maybeTypeOfKind(candidateType, TypeFlagsBooleanLiteral) ||
			(contextualType->flags & TypeFlagsUniqueESSymbol) &&
				maybeTypeOfKind(candidateType, TypeFlagsUniqueESSymbol);
	}
	return false;
}

// ---------------------------------------------------------------------------
// === dep stubs — removed when owner slice lands ===
// ---------------------------------------------------------------------------

// owner: typeops slice (checker.go:26020-28654)

}  // namespace tsc::checker
