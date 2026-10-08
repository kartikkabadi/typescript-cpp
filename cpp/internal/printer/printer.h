// Port of tsc/internal/printer/printer.go + emittextwriter.go +
// textwriter.go + singlelinestringwriter.go + semicolon_writer.go +
// emithost.go + sourcefilemetadataprovider.go + utilities.go decls +
// helpers.go globals + syntheticfile.go — the node-to-text emitter.
//
// Namespace tsc::printer. EmitTextWriter/PrinterOptions keep their Go method
// names (capitalized) since they form the externally-facing surface.
#pragma once

#include "internal/ast/ast.h"
#include "internal/ast/precedence.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/printer/emitcontext.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/tspath/tspath.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// Go has two EmitResolver types: printer.EmitResolver (interface,
// printer/emitresolver.go:76) and checker.EmitResolver (the concrete impl,
// checker/emitresolver.go:34). The interface has exactly one implementation,
// so C++ maps it with an alias to the concrete checker::EmitResolver.
namespace tsc::checker {
struct EmitResolver;
struct SourceOutputAndProjectReference;
}

namespace tsc::printer {

using EmitResolver = ::tsc::checker::EmitResolver;

// --- PrinterOptions / PrintHandlers (printer.go:35,55) ------------------------

struct PrinterOptions {
	bool RemoveComments = false;
	NewLineKind NewLine{};
	bool OmitTrailingSemicolon = false;
	bool NoEmitHelpers = false;
	ScriptTarget Target{};
	bool SourceMap = false;
	bool InlineSourceMap = false;
	bool InlineSources = false;
	bool OmitBraceSourceMapPositions = false;
	bool OnlyPrintJSDocStyle = false;
	bool NeverAsciiEscape = false;
	bool PreserveSourceNewlines = false;
	bool TerminateUnterminatedLiterals = false;
};

// Result of PrintHandlers::MapSourcePosition — Go returns
// (mappedSource, mappedPos, ok).
// Adapter: ast.SourceFile implements sourcemap.Source via this wrapper
// (Go has SourceFile implement the interface directly; C++ can't cross the
// package boundary, so we wrap).
struct SourceFileSource final : sourcemap::Source {
	SourceFile* sf = nullptr;
	std::string_view FileName() override { return sf->FileName(); }
	std::string_view Text() override { return sf->text; }
	const std::vector<TextPos>& ECMALineMap() override {
		return sf->ecmaLineMap();
	}
};

struct MappedSourcePosition {
	sourcemap::Source* source = nullptr;
	TextPos pos = 0;
	bool ok = false;
};

struct PrintHandlers {
	// Hook used when generating unique names to avoid collisions with
	// globally defined names outside the current source file.
	std::function<bool(const std::string&)> HasGlobalName;
	// Composes source-map positions before they reach the generator;
	// ok=false emits a generated-only mapping.
	std::function<MappedSourcePosition(sourcemap::Source*, TextPos)>
		MapSourcePosition;

	std::function<void(Node*)> OnBeforeEmitNode;
	std::function<void(Node*)> OnAfterEmitNode;
	std::function<void(NodeList*)> OnBeforeEmitNodeList;
	std::function<void(NodeList*)> OnAfterEmitNodeList;
	std::function<void(Node*)> OnBeforeEmitToken;
	std::function<void(Node*)> OnAfterEmitToken;
};

// --- EmitTextWriter (emittextwriter.go) ---------------------------------------

struct EmitTextWriter {
	virtual ~EmitTextWriter() = default;
	virtual void Write(const std::string& s) = 0;
	virtual void WriteTrailingSemicolon(const std::string& text) = 0;
	virtual void WriteComment(const std::string& text) = 0;
	virtual void WriteKeyword(const std::string& text) = 0;
	virtual void WriteOperator(const std::string& text) = 0;
	virtual void WritePunctuation(const std::string& text) = 0;
	virtual void WriteSpace(const std::string& text) = 0;
	virtual void WriteStringLiteral(const std::string& text) = 0;
	virtual void WriteParameter(const std::string& text) = 0;
	virtual void WriteProperty(const std::string& text) = 0;
	virtual void WriteSymbol(const std::string& text, Symbol* symbol) = 0;
	virtual void WriteLine() = 0;
	virtual void WriteLineForce(bool force) = 0;
	virtual void IncreaseIndent() = 0;
	virtual void DecreaseIndent() = 0;
	virtual void Clear() = 0;
	virtual std::string String() = 0;
	virtual void RawWrite(const std::string& s) = 0;
	virtual void WriteLiteral(const std::string& s) = 0;
	virtual int GetTextPos() = 0;
	virtual int GetLine() = 0;
	virtual TextPos GetColumn() = 0;
	virtual int GetIndent() = 0;
	virtual bool IsAtStartOfLine() = 0;
	virtual bool HasTrailingComment() = 0;
	virtual bool HasTrailingWhitespace() = 0;
	// Grow is an optional hint (Go: interface{ Grow(n int) }).
	virtual void Grow(size_t n) {}
};

// NewTextWriter (textwriter.go:218).
EmitTextWriter* NewTextWriter(const std::string& newLine, int indentSize);
int GetDefaultIndentSize();

// GetSingleLineStringWriter (singlelinestringwriter.go:21) — pooled writer
// plus its release func.
std::pair<EmitTextWriter*, std::function<void()>> GetSingleLineStringWriter();

// getTrailingSemicolonDeferringWriter (semicolon_writer.go:12).
EmitTextWriter* getTrailingSemicolonDeferringWriter(EmitTextWriter* writer);

// --- EmitHost / SourceFileMetaDataProvider (emithost.go /
// sourcefilemetadataprovider.go) — interfaces for hosts driving emit. ---

struct EmitHost {
	virtual ~EmitHost() = default;
	virtual const CompilerOptions* Options() = 0;
	virtual std::vector<SourceFile*> SourceFiles() = 0;
	virtual bool UseCaseSensitiveFileNames() = 0;
	virtual std::string GetCurrentDirectory() = 0;
	virtual std::string CommonSourceDirectory() = 0;
	virtual bool IsEmitBlocked(std::string_view file) = 0;
	// Go returns error; std::nullopt == nil.
	virtual std::optional<std::string> WriteFile(std::string_view fileName,
	                                             std::string_view text) = 0;
	virtual ModuleKind GetEmitModuleFormatOfFile(SourceFile* file) = 0;
	virtual EmitResolver* GetEmitResolver() = 0;
	virtual ::tsc::checker::SourceOutputAndProjectReference*
	GetProjectReferenceFromSource(const tspath::Path& path) = 0;
	virtual bool IsSourceFileFromExternalLibrary(SourceFile* file) = 0;
};

struct SourceFileMetaDataProvider {
	virtual ~SourceFileMetaDataProvider() = default;
	virtual const SourceFileMetaData* GetSourceFileMetaData(
		tspath::Path path) = 0;
};

// --- utilities.go shared declarations -----------------------------------------

using getLiteralTextFlags = int32_t;
inline constexpr getLiteralTextFlags getLiteralTextFlagsNone = 0;
inline constexpr getLiteralTextFlags getLiteralTextFlagsNeverAsciiEscape = 1 << 0;
inline constexpr getLiteralTextFlags getLiteralTextFlagsJsxAttributeEscape = 1 << 1;
inline constexpr getLiteralTextFlags getLiteralTextFlagsTerminateUnterminatedLiterals = 1 << 2;
inline constexpr getLiteralTextFlags getLiteralTextFlagsAllowNumericSeparator = 1 << 3;

using QuoteChar = char32_t;
inline constexpr QuoteChar QuoteCharSingleQuote = U'\'';
inline constexpr QuoteChar QuoteCharDoubleQuote = U'"';
inline constexpr QuoteChar QuoteCharBacktick = U'`';

std::string EscapeString(std::string_view s, QuoteChar quoteChar);
std::string escapeNonAsciiString(std::string_view s, QuoteChar quoteChar);
std::string escapeJsxAttributeString(std::string_view s, QuoteChar quoteChar);
std::string getLiteralText(Node* node, SourceFile* sourceFile,
                           getLiteralTextFlags flags);

bool isNotPrologueDirective(Node* node);
bool RangeIsOnSingleLine(TextRange r, SourceFile* sourceFile);
bool RangeStartPositionsAreOnSameLine(TextRange range1, TextRange range2,
                                      SourceFile* sourceFile);
bool rangeEndPositionsAreOnSameLine(TextRange range1, TextRange range2,
                                    SourceFile* sourceFile);
bool rangeStartIsOnSameLineAsRangeEnd(TextRange range1, TextRange range2,
                                      SourceFile* sourceFile);
bool rangeEndIsOnSameLineAsRangeStart(TextRange range1, TextRange range2,
                                      SourceFile* sourceFile);
int getStartPositionOfRange(TextRange r, SourceFile* sourceFile,
                            bool includeComments);
bool PositionsAreOnSameLine(TextPos pos1, TextPos pos2,
                            SourceFile* sourceFile);
int GetLinesBetweenPositions(SourceFile* sourceFile, TextPos pos1,
                             TextPos pos2);
int getLinesBetweenRangeEndAndRangeStart(TextRange range1, TextRange range2,
                                         SourceFile* sourceFile,
                                         bool includeSecondRangeComments);
int getLinesBetweenPositionAndPrecedingNonWhitespaceCharacter(
	TextPos pos, TextPos stopPos, SourceFile* sourceFile, bool includeComments);
int getLinesBetweenPositionAndNextNonWhitespaceCharacter(
	TextPos pos, TextPos stopPos, SourceFile* sourceFile, bool includeComments);
TextPos getPreviousNonWhitespacePosition(TextPos pos, TextPos stopPos,
                                         SourceFile* sourceFile);
bool siblingNodePositionsAreComparable(EmitContext* emitContext,
                                       Node* previousNode, Node* nextNode);
NodeList* getContainingNodeArray(Node* node);
// canHaveDecorators — uses ast.h's identical inline.
bool originalNodesHaveSameParent(EmitContext* emitContext, Node* nodeA,
                                 Node* nodeB);
Node* skipSynthesizedParentheses(Node* node);
bool isNewExpressionWithoutArguments(Node* node);
bool isBinaryOperation(Node* node, Kind token);
bool mixingBinaryOperatorsRequiresParentheses(Kind a, Kind b);
bool isImmediatelyInvokedFunctionExpressionOrArrowFunction(Node* node);
bool hasLeadingHash(std::string_view text);
std::string removeLeadingHash(std::string_view text);
std::string ensureLeadingHash(std::string_view text);
std::string FormatGeneratedName(bool privateName, std::string_view prefix,
                                std::string_view base, std::string_view suffix);
bool isASCIIWordCharacter(char32_t ch);
std::string makeIdentifierFromModuleName(std::string_view moduleName);

template <class T>
int findSpanEnd(const std::vector<T>& array,
                const std::function<bool(const T&)>& test, int start) {
	for (int i = start; i < static_cast<int>(array.size()); i++) {
		if (!test(array[i])) {
			return i;
		}
	}
	return static_cast<int>(array.size());
}

template <class T>
int findSpanEndWithEmitContext(
	EmitContext* c, const std::vector<T>& array,
	const std::function<bool(EmitContext*, const T&)>& test, int start) {
	for (int i = start; i < static_cast<int>(array.size()); i++) {
		if (!test(c, array[i])) {
			return i;
		}
	}
	return static_cast<int>(array.size());
}

bool IsRecognizedTripleSlashComment(std::string_view text, CommentRange comment);
bool isJSDocLikeText(std::string_view text, CommentRange comment);
bool IsPinnedComment(std::string_view text, CommentRange comment);
int calculateIndent(std::string_view text, TextPos pos, TextPos end);

// lineCharacterCache (utilities.go:894) — caches line starts for a sourcemap
// source.
struct lineCharacterCache {
	std::vector<TextPos> lineMap;
	std::string text;
	int cachedLine = 0;
	TextPos cachedPos = 0;
	TextPos cachedChar = 0;
	bool hasCached = false;

	explicit lineCharacterCache(sourcemap::Source* source);
	std::pair<int, TextPos> getLineAndCharacter(TextPos pos);
};

// --- helpers.go globals --------------------------------------------------------
// EmitHelper package-level values (helpers.go). Defined in helpers.cpp.

extern EmitHelper* decorateHelper;
extern EmitHelper* metadataHelper;
extern EmitHelper* paramHelper;
extern EmitHelper* addDisposableResourceHelper;
extern EmitHelper* disposeResourcesHelper;
extern EmitHelper* classPrivateFieldGetHelper;
extern EmitHelper* classPrivateFieldSetHelper;
extern EmitHelper* classPrivateFieldInHelper;
extern EmitHelper* awaitHelper;
extern EmitHelper* asyncGeneratorHelper;
extern EmitHelper* asyncDelegatorHelper;
extern EmitHelper* asyncValuesHelper;
extern EmitHelper* restHelper;
extern EmitHelper* awaiterHelper;
extern EmitHelper* AsyncSuperHelper;
extern EmitHelper* AdvancedAsyncSuperHelper;
extern EmitHelper* esDecorateHelper;
extern EmitHelper* runInitializersHelper;
extern EmitHelper* makeTemplateObjectHelper;
extern EmitHelper* propKeyHelper;
extern EmitHelper* setFunctionNameHelper;
extern EmitHelper* createBindingHelper;
extern EmitHelper* setModuleDefaultHelper;
extern EmitHelper* importStarHelper;
extern EmitHelper* importDefaultHelper;
extern EmitHelper* exportStarHelper;
extern EmitHelper* rewriteRelativeImportExtensionsHelper;

int compareEmitHelpers(const EmitHelper* x, const EmitHelper* y);

// --- syntheticfile.go -----------------------------------------------------------

// ChangeTrackerWriter (changetrackerwriter.go:12) — an EmitTextWriter that
// records the last non-trivia text position before/after each emitted node so
// change tracking can reassign positions to freshly printed nodes.
class ChangeTrackerWriter : public EmitTextWriter {
	std::unique_ptr<EmitTextWriter> tw;
	int lastNonTriviaPosition = 0;
	// Go: map[triviaPositionKey]int — keys are *ast.Node | *ast.NodeList;
	// interface identity == pointer identity.
	std::unordered_map<const void*, int> pos_, end_;

	void setPos(const void* node) { pos_[node] = lastNonTriviaPosition; }
	void setEnd(const void* node) { end_[node] = lastNonTriviaPosition; }
	int getPos(const void* node) {
		if (auto it = pos_.find(node); it != pos_.end()) return it->second;
		return 0;
	}
	int getEnd(const void* node) {
		if (auto it = end_.find(node); it != end_.end()) return it->second;
		return 0;
	}
	void setLastNonTriviaPosition(const std::string& s, bool force);
	Node* assignPositionsToNodeWorker(Node* node, NodeVisitor* v);
	NodeList* assignPositionsToNodeArray(NodeList* nodes, NodeVisitor* v);

public:
	explicit ChangeTrackerWriter(std::unique_ptr<EmitTextWriter> w) : tw(std::move(w)) {
		tw->Clear();
	}

	PrintHandlers GetPrintHandlers();
	Node* AssignPositionsToNode(Node* node, tsc::NodeFactory* factory);

	void Write(const std::string& s) override {
		tw->Write(s);
		setLastNonTriviaPosition(s, false);
	}
	void WriteTrailingSemicolon(const std::string& s) override {
		tw->WriteTrailingSemicolon(s);
		setLastNonTriviaPosition(s, false);
	}
	void WriteComment(const std::string& s) override { tw->WriteComment(s); }
	void WriteKeyword(const std::string& s) override {
		tw->WriteKeyword(s);
		setLastNonTriviaPosition(s, false);
	}
	void WriteOperator(const std::string& s) override {
		tw->WriteOperator(s);
		setLastNonTriviaPosition(s, false);
	}
	void WritePunctuation(const std::string& s) override {
		tw->WritePunctuation(s);
		setLastNonTriviaPosition(s, false);
	}
	void WriteSpace(const std::string& s) override {
		tw->WriteSpace(s);
		setLastNonTriviaPosition(s, false);
	}
	void WriteStringLiteral(const std::string& s) override {
		tw->WriteStringLiteral(s);
		setLastNonTriviaPosition(s, false);
	}
	void WriteParameter(const std::string& s) override {
		tw->WriteParameter(s);
		setLastNonTriviaPosition(s, false);
	}
	void WriteProperty(const std::string& s) override {
		tw->WriteProperty(s);
		setLastNonTriviaPosition(s, false);
	}
	void WriteSymbol(const std::string& s, Symbol* symbol) override {
		tw->WriteSymbol(s, symbol);
		setLastNonTriviaPosition(s, false);
	}
	void WriteLine() override { tw->WriteLine(); }
	void WriteLineForce(bool force) override { tw->WriteLineForce(force); }
	void IncreaseIndent() override { tw->IncreaseIndent(); }
	void DecreaseIndent() override { tw->DecreaseIndent(); }
	void Clear() override {
		tw->Clear();
		lastNonTriviaPosition = 0;
	}
	std::string String() override { return tw->String(); }
	void RawWrite(const std::string& s) override {
		tw->RawWrite(s);
		setLastNonTriviaPosition(s, false);
	}
	void WriteLiteral(const std::string& s) override {
		tw->WriteLiteral(s);
		setLastNonTriviaPosition(s, true);
	}
	int GetTextPos() override { return tw->GetTextPos(); }
	int GetLine() override { return tw->GetLine(); }
	TextPos GetColumn() override { return tw->GetColumn(); }
	int GetIndent() override { return tw->GetIndent(); }
	bool IsAtStartOfLine() override { return tw->IsAtStartOfLine(); }
	bool HasTrailingComment() override { return tw->HasTrailingComment(); }
	bool HasTrailingWhitespace() override { return tw->HasTrailingWhitespace(); }
};

// NewChangeTrackerWriter (changetrackerwriter.go:25).
ChangeTrackerWriter* NewChangeTrackerWriter(const std::string& newline,
                                            int indentSize);

// PrintAndPositionNode (syntheticfile.go:15).
std::pair<std::string, Node*> PrintAndPositionNode(
	tsc::NodeFactory* factory, Node* node, SourceFile* sourceFile,
	const std::string& newLine, int indentSize, EmitContext* emitContext);

// CreateSyntheticSourceFile (syntheticfile.go:35).
SourceFile* CreateSyntheticSourceFile(tsc::NodeFactory* factory, Node* node,
                                      const std::string& text,
                                      SourceFileParseOptions parseOptions);

// --- printer.go ------------------------------------------------------------------

// WriteKind (printer.go:268).
enum class WriteKind : int32_t {
	None = 0,
	Keyword,
	Operator,
	Punctuation,
	StringLiteral,
	Parameter,
	Property,
	Comment,
	Literal,
};

// tokenEmitFlags (printer.go:6201).
using tokenEmitFlags = uint32_t;
inline constexpr tokenEmitFlags tefNoComments = 1 << 0;
inline constexpr tokenEmitFlags tefIndentLeadingComments = 1 << 1;
inline constexpr tokenEmitFlags tefNoSourceMaps = 1 << 2;
inline constexpr tokenEmitFlags tefNone = 0;

// commentSeparator (printer.go:5757) — plain enum, not bit flags.
using commentSeparator = uint32_t;
inline constexpr commentSeparator commentSeparatorNone = 0;
inline constexpr commentSeparator commentSeparatorBefore = 1;
inline constexpr commentSeparator commentSeparatorAfter = 2;

// ListFormat (printer.go:6223).
using ListFormat = int32_t;
inline constexpr ListFormat LFNone = 0;
inline constexpr ListFormat LFSingleLine = 0;
inline constexpr ListFormat LFMultiLine = 1 << 0;
inline constexpr ListFormat LFPreserveLines = 1 << 1;
inline constexpr ListFormat LFLinesMask =
	LFSingleLine | LFMultiLine | LFPreserveLines;
inline constexpr ListFormat LFNotDelimited = 0;
inline constexpr ListFormat LFBarDelimited = 1 << 2;
inline constexpr ListFormat LFAmpersandDelimited = 1 << 3;
inline constexpr ListFormat LFCommaDelimited = 1 << 4;
inline constexpr ListFormat LFAsteriskDelimited = 1 << 5;
inline constexpr ListFormat LFDelimitersMask = LFBarDelimited |
	LFAmpersandDelimited | LFCommaDelimited | LFAsteriskDelimited;
inline constexpr ListFormat LFAllowTrailingComma = 1 << 6;
inline constexpr ListFormat LFIndented = 1 << 7;
inline constexpr ListFormat LFSpaceBetweenBraces = 1 << 8;
inline constexpr ListFormat LFSpaceBetweenSiblings = 1 << 9;
inline constexpr ListFormat LFBraces = 1 << 10;
inline constexpr ListFormat LFParenthesis = 1 << 11;
inline constexpr ListFormat LFAngleBrackets = 1 << 12;
inline constexpr ListFormat LFSquareBrackets = 1 << 13;
inline constexpr ListFormat LFBracketsMask =
	LFBraces | LFParenthesis | LFAngleBrackets | LFSquareBrackets;
inline constexpr ListFormat LFOptionalIfNil = 1 << 14;
inline constexpr ListFormat LFOptionalIfEmpty = 1 << 15;
inline constexpr ListFormat LFOptional = LFOptionalIfNil | LFOptionalIfEmpty;
inline constexpr ListFormat LFPreferNewLine = 1 << 16;
inline constexpr ListFormat LFNoTrailingNewLine = 1 << 17;
inline constexpr ListFormat LFNoInterveningComments = 1 << 18;
inline constexpr ListFormat LFNoSpaceIfEmpty = 1 << 19;
inline constexpr ListFormat LFSingleElement = 1 << 20;
inline constexpr ListFormat LFSpaceAfterList = 1 << 21;

// Precomputed Formats
inline constexpr ListFormat LFModifiers = LFSingleLine |
	LFSpaceBetweenSiblings | LFNoInterveningComments | LFSpaceAfterList;
inline constexpr ListFormat LFHeritageClauses =
	LFSingleLine | LFSpaceBetweenSiblings;
inline constexpr ListFormat LFSingleLineTypeLiteralMembers = LFSingleLine |
	LFSpaceBetweenBraces | LFSpaceBetweenSiblings;
inline constexpr ListFormat LFMultiLineTypeLiteralMembers = LFMultiLine |
	LFIndented | LFOptionalIfEmpty;
inline constexpr ListFormat LFSingleLineTupleTypeElements = LFCommaDelimited |
	LFSpaceBetweenSiblings | LFSingleLine;
inline constexpr ListFormat LFMultiLineTupleTypeElements = LFCommaDelimited |
	LFIndented | LFSpaceBetweenSiblings | LFMultiLine;
inline constexpr ListFormat LFUnionTypeConstituents = LFBarDelimited |
	LFSpaceBetweenSiblings | LFSingleLine;
inline constexpr ListFormat LFIntersectionTypeConstituents =
	LFAmpersandDelimited | LFSpaceBetweenSiblings | LFSingleLine;
inline constexpr ListFormat LFObjectBindingPatternElements = LFSingleLine |
	LFAllowTrailingComma | LFSpaceBetweenBraces | LFCommaDelimited |
	LFSpaceBetweenSiblings | LFNoSpaceIfEmpty;
inline constexpr ListFormat LFArrayBindingPatternElements = LFSingleLine |
	LFAllowTrailingComma | LFCommaDelimited | LFSpaceBetweenSiblings |
	LFNoSpaceIfEmpty;
inline constexpr ListFormat LFObjectLiteralExpressionProperties =
	LFPreserveLines | LFCommaDelimited | LFSpaceBetweenSiblings |
	LFSpaceBetweenBraces | LFIndented | LFBraces | LFNoSpaceIfEmpty;
inline constexpr ListFormat LFImportAttributes = LFPreserveLines |
	LFCommaDelimited | LFSpaceBetweenSiblings | LFSpaceBetweenBraces |
	LFIndented | LFBraces | LFNoSpaceIfEmpty;
inline constexpr ListFormat LFArrayLiteralExpressionElements = LFPreserveLines |
	LFCommaDelimited | LFSpaceBetweenSiblings | LFAllowTrailingComma |
	LFIndented | LFSquareBrackets;
inline constexpr ListFormat LFCommaListElements = LFCommaDelimited |
	LFSpaceBetweenSiblings | LFSingleLine;
inline constexpr ListFormat LFCallExpressionArguments = LFCommaDelimited |
	LFSpaceBetweenSiblings | LFSingleLine | LFParenthesis;
inline constexpr ListFormat LFNewExpressionArguments = LFCommaDelimited |
	LFSpaceBetweenSiblings | LFSingleLine | LFParenthesis | LFOptionalIfNil;
inline constexpr ListFormat LFTemplateExpressionSpans =
	LFSingleLine | LFNoInterveningComments;
inline constexpr ListFormat LFSingleLineBlockStatements =
	LFSpaceBetweenBraces | LFSpaceBetweenSiblings | LFSingleLine;
inline constexpr ListFormat LFMultiLineBlockStatements = LFIndented | LFMultiLine;
inline constexpr ListFormat LFVariableDeclarationList = LFCommaDelimited |
	LFSpaceBetweenSiblings | LFSingleLine;
inline constexpr ListFormat LFSingleLineFunctionBodyStatements = LFSingleLine |
	LFSpaceBetweenSiblings | LFSpaceBetweenBraces;
inline constexpr ListFormat LFMultiLineFunctionBodyStatements = LFMultiLine;
inline constexpr ListFormat LFClassHeritageClauses = LFSingleLine;
inline constexpr ListFormat LFClassMembers = LFIndented | LFMultiLine;
inline constexpr ListFormat LFInterfaceMembers = LFIndented | LFMultiLine;
inline constexpr ListFormat LFEnumMembers = LFCommaDelimited | LFIndented |
	LFMultiLine;
inline constexpr ListFormat LFCaseBlockClauses = LFIndented | LFMultiLine;
inline constexpr ListFormat LFNamedImportsOrExportsElements = LFCommaDelimited |
	LFSpaceBetweenSiblings | LFAllowTrailingComma | LFSingleLine |
	LFSpaceBetweenBraces | LFNoSpaceIfEmpty;
inline constexpr ListFormat LFJsxElementOrFragmentChildren =
	LFSingleLine | LFNoInterveningComments;
inline constexpr ListFormat LFJsxElementAttributes = LFSingleLine |
	LFSpaceBetweenSiblings | LFNoInterveningComments;
inline constexpr ListFormat LFCaseOrDefaultClauseStatements = LFIndented |
	LFMultiLine | LFNoTrailingNewLine | LFOptionalIfEmpty;
inline constexpr ListFormat LFHeritageClauseTypes = LFCommaDelimited |
	LFSpaceBetweenSiblings | LFSingleLine;
inline constexpr ListFormat LFSourceFileStatements =
	LFMultiLine | LFNoTrailingNewLine;
inline constexpr ListFormat LFDecorators =
	LFMultiLine | LFOptional | LFSpaceAfterList;
inline constexpr ListFormat LFTypeArguments = LFCommaDelimited |
	LFSpaceBetweenSiblings | LFSingleLine | LFAngleBrackets | LFOptional;
inline constexpr ListFormat LFTypeParameters = LFCommaDelimited |
	LFSpaceBetweenSiblings | LFSingleLine | LFAngleBrackets | LFOptional;
inline constexpr ListFormat LFParameters = LFCommaDelimited |
	LFSpaceBetweenSiblings | LFSingleLine | LFParenthesis;
inline constexpr ListFormat LFSingleArrowParameter = LFCommaDelimited |
	LFSpaceBetweenSiblings | LFSingleLine;
inline constexpr ListFormat LFIndexSignatureParameters = LFCommaDelimited |
	LFSpaceBetweenSiblings | LFSingleLine | LFIndented | LFSquareBrackets;
inline constexpr ListFormat LFJSDocComment = LFMultiLine | LFAsteriskDelimited;
inline constexpr ListFormat LFImportClauseEntries = LFImportAttributes;

struct detachedCommentsInfo {
	TextPos nodePos = 0;
	TextPos detachedCommentEndPos = 0;
};

struct commentState {
	EmitFlags emitFlags = 0;
	TextRange commentRange;
	TextPos containerPos = 0;
	TextPos containerEnd = 0;
	TextPos declarationListContainerEnd = 0;
};

struct sourceMapState {
	EmitFlags emitFlags = 0;
	TextRange sourceMapRange;
	bool hasTokenSourceMapRange = false;
};

struct printerState {
	commentState* commentState_ = nullptr;
	sourceMapState* sourceMapState_ = nullptr;
};

// Printer (printer.go:117).
struct Printer {
	// PrintHandlers (embedded in Go)
	std::function<bool(const std::string&)> HasGlobalName;
	std::function<MappedSourcePosition(sourcemap::Source*, TextPos)>
		MapSourcePosition;
	std::function<void(Node*)> OnBeforeEmitNode;
	std::function<void(Node*)> OnAfterEmitNode;
	std::function<void(NodeList*)> OnBeforeEmitNodeList;
	std::function<void(NodeList*)> OnAfterEmitNodeList;
	std::function<void(Node*)> OnBeforeEmitToken;
	std::function<void(Node*)> OnAfterEmitToken;

	PrinterOptions Options;
	EmitContext* emitContext = nullptr;
	SourceFile* currentSourceFile = nullptr;
	std::unordered_map<std::string, Node*> uniqueHelperNames;
	// Go's `uniqueHelperNames != nil` — true once setSourceFile activates the
	// map for EFExternalHelpers.
	bool uniqueHelperNamesSet = false;
	Node* externalHelpersModuleName = nullptr;
	TextPos nextListElementPos = 0;
	EmitTextWriter* writer = nullptr;
	std::unique_ptr<EmitTextWriter> ownWriter;
	// Owned trailingSemicolonDeferringWriter while OmitTrailingSemicolon is
	// in effect inside Write (Go: wrapped interface value).
	std::unique_ptr<EmitTextWriter> deferringWriter;
	WriteKind writeKind = WriteKind::None;
	bool sourceMapsDisabled = true;
	sourcemap::Generator* sourceMapGenerator = nullptr;
	sourcemap::Source* sourceMapSource = nullptr;
	SourceFileSource sourceFileAdapter;
	sourcemap::SourceIndex sourceMapSourceIndex = 0;
	bool sourceMapSourceIsJson = false;
	std::unique_ptr<lineCharacterCache> sourceMapLineCharCache;
	sourcemap::Source* mostRecentSourceMapSource = nullptr;
	sourcemap::SourceIndex mostRecentSourceMapSourceIndex = 0;
	TextPos containerPos = 0;
	TextPos containerEnd = 0;
	TextPos declarationListContainerEnd = 0;
	Stack<detachedCommentsInfo> detachedCommentsInfoStack;
	bool commentsDisabled = false;
	bool inExtends = false;
	NameGenerator nameGenerator;
	std::function<std::string(const std::string&)>
		makeFileLevelOptimisticUniqueName;
	std::deque<commentState> commentStateArena;
	std::deque<sourceMapState> sourceMapStateArena;
	std::unordered_map<Node*, Symbol*> IdToSymbol;

	// owned SourceFileSource adapters (per Write call)
	std::vector<std::unique_ptr<sourcemap::Source>> ownedSources;

	std::string getLiteralTextOfNode(Node* node, SourceFile* sourceFile,
	                               getLiteralTextFlags flags);
	std::string getTextOfNode(Node* node, bool includeTrivia);

	// low-level writing
	void writeAs(const std::string& text, WriteKind writeKind_);
	void write(const std::string& text);
	WriteKind setWriteKind(WriteKind kind);
	void writeSymbol(const std::string& text, Symbol* optSymbol);
	void writeLiteral(const std::string& text);
	void writePunctuation(const std::string& text);
	void writeOperator(const std::string& text);
	void writeKeyword(const std::string& text);
	void writeProperty(const std::string& text);
	void writeParameter(const std::string& text);
	void writeComment(const std::string& text);
	void writeSpace();
	void writeLine();
	void writeLineRepeat(int count);
	void writeLines(const std::string& text);
	void writeTrailingSemicolon();
	void increaseIndent();
	void decreaseIndent();
	void increaseIndentIf(bool indentRequested);
	void decreaseIndentIf(bool indentRequested);
	void writeLineOrSpace(Node* parentNode, Node* prevChildNode,
	                      Node* nextChildNode);
	void writeLinesAndIndent(int lineCount, bool writeSpaceIfNotIndenting);
	bool writeLineSeparatorsAndIndentBefore(Node* node, Node* parent);
	void writeLineSeparatorsAfter(Node* node, Node* parent);
	int getLinesBetweenNodes(Node* parent, Node* node1, Node* node2);
	int getEffectiveLines(const std::function<int(bool)>& getLineDifference);
	int getLeadingLineTerminatorCount(Node* parentNode, Node* firstChild,
	                                  ListFormat format);
	int getSeparatingLineTerminatorCount(Node* previousNode, Node* nextNode,
	                                     ListFormat format);
	int getClosingLineTerminatorCount(Node* parentNode, Node* lastChild,
	                                  ListFormat format,
	                                  TextRange childrenTextRange);
	void writeCommentRange(CommentRange comment);
	void writeCommentRangeWorker(std::string_view text,
	                             const std::vector<TextPos>& lineMap, Kind kind,
	                             TextRange loc);
	bool shouldEmitComments(Node* node);
	bool shouldWriteComment(CommentRange comment);
	bool shouldEmitIndented(Node* node);
	bool shouldElideIndentation(Node* node);
	bool shouldEmitOnSingleLine(Node* node);
	bool shouldEmitOnMultipleLines(Node* node);
	bool shouldEmitBlockFunctionBodyOnSingleLine(Node* body);
	bool shouldEmitOnNewLine(Node* node, ListFormat format);
	bool shouldEmitSourceMaps(Node* node);
	bool shouldEmitTokenSourceMaps(Kind token, TextPos pos, Node* contextNode,
	                               tokenEmitFlags flags);
	bool shouldEmitLeadingComments(Node* node);
	bool shouldEmitTrailingComments(Node* node);
	bool shouldEmitNestedComments(Node* node);
	bool shouldEmitDetachedComments(Node* node);
	bool hasCommentsAtPosition(TextPos pos);
	bool shouldEmitIndirectCall(Node* node);
	bool shouldAllowTrailingComma(Node* node, NodeList* list);
	int writeTokenText(Kind token, WriteKind writeKind, TextPos pos);
	int emitToken(Kind token, TextPos pos, WriteKind writeKind,
	              Node* contextNode);
	int emitTokenEx(Kind token, TextPos pos, WriteKind writeKind,
	                Node* contextNode, tokenEmitFlags flags);
	void emitKeywordNode(Node* node);
	void emitKeywordNodeEx(Node* node, tokenEmitFlags flags);
	void emitPunctuationNode(Node* node);
	void emitPunctuationNodeEx(Node* node, tokenEmitFlags flags);
	void emitTokenNode(Node* node);
	void emitTokenNodeEx(Node* node, tokenEmitFlags flags);
	void emitLiteral(Node* node, getLiteralTextFlags flags);
	void emitNumericLiteral(Node* node);
	void emitBigIntLiteral(Node* node);
	void emitStringLiteral(Node* node);
	void emitNoSubstitutionTemplateLiteral(Node* node);
	void emitRegularExpressionLiteral(Node* node);
	void emitTemplateHead(Node* node);
	void emitTemplateMiddle(Node* node);
	void emitTemplateTail(Node* node);
	void emitTemplateMiddleTail(Node* node);
	void emitSnippetNode(Node* node, SnippetElement* snippetElement);
	void emitTabStop(Node* node, SnippetElement* snippetElement);
	void emitIdentifierText(Node* node);
	void emitIdentifierName(Node* node);
	void emitIdentifierNameNode(Node* node);
	Node* getUniqueHelperName(const std::string& name);
	void emitIdentifierReference(Node* node);
	void emitBindingIdentifier(Node* node);
	void emitLabelIdentifier(Node* node);
	void emitPrivateIdentifier(Node* node);
	void emitQualifiedName(Node* node);
	void emitComputedPropertyName(Node* node);
	void emitEntityName(Node* node);
	void emitBindingName(Node* node);
	void emitPropertyName(Node* node);
	void emitMemberName(Node* node);
	void emitModuleName(Node* node);
	void emitModuleExportName(Node* node);
	void emitImportAttributeName(Node* node);
	void emitNestedModuleName(Node* node);
	int emitModifierList(Node* parentNode, ModifierList* modifiers,
	                     bool allowDecorators);
	void emitTypeParameter(Node* node);
	void emitTypeParameterDeclarationNode(Node* node);
	void emitParameterName(Node* node);
	void emitParameter(Node* node);
	void emitParameterDeclarationNode(Node* node);
	void emitDecorator(Node* node);
	void emitModifierLike(Node* node);
	void emitTypeParameters(Node* parentNode, NodeList* nodes);
	void emitTypeAnnotation(Node* node);
	void emitInitializer(Node* node, TextPos equalTokenPos, Node* contextNode);
	void emitParameters(Node* parentNode, NodeList* parameters);
	bool canEmitSimpleArrowHead(Node* parentNode, NodeList* parameters);
	void emitParametersForArrow(Node* parentNode, NodeList* parameters);
	void emitParametersForIndexSignature(Node* parentNode, NodeList* parameters);
	void emitSignature(Node* node);
	void emitFunctionBody(Node* body);
	void emitFunctionBodyNode(Node* node);
	void emitPropertySignature(Node* node);
	void emitPropertyDeclaration(Node* node);
	void emitMethodSignature(Node* node);
	void emitMethodDeclaration(Node* node);
	void emitClassStaticBlockDeclaration(Node* node);
	void emitConstructor(Node* node);
	void emitAccessorDeclaration(Kind token, Node* node);
	void emitGetAccessorDeclaration(Node* node);
	void emitSetAccessorDeclaration(Node* node);
	void emitCallSignature(Node* node);
	void emitConstructSignature(Node* node);
	void emitIndexSignature(Node* node);
	void emitClassElement(Node* node);
	void emitTypeElement(Node* node);
	void emitObjectLiteralElement(Node* node);
	void emitKeywordTypeNode(Node* node);
	void emitTypePredicateParameterName(Node* node);
	void emitTypePredicate(Node* node);
	void emitTypeArgument(Node* node);
	void emitTypeArguments(Node* parentNode, NodeList* nodes);
	void emitTypeReference(Node* node);
	void emitReturnType(Node* node);
	void emitFunctionType(Node* node);
	void emitConstructorType(Node* node);
	void emitTypeQuery(Node* node);
	void emitTypeLiteral(Node* node);
	void emitArrayType(Node* node);
	void emitPostfixTypeOperand(Node* operand, Node* parent);
	void emitTupleElementType(Node* node);
	void emitTupleType(Node* node);
	void emitRestType(Node* node);
	void emitOptionalType(Node* node);
	void emitNamedTupleMember(Node* node);
	void emitUnionTypeConstituent(Node* node);
	void emitUnionType(Node* node);
	void emitIntersectionTypeConstituent(Node* node);
	void emitIntersectionType(Node* node);
	void emitConditionalType(Node* node);
	void emitInferTypeParameter(Node* node);
	void emitInferType(Node* node);
	void emitParenthesizedType(Node* node);
	void emitThisType(Node* node);
	void emitTypeOperator(Node* node);
	void emitIndexedAccessType(Node* node);
	void emitMappedTypeParameter(Node* node);
	void emitMappedType(Node* node);
	void emitLiteralType(Node* node);
	void emitTemplateTypeSpan(Node* node);
	void emitTemplateTypeSpanNode(Node* node);
	void emitTemplateType(Node* node);
	void emitImportTypeNodeAttributes(Node* node);
	void emitImportTypeNode(Node* node);
	void emitTypeNodeInExtends(Node* node);
	void emitTypeNodeOutsideExtends(Node* node);
	void emitTypeNodePreservingExtends(Node* node, TypePrecedence precedence);
	void emitTypeNode(Node* node, TypePrecedence precedence);
	void emitObjectBindingPattern(Node* node);
	void emitArrayBindingPattern(Node* node);
	void emitBindingElement(Node* node);
	void emitBindingElementNode(Node* node);
	void emitJSDocAllType(Node* node);
	void emitJSDocNonNullableType(Node* node);
	void emitJSDocNullableType(Node* node);
	void emitJSDocOptionalType(Node* node);
	void emitJSDocVariadicType(Node* node);
	void emitKeywordExpression(Node* node);
	void emitArrayLiteralExpressionElement(Node* node);
	void emitArrayLiteralExpression(Node* node);
	void emitObjectLiteralExpression(Node* node);
	bool mayNeedDotDotForPropertyAccess(Node* expression);
	void emitPropertyAccessExpression(Node* node);
	void emitElementAccessExpression(Node* node);
	void emitArgument(Node* node);
	void emitCallee(Node* callee, Node* parentNode);
	void emitCallExpression(Node* node);
	void emitNewExpression(Node* node);
	void emitTemplateLiteral(Node* node);
	void emitTaggedTemplateExpression(Node* node);
	void emitTypeAssertionExpression(Node* node);
	void emitParenthesizedExpression(Node* node);
	void emitFunctionExpression(Node* node);
	void emitConciseBody(Node* node);
	void emitArrowFunction(Node* node);
	void emitDeleteExpression(Node* node);
	void emitTypeOfExpression(Node* node);
	void emitVoidExpression(Node* node);
	void emitAwaitExpression(Node* node);
	void emitPrefixUnaryExpression(Node* node);
	void emitPostfixUnaryExpression(Node* node);
	Kind getLiteralKindOfBinaryPlusOperand(Node* node);
	std::pair<OperatorPrecedence, OperatorPrecedence>
	getBinaryExpressionPrecedence(Node* node);
	void emitBinaryExpression(Node* node);
	void emitShortCircuitExpression(Node* node);
	void emitConditionalExpression(Node* node);
	void emitTemplateExpression(Node* node);
	void emitYieldExpression(Node* node);
	void emitSpreadElement(Node* node);
	void emitClassExpression(Node* node);
	void emitOmittedExpression(Node* node);
	void emitExpressionWithTypeArguments(Node* node);
	void emitAsExpression(Node* node);
	void emitSatisfiesExpression(Node* node);
	void emitNonNullExpression(Node* node);
	void emitMetaProperty(Node* node);
	void emitPartiallyEmittedExpression(Node* node);
	bool commentWillEmitNewLine(CommentRange comment);
	bool syntheticCommentWillEmitNewLine(const SynthesizedComment& comment);
	bool willEmitLeadingNewLine(Node* node);
	Node* parenthesizeExpressionForNoAsi(Node* node);
	void emitExpressionNoASI(Node* node, OperatorPrecedence precedence);
	void emitExpression(Node* node, OperatorPrecedence precedence);
	void emitTemplateSpan(Node* node);
	void emitTemplateSpanNode(Node* node);
	void emitSemicolonClassElement(Node* node);
	bool isEmptyBlock(Node* block, NodeList* statements);
	void emitBlock(Node* node);
	void emitVariableStatement(Node* node);
	void emitEmptyStatement(Node* node, bool isEmbeddedStatement);
	void emitExpressionStatement(Node* node);
	void emitIIFEWithParenthesizedCallee(Node* node);
	void emitIfStatement(Node* node);
	void emitWhileClause(Node* node, Node* expression, TextPos startPos);
	void emitDoStatement(Node* node);
	void emitWhileStatement(Node* node);
	void emitForInitializer(Node* node);
	void emitForStatement(Node* node);
	void emitForInStatement(Node* node);
	void emitForOfStatement(Node* node);
	void emitContinueStatement(Node* node);
	void emitBreakStatement(Node* node);
	void emitReturnStatement(Node* node);
	void emitWithStatement(Node* node);
	void emitSwitchStatement(Node* node);
	void emitLabeledStatement(Node* node);
	void emitThrowStatement(Node* node);
	void emitTryStatement(Node* node);
	void emitDebuggerStatement(Node* node);
	void emitNotEmittedStatement(Node* node);
	void emitNotEmittedTypeElement(Node* node);
	void emitVariableDeclaration(Node* node);
	void emitVariableDeclarationNode(Node* node);
	void emitVariableDeclarationList(Node* node);
	void emitFunctionDeclaration(Node* node);
	void emitClassDeclaration(Node* node);
	void emitInterfaceDeclaration(Node* node);
	void emitTypeAliasDeclaration(Node* node);
	void emitEnumDeclaration(Node* node);
	void emitModuleDeclaration(Node* node);
	void emitModuleBlock(Node* node);
	void emitCaseBlock(Node* node);
	void emitImportEqualsDeclaration(Node* node);
	void emitModuleReference(Node* node);
	void emitImportDeclaration(Node* node);
	void emitImportClause(Node* node);
	void emitNamespaceImport(Node* node);
	void emitNamedImports(Node* node);
	void emitNamedImportBindings(Node* node);
	void emitImportSpecifier(Node* node);
	void emitImportSpecifierNode(Node* node);
	void emitExportAssignment(Node* node);
	void emitExportDeclaration(Node* node);
	void emitImportAttributes(Node* node);
	void emitImportAttribute(Node* node);
	void emitImportAttributeNode(Node* node);
	void emitNamespaceExportDeclaration(Node* node);
	void emitNamespaceExport(Node* node);
	void emitNamedExports(Node* node);
	void emitNamedExportBindings(Node* node);
	void emitExportSpecifier(Node* node);
	void emitExportSpecifierNode(Node* node);
	void emitEmbeddedStatement(Node* parentNode, Node* node);
	void emitStatement(Node* node);
	void emitExternalModuleReference(Node* node);
	void emitJsxElement(Node* node);
	void emitJsxSelfClosingElement(Node* node);
	void emitJsxFragment(Node* node);
	void emitJsxOpeningElement(Node* node);
	void emitJsxClosingElement(Node* node);
	void emitJsxOpeningFragment(Node* node);
	void emitJsxClosingFragment(Node* node);
	void emitJsxText(Node* node);
	void emitJsxAttributes(Node* node);
	void emitJsxAttribute(Node* node);
	void emitJsxSpreadAttribute(Node* node);
	void emitJsxAttributeLike(Node* node);
	void emitJsxExpression(Node* node);
	void emitJsxNamespacedName(Node* node);
	void emitJsxChild(Node* node);
	void emitJsxTagName(Node* node);
	void emitJsxAttributeName(Node* node);
	void emitJsxAttributeValue(Node* node);
	void emitCaseOrDefaultClauseStatements(Node* node, TextPos colonPos);
	void emitCaseClause(Node* node);
	void emitDefaultClause(Node* node);
	void emitCaseOrDefaultClauseNode(Node* node);
	void emitHeritageClause(Node* node);
	void emitHeritageClauseElement(Node* node);
	void emitHeritageClauseNode(Node* node);
	void emitCatchClause(Node* node);
	void emitPropertyAssignment(Node* node);
	void emitShorthandPropertyAssignment(Node* node);
	void emitSpreadAssignment(Node* node);
	void emitEnumMember(Node* node);
	void emitEnumMemberNode(Node* node);
	void emitJSDocNode(Node* node);
	void emitShebangIfNeeded(Node* node);
	int emitPrologueDirectives(NodeList* statements);
	bool emitHelpers(Node* node);
	void emitSourceFile(Node* node);
	void emitTripleSlashDirectives(Node* node);
	void emitDirective(const std::string& kind,
	                   const std::vector<FileReference*>& refs);
	void emitList(const std::function<void(Printer*, Node*)>& emit,
	              Node* parentNode, NodeList* children, ListFormat format);
	void emitListRange(const std::function<void(Printer*, Node*)>& emit,
	                   Node* parentNode, NodeList* children, ListFormat format,
	                   int start, int count);
	bool hasTrailingComma(Node* parentNode, NodeList* children);
	void writeDelimiter(ListFormat format);
	void emitListItems(const std::function<void(Printer*, Node*)>& emit,
	                   Node* parentNode, const std::vector<Node*>& children,
	                   ListFormat format, bool hasTrailingComma_,
	                   TextRange childrenTextRange);
	std::string Emit(Node* node, SourceFile* sourceFile);
	std::string EmitSourceFile(SourceFile* sourceFile);
	void setSourceFile(SourceFile* sourceFile);
	void Write(Node* node, SourceFile* sourceFile, EmitTextWriter* writer,
	           sourcemap::Generator* sourceMapGenerator);
	commentState* emitCommentsBeforeNode(Node* node);
	void emitCommentsAfterNode(Node* node, commentState* state);
	std::pair<commentState*, int> emitCommentsBeforeToken(
		Kind token, TextPos pos, Node* contextNode, tokenEmitFlags flags);
	void emitCommentsAfterToken(Kind token, TextPos pos, Node* contextNode,
	                            commentState* state);
	commentState* emitDetachedCommentsBeforeStatementList(Node* node,
	                                                      TextRange detachedRange);
	void emitDetachedCommentsAfterStatementList(Node* node,
	                                            TextRange detachedRange,
	                                            commentState* state);
	void emitLeadingCommentsOfNode(Node* node, EmitFlags emitFlags,
	                               TextRange commentRange);
	void emitTrailingCommentsOfNode(Node* node, EmitFlags emitFlags,
	                                TextRange commentRange, TextPos containerPos,
	                                TextPos containerEnd,
	                                TextPos declarationListContainerEnd);
	void emitLeadingSyntheticCommentsOfNode(Node* node, EmitFlags emitFlags);
	void emitLeadingSynthesizedComment(const SynthesizedComment& comment);
	void emitTrailingSyntheticCommentsOfNode(Node* node, EmitFlags emitFlags);
	void emitTrailingSynthesizedComment(const SynthesizedComment& comment);
	std::string formatSynthesizedComment(const SynthesizedComment& comment);
	void writeSynthesizedComment(const SynthesizedComment& comment);
	bool emitLeadingComments(TextPos pos, bool elided);
	bool shouldEmitCommentIfTripleSlash(CommentRange comment, Tristate tripleSlash);
	bool shouldEmitNewLineBeforeLeadingCommentOfPosition(TextPos pos,
	                                                     TextPos commentPos);
	void emitLeadingCommentsOfPosition(TextPos pos);
	void emitTrailingComments(TextPos pos, commentSeparator separator);
	void emitTrailingCommentsOfPosition(TextPos pos, bool prefixSpace,
	                                    bool forceNoNewline);
	void emitDetachedCommentsAndUpdateCommentsInfo(TextRange textRange);
	std::pair<detachedCommentsInfo, bool> emitDetachedComments(
		TextRange textRange);
	bool emitComments(const std::vector<CommentRange>& comments,
	                  commentSeparator separator);
	void emitComment(CommentRange comment);
	bool isTripleSlashComment(CommentRange comment);
	void setSourceMapSource(sourcemap::Source* source);
	void emitPos(TextPos pos);
	void emitSourcePos(sourcemap::Source* source, TextPos pos);
	sourceMapState* emitSourceMapsBeforeNode(Node* node);
	void emitSourceMapsAfterNode(Node* node, sourceMapState* previousState);
	sourceMapState* emitSourceMapsBeforeToken(Kind token, TextPos pos,
	                                          Node* contextNode,
	                                          tokenEmitFlags flags);
	void emitSourceMapsAfterToken(Kind token, TextPos pos, Node* contextNode,
	                              sourceMapState* previousState);
	bool shouldReuseTempVariableScope(Node* node);
	void pushNameGenerationScope(Node* node);
	void popNameGenerationScope(Node* node);
	void generateAllNames(NodeList* nodes);
	void generateNames(Node* node);
	void generateAllMemberNames(NodeList* nodes);
	void generateMemberNames(Node* node);
	void generateNameIfNeeded(Node* name);
	void generateName(Node* name);
	bool isFileLevelUniqueNameInCurrentFile(const std::string& name, bool);
	printerState enterNode(Node* node);
	void exitNode(Node* node, printerState previousState);
	printerState enterTokenNode(Node* node, tokenEmitFlags flags);
	void exitTokenNode(Node* node, printerState previousState);
	std::pair<printerState, int> enterToken(Kind token, TextPos pos,
	                                        Node* contextNode,
	                                        tokenEmitFlags flags);
	void exitToken(Kind token, TextPos pos, Node* contextNode,
	               printerState previousState);
};

// NewPrinter (printer.go:173).
Printer* NewPrinter(const PrinterOptions& options, const PrintHandlers& handlers,
                    EmitContext* emitContext);

std::string getOpeningBracket(ListFormat format);
std::string getClosingBracket(ListFormat format);

// greatestEnd (utilities.go:577-611) — max of end() over heterogeneous args
// (Node*, NodeList*, ModifierList*, TextRange). Go iterates args backward;
// since it is a pure max, order is irrelevant.
namespace greatestEndDetail {
inline bool tryGetEnd(Node* n, TextPos* out) {
	if (n == nullptr) {
		return false;
	}
	*out = n->end();
	return true;
}
inline bool tryGetEnd(NodeList* n, TextPos* out) {
	if (n == nullptr) {
		return false;
	}
	*out = n->end();
	return true;
}
inline bool tryGetEnd(TextRange r, TextPos* out) {
	*out = r.end();
	return true;
}
} // namespace greatestEndDetail

template<typename... Args>
inline TextPos greatestEnd(TextPos end, const Args&... args) {
	TextPos e = end;
	auto tryOne = [&e](const auto& v) {
		TextPos t;
		if (greatestEndDetail::tryGetEnd(v, &t) && e < t) {
			e = t;
		}
	};
	(tryOne(args), ...);
	return e;
}

// firstOrNil/lastOrNil — core.FirstOrNil/LastOrNil for node slices.
inline Node* firstOrNil(const std::vector<Node*>& nodes) {
	return nodes.empty() ? nullptr : nodes.front();
}
inline Node* lastOrNil(const std::vector<Node*>& nodes) {
	return nodes.empty() ? nullptr : nodes.back();
}

} // namespace tsc::printer
