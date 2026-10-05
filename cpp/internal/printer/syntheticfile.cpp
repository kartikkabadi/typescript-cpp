// syntheticfile.go — port of tsc/internal/printer/syntheticfile.go
#include "internal/printer/printer.h"
#include "internal/printer/emitcontext.h"

namespace tsc::printer {

// PrintAndPositionNode — syntheticfile.go:15
std::pair<std::string, Node*> PrintAndPositionNode(
	tsc::NodeFactory* factory, Node* node, SourceFile* sourceFile,
	const std::string& newLine, int indentSize, EmitContext* emitContext) {
	// dep — NewChangeTrackerWriter lives in the changetrackerwriter slice
	// (explicitly out of scope for this port).
	TSC_UNREACHABLE("PrintAndPositionNode — changetrackerwriter slice");
	(void)factory;
	(void)node;
	(void)sourceFile;
	(void)newLine;
	(void)indentSize;
	(void)emitContext;
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
