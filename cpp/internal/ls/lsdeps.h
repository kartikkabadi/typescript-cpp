// === slice: ls-coreA (dep declarations) ===
// Declarations for sibling packages that have not landed on this branch:
// ls/lsutil (beyond the format.h block), ls/lsconv, ls/change, ls/autoimport.
// Types and pure-data constants below are faithful ports; function bodies are
// dep-stubs in lsdeps.cpp (TSC_UNREACHABLE("<name> — <slice>")). When the
// owner slices land, replace this file with their real headers.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <functional>
#include <tuple>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/format/format.h" // lsutil FormatCodeSettings dep block
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/modulespecifiers/types.h"
#include "internal/printer/emitcontext.h"
#include "internal/printer/printer.h"
#include "internal/spanmap/spanmap.h"

namespace tsc::checker {
class Checker;
struct Type;
} // namespace tsc::checker
namespace tsc::compiler {
class SimpleProgram;
} // namespace tsc::compiler

namespace tsc::lsutil {

// === lsutil enum/const decls (real port — pure data) ===

// symbol_display.go:9 — `type ScriptElementKind int`.
using ScriptElementKind = int32_t;
inline constexpr ScriptElementKind ScriptElementKindUnknown = 0;
inline constexpr ScriptElementKind ScriptElementKindWarning = 1;
// predefined type (void) or keyword (class)
inline constexpr ScriptElementKind ScriptElementKindKeyword = 2;
// top level script node
inline constexpr ScriptElementKind ScriptElementKindScriptElement = 3;
// module foo {}
inline constexpr ScriptElementKind ScriptElementKindModuleElement = 4;
// class X {}
inline constexpr ScriptElementKind ScriptElementKindClassElement = 5;
// var x = class X {}
inline constexpr ScriptElementKind ScriptElementKindLocalClassElement = 6;
// interface Y {}
inline constexpr ScriptElementKind ScriptElementKindInterfaceElement = 7;
// type T = ...
inline constexpr ScriptElementKind ScriptElementKindTypeElement = 8;
// enum E {}
inline constexpr ScriptElementKind ScriptElementKindEnumElement = 9;
inline constexpr ScriptElementKind ScriptElementKindEnumMemberElement = 10;
// Inside module and script only. const v = ...
inline constexpr ScriptElementKind ScriptElementKindVariableElement = 11;
// Inside function.
inline constexpr ScriptElementKind ScriptElementKindLocalVariableElement = 12;
// using foo = ...
inline constexpr ScriptElementKind ScriptElementKindVariableUsingElement = 13;
// await using foo = ...
inline constexpr ScriptElementKind ScriptElementKindVariableAwaitUsingElement = 14;
// Inside module and script only. function f() {}
inline constexpr ScriptElementKind ScriptElementKindFunctionElement = 15;
// Inside function.
inline constexpr ScriptElementKind ScriptElementKindLocalFunctionElement = 16;
// class X { [public|private]* foo() {} }
inline constexpr ScriptElementKind ScriptElementKindMemberFunctionElement = 17;
// class X { [public|private]* [get|set] foo:number; }
inline constexpr ScriptElementKind ScriptElementKindMemberGetAccessorElement = 18;
inline constexpr ScriptElementKind ScriptElementKindMemberSetAccessorElement = 19;
// class X { [public|private]* foo:number; } / interface Y { foo:number; }
inline constexpr ScriptElementKind ScriptElementKindMemberVariableElement = 20;
// class X { [public|private]* accessor foo: number; }
inline constexpr ScriptElementKind ScriptElementKindMemberAccessorVariableElement = 21;
// class X { constructor() { } } / class X { static { } }
inline constexpr ScriptElementKind ScriptElementKindConstructorImplementationElement = 22;
// interface Y { ():number; }
inline constexpr ScriptElementKind ScriptElementKindCallSignatureElement = 23;
// interface Y { []:number; }
inline constexpr ScriptElementKind ScriptElementKindIndexSignatureElement = 24;
// interface Y { new():Y; }
inline constexpr ScriptElementKind ScriptElementKindConstructSignatureElement = 25;
// function foo(*Y*: string)
inline constexpr ScriptElementKind ScriptElementKindParameterElement = 26;
inline constexpr ScriptElementKind ScriptElementKindTypeParameterElement = 27;
inline constexpr ScriptElementKind ScriptElementKindPrimitiveType = 28;
inline constexpr ScriptElementKind ScriptElementKindLabel = 29;
inline constexpr ScriptElementKind ScriptElementKindAlias = 30;
inline constexpr ScriptElementKind ScriptElementKindConstElement = 31;
inline constexpr ScriptElementKind ScriptElementKindLetElement = 32;
inline constexpr ScriptElementKind ScriptElementKindDirectory = 33;
inline constexpr ScriptElementKind ScriptElementKindExternalModuleName = 34;
// String literal
inline constexpr ScriptElementKind ScriptElementKindString = 35;
// Jsdoc @link: in `{@link C link text}`, the "{@link " and "}" parts
inline constexpr ScriptElementKind ScriptElementKindLink = 36;
// Jsdoc @link: the entity name "C"
inline constexpr ScriptElementKind ScriptElementKindLinkName = 37;
// Jsdoc @link: the link text "link text"
inline constexpr ScriptElementKind ScriptElementKindLinkText = 38;

// symbol_display.go:103 — `type ScriptElementKindModifier uint32`.
using ScriptElementKindModifier = uint32_t;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierNone = 0;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierPublic = 1u << 1;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierPrivate = 1u << 2;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierProtected = 1u << 3;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierExported = 1u << 4;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierAmbient = 1u << 5;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierStatic = 1u << 6;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierAbstract = 1u << 7;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierOptional = 1u << 8;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierDeprecated = 1u << 9;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierDts = 1u << 10;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierTs = 1u << 11;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierTsx = 1u << 12;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierJs = 1u << 13;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierJsx = 1u << 14;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierJson = 1u << 15;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierDmts = 1u << 16;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierMts = 1u << 17;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierMjs = 1u << 18;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierDcts = 1u << 19;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierCts = 1u << 20;
inline constexpr ScriptElementKindModifier ScriptElementKindModifierCjs = 1u << 21;

// userpreferences.go:230 — `type QuotePreference string`.
using QuotePreference = std::string;
inline const QuotePreference QuotePreferenceUnknown{""};
inline const QuotePreference QuotePreferenceAuto{"auto"};
inline const QuotePreference QuotePreferenceDouble{"double"};
inline const QuotePreference QuotePreferenceSingle{"single"};

// userpreferences.go:246 — `type JsxAttributeCompletionStyle string`.
using JsxAttributeCompletionStyle = std::string;
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleUnknown{""};
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleAuto{"auto"};
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleBraces{"braces"};
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleNone{"none"};

// userpreferences.go:262
using IncludeInlayParameterNameHints = std::string;
inline const IncludeInlayParameterNameHints IncludeInlayParameterNameHintsNone{""};
inline const IncludeInlayParameterNameHints IncludeInlayParameterNameHintsAll{"all"};
inline const IncludeInlayParameterNameHints IncludeInlayParameterNameHintsLiterals{"literals"};

// userpreferences.go:234
using WorkspaceSymbolsScope = std::string;
inline const WorkspaceSymbolsScope WorkspaceSymbolsScopeAllOpenProjects{"allOpenProjects"};
inline const WorkspaceSymbolsScope WorkspaceSymbolsScopeCurrentProject{"currentProject"};

// userpreferences.go:269
using OrganizeImportsSort = int32_t;
inline constexpr OrganizeImportsSort OrganizeImportsSortAuto = 0;
inline constexpr OrganizeImportsSort OrganizeImportsSortOrdinal = 1;
inline constexpr OrganizeImportsSort OrganizeImportsSortOrdinalIgnoreCase = 2;
inline constexpr OrganizeImportsSort OrganizeImportsSortNatural = 3;
inline constexpr OrganizeImportsSort OrganizeImportsSortNaturalIgnoreCase = 4;

// userpreferences.go:280 — `type OrganizeImportsCollation bool`.
using OrganizeImportsCollation = bool;
inline constexpr OrganizeImportsCollation OrganizeImportsCollationOrdinal = false;
inline constexpr OrganizeImportsCollation OrganizeImportsCollationUnicode = true;

// userpreferences.go:287
using OrganizeImportsCaseFirst = int32_t;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstFalse = 0;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstLower = 1;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstUpper = 2;

// userpreferences.go:296
using OrganizeImportsTypeOrder = int32_t;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderAuto = 0;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderLast = 1;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderInline = 2;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderFirst = 3;

// userpreferences.go:201
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

// userpreferences.go:212
struct CodeLensUserPreferences {
	Tristate ReferencesCodeLensEnabled = Tristate::Unknown;
	Tristate ImplementationsCodeLensEnabled = Tristate::Unknown;
	Tristate ReferencesCodeLensShowOnAllFunctions = Tristate::Unknown;
	Tristate ImplementationsCodeLensShowOnInterfaceMethods = Tristate::Unknown;
	Tristate ImplementationsCodeLensShowOnAllClassMethods = Tristate::Unknown;
};

// userpreferences.go:47 — UserPreferences. Field order/tags follow Go; the
// `raw:`/`config:` parsing machinery is owned by the lsutil slice and is not
// ported here.
struct UserPreferences {
	FormatCodeSettings FormatCodeSettings;

	QuotePreference QuotePreference;
	Tristate LazyConfiguredProjectsFromExternalProject = Tristate::Unknown;

	// A positive integer indicating the maximum length of a hover text
	// before it is truncated. Default: 500.
	int MaximumHoverLength = 0;

	// ------- Completions -------

	// If enabled, TypeScript will search through all external modules'
	// exports and add them to the completions list.
	Tristate IncludeCompletionsForModuleExports = Tristate::Unknown;
	// Enables auto-import-style completions on partially-typed import
	// statements.
	Tristate IncludeCompletionsForImportStatements = Tristate::Unknown;
	// Unless this option is `false`, member completion lists triggered with
	// `.` will include entries on potentially-null and
	// potentially-undefined values, with insertion text to replace
	// preceding `.` tokens with `?.`.
	Tristate IncludeAutomaticOptionalChainCompletions = Tristate::Unknown;
	// If enabled, completions for class members will include a whole
	// declaration for the member.
	Tristate IncludeCompletionsWithClassMemberSnippets = Tristate::Unknown;
	// If enabled, object literal methods will have a method declaration
	// completion entry in addition to the regular entry.
	Tristate IncludeCompletionsWithObjectLiteralMethodSnippets = Tristate::Unknown;
	JsxAttributeCompletionStyle JsxAttributeCompletionStyle;
	Tristate EnableAutoClosingTags = Tristate::Unknown;
	Tristate EnableJSDocCompletions = Tristate::Unknown;
	Tristate GenerateReturnInDocTemplate = Tristate::Unknown;

	// ------- AutoImports -------

	modulespecifiers::ImportModuleSpecifierPreference ImportModuleSpecifierPreference;
	modulespecifiers::ImportModuleSpecifierEndingPreference ImportModuleSpecifierEnding;
	std::vector<std::string> AutoImportSpecifierExcludeRegexes;
	std::vector<std::string> AutoImportFileExcludePatterns;
	Tristate AutoImportEntrypointDirectorySearch = Tristate::Unknown;
	Tristate PreferTypeOnlyAutoImports = Tristate::Unknown;

	// ------- OrganizeImports -------

	OrganizeImportsSort OrganizeImportsSort = lsutil::OrganizeImportsSortAuto;
	Tristate OrganizeImportsIgnoreCase = Tristate::Unknown;
	OrganizeImportsCollation OrganizeImportsCollation = OrganizeImportsCollationOrdinal;
	std::string OrganizeImportsLocale;
	Tristate OrganizeImportsNumericCollation = Tristate::Unknown;
	Tristate OrganizeImportsAccentCollation = Tristate::Unknown;
	OrganizeImportsCaseFirst OrganizeImportsCaseFirst = lsutil::OrganizeImportsCaseFirstFalse;
	OrganizeImportsTypeOrder OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderAuto;

	// ------- MoveToFile -------

	Tristate AllowTextChangesInNewFiles = Tristate::Unknown;

	// ------- Rename -------

	Tristate UseAliasesForRename = Tristate::Unknown;
	Tristate AllowRenameOfImportPath = Tristate::Unknown;

	// ------- CodeFixes/Refactors -------

	Tristate ProvideRefactorNotApplicableReason = Tristate::Unknown;

	// ------- InlayHints -------

	InlayHintsPreferences InlayHints;

	// ------- CodeLens -------

	CodeLensUserPreferences CodeLens;

	// ------- Definition -------

	bool PreferGoToSourceDefinition = false;

	// ------- Symbols -------

	Tristate ExcludeLibrarySymbolsInNavTo = Tristate::Unknown;
	WorkspaceSymbolsScope WorkspaceSymbolsScope;

	// ------- Misc -------

	Tristate EnableFormatting = Tristate::Unknown;
	Tristate EnableValidation = Tristate::Unknown;
	Tristate DisableSuggestions = Tristate::Unknown;
	Tristate DisableLineTextInReferences = Tristate::Unknown;
	Tristate DisplayPartsForJSDoc = Tristate::Unknown;
	Tristate ReportStyleChecksAsWarnings = Tristate::Unknown;
	std::string Locale;

	// ------- ATA -------

	Tristate DisableAutomaticTypeAcquisition = Tristate::Unknown;
	Tristate AutomaticTypeAcquisitionEnabled = Tristate::Unknown;

	// ------- Project Configuration -------

	std::string CustomConfigFileName;

	// userpreferences.go:877 ModuleSpecifierPreferences.
	modulespecifiers::UserPreferences ModuleSpecifierPreferences() const {
		return modulespecifiers::UserPreferences{
		    ImportModuleSpecifierPreference, ImportModuleSpecifierEnding,
		    AutoImportSpecifierExcludeRegexes};
	}
};

// === lsutil function decls — dep-stubs (bodies in lsdeps.cpp) ===

// children.go:13 GetLastChild.
Node* GetLastChild(Node* node, SourceFile* sourceFile);
// children.go:37 GetLastToken.
Node* GetLastToken(Node* node, SourceFile* sourceFile);
// children.go:87 GetFirstToken.
Node* GetFirstToken(Node* node, SourceFile* sourceFile);
// asi.go:9 PositionIsASICandidate.
bool PositionIsASICandidate(int pos, Node* context, SourceFile* file);
// utilities.go:15 ProbablyUsesSemicolons.
bool ProbablyUsesSemicolons(SourceFile* file);
// utilities.go:98 GetQuotePreference.
QuotePreference GetQuotePreference(SourceFile* sourceFile,
                                   const UserPreferences& preferences);
// utilities.go:158 IsNonContextualKeyword.
bool IsNonContextualKeyword(Kind token);
// symbol_display.go:162 GetSymbolKind.
ScriptElementKind GetSymbolKind(checker::Checker* typeChecker, Symbol* symbol,
                                Node* location);
// symbol_display.go:349 GetSymbolModifiers.
ScriptElementKindModifier GetSymbolModifiers(checker::Checker* typeChecker,
                                             Symbol* symbol);

} // namespace tsc::lsutil

namespace tsc::lsconv {

// === lsconv decls — dep-stubs (bodies in lsdeps.cpp) ===

// linemap.go:11
using LSPLineStarts = std::vector<TextPos>;
struct LSPLineMap {
	LSPLineStarts LineStarts;
	bool AsciiOnly = false;
};

// converters.go:33 — Go `Script` interface. SourceFile already implements all
// five methods; callers pass `*ast::SourceFile` for T.
template <typename T>
concept Script = requires(T t) {
	{ t->FileName() };
	{ t->OriginalFileName() };
	{ t->Text() };
	{ t->SpanMap() };
	{ t->OriginalText() };
};

// converters.go:25 — Go field promotion = C++ public base.
template <Script T>
struct MappedSpan : spanmap::MappedSpan {
	T Script;
};

// converters.go:30
template <Script T>
struct MappedPosition : spanmap::MappedPosition {
	T Script;
};

// converters.go:21 — `type Converters struct`.
class Converters {
public:
	Converters(lsproto::PositionEncodingKind positionEncoding,
	           std::function<LSPLineMap*(const std::string&)> getLineMap)
	    : positionEncoding_(std::move(positionEncoding)),
	      getLineMap_(std::move(getLineMap)) {}

	// converters.go:59 ToLSPRange.
	template <Script T>
	std::pair<lsproto::Range, spanmap::Fidelity> ToLSPRange(
	    T script, TextRange textRange);
	// converters.go:70 ToLSPRangeForFeature.
	template <Script T>
	std::pair<lsproto::Range, spanmap::Fidelity> ToLSPRangeForFeature(
	    T script, TextRange textRange, spanmap::Feature feature);
	// converters.go:83 ToLSPPosition.
	template <Script T>
	std::pair<lsproto::Position, spanmap::Fidelity> ToLSPPosition(
	    T script, TextPos position);
	// converters.go:93 ToLSPPositionForFeature.
	template <Script T>
	std::pair<lsproto::Position, spanmap::Fidelity> ToLSPPositionForFeature(
	    T script, TextPos position, spanmap::Feature feature);
	// converters.go:105 ToLSPLocation.
	template <Script T>
	std::pair<lsproto::Location, spanmap::Fidelity> ToLSPLocation(
	    T script, TextRange rng);
	// converters.go:118 ToLSPLocationForFeature.
	template <Script T>
	std::pair<lsproto::Location, spanmap::Fidelity> ToLSPLocationForFeature(
	    T script, TextRange rng, spanmap::Feature feature);

	// converters.go:202 FromLSPPositionForSourceFile.
	std::vector<MappedPosition<SourceFile*>> FromLSPPositionForSourceFile(
	    SourceFile* file, lsproto::Position position, spanmap::Feature feature);
	// converters.go:133 FromLSPRangeForSourceFile.
	std::vector<MappedSpan<SourceFile*>> FromLSPRangeForSourceFile(
	    SourceFile* file, lsproto::Range textRange, spanmap::Feature feature);
	// converters.go:151 FromLSPRangeIntersectingForSourceFile.
	std::vector<MappedSpan<SourceFile*>> FromLSPRangeIntersectingForSourceFile(
	    SourceFile* file, lsproto::Range textRange, spanmap::Feature feature);
	// converters.go:220 FromLSPRangeToOriginal.
	TextRange FromLSPRangeToOriginal(SourceFile* script,
	                                 lsproto::Range textRange);

private:
	lsproto::PositionEncodingKind positionEncoding_;
	std::function<LSPLineMap*(const std::string&)> getLineMap_;
};

// converters.go:43 NewConverters.
Converters* NewConverters(
    lsproto::PositionEncodingKind positionEncoding,
    std::function<LSPLineMap*(const std::string&)> getLineMap);

} // namespace tsc::lsconv

namespace tsc::autoimport {

// === autoimport decls — dep-stubs (bodies in lsdeps.cpp) ===

// export.go:17
using ModuleID = std::string;

// export.go:24 — `type ExportSyntax int`.
using ExportSyntax = int32_t;
inline constexpr ExportSyntax ExportSyntaxNone = 0;
// export const x = {}
inline constexpr ExportSyntax ExportSyntaxModifier = 1;
// export { x }
inline constexpr ExportSyntax ExportSyntaxNamed = 2;
// export default function f() {}
inline constexpr ExportSyntax ExportSyntaxDefaultModifier = 3;
// export default f
inline constexpr ExportSyntax ExportSyntaxDefaultDeclaration = 4;
// export = x
inline constexpr ExportSyntax ExportSyntaxEquals = 5;
// export as namespace x
inline constexpr ExportSyntax ExportSyntaxUMD = 6;
// export * from "module"
inline constexpr ExportSyntax ExportSyntaxStar = 7;
// module.exports = {}
inline constexpr ExportSyntax ExportSyntaxCommonJSModuleExports = 8;
// exports.x = {}
inline constexpr ExportSyntax ExportSyntaxCommonJSExportsProperty = 9;

// export.go:18
struct ExportID {
	ModuleID ModuleID;
	std::string ExportName;
};

// export.go:48 — embedded ExportID becomes a public base (field promotion).
struct Export : ExportID {
	std::string ModuleFileName;
	ExportSyntax Syntax = ExportSyntaxNone;
	SymbolFlags Flags = SymbolFlagsNone;
	std::string localName;
	// through is the name of the module symbol's export that this export was
	// found on, either 'export=', InternalSymbolNameExportStar, or "".
	std::string through;

	// Checker-set fields
	ExportID Target;
	bool IsTypeOnly = false;
	lsutil::ScriptElementKind ScriptElementKind = lsutil::ScriptElementKindUnknown;
	lsutil::ScriptElementKindModifier ScriptElementKindModifiers =
	    lsutil::ScriptElementKindModifierNone;

	// The file where the export was found.
	tspath::Path Path;

	std::string PackageName;

	// export.go:92 IsUnresolvedAlias.
	bool IsUnresolvedAlias() const { return Flags == SymbolFlagsAlias; }
};

// fix.go:37 — `type Fix struct { *lsproto.AutoImportFix ... }`.
struct Fix {
	lsproto::AutoImportFix* AutoImportFix = nullptr;
	modulespecifiers::ResultKind ModuleSpecifierKind =
	    modulespecifiers::ResultKind::None;
	bool IsReExport = false;
	std::string ModuleFileName;
	Node* TypeOnlyAliasDeclaration = nullptr;

	// fix.go:56 Edits — ([]*lsproto.TextEdit, string, bool).
	std::tuple<std::vector<lsproto::TextEdit*>, std::string, bool> Edits(
	    const ContextPtr& ctx, SourceFile* file,
	    const CompilerOptions* compilerOptions,
	    lsutil::FormatCodeSettings formatOptions,
	    lsconv::Converters* converters,
	    const lsutil::UserPreferences& preferences);
};

// view.go:175
struct FixAndExport {
	autoimport::Fix* Fix = nullptr;
	autoimport::Export* Export = nullptr;
};

// registry.go:32 — `type ProjectID interface { fmt.Stringer }`.
struct ProjectID {
	virtual ~ProjectID() = default;
	virtual std::string String() const = 0;
};

// registry.go:328 — `type Registry struct` (opaque to this slice).
class Registry {
public:
	// registry.go:354 IsPreparedForImportingFile.
	bool IsPreparedForImportingFile(const std::string& fileName,
	                                ProjectID* projectID,
	                                const lsutil::UserPreferences& preferences);
};

// import_adder.go:24 — `type ImportAdder interface`.
struct ImportAdder {
	virtual ~ImportAdder() = default;
	virtual bool HasFixes() = 0;
	virtual void AddImportFromExportedSymbol(Symbol* symbol,
	                                         bool isValidTypeOnlyUseSite) = 0;
	virtual void AddImportFix(Fix* fix) = 0;
	virtual std::vector<lsproto::TextEdit*> Edits() = 0;
};

// view.go — `type View struct` (opaque to this slice).
class View {
public:
	// view.go:180 GetCompletions.
	std::vector<FixAndExport*> GetCompletions(const std::string& prefix,
	                                          lsproto::Position position,
	                                          bool forJSX,
	                                          bool isTypeOnlyLocation);
};

// view.go:37 NewView.
View* NewView(Registry* registry, SourceFile* importingFile,
              ProjectID* projectID, compiler::SimpleProgram* program,
              checker::Checker* typeChecker,
              const modulespecifiers::UserPreferences& preferences);

// import_adder.go:70 NewImportAdder.
ImportAdder* NewImportAdder(const ContextPtr& ctx,
                            compiler::SimpleProgram* program,
                            checker::Checker* checker, SourceFile* file,
                            View* view,
                            lsutil::FormatCodeSettings formatOptions,
                            lsconv::Converters* converters,
                            const lsutil::UserPreferences& preferences);

// import_adder.go:382 TypeToAutoImportableTypeNode.
Node* TypeToAutoImportableTypeNode(checker::Checker* c,
                                   ImportAdder* importAdder, checker::Type* t,
                                   Node* contextNode);

// fix.go:796 GetImportKindForImportStatement.
lsproto::ImportKind GetImportKindForImportStatement(
    SourceFile* importingFile, Export* export_, compiler::SimpleProgram* program);

} // namespace tsc::autoimport

namespace tsc::change {

// === change decls — dep-stubs (bodies in lsdeps.cpp) ===

// tracker.go:83 — `type Tracker struct` embeds *printer.EmitContext and
// *ast.NodeFactory; only those promoted fields are accessed by this slice.
class Tracker {
public:
	printer::EmitContext* EmitContext = nullptr;
	NodeFactory* NodeFactory = nullptr;
};

// tracker.go:112 NewTracker.
Tracker* NewTracker(const ContextPtr& ctx,
                    const CompilerOptions* compilerOptions,
                    lsutil::FormatCodeSettings formatOptions,
                    lsconv::Converters* converters);

// trackerimpl.go:252 GetFormatCodeSettingsForWriting.
lsutil::FormatCodeSettings GetFormatCodeSettingsForWriting(
    lsutil::FormatCodeSettings options, SourceFile* sourceFile);

} // namespace tsc::change

namespace tsc::printer {

// === printer dep decls — partial dep-stub ===

// changetrackerwriter.go:10 — `type ChangeTrackerWriter struct` embeds
// textWriter. The embedded writer plumbing forwards to a real
// EmitTextWriter (emittextwriter.go is ported); the position-tracking
// methods (GetPrintHandlers/AssignPositionsToNode) belong to the
// changetrackerwriter slice and are dep-stubbed.
class ChangeTrackerWriter : public EmitTextWriter {
public:
	ChangeTrackerWriter(const std::string& newLine, int indentSize);

	// changetrackerwriter.go:39 GetPrintHandlers — dep-stub.
	PrintHandlers GetPrintHandlers();
	// changetrackerwriter.go:107 AssignPositionsToNode — dep-stub.
	Node* AssignPositionsToNode(Node* node, tsc::NodeFactory* factory);

	// EmitTextWriter virtuals — forward to the inner textWriter.
	void Write(const std::string& s) override;
	void WriteTrailingSemicolon(const std::string& text) override;
	void WriteComment(const std::string& text) override;
	void WriteKeyword(const std::string& text) override;
	void WriteOperator(const std::string& text) override;
	void WritePunctuation(const std::string& text) override;
	void WriteSpace(const std::string& text) override;
	void WriteStringLiteral(const std::string& text) override;
	void WriteParameter(const std::string& text) override;
	void WriteProperty(const std::string& text) override;
	void WriteSymbol(const std::string& text, Symbol* symbol) override;
	void WriteLine() override;
	void WriteLineForce(bool force) override;
	void IncreaseIndent() override;
	void DecreaseIndent() override;
	void Clear() override;
	std::string String() override;
	void RawWrite(const std::string& s) override;
	void WriteLiteral(const std::string& s) override;
	int GetTextPos() override;
	int GetLine() override;
	TextPos GetColumn() override;
	int GetIndent() override;
	bool IsAtStartOfLine() override;
	bool HasTrailingComment() override;
	bool HasTrailingWhitespace() override;

private:
	std::unique_ptr<EmitTextWriter> inner_;
};

// changetrackerwriter.go:22 NewChangeTrackerWriter.
ChangeTrackerWriter* NewChangeTrackerWriter(const std::string& newLine,
                                            int indentSize);

} // namespace tsc::printer
