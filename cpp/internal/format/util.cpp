// Port of tsc/internal/format/util.go.
#include "internal/format/format.h"

#include "internal/astnav/tokens.h"

namespace tsc::format {

bool rangeIsOnOneLine(TextRange node, SourceFile* file) {
	int startLine = getECMALineOfPosition(file, node.pos());
	int endLine = getECMALineOfPosition(file, node.end());
	return startLine == endLine;
}

Kind getOpenTokenForList(Node* node, NodeList* list) {
	switch (node->kind) {
	case Kind::Constructor:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::ArrowFunction:
	case Kind::CallSignature:
	case Kind::ConstructSignature:
	case Kind::FunctionType:
	case Kind::ConstructorType:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		if (node->typeParameterList() == list) {
			return Kind::LessThanToken;
		} else if (node->parameterList() == list) {
			return Kind::OpenParenToken;
		}
		break;
	case Kind::CallExpression:
	case Kind::NewExpression:
		if (node->typeArgumentList() == list) {
			return Kind::LessThanToken;
		} else if (node->argumentList() == list) {
			return Kind::OpenParenToken;
		}
		break;
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
		if (node->typeParameterList() == list) {
			return Kind::LessThanToken;
		}
		break;
	case Kind::TypeReference:
	case Kind::TaggedTemplateExpression:
	case Kind::TypeQuery:
	case Kind::ExpressionWithTypeArguments:
	case Kind::ImportType:
		if (node->typeArgumentList() == list) {
			return Kind::LessThanToken;
		}
		break;
	case Kind::TypeLiteral:
		return Kind::OpenBraceToken;
	default:
		break;
	}

	return Kind::Unknown;
}

Kind getCloseTokenForOpenToken(Kind kind) {
	// TODO: matches strada - seems like it could handle more pairs of braces, though? [] notably missing
	switch (kind) {
	case Kind::OpenParenToken:
		return Kind::CloseParenToken;
	case Kind::LessThanToken:
		return Kind::GreaterThanToken;
	case Kind::OpenBraceToken:
		return Kind::CloseBraceToken;
	default:
		break;
	}
	return Kind::Unknown;
}

int GetLineStartPositionForPosition(int position, SourceFile* sourceFile) {
	const ECMALineStarts& lineStarts = getECMALineStarts(sourceFile);
	int line = getECMALineOfPosition(sourceFile, position);
	return static_cast<int>(lineStarts[line]);
}

/**
 * Validating `expectedTokenKind` ensures the token was typed in the context we expect (eg: not a comment).
 * @param expectedTokenKind The kind of the last token constituting the desired parent node.
 */
Node* findImmediatelyPrecedingTokenOfKind(int end, Kind expectedTokenKind, SourceFile* sourceFile) {
	Node* precedingToken = astnav::findPrecedingToken(sourceFile, end);
	if (precedingToken == nullptr || precedingToken->kind != expectedTokenKind ||
		precedingToken->end() != end) {
		return nullptr;
	}
	return precedingToken;
}

/**
 * Finds the highest node enclosing `node` at the same list level as `node`
 * and whose end does not exceed `node.end`.
 *
 * Consider typing the following
 * ```
 * let x = 1;
 * while (true) {
 * }
 * ```
 * Upon typing the closing curly, we want to format the entire `while`-statement, but not the preceding
 * variable declaration.
 */
Node* findOutermostNodeWithinListLevel(Node* node) {
	Node* current = node;
	while (current != nullptr && current->parent != nullptr &&
		   current->parent->end() == node->end() && !isListElement(current->parent, current)) {
		current = current->parent;
	}

	return current;
}

// Returns true if node is a element in some list in parent
// i.e. parent is class declaration with the list of members and node is one of members.
bool isListElement(Node* parent, Node* node) {
	switch (parent->kind) {
	case Kind::ClassDeclaration:
	case Kind::InterfaceDeclaration:
		return node->loc.containedBy(parent->memberList()->loc);
	case Kind::ModuleDeclaration: {
		Node* body = parent->body();
		return body != nullptr && body->kind == Kind::ModuleBlock &&
			node->loc.containedBy(body->statementList()->loc);
	}
	case Kind::SourceFile:
	case Kind::Block:
	case Kind::ModuleBlock:
		return node->loc.containedBy(parent->statementList()->loc);
	case Kind::CatchClause:
		return node->loc.containedBy(parent->as<CatchClause>()->Block->statementList()->loc);
	default:
		break;
	}

	return false;
}

bool isMemberListElement(Node* parent, Node* node) {
	switch (parent->kind) {
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::InterfaceDeclaration:
	case Kind::EnumDeclaration:
	case Kind::TypeLiteral:
	case Kind::MappedType:
		return node->loc.containedBy(parent->memberList()->loc);
	default:
		break;
	}
	return false;
}

} // namespace tsc::format
