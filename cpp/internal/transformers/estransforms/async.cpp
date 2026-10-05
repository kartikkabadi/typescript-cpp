// Port of tsc/internal/transformers/estransforms/async.go — the downlevel
// async/await transformer (ES2017-): rewrites await expressions inside async
// functions into yields passed through the __awaiter helper, captures lexical
// `this`/`arguments`, and hoists `var` declarations that collide with
// parameter names.
#include "internal/transformers/estransforms/estransforms.h"

#include "internal/collections/collections.h"
#include "internal/printer/printer.h"

namespace tsc::transformers::estransforms {

// asyncContextFlags — async.go:12
using asyncContextFlags = uint32_t;
inline constexpr asyncContextFlags asyncContextNonTopLevel = 1 << 0;
inline constexpr asyncContextFlags asyncContextHasLexicalThis = 1 << 1;

// lexicalArgumentsInfo — async.go:19
struct lexicalArgumentsInfo {
	Node* binding = nullptr;
	bool used = false;
};

namespace {

// FnGuard — RAII stand-in for Go `defer`.
struct FnGuard {
	std::function<void()> f;
	explicit FnGuard(std::function<void()> fn) : f(std::move(fn)) {}
	~FnGuard() { f(); }
	FnGuard(const FnGuard&) = delete;
	FnGuard& operator=(const FnGuard&) = delete;
};

// isBreakOrContinueStatement — ast/utilities.go:2336 (file-local replica)
bool isBreakOrContinueStatement(Node* node) {
	return isBreakStatement(node) || isContinueStatement(node);
}

// isLabelOfLabeledStatement — ast/utilities.go:2316 (file-local replica)
bool isLabelOfLabeledStatement(Node* node) {
	if (!isIdentifier(node)) {
		return false;
	}
	if (!isLabeledStatement(node->parent)) {
		return false;
	}
	return node == node->parent->label();
}

// isJumpStatementTarget — ast/utilities.go:2326 (file-local replica)
bool isJumpStatementTarget(Node* node) {
	if (!isIdentifier(node)) {
		return false;
	}
	if (!isBreakOrContinueStatement(node->parent)) {
		return false;
	}
	return node == node->parent->label();
}

// isLabelName — ast/utilities.go:2312 (file-local replica)
bool isLabelName(Node* node) {
	return isLabelOfLabeledStatement(node) || isJumpStatementTarget(node);
}

// isNodeWithPossibleHoistedDeclaration — async.go:963
bool isNodeWithPossibleHoistedDeclaration(Node* node) {
	switch (node->kind) {
	case Kind::Block:
	case Kind::VariableStatement:
	case Kind::WithStatement:
	case Kind::IfStatement:
	case Kind::SwitchStatement:
	case Kind::CaseBlock:
	case Kind::CaseClause:
	case Kind::DefaultClause:
	case Kind::LabeledStatement:
	case Kind::ForStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::DoStatement:
	case Kind::WhileStatement:
	case Kind::TryStatement:
	case Kind::CatchClause:
		return true;
	default:
		return false;
	}
}

}  // anonymous namespace

// asyncTransformer — async.go:24
struct asyncTransformer : Transformer, superAccessState {
	asyncContextFlags contextFlags = 0;

	collections::Set<std::string>* enclosingFunctionParameterNames = nullptr;
	lexicalArgumentsInfo lexicalArguments;

	NodeVisitor* asyncBodyVisitor = nullptr;
	NodeVisitor* fallbackNodeVisitor = nullptr;

	Node* visitSourceFile(SourceFile* node);
	void setContextFlag(asyncContextFlags flag, bool val);
	bool inContext(asyncContextFlags flags);
	bool inTopLevelContext();
	bool inHasLexicalThisContext();
	Node* doWithContext(asyncContextFlags flags,
	                    Node* (asyncTransformer::*cb)(Node*), Node* node);
	Node* visitDefault(Node* node);
	Node* fallbackVisitor(Node* node);
	Node* visitFallback(Node* node);
	Node* visit(Node* node);
	Node* visitAsyncBodyNode(Node* node);
	Node* visitCatchClauseInAsyncBody(CatchClause* node);
	Node* visitVariableStatementInAsyncBody(Node* node);
	Node* visitForInStatementInAsyncBody(ForInOrOfStatement* node);
	Node* visitForOfStatementInAsyncBody(ForInOrOfStatement* node);
	Node* visitForStatementInAsyncBody(ForStatement* node);
	Node* visitAwaitExpression(AwaitExpression* node);
	Node* visitConstructorDeclaration(Node* node);
	Node* visitMethodDeclaration(Node* node);
	Node* visitGetAccessorDeclaration(Node* node);
	Node* visitSetAccessorDeclaration(Node* node);
	Node* visitFunctionDeclaration(Node* node);
	Node* visitFunctionExpression(Node* node);
	Node* visitArrowFunction(Node* node);
	void recordDeclarationName(Node* node,
	                           collections::Set<std::string>* names);
	bool isVariableDeclarationListWithCollidingName(Node* node);
	Node* visitVariableDeclarationListWithCollidingNames(
		VariableDeclarationList* node, bool hasReceiver);
	void hoistVariableDeclarationList(VariableDeclarationList* node);
	void hoistVariable(Node* node);
	Node* transformInitializedVariable(VariableDeclaration* node);
	bool collidesWithParameterName(Node* node);
	Node* transformMethodBody(Node* node);
	Node* createCaptureArgumentsStatement();
	NodeList* transformAsyncFunctionParameterList(Node* node);
	Node* transformAsyncFunctionBody(Node* node, NodeList* outerParameters);
	Node* transformAsyncFunctionBodyWorker(Node* body);
	Node* getOriginalIfFunctionLike(Node* node);
};

// newAsyncTransformer — async.go:37
Transformer* newAsyncTransformer(TransformOptions* opts) {
	auto* tx = new asyncTransformer();
	Transformer* result = tx->newTransformer(
		[tx](Node* node) { return tx->visit(node); }, opts->Context);
	tx->initSuperAccessVisitor(tx->emitContext(),
	                           tx->Transformer::factory());
	tx->asyncBodyVisitor = tx->emitContext()->newNodeVisitor(
		[tx](Node* node) { return tx->visitAsyncBodyNode(node); });
	tx->fallbackNodeVisitor = tx->emitContext()->newNodeVisitor(
		[tx](Node* node) { return tx->visitFallback(node); });
	return result;
}

// visitSourceFile — async.go:46
Node* asyncTransformer::visitSourceFile(SourceFile* node) {
	if (node->IsDeclarationFile) {
		return node->asNode();
	}

	setContextFlag(asyncContextNonTopLevel, false);
	setContextFlag(asyncContextHasLexicalThis, false);
	Node* visited = visitor()->visitEachChild(node->asNode());
	for (printer::EmitHelper* helper : emitContext()->readEmitHelpers()) {
		emitContext()->addEmitHelper(visited, helper);
	}
	return visited;
}

// setContextFlag — async.go:58
void asyncTransformer::setContextFlag(asyncContextFlags flag, bool val) {
	if (val) {
		contextFlags |= flag;
	} else {
		contextFlags &= ~flag;
	}
}

// inContext — async.go:66
bool asyncTransformer::inContext(asyncContextFlags flags) {
	return (contextFlags & flags) != 0;
}

// inTopLevelContext — async.go:70
bool asyncTransformer::inTopLevelContext() {
	return !inContext(asyncContextNonTopLevel);
}

// inHasLexicalThisContext — async.go:74
bool asyncTransformer::inHasLexicalThisContext() {
	return inContext(asyncContextHasLexicalThis);
}

// doWithContext — async.go:78
Node* asyncTransformer::doWithContext(asyncContextFlags flags,
                                      Node* (asyncTransformer::*cb)(Node*),
                                      Node* node) {
	asyncContextFlags flagsToSet = flags & ~contextFlags;
	if (flagsToSet != 0) {
		setContextFlag(flagsToSet, true);
		Node* result = (this->*cb)(node);
		setContextFlag(flagsToSet, false);
		return result;
	}
	return (this->*cb)(node);
}

// visitDefault — async.go:89
Node* asyncTransformer::visitDefault(Node* node) {
	return visitor()->visitEachChild(node);
}

// fallbackVisitor — async.go:93
Node* asyncTransformer::fallbackVisitor(Node* node) {
	if (capturedSuperProperties == nullptr &&
	    lexicalArguments.binding == nullptr) {
		return node;
	}
	trackSuperAccess(node);
	switch (node->kind) {
	case Kind::FunctionExpression:
	case Kind::FunctionDeclaration:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::Constructor:
		return node;
	case Kind::Parameter:
	case Kind::BindingElement:
	case Kind::VariableDeclaration:
		// fall through to visitEachChild
		break;
	case Kind::Identifier:
		if (lexicalArguments.binding != nullptr &&
		    node->text() == "arguments" && !isIdentifierName(node) &&
		    !isLabelName(node)) {
			lexicalArguments.used = true;
			return lexicalArguments.binding;
		}
		break;
	default:
		break;
	}
	return fallbackNodeVisitor->visitEachChild(node);
}

// visitFallback — async.go:122
Node* asyncTransformer::visitFallback(Node* node) {
	return fallbackVisitor(node);
}

// visit — async.go:126
Node* asyncTransformer::visit(Node* node) {
	std::optional<FnGuard> lexicalThisGuard;
	if ((emitContext()->emitFlags(node) & printer::EFNoLexicalThis) != 0 &&
	    inHasLexicalThisContext()) {
		setContextFlag(asyncContextHasLexicalThis, false);
		lexicalThisGuard.emplace([this] {
			setContextFlag(asyncContextHasLexicalThis, true);
		});
	}

	if ((node->subtreeFacts() &
	     (SubtreeContainsAnyAwait | SubtreeContainsAwait)) == 0) {
		return fallbackVisitor(node);
	}
	trackSuperAccess(node);
	switch (node->kind) {
	case Kind::AsyncKeyword:
		// ES2017 async modifier should be elided for targets < ES2017
		return nullptr;
	case Kind::SourceFile:
		return visitSourceFile(node->as<SourceFile>());
	case Kind::AwaitExpression:
		return visitAwaitExpression(node->as<AwaitExpression>());
	case Kind::MethodDeclaration:
		return doWithContext(asyncContextNonTopLevel | asyncContextHasLexicalThis,
		                     &asyncTransformer::visitMethodDeclaration, node);
	case Kind::FunctionDeclaration:
		return doWithContext(asyncContextNonTopLevel | asyncContextHasLexicalThis,
		                     &asyncTransformer::visitFunctionDeclaration, node);
	case Kind::FunctionExpression:
		return doWithContext(asyncContextNonTopLevel | asyncContextHasLexicalThis,
		                     &asyncTransformer::visitFunctionExpression, node);
	case Kind::ArrowFunction:
		return doWithContext(asyncContextNonTopLevel,
		                     &asyncTransformer::visitArrowFunction, node);
	case Kind::GetAccessor:
		return doWithContext(asyncContextNonTopLevel | asyncContextHasLexicalThis,
		                     &asyncTransformer::visitGetAccessorDeclaration,
		                     node);
	case Kind::SetAccessor:
		return doWithContext(asyncContextNonTopLevel | asyncContextHasLexicalThis,
		                     &asyncTransformer::visitSetAccessorDeclaration,
		                     node);
	case Kind::Constructor:
		return doWithContext(asyncContextNonTopLevel | asyncContextHasLexicalThis,
		                     &asyncTransformer::visitConstructorDeclaration,
		                     node);
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
		return doWithContext(asyncContextNonTopLevel | asyncContextHasLexicalThis,
		                     &asyncTransformer::visitDefault, node);
	default:
		return visitor()->visitEachChild(node);
	}
}

// visitAsyncBodyNode — async.go:165
Node* asyncTransformer::visitAsyncBodyNode(Node* node) {
	if (isNodeWithPossibleHoistedDeclaration(node)) {
		switch (node->kind) {
		case Kind::VariableStatement:
			return visitVariableStatementInAsyncBody(node);
		case Kind::ForStatement:
			return visitForStatementInAsyncBody(node->as<ForStatement>());
		case Kind::ForInStatement:
			return visitForInStatementInAsyncBody(
				node->as<ForInOrOfStatement>());
		case Kind::ForOfStatement:
			return visitForOfStatementInAsyncBody(
				node->as<ForInOrOfStatement>());
		case Kind::CatchClause:
			return visitCatchClauseInAsyncBody(node->as<CatchClause>());
		case Kind::Block:
		case Kind::SwitchStatement:
		case Kind::CaseBlock:
		case Kind::CaseClause:
		case Kind::DefaultClause:
		case Kind::TryStatement:
		case Kind::DoStatement:
		case Kind::WhileStatement:
		case Kind::IfStatement:
		case Kind::WithStatement:
		case Kind::LabeledStatement:
			return asyncBodyVisitor->visitEachChild(node);
		default:
			break;
		}
	}
	return visit(node);
}

// visitCatchClauseInAsyncBody — async.go:195
Node* asyncTransformer::visitCatchClauseInAsyncBody(CatchClause* node) {
	collections::Set<std::string> catchClauseNames;
	if (node->VariableDeclaration != nullptr) {
		recordDeclarationName(node->VariableDeclaration, &catchClauseNames);
	}

	// names declared in a catch variable are block scoped
	collections::Set<std::string>* catchClauseUnshadowedNames = nullptr;
	for (const std::string& escapedName : catchClauseNames.Keys()) {
		if (enclosingFunctionParameterNames != nullptr &&
		    enclosingFunctionParameterNames->Has(escapedName)) {
			if (catchClauseUnshadowedNames == nullptr) {
				catchClauseUnshadowedNames =
					new collections::Set<std::string>(
						enclosingFunctionParameterNames->Clone());
			}
			catchClauseUnshadowedNames->Delete(escapedName);
		}
	}

	if (catchClauseUnshadowedNames != nullptr) {
		collections::Set<std::string>* savedEnclosingFunctionParameterNames =
			enclosingFunctionParameterNames;
		enclosingFunctionParameterNames = catchClauseUnshadowedNames;
		Node* result = asyncBodyVisitor->visitEachChild(node->asNode());
		enclosingFunctionParameterNames = savedEnclosingFunctionParameterNames;
		return result;
	}
	return asyncBodyVisitor->visitEachChild(node->asNode());
}

// visitVariableStatementInAsyncBody — async.go:222
Node* asyncTransformer::visitVariableStatementInAsyncBody(Node* node) {
	Node* declList = node->as<VariableStatement>()->DeclarationList;
	if (isVariableDeclarationListWithCollidingName(declList)) {
		Node* expression = visitVariableDeclarationListWithCollidingNames(
			declList->as<VariableDeclarationList>(), false);
		if (expression != nullptr) {
			return Transformer::factory()->newExpressionStatement(expression);
		}
		return nullptr;
	}
	return visitor()->visitEachChild(node);
}

// visitForInStatementInAsyncBody — async.go:234
Node* asyncTransformer::visitForInStatementInAsyncBody(
	ForInOrOfStatement* node) {
	Node* visitedInitializer = nullptr;
	if (isVariableDeclarationListWithCollidingName(node->Initializer)) {
		visitedInitializer = visitVariableDeclarationListWithCollidingNames(
			node->Initializer->as<VariableDeclarationList>(), true);
	} else {
		visitedInitializer = visitor()->visitNode(node->Initializer);
	}

	Node* visitedExpression = visitor()->visitNode(node->Expression);
	Node* visitedStatement =
		asyncBodyVisitor->visitEmbeddedStatement(node->Statement);
	return Transformer::factory()->updateForInOrOfStatement(
		node,
		nullptr, /*awaitModifier*/
		visitedInitializer, visitedExpression, visitedStatement);
}

// visitForOfStatementInAsyncBody — async.go:251
Node* asyncTransformer::visitForOfStatementInAsyncBody(
	ForInOrOfStatement* node) {
	Node* visitedInitializer = nullptr;
	if (isVariableDeclarationListWithCollidingName(node->Initializer)) {
		visitedInitializer = visitVariableDeclarationListWithCollidingNames(
			node->Initializer->as<VariableDeclarationList>(), true);
	} else {
		visitedInitializer = visitor()->visitNode(node->Initializer);
	}

	Node* visitedAwaitModifier = visitor()->visitNode(node->AwaitModifier);
	Node* visitedExpression = visitor()->visitNode(node->Expression);
	Node* visitedStatement =
		asyncBodyVisitor->visitEmbeddedStatement(node->Statement);
	return Transformer::factory()->updateForInOrOfStatement(
		node, visitedAwaitModifier, visitedInitializer, visitedExpression,
		visitedStatement);
}

// visitForStatementInAsyncBody — async.go:268
Node* asyncTransformer::visitForStatementInAsyncBody(ForStatement* node) {
	Node* initializer = node->Initializer;
	Node* visitedInitializer = nullptr;
	if (initializer != nullptr &&
	    isVariableDeclarationListWithCollidingName(initializer)) {
		visitedInitializer = visitVariableDeclarationListWithCollidingNames(
			initializer->as<VariableDeclarationList>(), false);
	} else {
		visitedInitializer = visitor()->visitNode(node->Initializer);
	}

	Node* visitedCondition = visitor()->visitNode(node->Condition);
	Node* visitedIncrementor = visitor()->visitNode(node->Incrementor);
	Node* visitedStatement =
		asyncBodyVisitor->visitEmbeddedStatement(node->Statement);
	return Transformer::factory()->updateForStatement(
		node, visitedInitializer, visitedCondition, visitedIncrementor,
		visitedStatement);
}

// visitAwaitExpression visits an AwaitExpression node. — async.go:286
//
// This function will be called any time a ES2017 await expression is encountered.
Node* asyncTransformer::visitAwaitExpression(AwaitExpression* node) {
	// do not downlevel a top-level await as it is module syntax...
	if (inTopLevelContext()) {
		return visitor()->visitEachChild(node->asNode());
	}
	Node* yieldExpr = Transformer::factory()->newYieldExpression(
		nullptr, /*asteriskToken*/
		visitor()->visitNode(node->Expression));
	yieldExpr->loc = node->loc;
	emitContext()->setOriginal(yieldExpr, node->asNode());
	return yieldExpr;
}

// visitConstructorDeclaration — async.go:303
Node* asyncTransformer::visitConstructorDeclaration(Node* node) {
	ConstructorDeclaration* decl = node->as<ConstructorDeclaration>();
	lexicalArgumentsInfo savedLexicalArguments = lexicalArguments;
	lexicalArguments = lexicalArgumentsInfo{};
	ModifierList* visitedModifiers = visitor()->visitModifiers(decl->modifiers);
	NodeList* visitedParameters =
		emitContext()->visitParameters(decl->Parameters, visitor());
	Node* transformedBody = transformMethodBody(node);
	Node* updated = Transformer::factory()->updateConstructorDeclaration(
		decl, visitedModifiers,
		nullptr, /*typeParameters*/
		visitedParameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		transformedBody);
	lexicalArguments = savedLexicalArguments;
	return updated;
}

// visitMethodDeclaration visits a MethodDeclaration node. — async.go:320
//
// This function will be called when one of the following conditions are met:
// - The node is marked as async
Node* asyncTransformer::visitMethodDeclaration(Node* node) {
	MethodDeclaration* decl = node->as<MethodDeclaration>();
	FunctionFlags functionFlags = getFunctionFlags(node);
	lexicalArgumentsInfo savedLexicalArguments = lexicalArguments;
	lexicalArguments = lexicalArgumentsInfo{};

	NodeList* parameters = nullptr;
	Node* body = nullptr;
	if ((functionFlags & FunctionFlagsAsync) != 0) {
		parameters = transformAsyncFunctionParameterList(node);
		body = transformAsyncFunctionBody(node, parameters);
	} else {
		parameters = emitContext()->visitParameters(decl->Parameters, visitor());
		body = transformMethodBody(node);
	}

	ModifierList* visitedModifiers = visitor()->visitModifiers(decl->modifiers);
	Node* updated = Transformer::factory()->updateMethodDeclaration(
		decl, visitedModifiers,
		decl->AsteriskToken, decl->name,
		nullptr, /*postfixToken*/
		nullptr, /*typeParameters*/
		parameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		body);
	lexicalArguments = savedLexicalArguments;
	return updated;
}

// visitGetAccessorDeclaration — async.go:356
Node* asyncTransformer::visitGetAccessorDeclaration(Node* node) {
	GetAccessorDeclaration* decl = node->as<GetAccessorDeclaration>();
	lexicalArgumentsInfo savedLexicalArguments = lexicalArguments;
	lexicalArguments = lexicalArgumentsInfo{};
	ModifierList* visitedModifiers = visitor()->visitModifiers(decl->modifiers);
	NodeList* visitedParameters =
		emitContext()->visitParameters(decl->Parameters, visitor());
	Node* transformedBody = transformMethodBody(node);
	Node* updated = Transformer::factory()->updateGetAccessorDeclaration(
		decl, visitedModifiers, decl->name,
		nullptr, /*typeParameters*/
		visitedParameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		transformedBody);
	lexicalArguments = savedLexicalArguments;
	return updated;
}

// visitSetAccessorDeclaration — async.go:374
Node* asyncTransformer::visitSetAccessorDeclaration(Node* node) {
	SetAccessorDeclaration* decl = node->as<SetAccessorDeclaration>();
	lexicalArgumentsInfo savedLexicalArguments = lexicalArguments;
	lexicalArguments = lexicalArgumentsInfo{};
	ModifierList* visitedModifiers = visitor()->visitModifiers(decl->modifiers);
	NodeList* visitedParameters =
		emitContext()->visitParameters(decl->Parameters, visitor());
	Node* transformedBody = transformMethodBody(node);
	Node* updated = Transformer::factory()->updateSetAccessorDeclaration(
		decl, visitedModifiers, decl->name,
		nullptr, /*typeParameters*/
		visitedParameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		transformedBody);
	lexicalArguments = savedLexicalArguments;
	return updated;
}

// visitFunctionDeclaration visits a FunctionDeclaration node. — async.go:392
//
// This function will be called when one of the following conditions are met:
// - The node is marked async
Node* asyncTransformer::visitFunctionDeclaration(Node* node) {
	FunctionDeclaration* decl = node->as<FunctionDeclaration>();
	FunctionFlags functionFlags = getFunctionFlags(node);
	lexicalArgumentsInfo savedLexicalArguments = lexicalArguments;
	lexicalArguments = lexicalArgumentsInfo{};

	NodeList* parameters = nullptr;
	Node* body = nullptr;
	if ((functionFlags & FunctionFlagsAsync) != 0) {
		parameters = transformAsyncFunctionParameterList(node);
		body = transformAsyncFunctionBody(node, parameters);
	} else {
		parameters = emitContext()->visitParameters(decl->Parameters, visitor());
		body = emitContext()->visitFunctionBody(decl->Body, visitor());
	}

	ModifierList* visitedModifiers = visitor()->visitModifiers(decl->modifiers);
	Node* visitedName = visitor()->visitNode(decl->name);
	Node* updated = Transformer::factory()->updateFunctionDeclaration(
		decl, visitedModifiers,
		decl->AsteriskToken, visitedName,
		nullptr, /*typeParameters*/
		parameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		body);
	lexicalArguments = savedLexicalArguments;
	return updated;
}

// visitFunctionExpression visits a FunctionExpression node. — async.go:427
//
// This function will be called when one of the following conditions are met:
// - The node is marked async
Node* asyncTransformer::visitFunctionExpression(Node* node) {
	FunctionExpression* decl = node->as<FunctionExpression>();
	FunctionFlags functionFlags = getFunctionFlags(node);
	lexicalArgumentsInfo savedLexicalArguments = lexicalArguments;
	lexicalArguments = lexicalArgumentsInfo{};

	NodeList* parameters = nullptr;
	Node* body = nullptr;
	if ((functionFlags & FunctionFlagsAsync) != 0) {
		parameters = transformAsyncFunctionParameterList(node);
		body = transformAsyncFunctionBody(node, parameters);
	} else {
		parameters = emitContext()->visitParameters(decl->Parameters, visitor());
		body = emitContext()->visitFunctionBody(decl->Body, visitor());
	}

	ModifierList* visitedModifiers = visitor()->visitModifiers(decl->modifiers);
	Node* visitedName = visitor()->visitNode(decl->name);
	Node* updated = Transformer::factory()->updateFunctionExpression(
		decl, visitedModifiers,
		decl->AsteriskToken, visitedName,
		nullptr, /*typeParameters*/
		parameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		body);
	lexicalArguments = savedLexicalArguments;
	return updated;
}

// visitArrowFunction visits an ArrowFunction. — async.go:462
//
// This function will be called when one of the following conditions are met:
// - The node is marked async
Node* asyncTransformer::visitArrowFunction(Node* node) {
	// `arguments` in class static blocks is always an error, but we preserve Strada's emit
	// behavior for baseline compatibility. In Strada, checker-based `isArgumentsLocalBinding`
	// returns false for `arguments` in static blocks (since the binding doesn't exist due to
	// the error), so the async transform leaves them untouched.
	std::optional<FnGuard> argumentsGuard;
	if ((emitContext()->emitFlags(node) & printer::EFNoLexicalArguments) !=
	    0) {
		lexicalArgumentsInfo savedLexicalArguments = lexicalArguments;
		lexicalArguments = lexicalArgumentsInfo{};
		argumentsGuard.emplace([this, savedLexicalArguments] {
			lexicalArguments = savedLexicalArguments;
		});
	}

	ArrowFunction* decl = node->as<ArrowFunction>();
	FunctionFlags functionFlags = getFunctionFlags(node);

	NodeList* parameters = nullptr;
	Node* body = nullptr;
	if ((functionFlags & FunctionFlagsAsync) != 0) {
		parameters = transformAsyncFunctionParameterList(node);
		body = transformAsyncFunctionBody(node, parameters);
	} else {
		parameters = emitContext()->visitParameters(decl->Parameters, visitor());
		body = emitContext()->visitFunctionBody(decl->Body, visitor());
	}

	ModifierList* visitedModifiers = visitor()->visitModifiers(decl->modifiers);
	return Transformer::factory()->updateArrowFunction(
		decl, visitedModifiers,
		nullptr, /*typeParameters*/
		parameters,
		nullptr, /*returnType*/
		nullptr, /*fullSignature*/
		decl->EqualsGreaterThanToken, body);
}

// recordDeclarationName — async.go:502
void asyncTransformer::recordDeclarationName(
	Node* node, collections::Set<std::string>* names) {
	Node* name = node->name();
	if (name == nullptr) {
		return;
	}
	if (isIdentifier(name)) {
		names->Add(name->text());
	} else if (isBindingPattern(name)) {
		for (Node* element : name->as<BindingPattern>()->Elements->nodes) {
			if (!isOmittedExpression(element)) {
				recordDeclarationName(element, names);
			}
		}
	}
}

// isVariableDeclarationListWithCollidingName — async.go:518
bool asyncTransformer::isVariableDeclarationListWithCollidingName(Node* node) {
	if (node == nullptr || !isVariableDeclarationList(node) ||
	    (node->flags & NodeFlagsBlockScoped) != 0) {
		return false;
	}
	for (Node* decl :
	     node->as<VariableDeclarationList>()->Declarations->nodes) {
		if (collidesWithParameterName(decl)) {
			return true;
		}
	}
	return false;
}

// visitVariableDeclarationListWithCollidingNames — async.go:525
Node* asyncTransformer::visitVariableDeclarationListWithCollidingNames(
	VariableDeclarationList* node, bool hasReceiver) {
	hoistVariableDeclarationList(node);

	std::vector<Node*> variables;
	for (Node* decl : node->Declarations->nodes) {
		if (decl->as<VariableDeclaration>()->Initializer != nullptr) {
			variables.push_back(decl);
		}
	}

	if (variables.empty()) {
		if (hasReceiver) {
			Node* name = node->Declarations->nodes[0]->name();
			Node* target = nullptr;
			if (isBindingPattern(name)) {
				target = convertBindingPatternToAssignmentPattern(
					emitContext(), name->as<BindingPattern>());
			} else {
				target = name;
			}
			return visitor()->visitNode(target);
		}
		return nullptr;
	}

	std::vector<Node*> expressions;
	for (Node* variable : variables) {
		expressions.push_back(transformInitializedVariable(
			variable->as<VariableDeclaration>()));
	}
	return Transformer::factory()->inlineExpressions(expressions);
}

// hoistVariableDeclarationList — async.go:556
void asyncTransformer::hoistVariableDeclarationList(
	VariableDeclarationList* node) {
	for (Node* decl : node->Declarations->nodes) {
		hoistVariable(decl);
	}
}

// hoistVariable — async.go:562
void asyncTransformer::hoistVariable(Node* node) {
	Node* name = node->name();
	if (name == nullptr) {
		return;
	}
	if (isIdentifier(name)) {
		emitContext()->addVariableDeclaration(name);
	} else if (isBindingPattern(name)) {
		for (Node* element : name->as<BindingPattern>()->Elements->nodes) {
			if (!isOmittedExpression(element)) {
				hoistVariable(element);
			}
		}
	}
}

// transformInitializedVariable — async.go:578
Node* asyncTransformer::transformInitializedVariable(
	VariableDeclaration* node) {
	Node* target = nullptr;
	if (isBindingPattern(node->name)) {
		target = convertBindingPatternToAssignmentPattern(
			emitContext(), node->name->as<BindingPattern>());
	} else {
		target = node->name;
	}
	Node* converted =
		Transformer::factory()->newAssignmentExpression(target, node->Initializer);
	emitContext()->setSourceMapRange(converted, node->loc);
	return visitor()->visitNode(converted);
}

// collidesWithParameterName — async.go:590
bool asyncTransformer::collidesWithParameterName(Node* node) {
	Node* name = node->name();
	if (name == nullptr) {
		return false;
	}
	if (isIdentifier(name)) {
		return enclosingFunctionParameterNames != nullptr &&
		       enclosingFunctionParameterNames->Has(name->text());
	}
	if (isBindingPattern(name)) {
		for (Node* element : name->as<BindingPattern>()->Elements->nodes) {
			if (!isOmittedExpression(element) &&
			    collidesWithParameterName(element)) {
				return true;
			}
		}
	}
	return false;
}

// transformMethodBody — async.go:608
Node* asyncTransformer::transformMethodBody(Node* node) {
	printer::OrderedSet<std::string>* savedCapturedSuperProperties =
		capturedSuperProperties;
	bool savedHasSuperElementAccess = hasSuperElementAccess;
	bool savedHasSuperPropertyAssignment = hasSuperPropertyAssignment;
	Node* savedSuperBinding = superBinding;
	Node* savedSuperIndexBinding = superIndexBinding;
	capturedSuperProperties = new printer::OrderedSet<std::string>();
	hasSuperElementAccess = false;
	hasSuperPropertyAssignment = false;
	superBinding = Transformer::factory()->newUniqueName(
		"_super",
		printer::AutoGenerateOptions{
			printer::GeneratedIdentifierFlagsOptimistic |
				printer::GeneratedIdentifierFlagsFileLevel,
			"", ""});
	superIndexBinding = Transformer::factory()->newUniqueName(
		"_superIndex",
		printer::AutoGenerateOptions{
			printer::GeneratedIdentifierFlagsOptimistic |
				printer::GeneratedIdentifierFlagsFileLevel,
			"", ""});

	emitContext()->startVariableEnvironment();
	Node* updated = emitContext()->visitFunctionBody(node->body(), visitor());

	// Minor optimization, emit `_super` helper to capture `super` access in an arrow.
	bool emitSuperHelpers =
		(capturedSuperProperties->size() > 0 || hasSuperElementAccess) &&
		(getFunctionFlags(getOriginalIfFunctionLike(node)) &
		 FunctionFlagsAsyncGenerator) != FunctionFlagsAsyncGenerator;

	if (emitSuperHelpers) {
		if (capturedSuperProperties->size() > 0) {
			emitContext()->addInitializationStatement(
				createSuperAccessVariableStatement());
		}
	}

	NodeList* mergedStatements =
		emitContext()->endAndMergeVariableEnvironmentList(
			updated->statementList());
	if (emitSuperHelpers && hasSuperElementAccess &&
	    !updated->as<Block>()->MultiLine) {
		Node* newBlock =
			Transformer::factory()->newBlock(mergedStatements, true);
		newBlock->loc = updated->loc;
		updated = newBlock;
	} else {
		updated = Transformer::factory()->updateBlock(
			updated->as<Block>(), mergedStatements,
			updated->as<Block>()->MultiLine);
	}

	if (emitSuperHelpers && hasSuperElementAccess) {
		if (hasSuperPropertyAssignment) {
			emitContext()->addEmitHelper(updated,
			                             printer::AdvancedAsyncSuperHelper);
		} else {
			emitContext()->addEmitHelper(updated, printer::AsyncSuperHelper);
		}
	}

	capturedSuperProperties = savedCapturedSuperProperties;
	hasSuperElementAccess = savedHasSuperElementAccess;
	hasSuperPropertyAssignment = savedHasSuperPropertyAssignment;
	superBinding = savedSuperBinding;
	superIndexBinding = savedSuperIndexBinding;
	return updated;
}

// createCaptureArgumentsStatement — async.go:658
Node* asyncTransformer::createCaptureArgumentsStatement() {
	Node* variable = Transformer::factory()->newVariableDeclaration(
		lexicalArguments.binding, nullptr, nullptr,
		Transformer::factory()->newIdentifier("arguments"));
	Node* declList = Transformer::factory()->newVariableDeclarationList(
		Transformer::factory()->newNodeList({variable}), NodeFlagsNone);
	Node* statement =
		Transformer::factory()->newVariableStatement(nullptr, declList);
	emitContext()->addEmitFlags(
		statement, printer::EFStartOnNewLine | printer::EFCustomPrologue);
	return statement;
}

// transformAsyncFunctionParameterList — async.go:671
NodeList* asyncTransformer::transformAsyncFunctionParameterList(Node* node) {
	if (isSimpleParameterList(node->parameters())) {
		return emitContext()->visitParameters(node->parameterList(), visitor());
	}

	std::vector<Node*> newParameters;
	for (Node* parameter : node->parameters()) {
		ParameterDeclaration* param = parameter->as<ParameterDeclaration>();
		if (param->Initializer != nullptr || param->DotDotDotToken != nullptr) {
			// for an arrow function, capture the remaining arguments in a rest parameter.
			// for any other function/method this isn't necessary as we can just use `arguments`.
			if (node->kind == Kind::ArrowFunction) {
				Node* restParameter =
					Transformer::factory()->newParameterDeclaration(
						nullptr,
						Transformer::factory()->newToken(
							Kind::DotDotDotToken),
						Transformer::factory()->newUniqueName(
							"args",
							printer::AutoGenerateOptions{
								printer::
									GeneratedIdentifierFlagsReservedInNestedScopes,
								"", ""}),
						nullptr, nullptr, nullptr);
				newParameters.push_back(restParameter);
			}
			break;
		}
		// for arrow functions we capture fixed parameters to forward to `__awaiter`. For all other functions
		// we add fixed parameters to preserve the function's `length` property.
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

// transformAsyncFunctionBody — async.go:712
Node* asyncTransformer::transformAsyncFunctionBody(Node* node,
                                                   NodeList* outerParameters) {
	bool isArrow = node->kind == Kind::ArrowFunction;
	printer::OrderedSet<std::string>* savedCapturedSuperProperties =
		capturedSuperProperties;
	bool savedHasSuperElementAccess = hasSuperElementAccess;
	bool savedHasSuperPropertyAssignment = hasSuperPropertyAssignment;
	Node* savedSuperBinding = superBinding;
	Node* savedSuperIndexBinding = superIndexBinding;
	if (!isArrow) {
		capturedSuperProperties = new printer::OrderedSet<std::string>();
		hasSuperElementAccess = false;
		hasSuperPropertyAssignment = false;
		superBinding = Transformer::factory()->newUniqueName(
			"_super",
			printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsOptimistic |
					printer::GeneratedIdentifierFlagsFileLevel,
				"", ""});
		superIndexBinding = Transformer::factory()->newUniqueName(
			"_superIndex",
			printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsOptimistic |
					printer::GeneratedIdentifierFlagsFileLevel,
				"", ""});
	}

	NodeList* innerParameters = nullptr;
	if (!isSimpleParameterList(node->parameters())) {
		innerParameters =
			emitContext()->visitParameters(node->parameterList(), visitor());
	}

	lexicalArgumentsInfo savedLexicalArguments = lexicalArguments;
	bool captureLexicalArguments = lexicalArguments.binding == nullptr;
	if (captureLexicalArguments) {
		lexicalArguments = lexicalArgumentsInfo{
			Transformer::factory()->newUniqueName("arguments"), false};
	}

	Node* argumentsExpression = nullptr;
	if (innerParameters != nullptr) {
		if (isArrow) {
			// `node` does not have a simple parameter list, so `outerParameters` refers to placeholders that are
			// forwarded to `innerParameters`, matching how they are introduced in `transformAsyncFunctionParameterList`.
			std::vector<Node*> parameterBindings;
			size_t outerLen = outerParameters->nodes.size();
			std::vector<Node*> params = node->parameters();
			for (size_t i = 0; i < params.size(); i++) {
				if (i >= outerLen) {
					break;
				}
				ParameterDeclaration* originalParameter =
					params[i]->as<ParameterDeclaration>();
				ParameterDeclaration* outerParameter =
					outerParameters->nodes[i]->as<ParameterDeclaration>();
				if (originalParameter->Initializer != nullptr ||
				    originalParameter->DotDotDotToken != nullptr) {
					parameterBindings.push_back(
						Transformer::factory()->newSpreadElement(
							outerParameter->name));
					break;
				}
				parameterBindings.push_back(outerParameter->name);
			}
			argumentsExpression =
				Transformer::factory()->newArrayLiteralExpression(
					Transformer::factory()->newNodeList(parameterBindings),
					false);
		} else {
			argumentsExpression =
				Transformer::factory()->newIdentifier("arguments");
		}
	}

	// An async function is emit as an outer function that calls an inner
	// generator function. To preserve lexical bindings, we pass the current
	// `this` and `arguments` objects to `__awaiter`. The generator function
	// passed to `__awaiter` is executed inside of the callback to the
	// promise constructor.

	collections::Set<std::string>* savedEnclosingFunctionParameterNames =
		enclosingFunctionParameterNames;
	enclosingFunctionParameterNames = new collections::Set<std::string>();
	for (Node* parameter : node->parameters()) {
		recordDeclarationName(parameter, enclosingFunctionParameterNames);
	}

	bool hasLexicalThis = inHasLexicalThisContext();

	Node* asyncBody = transformAsyncFunctionBodyWorker(node->body());
	{
		Block* asyncBodyBlock = asyncBody->as<Block>();
		NodeList* mergedStatements =
			emitContext()->endAndMergeVariableEnvironmentList(
				asyncBodyBlock->statementList());
		asyncBody = Transformer::factory()->updateBlock(
			asyncBodyBlock, mergedStatements, asyncBodyBlock->MultiLine);
	}

	// Substitute super property accesses with _super/_superIndex helpers
	bool emitSuperHelpers = capturedSuperProperties != nullptr &&
	                      (capturedSuperProperties->size() > 0 ||
	                       hasSuperElementAccess);
	if (emitSuperHelpers) {
		innerParameters = superAccessVisitor->visitNodes(innerParameters);
		asyncBody = substituteSuperAccessesInBody(asyncBody);
	}

	Node* result = nullptr;
	if (!isArrow) {
		emitContext()->startVariableEnvironment();

		// Minor optimization, emit `_super` helper to capture `super` access in an arrow.
		if (emitSuperHelpers) {
			if (capturedSuperProperties->size() > 0) {
				emitContext()->addInitializationStatement(
					createSuperAccessVariableStatement());
			}
		}

		if (captureLexicalArguments && lexicalArguments.used) {
			emitContext()->addInitializationStatement(
				createCaptureArgumentsStatement());
		}

		std::vector<Node*> statements{
			Transformer::factory()->newReturnStatement(
				Transformer::factory()->newAwaiterHelper(
					hasLexicalThis, argumentsExpression, innerParameters,
					asyncBody)),
		};

		Node* block = Transformer::factory()->newBlock(
			emitContext()->endAndMergeVariableEnvironmentList(
				Transformer::factory()->newNodeList(statements)),
			true);
		block->loc = node->body()->loc;

		if (emitSuperHelpers && hasSuperElementAccess) {
			if (hasSuperPropertyAssignment) {
				emitContext()->addEmitHelper(
					block, printer::AdvancedAsyncSuperHelper);
			} else {
				emitContext()->addEmitHelper(block,
				                             printer::AsyncSuperHelper);
			}
		}

		result = block;
	} else {
		result = Transformer::factory()->newAwaiterHelper(
			hasLexicalThis, argumentsExpression, innerParameters, asyncBody);

		if (captureLexicalArguments && lexicalArguments.used) {
			Node* block = emitContext()->convertToFunctionBlock(
				result, true /*multiLine*/);
			if (!isBlock(result)) {
				emitContext()->setOriginal(
					block->statementList()->nodes[0], result);
			}
			result = Transformer::factory()->updateBlock(
				block->as<Block>(),
				emitContext()->mergeEnvironmentList(
					block->statementList(),
					std::vector<Node*>{createCaptureArgumentsStatement()}),
				block->as<Block>()->MultiLine);
		}
	}

	enclosingFunctionParameterNames = savedEnclosingFunctionParameterNames;
	if (!isArrow) {
		capturedSuperProperties = savedCapturedSuperProperties;
		hasSuperElementAccess = savedHasSuperElementAccess;
		hasSuperPropertyAssignment = savedHasSuperPropertyAssignment;
		superBinding = savedSuperBinding;
		superIndexBinding = savedSuperIndexBinding;
		lexicalArguments = savedLexicalArguments;
	} else if (captureLexicalArguments && !lexicalArguments.used) {
		// If we created a new binding but it wasn't used, restore the previous state.
		// If it was used, keep the binding alive so sibling arrows can reuse it
		// (the `var` declaration hoists to the enclosing function scope).
		lexicalArguments = savedLexicalArguments;
	} else if (captureLexicalArguments) {
		// Keep the binding but clear the used flag so siblings don't re-emit the capture statement.
		lexicalArguments.used = false;
	}
	return result;
}

// transformAsyncFunctionBodyWorker — async.go:876
Node* asyncTransformer::transformAsyncFunctionBodyWorker(Node* body) {
	if (isBlock(body)) {
		Block* bodyBlock = body->as<Block>();
		NodeList* visitedStatements =
			asyncBodyVisitor->visitNodes(body->statementList());
		return Transformer::factory()->updateBlock(
			bodyBlock, visitedStatements, bodyBlock->MultiLine);
	}
	// Convert expression body to block body with return statement
	Node* visited = asyncBodyVisitor->visitNode(body);
	Node* ret = Transformer::factory()->newReturnStatement(visited);
	ret->loc = body->loc;
	NodeList* list = Transformer::factory()->newNodeList({ret});
	list->loc = body->loc;
	Node* block = Transformer::factory()->newBlock(list, false /*multiLine*/);
	block->loc = body->loc;
	return block;
}

// getOriginalIfFunctionLike — async.go:943
Node* asyncTransformer::getOriginalIfFunctionLike(Node* node) {
	Node* original = emitContext()->mostOriginal(node);
	if (original != nullptr && isFunctionLikeDeclaration(original)) {
		return original;
	}
	return node;
}

// isSimpleParameterList checks if every parameter has no initializer and an
// Identifier name. — async.go:951. Shared with forawait.cpp (declared in
// estransforms.h).
bool isSimpleParameterList(const std::vector<Node*>& params) {
	for (Node* param : params) {
		ParameterDeclaration* p = param->as<ParameterDeclaration>();
		if (p->Initializer != nullptr || !isIdentifier(p->name)) {
			return false;
		}
	}
	return true;
}

}  // namespace tsc::transformers::estransforms
