// === slice: ls-foundation ===
// ls/change/tracker.go — the ChangeTracker.

#include "internal/ls/change/change.h"
#include "internal/ls/lsdeps.h" // lsconv::Converters methods

#include <algorithm>
#include <utility>

#include "internal/astnav/tokens.h"
#include "internal/core/types.h"
#include "internal/debug/debug.h"
#include "internal/printer/emitcontext.h"
#include "internal/printer/printer.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::ls::change {

namespace {

// core.NewTextRange
TextRange newTextRange(int pos, int end) {
	return TextRange{static_cast<TextPos>(pos), static_cast<TextPos>(end)};
}

// core.NewLineKind.GetNewLineCharacter
std::string getNewLineCharacter(NewLineKind newLine) {
	switch (newLine) {
	case NewLineKind::CarriageReturnLineFeed:
		return "\r\n";
	default:
		return "\n";
	}
}

// slices.Collect over the scanner's comment-range iterators.
std::vector<CommentRange> collectCommentRanges(
	const std::function<void(const std::function<bool(const CommentRange&)>&)>& seq) {
	std::vector<CommentRange> out;
	seq([&out](const CommentRange& r) {
		out.push_back(r);
		return true;
	});
	return out;
}

// positionsAreOnSameLine — delete.go:247
bool positionsAreOnSameLine(int pos1, int pos2, SourceFile* sourceFile) {
	return format::GetLineStartPositionForPosition(pos1, sourceFile) ==
		   format::GetLineStartPositionForPosition(pos2, sourceFile);
}

// hasCommentsBeforeLineBreak — trackerimpl.go:385
bool hasCommentsBeforeLineBreak(std::string_view text, int start) {
	for (size_t i = static_cast<size_t>(start); i < text.size(); i++) {
		int w = 0;
		char32_t ch = decodeUtf8Rune(text.substr(i), &w);
		i += static_cast<size_t>(w - 1);
		if (!isWhiteSpaceSingleLine(ch)) {
			return ch == U'/';
		}
	}
	return false;
}

// getMembersOrProperties — tracker.go:768
NodeList* getMembersOrProperties(Node* node) {
	if (isObjectLiteralExpression(node)) {
		return node->propertyList();
	}
	return node->memberList();
}

// rangeContainsRangeExclusive — tracker.go:775
bool rangeContainsRangeExclusive(Node* outer, Node* inner) {
	return outer->pos() < inner->pos() && inner->end() < outer->end();
}

// isSeparator — tracker.go:779
bool isSeparator(Node* node, Node* candidate) {
	return candidate != nullptr && node->parent != nullptr &&
		   (candidate->kind == Kind::CommaToken ||
			(candidate->kind == Kind::SemicolonToken &&
			 node->parent->kind == Kind::ObjectLiteralExpression));
}

// advanceIndentationColumn — tracker.go:810
int advanceIndentationColumn(int column, char32_t ch, int tabSize) {
	if (ch == U'\t') {
		return column + tabSize - (column % tabSize);
	}
	return column + 1;
}

// findIndentationColumn — tracker.go:788
int findIndentationColumn(std::string_view text, int lineStart, int memberStart, int tabSize) {
	int column = 0;

	for (int i = lineStart; i < memberStart && i < static_cast<int>(text.size()); i++) {
		char32_t ch = static_cast<unsigned char>(text[i]);

		if (isLineBreak(ch)) {
			return -1;
		}
		if (isWhiteSpaceSingleLine(ch)) {
			column = advanceIndentationColumn(column, ch, tabSize);
			continue;
		}
		return column;
	}

	return column;
}

// needSemicolonBetween — trackerimpl.go:392
bool needSemicolonBetween(Node* a, Node* b) {
	return (isPropertySignatureDeclaration(a) || isPropertyDeclaration(a)) &&
			   lsutil::detail::isClassOrTypeElement(b) &&
			   b->name()->kind == Kind::ComputedPropertyName ||
		   lsutil::detail::isStatementButNotDeclaration(a) &&
			   lsutil::detail::isStatementButNotDeclaration(b);
}

} // namespace

// Tracker ctor — tracker.go:102 (NewTracker)
Tracker::Tracker(const format::FormatRequestContext& ctxIn, const CompilerOptions* compilerOptions,
				 const lsutil::FormatCodeSettings& formatOptions, lsconv::Converters* convertersIn) {
	emitContext = printer::NewEmitContext();
	// Go: &emitContext.Factory.NodeFactory — the ast::NodeFactory embedded in
	// the emit-aware printer::NodeFactory (printer::NodeFactory IS-A
	// tsc::NodeFactory).
	nodeFactory = &emitContext->factory;
	newLine = getNewLineCharacter(compilerOptions->NewLine);
	ctx = format::WithFormatCodeSettings(ctxIn, formatOptions, newLine); // !!! formatSettings in context?
	converters = convertersIn;
	formatSettings = formatOptions;
}

Tracker::~Tracker() {
	delete emitContext;
}

// GetChanges — tracker.go:115. Returns the accumulated text edits grouped by
// file name; files whose edits could not be faithfully mapped back onto
// content-mapped original text are omitted and listed in the second return
// value. After calling this, the Tracker object must be discarded!
std::pair<std::map<std::string, std::vector<lsp::lsproto::TextEdit>>, std::vector<std::string>>
Tracker::GetChanges() {
	finishDeleteDeclarations();
	finishNodesWithInsertionsAtStart();
	auto changes = getTextChangesFromChanges();
	// !!! changes for new files
	if (unmappableFiles.Size() == 0) {
		return {changes, {}};
	}
	std::vector<std::string> unmappable;
	for (const std::string& fileName : unmappableFiles.Keys()) {
		changes.erase(fileName);
		unmappable.push_back(fileName);
	}
	std::sort(unmappable.begin(), unmappable.end());
	return {changes, unmappable};
}

// fromLSPEditRange — tracker.go:147. Converts an LSP range to a source file
// range. For a content-mapped file, an original range may have several
// projections; this selects the one belonging to sourceFile. A range with no
// exact projection cannot be written back and marks the file unmappable.
TextRange Tracker::fromLSPEditRange(SourceFile* sourceFile, lsp::lsproto::Range lsprotoRange) {
	auto spans = converters->FromLSPRangeForSourceFile(sourceFile, lsprotoRange, spanmap::FeatureAll);
	for (auto& span : spans) {
		if (span.Fidelity.IsExact() && span.Script == sourceFile) {
			return span.Span;
		}
	}
	unmappableFiles.Add(sourceFile->OriginalFileName());
	if (!spans.empty()) {
		return spans[0].Span;
	}
	return newTextRange(0, 0);
}

// toLSPEditRange — tracker.go:164. Converts a source file range to an LSP
// range. For a content-mapped file, the range is mapped back to the original
// document. If it does not fall entirely within a single verbatim span, the
// edit cannot be represented safely: the file is recorded so GetChanges drops
// its edits, and a best-effort range is returned so the accumulated edits stay
// well-formed.
lsp::lsproto::Range Tracker::toLSPEditRange(SourceFile* sourceFile, TextRange textRange) {
	auto [r, fidelity] = converters->ToLSPRange(sourceFile, textRange);
	if (!fidelity.IsExact()) {
		unmappableFiles.Add(sourceFile->OriginalFileName());
	}
	return r;
}

// ReplaceNode — tracker.go:176
void Tracker::ReplaceNode(SourceFile* sourceFile, Node* oldNode, Node* newNode, NodeOptions* options) {
	NodeOptions defaultOptions;
	if (options == nullptr) {
		// defaults to `useNonAdjustedPositions`
		defaultOptions.leadingTrivia = LeadingTriviaOptionExclude;
		defaultOptions.trailingTrivia = TrailingTriviaOptionExclude;
		options = &defaultOptions;
	}
	ReplaceRange(sourceFile,
				 GetAdjustedRange(sourceFile, oldNode, oldNode, options->leadingTrivia,
								  options->trailingTrivia),
				 newNode, *options);
}

// ReplaceNodeWithNodes — tracker.go:186
void Tracker::ReplaceNodeWithNodes(SourceFile* sourceFile, Node* oldNode, std::vector<Node*> newNodes,
								   NodeOptions* options) {
	NodeOptions defaultOptions;
	if (options == nullptr) {
		defaultOptions.leadingTrivia = LeadingTriviaOptionExclude;
		defaultOptions.trailingTrivia = TrailingTriviaOptionExclude;
		options = &defaultOptions;
	}
	ReplaceRangeWithNodes(sourceFile,
						  GetAdjustedRange(sourceFile, oldNode, oldNode, options->leadingTrivia,
										   options->trailingTrivia),
						  std::move(newNodes), *options);
}

// ReplaceRange — tracker.go:197
void Tracker::ReplaceRange(SourceFile* sourceFile, TextRange textRange, Node* newNode,
						   NodeOptions options) {
	auto* edit = new trackerEdit();
	edit->kind = trackerEditKindReplaceWithSingleNode;
	edit->textRange = textRange;
	edit->options = std::move(options);
	edit->node = newNode;
	changes.Add(sourceFile, edit);
}

// ReplaceRangeWithText — tracker.go:202. For a content-mapped file, the LSP
// range is in the original document; text may be placed at any exact
// projection because it does not need formatting context.
void Tracker::ReplaceRangeWithText(SourceFile* sourceFile, lsp::lsproto::Range lsprotoRange,
								   const std::string& text) {
	ReplaceTextRangeWithText(sourceFile, fromLSPEditRange(sourceFile, lsprotoRange), text);
}

// ReplaceTextRangeWithText — tracker.go:207
void Tracker::ReplaceTextRangeWithText(SourceFile* sourceFile, TextRange textRange,
									   const std::string& text) {
	auto* edit = new trackerEdit();
	edit->kind = trackerEditKindText;
	edit->textRange = textRange;
	edit->NewText = text;
	changes.Add(sourceFile, edit);
}

// ReplaceRangeWithNodes — tracker.go:212
void Tracker::ReplaceRangeWithNodes(SourceFile* sourceFile, TextRange textRange,
									std::vector<Node*> newNodes, NodeOptions options) {
	if (newNodes.size() == 1) {
		ReplaceRange(sourceFile, textRange, newNodes[0], options);
		return;
	}
	auto* edit = new trackerEdit();
	edit->kind = trackerEditKindReplaceWithMultipleNodes;
	edit->textRange = textRange;
	edit->nodes = std::move(newNodes);
	edit->options = std::move(options);
	changes.Add(sourceFile, edit);
}

// insertTextAt — tracker.go:221
void Tracker::insertTextAt(SourceFile* sourceFile, TextPos pos, const std::string& text) {
	ReplaceTextRangeWithText(sourceFile, newTextRange(pos, pos), text);
}

// InsertText — tracker.go:226
void Tracker::InsertText(SourceFile* sourceFile, lsp::lsproto::Position pos, const std::string& text) {
	lsp::lsproto::Range range;
	range.Start = pos;
	range.End = pos;
	ReplaceRangeWithText(sourceFile, range, text);
}

// InsertNodeAt — tracker.go:230
void Tracker::InsertNodeAt(SourceFile* sourceFile, TextPos pos, Node* newNode, NodeOptions options) {
	ReplaceRange(sourceFile, newTextRange(pos, pos), newNode, std::move(options));
}

// InsertNodesAt — tracker.go:234
void Tracker::InsertNodesAt(SourceFile* sourceFile, TextPos pos, std::vector<Node*> newNodes,
							NodeOptions options) {
	ReplaceRangeWithNodes(sourceFile, newTextRange(pos, pos), std::move(newNodes), std::move(options));
}

// InsertNodeAfter — tracker.go:238
void Tracker::InsertNodeAfter(SourceFile* sourceFile, Node* after, Node* newNode) {
	TextPos endPosition = endPosForInsertNodeAfter(sourceFile, after, newNode);
	InsertNodeAt(sourceFile, endPosition, newNode, getInsertNodeAfterOptions(sourceFile, after));
}

// InsertNodesAfter — tracker.go:243
void Tracker::InsertNodesAfter(SourceFile* sourceFile, Node* after, std::vector<Node*> newNodes) {
	TextPos endPosition = endPosForInsertNodeAfter(sourceFile, after, newNodes[0]);
	InsertNodesAt(sourceFile, endPosition, std::move(newNodes), getInsertNodeAfterOptions(sourceFile, after));
}

// InsertNodeBefore — tracker.go:248
void Tracker::InsertNodeBefore(SourceFile* sourceFile, Node* before, Node* newNode,
							   bool blankLineBetween, LeadingTriviaOption leadingTriviaOption) {
	InsertNodeAt(sourceFile,
				 static_cast<TextPos>(
					 getAdjustedStartPosition(sourceFile, before, leadingTriviaOption, false)),
				 newNode, getOptionsForInsertNodeBefore(before, newNode, blankLineBetween));
}

// TryInsertTypeAnnotation — tracker.go:253. Inserts a type annotation after
// the appropriate position on a node (after the close paren for
// function-like, after the name/exclamation/question for variable-like).
// Returns true if successful.
bool Tracker::TryInsertTypeAnnotation(SourceFile* sourceFile, Node* node, Node* typeNode) {
	Node* endNode = nullptr;
	if (isFunctionLike(node)) {
		endNode = astnav::findChildOfKind(node, Kind::CloseParenToken, sourceFile);
		if (endNode == nullptr) {
			if (!isArrowFunction(node)) {
				return false;
			}
			// If no `)`, is an arrow function `x => x`, so use the end of the
			// first parameter
			auto params = node->parameters();
			if (params.empty()) {
				return false;
			}
			endNode = params[0];
		}
	} else {
		switch (node->kind) {
		case Kind::VariableDeclaration:
			endNode = node->as<VariableDeclaration>()->ExclamationToken;
			break;
		case Kind::PropertySignature:
			endNode = node->as<PropertySignatureDeclaration>()->PostfixToken;
			break;
		case Kind::PropertyDeclaration:
			endNode = node->as<PropertyDeclaration>()->PostfixToken;
			break;
		case Kind::Parameter:
			endNode = node->as<ParameterDeclaration>()->QuestionToken;
			break;
		default:
			break;
		}
		if (endNode == nullptr) {
			endNode = node->name();
		}
	}
	if (endNode == nullptr) {
		return false;
	}
	NodeOptions options;
	options.Prefix = ": ";
	InsertNodeAt(sourceFile, static_cast<TextPos>(endNode->end()), typeNode, options);
	return true;
}

// ParenthesizeArrowParameters — tracker.go:279. Wraps the parameters of a
// paren-less arrow function in `(` and `)`. No-op if the arrow function
// already has parens.
void Tracker::ParenthesizeArrowParameters(SourceFile* sourceFile, Node* arrowFunc) {
	if (astnav::findChildOfKind(arrowFunc, Kind::CloseParenToken, sourceFile) != nullptr) {
		return;
	}
	auto params = arrowFunc->parameters();
	if (params.empty()) {
		return;
	}
	Node* firstParam = params[0];
	Node* lastParam = params.back();
	int startPos = astnav::getStartOfNode(firstParam, sourceFile, false);
	insertTextAt(sourceFile, static_cast<TextPos>(startPos), "(");
	insertTextAt(sourceFile, static_cast<TextPos>(lastParam->end()), ")");
}

// InsertModifierBefore — tracker.go:293. Inserts a modifier token (like
// 'type') before a node with a trailing space.
void Tracker::InsertModifierBefore(SourceFile* sourceFile, Kind modifier, Node* before) {
	int pos = astnav::getStartOfNode(before, sourceFile, false);
	Node* token = nodeFactory->newToken(modifier);
	token->loc = newTextRange(pos, pos);
	token->parent = before->parent;
	NodeOptions options;
	options.Suffix = " ";
	InsertNodeAt(sourceFile, static_cast<TextPos>(pos), token, options);
}

// Delete — tracker.go:303. Queues a node for deletion with smart handling of
// list items, imports, etc. The actual deletion happens in
// finishDeleteDeclarations during GetChanges.
void Tracker::Delete(SourceFile* sourceFile, Node* node) {
	deletedNodes.push_back(deletedNode{sourceFile, node});
}

// DeleteRange — tracker.go:308
void Tracker::DeleteRange(SourceFile* sourceFile, TextRange textRange) {
	ReplaceTextRangeWithText(sourceFile, textRange, "");
}

// DeleteNode — tracker.go:314. Deletes a node immediately with specified
// trivia options. Stop! Consider using Delete instead, which has logic for
// deleting nodes from delimited lists.
void Tracker::DeleteNode(SourceFile* sourceFile, Node* node, LeadingTriviaOption leadingTrivia,
						 TrailingTriviaOption trailingTrivia) {
	ReplaceTextRangeWithText(sourceFile,
							 GetAdjustedRange(sourceFile, node, node, leadingTrivia, trailingTrivia),
							 "");
}

// DeleteNodeRange — tracker.go:320
void Tracker::DeleteNodeRange(SourceFile* sourceFile, Node* startNode, Node* endNode,
							  LeadingTriviaOption leadingTrivia, TrailingTriviaOption trailingTrivia) {
	int startPosition = getAdjustedStartPosition(sourceFile, startNode, leadingTrivia, false);
	int endPosition = getAdjustedEndPosition(sourceFile, endNode, trailingTrivia);
	ReplaceTextRangeWithText(sourceFile, newTextRange(startPosition, endPosition), "");
}

// finishDeleteDeclarations — tracker.go:327
void Tracker::finishDeleteDeclarations() {
	std::unordered_map<Node*, bool> deletedNodesInLists;

	for (const deletedNode& deleted : deletedNodes) {
		// Skip if this node is contained within another deleted node
		bool isContained = false;
		for (const deletedNode& other : deletedNodes) {
			if (other.sourceFile == deleted.sourceFile && other.node != deleted.node &&
				rangeContainsRangeExclusive(other.node, deleted.node)) {
				isContained = true;
				break;
			}
		}
		if (isContained) {
			continue;
		}

		deleteDeclaration(this, deletedNodesInLists, deleted.sourceFile, deleted.node);
	}

	// Handle trailing commas for last elements in lists
	for (auto& [node, _] : deletedNodesInLists) {
		SourceFile* sourceFile = getSourceFileOfNode(node);
		NodeList* list = format::GetContainingList(node, sourceFile);
		if (list == nullptr || node != list->nodes.back()) {
			continue;
		}

		int lastNonDeletedIndex = -1;
		for (int i = static_cast<int>(list->nodes.size()) - 2; i >= 0; i--) {
			if (!deletedNodesInLists.count(list->nodes[i])) {
				lastNonDeletedIndex = i;
				break;
			}
		}

		if (lastNonDeletedIndex != -1) {
			int start = list->nodes[lastNonDeletedIndex]->end();
			int end = startPositionToDeleteNodeInList(sourceFile, list->nodes[lastNonDeletedIndex + 1]);
			ReplaceTextRangeWithText(sourceFile, newTextRange(start, end), "");
		}
	}
}

// endPosForInsertNodeAfter — tracker.go:367
TextPos Tracker::endPosForInsertNodeAfter(SourceFile* sourceFile, Node* after, Node* newNode) {
	if (needSemicolonBetween(after, newNode) &&
		static_cast<unsigned char>(sourceFile->Text()[after->end() - 1]) != ';') {
		// check if previous statement ends with semicolon
		// if not - insert semicolon to preserve the code from changing the
		// meaning due to ASI
		TextPos endPos = static_cast<TextPos>(after->end());
		Node* semicolon = nodeFactory->newToken(Kind::SemicolonToken);
		semicolon->loc = newTextRange(after->end(), after->end());
		semicolon->parent = after->parent;
		ReplaceRange(sourceFile, newTextRange(endPos, endPos), semicolon, NodeOptions{});
	}
	return static_cast<TextPos>(getAdjustedEndPosition(sourceFile, after, TrailingTriviaOptionNone));
}

// InsertNodeInListAfter — tracker.go:384. This function should be used to
// insert nodes in lists when nodes don't carry separators as the part of the
// node range, i.e. arguments in arguments lists, parameters in parameter
// lists etc. Note that separators are part of the node in statements and
// class elements.
void Tracker::InsertNodeInListAfter(SourceFile* sourceFile, Node* after, Node* newNode,
									NodeList* containingList) {
	if (containingList == nullptr) {
		containingList = format::GetContainingList(after, sourceFile);
	}
	if (containingList == nullptr) {
		// Debug.fail("node is not a list element")
		return;
	}
	auto it = std::find(containingList->nodes.begin(), containingList->nodes.end(), after);
	int index = it == containingList->nodes.end() ? -1 : static_cast<int>(it - containingList->nodes.begin());
	if (index < 0) {
		return;
	}
	int end = after->end();
	if (index != static_cast<int>(containingList->nodes.size()) - 1) {
		// any element except the last one
		// use next sibling as an anchor
		Node* nextToken = astnav::getTokenAtPosition(sourceFile, after->end());
		if (nextToken != nullptr && isSeparator(after, nextToken)) {
			// for list
			// a, b, c
			// create change for adding 'e' after 'a' as
			// - find start of next element after a (it is b)
			// - use next element start as start and end position in final change
			// - build text of change by formatting the text of node +
			//   whitespace trivia of b

			// in multiline case it will work as
			//   a,
			//   b,
			//   c,
			// result - '*' denotes leading trivia that will be inserted after
			// new text (displayed as '#')
			//   a,
			//   insertedtext<separator>#
			// ###b,
			//   c,
			Node* nextNode = containingList->nodes[index + 1];
			SkipTriviaOptions triviaOpts;
			triviaOpts.stopAfterLineBreak = false;
			triviaOpts.stopAtComments = true;
			int startPos = skipTriviaEx(sourceFile->Text(), nextNode->pos(), triviaOpts);

			// write separator and leading trivia of the next element as suffix
			std::string suffix = std::string(tokenToString(nextToken->kind)) +
				std::string(sourceFile->Text().substr(
					static_cast<size_t>(nextToken->end()),
					static_cast<size_t>(startPos - nextToken->end())));
			NodeOptions options;
			options.Suffix = suffix;
			InsertNodesAt(sourceFile, static_cast<TextPos>(startPos), {newNode}, options);
		}
		return;
	}

	int afterStart = astnav::getStartOfNode(after, sourceFile, false);
	int afterStartLinePosition = format::GetLineStartPositionForPosition(afterStart, sourceFile);

	// insert element after the last element in the list that has more than one
	// item pick the element preceding the after element to:
	// - pick the separator
	// - determine if list is a multiline
	bool multilineList = false;

	// if list has only one element then we'll format is as multiline if node
	// has comment in trailing trivia, or as singleline otherwise
	// i.e. var x = 1 // this is x
	//     | new element will be inserted at this position
	Kind separator = Kind::CommaToken; // SyntaxKind.CommaToken | SyntaxKind.SemicolonToken
	if (containingList->nodes.size() != 1) {
		// otherwise, if list has more than one element, pick separator from
		// the list
		Node* tokenBeforeInsertPosition = astnav::findPrecedingToken(sourceFile, after->pos());
		separator = isSeparator(after, tokenBeforeInsertPosition)
			? tokenBeforeInsertPosition->kind
			: Kind::CommaToken;
		// determine if list is multiline by checking lines of after element
		// and element that precedes it.
		int afterMinusOneStartLinePosition = format::GetLineStartPositionForPosition(
			astnav::getStartOfNode(containingList->nodes[index - 1], sourceFile, false), sourceFile);
		multilineList = afterMinusOneStartLinePosition != afterStartLinePosition;
	}
	if (hasCommentsBeforeLineBreak(sourceFile->Text(), after->end()) ||
		!positionsAreOnSameLine(containingList->pos(), containingList->end(), sourceFile)) {
		// in this case we'll always treat containing list as multiline
		multilineList = true;
	}
	if (multilineList) {
		// insert separator immediately following the 'after' node to preserve
		// comments in trailing trivia
		Node* separatorToken = nodeFactory->newToken(separator);
		std::string_view separatorString = tokenToString(separator);
		separatorToken->loc = newTextRange(end, end + static_cast<int>(separatorString.size()));
		separatorToken->parent = after->parent;
		TextPos endPos = static_cast<TextPos>(end);
		ReplaceRange(sourceFile, newTextRange(endPos, endPos), separatorToken, NodeOptions{});
		// use the same indentation as 'after' item
		int indentation = format::FindFirstNonWhitespaceColumn(afterStartLinePosition, afterStart,
															 sourceFile, formatSettings);
		// insert element before the line break on the line that contains
		// 'after' element
		SkipTriviaOptions triviaOpts;
		triviaOpts.stopAfterLineBreak = true;
		triviaOpts.stopAtComments = false;
		int insertPos = skipTriviaEx(sourceFile->Text(), end, triviaOpts);
		// find position before "\n" or "\r\n"
		while (insertPos != end &&
			   isLineBreak(
				   static_cast<unsigned char>(sourceFile->Text()[insertPos - 1]))) {
			insertPos--;
		}
		TextPos insertLSPos = static_cast<TextPos>(insertPos);
		NodeOptions options;
		options.indentation = indentation;
		options.Prefix = newLine;
		ReplaceRange(sourceFile, newTextRange(insertLSPos, insertLSPos), newNode, options);
	} else {
		std::string_view separatorString = tokenToString(separator);
		TextPos endPos = static_cast<TextPos>(end);
		NodeOptions options;
		options.Prefix = std::string(separatorString) + " ";
		ReplaceRange(sourceFile, newTextRange(endPos, endPos), newNode, options);
	}
}

// InsertImportSpecifierAtIndex — tracker.go:513
void Tracker::InsertImportSpecifierAtIndex(SourceFile* sourceFile, Node* newSpecifier,
										   Node* namedImports, int index) {
	NamedImports* namedImportsNode = namedImports->as<NamedImports>();
	std::vector<Node*>& elements = namedImportsNode->Elements->nodes;

	Node* prevSpecifier = nullptr;
	if (index > 0 && index - 1 < static_cast<int>(elements.size())) {
		prevSpecifier = elements[index - 1];
	}
	if (prevSpecifier != nullptr) {
		InsertNodeInListAfter(sourceFile, prevSpecifier, newSpecifier, nullptr);
	} else {
		InsertNodeBefore(
			sourceFile, elements[0], newSpecifier,
			!positionsAreOnSameLine(astnav::getStartOfNode(elements[0], sourceFile, false),
									astnav::getStartOfNode(namedImports->parent->parent, sourceFile, false),
									sourceFile),
			LeadingTriviaOptionNone);
	}
}

// InsertAtTopOfFile — tracker.go:533
void Tracker::InsertAtTopOfFile(SourceFile* sourceFile, std::vector<Node*> insert,
								bool blankLineBetween) {
	if (insert.empty()) {
		return;
	}

	int pos = getInsertionPositionAtSourceFileTop(sourceFile);
	int originalPos = pos;
	// A content mapper may synthesize a header. Advance to the first writable
	// segment so the insertion maps exactly to the original file, and use its
	// original position when deciding leading trivia.
	if (spanmap::SpanMap* spanMap = sourceFile->SpanMap(); spanMap != nullptr) {
		for (const spanmap::Segment& segment : spanmap::Segments(spanMap)) {
			if (segment.Kind != spanmap::KindVerbatim || segment.VirtualEnd <= pos) {
				continue;
			}
			if (segment.VirtualStart > pos) {
				pos = segment.VirtualStart;
			}
			originalPos = static_cast<int>(segment.OriginalStart + pos - segment.VirtualStart);
			break;
		}
	}
	NodeOptions options;
	if (originalPos != 0) {
		options.Prefix = newLine;
	}
	if (sourceFile->Text().empty() ||
		!isLineBreak(static_cast<unsigned char>(sourceFile->Text()[pos]))) {
		options.Suffix = newLine;
	}
	if (blankLineBetween) {
		options.Suffix += newLine;
	}

	if (insert.size() == 1) {
		InsertNodeAt(sourceFile, static_cast<TextPos>(pos), insert[0], options);
	} else {
		InsertNodesAt(sourceFile, static_cast<TextPos>(pos), std::move(insert), options);
	}
}

// InsertMemberAtStart — tracker.go:570
void Tracker::InsertMemberAtStart(SourceFile* sourceFile, Node* node, Node* newElement) {
	insertNodeAtStartWorker(sourceFile, node, newElement);
}

// insertNodeAtStartWorker — tracker.go:574
void Tracker::insertNodeAtStartWorker(SourceFile* sourceFile, Node* node, Node* newElement) {
	int indentation = tryComputeIndentationFromExistingMembers(sourceFile, node);
	if (indentation < 0) {
		indentation = tryComputeIndentationForNewMember(sourceFile, node);
	}

	NodeList* members = getMembersOrProperties(node);
	if (members == nullptr) {
		return;
	}

	InsertNodeAt(sourceFile, static_cast<TextPos>(members->pos()), newElement,
				 getInsertNodeAtStartInsertOptions(sourceFile, node, indentation));
}

// tryComputeIndentationForNewMember — tracker.go:588
int Tracker::tryComputeIndentationForNewMember(SourceFile* sourceFile, Node* node) {
	int nodeStart = astnav::getStartOfNode(node, sourceFile, false);
	int lineStart = format::GetLineStartPositionForPosition(nodeStart, sourceFile);

	int tabSize = formatSettings.TabSize;
	if (tabSize <= 0) {
		tabSize = 4;
	}

	int indentSize = formatSettings.IndentSize;
	if (indentSize <= 0) {
		indentSize = 4;
	}
	return std::max(findIndentationColumn(sourceFile->Text(), lineStart, nodeStart, tabSize), 0) +
		indentSize;
}

// tryComputeIndentationFromExistingMembers — tracker.go:603
int Tracker::tryComputeIndentationFromExistingMembers(SourceFile* sourceFile, Node* node) {
	NodeList* members = getMembersOrProperties(node);
	if (members == nullptr) {
		return -1;
	}

	int indentation = -1;
	const std::string& text = sourceFile->Text();
	int tabSize = formatSettings.TabSize;
	Node* last = node;

	if (tabSize <= 0) {
		tabSize = 4;
	}

	for (Node* member : members->nodes) {
		if (member == nullptr) {
			continue;
		}
		if (printer::RangeStartPositionsAreOnSameLine(last->loc, member->loc, sourceFile)) {
			return -1;
		}

		int memberStart = astnav::getStartOfNode(member, sourceFile, false);
		int lineStart = format::GetLineStartPositionForPosition(memberStart, sourceFile);
		int column = findIndentationColumn(text, lineStart, memberStart, tabSize);
		if (column < 0) {
			return -1;
		}

		if (indentation >= 0) {
			if (indentation != column) {
				return -1;
			}
			last = member;
			continue;
		}

		indentation = column;
		last = member;
	}

	return indentation;
}

// getInsertNodeAfterOptions — tracker.go:641
NodeOptions Tracker::getInsertNodeAfterOptions(SourceFile* sourceFile, Node* node) {
	const std::string& newLineChar = newLine;
	NodeOptions options;
	switch (node->kind) {
	case Kind::Parameter:
		// default opts
		options = NodeOptions{};
		break;
	case Kind::ClassDeclaration:
	case Kind::ModuleDeclaration:
		options = NodeOptions{};
		options.Prefix = newLineChar;
		options.Suffix = newLineChar;
		break;
	case Kind::VariableDeclaration:
	case Kind::StringLiteral:
	case Kind::Identifier:
		options = NodeOptions{};
		options.Prefix = ", ";
		break;
	case Kind::PropertyAssignment:
		options = NodeOptions{};
		options.Suffix = "," + newLineChar;
		break;
	case Kind::ExportKeyword:
		options = NodeOptions{};
		options.Prefix = " ";
		break;
	default:
		if (!(isStatement(node) || lsutil::detail::isClassOrTypeElement(node))) {
			// Else we haven't handled this kind of node yet -- add it
			TSC_UNREACHABLE("unimplemented node type in changeTracker.getInsertNodeAfterOptions");
		}
		options = NodeOptions{};
		options.Suffix = newLineChar;
		break;
	}
	if (node->end() == sourceFile->end() && isStatement(node)) {
		options.Prefix = newLine + options.Prefix;
	}

	return options;
}

// getOptionsForInsertNodeBefore — tracker.go:677
NodeOptions Tracker::getOptionsForInsertNodeBefore(Node* before, Node* inserted,
												   bool blankLineBetween) {
	if (isStatement(before) || lsutil::detail::isClassOrTypeElement(before)) {
		if (blankLineBetween) {
			NodeOptions o;
			o.Suffix = newLine + newLine;
			return o;
		}
		NodeOptions o;
		o.Suffix = newLine;
		return o;
	} else if (before->kind == Kind::VariableDeclaration) {
		// insert `x = 1, ` into `const x = 1, y = 2;
		NodeOptions o;
		o.Suffix = ", ";
		return o;
	} else if (before->kind == Kind::Parameter) {
		if (inserted->kind == Kind::Parameter) {
			NodeOptions o;
			o.Suffix = ", ";
			return o;
		}
		return NodeOptions{};
	} else if ((before->kind == Kind::StringLiteral && before->parent != nullptr &&
				before->parent->kind == Kind::ImportDeclaration) ||
			   before->kind == Kind::NamedImports) {
		NodeOptions o;
		o.Suffix = ", ";
		return o;
	} else if (before->kind == Kind::ImportSpecifier) {
		std::string suffix = ",";
		if (blankLineBetween) {
			suffix += newLine;
		} else {
			suffix += " ";
		}
		NodeOptions o;
		o.Suffix = suffix;
		return o;
	}
	// We haven't handled this kind of node yet -- add it
	TSC_UNREACHABLE("unimplemented node type in changeTracker.getOptionsForInsertNodeBefore");
}

// getInsertNodeAtStartInsertOptions — tracker.go:705
NodeOptions Tracker::getInsertNodeAtStartInsertOptions(SourceFile* sourceFile, Node* node,
													   int indentation) {
	nodesInsertedAtStartState* state = nullptr;
	auto it = nodesWithInsertionsAtStart.find(node);
	if (it != nodesWithInsertionsAtStart.end()) {
		state = it->second;
	}
	bool hasPreviousInsertion = state != nullptr;
	if (state == nullptr) {
		state = new nodesInsertedAtStartState();
		state->node = node;
		state->sourceFile = sourceFile;
		nodesWithInsertionsAtStart[node] = state;
	}

	NodeList* members = getMembersOrProperties(node);
	bool isObjectLiteral = isObjectLiteralExpression(node);
	bool isJSON = isJsonSourceFile(sourceFile);

	bool hasMembers = members != nullptr && !members->nodes.empty();

	bool insertTrailingComma = isObjectLiteral && (hasMembers || !isJSON);
	bool insertLeadingComma = isObjectLiteral && isJSON && !hasMembers && hasPreviousInsertion;

	std::string suffix;
	if (insertTrailingComma) {
		suffix = ",";
	} else if (isInterfaceDeclaration(node) && !hasMembers) {
		suffix = ";";
	}

	std::string prefix = newLine;
	if (insertLeadingComma) {
		prefix = "," + prefix;
	}

	NodeOptions options;
	options.indentation = indentation;
	options.Prefix = prefix;
	options.Suffix = suffix;
	return options;
}

// finishNodesWithInsertionsAtStart — tracker.go:738
void Tracker::finishNodesWithInsertionsAtStart() {
	for (auto& [_, state] : nodesWithInsertionsAtStart) {
		if (state == nullptr) {
			continue;
		}

		Node* openBrace = astnav::findChildOfKind(state->node, Kind::OpenBraceToken, state->sourceFile);
		if (openBrace == nullptr) {
			continue;
		}

		Node* closeBrace = astnav::findChildOfKind(state->node, Kind::CloseBraceToken, state->sourceFile);
		if (closeBrace == nullptr) {
			continue;
		}

		NodeList* members = getMembersOrProperties(state->node);
		bool isEmpty = members == nullptr || members->nodes.empty();
		bool isSingleLine =
			positionsAreOnSameLine(openBrace->end(), closeBrace->end(), state->sourceFile);

		if (isEmpty && isSingleLine && openBrace->end() != closeBrace->end() - 1) {
			DeleteRange(state->sourceFile, newTextRange(openBrace->end(), closeBrace->end() - 1));
		}

		if (isSingleLine) {
			insertTextAt(state->sourceFile, static_cast<TextPos>(closeBrace->end() - 1), newLine);
		}
	}
}

} // namespace tsc::ls::change
