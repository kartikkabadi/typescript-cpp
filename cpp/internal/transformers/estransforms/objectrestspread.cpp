// Port of tsc/internal/transformers/estransforms/objectrestspread.go.
#include <unordered_set>

#include "internal/transformers/estransforms/estransforms.h"

namespace tsc::transformers::estransforms {
namespace {

// restoreBoolOnExit — models Go's `defer func() { flag = saved }()`.
struct restoreBoolOnExit {
	bool& flag;
	bool saved;
	~restoreBoolOnExit() { flag = saved; }
};

// oldParamScope — objectrestspread.go:123 (map[*ast.Node]struct{})
using oldParamScope = std::unordered_set<Node*>*;

struct ObjectRestSpreadTransformer : Transformer {
	const CompilerOptions* compilerOptions = nullptr;

	bool inExportedVariableStatement = false;
	bool expressionResultIsUnused = false;

	// Go `map[*ast.Node]struct{}` — nullptr models the nil map.
	std::unordered_set<Node*>* parametersWithPrecedingObjectRestOrSpread =
		nullptr;

	// visit — objectrestspread.go:20
	Node* visit(Node* node) {
		if ((node->subtreeFacts() & SubtreeContainsESObjectRestOrSpread) == 0 &&
			parametersWithPrecedingObjectRestOrSpread == nullptr) {
			return node;
		}
		// Save the expressionResultIsUnused flag set by the parent for this
		// node, then reset to false for children (the default). Specific cases
		// below override as needed.
		bool expressionResultIsUnused_ = expressionResultIsUnused;
		expressionResultIsUnused = false;
		restoreBoolOnExit restore_{expressionResultIsUnused,
								   expressionResultIsUnused_};
		switch (node->kind) {
		case Kind::SourceFile:
			return visitSourceFile(node->as<SourceFile>());
		case Kind::ObjectLiteralExpression:
			return visitObjectLiteralExpression(
				node->as<ObjectLiteralExpression>());
		case Kind::BinaryExpression:
			return visitBinaryExpression(node->as<BinaryExpression>(),
										 expressionResultIsUnused_);
		case Kind::ExpressionStatement:
			expressionResultIsUnused = true;
			return visitor()->visitEachChild(node);
		case Kind::ParenthesizedExpression:
			expressionResultIsUnused = expressionResultIsUnused_;
			return visitor()->visitEachChild(node);
		case Kind::ForOfStatement:
			return visitForOftatement(node->as<ForInOrOfStatement>());
		case Kind::VariableStatement:
			return visitVariableStatement(node->as<VariableStatement>());
		case Kind::VariableDeclaration:
			return visitVariableDeclaration(node->as<VariableDeclaration>());
		case Kind::CatchClause:
			return visitCatchClause(node->as<CatchClause>());
		case Kind::Parameter:
			return visitParameter(node->as<ParameterDeclaration>());
		case Kind::Constructor:
			return visitContructorDeclaration(
				node->as<ConstructorDeclaration>());
		case Kind::GetAccessor:
			return visitGetAccessorDeclaration(
				node->as<GetAccessorDeclaration>());
		case Kind::SetAccessor:
			return visitSetAccessorDeclaration(
				node->as<SetAccessorDeclaration>());
		case Kind::MethodDeclaration:
			return visitMethodDeclaration(node->as<MethodDeclaration>());
		case Kind::FunctionDeclaration:
			return visitFunctionDeclaration(node->as<FunctionDeclaration>());
		case Kind::ArrowFunction:
			return visitArrowFunction(node->as<ArrowFunction>());
		case Kind::FunctionExpression:
			return visitFunctionExpression(node->as<FunctionExpression>());
		default:
			return visitor()->visitEachChild(node);
		}
	}

	// visitSourceFile — objectrestspread.go:71
	Node* visitSourceFile(SourceFile* node) {
		Node* visited = visitor()->visitEachChild(node->asNode());
		for (printer::EmitHelper* helper :
			 emitContext()->readEmitHelpers()) {
			emitContext()->addEmitHelper(visited, helper);
		}
		return visited;
	}

	// visitParameter — objectrestspread.go:77
	Node* visitParameter(ParameterDeclaration* node) {
		if (parametersWithPrecedingObjectRestOrSpread != nullptr) {
			if (parametersWithPrecedingObjectRestOrSpread->count(
					node->asNode()) != 0) {
				Node* name = node->name;
				if (isBindingPattern(name)) {
					name = factory()->newGeneratedNameForNode(
						node->asNode());
				}
				return factory()->updateParameterDeclaration(
					node, nullptr, node->DotDotDotToken, name, nullptr,
					nullptr, nullptr);
			}
		}
		if ((node->subtreeFacts() & SubtreeContainsObjectRestOrSpread) != 0) {
			// Binding patterns are converted into a generated name and are
			// evaluated inside the function body.
			return factory()->updateParameterDeclaration(
				node, nullptr, node->DotDotDotToken,
				factory()->newGeneratedNameForNode(node->asNode()), nullptr,
				nullptr, visitor()->visitNode(node->Initializer));
		}
		return visitor()->visitEachChild(node->asNode());
	}

	// collectParametersWithPrecedingObjectRestOrSpread —
	// objectrestspread.go:111
	std::unordered_set<Node*>*
	collectParametersWithPrecedingObjectRestOrSpread(Node* node) {
		std::unordered_set<Node*>* result = nullptr;
		for (Node* parameter : node->parameters()) {
			if (result != nullptr) {
				result->insert(parameter);
			} else if ((parameter->subtreeFacts() &
						SubtreeContainsObjectRestOrSpread) != 0) {
				result = new std::unordered_set<Node*>();
			}
		}
		return result;
	}

	// enterParameterListContext — objectrestspread.go:125
	oldParamScope enterParameterListContext(Node* node) {
		std::unordered_set<Node*>* old =
			parametersWithPrecedingObjectRestOrSpread;
		parametersWithPrecedingObjectRestOrSpread =
			collectParametersWithPrecedingObjectRestOrSpread(node);
		return old;
	}

	// exitParameterListContext — objectrestspread.go:131
	void exitParameterListContext(oldParamScope scope) {
		parametersWithPrecedingObjectRestOrSpread = scope;
	}

	// visitContructorDeclaration — objectrestspread.go:135
	Node* visitContructorDeclaration(ConstructorDeclaration* node) {
		oldParamScope old = enterParameterListContext(node->asNode());
		Node* result = factory()->updateConstructorDeclaration(
			node, node->modifiers, nullptr,
			visitor()->visitNodes(node->Parameters), nullptr, nullptr,
			transformFunctionBody(node->asNode()));
		exitParameterListContext(old);
		return result;
	}

	// visitGetAccessorDeclaration — objectrestspread.go:149
	Node* visitGetAccessorDeclaration(GetAccessorDeclaration* node) {
		oldParamScope old = enterParameterListContext(node->asNode());
		Node* result = factory()->updateGetAccessorDeclaration(
			node, node->modifiers, visitor()->visitNode(node->name),
			nullptr, visitor()->visitNodes(node->Parameters), nullptr,
			nullptr, transformFunctionBody(node->asNode()));
		exitParameterListContext(old);
		return result;
	}

	// visitSetAccessorDeclaration — objectrestspread.go:164
	Node* visitSetAccessorDeclaration(SetAccessorDeclaration* node) {
		oldParamScope old = enterParameterListContext(node->asNode());
		Node* result = factory()->updateSetAccessorDeclaration(
			node, node->modifiers, visitor()->visitNode(node->name),
			nullptr, visitor()->visitNodes(node->Parameters), nullptr,
			nullptr, transformFunctionBody(node->asNode()));
		exitParameterListContext(old);
		return result;
	}

	// visitMethodDeclaration — objectrestspread.go:179
	Node* visitMethodDeclaration(MethodDeclaration* node) {
		oldParamScope old = enterParameterListContext(node->asNode());
		Node* result = factory()->updateMethodDeclaration(
			node, node->modifiers, node->AsteriskToken,
			visitor()->visitNode(node->name), node->PostfixToken, nullptr,
			visitor()->visitNodes(node->Parameters), nullptr, nullptr,
			transformFunctionBody(node->asNode()));
		exitParameterListContext(old);
		return result;
	}

	// visitFunctionDeclaration — objectrestspread.go:196
	Node* visitFunctionDeclaration(FunctionDeclaration* node) {
		oldParamScope old = enterParameterListContext(node->asNode());
		Node* result = factory()->updateFunctionDeclaration(
			node, node->modifiers, node->AsteriskToken,
			visitor()->visitNode(node->name), nullptr,
			visitor()->visitNodes(node->Parameters), nullptr, nullptr,
			transformFunctionBody(node->asNode()));
		exitParameterListContext(old);
		return result;
	}

	// visitArrowFunction — objectrestspread.go:212
	Node* visitArrowFunction(ArrowFunction* node) {
		oldParamScope old = enterParameterListContext(node->asNode());
		Node* result = factory()->updateArrowFunction(
			node, node->modifiers, nullptr,
			visitor()->visitNodes(node->Parameters), nullptr, nullptr,
			node->EqualsGreaterThanToken,
			transformFunctionBody(node->asNode()));
		exitParameterListContext(old);
		return result;
	}

	// visitFunctionExpression — objectrestspread.go:227
	Node* visitFunctionExpression(FunctionExpression* node) {
		oldParamScope old = enterParameterListContext(node->asNode());
		Node* result = factory()->updateFunctionExpression(
			node, node->modifiers, node->AsteriskToken,
			visitor()->visitNode(node->name), nullptr,
			visitor()->visitNodes(node->Parameters), nullptr, nullptr,
			transformFunctionBody(node->asNode()));
		exitParameterListContext(old);
		return result;
	}

	// transformFunctionBody — objectrestspread.go:243
	Node* transformFunctionBody(Node* node) {
		// EmitContext().VisitFunctionBody is not used here because this
		// transformer needs to inject object rest assignments between
		// visiting the body and merging the variable environment.
		emitContext()->startVariableEnvironment();
		Node* body = visitor()->visitNode(node->body());
		std::vector<Node*> extras = emitContext()->endVariableEnvironment();
		emitContext()->startVariableEnvironment();
		std::vector<Node*> newStatements = collectObjectRestAssignments(node);
		extras = emitContext()->endAndMergeVariableEnvironment(extras);
		if (newStatements.empty() && extras.empty()) {
			return body;
		}

		if (body == nullptr) {
			body = factory()->newBlock(factory()->newNodeList({}), true);
		}
		std::vector<Node*> prefix;
		std::vector<Node*> suffix;
		if (isBlock(body)) {
			bool custom = false;
			std::vector<Node*> bodyStatements = body->statements();
			for (size_t i = 0; i < bodyStatements.size(); i++) {
				Node* statement = bodyStatements[i];
				if (!custom && isPrologueDirective(statement)) {
					prefix.push_back(statement);
				} else if ((emitContext()->emitFlags(statement) &
							printer::EFCustomPrologue) != 0) {
					custom = true;
					prefix.push_back(statement);
				} else {
					suffix.assign(bodyStatements.begin() + i,
								  bodyStatements.end());
					break;
				}
			}
		} else {
			Node* ret = factory()->newReturnStatement(body);
			ret->loc = body->loc;
			NodeList* list = factory()->newNodeList({});
			list->loc = body->loc;
			body = factory()->newBlock(list, true);
			suffix.push_back(ret);
		}

		std::vector<Node*> combined;
		combined.reserve(prefix.size() + extras.size() +
						 newStatements.size() + suffix.size());
		combined.insert(combined.end(), prefix.begin(), prefix.end());
		combined.insert(combined.end(), extras.begin(), extras.end());
		combined.insert(combined.end(), newStatements.begin(),
						newStatements.end());
		combined.insert(combined.end(), suffix.begin(), suffix.end());
		NodeList* newStatementList = factory()->newNodeList(combined);
		newStatementList->loc = body->statementList()->loc;
		return factory()->updateBlock(body->as<Block>(), newStatementList,
									  body->as<Block>()->MultiLine);
	}

	// collectObjectRestAssignments — objectrestspread.go:288
	std::vector<Node*> collectObjectRestAssignments(Node* node) {
		bool containsPrecedingObjectRestOrSpread = false;
		std::vector<Node*> results;
		for (Node* parameter : node->parameters()) {
			if (containsPrecedingObjectRestOrSpread) {
				if (isBindingPattern(parameter->name())) {
					// In cases where a binding pattern is simply '[]' or
					// '{}', we usually don't want to emit a var declaration;
					// however, in the presence of an initializer, we must
					// emit that expression to preserve side effects.
					if (!parameter->name()->elements().empty()) {
						Node* declarations = flattenDestructuringBinding(
							this, parameter,
							factory()->newGeneratedNameForNode(parameter),
							FlattenLevel::All, false, false);
						if (declarations != nullptr) {
							Node* declarationList =
								factory()->newVariableDeclarationList(
									factory()->newNodeList({}),
									NodeFlagsNone);
							std::vector<Node*> decls{declarations};
							if (declarations->kind == Kind::SyntaxList) {
								decls = declarations->as<SyntaxList>()
											->Children;
							}
							NodeList* declarationsList =
								declarationList->as<VariableDeclarationList>()
									->Declarations;
							declarationsList->nodes.insert(
								declarationsList->nodes.end(), decls.begin(),
								decls.end());
							Node* statement =
								factory()->newVariableStatement(
									nullptr, declarationList);
							emitContext()->addEmitFlags(
								statement, printer::EFCustomPrologue);
							results.push_back(statement);
						}
					} else if (parameter->initializer() != nullptr) {
						Node* name =
							factory()->newGeneratedNameForNode(parameter);
						Node* initializer =
							visitor()->visitNode(parameter->initializer());
						Node* assignment =
							factory()->newAssignmentExpression(name,
															   initializer);
						Node* statement =
							factory()->newExpressionStatement(assignment);
						emitContext()->addEmitFlags(statement,
													printer::EFCustomPrologue);
						results.push_back(statement);
					}
				} else if (parameter->initializer() != nullptr) {
					// Converts a parameter initializer into a function body
					// statement, i.e.:
					//
					//  function f(x = 1) { }
					//
					// becomes
					//
					//  function f(x) {
					//    if (typeof x === "undefined") { x = 1; }
					//  }
					Node* name = parameter->name()->clone(*factory());
					name->loc = parameter->name()->loc;
					emitContext()->addEmitFlags(name, printer::EFNoSourceMap);

					Node* initializer =
						visitor()->visitNode(parameter->initializer());
					emitContext()->addEmitFlags(
						initializer,
						printer::EFNoSourceMap | printer::EFNoComments);

					Node* assignment =
						factory()->newAssignmentExpression(name, initializer);
					assignment->loc = parameter->loc;
					emitContext()->addEmitFlags(assignment,
												printer::EFNoComments);

					Node* block = factory()->newBlock(
						factory()->newNodeList(
							{factory()->newExpressionStatement(assignment)}),
						false);
					block->loc = parameter->loc;
					emitContext()->addEmitFlags(
						block,
						printer::EFSingleLine |
							printer::EFNoTrailingSourceMap |
							printer::EFNoTokenSourceMaps |
							printer::EFNoComments);

					Node* typeCheck = factory()->newTypeCheck(
						name->clone(*factory()), "undefined");
					Node* statement =
						factory()->newIfStatement(typeCheck, block, nullptr);
					statement->loc = parameter->loc;
					emitContext()->addEmitFlags(
						statement,
						printer::EFNoTokenSourceMaps |
							printer::EFNoTrailingSourceMap |
							printer::EFCustomPrologue |
							printer::EFNoComments |
							printer::EFStartOnNewLine);
					results.push_back(statement);
				}
			} else if ((parameter->subtreeFacts() &
						SubtreeContainsObjectRestOrSpread) != 0) {
				containsPrecedingObjectRestOrSpread = true;
				Node* declarations = flattenDestructuringBinding(
					this, parameter,
					factory()->newGeneratedNameForNode(parameter),
					FlattenLevel::ObjectRest, false, true);
				if (declarations != nullptr) {
					Node* declarationList =
						factory()->newVariableDeclarationList(
							factory()->newNodeList({}), NodeFlagsNone);
					std::vector<Node*> decls{declarations};
					if (declarations->kind == Kind::SyntaxList) {
						decls = declarations->as<SyntaxList>()->Children;
					}
					NodeList* declarationsList =
						declarationList->as<VariableDeclarationList>()
							->Declarations;
					declarationsList->nodes.insert(
						declarationsList->nodes.end(), decls.begin(),
						decls.end());
					Node* statement =
						factory()->newVariableStatement(nullptr,
														declarationList);
					emitContext()->addEmitFlags(statement,
												printer::EFCustomPrologue);
					results.push_back(statement);
				}
			}
		}
		return results;
	}

	// visitCatchClause — objectrestspread.go:378
	Node* visitCatchClause(CatchClause* node) {
		if (node->VariableDeclaration != nullptr &&
			isBindingPattern(node->VariableDeclaration->name()) &&
			(node->VariableDeclaration->name()->subtreeFacts() &
			 SubtreeContainsObjectRestOrSpread) != 0) {
			Node* name = factory()->newGeneratedNameForNode(
				node->VariableDeclaration->name());
			Node* updatedDecl = factory()->updateVariableDeclaration(
				node->VariableDeclaration->as<VariableDeclaration>(),
				node->VariableDeclaration->name(), nullptr, nullptr, name);
			Node* visitedBindings = flattenDestructuringBinding(
				this, updatedDecl, nullptr, FlattenLevel::ObjectRest, false,
				false);
			Node* block = visitor()->visitNode(node->Block);
			if (visitedBindings != nullptr) {
				std::vector<Node*> decls;
				if (visitedBindings->kind == Kind::SyntaxList) {
					decls = visitedBindings->as<SyntaxList>()->Children;
				} else {
					decls = {visitedBindings};
				}
				Node* newStatement = factory()->newVariableStatement(
					nullptr,
					factory()->newVariableDeclarationList(
						factory()->newNodeList(decls), NodeFlagsNone));
				std::vector<Node*> statements{newStatement};
				std::vector<Node*> blockStatements = block->statements();
				statements.insert(statements.end(), blockStatements.begin(),
								  blockStatements.end());
				NodeList* statementList = factory()->newNodeList(statements);
				statementList->loc = block->statementList()->loc;

				block = factory()->updateBlock(block->as<Block>(),
											   statementList,
											   block->as<Block>()->MultiLine);
			}
			return factory()->updateCatchClause(
				node,
				factory()->updateVariableDeclaration(
					node->VariableDeclaration->as<VariableDeclaration>(),
					name, nullptr, nullptr, nullptr),
				block);
		}
		return visitor()->visitEachChild(node->asNode());
	}

	// visitVariableStatement — objectrestspread.go:412
	Node* visitVariableStatement(VariableStatement* node) {
		if (hasSyntacticModifier(node->asNode(), ModifierFlagsExport)) {
			bool oldInExportedVariableStatement =
				inExportedVariableStatement;
			inExportedVariableStatement = true;
			Node* result = visitor()->visitEachChild(node->asNode());
			inExportedVariableStatement = oldInExportedVariableStatement;
			return result;
		}
		return visitor()->visitEachChild(node->asNode());
	}

	// visitVariableDeclaration — objectrestspread.go:423
	Node* visitVariableDeclaration(VariableDeclaration* node) {
		if (inExportedVariableStatement) {
			inExportedVariableStatement = false;
			Node* result = visitVariableDeclarationWorker(node, true);
			inExportedVariableStatement = true;
			return result;
		}
		return visitVariableDeclarationWorker(node, false);
	}

	// visitVariableDeclarationWorker — objectrestspread.go:433
	Node* visitVariableDeclarationWorker(VariableDeclaration* node,
										 bool exported) {
		// If we are here it is because the name contains a binding pattern
		// with a rest somewhere in it.
		if (isBindingPattern(node->name) &&
			(node->subtreeFacts() & SubtreeContainsObjectRestOrSpread) != 0) {
			return flattenDestructuringBinding(this, node->asNode(), nullptr,
											   FlattenLevel::ObjectRest,
											   exported, false);
		}
		return visitor()->visitEachChild(node->asNode());
	}

	// visitForOftatement — objectrestspread.go:445
	Node* visitForOftatement(ForInOrOfStatement* node) {
		if ((node->Initializer->subtreeFacts() &
			 SubtreeContainsObjectRestOrSpread) != 0 ||
			(isAssignmentPattern(node->Initializer) &&
			 containsObjectRestOrSpread(node->Initializer))) {
			Node* initializerWithoutParens =
				skipParentheses(node->Initializer);
			if (isVariableDeclarationList(initializerWithoutParens) ||
				isAssignmentPattern(initializerWithoutParens)) {
				TextRange bodyLocation{};
				TextRange statementsLocation{};
				Node* temp = factory()->newTempVariable();
				Node* res = visitor()->visitNode(
					factory()->createForOfBindingStatement(
						initializerWithoutParens, temp));
				std::vector<Node*> statements;
				statements.reserve(1);
				if (res != nullptr) {
					statements.push_back(res);
				}
				if (isBlock(node->Statement)) {
					for (Node* statement : node->Statement->statements()) {
						Node* visited =
							visitor()->visitEachChild(statement);
						if (visited != nullptr) {
							statements.push_back(visited);
						}
					}
					bodyLocation = node->Statement->loc;
					statementsLocation =
						node->Statement->statementList()->loc;
				} else if (node->Statement != nullptr) {
					statements.push_back(
						visitor()->visitEachChild(node->Statement));
					bodyLocation = node->Statement->loc;
					statementsLocation = node->Statement->loc;
				}

				Node* list = factory()->newVariableDeclarationList(
					factory()->newNodeList(
						{factory()->newVariableDeclaration(temp, nullptr,
														   nullptr,
														   nullptr)}),
					NodeFlagsLet);
				list->loc = node->Initializer->loc;

				Node* expr = visitor()->visitEachChild(node->Expression);

				NodeList* statementsList = factory()->newNodeList(statements);
				statementsList->loc = statementsLocation;

				Node* block = factory()->newBlock(statementsList, true);
				block->loc = bodyLocation;

				return factory()->updateForInOrOfStatement(
					node, node->AwaitModifier, list, expr, block);
			}
		}
		return visitor()->visitEachChild(node->asNode());
	}

	// visitBinaryExpression — objectrestspread.go:498
	Node* visitBinaryExpression(BinaryExpression* node,
								bool expressionResultIsUnused) {
		if (isDestructuringAssignment(node->asNode()) &&
			containsObjectRestOrSpread(node->Left)) {
			return flattenDestructuringAssignment(
				this, node->asNode(), !expressionResultIsUnused,
				FlattenLevel::ObjectRest, nullptr);
		}
		if (node->OperatorToken->kind == Kind::CommaToken) {
			this->expressionResultIsUnused = true;
			Node* left = visitor()->visitNode(node->Left);
			this->expressionResultIsUnused = expressionResultIsUnused;
			Node* right = visitor()->visitNode(node->Right);
			return factory()->updateBinaryExpression(node, nullptr, left,
													 nullptr,
													 node->OperatorToken,
													 right);
		}
		return visitor()->visitEachChild(node->asNode());
	}

	// visitObjectLiteralExpression — objectrestspread.go:516
	Node* visitObjectLiteralExpression(ObjectLiteralExpression* node) {
		if ((node->subtreeFacts() & SubtreeContainsObjectRestOrSpread) == 0) {
			return visitor()->visitEachChild(node->asNode());
		}
		// spread elements emit like so:
		// non-spread elements are chunked together into object literals, and
		// then all are passed to __assign:
		//     { a, ...o, b } => __assign(__assign({a}, o), {b});
		// If the first element is a spread element, then the first argument
		// to __assign is {}:
		//     { ...o, a, b, ...o2 } => __assign(__assign(__assign({}, o),
		//     {a, b}), o2)
		//
		// We cannot call __assign with more than two elements, since any
		// element could cause side effects. For example:
		//      var k = { a: 1, b: 2 };
		//      var o = { a: 3, ...k, b: k.a++ };
		//      // expected: { a: 1, b: 1 }
		// If we translate the above to `__assign({ a: 3 }, k,
		// { b: k.a++ })`, the `k.a++` will evaluate before `k` is spread and
		// we end up with `{ a: 2, b: 1 }`.
		//
		// This also occurs for spread elements, not just property
		// assignments:
		//      var k = { a: 1, get b() { l = { z: 9 }; return 2; } };
		//      var l = { c: 3 };
		//      var o = { ...k, ...l };
		//      // expected: { a: 1, b: 2, z: 9 }
		// If we translate the above to `__assign({}, k, l)`, the `l` will
		// evaluate before `k` is spread and we end up with
		// `{ a: 1, b: 2, c: 3 }`

		std::vector<Node*> objects =
			chunkObjectLiteralElements(node->Properties);
		if (!objects.empty() &&
			objects[0]->kind != Kind::ObjectLiteralExpression) {
			objects.insert(
				objects.begin(),
				factory()->newObjectLiteralExpression(
					factory()->newNodeList({}), false));
		}
		Node* expression = objects[0];
		if (objects.size() > 1) {
			for (size_t i = 0; i < objects.size(); i++) {
				if (i == 0) {
					continue;
				}
				Node* obj = objects[i];
				expression = factory()->newAssignHelper(
					{expression, obj},
					compilerOptions->GetEmitScriptTarget());
			}
			return expression;
		}
		return factory()->newAssignHelper(
			objects, compilerOptions->GetEmitScriptTarget());
	}

	// chunkObjectLiteralElements — objectrestspread.go:559
	std::vector<Node*> chunkObjectLiteralElements(NodeList* list) {
		if (list == nullptr || list->nodes.empty()) {
			return {};
		}
		std::vector<Node*> elements = list->nodes;
		std::vector<Node*> chunkObject;
		std::vector<Node*> objects;
		objects.reserve(1);
		for (Node* e : elements) {
			if (e->kind == Kind::SpreadAssignment) {
				if (!chunkObject.empty()) {
					objects.push_back(
						factory()->newObjectLiteralExpression(
							factory()->newNodeList(chunkObject), false));
					chunkObject.clear();
				}
				Node* target = e->expression();
				objects.push_back(visitor()->visitNode(target));
			} else {
				Node* elem = nullptr;
				if (e->kind == Kind::PropertyAssignment) {
					elem = factory()->newPropertyAssignment(
						nullptr, e->name(), nullptr, nullptr,
						visitor()->visitNode(e->initializer()));
				} else {
					elem = visitor()->visitNode(e);
				}
				chunkObject.push_back(elem);
			}
		}
		if (!chunkObject.empty()) {
			objects.push_back(factory()->newObjectLiteralExpression(
				factory()->newNodeList(chunkObject), false));
		}
		return objects;
	}

	// newObjectRestSpreadTransformer — objectrestspread.go:590
	static Transformer* create(TransformOptions* opt) {
		auto* tx = new ObjectRestSpreadTransformer;
		tx->compilerOptions = opt->CompilerOptions;
		return tx->newTransformer(
			[tx](Node* node) { return tx->visit(node); }, opt->Context);
	}
};

}  // namespace

Transformer* newObjectRestSpreadTransformer(TransformOptions* opt) {
	return ObjectRestSpreadTransformer::create(opt);
}

}  // namespace tsc::transformers::estransforms
