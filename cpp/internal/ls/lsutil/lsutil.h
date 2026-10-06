// lsutil — dep-decl for the ls-autoimport slice: ls/lsutil's value types
// (UserPreferences + its enums, ScriptElementKind + modifiers — used
// in result structs) and function decls that are dep-stubs.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/core/types.h" // Tristate
#include "internal/format/format.h" // tsc::lsutil::FormatCodeSettings
#include "internal/modulespecifiers/types.h" // preferences enums
#include "internal/tspath/tspath.h"

namespace tsc {
struct Node;
struct SourceFile;
struct Symbol;
struct FileDetectorSubPathResult;
} // namespace tsc
namespace tsc::compiler {
class SimpleProgram;
} // namespace tsc::compiler
namespace tsc::checker {
class Checker;
}
namespace tsc::vfs::vfsmatch {
struct SpecMatcher;
}

namespace tsc::lsutil {

// === dep decls for ls-autoimport — owned by ls/lsutil ===

// ---- userpreferences.go ----

using QuotePreference = std::string;
inline const QuotePreference QuotePreferenceUnknown{""};
inline const QuotePreference QuotePreferenceAuto{"auto"};
inline const QuotePreference QuotePreferenceDouble{"double"};
inline const QuotePreference QuotePreferenceSingle{"single"};

using WorkspaceSymbolsScope = std::string;
inline const WorkspaceSymbolsScope WorkspaceSymbolsScopeAllOpenProjects{
	"allOpenProjects"};
inline const WorkspaceSymbolsScope WorkspaceSymbolsScopeCurrentProject{
	"currentProject"};

using JsxAttributeCompletionStyle = std::string;
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleUnknown{""};
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleAuto{
	"auto"};
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleBraces{
	"braces"};
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleNone{
	"none"};

using IncludeInlayParameterNameHints = std::string;
inline const IncludeInlayParameterNameHints
	IncludeInlayParameterNameHintsNone{""};
inline const IncludeInlayParameterNameHints IncludeInlayParameterNameHintsAll{
	"all"};
inline const IncludeInlayParameterNameHints
	IncludeInlayParameterNameHintsLiterals{"literals"};

using OrganizeImportsSort = int;
inline constexpr OrganizeImportsSort OrganizeImportsSortAuto = 0;
inline constexpr OrganizeImportsSort OrganizeImportsSortOrdinal = 1;
inline constexpr OrganizeImportsSort OrganizeImportsSortOrdinalIgnoreCase = 2;
inline constexpr OrganizeImportsSort OrganizeImportsSortNatural = 3;
inline constexpr OrganizeImportsSort OrganizeImportsSortNaturalIgnoreCase = 4;

using OrganizeImportsCollation = bool;
inline constexpr OrganizeImportsCollation OrganizeImportsCollationOrdinal =
	false;
inline constexpr OrganizeImportsCollation OrganizeImportsCollationUnicode =
	true;

using OrganizeImportsCaseFirst = int;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstFalse = 0;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstLower = 1;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstUpper = 2;

using OrganizeImportsTypeOrder = int;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderAuto = 0;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderLast = 1;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderInline = 2;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderFirst = 3;

struct InlayHintsPreferences {
	IncludeInlayParameterNameHints IncludeInlayParameterNameHints;
	Tristate IncludeInlayParameterNameHintsWhenArgumentMatchesName{};
	Tristate IncludeInlayFunctionParameterTypeHints{};
	Tristate IncludeInlayVariableTypeHints{};
	Tristate IncludeInlayVariableTypeHintsWhenTypeMatchesName{};
	Tristate IncludeInlayPropertyDeclarationTypeHints{};
	Tristate IncludeInlayFunctionLikeReturnTypeHints{};
	Tristate IncludeInlayEnumMemberValueHints{};
};

struct CodeLensUserPreferences {
	Tristate ReferencesCodeLensEnabled{};
	Tristate ImplementationsCodeLensEnabled{};
	Tristate ReferencesCodeLensShowOnAllFunctions{};
	Tristate ImplementationsCodeLensShowOnInterfaceMethods{};
	Tristate ImplementationsCodeLensShowOnAllClassMethods{};
};

// UserPreferences — userpreferences.go:41.
struct UserPreferences {
	FormatCodeSettings FormatCodeSettings;

	QuotePreference QuotePreference;
	Tristate LazyConfiguredProjectsFromExternalProject{};
	int MaximumHoverLength = 0;

	// Completions
	Tristate IncludeCompletionsForModuleExports{};
	Tristate IncludeCompletionsForImportStatements{};
	Tristate IncludeAutomaticOptionalChainCompletions{};
	Tristate IncludeCompletionsWithClassMemberSnippets{};
	Tristate IncludeCompletionsWithObjectLiteralMethodSnippets{};
	JsxAttributeCompletionStyle JsxAttributeCompletionStyle;
	Tristate EnableAutoClosingTags{};
	Tristate EnableJSDocCompletions{};
	Tristate GenerateReturnInDocTemplate{};

	// AutoImports
	modulespecifiers::ImportModuleSpecifierPreference
		ImportModuleSpecifierPreference;
	modulespecifiers::ImportModuleSpecifierEndingPreference
		ImportModuleSpecifierEnding;
	std::vector<std::string> AutoImportSpecifierExcludeRegexes;
	std::vector<std::string> AutoImportFileExcludePatterns;
	Tristate AutoImportEntrypointDirectorySearch{};
	Tristate PreferTypeOnlyAutoImports{};

	// OrganizeImports
	OrganizeImportsSort OrganizeImportsSort{};
	Tristate OrganizeImportsIgnoreCase{};
	OrganizeImportsCollation OrganizeImportsCollation{};
	std::string OrganizeImportsLocale;
	Tristate OrganizeImportsNumericCollation{};
	Tristate OrganizeImportsAccentCollation{};
	OrganizeImportsCaseFirst OrganizeImportsCaseFirst{};
	OrganizeImportsTypeOrder OrganizeImportsTypeOrder{};

	Tristate AllowTextChangesInNewFiles{};
	Tristate UseAliasesForRename{};
	Tristate AllowRenameOfImportPath{};
	Tristate ProvideRefactorNotApplicableReason{};
	InlayHintsPreferences InlayHints;
	CodeLensUserPreferences CodeLens;
	bool PreferGoToSourceDefinition = false;
	Tristate ExcludeLibrarySymbolsInNavTo{};
	WorkspaceSymbolsScope WorkspaceSymbolsScope;
	Tristate EnableFormatting{};
	Tristate EnableValidation{};
	Tristate DisableSuggestions{};
	Tristate DisableLineTextInReferences{};
	Tristate DisplayPartsForJSDoc{};
	Tristate ReportStyleChecksAsWarnings{};
	std::string Locale;
	Tristate DisableAutomaticTypeAcquisition{};
	Tristate AutomaticTypeAcquisitionEnabled{};
	std::string CustomConfigFileName;

	// ParsedAutoImportFileExcludePatterns — userpreferences.go:885.
	// Returns nullptr when no pattern compiles (matching Go's nil).
	std::unique_ptr<vfs::vfsmatch::SpecMatcher>
	ParsedAutoImportFileExcludePatterns(
	    bool useCaseSensitiveFileNames) const;

	bool IsATADisabled() const {
		if (AutomaticTypeAcquisitionEnabled != Tristate::Unknown) {
			return !tristateIsTrue(AutomaticTypeAcquisitionEnabled);
		}
		return tristateIsTrue(DisableAutomaticTypeAcquisition);
	}
};

// NewDefaultUserPreferences — userpreferences.go:13.
inline UserPreferences newDefaultUserPreferences() {
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

// ---- symbol_display.go ----

using ScriptElementKind = int;
inline constexpr ScriptElementKind ScriptElementKindUnknown = 0;
inline constexpr ScriptElementKind ScriptElementKindWarning = 1;
inline constexpr ScriptElementKind ScriptElementKindKeyword = 2;
inline constexpr ScriptElementKind ScriptElementKindScriptElement = 3;
inline constexpr ScriptElementKind ScriptElementKindModuleElement = 4;
inline constexpr ScriptElementKind ScriptElementKindClassElement = 5;
inline constexpr ScriptElementKind ScriptElementKindLocalClassElement = 6;
inline constexpr ScriptElementKind ScriptElementKindInterfaceElement = 7;
inline constexpr ScriptElementKind ScriptElementKindTypeElement = 8;
inline constexpr ScriptElementKind ScriptElementKindEnumElement = 9;
inline constexpr ScriptElementKind ScriptElementKindEnumMemberElement = 10;
inline constexpr ScriptElementKind ScriptElementKindVariableElement = 11;
inline constexpr ScriptElementKind ScriptElementKindLocalVariableElement = 12;
inline constexpr ScriptElementKind ScriptElementKindVariableUsingElement = 13;
inline constexpr ScriptElementKind ScriptElementKindVariableAwaitUsingElement =
	14;
inline constexpr ScriptElementKind ScriptElementKindFunctionElement = 15;
inline constexpr ScriptElementKind ScriptElementKindLocalFunctionElement = 16;
inline constexpr ScriptElementKind ScriptElementKindMemberFunctionElement = 17;
inline constexpr ScriptElementKind ScriptElementKindMemberGetAccessorElement =
	18;
inline constexpr ScriptElementKind ScriptElementKindMemberSetAccessorElement =
	19;
inline constexpr ScriptElementKind ScriptElementKindMemberVariableElement = 20;
inline constexpr ScriptElementKind
	ScriptElementKindMemberAccessorVariableElement = 21;
inline constexpr ScriptElementKind
	ScriptElementKindConstructorImplementationElement = 22;
inline constexpr ScriptElementKind ScriptElementKindCallSignatureElement = 23;
inline constexpr ScriptElementKind ScriptElementKindIndexSignatureElement = 24;
inline constexpr ScriptElementKind ScriptElementKindConstructSignatureElement =
	25;
inline constexpr ScriptElementKind ScriptElementKindParameterElement = 26;
inline constexpr ScriptElementKind ScriptElementKindTypeParameterElement = 27;
inline constexpr ScriptElementKind ScriptElementKindPrimitiveType = 28;
inline constexpr ScriptElementKind ScriptElementKindLabel = 29;
inline constexpr ScriptElementKind ScriptElementKindAlias = 30;
inline constexpr ScriptElementKind ScriptElementKindConstElement = 31;
inline constexpr ScriptElementKind ScriptElementKindLetElement = 32;
inline constexpr ScriptElementKind ScriptElementKindDirectory = 33;
inline constexpr ScriptElementKind ScriptElementKindExternalModuleName = 34;
inline constexpr ScriptElementKind ScriptElementKindString = 35;
inline constexpr ScriptElementKind ScriptElementKindLink = 36;
inline constexpr ScriptElementKind ScriptElementKindLinkName = 37;
inline constexpr ScriptElementKind ScriptElementKindLinkText = 38;

using ScriptElementKindModifier = uint32_t;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierNone = 0;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierPublic =
	1u << 0;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierPrivate =
	1u << 1;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierProtected =
	1u << 2;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierExported =
	1u << 3;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierAmbient =
	1u << 4;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierStatic =
	1u << 5;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierAbstract =
	1u << 6;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierOptional =
	1u << 7;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierDeprecated =
	1u << 8;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierDts =
	1u << 9;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierTs =
	1u << 10;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierTsx =
	1u << 11;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierJs =
	1u << 12;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierJsx =
	1u << 13;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierJson =
	1u << 14;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierDmts =
	1u << 15;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierMts =
	1u << 16;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierMjs =
	1u << 17;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierDcts =
	1u << 18;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierCts =
	1u << 19;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierCjs =
	1u << 20;

inline constexpr ScriptElementKindModifier FileExtensionKindModifiers =
	ScriptElementKindModifierDts | ScriptElementKindModifierTs |
	ScriptElementKindModifierTsx | ScriptElementKindModifierJs |
	ScriptElementKindModifierJsx | ScriptElementKindModifierJson |
	ScriptElementKindModifierDmts | ScriptElementKindModifierMts |
	ScriptElementKindModifierMjs | ScriptElementKindModifierDcts |
	ScriptElementKindModifierCts | ScriptElementKindModifierCjs;

// === dep stubs — owned by ls/lsutil ===

// GetFirstToken — children.go:87.
Node* getFirstToken(Node* node, SourceFile* sourceFile);
// CompareImportsOrRequireStatements — organizeimports.go:370.
int compareImportsOrRequireStatements(Node* s1, Node* s2,
	const std::function<int(const std::string&, const std::string&)>& comparer);
// GetImportSpecifierInsertionIndex — organizeimports.go:414.
int getImportSpecifierInsertionIndex(
	const std::vector<Node*>& sortedImports, Node* newImport,
	const std::function<int(Node*, Node*)>& comparer);
// GetImportDeclarationInsertIndex — organizeimports.go:421.
int getImportDeclarationInsertIndex(
	const std::vector<Node*>& sortedImports, Node* newImport,
	const std::function<int(Node*, Node*)>& comparer);
// GetOrganizeImportsStringComparerWithDetection — organizeimports.go:428.
std::pair<std::function<int(const std::string&, const std::string&)>, bool>
getOrganizeImportsStringComparerWithDetection(
	const std::vector<Node*>& originalImportDecls,
	const UserPreferences& preferences);
// GetNamedImportSpecifierComparerWithDetection — organizeimports.go:662.
std::pair<std::function<int(Node*, Node*)>, Tristate>
getNamedImportSpecifierComparerWithDetection(
	Node* importDecl, SourceFile* sourceFile,
	const UserPreferences& preferences);
// GetSymbolKind — symbol_display.go:162.
ScriptElementKind getSymbolKind(checker::Checker* typeChecker, Symbol* symbol,
                                Node* location);
// GetSymbolModifiers — symbol_display.go:349.
ScriptElementKindModifier getSymbolModifiers(checker::Checker* typeChecker,
                                             Symbol* symbol);
// ShouldUseUriStyleNodeCoreModules — utilities.go:77.
Tristate shouldUseUriStyleNodeCoreModules(SourceFile* file,
                                          compiler::SimpleProgram* program);
// GetQuotePreference — utilities.go:98.
QuotePreference getQuotePreference(SourceFile* sourceFile,
                                   const UserPreferences& preferences);
// ModuleSymbolToValidIdentifier — utilities.go:115.
std::string moduleSymbolToValidIdentifier(Symbol* moduleSymbol,
                                          bool forceCapitalize);
// ModuleSpecifierToValidIdentifier — utilities.go:123.
std::string moduleSpecifierToValidIdentifier(std::string moduleSpecifier,
                                             bool forceCapitalize);

} // namespace tsc::lsutil
