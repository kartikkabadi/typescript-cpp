// Port of tsc/internal/ls/jsdoc_snippet.go — JSDoc /** */ snippet
// completions. === slice: ls-coreA ===

#include "internal/ls/ls.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include "internal/astnav/tokens.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/format/format.h"
#include "internal/locale/locale.h"
#include "internal/parser/parser.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::ls {

namespace {

// --- jsdoc_snippet.go:19 docCommentTemplate ---

struct docCommentTemplate {
	std::string newText;
};

// --- jsdoc_snippet.go:23 commentOwnerInfo ---

struct commentOwnerInfo {
	Node* commentOwner = nullptr;
	std::vector<Node*> parameters;
	bool hasReturn = false;
};

// core.Find — first element satisfying pred, or nullptr.
template <class R, class F>
auto findRange(R&& v, F f) -> std::decay_t<std::ranges::range_value_t<R>> {
	using T = std::decay_t<std::ranges::range_value_t<R>>;
	auto it = std::find_if(v.begin(), v.end(), f);
	return it != v.end() ? static_cast<T>(*it) : T{};
}

// core.LastOrNil
template <class T>
T lastOrNil(const std::vector<T>& v) {
	return v.empty() ? T{} : v.back();
}

// strings.TrimLeft
inline std::string_view trimLeft(std::string_view s, std::string_view cutset) {
	size_t i = 0;
	while (i < s.size() && cutset.find(s[i]) != std::string_view::npos) i++;
	return s.substr(i);
}

// strings.Split
inline std::vector<std::string> splitString(std::string_view s,
                                            std::string_view sep) {
	std::vector<std::string> out;
	if (sep.empty()) {
		for (char c : s) out.emplace_back(1, c);
		return out;
	}
	size_t pos = 0;
	while (true) {
		size_t idx = s.find(sep, pos);
		if (idx == std::string_view::npos) {
			out.emplace_back(s.substr(pos));
			return out;
		}
		out.emplace_back(s.substr(pos, idx - pos));
		pos = idx + sep.size();
	}
}

// strings.Join
inline std::string joinStrings(const std::vector<std::string>& parts,
                               std::string_view sep) {
	std::string out;
	for (size_t i = 0; i < parts.size(); i++) {
		if (i != 0) {
			out += sep;
		}
		out += parts[i];
	}
	return out;
}

// --- jsdoc_snippet.go:74 isPotentiallyValidJSDocSnippetCompletionPosition ---
// (forward decl — mutual use order)

bool isJSDocSnippetPrefix(std::string_view prefix);
std::pair<int, bool> getJSDocSnippetPrefixStart(std::string_view prefix);
bool isJSDocSnippetSuffix(std::string_view suffix);
std::pair<int, bool> getJSDocSnippetSuffixEnd(std::string_view suffix);
std::string_view trimRightSingleLineWhitespace(std::string_view text);
int skipSingleLineWhitespace(std::string_view text, int pos);
bool startsWithSingleLineWhitespace(std::string_view text);
bool isOnlySpacesOrTabs(std::string_view text);
int skipWhitespace(std::string_view text, int position);
std::pair<commentOwnerInfo*, bool> getCommentOwnerInfoWorker(
    Node* commentOwner, bool generateReturnInDocTemplate);

// jsdoc_snippet.go:74

// --- jsdoc_snippet.go:124 getDocCommentTemplateAtPosition ---

// jsdoc_snippet.go:181 getDocCommentEndAtPosition — returns
// (end, ok, hasClosing).
std::tuple<int, bool, bool> getDocCommentEndAtPosition(SourceFile* file,
                                                     int position) {
	const std::string& text = file->Text();
	int lineStart = format::GetLineStartPositionForPosition(position, file);
	int lineEnd = getLineEndOfPosition(file, position);
	std::string_view prefix = std::string_view(text).substr(lineStart,
	                                                      position - lineStart);
	std::string_view suffix = std::string_view(text).substr(position,
	                                                      lineEnd - position);
	if (!trimRightSingleLineWhitespace(prefix).ends_with("/**")) {
		return {0, false, false};
	}
	auto [suffixEnd, hasClosing] = getJSDocSnippetSuffixEnd(suffix);
	return {position + suffixEnd, true, hasClosing};
}

// jsdoc_snippet.go:194 skipWhitespace.
int skipWhitespace(std::string_view text, int position) {
	while (position < static_cast<int>(text.size())) {
		int size = 0;
		char32_t ch = decodeJSStringRune(text, position, &size);
		if (size == 0) {
			break;
		}
		if (!isWhiteSpaceLike(ch)) {
			break;
		}
		position += size;
	}
	return position;
}

// jsdoc_snippet.go:208 getCommentOwnerInfo.
commentOwnerInfo* getCommentOwnerInfo(Node* tokenAtPos,
                                      bool generateReturnInDocTemplate) {
	for (Node* node = tokenAtPos; node != nullptr; node = node->parent) {
		auto [info, quit] =
		    getCommentOwnerInfoWorker(node, generateReturnInDocTemplate);
		if (info != nullptr || quit) {
			return info;
		}
	}
	return nullptr;
}

// --- hasReturn / getRightHandSideOfAssignment helpers ---

// ast/utilities.go:1157 ForEachReturnStatement (file-local port; also
// file-local in the checker slice).
bool forEachReturnStatement(Node* body,
                            const std::function<bool(Node*)>& visitor) {
	std::function<bool(Node*)> traverse;
	traverse = [&](Node* node) -> bool {
		switch (node->kind) {
		case Kind::ReturnStatement:
			return visitor(node);
		case Kind::CaseBlock:
		case Kind::Block:
		case Kind::IfStatement:
		case Kind::DoStatement:
		case Kind::WhileStatement:
		case Kind::ForStatement:
		case Kind::ForInStatement:
		case Kind::ForOfStatement:
		case Kind::WithStatement:
		case Kind::SwitchStatement:
		case Kind::CaseClause:
		case Kind::DefaultClause:
		case Kind::LabeledStatement:
		case Kind::TryStatement:
		case Kind::CatchClause:
			return node->forEachChild(traverse);
		}
		return false;
	};
	return traverse(body);
}

// jsdoc_snippet.go:288 hasReturn.
bool hasReturn(Node* node, bool generateReturnInDocTemplate) {
	if (!generateReturnInDocTemplate) {
		return false;
	}
	if (isFunctionTypeNode(node)) {
		return true;
	}
	if (isArrowFunction(node)) {
		if (Node* body = node->body();
		    body != nullptr && isExpression(body)) {
			return true;
		}
	}
	return isFunctionLikeDeclaration(node) && node->body() != nullptr &&
	       isBlock(node->body()) &&
	       forEachReturnStatement(node->body(), [](Node*) -> bool {
		       return true;
	       });
}

// jsdoc_snippet.go:299 getRightHandSideOfAssignment.
Node* getRightHandSideOfAssignment(Node* rightHandSide) {
	if (rightHandSide == nullptr) {
		return nullptr;
	}
	while (rightHandSide->kind == Kind::ParenthesizedExpression) {
		rightHandSide =
		    rightHandSide->as<ParenthesizedExpression>()->Expression;
	}
	switch (rightHandSide->kind) {
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
		return rightHandSide;
	case Kind::ClassExpression:
		return findRange(rightHandSide->members(), [](Node* m) {
			return isConstructorDeclaration(m);
		});
	default:
		return nullptr;
	}
}

// jsdoc_snippet.go:219 getCommentOwnerInfoWorker.
std::pair<commentOwnerInfo*, bool> getCommentOwnerInfoWorker(
    Node* commentOwner, bool generateReturnInDocTemplate) {
	if (commentOwner == nullptr) {
		return {nullptr, false};
	}
	switch (commentOwner->kind) {
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::MethodDeclaration:
	case Kind::Constructor:
	case Kind::MethodSignature:
	case Kind::ArrowFunction:
		return {new commentOwnerInfo{
		            .commentOwner = commentOwner,
		            .parameters = commentOwner->parameters(),
		            .hasReturn =
		                hasReturn(commentOwner, generateReturnInDocTemplate)},
		        false};
	case Kind::PropertyAssignment:
		return getCommentOwnerInfoWorker(
		    commentOwner->as<PropertyAssignment>()->Initializer,
		    generateReturnInDocTemplate);
	case Kind::ClassDeclaration:
	case Kind::InterfaceDeclaration:
	case Kind::EnumDeclaration:
	case Kind::EnumMember:
	case Kind::TypeAliasDeclaration:
		return {new commentOwnerInfo{.commentOwner = commentOwner}, false};
	case Kind::PropertySignature:
		if (Node* typeNode =
		        commentOwner->as<PropertySignatureDeclaration>()->Type;
		    typeNode != nullptr && isFunctionTypeNode(typeNode)) {
			return {new commentOwnerInfo{
			            .commentOwner = commentOwner,
			            .parameters = typeNode->parameters(),
			            .hasReturn =
			                hasReturn(typeNode, generateReturnInDocTemplate)},
			        false};
		}
		return {new commentOwnerInfo{.commentOwner = commentOwner}, false};
	case Kind::VariableStatement: {
		std::vector<Node*>& declarations =
		    commentOwner->as<VariableStatement>()
		        ->DeclarationList->as<VariableDeclarationList>()
		        ->Declarations->nodes;
		if (declarations.size() == 1) {
			if (Node* initializer = declarations[0]
			                            ->as<VariableDeclaration>()
			                            ->Initializer;
			    initializer != nullptr) {
				if (Node* host = getRightHandSideOfAssignment(initializer);
				    host != nullptr) {
					return {new commentOwnerInfo{
					            .commentOwner = commentOwner,
					            .parameters = host->parameters(),
					            .hasReturn = hasReturn(
					                host, generateReturnInDocTemplate)},
					        false};
				}
			}
		}
		return {new commentOwnerInfo{.commentOwner = commentOwner}, false};
	}
	case Kind::SourceFile:
		return {nullptr, true};
	case Kind::ModuleDeclaration:
		if (commentOwner->parent->kind == Kind::ModuleDeclaration) {
			return {nullptr, false};
		}
		return {new commentOwnerInfo{.commentOwner = commentOwner}, false};
	case Kind::ExpressionStatement:
		return getCommentOwnerInfoWorker(
		    commentOwner->as<ExpressionStatement>()->Expression,
		    generateReturnInDocTemplate);
	case Kind::BinaryExpression: {
		BinaryExpression* binaryExpression =
		    commentOwner->as<BinaryExpression>();
		if (getAssignmentDeclarationKind(commentOwner) ==
		    JSDeclarationKind::None) {
			return {nullptr, true};
		}
		if (isFunctionLike(binaryExpression->Right)) {
			return {new commentOwnerInfo{
			            .commentOwner = commentOwner,
			            .parameters = binaryExpression->Right->parameters(),
			            .hasReturn = hasReturn(binaryExpression->Right,
			                                   generateReturnInDocTemplate)},
			        false};
		}
		return {new commentOwnerInfo{.commentOwner = commentOwner}, false};
	}
	case Kind::PropertyDeclaration:
		if (Node* initializer =
		        commentOwner->as<PropertyDeclaration>()->Initializer;
		    initializer != nullptr &&
		    isFunctionExpressionOrArrowFunction(initializer)) {
			return {new commentOwnerInfo{
			            .commentOwner = commentOwner,
			            .parameters = initializer->parameters(),
			            .hasReturn = hasReturn(initializer,
			                                   generateReturnInDocTemplate)},
			        false};
		}
		break;
	default:
		break;
	}
	return {nullptr, false};
}

// jsdoc_snippet.go:350 isNonEmptyJSDoc.
bool isNonEmptyJSDoc(Node* jsdoc) {
	if (jsdoc == nullptr) {
		return false;
	}
	JSDoc* data = jsdoc->as<JSDoc>();
	return (data->Comment != nullptr && data->Comment->nodes.size() > 0) ||
	       (data->Tags != nullptr && data->Tags->nodes.size() > 0);
}

// jsdoc_snippet.go:358 hasJSDocTags.
bool hasJSDocTags(Node* node, SourceFile* file) {
	std::vector<Node*> jsdocs = node->jsDoc(file);
	if (jsdocs.empty()) {
		return false;
	}
	NodeList* tags = jsdocs.back()->as<JSDoc>()->Tags;
	return tags != nullptr && tags->nodes.size() > 0;
}

// jsdoc_snippet.go:305 parameterDocComments.
std::string parameterDocComments(const std::vector<Node*>& parameters,
                                 bool isJavaScriptFile,
                                 const std::string& indentation,
                                 const std::string& newLine) {
	std::string b;
	for (size_t i = 0; i < parameters.size(); i++) {
		Node* parameter = parameters[i];
		std::string paramName = gostd::sprintf("param%d", {(int)i});
		if (isIdentifier(parameter->name())) {
			paramName = parameter->name()->text();
		}
		std::string paramType;
		if (isJavaScriptFile) {
			if (parameter->as<ParameterDeclaration>()->DotDotDotToken !=
			    nullptr) {
				paramType = "{...any} ";
			} else {
				paramType = "{any} ";
			}
		}
		b += indentation;
		b += " * @param ";
		b += paramType;
		b += paramName;
		b += newLine;
	}
	return b;
}

// jsdoc_snippet.go:330 returnsDocComment.
std::string returnsDocComment(const std::string& indentation,
                              const std::string& newLine) {
	return indentation + " * @returns" + newLine;
}

// jsdoc_snippet.go:334 getIndentationStringAtPosition.
std::string getIndentationStringAtPosition(SourceFile* sourceFile,
                                           int position) {
	const std::string& text = sourceFile->Text();
	int lineStart =
	    format::GetLineStartPositionForPosition(position, sourceFile);
	int pos = lineStart;
	while (pos < position) {
		int size = 0;
		char32_t ch = decodeJSStringRune(text, pos, &size);
		if (size == 0) {
			break;
		}
		if (!isWhiteSpaceSingleLine(ch)) {
			break;
		}
		pos += size;
	}
	return text.substr(lineStart, pos - lineStart);
}

docCommentTemplate* getDocCommentTemplateAtPosition(
    SourceFile* sourceFile, int position, bool generateReturnInDocTemplate,
    const std::string& newLine) {
	Node* tokenAtPos = astnav::getTokenAtPosition(sourceFile, position);
	if (tokenAtPos == nullptr) {
		return nullptr;
	}

	Node* existingDocComment =
	    findAncestor(tokenAtPos, [](Node* n) { return isJSDoc(n); });
	auto [docCommentEnd, hasDocCommentAtPosition,
	      hasClosingDocCommentAtPosition] =
	    getDocCommentEndAtPosition(sourceFile, position);
	bool isInEmptyDocComment =
	    existingDocComment != nullptr || hasDocCommentAtPosition;
	if (isNonEmptyJSDoc(existingDocComment) && hasDocCommentAtPosition &&
	    !hasClosingDocCommentAtPosition) {
		std::string reparseText = sourceFile->Text().substr(0, position) +
		                          " */" +
		                          sourceFile->Text().substr(position);
		SourceFile* reparse =
		    parseSourceFile(sourceFile->ParseOptions(), reparseText,
		                            sourceFile->ScriptKind);
		return getDocCommentTemplateAtPosition(
		    reparse, position, generateReturnInDocTemplate, newLine);
	}
	if (isNonEmptyJSDoc(existingDocComment)) {
		return nullptr;
	}
	if (existingDocComment == nullptr && hasDocCommentAtPosition) {
		tokenAtPos = astnav::getTokenAtPosition(
		    sourceFile, skipWhitespace(sourceFile->Text(), docCommentEnd));
		if (tokenAtPos == nullptr) {
			return nullptr;
		}
	}
	int tokenStart =
	    astnav::getStartOfNode(tokenAtPos, sourceFile, false /*includeJSDoc*/);
	if (!isInEmptyDocComment && tokenStart < position) {
		return nullptr;
	}

	commentOwnerInfo* info =
	    getCommentOwnerInfo(tokenAtPos, generateReturnInDocTemplate);
	if (info == nullptr) {
		return nullptr;
	}

	Node* commentOwner = info->commentOwner;
	Node* lastJSDoc = lastOrNil(commentOwner->jsDoc(sourceFile));
	if (int commentOwnerStart = astnav::getStartOfNode(
	        commentOwner, sourceFile, false /*includeJSDoc*/);
	    commentOwnerStart < position ||
	    (lastJSDoc != nullptr && existingDocComment != nullptr &&
	     lastJSDoc != existingDocComment)) {
		return nullptr;
	}

	std::string indentation =
	    getIndentationStringAtPosition(sourceFile, position);
	std::string tags =
	    parameterDocComments(info->parameters, isSourceFileJS(sourceFile),
	                         indentation, newLine);
	if (info->hasReturn) {
		tags += returnsDocComment(indentation, newLine);
	}

	if (!tags.empty() && !hasJSDocTags(commentOwner, sourceFile)) {
		std::string preamble = "/**" + newLine + indentation + " * ";
		std::string endLine;
		if (tokenStart == position) {
			endLine = newLine + indentation;
		}
		return new docCommentTemplate{
		    .newText = preamble + newLine + tags + indentation + " */" +
		               endLine};
	}
	return new docCommentTemplate{.newText = "/** */"};
}

// --- jsdoc_snippet.go:367 templateToSnippet + snippet-transform helpers ---

// jsdoc_snippet.go:391 transformJSDocParamLine — forward decls.
std::pair<std::string, bool> transformJSDocParamLine(const std::string& line,
                                                     int* snippetIndex);
std::tuple<std::string, std::string_view, bool>
scanNonWhitespace(std::string_view text);
std::pair<std::string, bool> transformJSDocReturnsLine(
    const std::string& line, int* snippetIndex);
bool lineHasOnlyJSDocAsterisk(std::string_view line);

// jsdoc_snippet.go:378 stripJSDocTemplateIndentation.
std::string stripJSDocTemplateIndentation(const std::string& templateText,
                                          const std::string& newLine) {
	std::vector<std::string> lines = splitString(templateText, newLine);
	for (size_t i = 0; i < lines.size(); i++) {
		std::string_view trimmed = trimLeft(lines[i], " \t");
		if (trimmed.starts_with("/")) {
			lines[i] = trimmed;
		} else if (trimmed.starts_with("*")) {
			lines[i] = " " + std::string(trimmed);
		}
	}
	return joinStrings(lines, newLine);
}

// jsdoc_snippet.go:391 transformJSDocTemplateLines.
std::string transformJSDocTemplateLines(const std::string& templateText,
                                        const std::string& newLine,
                                        int* snippetIndex) {
	std::vector<std::string> lines = splitString(templateText, newLine);
	for (size_t i = 0; i < lines.size(); i++) {
		const std::string& line = lines[i];
		if (i > 0 && std::string_view(lines[i - 1]).starts_with("/**") &&
		    lineHasOnlyJSDocAsterisk(line)) {
			lines[i] = line + "$0";
			continue;
		}
		if (auto [transformed, ok] =
		        transformJSDocParamLine(line, snippetIndex);
		    ok) {
			lines[i] = transformed;
			continue;
		}
		if (auto [transformed, ok] =
		        transformJSDocReturnsLine(line, snippetIndex);
		    ok) {
			lines[i] = transformed;
		}
	}
	return joinStrings(lines, newLine);
}

// jsdoc_snippet.go:367 templateToSnippet.
std::string templateToSnippet(const std::string& templateText,
                              const std::string& newLine) {
	if (templateText == "/** */") {
		return "/**" + newLine + " * $0" + newLine + " */";
	}

	int snippetIndex = 1;
	std::string tpl = escapeSnippetText(templateText);
	tpl = stripJSDocTemplateIndentation(tpl, newLine);
	return transformJSDocTemplateLines(tpl, newLine, &snippetIndex);
}

// jsdoc_snippet.go:408 lineHasOnlyJSDocAsterisk.
bool lineHasOnlyJSDocAsterisk(std::string_view line) {
	line = trimLeft(line, " \t");
	return line.starts_with("*") && isOnlySpacesOrTabs(line.substr(1));
}

// jsdoc_snippet.go:413 transformJSDocParamLine.
std::pair<std::string, bool> transformJSDocParamLine(const std::string& line,
                                                     int* snippetIndex) {
	std::string prefix;
	std::string_view rest = line;
	if (rest.starts_with(" ")) {
		prefix = " ";
		rest = rest.substr(1);
	}
	if (!rest.starts_with("* @param")) {
		return {"", false};
	}
	rest = rest.substr(std::string_view("* @param").size());
	if (!startsWithSingleLineWhitespace(rest)) {
		return {"", false};
	}
	rest = trimLeft(rest, " \t");

	std::string typeText;
	if (rest.starts_with("{")) {
		size_t closeBrace = rest.find('}');
		if (closeBrace == std::string_view::npos) {
			return {"", false};
		}
		typeText =
		    " " + std::string(rest.substr(0, closeBrace + 1));
		rest = rest.substr(closeBrace + 1);
		if (!startsWithSingleLineWhitespace(rest)) {
			return {"", false};
		}
		rest = trimLeft(rest, " \t");
	}

	auto [paramName, restAfter, ok] = scanNonWhitespace(rest);
	if (!ok || !isOnlySpacesOrTabs(restAfter)) {
		return {"", false};
	}

	std::string out = prefix + "* @param ";
	if (typeText == " {any}" || typeText == " {*}") {
		out += gostd::sprintf("{${%d:*}} ", {*snippetIndex});
		(*snippetIndex)++;
	} else if (!typeText.empty()) {
		out += typeText + " ";
	}
	out += gostd::sprintf("%s ${%d}", {paramName, *snippetIndex});
	(*snippetIndex)++;
	return {out, true};
}

// jsdoc_snippet.go:456 scanNonWhitespace — forward-declared usage above.
std::tuple<std::string, std::string_view, bool>
scanNonWhitespace(std::string_view text) {
	if (text.empty()) {
		return {"", "", false};
	}
	for (size_t i = 0; i < text.size();) {
		int size = 0;
		char32_t ch = decodeJSStringRune(text, i, &size);
		if (size == 0 || isWhiteSpaceLike(ch)) {
			if (i == 0) {
				return {"", "", false};
			}
			return {std::string(text.substr(0, i)), text.substr(i), true};
		}
		i += size;
	}
	return {std::string(text), "", true};
}

// jsdoc_snippet.go:471 transformJSDocReturnsLine.
std::pair<std::string, bool> transformJSDocReturnsLine(
    const std::string& line, int* snippetIndex) {
	std::string prefix;
	std::string_view rest = line;
	if (rest.starts_with(" ")) {
		prefix = " ";
		rest = rest.substr(1);
	}
	if (!rest.starts_with("* @returns") ||
	    !isOnlySpacesOrTabs(
	        rest.substr(std::string_view("* @returns").size()))) {
		return {"", false};
	}
	std::string text = gostd::sprintf("%s* @returns ${%d}",
	                                  {prefix, *snippetIndex});
	(*snippetIndex)++;
	return {text, true};
}

// --- jsdoc_snippet.go:489 isJSDocSnippetPrefix et al. ---

// jsdoc_snippet.go:489
bool isJSDocSnippetPrefix(std::string_view prefix) {
	std::string_view trimmed = trimRightSingleLineWhitespace(prefix);
	if (trimmed.ends_with("/**")) {
		return true;
	}
	int start = skipSingleLineWhitespace(prefix, 0);
	if (start >= static_cast<int>(trimmed.size()) || trimmed[start] != '/') {
		return false;
	}
	if (start + 3 > static_cast<int>(trimmed.size())) {
		return false;
	}
	for (int i = start + 1; i < static_cast<int>(trimmed.size()); i++) {
		if (trimmed[i] != '*') {
			return false;
		}
	}
	return static_cast<int>(trimmed.size()) - start >= 3;
}

// jsdoc_snippet.go:509 getJSDocSnippetPrefixStart.
std::pair<int, bool> getJSDocSnippetPrefixStart(std::string_view prefix) {
	std::string_view trimmed = trimRightSingleLineWhitespace(prefix);
	for (int i = static_cast<int>(trimmed.size()) - 1;
	     i >= 0 && trimmed[i] == '*'; i--) {
		if (i > 0 && trimmed[i - 1] == '/') {
			return {i - 1, true};
		}
	}
	if (trimmed.ends_with("/")) {
		return {static_cast<int>(trimmed.size()) - 1, true};
	}
	return {0, false};
}

// jsdoc_snippet.go:522 isJSDocSnippetSuffix.
bool isJSDocSnippetSuffix(std::string_view suffix) {
	std::string_view trimmed = trimRightSingleLineWhitespace(
	    suffix.substr(skipSingleLineWhitespace(suffix, 0)));
	if (trimmed.empty()) {
		return true;
	}
	if (!trimmed.ends_with("/")) {
		return false;
	}
	for (size_t i = 0; i < trimmed.size() - 1; i++) {
		if (trimmed[i] != '*') {
			return false;
		}
	}
	return true;
}

// jsdoc_snippet.go:538 getJSDocSnippetSuffixEnd.
std::pair<int, bool> getJSDocSnippetSuffixEnd(std::string_view suffix) {
	int pos = skipSingleLineWhitespace(suffix, 0);
	while (pos < static_cast<int>(suffix.size()) && suffix[pos] == '*') {
		pos++;
	}
	if (pos < static_cast<int>(suffix.size()) && suffix[pos] == '/') {
		return {pos + 1, true};
	}
	return {0, false};
}

// jsdoc_snippet.go:549 trimRightSingleLineWhitespace.
std::string_view trimRightSingleLineWhitespace(std::string_view text) {
	int end = 0;
	for (size_t pos = 0; pos < text.size();) {
		int size = 0;
		char32_t ch = decodeJSStringRune(text, pos, &size);
		if (size == 0) {
			break;
		}
		pos += size;
		if (!isWhiteSpaceSingleLine(ch)) {
			end = static_cast<int>(pos);
		}
	}
	return text.substr(0, end);
}

// jsdoc_snippet.go:564 skipSingleLineWhitespace.
int skipSingleLineWhitespace(std::string_view text, int pos) {
	while (pos < static_cast<int>(text.size())) {
		int size = 0;
		char32_t ch = decodeJSStringRune(text, pos, &size);
		if (size == 0 || !isWhiteSpaceSingleLine(ch)) {
			break;
		}
		pos += size;
	}
	return pos;
}

// jsdoc_snippet.go:575 isOnlySingleLineWhitespace.
[[maybe_unused]] bool isOnlySingleLineWhitespace(std::string_view text) {
	return skipSingleLineWhitespace(text, 0) ==
	       static_cast<int>(text.size());
}

// jsdoc_snippet.go:579 startsWithSingleLineWhitespace.
bool startsWithSingleLineWhitespace(std::string_view text) {
	if (text.empty()) {
		return false;
	}
	int size = 0;
	char32_t ch = decodeJSStringRune(text, 0, &size);
	return size != 0 && isWhiteSpaceSingleLine(ch);
}

// jsdoc_snippet.go:587 isOnlySpacesOrTabs.
bool isOnlySpacesOrTabs(std::string_view text) {
	for (char c : text) {
		if (c != ' ' && c != '\t') {
			return false;
		}
	}
	return true;
}

} // namespace

// jsdoc_snippet.go:74 isPotentiallyValidJSDocSnippetCompletionPosition.
bool isPotentiallyValidJSDocSnippetCompletionPosition(SourceFile* file,
                                                      int position) {
	const std::string& text = file->Text();
	int lineStart = format::GetLineStartPositionForPosition(position, file);
	std::string_view prefix = std::string_view(text).substr(lineStart,
	                                                      position - lineStart);
	if (!isJSDocSnippetPrefix(prefix)) {
		return false;
	}

	int lineEnd = getLineEndOfPosition(file, position);
	std::string_view suffix = std::string_view(text).substr(position,
	                                                      lineEnd - position);
	return isJSDocSnippetSuffix(suffix);
}

// --- jsdoc_snippet.go:29 getJSDocSnippetCompletion ---

CompletionList* LanguageService::getJSDocSnippetCompletion(
    const ContextPtr& ctx, SourceFile* file, int position) {
	if (tristateIsFalse(UserPreferences().CompleteJSDocs)) {
		return nullptr;
	}
	if (!isPotentiallyValidJSDocSnippetCompletionPosition(file, position)) {
		return nullptr;
	}
	std::string newLine = FormatOptions().NewLineCharacter;
	if (newLine.empty()) {
		newLine = "\n";
	}
	docCommentTemplate* tmpl = getDocCommentTemplateAtPosition(
	    file, position,
	    tristateIsTrue(UserPreferences().GenerateReturnInDocTemplate),
	    newLine);
	if (tmpl == nullptr) {
		return nullptr;
	}

	std::string insertText = tmpl->newText;
	std::shared_ptr<lsproto::InsertTextFormat> insertTextFormat;
	if (clientSupportsItemSnippet(ctx)) {
		insertText = templateToSnippet(insertText, newLine);
		insertTextFormat =
		    std::make_shared<lsproto::InsertTextFormat>(
		        lsproto::InsertTextFormatSnippet);
	}

	std::shared_ptr<lsproto::TextEditOrInsertReplaceEdit> editRange =
	    getJSDocSnippetCompletionRange(ctx, file, position, insertText);
	std::shared_ptr<lsproto::Slice<std::string>> commitCharacters;
	if (clientSupportsItemCommitCharacters(ctx)) {
		commitCharacters = std::make_shared<lsproto::Slice<std::string>>(
		    std::vector<std::string>{});
	}
	CompletionItem* item = new CompletionItem{
	    .completionItem = new lsproto::CompletionItem{
	        .Label = "/** */",
	        .Kind = std::make_shared<lsproto::CompletionItemKind>(
	            lsproto::CompletionItemKindText),
	        .Detail = localize(locale::fromContext(ctx), JSDoc_comment,
	                           JSDoc_comment->key, {}),
	        .SortText = std::string("\x00", 1),
	        .InsertTextFormat = insertTextFormat,
	        .TextEdit = editRange,
	        .CommitCharacters = commitCharacters,
	    }};
	return new CompletionList{
	    .IsIncomplete = false,
	    .Items = {item},
	};
}

// --- jsdoc_snippet.go:87 getJSDocSnippetCompletionRange ---

std::shared_ptr<lsproto::TextEditOrInsertReplaceEdit>
LanguageService::getJSDocSnippetCompletionRange(const ContextPtr& ctx,
                                                SourceFile* file,
                                                int position,
                                                const std::string& newText) {
	const std::string& text = file->Text();
	int lineStart = format::GetLineStartPositionForPosition(position, file);
	std::string_view prefix = std::string_view(text).substr(lineStart,
	                                                      position - lineStart);
	int start = position;
	if (auto [prefixStart, ok] = getJSDocSnippetPrefixStart(prefix); ok) {
		start = lineStart + prefixStart;
	}

	int lineEnd = getLineEndOfPosition(file, position);
	std::string_view suffix = std::string_view(text).substr(position,
	                                                      lineEnd - position);
	int end = position;
	if (auto [suffixEnd, ok] = getJSDocSnippetSuffixEnd(suffix); ok) {
		end += suffixEnd;
	}

	auto [replacementRange, fidelity] =
	    createLspRangeFromBounds(start, end, file);
	if (!fidelity.IsExact()) {
		return nullptr;
	}
	if (clientSupportsItemInsertReplace(ctx)) {
		return std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(
		    lsproto::TextEditOrInsertReplaceEdit{
		        .InsertReplaceEdit =
		            std::make_shared<lsproto::InsertReplaceEdit>(
		                lsproto::InsertReplaceEdit{
		                    .NewText = newText,
		                    .Insert = replacementRange,
		                    .Replace = replacementRange,
		                })});
	}
	return std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(
	    lsproto::TextEditOrInsertReplaceEdit{
	        .TextEdit = std::make_shared<lsproto::TextEdit>(
	            lsproto::TextEdit{
	                .Range = replacementRange,
	                .NewText = newText,
	            })});
}

} // namespace tsc::ls
