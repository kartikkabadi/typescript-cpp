// Port of tsc/internal/transformers/estransforms/exponentiation.go.
#include "internal/transformers/estransforms/estransforms.h"

namespace tsc::transformers::estransforms {
namespace {

struct ExponentiationTransformer : Transformer {

	// visit — exponentiation.go:12
	Node* visit(Node* node) {
		if ((node->subtreeFacts() &
			 SubtreeContainsExponentiationOperator) == 0) {
			return node;
		}
		switch (node->kind) {
		case Kind::BinaryExpression:
			return visitBinaryExpression(node->as<BinaryExpression>());
		default:
			return visitor()->visitEachChild(node);
		}
	}

	// visitBinaryExpression — exponentiation.go:24
	Node* visitBinaryExpression(BinaryExpression* node) {
		switch (node->OperatorToken->kind) {
		case Kind::AsteriskAsteriskEqualsToken:
			return visitExponentiationAssignmentExpression(node);
		case Kind::AsteriskAsteriskToken:
			return visitExponentiationExpression(node);
		default:
			break;
		}
		return visitor()->visitEachChild(node->asNode());
	}

	// visitExponentiationAssignmentExpression — exponentiation.go:34
	Node* visitExponentiationAssignmentExpression(BinaryExpression* node) {
		Node* target = nullptr;
		Node* value = nullptr;
		Node* left = visitor()->visitNode(node->Left);
		Node* right = visitor()->visitNode(node->Right);
		if (isElementAccessExpression(left)) {
			// Transforms `a[x] **= b` into
			// `(_a = a)[_x = x] = Math.pow(_a[_x], b)`
			Node* expressionTemp = factory()->newTempVariable();
			emitContext()->addVariableDeclaration(expressionTemp);
			Node* argumentExpressionTemp = factory()->newTempVariable();
			emitContext()->addVariableDeclaration(argumentExpressionTemp);

			Node* objExpr = factory()->newAssignmentExpression(
				expressionTemp, left->expression());
			objExpr->loc = left->expression()->loc;
			Node* accessExpr = factory()->newAssignmentExpression(
				argumentExpressionTemp,
				left->as<ElementAccessExpression>()->ArgumentExpression);
			accessExpr->loc =
				left->as<ElementAccessExpression>()->ArgumentExpression->loc;

			target = factory()->newElementAccessExpression(
				objExpr, nullptr, accessExpr, NodeFlagsNone);

			value = factory()->newElementAccessExpression(
				expressionTemp, nullptr, argumentExpressionTemp,
				NodeFlagsNone);
			value->loc = left->loc;
		} else if (isPropertyAccessExpression(left)) {
			// Transforms `a.x **= b` into
			// `(_a = a).x = Math.pow(_a.x, b)`
			Node* expressionTemp = factory()->newTempVariable();
			emitContext()->addVariableDeclaration(expressionTemp);
			Node* assignment = factory()->newAssignmentExpression(
				expressionTemp, left->expression());
			assignment->loc = left->expression()->loc;
			target = factory()->newPropertyAccessExpression(
				assignment, nullptr, left->name(), NodeFlagsNone);
			target->loc = left->loc;

			value = factory()->newPropertyAccessExpression(
				expressionTemp, nullptr, left->name(), NodeFlagsNone);
			value->loc = left->loc;
		} else {
			// Transforms `a **= b` into `a = Math.pow(a, b)`
			target = left;
			value = left;
		}

		Node* rhs =
			factory()->newGlobalMethodCall("Math", "pow", {value, right});
		rhs->loc = node->loc;
		Node* result = factory()->newAssignmentExpression(target, rhs);
		result->loc = node->loc;
		return result;
	}

	// visitExponentiationExpression — exponentiation.go:79
	Node* visitExponentiationExpression(BinaryExpression* node) {
		Node* left = visitor()->visitNode(node->Left);
		Node* right = visitor()->visitNode(node->Right);
		Node* result =
			factory()->newGlobalMethodCall("Math", "pow", {left, right});
		result->loc = node->loc;
		return result;
	}

	// newExponentiationTransformer — exponentiation.go:87
	static Transformer* create(TransformOptions* opt) {
		auto* tx = new ExponentiationTransformer;
		return tx->newTransformer(
			[tx](Node* node) { return tx->visit(node); }, opt->Context);
	}
};

}  // namespace

Transformer* newExponentiationTransformer(TransformOptions* opt) {
	return ExponentiationTransformer::create(opt);
}

}  // namespace tsc::transformers::estransforms
