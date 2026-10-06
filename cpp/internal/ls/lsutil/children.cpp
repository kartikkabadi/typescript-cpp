// === slice: ls-foundation ===
// lsutil/children.go — last/first child and token navigation.

#include "internal/ls/lsutil/lsutil.h"

#include "internal/astnav/tokens.h"
#include "internal/scanner/scanner.h"

namespace tsc::ls::lsutil {

// GetLastChild — children.go:13. Replaces last(node.getChildren(sourceFile)).
Node* GetLastChild(Node* node, SourceFile* sourceFile) {
	Node* lastChildNode = GetLastVisitedChild(node, sourceFile);
	if (detail::isJSDocSingleCommentNode(node) && lastChildNode == nullptr) {
		return nullptr;
	}
	int tokenStartPos;
	if (lastChildNode != nullptr) {
		tokenStartPos = lastChildNode->end();
	} else {
		tokenStartPos = node->pos();
	}
	Node* lastToken = nullptr;
	Scanner scanner;
	getScannerForSourceFile(scanner, sourceFile, tokenStartPos);
	for (int startPos = tokenStartPos; startPos < node->end();) {
		Kind tokenKind = scanner.token();
		int tokenFullStart = scanner.tokenFullStart();
		int tokenEnd = scanner.tokenEnd();
		lastToken = astnav::getOrCreateToken(sourceFile, tokenKind, tokenFullStart, tokenEnd, node, scanner.tokenFlags());
		startPos = tokenEnd;
		scanner.scan();
	}
	return tsc::ifElse(lastToken != nullptr, lastToken, lastChildNode);
}

// GetLastToken — children.go:37
Node* GetLastToken(Node* node, SourceFile* sourceFile) {
	if (node == nullptr) {
		return nullptr;
	}

	if (detail::isTokenKind(node->kind) || isIdentifier(node)) {
		return nullptr;
	}

	AssertHasRealPosition(node);

	Node* lastChild = GetLastChild(node, sourceFile);
	if (lastChild == nullptr) {
		return nullptr;
	}

	if (lastChild->kind < KindFirstNode) {
		return lastChild;
	} else {
		return GetLastToken(lastChild, sourceFile);
	}
}

// GetLastVisitedChild — children.go:62. Gets the last visited child of the
// given node.
// NOTE: This doesn't include unvisited tokens; for this, use `GetLastChild` or
// `GetLastToken`.
Node* GetLastVisitedChild(Node* node, SourceFile* sourceFile) {
	Node* lastChild = nullptr;

	auto visitNode = [&lastChild](Node* n, NodeVisitor* /*visitor*/) -> Node* {
		if (n != nullptr && (n->flags & NodeFlagsReparsed) == 0) {
			lastChild = n;
		}
		return n;
	};
	auto visitNodeList = [&lastChild](NodeList* nodeList, NodeVisitor* /*visitor*/) -> NodeList* {
		if (nodeList != nullptr && !nodeList->nodes.empty()) {
			for (auto it = nodeList->nodes.rbegin(); it != nodeList->nodes.rend(); ++it) {
				Node* v = *it;
				if ((v->flags & NodeFlagsReparsed) == 0) {
					lastChild = v;
					break;
				}
			}
		}
		return nodeList;
	};

	astnav::visitEachChildAndJSDoc(node, sourceFile, visitNode, visitNodeList);
	return lastChild;
}

// GetFirstToken — children.go:87
Node* GetFirstToken(Node* node, SourceFile* sourceFile) {
	if (isIdentifier(node) || detail::isTokenKind(node->kind)) {
		return nullptr;
	}
	AssertHasRealPosition(node);
	Node* firstChild = nullptr;
	node->forEachChild([&firstChild, node](Node* n) -> bool {
		if (n == nullptr || (node->flags & NodeFlagsReparsed) != 0) {
			return false;
		}
		firstChild = n;
		return true;
	});

	int tokenEndPosition;
	if (firstChild != nullptr) {
		tokenEndPosition = firstChild->pos();
	} else {
		tokenEndPosition = node->end();
	}
	Scanner scanner;
	getScannerForSourceFile(scanner, sourceFile, node->pos());
	Node* firstToken = nullptr;
	if (node->pos() < tokenEndPosition) {
		Kind tokenKind = scanner.token();
		int tokenFullStart = scanner.tokenFullStart();
		int tokenEnd = scanner.tokenEnd();
		firstToken = astnav::getOrCreateToken(sourceFile, tokenKind, tokenFullStart, tokenEnd, node, scanner.tokenFlags());
	}

	if (firstToken != nullptr) {
		return firstToken;
	}
	if (firstChild == nullptr) {
		return nullptr;
	}
	if (firstChild->kind < KindFirstNode) {
		return firstChild;
	}
	return GetFirstToken(firstChild, sourceFile);
}

// AssertHasRealPosition — children.go:128
void AssertHasRealPosition(Node* node) {
	if (positionIsSynthesized(node->pos()) || positionIsSynthesized(node->end())) {
		TSC_UNREACHABLE("Node must have a real position for this operation.");
	}
}

} // namespace tsc::ls::lsutil
