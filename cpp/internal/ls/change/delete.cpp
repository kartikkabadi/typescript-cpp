// === slice: ls-foundation ===
// ls/change/delete.go — smart node deletion for the ChangeTracker.

#include "internal/ls/change/change.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "internal/astnav/tokens.h"
#include "internal/core/types.h"
#include "internal/debug/debug.h"
#include "internal/format/format.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::ls::change {

namespace {

// core.NewTextRange
TextRange newTextRange(int pos, int end) {
	return TextRange{static_cast<TextPos>(pos), static_cast<TextPos>(end)};
}

// core.Find — first element matching pred, or nullptr.
template <class T, class Pred>
T* find(const std::vector<T*>& v, Pred pred) {
	for (T* e : v) {
		if (pred(e)) return e;
	}
	return nullptr;
}

// positionsAreOnSameLine — delete.go:247
bool positionsAreOnSameLine(int pos1, int pos2, SourceFile* sourceFile) {
	return format::GetLineStartPositionForPosition(pos1, sourceFile) ==
		   format::GetLineStartPositionForPosition(pos2, sourceFile);
}

// isSeparator — tracker.go:779 (file-local there; repeated here because both
// translation units use it).
bool isSeparator(Node* node, Node* candidate) {
	return candidate != nullptr && node->parent != nullptr &&
		   (candidate->kind == Kind::CommaToken ||
			(candidate->kind == Kind::SemicolonToken &&
			 node->parent->kind == Kind::ObjectLiteralExpression));
}

// hasJSDocNodes — delete.go:253. Checks if a node has JSDoc comments.
bool hasJSDocNodes(Node* node) {
	if (node == nullptr) {
		return false;
	}
	// nil is ok for JSDoc - it will return empty slice if not available
	std::vector<Node*> jsdocs = node->jsDoc(nullptr);
	return !jsdocs.empty();
}

void deleteNode(Tracker* t, SourceFile* sourceFile, Node* node, LeadingTriviaOption leadingTrivia,
				TrailingTriviaOption trailingTrivia);
void deleteNodeInList(Tracker* t, std::unordered_map<Node*, bool>& deletedNodesInLists,
					  SourceFile* sourceFile, Node* node);
void deleteVariableDeclaration(Tracker* t, std::unordered_map<Node*, bool>& deletedNodesInLists,
							   SourceFile* sourceFile, Node* node);
void deleteDefaultImport(Tracker* t, SourceFile* sourceFile, Node* importClause);
void deleteImportBinding(Tracker* t, SourceFile* sourceFile, Node* node);

} // namespace

// deleteDeclaration — delete.go:14. Deletes a node with smart handling for
// different node types. This handles special cases like import specifiers in
// lists, parameters, etc.
void deleteDeclaration(Tracker* t, std::unordered_map<Node*, bool>& deletedNodesInLists,
					   SourceFile* sourceFile, Node* node) {
	switch (node->kind) {
	case Kind::Parameter: {
		Node* oldFunction = node->parent;
		if (oldFunction->kind == Kind::ArrowFunction &&
			oldFunction->as<ArrowFunction>()->Parameters->nodes.size() == 1 &&
			astnav::findChildOfKind(oldFunction, Kind::OpenParenToken, sourceFile) == nullptr) {
			// Lambdas with exactly one parameter are special because, after
			// removal, there must be an empty parameter list (i.e. `()`) and
			// this won't necessarily be the case if the parameter is simply
			// removed (e.g. in `x => 1`).
			t->ReplaceTextRangeWithText(
				sourceFile,
				t->GetAdjustedRange(sourceFile, node, node, LeadingTriviaOptionIncludeAll,
									TrailingTriviaOptionInclude),
				"()");
		} else {
			deleteNodeInList(t, deletedNodesInLists, sourceFile, node);
		}
		break;
	}
	case Kind::ImportDeclaration:
	case Kind::ImportEqualsDeclaration: {
		const std::vector<Node*>& imports = sourceFile->imports;
		bool isFirstImport =
			(!imports.empty() && node == imports[0]->parent) ||
			node == find(sourceFile->Statements->nodes,
						 [](Node* s) { return isAnyImportSyntax(s); });
		// For first import, leave header comment in place, otherwise only
		// delete JSDoc comments
		LeadingTriviaOption leadingTrivia = LeadingTriviaOptionStartLine;
		if (isFirstImport) {
			leadingTrivia = LeadingTriviaOptionExclude;
		} else if (hasJSDocNodes(node)) {
			leadingTrivia = LeadingTriviaOptionJSDoc;
		}
		deleteNode(t, sourceFile, node, leadingTrivia, TrailingTriviaOptionInclude);
		break;
	}
	case Kind::BindingElement: {
		Node* pattern = node->parent;
		std::vector<Node*>& elements = pattern->as<BindingPattern>()->Elements->nodes;
		bool preserveComma =
			pattern->kind == Kind::ArrayBindingPattern && node != elements.back();
		if (preserveComma) {
			deleteNode(t, sourceFile, node, LeadingTriviaOptionIncludeAll,
					   TrailingTriviaOptionExclude);
		} else {
			deleteNodeInList(t, deletedNodesInLists, sourceFile, node);
		}
		break;
	}
	case Kind::VariableDeclaration:
		deleteVariableDeclaration(t, deletedNodesInLists, sourceFile, node);
		break;
	case Kind::TypeParameter:
		deleteNodeInList(t, deletedNodesInLists, sourceFile, node);
		break;
	case Kind::ImportSpecifier: {
		Node* namedImports = node->parent;
		if (namedImports->as<NamedImports>()->Elements->nodes.size() == 1) {
			deleteImportBinding(t, sourceFile, namedImports);
		} else {
			deleteNodeInList(t, deletedNodesInLists, sourceFile, node);
		}
		break;
	}
	case Kind::NamespaceImport:
		deleteImportBinding(t, sourceFile, node);
		break;
	case Kind::SemicolonToken:
		deleteNode(t, sourceFile, node, LeadingTriviaOptionIncludeAll, TrailingTriviaOptionExclude);
		break;
	case Kind::TypeKeyword:
		// For type keyword in import clauses, we need to delete the keyword
		// and any trailing space. The trailing space is part of the next
		// token's leading trivia, so we include it.
		deleteNode(t, sourceFile, node, LeadingTriviaOptionExclude, TrailingTriviaOptionInclude);
		break;
	case Kind::FunctionKeyword:
		deleteNode(t, sourceFile, node, LeadingTriviaOptionExclude, TrailingTriviaOptionInclude);
		break;
	case Kind::ClassDeclaration:
	case Kind::FunctionDeclaration: {
		LeadingTriviaOption leadingTrivia = LeadingTriviaOptionStartLine;
		if (hasJSDocNodes(node)) {
			leadingTrivia = LeadingTriviaOptionJSDoc;
		}
		deleteNode(t, sourceFile, node, leadingTrivia, TrailingTriviaOptionInclude);
		break;
	}
	default:
		if (node->parent == nullptr) {
			// a misbehaving client can reach here with the SourceFile node
			deleteNode(t, sourceFile, node, LeadingTriviaOptionIncludeAll,
					   TrailingTriviaOptionInclude);
		} else if (node->parent->kind == Kind::ImportClause &&
				   node->parent->as<ImportClause>()->name == node) {
			deleteDefaultImport(t, sourceFile, node->parent);
		} else if (node->parent->kind == Kind::CallExpression &&
				   std::find(node->parent->as<CallExpression>()->Arguments->nodes.begin(),
							 node->parent->as<CallExpression>()->Arguments->nodes.end(),
							 node) != node->parent->as<CallExpression>()->Arguments->nodes.end()) {
			deleteNodeInList(t, deletedNodesInLists, sourceFile, node);
		} else {
			deleteNode(t, sourceFile, node, LeadingTriviaOptionIncludeAll,
					   TrailingTriviaOptionInclude);
		}
	}
}

namespace {

// deleteDefaultImport — delete.go:100
void deleteDefaultImport(Tracker* t, SourceFile* sourceFile, Node* importClause) {
	ImportClause* clause = importClause->as<ImportClause>();
	if (clause->NamedBindings == nullptr) {
		// Delete the whole import
		deleteNode(t, sourceFile, importClause->parent, LeadingTriviaOptionIncludeAll,
				   TrailingTriviaOptionInclude);
	} else {
		// import |d,| * as ns from './file'
		Node* name = clause->name;
		int start = astnav::getStartOfNode(name, sourceFile, false);
		Node* nextToken = astnav::getTokenAtPosition(sourceFile, name->end());
		if (nextToken != nullptr && nextToken->kind == Kind::CommaToken) {
			// shift first non-whitespace position after comma to the start
			// position of the node
			SkipTriviaOptions triviaOpts;
			triviaOpts.stopAfterLineBreak = false;
			triviaOpts.stopAtComments = true;
			int end = skipTriviaEx(sourceFile->Text(), nextToken->end(), triviaOpts);
			t->ReplaceTextRangeWithText(sourceFile, newTextRange(start, end), "");
		} else {
			deleteNode(t, sourceFile, name, LeadingTriviaOptionIncludeAll,
					   TrailingTriviaOptionInclude);
		}
	}
}

// deleteImportBinding — delete.go:117
void deleteImportBinding(Tracker* t, SourceFile* sourceFile, Node* node) {
	ImportClause* importClause = node->parent->as<ImportClause>();
	if (importClause->name != nullptr) {
		// Delete named imports while preserving the default import
		// import d|, * as ns| from './file'
		// import d|, { a }| from './file'
		Node* previousToken = astnav::getTokenAtPosition(sourceFile, node->pos() - 1);
		debug::assert(previousToken != nullptr, "previousToken should not be nil");
		int start = astnav::getStartOfNode(previousToken, sourceFile, false);
		t->ReplaceTextRangeWithText(sourceFile, newTextRange(start, node->end()), "");
	} else {
		// Delete the entire import declaration
		// |import * as ns from './file'|
		// |import { a } from './file'|
		Node* importDecl = findAncestorKind(node, Kind::ImportDeclaration);
		debug::assert(importDecl != nullptr, "importDecl should not be nil");
		deleteNode(t, sourceFile, importDecl, LeadingTriviaOptionIncludeAll,
				   TrailingTriviaOptionInclude);
	}
}

// deleteVariableDeclaration — delete.go:142
void deleteVariableDeclaration(Tracker* t, std::unordered_map<Node*, bool>& deletedNodesInLists,
							   SourceFile* sourceFile, Node* node) {
	Node* parent = node->parent;

	if (parent->kind == Kind::CatchClause) {
		// TODO: There's currently no unused diagnostic for this, could be a
		// suggestion
		Node* openParen = astnav::findChildOfKind(parent, Kind::OpenParenToken, sourceFile);
		Node* closeParen = astnav::findChildOfKind(parent, Kind::CloseParenToken, sourceFile);
		debug::assert(openParen != nullptr && closeParen != nullptr,
					  "catch clause should have parens");
		t->DeleteNodeRange(sourceFile, openParen, closeParen, LeadingTriviaOptionIncludeAll,
						   TrailingTriviaOptionInclude);
		return;
	}

	if (parent->as<VariableDeclarationList>()->Declarations->nodes.size() != 1) {
		deleteNodeInList(t, deletedNodesInLists, sourceFile, node);
		return;
	}

	Node* gp = parent->parent;
	switch (gp->kind) {
	case Kind::ForOfStatement:
	case Kind::ForInStatement: {
		t->ReplaceNode(sourceFile, node,
					   t->nodeFactory->newObjectLiteralExpression(
						   t->nodeFactory->newNodeList(std::vector<Node*>{}), false),
					   nullptr);
		break;
	}
	case Kind::ForStatement:
		deleteNode(t, sourceFile, parent, LeadingTriviaOptionIncludeAll,
				   TrailingTriviaOptionInclude);
		break;
	case Kind::VariableStatement: {
		LeadingTriviaOption leadingTrivia = LeadingTriviaOptionStartLine;
		if (hasJSDocNodes(gp)) {
			leadingTrivia = LeadingTriviaOptionJSDoc;
		}
		deleteNode(t, sourceFile, gp, leadingTrivia, TrailingTriviaOptionInclude);
		break;
	}
	default:
		debug::fail("Unexpected grandparent kind");
	}
}

// deleteNode — delete.go:179. Deletes a node with the specified trivia
// options. Warning: This deletes comments too.
void deleteNode(Tracker* t, SourceFile* sourceFile, Node* node, LeadingTriviaOption leadingTrivia,
				TrailingTriviaOption trailingTrivia) {
	int startPosition = t->getAdjustedStartPosition(sourceFile, node, leadingTrivia, false);
	int endPosition = t->getAdjustedEndPosition(sourceFile, node, trailingTrivia);
	t->ReplaceTextRangeWithText(sourceFile, newTextRange(startPosition, endPosition), "");
}

// deleteNodeInList — delete.go:185
void deleteNodeInList(Tracker* t, std::unordered_map<Node*, bool>& deletedNodesInLists,
					  SourceFile* sourceFile, Node* node) {
	NodeList* containingList = format::GetContainingList(node, sourceFile);
	debug::assert(containingList != nullptr, "containingList should not be nil");
	auto it = std::find(containingList->nodes.begin(), containingList->nodes.end(), node);
	int index =
		it == containingList->nodes.end()
			? -1
			: static_cast<int>(it - containingList->nodes.begin());
	debug::assert(index != -1, "node should be in containing list");

	if (containingList->nodes.size() == 1) {
		deleteNode(t, sourceFile, node, LeadingTriviaOptionIncludeAll, TrailingTriviaOptionInclude);
		return;
	}

	// Note: We will only delete a comma *after* a node. This will leave a
	// trailing comma if we delete the last node. That's handled in the end by
	// finishTrailingCommaAfterDeletingNodesInList.
	debug::assert(!deletedNodesInLists[node], "Deleting a node twice");
	deletedNodesInLists[node] = true;

	int startPos = t->startPositionToDeleteNodeInList(sourceFile, node);
	int endPos;
	if (index == static_cast<int>(containingList->nodes.size()) - 1) {
		endPos = t->getAdjustedEndPosition(sourceFile, node, TrailingTriviaOptionNone);
	} else {
		Node* prevNode = nullptr;
		if (index > 0) {
			prevNode = containingList->nodes[index - 1];
		}
		endPos = t->endPositionToDeleteNodeInList(sourceFile, node, prevNode,
												  containingList->nodes[index + 1]);
	}

	t->ReplaceTextRangeWithText(sourceFile, newTextRange(startPos, endPos), "");
}

} // namespace

// startPositionToDeleteNodeInList — delete.go:215. Finds the first
// non-whitespace position in the leading trivia of the node.
int Tracker::startPositionToDeleteNodeInList(SourceFile* sourceFile, Node* node) {
	int start = getAdjustedStartPosition(sourceFile, node, LeadingTriviaOptionIncludeAll, false);
	SkipTriviaOptions triviaOpts;
	triviaOpts.stopAfterLineBreak = false;
	triviaOpts.stopAtComments = true;
	return skipTriviaEx(sourceFile->Text(), start, triviaOpts);
}

// endPositionToDeleteNodeInList — delete.go:221
int Tracker::endPositionToDeleteNodeInList(SourceFile* sourceFile, Node* node, Node* prevNode,
										   Node* nextNode) {
	int end = startPositionToDeleteNodeInList(sourceFile, nextNode);
	if (prevNode == nullptr ||
		positionsAreOnSameLine(getAdjustedEndPosition(sourceFile, node, TrailingTriviaOptionInclude),
							   end, sourceFile)) {
		return end;
	}
	Node* token =
		astnav::findPrecedingToken(sourceFile, astnav::getStartOfNode(nextNode, sourceFile, false));
	if (isSeparator(node, token)) {
		Node* prevToken =
			astnav::findPrecedingToken(sourceFile, astnav::getStartOfNode(node, sourceFile, false));
		if (isSeparator(prevNode, prevToken)) {
			SkipTriviaOptions triviaOpts;
			triviaOpts.stopAfterLineBreak = true;
			triviaOpts.stopAtComments = true;
			int pos = skipTriviaEx(sourceFile->Text(), token->end(), triviaOpts);
			if (positionsAreOnSameLine(astnav::getStartOfNode(prevToken, sourceFile, false),
									   astnav::getStartOfNode(token, sourceFile, false),
									   sourceFile)) {
				if (pos > 0 &&
					isLineBreak(
						static_cast<unsigned char>(sourceFile->Text()[pos - 1]))) {
					return pos - 1;
				}
				return pos;
			}
			if (isLineBreak(static_cast<unsigned char>(sourceFile->Text()[pos]))) {
				return pos;
			}
		}
	}
	return end;
}

} // namespace tsc::ls::change
