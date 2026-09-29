// Port of tsc/internal/parser/jsdoc.go — JSDoc comment parsing.
// Arena note: Go uses nodeSliceArena/stringSliceArena for scratch slices;
// C++ uses std::vector directly (arena-sliced text is a perf TODO).

#include "internal/parser/parser.h"

#include <algorithm>

#include "internal/core/spelling.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

namespace tsc {

// parseJSDocForNodeImpl lazily parses JSDoc for a node in a TS file.
// Called on first access to Node::JSDoc() for non-JS source files.
static std::vector<Node*> parseJSDocForNodeImpl(SourceFile* sourceFile,
                                                Node* node) {
	Parser* p = getParser();
	std::vector<Node*> result;
	{
		struct PutBack {
			Parser* p;
			~PutBack() { putParser(p); }
		} putBack{p};
		p->initializeState(sourceFile->parseOptions, sourceFile->text,
		                   sourceFile->ScriptKind);
		std::vector<CommentRange> ranges =
			getJSDocCommentRanges(nullptr, node, sourceFile->text);
		if (!ranges.empty()) {
			int pos = node->pos();
			for (CommentRange comment : ranges) {
				if (Node* parsed = p->parseJSDocComment(
				        node, comment.pos(), comment.end(), pos);
				    parsed != nullptr) {
					parsed->parent = node;
					result.push_back(parsed);
					pos = parsed->end();
				}
			}
		}
	}
	return result;
}

namespace {
bool jsdocInit_ = (parseJSDocForNode = &parseJSDocForNodeImpl, true);
}

static std::vector<std::string> removeLeadingNewlines(
	std::vector<std::string> comments);
static std::vector<std::string> removeTrailingWhitespace(
	std::vector<std::string> comments);
static std::string trimEnd(std::string s);
bool isJSDocLikeText(std::string_view text);

enum class jsdocState : int32_t {
	jsdocStateBeginningOfLine = 0,
	jsdocStateSawAsterisk,
	jsdocStateSavingComments,
	jsdocStateSavingBackticks,
};


std::vector<Node*> Parser::withJSDoc(Node* node, JSDocScannerInfo info) {
	if ((info & JSDocScannerInfoHasJSDoc) == 0) {
		return {};
	}

	// For TS/TSX files, defer JSDoc parsing to first access, unless the
	// comment contains @see/@link (needed for unused-identifier checks).
	// @deprecated is detected via cheap text scan to set
	// PossiblyContainsDeprecatedTag; callers must confirm via JSDoc lookup.
	if (!isJavaScript()) {
		node->flags |= NodeFlagsHasJSDoc;
		if ((info & JSDocScannerInfoHasDeprecated) != 0) {
			node->flags |= NodeFlagsPossiblyContainsDeprecatedTag;
		}
		if ((info & JSDocScannerInfoHasSeeOrLink) == 0) {
			return {};
		}
		// Fall through to eager parse for @see/@link
	}

	std::vector<CommentRange> ranges =
		getJSDocCommentRanges(&jsdocCommentRangesSpace, node, sourceText);
	jsdocCommentRangesSpace.clear();

	// Should only be called once per node
	hasDeprecatedTag = false;
	std::vector<Node*> jsdoc;
	jsdoc.reserve(ranges.size());
	int pos = node->pos();
	for (CommentRange comment : ranges) {
		if (Node* parsed = parseJSDocComment(node, comment.pos(),
		                                     comment.end(), pos);
		    parsed != nullptr) {
			parsed->parent = node;
			jsdoc.push_back(parsed);
			pos = parsed->end();
		}
	}
	if (!jsdoc.empty()) {
		if ((node->flags & NodeFlagsHasJSDoc) == 0) {
			node->flags |= NodeFlagsHasJSDoc;
		}
		if (hasDeprecatedTag) {
			hasDeprecatedTag = false;
			node->flags |= NodeFlagsPossiblyContainsDeprecatedTag;
		}
		if (isJavaScript()) {
			reparseTags(node, jsdoc);
		}
		jsdocInfos.push_back(JSDocInfo{node, jsdoc});
		return jsdoc;
	}
	return {};
}

Node* Parser::parseJSDocTypeExpression(bool mayOmitBraces) {
	int pos = nodePos();
	bool hasBrace;
	if (mayOmitBraces) {
		hasBrace = parseOptional(Kind::OpenBraceToken);
	} else {
		hasBrace = parseExpected(Kind::OpenBraceToken);
	}
	NodeFlags saveContextFlags = contextFlags;
	setContextFlags(NodeFlagsJSDoc, true);
	Node* t = parseJSDocType();
	contextFlags = saveContextFlags;
	if (hasBrace) {
		parseExpectedJSDoc(Kind::CloseBraceToken);
	}

	return finishNode(factory.newJSDocTypeExpression(t), pos);
}

Node* Parser::parseJSDocNameReference() {
	int pos = nodePos();
	bool hasBrace = parseOptional(Kind::OpenBraceToken);
	Node* entityName = parseJSDocLinkName();
	if (hasBrace) {
		parseExpectedJSDoc(Kind::CloseBraceToken);
	}
	scanner->resetPos(scanner->tokenFullStart());
	nextTokenJSDoc();
	return finishNode(factory.newJSDocNameReference(entityName), pos);
}

// Pass end=-1 to parse the text to the end
Node* Parser::parseJSDocComment(Node* parent, int start, int end,
                                int fullStart) {
	if (end == -1) {
		end = (int)sourceText.size();
	}
	// Check for /** (JSDoc opening part)
	if (!isJSDocLikeText(sourceText.substr(start))) {
		// TODO: This should be a panic, unless parseSingleJSDocComment is
		// calling this (not ported yet)
		return nullptr;
	}

	std::string_view saveSourceText = sourceText;
	Kind saveToken = token;
	NodeFlags saveContextFlags = contextFlags;
	ParsingContexts saveParsingContexts = parsingContexts;
	ScannerState saveScannerState = scanner->mark();
	int saveDiagnosticsLength = (int)diagnostics.size();
	bool saveHasParseError = hasParseError;
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;

	// initial indent is start+4 to account for leading `/** `
	// + 1 because \n is one character before the first character in the
	// line and, if there is no \n before start, -1 is one index before the
	// first character in the string
	size_t lastNL = sourceText.substr(0, start).rfind('\n');
	int initialIndent =
		start + 4 - (lastNL == std::string_view::npos ? 0 : (int)lastNL + 1);
	// -2 for trailing `*/`
	sourceText = sourceText.substr(0, end - 2);
	scanner->setText(sourceText);
	// +3 for leading `/**`
	scanner->resetPos(start + 3);
	setContextFlags(NodeFlagsJSDoc, true);
	parsingContexts |= 1u << PCJSDocComment;

	Node* comment =
		parseJSDocCommentWorker(start, end, fullStart, initialIndent);
	// move jsdoc diagnostics to jsdocDiagnostics -- for JS files only
	if ((contextFlags & NodeFlagsJavaScriptFile) != 0) {
		jsdocDiagnostics.insert(jsdocDiagnostics.end(),
		                        diagnostics.begin() + saveDiagnosticsLength,
		                        diagnostics.end());
	}
	diagnostics.resize(saveDiagnosticsLength);

	sourceText = saveSourceText;
	scanner->setText(sourceText);
	parsingContexts = saveParsingContexts;
	contextFlags = saveContextFlags;
	scanner->rewind(saveScannerState);
	token = saveToken;
	hasParseError = saveHasParseError;
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;

	return comment;
}

/**
 * @param offset - the offset in the containing file
 * @param indent - the number of spaces to consider as the margin (applies to
 * non-first lines only)
 */
Node* Parser::parseJSDocCommentWorker(int start, int end, int fullStart,
                                      int indent) {
	// Initially we can parse out a tag.  We also have seen a starting
	// asterisk. This is so that /** * @type */ doesn't parse.
	std::vector<Node*> tags;
	int tagsPos = -1;
	int tagsEnd = -1;
	jsdocState state = jsdocState::jsdocStateSawAsterisk;
	int backtickCount = 0;
	bool inFencedCodeBlock = false;
	std::vector<Node*> commentParts;
	std::vector<std::string> comments = jsdocCommentsSpace;
	int commentsPos = -1;
	int linkEnd = start;
	int margin = -1;
	auto pushComment = [&](std::string_view text) {
		if (margin == -1) {
			margin = indent;
		}
		comments.push_back(std::string(text));
		indent += (int)text.size();
	};

	nextTokenJSDoc();
	while (parseOptionalJsdoc(Kind::WhitespaceTrivia)) {
	}
	if (parseOptionalJsdoc(Kind::NewLineTrivia)) {
		state = jsdocState::jsdocStateBeginningOfLine;
		indent = 0;
	}
	for (;;) {
		// Detect fenced code blocks by counting consecutive backtick
		// tokens. Three or more consecutive backticks toggle the fenced
		// code block state.
		if (token != Kind::BacktickToken && backtickCount > 0) {
			if (backtickCount >= 3) {
				inFencedCodeBlock = !inFencedCodeBlock;
			}
			backtickCount = 0;
		}
		bool done = false;
		switch (token) {
		case Kind::AtToken:
			if (inFencedCodeBlock || !scanner->canFollowJSDocAt()) {
				if (inFencedCodeBlock) {
					state = jsdocState::jsdocStateSavingBackticks;
				} else {
					state = jsdocState::jsdocStateSavingComments;
				}
				pushComment(scanner->tokenText());
				break;
			}
			comments = removeTrailingWhitespace(std::move(comments));
			if (commentsPos == -1) {
				commentsPos = nodePos();
			}
			{
				Node* tag = parseTag(tags, indent);
				if (tagsPos == -1) {
					tagsPos = tag->pos();
				}
				tags.push_back(tag);
				tagsEnd = tag->end();
			}
			// NOTE: According to usejsdoc.org, a tag goes to end of line,
			// except the last tag. Real-world comments may break this rule,
			// so "BeginningOfLine" will not be a real line beginning for
			// malformed examples like
			// `/** @param {string} x @returns {number} the length */`
			state = jsdocState::jsdocStateBeginningOfLine;
			margin = -1;
			break;
		case Kind::NewLineTrivia:
			comments.push_back(std::string(scanner->tokenText()));
			state = jsdocState::jsdocStateBeginningOfLine;
			indent = 0;
			break;
		case Kind::AsteriskToken: {
			std::string_view asterisk = scanner->tokenText();
			if (state == jsdocState::jsdocStateSawAsterisk) {
				// If we've already seen an asterisk, then we can no longer
				// parse a tag on this line
				state = jsdocState::jsdocStateSavingComments;
				pushComment(asterisk);
			} else {
				if (state != jsdocState::jsdocStateBeginningOfLine) {
					TSC_UNREACHABLE("state must be BeginningOfLine");
				}
				// Ignore the first asterisk on a line
				state = jsdocState::jsdocStateSawAsterisk;
				indent += (int)asterisk.size();
			}
			break;
		}
		case Kind::WhitespaceTrivia:
			if (state == jsdocState::jsdocStateSavingComments ||
			    state == jsdocState::jsdocStateSavingBackticks) {
				TSC_UNREACHABLE(
					"whitespace shouldn't come from the scanner while "
					"saving top-level comment text");
			}
			// only collect whitespace if we're already saving comments or
			// have just crossed the comment indent margin
			{
				std::string_view whitespace = scanner->tokenText();
				if (margin > -1 &&
				    indent + (int)whitespace.size() > margin) {
					int existingIndent = margin - indent;
					if (existingIndent < 0) {
						existingIndent += (int)whitespace.size();
					}
					if (existingIndent < 0) {
						existingIndent = 0;
					}
					comments.push_back(
						std::string(whitespace.substr(existingIndent)));
				}
				indent += (int)whitespace.size();
			}
			break;
		case Kind::EndOfFile:
			done = true;
			break;
		case Kind::JSDocCommentTextToken:
			if (state != jsdocState::jsdocStateSavingBackticks) {
				if (inFencedCodeBlock) {
					state = jsdocState::jsdocStateSavingBackticks;
				} else {
					state = jsdocState::jsdocStateSavingComments;
				}
			}
			pushComment(scanner->tokenValue());
			break;
		case Kind::BacktickToken:
			backtickCount++;
			if (state == jsdocState::jsdocStateSavingBackticks) {
				state = jsdocState::jsdocStateSavingComments;
			} else {
				state = jsdocState::jsdocStateSavingBackticks;
			}
			pushComment(scanner->tokenText());
			break;
		case Kind::OpenBraceToken:
			if (inFencedCodeBlock) {
				state = jsdocState::jsdocStateSavingBackticks;
				pushComment(scanner->tokenText());
				break;
			}
			state = jsdocState::jsdocStateSavingComments;
			{
				int commentEnd = scanner->tokenFullStart();
				int linkStart = scanner->tokenEnd() - 1;
				Node* link = parseJSDocLink(linkStart);
				if (link != nullptr) {
					if (linkEnd == start) {
						comments =
							removeLeadingNewlines(std::move(comments));
					}
					Node* jsdocText = finishNodeWithEnd(
						factory.newJSDocText(comments), linkEnd,
						commentEnd);
					commentParts.push_back(jsdocText);
					commentParts.push_back(link);
					comments.clear();
					linkEnd = scanner->tokenEnd();
					break;
				}
			}
			[[fallthrough]];
		default:
			// Anything else is doc comment text. We just save it. Because
			// it wasn't a tag, we can no longer parse a tag on this line
			// until we hit the next line break.
			if (state != jsdocState::jsdocStateSavingBackticks) {
				if (inFencedCodeBlock) {
					state = jsdocState::jsdocStateSavingBackticks;
				} else {
					state = jsdocState::jsdocStateSavingComments;
				}
			}
			pushComment(scanner->tokenText());
			break;
		}
		if (done) {
			break;
		}
		if (state == jsdocState::jsdocStateSavingComments ||
		    state == jsdocState::jsdocStateSavingBackticks) {
			nextJSDocCommentTextToken(state ==
			                        jsdocState::jsdocStateSavingBackticks);
		} else {
			nextTokenJSDoc();
		}
	}

	jsdocCommentsSpace.clear();  // Reuse this slice for further parses
	if (commentsPos == -1) {
		commentsPos = scanner->tokenFullStart();
	}

	if (!comments.empty()) {
		comments.back() = trimEnd(comments.back());
		Node* jsdocText = finishNodeWithEnd(
			factory.newJSDocText(comments), linkEnd, commentsPos);
		commentParts.push_back(jsdocText);
	}

	if (!commentParts.empty() && !tags.empty() && commentsPos == -1) {
		TSC_UNREACHABLE(
			"having parsed tags implies that the end of the comment span "
			"should be set");
	}

	NodeList* tagsNodeList = nullptr;
	if (tagsPos != -1) {
		tagsNodeList = newNodeList(TextRange{tagsPos, tagsEnd}, tags);
	}

	Node* jsdocComment = factory.newJSDoc(
		newNodeList(TextRange{start, commentsPos}, commentParts),
		tagsNodeList);
	return finishNodeWithEnd(jsdocComment, fullStart, end);
}

static std::vector<std::string> removeLeadingNewlines(
	std::vector<std::string> comments) {
	size_t i = 0;
	while (i < comments.size()) {
		std::string_view s = comments[i];
		size_t start = s.find_first_not_of("\r\n");
		if (start != std::string_view::npos && start > 0) {
			break;
		}
		if (start != std::string_view::npos) {
			break;
		}
		i++;
	}
	return std::vector<std::string>(comments.begin() + i, comments.end());
}

static std::string trimEnd(std::string s) {
	while (!s.empty() &&
	       isWhiteSpaceLike((char32_t)(uint8_t)s.back())) {
		s.pop_back();
	}
	return s;
}

static std::vector<std::string> removeTrailingWhitespace(
	std::vector<std::string> comments) {
	size_t end = comments.size();
	for (int i = (int)comments.size() - 1; i >= 0; i--) {
		std::string trimmed = trimEnd(comments[i]);
		if (trimmed.empty()) {
			end = i;
		} else {
			comments[i] = trimmed;
			break;
		}
	}
	comments.resize(end);
	return comments;
}

bool Parser::isNextNonwhitespaceTokenEndOfFile() {
	// We must use infinite lookahead, as there could be any number of
	// newlines :(
	for (;;) {
		nextTokenJSDoc();
		if (token == Kind::EndOfFile) {
			return true;
		}
		if (!(token == Kind::WhitespaceTrivia ||
		      token == Kind::NewLineTrivia)) {
			return false;
		}
	}
}

void Parser::skipWhitespace() {
	if (token == Kind::WhitespaceTrivia || token == Kind::NewLineTrivia) {
		if (lookAhead(&Parser::isNextNonwhitespaceTokenEndOfFile)) {
			return;
			// Don't skip whitespace prior to EoF (or end of comment) - that
			// shouldn't be included in any node's range
		}
	}
	while (token == Kind::WhitespaceTrivia ||
	       token == Kind::NewLineTrivia) {
		nextTokenJSDoc();
	}
}

std::string Parser::skipWhitespaceOrAsterisk() {
	if (token == Kind::WhitespaceTrivia || token == Kind::NewLineTrivia) {
		if (lookAhead(&Parser::isNextNonwhitespaceTokenEndOfFile)) {
			return "";
			// Don't skip whitespace prior to EoF (or end of comment) - that
			// shouldn't be included in any node's range
		}
	}

	bool precedingLineBreak = hasPrecedingLineBreak();
	bool seenLineBreak = false;
	std::vector<std::string> indents;
	while ((precedingLineBreak && token == Kind::AsteriskToken) ||
	       token == Kind::WhitespaceTrivia ||
	       token == Kind::NewLineTrivia) {
		indents.push_back(std::string(scanner->tokenText()));
		if (token == Kind::NewLineTrivia) {
			precedingLineBreak = true;
			seenLineBreak = true;
			indents.clear();
		} else if (token == Kind::AsteriskToken) {
			precedingLineBreak = false;
		}
		nextTokenJSDoc();
	}
	if (seenLineBreak) {
		std::string result;
		for (auto& s : indents) {
			result += s;
		}
		return result;
	}
	return "";
}

Node* Parser::parseTag(std::vector<Node*> tags, int margin) {
	if (token != Kind::AtToken) {
		TSC_UNREACHABLE("should be called only at the start of a tag");
	}
	int start = scanner->tokenStart();
	nextTokenJSDoc();

	Node* tagName = parseJSDocIdentifierName(Identifier_expected);
	std::string indentText = skipWhitespaceOrAsterisk();

	Node* tag = nullptr;
	std::string_view name = tagName->text();
	if (name == "implements") {
		tag = parseImplementsTag(start, tagName, margin, indentText);
	} else if (name == "augments" || name == "extends") {
		tag = parseAugmentsTag(start, tagName, margin, indentText);
	} else if (name == "public") {
		tag = parseSimpleTag(
			start,
			[this](Node* tagName, NodeList* comments) -> Node* {
				return factory.newJSDocPublicTag(tagName, comments);
			},
			tagName, margin, indentText);
	} else if (name == "private") {
		tag = parseSimpleTag(
			start,
			[this](Node* tagName, NodeList* comments) -> Node* {
				return factory.newJSDocPrivateTag(tagName, comments);
			},
			tagName, margin, indentText);
	} else if (name == "protected") {
		tag = parseSimpleTag(
			start,
			[this](Node* tagName, NodeList* comments) -> Node* {
				return factory.newJSDocProtectedTag(tagName, comments);
			},
			tagName, margin, indentText);
	} else if (name == "readonly") {
		tag = parseSimpleTag(
			start,
			[this](Node* tagName, NodeList* comments) -> Node* {
				return factory.newJSDocReadonlyTag(tagName, comments);
			},
			tagName, margin, indentText);
	} else if (name == "override") {
		tag = parseSimpleTag(
			start,
			[this](Node* tagName, NodeList* comments) -> Node* {
				return factory.newJSDocOverrideTag(tagName, comments);
			},
			tagName, margin, indentText);
	} else if (name == "deprecated") {
		hasDeprecatedTag = true;
		tag = parseSimpleTag(
			start,
			[this](Node* tagName, NodeList* comments) -> Node* {
				return factory.newJSDocDeprecatedTag(tagName, comments);
			},
			tagName, margin, indentText);
	} else if (name == "this") {
		tag = parseThisTag(start, tagName, margin, indentText);
	} else if (name == "arg" || name == "argument" || name == "param") {
		tag = parseParameterOrPropertyTag(start, tagName,
		                                  PropertyLikeParseParameter, margin);
	} else if (name == "return" || name == "returns") {
		tag = parseReturnTag(tags, start, tagName, margin, indentText);
	} else if (name == "template") {
		tag = parseTemplateTag(start, tagName, margin, indentText);
	} else if (name == "type") {
		tag = parseTypeTag(tags, start, tagName, margin, indentText);
	} else if (name == "typedef") {
		tag = parseTypedefTag(start, tagName, margin, indentText);
	} else if (name == "callback") {
		tag = parseCallbackTag(start, tagName, margin, indentText);
	} else if (name == "overload") {
		tag = parseOverloadTag(start, tagName, margin, indentText);
	} else if (name == "satisfies") {
		tag = parseSatisfiesTag(start, tagName, margin, indentText);
	} else if (name == "see") {
		tag = parseSeeTag(start, tagName, margin, indentText);
	} else if (name == "exception" || name == "throws") {
		tag = parseThrowsTag(start, tagName, margin, indentText);
	} else if (name == "import") {
		tag = parseImportTag(start, tagName, margin, indentText);
	} else {
		tag = parseUnknownTag(start, tagName, margin, indentText);
	}
	if (tag == nullptr) {
		TSC_UNREACHABLE("tag should not be nil");
	}
	return tag;
}

NodeList* Parser::parseTrailingTagComments(int pos, int end, int margin,
                                           std::string_view indentText) {
	// some tags, like typedef and callback, have already parsed their
	// comments earlier
	if (indentText.empty()) {
		margin += end - pos;
	}
	std::string initialMargin;
	if (margin < (int)indentText.size()) {
		initialMargin = std::string(indentText.substr(margin));
	}
	return parseTagComments(margin, std::move(initialMargin));
}

NodeList* Parser::parseTagComments(int indent, std::optional<std::string> initialMargin) {
	int commentsPos = nodePos();
	std::vector<std::string> comments = jsdocTagCommentsSpace;
	jsdocTagCommentsSpace.clear();  // !!! can parseTagComments call itself?
	std::vector<Node*> parts = jsdocTagCommentsPartsSpace;
	jsdocTagCommentsPartsSpace.clear();
	int linkEnd = -1;
	jsdocState state = jsdocState::jsdocStateBeginningOfLine;
	int backtickCount = 0;
	bool inFencedCodeBlock = false;
	if (indent < 0) {
		TSC_UNREACHABLE("indent must be a natural number");
	}
	int margin = -1;
	auto pushComment = [&](std::string_view text) {
		if (margin == -1) {
			margin = indent;
		}
		comments.push_back(std::string(text));
		indent += (int)text.size();
	};

	if (initialMargin.has_value()) {
		// jump straight to saving comments if there is some initial
		// indentation
		if (!initialMargin->empty()) {
			pushComment(*initialMargin);
		}
		state = jsdocState::jsdocStateSawAsterisk;
	}
	Kind tok = token;
	for (;;) {
		// Detect fenced code blocks by counting consecutive backtick
		// tokens. Three or more consecutive backticks toggle the fenced
		// code block state.
		if (tok != Kind::BacktickToken && backtickCount > 0) {
			if (backtickCount >= 3) {
				inFencedCodeBlock = !inFencedCodeBlock;
			}
			backtickCount = 0;
		}
		bool done = false;
		switch (tok) {
		case Kind::NewLineTrivia:
			state = jsdocState::jsdocStateBeginningOfLine;
			// don't use pushComment here because we want to keep the
			// margin unchanged
			comments.push_back(std::string(scanner->tokenText()));
			indent = 0;
			break;
		case Kind::AtToken:
			if (!inFencedCodeBlock && scanner->canFollowJSDocAt()) {
				scanner->resetPos(scanner->tokenEnd() - 1);
				done = true;
				break;
			}
			if (inFencedCodeBlock) {
				state = jsdocState::jsdocStateSavingBackticks;
			} else {
				state = jsdocState::jsdocStateSavingComments;
			}
			pushComment(scanner->tokenText());
			break;
		case Kind::EndOfFile:
			// Done
			done = true;
			break;
		case Kind::WhitespaceTrivia:
			if (state == jsdocState::jsdocStateSavingComments ||
			    state == jsdocState::jsdocStateSavingBackticks) {
				TSC_UNREACHABLE(
					"whitespace shouldn't come from the scanner while "
					"saving comment text");
			}
			{
				std::string_view whitespace = scanner->tokenText();
				// if the whitespace crosses the margin, take only the
				// whitespace that passes the margin
				if (margin > -1 &&
				    indent + (int)whitespace.size() > margin) {
					comments.push_back(std::string(whitespace.substr(
						std::max(margin - indent, 0))));
					if (inFencedCodeBlock) {
						state = jsdocState::jsdocStateSavingBackticks;
					} else {
						state = jsdocState::jsdocStateSavingComments;
					}
				}
				indent += (int)whitespace.size();
			}
			break;
		case Kind::OpenBraceToken:
			if (inFencedCodeBlock) {
				state = jsdocState::jsdocStateSavingBackticks;
				pushComment(scanner->tokenText());
				break;
			}
			state = jsdocState::jsdocStateSavingComments;
			{
				int commentEnd = scanner->tokenFullStart();
				int linkStart = scanner->tokenEnd() - 1;
				Node* link = parseJSDocLink(linkStart);
				if (link != nullptr) {
					int commentStart =
						linkEnd > -1 ? linkEnd : commentsPos;
					Node* text = finishNodeWithEnd(
						factory.newJSDocText(comments), commentStart,
						commentEnd);
					parts.push_back(text);
					parts.push_back(link);
					comments.clear();
					linkEnd = scanner->tokenEnd();
				} else {
					pushComment(scanner->tokenText());
				}
			}
			break;
		case Kind::BacktickToken:
			backtickCount++;
			if (state == jsdocState::jsdocStateSavingBackticks) {
				state = jsdocState::jsdocStateSavingComments;
			} else {
				state = jsdocState::jsdocStateSavingBackticks;
			}
			pushComment(scanner->tokenText());
			break;
		case Kind::JSDocCommentTextToken:
			if (state != jsdocState::jsdocStateSavingBackticks) {
				if (inFencedCodeBlock) {
					state = jsdocState::jsdocStateSavingBackticks;
				} else {
					state = jsdocState::jsdocStateSavingComments;
				}
				// leading identifiers start recording as well
			}
			pushComment(scanner->tokenValue());
			break;
		case Kind::AsteriskToken:
			if (state == jsdocState::jsdocStateBeginningOfLine) {
				// leading asterisks start recording on the *next*
				// (non-whitespace) token
				state = jsdocState::jsdocStateSawAsterisk;
				indent += 1;
				break;
			}
			// record the * as a comment
			[[fallthrough]];
		default:
			if (state != jsdocState::jsdocStateSavingBackticks) {
				if (inFencedCodeBlock) {
					state = jsdocState::jsdocStateSavingBackticks;
				} else {
					state = jsdocState::jsdocStateSavingComments;
				}
				// leading identifiers start recording as well
			}
			pushComment(scanner->tokenText());
			break;
		}
		if (done) {
			break;
		}
		if (state == jsdocState::jsdocStateSavingComments ||
		    state == jsdocState::jsdocStateSavingBackticks) {
			tok = nextJSDocCommentTextToken(
				state == jsdocState::jsdocStateSavingBackticks);
		} else {
			tok = nextTokenJSDoc();
		}
	}

	jsdocTagCommentsSpace.clear();

	comments = removeLeadingNewlines(std::move(comments));
	comments = removeTrailingWhitespace(std::move(comments));
	if (!comments.empty()) {
		int commentStart = linkEnd > -1 ? linkEnd : commentsPos;
		Node* text =
			finishNode(factory.newJSDocText(comments), commentStart);
		parts.push_back(text);
	}

	jsdocTagCommentsPartsSpace.clear();

	if (!parts.empty()) {
		return newNodeList(TextRange{commentsPos, scanner->tokenEnd()},
		                   parts);
	}
	return nullptr;
}

Node* Parser::parseJSDocLink(int start) {
	ParserState state = mark();
	auto [linkType, ok] = parseJSDocLinkPrefix();
	if (!ok) {
		rewind(state);
		return nullptr;
	}
	nextTokenJSDoc();
	// start at token after link, then skip any whitespace
	skipWhitespace();
	Node* name = parseJSDocLinkName();
	std::vector<std::string> text;
	while (token != Kind::CloseBraceToken &&
	       token != Kind::NewLineTrivia && token != Kind::EndOfFile) {
		text.push_back(std::string(scanner->tokenText()));
		nextTokenJSDoc();  // Couldn't this be nextTokenCommentJSDoc?
	}
	Node* create;
	if (linkType == "link") {
		create = factory.newJSDocLink(name, std::move(text));
	} else if (linkType == "linkcode") {
		create = factory.newJSDocLinkCode(name, std::move(text));
	} else {
		create = factory.newJSDocLinkPlain(name, std::move(text));
	}
	return finishNodeWithEnd(create, start, scanner->tokenEnd());
}

Node* Parser::parseJSDocLinkName() {
	if (tokenIsIdentifierOrKeyword(token)) {
		int pos = nodePos();
		Node* name = parseIdentifierName();
		while (parseOptional(Kind::DotToken)) {
			Node* right;
			if (token == Kind::PrivateIdentifier) {
				right = createMissingIdentifier();
			} else {
				right = parseIdentifierName();
			}
			name = finishNode(factory.newQualifiedName(name, right), pos);
		}
		while (token == Kind::PrivateIdentifier) {
			scanner->reScanHashToken();
			nextTokenJSDoc();
			name = finishNode(
				factory.newQualifiedName(name, parseIdentifier()), pos);
		}
		return name;
	}
	return nullptr;
}

std::pair<std::string, bool> Parser::parseJSDocLinkPrefix() {
	skipWhitespaceOrAsterisk();
	if (token == Kind::OpenBraceToken &&
	    nextTokenJSDoc() == Kind::AtToken &&
	    tokenIsIdentifierOrKeyword(nextTokenJSDoc())) {
		std::string kind = std::string(scanner->tokenValue());
		if (kind == "link" || kind == "linkcode" || kind == "linkplain") {
			return {kind, true};
		}
	}
	return {"NONE", false};
}

Node* Parser::parseUnknownTag(int start, Node* tagName, int indent,
                              std::string indentText) {
	return finishNode(
		factory.newJSDocUnknownTag(
			tagName, parseTrailingTagComments(start, nodePos(), indent,
		                                      std::move(indentText))),
		start);
}

Node* Parser::tryParseTypeExpression() {
	skipWhitespaceOrAsterisk();
	if (token == Kind::OpenBraceToken) {
		return parseJSDocTypeExpression(false);
	} else {
		return nullptr;
	}
}

std::pair<Node*, bool> Parser::parseBracketNameInPropertyAndParamTag(
	PropertyLikeParse target) {
	// Looking for something like '[foo]', 'foo', '[foo.bar]' or 'foo.bar'
	bool isBracketed = parseOptionalJsdoc(Kind::OpenBracketToken);
	if (isBracketed) {
		skipWhitespace();
	}
	// a markdown-quoted name: `arg` is not legal jsdoc, but occurs in the
	// wild
	bool isBackquoted = parseOptionalJsdoc(Kind::BacktickToken);
	Node* name = parseJSDocEntityName(
		target == PropertyLikeParseParameter ? nullptr
		                                     : Identifier_expected);
	if (isBackquoted) {
		parseExpectedTokenJSDoc(Kind::BacktickToken);
	}
	if (isBracketed) {
		skipWhitespace();
		// May have an optional default, e.g. '[foo = 42]'
		if (parseOptionalToken(Kind::EqualsToken) != nullptr) {
			parseExpression();
		}

		parseExpected(Kind::CloseBracketToken);
	}

	return {name, isBracketed};
}

static bool isObjectOrObjectArrayTypeReference(Node* node) {
	switch (node->kind) {
	case Kind::ObjectKeyword:
		return true;
	case Kind::ArrayType:
		return isObjectOrObjectArrayTypeReference(
			node->as<ArrayTypeNode>()->ElementType);
	default:
		if (isTypeReferenceNode(node)) {
			auto* ref = node->as<TypeReferenceNode>();
			return isIdentifier(ref->TypeName) &&
			       ref->TypeName->text() == "Object" &&
			       ref->TypeArguments == nullptr;
		}
		return false;
	}
}

Node* Parser::parseParameterOrPropertyTag(int start, Node* tagName,
                                          PropertyLikeParse target,
                                          int indent) {
	Node* typeExpression = tryParseTypeExpression();
	bool isNameFirst = typeExpression == nullptr;
	skipWhitespaceOrAsterisk();

	auto [name, isBracketed] =
		parseBracketNameInPropertyAndParamTag(target);
	std::string indentText = skipWhitespaceOrAsterisk();

	if (isNameFirst &&
	    lookAhead([](Parser* p) -> bool {
		    return !p->parseJSDocLinkPrefix().second;
	    })) {
		typeExpression = tryParseTypeExpression();
	}

	NodeList* comment = parseTrailingTagComments(start, nodePos(), indent,
	                                           indentText);

	Node* nestedTypeLiteral =
		parseNestedTypeLiteral(typeExpression, name, target, indent);
	if (nestedTypeLiteral != nullptr) {
		typeExpression = nestedTypeLiteral;
		isNameFirst = true;
	}
	Node* result = factory.newJSDocParameterOrPropertyTag(
		target == PropertyLikeParseProperty ? Kind::JSDocPropertyTag
		                                    : Kind::JSDocParameterTag,
		tagName, name, isBracketed, typeExpression, isNameFirst, comment);
	return finishNode(result, start);
}

Node* Parser::parseNestedTypeLiteral(Node* typeExpression, Node* name,
                                     PropertyLikeParse target, int indent) {
	if (typeExpression != nullptr &&
	    isObjectOrObjectArrayTypeReference(typeExpression->type())) {
		int pos = nodePos();
		std::vector<Node*> children;
		for (;;) {
			ParserState state = mark();
			Node* child =
				parseChildParameterOrPropertyTag(target, indent, name);
			if (child == nullptr) {
				rewind(state);
				break;
			}
			switch (child->kind) {
			case Kind::JSDocParameterTag:
			case Kind::JSDocPropertyTag:
				children.push_back(child);
				break;
			case Kind::JSDocTemplateTag:
				parseErrorAtRange(
					child->tagName()->loc,
					A_JSDoc_template_tag_may_not_follow_a_typedef_callback_or_overload_tag);
				break;
			default:
				break;
			}
		}
		if (!children.empty()) {
			Node* literal = finishNode(
				factory.newJSDocTypeLiteral(
					children, typeExpression->type()->kind ==
					              Kind::ArrayType),
				pos);
			return finishNode(factory.newJSDocTypeExpression(literal),
			                  pos);
		}
	}
	return nullptr;
}

Node* Parser::parseReturnTag(std::vector<Node*> previousTags, int start,
                             Node* tagName, int indent,
                             std::string indentText) {
	if (std::any_of(previousTags.begin(), previousTags.end(),
	                isJSDocReturnTag)) {
		parseErrorAt(tagName->pos(), scanner->tokenStart(),
		             X_0_tag_already_specified,
		             {std::string(tagName->text())});
	}

	Node* typeExpression = tryParseTypeExpression();
	return finishNode(
		factory.newJSDocReturnTag(
			tagName, typeExpression,
			parseTrailingTagComments(start, nodePos(), indent,
		                         std::move(indentText))),
		start);
}

// pass indent=-1 to skip parsing trailing comments (as when a type tag is
// nested in a typedef)
Node* Parser::parseTypeTag(std::vector<Node*> previousTags, int start,
                           Node* tagName, int indent,
                           std::string indentText) {
	if (std::any_of(previousTags.begin(), previousTags.end(),
	                isJSDocTypeTag)) {
		parseErrorAt(tagName->pos(), scanner->tokenStart(),
		             X_0_tag_already_specified,
		             {std::string(tagName->text())});
	}

	Node* typeExpression = parseJSDocTypeExpression(true);
	NodeList* comments = nullptr;
	if (indent != -1) {
		comments = parseTrailingTagComments(start, nodePos(), indent,
		                                    std::move(indentText));
	}
	return finishNode(
		factory.newJSDocTypeTag(tagName, typeExpression, comments), start);
}

Node* Parser::parseSeeTag(int start, Node* tagName, int indent,
                          std::string indentText) {
	bool hasNameReference =
		(isIdentifier() &&
	     !sourceText.substr(scanner->tokenEnd()).starts_with("://")) ||
		(token == Kind::OpenBraceToken &&
		 lookAhead(&Parser::nextTokenIsIdentifierOrKeyword));
	Node* nameExpression = nullptr;
	if (hasNameReference) {
		nameExpression = parseJSDocNameReference();
	}
	NodeList* comments = parseTrailingTagComments(start, nodePos(), indent,
	                                            std::move(indentText));
	return finishNode(
		factory.newJSDocSeeTag(tagName, nameExpression, comments), start);
}

Node* Parser::parseImplementsTag(int start, Node* tagName, int margin,
                                 std::string indentText) {
	Node* className = parseExpressionWithTypeArgumentsForAugments();
	return finishNode(
		factory.newJSDocImplementsTag(
			tagName, className,
			parseTrailingTagComments(start, nodePos(), margin,
		                         std::move(indentText))),
		start);
}

Node* Parser::parseAugmentsTag(int start, Node* tagName, int margin,
                               std::string indentText) {
	Node* className = parseExpressionWithTypeArgumentsForAugments();
	return finishNode(
		factory.newJSDocAugmentsTag(
			tagName, className,
			parseTrailingTagComments(start, nodePos(), margin,
		                         std::move(indentText))),
		start);
}

Node* Parser::parseSatisfiesTag(int start, Node* tagName, int margin,
                                std::string indentText) {
	Node* typeExpression = parseJSDocTypeExpression(false);
	NodeList* comments = parseTrailingTagComments(start, nodePos(), margin,
	                                            std::move(indentText));
	return finishNode(
		factory.newJSDocSatisfiesTag(tagName, typeExpression, comments),
		start);
}

Node* Parser::parseThrowsTag(int start, Node* tagName, int margin,
                             std::string indentText) {
	Node* typeExpression = tryParseTypeExpression();
	NodeList* comment = parseTrailingTagComments(start, nodePos(), margin,
	                                           std::move(indentText));
	return finishNode(
		factory.newJSDocThrowsTag(tagName, typeExpression, comment), start);
}

Node* Parser::parseImportTag(int start, Node* tagName, int margin,
                             std::string indentText) {
	int afterImportTagPos = scanner->tokenFullStart();

	Node* identifier = nullptr;
	if (isIdentifier()) {
		identifier = parseIdentifier();
	}

	Node* importClause = tryParseImportClause(identifier, afterImportTagPos,
	                                        Kind::TypeKeyword, true);
	Node* moduleSpecifier = parseModuleSpecifier();
	Node* attributes = tryParseImportAttributes();

	NodeList* comments = parseTrailingTagComments(start, nodePos(), margin,
	                                            std::move(indentText));
	return finishNode(
		factory.newJSDocImportTag(tagName, importClause, moduleSpecifier,
		                          attributes, comments),
		start);
}

Node* Parser::parseExpressionWithTypeArgumentsForAugments() {
	bool usedBrace = parseOptional(Kind::OpenBraceToken);
	int pos = nodePos();
	Node* expression = parsePropertyAccessEntityNameExpression();
	scanner->setSkipJSDocLeadingAsterisks(true);
	NodeList* typeArguments = parseTypeArguments();
	scanner->setSkipJSDocLeadingAsterisks(false);
	Node* node = finishNode(
		factory.newExpressionWithTypeArguments(expression, typeArguments),
		pos);
	if (usedBrace) {
		skipWhitespace();
		parseExpected(Kind::CloseBraceToken);
	}
	return node;
}

Node* Parser::parsePropertyAccessEntityNameExpression() {
	int pos = nodePos();
	Node* node = parseJSDocIdentifierName(Identifier_expected);
	while (parseOptional(Kind::DotToken)) {
		Node* name = parseJSDocIdentifierName(Identifier_expected);
		node = finishNode(factory.newPropertyAccessExpression(
			                  node, nullptr, name, NodeFlagsNone),
		                  pos);
	}
	return node;
}

Node* Parser::parseSimpleTag(
	int start,
	const std::function<Node*(Node* tagName, NodeList* comments)>& createTag,
	Node* tagName, int margin, std::string indentText) {
	return finishNode(
		createTag(tagName, parseTrailingTagComments(start, nodePos(), margin,
		                                          std::move(indentText))),
		start);
}

Node* Parser::parseThisTag(int start, Node* tagName, int margin,
                           std::string indentText) {
	Node* typeExpression = parseJSDocTypeExpression(true);
	skipWhitespace();
	Node* result = factory.newJSDocThisTag(
		tagName, typeExpression,
		parseTrailingTagComments(start, nodePos(), margin,
		                         std::move(indentText)));
	return finishNode(result, start);
}

Node* Parser::parseJSDocTypeNameWithNamespace(bool nested) {
	int start = scanner->tokenStart();
	if (!tokenIsIdentifierOrKeyword(token)) {
		return nullptr;
	}
	Node* typeNameOrNamespaceName = parseJSDocIdentifierName(nullptr);
	if (parseOptionalJsdoc(Kind::DotToken)) {
		Node* body = parseJSDocTypeNameWithNamespace(true);
		Node* jsDocNamespaceNode = factory.newModuleDeclaration(
			nullptr,                   /*modifiers*/
			Kind::NamespaceKeyword,    /*keyword*/
			typeNameOrNamespaceName,
			nullptr,                   /*attributes*/
			body);
		if (nested) {
			jsDocNamespaceNode->flags |= NodeFlagsNestedNamespace;
		}
		return finishNode(jsDocNamespaceNode, start);
	}
	if (nested) {
		typeNameOrNamespaceName->flags |= NodeFlagsIdentifierIsInJSDocNamespace;
	}
	return typeNameOrNamespaceName;
}

Node* Parser::parseTypedefTag(int start, Node* tagName, int indent,
                              std::string indentText) {
	Node* typeExpression = tryParseTypeExpression();
	skipWhitespaceOrAsterisk();
	Node* fullName = parseJSDocTypeNameWithNamespace(false);
	if (fullName == nullptr) {
		fullName = parseJSDocIdentifierName(Identifier_expected);
	}
	skipWhitespace();
	NodeList* comment = parseTagComments(indent, std::nullopt);

	int end = -1;
	bool hasChildren = false;
	if (typeExpression == nullptr ||
	    isObjectOrObjectArrayTypeReference(typeExpression->type())) {
		Node* child;
		Node* childTypeTag = nullptr;
		std::vector<Node*> jsdocPropertyTags;
		for (;;) {
			ParserState state = mark();
			child = parseChildPropertyTag(indent);
			if (child == nullptr) {
				rewind(state);
				break;
			}
			hasChildren = true;
			switch (child->kind) {
			case Kind::JSDocTemplateTag:
				parseErrorAtRange(
					child->tagName()->loc,
					A_JSDoc_template_tag_may_not_follow_a_typedef_callback_or_overload_tag);
				break;
			case Kind::JSDocTypeTag:
				if (childTypeTag == nullptr) {
					childTypeTag = child;
				} else {
					Diagnostic* lastError = parseErrorAtCurrentToken(
						A_JSDoc_typedef_comment_may_not_contain_multiple_type_tags);
					if (lastError != nullptr) {
						lastError->messageChain.push_back(
							newDetachedDiagnostic(
								TextRange{0, 0},
								The_tag_was_first_specified_here));
					}
				}
				break;
			default:
				jsdocPropertyTags.push_back(child);
				break;
			}
		}
		if (hasChildren) {
			bool isArrayType =
				typeExpression != nullptr &&
				typeExpression->type()->kind == Kind::ArrayType;
			Node* jsdocTypeLiteral = factory.newJSDocTypeLiteral(
				jsdocPropertyTags, isArrayType);
			if (childTypeTag != nullptr &&
			    childTypeTag->as<JSDocTypeTag>()->TypeExpression !=
			        nullptr &&
			    !isObjectOrObjectArrayTypeReference(
			        childTypeTag->as<JSDocTypeTag>()
			            ->TypeExpression->type())) {
				typeExpression =
					childTypeTag->as<JSDocTypeTag>()->TypeExpression;
			} else {
				// !!! This differs from Strada but prevents a crash
				int pos = start;
				if (!jsdocPropertyTags.empty()) {
					pos = jsdocPropertyTags[0]->pos();
				}
				typeExpression = finishNode(jsdocTypeLiteral, pos);
			}
			end = typeExpression->end();
		}
	}

	// Only include the characters between the name end and the next token
	// if a comment was actually parsed out - otherwise it's just whitespace
	if (end == -1) {
		if (hasChildren && typeExpression != nullptr) {
			end = typeExpression->end();
		} else if (comment != nullptr) {
			end = nodePos();
		} else if (fullName != nullptr) {
			end = fullName->end();
		} else if (typeExpression != nullptr) {
			end = typeExpression->end();
		} else {
			end = tagName->end();
		}
	}

	if (comment == nullptr) {
		comment = parseTrailingTagComments(start, end, indent, indentText);
	}

	Node* typedefTag = finishNodeWithEnd(
		factory.newJSDocTypedefTag(tagName, typeExpression, fullName,
		                           comment),
		start, end);
	if (typeExpression != nullptr) {
		typeExpression->parent = typedefTag;  // forcibly overwrite parent
		                                      // potentially set by inner
		                                      // type expression parse
	}
	return typedefTag;
}

NodeList* Parser::parseCallbackTagParameters(int indent) {
	std::vector<Node*> parameters;
	int pos = nodePos();
	for (;;) {
		ParserState state = mark();
		Node* child = parseChildParameterOrPropertyTag(
			PropertyLikeParseCallbackParameter, indent, nullptr);
		if (child == nullptr) {
			rewind(state);
			break;
		}
		if (child->kind == Kind::JSDocTemplateTag) {
			parseErrorAtRange(
				child->tagName()->loc,
				A_JSDoc_template_tag_may_not_follow_a_typedef_callback_or_overload_tag);
		} else {
			parameters.push_back(child);
		}
	}
	return newNodeList(TextRange{pos, nodePos()}, parameters);
}

Node* Parser::parseJSDocSignature(int start, int indent) {
	NodeList* parameters = parseCallbackTagParameters(indent);
	Node* returnTag = nullptr;
	ParserState state = mark();
	if (parseOptionalJsdoc(Kind::AtToken)) {
		Node* tag = parseTag({}, indent);
		if (tag->kind == Kind::JSDocReturnTag) {
			returnTag = tag;
		}
	}
	if (returnTag == nullptr) {
		rewind(state);
	}
	return finishNode(factory.newJSDocSignature(nullptr, parameters, returnTag),
	                  start);
}

Node* Parser::parseCallbackTag(int start, Node* tagName, int indent,
                               std::string indentText) {
	Node* fullName = parseJSDocTypeNameWithNamespace(false);
	if (fullName == nullptr) {
		fullName = parseJSDocIdentifierName(Identifier_expected);
	}
	skipWhitespace();
	NodeList* comment = parseTagComments(indent, std::nullopt);
	Node* typeExpression = parseJSDocSignature(nodePos(), indent);
	if (comment == nullptr) {
		comment = parseTrailingTagComments(start, nodePos(), indent,
		                                   std::move(indentText));
	}
	int end;
	if (comment != nullptr) {
		end = nodePos();
	} else {
		end = typeExpression->end();
	}
	return finishNodeWithEnd(
		factory.newJSDocCallbackTag(tagName, typeExpression, fullName,
		                            comment),
		start, end);
}

Node* Parser::parseOverloadTag(int start, Node* tagName, int indent,
                               std::string indentText) {
	skipWhitespace();
	NodeList* comment = parseTagComments(indent, std::nullopt);
	Node* typeExpression = parseJSDocSignature(start, indent);
	if (comment == nullptr) {
		comment = parseTrailingTagComments(start, nodePos(), indent,
		                                   std::move(indentText));
	}
	int end;
	if (comment != nullptr) {
		end = nodePos();
	} else {
		end = typeExpression->end();
	}
	return finishNodeWithEnd(
		factory.newJSDocOverloadTag(tagName, typeExpression, comment), start,
		end);
}

static bool textsEqual(Node* a, Node* b) {
	while (!::tsc::isIdentifier(a) || !::tsc::isIdentifier(b)) {
		if (!::tsc::isIdentifier(a) && !::tsc::isIdentifier(b) &&
		    a->as<QualifiedName>()->Right->text() ==
		        b->as<QualifiedName>()->Right->text()) {
			a = a->as<QualifiedName>()->Left;
			b = b->as<QualifiedName>()->Left;
		} else {
			return false;
		}
	}
	return a->text() == b->text();
}

Node* Parser::parseChildPropertyTag(int indent) {
	return parseChildParameterOrPropertyTag(PropertyLikeParseProperty, indent,
	                                        nullptr);
}

Node* Parser::parseChildParameterOrPropertyTag(PropertyLikeParse target,
                                               int indent, Node* name) {
	bool canParseTag = true;
	bool seenAsterisk = false;
	for (;;) {
		switch (nextTokenJSDoc()) {
		case Kind::AtToken:
			if (canParseTag && scanner->canFollowJSDocAt()) {
				Node* child = tryParseChildTag(target, indent);
				if (child != nullptr && name != nullptr &&
				    (child->kind == Kind::JSDocParameterTag ||
				     child->kind == Kind::JSDocPropertyTag) &&
				    (::tsc::isIdentifier(child->name()) ||
				     !textsEqual(name,
				                 child->name()->as<QualifiedName>()->Left))) {
					return nullptr;
				}
				return child;
			}
			seenAsterisk = false;
			break;
		case Kind::NewLineTrivia:
			canParseTag = true;
			seenAsterisk = false;
			break;
		case Kind::AsteriskToken:
			if (seenAsterisk) {
				canParseTag = false;
			}
			seenAsterisk = true;
			break;
		case Kind::Identifier:
			canParseTag = false;
			break;
		case Kind::EndOfFile:
			return nullptr;
		default:
			break;
		}
	}
}

Node* Parser::tryParseChildTag(PropertyLikeParse target, int indent) {
	if (token != Kind::AtToken) {
		TSC_UNREACHABLE("should only be called when at @");
	}
	int start = scanner->tokenFullStart();
	nextTokenJSDoc();

	Node* tagName = parseJSDocIdentifierName(Identifier_expected);
	std::string indentText = skipWhitespaceOrAsterisk();
	PropertyLikeParse t;
	std::string_view tagText = tagName->text();
	if (tagText == "type") {
		if (target == PropertyLikeParseProperty) {
			return parseTypeTag({}, start, tagName, -1, "");
		}
		return nullptr;
	} else if (tagText == "prop" || tagText == "property") {
		t = PropertyLikeParseProperty;
	} else if (tagText == "arg" || tagText == "argument" ||
	           tagText == "param") {
		t = PropertyLikeParseParameter | PropertyLikeParseCallbackParameter;
	} else if (tagText == "template") {
		return parseTemplateTag(start, tagName, indent, indentText);
	} else if (tagText == "this") {
		return parseThisTag(start, tagName, indent, indentText);
	} else {
		return nullptr;
	}
	if ((target & t) == 0) {
		return nullptr;
	}
	return parseParameterOrPropertyTag(start, tagName, target, indent);
}

Node* Parser::parseTemplateTagTypeParameter() {
	int typeParameterPos = nodePos();
	bool isBracketed = parseOptionalJsdoc(Kind::OpenBracketToken);
	if (isBracketed) {
		skipWhitespace();
	}

	ModifierList* modifiers = parseModifiersEx(false, true, false);
	Node* name = parseJSDocIdentifierName(
		Unexpected_token_A_type_parameter_name_was_expected_without_curly_braces);
	Node* defaultType = nullptr;
	if (isBracketed) {
		skipWhitespace();
		parseExpected(Kind::EqualsToken);
		NodeFlags saveContextFlags = contextFlags;
		setContextFlags(NodeFlagsJSDoc, true);
		defaultType = parseJSDocType();
		contextFlags = saveContextFlags;
		parseExpected(Kind::CloseBracketToken);
	}

	if (nodeIsMissing(name)) {
		return nullptr;
	}
	return finishNode(
		factory.newTypeParameterDeclaration(modifiers, name, nullptr,
		                                    nullptr, defaultType),
		typeParameterPos);
}

NodeList* Parser::parseTemplateTagTypeParameters() {
	std::vector<Node*> nodes;
	for (bool ok = true; ok; ok = parseOptionalJsdoc(Kind::CommaToken)) {
		skipWhitespace();
		Node* node = parseTemplateTagTypeParameter();
		if (node != nullptr) {
			nodes.push_back(node);
		}
		skipWhitespaceOrAsterisk();
	}
	// Go returns ast.TypeParameterList with unset Loc — equivalent to a
	// fresh NodeList here.
	return factory.newNodeList(std::move(nodes));
}

Node* Parser::parseTemplateTag(int start, Node* tagName, int indent,
                               std::string indentText) {
	// The template tag looks like one of the following:
	//   @template T,U,V
	//   @template {Constraint} T
	//
	// According to the [closure
	// docs](https://github.com/google/closure-compiler/wiki/Generic-Types#multiple-bounded-template-types):
	//   > Multiple bounded generics cannot be declared on the same line.
	//   > For the sake of clarity, if multiple templates share the same
	//   > type bound they must be declared on separate lines.
	Node* constraint = nullptr;
	if (token == Kind::OpenBraceToken) {
		constraint = parseJSDocTypeExpression(false);
	}
	NodeList* typeParameters = parseTemplateTagTypeParameters();
	Node* result = factory.newJSDocTemplateTag(
		tagName, constraint, typeParameters,
		parseTrailingTagComments(start, nodePos(), indent,
		                         std::move(indentText)));
	return finishNode(result, start);
}

bool Parser::parseOptionalJsdoc(Kind t) {
	if (token == t) {
		nextTokenJSDoc();
		return true;
	}
	return false;
}

Node* Parser::parseJSDocEntityName(const DiagnosticMessage* diagnosticMessage) {
	Node* entity = parseJSDocIdentifierName(diagnosticMessage);
	if (parseOptional(Kind::OpenBracketToken)) {
		parseExpected(Kind::CloseBracketToken);
		// Note that y[] is accepted as an entity name, but the postfix
		// brackets are not saved for checking. Technically usejsdoc.org
		// requires them for specifying a property of a type equivalent to
		// Array<{ x: ...}> but it's not worth it to enforce that
		// restriction.
	}
	while (parseOptional(Kind::DotToken)) {
		Node* name = parseJSDocIdentifierName(Identifier_expected);
		if (parseOptional(Kind::OpenBracketToken)) {
			parseExpected(Kind::CloseBracketToken);
		}
		int pos = entity->pos();
		entity = finishNode(factory.newQualifiedName(entity, name), pos);
	}
	return entity;
}

Node* Parser::parseJSDocIdentifierName(
	const DiagnosticMessage* diagnosticMessage) {
	if (!tokenIsIdentifierOrKeyword(token)) {
		if (diagnosticMessage != nullptr) {
			parseErrorAtCurrentToken(diagnosticMessage);
		} else if (isReservedWord(token)) {
			parseErrorAtCurrentToken(
				Identifier_expected_0_is_a_reserved_word_that_cannot_be_used_here,
				{std::string(scanner->tokenText())});
		}
		return finishNode(newIdentifier(""), nodePos());
	}
	int pos = scanner->tokenStart();
	int end = scanner->tokenEnd();
	std::string text = std::string(scanner->tokenValue());
	nextTokenJSDoc();
	return finishNodeWithEnd(newIdentifier(std::move(text)), pos, end);
}

// ---------------------------------------------------------------------------
// parser/utilities.go helpers used by JSDoc
// ---------------------------------------------------------------------------

std::vector<CommentRange> getJSDocCommentRanges(
	std::vector<CommentRange>* commentRanges, Node* node,
	std::string_view text) {
	std::vector<CommentRange> out;
	if (commentRanges != nullptr) {
		out = std::move(*commentRanges);
		out.clear();
	}
	switch (node->kind) {
	case Kind::Parameter:
	case Kind::TypeParameter:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::ParenthesizedExpression:
	case Kind::VariableDeclaration:
	case Kind::ExportSpecifier:
		getTrailingCommentRanges(
			text, node->pos(),
			[&](CommentRange r) { out.push_back(r); return true; });
		getLeadingCommentRanges(
			text, node->pos(),
			[&](CommentRange r) { out.push_back(r); return true; });
		break;
	default:
		getLeadingCommentRanges(
			text, node->pos(),
			[&](CommentRange r) { out.push_back(r); return true; });
		break;
	}
	// Keep if the comment starts with '/**' but not if it is '/**/'
	std::erase_if(out, [&](CommentRange comment) {
		int commentStart = comment.pos();
		int commentLen = comment.end() - commentStart;
		return comment.end() > node->end() || commentLen < 4 ||
		       text[commentStart + 1] != '*' ||
		       text[commentStart + 2] != '*' ||
		       text[commentStart + 3] == '/';
	});
	return out;
}

}  // namespace tsc
