// Port of tsc/internal/transformers/estransforms/forawait.go — downlevels
// `for await (... of ...)` loops and async generators for ES2018- targets.
#include "internal/transformers/estransforms/estransforms.h"

#include "internal/collections/collections.h"
#include "internal/printer/printer.h"

namespace tsc::transformers::estransforms {

// Facts we track as we traverse the tree
using forAwaitHierarchyFacts = int;
inline constexpr forAwaitHierarchyFacts forAwaitHierarchyFactsNone = 0;

//
// Ancestor facts
//
inline constexpr forAwaitHierarchyFacts forAwaitHierarchyFactsHasLexicalThis =
	1 << 0;
inline constexpr forAwaitHierarchyFacts forAwaitHierarchyFactsIterationContainer =
	1 << 1;

//
// Ancestor masks
//
inline constexpr forAwaitHierarchyFacts forAwaitHierarchyFactsAncestorFactsMask =
	(1 << 2) - 1;

inline constexpr forAwaitHierarchyFacts forAwaitHierarchyFactsSourceFileExcludes =
	forAwaitHierarchyFactsIterationContainer;
inline constexpr forAwaitHierarchyFacts
	forAwaitHierarchyFactsStrictModeSourceFileIncludes =
		forAwaitHierarchyFactsNone;

inline constexpr forAwaitHierarchyFacts
	forAwaitHierarchyFactsClassOrFunctionIncludes =
		forAwaitHierarchyFactsHasLexicalThis;
inline constexpr forAwaitHierarchyFacts
	forAwaitHierarchyFactsClassOrFunctionExcludes =
		forAwaitHierarchyFactsIterationContainer;

inline constexpr forAwaitHierarchyFacts forAwaitHierarchyFactsArrowFunctionIncludes =
	forAwaitHierarchyFactsNone;
inline constexpr forAwaitHierarchyFacts forAwaitHierarchyFactsArrowFunctionExcludes =
	forAwaitHierarchyFactsClassOrFunctionExcludes;

inline constexpr forAwaitHierarchyFacts
	forAwaitHierarchyFactsIterationStatementIncludes =
		forAwaitHierarchyFactsIterationContainer;
inline constexpr forAwaitHierarchyFacts
	forAwaitHierarchyFactsIterationStatementExcludes =
		forAwaitHierarchyFactsNone;

namespace {

// unwrapInnermostStatementOfLabel follows LabeledStatement chains to find the
// innermost statement. — forawait.go:315
Node* unwrapInnermostStatementOfLabel(LabeledStatement* node) {
	for (;;) {
		if (node->Statement->kind != Kind::LabeledStatement) {
			return node->Statement;
		}
		node = node->Statement->as<LabeledStatement>();
	}
}

}  // anonymous namespace

// forawaitTransformer — forawait.go:44
struct forawaitTransformer : Transformer, superAccessState {
	const CompilerOptions* compilerOptions;

	FunctionFlags enclosingFunctionFlags = FunctionFlagsNormal;
	estransforms::forAwaitHierarchyFacts forAwaitHierarchyFacts = 0;
	bool exportedVariableStatement = false;

	NodeVisitor* fallbackNodeVisitor = nullptr;
	NodeVisitor* noAsyncModifierVisitor = nullptr;

	bool affectsSubtree(estransforms::forAwaitHierarchyFacts excludeFacts,
	                    estransforms::forAwaitHierarchyFacts includeFacts);
	estransforms::forAwaitHierarchyFacts enterSubtree(
		estransforms::forAwaitHierarchyFacts excludeFacts,
		estransforms::forAwaitHierarchyFacts includeFacts);
	void exitSubtree(estransforms::forAwaitHierarchyFacts ancestorFacts);
	ModifierList* visitModifiersNoAsync(ModifierList* modifiers);
	Node* doWithHierarchyFacts(Node* (forawaitTransformer::*cb)(Node*),
	                           Node* node,
	                           estransforms::forAwaitHierarchyFacts excludeFacts,
	                           estransforms::forAwaitHierarchyFacts includeFacts);
	Node* visitDefault(Node* node);
	Node* fallbackVisitor(Node* node);
	Node* visitFallback(Node* node);
	Node* visit(Node* node);
	Node* visitAwaitExpression(AwaitExpression* node);
	Node* visitYieldExpression(YieldExpression* node);
	Node* visitReturnStatement(ReturnStatement* node);
	Node* visitLabeledStatement(LabeledStatement* node);
	Node* visitSourceFile(SourceFile* node);
	Node* visitForOfStatement(ForInOrOfStatement* node,
	                          LabeledStatement* outermostLabeledStatement);
	Node* convertForOfStatementHead(ForInOrOfStatement* node, Node* boundValue,
	                                Node* nonUserCode);
	Node* createDownlevelAwait(Node* expression);
	Node* transformForAwaitOfStatement(
		ForInOrOfStatement* node, LabeledStatement* outermostLabeledStatement,
		estransforms::forAwaitHierarchyFacts ancestorFacts);
	Node* visitConstructorDeclaration(Node* node);
	Node* visitGetAccessorDeclaration(Node* node);
	Node* visitSetAccessorDeclaration(Node* node);
	Node* visitMethodDeclaration(Node* node);
	Node* visitFunctionDeclaration(Node* node);
	Node* visitArrowFunction(Node* node);
	Node* visitFunctionExpression(Node* node);
	NodeList* transformAsyncGeneratorFunctionParameterList(Node* node);
	Node* transformAsyncGeneratorFunctionBody(Node* node);
};

// newforawaitTransformer — forawait.go:58
Transformer* newforawaitTransformer(TransformOptions* opts) {
	auto* tx = new forawaitTransformer();
	tx->compilerOptions = opts->CompilerOptions;
	Transformer* result = tx->newTransformer(
		[tx](Node* node) { return tx->visit(node); }, opts->Context);
	tx->initSuperAccessVisitor(tx->emitContext(),
	                           tx->Transformer::factory());
	tx->fallbackNodeVisitor = tx->emitContext()->newNodeVisitor(
		[tx](Node* node) { return tx->visitFallback(node); });
	tx->noAsyncModifierVisitor = tx->emitContext()->newNodeVisitor(
		[](Node* node) -> Node* {
			if (node->kind == Kind::AsyncKeyword) {
				return nullptr;
			}
			return node;
		});
	return result;
}

// affectsSubtree — forawait.go:76
bool forawaitTransformer::affectsSubtree(
	estransforms::forAwaitHierarchyFacts excludeFacts,
	estransforms::forAwaitHierarchyFacts includeFacts) {
	return forAwaitHierarchyFacts !=
	       ((forAwaitHierarchyFacts & ~excludeFacts) | includeFacts);
}

// enterSubtree sets the HierarchyFacts for this node prior to visiting this
// node's subtree, returning the facts set prior to modification. — forawait.go:82
estransforms::forAwaitHierarchyFacts forawaitTransformer::enterSubtree(
	estransforms::forAwaitHierarchyFacts excludeFacts,
	estransforms::forAwaitHierarchyFacts includeFacts) {
	estransforms::forAwaitHierarchyFacts ancestorFacts = forAwaitHierarchyFacts;
	forAwaitHierarchyFacts =
		((forAwaitHierarchyFacts & ~excludeFacts) | includeFacts) &
		forAwaitHierarchyFactsAncestorFactsMask;
	return ancestorFacts;
}

// exitSubtree restores the HierarchyFacts for this node's ancestor after
// visiting this node's subtree. — forawait.go:91
void forawaitTransformer::exitSubtree(
	estransforms::forAwaitHierarchyFacts ancestorFacts) {
	forAwaitHierarchyFacts = ancestorFacts;
}

// visitModifiersNoAsync — forawait.go:95
ModifierList* forawaitTransformer::visitModifiersNoAsync(
	ModifierList* modifiers) {
	return noAsyncModifierVisitor->visitModifiers(modifiers);
}

// doWithHierarchyFacts — forawait.go:99
Node* forawaitTransformer::doWithHierarchyFacts(
	Node* (forawaitTransformer::*cb)(Node*), Node* node,
	estransforms::forAwaitHierarchyFacts excludeFacts,
	estransforms::forAwaitHierarchyFacts includeFacts) {
	if (affectsSubtree(excludeFacts, includeFacts)) {
		estransforms::forAwaitHierarchyFacts ancestorFacts =
			enterSubtree(excludeFacts, includeFacts);
		Node* result = (this->*cb)(node);
		exitSubtree(ancestorFacts);
		return result;
	}
	return (this->*cb)(node);
}

// visitDefault — forawait.go:109
Node* forawaitTransformer::visitDefault(Node* node) {
	return visitor()->visitEachChild(node);
}

// fallbackVisitor — forawait.go:113
Node* forawaitTransformer::fallbackVisitor(Node* node) {
	if (capturedSuperProperties == nullptr) {
		return node;
	}
	switch (node->kind) {
	case Kind::FunctionExpression:
	case Kind::FunctionDeclaration:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::Constructor:
		return node;
	default:
		break;
	}
	trackSuperAccess(node);
	return fallbackNodeVisitor->visitEachChild(node);
}

// visitFallback — forawait.go:126
Node* forawaitTransformer::visitFallback(Node* node) {
	return fallbackVisitor(node);
}

// visit — forawait.go:130
Node* forawaitTransformer::visit(Node* node) {
	if ((node->subtreeFacts() & SubtreeContainsForAwaitOrAsyncGenerator) == 0) {
		return fallbackVisitor(node);
	}
	trackSuperAccess(node);
	switch (node->kind) {
	case Kind::SourceFile:
		return visitSourceFile(node->as<SourceFile>());
	case Kind::AwaitExpression:
		return visitAwaitExpression(node->as<AwaitExpression>());
	case Kind::YieldExpression:
		return visitYieldExpression(node->as<YieldExpression>());
	case Kind::ReturnStatement:
		return visitReturnStatement(node->as<ReturnStatement>());
	case Kind::LabeledStatement:
		return visitLabeledStatement(node->as<LabeledStatement>());
	case Kind::DoStatement:
	case Kind::WhileStatement:
	case Kind::ForInStatement:
		return doWithHierarchyFacts(
			&forawaitTransformer::visitDefault, node,
			forAwaitHierarchyFactsIterationStatementExcludes,
			forAwaitHierarchyFactsIterationStatementIncludes);
	case Kind::ForOfStatement:
		return visitForOfStatement(node->as<ForInOrOfStatement>(), nullptr);
	case Kind::ForStatement:
		return doWithHierarchyFacts(
			&forawaitTransformer::visitDefault, node,
			forAwaitHierarchyFactsIterationStatementExcludes,
			forAwaitHierarchyFactsIterationStatementIncludes);
	case Kind::Constructor:
		return doWithHierarchyFacts(
			&forawaitTransformer::visitConstructorDeclaration, node,
			forAwaitHierarchyFactsClassOrFunctionExcludes,
			forAwaitHierarchyFactsClassOrFunctionIncludes);
	case Kind::MethodDeclaration:
		return doWithHierarchyFacts(
			&forawaitTransformer::visitMethodDeclaration, node,
			forAwaitHierarchyFactsClassOrFunctionExcludes,
			forAwaitHierarchyFactsClassOrFunctionIncludes);
	case Kind::GetAccessor:
		return doWithHierarchyFacts(
			&forawaitTransformer::visitGetAccessorDeclaration, node,
			forAwaitHierarchyFactsClassOrFunctionExcludes,
			forAwaitHierarchyFactsClassOrFunctionIncludes);
	case Kind::SetAccessor:
		return doWithHierarchyFacts(
			&forawaitTransformer::visitSetAccessorDeclaration, node,
			forAwaitHierarchyFactsClassOrFunctionExcludes,
			forAwaitHierarchyFactsClassOrFunctionIncludes);
	case Kind::FunctionDeclaration:
		return doWithHierarchyFacts(
			&forawaitTransformer::visitFunctionDeclaration, node,
			forAwaitHierarchyFactsClassOrFunctionExcludes,
			forAwaitHierarchyFactsClassOrFunctionIncludes);
	case Kind::FunctionExpression:
		return doWithHierarchyFacts(
			&forawaitTransformer::visitFunctionExpression, node,
			forAwaitHierarchyFactsClassOrFunctionExcludes,
			forAwaitHierarchyFactsClassOrFunctionIncludes);
	case Kind::ArrowFunction:
		return doWithHierarchyFacts(
			&forawaitTransformer::visitArrowFunction, node,
			forAwaitHierarchyFactsArrowFunctionExcludes,
			forAwaitHierarchyFactsArrowFunctionIncludes);
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
		return doWithHierarchyFacts(
			&forawaitTransformer::visitDefault, node,
			forAwaitHierarchyFactsClassOrFunctionExcludes,
			forAwaitHierarchyFactsClassOrFunctionIncludes);
	default:
		return visitor()->visitEachChild(node);
	}
}

// visitAwaitExpression — forawait.go:219
Node* forawaitTransformer::visitAwaitExpression(AwaitExpression* node) {
	if ((enclosingFunctionFlags & FunctionFlagsAsync) != 0 &&
	    (enclosingFunctionFlags & FunctionFlagsGenerator) != 0) {
		Node* result = Transformer::factory()->newYieldExpression(
			nullptr, /*asteriskToken*/
			Transformer::factory()->newAwaitHelper(
				visitor()->visitNode(node->Expression)));
		result->loc = node->loc;
		emitContext()->setOriginal(result, node->asNode());
		return result;
	}
	return visitor()->visitEachChild(node->asNode());
}

// visitYieldExpression — forawait.go:232
Node* forawaitTransformer::visitYieldExpression(YieldExpression* node) {
	if ((enclosingFunctionFlags & FunctionFlagsAsync) != 0 &&
	    (enclosingFunctionFlags & FunctionFlagsGenerator) != 0) {
		if (node->AsteriskToken != nullptr) {
			Node* expression = visitor()->visitNode(node->Expression);

			Node* asyncValuesResult =
				Transformer::factory()->newAsyncValuesHelper(expression);
			asyncValuesResult->loc = expression->loc;

			Node* asyncDelegatorResult =
				Transformer::factory()->newAsyncDelegatorHelper(
					asyncValuesResult);
			asyncDelegatorResult->loc = expression->loc;

			Node* innerYield =
				Transformer::factory()->updateYieldExpression(
					node, node->AsteriskToken, asyncDelegatorResult);

			Node* awaitedYield =
				Transformer::factory()->newAwaitHelper(innerYield);

			Node* result = Transformer::factory()->newYieldExpression(
				nullptr, /*asteriskToken*/
				awaitedYield);
			result->loc = node->loc;
			emitContext()->setOriginal(result, node->asNode());
			return result;
		}

		Node* innerExpression = nullptr;
		if (node->Expression != nullptr) {
			innerExpression = visitor()->visitNode(node->Expression);
		} else {
			innerExpression =
				Transformer::factory()->newVoidZeroExpression();
		}

		Node* result = Transformer::factory()->newYieldExpression(
			nullptr, /*asteriskToken*/
			createDownlevelAwait(innerExpression));
		result->loc = node->loc;
		emitContext()->setOriginal(result, node->asNode());
		return result;
	}

	return visitor()->visitEachChild(node->asNode());
}

// visitReturnStatement — forawait.go:276
Node* forawaitTransformer::visitReturnStatement(ReturnStatement* node) {
	if ((enclosingFunctionFlags & FunctionFlagsAsync) != 0 &&
	    (enclosingFunctionFlags & FunctionFlagsGenerator) != 0) {
		Node* expression = nullptr;
		if (node->Expression != nullptr) {
			expression = visitor()->visitNode(node->Expression);
		} else {
			expression = Transformer::factory()->newVoidZeroExpression();
		}
		return Transformer::factory()->updateReturnStatement(
			node, createDownlevelAwait(expression));
	}

	return visitor()->visitEachChild(node->asNode());
}

// visitLabeledStatement — forawait.go:292
Node* forawaitTransformer::visitLabeledStatement(LabeledStatement* node) {
	if ((enclosingFunctionFlags & FunctionFlagsAsync) != 0) {
		Node* statement = unwrapInnermostStatementOfLabel(node);
		if (statement->kind == Kind::ForOfStatement &&
		    statement->as<ForInOrOfStatement>()->AwaitModifier != nullptr) {
			return visitForOfStatement(statement->as<ForInOrOfStatement>(),
			                           node);
		}
		return Transformer::factory()->restoreEnclosingLabel(
			visitor()->visitNode(statement), node);
	}
	return visitor()->visitEachChild(node->asNode());
}

// visitSourceFile — forawait.go:326
Node* forawaitTransformer::visitSourceFile(SourceFile* node) {
	estransforms::forAwaitHierarchyFacts ancestorFacts = enterSubtree(
		forAwaitHierarchyFactsSourceFileExcludes,
		forAwaitHierarchyFactsStrictModeSourceFileIncludes);
	exportedVariableStatement = false;
	Node* visited = visitor()->visitEachChild(node->asNode());
	for (printer::EmitHelper* helper : emitContext()->readEmitHelpers()) {
		emitContext()->addEmitHelper(visited, helper);
	}
	exitSubtree(ancestorFacts);
	return visited;
}

// visitForOfStatement visits a ForOfStatement and converts it into a
// ES2015-compatible ForOfStatement. — forawait.go:341
Node* forawaitTransformer::visitForOfStatement(
	ForInOrOfStatement* node,
	LabeledStatement* outermostLabeledStatement) {
	estransforms::forAwaitHierarchyFacts ancestorFacts = enterSubtree(
		forAwaitHierarchyFactsIterationStatementExcludes,
		forAwaitHierarchyFactsIterationStatementIncludes);
	Node* result = nullptr;
	if (node->AwaitModifier != nullptr) {
		result = transformForAwaitOfStatement(
			node, outermostLabeledStatement, ancestorFacts);
	} else {
		result = Transformer::factory()->restoreEnclosingLabel(
			visitor()->visitEachChild(node->asNode()),
			outermostLabeledStatement);
	}
	exitSubtree(ancestorFacts);
	return result;
}

// convertForOfStatementHead — forawait.go:353
Node* forawaitTransformer::convertForOfStatementHead(
	ForInOrOfStatement* node, Node* boundValue, Node* nonUserCode) {
	printer::NodeFactory* f = Transformer::factory();
	Node* value = f->newTempVariable();
	emitContext()->addVariableDeclaration(value);
	Node* iteratorValueExpression =
		f->newAssignmentExpression(value, boundValue);
	Node* iteratorValueStatement =
		f->newExpressionStatement(iteratorValueExpression);
	emitContext()->setSourceMapRange(iteratorValueStatement,
	                               node->Expression->loc);

	Node* exitNonUserCodeExpression = f->newAssignmentExpression(
		nonUserCode, f->newKeywordExpression(Kind::FalseKeyword));
	Node* exitNonUserCodeStatement =
		f->newExpressionStatement(exitNonUserCodeExpression);
	emitContext()->setSourceMapRange(exitNonUserCodeStatement,
	                               node->Expression->loc);

	std::vector<Node*> statements{iteratorValueStatement,
	                              exitNonUserCodeStatement};
	Node* binding = f->createForOfBindingStatement(node->Initializer, value);
	statements.push_back(visitor()->visitNode(binding));

	TextRange bodyLocation{0, 0};
	TextRange statementsLocation{0, 0};
	Node* statement = visitor()->visitEmbeddedStatement(node->Statement);
	if (isBlock(statement)) {
		for (Node* s : statement->statements()) {
			statements.push_back(s);
		}
		bodyLocation = statement->loc;
		statementsLocation = statement->statementList()->loc;
	} else {
		statements.push_back(statement);
	}

	NodeList* stmtList = f->newNodeList(statements);
	stmtList->loc = statementsLocation;
	Node* block = f->newBlock(stmtList, true);
	block->loc = bodyLocation;
	return block;
}

// createDownlevelAwait — forawait.go:390
Node* forawaitTransformer::createDownlevelAwait(Node* expression) {
	if ((enclosingFunctionFlags & FunctionFlagsGenerator) != 0) {
		return Transformer::factory()->newYieldExpression(
			nullptr, /*asteriskToken*/
			Transformer::factory()->newAwaitHelper(expression));
	}
	return Transformer::factory()->newAwaitExpression(expression);
}

// transformForAwaitOfStatement — forawait.go:402
Node* forawaitTransformer::transformForAwaitOfStatement(
	ForInOrOfStatement* node, LabeledStatement* outermostLabeledStatement,
	estransforms::forAwaitHierarchyFacts ancestorFacts) {
	printer::NodeFactory* f = Transformer::factory();
	Node* expression = visitor()->visitNode(node->Expression);

	Node* iterator = nullptr;
	if (isIdentifier(expression)) {
		iterator = f->newGeneratedNameForNode(expression);
	} else {
		iterator = f->newTempVariable();
	}

	Node* result = nullptr;
	if (isIdentifier(expression)) {
		result = f->newGeneratedNameForNode(iterator);
	} else {
		result = f->newTempVariable();
	}

	Node* nonUserCode = f->newTempVariable();
	Node* done = f->newTempVariable();
	emitContext()->addVariableDeclaration(done);
	Node* errorRecord = f->newUniqueName("e");
	Node* catchVariable = f->newGeneratedNameForNode(errorRecord);
	Node* returnMethod = f->newTempVariable();
	Node* callValues = f->newAsyncValuesHelper(expression);
	callValues->loc = node->Expression->loc;
	Node* callNext = f->newCallExpression(
		f->newPropertyAccessExpression(
			iterator, nullptr, f->newIdentifier("next"), NodeFlagsNone),
		nullptr, nullptr, f->newNodeList({}), NodeFlagsNone);
	Node* getDone = f->newPropertyAccessExpression(
		result, nullptr, f->newIdentifier("done"), NodeFlagsNone);
	Node* getValue = f->newPropertyAccessExpression(
		result, nullptr, f->newIdentifier("value"), NodeFlagsNone);
	Node* callReturn =
		f->newFunctionCallCall(returnMethod, iterator, {});

	emitContext()->addVariableDeclaration(errorRecord);
	emitContext()->addVariableDeclaration(returnMethod);

	// if we are enclosed in an outer loop ensure we reset 'errorRecord' per each iteration
	Node* initializer = nullptr;
	if ((ancestorFacts & forAwaitHierarchyFactsIterationContainer) != 0) {
		initializer = f->inlineExpressions(std::vector<Node*>{
			f->newAssignmentExpression(
				errorRecord, f->newVoidZeroExpression()),
			callValues,
		});
	} else {
		initializer = callValues;
	}

	// Build the for statement
	Node* iteratorDecl = f->newVariableDeclaration(
		iterator, nullptr, nullptr, initializer);
	iteratorDecl->loc = node->Expression->loc;
	Node* varDeclList = f->newVariableDeclarationList(
		f->newNodeList({
			f->newVariableDeclaration(
				nonUserCode, nullptr, nullptr,
				f->newKeywordExpression(Kind::TrueKeyword)),
			iteratorDecl,
			f->newVariableDeclaration(result, nullptr, nullptr, nullptr),
		}),
		NodeFlagsNone);
	varDeclList->loc = node->Expression->loc;

	Node* condition = f->inlineExpressions(std::vector<Node*>{
		f->newAssignmentExpression(result, createDownlevelAwait(callNext)),
		f->newAssignmentExpression(done, getDone),
		f->newPrefixUnaryExpression(Kind::ExclamationToken, done),
	});

	Node* incrementor = f->newAssignmentExpression(
		nonUserCode, f->newKeywordExpression(Kind::TrueKeyword));

	Node* forStatement = f->newForStatement(
		varDeclList, condition, incrementor,
		convertForOfStatementHead(node, getValue, nonUserCode));
	forStatement->loc = node->loc;
	emitContext()->addEmitFlags(forStatement,
	                            printer::EFNoTokenTrailingSourceMaps);
	emitContext()->setOriginal(forStatement, node->asNode());

	// Build the try/catch/finally
	Node* tryBlock = f->newBlock(f->newNodeList({
		f->restoreEnclosingLabel(forStatement, outermostLabeledStatement),
	}), true);

	// catch clause: { e_1 = { error: e_2 }; }
	Node* catchBody = f->newBlock(f->newNodeList({
		f->newExpressionStatement(f->newAssignmentExpression(
			errorRecord,
			f->newObjectLiteralExpression(
				f->newNodeList({
					f->newPropertyAssignment(
						nullptr, f->newIdentifier("error"), nullptr,
						nullptr, catchVariable),
				}),
				false))),
	}), false);
	emitContext()->addEmitFlags(catchBody, printer::EFSingleLine);
	Node* catchClause = f->newCatchClause(
		f->newVariableDeclaration(catchVariable, nullptr, nullptr, nullptr),
		catchBody);

	// finally block
	// inner try: if (!nonUserCode && !done && (returnMethod = iterator.return)) await returnMethod.call(iterator);
	Node* innerIfCondition = f->newBinaryExpression(
		nullptr,
		f->newBinaryExpression(
			nullptr,
			f->newPrefixUnaryExpression(Kind::ExclamationToken, nonUserCode),
			nullptr, f->newToken(Kind::AmpersandAmpersandToken),
			f->newPrefixUnaryExpression(Kind::ExclamationToken, done)),
		nullptr, f->newToken(Kind::AmpersandAmpersandToken),
		f->newAssignmentExpression(
			returnMethod,
			f->newPropertyAccessExpression(
				iterator, nullptr, f->newIdentifier("return"),
				NodeFlagsNone)));
	Node* innerIfStatement = f->newIfStatement(
		innerIfCondition,
		f->newExpressionStatement(createDownlevelAwait(callReturn)),
		nullptr);
	emitContext()->addEmitFlags(innerIfStatement, printer::EFSingleLine);

	Node* innerTryBlock =
		f->newBlock(f->newNodeList({innerIfStatement}), false);

	// inner finally: if (errorRecord) throw errorRecord.error;
	Node* innerFinallyIf = f->newIfStatement(
		errorRecord,
		f->newThrowStatement(f->newPropertyAccessExpression(
			errorRecord, nullptr, f->newIdentifier("error"),
			NodeFlagsNone)),
		nullptr);
	emitContext()->addEmitFlags(innerFinallyIf, printer::EFSingleLine);
	Node* innerFinallyBlock =
		f->newBlock(f->newNodeList({innerFinallyIf}), false);
	emitContext()->addEmitFlags(innerFinallyBlock, printer::EFSingleLine);

	Node* innerTryStatement =
		f->newTryStatement(innerTryBlock, nullptr, innerFinallyBlock);
	Node* finallyBlock =
		f->newBlock(f->newNodeList({innerTryStatement}), true);

	return f->newTryStatement(tryBlock, catchClause, finallyBlock);
}

// visitConstructorDeclaration — forawait.go:544
Node* forawaitTransformer::visitConstructorDeclaration(Node* node) {
	ConstructorDeclaration* decl = node->as<ConstructorDeclaration>();
	FunctionFlags savedEnclosingFunctionFlags = enclosingFunctionFlags;
	enclosingFunctionFlags = getFunctionFlags(node);
	NodeList* visitedParameters =
		emitContext()->visitParameters(decl->Parameters, visitor());
	Node* visitedBody = emitContext()->visitFunctionBody(node->body(), visitor());
	Node* updated = Transformer::factory()->updateConstructorDeclaration(
		decl, decl->modifiers,
		nullptr, /*typeParameters*/
		visitedParameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		visitedBody);
	enclosingFunctionFlags = savedEnclosingFunctionFlags;
	return updated;
}

// visitGetAccessorDeclaration — forawait.go:560
Node* forawaitTransformer::visitGetAccessorDeclaration(Node* node) {
	GetAccessorDeclaration* decl = node->as<GetAccessorDeclaration>();
	FunctionFlags savedEnclosingFunctionFlags = enclosingFunctionFlags;
	enclosingFunctionFlags = getFunctionFlags(node);
	Node* visitedName = visitor()->visitNode(decl->name);
	NodeList* visitedParameters =
		emitContext()->visitParameters(decl->Parameters, visitor());
	Node* visitedBody = emitContext()->visitFunctionBody(node->body(), visitor());
	Node* updated = Transformer::factory()->updateGetAccessorDeclaration(
		decl, decl->modifiers, visitedName,
		nullptr, /*typeParameters*/
		visitedParameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		visitedBody);
	enclosingFunctionFlags = savedEnclosingFunctionFlags;
	return updated;
}

// visitSetAccessorDeclaration — forawait.go:577
Node* forawaitTransformer::visitSetAccessorDeclaration(Node* node) {
	SetAccessorDeclaration* decl = node->as<SetAccessorDeclaration>();
	FunctionFlags savedEnclosingFunctionFlags = enclosingFunctionFlags;
	enclosingFunctionFlags = getFunctionFlags(node);
	Node* visitedName = visitor()->visitNode(decl->name);
	NodeList* visitedParameters =
		emitContext()->visitParameters(decl->Parameters, visitor());
	Node* visitedBody = emitContext()->visitFunctionBody(node->body(), visitor());
	Node* updated = Transformer::factory()->updateSetAccessorDeclaration(
		decl, decl->modifiers, visitedName,
		nullptr, /*typeParameters*/
		visitedParameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		visitedBody);
	enclosingFunctionFlags = savedEnclosingFunctionFlags;
	return updated;
}

// visitMethodDeclaration — forawait.go:594
Node* forawaitTransformer::visitMethodDeclaration(Node* node) {
	MethodDeclaration* decl = node->as<MethodDeclaration>();
	FunctionFlags savedEnclosingFunctionFlags = enclosingFunctionFlags;
	enclosingFunctionFlags = getFunctionFlags(node);

	ModifierList* modifiers = nullptr;
	if ((enclosingFunctionFlags & FunctionFlagsGenerator) != 0) {
		modifiers = visitModifiersNoAsync(decl->modifiers);
	} else {
		modifiers = decl->modifiers;
	}

	Node* asteriskToken = nullptr;
	if ((enclosingFunctionFlags & FunctionFlagsAsync) != 0) {
		asteriskToken = nullptr;
	} else {
		asteriskToken = decl->AsteriskToken;
	}

	NodeList* parameters = nullptr;
	Node* body = nullptr;
	if ((enclosingFunctionFlags & FunctionFlagsAsync) != 0 &&
	    (enclosingFunctionFlags & FunctionFlagsGenerator) != 0) {
		parameters = transformAsyncGeneratorFunctionParameterList(node);
		body = transformAsyncGeneratorFunctionBody(node);
	} else {
		parameters = emitContext()->visitParameters(decl->Parameters, visitor());
		body = emitContext()->visitFunctionBody(node->body(), visitor());
	}

	Node* visitedName = visitor()->visitNode(decl->name);
	Node* updated = Transformer::factory()->updateMethodDeclaration(
		decl, modifiers, asteriskToken, visitedName,
		nullptr, /*postfixToken*/
		nullptr, /*typeParameters*/
		parameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		body);
	enclosingFunctionFlags = savedEnclosingFunctionFlags;
	return updated;
}

// visitFunctionDeclaration — forawait.go:636
Node* forawaitTransformer::visitFunctionDeclaration(Node* node) {
	FunctionDeclaration* decl = node->as<FunctionDeclaration>();
	FunctionFlags savedEnclosingFunctionFlags = enclosingFunctionFlags;
	enclosingFunctionFlags = getFunctionFlags(node);

	ModifierList* modifiers = nullptr;
	if ((enclosingFunctionFlags & FunctionFlagsGenerator) != 0) {
		modifiers = visitModifiersNoAsync(decl->modifiers);
	} else {
		modifiers = decl->modifiers;
	}

	Node* asteriskToken = nullptr;
	if ((enclosingFunctionFlags & FunctionFlagsAsync) != 0) {
		asteriskToken = nullptr;
	} else {
		asteriskToken = decl->AsteriskToken;
	}

	NodeList* parameters = nullptr;
	Node* body = nullptr;
	if ((enclosingFunctionFlags & FunctionFlagsAsync) != 0 &&
	    (enclosingFunctionFlags & FunctionFlagsGenerator) != 0) {
		parameters = transformAsyncGeneratorFunctionParameterList(node);
		body = transformAsyncGeneratorFunctionBody(node);
	} else {
		parameters = emitContext()->visitParameters(decl->Parameters, visitor());
		body = emitContext()->visitFunctionBody(node->body(), visitor());
	}

	Node* updated = Transformer::factory()->updateFunctionDeclaration(
		decl, modifiers, asteriskToken, decl->name,
		nullptr, /*typeParameters*/
		parameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		body);
	enclosingFunctionFlags = savedEnclosingFunctionFlags;
	return updated;
}

// visitArrowFunction — forawait.go:678
Node* forawaitTransformer::visitArrowFunction(Node* node) {
	ArrowFunction* decl = node->as<ArrowFunction>();
	FunctionFlags savedEnclosingFunctionFlags = enclosingFunctionFlags;
	enclosingFunctionFlags = getFunctionFlags(node);
	NodeList* visitedParameters =
		emitContext()->visitParameters(decl->Parameters, visitor());
	Node* visitedBody = emitContext()->visitFunctionBody(node->body(), visitor());
	Node* updated = Transformer::factory()->updateArrowFunction(
		decl, decl->modifiers,
		nullptr, /*typeParameters*/
		visitedParameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		decl->EqualsGreaterThanToken, visitedBody);
	enclosingFunctionFlags = savedEnclosingFunctionFlags;
	return updated;
}

// visitFunctionExpression — forawait.go:695
Node* forawaitTransformer::visitFunctionExpression(Node* node) {
	FunctionExpression* decl = node->as<FunctionExpression>();
	FunctionFlags savedEnclosingFunctionFlags = enclosingFunctionFlags;
	enclosingFunctionFlags = getFunctionFlags(node);

	ModifierList* modifiers = nullptr;
	if ((enclosingFunctionFlags & FunctionFlagsGenerator) != 0) {
		modifiers = visitModifiersNoAsync(decl->modifiers);
	} else {
		modifiers = decl->modifiers;
	}

	Node* asteriskToken = nullptr;
	if ((enclosingFunctionFlags & FunctionFlagsAsync) != 0) {
		asteriskToken = nullptr;
	} else {
		asteriskToken = decl->AsteriskToken;
	}

	NodeList* parameters = nullptr;
	Node* body = nullptr;
	if ((enclosingFunctionFlags & FunctionFlagsAsync) != 0 &&
	    (enclosingFunctionFlags & FunctionFlagsGenerator) != 0) {
		parameters = transformAsyncGeneratorFunctionParameterList(node);
		body = transformAsyncGeneratorFunctionBody(node);
	} else {
		parameters = emitContext()->visitParameters(decl->Parameters, visitor());
		body = emitContext()->visitFunctionBody(node->body(), visitor());
	}

	Node* updated = Transformer::factory()->updateFunctionExpression(
		decl, modifiers, asteriskToken, decl->name,
		nullptr, /*typeParameters*/
		parameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		body);
	enclosingFunctionFlags = savedEnclosingFunctionFlags;
	return updated;
}

// transformAsyncGeneratorFunctionParameterList — forawait.go:737
NodeList* forawaitTransformer::transformAsyncGeneratorFunctionParameterList(
	Node* node) {
	if (isSimpleParameterList(node->parameters())) {
		return emitContext()->visitParameters(node->parameterList(), visitor());
	}
	// Add fixed parameters to preserve the function's `length` property.
	std::vector<Node*> newParameters;
	for (Node* parameter : node->parameters()) {
		ParameterDeclaration* param = parameter->as<ParameterDeclaration>();
		if (param->Initializer != nullptr || param->DotDotDotToken != nullptr) {
			break;
		}
		Node* newParameter = Transformer::factory()->newParameterDeclaration(
			nullptr, nullptr,
			Transformer::factory()->newGeneratedNameForNode(
				param->name,
				printer::AutoGenerateOptions{
					printer::
						GeneratedIdentifierFlagsReservedInNestedScopes,
					"", ""}),
			nullptr, nullptr, nullptr);
		newParameters.push_back(newParameter);
	}
	NodeList* newParametersArray =
		Transformer::factory()->newNodeList(newParameters);
	newParametersArray->loc = node->parameterList()->loc;
	return newParametersArray;
}

// transformAsyncGeneratorFunctionBody — forawait.go:762
Node* forawaitTransformer::transformAsyncGeneratorFunctionBody(Node* node) {
	printer::NodeFactory* f = Transformer::factory();
	NodeList* innerParameters = nullptr;
	if (!isSimpleParameterList(node->parameters())) {
		innerParameters =
			emitContext()->visitParameters(node->parameterList(), visitor());
	}

	printer::OrderedSet<std::string>* savedCapturedSuperProperties =
		capturedSuperProperties;
	bool savedHasSuperElementAccess = hasSuperElementAccess;
	bool savedHasSuperPropertyAssignment = hasSuperPropertyAssignment;
	Node* savedSuperBinding = superBinding;
	Node* savedSuperIndexBinding = superIndexBinding;
	capturedSuperProperties = new printer::OrderedSet<std::string>();
	hasSuperElementAccess = false;
	hasSuperPropertyAssignment = false;
	superBinding = f->newUniqueName(
		"_super",
		printer::AutoGenerateOptions{
			printer::GeneratedIdentifierFlagsOptimistic |
				printer::GeneratedIdentifierFlagsFileLevel,
			"", ""});
	superIndexBinding = f->newUniqueName(
		"_superIndex",
		printer::AutoGenerateOptions{
			printer::GeneratedIdentifierFlagsOptimistic |
				printer::GeneratedIdentifierFlagsFileLevel,
			"", ""});

	Node* asyncBody = f->updateBlock(
		node->body()->as<Block>(),
		visitor()->visitNodes(node->body()->statementList()),
		node->body()->as<Block>()->MultiLine);
	{
		Block* asyncBodyBlock = asyncBody->as<Block>();
		NodeList* mergedStatements =
			emitContext()->endAndMergeVariableEnvironmentList(
				asyncBodyBlock->statementList());
		asyncBody = f->updateBlock(asyncBodyBlock, mergedStatements,
		                           asyncBodyBlock->MultiLine);
	}

	// Substitute super property accesses with _super/_superIndex helpers
	bool emitSuperHelpers =
		capturedSuperProperties->size() > 0 || hasSuperElementAccess;
	if (emitSuperHelpers) {
		asyncBody = substituteSuperAccessesInBody(asyncBody);
	}

	NodeList* innerParams = nullptr;
	if (innerParameters != nullptr) {
		innerParams = innerParameters;
	} else {
		innerParams = f->newNodeList({});
	}

	Node* name = nullptr;
	if (node->name() != nullptr) {
		name = f->newGeneratedNameForNode(node->name());
	}

	Node* generatorFunc = f->newFunctionExpression(
		nullptr, /*modifiers*/
		f->newToken(Kind::AsteriskToken), name,
		nullptr, /*typeParameters*/
		innerParams,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		asyncBody);

	Node* returnStatement = f->newReturnStatement(
		f->newAsyncGeneratorHelper(
			generatorFunc,
			(forAwaitHierarchyFacts &
			 forAwaitHierarchyFactsHasLexicalThis) != 0));

	emitContext()->startVariableEnvironment();
	if (emitSuperHelpers) {
		if (capturedSuperProperties->size() > 0) {
			emitContext()->addInitializationStatement(
				createSuperAccessVariableStatement());
		}
	}

	std::vector<Node*> outerStatements{returnStatement};

	NodeList* mergedOuter =
		emitContext()->endAndMergeVariableEnvironmentList(
			f->newNodeList(outerStatements));
	Node* block = f->updateBlock(node->body()->as<Block>(), mergedOuter,
	                           node->body()->as<Block>()->MultiLine);

	if (emitSuperHelpers && hasSuperElementAccess) {
		if (hasSuperPropertyAssignment) {
			emitContext()->addEmitHelper(block,
			                             printer::AdvancedAsyncSuperHelper);
		} else {
			emitContext()->addEmitHelper(block, printer::AsyncSuperHelper);
		}
	}

	capturedSuperProperties = savedCapturedSuperProperties;
	hasSuperElementAccess = savedHasSuperElementAccess;
	hasSuperPropertyAssignment = savedHasSuperPropertyAssignment;
	superBinding = savedSuperBinding;
	superIndexBinding = savedSuperIndexBinding;

	return block;
}

}  // namespace tsc::transformers::estransforms
