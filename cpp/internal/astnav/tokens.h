// Port of tsc/internal/astnav/tokens.go — token/navigation helpers over the
// AST (getTouchingPropertyName and friends).
#pragma once

#include <functional>

#include "internal/ast/ast.h"

namespace tsc {
namespace astnav {

Node* getTouchingPropertyName(SourceFile* sourceFile, int position);
Node* getTouchingToken(SourceFile* sourceFile, int position);
Node* getTokenAtPosition(SourceFile* sourceFile, int position);

void visitEachChildAndJSDoc(
	Node* node,
	SourceFile* sourceFile,
	const std::function<Node*(Node*, NodeVisitor*)>& visitNode,
	const std::function<NodeList*(NodeList*, NodeVisitor*)>& visitNodes);

// Finds the leftmost token satisfying `position < token.End()`.
// If the leftmost token satisfying `position < token.End()` is invalid, or if
// position is in the trivia of that leftmost token, we will find the rightmost
// valid token with `token.End() <= position`.
Node* findPrecedingToken(SourceFile* sourceFile, int position);
Node* findPrecedingTokenEx(SourceFile* sourceFile, int position,
						   Node* startNode, bool excludeJSDoc);

int getStartOfNode(Node* node, SourceFile* file, bool includeJSDoc);

Node* findNextToken(Node* previousToken, Node* parent, SourceFile* file);

// findChildOfKind searches for a child node or token of the specified kind
// within a containing node. This function scans through both AST nodes and
// intervening tokens to find the first match.
Node* findChildOfKind(Node* containingNode, Kind kind, SourceFile* sourceFile);

// ast.go:2904 — SourceFile.GetOrCreateToken. Gets a token from the file's token
// cache, or creates it if it does not already exist. This function should NOT
// be used for creating synthetic tokens that are not in the file in the first
// place.
Node* getOrCreateToken(SourceFile* file, Kind kind, int pos, int end,
					   Node* parent, TokenFlags flags);

}  // namespace astnav
}  // namespace tsc
