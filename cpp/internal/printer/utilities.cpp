// Port of tsc/internal/printer/utilities.go — literal-text / escape helpers,
// line-terminator helpers, node-array lookup, generated-name helpers, and the
// lineCharacterCache.
#include "internal/printer/printer.h"

#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace tsc::printer {

namespace {

const std::unordered_map<char32_t, std::string>& jsxEscapedCharsMap() {
	static const std::unordered_map<char32_t, std::string> map = {
		{U'"', "&quot;"},
		{U'\'', "&apos;"},
	};
	return map;
}

const std::unordered_map<char32_t, std::string>& escapedCharsMap() {
	static const std::unordered_map<char32_t, std::string> map = {
		{U'\t', "\\t"},   {U'\v', "\\v"},   {U'\f', "\\f"},  {U'\b', "\\b"},
		{U'\r', "\\r"},   {U'\n', "\\n"},   {U'\\', "\\\\"}, {U'"', "\\\""},
		{U'\'', "\\'"},   {U'`', "\\`"},    {U'$', "\\$"}, // when quoteChar == '`'
		{0x2028, "\\u2028"},                  // lineSeparator
		{0x2029, "\\u2029"},                  // paragraphSeparator
		{0x0085, "\\u0085"},                  // nextLine
	};
	return map;
}

void encodeJsxCharacterEntity(std::string& b, char32_t charCode) {
	char buf[16];
	std::snprintf(buf, sizeof buf, "&#x%X;", static_cast<unsigned>(charCode));
	b += buf;
}

void encodeUtf16EscapeSequence(std::string& b, char32_t charCode) {
	char buf[8];
	std::snprintf(buf, sizeof buf, "\\u%04X",
	              static_cast<unsigned>(charCode) & 0xFFFFu);
	b += buf;
}

// Based heavily on the abstract 'Quote'/'QuoteJSONString' operation from
// ECMA-262 (24.3.2.2), but augmented for a few select characters (e.g.
// lineSeparator, paragraphSeparator, nextLine). Note that this doesn't
// actually wrap the input in double quotes.
void escapeStringWorker(std::string_view s, QuoteChar quoteChar,
                        getLiteralTextFlags flags, std::string& b) {
	size_t pos = 0;
	size_t i = 0;
	while (i < s.size()) {
		int size = 0;
		char32_t ch = decodeJSStringRune(s, i, &size);

		bool escape = false;
		if (ch >= 0xD800 && ch <= 0xDFFF) {
			escape = true;
		} else if (ch == kRuneError && size == 1) {
			// A stray byte that is not valid UTF-8 (for example, a fragment of
			// a surrogate sentinel left behind by code that sliced the string
			// by byte). Escape it as the Unicode replacement character so the
			// output is always well-formed rather than containing raw invalid
			// bytes.
			escape = true;
		}

		// This consists of the first 19 unprintable ASCII characters,
		// canonical escapes, lineSeparator, paragraphSeparator, and nextLine.
		// The latter three are just desirable to suppress new lines in the
		// language service. These characters should be escaped when printing,
		// and if any characters are added, `escapedCharsMap` and/or
		// `jsxEscapedCharsMap` must be updated. Note that this *does not*
		// include the 'delete' character. There is no reason for this other
		// than that JSON.stringify does not handle it either.
		switch (ch) {
			case U'\\':
				if ((flags & getLiteralTextFlagsJsxAttributeEscape) == 0) {
					escape = true;
				}
				break;
			case U'$':
				if (quoteChar == QuoteCharBacktick && i + 1 < s.size() &&
				    s[i + 1] == '{') {
					escape = true;
				}
				break;
			case 0x2028:
			case 0x2029:
			case 0x0085:
			case U'\r':
				escape = true;
				break;
			case U'\n':
				if (quoteChar != QuoteCharBacktick) {
					// Template strings preserve simple LF newlines, still
					// encode CRLF (or CR).
					escape = true;
				}
				break;
			default:
				if (ch == static_cast<char32_t>(quoteChar) ||
				    ch <= 0x1F ||
				    ((flags & getLiteralTextFlagsNeverAsciiEscape) == 0 &&
				     ch > 0x7F)) {
					escape = true;
				}
		}

		if (escape) {
			if (pos < i) {
				// Write string up to this point
				b += s.substr(pos, i - pos);
			}

			if ((flags & getLiteralTextFlagsJsxAttributeEscape) != 0) {
				if (ch == 0) {
					b += "&#0;";
				} else if (auto it = jsxEscapedCharsMap().find(ch);
				           it != jsxEscapedCharsMap().end()) {
					b += it->second;
				} else {
					encodeJsxCharacterEntity(b, ch);
				}
			} else {
				if (ch == U'\r' && quoteChar == QuoteCharBacktick &&
				    i + 1 < s.size() && s[i + 1] == '\n') {
					// Template strings preserve simple LF newlines, but still
					// must escape CRLF. Left alone, the above cases for `\r`
					// and `\n` would inadvertently escape CRLF as two
					// independent characters.
					size++;
					b += "\\r\\n";
				} else if (ch > 0xffff) {
					// encode as surrogate pair
					ch -= 0x10000;
					encodeUtf16EscapeSequence(b,
					                          ((ch & 0b11111111110000000000) >>
					                           10) +
					                              0xD800);
					encodeUtf16EscapeSequence(b,
					                          (ch & 0b00000000001111111111) +
					                              0xDC00);
				} else if (ch >= 0xD800 && ch <= 0xDFFF) {
					encodeUtf16EscapeSequence(b, ch);
				} else if (ch == 0) {
					if (i + 1 < s.size() &&
					    isDigit(
						    static_cast<char32_t>(s[i + 1]))) {
						// If the null character is followed by digits, print
						// as a hex escape to prevent the result from parsing
						// as an octal (which is forbidden in strict mode)
						b += "\\x00";
					} else {
						// Otherwise, keep printing a literal \0 for the null
						// character
						b += "\\0";
					}
				} else {
					if (auto it = escapedCharsMap().find(ch);
					    it != escapedCharsMap().end()) {
						b += it->second;
					} else {
						encodeUtf16EscapeSequence(b, ch);
					}
				}
			}
			pos = i + size;
		}

		i += size;
	}

	if (pos < i) {
		b += s.substr(pos);
	}
}

} // namespace

std::string EscapeString(std::string_view s, QuoteChar quoteChar) {
	std::string b;
	b.reserve(s.size() + 2);
	escapeStringWorker(s, quoteChar, getLiteralTextFlagsNeverAsciiEscape, b);
	return b;
}

std::string escapeNonAsciiString(std::string_view s, QuoteChar quoteChar) {
	std::string b;
	b.reserve(s.size() + 2);
	escapeStringWorker(s, quoteChar, getLiteralTextFlagsNone, b);
	return b;
}

std::string escapeJsxAttributeString(std::string_view s, QuoteChar quoteChar) {
	std::string b;
	b.reserve(s.size() + 2);
	escapeStringWorker(s, quoteChar,
	                   getLiteralTextFlagsJsxAttributeEscape |
	                       getLiteralTextFlagsNeverAsciiEscape,
	                   b);
	return b;
}

namespace {
bool canUseOriginalText(Node* node, getLiteralTextFlags flags) {
	// A synthetic node has no original text, nor does a node without a parent
	// as we would be unable to find the containing SourceFile. We also cannot
	// use the original text if the literal was unterminated and the caller has
	// requested proper termination of unterminated literals
	if (nodeIsSynthesized(node) || node->parent == nullptr ||
	    ((flags & getLiteralTextFlagsTerminateUnterminatedLiterals) != 0 &&
	     isUnterminatedLiteral(node))) {
		return false;
	}

	if (node->kind == Kind::NumericLiteral) {
		TokenFlags tokenFlags = node->as<NumericLiteral>()->TokenFlags;
		// For a numeric literal, we cannot use the original text if the
		// original text was an invalid literal
		if ((tokenFlags & TokenFlagsIsInvalid) != 0) {
			return false;
		}
		// We also cannot use the original text if the literal contains numeric
		// separators, but numeric separators are not permitted
		if ((tokenFlags & TokenFlagsContainsSeparator) != 0) {
			return (flags & getLiteralTextFlagsAllowNumericSeparator) != 0;
		}
	}

	// Finally, we do not use the original text of a BigInt literal
	// TODO(rbuckton): The reason as to why we do not use the original text for
	// bigints is not mentioned in the original compiler source. It could be
	// that this is no longer necessary, in which case bigint literals should
	// use the same code path as numeric literals, above
	return node->kind != Kind::BigIntLiteral;
}
} // namespace

std::string getLiteralText(Node* node, SourceFile* sourceFile,
                           getLiteralTextFlags flags) {
	// If we don't need to downlevel and we can reach the original source text
	// using the node's parent reference, then simply get the text as it was
	// originally written.
	if (sourceFile != nullptr && canUseOriginalText(node, flags)) {
		return getSourceTextOfNodeFromSourceFile(
			sourceFile, node, false /*includeTrivia*/);
	}

	// If we can't reach the original source text, use the canonical form if
	// it's a number, or a (possibly escaped) quoted form of the original text
	// if it's string-like.
	switch (node->kind) {
		case Kind::StringLiteral: {
			std::string b;
			QuoteChar quoteChar;
			if ((node->as<StringLiteral>()->TokenFlags &
			     TokenFlagsSingleQuote) != 0) {
				quoteChar = QuoteCharSingleQuote;
			} else {
				quoteChar = QuoteCharDoubleQuote;
			}

			std::string text = node->text();

			// Write leading quote character
			b.reserve(text.size() + 2);
			b += static_cast<char>(quoteChar);

			// Write text
			escapeStringWorker(text, quoteChar, flags, b);

			// Write trailing quote character
			b += static_cast<char>(quoteChar);
			return b;
		}

		case Kind::NoSubstitutionTemplateLiteral:
		case Kind::TemplateHead:
		case Kind::TemplateMiddle:
		case Kind::TemplateTail: {
			// If a NoSubstitutionTemplateLiteral appears to have a
			// substitution in it, the original text had to include a
			// backslash: `not \${a} substitution`.
			std::string b;
			std::string text = node->text();
			std::string_view rawText =
				*node->templateLiteralLikeData().rawText;
			bool raw = !rawText.empty() || text.empty();

			size_t textLen = raw ? rawText.size() : text.size();

			// Write leading quote character
			switch (node->kind) {
				case Kind::NoSubstitutionTemplateLiteral:
					b.reserve(2 + textLen);
					b += '`';
					break;
				case Kind::TemplateHead:
					b.reserve(3 + textLen);
					b += '`';
					break;
				case Kind::TemplateMiddle:
					b.reserve(3 + textLen);
					b += '}';
					break;
				case Kind::TemplateTail:
					b.reserve(2 + textLen);
					b += '}';
					break;
				default:
					break;
			}

			// Write text
			if (!rawText.empty() || text.empty()) {
				// If rawText is set, it is expected to be valid.
				b += rawText;
			} else {
				escapeStringWorker(text, QuoteCharBacktick, flags, b);
			}

			// Write trailing quote character
			switch (node->kind) {
				case Kind::NoSubstitutionTemplateLiteral:
					b += '`';
					break;
				case Kind::TemplateHead:
					b += "${";
					break;
				case Kind::TemplateMiddle:
					b += "${";
					break;
				case Kind::TemplateTail:
					b += '`';
					break;
				default:
					break;
			}
			return b;
		}

		case Kind::NumericLiteral:
		case Kind::BigIntLiteral:
			return std::string(node->text());

		case Kind::RegularExpressionLiteral:
			if ((flags & getLiteralTextFlagsTerminateUnterminatedLiterals) !=
			        0 &&
			    isUnterminatedLiteral(node)) {
				std::string b;
				std::string text = node->text();
				if (!text.empty() && text.back() == '\\') {
					b.reserve(2 + text.size());
					b += text;
					b += " /";
				} else {
					b.reserve(1 + text.size());
					b += text;
					b += "/";
				}
				return b;
			}
			return std::string(node->text());

		default:
			TSC_UNREACHABLE("Unsupported LiteralLikeNode");
	}
}

bool isNotPrologueDirective(Node* node) { return !isPrologueDirective(node); }

bool RangeIsOnSingleLine(TextRange r, SourceFile* sourceFile) {
	return rangeStartIsOnSameLineAsRangeEnd(r, r, sourceFile);
}

bool RangeStartPositionsAreOnSameLine(TextRange range1, TextRange range2,
                                      SourceFile* sourceFile) {
	return PositionsAreOnSameLine(
		getStartPositionOfRange(range1, sourceFile, false /*includeComments*/),
		getStartPositionOfRange(range2, sourceFile, false /*includeComments*/),
		sourceFile);
}

bool rangeEndPositionsAreOnSameLine(TextRange range1, TextRange range2,
                                    SourceFile* sourceFile) {
	return PositionsAreOnSameLine(range1.end(), range2.end(), sourceFile);
}

bool rangeStartIsOnSameLineAsRangeEnd(TextRange range1, TextRange range2,
                                      SourceFile* sourceFile) {
	return PositionsAreOnSameLine(
		getStartPositionOfRange(range1, sourceFile, false /*includeComments*/),
		range2.end(), sourceFile);
}

bool rangeEndIsOnSameLineAsRangeStart(TextRange range1, TextRange range2,
                                      SourceFile* sourceFile) {
	return PositionsAreOnSameLine(
		range1.end(),
		getStartPositionOfRange(range2, sourceFile, false /*includeComments*/),
		sourceFile);
}

int getStartPositionOfRange(TextRange r, SourceFile* sourceFile,
                            bool includeComments) {
	if (positionIsSynthesized(r.pos())) {
		return -1;
	}
	SkipTriviaOptions opts;
	opts.stopAtComments = includeComments;
	return skipTriviaEx(sourceFile->text, r.pos(), opts);
}

bool PositionsAreOnSameLine(TextPos pos1, TextPos pos2,
                            SourceFile* sourceFile) {
	return GetLinesBetweenPositions(sourceFile, pos1, pos2) == 0;
}

int GetLinesBetweenPositions(SourceFile* sourceFile, TextPos pos1,
                             TextPos pos2) {
	if (pos1 == pos2) {
		return 0;
	}
	const ECMALineStarts& lineStarts = getECMALineStarts(sourceFile);
	TextPos lower = pos1 < pos2 ? pos1 : pos2;
	bool isNegative = lower == pos2;
	TextPos upper = isNegative ? pos1 : pos2;
	int lowerLine = computeLineOfPosition(lineStarts, lower);
	std::vector<TextPos> tail(lineStarts.begin() + lowerLine, lineStarts.end());
	int upperLine = lowerLine + computeLineOfPosition(tail, upper);
	if (isNegative) {
		return lowerLine - upperLine;
	} else {
		return upperLine - lowerLine;
	}
}

int getLinesBetweenRangeEndAndRangeStart(TextRange range1, TextRange range2,
                                         SourceFile* sourceFile,
                                         bool includeSecondRangeComments) {
	int range2Start = getStartPositionOfRange(range2, sourceFile,
	                                          includeSecondRangeComments);
	return GetLinesBetweenPositions(sourceFile, range1.end(), range2Start);
}

int getLinesBetweenPositionAndPrecedingNonWhitespaceCharacter(
	TextPos pos, TextPos stopPos, SourceFile* sourceFile, bool includeComments) {
	SkipTriviaOptions opts;
	opts.stopAtComments = includeComments;
	int startPos = skipTriviaEx(sourceFile->text, pos, opts);
	TextPos prevPos =
		getPreviousNonWhitespacePosition(startPos, stopPos, sourceFile);
	return GetLinesBetweenPositions(sourceFile,
	                                prevPos >= 0 ? prevPos : stopPos, startPos);
}

int getLinesBetweenPositionAndNextNonWhitespaceCharacter(
	TextPos pos, TextPos stopPos, SourceFile* sourceFile, bool includeComments) {
	SkipTriviaOptions opts;
	opts.stopAtComments = includeComments;
	int nextPos = skipTriviaEx(sourceFile->text, pos, opts);
	return GetLinesBetweenPositions(sourceFile, pos,
	                                stopPos < nextPos ? stopPos : nextPos);
}

TextPos getPreviousNonWhitespacePosition(TextPos pos, TextPos stopPos,
                                         SourceFile* sourceFile) {
	for (; pos >= stopPos; pos--) {
		if (!isWhiteSpaceLike(
			    static_cast<char32_t>(sourceFile->text[pos]))) {
			return pos;
		}
	}
	return -1;
}

bool siblingNodePositionsAreComparable(EmitContext* emitContext,
                                       Node* previousNode, Node* nextNode) {
	if (nextNode->pos() < previousNode->end()) {
		return false;
	}

	previousNode = emitContext->mostOriginal(previousNode);
	nextNode = emitContext->mostOriginal(nextNode);
	Node* parent = previousNode->parent;
	if (parent == nullptr || parent != nextNode->parent) {
		return false;
	}

	NodeList* parentNodeArray = getContainingNodeArray(previousNode);
	if (parentNodeArray != nullptr) {
		auto it = std::find(parentNodeArray->nodes.begin(),
		                    parentNodeArray->nodes.end(), previousNode);
		if (it == parentNodeArray->nodes.end()) {
			return false;
		}
		auto next = it + 1;
		return next != parentNodeArray->nodes.end() && *next == nextNode;
	}

	return false;
}

NodeList* getContainingNodeArray(Node* node) {
	Node* parent = node->parent;
	if (parent == nullptr) {
		return nullptr;
	}

	switch (node->kind) {
		case Kind::TypeParameter:
			if (isFunctionLike(parent) || isClassLike(parent) ||
			    isInterfaceDeclaration(parent) ||
			    isTypeOrJSTypeAliasDeclaration(parent)) {
				return parent->typeParameterList();
			} else if (isInferTypeNode(parent)) {
				// infer type nodes have no associated type parameter list
			} else {
				TSC_UNREACHABLE("Unexpected TypeParameter parent");
			}
			break;
		case Kind::Parameter:
			return node->parent->functionLikeData().parameters
				? *node->parent->functionLikeData().parameters
				: nullptr;
		case Kind::TemplateLiteralTypeSpan:
			return parent->as<TemplateLiteralTypeNode>()->TemplateSpans;
		case Kind::TemplateSpan:
			return parent->as<TemplateExpression>()->TemplateSpans;
		case Kind::Decorator:
			if (canHaveDecorators(node->parent)) {
				if (ModifierList* modifiers = node->parent->modifiers()) {
					return modifiers;
				}
			}
			return nullptr;
		case Kind::HeritageClause:
			if (isClassLike(node->parent)) {
				return node->parent->classLikeData().heritageClauses
					? *node->parent->classLikeData().heritageClauses
					: nullptr;
			} else {
				return node->parent->as<InterfaceDeclaration>()->HeritageClauses;
			}
		default:
			break;
	}

	switch (parent->kind) {
		case Kind::TypeLiteral:
		case Kind::InterfaceDeclaration:
			if (isTypeElement(node)) {
				return parent->memberList();
			}
			break;
		case Kind::UnionType:
			return parent->as<UnionTypeNode>()->Types;
		case Kind::IntersectionType:
			return parent->as<IntersectionTypeNode>()->Types;
		case Kind::ArrayLiteralExpression:
		case Kind::TupleType:
		case Kind::NamedImports:
		case Kind::NamedExports:
			return parent->elementList();
		case Kind::ObjectLiteralExpression:
		case Kind::JsxAttributes:
			return parent->propertyList();
		case Kind::CallExpression: {
			auto* p = parent->as<CallExpression>();
			if (isTypeNode(node)) {
				return p->TypeArguments;
			}
			if (node != p->Expression) {
				return p->Arguments;
			}
			break;
		}
		case Kind::NewExpression: {
			auto* p = parent->as<NewExpression>();
			if (isTypeNode(node)) {
				return p->TypeArguments;
			}
			if (node != p->Expression) {
				return p->Arguments;
			}
			break;
		}
		case Kind::JsxElement:
		case Kind::JsxFragment:
			if (isJsxChild(node)) {
				return parent->children();
			}
			break;
		case Kind::JsxOpeningElement:
		case Kind::JsxSelfClosingElement:
			if (isTypeNode(node)) {
				return parent->typeArgumentList();
			}
			break;
		case Kind::Block:
		case Kind::ModuleBlock:
		case Kind::CaseClause:
		case Kind::DefaultClause:
			return parent->statementList();
		case Kind::CaseBlock:
			return parent->as<CaseBlock>()->Clauses;
		case Kind::ClassDeclaration:
		case Kind::ClassExpression:
			if (isClassElement(node)) {
				return parent->memberList();
			}
			break;
		case Kind::EnumDeclaration:
			if (isEnumMember(node)) {
				return parent->memberList();
			}
			break;
		case Kind::SourceFile:
			if (isStatement(node)) {
				return parent->statementList();
			}
			break;
		default:
			break;
	}

	if (isModifier(node)) {
		if (ModifierList* modifiers = parent->modifiers()) {
			return modifiers;
		}
	}

	return nullptr;
}

bool originalNodesHaveSameParent(EmitContext* emitContext, Node* nodeA,
                                 Node* nodeB) {
	nodeA = emitContext->mostOriginal(nodeA);
	if (nodeA->parent != nullptr) {
		// For performance, do not call `mostOriginal` for `nodeB` if `nodeA`
		// doesn't even have a parent node.
		nodeB = emitContext->mostOriginal(nodeB);
		return nodeA->parent == nodeB->parent;
	}
	return false;
}

Node* skipSynthesizedParentheses(Node* node) {
	while (node->kind == Kind::ParenthesizedExpression &&
	       nodeIsSynthesized(node)) {
		node = node->expression();
	}
	return node;
}

bool isNewExpressionWithoutArguments(Node* node) {
	return node->kind == Kind::NewExpression && node->argumentList() == nullptr;
}

bool isBinaryOperation(Node* node, Kind token) {
	node = skipPartiallyEmittedExpressions(node);
	return node->kind == Kind::BinaryExpression &&
	       node->as<BinaryExpression>()->OperatorToken->kind == token;
}

bool mixingBinaryOperatorsRequiresParentheses(Kind a, Kind b) {
	if (a == Kind::QuestionQuestionToken) {
		return b == Kind::AmpersandAmpersandToken || b == Kind::BarBarToken;
	}
	if (b == Kind::QuestionQuestionToken) {
		return a == Kind::AmpersandAmpersandToken || a == Kind::BarBarToken;
	}
	return false;
}

bool isImmediatelyInvokedFunctionExpressionOrArrowFunction(Node* node) {
	node = skipPartiallyEmittedExpressions(node);
	if (!isCallExpression(node)) {
		return false;
	}
	node = skipPartiallyEmittedExpressions(node->expression());
	return isFunctionExpression(node) || isArrowFunction(node);
}

bool hasLeadingHash(std::string_view text) {
	return !text.empty() && text[0] == '#';
}

std::string removeLeadingHash(std::string_view text) {
	if (hasLeadingHash(text)) {
		return std::string(text.substr(1));
	} else {
		return std::string(text);
	}
}

std::string ensureLeadingHash(std::string_view text) {
	if (hasLeadingHash(text)) {
		return std::string(text);
	} else {
		return "#" + std::string(text);
	}
}

std::string FormatGeneratedName(bool privateName, std::string_view prefix,
                                std::string_view base, std::string_view suffix) {
	std::string name = removeLeadingHash(prefix) + removeLeadingHash(base) +
	                   removeLeadingHash(suffix);
	if (privateName) {
		return ensureLeadingHash(name);
	}
	return name;
}

bool isASCIIWordCharacter(char32_t ch) {
	return isASCIILetter(ch) || isDigit(ch) || ch == U'_';
}

std::string makeIdentifierFromModuleName(std::string_view moduleName) {
	moduleName = tspath::getBaseFileName(moduleName);
	std::string builder;
	size_t start = 0;
	size_t pos = 0;
	while (pos < moduleName.size()) {
		char32_t ch = static_cast<unsigned char>(moduleName[pos]);
		if (pos == 0 && isDigit(ch)) {
			builder += '_';
		} else if (!isASCIIWordCharacter(ch)) {
			if (start < pos) {
				builder += moduleName.substr(start, pos - start);
			}
			builder += '_';
			start = pos + 1;
		}
		pos++;
	}
	if (start < pos) {
		builder += moduleName.substr(start, pos - start);
	}
	return builder;
}

namespace {

void skipWhiteSpaceSingleLine(std::string_view text, size_t* pos) {
	while (*pos < text.size()) {
		int size = 0;
		char32_t ch = decodeUtf8Rune(text.substr(*pos), &size);
		if (!isWhiteSpaceSingleLine(ch)) {
			break;
		}
		*pos += size;
	}
}

bool matchWhiteSpaceSingleLine(std::string_view text, size_t* pos) {
	size_t startPos = *pos;
	skipWhiteSpaceSingleLine(text, pos);
	return *pos != startPos;
}

bool matchRune(std::string_view text, size_t* pos, char32_t expected) {
	int size = 0;
	char32_t ch = decodeUtf8Rune(text.substr(*pos), &size);
	if (ch == expected) {
		*pos += size;
		return true;
	}
	return false;
}

bool matchString(std::string_view text, size_t* pos, std::string_view expected) {
	size_t textPos = *pos;
	size_t expectedPos = 0;
	while (expectedPos < expected.size()) {
		if (textPos >= text.size()) {
			return false;
		}

		int expectedSize = 0;
		char32_t expectedRune =
			decodeUtf8Rune(expected.substr(expectedPos),
		                           &expectedSize);
		if (!matchRune(text, &textPos, expectedRune)) {
			return false;
		}

		expectedPos += expectedSize;
	}

	*pos = textPos;
	return true;
}

bool matchQuotedString(std::string_view text, size_t* pos) {
	size_t textPos = *pos;
	char32_t quoteChar = 0;
	if (matchRune(text, &textPos, U'\'')) {
		quoteChar = U'\'';
	} else if (matchRune(text, &textPos, U'"')) {
		quoteChar = U'"';
	} else {
		return false;
	}
	while (textPos < text.size()) {
		int size = 0;
		char32_t ch = decodeUtf8Rune(text.substr(textPos), &size);
		textPos += size;
		if (ch == quoteChar) {
			*pos = textPos;
			return true;
		}
	}
	return false;
}

} // namespace

// /// <reference path="..." />
// /// <reference types="..." />
// /// <reference lib="..." />
// /// <reference no-default-lib="..." />
// /// <amd-dependency path="..." />
// /// <amd-module />
bool IsRecognizedTripleSlashComment(std::string_view text,
                                    CommentRange commentRange) {
	if (commentRange.kind == Kind::SingleLineCommentTrivia &&
	    commentRange.len() > 2 && text[commentRange.pos() + 1] == '/' &&
	    text[commentRange.pos() + 2] == '/') {
		text = text.substr(commentRange.pos() + 3,
		                   commentRange.end() - commentRange.pos() - 3);
		size_t pos = 0;
		skipWhiteSpaceSingleLine(text, &pos);
		if (!matchRune(text, &pos, U'<')) {
			return false;
		}
		if (matchString(text, &pos, "reference")) {
			if (!matchWhiteSpaceSingleLine(text, &pos)) {
				return false;
			}
			if (!matchString(text, &pos, "path") &&
			    !matchString(text, &pos, "types") &&
			    !matchString(text, &pos, "lib") &&
			    !matchString(text, &pos, "no-default-lib")) {
				return false;
			}
			skipWhiteSpaceSingleLine(text, &pos);
			if (!matchRune(text, &pos, U'=')) {
				return false;
			}
			skipWhiteSpaceSingleLine(text, &pos);
			if (!matchQuotedString(text, &pos)) {
				return false;
			}
		} else if (matchString(text, &pos, "amd-dependency")) {
			if (!matchWhiteSpaceSingleLine(text, &pos)) {
				return false;
			}
			if (!matchString(text, &pos, "path")) {
				return false;
			}
			skipWhiteSpaceSingleLine(text, &pos);
			if (!matchRune(text, &pos, U'=')) {
				return false;
			}
			skipWhiteSpaceSingleLine(text, &pos);
			if (!matchQuotedString(text, &pos)) {
				return false;
			}
		} else if (matchString(text, &pos, "amd-module")) {
			skipWhiteSpaceSingleLine(text, &pos);
		} else {
			return false;
		}
		size_t index = text.substr(pos).find("/>");
		return index != std::string_view::npos;
	}

	return false;
}

bool isJSDocLikeText(std::string_view text, CommentRange comment) {
	return comment.kind == Kind::MultiLineCommentTrivia && comment.len() >= 5 &&
	       text[comment.pos() + 2] == '*' && text[comment.pos() + 3] != '/';
}

bool IsPinnedComment(std::string_view text, CommentRange comment) {
	return comment.kind == Kind::MultiLineCommentTrivia && comment.len() > 5 &&
	       text[comment.pos() + 2] == '!';
}

int calculateIndent(std::string_view text, TextPos pos, TextPos end) {
	int currentLineIndent = 0;
	int indentSize = GetDefaultIndentSize();
	while (pos < end) {
		int size = 0;
		char32_t ch =
			decodeUtf8Rune(text.substr(pos), &size);
		if (!isWhiteSpaceSingleLine(ch)) {
			break;
		}
		if (ch == U'\t') {
			// Tabs = TabSize = indent size and go to next tabStop
			currentLineIndent += indentSize - (currentLineIndent % indentSize);
		} else {
			// Single space
			currentLineIndent++;
		}
		pos += size;
	}

	return currentLineIndent;
}

// --- lineCharacterCache (utilities.go:894) ------------------------------------
// cached line/character lookups for a source file, optimized for
// monotonically increasing positions (e.g., during source map emit).

lineCharacterCache::lineCharacterCache(sourcemap::Source* source)
	: lineMap(source->ECMALineMap()), text(source->Text()) {}

std::pair<int, TextPos> lineCharacterCache::getLineAndCharacter(TextPos pos) {
	int line = computeLineOfPosition(lineMap, pos);
	int lineStart = lineMap[line];
	// When pos is beyond the source text (e.g., for error-recovery tokens like
	// missing closing braces), we can't slice past the text end. Compute the
	// UTF-16 length up to EOF and add the remaining byte offset arithmetically.
	TextPos endPos = std::min(pos, static_cast<TextPos>(text.size()));
	TextPos character;
	if (hasCached && line == cachedLine && endPos >= cachedPos) {
		character = cachedChar + utf16Len(std::string_view(text).substr(
		                            cachedPos, endPos - cachedPos));
	} else {
		character = utf16Len(
			std::string_view(text).substr(lineStart, endPos - lineStart));
	}
	TextPos cachedChar_ = character;
	character += pos - endPos;
	cachedLine = line;
	cachedPos = endPos;
	cachedChar = cachedChar_;
	hasCached = true;
	return {line, character};
}

} // namespace tsc::printer
