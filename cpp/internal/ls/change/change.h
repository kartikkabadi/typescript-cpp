// === slice: ls-foundation ===
// ls/change — the ChangeTracker that records text edits during code fixes
// and refactors (tracker.go, trackerimpl.go, delete.go).
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/core/text.h"
#include "internal/format/format.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/spanmap/spanmap.h"

namespace tsc::printer {
struct EmitContext;
class ChangeTrackerWriter;
}
namespace tsc {
struct CompilerOptions;
struct NodeFactory;
}

namespace tsc::lsconv {

// === dep decls/stubs — owned by lsp slice (tsc/internal/ls/lsconv) ===
// Minimal lsconv surface used by change.Tracker. The lsp slice owns the real
// package; these declarations are replaced when it lands.

// MappedSpan/MappedPosition — converters.go:30/35. Canonical decls live
// in lsconv/lsconv.h (included above).

// converters.go:25 — the converter registry. A concrete dep-stub: the real
// implementation lands with the lsp slice; every method is unreachable.
struct Converters {
	std::pair<tsc::lsp::lsproto::Range, tsc::spanmap::Fidelity> ToLSPRange(
		SourceFile* script, tsc::TextRange textRange) {
		TSC_UNREACHABLE("Converters::ToLSPRange — owned by lsp slice");
	}
	std::vector<MappedSpan<SourceFile>> FromLSPRangeForSourceFile(
		SourceFile* file, tsc::lsp::lsproto::Range textRange,
		tsc::spanmap::Feature feature) {
		TSC_UNREACHABLE("Converters::FromLSPRangeForSourceFile — owned by lsp slice");
	}
	std::vector<MappedPosition<SourceFile>> FromLSPPositionForSourceFile(
		SourceFile* file, tsc::lsp::lsproto::Position position,
		tsc::spanmap::Feature feature) {
		TSC_UNREACHABLE("Converters::FromLSPPositionForSourceFile — owned by lsp slice");
	}
};

// === end dep decls/stubs ===

} // namespace tsc::lsconv

namespace tsc::ls::change {

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

// tracker.go:20 — NodeOptions. Go embeds LeadingTriviaOption and
// TrailingTriviaOption directly in the struct; C++ keeps them as fields
// (accessed as options.leadingTrivia / options.trailingTrivia).
struct NodeOptions {
	// Text to be inserted before the new node
	std::string Prefix;

	// Text to be inserted after the new node
	std::string Suffix;

	// Text of inserted node will be formatted with this indentation, otherwise
	// indentation will be inferred from the old node
	std::optional<int> indentation;

	// Text of inserted node will be formatted with this delta, otherwise delta
	// will be inferred from the new node kind
	std::optional<int> delta;

	LeadingTriviaOption leadingTrivia = LeadingTriviaOptionNone;
	TrailingTriviaOption trailingTrivia = TrailingTriviaOptionNone;
	std::string joiner;
};

// tracker.go:60
using trackerEditKind = int;
inline constexpr trackerEditKind trackerEditKindText = 1;
inline constexpr trackerEditKind trackerEditKindRemove = 2;
inline constexpr trackerEditKind trackerEditKindReplaceWithSingleNode = 3;
inline constexpr trackerEditKind trackerEditKindReplaceWithMultipleNodes = 4;

// tracker.go:69
struct trackerEdit {
	trackerEditKind kind;
	TextRange textRange;

	std::string NewText; // kind == text

	Node* node = nullptr;              // single
	std::vector<Node*> nodes;          // multiple
	NodeOptions options;
};

struct nodesInsertedAtStartState {
	Node* node = nullptr;
	SourceFile* sourceFile = nullptr;
};

struct deletedNode {
	SourceFile* sourceFile;
	Node* node;
};

// Tracker — tracker.go:80
struct Tracker {
	// initialized with
	lsutil::FormatCodeSettings formatSettings;
	std::string newLine;
	lsconv::Converters* converters = nullptr;
	format::FormatRequestContext ctx{};
	printer::EmitContext* emitContext = nullptr;

	NodeFactory* nodeFactory = nullptr;

	collections::MultiMap<SourceFile*, trackerEdit*> changes;
	std::vector<deletedNode> deletedNodes;
	std::unordered_map<Node*, nodesInsertedAtStartState*> nodesWithInsertionsAtStart;

	// unmappableFiles collects the files for which an edit could not be
	// represented within a single verbatim span of the original text.
	// GetChanges drops their edits so a partial, corrupting change is never
	// emitted for a content-mapped file.
	collections::Set<std::string> unmappableFiles;

	// created during call to getChanges
	printer::ChangeTrackerWriter* writer = nullptr;

	// tracker.go — NewTracker is a constructor in the port (the EmitContext is
	// owned by the Tracker).
	Tracker(const format::FormatRequestContext& ctx, const CompilerOptions* compilerOptions,
			const lsutil::FormatCodeSettings& formatOptions, lsconv::Converters* converters);
	~Tracker();

	Tracker(const Tracker&) = delete;
	Tracker& operator=(const Tracker&) = delete;

	// tracker.go:136
	std::pair<std::map<std::string, std::vector<lsp::lsproto::TextEdit>>, std::vector<std::string>>
	GetChanges();

	// tracker.go:152
	TextRange fromLSPEditRange(SourceFile* sourceFile, lsp::lsproto::Range lsprotoRange);

	// tracker.go:164
	lsp::lsproto::Range toLSPEditRange(SourceFile* sourceFile, TextRange textRange);

	// tracker.go:176
	void ReplaceNode(SourceFile* sourceFile, Node* oldNode, Node* newNode, NodeOptions* options);

	// tracker.go:186
	void ReplaceNodeWithNodes(SourceFile* sourceFile, Node* oldNode, std::vector<Node*> newNodes,
							  NodeOptions* options);

	// tracker.go:197
	void ReplaceRange(SourceFile* sourceFile, TextRange textRange, Node* newNode, NodeOptions options);

	// tracker.go:202
	void ReplaceRangeWithText(SourceFile* sourceFile, lsp::lsproto::Range lsprotoRange,
							  const std::string& text);

	// tracker.go:207
	void ReplaceTextRangeWithText(SourceFile* sourceFile, TextRange textRange, const std::string& text);

	// tracker.go:212
	void ReplaceRangeWithNodes(SourceFile* sourceFile, TextRange textRange,
							   std::vector<Node*> newNodes, NodeOptions options);

	// tracker.go:221
	void insertTextAt(SourceFile* sourceFile, TextPos pos, const std::string& text);

	// tracker.go:226
	void InsertText(SourceFile* sourceFile, lsp::lsproto::Position pos, const std::string& text);

	// tracker.go:230
	void InsertNodeAt(SourceFile* sourceFile, TextPos pos, Node* newNode, NodeOptions options);

	// tracker.go:234
	void InsertNodesAt(SourceFile* sourceFile, TextPos pos, std::vector<Node*> newNodes,
					   NodeOptions options);

	// tracker.go:238
	void InsertNodeAfter(SourceFile* sourceFile, Node* after, Node* newNode);

	// tracker.go:243
	void InsertNodesAfter(SourceFile* sourceFile, Node* after, std::vector<Node*> newNodes);

	// tracker.go:248
	void InsertNodeBefore(SourceFile* sourceFile, Node* before, Node* newNode, bool blankLineBetween,
						  LeadingTriviaOption leadingTriviaOption);

	// tracker.go:253
	bool TryInsertTypeAnnotation(SourceFile* sourceFile, Node* node, Node* typeNode);

	// tracker.go:279
	void ParenthesizeArrowParameters(SourceFile* sourceFile, Node* arrowFunc);

	// tracker.go:293
	void InsertModifierBefore(SourceFile* sourceFile, Kind modifier, Node* before);

	// tracker.go:303
	void Delete(SourceFile* sourceFile, Node* node);

	// tracker.go:308
	void DeleteRange(SourceFile* sourceFile, TextRange textRange);

	// tracker.go:314
	void DeleteNode(SourceFile* sourceFile, Node* node, LeadingTriviaOption leadingTrivia,
					TrailingTriviaOption trailingTrivia);

	// tracker.go:320
	void DeleteNodeRange(SourceFile* sourceFile, Node* startNode, Node* endNode,
						 LeadingTriviaOption leadingTrivia, TrailingTriviaOption trailingTrivia);

	// tracker.go:327
	void finishDeleteDeclarations();

	// tracker.go:367
	TextPos endPosForInsertNodeAfter(SourceFile* sourceFile, Node* after, Node* newNode);

	// tracker.go:418
	void InsertNodeInListAfter(SourceFile* sourceFile, Node* after, Node* newNode,
							   NodeList* containingList);

	// tracker.go:513
	void InsertImportSpecifierAtIndex(SourceFile* sourceFile, Node* newSpecifier,
									  Node* namedImports, int index);

	// tracker.go:533
	void InsertAtTopOfFile(SourceFile* sourceFile, std::vector<Node*> insert, bool blankLineBetween);

	// tracker.go:570
	void InsertMemberAtStart(SourceFile* sourceFile, Node* node, Node* newElement);

	// tracker.go:574
	void insertNodeAtStartWorker(SourceFile* sourceFile, Node* node, Node* newElement);

	// tracker.go:588
	int tryComputeIndentationForNewMember(SourceFile* sourceFile, Node* node);

	// tracker.go:603
	int tryComputeIndentationFromExistingMembers(SourceFile* sourceFile, Node* node);

	// tracker.go:641
	NodeOptions getInsertNodeAfterOptions(SourceFile* sourceFile, Node* node);

	// tracker.go:677
	NodeOptions getOptionsForInsertNodeBefore(Node* before, Node* inserted, bool blankLineBetween);

	// tracker.go:705
	NodeOptions getInsertNodeAtStartInsertOptions(SourceFile* sourceFile, Node* node, int indentation);

	// tracker.go:738
	void finishNodesWithInsertionsAtStart();

	// trackerimpl.go:20
	std::map<std::string, std::vector<lsp::lsproto::TextEdit>> getTextChangesFromChanges();

	// trackerimpl.go:113
	std::string computeNewText(trackerEdit* change, SourceFile* targetSourceFile,
							   SourceFile* sourceFile);

	// trackerimpl.go:183
	std::string reindentInsertedLines(SourceFile* sourceFile, trackerEdit* change,
									  const std::string& text);

	// trackerimpl.go:213
	std::string getFormattedTextOfNode(Node* nodeIn, SourceFile* targetSourceFile,
									   SourceFile* sourceFile, int pos, const NodeOptions& options);

	// trackerimpl.go:245
	std::pair<std::string, Node*> getNonformattedText(Node* node, SourceFile* sourceFile);

	// trackerimpl.go:256 — method on the changeTracker because use of converters
	TextRange GetAdjustedRange(SourceFile* sourceFile, Node* startNode, Node* endNode,
							   LeadingTriviaOption leadingOption, TrailingTriviaOption trailingOption);

	// trackerimpl.go:265
	int getAdjustedStartPosition(SourceFile* sourceFile, Node* node, LeadingTriviaOption leadingOption,
							   bool hasTrailingComment);

	// trackerimpl.go:332
	int getEndPositionOfMultilineTrailingComment(SourceFile* sourceFile, Node* node,
												 TrailingTriviaOption trailingOpt);

	// trackerimpl.go:354
	int getAdjustedEndPosition(SourceFile* sourceFile, Node* node, TrailingTriviaOption trailingOpt);

	// trackerimpl.go:470
	int getInsertionPositionAtSourceFileTop(SourceFile* sourceFile);

	// delete.go:216
	int startPositionToDeleteNodeInList(SourceFile* sourceFile, Node* node);

	// delete.go:221
	int endPositionToDeleteNodeInList(SourceFile* sourceFile, Node* node, Node* prevNode,
									  Node* nextNode);
};

// trackerimpl.go:239
lsutil::FormatCodeSettings GetFormatCodeSettingsForWriting(lsutil::FormatCodeSettings options,
														 SourceFile* sourceFile);

// === package-internal helpers shared between tracker.cpp and delete.cpp ===

// delete.go:14
void deleteDeclaration(Tracker* t, std::unordered_map<Node*, bool>& deletedNodesInLists,
					   SourceFile* sourceFile, Node* node);

} // namespace tsc::ls::change
