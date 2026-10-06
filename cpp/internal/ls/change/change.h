// === slice: change ===
// tracker.go — minimal change.Tracker declarations for the ls slice port.
// The sibling child package (change slice) owns the real implementation;
// all method bodies here are dep-stubs.
#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/ls/lsutil/lsutil.h"

namespace tsc {
struct SourceFile;
struct Node;
struct NodeFactory;
struct NodeList;
using Statement = Node;
enum class Kind : int16_t;
namespace printer {
struct EmitContext;
}
namespace lsconv {
struct Converters;
}
namespace change {

// NodeOptions — tracker.go:21.
struct NodeOptions {
	// Text to be inserted before the new node
	std::string Prefix;
	// Text to be inserted after the new node
	std::string Suffix;
	int* indentation = nullptr;
	int* delta = nullptr;
	// LeadingTriviaOption / TrailingTriviaOption (Go-embedded fields).
	int leadingTriviaOption = 0;
	int trailingTriviaOption = 0;
	std::string joiner;
};

// LeadingTriviaOption — tracker.go:39.
using LeadingTriviaOption = int;
inline constexpr LeadingTriviaOption LeadingTriviaOptionNone = 0;
inline constexpr LeadingTriviaOption LeadingTriviaOptionExclude = 1;
inline constexpr LeadingTriviaOption LeadingTriviaOptionIncludeAll = 2;
inline constexpr LeadingTriviaOption LeadingTriviaOptionJSDoc = 3;
inline constexpr LeadingTriviaOption LeadingTriviaOptionStartLine = 4;

// TrailingTriviaOption — tracker.go:49.
using TrailingTriviaOption = int;
inline constexpr TrailingTriviaOption TrailingTriviaOptionNone = 0;
inline constexpr TrailingTriviaOption TrailingTriviaOptionExclude = 1;
inline constexpr TrailingTriviaOption TrailingTriviaOptionExcludeWhitespace = 2;
inline constexpr TrailingTriviaOption TrailingTriviaOptionInclude = 3;

// Tracker — tracker.go. Method bodies dep-stubbed (owned by change slice).
struct Tracker {
	// Go-embedded fields accessed by callers:
	//   tracker.EmitContext.SetEmitFlags(...) -> tracker->EmitContext->setEmitFlags
	//   tracker.NodeFactory.NewX(...)        -> tracker->NodeFactory->newX
	printer::EmitContext* EmitContext = nullptr;
	tsc::NodeFactory* NodeFactory = nullptr;

	std::pair<std::unordered_map<std::string, std::vector<lsproto::TextEdit*>>,
	          std::vector<std::string>>
	GetChanges();
	void ReplaceNode(SourceFile* sourceFile, Node* oldNode,
	                 Node* newNode, NodeOptions* options);
	void ReplaceNodeWithNodes(SourceFile* sourceFile, Node* oldNode,
	                          std::vector<Node*> newNodes,
	                          NodeOptions* options);
	void ReplaceRangeWithText(SourceFile* sourceFile,
	                          lsproto::Range lsprotoRange,
	                          const std::string& text);
	void ReplaceTextRangeWithText(SourceFile* sourceFile,
	                              TextRange textRange, const std::string& text);
	void ReplaceRange(SourceFile* sourceFile, TextRange textRange,
	                  Node* newNode, NodeOptions options);
	void ReplaceRangeWithNodes(SourceFile* sourceFile, TextRange textRange,
	                           std::vector<Node*> newNodes,
	                           NodeOptions options);
	void InsertText(SourceFile* sourceFile, lsproto::Position pos,
	                const std::string& text);
	void InsertNodeAt(SourceFile* sourceFile, TextPos pos,
	                  Node* newNode, NodeOptions options);
	void InsertNodesAt(SourceFile* sourceFile, TextPos pos,
	                   std::vector<Node*> newNodes, NodeOptions options);
	void InsertNodeAfter(SourceFile* sourceFile, Node* after,
	                     Node* newNode);
	void InsertNodesAfter(SourceFile* sourceFile, Node* after,
	                      std::vector<Node*> newNodes);
	void InsertNodeBefore(SourceFile* sourceFile, Node* before,
	                      Node* newNode, bool blankLineBetween,
	                      LeadingTriviaOption leadingTriviaOption);
	bool TryInsertTypeAnnotation(SourceFile* sourceFile, Node* node,
	                             Node* typeNode);
	void ParenthesizeArrowParameters(SourceFile* sourceFile,
	                                 Node* arrowFunc);
	void InsertModifierBefore(SourceFile* sourceFile, Kind modifier,
	                          Node* before);
	void Delete(SourceFile* sourceFile, Node* node);
	void DeleteRange(SourceFile* sourceFile, TextRange textRange);
	void DeleteNode(SourceFile* sourceFile, Node* node,
	                LeadingTriviaOption leadingTrivia,
	                TrailingTriviaOption trailingTrivia);
	void DeleteNodeRange(SourceFile* sourceFile, Node* startNode,
	                     Node* endNode, LeadingTriviaOption leadingTrivia,
	                     TrailingTriviaOption trailingTrivia);
	void InsertNodeInListAfter(SourceFile* sourceFile, Node* after,
	                           Node* newNode, NodeList* containingList);
	void InsertImportSpecifierAtIndex(SourceFile* sourceFile,
	                                  Node* newSpecifier,
	                                  Node* namedImports, int index);
	void InsertAtTopOfFile(SourceFile* sourceFile,
	                       std::vector<Statement*> insert,
	                       bool blankLineBetween);
	void InsertMemberAtStart(SourceFile* sourceFile, Node* node,
	                         Node* newElement);
	TextRange GetAdjustedRange(SourceFile* sourceFile,
	                           Node* startNode, Node* endNode,
	                           LeadingTriviaOption leadingOption,
	                           TrailingTriviaOption trailingOption);
};

// NewTracker — tracker.go:112.
Tracker* NewTracker(const gostd::Context& ctx,
                    const CompilerOptions* compilerOptions,
                    lsutil::FormatCodeSettings formatOptions,
                    lsconv::Converters* converters);

} // namespace change
} // namespace tsc
