// Port of tsc/internal/format/context.go.
#include "internal/format/format.h"

#include "internal/astnav/tokens.h"

namespace tsc::format {

FormattingContext* NewFormattingContext(SourceFile* file, FormatRequestKind kind,
										lsutil::FormatCodeSettings options) {
	auto* res = new FormattingContext();
	res->SourceFile = file;
	res->FormattingRequestKind = kind;
	res->Options = std::move(options);
	return res;
}

void FormattingContext::UpdateContext(TextRangeWithKind cur, Node* curParent,
									  TextRangeWithKind next, Node* nextParent,
									  Node* commonParent) {
	if (curParent == nullptr) {
		TSC_UNREACHABLE("nil current range node parent in update context");
	}
	if (nextParent == nullptr) {
		TSC_UNREACHABLE("nil next range node parent in update context");
	}
	if (commonParent == nullptr) {
		TSC_UNREACHABLE("nil common parent node in update context");
	}
	currentTokenSpan = cur;
	currentTokenParent = curParent;
	nextTokenSpan = next;
	nextTokenParent = nextParent;
	contextNode = commonParent;

	// drop cached results
	contextNodeAllOnSameLine = Tristate::Unknown;
	nextNodeAllOnSameLine = Tristate::Unknown;
	tokensAreOnSameLine = Tristate::Unknown;
	contextNodeBlockIsOnOneLine = Tristate::Unknown;
	nextNodeBlockIsOnOneLine = Tristate::Unknown;
}

Tristate FormattingContext::rangeIsOnOneLine(TextRange node) {
	if (tsc::format::rangeIsOnOneLine(node, SourceFile)) {
		return Tristate::True;
	}
	return Tristate::False;
}

Tristate FormattingContext::nodeIsOnOneLine(Node* node) {
	return rangeIsOnOneLine(withTokenStart(node, SourceFile));
}

TextRange withTokenStart(Node* loc, SourceFile* file) {
	int startPos = getTokenPosOfNode(loc, file, false);
	return TextRange{TextPos(startPos), loc->end()};
}

Tristate FormattingContext::blockIsOnOneLine(Node* node) {
	Node* openBrace = astnav::findChildOfKind(node, Kind::OpenBraceToken, SourceFile);
	Node* closeBrace = astnav::findChildOfKind(node, Kind::CloseBraceToken, SourceFile);
	if (openBrace != nullptr && closeBrace != nullptr) {
		int closeBraceStart = getTokenPosOfNode(closeBrace, SourceFile, false);
		return rangeIsOnOneLine(TextRange{openBrace->end(), TextPos(closeBraceStart)});
	}
	return Tristate::False;
}

bool FormattingContext::ContextNodeAllOnSameLine() {
	if (contextNodeAllOnSameLine == Tristate::Unknown) {
		contextNodeAllOnSameLine = nodeIsOnOneLine(contextNode);
	}
	return contextNodeAllOnSameLine == Tristate::True;
}

bool FormattingContext::NextNodeAllOnSameLine() {
	if (nextNodeAllOnSameLine == Tristate::Unknown) {
		nextNodeAllOnSameLine = nodeIsOnOneLine(nextTokenParent);
	}
	return nextNodeAllOnSameLine == Tristate::True;
}

bool FormattingContext::TokensAreOnSameLine() {
	if (tokensAreOnSameLine == Tristate::Unknown) {
		tokensAreOnSameLine =
			rangeIsOnOneLine(TextRange{currentTokenSpan.Loc.pos(), nextTokenSpan.Loc.end()});
	}
	return tokensAreOnSameLine == Tristate::True;
}

bool FormattingContext::ContextNodeBlockIsOnOneLine() {
	if (contextNodeBlockIsOnOneLine == Tristate::Unknown) {
		contextNodeBlockIsOnOneLine = blockIsOnOneLine(contextNode);
	}
	return contextNodeBlockIsOnOneLine == Tristate::True;
}

bool FormattingContext::NextNodeBlockIsOnOneLine() {
	if (nextNodeBlockIsOnOneLine == Tristate::Unknown) {
		nextNodeBlockIsOnOneLine = blockIsOnOneLine(nextTokenParent);
	}
	return nextNodeBlockIsOnOneLine == Tristate::True;
}

} // namespace tsc::format
