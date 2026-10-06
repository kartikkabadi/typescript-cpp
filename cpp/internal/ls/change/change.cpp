// change — dep-stubs for the ls-autoimport slice. newTracker is real (the
// ctor's observable behavior — EmitContext + NodeFactory wiring — is needed
// by autoimport's Fix::Edits even while edit methods panic).
#include "internal/ls/change/change.h"

namespace tsc::change {

namespace {
// NewLineKind.GetNewLineCharacter (core/compileroptions.go:501) — file-local
// (printer.cpp has the same private helper; keep this TU self-contained).
std::string getNewLineCharacter(NewLineKind newLine) {
	switch (newLine) {
	case NewLineKind::CarriageReturnLineFeed:
		return "\r\n";
	case NewLineKind::LineFeed:
		return "\n";
	default:
		return "\n";
	}
}
} // namespace

// NewTracker — tracker.go:112.
Tracker* newTracker(gostd::Context ctx, const CompilerOptions* compilerOptions,
                    const lsutil::FormatCodeSettings& formatOptions,
                    lsconv::Converters* converters) {
	printer::EmitContext* emitContext = printer::NewEmitContext();
	std::string newLine = getNewLineCharacter(compilerOptions->NewLine);
	auto* t = new Tracker();
	t->emitContext = emitContext;
	t->nodeFactory = &emitContext->factory;
	t->ctx = ctx;
	t->formatCtx = format::WithFormatCodeSettings(
		format::FormatRequestContext{}, formatOptions, newLine);
	// !!! formatSettings in context? (Go comment)
	t->formatSettings = formatOptions;
	t->newLine = std::move(newLine);
	t->converters = converters;
	return t;
}

// === dep stubs — removed when owner slice lands ===

std::pair<std::unordered_map<std::string, std::vector<lsproto::TextEdit*>>,
          std::vector<std::string>>
Tracker::GetChanges() {
	TSC_UNREACHABLE("Tracker::GetChanges — owned by ls/change");
}
void Tracker::ReplaceNode(SourceFile*, Node*, Node*, NodeOptions*) {
	TSC_UNREACHABLE("Tracker::ReplaceNode — owned by ls/change");
}
void Tracker::ReplaceNodeWithNodes(SourceFile*, Node*,
                                   const std::vector<Node*>&, NodeOptions*) {
	TSC_UNREACHABLE("Tracker::ReplaceNodeWithNodes — owned by ls/change");
}
void Tracker::ReplaceRange(SourceFile*, TextRange, Node*,
                           const NodeOptions&) {
	TSC_UNREACHABLE("Tracker::ReplaceRange — owned by ls/change");
}
void Tracker::ReplaceRangeWithText(SourceFile*, const lsproto::Range&,
                                   std::string) {
	TSC_UNREACHABLE("Tracker::ReplaceRangeWithText — owned by ls/change");
}
void Tracker::ReplaceTextRangeWithText(SourceFile*, TextRange, std::string) {
	TSC_UNREACHABLE("Tracker::ReplaceTextRangeWithText — owned by ls/change");
}
void Tracker::ReplaceRangeWithNodes(SourceFile*, TextRange,
                                    const std::vector<Node*>&,
                                    const NodeOptions&) {
	TSC_UNREACHABLE("Tracker::ReplaceRangeWithNodes — owned by ls/change");
}
void Tracker::InsertText(SourceFile*, const lsproto::Position&, std::string) {
	TSC_UNREACHABLE("Tracker::InsertText — owned by ls/change");
}
void Tracker::InsertNodeAt(SourceFile*, TextPos, Node*, const NodeOptions&) {
	TSC_UNREACHABLE("Tracker::InsertNodeAt — owned by ls/change");
}
void Tracker::InsertNodesAt(SourceFile*, TextPos, const std::vector<Node*>&,
                            const NodeOptions&) {
	TSC_UNREACHABLE("Tracker::InsertNodesAt — owned by ls/change");
}
void Tracker::InsertNodeAfter(SourceFile*, Node*, Node*) {
	TSC_UNREACHABLE("Tracker::InsertNodeAfter — owned by ls/change");
}
void Tracker::InsertNodesAfter(SourceFile*, Node*, const std::vector<Node*>&) {
	TSC_UNREACHABLE("Tracker::InsertNodesAfter — owned by ls/change");
}
void Tracker::InsertNodeBefore(SourceFile*, Node*, Node*, bool,
                               LeadingTriviaOption) {
	TSC_UNREACHABLE("Tracker::InsertNodeBefore — owned by ls/change");
}
bool Tracker::TryInsertTypeAnnotation(SourceFile*, Node*, Node*) {
	TSC_UNREACHABLE("Tracker::TryInsertTypeAnnotation — owned by ls/change");
}
void Tracker::ParenthesizeArrowParameters(SourceFile*, Node*) {
	TSC_UNREACHABLE("Tracker::ParenthesizeArrowParameters — owned by ls/change");
}
void Tracker::InsertModifierBefore(SourceFile*, Kind, Node*) {
	TSC_UNREACHABLE("Tracker::InsertModifierBefore — owned by ls/change");
}
void Tracker::Delete(SourceFile*, Node*) {
	TSC_UNREACHABLE("Tracker::Delete — owned by ls/change");
}
void Tracker::DeleteRange(SourceFile*, TextRange) {
	TSC_UNREACHABLE("Tracker::DeleteRange — owned by ls/change");
}
void Tracker::DeleteNode(SourceFile*, Node*, LeadingTriviaOption,
                         TrailingTriviaOption) {
	TSC_UNREACHABLE("Tracker::DeleteNode — owned by ls/change");
}
void Tracker::DeleteNodeRange(SourceFile*, Node*, Node*, LeadingTriviaOption,
                              TrailingTriviaOption) {
	TSC_UNREACHABLE("Tracker::DeleteNodeRange — owned by ls/change");
}
void Tracker::InsertNodeInListAfter(SourceFile*, Node*, Node*, NodeList*) {
	TSC_UNREACHABLE("Tracker::InsertNodeInListAfter — owned by ls/change");
}
void Tracker::InsertImportSpecifierAtIndex(SourceFile*, Node*, Node*, int) {
	TSC_UNREACHABLE(
		"Tracker::InsertImportSpecifierAtIndex — owned by ls/change");
}
void Tracker::InsertAtTopOfFile(SourceFile*, const std::vector<Node*>&, bool) {
	TSC_UNREACHABLE("Tracker::InsertAtTopOfFile — owned by ls/change");
}
void Tracker::InsertMemberAtStart(SourceFile*, Node*, Node*) {
	TSC_UNREACHABLE("Tracker::InsertMemberAtStart — owned by ls/change");
}

} // namespace tsc::change
