// Port of tsc/internal/format/scanner.go.
#include "internal/format/format.h"

#include <deque>
#include <utility>

#include "internal/astnav/tokens.h"

namespace tsc::format {

// Go: ast.IsTrivia / ast.IsTokenKind.
static bool isTrivia(Kind kind) {
	return kind >= KindFirstTriviaToken && kind <= KindLastTriviaToken;
}
static bool isTokenKind(Kind kind) {
	return kind >= KindFirstToken && kind <= KindLastToken;
}

std::vector<TextChange> newFormattingScanner(std::string_view text,
											 LanguageVariant languageVariant, int startPos,
											 int endPos, formatSpanWorker* worker) {
	formattingScanner fmtScn;
	fmtScn.s.setSkipTrivia(false);
	fmtScn.s.setLanguageVariant(languageVariant);
	fmtScn.s.setText(text);
	fmtScn.s.resetTokenState(startPos);
	fmtScn.startPos = startPos;
	fmtScn.endPos = endPos;
	fmtScn.wasNewLine = true;

	std::vector<TextChange> res = worker->execute(&fmtScn);

	fmtScn.hasLastTokenInfo = false;
	fmtScn.s.reset();

	return res;
}

void formattingScanner::advance() {
	hasLastTokenInfo = false;
	bool isStarted = s.tokenFullStart() != startPos;

	if (isStarted) {
		wasNewLine = !trailingTrivia.empty() &&
			trailingTrivia.back().kind == Kind::NewLineTrivia;
	} else {
		s.scan();
	}

	leadingTrivia.clear();
	trailingTrivia.clear();

	int pos = s.tokenFullStart();

	// Read leading trivia and token
	while (pos < endPos) {
		Kind t = s.token();
		if (!isTrivia(t)) {
			break;
		}

		// consume leading trivia
		s.scan();
		TextRangeWithKind item = NewTextRangeWithKind(pos, s.tokenFullStart(), t);

		pos = s.tokenFullStart();

		leadingTrivia.push_back(item);
	}

	savedPos = s.tokenFullStart();
}

bool shouldRescanGreaterThanToken(Node* node) {
	switch (node->kind) {
	case Kind::GreaterThanEqualsToken:
	case Kind::GreaterThanGreaterThanEqualsToken:
	case Kind::GreaterThanGreaterThanGreaterThanEqualsToken:
	case Kind::GreaterThanGreaterThanGreaterThanToken:
	case Kind::GreaterThanGreaterThanToken:
		return true;
	default:
		break;
	}
	return false;
}

bool shouldRescanJsxIdentifier(Node* node) {
	if (node->parent != nullptr) {
		switch (node->parent->kind) {
		case Kind::JsxAttribute:
		case Kind::JsxOpeningElement:
		case Kind::JsxClosingElement:
		case Kind::JsxSelfClosingElement:
		case Kind::JsxNamespacedName:
			// May parse an identifier like `module-layout`; that will be scanned as a keyword at first, but we should parse the whole thing to get an identifier.
			return isKeywordKind(node->kind) || node->kind == Kind::Identifier;
		case Kind::PropertyAccessExpression:
			// The leftmost name of a dotted JSX tag name (e.g. `a-b` in `<a-b.c>`) may contain hyphens, so rescan it as a JSX identifier.
			return (isKeywordKind(node->kind) || node->kind == Kind::Identifier) &&
				isLeftmostJsxTagName(node);
		default:
			break;
		}
	}
	return false;
}

bool isLeftmostJsxTagName(Node* node) {
	return findAncestorOrQuit(node, [](Node* n) -> FindAncestorResult {
		if (n->parent == nullptr) {
			return FindAncestorResult::Quit;
		}
		if (isJsxTagName(n)) {
			return FindAncestorResult::True;
		}
		if (isPropertyAccessExpression(n->parent) && n->parent->expression() == n) {
			return FindAncestorResult::False;
		}
		return FindAncestorResult::Quit;
	}) != nullptr;
}

bool formattingScanner::shouldRescanJsxText(Node* node) {
	if (isJsxText(node)) {
		return true;
	}
	if (!isJsxElement(node) || !hasLastTokenInfo) {
		return false;
	}

	return lastTokenInfo.token.kind == Kind::JsxText;
}

bool shouldRescanSlashToken(Node* container) {
	return container->kind == Kind::RegularExpressionLiteral;
}

bool shouldRescanTemplateToken(Node* container) {
	return container->kind == Kind::TemplateMiddle ||
		container->kind == Kind::TemplateTail;
}

bool shouldRescanJsxAttributeValue(Node* node) {
	return node->parent != nullptr && isJsxAttribute(node->parent) &&
		node->parent->initializer() == node;
}

bool startsWithSlashToken(Kind t) {
	return t == Kind::SlashToken || t == Kind::SlashEqualsToken;
}

tokenInfo fixTokenKind(tokenInfo info, Node* container) {
	if (isTokenKind(container->kind) && info.token.kind != container->kind) {
		info.token.kind = container->kind;
	}
	return info;
}

tokenInfo formattingScanner::readTokenInfo(Node* n) {
	TSC_ASSERT(isOnToken(), "must be on token");

	// normally scanner returns the smallest available token
	// check the kind of context node to determine if scanner should have more greedy behavior and consume more text.

	scanAction expectedScanAction;
	if (shouldRescanGreaterThanToken(n)) {
		expectedScanAction = scanAction::actionRescanGreaterThanToken;
	} else if (shouldRescanSlashToken(n)) {
		expectedScanAction = scanAction::actionRescanSlashToken;
	} else if (shouldRescanTemplateToken(n)) {
		expectedScanAction = scanAction::actionRescanTemplateToken;
	} else if (shouldRescanJsxIdentifier(n)) {
		expectedScanAction = scanAction::actionRescanJsxIdentifier;
	} else if (shouldRescanJsxText(n)) {
		expectedScanAction = scanAction::actionRescanJsxText;
	} else if (shouldRescanJsxAttributeValue(n)) {
		expectedScanAction = scanAction::actionRescanJsxAttributeValue;
	} else {
		expectedScanAction = scanAction::actionScan;
	}

	if (hasLastTokenInfo && expectedScanAction == lastScanAction) {
		// readTokenInfo was called before with the same expected scan action.
		// No need to re-scan text, return existing 'lastTokenInfo'
		// it is ok to call fixTokenKind here since it does not affect
		// what portion of text is consumed. In contrast rescanning can change it,
		// i.e. for '>=' when originally scanner eats just one character
		// and rescanning forces it to consume more.
		lastTokenInfo = fixTokenKind(lastTokenInfo, n);
		return lastTokenInfo;
	}

	if (s.tokenFullStart() != savedPos) {
		// readTokenInfo was called before but scan action differs - rescan text
		s.resetTokenState(savedPos);
		s.scan();
	}

	Kind currentToken = getNextToken(n, expectedScanAction);

	TextRangeWithKind token = NewTextRangeWithKind(
		s.tokenFullStart(),
		s.tokenEnd(),
		currentToken
	);

	// consume trailing trivia
	trailingTrivia.clear();
	while (s.tokenFullStart() < endPos) {
		currentToken = s.scan();
		if (!isTrivia(currentToken)) {
			break;
		}
		TextRangeWithKind trivia = NewTextRangeWithKind(
			s.tokenFullStart(),
			s.tokenEnd(),
			currentToken
		);

		trailingTrivia.push_back(trivia);

		if (currentToken == Kind::NewLineTrivia) {
			// move past new line
			s.scan();
			break;
		}
	}

	hasLastTokenInfo = true;
	lastTokenInfo = tokenInfo{
		leadingTrivia,
		token,
		trailingTrivia,
	};
	lastTokenInfo = fixTokenKind(lastTokenInfo, n);

	return lastTokenInfo;
}

Kind formattingScanner::getNextToken(Node* n, scanAction expectedScanAction) {
	Kind token = s.token();
	lastScanAction = scanAction::actionScan;
	switch (expectedScanAction) {
	case scanAction::actionRescanGreaterThanToken:
		if (token == Kind::GreaterThanToken) {
			lastScanAction = scanAction::actionRescanGreaterThanToken;
			Kind newToken = s.reScanGreaterThanToken();
			TSC_ASSERT(n->kind == newToken, "rescan produced unexpected token kind");
			return newToken;
		}
		break;
	case scanAction::actionRescanSlashToken:
		if (startsWithSlashToken(token)) {
			lastScanAction = scanAction::actionRescanSlashToken;
			Kind newToken = s.reScanSlashToken();
			TSC_ASSERT(n->kind == newToken, "rescan produced unexpected token kind");
			return newToken;
		}
		break;
	case scanAction::actionRescanTemplateToken:
		if (token == Kind::CloseBraceToken) {
			lastScanAction = scanAction::actionRescanTemplateToken;
			return s.reScanTemplateToken(/*isTaggedTemplate*/ false);
		}
		break;
	case scanAction::actionRescanJsxIdentifier:
		lastScanAction = scanAction::actionRescanJsxIdentifier;
		return s.scanJsxIdentifier();
	case scanAction::actionRescanJsxText:
		lastScanAction = scanAction::actionRescanJsxText;
		return s.reScanJsxToken(/*allowMultilineJsxText*/ false);
	case scanAction::actionRescanJsxAttributeValue:
		lastScanAction = scanAction::actionRescanJsxAttributeValue;
		return s.reScanJsxAttributeValue();
	case scanAction::actionScan:
		// no rescan needed; the token was already produced by the normal scan
		break;
	default:
		TSC_UNREACHABLE("unhandled scan action kind");
	}
	return token;
}

TextRangeWithKind formattingScanner::readEOFTokenRange() {
	TSC_ASSERT(isOnEOF(), "must be on EOF");
	return NewTextRangeWithKind(
		s.tokenFullStart(),
		s.tokenEnd(),
		Kind::EndOfFile
	);
}

bool formattingScanner::isOnToken() {
	Kind current = s.token();
	if (hasLastTokenInfo) {
		current = lastTokenInfo.token.kind;
	}
	return current != Kind::EndOfFile && !isTrivia(current);
}

bool formattingScanner::isOnEOF() {
	Kind current = s.token();
	if (hasLastTokenInfo) {
		current = lastTokenInfo.token.kind;
	}
	return current == Kind::EndOfFile;
}

void formattingScanner::skipToEndOf(TextRange* r) {
	s.resetTokenState(r->end());
	savedPos = s.tokenFullStart();
	lastScanAction = scanAction::actionScan;
	hasLastTokenInfo = false;
	wasNewLine = false;
	leadingTrivia.clear();
	trailingTrivia.clear();
}

void formattingScanner::skipToStartOf(TextRange* r) {
	s.resetTokenState(r->pos());
	savedPos = s.tokenFullStart();
	lastScanAction = scanAction::actionScan;
	hasLastTokenInfo = false;
	wasNewLine = false;
	leadingTrivia.clear();
	trailingTrivia.clear();
}

std::vector<TextRangeWithKind> formattingScanner::getCurrentLeadingTrivia() {
	return leadingTrivia;
}

bool formattingScanner::lastTrailingTriviaWasNewLine() {
	return wasNewLine;
}

int formattingScanner::getTokenFullStart() {
	if (hasLastTokenInfo) {
		return lastTokenInfo.token.Loc.pos();
	}
	return s.tokenFullStart();
}

int formattingScanner::getStartPos() { // TODO: redundant?
	return getTokenFullStart();
}

} // namespace tsc::format
