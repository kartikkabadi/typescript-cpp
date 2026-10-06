// === slice: lsutil ===
// userpreferences.go / formatcodeoptions.go — minimal lsutil declarations
// for the ls slice port. The sibling child package (lsutil slice) owns the
// real implementation; this header carries the data types the ls slice
// touches plus dep-stub declarations for its helpers.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "internal/core/types.h"
#include "internal/modulespecifiers/types.h"
#include "internal/lsp/lsproto/lsproto.h"

namespace tsc {
struct Node;
using Statement = Node;
using Expression = Node;
struct SourceFile;
namespace compiler {
class SimpleProgram;
}
namespace lsutil {

// --- formatcodeoptions.go ---------------------------------------------------

// IndentStyle — formatcodeoptions.go:11.
enum class IndentStyle : int {
	None = 0,
	Block = 1,
	Smart = 2,
};
inline constexpr IndentStyle IndentStyleNone = IndentStyle::None;
inline constexpr IndentStyle IndentStyleBlock = IndentStyle::Block;
inline constexpr IndentStyle IndentStyleSmart = IndentStyle::Smart;

// SemicolonPreference — formatcodeoptions.go:38.
using SemicolonPreference = std::string;
inline const SemicolonPreference SemicolonPreferenceIgnore = "ignore";
inline const SemicolonPreference SemicolonPreferenceInsert = "insert";
inline const SemicolonPreference SemicolonPreferenceRemove = "remove";

// EditorSettings — formatcodeoptions.go:57.
struct EditorSettings {
	int BaseIndentSize = 0;
	int IndentSize = 0;
	int TabSize = 0;
	std::string NewLineCharacter;
	Tristate ConvertTabsToSpaces = Tristate::Unknown;
	// Member name matches its type (Go field `IndentStyle IndentStyle`); the
	// initializer needs the qualified enum name to escape the shadowing.
	lsutil::IndentStyle IndentStyle = lsutil::IndentStyle::Smart;
	Tristate TrimTrailingWhitespace = Tristate::Unknown;
};

// FormatCodeSettings — formatcodeoptions.go:70.
// Go embeds EditorSettings; C++ base class gives the same member access.
struct FormatCodeSettings : EditorSettings {
	Tristate InsertSpaceAfterCommaDelimiter = Tristate::Unknown;
	Tristate InsertSpaceAfterSemicolonInForStatements = Tristate::Unknown;
	Tristate InsertSpaceBeforeAndAfterBinaryOperators = Tristate::Unknown;
	Tristate InsertSpaceAfterConstructor = Tristate::Unknown;
	Tristate InsertSpaceAfterKeywordsInControlFlowStatements = Tristate::Unknown;
	Tristate InsertSpaceAfterFunctionKeywordForAnonymousFunctions = Tristate::Unknown;
	Tristate InsertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis = Tristate::Unknown;
	Tristate InsertSpaceAfterOpeningAndBeforeClosingNonemptyBrackets = Tristate::Unknown;
	Tristate InsertSpaceAfterOpeningAndBeforeClosingNonemptyBraces = Tristate::Unknown;
	Tristate InsertSpaceAfterOpeningAndBeforeClosingEmptyBraces = Tristate::Unknown;
	Tristate InsertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces = Tristate::Unknown;
	Tristate InsertSpaceAfterOpeningAndBeforeClosingJsxExpressionBraces = Tristate::Unknown;
	Tristate InsertSpaceAfterTypeAssertion = Tristate::Unknown;
	Tristate InsertSpaceBeforeFunctionParenthesis = Tristate::Unknown;
	Tristate PlaceOpenBraceOnNewLineForFunctions = Tristate::Unknown;
	Tristate PlaceOpenBraceOnNewLineForControlBlocks = Tristate::Unknown;
	Tristate InsertSpaceBeforeTypeAnnotation = Tristate::Unknown;
	Tristate IndentMultiLineObjectLiteralBeginningOnBlankLine = Tristate::Unknown;
	SemicolonPreference Semicolons;
	Tristate IndentSwitchCase = Tristate::Unknown;
};

// GetDefaultFormatCodeSettings — formatcodeoptions.go:113.
FormatCodeSettings GetDefaultFormatCodeSettings();

// --- userpreferences.go ------------------------------------------------------

// QuotePreference — userpreferences.go:230.
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

// OrganizeImportsSort — userpreferences.go:263.
using OrganizeImportsSort = int;
inline constexpr OrganizeImportsSort OrganizeImportsSortAuto = 0;
inline constexpr OrganizeImportsSort OrganizeImportsSortOrdinal = 1;
inline constexpr OrganizeImportsSort OrganizeImportsSortOrdinalIgnoreCase = 2;
inline constexpr OrganizeImportsSort OrganizeImportsSortNatural = 3;
inline constexpr OrganizeImportsSort OrganizeImportsSortNaturalIgnoreCase = 4;

// OrganizeImportsCollation — userpreferences.go.
using OrganizeImportsCollation = bool;
inline constexpr OrganizeImportsCollation OrganizeImportsCollationOrdinal = false;
inline constexpr OrganizeImportsCollation OrganizeImportsCollationUnicode = true;

// OrganizeImportsCaseFirst — userpreferences.go.
using OrganizeImportsCaseFirst = int;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstFalse = 0;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstLower = 1;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstUpper = 2;

// OrganizeImportsTypeOrder — userpreferences.go:288.
using OrganizeImportsTypeOrder = int;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderAuto = 0;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderLast = 1;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderInline = 2;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderFirst = 3;

// InlayHintsPreferences — userpreferences.go.
struct InlayHintsPreferences {
	IncludeInlayParameterNameHints IncludeInlayParameterNameHints;
	Tristate IncludeInlayParameterNameHintsWhenArgumentMatchesName = Tristate::Unknown;
	Tristate IncludeInlayFunctionParameterTypeHints = Tristate::Unknown;
	Tristate IncludeInlayVariableTypeHints = Tristate::Unknown;
	Tristate IncludeInlayVariableTypeHintsWhenTypeMatchesName = Tristate::Unknown;
	Tristate IncludeInlayPropertyDeclarationTypeHints = Tristate::Unknown;
	Tristate IncludeInlayFunctionLikeReturnTypeHints = Tristate::Unknown;
	Tristate IncludeInlayEnumMemberValueHints = Tristate::Unknown;
};

// CodeLensUserPreferences — userpreferences.go.
struct CodeLensUserPreferences {
	Tristate ReferencesCodeLensEnabled = Tristate::Unknown;
	Tristate ImplementationsCodeLensEnabled = Tristate::Unknown;
	Tristate ReferencesCodeLensShowOnAllFunctions = Tristate::Unknown;
	Tristate ImplementationsCodeLensShowOnInterfaceMethods = Tristate::Unknown;
	Tristate ImplementationsCodeLensShowOnAllClassMethods = Tristate::Unknown;
};

// UserPreferences — userpreferences.go:47.
struct UserPreferences {
	lsutil::FormatCodeSettings FormatCodeSettings;
	QuotePreference QuotePreference;
	Tristate LazyConfiguredProjectsFromExternalProject = Tristate::Unknown;
	int MaximumHoverLength = 0;
	Tristate IncludeCompletionsForModuleExports = Tristate::Unknown;
	Tristate IncludeCompletionsForImportStatements = Tristate::Unknown;
	Tristate IncludeAutomaticOptionalChainCompletions = Tristate::Unknown;
	Tristate IncludeCompletionsWithClassMemberSnippets = Tristate::Unknown;
	Tristate IncludeCompletionsWithObjectLiteralMethodSnippets = Tristate::Unknown;
	JsxAttributeCompletionStyle JsxAttributeCompletionStyle;
	Tristate EnableAutoClosingTags = Tristate::Unknown;
	Tristate EnableJSDocCompletions = Tristate::Unknown;
	Tristate GenerateReturnInDocTemplate = Tristate::Unknown;
	modulespecifiers::ImportModuleSpecifierPreference ImportModuleSpecifierPreference;
	modulespecifiers::ImportModuleSpecifierEndingPreference ImportModuleSpecifierEnding;
	std::vector<std::string> AutoImportSpecifierExcludeRegexes;
	std::vector<std::string> AutoImportFileExcludePatterns;
	Tristate AutoImportEntrypointDirectorySearch = Tristate::Unknown;
	Tristate PreferTypeOnlyAutoImports = Tristate::Unknown;
	OrganizeImportsSort OrganizeImportsSort = OrganizeImportsSortAuto;
	Tristate OrganizeImportsIgnoreCase = Tristate::Unknown;
	OrganizeImportsCollation OrganizeImportsCollation = OrganizeImportsCollationOrdinal;
	std::string OrganizeImportsLocale;
	Tristate OrganizeImportsNumericCollation = Tristate::Unknown;
	Tristate OrganizeImportsAccentCollation = Tristate::Unknown;
	OrganizeImportsCaseFirst OrganizeImportsCaseFirst = OrganizeImportsCaseFirstFalse;
	OrganizeImportsTypeOrder OrganizeImportsTypeOrder = OrganizeImportsTypeOrderAuto;
	Tristate AllowTextChangesInNewFiles = Tristate::Unknown;
	Tristate UseAliasesForRename = Tristate::Unknown;
	Tristate AllowRenameOfImportPath = Tristate::Unknown;
	Tristate ProvideRefactorNotApplicableReason = Tristate::Unknown;
	InlayHintsPreferences InlayHints;
	CodeLensUserPreferences CodeLens;
	bool PreferGoToSourceDefinition = false;
	Tristate ExcludeLibrarySymbolsInNavTo = Tristate::Unknown;
	WorkspaceSymbolsScope WorkspaceSymbolsScope;
	Tristate EnableFormatting = Tristate::Unknown;
	Tristate EnableValidation = Tristate::Unknown;
	Tristate DisableSuggestions = Tristate::Unknown;
	Tristate DisableLineTextInReferences = Tristate::Unknown;
	Tristate DisplayPartsForJSDoc = Tristate::Unknown;
	Tristate ReportStyleChecksAsWarnings = Tristate::Unknown;
	std::string Locale;
	Tristate DisableAutomaticTypeAcquisition = Tristate::Unknown;
	Tristate AutomaticTypeAcquisitionEnabled = Tristate::Unknown;
	std::string CustomConfigFileName;

	// IsATADisabled — userpreferences.go.
	bool IsATADisabled() const {
		if (AutomaticTypeAcquisitionEnabled != Tristate::Unknown) {
			return AutomaticTypeAcquisitionEnabled != Tristate::True;
		}
		return DisableAutomaticTypeAcquisition == Tristate::True;
	}
	// ModuleSpecifierPreferences — userpreferences.go:877.
	modulespecifiers::UserPreferences ModuleSpecifierPreferences() const {
		return modulespecifiers::UserPreferences{
			.ImportModuleSpecifierPreference = ImportModuleSpecifierPreference,
			.ImportModuleSpecifierEnding = ImportModuleSpecifierEnding,
			.AutoImportSpecifierExcludeRegexes = AutoImportSpecifierExcludeRegexes,
		};
	}
};

// NewDefaultUserPreferences — userpreferences.go.
UserPreferences NewDefaultUserPreferences();

// --- symbol_display.go -------------------------------------------------------
using ScriptElementKind = int;
using ScriptElementKindModifier = uint32_t;

// --- organizeimports.go / utilities.go (dep-stubs; lsutil slice owns them) ---

// FilterImportDeclarations — organizeimports.go:18.
std::vector<Statement*> FilterImportDeclarations(
    const std::vector<Statement*>& statements);
// GetDetectionLists — organizeimports.go:25.
std::pair<std::vector<std::function<int(std::string, std::string)>>,
          std::vector<OrganizeImportsTypeOrder>>
GetDetectionLists(UserPreferences preferences);
// ResolveOrganizeImportsSort — organizeimports.go:50.
OrganizeImportsSort ResolveOrganizeImportsSort(UserPreferences preferences);
// GetExternalModuleName — organizeimports.go:299.
std::string GetExternalModuleName(Expression* specifier);
// CompareModuleSpecifiers — organizeimports.go:307.
int CompareModuleSpecifiers(Expression* m1, Expression* m2,
                            const std::function<int(std::string, std::string)>& comparer);
// CompareImportsOrRequireStatements — organizeimports.go:370.
int CompareImportsOrRequireStatements(
    Statement* s1, Statement* s2,
    const std::function<int(std::string, std::string)>& comparer);
// GetNamedImportSpecifierComparer — organizeimports.go:400.
std::function<int(Node*, Node*)> GetNamedImportSpecifierComparer(
    UserPreferences preferences,
    const std::function<int(std::string, std::string)>& comparer);
// DetectNamedImportOrganizationBySort — organizeimports.go:454.
std::tuple<std::function<int(std::string, std::string)>,
           OrganizeImportsTypeOrder, bool>
DetectNamedImportOrganizationBySort(
    const std::vector<Statement*>& originalGroups,
    const std::vector<std::function<int(std::string, std::string)>>& comparersToTest,
    const std::vector<OrganizeImportsTypeOrder>& typesToTest);
// DetectModuleSpecifierCaseBySort — organizeimports.go:603.
std::pair<std::function<int(std::string, std::string)>, bool>
DetectModuleSpecifierCaseBySort(
    const std::vector<std::vector<Statement*>>& importDeclsByGroup,
    const std::vector<std::function<int(std::string, std::string)>>& comparersToTest);
// ShouldUseUriStyleNodeCoreModules — utilities.go:77.
Tristate ShouldUseUriStyleNodeCoreModules(SourceFile* file,
                                          compiler::SimpleProgram* program);
// GetQuotePreference — utilities.go:98.
QuotePreference GetQuotePreference(SourceFile* sourceFile,
                                   UserPreferences preferences);

} // namespace lsutil
} // namespace tsc
