// Port of tsc/internal/astnav/tokens.go — token/navigation helpers over the
// AST (getTouchingPropertyName and friends).
//
// Missing ast/core leaf helpers this file needs are replicated file-locally in
// the anonymous namespace below (internal linkage — removed when their owner
// ports a canonical definition).

#include "internal/astnav/tokens.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "internal/scanner/scanner.h"

namespace tsc {
namespace astnav {

namespace {

// ---------------------------------------------------------------------------
// ast/core helpers not yet ported to a canonical home (utilities.go,
// ast_generated.go, ast.go, core/*) — file-local replicas.
// ---------------------------------------------------------------------------

// ast_generated.go:9862 — ast.IsTokenKind
bool isTokenKind(Kind kind) {
	return kind >= KindFirstToken && kind <= KindLastToken;
}

// utilities.go:2134 — ast.IsJSDocTag
bool isJSDocTag(Node* node) {
	return node->kind >= KindFirstJSDocTagNode &&
	       node->kind <= KindLastJSDocTagNode;
}

// utilities.go:2205 — ast.IsWhitespaceOnlyJsxText
bool isWhitespaceOnlyJsxText(Node* node) {
	return node->kind == Kind::JsxText &&
	       node->as<JsxText>()->ContainsOnlyTriviaWhiteSpaces;
}

// utilities.go:2201 — ast.IsNonWhitespaceToken
bool isNonWhitespaceToken(Node* node) {
	return isTokenKind(node->kind) && !isWhitespaceOnlyJsxText(node);
}

// utilities.go:89 — ast.FindLastVisibleNode
Node* findLastVisibleNode(const std::vector<Node*>& nodes) {
	int fromEnd = 1;
	int n = static_cast<int>(nodes.size());
	while (fromEnd <= n &&
	       (nodes[n - fromEnd]->flags & NodeFlagsReparsed) != 0) {
		fromEnd++;
	}
	if (fromEnd <= n) {
		return nodes[n - fromEnd];
	}
	return nullptr;
}

// utilities.go:3848 — ast.hasComment (unexported; guards Node::commentList,
// which is unreachable for kinds not in this list).
bool hasComment(Kind kind) {
	switch (kind) {
	case Kind::JSDoc:
	case Kind::JSDocUnknownTag:
	case Kind::JSDocAugmentsTag:
	case Kind::JSDocImplementsTag:
	case Kind::JSDocDeprecatedTag:
	case Kind::JSDocPublicTag:
	case Kind::JSDocPrivateTag:
	case Kind::JSDocProtectedTag:
	case Kind::JSDocReadonlyTag:
	case Kind::JSDocOverrideTag:
	case Kind::JSDocCallbackTag:
	case Kind::JSDocOverloadTag:
	case Kind::JSDocParameterTag:
	case Kind::JSDocPropertyTag:
	case Kind::JSDocReturnTag:
	case Kind::JSDocThisTag:
	case Kind::JSDocTypeTag:
	case Kind::JSDocTemplateTag:
	case Kind::JSDocTypedefTag:
	case Kind::JSDocSeeTag:
	case Kind::JSDocThrowsTag:
	case Kind::JSDocSatisfiesTag:
	case Kind::JSDocImportTag:
		return true;
	default:
		return false;
	}
}

// utilities.go:3162 — ast.IsJSDocSingleCommentNode. In Strada, if a JSDoc node
// has a single comment, that comment is represented as a string property as a
// simplification, and therefore that comment is not visited by forEachChild.
bool isJSDocSingleCommentNode(Node* node) {
	return hasComment(node->kind) && node->commentList() != nullptr &&
	       node->commentList()->nodes.size() == 1;
}

// utilities.go:3144 — ast.IsJSDocSingleCommentNodeList.
bool isJSDocSingleCommentNodeList(NodeList* nodeList) {
	if (nodeList == nullptr || nodeList->nodes.empty()) {
		return false;
	}
	Node* parent = nodeList->nodes[0]->parent;
	if (parent == nullptr) {
		return false;
	}
	return isJSDocSingleCommentNode(parent) &&
	       nodeList == parent->commentList();
}

// utilities.go:3156 — ast.IsJSDocSingleCommentNodeComment.
bool isJSDocSingleCommentNodeComment(Node* node) {
	if (node == nullptr || node->parent == nullptr) {
		return false;
	}
	return isJSDocSingleCommentNode(node->parent) &&
	       node == node->parent->commentList()->nodes[0];
}

// ast.go:38 / utilities.go:3056 — visitNodes + ast.ForEachChildAndJSDoc.
bool forEachChildAndJSDoc(Node* node, SourceFile* sourceFile,
                          const std::function<bool(Node*)>& v) {
	for (Node* n : node->jsDoc(sourceFile)) {
		if (v(n)) {
			return true;
		}
	}
	return node->forEachChild(v);
}

// core.BinarySearchUniqueFunc — like a binary search, but assumes only one
// element in the slice could match the target; the comparison is passed the
// current index of the element being compared.
template <class F>
std::pair<int, bool> binarySearchUniqueFunc(const std::vector<Node*>& x, F cmp) {
	int n = static_cast<int>(x.size());
	if (n == 0) {
		return {0, false};
	}
	int low = 0;
	int high = n - 1;
	while (low <= high) {
		int middle = low + ((high - low) >> 1);
		int value = cmp(middle, x[middle]);
		if (value < 0) {
			low = middle + 1;
		} else if (value > 0) {
			high = middle - 1;
		} else {
			return {middle, true};
		}
	}
	return {low, false};
}

// core.Filter
template <class F>
std::vector<Node*> filterNodes(const std::vector<Node*>& slice, F f) {
	std::vector<Node*> result;
	for (Node* n : slice) {
		if (f(n)) {
			result.push_back(n);
		}
	}
	return result;
}

// ast.go:2935 — ast.createToken (unexported). `kind` should be a token kind.
Node* createToken(Kind kind, SourceFile* file, int pos, int end,
                  TokenFlags flags) {
	if (file->tokenFactory == nullptr) {
		file->tokenFactory = new NodeFactory(NodeFactoryHooks{});
	}
	std::string text = file->text.substr(pos, end - pos);
	switch (kind) {
	case Kind::NumericLiteral:
		return file->tokenFactory->newNumericLiteral(text, flags);
	case Kind::BigIntLiteral:
		return file->tokenFactory->newBigIntLiteral(text, flags);
	case Kind::StringLiteral:
		return file->tokenFactory->newStringLiteral(text, flags);
	case Kind::JsxText:
	case Kind::JsxTextAllWhiteSpaces:
		return file->tokenFactory->newJsxText(
			text, kind == Kind::JsxTextAllWhiteSpaces);
	case Kind::RegularExpressionLiteral:
		return file->tokenFactory->newRegularExpressionLiteral(text, flags);
	case Kind::NoSubstitutionTemplateLiteral:
		return file->tokenFactory->newNoSubstitutionTemplateLiteral(text, flags);
	case Kind::TemplateHead:
		return file->tokenFactory->newTemplateHead(text, "" /*rawText*/, flags);
	case Kind::TemplateMiddle:
		return file->tokenFactory->newTemplateMiddle(text, "" /*rawText*/, flags);
	case Kind::TemplateTail:
		return file->tokenFactory->newTemplateTail(text, "" /*rawText*/, flags);
	case Kind::Identifier:
		return file->tokenFactory->newIdentifier(text);
	case Kind::PrivateIdentifier:
		return file->tokenFactory->newPrivateIdentifier(text);
	default: // Punctuation and keywords
		return file->tokenFactory->newToken(kind);
	}
}

// ---------------------------------------------------------------------------
// tokens.go — file-private functions
// ---------------------------------------------------------------------------

// Forward declarations (Go has no ordering requirement).
NodeVisitor* getNodeVisitor(
	const std::function<Node*(Node*, NodeVisitor*)>& visitNode,
	const std::function<NodeList*(NodeList*, NodeVisitor*)>& visitNodes);
bool shouldSkipChild(Node* node);

bool shouldRescanLessThanLessThanToken(Scanner& s, Node* containingNode,
                                       Kind token) {
	return token == Kind::LessThanLessThanToken && isJsxChild(containingNode);
}

Kind scanNavigationToken(Scanner& s, Node* containingNode) {
	Kind token = s.token();
	if (shouldRescanLessThanLessThanToken(s, containingNode, token)) {
		return s.reScanJsxToken(true /*allowMultilineJsxText*/);
	}
	return token;
}

int getPosition(Node* node, SourceFile* sourceFile,
                bool allowPositionInLeadingTrivia) {
	if (allowPositionInLeadingTrivia) {
		return node->pos();
	}
	return getTokenPosOfNode(node, sourceFile, true /*includeJSDoc*/);
}

// tokens.go:38 — getTokenAtPosition returns a token at the given position in
// the source file. The token can be a real node in the AST, or a synthesized
// token constructed with information from the scanner. Synthesized tokens are
// only created when needed, and they are stored in the source file's token
// cache such that multiple calls to getTokenAtPosition with the same position
// will return the same object in memory. If there is no token at the given
// position (possible when `allowPositionInLeadingTrivia` is false), the lowest
// node that encloses the position is returned.
Node* getTokenAtPositionImpl(
	SourceFile* sourceFile,
	int position,
	bool allowPositionInLeadingTrivia,
	const std::function<bool(Node*)>& includePrecedingTokenAtEndPosition) {
	// `next` tracks the node whose children will be visited on the next
	// iteration. `prevSubtree` is a node whose end position is equal to the
	// target position, only if `includePrecedingTokenAtEndPosition` is
	// provided. Once set, the next iteration of the loop will test the
	// rightmost token of `prevSubtree` to see if it should be returned.
	Node* next = nullptr;
	Node* prevSubtree = nullptr;
	Node* current = sourceFile->asNode();
	// `left` tracks the lower boundary of the node/token that could be
	// returned, and is eventually the scanner's start position, if the scanner
	// is used.
	int left = 0;
	// `nodeAfterLeft` tracks the first node we visit after visiting the node
	// that advances `left`. When scanning in between nodes for token, we should
	// only scan up to the start of `nodeAfterLeft`.
	Node* nodeAfterLeft = nullptr;

	auto getIncludedPrecedingToken = [&](Node* subtree) -> Node* {
		Node* child = findPrecedingTokenEx(sourceFile, position, subtree,
		                                 false /*excludeJSDoc*/);
		if (child != nullptr && child->end() == position &&
		    includePrecedingTokenAtEndPosition(child)) {
			return child;
		}
		return nullptr;
	};

	auto testNode = [&](Node* node) -> int {
		if (node->kind != Kind::EndOfFile && node->end() == position &&
		    includePrecedingTokenAtEndPosition != nullptr &&
		    (node->flags & NodeFlagsReparsed) == 0) {
			if (prevSubtree != nullptr &&
			    getIncludedPrecedingToken(prevSubtree) != nullptr) {
				return 0;
			}
			prevSubtree = node;
		}

		// A node "contains" the position if position < end, except nodes at the
		// file end treat end as inclusive (there's nowhere else to look). This
		// applies to the EOF token itself, and to JSDoc nodes reaching EOF
		// (e.g. unterminated JSDoc comments).
		if (node->end() < position ||
		    (node->end() == position && node->kind != Kind::EndOfFile &&
		     (!isJSDocKind(node->kind) ||
		      node->end() != sourceFile->EndOfFileToken->end()))) {
			return -1;
		}
		int nodePos = getPosition(node, sourceFile, allowPositionInLeadingTrivia);
		if (nodePos > position) {
			return 1;
		}
		return 0;
	};

	// We zero in on the node that contains the target position by visiting each
	// child and JSDoc comment of the current node. Node children are walked in
	// order, while node lists are binary searched.
	auto visitNode = [&](Node* node, NodeVisitor*) -> Node* {
		// We can't abort visiting children, so once a match is found, we set
		// `next` and do nothing on subsequent visits.
		if (node == nullptr || (node->flags & NodeFlagsReparsed) != 0) {
			return nullptr;
		}
		if (nodeAfterLeft == nullptr) {
			nodeAfterLeft = node;
		}
		if (next == nullptr) {
			int result = testNode(node);
			switch (result) {
			case -1:
				if (!isJSDocKind(node->kind)) {
					// We can't move the left boundary into or beyond JSDoc,
					// because we may end up returning the token after this
					// JSDoc, constructing it with the scanner, and we need to
					// include all its leading trivia in its position.
					left = node->end();
				}
				nodeAfterLeft = nullptr;
				break;
			case 0:
				next = node;
				break;
			}
		}
		return node;
	};

	auto visitNodeList = [&](NodeList* nodeList, NodeVisitor*) -> NodeList* {
		if (nodeList == nullptr || nodeList->nodes.empty()) {
			return nodeList;
		}
		if (nodeAfterLeft == nullptr) {
			for (Node* node : nodeList->nodes) {
				if ((node->flags & NodeFlagsReparsed) == 0) {
					nodeAfterLeft = node;
					break;
				}
			}
		}
		if (next == nullptr) {
			if (nodeList->end() == position &&
			    includePrecedingTokenAtEndPosition != nullptr) {
				left = nodeList->end();
				nodeAfterLeft = nullptr;
				for (auto it = nodeList->nodes.rbegin();
				     it != nodeList->nodes.rend(); ++it) {
					if (((*it)->flags & NodeFlagsReparsed) == 0) {
						prevSubtree = *it;
						break;
					}
				}
			} else if (nodeList->end() <= position) {
				left = nodeList->end();
				nodeAfterLeft = nullptr;
			} else if (nodeList->pos() <= position) {
				std::vector<Node*> nodes = nodeList->nodes;
				std::pair<int, bool> res =
					binarySearchUniqueFunc(nodes, [&](int middle, Node* node) -> int {
						if ((node->flags & NodeFlagsReparsed) != 0) {
							return 0;
						}
						int cmp = testNode(node);
						if (cmp < 0) {
							left = node->end();
							nodeAfterLeft = nullptr;
							for (int i = middle + 1;
							     i < static_cast<int>(nodes.size()); i++) {
								if ((nodes[i]->flags & NodeFlagsReparsed) == 0) {
									nodeAfterLeft = nodes[i];
									break;
								}
							}
						}
						return cmp;
					});
				int index = res.first;
				bool match = res.second;
				if (match && (nodes[index]->flags & NodeFlagsReparsed) != 0) {
					// filter and search again
					nodes = filterNodes(nodes, [](Node* node) {
						return (node->flags & NodeFlagsReparsed) == 0;
					});
					res = binarySearchUniqueFunc(
						nodes, [&](int middle, Node* node) -> int {
							int cmp = testNode(node);
							if (cmp < 0) {
								left = node->end();
								if (middle + 1 < static_cast<int>(nodes.size())) {
									nodeAfterLeft = nodes[middle + 1];
								} else {
									nodeAfterLeft = nullptr;
								}
							}
							return cmp;
						});
					index = res.first;
					match = res.second;
				}
				if (match) {
					next = nodes[index];
				}
			}
		}
		return nodeList;
	};

	for (;;) {
		visitEachChildAndJSDoc(current, sourceFile, visitNode, visitNodeList);
		// If prevSubtree was set on the last iteration, it ends at the target
		// position. Check if the rightmost token of prevSubtree should be
		// returned based on the `includePrecedingTokenAtEndPosition` callback.
		if (prevSubtree != nullptr) {
			if (Node* child = getIncludedPrecedingToken(prevSubtree);
			    child != nullptr) {
				// Optimization: includePrecedingTokenAtEndPosition only ever
				// returns true for real AST nodes, so we don't run the scanner
				// here.
				return child;
			}
			prevSubtree = nullptr;
		}

		// No node was found that contains the target position, so we've gone as
		// deep as we can in the AST. We've either found a token, or we need to
		// run the scanner to construct one that isn't stored in the AST.
		if (next == nullptr) {
			if (isTokenKind(current->kind) || shouldSkipChild(current)) {
				return current;
			}
			Scanner scanner;
			getScannerForSourceFile(scanner, sourceFile, left);
			int end = current->end();
			// We should only scan up to the start of the next node in the AST
			// after the node ending at position `left`. It is necessary to
			// enforce this invariant in cases where `position` occurs in
			// between two node/tokens, such that we would not find a token in
			// the loop below before we reach the next node. We can fall into
			// this case when `allowPositionInLeadingTrivia` is false and
			// `position` is in a leading trivia, or when `position` would be in
			// the leading trivia of a node but this node is inside JSDoc:
			// ```
			// /**
			//  * @type {{
			//  */*$*/ identifier: boolean;
			//  * }}
			//  */
			// ```
			// The position of marker '$' falls in between the asterisk token
			// and the identifier token, but is not part of the leading trivia
			// for `identifier`.
			if (nodeAfterLeft != nullptr) {
				end = nodeAfterLeft->pos();
			}
			while (left < end) {
				Kind token = scanNavigationToken(scanner, current);
				int tokenFullStart = scanner.tokenFullStart();
				int tokenStart = ifElse(allowPositionInLeadingTrivia,
				                        tokenFullStart, scanner.tokenStart());
				int tokenEnd = scanner.tokenEnd();
				TokenFlags flags = scanner.tokenFlags();
				if (tokenEnd > end) {
					break;
				}
				if (tokenStart <= position && position < tokenEnd) {
					if (token == Kind::Identifier || !isTokenKind(token)) {
						if (isJSDocKind(current->kind)) {
							return current;
						}
						std::string msg =
							"did not expect " +
							std::string(kindToString(current->kind)) +
							" to have " + std::string(kindToString(token)) +
							" in its trivia";
						TSC_UNREACHABLE(msg.c_str());
					}
					return getOrCreateToken(sourceFile, token, tokenFullStart,
					                        tokenEnd, current, flags);
				}
				if (includePrecedingTokenAtEndPosition != nullptr &&
				    tokenEnd == position) {
					Node* prevToken =
						getOrCreateToken(sourceFile, token, tokenFullStart,
				                         tokenEnd, current, flags);
					if (includePrecedingTokenAtEndPosition(prevToken)) {
						return prevToken;
					}
				}
				left = tokenEnd;
				scanner.scan();
			}
			return current;
		}
		current = next;
		left = current->pos();
		nodeAfterLeft = nullptr;
		next = nullptr;
	}
}

// tokens.go:283 — findRightmostNode (defined but unused in the Go package too;
// kept for parity with the source).
[[maybe_unused]] Node* findRightmostNode(Node* node) {
	Node* next = nullptr;
	Node* current = node;
	auto visitNode = [&](Node* node, NodeVisitor*) -> Node* {
		if (node != nullptr) {
			next = node;
		}
		return node;
	};
	auto visitNodes = [&](NodeList* nodeList, NodeVisitor*) -> NodeList* {
		if (nodeList != nullptr) {
			if (Node* rightmost = findLastVisibleNode(nodeList->nodes);
			    rightmost != nullptr) {
				next = rightmost;
			}
		}
		return nodeList;
	};
	std::unique_ptr<NodeVisitor> visitor(
		getNodeVisitor(visitNode, visitNodes));

	for (;;) {
		current->visitEachChild(*visitor);
		if (next == nullptr) {
			return current;
		}
		current = next;
		next = nullptr;
	}
}

constexpr int comparisonLessThan = -1;
constexpr int comparisonEqualTo = 0;
constexpr int comparisonGreaterThan = 1;

// tokens.go:465 — isValidPrecedingNode
bool isValidPrecedingNode(Node* node, SourceFile* sourceFile) {
	if (node->kind == Kind::EndOfFile) {
		return !node->jsDoc(sourceFile).empty();
	}
	int start = getStartOfNode(node, sourceFile, false /*includeJSDoc*/);
	int width = node->end() - start;
	return !(isWhitespaceOnlyJsxText(node) || width == 0);
}

// Looks for rightmost valid token in the range [startPos, endPos). If position
// is >= 0, looks for rightmost valid token that precedes or touches that
// position.
Node* findRightmostValidToken(int endPos, SourceFile* sourceFile,
                              Node* containingNode, int position,
                              bool excludeJSDoc) {
	if (position == -1) {
		position = containingNode->end();
	}
	std::function<Node*(Node*, int)> find = [&](Node* n, int endPos) -> Node* {
		if (n == nullptr) {
			return nullptr;
		}
		if (isNonWhitespaceToken(n)) {
			return n;
		}

		Node* rightmostValidNode = nullptr;
		// Nodes after the last valid node.
		std::vector<Node*> rightmostVisitedNodes;
		rightmostVisitedNodes.reserve(1);
		bool hasChildren = false;
		auto shouldVisitNode = [&](Node* node) -> bool {
			// Node is synthetic or out of the desired range: don't visit it.
			return !((node->flags & NodeFlagsReparsed) != 0 ||
			         node->end() > endPos ||
			         getStartOfNode(node, sourceFile,
			                        !excludeJSDoc /*includeJSDoc*/) >= position);
		};
		auto visitNode = [&](Node* node, NodeVisitor*) -> Node* {
			if (node == nullptr || (node->flags & NodeFlagsReparsed) != 0) {
				return node;
			}
			hasChildren = true;
			if (!shouldVisitNode(node)) {
				return node;
			}
			rightmostVisitedNodes.push_back(node);
			if (isValidPrecedingNode(node, sourceFile)) {
				rightmostValidNode = node;
				rightmostVisitedNodes.clear();
			}
			return node;
		};
		auto visitNodes = [&](NodeList* nodeList, NodeVisitor*) -> NodeList* {
			if (nodeList != nullptr && !nodeList->nodes.empty()) {
				hasChildren = true;
				std::pair<int, bool> res = binarySearchUniqueFunc(
					nodeList->nodes, [&](int middle, Node* node) -> int {
						if (node->end() > endPos) {
							return comparisonGreaterThan;
						}
						return comparisonLessThan;
					});
				int index = res.first;
				int validIndex = -1;
				for (int i = index - 1; i >= 0; i--) {
					if (!shouldVisitNode(nodeList->nodes[i])) {
						continue;
					}
					if (isValidPrecedingNode(nodeList->nodes[i], sourceFile)) {
						validIndex = i;
						rightmostValidNode = nodeList->nodes[i];
						break;
					}
				}
				for (int i = validIndex + 1; i < index; i++) {
					if (!shouldVisitNode(nodeList->nodes[i])) {
						continue;
					}
					rightmostVisitedNodes.push_back(nodeList->nodes[i]);
				}
			}
			return nodeList;
		};
		visitEachChildAndJSDoc(n, sourceFile, visitNode, visitNodes);

		// Three cases:
		// 1. The answer is a token of `rightmostValidNode`.
		// 2. The answer is one of the unvisited tokens that occur after the
		//    rightmost valid node.
		// 3. The current node is a childless, token-less node. The answer is
		//    the current node.

		// Case 2: Look at unvisited trailing tokens that occur in between the
		// rightmost visited nodes.
		if (!shouldSkipChild(n)) { // JSDoc nodes don't include trivia tokens as
			                     // children.
			int startPos;
			if (rightmostValidNode != nullptr) {
				startPos = rightmostValidNode->end();
			} else {
				startPos = n->pos();
			}
			Scanner scanner;
			getScannerForSourceFile(scanner, sourceFile, startPos);
			std::vector<Node*> tokens;
			for (Node* visitedNode : rightmostVisitedNodes) {
				// Trailing tokens that occur before this node.
				while (startPos < std::min(visitedNode->pos(), position)) {
					Kind token = scanNavigationToken(scanner, n);
					int tokenStart = scanner.tokenStart();
					if (tokenStart >= std::min(visitedNode->pos(), position)) {
						break;
					}
					int tokenFullStart = scanner.tokenFullStart();
					int tokenEnd = scanner.tokenEnd();
					startPos = tokenEnd;
					TokenFlags flags = scanner.tokenFlags();
					tokens.push_back(getOrCreateToken(
						sourceFile, token, tokenFullStart, tokenEnd, n, flags));
					scanner.scan();
				}
				startPos = visitedNode->end();
				scanner.resetPos(startPos);
				scanner.scan();
			}
			// Trailing tokens after last visited node.
			while (startPos < std::min(endPos, position)) {
				Kind token = scanNavigationToken(scanner, n);
				int tokenStart = scanner.tokenStart();
				if (tokenStart >= std::min(endPos, position)) {
					break;
				}
				int tokenFullStart = scanner.tokenFullStart();
				int tokenEnd = scanner.tokenEnd();
				startPos = tokenEnd;
				TokenFlags flags = scanner.tokenFlags();
				tokens.push_back(getOrCreateToken(sourceFile, token,
				                                  tokenFullStart, tokenEnd, n,
				                                  flags));
				scanner.scan();
			}

			int lastToken = static_cast<int>(tokens.size()) - 1;
			// Find preceding valid token.
			for (int i = lastToken; i >= 0; i--) {
				if (!isWhitespaceOnlyJsxText(tokens[i])) {
					return tokens[i];
				}
			}
		}

		// Case 3: childless node.
		if (!hasChildren) {
			if (n != containingNode) {
				return n;
			}
			return nullptr;
		}
		// Case 1: recur on rightmostValidNode.
		if (rightmostValidNode != nullptr) {
			endPos = rightmostValidNode->end();
		}
		return find(rightmostValidNode, endPos);
	};

	return find(containingNode, endPos);
}

// tokens.go:691 — getNodeVisitor
NodeVisitor* getNodeVisitor(
	const std::function<Node*(Node*, NodeVisitor*)>& visitNode,
	const std::function<NodeList*(NodeList*, NodeVisitor*)>& visitNodes) {
	std::function<Node*(Node*, NodeVisitor*)> wrappedVisitNode;
	std::function<NodeList*(NodeList*, NodeVisitor*)> wrappedVisitNodes;
	if (visitNode != nullptr) {
		wrappedVisitNode = [visitNode](Node* n, NodeVisitor* v) -> Node* {
			if (isJSDocSingleCommentNodeComment(n)) {
				return n;
			}
			return visitNode(n, v);
		};
	}

	if (visitNodes != nullptr) {
		wrappedVisitNodes = [visitNodes](NodeList* n,
		                               NodeVisitor* v) -> NodeList* {
			if (isJSDocSingleCommentNodeList(n)) {
				return n;
			}
			return visitNodes(n, v);
		};
	}

	NodeVisitorHooks hooks;
	hooks.visitNode = wrappedVisitNode;
	hooks.visitToken = wrappedVisitNode;
	hooks.visitNodes = wrappedVisitNodes;
	hooks.visitModifiers =
		[wrappedVisitNodes](ModifierList* modifiers,
	                        NodeVisitor* visitor) -> ModifierList* {
		if (modifiers != nullptr) {
			wrappedVisitNodes(static_cast<NodeList*>(modifiers), visitor);
		}
		return modifiers;
	};
	return newNodeVisitor([](Node* n) -> Node* { return n; } /*identity*/,
	                      nullptr, hooks);
}

// tokens.go:728 — shouldSkipChild
bool shouldSkipChild(Node* node) {
	return node->kind == Kind::JSDoc || node->kind == Kind::JSDocText ||
	       node->kind == Kind::JSDocTypeLiteral ||
	       node->kind == Kind::JSDocSignature || isJSDocLinkLike(node) ||
	       isJSDocTag(node);
}

}  // namespace

// ---------------------------------------------------------------------------
// tokens.go — exported functions
// ---------------------------------------------------------------------------

// ast.go:2904 — SourceFile.GetOrCreateToken. Gets a token from the file's token
// cache, or creates it if it does not already exist. This function should NOT
// be used for creating synthetic tokens that are not in the file in the first
// place.
Node* getOrCreateToken(SourceFile* file, Kind kind, int pos, int end,
                       Node* parent, TokenFlags flags) {
	std::lock_guard<std::mutex> lock(file->tokenCacheMu);
	TextRange loc{pos, end};
	TokenCacheKey key{parent, loc};
	if (auto it = file->tokenCache.find(key); it != file->tokenCache.end()) {
		Node* token = it->second;
		if (token->kind != kind) {
			std::string msg = "Token cache mismatch: " +
				std::string(kindToString(token->kind)) +
				" != " + std::string(kindToString(kind));
			TSC_UNREACHABLE(msg.c_str());
		}
		return token;
	}
	if ((parent->flags & NodeFlagsReparsed) != 0) {
		std::string msg = "Cannot create token from reparsed node of kind " +
			std::string(kindToString(parent->kind));
		TSC_UNREACHABLE(msg.c_str());
	}
	Node* token = createToken(kind, file, pos, end, flags);
	token->loc = loc;
	token->parent = parent;
	file->tokenCache[key] = token;
	return token;
}


Node* getTouchingPropertyName(SourceFile* sourceFile, int position) {
	return getTokenAtPositionImpl(
		sourceFile, position, false /*allowPositionInLeadingTrivia*/,
		[](Node* node) -> bool {
			return isPropertyNameLiteral(node) || isKeyword(node->kind) ||
			       isPrivateIdentifier(node);
		});
}

Node* getTouchingToken(SourceFile* sourceFile, int position) {
	return getTokenAtPositionImpl(sourceFile, position,
	                              false /*allowPositionInLeadingTrivia*/,
	                              nullptr);
}

Node* getTokenAtPosition(SourceFile* sourceFile, int position) {
	return getTokenAtPositionImpl(sourceFile, position,
	                              true /*allowPositionInLeadingTrivia*/,
	                              nullptr);
}

void visitEachChildAndJSDoc(
	Node* node, SourceFile* sourceFile,
	const std::function<Node*(Node*, NodeVisitor*)>& visitNode,
	const std::function<NodeList*(NodeList*, NodeVisitor*)>& visitNodes) {
	std::unique_ptr<NodeVisitor> visitor(
		getNodeVisitor(visitNode, visitNodes));
	for (Node* jsdoc : node->jsDoc(sourceFile)) {
		if (visitor->hooks.visitNode != nullptr) {
			visitor->hooks.visitNode(jsdoc, visitor.get());
		} else {
			visitor->visitNode(jsdoc);
		}
	}
	node->visitEachChild(*visitor);
}

Node* findPrecedingToken(SourceFile* sourceFile, int position) {
	return findPrecedingTokenEx(sourceFile, position, nullptr, false);
}

Node* findPrecedingTokenEx(SourceFile* sourceFile, int position,
                           Node* startNode, bool excludeJSDoc) {
	std::function<Node*(Node*)> find = [&](Node* n) -> Node* {
		if (isNonWhitespaceToken(n) && n->kind != Kind::EndOfFile) {
			return n;
		}

		// `foundChild` is the leftmost node that contains the target position.
		// `prevChild` is the last visited child of the current node.
		Node* foundChild = nullptr;
		Node* prevChild = nullptr;
		auto visitNode = [&](Node* node, NodeVisitor*) -> Node* {
			// skip synthesized nodes (that will exist now because of jsdoc
			// handling)
			if (node == nullptr || (node->flags & NodeFlagsReparsed) != 0) {
				return node;
			}
			if (foundChild != nullptr) {
				// We cannot abort visiting children, so once the desired child
				// is found, we do nothing.
				return node;
			}
			if (position < node->end() &&
			    (prevChild == nullptr || prevChild->end() <= position)) {
				foundChild = node;
			} else {
				prevChild = node;
			}
			return node;
		};
		auto visitNodes = [&](NodeList* nodeList, NodeVisitor*) -> NodeList* {
			if (foundChild != nullptr) {
				return nodeList;
			}
			if (nodeList != nullptr && !nodeList->nodes.empty()) {
				const std::vector<Node*>& nodes = nodeList->nodes;
				std::pair<int, bool> res = binarySearchUniqueFunc(
					nodes, [&](int middle, Node*) -> int {
						// synthetic jsdoc nodes should have jsdocNode.End() <=
						// n.Pos()
						if ((nodes[middle]->flags & NodeFlagsReparsed) != 0) {
							return comparisonLessThan;
						}
						if (position < nodes[middle]->end()) {
							if (middle == 0 ||
							    position >= nodes[middle - 1]->end()) {
								return comparisonEqualTo;
							}
							return comparisonGreaterThan;
						}
						return comparisonLessThan;
					});
				int index = res.first;
				bool match = res.second;

				if (match) {
					foundChild = nodes[index];
				}

				int validLookupIndex = ifElse(
					match, index - 1, static_cast<int>(nodes.size()) - 1);
				for (int i = validLookupIndex; i >= 0; i--) {
					if ((nodes[i]->flags & NodeFlagsReparsed) != 0) {
						continue;
					}
					if (prevChild == nullptr) {
						prevChild = nodes[i];
					}
				}
			}
			return nodeList;
		};
		visitEachChildAndJSDoc(n, sourceFile, visitNode, visitNodes);

		if (foundChild != nullptr) {
			// Note that the span of a node's tokens is [getStartOfNode(node,
			// ...), node.end). Given that `position < child.end` and child has
			// constituent tokens, we distinguish these cases:
			// 1) `position` precedes `child`'s tokens or `child` has no tokens
			//    (ie: in a comment or whitespace preceding `child`): we need to
			//    find the last token in a previous child node or child tokens.
			// 2) `position` is within the same span: we recurse on `child`.
			int start = getStartOfNode(foundChild, sourceFile,
			                           !excludeJSDoc /*includeJSDoc*/);
			bool lookInPreviousChild =
				start >= position || // cursor in the leading trivia or
				                     // preceding tokens
				!isValidPrecedingNode(foundChild, sourceFile);
			if (lookInPreviousChild) {
				if (position >= foundChild->pos()) {
					// Find jsdoc preceding the foundChild.
					Node* jsDoc = nullptr;
					std::vector<Node*> nodeJSDoc = n->jsDoc(sourceFile);
					for (auto it = nodeJSDoc.rbegin(); it != nodeJSDoc.rend();
					     ++it) {
						if ((*it)->pos() >= foundChild->pos()) {
							jsDoc = *it;
							break;
						}
					}
					if (jsDoc != nullptr) {
						if (!excludeJSDoc && position < jsDoc->end()) {
							return find(jsDoc);
						} else {
							return findRightmostValidToken(
								jsDoc->end(), sourceFile, n, position,
								excludeJSDoc);
						}
					}
					return findRightmostValidToken(foundChild->pos(),
					                               sourceFile, n,
					                               -1 /*position*/,
					                               excludeJSDoc);
				} else {
					// Answer is in tokens between two visited children.
					return findRightmostValidToken(foundChild->pos(),
					                               sourceFile, n, position,
					                               excludeJSDoc);
				}
			} else {
				// position is in [foundChild.getStart(), foundChild.End):
				// recur.
				return find(foundChild);
			}
		}

		// We have two cases here: either the position is at the end of the
		// file, or the desired token is in the unvisited trailing tokens of the
		// current node.
		if (position >= n->end()) {
			return findRightmostValidToken(n->end(), sourceFile, n,
			                               -1 /*position*/, excludeJSDoc);
		} else {
			return findRightmostValidToken(n->end(), sourceFile, n, position,
			                               excludeJSDoc);
		}
	};

	Node* node;
	if (startNode != nullptr) {
		node = startNode;
	} else {
		node = sourceFile->asNode();
	}
	Node* result = find(node);
	if (result != nullptr && isWhitespaceOnlyJsxText(result)) {
		TSC_UNREACHABLE("Expected result to be a non-whitespace token.");
	}
	return result;
}

int getStartOfNode(Node* node, SourceFile* file, bool includeJSDoc) {
	return getTokenPosOfNode(node, file, includeJSDoc);
}

Node* findNextToken(Node* previousToken, Node* parent, SourceFile* file) {
	std::function<Node*(Node*)> find = [&](Node* n) -> Node* {
		if (isTokenKind(n->kind) && n->pos() == previousToken->end()) {
			// this is token that starts at the end of previous token - return
			// it
			return n;
		}
		// Node that contains `previousToken` or occurs immediately after it.
		Node* foundNode = nullptr;
		auto visitNode = [&](Node* node, NodeVisitor*) -> Node* {
			if (node != nullptr && (node->flags & NodeFlagsReparsed) == 0 &&
			    node->pos() <= previousToken->end() &&
			    node->end() > previousToken->end()) {
				foundNode = node;
			}
			return node;
		};
		auto visitNodes = [&](NodeList* nodeList, NodeVisitor*) -> NodeList* {
			if (nodeList != nullptr && !nodeList->nodes.empty() &&
			    foundNode == nullptr) {
				const std::vector<Node*>& nodes = nodeList->nodes;
				std::pair<int, bool> res = binarySearchUniqueFunc(
					nodes, [&](int, Node* node) -> int {
						if ((node->flags & NodeFlagsReparsed) != 0) {
							return comparisonLessThan;
						}
						if (node->pos() > previousToken->end()) {
							return comparisonGreaterThan;
						}
						if (node->end() <= previousToken->pos()) {
							return comparisonLessThan;
						}
						return comparisonEqualTo;
					});
				if (res.second) {
					foundNode = nodes[res.first];
				}
			}
			return nodeList;
		};
		visitEachChildAndJSDoc(n, file, visitNode, visitNodes);
		// Cases:
		// 1. no answer exists
		// 2. answer is an unvisited token
		// 3. answer is in the visited found node

		// Case 3: look for the next token inside the found node.
		if (foundNode != nullptr) {
			return find(foundNode);
		}
		int startPos = previousToken->end();
		// Case 2: look for the next token directly.
		if (startPos >= n->pos() && startPos < n->end()) {
			Scanner scanner;
			getScannerForSourceFile(scanner, file, startPos);
			Kind token = scanner.token();
			int tokenFullStart = scanner.tokenFullStart();
			int tokenEnd = scanner.tokenEnd();
			TokenFlags flags = scanner.tokenFlags();
			// Use tokenFullStart (which includes leading trivia) to match TS's
			// findNextToken behavior where `n.pos === previousToken.end` is
			// checked (TS's pos includes trivia, same as Go's
			// Pos()/tokenFullStart).
			if (tokenFullStart == previousToken->end()) {
				return getOrCreateToken(file, token, tokenFullStart, tokenEnd,
				                        n, flags);
			}
			std::string msg = "Expected to find next token at " +
				std::to_string(previousToken->end()) + ", got token " +
				std::string(kindToString(token)) + " at " +
				std::to_string(tokenFullStart);
			TSC_UNREACHABLE(msg.c_str());
		}
		// Case 3: no answer.
		return nullptr;
	};
	return find(parent);
}

Node* findChildOfKind(Node* containingNode, Kind kind,
                      SourceFile* sourceFile) {
	int lastNodePos = containingNode->pos();
	Scanner scan;
	getScannerForSourceFile(scan, sourceFile, lastNodePos);

	Node* foundChild = nullptr;
	auto visitNode = [&](Node* node) -> bool {
		if (node == nullptr || (node->flags & NodeFlagsReparsed) != 0) {
			return false;
		}
		// Look for child in preceding tokens.
		int startPos = lastNodePos;
		while (startPos < node->pos()) {
			Kind tokenKind = scan.token();
			int tokenEnd = scan.tokenEnd();
			if (tokenKind == kind) {
				int tokenFullStart = scan.tokenFullStart();
				TokenFlags flags = scan.tokenFlags();
				foundChild = getOrCreateToken(sourceFile, tokenKind,
				                              tokenFullStart, tokenEnd,
				                              containingNode, flags);
				return true;
			}
			startPos = tokenEnd;
			scan.scan();
		}

		if (node->kind == kind) {
			foundChild = node;
			return true;
		}

		lastNodePos = node->end();
		scan.resetPos(lastNodePos);
		return false;
	};

	forEachChildAndJSDoc(containingNode, sourceFile, visitNode);

	if (foundChild != nullptr) {
		return foundChild;
	}

	// Look for child in trailing tokens.
	int startPos = lastNodePos;
	while (startPos < containingNode->end()) {
		Kind tokenKind = scan.token();
		int tokenEnd = scan.tokenEnd();
		if (tokenKind == kind) {
			int tokenFullStart = scan.tokenFullStart();
			TokenFlags flags = scan.tokenFlags();
			Node* token = getOrCreateToken(sourceFile, tokenKind, tokenFullStart,
			                           tokenEnd, containingNode, flags);
			return token;
		}
		startPos = tokenEnd;
		scan.scan();
	}
	return nullptr;
}

}  // namespace astnav
}  // namespace tsc
