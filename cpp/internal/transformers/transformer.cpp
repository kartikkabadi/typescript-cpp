// Port of tsc/internal/transformers — transformer.go (Transformer +
// chainedTransformer), chain.go (Chain) and modifiervisitor.go
// (ExtractModifiers).
#include "internal/transformers/transformers.h"

namespace tsc::transformers {

// ---------------------------------------------------------------------------
// transformer.go
// ---------------------------------------------------------------------------

// Transformer::NewTransformer — transformer.go:14
Transformer* Transformer::newTransformer(std::function<Node*(Node*)> visit,
                                         printer::EmitContext* emitContext) {
	if (emitContext_ != nullptr) {
		TSC_UNREACHABLE("Transformer already initialized");
	}
	if (emitContext == nullptr) {
		emitContext = printer::NewEmitContext();
	}
	emitContext_ = emitContext;
	factory_ = &emitContext->factory;
	visitor_ = emitContext->newNodeVisitor(std::move(visit));
	return this;
}

// ---------------------------------------------------------------------------
// chain.go
// ---------------------------------------------------------------------------

// chainedTransformer — chain.go:10
struct chainedTransformer : Transformer {
	std::vector<std::unique_ptr<Transformer>> components;

	~chainedTransformer() override = default;

	Node* visit(Node* node) {
		if (node->kind != Kind::SourceFile) {
			TSC_UNREACHABLE(
				"Chained transform passed non-sourcefile initial node");
		}
		SourceFile* result = node->as<SourceFile>();
		for (auto& t : components) {
			result = t->transformSourceFile(result);
		}
		return result->asNode();
	}
};

// Chain — chain.go:38
TransformerFactory chain(std::vector<TransformerFactory> transforms) {
	if (transforms.size() < 2) {
		if (transforms.empty()) {
			TSC_UNREACHABLE(
				"Expected some number of transforms to chain, but got none");
		}
		return transforms[0];
	}
	return [transforms](TransformOptions* opt) -> Transformer* {
		std::vector<std::unique_ptr<Transformer>> constructed;
		constructed.reserve(transforms.size());
		for (auto& t : transforms) {
			// TODO: flatten nested chains?
			if (Transformer* result = t(opt); result != nullptr) {
				constructed.emplace_back(result);
			}
		}
		switch (constructed.size()) {
		case 0:
			return nullptr;
		case 1:
			return constructed[0].release();
		}
		auto* ch = new chainedTransformer;
		ch->components = std::move(constructed);
		ch->newTransformer(
			[ch](Node* node) { return ch->visit(node); }, opt->Context);
		return ch;
	};
}

// ---------------------------------------------------------------------------
// modifiervisitor.go
// ---------------------------------------------------------------------------

// modifierVisitor — modifiervisitor.go:8
struct modifierVisitor : Transformer {
	ModifierFlags allowedModifiers = ModifierFlagsNone;

	Node* visit(Node* node) {
		ModifierFlags flags = modifierToFlag(node->kind);
		if (flags != ModifierFlagsNone && (flags & allowedModifiers) == 0) {
			return nullptr;
		}
		return node;
	}
};

// ExtractModifiers — modifiervisitor.go:21
ModifierList* extractModifiers(printer::EmitContext* emitContext,
                               ModifierList* modifiers,
                               ModifierFlags allowed) {
	if (modifiers == nullptr) {
		return nullptr;
	}
	auto* tx = new modifierVisitor;
	tx->allowedModifiers = allowed;
	tx->newTransformer(
		[tx](Node* node) { return tx->visit(node); }, emitContext);
	ModifierList* result = tx->visitor()->visitModifiers(modifiers);
	delete tx;
	return result;
}

}  // namespace tsc::transformers
