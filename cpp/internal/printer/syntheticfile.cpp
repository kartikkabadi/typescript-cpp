// syntheticfile.go — port of tsc/internal/printer/syntheticfile.go
#include "internal/printer/printer.h"
#include "internal/printer/emitcontext.h"

namespace tsc::printer {

// core/compileroptions.go:490 GetNewLineKind — file-local replica
// (same helper as in ls/completions.cpp).
static NewLineKind syntheticGetNewLineKind(std::string_view s) {
	if (s == "\r\n") {
		return NewLineKind::CarriageReturnLineFeed;
	}
	if (s == "\n") {
		return NewLineKind::LineFeed;
	}
	return NewLineKind::None;
}

// PrintAndPositionNode — syntheticfile.go:15
std::pair<std::string, Node*> PrintAndPositionNode(
	tsc::NodeFactory* factory, Node* node, SourceFile* sourceFile,
	const std::string& newLine, int indentSize, EmitContext* emitContext) {
	ChangeTrackerWriter* writer =
		NewChangeTrackerWriter(newLine, indentSize);
	PrinterOptions options{};
	options.NewLine = syntheticGetNewLineKind(newLine);
	options.NeverAsciiEscape = true;
	options.PreserveSourceNewlines = true;
	options.TerminateUnterminatedLiterals = true;
	NewPrinter(options, writer->GetPrintHandlers(), emitContext)
		->Write(node, sourceFile, writer, nullptr);

	std::string text = writer->String();
	if (text.size() >= newLine.size() &&
	    text.compare(text.size() - newLine.size(), newLine.size(), newLine) == 0) {
		text.resize(text.size() - newLine.size());
	}
	Node* positioned = writer->AssignPositionsToNode(node, factory);
	return {text, positioned};
}

// CreateSyntheticSourceFile — syntheticfile.go:35
SourceFile* CreateSyntheticSourceFile(tsc::NodeFactory* factory, Node* node,
                                      const std::string& text,
                                      SourceFileParseOptions parseOptions) {
	Node* eof = factory->newToken(Kind::EndOfFile);
	eof->loc = TextRange{TextPos(text.size()), TextPos(text.size())};
	NodeList* statements = factory->newNodeList({node});
	statements->loc = TextRange{node->loc.pos(), node->loc.end()};
	Node* syntheticFile = factory->newSourceFile(
		parseOptions,
		std::string(text),
		statements,
		eof);
	syntheticFile->loc = TextRange{TextPos(0), TextPos(text.size())};
	setParentInChildren(syntheticFile);
	return syntheticFile->as<SourceFile>();
}

}  // namespace tsc::printer
