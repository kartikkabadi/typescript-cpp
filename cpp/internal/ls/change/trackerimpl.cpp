// === slice: ls-foundation ===
// ls/change/trackerimpl.go — Tracker text-change computation and trivia
// adjustments.

#include "internal/ls/change/change.h"
#include "internal/ls/lsdeps.h" // lsconv::Converters methods

#include <algorithm>
#include <string>
#include <vector>

#include "internal/astnav/tokens.h"
#include "internal/core/textchange.h"
#include "internal/debug/debug.h"
#include "internal/ls/lsutil/unicode_tables.h"
#include "internal/parser/parser.h"
#include "internal/printer/emitcontext.h"
#include "internal/printer/printer.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::ls::change {

namespace {

// core.NewTextRange
TextRange newTextRange(int pos, int end) {
	return TextRange{static_cast<TextPos>(pos), static_cast<TextPos>(end)};
}

// slices.Collect over the scanner's comment-range iterators.
std::vector<CommentRange> collectCommentRanges(
	const std::function<void(const std::function<bool(const CommentRange&)>&)>& seq) {
	std::vector<CommentRange> out;
	seq([&out](const CommentRange& r) {
		out.push_back(r);
		return true;
	});
	return out;
}

// strings.Join
std::string join(const std::vector<std::string>& parts, std::string_view sep) {
	std::string out;
	for (size_t i = 0; i < parts.size(); i++) {
		if (i != 0) out += sep;
		out += parts[i];
	}
	return out;
}

// strings.TrimSuffix
std::string trimSuffix(const std::string& s, std::string_view suffix) {
	if (!suffix.empty() && s.size() >= suffix.size() &&
		s.compare(s.size() - suffix.size(), std::string::npos, suffix) == 0) {
		return s.substr(0, s.size() - suffix.size());
	}
	return s;
}

// strings.HasSuffix
bool hasSuffix(std::string_view s, std::string_view suffix) {
	return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

// strings.LastIndexAny(original[:pos], "\r\n")
int lastIndexAnyLineBreak(std::string_view s, int pos) {
	int idx = -1;
	for (int i = 0; i < pos && i < static_cast<int>(s.size()); i++) {
		if (s[i] == '\r' || s[i] == '\n') {
			idx = i;
		}
	}
	return idx;
}

// dedupeIdenticalEdits — trackerimpl.go:92. Drops exact duplicates from a
// sorted slice of edits. When a mapper copies one span of the original into
// more than one projection, an edit computed against each projection
// describes the same change to the same original range; emitting it once per
// projection would apply it repeatedly.
std::vector<lsp::lsproto::TextEdit> dedupeIdenticalEdits(std::vector<lsp::lsproto::TextEdit> edits) {
	std::vector<lsp::lsproto::TextEdit> deduped;
	for (auto& edit : edits) {
		if (!deduped.empty() && deduped.back().Range == edit.Range &&
			deduped.back().NewText == edit.NewText) {
			continue;
		}
		deduped.push_back(std::move(edit));
	}
	return deduped;
}

// leadingIndentation — trackerimpl.go:208
std::string_view leadingIndentation(std::string_view text) {
	size_t end = 0;
	while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
		end++;
	}
	return text.substr(0, end);
}

// positionsAreOnSameLine — delete.go:247 (shared within the package; also
// defined in tracker.cpp's anonymous namespace).
bool positionsAreOnSameLine(int pos1, int pos2, SourceFile* sourceFile) {
	return format::GetLineStartPositionForPosition(pos1, sourceFile) ==
		   format::GetLineStartPositionForPosition(pos2, sourceFile);
}

} // namespace

// textEditsConflict — trackerimpl.go:105
bool textEditsConflict(const lsp::lsproto::TextEdit& a, const lsp::lsproto::TextEdit& b,
					   bool multipleProjections) {
	if (lsp::lsproto::ComparePositions(a.Range.End, b.Range.Start) > 0) {
		return true;
	}
	// Different insertions at the same position are ambiguous when they may
	// come from different projections.
	return multipleProjections && a.Range.Start == a.Range.End && a.Range == b.Range &&
		a.NewText != b.NewText;
}

// getTextChangesFromChanges — trackerimpl.go:20
std::map<std::string, std::vector<lsp::lsproto::TextEdit>> Tracker::getTextChangesFromChanges() {
	std::map<std::string, std::vector<lsp::lsproto::TextEdit>> changes;
	// A content-mapped file can have several projections, each keyed
	// separately in t.changes but all sharing one original file. Their edits
	// are collected together before being ordered and checked, so duplicate
	// edits are emitted once and conflicting edits are rejected regardless of
	// map iteration order.
	std::unordered_map<std::string, int> projections;
	for (auto& [sourceFile, changesInFile] : this->changes.M) {
		std::string fileName = sourceFile->OriginalFileName();
		if (unmappableFiles.Has(fileName)) {
			continue;
		}
		// For a content-mapped file, a mapper may reorder source text relative
		// to the original document. Convert first, then sort and check for
		// overlap in the space where edits are actually applied.
		std::vector<lsp::lsproto::TextEdit> textChanges;
		for (trackerEdit* change : changesInFile) {
			// !!! targetSourceFile

			std::string newText = computeNewText(change, sourceFile, sourceFile);
			// span := createTextSpanFromRange(c.Range)
			// !!!
			// Filter out redundant changes.
			// if (span.length == newText.length && stringContainsAt(targetSourceFile.text, newText, span.start)) { return nil }
			lsp::lsproto::TextEdit edit;
			edit.NewText = std::move(newText);
			edit.Range = toLSPEditRange(sourceFile, change->textRange);
			textChanges.push_back(std::move(edit));
		}

		if (!textChanges.empty()) {
			auto& dst = changes[fileName];
			dst.insert(dst.end(), std::make_move_iterator(textChanges.begin()),
					   std::make_move_iterator(textChanges.end()));
			projections[fileName]++;
		}
	}

	for (auto& [fileName, textChanges] : changes) {
		// Converting the edits above may have found that this file cannot be
		// represented in its original text. GetChanges drops it, so its order
		// does not matter, and the best-effort ranges left behind may overlap
		// in ways the check below would refuse.
		if (unmappableFiles.Has(fileName)) {
			continue;
		}
		// order changes by start position
		// If the start position is the same, put the shorter range first,
		// since an empty range (x, x) may precede (x, y) but not vice-versa.
		std::stable_sort(textChanges.begin(), textChanges.end(),
						 [](const lsp::lsproto::TextEdit& a, const lsp::lsproto::TextEdit& b) {
							 return lsp::lsproto::CompareRanges(a.Range, b.Range) < 0;
						 });
		if (projections[fileName] > 1) {
			textChanges = dedupeIdenticalEdits(std::move(textChanges));
		}
		// verify that change intervals do not overlap, except possibly at end
		// points.
		for (size_t i = 0; i + 1 < textChanges.size(); i++) {
			if (textEditsConflict(textChanges[i], textChanges[i + 1], projections[fileName] > 1)) {
				if (projections[fileName] > 1) {
					// Projections of one original range disagree about how to
					// edit it. That is a property of the mapper's output
					// rather than a bug here, so drop the file instead of
					// failing.
					unmappableFiles.Add(fileName);
					break;
				}
				// assert change[i].End <= change[i + 1].Start
				TSC_UNREACHABLE("changes overlap");
			}
		}
	}
	return changes;
}

// computeNewText — trackerimpl.go:113
std::string Tracker::computeNewText(trackerEdit* change, SourceFile* targetSourceFile,
									SourceFile* sourceFile) {
	switch (change->kind) {
	case trackerEditKindRemove:
		return "";
	case trackerEditKindText:
		return change->NewText;
	default:
		break;
	}

	auto positions = converters->FromLSPPositionForSourceFile(
		sourceFile, toLSPEditRange(sourceFile, change->textRange).Start, spanmap::FeatureAll);
	std::string result;
	bool found = false;
	// The original range may have multiple verbatim copies; it is safe to lose
	// their identity only when formatting at every exact projection produces
	// the same edit.
	for (auto& mapped : positions) {
		if (!mapped.Fidelity.IsExact()) {
			continue;
		}
		SourceFile* projection = mapped.Script;
		int pos = static_cast<int>(mapped.Position);
		auto formatNode = [&](Node* n) -> std::string {
			return getFormattedTextOfNode(n, targetSourceFile, projection, pos, change->options);
		};

		std::string text;
		switch (change->kind) {
		case trackerEditKindReplaceWithMultipleNodes: {
			std::string joiner = change->options.joiner;
			if (joiner.empty()) {
				joiner = newLine;
			}
			std::vector<std::string> formatted;
			formatted.reserve(change->nodes.size());
			for (Node* n : change->nodes) {
				formatted.push_back(trimSuffix(formatNode(n), newLine));
			}
			text = join(formatted, joiner);
			break;
		}
		case trackerEditKindReplaceWithSingleNode:
			text = formatNode(change->node);
			break;
		default:
			TSC_UNREACHABLE("change kind should have been handled earlier");
		}
		// Strip initial indentation if text will be inserted in the middle of
		// the line.
		std::string noIndent = text;
		if (!(change->options.indentation.has_value() ||
			  format::GetLineStartPositionForPosition(pos, projection) == pos)) {
			noIndent = std::string(lsutil::detail::trimSpaceLeft(text));
		}
		std::string candidate = change->options.Prefix + noIndent +
			(hasSuffix(noIndent, change->options.Suffix) ? "" : change->options.Suffix);
		if (found && candidate != result) {
			unmappableFiles.Add(sourceFile->OriginalFileName());
			return "";
		}
		result = candidate;
		found = true;
	}
	if (!found) {
		unmappableFiles.Add(sourceFile->OriginalFileName());
	}
	return reindentInsertedLines(sourceFile, change, result);
}

// reindentInsertedLines — trackerimpl.go:183. Fixes the indentation of a
// line an insertion introduces into the document the edit is applied to. The
// inserted text forms its own line when it ends in a newline, and that line
// has to pick up the indentation of the line it is being spliced into. Where
// the indentation goes depends on which side of the insertion point it
// already sits on:
//
//   - Inserting after a line's indentation (`\t\tfoo` with the point before
//     `foo`) leaves the inserted text indented but pushes the rest of the
//     line down bare, so the indentation is repeated after the text.
//   - Inserting at the very start of a line leaves the existing text
//     indented but puts the inserted text at column zero, so the indentation
//     is emitted before the text.
//
// The indentation is read from the original text at the edit's original
// position, so it holds for a content-mapped file whose projection is
// indented differently from the document the edit is applied to. Edits
// carrying an explicit indentation option are left alone.
std::string Tracker::reindentInsertedLines(SourceFile* sourceFile, trackerEdit* change,
										   const std::string& text) {
	if (text.empty() || change->textRange.pos() != change->textRange.end() ||
		change->options.indentation.has_value()) {
		return text;
	}
	if (!hasSuffix(text, newLine)) {
		return text;
	}
	const std::string& original = sourceFile->OriginalText();
	int pos = change->textRange.pos();
	if (spanmap::SpanMap* spans = sourceFile->SpanMap(); spans != nullptr) {
		auto [mapped, fidelity] = spanmap::VirtualToOriginalPosition(spans, TextPos(pos));
		if (!fidelity.IsExact()) {
			return text;
		}
		pos = static_cast<int>(mapped);
	}
	if (pos < 0 || pos > static_cast<int>(original.size())) {
		return text;
	}
	int lineStart = lastIndexAnyLineBreak(original, pos) + 1;
	std::string_view beforePoint = std::string_view(original).substr(lineStart, pos - lineStart);
	if (beforePoint.empty()) {
		// At the start of a line: the existing text keeps its indentation, and
		// the inserted line needs it — but only when the formatter left the
		// text at column zero. Where the formatter already indented it
		// (inserting into a multi-line list, say), that indentation is the
		// correct one.
		if (!leadingIndentation(text).empty()) {
			return text;
		}
		return std::string(leadingIndentation(std::string_view(original).substr(lineStart))) + text;
	}
	if (leadingIndentation(beforePoint) != beforePoint) {
		return text;
	}
	// Just past the indentation: the inserted line already has it, the text
	// pushed down needs it back.
	return text + std::string(beforePoint);
}

// getFormattedTextOfNode — trackerimpl.go:213
std::string Tracker::getFormattedTextOfNode(Node* nodeIn, SourceFile* targetSourceFile,
											SourceFile* sourceFile, int pos,
											const NodeOptions& options) {
	auto [text, sourceFileLike] = getNonformattedText(nodeIn, targetSourceFile);
	// !!! if (validate) validate(node, text);
	lsutil::FormatCodeSettings formatOptions =
		GetFormatCodeSettingsForWriting(formatSettings, targetSourceFile);

	int initialIndentation = 0, delta = 0;
	if (!options.indentation.has_value()) {
		initialIndentation = format::GetIndentation(
			pos, sourceFile, formatOptions,
			options.Prefix == newLine ||
				format::GetLineStartPositionForPosition(pos, sourceFile) == pos);
	} else {
		initialIndentation = *options.indentation;
	}

	if (options.delta.has_value()) {
		delta = *options.delta;
	} else if (formatOptions.IndentSize != 0 &&
			   format::ShouldIndentChildNode(formatOptions, nodeIn, nullptr, nullptr)) {
		delta = formatOptions.IndentSize;
	}

	auto edits = format::FormatNodeGivenIndentation(ctx, sourceFileLike,
													sourceFileLike->as<SourceFile>(),
													targetSourceFile->LanguageVariant,
													initialIndentation, delta);
	return ApplyBulkEdits(text, edits);
}

// GetFormatCodeSettingsForWriting — trackerimpl.go:239
lsutil::FormatCodeSettings GetFormatCodeSettingsForWriting(lsutil::FormatCodeSettings options,
														 SourceFile* sourceFile) {
	bool shouldAutoDetectSemicolonPreference =
		options.Semicolons == lsutil::SemicolonPreference::Ignore;
	bool shouldRemoveSemicolons =
		options.Semicolons == lsutil::SemicolonPreference::Remove ||
		(shouldAutoDetectSemicolonPreference && !lsutil::ProbablyUsesSemicolons(sourceFile));
	if (shouldRemoveSemicolons) {
		options.Semicolons = lsutil::SemicolonPreference::Remove;
	}

	return options;
}

// getNonformattedText — trackerimpl.go:254
std::pair<std::string, Node*> Tracker::getNonformattedText(Node* node, SourceFile* sourceFile) {
	auto [text, nodeOut] = printer::PrintAndPositionNode(nodeFactory, node, sourceFile, newLine,
													   formatSettings.IndentSize, emitContext);
	Node* sourceFileLike = printer::CreateSyntheticSourceFile(
		nodeFactory, nodeOut, text,
		SourceFileParseOptions{sourceFile->FileName(), sourceFile->Path()});
	return {text, sourceFileLike};
}

// GetAdjustedRange — trackerimpl.go:265. Computes the adjusted range for a
// node in a source file, accounting for trivia.
TextRange Tracker::GetAdjustedRange(SourceFile* sourceFile, Node* startNode, Node* endNode,
									LeadingTriviaOption leadingOption,
									TrailingTriviaOption trailingOption) {
	return newTextRange(getAdjustedStartPosition(sourceFile, startNode, leadingOption, false),
						getAdjustedEndPosition(sourceFile, endNode, trailingOption));
}

// getAdjustedStartPosition — trackerimpl.go:275. Method on the changeTracker
// because use of converters.
int Tracker::getAdjustedStartPosition(SourceFile* sourceFile, Node* node,
									  LeadingTriviaOption leadingOption, bool hasTrailingComment) {
	if (leadingOption == LeadingTriviaOptionJSDoc) {
		std::vector<CommentRange> jsDocComments =
			getJSDocCommentRanges(nullptr, node, sourceFile->Text());
		if (!jsDocComments.empty()) {
			return format::GetLineStartPositionForPosition(jsDocComments[0].pos(), sourceFile);
		}
	}

	int start = astnav::getStartOfNode(node, sourceFile, false);
	int startOfLinePos = format::GetLineStartPositionForPosition(start, sourceFile);

	switch (leadingOption) {
	case LeadingTriviaOptionExclude:
		return start;
	case LeadingTriviaOptionStartLine:
		if (node->loc.containsInclusive(startOfLinePos)) {
			return startOfLinePos;
		}
		return start;
	default:
		break;
	}

	int fullStart = node->pos();
	if (fullStart == start) {
		return start;
	}
	const std::vector<TextPos>& lineStarts = sourceFile->ecmaLineMap();
	int fullStartLineIndex = computeLineOfPosition(lineStarts, fullStart);
	int fullStartLinePos = static_cast<int>(lineStarts[fullStartLineIndex]);
	if (startOfLinePos == fullStartLinePos) {
		// full start and start of the node are on the same line
		//   a,     b;
		//    ^     ^
		//    |   start
		// fullstart
		// when b is replaced - we usually want to keep the leading trvia
		// when b is deleted - we delete it
		if (leadingOption == LeadingTriviaOptionIncludeAll) {
			return fullStart;
		}
		return start;
	}

	// if node has a trailing comments, use comment end position as the text
	// has already been included.
	if (hasTrailingComment) {
		// Check first for leading comments as if the node is the first import,
		// we want to exclude the trivia; otherwise we get the trailing
		// comments.
		std::vector<CommentRange> comments =
			collectCommentRanges([&](const std::function<bool(const CommentRange&)>& yield) {
				getLeadingCommentRanges(sourceFile->Text(), fullStart, yield);
			});
		if (comments.empty()) {
			comments = collectCommentRanges([&](const std::function<bool(const CommentRange&)>& yield) {
				getTrailingCommentRanges(sourceFile->Text(), fullStart, yield);
			});
		}
		if (!comments.empty()) {
			SkipTriviaOptions triviaOpts;
			triviaOpts.stopAfterLineBreak = true;
			triviaOpts.stopAtComments = true;
			return skipTriviaEx(sourceFile->Text(), comments[0].end(), triviaOpts);
		}
	}

	// get start position of the line following the line that contains
	// fullstart position (but only if the fullstart isn't the very beginning
	// of the file)
	int nextLineStart = fullStart > 0 ? 1 : 0;
	int adjustedStartPosition = static_cast<int>(lineStarts[fullStartLineIndex + nextLineStart]);
	// skip whitespaces/newlines
	SkipTriviaOptions triviaOpts;
	triviaOpts.stopAtComments = true;
	adjustedStartPosition = skipTriviaEx(sourceFile->Text(), adjustedStartPosition, triviaOpts);
	return static_cast<int>(lineStarts[computeLineOfPosition(lineStarts, adjustedStartPosition)]);
}

// getEndPositionOfMultilineTrailingComment — trackerimpl.go:332. Returns the
// end position of a multiline comment if it is on another line; otherwise
// returns 0 (Go's `undefined` sentinel usage here is 0).
int Tracker::getEndPositionOfMultilineTrailingComment(SourceFile* sourceFile, Node* node,
													  TrailingTriviaOption trailingOpt) {
	if (trailingOpt == TrailingTriviaOptionInclude) {
		// If the trailing comment is a multiline comment that extends to the
		// next lines, return the end of the comment and track it for the next
		// nodes to adjust.
		const std::vector<TextPos>& lineStarts = sourceFile->ecmaLineMap();
		int nodeEndLine = computeLineOfPosition(lineStarts, node->end());
		std::vector<CommentRange> comments =
			collectCommentRanges([&](const std::function<bool(const CommentRange&)>& yield) {
				getTrailingCommentRanges(sourceFile->Text(), node->end(), yield);
			});
		for (const CommentRange& comment : comments) {
			// Single line can break the loop as trivia will only be this line.
			// Comments on subsequent lines are also ignored.
			if (comment.kind == Kind::SingleLineCommentTrivia ||
				computeLineOfPosition(lineStarts, comment.pos()) > nodeEndLine) {
				break;
			}

			// Get the end line of the comment and compare against the end line
			// of the node. If the comment end line position and the multiline
			// comment extends to multiple lines, then is safe to return the
			// end position.
			int commentEndLine = computeLineOfPosition(lineStarts, comment.end());
			if (commentEndLine > nodeEndLine) {
				SkipTriviaOptions triviaOpts;
				triviaOpts.stopAfterLineBreak = true;
				triviaOpts.stopAtComments = true;
				return skipTriviaEx(sourceFile->Text(), comment.end(), triviaOpts);
			}
		}
	}

	return 0;
}

// getAdjustedEndPosition — trackerimpl.go:354. Method on the changeTracker
// because of converters.
int Tracker::getAdjustedEndPosition(SourceFile* sourceFile, Node* node,
									TrailingTriviaOption trailingOpt) {
	if (trailingOpt == TrailingTriviaOptionExclude) {
		return node->end();
	}
	if (trailingOpt == TrailingTriviaOptionExcludeWhitespace) {
		// slices.AppendSeq(collect(trailing), leading)
		std::vector<CommentRange> comments =
			collectCommentRanges([&](const std::function<bool(const CommentRange&)>& yield) {
				getTrailingCommentRanges(sourceFile->Text(), node->end(), yield);
			});
		getLeadingCommentRanges(sourceFile->Text(), node->end(),
								[&](const CommentRange& r) {
									comments.push_back(r);
									return true;
								});
		if (!comments.empty()) {
			if (int realEnd = comments.back().end(); realEnd != 0) {
				return realEnd;
			}
		}
		return node->end();
	}

	if (int multilineEndPosition =
			getEndPositionOfMultilineTrailingComment(sourceFile, node, trailingOpt);
		multilineEndPosition != 0) {
		return multilineEndPosition;
	}

	SkipTriviaOptions triviaOpts;
	triviaOpts.stopAfterLineBreak = true;
	int newEnd = skipTriviaEx(sourceFile->Text(), node->end(), triviaOpts);

	if (newEnd != node->end() &&
		(trailingOpt == TrailingTriviaOptionInclude ||
		 isLineBreak(static_cast<unsigned char>(sourceFile->Text()[newEnd - 1])))) {
		return newEnd;
	}
	return node->end();
}

// getInsertionPositionAtSourceFileTop — trackerimpl.go:470
int Tracker::getInsertionPositionAtSourceFileTop(SourceFile* sourceFile) {
	Node* lastPrologue = nullptr;
	for (Node* node : sourceFile->Statements->nodes) {
		if (isPrologueDirective(node)) {
			lastPrologue = node;
		} else {
			break;
		}
	}

	int position = 0;
	const std::string& text = sourceFile->Text();
	auto advancePastLineBreak = [&]() {
		if (position >= static_cast<int>(text.size())) {
			return;
		}
		char32_t char_ = static_cast<unsigned char>(text[position]);
		if (isLineBreak(char_)) {
			position++;
			if (position < static_cast<int>(text.size()) && char_ == U'\r' &&
				text[position] == '\n') {
				position++;
			}
		}
	};
	if (lastPrologue != nullptr) {
		position = lastPrologue->end();
		advancePastLineBreak();
		return position;
	}

	std::string_view shebang = getShebang(text);
	if (!shebang.empty()) {
		position = static_cast<int>(shebang.size());
		advancePastLineBreak();
	}

	std::vector<CommentRange> ranges =
		collectCommentRanges([&](const std::function<bool(const CommentRange&)>& yield) {
			getLeadingCommentRanges(text, position, yield);
		});
	if (ranges.empty()) {
		return position;
	}
	// Find the first attached comment to the first node and add before it
	const CommentRange* lastComment = nullptr;
	bool pinnedOrTripleSlash = false;
	int firstNodeLine = -1;

	int lenStatements = static_cast<int>(sourceFile->Statements->nodes.size());
	const std::vector<TextPos>& lineMap = sourceFile->ecmaLineMap();
	for (const CommentRange& r : ranges) {
		if (r.kind == Kind::MultiLineCommentTrivia) {
			if (printer::IsPinnedComment(text, r)) {
				lastComment = &r;
				pinnedOrTripleSlash = true;
				continue;
			}
		} else if (printer::IsRecognizedTripleSlashComment(text, r)) {
			lastComment = &r;
			pinnedOrTripleSlash = true;
			continue;
		}

		if (lastComment != nullptr) {
			// Always insert after pinned or triple slash comments
			if (pinnedOrTripleSlash) {
				break;
			}

			// There was a blank line between the last comment and this
			// comment. This comment is not part of the copyright comments
			int commentLine = computeLineOfPosition(lineMap, r.pos());
			int lastCommentEndLine = computeLineOfPosition(lineMap, lastComment->end());
			if (commentLine >= lastCommentEndLine + 2) {
				break;
			}
		}

		if (lenStatements > 0) {
			if (firstNodeLine == -1) {
				firstNodeLine = computeLineOfPosition(
					lineMap,
					astnav::getStartOfNode(sourceFile->Statements->nodes[0], sourceFile, false));
			}
			int commentEndLine = computeLineOfPosition(lineMap, r.end());
			if (firstNodeLine < commentEndLine + 2) {
				break;
			}
		}
		lastComment = &r;
		pinnedOrTripleSlash = false;
	}

	if (lastComment != nullptr) {
		position = lastComment->end();
		advancePastLineBreak();
	}
	return position;
}

} // namespace tsc::ls::change
