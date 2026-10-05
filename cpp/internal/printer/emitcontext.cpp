// Port of tsc/internal/printer/emitcontext.go — EmitContext.
#include "internal/printer/emitcontext.h"

#include "internal/printer/printer.h" // isPrologueDirective lives in ast, but printer.h pulls utilities

#include <algorithm>
#include <mutex>

namespace tsc::printer {

// NewNodeFactory (factory.go) — wires the emit-aware hooks (synthesized flag
// on create, original-node tracking on update/clone).
NodeFactory::NodeFactory(EmitContext* ctx) : emitContext(ctx) {
	hooks.onCreate = [ctx](Node* node) { ctx->onCreate(node); };
	hooks.onUpdate = [ctx](Node* updated, Node* original) {
		ctx->onUpdate(updated, original);
	};
	hooks.onClone = [ctx](Node* updated, Node* original) {
		ctx->onClone(updated, original);
	};
}

void EmitContext::reset() {
	// *c = EmitContext{Factory: c.Factory} — Factory is kept as-is.
	autoGenerate.clear();
	textSource.clear();
	original_.clear();
	emitNodes = LinkStore<Node*, emitNode>(&emitNodesArena);
	assignedName.clear();
	classThis.clear();
	varScopeStack.clear();
	letScopeStack.clear();
	emitHelpers.clear();
	visitors.clear();
}

// GetEmitContext (emitcontext.go:57) — pooled.
namespace {
std::mutex emitContextPoolMutex;
std::vector<EmitContext*> emitContextPool;
} // namespace

std::pair<EmitContext*, std::function<void()>> GetEmitContext() {
	EmitContext* c;
	{
		std::lock_guard<std::mutex> lock(emitContextPoolMutex);
		if (!emitContextPool.empty()) {
			c = emitContextPool.back();
			emitContextPool.pop_back();
		} else {
			c = NewEmitContext();
		}
	}
	return {c, [c] {
		        c->reset();
		        std::lock_guard<std::mutex> lock(emitContextPoolMutex);
		        emitContextPool.push_back(c);
	        }};
}

// NewNodeVisitor (emitcontext.go:90) — visitor attached to this EmitContext.
NodeVisitor* EmitContext::newNodeVisitor(std::function<Node*(Node*)> visit) {
	NodeVisitorHooks hooks;
	hooks.visitParameters = [this](NodeList* n, NodeVisitor* v) {
		return visitParameters(n, v);
	};
	hooks.visitFunctionBody = [this](Node* n, NodeVisitor* v) {
		return visitFunctionBody(n, v);
	};
	hooks.visitIterationBody = [this](Node* n, NodeVisitor* v) {
		return visitIterationBody(n, v);
	};
	hooks.visitTopLevelStatements = [this](NodeList* n, NodeVisitor* v) {
		return visitVariableEnvironment(n, v);
	};
	hooks.visitEmbeddedStatement = [this](Node* n, NodeVisitor* v) {
		return visitEmbeddedStatement(n, v);
	};
	NodeVisitor* v =
		tsc::newNodeVisitor(std::move(visit), factory.asNodeFactory(), hooks);
	visitors.emplace_back(v);
	return v;
}

//
// Environment tracking
//

void EmitContext::startVariableEnvironment() {
	varScopeStack.push(varScope{});
	startLexicalEnvironment();
}

std::vector<Node*> EmitContext::endVariableEnvironment() {
	varScope scope = varScopeStack.pop();
	std::vector<Node*> statements;
	if (!scope.functions.empty()) {
		statements = scope.functions;
	}
	if (!scope.variables.empty()) {
		Node* varDeclList = factory.newVariableDeclarationList(
			factory.newNodeList(scope.variables), NodeFlagsNone);
		Node* varStatement =
			factory.newVariableStatement(nullptr /*modifiers*/, varDeclList);
		setEmitFlags(varStatement, EFCustomPrologue);
		statements.push_back(varStatement);
	}
	if (!scope.initializationStatements.empty()) {
		statements.insert(statements.end(),
		                  scope.initializationStatements.begin(),
		                  scope.initializationStatements.end());
	}
	auto lexical = endLexicalEnvironment();
	statements.insert(statements.end(), lexical.begin(), lexical.end());
	return statements;
}

NodeList* EmitContext::endAndMergeVariableEnvironmentList(
	NodeList* statements) {
	std::vector<Node*> nodes;
	if (statements != nullptr) {
		nodes = statements->nodes;
	}

	auto [result, changed] =
		mergeEnvironmentImpl(nodes, endVariableEnvironment());
	if (changed) {
		NodeList* list = factory.newNodeList(result);
		list->loc = statements->loc;
		return list;
	}

	return statements;
}

std::vector<Node*> EmitContext::endAndMergeVariableEnvironment(
	const std::vector<Node*>& statements) {
	auto [result, changed] =
		mergeEnvironmentImpl(statements, endVariableEnvironment());
	return result;
}

void EmitContext::addVariableDeclaration(Node* name) {
	Node* varDecl = factory.newVariableDeclaration(
		name, nullptr /*exclamationToken*/, nullptr /*typeNode*/,
		nullptr /*initializer*/);
	setEmitFlags(varDecl, EFNoNestedSourceMaps);
	varScope* scope = varScopeStack.peek();
	scope->variables.push_back(varDecl);
	if ((scope->flags & environmentFlagsInParameters) != 0) {
		scope->flags |= environmentFlagsVariablesHoistedInParameters;
	}
}

void EmitContext::addHoistedFunctionDeclaration(Node* node) {
	setEmitFlags(node, EFCustomPrologue);
	varScope* scope = varScopeStack.peek();
	scope->functions.push_back(node);
}

void EmitContext::startLexicalEnvironment() {
	letScopeStack.push(varScope{});
}

std::vector<Node*> EmitContext::endLexicalEnvironment() {
	varScope scope = letScopeStack.pop();
	std::vector<Node*> statements;
	if (!scope.variables.empty()) {
		Node* varDeclList = factory.newVariableDeclarationList(
			factory.newNodeList(scope.variables), NodeFlagsLet);
		Node* varStatement =
			factory.newVariableStatement(nullptr /*modifiers*/, varDeclList);
		setEmitFlags(varStatement, EFCustomPrologue);
		statements.push_back(varStatement);
	}
	return statements;
}

NodeList* EmitContext::endAndMergeLexicalEnvironmentList(
	NodeList* statements) {
	std::vector<Node*> nodes;
	if (statements != nullptr) {
		nodes = statements->nodes;
	}

	auto [result, changed] =
		mergeEnvironmentImpl(nodes, endLexicalEnvironment());
	if (changed) {
		NodeList* list = factory.newNodeList(result);
		list->loc = statements->loc;
		return list;
	}

	return statements;
}

std::vector<Node*> EmitContext::endAndMergeLexicalEnvironment(
	const std::vector<Node*>& statements) {
	auto [result, changed] =
		mergeEnvironmentImpl(statements, endLexicalEnvironment());
	return result;
}

void EmitContext::addLexicalDeclaration(Node* name) {
	Node* varDecl = factory.newVariableDeclaration(
		name, nullptr /*exclamationToken*/, nullptr /*typeNode*/,
		nullptr /*initializer*/);
	setEmitFlags(varDecl, EFNoNestedSourceMaps);
	varScope* scope = letScopeStack.peek();
	scope->variables.push_back(varDecl);
}

NodeList* EmitContext::mergeEnvironmentList(
	NodeList* statements, const std::vector<Node*>& declarations) {
	auto [result, changed] =
		mergeEnvironmentImpl(statements->nodes, declarations);
	if (changed) {
		NodeList* list = factory.newNodeList(result);
		list->loc = statements->loc;
		return list;
	}
	return statements;
}

std::vector<Node*> EmitContext::mergeEnvironment(
	const std::vector<Node*>& statements,
	const std::vector<Node*>& declarations) {
	return mergeEnvironmentImpl(statements, declarations).first;
}

std::pair<std::vector<Node*>, bool> EmitContext::mergeEnvironmentImpl(
	const std::vector<Node*>& statements,
	const std::vector<Node*>& declarations) {
	if (declarations.empty()) {
		return {statements, false};
	}

	// When we merge new lexical statements into an existing statement list, we
	// merge them in the following manner:
	//
	// Given:
	//
	// | Left                               | Right                               |
	// |------------------------------------|-------------------------------------|
	// | [standard prologues (left)]        | [standard prologues (right)]        |
	// | [hoisted functions (left)]         | [hoisted functions (right)]         |
	// | [hoisted variables (left)]         | [hoisted variables (right)]         |
	// | [lexical init statements (left)]   | [lexical init statements (right)]   |
	// | [other statements (left)]          |                                     |
	//
	// The resulting statement list will be:
	//
	// | Result                              |
	// |-------------------------------------|
	// | [standard prologues (right)]        |
	// | [standard prologues (left)]         |
	// | [hoisted functions (right)]         |
	// | [hoisted functions (left)]          |
	// | [hoisted variables (right)]         |
	// | [hoisted variables (left)]          |
	// | [lexical init statements (right)]   |
	// | [lexical init statements (left)]    |
	// | [other statements (left)]           |
	//
	// NOTE: It is expected that new lexical init statements must be evaluated
	// before existing lexical init statements, as the prior transformation may
	// depend on the evaluation of the lexical init statements to be in the
	// correct state.

	bool changed = false;

	// find standard prologues on left in the following order: standard
	// directives, hoisted functions, hoisted variables, other custom
	int leftStandardPrologueEnd =
		findSpanEnd<Node*>(statements,
	                   [](Node* n) { return isPrologueDirective(n); }, 0);
	int leftHoistedFunctionsEnd = findSpanEndWithEmitContext<Node*>(
		this, statements,
		[](EmitContext* c, Node* n) { return c->isHoistedFunction(n); },
		leftStandardPrologueEnd);
	int leftHoistedVariablesEnd = findSpanEndWithEmitContext<Node*>(
		this, statements,
		[](EmitContext* c, Node* n) { return c->isHoistedVariableStatement(n); },
		leftHoistedFunctionsEnd);

	// find standard prologues on right in the following order: standard
	// directives, hoisted functions, hoisted variables, other custom
	int rightStandardPrologueEnd =
		findSpanEnd<Node*>(declarations,
	                   [](Node* n) { return isPrologueDirective(n); }, 0);
	int rightHoistedFunctionsEnd = findSpanEndWithEmitContext<Node*>(
		this, declarations,
		[](EmitContext* c, Node* n) { return c->isHoistedFunction(n); },
		rightStandardPrologueEnd);
	int rightHoistedVariablesEnd = findSpanEndWithEmitContext<Node*>(
		this, declarations,
		[](EmitContext* c, Node* n) { return c->isHoistedVariableStatement(n); },
		rightHoistedFunctionsEnd);
	int rightCustomPrologueEnd = findSpanEndWithEmitContext<Node*>(
		this, declarations,
		[](EmitContext* c, Node* n) { return c->isCustomPrologue(n); },
		rightHoistedVariablesEnd);
	if (rightCustomPrologueEnd != static_cast<int>(declarations.size())) {
		TSC_UNREACHABLE(
			"Expected declarations to be valid standard or custom prologues");
	}

	std::vector<Node*> left = statements;

	auto slice = [](const std::vector<Node*>& v, int lo, int hi) {
		return std::vector<Node*>(v.begin() + lo, v.begin() + hi);
	};

	// splice other custom prologues from right into left
	if (rightCustomPrologueEnd > rightHoistedVariablesEnd) {
		left = spliceSlice(
			left, leftHoistedVariablesEnd, 0,
			slice(declarations, rightHoistedVariablesEnd,
		          rightCustomPrologueEnd));
		changed = true;
	}

	// splice hoisted variables from right into left
	if (rightHoistedVariablesEnd > rightHoistedFunctionsEnd) {
		left = spliceSlice(
			left, leftHoistedFunctionsEnd, 0,
			slice(declarations, rightHoistedFunctionsEnd,
		          rightHoistedVariablesEnd));
		changed = true;
	}

	// splice hoisted functions from right into left
	if (rightHoistedFunctionsEnd > rightStandardPrologueEnd) {
		left = spliceSlice(
			left, leftStandardPrologueEnd, 0,
			slice(declarations, rightStandardPrologueEnd,
		          rightHoistedFunctionsEnd));
		changed = true;
	}

	// splice standard prologues from right into left (that are not already in
	// left)
	if (rightStandardPrologueEnd > 0) {
		if (leftStandardPrologueEnd == 0) {
			left = spliceSlice(left, 0, 0,
			                   slice(declarations, 0, rightStandardPrologueEnd));
			changed = true;
		} else {
			std::unordered_set<std::string> leftPrologues;
			for (int i = 0; i < leftStandardPrologueEnd; i++) {
				Node* leftPrologue = statements[i];
				leftPrologues.insert(leftPrologue->expression()->text());
			}
			for (int i = rightStandardPrologueEnd - 1; i >= 0; i--) {
				Node* rightPrologue = declarations[i];
				if (leftPrologues.count(
					    rightPrologue->expression()->text()) == 0) {
					left = concatenateSlices(std::vector<Node*>{rightPrologue},
					                         left);
					changed = true;
				}
			}
		}
	}

	return {left, changed};
}

bool EmitContext::isCustomPrologue(Node* node) {
	return (emitFlags(node) & EFCustomPrologue) != 0;
}

bool EmitContext::isHoistedFunction(Node* node) {
	return isCustomPrologue(node) && isFunctionDeclaration(node);
}

namespace {
bool isHoistedVariable(Node* node) {
	return isIdentifier(node->name()) && node->initializer() == nullptr;
}
} // namespace

bool EmitContext::isHoistedVariableStatement(Node* node) {
	return isCustomPrologue(node) && isVariableStatement(node) &&
	       std::all_of(node->as<VariableStatement>()
	                       ->DeclarationList->as<VariableDeclarationList>()
	                       ->Declarations->nodes.begin(),
	                   node->as<VariableStatement>()
	                       ->DeclarationList->as<VariableDeclarationList>()
	                       ->Declarations->nodes.end(),
	                   isHoistedVariable);
}

//
// Name Generation
//

bool EmitContext::hasAutoGenerateInfo(Node* node) const {
	if (node != nullptr) {
		return autoGenerate.count(node) != 0;
	}
	return false;
}

AutoGenerateInfo* EmitContext::getAutoGenerateInfo(Node* name) {
	if (name == nullptr) {
		return nullptr;
	}
	auto it = autoGenerate.find(name);
	return it != autoGenerate.end() ? &it->second : nullptr;
}

const AutoGenerateInfo* EmitContext::getAutoGenerateInfo(Node* name) const {
	if (name == nullptr) {
		return nullptr;
	}
	auto it = autoGenerate.find(name);
	return it != autoGenerate.end() ? &it->second : nullptr;
}

Node* EmitContext::getNodeForGeneratedName(Node* name) {
	if (AutoGenerateInfo* ag = getAutoGenerateInfo(name);
	    ag != nullptr && generatedIdentifierFlagsIsNode(ag->Flags)) {
		return getNodeForGeneratedNameWorker(ag->Node, ag->Id);
	}
	return name;
}

Node* EmitContext::getNodeForGeneratedNameWorker(Node* node,
                                                 AutoGenerateId autoGenerateId) {
	Node* original = this->original(node);
	while (original != nullptr) {
		node = original;
		if (isMemberName(node)) {
			// if "node" is a different generated name (having a different
			// "autoGenerateId"), use it and stop traversing.
			AutoGenerateInfo* ag = getAutoGenerateInfo(node);
			if (ag == nullptr ||
			    (generatedIdentifierFlagsIsNode(ag->Flags) &&
			     ag->Id != autoGenerateId)) {
				break;
			}
			if (generatedIdentifierFlagsIsNode(ag->Flags)) {
				original = ag->Node;
				continue;
			}
		}
		original = this->original(node);
	}
	return node;
}

namespace {
std::atomic<uint32_t> nextAutoGenerateIdCounter{0};
} // namespace

// emitcontext.go:427 — atomic.Uint32.Add(1) returns the new value.
uint32_t nextAutoGenerateId() { return ++nextAutoGenerateIdCounter; }

bool EmitContext::isFileLevelUniqueName(
	SourceFile* sourceFile, const std::string& name,
	const std::function<bool(const std::string&)>& hasGlobalName) {
	if (hasGlobalName && hasGlobalName(name)) {
		return false;
	}
	sourceFile = mostOriginal(sourceFile->asNode())->as<SourceFile>();
	return !sourceFileHasIdentifier(sourceFile, name);
}

//
// Emit-related Data
//

void EmitContext::requestEmitHelper(EmitHelper* helper) {
	if (helper->Scoped) {
		TSC_UNREACHABLE("Cannot request a scoped emit helper");
	}
	for (EmitHelper* h : helper->Dependencies) {
		requestEmitHelper(h);
	}
	emitHelpers.add(helper);
}

std::vector<EmitHelper*> EmitContext::readEmitHelpers() {
	std::vector<EmitHelper*> helpers = emitHelpers.elements;
	emitHelpers.clear();
	return helpers;
}

void EmitContext::addEmitHelper(Node* node, EmitHelper* helper) {
	emitNode* en = emitNodes.Get(node);
	appendIfUnique(en->helpers, helper);
}

void EmitContext::moveEmitHelpers(
	Node* source, Node* target,
	const std::function<bool(EmitHelper*)>& predicate) {
	emitNode* sourceEmitNode = emitNodes.TryGet(source);
	if (sourceEmitNode == nullptr) {
		return;
	}
	std::vector<EmitHelper*>& sourceEmitHelpers = sourceEmitNode->helpers;
	if (sourceEmitHelpers.empty()) {
		return;
	}

	emitNode* targetEmitNode = emitNodes.Get(target);
	int helpersRemoved = 0;
	for (size_t i = 0; i < sourceEmitHelpers.size(); i++) {
		EmitHelper* helper = sourceEmitHelpers[i];
		if (predicate(helper)) {
			helpersRemoved++;
			appendIfUnique(targetEmitNode->helpers, helper);
		} else if (helpersRemoved > 0) {
			sourceEmitHelpers[i - helpersRemoved] = helper;
		}
	}

	if (helpersRemoved > 0) {
		sourceEmitHelpers.resize(sourceEmitHelpers.size() - helpersRemoved);
	}
}

std::vector<EmitHelper*> EmitContext::getEmitHelpers(Node* node) {
	if (emitNode* en = emitNodes.TryGet(node)) {
		return en->helpers;
	}
	return {};
}

Node* EmitContext::getExternalHelpersModuleName(SourceFile* node) {
	if (Node* parseNode = this->parseNode(node->asNode()); parseNode != nullptr) {
		if (emitNode* en = emitNodes.TryGet(parseNode)) {
			return en->externalHelpersModuleName;
		}
	}
	return nullptr;
}

void EmitContext::setExternalHelpersModuleName(SourceFile* node, Node* name) {
	Node* parseNode = this->parseNode(node->asNode());
	if (parseNode == nullptr) {
		TSC_UNREACHABLE(
			"Node must be a parse tree node or have an Original pointer to a "
			"parse tree node.");
	}

	emitNodes.Get(parseNode)->externalHelpersModuleName = name;
}

bool EmitContext::hasRecordedExternalHelpers(SourceFile* node) {
	if (Node* parseNode = this->parseNode(node->asNode()); parseNode != nullptr) {
		emitNode* en = emitNodes.TryGet(parseNode);
		return en != nullptr &&
		       (en->externalHelpersModuleName != nullptr ||
		        (en->emitFlags & EFExternalHelpers) != 0);
	}
	return false;
}

bool EmitContext::isCallToHelper(Node* firstSegment,
                                 const std::string& helperName) {
	return isCallExpression(firstSegment) &&
	       isIdentifier(firstSegment->expression()) &&
	       (emitFlags(firstSegment->expression()) & EFHelperName) != 0 &&
	       firstSegment->expression()->text() == helperName;
}

//
// Visitor Hooks
//

NodeList* EmitContext::visitVariableEnvironment(NodeList* nodes,
                                                NodeVisitor* visitor) {
	startVariableEnvironment();
	return endAndMergeVariableEnvironmentList(visitor->visitNodes(nodes));
}

NodeList* EmitContext::visitParameters(NodeList* nodes, NodeVisitor* visitor) {
	startVariableEnvironment();
	varScope* scope = varScopeStack.peek();
	environmentFlags oldFlags = scope->flags;
	scope->flags |= environmentFlagsInParameters;
	nodes = visitor->visitNodes(nodes);

	// As of ES2015, any runtime execution of that occurs in for a parameter
	// (such as evaluating an initializer or a binding pattern), occurs in its
	// own lexical scope. As a result, any expression that we might transform
	// that introduces a temporary variable would fail as the temporary
	// variable exists in a different lexical scope. To address this, we move
	// any binding patterns and initializers in a parameter list to the body if
	// we detect a variable being hoisted while visiting a parameter list when
	// the emit target is greater than ES2015. (Which is now all targets.)
	if ((scope->flags & environmentFlagsVariablesHoistedInParameters) != 0) {
		nodes = addDefaultValueAssignmentsIfNeeded(nodes);
	}
	scope->flags = oldFlags;
	// !!! c.suspendVariableEnvironment()
	return nodes;
}

NodeList* EmitContext::addDefaultValueAssignmentsIfNeeded(NodeList* nodeList) {
	if (nodeList == nullptr) {
		return nodeList;
	}
	std::vector<Node*> result;
	std::vector<Node*>& nodes = nodeList->nodes;
	for (size_t i = 0; i < nodes.size(); i++) {
		Node* parameter = nodes[i];
		Node* updated = addDefaultValueAssignmentIfNeeded(
			parameter->as<ParameterDeclaration>());
		if (updated != parameter) {
			if (result.empty()) {
				result = nodes;
			}
			result[i] = updated;
		}
	}
	if (!result.empty()) {
		NodeList* res = factory.newNodeList(result);
		res->loc = nodeList->loc;
		return res;
	}
	return nodeList;
}

Node* EmitContext::addDefaultValueAssignmentIfNeeded(
	ParameterDeclaration* parameter) {
	// A rest parameter cannot have a binding pattern or an initializer,
	// so let's just ignore it.
	if (parameter->DotDotDotToken != nullptr) {
		return parameter->asNode();
	} else if (isBindingPattern(parameter->name)) {
		return addDefaultValueAssignmentForBindingPattern(parameter);
	} else if (parameter->Initializer != nullptr) {
		return addDefaultValueAssignmentForInitializer(
			parameter, parameter->name, parameter->Initializer);
	}
	return parameter->asNode();
}

Node* EmitContext::addDefaultValueAssignmentForBindingPattern(
	ParameterDeclaration* parameter) {
	Node* initNode;
	if (parameter->Initializer != nullptr) {
		initNode = factory.newConditionalExpression(
			factory.newStrictEqualityExpression(
				factory.newGeneratedNameForNode(parameter->asNode()),
				factory.newVoidZeroExpression()),
			factory.newToken(Kind::QuestionToken), parameter->Initializer,
			factory.newToken(Kind::ColonToken),
			factory.newGeneratedNameForNode(parameter->asNode()));
	} else {
		initNode = factory.newGeneratedNameForNode(parameter->asNode());
	}
	addInitializationStatement(factory.newVariableStatement(
		nullptr,
		factory.newVariableDeclarationList(
			factory.newNodeList(std::vector<Node*>{factory.newVariableDeclaration(
				parameter->name, nullptr, parameter->Type, initNode)}),
			NodeFlagsNone)));
	return factory.updateParameterDeclaration(
		parameter, parameter->modifiers, parameter->DotDotDotToken,
		factory.newGeneratedNameForNode(parameter->asNode()),
		parameter->QuestionToken, parameter->Type, nullptr);
}

Node* EmitContext::addDefaultValueAssignmentForInitializer(
	ParameterDeclaration* parameter, Node* name, Node* initializer) {
	addEmitFlags(initializer, EFNoSourceMap | EFNoComments);
	Node* nameClone = name->clone(factory);
	addEmitFlags(nameClone, EFNoSourceMap);
	Node* initAssignment = factory.newAssignmentExpression(nameClone, initializer);
	initAssignment->loc = parameter->asNode()->loc;
	addEmitFlags(initAssignment, EFNoComments);
	Node* initBlock = factory.newBlock(
		factory.newNodeList(std::vector<Node*>{
			factory.newExpressionStatement(initAssignment)}),
		false);
	initBlock->loc = parameter->asNode()->loc;
	addEmitFlags(initBlock,
	             EFSingleLine | EFNoTrailingSourceMap | EFNoTokenSourceMaps |
	                 EFNoComments);
	addInitializationStatement(factory.newIfStatement(
		factory.newTypeCheck(name->clone(factory), "undefined"), initBlock,
		nullptr));
	return factory.updateParameterDeclaration(
		parameter, parameter->modifiers, parameter->DotDotDotToken,
		parameter->name, parameter->QuestionToken, parameter->Type, nullptr);
}

void EmitContext::addInitializationStatement(Node* node) {
	varScope* scope = varScopeStack.peek();
	if (scope == nullptr) {
		TSC_UNREACHABLE(
			"Tried to add an initialization statement without a surrounding "
			"variable scope");
	}
	addEmitFlags(node, EFCustomPrologue);
	scope->initializationStatements.push_back(node);
}

Node* EmitContext::convertToFunctionBlock(Node* node, bool multiLine) {
	if (isBlock(node)) {
		return node;
	}
	Node* returnStatement = factory.newReturnStatement(node);
	returnStatement->loc = node->loc;
	NodeList* statements =
		factory.newNodeList(std::vector<Node*>{returnStatement});
	statements->loc = node->loc;
	Node* block = factory.newBlock(statements, multiLine);
	block->loc = node->loc;
	return block;
}

Node* EmitContext::visitFunctionBody(Node* node, NodeVisitor* visitor) {
	// !!! c.resumeVariableEnvironment()
	Node* updated = visitor->visitNode(node);
	std::vector<Node*> declarations = endVariableEnvironment();
	if (declarations.empty()) {
		return updated;
	}

	if (updated == nullptr) {
		return factory.newBlock(factory.newNodeList(declarations),
		                        true /*multiLine*/);
	}

	if (!isBlock(updated)) {
		addEmitFlags(updated, EFNoComments);
		Node* block = convertToFunctionBlock(updated, false /*multiLine*/);
		return factory.updateBlock(
			block->as<Block>(),
			mergeEnvironmentList(block->as<Block>()->statementList(),
			                     declarations),
			block->as<Block>()->MultiLine);
	}

	return factory.updateBlock(
		updated->as<Block>(),
		mergeEnvironmentList(updated->as<Block>()->statementList(), declarations),
		updated->as<Block>()->MultiLine);
}

Node* EmitContext::visitIterationBody(Node* body, NodeVisitor* visitor) {
	if (body == nullptr) {
		return nullptr;
	}

	startLexicalEnvironment();
	Node* updated = visitEmbeddedStatement(body, visitor);
	if (updated == nullptr) {
		TSC_UNREACHABLE("Expected visitor to return a statement.");
	}

	std::vector<Node*> statements = endLexicalEnvironment();
	if (!statements.empty()) {
		if (isBlock(updated)) {
			NodeList* updatedStatements = updated->statementList();
			statements.insert(statements.end(), updatedStatements->nodes.begin(),
			                  updatedStatements->nodes.end());
			NodeList* statementsList = factory.newNodeList(statements);
			statementsList->loc = updated->statementList()->loc;
			return factory.updateBlock(updated->as<Block>(), statementsList,
			                           updated->as<Block>()->MultiLine);
		}
		statements.push_back(updated);
		return factory.newBlock(factory.newNodeList(statements),
		                        true /*multiLine*/);
	}

	return updated;
}

Node* EmitContext::visitEmbeddedStatement(Node* node, NodeVisitor* visitor) {
	if (node == nullptr) {
		return nullptr;
	}
	Node* embeddedStatement = visitor->visitEmbeddedStatement(node);
	if (embeddedStatement == nullptr ||
	    isNotEmittedStatement(embeddedStatement)) {
		Node* emptyStatement = visitor->factory->newEmptyStatement();
		emptyStatement->loc = node->loc;
		setOriginal(emptyStatement, node);
		assignCommentRange(emptyStatement, node);
		return emptyStatement;
	}
	return embeddedStatement;
}

//
// Synthesized comments
//

Node* EmitContext::setSyntheticLeadingComments(
	Node* node, std::vector<SynthesizedComment> comments) {
	emitNodes.Get(node)->leadingComments = std::move(comments);
	return node;
}

Node* EmitContext::addSyntheticLeadingComment(Node* node, Kind kind,
                                              std::string text,
                                              bool hasTrailingNewLine) {
	emitNodes.Get(node)->leadingComments.push_back(
		SynthesizedComment{kind, TextRange{-1, -1}, false /*HasLeadingNewLine*/,
	                   hasTrailingNewLine, std::move(text)});
	return node;
}

std::vector<SynthesizedComment> EmitContext::getSyntheticLeadingComments(
	Node* node) {
	if (emitNodes.Has(node)) {
		return emitNodes.Get(node)->leadingComments;
	}
	return {};
}

Node* EmitContext::setSyntheticTrailingComments(
	Node* node, std::vector<SynthesizedComment> comments) {
	emitNodes.Get(node)->trailingComments = std::move(comments);
	return node;
}

Node* EmitContext::addSyntheticTrailingComment(Node* node, Kind kind,
                                               std::string text,
                                               bool hasTrailingNewLine) {
	emitNodes.Get(node)->trailingComments.push_back(
		SynthesizedComment{kind, TextRange{-1, -1}, false /*HasLeadingNewLine*/,
	                   hasTrailingNewLine, std::move(text)});
	return node;
}

std::vector<SynthesizedComment> EmitContext::getSyntheticTrailingComments(
	Node* node) {
	if (emitNodes.Has(node)) {
		return emitNodes.Get(node)->trailingComments;
	}
	return {};
}

void EmitContext::setTypeNode(Node* node, Node* typeNode) {
	emitNodes.Get(node)->typeNode = typeNode;
}

Node* EmitContext::getTypeNode(Node* node) {
	if (emitNode* en = emitNodes.TryGet(node)) {
		return en->typeNode;
	}
	return nullptr;
}

Node* EmitContext::newNotEmittedStatement(Node* node) {
	Node* statement = factory.newNotEmittedStatement();
	statement->loc = node->loc;
	setOriginal(statement, node);
	assignCommentRange(statement, node);
	return statement;
}

} // namespace tsc::printer
