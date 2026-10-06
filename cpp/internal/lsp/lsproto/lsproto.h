// Minimal subset of tsc/internal/lsp/lsproto needed by the ls-coreA slice
// (completions + friends). === slice: ls-coreA === — replace with the full
// generated port when the lsp slice lands; names/field order follow
// lsp_generated.go / lsp.go exactly.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "internal/core/context.h"
#include "internal/core/types.h"
#include "internal/tspath/tspath.h"

namespace tsc::lsproto {

// === scalars / enums ===

// lsp_generated.go:10241
using PositionEncodingKind = std::string;
inline const PositionEncodingKind PositionEncodingKindUTF8{"utf-8"};
inline const PositionEncodingKind PositionEncodingKindUTF16{"utf-16"};
inline const PositionEncodingKind PositionEncodingKindUTF32{"utf-32"};

// lsp_generated.go:10132
using MarkupKind = std::string;
inline const MarkupKind MarkupKindPlainText{"plaintext"};
inline const MarkupKind MarkupKindMarkdown{"markdown"};

// lsp_generated.go:9874
using CompletionItemKind = uint32_t;
inline constexpr CompletionItemKind CompletionItemKindText = 1;
inline constexpr CompletionItemKind CompletionItemKindMethod = 2;
inline constexpr CompletionItemKind CompletionItemKindFunction = 3;
inline constexpr CompletionItemKind CompletionItemKindConstructor = 4;
inline constexpr CompletionItemKind CompletionItemKindField = 5;
inline constexpr CompletionItemKind CompletionItemKindVariable = 6;
inline constexpr CompletionItemKind CompletionItemKindClass = 7;
inline constexpr CompletionItemKind CompletionItemKindInterface = 8;
inline constexpr CompletionItemKind CompletionItemKindModule = 9;
inline constexpr CompletionItemKind CompletionItemKindProperty = 10;
inline constexpr CompletionItemKind CompletionItemKindUnit = 11;
inline constexpr CompletionItemKind CompletionItemKindValue = 12;
inline constexpr CompletionItemKind CompletionItemKindEnum = 13;
inline constexpr CompletionItemKind CompletionItemKindKeyword = 14;
inline constexpr CompletionItemKind CompletionItemKindSnippet = 15;
inline constexpr CompletionItemKind CompletionItemKindColor = 16;
inline constexpr CompletionItemKind CompletionItemKindFile = 17;
inline constexpr CompletionItemKind CompletionItemKindReference = 18;
inline constexpr CompletionItemKind CompletionItemKindFolder = 19;
inline constexpr CompletionItemKind CompletionItemKindEnumMember = 20;
inline constexpr CompletionItemKind CompletionItemKindConstant = 21;
inline constexpr CompletionItemKind CompletionItemKindStruct = 22;
inline constexpr CompletionItemKind CompletionItemKindEvent = 23;
inline constexpr CompletionItemKind CompletionItemKindOperator = 24;
inline constexpr CompletionItemKind CompletionItemKindTypeParameter = 25;

// lsp_generated.go:9920
using CompletionItemTag = uint32_t;
inline constexpr CompletionItemTag CompletionItemTagDeprecated = 1;

// lsp_generated.go:9941
using InsertTextFormat = uint32_t;
inline constexpr InsertTextFormat InsertTextFormatPlainText = 1;
inline constexpr InsertTextFormat InsertTextFormatSnippet = 2;

// lsp_generated.go:9973
using InsertTextMode = uint32_t;
inline constexpr InsertTextMode InsertTextModeAsIs = 1;
inline constexpr InsertTextMode InsertTextModeAdjustIndentation = 2;

// lsp_generated.go:10374
using CompletionTriggerKind = uint32_t;
inline constexpr CompletionTriggerKind CompletionTriggerKindInvoked = 1;
inline constexpr CompletionTriggerKind CompletionTriggerKindTriggerCharacter = 2;
inline constexpr CompletionTriggerKind CompletionTriggerKindTriggerForIncompleteCompletions = 3;

// lsp_generated.go:10403
using ApplyKind = uint32_t;
inline constexpr ApplyKind ApplyKindReplace = 1;
inline constexpr ApplyKind ApplyKindMerge = 2;

// lsp_generated.go:10667
using AutoImportFixKind = int32_t;
inline constexpr AutoImportFixKind AutoImportFixKindUseNamespace = 0;
inline constexpr AutoImportFixKind AutoImportFixKindJsdocTypeImport = 1;
inline constexpr AutoImportFixKind AutoImportFixKindAddToExisting = 2;
inline constexpr AutoImportFixKind AutoImportFixKindAddNew = 3;
inline constexpr AutoImportFixKind AutoImportFixKindPromoteTypeOnly = 4;

// lsp_generated.go:10694
using ImportKind = int32_t;
inline constexpr ImportKind ImportKindNamed = 0;
inline constexpr ImportKind ImportKindDefault = 1;
inline constexpr ImportKind ImportKindNamespace = 2;
inline constexpr ImportKind ImportKindCommonJS = 3;

// lsp_generated.go (AddAsTypeOnly, adjacent to ImportKind)
using AddAsTypeOnly = int32_t;
inline constexpr AddAsTypeOnly AddAsTypeOnlyAllowed = 1;
inline constexpr AddAsTypeOnly AddAsTypeOnlyRequired = 2;
inline constexpr AddAsTypeOnly AddAsTypeOnlyNotAllowed = 4;

// lsp_generated.go:10750 — Roslyn classification type names used by VS.
using ClassificationTypeName = std::string;
inline const ClassificationTypeName ClassificationTypeNameKeyword{"keyword"};
inline const ClassificationTypeName ClassificationTypeNamePunctuation{"punctuation"};
inline const ClassificationTypeName ClassificationTypeNameOperator{"operator"};
inline const ClassificationTypeName ClassificationTypeNameWhiteSpace{"whitespace"};
inline const ClassificationTypeName ClassificationTypeNameText{"text"};
inline const ClassificationTypeName ClassificationTypeNameString{"string"};
inline const ClassificationTypeName ClassificationTypeNameNumber{"number"};
inline const ClassificationTypeName ClassificationTypeNameComment{"comment"};
inline const ClassificationTypeName ClassificationTypeNameClassName{"class name"};
inline const ClassificationTypeName ClassificationTypeNameInterfaceName{"interface name"};
inline const ClassificationTypeName ClassificationTypeNameEnumName{"enum name"};
inline const ClassificationTypeName ClassificationTypeNameModuleName{"module name"};
inline const ClassificationTypeName ClassificationTypeNameMethodName{"method name"};
inline const ClassificationTypeName ClassificationTypeNameParameterName{"parameter name"};
inline const ClassificationTypeName ClassificationTypeNamePropertyName{"property name"};
inline const ClassificationTypeName ClassificationTypeNameFieldName{"field name"};
inline const ClassificationTypeName ClassificationTypeNameLocalName{"local name"};
inline const ClassificationTypeName ClassificationTypeNameTypeParameterName{"type parameter name"};
inline const ClassificationTypeName ClassificationTypeNameIdentifier{"identifier"};

// === documents ===

// lsp.go:17 — `type DocumentUri string`.
struct DocumentUri {
	std::string Uri;
	DocumentUri() = default;
	DocumentUri(std::string u) : Uri(std::move(u)) {}
	DocumentUri(const char* u) : Uri(u) {}
	// lsp.go:20 FileName — decodes the URI to a file name.
	std::string FileName() const;
	// lsp.go Path.
	tspath::Path Path(bool useCaseSensitiveFileNames) const;
	bool operator==(const DocumentUri&) const = default;

	// === slice: api === — Go `type DocumentUri string` conveniences.
	const std::string& str() const { return Uri; }
	bool empty() const { return Uri.empty(); }
	operator const std::string&() const { return Uri; }
};

// lsp_generated.go:3902
struct Position {
	uint32_t Line = 0;
	uint32_t Character = 0;

	int Compare(const Position* other) const {
		if (Line != other->Line) {
			return Line < other->Line ? -1 : 1;
		}
		if (Character != other->Character) {
			return Character < other->Character ? -1 : 1;
		}
		return 0;
	}
	bool operator==(const Position&) const = default;
};

// lsp_generated.go:3728
struct Range {
	Position Start;
	Position End;

	int Compare(const Range* other) const {
		if (int c = Start.Compare(&other->Start); c != 0) {
			return c;
		}
		return End.Compare(&other->End);
	}
	bool operator==(const Range&) const = default;
};

// lsp_generated.go:48
struct Location {
	DocumentUri Uri;
	Range Range;

	Location GetLocation() const { return *this; }
};

// lsp_generated.go:1998
struct TextEdit {
	Range Range;
	std::string NewText;

	int Compare(const TextEdit* other) const {
		if (int c = Range.Compare(&other->Range); c != 0) {
			return c;
		}
		if (NewText != other->NewText) {
			return NewText < other->NewText ? -1 : 1;
		}
		return 0;
	}
};

// lsp_generated.go:5565
struct InsertReplaceEdit {
	std::string NewText;
	tsc::lsproto::Range Insert;
	tsc::lsproto::Range Replace;
};

// lsp_generated.go:12208 — union: TextEdit | InsertReplaceEdit
struct TextEditOrInsertReplaceEdit {
	TextEdit* TextEdit = nullptr;
	InsertReplaceEdit* InsertReplaceEdit = nullptr;
};

// lsp_generated.go:6698
struct EditRangeWithInsertReplace {
	Range Insert;
	Range Replace;
};

// lsp_generated.go:13352 — union: Range | EditRangeWithInsertReplace
struct RangeOrEditRangeWithInsertReplace {
	Range* Range = nullptr;
	EditRangeWithInsertReplace* EditRangeWithInsertReplace = nullptr;
};

// lsp_generated.go:4367
struct MarkupContent {
	MarkupKind Kind;
	std::string Value;
};

// lsp_generated.go:11929 — union: string | MarkupContent
struct StringOrMarkupContent {
	std::string* String = nullptr;
	MarkupContent* MarkupContent = nullptr;
};

// lsp_generated.go:2834 (subset — Arguments `[]any` unused by ls)
struct Command {
	std::string Title;
	std::string* Tooltip = nullptr;
	std::string command; // Go field `Command` renamed (member==class name is illegal)
};

// lsp_generated.go:8811
struct AutoImportFix {
	AutoImportFixKind Kind = AutoImportFixKindUseNamespace;
	std::string Name;
	ImportKind ImportKind = lsproto::ImportKindNamed;
	bool UseRequire = false;
	AddAsTypeOnly AddAsTypeOnly = lsproto::AddAsTypeOnlyAllowed;
	std::string ModuleSpecifier;
	int32_t ImportIndex = 0;
	Position* UsagePosition = nullptr;
	std::string NamespacePrefix;
};

// lsp_generated.go:8840
struct CompletionItemData {
	std::string FileName;
	int32_t Position = 0;
	int32_t* SupplementalFileIndex = nullptr;
	std::string Source;
	std::string Name;
	AutoImportFix* AutoImport = nullptr;
	bool IsImportStatementCompletion = false;
};

// lsp_generated.go:5528
struct CompletionContext {
	CompletionTriggerKind TriggerKind = CompletionTriggerKindInvoked;
	std::string* TriggerCharacter = nullptr;
};

// lsp_generated.go:5546
struct CompletionItemLabelDetails {
	std::string* Detail = nullptr;
	std::string* Description = nullptr;
};

// lsp_generated.go:9508 — placeholder for custom data on defaults.
struct CompletionItemDefaultsData {};

// lsp_generated.go:5597
struct CompletionItemDefaults {
	std::vector<std::string>* CommitCharacters = nullptr;
	RangeOrEditRangeWithInsertReplace* EditRange = nullptr;
	InsertTextFormat* InsertTextFormat = nullptr;
	InsertTextMode* InsertTextMode = nullptr;
	CompletionItemDefaultsData* Data = nullptr;
};

// lsp_generated.go:5647
struct CompletionItemApplyKinds {
	ApplyKind* CommitCharacters = nullptr;
	ApplyKind* Data = nullptr;
};

// lsp_generated.go:2101
struct CompletionItem {
	std::string Label;
	CompletionItemLabelDetails* LabelDetails = nullptr;
	CompletionItemKind* Kind = nullptr;
	std::vector<CompletionItemTag>* Tags = nullptr;
	std::string* Detail = nullptr;
	StringOrMarkupContent* Documentation = nullptr;
	bool* Deprecated = nullptr;
	bool* Preselect = nullptr;
	std::string* SortText = nullptr;
	std::string* FilterText = nullptr;
	std::string* InsertText = nullptr;
	tsc::lsproto::InsertTextFormat* InsertTextFormat = nullptr;
	tsc::lsproto::InsertTextMode* InsertTextMode = nullptr;
	TextEditOrInsertReplaceEdit* TextEdit = nullptr;
	std::string* TextEditText = nullptr;
	std::vector<tsc::lsproto::TextEdit*>* AdditionalTextEdits = nullptr;
	std::vector<std::string>* CommitCharacters = nullptr;
	Command* Command_ = nullptr; // Go field `Command` renamed
	CompletionItemData* Data = nullptr;
};

// lsp_generated.go:2248
struct CompletionList {
	bool IsIncomplete = false;
	CompletionItemDefaults* ItemDefaults = nullptr;
	CompletionItemApplyKinds* ApplyKind = nullptr;
	std::vector<CompletionItem*> Items;
};

// lsp_generated.go:14168 — union: []*CompletionItem | CompletionList | null
struct CompletionItemsOrListOrNull {
	std::vector<CompletionItem*>* Items = nullptr;
	CompletionList* List = nullptr;
};

// lsp_generated.go:11458
using CompletionResponse = CompletionItemsOrListOrNull;

// lsp_generated.go:9397
struct VSClassifiedTextRun {
	std::string ClassificationTypeName;
	std::string Text;
	std::string* MarkerTagType = nullptr;
	int32_t Style = 0;
	std::string VSType{"ClassifiedTextRun"};
};

// === resolved client capabilities (subset: only fields ls reads) ===

// ResolvedCompletionItemTagOptions (subset)
struct ResolvedCompletionItemTagOptions {
	std::vector<CompletionItemTag> ValueSet;
};

// ResolvedClientCompletionItemResolveOptions (subset)
struct ResolvedClientCompletionItemResolveOptions {
	std::vector<std::string> Properties;
};

// ResolvedClientCompletionItemInsertTextModeOptions (subset)
struct ResolvedClientCompletionItemInsertTextModeOptions {
	std::vector<InsertTextMode> Supported;
};

// lsp_generated.go:15863 ResolvedClientCompletionItemOptions
struct ResolvedClientCompletionItemOptions {
	bool SnippetSupport = false;
	bool CommitCharactersSupport = false;
	std::vector<MarkupKind> DocumentationFormat;
	bool DeprecatedSupport = false;
	bool PreselectSupport = false;
	ResolvedCompletionItemTagOptions TagSupport;
	bool InsertReplaceSupport = false;
	ResolvedClientCompletionItemResolveOptions ResolveSupport;
	ResolvedClientCompletionItemInsertTextModeOptions InsertTextModeSupport;
	bool LabelDetailsSupport = false;
};

// ResolvedClientCompletionItemOptionsKind
struct ResolvedClientCompletionItemOptionsKind {
	std::vector<CompletionItemKind> ValueSet;
};

// lsp_generated.go:15970 ResolvedCompletionListCapabilities
struct ResolvedCompletionListCapabilities {
	std::vector<std::string> ItemDefaults;
	bool ApplyKindSupport = false;
};

// lsp_generated.go:16003 ResolvedCompletionClientCapabilities
struct ResolvedCompletionClientCapabilities {
	bool DynamicRegistration = false;
	ResolvedClientCompletionItemOptions CompletionItem;
	ResolvedClientCompletionItemOptionsKind CompletionItemKind;
	InsertTextMode InsertTextMode = InsertTextModeAsIs;
	bool ContextSupport = false;
	ResolvedCompletionListCapabilities CompletionList;
};

// ResolvedTextDocumentClientCapabilities — subset; other features are owned by
// their own slices and added as they land.
struct ResolvedTextDocumentClientCapabilities {
	ResolvedCompletionClientCapabilities Completion;
};

// ResolvedWorkspaceClientCapabilities — placeholders (other slices extend).
struct ResolvedWorkspaceClientCapabilities {};
struct ResolvedWindowClientCapabilities {};
struct ResolvedGeneralClientCapabilities {};
struct ResolvedExperimentalClientCapabilities {};

// lsp_generated.go:17497
struct ResolvedClientCapabilities {
	ResolvedWorkspaceClientCapabilities Workspace;
	ResolvedTextDocumentClientCapabilities TextDocument;
	ResolvedWindowClientCapabilities Window;
	ResolvedGeneralClientCapabilities General;
	ResolvedExperimentalClientCapabilities Experimental;
	bool VSSupportsVisualStudioExtensions = false;
	int32_t VSSupportedSnippetVersion = 0;
	bool VSSupportsNotIncludingTextInTextDocumentDidOpen = false;
	bool VSSupportsIconExtensions = false;
	bool VSSupportsDiagnosticRequests = false;
};

// lsp.go:287 WithClientCapabilities / :293 GetClientCapabilities — ctx key.
const ResolvedClientCapabilities* GetClientCapabilities(const ContextPtr& ctx);
ContextPtr WithClientCapabilities(const ContextPtr& ctx,
                                  const ResolvedClientCapabilities* caps);

// lsp.go:302 PreferredMarkupKind.
MarkupKind PreferredMarkupKind(const std::vector<MarkupKind>& formats);

// === slice: api === — additional lsproto types the api needs
// (lsp_generated.go / lsp.go).

// lsp_generated.go — LanguageKind (document language identifier).
struct LanguageKind {
	std::string v;
	LanguageKind() = default;
	LanguageKind(std::string s) : v(std::move(s)) {}
	LanguageKind(const char* s) : v(s) {}
	const std::string& str() const { return v; }
	bool empty() const { return v.empty(); }
	bool operator==(const LanguageKind&) const = default;
};
inline const LanguageKind LanguageKindTypeScript{"typescript"};
inline const LanguageKind LanguageKindJavaScript{"javascript"};
inline const LanguageKind LanguageKindTypeScriptReact{"typescriptreact"};
inline const LanguageKind LanguageKindJavaScriptReact{"javascriptreact"};
inline const LanguageKind LanguageKindJSON{"json"};
inline const LanguageKind LanguageKindPlaintext{"plaintext"};

// lsp_generated.go — TextDocumentContentChangeEvent variants.
struct TextDocumentContentChangePartial {
	Range range;
	std::optional<uint32_t> rangeLength;
	std::string text;
};
struct TextDocumentContentChangeWholeDocument {
	std::string text;
};
struct TextDocumentContentChangePartialOrWholeDocument {
	std::optional<TextDocumentContentChangePartial> partial;
	std::optional<TextDocumentContentChangeWholeDocument> wholeDocument;
};

// lsp_generated.go — FileChangeType + FileEvent (workspace/didChangeWatchedFiles).
using FileChangeType = int32_t;
inline constexpr FileChangeType FileChangeTypeCreated = 1;
inline constexpr FileChangeType FileChangeTypeChanged = 2;
inline constexpr FileChangeType FileChangeTypeDeleted = 3;

struct FileEvent {
	DocumentUri uri;
	FileChangeType type{};
};

// lsp_generated.go — FormattingOptions (workspace/formatting options).
struct FormattingOptions {
	uint32_t TabSize = 0;
	bool InsertSpaces = false;
	std::optional<bool> TrimTrailingWhitespace;
	std::optional<bool> InsertFinalNewline;
	std::optional<bool> TrimFinalNewlines;
};

// util.go:11 / lsp.go:19-55 — free-function forms used by ls consumers
// (delegate to the member versions).
int ComparePositions(const Position& pos, const Position& other);
int CompareRanges(const Range& lsRange, const Range& other);
std::string documentUriFileName(const DocumentUri& uri);
tspath::Path documentUriPath(const DocumentUri& uri, bool useCaseSensitiveFileNames);

} // namespace tsc::lsproto

// Canonical-name bridge: the real `tsc::lsp::lsproto` port has not merged yet;
// until it does, ls/autoimport/change consumers written against `tsc::lsp::lsproto`
// resolve into this flat namespace.
namespace tsc::lsp {
namespace lsproto = tsc::lsproto;
}

template <>
struct std::hash<tsc::lsproto::DocumentUri> {
	size_t operator()(const tsc::lsproto::DocumentUri& u) const noexcept {
		return std::hash<std::string>()(u.Uri);
	}
};
