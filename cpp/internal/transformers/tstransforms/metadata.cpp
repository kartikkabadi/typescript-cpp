// Port of tsc/internal/transformers/tstransforms/metadata.go.
#include "internal/transformers/tstransforms/tstransforms.h"

#include "internal/checker/checker.h" // checker::EmitResolver

namespace tsc::transformers::tstransforms {
namespace {

constexpr bool USE_NEW_TYPE_METADATA_FORMAT = false;

struct MetadataTransformer : Transformer {
	bool legacyDecorators;
	checker::EmitResolver* resolver{};

	metadataSerializer* serializer{};
	ScriptTarget languageVersion;
	bool strictNullChecks;
	Node* parent{};
	Node* currentLexicalScope{};

	static Transformer* create(TransformOptions* opt) {
		auto* tx = new MetadataTransformer;
		tx->legacyDecorators =
			opt->CompilerOptions->ExperimentalDecorators == Tristate::True;
		tx->resolver = opt->EmitResolver;
		tx->languageVersion = opt->CompilerOptions->GetEmitScriptTarget();
		tx->strictNullChecks = opt->CompilerOptions->GetStrictOptionValue(
			opt->CompilerOptions->StrictNullChecks);
		return tx->newTransformer(
			[tx](Node* node) -> Node* { return tx->visit(node); },
			opt->Context);
	}

	void setParent(Node* node) { parent = node; }
	void setCurrentLexicalScope(Node* node) { currentLexicalScope = node; }

	Node* visit(Node* node) {
		if ((node->subtreeFacts() & SubtreeContainsDecorators) == 0) {
			return node;
		}

		switch (node->kind) {
		case Kind::ClassDeclaration:
			return visitClassDeclaration(node->as<ClassDeclaration>());
		case Kind::ClassExpression:
			return visitClassExpression(node->as<ClassExpression>());
		case Kind::ObjectLiteralExpression:
			return visitObjectLiteralExpression(node);
		case Kind::PropertyDeclaration:
			return visitPropertyDeclaration(
				node->as<PropertyDeclaration>());
		case Kind::MethodDeclaration:
			return visitMethodDeclaration(node->as<MethodDeclaration>());
		case Kind::SetAccessor:
			return visitSetAccessor(node->as<SetAccessorDeclaration>());
		case Kind::GetAccessor:
			return visitGetAccessor(node->as<GetAccessorDeclaration>());
		case Kind::SourceFile: {
			parent = nullptr;
			struct parentGuard {
				MetadataTransformer* tx;
				~parentGuard() { tx->setParent(nullptr); }
			} g1{this};
			currentLexicalScope = node;
			struct scopeGuard {
				MetadataTransformer* tx;
				~scopeGuard() { tx->setCurrentLexicalScope(nullptr); }
			} g2{this};
			serializer = newMetadataSerializer(
				resolver, factory(), emitContext(), languageVersion,
				strictNullChecks);
			Node* updated = visitor()->visitEachChild(node);
			for (printer::EmitHelper* helper : emitContext()->readEmitHelpers()) {
				emitContext()->addEmitHelper(updated, helper);
			}
			return updated;
		}
		case Kind::ModuleBlock:
		case Kind::Block:
		case Kind::CaseBlock: {
			Node* oldScope = currentLexicalScope;
			currentLexicalScope = node;
			struct scopeGuard {
				MetadataTransformer* tx;
				Node* old;
				~scopeGuard() { tx->setCurrentLexicalScope(old); }
			} g{this, oldScope};
			return visitor()->visitEachChild(node);
		}
		default:
			return visitor()->visitEachChild(node);
		}
	}

	Node* visitObjectLiteralExpression(Node* node) {
		Node* oldParent = parent;
		parent = node;
		struct parentGuard {
			MetadataTransformer* tx;
			Node* old;
			~parentGuard() { tx->setParent(old); }
		} g{this, oldParent};

		return visitor()->visitEachChild(node);
	}

	Node* visitClassExpression(ClassExpression* node) {
		Node* oldParent = parent;
		parent = node->asNode();
		struct parentGuard {
			MetadataTransformer* tx;
			Node* old;
			~parentGuard() { tx->setParent(old); }
		} g{this, oldParent};

		if (!classOrConstructorParameterIsDecorated(legacyDecorators,
												  node->asNode())) {
			return visitor()->visitEachChild(node->asNode());
		}
		ModifierList* modifiers = injectClassTypeMetadata(
			visitor()->visitModifiers(node->modifiers), node->asNode());
		return factory()->updateClassExpression(
			node, modifiers, visitor()->visitNode(node->name),
			visitor()->visitNodes(node->TypeParameters),
			visitor()->visitNodes(node->HeritageClauses),
			visitor()->visitNodes(node->Members));
	}

	Node* visitClassDeclaration(ClassDeclaration* node) {
		Node* oldParent = parent;
		parent = node->asNode();
		struct parentGuard {
			MetadataTransformer* tx;
			Node* old;
			~parentGuard() { tx->setParent(old); }
		} g{this, oldParent};

		if (!classOrConstructorParameterIsDecorated(legacyDecorators,
												  node->asNode())) {
			return visitor()->visitEachChild(node->asNode());
		}
		ModifierList* modifiers = injectClassTypeMetadata(
			visitor()->visitModifiers(node->modifiers), node->asNode());
		return factory()->updateClassDeclaration(
			node, modifiers, visitor()->visitNode(node->name),
			visitor()->visitNodes(node->TypeParameters),
			visitor()->visitNodes(node->HeritageClauses),
			visitor()->visitNodes(node->Members));
	}

	Node* visitPropertyDeclaration(PropertyDeclaration* node) {
		if (!hasDecorators(node->asNode())) {
			return visitor()->visitEachChild(node->asNode());
		}

		ModifierList* modifiers = injectClassElementTypeMetadata(
			visitor()->visitModifiers(node->modifiers), node->asNode(),
			parent);
		return factory()->updatePropertyDeclaration(
			node, modifiers, visitor()->visitNode(node->name),
			visitor()->visitNode(node->PostfixToken),
			visitor()->visitNode(node->Type),
			visitor()->visitNode(node->Initializer));
	}

	Node* visitMethodDeclaration(MethodDeclaration* node) {
		if (!hasDecorators(node->asNode()) &&
			getDecoratorsOfParameters(node->asNode()).empty()) {
			return visitor()->visitEachChild(node->asNode());
		}

		ModifierList* modifiers = injectClassElementTypeMetadata(
			visitor()->visitModifiers(node->modifiers), node->asNode(),
			parent);
		return factory()->updateMethodDeclaration(
			node, modifiers, visitor()->visitNode(node->AsteriskToken),
			visitor()->visitNode(node->name),
			visitor()->visitNode(node->PostfixToken),
			visitor()->visitNodes(node->TypeParameters),
			visitor()->visitNodes(node->Parameters),
			visitor()->visitNode(node->Type),
			visitor()->visitNode(node->FullSignature),
			visitor()->visitNode(node->Body));
	}

	Node* visitSetAccessor(SetAccessorDeclaration* node) {
		if (!hasDecorators(node->asNode()) &&
			getDecoratorsOfParameters(node->asNode()).empty()) {
			return visitor()->visitEachChild(node->asNode());
		}

		ModifierList* modifiers = injectClassElementTypeMetadata(
			visitor()->visitModifiers(node->modifiers), node->asNode(),
			parent);
		return factory()->updateSetAccessorDeclaration(
			node, modifiers, visitor()->visitNode(node->name),
			visitor()->visitNodes(node->TypeParameters),
			visitor()->visitNodes(node->Parameters),
			visitor()->visitNode(node->Type),
			visitor()->visitNode(node->FullSignature),
			visitor()->visitNode(node->Body));
	}

	Node* visitGetAccessor(GetAccessorDeclaration* node) {
		if (!hasDecorators(node->asNode())) {
			return visitor()->visitEachChild(node->asNode());
		}

		ModifierList* modifiers = injectClassElementTypeMetadata(
			visitor()->visitModifiers(node->modifiers), node->asNode(),
			parent);
		return factory()->updateGetAccessorDeclaration(
			node, modifiers, visitor()->visitNode(node->name),
			visitor()->visitNodes(node->TypeParameters),
			visitor()->visitNodes(node->Parameters),
			visitor()->visitNode(node->Type),
			visitor()->visitNode(node->FullSignature),
			visitor()->visitNode(node->Body));
	}

	ModifierList* injectClassTypeMetadata(ModifierList* list, Node* node) {
		std::vector<Node*> metadata = getTypeMetadata(node, node);
		if (!metadata.empty()) {
			std::vector<Node*> originalNodes;
			if (list != nullptr) {
				originalNodes = list->nodes;
			}
			if (originalNodes.empty()) {
				ModifierList* res = factory()->newModifierList(metadata);
				if (list != nullptr) {
					res->loc = list->loc;
				}
				return res;
			}
			std::vector<Node*> modifiersArray;
			if (isModifier(originalNodes[0]) &&
				(originalNodes[0]->kind == Kind::DefaultKeyword ||
				 originalNodes[0]->kind == Kind::ExportKeyword)) {
				modifiersArray.push_back(originalNodes[0]);
				if (originalNodes.size() > 1 &&
					(originalNodes[1]->kind == Kind::DefaultKeyword ||
					 originalNodes[1]->kind == Kind::ExportKeyword)) {
					modifiersArray.push_back(originalNodes[1]);
				}
			}
			size_t restStart = modifiersArray.size();
			for (Node* n : originalNodes) {
				if (isDecorator(n)) {
					modifiersArray.push_back(n);
				}
			}
			for (Node* m : metadata) {
				modifiersArray.push_back(m);
			}
			for (size_t i = restStart; i < originalNodes.size(); i++) {
				if (isModifier(originalNodes[i])) {
					modifiersArray.push_back(originalNodes[i]);
				}
			}
			ModifierList* res =
				factory()->newModifierList(modifiersArray);
			res->loc = list->loc;
			return res;
		}
		return list;
	}

	ModifierList* injectClassElementTypeMetadata(ModifierList* list,
												 Node* node,
												 Node* container) {
		if (!isClassLike(container)) {
			return list;
		}
		if (!classElementOrClassElementParameterIsDecorated(
				legacyDecorators, node, container)) {
			return list;
		}
		std::vector<Node*> metadata = getTypeMetadata(node, container);
		if (!metadata.empty()) {
			std::vector<Node*> originalNodes;
			if (list != nullptr) {
				originalNodes = list->nodes;
			}
			if (originalNodes.empty()) {
				ModifierList* res = factory()->newModifierList(metadata);
				if (list != nullptr) {
					res->loc = list->loc;
				}
				return res;
			}
			std::vector<Node*> modifiersArray;
			for (Node* n : originalNodes) {
				if (isDecorator(n)) {
					modifiersArray.push_back(n);
				}
			}
			for (Node* m : metadata) {
				modifiersArray.push_back(m);
			}
			for (Node* n : originalNodes) {
				if (isModifier(n)) {
					modifiersArray.push_back(n);
				}
			}
			ModifierList* res =
				factory()->newModifierList(modifiersArray);
			res->loc = list->loc;
			return res;
		}
		return list;
	}

	/**
	 * Gets optional type metadata for a declaration.
	 *
	 * @param node The declaration node.
	 */
	std::vector<Node*> getTypeMetadata(Node* node, Node* container) {
		// Decorator metadata is not yet supported for ES decorators.
		if (!legacyDecorators) {
			return {};
		}
		if (USE_NEW_TYPE_METADATA_FORMAT) {
			return getNewTypeMetadata(node, container);
		}
		return getOldTypeMetadata(node, container);
	}

	std::vector<Node*> getOldTypeMetadata(Node* node, Node* container) {
		std::vector<Node*> decorators;
		if (shouldAddTypeMetadata(node)) {
			Node* typeMetadata = factory()->newMetadataHelper(
				"design:type",
				serializer->SerializeTypeOfNode(
					metadataSerializerContext{currentLexicalScope,
											  container, false},
					node, container));
			decorators.push_back(factory()->newDecorator(typeMetadata));
		}
		if (shouldAddParamTypesMetadata(node)) {
			Node* paramTypesMetadata = factory()->newMetadataHelper(
				"design:paramtypes",
				serializer->SerializeParameterTypesOfNode(
					metadataSerializerContext{currentLexicalScope,
											  container, false},
					node, container));
			decorators.push_back(
				factory()->newDecorator(paramTypesMetadata));
		}
		if (shouldAddReturnTypeMetadata(node)) {
			Node* returnTypeMetadata = factory()->newMetadataHelper(
				"design:returntype",
				serializer->SerializeReturnTypeOfNode(
					metadataSerializerContext{currentLexicalScope,
											  container, false},
					node));
			decorators.push_back(
				factory()->newDecorator(returnTypeMetadata));
		}
		return decorators;
	}

	std::vector<Node*> getNewTypeMetadata(Node* node, Node* container) {
		std::vector<Node*> properties;
		if (shouldAddTypeMetadata(node)) {
			properties.push_back(factory()->newPropertyAssignment(
				nullptr, factory()->newIdentifier("type"), nullptr,
				nullptr,
				factory()->newArrowFunction(
					nullptr, nullptr, factory()->newNodeList({}),
					nullptr, nullptr,
					factory()->newToken(Kind::EqualsGreaterThanToken),
					serializer->SerializeTypeOfNode(
						metadataSerializerContext{currentLexicalScope,
												  container, false},
						node, container))));
		}
		if (shouldAddParamTypesMetadata(node)) {
			properties.push_back(factory()->newPropertyAssignment(
				nullptr, factory()->newIdentifier("paramTypes"), nullptr,
				nullptr,
				factory()->newArrowFunction(
					nullptr, nullptr, factory()->newNodeList({}),
					nullptr, nullptr,
					factory()->newToken(Kind::EqualsGreaterThanToken),
					serializer->SerializeParameterTypesOfNode(
						metadataSerializerContext{currentLexicalScope,
												  container, false},
						node, container))));
		}
		if (shouldAddReturnTypeMetadata(node)) {
			properties.push_back(factory()->newPropertyAssignment(
				nullptr, factory()->newIdentifier("returnType"), nullptr,
				nullptr,
				factory()->newArrowFunction(
					nullptr, nullptr, factory()->newNodeList({}),
					nullptr, nullptr,
					factory()->newToken(Kind::EqualsGreaterThanToken),
					serializer->SerializeReturnTypeOfNode(
						metadataSerializerContext{currentLexicalScope,
												  container, false},
						node))));
		}
		if (!properties.empty()) {
			Node* typeInfoMetadata = factory()->newMetadataHelper(
				"design:typeinfo",
				factory()->newObjectLiteralExpression(
					factory()->newNodeList(properties), true));
			return {factory()->newDecorator(typeInfoMetadata)};
		}
		return {};
	}

	/**
	 * Determines whether to emit the "design:type" metadata based on the
	 * node's kind. The caller should have already tested whether the node
	 * has decorators and whether the emitDecoratorMetadata compiler option
	 * is set.
	 *
	 * @param node The node to test.
	 */
	bool shouldAddTypeMetadata(Node* node) {
		switch (node->kind) {
		case Kind::MethodDeclaration:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
		case Kind::PropertyDeclaration:
			return true;
		}
		return false;
	}

	/**
	 * Determines whether to emit the "design:returntype" metadata based on
	 * the node's kind. The caller should have already tested whether the
	 * node has decorators and whether the emitDecoratorMetadata compiler
	 * option is set.
	 *
	 * @param node The node to test.
	 */
	bool shouldAddReturnTypeMetadata(Node* node) {
		return node->kind == Kind::MethodDeclaration;
	}

	/**
	 * Determines whether to emit the "design:paramtypes" metadata based on
	 * the node's kind. The caller should have already tested whether the
	 * node has decorators and whether the emitDecoratorMetadata compiler
	 * option is set.
	 *
	 * @param node The node to test.
	 */
	bool shouldAddParamTypesMetadata(Node* node) {
		switch (node->kind) {
		case Kind::ClassDeclaration:
		case Kind::ClassExpression:
			return getFirstConstructorWithBody(node) != nullptr;
		case Kind::MethodDeclaration:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
			return true;
		}
		return false;
	}
};

} // namespace

Transformer* NewMetadataTransformer(TransformOptions* opt) {
	return MetadataTransformer::create(opt);
}

} // namespace tsc::transformers::tstransforms
