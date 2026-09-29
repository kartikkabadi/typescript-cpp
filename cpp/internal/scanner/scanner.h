// Port of tsc/internal/scanner/scanner.go
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/jsnum/jsnum.h"
#include "internal/stringutil/stringutil.h"
#include "internal/stringutil/unicode.h"


namespace tsc {

namespace detail {
struct RegExpParser;
}

enum class IdentifierVariant : int32_t {
	Standard = 0,
	JSX = 1,
	RegExpGroupName = 2,
};

enum class EscapeSequenceScanningFlags : int32_t {
	String = 1 << 0,
	ReportErrors = 1 << 1,
	RegularExpression = 1 << 2,
	AnnexB = 1 << 3,
	AnyUnicodeMode = 1 << 4,
	AtomEscape = 1 << 5,
	ReportInvalidEscapeErrors = RegularExpression | ReportErrors,
	AllowExtendedUnicodeEscape = String | AnyUnicodeMode,
};

inline EscapeSequenceScanningFlags operator|(EscapeSequenceScanningFlags a,
                                             EscapeSequenceScanningFlags b) {
	return static_cast<EscapeSequenceScanningFlags>(
		static_cast<int32_t>(a) | static_cast<int32_t>(b));
}
inline EscapeSequenceScanningFlags operator&(EscapeSequenceScanningFlags a,
                                             EscapeSequenceScanningFlags b) {
	return static_cast<EscapeSequenceScanningFlags>(
		static_cast<int32_t>(a) & static_cast<int32_t>(b));
}
inline EscapeSequenceScanningFlags& operator|=(
	EscapeSequenceScanningFlags& a, EscapeSequenceScanningFlags b) {
	return a = a | b;
}

using ErrorCallback =
    std::function<void(const DiagnosticMessage*, int pos, int length,
                       const std::vector<std::string>& args)>;

extern const std::unordered_map<std::string_view, Kind> textToKeyword;
extern const std::unordered_map<std::string_view, Kind> textToToken;

struct ScannerState {
	int pos = 0;
	int fullStartPos = 0;
	int tokenStart = 0;
	Kind token = Kind::Unknown;
	std::string tokenValue;
	TokenFlags tokenFlags = TokenFlagsNone;
	std::vector<CommentDirective> commentDirectives;
	int skipJSDocLeadingAsterisks = 0;
};

class Scanner {
	friend struct detail::RegExpParser;

public:
	std::string_view text;
	int end = 0;
	LanguageVariant languageVariant = LanguageVariant::Standard;
	ScriptTarget scriptTarget = ScriptTarget::None;
	ErrorCallback onError;
	bool skipTrivia = true;

	ScannerState st;

	Scanner() = default;

	void reset() {
		st = ScannerState{};
	}

	std::string_view getText() const { return text; }
	Kind token() const { return st.token; }
	TokenFlags tokenFlags() const { return st.tokenFlags; }
	int tokenFullStart() const { return st.fullStartPos; }
	int tokenStart() const { return st.tokenStart; }
	int tokenEnd() const { return st.pos; }
	std::string_view tokenText() const {
		return text.substr(st.tokenStart, st.pos - st.tokenStart);
	}
	std::string_view tokenValue() const { return st.tokenValue; }
	TextRange tokenRange() const { return TextRange{st.tokenStart, st.pos}; }
	const std::vector<CommentDirective>& getCommentDirectives() const {
		return st.commentDirectives;
	}

	ScannerState mark() const { return st; }
	void rewind(const ScannerState& state) { st = state; }

	void resetPos(int pos) {
		st.pos = pos;
		st.fullStartPos = pos;
		st.tokenStart = pos;
	}
	void resetTokenState(int pos) {
		resetPos(pos);
		st.token = Kind::Unknown;
		st.tokenValue.clear();
		st.tokenFlags = TokenFlagsNone;
	}
	void setSkipJSDocLeadingAsterisks(bool skip) {
		st.skipJSDocLeadingAsterisks += skip ? 1 : -1;
	}
	void setSkipTrivia(bool skip) { skipTrivia = skip; }

	bool hasUnicodeEscape() const {
		return (st.tokenFlags & TokenFlagsUnicodeEscape) != 0;
	}
	bool hasExtendedUnicodeEscape() const {
		return (st.tokenFlags & TokenFlagsExtendedUnicodeEscape) != 0;
	}
	bool hasPrecedingLineBreak() const {
		return (st.tokenFlags & TokenFlagsPrecedingLineBreak) != 0;
	}
	bool hasPrecedingJSDocComment() const {
		return (st.tokenFlags & TokenFlagsPrecedingJSDocComment) != 0;
	}
	bool hasPrecedingJSDocLeadingAsterisks() const {
		return (st.tokenFlags & TokenFlagsPrecedingJSDocLeadingAsterisks) != 0;
	}
	bool hasPrecedingJSDocWithDeprecatedTag() const {
		return (st.tokenFlags & TokenFlagsPrecedingJSDocWithDeprecated) != 0;
	}
	bool hasPrecedingJSDocWithSeeOrLink() const {
		return (st.tokenFlags & TokenFlagsPrecedingJSDocWithSeeOrLink) != 0;
	}

	void setText(std::string_view t) {
		text = t;
		end = static_cast<int>(t.size());
		st = ScannerState{};
	}
	void setOnError(ErrorCallback cb) { onError = std::move(cb); }
	void setLanguageVariant(LanguageVariant v) { languageVariant = v; }
	void setScriptTarget(ScriptTarget t) { scriptTarget = t; }

	ScriptTarget languageVersion() const {
		return scriptTarget == ScriptTarget::None ? ScriptTarget::ESNext
		                                          : scriptTarget;
	}

	void error(const DiagnosticMessage* diagnostic) {
		errorAt(diagnostic, st.pos, 0, {});
	}
	void errorAt(const DiagnosticMessage* diagnostic, int pos, int length,
	             const std::vector<std::string>& args = {}) {
		if (onError)
			onError(diagnostic, pos, length, args);
	}

	Kind scan();
	Kind reScanLessThanToken();
	Kind reScanGreaterThanToken();
	Kind reScanTemplateToken(bool isTaggedTemplate);
	Kind reScanAsteriskEqualsToken();
	Kind reScanSlashToken(bool reportErrors = false);
	Kind reScanJsxToken(bool allowMultilineJsxText);
	Kind reScanHashToken();
	Kind reScanQuestionToken();
	Kind scanJsxToken() { return scanJsxTokenEx(true); }
	Kind scanJsxTokenEx(bool allowMultilineJsxText);
	Kind scanJsxIdentifier();
	Kind scanJsxAttributeValue();
	Kind reScanJsxAttributeValue();
	Kind scanJSDocCommentTextToken(bool inBackticks);
	Kind scanJSDocToken();
	bool canFollowJSDocAt();

private:
	std::unordered_map<std::string, std::string> numberCache;
	std::unordered_map<std::string, std::string> hexNumberCache;
	std::unordered_map<std::string, std::string> hexDigitCache;

	char32_t charAt(int offset) const {
		if (st.pos + offset < end)
			return static_cast<unsigned char>(text[st.pos + offset]);
		return -1;
	}
	char32_t char_() const {
		if (st.pos < end)
			return static_cast<unsigned char>(text[st.pos]);
		return -1;
	}
	std::pair<char32_t, int> charAndSize() const {
		if (st.pos < end) {
			auto b = static_cast<unsigned char>(text[st.pos]);
			if (b < 0x80)
				return {b, 1};
		}
		int width = 0;
		char32_t r = decodeUtf8Rune(text.substr(st.pos), &width);
		return {r, width};
	}
	template <class Pred>
	void scanASCIIWhile(Pred pred) {
		size_t i = static_cast<size_t>(st.pos);
		const size_t e = static_cast<size_t>(end);
		while (i < e) {
			auto b = static_cast<unsigned char>(text[i]);
			if (b >= 0x80 || !pred(static_cast<uint8_t>(b)))
				break;
			i++;
		}
		st.pos = static_cast<int>(i);
	}

	void scanJSDocCommentForTags(std::string_view commentText);
	void processCommentDirective(int start, int end, bool multiline);
	void reScanGreaterThanTokenInner();
	bool scanIdentifier(int prefixLength, IdentifierVariant variant);
	std::string scanIdentifierParts(IdentifierVariant variant);
	std::pair<char32_t, bool> scanIdentifierEscape(
		const std::function<bool(char32_t)>& isValid,
		bool allowSurrogatePairEscape);
	std::string scanString(bool jsxAttributeString);
	Kind scanTemplateAndSetTokenValue(bool shouldEmitInvalidEscapeError);
	std::string scanEscapeSequence(EscapeSequenceScanningFlags flags);
	char32_t scanUnicodeEscape(bool shouldEmitInvalidEscapeError);
	std::pair<char32_t, bool> scanLowSurrogateEscape(char32_t high);
	char32_t peekUnicodeEscape();
	Kind scanNumber();
	std::string scanNumberFragment();
	std::pair<std::string_view, bool> scanDigits();
	std::string scanHexDigits(int minCount, bool scanAsManyAsPossible,
	                          bool canHaveSeparators);
	std::string scanBinaryOrOctalDigits(int32_t base);
	Kind scanBigIntSuffix();
	void scanInvalidCharacter();
};

Kind getIdentifierToken(std::string_view str);
bool isValidIdentifier(std::string_view s);
bool isWordCharacter(char32_t ch);
bool isIdentifierStart(char32_t ch);
bool isIdentifierPart(char32_t ch);
bool isIdentifierPartEx(char32_t ch, LanguageVariant languageVariant);
std::string_view tokenToString(Kind token);
Kind stringToToken(std::string_view s);
const std::vector<std::string_view>& getViableKeywordSuggestions();

struct SkipTriviaOptions {
	bool stopAfterLineBreak = false;
	bool stopAtComments = false;
	bool inJSDoc = false;
};

int skipTrivia(std::string_view text, int pos);
int skipTriviaEx(std::string_view text, int pos, const SkipTriviaOptions& options);
bool couldStartTrivia(std::string_view text, int pos);
bool isConflictMarkerTrivia(std::string_view text, int pos);
int scanConflictMarkerTrivia(
	std::string_view text, int pos,
	const std::function<void(const DiagnosticMessage*, int, int)>& reportError);
bool isShebangTrivia(std::string_view text, int pos);
int scanShebangTrivia(std::string_view text, int pos);
std::string_view getShebang(std::string_view text);

Scanner& getScannerForSourceFile(Scanner& s, SourceFile* sourceFile, int pos);
Kind scanTokenAtPosition(SourceFile* sourceFile, int pos);
TextRange getRangeOfTokenAtPosition(SourceFile* sourceFile, int pos);
int getTokenPosOfNode(Node* node, SourceFile* sourceFile, bool includeJSDoc);
TextRange getErrorRangeForNode(SourceFile* sourceFile, Node* node);

int computeLineOfPosition(const std::vector<TextPos>& lineStarts, int pos);
const ECMALineStarts& getECMALineStarts(SourceFile* sourceFile);
int getECMALineOfPosition(SourceFile* sourceFile, int pos);
std::pair<int, int> getECMALineAndUTF16CharacterOfPosition(SourceFile* sourceFile,
                                                         int pos);
std::pair<int, int> getECMALineAndByteOffsetOfPosition(SourceFile* sourceFile,
                                                     int pos);
int getECMAEndLinePosition(SourceFile* sourceFile, int line);
int getECMAPositionOfLineAndUTF16Character(SourceFile* sourceFile, int line,
                                         int character);
int getECMAPositionOfLineAndByteOffset(SourceFile* sourceFile, int line,
                                       int byteOffset);
int computePositionOfLineAndByteOffset(const std::vector<TextPos>& lineStarts,
                                       int line, int byteOffset);
int computePositionOfLineAndUTF16Character(
	const std::vector<TextPos>& lineStarts, int line, int character,
	std::string_view text, bool allowEdits);

// utilities.go
bool tokenIsIdentifierOrKeyword(Kind token);
Kind identifierToKeywordKind(const Identifier* node);
std::string getTextOfNodeFromSourceText(std::string_view sourceText,
                                        const Node* node, bool includeTrivia);
std::string getSourceTextOfNodeFromSourceFile(SourceFile* sourceFile,
                                            const Node* node,
                                            bool includeTrivia);
std::string getTextOfNode(const Node* node);
std::string getTextOfJSDocComment(const NodeList* comment);
std::string declarationNameToString(const Node* name);
bool isIdentifierText(std::string_view name, LanguageVariant languageVariant);
bool isIntrinsicJsxName(std::string_view name);

void iterateCommentRanges(
	std::string_view text, int pos, bool trailing,
	const std::function<bool(const CommentRange&)>& yield);
void getLeadingCommentRanges(
	std::string_view text, int pos,
	const std::function<bool(const CommentRange&)>& yield);
void getTrailingCommentRanges(
	std::string_view text, int pos,
	const std::function<bool(const CommentRange&)>& yield);

}  // namespace tsc
