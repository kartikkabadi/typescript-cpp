// Port of tsc/internal/format — declarations. See PORTING.md for conventions.
#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/kind.h"
#include "internal/ast/visitor.h"
#include "internal/core/text.h"
#include "internal/core/textchange.h"
#include "internal/core/types.h"
#include "internal/modulespecifiers/types.h" // === slice: api === (UserPreferences)
#include "internal/scanner/scanner.h"

namespace tsc::lsutil {
// === dep decls for ls slice (tsc/internal/ls/lsutil) ===
// The enums/structs and GetDefaultFormatCodeSettings below are a real port of
// formatcodeoptions.go — the format package needs them unconditionally at
// compile time and runtime, so they cannot be stubbed. The owner slice should
// move this block when it lands. The AST helpers are dep-stubbed (defined in
// format/api.cpp under the dep-stub marker).

enum class IndentStyle : int32_t {
	None,
	Block,
	Smart,
};

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

struct EditorSettings {
	int BaseIndentSize = 0;
	int IndentSize = 0;
	int TabSize = 0;
	std::string NewLineCharacter;
	Tristate ConvertTabsToSpaces = Tristate::Unknown;
	IndentStyle IndentStyle = IndentStyle::None;
	Tristate TrimTrailingWhitespace = Tristate::Unknown;
};

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
	SemicolonPreference Semicolons = SemicolonPreference::Ignore;
	Tristate IndentSwitchCase = Tristate::Unknown;
};

FormatCodeSettings GetDefaultFormatCodeSettings();

// Owned by the ls slice — stubbed in format/api.cpp until it lands.
bool PositionBelongsToNode(Node* candidate, int position, SourceFile* file);
Node* GetFirstToken(Node* node, SourceFile* sourceFile);
bool PositionIsASICandidate(int pos, Node* context, SourceFile* file);

// === slice: api ===
// The remaining lsutil types the api dep-declares (userpreferences.go). Pure
// data; ported faithfully. The ls slice should adopt these when it lands.

// QuotePreference (userpreferences.go:230).
using QuotePreference = std::string;
inline const QuotePreference QuotePreferenceUnknown{""};
inline const QuotePreference QuotePreferenceAuto{"auto"};
inline const QuotePreference QuotePreferenceDouble{"double"};
inline const QuotePreference QuotePreferenceSingle{"single"};

// WorkspaceSymbolsScope (userpreferences.go:232).
using WorkspaceSymbolsScope = std::string;
inline const WorkspaceSymbolsScope WorkspaceSymbolsScopeAllOpenProjects{"allOpenProjects"};
inline const WorkspaceSymbolsScope WorkspaceSymbolsScopeCurrentProject{"currentProject"};

// JsxAttributeCompletionStyle (userpreferences.go:246).
using JsxAttributeCompletionStyle = std::string;
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleUnknown{""};
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleAuto{"auto"};
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleBraces{"braces"};
inline const JsxAttributeCompletionStyle JsxAttributeCompletionStyleNone{"none"};

// IncludeInlayParameterNameHints (userpreferences.go:255).
using IncludeInlayParameterNameHints = std::string;
inline const IncludeInlayParameterNameHints IncludeInlayParameterNameHintsNone{""};
inline const IncludeInlayParameterNameHints IncludeInlayParameterNameHintsAll{"all"};
inline const IncludeInlayParameterNameHints IncludeInlayParameterNameHintsLiterals{"literals"};

// OrganizeImportsSort (userpreferences.go:263).
using OrganizeImportsSort = int;
inline constexpr OrganizeImportsSort OrganizeImportsSortAuto = 0;
inline constexpr OrganizeImportsSort OrganizeImportsSortOrdinal = 1;
inline constexpr OrganizeImportsSort OrganizeImportsSortOrdinalIgnoreCase = 2;
inline constexpr OrganizeImportsSort OrganizeImportsSortNatural = 3;
inline constexpr OrganizeImportsSort OrganizeImportsSortNaturalIgnoreCase = 4;

// OrganizeImportsCollation (userpreferences.go:273).
using OrganizeImportsCollation = bool;
inline constexpr OrganizeImportsCollation OrganizeImportsCollationOrdinal = false;
inline constexpr OrganizeImportsCollation OrganizeImportsCollationUnicode = true;

// OrganizeImportsCaseFirst (userpreferences.go:280).
using OrganizeImportsCaseFirst = int;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstFalse = 0;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstLower = 1;
inline constexpr OrganizeImportsCaseFirst OrganizeImportsCaseFirstUpper = 2;

// OrganizeImportsTypeOrder (userpreferences.go:288).
using OrganizeImportsTypeOrder = int;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderAuto = 0;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderLast = 1;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderInline = 2;
inline constexpr OrganizeImportsTypeOrder OrganizeImportsTypeOrderFirst = 3;

// InlayHintsPreferences (userpreferences.go:209).
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

// CodeLensUserPreferences (userpreferences.go:220).
struct CodeLensUserPreferences {
	Tristate ReferencesCodeLensEnabled = Tristate::Unknown;
	Tristate ImplementationsCodeLensEnabled = Tristate::Unknown;
	Tristate ReferencesCodeLensShowOnAllFunctions = Tristate::Unknown;
	Tristate ImplementationsCodeLensShowOnInterfaceMethods = Tristate::Unknown;
	Tristate ImplementationsCodeLensShowOnAllClassMethods = Tristate::Unknown;
};

// UserPreferences (userpreferences.go:47).
struct UserPreferences {
	FormatCodeSettings FormatCodeSettings;

	QuotePreference QuotePreference;
	Tristate LazyConfiguredProjectsFromExternalProject = Tristate::Unknown;

	// A positive integer indicating the maximum length of a hover text before
	// it is truncated. Default: `500`.
	int MaximumHoverLength = 0;

	// ------- Completions -------
	Tristate IncludeCompletionsForModuleExports = Tristate::Unknown;
	Tristate IncludeCompletionsForImportStatements = Tristate::Unknown;
	Tristate IncludeAutomaticOptionalChainCompletions = Tristate::Unknown;
	Tristate IncludeCompletionsWithClassMemberSnippets = Tristate::Unknown;
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
	OrganizeImportsSort OrganizeImportsSort{};
	Tristate OrganizeImportsIgnoreCase = Tristate::Unknown;
	OrganizeImportsCollation OrganizeImportsCollation{};
	std::string OrganizeImportsLocale;
	Tristate OrganizeImportsNumericCollation = Tristate::Unknown;
	Tristate OrganizeImportsAccentCollation = Tristate::Unknown;
	OrganizeImportsCaseFirst OrganizeImportsCaseFirst{};
	OrganizeImportsTypeOrder OrganizeImportsTypeOrder{};

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

	// ModuleSpecifierPreferences (userpreferences.go:877).
	modulespecifiers::UserPreferences ModuleSpecifierPreferences() const {
		return modulespecifiers::UserPreferences{
			ImportModuleSpecifierPreference, ImportModuleSpecifierEnding,
			AutoImportSpecifierExcludeRegexes};
	}

	// IsATADisabled (userpreferences.go:200).
	bool IsATADisabled() const {
		if (AutomaticTypeAcquisitionEnabled != Tristate::Unknown) {
			return AutomaticTypeAcquisitionEnabled != Tristate::True;
		}
		return DisableAutomaticTypeAcquisition == Tristate::True;
	}
};

// NewDefaultUserPreferences (userpreferences.go:16).
UserPreferences NewDefaultUserPreferences();
// === end slice: api ===
// === end dep decls for ls slice ===
} // namespace tsc::lsutil

namespace tsc::format {

struct FormattingContext;
struct formatSpanWorker;
enum class FormatRequestKind : int32_t;

// --- scanner.go ---

struct TextRangeWithKind {
	TextRange Loc;
	Kind kind = Kind::Unknown;

	constexpr bool operator==(const TextRangeWithKind&) const = default;
};
inline TextRangeWithKind NewTextRangeWithKind(int pos, int end, Kind kind) {
	return TextRangeWithKind{TextRange{TextPos(pos), TextPos(end)}, kind};
}

struct tokenInfo {
	std::vector<TextRangeWithKind> leadingTrivia;
	TextRangeWithKind token;
	std::vector<TextRangeWithKind> trailingTrivia;
};

enum class scanAction : int32_t {
	actionScan,
	actionRescanGreaterThanToken,
	actionRescanSlashToken,
	actionRescanTemplateToken,
	actionRescanJsxIdentifier,
	actionRescanJsxText,
	actionRescanJsxAttributeValue,
};

struct formattingScanner {
	Scanner s;
	int startPos = 0;
	int endPos = 0;
	int savedPos = 0;
	bool hasLastTokenInfo = false;
	tokenInfo lastTokenInfo;
	scanAction lastScanAction = scanAction::actionScan;
	std::vector<TextRangeWithKind> leadingTrivia;
	std::vector<TextRangeWithKind> trailingTrivia;
	bool wasNewLine = false;

	void advance();
	bool shouldRescanJsxText(Node* node);
	tokenInfo readTokenInfo(Node* n);
	Kind getNextToken(Node* n, scanAction expectedScanAction);
	TextRangeWithKind readEOFTokenRange();
	bool isOnToken();
	bool isOnEOF();
	void skipToEndOf(TextRange* r);
	void skipToStartOf(TextRange* r);
	std::vector<TextRangeWithKind> getCurrentLeadingTrivia();
	bool lastTrailingTriviaWasNewLine();
	int getTokenFullStart();
	int getStartPos();
};

// newFormattingScanner is the package entrypoint: creates the scanner, runs the
// worker, returns the edits.
std::vector<TextChange> newFormattingScanner(std::string_view text, LanguageVariant languageVariant,
											 int startPos, int endPos, formatSpanWorker* worker);

bool shouldRescanGreaterThanToken(Node* node);
bool shouldRescanJsxIdentifier(Node* node);
bool isLeftmostJsxTagName(Node* node);
bool shouldRescanSlashToken(Node* container);
bool shouldRescanTemplateToken(Node* container);
bool shouldRescanJsxAttributeValue(Node* node);
bool startsWithSlashToken(Kind t);
tokenInfo fixTokenKind(tokenInfo tokenInfo, Node* container);

// --- rule.go ---

struct ruleImpl {
	std::string debugName;
	std::vector<std::function<bool(FormattingContext*)>> context;
	int action = 0;  // ruleAction bitmask
	int flags = 0;   // ruleFlags

	int Action() const { return action; }
	const std::vector<std::function<bool(FormattingContext*)>>& Context() const { return context; }
	int Flags() const { return flags; }
	const std::string& String() const { return debugName; }
};

struct tokenRange {
	std::vector<Kind> tokens;
	bool isSpecific = false;
};

struct ruleSpec {
	tokenRange leftTokenRange;
	tokenRange rightTokenRange;
	ruleImpl* rule = nullptr;
};

using contextPredicate = std::function<bool(FormattingContext*)>;
// Go: `var anyContext = []contextPredicate{}`
inline const std::vector<contextPredicate> anyContext{};

using ruleAction = int;
inline constexpr ruleAction ruleActionNone = 0;
inline constexpr ruleAction ruleActionStopProcessingSpaceActions = 1 << 0;
inline constexpr ruleAction ruleActionStopProcessingTokenActions = 1 << 1;
inline constexpr ruleAction ruleActionInsertSpace = 1 << 2;
inline constexpr ruleAction ruleActionInsertNewLine = 1 << 3;
inline constexpr ruleAction ruleActionDeleteSpace = 1 << 4;
inline constexpr ruleAction ruleActionDeleteToken = 1 << 5;
inline constexpr ruleAction ruleActionInsertTrailingSemicolon = 1 << 6;
inline constexpr ruleAction ruleActionStopAction =
	ruleActionStopProcessingSpaceActions | ruleActionStopProcessingTokenActions;
inline constexpr ruleAction ruleActionModifySpaceAction =
	ruleActionInsertSpace | ruleActionInsertNewLine | ruleActionDeleteSpace;
inline constexpr ruleAction ruleActionModifyTokenAction =
	ruleActionDeleteToken | ruleActionInsertTrailingSemicolon;

using ruleFlags = int;
inline constexpr ruleFlags ruleFlagsNone = 0;
inline constexpr ruleFlags ruleFlagsCanDeleteNewLines = 1;

// ruleTokenArg stands in for Go's `any` left/right args to rule(): accepts a
// Kind, a list of Kinds, or an already-built tokenRange — the same types
// toTokenRange accepts.
struct ruleTokenArg {
	tokenRange range;
	ruleTokenArg(Kind k) : range{std::vector<Kind>{k}, true} {}
	ruleTokenArg(std::initializer_list<Kind> ks) : range{std::vector<Kind>(ks), true} {}
	ruleTokenArg(std::vector<Kind> ks) : range{std::move(ks), true} {}
	ruleTokenArg(tokenRange r) : range(std::move(r)) {}
};

ruleSpec rule(std::string debugName, ruleTokenArg left, ruleTokenArg right,
			  std::vector<contextPredicate> context, ruleAction action, ruleFlags flags = ruleFlagsNone);
tokenRange toTokenRange(ruleTokenArg e);
tokenRange tokenRangeFrom(std::initializer_list<Kind> tokens);
tokenRange tokenRangeFromEx(const std::vector<Kind>& prefix, std::initializer_list<Kind> tokens);
tokenRange tokenRangeFromRange(Kind start, Kind end);

// --- rules.go ---
std::vector<ruleSpec> getAllRules();

// --- rulesmap.go ---
using RulesPosition = int;
inline constexpr int maskBitSize = 5;
inline constexpr int rulesmap_mask = 0b11111;
inline constexpr int mapRowLength = static_cast<int>(KindLastToken) + 1;
inline constexpr RulesPosition RulesPositionStopRulesSpecific = 0;
inline constexpr RulesPosition RulesPositionStopRulesAny = maskBitSize * 1;
inline constexpr RulesPosition RulesPositionContextRulesSpecific = maskBitSize * 2;
inline constexpr RulesPosition RulesPositionContextRulesAny = maskBitSize * 3;
inline constexpr RulesPosition RulesPositionNoContextRulesSpecific = maskBitSize * 4;
inline constexpr RulesPosition RulesPositionNoContextRulesAny = maskBitSize * 5;

std::vector<ruleImpl*> getRules(FormattingContext* context, std::vector<ruleImpl*> rules);
int getRuleBucketIndex(Kind row, Kind column);
ruleAction getRuleActionExclusion(ruleAction action);
const std::vector<std::vector<ruleImpl*>>& getRulesMap();
std::vector<ruleImpl*> addRule(std::vector<ruleImpl*> rules, ruleImpl* rule, bool specificTokens,
							   std::vector<int>& constructionState, int rulesBucketIndex);
int getRuleInsertionIndex(int indexBitmap, RulesPosition maskPosition);
int increaseInsertionIndex(int indexBitmap, RulesPosition maskPosition);

// --- context.go ---
struct FormattingContext {
	TextRangeWithKind currentTokenSpan;
	TextRangeWithKind nextTokenSpan;
	Node* contextNode = nullptr;
	Node* currentTokenParent = nullptr;
	Node* nextTokenParent = nullptr;

	Tristate contextNodeAllOnSameLine = Tristate::Unknown;
	Tristate nextNodeAllOnSameLine = Tristate::Unknown;
	Tristate tokensAreOnSameLine = Tristate::Unknown;
	Tristate contextNodeBlockIsOnOneLine = Tristate::Unknown;
	Tristate nextNodeBlockIsOnOneLine = Tristate::Unknown;

	SourceFile* SourceFile = nullptr;
	FormatRequestKind FormattingRequestKind;
	lsutil::FormatCodeSettings Options;

	void UpdateContext(TextRangeWithKind cur, Node* curParent, TextRangeWithKind next,
					   Node* nextParent, Node* commonParent);
	Tristate rangeIsOnOneLine(TextRange node);
	Tristate nodeIsOnOneLine(Node* node);
	Tristate blockIsOnOneLine(Node* node);
	bool ContextNodeAllOnSameLine();
	bool NextNodeAllOnSameLine();
	bool TokensAreOnSameLine();
	bool ContextNodeBlockIsOnOneLine();
	bool NextNodeBlockIsOnOneLine();
};
FormattingContext* NewFormattingContext(SourceFile* file, FormatRequestKind kind,
										lsutil::FormatCodeSettings options);
TextRange withTokenStart(Node* loc, SourceFile* file);

// --- api.go ---
enum class FormatRequestKind : int32_t {
	FormatDocument,
	FormatSelection,
	FormatOnEnter,
	FormatOnSemicolon,
	FormatOnOpeningCurlyBrace,
	FormatOnClosingCurlyBrace,
};
inline constexpr FormatRequestKind FormatRequestKindFormatDocument = FormatRequestKind::FormatDocument;
inline constexpr FormatRequestKind FormatRequestKindFormatSelection = FormatRequestKind::FormatSelection;
inline constexpr FormatRequestKind FormatRequestKindFormatOnEnter = FormatRequestKind::FormatOnEnter;
inline constexpr FormatRequestKind FormatRequestKindFormatOnSemicolon = FormatRequestKind::FormatOnSemicolon;
inline constexpr FormatRequestKind FormatRequestKindFormatOnOpeningCurlyBrace = FormatRequestKind::FormatOnOpeningCurlyBrace;
inline constexpr FormatRequestKind FormatRequestKindFormatOnClosingCurlyBrace = FormatRequestKind::FormatOnClosingCurlyBrace;

// FormatRequestContext stands in for Go's context.Context carrier used by
// WithFormatCodeSettings/GetFormatCodeSettingsFromContext/GetNewLineOrDefaultFromContext.
struct FormatRequestContext {
	std::optional<lsutil::FormatCodeSettings> options;
	std::optional<std::string> newLine;
};
FormatRequestContext WithFormatCodeSettings(FormatRequestContext ctx,
											lsutil::FormatCodeSettings options, std::string newLine);
lsutil::FormatCodeSettings GetFormatCodeSettingsFromContext(const FormatRequestContext& ctx);
std::string GetNewLineOrDefaultFromContext(const FormatRequestContext& ctx);

std::vector<TextChange> FormatSpan(const FormatRequestContext& ctx, TextRange span,
								   SourceFile* file, FormatRequestKind kind);
std::vector<TextChange> FormatNodeGivenIndentation(const FormatRequestContext& ctx, Node* node,
												   SourceFile* file, LanguageVariant languageVariant,
												   int initialIndentation, int delta);
std::vector<TextChange> formatNodeLines(const FormatRequestContext& ctx, SourceFile* sourceFile,
										Node* node, FormatRequestKind requestKind);
std::vector<TextChange> FormatDocument(const FormatRequestContext& ctx, SourceFile* sourceFile);
std::vector<TextChange> FormatSelection(const FormatRequestContext& ctx, SourceFile* sourceFile,
										int start, int end);
std::vector<TextChange> FormatOnOpeningCurly(const FormatRequestContext& ctx,
											 SourceFile* sourceFile, int position);
std::vector<TextChange> FormatOnClosingCurly(const FormatRequestContext& ctx,
											 SourceFile* sourceFile, int position);
std::vector<TextChange> FormatOnSemicolon(const FormatRequestContext& ctx, SourceFile* sourceFile,
										  int position);
std::vector<TextChange> FormatOnEnter(const FormatRequestContext& ctx, SourceFile* sourceFile,
									  int position);

// --- util.go ---
bool rangeIsOnOneLine(TextRange node, SourceFile* file);
Kind getOpenTokenForList(Node* node, NodeList* list);
Kind getCloseTokenForOpenToken(Kind kind);
int GetLineStartPositionForPosition(int position, SourceFile* sourceFile);
Node* findImmediatelyPrecedingTokenOfKind(int end, Kind expectedTokenKind, SourceFile* sourceFile);
Node* findOutermostNodeWithinListLevel(Node* node);
bool isListElement(Node* parent, Node* node);
bool isMemberListElement(Node* parent, Node* node);

// --- indent.go ---
int GetIndentationForNode(Node* n, TextRange* ignoreActualIndentationRange, SourceFile* sourceFile,
						  const lsutil::FormatCodeSettings& options);
int GetIndentation(int position, SourceFile* sourceFile, const lsutil::FormatCodeSettings& options,
				   bool assumeNewLineBeforeCloseBrace);
int getCommentIndent(SourceFile* sourceFile, int position, const lsutil::FormatCodeSettings& options,
					 CommentRange* enclosingCommentRange);
void getLeadingCommentRangesOfNode(Node* node, SourceFile* file,
								   const std::function<bool(const CommentRange&)>& yield);
std::optional<CommentRange> getRangeOfEnclosingComment(SourceFile* sourceFile, int position,
													 Node* precedingToken);
int getBlockIndent(SourceFile* sourceFile, int position, const lsutil::FormatCodeSettings& options);
int getActualIndentationForListItemBeforeComma(Node* commaToken, SourceFile* sourceFile,
											   const lsutil::FormatCodeSettings& options);

enum class nextTokenKind : int32_t {
	nextTokenKindUnknown,
	nextTokenKindOpenBrace,
	nextTokenKindCloseBrace,
};
nextTokenKind nextTokenIsCurlyBraceOnSameLineAsCursor(Node* precedingToken, Node* current,
													  int lineAtPosition, SourceFile* sourceFile);
int getSmartIndent(SourceFile* sourceFile, int position, Node* precedingToken, int lineAtPosition,
				   bool assumeNewLineBeforeCloseBrace, const lsutil::FormatCodeSettings& options);
int getIndentationForNodeWorker(Node* current, int currentStartLine, int currentStartCharacter,
								TextRange* ignoreActualIndentationRange, int indentationDelta,
								SourceFile* sourceFile, bool isNextChild,
								const lsutil::FormatCodeSettings& options);
int getActualIndentationForNode(Node* current, Node* parent, int cuurentLine, int currentChar,
								bool parentAndChildShareLine, SourceFile* sourceFile,
								const lsutil::FormatCodeSettings& options);
bool isArgumentAndStartLineOverlapsExpressionBeingCalled(Node* parent, Node* child,
														 int childStartLine, SourceFile* sourceFile);
int getActualIndentationForListItem(Node* node, SourceFile* sourceFile,
									const lsutil::FormatCodeSettings& options, bool listIndentsChild);
int getActualIndentationForListStartLine(NodeList* list, SourceFile* sourceFile,
										 const lsutil::FormatCodeSettings& options);
int deriveActualIndentationFromList(NodeList* list, int index, SourceFile* sourceFile,
									const lsutil::FormatCodeSettings& options);
int findColumnForFirstNonWhitespaceCharacterInLine(int line, int ch, SourceFile* sourceFile,
												   const lsutil::FormatCodeSettings& options);
int FindFirstNonWhitespaceColumn(int startPos, int endPos, SourceFile* sourceFile,
								 const lsutil::FormatCodeSettings& options);
std::pair<int, int> findFirstNonWhitespaceCharacterAndColumn(int startPos, int endPos,
															 SourceFile* sourceFile,
															 const lsutil::FormatCodeSettings& options);
bool childStartsOnTheSameLineWithElseInIfStatement(Node* parent, Node* child, int childStartLine,
												   SourceFile* sourceFile);
std::pair<int, int> getStartLineAndCharacterForNode(Node* n, SourceFile* sourceFile);
int getStartLineForNode(Node* n, SourceFile* sourceFile);
NodeList* GetContainingList(Node* node, SourceFile* sourceFile);
NodeList* getListByPosition(int pos, Node* node, SourceFile* sourceFile);
NodeList* getListByRange(int start, int end, Node* node, SourceFile* sourceFile);
NodeList* getList(NodeList* list, TextRange r, Node* node, SourceFile* sourceFile);
TextRange getVisualListRange(Node* node, TextRange list, SourceFile* sourceFile);
std::pair<int, int> getContainingListOrParentStart(Node* parent, Node* child, SourceFile* sourceFile);
bool isControlFlowEndingStatement(Kind kind, Kind parentKind);
bool ShouldIndentChildNode(const lsutil::FormatCodeSettings& settings, Node* parent, Node* child,
						   SourceFile* sourceFile, bool isNextChild = false);
bool NodeWillIndentChild(const lsutil::FormatCodeSettings& settings, Node* parent, Node* child,
						 SourceFile* sourceFile, bool indentByDefault);
bool childIsUnindentedBranchOfConditionalExpression(Node* parent, Node* child, int childStartLine,
													SourceFile* sourceFile);
bool argumentStartsOnSameLineAsPreviousArgument(Node* parent, Node* child, int childStartLine,
												SourceFile* sourceFile);

// --- rulecontext.go ---
using optionSelector = Tristate (*)(const lsutil::FormatCodeSettings&);
template <typename T>
using anyOptionSelector = T (*)(const lsutil::FormatCodeSettings&);

lsutil::SemicolonPreference semicolonOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceAfterCommaDelimiterOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceAfterSemicolonInForStatementsOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceBeforeAndAfterBinaryOperatorsOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceAfterConstructorOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceAfterKeywordsInControlFlowStatementsOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceAfterFunctionKeywordForAnonymousFunctionsOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesisOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceAfterOpeningAndBeforeClosingNonemptyBracketsOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceAfterOpeningAndBeforeClosingNonemptyBracesOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceAfterOpeningAndBeforeClosingEmptyBracesOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceAfterOpeningAndBeforeClosingTemplateStringBracesOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceAfterOpeningAndBeforeClosingJsxExpressionBracesOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceAfterTypeAssertionOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceBeforeFunctionParenthesisOption(const lsutil::FormatCodeSettings& options);
Tristate placeOpenBraceOnNewLineForFunctionsOption(const lsutil::FormatCodeSettings& options);
Tristate placeOpenBraceOnNewLineForControlBlocksOption(const lsutil::FormatCodeSettings& options);
Tristate insertSpaceBeforeTypeAnnotationOption(const lsutil::FormatCodeSettings& options);
Tristate indentMultiLineObjectLiteralBeginningOnBlankLineOption(const lsutil::FormatCodeSettings& options);
Tristate indentSwitchCaseOption(const lsutil::FormatCodeSettings& options);

// Go: `func optionEquals[T comparable](optionName, optionValue) contextPredicate`
template <typename T>
contextPredicate optionEquals(anyOptionSelector<T> optionName, T optionValue) {
	return [optionName, optionValue](FormattingContext* context) {
		return optionName(context->Options) == optionValue;
	};
}
contextPredicate isOptionEnabled(optionSelector optionName);
contextPredicate isOptionDisabled(optionSelector optionName);
contextPredicate isOptionDisabledOrUndefined(optionSelector optionName);
contextPredicate isOptionDisabledOrUndefinedOrTokensOnSameLine(optionSelector optionName);
contextPredicate isOptionEnabledOrUndefined(optionSelector optionName);

bool isForContext(FormattingContext* context);
bool isNotForContext(FormattingContext* context);
bool isBinaryOpContext(FormattingContext* context);
bool isNotBinaryOpContext(FormattingContext* context);
bool isNotTypeAnnotationContext(FormattingContext* context);
bool isTypeAnnotationContext(FormattingContext* context);
bool isOptionalPropertyContext(FormattingContext* context);
bool isNonOptionalPropertyContext(FormattingContext* context);
bool isConditionalOperatorContext(FormattingContext* context);
bool isSameLineTokenOrBeforeBlockContext(FormattingContext* context);
bool isBraceWrappedContext(FormattingContext* context);
bool isBeforeMultilineBlockContext(FormattingContext* context);
bool isMultilineBlockContext(FormattingContext* context);
bool isSingleLineBlockContext(FormattingContext* context);
bool isBlockContext(FormattingContext* context);
bool isBeforeBlockContext(FormattingContext* context);
bool nodeIsBlockContext(Node* node);
bool isFunctionDeclContext(FormattingContext* context);
bool isNotFunctionDeclContext(FormattingContext* context);
bool isFunctionDeclarationOrFunctionExpressionContext(FormattingContext* context);
bool isTypeScriptDeclWithBlockContext(FormattingContext* context);
bool nodeIsTypeScriptDeclWithBlockContext(Node* node);
bool isAfterCodeBlockContext(FormattingContext* context);
bool isControlDeclContext(FormattingContext* context);
bool isObjectContext(FormattingContext* context);
bool isFunctionCallContext(FormattingContext* context);
bool isNewContext(FormattingContext* context);
bool isFunctionCallOrNewContext(FormattingContext* context);
bool isPreviousTokenNotComma(FormattingContext* context);
bool isNextTokenNotCloseBracket(FormattingContext* context);
bool isNextTokenNotCloseParen(FormattingContext* context);
bool isArrowFunctionContext(FormattingContext* context);
bool isImportTypeContext(FormattingContext* context);
bool isNonJsxSameLineTokenContext(FormattingContext* context);
bool isNonJsxTextContext(FormattingContext* context);
bool isNonJsxElementOrFragmentContext(FormattingContext* context);
bool isJsxExpressionContext(FormattingContext* context);
bool isNextTokenParentJsxAttribute(FormattingContext* context);
bool isJsxAttributeContext(FormattingContext* context);
bool isNextTokenParentNotJsxNamespacedName(FormattingContext* context);
bool isNextTokenParentJsxNamespacedName(FormattingContext* context);
bool isJsxSelfClosingElementContext(FormattingContext* context);
bool isNotBeforeBlockInFunctionDeclarationContext(FormattingContext* context);
bool isEndOfDecoratorContextOnSameLine(FormattingContext* context);
bool nodeIsInDecoratorContext(Node* node);
bool isStartOfVariableDeclarationList(FormattingContext* context);
bool isNotFormatOnEnter(FormattingContext* context);
bool isModuleDeclContext(FormattingContext* context);
bool isObjectTypeContext(FormattingContext* context);
bool isConstructorSignatureContext(FormattingContext* context);
bool isTypeArgumentOrParameterOrAssertion(TextRangeWithKind token, Node* parent);
bool isTypeArgumentOrParameterOrAssertionContext(FormattingContext* context);
bool isTypeAssertionContext(FormattingContext* context);
bool isNonTypeAssertionContext(FormattingContext* context);
bool isVoidOpContext(FormattingContext* context);
bool isYieldOrYieldStarWithOperand(FormattingContext* context);
bool isNonNullAssertionContext(FormattingContext* context);
bool isNotStatementConditionContext(FormattingContext* context);
bool isStatementConditionContext(FormattingContext* context);
bool isSemicolonDeletionContext(FormattingContext* context);
bool isSemicolonInsertionContext(FormattingContext* context);
bool isNotPropertyAccessOnIntegerLiteral(FormattingContext* context);

// --- span.go ---
enum class LineAction : int32_t {
	LineActionNone,
	LineActionLineAdded,
	LineActionLineRemoved,
};

struct dynamicIndenter {
	Node* node = nullptr;
	int nodeStartLine = 0;
	int indentation = 0;
	int delta = 0;
	lsutil::FormatCodeSettings options;
	SourceFile* sourceFile = nullptr;

	int getIndentationForComment(Kind kind, int tokenIndentation, Node* container);
	int getIndentationForToken(int line, Kind kind, Node* container, bool suppressDelta);
	int getIndentation();
	int getDelta(Node* child);
	void recomputeIndentation(bool lineAdded, Node* parent);
	bool shouldAddDelta(int line, Kind kind, Node* container);
};
Kind getFirstNonDecoratorTokenOfNode(Node* node);

using rangeContainsErrorFunc = std::function<bool(TextRange)>;

struct formatSpanWorker {
	TextRange originalRange;
	Node* enclosingNode = nullptr;
	int initialIndentation = 0;
	int delta = 0;
	FormatRequestKind requestKind = FormatRequestKind::FormatDocument;
	rangeContainsErrorFunc rangeContainsError;
	SourceFile* sourceFile = nullptr;

	FormatRequestContext ctx;

	formattingScanner* fmtScanner = nullptr;
	std::unique_ptr<FormattingContext> formattingContext;

	std::vector<TextChange> edits;
	TextRangeWithKind previousRange;
	int previousRangeTriviaEnd = 0;
	Node* previousParent = nullptr;
	int previousRangeStartLine = 0;

	Node* childContextNode = nullptr;
	int lastIndentedLine = 0;
	int indentationOnLastIndentedLine = 0;

	std::unique_ptr<NodeVisitor> visitor;
	Node* visitingNode = nullptr;
	dynamicIndenter* visitingIndenter = nullptr;
	int visitingNodeStartLine = 0;
	int visitingUndecoratedNodeStartLine = 0;

	// GC'd in Go; pool keeps returned pointers stable for the worker's lifetime.
	std::deque<dynamicIndenter> indenterPool;

	std::vector<ruleImpl*> currentRules;

	std::vector<TextChange> execute(formattingScanner* s);
	int processChildNode(Node* node, dynamicIndenter* indenter, int nodeStartLine,
						 int undecoratedNodeStartLine, Node* child, int inheritedIndentation,
						 Node* parent, dynamicIndenter* parentDynamicIndentation,
						 int parentStartLine, int undecoratedParentStartLine, bool isListItem,
						 bool isFirstListItem);
	void processChildNodes(Node* node, dynamicIndenter* indenter, int nodeStartLine,
						   int undecoratedNodeStartLine, NodeList* nodes, Node* parent,
						   int parentStartLine, dynamicIndenter* parentDynamicIndentation);
	void executeProcessNodeVisitor(Node* node, dynamicIndenter* indenter, int nodeStartLine,
								   int undecoratedNodeStartLine);
	int getCurrentIndentationAtPosition(int pos);
	std::pair<int, int> computeIndentation(Node* node, int startLine, int inheritedIndentation,
										   Node* parent, dynamicIndenter* parentDynamicIndentation,
										   int effectiveParentStartLine);
	int tryComputeIndentationForListItem(int startPos, int endPos, int parentStartLine, TextRange r,
										 int inheritedIndentation);
	void processNode(Node* node, Node* contextNode, int nodeStartLine, int undecoratedNodeStartLine,
					 int indentation, int delta);
	LineAction processPair(TextRangeWithKind currentItem, int currentStartLine, Node* currentParent,
						   TextRangeWithKind previousItem, int previousStartLine,
						   Node* previousParent, Node* contextNode,
						   dynamicIndenter* dynamicIndentation);
	LineAction applyRuleEdits(ruleImpl* rule, TextRangeWithKind previousRange, int previousStartLine,
							  TextRangeWithKind currentRange, int currentStartLine);
	LineAction processRange(TextRangeWithKind r, int rangeStartLine, int rangeStartCharacter,
							Node* parent, Node* contextNode, dynamicIndenter* dynamicIndentation);
	void processTrivia(const std::vector<TextRangeWithKind>& trivia, Node* parent, Node* contextNode,
					   dynamicIndenter* dynamicIndentation);
	void trimTrailingWhitespacesForRemainingRange(const std::vector<TextRangeWithKind>& trivias);
	void trimTrailingWitespacesForPositions(int startPos, int endPos, TextRangeWithKind previousRange);
	void trimTrailingWhitespacesForLines(int line1, int line2, TextRangeWithKind r);
	int getTrailingWhitespaceStartPosition(int start, int end);
	void insertIndentation(int pos, int indentation, bool lineAdded);
	int characterToColumn(int startLinePosition, int characterInLine);
	bool indentationIsDifferent(const std::string& indentationString, int startLinePosition);
	bool indentTriviaItems(const std::vector<TextRangeWithKind>& trivia, int commentIndentation,
						   bool indentNextTokenOrTrivia,
						   std::function<void(TextRangeWithKind)> indentSingleLine);
	void indentMultilineComment(TextRange commentRange, int indentation, bool firstLineIsIndented,
								bool indentFinalLine);
	void recordDelete(int start, int length);
	void recordReplace(int start, int length, const std::string& newText);
	void recordInsert(int start, const std::string& text);
	void consumeTokenAndAdvanceScanner(tokenInfo currentTokenInfo, Node* parent,
									   dynamicIndenter* dynamicIndenation, Node* container,
									   bool isListEndToken);
	dynamicIndenter* getDynamicIndentation(Node* node, int nodeStartLine, int indentation, int delta);
};

Node* findEnclosingNode(TextRange r, SourceFile* sourceFile);
int getScanStartPosition(Node* enclosingNode, TextRange originalRange, SourceFile* sourceFile);
int getOwnOrInheritedDelta(Node* n, const lsutil::FormatCodeSettings& options, SourceFile* sourceFile);
bool rangeHasNoErrors(TextRange);
rangeContainsErrorFunc prepareRangeContainsErrorFunction(const std::vector<Diagnostic*>& errors,
														 TextRange originalRange);
formatSpanWorker* newFormatSpanWorker(FormatRequestContext ctx, TextRange originalRange,
									  Node* enclosingNode, int initialIndentation, int delta,
									  FormatRequestKind requestKind,
									  rangeContainsErrorFunc rangeContainsError,
									  SourceFile* sourceFile);
int getNonDecoratorTokenPosOfNode(Node* node, SourceFile* file);
bool isStringOrRegularExpressionOrTemplateLiteral(Kind kind);
bool isComment(Kind kind);
std::string getIndentationString(int indentation, const lsutil::FormatCodeSettings& options);
TextChange createTextChangeFromStartLength(int start, int length, std::string newText);

} // namespace tsc::format
