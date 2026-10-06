// change — dep-decl for the ls-autoimport slice: ls/change's Tracker
// (tracker.go) is owned by the ls/change slice; only the type shape and the
// ctor (newTracker — needed to construct a Tracker for Fix::Edits) are real.
// All edit/query methods are dep-stubs.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/kind.h"
#include "internal/collections/collections.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/format/format.h"    // format::FormatRequestContext
#include "internal/gostd/gostd.h"     // gostd::Context
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h" // lsutil::FormatCodeSettings
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/printer/emitcontext.h"

namespace tsc::change {

// === dep decls for ls-autoimport — owned by ls/change ===

using LeadingTriviaOption = int;
inline constexpr LeadingTriviaOption LeadingTriviaOptionNone = 0;
inline constexpr LeadingTriviaOption LeadingTriviaOptionExclude = 1;
inline constexpr LeadingTriviaOption LeadingTriviaOptionIncludeAll = 2;
inline constexpr LeadingTriviaOption LeadingTriviaOptionJSDoc = 3;
inline constexpr LeadingTriviaOption LeadingTriviaOptionStartLine = 4;

using TrailingTriviaOption = int;
inline constexpr TrailingTriviaOption TrailingTriviaOptionNone = 0;
inline constexpr TrailingTriviaOption TrailingTriviaOptionExclude = 1;
inline constexpr TrailingTriviaOption TrailingTriviaOptionExcludeWhitespace = 2;
inline constexpr TrailingTriviaOption TrailingTriviaOptionInclude = 3;

// NodeOptions — tracker.go:21. (unexported Go fields kept lowercase)
struct NodeOptions {
	// Text to be inserted before the new node
	std::string Prefix;
	// Text to be inserted after the new node
	std::string Suffix;
	// Text of inserted node will be formatted with this indentation,
	// otherwise indentation will be inferred from the old node
	int* indentation = nullptr;
	// Text of inserted node will be formatted with this delta, otherwise
	// delta will be inferred from the new node kind
	int* delta = nullptr;
	// Go embeds LeadingTriviaOption/TrailingTriviaOption anonymously.
	::tsc::change::LeadingTriviaOption leadingTriviaOption{};
	::tsc::change::TrailingTriviaOption trailingTriviaOption{};
	std::string joiner;
};

struct trackerEdit; // owned by ls/change

// Tracker — tracker.go:67.
struct Tracker {
	// initialized with
	lsutil::FormatCodeSettings formatSettings;
	std::string newLine;
	lsconv::Converters* converters = nullptr;
	gostd::Context ctx;
	format::FormatRequestContext formatCtx;
	printer::EmitContext* emitContext = nullptr; // owned

	// Go embeds *ast.NodeFactory; ct.NodeFactory.NewX → ct->nodeFactory->newX.
	NodeFactory* nodeFactory = nullptr; // == &emitContext->factory (upcast)

	collections::MultiMap<SourceFile*, trackerEdit*> changes;
	struct deletedNode {
		SourceFile* sourceFile = nullptr;
		Node* node = nullptr;
	};
	std::vector<deletedNode> deletedNodes;
	struct nodesInsertedAtStartState {
		Node* node = nullptr;
		SourceFile* sourceFile = nullptr;
	};
	std::unordered_map<Node*, nodesInsertedAtStartState*>
		nodesWithInsertionsAtStart;
	collections::Set<std::string> unmappableFiles;

	// NodeFactory — Go's embedded-factory deref (`ct.AsNodeFactory()`).
	NodeFactory* AsNodeFactory() { return nodeFactory; }

	// GetChanges — tracker.go:134. dep-stub.
	std::pair<std::unordered_map<std::string, std::vector<lsproto::TextEdit*>>,
	          std::vector<std::string>>
	GetChanges();

	// --- edit/query methods — dep-stubs (owned by ls/change) ---
	void ReplaceNode(SourceFile* sourceFile, Node* oldNode, Node* newNode,
	                 NodeOptions* options = nullptr);
	void ReplaceNodeWithNodes(SourceFile* sourceFile, Node* oldNode,
	                          const std::vector<Node*>& newNodes,
	                          NodeOptions* options = nullptr);
	void ReplaceRange(SourceFile* sourceFile, TextRange textRange,
	                  Node* newNode, const NodeOptions& options);
	void ReplaceRangeWithText(SourceFile* sourceFile,
	                          const lsproto::Range& lsprotoRange,
	                          std::string text);
	void ReplaceTextRangeWithText(SourceFile* sourceFile, TextRange textRange,
	                              std::string text);
	void ReplaceRangeWithNodes(SourceFile* sourceFile, TextRange textRange,
	                           const std::vector<Node*>& newNodes,
	                           const NodeOptions& options);
	void InsertText(SourceFile* sourceFile, const lsproto::Position& pos,
	                std::string text);
	void InsertNodeAt(SourceFile* sourceFile, TextPos pos, Node* newNode,
	                  const NodeOptions& options);
	void InsertNodesAt(SourceFile* sourceFile, TextPos pos,
	                   const std::vector<Node*>& newNodes,
	                   const NodeOptions& options);
	void InsertNodeAfter(SourceFile* sourceFile, Node* after, Node* newNode);
	void InsertNodesAfter(SourceFile* sourceFile, Node* after,
	                      const std::vector<Node*>& newNodes);
	void InsertNodeBefore(SourceFile* sourceFile, Node* before, Node* newNode,
	                      bool blankLineBetween,
	                      LeadingTriviaOption leadingTriviaOption);
	bool TryInsertTypeAnnotation(SourceFile* sourceFile, Node* node,
	                             Node* typeNode);
	void ParenthesizeArrowParameters(SourceFile* sourceFile, Node* arrowFunc);
	void InsertModifierBefore(SourceFile* sourceFile, Kind modifier,
	                          Node* before);
	void Delete(SourceFile* sourceFile, Node* node);
	void DeleteRange(SourceFile* sourceFile, TextRange textRange);
	void DeleteNode(SourceFile* sourceFile, Node* node,
	                LeadingTriviaOption leadingTrivia,
	                TrailingTriviaOption trailingTrivia);
	void DeleteNodeRange(SourceFile* sourceFile, Node* startNode, Node* endNode,
	                     LeadingTriviaOption leadingTrivia,
	                     TrailingTriviaOption trailingTrivia);
	void InsertNodeInListAfter(SourceFile* sourceFile, Node* after,
	                           Node* newNode, NodeList* containingList);
	void InsertImportSpecifierAtIndex(SourceFile* sourceFile,
	                                  Node* newSpecifier, Node* namedImports,
	                                  int index);
	void InsertAtTopOfFile(SourceFile* sourceFile,
	                       const std::vector<Node*>& insert,
	                       bool blankLineBetween);
	void InsertMemberAtStart(SourceFile* sourceFile, Node* node,
	                         Node* newElement);
};

// NewTracker — tracker.go:112.
Tracker* newTracker(gostd::Context ctx, const CompilerOptions* compilerOptions,
                    const lsutil::FormatCodeSettings& formatOptions,
                    lsconv::Converters* converters);

} // namespace tsc::change
