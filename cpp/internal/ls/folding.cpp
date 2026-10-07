// === slice: ls-coreC ===
// folding.cpp — folding.go: folding ranges (node outlining + region comments).
#include "internal/astnav/tokens.h"
#include "internal/ls/ls.h"

namespace tsc::ls {

namespace {

// core.NewTextRange
TextRange newTextRange(int pos, int end) {
	return TextRange{static_cast<TextPos>(pos), static_cast<TextPos>(end)};
}

// folding.go:53 — foldingRangeKey
struct foldingRangeKey {
	uint32_t startLine = 0, startCharacter = 0, endLine = 0, endCharacter = 0;
	lsp::lsproto::FoldingRangeKind kind;
	std::string collapsedText;
	bool hasStartCharacter = false, hasEndCharacter = false;
	bool hasKind = false, hasCollapsedText = false;
	bool operator==(const foldingRangeKey&) const = default;
};

struct foldingRangeKeyHash {
	size_t operator()(const foldingRangeKey& k) const {
		size_t h = std::hash<uint32_t>{}(k.startLine) * 31 +
				   k.startCharacter;
		h = h * 131 + std::hash<uint32_t>{}(k.endLine) * 31 + k.endCharacter;
		h = h * 131 + std::hash<std::string>{}(k.kind) +
			std::hash<std::string>{}(k.collapsedText);
		h = h * 131 + (k.hasStartCharacter ? 1 : 0) +
			(k.hasEndCharacter ? 2 : 0) + (k.hasKind ? 4 : 0) +
			(k.hasCollapsedText ? 8 : 0);
		return h;
	}
};

// folding.go:61 — keyForFoldingRange
foldingRangeKey keyForFoldingRange(std::shared_ptr<lsp::lsproto::FoldingRange> foldingRange) {
	foldingRangeKey key;
	key.startLine = foldingRange->StartLine;
	key.endLine = foldingRange->EndLine;
	if (foldingRange->StartCharacter.has_value()) {
		key.startCharacter = *foldingRange->StartCharacter;
		key.hasStartCharacter = true;
	}
	if (foldingRange->EndCharacter.has_value()) {
		key.endCharacter = *foldingRange->EndCharacter;
		key.hasEndCharacter = true;
	}
	if (foldingRange->Kind != nullptr) {
		key.kind = *foldingRange->Kind;
		key.hasKind = true;
	}
	if (foldingRange->CollapsedText.has_value()) {
		key.collapsedText = *foldingRange->CollapsedText;
		key.hasCollapsedText = true;
	}
	return key;
}

// folding.go:367 — regionDelimiterResult
struct regionDelimiterResult {
	bool isStart = false;
	std::string name;
};

// strings helpers used by parseRegionDelimiter.
std::string_view trimLeftSpace(std::string_view s) {
	size_t i = 0;
	while (i < s.size() &&
		   (s[i] == ' ' || s[i] == '\t' || s[i] == '\v' || s[i] == '\f' ||
			s[i] == '\r' || s[i] == '\n' || static_cast<unsigned char>(s[i]) == 0x85)) {
		i++;
	}
	return s.substr(i);
}
std::string_view trimSpace(std::string_view s) {
	s = trimLeftSpace(s);
	while (!s.empty() &&
		   (s.back() == ' ' || s.back() == '\t' || s.back() == '\v' ||
			s.back() == '\f' || s.back() == '\r' || s.back() == '\n' ||
			static_cast<unsigned char>(s.back()) == 0x85)) {
		s.remove_suffix(1);
	}
	return s;
}
bool startsWith(std::string_view s, std::string_view prefix) {
	return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}
bool endsWith(std::string_view s, std::string_view suffix) {
	return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

// folding.go:372 — parseRegionDelimiter
regionDelimiterResult* parseRegionDelimiter(std::string lineText) {
	// We trim the leading whitespace and // without the regex since the
	// multiple potential whitespace matches can make for some gnarly backtracking behavior
	lineText = std::string{trimLeftSpace(lineText)};
	if (!startsWith(lineText, "//")) {
		return nullptr;
	}
	lineText = std::string{trimSpace(std::string_view(lineText).substr(2))};
	if (endsWith(lineText, "\r")) {
		lineText = lineText.substr(0, lineText.size() - 1);
	}
	if (!startsWith(lineText, "#")) {
		return nullptr;
	}
	lineText = lineText.substr(1);
	bool isStart = true;
	if (startsWith(lineText, "end")) {
		isStart = false;
		lineText = lineText.substr(3);
	}
	if (!startsWith(lineText, "region")) {
		return nullptr;
	}
	lineText = lineText.substr(6);
	return new regionDelimiterResult{
		isStart,
		std::string{trimSpace(lineText)},
	};
}

// folding.go:604 — createFoldingRange
// folding.go:600 — fwd decl
bool supportsCollapsedText(gostd::Context ctx);

std::shared_ptr<lsp::lsproto::FoldingRange> createFoldingRange(gostd::Context ctx,
											 lsp::lsproto::Range textRange,
											 lsp::lsproto::FoldingRangeKind foldingRangeKind,
											 std::string collapsedText) {
	std::shared_ptr<lsp::lsproto::FoldingRangeKind> kind;
	if (!foldingRangeKind.empty()) {
		kind = std::make_shared<lsp::lsproto::FoldingRangeKind>(foldingRangeKind);
	}
	auto result = std::make_shared<lsp::lsproto::FoldingRange>();
	result->StartLine = textRange.Start.Line;
	result->StartCharacter = textRange.Start.Character;
	result->EndLine = textRange.End.Line;
	result->EndCharacter = textRange.End.Character;
	result->Kind = kind;
	if (!collapsedText.empty() && supportsCollapsedText(ctx)) {
		result->CollapsedText = std::move(collapsedText);
	}
	return result;
}

// folding.go:600 — supportsCollapsedText
bool supportsCollapsedText(gostd::Context ctx) {
	return lsp::lsproto::getClientCapabilities(ctx)
		->TextDocument.FoldingRange.FoldingRange.CollapsedText;
}

// folding.go:622 — createFoldingRangeFromBounds (free fn)
std::shared_ptr<lsp::lsproto::FoldingRange> createFoldingRangeFromBounds(
	gostd::Context ctx, int pos, int end,
	lsp::lsproto::FoldingRangeKind foldingRangeKind, SourceFile* sourceFile,
	LanguageService* l) {
	auto [textRange, fidelity] = l->createFoldingRangeFromBounds(pos, end, sourceFile);
	if (fidelity.IsNone()) {
		return nullptr;
	}
	return createFoldingRange(ctx, textRange, foldingRangeKind, "");
}

// folding.go:586 — rangeBetweenTokens
std::shared_ptr<lsp::lsproto::FoldingRange> rangeBetweenTokens(gostd::Context ctx,
											 ::tsc::Node* openToken,
											 ::tsc::Node* closeToken,
											 SourceFile* sourceFile,
											 bool useFullStart,
											 LanguageService* l) {
	lsp::lsproto::Range textRange;
	spanmap::Fidelity fidelity;
	if (useFullStart) {
		auto [r, f] = l->createFoldingRangeFromBounds(int(openToken->pos()),
													int(closeToken->end()), sourceFile);
		textRange = r;
		fidelity = f;
	} else {
		auto [r, f] = l->createFoldingRangeFromBounds(
			astnav::getStartOfNode(openToken, sourceFile,
								   false /*includeJSDoc*/),
			int(closeToken->end()), sourceFile);
		textRange = r;
		fidelity = f;
	}
	if (fidelity.IsNone()) {
		return nullptr;
	}
	return createFoldingRange(ctx, textRange, "", "");
}

// folding.go:653 — isNodeArrayMultiLine
bool isNodeArrayMultiLine(std::span<::tsc::Node* const> list, SourceFile* sourceFile) {
	if (list.empty()) {
		return false;
	}
	return !printer::PositionsAreOnSameLine(list.front()->pos(),
										  list.back()->end(), sourceFile);
}

// folding.go:643 — tryGetFunctionOpenToken
::tsc::Node* tryGetFunctionOpenToken(::tsc::Node* node, ::tsc::Node* body,
								   SourceFile* sourceFile) {
	if (isNodeArrayMultiLine(node->parameters(), sourceFile)) {
		::tsc::Node* openParenToken =
			astnav::findChildOfKind(node, Kind::OpenParenToken, sourceFile);
		if (openParenToken != nullptr) {
			return openParenToken;
		}
	}
	return astnav::findChildOfKind(body, Kind::OpenBraceToken, sourceFile);
}

// folding.go:634 — functionSpan
std::shared_ptr<lsp::lsproto::FoldingRange> functionSpan(gostd::Context ctx, ::tsc::Node* node,
										 ::tsc::Node* body, SourceFile* sourceFile,
										 LanguageService* l) {
	::tsc::Node* openToken = tryGetFunctionOpenToken(node, body, sourceFile);
	::tsc::Node* closeToken =
		astnav::findChildOfKind(body, Kind::CloseBraceToken, sourceFile);
	if (openToken != nullptr && closeToken != nullptr) {
		return rangeBetweenTokens(ctx, openToken, closeToken, sourceFile,
								  true /*useFullStart*/, l);
	}
	return nullptr;
}

// folding.go:573 — spanForNode
std::shared_ptr<lsp::lsproto::FoldingRange> spanForNode(gostd::Context ctx, ::tsc::Node* node,
										Kind open, bool useFullStart,
										SourceFile* sourceFile, LanguageService* l) {
	Kind closeBrace = Kind::CloseBraceToken;
	if (open != Kind::OpenBraceToken) {
		closeBrace = Kind::CloseBracketToken;
	}
	::tsc::Node* openToken = astnav::findChildOfKind(node, open, sourceFile);
	::tsc::Node* closeToken = astnav::findChildOfKind(node, closeBrace, sourceFile);
	if (openToken != nullptr && closeToken != nullptr) {
		return rangeBetweenTokens(ctx, openToken, closeToken, sourceFile,
								  useFullStart, l);
	}
	return nullptr;
}

// folding.go:562 — spanForNodeArray
std::shared_ptr<lsp::lsproto::FoldingRange> spanForNodeArray(gostd::Context ctx,
											 NodeList* statements,
											 SourceFile* sourceFile,
											 LanguageService* l) {
	if (statements != nullptr && !statements->nodes.empty()) {
		auto [textRange, fidelity] = l->createFoldingRangeFromBounds(
			int(statements->pos()), int(statements->end()), sourceFile);
		if (fidelity.IsNone()) {
			return nullptr;
		}
		return createFoldingRange(ctx, textRange, "", "");
	}
	return nullptr;
}

// folding.go:549 — spanForJSXAttributes
std::shared_ptr<lsp::lsproto::FoldingRange> spanForJSXAttributes(gostd::Context ctx,
												 ::tsc::Node* node,
												 SourceFile* sourceFile,
												 LanguageService* l) {
	JsxAttributes* attributes = nullptr;
	if (node->kind == Kind::JsxSelfClosingElement) {
		attributes = node->as<JsxSelfClosingElement>()->Attributes->as<JsxAttributes>();
	} else {
		attributes = node->as<JsxOpeningElement>()->Attributes->as<JsxAttributes>();
	}
	if (attributes->properties().empty()) {
		return nullptr;
	}
	return createFoldingRangeFromBounds(
		ctx, astnav::getStartOfNode(node, sourceFile, false /*includeJSDoc*/),
		int(node->end()), "", sourceFile, l);
}

// folding.go:529 — spanForJSXElement
std::shared_ptr<lsp::lsproto::FoldingRange> spanForJSXElement(gostd::Context ctx,
											  ::tsc::Node* node,
											  SourceFile* sourceFile,
											  LanguageService* l) {
	if (node->kind == Kind::JsxElement) {
		JsxElement* jsxElement = node->as<JsxElement>();
		auto [textRange, fidelity] = l->createFoldingRangeFromBounds(
			astnav::getStartOfNode(jsxElement->OpeningElement, sourceFile,
								   false /*includeJSDoc*/),
			int(jsxElement->ClosingElement->end()), sourceFile);
		if (fidelity.IsNone()) {
			return nullptr;
		}
		std::string tagName =
			tsc::getTextOfNode(jsxElement->OpeningElement->tagName());
		std::string bannerText = "<" + tagName + ">...</" + tagName + ">";
		return createFoldingRange(ctx, textRange, "", bannerText);
	}
	// JsxFragment
	JsxFragment* jsxFragment = node->as<JsxFragment>();
	auto [textRange, fidelity] = l->createFoldingRangeFromBounds(
		astnav::getStartOfNode(jsxFragment->OpeningFragment, sourceFile,
							   false /*includeJSDoc*/),
		int(jsxFragment->ClosingFragment->end()), sourceFile);
	if (fidelity.IsNone()) {
		return nullptr;
	}
	return createFoldingRange(ctx, textRange, "", "<>...</>");
}

// folding.go:522 — spanForTemplateLiteral
std::shared_ptr<lsp::lsproto::FoldingRange> spanForTemplateLiteral(gostd::Context ctx,
												 ::tsc::Node* node,
												 SourceFile* sourceFile,
												 LanguageService* l) {
	if (node->kind == Kind::NoSubstitutionTemplateLiteral &&
		node->text().empty()) {
		return nullptr;
	}
	return createFoldingRangeFromBounds(
		ctx, astnav::getStartOfNode(node, sourceFile, false /*includeJSDoc*/),
		int(node->end()), "", sourceFile, l);
}

// folding.go:510 — spanForArrowFunction
std::shared_ptr<lsp::lsproto::FoldingRange> spanForArrowFunction(gostd::Context ctx,
												 ::tsc::Node* node,
												 SourceFile* sourceFile,
												 LanguageService* l) {
	ArrowFunction* arrowFunctionNode = node->as<ArrowFunction>();
	if (isBlock(arrowFunctionNode->Body) ||
		isParenthesizedExpression(arrowFunctionNode->Body) ||
		printer::PositionsAreOnSameLine(arrowFunctionNode->Body->pos(),
										arrowFunctionNode->Body->end(), sourceFile)) {
		return nullptr;
	}
	auto [textRange, fidelity] = l->createFoldingRangeFromBounds(
		int(arrowFunctionNode->Body->pos()), int(arrowFunctionNode->Body->end()),
		sourceFile);
	if (fidelity.IsNone()) {
		return nullptr;
	}
	return createFoldingRange(ctx, textRange, "", "");
}

// folding.go:497 — spanForCallExpression
std::shared_ptr<lsp::lsproto::FoldingRange> spanForCallExpression(gostd::Context ctx,
												  ::tsc::Node* node,
												  SourceFile* sourceFile,
												  LanguageService* l) {
	if (node->as<CallExpression>()->Arguments == nullptr ||
		node->as<CallExpression>()->Arguments->nodes.empty()) {
		return nullptr;
	}
	::tsc::Node* openToken =
		astnav::findChildOfKind(node, Kind::OpenParenToken, sourceFile);
	::tsc::Node* closeToken =
		astnav::findChildOfKind(node, Kind::CloseParenToken, sourceFile);
	if (openToken == nullptr || closeToken == nullptr ||
		printer::PositionsAreOnSameLine(openToken->pos(), closeToken->pos(),
										sourceFile)) {
		return nullptr;
	}

	return rangeBetweenTokens(ctx, openToken, closeToken, sourceFile,
							  true /*useFullStart*/, l);
}

// folding.go:485 — spanForParenthesizedExpression
std::shared_ptr<lsp::lsproto::FoldingRange> spanForParenthesizedExpression(gostd::Context ctx,
														   ::tsc::Node* node,
														   SourceFile* sourceFile,
														   LanguageService* l) {
	int start = astnav::getStartOfNode(node, sourceFile, false /*includeJSDoc*/);
	if (printer::PositionsAreOnSameLine(start, node->end(), sourceFile)) {
		return nullptr;
	}
	auto [textRange, fidelity] =
		l->createFoldingRangeFromBounds(start, int(node->end()), sourceFile);
	if (fidelity.IsNone()) {
		return nullptr;
	}
	return createFoldingRange(ctx, textRange, "", "");
}

// folding.go:464 — spanForImportExportElements
std::shared_ptr<lsp::lsproto::FoldingRange> spanForImportExportElements(gostd::Context ctx,
													  ::tsc::Node* node,
													  SourceFile* sourceFile,
													  LanguageService* l) {
	NodeList* elements = nullptr;
	switch (node->kind) {
	case Kind::NamedImports:
		elements = node->as<NamedImports>()->Elements;
		break;
	case Kind::NamedExports:
		elements = node->as<NamedExports>()->Elements;
		break;
	case Kind::ImportAttributes:
		elements = node->as<ImportAttributes>()->Attributes;
		break;
	default:
		break;
	}
	if (elements == nullptr || elements->nodes.empty()) {
		return nullptr;
	}
	::tsc::Node* openToken =
		astnav::findChildOfKind(node, Kind::OpenBraceToken, sourceFile);
	::tsc::Node* closeToken =
		astnav::findChildOfKind(node, Kind::CloseBraceToken, sourceFile);
	if (openToken == nullptr || closeToken == nullptr ||
		printer::PositionsAreOnSameLine(openToken->pos(), closeToken->pos(),
										sourceFile)) {
		return nullptr;
	}
	return rangeBetweenTokens(ctx, openToken, closeToken, sourceFile,
							  false /*useFullStart*/, l);
}

// folding.go:400 — getOutliningSpanForNode
std::shared_ptr<lsp::lsproto::FoldingRange> getOutliningSpanForNode(gostd::Context ctx,
												  ::tsc::Node* n,
												  SourceFile* sourceFile,
												  LanguageService* l) {
	switch (n->kind) {
	case Kind::Block:
		if (isFunctionLike(n->parent)) {
			return functionSpan(ctx, n->parent, n, sourceFile, l);
		}
		// Check if the block is standalone, or 'attached' to some parent statement.
		// If the latter, we want to collapse the block, but consider its hint span
		// to be the entire span of the parent.
		switch (n->parent->kind) {
		case Kind::DoStatement:
		case Kind::ForInStatement:
		case Kind::ForOfStatement:
		case Kind::ForStatement:
		case Kind::IfStatement:
		case Kind::WhileStatement:
		case Kind::WithStatement:
		case Kind::CatchClause:
			return spanForNode(ctx, n, Kind::OpenBraceToken,
							   true /*useFullStart*/, sourceFile, l);
		case Kind::TryStatement: {
			// Could be the try-block, or the finally-block.
			TryStatement* tryStatement = n->parent->as<TryStatement>();
			if (tryStatement->TryBlock == n) {
				return spanForNode(ctx, n, Kind::OpenBraceToken,
								   true /*useFullStart*/, sourceFile, l);
			} else if (tryStatement->FinallyBlock == n) {
				if (auto span = spanForNode(
						ctx, n, Kind::OpenBraceToken, true /*useFullStart*/,
						sourceFile, l);
					span != nullptr) {
					return span;
				}
			}
			[[fallthrough]];
		}
		default: {
			// Block was a standalone block.  In this case we want to only collapse
			// the span of the block, independent of any parent span.
			auto [textRange, fidelity] = l->createLspRangeFromNodeForFeature(
				n, sourceFile, spanmap::FeatureFoldingRanges);
			if (fidelity.IsNone()) {
				return nullptr;
			}
			return createFoldingRange(ctx, textRange, "", "");
		}
		}
	case Kind::ModuleBlock:
		return spanForNode(ctx, n, Kind::OpenBraceToken, true /*useFullStart*/,
						   sourceFile, l);
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::InterfaceDeclaration:
	case Kind::EnumDeclaration:
	case Kind::CaseBlock:
	case Kind::TypeLiteral:
	case Kind::ObjectBindingPattern:
		return spanForNode(ctx, n, Kind::OpenBraceToken, true /*useFullStart*/,
						   sourceFile, l);
	case Kind::TupleType:
		return spanForNode(ctx, n, Kind::OpenBracketToken,
						   !isTupleTypeNode(n->parent) /*useFullStart*/,
						   sourceFile, l);
	case Kind::CaseClause:
	case Kind::DefaultClause:
		return spanForNodeArray(ctx, n->as<CaseOrDefaultClause>()->Statements,
								sourceFile, l);
	case Kind::ObjectLiteralExpression:
		return spanForNode(
			ctx, n, Kind::OpenBraceToken,
			!isArrayLiteralExpression(n->parent) &&
				!isCallExpression(n->parent) /*useFullStart*/,
			sourceFile, l);
	case Kind::ArrayLiteralExpression:
		return spanForNode(
			ctx, n, Kind::OpenBracketToken,
			!isArrayLiteralExpression(n->parent) &&
				!isCallExpression(n->parent) /*useFullStart*/,
			sourceFile, l);
	case Kind::JsxElement:
	case Kind::JsxFragment:
		return spanForJSXElement(ctx, n, sourceFile, l);
	case Kind::JsxSelfClosingElement:
	case Kind::JsxOpeningElement:
		return spanForJSXAttributes(ctx, n, sourceFile, l);
	case Kind::TemplateExpression:
	case Kind::NoSubstitutionTemplateLiteral:
		return spanForTemplateLiteral(ctx, n, sourceFile, l);
	case Kind::ArrayBindingPattern:
		return spanForNode(ctx, n, Kind::OpenBracketToken,
						   !isBindingElement(n->parent) /*useFullStart*/,
						   sourceFile, l);
	case Kind::ArrowFunction:
		return spanForArrowFunction(ctx, n, sourceFile, l);
	case Kind::CallExpression:
		return spanForCallExpression(ctx, n, sourceFile, l);
	case Kind::ParenthesizedExpression:
		return spanForParenthesizedExpression(ctx, n, sourceFile, l);
	case Kind::NamedImports:
	case Kind::NamedExports:
	case Kind::ImportAttributes:
		return spanForImportExportElements(ctx, n, sourceFile, l);
	default:
		break;
	}
	return nullptr;
}

// folding.go:302 — addOutliningForLeadingCommentsForPos
std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> addOutliningForLeadingCommentsForPos(
	gostd::Context ctx, int pos, SourceFile* sourceFile, LanguageService* l) {
	std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> foldingRange;
	foldingRange.reserve(40);
	int firstSingleLineCommentStart = -1;
	int lastSingleLineCommentEnd = -1;
	int singleLineCommentCount = 0;
	lsp::lsproto::FoldingRangeKind foldingRangeKindComment =
		lsp::lsproto::FoldingRangeKindComment;

	auto combineAndAddMultipleSingleLineComments =
		[&]() -> std::shared_ptr<lsp::lsproto::FoldingRange> {
		// Only outline spans of two or more consecutive single line comments
		if (singleLineCommentCount > 1) {
			return createFoldingRangeFromBounds(ctx, firstSingleLineCommentStart,
												lastSingleLineCommentEnd,
												foldingRangeKindComment,
												sourceFile, l);
		}
		return nullptr;
	};

	const std::string& sourceText = sourceFile->Text();
	tsc::getLeadingCommentRanges(
		sourceText, pos, [&](const CommentRange& comment) {
			int commentPos = int(comment.pos());
			int commentEnd = int(comment.end());

			if (gostd::ctxErr(ctx) != nullptr) {
				return false;
			}
			switch (comment.kind) {
			case Kind::SingleLineCommentTrivia: {
				// never fold region delimiters into single-line comment regions
				std::string commentText =
					sourceText.substr(commentPos, commentEnd - commentPos);
				if (parseRegionDelimiter(commentText) != nullptr) {
					if (auto comments =
							combineAndAddMultipleSingleLineComments();
						comments != nullptr) {
						foldingRange.push_back(comments);
					}
					singleLineCommentCount = 0;
					break;
				}

				// For single line comments, combine consecutive ones (2 or more) into
				// a single span from the start of the first till the end of the last
				if (singleLineCommentCount == 0) {
					firstSingleLineCommentStart = commentPos;
				}
				lastSingleLineCommentEnd = commentEnd;
				singleLineCommentCount++;
				break;
			}
			case Kind::MultiLineCommentTrivia: {
				if (auto comments =
						combineAndAddMultipleSingleLineComments();
					comments != nullptr) {
					foldingRange.push_back(comments);
				}
				auto commentRange =
					createFoldingRangeFromBounds(ctx, commentPos, commentEnd,
												 foldingRangeKindComment,
												 sourceFile, l);
				if (commentRange != nullptr) {
					foldingRange.push_back(commentRange);
				}
				singleLineCommentCount = 0;
				break;
			}
			default:
				TSC_UNREACHABLE("unexpected comment kind");
			}
			return true;
		});
	if (auto addedComments =
			combineAndAddMultipleSingleLineComments();
		addedComments != nullptr) {
		foldingRange.push_back(addedComments);
	}
	return foldingRange;
}

// folding.go:295 — addOutliningForLeadingCommentsForNode
std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> addOutliningForLeadingCommentsForNode(
	gostd::Context ctx, ::tsc::Node* n, SourceFile* sourceFile,
	LanguageService* l) {
	if (isJsxText(n)) {
		return {};
	}
	return addOutliningForLeadingCommentsForPos(ctx, int(n->pos()), sourceFile, l);
}

// folding.go:203 — visitNode
std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> visitNode(gostd::Context ctx,
												 ::tsc::Node* n,
												 int depthRemaining,
												 SourceFile* sourceFile,
												 LanguageService* l) {
	if ((n->flags & NodeFlagsReparsed) != 0 || depthRemaining == 0 ||
		gostd::ctxErr(ctx) != nullptr) {
		return {};
	}
	std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> foldingRange;
	foldingRange.reserve(40);
	if ((!isBinaryExpression(n) && isDeclaration(n)) ||
		isVariableStatement(n) || isReturnStatement(n) ||
		isCallOrNewExpression(n) || n->kind == Kind::EndOfFile) {
		auto part =
			addOutliningForLeadingCommentsForNode(ctx, n, sourceFile, l);
		foldingRange.insert(foldingRange.end(), part.begin(), part.end());
	}
	if (isFunctionLike(n) && n->parent != nullptr &&
		isBinaryExpression(n->parent) &&
		n->parent->as<BinaryExpression>()->Left != nullptr &&
		isPropertyAccessExpression(
			n->parent->as<BinaryExpression>()->Left)) {
		auto part = addOutliningForLeadingCommentsForNode(
			ctx, n->parent->as<BinaryExpression>()->Left, sourceFile, l);
		foldingRange.insert(foldingRange.end(), part.begin(), part.end());
	}
	if (isBlock(n)) {
		NodeList* statements = n->as<Block>()->Statements;
		if (statements != nullptr) {
			auto part = addOutliningForLeadingCommentsForPos(
				ctx, int(statements->end()), sourceFile, l);
			foldingRange.insert(foldingRange.end(), part.begin(), part.end());
		}
	}
	if (isModuleBlock(n)) {
		NodeList* statements = n->as<ModuleBlock>()->Statements;
		if (statements != nullptr) {
			auto part = addOutliningForLeadingCommentsForPos(
				ctx, int(statements->end()), sourceFile, l);
			foldingRange.insert(foldingRange.end(), part.begin(), part.end());
		}
	}
	if (isClassLike(n) || isInterfaceDeclaration(n)) {
		NodeList* members = nullptr;
		if (isClassDeclaration(n)) {
			members = n->as<ClassDeclaration>()->Members;
		} else if (isClassExpression(n)) {
			members = n->as<ClassExpression>()->Members;
		} else {
			members = n->as<InterfaceDeclaration>()->Members;
		}
		if (members != nullptr) {
			auto part = addOutliningForLeadingCommentsForPos(
				ctx, int(members->end()), sourceFile, l);
			foldingRange.insert(foldingRange.end(), part.begin(), part.end());
		}
	}

	std::shared_ptr<lsp::lsproto::FoldingRange> span = getOutliningSpanForNode(ctx, n, sourceFile, l);
	if (span != nullptr) {
		foldingRange.push_back(span);
	}

	depthRemaining--;
	if (isCallExpression(n)) {
		depthRemaining++;
		auto expressionNodes =
			visitNode(ctx, n->expression(), depthRemaining, sourceFile, l);
		if (!expressionNodes.empty()) {
			foldingRange.insert(foldingRange.end(), expressionNodes.begin(),
								expressionNodes.end());
		}
		depthRemaining--;
		for (auto* arg : n->arguments()) {
			if (arg != nullptr) {
				auto part =
					visitNode(ctx, arg, depthRemaining, sourceFile, l);
				foldingRange.insert(foldingRange.end(), part.begin(), part.end());
			}
		}
		auto typeArguments = n->typeArguments();
		for (auto* typeArg : typeArguments) {
			if (typeArg != nullptr) {
				auto part =
					visitNode(ctx, typeArg, depthRemaining, sourceFile, l);
				foldingRange.insert(foldingRange.end(), part.begin(), part.end());
			}
		}
	} else if (isIfStatement(n) &&
			   n->as<IfStatement>()->ElseStatement != nullptr &&
			   isIfStatement(n->as<IfStatement>()->ElseStatement)) {
		// Consider an 'else if' to be on the same depth as the 'if'.
		IfStatement* ifStatement = n->as<IfStatement>();
		auto expressionNodes =
			visitNode(ctx, n->expression(), depthRemaining, sourceFile, l);
		if (!expressionNodes.empty()) {
			foldingRange.insert(foldingRange.end(), expressionNodes.begin(),
								expressionNodes.end());
		}
		auto thenNode =
			visitNode(ctx, ifStatement->ThenStatement, depthRemaining, sourceFile, l);
		if (!thenNode.empty()) {
			foldingRange.insert(foldingRange.end(), thenNode.begin(), thenNode.end());
		}
		depthRemaining++;
		auto elseNode =
			visitNode(ctx, ifStatement->ElseStatement, depthRemaining, sourceFile, l);
		if (!elseNode.empty()) {
			foldingRange.insert(foldingRange.end(), elseNode.begin(), elseNode.end());
		}
		depthRemaining--;
	} else {
		n->forEachChild([&](::tsc::Node* node) {
			auto childNode = visitNode(ctx, node, depthRemaining, sourceFile, l);
			if (!childNode.empty()) {
				foldingRange.insert(foldingRange.end(), childNode.begin(),
									childNode.end());
			}
			return false;
		});
	}
	depthRemaining++;
	return foldingRange;
}

} // namespace

// ============================================================================
// folding.go — ProvideFoldingRange
// ============================================================================
// folding.go:22
lsp::lsproto::FoldingRangeResponse LanguageService::ProvideFoldingRange(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI) {
	SourceFile* sourceFile = getProgramAndFile(documentURI).second;
	std::vector<SourceFile*> projections;
	projections.push_back(sourceFile);
	if (const std::vector<SourceFile*>* supplemental =
			sourceFile->SupplementalSourceFiles()) {
		projections.insert(projections.end(), supplemental->begin(),
						   supplemental->end());
	}
	std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> res;
	for (auto* projection : projections) {
		auto ranges = addNodeOutliningSpans(ctx, projection);
		auto regionRanges = addRegionOutliningSpans(ctx, projection);
		ranges.insert(ranges.end(), regionRanges.begin(), regionRanges.end());
		if (lsp::lsproto::getClientCapabilities(ctx)
				->TextDocument.FoldingRange.LineFoldingOnly) {
			ranges = adjustFoldingEnd(ranges, projection);
		}
		res.insert(res.end(), ranges.begin(), ranges.end());
	}
	std::stable_sort(res.begin(), res.end(),
					 [](std::shared_ptr<lsp::lsproto::FoldingRange> a, std::shared_ptr<lsp::lsproto::FoldingRange> b) {
						 if (a->StartLine != b->StartLine) {
							 return a->StartLine < b->StartLine;
						 }
						 if (*a->StartCharacter != *b->StartCharacter) {
							 return *a->StartCharacter < *b->StartCharacter;
						 }
						 if (a->EndLine != b->EndLine) {
							 return a->EndLine < b->EndLine;
						 }
						 return *a->EndCharacter < *b->EndCharacter;
					 });
	std::unordered_set<foldingRangeKey, foldingRangeKeyHash> seen;
	res.erase(std::remove_if(res.begin(), res.end(),
							 [&](std::shared_ptr<lsp::lsproto::FoldingRange> foldingRange) {
								 return !seen.insert(keyForFoldingRange(
											 foldingRange))
											 .second;
							 }),
			  res.end());
	lsp::lsproto::FoldingRangesOrNull out;
	out.FoldingRanges =
		std::make_shared<lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::FoldingRange>>>(std::move(res));
	return out;
}

// adjustFoldingEnd adjusts the end line of folding ranges when the client signals lineFoldingOnly.
// This mirrors the behavior of VS Code's built-in TypeScript extension (workaround for vscode#47240).
// When lineFoldingOnly is true, we hide lines from startLine+1 to endLine. And to keep closing
// brackets/braces visible, we subtract 1 from endLine when the range ends with a closing pair character.
// folding.go:86
std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> LanguageService::adjustFoldingEnd(
	std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> ranges, SourceFile* sourceFile) {
	const std::string& sourceText = sourceFile->Text();
	std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> result;
	result.reserve(ranges.size());
	for (auto& r : ranges) {
		if (r->EndCharacter.has_value() && *r->EndCharacter > 0) {
			lsp::lsproto::Position p;
			p.Line = r->EndLine;
			p.Character = *r->EndCharacter;
			auto positions = converters->FromLSPPositionForSourceFile(
				sourceFile, p, spanmap::FeatureFoldingRanges);
			const lsconv::MappedPosition<SourceFile*>* position = nullptr;
			for (auto& mp : positions) {
				if (mp.Script == sourceFile && !mp.Fidelity.IsNone()) {
					position = &mp;
					break;
				}
			}
			if (position != nullptr && position->Position > 0 &&
				int(position->Position) <= int(sourceText.size())) {
				TextPos endOffset = position->Position;
				char foldEndChar = sourceText[int(endOffset) - 1];
				if (foldEndChar == '}' || foldEndChar == ']' || foldEndChar == ')' ||
					foldEndChar == '`' || foldEndChar == '>') {
					if (r->EndLine > r->StartLine) {
						r->EndLine--;
					}
				}
			}
		}
		result.push_back(r);
	}
	return result;
}

// folding.go:117 — addNodeOutliningSpans
std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> LanguageService::addNodeOutliningSpans(
	gostd::Context ctx, SourceFile* sourceFile) {
	int depthRemaining = 40;
	size_t current = 0;

	NodeList* statements = sourceFile->Statements;
	size_t n = statements->nodes.size();
	std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> foldingRange;
	foldingRange.reserve(40);
	while (current < n) {
		while (current < n &&
			   !isAnyImportSyntax(statements->nodes[current])) {
			auto part = visitNode(ctx, statements->nodes[current],
								  depthRemaining, sourceFile, this);
			foldingRange.insert(foldingRange.end(), part.begin(), part.end());
			current++;
		}
		if (current == n) {
			break;
		}
		size_t firstImport = current;
		while (current < n &&
			   isAnyImportSyntax(statements->nodes[current])) {
			auto part = visitNode(ctx, statements->nodes[current],
								  depthRemaining, sourceFile, this);
			foldingRange.insert(foldingRange.end(), part.begin(), part.end());
			current++;
		}
		size_t lastImport = current - 1;
		if (lastImport != firstImport) {
			lsp::lsproto::FoldingRangeKind foldingRangeKind =
				lsp::lsproto::FoldingRangeKindImports;
			std::shared_ptr<lsp::lsproto::FoldingRange> imports =
				::tsc::ls::createFoldingRangeFromBounds(
				ctx,
				astnav::getStartOfNode(
					astnav::findChildOfKind(statements->nodes[firstImport],
											Kind::ImportKeyword, sourceFile),
					sourceFile, false /*includeJSDoc*/),
				int(statements->nodes[lastImport]->end()),
				foldingRangeKind,
				sourceFile,
				this);
			if (imports != nullptr) {
				foldingRange.push_back(imports);
			}
		}
	}

	// Visit the EOF Token so that comments which aren't attached to statements are included.
	auto eofPart = visitNode(ctx, sourceFile->EndOfFileToken, depthRemaining,
							 sourceFile, this);
	foldingRange.insert(foldingRange.end(), eofPart.begin(), eofPart.end());
	return foldingRange;
}

// folding.go:160 — addRegionOutliningSpans
std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> LanguageService::addRegionOutliningSpans(
	gostd::Context ctx, SourceFile* sourceFile) {
	struct regionStart {
		int position = 0;
		std::string* collapsedText = nullptr;
	};
	std::vector<regionStart> regions;
	regions.reserve(40);
	std::vector<std::shared_ptr<lsp::lsproto::FoldingRange>> out;
	out.reserve(40);
	auto& lineStarts = tsc::getECMALineStarts(sourceFile);
	for (auto currentLineStart : lineStarts) {
		int lineEnd = getLineEndOfPosition(sourceFile, int(currentLineStart));
		std::string lineText = sourceFile->Text().substr(
			currentLineStart, lineEnd - int(currentLineStart));
		regionDelimiterResult* result = parseRegionDelimiter(lineText);
		if (result == nullptr ||
			isInComment(sourceFile, int(currentLineStart),
						astnav::getTokenAtPosition(sourceFile,
												   int(currentLineStart))) != nullptr) {
			continue;
		}

		if (result->isStart) {
			regionStart region;
			region.position =
				int(sourceFile->Text().find("//", currentLineStart)) -
				int(currentLineStart) + int(currentLineStart);
			if (supportsCollapsedText(ctx)) {
				std::string collapsedText = "#region";
				if (!result->name.empty()) {
					collapsedText = result->name;
				}
				region.collapsedText = new std::string(std::move(collapsedText));
			}
			regions.push_back(region);
		} else {
			if (!regions.empty()) {
				regionStart region = regions.back();
				regions.pop_back();
				auto [textRange, fidelity] =
					createFoldingRangeFromBounds(region.position, lineEnd,
												 sourceFile);
				if (fidelity.IsNone()) {
					continue;
				}
				std::shared_ptr<lsp::lsproto::FoldingRange> foldingRange = createFoldingRange(
					ctx, textRange, lsp::lsproto::FoldingRangeKindRegion, "");
				if (region.collapsedText != nullptr) { foldingRange->CollapsedText = *region.collapsedText; }
				out.push_back(foldingRange);
			}
		}
	}
	return out;
}

// folding.go:630 — createFoldingRangeFromBounds (method)
std::pair<lsp::lsproto::Range, spanmap::Fidelity>
LanguageService::createFoldingRangeFromBounds(int start, int end,
											  SourceFile* sourceFile) {
	return converters->ToLSPRangeForFeature(sourceFile, newTextRange(start, end),
										  spanmap::FeatureFoldingRanges);
}

} // namespace tsc::ls
