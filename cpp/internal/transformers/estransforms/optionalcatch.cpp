// Port of tsc/internal/transformers/estransforms/optionalcatch.go.
#include "internal/transformers/estransforms/estransforms.h"

namespace tsc::transformers::estransforms {
namespace {

struct OptionalCatchTransformer : Transformer {

	// visit — optionalcatch.go:12
	Node* visit(Node* node) {
		if ((node->subtreeFacts() &
			 SubtreeContainsMissingCatchClauseVariable) == 0) {
			return node;
		}
		switch (node->kind) {
		case Kind::CatchClause:
			return visitCatchClause(node->as<CatchClause>());
		default:
			return visitor()->visitEachChild(node);
		}
	}

	// visitCatchClause — optionalcatch.go:24
	Node* visitCatchClause(CatchClause* node) {
		if (node->VariableDeclaration == nullptr) {
			return factory()->newCatchClause(
				factory()->newVariableDeclaration(
					factory()->newTempVariable(), nullptr, nullptr,
					nullptr),
				visitor()->visit(node->Block));
		}
		return visitor()->visitEachChild(node->asNode());
	}

	// newOptionalCatchTransformer — optionalcatch.go:34
	static Transformer* create(TransformOptions* opt) {
		auto* tx = new OptionalCatchTransformer;
		return tx->newTransformer(
			[tx](Node* node) { return tx->visit(node); }, opt->Context);
	}
};

}  // namespace

Transformer* newOptionalCatchTransformer(TransformOptions* opt) {
	return OptionalCatchTransformer::create(opt);
}

}  // namespace tsc::transformers::estransforms
