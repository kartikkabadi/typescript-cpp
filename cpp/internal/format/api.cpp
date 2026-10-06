// Port of tsc/internal/format/api.go.
#include "internal/format/format.h"

#include "internal/astnav/tokens.h"
#include "internal/printer/printer.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::lsutil {

// Real port (needed unconditionally by tsc::format): lsutil/formatcodeoptions.go
// GetDefaultFormatCodeSettings.
FormatCodeSettings GetDefaultFormatCodeSettings() {
	FormatCodeSettings opts;
	opts.IndentSize = tsc::printer::GetDefaultIndentSize();
	opts.TabSize = tsc::printer::GetDefaultIndentSize();
	opts.NewLineCharacter = "\n";
	opts.ConvertTabsToSpaces = Tristate::True;
	opts.IndentStyle = IndentStyle::Smart;
	opts.TrimTrailingWhitespace = Tristate::True;
	opts.InsertSpaceAfterCommaDelimiter = Tristate::True;
	opts.InsertSpaceAfterSemicolonInForStatements = Tristate::True;
	opts.InsertSpaceBeforeAndAfterBinaryOperators = Tristate::True;
	opts.InsertSpaceAfterKeywordsInControlFlowStatements = Tristate::True;
	opts.InsertSpaceAfterOpeningAndBeforeClosingNonemptyBraces = Tristate::True;
	opts.InsertSpaceAfterFunctionKeywordForAnonymousFunctions = Tristate::False;
	opts.InsertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis = Tristate::False;
	opts.InsertSpaceAfterOpeningAndBeforeClosingNonemptyBrackets = Tristate::False;
	opts.InsertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces = Tristate::False;
	opts.InsertSpaceAfterOpeningAndBeforeClosingJsxExpressionBraces = Tristate::False;
	opts.InsertSpaceBeforeFunctionParenthesis = Tristate::False;
	opts.PlaceOpenBraceOnNewLineForFunctions = Tristate::False;
	opts.PlaceOpenBraceOnNewLineForControlBlocks = Tristate::False;
	opts.InsertSpaceAfterConstructor = Tristate::False;
	opts.Semicolons = SemicolonPreference::Ignore;
	opts.IndentSwitchCase = Tristate::True;
	return opts;
}

} // namespace tsc::lsutil

namespace tsc::format {

FormatRequestContext WithFormatCodeSettings(FormatRequestContext ctx,
											const lsutil::FormatCodeSettings& options,
											std::string newLine) {
	ctx.options = options;
	ctx.newLine = std::move(newLine);
	// In strada, the rules map was both globally cached *and* cached into the context, for some reason. We skip that here and just use the global one.
	return ctx;
}

lsutil::FormatCodeSettings GetFormatCodeSettingsFromContext(const FormatRequestContext& ctx) {
	if (ctx.options.has_value()) {
		return *ctx.options;
	}
	return lsutil::GetDefaultFormatCodeSettings();
}

std::string GetNewLineOrDefaultFromContext(const FormatRequestContext& ctx) { // TODO: Move into broader LS - more than just the formatter uses the newline editor setting/host new line
	lsutil::FormatCodeSettings opt = GetFormatCodeSettingsFromContext(ctx);
	if (!opt.NewLineCharacter.empty()) {
		return opt.NewLineCharacter;
	}
	if (ctx.newLine.has_value() && !ctx.newLine->empty()) {
		return *ctx.newLine;
	}
	return "\n";
}

std::vector<TextChange> FormatSpan(const FormatRequestContext& ctx, TextRange span, SourceFile* file,
								   FormatRequestKind kind) {
	// find the smallest node that fully wraps the range and compute the initial indentation for the node
	Node* enclosingNode = findEnclosingNode(span, file);
	lsutil::FormatCodeSettings opts = GetFormatCodeSettingsFromContext(ctx);

	std::unique_ptr<formatSpanWorker> worker(newFormatSpanWorker(
		ctx,
		span,
		enclosingNode,
		GetIndentationForNode(enclosingNode, &span, file, opts),
		getOwnOrInheritedDelta(enclosingNode, opts, file),
		kind,
		prepareRangeContainsErrorFunction(file->diagnostics, span),
		file));
	return newFormattingScanner(file->text,
								file->LanguageVariant,
								getScanStartPosition(enclosingNode, span, file),
								span.end(),
								worker.get());
}

std::vector<TextChange> FormatNodeGivenIndentation(const FormatRequestContext& ctx, Node* node,
												   SourceFile* file,
												   LanguageVariant languageVariant,
												   int initialIndentation, int delta) {
	TextRange textRange{TextPos(node->pos()), TextPos(node->end())};
	std::unique_ptr<formatSpanWorker> worker(newFormatSpanWorker(
		ctx,
		textRange,
		node,
		initialIndentation,
		delta,
		FormatRequestKind::FormatSelection,
		[](TextRange) { return false; }, // assume that node does not have any errors
		file));
	return newFormattingScanner(file->text, languageVariant, textRange.pos(), textRange.end(),
								worker.get());
}

std::vector<TextChange> formatNodeLines(const FormatRequestContext& ctx, SourceFile* sourceFile,
										Node* node, FormatRequestKind requestKind) {
	if (node == nullptr) {
		return {};
	}
	int tokenStart = getTokenPosOfNode(node, sourceFile, false);
	int lineStart = GetLineStartPositionForPosition(tokenStart, sourceFile);
	TextRange span{TextPos(lineStart), TextPos(node->end())};
	return FormatSpan(ctx, span, sourceFile, requestKind);
}

std::vector<TextChange> FormatDocument(const FormatRequestContext& ctx, SourceFile* sourceFile) {
	return FormatSpan(ctx, TextRange{TextPos(0), TextPos(sourceFile->end())},
					  sourceFile, FormatRequestKind::FormatDocument);
}

std::vector<TextChange> FormatSelection(const FormatRequestContext& ctx, SourceFile* sourceFile,
										int start, int end) {
	return FormatSpan(ctx,
					  TextRange{TextPos(GetLineStartPositionForPosition(start, sourceFile)),
								TextPos(end)},
					  sourceFile, FormatRequestKind::FormatSelection);
}

std::vector<TextChange> FormatOnOpeningCurly(const FormatRequestContext& ctx, SourceFile* sourceFile,
											 int position) {
	Node* openingCurly =
		findImmediatelyPrecedingTokenOfKind(position, Kind::OpenBraceToken, sourceFile);
	if (openingCurly == nullptr) {
		return {};
	}
	Node* curlyBraceRange = openingCurly->parent;
	Node* outermostNode = findOutermostNodeWithinListLevel(curlyBraceRange);
	/**
	 * We limit the span to end at the opening curly to handle the case where
	 * the brace matched to that just typed will be incorrect after further edits.
	 * For example, we could type the opening curly for the following method
	 * body without brace-matching activated:
	 * ```
	 * class C {
	 *     foo()
	 * }
	 * ```
	 * and we wouldn't want to move the closing brace.
	 */
	TextRange textRange{
		TextPos(GetLineStartPositionForPosition(
			getTokenPosOfNode(outermostNode, sourceFile, false), sourceFile)),
		TextPos(position),
	};
	return FormatSpan(ctx, textRange, sourceFile,
					  FormatRequestKind::FormatOnOpeningCurlyBrace);
}

std::vector<TextChange> FormatOnClosingCurly(const FormatRequestContext& ctx, SourceFile* sourceFile,
											 int position) {
	Node* precedingToken =
		findImmediatelyPrecedingTokenOfKind(position, Kind::CloseBraceToken, sourceFile);
	return formatNodeLines(ctx, sourceFile,
						   findOutermostNodeWithinListLevel(precedingToken),
						   FormatRequestKind::FormatOnClosingCurlyBrace);
}

std::vector<TextChange> FormatOnSemicolon(const FormatRequestContext& ctx, SourceFile* sourceFile,
										  int position) {
	Node* semicolon =
		findImmediatelyPrecedingTokenOfKind(position, Kind::SemicolonToken, sourceFile);
	return formatNodeLines(ctx, sourceFile,
						   findOutermostNodeWithinListLevel(semicolon),
						   FormatRequestKind::FormatOnSemicolon);
}

std::vector<TextChange> FormatOnEnter(const FormatRequestContext& ctx, SourceFile* sourceFile,
									  int position) {
	int line = getECMALineOfPosition(sourceFile, position);
	if (line == 0) {
		return {};
	}
	// get start position for the previous line
	int startPos = static_cast<int>(getECMALineStarts(sourceFile)[line - 1]);
	// After the enter key, the cursor is now at a new line. The new line may or may not contain non-whitespace characters.
	// If the new line has only whitespaces, we won't want to format this line, because that would remove the indentation as
	// trailing whitespaces. So the end of the formatting span should be the later one between:
	//  1. the end of the previous line
	//  2. the last non-whitespace character in the current line
	int endOfFormatSpan = getECMAEndLinePosition(sourceFile, line);
	while (endOfFormatSpan > startPos) {
		int s;
		char32_t ch = decodeUtf8Rune(std::string_view(sourceFile->text).substr(endOfFormatSpan),
									 &s);
		if (s == 0 || isWhiteSpaceSingleLine(ch)) { // on multibyte character keep backing up
			endOfFormatSpan--;
			continue;
		}
		break;
	}

	// if the character at the end of the span is a line break, we shouldn't include it, because it indicates we don't want to
	// touch the current line at all. Also, on some OSes the line break consists of two characters (\r\n), we should test if the
	// previous character before the end of format span is line break character as well.
	char32_t ch = decodeUtf8Rune(std::string_view(sourceFile->text).substr(endOfFormatSpan),
								 nullptr);
	if (isLineBreak(ch)) {
		endOfFormatSpan--;
	}

	TextRange span{
		TextPos(startPos),
		// end value is exclusive so add 1 to the result
		TextPos(endOfFormatSpan + 1),
	};

	return FormatSpan(ctx, span, sourceFile, FormatRequestKind::FormatOnEnter);
}

} // namespace tsc::format

// === dep stubs — removed when owner slice lands ===
namespace tsc::lsutil {

bool PositionBelongsToNode(Node* /*current*/, int /*pos*/, SourceFile* /*sourceFile*/) {
	TSC_UNREACHABLE("lsutil::PositionBelongsToNode — owned by ls slice");
}

Node* GetFirstToken(Node* /*n*/, SourceFile* /*sourceFile*/) {
	TSC_UNREACHABLE("lsutil::GetFirstToken — owned by ls slice");
}

bool PositionIsASICandidate(int /*position*/, Node* /*context*/, SourceFile* /*currentFile*/) {
	TSC_UNREACHABLE("lsutil::PositionIsASICandidate — owned by ls slice");
}

// === slice: api ===
// NewDefaultUserPreferences — userpreferences.go:16.
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
// === end slice: api ===

} // namespace tsc::lsutil
