// Port of tsc/internal/transformers/estransforms/logicalassignment.go.
#include "internal/transformers/estransforms/estransforms.h"

namespace tsc::transformers::estransforms {
namespace {

struct LogicalAssignmentTransformer : Transformer {

	// visit — logicalassignment.go:12
	Node* visit(Node* node) {
		if ((node->subtreeFacts() & SubtreeContainsLogicalAssignments) == 0) {
			return node;
		}
		switch (node->kind) {
		case Kind::BinaryExpression:
			return visitBinaryExpression(node->as<BinaryExpression>());
		default:
			return visitor()->visitEachChild(node);
		}
	}

	// visitBinaryExpression — logicalassignment.go:24
	Node* visitBinaryExpression(BinaryExpression* node) {
		Kind nonAssignmentOperator = Kind::Unknown;
		switch (node->OperatorToken->kind) {
		case Kind::BarBarEqualsToken:
			nonAssignmentOperator = Kind::BarBarToken;
			break;
		case Kind::AmpersandAmpersandEqualsToken:
			nonAssignmentOperator = Kind::AmpersandAmpersandToken;
			break;
		case Kind::QuestionQuestionEqualsToken:
			nonAssignmentOperator = Kind::QuestionQuestionToken;
			break;
		default:
			return visitor()->visitEachChild(node->asNode());
		}

		Node* left = skipParentheses(visitor()->visitNode(node->Left));
		Node* assignmentTarget = left;
		Node* right = skipParentheses(visitor()->visitNode(node->Right));

		if (isAccessExpression(left)) {
			bool propertyAccessTargetSimpleCopiable =
				isSimpleCopiableExpression(left->expression());
			Node* propertyAccessTarget = left->expression();
			Node* propertyAccessTargetAssignment = left->expression();
			if (!propertyAccessTargetSimpleCopiable) {
				propertyAccessTarget = factory()->newTempVariable();
				emitContext()->addVariableDeclaration(propertyAccessTarget);
				propertyAccessTargetAssignment =
					factory()->newAssignmentExpression(propertyAccessTarget,
													   left->expression());
			}

			if (isPropertyAccessExpression(left)) {
				assignmentTarget = factory()->newPropertyAccessExpression(
					propertyAccessTarget, nullptr, left->name(),
					NodeFlagsNone);
				left = factory()->newPropertyAccessExpression(
					propertyAccessTargetAssignment, nullptr, left->name(),
					NodeFlagsNone);
			} else {
				bool elementAccessArgumentSimpleCopiable =
					isSimpleCopiableExpression(
						left->as<ElementAccessExpression>()
							->ArgumentExpression);
				Node* elementAccessArgument =
					left->as<ElementAccessExpression>()->ArgumentExpression;
				Node* argumentExpr = elementAccessArgument;
				if (!elementAccessArgumentSimpleCopiable) {
					elementAccessArgument = factory()->newTempVariable();
					emitContext()->addVariableDeclaration(
						elementAccessArgument);
					argumentExpr = factory()->newAssignmentExpression(
						elementAccessArgument,
						left->as<ElementAccessExpression>()
							->ArgumentExpression);
				}

				assignmentTarget = factory()->newElementAccessExpression(
					propertyAccessTarget, nullptr, elementAccessArgument,
					NodeFlagsNone);
				left = factory()->newElementAccessExpression(
					propertyAccessTargetAssignment, nullptr, argumentExpr,
					NodeFlagsNone);
			}
		}

		return factory()->newBinaryExpression(
			nullptr, left, nullptr, factory()->newToken(nonAssignmentOperator),
			factory()->newParenthesizedExpression(
				factory()->newAssignmentExpression(assignmentTarget, right)));
	}

	// newLogicalAssignmentTransformer — logicalassignment.go:110
	static Transformer* create(TransformOptions* opt) {
		auto* tx = new LogicalAssignmentTransformer;
		return tx->newTransformer(
			[tx](Node* node) { return tx->visit(node); }, opt->Context);
	}
};

}  // namespace

Transformer* newLogicalAssignmentTransformer(TransformOptions* opt) {
	return LogicalAssignmentTransformer::create(opt);
}

}  // namespace tsc::transformers::estransforms
