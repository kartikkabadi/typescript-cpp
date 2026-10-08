// Port of tsc/internal/transformers/estransforms/classthis.go — detects the
// `static {}` block containing only a `_classThis = this` assignment.
#include "internal/transformers/estransforms/estransforms.h"

namespace tsc::transformers::estransforms {

// isClassThisAssignmentBlock — classthis.go:8. Gets whether a node is a
// `static {}` block containing only a single assignment of the static `this`
// to the `_classThis` (or similar) variable stored in the `classthis`
// property of the block's `EmitNode`.
bool isClassThisAssignmentBlock(printer::EmitContext* emitContext,
                                Node* node) {
	if (isClassStaticBlockDeclaration(node)) {
		ClassStaticBlockDeclaration* n =
			node->as<ClassStaticBlockDeclaration>();
		Block* body = n->Body->as<Block>();
		if (body->Statements->nodes.size() == 1) {
			Node* statement = body->Statements->nodes[0];
			if (isExpressionStatement(statement)) {
				Node* expression = statement->expression();
				if (isAssignmentExpression(
				        expression, true /*excludeCompoundAssignment*/)) {
					BinaryExpression* binary =
						expression->as<BinaryExpression>();
					return isIdentifier(binary->Left) &&
						emitContext->classThisOf(node) == binary->Left &&
						binary->Right->kind == Kind::ThisKeyword;
				}
			}
		}
	}
	return false;
}

}  // namespace tsc::transformers::estransforms
