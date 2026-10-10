// Port of binder-relevant helpers from tsc/internal/ast/utilities.go,
// ast.go, symbol.go, flow.go, symbolcompare.go.
#include <algorithm>
#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/flow.h"
#include "internal/ast/symbol.h"
#include "internal/ast/symbolflags.h"
#include "internal/core/types.h"
#include "internal/tspath/tspath.h"

namespace tsc {

// --- Atomic ids (ast/utilities.go) ---

static std::atomic<NodeId> nextNodeId{0};
static std::atomic<SymbolId> nextSymbolId{0};
static std::atomic<uint64_t> nextNodeBlockId{0};
static std::atomic<uint64_t> nextSymbolBlockId{0};

template <class F>
static bool someList(std::span<Node* const> ts, F&& f) {
	for (Node* t : ts) {
		if (f(t)) {
			return true;
		}
	}
	return false;
}

NodeId getNodeId(Node* node) {
	NodeId id = node->id.load();
	if (id == 0) {
		id = nextNodeId.fetch_add(1) + 1;
		NodeId expected = 0;
		if (!node->id.compare_exchange_strong(expected, id)) {
			id = expected;
		}
	}
	return id;
}

SymbolId getSymbolId(Symbol* symbol) {
	SymbolId id = symbol->id.load();
	if (id == 0) {
		id = nextSymbolId.fetch_add(1) + 1;
		SymbolId expected = 0;
		if (!symbol->id.compare_exchange_strong(expected, id)) {
			id = expected;
		}
	}
	return id;
}

// NodeIdGenerator / SymbolIdGenerator (utilities.go): each link store owns a
// generator that hands out IDs in blocks of BlockIdSize grabbed from the
// central atomic counters above; IDs are offset by BlockIdOffset so the dense
// paged store can index them directly.

NodeId NodeIdGenerator::GetNodeId(Node* node) {
	uint64_t id = node->id.load();
	if (id == 0) {
		if (nextId == lastId) {
			nextId = nextNodeBlockId.fetch_add(BlockIdSize);
			lastId = nextId + BlockIdSize;
		}
		id = nextId + BlockIdOffset;
		nextId++;
		uint64_t expected = 0;
		if (!node->id.compare_exchange_strong(expected, id)) {
			id = expected;
		}
	}
	return static_cast<NodeId>(id);
}

SymbolId SymbolIdGenerator::GetSymbolId(Symbol* symbol) {
	uint64_t id = symbol->id.load();
	if (id == 0) {
		if (nextId == lastId) {
			nextId = nextSymbolBlockId.fetch_add(BlockIdSize);
			lastId = nextId + BlockIdSize;
		}
		id = nextId + BlockIdOffset;
		nextId++;
		uint64_t expected = 0;
		if (!symbol->id.compare_exchange_strong(expected, id)) {
			id = expected;
		}
	}
	return static_cast<SymbolId>(id);
}

SymbolTable& getSymbolTable(SymbolTable& data) { return data; }
SymbolTable& getMembers(Symbol* symbol) { return symbol->data->members; }
SymbolTable& getExports(Symbol* symbol) { return symbol->data->exports; }
SymbolTable& getLocals(Node* container) {
	return *container->localsContainerData().locals;
}

Node* getRootDeclaration(Node* node) {
	while (node->kind == Kind::BindingElement) node = node->parent->parent;
	return node;
}

NodeFlags getCombinedNodeFlags(Node* node) {
	node = getRootDeclaration(node);
	NodeFlags flags = node->flags;
	if (node->kind == Kind::VariableDeclaration) node = node->parent;
	if (node && node->kind == Kind::VariableDeclarationList) {
		flags |= node->flags;
		node = node->parent;
	}
	if (node && node->kind == Kind::VariableStatement) flags |= node->flags;
	return flags;
}

ModifierFlags getCombinedModifierFlags(Node* node) {
	node = getRootDeclaration(node);
	ModifierFlags flags = node->modifierFlags();
	if (node->kind == Kind::VariableDeclaration) node = node->parent;
	if (node && node->kind == Kind::VariableDeclarationList) {
		flags |= node->modifierFlags();
		node = node->parent;
	}
	if (node && node->kind == Kind::VariableStatement)
		flags |= node->modifierFlags();
	return flags;
}

bool isDeclarationStatementKind(Kind kind) {
	switch (kind) {
	case Kind::FunctionDeclaration:
	case Kind::MissingDeclaration:
	case Kind::ClassDeclaration:
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::EnumDeclaration:
	case Kind::ModuleDeclaration:
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::ImportEqualsDeclaration:
	case Kind::ExportDeclaration:
	case Kind::ExportAssignment:
	case Kind::NamespaceExportDeclaration:
		return true;
	}
	return false;
}

bool isStatementKindButNotDeclarationKind(Kind kind) {
	return kind >= KindFirstStatement && kind <= KindLastStatement &&
	       !isDeclarationStatementKind(kind);
}

bool isBlockStatement(Node* node) {
	if (node->kind != Kind::Block) return false;
	if (node->parent &&
	    (node->parent->kind == Kind::TryStatement ||
	     node->parent->kind == Kind::CatchClause))
		return false;
	return !isFunctionLike(node->parent);
}

bool isStatement(Node* node) {
	Kind kind = node->kind;
	return isStatementKindButNotDeclarationKind(kind) ||
	       isDeclarationStatementKind(kind) || isBlockStatement(node);
}

bool isDeclarationStatement(Node* node) {
	return isDeclarationStatementKind(node->kind);
}

bool isClassElement(Node* node) {
	switch (node->kind) {
	case Kind::Constructor:
	case Kind::PropertyDeclaration:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::IndexSignature:
	case Kind::ClassStaticBlockDeclaration:
	case Kind::SemicolonClassElement:
		return true;
	}
	return false;
}

bool isClassLike(Node* node) {
	return node && (node->kind == Kind::ClassDeclaration ||
	                node->kind == Kind::ClassExpression);
}

bool isFunctionExpressionOrArrowFunction(Node* node) {
	return isFunctionExpression(node) || isArrowFunction(node);
}

bool isObjectLiteralOrClassExpressionMethodOrAccessor(Node* node) {
	Kind kind = node->kind;
	return (kind == Kind::MethodDeclaration || kind == Kind::GetAccessor ||
	        kind == Kind::SetAccessor) &&
	       node->parent &&
	       (node->parent->kind == Kind::ObjectLiteralExpression ||
	        node->parent->kind == Kind::ClassExpression);
}

bool isObjectLiteralMethod(Node* node) {
	return node && node->kind == Kind::MethodDeclaration && node->parent &&
	       node->parent->kind == Kind::ObjectLiteralExpression;
}

bool isEnumConst(Node* node) {
	return (getCombinedModifierFlags(node) & ModifierFlagsConst) != 0;
}

bool isAutoAccessorPropertyDeclaration(Node* node) {
	return isPropertyDeclaration(node) && hasAccessorModifier(node);
}

bool isStatic(Node* node) {
	return (isClassElement(node) && hasStaticModifier(node)) ||
	       isClassStaticBlockDeclaration(node);
}

Node* getThisContainer(Node* node, bool includeArrowFunctions,
                       bool includeClassComputedPropertyName) {
	for (;;) {
		node = node->parent;
		switch (node->kind) {
		case Kind::ComputedPropertyName:
			if (includeClassComputedPropertyName &&
			    node->parent && node->parent->parent &&
			    isClassLike(node->parent->parent)) {
				return node;
			}
			node = node->parent->parent;
			break;
		case Kind::Decorator:
			if (node->parent->kind == Kind::Parameter &&
			    isClassElement(node->parent->parent)) {
				node = node->parent->parent;
			} else if (isClassElement(node->parent)) {
				node = node->parent;
			}
			break;
		case Kind::ArrowFunction:
			if (includeArrowFunctions) return node;
			break;
		case Kind::FunctionDeclaration:
		case Kind::FunctionExpression:
		case Kind::ModuleDeclaration:
		case Kind::ClassStaticBlockDeclaration:
		case Kind::PropertyDeclaration:
		case Kind::PropertySignature:
		case Kind::MethodDeclaration:
		case Kind::MethodSignature:
		case Kind::Constructor:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
		case Kind::CallSignature:
		case Kind::ConstructSignature:
		case Kind::IndexSignature:
		case Kind::EnumDeclaration:
		case Kind::SourceFile:
			return node;
		default:
			break;
		}
	}
}

bool isInTopLevelContext(Node* node) {
	if (isIdentifier(node)) {
		Node* parent = node->parent;
		if ((isClassDeclaration(parent) || isFunctionDeclaration(parent)) &&
		    parent->name() == node) {
			node = parent;
		}
	}
	Node* container =
		getThisContainer(node, /*includeArrowFunctions*/ true,
	                     /*includeClassComputedPropertyName*/ false);
	return isSourceFile(container);
}

bool isIdentifierName(Node* node) {
	Node* parent = node->parent;
	if (!parent) return false;
	switch (parent->kind) {
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::EnumMember:
	case Kind::PropertyAssignment:
	case Kind::PropertyAccessExpression:
		return parent->name() == node;
	case Kind::QualifiedName:
		return parent->as<QualifiedName>()->Right == node;
	case Kind::BindingElement:
		return parent->propertyName() == node;
	case Kind::ImportSpecifier:
		return parent->propertyName() == node;
	case Kind::ExportSpecifier:
	case Kind::JsxAttribute:
	case Kind::JsxSelfClosingElement:
	case Kind::JsxOpeningElement:
	case Kind::JsxClosingElement:
		return true;
	default:
		return false;
	}
}

Node* findAncestor(Node* node, const std::function<bool(Node*)>& callback) {
	while (node) {
		if (callback(node)) return node;
		node = node->parent;
	}
	return nullptr;
}

bool isPrologueDirective(Node* node) {
	return node->kind == Kind::ExpressionStatement &&
	       node->expression()->kind == Kind::StringLiteral;
}

bool isDottedName(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
	case Kind::ThisKeyword:
	case Kind::SuperKeyword:
	case Kind::MetaProperty:
		return true;
	case Kind::PropertyAccessExpression:
	case Kind::ParenthesizedExpression:
		return isDottedName(node->expression());
	default:
		return false;
	}
}

bool isPushOrUnshiftIdentifier(Node* node) {
	std::string scratch;
	std::string_view text = node->textView(scratch);
	return text == "push" || text == "unshift";
}

bool isDestructuringAssignment(Node* node) {
	if (isAssignmentExpression(node, /*excludeCompoundAssignment*/ true)) {
		Kind kind = node->as<BinaryExpression>()->Left->kind;
		return kind == Kind::ObjectLiteralExpression ||
		       kind == Kind::ArrayLiteralExpression;
	}
	return false;
}

bool isExpressionOfOptionalChainRoot(Node* node) {
	return node->parent && isOptionalChainRoot(node->parent) &&
	       node->parent->expression() == node;
}

bool isNullishCoalesce(Node* node) {
	return node->kind == Kind::BinaryExpression &&
	       node->as<BinaryExpression>()->OperatorToken->kind ==
	           Kind::QuestionQuestionToken;
}

bool isDynamicName(Node* name) {
	Node* expr = nullptr;
	switch (name->kind) {
	case Kind::ComputedPropertyName:
		expr = name->expression();
		break;
	case Kind::ElementAccessExpression:
		expr = skipParentheses(
			name->as<ElementAccessExpression>()->ArgumentExpression);
		break;
	default:
		return false;
	}
	return !isStringOrNumericLiteralLike(expr) &&
	       !isSignedNumericLiteral(expr);
}

bool hasDynamicName(Node* declaration) {
	Node* name = getNameOfDeclaration(declaration);
	return name && isDynamicName(name);
}

bool isPropertyNameLiteral(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::NumericLiteral:
		return true;
	default:
		return false;
	}
}

Node* getPropertyNameForPropertyOrComputedPropertyName(Node* name) {
	return name->kind == Kind::ComputedPropertyName
	           ? name->as<ComputedPropertyName>()->Expression
	           : name;
}

bool isLogicalBinaryOperator(Kind token) {
	return token == Kind::BarBarToken || token == Kind::AmpersandAmpersandToken;
}

bool isLogicalOrCoalescingBinaryOperator(Kind token) {
	return isLogicalBinaryOperator(token) || token == Kind::QuestionQuestionToken;
}

bool isLogicalOrCoalescingBinaryExpression(Node* expr) {
	return isBinaryExpression(expr) &&
	       isLogicalOrCoalescingBinaryOperator(
	           expr->as<BinaryExpression>()->OperatorToken->kind);
}

bool isLogicalExpression(Node* node) {
	for (;;) {
		if (node->kind == Kind::ParenthesizedExpression) {
			node = node->expression();
		} else if (node->kind == Kind::PrefixUnaryExpression &&
		           node->as<PrefixUnaryExpression>()->Operator ==
		               Kind::ExclamationToken) {
			node = node->as<PrefixUnaryExpression>()->Operand;
		} else {
			return isLogicalOrCoalescingBinaryExpression(node);
		}
	}
}

bool isLogicalOrCoalescingAssignmentOperator(Kind token) {
	return token == Kind::AmpersandAmpersandEqualsToken ||
	       token == Kind::BarBarEqualsToken ||
	       token == Kind::QuestionQuestionEqualsToken;
}

bool isLogicalOrCoalescingAssignmentExpression(Node* expr) {
	return isBinaryExpression(expr) &&
	       isLogicalOrCoalescingAssignmentOperator(
	           expr->as<BinaryExpression>()->OperatorToken->kind);
}

bool isBooleanLiteral(Node* node) {
	return node->kind == Kind::TrueKeyword || node->kind == Kind::FalseKeyword;
}

Node* getImmediatelyInvokedFunctionExpression(Node* fn) {
	if (isFunctionExpressionOrArrowFunction(fn)) {
		Node* prev = fn;
		Node* parent = fn->parent;
		while (isParenthesizedExpression(parent)) {
			prev = parent;
			parent = parent->parent;
		}
		if (parent && isCallExpression(parent) &&
		    parent->expression() == prev) {
			return parent;
		}
	}
	return nullptr;
}

bool isExpandoInitializer(Node* declaration, Node* initializer) {
	if (!initializer) return false;
	if (isFunctionExpressionOrArrowFunction(initializer)) return true;
	if (isInJSFile(initializer)) {
		return isClassExpression(initializer) ||
		       (isObjectLiteralExpression(initializer) &&
		        initializer->properties().empty() &&
		        declaration->type() == nullptr);
	}
	return false;
}

bool isVariableDeclarationInitializedToRequire(Node* node) {
	if (node->kind == Kind::BindingElement) node = node->parent->parent;
	return isVariableDeclarationInitializedWithRequireHelper(
		node, /*allowAccessedRequire*/ false);
}

bool isVariableDeclarationInitializedWithRequireHelper(
	Node* node, bool allowAccessedRequire) {
	if (!isInJSFile(node)) return false;
	if (node->kind != Kind::VariableDeclaration) return false;
	Node* initializer = node->initializer();
	if (!initializer) return false;
	if (allowAccessedRequire)
		initializer = getLeftmostAccessExpression(initializer);
	return (node->parent->parent->modifierFlags() & ModifierFlagsExport) == 0 &&
	       node->type() == nullptr &&
	       isRequireCall(initializer, /*requireStringLiteralLikeArgument*/ true);
}

bool isPotentiallyExecutableNode(Node* node) {
	if (node->kind >= KindFirstStatement &&
	    node->kind <= KindLastStatement) {
		if (isVariableStatement(node)) {
			Node* declarationList =
				node->as<VariableStatement>()->DeclarationList;
			if (getCombinedNodeFlags(declarationList) &
			    NodeFlagsBlockScoped) {
				return true;
			}
			for (Node* d : declarationList->as<VariableDeclarationList>()
			                  ->Declarations->nodes) {
				if (d->initializer()) return true;
			}
			return false;
		}
		return true;
	}
	return isClassDeclaration(node) || isEnumDeclaration(node) ||
	       isModuleDeclaration(node);
}

bool isCatchClauseVariableDeclarationOrBindingElement(Node* declaration) {
	Node* node = getRootDeclaration(declaration);
	return node->kind == Kind::VariableDeclaration && node->parent &&
	       node->parent->kind == Kind::CatchClause;
}

bool isBlockOrCatchScoped(Node* declaration) {
	return (getCombinedNodeFlags(declaration) & NodeFlagsBlockScoped) != 0 ||
	       isCatchClauseVariableDeclarationOrBindingElement(declaration);
}

bool isPartOfParameterDeclaration(Node* node) {
	return getRootDeclaration(node)->kind == Kind::Parameter;
}

bool isParameterPropertyDeclaration(Node* node, Node* parent) {
	return isParameterDeclaration(node) &&
	       hasSyntacticModifier(node, ModifierFlagsParameterPropertyModifier) &&
	       parent->kind == Kind::Constructor;
}

bool isAsyncFunction(Node* node) {
	switch (node->kind) {
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::MethodDeclaration: {
		auto data = node->bodyData();
		return data.body && *data.body && *data.asteriskToken == nullptr &&
		       hasSyntacticModifier(node, ModifierFlagsAsync);
	}
	default:
		return false;
	}
}

bool isBindingPattern(Node* node) {
	return node && (node->kind == Kind::ObjectBindingPattern ||
	                node->kind == Kind::ArrayBindingPattern);
}

bool isForInOrOfStatement(Node* node) {
	return node && (node->kind == Kind::ForInStatement ||
	                node->kind == Kind::ForOfStatement);
}

Node* getAssignmentTarget(Node* node) {
	for (;;) {
		Node* parent = node->parent;
		if (!parent) return nullptr;
		switch (parent->kind) {
		case Kind::BinaryExpression: {
			auto* b = parent->as<BinaryExpression>();
			if (isAssignmentOperator(b->OperatorToken->kind) &&
			    b->Left == node) {
				return parent;
			}
			return nullptr;
		}
		case Kind::PrefixUnaryExpression: {
			auto* p = parent->as<PrefixUnaryExpression>();
			if (p->Operator == Kind::PlusPlusToken ||
			    p->Operator == Kind::MinusMinusToken)
				return parent;
			return nullptr;
		}
		case Kind::PostfixUnaryExpression: {
			auto* p = parent->as<PostfixUnaryExpression>();
			if (p->Operator == Kind::PlusPlusToken ||
			    p->Operator == Kind::MinusMinusToken)
				return parent;
			return nullptr;
		}
		case Kind::ForInStatement:
		case Kind::ForOfStatement:
			if (parent->initializer() == node) return parent;
			return nullptr;
		case Kind::ParenthesizedExpression:
		case Kind::ArrayLiteralExpression:
		case Kind::SpreadElement:
		case Kind::NonNullExpression:
			node = parent;
			break;
		case Kind::SpreadAssignment:
			node = parent->parent;
			break;
		case Kind::ShorthandPropertyAssignment:
			if (parent->as<ShorthandPropertyAssignment>()->name != node)
				return nullptr;
			node = parent->parent;
			break;
		case Kind::PropertyAssignment:
			if (parent->as<PropertyAssignment>()->name == node)
				return nullptr;
			node = parent->parent;
			break;
		default:
			return nullptr;
		}
	}
}

bool isAssignmentTarget(Node* node) { return getAssignmentTarget(node) != nullptr; }

bool isMethodOrAccessor(Node* node) {
	switch (node->kind) {
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		return true;
	default:
		return false;
	}
}

bool isPrivateIdentifierClassElementDeclaration(Node* node) {
	return (isPropertyDeclaration(node) || isMethodOrAccessor(node)) &&
	       isPrivateIdentifier(node->name());
}

Node* getLeftmostAccessExpression(Node* expr) {
	while (isAccessExpression(expr)) expr = expr->expression();
	return expr;
}

std::pair<std::string, bool> tryGetAmbientModuleNameFromSymbolName(
	const std::string& s) {
	if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
		return {s.substr(1, s.size() - 2), true};
	static const std::string patternPrefix =
		std::string(1, kInternalSymbolNamePrefix) + "\"";
	if (s.compare(0, patternPrefix.size(), patternPrefix) != 0)
		return {"", false};
	std::string rest = s.substr(patternPrefix.size());
	size_t marker = rest.rfind("\"pattern@");
	if (marker == std::string::npos || marker < 1) return {"", false};
	return {rest.substr(0, marker), true};
}

bool isAmbientModuleSymbolName(const std::string& s) {
	return tryGetAmbientModuleNameFromSymbolName(s).second;
}

bool isExternalOrCommonJSModule(SourceFile* file) {
	return file->ExternalModuleIndicator != nullptr ||
	       file->CommonJSModuleIndicator != nullptr;
}

bool isJsonSourceFile(SourceFile* file) {
	return file->ScriptKind == ScriptKind::JSON;
}

// --- Module instance state ---

// ancestors is a virtual-parent stack: push/pop return updated copies, as in
// Go where the slice header is passed by value.
static std::vector<Node*> pushAncestor(std::vector<Node*> ancestors,
                                       Node* parent) {
	ancestors.push_back(parent);
	return ancestors;
}

static std::pair<std::vector<Node*>, Node*> popAncestor(
	std::vector<Node*> ancestors, Node* node) {
	if (ancestors.empty()) return {ancestors, node->parent};
	Node* top = ancestors.back();
	ancestors.pop_back();
	return {ancestors, top};
}

using ModuleVisited = std::unordered_map<NodeId, ModuleInstanceState>;

bool nodeHasName(Node* statement, Node* id) {
	Node* name = statement->name();
	if (name) return isIdentifier(name) && name->text() == id->text();
	if (isVariableStatement(statement)) {
		for (Node* d : statement->as<VariableStatement>()
		                  ->DeclarationList->as<VariableDeclarationList>()
		                  ->Declarations->nodes) {
			if (nodeHasName(d, id)) return true;
		}
	}
	return false;
}

static ModuleInstanceState getModuleInstanceStateCached(
	Node* node, std::vector<Node*> ancestors, ModuleVisited* visited);

static ModuleInstanceState getModuleInstanceState(Node* node,
                                                  std::vector<Node*> ancestors,
                                                  ModuleVisited* visited);

static ModuleInstanceState getModuleInstanceStateForAliasTarget(
	Node* node, std::vector<Node*> ancestors, ModuleVisited* visited) {
	Node* name = node->propertyNameOrName();
	if (name->kind != Kind::Identifier)
		return ModuleInstanceState::Instantiated;
	for (;;) {
		auto popped = popAncestor(ancestors, node);
		ancestors = popped.first;
		Node* p = popped.second;
		if (!p) break;
		if (isBlock(p) || isModuleBlock(p) || isSourceFile(p)) {
			ModuleInstanceState found = ModuleInstanceState::Unknown;
			auto statementsAncestors = pushAncestor(ancestors, p);
			for (Node* statement : p->statements()) {
				if (nodeHasName(statement, name)) {
					ModuleInstanceState state = getModuleInstanceStateCached(
						statement, statementsAncestors, visited);
					if (found == ModuleInstanceState::Unknown ||
					    state > found)
						found = state;
					if (found == ModuleInstanceState::Instantiated)
						return found;
					if (statement->kind == Kind::ImportEqualsDeclaration)
						found = ModuleInstanceState::Instantiated;
				}
			}
			if (found != ModuleInstanceState::Unknown) return found;
		}
		node = p;
	}
	return ModuleInstanceState::Instantiated;
}

static ModuleInstanceState getModuleInstanceStateWorker(
	Node* node, std::vector<Node*> ancestors, ModuleVisited* visited) {
	switch (node->kind) {
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
		return ModuleInstanceState::NonInstantiated;
	case Kind::EnumDeclaration:
		if (isEnumConst(node)) return ModuleInstanceState::ConstEnumOnly;
		break;
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::ImportEqualsDeclaration:
		if (!hasSyntacticModifier(node, ModifierFlagsExport))
			return ModuleInstanceState::NonInstantiated;
		break;
	case Kind::ExportDeclaration: {
		auto* decl = node->as<ExportDeclaration>();
		if (decl->ModuleSpecifier == nullptr && decl->ExportClause &&
		    decl->ExportClause->kind == Kind::NamedExports) {
			ModuleInstanceState state = ModuleInstanceState::NonInstantiated;
			ancestors = pushAncestor(ancestors, node);
			ancestors = pushAncestor(ancestors, decl->ExportClause);
			for (Node* specifier : decl->ExportClause->elements()) {
				ModuleInstanceState specifierState =
					getModuleInstanceStateForAliasTarget(specifier, ancestors,
					                                     visited);
				if (specifierState > state) state = specifierState;
				if (state == ModuleInstanceState::Instantiated) return state;
			}
			return state;
		}
		break;
	}
	case Kind::ModuleBlock: {
		ModuleInstanceState state = ModuleInstanceState::NonInstantiated;
		ancestors = pushAncestor(ancestors, node);
		node->forEachChild([&](Node* n) {
			ModuleInstanceState childState =
				getModuleInstanceStateCached(n, ancestors, visited);
			switch (childState) {
			case ModuleInstanceState::NonInstantiated:
				return false;
			case ModuleInstanceState::ConstEnumOnly:
				state = ModuleInstanceState::ConstEnumOnly;
				return false;
			case ModuleInstanceState::Instantiated:
				state = ModuleInstanceState::Instantiated;
				return true;
			}
			return true;
		});
		return state;
	}
	case Kind::ModuleDeclaration:
		return getModuleInstanceState(node, ancestors, visited);
	default:
		break;
	}
	return ModuleInstanceState::Instantiated;
}

static ModuleInstanceState getModuleInstanceStateCached(
	Node* node, std::vector<Node*> ancestors, ModuleVisited* visited) {
	NodeId nodeId = getNodeId(node);
	auto it = visited->find(nodeId);
	if (it != visited->end()) {
		if (it->second != ModuleInstanceState::Unknown) return it->second;
		return ModuleInstanceState::NonInstantiated;
	}
	(*visited)[nodeId] = ModuleInstanceState::Unknown;
	ModuleInstanceState result =
		getModuleInstanceStateWorker(node, ancestors, visited);
	(*visited)[nodeId] = result;
	return result;
}

static ModuleInstanceState getModuleInstanceState(
	Node* node, std::vector<Node*> ancestors, ModuleVisited* visited) {
	auto* module = node->as<ModuleDeclaration>();
	if (module->Body != nullptr)
		return getModuleInstanceStateCached(module->Body,
		                                    pushAncestor(ancestors, node),
		                                    visited);
	return ModuleInstanceState::Instantiated;
}

ModuleInstanceState getModuleInstanceState(Node* node) {
	ModuleVisited visited;
	return getModuleInstanceState(node, {}, &visited);
}

bool isInstantiatedModule(Node* node, bool preserveConstEnums) {
	ModuleInstanceState moduleState = getModuleInstanceState(node);
	return moduleState == ModuleInstanceState::Instantiated ||
	       (preserveConstEnums &&
	        moduleState == ModuleInstanceState::ConstEnumOnly);
}

// --- symbol.go helpers ---

std::string symbolName(const Symbol* symbol) {
	if (symbol->data->valueDeclaration &&
	    isPrivateIdentifierClassElementDeclaration(symbol->data->valueDeclaration)) {
		return symbol->data->valueDeclaration->name()->text();
	}
	return symbol->data->name;
}

std::string escapeAllInternalSymbolNames(std::string_view name) {
	std::string out;
	out.reserve(name.size());
	for (size_t i = 0; i < name.size(); i++) {
		if ((char)name[i] == kInternalSymbolNamePrefix) {
			out += "__";
		} else {
			out += name[i];
		}
	}
	return out;
}

std::string escapeInternalSymbolName(std::string_view name) {
	if (!name.empty() && (char)name[0] == kInternalSymbolNamePrefix)
		return "__" + std::string(name.substr(1));
	return std::string(name);
}

std::string escapeSymbolName(std::string_view name) {
	if (!name.empty() && (char)name[0] == kInternalSymbolNamePrefix)
		return "__" + std::string(name.substr(1));
	if (name.size() >= 2 && name[0] == '_' && name[1] == '_')
		return "_" + std::string(name);
	return std::string(name);
}

} // namespace tsc
namespace tsc {

// appended: symbol-capacity + misc predicates needed by binder
bool canHaveSymbol(Node* node) {
	switch (node->kind) {
	case Kind::ArrowFunction:
	case Kind::BinaryExpression:
	case Kind::BindingElement:
	case Kind::CallExpression:
	case Kind::CallSignature:
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::ClassStaticBlockDeclaration:
	case Kind::Constructor:
	case Kind::ConstructorType:
	case Kind::ConstructSignature:
	case Kind::ElementAccessExpression:
	case Kind::EnumDeclaration:
	case Kind::EnumMember:
	case Kind::ExportAssignment:
	case Kind::ExportDeclaration:
	case Kind::ExportSpecifier:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::FunctionType:
	case Kind::GetAccessor:
	case Kind::ImportClause:
	case Kind::ImportEqualsDeclaration:
	case Kind::ImportSpecifier:
	case Kind::IndexSignature:
	case Kind::InterfaceDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::JsxAttribute:
	case Kind::JsxAttributes:
	case Kind::JsxSpreadAttribute:
	case Kind::MappedType:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::ModuleDeclaration:
	case Kind::NamedTupleMember:
	case Kind::NamespaceExport:
	case Kind::NamespaceExportDeclaration:
	case Kind::NamespaceImport:
	case Kind::NewExpression:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::NumericLiteral:
	case Kind::ObjectLiteralExpression:
	case Kind::Parameter:
	case Kind::PropertyAccessExpression:
	case Kind::PropertyAssignment:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::SetAccessor:
	case Kind::ShorthandPropertyAssignment:
	case Kind::SourceFile:
	case Kind::SpreadAssignment:
	case Kind::StringLiteral:
	case Kind::TypeAliasDeclaration:
	case Kind::TypeLiteral:
	case Kind::TypeParameter:
	case Kind::VariableDeclaration:
		return true;
	default:
		return false;
	}
}

bool hasQuestionToken(Node* node) { return isQuestionToken(node->questionToken()); }

bool hasStaticModifier(Node* node) {
	return hasSyntacticModifier(node, ModifierFlagsStatic);
}

bool hasAccessorModifier(Node* node) {
	return hasSyntacticModifier(node, ModifierFlagsAccessor);
}

bool isPrivateIdentifierOrExpression(Node* node) {
	return isPrivateIdentifier(node);
}

} // namespace tsc
namespace tsc {

bool isPartOfTypeQuery(Node* node) {
	while (node->kind == Kind::QualifiedName || node->kind == Kind::Identifier)
		node = node->parent;
	return node->kind == Kind::TypeQuery;
}

Node* getContainingClass(Node* node) {
	return findAncestor(node->parent, [](Node* n) { return isClassLike(n); });
}

bool isModuleAugmentationExternal(Node* node) {
	switch (node->parent->kind) {
	case Kind::SourceFile:
		return isExternalModule(node->parent->as<SourceFile>());
	case Kind::ModuleBlock: {
		Node* grandParent = node->parent->parent;
		return isAmbientModule(grandParent) &&
		       isSourceFile(grandParent->parent) &&
		       !isExternalModule(grandParent->parent->as<SourceFile>());
	}
	default:
		return false;
	}
}

bool isImplicitlyExportedJSDocDeclaration(Node* node) {
	if (!node->parent || !isSourceFile(node->parent) ||
	    !isExternalOrCommonJSModule(node->parent->as<SourceFile>()))
		return false;
	if (isJSTypeAliasDeclaration(node)) return true;
	return isModuleDeclaration(node) &&
	       (node->flags & NodeFlagsReparsed) != 0;
}

// utilities.go:2158 — EntityNameToString
std::string EntityNameToString(
	Node* name, const std::function<std::string(const Node*)>& getTextOfNode) {
	switch (name->kind) {
	case Kind::ThisKeyword:
		return "this";
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
		if (nodeIsSynthesized(name) || getTextOfNode == nullptr) {
			return name->text();
		}
		return getTextOfNode(name);
	case Kind::QualifiedName:
		return EntityNameToString(name->as<QualifiedName>()->Left, getTextOfNode) +
			"." +
			EntityNameToString(name->as<QualifiedName>()->Right, getTextOfNode);
	case Kind::PropertyAccessExpression:
		return EntityNameToString(name->expression(), getTextOfNode) + "." +
			EntityNameToString(name->as<PropertyAccessExpression>()->name,
							   getTextOfNode);
	case Kind::JsxNamespacedName:
		return EntityNameToString(name->as<JsxNamespacedName>()->Namespace,
								  getTextOfNode) +
			":" +
			EntityNameToString(name->as<JsxNamespacedName>()->name, getTextOfNode);
	}
	TSC_UNREACHABLE("Unhandled case in EntityNameToString");
}

} // namespace tsc

namespace tsc {

bool moduleExportNameIsDefault(Node* node) {
	std::string scratch;
	return node->textView(scratch) == InternalSymbolNameDefault;
}

bool expressionIsAlias(Node* node) {
	return isEntityNameExpression(node) || isClassExpression(node);
}


bool isModuleOrEnumDeclaration(Node* node) {
	return node->kind == Kind::ModuleDeclaration || node->kind == Kind::EnumDeclaration;
}

bool isGlobalSourceFile(Node* node) {
	return node->kind == Kind::SourceFile && !isExternalOrCommonJSModule(node->as<SourceFile>());
}

bool isConstTypeReference(Node* node) {
	if (!(isTypeReferenceNode(node) && node->typeArguments().empty() &&
	      isIdentifier(node->as<TypeReferenceNode>()->TypeName))) {
		return false;
	}
	std::string scratch;
	return node->as<TypeReferenceNode>()->TypeName->textView(scratch) == "const";
}

bool isConstAssertion(Node* node) {
	switch (node->kind) {
	case Kind::AsExpression:
	case Kind::TypeAssertionExpression:
		return isConstTypeReference(node->type());
	}
	return false;
}

Node* getDeclarationOfKind(Symbol* symbol, Kind kind) {
	for (Node* declaration : symbol->data->declarations) {
		if (declaration->kind == kind) {
			return declaration;
		}
	}
	return nullptr;
}

Node* findConstructorDeclaration(Node* node) {
	for (Node* member : node->members()) {
		if (isConstructorDeclaration(member) && nodeIsPresent(member->body())) {
			return member;
		}
	}
	return nullptr;
}

bool isNonLocalAlias(Symbol* symbol, SymbolFlags excludes) {
	if (symbol == nullptr) {
		return false;
	}
	return (symbol->flags & (SymbolFlagsAlias | excludes)) == SymbolFlagsAlias ||
		((symbol->flags & SymbolFlagsAlias) != 0 &&
		 (symbol->flags & SymbolFlagsAssignment) != 0);
}

bool isPlainJSFile(SourceFile* file, Tristate checkJs) {
	return file != nullptr &&
		(file->ScriptKind == ScriptKind::JS || file->ScriptKind == ScriptKind::JSX) &&
		file->CheckJsDirective == nullptr && checkJs == Tristate::Unknown;
}

bool nodeKindIs(Node* node, Kind k1) {
	return node->kind == k1;
}

bool nodeKindIs(Node* node, Kind k1, Kind k2) {
	return node->kind == k1 || node->kind == k2;
}

bool nodeKindIs(Node* node, Kind k1, Kind k2, Kind k3) {
	return node->kind == k1 || node->kind == k2 || node->kind == k3;
}

bool nodeKindIs(Node* node, std::initializer_list<Kind> kinds) {
	for (Kind k : kinds) {
		if (node->kind == k) {
			return true;
		}
	}
	return false;
}

bool isAliasSymbolDeclaration(Node* node) {
	switch (node->kind) {
	case Kind::ImportEqualsDeclaration:
	case Kind::NamespaceExportDeclaration:
	case Kind::NamespaceImport:
	case Kind::NamespaceExport:
	case Kind::ImportSpecifier:
	case Kind::ExportSpecifier:
		return true;
	case Kind::ImportClause:
		return node->as<ImportClause>()->name != nullptr;
	case Kind::ExportAssignment:
		return expressionIsAlias(node->as<ExportAssignment>()->Expression);
	case Kind::VariableDeclaration:
	case Kind::BindingElement:
		return isVariableDeclarationInitializedToRequire(node);
	case Kind::BinaryExpression:
		switch (getAssignmentDeclarationKind(node)) {
		case JSDeclarationKind::ModuleExports:
		case JSDeclarationKind::ExportsProperty:
			return expressionIsAlias(node->as<BinaryExpression>()->Right);
		default:
			break;
		}
	}
	return false;
}

Node* getImportAttributes(Node* node) {
	switch (node->kind) {
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
		return node->as<ImportDeclaration>()->Attributes;
	case Kind::ExportDeclaration:
		return node->as<ExportDeclaration>()->Attributes;
	case Kind::ImportType:
		return node->as<ImportTypeNode>()->Attributes;
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in getImportAttributes");
}

Diagnostic* newDiagnosticChain(Diagnostic* chain, const DiagnosticMessage* message,
							   const std::vector<std::string>& args) {
	if (chain != nullptr) {
		return newDiagnostic(chain->file, chain->loc, message, args)
			->AddMessageChain(chain)
			->SetRelatedInfo(chain->relatedInformation);
	}
	return newDiagnostic(nullptr, TextRange{}, message, args);
}


// Walks up the parents of a node to find the ancestor that matches the kind.
Node* findAncestorKind(Node* node, Kind kind) {
	for (; node != nullptr; node = node->parent) {
		if (node->kind == kind) {
			return node;
		}
	}
	return nullptr;
}

// Walks up the parents of a node to find the ancestor that matches the callback.
Node* findAncestorOrQuit(
	Node* node, const std::function<FindAncestorResult(Node*)>& callback) {
	for (; node != nullptr; node = node->parent) {
		switch (callback(node)) {
		case FindAncestorResult::Quit:
			return nullptr;
		case FindAncestorResult::True:
			return node;
		default:
			break;
		}
	}
	return nullptr;
}

bool isNodeDescendantOf(Node* node, Node* ancestor) {
	for (; node != nullptr; node = node->parent) {
		if (node == ancestor) {
			return true;
		}
	}
	return false;
}

bool isFunctionLikeOrClassStaticBlockDeclaration(Node* node) {
	return node != nullptr && (isFunctionLike(node) || isClassStaticBlockDeclaration(node));
}

FunctionFlags getFunctionFlags(Node* node) {
	if (node == nullptr) {
		return FunctionFlagsInvalid;
	}
	BodyDataRef data = node->bodyData();
	if (data.body == nullptr) {
		return FunctionFlagsInvalid;
	}
	FunctionFlags flags = FunctionFlagsNormal;
	switch (node->kind) {
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::MethodDeclaration:
		if (*data.asteriskToken != nullptr) {
			flags |= FunctionFlagsGenerator;
		}
		[[fallthrough]];
	case Kind::ArrowFunction:
		if (hasSyntacticModifier(node, ModifierFlagsAsync)) {
			flags |= FunctionFlagsAsync;
		}
		break;
	default:
		break;
	}
	if (*data.body == nullptr) {
		flags |= FunctionFlagsInvalid;
	}
	return flags;
}


bool isComputedNonLiteralName(Node* name) {
	return isComputedPropertyName(name) &&
		   !isStringOrNumericLiteralLike(name->expression());
}

// utilities.go: IsDeclaration
bool isDeclaration(Node* node) {
	if (node->kind == Kind::TypeParameter) {
		return node->parent != nullptr;
	}
	return isDeclarationNode(node);
}

// utilities.go: IsDeclarationName — true if `name` is the name of a
// declaration node.
bool isDeclarationName(Node* name) {
	return !isSourceFile(name) && !isBindingPattern(name) &&
		   isDeclaration(name->parent) && name->parent->name() == name;
}

bool tryGetTextOfPropertyName(Node* name, std::string& out) {
	switch (name->kind) {
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
	case Kind::StringLiteral:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
		out = name->text();
		return true;
	case Kind::ComputedPropertyName:
		if (isStringOrNumericLiteralLike(name->expression())) {
			out = name->expression()->text();
			return true;
		}
		break;
	case Kind::JsxNamespacedName:
		out = name->as<JsxNamespacedName>()->Namespace->text() + ":" +
			  name->as<JsxNamespacedName>()->name->text();
		return true;
	default:
		break;
	}
	return false;
}

std::string getTextOfPropertyName(Node* name) {
	std::string text;
	tryGetTextOfPropertyName(name, text);
	return text;
}

bool isBlockScope(Node* node, Node* parentNode) {
	switch (node->kind) {
	case Kind::SourceFile:
	case Kind::CaseBlock:
	case Kind::CatchClause:
	case Kind::ModuleDeclaration:
	case Kind::ForStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::Constructor:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::PropertyDeclaration:
	case Kind::ClassStaticBlockDeclaration:
		return true;
	case Kind::Block:
		// function block is not considered block-scope container
		// see comment in binder.ts: bind(...), case for SyntaxKind.Block
		return !isFunctionLikeOrClassStaticBlockDeclaration(parentNode);
	default:
		break;
	}
	return false;
}

Node* getEnclosingBlockScopeContainer(Node* node) {
	return findAncestor(node->parent, [](Node* current) {
		return isBlockScope(current, current->parent);
	});
}


static NodeList* getHeritageClauses(Node* node) {
	switch (node->kind) {
	case Kind::ClassDeclaration:
		return node->as<ClassDeclaration>()->HeritageClauses;
	case Kind::ClassExpression:
		return node->as<ClassExpression>()->HeritageClauses;
	case Kind::InterfaceDeclaration:
		return node->as<InterfaceDeclaration>()->HeritageClauses;
	default:
		break;
	}
	return nullptr;
}

Node* getHeritageClause(Node* node, Kind kind) {
	if (NodeList* clauses = getHeritageClauses(node)) {
		for (Node* clause : clauses->nodes) {
			if (clause->as<HeritageClause>()->Token == kind) {
				return clause;
			}
		}
	}
	return nullptr;
}

std::vector<Node*> getHeritageElements(Node* node, Kind kind) {
	if (Node* clause = getHeritageClause(node, kind)) {
		return clause->as<HeritageClause>()->Types->nodes;
	}
	return {};
}

// Returns the expression or type name of a heritage clause element.
Node* getHeritageClauseElementName(Node* node) {
	if (isTypeReferenceNode(node)) {
		return node->as<TypeReferenceNode>()->TypeName;
	}
	return node->as<ExpressionWithTypeArguments>()->Expression;
}

// Ported with the checker bootstrap slice.

bool isTypeDeclaration(Node* node) {
	switch (node->kind) {
	case Kind::TypeParameter:
	case Kind::ClassDeclaration:
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::EnumDeclaration:
		return true;
	case Kind::ImportClause:
		return node->isTypeOnly() &&
		       node->as<ImportClause>()->name != nullptr;
	case Kind::ImportSpecifier:
	case Kind::ExportSpecifier:
		return node->parent->parent->isTypeOnly();
	default:
		return false;
	}
}

bool isTypeDeclarationName(Node* name) {
	return name->kind == Kind::Identifier && isTypeDeclaration(name->parent) &&
	       getNameOfDeclaration(name->parent) == name;
}

bool isTypeOnlyImportDeclaration(Node* node) {
	switch (node->kind) {
	case Kind::ImportSpecifier:
		return node->isTypeOnly() || node->parent->parent->isTypeOnly();
	case Kind::NamespaceImport:
		return node->parent->isTypeOnly();
	case Kind::ImportClause:
	case Kind::ImportEqualsDeclaration:
		return node->isTypeOnly();
	}
	return false;
}

static bool isTypeOnlyExportDeclaration(Node* node) {
	switch (node->kind) {
	case Kind::ExportSpecifier:
		return node->isTypeOnly() || node->parent->parent->isTypeOnly();
	case Kind::ExportDeclaration: {
		ExportDeclaration* d = node->as<ExportDeclaration>();
		return d->IsTypeOnly && d->ModuleSpecifier != nullptr &&
		       d->ExportClause == nullptr;
	}
	case Kind::NamespaceExport:
		return node->parent->isTypeOnly();
	}
	return false;
}

bool isTypeOnlyImportOrExportDeclaration(Node* node) {
	return isTypeOnlyImportDeclaration(node) || isTypeOnlyExportDeclaration(node);
}

bool isExclusivelyTypeOnlyImportOrExport(Node* node) {
	switch (node->kind) {
	case Kind::ExportDeclaration:
		return node->isTypeOnly();
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration: {
		if (Node* importClause = node->importClause()) {
			return importClause->as<ImportClause>()->isTypeOnly();
		}
		break;
	}
	case Kind::JSDocImportTag: {
		if (Node* importClause = node->importClause()) {
			return importClause->as<ImportClause>()->isTypeOnly();
		}
		break;
	}
	default:
		break;
	}
	return false;
}

bool isJsxTagName(Node* node) {
	Node* parent = node->parent;
	switch (parent->kind) {
	case Kind::JsxOpeningElement:
	case Kind::JsxClosingElement:
	case Kind::JsxSelfClosingElement:
		return parent->tagName() == node;
	}
	return false;
}

bool isJSDocLinkLike(Node* node) {
	return nodeKindIs(node, Kind::JSDocLink, Kind::JSDocLinkCode,
	                  Kind::JSDocLinkPlain);
}

bool isAssertionExpression(Node* node) {
	Kind kind = node->kind;
	return kind == Kind::TypeAssertionExpression || kind == Kind::AsExpression;
}

bool isPropertyAccessOrQualifiedName(Node* node) {
	return node->kind == Kind::PropertyAccessExpression ||
	       node->kind == Kind::QualifiedName;
}

static bool isPartOfTypeExpressionWithTypeArguments(Node* node) {
	Node* parent = node->parent;
	return (isHeritageClause(parent) &&
	           (!isClassLike(parent->parent) ||
	            parent->as<HeritageClause>()->Token == Kind::ImplementsKeyword)) ||
	       isJSDocImplementsTag(parent) || isJSDocAugmentsTag(parent);
}

static bool isPartOfTypeNodeInParent(Node* node) {
	Node* parent = node->parent;
	if (parent->kind == Kind::TypeQuery) {
		return false;
	}
	if (parent->kind == Kind::ImportType) {
		return !parent->as<ImportTypeNode>()->IsTypeOf;
	}
	// Do not recursively call isPartOfTypeNode on the parent. In the example:
	//
	//     let a: A.B.C;
	//
	// Calling isPartOfTypeNode would consider the qualified name A.B a type node.
	// Only C and A.B.C are type nodes.
	if (parent->kind >= KindFirstTypeNode && parent->kind <= KindLastTypeNode) {
		return true;
	}
	switch (parent->kind) {
	case Kind::ExpressionWithTypeArguments:
		return isPartOfTypeExpressionWithTypeArguments(parent);
	case Kind::TypeParameter:
		return node == parent->as<TypeParameterDeclaration>()->Constraint;
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::Constructor:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::CallSignature:
	case Kind::ConstructSignature:
	case Kind::IndexSignature:
	case Kind::TypeAssertionExpression:
		return node == parent->type();
	case Kind::CallExpression:
	case Kind::NewExpression:
	case Kind::TaggedTemplateExpression: {
		auto typeArgs = parent->typeArguments();
		return std::find(typeArgs.begin(), typeArgs.end(), node) !=
		       typeArgs.end();
	}
	}
	return false;
}

bool isPartOfTypeNode(Node* node) {
	Kind kind = node->kind;
	if (kind >= KindFirstTypeNode && kind <= KindLastTypeNode) {
		return true;
	}
	switch (node->kind) {
	case Kind::AnyKeyword:
	case Kind::UnknownKeyword:
	case Kind::NumberKeyword:
	case Kind::BigIntKeyword:
	case Kind::StringKeyword:
	case Kind::BooleanKeyword:
	case Kind::SymbolKeyword:
	case Kind::ObjectKeyword:
	case Kind::UndefinedKeyword:
	case Kind::NullKeyword:
	case Kind::NeverKeyword:
		return true;
	case Kind::VoidKeyword:
		return node->parent->kind != Kind::VoidExpression;
	case Kind::ExpressionWithTypeArguments:
		return isPartOfTypeExpressionWithTypeArguments(node);
	case Kind::TypeParameter:
		return node->parent->kind == Kind::MappedType ||
		       node->parent->kind == Kind::InferType;
	case Kind::Identifier: {
		Node* parent = node->parent;
		if (isQualifiedName(parent) &&
		    parent->as<QualifiedName>()->Right == node) {
			return isPartOfTypeNodeInParent(parent);
		}
		if (isPropertyAccessExpression(parent) &&
		    parent->as<PropertyAccessExpression>()->name == node) {
			return isPartOfTypeNodeInParent(parent);
		}
		return isPartOfTypeNodeInParent(node);
	}
	case Kind::QualifiedName:
	case Kind::PropertyAccessExpression:
	case Kind::ThisKeyword:
		return isPartOfTypeNodeInParent(node);
	}
	return false;
}

bool isExpressionNode(Node* node) {
	switch (node->kind) {
	case Kind::SuperKeyword:
	case Kind::NullKeyword:
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::RegularExpressionLiteral:
	case Kind::ArrayLiteralExpression:
	case Kind::ObjectLiteralExpression:
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
	case Kind::CallExpression:
	case Kind::NewExpression:
	case Kind::TaggedTemplateExpression:
	case Kind::AsExpression:
	case Kind::TypeAssertionExpression:
	case Kind::SatisfiesExpression:
	case Kind::NonNullExpression:
	case Kind::ParenthesizedExpression:
	case Kind::FunctionExpression:
	case Kind::ClassExpression:
	case Kind::ArrowFunction:
	case Kind::VoidExpression:
	case Kind::DeleteExpression:
	case Kind::TypeOfExpression:
	case Kind::PrefixUnaryExpression:
	case Kind::PostfixUnaryExpression:
	case Kind::BinaryExpression:
	case Kind::ConditionalExpression:
	case Kind::SpreadElement:
	case Kind::TemplateExpression:
	case Kind::OmittedExpression:
	case Kind::JsxElement:
	case Kind::JsxSelfClosingElement:
	case Kind::JsxFragment:
	case Kind::YieldExpression:
	case Kind::AwaitExpression:
		return true;
	case Kind::MetaProperty:
		// `import.<phase>` in `import.<phase>(...)` is not an expression
		return !isImportCall(node->parent) ||
		       node->parent->expression() != node;
	case Kind::ExpressionWithTypeArguments:
		return !isHeritageClause(node->parent);
	case Kind::QualifiedName:
		while (node->parent->kind == Kind::QualifiedName) {
			node = node->parent;
		}
		return isTypeQueryNode(node->parent) ||
		       isJSDocLinkLike(node->parent) ||
		       isJSDocNameReference(node->parent) || isJsxTagName(node);
	case Kind::PrivateIdentifier:
		return isBinaryExpression(node->parent) &&
		       node->parent->as<BinaryExpression>()->Left == node &&
		       node->parent->as<BinaryExpression>()->OperatorToken->kind ==
		           Kind::InKeyword;
	case Kind::Identifier:
		if (isTypeQueryNode(node->parent) || isJSDocLinkLike(node->parent) ||
		    isJSDocNameReference(node->parent) || isJsxTagName(node)) {
			return true;
		}
		[[fallthrough]];
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::ThisKeyword:
		return isInExpressionContext(node);
	default:
		return false;
	}
}

bool isInExpressionContext(Node* node) {
	Node* parent = node->parent;
	switch (parent->kind) {
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::EnumMember:
	case Kind::PropertyAssignment:
	case Kind::BindingElement:
		return parent->initializer() == node;
	case Kind::ExpressionStatement:
	case Kind::IfStatement:
	case Kind::DoStatement:
	case Kind::WhileStatement:
	case Kind::ReturnStatement:
	case Kind::WithStatement:
	case Kind::SwitchStatement:
	case Kind::CaseClause:
	case Kind::DefaultClause:
	case Kind::ThrowStatement:
	case Kind::TypeAssertionExpression:
	case Kind::AsExpression:
	case Kind::TemplateSpan:
	case Kind::ComputedPropertyName:
	case Kind::SatisfiesExpression:
		return parent->expression() == node;
	case Kind::ForStatement: {
		ForStatement* s = parent->as<ForStatement>();
		return (s->Initializer == node &&
		               s->Initializer->kind != Kind::VariableDeclarationList) ||
		       s->Condition == node || s->Incrementor == node;
	}
	case Kind::ForInStatement:
	case Kind::ForOfStatement: {
		ForInOrOfStatement* s = parent->as<ForInOrOfStatement>();
		return (s->Initializer == node &&
		               s->Initializer->kind != Kind::VariableDeclarationList) ||
		       s->Expression == node;
	}
	case Kind::Decorator:
	case Kind::JsxExpression:
	case Kind::JsxSpreadAttribute:
	case Kind::SpreadAssignment:
		return true;
	case Kind::ExpressionWithTypeArguments:
		return parent->expression() == node && !isPartOfTypeNode(parent);
	case Kind::ShorthandPropertyAssignment:
		return parent->as<ShorthandPropertyAssignment>()
		               ->ObjectAssignmentInitializer == node;
	case Kind::FunctionExpression:
	case Kind::ClassExpression:
		// The name of a function or class expression is a declaration name,
		// not an expression.
		return parent->name() != node;
	default:
		return isExpressionNode(parent);
	}
}

bool isShorthandPropertyNameUseSite(Node* useSite) {
	return isIdentifier(useSite) &&
	       isShorthandPropertyAssignment(useSite->parent) &&
	       useSite->parent->as<ShorthandPropertyAssignment>()->name == useSite;
}

static bool isIdentifierInNonEmittingHeritageClause(Node* node) {
	if (!isIdentifier(node)) {
		return false;
	}
	Node* parent = node->parent;
	while (isPropertyAccessExpression(parent) ||
	       isExpressionWithTypeArguments(parent)) {
		parent = parent->parent;
	}
	return isHeritageClause(parent) &&
	       (parent->as<HeritageClause>()->Token == Kind::ImplementsKeyword ||
	        isInterfaceDeclaration(parent->parent));
}

static bool isPartOfPossiblyValidTypeOrAbstractComputedPropertyName(
	Node* node) {
	while (nodeKindIs(node, Kind::Identifier, Kind::PropertyAccessExpression)) {
		node = node->parent;
	}
	if (node->kind != Kind::ComputedPropertyName) {
		return false;
	}
	if (hasSyntacticModifier(node->parent, ModifierFlagsAbstract)) {
		return true;
	}
	return nodeKindIs(node->parent->parent, Kind::InterfaceDeclaration,
	                  Kind::TypeLiteral);
}

bool isValidTypeOnlyAliasUseSite(Node* useSite) {
	return useSite->flags & (NodeFlagsAmbient | NodeFlagsJSDoc) ||
	       isPartOfTypeQuery(useSite) ||
	       isIdentifierInNonEmittingHeritageClause(useSite) ||
	       isPartOfPossiblyValidTypeOrAbstractComputedPropertyName(useSite) ||
	       !(isExpressionNode(useSite) ||
	         isShorthandPropertyNameUseSite(useSite));
}

bool hasAbstractModifier(Node* node) {
	return hasSyntacticModifier(node, ModifierFlagsAbstract);
}

bool hasAmbientModifier(Node* node) {
	return hasSyntacticModifier(node, ModifierFlagsAmbient);
}

bool hasDecorators(Node* node) {
	return hasSyntacticModifier(node, ModifierFlagsDecorator);
}


Node* getFirstConstructorWithBody(Node* node) {
	for (Node* member : node->members()) {
		if (isConstructorDeclaration(member) && nodeIsPresent(member->body())) {
			return member;
		}
	}
	return nullptr;
}

bool nodeCanBeDecorated(bool useLegacyDecorators, Node* node, Node* parent,
                        Node* grandparent) {
	// private names cannot be used with decorators yet
	if (useLegacyDecorators && node->name() != nullptr &&
	    isPrivateIdentifier(node->name())) {
		return false;
	}
	switch (node->kind) {
	case Kind::ClassDeclaration:
		// class declarations are valid targets
		return true;
	case Kind::ClassExpression:
		// class expressions are valid targets for native decorators
		return !useLegacyDecorators;
	case Kind::PropertyDeclaration:
		// property declarations are valid if their parent is a class declaration.
		return parent != nullptr &&
		       ((useLegacyDecorators && isClassDeclaration(parent)) ||
		        (!useLegacyDecorators && isClassLike(parent) &&
		            !hasAbstractModifier(node) && !hasAmbientModifier(node)));
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::MethodDeclaration:
		// if this method has a body and its parent is a class declaration, this
		// is a valid target.
		return parent != nullptr && node->body() != nullptr &&
		       ((useLegacyDecorators && isClassDeclaration(parent)) ||
		        (!useLegacyDecorators && isClassLike(parent)));
	case Kind::Parameter:
		// TODO(rbuckton): ParameterDeclaration decorator support for ES decorators
		// must wait until it is standardized
		if (!useLegacyDecorators) {
			return false;
		}
		// if the parameter's parent has a body and its grandparent is a class
		// declaration, this is a valid target.
		return parent != nullptr && parent->body() != nullptr &&
		       (parent->kind == Kind::Constructor ||
		        parent->kind == Kind::MethodDeclaration ||
		        parent->kind == Kind::SetAccessor) &&
		       getThisParameter(parent) != node && grandparent != nullptr &&
		       grandparent->kind == Kind::ClassDeclaration;
	}
	return false;
}

bool nodeIsDecorated(bool useLegacyDecorators, Node* node, Node* parent,
                     Node* grandparent) {
	return hasDecorators(node) &&
	       nodeCanBeDecorated(useLegacyDecorators, node, parent, grandparent);
}

bool nodeOrChildIsDecorated(bool useLegacyDecorators, Node* node,
                            Node* parent, Node* grandparent) {
	return nodeIsDecorated(useLegacyDecorators, node, parent, grandparent) ||
	       childIsDecorated(useLegacyDecorators, node, parent);
}

bool childIsDecorated(bool useLegacyDecorators, Node* node, Node* parent) {
	switch (node->kind) {
	case Kind::ClassDeclaration:
	case Kind::ClassExpression: {
		auto members = node->members();
		return someList(members, [&](Node* m) {
			return nodeOrChildIsDecorated(useLegacyDecorators, m, node, parent);
		});
	}
	case Kind::MethodDeclaration:
	case Kind::SetAccessor:
	case Kind::Constructor: {
		auto parameters = node->parameters();
		return someList(parameters, [&](Node* p) {
			return nodeIsDecorated(useLegacyDecorators, p, node, parent);
		});
	}
	default:
		return false;
	}
}

AllAccessorDeclarations getAllAccessorDeclarationsForDeclaration(
	Node* accessor, std::span<Node* const> declarationsOfSymbol) {
	Kind otherKind{};
	if (accessor->kind == Kind::SetAccessor) {
		otherKind = Kind::GetAccessor;
	} else if (accessor->kind == Kind::GetAccessor) {
		otherKind = Kind::SetAccessor;
	} else {
		TSC_UNREACHABLE("Unexpected node kind");
	}
	// otherAccessor := GetDeclarationOfKind(c.getSymbolOfDeclaration(accessor),
	// otherKind)
	Node* otherAccessor = nullptr;
	for (Node* d : declarationsOfSymbol) {
		if (d->kind == otherKind) {
			otherAccessor = d;
			break;
		}
	}

	Node* firstAccessor;
	Node* secondAccessor;
	if (otherAccessor != nullptr && otherAccessor->pos() < accessor->pos()) {
		firstAccessor = otherAccessor;
		secondAccessor = accessor;
	} else {
		firstAccessor = accessor;
		secondAccessor = otherAccessor;
	}

	Node* setAccessor = nullptr;
	Node* getAccessor = nullptr;
	if (accessor->kind == Kind::SetAccessor) {
		setAccessor = accessor;
		if (otherAccessor != nullptr) {
			getAccessor = otherAccessor;
		}
	} else {
		getAccessor = accessor;
		if (otherAccessor != nullptr) {
			setAccessor = otherAccessor;
		}
	}

	return AllAccessorDeclarations{firstAccessor, secondAccessor, setAccessor,
	                               getAccessor};
}

AllAccessorDeclarations getAllAccessorDeclarations(
	std::span<Node* const> parentDeclarations, Node* accessor) {
	if (hasDynamicName(accessor)) {
		// dynamic names can only be match up via checker symbol lookup, just
		// return an object with just this accessor
		return getAllAccessorDeclarationsForDeclaration(accessor,
		                                                {&accessor, 1});
	}

	std::string accessorName = getPropertyNameForPropertyNameNode(accessor->name());
	bool accessorStatic = isStatic(accessor);
	std::vector<Node*> matches;
	for (Node* member : parentDeclarations) {
		if (!isAccessor(member) || isStatic(member) != accessorStatic) {
			continue;
		}
		std::string memberName =
			getPropertyNameForPropertyNameNode(member->name());
		if (memberName == accessorName) {
			matches.push_back(member);
		}
	}
	return getAllAccessorDeclarationsForDeclaration(accessor, matches);
}

std::string getPropertyNameForPropertyNameNode(Node* name) {
	switch (name->kind) {
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::JsxNamespacedName:
		return name->text();
	case Kind::ComputedPropertyName: {
		Node* nameExpression = name->expression();
		if (isStringOrNumericLiteralLike(nameExpression)) {
			return nameExpression->text();
		}
		if (isSignedNumericLiteral(nameExpression)) {
			std::string text = nameExpression->as<PrefixUnaryExpression>()
			                       ->Operand->text();
			if (nameExpression->as<PrefixUnaryExpression>()->Operator ==
			    Kind::MinusToken) {
				text = "-" + text;
			}
			return text;
		}
		return InternalSymbolNameMissing;
	}
	default:
		TSC_UNREACHABLE("Unhandled case in getPropertyNameForPropertyNameNode");
	}
}

Node* getThisParameter(Node* signature) {
	// callback tags do not currently support this parameters
	auto parameters = signature->parameters();
	if (!parameters.empty()) {
		Node* thisParameter = parameters[0];
		if (isThisParameter(thisParameter)) {
			return thisParameter;
		}
	}
	return nullptr;
}

bool isThisParameter(Node* node) {
	return isParameterDeclaration(node) && node->name() != nullptr &&
	       isThisIdentifier(node->name());
}

bool classOrConstructorParameterIsDecorated(bool useLegacyDecorators,
                                            Node* node) {
	if (nodeIsDecorated(useLegacyDecorators, node, nullptr, nullptr)) {
		return true;
	}
	Node* constructor = getFirstConstructorWithBody(node);
	return constructor != nullptr &&
	       childIsDecorated(useLegacyDecorators, constructor, node);
}

bool classElementOrClassElementParameterIsDecorated(bool useLegacyDecorators,
                                                    Node* node,
                                                    Node* parent) {
	NodeList* parameters = nullptr;
	if (isAccessor(node)) {
		AllAccessorDeclarations decls =
			getAllAccessorDeclarations(parent->members(), node);
		Node* firstAccessorWithDecorators = nullptr;
		if (hasDecorators(decls.firstAccessor)) {
			firstAccessorWithDecorators = decls.firstAccessor;
		} else if (decls.secondAccessor != nullptr &&
		           hasDecorators(decls.secondAccessor)) {
			firstAccessorWithDecorators = decls.secondAccessor;
		}
		if (firstAccessorWithDecorators == nullptr ||
		    node != firstAccessorWithDecorators) {
			return false;
		}
		if (decls.setAccessor != nullptr) {
			parameters = decls.setAccessor->parameterList();
		}
	} else if (isMethodDeclaration(node)) {
		parameters = node->parameterList();
	}
	if (nodeIsDecorated(useLegacyDecorators, node, parent, nullptr)) {
		return true;
	}
	if (parameters != nullptr && !parameters->nodes.empty()) {
		for (Node* parameter : parameters->nodes) {
			if (isThisParameter(parameter)) {
				continue;
			}
			if (nodeIsDecorated(useLegacyDecorators, parameter, node,
			                    parent)) {
				return true;
			}
		}
	}
	return false;
}

bool isModuleWithStringLiteralName(Node* node) {
	return isModuleDeclaration(node) && node->name()->kind == Kind::StringLiteral;
}

} // namespace tsc

namespace tsc {

// utilities.go: GetFirstIdentifier
Node* getFirstIdentifier(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
		return node;
	case Kind::QualifiedName:
		return getFirstIdentifier(node->as<QualifiedName>()->Left);
	case Kind::PropertyAccessExpression:
		return getFirstIdentifier(
		    node->as<PropertyAccessExpression>()->Expression);
	default:
		tscUnreachable("Unhandled case in GetFirstIdentifier");
	}
}

// utilities.go: IsLateVisibilityPaintedStatement
bool isLateVisibilityPaintedStatement(Node* node) {
	switch (node->kind) {
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::ImportEqualsDeclaration:
	case Kind::VariableStatement:
	case Kind::ClassDeclaration:
	case Kind::FunctionDeclaration:
	case Kind::ModuleDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::InterfaceDeclaration:
	case Kind::EnumDeclaration:
		return true;
	default:
		return false;
	}
}

// utilities.go: IsExternalModuleAugmentation
bool isExternalModuleAugmentation(Node* node) {
	return isAmbientModule(node) && isModuleAugmentationExternal(node);
}

// utilities.go: IsEmittableImport
bool isEmittableImport(Node* node) {
	switch (node->kind) {
	case Kind::ImportDeclaration:
		return node->importClause() != nullptr &&
		       !node->importClause()->isTypeOnly();
	case Kind::ExportDeclaration:
	case Kind::ImportEqualsDeclaration:
		return !node->isTypeOnly();
	case Kind::CallExpression:
		return isImportCall(node);
	default:
		return false;
	}
}

// utilities.go: GetModuleSpecifierOfBareOrAccessedRequire
Node* getModuleSpecifierOfBareOrAccessedRequire(Node* node) {
	if (isVariableDeclarationInitializedWithRequireHelper(node, false)) {
		return node->initializer()->arguments()[0];
	}
	if (isVariableDeclarationInitializedWithRequireHelper(node, true)) {
		Node* leftmost = getLeftmostAccessExpression(node->initializer());
		if (isRequireCall(leftmost, true)) {
			return leftmost->arguments()[0];
		}
	}
	return nullptr;
}

// ast.go: ImportAttributesNode.GetResolutionModeOverride
std::pair<ResolutionMode, bool> getResolutionModeOverride(
    Node* attributes,
    const std::function<bool(Node*, const DiagnosticMessage*)>& grammarErrorOnNode) {
	if (attributes == nullptr) {
		return {ResolutionModeNone, false};
	}
	Node* attribute = nullptr;
	for (Node* attr : attributes->as<ImportAttributes>()->Attributes->nodes) {
		if (attr->name()->text() == "resolution-mode") {
			attribute = attr;
			break;
		}
	}
	if (attribute == nullptr) {
		return {ResolutionModeNone, false};
	}
	ImportAttribute* elem = attribute->as<ImportAttribute>();
	if (!isStringLiteralLike(elem->Value)) {
		return {ResolutionModeNone, false};
	}
	if (elem->Value->text() != "import" && elem->Value->text() != "require") {
		if (grammarErrorOnNode) {
			grammarErrorOnNode(
			    elem->Value,
			    X_resolution_mode_should_be_either_require_or_import);
		}
		return {ResolutionModeNone, false};
	}
	if (elem->Value->text() == "import") {
		return {ResolutionModeESM, true};
	}
	return {ModuleKind::CommonJS, true};
}

// utilities.go: HasResolutionModeOverride
bool hasResolutionModeOverride(Node* node) {
	if (node == nullptr) {
		return false;
	}
	Node* attributes = nullptr;
	switch (node->kind) {
	case Kind::ImportType:
		attributes = node->as<ImportTypeNode>()->Attributes;
		break;
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
		attributes = node->as<ImportDeclaration>()->Attributes;
		break;
	case Kind::ExportDeclaration:
		attributes = node->as<ExportDeclaration>()->Attributes;
		break;
	}
	if (attributes != nullptr) {
		return getResolutionModeOverride(attributes, nullptr).second;
	}
	return false;
}

// utilities.go: IsResolutionModeOverrideHost
bool isResolutionModeOverrideHost(Node* node) {
	if (node == nullptr) {
		return false;
	}
	switch (node->kind) {
	case Kind::ImportType:
	case Kind::ExportDeclaration:
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
		return true;
	default:
		return false;
	}
}

// utilities.go: HasImportAttributes
bool hasImportAttributes(Node* node) {
	switch (node->kind) {
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::ExportDeclaration:
	case Kind::ImportType:
		return true;
	default:
		return false;
	}
}

// utilities.go: IsPartOfTypeOnlyImportOrExportDeclaration
bool isPartOfTypeOnlyImportOrExportDeclaration(Node* node) {
	return findAncestor(node, [](Node* n) {
		       return isTypeOnlyImportOrExportDeclaration(n);
	       }) != nullptr;
}

// utilities.go: IsVariableDeclarationInitializedToBareOrAccessedRequire
bool isVariableDeclarationInitializedToBareOrAccessedRequire(Node* node) {
	return isVariableDeclarationInitializedWithRequireHelper(
	    node, true /*allowAccessedRequire*/);
}

// --- slice: grammarchecks — utilities.go helpers ---

// utilities.go: IsCommaExpression
bool isCommaExpression(Node* node) {
	return node->kind == Kind::BinaryExpression &&
	       node->as<BinaryExpression>()->OperatorToken->kind == Kind::CommaToken;
}

// utilities.go: IsCommaSequence
bool isCommaSequence(Node* node) {
	return isCommaExpression(node);
}

// utilities.go: IsIterationStatement
bool isIterationStatement(Node* node, bool lookInLabeledStatements) {
	switch (node->kind) {
	case Kind::ForStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::DoStatement:
	case Kind::WhileStatement:
		return true;
	case Kind::LabeledStatement:
		return lookInLabeledStatements &&
		       isIterationStatement(node->statement(), lookInLabeledStatements);
	default:
		return false;
	}
}

// utilities.go: IsStringLiteralLikeType
bool isStringLiteralLikeType(Node* node) {
	return node->kind == Kind::LiteralType &&
	       isStringLiteralLike(node->as<LiteralTypeNode>()->Literal);
}

// utilities.go: WalkUpParenthesizedTypes
Node* walkUpParenthesizedTypes(Node* node) {
	while (node != nullptr && node->kind == Kind::ParenthesizedType) {
		node = node->parent;
	}
	return node;
}

// utilities.go: CanHaveModifiers
bool canHaveModifiers(Node* node) {
	switch (node->kind) {
	case Kind::TypeParameter:
	case Kind::Parameter:
	case Kind::PropertySignature:
	case Kind::PropertyDeclaration:
	case Kind::MethodSignature:
	case Kind::MethodDeclaration:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::IndexSignature:
	case Kind::ConstructorType:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::ClassExpression:
	case Kind::VariableStatement:
	case Kind::FunctionDeclaration:
	case Kind::ClassDeclaration:
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::EnumDeclaration:
	case Kind::ModuleDeclaration:
	case Kind::ImportEqualsDeclaration:
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::ExportAssignment:
	case Kind::ExportDeclaration:
		return true;
	default:
		return false;
	}
}

// utilities.go: CanHaveIllegalModifiers
bool canHaveIllegalModifiers(Node* node) {
	switch (node->kind) {
	case Kind::ClassStaticBlockDeclaration:
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::MissingDeclaration:
	case Kind::NamespaceExportDeclaration:
		return true;
	default:
		return false;
	}
}

// utilities.go: HasModifier
bool hasModifier(Node* node, ModifierFlags flags) {
	return (node->modifierFlags() & flags) != 0;
}

// utilities.go: GetContainingFunction
Node* getContainingFunction(Node* node) {
	return findAncestor(node->parent, isFunctionLike);
}

// utilities.go: isCommonJSContainingModuleKind
static bool isCommonJSContainingModuleKind(ModuleKind kind) {
	return kind == ModuleKind::CommonJS ||
	       (ModuleKind::Node16 <= kind && kind <= ModuleKind::NodeNext);
}

// utilities.go: IsEffectiveExternalModule
bool isEffectiveExternalModule(SourceFile* node, const CompilerOptions* compilerOptions) {
	return isExternalModule(node) ||
	       (isCommonJSContainingModuleKind(compilerOptions->GetEmitModuleKind()) &&
	        node->CommonJSModuleIndicator != nullptr);
}

// utilities.go: IsLiteralLikeElementAccess (1397)
bool isLiteralLikeElementAccess(Node* node) {
	return isElementAccessExpression(node) &&
		isStringOrNumericLiteralLike(node->as<ElementAccessExpression>()->ArgumentExpression);
}

// utilities.go: IsBindableStaticElementAccessExpression (1381)
bool isBindableStaticElementAccessExpression(Node* node, bool excludeThisKeyword) {
	return isLiteralLikeElementAccess(node) &&
		((!excludeThisKeyword && node->expression()->kind == Kind::ThisKeyword) ||
			isEntityNameExpression(node->expression()) ||
			isBindableStaticAccessExpression(node->expression(), true /*excludeThisKeyword*/));
}

// utilities.go: IsBindableStaticAccessExpression (1375)
bool isBindableStaticAccessExpression(Node* node, bool excludeThisKeyword) {
	return (isPropertyAccessExpression(node) &&
		((!excludeThisKeyword && node->expression()->kind == Kind::ThisKeyword) ||
			(isIdentifier(node->name()) &&
				isBindableStaticNameExpression(node->expression(), true /*excludeThisKeyword*/)))) ||
		isBindableStaticElementAccessExpression(node, excludeThisKeyword);
}

// utilities.go: IsBindableStaticNameExpression (1579)
bool isBindableStaticNameExpression(Node* node, bool excludeThisKeyword) {
	return isEntityNameExpression(node) || isBindableStaticAccessExpression(node, excludeThisKeyword);
}

// utilities.go: IsPrototypeAccess (1388)
bool isPrototypeAccess(Node* node) {
	if (isBindableStaticAccessExpression(node, false /*excludeThisKeyword*/)) {
		if (Node* name = getElementOrPropertyAccessName(node)) {
			return name->text() == "prototype";
		}
	}
	return false;
}

// utilities.go: IsNameOfHeritageClauseTypeReference (1766)
bool isNameOfHeritageClauseTypeReference(Node* node) {
	while (isQualifiedName(node->parent)) {
		node = node->parent;
	}
	return isTypeReferenceNode(node->parent) &&
		node->parent->as<TypeReferenceNode>()->TypeName == node &&
		isHeritageClause(node->parent->parent);
}

// utilities.go: IsCallOrNewExpression (3779)
bool isCallOrNewExpression(Node* node) {
	return isCallExpression(node) || isNewExpression(node);
}

// utilities.go: IsExpandoPropertyDeclaration (4584)
bool isExpandoPropertyDeclaration(Node* node) {
	return node != nullptr && isBinaryExpression(node);
}

// === slice: program === — utilities.go helpers (deduped vs HEAD)

// ---------------------------------------------------------------------------

// utilities.go: IsSourceFileJS
bool isSourceFileJS(SourceFile* file) {
	return file->ScriptKind == ScriptKind::JS || file->ScriptKind == ScriptKind::JSX;
}

// utilities.go: IsCheckJSEnabledForFile
bool isCheckJSEnabledForFile(SourceFile* sourceFile,
                             const CompilerOptions* compilerOptions) {
	if (sourceFile->CheckJsDirective != nullptr) {
		return sourceFile->CheckJsDirective->Enabled;
	}
	return compilerOptions->CheckJs == Tristate::True;
}



// utilities.go: ShouldTransformImportCall
bool shouldTransformImportCall(std::string_view fileName,
                               const CompilerOptions* options,
                               ModuleKind impliedNodeFormatForEmit) {
	ModuleKind moduleKind = options->GetEmitModuleKind();
	if ((ModuleKind::Node16 <= moduleKind &&
	     moduleKind <= ModuleKind::NodeNext) ||
	    moduleKind == ModuleKind::Preserve) {
		return false;
	}
	return impliedNodeFormatForEmit < ModuleKind::ES2015;
}

// utilities.go: GetImpliedNodeFormatForFile (ast.go equivalent)
ModuleKind getImpliedNodeFormatForFile(std::string_view path,
                                       std::string_view packageJsonType) {
	ModuleKind impliedNodeFormat = ResolutionModeNone;
	if (tspath::fileExtensionIsOneOf(path, {tspath::extensionDmts, tspath::extensionMts, tspath::extensionMjs})) {
		impliedNodeFormat = ResolutionModeESM;
	} else if (tspath::fileExtensionIsOneOf(path,
	                                {tspath::extensionDcts, tspath::extensionCts, tspath::extensionCjs})) {
		impliedNodeFormat = ResolutionModeCommonJS;
	} else if (tspath::fileExtensionIsOneOf(path, {tspath::extensionDts, tspath::extensionTs, tspath::extensionTsx,
	                                       tspath::extensionJs, tspath::extensionJsx})) {
		impliedNodeFormat = packageJsonType == "module" ? ResolutionModeESM
		                                                : ResolutionModeCommonJS;
	}
	return impliedNodeFormat;
}

// utilities.go: GetImpliedNodeFormatForEmitWorker
ResolutionMode getImpliedNodeFormatForEmitWorker(
    std::string_view fileName, ModuleKind emitModuleKind,
    const SourceFileMetaData& sourceFileMetaData) {
	if (ModuleKind::Node16 <= emitModuleKind &&
	    emitModuleKind <= ModuleKind::NodeNext) {
		return sourceFileMetaData.ImpliedNodeFormat;
	}
	if (sourceFileMetaData.ImpliedNodeFormat == ModuleKind::CommonJS &&
	    (sourceFileMetaData.PackageJsonType == "commonjs" ||
	     tspath::fileExtensionIsOneOf(fileName, {tspath::extensionCjs, tspath::extensionCts}))) {
		return ModuleKind::CommonJS;
	}
	if (sourceFileMetaData.ImpliedNodeFormat == ModuleKind::ESNext &&
	    (sourceFileMetaData.PackageJsonType == "module" ||
	     tspath::fileExtensionIsOneOf(fileName, {tspath::extensionMjs, tspath::extensionMts}))) {
		return ModuleKind::ESNext;
	}
	return ModuleKind::None;
}

// utilities.go: GetEmitModuleFormatOfFileWorker
ModuleKind getEmitModuleFormatOfFileWorker(
    std::string_view fileName, const CompilerOptions* options,
    const SourceFileMetaData& sourceFileMetaData) {
	ModuleKind result = getImpliedNodeFormatForEmitWorker(
	    fileName, options->GetEmitModuleKind(), sourceFileMetaData);
	if (result != ModuleKind::None) {
		return result;
	}
	return options->GetEmitModuleKind();
}

// parseoptions.go: isFileForcedToBeModuleByFormat
static bool isFileForcedToBeModuleByFormat(
    std::string_view fileName, const CompilerOptions* options,
    const SourceFileMetaData& metadata) {
	if (getImpliedNodeFormatForEmitWorker(
	        fileName, options->GetEmitModuleKind(), metadata) ==
	        ModuleKind::ESNext ||
	    tspath::fileExtensionIsOneOf(
	        fileName, {tspath::extensionCjs, tspath::extensionCts, tspath::extensionMjs, tspath::extensionMts})) {
		return true;
	}
	return false;
}

// parseoptions.go: GetExternalModuleIndicatorOptions
ExternalModuleIndicatorOptions getExternalModuleIndicatorOptions(
    std::string_view fileName, const CompilerOptions* options,
    const SourceFileMetaData& metadata) {
	if (tspath::isDeclarationFileName(fileName)) {
		return ExternalModuleIndicatorOptions{};
	}

	switch (options->GetEmitModuleDetectionKind()) {
	case ModuleDetectionKind::Force:
		// All non-declaration files are modules, declaration files still do the
		// usual isFileProbablyExternalModule
		return ExternalModuleIndicatorOptions{.Force = true};
	case ModuleDetectionKind::Legacy:
		// Files are modules if they have imports, exports, or import.meta
		return ExternalModuleIndicatorOptions{};
	case ModuleDetectionKind::Auto:
		// C++ stores the raw JsxEmit; getExternalModuleIndicator checks
		// JSX == ReactJSX || ReactJSXDev itself.
		return ExternalModuleIndicatorOptions{
		    .JSX = options->Jsx,
		    .Force = isFileForcedToBeModuleByFormat(fileName, options, metadata)};
	default:
		return ExternalModuleIndicatorOptions{};
	}
}

// utilities.go: GetPragmaFromSourceFile — last matching pragma wins.
const Pragma* getPragmaFromSourceFile(const SourceFile* file,
                                      std::string_view name) {
	const Pragma* result = nullptr;
	if (file != nullptr) {
		for (const auto& pragma : file->Pragmas) {
			if (pragma.Name == name) {
				result = &pragma;
			}
		}
	}
	return result;
}

// utilities.go: GetPragmaArgument
std::string getPragmaArgument(const Pragma* pragma, std::string_view name) {
	if (pragma != nullptr) {
		auto it = pragma->Args.find(std::string(name));
		if (it != pragma->Args.end()) {
			return it->second.Value;
		}
	}
	return "";
}

// utilities.go: GetJSXImplicitImportBase
std::string getJSXImplicitImportBase(const CompilerOptions* compilerOptions,
                                     SourceFile* file) {
	const Pragma* jsxImportSourcePragma =
	    getPragmaFromSourceFile(file, "jsximportsource");
	const Pragma* jsxRuntimePragma =
	    getPragmaFromSourceFile(file, "jsxruntime");
	if (getPragmaArgument(jsxRuntimePragma, "factory") == "classic") {
		return "";
	}
	if (compilerOptions->Jsx == JsxEmit::ReactJSX ||
	    compilerOptions->Jsx == JsxEmit::ReactJSXDev ||
	    compilerOptions->JsxImportSource != "" ||
	    jsxImportSourcePragma != nullptr ||
	    getPragmaArgument(jsxRuntimePragma, "factory") == "automatic") {
		std::string result = getPragmaArgument(jsxImportSourcePragma, "factory");
		if (result == "") {
			result = compilerOptions->JsxImportSource;
		}
		if (result == "") {
			result = "react";
		}
		return result;
	}
	return "";
}

// utilities.go: GetJSXRuntimeImport
std::string getJSXRuntimeImport(std::string_view base,
                                const CompilerOptions* options) {
	if (base == "") {
		return std::string(base);
	}
	return std::string(base) + "/" +
	       (options->Jsx == JsxEmit::ReactJSXDev ? "jsx-dev-runtime"
	                                             : "jsx-runtime");
}

// utilities.go: GetSemanticJsxChildren
std::vector<Node*> getSemanticJsxChildren(const std::vector<Node*>& children) {
	std::vector<Node*> result;
	for (Node* i : children) {
		switch (i->kind) {
		case Kind::JsxExpression:
			if (i->expression() != nullptr) {
				result.push_back(i);
			}
			break;
		case Kind::JsxText:
			if (!i->as<JsxText>()->ContainsOnlyTriviaWhiteSpaces) {
				result.push_back(i);
			}
			break;
		default:
			result.push_back(i);
		}
	}
	return result;
}

// ast.go: collectIdentifiersForSourceFile — every Identifier /
// PrivateIdentifier / literal Text() in the file.
static std::unordered_set<std::string> collectIdentifiersForSourceFile(
	SourceFile* sourceFile) {
	std::unordered_set<std::string> identifiers;
	std::function<bool(Node*)> collect = [&](Node* node) -> bool {
		switch (node->kind) {
			case Kind::Identifier:
			case Kind::PrivateIdentifier:
			case Kind::StringLiteral:
			case Kind::NumericLiteral:
			case Kind::BigIntLiteral:
			case Kind::NoSubstitutionTemplateLiteral:
				identifiers.insert(node->text());
		}
		node->forEachChild(collect);
		return false;
	};
	collect(sourceFile->asNode());
	return identifiers;
}

// ast.go: SourceFile::HasIdentifier
bool sourceFileHasIdentifier(SourceFile* file, const std::string& name) {
	file->identifiersOnce.run(
		[&] { file->identifiers = collectIdentifiersForSourceFile(file); });
	return file->identifiers.count(name) != 0;
}

// === slice: ls-autoimport ===

// utilities.go:2811 IsRequireVariableStatement
bool isRequireVariableStatement(Node* node) {
	if (isVariableStatement(node)) {
		auto* declList = node->as<VariableStatement>()->DeclarationList;
		auto* declarations = declList->as<VariableDeclarationList>()->Declarations;
		if (declarations != nullptr && !declarations->nodes.empty()) {
			return std::all_of(declarations->nodes.begin(),
			                   declarations->nodes.end(),
			                   isVariableDeclarationInitializedToRequire);
		}
	}
	return false;
}

// utilities.go:3618 GetNonAugmentationDeclaration
Node* getNonAugmentationDeclaration(Symbol* symbol) {
	for (Node* d : symbol->data->declarations) {
		if (!isExternalModuleAugmentation(d) && !isGlobalScopeAugmentation(d)) {
			return d;
		}
	}
	return nullptr;
}

// utilities.go:3613 GetSourceFileOfModule
SourceFile* getSourceFileOfModule(Symbol* module) {
	Node* declaration = module->data->valueDeclaration;
	if (declaration == nullptr) {
		declaration = getNonAugmentationDeclaration(module);
	}
	return getSourceFileOfNode(declaration);
}

// utilities.go:4203 TryGetImportFromModuleSpecifier
Node* tryGetImportFromModuleSpecifier(Node* node) {
	switch (node->parent->kind) {
		case Kind::ImportDeclaration:
		case Kind::JSImportDeclaration:
		case Kind::ExportDeclaration:
			return node->parent;
		case Kind::ExternalModuleReference:
			return node->parent->parent;
		case Kind::CallExpression:
			if (isImportCall(node->parent) ||
			    isRequireCall(node->parent,
			                  false /*requireStringLiteralLikeArgument*/)) {
				return node->parent;
			}
			return nullptr;
		case Kind::LiteralType:
			if (!isStringLiteral(node)) {
				return nullptr;
			}
			if (isImportTypeNode(node->parent->parent)) {
				return node->parent->parent;
			}
			return nullptr;
	}
	return nullptr;
}

} // namespace tsc
