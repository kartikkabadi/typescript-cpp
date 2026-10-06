// === slice: fourslash ===
// fourslash.h — package-level declarations for tsc/internal/fourslash:
// fourslash.go, test_parser.go, baselineutil.go, statebaseline.go,
// semantictokens.go. Bodies live in the matching .cpp files; template
// bodies sit at the bottom of this header.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/debug/debug.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/diagnosticwriter/diagnosticwriter.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/fourslash/fourslash_deps.h"
#include "internal/fourslash/goutil.h"
#include "internal/fourslash/test_parser.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/jsonrpc/jsonrpc.h"
#include "internal/locale/locale.h"
#include "internal/ls/ls.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsdeps.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/spanmap/spanmap.h"
#include "internal/stringutil/stringutil.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/tsbaseline/tsbaseline.h"
#include "internal/testutil/fsbaselineutil/differ.h"
#include "internal/testutil/harnessutil/harnessutil.h"
#include "internal/testutil/lsptestutil/lspclient.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::fourslash {

// sliceOf<T>{...} — builds shared_ptr<lsproto::Slice<T>> for generated
// fields of shape `*Slice[T]` (Slice = optional<vector<T>>).
template <typename T>
inline std::shared_ptr<lsproto::Slice<T>> sliceOf(
	std::initializer_list<T> elems) {
	return std::make_shared<lsproto::Slice<T>>(
	    lsproto::Slice<T>(std::vector<T>(elems)));
}

// orNilSlice — Go `*ptr` on `*Slice[T]`: nil *Slice or a JSON `null`
// slice both iterate as empty.
template <typename T>
inline const std::vector<T>& orNilSlice(
	const std::shared_ptr<lsproto::Slice<T>>& s) {
	static const std::vector<T> empty;
	if (s == nullptr || !s->has_value()) {
		return empty;
	}
	return **s;
}

// stringifyJson — core.StringifyJson(v, prefix, indent) → marshalIndent.
template <typename T>
inline std::pair<std::string, std::string> stringifyJson(
	const T& v, const std::string& prefix, const std::string& indent) {
	return json::marshalIndent(v, prefix, indent);
}

// sliceOr — Go `[]T` semantics on a `Slice[T]` (optional<vector<T>>):
// absent/null iterates as empty. For `*Slice[T]` use orNilSlice.
template <typename T>
inline const std::vector<T>& sliceOr(const lsproto::Slice<T>& s) {
	static const std::vector<T> empty;
	return s.has_value() ? *s : empty;
}


// ===========================================================================
// baselineutil.go:23,46 — baseline command names
// ===========================================================================
using baselineCommand = std::string;

inline const baselineCommand autoImportsCmd = "Auto Imports";
inline const baselineCommand callHierarchyCmd = "Call Hierarchy";
inline const baselineCommand closingTagCmd = "Closing Tag";
inline const baselineCommand documentHighlightsCmd = "documentHighlights";
inline const baselineCommand findAllReferencesCmd = "findAllReferences";
inline const baselineCommand vsFindAllReferencesCmd = "vsFindAllReferences";
inline const baselineCommand goToDefinitionCmd = "goToDefinition";
inline const baselineCommand goToImplementationCmd = "goToImplementation";
inline const baselineCommand goToSourceDefinitionCmd = "goToSourceDefinition";
inline const baselineCommand goToTypeDefinitionCmd = "goToType";
inline const baselineCommand inlayHintsCmd = "Inlay Hints";
inline const baselineCommand nonSuggestionDiagnosticsCmd =
    "Syntax and Semantic Diagnostics";
inline const baselineCommand quickInfoCmd = "QuickInfo";
inline const baselineCommand vsQuickInfoCmd = "VSQuickInfo";
inline const baselineCommand linkedEditingCmd = "linkedEditing";
inline const baselineCommand renameCmd = "findRenameLocations";
inline const baselineCommand signatureHelpCmd = "SignatureHelp";
inline const baselineCommand smartSelectionCmd = "Smart Selection";
inline const baselineCommand codeLensesCmd = "Code Lenses";
inline const baselineCommand documentSymbolsCmd = "Document Symbols";

struct FourslashTest;
struct stateBaseline;
struct scriptInfo;
struct testConverters;
struct documentSpan;
struct baselineDetail;
struct baselineFourslashLocationsOptions;
template <typename T>
struct markerAndItem;

// ===========================================================================
// fourslash.go:1111-1156 — completion expectation types
// ===========================================================================
struct CompletionsExpectedList;
struct CompletionsExpectedItems;

// Ignored — fourslash.go:1118 (`type Ignored = struct{}`).
struct Ignored {};

// *EditRange | Ignored — fourslash.go:1121.
using ExpectedCompletionEditRange =
    std::variant<std::monostate, std::shared_ptr<struct EditRange>, Ignored>;

// EditRange — fourslash.go:1123.
struct EditRange {
	std::shared_ptr<RangeMarker> Insert;
	std::shared_ptr<RangeMarker> Replace;
};

// CompletionsExpectedItemDefaults — fourslash.go:1128.
struct CompletionsExpectedItemDefaults {
	std::shared_ptr<std::vector<std::string>> CommitCharacters;
	ExpectedCompletionEditRange EditRange;
};

// *lsproto.CompletionItem | string — fourslash.go:1134.
using CompletionsExpectedItem =
    std::variant<std::string, std::shared_ptr<lsproto::CompletionItem>>;

// CompletionsExpectedList — fourslash.go:1111.
struct CompletionsExpectedList {
	bool IsIncomplete = false;
	std::shared_ptr<CompletionsExpectedItemDefaults> ItemDefaults;
	std::shared_ptr<CompletionsExpectedItems> Items;
	std::shared_ptr<lsutil::UserPreferences> UserPreferences;
};

// CompletionsExpectedItems — fourslash.go:1136.
struct CompletionsExpectedItems {
	std::vector<CompletionsExpectedItem> Includes;
	std::vector<std::string> Excludes;
	std::vector<CompletionsExpectedItem> Exact;
	std::vector<CompletionsExpectedItem> Unsorted;
};

// CompletionsExpectedCodeAction — fourslash.go:1143.
struct CompletionsExpectedCodeAction {
	std::string Name;
	std::string Source;
	std::string Description;
	std::string NewFileContent;
};

// VerifyCompletionsResult — fourslash.go:1150.
struct VerifyCompletionsResult {
	std::function<void(gostd::testing::T*,
	                   const CompletionsExpectedCodeAction*)>
	    AndApplyCodeAction;
	std::function<void(gostd::testing::T*,
	                   const CompletionsExpectedCodeAction*)>
	    AndHasNoCodeAction;
};

// string | *Marker | []string | []*Marker — fourslash.go:1156.
using MarkerInput =
    std::variant<std::monostate, std::string, std::vector<std::string>,
                 std::shared_ptr<Marker>,
                 std::vector<std::shared_ptr<Marker>>>;

// string | *Marker | *RangeMarker — fourslash.go:4748 (`type
// MarkerOrRangeOrName = any`, exercised with just these cases).
using MarkerOrRangeOrName =
    std::variant<std::string, std::shared_ptr<Marker>,
                 std::shared_ptr<RangeMarker>>;

// ===========================================================================
// fourslash.go code-fix / misc option structs
// ===========================================================================

// VerifyCodeFixOptions — fourslash.go:1671.
struct VerifyCodeFixOptions {
	std::string Description;
	std::string NewFileContent;
	std::string NewRangeContent;
	int Index = 0;
	bool ApplyChanges = false;
	std::shared_ptr<lsutil::UserPreferences> UserPreferences;
};

// VerifyCodeFixAllOptions — fourslash.go:1681.
struct VerifyCodeFixAllOptions {
	std::string FixID;
	std::string NewFileContent;
};

// ApplyCodeActionFromCompletionOptions — fourslash.go:2208.
struct ApplyCodeActionFromCompletionOptions {
	std::string Name;
	std::string Source;
	std::shared_ptr<lsproto::AutoImportFix> AutoImportFix;
	std::string Description;
	std::shared_ptr<std::string> NewFileContent;
	std::shared_ptr<std::string> NewRangeContent;
	std::shared_ptr<lsutil::UserPreferences> UserPreferences;
};

// FoldingRangeLineExpected — fourslash.go:2896.
struct FoldingRangeLineExpected {
	uint32_t StartLine = 0;
	uint32_t EndLine = 0;
};

// hoverWithVerbosity — fourslash.go:3061.
struct hoverWithVerbosity {
	std::shared_ptr<lsproto::Hover> Hover;
	std::shared_ptr<int> VerbosityLevel;

	std::string marshalJSONTo(json::Encoder& enc) const {
		if (auto e = enc.writeToken(json::BeginObject); !e.empty())
			return e;
		if (auto e = enc.writeValue("\"hover\""); !e.empty()) return e;
		if (Hover == nullptr) {
			if (auto e = enc.writeValue("null"); !e.empty()) return e;
		} else {
			if (auto e = json::marshalEncode(enc, *Hover); !e.empty())
				return e;
		}
		if (auto e = enc.writeValue("\"verbosityLevel\""); !e.empty())
			return e;
		// Go field is a non-nil-able int; a null shared_ptr is 0.
		if (auto e = json::marshalEncode(
		        enc, VerbosityLevel != nullptr ? *VerbosityLevel : 0);
		    !e.empty())
			return e;
		return enc.writeToken(json::EndObject);
	}
};

// VerifySignatureHelpOptions — fourslash.go:4325.
struct VerifySignatureHelpOptions {
	// Text is the full signature text (e.g., "fn(x: string, y: number):
	// void")
	std::string Text;
	// DocComment is the documentation comment for the signature
	std::string DocComment;
	// ParameterCount is the expected number of parameters
	int ParameterCount = 0;
	// ParameterName is the expected name of the active parameter
	std::string ParameterName;
	// ParameterSpan is the expected label of the active parameter (e.g.,
	// "x: string")
	std::string ParameterSpan;
	// ParameterDocComment is the documentation for the active parameter
	std::string ParameterDocComment;
	// OverloadsCount is the expected number of overloads (signatures)
	int OverloadsCount = 0;
	// OverrideSelectedItemIndex overrides which signature to check
	// (default: ActiveSignature)
	int OverrideSelectedItemIndex = 0;
	// IsVariadic indicates if the signature has a rest parameter
	bool IsVariadic = false;
	// IsVariadicSet is true when IsVariadic was explicitly set (to
	// distinguish from default false)
	bool IsVariadicSet = false;
};

// SignatureHelpCase — fourslash.go:4585.
struct SignatureHelpCase {
	std::shared_ptr<lsproto::SignatureHelpContext> Context;
	fourslash::MarkerInput MarkerInput;
	std::shared_ptr<lsproto::SignatureHelp> Expected;
};

// VerifyWorkspaceSymbolCase — fourslash.go:5714.
struct VerifyWorkspaceSymbolCase {
	std::string Pattern;
	std::shared_ptr<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>
	    Includes;
	std::shared_ptr<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>
	    Exact;
	std::shared_ptr<lsutil::UserPreferences> Preferences;
};

// callHierarchyItemDirection — fourslash.go:3446.
enum class callHierarchyItemDirection : int {
	root,
	incoming,
	outgoing,
};

// callHierarchyItemKey — fourslash.go:3453.
struct callHierarchyItemKey {
	lsproto::DocumentUri uri;
	lsproto::Range range_;
	callHierarchyItemDirection direction = callHierarchyItemDirection::root;

	bool operator==(const callHierarchyItemKey&) const = default;
};
struct callHierarchyItemKeyHash {
	size_t operator()(const callHierarchyItemKey& k) const {
		return std::hash<std::string>{}(k.uri) ^
		       (std::hash<uint32_t>{}(k.range_.Start.Line) << 1) ^
		       (std::hash<uint32_t>{}(k.range_.Start.Character) << 2) ^
		       (std::hash<uint32_t>{}(k.range_.End.Line) << 3) ^
		       (std::hash<uint32_t>{}(k.range_.End.Character) << 4) ^
		       (std::hash<int>{}(static_cast<int>(k.direction)) << 5);
	}
};

// ===========================================================================
// baselineutil.go types
// ===========================================================================

// documentSpan — baselineutil.go:112. Also used as a map key
// (spanToContextId), hence hashable.
struct documentSpan {
	lsproto::DocumentUri uri;
	lsproto::Range textSpan;
	std::shared_ptr<lsproto::Range> contextSpan;

	bool operator==(const documentSpan& o) const {
		return uri == o.uri && textSpan == o.textSpan &&
		       ((contextSpan == nullptr) == (o.contextSpan == nullptr)) &&
		       (contextSpan == nullptr || *contextSpan == *o.contextSpan);
	}
};
// lspRangeHash — hash for lsproto::Range (Go map key semantics over a
// comparable struct).
struct lspRangeHash {
	size_t operator()(const lsproto::Range& r) const {
		return (std::hash<uint32_t>{}(r.Start.Line) << 1) ^
		       (std::hash<uint32_t>{}(r.Start.Character) << 2) ^
		       (std::hash<uint32_t>{}(r.End.Line) << 3) ^
		       (std::hash<uint32_t>{}(r.End.Character) << 4);
	}
};

struct documentSpanHash {
	size_t operator()(const documentSpan& s) const {
		return std::hash<std::string>{}(s.uri) ^
		       (std::hash<uint32_t>{}(s.textSpan.Start.Line) << 1) ^
		       (std::hash<uint32_t>{}(s.textSpan.Start.Character) << 2) ^
		       (std::hash<uint32_t>{}(s.textSpan.End.Line) << 3) ^
		       (std::hash<uint32_t>{}(s.textSpan.End.Character) << 4) ^
		       (std::hash<uint32_t>{}(
		            s.contextSpan ? s.contextSpan->Start.Line : 0)
		        << 5);
	}
};

// baselineFourslashLocationsOptions — baselineutil.go:118.
struct baselineFourslashLocationsOptions {
	// markerInfo
	MarkerOrRange* marker = nullptr; // location
	std::string markerName; // name of the marker to be printed in baseline

	std::string endMarker;

	std::function<std::shared_ptr<std::string>(const documentSpan&)>
	    startMarkerPrefix;
	std::function<std::shared_ptr<std::string>(const documentSpan&)>
	    endMarkerSuffix;
	std::function<std::string(const documentSpan&)> getLocationData;

	std::shared_ptr<documentSpan> additionalSpan;
	bool preserveResultOrder = false;
	std::vector<lsproto::DocumentUri> orderedFiles;
};

// detailKind — baselineutil.go:274.
enum class detailKind : int {
	marker       = 0, // /*MARKER*/
	contextStart = 1, // <|
	textStart    = 2, // [|
	textEnd      = 3, // |]
	contextEnd   = 4, // |>
};
inline bool isEnd(detailKind k) {
	return k == detailKind::contextEnd || k == detailKind::textEnd;
}
inline bool isStart(detailKind k) {
	return k == detailKind::contextStart || k == detailKind::textStart;
}

// baselineDetail — baselineutil.go:292.
struct baselineDetail {
	lsproto::Position pos;
	std::string positionMarker;
	std::shared_ptr<documentSpan> span;
	detailKind kind = detailKind::marker;

	// getRange — baselineutil.go:300.
	lsproto::Range getRange() const;
};

// lineSplitter — baselineutil.go:527 (`\r?\n`).
std::vector<std::string> splitIntoLines(const std::string& text);

// textWithContext — baselineutil.go:529. Implements lsconv.Script (raw
// script; SpanMap is nil — fourslash content is never content-mapped).
struct textWithContext {
	int nLinesContext = 0; // number of context lines to write to baseline

	std::shared_ptr<gostr::Builder>
	    readableContents; // builds what will be returned to be written to baseline

	std::shared_ptr<gostr::Builder>
	    newContent; // helper; the part of the original file content to write between details
	lsproto::Position pos;
	bool isLibFile_ = false;
	std::string fileName;
	std::string content; // content of the original file
	lsconv::LSPLineMap* lineStarts = nullptr;
	std::shared_ptr<testConverters> converters;

	// posLineInfo
	std::shared_ptr<lsproto::Position> posInfo;
	int lineInfo = 0;

	// lsconv.Script implementation
	const std::string& FileName() const { return fileName; }
	std::string OriginalFileName() const { return fileName; }
	const std::string& Text() const { return content; }
	const std::string& OriginalText() const { return content; }
	spanmap::SpanMap* SpanMap() const { return nullptr; }

	// add — baselineutil.go:589.
	void add(baselineDetail* detail);
	// readableJsoncBaseline — baselineutil.go:656.
	void readableJsoncBaseline(const std::string& text);
	// sliceOfContent — baselineutil.go:763.
	std::string sliceOfContent(std::optional<int> start,
	                           std::optional<int> end);
	// getIndex — baselineutil.go:779. Go `any` accepts *int, int,
	// core.TextPos, *core.TextPos, lsproto.Position, *lsproto.Position;
	// all callers pass a value so overloads suffice.
	int getIndex(const lsproto::Position& p);
	int getIndex(TextPos p) { return static_cast<int>(p); }
	// Go's `*int`/`int`/`TextPos` collapse: TextPos == int32_t here.
	int getIndex(int* p) {
		return p != nullptr ? (int)*p : 0;
	}
};

// newTextWithContext — baselineutil.go:566.
std::shared_ptr<textWithContext> newTextWithContext(
    const std::string& fileName, const std::string& content);

// markerAndItem — baselineutil.go:666.
// marshalDom — encoding/json output for a parsed json::Dom (Go
// map[string]any members marshal sorted; numbers pass through verbatim).
inline std::string marshalDom(const json::Dom& d, json::Encoder& enc) {
	switch (d.kind) {
	case json::Dom::K::Bool:
		return enc.writeValue(d.boolVal ? "true" : "false");
	case json::Dom::K::Number:
		return enc.writeValue(d.strVal);
	case json::Dom::K::String:
		return enc.writeValue(std::string_view(json::marshalString(d.strVal)));
	case json::Dom::K::Array: {
		if (auto e = enc.writeToken(json::BeginArray); !e.empty()) return e;
		for (auto& el : d.arr) {
			if (auto e = marshalDom(el, enc); !e.empty()) return e;
		}
		return enc.writeToken(json::EndArray);
	}
	case json::Dom::K::Object: {
		if (auto e = enc.writeToken(json::BeginObject); !e.empty()) return e;
		auto members = d.obj;
		std::sort(members.begin(), members.end(),
		          [](const std::pair<std::string, json::Dom>& a,
		             const std::pair<std::string, json::Dom>& b) {
			          return a.first < b.first;
		          });
		for (auto& m : members) {
			if (auto e = enc.writeValue(std::string_view(
			        json::marshalString(m.first)));
			    !e.empty())
				return e;
			if (auto e = marshalDom(m.second, enc); !e.empty()) return e;
		}
		return enc.writeToken(json::EndObject);
	}
	default:
		return enc.writeValue("null");
	}
}

template <typename T>
struct markerAndItem {
	std::shared_ptr<Marker> Marker;
	T Item;

	// marshalJSONTo — Go struct marshal of markerAndItem: exported fields
	// "Marker" and "Item". Marker marshals only its exported Go fields
	// (Position, LSPosition, Name, Data); fileName is unexported.
	std::string marshalJSONTo(json::Encoder& enc) const {
		if (auto e = enc.writeToken(json::BeginObject); !e.empty())
			return e;
		if (auto e = enc.writeValue("\"Marker\""); !e.empty()) return e;
		if (Marker == nullptr) {
			if (auto e = enc.writeValue("null"); !e.empty()) return e;
		} else {
			if (auto e = enc.writeToken(json::BeginObject); !e.empty())
				return e;
			if (auto e = enc.writeValue("\"Position\""); !e.empty())
				return e;
			if (auto e = json::marshalEncode(enc, Marker->Position);
			    !e.empty())
				return e;
			if (auto e = enc.writeValue("\"LSPosition\""); !e.empty())
				return e;
			if (auto e = json::marshalEncode(enc, Marker->LSPosition);
			    !e.empty())
				return e;
			if (auto e = enc.writeValue("\"Name\""); !e.empty())
				return e;
			if (Marker->Name != nullptr) {
				if (auto e =
				        json::marshalEncode(enc, *Marker->Name);
				    !e.empty())
					return e;
			} else {
				if (auto e = enc.writeValue("null"); !e.empty())
					return e;
			}
			if (auto e = enc.writeValue("\"Data\""); !e.empty())
				return e;
			if (Marker->Data != nullptr) {
				if (auto e = marshalDom(*Marker->Data, enc);
				    !e.empty())
					return e;
			} else {
				if (auto e = enc.writeValue("null"); !e.empty())
					return e;
			}
			if (auto e = enc.writeToken(json::EndObject); !e.empty())
				return e;
		}
		if (auto e = enc.writeValue("\"Item\""); !e.empty()) return e;
		if constexpr (requires { Item == nullptr; }) {
			if (Item == nullptr) {
				if (auto e = enc.writeValue("null"); !e.empty())
					return e;
			} else {
				if (auto e = json::marshalEncode(enc, Item);
				    !e.empty())
					return e;
			}
		} else {
			if (auto e = json::marshalEncode(enc, Item); !e.empty())
				return e;
		}
		return enc.writeToken(json::EndObject);
	}
};

// symbolInformationToData — baselineutil.go:800.
std::string symbolInformationToData(
    const std::shared_ptr<lsproto::SymbolInformation>& symbol);

// ===========================================================================
// statebaseline.go types
// ===========================================================================

// projectInfo — statebaseline.go:112 (`*compiler.Program`).
// projectInfo — statebaseline.go:112 (`*compiler.Program`).
using projectInfo = compiler::SimpleProgram*;

// openFileInfo — statebaseline.go:114.
struct openFileInfo;

// stateBaseline — statebaseline.go:25.
struct stateBaseline {
	gostr::Builder baseline;
	std::shared_ptr<testutil::fsbaselineutil::FSDiffer> fsDiffer;
	bool isInitialized = false;

	std::unordered_map<std::string, projectInfo> serializedProjects;
	std::unordered_map<std::string, std::shared_ptr<openFileInfo>>
	    serializedOpenFiles;
	project::ConfigFileRegistry* serializedConfigFileRegistry = nullptr;
};

// newStateBaseline — statebaseline.go:35.
std::shared_ptr<stateBaseline> newStateBaseline(
	const std::shared_ptr<vfs::iovfs::FsWithSys>& fsFromMap);

// requestOrMessage — statebaseline.go:47. `params` is carried as raw JSON
// (json::Value) so heterogeneous param types marshal uniformly.
struct requestOrMessage {
	std::string method;
	json::Value params;
};

// reindentJsonText — json.Marshal(v, jsontext.WithIndent("  ")) applied to
// pre-serialized JSON text (token-stream copy through an indenting Encoder).
std::string reindentJsonText(const std::string& raw);

// openFileInfo — statebaseline.go:114.
struct openFileInfo {
	std::string defaultProjectName;
	std::vector<std::string> allProjects;
};

// diffTableOptions — statebaseline.go:119.
struct diffTableOptions {
	std::string indent;
	bool sortKeys = false;
};

// diffTable — statebaseline.go:124.
struct diffTable {
	collections::OrderedMap<std::string, std::string> diff;
	diffTableOptions options;

	void add(const std::string& key, const std::string& value);
	void print(gostd::io::Writer* w, const std::string& header);
};

// diffTableWriter — statebaseline.go:158.
struct diffTableWriter {
	bool hasChange = false;
	std::string header;
	std::unordered_map<std::string,
	                   std::function<void(gostd::io::Writer*)>>
	    diffs;

	void setHasChange();
	void add(const std::string& key,
	         const std::function<void(gostd::io::Writer*)>& fn);
	void print(gostd::io::Writer* w);
};

// newDiffTableWriter — statebaseline.go:164.
std::shared_ptr<diffTableWriter> newDiffTableWriter(
    const std::string& header);

// areIterSeqEqual / sliceFromIterSeqString /
// printSlicesWithDiffTable / printStringIterSeqWithDiffTable —
// statebaseline.go:187,218,232.
bool areIterSeqEqual(const goseq::Seq<std::string>& a,
                     const goseq::Seq<std::string>& b);
std::vector<std::string> sliceFromIterSeqString(
    const goseq::Seq<std::string>& seq);
void printSlicesWithDiffTable(
    gostd::io::Writer* w, const std::string& header,
    const std::vector<std::string>& newSlice,
    const std::function<std::vector<std::string>()>& getOldSlice,
    const diffTableOptions& options, const std::string& topChange,
    const std::function<bool(const std::string&)>& isDefault);
void printStringIterSeqWithDiffTable(
    gostd::io::Writer* w, const std::string& header,
    const goseq::Seq<std::string>& newIterSeq,
    const std::function<goseq::Seq<std::string>()>& getOldIterSeq,
    const diffTableOptions& options, const std::string& topChange);

// ===========================================================================
// semantictokens.go types
// ===========================================================================

// SemanticToken — semantictokens.go:12.
struct SemanticToken {
	std::string Type;
	std::string Text;
};

// ===========================================================================
// fourslashDiagnostic — fourslash.go:5508
// ===========================================================================
struct fourslashDiagnosticFile final : diagnosticwriter::FileLike {
	std::shared_ptr<testutil::harnessutil::TestFile> file_;
	std::vector<TextPos> ecmaLineMap_;

	// diagnosticwriter::FileLike
	std::string_view fileName() const override {
		return file_ ? file_->UnitName : "";
	}
	std::string_view text() const override {
		return file_ ? file_->Content : "";
	}
	const std::vector<TextPos>& ecmaLineMap() const override;
	// Script-style helpers (not part of FileLike)
	std::string FileName() const { return file_->UnitName; }
	std::string OriginalFileName() const { return file_->UnitName; }
	const std::string& Text() const { return file_->Content; }
	const std::string& OriginalText() const { return file_->Content; }
	spanmap::SpanMap* SpanMap() const { return nullptr; }
};

struct fourslashDiagnostic final : diagnosticwriter::Diagnostic {
	// Go field is `file`; renamed so it can coexist with the file()
	// override required by diagnosticwriter::Diagnostic.
	std::shared_ptr<fourslashDiagnosticFile> file_;
	TextRange loc;
	int32_t code_ = 0;
	tsc::DiagnosticCategory category_ =
	    tsc::DiagnosticCategory::Error;
	std::string message;
	std::vector<std::shared_ptr<fourslashDiagnostic>> relatedDiagnostics;
	bool reportsUnnecessary = false;
	bool reportsDeprecated = false;

	// diagnosticwriter::Diagnostic
	const diagnosticwriter::FileLike* file() override;
	int pos() override;
	int end() override;
	int len() override;
	int32_t code() override;
	tsc::DiagnosticCategory category() override;
	std::string_view source() override;
	std::string localize(const locale::Locale&) override;
	std::vector<diagnosticwriter::Diagnostic*> messageChain() override;
	std::vector<diagnosticwriter::Diagnostic*> relatedInformation() override;
};

// ===========================================================================
// scriptInfo — fourslash.go:75
// ===========================================================================
struct scriptInfo {
	std::string fileName;
	std::string content;
	lsconv::LSPLineMap* lineMap = nullptr;
	int32_t version = 0;

	// editContent — fourslash.go:117? (editScript in fourslash.go).
	void editContent(const TextChange& change);

	// lsconv.Script
	const std::string& FileName() const { return fileName; }
	const std::string& OriginalFileName() const { return fileName; }
	const std::string& Text() const { return content; }
	const std::string& OriginalText() const { return content; }
	spanmap::SpanMap* SpanMap() const { return nullptr; }

	// GetLineContent — fourslash.go.
	std::string GetLineContent(int line) const;
};

// newScriptInfo — fourslash.go:107.
std::shared_ptr<scriptInfo> newScriptInfo(const std::string& fileName,
                                          const std::string& content);

// ===========================================================================
// testConverters — fourslash.go:82
//
// Go embeds *lsconv.Converters. The cpp Converters' generic member bodies
// are ls-slice dep-stubs, so the embedded converters carry the
// positionEncoding/getLineMap args directly and this type implements the
// nil-SpanMap conversion paths of converters.go itself (all fourslash
// scripts are raw, non-content-mapped).
// ===========================================================================
struct testConverters {
	// Go: `type testConverters struct { *lsconv.Converters }`
	lsconv::Converters* Converters = nullptr;
	lsproto::PositionEncodingKind positionEncoding_;
	std::function<lsconv::LSPLineMap*(const std::string&)> getLineMap_;

	testConverters(lsproto::PositionEncodingKind enc,
	               std::function<lsconv::LSPLineMap*(const std::string&)>
	                   getLineMap);

	// positionToLineAndCharacter — converters.go:417 (nil-SpanMap path).
	template <lsconv::Script T>
	lsproto::Position positionToLineAndCharacter(
	    T script, TextPos position) {
		// UTF-8 offset to UTF-8/16 0-indexed line and character
		tsc::debug::assert(
		    script->SpanMap() == nullptr,
		    "raw coordinate conversion requires a non-content-mapped "
		    "script");

		position = std::max<TextPos>(
		    0, std::min<TextPos>(position, (TextPos)script->Text().size()));

		lsconv::LSPLineMap* lineMap = getLineMap_(script->FileName());

		// slices.BinarySearch(lineMap.LineStarts, position)
		auto& starts = lineMap->LineStarts;
		auto it = std::lower_bound(starts.begin(), starts.end(), position);
		int line = (int)(it - starts.begin());
		bool isLineStart = it != starts.end() && *it == position;
		if (!isLineStart) {
			line--;
		}
		line = std::max(0, std::min(line, (int)starts.size() - 1));

		// The current line ranges from lineMap.LineStarts[line] (or 0) to
		// lineMap.LineStarts[line+1] (or len(text)).
		TextPos start = starts[line];

		TextPos character = 0;
		if (lineMap->AsciiOnly ||
		    positionEncoding_ == lsproto::PositionEncodingKindUTF8) {
			character = position - start;
		} else {
			// We need to rescan the text as UTF-16 to find the character
			// offset. (Go: `for _, r := range text[start:position]`)
			std::string_view rest =
			    std::string_view(script->Text())
			        .substr(start, position - start);
			while (!rest.empty()) {
				auto [r, size] =
				    gostr::decodeRuneInString(rest);
				character += gostr::utf16RuneLen(r);
				rest = rest.substr(size);
			}
		}

		return lsproto::Position{
		    .Line = (uint32_t)line,
		    .Character = (uint32_t)character,
		};
	}

	// lineAndCharacterToPosition — converters.go:366 (nil-SpanMap path).
	template <lsconv::Script T>
	TextPos lineAndCharacterToPosition(
	    T script, lsproto::Position lineAndCharacter) {
		// UTF-8/16 0-indexed line and character to UTF-8 offset
		tsc::debug::assert(
		    script->SpanMap() == nullptr,
		    "raw coordinate conversion requires a non-content-mapped "
		    "script");

		lsconv::LSPLineMap* lineMap = getLineMap_(script->FileName());

		TextPos line = (TextPos)lineAndCharacter.Line;
		TextPos char_ = (TextPos)lineAndCharacter.Character;

		TextPos textLen = (TextPos)script->Text().size();

		// Clamp line to valid range.
		if ((int)line >= (int)lineMap->LineStarts.size()) {
			return textLen;
		}

		TextPos start = lineMap->LineStarts[line];

		// Determine the end of this line (start of next line, or end of
		// text).
		TextPos lineEnd;
		if ((int)line + 1 < (int)lineMap->LineStarts.size()) {
			lineEnd = lineMap->LineStarts[(int)line + 1];
		} else {
			lineEnd = textLen;
		}

		if (lineMap->AsciiOnly ||
		    positionEncoding_ == lsproto::PositionEncodingKindUTF8) {
			return std::max(start, std::min(start + char_, lineEnd));
		}

		// Scan from line start counting UTF-16 code units to find the
		// byte position. Uses DecodeRuneInString (not range + RuneLen) so
		// that invalid UTF-8 bytes advance by their actual size (1)
		// rather than RuneLen(RuneError) == 3. This matches the approach
		// in scanner.ComputePositionOfLineAndUTF16Character.
		TextPos utf16Char = 0;
		int pos = (int)start;
		int end = (int)lineEnd;
		std::string_view text(script->Text());
		while (pos < end) {
			auto [r, size] =
			    gostr::decodeRuneInString(text.substr(pos));
			TextPos u16Len = gostr::utf16RuneLen(r);
			if (utf16Char + u16Len > char_) {
				break;
			}
			utf16Char += u16Len;
			pos += size;
		}

		return (TextPos)pos;
	}

	// PositionToLineAndCharacter — fourslash.go:90.
	template <lsconv::Script T>
	lsproto::Position PositionToLineAndCharacter(T script,
	                                             TextPos position) {
		auto [pos, fidelity] = ToLSPPosition(script, position);
		// debug.Assert(fidelity.IsExact()) — Go fourslash.go:91-93 does not
		// check the fidelity explicitly, but all fourslash scripts are raw
		// (nil SpanMap) so the mapping is always exact.
		tsc::debug::assert(fidelity.IsExact(), "expected exact mapping");
		return pos;
	}

	// LineAndCharacterToPosition — fourslash.go:95.
	template <lsconv::Script T>
	TextPos LineAndCharacterToPosition(
	    T script, lsproto::Position position) {
		auto positions =
		    FromLSPPosition(script, position, spanmap::FeatureAll);
		tsc::debug::assert(positions.size() == 1,
	                   "fourslash script must have exactly one position "
	                   "projection");
		return positions[0].Position;
	}

	// FromLSPPosition — Converters.FromLSPPosition
	// (converters.go:202): raw scripts produce a single exact position.
	template <lsconv::Script T>
	std::vector<lsconv::MappedPosition<T>> FromLSPPosition(
	    T script, lsproto::Position position, spanmap::Feature feature) {
		if (script->SpanMap() != nullptr) {
			TSC_UNREACHABLE(
			    "FromLSPPosition — content-mapped scripts owned by lsconv "
			    "slice");
		}
		lsconv::MappedPosition<T> mp{};
		mp.Script = script;
		mp.Position =
		    lineAndCharacterToPosition(script, position);
		mp.Fidelity = spanmap::FidelityExact;
		return {mp};
	}

	// FromLSPRange — Converters.FromLSPRange (converters.go:133).
	template <lsconv::Script T>
	std::vector<lsconv::MappedSpan<T>> FromLSPRange(
	    T script, lsproto::Range range_, spanmap::Feature feature) {
		if (script->SpanMap() != nullptr) {
			TSC_UNREACHABLE(
			    "FromLSPRange — content-mapped scripts owned by lsconv "
			    "slice");
		}
		auto start =
		    lineAndCharacterToPosition(script, range_.Start);
		auto end = lineAndCharacterToPosition(script, range_.End);
		lsconv::MappedSpan<T> ms{};
		ms.Script = script;
		ms.Span = TextRange{start, end};
		ms.Fidelity = spanmap::FidelityExact;
		return {ms};
	}

	// ToLSPRange — Converters.ToLSPRange (converters.go:59).
	template <lsconv::Script T>
	std::pair<lsproto::Range, spanmap::Fidelity> ToLSPRange(
	    T script, TextRange textRange) {
		// virtualRangeToOriginal: nil SpanMap returns the same script +
		// FidelityExact.
		if (script->SpanMap() != nullptr) {
			TSC_UNREACHABLE(
			    "ToLSPRange — content-mapped scripts owned by lsconv "
			    "slice");
		}
		return {lsproto::Range{
		            .Start = positionToLineAndCharacter(
		                script, (TextPos)textRange.pos()),
		            .End = positionToLineAndCharacter(
		                script, (TextPos)textRange.end()),
		        },
		        spanmap::FidelityExact};
	}

	// ToLSPPosition — Converters.ToLSPPosition (converters.go:83).
	template <lsconv::Script T>
	std::pair<lsproto::Position, spanmap::Fidelity> ToLSPPosition(
	    T script, TextPos position) {
		if (script->SpanMap() != nullptr) {
			TSC_UNREACHABLE(
			    "ToLSPPosition — content-mapped scripts owned by lsconv "
			    "slice");
		}
		return {positionToLineAndCharacter(script, position),
		        spanmap::FidelityExact};
	}
};

// newTestConverters — fourslash.go:86 (signature adjusted: takes the
// NewConverters args since cpp Converters keeps them private).
std::shared_ptr<testConverters> newTestConverters(
    lsproto::PositionEncodingKind encoding,
    std::function<lsconv::LSPLineMap*(const std::string&)> getLineMap);

// textEditSpan — fourslash.go:101.
struct textEditSpan {
	int start = 0;
	int end = 0;
	int length = 0;
};

// ===========================================================================
// FourslashTest — fourslash.go:46
// ===========================================================================
struct FourslashTest {
	std::shared_ptr<testutil::lsptestutil::LSPClient> client;
	std::shared_ptr<::tsc::vfs::FS> vfs;

	std::shared_ptr<TestData>
	    testData; // !!! consolidate test files from test data and script info
	std::unordered_map<baselineCommand, std::shared_ptr<gostr::Builder>>
	    baselines;
	std::shared_ptr<collections::MultiMap<std::string,
	                                      std::shared_ptr<RangeMarker>>>
	    rangesByText;
	std::unordered_set<std::string> openFiles;
	std::shared_ptr<stateBaseline> stateBaseline_;

	std::unordered_map<std::string, std::shared_ptr<scriptInfo>> scriptInfos;
	std::shared_ptr<testConverters> converters;

	bool stateEnableFormatting = false;
	bool reportFormatOnTypeCrash = false;
	lsutil::UserPreferences userPreferences;
	lsproto::Position currentCaretPosition;
	std::shared_ptr<std::string> lastKnownMarkerName;
	std::string activeFilename;
	std::shared_ptr<lsproto::Position> selectionEnd;

	std::shared_ptr<lsproto::ClientCapabilities> capabilities;
	bool isStradaServer =
	    false; // Whether this is a fourslash server test in Strada. !!! Remove
	           // once we don't need to diff baselines.

	// Semantic token configuration
	std::vector<std::string> semanticTokenTypes;
	std::vector<std::string> semanticTokenModifiers;

	// --- fourslash.go ---

	// handleServerRequest — fourslash.go:288.
	std::shared_ptr<lsproto::ResponseMessage> handleServerRequest(
	    gostd::Context ctx,
	    const std::shared_ptr<lsproto::RequestMessage>& req);

	// initialize — fourslash.go:368.
	void initialize(gostd::testing::T* t,
	                const std::shared_ptr<lsproto::ClientCapabilities>&
	                    capabilities,
	                bool runExternalCode);

	// sendRequest — fourslash.go:739 (template body below).
	template <typename Params, typename Resp>
	Resp sendRequest(gostd::testing::T* t,
	                 const lsproto::RequestInfo<Params, Resp>& info,
	                 const Params& params);

	// sendRequestAndBaselineWorker — fourslash.go:744.
	template <typename Params, typename Resp>
	Resp sendRequestAndBaselineWorker(
	    gostd::testing::T* t,
	    const lsproto::RequestInfo<Params, Resp>& info, const Params& params,
	    bool baselineProjects);

	// sendNotification — fourslash.go:776.
	template <typename Params>
	void sendNotification(gostd::testing::T* t,
	                      const lsproto::NotificationInfo<Params>& info,
	                      const Params& params);

	// updateState — fourslash.go:790. Go takes `params any` and type-
	// asserts; here it's a template so the typed notification params flow
	// through from sendNotification.
	template <typename Params>
	void updateState(const lsproto::Method& method, const Params& params);

	// GetOptions — fourslash.go:799.
	lsutil::UserPreferences GetOptions();

	// Configure — fourslash.go:803.
	void Configure(gostd::testing::T* t,
	               const lsutil::UserPreferences& config);

	// ConfigureWithReset — fourslash.go:815.
	std::function<void()> ConfigureWithReset(
	    gostd::testing::T* t, const lsutil::UserPreferences& config);

	void GoToMarkerOrRange(gostd::testing::T* t,
	                       MarkerOrRange* markerOrRange);
	void GoToMarker(gostd::testing::T* t, const std::string& markerName);
	void goToMarker(gostd::testing::T* t, MarkerOrRange* markerOrRange);
	void GoToEOF(gostd::testing::T* t);
	void GoToBOF(gostd::testing::T* t);
	void GoToPosition(gostd::testing::T* t, int position);
	void goToPosition(gostd::testing::T* t, lsproto::Position position);
	void GoToEachMarker(
	    gostd::testing::T* t, const std::vector<std::string>& markerNames,
	    const std::function<void(std::shared_ptr<Marker>, int)>& action);
	void GoToEachRange(
	    gostd::testing::T* t,
	    const std::function<void(gostd::testing::T*,
	                             std::shared_ptr<RangeMarker>)>& action);
	void GoToRangeStart(gostd::testing::T* t,
	                    const std::shared_ptr<RangeMarker>& rangeMarker);
	void GoToSelect(gostd::testing::T* t, const std::string& startMarkerName,
	                const std::string& endMarkerName);
	void GoToSelectRange(gostd::testing::T* t,
	                     const std::shared_ptr<RangeMarker>& rangeMarker);
	void GoToFile(gostd::testing::T* t, const std::string& filename);
	void GoToFileNumber(gostd::testing::T* t, int index);
	std::vector<std::shared_ptr<Marker>> Markers();
	std::vector<std::string> MarkerNames();
	std::shared_ptr<Marker> MarkerByName(gostd::testing::T* t,
	                                     const std::string& name);
	std::vector<std::shared_ptr<RangeMarker>> Ranges();
	std::vector<std::shared_ptr<RangeMarker>> getRangesInFile(
	    const std::string& fileName);
	void ensureActiveFile(gostd::testing::T* t, const std::string& filename);
	void CloseFileOfMarker(gostd::testing::T* t,
	                       const std::string& markerName);
	void openFile(gostd::testing::T* t, const std::string& filename);
	void FormatDocument(gostd::testing::T* t, const std::string& filename);
	void FormatSelection(gostd::testing::T* t,
	                     const std::string& startMarkerName,
	                     const std::string& endMarkerName);
	void VerifyCurrentFileContent(gostd::testing::T* t,
	                              const std::string& expectedContent);
	void VerifyCurrentLineContent(gostd::testing::T* t,
	                              const std::string& expectedContent);
	void VerifyIndentation(gostd::testing::T* t, int numSpaces);

	// completions — fourslash.go:1160-1668.
	VerifyCompletionsResult VerifyCompletions(
	    gostd::testing::T* t, const MarkerInput& markerInput,
	    const CompletionsExpectedList* expected);
	VerifyCompletionsResult verifyCompletionsActions(
	    const std::shared_ptr<lsproto::CompletionList>& list);
	std::shared_ptr<lsproto::CompletionList> verifyCompletionsWorker(
	    gostd::testing::T* t, const CompletionsExpectedList* expected);
	std::shared_ptr<lsproto::CompletionList> GetCompletions(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsutil::UserPreferences>& userPreferences);
	void VerifyJSDocCompletion(
	    gostd::testing::T* t, const MarkerInput& markerInput,
	    int expectedOffset, const std::string& expectedText,
	    const std::shared_ptr<bool>& generateReturnInDocTemplate);
	void VerifyNoJSDocCompletion(gostd::testing::T* t,
	                             const MarkerInput& markerInput);
	void goToMarkerInput(gostd::testing::T* t,
	                     const MarkerInput& markerInput);
	std::shared_ptr<lsproto::CompletionList> getCompletions(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsutil::UserPreferences>& userPreferences);
	void verifyCompletionsResult(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsproto::CompletionList>& actual,
	    const CompletionsExpectedList* expected, const std::string& prefix);
	void verifyCompletionsItems(
	    gostd::testing::T* t, const std::string& prefix,
	    const std::vector<std::shared_ptr<lsproto::CompletionItem>>& actual,
	    const CompletionsExpectedItems* expected);
	void verifyCompletionsAreExactly(
	    gostd::testing::T* t, const std::string& prefix,
	    const std::vector<std::shared_ptr<lsproto::CompletionItem>>& actual,
	    const std::vector<CompletionsExpectedItem>& expected);
	std::string verifyCompletionItem(
	    gostd::testing::T* t, const std::string& prefix,
	    // by value: Go reassigns `actual` after resolving.
	    std::shared_ptr<lsproto::CompletionItem> actual,
	    const std::shared_ptr<lsproto::CompletionItem>& expected);
	std::shared_ptr<lsproto::CompletionItem> ResolveCompletionItem(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsproto::CompletionItem>& item);
	std::shared_ptr<lsproto::CompletionItem> resolveCompletionItem(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsproto::CompletionItem>& item);

	// code fix — fourslash.go:1687-2490.
	void VerifyCodeFix(gostd::testing::T* t,
	                   const VerifyCodeFixOptions& options);
	void VerifyRangeAfterCodeFix(gostd::testing::T* t,
	                             const std::string& expectedText,
	                             bool includeWhitespace, int errorCode,
	                             int index);
	std::vector<std::shared_ptr<lsproto::TextEdit>>
	getCodeActionEditsForActiveFile(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsproto::CodeAction>& action);
	void VerifyCodeFixAvailable(
	    gostd::testing::T* t,
	    const std::optional<std::vector<std::string>>&
	        expectedDescriptions);
	void VerifyCodeFixNotAvailable(
	    gostd::testing::T* t, const std::vector<std::string>& expected);
	void VerifyCodeFixAvailableExact(
	    gostd::testing::T* t,
	    const std::vector<std::string>& expectedDescriptions);
	void VerifyCodeFixAll(gostd::testing::T* t,
	                      const VerifyCodeFixAllOptions& options);
	void VerifySourceFixAll(gostd::testing::T* t,
	                        const std::string& expectedContent);
	std::vector<std::shared_ptr<lsproto::CodeAction>> getCodeFixActions(
	    gostd::testing::T* t, const std::vector<int>& errorCode);
	std::vector<std::shared_ptr<lsproto::CodeAction>> getAllQuickFixActions(
	    gostd::testing::T* t, const std::vector<int>& errorCode);
	TextRange updateTextRangeForTextEdits(
	    TextRange textRange,
	    const std::vector<std::shared_ptr<lsproto::TextEdit>>& edits);
	// Go sorts the edits slice in place (sort.Slice), so this takes a
	// mutable vector copy to preserve the same observable behavior.
	std::string applyEditsToContent(
	    std::string content,
	    std::vector<std::shared_ptr<lsproto::TextEdit>> edits);
	void VerifyOrganizeImports(
	    gostd::testing::T* t, const std::string& expectedContent,
	    const lsproto::CodeActionKind& codeActionKind,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences);
	void VerifyOrganizeImportsWithRequestKind(
	    gostd::testing::T* t, const std::string& expectedContent,
	    const lsproto::CodeActionKind& requestedKind,
	    const lsproto::CodeActionKind& expectedKind,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences);
	void verifyOrganizeImports(
	    gostd::testing::T* t, const std::string& expectedContent,
	    const lsproto::CodeActionKind& requestedKind,
	    const lsproto::CodeActionKind& expectedKind,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences);
	std::shared_ptr<lsproto::CompletionItem> findCompletionForCodeAction(
	    gostd::testing::T* t,
	    const std::vector<std::shared_ptr<lsproto::CompletionItem>>& items,
	    const std::string& description);
	void VerifyApplyCodeActionFromCompletion(
	    gostd::testing::T* t, const std::shared_ptr<std::string>& markerName,
	    const ApplyCodeActionFromCompletionOptions* options);
	void VerifyImportFixAtPosition(
	    gostd::testing::T* t, const std::vector<std::string>& expectedTexts,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences);
	void VerifyImportFixModuleSpecifiers(
	    gostd::testing::T* t, const std::string& markerName,
	    const std::vector<std::string>& expectedModuleSpecifiers,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences);

	// baselines — fourslash.go:2519-6045.
	void VerifyBaselineFindAllReferences(
	    gostd::testing::T* t, const std::vector<std::string>& markers);
	void VerifyBaselineVSFindAllReferences(
	    gostd::testing::T* t, const std::vector<std::string>& markers);
	void VerifyBaselineCodeLens(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences);
	void MarkTestAsStradaServer();
	void VerifyBaselineGoToDefinition(
	    gostd::testing::T* t, bool includeOriginalSelectionRange,
	    const std::vector<std::string>& markers);
	void verifyBaselineDefinitions(
	    gostd::testing::T* t, const baselineCommand& definitionCommand,
	    const std::string& definitionMarker,
	    const std::function<
	        lsproto::LocationOrLocationsOrDefinitionLinksOrNull(
	            gostd::testing::T*, FourslashTest*, const std::string&,
	            lsproto::Position)>& getDefinitions,
	    bool includeOriginalSelectionRange,
	    const std::vector<std::string>& markers);
	void VerifyBaselineGoToTypeDefinition(
	    gostd::testing::T* t, const std::vector<std::string>& markerNames);
	void VerifyBaselineGoToSourceDefinition(
	    gostd::testing::T* t, const std::vector<std::string>& markerNames);
	void VerifyBaselineGoToImplementation(
	    gostd::testing::T* t, const std::vector<std::string>& markerNames);
	void VerifyBaselineWorkspaceSymbol(gostd::testing::T* t,
	                                   const std::string& query);
	void VerifyOutliningSpans(
	    gostd::testing::T* t,
	    const std::vector<lsproto::FoldingRangeKind>& foldingRangeKind);
	void VerifyFoldingRangeLines(
	    gostd::testing::T* t,
	    const std::vector<FoldingRangeLineExpected>& expected);
	void VerifyBaselineHover(gostd::testing::T* t);
	void VerifyBaselineVSHover(gostd::testing::T* t);
	void VerifyBaselineHoverWithVerbosity(
	    gostd::testing::T* t,
	    const std::unordered_map<std::string, std::vector<int>>&
	        verbosityLevels);
	void VerifyBaselineSignatureHelp(gostd::testing::T* t);
	void VerifyBaselineSelectionRanges(gostd::testing::T* t);
	void VerifyBaselineCallHierarchy(gostd::testing::T* t);
	void VerifyBaselineDocumentHighlights(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences,
	    const std::vector<MarkerOrRangeOrName>& markerOrRangeOrNames);
	void VerifyBaselineDocumentHighlightsWithOptions(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences,
	    const std::vector<std::string>& filesToSearch,
	    const std::vector<MarkerOrRangeOrName>& markerOrRangeOrNames);
	void verifyBaselineDocumentHighlights(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences,
	    const std::vector<std::string>& filesToSearch,
	    const std::vector<MarkerOrRange*>& markerOrRanges);
	std::vector<MarkerOrRange*> lookupMarkersOrGetRanges(
	    gostd::testing::T* t, const std::vector<std::string>& markers);
	void Insert(gostd::testing::T* t, const std::string& text);
	void InsertLine(gostd::testing::T* t, const std::string& text);
	void Backspace(gostd::testing::T* t, int count);
	void DeleteAtCaret(gostd::testing::T* t, int count);
	void Paste(gostd::testing::T* t, const std::string& text);
	void ReplaceLine(gostd::testing::T* t, int lineIndex,
	                 const std::string& text);
	void selectLine(gostd::testing::T* t, int lineIndex);
	void selectRange(gostd::testing::T* t, TextRange textRange);
	TextRange getSelection();
	int applyTextEdits(
	    gostd::testing::T* t,
	    const std::vector<std::shared_ptr<lsproto::TextEdit>>& edits);
	void Replace(gostd::testing::T* t, int start, int length,
	             const std::string& text);
	void replaceWorker(gostd::testing::T* t, int start, int length,
	                   const std::string& text);
	void typeText(gostd::testing::T* t, const std::string& text);
	void editScriptAndUpdateMarkers(gostd::testing::T* t,
	                                const std::string& fileName, int editStart,
	                                int editEnd, const std::string& newText);
	void editScriptAndUpdateMarkersWorker(
	    gostd::testing::T* t, const std::string& fileName,
	    const std::vector<TextChange>& changes);
	TextRange fromLSPRange(scriptInfo* script,
	                             const lsproto::Range& r);
	scriptInfo* editScript(gostd::testing::T* t, const std::string& fileName,
	                       const TextChange& change);
	scriptInfo* getScriptInfo(const std::string& fileName);
	scriptInfo* getOrLoadScriptInfo(const std::string& fileName);
	void VerifyQuickInfoAt(gostd::testing::T* t, const std::string& marker,
	                       const std::string& expectedText,
	                       const std::string& expectedDocumentation);
	std::shared_ptr<lsproto::Hover> getQuickInfoAtCurrentPosition(
	    gostd::testing::T* t);
	void verifyHoverContent(
	    gostd::testing::T* t,
	    const lsproto::MarkupContentOrStringOrMarkedStringWithLanguageOrMarkedStrings&
	        actual,
	    const std::string& expectedText,
	    const std::string& expectedDocumentation,
	    const std::string& prefix);
	void verifyHoverMarkdown(gostd::testing::T* t,
	                         const std::string& actual,
	                         const std::string& expectedText,
	                         const std::string& expectedDocumentation,
	                         const std::string& prefix);
	void VerifyQuickInfoExists(gostd::testing::T* t);
	void VerifyNotQuickInfoExists(gostd::testing::T* t);
	std::pair<bool, std::shared_ptr<lsproto::Hover>> quickInfoIsEmpty(
	    gostd::testing::T* t);
	void VerifyQuickInfoIs(gostd::testing::T* t,
	                       const std::string& expectedText,
	                       const std::string& expectedDocumentation);
	void VerifyJsxClosingTag(
	    gostd::testing::T* t,
	    const std::unordered_map<std::string, std::shared_ptr<std::string>>&
	        markersToNewText);
	void VerifyBaselineClosingTags(gostd::testing::T* t);
	void VerifySignatureHelp(gostd::testing::T* t,
	                         const VerifySignatureHelpOptions& expected);
	void VerifyNoSignatureHelp(gostd::testing::T* t);
	void VerifyNoSignatureHelpWithContext(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsproto::SignatureHelpContext>& context);
	void VerifyNoSignatureHelpForMarkersWithContext(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsproto::SignatureHelpContext>& context,
	    const std::vector<std::string>& markers);
	void VerifySignatureHelpPresent(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsproto::SignatureHelpContext>& context);
	void VerifySignatureHelpPresentForMarkers(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsproto::SignatureHelpContext>& context,
	    const std::vector<std::string>& markers);
	void VerifyNoSignatureHelpForMarkers(
	    gostd::testing::T* t, const std::vector<std::string>& markers);
	void VerifySignatureHelpWithCases(
	    gostd::testing::T* t,
	    const std::vector<std::shared_ptr<SignatureHelpCase>>&
	        signatureHelpCases);
	void verifySignatureHelp(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsproto::SignatureHelpContext>& context,
	    const std::shared_ptr<lsproto::SignatureHelp>& expected);
	void verifySignatureHelpResult(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsproto::SignatureHelp>& actual,
	    const std::shared_ptr<lsproto::SignatureHelp>& expected,
	    const std::string& prefix);
	std::string getCurrentPositionPrefix();
	void BaselineAutoImportsCompletions(
	    gostd::testing::T* t, const std::vector<std::string>& markerNames);
	void VerifyBaselineRename(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences,
	    const std::vector<MarkerOrRangeOrName>& markerOrNameOrRanges);
	void verifyBaselineRename(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences,
	    const std::vector<MarkerOrRange*>& markerOrRanges);
	void VerifyRenameSucceeded(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences);
	void VerifyRenameRange(
	    gostd::testing::T* t, const lsproto::Range& expectedRange,
	    const std::string& expectedPlaceholder,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences);
	lsproto::RenameResponse RenameAtCaret(gostd::testing::T* t,
	                                      const std::string& newName);
	lsproto::WillRenameFilesResponse WillRenameFiles(
	    gostd::testing::T* t,
	    const std::vector<std::shared_ptr<lsproto::FileRename>>& files);
	void willRenameFilesWorker(
	    gostd::testing::T* t,
	    const std::vector<std::shared_ptr<lsproto::FileRename>>& files);
	void VerifyRename(
	    gostd::testing::T* t, const std::string& markerName,
	    const std::string& newName,
	    const std::unordered_map<std::string, std::string>&
	        expectedFileContents);
	void VerifyWillRenameFilesEdits(
	    gostd::testing::T* t, const std::string& oldPath,
	    const std::string& newPath,
	    const std::unordered_map<std::string, std::string>&
	        expectedFileContents,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences);
	std::function<std::pair<std::string, bool>(const std::string&)>
	getPathUpdater(const std::string& oldPath, const std::string& newPath);
	void renameFileOrDirectory(gostd::testing::T* t,
	                           const std::string& oldPath,
	                           const std::string& newPath);
	void VerifyRenameFailed(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences);
	void VerifyBaselineRenameAtRangesWithText(
	    gostd::testing::T* t,
	    const std::shared_ptr<lsutil::UserPreferences>& preferences,
	    const std::vector<std::string>& texts);
	std::shared_ptr<collections::MultiMap<
	    std::string, std::shared_ptr<RangeMarker>>>
	GetRangesByText();
	std::string getRangeText(const RangeMarker* r);
	void verifyBaselines(gostd::testing::T* t, const std::string& testPath);
	void VerifyBaselineInlayHints(
	    gostd::testing::T* t, const std::shared_ptr<lsproto::Range>& span,
	    const std::shared_ptr<lsutil::UserPreferences>& testPreferences);
	void VerifyBaselineLinkedEditing(gostd::testing::T* t);
	void VerifyLinkedEditing(
	    gostd::testing::T* t,
	    const std::unordered_map<std::string, std::vector<lsproto::Range>>&
	        markerNamesToExpected);
	void VerifyDiagnostics(
	    gostd::testing::T* t,
	    const std::vector<std::shared_ptr<lsproto::Diagnostic>>& expected);
	void VerifyNonSuggestionDiagnostics(
	    gostd::testing::T* t,
	    const std::vector<std::shared_ptr<lsproto::Diagnostic>>& expected);
	void VerifySuggestionDiagnostics(
	    gostd::testing::T* t,
	    const std::vector<std::shared_ptr<lsproto::Diagnostic>>& expected);
	void verifyDiagnostics(
	    gostd::testing::T* t,
	    const std::vector<std::shared_ptr<lsproto::Diagnostic>>& expected,
	    const std::function<
	        bool(const std::shared_ptr<lsproto::Diagnostic>&)>&
	        filterDiagnostics);
	std::vector<std::shared_ptr<lsproto::Diagnostic>> getDiagnostics(
	    gostd::testing::T* t, const std::string& fileName);
	void VerifyBaselineNonSuggestionDiagnostics(gostd::testing::T* t);
	std::shared_ptr<fourslashDiagnostic> toDiagnostic(
	    scriptInfo* script,
	    const std::shared_ptr<lsproto::Diagnostic>& lspDiagnostic);
	void VerifyBaselineDocumentSymbol(gostd::testing::T* t);
	// VerifyWorkspaceSymbol — fourslash.go:5721 (`verify.navigateTo`).
	void VerifyWorkspaceSymbol(
	    gostd::testing::T* t,
	    const std::vector<std::shared_ptr<VerifyWorkspaceSymbolCase>>&
	        cases);
	void VerifyNumberOfErrorsInCurrentFile(gostd::testing::T* t,
	                                       int expectedCount);
	void VerifyNoErrors(gostd::testing::T* t);
	void VerifyErrorExistsAtRange(
	    gostd::testing::T* t,
	    const std::shared_ptr<RangeMarker>& rangeMarker, int code,
	    const std::string& message);
	void VerifyErrorExistsBetweenMarkers(
	    gostd::testing::T* t, const std::string& startMarkerName,
	    const std::string& endMarkerName);
	void VerifyErrorExistsAfterMarker(gostd::testing::T* t,
	                                  const std::string& markerName);
	void VerifyErrorExistsBeforeMarker(gostd::testing::T* t,
	                                   const std::string& markerName);

	// --- baselineutil.go ---
	void addResultToBaseline(gostd::testing::T* t,
	                         const baselineCommand& command,
	                         const std::string& actual);
	void writeToBaseline(const baselineCommand& command,
	                     const std::string& content);
	testutil::baseline::Options getBaselineOptions(
	    const baselineCommand& command, const std::string& testPath);
	std::string getBaselineForLocationsWithFileContents(
	    const std::vector<lsproto::Location>& locations,
	    const baselineFourslashLocationsOptions& options);
	std::string getBaselineForSpansWithFileContents(
	    const std::vector<documentSpan>& spans,
	    const baselineFourslashLocationsOptions& options);
	std::string getBaselineForGroupedSpansWithFileContents(
	    collections::MultiMap<lsproto::DocumentUri, documentSpan>*
	        groupedRanges,
	    const baselineFourslashLocationsOptions& options);
	std::pair<std::string, bool> textOfFile(const std::string& fileName);
	std::string getBaselineContentForFile(
	    const std::string& fileName, const std::string& content,
	    const std::vector<documentSpan>& spansInFile,
	    std::unordered_map<documentSpan, int, documentSpanHash>*
	        spanToContextId,
	    const baselineFourslashLocationsOptions& options);
	// getRange/getTooltipLines are templated callables (std::function
	// params would block T deduction from lambdas).
	template <typename T, typename GetRange, typename GetTooltipLines>
	std::string annotateContentWithTooltips(
	    gostd::testing::T* t,
	    const std::vector<markerAndItem<T>>& markersAndItems,
	    const std::string& opName,
	    GetRange&& getRange,
	    GetTooltipLines&& getTooltipLines);

	// --- statebaseline.go ---
	void baselineRequestOrNotificationWorker(gostd::testing::T* t,
	                                         const std::string& rawJson);
	template <typename P>
	void baselineRequestOrNotification(
	    gostd::testing::T* t, const lsproto::Method& method,
	    const P& params) {
		// baselineRequestOrNotification — statebaseline.go:53.
		t->Helper();
		if (!testData->isStateBaseliningEnabled()) {
			return;
		}
		// requestOrMessage{Method: method, Params: params}
		auto paramsJson = json::marshal(params).first;
		auto methodJson = json::marshal(std::string(method)).first;
		std::string raw = std::string("{\"method\":") + methodJson +
		                  ",\"params\":" +
		                  (paramsJson.empty() ? "null" : paramsJson) +
		                  "}";
		baselineRequestOrNotificationWorker(t, raw);
	}
	void baselineProjectsAfterNotification(gostd::testing::T* t,
	                                       const std::string& fileName);
	void baselineState(gostd::testing::T* t);
	std::string serializedState(gostd::testing::T* t);
	void printStateDiff(gostd::testing::T* t, gostd::io::Writer* w);
	void printProjectsDiff(gostd::testing::T* t,
	                       project::Snapshot* snapshot,
	                       gostd::io::Writer* w);
	void printOpenFilesDiff(gostd::testing::T* t,
	                        project::Snapshot* snapshot,
	                        gostd::io::Writer* w);
	void printConfigFileRegistryDiff(
	    gostd::testing::T* t, project::Snapshot* snapshot,
	    gostd::io::Writer* w);

	// --- semantictokens.go ---
	void VerifySemanticTokens(
	    gostd::testing::T* t,
	    const std::vector<SemanticToken>& expected);
};

// ===========================================================================
// Free functions — fourslash.go
// ===========================================================================

// rootDir — fourslash.go:152.
inline constexpr std::string_view rootDir = "/";

// parseCache — fourslash.go:154.
std::shared_ptr<project::ParseCache> parseCache();

// NewFourslash — fourslash.go:160.
std::pair<std::shared_ptr<FourslashTest>, std::function<void()>> NewFourslash(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::ClientCapabilities>& capabilities,
    const std::string& content);

// FourslashOptions — fourslash.go:165.
struct FourslashOptions {
	std::shared_ptr<lsproto::ClientCapabilities> Capabilities;
	std::shared_ptr<contentmapper::Spawner> ContentMapperSpawner;
	bool RunExternalCode = false;
};

// NewFourslashWithOptions — fourslash.go:171.
std::pair<std::shared_ptr<FourslashTest>, std::function<void()>>
NewFourslashWithOptions(gostd::testing::T* t, const std::string& content,
                        const FourslashOptions* options);

// newFourslash — fourslash.go:176. testPath mirrors Go's
// runtime.Caller(1) result (only used for baseline options plumbing).
std::pair<std::shared_ptr<FourslashTest>, std::function<void()>>
newFourslash(gostd::testing::T* t, const std::string& content,
             std::shared_ptr<FourslashOptions> options,
             const std::string& testPath);

// getBaseFileNameFromTest — fourslash.go:343.
std::string getBaseFileNameFromTest(gostd::testing::T* t);

// showCodeLensLocationsCommandName — fourslash.go:366.
inline constexpr std::string_view showCodeLensLocationsCommandName =
    "typescript.showCodeLensLocations";

// defaultSemanticTokenTypes / defaultSemanticTokenModifiers —
// fourslash.go:397,425.
std::vector<std::string> defaultSemanticTokenTypes();
std::vector<std::string> defaultSemanticTokenModifiers();

// GetDefaultCapabilities — fourslash.go:526.
std::shared_ptr<lsproto::ClientCapabilities> GetDefaultCapabilities();

// ClientCapabilitiesOptions — fourslash.go:620.
struct ClientCapabilitiesOptions {
	std::shared_ptr<lsproto::ClientCompletionItemOptions> CompletionItem;
};

// GetDefaultCapabilitiesWithOptions — fourslash.go:624.
std::shared_ptr<lsproto::ClientCapabilities>
GetDefaultCapabilitiesWithOptions(const ClientCapabilitiesOptions* options);

// getCapabilitiesWithDefaults — fourslash.go:666.
std::shared_ptr<lsproto::ClientCapabilities> getCapabilitiesWithDefaults(
    const std::shared_ptr<lsproto::ClientCapabilities>& capabilities);

// getLanguageKind — fourslash.go:1086.
lsproto::LanguageKind getLanguageKind(const std::string& filename);

// isEmptyExpectedList — fourslash.go:1371.
bool isEmptyExpectedList(const CompletionsExpectedList* expected);

// findJSDocCompletionItem — fourslash.go:1302.
std::shared_ptr<lsproto::CompletionItem> findJSDocCompletionItem(
    const std::shared_ptr<lsproto::CompletionList>& list);

// verifyCompletionsItemDefaults — fourslash.go:1375.
void verifyCompletionsItemDefaults(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::CompletionItemDefaults>& actual,
    const CompletionsExpectedItemDefaults* expected,
    const std::string& prefix);

// ignorePaths — fourslash.go:1565.
tsc::cmp::Option ignorePaths(std::vector<std::string> paths);

// assertDeepEqual — fourslash.go:1661.
template <typename T, typename U>
void assertDeepEqual(gostd::testing::T* t, const T& actual, const U& expected,
                     const std::string& prefix,
                     const std::vector<tsc::cmp::Option>& opts);

// extractModuleSpecifier — fourslash.go:2487.
std::string extractModuleSpecifier(const std::string& text);

// renderVSContainerElement / appendLinesForMarkedStringWithLanguage /
// hoverContentString — fourslash.go:3032,3054,3067.
std::vector<std::string> renderVSContainerElement(
    const std::shared_ptr<lsproto::VSContainerElement>& el,
    const std::string& indent);
std::vector<std::string> appendLinesForMarkedStringWithLanguage(
    std::vector<std::string> result,
    const std::shared_ptr<lsproto::MarkedStringWithLanguage>& ms);
std::string hoverContentString(
    const std::shared_ptr<lsproto::Hover>& hover);

// symbolKindToLowercase / formatCallHierarchyItem /
// formatCallHierarchyItemSpan / formatCallHierarchyItemSpans /
// computeLineStarts — fourslash.go:3460,3464,3609,3719.
std::string symbolKindToLowercase(lsproto::SymbolKind kind);
void formatCallHierarchyItem(
    gostd::testing::T* t, FourslashTest* f, scriptInfo* file,
    gostr::Builder* result,
    const lsproto::CallHierarchyItem& callHierarchyItem,
    callHierarchyItemDirection direction,
    std::unordered_map<callHierarchyItemKey, bool, callHierarchyItemKeyHash>&
        seen,
    const std::string& prefix);
void formatCallHierarchyItemSpan(FourslashTest* f, scriptInfo* file,
                                 gostr::Builder* result,
                                 const lsproto::Range& span,
                                 const std::string& indent,
                                 const std::string& endIndent);
std::vector<int> computeLineStarts(const std::string& content);
void formatCallHierarchyItemSpans(
    FourslashTest* f, scriptInfo* file, gostr::Builder* result,
    const std::vector<lsproto::Range>& spans, const std::string& prefix,
    const std::string& trailingPrefix);

// roundtripThroughJson — fourslash.go:3873.
template <typename T>
std::pair<T, gostd::Error> roundtripThroughJson(
    const lsproto::LSPAny& value) {
	T result;
	auto [bytes, marshalErr] = json::marshal(value);
	if (!marshalErr.empty()) {
		return {result, gostd::errorf(
		                    "failed to marshal value to JSON: %w",
		                    {gostd::newError(marshalErr)})};
	}
	T out;
	auto unmarshalErr = json::unmarshal(bytes, &out);
	if (!unmarshalErr.empty()) {
		return {result, gostd::errorf(
		                    "failed to unmarshal value from JSON: %w",
		                    {gostd::newError(unmarshalErr)})};
	}
	return {std::move(out), nullptr};
}

// verifyExactSymbols / verifyIncludesSymbols —
// fourslash.go:5754,5768.
void verifyExactSymbols(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<lsproto::SymbolInformation>>& actual,
    const std::vector<std::shared_ptr<lsproto::SymbolInformation>>& expected,
    const std::string& prefix);
void verifyIncludesSymbols(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<lsproto::SymbolInformation>>& actual,
    const std::vector<std::shared_ptr<lsproto::SymbolInformation>>& includes,
    const std::string& prefix);

// writeDocumentSymbolDetails / collectDocumentSymbolSpans —
// fourslash.go:5828,5837.
void writeDocumentSymbolDetails(
    const std::vector<std::shared_ptr<lsproto::DocumentSymbol>>& symbols,
    int indent, gostr::Builder* builder);

// documentSpanKey — fourslash.go:5860.
struct documentSpanKey {
	lsproto::DocumentUri uri;
	lsproto::Range textSpan;
	lsproto::Range contextSpan;

	bool operator==(const documentSpanKey&) const = default;
};
struct documentSpanKeyHash {
	size_t operator()(const documentSpanKey& k) const {
		return std::hash<std::string>{}(k.uri) ^
		       (std::hash<uint32_t>{}(k.textSpan.Start.Line) << 1) ^
		       (std::hash<uint32_t>{}(k.textSpan.Start.Character) << 2) ^
		       (std::hash<uint32_t>{}(k.textSpan.End.Line) << 3) ^
		       (std::hash<uint32_t>{}(k.textSpan.End.Character) << 4) ^
		       (std::hash<uint32_t>{}(k.contextSpan.Start.Line) << 5) ^
		       (std::hash<uint32_t>{}(k.contextSpan.Start.Character) << 6);
	}
};
void collectDocumentSymbolSpans(
    const lsproto::DocumentUri& uri,
    const std::shared_ptr<lsproto::DocumentSymbol>& symbol,
    std::unordered_map<documentSpanKey,
                       std::shared_ptr<lsproto::DocumentSymbol>,
                       documentSpanKeyHash>& symbolBySpan);

// isSuggestionDiagnostic — fourslash.go:5481.
bool isSuggestionDiagnostic(
    const std::shared_ptr<lsproto::Diagnostic>& diag);

// compareDiagnostics / compareRelatedDiagnostics —
// fourslash.go:5643,5667.
int compareDiagnostics(const fourslashDiagnostic* d1,
                       const fourslashDiagnostic* d2);
int compareRelatedDiagnostics(
    const std::vector<std::shared_ptr<fourslashDiagnostic>>& d1,
    const std::vector<std::shared_ptr<fourslashDiagnostic>>& d2);

// isLibFile — fourslash.go:5681.
bool isLibFile(const std::string& fileName);

// AnyTextEdits / NoTextEdits — fourslash.go:5690-5691 (compared by pointer
// identity).
extern const std::shared_ptr<
    lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>
    AnyTextEdits;
extern const std::shared_ptr<
    lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>
    NoTextEdits;

// updatePosition / updatePositionForTextEdit / removeWhitespace /
// assertValidTextRange / selectCodeFixDiagnostic —
// fourslash.go:4118,6009,6019,6030,6038.
int updatePosition(int pos, int editStart, int editEnd,
                   const std::string& newText);
int updatePositionForTextEdit(int position, int editStart, int editEnd,
                              int newTextLength);
std::string removeWhitespace(const std::string& text);
void assertValidTextRange(gostd::testing::T* t, TextRange textRange,
                          const std::string& message);
std::shared_ptr<lsproto::Diagnostic> selectCodeFixDiagnostic(
    const std::vector<std::shared_ptr<lsproto::Diagnostic>>& diagnostics,
    int errorCode);

// ===========================================================================
// Free functions — baselineutil.go
// ===========================================================================

// locationToSpan — baselineutil.go:134.
documentSpan locationToSpan(const lsproto::Location& loc);

// getAccessibleFilePaths — baselineutil.go:231.
std::vector<std::string> getAccessibleFilePaths(
    ::tsc::vfs::FS* fileSystem, const std::string& root);

// uniqueFilesInSpanOrder — baselineutil.go:251.
std::vector<lsproto::DocumentUri> uniqueFilesInSpanOrder(
    const std::vector<documentSpan>& spans);

// codeFence — baselineutil.go:797.
std::string codeFence(const std::string& lang, const std::string& code);

// getBaselineFileName / getBaselineExtension /
// dropTrailingEmptyLines / normalizeCommandName —
// baselineutil.go:77,81,102,106.
std::string getBaselineFileName(gostd::testing::T* t,
                                const baselineCommand& command);
std::string getBaselineExtension(const baselineCommand& command);
std::vector<std::string> dropTrailingEmptyLines(
    const std::vector<std::string>& ss);
std::string normalizeCommandName(const std::string& command);

// ===========================================================================
// Free functions — semantictokens.go
// ===========================================================================

// decodeSemanticTokens / formatSemanticTokens —
// semantictokens.go:55,124.
std::vector<SemanticToken> decodeSemanticTokens(
    FourslashTest* f, const std::vector<uint32_t>& data,
    const std::vector<std::string>& tokenTypes,
    const std::vector<std::string>& tokenModifiers);
std::string formatSemanticTokens(const std::vector<SemanticToken>& tokens);


// assertDeepEqual — fourslash.go:1666. Go:
//   assertDeepEqual(t, actual any, expected any, prefix string,
//                   opts ...cmp.Option)
// Path-ignored options are merged (all fourslash uses are
// ignorePaths-based).
template <typename T, typename U>
void assertDeepEqual(gostd::testing::T* t, const T& actual,
                     const U& expected, const std::string& prefix,
                     std::initializer_list<tsc::cmp::Option> opts = {}) {
	t->Helper();
	tsc::cmp::Option merged;
	for (auto& o : opts) {
		merged.insert(o.begin(), o.end());
	}
	std::string diff = tsc::cmp::diff(actual, expected, merged);
	if (!diff.empty()) {
		t->Fatalf("%s:\n%s", {prefix, diff});
	}
}

// annotateContentWithTooltips — baselineutil.go:671.
template <typename T, typename GetRange, typename GetTooltipLines>
std::string FourslashTest::annotateContentWithTooltips(
    gostd::testing::T* t,
    const std::vector<markerAndItem<T>>& markersAndItems,
    const std::string& opName,
    GetRange&& getRange,
    GetTooltipLines&& getTooltipLines) {
	t->Helper();
	std::string barWithGutter =
	    "| " + gostr::repeat("-", 70);

	// sort by file, then *backwards* by position in the file
	// so we can insert multiple times on a line without counting.
	auto sorted = markersAndItems;
	std::stable_sort(sorted.begin(), sorted.end(),
	                 [](const markerAndItem<T>& a,
	                    const markerAndItem<T>& b) {
		                 if (a.Marker->FileName() != b.Marker->FileName()) {
			                 return a.Marker->FileName() <
			                        b.Marker->FileName();
		                 }
		                 return a.Marker->Position >
		                        b.Marker->Position;
	                 });

	collections::OrderedMap<std::string, std::vector<std::string>>
	    filesToLines(1);
	T previous{};
	for (auto& itemAndMarker : sorted) {
		auto& marker = itemAndMarker.Marker;
		auto& item = itemAndMarker.Item;

		auto textRangeResult = getRange(item);
		lsproto::Range caretRange;
		const lsproto::Range* textRange = nullptr;
		if (textRangeResult == nullptr) {
			auto start = marker->LSPosition;
			auto end = start;
			end.Character = end.Character + 1;
			caretRange.Start = start;
			caretRange.End = end;
			textRange = &caretRange;
		} else {
			textRange = textRangeResult.get();
		}

		if (textRange->Start.Line != textRange->End.Line) {
			t->Fatalf(
			    "Expected text range to be on a single line",
			    {});
		}
		std::string underline =
		    gostr::repeat(" ", (int)textRange->Start.Character) +
		    gostr::repeat("^",
		                  (int)(textRange->End.Character -
		                        textRange->Start.Character));

		const std::string& fileName = marker->FileName();
		auto linesEntry = filesToLines.Get(fileName);
		std::vector<std::string> lines;
		if (linesEntry.second) {
			lines = *linesEntry.first;
		} else {
			lines = splitIntoLines(
			    getScriptInfo(fileName)->content);
		}

		std::vector<std::string> tooltipLines;
		if (item != T{}) {
			tooltipLines = getTooltipLines(item, previous);
		}
		if (tooltipLines.empty()) {
			tooltipLines = {gostd::sprintf(
			    "No %s at /*%s*/.", {opName, *marker->Name})};
		}
		tooltipLines = gostr::coreMap(
		    tooltipLines, [](const std::string& line) {
			    return "| " + line;
		    });

		std::vector<std::string> linesToInsert(
		    tooltipLines.size() + 3);
		linesToInsert[0] = underline;
		linesToInsert[1] = barWithGutter;
		std::copy(tooltipLines.begin(), tooltipLines.end(),
		          linesToInsert.begin() + 2);
		linesToInsert[linesToInsert.size() - 1] = barWithGutter;

		lines.insert(lines.begin() + (int)textRange->Start.Line + 1,
		             linesToInsert.begin(), linesToInsert.end());
		filesToLines.Set(fileName, std::move(lines));

		previous = item;
	}

	gostr::Builder builder;
	bool seenFirst = false;
	for (auto& fileName : filesToLines.Keys()) {
		builder.WriteString(gostd::sprintf(
		    "=== %s ===\n", {fileName}));
		for (auto& line : filesToLines.GetOrZero(fileName)) {
			builder.WriteString("// ");
			builder.WriteString(line);
			builder.WriteByte('\n');
		}

		if (seenFirst) {
			builder.WriteString("\n\n");
		} else {
			seenFirst = true;
		}
	}

	return builder.String();
}

} // namespace tsc::fourslash

// ===========================================================================
// Template implementations
// ===========================================================================
namespace tsc::fourslash {

template <typename Params, typename Resp>
Resp FourslashTest::sendRequest(
    gostd::testing::T* t, const lsproto::RequestInfo<Params, Resp>& info,
    const Params& params) {
	t->Helper();
	return sendRequestAndBaselineWorker(t, info, params, true);
}

template <typename Params>
void FourslashTest::sendNotification(
    gostd::testing::T* t, const lsproto::NotificationInfo<Params>& info,
    const Params& params) {
	t->Helper();
	if (info.Method != lsproto::MethodTextDocumentDidChange) {
		// This is called eg when doing typeText = which is series of edits
		// and formatting - which becomes non deterministic "after state"
		// The notification can only guarantee before state and thats what
		// it baselines, but in case of type it creates
		// multiple edits which results in getting different state -based
		// on if the snapshot was updated or not at the time of formatting
		// requests
		// So this is used for all the incremental edits - to baseline only
		// request data but not project state between those edits
		baselineState(t);
		updateState(info.Method, params);
	}
	baselineRequestOrNotification(t, info.Method, params);
	client->SendNotification(t, info, params);
}

// sendRequestAndBaselineWorker — fourslash.go:744.
template <typename Params, typename Resp>
Resp FourslashTest::sendRequestAndBaselineWorker(
    gostd::testing::T* t, const lsproto::RequestInfo<Params, Resp>& info,
    const Params& params, bool baselineProjects) {
	t->Helper();
	std::string prefix = getCurrentPositionPrefix();
	if (baselineProjects) {
		baselineState(t);
	}
	baselineRequestOrNotification(t, info.Method, params);
	auto [resMsg, result, resultOk] =
	    client->SendRequest(t, info, params);
	if (baselineProjects) {
		baselineState(t);
	}
	bool checkResult = true;
	if (info.Method == lsproto::MethodTextDocumentOnTypeFormatting &&
	    !reportFormatOnTypeCrash) {
		checkResult = false;
	}
	if (checkResult) {
		if (resMsg == nullptr) {
			t->Fatalf(prefix + "Nil response received for %s request",
			          {info.Method});
		}
		auto resp = resMsg->AsResponse();
		if (resp->Error != nullptr) {
			t->Fatalf(prefix + "%s request returned error: %s",
			          {info.Method, resp->Error->String()});
		}
		if (!resultOk) {
			t->Fatalf(
			    prefix +
			        "Unexpected %s response type: %T, error: %v",
			    {info.Method, resp->Result.isSet() ? "set" : "<nil>",
			     resp->Error ? resp->Error->String() : "<nil>"});
		}
	}
	return result;
}

// updateState — fourslash.go:790.
template <typename Params>
void FourslashTest::updateState(const lsproto::Method& method,
                                const Params& params) {
	if constexpr (std::is_same_v<
	                  Params,
	                  std::shared_ptr<
	                      lsproto::DidOpenTextDocumentParams>>) {
		if (method == lsproto::MethodTextDocumentDidOpen) {
			openFiles.insert(lsproto::documentUriFileName(
			    params->TextDocument->Uri));
		}
	} else if constexpr (
	    std::is_same_v<
	        Params,
	        std::shared_ptr<lsproto::DidCloseTextDocumentParams>>) {
		if (method == lsproto::MethodTextDocumentDidClose) {
			openFiles.erase(lsproto::documentUriFileName(
			    params->TextDocument.Uri));
		}
	}
}



} // namespace tsc::fourslash
