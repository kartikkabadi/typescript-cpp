// Port of binder-relevant helpers from tsc/internal/ast/utilities.go,
// ast.go, symbol.go, flow.go, symbolcompare.go.
#include <algorithm>
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/flow.h"
#include "internal/ast/symbol.h"
#include "internal/ast/symbolflags.h"
#include "internal/core/types.h"

namespace tsc {

// --- Atomic ids (ast/utilities.go) ---

static std::atomic<NodeId> nextNodeId{0};
static std::atomic<SymbolId> nextSymbolId{0};

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

SymbolTable& getSymbolTable(SymbolTable& data) { return data; }
SymbolTable& getMembers(Symbol* symbol) { return symbol->members; }
SymbolTable& getExports(Symbol* symbol) { return symbol->exports; }
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
	const std::string& text = node->text();
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
	if (symbol->valueDeclaration &&
	    isPrivateIdentifierClassElementDeclaration(symbol->valueDeclaration)) {
		return symbol->valueDeclaration->name()->text();
	}
	return symbol->name;
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

} // namespace tsc

namespace tsc {

bool moduleExportNameIsDefault(Node* node) {
	return node->text() == InternalSymbolNameDefault;
}

bool expressionIsAlias(Node* node) {
	return isEntityNameExpression(node) || isClassExpression(node);
}

} // namespace tsc
