// Port of tsc/internal/ast/visitor.go — NodeVisitor + NodeVisitorHooks.
// Go's public Visit* methods keep their names (lowercased); the unexported
// hook-dispatch methods (v.visitNode etc.) get a `Hooked` suffix to
// disambiguate in C++.
#pragma once

#include "internal/ast/ast.h"

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace tsc {

// NodeVisitorHooks — hooks to be invoked when visiting a node (visitor.go).
struct NodeVisitorHooks {
	std::function<Node*(Node* node, NodeVisitor* v)> visitNode;
	std::function<Node*(Node* node, NodeVisitor* v)> visitToken;
	std::function<NodeList*(NodeList* nodes, NodeVisitor* v)> visitNodes;
	std::function<ModifierList*(ModifierList* nodes, NodeVisitor* v)>
		visitModifiers;
	std::function<Node*(Node* node, NodeVisitor* v)> visitEmbeddedStatement;
	std::function<Node*(Node* node, NodeVisitor* v)> visitIterationBody;
	std::function<NodeList*(NodeList* nodes, NodeVisitor* v)> visitParameters;
	std::function<Node*(Node* node, NodeVisitor* v)> visitFunctionBody;
	std::function<NodeList*(NodeList* nodes, NodeVisitor* v)>
		visitTopLevelStatements;
};

// NodeVisitor — mirrors ast.NodeVisitor.
struct NodeVisitor {
	std::function<Node*(Node* node)> visit; // Required. The callback used to visit a node
	NodeFactory* factory = nullptr;         // Required. The NodeFactory used to produce new nodes when passed to VisitEachChild
	NodeVisitorHooks hooks;                 // Hooks to be invoked when visiting a node
	std::unique_ptr<NodeFactory> ownedFactory; // backing store for the default factory

	// Like VisitNode, but static casts to *SourceFile.
	SourceFile* visitSourceFile(SourceFile* node) {
		return visitNode(node->asNode())->as<SourceFile>();
	}

	// Visits a Node, possibly returning a new Node in its place. Returning
	// nullptr will cause the child node to be removed.
	Node* visitNode(Node* node) {
		if (node == nullptr || !visit) {
			return node;
		}
		Node* visited = visit(node);
		if (visited != nullptr && visited->kind == Kind::SyntaxList) {
			auto& children = visited->as<SyntaxList>()->Children;
			if (children.size() != 1) {
				TSC_UNREACHABLE(
					"Expected only a single node to be written to output");
			}
			visited = children[0];
			if (visited != nullptr && visited->kind == Kind::SyntaxList) {
				TSC_UNREACHABLE(
					"The result of visiting and lifting a Node may not be "
					"SyntaxList");
			}
		}
		return visited;
	}

	// Like VisitNode, but always lifts the result to a single Node that is a
	// Block.
	Node* visitEmbeddedStatement(Node* node) {
		if (node == nullptr || !visit) {
			return node;
		}
		Node* visited = visit(node);
		if (visited == nullptr) {
			return nullptr;
		}
		return liftToBlock(visited);
	}

	// Visits a NodeList, possibly returning a new NodeList in its place.
	NodeList* visitNodes(NodeList* nodes) {
		if (nodes == nullptr || !visit) {
			return nodes;
		}
		auto [result, changed] = visitSlice(nodes->nodes);
		if (changed) {
			NodeList* list = factory->newNodeList(result);
			list->loc = nodes->loc;
			return list;
		}
		return nodes;
	}

	// Visits a ModifierList, possibly returning a new list in its place.
	ModifierList* visitModifiers(ModifierList* nodes) {
		if (nodes == nullptr || !visit) {
			return nodes;
		}
		auto [result, changed] = visitSlice(nodes->nodes);
		if (changed) {
			ModifierList* list = factory->newModifierList(result);
			list->loc = nodes->loc;
			return list;
		}
		return nodes;
	}

	// Visits a slice of Nodes, possibly returning a new slice in its place.
	// Returns {slice, changed}.
	std::pair<std::vector<Node*>, bool> visitSlice(
		const std::vector<Node*>& nodes) {
		if (nodes.empty() || !visit) {
			return {nodes, false};
		}

		for (size_t i = 0; i < nodes.size(); i++) {
			Node* node = nodes[i];
			if (!visit) {
				break;
			}
			Node* visited = visit(node);
			if (visited == nullptr || visited != node) {
				std::vector<Node*> updated(nodes.begin(), nodes.begin() + i);
				for (;;) {
					if (visited == nullptr) {
						// do nothing
					} else if (visited->kind == Kind::SyntaxList) {
						auto& ch = visited->as<SyntaxList>()->Children;
						updated.insert(updated.end(), ch.begin(), ch.end());
					} else {
						updated.push_back(visited);
					}
					i++;
					if (i < nodes.size()) {
						if (visit) {
							node = nodes[i];
							visited = visit(node);
						} else {
							updated.insert(updated.end(), nodes.begin() + i,
							               nodes.end());
							break;
						}
					} else {
						break;
					}
				}
				return {updated, true};
			}
		}
		return {nodes, false};
	}

	// Visits each child of a Node, possibly returning a new Node in its place.
	Node* visitEachChild(Node* node) {
		if (node == nullptr || !visit) {
			return node;
		}
		return node->visitEachChild(*this);
	}

	// Hook dispatchers (unexported methods in Go).
	Node* visitNodeHooked(Node* node) {
		if (hooks.visitNode) {
			return hooks.visitNode(node, this);
		}
		return visitNode(node);
	}

	Node* visitEmbeddedStatementHooked(Node* node) {
		if (hooks.visitEmbeddedStatement) {
			return hooks.visitEmbeddedStatement(node, this);
		}
		if (hooks.visitNode) {
			return liftToBlock(hooks.visitNode(node, this));
		}
		return visitEmbeddedStatement(node);
	}

	Node* visitIterationBodyHooked(Node* node) {
		if (hooks.visitIterationBody) {
			return hooks.visitIterationBody(node, this);
		}
		return visitEmbeddedStatementHooked(node);
	}

	Node* visitFunctionBodyHooked(Node* node) {
		if (hooks.visitFunctionBody) {
			return hooks.visitFunctionBody(node, this);
		}
		return visitNodeHooked(node);
	}

	Node* visitTokenHooked(Node* node) {
		if (hooks.visitToken) {
			return hooks.visitToken(node, this);
		}
		return visitNode(node);
	}

	NodeList* visitNodesHooked(NodeList* nodes) {
		if (hooks.visitNodes) {
			return hooks.visitNodes(nodes, this);
		}
		return visitNodes(nodes);
	}

	ModifierList* visitModifiersHooked(ModifierList* nodes) {
		if (hooks.visitModifiers) {
			return hooks.visitModifiers(nodes, this);
		}
		return visitModifiers(nodes);
	}

	NodeList* visitParametersHooked(NodeList* nodes) {
		if (hooks.visitParameters) {
			return hooks.visitParameters(nodes, this);
		}
		return visitNodesHooked(nodes);
	}

	NodeList* visitTopLevelStatementsHooked(NodeList* nodes) {
		if (hooks.visitTopLevelStatements) {
			return hooks.visitTopLevelStatements(nodes, this);
		}
		return visitNodesHooked(nodes);
	}

private:
	Node* liftToBlock(Node* node) {
		std::vector<Node*> nodes;
		if (node != nullptr) {
			if (node->kind == Kind::SyntaxList) {
				nodes = node->as<SyntaxList>()->Children;
			} else {
				nodes = {node};
			}
		}
		if (nodes.size() == 1) {
			node = nodes[0];
		} else {
			node = factory->newBlock(factory->newNodeList(nodes),
			                         true /*multiLine*/);
		}
		if (node->kind == Kind::SyntaxList) {
			TSC_UNREACHABLE(
				"The result of visiting and lifting a Node may not be "
				"SyntaxList");
		}
		return node;
	}
};

// NewNodeVisitor — allocates a NodeVisitor; a nullptr factory means the
// visitor owns a default-constructed one.
inline NodeVisitor* newNodeVisitor(std::function<Node*(Node*)> visit,
                                   NodeFactory* factory,
                                   const NodeVisitorHooks& hooks) {
	auto* v = new NodeVisitor();
	v->visit = std::move(visit);
	v->hooks = hooks;
	if (factory == nullptr) {
		v->ownedFactory = std::make_unique<NodeFactory>();
		factory = v->ownedFactory.get();
	}
	v->factory = factory;
	return v;
}

}  // namespace tsc

// Generated clone/visitEachChild kind-dispatch bodies (needs the complete
// NodeVisitor above).
#include "internal/ast/nodes_visitor_generated.h"
