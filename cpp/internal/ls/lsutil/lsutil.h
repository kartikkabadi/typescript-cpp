#pragma once

// lsutil — tsc/internal/ls/lsutil. Shared language-service utilities:
// ASI rules, child/token navigation, completed-node detection, format code
// options, organize-imports comparers, symbol display parts, user preferences,
// and misc helpers.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/nodes_generated.h"
#include "internal/ast/kind.h"
#include "internal/ast/flags.h"
#include "internal/collections/collections.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/lsp/lsproto/lsproto.h"

namespace tsc::checker {
class Checker;
}
namespace tsc::compiler {
struct SimpleProgram;
}
namespace tsc::vfs::vfsmatch {
struct SpecMatcher;
}
namespace tsc::modulespecifiers {
struct UserPreferences;
}
namespace tsc::json {
class Encoder;
class Decoder;
}

namespace tsc::ls::lsutil {

// --- formatcodeoptions.go ---

// IndentStyle — formatcodeoptions.go:32
enum class IndentStyle : int32_t {
	None,
	Block,
	Smart,
};
inline constexpr IndentStyle IndentStyleNone = IndentStyle::None;
inline constexpr IndentStyle IndentStyleBlock = IndentStyle::Block;
inline constexpr IndentStyle IndentStyleSmart = IndentStyle::Smart;

// SemicolonPreference — formatcodeoptions.go:37
// Go: `type SemicolonPreference string` ("ignore" | "insert" | "remove").
enum class SemicolonPreference : int32_t {
	Ignore,
	Insert,
	Remove,
};
inline constexpr SemicolonPreference SemicolonPreferenceIgnore = SemicolonPreference::Ignore;
inline constexpr SemicolonPreference SemicolonPreferenceInsert = SemicolonPreference::Insert;
inline constexpr SemicolonPreference SemicolonPreferenceRemove = SemicolonPreference::Remove;
inline constexpr SemicolonPreference SemicolonPreferenceDefault = SemicolonPreference::Ignore;

// EditorSettings — formatcodeoptions.go:42. Field tags preserved as comments:
// the Go `raw`/`config` tags are compiled into the fieldInfo table in
// userpreferences.cpp.
struct EditorSettings {
	int BaseIndentSize = 0;                    // raw:"baseIndentSize" config:"format.baseIndentSize"
	int IndentSize = 0;                        // raw:"indentSize" config:"format.indentSize"
	int TabSize = 0;                           // raw:"tabSize" config:"format.tabSize"
	std::string NewLineCharacter;              // raw:"newLineCharacter" config:"format.newLineCharacter"
	Tristate ConvertTabsToSpaces = Tristate::Unknown; // raw:"convertTabsToSpaces" config:"format.convertTabsToSpaces"
	enum IndentStyle IndentStyle = IndentStyle::None; // raw:"indentStyle" config:"format.indentStyle"
	Tristate TrimTrailingWhitespace = Tristate::Unknown; // raw:"trimTrailingWhitespace" config:"format.trimTrailingWhitespace"
};

// FormatCodeSettings — formatcodeoptions.go:52
struct FormatCodeSettings : EditorSettings {
	Tristate InsertSpaceAfterCommaDelimiter = Tristate::Unknown;                              // raw:"insertSpaceAfterCommaDelimiter" config:"format.insertSpaceAfterCommaDelimiter"
	Tristate InsertSpaceAfterSemicolonInForStatements = Tristate::Unknown;                    // raw:"insertSpaceAfterSemicolonInForStatements" config:"format.insertSpaceAfterSemicolonInForStatements"
	Tristate InsertSpaceBeforeAndAfterBinaryOperators = Tristate::Unknown;                    // raw:"insertSpaceBeforeAndAfterBinaryOperators" config:"format.insertSpaceBeforeAndAfterBinaryOperators"
	Tristate InsertSpaceAfterConstructor = Tristate::Unknown;                                 // raw:"insertSpaceAfterConstructor" config:"format.insertSpaceAfterConstructor"
	Tristate InsertSpaceAfterKeywordsInControlFlowStatements = Tristate::Unknown;             // raw:"insertSpaceAfterKeywordsInControlFlowStatements" config:"format.insertSpaceAfterKeywordsInControlFlowStatements"
	Tristate InsertSpaceAfterFunctionKeywordForAnonymousFunctions = Tristate::Unknown;        // raw:"insertSpaceAfterFunctionKeywordForAnonymousFunctions" config:"format.insertSpaceAfterFunctionKeywordForAnonymousFunctions"
	Tristate InsertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis = Tristate::Unknown;  // raw:"insertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis" config:"format.insertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis"
	Tristate InsertSpaceAfterOpeningAndBeforeClosingNonemptyBrackets = Tristate::Unknown;     // raw:"insertSpaceAfterOpeningAndBeforeClosingNonemptyBrackets" config:"format.insertSpaceAfterOpeningAndBeforeClosingNonemptyBrackets"
	Tristate InsertSpaceAfterOpeningAndBeforeClosingNonemptyBraces = Tristate::Unknown;       // raw:"insertSpaceAfterOpeningAndBeforeClosingNonemptyBraces" config:"format.insertSpaceAfterOpeningAndBeforeClosingNonemptyBraces"
	Tristate InsertSpaceAfterOpeningAndBeforeClosingEmptyBraces = Tristate::Unknown;          // raw:"insertSpaceAfterOpeningAndBeforeClosingEmptyBraces" config:"format.insertSpaceAfterOpeningAndBeforeClosingEmptyBraces"
	Tristate InsertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces = Tristate::Unknown; // raw:"insertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces" config:"format.insertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces"
	Tristate InsertSpaceAfterOpeningAndBeforeClosingJsxExpressionBraces = Tristate::Unknown;  // raw:"insertSpaceAfterOpeningAndBeforeClosingJsxExpressionBraces" config:"format.insertSpaceAfterOpeningAndBeforeClosingJsxExpressionBraces"
	Tristate InsertSpaceAfterTypeAssertion = Tristate::Unknown;                               // raw:"insertSpaceAfterTypeAssertion" config:"format.insertSpaceAfterTypeAssertion"
	Tristate InsertSpaceBeforeFunctionParenthesis = Tristate::Unknown;                        // raw:"insertSpaceBeforeFunctionParenthesis" config:"format.insertSpaceBeforeFunctionParenthesis"
	Tristate PlaceOpenBraceOnNewLineForFunctions = Tristate::Unknown;                         // raw:"placeOpenBraceOnNewLineForFunctions" config:"format.placeOpenBraceOnNewLineForFunctions"
	Tristate PlaceOpenBraceOnNewLineForControlBlocks = Tristate::Unknown;                     // raw:"placeOpenBraceOnNewLineForControlBlocks" config:"format.placeOpenBraceOnNewLineForControlBlocks"
	Tristate InsertSpaceBeforeTypeAnnotation = Tristate::Unknown;                             // raw:"insertSpaceBeforeTypeAnnotation" config:"format.insertSpaceBeforeTypeAnnotation"
	Tristate IndentMultiLineObjectLiteralBeginningOnBlankLine = Tristate::Unknown;            // raw:"indentMultiLineObjectLiteralBeginningOnBlankLine" config:"format.indentMultiLineObjectLiteralBeginningOnBlankLine"
	enum SemicolonPreference Semicolons = SemicolonPreference::Ignore;                        // raw:"semicolons" config:"format.semicolons"
	Tristate IndentSwitchCase = Tristate::Unknown;                                            // raw:"indentSwitchCase" config:"format.indentSwitchCase"

	// ToLSFormatOptions — formatcodeoptions.go:116
	std::unique_ptr<tsc::lsp::lsproto::FormattingOptions> ToLSFormatOptions() const;
};

// formatcodeoptions.go:105
FormatCodeSettings FromLSFormatOptions(FormatCodeSettings f, const tsc::lsp::lsproto::FormattingOptions& opt);
// formatcodeoptions.go:126
FormatCodeSettings GetDefaultFormatCodeSettings();

struct JsonAny;
// formatcodeoptions.go:19,46 — unexported in Go; shared across this package's
// translation units for the fieldInfo table.
IndentStyle parseIndentStyle(const JsonAny& val);
SemicolonPreference parseSemicolonPreference(const JsonAny& val);

// --- userpreferences.go ---

// JSON-value variant standing in for Go's `any` as decoded by
// encoding/json into map[string]any (nil/bool/float64/string/[]any/
// map[string]any), plus Int for programmatically-constructed values.
struct JsonAny {
	enum class K { Nil, Bool, Int, Float, String, Array, Object };
	K kind = K::Nil;
	bool b = false;
	int64_t i = 0;
	double f = 0;
	std::string s;
	std::vector<JsonAny> arr;
	std::map<std::string, JsonAny> obj;

	JsonAny() = default;
	JsonAny(bool v) : kind(K::Bool), b(v) {}
	JsonAny(int v) : kind(K::Int), i(v) {}
	JsonAny(int64_t v) : kind(K::Int), i(v) {}
	JsonAny(double v) : kind(K::Float), f(v) {}
	JsonAny(const char* v) : kind(K::String), s(v) {}
	JsonAny(std::string_view v) : kind(K::String), s(v) {}
	JsonAny(std::string v) : kind(K::String), s(std::move(v)) {}
	JsonAny(std::vector<JsonAny> v) : kind(K::Array), arr(std::move(v)) {}
	JsonAny(std::map<std::string, JsonAny> v) : kind(K::Object), obj(std::move(v)) {}
	template <class T>
	JsonAny(std::vector<T> v) : kind(K::Array) {
		arr.reserve(v.size());
		for (auto& e : v) arr.emplace_back(JsonAny(std::move(e)));
	}

	bool is(K k) const { return kind == k; }
	std::string marshalJSONTo(tsc::json::Encoder& enc) const;
	std::string unmarshalJSONFrom(tsc::json::Decoder& dec);
};
// Go `map[string]any`
using JsonObject = std::map<std::string, JsonAny>;

// --- enum types (userpreferences.go:228-295) ---

using QuotePreference = std::string;
inline const QuotePreference QuotePreferenceUnknown = "";
inline const QuotePreference QuotePreferenceAuto = "auto";
inline const QuotePreference QuotePreferenceDouble = "double";
inline const QuotePreference QuotePreferenceSingle = "single";

using WorkspaceSymbolsScope = std::string;
inline const WorkspaceSymbolsScope WorkspaceSymbolsScopeAllOpenProjects = "allOpenProjects";
inline const WorkspaceSymbolsScope WorkspaceSymbolsScopeCurrentProject = "currentProject";

using JsxAttributeCompletionStyle = std::string;
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleUnknown = "";
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleAuto = "auto";
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleBraces = "braces";
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleNone = "none";

using IncludeInlayParameterNameHints = std::string;
inline const IncludeInlayParameterNameHints IncludeInlayParameterNameHintsNone = "";
inline const IncludeInlayParameterNameHints IncludeInlayParameterNameHintsAll = "all";
inline const IncludeInlayParameterNameHints IncludeInlayParameterNameHintsLiterals = "literals";

// OrganizeImportsSort — userpreferences.go:263 (`type OrganizeImportsSort int`)
using OrganizeImportsSort = int;
inline constexpr OrganizeImportsSort OrganizeImportsSortAuto = 0;
inline constexpr OrganizeImportsSort OrganizeImportsSortOrdinal = 1;
inline constexpr OrganizeImportsSort OrganizeImportsSortOrdinalIgnoreCase = 2;
inline constexpr OrganizeImportsSort OrganizeImportsSortNatural = 3;
inline constexpr OrganizeImportsSort OrganizeImportsSortNaturalIgnoreCase = 4;

// OrganizeImportsCollation — userpreferences.go:273 (`type OrganizeImportsCollation bool`)
using OrganizeImportsCollation = bool;
inline constexpr OrganizeImportsCollation OrganizeImportsCollationOrdinal = false;
inline constexpr OrganizeImportsCollation OrganizeImportsCollationUnicode = true;

// OrganizeImportsCaseFirst — userpreferences.go:280 (`type OrganizeImportsCaseFirst int`)
using OrganizeImportsCaseFirst = int;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstFalse = 0;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstLower = 1;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstUpper = 2;

// OrganizeImportsTypeOrder — userpreferences.go:288 (`type OrganizeImportsTypeOrder int`)
using OrganizeImportsTypeOrder = int;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderAuto = 0;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderLast = 1;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderInline = 2;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderFirst = 3;

// InlayHintsPreferences — userpreferences.go:209
struct InlayHintsPreferences {
	IncludeInlayParameterNameHints IncludeInlayParameterNameHints;                        // raw:"includeInlayParameterNameHints" config:"inlayHints.parameterNames.enabled"
	Tristate IncludeInlayParameterNameHintsWhenArgumentMatchesName = Tristate::Unknown;  // raw:"includeInlayParameterNameHintsWhenArgumentMatchesName" config:"inlayHints.parameterNames.suppressWhenArgumentMatchesName,invert"
	Tristate IncludeInlayFunctionParameterTypeHints = Tristate::Unknown;                 // raw:"includeInlayFunctionParameterTypeHints" config:"inlayHints.parameterTypes.enabled"
	Tristate IncludeInlayVariableTypeHints = Tristate::Unknown;                          // raw:"includeInlayVariableTypeHints" config:"inlayHints.variableTypes.enabled"
	Tristate IncludeInlayVariableTypeHintsWhenTypeMatchesName = Tristate::Unknown;       // raw:"includeInlayVariableTypeHintsWhenTypeMatchesName" config:"inlayHints.variableTypes.suppressWhenTypeMatchesName,invert"
	Tristate IncludeInlayPropertyDeclarationTypeHints = Tristate::Unknown;               // raw:"includeInlayPropertyDeclarationTypeHints" config:"inlayHints.propertyDeclarationTypes.enabled"
	Tristate IncludeInlayFunctionLikeReturnTypeHints = Tristate::Unknown;                // raw:"includeInlayFunctionLikeReturnTypeHints" config:"inlayHints.functionLikeReturnTypes.enabled"
	Tristate IncludeInlayEnumMemberValueHints = Tristate::Unknown;                       // raw:"includeInlayEnumMemberValueHints" config:"inlayHints.enumMemberValues.enabled"
};

// CodeLensUserPreferences — userpreferences.go:220
struct CodeLensUserPreferences {
	Tristate ReferencesCodeLensEnabled = Tristate::Unknown;                     // raw:"referencesCodeLensEnabled" config:"referencesCodeLens.enabled"
	Tristate ImplementationsCodeLensEnabled = Tristate::Unknown;                // raw:"implementationsCodeLensEnabled" config:"implementationsCodeLens.enabled"
	Tristate ReferencesCodeLensShowOnAllFunctions = Tristate::Unknown;          // raw:"referencesCodeLensShowOnAllFunctions" config:"referencesCodeLens.showOnAllFunctions"
	Tristate ImplementationsCodeLensShowOnInterfaceMethods = Tristate::Unknown; // raw:"implementationsCodeLensShowOnInterfaceMethods" config:"implementationsCodeLens.showOnInterfaceMethods"
	Tristate ImplementationsCodeLensShowOnAllClassMethods = Tristate::Unknown;  // raw:"implementationsCodeLensShowOnAllClassMethods" config:"implementationsCodeLens.showOnAllClassMethods"
};

// UserPreferences — userpreferences.go:47. Field tags preserved as comments;
// the Go `raw`/`config`/`fallbackConfig` tags are compiled into the fieldInfo
// table in userpreferences.cpp.
struct UserPreferences {
	FormatCodeSettings FormatCodeSettings;

	QuotePreference QuotePreference;                                          // raw:"quotePreference" config:"preferences.quoteStyle"
	Tristate LazyConfiguredProjectsFromExternalProject = Tristate::Unknown;   // raw:"lazyConfiguredProjectsFromExternalProject" // !!!

	// A positive integer indicating the maximum length of a hover text before it is truncated.
	//
	// Default: `500`
	int MaximumHoverLength = 0;                                               // raw:"maximumHoverLength" // !!!

	// ------- Completions -------

	// If enabled, TypeScript will search through all external modules' exports and add them to the completions list.
	// This affects lone identifier completions but not completions on the right hand side of `obj.`.
	Tristate IncludeCompletionsForModuleExports = Tristate::Unknown;          // raw:"includeCompletionsForModuleExports" config:"suggest.autoImports"
	// Enables auto-import-style completions on partially-typed import statements. E.g., allows
	// `import write|` to be completed to `import { writeFile } from "fs"`.
	Tristate IncludeCompletionsForImportStatements = Tristate::Unknown;       // raw:"includeCompletionsForImportStatements" config:"suggest.includeCompletionsForImportStatements"
	// Unless this option is `false`,  member completion lists triggered with `.` will include entries
	// on potentially-null and potentially-undefined values, with insertion text to replace
	// preceding `.` tokens with `?.`.
	Tristate IncludeAutomaticOptionalChainCompletions = Tristate::Unknown;    // raw:"includeAutomaticOptionalChainCompletions" config:"suggest.includeAutomaticOptionalChainCompletions"
	// If enabled, completions for class members (e.g. methods and properties) will include
	// a whole declaration for the member.
	// E.g., `class A { f| }` could be completed to `class A { foo(): number {} }`, instead of
	// `class A { foo }`.
	Tristate IncludeCompletionsWithClassMemberSnippets = Tristate::Unknown;   // raw:"includeCompletionsWithClassMemberSnippets" config:"suggest.classMemberSnippets.enabled"
	// If enabled, object literal methods will have a method declaration completion entry in addition
	// to the regular completion entry containing just the method name.
	// E.g., `const objectLiteral: T = { f| }` could be completed to `const objectLiteral: T = { foo(): void {} }`,
	// in addition to `const objectLiteral: T = { foo }`.
	Tristate IncludeCompletionsWithObjectLiteralMethodSnippets = Tristate::Unknown; // raw:"includeCompletionsWithObjectLiteralMethodSnippets" config:"suggest.objectLiteralMethodSnippets.enabled"
	JsxAttributeCompletionStyle JsxAttributeCompletionStyle;                        // raw:"jsxAttributeCompletionStyle" config:"preferences.jsxAttributeCompletionStyle"
	Tristate EnableAutoClosingTags = Tristate::Unknown;                             // raw:"autoClosingTags" config:"autoClosingTags.enabled" fallbackConfig:"autoClosingTags"
	Tristate EnableJSDocCompletions = Tristate::Unknown;                            // raw:"completeJSDocs" config:"suggest.jsdoc.enabled" fallbackConfig:"suggest.completeJSDocs"
	Tristate GenerateReturnInDocTemplate = Tristate::Unknown;                       // raw:"generateReturnInDocTemplate" config:"suggest.jsdoc.generateReturns"

	// ------- AutoImports --------

	std::string ImportModuleSpecifierPreference;              // modulespecifiers.ImportModuleSpecifierPreference; raw:"importModuleSpecifierPreference" config:"preferences.importModuleSpecifier" // !!!
	// Determines whether we import `foo/index.ts` as "foo", "foo/index", or "foo/index.js"
	std::string ImportModuleSpecifierEnding;                  // modulespecifiers.ImportModuleSpecifierEndingPreference; raw:"importModuleSpecifierEnding" config:"preferences.importModuleSpecifierEnding" // !!!
	std::vector<std::string> AutoImportSpecifierExcludeRegexes; // raw:"autoImportSpecifierExcludeRegexes" config:"preferences.autoImportSpecifierExcludeRegexes" // !!!
	std::vector<std::string> AutoImportFileExcludePatterns;     // raw:"autoImportFileExcludePatterns" config:"preferences.autoImportFileExcludePatterns"
	Tristate AutoImportEntrypointDirectorySearch = Tristate::Unknown; // raw:"autoImportEntrypointDirectorySearch" config:"preferences.autoImportEntrypointDirectorySearch"
	Tristate PreferTypeOnlyAutoImports = Tristate::Unknown;           // raw:"preferTypeOnlyAutoImports" config:"preferences.preferTypeOnlyAutoImports"

	// ------- OrganizeImports -------

	// Indicates which deterministic preset should be used to sort imports.
	// "auto" detects the existing ordinal case sensitivity where possible.
	OrganizeImportsSort OrganizeImportsSort = OrganizeImportsSortAuto; // raw:"organizeImportsSort" config:"preferences.organizeImports.sort" // !!!
	// Indicates whether imports should be organized in a case-insensitive manner.
	//
	// Default: TSUnknown ("auto" in strada), will perform detection
	Tristate OrganizeImportsIgnoreCase = Tristate::Unknown;            // raw:"organizeImportsIgnoreCase" config:"preferences.organizeImports.caseSensitivity" // !!!
	// Indicates whether imports should be organized via an "ordinal" (binary) comparison using the numeric value of their
	// code points, or via "unicode" natural sorting. This implementation is locale-agnostic and approximates the practical
	// import-sorting behavior rather than the full Unicode Collation Algorithm.
	//
	// Default: Ordinal
	OrganizeImportsCollation OrganizeImportsCollation = OrganizeImportsCollationOrdinal; // raw:"organizeImportsCollation" config:"preferences.organizeImports.unicodeCollation" // !!!
	// Indicates the locale to use for "unicode" collation in legacy clients. This is accepted for compatibility, but
	// currently ignored because organize-import sorting is deterministic and locale-agnostic.
	//
	// This preference is ignored if organizeImportsCollation is not `unicode`.
	//
	// Default: `"en"`
	std::string OrganizeImportsLocale;                                 // raw:"organizeImportsLocale" config:"preferences.organizeImports.locale" // !!!
	// Indicates whether numeric collation should be used for digit sequences in strings. When `true`, will collate
	// strings such that `a1z < a2z < a100z`. When `false`, will collate strings such that `a1z < a100z < a2z`.
	//
	// This preference is ignored if organizeImportsCollation is not `unicode`.
	//
	// Default: `false`
	Tristate OrganizeImportsNumericCollation = Tristate::Unknown;      // raw:"organizeImportsNumericCollation" config:"preferences.organizeImports.numericCollation" // !!!
	// Indicates whether accents and other diacritic marks are considered unequal for the purpose of sorting.
	//
	// This preference is ignored if organizeImportsCollation is not `unicode`.
	//
	// Default: `true`
	Tristate OrganizeImportsAccentCollation = Tristate::Unknown;       // raw:"organizeImportsAccentCollation" config:"preferences.organizeImports.accentCollation" // !!!
	// Indicates whether upper case or lower case should sort first.
	//
	// This permission is ignored if:
	//	- organizeImportsCollation is not `unicode`
	//	- organizeImportsIgnoreCase is `true`
	//	- organizeImportsIgnoreCase is `auto` and the auto-detected case sensitivity is case-insensitive.
	//
	// Default: `false`
	OrganizeImportsCaseFirst OrganizeImportsCaseFirst = OrganizeImportsCaseFirstFalse; // raw:"organizeImportsCaseFirst" config:"preferences.organizeImports.caseFirst" // !!!
	// Indicates where named type-only imports should sort. "inline" sorts named imports without regard to if the import is type-only.
	//
	// Default: `auto`, which defaults to `last`
	OrganizeImportsTypeOrder OrganizeImportsTypeOrder = OrganizeImportsTypeOrderAuto;  // raw:"organizeImportsTypeOrder" config:"preferences.organizeImports.typeOrder" // !!!

	// ------- MoveToFile -------

	Tristate AllowTextChangesInNewFiles = Tristate::Unknown;           // raw:"allowTextChangesInNewFiles" // !!!

	// ------- Rename -------

	Tristate UseAliasesForRename = Tristate::Unknown;                  // raw:"providePrefixAndSuffixTextForRename" config:"preferences.useAliasesForRenames"
	Tristate AllowRenameOfImportPath = Tristate::Unknown;              // raw:"allowRenameOfImportPath"

	// ------- CodeFixes/Refactors -------

	Tristate ProvideRefactorNotApplicableReason = Tristate::Unknown;   // raw:"provideRefactorNotApplicableReason" // !!!

	// ------- InlayHints -------

	InlayHintsPreferences InlayHints;

	// ------- CodeLens -------

	CodeLensUserPreferences CodeLens;

	// ------- Definition -------

	bool PreferGoToSourceDefinition = false;                           // raw:"preferGoToSourceDefinition"

	// ------- Symbols -------

	Tristate ExcludeLibrarySymbolsInNavTo = Tristate::Unknown;         // raw:"excludeLibrarySymbolsInNavTo" config:"workspaceSymbols.excludeLibrarySymbols"
	WorkspaceSymbolsScope WorkspaceSymbolsScope;                       // config:"workspaceSymbols.scope"

	// ------- Misc -------

	Tristate EnableFormatting = Tristate::Unknown;                     // raw:"formatEnabled" config:"format.enabled" fallbackConfig:"format.enable"
	Tristate EnableValidation = Tristate::Unknown;                     // raw:"validateEnabled" config:"validate.enabled" fallbackConfig:"validate.enable"
	Tristate DisableSuggestions = Tristate::Unknown;                   // raw:"disableSuggestions"          // !!!
	Tristate DisableLineTextInReferences = Tristate::Unknown;          // raw:"disableLineTextInReferences" // !!!
	Tristate DisplayPartsForJSDoc = Tristate::Unknown;                 // raw:"displayPartsForJSDoc"        // !!!
	Tristate ReportStyleChecksAsWarnings = Tristate::Unknown;          // raw:"reportStyleChecksAsWarnings" config:"reportStyleChecksAsWarnings"
	std::string Locale;                                                // config:"locale"

	// ------- ATA -------

	// DisableAutomaticTypeAcquisition is the deprecated setting from typescript.disableAutomaticTypeAcquisition.
	Tristate DisableAutomaticTypeAcquisition = Tristate::Unknown;      // raw:"disableAutomaticTypeAcquisition" config:"disableAutomaticTypeAcquisition"
	// AutomaticTypeAcquisitionEnabled is the unified setting from tsserver.automaticTypeAcquisition.enabled under the js/ts section.
	// When set, it takes precedence over DisableAutomaticTypeAcquisition.
	Tristate AutomaticTypeAcquisitionEnabled = Tristate::Unknown;      // raw:"automaticTypeAcquisitionEnabled" config:"tsserver.automaticTypeAcquisition.enabled"
	// TODO: add tsserver.web.typeAcquisition.enabled under the js/ts section for the web variant when web support is implemented.

	// ------- Project Configuration -------

	// CustomConfigFileName specifies a custom config file name to use before defaulting to tsconfig.json/jsconfig.json.
	std::string CustomConfigFileName;                                  // raw:"customConfigFileName" config:"customConfigFileName"

	// IsATADisabled returns whether Automatic Type Acquisition is disabled based on user preferences.
	// It checks the unified setting (tsserver.automaticTypeAcquisition.enabled) first,
	// then falls back to the deprecated setting (disableAutomaticTypeAcquisition).
	bool IsATADisabled() const;

	// userpreferences.go:877
	tsc::modulespecifiers::UserPreferences ModuleSpecifierPreferences() const;
	// userpreferences.go:885
	std::unique_ptr<tsc::vfs::vfsmatch::SpecMatcher> ParsedAutoImportFileExcludePatterns(bool useCaseSensitiveFileNames) const;
	// userpreferences.go:889
	bool IsModuleSpecifierExcluded(std::string_view moduleSpecifier) const;

	// userpreferences.go:793
	std::string marshalJSONTo(tsc::json::Encoder& enc) const;
	// userpreferences.go:865
	std::string unmarshalJSONFrom(tsc::json::Decoder& dec);
};

// userpreferences.go:16
UserPreferences NewDefaultUserPreferences();
// userpreferences.go:893
UserPreferences ParseUserPreferences(const JsonObject& items);

// --- asi.go ---

// asi.go:12
bool PositionIsASICandidate(int pos, Node* context, SourceFile* file);
// asi.go:21
bool SyntaxMayBeASICandidate(Kind kind);
// asi.go:28
bool SyntaxRequiresTrailingCommaOrSemicolonOrASI(Kind kind);
// asi.go:48
bool SyntaxRequiresTrailingFunctionBlockOrSemicolonOrASI(Kind kind);
// asi.go:53
bool SyntaxRequiresTrailingModuleBlockOrSemicolonOrASI(Kind kind);
// asi.go:58
bool SyntaxRequiresTrailingSemicolonOrASI(Kind kind);
// asi.go:63
bool NodeIsASICandidate(Node* node, SourceFile* file);

// --- children.go ---

// children.go:15
Node* GetLastChild(Node* node, SourceFile* sourceFile);
// children.go:65
Node* GetLastToken(Node* node, SourceFile* sourceFile);
// children.go:97
Node* GetLastVisitedChild(Node* node, SourceFile* sourceFile);
// children.go:113
Node* GetFirstToken(Node* node, SourceFile* sourceFile);
// children.go:131
void AssertHasRealPosition(Node* node);

// --- completednode.go ---

// completednode.go:12
bool PositionBelongsToNode(Node* candidate, int position, SourceFile* file);
// completednode.go:21
bool IsCompletedNode(Node* n, SourceFile* sourceFile);

// --- organizeimports.go ---

// Comparer standing in for Go `func(a, b string) int`.
using StringComparer = std::function<int(std::string_view a, std::string_view b)>;
// Comparer standing in for Go `func(s1, s2 *ast.Node) int`.
using NodeComparer = std::function<int(Node*, Node*)>;

// organizeimports.go:18
std::vector<Node*> FilterImportDeclarations(const std::vector<Node*>& statements);
// organizeimports.go:25 — returns (comparersToTest, typeOrdersToTest)
std::pair<std::vector<StringComparer>, std::vector<OrganizeImportsTypeOrder>> GetDetectionLists(const UserPreferences& preferences);
// organizeimports.go:50
OrganizeImportsSort ResolveOrganizeImportsSort(const UserPreferences& preferences);
// organizeimports.go:299
std::string GetExternalModuleName(Node* specifier);
// organizeimports.go:307
int CompareModuleSpecifiers(Node* m1, Node* m2, const StringComparer& comparer);
// organizeimports.go:370
int CompareImportsOrRequireStatements(Node* s1, Node* s2, const StringComparer& comparer);
// organizeimports.go:400 — nil comparer is allowed like Go.
NodeComparer GetNamedImportSpecifierComparer(const UserPreferences& preferences, StringComparer comparer);
// organizeimports.go:414
int GetImportSpecifierInsertionIndex(const std::vector<Node*>& sortedImports, Node* newImport, const NodeComparer& comparer);
// organizeimports.go:421 — comparer: func(a, b *ast.Statement) int
int GetImportDeclarationInsertIndex(const std::vector<Node*>& sortedImports, Node* newImport, const NodeComparer& comparer);
// organizeimports.go:428 — returns (comparer, isSorted)
std::pair<StringComparer, bool> GetOrganizeImportsStringComparerWithDetection(const std::vector<Node*>& originalImportDecls, const UserPreferences& preferences);
// organizeimports.go:454 — returns (comparer, typeOrder, found); comparer == nullptr when !found
std::tuple<StringComparer, OrganizeImportsTypeOrder, bool> DetectNamedImportOrganizationBySort(
	const std::vector<Node*>& originalGroups,
	const std::vector<StringComparer>& comparersToTest,
	const std::vector<OrganizeImportsTypeOrder>& typesToTest);
// organizeimports.go:603 — returns (comparer, isSorted)
std::pair<StringComparer, bool> DetectModuleSpecifierCaseBySort(const std::vector<std::vector<Node*>>& importDeclsByGroup, const std::vector<StringComparer>& comparersToTest);
// organizeimports.go:662 — returns (specifierComparer, isSorted)
std::pair<NodeComparer, Tristate> GetNamedImportSpecifierComparerWithDetection(Node* importDecl, SourceFile* sourceFile, const UserPreferences& preferences);

// --- symbol_display.go ---

// ScriptElementKind — symbol_display.go:10
enum class ScriptElementKind : int32_t {
	Unknown = 0,
	Warning,
	// predefined type (void) or keyword (class)
	Keyword,
	// top level script node
	ScriptElement,
	// module foo {}
	ModuleElement,
	// class X {}
	ClassElement,
	// var x = class X {}
	LocalClassElement,
	// interface Y {}
	InterfaceElement,
	// type T = ...
	TypeElement,
	// enum E {}
	EnumElement,
	EnumMemberElement,
	// Inside module and script only.
	// const v = ...
	VariableElement,
	// Inside function.
	LocalVariableElement,
	// using foo = ...
	VariableUsingElement,
	// await using foo = ...
	VariableAwaitUsingElement,
	// Inside module and script only.
	// function f() {}
	FunctionElement,
	// Inside function.
	LocalFunctionElement,
	// class X { [public|private]* foo() {} }
	MemberFunctionElement,
	// class X { [public|private]* [get|set] foo:number; }
	MemberGetAccessorElement,
	MemberSetAccessorElement,
	// class X { [public|private]* foo:number; }
	// interface Y { foo:number; }
	MemberVariableElement,
	// class X { [public|private]* accessor foo: number; }
	MemberAccessorVariableElement,
	// class X { constructor() { } }
	// class X { static { } }
	ConstructorImplementationElement,
	// interface Y { ():number; }
	CallSignatureElement,
	// interface Y { []:number; }
	IndexSignatureElement,
	// interface Y { new():Y; }
	ConstructSignatureElement,
	// function foo(*Y*: string)
	ParameterElement,
	TypeParameterElement,
	PrimitiveTypeElement,
	LabelElement,
	AliasElement,
	ConstElement,
	LetElement,
	DirectoryElement,
	ExternalModuleNameElement,
	// String literal
	StringElement,
	// Jsdoc @link: in `{@link C link text}`, the before and after text "{@link " and "}"
	LinkElement,
	// Jsdoc @link: in `{@link C link text}`, the entity name "C"
	LinkNameElement,
	// Jsdoc @link: in `{@link C link text}`, the link text "link text"
	LinkTextElement,
};
// Go const names (ScriptElementKind*) kept as aliases for call sites.
inline constexpr ScriptElementKind ScriptElementKindUnknown = ScriptElementKind::Unknown;
inline constexpr ScriptElementKind ScriptElementKindWarning = ScriptElementKind::Warning;
inline constexpr ScriptElementKind ScriptElementKindKeyword = ScriptElementKind::Keyword;
inline constexpr ScriptElementKind ScriptElementKindScriptElement = ScriptElementKind::ScriptElement;
inline constexpr ScriptElementKind ScriptElementKindModuleElement = ScriptElementKind::ModuleElement;
inline constexpr ScriptElementKind ScriptElementKindClassElement = ScriptElementKind::ClassElement;
inline constexpr ScriptElementKind ScriptElementKindLocalClassElement = ScriptElementKind::LocalClassElement;
inline constexpr ScriptElementKind ScriptElementKindInterfaceElement = ScriptElementKind::InterfaceElement;
inline constexpr ScriptElementKind ScriptElementKindTypeElement = ScriptElementKind::TypeElement;
inline constexpr ScriptElementKind ScriptElementKindEnumElement = ScriptElementKind::EnumElement;
inline constexpr ScriptElementKind ScriptElementKindEnumMemberElement = ScriptElementKind::EnumMemberElement;
inline constexpr ScriptElementKind ScriptElementKindVariableElement = ScriptElementKind::VariableElement;
inline constexpr ScriptElementKind ScriptElementKindLocalVariableElement = ScriptElementKind::LocalVariableElement;
inline constexpr ScriptElementKind ScriptElementKindVariableUsingElement = ScriptElementKind::VariableUsingElement;
inline constexpr ScriptElementKind ScriptElementKindVariableAwaitUsingElement = ScriptElementKind::VariableAwaitUsingElement;
inline constexpr ScriptElementKind ScriptElementKindFunctionElement = ScriptElementKind::FunctionElement;
inline constexpr ScriptElementKind ScriptElementKindLocalFunctionElement = ScriptElementKind::LocalFunctionElement;
inline constexpr ScriptElementKind ScriptElementKindMemberFunctionElement = ScriptElementKind::MemberFunctionElement;
inline constexpr ScriptElementKind ScriptElementKindMemberGetAccessorElement = ScriptElementKind::MemberGetAccessorElement;
inline constexpr ScriptElementKind ScriptElementKindMemberSetAccessorElement = ScriptElementKind::MemberSetAccessorElement;
inline constexpr ScriptElementKind ScriptElementKindMemberVariableElement = ScriptElementKind::MemberVariableElement;
inline constexpr ScriptElementKind ScriptElementKindMemberAccessorVariableElement = ScriptElementKind::MemberAccessorVariableElement;
inline constexpr ScriptElementKind ScriptElementKindConstructorImplementationElement = ScriptElementKind::ConstructorImplementationElement;
inline constexpr ScriptElementKind ScriptElementKindCallSignatureElement = ScriptElementKind::CallSignatureElement;
inline constexpr ScriptElementKind ScriptElementKindIndexSignatureElement = ScriptElementKind::IndexSignatureElement;
inline constexpr ScriptElementKind ScriptElementKindConstructSignatureElement = ScriptElementKind::ConstructSignatureElement;
inline constexpr ScriptElementKind ScriptElementKindParameterElement = ScriptElementKind::ParameterElement;
inline constexpr ScriptElementKind ScriptElementKindTypeParameterElement = ScriptElementKind::TypeParameterElement;
inline constexpr ScriptElementKind ScriptElementKindPrimitiveType = ScriptElementKind::PrimitiveTypeElement;
inline constexpr ScriptElementKind ScriptElementKindLabel = ScriptElementKind::LabelElement;
inline constexpr ScriptElementKind ScriptElementKindAlias = ScriptElementKind::AliasElement;
inline constexpr ScriptElementKind ScriptElementKindConstElement = ScriptElementKind::ConstElement;
inline constexpr ScriptElementKind ScriptElementKindLetElement = ScriptElementKind::LetElement;
inline constexpr ScriptElementKind ScriptElementKindDirectory = ScriptElementKind::DirectoryElement;
inline constexpr ScriptElementKind ScriptElementKindExternalModuleName = ScriptElementKind::ExternalModuleNameElement;
inline constexpr ScriptElementKind ScriptElementKindString = ScriptElementKind::StringElement;
inline constexpr ScriptElementKind ScriptElementKindLink = ScriptElementKind::LinkElement;
inline constexpr ScriptElementKind ScriptElementKindLinkName = ScriptElementKind::LinkNameElement;
inline constexpr ScriptElementKind ScriptElementKindLinkText = ScriptElementKind::LinkTextElement;

// ScriptElementKindModifier — symbol_display.go:85
enum class ScriptElementKindModifier : uint32_t;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierNone = ScriptElementKindModifier(0);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierPublic = ScriptElementKindModifier(1 << 1);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierPrivate = ScriptElementKindModifier(1 << 2);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierProtected = ScriptElementKindModifier(1 << 3);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierExported = ScriptElementKindModifier(1 << 4);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierAmbient = ScriptElementKindModifier(1 << 5);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierStatic = ScriptElementKindModifier(1 << 6);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierAbstract = ScriptElementKindModifier(1 << 7);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierOptional = ScriptElementKindModifier(1 << 8);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierDeprecated = ScriptElementKindModifier(1 << 9);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierDts = ScriptElementKindModifier(1 << 10);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierTs = ScriptElementKindModifier(1 << 11);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierTsx = ScriptElementKindModifier(1 << 12);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierJs = ScriptElementKindModifier(1 << 13);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierJsx = ScriptElementKindModifier(1 << 14);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierJson = ScriptElementKindModifier(1 << 15);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierDmts = ScriptElementKindModifier(1 << 16);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierMts = ScriptElementKindModifier(1 << 17);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierMjs = ScriptElementKindModifier(1 << 18);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierDcts = ScriptElementKindModifier(1 << 19);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierCts = ScriptElementKindModifier(1 << 20);
inline constexpr ScriptElementKindModifier ScriptElementKindModifierCjs = ScriptElementKindModifier(1 << 21);

constexpr ScriptElementKindModifier operator|(ScriptElementKindModifier a, ScriptElementKindModifier b) {
	return ScriptElementKindModifier(uint32_t(a) | uint32_t(b));
}
constexpr ScriptElementKindModifier operator&(ScriptElementKindModifier a, ScriptElementKindModifier b) {
	return ScriptElementKindModifier(uint32_t(a) & uint32_t(b));
}
inline ScriptElementKindModifier& operator|=(ScriptElementKindModifier& a, ScriptElementKindModifier b) {
	return a = a | b;
}
inline ScriptElementKindModifier& operator&=(ScriptElementKindModifier& a, ScriptElementKindModifier b) {
	return a = a & b;
}
constexpr bool operator!(ScriptElementKindModifier a) { return uint32_t(a) == 0; }

// Strings — symbol_display.go:139
collections::Set<std::string> scriptElementKindModifierStrings(ScriptElementKindModifier m);

// FileExtensionKindModifiers — symbol_display.go:149
inline constexpr ScriptElementKindModifier FileExtensionKindModifiers =
	ScriptElementKindModifierDts | ScriptElementKindModifierTs |
	ScriptElementKindModifierTsx | ScriptElementKindModifierJs |
	ScriptElementKindModifierJsx | ScriptElementKindModifierJson |
	ScriptElementKindModifierDmts | ScriptElementKindModifierMts |
	ScriptElementKindModifierMjs | ScriptElementKindModifierDcts |
	ScriptElementKindModifierCts | ScriptElementKindModifierCjs;

// symbol_display.go:162
ScriptElementKind GetSymbolKind(tsc::checker::Checker* typeChecker, Symbol* symbol, Node* location);
// symbol_display.go:349
ScriptElementKindModifier GetSymbolModifiers(tsc::checker::Checker* typeChecker, Symbol* symbol);

// --- utilities.go ---

// utilities.go:14
bool ProbablyUsesSemicolons(SourceFile* sourceFile);
// utilities.go:103
Tristate ShouldUseUriStyleNodeCoreModules(SourceFile* file, tsc::compiler::SimpleProgram* program);
// utilities.go:121
QuotePreference QuotePreferenceFromString(Node* node);
// utilities.go:130
QuotePreference GetQuotePreference(SourceFile* sourceFile, const UserPreferences& preferences);
// utilities.go:150
std::string ModuleSymbolToValidIdentifier(Symbol* moduleSymbol, bool forceCapitalize);
// utilities.go:154
std::string ModuleSpecifierToValidIdentifier(std::string_view moduleSpecifier, bool forceCapitalize);
// utilities.go:190
bool IsNonContextualKeyword(Kind token);

} // namespace tsc::ls::lsutil

// AST predicates not yet in the ast slice — faithful ports of ast/utilities.go
// (and ast_generated.go) needed by this package, namespaced to detail to avoid
// ODR collisions when the ast slice lands them under tsc::.
namespace tsc::ls::lsutil::detail {

// utilities.go:3848 — ast.hasComment (unexported; guards Node::commentList,
// which is unreachable for kinds not in this list).
inline bool hasComment(Kind kind) {
	switch (kind) {
	case Kind::JSDoc:
	case Kind::JSDocUnknownTag:
	case Kind::JSDocAugmentsTag:
	case Kind::JSDocImplementsTag:
	case Kind::JSDocDeprecatedTag:
	case Kind::JSDocPublicTag:
	case Kind::JSDocPrivateTag:
	case Kind::JSDocProtectedTag:
	case Kind::JSDocReadonlyTag:
	case Kind::JSDocOverrideTag:
	case Kind::JSDocCallbackTag:
	case Kind::JSDocOverloadTag:
	case Kind::JSDocParameterTag:
	case Kind::JSDocPropertyTag:
	case Kind::JSDocReturnTag:
	case Kind::JSDocThisTag:
	case Kind::JSDocTypeTag:
	case Kind::JSDocTemplateTag:
	case Kind::JSDocTypedefTag:
	case Kind::JSDocSeeTag:
	case Kind::JSDocThrowsTag:
	case Kind::JSDocSatisfiesTag:
	case Kind::JSDocImportTag:
		return true;
	default:
		return false;
	}
}

// utilities.go:3165 — ast.IsJSDocSingleCommentNode. In Strada, if a JSDoc node
// has a single comment, that comment is represented as a string property as a
// simplification, and therefore that comment is not visited by forEachChild.
inline bool isJSDocSingleCommentNode(Node* node) {
	return hasComment(node->kind) && node->commentList() != nullptr &&
		node->commentList()->nodes.size() == 1;
}

// ast_generated.go — ast.IsTokenKind
inline bool isTokenKind(Kind token) {
	return KindFirstToken <= token && token <= KindLastToken;
}

// utilities.go:713 — ast.IsFunctionBlock
inline bool isFunctionBlock(Node* node) {
	return node != nullptr && node->kind == Kind::Block && node->parent != nullptr &&
		isFunctionLike(node->parent);
}

// utilities.go:3028 — ast.IsContextualKeyword
inline bool isContextualKeyword(Kind token) {
	return KindFirstContextualKeyword <= token && token <= KindLastContextualKeyword;
}

// utilities.go:1669 — ast.TryGetAmbientModuleNameFromSymbolName
inline std::pair<std::string, bool> tryGetAmbientModuleNameFromSymbolName(
    std::string_view s) {
	if (!s.empty() && s.front() == '"' && s.back() == '"') {
		if (s.size() < 2) {
			// Go: s[1:len(s)-1] — slice bounds out of range when len(s) < 2.
			TSC_UNREACHABLE("slice bounds out of range");
		}
		return {std::string(s.substr(1, s.size() - 2)), true};
	}

	const std::string patternPrefix =
		std::string(1, kInternalSymbolNamePrefix) + "\"";
	if (s.compare(0, patternPrefix.size(), patternPrefix) != 0) {
		return {"", false};
	}
	std::string_view rest = s.substr(patternPrefix.size());
	size_t markerIndex = rest.rfind("\"pattern@");
	if (markerIndex == std::string_view::npos || markerIndex < 1) {
		return {"", false};
	}
	return {std::string(rest.substr(0, markerIndex)), true};
}

// utilities.go:3127 — ast.IsClassOrTypeElement
inline bool isClassOrTypeElement(Node* node) {
	return isClassElement(node) || isTypeElement(node);
}

// utilities.go:687 — ast.IsStatementButNotDeclaration
inline bool isStatementButNotDeclaration(Node* node) {
	return isStatementKindButNotDeclarationKind(node->kind);
}

// utilities.go:3043 — ast.IsLet
inline bool isLet(Node* node) {
	return (getCombinedNodeFlags(node) & NodeFlagsBlockScoped) == NodeFlagsLet;
}

// utilities.go:1223 — ast.GetJSDocDeprecatedTag
inline Node* getJSDocDeprecatedTag(Node* node) {
	for (Node* jsdoc : node->jsDoc()) {
		NodeList* tags = jsdoc->as<JSDoc>()->Tags;
		if (tags != nullptr) {
			for (Node* tag : tags->nodes) {
				if (tag->kind == Kind::JSDocDeprecatedTag) {
					return tag;
				}
			}
		}
	}
	return nullptr;
}

// utilities.go:1246 — ast.IsDeprecatedDeclarationWithCachedFlags
inline bool isDeprecatedDeclarationWithCachedFlags(Node* declaration, NodeFlags combinedFlags) {
	if ((combinedFlags & NodeFlagsPossiblyContainsDeprecatedTag) == 0) {
		return false;
	}
	// Walk up to find the node that directly has the flag, since JSDoc is
	// attached to that node (e.g. VariableStatement, not VariableDeclaration).
	for (Node* n = declaration; n != nullptr; n = n->parent) {
		if ((n->flags & NodeFlagsPossiblyContainsDeprecatedTag) != 0) {
			return getJSDocDeprecatedTag(n) != nullptr;
		}
	}
	return false;
}

// utilities.go:1240 — ast.IsDeprecatedDeclaration
inline bool isDeprecatedDeclaration(Node* declaration) {
	return isDeprecatedDeclarationWithCachedFlags(declaration, getCombinedNodeFlags(declaration));
}

} // namespace tsc::ls::lsutil::detail
