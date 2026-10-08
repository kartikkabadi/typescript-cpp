// Port of tsc/internal/format/span.go.
#include "internal/format/format.h"

#include <algorithm>

#include "internal/astnav/tokens.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::format {

/** find node that fully contains given text range */
Node* findEnclosingNode(TextRange r, SourceFile* sourceFile) {
	auto find = [&](auto&& findRef, Node* n) -> Node* {
		Node* candidate = nullptr;
		n->forEachChild([&](Node* c) -> bool {
			if ((c->flags & NodeFlagsReparsed) != 0) {
				return false;
			}
			if (r.containedBy(withTokenStart(c, sourceFile))) {
				candidate = c;
				return true;
			}
			return false;
		});
		if (candidate != nullptr) {
			Node* result = findRef(findRef, candidate);
			if (result != nullptr) {
				return result;
			}
		}

		return n;
	};
	return find(find, sourceFile->asNode());
}

/**
 * Start of the original range might fall inside the comment - scanner will not yield appropriate results
 * This function will look for token that is located before the start of target range
 * and return its end as start position for the scanner.
 */
int getScanStartPosition(Node* enclosingNode, TextRange originalRange, SourceFile* sourceFile) {
	TextRange adjusted = withTokenStart(enclosingNode, sourceFile);
	int start = adjusted.pos();
	if (start == originalRange.pos() && enclosingNode->end() == originalRange.end()) {
		return start;
	}

	// exclude JSDoc so the scan never starts inside a JSDoc comment
	Node* precedingToken =
		astnav::findPrecedingTokenEx(sourceFile, originalRange.pos(), nullptr /*startNode*/,
									 true /*excludeJSDoc*/);
	if (precedingToken == nullptr) {
		// no preceding token found - start from the beginning of enclosing node
		return enclosingNode->pos();
	}

	// preceding token ends after the start of original range (i.e when originalRange.pos falls in the middle of literal)
	// start from the beginning of enclosingNode to handle the entire 'originalRange'
	if (precedingToken->end() >= originalRange.pos()) {
		return enclosingNode->pos();
	}

	return precedingToken->end();
}

/*
 * For cases like
 * if (a ||
 *     b ||$
 *     c) {...}
 * If we hit Enter at $ we want line '    b ||' to be indented.
 * Formatting will be applied to the last two lines.
 * Node that fully encloses these lines is binary expression 'a ||...'.
 * Initial indentation for this node will be 0.
 * Binary expressions don't introduce new indentation scopes, however it is possible
 * that some parent node on the same line does - like if statement in this case.
 * Note that we are considering parents only from the same line with initial node -
 * if parent is on the different line - its delta was already contributed
 * to the initial indentation.
 */
int getOwnOrInheritedDelta(Node* n, const lsutil::FormatCodeSettings& options,
						   SourceFile* sourceFile) {
	int previousLine = -1;
	Node* child = nullptr;
	while (n != nullptr) {
		int line =
			getECMALineOfPosition(sourceFile, withTokenStart(n, sourceFile).pos());
		if (previousLine != -1 && line != previousLine) {
			break;
		}

		if (ShouldIndentChildNode(options, n, child, sourceFile)) {
			return options.IndentSize; // !!! nil check???
		}

		previousLine = line;
		child = n;
		n = n->parent;
	}
	return 0;
}

bool rangeHasNoErrors(TextRange) {
	return false;
}

rangeContainsErrorFunc prepareRangeContainsErrorFunction(const std::vector<Diagnostic*>& errors,
														 TextRange originalRange) {
	if (errors.empty()) {
		return rangeHasNoErrors;
	}

	// pick only errors that fall in range
	std::vector<Diagnostic*> sorted;
	for (Diagnostic* d : errors) {
		if (originalRange.overlaps(d->Loc())) {
			sorted.push_back(d);
		}
	}
	if (sorted.empty()) {
		return rangeHasNoErrors;
	}
	std::stable_sort(sorted.begin(), sorted.end(),
					 [](Diagnostic* a, Diagnostic* b) { return a->Pos() - b->Pos() < 0; });

	int index = 0;
	return [sorted, index](TextRange r) mutable -> bool {
		// in current implementation sequence of arguments [r1, r2...] is monotonically increasing.
		// 'index' tracks the index of the most recent error that was checked.
		for (;;) {
			if (index >= static_cast<int>(sorted.size())) {
				// all errors in the range were already checked -> no error in specified range
				return false;
			}

			Diagnostic* err = sorted[index];

			if (r.end() <= err->Pos()) {
				// specified range ends before the error referred by 'index' - no error in range
				return false;
			}

			if (r.overlaps(err->Loc())) {
				// specified range overlaps with error range
				return true;
			}

			index++;
		}
		return false; // unreachable
	};
}

formatSpanWorker* newFormatSpanWorker(FormatRequestContext ctx, TextRange originalRange,
									  Node* enclosingNode, int initialIndentation, int delta,
									  FormatRequestKind requestKind,
									  rangeContainsErrorFunc rangeContainsError,
									  SourceFile* sourceFile) {
	formatSpanWorker* w = new formatSpanWorker();
	w->ctx = std::move(ctx);
	w->originalRange = originalRange;
	w->enclosingNode = enclosingNode;
	w->initialIndentation = initialIndentation;
	w->delta = delta;
	w->requestKind = requestKind;
	w->rangeContainsError = std::move(rangeContainsError);
	w->sourceFile = sourceFile;
	w->currentRules.reserve(32); // increaseInsertionIndex should assert there are no more than 32 rules in a given bucket
	return w;
}

int getNonDecoratorTokenPosOfNode(Node* node, SourceFile* file) {
	Node* lastDecorator = nullptr;
	if (hasDecorators(node)) {
		std::vector<Node*> modifierNodes = node->modifierNodes();
		for (auto it = modifierNodes.rbegin(); it != modifierNodes.rend(); ++it) {
			if (isDecorator(*it)) {
				lastDecorator = *it;
				break;
			}
		}
	}
	if (file == nullptr) {
		file = getSourceFileOfNode(node);
	}
	if (lastDecorator == nullptr) {
		return withTokenStart(node, file).pos();
	}
	return skipTrivia(file->text, lastDecorator->end());
}

std::vector<TextChange> formatSpanWorker::execute(formattingScanner* s) {
	fmtScanner = s;
	indentationOnLastIndentedLine = -1;
	lastIndentedLine = -1;
	lsutil::FormatCodeSettings opt = GetFormatCodeSettingsFromContext(ctx);
	formattingContext.reset(NewFormattingContext(sourceFile, requestKind, opt));
	// formatting context is used by rules provider
	visitor = std::unique_ptr<NodeVisitor>(newNodeVisitor(
		[this](Node* child) -> Node* {
			if (child == nullptr) {
				return child;
			}
			processChildNode(visitingNode, visitingIndenter, visitingNodeStartLine,
							 visitingUndecoratedNodeStartLine, child, -1, visitingNode,
							 visitingIndenter, visitingNodeStartLine,
							 visitingUndecoratedNodeStartLine, false, false);
			return child;
		},
		nullptr,
		NodeVisitorHooks{
			.visitNodes = [this](NodeList* nodes, NodeVisitor* /*v*/) -> NodeList* {
				if (nodes == nullptr) {
					return nodes;
				}
				processChildNodes(visitingNode, visitingIndenter, visitingNodeStartLine,
								  visitingUndecoratedNodeStartLine, nodes, visitingNode,
								  visitingNodeStartLine, visitingIndenter);
				return nodes;
			},
		}));

	fmtScanner->advance();

	if (fmtScanner->isOnToken()) {
		int startLine = getECMALineOfPosition(
			sourceFile, withTokenStart(enclosingNode, sourceFile).pos());
		int undecoratedStartLine = startLine;
		if (hasDecorators(enclosingNode)) {
			undecoratedStartLine = getECMALineOfPosition(
				sourceFile, getNonDecoratorTokenPosOfNode(enclosingNode, sourceFile));
		}

		processNode(enclosingNode, enclosingNode, startLine, undecoratedStartLine,
					initialIndentation, delta);
	}

	// Leading trivia items get attached to and processed with the token that proceeds them. If the
	// range ends in the middle of some leading trivia, the token that proceeds them won't be in the
	// range and thus won't get processed. So we process those remaining trivia items here.
	std::vector<TextRangeWithKind> remainingTrivia = fmtScanner->getCurrentLeadingTrivia();
	if (!remainingTrivia.empty()) {
		int indentation = initialIndentation;
		if (NodeWillIndentChild(formattingContext->Options, enclosingNode, nullptr, sourceFile,
								false)) {
			indentation += opt.IndentSize; // !!! TODO: nil check???
		}

		indentTriviaItems(remainingTrivia, indentation, true, [&](TextRangeWithKind item) {
			auto [startLine, startChar] =
				getECMALineAndByteOffsetOfPosition(sourceFile, item.Loc.pos());
			processRange(item, startLine, startChar, enclosingNode, enclosingNode, nullptr);
			insertIndentation(item.Loc.pos(), indentation, false);
		});

		if (tristateIsTrue(opt.TrimTrailingWhitespace)) {
			trimTrailingWhitespacesForRemainingRange(remainingTrivia);
		}
	}

	if (!(previousRange == NewTextRangeWithKind(0, 0, Kind::Unknown)) &&
		fmtScanner->getTokenFullStart() >= originalRange.end()) {
		// Formatting edits happen by looking at pairs of contiguous tokens (see `processPair`),
		// typically inserting or deleting whitespace between them. The recursive `processNode`
		// logic above bails out as soon as it encounters a token that is beyond the end of the
		// range we're supposed to format (or if we reach the end of the file). But this potentially
		// leaves out an edit that would occur *inside* the requested range but cannot be discovered
		// without looking at one token *beyond* the end of the range: consider the line `x = { }`
		// with a selection from the beginning of the line to the space inside the curly braces,
		// inclusive. We would expect a format-selection would delete the space (if rules apply),
		// but in order to do that, we need to process the pair ["{", "}"], but we stopped processing
		// just before getting there. This block handles this trailing edit.
		TextRangeWithKind tokenInfo;
		bool hasTokenInfo = false;
		if (fmtScanner->isOnEOF()) {
			tokenInfo = fmtScanner->readEOFTokenRange();
			hasTokenInfo = true;
		} else if (fmtScanner->isOnToken()) {
			tokenInfo = fmtScanner->readTokenInfo(enclosingNode).token;
			hasTokenInfo = true;
		}

		if (hasTokenInfo && tokenInfo.Loc.pos() == previousRangeTriviaEnd) {
			// We need to check that tokenInfo and previousRange are contiguous: the `originalRange`
			// may have ended in the middle of a token, which means we will have stopped formatting
			// on that token, leaving `previousRange` pointing to the token before it, but already
			// having moved the formatting scanner (where we just got `tokenInfo`) to the next token.
			// If this happens, our supposed pair [previousRange, tokenInfo] actually straddles the
			// token that intersects the end of the range we're supposed to format, so the pair will
			// produce bogus edits if we try to `processPair`. Recall that the point of this logic is
			// to perform a trailing edit at the end of the selection range: but there can be no valid
			// edit in the middle of a token where the range ended, so if we have a non-contiguous
			// pair here, we're already done and we can ignore it.
			Node* parent = astnav::findPrecedingToken(sourceFile, tokenInfo.Loc.end());
			if (parent != nullptr) {
				parent = parent->parent;
			}
			if (parent == nullptr) {
				parent = previousParent;
			}
			int line = getECMALineOfPosition(sourceFile, tokenInfo.Loc.pos());
			processPair(tokenInfo,
						line,
						parent,
						previousRange,
						previousRangeStartLine,
						previousParent,
						parent,
						nullptr);
		}
	}

	return edits;
}

int formatSpanWorker::processChildNode(
	Node* node,
	dynamicIndenter* indenter,
	int nodeStartLine,
	int undecoratedNodeStartLine,
	Node* child,
	int inheritedIndentation,
	Node* parent,
	dynamicIndenter* parentDynamicIndentation,
	int parentStartLine,
	int undecoratedParentStartLine,
	bool isListItem,
	bool isFirstListItem) {
	TSC_ASSERT(!nodeIsSynthesized(child), "child is not synthesized");

	if (nodeIsMissing(child) || (child->flags & NodeFlagsReparsed) != 0) {
		return inheritedIndentation;
	}
	int childStartPos = getTokenPosOfNode(child, sourceFile, false);
	int childStartLine = getECMALineOfPosition(sourceFile, childStartPos);

	int undecoratedChildStartLine = childStartLine;
	if (hasDecorators(child)) {
		undecoratedChildStartLine =
			getECMALineOfPosition(sourceFile,
								  getNonDecoratorTokenPosOfNode(child, sourceFile));
	}

	bool isErrorMemberListElement =
		(child->flags & NodeFlagsThisNodeHasError) != 0 && isMemberListElement(parent, child);
	// if child is a list item - try to get its indentation, only if parent is within the original range.
	int childIndentationAmount = -1;

	if (!isErrorMemberListElement && isListItem && parent->loc.containedBy(originalRange)) {
		childIndentationAmount = tryComputeIndentationForListItem(childStartPos, child->end(),
																  parentStartLine, originalRange,
																  inheritedIndentation);
		if (childIndentationAmount != -1) {
			inheritedIndentation = childIndentationAmount;
		}
	}

	// child node is outside the target range - do not dive inside
	if (!originalRange.overlaps(child->loc)) {
		if (child->end() < originalRange.pos()) {
			fmtScanner->skipToEndOf(&child->loc);
		}
		return inheritedIndentation;
	}

	if (child->loc.len() == 0) {
		return inheritedIndentation;
	}

	while (fmtScanner->isOnToken() && fmtScanner->getTokenFullStart() < originalRange.end()) {
		// proceed any parent tokens that are located prior to child.getStart()
		tokenInfo t = fmtScanner->readTokenInfo(node);
		if (t.token.Loc.end() > originalRange.end()) {
			return inheritedIndentation;
		}
		if (t.token.Loc.end() > childStartPos) {
			if (t.token.Loc.pos() > childStartPos) {
				fmtScanner->skipToStartOf(&child->loc);
			}
			// stop when formatting scanner advances past the beginning of the child
			break;
		}

		consumeTokenAndAdvanceScanner(t, node, parentDynamicIndentation, node, false);
	}

	if (!fmtScanner->isOnToken() || fmtScanner->getTokenFullStart() >= originalRange.end()) {
		return inheritedIndentation;
	}

	if (child->kind >= KindFirstToken && child->kind <= KindLastToken) {
		// if child node is a token, it does not impact indentation, proceed it using parent indentation scope rules
		tokenInfo t = fmtScanner->readTokenInfo(child);
		// JSX text shouldn't affect indenting
		if (child->kind != Kind::JsxText) {
			TSC_ASSERT(t.token.Loc.end() == child->loc.end(), "Token end is child end");
			consumeTokenAndAdvanceScanner(t, node, parentDynamicIndentation, child, false);
			return inheritedIndentation;
		}
	}

	int effectiveParentStartLine = undecoratedParentStartLine;
	if (child->kind == Kind::Decorator) {
		effectiveParentStartLine = childStartLine;
	}
	int childIndentation = 0;
	int delta = 0;
	if (isErrorMemberListElement) {
		childIndentation = getCurrentIndentationAtPosition(childStartPos);
	} else {
		auto [ci, d] = computeIndentation(child, childStartLine, childIndentationAmount, node,
										  parentDynamicIndentation, effectiveParentStartLine);
		childIndentation = ci;
		delta = d;
	}

	processNode(child, childContextNode, childStartLine, undecoratedChildStartLine, childIndentation,
				delta);

	childContextNode = node;

	if (isFirstListItem && parent->kind == Kind::ArrayLiteralExpression &&
		inheritedIndentation == -1) {
		inheritedIndentation = childIndentation;
	}

	return inheritedIndentation;
}

void formatSpanWorker::processChildNodes(
	Node* node,
	dynamicIndenter* indenter,
	int nodeStartLine,
	int undecoratedNodeStartLine,
	NodeList* nodes,
	Node* parent,
	int parentStartLine,
	dynamicIndenter* parentDynamicIndentation) {
	TSC_ASSERT(nodes != nullptr, "nodes != nullptr");
	TSC_ASSERT(!positionIsSynthesized(nodes->pos()), "nodes pos not synthesized");
	TSC_ASSERT(!positionIsSynthesized(nodes->end()), "nodes end not synthesized");

	Kind listStartToken = getOpenTokenForList(parent, nodes);

	dynamicIndenter* listDynamicIndentation = parentDynamicIndentation;
	int startLine = parentStartLine;

	// node range is outside the target range - do not dive inside
	if (!originalRange.overlaps(nodes->loc)) {
		if (nodes->end() < originalRange.pos() &&
			(nodes->nodes.empty() || (nodes->nodes[0]->flags & NodeFlagsReparsed) == 0)) {
			fmtScanner->skipToEndOf(&nodes->loc);
		}
		return;
	}

	if (listStartToken != Kind::Unknown) {
		// introduce a new indentation scope for lists (including list start and end tokens)
		while (fmtScanner->isOnToken() && fmtScanner->getTokenFullStart() < originalRange.end()) {
			tokenInfo t = fmtScanner->readTokenInfo(parent);
			if (t.token.Loc.end() > nodes->pos()) {
				// stop when formatting scanner moves past the beginning of node list
				break;
			} else if (t.token.kind == listStartToken) {
				// consume list start token
				startLine = getECMALineOfPosition(sourceFile, t.token.Loc.pos());

				consumeTokenAndAdvanceScanner(t, parent, parentDynamicIndentation, parent, false);

				int indentationOnListStartToken = 0;
				if (indentationOnLastIndentedLine != -1) {
					// scanner just processed list start token so consider last indentation as list indentation
					// function foo(): { // last indentation was 0, list item will be indented based on this value
					//   foo: number;
					// }: {};
					indentationOnListStartToken = indentationOnLastIndentedLine;
				} else {
					indentationOnListStartToken =
						getCurrentIndentationAtPosition(t.token.Loc.pos());
				}

				listDynamicIndentation =
					getDynamicIndentation(parent, parentStartLine, indentationOnListStartToken,
										  formattingContext->Options.IndentSize);
			} else {
				// consume any tokens that precede the list as child elements of 'node' using its indentation scope
				consumeTokenAndAdvanceScanner(t, parent, parentDynamicIndentation, parent, false);
			}
		}
	}

	int inheritedIndentation = -1;
	for (size_t i = 0; i < nodes->nodes.size(); i++) {
		Node* child = nodes->nodes[i];
		inheritedIndentation = processChildNode(node, indenter, nodeStartLine,
												undecoratedNodeStartLine, child,
												inheritedIndentation, node, listDynamicIndentation,
												startLine, startLine, true, i == 0);
	}

	Kind listEndToken = getCloseTokenForOpenToken(listStartToken);
	if (listEndToken != Kind::Unknown && fmtScanner->isOnToken() &&
		fmtScanner->getTokenFullStart() < originalRange.end()) {
		tokenInfo t = fmtScanner->readTokenInfo(parent);
		if (t.token.kind == Kind::CommaToken) {
			// consume the comma
			consumeTokenAndAdvanceScanner(t, parent, listDynamicIndentation, parent, false);
			if (fmtScanner->isOnToken()) {
				t = fmtScanner->readTokenInfo(parent);
			} else {
				return;
			}
		}

		// consume the list end token only if it is still belong to the parent
		// there might be the case when current token matches end token but does not considered as one
		// function (x: function) <--
		// without this check close paren will be interpreted as list end token for function expression which is wrong
		if (t.token.kind == listEndToken && t.token.Loc.containedBy(parent->loc)) {
			// consume list end token
			consumeTokenAndAdvanceScanner(t, parent, listDynamicIndentation,
											parent /*isListEndToken*/, true);
		}
	}
}

void formatSpanWorker::executeProcessNodeVisitor(Node* node, dynamicIndenter* indenter,
												 int nodeStartLine,
												 int undecoratedNodeStartLine) {
	Node* oldNode = visitingNode;
	dynamicIndenter* oldIndenter = visitingIndenter;
	int oldStart = visitingNodeStartLine;
	int oldUndecoratedStart = visitingUndecoratedNodeStartLine;
	visitingNode = node;
	visitingIndenter = indenter;
	visitingNodeStartLine = nodeStartLine;
	visitingUndecoratedNodeStartLine = undecoratedNodeStartLine;
	node->visitEachChild(*visitor);
	visitingNode = oldNode;
	visitingIndenter = oldIndenter;
	visitingNodeStartLine = oldStart;
	visitingUndecoratedNodeStartLine = oldUndecoratedStart;
}

int formatSpanWorker::getCurrentIndentationAtPosition(int pos) {
	int startLinePosition = GetLineStartPositionForPosition(pos, sourceFile);
	return FindFirstNonWhitespaceColumn(startLinePosition, pos, sourceFile,
									  formattingContext->Options);
}

std::pair<int, int> formatSpanWorker::computeIndentation(Node* node, int startLine,
														 int inheritedIndentation, Node* parent,
														 dynamicIndenter* parentDynamicIndentation,
														 int effectiveParentStartLine) {
	int indentation;
	int delta = 0;
	if (ShouldIndentChildNode(formattingContext->Options, node, nullptr, nullptr)) {
		delta = formattingContext->Options.IndentSize;
	}

	if (effectiveParentStartLine == startLine) {
		// if node is located on the same line with the parent
		// - inherit indentation from the parent
		// - push children if either parent of node itself has non-zero delta
		indentation = indentationOnLastIndentedLine;
		if (startLine != lastIndentedLine) {
			indentation = parentDynamicIndentation->getIndentation();
		}
		delta = std::min(formattingContext->Options.IndentSize,
						 parentDynamicIndentation->getDelta(node) + delta);
		return {indentation, delta};
	} else if (inheritedIndentation == -1) {
		if (node->kind == Kind::OpenParenToken && startLine == lastIndentedLine) {
			// the is used for chaining methods formatting
			// - we need to get the indentation on last line and the delta of parent
			return {indentationOnLastIndentedLine, parentDynamicIndentation->getDelta(node)};
		} else if (childStartsOnTheSameLineWithElseInIfStatement(parent, node, startLine,
															   sourceFile) ||
				   childIsUnindentedBranchOfConditionalExpression(parent, node, startLine,
																  sourceFile) ||
				   argumentStartsOnSameLineAsPreviousArgument(parent, node, startLine, sourceFile)) {
			return {parentDynamicIndentation->getIndentation(), delta};
		} else {
			int i = parentDynamicIndentation->getIndentation();
			if (i == -1) {
				return {parentDynamicIndentation->getIndentation(), delta};
			}
			return {i + parentDynamicIndentation->getDelta(node), delta};
		}
	}

	return {inheritedIndentation, delta};
}

/** Tries to compute the indentation for a list element.
* If list element is not in range then
* function will pick its actual indentation
* so it can be pushed downstream as inherited indentation.
* If list element is in the range - its indentation will be equal
* to inherited indentation from its predecessors.
 */
int formatSpanWorker::tryComputeIndentationForListItem(int startPos, int endPos,
													   int parentStartLine, TextRange r,
													   int inheritedIndentation) {
	TextRange r2{TextPos(startPos), TextPos(endPos)};
	if (r.overlaps(r2) || r2.containedBy(r)) { /* Not to miss zero-range nodes e.g. JsxText */
		if (inheritedIndentation != -1) {
			return inheritedIndentation;
		}
	} else {
		int startLine = getECMALineOfPosition(sourceFile, startPos);
		int column = getCurrentIndentationAtPosition(startPos);
		if (startLine != parentStartLine || startPos == column) {
			// Use the base indent size if it is greater than
			// the indentation of the inherited predecessor.
			int baseIndentSize = formattingContext->Options.BaseIndentSize;
			if (baseIndentSize > column) {
				return baseIndentSize;
			}
			return column;
		}
	}
	return -1;
}

void formatSpanWorker::processNode(Node* node, Node* contextNode, int nodeStartLine,
								   int undecoratedNodeStartLine, int indentation, int delta) {
	if (!originalRange.overlaps(withTokenStart(node, sourceFile))) {
		return;
	}

	dynamicIndenter* nodeDynamicIndentation =
		getDynamicIndentation(node, nodeStartLine, indentation, delta);

	// a useful observations when tracking context node
	//        /
	//      [a]
	//   /   |   \
	//  [b] [c] [d]
	// node 'a' is a context node for nodes 'b', 'c', 'd'
	// except for the leftmost leaf token in [b] - in this case context node ('e') is located somewhere above 'a'
	// this rule can be applied recursively to child nodes of 'a'.
	//
	// context node is set to parent node value after processing every child node
	// context node is set to parent of the token after processing every token

	childContextNode = contextNode;

	// if there are any tokens that logically belong to node and interleave child nodes
	// such tokens will be consumed in processChildNode for the child that follows them
	executeProcessNodeVisitor(node, nodeDynamicIndentation, nodeStartLine,
							  undecoratedNodeStartLine);

	// proceed any tokens in the node that are located after child nodes
	while (fmtScanner->isOnToken() && fmtScanner->getTokenFullStart() < originalRange.end()) {
		tokenInfo t = fmtScanner->readTokenInfo(node);
		if (t.token.Loc.end() > std::min(node->end(), originalRange.end())) {
			break;
		}
		consumeTokenAndAdvanceScanner(t, node, nodeDynamicIndentation, node, false);
	}
}

LineAction formatSpanWorker::processPair(TextRangeWithKind currentItem, int currentStartLine,
										 Node* currentParent, TextRangeWithKind previousItem,
										 int previousStartLine, Node* previousParent,
										 Node* contextNode,
										 dynamicIndenter* dynamicIndentation) {
	formattingContext->UpdateContext(previousItem, previousParent, currentItem, currentParent,
									 contextNode);

	currentRules.clear();
	currentRules = getRules(formattingContext.get(), currentRules);

	bool trimTrailingWhitespaces =
		!tristateIsFalse(formattingContext->Options.TrimTrailingWhitespace);
	LineAction lineAction = LineAction::LineActionNone;

	if (!currentRules.empty()) {
		// Apply rules in reverse order so that higher priority rules (which are first in the array)
		// win in a conflict with lower priority rules.
		for (auto it = currentRules.rbegin(); it != currentRules.rend(); ++it) {
			ruleImpl* rule = *it;

			lineAction =
				applyRuleEdits(rule, previousItem, previousStartLine, currentItem, currentStartLine);
			if (dynamicIndentation != nullptr) {
				switch (lineAction) {
				case LineAction::LineActionLineRemoved:
					// Handle the case where the next line is moved to be the end of this line.
					// In this case we don't indent the next line in the next pass.
					if (getTokenPosOfNode(currentParent, sourceFile, false) ==
						currentItem.Loc.pos()) {
						dynamicIndentation->recomputeIndentation(
							/*lineAddedByFormatting*/ false, contextNode);
					}
					break;
				case LineAction::LineActionLineAdded:
					// Handle the case where token2 is moved to the new line.
					// In this case we indent token2 in the next pass but we set
					// sameLineIndent flag to notify the indenter that the indentation is within the line.
					if (getTokenPosOfNode(currentParent, sourceFile, false) ==
						currentItem.Loc.pos()) {
						dynamicIndentation->recomputeIndentation(
							/*lineAddedByFormatting*/ true, contextNode);
					}
					break;
				default:
					TSC_ASSERT(lineAction == LineAction::LineActionNone, "line action is none");
				}
			}

			// We need to trim trailing whitespace between the tokens if they were on different lines, and no rule was applied to put them on the same line
			trimTrailingWhitespaces = trimTrailingWhitespaces &&
				(rule->Action() & ruleActionDeleteSpace) == 0 &&
				rule->Flags() != ruleFlagsCanDeleteNewLines;
		}
	} else {
		trimTrailingWhitespaces =
			trimTrailingWhitespaces && currentItem.kind != Kind::EndOfFile;
	}

	if (currentStartLine != previousStartLine && trimTrailingWhitespaces) {
		// We need to trim trailing whitespace between the tokens if they were on different lines, and no rule was applied to put them on the same line
		trimTrailingWhitespacesForLines(previousStartLine, currentStartLine, previousItem);
	}

	return lineAction;
}

LineAction formatSpanWorker::applyRuleEdits(ruleImpl* rule, TextRangeWithKind previousRange,
											int previousStartLine, TextRangeWithKind currentRange,
											int currentStartLine) {
	bool onLaterLine = currentStartLine != previousStartLine;
	switch (rule->Action()) {
	case ruleActionStopProcessingSpaceActions:
		// no action required
		return LineAction::LineActionNone;
	case ruleActionDeleteSpace:
		if (previousRange.Loc.end() != currentRange.Loc.pos()) {
			// delete characters starting from t1.end up to t2.pos exclusive
			recordDelete(previousRange.Loc.end(), currentRange.Loc.pos() - previousRange.Loc.end());
			if (onLaterLine) {
				return LineAction::LineActionLineRemoved;
			}
			return LineAction::LineActionNone;
		}
		break;
	case ruleActionDeleteToken:
		recordDelete(previousRange.Loc.pos(), previousRange.Loc.len());
		break;
	case ruleActionInsertNewLine:
		// exit early if we on different lines and rule cannot change number of newlines
		// if line1 and line2 are on subsequent lines then no edits are required - ok to exit
		// if line1 and line2 are separated with more than one newline - ok to exit since we cannot delete extra new lines
		if (rule->Flags() != ruleFlagsCanDeleteNewLines && previousStartLine != currentStartLine) {
			return LineAction::LineActionNone;
		}

		// edit should not be applied if we have one line feed between elements
		if (currentStartLine - previousStartLine != 1) {
			recordReplace(previousRange.Loc.end(), currentRange.Loc.pos() - previousRange.Loc.end(),
						  GetNewLineOrDefaultFromContext(ctx));
			if (onLaterLine) {
				return LineAction::LineActionNone;
			}
			return LineAction::LineActionLineAdded;
		}
		break;
	case ruleActionInsertSpace:
		// exit early if we on different lines and rule cannot change number of newlines
		if (rule->Flags() != ruleFlagsCanDeleteNewLines && previousStartLine != currentStartLine) {
			return LineAction::LineActionNone;
		}

		{
			int posDelta = currentRange.Loc.pos() - previousRange.Loc.end();
			if (posDelta != 1 ||
				sourceFile->text.compare(static_cast<size_t>(previousRange.Loc.end()), 1, " ") !=
					0) {
				recordReplace(previousRange.Loc.end(), posDelta, " ");
				if (onLaterLine) {
					return LineAction::LineActionLineRemoved;
				}
				return LineAction::LineActionNone;
			}
		}
		break;
	case ruleActionInsertTrailingSemicolon:
		recordInsert(previousRange.Loc.end(), ";");
		break;
	}
	return LineAction::LineActionNone;
}

LineAction formatSpanWorker::processRange(TextRangeWithKind r, int rangeStartLine,
										  int /*rangeStartCharacter*/, Node* parent,
										  Node* contextNode,
										  dynamicIndenter* dynamicIndentation) {
	bool rangeHasError = rangeContainsError(r.Loc);
	LineAction lineAction = LineAction::LineActionNone;
	if (!rangeHasError) {
		if (previousRange == NewTextRangeWithKind(0, 0, Kind::Unknown)) {
			// trim whitespaces starting from the beginning of the span up to the current line
			int originalStartLine = getECMALineOfPosition(sourceFile, originalRange.pos());
			trimTrailingWhitespacesForLines(originalStartLine, rangeStartLine,
											NewTextRangeWithKind(0, 0, Kind::Unknown));
		} else {
			lineAction = processPair(r, rangeStartLine, parent, previousRange,
									 previousRangeStartLine, previousParent, contextNode,
									 dynamicIndentation);
		}
	}

	previousRange = r;
	previousRangeTriviaEnd = r.Loc.end();
	previousParent = parent;
	previousRangeStartLine = rangeStartLine;

	return lineAction;
}

void formatSpanWorker::processTrivia(const std::vector<TextRangeWithKind>& trivia, Node* parent,
									 Node* contextNode, dynamicIndenter* dynamicIndentation) {
	for (const TextRangeWithKind& triviaItem : trivia) {
		if (isComment(triviaItem.kind) && triviaItem.Loc.containedBy(originalRange)) {
			auto [triviaItemStartLine, triviaItemStartCharacter] =
				getECMALineAndByteOffsetOfPosition(sourceFile, triviaItem.Loc.pos());
			processRange(triviaItem, triviaItemStartLine, triviaItemStartCharacter, parent,
						 contextNode, dynamicIndentation);
		}
	}
}

/**
* Trimming will be done for lines after the previous range.
* Exclude comments as they had been previously processed.
 */
void formatSpanWorker::trimTrailingWhitespacesForRemainingRange(
	const std::vector<TextRangeWithKind>& trivias) {
	int startPos = originalRange.pos();
	if (!(previousRange == NewTextRangeWithKind(0, 0, Kind::Unknown))) {
		startPos = previousRange.Loc.end();
	}

	for (const TextRangeWithKind& trivia : trivias) {
		if (isComment(trivia.kind)) {
			if (startPos < trivia.Loc.pos()) {
				trimTrailingWitespacesForPositions(startPos, trivia.Loc.pos() - 1, previousRange);
			}

			startPos = trivia.Loc.end() + 1;
		}
	}

	if (startPos < originalRange.end()) {
		trimTrailingWitespacesForPositions(startPos, originalRange.end(), previousRange);
	}
}

void formatSpanWorker::trimTrailingWitespacesForPositions(int startPos, int endPos,
														  TextRangeWithKind previousRange) {
	int startLine = getECMALineOfPosition(sourceFile, startPos);
	int endLine = getECMALineOfPosition(sourceFile, endPos);

	trimTrailingWhitespacesForLines(startLine, endLine + 1, previousRange);
}

void formatSpanWorker::trimTrailingWhitespacesForLines(int line1, int line2,
													   TextRangeWithKind r) {
	const ECMALineStarts& lineStarts = getECMALineStarts(sourceFile);
	for (int line = line1; line < line2; line++) {
		int lineStartPosition = static_cast<int>(lineStarts[line]);
		int lineEndPosition = getECMAEndLinePosition(sourceFile, line);

		// do not trim whitespaces in comments or template expression
		if (!(r == NewTextRangeWithKind(0, 0, Kind::Unknown)) &&
			(isComment(r.kind) || isStringOrRegularExpressionOrTemplateLiteral(r.kind)) &&
			r.Loc.pos() <= lineEndPosition && r.Loc.end() > lineEndPosition) {
			continue;
		}

		int whitespaceStart = getTrailingWhitespaceStartPosition(lineStartPosition, lineEndPosition);
		if (whitespaceStart != -1) {
			if (whitespaceStart != lineStartPosition) {
				int w = 0;
				char32_t ch = decodeUtf8Rune(
					std::string_view(sourceFile->text).substr(whitespaceStart - 1), &w);
				TSC_ASSERT(!isWhiteSpaceSingleLine(ch), "not whitespace");
			}
			recordDelete(whitespaceStart, lineEndPosition + 1 - whitespaceStart);
		}
	}
}

/**
* @param start The position of the first character in range
* @param end The position of the last character in range
 */
int formatSpanWorker::getTrailingWhitespaceStartPosition(int start, int end) {
	int pos = end;
	const std::string& text = sourceFile->text;
	while (pos >= start) {
		int size;
		char32_t ch = decodeUtf8Rune(std::string_view(text).substr(pos), &size);
		if (size == 0) {
			pos--; // multibyte character, rewind more
			continue;
		}
		if (!isWhiteSpaceSingleLine(ch)) {
			break;
		}
		pos--;
	}
	if (pos != end) {
		return pos + 1;
	}
	return -1;
}

bool isStringOrRegularExpressionOrTemplateLiteral(Kind kind) {
	return kind == Kind::StringLiteral || kind == Kind::RegularExpressionLiteral ||
		isTemplateLiteralKind(kind);
}

bool isComment(Kind kind) {
	return kind == Kind::SingleLineCommentTrivia || kind == Kind::MultiLineCommentTrivia;
}

void formatSpanWorker::insertIndentation(int pos, int indentation, bool lineAdded) {
	std::string indentationString = getIndentationString(indentation, formattingContext->Options);
	if (lineAdded) {
		// new line is added before the token by the formatting rules
		// insert indentation string at the very beginning of the token
		recordReplace(pos, 0, indentationString);
	} else {
		auto [tokenStartLine, tokenStartCharacter] =
			getECMALineAndByteOffsetOfPosition(sourceFile, pos);
		int startLinePosition =
			static_cast<int>(getECMALineStarts(sourceFile)[tokenStartLine]);
		if (indentation != characterToColumn(startLinePosition, tokenStartCharacter) ||
			indentationIsDifferent(indentationString, startLinePosition)) {
			recordReplace(startLinePosition, tokenStartCharacter, indentationString);
		}
	}
}

int formatSpanWorker::characterToColumn(int startLinePosition, int characterInLine) {
	int column = 0;
	for (int i = 0; i < characterInLine; i++) {
		if (sourceFile->text[startLinePosition + i] == '\t') {
			if (formattingContext->Options.TabSize > 0) {
				column += formattingContext->Options.TabSize -
					(column % formattingContext->Options.TabSize);
			}
		} else {
			column++;
		}
	}
	return column;
}

bool formatSpanWorker::indentationIsDifferent(const std::string& indentationString,
											  int startLinePosition) {
	const std::string& text = sourceFile->text;
	size_t end = startLinePosition + indentationString.size();
	if (end > text.size()) {
		return true;
	}
	return text.compare(startLinePosition, indentationString.size(), indentationString) != 0;
}

bool formatSpanWorker::indentTriviaItems(const std::vector<TextRangeWithKind>& trivia,
										 int commentIndentation, bool indentNextTokenOrTrivia,
										 std::function<void(TextRangeWithKind)> indentSingleLine) {
	for (const TextRangeWithKind& triviaItem : trivia) {
		bool triviaInRange = triviaItem.Loc.containedBy(originalRange);
		switch (triviaItem.kind) {
		case Kind::MultiLineCommentTrivia:
			if (triviaInRange) {
				indentMultilineComment(triviaItem.Loc, commentIndentation,
									   !indentNextTokenOrTrivia, true);
			}
			indentNextTokenOrTrivia = false;
			break;
		case Kind::SingleLineCommentTrivia:
			if (indentNextTokenOrTrivia && triviaInRange) {
				indentSingleLine(triviaItem);
			}
			indentNextTokenOrTrivia = false;
			break;
		case Kind::NewLineTrivia:
			indentNextTokenOrTrivia = true;
			break;
		default:
			break;
		}
	}
	return indentNextTokenOrTrivia;
}

void formatSpanWorker::indentMultilineComment(TextRange commentRange, int indentation,
											  bool firstLineIsIndented, bool indentFinalLine) {
	// split comment in lines
	int startLine = getECMALineOfPosition(sourceFile, commentRange.pos());
	int endLine = getECMALineOfPosition(sourceFile, commentRange.end());

	if (startLine == endLine) {
		if (!firstLineIsIndented) {
			// treat as single line comment
			insertIndentation(commentRange.pos(), indentation, false);
		}
		return;
	}

	std::vector<TextRange> parts;
	parts.reserve(static_cast<size_t>(
		std::count(sourceFile->text.begin() + commentRange.pos(),
				   sourceFile->text.begin() + commentRange.end(), '\n')));
	int startPos = commentRange.pos();
	for (int line = startLine; line < endLine; line++) {
		int endOfLine = getECMAEndLinePosition(sourceFile, line);
		parts.push_back(TextRange{TextPos(startPos), TextPos(endOfLine)});
		startPos = static_cast<int>(getECMALineStarts(sourceFile)[line + 1]);
	}

	if (indentFinalLine) {
		parts.push_back(TextRange{TextPos(startPos), TextPos(commentRange.end())});
	}

	if (parts.empty()) {
		return;
	}

	int startLinePos = static_cast<int>(getECMALineStarts(sourceFile)[startLine]);

	auto [nonWhitespaceInFirstPartCharacter, nonWhitespaceInFirstPartColumn] =
		findFirstNonWhitespaceCharacterAndColumn(startLinePos, parts[0].pos(), sourceFile,
												 formattingContext->Options);

	int startIndex = 0;

	if (firstLineIsIndented) {
		startIndex = 1;
		startLine++;
	}

	// shift all parts on the delta size
	int delta = indentation - nonWhitespaceInFirstPartColumn;
	for (int i = startIndex; i < static_cast<int>(parts.size()); i++) {
		int partStartLinePos = static_cast<int>(getECMALineStarts(sourceFile)[startLine]);
		int nonWhitespaceCharacter = nonWhitespaceInFirstPartCharacter;
		int nonWhitespaceColumn = nonWhitespaceInFirstPartColumn;
		if (i != 0) {
			auto [ch, col] =
				findFirstNonWhitespaceCharacterAndColumn(parts[i].pos(), parts[i].end(),
														 sourceFile, formattingContext->Options);
			nonWhitespaceCharacter = ch;
			nonWhitespaceColumn = col;
		}
		int newIndentation = nonWhitespaceColumn + delta;
		if (newIndentation > 0) {
			std::string indentationString =
				getIndentationString(newIndentation, formattingContext->Options);
			recordReplace(partStartLinePos, nonWhitespaceCharacter, indentationString);
		} else {
			recordDelete(partStartLinePos, nonWhitespaceCharacter);
		}

		startLine++;
	}
}

std::string getIndentationString(int indentation, const lsutil::FormatCodeSettings& options) {
	// go's `strings.Repeat` already has static, global caching for repeated tabs and spaces, so there's no need to cache here like in strada
	if (!tristateIsTrue(options.ConvertTabsToSpaces)) {
		if (options.TabSize == 0) {
			return "";
		}
		int tabs = indentation / options.TabSize;
		int spaces = indentation - (tabs * options.TabSize);
		std::string res(tabs, '\t');
		if (spaces > 0) {
			res += std::string(spaces, ' ');
		}

		return res;
	} else {
		return std::string(indentation, ' ');
	}
}

TextChange createTextChangeFromStartLength(int start, int length, std::string newText) {
	TextChange c;
	c.NewText = std::move(newText);
	c.pos_ = TextPos(start);
	c.end_ = TextPos(start + length);
	return c;
}

void formatSpanWorker::recordDelete(int start, int length) {
	if (length != 0) {
		edits.push_back(createTextChangeFromStartLength(start, length, ""));
	}
}

void formatSpanWorker::recordReplace(int start, int length, const std::string& newText) {
	if (length != 0 || !newText.empty()) {
		edits.push_back(createTextChangeFromStartLength(start, length, newText));
	}
}

void formatSpanWorker::recordInsert(int start, const std::string& text) {
	if (!text.empty()) {
		edits.push_back(createTextChangeFromStartLength(start, 0, text));
	}
}

void formatSpanWorker::consumeTokenAndAdvanceScanner(tokenInfo currentTokenInfo, Node* parent,
													 dynamicIndenter* dynamicIndenation,
													 Node* container, bool isListEndToken) {
	// assert(currentTokenInfo.token.Loc.ContainedBy(parent.Loc)) // !!!
	bool lastTriviaWasNewLine = fmtScanner->lastTrailingTriviaWasNewLine();
	bool indentToken = false;

	if (!currentTokenInfo.leadingTrivia.empty()) {
		processTrivia(currentTokenInfo.leadingTrivia, parent, childContextNode,
					  dynamicIndenation);
	}

	LineAction lineAction = LineAction::LineActionNone;
	bool isTokenInRange = currentTokenInfo.token.Loc.containedBy(originalRange);

	auto [tokenStartLine, tokenStartChar] =
		getECMALineAndByteOffsetOfPosition(sourceFile, currentTokenInfo.token.Loc.pos());

	if (isTokenInRange) {
		bool rangeHasError = rangeContainsError(currentTokenInfo.token.Loc);
		// save previousRange since processRange will overwrite this value with current one
		TextRangeWithKind savePreviousRange = previousRange;
		lineAction = processRange(currentTokenInfo.token, tokenStartLine, tokenStartChar, parent,
								  childContextNode, dynamicIndenation);
		// do not indent comments\token if token range overlaps with some error
		if (!rangeHasError) {
			if (lineAction == LineAction::LineActionNone) {
				// indent token only if end line of previous range does not match start line of the token
				if (!(savePreviousRange == NewTextRangeWithKind(0, 0, Kind::Unknown))) {
					int prevEndLine =
						getECMALineOfPosition(sourceFile, savePreviousRange.Loc.end());
					indentToken = lastTriviaWasNewLine && tokenStartLine != prevEndLine;
				} else {
					// When there's no previous range (first token), TS sets prevEndLine to undefined.
					// tokenStart.line !== undefined is always true in JS, so indentToken = lastTriviaWasNewLine.
					indentToken = lastTriviaWasNewLine;
				}
			} else {
				indentToken = lineAction == LineAction::LineActionLineAdded;
			}
		}
	}

	if (!currentTokenInfo.trailingTrivia.empty()) {
		previousRangeTriviaEnd = currentTokenInfo.trailingTrivia.back().Loc.end();
		// If any trailing comment trivia extends past the original range, it won't be
		// processed by processTrivia (which skips comments not contained by originalRange).
		// Cap previousRangeTriviaEnd before such comments so the trailing edit contiguity
		// check in execute() won't pair across unprocessed comment content.
		for (const TextRangeWithKind& trivia : currentTokenInfo.trailingTrivia) {
			if (isComment(trivia.kind) && !trivia.Loc.containedBy(originalRange)) {
				previousRangeTriviaEnd = trivia.Loc.pos();
				break;
			}
		}
		processTrivia(currentTokenInfo.trailingTrivia, parent, childContextNode,
					  dynamicIndenation);
	}

	if (indentToken) {
		int tokenIndentation = -1;
		if (isTokenInRange && !rangeContainsError(currentTokenInfo.token.Loc)) {
			tokenIndentation =
				dynamicIndenation->getIndentationForToken(tokenStartLine,
														  currentTokenInfo.token.kind, container,
														  isListEndToken);
		}
		bool indentNextTokenOrTrivia = true;
		if (!currentTokenInfo.leadingTrivia.empty()) {
			int commentIndentation =
				dynamicIndenation->getIndentationForComment(currentTokenInfo.token.kind,
															tokenIndentation, container);
			indentNextTokenOrTrivia =
				indentTriviaItems(currentTokenInfo.leadingTrivia, commentIndentation,
								  indentNextTokenOrTrivia, [&](TextRangeWithKind item) {
									  insertIndentation(item.Loc.pos(), commentIndentation, false);
								  });
		}

		// indent token only if is it is in target range and does not overlap with any error ranges
		if (tokenIndentation != -1 && indentNextTokenOrTrivia) {
			insertIndentation(currentTokenInfo.token.Loc.pos(), tokenIndentation,
							  lineAction == LineAction::LineActionLineAdded);

			lastIndentedLine = tokenStartLine;
			indentationOnLastIndentedLine = tokenIndentation;
		}
	}

	fmtScanner->advance();

	childContextNode = parent;
}

int dynamicIndenter::getIndentationForComment(Kind kind, int tokenIndentation, Node* container) {
	switch (kind) {
	// preceding comment to the token that closes the indentation scope inherits the indentation from the scope
	// ..  {
	//     // comment
	// }
	case Kind::CloseBraceToken:
	case Kind::CloseBracketToken:
	case Kind::CloseParenToken:
		return indentation + getDelta(container);
	default:
		break;
	}
	if (tokenIndentation != -1) {
		return tokenIndentation;
	}
	return indentation;
}

// if list end token is LessThanToken '>' then its delta should be explicitly suppressed
// so that LessThanToken as a binary operator can still be indented.
// foo.then
//
//	<
//	    number,
//	    string,
//	>();
//
// vs
// var a = xValue
//
//	> yValue;
int dynamicIndenter::getIndentationForToken(int line, Kind kind, Node* container,
											bool suppressDelta) {
	if (!suppressDelta && shouldAddDelta(line, kind, container)) {
		return indentation + getDelta(container);
	}
	return indentation;
}

int dynamicIndenter::getIndentation() {
	return indentation;
}

int dynamicIndenter::getDelta(Node* child) {
	// Delta value should be zero when the node explicitly prevents indentation of the child node
	if (NodeWillIndentChild(options, node, child, sourceFile, true)) {
		return delta;
	}
	return 0;
}

void dynamicIndenter::recomputeIndentation(bool lineAdded, Node* parent) {
	if (ShouldIndentChildNode(options, parent, node, sourceFile)) {
		if (lineAdded) {
			indentation += options.IndentSize; // !!! no nil check???
		} else {
			indentation -= options.IndentSize; // !!! no nil check???
		}
		if (ShouldIndentChildNode(options, node, nullptr, nullptr)) {
			delta = options.IndentSize;
		} else {
			delta = 0;
		}
	}
}

bool dynamicIndenter::shouldAddDelta(int line, Kind kind, Node* container) {
	switch (kind) {
	// open and close brace, 'else' and 'while' (in do statement) tokens has indentation of the parent
	case Kind::OpenBraceToken:
	case Kind::CloseBraceToken:
	case Kind::CloseParenToken:
	case Kind::ElseKeyword:
	case Kind::WhileKeyword:
	case Kind::AtToken:
		return false;
	case Kind::SlashToken:
	case Kind::GreaterThanToken:
		switch (container->kind) {
		case Kind::JsxOpeningElement:
		case Kind::JsxClosingElement:
		case Kind::JsxSelfClosingElement:
			return false;
		default:
			break;
		}
		break;
	case Kind::OpenBracketToken:
	case Kind::CloseBracketToken:
		if (container->kind != Kind::MappedType) {
			return false;
		}
		break;
	default:
		break;
	}
	// if token line equals to the line of containing node (this is a first token in the node) - use node indentation
	return nodeStartLine != line &&
		// if this token is the first token following the list of decorators, we do not need to indent
		!(hasDecorators(node) && kind == getFirstNonDecoratorTokenOfNode(node));
}

Kind getFirstNonDecoratorTokenOfNode(Node* node) {
	if (canHaveModifiers(node)) {
		std::vector<Node*> modifierNodes = node->modifierNodes();
		size_t i = 0;
		while (i < modifierNodes.size() && isDecorator(modifierNodes[i])) {
			i++;
		}
		Node* modifier = nullptr;
		for (; i < modifierNodes.size(); i++) {
			if (isModifier(modifierNodes[i])) {
				modifier = modifierNodes[i];
				break;
			}
		}
		if (modifier != nullptr) {
			return modifier->kind;
		}
	}

	switch (node->kind) {
	case Kind::ClassDeclaration:
		return Kind::ClassKeyword;
	case Kind::InterfaceDeclaration:
		return Kind::InterfaceKeyword;
	case Kind::FunctionDeclaration:
		return Kind::FunctionKeyword;
	case Kind::EnumDeclaration:
		return Kind::EnumDeclaration;
	case Kind::GetAccessor:
		return Kind::GetKeyword;
	case Kind::SetAccessor:
		return Kind::SetKeyword;
	case Kind::MethodDeclaration:
		if (node->as<MethodDeclaration>()->AsteriskToken != nullptr) {
			return Kind::AsteriskToken;
		}
		[[fallthrough]];

	case Kind::PropertyDeclaration:
	case Kind::Parameter: {
		Node* name = getNameOfDeclaration(node);
		if (name != nullptr) {
			return name->kind;
		}
		break;
	}
	default:
		break;
	}

	return Kind::Unknown;
}

dynamicIndenter* formatSpanWorker::getDynamicIndentation(Node* node, int nodeStartLine,
														 int indentation, int delta) {
	indenterPool.push_back(dynamicIndenter{
		node,
		nodeStartLine,
		indentation,
		delta,
		formattingContext->Options,
		sourceFile,
	});
	return &indenterPool.back();
}

} // namespace tsc::format
