// Port of tsc/internal/format/indent.go.
#include "internal/format/format.h"

#include <algorithm>

#include "internal/astnav/tokens.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::format {

// Go: ast.IsStatementButNotDeclaration.
static bool isStatementButNotDeclaration(Node* node) {
	return isStatementKindButNotDeclarationKind(node->kind);
}

// Go: CommentRange.ContainsExclusive — pos < position < end.
static bool commentRangeContainsExclusive(const CommentRange& r, int position) {
	return r.pos() < position && position < r.end();
}

int GetIndentationForNode(Node* n, TextRange* ignoreActualIndentationRange,
						  SourceFile* sourceFile, const lsutil::FormatCodeSettings& options) {
	auto [startline, startpos] =
		getECMALineAndByteOffsetOfPosition(sourceFile,
										   getTokenPosOfNode(n, sourceFile, false));
	return getIndentationForNodeWorker(n, startline, startpos,
									   ignoreActualIndentationRange /*indentationDelta*/, 0,
									   sourceFile /*isNextChild*/, false, options);
}

// GetIndentation computes the expected indentation for a position in a source file.
// This is the Go port of SmartIndenter.getIndentation from TypeScript.
int GetIndentation(int position, SourceFile* sourceFile, const lsutil::FormatCodeSettings& options,
				   bool assumeNewLineBeforeCloseBrace) {
	if (position > static_cast<int>(sourceFile->text.size())) {
		return options.BaseIndentSize; // past EOF
	}

	// no indentation when the indent style is set to none,
	// so we can return fast
	if (options.IndentStyle == lsutil::IndentStyle::None) {
		return 0;
	}

	Node* precedingToken =
		astnav::findPrecedingTokenEx(sourceFile, position, nullptr /*startNode*/,
									 true /*excludeJSDoc*/);

	std::optional<CommentRange> enclosingCommentRange =
		getRangeOfEnclosingComment(sourceFile, position, precedingToken);
	if (enclosingCommentRange.has_value() &&
		enclosingCommentRange->kind == Kind::MultiLineCommentTrivia) {
		return getCommentIndent(sourceFile, position, options, &*enclosingCommentRange);
	}

	if (precedingToken == nullptr) {
		return options.BaseIndentSize;
	}

	// no indentation in string/regex/template literals
	if (isStringOrRegularExpressionOrTemplateLiteral(precedingToken->kind)) {
		int tokenStart = getTokenPosOfNode(precedingToken, sourceFile, false);
		if (tokenStart <= position && position < precedingToken->end()) {
			return 0;
		}
	}

	int lineAtPosition = getECMALineOfPosition(sourceFile, position);

	// indentation is first non-whitespace character in a previous line
	// for block indentation, we should look for a line which contains something that's not
	// whitespace.
	Node* currentToken = astnav::getTokenAtPosition(sourceFile, position);
	// For object literals, we want indentation to work just like with blocks.
	// If the `{` starts in any position (even in the middle of a line), then
	// the following indentation should treat `{` as the start of that line (including leading whitespace).
	bool isObjectLiteral = currentToken->kind == Kind::OpenBraceToken &&
		currentToken->parent != nullptr &&
		currentToken->parent->kind == Kind::ObjectLiteralExpression;
	if (options.IndentStyle == lsutil::IndentStyle::Block || isObjectLiteral) {
		return getBlockIndent(sourceFile, position, options);
	}

	if (precedingToken->kind == Kind::CommaToken && precedingToken->parent != nullptr &&
		precedingToken->parent->kind != Kind::BinaryExpression) {
		// previous token is comma that separates items in list - find the previous item and try to derive indentation from it
		int actualIndentation =
			getActualIndentationForListItemBeforeComma(precedingToken, sourceFile, options);
		if (actualIndentation != -1) {
			return actualIndentation;
		}
	}

	NodeList* containerList = getListByPosition(position, precedingToken->parent, sourceFile);
	// use list position if the preceding token is before any list items
	if (containerList != nullptr && !precedingToken->loc.containedBy(containerList->loc)) {
		bool useTheSameBaseIndentation =
			currentToken->parent != nullptr &&
			(currentToken->parent->kind == Kind::FunctionExpression ||
			 currentToken->parent->kind == Kind::ArrowFunction);
		int indentSize = 0;
		if (!useTheSameBaseIndentation) {
			indentSize = options.IndentSize;
		}
		int res = getActualIndentationForListStartLine(containerList, sourceFile, options);
		if (res == -1) {
			return indentSize;
		}
		return res + indentSize;
	}

	return getSmartIndent(sourceFile, position, precedingToken, lineAtPosition,
						  assumeNewLineBeforeCloseBrace, options);
}

int getCommentIndent(SourceFile* sourceFile, int position,
					 const lsutil::FormatCodeSettings& options,
					 CommentRange* enclosingCommentRange) {
	int previousLine = getECMALineOfPosition(sourceFile, position) - 1;
	int commentStartLine = getECMALineOfPosition(sourceFile, enclosingCommentRange->pos());

	TSC_ASSERT(commentStartLine >= 0, "commentStartLine >= 0");

	if (previousLine <= commentStartLine) {
		const ECMALineStarts& lineStarts = getECMALineStarts(sourceFile);
		return FindFirstNonWhitespaceColumn(static_cast<int>(lineStarts[commentStartLine]),
											position, sourceFile, options);
	}

	const ECMALineStarts& lineStarts = getECMALineStarts(sourceFile);
	int startPositionOfLine = static_cast<int>(lineStarts[previousLine]);
	auto [character, column] = findFirstNonWhitespaceCharacterAndColumn(startPositionOfLine,
																	  position, sourceFile,
																	  options);

	if (column == 0) {
		return column;
	}

	char32_t firstNonWhitespaceCharacterCode = static_cast<unsigned char>(
		sourceFile->text[startPositionOfLine + character]);
	if (firstNonWhitespaceCharacterCode == U'*') {
		return column - 1;
	}
	return column;
}

void getLeadingCommentRangesOfNode(Node* node, SourceFile* file,
								   const std::function<bool(const CommentRange&)>& yield) {
	if (node->kind == Kind::JsxText) {
		return;
	}
	getLeadingCommentRanges(file->text, node->pos(), yield);
}

std::optional<CommentRange> getRangeOfEnclosingComment(
	SourceFile* sourceFile,
	int position,
	Node* precedingToken) {
	Node* tokenAtPosition = astnav::getTokenAtPosition(sourceFile, position);
	Node* jsdoc = findAncestor(tokenAtPosition, [](Node* n) { return isJSDoc(n); });
	if (jsdoc != nullptr) {
		tokenAtPosition = jsdoc->parent;
	}
	int tokenStart = astnav::getStartOfNode(tokenAtPosition, sourceFile,
										  false /*includeJSDoc*/);
	if (tokenStart <= position && position < tokenAtPosition->end()) {
		return std::nullopt;
	}

	// Between two consecutive tokens, all comments are either trailing on the former
	// or leading on the latter (and none are in both lists).
	std::optional<CommentRange> result;
	auto yield = [&](const CommentRange& commentRange) -> bool {
		if (commentRangeContainsExclusive(commentRange, position) ||
			(position == commentRange.end() &&
			 (commentRange.kind == Kind::SingleLineCommentTrivia ||
			  position == static_cast<int>(sourceFile->text.size())))) {
			result = commentRange;
			return false; // stop iterating (yield returns true to continue)
		}
		return true;
	};
	if (precedingToken != nullptr) {
		getTrailingCommentRanges(sourceFile->text, precedingToken->end(), yield);
	}
	if (!result.has_value()) {
		getLeadingCommentRangesOfNode(tokenAtPosition, sourceFile, yield);
	}
	return result;
}

int getBlockIndent(SourceFile* sourceFile, int position,
				   const lsutil::FormatCodeSettings& options) {
	// move backwards until we find a line with a non-whitespace character,
	// then find the first non-whitespace character for that line.
	int current = position;
	while (current > 0) {
		int size;
		char32_t ch = decodeUtf8Rune(std::string_view(sourceFile->text).substr(current), &size);
		if (!isWhiteSpaceLike(ch)) {
			break;
		}
		current -= size;
	}

	int lineStart = GetLineStartPositionForPosition(current, sourceFile);
	return FindFirstNonWhitespaceColumn(lineStart, current, sourceFile, options);
}

int getActualIndentationForListItemBeforeComma(Node* commaToken, SourceFile* sourceFile,
											   const lsutil::FormatCodeSettings& options) {
	// previous token is comma that separates items in list - find the previous item and try to derive indentation from it
	if (commaToken->parent == nullptr) {
		return -1;
	}
	NodeList* containingList = GetContainingList(commaToken, sourceFile);
	if (containingList == nullptr) {
		return -1;
	}
	auto commaIt = std::find(containingList->nodes.begin(), containingList->nodes.end(), commaToken);
	int commaIndex = static_cast<int>(commaIt - containingList->nodes.begin());
	if (commaIndex > 0) {
		return deriveActualIndentationFromList(containingList, commaIndex - 1, sourceFile, options);
	}
	return -1;
}

nextTokenKind nextTokenIsCurlyBraceOnSameLineAsCursor(Node* precedingToken, Node* current,
													  int lineAtPosition, SourceFile* sourceFile) {
	Node* nextToken = astnav::findNextToken(precedingToken, current, sourceFile);
	if (nextToken == nullptr) {
		return nextTokenKind::nextTokenKindUnknown;
	}

	if (nextToken->kind == Kind::OpenBraceToken) {
		// open braces are always indented at the parent level
		return nextTokenKind::nextTokenKindOpenBrace;
	} else if (nextToken->kind == Kind::CloseBraceToken) {
		// close braces are indented at the parent level if they are located on the same line with cursor
		int nextTokenStartLine = getStartLineForNode(nextToken, sourceFile);
		if (lineAtPosition == nextTokenStartLine) {
			return nextTokenKind::nextTokenKindCloseBrace;
		}
		return nextTokenKind::nextTokenKindUnknown;
	}

	return nextTokenKind::nextTokenKindUnknown;
}

int getSmartIndent(SourceFile* sourceFile, int position, Node* precedingToken,
				   int lineAtPosition, bool assumeNewLineBeforeCloseBrace,
				   const lsutil::FormatCodeSettings& options) {
	// try to find node that can contribute to indentation and includes 'position' starting from 'precedingToken'
	// if such node is found - compute initial indentation for 'position' inside this node
	Node* previous = nullptr;
	Node* current = precedingToken;

	while (current != nullptr) {
		if (lsutil::PositionBelongsToNode(current, position, sourceFile) &&
			ShouldIndentChildNode(options, current, previous, sourceFile, true)) {
			auto [currentStartLine, currentStartChar] =
				getStartLineAndCharacterForNode(current, sourceFile);
			nextTokenKind ntk = nextTokenIsCurlyBraceOnSameLineAsCursor(precedingToken, current,
																		lineAtPosition, sourceFile);
			int indentationDelta = 0;
			if (ntk != nextTokenKind::nextTokenKindUnknown) {
				// handle cases when codefix is about to be inserted before the close brace
				if (assumeNewLineBeforeCloseBrace &&
					ntk == nextTokenKind::nextTokenKindCloseBrace) {
					indentationDelta = options.IndentSize;
				}
				// else 0
			} else {
				if (lineAtPosition != currentStartLine) {
					indentationDelta = options.IndentSize;
				}
			}
			return getIndentationForNodeWorker(current, currentStartLine, currentStartChar, nullptr,
											   indentationDelta, sourceFile, true, options);
		}

		// check if current node is a list item - if yes, take indentation from it
		// do not consider parent-child line sharing yet:
		// function foo(a
		//    | preceding node 'a' does share line with its parent but indentation is expected
		int actualIndentation =
			getActualIndentationForListItem(current, sourceFile, options, true /*listIndentsChild*/);
		if (actualIndentation != -1) {
			return actualIndentation;
		}

		previous = current;
		current = current->parent;
	}
	// no parent was found - return the base indentation of the SourceFile
	return options.BaseIndentSize;
}

int getIndentationForNodeWorker(
	Node* current,
	int currentStartLine,
	int currentStartCharacter,
	TextRange* ignoreActualIndentationRange,
	int indentationDelta,
	SourceFile* sourceFile,
	bool isNextChild,
	const lsutil::FormatCodeSettings& options) {
	Node* parent = current->parent;

	// Walk up the tree and collect indentation for parent-child node pairs. Indentation is not added if
	// * parent and child nodes start on the same line, or
	// * parent is an IfStatement and child starts on the same line as an 'else clause'.
	while (parent != nullptr) {
		bool useActualIndentation = true;
		if (ignoreActualIndentationRange != nullptr) {
			int start = getTokenPosOfNode(current, sourceFile, false);
			useActualIndentation = start < ignoreActualIndentationRange->pos() ||
				start > ignoreActualIndentationRange->end();
		}

		auto [containingListOrParentStartLine, containingListOrParentStartCharacter] =
			getContainingListOrParentStart(parent, current, sourceFile);
		bool parentAndChildShareLine = containingListOrParentStartLine == currentStartLine ||
			childStartsOnTheSameLineWithElseInIfStatement(parent, current, currentStartLine,
														  sourceFile);

		if (useActualIndentation) {
			// check if current node is a list item - if yes, take indentation from it
			Node* firstListChild = nullptr;
			NodeList* containerList = GetContainingList(current, sourceFile);
			if (containerList != nullptr && !containerList->nodes.empty()) {
				firstListChild = containerList->nodes.front();
			}
			// A list indents its children if the children begin on a later line than the list itself:
			//
			// f1(               L0 - List start
			//   {               L1 - First child start: indented, along with all other children
			//     prop: 0
			//   },
			//   {
			//     prop: 1
			//   }
			// )
			//
			// f2({             L0 - List start and first child start: children are not indented.
			//   prop: 0             Object properties are indented only one level, because the list
			// }, {                  itself contributes nothing.
			//   prop: 1        L3 - The indentation of the second object literal is best understood by
			// })                    looking at the relationship between the list and *first* list item.
			bool listIndentsChild = false;
			if (firstListChild != nullptr) {
				int listLine = getStartLineForNode(firstListChild, sourceFile);
				listIndentsChild = listLine > containingListOrParentStartLine;
			}
			int actualIndentation = getActualIndentationForListItem(current, sourceFile, options,
																	listIndentsChild);
			if (actualIndentation != -1) {
				return actualIndentation + indentationDelta;
			}

			// try to fetch actual indentation for current node from source text
			actualIndentation = getActualIndentationForNode(current, parent, currentStartLine,
															currentStartCharacter,
															parentAndChildShareLine, sourceFile,
															options);
			if (actualIndentation != -1) {
				return actualIndentation + indentationDelta;
			}
		}

		// increase indentation if parent node wants its content to be indented and parent and child nodes don't start on the same line
		if (ShouldIndentChildNode(options, parent, current, sourceFile, isNextChild) &&
			!parentAndChildShareLine) {
			indentationDelta += options.IndentSize;
		}

		// In our AST, a call argument's `parent` is the call-expression, not the argument list.
		// We would like to increase indentation based on the relationship between an argument and its argument-list,
		// so we spoof the starting position of the (parent) call-expression to match the (non-parent) argument-list.
		// But, the spoofed start-value could then cause a problem when comparing the start position of the call-expression
		// to *its* parent (in the case of an iife, an expression statement), adding an extra level of indentation.
		//
		// Instead, when at an argument, we unspoof the starting position of the enclosing call expression
		// *after* applying indentation for the argument.

		bool useTrueStart = isArgumentAndStartLineOverlapsExpressionBeingCalled(
			parent, current, currentStartLine, sourceFile);

		current = parent;
		parent = current->parent;

		if (useTrueStart) {
			auto [l, c] = getECMALineAndByteOffsetOfPosition(
				sourceFile, getTokenPosOfNode(current, sourceFile, false));
			currentStartLine = l;
			currentStartCharacter = c;
		} else {
			currentStartLine = containingListOrParentStartLine;
			currentStartCharacter = containingListOrParentStartCharacter;
		}
	}

	return indentationDelta + options.BaseIndentSize;
}

/*
* Function returns -1 if actual indentation for node should not be used (i.e because node is nested expression)
 */
int getActualIndentationForNode(Node* current, Node* parent, int cuurentLine, int currentChar,
								bool parentAndChildShareLine, SourceFile* sourceFile,
								const lsutil::FormatCodeSettings& options) {
	// actual indentation is used for statements\declarations if one of cases below is true:
	// - parent is SourceFile - by default immediate children of SourceFile are not indented except when user indents them manually
	// - parent and child are not on the same line
	bool useActualIndentation = (isDeclaration(current) || isStatementButNotDeclaration(current)) &&
		(parent->kind == Kind::SourceFile || !parentAndChildShareLine);

	if (!useActualIndentation) {
		return -1;
	}

	return findColumnForFirstNonWhitespaceCharacterInLine(cuurentLine, currentChar, sourceFile,
														  options);
}

bool isArgumentAndStartLineOverlapsExpressionBeingCalled(Node* parent, Node* child,
														 int childStartLine,
														 SourceFile* sourceFile) {
	if (!isCallExpression(parent)) {
		return false;
	}
	auto args = parent->arguments();
	if (std::find(args.begin(), args.end(), child) == args.end()) {
		return false;
	}
	int expressionOfCallExpressionEnd = parent->expression()->end();
	int expressionOfCallExpressionEndLine =
		getECMALineOfPosition(sourceFile, expressionOfCallExpressionEnd);
	return expressionOfCallExpressionEndLine == childStartLine;
}

int getActualIndentationForListItem(Node* node, SourceFile* sourceFile,
									const lsutil::FormatCodeSettings& options,
									bool listIndentsChild) {
	if (node->parent != nullptr && node->parent->kind == Kind::VariableDeclarationList) {
		// VariableDeclarationList has no wrapping tokens
		return -1;
	}
	NodeList* containingList = GetContainingList(node, sourceFile);
	if (containingList != nullptr) {
		auto it = std::find(containingList->nodes.begin(), containingList->nodes.end(), node);
		int index = it == containingList->nodes.end()
			? -1
			: static_cast<int>(it - containingList->nodes.begin());
		if (index != -1) {
			int result = deriveActualIndentationFromList(containingList, index, sourceFile, options);
			if (result != -1) {
				return result;
			}
		}
		int delta = 0;
		if (listIndentsChild) {
			delta = options.IndentSize;
		}
		int res = getActualIndentationForListStartLine(containingList, sourceFile, options);
		if (res == -1) {
			return delta;
		}
		return res + delta;
	}
	return -1;
}

int getActualIndentationForListStartLine(NodeList* list, SourceFile* sourceFile,
										 const lsutil::FormatCodeSettings& options) {
	if (list == nullptr) {
		return -1;
	}
	auto [line, ch] = getECMALineAndByteOffsetOfPosition(sourceFile, list->loc.pos());
	return findColumnForFirstNonWhitespaceCharacterInLine(line, ch, sourceFile, options);
}

int deriveActualIndentationFromList(NodeList* list, int index, SourceFile* sourceFile,
									const lsutil::FormatCodeSettings& options) {
	TSC_ASSERT(list != nullptr && index >= 0 && index < static_cast<int>(list->nodes.size()),
			   "list and index in range");

	Node* node = list->nodes[index];

	// walk toward the start of the list starting from current node and check if the line is the same for all items.
	// if end line for item [i - 1] differs from the start line for item [i] - find column of the first non-whitespace character on the line of item [i]

	auto [line, ch] = getStartLineAndCharacterForNode(node, sourceFile);

	for (int i = index; i >= 0; i--) {
		if (list->nodes[i]->kind == Kind::CommaToken) {
			continue;
		}
		// skip list items that ends on the same line with the current list element
		int prevEndLine = getECMALineOfPosition(sourceFile, list->nodes[i]->end());
		if (prevEndLine != line) {
			return findColumnForFirstNonWhitespaceCharacterInLine(line, ch, sourceFile, options);
		}

		auto [l2, c2] = getStartLineAndCharacterForNode(list->nodes[i], sourceFile);
		line = l2;
		ch = c2;
	}
	return -1;
}

int findColumnForFirstNonWhitespaceCharacterInLine(int line, int ch, SourceFile* sourceFile,
												   const lsutil::FormatCodeSettings& options) {
	int lineStart = getECMAPositionOfLineAndByteOffset(sourceFile, line, 0);
	return FindFirstNonWhitespaceColumn(lineStart, lineStart + ch, sourceFile, options);
}

int FindFirstNonWhitespaceColumn(int startPos, int endPos, SourceFile* sourceFile,
								 const lsutil::FormatCodeSettings& options) {
	auto [_, col] = findFirstNonWhitespaceCharacterAndColumn(startPos, endPos, sourceFile, options);
	return col;
}

/**
* Character is the actual index of the character since the beginning of the line.
* Column - position of the character after expanding tabs to spaces.
* "0\t2$"
* value of 'character' for '$' is 3
* value of 'column' for '$' is 6 (assuming that tab size is 4)
 */
std::pair<int, int> findFirstNonWhitespaceCharacterAndColumn(int startPos, int endPos,
															 SourceFile* sourceFile,
															 const lsutil::FormatCodeSettings& options) {
	int column = 0;
	const std::string& text = sourceFile->text;
	int pos = startPos;
	while (pos < endPos) {
		int size;
		char32_t ch = decodeUtf8Rune(std::string_view(text).substr(pos), &size);
		if (!isWhiteSpaceSingleLine(ch)) {
			break;
		}

		if (ch == U'\t') {
			if (options.TabSize > 0) {
				column += options.TabSize + (column % options.TabSize);
			}
		} else {
			column++;
		}

		pos += size;
	}
	return {pos - startPos, column};
}

bool childStartsOnTheSameLineWithElseInIfStatement(Node* parent, Node* child, int childStartLine,
												   SourceFile* sourceFile) {
	if (parent->kind == Kind::IfStatement &&
		parent->as<IfStatement>()->ElseStatement == child) {
		Node* elseKeyword = astnav::findPrecedingToken(sourceFile, child->pos());
		TSC_ASSERT(elseKeyword != nullptr, "elseKeyword != nullptr");
		int elseKeywordStartLine = getStartLineForNode(elseKeyword, sourceFile);
		return elseKeywordStartLine == childStartLine;
	}
	return false;
}

std::pair<int, int> getStartLineAndCharacterForNode(Node* n, SourceFile* sourceFile) {
	return getECMALineAndByteOffsetOfPosition(sourceFile,
											  getTokenPosOfNode(n, sourceFile, false));
}

int getStartLineForNode(Node* n, SourceFile* sourceFile) {
	return getECMALineOfPosition(sourceFile, getTokenPosOfNode(n, sourceFile, false));
}

NodeList* GetContainingList(Node* node, SourceFile* sourceFile) {
	if (node->parent == nullptr) {
		return nullptr;
	}
	return getListByRange(getTokenPosOfNode(node, sourceFile, false), node->end(), node->parent,
						  sourceFile);
}

NodeList* getListByPosition(int pos, Node* node, SourceFile* sourceFile) {
	if (node == nullptr) {
		return nullptr;
	}
	return getListByRange(pos, pos, node, sourceFile);
}

NodeList* getListByRange(int start, int end, Node* node, SourceFile* sourceFile) {
	TextRange r{TextPos(start), TextPos(end)};
	switch (node->kind) {
	case Kind::TypeReference:
		return getList(node->typeArgumentList(), r, node, sourceFile);
	case Kind::ObjectLiteralExpression:
		return getList(node->propertyList(), r, node, sourceFile);
	case Kind::ArrayLiteralExpression:
		return getList(node->elementList(), r, node, sourceFile);
	case Kind::TypeLiteral:
		return getList(node->memberList(), r, node, sourceFile);
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::CallSignature:
	case Kind::Constructor:
	case Kind::ConstructorType:
	case Kind::ConstructSignature: {
		NodeList* tpl = getList(node->typeParameterList(), r, node, sourceFile);
		if (tpl != nullptr) {
			return tpl;
		}
		return getList(node->parameterList(), r, node, sourceFile);
	}
	case Kind::GetAccessor:
		return getList(node->parameterList(), r, node, sourceFile);
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSDocTemplateTag:
		return getList(node->typeParameterList(), r, node, sourceFile);
	case Kind::NewExpression:
	case Kind::CallExpression: {
		NodeList* l = getList(node->typeArgumentList(), r, node, sourceFile);
		if (l != nullptr) {
			return l;
		}
		return getList(node->argumentList(), r, node, sourceFile);
	}
	case Kind::VariableDeclarationList:
		return getList(node->as<VariableDeclarationList>()->Declarations, r, node, sourceFile);
	case Kind::ObjectBindingPattern:
	case Kind::ArrayBindingPattern:
	case Kind::NamedImports:
	case Kind::NamedExports:
		return getList(node->elementList(), r, node, sourceFile);
	default:
		break;
	}
	return nullptr; // TODO: should this be a panic? It isn't in strada.
}

NodeList* getList(NodeList* list, TextRange r, Node* node, SourceFile* sourceFile) {
	if (list == nullptr) {
		return nullptr;
	}
	if (r.containedBy(getVisualListRange(node, list->loc, sourceFile))) {
		return list;
	}
	return nullptr;
}

TextRange getVisualListRange(Node* node, TextRange list, SourceFile* sourceFile) {
	// In strada, this relied on the services .getChildren method, which manifested synthetic token nodes
	// _however_, the logic boils down to "find the child with the matching span and adjust its start to the
	// previous (possibly token) child's end and its end to the token start of the following element" - basically
	// expanding the range to encompass all the neighboring non-token trivia
	// Now, we perform that logic with the scanner instead
	Node* prior = astnav::findPrecedingToken(sourceFile, list.pos());
	int priorEnd;
	if (prior == nullptr) {
		priorEnd = list.pos();
	} else {
		priorEnd = prior->end();
	}
	// Find the token that starts at or after list.End() using the scanner
	Scanner scan;
	getScannerForSourceFile(scan, sourceFile, list.end());
	int nextStart;
	if (scan.token() == Kind::EndOfFile) {
		nextStart = list.end();
	} else {
		nextStart = scan.tokenStart();
	}
	return TextRange{TextPos(priorEnd), TextPos(nextStart)};
}

std::pair<int, int> getContainingListOrParentStart(Node* parent, Node* child,
												   SourceFile* sourceFile) {
	NodeList* containingList = GetContainingList(child, sourceFile);
	int startPos;
	if (containingList != nullptr) {
		startPos = containingList->loc.pos();
	} else {
		startPos = getTokenPosOfNode(parent, sourceFile, false);
	}
	return getECMALineAndByteOffsetOfPosition(sourceFile, startPos);
}

bool isControlFlowEndingStatement(Kind kind, Kind parentKind) {
	switch (kind) {
	case Kind::ReturnStatement:
	case Kind::ThrowStatement:
	case Kind::ContinueStatement:
	case Kind::BreakStatement:
		return parentKind != Kind::Block;
	default:
		return false;
	}
}

/**
* True when the parent node should indent the given child by an explicit rule.
* @param isNextChild If true, we are judging indent of a hypothetical child *after* this one, not the current child.
 */
bool ShouldIndentChildNode(const lsutil::FormatCodeSettings& settings, Node* parent, Node* child,
						   SourceFile* sourceFile, bool isNextChild) {
	return NodeWillIndentChild(settings, parent, child, sourceFile, false) &&
		!(isNextChild && child != nullptr &&
		  isControlFlowEndingStatement(child->kind, parent->kind));
}

bool NodeWillIndentChild(const lsutil::FormatCodeSettings& settings, Node* parent, Node* child,
						 SourceFile* sourceFile, bool indentByDefault) {
	Kind childKind = Kind::Unknown;
	if (child != nullptr) {
		childKind = child->kind;
	}

	switch (parent->kind) {
	case Kind::ExpressionStatement:
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::InterfaceDeclaration:
	case Kind::EnumDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::ArrayLiteralExpression:
	case Kind::Block:
	case Kind::ModuleBlock:
	case Kind::ObjectLiteralExpression:
	case Kind::TypeLiteral:
	case Kind::MappedType:
	case Kind::TupleType:
	case Kind::ParenthesizedExpression:
	case Kind::PropertyAccessExpression:
	case Kind::CallExpression:
	case Kind::NewExpression:
	case Kind::VariableStatement:
	case Kind::ExportAssignment:
	case Kind::ReturnStatement:
	case Kind::ConditionalExpression:
	case Kind::ArrayBindingPattern:
	case Kind::ObjectBindingPattern:
	case Kind::JsxOpeningElement:
	case Kind::JsxOpeningFragment:
	case Kind::JsxSelfClosingElement:
	case Kind::JsxExpression:
	case Kind::MethodSignature:
	case Kind::CallSignature:
	case Kind::ConstructSignature:
	case Kind::Parameter:
	case Kind::FunctionType:
	case Kind::ConstructorType:
	case Kind::ParenthesizedType:
	case Kind::TaggedTemplateExpression:
	case Kind::AwaitExpression:
	case Kind::NamedExports:
	case Kind::NamedImports:
	case Kind::ExportSpecifier:
	case Kind::ImportSpecifier:
	case Kind::PropertyDeclaration:
	case Kind::CaseClause:
	case Kind::DefaultClause:
		return true;
	case Kind::CaseBlock:
		return tristateIsTrueOrUnknown(settings.IndentSwitchCase);
	case Kind::VariableDeclaration:
	case Kind::PropertyAssignment:
	case Kind::BinaryExpression:
		if (tristateIsFalseOrUnknown(settings.IndentMultiLineObjectLiteralBeginningOnBlankLine) &&
			sourceFile != nullptr && childKind == Kind::ObjectLiteralExpression) {
			return rangeIsOnOneLine(child->loc, sourceFile);
		}
		if (parent->kind == Kind::BinaryExpression && sourceFile != nullptr &&
			childKind == Kind::JsxElement) {
			int parentStartLine =
				getECMALineOfPosition(sourceFile, skipTrivia(sourceFile->text, parent->pos()));
			int childStartLine =
				getECMALineOfPosition(sourceFile, skipTrivia(sourceFile->text, child->pos()));
			return parentStartLine != childStartLine;
		}
		if (parent->kind != Kind::BinaryExpression) {
			return true;
		}
		return indentByDefault;
	case Kind::DoStatement:
	case Kind::WhileStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::ForStatement:
	case Kind::IfStatement:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::MethodDeclaration:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		return childKind != Kind::Block;
	case Kind::ArrowFunction:
		if (sourceFile != nullptr && childKind == Kind::ParenthesizedExpression) {
			return rangeIsOnOneLine(child->loc, sourceFile);
		}
		return childKind != Kind::Block;
	case Kind::ExportDeclaration:
		return childKind != Kind::NamedExports;
	case Kind::ImportDeclaration:
		return childKind != Kind::ImportClause ||
			(child->as<ImportClause>()->NamedBindings != nullptr &&
			 child->as<ImportClause>()->NamedBindings->kind != Kind::NamedImports);
	case Kind::JsxElement:
		return childKind != Kind::JsxClosingElement;
	case Kind::JsxFragment:
		return childKind != Kind::JsxClosingFragment;
	case Kind::IntersectionType:
	case Kind::UnionType:
	case Kind::SatisfiesExpression:
		if (childKind == Kind::TypeLiteral || childKind == Kind::TupleType ||
			childKind == Kind::MappedType) {
			return false;
		}
		return indentByDefault;
	case Kind::TryStatement:
		if (childKind == Kind::Block) {
			return false;
		}
		return indentByDefault;
	default:
		break;
	}

	// No explicit rule for given nodes so the result will follow the default value argument
	return indentByDefault;
}

// A multiline conditional typically increases the indentation of its whenTrue and whenFalse children:
//
// condition
//
//	? whenTrue
//	: whenFalse;
//
// However, that indentation does not apply if the subexpressions themselves span multiple lines,
// applying their own indentation:
//
//	(() => {
//	  return complexCalculationForCondition();
//	})() ? {
//
//	  whenTrue: 'multiline object literal'
//	} : (
//
//	whenFalse('multiline parenthesized expression')
//
// );
//
// In these cases, we must discard the indentation increase that would otherwise be applied to the
// whenTrue and whenFalse children to avoid double-indenting their contents. To identify this scenario,
// we check for the whenTrue branch beginning on the line that the condition ends, and the whenFalse
// branch beginning on the line that the whenTrue branch ends.
bool childIsUnindentedBranchOfConditionalExpression(Node* parent, Node* child, int childStartLine,
													SourceFile* sourceFile) {
	if (parent->kind == Kind::ConditionalExpression &&
		(child == parent->as<ConditionalExpression>()->WhenTrue ||
		 child == parent->as<ConditionalExpression>()->WhenFalse)) {
		int conditionEndLine = getECMALineOfPosition(
			sourceFile, parent->as<ConditionalExpression>()->Condition->end());
		if (child == parent->as<ConditionalExpression>()->WhenTrue) {
			return childStartLine == conditionEndLine;
		} else {
			// On the whenFalse side, we have to look at the whenTrue side, because if that one was
			// indented, whenFalse must also be indented:
			//
			// const y = true
			//   ? 1 : (          L1: whenTrue indented because it's on a new line
			//     0              L2: indented two stops, one because whenTrue was indented
			//   );                   and one because of the parentheses spanning multiple lines
			int trueStartLine =
				getStartLineForNode(parent->as<ConditionalExpression>()->WhenTrue, sourceFile);
			int trueEndLine = getECMALineOfPosition(
				sourceFile, parent->as<ConditionalExpression>()->WhenTrue->end());
			return conditionEndLine == trueStartLine && trueEndLine == childStartLine;
		}
	}
	return false;
}

bool argumentStartsOnSameLineAsPreviousArgument(Node* parent, Node* child, int childStartLine,
												SourceFile* sourceFile) {
	if (isCallExpression(parent) || isNewExpression(parent)) {
		std::vector<Node*> args = parent->arguments();
		if (args.empty()) {
			return false;
		}
		auto it = std::find(args.begin(), args.end(), child);
		int currentIndex =
			it == args.end() ? -1 : static_cast<int>(it - args.begin());
		if (currentIndex == -1) {
			// If it's not one of the arguments, don't look past this
			return false;
		}
		if (currentIndex == 0) {
			return false; // Can't look at previous node if first
		}

		Node* previousNode = args[currentIndex - 1];
		int lineOfPreviousNode = getECMALineOfPosition(sourceFile, previousNode->end());
		if (childStartLine == lineOfPreviousNode) {
			return true;
		}
	}
	return false;
}

} // namespace tsc::format
