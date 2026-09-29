// Port of tsc/internal/scanner/scanner.go — UTF-8 byte-offset scanner.
#include "internal/scanner/scanner.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "internal/scanner/regexp.h"

namespace tsc {

// ---------------------------------------------------------------------------
// keyword / token tables
// ---------------------------------------------------------------------------

static const std::pair<const char*, Kind> kKeywordList[] = {
	{"abstract", Kind::AbstractKeyword},
	{"accessor", Kind::AccessorKeyword},
	{"any", Kind::AnyKeyword},
	{"as", Kind::AsKeyword},
	{"asserts", Kind::AssertsKeyword},
	{"assert", Kind::AssertKeyword},
	{"bigint", Kind::BigIntKeyword},
	{"boolean", Kind::BooleanKeyword},
	{"break", Kind::BreakKeyword},
	{"case", Kind::CaseKeyword},
	{"catch", Kind::CatchKeyword},
	{"class", Kind::ClassKeyword},
	{"continue", Kind::ContinueKeyword},
	{"const", Kind::ConstKeyword},
	{"constructor", Kind::ConstructorKeyword},
	{"debugger", Kind::DebuggerKeyword},
	{"declare", Kind::DeclareKeyword},
	{"default", Kind::DefaultKeyword},
	{"defer", Kind::DeferKeyword},
	{"delete", Kind::DeleteKeyword},
	{"do", Kind::DoKeyword},
	{"else", Kind::ElseKeyword},
	{"enum", Kind::EnumKeyword},
	{"export", Kind::ExportKeyword},
	{"extends", Kind::ExtendsKeyword},
	{"false", Kind::FalseKeyword},
	{"finally", Kind::FinallyKeyword},
	{"for", Kind::ForKeyword},
	{"from", Kind::FromKeyword},
	{"function", Kind::FunctionKeyword},
	{"get", Kind::GetKeyword},
	{"if", Kind::IfKeyword},
	{"immediate", Kind::ImmediateKeyword},
	{"implements", Kind::ImplementsKeyword},
	{"import", Kind::ImportKeyword},
	{"in", Kind::InKeyword},
	{"infer", Kind::InferKeyword},
	{"instanceof", Kind::InstanceOfKeyword},
	{"interface", Kind::InterfaceKeyword},
	{"intrinsic", Kind::IntrinsicKeyword},
	{"is", Kind::IsKeyword},
	{"keyof", Kind::KeyOfKeyword},
	{"let", Kind::LetKeyword},
	{"module", Kind::ModuleKeyword},
	{"namespace", Kind::NamespaceKeyword},
	{"never", Kind::NeverKeyword},
	{"new", Kind::NewKeyword},
	{"null", Kind::NullKeyword},
	{"number", Kind::NumberKeyword},
	{"object", Kind::ObjectKeyword},
	{"package", Kind::PackageKeyword},
	{"private", Kind::PrivateKeyword},
	{"protected", Kind::ProtectedKeyword},
	{"public", Kind::PublicKeyword},
	{"override", Kind::OverrideKeyword},
	{"out", Kind::OutKeyword},
	{"readonly", Kind::ReadonlyKeyword},
	{"require", Kind::RequireKeyword},
	{"global", Kind::GlobalKeyword},
	{"return", Kind::ReturnKeyword},
	{"satisfies", Kind::SatisfiesKeyword},
	{"set", Kind::SetKeyword},
	{"static", Kind::StaticKeyword},
	{"string", Kind::StringKeyword},
	{"super", Kind::SuperKeyword},
	{"switch", Kind::SwitchKeyword},
	{"symbol", Kind::SymbolKeyword},
	{"this", Kind::ThisKeyword},
	{"throw", Kind::ThrowKeyword},
	{"true", Kind::TrueKeyword},
	{"try", Kind::TryKeyword},
	{"type", Kind::TypeKeyword},
	{"typeof", Kind::TypeOfKeyword},
	{"undefined", Kind::UndefinedKeyword},
	{"unique", Kind::UniqueKeyword},
	{"unknown", Kind::UnknownKeyword},
	{"using", Kind::UsingKeyword},
	{"var", Kind::VarKeyword},
	{"void", Kind::VoidKeyword},
	{"while", Kind::WhileKeyword},
	{"with", Kind::WithKeyword},
	{"yield", Kind::YieldKeyword},
	{"async", Kind::AsyncKeyword},
	{"await", Kind::AwaitKeyword},
	{"of", Kind::OfKeyword},
};

static const std::pair<const char*, Kind> kTokenList[] = {
	{"{", Kind::OpenBraceToken},
	{"}", Kind::CloseBraceToken},
	{"(", Kind::OpenParenToken},
	{")", Kind::CloseParenToken},
	{"[", Kind::OpenBracketToken},
	{"]", Kind::CloseBracketToken},
	{".", Kind::DotToken},
	{"...", Kind::DotDotDotToken},
	{";", Kind::SemicolonToken},
	{",", Kind::CommaToken},
	{"<", Kind::LessThanToken},
	{">", Kind::GreaterThanToken},
	{"<=", Kind::LessThanEqualsToken},
	{">=", Kind::GreaterThanEqualsToken},
	{"==", Kind::EqualsEqualsToken},
	{"!=", Kind::ExclamationEqualsToken},
	{"===", Kind::EqualsEqualsEqualsToken},
	{"!==", Kind::ExclamationEqualsEqualsToken},
	{"=>", Kind::EqualsGreaterThanToken},
	{"+", Kind::PlusToken},
	{"-", Kind::MinusToken},
	{"**", Kind::AsteriskAsteriskToken},
	{"*", Kind::AsteriskToken},
	{"/", Kind::SlashToken},
	{"%", Kind::PercentToken},
	{"++", Kind::PlusPlusToken},
	{"--", Kind::MinusMinusToken},
	{"<<", Kind::LessThanLessThanToken},
	{"</", Kind::LessThanSlashToken},
	{">>", Kind::GreaterThanGreaterThanToken},
	{">>>", Kind::GreaterThanGreaterThanGreaterThanToken},
	{"&", Kind::AmpersandToken},
	{"|", Kind::BarToken},
	{"^", Kind::CaretToken},
	{"!", Kind::ExclamationToken},
	{"~", Kind::TildeToken},
	{"&&", Kind::AmpersandAmpersandToken},
	{"||", Kind::BarBarToken},
	{"?", Kind::QuestionToken},
	{"??", Kind::QuestionQuestionToken},
	{"?.", Kind::QuestionDotToken},
	{":", Kind::ColonToken},
	{"=", Kind::EqualsToken},
	{"+=", Kind::PlusEqualsToken},
	{"-=", Kind::MinusEqualsToken},
	{"*=", Kind::AsteriskEqualsToken},
	{"**=", Kind::AsteriskAsteriskEqualsToken},
	{"/=", Kind::SlashEqualsToken},
	{"%=", Kind::PercentEqualsToken},
	{"<<=", Kind::LessThanLessThanEqualsToken},
	{">>=", Kind::GreaterThanGreaterThanEqualsToken},
	{">>>=", Kind::GreaterThanGreaterThanGreaterThanEqualsToken},
	{"&=", Kind::AmpersandEqualsToken},
	{"|=", Kind::BarEqualsToken},
	{"^=", Kind::CaretEqualsToken},
	{"||=", Kind::BarBarEqualsToken},
	{"&&=", Kind::AmpersandAmpersandEqualsToken},
	{"??=", Kind::QuestionQuestionEqualsToken},
	{"@", Kind::AtToken},
	{"#", Kind::HashToken},
	{"`", Kind::BacktickToken},
};

const std::unordered_map<std::string_view, Kind> textToKeyword = [] {
	std::unordered_map<std::string_view, Kind> m;
	for (auto& [k, v] : kKeywordList)
		m.emplace(k, v);
	return m;
}();

const std::unordered_map<std::string_view, Kind> textToToken = [] {
	std::unordered_map<std::string_view, Kind> m;
	for (auto& [k, v] : kTokenList)
		m.emplace(k, v);
	for (auto& [k, v] : kKeywordList)
		m.emplace(k, v);
	return m;
}();

static const std::string_view* tokenToTextTable() {
	static const auto* table = [] {
		auto* t = new std::string_view[static_cast<size_t>(Kind::Count)]();
		for (auto& [k, v] : kTokenList)
			t[static_cast<size_t>(v)] = k;
		for (auto& [k, v] : kKeywordList)
			t[static_cast<size_t>(v)] = k;
		return t;
	}();
	return table;
}

// ---------------------------------------------------------------------------
// JSDoc tag pre-scan
// ---------------------------------------------------------------------------

static bool hasJSDocTag(std::string_view text,
                        std::initializer_list<std::string_view> tags) {
	for (auto tag : tags) {
		if (!text.starts_with(tag))
			continue;
		if (text.size() == tag.size())
			return true;
		char ch = text[tag.size()];
		if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
		    ch == '}' || ch == '*')
			return true;
	}
	return false;
}

void Scanner::scanJSDocCommentForTags(std::string_view commentText) {
	for (;;) {
		auto i = commentText.find('@');
		if (i == std::string_view::npos)
			return;
		commentText = commentText.substr(i + 1);
		if ((st.tokenFlags & TokenFlagsPrecedingJSDocWithDeprecated) == 0 &&
		    hasJSDocTag(commentText, {"deprecated"}))
			st.tokenFlags |= TokenFlagsPrecedingJSDocWithDeprecated;
		if ((st.tokenFlags & TokenFlagsPrecedingJSDocWithSeeOrLink) == 0 &&
		    hasJSDocTag(commentText, {"see", "link", "linkcode", "linkplain"}))
			st.tokenFlags |= TokenFlagsPrecedingJSDocWithSeeOrLink;
		if ((st.tokenFlags & (TokenFlagsPrecedingJSDocWithDeprecated |
		                    TokenFlagsPrecedingJSDocWithSeeOrLink)) ==
		    (TokenFlagsPrecedingJSDocWithDeprecated |
		     TokenFlagsPrecedingJSDocWithSeeOrLink))
			return;
	}
}

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------

Kind Scanner::scan() {
	st.fullStartPos = st.pos;
	st.tokenFlags = TokenFlagsNone;
	for (;;) {
		char32_t ch = char_();
		st.tokenStart = st.pos;

		switch (ch) {
		case '\t':
		case '\v':
		case '\f':
		case ' ':
			st.pos++;
			if (skipTrivia)
				continue;
			for (;;) {
				auto [ch2, size] = charAndSize();
				if (!isWhiteSpaceSingleLine(ch2))
					break;
				st.pos += size;
			}
			st.token = Kind::WhitespaceTrivia;
			break;
		case '\n':
		case '\r':
			st.tokenFlags |= TokenFlagsPrecedingLineBreak;
			if (skipTrivia) {
				st.pos++;
				scanASCIIWhile([](uint8_t b) {
					return b == ' ' || (b >= '\t' && b <= '\r');
				});
				continue;
			}
			if (ch == '\r' && charAt(1) == '\n')
				st.pos += 2;
			else
				st.pos++;
			st.token = Kind::NewLineTrivia;
			break;
		case '!':
			if (charAt(1) == '=') {
				if (charAt(2) == '=') {
					st.pos += 3;
					st.token = Kind::ExclamationEqualsEqualsToken;
				} else {
					st.pos += 2;
					st.token = Kind::ExclamationEqualsToken;
				}
			} else {
				st.pos++;
				st.token = Kind::ExclamationToken;
			}
			break;
		case '"':
		case '\'': {
			st.tokenValue = scanString(false);
			st.token = Kind::StringLiteral;
			break;
		}
		case '`':
			st.token = scanTemplateAndSetTokenValue(false);
			break;
		case '%':
			if (charAt(1) == '=') {
				st.pos += 2;
				st.token = Kind::PercentEqualsToken;
			} else {
				st.pos++;
				st.token = Kind::PercentToken;
			}
			break;
		case '&': {
			auto next = charAt(1);
			if (next == '&') {
				if (charAt(2) == '=') {
					st.pos += 3;
					st.token = Kind::AmpersandAmpersandEqualsToken;
				} else {
					st.pos += 2;
					st.token = Kind::AmpersandAmpersandToken;
				}
			} else if (next == '=') {
				st.pos += 2;
				st.token = Kind::AmpersandEqualsToken;
			} else {
				st.pos++;
				st.token = Kind::AmpersandToken;
			}
			break;
		}
		case '(':
			st.pos++;
			st.token = Kind::OpenParenToken;
			break;
		case ')':
			st.pos++;
			st.token = Kind::CloseParenToken;
			break;
		case '*': {
			auto next = charAt(1);
			if (next == '=') {
				st.pos += 2;
				st.token = Kind::AsteriskEqualsToken;
			} else if (next == '*') {
				if (charAt(2) == '=') {
					st.pos += 3;
					st.token = Kind::AsteriskAsteriskEqualsToken;
				} else {
					st.pos += 2;
					st.token = Kind::AsteriskAsteriskToken;
				}
			} else {
				st.pos++;
				if (st.skipJSDocLeadingAsterisks != 0 &&
				    (st.tokenFlags & TokenFlagsPrecedingJSDocLeadingAsterisks) ==
				        0 &&
				    (st.tokenFlags & TokenFlagsPrecedingLineBreak) != 0) {
					st.tokenFlags |= TokenFlagsPrecedingJSDocLeadingAsterisks;
					continue;
				}
				st.token = Kind::AsteriskToken;
			}
			break;
		}
		case '+': {
			auto next = charAt(1);
			if (next == '=') {
				st.pos += 2;
				st.token = Kind::PlusEqualsToken;
			} else if (next == '+') {
				st.pos += 2;
				st.token = Kind::PlusPlusToken;
			} else {
				st.pos++;
				st.token = Kind::PlusToken;
			}
			break;
		}
		case ',':
			st.pos++;
			st.token = Kind::CommaToken;
			break;
		case '-': {
			auto next = charAt(1);
			if (next == '=') {
				st.pos += 2;
				st.token = Kind::MinusEqualsToken;
			} else if (next == '-') {
				st.pos += 2;
				st.token = Kind::MinusMinusToken;
			} else {
				st.pos++;
				st.token = Kind::MinusToken;
			}
			break;
		}
		case '.':
			if (isDigit(charAt(1))) {
				st.token = scanNumber();
			} else if (charAt(1) == '.' && charAt(2) == '.') {
				st.pos += 3;
				st.token = Kind::DotDotDotToken;
			} else {
				st.pos++;
				st.token = Kind::DotToken;
			}
			break;
		case '/':
			// Single-line comment
			if (charAt(1) == '/') {
				st.pos += 2;
				for (;;) {
					scanASCIIWhile([](uint8_t b) {
						return b != '\n' && b != '\r';
					});
					auto [ch1, size] = charAndSize();
					if (size == 0 || isLineBreak(ch1))
						break;
					st.pos += size;
				}
				processCommentDirective(st.tokenStart, st.pos, false);
				if (skipTrivia)
					continue;
				st.token = Kind::SingleLineCommentTrivia;
				return st.token;
			}
			// Multi-line comment
			if (charAt(1) == '*') {
				st.pos += 2;
				bool isJSDoc = char_() == '*' && charAt(1) != '/';
				bool commentClosed = false;
				int lastLineStart = st.tokenStart;
				for (;;) {
					scanASCIIWhile([](uint8_t b) {
						return b != '*' && b != '\n' && b != '\r';
					});
					auto [ch1, size] = charAndSize();
					if (size == 0)
						break;
					if (ch1 == '*' && charAt(1) == '/') {
						st.pos += 2;
						commentClosed = true;
						break;
					}
					st.pos += size;
					if (isLineBreak(ch1)) {
						lastLineStart = st.pos;
						st.tokenFlags |= TokenFlagsPrecedingLineBreak;
					}
				}
				if (isJSDoc) {
					st.tokenFlags |= TokenFlagsPrecedingJSDocComment;
					scanJSDocCommentForTags(
						text.substr(st.tokenStart, st.pos - st.tokenStart));
				}
				processCommentDirective(lastLineStart, st.pos, true);
				if (!commentClosed)
					error(Asterisk_Slash_expected);
				if (skipTrivia)
					continue;
				if (!commentClosed)
					st.tokenFlags |= TokenFlagsUnterminated;
				st.token = Kind::MultiLineCommentTrivia;
				return st.token;
			}
			if (charAt(1) == '=') {
				st.pos += 2;
				st.token = Kind::SlashEqualsToken;
			} else {
				st.pos++;
				st.token = Kind::SlashToken;
			}
			break;
		case '0':
			if (charAt(1) == 'X' || charAt(1) == 'x') {
				int start = st.pos;
				st.pos += 2;
				std::string digits = scanHexDigits(1, true, true);
				if (digits.empty()) {
					error(Hexadecimal_digit_expected);
					digits = "0";
				}
				if (auto it = hexNumberCache.find(digits);
				    it != hexNumberCache.end()) {
					st.tokenValue = it->second;
				} else {
					std::string_view rawText =
						text.substr(start, st.pos - start);
					if (rawText.starts_with("0x") &&
					    rawText.substr(2) == digits) {
						st.tokenValue = rawText;
					} else {
						st.tokenValue = "0x" + digits;
					}
					hexNumberCache[digits] = st.tokenValue;
				}
				st.tokenFlags |= TokenFlagsHexSpecifier;
				st.token = scanBigIntSuffix();
				break;
			}
			if (charAt(1) == 'B' || charAt(1) == 'b') {
				st.pos += 2;
				std::string digits = scanBinaryOrOctalDigits(2);
				if (digits.empty()) {
					error(Binary_digit_expected);
					digits = "0";
				}
				st.tokenValue = "0b" + digits;
				st.tokenFlags |= TokenFlagsBinarySpecifier;
				st.token = scanBigIntSuffix();
				break;
			}
			if (charAt(1) == 'O' || charAt(1) == 'o') {
				st.pos += 2;
				std::string digits = scanBinaryOrOctalDigits(8);
				if (digits.empty()) {
					error(Octal_digit_expected);
					digits = "0";
				}
				st.tokenValue = "0o" + digits;
				st.tokenFlags |= TokenFlagsOctalSpecifier;
				st.token = scanBigIntSuffix();
				break;
			}
			[[fallthrough]];
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			st.token = scanNumber();
			break;
		case ':':
			st.pos++;
			st.token = Kind::ColonToken;
			break;
		case ';':
			st.pos++;
			st.token = Kind::SemicolonToken;
			break;
		case '<':
			if (charAt(1) == '<' &&
			    isConflictMarkerTrivia(text, st.pos)) {
				st.pos = scanConflictMarkerTrivia(
					text, st.pos,
					[this](const DiagnosticMessage* d, int p, int l) {
						errorAt(d, p, l);
					});
				if (skipTrivia) {
					continue;
				}
				st.token = Kind::ConflictMarkerTrivia;
				return st.token;
			}
			if (charAt(1) == '<') {
				if (charAt(2) == '=') {
					st.pos += 3;
					st.token = Kind::LessThanLessThanEqualsToken;
				} else {
					st.pos += 2;
					st.token = Kind::LessThanLessThanToken;
				}
			} else if (charAt(1) == '=') {
				st.pos += 2;
				st.token = Kind::LessThanEqualsToken;
			} else if (languageVariant == LanguageVariant::JSX &&
			           charAt(1) == '/' && charAt(2) != '*') {
				st.pos += 2;
				st.token = Kind::LessThanSlashToken;
			} else {
				st.pos++;
				st.token = Kind::LessThanToken;
			}
			break;
		case '=':
			if (charAt(1) == '=' &&
			    isConflictMarkerTrivia(text, st.pos)) {
				st.pos = scanConflictMarkerTrivia(
					text, st.pos,
					[this](const DiagnosticMessage* d, int p, int l) {
						errorAt(d, p, l);
					});
				if (skipTrivia) {
					continue;
				}
				st.token = Kind::ConflictMarkerTrivia;
				return st.token;
			}
			if (charAt(1) == '=') {
				if (charAt(2) == '=') {
					st.pos += 3;
					st.token = Kind::EqualsEqualsEqualsToken;
				} else {
					st.pos += 2;
					st.token = Kind::EqualsEqualsToken;
				}
			} else if (charAt(1) == '>') {
				st.pos += 2;
				st.token = Kind::EqualsGreaterThanToken;
			} else {
				st.pos++;
				st.token = Kind::EqualsToken;
			}
			break;
		case '>':
			if (charAt(1) == '>' &&
			    isConflictMarkerTrivia(text, st.pos)) {
				st.pos = scanConflictMarkerTrivia(
					text, st.pos,
					[this](const DiagnosticMessage* d, int p, int l) {
						errorAt(d, p, l);
					});
				if (skipTrivia) {
					continue;
				}
				st.token = Kind::ConflictMarkerTrivia;
				return st.token;
			}
			st.pos++;
			st.token = Kind::GreaterThanToken;
			break;
		case '?':
			if (charAt(1) == '.' && !isDigit(charAt(2))) {
				st.pos += 2;
				st.token = Kind::QuestionDotToken;
			} else if (charAt(1) == '?') {
				if (charAt(2) == '=') {
					st.pos += 3;
					st.token = Kind::QuestionQuestionEqualsToken;
				} else {
					st.pos += 2;
					st.token = Kind::QuestionQuestionToken;
				}
			} else {
				st.pos++;
				st.token = Kind::QuestionToken;
			}
			break;
		case '[':
			st.pos++;
			st.token = Kind::OpenBracketToken;
			break;
		case ']':
			st.pos++;
			st.token = Kind::CloseBracketToken;
			break;
		case '^':
			if (charAt(1) == '=') {
				st.pos += 2;
				st.token = Kind::CaretEqualsToken;
			} else {
				st.pos++;
				st.token = Kind::CaretToken;
			}
			break;
		case '{':
			st.pos++;
			st.token = Kind::OpenBraceToken;
			break;
		case '|':
			if (charAt(1) == '|' &&
			    isConflictMarkerTrivia(text, st.pos)) {
				st.pos = scanConflictMarkerTrivia(
					text, st.pos,
					[this](const DiagnosticMessage* d, int p, int l) {
						errorAt(d, p, l);
					});
				if (skipTrivia) {
					continue;
				}
				st.token = Kind::ConflictMarkerTrivia;
				return st.token;
			}
			if (charAt(1) == '|') {
				if (charAt(2) == '=') {
					st.pos += 3;
					st.token = Kind::BarBarEqualsToken;
				} else {
					st.pos += 2;
					st.token = Kind::BarBarToken;
				}
			} else if (charAt(1) == '=') {
				st.pos += 2;
				st.token = Kind::BarEqualsToken;
			} else {
				st.pos++;
				st.token = Kind::BarToken;
			}
			break;
		case '}':
			st.pos++;
			st.token = Kind::CloseBraceToken;
			break;
		case '~':
			st.pos++;
			st.token = Kind::TildeToken;
			break;
		case '@':
			st.pos++;
			st.token = Kind::AtToken;
			break;
		case '\\':
			if (scanIdentifier(0, IdentifierVariant::Standard)) {
				st.token = getIdentifierToken(st.tokenValue);
			} else {
				scanInvalidCharacter();
			}
			break;
		case '#':
			if (charAt(1) == '!') {
				if (st.pos == 0) {
					st.pos += 2;
					for (auto [ch2, size] = charAndSize();
					     size > 0 && !isLineBreak(ch2);
					     std::tie(ch2, size) = charAndSize()) {
						st.pos += size;
					}
					continue;
				}
				errorAt(X_can_only_be_used_at_the_start_of_a_file, st.pos, 2);
				st.pos += 2;
				st.token = Kind::Unknown;
				break;
			}
			if (!scanIdentifier(1, IdentifierVariant::Standard)) {
				errorAt(Invalid_character, st.pos - 1, 1);
				st.tokenValue = "#";
			}
			st.token = Kind::PrivateIdentifier;
			break;
		default:
			if (ch == static_cast<char32_t>(-1)) {
				st.token = Kind::EndOfFile;
				break;
			}
			if (scanIdentifier(0, IdentifierVariant::Standard)) {
				st.token = getIdentifierToken(st.tokenValue);
				break;
			}
			{
				auto [ch2, size] = charAndSize();
				if (ch2 == kRuneError) {
					errorAt(File_appears_to_be_binary, 0, 0);
					st.pos = static_cast<int>(text.size());
					st.token = Kind::NonTextFileMarkerTrivia;
					break;
				}
				if (isWhiteSpaceSingleLine(ch2)) {
					st.pos += size;
					if (ch2 == 0x0085 || skipTrivia)
						continue;
					for (;;) {
						auto [ch3, sz3] = charAndSize();
						if (!isWhiteSpaceSingleLine(ch3))
							break;
						st.pos += sz3;
					}
					st.token = Kind::WhitespaceTrivia;
					return st.token;
				}
				if (isLineBreak(ch2)) {
					st.tokenFlags |= TokenFlagsPrecedingLineBreak;
					st.pos += size;
					continue;
				}
				scanInvalidCharacter();
			}
		}
		return st.token;
	}
}

void Scanner::processCommentDirective(int start, int endPos, bool multiline) {
	int pos = start;
	if (multiline) {
		while (pos < endPos && (text[pos] == ' ' || text[pos] == '\t'))
			pos++;
		while (pos < endPos && (text[pos] == '/' || text[pos] == '*'))
			pos++;
	} else {
		pos += 2;
		while (pos < endPos && text[pos] == '/')
			pos++;
	}
	while (pos < endPos && (text[pos] == ' ' || text[pos] == '\t'))
		pos++;
	if (!(pos < endPos && text[pos] == '@'))
		return;
	pos++;
	CommentDirectiveKind kind;
	if (text.substr(pos).starts_with("ts-expect-error")) {
		kind = CommentDirectiveKind::ExpectError;
	} else if (text.substr(pos).starts_with("ts-ignore")) {
		kind = CommentDirectiveKind::Ignore;
	} else {
		return;
	}
	CommentDirective d;
	d.Loc = TextRange{start, endPos};
	d.Kind = kind;
	st.commentDirectives.push_back(d);
}

Kind Scanner::reScanLessThanToken() {
	if (st.token == Kind::LessThanLessThanToken) {
		st.pos = st.tokenStart + 1;
		st.token = Kind::LessThanToken;
	}
	return st.token;
}

void Scanner::reScanGreaterThanTokenInner() {
	st.pos = st.tokenStart + 1;
	if (char_() == '>') {
		if (charAt(1) == '>') {
			if (charAt(2) == '=') {
				st.pos += 3;
				st.token = Kind::GreaterThanGreaterThanGreaterThanEqualsToken;
			} else {
				st.pos += 2;
				st.token = Kind::GreaterThanGreaterThanGreaterThanToken;
			}
		} else if (charAt(1) == '=') {
			st.pos += 2;
			st.token = Kind::GreaterThanGreaterThanEqualsToken;
		} else {
			st.pos++;
			st.token = Kind::GreaterThanGreaterThanToken;
		}
	} else if (char_() == '=') {
		st.pos++;
		st.token = Kind::GreaterThanEqualsToken;
	}
}

Kind Scanner::reScanGreaterThanToken() {
	if (st.token == Kind::GreaterThanToken)
		reScanGreaterThanTokenInner();
	return st.token;
}

Kind Scanner::reScanTemplateToken(bool isTaggedTemplate) {
	st.pos = st.tokenStart;
	st.token = scanTemplateAndSetTokenValue(!isTaggedTemplate);
	return st.token;
}

Kind Scanner::reScanAsteriskEqualsToken() {
	st.pos = st.tokenStart + 1;
	st.token = Kind::EqualsToken;
	return st.token;
}

Kind Scanner::reScanSlashToken(bool shouldReportErrors) {
	if (st.token == Kind::SlashToken || st.token == Kind::SlashEqualsToken) {
		int startOfRegExpBody = st.tokenStart + 1;
		int p = startOfRegExpBody;
		bool inEscape = false;
		bool namedCaptureGroups = false;
		bool inCharacterClass = false;
		for (;;) {
			if (p >= end) {
				st.tokenFlags |= TokenFlagsUnterminated;
				break;
			}
			char32_t ch = static_cast<unsigned char>(text[p]);
			if (isLineBreak(ch)) {
				st.tokenFlags |= TokenFlagsUnterminated;
				break;
			} else if (inEscape) {
				inEscape = false;
			} else if (ch == '/' && !inCharacterClass) {
				break;
			} else if (ch == '[') {
				inCharacterClass = true;
			} else if (ch == '\\') {
				inEscape = true;
			} else if (ch == ']') {
				inCharacterClass = false;
			} else if (!inCharacterClass && ch == '(' &&
			           p + 1 < end && text[p + 1] == '?' && p + 2 < end &&
			           text[p + 2] == '<' &&
			           (p + 3 >= end || (text[p + 3] != '=' &&
			                             text[p + 3] != '!'))) {
				namedCaptureGroups = true;
			}
			p++;
		}

		int endOfRegExpBody = p;
		if ((st.tokenFlags & TokenFlagsUnterminated) != 0) {
			p = startOfRegExpBody;
			inEscape = false;
			int characterClassDepth = 0;
			bool inDecimalQuantifier = false;
			int groupDepth = 0;
			while (p < endOfRegExpBody) {
				char32_t ch = static_cast<unsigned char>(text[p]);
				if (inEscape) {
					inEscape = false;
				} else if (ch == '\\') {
					inEscape = true;
				} else if (ch == '[') {
					characterClassDepth++;
				} else if (ch == ']' && characterClassDepth != 0) {
					characterClassDepth--;
				} else if (characterClassDepth == 0) {
					if (ch == '{') {
						inDecimalQuantifier = true;
					} else if (ch == '}' && inDecimalQuantifier) {
						inDecimalQuantifier = false;
					} else if (!inDecimalQuantifier) {
						if (ch == '(') {
							groupDepth++;
						} else if (ch == ')' && groupDepth != 0) {
							groupDepth--;
						} else if (ch == ')' || ch == ']' || ch == '}') {
							break;
						}
					}
				}
				p++;
			}
			while (p > startOfRegExpBody) {
				int size;
				char32_t ch = decodeLastUtf8Rune(text.substr(0, p), &size);
				if (isWhiteSpaceLike(ch) || ch == ';')
					p -= size;
				else
					break;
			}
			errorAt(Unterminated_regular_expression_literal, st.tokenStart,
			        p - st.tokenStart);
		} else {
			p++;
			uint32_t regExpFlags = 0;
			while (p < end) {
				int size;
				char32_t ch = decodeUtf8Rune(text.substr(p), &size);
				if (ch == kRuneError || !isIdentifierPart(ch))
					break;
				if (shouldReportErrors) {
					uint32_t flag = charCodeToRegExpFlag(ch);
					if (flag == 0) {
						errorAt(Unknown_regular_expression_flag, p, size);
					} else if ((regExpFlags & flag) != 0) {
						errorAt(Duplicate_regular_expression_flag, p, size);
					} else if (((regExpFlags | flag) &
					            RegularExpressionFlagsAnyUnicodeMode) ==
					           RegularExpressionFlagsAnyUnicodeMode) {
						errorAt(
							The_Unicode_u_flag_and_the_Unicode_Sets_v_flag_cannot_be_set_simultaneously,
							p, size);
					} else {
						regExpFlags |= flag;
						checkRegularExpressionFlagAvailability(
							*this, flag, p, size);
					}
				}
				p += size;
			}
			if (shouldReportErrors) {
				st.pos = startOfRegExpBody;
				int saveEnd = end;
				int saveTokenPos = st.tokenStart;
				TokenFlags saveTokenFlags = st.tokenFlags;
				runRegExpValidator(*this, startOfRegExpBody, endOfRegExpBody,
				                   regExpFlags, namedCaptureGroups);
				end = saveEnd;
				st.pos = p;
				st.tokenStart = saveTokenPos;
				st.tokenFlags = saveTokenFlags;
			} else {
				st.pos = p;
			}
		}
		st.pos = p;
		st.tokenValue = text.substr(st.tokenStart, st.pos - st.tokenStart);
		st.token = Kind::RegularExpressionLiteral;
	}
	return st.token;
}

Kind Scanner::reScanJsxToken(bool allowMultilineJsxText) {
	st.pos = st.fullStartPos;
	st.tokenStart = st.fullStartPos;
	st.token = scanJsxTokenEx(allowMultilineJsxText);
	return st.token;
}

Kind Scanner::reScanHashToken() {
	if (st.token == Kind::PrivateIdentifier) {
		st.pos = st.tokenStart + 1;
		st.token = Kind::HashToken;
	}
	return st.token;
}

Kind Scanner::reScanQuestionToken() {
	st.pos = st.tokenStart + 1;
	st.token = Kind::QuestionToken;
	return st.token;
}

Kind Scanner::scanJsxTokenEx(bool allowMultilineJsxText) {
	st.fullStartPos = st.pos;
	st.tokenStart = st.pos;
	char32_t ch = char_();
	if (ch == static_cast<char32_t>(-1)) {
		st.token = Kind::EndOfFile;
	} else if (ch == '<') {
		if (charAt(1) == '/') {
			st.pos += 2;
			st.token = Kind::LessThanSlashToken;
		} else {
			st.pos++;
			st.token = Kind::LessThanToken;
		}
	} else if (ch == '{') {
		st.pos++;
		st.token = Kind::OpenBraceToken;
	} else {
		int firstNonWhitespace = 0;
		for (;;) {
			auto [ch2, size] = charAndSize();
			if (size == 0 || ch2 == '{')
				break;
			if (ch2 == '<') {
				if (isConflictMarkerTrivia(text, st.pos)) {
					st.pos = scanConflictMarkerTrivia(
						text, st.pos,
						[this](const DiagnosticMessage* d, int p, int l) {
							errorAt(d, p, l);
						});
					st.token = Kind::ConflictMarkerTrivia;
					return st.token;
				}
				break;
			}
			if (ch2 == '>') {
				errorAt(Unexpected_token_Did_you_mean_or_gt, st.pos, 1);
			} else if (ch2 == '}') {
				errorAt(Unexpected_token_Did_you_mean_or_rbrace, st.pos, 1);
			}
			if (isLineBreak(ch2) && firstNonWhitespace == 0) {
				firstNonWhitespace = -1;
			} else if (!allowMultilineJsxText && isLineBreak(ch2) &&
			           firstNonWhitespace > 0) {
				break;
			} else if (!isWhiteSpaceLike(ch2)) {
				firstNonWhitespace = st.pos;
			}
			st.pos += size;
		}
		st.tokenValue =
			text.substr(st.fullStartPos, st.pos - st.fullStartPos);
		st.token = Kind::JsxText;
		if (firstNonWhitespace == -1)
			st.token = Kind::JsxTextAllWhiteSpaces;
	}
	return st.token;
}

Kind Scanner::scanJsxIdentifier() {
	if (tokenIsIdentifierOrKeyword(st.token)) {
		st.tokenValue += scanIdentifierParts(IdentifierVariant::JSX);
		st.token = getIdentifierToken(st.tokenValue);
	}
	return st.token;
}

Kind Scanner::scanJsxAttributeValue() {
	st.fullStartPos = st.pos;
	for (auto [ch, size] = charAndSize(); size > 0 && isWhiteSpaceLike(ch);
	     std::tie(ch, size) = charAndSize()) {
		st.pos += size;
	}
	st.tokenStart = st.pos;
	switch (char_()) {
	case '"':
	case '\'':
		st.tokenValue = scanString(true);
		st.token = Kind::StringLiteral;
		return st.token;
	default:
		return scan();
	}
}

Kind Scanner::reScanJsxAttributeValue() {
	st.pos = st.fullStartPos;
	st.tokenStart = st.fullStartPos;
	return scanJsxAttributeValue();
}

Kind Scanner::scanJSDocCommentTextToken(bool inBackticks) {
	st.fullStartPos = st.pos;
	st.tokenFlags = TokenFlagsNone;
	if (st.pos >= static_cast<int>(text.size())) {
		st.token = Kind::EndOfFile;
		return st.token;
	}
	st.tokenStart = st.pos;
	for (auto [ch, size] = charAndSize();
	     st.pos < static_cast<int>(text.size()) && !isLineBreak(ch) &&
	     ch != '`';
	     std::tie(ch, size) = charAndSize()) {
		if (!inBackticks) {
			if (ch == '{') {
				break;
			} else if (ch == '@' && st.pos >= 0) {
				int pw;
				char32_t previous =
					decodeLastUtf8Rune(text.substr(0, st.pos), &pw);
				if (isWhiteSpaceSingleLine(previous)) {
					int nw;
					char32_t next =
						decodeUtf8Rune(text.substr(st.pos + size), &nw);
					if (isIdentifierStart(next))
						break;
				}
			}
		}
		st.pos += size;
	}
	if (st.pos == st.tokenStart)
		return scanJSDocToken();
	st.tokenValue = text.substr(st.tokenStart, st.pos - st.tokenStart);
	st.token = Kind::JSDocCommentTextToken;
	return st.token;
}

bool Scanner::canFollowJSDocAt() {
	if (st.pos >= static_cast<int>(text.size()))
		return true;
	int size;
	char32_t ch = decodeUtf8Rune(text.substr(st.pos), &size);
	return isIdentifierStart(ch) || isWhiteSpaceSingleLine(ch) ||
	       isLineBreak(ch);
}

Kind Scanner::scanJSDocToken() {
	st.fullStartPos = st.pos;
	st.tokenFlags = TokenFlagsNone;
	if (st.pos >= static_cast<int>(text.size())) {
		st.token = Kind::EndOfFile;
		return st.token;
	}
	st.tokenStart = st.pos;
	auto [ch, size] = charAndSize();
	st.pos += size;
	switch (ch) {
	case '\t':
	case '\v':
	case '\f':
	case ' ':
		for (auto [ch2, size2] = charAndSize();
		     size2 > 0 && isWhiteSpaceSingleLine(ch2);
		     std::tie(ch2, size2) = charAndSize()) {
			st.pos += size2;
		}
		st.token = Kind::WhitespaceTrivia;
		return st.token;
	case '@':
		st.token = Kind::AtToken;
		return st.token;
	case '\r':
		if (char_() == '\n')
			st.pos++;
		[[fallthrough]];
	case '\n':
		st.tokenFlags |= TokenFlagsPrecedingLineBreak;
		st.token = Kind::NewLineTrivia;
		return st.token;
	case '*':
		st.token = Kind::AsteriskToken;
		return st.token;
	case '{':
		st.token = Kind::OpenBraceToken;
		return st.token;
	case '}':
		st.token = Kind::CloseBraceToken;
		return st.token;
	case '[':
		st.token = Kind::OpenBracketToken;
		return st.token;
	case ']':
		st.token = Kind::CloseBracketToken;
		return st.token;
	case '(':
		st.token = Kind::OpenParenToken;
		return st.token;
	case ')':
		st.token = Kind::CloseParenToken;
		return st.token;
	case '<':
		st.token = Kind::LessThanToken;
		return st.token;
	case '>':
		st.token = Kind::GreaterThanToken;
		return st.token;
	case '=':
		st.token = Kind::EqualsToken;
		return st.token;
	case ',':
		st.token = Kind::CommaToken;
		return st.token;
	case '.':
		st.token = Kind::DotToken;
		return st.token;
	case '`':
		st.token = Kind::BacktickToken;
		return st.token;
	case '#':
		st.token = Kind::HashToken;
		return st.token;
	}

	st.pos = st.tokenStart;
	if (scanIdentifier(0, IdentifierVariant::JSX)) {
		st.token = getIdentifierToken(st.tokenValue);
		return st.token;
	}
	st.pos = st.tokenStart + size;
	st.token = Kind::Unknown;
	return st.token;
}

// ---------------------------------------------------------------------------
// identifiers
// ---------------------------------------------------------------------------

bool Scanner::scanIdentifier(int prefixLength, IdentifierVariant variant) {
	int start = st.pos;
	st.pos += prefixLength;
	int identifierStart = st.pos;
	char32_t ch = char_();
	if (variant != IdentifierVariant::JSX &&
	    (isASCIILetter(ch) || ch == '_' || ch == '$')) {
		st.pos++;
		scanASCIIWhile([](uint8_t b) {
			return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') ||
			       (b >= '0' && b <= '9') || b == '_' || b == '$';
		});
		ch = char_();
		if (ch < kRuneSelf && ch != '\\') {
			st.tokenValue = text.substr(start, st.pos - start);
			return true;
		}
		st.pos = identifierStart;
	}
	auto [ch2, size] = charAndSize();
	if (isIdentifierStart(ch2)) {
		LanguageVariant lv = variant == IdentifierVariant::JSX
		                         ? LanguageVariant::JSX
		                         : LanguageVariant::Standard;
		for (;;) {
			st.pos += size;
			auto [c3, s3] = charAndSize();
			ch2 = c3;
			size = s3;
			if (!isIdentifierPartEx(ch2, lv))
				break;
		}
		st.tokenValue = text.substr(start, st.pos - start);
		if (ch2 == '\\')
			st.tokenValue += scanIdentifierParts(variant);
		return true;
	}
	if (ch2 == '\\') {
		if (auto [escaped, ok] = scanIdentifierEscape(
		        isIdentifierStart, variant == IdentifierVariant::RegExpGroupName);
		    ok) {
			st.tokenValue =
				std::string(text.substr(start, identifierStart - start)) +
				utf8String(escaped) + scanIdentifierParts(variant);
			return true;
		}
	}
	return false;
}

std::string Scanner::scanIdentifierParts(IdentifierVariant variant) {
	std::string sb;
	int start = st.pos;
	LanguageVariant lv = variant == IdentifierVariant::JSX
	                         ? LanguageVariant::JSX
	                         : LanguageVariant::Standard;
	for (;;) {
		auto [ch, size] = charAndSize();
		if (isIdentifierPartEx(ch, lv)) {
			st.pos += size;
			continue;
		}
		if (ch == '\\') {
			int escapeStart = st.pos;
			auto isValid = [lv](char32_t c) {
				return isIdentifierPartEx(c, lv);
			};
			if (auto [escaped, ok] = scanIdentifierEscape(
			        isValid,
			        variant == IdentifierVariant::RegExpGroupName);
			    ok) {
				sb.append(text.substr(start, escapeStart - start));
				sb += utf8String(escaped);
				start = st.pos;
				continue;
			}
		}
		break;
	}
	sb.append(text.substr(start, st.pos - start));
	return sb;
}

std::pair<char32_t, bool> Scanner::scanIdentifierEscape(
	const std::function<bool(char32_t)>& isValid,
	bool allowSurrogatePairEscape) {
	char32_t escaped = peekUnicodeEscape();
	if (escaped >= 0 && isValid(escaped))
		return {scanUnicodeEscape(true), true};
	if (allowSurrogatePairEscape && charAt(2) != '{' &&
	    isHighSurrogate(escaped)) {
		int savedPos = st.pos;
		TokenFlags savedTokenFlags = st.tokenFlags;
		scanUnicodeEscape(false);
		if (charAt(2) != '{') {
			if (auto [codePoint, ok] = scanLowSurrogateEscape(escaped);
			    ok && isValid(codePoint))
				return {codePoint, true};
		}
		st.pos = savedPos;
		st.tokenFlags = savedTokenFlags;
	}
	return {0, false};
}

// ---------------------------------------------------------------------------
// strings and templates
// ---------------------------------------------------------------------------

std::string Scanner::scanString(bool jsxAttributeString) {
	char32_t quote = char_();
	if (quote == '\'')
		st.tokenFlags |= TokenFlagsSingleQuote;
	st.pos++;
	// Fast path: no escapes.
	auto strLen = text.substr(st.pos).find(static_cast<char>(quote));
	if (strLen == 0) {
		st.pos++;
		return "";
	}
	if (strLen != std::string_view::npos) {
		std::string_view str = text.substr(st.pos, strLen);
		if (jsxAttributeString ||
		    (str.find('\\') == std::string_view::npos &&
		     str.find('\r') == std::string_view::npos &&
		     str.find('\n') == std::string_view::npos)) {
			st.pos += static_cast<int>(strLen) + 1;
			return std::string(str);
		}
	}
	std::string sb;
	int start = st.pos;
	for (;;) {
		char32_t ch = char_();
		if (ch == static_cast<char32_t>(-1)) {
			sb.append(text.substr(start, st.pos - start));
			st.tokenFlags |= TokenFlagsUnterminated;
			error(Unterminated_string_literal);
			break;
		}
		if (ch == quote) {
			sb.append(text.substr(start, st.pos - start));
			st.pos++;
			break;
		}
		if (ch == '\\' && !jsxAttributeString) {
			sb.append(text.substr(start, st.pos - start));
			sb += scanEscapeSequence(EscapeSequenceScanningFlags::String |
			                         EscapeSequenceScanningFlags::ReportErrors);
			start = st.pos;
			continue;
		}
		if ((ch == '\n' || ch == '\r') && !jsxAttributeString) {
			sb.append(text.substr(start, st.pos - start));
			st.tokenFlags |= TokenFlagsUnterminated;
			error(Unterminated_string_literal);
			break;
		}
		st.pos++;
	}
	return sb;
}

Kind Scanner::scanTemplateAndSetTokenValue(bool shouldEmitInvalidEscapeError) {
	bool startedWithBacktick = char_() == '`';
	st.pos++;
	int start = st.pos;
	std::vector<std::string> parts;
	parts.reserve(4);
	Kind token;
	for (;;) {
		scanASCIIWhile([](uint8_t b) {
			return b != '`' && b != '$' && b != '\\' && b != '\r';
		});
		char32_t ch = char_();
		if (ch == static_cast<char32_t>(-1) || ch == '`') {
			parts.emplace_back(text.substr(start, st.pos - start));
			if (ch == '`') {
				st.pos++;
			} else {
				st.tokenFlags |= TokenFlagsUnterminated;
				error(Unterminated_template_literal);
			}
			token = startedWithBacktick ? Kind::NoSubstitutionTemplateLiteral
			                            : Kind::TemplateTail;
			break;
		}
		if (ch == '$' && charAt(1) == '{') {
			parts.emplace_back(text.substr(start, st.pos - start));
			st.pos += 2;
			token = startedWithBacktick ? Kind::TemplateHead
			                            : Kind::TemplateMiddle;
			break;
		}
		if (ch == '\\') {
			parts.emplace_back(text.substr(start, st.pos - start));
			parts.push_back(scanEscapeSequence(
				EscapeSequenceScanningFlags::String |
				(shouldEmitInvalidEscapeError
				     ? EscapeSequenceScanningFlags::ReportErrors
				     : EscapeSequenceScanningFlags{})));
			start = st.pos;
			continue;
		}
		if (ch == '\r') {
			parts.emplace_back(text.substr(start, st.pos - start));
			st.pos++;
			if (char_() == '\n')
				st.pos++;
			parts.emplace_back("\n");
			start = st.pos;
			continue;
		}
		st.pos++;
	}
	std::string joined;
	for (auto p : parts)
		joined += p;
	st.tokenValue = joined;
	return token;
}

std::string Scanner::scanEscapeSequence(EscapeSequenceScanningFlags flags) {
	int start = st.pos;
	st.pos++;
	char32_t ch = char_();
	if (ch == static_cast<char32_t>(-1)) {
		error(Unexpected_end_of_text);
		return "";
	}
	st.pos++;
	switch (ch) {
	case '0':
		if (!isDigit(char_())) {
			return "\x00";
		}
		[[fallthrough]];
	case '1':
	case '2':
	case '3':
		if (isOctalDigit(char_()))
			st.pos++;
		[[fallthrough]];
	case '4':
	case '5':
	case '6':
	case '7':
		if (isOctalDigit(char_()))
			st.pos++;
		st.tokenFlags |= TokenFlagsContainsInvalidEscape;
		if ((flags & EscapeSequenceScanningFlags::ReportInvalidEscapeErrors) !=
		    EscapeSequenceScanningFlags{}) {
			std::string digits(text.substr(start + 1, st.pos - start - 1));
			long code = std::strtol(digits.c_str(), nullptr, 8);
			char buf[16];
			std::snprintf(buf, sizeof(buf), "\\x%02lx", code);
			if ((flags & EscapeSequenceScanningFlags::RegularExpression) !=
			        EscapeSequenceScanningFlags{} &&
			    (flags & EscapeSequenceScanningFlags::AtomEscape) ==
			        EscapeSequenceScanningFlags{} &&
			    ch != '0') {
				errorAt(
					Octal_escape_sequences_and_backreferences_are_not_allowed_in_a_character_class_If_this_was_intended_as_an_escape_sequence_use_the_syntax_0_instead,
					start, st.pos - start, {buf});
			} else {
				errorAt(Octal_escape_sequences_are_not_allowed_Use_the_syntax_0,
				        start, st.pos - start, {buf});
			}
			return utf8String(static_cast<char32_t>(code));
		}
		return std::string(text.substr(start, st.pos - start));
	case '8':
	case '9':
		st.tokenFlags |= TokenFlagsContainsInvalidEscape;
		if ((flags & EscapeSequenceScanningFlags::ReportInvalidEscapeErrors) !=
		    EscapeSequenceScanningFlags{}) {
			if ((flags & EscapeSequenceScanningFlags::RegularExpression) !=
			        EscapeSequenceScanningFlags{} &&
			    (flags & EscapeSequenceScanningFlags::AtomEscape) ==
			        EscapeSequenceScanningFlags{}) {
				errorAt(
					Decimal_escape_sequences_and_backreferences_are_not_allowed_in_a_character_class,
					start, st.pos - start);
			} else {
				errorAt(Escape_sequence_0_is_not_allowed, start,
				        st.pos - start,
				        {std::string(text.substr(start, st.pos - start))});
			}
			return utf8String(ch);
		}
		return std::string(text.substr(start, st.pos - start));
	case 'b':
		return "\b";
	case 't':
		return "\t";
	case 'n':
		return "\n";
	case 'v':
		return "\v";
	case 'f':
		return "\f";
	case 'r':
		return "\r";
	case '\'':
		return "'";
	case '"':
		return "\"";
	case 'u': {
		bool extended = char_() == '{';
		st.pos -= 2;
		char32_t codePoint = scanUnicodeEscape(
			(flags & EscapeSequenceScanningFlags::ReportInvalidEscapeErrors) !=
			EscapeSequenceScanningFlags{});
		if (extended) {
			if ((flags &
			     EscapeSequenceScanningFlags::AllowExtendedUnicodeEscape) ==
			    EscapeSequenceScanningFlags{}) {
				st.tokenFlags |= TokenFlagsContainsInvalidEscape;
				if ((flags &
				     EscapeSequenceScanningFlags::ReportInvalidEscapeErrors) !=
				    EscapeSequenceScanningFlags{}) {
					errorAt(
						Unicode_escape_sequences_are_only_available_when_the_Unicode_u_flag_or_the_Unicode_Sets_v_flag_is_set,
						start, st.pos - start);
				}
			}
			if (static_cast<int32_t>(codePoint) < 0)
				return std::string(text.substr(start, st.pos - start));
			if ((flags & EscapeSequenceScanningFlags::RegularExpression) ==
			        EscapeSequenceScanningFlags{} &&
			    isHighSurrogate(codePoint)) {
				if (auto [combined, ok] = scanLowSurrogateEscape(codePoint);
				    ok)
					return utf8String(combined);
			}
			char buf[4];
			int n = encodeJSStringRune(codePoint, buf);
			return std::string(buf, n);
		}
		if (static_cast<int32_t>(codePoint) < 0) {
			return std::string(text.substr(start, st.pos - start));
		} else if (isHighSurrogate(codePoint)) {
			if ((flags & EscapeSequenceScanningFlags::RegularExpression) ==
			    EscapeSequenceScanningFlags{}) {
				if (auto [combined, ok] = scanLowSurrogateEscape(codePoint);
				    ok)
					return utf8String(combined);
			} else if ((flags &
			            EscapeSequenceScanningFlags::AnyUnicodeMode) !=
			               EscapeSequenceScanningFlags{} &&
			           char_() == '\\' && charAt(1) == 'u' &&
			           charAt(2) != '{') {
				int savedPos = st.pos;
				char32_t nextCodePoint = scanUnicodeEscape(
					(flags &
					 EscapeSequenceScanningFlags::ReportInvalidEscapeErrors) !=
					EscapeSequenceScanningFlags{});
				if (isLowSurrogate(nextCodePoint)) {
					return utf8String(
						surrogatePairToCodePoint(codePoint, nextCodePoint));
				}
				st.pos = savedPos;
			}
		}
		char buf[4];
		int n = encodeJSStringRune(codePoint, buf);
		return std::string(buf, n);
	}
	case 'x':
		for (; st.pos < start + 4; st.pos++) {
			if (!isHexDigit(char_())) {
				st.tokenFlags |= TokenFlagsContainsInvalidEscape;
				if ((flags &
				     EscapeSequenceScanningFlags::ReportInvalidEscapeErrors) !=
				    EscapeSequenceScanningFlags{})
					error(Hexadecimal_digit_expected);
				return std::string(text.substr(start, st.pos - start));
			}
		}
		st.tokenFlags |= TokenFlagsHexEscape;
		{
			std::string hex(text.substr(start + 2, st.pos - start - 2));
			long v = std::strtol(hex.c_str(), nullptr, 16);
			return utf8String(static_cast<char32_t>(v));
		}
	case '\r':
		if (char_() == '\n')
			st.pos++;
		[[fallthrough]];
	case '\n':
		return "";
	default:
		if (ch >= kRuneSelf) {
			st.pos--;
			int size;
			ch = decodeUtf8Rune(text.substr(st.pos), &size);
			st.pos += size;
		}
		if (ch == 0x2028 || ch == 0x2029)
			return "";
		if ((flags & EscapeSequenceScanningFlags::AnyUnicodeMode) !=
		        EscapeSequenceScanningFlags{} ||
		    ((flags & EscapeSequenceScanningFlags::RegularExpression) !=
		         EscapeSequenceScanningFlags{} &&
		     (flags & EscapeSequenceScanningFlags::AnnexB) ==
		         EscapeSequenceScanningFlags{} &&
		     isIdentifierPart(ch))) {
			errorAt(This_character_cannot_be_escaped_in_a_regular_expression,
			        start, st.pos - start);
		}
		return utf8String(ch);
	}
}

char32_t Scanner::scanUnicodeEscape(bool shouldEmitInvalidEscapeError) {
	st.pos += 2;
	int start = st.pos;
	bool extended = char_() == '{';
	std::string hexDigits;
	if (extended) {
		st.pos++;
		hexDigits = scanHexDigits(1, true, false);
	} else {
		st.tokenFlags |= TokenFlagsUnicodeEscape;
		hexDigits = scanHexDigits(4, false, false);
	}
	if (hexDigits.empty()) {
		st.tokenFlags |= TokenFlagsContainsInvalidEscape;
		if (shouldEmitInvalidEscapeError)
			error(Hexadecimal_digit_expected);
		return static_cast<char32_t>(-1);
	}
	long hexValue = std::strtol(hexDigits.c_str(), nullptr, 16);
	if (extended) {
		bool isInvalidExtendedEscape = false;
		if (hexValue > 0x10FFFF) {
			if (shouldEmitInvalidEscapeError)
				errorAt(
					An_extended_Unicode_escape_value_must_be_between_0x0_and_0x10FFFF_inclusive,
					start + 1, st.pos - start - 1);
			isInvalidExtendedEscape = true;
		}
		if (st.pos >= end) {
			if (shouldEmitInvalidEscapeError)
				error(Unexpected_end_of_text);
			isInvalidExtendedEscape = true;
		} else if (char_() == '}') {
			st.pos++;
		} else {
			if (shouldEmitInvalidEscapeError)
				error(Unterminated_Unicode_escape_sequence);
			isInvalidExtendedEscape = true;
		}
		if (isInvalidExtendedEscape) {
			st.tokenFlags |= TokenFlagsContainsInvalidEscape;
			return static_cast<char32_t>(-1);
		}
		st.tokenFlags |= TokenFlagsExtendedUnicodeEscape;
	}
	return static_cast<char32_t>(hexValue);
}

std::pair<char32_t, bool> Scanner::scanLowSurrogateEscape(char32_t high) {
	if (char_() != '\\' || charAt(1) != 'u')
		return {0, false};
	int savedPos = st.pos;
	TokenFlags savedTokenFlags = st.tokenFlags;
	char32_t low = scanUnicodeEscape(false);
	if (isLowSurrogate(low))
		return {surrogatePairToCodePoint(high, low), true};
	st.pos = savedPos;
	st.tokenFlags = savedTokenFlags;
	return {0, false};
}

char32_t Scanner::peekUnicodeEscape() {
	if (charAt(1) == 'u') {
		int savePos = st.pos;
		TokenFlags saveTokenFlags = st.tokenFlags;
		char32_t codePoint = scanUnicodeEscape(false);
		st.pos = savePos;
		st.tokenFlags = saveTokenFlags;
		return codePoint;
	}
	return static_cast<char32_t>(-1);
}

// ---------------------------------------------------------------------------
// numbers
// ---------------------------------------------------------------------------

Kind Scanner::scanNumber() {
	int start = st.pos;
	std::string fixedPart;
	if (char_() == '0') {
		st.pos++;
		if (char_() == '_') {
			st.tokenFlags |=
				TokenFlagsContainsSeparator | TokenFlagsContainsInvalidSeparator;
			errorAt(Numeric_separators_are_not_allowed_here, st.pos, 1);
			st.pos = start;
			fixedPart = scanNumberFragment();
		} else {
			auto [digits, isOctal] = scanDigits();
			if (digits.empty()) {
				fixedPart = "0";
			} else if (!isOctal) {
				st.tokenFlags |= TokenFlagsContainsLeadingZero;
				fixedPart = digits;
			} else {
				std::string ds(digits);
				long long val = std::strtoll(ds.c_str(), nullptr, 8);
				st.tokenValue = std::to_string(val);
				st.tokenFlags |= TokenFlagsOctal;
				bool withMinus = st.token == Kind::MinusToken;
				std::string literal =
					std::string(withMinus ? "-" : "") + "0o" +
					[&] {
						char buf[32];
						std::snprintf(buf, sizeof(buf), "%llo", val);
						return std::string(buf);
					}();
				if (withMinus)
					start--;
				errorAt(Octal_literals_are_not_allowed_Use_the_syntax_0, start,
				        st.pos - start, {literal});
				return Kind::NumericLiteral;
			}
		}
	} else {
		fixedPart = scanNumberFragment();
	}
	int fixedPartEnd = st.pos;
	std::string fractionalPart;
	std::string_view exponentPreamble;
	std::string exponentPart;
	if (char_() == '.') {
		st.pos++;
		fractionalPart = scanNumberFragment();
	}
	int endPos = st.pos;
	if (char_() == 'E' || char_() == 'e') {
		st.pos++;
		st.tokenFlags |= TokenFlagsScientific;
		if (char_() == '+' || char_() == '-')
			st.pos++;
		int startNumericPart = st.pos;
		exponentPart = scanNumberFragment();
		if (exponentPart.empty()) {
			error(Digit_expected);
		} else {
			exponentPreamble = text.substr(endPos, startNumericPart - endPos);
			endPos = st.pos;
		}
	}
	if ((st.tokenFlags & TokenFlagsContainsSeparator) != 0) {
		std::string v(fixedPart);
		if (!fractionalPart.empty())
			v += "." + fractionalPart;
		if (!exponentPart.empty())
			v += std::string(exponentPreamble) + exponentPart;
		st.tokenValue = v;
	} else {
		st.tokenValue = text.substr(start, endPos - start);
	}
	if ((st.tokenFlags & TokenFlagsContainsLeadingZero) != 0) {
		errorAt(Decimals_with_leading_zeros_are_not_allowed, start,
		        st.pos - start);
		st.tokenValue = numberFromString(st.tokenValue).string();
		return Kind::NumericLiteral;
	}
	Kind result;
	if (fixedPartEnd == st.pos) {
		result = scanBigIntSuffix();
	} else {
		st.tokenValue = numberFromString(st.tokenValue).string();
		result = Kind::NumericLiteral;
	}
	auto [ch, _size] = charAndSize();
	if (isIdentifierStart(ch)) {
		int idStart = st.pos;
		std::string id = scanIdentifierParts(IdentifierVariant::Standard);
		if (result != Kind::BigIntLiteral && id.size() == 1 &&
		    text[idStart] == 'n') {
			if ((st.tokenFlags & TokenFlagsScientific) != 0) {
				errorAt(A_bigint_literal_cannot_use_exponential_notation, start,
				        st.pos - start);
				return result;
			}
			if (fixedPartEnd < idStart) {
				errorAt(A_bigint_literal_must_be_an_integer, start,
				        st.pos - start);
				return result;
			}
		}
		errorAt(An_identifier_or_keyword_cannot_immediately_follow_a_numeric_literal,
		        idStart, st.pos - idStart);
		st.pos = idStart;
	}
	return result;
}

std::string Scanner::scanNumberFragment() {
	int start = st.pos;
	bool allowSeparator = false;
	bool isPreviousTokenSeparator = false;
	std::string result;
	for (;;) {
		int before = st.pos;
		scanASCIIWhile([](uint8_t b) { return b >= '0' && b <= '9'; });
		if (st.pos > before) {
			allowSeparator = true;
			isPreviousTokenSeparator = false;
		}
		char32_t ch = char_();
		if (ch == '_') {
			st.tokenFlags |= TokenFlagsContainsSeparator;
			if (allowSeparator) {
				allowSeparator = false;
				isPreviousTokenSeparator = true;
				result.append(text.substr(start, st.pos - start));
			} else {
				st.tokenFlags |= TokenFlagsContainsInvalidSeparator;
				if (isPreviousTokenSeparator) {
					errorAt(Multiple_consecutive_numeric_separators_are_not_permitted,
					        st.pos, 1);
				} else {
					errorAt(Numeric_separators_are_not_allowed_here, st.pos, 1);
				}
			}
			st.pos++;
			start = st.pos;
			continue;
		}
		break;
	}
	if (isPreviousTokenSeparator) {
		st.tokenFlags |= TokenFlagsContainsInvalidSeparator;
		errorAt(Numeric_separators_are_not_allowed_here, st.pos - 1, 1);
	}
	if (result.empty())
		return std::string(text.substr(start, st.pos - start));
	result.append(text.substr(start, st.pos - start));
	return result;
}

std::pair<std::string_view, bool> Scanner::scanDigits() {
	int start = st.pos;
	bool isOctal = true;
	while (isDigit(char_())) {
		if (!isOctalDigit(char_()))
			isOctal = false;
		st.pos++;
	}
	return {text.substr(start, st.pos - start), isOctal};
}

std::string Scanner::scanHexDigits(int minCount, bool scanAsManyAsPossible,
                                   bool canHaveSeparators) {
	int digitCount = 0;
	int start = st.pos;
	bool allowSeparator = false;
	bool isPreviousTokenSeparator = false;
	while (digitCount < minCount || scanAsManyAsPossible) {
		char32_t ch = char_();
		if (isHexDigit(ch)) {
			allowSeparator = canHaveSeparators;
			isPreviousTokenSeparator = false;
			digitCount++;
		} else if (canHaveSeparators && ch == '_') {
			st.tokenFlags |= TokenFlagsContainsSeparator;
			if (allowSeparator) {
				allowSeparator = false;
				isPreviousTokenSeparator = true;
			} else if (isPreviousTokenSeparator) {
				errorAt(Multiple_consecutive_numeric_separators_are_not_permitted,
				        st.pos, 1);
			} else {
				errorAt(Numeric_separators_are_not_allowed_here, st.pos, 1);
			}
		} else {
			break;
		}
		st.pos++;
	}
	if (isPreviousTokenSeparator)
		errorAt(Numeric_separators_are_not_allowed_here, st.pos - 1, 1);
	if (digitCount < minCount)
		return "";
	std::string_view digits = text.substr(start, st.pos - start);
	if (auto it = hexDigitCache.find(std::string(digits));
	    it != hexDigitCache.end()) {
		return it->second;
	}
	std::string original(digits);
	std::string result(digits);
	if ((st.tokenFlags & TokenFlagsContainsSeparator) != 0) {
		result.clear();
		for (char c : original)
			if (c != '_')
				result.push_back(c);
	}
	for (auto& c : result)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	hexDigitCache[original] = result;
	return result;
}

std::string Scanner::scanBinaryOrOctalDigits(int32_t base) {
	std::string sb;
	bool allowSeparator = false;
	bool isPreviousTokenSeparator = false;
	for (;;) {
		char32_t ch = char_();
		if (isDigit(ch) && ch - '0' < base) {
			sb.push_back(static_cast<char>(ch));
			allowSeparator = true;
			isPreviousTokenSeparator = false;
		} else if (ch == '_') {
			st.tokenFlags |= TokenFlagsContainsSeparator;
			if (allowSeparator) {
				allowSeparator = false;
				isPreviousTokenSeparator = true;
			} else if (isPreviousTokenSeparator) {
				errorAt(Multiple_consecutive_numeric_separators_are_not_permitted,
				        st.pos, 1);
			} else {
				errorAt(Numeric_separators_are_not_allowed_here, st.pos, 1);
			}
		} else {
			break;
		}
		st.pos++;
	}
	if (isPreviousTokenSeparator)
		errorAt(Numeric_separators_are_not_allowed_here, st.pos - 1, 1);
	return sb;
}

Kind Scanner::scanBigIntSuffix() {
	if (char_() == 'n') {
		st.tokenValue += "n";
		if ((st.tokenFlags & TokenFlagsBinaryOrOctalSpecifier) != 0)
			st.tokenValue = parsePseudoBigInt(st.tokenValue) + "n";
		st.pos++;
		return Kind::BigIntLiteral;
	}
	if (auto it = numberCache.find(st.tokenValue); it != numberCache.end()) {
		st.tokenValue = it->second;
	} else {
		std::string tv = numberFromString(st.tokenValue).string();
		if (tv == st.tokenValue)
			tv = st.tokenValue;
		numberCache[st.tokenValue] = tv;
		st.tokenValue = tv;
	}
	return Kind::NumericLiteral;
}

void Scanner::scanInvalidCharacter() {
	auto [ch, size] = charAndSize();
	errorAt(Invalid_character, st.pos, size);
	st.pos += size;
	st.token = Kind::Unknown;
}

// ---------------------------------------------------------------------------
// free functions
// ---------------------------------------------------------------------------

Kind getIdentifierToken(std::string_view str) {
	if (str.size() >= 2 && str.size() <= 12 && str[0] >= 'a' && str[0] <= 'z') {
		if (auto it = textToKeyword.find(str);
		    it != textToKeyword.end() && it->second != Kind::Unknown)
			return it->second;
	}
	return Kind::Identifier;
}

bool isValidIdentifier(std::string_view s) {
	if (s.empty())
		return false;
	int i = 0;
	for (; i < static_cast<int>(s.size());) {
		int size;
		char32_t ch = decodeUtf8Rune(s.substr(i), &size);
		if ((i == 0 && !isIdentifierStart(ch)) ||
		    (i != 0 && !isIdentifierPart(ch)))
			return false;
		i += size;
	}
	return true;
}

bool isWordCharacter(char32_t ch) {
	return isASCIILetter(ch) || isDigit(ch) || ch == '_';
}

bool isIdentifierStart(char32_t ch) {
	return isASCIILetter(ch) || ch == '_' || ch == '$' ||
	       (ch >= kRuneSelf && isUnicodeIdentifierStart(ch));
}

bool isIdentifierPart(char32_t ch) {
	return isIdentifierPartEx(ch, LanguageVariant::Standard);
}

bool isIdentifierPartEx(char32_t ch, LanguageVariant languageVariant) {
	return isWordCharacter(ch) || ch == '$' ||
	       (ch >= kRuneSelf && isUnicodeIdentifierPart(ch)) ||
	       (languageVariant == LanguageVariant::JSX && ch == '-');
}

std::string_view tokenToString(Kind token) {
	return tokenToTextTable()[static_cast<size_t>(token)];
}

Kind stringToToken(std::string_view s) {
	if (auto it = textToToken.find(s); it != textToToken.end())
		return it->second;
	return Kind::Unknown;
}

const std::vector<std::string_view>& getViableKeywordSuggestions() {
	static const auto* v = [] {
		auto* r = new std::vector<std::string_view>();
		for (auto& [k, _] : kKeywordList)
			if (std::string_view(k).size() > 2)
				r->push_back(k);
		return r;
	}();
	return *v;
}

// ---------------------------------------------------------------------------
// trivia
// ---------------------------------------------------------------------------

constexpr int mergeConflictMarkerLength = 7;
constexpr uint8_t maxAsciiCharacter = 127;

bool couldStartTrivia(std::string_view text, int pos) {
	switch (text[pos]) {
	case '\r':
	case '\n':
	case '\t':
	case '\v':
	case '\f':
	case ' ':
	case '/':
	case '<':
	case '|':
	case '=':
	case '>':
		return true;
	case '#':
		return pos == 0;
	default:
		return static_cast<unsigned char>(text[pos]) > maxAsciiCharacter;
	}
}

int skipTrivia(std::string_view text, int pos) {
	return skipTriviaEx(text, pos, SkipTriviaOptions{});
}

int skipTriviaEx(std::string_view text, int pos, const SkipTriviaOptions& options) {
	if (positionIsSynthesized(pos))
		return pos;

	int textLen = static_cast<int>(text.size());
	bool canConsumeStar = false;
	for (;;) {
		if (pos >= textLen)
			return pos;
		int size;
		char32_t ch = decodeUtf8Rune(text.substr(pos), &size);
		switch (ch) {
		case '\r':
			if (pos + 1 < textLen && text[pos + 1] == '\n')
				pos++;
			[[fallthrough]];
		case '\n':
			pos++;
			if (options.stopAfterLineBreak)
				return pos;
			canConsumeStar = options.inJSDoc;
			continue;
		case '\t':
		case '\v':
		case '\f':
		case ' ':
			pos++;
			continue;
		case '/':
			if (options.stopAtComments)
				break;
			if (pos + 1 < textLen) {
				if (text[pos + 1] == '/') {
					pos += 2;
					while (pos < textLen) {
						int sz;
						char32_t c =
							decodeUtf8Rune(text.substr(pos), &sz);
						if (isLineBreak(c))
							break;
						pos += sz;
					}
					canConsumeStar = false;
					continue;
				}
				if (text[pos + 1] == '*') {
					pos += 2;
					while (pos < textLen) {
						if (text[pos] == '*' && pos + 1 < textLen &&
						    text[pos + 1] == '/') {
							pos += 2;
							break;
						}
						int sz;
						decodeUtf8Rune(text.substr(pos), &sz);
						pos += sz;
					}
					canConsumeStar = false;
					continue;
				}
			}
			break;
		case '<':
		case '|':
		case '=':
		case '>':
			if (isConflictMarkerTrivia(text, pos)) {
				pos = scanConflictMarkerTrivia(text, pos, nullptr);
				canConsumeStar = false;
				continue;
			}
			break;
		case '#':
			if (pos == 0 && isShebangTrivia(text, pos)) {
				pos = scanShebangTrivia(text, pos);
				canConsumeStar = false;
				continue;
			}
			break;
		case '*':
			if (canConsumeStar) {
				pos++;
				canConsumeStar = false;
				continue;
			}
			break;
		default:
			if (static_cast<uint32_t>(ch) > maxAsciiCharacter &&
			    isWhiteSpaceLike(ch)) {
				pos += size;
				continue;
			}
			break;
		}
		return pos;
	}
}

bool isConflictMarkerTrivia(std::string_view text, int pos) {
	if (pos + 1 >= static_cast<int>(text.size()) ||
	    text[pos + 1] != text[pos])
		return false;

	bool atLineStart =
		pos == 0 || isLineBreak(static_cast<unsigned char>(text[pos - 1]));
	if (!atLineStart && pos >= 2) {
		int w;
		char32_t prev = decodeLastUtf8Rune(text.substr(0, pos - 2), &w);
		atLineStart = isLineBreak(prev);
	}
	if (atLineStart) {
		char ch = text[pos];
		if (pos + mergeConflictMarkerLength <
		    static_cast<int>(text.size())) {
			for (int i = 0; i < mergeConflictMarkerLength; i++) {
				if (text[pos + i] != ch)
					return false;
			}
			return ch == '=' ||
			       text[pos + mergeConflictMarkerLength] == ' ';
		}
	}
	return false;
}

int scanConflictMarkerTrivia(
	std::string_view text, int pos,
	const std::function<void(const DiagnosticMessage*, int, int)>& reportError) {
	if (reportError)
		reportError(Merge_conflict_marker_encountered, pos,
		            mergeConflictMarkerLength);
	int size;
	char32_t ch = decodeUtf8Rune(text.substr(pos), &size);
	int length = static_cast<int>(text.size());

	if (ch == '<' || ch == '>') {
		while (pos < length && !isLineBreak(ch)) {
			pos += size;
			ch = decodeUtf8Rune(text.substr(pos), &size);
		}
	} else {
		while (pos < length) {
			char currentChar = text[pos];
			if ((currentChar == '=' || currentChar == '>') &&
			    static_cast<char32_t>(static_cast<unsigned char>(currentChar)) !=
			        ch &&
			    isConflictMarkerTrivia(text, pos)) {
				break;
			}
			pos++;
		}
	}
	return pos;
}

bool isShebangTrivia(std::string_view text, int pos) {
	if (text.size() < 2)
		return false;
	return text[0] == '#' && text[1] == '!';
}

int scanShebangTrivia(std::string_view text, int pos) {
	pos += 2;
	while (pos < static_cast<int>(text.size())) {
		int size;
		char32_t ch = decodeUtf8Rune(text.substr(pos), &size);
		if (isLineBreak(ch))
			break;
		pos += size;
	}
	return pos;
}

std::string_view getShebang(std::string_view text) {
	if (!isShebangTrivia(text, 0))
		return "";
	int endPos = scanShebangTrivia(text, 0);
	return text.substr(0, endPos);
}

// ---------------------------------------------------------------------------
// position helpers
// ---------------------------------------------------------------------------

Scanner& getScannerForSourceFile(Scanner& s, SourceFile* sourceFile, int pos) {
	s.setText(sourceFile->text);
	s.st.pos = pos;
	s.st.fullStartPos = pos;
	s.st.tokenStart = pos;
	s.languageVariant = sourceFile->LanguageVariant;
	s.scan();
	return s;
}

Kind scanTokenAtPosition(SourceFile* sourceFile, int pos) {
	Scanner s;
	getScannerForSourceFile(s, sourceFile, pos);
	return s.token();
}

TextRange getRangeOfTokenAtPosition(SourceFile* sourceFile, int pos) {
	Scanner s;
	getScannerForSourceFile(s, sourceFile, pos);
	return TextRange{s.st.tokenStart, s.st.pos};
}

int getTokenPosOfNode(Node* node, SourceFile* sourceFile, bool includeJSDoc) {
	if (nodeIsMissing(node))
		return node->pos();
	if (isJSDocNode(node) || node->kind == Kind::JsxText) {
		SkipTriviaOptions o;
		o.stopAtComments = true;
		return skipTriviaEx(sourceFile->text, node->pos(), o);
	}
	if (includeJSDoc) {
		auto jsdoc = node->jsDoc(sourceFile);
		if (!jsdoc.empty())
			return getTokenPosOfNode(jsdoc[0], sourceFile, false);
	}
	SkipTriviaOptions o;
	o.inJSDoc = (node->flags & NodeFlagsJSDoc) != 0;
	return skipTriviaEx(sourceFile->text, node->pos(), o);
}

static TextRange getErrorRangeForArrowFunction(SourceFile* sourceFile,
                                               Node* node) {
	int pos = skipTrivia(sourceFile->text, node->pos());
	Node* body = node->body();
	if (body != nullptr && body->kind == Kind::Block) {
		int startLine = getECMALineOfPosition(sourceFile, body->pos());
		int endLine = getECMALineOfPosition(sourceFile, body->end());
		if (startLine < endLine)
			return TextRange{pos,
			                 getECMAEndLinePosition(sourceFile, startLine) + 1};
	}
	return TextRange{pos, node->end()};
}

static Node* findOriginatingJSDocSatisfiesTag(SourceFile* sourceFile,
                                            Node* node) {
	Node* targetType = node->as<SatisfiesExpression>()->Type;
	if ((targetType->flags & NodeFlagsReparsed) == 0)
		return nullptr;
	for (Node* current = node->parent; current != nullptr;
	     current = current->parent) {
		if ((current->flags & NodeFlagsHasJSDoc) == 0)
			continue;
		Node* firstSatisfiesTag = nullptr;
		for (Node* jsDoc : current->eagerJSDoc(sourceFile)) {
			NodeList* tags = jsDoc->as<JSDoc>()->Tags;
			if (tags != nullptr) {
				for (Node* tag : tags->nodes) {
					if (!isJSDocSatisfiesTag(tag))
						continue;
					if (firstSatisfiesTag == nullptr)
						firstSatisfiesTag = tag;
					if (Node* typeExpr =
					        tag->as<JSDocSatisfiesTag>()->TypeExpression;
					    typeExpr != nullptr) {
						if (Node* t = typeExpr->type();
						    t != nullptr && t->loc == targetType->loc)
							return tag;
					}
				}
			}
		}
		return firstSatisfiesTag;
	}
	return nullptr;
}

TextRange getErrorRangeForNode(SourceFile* sourceFile, Node* node) {
	Node* errorNode = node;
	switch (node->kind) {
	case Kind::SourceFile: {
		int pos = skipTrivia(sourceFile->text, 0);
		if (pos == static_cast<int>(sourceFile->text.size()))
			return TextRange{0, 0};
		return getRangeOfTokenAtPosition(sourceFile, pos);
	}
	case Kind::FunctionDeclaration:
	case Kind::MethodDeclaration:
		if ((node->flags & NodeFlagsReparsed) != 0) {
			errorNode = node;
			break;
		}
		[[fallthrough]];
	case Kind::VariableDeclaration:
	case Kind::BindingElement:
	case Kind::ClassDeclaration:
	case Kind::InterfaceDeclaration:
	case Kind::ModuleDeclaration:
	case Kind::EnumDeclaration:
	case Kind::EnumMember:
	case Kind::FunctionExpression:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::NamespaceImport:
		errorNode = getNameOfDeclaration(node);
		break;
	case Kind::ClassExpression:
		errorNode = node->name();
		break;
	case Kind::ArrowFunction:
		return getErrorRangeForArrowFunction(sourceFile, node);
	case Kind::CaseClause:
	case Kind::DefaultClause: {
		int start = skipTrivia(sourceFile->text, node->pos());
		int endP = node->end();
		auto statements = node->statements();
		if (!statements.empty())
			endP = statements[0]->pos();
		return TextRange{start, endP};
	}
	case Kind::ReturnStatement:
	case Kind::YieldExpression: {
		int pos = skipTrivia(sourceFile->text, node->pos());
		return getRangeOfTokenAtPosition(sourceFile, pos);
	}
	case Kind::SatisfiesExpression: {
		if (Node* jsDocSatisfiesTag =
		        findOriginatingJSDocSatisfiesTag(sourceFile, node)) {
			int pos =
				skipTrivia(sourceFile->text, jsDocSatisfiesTag->tagName()->pos());
			return getRangeOfTokenAtPosition(sourceFile, pos);
		}
		int pos = skipTrivia(
			sourceFile->text, node->as<SatisfiesExpression>()->Expression->end());
		return getRangeOfTokenAtPosition(sourceFile, pos);
	}
	case Kind::Constructor: {
		if ((node->flags & NodeFlagsReparsed) != 0) {
			errorNode = node;
			break;
		}
		Scanner scanner;
		getScannerForSourceFile(scanner, sourceFile, node->pos());
		int start = scanner.tokenStart();
		while (scanner.token() != Kind::ConstructorKeyword &&
		       scanner.token() != Kind::StringLiteral &&
		       scanner.token() != Kind::EndOfFile) {
			scanner.scan();
		}
		return TextRange{start, scanner.tokenEnd()};
	}
	default:
		break;
	}
	if (errorNode == nullptr)
		return getRangeOfTokenAtPosition(sourceFile, node->pos());
	int pos = errorNode->pos();
	if (!nodeIsMissing(errorNode) && !isJsxText(errorNode))
		pos = skipTrivia(sourceFile->text, pos);
	return TextRange{pos, errorNode->end()};
}

int computeLineOfPosition(const std::vector<TextPos>& lineStarts, int pos) {
	int low = 0;
	int high = static_cast<int>(lineStarts.size()) - 1;
	while (low <= high) {
		int middle = low + ((high - low) >> 1);
		int value = lineStarts[middle];
		if (value < pos) {
			low = middle + 1;
		} else if (value > pos) {
			high = middle - 1;
		} else {
			return middle;
		}
	}
	return low - 1;
}

const ECMALineStarts& getECMALineStarts(SourceFile* sourceFile) {
	return sourceFile->ecmaLineMap();
}

int getECMALineOfPosition(SourceFile* sourceFile, int pos) {
	return computeLineOfPosition(getECMALineStarts(sourceFile), pos);
}

std::pair<int, int> getECMALineAndUTF16CharacterOfPosition(SourceFile* sourceFile,
                                                         int pos) {
	auto& lineMap = getECMALineStarts(sourceFile);
	int line = computeLineOfPosition(lineMap, pos);
	int character = utf16Len(
		sourceFile->text.substr(lineMap[line], pos - lineMap[line]));
	return {line, character};
}

std::pair<int, int> getECMALineAndByteOffsetOfPosition(SourceFile* sourceFile,
                                                     int pos) {
	auto& lineMap = getECMALineStarts(sourceFile);
	int line = computeLineOfPosition(lineMap, pos);
	return {line, pos - lineMap[line]};
}

int getECMAEndLinePosition(SourceFile* sourceFile, int line) {
	int pos = getECMALineStarts(sourceFile)[line];
	for (;;) {
		int size;
		char32_t ch = decodeUtf8Rune(sourceFile->text.substr(pos), &size);
		if (size == 0 || isLineBreak(ch))
			return pos - 1;
		pos += size;
	}
}

int getECMAPositionOfLineAndUTF16Character(SourceFile* sourceFile, int line,
                                         int character) {
	return computePositionOfLineAndUTF16Character(
		getECMALineStarts(sourceFile), line, character, sourceFile->text,
		false);
}

int getECMAPositionOfLineAndByteOffset(SourceFile* sourceFile, int line,
                                       int byteOffset) {
	return computePositionOfLineAndByteOffset(getECMALineStarts(sourceFile),
	                                          line, byteOffset);
}

int computePositionOfLineAndByteOffset(
	const std::vector<TextPos>& lineStarts, int line, int byteOffset) {
	if (line < 0 || line >= static_cast<int>(lineStarts.size()))
		abort();
	return lineStarts[line] + byteOffset;
}

int computePositionOfLineAndUTF16Character(
	const std::vector<TextPos>& lineStarts, int line, int character,
	std::string_view text, bool allowEdits) {
	if (line < 0 || line >= static_cast<int>(lineStarts.size())) {
		if (allowEdits) {
			if (line < 0) {
				line = 0;
			} else {
				line = static_cast<int>(lineStarts.size()) - 1;
			}
		} else {
			abort();
		}
	}

	int lineStart = lineStarts[line];

	if (character > 0) {
		int lineEnd = static_cast<int>(text.size());
		if (line + 1 < static_cast<int>(lineStarts.size()))
			lineEnd = lineStarts[line + 1];
		int utf16Count = 0;
		int pos = lineStart;
		while (pos < lineEnd) {
			if (utf16Count >= character)
				break;
			int size;
			char32_t r = decodeUtf8Rune(text.substr(pos), &size);
			utf16Count += r < 0x10000 ? 1 : 2;
			pos += size;
		}
		if (!allowEdits) {
			if (pos == lineEnd && utf16Count < character)
				abort();
			return pos;
		}
		if (pos > static_cast<int>(text.size()))
			return static_cast<int>(text.size());
		return pos;
	}

	int res = lineStart;
	if (allowEdits) {
		if (res > static_cast<int>(text.size()))
			return static_cast<int>(text.size());
		return res;
	}
	return res;
}

// ---------------------------------------------------------------------------
// utilities.go
// ---------------------------------------------------------------------------

bool tokenIsIdentifierOrKeyword(Kind token) {
	return token >= Kind::Identifier;
}

Kind identifierToKeywordKind(const Identifier* node) {
	if (auto it = textToKeyword.find(node->Text); it != textToKeyword.end())
		return it->second;
	return Kind::Unknown;
}

static std::string stripLeadingJSDocComment(std::string_view line);

static bool isJSDocTypeExpressionOrChild(const Node* node) {
	if (isJSDocTypeExpression(node))
		return true;
	if ((node->flags & (NodeFlagsJSDoc | NodeFlagsReparsed)) == 0)
		return false;
	for (const Node* current = node; current != nullptr;
	     current = current->parent) {
		if (isTypeNode(current))
			return true;
	}
	return false;
}

static std::string normalizeJSDocTypeSourceText(std::string_view text) {
	auto lineStarts = computeECMALineStarts(text);
	if (lineStarts.size() == 1)
		return stripLeadingJSDocComment(text);
	std::string result;
	result.reserve(text.size());
	for (size_t i = 0; i < lineStarts.size(); i++) {
		if (i > 0)
			result += "\n";
		int lineEnd = static_cast<int>(text.size());
		if (i + 1 < lineStarts.size())
			lineEnd = lineStarts[i + 1];
		std::string_view line = text.substr(lineStarts[i], lineEnd - lineStarts[i]);
		while (!line.empty() && isLineBreak(line.back()))
			line.remove_suffix(1);
		result += stripLeadingJSDocComment(line);
	}
	return result;
}

static std::string stripLeadingJSDocComment(std::string_view line) {
	for (;;) {
		int w;
		char32_t r = decodeUtf8Rune(line, &w);
		if (w == 0 || !isWhiteSpaceLike(r))
			break;
		line.remove_prefix(w);
	}
	if (!line.empty() && line[0] == '*')
		line.remove_prefix(1);
	for (;;) {
		int w;
		char32_t r = decodeUtf8Rune(line, &w);
		if (w == 0 || !isWhiteSpaceLike(r))
			break;
		line.remove_prefix(w);
	}
	return std::string(line);
}

std::string getTextOfNodeFromSourceText(std::string_view sourceText,
                                        const Node* node, bool includeTrivia) {
	if (nodeIsMissing(node))
		return "";
	int pos = node->pos();
	if (!includeTrivia)
		pos = skipTrivia(sourceText, pos);
	std::string_view txt = sourceText.substr(pos, node->end() - pos);
	if (isJSDocTypeExpressionOrChild(node))
		return normalizeJSDocTypeSourceText(txt);
	if ((node->flags & NodeFlagsReparserTransformedLiteral) != 0) {
		if (isStringLiteral(node)) {
			if ((node->as<StringLiteral>()->TokenFlags &
			     TokenFlagsSingleQuote) != 0)
				return "'" + std::string(txt) + "'";
			return "\"" + std::string(txt) + "\"";
		} else if (isIdentifier(node)) {
			return node->text();
		}
		abort();
	}
	return std::string(txt);
}

std::string getSourceTextOfNodeFromSourceFile(SourceFile* sourceFile,
                                            const Node* node,
                                            bool includeTrivia) {
	return getTextOfNodeFromSourceText(sourceFile->text, node, includeTrivia);
}

std::string getTextOfNode(const Node* node) {
	return getSourceTextOfNodeFromSourceFile(
		const_cast<SourceFile*>(getSourceFileOfNode(node)), node, false);
}

std::string getTextOfJSDocComment(const NodeList* comment) {
	if (comment == nullptr)
		return "";
	std::string b;
	for (Node* n : comment->nodes) {
		switch (n->kind) {
		case Kind::JSDocText:
			b += n->text();
			break;
		case Kind::JSDocLink:
		case Kind::JSDocLinkCode:
		case Kind::JSDocLinkPlain:
			b += getTextOfNode(n);
			break;
		default:
			break;
		}
	}
	while (!b.empty() &&
	       std::isspace(static_cast<unsigned char>(b.back())))
		b.pop_back();
	return b;
}

std::string declarationNameToString(const Node* name) {
	if (name == nullptr || name->pos() == name->end())
		return "(Missing)";
	return getTextOfNode(name);
}

bool isIdentifierText(std::string_view name, LanguageVariant languageVariant) {
	int size;
	char32_t ch = decodeUtf8Rune(name, &size);
	if (!isIdentifierStart(ch))
		return false;
	for (size_t i = size; i < name.size();) {
		int sz;
		ch = decodeUtf8Rune(name.substr(i), &sz);
		if (!isIdentifierPartEx(ch, languageVariant))
			return false;
		i += sz;
	}
	return true;
}

bool isIntrinsicJsxName(std::string_view name) {
	return !name.empty() &&
	       ((name[0] >= 'a' && name[0] <= 'z') || name.find('-') != std::string_view::npos);
}

// ---------------------------------------------------------------------------
// comment range iteration
// ---------------------------------------------------------------------------

void getLeadingCommentRanges(std::string_view text, int pos,
                             const std::function<bool(const CommentRange&)>& yield) {
	iterateCommentRanges(text, pos, false, yield);
}

void getTrailingCommentRanges(std::string_view text, int pos,
                              const std::function<bool(const CommentRange&)>& yield) {
	iterateCommentRanges(text, pos, true, yield);
}

void iterateCommentRanges(
	std::string_view text, int pos, bool trailing,
	const std::function<bool(const CommentRange&)>& yield) {
	int pendingPos = 0;
	int pendingEnd = 0;
	Kind pendingKind = Kind::Unknown;
	bool pendingHasTrailingNewLine = false;
	bool hasPendingCommentRange = false;
	bool collecting = trailing;
	if (pos == 0) {
		collecting = true;
		if (isShebangTrivia(text, pos))
			pos = scanShebangTrivia(text, pos);
	}
	while (pos >= 0 && pos < static_cast<int>(text.size())) {
		int size;
		char32_t ch = decodeUtf8Rune(text.substr(pos), &size);
		switch (ch) {
		case '\r':
			if (pos + 1 < static_cast<int>(text.size()) && text[pos + 1] == '\n')
				pos++;
			[[fallthrough]];
		case '\n':
			pos++;
			if (trailing)
				goto scanDone;
			collecting = true;
			if (hasPendingCommentRange)
				pendingHasTrailingNewLine = true;
			continue;
		case '\t':
		case '\v':
		case '\f':
		case ' ':
			pos++;
			continue;
		case '/': {
			uint8_t nextChar = 0;
			if (pos + 1 < static_cast<int>(text.size()))
				nextChar = text[pos + 1];
			bool hasTrailingNewLine = false;
			if (nextChar == '/' || nextChar == '*') {
				Kind kind = nextChar == '/' ? Kind::SingleLineCommentTrivia
				                            : Kind::MultiLineCommentTrivia;
				int startPos = pos;
				pos += 2;
				if (nextChar == '/') {
					while (pos < static_cast<int>(text.size())) {
						int sz;
						char32_t c =
							decodeUtf8Rune(text.substr(pos), &sz);
						if (isLineBreak(c)) {
							hasTrailingNewLine = true;
							break;
						}
						pos += sz;
					}
				} else {
					if (auto i = text.substr(pos).find("*/");
					    i != std::string_view::npos) {
						pos += static_cast<int>(i) + 2;
					} else {
						pos = static_cast<int>(text.size());
					}
				}
				if (collecting) {
					if (hasPendingCommentRange) {
						CommentRange cr;
						cr.pos_ = pendingPos;
						cr.end_ = pendingEnd;
						cr.kind = pendingKind;
						cr.HasTrailingNewLine = pendingHasTrailingNewLine;
						if (!yield(cr))
							return;
					}
					pendingPos = startPos;
					pendingEnd = pos;
					pendingKind = kind;
					pendingHasTrailingNewLine = hasTrailingNewLine;
					hasPendingCommentRange = true;
				}
				continue;
			}
			goto scanDone;
		}
		default:
			if (static_cast<uint32_t>(ch) > maxAsciiCharacter &&
			    isWhiteSpaceLike(ch)) {
				if (hasPendingCommentRange && isLineBreak(ch))
					pendingHasTrailingNewLine = true;
				pos += size;
				continue;
			}
			goto scanDone;
		}
	}
scanDone:
	if (hasPendingCommentRange) {
		CommentRange cr;
		cr.pos_ = pendingPos;
		cr.end_ = pendingEnd;
		cr.kind = pendingKind;
		cr.HasTrailingNewLine = pendingHasTrailingNewLine;
		yield(cr);
	}
}

}  // namespace tsc
