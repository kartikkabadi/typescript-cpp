// Port of tsc/internal/transformers/estransforms/usestrict.go.
#include "internal/transformers/estransforms/estransforms.h"

namespace tsc::transformers::estransforms {
namespace {

struct UseStrictTransformer : Transformer {
	const CompilerOptions* compilerOptions = nullptr;
	std::function<ModuleKind(SourceFile*)> getEmitModuleFormatOfFile;

	// visit — usestrict.go:23
	Node* visit(Node* node) {
		if (node->kind != Kind::SourceFile) {
			return node;
		}
		return visitSourceFile(node->as<SourceFile>());
	}

	// visitSourceFile — usestrict.go:30
	Node* visitSourceFile(SourceFile* node) {
		if (node->ScriptKind == ScriptKind::JSON) {
			return node->asNode();
		}

		bool isExternalModule_ = isExternalModule(node);
		ModuleKind moduleKind = compilerOptions->GetEmitModuleKind();
		ModuleKind format = getEmitModuleFormatOfFile(node);

		// ESM is always strict. If the file is ESM, and CJS emit
		// has not been requested, then skip adding "use strict".
		if (isExternalModule_ && moduleKind >= ModuleKind::ES2015 &&
			(moduleKind == ModuleKind::Preserve ||
			 format >= ModuleKind::ES2015)) {
			return node->asNode();
		}

		std::vector<Node*> statements =
			factory()->ensureUseStrict(node->Statements->nodes);
		NodeList* statementList = factory()->newNodeList(statements);
		statementList->loc = node->Statements->loc;
		return factory()->updateSourceFile(node, statementList,
										   node->EndOfFileToken);
	}

	// NewUseStrictTransformer — usestrict.go:9
	static Transformer* create(TransformOptions* opt) {
		auto* tx = new UseStrictTransformer;
		tx->compilerOptions = opt->CompilerOptions;
		tx->getEmitModuleFormatOfFile = opt->GetEmitModuleFormatOfFile;
		return tx->newTransformer(
			[tx](Node* node) { return tx->visit(node); }, opt->Context);
	}
};

}  // namespace

Transformer* NewUseStrictTransformer(TransformOptions* opt) {
	return UseStrictTransformer::create(opt);
}

}  // namespace tsc::transformers::estransforms
