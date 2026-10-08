// === slice: ls-foundation ===
// lsutil/completednode.go — node-completion predicates used by completions.

#include "internal/ls/lsutil/lsutil.h"

#include "internal/ast/ast.h"
#include "internal/astnav/tokens.h"
#include "internal/scanner/scanner.h"

namespace tsc::ls::lsutil {

namespace {
// core.LastOrNil
template <class T>
T* lastOrNil(const std::vector<T*>& v) {
	return v.empty() ? nullptr : v.back();
}
} // namespace

// Unexported helpers (completednode.go) — declared for use before definition.
static bool nodeEndsWith(Node* n, Kind expectedLastToken, SourceFile* sourceFile);
static bool hasChildOfKind(Node* containingNode, Kind kind, SourceFile* sourceFile);

// PositionBelongsToNode — completednode.go:12. Returns true if the position
// belongs to the node. Assumes `candidate.Pos() <= position` holds.
bool PositionBelongsToNode(Node* candidate, int position, SourceFile* file) {
	if (candidate->pos() > position) {
		TSC_UNREACHABLE("Expected candidate.pos <= position");
	}
	return position < candidate->end() || !IsCompletedNode(candidate, file);
}

// IsCompletedNode — completednode.go:19
bool IsCompletedNode(Node* n, SourceFile* sourceFile) {
	if (n == nullptr || nodeIsMissing(n)) {
		return false;
	}

	switch (n->kind) {
	case Kind::ClassDeclaration:
	case Kind::InterfaceDeclaration:
	case Kind::EnumDeclaration:
	case Kind::ObjectLiteralExpression:
	case Kind::ObjectBindingPattern:
	case Kind::TypeLiteral:
	case Kind::Block:
	case Kind::ModuleBlock:
	case Kind::CaseBlock:
	case Kind::NamedImports:
	case Kind::NamedExports:
		return nodeEndsWith(n, Kind::CloseBraceToken, sourceFile);

	case Kind::CatchClause:
		return IsCompletedNode(n->as<CatchClause>()->Block, sourceFile);

	case Kind::NewExpression:
		if (n->argumentList() == nullptr) {
			return true;
		}
		[[fallthrough]];

	case Kind::CallExpression:
	case Kind::ParenthesizedExpression:
	case Kind::ParenthesizedType:
		return nodeEndsWith(n, Kind::CloseParenToken, sourceFile);

	case Kind::FunctionType:
	case Kind::ConstructorType:
		return IsCompletedNode(n->type(), sourceFile);

	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::ConstructSignature:
	case Kind::CallSignature:
	case Kind::ArrowFunction:
		if (n->body() != nullptr) {
			return IsCompletedNode(n->body(), sourceFile);
		}
		if (n->type() != nullptr) {
			return IsCompletedNode(n->type(), sourceFile);
		}
		// Even though type parameters can be unclosed, we can get away with
		// having at least a closing paren.
		return hasChildOfKind(n, Kind::CloseParenToken, sourceFile);

	case Kind::ModuleDeclaration:
		return n->body() != nullptr && IsCompletedNode(n->body(), sourceFile);

	case Kind::IfStatement:
		if (n->as<IfStatement>()->ElseStatement != nullptr) {
			return IsCompletedNode(n->as<IfStatement>()->ElseStatement, sourceFile);
		}
		return IsCompletedNode(n->as<IfStatement>()->ThenStatement, sourceFile);

	case Kind::ExpressionStatement:
		return IsCompletedNode(n->expression(), sourceFile) ||
			hasChildOfKind(n, Kind::SemicolonToken, sourceFile);

	case Kind::ArrayLiteralExpression:
	case Kind::ArrayBindingPattern:
	case Kind::ElementAccessExpression:
	case Kind::ComputedPropertyName:
	case Kind::TupleType:
		return nodeEndsWith(n, Kind::CloseBracketToken, sourceFile);

	case Kind::IndexSignature:
		if (n->as<IndexSignatureDeclaration>()->Type != nullptr) {
			return IsCompletedNode(n->as<IndexSignatureDeclaration>()->Type, sourceFile);
		}
		return hasChildOfKind(n, Kind::CloseBracketToken, sourceFile);

	case Kind::CaseClause:
	case Kind::DefaultClause:
		// there is no such thing as terminator token for CaseClause/DefaultClause so for simplicity always consider them non-completed
		return false;

	case Kind::ForStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::WhileStatement:
		return IsCompletedNode(n->statement(), sourceFile);

	case Kind::DoStatement:
		// rough approximation: if DoStatement has While keyword - then if node is completed is checking the presence of ')';
		if (hasChildOfKind(n, Kind::WhileKeyword, sourceFile)) {
			return nodeEndsWith(n, Kind::CloseParenToken, sourceFile);
		}
		return IsCompletedNode(n->statement(), sourceFile);

	case Kind::TypeQuery:
		return IsCompletedNode(n->as<TypeQueryNode>()->ExprName, sourceFile);

	case Kind::TypeOfExpression:
	case Kind::DeleteExpression:
	case Kind::VoidExpression:
	case Kind::YieldExpression:
	case Kind::SpreadElement:
		return IsCompletedNode(n->expression(), sourceFile);

	case Kind::TaggedTemplateExpression:
		return IsCompletedNode(n->as<TaggedTemplateExpression>()->Template, sourceFile);

	case Kind::TemplateExpression:
		if (n->as<TemplateExpression>()->TemplateSpans == nullptr) {
			return false;
		}
		{
			Node* lastSpan = lastOrNil(n->as<TemplateExpression>()->TemplateSpans->nodes);
			return IsCompletedNode(lastSpan, sourceFile);
		}

	case Kind::TemplateSpan:
		return nodeIsPresent(n->as<TemplateSpan>()->Literal);

	case Kind::ExportDeclaration:
	case Kind::ImportDeclaration:
		return nodeIsPresent(n->moduleSpecifier());

	case Kind::PrefixUnaryExpression:
		return IsCompletedNode(n->as<PrefixUnaryExpression>()->Operand, sourceFile);

	case Kind::BinaryExpression:
		return IsCompletedNode(n->as<BinaryExpression>()->Right, sourceFile);

	case Kind::ConditionalExpression:
		return IsCompletedNode(n->as<ConditionalExpression>()->WhenFalse, sourceFile);

	default:
		return true;
	}
}

// nodeEndsWith — completednode.go:162. Checks if node ends with
// 'expectedLastToken'. If child at position 'length - 1' is 'SemicolonToken'
// it is skipped and 'expectedLastToken' is compared with child at position
// 'length - 2'.
static bool nodeEndsWith(Node* n, Kind expectedLastToken, SourceFile* sourceFile) {
	Node* lastChildNode = GetLastVisitedChild(n, sourceFile);
	std::vector<Node*> lastNodeAndTokens;
	int tokenStartPos;
	if (lastChildNode != nullptr) {
		lastNodeAndTokens = {lastChildNode};
		tokenStartPos = lastChildNode->end();
	} else {
		tokenStartPos = n->pos();
	}
	Scanner scanner;
	getScannerForSourceFile(scanner, sourceFile, tokenStartPos);
	for (int startPos = tokenStartPos; startPos < n->end();) {
		Kind tokenKind = scanner.token();
		int tokenFullStart = scanner.tokenFullStart();
		int tokenEnd = scanner.tokenEnd();
		Node* token = astnav::getOrCreateToken(sourceFile, tokenKind, tokenFullStart, tokenEnd, n, scanner.tokenFlags());
		lastNodeAndTokens.push_back(token);
		startPos = tokenEnd;
		scanner.scan();
	}
	if (lastNodeAndTokens.empty()) {
		return false;
	}
	Node* lastChild = lastNodeAndTokens.back();
	if (lastChild->kind == expectedLastToken) {
		return true;
	} else if (lastChild->kind == Kind::SemicolonToken && lastNodeAndTokens.size() > 1) {
		return lastNodeAndTokens[lastNodeAndTokens.size() - 2]->kind == expectedLastToken;
	}
	return false;
}

// hasChildOfKind — completednode.go:194
static bool hasChildOfKind(Node* containingNode, Kind kind, SourceFile* sourceFile) {
	return astnav::findChildOfKind(containingNode, kind, sourceFile) != nullptr;
}

} // namespace tsc::ls::lsutil
