// === slice: ls-foundation ===
// lsutil/asi.go — ASI (automatic semicolon insertion) candidate rules.

#include "internal/ls/lsutil/lsutil.h"

#include "internal/ast/ast.h"
#include "internal/astnav/tokens.h"
#include "internal/scanner/scanner.h"

namespace tsc::ls::lsutil {

// --- asi.go ---

// PositionIsASICandidate — asi.go:9
bool PositionIsASICandidate(int pos, Node* context, SourceFile* file) {
	Node* contextAncestor =
		findAncestorOrQuit(context, [pos](Node* ancestor) -> FindAncestorResult {
			if (ancestor->end() != pos) {
				return FindAncestorResult::Quit;
			}

		return toFindAncestorResult(SyntaxMayBeASICandidate(ancestor->kind));
	});

	return contextAncestor != nullptr && NodeIsASICandidate(contextAncestor, file);
}

// SyntaxMayBeASICandidate — asi.go:21
bool SyntaxMayBeASICandidate(Kind kind) {
	return SyntaxRequiresTrailingCommaOrSemicolonOrASI(kind) ||
		SyntaxRequiresTrailingFunctionBlockOrSemicolonOrASI(kind) ||
		SyntaxRequiresTrailingModuleBlockOrSemicolonOrASI(kind) ||
		SyntaxRequiresTrailingSemicolonOrASI(kind);
}

// SyntaxRequiresTrailingCommaOrSemicolonOrASI — asi.go:28
bool SyntaxRequiresTrailingCommaOrSemicolonOrASI(Kind kind) {
	return kind == Kind::CallSignature ||
		kind == Kind::ConstructSignature ||
		kind == Kind::IndexSignature ||
		kind == Kind::PropertySignature ||
		kind == Kind::MethodSignature;
}

// SyntaxRequiresTrailingFunctionBlockOrSemicolonOrASI — asi.go:36
bool SyntaxRequiresTrailingFunctionBlockOrSemicolonOrASI(Kind kind) {
	return kind == Kind::FunctionDeclaration ||
		kind == Kind::Constructor ||
		kind == Kind::MethodDeclaration ||
		kind == Kind::GetAccessor ||
		kind == Kind::SetAccessor;
}

// SyntaxRequiresTrailingModuleBlockOrSemicolonOrASI — asi.go:44
bool SyntaxRequiresTrailingModuleBlockOrSemicolonOrASI(Kind kind) {
	return kind == Kind::ModuleDeclaration;
}

// SyntaxRequiresTrailingSemicolonOrASI — asi.go:48
bool SyntaxRequiresTrailingSemicolonOrASI(Kind kind) {
	return kind == Kind::VariableStatement ||
		kind == Kind::ExpressionStatement ||
		kind == Kind::DoStatement ||
		kind == Kind::ContinueStatement ||
		kind == Kind::BreakStatement ||
		kind == Kind::ReturnStatement ||
		kind == Kind::ThrowStatement ||
		kind == Kind::DebuggerStatement ||
		kind == Kind::PropertyDeclaration ||
		kind == Kind::TypeAliasDeclaration ||
		kind == Kind::ImportDeclaration ||
		kind == Kind::ImportEqualsDeclaration ||
		kind == Kind::ExportDeclaration ||
		kind == Kind::NamespaceExportDeclaration ||
		kind == Kind::ExportAssignment;
}

// NodeIsASICandidate — asi.go:66
bool NodeIsASICandidate(Node* node, SourceFile* file) {
	Node* lastToken = GetLastToken(node, file);
	if (lastToken != nullptr && lastToken->kind == Kind::SemicolonToken) {
		return false;
	}

	if (SyntaxRequiresTrailingCommaOrSemicolonOrASI(node->kind)) {
		if (lastToken != nullptr && lastToken->kind == Kind::CommaToken) {
			return false;
		}
	} else if (SyntaxRequiresTrailingModuleBlockOrSemicolonOrASI(node->kind)) {
		Node* lastChild = GetLastChild(node, file);
		if (lastChild != nullptr && isModuleBlock(lastChild)) {
			return false;
		}
	} else if (SyntaxRequiresTrailingFunctionBlockOrSemicolonOrASI(node->kind)) {
		Node* lastChild = GetLastChild(node, file);
		if (lastChild != nullptr && detail::isFunctionBlock(lastChild)) {
			return false;
		}
	} else if (!SyntaxRequiresTrailingSemicolonOrASI(node->kind)) {
		return false;
	}

	// See comment in parser's `parseDoStatement`
	if (node->kind == Kind::DoStatement) {
		return true;
	}

	Node* topNode = findAncestor(node, [](Node* ancestor) { return ancestor->parent == nullptr; });
	Node* nextToken = astnav::findNextToken(node, topNode, file);
	if (nextToken == nullptr || nextToken->kind == Kind::CloseBraceToken) {
		return true;
	}

	int startLine = getECMALineOfPosition(file, node->end());
	int endLine = getECMALineOfPosition(file, astnav::getStartOfNode(nextToken, file, false /*includeJSDoc*/));
	return startLine != endLine;
}

} // namespace tsc::ls::lsutil
