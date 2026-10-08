// Port of tsc/internal/transformers/tstransforms/typeeraser.go.
#include "internal/transformers/tstransforms/tstransforms.h"

namespace tsc::transformers::tstransforms {
namespace {

struct TypeEraserTransformer : Transformer {
	const CompilerOptions* compilerOptions = nullptr;
	Node* parentNode = nullptr;
	Node* currentNode = nullptr;

	// NewTypeEraserTransformer — typeeraser.go:18
	static Transformer* create(TransformOptions* opt) {
		auto* tx = new TypeEraserTransformer;
		tx->compilerOptions = opt->CompilerOptions;
		return tx->newTransformer(
			[tx](Node* node) { return tx->visit(node); }, opt->Context);
	}

	// pushNode — typeeraser.go:26. Pushes a new child node onto the ancestor
	// tracking stack, returning the grandparent node to be restored later
	// via `popNode`.
	Node* pushNode(Node* node) {
		Node* grandparentNode = parentNode;
		parentNode = currentNode;
		currentNode = node;
		return grandparentNode;
	}

	// popNode — typeeraser.go:34. Pops the last child node off the ancestor
	// tracking stack, restoring the grandparent node.
	void popNode(Node* grandparentNode) {
		currentNode = parentNode;
		parentNode = grandparentNode;
	}

	// RAII helper for the Go `defer tx.popNode(grandparentNode)` pattern.
	struct ancestorGuard {
		TypeEraserTransformer* tx;
		Node* grandparentNode;
		ancestorGuard(TypeEraserTransformer* tx, Node* node)
			: tx(tx), grandparentNode(tx->pushNode(node)) {}
		~ancestorGuard() { tx->popNode(grandparentNode); }
	};

	// elide — typeeraser.go:39
	Node* elide(Node* node) {
		return emitContext()->newNotEmittedStatement(node);
	}

	// visit — typeeraser.go:43
	Node* visit(Node* node) {
		if ((node->subtreeFacts() & SubtreeContainsTypeScript) == 0) {
			return node;
		}

		if (isStatement(node) &&
			hasSyntacticModifier(node, ModifierFlagsAmbient)) {
			return elide(node);
		}

		ancestorGuard guard(this, node);

		switch (node->kind) {
		// TypeScript accessibility and readonly modifiers are elided
		case Kind::PublicKeyword:
		case Kind::PrivateKeyword:
		case Kind::ProtectedKeyword:
		case Kind::AbstractKeyword:
		case Kind::OverrideKeyword:
		case Kind::ConstKeyword:
		case Kind::DeclareKeyword:
		case Kind::ReadonlyKeyword:
		// TypeScript type nodes are elided.
		case Kind::ArrayType:
		case Kind::TupleType:
		case Kind::OptionalType:
		case Kind::RestType:
		case Kind::TypeLiteral:
		case Kind::TypePredicate:
		case Kind::TypeParameter:
		case Kind::AnyKeyword:
		case Kind::UnknownKeyword:
		case Kind::BooleanKeyword:
		case Kind::StringKeyword:
		case Kind::NumberKeyword:
		case Kind::NeverKeyword:
		case Kind::VoidKeyword:
		case Kind::SymbolKeyword:
		case Kind::ConstructorType:
		case Kind::FunctionType:
		case Kind::TypeQuery:
		case Kind::TypeReference:
		case Kind::UnionType:
		case Kind::IntersectionType:
		case Kind::ConditionalType:
		case Kind::ParenthesizedType:
		case Kind::ThisType:
		case Kind::TypeOperator:
		case Kind::IndexedAccessType:
		case Kind::MappedType:
		case Kind::LiteralType:
		// TypeScript index signatures are elided.
		case Kind::IndexSignature:
			return nullptr;

		case Kind::InKeyword:
		case Kind::OutKeyword:
			// TypeScript `in`/`out` variance modifiers are elided. These
			// keywords are only meaningful as modifiers on type parameters
			// (which are themselves elided), but they may appear as a grammar
			// error on other declarations and must not leak into the emitted
			// JS. The `in` binary operator shares this token kind, so only
			// elide when used as a modifier.
			if (parentNode == nullptr || !isBinaryExpression(parentNode)) {
				return nullptr;
			}
			return visitor()->visitEachChild(node);

		case Kind::JSImportDeclaration:
			// reparsed commonjs are elided
			return nullptr;
		case Kind::TypeAliasDeclaration:
		case Kind::JSTypeAliasDeclaration:
		case Kind::InterfaceDeclaration:
			// TypeScript type-only declarations are elided.
			return elide(node);

		case Kind::NamespaceExportDeclaration:
			// TypeScript namespace export declarations are elided.
			return nullptr;

		case Kind::ModuleDeclaration:
			if (!isIdentifier(node->name()) ||
				!isInstantiatedModule(
					node, compilerOptions->ShouldPreserveConstEnums()) ||
				getInnermostModuleDeclarationFromDottedModule(
						node->as<ModuleDeclaration>())
						->Body == nullptr) {
				// TypeScript module declarations are elided if they are not
				// instantiated or have no body
				return elide(node);
			}
			return visitor()->visitEachChild(node);

		case Kind::ExpressionWithTypeArguments: {
			auto* n = node->as<ExpressionWithTypeArguments>();
			return factory()->updateExpressionWithTypeArguments(
				n, visitor()->visitNode(n->Expression), nullptr);
		}

		case Kind::PropertyDeclaration: {
			auto* n = node->as<PropertyDeclaration>();
			if (tristateIsTrue(compilerOptions->ExperimentalDecorators) &&
				hasSyntacticModifier(
					node, ModifierFlagsAmbient | ModifierFlagsAbstract) &&
				hasDecorators(node)) {
				// declare/abstract props with decorators must be preserved
				// until the decorator transform can process them and remove
				// them
				return factory()->updatePropertyDeclaration(
					n, visitor()->visitModifiers(n->modifiers),
					visitor()->visitNode(n->name), nullptr, nullptr,
					visitor()->visitNode(n->Initializer));
			}
			if (hasSyntacticModifier(
					node, ModifierFlagsAmbient | ModifierFlagsAbstract)) {
				// TypeScript `declare` fields are elided
				return nullptr;
			}
			return factory()->updatePropertyDeclaration(
				n, visitor()->visitModifiers(n->modifiers),
				visitor()->visitNode(n->name), nullptr, nullptr,
				visitor()->visitNode(n->Initializer));
		}

		case Kind::Constructor: {
			auto* n = node->as<ConstructorDeclaration>();
			if (nodeIsMissing(n->Body)) {
				// TypeScript overloads are elided
				return nullptr;
			}
			return factory()->updateConstructorDeclaration(
				n, nullptr, nullptr, visitor()->visitNodes(n->Parameters),
				nullptr, nullptr, visitor()->visitNode(n->Body));
		}

		case Kind::MethodDeclaration: {
			auto* n = node->as<MethodDeclaration>();
			if (nodeIsMissing(n->Body)) {
				// TypeScript overloads are elided
				return nullptr;
			}
			return factory()->updateMethodDeclaration(
				n, visitor()->visitModifiers(n->modifiers),
				n->AsteriskToken, visitor()->visitNode(n->name), nullptr,
				nullptr, visitor()->visitNodes(n->Parameters), nullptr,
				nullptr, visitor()->visitNode(n->Body));
		}

		case Kind::GetAccessor: {
			auto* n = node->as<GetAccessorDeclaration>();
			if (nodeIsMissing(n->Body) &&
				hasSyntacticModifier(node, ModifierFlagsAbstract)) {
				// Abstract accessors are elided
				return nullptr;
			}
			Node* body = visitor()->visitNode(n->Body);
			if (body == nullptr) {
				body = factory()->newBlock(factory()->newNodeList({}), false);
			}
			return factory()->updateGetAccessorDeclaration(
				n, visitor()->visitModifiers(n->modifiers),
				visitor()->visitNode(n->name), nullptr,
				visitor()->visitNodes(n->Parameters), nullptr, nullptr, body);
		}

		case Kind::SetAccessor: {
			auto* n = node->as<SetAccessorDeclaration>();
			if (nodeIsMissing(n->Body) &&
				hasSyntacticModifier(node, ModifierFlagsAbstract)) {
				// Abstract accessors are elided
				return nullptr;
			}
			Node* body = visitor()->visitNode(n->Body);
			if (body == nullptr) {
				body = factory()->newBlock(factory()->newNodeList({}), false);
			}
			return factory()->updateSetAccessorDeclaration(
				n, visitor()->visitModifiers(n->modifiers),
				visitor()->visitNode(n->name), nullptr,
				visitor()->visitNodes(n->Parameters), nullptr, nullptr, body);
		}

		case Kind::VariableDeclaration: {
			auto* n = node->as<VariableDeclaration>();
			Node* updated = factory()->updateVariableDeclaration(
				n, visitor()->visitNode(n->name), nullptr, nullptr,
				visitor()->visitNode(n->Initializer));
			if (n->Type != nullptr) {
				emitContext()->setTypeNode(
					updated->as<VariableDeclaration>()->name, n->Type);
			}
			return updated;
		}

		case Kind::HeritageClause: {
			auto* n = node->as<HeritageClause>();
			if (n->Token == Kind::ImplementsKeyword) {
				// TypeScript `implements` clauses are elided
				return nullptr;
			}
			return factory()->updateHeritageClause(
				n, n->Token, visitor()->visitNodes(n->Types));
		}

		case Kind::ClassDeclaration: {
			auto* n = node->as<ClassDeclaration>();
			return factory()->updateClassDeclaration(
				n, visitor()->visitModifiers(n->modifiers),
				visitor()->visitNode(n->name), nullptr,
				visitor()->visitNodes(n->HeritageClauses),
				visitor()->visitNodes(n->Members));
		}

		case Kind::ClassExpression: {
			auto* n = node->as<ClassExpression>();
			return factory()->updateClassExpression(
				n, visitor()->visitModifiers(n->modifiers),
				visitor()->visitNode(n->name), nullptr,
				visitor()->visitNodes(n->HeritageClauses),
				visitor()->visitNodes(n->Members));
		}

		case Kind::FunctionDeclaration: {
			auto* n = node->as<FunctionDeclaration>();
			if (nodeIsMissing(n->Body)) {
				// TypeScript overloads are elided
				return elide(node);
			}
			return factory()->updateFunctionDeclaration(
				n, visitor()->visitModifiers(n->modifiers),
				n->AsteriskToken, visitor()->visitNode(n->name), nullptr,
				visitor()->visitNodes(n->Parameters), nullptr, nullptr,
				visitor()->visitNode(n->Body));
		}

		case Kind::FunctionExpression: {
			auto* n = node->as<FunctionExpression>();
			return factory()->updateFunctionExpression(
				n, visitor()->visitModifiers(n->modifiers),
				n->AsteriskToken, visitor()->visitNode(n->name), nullptr,
				visitor()->visitNodes(n->Parameters), nullptr, nullptr,
				visitor()->visitNode(n->Body));
		}

		case Kind::ArrowFunction: {
			auto* n = node->as<ArrowFunction>();
			return factory()->updateArrowFunction(
				n, visitor()->visitModifiers(n->modifiers), nullptr,
				visitor()->visitNodes(n->Parameters), nullptr, nullptr,
				n->EqualsGreaterThanToken, visitor()->visitNode(n->Body));
		}

		case Kind::Parameter: {
			if (isThisParameter(node)) {
				// TypeScript `this` parameters are elided
				return nullptr;
			}
			auto* n = node->as<ParameterDeclaration>();
			// preserve parameter property modifiers to be handled by the
			// runtime transformer
			ModifierList* modifiers = nullptr;
			if (isParameterPropertyDeclaration(node, parentNode)) {
				modifiers = extractModifiers(
					emitContext(), n->modifiers,
					ModifierFlagsParameterPropertyModifier);
			}
			// preserve decorators for the decorator transforms
			if (hasDecorators(node)) {
				std::vector<Node*> decorators = node->decorators();
				auto [visited, _] = visitor()->visitSlice(decorators);
				if (modifiers == nullptr) {
					modifiers = factory()->newModifierList(visited);
				} else {
					std::vector<Node*> concatenated = modifiers->nodes;
					concatenated.insert(concatenated.end(), visited.begin(),
										visited.end());
					modifiers = factory()->newModifierList(concatenated);
				}
			}
			return factory()->updateParameterDeclaration(
				n, modifiers, n->DotDotDotToken,
				visitor()->visitNode(n->name), nullptr, nullptr,
				visitor()->visitNode(n->Initializer));
		}

		case Kind::CallExpression: {
			auto* n = node->as<CallExpression>();
			return factory()->updateCallExpression(
				n, visitor()->visitNode(n->Expression), n->QuestionDotToken,
				nullptr, visitor()->visitNodes(n->Arguments), n->flags);
		}

		case Kind::NewExpression: {
			auto* n = node->as<NewExpression>();
			return factory()->updateNewExpression(
				n, visitor()->visitNode(n->Expression), nullptr,
				visitor()->visitNodes(n->Arguments));
		}

		case Kind::TaggedTemplateExpression: {
			auto* n = node->as<TaggedTemplateExpression>();
			return factory()->updateTaggedTemplateExpression(
				n, visitor()->visitNode(n->Tag), n->QuestionDotToken, nullptr,
				visitor()->visitNode(n->Template), n->flags);
		}

		case Kind::NonNullExpression:
		case Kind::TypeAssertionExpression:
		case Kind::AsExpression:
		case Kind::SatisfiesExpression: {
			Node* partial = factory()->newPartiallyEmittedExpression(
				visitor()->visitNode(node->expression()));
			emitContext()->setOriginal(partial, node);
			partial->loc = node->loc;
			return partial;
		}

		case Kind::ParenthesizedExpression: {
			if (!isJSDocTypeAssertion(node)) {
				auto* n = node->as<ParenthesizedExpression>();
				Node* expression = skipOuterExpressions(
					n->Expression,
					OEKAllExceptAssertionsOrExpressionsWithTypeArguments);
				if (isAssertionExpression(expression) ||
					isSatisfiesExpression(expression)) {
					Node* partial = factory()->newPartiallyEmittedExpression(
						visitor()->visitNode(n->Expression));
					emitContext()->setOriginal(partial, node);
					partial->loc = node->loc;
					return partial;
				}
			}
			return visitor()->visitEachChild(node);
		}

		case Kind::JsxSelfClosingElement: {
			auto* n = node->as<JsxSelfClosingElement>();
			return factory()->updateJsxSelfClosingElement(
				n, visitor()->visitNode(n->TagName), nullptr,
				visitor()->visitNode(n->Attributes));
		}

		case Kind::JsxOpeningElement: {
			auto* n = node->as<JsxOpeningElement>();
			return factory()->updateJsxOpeningElement(
				n, visitor()->visitNode(n->TagName), nullptr,
				visitor()->visitNode(n->Attributes));
		}

		case Kind::ImportEqualsDeclaration: {
			auto* n = node->as<ImportEqualsDeclaration>();
			if (n->IsTypeOnly) {
				// elide type-only imports
				return nullptr;
			}
			return visitor()->visitEachChild(node);
		}

		case Kind::ImportDeclaration: {
			auto* n = node->as<ImportDeclaration>();
			if (n->ImportClause == nullptr) {
				// Do not elide a side-effect only import declaration.
				//  import "foo";
				return node;
			}
			Node* importClause = visitor()->visitNode(n->ImportClause);
			if (importClause == nullptr) {
				return nullptr;
			}
			return factory()->updateImportDeclaration(
				n, n->modifiers, importClause, n->ModuleSpecifier,
				n->Attributes);
		}

		case Kind::ImportClause: {
			auto* n = node->as<ImportClause>();
			if (n->isTypeOnly()) {
				// Always elide type-only imports
				return nullptr;
			}
			Node* name = n->name;
			Node* namedBindings = visitor()->visitNode(n->NamedBindings);
			if (name == nullptr && namedBindings == nullptr) {
				// all import bindings were elided
				return nullptr;
			}
			return factory()->updateImportClause(n, n->PhaseModifier, name,
												 namedBindings);
		}

		case Kind::NamedImports: {
			auto* n = node->as<NamedImports>();
			if (n->Elements->nodes.empty()) {
				// Do not elide a side-effect only import declaration.
				return node;
			}
			NodeList* elements = visitor()->visitNodes(n->Elements);
			if (!tristateIsTrue(compilerOptions->VerbatimModuleSyntax) &&
				elements->nodes.empty()) {
				// all import specifiers were elided
				return nullptr;
			}
			return factory()->updateNamedImports(n, elements);
		}

		case Kind::ImportSpecifier: {
			auto* n = node->as<ImportSpecifier>();
			if (n->IsTypeOnly) {
				// elide type-only or unused imports
				return nullptr;
			}
			return node;
		}

		case Kind::ExportDeclaration: {
			auto* n = node->as<ExportDeclaration>();
			if (n->IsTypeOnly) {
				// elide type-only exports
				return nullptr;
			}
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
			if (n->Elements->nodes.empty()) {
				// Do not elide an empty export declaration.
				return node;
			}

			NodeList* elements = visitor()->visitNodes(n->Elements);
			if (!tristateIsTrue(compilerOptions->VerbatimModuleSyntax) &&
				elements->nodes.empty()) {
				// all export specifiers were elided
				return nullptr;
			}
			return factory()->updateNamedExports(n, elements);
		}

		case Kind::ExportSpecifier: {
			auto* n = node->as<ExportSpecifier>();
			if (n->IsTypeOnly) {
				// elide unused export
				return nullptr;
			}
			return node;
		}

		case Kind::EnumDeclaration:
			if (isEnumConst(node)) {
				return node;
			}
			return visitor()->visitEachChild(node);

		default:
			return visitor()->visitEachChild(node);
		}
	}
};

} // namespace

Transformer* NewTypeEraserTransformer(TransformOptions* opt) {
	return TypeEraserTransformer::create(opt);
}

} // namespace tsc::transformers::tstransforms
