// Port of tsc/internal/transformers/estransforms/nullishcoalescing.go.
#include "internal/transformers/estransforms/estransforms.h"

namespace tsc::transformers::estransforms {
namespace {

struct NullishCoalescingTransformer : Transformer {

	// visit — nullishcoalescing.go:12
	Node* visit(Node* node) {
		if ((node->subtreeFacts() & SubtreeContainsNullishCoalescing) == 0) {
			return node;
		}
		switch (node->kind) {
		case Kind::BinaryExpression:
			return visitBinaryExpression(node->as<BinaryExpression>());
		default:
			return visitor()->visitEachChild(node);
		}
	}

	// visitBinaryExpression — nullishcoalescing.go:24
	Node* visitBinaryExpression(BinaryExpression* node) {
		switch (node->OperatorToken->kind) {
		case Kind::QuestionQuestionToken: {
			Node* left = visitor()->visitNode(node->Left);
			Node* right = left;
			if (!isSimpleCopiableExpression(left)) {
				right = factory()->newTempVariable();
				emitContext()->addVariableDeclaration(right);
				left = factory()->newAssignmentExpression(right, left);
			}
			return factory()->newConditionalExpression(
				createNotNullCondition(emitContext(), left, right, false),
				factory()->newToken(Kind::QuestionToken), right,
				factory()->newToken(Kind::ColonToken),
				visitor()->visitNode(node->Right));
		}
		default:
			return visitor()->visitEachChild(node->asNode());
		}
	}

	// newNullishCoalescingTransformer — nullishcoalescing.go:46
	static Transformer* create(TransformOptions* opt) {
		auto* tx = new NullishCoalescingTransformer;
		return tx->newTransformer(
			[tx](Node* node) { return tx->visit(node); }, opt->Context);
	}
};

}  // namespace

Transformer* newNullishCoalescingTransformer(TransformOptions* opt) {
	return NullishCoalescingTransformer::create(opt);
}

}  // namespace tsc::transformers::estransforms
