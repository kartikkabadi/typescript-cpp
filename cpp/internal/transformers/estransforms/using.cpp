// Port of tsc/internal/transformers/estransforms/using.go — the downlevel
// transform for `using`/`await using` declarations (Explicit Resource
// Management) to ES targets without native support.
#include "internal/transformers/estransforms/estransforms.h"

#include "internal/printer/printer.h"

namespace tsc::transformers::estransforms {

namespace {

// usingKind — using.go:26
enum class usingKind : uint32_t {
	None,
	Sync,
	Async,
};

// isUsingVariableDeclarationList — using.go:744
bool isUsingVariableDeclarationList(Node* node);

// getUsingKindOfVariableDeclarationList — using.go:748
usingKind getUsingKindOfVariableDeclarationList(VariableDeclarationList* node) {
	switch (node->flags & NodeFlagsBlockScoped) {
	case NodeFlagsAwaitUsing:
		return usingKind::Async;
	case NodeFlagsUsing:
		return usingKind::Sync;
	default:
		return usingKind::None;
	}
}

// getUsingKindOfVariableStatement — using.go:758
usingKind getUsingKindOfVariableStatement(VariableStatement* node) {
	return getUsingKindOfVariableDeclarationList(
		node->DeclarationList->as<VariableDeclarationList>());
}

// getUsingKind — using.go:762
usingKind getUsingKind(Node* statement) {
	if (isVariableStatement(statement)) {
		return getUsingKindOfVariableStatement(
			statement->as<VariableStatement>());
	}
	return usingKind::None;
}

// getUsingKindOfStatements — using.go:769
usingKind getUsingKindOfStatements(const std::vector<Node*>& statements) {
	usingKind result = usingKind::None;
	for (Node* statement : statements) {
		usingKind kind = getUsingKind(statement);
		if (kind == usingKind::Async) {
			return usingKind::Async;
		}
		if (kind > result) {
			result = kind;
		}
	}
	return result;
}

bool isUsingVariableDeclarationList(Node* node) {
	return isVariableDeclarationList(node) &&
	       getUsingKindOfVariableDeclarationList(
	           node->as<VariableDeclarationList>()) != usingKind::None;
}

}  // anonymous namespace

// usingDeclarationTransformer — using.go:10
struct usingDeclarationTransformer : Transformer {
	std::unordered_map<std::string, Node*>* exportBindings = nullptr;
	std::vector<std::string> exportBindingNames;
	std::vector<Node*> exportVars;
	Node* defaultExportBinding = nullptr;
	Node* exportEqualsBinding = nullptr;

	Node* visit(Node* node);
	Node* visitSourceFile(SourceFile* node);
	Node* visitBlock(Block* node);
	Node* visitForStatement(ForStatement* node);
	Node* visitForOfStatement(ForInOrOfStatement* node);
	std::vector<Node*> transformUsingDeclarations(
		const std::vector<Node*>& statementsIn, Node* envBinding,
		std::vector<Node*>* topLevelStatements);
	void hoistImportOrExportOrHoistedDeclaration(
		Node* node, std::vector<Node*>* topLevelStatements);
	Node* hoistExportAssignment(ExportAssignment* node);
	Node* hoistExportDefault(ExportAssignment* node);
	Node* hoistExportEquals(ExportAssignment* node);
	Node* hoistClassDeclaration(ClassDeclaration* node);
	Node* hoistVariableStatement(VariableStatement* node);
	Node* hoistInitializedVariable(VariableDeclaration* node);
	void hoistBindingElement(Node* node, bool isExportedDeclaration,
	                         Node* original);
	void hoistBindingIdentifier(Node* node, bool isExport, Node* exportAlias,
	                            Node* original);
	Node* createEnvBinding();
	std::vector<Node*> createDownlevelUsingStatements(
		const std::vector<Node*>& bodyStatements, Node* envBinding,
		bool async_);
};

// newUsingDeclarationTransformer — using.go:19
Transformer* newUsingDeclarationTransformer(TransformOptions* opts) {
	auto* tx = new usingDeclarationTransformer();
	return tx->newTransformer([tx](Node* node) { return tx->visit(node); },
	                          opts->Context);
}

// visit — using.go:31
Node* usingDeclarationTransformer::visit(Node* node) {
	if ((node->subtreeFacts() & SubtreeContainsUsing) == 0) {
		return node;
	}

	switch (node->kind) {
	case Kind::SourceFile:
		node = visitSourceFile(node->as<SourceFile>());
		break;
	case Kind::Block:
		node = visitBlock(node->as<Block>());
		break;
	case Kind::ForStatement:
		node = visitForStatement(node->as<ForStatement>());
		break;
	case Kind::ForOfStatement:
		node = visitForOfStatement(node->as<ForInOrOfStatement>());
		break;
	default:
		node = visitor()->visitEachChild(node);
	}
	return node;
}

// visitSourceFile — using.go:53
Node* usingDeclarationTransformer::visitSourceFile(SourceFile* node) {
	if (node->IsDeclarationFile) {
		return node->asNode();
	}

	Node* visited = nullptr;
	usingKind kind = getUsingKindOfStatements(node->Statements->nodes);
	if (kind != usingKind::None) {
		// Imports and exports must stay at the top level. This means we must hoist all imports, exports, and
		// top-level function declarations and bindings out of the `try` statements we generate. For example:
		//
		// given:
		//
		//  import { w } from "mod";
		//  const x = expr1;
		//  using y = expr2;
		//  const z = expr3;
		//  export function f() {
		//    console.log(z);
		//  }
		//
		// produces:
		//
		//  import { x } from "mod";        // <-- preserved
		//  const x = expr1;                // <-- preserved
		//  var y, z;                       // <-- hoisted
		//  export function f() {           // <-- hoisted
		//    console.log(z);
		//  }
		//  const env_1 = { stack: [], error: void 0, hasError: false };
		//  try {
		//    y = __addDisposableResource(env_1, expr2, false);
		//    z = expr3;
		//  }
		//  catch (e_1) {
		//    env_1.error = e_1;
		//    env_1.hasError = true;
		//  }
		//  finally {
		//    __disposeResource(env_1);
		//  }
		//
		// In this transformation, we hoist `y`, `z`, and `f` to a new outer statement list while moving all other
		// statements in the source file into the `try` block, which is the same approach we use for System module
		// emit. Unlike System module emit, we attempt to preserve all statements prior to the first top-level
		// `using` to isolate the complexity of the transformed output to only where it is necessary.
		emitContext()->startVariableEnvironment();

		exportBindings =
			new std::unordered_map<std::string, Node*>();
		exportVars.clear();

		auto [prologue, rest] =
			factory()->splitStandardPrologue(node->Statements->nodes);
		std::vector<Node*> topLevelStatements;
		for (Node* s : visitor()->visitSlice(prologue).first) {
			topLevelStatements.push_back(s);
		}

		// Collect and transform any leading statements up to the first `using` or `await using`. This preserves
		// the original statement order much as is possible.

		size_t pos = 0;
		while (pos < rest.size()) {
			Node* statement = rest[pos];
			if (getUsingKind(statement) != usingKind::None) {
				if (pos > 0) {
					std::vector<Node*> prefix(rest.begin(),
					                          rest.begin() + pos);
					for (Node* s :
					     visitor()->visitSlice(prefix).first) {
						topLevelStatements.push_back(s);
					}
				}
				break;
			}
			pos++;
		}

		if (pos >= rest.size()) {
			TSC_UNREACHABLE(
				"Should have encountered at least one 'using' statement.");
		}

		// transform the rest of the body
		Node* envBinding = createEnvBinding();
		std::vector<Node*> remainder(rest.begin() + pos, rest.end());
		std::vector<Node*> bodyStatements = transformUsingDeclarations(
			remainder, envBinding, &topLevelStatements);

		// add `export {}` declarations for any hoisted bindings.
		if (!exportBindings->empty()) {
			std::vector<Node*> exportSpecifiers;
			exportSpecifiers.reserve(exportBindingNames.size());
			for (const std::string& name : exportBindingNames) {
				Node* specifier = (*exportBindings)[name];
				if (specifier == nullptr) {
					TSC_UNREACHABLE(
						"Missing export binding for hoisted export name");
				}
				exportSpecifiers.push_back(specifier);
			}
			topLevelStatements.push_back(factory()->newExportDeclaration(
				nullptr, /*modifiers*/
				false,   /*isTypeOnly*/
				factory()->newNamedExports(
					factory()->newNodeList(exportSpecifiers)),
				nullptr, /*moduleSpecifier*/
				nullptr  /*attributes*/
				));
		}

		for (Node* s : emitContext()->endVariableEnvironment()) {
			topLevelStatements.push_back(s);
		}
		if (!exportVars.empty()) {
			topLevelStatements.push_back(factory()->newVariableStatement(
				factory()->newModifierList(
					std::vector<Node*>{
						factory()->newToken(Kind::ExportKeyword)}),
				factory()->newVariableDeclarationList(
					factory()->newNodeList(exportVars), NodeFlagsLet)));
		}
		for (Node* s : createDownlevelUsingStatements(
			     bodyStatements, envBinding, kind == usingKind::Async)) {
			topLevelStatements.push_back(s);
		}

		if (exportEqualsBinding != nullptr) {
			topLevelStatements.push_back(factory()->newExportAssignment(
				nullptr, /*modifiers*/
				true,    /*isExportEquals*/
				nullptr, /*typeNode*/
				exportEqualsBinding));
		}

		visited = factory()->updateSourceFile(
			node, factory()->newNodeList(topLevelStatements),
			node->EndOfFileToken);
	} else {
		visited = visitor()->visitEachChild(node->asNode());
	}
	for (printer::EmitHelper* helper : emitContext()->readEmitHelpers()) {
		emitContext()->addEmitHelper(visited, helper);
	}
	exportVars.clear();
	delete exportBindings;
	exportBindings = nullptr;
	exportBindingNames.clear();
	defaultExportBinding = nullptr;
	exportEqualsBinding = nullptr;
	return visited;
}

// visitBlock — using.go:192
Node* usingDeclarationTransformer::visitBlock(Block* node) {
	usingKind kind = getUsingKindOfStatements(node->Statements->nodes);
	if (kind != usingKind::None) {
		auto [prologue, rest] =
			factory()->splitStandardPrologue(node->Statements->nodes);
		Node* envBinding = createEnvBinding();
		std::vector<Node*> statements;
		statements.reserve(prologue.size() + 2);
		for (Node* s : visitor()->visitSlice(prologue).first) {
			statements.push_back(s);
		}
		for (Node* s : createDownlevelUsingStatements(
			     transformUsingDeclarations(rest, envBinding,
			                                nullptr /*topLevelStatements*/),
			     envBinding, kind == usingKind::Async)) {
			statements.push_back(s);
		}
		NodeList* statementList = factory()->newNodeList(statements);
		statementList->loc = node->Statements->loc;
		return factory()->updateBlock(node, statementList, node->MultiLine);
	}
	return visitor()->visitEachChild(node->asNode());
}

// visitForStatement — using.go:210
Node* usingDeclarationTransformer::visitForStatement(ForStatement* node) {
	if (node->Initializer != nullptr &&
	    isUsingVariableDeclarationList(node->Initializer)) {
		// given:
		//
		//  for (using x = expr; cond; incr) { ... }
		//
		// produces a shallow transformation to:
		//
		//  {
		//    using x = expr;
		//    for (; cond; incr) { ... }
		//  }
		//
		// before handing the shallow transformation back to the visitor for an in-depth transformation.
		return visitor()->visitNode(factory()->newBlock(
			factory()->newNodeList(std::vector<Node*>{
				factory()->newVariableStatement(nullptr /*modifiers*/,
				                              node->Initializer),
				factory()->updateForStatement(
					node,
					nullptr, /*initializer*/
					node->Condition, node->Incrementor, node->Statement),
			}),
			false /*multiLine*/));
	}
	return visitor()->visitEachChild(node->asNode());
}

// visitForOfStatement — using.go:236
Node* usingDeclarationTransformer::visitForOfStatement(
	ForInOrOfStatement* node) {
	if (isUsingVariableDeclarationList(node->Initializer)) {
		// given:
		//
		//  for (using x of y) { ... }
		//
		// produces a shallow transformation to:
		//
		//  for (const x_1 of y) {
		//    using x = x;
		//    ...
		//  }
		//
		// before handing the shallow transformation back to the visitor for an in-depth transformation.
		VariableDeclarationList* forInitializer =
			node->Initializer->as<VariableDeclarationList>();
		Node* forDecl = forInitializer->Declarations->nodes.empty()
		                    ? nullptr
		                    : forInitializer->Declarations->nodes[0];
		if (forDecl == nullptr) {
			forDecl = factory()->newVariableDeclaration(
				factory()->newTempVariable(), nullptr, nullptr, nullptr);
		}

		bool isAwaitUsing =
			getUsingKindOfVariableDeclarationList(forInitializer) ==
			usingKind::Async;
		Node* temp = factory()->newGeneratedNameForNode(forDecl->name());
		Node* usingVar = factory()->updateVariableDeclaration(
			forDecl->as<VariableDeclaration>(), forDecl->name(),
			nullptr /*exclamationToken*/, nullptr /*type*/, temp);
		Node* usingVarList = factory()->newVariableDeclarationList(
			factory()->newNodeList(std::vector<Node*>{usingVar}),
			isAwaitUsing ? NodeFlagsAwaitUsing : NodeFlagsUsing);
		Node* usingVarStatement = factory()->newVariableStatement(
			nullptr /*modifiers*/, usingVarList);
		Node* statement = nullptr;
		if (isBlock(node->Statement)) {
			std::vector<Node*> statements;
			statements.reserve(node->Statement->statements().size() + 1);
			statements.push_back(usingVarStatement);
			for (Node* s : node->Statement->statements()) {
				statements.push_back(s);
			}
			statement = factory()->updateBlock(
				node->Statement->as<Block>(),
				factory()->newNodeList(statements),
				node->Statement->as<Block>()->MultiLine);
		} else {
			statement = factory()->newBlock(
				factory()->newNodeList(std::vector<Node*>{
					usingVarStatement, node->Statement}),
				true /*multiLine*/);
		}
		return visitor()->visitNode(factory()->updateForInOrOfStatement(
			node, node->AwaitModifier,
			factory()->newVariableDeclarationList(
				factory()->newNodeList(std::vector<Node*>{
					factory()->newVariableDeclaration(
						temp, nullptr /*exclamationToken*/,
						nullptr /*type*/, nullptr),
				}),
				NodeFlagsConst),
			node->Expression, statement));
	}
	return visitor()->visitEachChild(node->asNode());
}

// transformUsingDeclarations — using.go:291
std::vector<Node*> usingDeclarationTransformer::transformUsingDeclarations(
	const std::vector<Node*>& statementsIn, Node* envBinding,
	std::vector<Node*>* topLevelStatements) {
	std::vector<Node*> statements;

	std::function<Node*(Node*)> hoist = [&](Node* node) -> Node* {
		if (topLevelStatements == nullptr) {
			return node;
		}

		switch (node->kind) {
		case Kind::ImportDeclaration:
		case Kind::ImportEqualsDeclaration:
		case Kind::ExportDeclaration:
		case Kind::FunctionDeclaration:
			hoistImportOrExportOrHoistedDeclaration(node,
			                                      topLevelStatements);
			return nullptr;
		case Kind::ExportAssignment:
			return hoistExportAssignment(node->as<ExportAssignment>());
		case Kind::ClassDeclaration:
			return hoistClassDeclaration(node->as<ClassDeclaration>());
		case Kind::VariableStatement:
			return hoistVariableStatement(node->as<VariableStatement>());
		default:
			break;
		}

		return node;
	};

	std::function<void(Node*)> hoistOrAppendNode = [&](Node* node) {
		node = hoist(node);
		if (node != nullptr) {
			statements.push_back(node);
		}
	};

	for (Node* statement : statementsIn) {
		usingKind kind = getUsingKind(statement);
		if (kind != usingKind::None) {
			VariableStatement* varStatement =
				statement->as<VariableStatement>();
			Node* declarationList = varStatement->DeclarationList;
			std::vector<Node*> declarations;
			for (Node* declaration :
			     declarationList->as<VariableDeclarationList>()
				     ->Declarations->nodes) {
				if (!isIdentifier(declaration->name())) {
					// Since binding patterns are a grammar error, we reset `declarations` so we don't process this as a `using`.
					declarations.clear();
					break;
				}

				// perform a shallow transform for any named evaluation
				if (isNamedEvaluation(emitContext(), declaration)) {
					declaration = transformNamedEvaluation(
						emitContext(), declaration,
						false /*ignoreEmptyStringLiteral*/,
						"" /*assignedName*/);
				}

				Node* initializer =
					visitor()->visitNode(declaration->initializer());
				if (initializer == nullptr) {
					initializer =
						factory()->newVoidZeroExpression();
				}
				declarations.push_back(
					factory()->updateVariableDeclaration(
						declaration->as<VariableDeclaration>(),
						declaration->name(),
						nullptr, /*exclamationToken*/
						nullptr, /*type*/
						factory()->newAddDisposableResourceHelper(
							envBinding, initializer,
							kind == usingKind::Async)));
			}

			// Only replace the statement if it was valid.
			if (!declarations.empty()) {
				Node* varList = factory()->newVariableDeclarationList(
					factory()->newNodeList(declarations),
					NodeFlagsConst);
				emitContext()->setOriginal(varList, declarationList);
				varList->loc = declarationList->loc;
				hoistOrAppendNode(factory()->updateVariableStatement(
					varStatement, nullptr /*modifiers*/, varList));
				continue;
			}
		}

		Node* result = visit(statement);
		if (result != nullptr) {
			if (result->kind == Kind::SyntaxList) {
				for (Node* child :
				     result->as<SyntaxList>()->Children) {
					hoistOrAppendNode(child);
				}
			} else {
				hoistOrAppendNode(result);
			}
		}
	}
	return statements;
}

// hoistImportOrExportOrHoistedDeclaration — using.go:361
void usingDeclarationTransformer::hoistImportOrExportOrHoistedDeclaration(
	Node* node, std::vector<Node*>* topLevelStatements) {
	// NOTE: `node` has already been visited
	topLevelStatements->push_back(node);
}

// hoistExportAssignment — using.go:366
Node* usingDeclarationTransformer::hoistExportAssignment(
	ExportAssignment* node) {
	if (node->IsExportEquals) {
		return hoistExportEquals(node);
	}
	return hoistExportDefault(node);
}

// hoistExportDefault — using.go:374
Node* usingDeclarationTransformer::hoistExportDefault(ExportAssignment* node) {
	// NOTE: `node` has already been visited
	if (defaultExportBinding != nullptr) {
		// invalid case of multiple `export default` declarations. Don't assert here, just pass it through
		return node->asNode();
	}

	// given:
	//
	//   export default expr;
	//
	// produces:
	//
	//   // top level
	//   var default_1;
	//   export { default_1 as default };
	//
	//   // body
	//   default_1 = expr;

	defaultExportBinding = factory()->newUniqueName(
		"_default",
		printer::AutoGenerateOptions{
			printer::GeneratedIdentifierFlagsReservedInNestedScopes |
				printer::GeneratedIdentifierFlagsFileLevel |
				printer::GeneratedIdentifierFlagsOptimistic,
			"", ""});
	hoistBindingIdentifier(defaultExportBinding, true /*isExport*/,
	                       factory()->newIdentifier("default"),
	                       node->asNode());

	// give a class or function expression an assigned name, if needed.
	Node* expression = node->Expression;
	Node* innerExpression = skipOuterExpressions(expression, OEKAll);
	if (isNamedEvaluation(emitContext(), innerExpression)) {
		innerExpression = transformNamedEvaluation(
			emitContext(), innerExpression,
			false /*ignoreEmptyStringLiteral*/, "default");
		expression = factory()->restoreOuterExpressions(
			expression, innerExpression, OEKAll);
	}

	Node* assignment =
		factory()->newAssignmentExpression(defaultExportBinding, expression);
	return factory()->newExpressionStatement(assignment);
}

// hoistExportEquals — using.go:404
Node* usingDeclarationTransformer::hoistExportEquals(ExportAssignment* node) {
	// NOTE: `node` has already been visited
	if (exportEqualsBinding != nullptr) {
		// invalid case of multiple `export default` declarations. Don't assert here, just pass it through
		return node->asNode();
	}

	// given:
	//
	//   export = expr;
	//
	// produces:
	//
	//   // top level
	//   var default_1;
	//
	//   try {
	//       // body
	//       default_1 = expr;
	//   } ...
	//
	//   // top level suffix
	//   export = default_1;

	exportEqualsBinding = factory()->newUniqueName(
		"_default",
		printer::AutoGenerateOptions{
			printer::GeneratedIdentifierFlagsReservedInNestedScopes |
				printer::GeneratedIdentifierFlagsFileLevel |
				printer::GeneratedIdentifierFlagsOptimistic,
			"", ""});
	emitContext()->addVariableDeclaration(exportEqualsBinding);

	// give a class or function expression an assigned name, if needed.
	Node* assignment = factory()->newAssignmentExpression(exportEqualsBinding,
	                                                    node->Expression);
	return factory()->newExpressionStatement(assignment);
}

// hoistClassDeclaration — using.go:432
Node* usingDeclarationTransformer::hoistClassDeclaration(
	ClassDeclaration* node) {
	// NOTE: `node` has already been visited
	if (node->name == nullptr && defaultExportBinding != nullptr) {
		// invalid case of multiple `export default` declarations. Don't assert here, just pass it through
		return node->asNode();
	}

	bool isExported =
		hasSyntacticModifier(node->asNode(), ModifierFlagsExport);
	bool isDefault =
		hasSyntacticModifier(node->asNode(), ModifierFlagsDefault);

	// When hoisting a class declaration at the top level of a file containing a top-level `using` statement, we
	// must first convert it to a class expression so that we can hoist the binding outside of the `try`.
	Node* expression =
		convertClassDeclarationToClassExpression(emitContext(), node->asNode());
	if (node->name != nullptr) {
		// given:
		//
		//  using x = expr;
		//  class C {}
		//
		// produces:
		//
		//  var x, C;
		//  const env_1 = { ... };
		//  try {
		//    x = __addDisposableResource(env_1, expr, false);
		//    C = class {};
		//  }
		//  catch (e_1) {
		//    env_1.error = e_1;
		//    env_1.hasError = true;
		//  }
		//  finally {
		//    __disposeResources(env_1);
		//  }
		//
		// If the class is exported, we also produce an `export { C };`
		hoistBindingIdentifier(
			factory()->getLocalName(node->asNode()),
			isExported && !isDefault, nullptr /*exportAlias*/,
			node->asNode());
		expression = factory()->newAssignmentExpression(
			factory()->getDeclarationName(node->asNode()), expression);
		emitContext()->setOriginal(expression, node->asNode());
		emitContext()->setSourceMapRange(expression, node->loc);
		emitContext()->setCommentRange(expression, node->loc);
		if (isNamedEvaluation(emitContext(), expression)) {
			expression = transformNamedEvaluation(
				emitContext(), expression,
				false /*ignoreEmptyStringLiteral*/,
				"" /*assignedName*/);
		}
	}

	if (isDefault && defaultExportBinding == nullptr) {
		// In the case of a default export, we create a temporary variable that we export as the default and then
		// assign to that variable.
		//
		// given:
		//
		//  using x = expr;
		//  export default class C {}
		//
		// produces:
		//
		//  export { default_1 as default };
		//  var x, C, default_1;
		//  const env_1 = { ... };
		//  try {
		//    x = __addDisposableResource(env_1, expr, false);
		//    default_1 = C = class {};
		//  }
		//  catch (e_1) {
		//    env_1.error = e_1;
		//    env_1.hasError = true;
		//  }
		//  finally {
		//    __disposeResources(env_1);
		//  }
		//
		// Though we will never reassign `default_1`, this most closely matches the specified runtime semantics.
		defaultExportBinding = factory()->newUniqueName(
			"_default",
			printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsReservedInNestedScopes |
					printer::GeneratedIdentifierFlagsFileLevel |
					printer::GeneratedIdentifierFlagsOptimistic,
				"", ""});
		hoistBindingIdentifier(defaultExportBinding, true /*isExport*/,
		                       factory()->newIdentifier("default"),
		                       node->asNode());
		expression = factory()->newAssignmentExpression(
			defaultExportBinding, expression);
		emitContext()->setOriginal(expression, node->asNode());
		if (isNamedEvaluation(emitContext(), expression)) {
			expression = transformNamedEvaluation(
				emitContext(), expression,
				false /*ignoreEmptyStringLiteral*/, "default");
		}
	}

	return factory()->newExpressionStatement(expression);
}

// hoistVariableStatement — using.go:532
Node* usingDeclarationTransformer::hoistVariableStatement(
	VariableStatement* node) {
	// NOTE: `node` has already been visited
	std::vector<Node*> expressions;
	bool isExported =
		hasSyntacticModifier(node->asNode(), ModifierFlagsExport);
	for (Node* variable :
	     node->DeclarationList->as<VariableDeclarationList>()
		     ->Declarations->nodes) {
		hoistBindingElement(variable, isExported, variable);
		if (variable->initializer() != nullptr) {
			expressions.push_back(hoistInitializedVariable(
				variable->as<VariableDeclaration>()));
		}
	}
	if (!expressions.empty()) {
		Node* statement = factory()->newExpressionStatement(
			factory()->inlineExpressions(expressions));
		emitContext()->setOriginal(statement, node->asNode());
		emitContext()->setCommentRange(statement, node->loc);
		emitContext()->setSourceMapRange(statement, node->loc);
		return statement;
	}
	return nullptr;
}

// hoistInitializedVariable — using.go:549
Node* usingDeclarationTransformer::hoistInitializedVariable(
	VariableDeclaration* node) {
	// NOTE: `node` has already been visited
	if (node->Initializer == nullptr) {
		TSC_UNREACHABLE("Expected initializer");
	}
	Node* target = nullptr;
	if (isIdentifier(node->name)) {
		target = node->name->clone(*factory());
		emitContext()->setEmitFlags(
			target,
			emitContext()->emitFlags(target) &
				~(printer::EFLocalName | printer::EFExportName));
	} else {
		target = convertBindingPatternToAssignmentPattern(
			emitContext(), node->name->as<BindingPattern>());
	}

	Node* assignment =
		factory()->newAssignmentExpression(target, node->Initializer);
	emitContext()->setOriginal(assignment, node->asNode());
	emitContext()->setCommentRange(assignment, node->loc);
	emitContext()->setSourceMapRange(assignment, node->loc);
	return assignment;
}

// hoistBindingElement — using.go:568
void usingDeclarationTransformer::hoistBindingElement(
	Node* node, bool isExportedDeclaration, Node* original) {
	// NOTE: `node` has already been visited
	if (isBindingPattern(node->name())) {
		for (Node* element : node->name()->elements()) {
			if (element->name() != nullptr) {
				hoistBindingElement(element, isExportedDeclaration,
				                    original);
			}
		}
	} else {
		hoistBindingIdentifier(node->name(), isExportedDeclaration,
		                       nullptr /*exportAlias*/, original);
	}
}

// hoistBindingIdentifier — using.go:579
void usingDeclarationTransformer::hoistBindingIdentifier(
	Node* node, bool isExport, Node* exportAlias, Node* original) {
	// NOTE: `node` has already been visited
	Node* name = node;
	if (!isGeneratedIdentifier(emitContext(), node)) {
		name = name->clone(*factory());
	}
	if (isExport) {
		if (exportAlias == nullptr &&
		    !isLocalName(emitContext(), name)) {
			Node* varDecl = factory()->newVariableDeclaration(
				name, nullptr /*exclamationToken*/,
				nullptr /*type*/, nullptr /*initializer*/);
			if (original != nullptr) {
				emitContext()->setOriginal(varDecl, original);
			}
			exportVars.push_back(varDecl);
			return;
		}

		Node* localName = nullptr;
		Node* exportName = nullptr;
		if (exportAlias != nullptr) {
			localName = name;
			exportName = exportAlias;
		} else {
			exportName = name;
		}
		Node* specifier = factory()->newExportSpecifier(
			/*isTypeOnly*/ false, localName, exportName);
		if (original != nullptr) {
			emitContext()->setOriginal(specifier, original);
		}
		if (exportBindings == nullptr) {
			exportBindings = new std::unordered_map<std::string, Node*>();
		}
		if (exportBindings->find(name->text()) == exportBindings->end()) {
			exportBindingNames.push_back(name->text());
		}
		(*exportBindings)[name->text()] = specifier;
	}
	emitContext()->addVariableDeclaration(name);
}

// createEnvBinding — using.go:611
Node* usingDeclarationTransformer::createEnvBinding() {
	return factory()->newUniqueName("env");
}

// createDownlevelUsingStatements — using.go:615
std::vector<Node*>
usingDeclarationTransformer::createDownlevelUsingStatements(
	const std::vector<Node*>& bodyStatements, Node* envBinding, bool async_) {
	std::vector<Node*> statements;
	statements.reserve(2);

	// produces:
	//
	//  const env_1 = { stack: [], error: void 0, hasError: false };
	//
	Node* envObject = factory()->newObjectLiteralExpression(
		factory()->newNodeList(std::vector<Node*>{
			factory()->newPropertyAssignment(
				nullptr /*modifiers*/, factory()->newIdentifier("stack"),
				nullptr /*postfixToken*/, nullptr /*typeNode*/,
				factory()->newArrayLiteralExpression(
					nullptr, false /*multiLine*/)),
			factory()->newPropertyAssignment(
				nullptr /*modifiers*/, factory()->newIdentifier("error"),
				nullptr /*postfixToken*/, nullptr /*typeNode*/,
				factory()->newVoidZeroExpression()),
			factory()->newPropertyAssignment(
				nullptr /*modifiers*/,
				factory()->newIdentifier("hasError"),
				nullptr /*postfixToken*/, nullptr /*typeNode*/,
				factory()->newFalseExpression()),
		}),
		false /*multiLine*/);
	Node* envVar = factory()->newVariableDeclaration(
		envBinding, nullptr /*exclamationToken*/, nullptr /*typeNode*/,
		envObject);
	Node* envVarList = factory()->newVariableDeclarationList(
		factory()->newNodeList(std::vector<Node*>{envVar}),
		NodeFlagsConst);
	Node* envVarStatement = factory()->newVariableStatement(
		nullptr /*modifiers*/, envVarList);
	statements.push_back(envVarStatement);

	// when `async` is `false`, produces:
	//
	//  try {
	//    <bodyStatements>
	//  }
	//  catch (e_1) {
	//      env_1.error = e_1;
	//      env_1.hasError = true;
	//  }
	//  finally {
	//    __disposeResources(env_1);
	//  }

	// when `async` is `true`, produces:
	//
	//  try {
	//    <bodyStatements>
	//  }
	//  catch (e_1) {
	//      env_1.error = e_1;
	//      env_1.hasError = true;
	//  }
	//  finally {
	//    const result_1 = __disposeResources(env_1);
	//    if (result_1) {
	//      await result_1;
	//    }
	//  }

	// Unfortunately, it is necessary to use two properties to indicate an error because `throw undefined` is legal
	// JavaScript.
	Node* tryBlock = factory()->newBlock(
		factory()->newNodeList(bodyStatements), true /*multiLine*/);
	Node* bodyCatchBinding = factory()->newUniqueName("e");
	Node* catchClause = factory()->newCatchClause(
		factory()->newVariableDeclaration(
			bodyCatchBinding,
			nullptr, /*exclamationToken*/
			nullptr, /*type*/
			nullptr  /*initializer*/
			),
		factory()->newBlock(
			factory()->newNodeList(std::vector<Node*>{
				factory()->newExpressionStatement(
					factory()->newAssignmentExpression(
						factory()->newPropertyAccessExpression(
							envBinding, nullptr,
							factory()->newIdentifier("error"),
							NodeFlagsNone),
						bodyCatchBinding)),
				factory()->newExpressionStatement(
					factory()->newAssignmentExpression(
						factory()->newPropertyAccessExpression(
							envBinding, nullptr,
							factory()->newIdentifier("hasError"),
							NodeFlagsNone),
						factory()->newTrueExpression())),
			}),
			true /*multiLine*/));

	Node* finallyBlock = nullptr;
	if (async_) {
		Node* result = factory()->newUniqueName("result");
		finallyBlock = factory()->newBlock(
			factory()->newNodeList(std::vector<Node*>{
				factory()->newVariableStatement(
					nullptr, /*modifiers*/
					factory()->newVariableDeclarationList(
						factory()->newNodeList(std::vector<Node*>{
							factory()->newVariableDeclaration(
								result,
								nullptr, /*exclamationToken*/
								nullptr, /*type*/
								factory()->
									newDisposeResourcesHelper(
										envBinding)),
						}),
						NodeFlagsConst)),
				factory()->newIfStatement(
					result,
					factory()->newExpressionStatement(
						factory()->newAwaitExpression(result)),
					nullptr /*elseStatement*/),
			}),
			true /*multiLine*/);
	} else {
		finallyBlock = factory()->newBlock(
			factory()->newNodeList(std::vector<Node*>{
				factory()->newExpressionStatement(
					factory()->newDisposeResourcesHelper(envBinding)),
			}),
			true /*multiLine*/);
	}

	Node* tryStatement =
		factory()->newTryStatement(tryBlock, catchClause, finallyBlock);
	statements.push_back(tryStatement);
	return statements;
}

}  // namespace tsc::transformers::estransforms
