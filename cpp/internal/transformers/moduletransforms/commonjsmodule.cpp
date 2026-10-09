// Port of tsc/internal/transformers/moduletransforms/commonjsmodule.go —
// CommonJSModuleTransformer.
#include "internal/transformers/moduletransforms/moduletransforms.h"

#include <algorithm>

#include "internal/tspath/tspath.h"

namespace tsc::transformers::moduletransforms {

namespace {

// CommonJSModuleTransformer — commonjsmodule.go:14
struct CommonJSModuleTransformer : Transformer {
	NodeVisitor* topLevelVisitor =
		nullptr; // visits statements at top level of a module
	NodeVisitor* topLevelNestedVisitor =
		nullptr; // visits nested statements at top level of a module
	NodeVisitor* discardedValueVisitor =
		nullptr; // visits expressions whose values would be discarded at runtime
	NodeVisitor* assignmentPatternVisitor =
		nullptr; // visits assignment patterns in a destructuring assignment
	const CompilerOptions* compilerOptions = nullptr;
	binder::ReferenceResolver* resolver = nullptr;
	std::function<ModuleKind(SourceFile*)> getEmitModuleFormatOfFile;
	ModuleKind moduleKind = ModuleKind::None;
	ScriptTarget languageVersion = ScriptTarget::None;
	SourceFile* currentSourceFile = nullptr;
	externalModuleInfo* currentModuleInfo = nullptr;
	Node* parentNode =
		nullptr; // used for ancestor tracking via pushNode/popNode to detect
	             // expression identifiers
	Node* currentNode = nullptr; // used for ancestor tracking via
	                             // pushNode/popNode to detect expression
	                             // identifiers

	// pushNode — commonjsmodule.go:46. Pushes a new child node onto the
	// ancestor tracking stack, returning the grandparent node to be restored
	// later via `popNode`.
	Node* pushNode(Node* node) {
		Node* grandparentNode = parentNode;
		parentNode = currentNode;
		currentNode = node;
		return grandparentNode;
	}

	// popNode — commonjsmodule.go:54. Pops the last child node off the
	// ancestor tracking stack, restoring the grandparent node.
	void popNode(Node* grandparentNode) {
		currentNode = parentNode;
		parentNode = grandparentNode;
	}

	// RAII helper for the Go `defer tx.popNode(grandparentNode)` pattern.
	struct ancestorGuard {
		CommonJSModuleTransformer* tx;
		Node* grandparentNode;
		ancestorGuard(CommonJSModuleTransformer* tx, Node* node)
		    : tx(tx), grandparentNode(tx->pushNode(node)) {}
		~ancestorGuard() { tx->popNode(grandparentNode); }
	};

	Node* visitTopLevel(Node* node);
	Node* visitTopLevelNested(Node* node);
	Node* visitTopLevelNestedNoStack(Node* node);
	Node* visit(Node* node);
	Node* visitNoStack(Node* node, bool resultIsDiscarded);
	Node* visitDiscardedValue(Node* node);
	Node* visitAssignmentPattern(Node* node);
	Node* visitAssignmentPatternNoStack(Node* node);
	Node* visitSourceFile(SourceFile* node);
	bool shouldEmitUnderscoreUnderscoreESModule();
	Node* createUnderscoreUnderscoreESModule();
	Node* transformCommonJSModule(SourceFile* node);
	std::vector<Node*> appendExportEqualsIfNeeded(
		std::vector<Node*> statements);
	Node* visitExportEquals(ExportAssignment* node);
	std::vector<Node*> appendExportsOfImportDeclaration(
		std::vector<Node*> statements, ImportDeclaration* decl);
	std::vector<Node*> appendExportsOfVariableStatement(
		std::vector<Node*> statements, VariableStatement* node);
	std::vector<Node*> appendExportsOfVariableDeclarationList(
		std::vector<Node*> statements, VariableDeclarationList* node,
		bool isForInOrOfInitializer);
	std::vector<Node*> appendExportsOfBindingElement(
		std::vector<Node*> statements,
		Node* decl /*VariableDeclaration | BindingElement*/,
		bool isForInOrOfInitializer);
	std::vector<Node*> appendExportsOfClassOrFunctionDeclaration(
		std::vector<Node*> statements, Node* decl);
	std::vector<Node*> appendExportsOfDeclaration(
		std::vector<Node*> statements, Node* decl,
		collections::Set<std::string>* seen, bool liveBinding);
	std::vector<Node*> appendExportStatement(
		std::vector<Node*> statements, collections::Set<std::string>* seen,
		Node* exportName, Node* expression, TextRange* location,
		bool allowComments, bool liveBinding);
	Node* createExportStatement(Node* name, Node* value, TextRange* location,
	                            bool allowComments, bool liveBinding);
	Node* createExportExpression(Node* name, Node* value, TextRange* location,
	                             bool liveBinding);
	Node* createRequireCall(Node* node);
	Node* getHelperExpressionForExport(ExportDeclaration* node,
	                                   Node* innerExpr);
	Node* getHelperExpressionForImport(ImportDeclaration* node,
	                                   Node* innerExpr);
	Node* visitTopLevelImportDeclaration(ImportDeclaration* node);
	Node* visitTopLevelImportEqualsDeclaration(ImportEqualsDeclaration* node);
	Node* visitTopLevelExportDeclaration(ExportDeclaration* node);
	Node* visitTopLevelExportAssignment(ExportAssignment* node);
	Node* visitTopLevelFunctionDeclaration(FunctionDeclaration* node);
	Node* visitTopLevelClassDeclaration(ClassDeclaration* node);
	Node* visitTopLevelVariableStatement(VariableStatement* node);
	Node* transformInitializedVariable(VariableDeclaration* node);
	Node* visitTopLevelNestedVariableStatement(VariableStatement* node);
	Node* visitTopLevelNestedForStatement(ForStatement* node);
	Node* visitTopLevelNestedForInOrOfStatement(ForInOrOfStatement* node);
	Node* visitTopLevelNestedDoStatement(DoStatement* node);
	Node* visitTopLevelNestedWhileStatement(WhileStatement* node);
	Node* visitTopLevelNestedLabeledStatement(LabeledStatement* node);
	Node* visitTopLevelNestedWithStatement(WithStatement* node);
	Node* visitTopLevelNestedIfStatement(IfStatement* node);
	Node* visitTopLevelNestedSwitchStatement(SwitchStatement* node);
	Node* visitTopLevelNestedCaseBlock(CaseBlock* node);
	Node* visitTopLevelNestedCaseOrDefaultClause(CaseOrDefaultClause* node);
	Node* visitTopLevelNestedTryStatement(TryStatement* node);
	Node* visitTopLevelNestedCatchClause(CatchClause* node);
	Node* visitTopLevelNestedBlock(Block* node);
	Node* visitForStatement(ForStatement* node);
	Node* visitForInOrOfStatement(ForInOrOfStatement* node);
	Node* visitExpressionStatement(ExpressionStatement* node);
	Node* visitVoidExpression(VoidExpression* node);
	Node* visitParenthesizedExpression(ParenthesizedExpression* node,
	                                   bool resultIsDiscarded);
	Node* visitPartiallyEmittedExpression(PartiallyEmittedExpression* node,
	                                      bool resultIsDiscarded);
	Node* visitBinaryExpression(BinaryExpression* node,
	                            bool resultIsDiscarded);
	Node* visitAssignmentExpression(BinaryExpression* node);
	Node* visitDestructuringAssignment(BinaryExpression* node,
	                                   bool valueIsDiscarded);
	bool destructuringNeedsFlattening(Node* node);
	Node* createAllExportExpressions(Node* name, Node* value,
	                                 TextRange* location);
	bool isDirectExport(Node* name);
	Node* visitAssignmentProperty(PropertyAssignment* node);
	Node* visitShorthandAssignmentProperty(ShorthandPropertyAssignment* node);
	Node* visitAssignmentRestProperty(SpreadAssignment* node);
	Node* visitAssignmentRestElement(SpreadElement* node);
	Node* visitAssignmentElement(Node* node);
	Node* visitDestructuringAssignmentTarget(Node* node);
	Node* visitDestructuringAssignmentTargetNoStack(Node* node);
	Node* visitCommaExpression(BinaryExpression* node, bool resultIsDiscarded);
	Node* visitPrefixUnaryExpression(PrefixUnaryExpression* node,
	                                 bool resultIsDiscarded);
	Node* visitPostfixUnaryExpression(PostfixUnaryExpression* node,
	                                  bool resultIsDiscarded);
	Node* visitCallExpression(CallExpression* node);
	bool shouldTransformImportCall();
	Node* visitImportCallExpression(CallExpression* node, bool rewriteOrShim);
	Node* createImportCallExpressionCommonJS(Node* arg);
	Node* shimOrRewriteImportOrRequireCall(CallExpression* node);
	Node* visitTaggedTemplateExpression(TaggedTemplateExpression* node);
	Node* visitShorthandPropertyAssignment(ShorthandPropertyAssignment* node);
	Node* visitIdentifier(Node* node);
	Node* visitExpressionIdentifier(Node* node);
	std::vector<Node*> getExports(Node* name);
};

}  // namespace

// NewCommonJSModuleTransformer — commonjsmodule.go:32
Transformer* NewCommonJSModuleTransformer(TransformOptions* opts) {
	const CompilerOptions* compilerOptions = opts->CompilerOptions;
	printer::EmitContext* emitContext = opts->Context;
	auto* tx = new CommonJSModuleTransformer();
	tx->compilerOptions = compilerOptions;
	tx->resolver = opts->Resolver;
	tx->getEmitModuleFormatOfFile = opts->GetEmitModuleFormatOfFile;
	tx->topLevelVisitor = emitContext->newNodeVisitor(
		[tx](Node* node) { return tx->visitTopLevel(node); });
	tx->topLevelNestedVisitor = emitContext->newNodeVisitor(
		[tx](Node* node) { return tx->visitTopLevelNested(node); });
	tx->discardedValueVisitor = emitContext->newNodeVisitor(
		[tx](Node* node) { return tx->visitDiscardedValue(node); });
	tx->assignmentPatternVisitor = emitContext->newNodeVisitor(
		[tx](Node* node) { return tx->visitAssignmentPattern(node); });
	tx->languageVersion = compilerOptions->GetEmitScriptTarget();
	tx->moduleKind = compilerOptions->GetEmitModuleKind();
	return tx->newTransformer([tx](Node* node) { return tx->visit(node); },
	                          emitContext);
}

// Visits a node at the top level of the source file. — commonjsmodule.go:61
Node* CommonJSModuleTransformer::visitTopLevel(Node* node) {
	ancestorGuard guard(this, node);

	switch (node->kind) {
	case Kind::ImportDeclaration:
		node = visitTopLevelImportDeclaration(node->as<ImportDeclaration>());
		break;
	case Kind::ImportEqualsDeclaration:
		node = visitTopLevelImportEqualsDeclaration(
			node->as<ImportEqualsDeclaration>());
		break;
	case Kind::ExportDeclaration:
		node = visitTopLevelExportDeclaration(node->as<ExportDeclaration>());
		break;
	case Kind::ExportAssignment:
		node = visitTopLevelExportAssignment(node->as<ExportAssignment>());
		break;
	case Kind::FunctionDeclaration:
		node = visitTopLevelFunctionDeclaration(
			node->as<FunctionDeclaration>());
		break;
	case Kind::ClassDeclaration:
		node = visitTopLevelClassDeclaration(node->as<ClassDeclaration>());
		break;
	case Kind::VariableStatement:
		node = visitTopLevelVariableStatement(node->as<VariableStatement>());
		break;
	default:
		node = visitTopLevelNestedNoStack(node);
		break;
	}
	return node;
}

// Visits nested elements at the top-level of a module. — commonjsmodule.go:88
Node* CommonJSModuleTransformer::visitTopLevelNested(Node* node) {
	ancestorGuard guard(this, node);
	return visitTopLevelNestedNoStack(node);
}

// Visits nested elements at the top-level of a module without ancestor
// tracking. — commonjsmodule.go:96
Node* CommonJSModuleTransformer::visitTopLevelNestedNoStack(Node* node) {
	switch (node->kind) {
	case Kind::VariableStatement:
		node = visitTopLevelVariableStatement(node->as<VariableStatement>());
		break;
	case Kind::ForStatement:
		node = visitTopLevelNestedForStatement(node->as<ForStatement>());
		break;
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		node = visitTopLevelNestedForInOrOfStatement(
			node->as<ForInOrOfStatement>());
		break;
	case Kind::DoStatement:
		node = visitTopLevelNestedDoStatement(node->as<DoStatement>());
		break;
	case Kind::WhileStatement:
		node = visitTopLevelNestedWhileStatement(node->as<WhileStatement>());
		break;
	case Kind::LabeledStatement:
		node = visitTopLevelNestedLabeledStatement(
			node->as<LabeledStatement>());
		break;
	case Kind::WithStatement:
		node = visitTopLevelNestedWithStatement(node->as<WithStatement>());
		break;
	case Kind::IfStatement:
		node = visitTopLevelNestedIfStatement(node->as<IfStatement>());
		break;
	case Kind::SwitchStatement:
		node = visitTopLevelNestedSwitchStatement(node->as<SwitchStatement>());
		break;
	case Kind::CaseBlock:
		node = visitTopLevelNestedCaseBlock(node->as<CaseBlock>());
		break;
	case Kind::CaseClause:
	case Kind::DefaultClause:
		node = visitTopLevelNestedCaseOrDefaultClause(
			node->as<CaseOrDefaultClause>());
		break;
	case Kind::TryStatement:
		node = visitTopLevelNestedTryStatement(node->as<TryStatement>());
		break;
	case Kind::CatchClause:
		node = visitTopLevelNestedCatchClause(node->as<CatchClause>());
		break;
	case Kind::Block:
		node = visitTopLevelNestedBlock(node->as<Block>());
		break;
	default:
		node = visitNoStack(node, false /*resultIsDiscarded*/);
		break;
	}
	return node;
}

// Visits source elements that are not top-level or top-level nested
// statements. — commonjsmodule.go:127
Node* CommonJSModuleTransformer::visit(Node* node) {
	ancestorGuard guard(this, node);
	return visitNoStack(node, false /*resultIsDiscarded*/);
}

// Visits source elements that are not top-level or top-level nested statements
// without ancestor tracking. — commonjsmodule.go:135
Node* CommonJSModuleTransformer::visitNoStack(Node* node,
                                              bool resultIsDiscarded) {
	// This visitor does not need to descend into the tree if there are no
	// dynamic imports or identifiers in the subtree
	if (!isSourceFile(node) &&
	    (node->subtreeFacts() &
	     (SubtreeContainsDynamicImport | SubtreeContainsIdentifier)) == 0) {
		return node;
	}

	switch (node->kind) {
	case Kind::SourceFile:
		node = visitSourceFile(node->as<SourceFile>())->asNode();
		break;
	case Kind::ForStatement:
		node = visitForStatement(node->as<ForStatement>());
		break;
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		node = visitForInOrOfStatement(node->as<ForInOrOfStatement>());
		break;
	case Kind::ExpressionStatement:
		node = visitExpressionStatement(node->as<ExpressionStatement>());
		break;
	case Kind::VoidExpression:
		node = visitVoidExpression(node->as<VoidExpression>());
		break;
	case Kind::ParenthesizedExpression:
		node = visitParenthesizedExpression(
			node->as<ParenthesizedExpression>(), resultIsDiscarded);
		break;
	case Kind::PartiallyEmittedExpression:
		node = visitPartiallyEmittedExpression(
			node->as<PartiallyEmittedExpression>(), resultIsDiscarded);
		break;
	case Kind::CallExpression:
		node = visitCallExpression(node->as<CallExpression>());
		break;
	case Kind::TaggedTemplateExpression:
		node = visitTaggedTemplateExpression(
			node->as<TaggedTemplateExpression>());
		break;
	case Kind::BinaryExpression:
		node = visitBinaryExpression(node->as<BinaryExpression>(),
		                             resultIsDiscarded);
		break;
	case Kind::PrefixUnaryExpression:
		node = visitPrefixUnaryExpression(node->as<PrefixUnaryExpression>(),
		                                  resultIsDiscarded);
		break;
	case Kind::PostfixUnaryExpression:
		node = visitPostfixUnaryExpression(node->as<PostfixUnaryExpression>(),
		                                   resultIsDiscarded);
		break;
	case Kind::ShorthandPropertyAssignment:
		node = visitShorthandPropertyAssignment(
			node->as<ShorthandPropertyAssignment>());
		break;
	case Kind::Identifier:
		node = visitIdentifier(node);
		break;
	default:
		node = visitor()->visitEachChild(node);
		break;
	}

	return node;
}

// Visits source elements whose value is discarded if they are expressions.
// — commonjsmodule.go:179
Node* CommonJSModuleTransformer::visitDiscardedValue(Node* node) {
	ancestorGuard guard(this, node);
	return visitNoStack(node, true /*resultIsDiscarded*/);
}

// visitAssignmentPattern — commonjsmodule.go:186
Node* CommonJSModuleTransformer::visitAssignmentPattern(Node* node) {
	ancestorGuard guard(this, node);
	return visitAssignmentPatternNoStack(node);
}

// visitAssignmentPatternNoStack — commonjsmodule.go:193
Node* CommonJSModuleTransformer::visitAssignmentPatternNoStack(Node* node) {
	switch (node->kind) {
	// AssignmentPattern
	case Kind::ObjectLiteralExpression:
	case Kind::ArrayLiteralExpression:
		node = assignmentPatternVisitor->visitEachChild(node);
		break;

	// AssignmentProperty
	case Kind::PropertyAssignment:
		node = visitAssignmentProperty(node->as<PropertyAssignment>());
		break;
	case Kind::ShorthandPropertyAssignment:
		node = visitShorthandAssignmentProperty(
			node->as<ShorthandPropertyAssignment>());
		break;

	// AssignmentRestProperty
	case Kind::SpreadAssignment:
		node = visitAssignmentRestProperty(node->as<SpreadAssignment>());
		break;

	// AssignmentRestElement
	case Kind::SpreadElement:
		node = visitAssignmentRestElement(node->as<SpreadElement>());
		break;

	// AssignmentElement
	default:
		if (isExpression(node)) {
			node = visitAssignmentElement(node);
			break;
		}

		node = visitNoStack(node, false /*resultIsDiscarded*/);
		break;
	}
	return node;
}

// visitSourceFile — commonjsmodule.go:226
Node* CommonJSModuleTransformer::visitSourceFile(SourceFile* node) {
	if (node->IsDeclarationFile ||
	    !(isEffectiveExternalModule(node, compilerOptions) ||
	      (node->subtreeFacts() & SubtreeContainsDynamicImport) != 0)) {
		return node->asNode();
	}

	currentSourceFile = node;
	currentModuleInfo = collectExternalModuleInfo(node, compilerOptions,
	                                              emitContext(), resolver);
	SourceFile* updated =
		transformCommonJSModule(node)->as<SourceFile>();
	currentSourceFile = nullptr;
	currentModuleInfo = nullptr;
	return updated->asNode();
}

// shouldEmitUnderscoreUnderscoreESModule — commonjsmodule.go:240
bool CommonJSModuleTransformer::shouldEmitUnderscoreUnderscoreESModule() {
	if (tspath::fileExtensionIsOneOf(currentSourceFile->FileName(),
	                                 tspath::supportedJSExtensionsFlat) &&
	    currentSourceFile->CommonJSModuleIndicator != nullptr &&
	    (currentSourceFile->ExternalModuleIndicator == nullptr ||
	     currentSourceFile->ExternalModuleIndicator->kind ==
	         Kind::SourceFile)) {
		return false;
	}
	if (currentModuleInfo->exportEquals == nullptr &&
	    isExternalModule(currentSourceFile)) {
		return true;
	}
	return false;
}

// createUnderscoreUnderscoreESModule — commonjsmodule.go:252
Node* CommonJSModuleTransformer::createUnderscoreUnderscoreESModule() {
	Node* statement = factory()->newExpressionStatement(
		factory()->newCallExpression(
			factory()->newPropertyAccessExpression(
				factory()->newIdentifier("Object"),
				nullptr /*questionDotToken*/,
				factory()->newIdentifier("defineProperty"), NodeFlagsNone),
			nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
			factory()->newNodeList(
				{factory()->newIdentifier("exports"),
			     factory()->newStringLiteral("__esModule", TokenFlagsNone),
			     factory()->newObjectLiteralExpression(
				     factory()->newNodeList({factory()->newPropertyAssignment(
					     nullptr /*modifiers*/,
					     factory()->newIdentifier("value"),
					     nullptr /*postfixToken*/, nullptr /*typeNode*/,
					     factory()->newTrueExpression())}),
				     false /*multiLine*/)}),
			NodeFlagsNone));
	emitContext()->setEmitFlags(statement, printer::EFCustomPrologue);
	return statement;
}

// transformCommonJSModule — commonjsmodule.go:281
Node* CommonJSModuleTransformer::transformCommonJSModule(SourceFile* node) {
	emitContext()->startVariableEnvironment();

	// emit standard prologue directives (e.g. "use strict")
	auto [prologue, rest0] =
		factory()->splitStandardPrologue(node->Statements->nodes);
	std::vector<Node*> statements = prologue;

	// emit custom prologues from other transformations
	auto [custom, rest] = factory()->splitCustomPrologue(rest0);
	auto visitedCustom = topLevelVisitor->visitSlice(custom).first;
	statements.insert(statements.end(), visitedCustom.begin(),
	                  visitedCustom.end());

	// emits `Object.defineProperty(exports, "__esModule", { value: true });`
	// at the top of the file
	if (shouldEmitUnderscoreUnderscoreESModule()) {
		statements.push_back(createUnderscoreUnderscoreESModule());
	}

	// initialize all exports to `undefined`, e.g.:
	//  exports.a = exports.b = void 0;
	if (!currentModuleInfo->exportedNames.empty()) {
		const size_t chunkSize = 50;
		size_t l = currentModuleInfo->exportedNames.size();
		for (size_t i = 0; i < l; i += chunkSize) {
			Node* right = factory()->newVoidZeroExpression();
			size_t end = std::min(i + chunkSize, l);
			for (size_t j = i; j < end; j++) {
				Node* nextId = currentModuleInfo->exportedNames[j];
				Node* left;
				if (nextId->kind == Kind::StringLiteral) {
					left = factory()->newElementAccessExpression(
						factory()->newIdentifier("exports"),
						nullptr /*questionDotToken*/,
						factory()->newStringLiteralFromNode(nextId),
						NodeFlagsNone);
				} else {
					Node* name = nextId->clone(*factory());
					emitContext()->setEmitFlags(
						name, printer::EFNoSourceMap | printer::EFNoComments);
					left = factory()->newPropertyAccessExpression(
						factory()->newIdentifier("exports"),
						nullptr /*questionDotToken*/, name, NodeFlagsNone);
				}
				right = factory()->newAssignmentExpression(left, right);
			}
			Node* statement = factory()->newExpressionStatement(right);
			emitContext()->addEmitFlags(statement, printer::EFCustomPrologue);
			statements.push_back(statement);
		}
	}

	// initialize exports for function declarations, e.g.:
	//  exports.f = f;
	//  function f() {}
	// These are marked as custom prologue so they are ordered before the
	// external helpers import declaration (e.g., `const tslib_1 =
	// require("tslib")`), matching TypeScript's emit order.
	size_t exportedFunctionsStart = statements.size();
	for (Node* f : currentModuleInfo->exportedFunctions.elements) {
		statements = appendExportsOfClassOrFunctionDeclaration(statements, f);
	}
	for (size_t i = exportedFunctionsStart; i < statements.size(); i++) {
		emitContext()->addEmitFlags(statements[i], printer::EFCustomPrologue);
	}

	// visit the remaining statements in the source file
	auto visitedRest = topLevelVisitor->visitSlice(rest).first;
	statements.insert(statements.end(), visitedRest.begin(),
	                  visitedRest.end());

	// emit `module.exports = ...` if needd
	statements = appendExportEqualsIfNeeded(statements);

	// merge temp variables into the statement list
	statements = emitContext()->endAndMergeVariableEnvironment(statements);

	NodeList* statementList = factory()->newNodeList(statements);
	statementList->loc = node->Statements->loc;
	SourceFile* result =
		factory()
			->updateSourceFile(node, statementList, node->EndOfFileToken)
			->as<SourceFile>();
	for (printer::EmitHelper* helper : emitContext()->readEmitHelpers()) {
		emitContext()->addEmitHelper(result->asNode(), helper);
	}

	Node* externalHelpersImportDeclaration =
		createExternalHelpersImportDeclarationIfNeeded(
			emitContext(), result, compilerOptions,
			getEmitModuleFormatOfFile(node),
			false /*hasExportStarsToExportValues*/, false /*hasImportStar*/,
			false /*hasImportDefault*/);
	if (externalHelpersImportDeclaration != nullptr) {
		auto [prologue2, rest2] =
			factory()->splitStandardPrologue(result->Statements->nodes);
		auto [custom2, rest3] = factory()->splitCustomPrologue(rest2);
		std::vector<Node*> statements2 = prologue2;
		statements2.insert(statements2.end(), custom2.begin(), custom2.end());
		statements2.push_back(
			topLevelVisitor->visitNode(externalHelpersImportDeclaration));
		statements2.insert(statements2.end(), rest3.begin(), rest3.end());
		NodeList* statementList2 = factory()->newNodeList(statements2);
		statementList2->loc = result->Statements->loc;
		result = factory()
		             ->updateSourceFile(result, statementList2,
		                                node->EndOfFileToken)
		             ->as<SourceFile>();
	}

	return result->asNode();
}

// Adds the down-level representation of `export=` to the statement list if one
// exists in the source file.
//
// - The `statements` parameter is a statement list to which the down-level
//   export statements are to be appended.
// — commonjsmodule.go:370
std::vector<Node*> CommonJSModuleTransformer::appendExportEqualsIfNeeded(
	std::vector<Node*> statements) {
	if (currentModuleInfo->exportEquals != nullptr) {
		Node* expressionResult =
			visitExportEquals(currentModuleInfo->exportEquals
			                      ->as<ExportAssignment>());
		if (expressionResult != nullptr) {
			Node* statement = factory()->newExpressionStatement(
				factory()->newAssignmentExpression(
					factory()->newPropertyAccessExpression(
						factory()->newIdentifier("module"),
						nullptr /*questionDotToken*/,
						factory()->newIdentifier("exports"), NodeFlagsNone),
					expressionResult));

			emitContext()->assignCommentAndSourceMapRanges(
				statement, currentModuleInfo->exportEquals);
			emitContext()->addEmitFlags(statement, printer::EFNoComments);
			statements.push_back(statement);
		}
	}
	return statements;
}

// visitExportEquals — commonjsmodule.go:392
Node* CommonJSModuleTransformer::visitExportEquals(ExportAssignment* node) {
	ancestorGuard guard(this, node->asNode());
	return visitor()->visitNode(node->Expression);
}

// Appends the exports of an ImportDeclaration to a statement list, returning
// the statement list.
//
//   - The `statements` parameter is a statement list to which the down-level
//     export statements are to be appended.
//   - The `decl` parameter is the declaration whose exports are to be
//     recorded.
// — commonjsmodule.go:403
std::vector<Node*>
CommonJSModuleTransformer::appendExportsOfImportDeclaration(
	std::vector<Node*> statements, ImportDeclaration* decl) {
	if (currentModuleInfo->exportEquals != nullptr) {
		return statements;
	}

	Node* importClause = decl->ImportClause;
	if (importClause == nullptr) {
		return statements;
	}

	collections::Set<std::string> seen;
	if (importClause->as<ImportClause>()->name != nullptr) {
		statements = appendExportsOfDeclaration(statements, importClause, &seen,
		                                        false /*liveBinding*/);
	}

	Node* namedBindings = importClause->as<ImportClause>()->NamedBindings;
	if (namedBindings != nullptr) {
		switch (namedBindings->kind) {
		case Kind::NamespaceImport:
			statements =
				appendExportsOfDeclaration(statements, namedBindings, &seen,
			                               false /*liveBinding*/);
			break;
		case Kind::NamedImports:
			for (Node* importBinding : namedBindings->elements()) {
				statements = appendExportsOfDeclaration(
					statements, importBinding, &seen, true /*liveBinding*/);
			}
			break;
		default:
			break;
		}
	}

	return statements;
}

// Appends the exports of a VariableStatement to a statement list, returning
// the statement list.
//
//   - The `statements` parameter is a statement list to which the down-level
//     export statements are to be appended.
//   - The `node` parameter is the VariableStatement whose exports are to be
//     recorded.
// — commonjsmodule.go:437
std::vector<Node*>
CommonJSModuleTransformer::appendExportsOfVariableStatement(
	std::vector<Node*> statements, VariableStatement* node) {
	return appendExportsOfVariableDeclarationList(
		statements,
		node->DeclarationList->as<VariableDeclarationList>(),
		false /*isForInOrOfInitializer*/);
}

// Appends the exports of a VariableDeclarationList to a statement list,
// returning the statement list.
//
//   - The `statements` parameter is a statement list to which the down-level
//     export statements are to be appended.
//   - The `node` parameter is the VariableDeclarationList whose exports are to
//     be recorded.
// — commonjsmodule.go:447
std::vector<Node*>
CommonJSModuleTransformer::appendExportsOfVariableDeclarationList(
	std::vector<Node*> statements, VariableDeclarationList* node,
	bool isForInOrOfInitializer) {
	if (currentModuleInfo->exportEquals != nullptr) {
		return statements;
	}

	for (Node* decl : node->Declarations->nodes) {
		statements = appendExportsOfBindingElement(statements, decl,
		                                           isForInOrOfInitializer);
	}

	return statements;
}

// Appends the exports of a VariableDeclaration or BindingElement to a
// statement list, returning the statement list.
//
//   - The `statements` parameter is a statement list to which the down-level
//     export statements are to be appended.
//   - The `decl` parameter is the declaration whose exports are to be
//     recorded.
// — commonjsmodule.go:466
std::vector<Node*> CommonJSModuleTransformer::appendExportsOfBindingElement(
	std::vector<Node*> statements, Node* decl,
	bool isForInOrOfInitializer) {
	if (currentModuleInfo->exportEquals != nullptr || decl->name() == nullptr) {
		return statements;
	}

	if (isBindingPattern(decl->name())) {
		for (Node* element : decl->name()->elements()) {
			if (!isOmittedExpression(element)) {
				statements =
					appendExportsOfBindingElement(statements, element,
					                              isForInOrOfInitializer);
			}
		}
	} else if (!transformers::isGeneratedIdentifier(emitContext(),
	                                                decl->name()) &&
	           (!isVariableDeclaration(decl) ||
	            decl->initializer() != nullptr || isForInOrOfInitializer)) {
		statements = appendExportsOfDeclaration(statements, decl,
		                                        nullptr /*seen*/,
		                                        false /*liveBinding*/);
	}

	return statements;
}

// Appends the exports of a ClassDeclaration or FunctionDeclaration to a
// statement list, returning the statement list.
//
//   - The `statements` parameter is a statement list to which the down-level
//     export statements are to be appended.
//   - The `decl` parameter is the declaration whose exports are to be
//     recorded.
// — commonjsmodule.go:500
std::vector<Node*>
CommonJSModuleTransformer::appendExportsOfClassOrFunctionDeclaration(
	std::vector<Node*> statements, Node* decl) {
	if (currentModuleInfo->exportEquals != nullptr) {
		return statements;
	}

	collections::Set<std::string> seen;
	if (hasSyntacticModifier(decl, ModifierFlagsExport)) {
		Node* exportName;
		if (hasSyntacticModifier(decl, ModifierFlagsDefault)) {
			exportName = factory()->newIdentifier("default");
		} else {
			exportName = factory()->getDeclarationName(decl);
		}

		Node* exportValue = factory()->getLocalName(decl);
		statements =
			appendExportStatement(statements, &seen, exportName, exportValue,
			                      &decl->loc, false /*allowComments*/,
			                      false /*liveBinding*/);
	}

	if (decl->name() != nullptr) {
		return appendExportsOfDeclaration(statements, decl, &seen,
		                                false /*liveBinding*/);
	}

	return statements;
}

// Appends the exports of a declaration to a statement list, returning the
// statement list.
//
//   - The `statements` parameter is a statement list to which the down-level
//     export statements are to be appended.
//   - The `decl` parameter is the declaration to export.
// — commonjsmodule.go:528
std::vector<Node*> CommonJSModuleTransformer::appendExportsOfDeclaration(
	std::vector<Node*> statements, Node* decl,
	collections::Set<std::string>* seen, bool liveBinding) {
	if (currentModuleInfo->exportEquals != nullptr) {
		return statements;
	}

	collections::Set<std::string> seenStorage;
	if (seen == nullptr) {
		seen = &seenStorage;
	}

	Node* name = decl->name();
	if (currentModuleInfo->exportSpecifiers.Len() > 0 && name != nullptr &&
	    isIdentifier(name)) {
		name = factory()->getDeclarationName(decl);
		std::vector<Node*> exportSpecifiers =
			currentModuleInfo->exportSpecifiers.Get(name->text());
		if (!exportSpecifiers.empty()) {
			Node* exportValue = visitExpressionIdentifier(name);
			for (Node* exportSpecifier : exportSpecifiers) {
				statements = appendExportStatement(
					statements, seen, exportSpecifier->name(), exportValue,
					&exportSpecifier->name()->loc /*location*/,
					false /*allowComments*/, liveBinding);
			}
		}
	}

	return statements;
}

// Appends the down-level representation of an export to a statement list,
// returning the statement list.
//
//   - The `statements` parameter is a statement list to which the down-level
//     export statements are to be appended.
//   - The `exportName` parameter is the name of the export.
//   - The `expression` parameter is the expression to export.
//   - The `location` parameter is the location to use for source maps and
//     comments for the export.
//   - The `allowComments` parameter indicates whether to allow comments on the
//     export.
// — commonjsmodule.go:557
std::vector<Node*> CommonJSModuleTransformer::appendExportStatement(
	std::vector<Node*> statements, collections::Set<std::string>* seen,
	Node* exportName, Node* expression, TextRange* location,
	bool allowComments, bool liveBinding) {
	if (exportName->kind != Kind::StringLiteral) {
		if (seen->Has(exportName->text())) {
			return statements;
		}
		seen->Add(exportName->text());
	}
	statements.push_back(createExportStatement(exportName, expression,
	                                           location, allowComments,
	                                           liveBinding));
	return statements;
}

// Creates a call to the current file's export function to export a value.
//
//   - The `name` parameter is the bound name of the export.
//   - The `value` parameter is the exported value.
//   - The `location` parameter is the location to use for source maps and
//     comments for the export.
//   - The `allowComments` parameter indicates whether to emit comments for the
//     statement.
// — commonjsmodule.go:572
Node* CommonJSModuleTransformer::createExportStatement(
	Node* name, Node* value, TextRange* location, bool allowComments,
	bool liveBinding) {
	Node* statement = factory()->newExpressionStatement(createExportExpression(
		name, value, nullptr /*location*/, liveBinding));
	if (location != nullptr) {
		emitContext()->setCommentRange(statement, *location);
	}
	emitContext()->addEmitFlags(statement, printer::EFStartOnNewLine);
	if (!allowComments) {
		emitContext()->addEmitFlags(statement, printer::EFNoComments);
	}
	return statement;
}

// Creates a call to the current file's export function to export a value.
//
//   - The `name` parameter is the bound name of the export.
//   - The `value` parameter is the exported value.
//   - The `location` parameter is the location to use for source maps and
//     comments for the export.
// — commonjsmodule.go:588
Node* CommonJSModuleTransformer::createExportExpression(
	Node* name, Node* value, TextRange* location, bool liveBinding) {
	Node* expression;
	if (liveBinding) {
		// For a live binding we emit a getter on `exports` that returns the
		// value:
		//  Object.defineProperty(exports, "<name>", { enumerable: true, get:
		//  function () { return <value>; } });
		Node* enumerableProp = factory()->newPropertyAssignment(
			nullptr /*modifiers*/, factory()->newIdentifier("enumerable"),
			nullptr /*postfixToken*/, nullptr /*typeNode*/,
			factory()->newTrueExpression());
		Node* getter = factory()->newFunctionExpression(
			nullptr /*modifiers*/, nullptr /*asteriskToken*/, nullptr /*name*/,
			nullptr /*typeParameters*/, factory()->newNodeList({}),
			nullptr /*type*/, nullptr /*fullSignature*/,
			factory()->newBlock(
				factory()->newNodeList(
					{factory()->newReturnStatement(value)}),
				false /*multiLine*/));
		Node* getProp = factory()->newPropertyAssignment(
			nullptr /*modifiers*/, factory()->newIdentifier("get"),
			nullptr /*postfixToken*/, nullptr /*typeNode*/, getter);
		Node* descriptor = factory()->newObjectLiteralExpression(
			factory()->newNodeList({enumerableProp, getProp}),
			false /*multiLine*/);
		expression = factory()->newCallExpression(
			factory()->newPropertyAccessExpression(
				factory()->newIdentifier("Object"),
				nullptr /*questionDotToken*/,
				factory()->newIdentifier("defineProperty"), NodeFlagsNone),
			nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
			factory()->newNodeList(
				{factory()->newIdentifier("exports"),
			     factory()->newStringLiteralFromNode(name), descriptor}),
			NodeFlagsNone);
	} else {
		// Otherwise, we emit a simple property assignment.
		Node* left;
		if (name->kind == Kind::StringLiteral) {
			// emits:
			//  exports["<name>"] = <value>;
			left = factory()->newElementAccessExpression(
				factory()->newIdentifier("exports"),
				nullptr /*questionDotToken*/,
				factory()->newStringLiteralFromNode(name), NodeFlagsNone);
		} else {
			// emits:
			//  exports.<name> = <value>;
			left = factory()->newPropertyAccessExpression(
				factory()->newIdentifier("exports"),
				nullptr /*questionDotToken*/, name->clone(*factory()),
				NodeFlagsNone);
		}
		expression = factory()->newAssignmentExpression(left, value);
	}
	if (location != nullptr) {
		emitContext()->setCommentRange(expression, *location);
	}
	return expression;
}

// Creates a `require()` call to import an external module.
// — commonjsmodule.go:661
Node* CommonJSModuleTransformer::createRequireCall(Node* node) {
	std::vector<Node*> args;
	Node* moduleName = getExternalModuleNameLiteral(
		factory(), node, currentSourceFile, nullptr /*host*/,
		nullptr /*resolver*/, compilerOptions);
	if (moduleName != nullptr) {
		args.push_back(rewriteModuleSpecifier(emitContext(), moduleName,
		                                      compilerOptions));
	}
	return factory()->newCallExpression(factory()->newIdentifier("require"),
	                                    nullptr /*questionDotToken*/,
	                                    nullptr /*typeArguments*/,
	                                    factory()->newNodeList(args),
	                                    NodeFlagsNone);
}

// getHelperExpressionForExport — commonjsmodule.go:675
Node* CommonJSModuleTransformer::getHelperExpressionForExport(
	ExportDeclaration* node, Node* innerExpr) {
	if (getExportNeedsImportStarHelper(node)) {
		return visitor()->visitNode(factory()->newImportStarHelper(innerExpr));
	}
	return innerExpr;
}

// getHelperExpressionForImport — commonjsmodule.go:682
Node* CommonJSModuleTransformer::getHelperExpressionForImport(
	ImportDeclaration* node, Node* innerExpr) {
	if (getImportNeedsImportStarHelper(node)) {
		return visitor()->visitNode(factory()->newImportStarHelper(innerExpr));
	}
	if (getImportNeedsImportDefaultHelper(node)) {
		return visitor()->visitNode(
			factory()->newImportDefaultHelper(innerExpr));
	}
	return innerExpr;
}

// visitTopLevelImportDeclaration — commonjsmodule.go:690
Node* CommonJSModuleTransformer::visitTopLevelImportDeclaration(
	ImportDeclaration* node) {
	if (node->ImportClause == nullptr) {
		// import "mod";
		Node* statement = factory()->newExpressionStatement(
			createRequireCall(node->asNode()));
		emitContext()->setOriginal(statement, node->asNode());
		emitContext()->assignCommentAndSourceMapRanges(statement,
		                                             node->asNode());
		return statement;
	}

	std::vector<Node*> statements;
	std::vector<Node*> variables;
	Node* namespaceDeclaration =
		getNamespaceDeclarationNode(node->asNode());
	if (namespaceDeclaration != nullptr &&
	    !isDefaultImport(node->asNode())) {
		// import * as n from "mod";
		Node* initializer = getHelperExpressionForImport(
			node, createRequireCall(node->asNode()));
		variables.push_back(factory()->newVariableDeclaration(
			namespaceDeclaration->name()->clone(*factory()),
			nullptr /*exclamationToken*/, nullptr /*type*/, initializer));
	} else {
		// import d from "mod";
		// import { x, y } from "mod";
		// import d, { x, y } from "mod";
		// import d, * as n from "mod";
		Node* initializer = getHelperExpressionForImport(
			node, createRequireCall(node->asNode()));
		variables.push_back(factory()->newVariableDeclaration(
			factory()->newGeneratedNameForNode(node->asNode()),
			nullptr /*exclamationToken*/, nullptr /*type*/, initializer));

		if (namespaceDeclaration != nullptr &&
		    isDefaultImport(node->asNode())) {
			variables.push_back(factory()->newVariableDeclaration(
				namespaceDeclaration->name()->clone(*factory()),
				nullptr /*exclamationToken*/, nullptr /*type*/,
				factory()->newGeneratedNameForNode(node->asNode())));
		}
	}

	Node* varStatement = factory()->newVariableStatement(
		nullptr /*modifiers*/,
		factory()->newVariableDeclarationList(factory()->newNodeList(variables),
		                                      NodeFlagsConst));

	emitContext()->setOriginal(varStatement, node->asNode());
	emitContext()->assignCommentAndSourceMapRanges(varStatement,
	                                             node->asNode());
	statements.push_back(varStatement);
	statements = appendExportsOfImportDeclaration(statements, node);
	return transformers::singleOrMany(statements, factory());
}

// visitTopLevelImportEqualsDeclaration — commonjsmodule.go:764
Node* CommonJSModuleTransformer::visitTopLevelImportEqualsDeclaration(
	ImportEqualsDeclaration* node) {
	if (!isExternalModuleImportEqualsDeclaration(node->asNode())) {
		// import m = n;
		TSC_UNREACHABLE(
			"import= for internal module references should be handled in an "
			"earlier transformer.");
	}

	std::vector<Node*> statements;
	if (hasSyntacticModifier(node->asNode(), ModifierFlagsExport)) {
		// export import m = require("mod");
		Node* requireCall = createRequireCall(node->asNode());
		Node* statement = factory()->newExpressionStatement(
			createExportExpression(node->name, requireCall, &node->loc,
		                           false /*liveBinding*/));

		emitContext()->setOriginal(statement, node->asNode());
		emitContext()->assignCommentAndSourceMapRanges(statement,
		                                             node->asNode());
		statements.push_back(statement);
	} else {
		// import m = require("mod");
		Node* requireCall = createRequireCall(node->asNode());
		Node* statement = factory()->newVariableStatement(
			nullptr /*modifiers*/,
			factory()->newVariableDeclarationList(
				factory()->newNodeList({factory()->newVariableDeclaration(
					node->name->clone(*factory()),
					nullptr /*exclamationToken*/, nullptr /*typeNode*/,
					requireCall)}),
				NodeFlagsConst));
		emitContext()->setOriginal(statement, node->asNode());
		emitContext()->assignCommentAndSourceMapRanges(statement,
		                                             node->asNode());
		statements.push_back(statement);
	}

	statements = appendExportsOfDeclaration(statements, node->asNode(),
	                                        nullptr /*seen*/,
	                                        false /*liveBinding*/);
	return transformers::singleOrMany(statements, factory());
}

// visitTopLevelExportDeclaration — commonjsmodule.go:807
Node* CommonJSModuleTransformer::visitTopLevelExportDeclaration(
	ExportDeclaration* node) {
	if (node->ModuleSpecifier == nullptr) {
		// Elide export declarations with no module specifier as they are
		// handled elsewhere.
		return nullptr;
	}

	Node* generatedName = factory()->newGeneratedNameForNode(node->asNode());
	if (node->ExportClause != nullptr &&
	    isNamedExports(node->ExportClause)) {
		// export { x, y } from "mod";
		std::vector<Node*> statements;
		Node* requireCall = createRequireCall(node->asNode());
		Node* varStatement = factory()->newVariableStatement(
			nullptr /*modifiers*/,
			factory()->newVariableDeclarationList(
				factory()->newNodeList({factory()->newVariableDeclaration(
					generatedName, nullptr /*exclamationToken*/,
					nullptr /*type*/, requireCall)}),
				NodeFlagsNone));
		emitContext()->setOriginal(varStatement, node->asNode());
		emitContext()->assignCommentAndSourceMapRanges(varStatement,
		                                             node->asNode());
		statements.push_back(varStatement);

		for (Node* specifier : node->ExportClause->elements()) {
			Node* specifierName = specifier->propertyNameOrName();
			bool exportNeedsImportDefault =
				moduleExportNameIsDefault(specifierName);

			Node* target;
			if (exportNeedsImportDefault) {
				target = factory()->newImportDefaultHelper(generatedName);
			} else {
				target = generatedName;
			}

			Node* exportName;
			if (isStringLiteral(specifier->name())) {
				exportName =
					factory()->newStringLiteralFromNode(specifier->name());
			} else {
				exportName = factory()->getExportName(specifier);
			}

			Node* exportedValue;
			if (isStringLiteral(specifierName)) {
				exportedValue = factory()->newElementAccessExpression(
					target, nullptr /*questionDotToken*/, specifierName,
					NodeFlagsNone);
			} else {
				exportedValue = factory()->newPropertyAccessExpression(
					target, nullptr /*questionDotToken*/, specifierName,
					NodeFlagsNone);
			}
			Node* statement = factory()->newExpressionStatement(
				createExportExpression(exportName, exportedValue,
				                       nullptr /*location*/,
				                       true /*liveBinding*/));
			emitContext()->setOriginal(statement, specifier);
			emitContext()->assignCommentAndSourceMapRanges(statement,
			                                             specifier);
			statements.push_back(statement);
		}

		return transformers::singleOrMany(statements, factory());
	}

	if (node->ExportClause != nullptr) {
		// export * as ns from "mod";
		// export * as default from "mod";
		Node* exportName;
		if (isStringLiteral(node->ExportClause->name())) {
			exportName = factory()->newStringLiteralFromNode(
				node->ExportClause->name());
		} else {
			exportName = node->ExportClause->name()->clone(*factory());
		}
		Node* requireCall = createRequireCall(node->asNode());
		Node* helperExpr =
			getHelperExpressionForExport(node, requireCall);
		Node* statement = factory()->newExpressionStatement(
			createExportExpression(exportName, helperExpr,
			                       nullptr /*location*/,
			                       false /*liveBinding*/));
		emitContext()->setOriginal(statement, node->asNode());
		emitContext()->assignCommentAndSourceMapRanges(statement,
		                                             node->asNode());
		return statement;
	}

	// export * from "mod";
	Node* requireCall = createRequireCall(node->asNode());
	Node* statement = factory()->newExpressionStatement(
		visitor()->visitNode(factory()->newExportStarHelper(
			requireCall, factory()->newIdentifier("exports"))));
	emitContext()->setOriginal(statement, node->asNode());
	emitContext()->assignCommentAndSourceMapRanges(statement,
	                                             node->asNode());
	return statement;
}

// visitTopLevelExportAssignment — commonjsmodule.go:891
Node* CommonJSModuleTransformer::visitTopLevelExportAssignment(
	ExportAssignment* node) {
	if (node->IsExportEquals) {
		return nullptr;
	}

	return createExportStatement(factory()->newIdentifier("default"),
	                             visitor()->visitNode(node->Expression),
	                             &node->loc /*location*/,
	                             true /*allowComments*/,
	                             false /*liveBinding*/);
}

// visitTopLevelFunctionDeclaration — commonjsmodule.go:905
Node* CommonJSModuleTransformer::visitTopLevelFunctionDeclaration(
	FunctionDeclaration* node) {
	if (hasSyntacticModifier(node->asNode(), ModifierFlagsExport)) {
		// Hoist mutating call args to preserve Go's L→R evaluation order.
		ModifierList* modifiers = transformers::extractModifiers(
			emitContext(), node->modifiers, ~ModifierFlagsExportDefault);
		Node* name = factory()->getDeclarationName(node->asNode());
		NodeList* parameters = visitor()->visitNodes(node->Parameters);
		Node* body = visitor()->visitNode(node->Body);
		return factory()->updateFunctionDeclaration(
			node, modifiers, node->AsteriskToken, name,
			nullptr /*typeParameters*/, parameters, nullptr /*type*/,
			nullptr /*fullSignature*/, body);
	} else {
		return visitor()->visitEachChild(node->asNode());
	}
}

// visitTopLevelClassDeclaration — commonjsmodule.go:922
Node* CommonJSModuleTransformer::visitTopLevelClassDeclaration(
	ClassDeclaration* node) {
	std::vector<Node*> statements;
	if (hasSyntacticModifier(node->asNode(), ModifierFlagsExport)) {
		ModifierList* modifiers = visitor()->visitModifiers(
			transformers::extractModifiers(emitContext(), node->modifiers,
			                             ~ModifierFlagsExportDefault));
		Node* name = factory()->getDeclarationName(node->asNode());
		NodeList* heritageClauses =
			visitor()->visitNodes(node->HeritageClauses);
		NodeList* members = visitor()->visitNodes(node->Members);
		statements.push_back(factory()->updateClassDeclaration(
			node, modifiers, name, nullptr /*typeParameters*/,
			heritageClauses, members));
	} else {
		statements.push_back(visitor()->visitEachChild(node->asNode()));
	}
	statements =
		appendExportsOfClassOrFunctionDeclaration(statements, node->asNode());
	return transformers::singleOrMany(statements, factory());
}

// visitTopLevelVariableStatement — commonjsmodule.go:940
Node* CommonJSModuleTransformer::visitTopLevelVariableStatement(
	VariableStatement* node) {
	std::vector<Node*> statements;
	if (hasSyntacticModifier(node->asNode(), ModifierFlagsExport)) {
		// export var a = b;
		std::vector<Node*> variables;
		std::vector<Node*> expressions;
		ModifierList* modifiers = nullptr;

		auto commitPendingVariables = [&]() {
			if (!variables.empty()) {
				NodeList* variableList = factory()->newNodeList(variables);
				Node* statement = factory()->updateVariableStatement(
					node, modifiers,
					factory()->updateVariableDeclarationList(
						node->DeclarationList
							->as<VariableDeclarationList>(),
						variableList, node->DeclarationList->flags));
				if (!statements.empty()) {
					emitContext()->addEmitFlags(statement,
					                            printer::EFNoComments);
				}
				statements.push_back(statement);
				variables.clear();
			}
		};

		auto commitPendingExpressions = [&]() {
			if (!expressions.empty()) {
				Node* statement = factory()->newExpressionStatement(
					factory()->inlineExpressions(expressions));
				emitContext()->assignCommentAndSourceMapRanges(
					statement, node->asNode());
				if (!statements.empty()) {
					emitContext()->addEmitFlags(statement,
					                            printer::EFNoComments);
				}
				statements.push_back(statement);
				expressions.clear();
			}
		};

		auto pushVariable = [&](Node* variable) {
			commitPendingExpressions();
			variables.push_back(variable);
		};

		auto pushExpression = [&](Node* expression) {
			commitPendingVariables();
			expressions.push_back(expression);
		};

		// If we're exporting these variables, then these just become
		// assignments to 'exports.x'.
		for (Node* variable :
		     node->DeclarationList->as<VariableDeclarationList>()
			     ->Declarations->nodes) {
			auto* v = variable->as<VariableDeclaration>();

			if (isIdentifier(v->name) &&
			    transformers::isLocalName(emitContext(), v->name)) {
				// A "local name" generally means a variable declaration that
				// *shouldn't* be converted to `exports.x = ...`, even if the
				// declaration is exported. This usually indicates a class or
				// function declaration that was converted into a variable
				// declaration, as most references to the declaration will
				// remain untransformed (i.e., `new C` rather than `new
				// exports.C`). In these cases, an `export { x }` declaration
				// will follow.

				if (modifiers == nullptr) {
					modifiers = transformers::extractModifiers(
						emitContext(), node->modifiers,
						~ModifierFlagsExportDefault);
				}

				if (v->Initializer != nullptr) {
					Node* exportExpression = createExportExpression(
						v->name, visitor()->visitNode(v->Initializer),
						nullptr, false /*liveBinding*/);
					variable = factory()->updateVariableDeclaration(
						v, v->name, nullptr /*exclamationToken*/,
						nullptr /*type*/, exportExpression);
				}

				pushVariable(variable);
			} else if (v->Initializer != nullptr &&
			           !isBindingPattern(v->name) &&
			           (isArrowFunction(v->Initializer) ||
			            isFunctionExpression(v->Initializer) ||
			            isClassExpression(v->Initializer))) {
				// preserve variable declarations for functions and classes to
				// assign names

				pushVariable(factory()->newVariableDeclaration(
					v->name, v->ExclamationToken, v->Type,
					visitor()->visitNode(v->Initializer)));

				Node* propertyAccess =
					factory()->newPropertyAccessExpression(
						factory()->newIdentifier("exports"),
						nullptr /*questionDotToken*/, v->name, NodeFlagsNone);
				emitContext()->assignCommentAndSourceMapRanges(propertyAccess,
				                                             v->name);

				pushExpression(factory()->newAssignmentExpression(
					propertyAccess, v->name->clone(*factory())));
			} else if (isIdentifier(v->name)) {
				Node* expression = transformInitializedVariable(v);
				if (expression != nullptr) {
					pushExpression(visitor()->visitNode(expression));
				}
			} else if (isBindingPattern(v->name)) {
				// For binding patterns with export modifier, use
				// flattenDestructuringAssignment to decompose into individual
				// export assignments
				Node* expression = transformInitializedVariable(v);
				if (expression != nullptr) {
					pushExpression(expression);
				}
			} else {
				// For binding patterns, we can't do exports.{pattern} = value
				// Just emit the assignment and let
				// appendExportsOfVariableStatement handle the exports
				Node* expression =
					transformers::convertVariableDeclarationToAssignmentExpression(
						emitContext(), v->asNode());
				if (expression != nullptr) {
					pushExpression(visitor()->visitNode(expression));
				}
			}
		}

		commitPendingVariables();
		commitPendingExpressions();
		statements = appendExportsOfVariableStatement(statements, node);
		return transformers::singleOrMany(statements, factory());
	}
	return visitTopLevelNestedVariableStatement(node);
}

// transformInitializedVariable — commonjsmodule.go:1095
Node* CommonJSModuleTransformer::transformInitializedVariable(
	VariableDeclaration* node) {
	if (node->Initializer == nullptr) {
		return nullptr;
	}
	Node* name = node->name;
	if (isBindingPattern(name)) {
		// Convert the binding pattern into an equivalent assignment
		// expression and visit it as a destructuring assignment. This
		// preserves native destructuring (and therefore iterator semantics for
		// array patterns) whenever each leaf identifier can be substituted to
		// an export reference. Only when the destructuring would assign to
		// re-aliased or multi-exported names (where native destructuring
		// cannot update all targets) does `visitDestructuringAssignment` fall
		// back to flattening.
		Node* assignment =
			transformers::convertVariableDeclarationToAssignmentExpression(
				emitContext(), node->asNode());
		ancestorGuard guard(this, assignment);
		return visitDestructuringAssignment(
			assignment->as<BinaryExpression>(), true /*valueIsDiscarded*/);
	}
	Node* propertyAccess = factory()->newPropertyAccessExpression(
		factory()->newIdentifier("exports"), nullptr /*questionDotToken*/,
		name, NodeFlagsNone);
	emitContext()->assignCommentAndSourceMapRanges(propertyAccess, name);
	return factory()->newAssignmentExpression(propertyAccess,
	                                          node->Initializer);
}

// Visits a top-level nested variable statement as it may contain `var`
// declarations that are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1127
Node* CommonJSModuleTransformer::visitTopLevelNestedVariableStatement(
	VariableStatement* node) {
	std::vector<Node*> statements;
	statements.push_back(visitor()->visitEachChild(node->asNode()));
	statements = appendExportsOfVariableStatement(statements, node);
	return transformers::singleOrMany(statements, factory());
}

// Visits a top-level nested `for` statement as it may contain `var`
// declarations that are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1137
Node* CommonJSModuleTransformer::visitTopLevelNestedForStatement(
	ForStatement* node) {
	if (node->Initializer != nullptr &&
	    isVariableDeclarationList(node->Initializer) &&
	    (node->Initializer->flags & NodeFlagsBlockScoped) == 0) {
		std::vector<Node*> exportStatements =
			appendExportsOfVariableDeclarationList(
				{} /*statements*/,
				node->Initializer->as<VariableDeclarationList>(),
				false /*isForInOrOfInitializer*/);
		if (!exportStatements.empty()) {
			// given:
			//   export { x }
			//   for (var x = 0; ;) { }
			// emits:
			//   var x = 0;
			//   exports.x = x;
			//   for (; ;) { }

			std::vector<Node*> statements;
			Node* varDeclList =
				discardedValueVisitor->visitNode(node->Initializer);
			Node* varStatement = factory()->newVariableStatement(
				nullptr /*modifiers*/, varDeclList);
			statements.push_back(varStatement);
			statements.insert(statements.end(), exportStatements.begin(),
			                  exportStatements.end());

			Node* condition = visitor()->visitNode(node->Condition);
			Node* incrementor =
				discardedValueVisitor->visitNode(node->Incrementor);
			Node* body = emitContext()->visitIterationBody(
				node->Statement, topLevelNestedVisitor);
			statements.push_back(factory()->updateForStatement(
				node, nullptr /*initializer*/, condition, incrementor, body));
			return transformers::singleOrMany(statements, factory());
		}
	}
	Node* initializer = discardedValueVisitor->visitNode(node->Initializer);
	Node* condition = visitor()->visitNode(node->Condition);
	Node* incrementor = discardedValueVisitor->visitNode(node->Incrementor);
	Node* body = emitContext()->visitIterationBody(node->Statement,
	                                             topLevelNestedVisitor);
	return factory()->updateForStatement(node, initializer, condition,
	                                     incrementor, body);
}

// Visits a top-level nested `for..in` or `for..of` statement as it may contain
// `var` declarations that are hoisted and may still be exported with
// `export {}`. — commonjsmodule.go:1178
Node* CommonJSModuleTransformer::visitTopLevelNestedForInOrOfStatement(
	ForInOrOfStatement* node) {
	if (isVariableDeclarationList(node->Initializer) &&
	    (node->Initializer->flags & NodeFlagsBlockScoped) == 0) {
		std::vector<Node*> exportStatements =
			appendExportsOfVariableDeclarationList(
				{} /*statements*/,
				node->Initializer->as<VariableDeclarationList>(),
				true /*isForInOrOfInitializer*/);
		if (!exportStatements.empty()) {
			// given:
			//   export { x }
			//   for (var x in y) {
			//     ...
			//   }
			// emits:
			//   for (var x in y) {
			//     exports.x = x;
			//     ...
			//   }

			Node* initializer =
				discardedValueVisitor->visitNode(node->Initializer);
			Node* expression = visitor()->visitNode(node->Expression);
			Node* body = emitContext()->visitIterationBody(
				node->Statement, topLevelNestedVisitor);
			if (isBlock(body)) {
				auto* block = body->as<Block>();
				std::vector<Node*> bodyStatements = exportStatements;
				bodyStatements.insert(bodyStatements.end(),
				                      block->Statements->nodes.begin(),
				                      block->Statements->nodes.end());
				NodeList* bodyStatementList =
					factory()->newNodeList(bodyStatements);
				bodyStatementList->loc = block->Statements->loc;
				body = factory()->updateBlock(block, bodyStatementList,
				                              block->MultiLine);
			} else {
				std::vector<Node*> bodyStatements = exportStatements;
				bodyStatements.push_back(body);
				body = factory()->newBlock(factory()->newNodeList(bodyStatements),
				                           true /*multiLine*/);
			}
			return factory()->updateForInOrOfStatement(
				node, node->AwaitModifier, initializer, expression, body);
		}
	}
	Node* initializer = discardedValueVisitor->visitNode(node->Initializer);
	Node* expression = visitor()->visitNode(node->Expression);
	Node* body = emitContext()->visitIterationBody(node->Statement,
	                                             topLevelNestedVisitor);
	return factory()->updateForInOrOfStatement(
		node, node->AwaitModifier, initializer, expression, body);
}

// Visits a top-level nested `do` statement as it may contain `var`
// declarations that are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1224
Node* CommonJSModuleTransformer::visitTopLevelNestedDoStatement(
	DoStatement* node) {
	Node* statement = emitContext()->visitIterationBody(
		node->Statement, topLevelNestedVisitor);
	Node* expression = visitor()->visitNode(node->Expression);
	return factory()->updateDoStatement(node, statement, expression);
}

// Visits a top-level nested `while` statement as it may contain `var`
// declarations that are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1234
Node* CommonJSModuleTransformer::visitTopLevelNestedWhileStatement(
	WhileStatement* node) {
	Node* expression = visitor()->visitNode(node->Expression);
	Node* statement = emitContext()->visitIterationBody(
		node->Statement, topLevelNestedVisitor);
	return factory()->updateWhileStatement(node, expression, statement);
}

// Visits a top-level nested labeled statement as it may contain `var`
// declarations that are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1244
Node* CommonJSModuleTransformer::visitTopLevelNestedLabeledStatement(
	LabeledStatement* node) {
	Node* statement =
		topLevelNestedVisitor->visitEmbeddedStatement(node->Statement);
	if (statement == nullptr) {
		statement = factory()->newEmptyStatement();
	}
	return factory()->updateLabeledStatement(node, node->Label, statement);
}

// Visits a top-level nested `with` statement as it may contain `var`
// declarations that are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1255
Node* CommonJSModuleTransformer::visitTopLevelNestedWithStatement(
	WithStatement* node) {
	Node* expression = visitor()->visitNode(node->Expression);
	Node* statement =
		topLevelNestedVisitor->visitEmbeddedStatement(node->Statement);
	if (statement == nullptr) {
		statement = factory()->newEmptyStatement();
	}
	return factory()->updateWithStatement(node, expression, statement);
}

// Visits a top-level nested `if` statement as it may contain `var`
// declarations that are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1265
Node* CommonJSModuleTransformer::visitTopLevelNestedIfStatement(
	IfStatement* node) {
	Node* expression = visitor()->visitNode(node->Expression);
	Node* thenStatement = topLevelNestedVisitor->visitEmbeddedStatement(
		node->ThenStatement);
	if (thenStatement == nullptr) {
		thenStatement =
			factory()->newBlock(factory()->newNodeList({}),
		                        false /*multiLine*/);
	}
	Node* elseStatement = topLevelNestedVisitor->visitEmbeddedStatement(
		node->ElseStatement);
	return factory()->updateIfStatement(node, expression, thenStatement,
	                                    elseStatement);
}

// Visits a top-level nested `switch` statement as it may contain `var`
// declarations that are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1278
Node* CommonJSModuleTransformer::visitTopLevelNestedSwitchStatement(
	SwitchStatement* node) {
	Node* expression = visitor()->visitNode(node->Expression);
	Node* caseBlock =
		topLevelNestedVisitor->visitNode(node->CaseBlock);
	return factory()->updateSwitchStatement(node, expression, caseBlock);
}

// Visits a top-level nested case block as it may contain `var` declarations
// that are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1288
Node* CommonJSModuleTransformer::visitTopLevelNestedCaseBlock(
	CaseBlock* node) {
	return topLevelNestedVisitor->visitEachChild(node->asNode());
}

// Visits a top-level nested `case` or `default` clause as it may contain `var`
// declarations that are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1294
Node* CommonJSModuleTransformer::visitTopLevelNestedCaseOrDefaultClause(
	CaseOrDefaultClause* node) {
	Node* expression = visitor()->visitNode(node->Expression);
	NodeList* statements =
		topLevelNestedVisitor->visitNodes(node->Statements);
	return factory()->updateCaseOrDefaultClause(node, expression, statements);
}

// Visits a top-level nested `try` statement as it may contain `var`
// declarations that are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1304
Node* CommonJSModuleTransformer::visitTopLevelNestedTryStatement(
	TryStatement* node) {
	return topLevelNestedVisitor->visitEachChild(node->asNode());
}

// Visits a top-level nested `catch` clause as it may contain `var`
// declarations that are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1310
Node* CommonJSModuleTransformer::visitTopLevelNestedCatchClause(
	CatchClause* node) {
	Node* block = topLevelNestedVisitor->visitNode(node->Block);
	return factory()->updateCatchClause(node, node->VariableDeclaration,
	                                    block);
}

// Visits a top-level nested block as it may contain `var` declarations that
// are hoisted and may still be exported with `export {}`.
// — commonjsmodule.go:1320
Node* CommonJSModuleTransformer::visitTopLevelNestedBlock(Block* node) {
	return topLevelNestedVisitor->visitEachChild(node->asNode());
}

// visitForStatement — commonjsmodule.go:1326
Node* CommonJSModuleTransformer::visitForStatement(ForStatement* node) {
	Node* initializer = discardedValueVisitor->visitNode(node->Initializer);
	Node* condition = visitor()->visitNode(node->Condition);
	Node* incrementor = discardedValueVisitor->visitNode(node->Incrementor);
	Node* statement = emitContext()->visitIterationBody(
		node->Statement, visitor());
	return factory()->updateForStatement(node, initializer, condition,
	                                     incrementor, statement);
}

// visitForInOrOfStatement — commonjsmodule.go:1336
Node* CommonJSModuleTransformer::visitForInOrOfStatement(
	ForInOrOfStatement* node) {
	Node* initializer = discardedValueVisitor->visitNode(node->Initializer);
	Node* expression = visitor()->visitNode(node->Expression);
	Node* statement = emitContext()->visitIterationBody(
		node->Statement, visitor());
	return factory()->updateForInOrOfStatement(node, node->AwaitModifier,
	                                           initializer, expression,
	                                           statement);
}

// Visits an expression statement whose value will be discarded at runtime.
// — commonjsmodule.go:1348
Node* CommonJSModuleTransformer::visitExpressionStatement(
	ExpressionStatement* node) {
	return discardedValueVisitor->visitEachChild(node->asNode());
}

// Visits a `void` expression whose value will be discarded at runtime.
// — commonjsmodule.go:1354
Node* CommonJSModuleTransformer::visitVoidExpression(VoidExpression* node) {
	return discardedValueVisitor->visitEachChild(node->asNode());
}

// Visits a parenthesized expression whose value may be discarded at runtime.
// — commonjsmodule.go:1360
Node* CommonJSModuleTransformer::visitParenthesizedExpression(
	ParenthesizedExpression* node, bool resultIsDiscarded) {
	Node* expression =
		(resultIsDiscarded ? (NodeVisitor*)discardedValueVisitor
		                   : (NodeVisitor*)visitor())
			->visitNode(node->Expression);
	return factory()->updateParenthesizedExpression(node, expression);
}

// Visits a partially emitted expression whose value may be discarded at
// runtime. — commonjsmodule.go:1367
Node* CommonJSModuleTransformer::visitPartiallyEmittedExpression(
	PartiallyEmittedExpression* node, bool resultIsDiscarded) {
	Node* expression =
		(resultIsDiscarded ? (NodeVisitor*)discardedValueVisitor
		                   : (NodeVisitor*)visitor())
			->visitNode(node->Expression);
	return factory()->updatePartiallyEmittedExpression(node, expression);
}

// Visits a binary expression whose value may be discarded, or which might
// contain an assignment to an exported identifier. — commonjsmodule.go:1374
Node* CommonJSModuleTransformer::visitBinaryExpression(
	BinaryExpression* node, bool resultIsDiscarded) {
	if (isDestructuringAssignment(node->asNode())) {
		return visitDestructuringAssignment(node, resultIsDiscarded);
	}

	if (isAssignmentExpression(node->asNode(),
	                           false /*excludeCompoundAssignment*/)) {
		return visitAssignmentExpression(node);
	}

	if (isCommaExpression(node->asNode())) {
		return visitCommaExpression(node, resultIsDiscarded);
	}

	return visitor()->visitEachChild(node->asNode());
}

// visitAssignmentExpression — commonjsmodule.go:1391
Node* CommonJSModuleTransformer::visitAssignmentExpression(
	BinaryExpression* node) {
	// When we see an assignment expression whose left-hand side is an exported
	// symbol, we should ensure all exports of that symbol are updated with the
	// correct value.
	//
	// - We do not transform generated identifiers unless they are file-level
	//   reserved names.
	// - We do not transform identifiers tagged with the LocalName flag.
	// - We only transform identifiers that are exported at the top level.
	if (isIdentifier(node->Left) &&
	    (!transformers::isGeneratedIdentifier(emitContext(), node->Left) ||
	     isFileLevelReservedGeneratedIdentifier(emitContext(), node->Left)) &&
	    !transformers::isLocalName(emitContext(), node->Left)) {
		std::vector<Node*> exportedNames = getExports(node->Left);
		if (!exportedNames.empty()) {
			// For each additional export of the declaration, apply an export
			// assignment.
			Node* expression = visitor()->visitEachChild(node->asNode());
			for (Node* exportName : exportedNames) {
				expression = createExportExpression(
					exportName, expression, &node->loc /*location*/,
					false /*liveBinding*/);
			}
			return expression;
		}
	}

	return visitor()->visitEachChild(node->asNode());
}

// Visits a destructuring assignment which might target an exported
// identifier. — commonjsmodule.go:1421
Node* CommonJSModuleTransformer::visitDestructuringAssignment(
	BinaryExpression* node, bool valueIsDiscarded) {
	if (destructuringNeedsFlattening(node->Left)) {
		return transformers::flattenDestructuringAssignment(
			this, node->asNode(),
			!valueIsDiscarded /*needsValue*/, FlattenLevel::All,
			[this](Node* name, Node* value, TextRange* location) {
				return createAllExportExpressions(name, value, location);
			});
	}
	return visitor()->visitEachChild(node->asNode());
}

// destructuringNeedsFlattening checks whether a destructuring assignment
// target contains any exported identifiers that need to be flattened into
// individual export assignments. — commonjsmodule.go:1435
bool CommonJSModuleTransformer::destructuringNeedsFlattening(Node* node) {
	if (isObjectLiteralExpression(node)) {
		for (Node* elem : node->properties()) {
			switch (elem->kind) {
			case Kind::PropertyAssignment:
				if (destructuringNeedsFlattening(elem->initializer())) {
					return true;
				}
				break;
			case Kind::ShorthandPropertyAssignment:
				if (destructuringNeedsFlattening(elem->name())) {
					return true;
				}
				break;
			case Kind::SpreadAssignment:
				if (destructuringNeedsFlattening(elem->expression())) {
					return true;
				}
				break;
			case Kind::MethodDeclaration:
			case Kind::GetAccessor:
			case Kind::SetAccessor:
				return false;
			default:
				break;
			}
		}
	} else if (isArrayLiteralExpression(node)) {
		for (Node* elem : node->as<ArrayLiteralExpression>()->Elements->nodes) {
			if (isSpreadElement(elem)) {
				if (destructuringNeedsFlattening(elem->expression())) {
					return true;
				}
			} else if (destructuringNeedsFlattening(elem)) {
				return true;
			}
		}
	} else if (isIdentifier(node)) {
		std::vector<Node*> exportedNames = getExports(node);
		if (transformers::isExportName(emitContext(), node)) {
			// The identifier is already wrapped to be an export reference;
			// tolerate up to one matching export.
			return exportedNames.size() > 1;
		}
		if (exportedNames.empty()) {
			return false;
		}
		// A single direct export whose export name matches the identifier
		// text can be handled natively: substitution will rewrite the
		// identifier to `exports.X`, so no flattening is needed. Re-aliased
		// exports (where the export name differs from the local name) or
		// multi-exported names cannot be expressed natively in a
		// destructuring assignment.
		if (exportedNames.size() == 1 && isDirectExport(node) &&
		    exportedNames[0]->text() == node->text()) {
			return false;
		}
		return true;
	}
	return false;
}

// createAllExportExpressions is the callback used during destructuring
// flattening to create export expressions for each exported identifier
// binding. — commonjsmodule.go:1481
Node* CommonJSModuleTransformer::createAllExportExpressions(
	Node* name, Node* value, TextRange* location) {
	std::vector<Node*> exportedNames = getExports(name);
	if (!exportedNames.empty()) {
		// If the name is directly exported (i.e., `export let x`), assign to
		// exports.name directly. Otherwise, assign to the local binding first
		// (i.e., `let x; export { x }`).
		Node* expression;
		if (isDirectExport(name)) {
			// Create exports.name = value to handle the direct export
			// assignment, since the Go port doesn't have an onSubstituteNode
			// mechanism to rewrite identifiers.
			Node* exportName = name->clone(*factory());
			emitContext()->addEmitFlags(
				exportName, printer::EFNoComments | printer::EFNoSourceMap);
			Node* propertyAccess =
				factory()->newPropertyAccessExpression(
					factory()->newIdentifier("exports"),
					nullptr /*questionDotToken*/, exportName, NodeFlagsNone);
			emitContext()->addEmitFlags(propertyAccess,
			                            printer::EFNoComments);
			expression =
				factory()->newAssignmentExpression(propertyAccess, value);
			emitContext()->assignCommentAndSourceMapRanges(expression, name);
		} else {
			expression = factory()->newAssignmentExpression(name, value);
		}
		for (Node* exportName : exportedNames) {
			expression =
				createExportExpression(exportName, expression, location,
			                           false /*liveBinding*/);
		}
		return expression;
	}
	// If the identifier is directly exported but has no additional export
	// aliases, still write to exports.name.
	if (isDirectExport(name)) {
		Node* exportName = name->clone(*factory());
		emitContext()->addEmitFlags(
			exportName, printer::EFNoComments | printer::EFNoSourceMap);
		Node* propertyAccess = factory()->newPropertyAccessExpression(
			factory()->newIdentifier("exports"), nullptr /*questionDotToken*/,
			exportName, NodeFlagsNone);
		emitContext()->addEmitFlags(propertyAccess, printer::EFNoComments);
		Node* result =
			factory()->newAssignmentExpression(propertyAccess, value);
		emitContext()->assignCommentAndSourceMapRanges(result, name);
		return result;
	}
	return factory()->newAssignmentExpression(name, value);
}

// isDirectExport checks whether the identifier is directly exported from the
// source file (e.g., `export let x` or `export function f()`), as opposed to
// being re-exported via `export { x }` for a locally-declared variable.
// — commonjsmodule.go:1537
bool CommonJSModuleTransformer::isDirectExport(Node* name) {
	Node* exportContainer = resolver->GetReferencedExportContainer(
		emitContext()->mostOriginal(name), false /*prefixLocals*/);
	return exportContainer != nullptr && isSourceFile(exportContainer);
}

// visitAssignmentProperty — commonjsmodule.go:1543
Node* CommonJSModuleTransformer::visitAssignmentProperty(
	PropertyAssignment* node) {
	Node* name = visitor()->visitNode(node->name);
	Node* initializer =
		assignmentPatternVisitor->visitNode(node->Initializer);
	return factory()->updatePropertyAssignment(
		node, nullptr /*modifiers*/, name, nullptr /*postfixToken*/,
		nullptr /*typeNode*/, initializer);
}

// visitShorthandAssignmentProperty — commonjsmodule.go:1555
Node* CommonJSModuleTransformer::visitShorthandAssignmentProperty(
	ShorthandPropertyAssignment* node) {
	Node* target = visitDestructuringAssignmentTargetNoStack(node->name);
	if (isIdentifier(target)) {
		Node* objectAssignmentInitializer =
			visitor()->visitNode(node->ObjectAssignmentInitializer);
		return factory()->updateShorthandPropertyAssignment(
			node, nullptr /*modifiers*/, target, nullptr /*postfixToken*/,
			nullptr /*typeNode*/, node->EqualsToken,
			objectAssignmentInitializer);
	}
	if (node->ObjectAssignmentInitializer != nullptr) {
		Node* equalsToken = node->EqualsToken;
		if (equalsToken == nullptr) {
			equalsToken = factory()->newToken(Kind::EqualsToken);
		}
		Node* initializer =
			visitor()->visitNode(node->ObjectAssignmentInitializer);
		target = factory()->newBinaryExpression(
			nullptr /*modifiers*/, target, nullptr /*typeNode*/, equalsToken,
			initializer);
	}
	Node* updated = factory()->newPropertyAssignment(
		nullptr /*modifiers*/, node->name, nullptr /*postfixToken*/,
		nullptr /*typeNode*/, target);
	emitContext()->setOriginal(updated, node->asNode());
	emitContext()->assignCommentAndSourceMapRanges(updated, node->asNode());
	return updated;
}

// visitAssignmentRestProperty — commonjsmodule.go:1585
Node* CommonJSModuleTransformer::visitAssignmentRestProperty(
	SpreadAssignment* node) {
	return factory()->updateSpreadAssignment(
		node, visitDestructuringAssignmentTarget(node->Expression));
}

// visitAssignmentRestElement — commonjsmodule.go:1592
Node* CommonJSModuleTransformer::visitAssignmentRestElement(
	SpreadElement* node) {
	return factory()->updateSpreadElement(
		node, visitDestructuringAssignmentTarget(node->Expression));
}

// visitAssignmentElement — commonjsmodule.go:1599
Node* CommonJSModuleTransformer::visitAssignmentElement(Node* node) {
	if (isBinaryExpression(node)) {
		auto* n = node->as<BinaryExpression>();
		if (n->OperatorToken->kind == Kind::EqualsToken) {
			Node* left = visitDestructuringAssignmentTarget(n->Left);
			Node* right = visitor()->visitNode(n->Right);
			return factory()->updateBinaryExpression(
				n, nullptr /*modifiers*/, left, nullptr /*typeNode*/,
				n->OperatorToken, right);
		}
	}

	return visitDestructuringAssignmentTargetNoStack(node);
}

// visitDestructuringAssignmentTarget — commonjsmodule.go:1616
Node* CommonJSModuleTransformer::visitDestructuringAssignmentTarget(
	Node* node) {
	ancestorGuard guard(this, node);

	switch (node->kind) {
	case Kind::ObjectLiteralExpression:
	case Kind::ArrayLiteralExpression:
		node = visitAssignmentPatternNoStack(node);
		break;
	default:
		node = visitDestructuringAssignmentTargetNoStack(node);
		break;
	}
	return node;
}

// visitDestructuringAssignmentTargetNoStack — commonjsmodule.go:1631
Node* CommonJSModuleTransformer::visitDestructuringAssignmentTargetNoStack(
	Node* node) {
	if (isIdentifier(node) &&
	    (!transformers::isGeneratedIdentifier(emitContext(), node) ||
	     isFileLevelReservedGeneratedIdentifier(emitContext(), node)) &&
	    !transformers::isLocalName(emitContext(), node)) {
		Node* expression = visitExpressionIdentifier(node);
		std::vector<Node*> exportedNames = getExports(node);
		if (!exportedNames.empty()) {
			// transforms:
			//  var x;
			//  export { x }
			//  { x: x } = y
			// to:
			//  { x: { set value(v) { exports.x = x = v; } }.value } = y

			Node* value = factory()->newUniqueName(
				"value",
				printer::AutoGenerateOptions{
					printer::GeneratedIdentifierFlagsOptimistic, "", ""});
			expression =
				factory()->newAssignmentExpression(expression, value);

			for (Node* exportName : exportedNames) {
				expression = createExportExpression(
					exportName, expression, nullptr /*location*/,
					false /*liveBinding*/);
			}

			Node* statement = factory()->newExpressionStatement(expression);
			NodeList* statementList =
				factory()->newNodeList({statement});
			Node* param = factory()->newParameterDeclaration(
				nullptr /*modifiers*/, nullptr /*dotDotDotToken*/, value,
				nullptr /*questionToken*/, nullptr /*type*/,
				nullptr /*initializer*/);
			Node* valueSetter = factory()->newSetAccessorDeclaration(
				nullptr /*modifiers*/, factory()->newIdentifier("value"),
				nullptr /*typeParameters*/,
				factory()->newNodeList({param}), nullptr /*returnType*/,
				nullptr /*fullSignature*/,
				factory()->newBlock(statementList, false /*multiLine*/));
			NodeList* propertyList =
				factory()->newNodeList({valueSetter});
			expression = factory()->newObjectLiteralExpression(
				propertyList, false /*multiLine*/);
			expression = factory()->newPropertyAccessExpression(
				expression, nullptr /*questionDotToken*/,
				factory()->newIdentifier("value"), NodeFlagsNone);
		}
		return expression;
	}

	return visitNoStack(node, false /*resultIsDiscarded*/);
}

// Visits a comma expression whose left-hand value is always discard, and whose
// right-hand value may be discarded at runtime. — commonjsmodule.go:1687
Node* CommonJSModuleTransformer::visitCommaExpression(
	BinaryExpression* node, bool resultIsDiscarded) {
	Node* left = discardedValueVisitor->visitNode(node->Left);
	Node* right =
		(resultIsDiscarded ? (NodeVisitor*)discardedValueVisitor
		                   : (NodeVisitor*)visitor())
			->visitNode(node->Right);
	return factory()->updateBinaryExpression(
		node, nullptr /*modifiers*/, left, nullptr /*typeNode*/,
		node->OperatorToken, right);
}

// Visits a prefix unary expression that might modify an exported identifier.
// — commonjsmodule.go:1694
Node* CommonJSModuleTransformer::visitPrefixUnaryExpression(
	PrefixUnaryExpression* node, bool resultIsDiscarded) {
	// When we see a prefix increment expression whose operand is an exported
	// symbol, we should ensure all exports of that symbol are updated with the
	// correct value.
	//
	// - We do not transform generated identifiers for any reason.
	// - We do not transform identifiers tagged with the LocalName flag.
	// - We do not transform identifiers that were originally the name of an
	//   enum or namespace due to how they are transformed in TypeScript.
	// - We only transform identifiers that are exported at the top level.
	if ((node->Operator == Kind::PlusPlusToken ||
	     node->Operator == Kind::MinusMinusToken) &&
	    isIdentifier(node->Operand) &&
	    !transformers::isLocalName(emitContext(), node->Operand)) {
		std::vector<Node*> exportedNames = getExports(node->Operand);
		if (!exportedNames.empty()) {
			// given:
			//   var x = 0;
			//   export { x }
			//   ++x;
			// emits:
			//   var x = 0;
			//   exports.x = x;
			//   exports.x = ++x;
			// note:
			//   after the operation, `exports.x` will hold the value of `x`
			//   after the increment.

			Node* expression = factory()->updatePrefixUnaryExpression(
				node, node->Operator,
				visitor()->visitNode(node->Operand));
			for (Node* exportName : exportedNames) {
				expression = createExportExpression(
					exportName, expression, nullptr /*location*/,
					false /*liveBinding*/);
				emitContext()->assignCommentAndSourceMapRanges(
					expression, node->asNode());
			}
			return expression;
		}
	}
	return visitor()->visitEachChild(node->asNode());
}

// Visits a postfix unary expression that might modify an exported identifier.
// — commonjsmodule.go:1733
Node* CommonJSModuleTransformer::visitPostfixUnaryExpression(
	PostfixUnaryExpression* node, bool resultIsDiscarded) {
	// When we see a postfix increment expression whose operand is an exported
	// symbol, we should ensure all exports of that symbol are updated with the
	// correct value.
	//
	// - We do not transform generated identifiers for any reason.
	// - We do not transform identifiers tagged with the LocalName flag.
	// - We do not transform identifiers that were originally the name of an
	//   enum or namespace due to how they are transformed in TypeScript.
	// - We only transform identifiers that are exported at the top level.
	if ((node->Operator == Kind::PlusPlusToken ||
	     node->Operator == Kind::MinusMinusToken) &&
	    isIdentifier(node->Operand) &&
	    !transformers::isLocalName(emitContext(), node->Operand)) {
		std::vector<Node*> exportedNames = getExports(node->Operand);
		if (!exportedNames.empty()) {
			// given (value is discarded):
			//   var x = 0;
			//   export { x }
			//   x++;
			// emits:
			//   var x = 0, y;
			//   exports.x = x;
			//   exports.x = (x++, x);
			// note:
			//   after the operation, `exports.x` will hold the value of `x`
			//   after the increment.
			//
			// given (value is not discarded):
			//   var x = 0, y;
			//   export { x }
			//   y = x++;
			// emits:
			//   var _a;
			//   var x = 0, y;
			//   exports.x = x;
			//   y = (exports.x = (_a = x++, x), _a);
			// note:
			//   after the operation, `exports.x` will hold the value of `x`
			//   after the increment, while `y` will hold the value of `x`
			//   before the increment.

			Node* temp = nullptr;
			Node* expression = factory()->updatePostfixUnaryExpression(
				node, visitor()->visitNode(node->Operand), node->Operator);
			if (!resultIsDiscarded) {
				temp = factory()->newTempVariable();
				emitContext()->addVariableDeclaration(temp);

				expression =
					factory()->newAssignmentExpression(temp, expression);
				emitContext()->assignCommentAndSourceMapRanges(
					expression, node->asNode());
			}

			expression = factory()->newCommaExpression(
				expression, node->Operand->clone(*factory()));
			emitContext()->assignCommentAndSourceMapRanges(expression,
			                                             node->asNode());

			for (Node* exportName : exportedNames) {
				expression = createExportExpression(
					exportName, expression, nullptr /*location*/,
					false /*liveBinding*/);
				emitContext()->assignCommentAndSourceMapRanges(
					expression, node->asNode());
			}

			if (temp != nullptr) {
				expression =
					factory()->newCommaExpression(expression, temp);
				emitContext()->assignCommentAndSourceMapRanges(
					expression, node->asNode());
			}

			return expression;
		}
	}

	return visitor()->visitEachChild(node->asNode());
}

// Visits a call expression that might reference an imported symbol and thus
// require an indirect call, or that might be an `import()` or `require()` call
// that may need to be rewritten. — commonjsmodule.go:1819
Node* CommonJSModuleTransformer::visitCallExpression(CallExpression* node) {
	bool needsRewrite = false;
	if (tristateIsTrue(compilerOptions->RewriteRelativeImportExtensions)) {
		if ((isImportCall(node->asNode()) &&
		     !node->Arguments->nodes.empty()) ||
		    (isInJSFile(node->asNode()) &&
		     isRequireCall(node->asNode(),
		                   false /*requireStringLiteralLikeArgument*/))) {
			needsRewrite = true;
		}
	}
	if (isImportCall(node->asNode()) && shouldTransformImportCall()) {
		return visitImportCallExpression(node, needsRewrite);
	}
	if (needsRewrite) {
		return shimOrRewriteImportOrRequireCall(node);
	}
	if (isIdentifier(node->Expression)) {
		// given:
		//   import { f } from "mod";
		//   f();
		// emits:
		//   const mod_1 = require("mod");
		//   (0, mod_1.f)();
		// note:
		//   the indirect call is applied by the printer by way of the
		//   `EFIndirectCall` emit flag.
		Node* expression = visitExpressionIdentifier(node->Expression);
		NodeList* arguments = visitor()->visitNodes(node->Arguments);
		Node* updated = factory()->updateCallExpression(
			node, expression, node->QuestionDotToken,
			nullptr /*typeArguments*/, arguments, node->flags);
		if (!isIdentifier(expression) &&
		    !transformers::isHelperName(emitContext(), node->Expression)) {
			emitContext()->addEmitFlags(updated, printer::EFIndirectCall);
		}
		return updated;
	}
	return visitor()->visitEachChild(node->asNode());
}

// shouldTransformImportCall — commonjsmodule.go:1858
bool CommonJSModuleTransformer::shouldTransformImportCall() {
	return ::tsc::shouldTransformImportCall(
		currentSourceFile->FileName(), compilerOptions,
		getEmitModuleFormatOfFile(currentSourceFile));
}

// visitImportCallExpression — commonjsmodule.go:1862
Node* CommonJSModuleTransformer::visitImportCallExpression(
	CallExpression* node, bool rewriteOrShim) {
	if (moduleKind == ModuleKind::None &&
	    languageVersion >= ScriptTarget::ES2020) {
		return visitor()->visitEachChild(node->asNode());
	}

	Node* externalModuleName = getExternalModuleNameLiteral(
		factory(), node->asNode(), currentSourceFile, nullptr /*host*/,
		nullptr /*resolver*/, compilerOptions);
	Node* firstArgument = visitor()->visitNode(
		node->Arguments->nodes.empty() ? nullptr
		                             : node->Arguments->nodes[0]);

	// Only use the external module name if it differs from the first argument.
	// This allows us to preserve the quote style of the argument on output.
	Node* argument;
	if (externalModuleName != nullptr &&
	    (firstArgument == nullptr || !isStringLiteral(firstArgument) ||
	     firstArgument->text() != externalModuleName->text())) {
		argument = externalModuleName;
	} else if (firstArgument != nullptr && rewriteOrShim) {
		if (isStringLiteral(firstArgument)) {
			argument = rewriteModuleSpecifier(emitContext(), firstArgument,
			                                  compilerOptions);
		} else {
			argument =
				factory()->newRewriteRelativeImportExtensionsHelper(
					firstArgument,
					compilerOptions->Jsx == JsxEmit::Preserve);
		}
	} else {
		argument = firstArgument;
	}
	return createImportCallExpressionCommonJS(argument);
}

// createImportCallExpressionCommonJS — commonjsmodule.go:1883
Node* CommonJSModuleTransformer::createImportCallExpressionCommonJS(
	Node* arg) {
	// import(x)
	// emit as
	// Promise.resolve(`${x}`).then((s) => require(s)) /*CommonJS Require*/
	// We have to wrap require in then callback so that require is done in
	// asynchronously
	// if we simply do require in resolve callback in Promise constructor. We
	// will execute the loading immediately
	// If the arg is not inlineable, we have to evaluate and ToString() it in
	// the current scope
	// Otherwise, we inline it in require() so that it's statically analyzable

	bool needSyncEval = arg != nullptr && !isSimpleInlineableExpression(arg);

	std::vector<Node*> promiseResolveArguments;
	if (needSyncEval) {
		promiseResolveArguments = {factory()->newTemplateExpression(
			factory()->newTemplateHead("", "", TokenFlagsNone),
			factory()->newNodeList({factory()->newTemplateSpan(
				arg, factory()->newTemplateTail("", "", TokenFlagsNone))}))};
	}
	Node* promiseResolveCall = factory()->newCallExpression(
		factory()->newPropertyAccessExpression(
			factory()->newIdentifier("Promise"), nullptr /*questionDotToken*/,
			factory()->newIdentifier("resolve"), NodeFlagsNone),
		nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
		factory()->newNodeList(promiseResolveArguments), NodeFlagsNone);

	std::vector<Node*> requireArguments;
	if (needSyncEval) {
		requireArguments = {factory()->newIdentifier("s")};
	} else if (arg != nullptr) {
		requireArguments = {arg};
	}

	Node* requireCall = factory()->newImportStarHelper(
		factory()->newCallExpression(factory()->newIdentifier("require"),
		                             nullptr /*questionDotToken*/,
		                             nullptr /*typeArguments*/,
		                             factory()->newNodeList(requireArguments),
		                             NodeFlagsNone));

	std::vector<Node*> parameters;
	if (needSyncEval) {
		parameters = {factory()->newParameterDeclaration(
			nullptr /*modifiers*/, nullptr /*dotDotDotToken*/,
			factory()->newIdentifier("s"), nullptr /*questionToken*/,
			nullptr /*type*/, nullptr /*initializer*/)};
	}

	Node* function = factory()->newArrowFunction(
		nullptr /*modifiers*/, nullptr /*typeParameters*/,
		factory()->newNodeList(parameters), nullptr /*type*/,
		nullptr /*fullSignature*/,
		factory()->newToken(
			Kind::EqualsGreaterThanToken) /*equalsGreaterThanToken*/,
		requireCall);

	Node* downleveledImport = factory()->newCallExpression(
		factory()->newPropertyAccessExpression(
			promiseResolveCall, nullptr /*questionDotToken*/,
			factory()->newIdentifier("then"), NodeFlagsNone),
		nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
		factory()->newNodeList({function}), NodeFlagsNone);
	return downleveledImport;
}

// shimOrRewriteImportOrRequireCall — commonjsmodule.go:1964
Node* CommonJSModuleTransformer::shimOrRewriteImportOrRequireCall(
	CallExpression* node) {
	Node* expression = visitor()->visitNode(node->Expression);
	NodeList* argumentsList = node->Arguments;
	if (!node->Arguments->nodes.empty()) {
		Node* firstArgument =
			visitor()->visitNode(node->Arguments->nodes[0]);
		bool firstArgumentChanged = false;
		if (isStringLiteralLike(firstArgument)) {
			Node* rewritten = rewriteModuleSpecifier(
				emitContext(), firstArgument, compilerOptions);
			firstArgumentChanged = rewritten != firstArgument;
			firstArgument = rewritten;
		} else {
			firstArgument =
				factory()->newRewriteRelativeImportExtensionsHelper(
					firstArgument,
					compilerOptions->Jsx == JsxEmit::Preserve);
			firstArgumentChanged = true;
		}

		std::vector<Node*> restSource(node->Arguments->nodes.begin() + 1,
		                              node->Arguments->nodes.end());
		auto restResult = visitor()->visitSlice(restSource);
		if (firstArgumentChanged || restResult.second) {
			std::vector<Node*> arguments{firstArgument};
			arguments.insert(arguments.end(), restResult.first.begin(),
			                 restResult.first.end());
			argumentsList = factory()->newNodeList(arguments);
			argumentsList->loc = node->Arguments->loc;
		}
	}

	return factory()->updateCallExpression(
		node, expression, node->QuestionDotToken, nullptr /*typeArguments*/,
		argumentsList, node->flags);
}

// Visits a tagged template expression that might reference an imported symbol
// and thus require an indirect call. — commonjsmodule.go:1995
Node* CommonJSModuleTransformer::visitTaggedTemplateExpression(
	TaggedTemplateExpression* node) {
	if (isIdentifier(node->Tag)) {
		// given:
		//   import { f } from "mod";
		//   f``;
		// emits:
		//   const mod_1 = require("mod");
		//   (0, mod_1.f) ``;
		// note:
		//   the indirect call is applied by the printer by way of the
		//   `EFIndirectCall` emit flag.

		Node* expression = visitExpressionIdentifier(node->Tag);
		Node* template_ = visitor()->visitNode(node->Template);
		Node* updated = factory()->updateTaggedTemplateExpression(
			node, expression, nullptr /*questionDotToken*/,
			nullptr /*typeArguments*/, template_, node->flags);
		if (!isIdentifier(expression) &&
		    !transformers::isHelperName(emitContext(), node->Tag)) {
			emitContext()->addEmitFlags(updated, printer::EFIndirectCall);
		}
		return updated;
	}
	return visitor()->visitEachChild(node->asNode());
}

// Visits a shorthand property assignment that might reference an imported or
// exported symbol. — commonjsmodule.go:2024
Node* CommonJSModuleTransformer::visitShorthandPropertyAssignment(
	ShorthandPropertyAssignment* node) {
	Node* name = node->name;
	Node* exportedOrImportedName = visitExpressionIdentifier(name);
	if (exportedOrImportedName != name) {
		// A shorthand property with an assignment initializer is probably part
		// of a destructuring assignment
		Node* expression = exportedOrImportedName;
		if (node->ObjectAssignmentInitializer != nullptr) {
			expression = factory()->newAssignmentExpression(
				expression,
				visitor()->visitNode(node->ObjectAssignmentInitializer));
		}
		Node* assignment = factory()->newPropertyAssignment(
			nullptr /*modifiers*/, name, nullptr /*postfixToken*/,
			nullptr /*typeNode*/, expression);
		assignment->loc = node->loc;
		emitContext()->assignCommentAndSourceMapRanges(assignment,
		                                             node->asNode());
		return assignment;
	}
	return factory()->updateShorthandPropertyAssignment(
		node, nullptr /*modifiers*/, exportedOrImportedName,
		nullptr /*postfixToken*/, nullptr /*typeNode*/, node->EqualsToken,
		visitor()->visitNode(node->ObjectAssignmentInitializer));
}

// Visits an identifier that, if it is in an expression position, might
// reference an imported or exported symbol. — commonjsmodule.go:2053
Node* CommonJSModuleTransformer::visitIdentifier(Node* node) {
	if (transformers::isIdentifierReference(node, parentNode)) {
		return visitExpressionIdentifier(node);
	}
	return node;
}

// Visits an identifier in an expression position that might reference an
// imported or exported symbol. — commonjsmodule.go:2061
Node* CommonJSModuleTransformer::visitExpressionIdentifier(Node* node) {
	printer::AutoGenerateInfo* info = emitContext()->getAutoGenerateInfo(node);
	if (!(info != nullptr &&
	      !printer::generatedIdentifierFlagsHasAllowNameSubstitution(
		      info->Flags)) &&
	    !transformers::isHelperName(emitContext(), node) &&
	    !transformers::isLocalName(emitContext(), node) &&
	    !isDeclarationNameOfEnumOrNamespace(emitContext(), node)) {
		Node* exportContainer = resolver->GetReferencedExportContainer(
			emitContext()->mostOriginal(node),
			transformers::isExportName(emitContext(), node));
		if (exportContainer != nullptr && isSourceFile(exportContainer)) {
			Node* reference = factory()->newPropertyAccessExpression(
				factory()->newIdentifier("exports"),
				nullptr /*questionDotToken*/, node->clone(*factory()),
				NodeFlagsNone);
			emitContext()->assignCommentAndSourceMapRanges(reference, node);
			reference->loc = node->loc;
			return reference;
		}

		Node* importDeclaration = resolver->GetReferencedImportDeclaration(
			emitContext()->mostOriginal(node));
		if (importDeclaration != nullptr) {
			if (isImportClause(importDeclaration)) {
				// Resolver returns parse-tree declarations; Parent is used to
				// find the owning import declaration.
				Node* reference =
					factory()->newPropertyAccessExpression(
						factory()->newGeneratedNameForNode(
							importDeclaration->parent),
						nullptr /*questionDotToken*/,
						factory()->newIdentifier("default"), NodeFlagsNone);
				emitContext()->assignCommentAndSourceMapRanges(reference,
				                                             node);
				reference->loc = node->loc;
				return reference;
			}
			if (isImportSpecifier(importDeclaration)) {
				Node* name =
					importDeclaration->as<ImportSpecifier>()
						->propertyNameOrName();
				Node* decl = findAncestor(importDeclaration,
				                          [](Node* n) {
					                          return isImportDeclaration(n);
				                          });
				Node* target = factory()->newGeneratedNameForNode(
					decl != nullptr ? decl : importDeclaration);
				Node* reference;
				if (isStringLiteral(name)) {
					reference = factory()->newElementAccessExpression(
						target, nullptr /*questionDotToken*/,
						factory()->newStringLiteralFromNode(name),
						NodeFlagsNone);
				} else {
					Node* referenceName = name->clone(*factory());
					emitContext()->addEmitFlags(
						referenceName,
						printer::EFNoSourceMap | printer::EFNoComments);
					reference = factory()->newPropertyAccessExpression(
						target, nullptr /*questionDotToken*/, referenceName,
						NodeFlagsNone);
				}
				emitContext()->assignCommentAndSourceMapRanges(reference,
				                                             node);
				reference->loc = node->loc;
				return reference;
			}
		}
	}
	return node;
}

// Gets the exported names of an identifier, if it is exported.
// — commonjsmodule.go:2113
std::vector<Node*> CommonJSModuleTransformer::getExports(Node* name) {
	if (!transformers::isGeneratedIdentifier(emitContext(), name)) {
		Node* importDeclaration = resolver->GetReferencedImportDeclaration(
			emitContext()->mostOriginal(name));
		if (importDeclaration != nullptr) {
			return currentModuleInfo->exportedBindings.Get(importDeclaration);
		}

		// An exported namespace or enum may merge with an ambient declaration,
		// which won't show up in .js emit, so we analyze all value exports of
		// a symbol.
		collections::Set<Node*> bindingsSet;
		std::vector<Node*> bindings;
		std::vector<Node*> declarations =
			resolver->GetReferencedValueDeclarations(
				emitContext()->mostOriginal(name));
		if (!declarations.empty()) {
			for (Node* declaration : declarations) {
				std::vector<Node*> exportedBindings =
					currentModuleInfo->exportedBindings.Get(declaration);
				for (Node* binding : exportedBindings) {
					if (!bindingsSet.Has(binding)) {
						bindingsSet.Add(binding);
						bindings.push_back(binding);
					}
				}
			}
			return bindings;
		}
	} else if (isFileLevelReservedGeneratedIdentifier(emitContext(), name)) {
		std::vector<Node*> exportSpecifiers =
			currentModuleInfo->exportSpecifiers.Get(name->text());
		if (!exportSpecifiers.empty()) {
			std::vector<Node*> exportedNames;
			for (Node* exportSpecifier : exportSpecifiers) {
				exportedNames.push_back(exportSpecifier->name());
			}
			return exportedNames;
		}
	}
	return {};
}

}  // namespace tsc::transformers::moduletransforms
