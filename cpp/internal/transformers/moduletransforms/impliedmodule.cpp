// Port of tsc/internal/transformers/moduletransforms/impliedmodule.go —
// ImpliedModuleTransformer, which delegates to the ES module or CommonJS
// module transformer based on each file's implied module format.
#include "internal/transformers/moduletransforms/moduletransforms.h"

namespace tsc::transformers::moduletransforms {

namespace {

// ImpliedModuleTransformer — impliedmodule.go:10
struct ImpliedModuleTransformer : Transformer {
	TransformOptions* opts = nullptr;
	binder::ReferenceResolver* resolver = nullptr;
	std::function<ModuleKind(SourceFile*)> getEmitModuleFormatOfFile;
	Transformer* cjsTransformer = nullptr;
	Transformer* esmTransformer = nullptr;

	Node* visit(Node* node);
	Node* visitSourceFile(SourceFile* node);
};

}  // namespace

// NewImpliedModuleTransformer — impliedmodule.go:19
Transformer* NewImpliedModuleTransformer(TransformOptions* opts) {
	auto* tx = new ImpliedModuleTransformer();
	tx->opts = opts;
	tx->resolver = opts->Resolver;
	tx->getEmitModuleFormatOfFile = opts->GetEmitModuleFormatOfFile;
	return tx->newTransformer([tx](Node* node) { return tx->visit(node); },
	                          opts->Context);
}

// visit — impliedmodule.go:24
Node* ImpliedModuleTransformer::visit(Node* node) {
	switch (node->kind) {
	case Kind::SourceFile:
		node = visitSourceFile(node->as<SourceFile>());
		break;
	default:
		break;
	}
	return node;
}

// visitSourceFile — impliedmodule.go:32
Node* ImpliedModuleTransformer::visitSourceFile(SourceFile* node) {
	if (node->IsDeclarationFile) {
		return node->asNode();
	}

	ModuleKind format = getEmitModuleFormatOfFile(node);

	Transformer* transformer;
	if (format >= ModuleKind::ES2015) {
		if (esmTransformer == nullptr) {
			esmTransformer = NewESModuleTransformer(opts);
		}
		transformer = esmTransformer;
	} else {
		if (cjsTransformer == nullptr) {
			cjsTransformer = NewCommonJSModuleTransformer(opts);
		}
		transformer = cjsTransformer;
	}

	return transformer->transformSourceFile(node)->asNode();
}

}  // namespace tsc::transformers::moduletransforms
