// autoinsert.cpp — port of tsc/internal/ls/autoinsert.go.

#include "internal/ls/ls.h"

#include "internal/astnav/tokens.h"
#include "internal/scanner/scanner.h"
#include "internal/spanmap/spanmap.h"

namespace tsc::ls {

namespace {

// autoinsert.go:78 — isUnclosedTag
bool isUnclosedTag(::tsc::JsxElement* node) {
	::tsc::Node* openingElement = node->OpeningElement;
	::tsc::Node* closingElement = node->ClosingElement;
	if (!tagNamesAreEquivalent(openingElement->tagName(),
	                           closingElement->tagName())) {
		return true;
	}

	::tsc::Node* parent = node->parent;
	if (isJsxElement(parent)) {
		::tsc::JsxElement* parentElement = parent->as<::tsc::JsxElement>();
		return tagNamesAreEquivalent(
		           openingElement->tagName(),
		           parentElement->OpeningElement->tagName()) &&
		       isUnclosedTag(parentElement);
	}

	return false;
}

// autoinsert.go:94 — isUnclosedFragment
bool isUnclosedFragment(::tsc::JsxFragment* node) {
	::tsc::Node* closingFragment = node->ClosingFragment;
	if (closingFragment->flags & NodeFlagsThisNodeHasError) {
		return true;
	}

	::tsc::Node* parent = node->parent;
	if (isJsxFragment(parent) &&
	    isUnclosedFragment(parent->as<::tsc::JsxFragment>())) {
		return true;
	}

	return false;
}

} // namespace

// autoinsert.go:13 — LanguageService.ProvideOnAutoInsert
std::pair<lsp::lsproto::VSOnAutoInsertResponse, gostd::Error>
LanguageService::ProvideOnAutoInsert(
    gostd::Context ctx, lsp::lsproto::VSOnAutoInsertParams* params) {
	if (tristateIsFalse(UserPreferences().EnableAutoClosingTags)) {
		return {lsp::lsproto::VSOnAutoInsertResponse{}, nullptr};
	}
	if (params->VSCh != ">") {
		return {lsp::lsproto::VSOnAutoInsertResponse{}, nullptr};
	}

	auto [program, sourceFile] =
	    getProgramAndFile(params->VSTextDocument.Uri);
	auto positions = converters->FromLSPPositionForSourceFile(
	    sourceFile, params->VSPosition, spanmap::FeatureAutoInsert);
	if (positions.size() != 1 || !positions[0].Fidelity.IsExact()) {
		return {lsp::lsproto::VSOnAutoInsertResponse{}, nullptr};
	}
	sourceFile = positions[0].Script;
	auto position = positions[0].Position;

	::tsc::Node* token =
	    astnav::findPrecedingToken(sourceFile, int(position));
	if (token == nullptr) {
		return {lsp::lsproto::VSOnAutoInsertResponse{}, nullptr};
	}

	std::string closingText;
	::tsc::Node* element = nullptr;
	if (token->kind == Kind::GreaterThanToken &&
	    isJsxOpeningElement(token->parent)) {
		element = token->parent->parent;
	} else if (isJsxText(token) && isJsxElement(token->parent)) {
		element = token->parent;
	}

	if (element != nullptr &&
	    isUnclosedTag(element->as<::tsc::JsxElement>())) {
		::tsc::Node* tagNameNode =
		    element->as<::tsc::JsxElement>()->OpeningElement->tagName();
		// Slight divergence from Strada - we don't use the verbatim text
		// from the opening tag.
		closingText = "</" +
		              EntityNameToString(tagNameNode, getTextOfNode) +
		              ">";
	} else {
		::tsc::Node* fragment = nullptr;
		if (token->kind == Kind::GreaterThanToken &&
		    isJsxOpeningFragment(token->parent)) {
			fragment = token->parent->parent;
		} else if (isJsxText(token) &&
		           isJsxFragment(token->parent)) {
			fragment = token->parent;
		}

		if (fragment != nullptr &&
		    isUnclosedFragment(fragment->as<::tsc::JsxFragment>())) {
			closingText = "</>";
		}
	}

	if (closingText.empty()) {
		return {lsp::lsproto::VSOnAutoInsertResponse{}, nullptr};
	}

	auto textEdit = std::make_shared<lsp::lsproto::TextEdit>();
	textEdit->Range = lsp::lsproto::Range{params->VSPosition,
	                                    params->VSPosition};
	// Tag names can contain `$` (valid JSX identifier characters), so
	// escape the closing text to avoid being interpreted as a snippet
	// placeholder/variable.
	textEdit->NewText = "$0" + escapeSnippetText(closingText);
	auto item = std::make_shared<lsp::lsproto::VSOnAutoInsertResponseItem>();
	item->VSTextEditFormat = lsp::lsproto::InsertTextFormatSnippet;
	item->VSTextEdit = std::move(textEdit);
	return {lsp::lsproto::VSOnAutoInsertResponse{std::move(item)}, nullptr};
}

} // namespace tsc::ls
