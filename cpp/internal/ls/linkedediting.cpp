// linkedediting.cpp — port of tsc/internal/ls/linkedediting.go.

#include "internal/ls/ls.h"

#include "internal/astnav/tokens.h"
#include "internal/scanner/scanner.h"
#include "internal/spanmap/spanmap.h"

namespace tsc::ls {

namespace {
// allow the client to match more than valid tag names. This allows linked
// editing when typing is in progress or tag name is incomplete
const std::string jsxTagWordPattern = "[a-zA-Z0-9:\\-\\._$]*";
} // namespace

// linkedediting.go:18 — LanguageService.ProvideLinkedEditingRange
std::pair<lsp::lsproto::LinkedEditingRangeResponse, gostd::Error>
LanguageService::ProvideLinkedEditingRange(
    gostd::Context ctx, lsp::lsproto::LinkedEditingRangeParams* params) {
	auto [program, sourceFile] =
	    getProgramAndFile(params->TextDocument.Uri);
	auto positions = converters->FromLSPPositionForSourceFile(
	    sourceFile, params->Position, spanmap::FeatureLinkedEditing);
	if (positions.size() != 1 || !positions[0].Fidelity.IsExact()) {
		return {lsp::lsproto::LinkedEditingRangeResponse{}, nullptr};
	}
	sourceFile = positions[0].Script;
	auto position = positions[0].Position;
	::tsc::Node* token =
	    astnav::findPrecedingToken(sourceFile, int(position));

	if (token == nullptr || token->parent->kind == Kind::SourceFile) {
		return {lsp::lsproto::LinkedEditingRangeResponse{}, nullptr};
	}

	if (isJsxFragment(token->parent->parent)) {
		::tsc::JsxFragment* fragment =
		    token->parent->parent->as<::tsc::JsxFragment>();
		::tsc::Node* openFragment = fragment->OpeningFragment;
		::tsc::Node* closeFragment = fragment->ClosingFragment;
		if (openFragment->flags &
		            NodeFlagsThisNodeOrAnySubNodesHasError ||
		    closeFragment->flags &
		            NodeFlagsThisNodeOrAnySubNodesHasError) {
			return {lsp::lsproto::LinkedEditingRangeResponse{}, nullptr};
		}

		TextPos openPos =
		    TextPos(astnav::getStartOfNode(openFragment, sourceFile,
		                                 false) +
		            int(std::string("<").size()));
		TextPos closePos =
		    TextPos(astnav::getStartOfNode(closeFragment, sourceFile,
		                                 false) +
		            int(std::string("</").size()));

		// only allows linked editing right after opening bracket: <| ></| >
		if (position != openPos && position != closePos) {
			return {lsp::lsproto::LinkedEditingRangeResponse{}, nullptr};
		}

		auto [openLineChar, openFidelity] =
		    converters->ToLSPPositionForFeature(sourceFile, openPos,
		                                      spanmap::FeatureLinkedEditing);
		auto [closeLineChar, closeFidelity] =
		    converters->ToLSPPositionForFeature(sourceFile, closePos,
		                                      spanmap::FeatureLinkedEditing);
		if (!openFidelity.IsExact() || !closeFidelity.IsExact()) {
			return {lsp::lsproto::LinkedEditingRangeResponse{}, nullptr};
		}
		auto ranges = std::make_shared<lsp::lsproto::LinkedEditingRanges>();
		// only return start position for opening tag since the length of a
		// fragment is always 3 and it is unlikely user will type in the
		// middle of a fragment tag
		ranges->Ranges = {{openLineChar, openLineChar},
		                  {closeLineChar, closeLineChar}};
		ranges->WordPattern = jsxTagWordPattern;
		return {lsp::lsproto::LinkedEditingRangeResponse{std::move(ranges)},
		        nullptr};
	} else {
		// determines if the cursor is in an element tag
		::tsc::Node* tag = findAncestor(
		    token->parent, [](::tsc::Node* n) -> bool {
			    if (isJsxOpeningElement(n) || isJsxClosingElement(n)) {
				    return true;
			    }
			    return false;
		    });
		if (tag == nullptr) {
			return {lsp::lsproto::LinkedEditingRangeResponse{}, nullptr};
		}
		if (!(isJsxOpeningElement(tag) || isJsxClosingElement(tag))) {
			TSC_UNREACHABLE(
			    "tag should be opening or closing element");
		}

		::tsc::JsxElement* jsxElement =
		    tag->parent->as<::tsc::JsxElement>();
		::tsc::Node* openTag = jsxElement->OpeningElement;
		::tsc::Node* closeTag = jsxElement->ClosingElement;

		int openTagNameStart = astnav::getStartOfNode(
		    openTag->tagName(), sourceFile, false);
		int openTagNameEnd = openTag->tagName()->end();
		int closeTagNameStart = astnav::getStartOfNode(
		    closeTag->tagName(), sourceFile, false);
		int closeTagNameEnd = closeTag->tagName()->end();
		// do not return linked cursors if tags are not well-formed
		if (openTagNameStart ==
		        astnav::getStartOfNode(openTag, sourceFile, false) ||
		    closeTagNameStart ==
		        astnav::getStartOfNode(closeTag, sourceFile, false) ||
		    openTagNameEnd == openTag->end() ||
		    closeTagNameEnd == closeTag->end()) {
			return {lsp::lsproto::LinkedEditingRangeResponse{}, nullptr};
		}
		// only return linked cursors if the cursor is within a tag name
		int positionInt = int(position);
		if (!((openTagNameStart <= positionInt &&
		              positionInt <= openTagNameEnd) ||
		      (closeTagNameStart <= positionInt &&
		              positionInt <= closeTagNameEnd))) {
			return {lsp::lsproto::LinkedEditingRangeResponse{}, nullptr};
		}

		// only return linked cursors if text in both tags is identical
		std::string openingTagText =
		    getTextOfNode(openTag->tagName());
		if (openingTagText !=
		    getTextOfNode(closeTag->tagName())) {
			return {lsp::lsproto::LinkedEditingRangeResponse{}, nullptr};
		}

		auto [openRange, openFidelity] = converters->ToLSPRangeForFeature(
		    sourceFile, TextRange{TextPos(openTagNameStart),
		                          TextPos(openTagNameEnd)},
		    spanmap::FeatureLinkedEditing);
		auto [closeRange, closeFidelity] = converters->ToLSPRangeForFeature(
		    sourceFile, TextRange{TextPos(closeTagNameStart),
		                          TextPos(closeTagNameEnd)},
		    spanmap::FeatureLinkedEditing);
		if (!openFidelity.IsExact() || !closeFidelity.IsExact()) {
			return {lsp::lsproto::LinkedEditingRangeResponse{}, nullptr};
		}

		auto ranges = std::make_shared<lsp::lsproto::LinkedEditingRanges>();
		ranges->Ranges = {openRange, closeRange};
		ranges->WordPattern = jsxTagWordPattern;
		return {lsp::lsproto::LinkedEditingRangeResponse{std::move(ranges)},
		        nullptr};
	}
}

} // namespace tsc::ls
