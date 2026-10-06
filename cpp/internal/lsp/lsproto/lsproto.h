// === slice: lsproto ===
// lsp_generated.go — minimal lsproto declarations for the ls slice port.
// The Go oracle generates this file from the LSP spec (17.5k lines); only
// the types the ls slice touches are declared here. When the real lsproto
// package lands (lsp slice), replace this header with it.
#pragma once

#include <algorithm>
#include <any>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/gostd/gostd.h"

namespace tsc::lsp::lsproto {

// --- Scalar kinds (string-typed enums) -------------------------------------

using MarkupKind = std::string;
inline const MarkupKind MarkupKindPlainText = "plaintext";
inline const MarkupKind MarkupKindMarkdown = "markdown";
// PreferredMarkupKind — lsp.go.
inline MarkupKind PreferredMarkupKind(const std::vector<MarkupKind>* formats) {
	return (formats != nullptr && !formats->empty()) ? (*formats)[0]
	                                                 : MarkupKindPlainText;
}

using PositionEncodingKind = std::string;
inline const PositionEncodingKind PositionEncodingKindUTF8 = "utf-8";
inline const PositionEncodingKind PositionEncodingKindUTF16 = "utf-16";
inline const PositionEncodingKind PositionEncodingKindUTF32 = "utf-32";

// CodeActionKind — lsp_generated.go (`type CodeActionKind string`).
using CodeActionKind = std::string;
inline const CodeActionKind CodeActionKindEmpty = "";
inline const CodeActionKind CodeActionKindQuickFix = "quickfix";
inline const CodeActionKind CodeActionKindRefactor = "refactor";
inline const CodeActionKind CodeActionKindRefactorExtract = "refactor.extract";
inline const CodeActionKind CodeActionKindRefactorInline = "refactor.inline";
inline const CodeActionKind CodeActionKindRefactorRewrite = "refactor.rewrite";
inline const CodeActionKind CodeActionKindSource = "source";
inline const CodeActionKind CodeActionKindSourceOrganizeImports = "source.organizeImports";
// lsp.go:318 — CodeActionKindSourceOrganizeImports + ".ts"
inline const CodeActionKind CodeActionKindSourceOrganizeImportsTs = "source.organizeImports.ts";
inline const CodeActionKind CodeActionKindSourceFixAll = "source.fixAll";
inline const CodeActionKind CodeActionKindSourceFixAllTs = "source.fixAll.ts";
inline const CodeActionKind CodeActionKindSourceRemoveUnusedTs = "source.removeUnused.ts";
inline const CodeActionKind CodeActionKindSourceAddMissingImportsTs = "source.addMissingImports.ts";
inline const CodeActionKind CodeActionKindSourceFixUnreachableCodeTs = "source.fixUnreachableCode.ts";
inline const CodeActionKind CodeActionKindSourceFixAllImportTs = "source.fixAllImports.ts";
inline const CodeActionKind CodeActionKindSourceMissingDeclarationTs = "source.missingDeclaration.ts";
inline const CodeActionKind CodeActionKindSourceSortImportsTs = "source.sortImports.ts";
inline const CodeActionKind CodeActionKindSourceRemoveUnusedImportsTs = "source.removeUnusedImports.ts";
// (CodeActionKind).Contains — lsp_generated.go
inline bool codeActionKindContains(const CodeActionKind& kind, const CodeActionKind& other) {
	return kind == other || (other.size() > 0 && kind.size() > other.size() &&
	                         kind.compare(0, other.size(), other) == 0 &&
	                         kind[other.size()] == '.');
}

// ClassificationTypeName — lsp_generated.go (`type ClassificationTypeName string`).
using ClassificationTypeName = std::string;
inline const ClassificationTypeName ClassificationTypeComment = "comment";
inline const ClassificationTypeName ClassificationTypeIdentifier = "identifier";
inline const ClassificationTypeName ClassificationTypeKeyword = "keyword";
inline const ClassificationTypeName ClassificationTypeNumericLiteral = "number";
inline const ClassificationTypeName ClassificationTypeOperator = "operator";
inline const ClassificationTypeName ClassificationTypeStringLiteral = "string";
inline const ClassificationTypeName ClassificationTypeRegularExpressionLiteral = "regexp";
inline const ClassificationTypeName ClassificationTypeWhiteSpace = "whitespace";
inline const ClassificationTypeName ClassificationTypeText = "text";
inline const ClassificationTypeName ClassificationTypePunctuation = "punctuation";
inline const ClassificationTypeName ClassificationTypeClassName = "class name";
inline const ClassificationTypeName ClassificationTypeEnumName = "enum name";
inline const ClassificationTypeName ClassificationTypeInterfaceName = "interface name";
inline const ClassificationTypeName ClassificationTypeModuleName = "module name";
inline const ClassificationTypeName ClassificationTypeTypeParameterName = "type parameter name";
inline const ClassificationTypeName ClassificationTypeTypeAliasName = "type alias name";
inline const ClassificationTypeName ClassificationTypeParameterName = "parameter name";
inline const ClassificationTypeName ClassificationTypeDocCommentTagName = "doc comment tag name";
inline const ClassificationTypeName ClassificationTypePropertyName = "property name";
inline const ClassificationTypeName ClassificationTypeMethodName = "method name";
inline const ClassificationTypeName ClassificationTypeLocalName = "local name";
inline const ClassificationTypeName ClassificationTypeFieldName = "field name";

// VSReferenceKind — lsp_generated.go (`type VSReferenceKind int32`).
using VSReferenceKind = int32_t;
inline constexpr VSReferenceKind VSReferenceKindReferences = 0;
inline constexpr VSReferenceKind VSReferenceKindExtension = 1;
inline constexpr VSReferenceKind VSReferenceKindInheritance = 2;
inline constexpr VSReferenceKind VSReferenceKindRead = 3;
inline constexpr VSReferenceKind VSReferenceKindWrite = 4;
inline constexpr VSReferenceKind VSReferenceKindOverload = 5;
inline constexpr VSReferenceKind VSReferenceKindBase = 6;
inline constexpr VSReferenceKind VSReferenceKindReadAndWrite = 7;
inline constexpr VSReferenceKind VSReferenceKindMember = 8;
inline constexpr VSReferenceKind VSReferenceKindOverride = 9;
inline constexpr VSReferenceKind VSReferenceKindInheritor = 10;
inline constexpr VSReferenceKind VSReferenceKindInterfaceImplementation = 11;
inline constexpr VSReferenceKind VSReferenceKindComputedMember = 12;
inline constexpr VSReferenceKind VSReferenceKindTypedElement = 13;
inline constexpr VSReferenceKind VSReferenceKindComputedValue = 14;
inline constexpr VSReferenceKind VSReferenceKindNull = 15;
inline constexpr VSReferenceKind VSReferenceKindInitialize = 16;
inline constexpr VSReferenceKind VSReferenceKindUnknown = 17;

// AutoImportFixKind — lsp_generated.go (`type AutoImportFixKind int32`).
using AutoImportFixKind = int32_t;
inline constexpr AutoImportFixKind AutoImportFixKindUseNamespace = 0;
inline constexpr AutoImportFixKind AutoImportFixKindJsdocTypeImport = 1;
inline constexpr AutoImportFixKind AutoImportFixKindAddToExisting = 2;
inline constexpr AutoImportFixKind AutoImportFixKindAddNew = 3;
inline constexpr AutoImportFixKind AutoImportFixKindPromoteTypeOnly = 4;

// ImportKind — lsp_generated.go (`type ImportKind int32`).
using ImportKind = int32_t;
inline constexpr ImportKind ImportKindNamed = 0;
inline constexpr ImportKind ImportKindDefault = 1;
inline constexpr ImportKind ImportKindNamespace = 2;
inline constexpr ImportKind ImportKindCommonJS = 3;

// AddAsTypeOnly — lsp_generated.go (`type AddAsTypeOnly int32`).
using AddAsTypeOnly = int32_t;
inline constexpr AddAsTypeOnly AddAsTypeOnlyNotApplicable = 0;
inline constexpr AddAsTypeOnly AddAsTypeOnlyAllowed = 1;
inline constexpr AddAsTypeOnly AddAsTypeOnlyRequired = 2;
inline constexpr AddAsTypeOnly AddAsTypeOnlyNotAllowed = 4;
inline constexpr AddAsTypeOnly AddAsTypeOnlyAllowedAndRequired = AddAsTypeOnlyAllowed | AddAsTypeOnlyRequired;

// SignatureHelpTriggerKind — lsp_generated.go.
using SignatureHelpTriggerKind = int32_t;
inline constexpr SignatureHelpTriggerKind SignatureHelpTriggerKindInvoked = 1;
inline constexpr SignatureHelpTriggerKind SignatureHelpTriggerKindTriggerCharacter = 2;
inline constexpr SignatureHelpTriggerKind SignatureHelpTriggerKindContentChange = 3;

// SymbolKind — lsp_generated.go.
using SymbolKind = uint32_t;
inline constexpr SymbolKind SymbolKindFile = 1;
inline constexpr SymbolKind SymbolKindModule = 2;
inline constexpr SymbolKind SymbolKindNamespace = 3;
inline constexpr SymbolKind SymbolKindPackage = 4;
inline constexpr SymbolKind SymbolKindClass = 5;
inline constexpr SymbolKind SymbolKindMethod = 6;
inline constexpr SymbolKind SymbolKindProperty = 7;
inline constexpr SymbolKind SymbolKindField = 8;
inline constexpr SymbolKind SymbolKindConstructor = 9;
inline constexpr SymbolKind SymbolKindEnum = 10;
inline constexpr SymbolKind SymbolKindInterface = 11;
inline constexpr SymbolKind SymbolKindFunction = 12;
inline constexpr SymbolKind SymbolKindVariable = 13;
inline constexpr SymbolKind SymbolKindConstant = 14;
inline constexpr SymbolKind SymbolKindString = 15;
inline constexpr SymbolKind SymbolKindNumber = 16;
inline constexpr SymbolKind SymbolKindBoolean = 17;
inline constexpr SymbolKind SymbolKindArray = 18;
inline constexpr SymbolKind SymbolKindObject = 19;
inline constexpr SymbolKind SymbolKindKey = 20;
inline constexpr SymbolKind SymbolKindNull = 21;
inline constexpr SymbolKind SymbolKindEnumMember = 22;
inline constexpr SymbolKind SymbolKindStruct = 23;
inline constexpr SymbolKind SymbolKindEvent = 24;
inline constexpr SymbolKind SymbolKindOperator = 25;
inline constexpr SymbolKind SymbolKindTypeParameter = 26;

// SymbolTag — lsp_generated.go.
using SymbolTag = uint32_t;
inline constexpr SymbolTag SymbolTagDeprecated = 1;

// DiagnosticSeverity — lsp_generated.go.
using DiagnosticSeverity = uint32_t;
inline constexpr DiagnosticSeverity DiagnosticSeverityError = 1;
inline constexpr DiagnosticSeverity DiagnosticSeverityWarning = 2;
inline constexpr DiagnosticSeverity DiagnosticSeverityInformation = 3;
inline constexpr DiagnosticSeverity DiagnosticSeverityHint = 4;

// DiagnosticTag — lsp_generated.go.
using DiagnosticTag = uint32_t;
inline constexpr DiagnosticTag DiagnosticTagUnnecessary = 1;
inline constexpr DiagnosticTag DiagnosticTagDeprecated = 2;

// CodeActionTag — lsp_generated.go.
using CodeActionTag = uint32_t;
inline constexpr CodeActionTag CodeActionTagRefactorExtract = 1;

// CodeActionTriggerKind — lsp_generated.go.
using CodeActionTriggerKind = uint32_t;
inline constexpr CodeActionTriggerKind CodeActionTriggerKindInvoked = 1;
inline constexpr CodeActionTriggerKind CodeActionTriggerKindAutomatic = 2;

// --- String literal marker types --------------------------------------------
// lsp_generated.go — `type StringLiteralX struct{}` marshals a fixed string.

struct StringLiteralRename {};
struct StringLiteralCreate {};
struct StringLiteralDelete {};
struct StringLiteralFull {};
struct StringLiteralUnchanged {};
struct StringLiteralClassifiedTextRun {};
struct StringLiteralClassifiedTextElement {};
struct StringLiteralImageId {};

// --- Nullable wrappers --------------------------------------------------------

struct UintegerOrNull {
	uint32_t* Uinteger = nullptr;
	bool isNull() const { return Uinteger == nullptr; }
};
struct IntegerOrNull {
	int32_t* Integer = nullptr;
	bool isNull() const { return Integer == nullptr; }
};
struct IntegerOrString {
	int32_t* Integer = nullptr;
	std::string* String = nullptr;
};
struct StringOrTuple {
	std::string* String = nullptr;
	std::vector<uint32_t>* Tuple = nullptr;
};

// MarkupContent / StringOrMarkupContent — lsp_generated.go + util.go.
struct MarkupContent {
	MarkupKind Kind;
	std::string Value;
};
struct StringOrMarkupContent {
	std::string* String = nullptr;
	MarkupContent* MarkupContent = nullptr;
	// AsString — util.go.
	std::string AsString() const {
		if (String != nullptr) return *String;
		if (MarkupContent != nullptr) return MarkupContent->Value;
		return "";
	}
};

// --- Core primitives ----------------------------------------------------------

// DocumentUri — lsp.go (`type DocumentUri string`).
struct DocumentUri {
	std::string value;
	DocumentUri() = default;
	DocumentUri(const char* s) : value(s) {}
	DocumentUri(const std::string& s) : value(s) {}
	DocumentUri(std::string&& s) : value(std::move(s)) {}
	operator const std::string&() const { return value; }
	bool operator==(const DocumentUri& o) const { return value == o.value; }
	bool operator!=(const DocumentUri& o) const { return !(*this == o); }
	bool operator<(const DocumentUri& o) const { return value < o.value; }
	bool empty() const { return value.empty(); }
	size_t size() const { return value.size(); }
	const char* c_str() const { return value.c_str(); }
	const std::string& String() const { return value; }

	// FileName — lsp.go:19.
	std::string FileName() const;
	// Path — lsp.go:45.
	std::string Path(bool useCaseSensitiveFileNames) const;
};
struct DocumentUriHash {
	size_t operator()(const DocumentUri& u) const {
		return std::hash<std::string>{}(u.value);
	}
};

// Position — lsp_generated.go.
struct Position {
	uint32_t Line = 0;
	uint32_t Character = 0;
	bool operator==(const Position&) const = default;
};
// ComparePositions — util.go.
inline int ComparePositions(Position a, Position b) {
	if (a.Line != b.Line) return a.Line < b.Line ? -1 : 1;
	if (a.Character != b.Character) return a.Character < b.Character ? -1 : 1;
	return 0;
}

// Range — lsp_generated.go.
struct Range {
	Position Start;
	Position End;
	bool operator==(const Range&) const = default;
	bool operator!=(const Range& o) const { return !(*this == o); }
};
// CompareRanges — util.go.
inline int CompareRanges(Range lsRange, Range other) {
	if (int c = ComparePositions(lsRange.Start, other.Start); c != 0) return c;
	return ComparePositions(lsRange.End, other.End);
}

// Location — lsp_generated.go.
struct Location {
	DocumentUri Uri;
	Range Range_;
	// GetLocation — lsp.go (HasLocation).
	Location GetLocation() const { return *this; }
	bool operator==(const Location&) const = default;
};

// LocationLink — lsp_generated.go.
struct LocationLink {
	Range* OriginSelectionRange = nullptr;
	DocumentUri TargetUri;
	Range TargetRange;
	Range TargetSelectionRange;
	// GetLocation — lsp.go.
	Location GetLocation() const {
		return Location{TargetUri, TargetSelectionRange};
	}
};

// TextEdit — lsp_generated.go.
struct TextEdit {
	Range Range;
	std::string NewText;
	bool operator==(const TextEdit&) const = default;
};
// CompareTextEdits — util.go.
inline int CompareTextEdits(const TextEdit& a, const TextEdit& b) {
	if (int c = CompareRanges(a.Range, b.Range); c != 0) return c;
	return a.NewText < b.NewText ? -1 : (a.NewText == b.NewText ? 0 : 1);
}

struct AnnotatedTextEdit {
	Range Range;
	std::string NewText;
	std::string AnnotationId;
};
struct StringValue {}; // opaque marker
struct SnippetTextEdit {
	Range Range;
	StringValue* Snippet = nullptr;
	std::string* AnnotationId = nullptr;
};
struct TextEditOrAnnotatedTextEditOrSnippetTextEdit {
	TextEdit* TextEdit = nullptr;
	AnnotatedTextEdit* AnnotatedTextEdit = nullptr;
	SnippetTextEdit* SnippetTextEdit = nullptr;
};

struct TextDocumentIdentifier {
	DocumentUri Uri;
};

struct OptionalVersionedTextDocumentIdentifier {
	DocumentUri Uri;
	IntegerOrNull Version;
};

// --- Workspace edit types -----------------------------------------------------

struct CreateFileOptions {
	bool* Overwrite = nullptr;
	bool* IgnoreIfExists = nullptr;
};
struct RenameFileOptions {
	bool* Overwrite = nullptr;
	bool* IgnoreIfExists = nullptr;
};
struct DeleteFileOptions {
	bool* Recursive = nullptr;
	bool* IgnoreIfNotExists = nullptr;
};
struct CreateFile {
	StringLiteralCreate Kind;
	std::string* AnnotationId = nullptr;
	DocumentUri Uri;
	CreateFileOptions* Options = nullptr;
};
struct RenameFile {
	StringLiteralRename Kind;
	std::string* AnnotationId = nullptr;
	DocumentUri OldUri;
	DocumentUri NewUri;
	RenameFileOptions* Options = nullptr;
};
struct DeleteFile {
	StringLiteralDelete Kind;
	std::string* AnnotationId = nullptr;
	DocumentUri Uri;
	DeleteFileOptions* Options = nullptr;
};
struct TextDocumentEdit {
	OptionalVersionedTextDocumentIdentifier TextDocument;
	std::vector<TextEditOrAnnotatedTextEditOrSnippetTextEdit> Edits;
};
struct TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile {
	TextDocumentEdit* TextDocumentEdit = nullptr;
	CreateFile* CreateFile = nullptr;
	RenameFile* RenameFile = nullptr;
	DeleteFile* DeleteFile = nullptr;
};
using ChangeAnnotationIdentifier = std::string;
struct ChangeAnnotation {
	std::string Label;
	bool* NeedsConfirmation = nullptr;
	std::string* Description = nullptr;
};
struct WorkspaceEdit {
	std::unordered_map<DocumentUri, std::vector<TextEdit*>, DocumentUriHash>* Changes = nullptr;
	std::vector<TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile>* DocumentChanges = nullptr;
	std::unordered_map<std::string, ChangeAnnotation*>* ChangeAnnotations = nullptr;
};

// --- Diagnostics --------------------------------------------------------------

struct CodeActionData {}; // lsp_generated.go (`type CodeActionData struct{}`)
struct DiagnosticData {};
struct CallHierarchyItemData {};

struct CodeDescription {
	std::string Href;
};
struct DiagnosticRelatedInformation {
	Location Location;
	std::string Message;
};
struct Diagnostic {
	Range Range;
	DiagnosticSeverity* Severity = nullptr;
	IntegerOrString* Code = nullptr;
	CodeDescription* CodeDescription = nullptr;
	std::string* Source = nullptr;
	StringOrMarkupContent Message;
	std::vector<DiagnosticTag>* Tags = nullptr;
	std::vector<DiagnosticRelatedInformation*>* RelatedInformation = nullptr;
	DiagnosticData* Data = nullptr;
};

// --- Commands / code actions ---------------------------------------------------

struct Command {
	std::string Title;
	std::string* Tooltip = nullptr;
	std::string Command;
	std::vector<std::any>* Arguments = nullptr;
	std::any* Data = nullptr;
};
struct CodeActionDisabled {
	std::string Reason;
};
struct CodeAction {
	std::string Title;
	CodeActionKind* Kind = nullptr;
	std::vector<Diagnostic*>* Diagnostics = nullptr;
	bool* IsPreferred = nullptr;
	CodeActionDisabled* Disabled = nullptr;
	WorkspaceEdit* Edit = nullptr;
	Command* Command = nullptr;
	CodeActionData* Data = nullptr;
	std::vector<CodeActionTag>* Tags = nullptr;
};
struct CommandOrCodeAction {
	Command* Command = nullptr;
	CodeAction* CodeAction = nullptr;
};
using CommandOrCodeActionArray = std::vector<CommandOrCodeAction>;

// --- Request param types ---------------------------------------------------------

struct WorkDoneProgressParams {
	IntegerOrString* WorkDoneToken = nullptr;
};
struct PartialResultParams {
	IntegerOrString* PartialResultToken = nullptr;
};

struct ReferenceContext {
	bool IncludeDeclaration = false;
};
// ReferenceParams — lsp_generated.go.
struct ReferenceParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	ReferenceContext* Context = nullptr;

	// lsp.go — HasTextDocumentURI / HasTextDocumentPosition.
	DocumentUri TextDocumentURI() const { return TextDocument.Uri; }
	lsproto::Position TextDocumentPosition() const { return Position; }
};
// ImplementationParams — lsp_generated.go.
struct ImplementationParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;

	DocumentUri TextDocumentURI() const { return TextDocument.Uri; }
	lsproto::Position TextDocumentPosition() const { return Position; }
};
// RenameParams — lsp_generated.go.
struct RenameParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	IntegerOrString* WorkDoneToken = nullptr;
	std::string NewName;

	DocumentUri TextDocumentURI() const { return TextDocument.Uri; }
	lsproto::Position TextDocumentPosition() const { return Position; }
};
// VSCode-speak: rename request params.
struct RenameRequestParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	IntegerOrString* WorkDoneToken = nullptr;
	std::string NewName;
	bool IncludeInStringOrComment = false;

	DocumentUri TextDocumentURI() const { return TextDocument.Uri; }
	lsproto::Position TextDocumentPosition() const { return Position; }
};

// SignatureHelpContext / SignatureHelpParams — lsp_generated.go.
struct SignatureHelp; // fwd
struct SignatureHelpContext {
	SignatureHelpTriggerKind TriggerKind = SignatureHelpTriggerKindInvoked;
	std::string* TriggerCharacter = nullptr;
	bool IsRetrigger = false;
	SignatureHelp* ActiveSignatureHelp = nullptr;
};
struct SignatureHelpParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	IntegerOrString* WorkDoneToken = nullptr;
	SignatureHelpContext* Context = nullptr;

	DocumentUri TextDocumentURI() const { return TextDocument.Uri; }
	lsproto::Position TextDocumentPosition() const { return Position; }
};

// CodeActionContext / CodeActionParams — lsp_generated.go.
struct CodeActionContext {
	std::vector<Diagnostic*> Diagnostics;
	std::vector<CodeActionKind>* Only = nullptr;
	CodeActionTriggerKind* TriggerKind = nullptr;
};
struct CodeActionParams {
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	TextDocumentIdentifier TextDocument;
	Range Range;
	CodeActionContext* Context = nullptr;

	DocumentUri TextDocumentURI() const { return TextDocument.Uri; }
};

// DocumentDiagnosticParams — lsp_generated.go.
struct DocumentDiagnosticParams {
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	TextDocumentIdentifier TextDocument;
	std::string* Identifier = nullptr;
	std::string* PreviousResultId = nullptr;

	DocumentUri TextDocumentURI() const { return TextDocument.Uri; }
};

// --- Signature help --------------------------------------------------------------

struct ParameterInformation {
	StringOrTuple Label;
	StringOrMarkupContent* Documentation = nullptr;
};
struct VSClassifiedTextRun;
struct VSClassifiedTextElement {
	std::vector<VSClassifiedTextRun*> Runs;
	StringLiteralClassifiedTextElement VSType;
};
struct SignatureInformation {
	std::string Label;
	StringOrMarkupContent* Documentation = nullptr;
	std::vector<ParameterInformation*>* Parameters = nullptr;
	UintegerOrNull* ActiveParameter = nullptr;
	VSClassifiedTextElement* VSColorizedLabel = nullptr;
};
struct SignatureHelp {
	std::vector<SignatureInformation*> Signatures;
	uint32_t* ActiveSignature = nullptr;
	UintegerOrNull* ActiveParameter = nullptr;
};

// --- Call hierarchy ---------------------------------------------------------------

struct CallHierarchyItem {
	std::string Name;
	SymbolKind Kind = SymbolKindFile;
	std::vector<SymbolTag>* Tags = nullptr;
	std::string* Detail = nullptr;
	DocumentUri Uri;
	Range Range;
	lsproto::Range SelectionRange;
	CallHierarchyItemData* Data = nullptr;

	// lsp.go — HasLocation / HasTextDocumentURI for handleCrossProject.
	Location GetLocation() const { return Location{Uri, Range}; }
	DocumentUri TextDocumentURI() const { return Uri; }
	lsproto::Position TextDocumentPosition() const { return Range.Start; }
};
struct CallHierarchyIncomingCall {
	CallHierarchyItem* From = nullptr;
	std::vector<Range> FromRanges;
};
struct CallHierarchyIncomingCallsParams {
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	CallHierarchyItem* Item = nullptr;
};

// --- VS extensions ----------------------------------------------------------------

struct VSClassifiedTextRun {
	ClassificationTypeName ClassificationTypeName;
	std::string Text;
	std::string* MarkerTagType = nullptr;
	int32_t Style = 0;
	StringLiteralClassifiedTextRun VSType;
};
// (VSClassifiedTextElement defined above)

struct VSReferenceItem {
	int32_t VSId = 0;
	int32_t* VSDefinitionId = nullptr;
	std::vector<VSReferenceKind>* VSKind = nullptr;
	Location VSLocation;
	VSClassifiedTextElement* VSDefinitionText = nullptr;
	std::string* VSProjectName = nullptr;
	std::string* VSContainingType = nullptr;
};

struct AutoImportFix {
	AutoImportFixKind Kind = AutoImportFixKindUseNamespace;
	std::string Name;
	ImportKind ImportKind = ImportKindNamed;
	bool UseRequire = false;
	AddAsTypeOnly AddAsTypeOnly = AddAsTypeOnlyNotApplicable;
	std::string ModuleSpecifier;
	int32_t ImportIndex = -1;
	uint32_t* UsagePosition = nullptr;
	std::string NamespacePrefix;
};

// --- Response union types ------------------------------------------------------------

using Locations = std::vector<Location>;
using VSReferenceItems = std::vector<VSReferenceItem*>;
using CallHierarchyIncomingCalls = std::vector<CallHierarchyIncomingCall*>;
using Definitions = std::vector<Location>;
using DefinitionLinks = std::vector<LocationLink*>;

struct LocationsOrNull {
	lsproto::Locations* Locations = nullptr;
	bool isNull() const { return Locations == nullptr; }
	lsproto::Locations* GetLocations() const { return Locations; }
};
struct LocationOrLocationsOrDefinitionLinksOrNull {
	lsproto::Location* Location = nullptr;
	lsproto::Locations* Locations = nullptr;
	std::vector<LocationLink*>* DefinitionLinks = nullptr;
	bool isNull() const {
		return Location == nullptr && Locations == nullptr && DefinitionLinks == nullptr;
	}
	// GetLocations — lsp_generated.go.
	lsproto::Locations* GetLocations() const { return Locations; }
};
struct SignatureHelpOrNull {
	SignatureHelp* SignatureHelp = nullptr;
	bool isNull() const { return SignatureHelp == nullptr; }
};
struct WorkspaceEditOrNull {
	WorkspaceEdit* WorkspaceEdit = nullptr;
	bool isNull() const { return WorkspaceEdit == nullptr; }
};
struct CommandOrCodeActionArrayOrNull {
	CommandOrCodeActionArray* CommandOrCodeActionArray = nullptr;
	bool isNull() const { return CommandOrCodeActionArray == nullptr; }
};
struct VSReferenceItemsOrNull {
	VSReferenceItems* VSReferenceItems = nullptr;
	bool isNull() const { return VSReferenceItems == nullptr; }
};
struct CallHierarchyIncomingCallsOrNull {
	CallHierarchyIncomingCalls* CallHierarchyIncomingCalls = nullptr;
	bool isNull() const { return CallHierarchyIncomingCalls == nullptr; }
};

// --- Document diagnostic reports --------------------------------------------------------

struct FullDocumentDiagnosticReport {
	StringLiteralFull Kind;
	std::string* ResultId = nullptr;
	std::vector<Diagnostic*> Items;
};
struct UnchangedDocumentDiagnosticReport {
	StringLiteralUnchanged Kind;
	std::string ResultId;
};
struct RelatedFullDocumentDiagnosticReport {
	StringLiteralFull Kind;
	std::string* ResultId = nullptr;
	std::vector<Diagnostic*> Items;
	std::unordered_map<DocumentUri, struct FullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport, DocumentUriHash>* RelatedDocuments = nullptr;
};
struct RelatedUnchangedDocumentDiagnosticReport {
	StringLiteralUnchanged Kind;
	std::string ResultId;
	std::unordered_map<DocumentUri, struct FullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport, DocumentUriHash>* RelatedDocuments = nullptr;
};
struct FullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport {
	FullDocumentDiagnosticReport* FullDocumentDiagnosticReport = nullptr;
	UnchangedDocumentDiagnosticReport* UnchangedDocumentDiagnosticReport = nullptr;
};
struct RelatedFullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport {
	RelatedFullDocumentDiagnosticReport* FullDocumentDiagnosticReport = nullptr;
	RelatedUnchangedDocumentDiagnosticReport* UnchangedDocumentDiagnosticReport = nullptr;
};

// --- Response type aliases ---------------------------------------------------------------
// handleCrossProject's Resp parameter — the "response" type of each request.

using ReferencesResponse = LocationsOrNull;
using VSReferencesResponse = VSReferenceItemsOrNull;
using ImplementationResponse = LocationOrLocationsOrDefinitionLinksOrNull;
using RenameResponse = WorkspaceEditOrNull;
using CodeActionResponse = CommandOrCodeActionArrayOrNull;
using SignatureHelpResponse = SignatureHelpOrNull;
using CallHierarchyIncomingCallsResponse = CallHierarchyIncomingCallsOrNull;
using DocumentDiagnosticResponse = RelatedFullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport;

// --- Client capabilities -------------------------------------------------------------------
// lsp_generated.go ResolvedClientCapabilities — only the fields the ls slice reads.

struct ResolvedClientSignatureParameterInformationOptions {
	bool LabelOffsetSupport = false;
};
struct ResolvedClientSignatureInformationOptions {
	std::vector<MarkupKind> DocumentationFormat;
	ResolvedClientSignatureParameterInformationOptions ParameterInformation;
	bool ActiveParameterSupport = false;
	bool NoActiveParameterSupport = false;
};
struct ResolvedSignatureHelpClientCapabilities {
	bool DynamicRegistration = false;
	ResolvedClientSignatureInformationOptions SignatureInformation;
	bool ContextSupport = false;
};
struct ResolvedImplementationClientCapabilities {
	bool DynamicRegistration = false;
	bool LinkSupport = false;
};
struct ResolvedTextDocumentClientCapabilities {
	ResolvedSignatureHelpClientCapabilities SignatureHelp;
	ResolvedImplementationClientCapabilities Implementation;
	// The rest of the resolved text-document capabilities are unused here.
	std::any _unused{};
};
struct ResolvedWorkspaceClientCapabilities {
	std::any _unused{};
};
struct ResolvedWindowClientCapabilities {
	std::any _unused{};
};
struct ResolvedGeneralClientCapabilities {
	std::any _unused{};
};
struct ResolvedExperimentalClientCapabilities {
	std::any _unused{};
};
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

// GetClientCapabilities / WithClientCapabilities — lsp.go. Stored off the
// context value slot (gostd::Context has no Value map; keyed on the impl).
const ResolvedClientCapabilities* GetClientCapabilities(const gostd::Context& ctx);
gostd::Context WithClientCapabilities(const gostd::Context& ctx, const ResolvedClientCapabilities& caps);

// --- Concepts for handleCrossProject's generic constraint -----------------------------------
// lsp.go: HasTextDocumentURI / HasTextDocumentPosition / HasLocation / HasLocations.

template <typename T>
concept HasTextDocumentURI = requires(const T& t) {
	{ t.TextDocumentURI() } -> std::convertible_to<DocumentUri>;
};
template <typename T>
concept HasTextDocumentPosition = requires(const T& t) {
	{ t.TextDocumentURI() } -> std::convertible_to<DocumentUri>;
	{ t.TextDocumentPosition() } -> std::convertible_to<Position>;
};
template <typename T>
concept HasLocation = requires(const T& t) {
	{ t.GetLocation() } -> std::convertible_to<Location>;
};
template <typename T>
concept HasLocations = requires(const T& t) {
	{ t.GetLocations() } -> std::convertible_to<Locations*>;
};

} // namespace tsc::lsp::lsproto

// std::hash specializations so collections::Set<T>/unordered maps over the
// generated LSP value types work (needed by crossproject combine helpers).
template <>
struct std::hash<tsc::lsp::lsproto::DocumentUri> {
	size_t operator()(const tsc::lsp::lsproto::DocumentUri& u) const {
		return std::hash<std::string>{}(u.value);
	}
};
template <>
struct std::hash<tsc::lsp::lsproto::Position> {
	size_t operator()(const tsc::lsp::lsproto::Position& p) const {
		return (size_t)p.Line << 32 | p.Character;
	}
};
template <>
struct std::hash<tsc::lsp::lsproto::Range> {
	size_t operator()(const tsc::lsp::lsproto::Range& r) const {
		return std::hash<tsc::lsp::lsproto::Position>{}(r.Start) * 31 +
		       std::hash<tsc::lsp::lsproto::Position>{}(r.End);
	}
};
template <>
struct std::hash<tsc::lsp::lsproto::Location> {
	size_t operator()(const tsc::lsp::lsproto::Location& l) const {
		return std::hash<tsc::lsp::lsproto::DocumentUri>{}(l.Uri) * 31 +
		       std::hash<tsc::lsp::lsproto::Range>{}(l.Range_);
	}
};

// Merge bridge: allow unqualified `lsproto::X` spellings from consumers
// written against the pre-canonical flat namespace.
namespace tsc { namespace lsproto = tsc::lsp::lsproto; }
