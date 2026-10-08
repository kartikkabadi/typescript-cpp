// Port of tsc/internal/printer/changetrackerwriter.go — ChangeTrackerWriter:
// a text writer that records last non-trivia positions so change tracking can
// reassign positions to freshly printed nodes.

#include "internal/printer/printer.h"

#include "internal/ast/visitor.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::printer {

namespace {

// utf8.DecodeLastRuneInString — decode the final rune; width receives its
// byte length (0 for empty input).
char32_t decodeLastRuneInString(std::string_view s, int* width) {
	if (s.empty()) {
		*width = 0;
		return 0;
	}
	size_t pos = s.size();
	while (pos > 0 && (static_cast<unsigned char>(s[pos - 1]) & 0xC0) == 0x80) {
		pos--;
	}
	pos = pos > 0 ? pos - 1 : 0;
	*width = static_cast<int>(s.size() - pos);
	unsigned char c0 = static_cast<unsigned char>(s[pos]);
	if (c0 < 0x80) return c0;
	char32_t r = c0 & 0x3F;
	int more = c0 >= 0xF0 ? 3 : c0 >= 0xE0 ? 2 : c0 >= 0xC0 ? 1 : 0;
	for (int i = 1; i <= more && pos + i < s.size(); i++) {
		r = (r << 6) | (static_cast<unsigned char>(s[pos + i]) & 0x3F);
	}
	return r;
}

} // namespace

// NewChangeTrackerWriter — changetrackerwriter.go:25.
ChangeTrackerWriter* NewChangeTrackerWriter(const std::string& newline,
                                            int indentSize) {
	// TODO: Callers passing -1 should pass actual indent options once
	// indent-related formatting is ported.
	if (indentSize < 0) {
		indentSize = GetDefaultIndentSize();
	}
	return new ChangeTrackerWriter{
		std::unique_ptr<EmitTextWriter>(NewTextWriter(newline, indentSize))};
}

// GetPrintHandlers — changetrackerwriter.go:34.
PrintHandlers ChangeTrackerWriter::GetPrintHandlers() {
	PrintHandlers h;
	h.OnBeforeEmitNode = [this](Node* nodeOpt) {
		if (nodeOpt != nullptr) {
			this->setPos(nodeOpt);
		}
	};
	h.OnAfterEmitNode = [this](Node* nodeOpt) {
		if (nodeOpt != nullptr) {
			this->setEnd(nodeOpt);
		}
	};
	h.OnBeforeEmitNodeList = [this](NodeList* nodesOpt) {
		if (nodesOpt != nullptr) {
			this->setPos(nodesOpt);
		}
	};
	h.OnAfterEmitNodeList = [this](NodeList* nodesOpt) {
		if (nodesOpt != nullptr) {
			this->setEnd(nodesOpt);
		}
	};
	h.OnBeforeEmitToken = [this](Node* nodeOpt) {
		if (nodeOpt != nullptr) {
			this->setPos(nodeOpt);
		}
	};
	h.OnAfterEmitToken = [this](Node* nodeOpt) {
		if (nodeOpt != nullptr) {
			this->setEnd(nodeOpt);
		}
	};
	return h;
}

// setLastNonTriviaPosition — changetrackerwriter.go:91.
void ChangeTrackerWriter::setLastNonTriviaPosition(const std::string& s,
                                                   bool force) {
	if (force || skipTrivia(s, 0) != static_cast<int>(s.size())) {
		lastNonTriviaPosition = tw->GetTextPos();
		// trim trailing whitespaces
		int pos = static_cast<int>(s.size());
		while (pos > 0) {
			int width = 0;
			char32_t r = decodeLastRuneInString(
				std::string_view(s).substr(0, pos), &width);
			if (isWhiteSpaceLike(r)) {
				pos -= width;
			} else {
				break;
			}
		}
		lastNonTriviaPosition -= static_cast<int>(s.size()) - pos;
	}
}

// AssignPositionsToNode — changetrackerwriter.go:112.
Node* ChangeTrackerWriter::AssignPositionsToNode(
    Node* node, tsc::NodeFactory* factory) {
	NodeVisitor visitor;
	visitor.visit = [this, &visitor](Node* n) {
		return this->assignPositionsToNodeWorker(n, &visitor);
	};
	visitor.factory = factory;
	visitor.hooks.visitNode = [this](Node* n, NodeVisitor* v) {
		return this->assignPositionsToNodeWorker(n, v);
	};
	visitor.hooks.visitNodes = [this](NodeList* nodes, NodeVisitor* v) {
		return this->assignPositionsToNodeArray(nodes, v);
	};
	visitor.hooks.visitToken = [this](Node* n, NodeVisitor* v) {
		return this->assignPositionsToNodeWorker(n, v);
	};
	visitor.hooks.visitModifiers =
	    [this](ModifierList* modifiers, NodeVisitor* v) -> ModifierList* {
		if (modifiers != nullptr) {
			NodeList* newNodeList =
			    this->assignPositionsToNodeArray(modifiers, v);
			// Return a new ModifierList so that VisitEachChild/Update detects
			// the change and creates a new node with reassigned child
			// positions.
			return v->factory->newModifierList(newNodeList->nodes);
		}
		return modifiers;
	};
	return assignPositionsToNodeWorker(node, &visitor);
}

// assignPositionsToNodeWorker — changetrackerwriter.go:144.
Node* ChangeTrackerWriter::assignPositionsToNodeWorker(Node* node,
                                                     NodeVisitor* v) {
	if (node == nullptr) {
		return node;
	}
	Node* visited = node->visitEachChild(*v);
	// Assigning positions must not mutate the caller's node: it may be printed
	// again (a change in a content-mapped file is formatted once per virtual
	// projection of its insertion point), and a node that has acquired
	// positions is printed by reading text back out of the source file.
	// VisitEachChild returns a fresh node only when a child changed, so clone
	// whenever it hands back the input.
	Node* newNode = visited;
	if (visited == node) {
		newNode = visited->clone(*v->factory);
	}
	newNode->forEachChild([newNode](Node* child) {
		child->parent = newNode;
		return true;
	});
	newNode->loc = TextRange{static_cast<TextPos>(getPos(node)),
	                        static_cast<TextPos>(getEnd(node))};
	return newNode;
}

// assignPositionsToNodeArray — changetrackerwriter.go:169.
NodeList* ChangeTrackerWriter::assignPositionsToNodeArray(NodeList* nodes,
                                                        NodeVisitor* v) {
	// changetrackerwriter.go:158 calls v.VisitNodes — the raw element
	// iteration, NOT the hooked wrapper (which would recurse into this
	// function forever).
	NodeList* visited = v->visitNodes(nodes);
	if (visited == nullptr) {
		return visited;
	}
	if (nodes == nullptr) {
		// Debug.assert(nodes);
		TSC_UNREACHABLE("if nodes is nil, visited should not be nil");
	}
	// clone nodearray if necessary
	NodeList* nodeArray = visited;
	if (visited == nodes) {
		nodeArray = visited->clone(*v->factory);
	}
	nodeArray->loc = TextRange{static_cast<TextPos>(getPos(nodes)),
	                           static_cast<TextPos>(getEnd(nodes))};
	return nodeArray;
}

} // namespace tsc::printer
