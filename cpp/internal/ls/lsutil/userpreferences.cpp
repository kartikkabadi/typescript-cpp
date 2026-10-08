// === slice: ls-foundation ===
// lsutil/userpreferences.go — the UserPreferences struct plus the
// reflection-based raw/config parsing machinery. Reflection is compiled
// into a static fieldInfo table preserving collectFieldInfos order
// (declaration order; untagged embedded structs are recursed at their
// declaration position).

#include "internal/ls/lsutil/lsutil.h"

#include <cstdlib>
#include <functional>
#include <optional>
#include <utility>

#include "internal/json/json.h"
#include "internal/modulespecifiers/types.h"
#include "internal/stringutil/stringutil.h"
#include "internal/ls/lsutil/unicode_tables.h"
#include "internal/vfs/vfsmatch/vfsmatch.h"

namespace tsc::ls::lsutil {

namespace {

// --- typeParsers (userpreferences.go:298) ---
// Each Go type-parser always returns a value; it is invoked for every
// non-nil input and the result is unconditionally assigned. The generic
// parsers (bool/int/string/slice) only assign when the JSON type matches —
// they return nullopt for "do not set" so they can share the same apply
// plumbing as the typed parsers.

Tristate parseTristate(const JsonAny& val) {
	if (val.is(JsonAny::K::Bool)) {
		return val.b ? Tristate::True : Tristate::False;
	}
	return Tristate::Unknown;
}

QuotePreference parseQuotePreference(const JsonAny& val) {
	if (val.is(JsonAny::K::String)) {
		std::string s = detail::goToLower(val.s);
		if (s == "auto") return QuotePreferenceAuto;
		if (s == "double") return QuotePreferenceDouble;
		if (s == "single") return QuotePreferenceSingle;
	}
	return QuotePreferenceUnknown;
}

JsxAttributeCompletionStyle parseJsxAttributeCompletionStyle(const JsonAny& val) {
	if (val.is(JsonAny::K::String)) {
		std::string s = detail::goToLower(val.s);
		if (s == "braces") return JsxAttributeCompletionStyleBraces;
		if (s == "none") return JsxAttributeCompletionStyleNone;
	}
	return JsxAttributeCompletionStyleAuto;
}

IncludeInlayParameterNameHints parseIncludeInlayParameterNameHints(const JsonAny& val) {
	if (val.is(JsonAny::K::String)) {
		// NOTE: Go does not lowercase here — exact match only.
		if (val.s == "all") return IncludeInlayParameterNameHintsAll;
		if (val.s == "literals") return IncludeInlayParameterNameHintsLiterals;
	}
	return IncludeInlayParameterNameHintsNone;
}

OrganizeImportsSort parseOrganizeImportsSort(const JsonAny& val) {
	if (val.is(JsonAny::K::String)) {
		std::string s = detail::goToLower(val.s);
		if (s == "ordinal") return OrganizeImportsSortOrdinal;
		if (s == "ordinalignorecase") return OrganizeImportsSortOrdinalIgnoreCase;
		if (s == "natural") return OrganizeImportsSortNatural;
		if (s == "naturalignorecase") return OrganizeImportsSortNaturalIgnoreCase;
	}
	return OrganizeImportsSortAuto;
}

OrganizeImportsCollation parseOrganizeImportsCollation(const JsonAny& val) {
	if (val.is(JsonAny::K::String) && detail::goToLower(val.s) == "unicode") {
		return OrganizeImportsCollationUnicode;
	}
	return OrganizeImportsCollationOrdinal;
}

OrganizeImportsCaseFirst parseOrganizeImportsCaseFirst(const JsonAny& val) {
	if (val.is(JsonAny::K::String)) {
		if (val.s == "lower") return OrganizeImportsCaseFirstLower;
		if (val.s == "upper") return OrganizeImportsCaseFirstUpper;
	}
	return OrganizeImportsCaseFirstFalse;
}

OrganizeImportsTypeOrder parseOrganizeImportsTypeOrder(const JsonAny& val) {
	if (val.is(JsonAny::K::String)) {
		if (val.s == "last") return OrganizeImportsTypeOrderLast;
		if (val.s == "inline") return OrganizeImportsTypeOrderInline;
		if (val.s == "first") return OrganizeImportsTypeOrderFirst;
	}
	return OrganizeImportsTypeOrderAuto;
}

tsc::modulespecifiers::ImportModuleSpecifierPreference parseImportModuleSpecifierPreference(const JsonAny& val) {
	if (val.is(JsonAny::K::String)) {
		std::string s = detail::goToLower(val.s);
		if (s == "project-relative") return tsc::modulespecifiers::ImportModuleSpecifierPreferenceProjectRelative;
		if (s == "relative") return tsc::modulespecifiers::ImportModuleSpecifierPreferenceRelative;
		if (s == "non-relative") return tsc::modulespecifiers::ImportModuleSpecifierPreferenceNonRelative;
	}
	return tsc::modulespecifiers::ImportModuleSpecifierPreferenceShortest;
}

tsc::modulespecifiers::ImportModuleSpecifierEndingPreference parseImportModuleSpecifierEndingPreference(const JsonAny& val) {
	if (val.is(JsonAny::K::String)) {
		std::string s = detail::goToLower(val.s);
		if (s == "minimal") return tsc::modulespecifiers::ImportModuleSpecifierEndingPreferenceMinimal;
		if (s == "index") return tsc::modulespecifiers::ImportModuleSpecifierEndingPreferenceIndex;
		if (s == "js") return tsc::modulespecifiers::ImportModuleSpecifierEndingPreferenceJs;
	}
	return tsc::modulespecifiers::ImportModuleSpecifierEndingPreferenceAuto;
}

// Generic Kind() parsers — only assign on a type match (nullopt = unchanged).
std::optional<bool> parseBool(const JsonAny& val) {
	if (val.is(JsonAny::K::Bool)) return val.b;
	return std::nullopt;
}
std::optional<int> parseInt(const JsonAny& val) {
	// reflect.Int case: int or float64 (int64(v) truncation).
	if (val.is(JsonAny::K::Int)) return static_cast<int>(val.i);
	if (val.is(JsonAny::K::Float)) return static_cast<int>(val.f);
	return std::nullopt;
}
std::optional<std::string> parseString(const JsonAny& val) {
	if (val.is(JsonAny::K::String)) return val.s;
	return std::nullopt;
}
std::optional<std::vector<std::string>> parseStringSlice(const JsonAny& val) {
	if (!val.is(JsonAny::K::Array)) return std::nullopt;
	std::vector<std::string> result;
	result.reserve(val.arr.size());
	for (auto& item : val.arr) {
		if (item.is(JsonAny::K::String)) result.push_back(item.s);
	}
	return result;
}

// --- typeSerializers (userpreferences.go:447) ---

JsonAny serializeTristate(Tristate v) {
	switch (v) {
	case Tristate::True: return JsonAny(true);
	case Tristate::False: return JsonAny(false);
	default: return JsonAny();
	}
}

JsonAny serializeOrganizeImportsSort(OrganizeImportsSort v) {
	switch (v) {
	case OrganizeImportsSortOrdinal: return JsonAny("ordinal");
	case OrganizeImportsSortOrdinalIgnoreCase: return JsonAny("ordinalIgnoreCase");
	case OrganizeImportsSortNatural: return JsonAny("natural");
	case OrganizeImportsSortNaturalIgnoreCase: return JsonAny("naturalIgnoreCase");
	default: return JsonAny("auto");
	}
}

JsonAny serializeOrganizeImportsCollation(OrganizeImportsCollation v) {
	return JsonAny(v == OrganizeImportsCollationUnicode ? "unicode" : "ordinal");
}

JsonAny serializeOrganizeImportsCaseFirst(OrganizeImportsCaseFirst v) {
	switch (v) {
	case OrganizeImportsCaseFirstLower: return JsonAny("lower");
	case OrganizeImportsCaseFirstUpper: return JsonAny("upper");
	default: return JsonAny("default");
	}
}

JsonAny serializeOrganizeImportsTypeOrder(OrganizeImportsTypeOrder v) {
	switch (v) {
	case OrganizeImportsTypeOrderLast: return JsonAny("last");
	case OrganizeImportsTypeOrderInline: return JsonAny("inline");
	case OrganizeImportsTypeOrderFirst: return JsonAny("first");
	default: return JsonAny("auto");
	}
}

// These enums distinguish an unset zero value (e.g. "") from their effective
// default (e.g. "auto"): the parser promotes unset/unknown input to the
// non-zero default. Plain string serialization would therefore write "" for
// an unset field and the parser would read it back as the non-zero default,
// breaking round-tripping. Mirror the core.Tristate serializer above and omit
// the unset value (return nil) so it decodes back to the zero value. (Enums
// whose default already is their zero value, like the OrganizeImports* ones,
// round-trip without this.)
JsonAny serializeJsxAttributeCompletionStyle(const JsxAttributeCompletionStyle& v) {
	// TODO: make consistent with other enums (see note above). Unlike the
	// module-specifier enums, the consumer in completions.go distinguishes
	// JsxAttributeCompletionStyleUnknown from ...Auto, so converting this one
	// requires updating that consumer to treat the zero value as "auto".
	if (v != JsxAttributeCompletionStyleUnknown) return JsonAny(v);
	return JsonAny();
}

JsonAny serializeImportModuleSpecifierPreference(const tsc::modulespecifiers::ImportModuleSpecifierPreference& v) {
	// TODO: make consistent with other enums (see note above): have the parser
	// return the zero value (None) as its fallback and drop this serializer.
	if (!v.empty()) return JsonAny(v);
	return JsonAny();
}

JsonAny serializeImportModuleSpecifierEndingPreference(const tsc::modulespecifiers::ImportModuleSpecifierEndingPreference& v) {
	// TODO: make consistent with other enums (see note above): have the parser
	// return the zero value (None) as its fallback and drop this serializer.
	if (!v.empty()) return JsonAny(v);
	return JsonAny();
}

// Generic Kind() serializers — reflect.Int / reflect.String omit their zero
// value so a partial config does not clobber defaults with zeros when
// round-tripped through withConfig.
JsonAny serializeInt(int v) {
	if (v == 0) return JsonAny();
	return JsonAny(static_cast<int64_t>(v));
}
JsonAny serializeString(const std::string& v) {
	if (v.empty()) return JsonAny();
	return JsonAny(v);
}
JsonAny serializeBool(bool v) {
	return JsonAny(v);
}
JsonAny serializeStringSlice(const std::vector<std::string>& v) {
	// Go returns nil for a nil slice only; a non-nil empty slice serializes
	// as []. A C++ vector cannot distinguish nil from empty — we treat empty
	// as nil (the common case) and omit it.
	if (v.empty()) return JsonAny();
	std::vector<JsonAny> arr;
	arr.reserve(v.size());
	for (auto& s : v) arr.emplace_back(s);
	return JsonAny(std::move(arr));
}

// IndentStyle has Kind() == Int in Go (a custom int type), so it serializes
// via the generic int branch.
JsonAny serializeIndentStyle(IndentStyle v) {
	return serializeInt(static_cast<int>(v));
}
// SemicolonPreference / QuotePreference / WorkspaceSymbolsScope /
// IncludeInlayParameterNameHints have Kind() == String.
JsonAny serializeSemicolonPreference(const SemicolonPreference& v) {
	// SemicolonPreference has Kind() == String in Go; it serializes through
	// the generic string branch, so the "" zero value (Unset) returns nil.
	if (v == SemicolonPreference::Unset) return JsonAny();
	return serializeString(v == SemicolonPreference::Insert ? "insert" :
		(v == SemicolonPreference::Remove ? "remove" : "ignore"));
}

// --- configPathParsers (userpreferences.go:486) ---
// Field-specific config value parsers that override the default type-based
// parser when the VS Code config value format differs from the Go field type.
Tristate parseCaseSensitivityConfig(const JsonAny& val) {
	// VS Code sends caseSensitivity as a string
	// ("auto"/"caseSensitive"/"caseInsensitive"), but
	// OrganizeImportsIgnoreCase is a core.Tristate.
	if (val.is(JsonAny::K::String)) {
		std::string s = detail::goToLower(val.s);
		if (s == "caseinsensitive") return Tristate::True;
		if (s == "casesensitive") return Tristate::False;
	}
	if (val.is(JsonAny::K::Bool)) {
		return val.b ? Tristate::True : Tristate::False;
	}
	return Tristate::Unknown;
}

// --- fieldInfo table (userpreferences.go:499) ---

struct configPathInfo {
	const char* path;
	bool invert = false;
};

using GetFn = std::function<JsonAny(const UserPreferences&)>;
// parse returns nullopt for "leave field unchanged" (the Go generic parsers'
// no-match case); an engaged value is always assigned.
using AppFn = std::function<void(UserPreferences*, const JsonAny&)>;

struct fieldInfo {
	const char* rawName = nullptr;         // raw name for unstable section lookup
	const char* configPath = nullptr;      // dotted path for config
	std::vector<configPathInfo> fallbackConfigPaths;
	bool rawInvert = false;
	bool configInvert = false;
	GetFn serialize;                        // serializeField for this field
	AppFn apply;                            // setFieldFromValue for this field
};

// parse/ser arrive as lambdas; taking them generically avoids
// std::function-in-signature deduction failures.
template <class M, class Parse, class Ser>
std::pair<GetFn, AppFn> makeAcc(M UserPreferences::*mp, Parse&& parse, Ser&& ser) {
	std::function<std::optional<M>(const JsonAny&)> parseFn = parse;
	std::function<JsonAny(const M&)> serFn = ser;
	return {
		[mp, serFn](const UserPreferences& u) -> JsonAny { return serFn(u.*mp); },
		[mp, parseFn](UserPreferences* u, const JsonAny& v) {
			if (auto p = parseFn(v)) u->*mp = *p;
		},
	};
}

template <class S, class M, class B, class Parse, class Ser>
std::pair<GetFn, AppFn> makeAcc2(S UserPreferences::*outer, M B::*mp, Parse&& parse, Ser&& ser) {
	std::function<std::optional<M>(const JsonAny&)> parseFn = parse;
	std::function<JsonAny(const M&)> serFn = ser;
	return {
		[outer, mp, serFn](const UserPreferences& u) -> JsonAny { return serFn((u.*outer).*mp); },
		[outer, mp, parseFn](UserPreferences* u, const JsonAny& v) {
			if (auto p = parseFn(v)) (u->*outer).*mp = *p;
		},
	};
}

// Wraps a Go type-parser that always produces a value.
template <class M>
std::optional<M> alwaysParse(std::function<M(const JsonAny&)> parse, const JsonAny& v) {
	return parse(v);
}

template <class M, class Parse, class Ser>
std::pair<GetFn, AppFn> makeAccParsed(M UserPreferences::*mp, Parse&& parse, Ser&& ser) {
	return makeAcc<M>(mp,
		[parse](const JsonAny& v) -> std::optional<M> { return alwaysParse<M>(parse, v); },
		std::forward<Ser>(ser));
}

template <class S, class M, class B, class Parse, class Ser>
std::pair<GetFn, AppFn> makeAcc2Parsed(S UserPreferences::*outer, M B::*mp, Parse&& parse, Ser&& ser) {
	return makeAcc2<S, M, B>(outer, mp,
		[parse](const JsonAny& v) -> std::optional<M> { return alwaysParse<M>(parse, v); },
		std::forward<Ser>(ser));
}

// fieldInfoCache — userpreferences.go:561. sync.OnceValue compiled into a
// static init; entries appear in collectFieldInfos order.
const std::vector<fieldInfo>& fieldInfoCache() {
	static const std::vector<fieldInfo> infos = [] {
		std::vector<fieldInfo> v;
		auto F = [&v](std::pair<GetFn, AppFn> acc, const char* raw, const char* cfg,
					  std::vector<configPathInfo> fb = {}, bool rawInv = false,
					  bool cfgInv = false) {
			fieldInfo i;
			i.rawName = raw;
			i.configPath = cfg;
			i.fallbackConfigPaths = std::move(fb);
			i.rawInvert = rawInv;
			i.configInvert = cfgInv;
			i.serialize = acc.first;
			i.apply = acc.second;
			v.push_back(std::move(i));
		};

		// FormatCodeSettings (embedded, untagged) — recursed at declaration
		// position; its embedded EditorSettings recurses first.
		F(makeAcc2(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::BaseIndentSize,
				   [](const JsonAny& val) { return parseInt(val); }, serializeInt),
		  "baseIndentSize", "format.baseIndentSize");
		F(makeAcc2(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::IndentSize,
				   [](const JsonAny& val) { return parseInt(val); }, serializeInt),
		  "indentSize", "format.indentSize");
		F(makeAcc2(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::TabSize,
				   [](const JsonAny& val) { return parseInt(val); }, serializeInt),
		  "tabSize", "format.tabSize");
		F(makeAcc2(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::NewLineCharacter,
				   [](const JsonAny& val) { return parseString(val); }, serializeString),
		  "newLineCharacter", "format.newLineCharacter");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::ConvertTabsToSpaces,
						 parseTristate, serializeTristate),
		  "convertTabsToSpaces", "format.convertTabsToSpaces");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::IndentStyle,
						 parseIndentStyle, serializeIndentStyle),
		  "indentStyle", "format.indentStyle");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::TrimTrailingWhitespace,
						 parseTristate, serializeTristate),
		  "trimTrailingWhitespace", "format.trimTrailingWhitespace");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceAfterCommaDelimiter,
						 parseTristate, serializeTristate),
		  "insertSpaceAfterCommaDelimiter", "format.insertSpaceAfterCommaDelimiter");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceAfterSemicolonInForStatements,
						 parseTristate, serializeTristate),
		  "insertSpaceAfterSemicolonInForStatements", "format.insertSpaceAfterSemicolonInForStatements");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceBeforeAndAfterBinaryOperators,
						 parseTristate, serializeTristate),
		  "insertSpaceBeforeAndAfterBinaryOperators", "format.insertSpaceBeforeAndAfterBinaryOperators");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceAfterConstructor,
						 parseTristate, serializeTristate),
		  "insertSpaceAfterConstructor", "format.insertSpaceAfterConstructor");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceAfterKeywordsInControlFlowStatements,
						 parseTristate, serializeTristate),
		  "insertSpaceAfterKeywordsInControlFlowStatements", "format.insertSpaceAfterKeywordsInControlFlowStatements");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceAfterFunctionKeywordForAnonymousFunctions,
						 parseTristate, serializeTristate),
		  "insertSpaceAfterFunctionKeywordForAnonymousFunctions", "format.insertSpaceAfterFunctionKeywordForAnonymousFunctions");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis,
						 parseTristate, serializeTristate),
		  "insertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis", "format.insertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceAfterOpeningAndBeforeClosingNonemptyBrackets,
						 parseTristate, serializeTristate),
		  "insertSpaceAfterOpeningAndBeforeClosingNonemptyBrackets", "format.insertSpaceAfterOpeningAndBeforeClosingNonemptyBrackets");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceAfterOpeningAndBeforeClosingNonemptyBraces,
						 parseTristate, serializeTristate),
		  "insertSpaceAfterOpeningAndBeforeClosingNonemptyBraces", "format.insertSpaceAfterOpeningAndBeforeClosingNonemptyBraces");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceAfterOpeningAndBeforeClosingEmptyBraces,
						 parseTristate, serializeTristate),
		  "insertSpaceAfterOpeningAndBeforeClosingEmptyBraces", "format.insertSpaceAfterOpeningAndBeforeClosingEmptyBraces");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces,
						 parseTristate, serializeTristate),
		  "insertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces", "format.insertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceAfterOpeningAndBeforeClosingJsxExpressionBraces,
						 parseTristate, serializeTristate),
		  "insertSpaceAfterOpeningAndBeforeClosingJsxExpressionBraces", "format.insertSpaceAfterOpeningAndBeforeClosingJsxExpressionBraces");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceAfterTypeAssertion,
						 parseTristate, serializeTristate),
		  "insertSpaceAfterTypeAssertion", "format.insertSpaceAfterTypeAssertion");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceBeforeFunctionParenthesis,
						 parseTristate, serializeTristate),
		  "insertSpaceBeforeFunctionParenthesis", "format.insertSpaceBeforeFunctionParenthesis");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::PlaceOpenBraceOnNewLineForFunctions,
						 parseTristate, serializeTristate),
		  "placeOpenBraceOnNewLineForFunctions", "format.placeOpenBraceOnNewLineForFunctions");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::PlaceOpenBraceOnNewLineForControlBlocks,
						 parseTristate, serializeTristate),
		  "placeOpenBraceOnNewLineForControlBlocks", "format.placeOpenBraceOnNewLineForControlBlocks");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::InsertSpaceBeforeTypeAnnotation,
						 parseTristate, serializeTristate),
		  "insertSpaceBeforeTypeAnnotation", "format.insertSpaceBeforeTypeAnnotation");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::IndentMultiLineObjectLiteralBeginningOnBlankLine,
						 parseTristate, serializeTristate),
		  "indentMultiLineObjectLiteralBeginningOnBlankLine", "format.indentMultiLineObjectLiteralBeginningOnBlankLine");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::Semicolons,
						 parseSemicolonPreference, serializeSemicolonPreference),
		  "semicolons", "format.semicolons");
		F(makeAcc2Parsed(&UserPreferences::FormatCodeSettings, &FormatCodeSettings::IndentSwitchCase,
						 parseTristate, serializeTristate),
		  "indentSwitchCase", "format.indentSwitchCase");

		// UserPreferences fields in declaration order.
		F(makeAccParsed(&UserPreferences::QuotePreference, parseQuotePreference, serializeString),
		  "quotePreference", "preferences.quoteStyle");
		F(makeAccParsed(&UserPreferences::LazyConfiguredProjectsFromExternalProject, parseTristate, serializeTristate),
		  "lazyConfiguredProjectsFromExternalProject", nullptr);
		F(makeAcc(&UserPreferences::MaximumHoverLength,
				  [](const JsonAny& val) { return parseInt(val); }, serializeInt),
		  "maximumHoverLength", nullptr);

		F(makeAccParsed(&UserPreferences::IncludeCompletionsForModuleExports, parseTristate, serializeTristate),
		  "includeCompletionsForModuleExports", "suggest.autoImports");
		F(makeAccParsed(&UserPreferences::IncludeCompletionsForImportStatements, parseTristate, serializeTristate),
		  "includeCompletionsForImportStatements", "suggest.includeCompletionsForImportStatements");
		F(makeAccParsed(&UserPreferences::IncludeAutomaticOptionalChainCompletions, parseTristate, serializeTristate),
		  "includeAutomaticOptionalChainCompletions", "suggest.includeAutomaticOptionalChainCompletions");
		F(makeAccParsed(&UserPreferences::IncludeCompletionsWithClassMemberSnippets, parseTristate, serializeTristate),
		  "includeCompletionsWithClassMemberSnippets", "suggest.classMemberSnippets.enabled");
		F(makeAccParsed(&UserPreferences::IncludeCompletionsWithObjectLiteralMethodSnippets, parseTristate, serializeTristate),
		  "includeCompletionsWithObjectLiteralMethodSnippets", "suggest.objectLiteralMethodSnippets.enabled");
		F(makeAccParsed(&UserPreferences::JsxAttributeCompletionStyle, parseJsxAttributeCompletionStyle, serializeJsxAttributeCompletionStyle),
		  "jsxAttributeCompletionStyle", "preferences.jsxAttributeCompletionStyle");
		F(makeAccParsed(&UserPreferences::EnableAutoClosingTags, parseTristate, serializeTristate),
		  "autoClosingTags", "autoClosingTags.enabled", {{"autoClosingTags"}});
		F(makeAccParsed(&UserPreferences::EnableJSDocCompletions, parseTristate, serializeTristate),
		  "completeJSDocs", "suggest.jsdoc.enabled", {{"suggest.completeJSDocs"}});
		F(makeAccParsed(&UserPreferences::GenerateReturnInDocTemplate, parseTristate, serializeTristate),
		  "generateReturnInDocTemplate", "suggest.jsdoc.generateReturns");

		F(makeAccParsed(&UserPreferences::ImportModuleSpecifierPreference, parseImportModuleSpecifierPreference,
						serializeImportModuleSpecifierPreference),
		  "importModuleSpecifierPreference", "preferences.importModuleSpecifier");
		F(makeAccParsed(&UserPreferences::ImportModuleSpecifierEnding, parseImportModuleSpecifierEndingPreference,
						serializeImportModuleSpecifierEndingPreference),
		  "importModuleSpecifierEnding", "preferences.importModuleSpecifierEnding");
		F(makeAcc(&UserPreferences::AutoImportSpecifierExcludeRegexes,
				  [](const JsonAny& val) { return parseStringSlice(val); }, serializeStringSlice),
		  "autoImportSpecifierExcludeRegexes", "preferences.autoImportSpecifierExcludeRegexes");
		F(makeAcc(&UserPreferences::AutoImportFileExcludePatterns,
				  [](const JsonAny& val) { return parseStringSlice(val); }, serializeStringSlice),
		  "autoImportFileExcludePatterns", "preferences.autoImportFileExcludePatterns");
		F(makeAccParsed(&UserPreferences::AutoImportEntrypointDirectorySearch, parseTristate, serializeTristate),
		  "autoImportEntrypointDirectorySearch", "preferences.autoImportEntrypointDirectorySearch");
		F(makeAccParsed(&UserPreferences::PreferTypeOnlyAutoImports, parseTristate, serializeTristate),
		  "preferTypeOnlyAutoImports", "preferences.preferTypeOnlyAutoImports");

		F(makeAccParsed(&UserPreferences::OrganizeImportsSort, parseOrganizeImportsSort, serializeOrganizeImportsSort),
		  "organizeImportsSort", "preferences.organizeImports.sort");
		F(makeAccParsed(&UserPreferences::OrganizeImportsIgnoreCase, parseTristate, serializeTristate),
		  "organizeImportsIgnoreCase", "preferences.organizeImports.caseSensitivity");
		F(makeAccParsed(&UserPreferences::OrganizeImportsCollation, parseOrganizeImportsCollation, serializeOrganizeImportsCollation),
		  "organizeImportsCollation", "preferences.organizeImports.unicodeCollation");
		F(makeAcc(&UserPreferences::OrganizeImportsLocale,
				  [](const JsonAny& val) { return parseString(val); }, serializeString),
		  "organizeImportsLocale", "preferences.organizeImports.locale");
		F(makeAccParsed(&UserPreferences::OrganizeImportsNumericCollation, parseTristate, serializeTristate),
		  "organizeImportsNumericCollation", "preferences.organizeImports.numericCollation");
		F(makeAccParsed(&UserPreferences::OrganizeImportsAccentCollation, parseTristate, serializeTristate),
		  "organizeImportsAccentCollation", "preferences.organizeImports.accentCollation");
		F(makeAccParsed(&UserPreferences::OrganizeImportsCaseFirst, parseOrganizeImportsCaseFirst, serializeOrganizeImportsCaseFirst),
		  "organizeImportsCaseFirst", "preferences.organizeImports.caseFirst");
		F(makeAccParsed(&UserPreferences::OrganizeImportsTypeOrder, parseOrganizeImportsTypeOrder, serializeOrganizeImportsTypeOrder),
		  "organizeImportsTypeOrder", "preferences.organizeImports.typeOrder");

		F(makeAccParsed(&UserPreferences::AllowTextChangesInNewFiles, parseTristate, serializeTristate),
		  "allowTextChangesInNewFiles", nullptr);

		F(makeAccParsed(&UserPreferences::UseAliasesForRename, parseTristate, serializeTristate),
		  "providePrefixAndSuffixTextForRename", "preferences.useAliasesForRenames");
		F(makeAccParsed(&UserPreferences::AllowRenameOfImportPath, parseTristate, serializeTristate),
		  "allowRenameOfImportPath", nullptr);

		F(makeAccParsed(&UserPreferences::ProvideRefactorNotApplicableReason, parseTristate, serializeTristate),
		  "provideRefactorNotApplicableReason", nullptr);

		// InlayHints (embedded, untagged) — recursed at declaration position.
		F(makeAcc2Parsed(&UserPreferences::InlayHints, &InlayHintsPreferences::IncludeInlayParameterNameHints,
						 parseIncludeInlayParameterNameHints, serializeString),
		  "includeInlayParameterNameHints", "inlayHints.parameterNames.enabled");
		F(makeAcc2Parsed(&UserPreferences::InlayHints, &InlayHintsPreferences::IncludeInlayParameterNameHintsWhenArgumentMatchesName,
						 parseTristate, serializeTristate),
		  "includeInlayParameterNameHintsWhenArgumentMatchesName",
		  "inlayHints.parameterNames.suppressWhenArgumentMatchesName", {}, false, true);
		F(makeAcc2Parsed(&UserPreferences::InlayHints, &InlayHintsPreferences::IncludeInlayFunctionParameterTypeHints,
						 parseTristate, serializeTristate),
		  "includeInlayFunctionParameterTypeHints", "inlayHints.parameterTypes.enabled");
		F(makeAcc2Parsed(&UserPreferences::InlayHints, &InlayHintsPreferences::IncludeInlayVariableTypeHints,
						 parseTristate, serializeTristate),
		  "includeInlayVariableTypeHints", "inlayHints.variableTypes.enabled");
		F(makeAcc2Parsed(&UserPreferences::InlayHints, &InlayHintsPreferences::IncludeInlayVariableTypeHintsWhenTypeMatchesName,
						 parseTristate, serializeTristate),
		  "includeInlayVariableTypeHintsWhenTypeMatchesName",
		  "inlayHints.variableTypes.suppressWhenTypeMatchesName", {}, false, true);
		F(makeAcc2Parsed(&UserPreferences::InlayHints, &InlayHintsPreferences::IncludeInlayPropertyDeclarationTypeHints,
						 parseTristate, serializeTristate),
		  "includeInlayPropertyDeclarationTypeHints", "inlayHints.propertyDeclarationTypes.enabled");
		F(makeAcc2Parsed(&UserPreferences::InlayHints, &InlayHintsPreferences::IncludeInlayFunctionLikeReturnTypeHints,
						 parseTristate, serializeTristate),
		  "includeInlayFunctionLikeReturnTypeHints", "inlayHints.functionLikeReturnTypes.enabled");
		F(makeAcc2Parsed(&UserPreferences::InlayHints, &InlayHintsPreferences::IncludeInlayEnumMemberValueHints,
						 parseTristate, serializeTristate),
		  "includeInlayEnumMemberValueHints", "inlayHints.enumMemberValues.enabled");

		// CodeLens (embedded, untagged) — recursed at declaration position.
		F(makeAcc2Parsed(&UserPreferences::CodeLens, &CodeLensUserPreferences::ReferencesCodeLensEnabled,
						 parseTristate, serializeTristate),
		  "referencesCodeLensEnabled", "referencesCodeLens.enabled");
		F(makeAcc2Parsed(&UserPreferences::CodeLens, &CodeLensUserPreferences::ImplementationsCodeLensEnabled,
						 parseTristate, serializeTristate),
		  "implementationsCodeLensEnabled", "implementationsCodeLens.enabled");
		F(makeAcc2Parsed(&UserPreferences::CodeLens, &CodeLensUserPreferences::ReferencesCodeLensShowOnAllFunctions,
						 parseTristate, serializeTristate),
		  "referencesCodeLensShowOnAllFunctions", "referencesCodeLens.showOnAllFunctions");
		F(makeAcc2Parsed(&UserPreferences::CodeLens, &CodeLensUserPreferences::ImplementationsCodeLensShowOnInterfaceMethods,
						 parseTristate, serializeTristate),
		  "implementationsCodeLensShowOnInterfaceMethods", "implementationsCodeLens.showOnInterfaceMethods");
		F(makeAcc2Parsed(&UserPreferences::CodeLens, &CodeLensUserPreferences::ImplementationsCodeLensShowOnAllClassMethods,
						 parseTristate, serializeTristate),
		  "implementationsCodeLensShowOnAllClassMethods", "implementationsCodeLens.showOnAllClassMethods");

		F(makeAcc(&UserPreferences::PreferGoToSourceDefinition,
				  [](const JsonAny& val) { return parseBool(val); }, serializeBool),
		  "preferGoToSourceDefinition", nullptr);

		F(makeAccParsed(&UserPreferences::ExcludeLibrarySymbolsInNavTo, parseTristate, serializeTristate),
		  "excludeLibrarySymbolsInNavTo", "workspaceSymbols.excludeLibrarySymbols");
		F(makeAcc(&UserPreferences::WorkspaceSymbolsScope,
				  [](const JsonAny& val) { return parseString(val); }, serializeString),
		  nullptr, "workspaceSymbols.scope");

		F(makeAccParsed(&UserPreferences::EnableFormatting, parseTristate, serializeTristate),
		  "formatEnabled", "format.enabled", {{"format.enable"}});
		F(makeAccParsed(&UserPreferences::EnableValidation, parseTristate, serializeTristate),
		  "validateEnabled", "validate.enabled", {{"validate.enable"}});
		F(makeAccParsed(&UserPreferences::DisableSuggestions, parseTristate, serializeTristate),
		  "disableSuggestions", nullptr);
		F(makeAccParsed(&UserPreferences::DisableLineTextInReferences, parseTristate, serializeTristate),
		  "disableLineTextInReferences", nullptr);
		F(makeAccParsed(&UserPreferences::DisplayPartsForJSDoc, parseTristate, serializeTristate),
		  "displayPartsForJSDoc", nullptr);
		F(makeAccParsed(&UserPreferences::ReportStyleChecksAsWarnings, parseTristate, serializeTristate),
		  "reportStyleChecksAsWarnings", "reportStyleChecksAsWarnings");
		F(makeAcc(&UserPreferences::Locale,
				  [](const JsonAny& val) { return parseString(val); }, serializeString),
		  nullptr, "locale");

		F(makeAccParsed(&UserPreferences::DisableAutomaticTypeAcquisition, parseTristate, serializeTristate),
		  "disableAutomaticTypeAcquisition", "disableAutomaticTypeAcquisition");
		F(makeAccParsed(&UserPreferences::AutomaticTypeAcquisitionEnabled, parseTristate, serializeTristate),
		  "automaticTypeAcquisitionEnabled", "tsserver.automaticTypeAcquisition.enabled");

		F(makeAcc(&UserPreferences::CustomConfigFileName,
				  [](const JsonAny& val) { return parseString(val); }, serializeString),
		  "customConfigFileName", "customConfigFileName");

		return v;
	}();
	return infos;
}

// unstableNameIndex — userpreferences.go:567. Maps raw names to fieldInfo
// index for unstable section lookup.
const std::map<std::string, size_t>& unstableNameIndex() {
	static const std::map<std::string, size_t> index = [] {
		const std::vector<fieldInfo>& infos = fieldInfoCache();
		std::map<std::string, size_t> m;
		for (size_t i = 0; i < infos.size(); i++) {
			if (infos[i].rawName != nullptr) {
				m[infos[i].rawName] = i;
			}
		}
		return m;
	}();
	return index;
}

std::vector<std::string> goSplit(const std::string& s, char sep) {
	std::vector<std::string> out;
	size_t pos = 0;
	while (true) {
		size_t next = s.find(sep, pos);
		if (next == std::string::npos) {
			out.push_back(s.substr(pos));
			return out;
		}
		out.push_back(s.substr(pos, next - pos));
		pos = next + 1;
	}
}

// getNestedValue — userpreferences.go:634
std::pair<const JsonAny*, bool> getNestedValue(const JsonObject& config, const std::string& path) {
	std::vector<std::string> parts = goSplit(path, '.');
	const JsonAny* current = nullptr;
	const JsonObject* currentMap = &config;
	bool first = true;
	for (const std::string& part : parts) {
		if (!first) {
			if (current == nullptr || !current->is(JsonAny::K::Object)) {
				return {nullptr, false};
			}
			currentMap = &current->obj;
		}
		auto it = currentMap->find(part);
		if (it == currentMap->end()) {
			return {nullptr, false};
		}
		current = &it->second;
		first = false;
	}
	return {current, true};
}

// setNestedValue — userpreferences.go:648
void setNestedValue(JsonObject& config, const std::string& path, JsonAny value) {
	std::vector<std::string> parts = goSplit(path, '.');
	JsonObject* current = &config;
	for (size_t i = 0; i + 1 < parts.size(); i++) {
		JsonAny& slot = (*current)[parts[i]];
		if (!slot.is(JsonAny::K::Object)) {
			slot = JsonAny(JsonObject{});
		}
		current = &slot.obj;
	}
	(*current)[parts.back()] = std::move(value);
}

// setFieldFromValue — userpreferences.go:755. The typeParser / typed-parse
// selection is compiled into each fieldInfo's apply lambda.
void setFieldFromValue(UserPreferences* p, const fieldInfo& info, const JsonAny& val) {
	if (val.is(JsonAny::K::Nil)) {
		return;
	}
	info.apply(p, val);
}

// setRawFieldsFromConfig — userpreferences.go:662
void setRawFieldsFromConfig(UserPreferences* p, const JsonObject& settings) {
	const std::map<std::string, size_t>& index = unstableNameIndex();
	for (auto& [name, value] : settings) {
		auto it = index.find(name);
		if (it == index.end()) continue;
		const fieldInfo& info = fieldInfoCache()[it->second];
		JsonAny v = value;
		if (info.rawInvert) {
			if (v.is(JsonAny::K::Bool)) {
				v.b = !v.b;
			}
		}
		setFieldFromValue(p, info, v);
	}
}

// withConfig — userpreferences.go:680
UserPreferences withConfig(UserPreferences p, const JsonObject& config) {
	const std::vector<fieldInfo>& infos = fieldInfoCache();

	// Raw UserPreferences can be provided directly, notably via LSP initializationOptions.
	setRawFieldsFromConfig(&p, config);

	// Process "unstable" section first - allows any field to be set by raw name.
	// This mirrors VS Code's behavior: { ...config.get('unstable'), ...stableOptions }
	// where stable options are spread after and take precedence.
	if (auto it = config.find("unstable"); it != config.end() && it->second.is(JsonAny::K::Object)) {
		setRawFieldsFromConfig(&p, it->second.obj);
	}

	// Process path-based config (VS Code style nested paths).
	// These run after unstable, so stable config values take precedence.
	for (const fieldInfo& info : infos) {
		if (info.configPath == nullptr) {
			continue;
		}
		configPathInfo configPath{info.configPath, info.configInvert};
		auto [val, ok] = getNestedValue(config, configPath.path);
		if (!ok) {
			for (const configPathInfo& fallbackConfigPath : info.fallbackConfigPaths) {
				auto res = getNestedValue(config, fallbackConfigPath.path);
				val = res.first;
				ok = res.second;
				if (ok) {
					configPath = fallbackConfigPath;
					break;
				}
			}
		}
		if (!ok) {
			continue;
		}

		JsonAny v = *val;
		if (configPath.invert) {
			if (v.is(JsonAny::K::Bool)) {
				v.b = !v.b;
			}
		}
		if (std::string(configPath.path) == "preferences.organizeImports.caseSensitivity") {
			p.OrganizeImportsIgnoreCase = parseCaseSensitivityConfig(v);
			continue;
		}
		setFieldFromValue(&p, info, v);
	}

	// Validate CustomConfigFileName for path traversal
	if (!p.CustomConfigFileName.empty()) {
		std::string name = detail::goTrimSpace(p.CustomConfigFileName);
		if (name.find_first_of("/\\") != std::string::npos || name == ".." || name == ".") {
			p.CustomConfigFileName.clear();
		} else {
			p.CustomConfigFileName = std::move(name);
		}
	}

	return p;
}

} // namespace

// --- test access (userpreferences_test.go) ---
// The Go tests live in-package and call unexported helpers directly; these
// wrappers give the C++ test ports the same access without widening the
// product API surface.

// TestWithConfig — UserPreferences{}.withConfig(config).
UserPreferences TestWithConfig(UserPreferences p, const JsonObject& config) {
	return withConfig(std::move(p), config);
}

static bool sameJsonAny(const JsonAny& a, const JsonAny& b) {
	if (a.kind != b.kind) return false;
	switch (a.kind) {
	case JsonAny::K::Nil: return true;
	case JsonAny::K::Bool: return a.b == b.b;
	case JsonAny::K::Int: return a.i == b.i;
	case JsonAny::K::Float: return a.f == b.f;
	case JsonAny::K::String: return a.s == b.s;
	case JsonAny::K::Array:
		if (a.arr.size() != b.arr.size()) return false;
		for (size_t i = 0; i < a.arr.size(); i++) {
			if (!sameJsonAny(a.arr[i], b.arr[i])) return false;
		}
		return true;
	case JsonAny::K::Object:
		if (a.obj.size() != b.obj.size()) return false;
		for (auto& [k, v] : a.obj) {
			auto it = b.obj.find(k);
			if (it == b.obj.end() || !sameJsonAny(v, it->second)) return false;
		}
		return true;
	}
	return false;
}

// AllFieldsNonZeroUserPreferences — fillNonZeroValues (userpreferences_test.go).
// Go fills every exported field with a non-zero value via reflection; here each
// fieldInfo's apply is driven with candidate JSON values until the field's
// serialized value differs from its zero-field baseline.
UserPreferences AllFieldsNonZeroUserPreferences() {
	UserPreferences p;
	UserPreferences zero;
	std::vector<JsonAny> candidates;
	candidates.emplace_back(true);
	candidates.emplace_back(1);
	candidates.emplace_back(1.5);
	static const char* strings[] = {
	    "test", "auto", "single", "double", "braces", "none", "all",
	    "literals", "insert", "remove", "ignore", "shortest", "relative",
	    "non-relative", "project-relative", "minimal", "index", "js",
	    "always", "prompt", "never", "on", "off", "default", "first",
	    "last", "natural", "ordinal", "caseSensitive", "caseInsensitive",
	    "unicode", "lower", "upper", "inline", ".", "..", "en", "normal",
	    "verbose", "classic", "node", "preserve", "es2015", "esnext",
	    "commonjs", "system", "amd", "umd",
	};
	for (const char* s : strings) candidates.emplace_back(s);
	candidates.emplace_back(std::vector<JsonAny>{JsonAny("test")});
	candidates.emplace_back(JsonObject{{"test", JsonAny("test")}});

	std::vector<std::string> unfilled;
	for (const fieldInfo& info : fieldInfoCache()) {
		JsonAny baseline = info.serialize(zero);
		bool filled = false;
		for (const JsonAny& candidate : candidates) {
			info.apply(&p, candidate);
			if (!sameJsonAny(info.serialize(p), baseline)) {
				filled = true;
				break;
			}
		}
		if (!filled) unfilled.emplace_back(info.rawName);
	}
	if (!unfilled.empty()) {
		std::string names;
		for (auto& n : unfilled) names += " " + n;
		fprintf(stderr,
		        "AllFieldsNonZeroUserPreferences: no candidate filled "
		        "field(s):%s\n",
		        names.c_str());
	}
	return p;
}

// NewDefaultUserPreferences — userpreferences.go:16
UserPreferences NewDefaultUserPreferences() {
	UserPreferences p;
	p.FormatCodeSettings = GetDefaultFormatCodeSettings();
	p.IncludeCompletionsForModuleExports = Tristate::True;
	p.IncludeCompletionsForImportStatements = Tristate::True;
	p.EnableAutoClosingTags = Tristate::True;
	p.EnableJSDocCompletions = Tristate::True;
	p.GenerateReturnInDocTemplate = Tristate::True;
	p.AllowRenameOfImportPath = Tristate::True;
	p.ProvideRefactorNotApplicableReason = Tristate::True;
	p.EnableFormatting = Tristate::True;
	p.EnableValidation = Tristate::True;
	p.DisplayPartsForJSDoc = Tristate::True;
	p.DisableLineTextInReferences = Tristate::True;
	p.ReportStyleChecksAsWarnings = Tristate::True;
	p.ExcludeLibrarySymbolsInNavTo = Tristate::True;
	p.WorkspaceSymbolsScope = WorkspaceSymbolsScopeAllOpenProjects;
	return p;
}

// IsATADisabled — userpreferences.go:194
bool UserPreferences::IsATADisabled() const {
	if (AutomaticTypeAcquisitionEnabled != Tristate::Unknown) {
		return AutomaticTypeAcquisitionEnabled != Tristate::True;
	}
	return DisableAutomaticTypeAcquisition == Tristate::True;
}

// MarshalJSONTo — userpreferences.go:793
std::string UserPreferences::marshalJSONTo(tsc::json::Encoder& enc) const {
	JsonObject config;
	for (const fieldInfo& info : fieldInfoCache()) {
		JsonAny val = info.serialize(*this);
		if (val.is(JsonAny::K::Nil)) {
			continue;
		}

		// Prefer config path if available, otherwise use unstable section
		if (info.configPath != nullptr) {
			if (info.configInvert && val.is(JsonAny::K::Bool)) {
				val.b = !val.b;
			}
			setNestedValue(config, info.configPath, std::move(val));
		} else if (info.rawName != nullptr) {
			if (info.rawInvert && val.is(JsonAny::K::Bool)) {
				val.b = !val.b;
			}
			setNestedValue(config, std::string("unstable.") + info.rawName, std::move(val));
		}
	}

	return tsc::json::marshalEncode(enc, config, {tsc::json::deterministic(true)});
}

// UnmarshalJSONFrom — userpreferences.go:865
std::string UserPreferences::unmarshalJSONFrom(tsc::json::Decoder& dec) {
	JsonObject config;
	if (std::string err = tsc::json::unmarshalDecode(dec, &config); !err.empty()) {
		return err;
	}
	// Start with defaults, then overlay parsed values
	*this = withConfig(NewDefaultUserPreferences(), config);
	return {};
}

// ModuleSpecifierPreferences — userpreferences.go:877
tsc::modulespecifiers::UserPreferences UserPreferences::ModuleSpecifierPreferences() const {
	tsc::modulespecifiers::UserPreferences prefs;
	prefs.ImportModuleSpecifierPreference = ImportModuleSpecifierPreference;
	prefs.ImportModuleSpecifierEnding = ImportModuleSpecifierEnding;
	prefs.AutoImportSpecifierExcludeRegexes = AutoImportSpecifierExcludeRegexes;
	return prefs;
}

// ParsedAutoImportFileExcludePatterns — userpreferences.go:885
std::unique_ptr<tsc::vfs::vfsmatch::SpecMatcher> UserPreferences::ParsedAutoImportFileExcludePatterns(bool useCaseSensitiveFileNames) const {
	return tsc::vfs::vfsmatch::NewSpecMatcher(AutoImportFileExcludePatterns, "", tsc::vfs::vfsmatch::Usage::Exclude, useCaseSensitiveFileNames);
}

// IsModuleSpecifierExcluded — userpreferences.go:889
bool UserPreferences::IsModuleSpecifierExcluded(std::string_view moduleSpecifier) const {
	return tsc::modulespecifiers::IsExcludedByRegex(std::string(moduleSpecifier), AutoImportSpecifierExcludeRegexes);
}

// ParseUserPreferences — userpreferences.go:893
UserPreferences ParseUserPreferences(const JsonObject& items) {
	UserPreferences prefs = NewDefaultUserPreferences();
	// Apply editor settings first (tabSize, indentSize, etc.) as raw-name defaults,
	// then overlay language-specific settings with increasing precedence:
	// editor < javascript < typescript < js/ts
	if (auto it = items.find("editor"); it != items.end() && !it->second.is(JsonAny::K::Nil)) {
		if (it->second.is(JsonAny::K::Object)) {
			const JsonObject& editorSettings = it->second.obj;
			JsonObject normalizedSettings = editorSettings;
			if (auto ts = normalizedSettings.find("tabSize"); ts != normalizedSettings.end()) {
				if (normalizedSettings.find("indentSize") == normalizedSettings.end()) {
					normalizedSettings["indentSize"] = ts->second;
				}
			}
			if (auto is = normalizedSettings.find("insertSpaces"); is != normalizedSettings.end()) {
				if (normalizedSettings.find("convertTabsToSpaces") == normalizedSettings.end()) {
					normalizedSettings["convertTabsToSpaces"] = is->second;
				}
			}
			prefs = withConfig(std::move(prefs), JsonObject{{"unstable", JsonAny(std::move(normalizedSettings))}});
		}
	}
	// Apply javascript, then typescript, then js/ts (highest precedence).
	for (const char* section : {"javascript", "typescript", "js/ts"}) {
		if (auto it = items.find(section); it != items.end() && !it->second.is(JsonAny::K::Nil)) {
			if (it->second.is(JsonAny::K::Object)) {
				prefs = withConfig(std::move(prefs), it->second.obj);
			}
		}
	}
	return prefs;
}

// --- JsonAny ---

// marshalJSONTo — a JSON value in the Go `any` sense.
std::string JsonAny::marshalJSONTo(tsc::json::Encoder& enc) const {
	switch (kind) {
	case K::Nil:
		return enc.writeToken(tsc::json::Null);
	case K::Bool:
		return enc.writeToken(tsc::json::tokenBool(b));
	case K::Int:
		return enc.writeToken(tsc::json::tokenInt(i));
	case K::Float:
		return enc.writeToken(tsc::json::tokenFloat(f));
	case K::String:
		return enc.writeToken(tsc::json::tokenString(s));
	case K::Array: {
		if (auto err = enc.writeToken(tsc::json::BeginArray); !err.empty()) return err;
		for (const JsonAny& e : arr) {
			if (auto err = e.marshalJSONTo(enc); !err.empty()) return err;
		}
		return enc.writeToken(tsc::json::EndArray);
	}
	case K::Object: {
		if (auto err = enc.writeToken(tsc::json::BeginObject); !err.empty()) return err;
		for (auto& [name, v] : obj) {
			if (auto err = enc.writeToken(tsc::json::tokenString(name)); !err.empty()) return err;
			if (auto err = v.marshalJSONTo(enc); !err.empty()) return err;
		}
		return enc.writeToken(tsc::json::EndObject);
	}
	}
	return {};
}

// unmarshalJSONFrom — decodes like Go's encoding/json into `any`: objects →
// map[string]any, arrays → []any, numbers → float64, literals as themselves.
std::string JsonAny::unmarshalJSONFrom(tsc::json::Decoder& dec) {
	switch (dec.peekKind()) {
	case 'n': {
		auto [tok, err] = dec.readToken();
		if (!err.empty()) return err;
		kind = K::Nil;
		return {};
	}
	case 't':
	case 'f': {
		auto [tok, err] = dec.readToken();
		if (!err.empty()) return err;
		kind = K::Bool;
		b = tok.string() == "true";
		return {};
	}
	case '0': {
		auto [tok, err] = dec.readToken();
		if (!err.empty()) return err;
		kind = K::Float;
		f = std::strtod(tok.string().c_str(), nullptr);
		return {};
	}
	case '"': {
		auto [tok, err] = dec.readToken();
		if (!err.empty()) return err;
		kind = K::String;
		s = tok.string();
		return {};
	}
	case '[': {
		if (auto [tok, err] = dec.readToken(); !err.empty()) return err;
		kind = K::Array;
		arr.clear();
		while (dec.peekKind() != ']') {
			JsonAny e;
			if (auto err = e.unmarshalJSONFrom(dec); !err.empty()) return err;
			arr.push_back(std::move(e));
		}
		auto [tok, err] = dec.readToken();
		return err;
	}
	case '{': {
		if (auto [tok, err] = dec.readToken(); !err.empty()) return err;
		kind = K::Object;
		obj.clear();
		while (dec.peekKind() != '}') {
			auto [nameTok, err] = dec.readToken();
			if (!err.empty()) return err;
			JsonAny v;
			if (auto err = v.unmarshalJSONFrom(dec); !err.empty()) return err;
			obj[nameTok.string()] = std::move(v);
		}
		auto [tok, err] = dec.readToken();
		return err;
	}
	}
	return "json: unexpected token kind";
}

} // namespace tsc::ls::lsutil
