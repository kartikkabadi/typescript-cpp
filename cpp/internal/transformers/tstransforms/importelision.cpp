// Port of tsc/internal/transformers/tstransforms/importelision.go.
#include "internal/transformers/tstransforms/tstransforms.h"

#include "internal/checker/checker.h" // checker::EmitResolver

namespace tsc::transformers::tstransforms {
namespace {

struct ImportElisionTransformer : Transformer {
	const CompilerOptions* compilerOptions = nullptr;
	SourceFile* currentSourceFile = nullptr;
	checker::EmitResolver* emitResolver = nullptr;

	// NewImportElisionTransformer — importelision.go:17
	static Transformer* create(TransformOptions* opt) {
		const CompilerOptions* compilerOptions = opt->CompilerOptions;
		printer::EmitContext* emitContext = opt->Context;
		if (tristateIsTrue(compilerOptions->VerbatimModuleSyntax)) {
			TSC_UNREACHABLE(
				"ImportElisionTransformer should not be used with "
				"VerbatimModuleSyntax");
		}
		auto* tx = new ImportElisionTransformer;
		tx->compilerOptions = compilerOptions;
		tx->emitResolver = opt->EmitResolver;
		return tx->newTransformer(
			[tx](Node* node) { return tx->visit(node); }, emitContext);
	}

	// visit — importelision.go:27
	Node* visit(Node* node) {
		if (isSourceFile(node) && emitResolver != nullptr) {
			emitResolver->MarkLinkedReferencesRecursively(
				emitContext()->mostOriginal(node)->as<SourceFile>());
		}

		switch (node->kind) {
		case Kind::ImportEqualsDeclaration:
			if (isExternalModuleImportEqualsDeclaration(node)) {
				if (!shouldEmitAliasDeclaration(node)) {
					return nullptr;
				}
			} else {
				if (!shouldEmitImportEqualsDeclaration(
						node->as<ImportEqualsDeclaration>())) {
					return nullptr;
				}
			}
			return visitor()->visitEachChild(node);
		case Kind::ImportDeclaration: {
			auto* n = node->as<ImportDeclaration>();
			// Do not elide a side-effect only import declaration.
			//  import "foo";
			if (n->ImportClause != nullptr) {
				Node* importClause = visitor()->visitNode(n->ImportClause);
				if (importClause == nullptr) {
					return nullptr;
				}
				return factory()->updateImportDeclaration(
					n, n->modifiers, importClause, n->ModuleSpecifier,
					visitor()->visitNode(n->Attributes));
			}
			return visitor()->visitEachChild(node);
		}
		case Kind::ImportClause: {
			auto* n = node->as<ImportClause>();
			Node* name =
				shouldEmitAliasDeclaration(node) ? n->name : nullptr;
			Node* namedBindings = visitor()->visitNode(n->NamedBindings);
			if (name == nullptr && namedBindings == nullptr) {
				// all import bindings were elided
				return nullptr;
			}
			return factory()->updateImportClause(n, n->PhaseModifier, name,
												 namedBindings);
		}
		case Kind::NamespaceImport:
			if (!shouldEmitAliasDeclaration(node)) {
				// elide unused imports
				return nullptr;
			}
			return node;
		case Kind::NamedImports: {
			auto* n = node->as<NamedImports>();
			NodeList* elements = visitor()->visitNodes(n->Elements);
			if (elements->nodes.empty()) {
				// all import specifiers were elided
				return nullptr;
			}
			return factory()->updateNamedImports(n, elements);
		}
		case Kind::ImportSpecifier:
			if (!shouldEmitAliasDeclaration(node)) {
				// elide type-only or unused imports
				return nullptr;
			}
			return node;
		case Kind::ExportAssignment:
			if (!tristateIsTrue(compilerOptions->VerbatimModuleSyntax) &&
				!isValueAliasDeclaration(node)) {
				// elide unused import
				return nullptr;
			}
			return visitor()->visitEachChild(node);
		case Kind::ExportDeclaration: {
			auto* n = node->as<ExportDeclaration>();
			Node* exportClause = nullptr;
			if (n->ExportClause != nullptr) {
				exportClause = visitor()->visitNode(n->ExportClause);
				if (exportClause == nullptr) {
					// all export bindings were elided
					return nullptr;
				}
			}
			return factory()->updateExportDeclaration(
				n, nullptr /*modifiers*/, false /*isTypeOnly*/, exportClause,
				visitor()->visitNode(n->ModuleSpecifier),
				visitor()->visitNode(n->Attributes));
		}
		case Kind::NamedExports: {
			auto* n = node->as<NamedExports>();
			NodeList* elements = visitor()->visitNodes(n->Elements);
			if (elements->nodes.empty()) {
				// all export specifiers were elided
				return nullptr;
			}
			return factory()->updateNamedExports(n, elements);
		}
		case Kind::ExportSpecifier:
			if (!isValueAliasDeclaration(node)) {
				// elide unused export
				return nullptr;
			}
			return node;
		case Kind::SourceFile: {
			SourceFile* savedCurrentSourceFile = currentSourceFile;
			currentSourceFile = node->as<SourceFile>();
			node = visitor()->visitEachChild(node);
			currentSourceFile = savedCurrentSourceFile;
			return node;
		}
		case Kind::ModuleDeclaration:
		case Kind::ModuleBlock:
			return visitor()->visitEachChild(node);
		default:
			return node;
		}
	}

	// shouldEmitAliasDeclaration — importelision.go:129
	bool shouldEmitAliasDeclaration(Node* node) {
		return isInJSFile(node) || isReferencedAliasDeclaration(node);
	}

	// shouldEmitImportEqualsDeclaration — importelision.go:133
	bool shouldEmitImportEqualsDeclaration(ImportEqualsDeclaration* node) {
		// preserve old compiler's behavior: emit import declaration (even if
		// we do not consider them referenced) when
		// - current file is not external module
		// - import declaration is top level and target is value imported by
		//   entity name
		return shouldEmitAliasDeclaration(node->asNode()) ||
			   (!isExternalModule(currentSourceFile) &&
				isTopLevelValueImportEqualsWithEntityName(node->asNode()));
	}

	// isReferencedAliasDeclaration — importelision.go:140
	bool isReferencedAliasDeclaration(Node* node) {
		node = emitContext()->parseNode(node);
		return node == nullptr ||
			   emitResolver->IsReferencedAliasDeclaration(node);
	}

	// isValueAliasDeclaration — importelision.go:145
	bool isValueAliasDeclaration(Node* node) {
		node = emitContext()->parseNode(node);
		return node == nullptr || emitResolver->IsValueAliasDeclaration(node);
	}

	// isTopLevelValueImportEqualsWithEntityName — importelision.go:150
	bool isTopLevelValueImportEqualsWithEntityName(Node* node) {
		node = emitContext()->parseNode(node);
		return node != nullptr &&
			   emitResolver->IsTopLevelValueImportEqualsWithEntityName(node);
	}
};

} // namespace

Transformer* NewImportElisionTransformer(TransformOptions* opt) {
	return ImportElisionTransformer::create(opt);
}

} // namespace tsc::transformers::tstransforms
