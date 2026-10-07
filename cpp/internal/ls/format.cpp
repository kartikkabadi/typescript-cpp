// === slice: ls-coreC ===
// format.cpp — format.go: formatting feature handlers (document/range/on-type)
// plus the mapped-range helpers for content-mapped (virtual) files.
#include "internal/astnav/tokens.h"
#include "internal/ls/ls.h"

#include <algorithm>

namespace tsc::ls {

namespace {

// core.NewTextRange
TextRange newTextRange(int pos, int end) {
	return TextRange{static_cast<TextPos>(pos), static_cast<TextPos>(end)};
}

} // namespace

// ============================================================================
// format.go — text-edit conversion helper
// ============================================================================
// format.go:19 — toLSProtoTextEdits. Any non-exact fidelity aborts the whole
// conversion (Go `return nil`).
lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>>
LanguageService::toLSProtoTextEdits(SourceFile* file,
                                    std::vector<TextChange> changes) {
	std::vector<std::shared_ptr<lsp::lsproto::TextEdit>> result;
	result.reserve(changes.size());
	for (auto& c : changes) {
		auto [lspRange, fidelity] =
			converters->ToLSPRange(file, newTextRange(c.pos(), c.end()));
		if (!fidelity.IsExact()) {
			return std::nullopt;
		}
		auto edit = std::make_shared<lsp::lsproto::TextEdit>();
		edit->NewText = c.NewText;
		edit->Range = lspRange;
		result.push_back(std::move(edit));
	}
	return result;
}

// ============================================================================
// format.go — document formatting handlers
// ============================================================================
// format.go:34 — ProvideFormatDocument
lsp::lsproto::DocumentFormattingResponse LanguageService::ProvideFormatDocument(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
	lsp::lsproto::FormattingOptions* options) {
	if (tristateIsFalse(UserPreferences().EnableFormatting)) {
		return lsp::lsproto::TextEditsOrNull{};
	}

	SourceFile* file = getProgramAndFile(documentURI).second;
	lsutil::FormatCodeSettings formatOpts =
		lsutil::FromLSFormatOptions(FormatOptions(), *options);
	lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>> edits;
	if (file->ContentMapper().empty()) {
		edits = toLSProtoTextEdits(file, getFormattingEditsForDocument(ctx, file, formatOpts));
	} else {
		edits = getFormattingEditsForMappedRange(
			ctx, file, formatOpts, newTextRange(0, int(file->OriginalText().size())));
	}
	lsp::lsproto::TextEditsOrNull res;
	res.TextEdits = std::make_shared<lsp::lsproto::Slice<
		std::shared_ptr<lsp::lsproto::TextEdit>>>(std::move(edits));
	return res;
}

// getFormattingEditsForMappedRange formats each formatting-enabled verbatim
// intersection with originalRange. Duplicate formatting projections are
// unsupported. If mappings overlap anyway, each original-text position is
// formatted only once, preferring the earliest and then longest applicable
// mapping.
// format.go:56
lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>>
LanguageService::getFormattingEditsForMappedRange(
	gostd::Context ctx, SourceFile* file, lsutil::FormatCodeSettings options,
	TextRange originalRange) {
	std::vector<SourceFile*> projections;
	projections.push_back(file);
	if (const std::vector<SourceFile*>* supplemental = file->SupplementalSourceFiles()) {
		projections.insert(projections.end(), supplemental->begin(), supplemental->end());
	}
	std::vector<mappedFormattingRange> candidates;
	for (auto* projection : projections) {
		auto* spanMap = projection->SpanMap();
		if (spanMap == nullptr) {
			continue;
		}
		for (auto& segment : spanmap::Segments(spanMap)) {
			if (segment.Kind != spanmap::KindVerbatim ||
				(segment.Features & spanmap::FeatureFormatting) == 0) {
				continue;
			}
			int originalStart = std::max(int(originalRange.pos()), int(segment.OriginalStart));
			int originalEnd = std::min(int(originalRange.end()), int(segment.OriginalEnd));
			if (originalStart >= originalEnd) {
				continue;
			}
			candidates.push_back(mappedFormattingRange{projection, segment,
													   newTextRange(originalStart, originalEnd)});
		}
	}

	std::vector<std::shared_ptr<lsp::lsproto::TextEdit>> edits;
	for (auto& candidate : nonOverlappingFormattingRanges(candidates)) {
		TextRange virtualRange = newTextRange(
			int(candidate.segment.VirtualStart) + int(candidate.originalRange.pos()) -
				int(candidate.segment.OriginalStart),
			int(candidate.segment.VirtualStart) + int(candidate.originalRange.end()) -
				int(candidate.segment.OriginalStart));
		for (auto& change :
			 getFormattingEditsForRange(ctx, candidate.projection, options, virtualRange)) {
			if (change.pos() < virtualRange.pos() || change.end() > virtualRange.end()) {
				continue;
			}
			auto [lspRange, fidelity] = converters->ToLSPRangeForFeature(
				candidate.projection, newTextRange(change.pos(), change.end()),
				spanmap::FeatureFormatting);
			if (!fidelity.IsExact()) {
				continue;
			}
			auto edit = std::make_shared<lsp::lsproto::TextEdit>();
			edit->Range = lspRange;
			edit->NewText = change.NewText;
			edits.push_back(std::move(edit));
		}
	}
	std::stable_sort(edits.begin(), edits.end(),
					 [](const std::shared_ptr<lsp::lsproto::TextEdit>& a,
						const std::shared_ptr<lsp::lsproto::TextEdit>& b) {
						 if (int c = lsp::lsproto::CompareRanges(a->Range, b->Range); c != 0) {
							 return c < 0;
						 }
						 return a->NewText < b->NewText;
					 });
	return edits;
}

// mappedFormattingRange — format.go:107
// (declared in ls.h)

// nonOverlappingFormattingRanges chooses at most one formatting projection
// for each original-text position. Candidates are ordered by original start
// and then descending end, so a longer mapping wins when several mappings
// start together. A later candidate's start is trimmed to the last accepted
// range's end; fully covered candidates are discarded.
// format.go:130
std::vector<mappedFormattingRange> nonOverlappingFormattingRanges(
	std::vector<mappedFormattingRange> candidates) {
	std::stable_sort(candidates.begin(), candidates.end(),
					 [](const mappedFormattingRange& a, const mappedFormattingRange& b) {
						 if (a.originalRange.pos() != b.originalRange.pos()) {
							 return a.originalRange.pos() < b.originalRange.pos();
						 }
						 return b.originalRange.end() < a.originalRange.end();
					 });

	std::vector<mappedFormattingRange> result;
	result.reserve(candidates.size());
	for (auto& candidate : candidates) {
		if (!result.empty()) {
			candidate.originalRange = candidate.originalRange.withPos(std::max(
				int(candidate.originalRange.pos()), int(result.back().originalRange.end())));
		}
		if (candidate.originalRange.len() > 0) {
			result.push_back(candidate);
		}
	}
	return result;
}

// format.go:151 — ProvideFormatDocumentRange
lsp::lsproto::DocumentRangeFormattingResponse LanguageService::ProvideFormatDocumentRange(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
	lsp::lsproto::FormattingOptions* options, lsp::lsproto::Range r) {
	if (tristateIsFalse(UserPreferences().EnableFormatting)) {
		return lsp::lsproto::TextEditsOrNull{};
	}
	SourceFile* file = getProgramAndFile(documentURI).second;
	lsutil::FormatCodeSettings formatOpts =
		lsutil::FromLSFormatOptions(FormatOptions(), *options);
	if (!file->ContentMapper().empty()) {
		lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>> edits =
			getFormattingEditsForMappedRange(
				ctx, file, formatOpts, converters->FromLSPRangeToOriginal(file, r));
		lsp::lsproto::TextEditsOrNull res;
		res.TextEdits = std::make_shared<lsp::lsproto::Slice<
		std::shared_ptr<lsp::lsproto::TextEdit>>>(std::move(edits));
		return res;
	}
	auto ranges =
		converters->FromLSPRangeForSourceFile(file, r, spanmap::FeatureFormatting);
	if (ranges.size() != 1 || !ranges[0].Fidelity.IsExact()) {
		return lsp::lsproto::TextEditsOrNull{};
	}
	file = ranges[0].Script;
	lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>> edits =
		toLSProtoTextEdits(file, getFormattingEditsForRange(ctx, file, formatOpts, ranges[0].Span));
	lsp::lsproto::TextEditsOrNull res;
	res.TextEdits = std::make_shared<lsp::lsproto::Slice<
		std::shared_ptr<lsp::lsproto::TextEdit>>>(std::move(edits));
	return res;
}

// format.go:180 — ProvideFormatDocumentOnType
lsp::lsproto::DocumentOnTypeFormattingResponse LanguageService::ProvideFormatDocumentOnType(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
	lsp::lsproto::FormattingOptions* options, lsp::lsproto::Position position,
	std::string character) {
	if (tristateIsFalse(UserPreferences().EnableFormatting)) {
		return lsp::lsproto::TextEditsOrNull{};
	}
	SourceFile* file = getProgramAndFile(documentURI).second;
	lsutil::FormatCodeSettings formatOpts =
		lsutil::FromLSFormatOptions(FormatOptions(), *options);
	auto positions = converters->FromLSPPositionForSourceFile(file, position,
															spanmap::FeatureFormatting);
	if (positions.size() != 1 || !positions[0].Fidelity.IsExact()) {
		return lsp::lsproto::TextEditsOrNull{};
	}
	file = positions[0].Script;
	lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>> edits =
		toLSProtoTextEdits(file, getFormattingEditsAfterKeystroke(
							   ctx, file, formatOpts, int(positions[0].Position),
							   character));
	lsp::lsproto::TextEditsOrNull res;
	res.TextEdits = std::make_shared<lsp::lsproto::Slice<
		std::shared_ptr<lsp::lsproto::TextEdit>>>(std::move(edits));
	return res;
}

// ============================================================================
// format.go — format-package entry points
// ============================================================================
// format.go:207 — getFormattingEditsForRange
std::vector<TextChange> LanguageService::getFormattingEditsForRange(
	gostd::Context ctx, SourceFile* file, lsutil::FormatCodeSettings options, TextRange r) {
	// Go decorates the incoming context.Context; the C++ FormatRequestContext
	// replaces it (no values carried).
	format::FormatRequestContext fctx;
	fctx = format::WithFormatCodeSettings(fctx, options, options.NewLineCharacter);
	return format::FormatSelection(fctx, file, int(r.pos()), int(r.end()));
}

// format.go:217 — getFormattingEditsForDocument
std::vector<TextChange> LanguageService::getFormattingEditsForDocument(
	gostd::Context ctx, SourceFile* file, lsutil::FormatCodeSettings options) {
	format::FormatRequestContext fctx;
	fctx = format::WithFormatCodeSettings(fctx, options, options.NewLineCharacter);
	return format::FormatDocument(fctx, file);
}

// format.go:226 — getFormattingEditsAfterKeystroke
std::vector<TextChange> LanguageService::getFormattingEditsAfterKeystroke(
	gostd::Context ctx, SourceFile* file, lsutil::FormatCodeSettings options, int position,
	std::string key) {
	format::FormatRequestContext fctx;
	fctx = format::WithFormatCodeSettings(fctx, options, options.NewLineCharacter);

	::tsc::Node* tokenAtPosition = astnav::getTokenAtPosition(file, position);
	if (isInComment(file, position, tokenAtPosition) == nullptr) {
		if (key == "{") {
			return format::FormatOnOpeningCurly(fctx, file, position);
		}
		if (key == "}") {
			return format::FormatOnClosingCurly(fctx, file, position);
		}
		if (key == ";") {
			return format::FormatOnSemicolon(fctx, file, position);
		}
		if (key == "\n") {
			return format::FormatOnEnter(fctx, file, position);
		}
		return {};
	}
	return {};
}

// Unlike the TS implementation, this function *will not* compute default values for
// `precedingToken` and `tokenAtPosition`.
// It is the caller's responsibility to call `astnav.getTokenAtPosition` to compute a default `tokenAtPosition`,
// or `astnav.findPrecedingToken` to compute a default `precedingToken`.
// format.go:257 — getRangeOfEnclosingComment
CommentRange* getRangeOfEnclosingComment(SourceFile* file, int position,
										 ::tsc::Node* precedingToken,
										 ::tsc::Node* tokenAtPosition) {
	::tsc::Node* jsdoc = findAncestorKind(tokenAtPosition, Kind::JSDoc);
	if (jsdoc != nullptr) {
		tokenAtPosition = jsdoc->parent;
	}
	int tokenStart = astnav::getStartOfNode(tokenAtPosition, file, false /*includeJSDoc*/);
	if (tokenStart <= position && position < int(tokenAtPosition->end())) {
		return nullptr;
	}

	// Between two consecutive tokens, all comments are either trailing on the former
	// or leading on the latter (and none are in both lists).
	std::vector<CommentRange> commentRanges;
	if (precedingToken != nullptr) {
		tsc::getTrailingCommentRanges(file->Text(), int(precedingToken->end()),
										  [&](const CommentRange& r) {
											  commentRanges.push_back(r);
											  return true;
										  });
	}
	for (auto& r : getLeadingCommentRangesOfNode(tokenAtPosition, file)) {
		commentRanges.push_back(r);
	}
	for (auto& commentRange : commentRanges) {
		// The end marker of a single-line comment does not include the newline character.
		// In the following case where the cursor is at `^`, we are inside a comment:
		//
		//    // asdf   ^\n
		//
		// But for closed multi-line comments, we don't want to be inside the comment in the following case:
		//
		//    /* asdf */^
		//
		// Internally, we represent the end of the comment prior to the newline and at the '/', respectively.
		//
		// However, unterminated multi-line comments lack a `/`, end at the end of the file, and *do* contain their end.
		if ((position > int(commentRange.pos()) && position < int(commentRange.end())) ||
			(position == int(commentRange.end()) &&
			 (commentRange.kind == Kind::SingleLineCommentTrivia ||
			  position == int(file->Text().size())))) {
			// Go returns &commentRange — the range variable escapes to the
			// heap. Returning the address of the local vector element here
			// would dangle once commentRanges is destroyed.
			return new CommentRange(commentRange);
		}
	}
	return nullptr;
}

} // namespace tsc::ls
