// === slice: ls-foundation ===
// lsutil/organizeimports.go — import/require classification, specifier and
// module-name comparers, and sort-order detection for organize-imports.

#include "internal/ls/lsutil/lsutil.h"

#include <cstring>
#include <limits>
#include <map>
#include <tuple>

#include "internal/ast/ast.h"
#include "internal/ls/lsutil/unicode_tables.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"

namespace tsc::ls::lsutil {

namespace {
// Forward declarations — the comparer helpers reference each other before
// their definitions, matching organizeimports.go's package-level ordering.
std::string naturalCollationKey(std::string_view s);
int compareOrganizeImportsUnicodeKeys(std::string_view a, std::string_view b, bool numeric);
int compareStringsNumeric(std::string_view a, std::string_view b);
bool isASCIIDigit(char ch);
int asciiDigitRunEnd(std::string_view s);
int compareNumericText(std::string_view a, std::string_view b);
int compareOrganizeImportsCase(std::string_view a, std::string_view b, OrganizeImportsCaseFirst caseFirst);
int compareOrganizeImportsCaseUpperFirst(std::string_view a, std::string_view b);
int compareOrganizeImportsNaturalStrings(std::string a, std::string b, bool caseSensitive);
int compareOrganizeImportsUnicodeStrings(std::string a, std::string b, bool ignoreCase,
	OrganizeImportsCaseFirst caseFirst, bool numeric, bool accents);



// core.Filter
template <class T, class Pred>
std::vector<T*> filter(const std::vector<T*>& v, Pred pred) {
	std::vector<T*> out;
	out.reserve(v.size());
	for (T* e : v) {
		if (pred(e)) out.push_back(e);
	}
	return out;
}

// cmp.Compare for ordered values.
template <class T>
int compareOrdered(const T& a, const T& b) {
	return a < b ? -1 : (a > b ? 1 : 0);
}

// strings.Compare — unsigned byte comparison.
int stringsCompare(std::string_view a, std::string_view b) {
	size_t n = std::min(a.size(), b.size());
	int c = n ? std::memcmp(a.data(), b.data(), n) : 0;
	if (c != 0) return c < 0 ? -1 : 1;
	return compareOrdered(a.size(), b.size());
}

// core.CompareBooleans — true > false.
int compareBooleans(bool a, bool b) {
	if (a && !b) return 1;
	if (!a && b) return -1;
	return 0;
}

// core.BinarySearchUniqueFunc — canonical def in core/types.h.

// stringutil.CompareStringsCaseInsensitiveEslintCompatible — compare.go:121.
// (A stringutil dep not yet ported; ported here because it is part of the
// organize-imports comparer matrix.) Case-insensitive comparison using
// toLowerCase() instead of toUpperCase() for ESLint compatibility.
int compareStringsCaseInsensitiveEslintCompatible(std::string_view a, std::string_view b) {
	if (a == b) {
		return 0;
	}
	return stringsCompare(detail::goToLower(a), detail::goToLower(b));
}

// Go type aliases inside this file.
using TypeOrderMap = std::map<OrganizeImportsTypeOrder, int>;
using TypeOrderComparerMap = std::map<OrganizeImportsTypeOrder, StringComparer>;

// getOrganizeImportsOrdinalStringComparer — organizeimports.go:74
StringComparer getOrganizeImportsOrdinalStringComparer(bool ignoreCase) {
	if (ignoreCase) {
		return [](std::string_view a, std::string_view b) {
			return compareStringsCaseInsensitiveEslintCompatible(a, b);
		};
	}
	return [](std::string_view a, std::string_view b) {
		return stringutil::CompareStringsCaseSensitive(a, b);
	};
}

// getOrganizeImportsNaturalStringComparer — organizeimports.go:80
StringComparer getOrganizeImportsNaturalStringComparer(bool caseSensitive) {
	return [caseSensitive](std::string_view a, std::string_view b) {
		return compareOrganizeImportsNaturalStrings(std::string(a), std::string(b), caseSensitive);
	};
}

// getOrganizeImportsUnicodeStringComparer — organizeimports.go:86
StringComparer getOrganizeImportsUnicodeStringComparer(bool ignoreCase, const UserPreferences& preferences) {
	OrganizeImportsCaseFirst caseFirst = preferences.OrganizeImportsCaseFirst;
	bool numeric = preferences.OrganizeImportsNumericCollation == Tristate::True;
	bool accents = preferences.OrganizeImportsAccentCollation != Tristate::False;

	return [ignoreCase, caseFirst, numeric, accents](std::string_view a, std::string_view b) {
		return compareOrganizeImportsUnicodeStrings(std::string(a), std::string(b), ignoreCase, caseFirst, numeric, accents);
	};
}

// compareOrganizeImportsNaturalStrings — organizeimports.go:98
int compareOrganizeImportsNaturalStrings(std::string a, std::string b, bool caseSensitive) {
	if (int cmp = compareStringsNumeric(naturalCollationKey(a), naturalCollationKey(b)); cmp != 0) {
		return cmp;
	}

	if (caseSensitive) {
		if (int cmp = compareOrganizeImportsCaseUpperFirst(a, b); cmp != 0) {
			return cmp;
		}
	}

	return stringsCompare(a, b);
}

// compareOrganizeImportsUnicodeStrings — organizeimports.go:110
int compareOrganizeImportsUnicodeStrings(std::string a, std::string b, bool ignoreCase,
	OrganizeImportsCaseFirst caseFirst, bool numeric, bool accents) {
	if (int cmp = compareOrganizeImportsUnicodeKeys(naturalCollationKey(a), naturalCollationKey(b), numeric); cmp != 0) {
		return cmp;
	}

	if (accents) {
		if (int cmp = compareOrganizeImportsUnicodeKeys(detail::goToLower(a), detail::goToLower(b), numeric); cmp != 0) {
			return cmp;
		}
	}

	if (!ignoreCase) {
		if (int cmp = compareOrganizeImportsCase(a, b, caseFirst); cmp != 0) {
			return cmp;
		}
	}

	return stringsCompare(a, b);
}

// naturalCollationKey — organizeimports.go:134.
// strings.ToLower(removeDiacritics(s)): removing Mn marks first and
// lowercasing the survivors is equivalent per-rune to lowercasing while
// dropping Mn.
std::string naturalCollationKey(std::string_view s) {
	std::u32string runes = detail::nfd(detail::toRunes(s));
	std::string out;
	char buf[8];
	for (char32_t r : runes) {
		if (!detail::isMnRune(r)) {
			out.append(buf, static_cast<size_t>(
				encodeUtf8Rune(detail::goSimpleLower(r), buf)));
		}
	}
	return out;
}

// compareOrganizeImportsUnicodeKeys — organizeimports.go:145
int compareOrganizeImportsUnicodeKeys(std::string_view a, std::string_view b, bool numeric) {
	if (numeric) {
		return compareStringsNumeric(a, b);
	}
	return stringsCompare(a, b);
}

// compareStringsNumeric — organizeimports.go:152. Operates on UTF-8 byte
// strings like Go (byte indices, byte lengths, utf8.DecodeRuneInString).
int compareStringsNumeric(std::string_view a, std::string_view b) {
	while (!a.empty() && !b.empty()) {
		if (isASCIIDigit(a[0]) && isASCIIDigit(b[0])) {
			size_t aRunEnd = asciiDigitRunEnd(a);
			size_t bRunEnd = asciiDigitRunEnd(b);

			if (int cmp = compareNumericText(a.substr(0, aRunEnd), b.substr(0, bRunEnd)); cmp != 0) {
				return cmp;
			}

			a = a.substr(aRunEnd);
			b = b.substr(bRunEnd);
			continue;
		}

		int aSize = 0, bSize = 0;
		char32_t aRune = decodeUtf8Rune(a, &aSize);
		char32_t bRune = decodeUtf8Rune(b, &bSize);
		if (aRune != bRune) {
			return compareOrdered(aRune, bRune);
		}

		a = a.substr(static_cast<size_t>(aSize));
		b = b.substr(static_cast<size_t>(bSize));
	}

	return compareOrdered(a.size(), b.size());
}

// isASCIIDigit — organizeimports.go:177 (byte-level, like Go).
bool isASCIIDigit(char ch) {
	return ch >= '0' && ch <= '9';
}

// asciiDigitRunEnd — organizeimports.go:181 (byte index, like Go).
int asciiDigitRunEnd(std::string_view s) {
	size_t i = 0;
	while (i < s.size() && isASCIIDigit(s[i])) {
		i++;
	}
	return i;
}

// compareNumericText — organizeimports.go:189
int compareNumericText(std::string_view a, std::string_view b) {
	// strings.TrimLeft(a, "0")
	auto trimZeros = [](std::string_view s) -> std::string_view {
		size_t i = 0;
		while (i < s.size() && s[i] == '0') i++;
		return s.substr(i);
	};
	std::string_view aDigits = trimZeros(a);
	std::string_view bDigits = trimZeros(b);
	if (aDigits.empty()) {
		aDigits = "0";
	}
	if (bDigits.empty()) {
		bDigits = "0";
	}

	if (aDigits.size() != bDigits.size()) {
		return compareOrdered(aDigits.size(), bDigits.size());
	}
	if (int c = stringsCompare(aDigits, bDigits); c != 0) {
		return c;
	}
	return stringsCompare(a, b);
}

// compareOrganizeImportsCaseUpperFirst — organizeimports.go:212
int compareOrganizeImportsCaseUpperFirst(std::string_view a, std::string_view b) {
	return compareOrganizeImportsCase(a, b, OrganizeImportsCaseFirstUpper);
}

// compareOrganizeImportsCase — organizeimports.go:216
int compareOrganizeImportsCase(std::string_view a, std::string_view b, OrganizeImportsCaseFirst caseFirst) {
	std::u32string aRunes = detail::toRunes(a);
	std::u32string bRunes = detail::toRunes(b);
	size_t minLen = std::min(aRunes.size(), bRunes.size());

	for (size_t i = 0; i < minLen; i++) {
		bool aUpper = detail::isUpperRune(aRunes[i]);
		bool bUpper = detail::isUpperRune(bRunes[i]);
		if (aUpper != bUpper) {
			switch (caseFirst) {
			case OrganizeImportsCaseFirstUpper:
				if (aUpper) {
					return -1;
				}
				return 1;
			case OrganizeImportsCaseFirstLower:
				if (!aUpper) {
					return -1;
				}
				return 1;
			default:
				if (aUpper) {
					return 1;
				}
				return -1;
			}
		}
	}

	return compareOrdered(aRunes.size(), bRunes.size());
}

// getOrganizeImportsPresetStringComparer — organizeimports.go:244
StringComparer getOrganizeImportsPresetStringComparer(OrganizeImportsSort sort) {
	switch (sort) {
	case OrganizeImportsSortOrdinalIgnoreCase:
		return getOrganizeImportsOrdinalStringComparer(true);
	case OrganizeImportsSortNatural:
		return getOrganizeImportsNaturalStringComparer(true);
	case OrganizeImportsSortNaturalIgnoreCase:
		return getOrganizeImportsNaturalStringComparer(false);
	default:
		return getOrganizeImportsOrdinalStringComparer(false);
	}
}

// getOrganizeImportsStringComparer — organizeimports.go:256
StringComparer getOrganizeImportsStringComparer(const UserPreferences& preferences, bool ignoreCase) {
	if (preferences.OrganizeImportsSort != OrganizeImportsSortAuto) {
		return getOrganizeImportsPresetStringComparer(preferences.OrganizeImportsSort);
	}
	if (preferences.OrganizeImportsCollation == OrganizeImportsCollationUnicode) {
		return getOrganizeImportsUnicodeStringComparer(ignoreCase, preferences);
	}
	return getOrganizeImportsOrdinalStringComparer(ignoreCase);
}

// getModuleSpecifierExpression — organizeimports.go:266
Node* getModuleSpecifierExpression(Node* declaration) {
	switch (declaration->kind) {
	case Kind::ImportEqualsDeclaration: {
		ImportEqualsDeclaration* importEquals = declaration->as<ImportEqualsDeclaration>();
		if (importEquals->ModuleReference->kind == Kind::ExternalModuleReference) {
			return importEquals->ModuleReference->expression();
		}
		return nullptr;
	}
	case Kind::ImportDeclaration:
		return declaration->moduleSpecifier();
	case Kind::VariableStatement: {
		std::vector<Node*>& declarations =
			declaration->as<VariableStatement>()->DeclarationList
				->as<VariableDeclarationList>()->Declarations->nodes;
		if (!declarations.empty()) {
			Node* initializer = declarations[0]->initializer();
			if (initializer != nullptr && initializer->kind == Kind::CallExpression) {
				CallExpression* callExpr = initializer->as<CallExpression>();
				if (!callExpr->Arguments->nodes.empty()) {
					return callExpr->Arguments->nodes[0];
				}
			}
		}
		return nullptr;
	}
	default:
		return nullptr;
	}
}

// getImportKindOrder — organizeimports.go:329
// Sort order for different import kinds:
// 1. Side-effect imports
// 2. Type-only imports
// 3. Namespace imports
// 4. Default imports
// 5. Named imports
// 6. ImportEqualsDeclarations
// 7. Require variable statements
enum {
	importKindOrderSideEffect = 0,
	importKindOrderTypeOnly = 1,
	importKindOrderNamespace = 2,
	importKindOrderDefault = 3,
	importKindOrderNamed = 4,
	importKindOrderImportEquals = 5,
	importKindOrderRequire = 6,
	importKindOrderUnknown = 7,
};

int getImportKindOrder(Node* s1) {
	switch (s1->kind) {
	case Kind::ImportDeclaration: {
		ImportDeclaration* importDecl = s1->as<ImportDeclaration>();
		if (importDecl->ImportClause == nullptr) {
			return importKindOrderSideEffect;
		}
		ImportClause* importClause = importDecl->ImportClause->as<ImportClause>();
		if (importClause->isTypeOnly()) {
			return importKindOrderTypeOnly;
		}
		if (importClause->NamedBindings != nullptr && importClause->NamedBindings->kind == Kind::NamespaceImport) {
			return importKindOrderNamespace;
		}
		if (importClause->name != nullptr) {
			return importKindOrderDefault;
		}
		return importKindOrderNamed;
	}
	case Kind::ImportEqualsDeclaration:
		return importKindOrderImportEquals;
	case Kind::VariableStatement:
		return importKindOrderRequire;
	default:
		return importKindOrderUnknown;
	}
}

// compareImportKind — organizeimports.go:325
int compareImportKind(Node* s1, Node* s2) {
	return compareOrdered(getImportKindOrder(s1), getImportKindOrder(s2));
}

// compareImportOrExportSpecifiers — organizeimports.go:385
int compareImportOrExportSpecifiers(Node* s1, Node* s2, const StringComparer& comparer,
	const UserPreferences& preferences) {
	OrganizeImportsTypeOrder typeOrder = preferences.OrganizeImportsTypeOrder;

	std::string s1Name(s1->name()->text());
	std::string s2Name(s2->name()->text());

	switch (typeOrder) {
	case OrganizeImportsTypeOrderFirst:
		if (int cmp = compareBooleans(s2->isTypeOnly(), s1->isTypeOnly()); cmp != 0) {
			return cmp;
		}
		return comparer(s1Name, s2Name);
	case OrganizeImportsTypeOrderInline:
		return comparer(s1Name, s2Name);
	default: // OrganizeImportsTypeOrderLast
		if (int cmp = compareBooleans(s1->isTypeOnly(), s2->isTypeOnly()); cmp != 0) {
			return cmp;
		}
		return comparer(s1Name, s2Name);
	}
}

// namedImportSortResult — organizeimports.go:449
struct namedImportSortResult {
	StringComparer namedImportComparer;
	OrganizeImportsTypeOrder typeOrder;
	bool isSorted;
};

// caseSensitivityDetectionResult — organizeimports.go:594
struct caseSensitivityDetectionResult {
	StringComparer comparer;
	bool isSorted;
};

// measureSortedness — organizeimports.go:653. The comparer arrives as a
// lambda; take it generically (std::function params can't deduce).
template <class T, class F>
int measureSortedness(const std::vector<T>& arr, const F& comparer) {
	int i = 0;
	for (size_t j = 0; j + 1 < arr.size(); j++) {
		if (comparer(arr[j], arr[j + 1]) > 0) {
			i++;
		}
	}
	return i;
}

// detectCaseSensitivityBySort — organizeimports.go:625
caseSensitivityDetectionResult detectCaseSensitivityBySort(
	const std::vector<std::vector<std::string>>& originalGroups,
	const std::vector<StringComparer>& comparersToTest) {
	StringComparer bestComparer;
	int bestDiff = std::numeric_limits<int>::max();

	for (const StringComparer& curComparer : comparersToTest) {
		int diffOfCurrentComparer = 0;

		for (auto& listToSort : originalGroups) {
			if (listToSort.size() <= 1) {
				continue;
			}
			int diff = measureSortedness(listToSort,
				[&curComparer](const std::string& a, const std::string& b) {
					return curComparer(a, b);
				});
			diffOfCurrentComparer += diff;
		}

		if (diffOfCurrentComparer < bestDiff) {
			bestDiff = diffOfCurrentComparer;
			bestComparer = curComparer;
		}
	}

	if (bestComparer == nullptr && !comparersToTest.empty()) {
		bestComparer = comparersToTest[0];
	}

	return {bestComparer, bestDiff == 0};
}

// detectNamedImportOrganizationBySort — organizeimports.go:468
namedImportSortResult* detectNamedImportOrganizationBySort(
	const std::vector<Node*>& originalGroups,
	const std::vector<StringComparer>& comparersToTest,
	const std::vector<OrganizeImportsTypeOrder>& typesToTest,
	namedImportSortResult& result) {
	bool bothNamedImports = false;
	std::vector<Node*> importDeclsWithNamed;

	for (Node* imp : originalGroups) {
		if (imp->as<ImportDeclaration>()->ImportClause == nullptr) {
			continue;
		}
		ImportClause* clause = imp->as<ImportDeclaration>()->ImportClause->as<ImportClause>();
		if (clause->NamedBindings == nullptr || clause->NamedBindings->kind != Kind::NamedImports) {
			continue;
		}
		NamedImports* namedImports = clause->NamedBindings->as<NamedImports>();
		if (namedImports->Elements->nodes.empty()) {
			continue;
		}

		if (!bothNamedImports) {
			bool hasTypeOnly = false;
			bool hasRegular = false;
			for (Node* elem : namedImports->Elements->nodes) {
				if (elem->isTypeOnly()) {
					hasTypeOnly = true;
				} else {
					hasRegular = true;
				}
			}
			if (hasTypeOnly && hasRegular) {
				bothNamedImports = true;
			}
		}

		importDeclsWithNamed.push_back(imp);
	}

	if (importDeclsWithNamed.empty()) {
		return nullptr;
	}

	std::vector<std::vector<Node*>> namedImportsByDecl;
	namedImportsByDecl.reserve(importDeclsWithNamed.size());
	for (Node* imp : importDeclsWithNamed) {
		ImportClause* clause = imp->as<ImportDeclaration>()->ImportClause->as<ImportClause>();
		NamedImports* namedImports = clause->NamedBindings->as<NamedImports>();
		namedImportsByDecl.push_back(namedImports->Elements->nodes);
	}

	if (!bothNamedImports || typesToTest.empty()) {
		std::vector<std::vector<std::string>> namesList(namedImportsByDecl.size());
		for (size_t i = 0; i < namedImportsByDecl.size(); i++) {
			std::vector<std::string>& names = namesList[i];
			names.reserve(namedImportsByDecl[i].size());
			for (Node* imp : namedImportsByDecl[i]) {
				names.push_back(std::string(imp->name()->text()));
			}
		}
		caseSensitivityDetectionResult sortState = detectCaseSensitivityBySort(namesList, comparersToTest);
		OrganizeImportsTypeOrder typeOrder = OrganizeImportsTypeOrderLast;
		if (typesToTest.size() == 1) {
			typeOrder = typesToTest[0];
		}
		result = {sortState.comparer, typeOrder, sortState.isSorted};
		return &result;
	}

	if (comparersToTest.empty()) {
		// Go indexes comparersToTest[0] unconditionally — index-out-of-range
		// panic.
		TSC_UNREACHABLE("organizeimports.go:561 — index out of range [0] with length 0");
	}
	TypeOrderMap bestDiff = {
		{OrganizeImportsTypeOrderFirst, std::numeric_limits<int>::max()},
		{OrganizeImportsTypeOrderLast, std::numeric_limits<int>::max()},
		{OrganizeImportsTypeOrderInline, std::numeric_limits<int>::max()},
	};
	TypeOrderComparerMap bestComparer = {
		{OrganizeImportsTypeOrderFirst, comparersToTest[0]},
		{OrganizeImportsTypeOrderLast, comparersToTest[0]},
		{OrganizeImportsTypeOrderInline, comparersToTest[0]},
	};

	for (const StringComparer& curComparer : comparersToTest) {
		TypeOrderMap currDiff = {
			{OrganizeImportsTypeOrderFirst, 0},
			{OrganizeImportsTypeOrderLast, 0},
			{OrganizeImportsTypeOrderInline, 0},
		};

		for (auto& importDecl : namedImportsByDecl) {
			for (OrganizeImportsTypeOrder typeOrder : typesToTest) {
				UserPreferences prefs{}; // Go: UserPreferences{OrganizeImportsTypeOrder: typeOrder}
				prefs.OrganizeImportsTypeOrder = typeOrder;
				int diff = measureSortedness(importDecl,
					[&curComparer, &prefs](Node* const& n1, Node* const& n2) {
						return compareImportOrExportSpecifiers(n1, n2, curComparer, prefs);
					});
				currDiff[typeOrder] = currDiff[typeOrder] + diff;
			}
		}

		for (OrganizeImportsTypeOrder typeOrder : typesToTest) {
			if (currDiff[typeOrder] < bestDiff[typeOrder]) {
				bestDiff[typeOrder] = currDiff[typeOrder];
				bestComparer[typeOrder] = curComparer;
			}
		}
	}

	for (OrganizeImportsTypeOrder bestTypeOrder : typesToTest) {
		bool isBest = true;
		for (OrganizeImportsTypeOrder testTypeOrder : typesToTest) {
			if (bestDiff[testTypeOrder] < bestDiff[bestTypeOrder]) {
				isBest = false;
				break;
			}
		}
		if (isBest) {
			result = {bestComparer[bestTypeOrder], bestTypeOrder, bestDiff[bestTypeOrder] == 0};
			return &result;
		}
	}

	result = {bestComparer[OrganizeImportsTypeOrderLast], OrganizeImportsTypeOrderLast,
			  bestDiff[OrganizeImportsTypeOrderLast] == 0};
	return &result;
}

// getComparers — organizeimports.go:438
std::vector<StringComparer> getComparers(const UserPreferences& preferences) {
	if (preferences.OrganizeImportsSort != OrganizeImportsSortAuto ||
		preferences.OrganizeImportsIgnoreCase != Tristate::Unknown) {
		bool ignoreCase = false;
		if (preferences.OrganizeImportsIgnoreCase != Tristate::Unknown) {
			ignoreCase = preferences.OrganizeImportsIgnoreCase == Tristate::True;
		}
		return {getOrganizeImportsStringComparer(preferences, ignoreCase)};
	}
	return {
		getOrganizeImportsStringComparer(preferences, true),
		getOrganizeImportsStringComparer(preferences, false),
	};
}

} // namespace

// test access (utilities_test.go) — the Go test is in-package and calls the
// unexported comparer factory directly.
StringComparer TestGetOrganizeImportsPresetStringComparer(OrganizeImportsSort sort) {
	return getOrganizeImportsPresetStringComparer(sort);
}

// FilterImportDeclarations — organizeimports.go:17
std::vector<Node*> FilterImportDeclarations(const std::vector<Node*>& statements) {
	return filter(statements, [](Node* stmt) {
		return stmt->kind == Kind::ImportDeclaration;
	});
}

// GetDetectionLists — organizeimports.go:24. Returns the lists of comparers
// and type orders to test for organize imports detection.
std::pair<std::vector<StringComparer>, std::vector<OrganizeImportsTypeOrder>>
GetDetectionLists(const UserPreferences& preferences) {
	std::vector<StringComparer> comparersToTest;
	std::vector<OrganizeImportsTypeOrder> typeOrdersToTest;

	if (preferences.OrganizeImportsSort != OrganizeImportsSortAuto) {
		comparersToTest = {getOrganizeImportsPresetStringComparer(preferences.OrganizeImportsSort)};
	} else if (preferences.OrganizeImportsIgnoreCase != Tristate::Unknown) {
		comparersToTest = {getOrganizeImportsStringComparer(
			preferences, preferences.OrganizeImportsIgnoreCase == Tristate::True)};
	} else {
		comparersToTest = {
			getOrganizeImportsStringComparer(preferences, true),
			getOrganizeImportsStringComparer(preferences, false),
		};
	}

	if (preferences.OrganizeImportsTypeOrder != OrganizeImportsTypeOrderAuto) {
		typeOrdersToTest = {preferences.OrganizeImportsTypeOrder};
	} else {
		typeOrdersToTest = {
			OrganizeImportsTypeOrderLast,
			OrganizeImportsTypeOrderInline,
			OrganizeImportsTypeOrderFirst,
		};
	}

	return {comparersToTest, typeOrdersToTest};
}

// ResolveOrganizeImportsSort — organizeimports.go:50
OrganizeImportsSort ResolveOrganizeImportsSort(const UserPreferences& preferences) {
	if (preferences.OrganizeImportsSort != OrganizeImportsSortAuto) {
		return preferences.OrganizeImportsSort;
	}

	if (preferences.OrganizeImportsCollation == OrganizeImportsCollationUnicode) {
		switch (preferences.OrganizeImportsIgnoreCase) {
		case Tristate::True:
			return OrganizeImportsSortNaturalIgnoreCase;
		case Tristate::False:
			return OrganizeImportsSortNatural;
		default:
			return OrganizeImportsSortAuto;
		}
	}

	switch (preferences.OrganizeImportsIgnoreCase) {
	case Tristate::True:
		return OrganizeImportsSortOrdinalIgnoreCase;
	case Tristate::False:
		return OrganizeImportsSortOrdinal;
	default:
		return OrganizeImportsSortAuto;
	}
}

// GetExternalModuleName — organizeimports.go:298. Returns the module name
// from a module specifier expression.
std::string GetExternalModuleName(Node* specifier) {
	if (specifier != nullptr && isStringLiteralLike(specifier)) {
		return std::string(specifier->text());
	}
	return "";
}

// CompareModuleSpecifiers — organizeimports.go:306
int CompareModuleSpecifiers(Node* m1, Node* m2, const StringComparer& comparer) {
	std::string name1 = GetExternalModuleName(m1);
	std::string name2 = GetExternalModuleName(m2);
	if (int cmp = compareBooleans(name1.empty(), name2.empty()); cmp != 0) {
		return cmp;
	}
	if (int cmp = compareBooleans(tspath::isExternalModuleNameRelative(name1),
								  tspath::isExternalModuleNameRelative(name2)); cmp != 0) {
		return cmp;
	}
	return comparer(name1, name2);
}

// CompareImportsOrRequireStatements — organizeimports.go:370
int CompareImportsOrRequireStatements(Node* s1, Node* s2, const StringComparer& comparer) {
	if (int cmp = CompareModuleSpecifiers(getModuleSpecifierExpression(s1),
										  getModuleSpecifierExpression(s2), comparer); cmp != 0) {
		return cmp;
	}
	return compareImportKind(s1, s2);
}

// GetNamedImportSpecifierComparer — organizeimports.go:400
NodeComparer GetNamedImportSpecifierComparer(const UserPreferences& preferences, StringComparer comparer) {
	if (comparer == nullptr) {
		bool ignoreCase = false;
		if (preferences.OrganizeImportsIgnoreCase != Tristate::Unknown) {
			ignoreCase = preferences.OrganizeImportsIgnoreCase == Tristate::True;
		}
		comparer = getOrganizeImportsStringComparer(preferences, ignoreCase);
	}
	return [comparer, preferences](Node* s1, Node* s2) {
		return compareImportOrExportSpecifiers(s1, s2, comparer, preferences);
	};
}

// GetImportSpecifierInsertionIndex — organizeimports.go:414
int GetImportSpecifierInsertionIndex(const std::vector<Node*>& sortedImports, Node* newImport,
	const NodeComparer& comparer) {
	return binarySearchUniqueFunc(sortedImports,
		[&comparer, newImport](int mid, Node* const& value) {
			return comparer(value, newImport);
		}).first;
}

// GetImportDeclarationInsertIndex — organizeimports.go:421
int GetImportDeclarationInsertIndex(const std::vector<Node*>& sortedImports, Node* newImport,
	const NodeComparer& comparer) {
	return binarySearchUniqueFunc(sortedImports,
		[&comparer, newImport](int mid, Node* const& value) {
			return comparer(value, newImport);
		}).first;
}

// GetOrganizeImportsStringComparerWithDetection — organizeimports.go:428
std::pair<StringComparer, bool> GetOrganizeImportsStringComparerWithDetection(
	const std::vector<Node*>& originalImportDecls, const UserPreferences& preferences) {
	auto [result, sorted] = DetectModuleSpecifierCaseBySort({originalImportDecls}, getComparers(preferences));
	return {result, sorted};
}

// DetectNamedImportOrganizationBySort — organizeimports.go:458. Detects the
// order of named imports throughout the file by considering the named
// imports in each statement as a group.
std::tuple<StringComparer, OrganizeImportsTypeOrder, bool> DetectNamedImportOrganizationBySort(
	const std::vector<Node*>& originalGroups,
	const std::vector<StringComparer>& comparersToTest,
	const std::vector<OrganizeImportsTypeOrder>& typesToTest) {
	namedImportSortResult result;
	if (detectNamedImportOrganizationBySort(originalGroups, comparersToTest, typesToTest, result) == nullptr) {
		return {nullptr, OrganizeImportsTypeOrderLast, false};
	}
	return {result.namedImportComparer, result.typeOrder, true};
}

// DetectModuleSpecifierCaseBySort — organizeimports.go:600. Detects the
// order of module specifiers based on import statements throughout the
// module/file.
std::pair<StringComparer, bool> DetectModuleSpecifierCaseBySort(
	const std::vector<std::vector<Node*>>& importDeclsByGroup,
	const std::vector<StringComparer>& comparersToTest) {
	std::vector<std::vector<std::string>> moduleSpecifiersByGroup;
	moduleSpecifiersByGroup.reserve(importDeclsByGroup.size());
	for (auto& importGroup : importDeclsByGroup) {
		std::vector<std::string> moduleNames;
		moduleNames.reserve(importGroup.size());
		for (Node* decl : importGroup) {
			if (Node* expr = getModuleSpecifierExpression(decl); expr != nullptr) {
				moduleNames.push_back(GetExternalModuleName(expr));
			} else {
				moduleNames.push_back("");
			}
		}
		moduleSpecifiersByGroup.push_back(std::move(moduleNames));
	}
	caseSensitivityDetectionResult result = detectCaseSensitivityBySort(moduleSpecifiersByGroup, comparersToTest);
	return {result.comparer, result.isSorted};
}

// GetNamedImportSpecifierComparerWithDetection — organizeimports.go:662.
// Returns a specifier comparer based on detecting the existing sort order
// within a single import statement.
std::pair<NodeComparer, Tristate> GetNamedImportSpecifierComparerWithDetection(
	Node* importDecl, SourceFile* sourceFile, const UserPreferences& preferences) {
	auto [comparersToTest, typeOrdersToTest] = GetDetectionLists(preferences);

	Node* importStmt = nullptr;
	if (importDecl->kind == Kind::ImportDeclaration) {
		importStmt = importDecl;
	}

	NodeComparer specifierComparer = GetNamedImportSpecifierComparer(preferences, comparersToTest[0]);
	Tristate isSorted = Tristate::Unknown;

	if ((ResolveOrganizeImportsSort(preferences) == OrganizeImportsSortAuto ||
		 preferences.OrganizeImportsTypeOrder == OrganizeImportsTypeOrderAuto) &&
		importStmt != nullptr) {
		namedImportSortResult detectFromDecl;
		if (detectNamedImportOrganizationBySort({importStmt}, comparersToTest, typeOrdersToTest, detectFromDecl) != nullptr) {
			isSorted = detectFromDecl.isSorted ? Tristate::True : Tristate::False;
			// Go: UserPreferences{OrganizeImportsTypeOrder: detectFromDecl.typeOrder}
			UserPreferences prefs{};
			prefs.OrganizeImportsTypeOrder = detectFromDecl.typeOrder;
			specifierComparer = GetNamedImportSpecifierComparer(prefs, detectFromDecl.namedImportComparer);
		} else if (sourceFile != nullptr) {
			std::vector<Node*> allImports = FilterImportDeclarations(sourceFile->Statements->nodes);
			namedImportSortResult detectFromFile;
			if (detectNamedImportOrganizationBySort(allImports, comparersToTest, typeOrdersToTest, detectFromFile) != nullptr) {
				isSorted = detectFromFile.isSorted ? Tristate::True : Tristate::False;
				UserPreferences prefs{};
				prefs.OrganizeImportsTypeOrder = detectFromFile.typeOrder;
				specifierComparer = GetNamedImportSpecifierComparer(prefs, detectFromFile.namedImportComparer);
			}
		}
	}

	return {specifierComparer, isSorted};
}

} // namespace tsc::ls::lsutil
