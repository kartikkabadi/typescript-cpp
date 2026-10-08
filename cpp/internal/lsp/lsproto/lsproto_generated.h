#pragma once
// ============================================================================
// lsproto_generated.h — generated port of
// tsc/internal/lsp/lsproto/lsp_generated.go (the Go file is itself generated;
// this header was produced by translating its output — regenerate with the
// slice generator, do not edit).
// ============================================================================
#include "internal/lsp/lsproto/lsproto_runtime.h"

namespace tsc::lsp::lsproto {

// ---------------------------------------------------------------------------
// Enumerations — lsp_generated.go:9510
// ---------------------------------------------------------------------------

// Enumerations
// A set of predefined token types. This set is not fixed
// an clients can specify additional token types via the
// corresponding client capabilities.
//
// Since: 3.16.0
using SemanticTokenType = std::string;

inline const SemanticTokenType SemanticTokenTypeNamespace = "namespace";
inline const SemanticTokenType SemanticTokenTypeType = "type";
inline const SemanticTokenType SemanticTokenTypeClass = "class";
inline const SemanticTokenType SemanticTokenTypeEnum = "enum";
inline const SemanticTokenType SemanticTokenTypeInterface = "interface";
inline const SemanticTokenType SemanticTokenTypeStruct = "struct";
inline const SemanticTokenType SemanticTokenTypeTypeParameter = "typeParameter";
inline const SemanticTokenType SemanticTokenTypeParameter = "parameter";
inline const SemanticTokenType SemanticTokenTypeVariable = "variable";
inline const SemanticTokenType SemanticTokenTypeProperty = "property";
inline const SemanticTokenType SemanticTokenTypeEnumMember = "enumMember";
inline const SemanticTokenType SemanticTokenTypeEvent = "event";
inline const SemanticTokenType SemanticTokenTypeFunction = "function";
inline const SemanticTokenType SemanticTokenTypeMethod = "method";
inline const SemanticTokenType SemanticTokenTypeMacro = "macro";
inline const SemanticTokenType SemanticTokenTypeKeyword = "keyword";
inline const SemanticTokenType SemanticTokenTypeModifier = "modifier";
inline const SemanticTokenType SemanticTokenTypeComment = "comment";
inline const SemanticTokenType SemanticTokenTypeString = "string";
inline const SemanticTokenType SemanticTokenTypeNumber = "number";
inline const SemanticTokenType SemanticTokenTypeRegexp = "regexp";
inline const SemanticTokenType SemanticTokenTypeOperator = "operator";
inline const SemanticTokenType SemanticTokenTypeDecorator = "decorator";
inline const SemanticTokenType SemanticTokenTypeLabel = "label";

// A set of predefined token modifiers. This set is not fixed
// an clients can specify additional token types via the
// corresponding client capabilities.
//
// Since: 3.16.0
using SemanticTokenModifier = std::string;

inline const SemanticTokenModifier SemanticTokenModifierDeclaration = "declaration";
inline const SemanticTokenModifier SemanticTokenModifierDefinition = "definition";
inline const SemanticTokenModifier SemanticTokenModifierReadonly = "readonly";
inline const SemanticTokenModifier SemanticTokenModifierStatic = "static";
inline const SemanticTokenModifier SemanticTokenModifierDeprecated = "deprecated";
inline const SemanticTokenModifier SemanticTokenModifierAbstract = "abstract";
inline const SemanticTokenModifier SemanticTokenModifierAsync = "async";
inline const SemanticTokenModifier SemanticTokenModifierModification = "modification";
inline const SemanticTokenModifier SemanticTokenModifierDocumentation = "documentation";
inline const SemanticTokenModifier SemanticTokenModifierDefaultLibrary = "defaultLibrary";

// The document diagnostic report kinds.
//
// Since: 3.17.0
using DocumentDiagnosticReportKind = std::string;

inline const DocumentDiagnosticReportKind DocumentDiagnosticReportKindFull = "full";
inline const DocumentDiagnosticReportKind DocumentDiagnosticReportKindUnchanged = "unchanged";

// Predefined error codes.
enum class ErrorCode : int32_t {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    ServerNotInitialized = -32002,
    UnknownErrorCode = -32001,
    RequestFailed = -32803,
    ServerCancelled = -32802,
    ContentModified = -32801,
    RequestCancelled = -32800,
};

inline constexpr ErrorCode ErrorCodeParseError = ErrorCode::ParseError;
inline constexpr ErrorCode ErrorCodeInvalidRequest = ErrorCode::InvalidRequest;
inline constexpr ErrorCode ErrorCodeMethodNotFound = ErrorCode::MethodNotFound;
inline constexpr ErrorCode ErrorCodeInvalidParams = ErrorCode::InvalidParams;
inline constexpr ErrorCode ErrorCodeInternalError = ErrorCode::InternalError;
inline constexpr ErrorCode ErrorCodeServerNotInitialized = ErrorCode::ServerNotInitialized;
inline constexpr ErrorCode ErrorCodeUnknownErrorCode = ErrorCode::UnknownErrorCode;
inline constexpr ErrorCode ErrorCodeRequestFailed = ErrorCode::RequestFailed;
inline constexpr ErrorCode ErrorCodeServerCancelled = ErrorCode::ServerCancelled;
inline constexpr ErrorCode ErrorCodeContentModified = ErrorCode::ContentModified;
inline constexpr ErrorCode ErrorCodeRequestCancelled = ErrorCode::RequestCancelled;
std::string String(ErrorCode e);
struct ErrorCodeError : gostd::ErrObj {
    ErrorCode code;
    explicit ErrorCodeError(ErrorCode c) : code(c) {}
    std::string Error() const override { return String(code); }
    // errors.Is: ErrorCode is a comparable int in Go — match by value.
    bool isEqual(const gostd::ErrObj& o) const override {
        auto* p = dynamic_cast<const ErrorCodeError*>(&o);
        return p != nullptr && p->code == code;
    }
};
inline gostd::Error errorCodeErr(ErrorCode c) { return std::make_shared<ErrorCodeError>(c); }

// A set of predefined range kinds.
using FoldingRangeKind = std::string;

inline const FoldingRangeKind FoldingRangeKindComment = "comment";
inline const FoldingRangeKind FoldingRangeKindImports = "imports";
inline const FoldingRangeKind FoldingRangeKindRegion = "region";

// A symbol kind.
enum class SymbolKind : uint32_t {
    File = 1,
    Module = 2,
    Namespace = 3,
    Package = 4,
    Class = 5,
    Method = 6,
    Property = 7,
    Field = 8,
    Constructor = 9,
    Enum = 10,
    Interface = 11,
    Function = 12,
    Variable = 13,
    Constant = 14,
    String = 15,
    Number = 16,
    Boolean = 17,
    Array = 18,
    Object = 19,
    Key = 20,
    Null = 21,
    EnumMember = 22,
    Struct = 23,
    Event = 24,
    Operator = 25,
    TypeParameter = 26,
};

inline constexpr SymbolKind SymbolKindFile = SymbolKind::File;
inline constexpr SymbolKind SymbolKindModule = SymbolKind::Module;
inline constexpr SymbolKind SymbolKindNamespace = SymbolKind::Namespace;
inline constexpr SymbolKind SymbolKindPackage = SymbolKind::Package;
inline constexpr SymbolKind SymbolKindClass = SymbolKind::Class;
inline constexpr SymbolKind SymbolKindMethod = SymbolKind::Method;
inline constexpr SymbolKind SymbolKindProperty = SymbolKind::Property;
inline constexpr SymbolKind SymbolKindField = SymbolKind::Field;
inline constexpr SymbolKind SymbolKindConstructor = SymbolKind::Constructor;
inline constexpr SymbolKind SymbolKindEnum = SymbolKind::Enum;
inline constexpr SymbolKind SymbolKindInterface = SymbolKind::Interface;
inline constexpr SymbolKind SymbolKindFunction = SymbolKind::Function;
inline constexpr SymbolKind SymbolKindVariable = SymbolKind::Variable;
inline constexpr SymbolKind SymbolKindConstant = SymbolKind::Constant;
inline constexpr SymbolKind SymbolKindString = SymbolKind::String;
inline constexpr SymbolKind SymbolKindNumber = SymbolKind::Number;
inline constexpr SymbolKind SymbolKindBoolean = SymbolKind::Boolean;
inline constexpr SymbolKind SymbolKindArray = SymbolKind::Array;
inline constexpr SymbolKind SymbolKindObject = SymbolKind::Object;
inline constexpr SymbolKind SymbolKindKey = SymbolKind::Key;
inline constexpr SymbolKind SymbolKindNull = SymbolKind::Null;
inline constexpr SymbolKind SymbolKindEnumMember = SymbolKind::EnumMember;
inline constexpr SymbolKind SymbolKindStruct = SymbolKind::Struct;
inline constexpr SymbolKind SymbolKindEvent = SymbolKind::Event;
inline constexpr SymbolKind SymbolKindOperator = SymbolKind::Operator;
inline constexpr SymbolKind SymbolKindTypeParameter = SymbolKind::TypeParameter;
std::string String(SymbolKind e);

// Symbol tags are extra annotations that tweak the rendering of a symbol.
//
// Since: 3.16
enum class SymbolTag : uint32_t {
    Deprecated = 1,
};

inline constexpr SymbolTag SymbolTagDeprecated = SymbolTag::Deprecated;
std::string String(SymbolTag e);

// Moniker uniqueness level to define scope of the moniker.
//
// Since: 3.16.0
using UniquenessLevel = std::string;

inline const UniquenessLevel UniquenessLevelDocument = "document";
inline const UniquenessLevel UniquenessLevelProject = "project";
inline const UniquenessLevel UniquenessLevelGroup = "group";
inline const UniquenessLevel UniquenessLevelScheme = "scheme";
inline const UniquenessLevel UniquenessLevelGlobal = "global";

// The moniker kind.
//
// Since: 3.16.0
using MonikerKind = std::string;

inline const MonikerKind MonikerKindImport = "import";
inline const MonikerKind MonikerKindExport = "export";
inline const MonikerKind MonikerKindLocal = "local";

// Inlay hint kinds.
//
// Since: 3.17.0
enum class InlayHintKind : uint32_t {
    Type = 1,
    Parameter = 2,
};

inline constexpr InlayHintKind InlayHintKindType = InlayHintKind::Type;
inline constexpr InlayHintKind InlayHintKindParameter = InlayHintKind::Parameter;
std::string String(InlayHintKind e);

// The message type
enum class MessageType : uint32_t {
    Error = 1,
    Warning = 2,
    Info = 3,
    Log = 4,
    Debug = 5,
};

inline constexpr MessageType MessageTypeError = MessageType::Error;
inline constexpr MessageType MessageTypeWarning = MessageType::Warning;
inline constexpr MessageType MessageTypeInfo = MessageType::Info;
inline constexpr MessageType MessageTypeLog = MessageType::Log;
inline constexpr MessageType MessageTypeDebug = MessageType::Debug;
std::string String(MessageType e);

// Defines how the host (editor) should sync
// document changes to the language server.
enum class TextDocumentSyncKind : uint32_t {
    None = 0,
    Full = 1,
    Incremental = 2,
};

inline constexpr TextDocumentSyncKind TextDocumentSyncKindNone = TextDocumentSyncKind::None;
inline constexpr TextDocumentSyncKind TextDocumentSyncKindFull = TextDocumentSyncKind::Full;
inline constexpr TextDocumentSyncKind TextDocumentSyncKindIncremental = TextDocumentSyncKind::Incremental;
std::string String(TextDocumentSyncKind e);

// Represents reasons why a text document is saved.
enum class TextDocumentSaveReason : uint32_t {
    Manual = 1,
    AfterDelay = 2,
    FocusOut = 3,
};

inline constexpr TextDocumentSaveReason TextDocumentSaveReasonManual = TextDocumentSaveReason::Manual;
inline constexpr TextDocumentSaveReason TextDocumentSaveReasonAfterDelay = TextDocumentSaveReason::AfterDelay;
inline constexpr TextDocumentSaveReason TextDocumentSaveReasonFocusOut = TextDocumentSaveReason::FocusOut;
std::string String(TextDocumentSaveReason e);

// The kind of a completion entry.
enum class CompletionItemKind : uint32_t {
    Text = 1,
    Method = 2,
    Function = 3,
    Constructor = 4,
    Field = 5,
    Variable = 6,
    Class = 7,
    Interface = 8,
    Module = 9,
    Property = 10,
    Unit = 11,
    Value = 12,
    Enum = 13,
    Keyword = 14,
    Snippet = 15,
    Color = 16,
    File = 17,
    Reference = 18,
    Folder = 19,
    EnumMember = 20,
    Constant = 21,
    Struct = 22,
    Event = 23,
    Operator = 24,
    TypeParameter = 25,
};

inline constexpr CompletionItemKind CompletionItemKindText = CompletionItemKind::Text;
inline constexpr CompletionItemKind CompletionItemKindMethod = CompletionItemKind::Method;
inline constexpr CompletionItemKind CompletionItemKindFunction = CompletionItemKind::Function;
inline constexpr CompletionItemKind CompletionItemKindConstructor = CompletionItemKind::Constructor;
inline constexpr CompletionItemKind CompletionItemKindField = CompletionItemKind::Field;
inline constexpr CompletionItemKind CompletionItemKindVariable = CompletionItemKind::Variable;
inline constexpr CompletionItemKind CompletionItemKindClass = CompletionItemKind::Class;
inline constexpr CompletionItemKind CompletionItemKindInterface = CompletionItemKind::Interface;
inline constexpr CompletionItemKind CompletionItemKindModule = CompletionItemKind::Module;
inline constexpr CompletionItemKind CompletionItemKindProperty = CompletionItemKind::Property;
inline constexpr CompletionItemKind CompletionItemKindUnit = CompletionItemKind::Unit;
inline constexpr CompletionItemKind CompletionItemKindValue = CompletionItemKind::Value;
inline constexpr CompletionItemKind CompletionItemKindEnum = CompletionItemKind::Enum;
inline constexpr CompletionItemKind CompletionItemKindKeyword = CompletionItemKind::Keyword;
inline constexpr CompletionItemKind CompletionItemKindSnippet = CompletionItemKind::Snippet;
inline constexpr CompletionItemKind CompletionItemKindColor = CompletionItemKind::Color;
inline constexpr CompletionItemKind CompletionItemKindFile = CompletionItemKind::File;
inline constexpr CompletionItemKind CompletionItemKindReference = CompletionItemKind::Reference;
inline constexpr CompletionItemKind CompletionItemKindFolder = CompletionItemKind::Folder;
inline constexpr CompletionItemKind CompletionItemKindEnumMember = CompletionItemKind::EnumMember;
inline constexpr CompletionItemKind CompletionItemKindConstant = CompletionItemKind::Constant;
inline constexpr CompletionItemKind CompletionItemKindStruct = CompletionItemKind::Struct;
inline constexpr CompletionItemKind CompletionItemKindEvent = CompletionItemKind::Event;
inline constexpr CompletionItemKind CompletionItemKindOperator = CompletionItemKind::Operator;
inline constexpr CompletionItemKind CompletionItemKindTypeParameter = CompletionItemKind::TypeParameter;
std::string String(CompletionItemKind e);

// Completion item tags are extra annotations that tweak the rendering of a completion
// item.
//
// Since: 3.15.0
enum class CompletionItemTag : uint32_t {
    Deprecated = 1,
};

inline constexpr CompletionItemTag CompletionItemTagDeprecated = CompletionItemTag::Deprecated;
std::string String(CompletionItemTag e);

// Defines whether the insert text in a completion item should be interpreted as
// plain text or a snippet.
enum class InsertTextFormat : uint32_t {
    PlainText = 1,
    Snippet = 2,
};

inline constexpr InsertTextFormat InsertTextFormatPlainText = InsertTextFormat::PlainText;
inline constexpr InsertTextFormat InsertTextFormatSnippet = InsertTextFormat::Snippet;
std::string String(InsertTextFormat e);

// How whitespace and indentation is handled during completion
// item insertion.
//
// Since: 3.16.0
enum class InsertTextMode : uint32_t {
    AsIs = 1,
    AdjustIndentation = 2,
};

inline constexpr InsertTextMode InsertTextModeAsIs = InsertTextMode::AsIs;
inline constexpr InsertTextMode InsertTextModeAdjustIndentation = InsertTextMode::AdjustIndentation;
std::string String(InsertTextMode e);

// A document highlight kind.
enum class DocumentHighlightKind : uint32_t {
    Text = 1,
    Read = 2,
    Write = 3,
};

inline constexpr DocumentHighlightKind DocumentHighlightKindText = DocumentHighlightKind::Text;
inline constexpr DocumentHighlightKind DocumentHighlightKindRead = DocumentHighlightKind::Read;
inline constexpr DocumentHighlightKind DocumentHighlightKindWrite = DocumentHighlightKind::Write;
std::string String(DocumentHighlightKind e);

// A set of predefined code action kinds
using CodeActionKind = std::string;

inline const CodeActionKind CodeActionKindEmpty = "";
inline const CodeActionKind CodeActionKindQuickFix = "quickfix";
inline const CodeActionKind CodeActionKindRefactor = "refactor";
inline const CodeActionKind CodeActionKindRefactorExtract = "refactor.extract";
inline const CodeActionKind CodeActionKindRefactorInline = "refactor.inline";
inline const CodeActionKind CodeActionKindRefactorMove = "refactor.move";
inline const CodeActionKind CodeActionKindRefactorRewrite = "refactor.rewrite";
inline const CodeActionKind CodeActionKindSource = "source";
inline const CodeActionKind CodeActionKindSourceOrganizeImports = "source.organizeImports";
inline const CodeActionKind CodeActionKindSourceFixAll = "source.fixAll";

// Code action tags are extra annotations that tweak the behavior of a code action.
//
// Since: 3.18.0
enum class CodeActionTag : uint32_t {
    LLMGenerated = 1,
};

inline constexpr CodeActionTag CodeActionTagLLMGenerated = CodeActionTag::LLMGenerated;
std::string String(CodeActionTag e);

using TraceValue = std::string;

inline const TraceValue TraceValueOff = "off";
inline const TraceValue TraceValueMessages = "messages";
inline const TraceValue TraceValueVerbose = "verbose";

// Describes the content type that a client supports in various
// result literals like `Hover`, `ParameterInfo` or `CompletionItem`.
//
// Please note that `MarkupKinds` must not start with a `$`. This kinds
// are reserved for internal usage.
using MarkupKind = std::string;

inline const MarkupKind MarkupKindPlainText = "plaintext";
inline const MarkupKind MarkupKindMarkdown = "markdown";

// Predefined Language kinds
//
// Since: 3.18.0
using LanguageKind = std::string;

inline const LanguageKind LanguageKindABAP = "abap";
inline const LanguageKind LanguageKindWindowsBat = "bat";
inline const LanguageKind LanguageKindBibTeX = "bibtex";
inline const LanguageKind LanguageKindClojure = "clojure";
inline const LanguageKind LanguageKindCoffeescript = "coffeescript";
inline const LanguageKind LanguageKindC = "c";
inline const LanguageKind LanguageKindCPP = "cpp";
inline const LanguageKind LanguageKindCSharp = "csharp";
inline const LanguageKind LanguageKindCSS = "css";
inline const LanguageKind LanguageKindD = "d";
inline const LanguageKind LanguageKindDelphi = "pascal";
inline const LanguageKind LanguageKindDiff = "diff";
inline const LanguageKind LanguageKindDart = "dart";
inline const LanguageKind LanguageKindDockerfile = "dockerfile";
inline const LanguageKind LanguageKindElixir = "elixir";
inline const LanguageKind LanguageKindErlang = "erlang";
inline const LanguageKind LanguageKindFSharp = "fsharp";
inline const LanguageKind LanguageKindGitCommit = "git-commit";
inline const LanguageKind LanguageKindGitRebase = "git-rebase";
inline const LanguageKind LanguageKindGo = "go";
inline const LanguageKind LanguageKindGroovy = "groovy";
inline const LanguageKind LanguageKindHandlebars = "handlebars";
inline const LanguageKind LanguageKindHaskell = "haskell";
inline const LanguageKind LanguageKindHTML = "html";
inline const LanguageKind LanguageKindIni = "ini";
inline const LanguageKind LanguageKindJava = "java";
inline const LanguageKind LanguageKindJavaScript = "javascript";
inline const LanguageKind LanguageKindJavaScriptReact = "javascriptreact";
inline const LanguageKind LanguageKindJSON = "json";
inline const LanguageKind LanguageKindLaTeX = "latex";
inline const LanguageKind LanguageKindLess = "less";
inline const LanguageKind LanguageKindLua = "lua";
inline const LanguageKind LanguageKindMakefile = "makefile";
inline const LanguageKind LanguageKindMarkdown = "markdown";
inline const LanguageKind LanguageKindObjectiveC = "objective-c";
inline const LanguageKind LanguageKindObjectiveCPP = "objective-cpp";
inline const LanguageKind LanguageKindPascal = "pascal";
inline const LanguageKind LanguageKindPerl = "perl";
inline const LanguageKind LanguageKindPerl6 = "perl6";
inline const LanguageKind LanguageKindPHP = "php";
inline const LanguageKind LanguageKindPlaintext = "plaintext";
inline const LanguageKind LanguageKindPowershell = "powershell";
inline const LanguageKind LanguageKindPug = "jade";
inline const LanguageKind LanguageKindPython = "python";
inline const LanguageKind LanguageKindR = "r";
inline const LanguageKind LanguageKindRazor = "razor";
inline const LanguageKind LanguageKindRuby = "ruby";
inline const LanguageKind LanguageKindRust = "rust";
inline const LanguageKind LanguageKindSCSS = "scss";
inline const LanguageKind LanguageKindSASS = "sass";
inline const LanguageKind LanguageKindScala = "scala";
inline const LanguageKind LanguageKindShaderLab = "shaderlab";
inline const LanguageKind LanguageKindShellScript = "shellscript";
inline const LanguageKind LanguageKindSQL = "sql";
inline const LanguageKind LanguageKindSwift = "swift";
inline const LanguageKind LanguageKindTypeScript = "typescript";
inline const LanguageKind LanguageKindTypeScriptReact = "typescriptreact";
inline const LanguageKind LanguageKindTeX = "tex";
inline const LanguageKind LanguageKindVisualBasic = "vb";
inline const LanguageKind LanguageKindXML = "xml";
inline const LanguageKind LanguageKindXSL = "xsl";
inline const LanguageKind LanguageKindYAML = "yaml";

// Describes how an provider was triggered.
//
// Since: 3.18.0
enum class InlineCompletionTriggerKind : uint32_t {
    Invoked = 1,
    Automatic = 2,
};

inline constexpr InlineCompletionTriggerKind InlineCompletionTriggerKindInvoked = InlineCompletionTriggerKind::Invoked;
inline constexpr InlineCompletionTriggerKind InlineCompletionTriggerKindAutomatic = InlineCompletionTriggerKind::Automatic;
std::string String(InlineCompletionTriggerKind e);

// A set of predefined position encoding kinds.
//
// Since: 3.17.0
using PositionEncodingKind = std::string;

inline const PositionEncodingKind PositionEncodingKindUTF8 = "utf-8";
inline const PositionEncodingKind PositionEncodingKindUTF16 = "utf-16";
inline const PositionEncodingKind PositionEncodingKindUTF32 = "utf-32";

// The file event type
enum class FileChangeType : uint32_t {
    Created = 1,
    Changed = 2,
    Deleted = 3,
};

inline constexpr FileChangeType FileChangeTypeCreated = FileChangeType::Created;
inline constexpr FileChangeType FileChangeTypeChanged = FileChangeType::Changed;
inline constexpr FileChangeType FileChangeTypeDeleted = FileChangeType::Deleted;
std::string String(FileChangeType e);

enum class WatchKind : uint32_t {
    Create = 1,
    Change = 2,
    Delete = 4,
};

inline constexpr WatchKind WatchKindCreate = WatchKind::Create;
inline constexpr WatchKind WatchKindChange = WatchKind::Change;
inline constexpr WatchKind WatchKindDelete = WatchKind::Delete;
std::string String(WatchKind e);

// The diagnostic's severity.
enum class DiagnosticSeverity : uint32_t {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
};

inline constexpr DiagnosticSeverity DiagnosticSeverityError = DiagnosticSeverity::Error;
inline constexpr DiagnosticSeverity DiagnosticSeverityWarning = DiagnosticSeverity::Warning;
inline constexpr DiagnosticSeverity DiagnosticSeverityInformation = DiagnosticSeverity::Information;
inline constexpr DiagnosticSeverity DiagnosticSeverityHint = DiagnosticSeverity::Hint;
std::string String(DiagnosticSeverity e);

// The diagnostic tags.
//
// Since: 3.15.0
enum class DiagnosticTag : uint32_t {
    Unnecessary = 1,
    Deprecated = 2,
};

inline constexpr DiagnosticTag DiagnosticTagUnnecessary = DiagnosticTag::Unnecessary;
inline constexpr DiagnosticTag DiagnosticTagDeprecated = DiagnosticTag::Deprecated;
std::string String(DiagnosticTag e);

// How a completion was triggered
enum class CompletionTriggerKind : uint32_t {
    Invoked = 1,
    TriggerCharacter = 2,
    TriggerForIncompleteCompletions = 3,
};

inline constexpr CompletionTriggerKind CompletionTriggerKindInvoked = CompletionTriggerKind::Invoked;
inline constexpr CompletionTriggerKind CompletionTriggerKindTriggerCharacter = CompletionTriggerKind::TriggerCharacter;
inline constexpr CompletionTriggerKind CompletionTriggerKindTriggerForIncompleteCompletions = CompletionTriggerKind::TriggerForIncompleteCompletions;
std::string String(CompletionTriggerKind e);

// Defines how values from a set of defaults and an individual item will be
// merged.
//
// Since: 3.18.0
enum class ApplyKind : uint32_t {
    Replace = 1,
    Merge = 2,
};

inline constexpr ApplyKind ApplyKindReplace = ApplyKind::Replace;
inline constexpr ApplyKind ApplyKindMerge = ApplyKind::Merge;
std::string String(ApplyKind e);

// How a signature help was triggered.
//
// Since: 3.15.0
enum class SignatureHelpTriggerKind : uint32_t {
    Invoked = 1,
    TriggerCharacter = 2,
    ContentChange = 3,
};

inline constexpr SignatureHelpTriggerKind SignatureHelpTriggerKindInvoked = SignatureHelpTriggerKind::Invoked;
inline constexpr SignatureHelpTriggerKind SignatureHelpTriggerKindTriggerCharacter = SignatureHelpTriggerKind::TriggerCharacter;
inline constexpr SignatureHelpTriggerKind SignatureHelpTriggerKindContentChange = SignatureHelpTriggerKind::ContentChange;
std::string String(SignatureHelpTriggerKind e);

// The reason why code actions were requested.
//
// Since: 3.17.0
enum class CodeActionTriggerKind : uint32_t {
    Invoked = 1,
    Automatic = 2,
};

inline constexpr CodeActionTriggerKind CodeActionTriggerKindInvoked = CodeActionTriggerKind::Invoked;
inline constexpr CodeActionTriggerKind CodeActionTriggerKindAutomatic = CodeActionTriggerKind::Automatic;
std::string String(CodeActionTriggerKind e);

// A pattern kind describing if a glob pattern matches a file a folder or
// both.
//
// Since: 3.16.0
using FileOperationPatternKind = std::string;

inline const FileOperationPatternKind FileOperationPatternKindFile = "file";
inline const FileOperationPatternKind FileOperationPatternKindFolder = "folder";

using ResourceOperationKind = std::string;

inline const ResourceOperationKind ResourceOperationKindCreate = "create";
inline const ResourceOperationKind ResourceOperationKindRename = "rename";
inline const ResourceOperationKind ResourceOperationKindDelete = "delete";

using FailureHandlingKind = std::string;

inline const FailureHandlingKind FailureHandlingKindAbort = "abort";
inline const FailureHandlingKind FailureHandlingKindTransactional = "transactional";
inline const FailureHandlingKind FailureHandlingKindTextOnlyTransactional = "textOnlyTransactional";
inline const FailureHandlingKind FailureHandlingKindUndo = "undo";

enum class PrepareSupportDefaultBehavior : uint32_t {
    Identifier = 1,
};

inline constexpr PrepareSupportDefaultBehavior PrepareSupportDefaultBehaviorIdentifier = PrepareSupportDefaultBehavior::Identifier;
std::string String(PrepareSupportDefaultBehavior e);

using TokenFormat = std::string;

inline const TokenFormat TokenFormatRelative = "relative";

// Layout style for a VSContainerElement's children, mirroring VS's Microsoft.VisualStudio.Text.Adornments.ContainerElementStyle.
enum class VSContainerElementStyle : int32_t {
    Wrapped = 0,
    Stacked = 1,
};

inline constexpr VSContainerElementStyle VSContainerElementStyleWrapped = VSContainerElementStyle::Wrapped;
inline constexpr VSContainerElementStyle VSContainerElementStyleStacked = VSContainerElementStyle::Stacked;
std::string String(VSContainerElementStyle e);

// Log verbosity level, mirroring the VS Code LogLevel enum values.
enum class LogVerbosity : int32_t {
    Off = 0,
    Trace = 1,
    Debug = 2,
    Info = 3,
    Warning = 4,
    Error = 5,
};

inline constexpr LogVerbosity LogVerbosityOff = LogVerbosity::Off;
inline constexpr LogVerbosity LogVerbosityTrace = LogVerbosity::Trace;
inline constexpr LogVerbosity LogVerbosityDebug = LogVerbosity::Debug;
inline constexpr LogVerbosity LogVerbosityInfo = LogVerbosity::Info;
inline constexpr LogVerbosity LogVerbosityWarning = LogVerbosity::Warning;
inline constexpr LogVerbosity LogVerbosityError = LogVerbosity::Error;
std::string String(LogVerbosity e);

// Behavior for tracking and logging flaky diagnostics.
enum class DiagnosticFlakeLogLevel : int32_t {
    Off = 0,
    Log = 1,
    Panic = 2,
};

inline constexpr DiagnosticFlakeLogLevel DiagnosticFlakeLogLevelOff = DiagnosticFlakeLogLevel::Off;
inline constexpr DiagnosticFlakeLogLevel DiagnosticFlakeLogLevelLog = DiagnosticFlakeLogLevel::Log;
inline constexpr DiagnosticFlakeLogLevel DiagnosticFlakeLogLevelPanic = DiagnosticFlakeLogLevel::Panic;
std::string String(DiagnosticFlakeLogLevel e);

enum class VSReferenceKind : int32_t {
    Inactive = 0,
    Comment = 1,
    String = 2,
    Read = 3,
    Write = 4,
    Reference = 5,
    Name = 6,
    Qualified = 7,
    TypeArgument = 8,
    TypeConstraint = 9,
    BaseType = 10,
    Constructor = 11,
    Destructor = 12,
    Import = 13,
    Declaration = 14,
    AddressOf = 15,
    NotReference = 16,
    Unknown = 17,
};

inline constexpr VSReferenceKind VSReferenceKindInactive = VSReferenceKind::Inactive;
inline constexpr VSReferenceKind VSReferenceKindComment = VSReferenceKind::Comment;
inline constexpr VSReferenceKind VSReferenceKindString = VSReferenceKind::String;
inline constexpr VSReferenceKind VSReferenceKindRead = VSReferenceKind::Read;
inline constexpr VSReferenceKind VSReferenceKindWrite = VSReferenceKind::Write;
inline constexpr VSReferenceKind VSReferenceKindReference = VSReferenceKind::Reference;
inline constexpr VSReferenceKind VSReferenceKindName = VSReferenceKind::Name;
inline constexpr VSReferenceKind VSReferenceKindQualified = VSReferenceKind::Qualified;
inline constexpr VSReferenceKind VSReferenceKindTypeArgument = VSReferenceKind::TypeArgument;
inline constexpr VSReferenceKind VSReferenceKindTypeConstraint = VSReferenceKind::TypeConstraint;
inline constexpr VSReferenceKind VSReferenceKindBaseType = VSReferenceKind::BaseType;
inline constexpr VSReferenceKind VSReferenceKindConstructor = VSReferenceKind::Constructor;
inline constexpr VSReferenceKind VSReferenceKindDestructor = VSReferenceKind::Destructor;
inline constexpr VSReferenceKind VSReferenceKindImport = VSReferenceKind::Import;
inline constexpr VSReferenceKind VSReferenceKindDeclaration = VSReferenceKind::Declaration;
inline constexpr VSReferenceKind VSReferenceKindAddressOf = VSReferenceKind::AddressOf;
inline constexpr VSReferenceKind VSReferenceKindNotReference = VSReferenceKind::NotReference;
inline constexpr VSReferenceKind VSReferenceKindUnknown = VSReferenceKind::Unknown;
std::string String(VSReferenceKind e);

using CodeLensKind = std::string;

inline const CodeLensKind CodeLensKindReferences = "references";
inline const CodeLensKind CodeLensKindImplementations = "implementations";

enum class AutoImportFixKind : int32_t {
    UseNamespace = 0,
    JsdocTypeImport = 1,
    AddToExisting = 2,
    AddNew = 3,
    PromoteTypeOnly = 4,
};

inline constexpr AutoImportFixKind AutoImportFixKindUseNamespace = AutoImportFixKind::UseNamespace;
inline constexpr AutoImportFixKind AutoImportFixKindJsdocTypeImport = AutoImportFixKind::JsdocTypeImport;
inline constexpr AutoImportFixKind AutoImportFixKindAddToExisting = AutoImportFixKind::AddToExisting;
inline constexpr AutoImportFixKind AutoImportFixKindAddNew = AutoImportFixKind::AddNew;
inline constexpr AutoImportFixKind AutoImportFixKindPromoteTypeOnly = AutoImportFixKind::PromoteTypeOnly;
std::string String(AutoImportFixKind e);

enum class ImportKind : int32_t {
    Named = 0,
    Default = 1,
    Namespace = 2,
    CommonJS = 3,
};

inline constexpr ImportKind ImportKindNamed = ImportKind::Named;
inline constexpr ImportKind ImportKindDefault = ImportKind::Default;
inline constexpr ImportKind ImportKindNamespace = ImportKind::Namespace;
inline constexpr ImportKind ImportKindCommonJS = ImportKind::CommonJS;
std::string String(ImportKind e);

enum class AddAsTypeOnly : int32_t {
    Allowed = 1,
    Required = 2,
    NotAllowed = 4,
};

inline constexpr AddAsTypeOnly AddAsTypeOnlyAllowed = AddAsTypeOnly::Allowed;
inline constexpr AddAsTypeOnly AddAsTypeOnlyRequired = AddAsTypeOnly::Required;
inline constexpr AddAsTypeOnly AddAsTypeOnlyNotAllowed = AddAsTypeOnly::NotAllowed;
std::string String(AddAsTypeOnly e);

// Roslyn classification type names used by VS for syntax coloring in tooltips and other UI elements.
using ClassificationTypeName = std::string;

inline const ClassificationTypeName ClassificationTypeNameKeyword = "keyword";
inline const ClassificationTypeName ClassificationTypeNamePunctuation = "punctuation";
inline const ClassificationTypeName ClassificationTypeNameOperator = "operator";
inline const ClassificationTypeName ClassificationTypeNameWhiteSpace = "whitespace";
inline const ClassificationTypeName ClassificationTypeNameText = "text";
inline const ClassificationTypeName ClassificationTypeNameString = "string";
inline const ClassificationTypeName ClassificationTypeNameNumber = "number";
inline const ClassificationTypeName ClassificationTypeNameComment = "comment";
inline const ClassificationTypeName ClassificationTypeNameClassName = "class name";
inline const ClassificationTypeName ClassificationTypeNameInterfaceName = "interface name";
inline const ClassificationTypeName ClassificationTypeNameEnumName = "enum name";
inline const ClassificationTypeName ClassificationTypeNameModuleName = "module name";
inline const ClassificationTypeName ClassificationTypeNameMethodName = "method name";
inline const ClassificationTypeName ClassificationTypeNameParameterName = "parameter name";
inline const ClassificationTypeName ClassificationTypeNamePropertyName = "property name";
inline const ClassificationTypeName ClassificationTypeNameFieldName = "field name";
inline const ClassificationTypeName ClassificationTypeNameLocalName = "local name";
inline const ClassificationTypeName ClassificationTypeNameTypeParameterName = "type parameter name";
inline const ClassificationTypeName ClassificationTypeNameIdentifier = "identifier";

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

struct WorkspaceFolder;
struct DidChangeWorkspaceFoldersParams;
struct ConfigurationParams;
struct ColorPresentation;
struct FoldingRange;
struct CallHierarchyIncomingCallsParams;
struct CallHierarchyOutgoingCallsParams;
struct SemanticTokens;
struct SemanticTokensPartialResult;
struct SemanticTokensDelta;
struct SemanticTokensDeltaPartialResult;
struct ShowDocumentParams;
struct ShowDocumentResult;
struct CreateFilesParams;
struct FileOperationRegistrationOptions;
struct RenameFilesParams;
struct DeleteFilesParams;
struct TypeHierarchySupertypesParams;
struct TypeHierarchySubtypesParams;
struct DiagnosticServerCancellationData;
struct InlineCompletionList;
struct TextDocumentContentParams;
struct TextDocumentContentResult;
struct TextDocumentContentRegistrationOptions;
struct TextDocumentContentRefreshParams;
struct RegistrationParams;
struct UnregistrationParams;
struct InitializeResult;
struct InitializeError;
struct InitializedParams;
struct DidChangeConfigurationParams;
struct DidChangeConfigurationRegistrationOptions;
struct MessageActionItem;
struct DidOpenTextDocumentParams;
struct DidChangeWatchedFilesParams;
struct DidChangeWatchedFilesRegistrationOptions;
struct PublishDiagnosticsParams;
struct CompletionList;
struct SignatureHelp;
struct Command;
struct WorkspaceSymbolParams;
struct WorkspaceSymbolRegistrationOptions;
struct ExecuteCommandParams;
struct ExecuteCommandRegistrationOptions;
struct ApplyWorkspaceEditParams;
struct ApplyWorkspaceEditResult;
struct LogTraceParams;
struct WorkDoneProgressParams;
struct PartialResultParams;
struct ImplementationOptions;
struct StaticRegistrationOptions;
struct TypeDefinitionOptions;
struct WorkspaceFoldersChangeEvent;
struct ConfigurationItem;
struct TextDocumentIdentifier;
struct DocumentColorParams;
struct FoldingRangeParams;
struct SemanticTokensParams;
struct SemanticTokensDeltaParams;
struct DocumentDiagnosticParams;
struct DidCloseTextDocumentParams;
struct DidSaveTextDocumentParams;
struct DocumentSymbolParams;
struct CodeLensParams;
struct DocumentLinkParams;
struct DocumentFormattingParams;
struct Color;
struct DocumentColorOptions;
struct FoldingRangeOptions;
struct DeclarationOptions;
struct Position;
struct ImplementationParams;
struct TypeDefinitionParams;
struct DeclarationParams;
struct SelectionRangeParams;
struct CallHierarchyPrepareParams;
struct LinkedEditingRangeParams;
struct MonikerParams;
struct TypeHierarchyPrepareParams;
struct InlineCompletionParams;
struct CompletionParams;
struct HoverParams;
struct SignatureHelpParams;
struct DefinitionParams;
struct ReferenceParams;
struct DocumentHighlightParams;
struct DocumentOnTypeFormattingParams;
struct RenameParams;
struct PrepareRenameParams;
struct TextDocumentPositionParams;
struct Range;
struct Location;
struct ColorInformation;
struct ColorPresentationParams;
struct SelectionRange;
struct CallHierarchyIncomingCall;
struct CallHierarchyOutgoingCall;
struct SemanticTokensRangeParams;
struct LinkedEditingRanges;
struct InlineValueParams;
struct InlayHintParams;
struct TextEdit;
struct DocumentHighlight;
struct CodeActionParams;
struct CodeLens;
struct DocumentLink;
struct DocumentRangeFormattingParams;
struct DocumentRangesFormattingParams;
struct LocationLink;
struct SelectionRangeOptions;
struct CallHierarchyOptions;
struct SemanticTokensOptions;
struct SemanticTokensEdit;
struct LinkedEditingRangeOptions;
struct FileCreate;
struct ChangeAnnotation;
struct FileOperationFilter;
struct FileRename;
struct FileDelete;
struct MonikerOptions;
struct TypeHierarchyOptions;
struct InlineValueContext;
struct InlineValueText;
struct InlineValueVariableLookup;
struct InlineValueEvaluatableExpression;
struct InlineValueOptions;
struct InlayHintLabelPart;
struct InlayHintOptions;
struct DiagnosticOptions;
struct PreviousResultId;
struct WorkspaceDiagnosticParams;
struct InlineCompletionOptions;
struct TextDocumentContentOptions;
struct Registration;
struct RegisterOptions;
struct Unregistration;
struct WorkspaceFoldersInitializeParams;
struct ServerCapabilities;
struct ServerInfo;
struct VersionedTextDocumentIdentifier;
struct SaveOptions;
struct CompletionItemLabelDetails;
struct InsertReplaceEdit;
struct CompletionItemDefaults;
struct CompletionItemApplyKinds;
struct CompletionOptions;
struct HoverOptions;
struct SignatureInformation;
struct SignatureHelpOptions;
struct DefinitionOptions;
struct ReferenceContext;
struct ReferenceOptions;
struct DocumentHighlightOptions;
struct DocumentSymbolOptions;
struct CodeActionDisabled;
struct LocationUriOnly;
struct WorkspaceSymbolOptions;
struct CodeLensOptions;
struct DocumentLinkOptions;
struct FormattingOptions;
struct DocumentFormattingOptions;
struct DocumentRangeFormattingOptions;
struct DocumentOnTypeFormattingOptions;
struct RenameOptions;
struct PrepareRenamePlaceholder;
struct PrepareRenameDefaultBehavior;
struct ExecuteCommandOptions;
struct WorkspaceEditMetadata;
struct WorkDoneProgressOptions;
struct SemanticTokensLegend;
struct SemanticTokensFullDelta;
struct AnnotatedTextEdit;
struct SnippetTextEdit;
struct ResourceOperation;
struct CreateFileOptions;
struct RenameFileOptions;
struct DeleteFileOptions;
struct FileOperationPattern;
struct SelectedCompletionInfo;
struct ClientInfo;
struct ClientCapabilities;
struct TextDocumentSyncOptions;
struct WorkspaceOptions;
struct TextDocumentContentChangePartial;
struct TextDocumentContentChangeWholeDocument;
struct CodeDescription;
struct DiagnosticRelatedInformation;
struct EditRangeWithInsertReplace;
struct ServerCompletionItemOptions;
struct MarkedStringWithLanguage;
struct FileOperationPatternOptions;
struct WorkspaceClientCapabilities;
struct TextDocumentClientCapabilities;
struct WindowClientCapabilities;
struct WorkspaceFoldersServerCapabilities;
struct FileOperationOptions;
struct DidChangeConfigurationClientCapabilities;
struct DidChangeWatchedFilesClientCapabilities;
struct WorkspaceSymbolClientCapabilities;
struct ExecuteCommandClientCapabilities;
struct SemanticTokensWorkspaceClientCapabilities;
struct CodeLensWorkspaceClientCapabilities;
struct FileOperationClientCapabilities;
struct InlineValueWorkspaceClientCapabilities;
struct InlayHintWorkspaceClientCapabilities;
struct DiagnosticWorkspaceClientCapabilities;
struct FoldingRangeWorkspaceClientCapabilities;
struct TextDocumentContentClientCapabilities;
struct TextDocumentSyncClientCapabilities;
struct TextDocumentFilterClientCapabilities;
struct CompletionClientCapabilities;
struct SignatureHelpClientCapabilities;
struct DeclarationClientCapabilities;
struct DefinitionClientCapabilities;
struct TypeDefinitionClientCapabilities;
struct ImplementationClientCapabilities;
struct ReferenceClientCapabilities;
struct DocumentHighlightClientCapabilities;
struct DocumentSymbolClientCapabilities;
struct CodeActionClientCapabilities;
struct CodeLensClientCapabilities;
struct DocumentLinkClientCapabilities;
struct DocumentColorClientCapabilities;
struct DocumentFormattingClientCapabilities;
struct DocumentRangeFormattingClientCapabilities;
struct DocumentOnTypeFormattingClientCapabilities;
struct RenameClientCapabilities;
struct FoldingRangeClientCapabilities;
struct SelectionRangeClientCapabilities;
struct PublishDiagnosticsClientCapabilities;
struct CallHierarchyClientCapabilities;
struct LinkedEditingRangeClientCapabilities;
struct MonikerClientCapabilities;
struct TypeHierarchyClientCapabilities;
struct InlineValueClientCapabilities;
struct InlayHintClientCapabilities;
struct DiagnosticClientCapabilities;
struct InlineCompletionClientCapabilities;
struct ShowMessageRequestClientCapabilities;
struct ShowDocumentClientCapabilities;
struct StaleRequestSupportOptions;
struct RegularExpressionsClientCapabilities;
struct MarkdownClientCapabilities;
struct TextDocumentFilterLanguage;
struct TextDocumentFilterScheme;
struct ChangeAnnotationsSupportOptions;
struct ClientSymbolResolveOptions;
struct CompletionListCapabilities;
struct ClientCodeActionLiteralOptions;
struct ClientCodeActionResolveOptions;
struct ClientCodeLensResolveOptions;
struct ClientFoldingRangeOptions;
struct DiagnosticsCapabilities;
struct ClientSemanticTokensRequestOptions;
struct ClientInlayHintResolveOptions;
struct ClientShowMessageActionItemOptions;
struct ClientCompletionItemResolveOptions;
struct ClientSignatureParameterInformationOptions;
struct ClientSemanticTokensRequestFullDelta;
struct InitializationOptions;
struct CompletionItemData;
struct ExperimentalServerCapabilities;
struct ExperimentalClientCapabilities;
struct VSOnAutoInsertOptions;
struct VSOnAutoInsertParams;
struct RequestFailureTelemetryProperties;
struct ProfileParams;
struct ProfileResult;
struct InitializeAPISessionParams;
struct InitializeAPISessionResult;
struct ProjectInfoParams;
struct ProjectInfoResult;
struct ContentMapperManifest;
struct InferredProjectContentMapperContribution;
struct ContentMapperContribution;
struct SetContentMapperContributionsParams;
struct PerformanceStatsTelemetryMeasurements;
struct ProjectInfoTelemetryMeasurements;
struct MultiDocumentHighlight;
struct MultiDocumentHighlightParams;
struct CallHierarchyItemData;
struct TypeHierarchyItemData;
struct InlayHintData;
struct CodeActionData;
struct WorkspaceSymbolData;
struct DocumentLinkData;
struct DiagnosticData;
struct CompletionItemDefaultsData;
struct ClientFoldingRangeKindOptions;
struct ClientSymbolKindOptions;
struct CallHierarchyItem;
struct TypeHierarchyItem;
struct SymbolInformation;
struct DocumentSymbol;
struct BaseSymbolInformation;
struct ClientSymbolTagOptions;
struct Moniker;
struct ShowMessageParams;
struct ShowMessageRequestParams;
struct LogMessageParams;
struct WillSaveTextDocumentParams;
struct ClientCompletionItemOptionsKind;
struct CompletionItem;
struct CompletionItemTagOptions;
struct VSOnAutoInsertResponseItem;
struct ClientCompletionItemInsertTextModeOptions;
struct CodeActionContext;
struct CodeActionOptions;
struct CodeActionKindDocumentation;
struct ClientCodeActionKindOptions;
struct CodeAction;
struct CodeActionTagOptions;
struct SetTraceParams;
struct MarkupContent;
struct HoverClientCapabilities;
struct ClientCompletionItemOptions;
struct ClientSignatureInformationOptions;
struct TextDocumentItem;
struct InlineCompletionContext;
struct GeneralClientCapabilities;
struct FileEvent;
struct ClientDiagnosticsTagOptions;
struct CompletionContext;
struct SignatureHelpContext;
struct WorkspaceEditClientCapabilities;
struct SemanticTokensClientCapabilities;
struct SetLogVerbosityParams;
struct VSReferenceItem;
struct CodeLensData;
struct AutoImportFix;
struct IntegerOrString;
struct WorkDoneProgressCreateParams;
struct WorkDoneProgressCancelParams;
struct CancelParams;
struct BooleanOrEmptyObject;
struct BooleanOrSemanticTokensFullDelta;
struct TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile;
struct WorkspaceEdit;
struct StringOrInlayHintLabelParts;
struct InlayHint;
struct StringOrMarkupContent;
struct Diagnostic;
struct WorkspaceFullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport;
struct WorkspaceDiagnosticReport;
struct WorkspaceDiagnosticReportPartialResult;
struct StringOrStringValue;
struct InlineCompletionItem;
struct IntegerOrNull;
struct OptionalVersionedTextDocumentIdentifier;
struct StringOrNull;
struct DocumentUriOrNull;
struct InitializeParams;
struct InitializationOptionsOrNull;
struct WorkspaceFoldersOrNull;
struct StringOrStrings;
struct TextDocumentContentChangePartialOrWholeDocument;
struct DidChangeTextDocumentParams;
struct TextEditOrInsertReplaceEdit;
struct UintegerOrNull;
struct LocationOrLocationUriOnly;
struct WorkspaceSymbol;
struct WorkDoneProgressBeginOrReportOrEnd;
struct ProgressParams;
struct TextEditOrAnnotatedTextEditOrSnippetTextEdit;
struct TextDocumentEdit;
struct FullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport;
struct DocumentDiagnosticReportPartialResult;
struct TextDocumentSyncOptionsOrKind;
struct BooleanOrHoverOptions;
struct BooleanOrDeclarationOptionsOrDeclarationRegistrationOptions;
struct BooleanOrDefinitionOptions;
struct BooleanOrTypeDefinitionOptionsOrTypeDefinitionRegistrationOptions;
struct BooleanOrImplementationOptionsOrImplementationRegistrationOptions;
struct BooleanOrReferenceOptions;
struct BooleanOrDocumentHighlightOptions;
struct BooleanOrDocumentSymbolOptions;
struct BooleanOrCodeActionOptions;
struct BooleanOrDocumentColorOptionsOrDocumentColorRegistrationOptions;
struct BooleanOrWorkspaceSymbolOptions;
struct BooleanOrDocumentFormattingOptions;
struct BooleanOrDocumentRangeFormattingOptions;
struct BooleanOrRenameOptions;
struct BooleanOrFoldingRangeOptionsOrFoldingRangeRegistrationOptions;
struct BooleanOrSelectionRangeOptionsOrSelectionRangeRegistrationOptions;
struct BooleanOrCallHierarchyOptionsOrCallHierarchyRegistrationOptions;
struct BooleanOrLinkedEditingRangeOptionsOrLinkedEditingRangeRegistrationOptions;
struct SemanticTokensOptionsOrRegistrationOptions;
struct BooleanOrMonikerOptionsOrMonikerRegistrationOptions;
struct BooleanOrTypeHierarchyOptionsOrTypeHierarchyRegistrationOptions;
struct BooleanOrInlineValueOptionsOrInlineValueRegistrationOptions;
struct BooleanOrInlayHintOptionsOrInlayHintRegistrationOptions;
struct DiagnosticOptionsOrRegistrationOptions;
struct BooleanOrInlineCompletionOptions;
struct PatternOrRelativePattern;
struct FileSystemWatcher;
struct TextDocumentFilterPattern;
struct RangeOrEditRangeWithInsertReplace;
struct BooleanOrSaveOptions;
struct TextDocumentContentOptionsOrRegistrationOptions;
struct StringOrTuple;
struct ParameterInformation;
struct StringOrBoolean;
struct WorkspaceFolderOrURI;
struct RelativePattern;
struct BooleanOrClientSemanticTokensRequestFullDelta;
struct VSImageElementOrClassifiedTextElementOrContainerElement;
struct LocationOrLocationsOrDefinitionLinksOrNull;
struct FoldingRangesOrNull;
struct LocationOrLocationsOrDeclarationLinksOrNull;
struct SelectionRangesOrNull;
struct CallHierarchyItemsOrNull;
struct CallHierarchyIncomingCallsOrNull;
struct CallHierarchyOutgoingCallsOrNull;
struct SemanticTokensOrNull;
struct SemanticTokensOrSemanticTokensDeltaOrNull;
struct LinkedEditingRangesOrNull;
struct WorkspaceEditOrNull;
struct MonikersOrNull;
struct TypeHierarchyItemsOrNull;
struct InlayHintsOrNull;
struct RelatedFullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport;
struct InlineCompletionListOrItemsOrNull;
struct MessageActionItemOrNull;
struct TextEditsOrNull;
struct CompletionItemsOrListOrNull;
struct HoverOrNull;
struct SignatureHelpOrNull;
struct LocationsOrNull;
struct DocumentHighlightsOrNull;
struct SymbolInformationsOrDocumentSymbolsOrNull;
struct CommandOrCodeAction;
struct CommandOrCodeActionArrayOrNull;
struct SymbolInformationsOrWorkspaceSymbolsOrNull;
struct CodeLensesOrNull;
struct DocumentLinksOrNull;
struct RangeOrPrepareRenamePlaceholderOrPrepareRenameDefaultBehaviorOrNull;
struct LSPAnyOrNull;
struct MultiDocumentHighlightsOrNull;
struct VSOnAutoInsertResponseItemOrNull;
struct VSReferenceItemsOrNull;
struct RequestFailureTelemetryEventOrPerformanceStatsTelemetryEventOrProjectInfoTelemetryEventOrNull;
struct TextDocumentFilterLanguageOrSchemeOrPattern;
struct DocumentSelectorOrNull;
struct ImplementationRegistrationOptions;
struct TypeDefinitionRegistrationOptions;
struct DocumentColorRegistrationOptions;
struct FoldingRangeRegistrationOptions;
struct DeclarationRegistrationOptions;
struct SelectionRangeRegistrationOptions;
struct CallHierarchyRegistrationOptions;
struct SemanticTokensRegistrationOptions;
struct LinkedEditingRangeRegistrationOptions;
struct MonikerRegistrationOptions;
struct TypeHierarchyRegistrationOptions;
struct InlineValueRegistrationOptions;
struct InlayHintRegistrationOptions;
struct DiagnosticRegistrationOptions;
struct InlineCompletionRegistrationOptions;
struct TextDocumentRegistrationOptions;
struct TextDocumentChangeRegistrationOptions;
struct TextDocumentSaveRegistrationOptions;
struct CompletionRegistrationOptions;
struct HoverRegistrationOptions;
struct SignatureHelpRegistrationOptions;
struct DefinitionRegistrationOptions;
struct ReferenceRegistrationOptions;
struct DocumentHighlightRegistrationOptions;
struct DocumentSymbolRegistrationOptions;
struct CodeActionRegistrationOptions;
struct CodeLensRegistrationOptions;
struct DocumentLinkRegistrationOptions;
struct DocumentFormattingRegistrationOptions;
struct DocumentRangeFormattingRegistrationOptions;
struct DocumentOnTypeFormattingRegistrationOptions;
struct RenameRegistrationOptions;
struct StringOrMarkedStringWithLanguage;
struct MarkupContentOrStringOrMarkedStringWithLanguageOrMarkedStrings;
struct Hover;
struct InlineValueTextOrVariableLookupOrEvaluatableExpression;
struct InlineValuesOrNull;
struct StringLiteralBegin;
struct WorkDoneProgressBegin;
struct StringLiteralReport;
struct WorkDoneProgressReport;
struct StringLiteralEnd;
struct WorkDoneProgressEnd;
struct StringLiteralCreate;
struct CreateFile;
struct StringLiteralRename;
struct RenameFile;
struct StringLiteralDelete;
struct DeleteFile;
struct StringLiteralFull;
struct RelatedFullDocumentDiagnosticReport;
struct FullDocumentDiagnosticReport;
struct WorkspaceFullDocumentDiagnosticReport;
struct StringLiteralUnchanged;
struct RelatedUnchangedDocumentDiagnosticReport;
struct UnchangedDocumentDiagnosticReport;
struct WorkspaceUnchangedDocumentDiagnosticReport;
struct StringLiteralSnippet;
struct StringValue;
struct StringLiteralLanguageServerErrorResponse;
struct StringLiteralError;
struct RequestFailureTelemetryEvent;
struct StringLiteralLanguageServerPerformanceStats;
struct StringLiteralUsage;
struct PerformanceStatsTelemetryEvent;
struct StringLiteralLanguageServerProjectInfo;
struct ProjectInfoTelemetryEvent;
struct StringLiteralClassifiedTextRun;
struct VSClassifiedTextRun;
struct StringLiteralClassifiedTextElement;
struct VSClassifiedTextElement;
struct StringLiteralImageId;
struct VSImageId;
struct StringLiteralImageElement;
struct VSImageElement;
struct StringLiteralContainerElement;
struct VSContainerElement;
struct ResolvedChangeAnnotationsSupportOptions;
struct ResolvedWorkspaceEditClientCapabilities;
struct ResolvedDidChangeConfigurationClientCapabilities;
struct ResolvedDidChangeWatchedFilesClientCapabilities;
struct ResolvedClientSymbolKindOptions;
struct ResolvedClientSymbolTagOptions;
struct ResolvedClientSymbolResolveOptions;
struct ResolvedWorkspaceSymbolClientCapabilities;
struct ResolvedExecuteCommandClientCapabilities;
struct ResolvedSemanticTokensWorkspaceClientCapabilities;
struct ResolvedCodeLensWorkspaceClientCapabilities;
struct ResolvedFileOperationClientCapabilities;
struct ResolvedInlineValueWorkspaceClientCapabilities;
struct ResolvedInlayHintWorkspaceClientCapabilities;
struct ResolvedDiagnosticWorkspaceClientCapabilities;
struct ResolvedFoldingRangeWorkspaceClientCapabilities;
struct ResolvedTextDocumentContentClientCapabilities;
struct ResolvedWorkspaceClientCapabilities;
struct ResolvedTextDocumentSyncClientCapabilities;
struct ResolvedTextDocumentFilterClientCapabilities;
struct ResolvedCompletionItemTagOptions;
struct ResolvedClientCompletionItemResolveOptions;
struct ResolvedClientCompletionItemInsertTextModeOptions;
struct ResolvedClientCompletionItemOptions;
struct ResolvedClientCompletionItemOptionsKind;
struct ResolvedCompletionListCapabilities;
struct ResolvedCompletionClientCapabilities;
struct ResolvedHoverClientCapabilities;
struct ResolvedClientSignatureParameterInformationOptions;
struct ResolvedClientSignatureInformationOptions;
struct ResolvedSignatureHelpClientCapabilities;
struct ResolvedDeclarationClientCapabilities;
struct ResolvedDefinitionClientCapabilities;
struct ResolvedTypeDefinitionClientCapabilities;
struct ResolvedImplementationClientCapabilities;
struct ResolvedReferenceClientCapabilities;
struct ResolvedDocumentHighlightClientCapabilities;
struct ResolvedDocumentSymbolClientCapabilities;
struct ResolvedClientCodeActionKindOptions;
struct ResolvedClientCodeActionLiteralOptions;
struct ResolvedClientCodeActionResolveOptions;
struct ResolvedCodeActionTagOptions;
struct ResolvedCodeActionClientCapabilities;
struct ResolvedClientCodeLensResolveOptions;
struct ResolvedCodeLensClientCapabilities;
struct ResolvedDocumentLinkClientCapabilities;
struct ResolvedDocumentColorClientCapabilities;
struct ResolvedDocumentFormattingClientCapabilities;
struct ResolvedDocumentRangeFormattingClientCapabilities;
struct ResolvedDocumentOnTypeFormattingClientCapabilities;
struct ResolvedRenameClientCapabilities;
struct ResolvedClientFoldingRangeKindOptions;
struct ResolvedClientFoldingRangeOptions;
struct ResolvedFoldingRangeClientCapabilities;
struct ResolvedSelectionRangeClientCapabilities;
struct ResolvedClientDiagnosticsTagOptions;
struct ResolvedPublishDiagnosticsClientCapabilities;
struct ResolvedCallHierarchyClientCapabilities;
struct ResolvedClientSemanticTokensRequestOptions;
struct ResolvedSemanticTokensClientCapabilities;
struct ResolvedLinkedEditingRangeClientCapabilities;
struct ResolvedMonikerClientCapabilities;
struct ResolvedTypeHierarchyClientCapabilities;
struct ResolvedInlineValueClientCapabilities;
struct ResolvedClientInlayHintResolveOptions;
struct ResolvedInlayHintClientCapabilities;
struct ResolvedDiagnosticClientCapabilities;
struct ResolvedInlineCompletionClientCapabilities;
struct ResolvedTextDocumentClientCapabilities;
struct ResolvedClientShowMessageActionItemOptions;
struct ResolvedShowMessageRequestClientCapabilities;
struct ResolvedShowDocumentClientCapabilities;
struct ResolvedWindowClientCapabilities;
struct ResolvedStaleRequestSupportOptions;
struct ResolvedRegularExpressionsClientCapabilities;
struct ResolvedMarkdownClientCapabilities;
struct ResolvedGeneralClientCapabilities;
struct ResolvedExperimentalClientCapabilities;
struct ResolvedClientCapabilities;
// ---------------------------------------------------------------------------
// Structures — lsp_generated.go:15
// ---------------------------------------------------------------------------

// A workspace folder inside a client.
struct WorkspaceFolder {
    lsproto::URI Uri;
    std::string Name;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters of a `workspace/didChangeWorkspaceFolders` notification.
struct DidChangeWorkspaceFoldersParams {
    std::shared_ptr<lsproto::WorkspaceFoldersChangeEvent> Event;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters of a configuration request.
struct ConfigurationParams {
    Slice<std::shared_ptr<lsproto::ConfigurationItem>> Items;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct ColorPresentation {
    std::string Label;
    std::shared_ptr<lsproto::TextEdit> TextEdit;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::TextEdit>>> AdditionalTextEdits;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents a folding range. To be valid, start and end line must be bigger than zero and smaller
// than the number of lines in the document. Clients are free to ignore invalid ranges.
struct FoldingRange {
    uint32_t StartLine;
    std::optional<uint32_t> StartCharacter;
    uint32_t EndLine;
    std::optional<uint32_t> EndCharacter;
    std::shared_ptr<lsproto::FoldingRangeKind> Kind;
    std::optional<std::string> CollapsedText;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameter of a `callHierarchy/incomingCalls` request.
//
// Since: 3.16.0
struct CallHierarchyIncomingCallsParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::shared_ptr<lsproto::CallHierarchyItem> Item;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameter of a `callHierarchy/outgoingCalls` request.
//
// Since: 3.16.0
struct CallHierarchyOutgoingCallsParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::shared_ptr<lsproto::CallHierarchyItem> Item;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.16.0
struct SemanticTokens {
    std::optional<std::string> ResultId;
    Slice<uint32_t> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.16.0
struct SemanticTokensPartialResult {
    Slice<uint32_t> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.16.0
struct SemanticTokensDelta {
    std::optional<std::string> ResultId;
    Slice<std::shared_ptr<lsproto::SemanticTokensEdit>> Edits;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.16.0
struct SemanticTokensDeltaPartialResult {
    Slice<std::shared_ptr<lsproto::SemanticTokensEdit>> Edits;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Params to show a resource in the UI.
//
// Since: 3.16.0
struct ShowDocumentParams {
    lsproto::URI Uri;
    std::optional<bool> External;
    std::optional<bool> TakeFocus;
    std::shared_ptr<lsproto::Range> Selection;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The result of a showDocument request.
//
// Since: 3.16.0
struct ShowDocumentResult {
    bool Success;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters sent in notifications/requests for user-initiated creation of
// files.
//
// Since: 3.16.0
struct CreateFilesParams {
    Slice<std::shared_ptr<lsproto::FileCreate>> Files;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The options to register for file operations.
//
// Since: 3.16.0
struct FileOperationRegistrationOptions {
    Slice<std::shared_ptr<lsproto::FileOperationFilter>> Filters;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters sent in notifications/requests for user-initiated renames of
// files.
//
// Since: 3.16.0
struct RenameFilesParams {
    Slice<std::shared_ptr<lsproto::FileRename>> Files;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters sent in notifications/requests for user-initiated deletes of
// files.
//
// Since: 3.16.0
struct DeleteFilesParams {
    Slice<std::shared_ptr<lsproto::FileDelete>> Files;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameter of a `typeHierarchy/supertypes` request.
//
// Since: 3.17.0
struct TypeHierarchySupertypesParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::shared_ptr<lsproto::TypeHierarchyItem> Item;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameter of a `typeHierarchy/subtypes` request.
//
// Since: 3.17.0
struct TypeHierarchySubtypesParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::shared_ptr<lsproto::TypeHierarchyItem> Item;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Cancellation data returned from a diagnostic request.
//
// Since: 3.17.0
struct DiagnosticServerCancellationData {
    bool RetriggerRequest;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents a collection of items to be presented in the editor.
//
// Since: 3.18.0
struct InlineCompletionList {
    Slice<std::shared_ptr<lsproto::InlineCompletionItem>> Items;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Parameters for the `workspace/textDocumentContent` request.
//
// Since: 3.18.0
struct TextDocumentContentParams {
    lsproto::DocumentUri Uri;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Result of the `workspace/textDocumentContent` request.
//
// Since: 3.18.0
struct TextDocumentContentResult {
    std::string Text;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Text document content provider registration options.
//
// Since: 3.18.0
struct TextDocumentContentRegistrationOptions {
    Slice<std::string> Schemes;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Parameters for the `workspace/textDocumentContent/refresh` request.
//
// Since: 3.18.0
struct TextDocumentContentRefreshParams {
    lsproto::DocumentUri Uri;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct RegistrationParams {
    Slice<std::shared_ptr<lsproto::Registration>> Registrations;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct UnregistrationParams {
    Slice<std::shared_ptr<lsproto::Unregistration>> Unregisterations;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The result returned from an initialize request.
struct InitializeResult {
    std::shared_ptr<lsproto::ServerCapabilities> Capabilities;
    std::shared_ptr<lsproto::ServerInfo> ServerInfo;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The data type of the ResponseError if the
// initialize request fails.
struct InitializeError {
    bool Retry;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct InitializedParams {
    std::vector<StructFieldBinding> fieldBindings() { return {}; }
    std::string unmarshalJSONFrom(json::Decoder& dec) { return unmarshalStructReflectGo(dec, {}); }
    std::string marshalJSONTo(json::Encoder& enc) const { return enc.writeToken(json::BeginObject).empty() ? enc.writeToken(json::EndObject) : "json: write error"; }
    bool isZero() const { return true; }
};

// The parameters of a change configuration notification.
struct DidChangeConfigurationParams {
    lsproto::LSPAny Settings;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct DidChangeConfigurationRegistrationOptions {
    std::shared_ptr<lsproto::StringOrStrings> Section;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct MessageActionItem {
    std::string Title;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters sent in an open text document notification
struct DidOpenTextDocumentParams {
    std::shared_ptr<lsproto::TextDocumentItem> TextDocument;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The watched files change notification's parameters.
struct DidChangeWatchedFilesParams {
    Slice<std::shared_ptr<lsproto::FileEvent>> Changes;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Describe options to be used when registered for text document change events.
struct DidChangeWatchedFilesRegistrationOptions {
    Slice<std::shared_ptr<lsproto::FileSystemWatcher>> Watchers;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The publish diagnostic notification's parameters.
struct PublishDiagnosticsParams {
    lsproto::DocumentUri Uri;
    std::optional<int32_t> Version;
    Slice<std::shared_ptr<lsproto::Diagnostic>> Diagnostics;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents a collection of items to be presented
// in the editor.
struct CompletionList {
    bool IsIncomplete;
    std::shared_ptr<lsproto::CompletionItemDefaults> ItemDefaults;
    std::shared_ptr<lsproto::CompletionItemApplyKinds> ApplyKind;
    Slice<std::shared_ptr<lsproto::CompletionItem>> Items;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Signature help represents the signature of something
// callable. There can be multiple signature but only one
// active and only one active parameter.
struct SignatureHelp {
    Slice<std::shared_ptr<lsproto::SignatureInformation>> Signatures;
    std::optional<uint32_t> ActiveSignature;
    std::shared_ptr<lsproto::UintegerOrNull> ActiveParameter;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents a reference to a command. Provides a title which
// will be used to represent a command in the UI and, optionally,
// an array of arguments which will be passed to the command handler
// function when invoked.
struct Command {
    std::string Title;
    std::optional<std::string> Tooltip;
    std::string Command;
    std::shared_ptr<Slice<lsproto::LSPAny>> Arguments;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters of a WorkspaceSymbolRequest.
struct WorkspaceSymbolParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::string Query;
    std::shared_ptr<lsproto::TextDocumentIdentifier> TextDocument;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a WorkspaceSymbolRequest.
struct WorkspaceSymbolRegistrationOptions {
    std::optional<bool> WorkDoneProgress;
    std::optional<bool> ResolveProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters of a ExecuteCommandRequest.
struct ExecuteCommandParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::string Command;
    std::shared_ptr<Slice<lsproto::LSPAny>> Arguments;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a ExecuteCommandRequest.
struct ExecuteCommandRegistrationOptions {
    std::optional<bool> WorkDoneProgress;
    Slice<std::string> Commands;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters passed via an apply workspace edit request.
struct ApplyWorkspaceEditParams {
    std::optional<std::string> Label;
    std::shared_ptr<lsproto::WorkspaceEdit> Edit;
    std::shared_ptr<lsproto::WorkspaceEditMetadata> Metadata;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The result returned from the apply workspace edit request.
//
// Since: 3.17 renamed from ApplyWorkspaceEditResponse
struct ApplyWorkspaceEditResult {
    bool Applied;
    std::optional<std::string> FailureReason;
    std::optional<uint32_t> FailedChange;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct LogTraceParams {
    std::string Message;
    std::optional<std::string> Verbose;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct WorkDoneProgressParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct PartialResultParams {
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct ImplementationOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Static registration options to be returned in the initialize
// request.
struct StaticRegistrationOptions {
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct TypeDefinitionOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The workspace folder change event.
struct WorkspaceFoldersChangeEvent {
    Slice<std::shared_ptr<lsproto::WorkspaceFolder>> Added;
    Slice<std::shared_ptr<lsproto::WorkspaceFolder>> Removed;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct ConfigurationItem {
    std::shared_ptr<lsproto::URI> ScopeUri;
    std::optional<std::string> Section;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A literal to identify a text document in the client.
struct TextDocumentIdentifier {
    lsproto::DocumentUri Uri;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Parameters for a DocumentColorRequest.
struct DocumentColorParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    lsproto::TextDocumentIdentifier TextDocument;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// Parameters for a FoldingRangeRequest.
struct FoldingRangeParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    lsproto::TextDocumentIdentifier TextDocument;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// Since: 3.16.0
struct SemanticTokensParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    lsproto::TextDocumentIdentifier TextDocument;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// Since: 3.16.0
struct SemanticTokensDeltaParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    lsproto::TextDocumentIdentifier TextDocument;
    std::string PreviousResultId;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// Parameters of the document diagnostic request.
//
// Since: 3.17.0
struct DocumentDiagnosticParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    lsproto::TextDocumentIdentifier TextDocument;
    std::optional<std::string> Identifier;
    std::optional<std::string> PreviousResultId;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// The parameters sent in a close text document notification
struct DidCloseTextDocumentParams {
    lsproto::TextDocumentIdentifier TextDocument;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// The parameters sent in a save text document notification
struct DidSaveTextDocumentParams {
    lsproto::TextDocumentIdentifier TextDocument;
    std::optional<std::string> Text;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// Parameters for a DocumentSymbolRequest.
struct DocumentSymbolParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    lsproto::TextDocumentIdentifier TextDocument;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// The parameters of a CodeLensRequest.
struct CodeLensParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    lsproto::TextDocumentIdentifier TextDocument;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// The parameters of a DocumentLinkRequest.
struct DocumentLinkParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    lsproto::TextDocumentIdentifier TextDocument;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// The parameters of a DocumentFormattingRequest.
struct DocumentFormattingParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    lsproto::TextDocumentIdentifier TextDocument;
    std::shared_ptr<lsproto::FormattingOptions> Options;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// Represents a color in RGBA space.
struct Color {
    double Red;
    double Green;
    double Blue;
    double Alpha;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct DocumentColorOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct FoldingRangeOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct DeclarationOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Position in a text document expressed as zero-based line and character
// offset. Prior to 3.17 the offsets were always based on a UTF-16 string
// representation. So a string of the form `a𐐀b` the character offset of the
// character `a` is 0, the character offset of `𐐀` is 1 and the character
// offset of b is 3 since `𐐀` is represented using two code units in UTF-16.
// Since 3.17 clients and servers can agree on a different string encoding
// representation (e.g. UTF-8). The client announces it's supported encoding
// via the client capability [`general.positionEncodings`](https://microsoft.github.io/language-server-protocol/specifications/specification-current/#clientCapabilities).
// The value is an array of position encodings the client supports, with
// decreasing preference (e.g. the encoding at index `0` is the most preferred
// one). To stay backwards compatible the only mandatory encoding is UTF-16
// represented via the string `utf-16`. The server can pick one of the
// encodings offered by the client and signals that encoding back to the
// client via the initialize result's property
// [`capabilities.positionEncoding`](https://microsoft.github.io/language-server-protocol/specifications/specification-current/#serverCapabilities). If the string value
// `utf-16` is missing from the client's capability `general.positionEncodings`
// servers can safely assume that the client supports UTF-16. If the server
// omits the position encoding in its initialize result the encoding defaults
// to the string value `utf-16`. Implementation considerations: since the
// conversion from one encoding into another requires the content of the
// file / line the conversion is best done where the file is read which is
// usually on the server side.
//
// Positions are line end character agnostic. So you can not specify a position
// that denotes `\r|\n` or `\n|` where `|` represents the character offset.
//
// Since: 3.17.0 - support for negotiated position encoding.
struct Position {
    uint32_t Line;
    uint32_t Character;
    bool operator==(const Position&) const = default;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    int Compare(const Position* other) const;
};

// Meta model version 3.18.0
// Structures
struct ImplementationParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

struct TypeDefinitionParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

struct DeclarationParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// A parameter literal used in selection range requests.
struct SelectionRangeParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    lsproto::TextDocumentIdentifier TextDocument;
    Slice<lsproto::Position> Positions;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// The parameter of a `textDocument/prepareCallHierarchy` request.
//
// Since: 3.16.0
struct CallHierarchyPrepareParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

struct LinkedEditingRangeParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

struct MonikerParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// The parameter of a `textDocument/prepareTypeHierarchy` request.
//
// Since: 3.17.0
struct TypeHierarchyPrepareParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// A parameter literal used in inline completion requests.
//
// Since: 3.18.0
struct InlineCompletionParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::InlineCompletionContext> Context;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// Completion parameters
struct CompletionParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::shared_ptr<lsproto::CompletionContext> Context;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// Parameters for a HoverRequest.
struct HoverParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::optional<int32_t> VerbosityLevel;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// Parameters for a SignatureHelpRequest.
struct SignatureHelpParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::SignatureHelpContext> Context;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// Parameters for a DefinitionRequest.
struct DefinitionParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// Parameters for a ReferencesRequest.
struct ReferenceParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::shared_ptr<lsproto::ReferenceContext> Context;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// Parameters for a DocumentHighlightRequest.
struct DocumentHighlightParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// The parameters of a DocumentOnTypeFormattingRequest.
struct DocumentOnTypeFormattingParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::string Ch;
    std::shared_ptr<lsproto::FormattingOptions> Options;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// The parameters of a RenameRequest.
struct RenameParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::string NewName;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

struct PrepareRenameParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// A parameter literal used in requests to pass a text document and a position inside that
// document.
struct TextDocumentPositionParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// A range in a text document expressed as (zero-based) start and end positions.
//
// If you want to specify a range that contains a line including the line ending
// character(s) then use an end position denoting the start of the next line.
// For example:
// ```ts
//
//	{
//	 start: { line: 5, character: 23 }
//	 end : { line 6, character : 0 }
//	}
//
// ```
struct Range {
    lsproto::Position Start;
    lsproto::Position End;
    bool operator==(const Range&) const = default;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    int Compare(const Range* other) const;
};

// Represents a location inside a resource, such as a line
// inside a text file.
struct Location {
    lsproto::DocumentUri Uri;
    lsproto::Range Range;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    struct Location GetLocation() const;
};

// Represents a color range from a document.
struct ColorInformation {
    lsproto::Range Range;
    lsproto::Color Color;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Parameters for a ColorPresentationRequest.
struct ColorPresentationParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Color Color;
    lsproto::Range Range;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// A selection range represents a part of a selection hierarchy. A selection range
// may have a parent selection range that contains it.
struct SelectionRange {
    lsproto::Range Range;
    std::shared_ptr<lsproto::SelectionRange> Parent;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents an incoming call, e.g. a caller of a method or constructor.
//
// Since: 3.16.0
struct CallHierarchyIncomingCall {
    std::shared_ptr<lsproto::CallHierarchyItem> From;
    Slice<lsproto::Range> FromRanges;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents an outgoing call, e.g. calling a getter from a method or a method from a constructor etc.
//
// Since: 3.16.0
struct CallHierarchyOutgoingCall {
    std::shared_ptr<lsproto::CallHierarchyItem> To;
    Slice<lsproto::Range> FromRanges;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.16.0
struct SemanticTokensRangeParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Range Range;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// The result of a linked editing range request.
//
// Since: 3.16.0
struct LinkedEditingRanges {
    Slice<lsproto::Range> Ranges;
    std::optional<std::string> WordPattern;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A parameter literal used in inline value requests.
//
// Since: 3.17.0
struct InlineValueParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Range Range;
    std::shared_ptr<lsproto::InlineValueContext> Context;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// A parameter literal used in inlay hint requests.
//
// Since: 3.17.0
struct InlayHintParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Range Range;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// A text edit applicable to a text document.
struct TextEdit {
    lsproto::Range Range;
    std::string NewText;
    bool operator==(const TextEdit&) const = default;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    int Compare(const TextEdit* other) const;
};

// A document highlight is a range inside a text document which deserves
// special attention. Usually a document highlight is visualized by changing
// the background color of its range.
struct DocumentHighlight {
    lsproto::Range Range;
    std::shared_ptr<lsproto::DocumentHighlightKind> Kind;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters of a CodeActionRequest.
struct CodeActionParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Range Range;
    std::shared_ptr<lsproto::CodeActionContext> Context;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// A code lens represents a command that should be shown along with
// source text, like the number of references, a way to run tests, etc.
//
// A code lens is _unresolved_ when no command is associated to it. For performance
// reasons the creation of a code lens and resolving should be done in two stages.
struct CodeLens {
    lsproto::Range Range;
    std::shared_ptr<lsproto::Command> Command;
    std::shared_ptr<lsproto::CodeLensData> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A document link is a range in a text document that links to an internal or external resource, like another
// text document or a web site.
struct DocumentLink {
    lsproto::Range Range;
    std::shared_ptr<lsproto::URI> Target;
    std::optional<std::string> Tooltip;
    std::shared_ptr<lsproto::DocumentLinkData> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters of a DocumentRangeFormattingRequest.
struct DocumentRangeFormattingParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Range Range;
    std::shared_ptr<lsproto::FormattingOptions> Options;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// The parameters of a DocumentRangesFormattingRequest.
//
// Since: 3.18.0
struct DocumentRangesFormattingParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    lsproto::TextDocumentIdentifier TextDocument;
    Slice<lsproto::Range> Ranges;
    std::shared_ptr<lsproto::FormattingOptions> Options;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// Represents the connection of two locations. Provides additional metadata over normal locations,
// including an origin range.
struct LocationLink {
    std::shared_ptr<lsproto::Range> OriginSelectionRange;
    lsproto::DocumentUri TargetUri;
    lsproto::Range TargetRange;
    lsproto::Range TargetSelectionRange;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    struct Location GetLocation() const;
};

struct SelectionRangeOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Call hierarchy options used during static registration.
//
// Since: 3.16.0
struct CallHierarchyOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.16.0
struct SemanticTokensOptions {
    std::optional<bool> WorkDoneProgress;
    std::shared_ptr<lsproto::SemanticTokensLegend> Legend;
    std::shared_ptr<lsproto::BooleanOrEmptyObject> Range;
    std::shared_ptr<lsproto::BooleanOrSemanticTokensFullDelta> Full;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.16.0
struct SemanticTokensEdit {
    uint32_t Start;
    uint32_t DeleteCount;
    std::shared_ptr<Slice<uint32_t>> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct LinkedEditingRangeOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents information on a file/folder create.
//
// Since: 3.16.0
struct FileCreate {
    lsproto::DocumentUri Uri;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Additional information that describes document changes.
//
// Since: 3.16.0
struct ChangeAnnotation {
    std::string Label;
    std::optional<bool> NeedsConfirmation;
    std::optional<std::string> Description;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A filter to describe in which file operation requests or notifications
// the server is interested in receiving.
//
// Since: 3.16.0
struct FileOperationFilter {
    std::optional<std::string> Scheme;
    std::shared_ptr<lsproto::FileOperationPattern> Pattern;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents information on a file/folder rename.
//
// Since: 3.16.0
struct FileRename {
    lsproto::DocumentUri OldUri;
    lsproto::DocumentUri NewUri;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents information on a file/folder delete.
//
// Since: 3.16.0
struct FileDelete {
    lsproto::DocumentUri Uri;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct MonikerOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Type hierarchy options used during static registration.
//
// Since: 3.17.0
struct TypeHierarchyOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.17.0
struct InlineValueContext {
    int32_t FrameId;
    lsproto::Range StoppedLocation;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Returns inline value information as the complete text to be shown.
//
// Since: 3.17.0
struct InlineValueText {
    lsproto::Range Range;
    std::string Text;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// To compute inline value through a variable lookup.
//
// If only a range is specified, the variable name should
// be extracted from the underlying document.
//
// An optional variable name could be used to lookup instead
// of the extracted name.
//
// Since: 3.17.0
struct InlineValueVariableLookup {
    lsproto::Range Range;
    std::optional<std::string> VariableName;
    bool CaseSensitiveLookup;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// To compute an inline value through an expression evaluation.
//
// If only a range is specified, the expression should be
// extracted from the underlying document.
//
// An optional expression could be evaluated instead of
// the extracted expression.
//
// Since: 3.17.0
struct InlineValueEvaluatableExpression {
    lsproto::Range Range;
    std::optional<std::string> Expression;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Inline value options used during static registration.
//
// Since: 3.17.0
struct InlineValueOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// An inlay hint label part allows for interactive and composite labels
// of inlay hints.
//
// Since: 3.17.0
struct InlayHintLabelPart {
    std::string Value;
    std::shared_ptr<lsproto::StringOrMarkupContent> Tooltip;
    std::shared_ptr<lsproto::Location> Location;
    std::shared_ptr<lsproto::Command> Command;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Inlay hint options used during static registration.
//
// Since: 3.17.0
struct InlayHintOptions {
    std::optional<bool> WorkDoneProgress;
    std::optional<bool> ResolveProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Diagnostic options.
//
// Since: 3.17.0
struct DiagnosticOptions {
    std::optional<bool> WorkDoneProgress;
    std::optional<std::string> Identifier;
    bool InterFileDependencies;
    bool WorkspaceDiagnostics;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A previous result id in a workspace pull request.
//
// Since: 3.17.0
struct PreviousResultId {
    lsproto::DocumentUri Uri;
    std::string Value;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Parameters of the workspace diagnostic request.
//
// Since: 3.17.0
struct WorkspaceDiagnosticParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    std::shared_ptr<lsproto::IntegerOrString> PartialResultToken;
    std::optional<std::string> Identifier;
    Slice<lsproto::PreviousResultId> PreviousResultIds;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Inline completion options used during static registration.
//
// Since: 3.18.0
struct InlineCompletionOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Text document content provider options.
//
// Since: 3.18.0
struct TextDocumentContentOptions {
    Slice<std::string> Schemes;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// General parameters to register for a notification or to register a provider.
struct Registration {
    std::string Id;
    std::shared_ptr<lsproto::RegisterOptions> RegisterOptions;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// RegisterOptions is an externally-tagged union representing the options for a capability registration.
// Exactly one field should be set. The set field determines the method for the registration.
struct RegisterOptions {
    std::shared_ptr<lsproto::ImplementationRegistrationOptions> TextDocumentImplementation;
    std::shared_ptr<lsproto::TypeDefinitionRegistrationOptions> TextDocumentTypeDefinition;
    std::shared_ptr<lsproto::DocumentColorRegistrationOptions> TextDocumentDocumentColor;
    std::shared_ptr<lsproto::DocumentColorRegistrationOptions> TextDocumentColorPresentation;
    std::shared_ptr<lsproto::FoldingRangeRegistrationOptions> TextDocumentFoldingRange;
    std::shared_ptr<lsproto::DeclarationRegistrationOptions> TextDocumentDeclaration;
    std::shared_ptr<lsproto::SelectionRangeRegistrationOptions> TextDocumentSelectionRange;
    std::shared_ptr<lsproto::CallHierarchyRegistrationOptions> TextDocumentPrepareCallHierarchy;
    std::shared_ptr<lsproto::SemanticTokensRegistrationOptions> TextDocumentSemanticTokens;
    std::shared_ptr<lsproto::LinkedEditingRangeRegistrationOptions> TextDocumentLinkedEditingRange;
    std::shared_ptr<lsproto::FileOperationRegistrationOptions> WorkspaceWillCreateFiles;
    std::shared_ptr<lsproto::FileOperationRegistrationOptions> WorkspaceWillRenameFiles;
    std::shared_ptr<lsproto::FileOperationRegistrationOptions> WorkspaceWillDeleteFiles;
    std::shared_ptr<lsproto::MonikerRegistrationOptions> TextDocumentMoniker;
    std::shared_ptr<lsproto::TypeHierarchyRegistrationOptions> TextDocumentPrepareTypeHierarchy;
    std::shared_ptr<lsproto::InlineValueRegistrationOptions> TextDocumentInlineValue;
    std::shared_ptr<lsproto::InlayHintRegistrationOptions> TextDocumentInlayHint;
    std::shared_ptr<lsproto::DiagnosticRegistrationOptions> TextDocumentDiagnostic;
    std::shared_ptr<lsproto::InlineCompletionRegistrationOptions> TextDocumentInlineCompletion;
    std::shared_ptr<lsproto::TextDocumentContentRegistrationOptions> WorkspaceTextDocumentContent;
    std::shared_ptr<lsproto::TextDocumentRegistrationOptions> TextDocumentWillSaveWaitUntil;
    std::shared_ptr<lsproto::CompletionRegistrationOptions> TextDocumentCompletion;
    std::shared_ptr<lsproto::HoverRegistrationOptions> TextDocumentHover;
    std::shared_ptr<lsproto::SignatureHelpRegistrationOptions> TextDocumentSignatureHelp;
    std::shared_ptr<lsproto::DefinitionRegistrationOptions> TextDocumentDefinition;
    std::shared_ptr<lsproto::ReferenceRegistrationOptions> TextDocumentReferences;
    std::shared_ptr<lsproto::DocumentHighlightRegistrationOptions> TextDocumentDocumentHighlight;
    std::shared_ptr<lsproto::DocumentSymbolRegistrationOptions> TextDocumentDocumentSymbol;
    std::shared_ptr<lsproto::CodeActionRegistrationOptions> TextDocumentCodeAction;
    std::shared_ptr<lsproto::WorkspaceSymbolRegistrationOptions> WorkspaceSymbol;
    std::shared_ptr<lsproto::CodeLensRegistrationOptions> TextDocumentCodeLens;
    std::shared_ptr<lsproto::DocumentLinkRegistrationOptions> TextDocumentDocumentLink;
    std::shared_ptr<lsproto::DocumentFormattingRegistrationOptions> TextDocumentFormatting;
    std::shared_ptr<lsproto::DocumentRangeFormattingRegistrationOptions> TextDocumentRangeFormatting;
    std::shared_ptr<lsproto::DocumentRangeFormattingRegistrationOptions> TextDocumentRangesFormatting;
    std::shared_ptr<lsproto::DocumentOnTypeFormattingRegistrationOptions> TextDocumentOnTypeFormatting;
    std::shared_ptr<lsproto::RenameRegistrationOptions> TextDocumentRename;
    std::shared_ptr<lsproto::ExecuteCommandRegistrationOptions> WorkspaceExecuteCommand;
    std::shared_ptr<lsproto::FileOperationRegistrationOptions> WorkspaceDidCreateFiles;
    std::shared_ptr<lsproto::FileOperationRegistrationOptions> WorkspaceDidRenameFiles;
    std::shared_ptr<lsproto::FileOperationRegistrationOptions> WorkspaceDidDeleteFiles;
    std::shared_ptr<lsproto::DidChangeConfigurationRegistrationOptions> WorkspaceDidChangeConfiguration;
    std::shared_ptr<lsproto::TextDocumentRegistrationOptions> TextDocumentDidOpen;
    std::shared_ptr<lsproto::TextDocumentChangeRegistrationOptions> TextDocumentDidChange;
    std::shared_ptr<lsproto::TextDocumentRegistrationOptions> TextDocumentDidClose;
    std::shared_ptr<lsproto::TextDocumentSaveRegistrationOptions> TextDocumentDidSave;
    std::shared_ptr<lsproto::TextDocumentRegistrationOptions> TextDocumentWillSave;
    std::shared_ptr<lsproto::DidChangeWatchedFilesRegistrationOptions> WorkspaceDidChangeWatchedFiles;
    bool isZero() const;
};

// General parameters to unregister a request or notification.
struct Unregistration {
    std::string Id;
    std::string Method;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct WorkspaceFoldersInitializeParams {
    std::shared_ptr<lsproto::WorkspaceFoldersOrNull> WorkspaceFolders;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Defines the capabilities provided by a language
// server.
struct ServerCapabilities {
    std::shared_ptr<lsproto::PositionEncodingKind> PositionEncoding;
    std::shared_ptr<lsproto::TextDocumentSyncOptionsOrKind> TextDocumentSync;
    std::shared_ptr<lsproto::CompletionOptions> CompletionProvider;
    std::shared_ptr<lsproto::BooleanOrHoverOptions> HoverProvider;
    std::shared_ptr<lsproto::SignatureHelpOptions> SignatureHelpProvider;
    std::shared_ptr<lsproto::BooleanOrDeclarationOptionsOrDeclarationRegistrationOptions> DeclarationProvider;
    std::shared_ptr<lsproto::BooleanOrDefinitionOptions> DefinitionProvider;
    std::shared_ptr<lsproto::BooleanOrTypeDefinitionOptionsOrTypeDefinitionRegistrationOptions> TypeDefinitionProvider;
    std::shared_ptr<lsproto::BooleanOrImplementationOptionsOrImplementationRegistrationOptions> ImplementationProvider;
    std::shared_ptr<lsproto::BooleanOrReferenceOptions> ReferencesProvider;
    std::shared_ptr<lsproto::BooleanOrDocumentHighlightOptions> DocumentHighlightProvider;
    std::shared_ptr<lsproto::BooleanOrDocumentSymbolOptions> DocumentSymbolProvider;
    std::shared_ptr<lsproto::BooleanOrCodeActionOptions> CodeActionProvider;
    std::shared_ptr<lsproto::CodeLensOptions> CodeLensProvider;
    std::shared_ptr<lsproto::DocumentLinkOptions> DocumentLinkProvider;
    std::shared_ptr<lsproto::BooleanOrDocumentColorOptionsOrDocumentColorRegistrationOptions> ColorProvider;
    std::shared_ptr<lsproto::BooleanOrWorkspaceSymbolOptions> WorkspaceSymbolProvider;
    std::shared_ptr<lsproto::BooleanOrDocumentFormattingOptions> DocumentFormattingProvider;
    std::shared_ptr<lsproto::BooleanOrDocumentRangeFormattingOptions> DocumentRangeFormattingProvider;
    std::shared_ptr<lsproto::DocumentOnTypeFormattingOptions> DocumentOnTypeFormattingProvider;
    std::shared_ptr<lsproto::BooleanOrRenameOptions> RenameProvider;
    std::shared_ptr<lsproto::BooleanOrFoldingRangeOptionsOrFoldingRangeRegistrationOptions> FoldingRangeProvider;
    std::shared_ptr<lsproto::BooleanOrSelectionRangeOptionsOrSelectionRangeRegistrationOptions> SelectionRangeProvider;
    std::shared_ptr<lsproto::ExecuteCommandOptions> ExecuteCommandProvider;
    std::shared_ptr<lsproto::BooleanOrCallHierarchyOptionsOrCallHierarchyRegistrationOptions> CallHierarchyProvider;
    std::shared_ptr<lsproto::BooleanOrLinkedEditingRangeOptionsOrLinkedEditingRangeRegistrationOptions> LinkedEditingRangeProvider;
    std::shared_ptr<lsproto::SemanticTokensOptionsOrRegistrationOptions> SemanticTokensProvider;
    std::shared_ptr<lsproto::BooleanOrMonikerOptionsOrMonikerRegistrationOptions> MonikerProvider;
    std::shared_ptr<lsproto::BooleanOrTypeHierarchyOptionsOrTypeHierarchyRegistrationOptions> TypeHierarchyProvider;
    std::shared_ptr<lsproto::BooleanOrInlineValueOptionsOrInlineValueRegistrationOptions> InlineValueProvider;
    std::shared_ptr<lsproto::BooleanOrInlayHintOptionsOrInlayHintRegistrationOptions> InlayHintProvider;
    std::shared_ptr<lsproto::DiagnosticOptionsOrRegistrationOptions> DiagnosticProvider;
    std::shared_ptr<lsproto::BooleanOrInlineCompletionOptions> InlineCompletionProvider;
    std::shared_ptr<lsproto::WorkspaceOptions> Workspace;
    std::shared_ptr<lsproto::ExperimentalServerCapabilities> Experimental;
    std::shared_ptr<lsproto::VSOnAutoInsertOptions> VSOnAutoInsertProvider;
    std::optional<bool> VSReferencesProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Information about the server
//
// Since: 3.15.0
//
// Since: 3.18.0 ServerInfo type name added.
struct ServerInfo {
    std::string Name;
    std::optional<std::string> Version;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A text document identifier to denote a specific version of a text document.
struct VersionedTextDocumentIdentifier {
    lsproto::DocumentUri Uri;
    int32_t Version;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Save options.
struct SaveOptions {
    std::optional<bool> IncludeText;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Additional details for a completion item label.
//
// Since: 3.17.0
struct CompletionItemLabelDetails {
    std::optional<std::string> Detail;
    std::optional<std::string> Description;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A special text edit to provide an insert and a replace operation.
//
// Since: 3.16.0
struct InsertReplaceEdit {
    std::string NewText;
    lsproto::Range Insert;
    lsproto::Range Replace;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// In many cases the items of an actual completion result share the same
// value for properties like `commitCharacters` or the range of a text
// edit. A completion list can therefore define item defaults which will
// be used if a completion item itself doesn't specify the value.
//
// If a completion list specifies a default value and a completion item
// also specifies a corresponding value, the rules for combining these are
// defined by `applyKinds` (if the client supports it), defaulting to
// ApplyKind.Replace.
//
// Servers are only allowed to return default values if the client
// signals support for this via the `completionList.itemDefaults`
// capability.
//
// Since: 3.17.0
struct CompletionItemDefaults {
    std::shared_ptr<Slice<std::string>> CommitCharacters;
    std::shared_ptr<lsproto::RangeOrEditRangeWithInsertReplace> EditRange;
    std::shared_ptr<lsproto::InsertTextFormat> InsertTextFormat;
    std::shared_ptr<lsproto::InsertTextMode> InsertTextMode;
    std::shared_ptr<lsproto::CompletionItemDefaultsData> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Specifies how fields from a completion item should be combined with those
// from `completionList.itemDefaults`.
//
// If unspecified, all fields will be treated as ApplyKind.Replace.
//
// If a field's value is ApplyKind.Replace, the value from a completion item (if
// provided and not `null`) will always be used instead of the value from
// `completionItem.itemDefaults`.
//
// If a field's value is ApplyKind.Merge, the values will be merged using the rules
// defined against each field below.
//
// Servers are only allowed to return `applyKind` if the client
// signals support for this via the `completionList.applyKindSupport`
// capability.
//
// Since: 3.18.0
struct CompletionItemApplyKinds {
    std::shared_ptr<lsproto::ApplyKind> CommitCharacters;
    std::shared_ptr<lsproto::ApplyKind> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Completion options.
struct CompletionOptions {
    std::optional<bool> WorkDoneProgress;
    std::shared_ptr<Slice<std::string>> TriggerCharacters;
    std::shared_ptr<Slice<std::string>> AllCommitCharacters;
    std::optional<bool> ResolveProvider;
    std::shared_ptr<lsproto::ServerCompletionItemOptions> CompletionItem;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Hover options.
struct HoverOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents the signature of something callable. A signature
// can have a label, like a function-name, a doc-comment, and
// a set of parameters.
struct SignatureInformation {
    std::string Label;
    std::shared_ptr<lsproto::StringOrMarkupContent> Documentation;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::ParameterInformation>>> Parameters;
    std::shared_ptr<lsproto::UintegerOrNull> ActiveParameter;
    std::shared_ptr<lsproto::VSClassifiedTextElement> VSColorizedLabel;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Server Capabilities for a SignatureHelpRequest.
struct SignatureHelpOptions {
    std::optional<bool> WorkDoneProgress;
    std::shared_ptr<Slice<std::string>> TriggerCharacters;
    std::shared_ptr<Slice<std::string>> RetriggerCharacters;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Server Capabilities for a DefinitionRequest.
struct DefinitionOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Value-object that contains additional information when
// requesting references.
struct ReferenceContext {
    bool IncludeDeclaration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Reference options.
struct ReferenceOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Provider options for a DocumentHighlightRequest.
struct DocumentHighlightOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Provider options for a DocumentSymbolRequest.
struct DocumentSymbolOptions {
    std::optional<bool> WorkDoneProgress;
    std::optional<std::string> Label;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Captures why the code action is currently disabled.
//
// Since: 3.18.0
struct CodeActionDisabled {
    std::string Reason;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Location with only uri and does not include range.
//
// Since: 3.18.0
struct LocationUriOnly {
    lsproto::DocumentUri Uri;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Server capabilities for a WorkspaceSymbolRequest.
struct WorkspaceSymbolOptions {
    std::optional<bool> WorkDoneProgress;
    std::optional<bool> ResolveProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Code Lens provider options of a CodeLensRequest.
struct CodeLensOptions {
    std::optional<bool> WorkDoneProgress;
    std::optional<bool> ResolveProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Provider options for a DocumentLinkRequest.
struct DocumentLinkOptions {
    std::optional<bool> WorkDoneProgress;
    std::optional<bool> ResolveProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Value-object describing what options formatting should use.
struct FormattingOptions {
    uint32_t TabSize;
    bool InsertSpaces;
    std::optional<bool> TrimTrailingWhitespace;
    std::optional<bool> InsertFinalNewline;
    std::optional<bool> TrimFinalNewlines;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Provider options for a DocumentFormattingRequest.
struct DocumentFormattingOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Provider options for a DocumentRangeFormattingRequest.
struct DocumentRangeFormattingOptions {
    std::optional<bool> WorkDoneProgress;
    std::optional<bool> RangesSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Provider options for a DocumentOnTypeFormattingRequest.
struct DocumentOnTypeFormattingOptions {
    std::string FirstTriggerCharacter;
    std::shared_ptr<Slice<std::string>> MoreTriggerCharacter;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Provider options for a RenameRequest.
struct RenameOptions {
    std::optional<bool> WorkDoneProgress;
    std::optional<bool> PrepareProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct PrepareRenamePlaceholder {
    lsproto::Range Range;
    std::string Placeholder;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct PrepareRenameDefaultBehavior {
    bool DefaultBehavior;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The server capabilities of a ExecuteCommandRequest.
struct ExecuteCommandOptions {
    std::optional<bool> WorkDoneProgress;
    Slice<std::string> Commands;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Additional data about a workspace edit.
//
// Since: 3.18.0
struct WorkspaceEditMetadata {
    std::optional<bool> IsRefactoring;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct WorkDoneProgressOptions {
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.16.0
struct SemanticTokensLegend {
    Slice<std::string> TokenTypes;
    Slice<std::string> TokenModifiers;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Semantic tokens options to support deltas for full documents
//
// Since: 3.18.0
struct SemanticTokensFullDelta {
    std::optional<bool> Delta;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A special text edit with an additional change annotation.
//
// Since: 3.16.0.
struct AnnotatedTextEdit {
    lsproto::Range Range;
    std::string NewText;
    std::string AnnotationId;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// An interactive text edit.
//
// Since: 3.18.0
struct SnippetTextEdit {
    lsproto::Range Range;
    std::shared_ptr<lsproto::StringValue> Snippet;
    std::optional<std::string> AnnotationId;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A generic resource operation.
struct ResourceOperation {
    std::string Kind;
    std::optional<std::string> AnnotationId;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Options to create a file.
struct CreateFileOptions {
    std::optional<bool> Overwrite;
    std::optional<bool> IgnoreIfExists;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Rename file options
struct RenameFileOptions {
    std::optional<bool> Overwrite;
    std::optional<bool> IgnoreIfExists;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Delete file options
struct DeleteFileOptions {
    std::optional<bool> Recursive;
    std::optional<bool> IgnoreIfNotExists;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A pattern to describe in which file operation requests or notifications
// the server is interested in receiving.
//
// Since: 3.16.0
struct FileOperationPattern {
    std::string Glob;
    std::shared_ptr<lsproto::FileOperationPatternKind> Matches;
    std::shared_ptr<lsproto::FileOperationPatternOptions> Options;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Describes the currently selected completion item.
//
// Since: 3.18.0
struct SelectedCompletionInfo {
    lsproto::Range Range;
    std::string Text;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Information about the client
//
// Since: 3.15.0
//
// Since: 3.18.0 ClientInfo type name added.
struct ClientInfo {
    std::string Name;
    std::optional<std::string> Version;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Defines the capabilities provided by the client.
struct ClientCapabilities {
    std::shared_ptr<lsproto::WorkspaceClientCapabilities> Workspace;
    std::shared_ptr<lsproto::TextDocumentClientCapabilities> TextDocument;
    std::shared_ptr<lsproto::WindowClientCapabilities> Window;
    std::shared_ptr<lsproto::GeneralClientCapabilities> General;
    std::shared_ptr<lsproto::ExperimentalClientCapabilities> Experimental;
    std::optional<bool> VSSupportsVisualStudioExtensions;
    std::optional<int32_t> VSSupportedSnippetVersion;
    std::optional<bool> VSSupportsNotIncludingTextInTextDocumentDidOpen;
    std::optional<bool> VSSupportsIconExtensions;
    std::optional<bool> VSSupportsDiagnosticRequests;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientCapabilities Resolve() const;
};

struct TextDocumentSyncOptions {
    std::optional<bool> OpenClose;
    std::shared_ptr<lsproto::TextDocumentSyncKind> Change;
    std::optional<bool> WillSave;
    std::optional<bool> WillSaveWaitUntil;
    std::shared_ptr<lsproto::BooleanOrSaveOptions> Save;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Defines workspace specific capabilities of the server.
//
// Since: 3.18.0
struct WorkspaceOptions {
    std::shared_ptr<lsproto::WorkspaceFoldersServerCapabilities> WorkspaceFolders;
    std::shared_ptr<lsproto::FileOperationOptions> FileOperations;
    std::shared_ptr<lsproto::TextDocumentContentOptionsOrRegistrationOptions> TextDocumentContent;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct TextDocumentContentChangePartial {
    lsproto::Range Range;
    std::optional<uint32_t> RangeLength;
    std::string Text;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct TextDocumentContentChangeWholeDocument {
    std::string Text;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Structure to capture a description for an error code.
//
// Since: 3.16.0
struct CodeDescription {
    lsproto::URI Href;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents a related message and source code location for a diagnostic. This should be
// used to point to code locations that cause or related to a diagnostics, e.g when duplicating
// a symbol in a scope.
struct DiagnosticRelatedInformation {
    lsproto::Location Location;
    std::string Message;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Edit range variant that includes ranges for insert and replace operations.
//
// Since: 3.18.0
struct EditRangeWithInsertReplace {
    lsproto::Range Insert;
    lsproto::Range Replace;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct ServerCompletionItemOptions {
    std::optional<bool> LabelDetailsSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
//
// Deprecated: use MarkupContent instead.
struct MarkedStringWithLanguage {
    std::string Language;
    std::string Value;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Matching options for the file operation pattern.
//
// Since: 3.16.0
struct FileOperationPatternOptions {
    std::optional<bool> IgnoreCase;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Workspace specific client capabilities.
struct WorkspaceClientCapabilities {
    std::optional<bool> ApplyEdit;
    std::shared_ptr<lsproto::WorkspaceEditClientCapabilities> WorkspaceEdit;
    std::shared_ptr<lsproto::DidChangeConfigurationClientCapabilities> DidChangeConfiguration;
    std::shared_ptr<lsproto::DidChangeWatchedFilesClientCapabilities> DidChangeWatchedFiles;
    std::shared_ptr<lsproto::WorkspaceSymbolClientCapabilities> Symbol;
    std::shared_ptr<lsproto::ExecuteCommandClientCapabilities> ExecuteCommand;
    std::optional<bool> WorkspaceFolders;
    std::optional<bool> Configuration;
    std::shared_ptr<lsproto::SemanticTokensWorkspaceClientCapabilities> SemanticTokens;
    std::shared_ptr<lsproto::CodeLensWorkspaceClientCapabilities> CodeLens;
    std::shared_ptr<lsproto::FileOperationClientCapabilities> FileOperations;
    std::shared_ptr<lsproto::InlineValueWorkspaceClientCapabilities> InlineValue;
    std::shared_ptr<lsproto::InlayHintWorkspaceClientCapabilities> InlayHint;
    std::shared_ptr<lsproto::DiagnosticWorkspaceClientCapabilities> Diagnostics;
    std::shared_ptr<lsproto::FoldingRangeWorkspaceClientCapabilities> FoldingRange;
    std::shared_ptr<lsproto::TextDocumentContentClientCapabilities> TextDocumentContent;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedWorkspaceClientCapabilities resolve() const;
};

// Text document specific client capabilities.
struct TextDocumentClientCapabilities {
    std::shared_ptr<lsproto::TextDocumentSyncClientCapabilities> Synchronization;
    std::shared_ptr<lsproto::TextDocumentFilterClientCapabilities> Filters;
    std::shared_ptr<lsproto::CompletionClientCapabilities> Completion;
    std::shared_ptr<lsproto::HoverClientCapabilities> Hover;
    std::shared_ptr<lsproto::SignatureHelpClientCapabilities> SignatureHelp;
    std::shared_ptr<lsproto::DeclarationClientCapabilities> Declaration;
    std::shared_ptr<lsproto::DefinitionClientCapabilities> Definition;
    std::shared_ptr<lsproto::TypeDefinitionClientCapabilities> TypeDefinition;
    std::shared_ptr<lsproto::ImplementationClientCapabilities> Implementation;
    std::shared_ptr<lsproto::ReferenceClientCapabilities> References;
    std::shared_ptr<lsproto::DocumentHighlightClientCapabilities> DocumentHighlight;
    std::shared_ptr<lsproto::DocumentSymbolClientCapabilities> DocumentSymbol;
    std::shared_ptr<lsproto::CodeActionClientCapabilities> CodeAction;
    std::shared_ptr<lsproto::CodeLensClientCapabilities> CodeLens;
    std::shared_ptr<lsproto::DocumentLinkClientCapabilities> DocumentLink;
    std::shared_ptr<lsproto::DocumentColorClientCapabilities> ColorProvider;
    std::shared_ptr<lsproto::DocumentFormattingClientCapabilities> Formatting;
    std::shared_ptr<lsproto::DocumentRangeFormattingClientCapabilities> RangeFormatting;
    std::shared_ptr<lsproto::DocumentOnTypeFormattingClientCapabilities> OnTypeFormatting;
    std::shared_ptr<lsproto::RenameClientCapabilities> Rename;
    std::shared_ptr<lsproto::FoldingRangeClientCapabilities> FoldingRange;
    std::shared_ptr<lsproto::SelectionRangeClientCapabilities> SelectionRange;
    std::shared_ptr<lsproto::PublishDiagnosticsClientCapabilities> PublishDiagnostics;
    std::shared_ptr<lsproto::CallHierarchyClientCapabilities> CallHierarchy;
    std::shared_ptr<lsproto::SemanticTokensClientCapabilities> SemanticTokens;
    std::shared_ptr<lsproto::LinkedEditingRangeClientCapabilities> LinkedEditingRange;
    std::shared_ptr<lsproto::MonikerClientCapabilities> Moniker;
    std::shared_ptr<lsproto::TypeHierarchyClientCapabilities> TypeHierarchy;
    std::shared_ptr<lsproto::InlineValueClientCapabilities> InlineValue;
    std::shared_ptr<lsproto::InlayHintClientCapabilities> InlayHint;
    std::shared_ptr<lsproto::DiagnosticClientCapabilities> Diagnostic;
    std::shared_ptr<lsproto::InlineCompletionClientCapabilities> InlineCompletion;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedTextDocumentClientCapabilities resolve() const;
};

struct WindowClientCapabilities {
    std::optional<bool> WorkDoneProgress;
    std::shared_ptr<lsproto::ShowMessageRequestClientCapabilities> ShowMessage;
    std::shared_ptr<lsproto::ShowDocumentClientCapabilities> ShowDocument;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedWindowClientCapabilities resolve() const;
};

struct WorkspaceFoldersServerCapabilities {
    std::optional<bool> Supported;
    std::shared_ptr<lsproto::StringOrBoolean> ChangeNotifications;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Options for notifications/requests for user operations on files.
//
// Since: 3.16.0
struct FileOperationOptions {
    std::shared_ptr<lsproto::FileOperationRegistrationOptions> DidCreate;
    std::shared_ptr<lsproto::FileOperationRegistrationOptions> WillCreate;
    std::shared_ptr<lsproto::FileOperationRegistrationOptions> DidRename;
    std::shared_ptr<lsproto::FileOperationRegistrationOptions> WillRename;
    std::shared_ptr<lsproto::FileOperationRegistrationOptions> DidDelete;
    std::shared_ptr<lsproto::FileOperationRegistrationOptions> WillDelete;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct DidChangeConfigurationClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDidChangeConfigurationClientCapabilities resolve() const;
};

struct DidChangeWatchedFilesClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::optional<bool> RelativePatternSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDidChangeWatchedFilesClientCapabilities resolve() const;
};

// Client capabilities for a WorkspaceSymbolRequest.
struct WorkspaceSymbolClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::shared_ptr<lsproto::ClientSymbolKindOptions> SymbolKind;
    std::shared_ptr<lsproto::ClientSymbolTagOptions> TagSupport;
    std::shared_ptr<lsproto::ClientSymbolResolveOptions> ResolveSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedWorkspaceSymbolClientCapabilities resolve() const;
};

// The client capabilities of a ExecuteCommandRequest.
struct ExecuteCommandClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedExecuteCommandClientCapabilities resolve() const;
};

// Since: 3.16.0
struct SemanticTokensWorkspaceClientCapabilities {
    std::optional<bool> RefreshSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedSemanticTokensWorkspaceClientCapabilities resolve() const;
};

// Since: 3.16.0
struct CodeLensWorkspaceClientCapabilities {
    std::optional<bool> RefreshSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedCodeLensWorkspaceClientCapabilities resolve() const;
};

// Capabilities relating to events from file operations by the user in the client.
//
// These events do not come from the file system, they come from user operations
// like renaming a file in the UI.
//
// Since: 3.16.0
struct FileOperationClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::optional<bool> DidCreate;
    std::optional<bool> WillCreate;
    std::optional<bool> DidRename;
    std::optional<bool> WillRename;
    std::optional<bool> DidDelete;
    std::optional<bool> WillDelete;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedFileOperationClientCapabilities resolve() const;
};

// Client workspace capabilities specific to inline values.
//
// Since: 3.17.0
struct InlineValueWorkspaceClientCapabilities {
    std::optional<bool> RefreshSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedInlineValueWorkspaceClientCapabilities resolve() const;
};

// Client workspace capabilities specific to inlay hints.
//
// Since: 3.17.0
struct InlayHintWorkspaceClientCapabilities {
    std::optional<bool> RefreshSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedInlayHintWorkspaceClientCapabilities resolve() const;
};

// Workspace client capabilities specific to diagnostic pull requests.
//
// Since: 3.17.0
struct DiagnosticWorkspaceClientCapabilities {
    std::optional<bool> RefreshSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDiagnosticWorkspaceClientCapabilities resolve() const;
};

// Client workspace capabilities specific to folding ranges
//
// Since: 3.18.0
struct FoldingRangeWorkspaceClientCapabilities {
    std::optional<bool> RefreshSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedFoldingRangeWorkspaceClientCapabilities resolve() const;
};

// Client capabilities for a text document content provider.
//
// Since: 3.18.0
struct TextDocumentContentClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedTextDocumentContentClientCapabilities resolve() const;
};

struct TextDocumentSyncClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::optional<bool> WillSave;
    std::optional<bool> WillSaveWaitUntil;
    std::optional<bool> DidSave;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedTextDocumentSyncClientCapabilities resolve() const;
};

struct TextDocumentFilterClientCapabilities {
    std::optional<bool> RelativePatternSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedTextDocumentFilterClientCapabilities resolve() const;
};

// Completion client capabilities
struct CompletionClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::shared_ptr<lsproto::ClientCompletionItemOptions> CompletionItem;
    std::shared_ptr<lsproto::ClientCompletionItemOptionsKind> CompletionItemKind;
    std::shared_ptr<lsproto::InsertTextMode> InsertTextMode;
    std::optional<bool> ContextSupport;
    std::shared_ptr<lsproto::CompletionListCapabilities> CompletionList;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedCompletionClientCapabilities resolve() const;
};

// Client Capabilities for a SignatureHelpRequest.
struct SignatureHelpClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::shared_ptr<lsproto::ClientSignatureInformationOptions> SignatureInformation;
    std::optional<bool> ContextSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedSignatureHelpClientCapabilities resolve() const;
};

// Since: 3.14.0
struct DeclarationClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::optional<bool> LinkSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDeclarationClientCapabilities resolve() const;
};

// Client Capabilities for a DefinitionRequest.
struct DefinitionClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::optional<bool> LinkSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDefinitionClientCapabilities resolve() const;
};

// Since 3.6.0
struct TypeDefinitionClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::optional<bool> LinkSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedTypeDefinitionClientCapabilities resolve() const;
};

// Since: 3.6.0
struct ImplementationClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::optional<bool> LinkSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedImplementationClientCapabilities resolve() const;
};

// Client Capabilities for a ReferencesRequest.
struct ReferenceClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedReferenceClientCapabilities resolve() const;
};

// Client Capabilities for a DocumentHighlightRequest.
struct DocumentHighlightClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDocumentHighlightClientCapabilities resolve() const;
};

// Client Capabilities for a DocumentSymbolRequest.
struct DocumentSymbolClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::shared_ptr<lsproto::ClientSymbolKindOptions> SymbolKind;
    std::optional<bool> HierarchicalDocumentSymbolSupport;
    std::shared_ptr<lsproto::ClientSymbolTagOptions> TagSupport;
    std::optional<bool> LabelSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDocumentSymbolClientCapabilities resolve() const;
};

// The Client Capabilities of a CodeActionRequest.
struct CodeActionClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::shared_ptr<lsproto::ClientCodeActionLiteralOptions> CodeActionLiteralSupport;
    std::optional<bool> IsPreferredSupport;
    std::optional<bool> DisabledSupport;
    std::optional<bool> DataSupport;
    std::shared_ptr<lsproto::ClientCodeActionResolveOptions> ResolveSupport;
    std::optional<bool> HonorsChangeAnnotations;
    std::optional<bool> DocumentationSupport;
    std::shared_ptr<lsproto::CodeActionTagOptions> TagSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedCodeActionClientCapabilities resolve() const;
};

// The client capabilities of a CodeLensRequest.
struct CodeLensClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::shared_ptr<lsproto::ClientCodeLensResolveOptions> ResolveSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedCodeLensClientCapabilities resolve() const;
};

// The client capabilities of a DocumentLinkRequest.
struct DocumentLinkClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::optional<bool> TooltipSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDocumentLinkClientCapabilities resolve() const;
};

struct DocumentColorClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDocumentColorClientCapabilities resolve() const;
};

// Client capabilities of a DocumentFormattingRequest.
struct DocumentFormattingClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDocumentFormattingClientCapabilities resolve() const;
};

// Client capabilities of a DocumentRangeFormattingRequest.
struct DocumentRangeFormattingClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::optional<bool> RangesSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDocumentRangeFormattingClientCapabilities resolve() const;
};

// Client capabilities of a DocumentOnTypeFormattingRequest.
struct DocumentOnTypeFormattingClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDocumentOnTypeFormattingClientCapabilities resolve() const;
};

struct RenameClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::optional<bool> PrepareSupport;
    std::shared_ptr<lsproto::PrepareSupportDefaultBehavior> PrepareSupportDefaultBehavior;
    std::optional<bool> HonorsChangeAnnotations;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedRenameClientCapabilities resolve() const;
};

struct FoldingRangeClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::optional<uint32_t> RangeLimit;
    std::optional<bool> LineFoldingOnly;
    std::shared_ptr<lsproto::ClientFoldingRangeKindOptions> FoldingRangeKind;
    std::shared_ptr<lsproto::ClientFoldingRangeOptions> FoldingRange;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedFoldingRangeClientCapabilities resolve() const;
};

struct SelectionRangeClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedSelectionRangeClientCapabilities resolve() const;
};

// The publish diagnostic client capabilities.
struct PublishDiagnosticsClientCapabilities {
    std::optional<bool> RelatedInformation;
    std::shared_ptr<lsproto::ClientDiagnosticsTagOptions> TagSupport;
    std::optional<bool> CodeDescriptionSupport;
    std::optional<bool> DataSupport;
    std::optional<bool> VersionSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedPublishDiagnosticsClientCapabilities resolve() const;
};

// Since: 3.16.0
struct CallHierarchyClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedCallHierarchyClientCapabilities resolve() const;
};

// Client capabilities for the linked editing range request.
//
// Since: 3.16.0
struct LinkedEditingRangeClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedLinkedEditingRangeClientCapabilities resolve() const;
};

// Client capabilities specific to the moniker request.
//
// Since: 3.16.0
struct MonikerClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedMonikerClientCapabilities resolve() const;
};

// Since: 3.17.0
struct TypeHierarchyClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedTypeHierarchyClientCapabilities resolve() const;
};

// Client capabilities specific to inline values.
//
// Since: 3.17.0
struct InlineValueClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedInlineValueClientCapabilities resolve() const;
};

// Inlay hint client capabilities.
//
// Since: 3.17.0
struct InlayHintClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::shared_ptr<lsproto::ClientInlayHintResolveOptions> ResolveSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedInlayHintClientCapabilities resolve() const;
};

// Client capabilities specific to diagnostic pull requests.
//
// Since: 3.17.0
struct DiagnosticClientCapabilities {
    std::optional<bool> RelatedInformation;
    std::shared_ptr<lsproto::ClientDiagnosticsTagOptions> TagSupport;
    std::optional<bool> CodeDescriptionSupport;
    std::optional<bool> DataSupport;
    std::optional<bool> DynamicRegistration;
    std::optional<bool> RelatedDocumentSupport;
    std::optional<bool> MarkupMessageSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedDiagnosticClientCapabilities resolve() const;
};

// Client capabilities specific to inline completions.
//
// Since: 3.18.0
struct InlineCompletionClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedInlineCompletionClientCapabilities resolve() const;
};

// Show message request client capabilities
struct ShowMessageRequestClientCapabilities {
    std::shared_ptr<lsproto::ClientShowMessageActionItemOptions> MessageActionItem;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedShowMessageRequestClientCapabilities resolve() const;
};

// Client capabilities for the showDocument request.
//
// Since: 3.16.0
struct ShowDocumentClientCapabilities {
    bool Support;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedShowDocumentClientCapabilities resolve() const;
};

// Since: 3.18.0
struct StaleRequestSupportOptions {
    bool Cancel;
    Slice<std::string> RetryOnContentModified;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedStaleRequestSupportOptions resolve() const;
};

// Client capabilities specific to regular expressions.
//
// Since: 3.16.0
struct RegularExpressionsClientCapabilities {
    std::string Engine;
    std::optional<std::string> Version;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedRegularExpressionsClientCapabilities resolve() const;
};

// Client capabilities specific to the used markdown parser.
//
// Since: 3.16.0
struct MarkdownClientCapabilities {
    std::string Parser;
    std::optional<std::string> Version;
    std::shared_ptr<Slice<std::string>> AllowedTags;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedMarkdownClientCapabilities resolve() const;
};

// A document filter where `language` is required field.
//
// Since: 3.18.0
struct TextDocumentFilterLanguage {
    std::string Language;
    std::optional<std::string> Scheme;
    std::shared_ptr<lsproto::PatternOrRelativePattern> Pattern;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A document filter where `scheme` is required field.
//
// Since: 3.18.0
struct TextDocumentFilterScheme {
    std::optional<std::string> Language;
    std::string Scheme;
    std::shared_ptr<lsproto::PatternOrRelativePattern> Pattern;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct ChangeAnnotationsSupportOptions {
    std::optional<bool> GroupsOnLabel;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedChangeAnnotationsSupportOptions resolve() const;
};

// Since: 3.18.0
struct ClientSymbolResolveOptions {
    Slice<std::string> Properties;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientSymbolResolveOptions resolve() const;
};

// The client supports the following `CompletionList` specific
// capabilities.
//
// Since: 3.17.0
struct CompletionListCapabilities {
    std::shared_ptr<Slice<std::string>> ItemDefaults;
    std::optional<bool> ApplyKindSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedCompletionListCapabilities resolve() const;
};

// Since: 3.18.0
struct ClientCodeActionLiteralOptions {
    std::shared_ptr<lsproto::ClientCodeActionKindOptions> CodeActionKind;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientCodeActionLiteralOptions resolve() const;
};

// Since: 3.18.0
struct ClientCodeActionResolveOptions {
    Slice<std::string> Properties;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientCodeActionResolveOptions resolve() const;
};

// Since: 3.18.0
struct ClientCodeLensResolveOptions {
    Slice<std::string> Properties;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientCodeLensResolveOptions resolve() const;
};

// Since: 3.18.0
struct ClientFoldingRangeOptions {
    std::optional<bool> CollapsedText;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientFoldingRangeOptions resolve() const;
};

// General diagnostics capabilities for pull and push model.
struct DiagnosticsCapabilities {
    std::optional<bool> RelatedInformation;
    std::shared_ptr<lsproto::ClientDiagnosticsTagOptions> TagSupport;
    std::optional<bool> CodeDescriptionSupport;
    std::optional<bool> DataSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct ClientSemanticTokensRequestOptions {
    std::shared_ptr<lsproto::BooleanOrEmptyObject> Range;
    std::shared_ptr<lsproto::BooleanOrClientSemanticTokensRequestFullDelta> Full;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientSemanticTokensRequestOptions resolve() const;
};

// Since: 3.18.0
struct ClientInlayHintResolveOptions {
    Slice<std::string> Properties;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientInlayHintResolveOptions resolve() const;
};

// Since: 3.18.0
struct ClientShowMessageActionItemOptions {
    std::optional<bool> AdditionalPropertiesSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientShowMessageActionItemOptions resolve() const;
};

// Since: 3.18.0
struct ClientCompletionItemResolveOptions {
    Slice<std::string> Properties;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientCompletionItemResolveOptions resolve() const;
};

// Since: 3.18.0
struct ClientSignatureParameterInformationOptions {
    std::optional<bool> LabelOffsetSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientSignatureParameterInformationOptions resolve() const;
};

// Since: 3.18.0
struct ClientSemanticTokensRequestFullDelta {
    std::optional<bool> Delta;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// InitializationOptions contains user-provided initialization options.
struct InitializationOptions {
    std::optional<bool> DisablePushDiagnostics;
    std::optional<std::string> CodeLensShowLocationsCommandName;
    std::shared_ptr<lsproto::LSPAny> UserPreferences;
    std::optional<bool> EnableTelemetry;
    std::shared_ptr<lsproto::LogVerbosity> LogVerbosity;
    std::optional<bool> RunExternalCode;
    std::shared_ptr<lsproto::DiagnosticFlakeLogLevel> TrackFlakyDiagnostics;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// CompletionItemData is preserved on a CompletionItem between CompletionRequest and CompletionResolveRequest.
struct CompletionItemData {
    std::string FileName;
    int32_t Position;
    std::optional<int32_t> SupplementalFileIndex;
    std::string Source;
    std::string Name;
    std::shared_ptr<lsproto::AutoImportFix> AutoImport;
    bool IsImportStatementCompletion;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ExperimentalServerCapabilities contains experimental capabilities under development.
struct ExperimentalServerCapabilities {
    std::optional<bool> CustomSourceDefinitionProvider;
    std::optional<bool> CustomMultiDocumentHighlightProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ExperimentalClientCapabilities contains experimental capabilities under development.
struct ExperimentalClientCapabilities {
    std::optional<bool> HoverVerbosityLevel;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedExperimentalClientCapabilities resolve() const;
};

// Options for the textDocument/_vs_onAutoInsert provider capability.
struct VSOnAutoInsertOptions {
    Slice<std::string> VSTriggerCharacters;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Parameters for the textDocument/_vs_onAutoInsert request.
struct VSOnAutoInsertParams {
    lsproto::TextDocumentIdentifier VSTextDocument;
    lsproto::Position VSPosition;
    std::string VSCh;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// RequestFailureTelemetryProperties contains failure information when an LSP request manages to recover.
struct RequestFailureTelemetryProperties {
    std::string ErrorCode;
    std::string RequestMethod;
    std::string Stack;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Parameters for profiling requests.
struct ProfileParams {
    std::string Dir;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Result of a profiling request.
struct ProfileResult {
    std::string File;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Parameters for the initializeAPISession request.
struct InitializeAPISessionParams {
    std::optional<std::string> Pipe;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Result for the initializeAPISession request.
struct InitializeAPISessionResult {
    std::string SessionId;
    std::string Pipe;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Parameters for the custom/projectInfo request.
struct ProjectInfoParams {
    lsproto::TextDocumentIdentifier TextDocument;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// Result for the custom/projectInfo request.
struct ProjectInfoResult {
    std::string ConfigFilePath;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Inline content mapper manifest supplied by a contributing extension.
struct ContentMapperManifest {
    std::string Name;
    std::optional<std::string> Version;
    Slice<std::string> Exec;
    std::optional<std::string> Cwd;
    std::shared_ptr<Slice<std::string>> CompilerOptions;
    std::optional<bool> DynamicConfig;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Content mapper configuration contributed to inferred projects.
struct InferredProjectContentMapperContribution {
    std::shared_ptr<Map<std::string,lsproto::LSPAny>> Options;
    std::shared_ptr<lsproto::ContentMapperManifest> Manifest;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// One extension-provided content mapper contribution.
struct ContentMapperContribution {
    std::string ContributorId;
    Slice<std::string> Extensions;
    std::shared_ptr<lsproto::InferredProjectContentMapperContribution> InferredProjectContribution;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Parameters for the custom/setContentMapperContributions request.
struct SetContentMapperContributionsParams {
    Slice<std::shared_ptr<lsproto::ContentMapperContribution>> Contributions;
    Slice<lsproto::TextDocumentIdentifier> OpenDocuments;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Numeric measurements for PerformanceStatsTelemetryEvent.
struct PerformanceStatsTelemetryMeasurements {
    double OpenFileCount;
    double UptimeSeconds;
    double ProjectCount;
    double ConfigCount;
    double CachedDiskFileCount;
    double MemoryUsedBytes;
    double GoMemLimit;
    double GoGCPercent;
    double HeapGoalBytes;
    double HeapLiveBytes;
    double HeapObjectCount;
    double HeapStackBytes;
    double HeapReleasedBytes;
    double HeapFreeBytes;
    double GcScanHeapBytes;
    double GoMaxProcs;
    double GoroutineCount;
    double GcCyclesTotal;
    double GcCPUSeconds;
    double UserCPUSeconds;
    double SystemMemTotal;
    double SystemMemUsed;
    double AutoImportProjectBucketCount;
    double AutoImportNodeModulesBucketCount;
    double AutoImportUniquePackageCount;
    double AutoImportProjectExportCount;
    double AutoImportNodeModulesExportCount;
    double AutoImportProjectFileCount;
    double AutoImportNodeModulesFileCount;
    double AutoImportNodeModulesUnfilteredBucketCount;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Numeric measurements for ProjectInfoTelemetryEvent.
struct ProjectInfoTelemetryMeasurements {
    double JsFileCount;
    double JsFileSize;
    double JsxFileCount;
    double JsxFileSize;
    double TsFileCount;
    double TsFileSize;
    double TsxFileCount;
    double TsxFileSize;
    double DtsFileCount;
    double DtsFileSize;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents a collection of document highlights from a single document, used in multi-document highlight responses.
struct MultiDocumentHighlight {
    lsproto::DocumentUri Uri;
    Slice<std::shared_ptr<lsproto::DocumentHighlight>> Highlights;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Parameters for the custom/textDocument/multiDocumentHighlight request.
struct MultiDocumentHighlightParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::Position Position;
    Slice<lsproto::DocumentUri> FilesToSearch;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
    struct Position TextDocumentPosition() const;
};

// CallHierarchyItemData is a placeholder for custom data preserved on a CallHierarchyItem.
struct CallHierarchyItemData {
    std::vector<StructFieldBinding> fieldBindings() { return {}; }
    std::string unmarshalJSONFrom(json::Decoder& dec) { return unmarshalStructReflectGo(dec, {}); }
    std::string marshalJSONTo(json::Encoder& enc) const { return enc.writeToken(json::BeginObject).empty() ? enc.writeToken(json::EndObject) : "json: write error"; }
    bool isZero() const { return true; }
};

// TypeHierarchyItemData is a placeholder for custom data preserved on a TypeHierarchyItem.
struct TypeHierarchyItemData {
    std::vector<StructFieldBinding> fieldBindings() { return {}; }
    std::string unmarshalJSONFrom(json::Decoder& dec) { return unmarshalStructReflectGo(dec, {}); }
    std::string marshalJSONTo(json::Encoder& enc) const { return enc.writeToken(json::BeginObject).empty() ? enc.writeToken(json::EndObject) : "json: write error"; }
    bool isZero() const { return true; }
};

// InlayHintData is a placeholder for custom data preserved on a InlayHint.
struct InlayHintData {
    std::vector<StructFieldBinding> fieldBindings() { return {}; }
    std::string unmarshalJSONFrom(json::Decoder& dec) { return unmarshalStructReflectGo(dec, {}); }
    std::string marshalJSONTo(json::Encoder& enc) const { return enc.writeToken(json::BeginObject).empty() ? enc.writeToken(json::EndObject) : "json: write error"; }
    bool isZero() const { return true; }
};

// CodeActionData is a placeholder for custom data preserved on a CodeAction.
struct CodeActionData {
    std::vector<StructFieldBinding> fieldBindings() { return {}; }
    std::string unmarshalJSONFrom(json::Decoder& dec) { return unmarshalStructReflectGo(dec, {}); }
    std::string marshalJSONTo(json::Encoder& enc) const { return enc.writeToken(json::BeginObject).empty() ? enc.writeToken(json::EndObject) : "json: write error"; }
    bool isZero() const { return true; }
};

// WorkspaceSymbolData is a placeholder for custom data preserved on a WorkspaceSymbol.
struct WorkspaceSymbolData {
    std::vector<StructFieldBinding> fieldBindings() { return {}; }
    std::string unmarshalJSONFrom(json::Decoder& dec) { return unmarshalStructReflectGo(dec, {}); }
    std::string marshalJSONTo(json::Encoder& enc) const { return enc.writeToken(json::BeginObject).empty() ? enc.writeToken(json::EndObject) : "json: write error"; }
    bool isZero() const { return true; }
};

// DocumentLinkData is a placeholder for custom data preserved on a DocumentLink.
struct DocumentLinkData {
    std::vector<StructFieldBinding> fieldBindings() { return {}; }
    std::string unmarshalJSONFrom(json::Decoder& dec) { return unmarshalStructReflectGo(dec, {}); }
    std::string marshalJSONTo(json::Encoder& enc) const { return enc.writeToken(json::BeginObject).empty() ? enc.writeToken(json::EndObject) : "json: write error"; }
    bool isZero() const { return true; }
};

// DiagnosticData is a placeholder for custom data preserved on a Diagnostic.
struct DiagnosticData {
    std::vector<StructFieldBinding> fieldBindings() { return {}; }
    std::string unmarshalJSONFrom(json::Decoder& dec) { return unmarshalStructReflectGo(dec, {}); }
    std::string marshalJSONTo(json::Encoder& enc) const { return enc.writeToken(json::BeginObject).empty() ? enc.writeToken(json::EndObject) : "json: write error"; }
    bool isZero() const { return true; }
};

// CompletionItemDefaultsData is a placeholder for custom data preserved on a CompletionItemDefaults.
struct CompletionItemDefaultsData {
    std::vector<StructFieldBinding> fieldBindings() { return {}; }
    std::string unmarshalJSONFrom(json::Decoder& dec) { return unmarshalStructReflectGo(dec, {}); }
    std::string marshalJSONTo(json::Encoder& enc) const { return enc.writeToken(json::BeginObject).empty() ? enc.writeToken(json::EndObject) : "json: write error"; }
    bool isZero() const { return true; }
};

// Since: 3.18.0
struct ClientFoldingRangeKindOptions {
    std::shared_ptr<Slice<lsproto::FoldingRangeKind>> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientFoldingRangeKindOptions resolve() const;
};

// Since: 3.18.0
struct ClientSymbolKindOptions {
    std::shared_ptr<Slice<lsproto::SymbolKind>> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientSymbolKindOptions resolve() const;
};

// Represents programming constructs like functions or constructors in the context
// of call hierarchy.
//
// Since: 3.16.0
struct CallHierarchyItem {
    std::string Name;
    lsproto::SymbolKind Kind;
    std::shared_ptr<Slice<lsproto::SymbolTag>> Tags;
    std::optional<std::string> Detail;
    lsproto::DocumentUri Uri;
    lsproto::Range Range;
    lsproto::Range SelectionRange;
    std::shared_ptr<lsproto::CallHierarchyItemData> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    struct Location GetLocation() const;
};

// Since: 3.17.0
struct TypeHierarchyItem {
    std::string Name;
    lsproto::SymbolKind Kind;
    std::shared_ptr<Slice<lsproto::SymbolTag>> Tags;
    std::optional<std::string> Detail;
    lsproto::DocumentUri Uri;
    lsproto::Range Range;
    lsproto::Range SelectionRange;
    std::shared_ptr<lsproto::TypeHierarchyItemData> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    struct Location GetLocation() const;
};

// Represents information about programming constructs like variables, classes,
// interfaces etc.
struct SymbolInformation {
    std::string Name;
    lsproto::SymbolKind Kind;
    std::shared_ptr<Slice<lsproto::SymbolTag>> Tags;
    std::optional<std::string> ContainerName;
    std::optional<bool> Deprecated;
    lsproto::Location Location;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Represents programming constructs like variables, classes, interfaces etc.
// that appear in a document. Document symbols can be hierarchical and they
// have two ranges: one that encloses its definition and one that points to
// its most interesting range, e.g. the range of an identifier.
struct DocumentSymbol {
    std::string Name;
    std::optional<std::string> Detail;
    lsproto::SymbolKind Kind;
    std::shared_ptr<Slice<lsproto::SymbolTag>> Tags;
    std::optional<bool> Deprecated;
    lsproto::Range Range;
    lsproto::Range SelectionRange;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::DocumentSymbol>>> Children;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A base for all symbol information.
struct BaseSymbolInformation {
    std::string Name;
    lsproto::SymbolKind Kind;
    std::shared_ptr<Slice<lsproto::SymbolTag>> Tags;
    std::optional<std::string> ContainerName;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct ClientSymbolTagOptions {
    Slice<lsproto::SymbolTag> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientSymbolTagOptions resolve() const;
};

// Moniker definition to match LSIF 0.5 moniker definition.
//
// Since: 3.16.0
struct Moniker {
    std::string Scheme;
    std::string Identifier;
    lsproto::UniquenessLevel Unique;
    std::shared_ptr<lsproto::MonikerKind> Kind;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters of a notification message.
struct ShowMessageParams {
    lsproto::MessageType Type;
    std::string Message;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct ShowMessageRequestParams {
    lsproto::MessageType Type;
    std::string Message;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::MessageActionItem>>> Actions;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The log message parameters.
struct LogMessageParams {
    lsproto::MessageType Type;
    std::string Message;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// The parameters sent in a will save text document notification.
struct WillSaveTextDocumentParams {
    lsproto::TextDocumentIdentifier TextDocument;
    lsproto::TextDocumentSaveReason Reason;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    DocumentUri TextDocumentURI() const;
};

// Since: 3.18.0
struct ClientCompletionItemOptionsKind {
    std::shared_ptr<Slice<lsproto::CompletionItemKind>> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientCompletionItemOptionsKind resolve() const;
};

// A completion item represents a text snippet that is
// proposed to complete text that is being typed.
struct CompletionItem {
    std::string Label;
    std::shared_ptr<lsproto::CompletionItemLabelDetails> LabelDetails;
    std::shared_ptr<lsproto::CompletionItemKind> Kind;
    std::shared_ptr<Slice<lsproto::CompletionItemTag>> Tags;
    std::optional<std::string> Detail;
    std::shared_ptr<lsproto::StringOrMarkupContent> Documentation;
    std::optional<bool> Deprecated;
    std::optional<bool> Preselect;
    std::optional<std::string> SortText;
    std::optional<std::string> FilterText;
    std::optional<std::string> InsertText;
    std::shared_ptr<lsproto::InsertTextFormat> InsertTextFormat;
    std::shared_ptr<lsproto::InsertTextMode> InsertTextMode;
    std::shared_ptr<lsproto::TextEditOrInsertReplaceEdit> TextEdit;
    std::optional<std::string> TextEditText;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::TextEdit>>> AdditionalTextEdits;
    std::shared_ptr<Slice<std::string>> CommitCharacters;
    std::shared_ptr<lsproto::Command> Command;
    std::shared_ptr<lsproto::CompletionItemData> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct CompletionItemTagOptions {
    Slice<lsproto::CompletionItemTag> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedCompletionItemTagOptions resolve() const;
};

// Response item for the textDocument/_vs_onAutoInsert request.
struct VSOnAutoInsertResponseItem {
    lsproto::InsertTextFormat VSTextEditFormat;
    std::shared_ptr<lsproto::TextEdit> VSTextEdit;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct ClientCompletionItemInsertTextModeOptions {
    Slice<lsproto::InsertTextMode> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientCompletionItemInsertTextModeOptions resolve() const;
};

// Contains additional diagnostic information about the context in which
// a action is run.
struct CodeActionContext {
    Slice<std::shared_ptr<lsproto::Diagnostic>> Diagnostics;
    std::shared_ptr<Slice<lsproto::CodeActionKind>> Only;
    std::shared_ptr<lsproto::CodeActionTriggerKind> TriggerKind;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Provider options for a CodeActionRequest.
struct CodeActionOptions {
    std::optional<bool> WorkDoneProgress;
    std::shared_ptr<Slice<lsproto::CodeActionKind>> CodeActionKinds;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::CodeActionKindDocumentation>>> Documentation;
    std::optional<bool> ResolveProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Documentation for a class of code actions.
//
// Since: 3.18.0
struct CodeActionKindDocumentation {
    lsproto::CodeActionKind Kind;
    std::shared_ptr<lsproto::Command> Command;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct ClientCodeActionKindOptions {
    Slice<lsproto::CodeActionKind> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientCodeActionKindOptions resolve() const;
};

// A code action represents a change that can be performed in code, e.g. to fix a problem or
// to refactor code.
//
// A CodeAction must set either `edit` and/or a `command`. If both are supplied, the `edit` is applied first, then the `command` is executed.
struct CodeAction {
    std::string Title;
    std::shared_ptr<lsproto::CodeActionKind> Kind;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::Diagnostic>>> Diagnostics;
    std::optional<bool> IsPreferred;
    std::shared_ptr<lsproto::CodeActionDisabled> Disabled;
    std::shared_ptr<lsproto::WorkspaceEdit> Edit;
    std::shared_ptr<lsproto::Command> Command;
    std::shared_ptr<lsproto::CodeActionData> Data;
    std::shared_ptr<Slice<lsproto::CodeActionTag>> Tags;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct CodeActionTagOptions {
    Slice<lsproto::CodeActionTag> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedCodeActionTagOptions resolve() const;
};

struct SetTraceParams {
    lsproto::TraceValue Value;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A `MarkupContent` literal represents a string value which content is interpreted base on its
// kind flag. Currently the protocol supports `plaintext` and `markdown` as markup kinds.
//
// If the kind is `markdown` then the value can contain fenced code blocks like in GitHub issues.
// See https://help.github.com/articles/creating-and-highlighting-code-blocks/#syntax-highlighting
//
// Here is an example how such a string can be constructed using JavaScript / TypeScript:
// ```ts
//
//	let markdown: MarkdownContent = {
//	 kind: MarkupKind.Markdown,
//	 value: [
//	 '# Header',
//	 'Some text',
//	 '```typescript',
//	 'someCode();',
//	 '```'
//	 ].join('\n')
//	};
//
// ```
//
// *Please Note* that clients might sanitize the return markdown. A client could decide to
// remove HTML from the markdown to avoid script execution.
struct MarkupContent {
    lsproto::MarkupKind Kind;
    std::string Value;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct HoverClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::shared_ptr<Slice<lsproto::MarkupKind>> ContentFormat;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedHoverClientCapabilities resolve() const;
};

// Since: 3.18.0
struct ClientCompletionItemOptions {
    std::optional<bool> SnippetSupport;
    std::optional<bool> CommitCharactersSupport;
    std::shared_ptr<Slice<lsproto::MarkupKind>> DocumentationFormat;
    std::optional<bool> DeprecatedSupport;
    std::optional<bool> PreselectSupport;
    std::shared_ptr<lsproto::CompletionItemTagOptions> TagSupport;
    std::optional<bool> InsertReplaceSupport;
    std::shared_ptr<lsproto::ClientCompletionItemResolveOptions> ResolveSupport;
    std::shared_ptr<lsproto::ClientCompletionItemInsertTextModeOptions> InsertTextModeSupport;
    std::optional<bool> LabelDetailsSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientCompletionItemOptions resolve() const;
};

// Since: 3.18.0
struct ClientSignatureInformationOptions {
    std::shared_ptr<Slice<lsproto::MarkupKind>> DocumentationFormat;
    std::shared_ptr<lsproto::ClientSignatureParameterInformationOptions> ParameterInformation;
    std::optional<bool> ActiveParameterSupport;
    std::optional<bool> NoActiveParameterSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientSignatureInformationOptions resolve() const;
};

// An item to transfer a text document from the client to the
// server.
struct TextDocumentItem {
    lsproto::DocumentUri Uri;
    lsproto::LanguageKind LanguageId;
    int32_t Version;
    std::string Text;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Provides information about the context in which an inline completion was requested.
//
// Since: 3.18.0
struct InlineCompletionContext {
    lsproto::InlineCompletionTriggerKind TriggerKind;
    std::shared_ptr<lsproto::SelectedCompletionInfo> SelectedCompletionInfo;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// General client capabilities.
//
// Since: 3.16.0
struct GeneralClientCapabilities {
    std::shared_ptr<lsproto::StaleRequestSupportOptions> StaleRequestSupport;
    std::shared_ptr<lsproto::RegularExpressionsClientCapabilities> RegularExpressions;
    std::shared_ptr<lsproto::MarkdownClientCapabilities> Markdown;
    std::shared_ptr<Slice<lsproto::PositionEncodingKind>> PositionEncodings;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedGeneralClientCapabilities resolve() const;
};

// An event describing a file change.
struct FileEvent {
    lsproto::DocumentUri Uri;
    lsproto::FileChangeType Type;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.18.0
struct ClientDiagnosticsTagOptions {
    Slice<lsproto::DiagnosticTag> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedClientDiagnosticsTagOptions resolve() const;
};

// Contains additional information about the context in which a completion request is triggered.
struct CompletionContext {
    lsproto::CompletionTriggerKind TriggerKind;
    std::optional<std::string> TriggerCharacter;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Additional information about the context in which a signature help request was triggered.
//
// Since: 3.15.0
struct SignatureHelpContext {
    lsproto::SignatureHelpTriggerKind TriggerKind;
    std::optional<std::string> TriggerCharacter;
    bool IsRetrigger;
    std::shared_ptr<lsproto::SignatureHelp> ActiveSignatureHelp;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct WorkspaceEditClientCapabilities {
    std::optional<bool> DocumentChanges;
    std::shared_ptr<Slice<lsproto::ResourceOperationKind>> ResourceOperations;
    std::shared_ptr<lsproto::FailureHandlingKind> FailureHandling;
    std::optional<bool> NormalizesLineEndings;
    std::shared_ptr<lsproto::ChangeAnnotationsSupportOptions> ChangeAnnotationSupport;
    std::optional<bool> MetadataSupport;
    std::optional<bool> SnippetEditSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedWorkspaceEditClientCapabilities resolve() const;
};

// Since: 3.16.0
struct SemanticTokensClientCapabilities {
    std::optional<bool> DynamicRegistration;
    std::shared_ptr<lsproto::ClientSemanticTokensRequestOptions> Requests;
    Slice<std::string> TokenTypes;
    Slice<std::string> TokenModifiers;
    Slice<lsproto::TokenFormat> Formats;
    std::optional<bool> OverlappingTokenSupport;
    std::optional<bool> MultilineTokenSupport;
    std::optional<bool> ServerCancelSupport;
    std::optional<bool> AugmentsSyntaxTokens;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    ResolvedSemanticTokensClientCapabilities resolve() const;
};

// Parameters for the custom/setLogVerbosity notification.
struct SetLogVerbosityParams {
    lsproto::LogVerbosity Verbosity;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A VS-specific reference item with grouping support for Find All References.
struct VSReferenceItem {
    int32_t VSId;
    std::optional<int32_t> VSDefinitionId;
    std::shared_ptr<Slice<lsproto::VSReferenceKind>> VSKind;
    lsproto::Location VSLocation;
    std::shared_ptr<lsproto::VSClassifiedTextElement> VSDefinitionText;
    std::optional<std::string> VSProjectName;
    std::optional<std::string> VSContainingType;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct CodeLensData {
    lsproto::CodeLensKind Kind;
    lsproto::DocumentUri Uri;
    int32_t Position;
    std::optional<int32_t> SupplementalFileIndex;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// AutoImportFix contains information about an auto-import suggestion.
struct AutoImportFix {
    lsproto::AutoImportFixKind Kind;
    std::string Name;
    lsproto::ImportKind ImportKind;
    bool UseRequire;
    lsproto::AddAsTypeOnly AddAsTypeOnly;
    std::string ModuleSpecifier;
    int32_t ImportIndex;
    std::shared_ptr<lsproto::Position> UsagePosition;
    std::string NamespacePrefix;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Union types
struct IntegerOrString {
    std::shared_ptr<int32_t> Integer;
    std::shared_ptr<std::string> String;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
    std::string AsString() const;
};

struct WorkDoneProgressCreateParams {
    lsproto::IntegerOrString Token;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct WorkDoneProgressCancelParams {
    lsproto::IntegerOrString Token;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct CancelParams {
    lsproto::IntegerOrString Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct BooleanOrEmptyObject {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<EmptyObject> EmptyObject;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrSemanticTokensFullDelta {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::SemanticTokensFullDelta> SemanticTokensFullDelta;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile {
    std::shared_ptr<lsproto::TextDocumentEdit> TextDocumentEdit;
    std::shared_ptr<lsproto::CreateFile> CreateFile;
    std::shared_ptr<lsproto::RenameFile> RenameFile;
    std::shared_ptr<lsproto::DeleteFile> DeleteFile;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// A workspace edit represents changes to many resources managed in the workspace. The edit
// should either provide `changes` or `documentChanges`. If documentChanges are present
// they are preferred over `changes` if the client can handle versioned document edits.
//
// Since version 3.13.0 a workspace edit can contain resource operations as well. If resource
// operations are present clients need to execute the operations in the order in which they
// are provided. So a workspace edit for example can consist of the following two changes:
// (1) a create file a.txt and (2) a text document edit which insert text into file a.txt.
//
// An invalid sequence (e.g. (1) delete file a.txt and (2) insert text into file a.txt) will
// cause failure of the operation. How the client recovers from the failure is described by
// the client capability: `workspace.workspaceEdit.failureHandling`
struct WorkspaceEdit {
    std::shared_ptr<Map<lsproto::DocumentUri,Slice<std::shared_ptr<lsproto::TextEdit>>>> Changes;
    std::shared_ptr<Slice<lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile>> DocumentChanges;
    std::shared_ptr<Map<std::string,std::shared_ptr<lsproto::ChangeAnnotation>>> ChangeAnnotations;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct StringOrInlayHintLabelParts {
    std::shared_ptr<std::string> String;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::InlayHintLabelPart>>> InlayHintLabelParts;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// Inlay hint information.
//
// Since: 3.17.0
struct InlayHint {
    lsproto::Position Position;
    lsproto::StringOrInlayHintLabelParts Label;
    std::shared_ptr<lsproto::InlayHintKind> Kind;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::TextEdit>>> TextEdits;
    std::shared_ptr<lsproto::StringOrMarkupContent> Tooltip;
    std::optional<bool> PaddingLeft;
    std::optional<bool> PaddingRight;
    std::shared_ptr<lsproto::InlayHintData> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct StringOrMarkupContent {
    std::shared_ptr<std::string> String;
    std::shared_ptr<lsproto::MarkupContent> MarkupContent;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
    std::string AsString() const;
};

// Represents a diagnostic, such as a compiler error or warning. Diagnostic objects
// are only valid in the scope of a resource.
struct Diagnostic {
    lsproto::Range Range;
    std::shared_ptr<lsproto::DiagnosticSeverity> Severity;
    std::shared_ptr<lsproto::IntegerOrString> Code;
    std::shared_ptr<lsproto::CodeDescription> CodeDescription;
    std::optional<std::string> Source;
    lsproto::StringOrMarkupContent Message;
    std::shared_ptr<Slice<lsproto::DiagnosticTag>> Tags;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::DiagnosticRelatedInformation>>> RelatedInformation;
    std::shared_ptr<lsproto::DiagnosticData> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
    std::string AsString() const;
    std::string CodeAsString() const;
};

struct WorkspaceFullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport {
    std::shared_ptr<lsproto::WorkspaceFullDocumentDiagnosticReport> FullDocumentDiagnosticReport;
    std::shared_ptr<lsproto::WorkspaceUnchangedDocumentDiagnosticReport> UnchangedDocumentDiagnosticReport;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// A workspace diagnostic report.
//
// Since: 3.17.0
struct WorkspaceDiagnosticReport {
    Slice<lsproto::WorkspaceFullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport> Items;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A partial result for a workspace diagnostic report.
//
// Since: 3.17.0
struct WorkspaceDiagnosticReportPartialResult {
    Slice<lsproto::WorkspaceFullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport> Items;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct StringOrStringValue {
    std::shared_ptr<std::string> String;
    std::shared_ptr<lsproto::StringValue> StringValue;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// An inline completion item represents a text snippet that is proposed inline to complete text that is being typed.
//
// Since: 3.18.0
struct InlineCompletionItem {
    lsproto::StringOrStringValue InsertText;
    std::optional<std::string> FilterText;
    std::shared_ptr<lsproto::Range> Range;
    std::shared_ptr<lsproto::Command> Command;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct IntegerOrNull {
    std::shared_ptr<int32_t> Integer;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// A text document identifier to optionally denote a specific version of a text document.
struct OptionalVersionedTextDocumentIdentifier {
    lsproto::DocumentUri Uri;
    lsproto::IntegerOrNull Version;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct StringOrNull {
    std::shared_ptr<std::string> String;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct DocumentUriOrNull {
    std::shared_ptr<lsproto::DocumentUri> DocumentUri;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct InitializeParams {
    std::shared_ptr<lsproto::IntegerOrString> WorkDoneToken;
    lsproto::IntegerOrNull ProcessId;
    std::shared_ptr<lsproto::ClientInfo> ClientInfo;
    std::optional<std::string> Locale;
    std::shared_ptr<lsproto::StringOrNull> RootPath;
    lsproto::DocumentUriOrNull RootUri;
    std::shared_ptr<lsproto::ClientCapabilities> Capabilities;
    std::shared_ptr<lsproto::InitializationOptionsOrNull> InitializationOptions;
    std::shared_ptr<lsproto::TraceValue> Trace;
    std::shared_ptr<lsproto::WorkspaceFoldersOrNull> WorkspaceFolders;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct InitializationOptionsOrNull {
    std::shared_ptr<lsproto::InitializationOptions> InitializationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct WorkspaceFoldersOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::WorkspaceFolder>>> WorkspaceFolders;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct StringOrStrings {
    std::shared_ptr<std::string> String;
    std::shared_ptr<Slice<std::string>> Strings;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct TextDocumentContentChangePartialOrWholeDocument {
    std::shared_ptr<lsproto::TextDocumentContentChangePartial> Partial;
    std::shared_ptr<lsproto::TextDocumentContentChangeWholeDocument> WholeDocument;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// The change text document notification's parameters.
struct DidChangeTextDocumentParams {
    lsproto::VersionedTextDocumentIdentifier TextDocument;
    Slice<lsproto::TextDocumentContentChangePartialOrWholeDocument> ContentChanges;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct TextEditOrInsertReplaceEdit {
    std::shared_ptr<lsproto::TextEdit> TextEdit;
    std::shared_ptr<lsproto::InsertReplaceEdit> InsertReplaceEdit;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct UintegerOrNull {
    std::shared_ptr<uint32_t> Uinteger;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct LocationOrLocationUriOnly {
    std::shared_ptr<lsproto::Location> Location;
    std::shared_ptr<lsproto::LocationUriOnly> LocationUriOnly;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// A special workspace symbol that supports locations without a range.
//
// See also SymbolInformation.
//
// Since: 3.17.0
struct WorkspaceSymbol {
    std::string Name;
    lsproto::SymbolKind Kind;
    std::shared_ptr<Slice<lsproto::SymbolTag>> Tags;
    std::optional<std::string> ContainerName;
    lsproto::LocationOrLocationUriOnly Location;
    std::shared_ptr<lsproto::WorkspaceSymbolData> Data;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct WorkDoneProgressBeginOrReportOrEnd {
    std::shared_ptr<lsproto::WorkDoneProgressBegin> Begin;
    std::shared_ptr<lsproto::WorkDoneProgressReport> Report;
    std::shared_ptr<lsproto::WorkDoneProgressEnd> End;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct ProgressParams {
    lsproto::IntegerOrString Token;
    lsproto::WorkDoneProgressBeginOrReportOrEnd Value;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct TextEditOrAnnotatedTextEditOrSnippetTextEdit {
    std::shared_ptr<lsproto::TextEdit> TextEdit;
    std::shared_ptr<lsproto::AnnotatedTextEdit> AnnotatedTextEdit;
    std::shared_ptr<lsproto::SnippetTextEdit> SnippetTextEdit;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// Describes textual changes on a text document. A TextDocumentEdit describes all changes
// on a document version Si and after they are applied move the document to version Si+1.
// So the creator of a TextDocumentEdit doesn't need to sort the array of edits or do any
// kind of ordering. However the edits must be non overlapping.
struct TextDocumentEdit {
    lsproto::OptionalVersionedTextDocumentIdentifier TextDocument;
    Slice<lsproto::TextEditOrAnnotatedTextEditOrSnippetTextEdit> Edits;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct FullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport {
    std::shared_ptr<lsproto::FullDocumentDiagnosticReport> FullDocumentDiagnosticReport;
    std::shared_ptr<lsproto::UnchangedDocumentDiagnosticReport> UnchangedDocumentDiagnosticReport;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// A partial result for a document diagnostic report.
//
// Since: 3.17.0
struct DocumentDiagnosticReportPartialResult {
    Map<lsproto::DocumentUri,lsproto::FullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport> RelatedDocuments;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct TextDocumentSyncOptionsOrKind {
    std::shared_ptr<lsproto::TextDocumentSyncOptions> Options;
    std::shared_ptr<lsproto::TextDocumentSyncKind> Kind;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrHoverOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::HoverOptions> HoverOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrDeclarationOptionsOrDeclarationRegistrationOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::DeclarationOptions> DeclarationOptions;
    std::shared_ptr<lsproto::DeclarationRegistrationOptions> DeclarationRegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrDefinitionOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::DefinitionOptions> DefinitionOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrTypeDefinitionOptionsOrTypeDefinitionRegistrationOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::TypeDefinitionOptions> TypeDefinitionOptions;
    std::shared_ptr<lsproto::TypeDefinitionRegistrationOptions> TypeDefinitionRegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrImplementationOptionsOrImplementationRegistrationOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::ImplementationOptions> ImplementationOptions;
    std::shared_ptr<lsproto::ImplementationRegistrationOptions> ImplementationRegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrReferenceOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::ReferenceOptions> ReferenceOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrDocumentHighlightOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::DocumentHighlightOptions> DocumentHighlightOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrDocumentSymbolOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::DocumentSymbolOptions> DocumentSymbolOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrCodeActionOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::CodeActionOptions> CodeActionOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrDocumentColorOptionsOrDocumentColorRegistrationOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::DocumentColorOptions> DocumentColorOptions;
    std::shared_ptr<lsproto::DocumentColorRegistrationOptions> DocumentColorRegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrWorkspaceSymbolOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::WorkspaceSymbolOptions> WorkspaceSymbolOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrDocumentFormattingOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::DocumentFormattingOptions> DocumentFormattingOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrDocumentRangeFormattingOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::DocumentRangeFormattingOptions> DocumentRangeFormattingOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrRenameOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::RenameOptions> RenameOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrFoldingRangeOptionsOrFoldingRangeRegistrationOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::FoldingRangeOptions> FoldingRangeOptions;
    std::shared_ptr<lsproto::FoldingRangeRegistrationOptions> FoldingRangeRegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrSelectionRangeOptionsOrSelectionRangeRegistrationOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::SelectionRangeOptions> SelectionRangeOptions;
    std::shared_ptr<lsproto::SelectionRangeRegistrationOptions> SelectionRangeRegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrCallHierarchyOptionsOrCallHierarchyRegistrationOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::CallHierarchyOptions> CallHierarchyOptions;
    std::shared_ptr<lsproto::CallHierarchyRegistrationOptions> CallHierarchyRegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrLinkedEditingRangeOptionsOrLinkedEditingRangeRegistrationOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::LinkedEditingRangeOptions> LinkedEditingRangeOptions;
    std::shared_ptr<lsproto::LinkedEditingRangeRegistrationOptions> LinkedEditingRangeRegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct SemanticTokensOptionsOrRegistrationOptions {
    std::shared_ptr<lsproto::SemanticTokensOptions> Options;
    std::shared_ptr<lsproto::SemanticTokensRegistrationOptions> RegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrMonikerOptionsOrMonikerRegistrationOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::MonikerOptions> MonikerOptions;
    std::shared_ptr<lsproto::MonikerRegistrationOptions> MonikerRegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrTypeHierarchyOptionsOrTypeHierarchyRegistrationOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::TypeHierarchyOptions> TypeHierarchyOptions;
    std::shared_ptr<lsproto::TypeHierarchyRegistrationOptions> TypeHierarchyRegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrInlineValueOptionsOrInlineValueRegistrationOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::InlineValueOptions> InlineValueOptions;
    std::shared_ptr<lsproto::InlineValueRegistrationOptions> InlineValueRegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrInlayHintOptionsOrInlayHintRegistrationOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::InlayHintOptions> InlayHintOptions;
    std::shared_ptr<lsproto::InlayHintRegistrationOptions> InlayHintRegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct DiagnosticOptionsOrRegistrationOptions {
    std::shared_ptr<lsproto::DiagnosticOptions> Options;
    std::shared_ptr<lsproto::DiagnosticRegistrationOptions> RegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrInlineCompletionOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::InlineCompletionOptions> InlineCompletionOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct PatternOrRelativePattern {
    std::shared_ptr<std::string> Pattern;
    std::shared_ptr<lsproto::RelativePattern> RelativePattern;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct FileSystemWatcher {
    lsproto::PatternOrRelativePattern GlobPattern;
    std::shared_ptr<lsproto::WatchKind> Kind;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A document filter where `pattern` is required field.
//
// Since: 3.18.0
struct TextDocumentFilterPattern {
    std::optional<std::string> Language;
    std::optional<std::string> Scheme;
    lsproto::PatternOrRelativePattern Pattern;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct RangeOrEditRangeWithInsertReplace {
    std::shared_ptr<lsproto::Range> Range;
    std::shared_ptr<lsproto::EditRangeWithInsertReplace> EditRangeWithInsertReplace;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct BooleanOrSaveOptions {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::SaveOptions> SaveOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct TextDocumentContentOptionsOrRegistrationOptions {
    std::shared_ptr<lsproto::TextDocumentContentOptions> Options;
    std::shared_ptr<lsproto::TextDocumentContentRegistrationOptions> RegistrationOptions;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct StringOrTuple {
    std::shared_ptr<std::string> String;
    std::shared_ptr<std::array<uint32_t,2>> Tuple;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// Represents a parameter of a callable-signature. A parameter can
// have a label and a doc-comment.
struct ParameterInformation {
    lsproto::StringOrTuple Label;
    std::shared_ptr<lsproto::StringOrMarkupContent> Documentation;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct StringOrBoolean {
    std::shared_ptr<std::string> String;
    std::shared_ptr<bool> Boolean;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct WorkspaceFolderOrURI {
    std::shared_ptr<lsproto::WorkspaceFolder> WorkspaceFolder;
    std::shared_ptr<lsproto::URI> URI;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// A relative pattern is a helper to construct glob patterns that are matched
// relatively to a base URI. The common value for a `baseUri` is a workspace
// folder root, but it can be another absolute URI as well.
//
// Since: 3.17.0
struct RelativePattern {
    lsproto::WorkspaceFolderOrURI BaseUri;
    std::string Pattern;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct BooleanOrClientSemanticTokensRequestFullDelta {
    std::shared_ptr<bool> Boolean;
    std::shared_ptr<lsproto::ClientSemanticTokensRequestFullDelta> ClientSemanticTokensRequestFullDelta;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct VSImageElementOrClassifiedTextElementOrContainerElement {
    std::shared_ptr<lsproto::VSImageElement> ImageElement;
    std::shared_ptr<lsproto::VSClassifiedTextElement> ClassifiedTextElement;
    std::shared_ptr<lsproto::VSContainerElement> ContainerElement;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct LocationOrLocationsOrDefinitionLinksOrNull {
    std::shared_ptr<lsproto::Location> Location;
    std::shared_ptr<Slice<lsproto::Location>> Locations;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::LocationLink>>> DefinitionLinks;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
    std::shared_ptr<Slice<lsproto::Location>> GetLocations() const { return Locations; }
};

struct FoldingRangesOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::FoldingRange>>> FoldingRanges;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct LocationOrLocationsOrDeclarationLinksOrNull {
    std::shared_ptr<lsproto::Location> Location;
    std::shared_ptr<Slice<lsproto::Location>> Locations;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::LocationLink>>> DeclarationLinks;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
    std::shared_ptr<Slice<lsproto::Location>> GetLocations() const { return Locations; }
};

struct SelectionRangesOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::SelectionRange>>> SelectionRanges;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct CallHierarchyItemsOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::CallHierarchyItem>>> CallHierarchyItems;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct CallHierarchyIncomingCallsOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::CallHierarchyIncomingCall>>> CallHierarchyIncomingCalls;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct CallHierarchyOutgoingCallsOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::CallHierarchyOutgoingCall>>> CallHierarchyOutgoingCalls;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct SemanticTokensOrNull {
    std::shared_ptr<lsproto::SemanticTokens> SemanticTokens;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct SemanticTokensOrSemanticTokensDeltaOrNull {
    std::shared_ptr<lsproto::SemanticTokens> SemanticTokens;
    std::shared_ptr<lsproto::SemanticTokensDelta> SemanticTokensDelta;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct LinkedEditingRangesOrNull {
    std::shared_ptr<lsproto::LinkedEditingRanges> LinkedEditingRanges;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct WorkspaceEditOrNull {
    std::shared_ptr<lsproto::WorkspaceEdit> WorkspaceEdit;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct MonikersOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::Moniker>>> Monikers;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct TypeHierarchyItemsOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::TypeHierarchyItem>>> TypeHierarchyItems;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct InlayHintsOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::InlayHint>>> InlayHints;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct RelatedFullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport {
    std::shared_ptr<lsproto::RelatedFullDocumentDiagnosticReport> FullDocumentDiagnosticReport;
    std::shared_ptr<lsproto::RelatedUnchangedDocumentDiagnosticReport> UnchangedDocumentDiagnosticReport;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct InlineCompletionListOrItemsOrNull {
    std::shared_ptr<lsproto::InlineCompletionList> List;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::InlineCompletionItem>>> Items;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct MessageActionItemOrNull {
    std::shared_ptr<lsproto::MessageActionItem> MessageActionItem;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct TextEditsOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::TextEdit>>> TextEdits;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct CompletionItemsOrListOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::CompletionItem>>> Items;
    std::shared_ptr<lsproto::CompletionList> List;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct HoverOrNull {
    std::shared_ptr<lsproto::Hover> Hover;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct SignatureHelpOrNull {
    std::shared_ptr<lsproto::SignatureHelp> SignatureHelp;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct LocationsOrNull {
    std::shared_ptr<Slice<lsproto::Location>> Locations;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
    std::shared_ptr<Slice<lsproto::Location>> GetLocations() const { return Locations; }
};

struct DocumentHighlightsOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::DocumentHighlight>>> DocumentHighlights;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct SymbolInformationsOrDocumentSymbolsOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::SymbolInformation>>> SymbolInformations;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::DocumentSymbol>>> DocumentSymbols;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct CommandOrCodeAction {
    std::shared_ptr<lsproto::Command> Command;
    std::shared_ptr<lsproto::CodeAction> CodeAction;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct CommandOrCodeActionArrayOrNull {
    std::shared_ptr<Slice<lsproto::CommandOrCodeAction>> CommandOrCodeActionArray;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct SymbolInformationsOrWorkspaceSymbolsOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::SymbolInformation>>> SymbolInformations;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::WorkspaceSymbol>>> WorkspaceSymbols;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct CodeLensesOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::CodeLens>>> CodeLenses;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct DocumentLinksOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::DocumentLink>>> DocumentLinks;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct RangeOrPrepareRenamePlaceholderOrPrepareRenameDefaultBehaviorOrNull {
    std::shared_ptr<lsproto::Range> Range;
    std::shared_ptr<lsproto::PrepareRenamePlaceholder> PrepareRenamePlaceholder;
    std::shared_ptr<lsproto::PrepareRenameDefaultBehavior> PrepareRenameDefaultBehavior;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct LSPAnyOrNull {
    std::shared_ptr<lsproto::LSPAny> LSPAny;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct MultiDocumentHighlightsOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::MultiDocumentHighlight>>> MultiDocumentHighlights;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct VSOnAutoInsertResponseItemOrNull {
    std::shared_ptr<lsproto::VSOnAutoInsertResponseItem> VSOnAutoInsertResponseItem;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct VSReferenceItemsOrNull {
    std::shared_ptr<Slice<std::shared_ptr<lsproto::VSReferenceItem>>> VSReferenceItems;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct RequestFailureTelemetryEventOrPerformanceStatsTelemetryEventOrProjectInfoTelemetryEventOrNull {
    std::shared_ptr<lsproto::RequestFailureTelemetryEvent> RequestFailureTelemetryEvent;
    std::shared_ptr<lsproto::PerformanceStatsTelemetryEvent> PerformanceStatsTelemetryEvent;
    std::shared_ptr<lsproto::ProjectInfoTelemetryEvent> ProjectInfoTelemetryEvent;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct TextDocumentFilterLanguageOrSchemeOrPattern {
    std::shared_ptr<lsproto::TextDocumentFilterLanguage> Language;
    std::shared_ptr<lsproto::TextDocumentFilterScheme> Scheme;
    std::shared_ptr<lsproto::TextDocumentFilterPattern> Pattern;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct DocumentSelectorOrNull {
    std::shared_ptr<Slice<lsproto::TextDocumentFilterLanguageOrSchemeOrPattern>> DocumentSelector;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct ImplementationRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct TypeDefinitionRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct DocumentColorRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct FoldingRangeRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct DeclarationRegistrationOptions {
    std::optional<bool> WorkDoneProgress;
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct SelectionRangeRegistrationOptions {
    std::optional<bool> WorkDoneProgress;
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Call hierarchy options used during static or dynamic registration.
//
// Since: 3.16.0
struct CallHierarchyRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Since: 3.16.0
struct SemanticTokensRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::shared_ptr<lsproto::SemanticTokensLegend> Legend;
    std::shared_ptr<lsproto::BooleanOrEmptyObject> Range;
    std::shared_ptr<lsproto::BooleanOrSemanticTokensFullDelta> Full;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct LinkedEditingRangeRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct MonikerRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Type hierarchy options used during static or dynamic registration.
//
// Since: 3.17.0
struct TypeHierarchyRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Inline value options used during static or dynamic registration.
//
// Since: 3.17.0
struct InlineValueRegistrationOptions {
    std::optional<bool> WorkDoneProgress;
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Inlay hint options used during static or dynamic registration.
//
// Since: 3.17.0
struct InlayHintRegistrationOptions {
    std::optional<bool> WorkDoneProgress;
    std::optional<bool> ResolveProvider;
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Diagnostic registration options.
//
// Since: 3.17.0
struct DiagnosticRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<std::string> Identifier;
    bool InterFileDependencies;
    bool WorkspaceDiagnostics;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Inline completion options used during static or dynamic registration.
//
// Since: 3.18.0
struct InlineCompletionRegistrationOptions {
    std::optional<bool> WorkDoneProgress;
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<std::string> Id;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// General text document registration options.
struct TextDocumentRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Describe options to be used when registered for text document change events.
struct TextDocumentChangeRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    lsproto::TextDocumentSyncKind SyncKind;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Save registration options.
struct TextDocumentSaveRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> IncludeText;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a CompletionRequest.
struct CompletionRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::shared_ptr<Slice<std::string>> TriggerCharacters;
    std::shared_ptr<Slice<std::string>> AllCommitCharacters;
    std::optional<bool> ResolveProvider;
    std::shared_ptr<lsproto::ServerCompletionItemOptions> CompletionItem;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a HoverRequest.
struct HoverRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a SignatureHelpRequest.
struct SignatureHelpRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::shared_ptr<Slice<std::string>> TriggerCharacters;
    std::shared_ptr<Slice<std::string>> RetriggerCharacters;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a DefinitionRequest.
struct DefinitionRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a ReferencesRequest.
struct ReferenceRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a DocumentHighlightRequest.
struct DocumentHighlightRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a DocumentSymbolRequest.
struct DocumentSymbolRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<std::string> Label;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a CodeActionRequest.
struct CodeActionRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::shared_ptr<Slice<lsproto::CodeActionKind>> CodeActionKinds;
    std::shared_ptr<Slice<std::shared_ptr<lsproto::CodeActionKindDocumentation>>> Documentation;
    std::optional<bool> ResolveProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a CodeLensRequest.
struct CodeLensRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<bool> ResolveProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a DocumentLinkRequest.
struct DocumentLinkRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<bool> ResolveProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a DocumentFormattingRequest.
struct DocumentFormattingRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a DocumentRangeFormattingRequest.
struct DocumentRangeFormattingRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<bool> RangesSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a DocumentOnTypeFormattingRequest.
struct DocumentOnTypeFormattingRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::string FirstTriggerCharacter;
    std::shared_ptr<Slice<std::string>> MoreTriggerCharacter;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// Registration options for a RenameRequest.
struct RenameRegistrationOptions {
    lsproto::DocumentSelectorOrNull DocumentSelector;
    std::optional<bool> WorkDoneProgress;
    std::optional<bool> PrepareProvider;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct StringOrMarkedStringWithLanguage {
    std::shared_ptr<std::string> String;
    std::shared_ptr<lsproto::MarkedStringWithLanguage> MarkedStringWithLanguage;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct MarkupContentOrStringOrMarkedStringWithLanguageOrMarkedStrings {
    std::shared_ptr<lsproto::MarkupContent> MarkupContent;
    std::shared_ptr<std::string> String;
    std::shared_ptr<lsproto::MarkedStringWithLanguage> MarkedStringWithLanguage;
    std::shared_ptr<Slice<lsproto::StringOrMarkedStringWithLanguage>> MarkedStrings;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// The result of a hover request.
struct Hover {
    lsproto::MarkupContentOrStringOrMarkedStringWithLanguageOrMarkedStrings Contents;
    std::shared_ptr<lsproto::Range> Range;
    bool CanIncreaseVerbosity;
    std::shared_ptr<lsproto::VSContainerElement> VSRawContent;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

struct InlineValueTextOrVariableLookupOrEvaluatableExpression {
    std::shared_ptr<lsproto::InlineValueText> Text;
    std::shared_ptr<lsproto::InlineValueVariableLookup> VariableLookup;
    std::shared_ptr<lsproto::InlineValueEvaluatableExpression> EvaluatableExpression;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

struct InlineValuesOrNull {
    std::shared_ptr<Slice<lsproto::InlineValueTextOrVariableLookupOrEvaluatableExpression>> InlineValues;
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
    bool isZero() const;
};

// Literal types
// StringLiteralBegin is a literal type for "begin"
// StringLiteralBegin — literal type
struct StringLiteralBegin {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct WorkDoneProgressBegin {
    lsproto::StringLiteralBegin Kind;
    std::string Title;
    std::optional<bool> Cancellable;
    std::optional<std::string> Message;
    std::optional<uint32_t> Percentage;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralReport is a literal type for "report"
// StringLiteralReport — literal type
struct StringLiteralReport {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct WorkDoneProgressReport {
    lsproto::StringLiteralReport Kind;
    std::optional<bool> Cancellable;
    std::optional<std::string> Message;
    std::optional<uint32_t> Percentage;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralEnd is a literal type for "end"
// StringLiteralEnd — literal type
struct StringLiteralEnd {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

struct WorkDoneProgressEnd {
    lsproto::StringLiteralEnd Kind;
    std::optional<std::string> Message;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralCreate is a literal type for "create"
// StringLiteralCreate — literal type
struct StringLiteralCreate {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// Create file operation.
struct CreateFile {
    lsproto::StringLiteralCreate Kind;
    std::optional<std::string> AnnotationId;
    lsproto::DocumentUri Uri;
    std::shared_ptr<lsproto::CreateFileOptions> Options;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralRename is a literal type for "rename"
// StringLiteralRename — literal type
struct StringLiteralRename {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// Rename file operation
struct RenameFile {
    lsproto::StringLiteralRename Kind;
    std::optional<std::string> AnnotationId;
    lsproto::DocumentUri OldUri;
    lsproto::DocumentUri NewUri;
    std::shared_ptr<lsproto::RenameFileOptions> Options;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralDelete is a literal type for "delete"
// StringLiteralDelete — literal type
struct StringLiteralDelete {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// Delete file operation
struct DeleteFile {
    lsproto::StringLiteralDelete Kind;
    std::optional<std::string> AnnotationId;
    lsproto::DocumentUri Uri;
    std::shared_ptr<lsproto::DeleteFileOptions> Options;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralFull is a literal type for "full"
// StringLiteralFull — literal type
struct StringLiteralFull {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// A full diagnostic report with a set of related documents.
//
// Since: 3.17.0
struct RelatedFullDocumentDiagnosticReport {
    lsproto::StringLiteralFull Kind;
    std::optional<std::string> ResultId;
    Slice<std::shared_ptr<lsproto::Diagnostic>> Items;
    std::shared_ptr<Map<lsproto::DocumentUri,lsproto::FullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport>> RelatedDocuments;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A diagnostic report with a full set of problems.
//
// Since: 3.17.0
struct FullDocumentDiagnosticReport {
    lsproto::StringLiteralFull Kind;
    std::optional<std::string> ResultId;
    Slice<std::shared_ptr<lsproto::Diagnostic>> Items;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A full document diagnostic report for a workspace diagnostic result.
//
// Since: 3.17.0
struct WorkspaceFullDocumentDiagnosticReport {
    lsproto::StringLiteralFull Kind;
    std::optional<std::string> ResultId;
    Slice<std::shared_ptr<lsproto::Diagnostic>> Items;
    lsproto::DocumentUri Uri;
    lsproto::IntegerOrNull Version;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralUnchanged is a literal type for "unchanged"
// StringLiteralUnchanged — literal type
struct StringLiteralUnchanged {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// An unchanged diagnostic report with a set of related documents.
//
// Since: 3.17.0
struct RelatedUnchangedDocumentDiagnosticReport {
    lsproto::StringLiteralUnchanged Kind;
    std::string ResultId;
    std::shared_ptr<Map<lsproto::DocumentUri,lsproto::FullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport>> RelatedDocuments;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// A diagnostic report indicating that the last returned
// report is still accurate.
//
// Since: 3.17.0
struct UnchangedDocumentDiagnosticReport {
    lsproto::StringLiteralUnchanged Kind;
    std::string ResultId;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// An unchanged document diagnostic report for a workspace diagnostic result.
//
// Since: 3.17.0
struct WorkspaceUnchangedDocumentDiagnosticReport {
    lsproto::StringLiteralUnchanged Kind;
    std::string ResultId;
    lsproto::DocumentUri Uri;
    lsproto::IntegerOrNull Version;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralSnippet is a literal type for "snippet"
// StringLiteralSnippet — literal type
struct StringLiteralSnippet {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// A string value used as a snippet is a template which allows to insert text
// and to control the editor cursor when insertion happens.
//
// A snippet can define tab stops and placeholders with `$1`, `$2`
// and `${3:foo}`. `$0` defines the final tab stop, it defaults to
// the end of the snippet. Variables are defined with `$name` and
// `${name:default value}`.
//
// Since: 3.18.0
struct StringValue {
    lsproto::StringLiteralSnippet Kind;
    std::string Value;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralLanguageServerErrorResponse is a literal type for "languageServer.errorResponse"
// StringLiteralLanguageServerErrorResponse — literal type
struct StringLiteralLanguageServerErrorResponse {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// StringLiteralError is a literal type for "error"
// StringLiteralError — literal type
struct StringLiteralError {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// A RequestFailureTelemetryEvent is sent when a request fails and the server recovers.
struct RequestFailureTelemetryEvent {
    lsproto::StringLiteralLanguageServerErrorResponse EventName;
    lsproto::StringLiteralError TelemetryPurpose;
    std::shared_ptr<lsproto::RequestFailureTelemetryProperties> Properties;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralLanguageServerPerformanceStats is a literal type for "languageServer.performanceStats"
// StringLiteralLanguageServerPerformanceStats — literal type
struct StringLiteralLanguageServerPerformanceStats {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// StringLiteralUsage is a literal type for "usage"
// StringLiteralUsage — literal type
struct StringLiteralUsage {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// A PerformanceStatsTelemetryEvent is sent periodically with performance and resource usage statistics.
struct PerformanceStatsTelemetryEvent {
    lsproto::StringLiteralLanguageServerPerformanceStats EventName;
    lsproto::StringLiteralUsage TelemetryPurpose;
    std::shared_ptr<lsproto::PerformanceStatsTelemetryMeasurements> Measurements;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralLanguageServerProjectInfo is a literal type for "languageServer.projectInfo"
// StringLiteralLanguageServerProjectInfo — literal type
struct StringLiteralLanguageServerProjectInfo {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// A ProjectInfoTelemetryEvent is sent once per project when it is first loaded.
struct ProjectInfoTelemetryEvent {
    lsproto::StringLiteralLanguageServerProjectInfo EventName;
    lsproto::StringLiteralUsage TelemetryPurpose;
    Map<std::string,std::string> Properties;
    std::shared_ptr<lsproto::ProjectInfoTelemetryMeasurements> Measurements;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralClassifiedTextRun is a literal type for "ClassifiedTextRun"
// StringLiteralClassifiedTextRun — literal type
struct StringLiteralClassifiedTextRun {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// A classified text run with text and classification type, used for colorized display in VS.
struct VSClassifiedTextRun {
    std::string ClassificationTypeName;
    std::string Text;
    std::optional<std::string> MarkerTagType;
    int32_t Style;
    lsproto::StringLiteralClassifiedTextRun VSType;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralClassifiedTextElement is a literal type for "ClassifiedTextElement"
// StringLiteralClassifiedTextElement — literal type
struct StringLiteralClassifiedTextElement {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// A classified text element containing an array of classified text runs, used for colorized labels in VS.
struct VSClassifiedTextElement {
    Slice<std::shared_ptr<lsproto::VSClassifiedTextRun>> Runs;
    lsproto::StringLiteralClassifiedTextElement VSType;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralImageId is a literal type for "ImageId"
// StringLiteralImageId — literal type
struct StringLiteralImageId {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// Identifies an image in a VS image catalog. Used to render symbol-kind icons (e.g. in hover tooltips).
struct VSImageId {
    std::string Guid;
    int32_t Id;
    lsproto::StringLiteralImageId VSType;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralImageElement is a literal type for "ImageElement"
// StringLiteralImageElement — literal type
struct StringLiteralImageElement {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// An image element (e.g. a symbol-kind icon) for use in VS rich content such as hover tooltips.
struct VSImageElement {
    std::shared_ptr<lsproto::VSImageId> ImageId;
    lsproto::StringLiteralImageElement VSType;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// StringLiteralContainerElement is a literal type for "ContainerElement"
// StringLiteralContainerElement — literal type
struct StringLiteralContainerElement {
    bool isZero() const { return true; }
    std::string marshalJSONTo(json::Encoder& enc) const;
    std::string unmarshalJSONFrom(json::Decoder& dec);
};

// A container element that groups other VS rich-content elements (images, classified text, or nested containers). Used to build the VS hover raw content that combines a symbol icon with colorized text.
struct VSContainerElement {
    lsproto::VSContainerElementStyle Style;
    Slice<lsproto::VSImageElementOrClassifiedTextElementOrContainerElement> Elements;
    lsproto::StringLiteralContainerElement VSType;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedChangeAnnotationsSupportOptions is a resolved version of ChangeAnnotationsSupportOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedChangeAnnotationsSupportOptions {
    bool GroupsOnLabel;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedWorkspaceEditClientCapabilities is a resolved version of WorkspaceEditClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
struct ResolvedWorkspaceEditClientCapabilities {
    bool DocumentChanges;
    Slice<lsproto::ResourceOperationKind> ResourceOperations;
    lsproto::FailureHandlingKind FailureHandling;
    bool NormalizesLineEndings;
    lsproto::ResolvedChangeAnnotationsSupportOptions ChangeAnnotationSupport;
    bool MetadataSupport;
    bool SnippetEditSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDidChangeConfigurationClientCapabilities is a resolved version of DidChangeConfigurationClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
struct ResolvedDidChangeConfigurationClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDidChangeWatchedFilesClientCapabilities is a resolved version of DidChangeWatchedFilesClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
struct ResolvedDidChangeWatchedFilesClientCapabilities {
    bool DynamicRegistration;
    bool RelativePatternSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientSymbolKindOptions is a resolved version of ClientSymbolKindOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientSymbolKindOptions {
    Slice<lsproto::SymbolKind> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientSymbolTagOptions is a resolved version of ClientSymbolTagOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientSymbolTagOptions {
    Slice<lsproto::SymbolTag> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientSymbolResolveOptions is a resolved version of ClientSymbolResolveOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientSymbolResolveOptions {
    Slice<std::string> Properties;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedWorkspaceSymbolClientCapabilities is a resolved version of WorkspaceSymbolClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities for a WorkspaceSymbolRequest.
struct ResolvedWorkspaceSymbolClientCapabilities {
    bool DynamicRegistration;
    lsproto::ResolvedClientSymbolKindOptions SymbolKind;
    lsproto::ResolvedClientSymbolTagOptions TagSupport;
    lsproto::ResolvedClientSymbolResolveOptions ResolveSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedExecuteCommandClientCapabilities is a resolved version of ExecuteCommandClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// The client capabilities of a ExecuteCommandRequest.
struct ResolvedExecuteCommandClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedSemanticTokensWorkspaceClientCapabilities is a resolved version of SemanticTokensWorkspaceClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.16.0
struct ResolvedSemanticTokensWorkspaceClientCapabilities {
    bool RefreshSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedCodeLensWorkspaceClientCapabilities is a resolved version of CodeLensWorkspaceClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.16.0
struct ResolvedCodeLensWorkspaceClientCapabilities {
    bool RefreshSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedFileOperationClientCapabilities is a resolved version of FileOperationClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Capabilities relating to events from file operations by the user in the client.
//
// These events do not come from the file system, they come from user operations
// like renaming a file in the UI.
//
// Since: 3.16.0
struct ResolvedFileOperationClientCapabilities {
    bool DynamicRegistration;
    bool DidCreate;
    bool WillCreate;
    bool DidRename;
    bool WillRename;
    bool DidDelete;
    bool WillDelete;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedInlineValueWorkspaceClientCapabilities is a resolved version of InlineValueWorkspaceClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client workspace capabilities specific to inline values.
//
// Since: 3.17.0
struct ResolvedInlineValueWorkspaceClientCapabilities {
    bool RefreshSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedInlayHintWorkspaceClientCapabilities is a resolved version of InlayHintWorkspaceClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client workspace capabilities specific to inlay hints.
//
// Since: 3.17.0
struct ResolvedInlayHintWorkspaceClientCapabilities {
    bool RefreshSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDiagnosticWorkspaceClientCapabilities is a resolved version of DiagnosticWorkspaceClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Workspace client capabilities specific to diagnostic pull requests.
//
// Since: 3.17.0
struct ResolvedDiagnosticWorkspaceClientCapabilities {
    bool RefreshSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedFoldingRangeWorkspaceClientCapabilities is a resolved version of FoldingRangeWorkspaceClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// # Client workspace capabilities specific to folding ranges
//
// Since: 3.18.0
struct ResolvedFoldingRangeWorkspaceClientCapabilities {
    bool RefreshSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedTextDocumentContentClientCapabilities is a resolved version of TextDocumentContentClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities for a text document content provider.
//
// Since: 3.18.0
struct ResolvedTextDocumentContentClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedWorkspaceClientCapabilities is a resolved version of WorkspaceClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Workspace specific client capabilities.
struct ResolvedWorkspaceClientCapabilities {
    bool ApplyEdit;
    lsproto::ResolvedWorkspaceEditClientCapabilities WorkspaceEdit;
    lsproto::ResolvedDidChangeConfigurationClientCapabilities DidChangeConfiguration;
    lsproto::ResolvedDidChangeWatchedFilesClientCapabilities DidChangeWatchedFiles;
    lsproto::ResolvedWorkspaceSymbolClientCapabilities Symbol;
    lsproto::ResolvedExecuteCommandClientCapabilities ExecuteCommand;
    bool WorkspaceFolders;
    bool Configuration;
    lsproto::ResolvedSemanticTokensWorkspaceClientCapabilities SemanticTokens;
    lsproto::ResolvedCodeLensWorkspaceClientCapabilities CodeLens;
    lsproto::ResolvedFileOperationClientCapabilities FileOperations;
    lsproto::ResolvedInlineValueWorkspaceClientCapabilities InlineValue;
    lsproto::ResolvedInlayHintWorkspaceClientCapabilities InlayHint;
    lsproto::ResolvedDiagnosticWorkspaceClientCapabilities Diagnostics;
    lsproto::ResolvedFoldingRangeWorkspaceClientCapabilities FoldingRange;
    lsproto::ResolvedTextDocumentContentClientCapabilities TextDocumentContent;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedTextDocumentSyncClientCapabilities is a resolved version of TextDocumentSyncClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
struct ResolvedTextDocumentSyncClientCapabilities {
    bool DynamicRegistration;
    bool WillSave;
    bool WillSaveWaitUntil;
    bool DidSave;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedTextDocumentFilterClientCapabilities is a resolved version of TextDocumentFilterClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
struct ResolvedTextDocumentFilterClientCapabilities {
    bool RelativePatternSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedCompletionItemTagOptions is a resolved version of CompletionItemTagOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedCompletionItemTagOptions {
    Slice<lsproto::CompletionItemTag> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientCompletionItemResolveOptions is a resolved version of ClientCompletionItemResolveOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientCompletionItemResolveOptions {
    Slice<std::string> Properties;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientCompletionItemInsertTextModeOptions is a resolved version of ClientCompletionItemInsertTextModeOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientCompletionItemInsertTextModeOptions {
    Slice<lsproto::InsertTextMode> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientCompletionItemOptions is a resolved version of ClientCompletionItemOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientCompletionItemOptions {
    bool SnippetSupport;
    bool CommitCharactersSupport;
    Slice<lsproto::MarkupKind> DocumentationFormat;
    bool DeprecatedSupport;
    bool PreselectSupport;
    lsproto::ResolvedCompletionItemTagOptions TagSupport;
    bool InsertReplaceSupport;
    lsproto::ResolvedClientCompletionItemResolveOptions ResolveSupport;
    lsproto::ResolvedClientCompletionItemInsertTextModeOptions InsertTextModeSupport;
    bool LabelDetailsSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientCompletionItemOptionsKind is a resolved version of ClientCompletionItemOptionsKind with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientCompletionItemOptionsKind {
    Slice<lsproto::CompletionItemKind> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedCompletionListCapabilities is a resolved version of CompletionListCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// The client supports the following `CompletionList` specific
// capabilities.
//
// Since: 3.17.0
struct ResolvedCompletionListCapabilities {
    Slice<std::string> ItemDefaults;
    bool ApplyKindSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedCompletionClientCapabilities is a resolved version of CompletionClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Completion client capabilities
struct ResolvedCompletionClientCapabilities {
    bool DynamicRegistration;
    lsproto::ResolvedClientCompletionItemOptions CompletionItem;
    lsproto::ResolvedClientCompletionItemOptionsKind CompletionItemKind;
    lsproto::InsertTextMode InsertTextMode;
    bool ContextSupport;
    lsproto::ResolvedCompletionListCapabilities CompletionList;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedHoverClientCapabilities is a resolved version of HoverClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
struct ResolvedHoverClientCapabilities {
    bool DynamicRegistration;
    Slice<lsproto::MarkupKind> ContentFormat;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientSignatureParameterInformationOptions is a resolved version of ClientSignatureParameterInformationOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientSignatureParameterInformationOptions {
    bool LabelOffsetSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientSignatureInformationOptions is a resolved version of ClientSignatureInformationOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientSignatureInformationOptions {
    Slice<lsproto::MarkupKind> DocumentationFormat;
    lsproto::ResolvedClientSignatureParameterInformationOptions ParameterInformation;
    bool ActiveParameterSupport;
    bool NoActiveParameterSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedSignatureHelpClientCapabilities is a resolved version of SignatureHelpClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client Capabilities for a SignatureHelpRequest.
struct ResolvedSignatureHelpClientCapabilities {
    bool DynamicRegistration;
    lsproto::ResolvedClientSignatureInformationOptions SignatureInformation;
    bool ContextSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDeclarationClientCapabilities is a resolved version of DeclarationClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.14.0
struct ResolvedDeclarationClientCapabilities {
    bool DynamicRegistration;
    bool LinkSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDefinitionClientCapabilities is a resolved version of DefinitionClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client Capabilities for a DefinitionRequest.
struct ResolvedDefinitionClientCapabilities {
    bool DynamicRegistration;
    bool LinkSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedTypeDefinitionClientCapabilities is a resolved version of TypeDefinitionClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Since 3.6.0
struct ResolvedTypeDefinitionClientCapabilities {
    bool DynamicRegistration;
    bool LinkSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedImplementationClientCapabilities is a resolved version of ImplementationClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.6.0
struct ResolvedImplementationClientCapabilities {
    bool DynamicRegistration;
    bool LinkSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedReferenceClientCapabilities is a resolved version of ReferenceClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client Capabilities for a ReferencesRequest.
struct ResolvedReferenceClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDocumentHighlightClientCapabilities is a resolved version of DocumentHighlightClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client Capabilities for a DocumentHighlightRequest.
struct ResolvedDocumentHighlightClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDocumentSymbolClientCapabilities is a resolved version of DocumentSymbolClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client Capabilities for a DocumentSymbolRequest.
struct ResolvedDocumentSymbolClientCapabilities {
    bool DynamicRegistration;
    lsproto::ResolvedClientSymbolKindOptions SymbolKind;
    bool HierarchicalDocumentSymbolSupport;
    lsproto::ResolvedClientSymbolTagOptions TagSupport;
    bool LabelSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientCodeActionKindOptions is a resolved version of ClientCodeActionKindOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientCodeActionKindOptions {
    Slice<lsproto::CodeActionKind> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientCodeActionLiteralOptions is a resolved version of ClientCodeActionLiteralOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientCodeActionLiteralOptions {
    lsproto::ResolvedClientCodeActionKindOptions CodeActionKind;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientCodeActionResolveOptions is a resolved version of ClientCodeActionResolveOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientCodeActionResolveOptions {
    Slice<std::string> Properties;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedCodeActionTagOptions is a resolved version of CodeActionTagOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedCodeActionTagOptions {
    Slice<lsproto::CodeActionTag> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedCodeActionClientCapabilities is a resolved version of CodeActionClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// The Client Capabilities of a CodeActionRequest.
struct ResolvedCodeActionClientCapabilities {
    bool DynamicRegistration;
    lsproto::ResolvedClientCodeActionLiteralOptions CodeActionLiteralSupport;
    bool IsPreferredSupport;
    bool DisabledSupport;
    bool DataSupport;
    lsproto::ResolvedClientCodeActionResolveOptions ResolveSupport;
    bool HonorsChangeAnnotations;
    bool DocumentationSupport;
    lsproto::ResolvedCodeActionTagOptions TagSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientCodeLensResolveOptions is a resolved version of ClientCodeLensResolveOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientCodeLensResolveOptions {
    Slice<std::string> Properties;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedCodeLensClientCapabilities is a resolved version of CodeLensClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// The client capabilities of a CodeLensRequest.
struct ResolvedCodeLensClientCapabilities {
    bool DynamicRegistration;
    lsproto::ResolvedClientCodeLensResolveOptions ResolveSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDocumentLinkClientCapabilities is a resolved version of DocumentLinkClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// The client capabilities of a DocumentLinkRequest.
struct ResolvedDocumentLinkClientCapabilities {
    bool DynamicRegistration;
    bool TooltipSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDocumentColorClientCapabilities is a resolved version of DocumentColorClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
struct ResolvedDocumentColorClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDocumentFormattingClientCapabilities is a resolved version of DocumentFormattingClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities of a DocumentFormattingRequest.
struct ResolvedDocumentFormattingClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDocumentRangeFormattingClientCapabilities is a resolved version of DocumentRangeFormattingClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities of a DocumentRangeFormattingRequest.
struct ResolvedDocumentRangeFormattingClientCapabilities {
    bool DynamicRegistration;
    bool RangesSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDocumentOnTypeFormattingClientCapabilities is a resolved version of DocumentOnTypeFormattingClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities of a DocumentOnTypeFormattingRequest.
struct ResolvedDocumentOnTypeFormattingClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedRenameClientCapabilities is a resolved version of RenameClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
struct ResolvedRenameClientCapabilities {
    bool DynamicRegistration;
    bool PrepareSupport;
    lsproto::PrepareSupportDefaultBehavior PrepareSupportDefaultBehavior;
    bool HonorsChangeAnnotations;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientFoldingRangeKindOptions is a resolved version of ClientFoldingRangeKindOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientFoldingRangeKindOptions {
    Slice<lsproto::FoldingRangeKind> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientFoldingRangeOptions is a resolved version of ClientFoldingRangeOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientFoldingRangeOptions {
    bool CollapsedText;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedFoldingRangeClientCapabilities is a resolved version of FoldingRangeClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
struct ResolvedFoldingRangeClientCapabilities {
    bool DynamicRegistration;
    uint32_t RangeLimit;
    bool LineFoldingOnly;
    lsproto::ResolvedClientFoldingRangeKindOptions FoldingRangeKind;
    lsproto::ResolvedClientFoldingRangeOptions FoldingRange;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedSelectionRangeClientCapabilities is a resolved version of SelectionRangeClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
struct ResolvedSelectionRangeClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientDiagnosticsTagOptions is a resolved version of ClientDiagnosticsTagOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientDiagnosticsTagOptions {
    Slice<lsproto::DiagnosticTag> ValueSet;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedPublishDiagnosticsClientCapabilities is a resolved version of PublishDiagnosticsClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// The publish diagnostic client capabilities.
struct ResolvedPublishDiagnosticsClientCapabilities {
    bool RelatedInformation;
    lsproto::ResolvedClientDiagnosticsTagOptions TagSupport;
    bool CodeDescriptionSupport;
    bool DataSupport;
    bool VersionSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedCallHierarchyClientCapabilities is a resolved version of CallHierarchyClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.16.0
struct ResolvedCallHierarchyClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientSemanticTokensRequestOptions is a resolved version of ClientSemanticTokensRequestOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientSemanticTokensRequestOptions {
    lsproto::BooleanOrEmptyObject Range;
    lsproto::BooleanOrClientSemanticTokensRequestFullDelta Full;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedSemanticTokensClientCapabilities is a resolved version of SemanticTokensClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.16.0
struct ResolvedSemanticTokensClientCapabilities {
    bool DynamicRegistration;
    lsproto::ResolvedClientSemanticTokensRequestOptions Requests;
    Slice<std::string> TokenTypes;
    Slice<std::string> TokenModifiers;
    Slice<lsproto::TokenFormat> Formats;
    bool OverlappingTokenSupport;
    bool MultilineTokenSupport;
    bool ServerCancelSupport;
    bool AugmentsSyntaxTokens;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedLinkedEditingRangeClientCapabilities is a resolved version of LinkedEditingRangeClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities for the linked editing range request.
//
// Since: 3.16.0
struct ResolvedLinkedEditingRangeClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedMonikerClientCapabilities is a resolved version of MonikerClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities specific to the moniker request.
//
// Since: 3.16.0
struct ResolvedMonikerClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedTypeHierarchyClientCapabilities is a resolved version of TypeHierarchyClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.17.0
struct ResolvedTypeHierarchyClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedInlineValueClientCapabilities is a resolved version of InlineValueClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities specific to inline values.
//
// Since: 3.17.0
struct ResolvedInlineValueClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientInlayHintResolveOptions is a resolved version of ClientInlayHintResolveOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientInlayHintResolveOptions {
    Slice<std::string> Properties;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedInlayHintClientCapabilities is a resolved version of InlayHintClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Inlay hint client capabilities.
//
// Since: 3.17.0
struct ResolvedInlayHintClientCapabilities {
    bool DynamicRegistration;
    lsproto::ResolvedClientInlayHintResolveOptions ResolveSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedDiagnosticClientCapabilities is a resolved version of DiagnosticClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities specific to diagnostic pull requests.
//
// Since: 3.17.0
struct ResolvedDiagnosticClientCapabilities {
    bool RelatedInformation;
    lsproto::ResolvedClientDiagnosticsTagOptions TagSupport;
    bool CodeDescriptionSupport;
    bool DataSupport;
    bool DynamicRegistration;
    bool RelatedDocumentSupport;
    bool MarkupMessageSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedInlineCompletionClientCapabilities is a resolved version of InlineCompletionClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities specific to inline completions.
//
// Since: 3.18.0
struct ResolvedInlineCompletionClientCapabilities {
    bool DynamicRegistration;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedTextDocumentClientCapabilities is a resolved version of TextDocumentClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Text document specific client capabilities.
struct ResolvedTextDocumentClientCapabilities {
    lsproto::ResolvedTextDocumentSyncClientCapabilities Synchronization;
    lsproto::ResolvedTextDocumentFilterClientCapabilities Filters;
    lsproto::ResolvedCompletionClientCapabilities Completion;
    lsproto::ResolvedHoverClientCapabilities Hover;
    lsproto::ResolvedSignatureHelpClientCapabilities SignatureHelp;
    lsproto::ResolvedDeclarationClientCapabilities Declaration;
    lsproto::ResolvedDefinitionClientCapabilities Definition;
    lsproto::ResolvedTypeDefinitionClientCapabilities TypeDefinition;
    lsproto::ResolvedImplementationClientCapabilities Implementation;
    lsproto::ResolvedReferenceClientCapabilities References;
    lsproto::ResolvedDocumentHighlightClientCapabilities DocumentHighlight;
    lsproto::ResolvedDocumentSymbolClientCapabilities DocumentSymbol;
    lsproto::ResolvedCodeActionClientCapabilities CodeAction;
    lsproto::ResolvedCodeLensClientCapabilities CodeLens;
    lsproto::ResolvedDocumentLinkClientCapabilities DocumentLink;
    lsproto::ResolvedDocumentColorClientCapabilities ColorProvider;
    lsproto::ResolvedDocumentFormattingClientCapabilities Formatting;
    lsproto::ResolvedDocumentRangeFormattingClientCapabilities RangeFormatting;
    lsproto::ResolvedDocumentOnTypeFormattingClientCapabilities OnTypeFormatting;
    lsproto::ResolvedRenameClientCapabilities Rename;
    lsproto::ResolvedFoldingRangeClientCapabilities FoldingRange;
    lsproto::ResolvedSelectionRangeClientCapabilities SelectionRange;
    lsproto::ResolvedPublishDiagnosticsClientCapabilities PublishDiagnostics;
    lsproto::ResolvedCallHierarchyClientCapabilities CallHierarchy;
    lsproto::ResolvedSemanticTokensClientCapabilities SemanticTokens;
    lsproto::ResolvedLinkedEditingRangeClientCapabilities LinkedEditingRange;
    lsproto::ResolvedMonikerClientCapabilities Moniker;
    lsproto::ResolvedTypeHierarchyClientCapabilities TypeHierarchy;
    lsproto::ResolvedInlineValueClientCapabilities InlineValue;
    lsproto::ResolvedInlayHintClientCapabilities InlayHint;
    lsproto::ResolvedDiagnosticClientCapabilities Diagnostic;
    lsproto::ResolvedInlineCompletionClientCapabilities InlineCompletion;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientShowMessageActionItemOptions is a resolved version of ClientShowMessageActionItemOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedClientShowMessageActionItemOptions {
    bool AdditionalPropertiesSupport;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedShowMessageRequestClientCapabilities is a resolved version of ShowMessageRequestClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Show message request client capabilities
struct ResolvedShowMessageRequestClientCapabilities {
    lsproto::ResolvedClientShowMessageActionItemOptions MessageActionItem;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedShowDocumentClientCapabilities is a resolved version of ShowDocumentClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities for the showDocument request.
//
// Since: 3.16.0
struct ResolvedShowDocumentClientCapabilities {
    bool Support;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedWindowClientCapabilities is a resolved version of WindowClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
struct ResolvedWindowClientCapabilities {
    bool WorkDoneProgress;
    lsproto::ResolvedShowMessageRequestClientCapabilities ShowMessage;
    lsproto::ResolvedShowDocumentClientCapabilities ShowDocument;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedStaleRequestSupportOptions is a resolved version of StaleRequestSupportOptions with all optional fields
// converted to non-pointer values for easier access.
//
// Since: 3.18.0
struct ResolvedStaleRequestSupportOptions {
    bool Cancel;
    Slice<std::string> RetryOnContentModified;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedRegularExpressionsClientCapabilities is a resolved version of RegularExpressionsClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities specific to regular expressions.
//
// Since: 3.16.0
struct ResolvedRegularExpressionsClientCapabilities {
    std::string Engine;
    std::string Version;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedMarkdownClientCapabilities is a resolved version of MarkdownClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// Client capabilities specific to the used markdown parser.
//
// Since: 3.16.0
struct ResolvedMarkdownClientCapabilities {
    std::string Parser;
    std::string Version;
    Slice<std::string> AllowedTags;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedGeneralClientCapabilities is a resolved version of GeneralClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// General client capabilities.
//
// Since: 3.16.0
struct ResolvedGeneralClientCapabilities {
    lsproto::ResolvedStaleRequestSupportOptions StaleRequestSupport;
    lsproto::ResolvedRegularExpressionsClientCapabilities RegularExpressions;
    lsproto::ResolvedMarkdownClientCapabilities Markdown;
    Slice<lsproto::PositionEncodingKind> PositionEncodings;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedExperimentalClientCapabilities is a resolved version of ExperimentalClientCapabilities with all optional fields
// converted to non-pointer values for easier access.
//
// ExperimentalClientCapabilities contains experimental capabilities under development.
struct ResolvedExperimentalClientCapabilities {
    bool HoverVerbosityLevel;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ResolvedClientCapabilities is a version of ClientCapabilities where all nested
// fields are values (not pointers), making it easier to access deeply nested capabilities.
// Use (*ClientCapabilities).Resolve() to convert from ClientCapabilities.
//
// Defines the capabilities provided by the client.
struct ResolvedClientCapabilities {
    lsproto::ResolvedWorkspaceClientCapabilities Workspace;
    lsproto::ResolvedTextDocumentClientCapabilities TextDocument;
    lsproto::ResolvedWindowClientCapabilities Window;
    lsproto::ResolvedGeneralClientCapabilities General;
    lsproto::ResolvedExperimentalClientCapabilities Experimental;
    bool VSSupportsVisualStudioExtensions;
    int32_t VSSupportedSnippetVersion;
    bool VSSupportsNotIncludingTextInTextDocumentDidOpen;
    bool VSSupportsIconExtensions;
    bool VSSupportsDiagnosticRequests;
    std::vector<StructFieldBinding> fieldBindings();
    std::string unmarshalJSONFrom(json::Decoder& dec);
    std::string marshalJSONTo(json::Encoder& enc) const;
    bool isZero() const;
};

// ---------------------------------------------------------------------------
// Type aliases — lsp_generated.go:11748
// ---------------------------------------------------------------------------

// Request response types
// Response type for `textDocument/implementation`
using ImplementationResponse = lsproto::LocationOrLocationsOrDefinitionLinksOrNull;

// Response type for `textDocument/typeDefinition`
using TypeDefinitionResponse = lsproto::LocationOrLocationsOrDefinitionLinksOrNull;

// Response type for `workspace/workspaceFolders`
using WorkspaceFoldersResponse = lsproto::WorkspaceFoldersOrNull;

// Response type for `workspace/configuration`
using ConfigurationResponse = Slice<lsproto::LSPAny>;

// Response type for `textDocument/documentColor`
using DocumentColorResponse = Slice<std::shared_ptr<lsproto::ColorInformation>>;

// Response type for `textDocument/colorPresentation`
using ColorPresentationResponse = Slice<std::shared_ptr<lsproto::ColorPresentation>>;

// Response type for `textDocument/foldingRange`
using FoldingRangeResponse = lsproto::FoldingRangesOrNull;

// Response type for `workspace/foldingRange/refresh`
using FoldingRangeRefreshResponse = lsproto::Null;

// Response type for `textDocument/declaration`
using DeclarationResponse = lsproto::LocationOrLocationsOrDeclarationLinksOrNull;

// Response type for `textDocument/selectionRange`
using SelectionRangeResponse = lsproto::SelectionRangesOrNull;

// Response type for `window/workDoneProgress/create`
using WorkDoneProgressCreateResponse = lsproto::Null;

// Response type for `textDocument/prepareCallHierarchy`
using CallHierarchyPrepareResponse = lsproto::CallHierarchyItemsOrNull;

// Response type for `callHierarchy/incomingCalls`
using CallHierarchyIncomingCallsResponse = lsproto::CallHierarchyIncomingCallsOrNull;

// Response type for `callHierarchy/outgoingCalls`
using CallHierarchyOutgoingCallsResponse = lsproto::CallHierarchyOutgoingCallsOrNull;

// Response type for `textDocument/semanticTokens/full`
using SemanticTokensResponse = lsproto::SemanticTokensOrNull;

// Response type for `textDocument/semanticTokens/full/delta`
using SemanticTokensDeltaResponse = lsproto::SemanticTokensOrSemanticTokensDeltaOrNull;

// Response type for `textDocument/semanticTokens/range`
using SemanticTokensRangeResponse = lsproto::SemanticTokensOrNull;

// Response type for `workspace/semanticTokens/refresh`
using SemanticTokensRefreshResponse = lsproto::Null;

// Response type for `window/showDocument`
using ShowDocumentResponse = std::shared_ptr<lsproto::ShowDocumentResult>;

// Response type for `textDocument/linkedEditingRange`
using LinkedEditingRangeResponse = lsproto::LinkedEditingRangesOrNull;

// Response type for `workspace/willCreateFiles`
using WillCreateFilesResponse = lsproto::WorkspaceEditOrNull;

// Response type for `workspace/willRenameFiles`
using WillRenameFilesResponse = lsproto::WorkspaceEditOrNull;

// Response type for `workspace/willDeleteFiles`
using WillDeleteFilesResponse = lsproto::WorkspaceEditOrNull;

// Response type for `textDocument/moniker`
using MonikerResponse = lsproto::MonikersOrNull;

// Response type for `textDocument/prepareTypeHierarchy`
using TypeHierarchyPrepareResponse = lsproto::TypeHierarchyItemsOrNull;

// Response type for `typeHierarchy/supertypes`
using TypeHierarchySupertypesResponse = lsproto::TypeHierarchyItemsOrNull;

// Response type for `typeHierarchy/subtypes`
using TypeHierarchySubtypesResponse = lsproto::TypeHierarchyItemsOrNull;

// Response type for `textDocument/inlineValue`
using InlineValueResponse = lsproto::InlineValuesOrNull;

// Response type for `workspace/inlineValue/refresh`
using InlineValueRefreshResponse = lsproto::Null;

// Response type for `textDocument/inlayHint`
using InlayHintResponse = lsproto::InlayHintsOrNull;

// Response type for `inlayHint/resolve`
using InlayHintResolveResponse = std::shared_ptr<lsproto::InlayHint>;

// Response type for `workspace/inlayHint/refresh`
using InlayHintRefreshResponse = lsproto::Null;

// Response type for `textDocument/diagnostic`
using DocumentDiagnosticResponse = lsproto::RelatedFullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport;

// Response type for `workspace/diagnostic`
using WorkspaceDiagnosticResponse = std::shared_ptr<lsproto::WorkspaceDiagnosticReport>;

// Response type for `workspace/diagnostic/refresh`
using DiagnosticRefreshResponse = lsproto::Null;

// Response type for `textDocument/inlineCompletion`
using InlineCompletionResponse = lsproto::InlineCompletionListOrItemsOrNull;

// Response type for `workspace/textDocumentContent`
using TextDocumentContentResponse = std::shared_ptr<lsproto::TextDocumentContentResult>;

// Response type for `workspace/textDocumentContent/refresh`
using TextDocumentContentRefreshResponse = lsproto::Null;

// Response type for `client/registerCapability`
using RegistrationResponse = lsproto::Null;

// Response type for `client/unregisterCapability`
using UnregistrationResponse = lsproto::Null;

// Response type for `initialize`
using InitializeResponse = std::shared_ptr<lsproto::InitializeResult>;

// Response type for `shutdown`
using ShutdownResponse = lsproto::Null;

// Response type for `window/showMessageRequest`
using ShowMessageResponse = lsproto::MessageActionItemOrNull;

// Response type for `textDocument/willSaveWaitUntil`
using WillSaveTextDocumentWaitUntilResponse = lsproto::TextEditsOrNull;

// Response type for `textDocument/completion`
using CompletionResponse = lsproto::CompletionItemsOrListOrNull;

// Response type for `completionItem/resolve`
using CompletionResolveResponse = std::shared_ptr<lsproto::CompletionItem>;

// Response type for `textDocument/hover`
using HoverResponse = lsproto::HoverOrNull;

// Response type for `textDocument/signatureHelp`
using SignatureHelpResponse = lsproto::SignatureHelpOrNull;

// Response type for `textDocument/definition`
using DefinitionResponse = lsproto::LocationOrLocationsOrDefinitionLinksOrNull;

// Response type for `textDocument/references`
using ReferencesResponse = lsproto::LocationsOrNull;

// Response type for `textDocument/documentHighlight`
using DocumentHighlightResponse = lsproto::DocumentHighlightsOrNull;

// Response type for `textDocument/documentSymbol`
using DocumentSymbolResponse = lsproto::SymbolInformationsOrDocumentSymbolsOrNull;

// Response type for `textDocument/codeAction`
using CodeActionResponse = lsproto::CommandOrCodeActionArrayOrNull;

// Response type for `codeAction/resolve`
using CodeActionResolveResponse = std::shared_ptr<lsproto::CodeAction>;

// Response type for `workspace/symbol`
using WorkspaceSymbolResponse = lsproto::SymbolInformationsOrWorkspaceSymbolsOrNull;

// Response type for `workspaceSymbol/resolve`
using WorkspaceSymbolResolveResponse = std::shared_ptr<lsproto::WorkspaceSymbol>;

// Response type for `textDocument/codeLens`
using CodeLensResponse = lsproto::CodeLensesOrNull;

// Response type for `codeLens/resolve`
using CodeLensResolveResponse = std::shared_ptr<lsproto::CodeLens>;

// Response type for `workspace/codeLens/refresh`
using CodeLensRefreshResponse = lsproto::Null;

// Response type for `textDocument/documentLink`
using DocumentLinkResponse = lsproto::DocumentLinksOrNull;

// Response type for `documentLink/resolve`
using DocumentLinkResolveResponse = std::shared_ptr<lsproto::DocumentLink>;

// Response type for `textDocument/formatting`
using DocumentFormattingResponse = lsproto::TextEditsOrNull;

// Response type for `textDocument/rangeFormatting`
using DocumentRangeFormattingResponse = lsproto::TextEditsOrNull;

// Response type for `textDocument/rangesFormatting`
using DocumentRangesFormattingResponse = lsproto::TextEditsOrNull;

// Response type for `textDocument/onTypeFormatting`
using DocumentOnTypeFormattingResponse = lsproto::TextEditsOrNull;

// Response type for `textDocument/rename`
using RenameResponse = lsproto::WorkspaceEditOrNull;

// Response type for `textDocument/prepareRename`
using PrepareRenameResponse = lsproto::RangeOrPrepareRenamePlaceholderOrPrepareRenameDefaultBehaviorOrNull;

// Response type for `workspace/executeCommand`
using ExecuteCommandResponse = lsproto::LSPAnyOrNull;

// Response type for `workspace/applyEdit`
using ApplyWorkspaceEditResponse = std::shared_ptr<lsproto::ApplyWorkspaceEditResult>;

// Response type for `custom/runGC`
using RunGCResponse = lsproto::Null;

// Response type for `custom/saveHeapProfile`
using SaveHeapProfileResponse = std::shared_ptr<lsproto::ProfileResult>;

// Response type for `custom/saveAllocProfile`
using SaveAllocProfileResponse = std::shared_ptr<lsproto::ProfileResult>;

// Response type for `custom/startCPUProfile`
using StartCPUProfileResponse = lsproto::Null;

// Response type for `custom/stopCPUProfile`
using StopCPUProfileResponse = std::shared_ptr<lsproto::ProfileResult>;

// Response type for `custom/initializeAPISession`
using CustomInitializeAPISessionResponse = std::shared_ptr<lsproto::InitializeAPISessionResult>;

// Response type for `custom/projectInfo`
using CustomProjectInfoResponse = std::shared_ptr<lsproto::ProjectInfoResult>;

// Response type for `custom/setContentMapperContributions`
using CustomSetContentMapperContributionsResponse = lsproto::Null;

// Response type for `custom/textDocument/sourceDefinition`
using CustomTextDocumentSourceDefinitionResponse = std::shared_ptr<lsproto::LocationOrLocationsOrDefinitionLinksOrNull>;

// Response type for `custom/textDocument/multiDocumentHighlight`
using CustomMultiDocumentHighlightResponse = lsproto::MultiDocumentHighlightsOrNull;

// Response type for `textDocument/_vs_onAutoInsert`
using VSOnAutoInsertResponse = lsproto::VSOnAutoInsertResponseItemOrNull;

// Response type for `textDocument/_vs_references`
using VSReferencesResponse = lsproto::VSReferenceItemsOrNull;

// Type aliases
using TelemetryEvent = lsproto::RequestFailureTelemetryEventOrPerformanceStatsTelemetryEventOrProjectInfoTelemetryEventOrNull;

// ---------------------------------------------------------------------------
// Methods — lsp_generated.go:10793
// ---------------------------------------------------------------------------

inline const Method MethodTextDocumentImplementation = "textDocument/implementation";
inline const Method MethodTextDocumentTypeDefinition = "textDocument/typeDefinition";
inline const Method MethodWorkspaceWorkspaceFolders = "workspace/workspaceFolders";
inline const Method MethodWorkspaceConfiguration = "workspace/configuration";
inline const Method MethodTextDocumentDocumentColor = "textDocument/documentColor";
inline const Method MethodTextDocumentColorPresentation = "textDocument/colorPresentation";
inline const Method MethodTextDocumentFoldingRange = "textDocument/foldingRange";
inline const Method MethodWorkspaceFoldingRangeRefresh = "workspace/foldingRange/refresh";
inline const Method MethodTextDocumentDeclaration = "textDocument/declaration";
inline const Method MethodTextDocumentSelectionRange = "textDocument/selectionRange";
inline const Method MethodWindowWorkDoneProgressCreate = "window/workDoneProgress/create";
inline const Method MethodTextDocumentPrepareCallHierarchy = "textDocument/prepareCallHierarchy";
inline const Method MethodCallHierarchyIncomingCalls = "callHierarchy/incomingCalls";
inline const Method MethodCallHierarchyOutgoingCalls = "callHierarchy/outgoingCalls";
inline const Method MethodTextDocumentSemanticTokensFull = "textDocument/semanticTokens/full";
inline const Method MethodTextDocumentSemanticTokensFullDelta = "textDocument/semanticTokens/full/delta";
inline const Method MethodTextDocumentSemanticTokensRange = "textDocument/semanticTokens/range";
inline const Method MethodWorkspaceSemanticTokensRefresh = "workspace/semanticTokens/refresh";
inline const Method MethodWindowShowDocument = "window/showDocument";
inline const Method MethodTextDocumentLinkedEditingRange = "textDocument/linkedEditingRange";
inline const Method MethodWorkspaceWillCreateFiles = "workspace/willCreateFiles";
inline const Method MethodWorkspaceWillRenameFiles = "workspace/willRenameFiles";
inline const Method MethodWorkspaceWillDeleteFiles = "workspace/willDeleteFiles";
inline const Method MethodTextDocumentMoniker = "textDocument/moniker";
inline const Method MethodTextDocumentPrepareTypeHierarchy = "textDocument/prepareTypeHierarchy";
inline const Method MethodTypeHierarchySupertypes = "typeHierarchy/supertypes";
inline const Method MethodTypeHierarchySubtypes = "typeHierarchy/subtypes";
inline const Method MethodTextDocumentInlineValue = "textDocument/inlineValue";
inline const Method MethodWorkspaceInlineValueRefresh = "workspace/inlineValue/refresh";
inline const Method MethodTextDocumentInlayHint = "textDocument/inlayHint";
inline const Method MethodInlayHintResolve = "inlayHint/resolve";
inline const Method MethodWorkspaceInlayHintRefresh = "workspace/inlayHint/refresh";
inline const Method MethodTextDocumentDiagnostic = "textDocument/diagnostic";
inline const Method MethodWorkspaceDiagnostic = "workspace/diagnostic";
inline const Method MethodWorkspaceDiagnosticRefresh = "workspace/diagnostic/refresh";
inline const Method MethodTextDocumentInlineCompletion = "textDocument/inlineCompletion";
inline const Method MethodWorkspaceTextDocumentContent = "workspace/textDocumentContent";
inline const Method MethodWorkspaceTextDocumentContentRefresh = "workspace/textDocumentContent/refresh";
inline const Method MethodClientRegisterCapability = "client/registerCapability";
inline const Method MethodClientUnregisterCapability = "client/unregisterCapability";
inline const Method MethodInitialize = "initialize";
inline const Method MethodShutdown = "shutdown";
inline const Method MethodWindowShowMessageRequest = "window/showMessageRequest";
inline const Method MethodTextDocumentWillSaveWaitUntil = "textDocument/willSaveWaitUntil";
inline const Method MethodTextDocumentCompletion = "textDocument/completion";
inline const Method MethodCompletionItemResolve = "completionItem/resolve";
inline const Method MethodTextDocumentHover = "textDocument/hover";
inline const Method MethodTextDocumentSignatureHelp = "textDocument/signatureHelp";
inline const Method MethodTextDocumentDefinition = "textDocument/definition";
inline const Method MethodTextDocumentReferences = "textDocument/references";
inline const Method MethodTextDocumentDocumentHighlight = "textDocument/documentHighlight";
inline const Method MethodTextDocumentDocumentSymbol = "textDocument/documentSymbol";
inline const Method MethodTextDocumentCodeAction = "textDocument/codeAction";
inline const Method MethodCodeActionResolve = "codeAction/resolve";
inline const Method MethodWorkspaceSymbol = "workspace/symbol";
inline const Method MethodWorkspaceSymbolResolve = "workspaceSymbol/resolve";
inline const Method MethodTextDocumentCodeLens = "textDocument/codeLens";
inline const Method MethodCodeLensResolve = "codeLens/resolve";
inline const Method MethodWorkspaceCodeLensRefresh = "workspace/codeLens/refresh";
inline const Method MethodTextDocumentDocumentLink = "textDocument/documentLink";
inline const Method MethodDocumentLinkResolve = "documentLink/resolve";
inline const Method MethodTextDocumentFormatting = "textDocument/formatting";
inline const Method MethodTextDocumentRangeFormatting = "textDocument/rangeFormatting";
inline const Method MethodTextDocumentRangesFormatting = "textDocument/rangesFormatting";
inline const Method MethodTextDocumentOnTypeFormatting = "textDocument/onTypeFormatting";
inline const Method MethodTextDocumentRename = "textDocument/rename";
inline const Method MethodTextDocumentPrepareRename = "textDocument/prepareRename";
inline const Method MethodWorkspaceExecuteCommand = "workspace/executeCommand";
inline const Method MethodWorkspaceApplyEdit = "workspace/applyEdit";
inline const Method MethodCustomRunGC = "custom/runGC";
inline const Method MethodCustomSaveHeapProfile = "custom/saveHeapProfile";
inline const Method MethodCustomSaveAllocProfile = "custom/saveAllocProfile";
inline const Method MethodCustomStartCPUProfile = "custom/startCPUProfile";
inline const Method MethodCustomStopCPUProfile = "custom/stopCPUProfile";
inline const Method MethodCustomInitializeAPISession = "custom/initializeAPISession";
inline const Method MethodCustomProjectInfo = "custom/projectInfo";
inline const Method MethodCustomSetContentMapperContributions = "custom/setContentMapperContributions";
inline const Method MethodCustomTextDocumentSourceDefinition = "custom/textDocument/sourceDefinition";
inline const Method MethodCustomTextDocumentMultiDocumentHighlight = "custom/textDocument/multiDocumentHighlight";
inline const Method MethodTextDocumentVSOnAutoInsert = "textDocument/_vs_onAutoInsert";
inline const Method MethodTextDocumentVSReferences = "textDocument/_vs_references";
inline const Method MethodWorkspaceDidChangeWorkspaceFolders = "workspace/didChangeWorkspaceFolders";
inline const Method MethodWindowWorkDoneProgressCancel = "window/workDoneProgress/cancel";
inline const Method MethodWorkspaceDidCreateFiles = "workspace/didCreateFiles";
inline const Method MethodWorkspaceDidRenameFiles = "workspace/didRenameFiles";
inline const Method MethodWorkspaceDidDeleteFiles = "workspace/didDeleteFiles";
inline const Method MethodInitialized = "initialized";
inline const Method MethodExit = "exit";
inline const Method MethodWorkspaceDidChangeConfiguration = "workspace/didChangeConfiguration";
inline const Method MethodWindowShowMessage = "window/showMessage";
inline const Method MethodWindowLogMessage = "window/logMessage";
inline const Method MethodTelemetryEvent = "telemetry/event";
inline const Method MethodTextDocumentDidOpen = "textDocument/didOpen";
inline const Method MethodTextDocumentDidChange = "textDocument/didChange";
inline const Method MethodTextDocumentDidClose = "textDocument/didClose";
inline const Method MethodTextDocumentDidSave = "textDocument/didSave";
inline const Method MethodTextDocumentWillSave = "textDocument/willSave";
inline const Method MethodWorkspaceDidChangeWatchedFiles = "workspace/didChangeWatchedFiles";
inline const Method MethodTextDocumentPublishDiagnostics = "textDocument/publishDiagnostics";
inline const Method MethodSetTrace = "$/setTrace";
inline const Method MethodLogTrace = "$/logTrace";
inline const Method MethodCancelRequest = "$/cancelRequest";
inline const Method MethodProgress = "$/progress";
inline const Method MethodCustomSetLogVerbosity = "custom/setLogVerbosity";
inline const Method MethodTextDocumentSemanticTokens = "textDocument/semanticTokens";

// Type mapping info for `textDocument/implementation`
inline const RequestInfo<std::shared_ptr<lsproto::ImplementationParams>, lsproto::ImplementationResponse> TextDocumentImplementationInfo{MethodTextDocumentImplementation};

// Type mapping info for `textDocument/typeDefinition`
inline const RequestInfo<std::shared_ptr<lsproto::TypeDefinitionParams>, lsproto::TypeDefinitionResponse> TextDocumentTypeDefinitionInfo{MethodTextDocumentTypeDefinition};

// Type mapping info for `workspace/workspaceFolders`
inline const RequestInfo<lsproto::NoParams, lsproto::WorkspaceFoldersResponse> WorkspaceWorkspaceFoldersInfo{MethodWorkspaceWorkspaceFolders};

// Type mapping info for `workspace/configuration`
inline const RequestInfo<std::shared_ptr<lsproto::ConfigurationParams>, lsproto::ConfigurationResponse> WorkspaceConfigurationInfo{MethodWorkspaceConfiguration};

// Type mapping info for `textDocument/documentColor`
inline const RequestInfo<std::shared_ptr<lsproto::DocumentColorParams>, lsproto::DocumentColorResponse> TextDocumentDocumentColorInfo{MethodTextDocumentDocumentColor};

// Type mapping info for `textDocument/colorPresentation`
inline const RequestInfo<std::shared_ptr<lsproto::ColorPresentationParams>, lsproto::ColorPresentationResponse> TextDocumentColorPresentationInfo{MethodTextDocumentColorPresentation};

// Type mapping info for `textDocument/foldingRange`
inline const RequestInfo<std::shared_ptr<lsproto::FoldingRangeParams>, lsproto::FoldingRangeResponse> TextDocumentFoldingRangeInfo{MethodTextDocumentFoldingRange};

// Type mapping info for `workspace/foldingRange/refresh`
inline const RequestInfo<lsproto::NoParams, lsproto::FoldingRangeRefreshResponse> WorkspaceFoldingRangeRefreshInfo{MethodWorkspaceFoldingRangeRefresh};

// Type mapping info for `textDocument/declaration`
inline const RequestInfo<std::shared_ptr<lsproto::DeclarationParams>, lsproto::DeclarationResponse> TextDocumentDeclarationInfo{MethodTextDocumentDeclaration};

// Type mapping info for `textDocument/selectionRange`
inline const RequestInfo<std::shared_ptr<lsproto::SelectionRangeParams>, lsproto::SelectionRangeResponse> TextDocumentSelectionRangeInfo{MethodTextDocumentSelectionRange};

// Type mapping info for `window/workDoneProgress/create`
inline const RequestInfo<std::shared_ptr<lsproto::WorkDoneProgressCreateParams>, lsproto::WorkDoneProgressCreateResponse> WindowWorkDoneProgressCreateInfo{MethodWindowWorkDoneProgressCreate};

// Type mapping info for `textDocument/prepareCallHierarchy`
inline const RequestInfo<std::shared_ptr<lsproto::CallHierarchyPrepareParams>, lsproto::CallHierarchyPrepareResponse> TextDocumentPrepareCallHierarchyInfo{MethodTextDocumentPrepareCallHierarchy};

// Type mapping info for `callHierarchy/incomingCalls`
inline const RequestInfo<std::shared_ptr<lsproto::CallHierarchyIncomingCallsParams>, lsproto::CallHierarchyIncomingCallsResponse> CallHierarchyIncomingCallsInfo{MethodCallHierarchyIncomingCalls};

// Type mapping info for `callHierarchy/outgoingCalls`
inline const RequestInfo<std::shared_ptr<lsproto::CallHierarchyOutgoingCallsParams>, lsproto::CallHierarchyOutgoingCallsResponse> CallHierarchyOutgoingCallsInfo{MethodCallHierarchyOutgoingCalls};

// Type mapping info for `textDocument/semanticTokens/full`
inline const RequestInfo<std::shared_ptr<lsproto::SemanticTokensParams>, lsproto::SemanticTokensResponse> TextDocumentSemanticTokensFullInfo{MethodTextDocumentSemanticTokensFull};

// Type mapping info for `textDocument/semanticTokens/full/delta`
inline const RequestInfo<std::shared_ptr<lsproto::SemanticTokensDeltaParams>, lsproto::SemanticTokensDeltaResponse> TextDocumentSemanticTokensFullDeltaInfo{MethodTextDocumentSemanticTokensFullDelta};

// Type mapping info for `textDocument/semanticTokens/range`
inline const RequestInfo<std::shared_ptr<lsproto::SemanticTokensRangeParams>, lsproto::SemanticTokensRangeResponse> TextDocumentSemanticTokensRangeInfo{MethodTextDocumentSemanticTokensRange};

// Type mapping info for `workspace/semanticTokens/refresh`
inline const RequestInfo<lsproto::NoParams, lsproto::SemanticTokensRefreshResponse> WorkspaceSemanticTokensRefreshInfo{MethodWorkspaceSemanticTokensRefresh};

// Type mapping info for `window/showDocument`
inline const RequestInfo<std::shared_ptr<lsproto::ShowDocumentParams>, lsproto::ShowDocumentResponse> WindowShowDocumentInfo{MethodWindowShowDocument};

// Type mapping info for `textDocument/linkedEditingRange`
inline const RequestInfo<std::shared_ptr<lsproto::LinkedEditingRangeParams>, lsproto::LinkedEditingRangeResponse> TextDocumentLinkedEditingRangeInfo{MethodTextDocumentLinkedEditingRange};

// Type mapping info for `workspace/willCreateFiles`
inline const RequestInfo<std::shared_ptr<lsproto::CreateFilesParams>, lsproto::WillCreateFilesResponse> WorkspaceWillCreateFilesInfo{MethodWorkspaceWillCreateFiles};

// Type mapping info for `workspace/willRenameFiles`
inline const RequestInfo<std::shared_ptr<lsproto::RenameFilesParams>, lsproto::WillRenameFilesResponse> WorkspaceWillRenameFilesInfo{MethodWorkspaceWillRenameFiles};

// Type mapping info for `workspace/willDeleteFiles`
inline const RequestInfo<std::shared_ptr<lsproto::DeleteFilesParams>, lsproto::WillDeleteFilesResponse> WorkspaceWillDeleteFilesInfo{MethodWorkspaceWillDeleteFiles};

// Type mapping info for `textDocument/moniker`
inline const RequestInfo<std::shared_ptr<lsproto::MonikerParams>, lsproto::MonikerResponse> TextDocumentMonikerInfo{MethodTextDocumentMoniker};

// Type mapping info for `textDocument/prepareTypeHierarchy`
inline const RequestInfo<std::shared_ptr<lsproto::TypeHierarchyPrepareParams>, lsproto::TypeHierarchyPrepareResponse> TextDocumentPrepareTypeHierarchyInfo{MethodTextDocumentPrepareTypeHierarchy};

// Type mapping info for `typeHierarchy/supertypes`
inline const RequestInfo<std::shared_ptr<lsproto::TypeHierarchySupertypesParams>, lsproto::TypeHierarchySupertypesResponse> TypeHierarchySupertypesInfo{MethodTypeHierarchySupertypes};

// Type mapping info for `typeHierarchy/subtypes`
inline const RequestInfo<std::shared_ptr<lsproto::TypeHierarchySubtypesParams>, lsproto::TypeHierarchySubtypesResponse> TypeHierarchySubtypesInfo{MethodTypeHierarchySubtypes};

// Type mapping info for `textDocument/inlineValue`
inline const RequestInfo<std::shared_ptr<lsproto::InlineValueParams>, lsproto::InlineValueResponse> TextDocumentInlineValueInfo{MethodTextDocumentInlineValue};

// Type mapping info for `workspace/inlineValue/refresh`
inline const RequestInfo<lsproto::NoParams, lsproto::InlineValueRefreshResponse> WorkspaceInlineValueRefreshInfo{MethodWorkspaceInlineValueRefresh};

// Type mapping info for `textDocument/inlayHint`
inline const RequestInfo<std::shared_ptr<lsproto::InlayHintParams>, lsproto::InlayHintResponse> TextDocumentInlayHintInfo{MethodTextDocumentInlayHint};

// Type mapping info for `inlayHint/resolve`
inline const RequestInfo<std::shared_ptr<lsproto::InlayHint>, lsproto::InlayHintResolveResponse> InlayHintResolveInfo{MethodInlayHintResolve};

// Type mapping info for `workspace/inlayHint/refresh`
inline const RequestInfo<lsproto::NoParams, lsproto::InlayHintRefreshResponse> WorkspaceInlayHintRefreshInfo{MethodWorkspaceInlayHintRefresh};

// Type mapping info for `textDocument/diagnostic`
inline const RequestInfo<std::shared_ptr<lsproto::DocumentDiagnosticParams>, lsproto::DocumentDiagnosticResponse> TextDocumentDiagnosticInfo{MethodTextDocumentDiagnostic};

// Type mapping info for `workspace/diagnostic`
inline const RequestInfo<std::shared_ptr<lsproto::WorkspaceDiagnosticParams>, lsproto::WorkspaceDiagnosticResponse> WorkspaceDiagnosticInfo{MethodWorkspaceDiagnostic};

// Type mapping info for `workspace/diagnostic/refresh`
inline const RequestInfo<lsproto::NoParams, lsproto::DiagnosticRefreshResponse> WorkspaceDiagnosticRefreshInfo{MethodWorkspaceDiagnosticRefresh};

// Type mapping info for `textDocument/inlineCompletion`
inline const RequestInfo<std::shared_ptr<lsproto::InlineCompletionParams>, lsproto::InlineCompletionResponse> TextDocumentInlineCompletionInfo{MethodTextDocumentInlineCompletion};

// Type mapping info for `workspace/textDocumentContent`
inline const RequestInfo<std::shared_ptr<lsproto::TextDocumentContentParams>, lsproto::TextDocumentContentResponse> WorkspaceTextDocumentContentInfo{MethodWorkspaceTextDocumentContent};

// Type mapping info for `workspace/textDocumentContent/refresh`
inline const RequestInfo<std::shared_ptr<lsproto::TextDocumentContentRefreshParams>, lsproto::TextDocumentContentRefreshResponse> WorkspaceTextDocumentContentRefreshInfo{MethodWorkspaceTextDocumentContentRefresh};

// Type mapping info for `client/registerCapability`
inline const RequestInfo<std::shared_ptr<lsproto::RegistrationParams>, lsproto::RegistrationResponse> ClientRegisterCapabilityInfo{MethodClientRegisterCapability};

// Type mapping info for `client/unregisterCapability`
inline const RequestInfo<std::shared_ptr<lsproto::UnregistrationParams>, lsproto::UnregistrationResponse> ClientUnregisterCapabilityInfo{MethodClientUnregisterCapability};

// Type mapping info for `initialize`
inline const RequestInfo<std::shared_ptr<lsproto::InitializeParams>, lsproto::InitializeResponse> InitializeInfo{MethodInitialize};

// Type mapping info for `shutdown`
inline const RequestInfo<lsproto::NoParams, lsproto::ShutdownResponse> ShutdownInfo{MethodShutdown};

// Type mapping info for `window/showMessageRequest`
inline const RequestInfo<std::shared_ptr<lsproto::ShowMessageRequestParams>, lsproto::ShowMessageResponse> WindowShowMessageRequestInfo{MethodWindowShowMessageRequest};

// Type mapping info for `textDocument/willSaveWaitUntil`
inline const RequestInfo<std::shared_ptr<lsproto::WillSaveTextDocumentParams>, lsproto::WillSaveTextDocumentWaitUntilResponse> TextDocumentWillSaveWaitUntilInfo{MethodTextDocumentWillSaveWaitUntil};

// Type mapping info for `textDocument/completion`
inline const RequestInfo<std::shared_ptr<lsproto::CompletionParams>, lsproto::CompletionResponse> TextDocumentCompletionInfo{MethodTextDocumentCompletion};

// Type mapping info for `completionItem/resolve`
inline const RequestInfo<std::shared_ptr<lsproto::CompletionItem>, lsproto::CompletionResolveResponse> CompletionItemResolveInfo{MethodCompletionItemResolve};

// Type mapping info for `textDocument/hover`
inline const RequestInfo<std::shared_ptr<lsproto::HoverParams>, lsproto::HoverResponse> TextDocumentHoverInfo{MethodTextDocumentHover};

// Type mapping info for `textDocument/signatureHelp`
inline const RequestInfo<std::shared_ptr<lsproto::SignatureHelpParams>, lsproto::SignatureHelpResponse> TextDocumentSignatureHelpInfo{MethodTextDocumentSignatureHelp};

// Type mapping info for `textDocument/definition`
inline const RequestInfo<std::shared_ptr<lsproto::DefinitionParams>, lsproto::DefinitionResponse> TextDocumentDefinitionInfo{MethodTextDocumentDefinition};

// Type mapping info for `textDocument/references`
inline const RequestInfo<std::shared_ptr<lsproto::ReferenceParams>, lsproto::ReferencesResponse> TextDocumentReferencesInfo{MethodTextDocumentReferences};

// Type mapping info for `textDocument/documentHighlight`
inline const RequestInfo<std::shared_ptr<lsproto::DocumentHighlightParams>, lsproto::DocumentHighlightResponse> TextDocumentDocumentHighlightInfo{MethodTextDocumentDocumentHighlight};

// Type mapping info for `textDocument/documentSymbol`
inline const RequestInfo<std::shared_ptr<lsproto::DocumentSymbolParams>, lsproto::DocumentSymbolResponse> TextDocumentDocumentSymbolInfo{MethodTextDocumentDocumentSymbol};

// Type mapping info for `textDocument/codeAction`
inline const RequestInfo<std::shared_ptr<lsproto::CodeActionParams>, lsproto::CodeActionResponse> TextDocumentCodeActionInfo{MethodTextDocumentCodeAction};

// Type mapping info for `codeAction/resolve`
inline const RequestInfo<std::shared_ptr<lsproto::CodeAction>, lsproto::CodeActionResolveResponse> CodeActionResolveInfo{MethodCodeActionResolve};

// Type mapping info for `workspace/symbol`
inline const RequestInfo<std::shared_ptr<lsproto::WorkspaceSymbolParams>, lsproto::WorkspaceSymbolResponse> WorkspaceSymbolInfo{MethodWorkspaceSymbol};

// Type mapping info for `workspaceSymbol/resolve`
inline const RequestInfo<std::shared_ptr<lsproto::WorkspaceSymbol>, lsproto::WorkspaceSymbolResolveResponse> WorkspaceSymbolResolveInfo{MethodWorkspaceSymbolResolve};

// Type mapping info for `textDocument/codeLens`
inline const RequestInfo<std::shared_ptr<lsproto::CodeLensParams>, lsproto::CodeLensResponse> TextDocumentCodeLensInfo{MethodTextDocumentCodeLens};

// Type mapping info for `codeLens/resolve`
inline const RequestInfo<std::shared_ptr<lsproto::CodeLens>, lsproto::CodeLensResolveResponse> CodeLensResolveInfo{MethodCodeLensResolve};

// Type mapping info for `workspace/codeLens/refresh`
inline const RequestInfo<lsproto::NoParams, lsproto::CodeLensRefreshResponse> WorkspaceCodeLensRefreshInfo{MethodWorkspaceCodeLensRefresh};

// Type mapping info for `textDocument/documentLink`
inline const RequestInfo<std::shared_ptr<lsproto::DocumentLinkParams>, lsproto::DocumentLinkResponse> TextDocumentDocumentLinkInfo{MethodTextDocumentDocumentLink};

// Type mapping info for `documentLink/resolve`
inline const RequestInfo<std::shared_ptr<lsproto::DocumentLink>, lsproto::DocumentLinkResolveResponse> DocumentLinkResolveInfo{MethodDocumentLinkResolve};

// Type mapping info for `textDocument/formatting`
inline const RequestInfo<std::shared_ptr<lsproto::DocumentFormattingParams>, lsproto::DocumentFormattingResponse> TextDocumentFormattingInfo{MethodTextDocumentFormatting};

// Type mapping info for `textDocument/rangeFormatting`
inline const RequestInfo<std::shared_ptr<lsproto::DocumentRangeFormattingParams>, lsproto::DocumentRangeFormattingResponse> TextDocumentRangeFormattingInfo{MethodTextDocumentRangeFormatting};

// Type mapping info for `textDocument/rangesFormatting`
inline const RequestInfo<std::shared_ptr<lsproto::DocumentRangesFormattingParams>, lsproto::DocumentRangesFormattingResponse> TextDocumentRangesFormattingInfo{MethodTextDocumentRangesFormatting};

// Type mapping info for `textDocument/onTypeFormatting`
inline const RequestInfo<std::shared_ptr<lsproto::DocumentOnTypeFormattingParams>, lsproto::DocumentOnTypeFormattingResponse> TextDocumentOnTypeFormattingInfo{MethodTextDocumentOnTypeFormatting};

// Type mapping info for `textDocument/rename`
inline const RequestInfo<std::shared_ptr<lsproto::RenameParams>, lsproto::RenameResponse> TextDocumentRenameInfo{MethodTextDocumentRename};

// Type mapping info for `textDocument/prepareRename`
inline const RequestInfo<std::shared_ptr<lsproto::PrepareRenameParams>, lsproto::PrepareRenameResponse> TextDocumentPrepareRenameInfo{MethodTextDocumentPrepareRename};

// Type mapping info for `workspace/executeCommand`
inline const RequestInfo<std::shared_ptr<lsproto::ExecuteCommandParams>, lsproto::ExecuteCommandResponse> WorkspaceExecuteCommandInfo{MethodWorkspaceExecuteCommand};

// Type mapping info for `workspace/applyEdit`
inline const RequestInfo<std::shared_ptr<lsproto::ApplyWorkspaceEditParams>, lsproto::ApplyWorkspaceEditResponse> WorkspaceApplyEditInfo{MethodWorkspaceApplyEdit};

// Type mapping info for `custom/runGC`
inline const RequestInfo<lsproto::NoParams, lsproto::RunGCResponse> CustomRunGCInfo{MethodCustomRunGC};

// Type mapping info for `custom/saveHeapProfile`
inline const RequestInfo<std::shared_ptr<lsproto::ProfileParams>, lsproto::SaveHeapProfileResponse> CustomSaveHeapProfileInfo{MethodCustomSaveHeapProfile};

// Type mapping info for `custom/saveAllocProfile`
inline const RequestInfo<std::shared_ptr<lsproto::ProfileParams>, lsproto::SaveAllocProfileResponse> CustomSaveAllocProfileInfo{MethodCustomSaveAllocProfile};

// Type mapping info for `custom/startCPUProfile`
inline const RequestInfo<std::shared_ptr<lsproto::ProfileParams>, lsproto::StartCPUProfileResponse> CustomStartCPUProfileInfo{MethodCustomStartCPUProfile};

// Type mapping info for `custom/stopCPUProfile`
inline const RequestInfo<lsproto::NoParams, lsproto::StopCPUProfileResponse> CustomStopCPUProfileInfo{MethodCustomStopCPUProfile};

// Type mapping info for `custom/initializeAPISession`
inline const RequestInfo<std::shared_ptr<lsproto::InitializeAPISessionParams>, lsproto::CustomInitializeAPISessionResponse> CustomInitializeAPISessionInfo{MethodCustomInitializeAPISession};

// Type mapping info for `custom/projectInfo`
inline const RequestInfo<std::shared_ptr<lsproto::ProjectInfoParams>, lsproto::CustomProjectInfoResponse> CustomProjectInfoInfo{MethodCustomProjectInfo};

// Type mapping info for `custom/setContentMapperContributions`
inline const RequestInfo<std::shared_ptr<lsproto::SetContentMapperContributionsParams>, lsproto::CustomSetContentMapperContributionsResponse> CustomSetContentMapperContributionsInfo{MethodCustomSetContentMapperContributions};

// Type mapping info for `custom/textDocument/sourceDefinition`
inline const RequestInfo<std::shared_ptr<lsproto::TextDocumentPositionParams>, lsproto::CustomTextDocumentSourceDefinitionResponse> CustomTextDocumentSourceDefinitionInfo{MethodCustomTextDocumentSourceDefinition};

// Type mapping info for `custom/textDocument/multiDocumentHighlight`
inline const RequestInfo<std::shared_ptr<lsproto::MultiDocumentHighlightParams>, lsproto::CustomMultiDocumentHighlightResponse> CustomTextDocumentMultiDocumentHighlightInfo{MethodCustomTextDocumentMultiDocumentHighlight};

// Type mapping info for `textDocument/_vs_onAutoInsert`
inline const RequestInfo<std::shared_ptr<lsproto::VSOnAutoInsertParams>, lsproto::VSOnAutoInsertResponse> TextDocumentVSOnAutoInsertInfo{MethodTextDocumentVSOnAutoInsert};

// Type mapping info for `textDocument/_vs_references`
inline const RequestInfo<std::shared_ptr<lsproto::ReferenceParams>, lsproto::VSReferencesResponse> TextDocumentVSReferencesInfo{MethodTextDocumentVSReferences};

// Type mapping info for `workspace/didChangeWorkspaceFolders`
inline const NotificationInfo<std::shared_ptr<lsproto::DidChangeWorkspaceFoldersParams>> WorkspaceDidChangeWorkspaceFoldersInfo{MethodWorkspaceDidChangeWorkspaceFolders};

// Type mapping info for `window/workDoneProgress/cancel`
inline const NotificationInfo<std::shared_ptr<lsproto::WorkDoneProgressCancelParams>> WindowWorkDoneProgressCancelInfo{MethodWindowWorkDoneProgressCancel};

// Type mapping info for `workspace/didCreateFiles`
inline const NotificationInfo<std::shared_ptr<lsproto::CreateFilesParams>> WorkspaceDidCreateFilesInfo{MethodWorkspaceDidCreateFiles};

// Type mapping info for `workspace/didRenameFiles`
inline const NotificationInfo<std::shared_ptr<lsproto::RenameFilesParams>> WorkspaceDidRenameFilesInfo{MethodWorkspaceDidRenameFiles};

// Type mapping info for `workspace/didDeleteFiles`
inline const NotificationInfo<std::shared_ptr<lsproto::DeleteFilesParams>> WorkspaceDidDeleteFilesInfo{MethodWorkspaceDidDeleteFiles};

// Type mapping info for `initialized`
inline const NotificationInfo<std::shared_ptr<lsproto::InitializedParams>> InitializedInfo{MethodInitialized};

// Type mapping info for `exit`
inline const NotificationInfo<lsproto::NoParams> ExitInfo{MethodExit};

// Type mapping info for `workspace/didChangeConfiguration`
inline const NotificationInfo<std::shared_ptr<lsproto::DidChangeConfigurationParams>> WorkspaceDidChangeConfigurationInfo{MethodWorkspaceDidChangeConfiguration};

// Type mapping info for `window/showMessage`
inline const NotificationInfo<std::shared_ptr<lsproto::ShowMessageParams>> WindowShowMessageInfo{MethodWindowShowMessage};

// Type mapping info for `window/logMessage`
inline const NotificationInfo<std::shared_ptr<lsproto::LogMessageParams>> WindowLogMessageInfo{MethodWindowLogMessage};

// Type mapping info for `telemetry/event`
inline const NotificationInfo<lsproto::TelemetryEvent> TelemetryEventInfo{MethodTelemetryEvent};

// Type mapping info for `textDocument/didOpen`
inline const NotificationInfo<std::shared_ptr<lsproto::DidOpenTextDocumentParams>> TextDocumentDidOpenInfo{MethodTextDocumentDidOpen};

// Type mapping info for `textDocument/didChange`
inline const NotificationInfo<std::shared_ptr<lsproto::DidChangeTextDocumentParams>> TextDocumentDidChangeInfo{MethodTextDocumentDidChange};

// Type mapping info for `textDocument/didClose`
inline const NotificationInfo<std::shared_ptr<lsproto::DidCloseTextDocumentParams>> TextDocumentDidCloseInfo{MethodTextDocumentDidClose};

// Type mapping info for `textDocument/didSave`
inline const NotificationInfo<std::shared_ptr<lsproto::DidSaveTextDocumentParams>> TextDocumentDidSaveInfo{MethodTextDocumentDidSave};

// Type mapping info for `textDocument/willSave`
inline const NotificationInfo<std::shared_ptr<lsproto::WillSaveTextDocumentParams>> TextDocumentWillSaveInfo{MethodTextDocumentWillSave};

// Type mapping info for `workspace/didChangeWatchedFiles`
inline const NotificationInfo<std::shared_ptr<lsproto::DidChangeWatchedFilesParams>> WorkspaceDidChangeWatchedFilesInfo{MethodWorkspaceDidChangeWatchedFiles};

// Type mapping info for `textDocument/publishDiagnostics`
inline const NotificationInfo<std::shared_ptr<lsproto::PublishDiagnosticsParams>> TextDocumentPublishDiagnosticsInfo{MethodTextDocumentPublishDiagnostics};

// Type mapping info for `$/setTrace`
inline const NotificationInfo<std::shared_ptr<lsproto::SetTraceParams>> SetTraceInfo{MethodSetTrace};

// Type mapping info for `$/logTrace`
inline const NotificationInfo<std::shared_ptr<lsproto::LogTraceParams>> LogTraceInfo{MethodLogTrace};

// Type mapping info for `$/cancelRequest`
inline const NotificationInfo<std::shared_ptr<lsproto::CancelParams>> CancelRequestInfo{MethodCancelRequest};

// Type mapping info for `$/progress`
inline const NotificationInfo<std::shared_ptr<lsproto::ProgressParams>> ProgressInfo{MethodProgress};

// Type mapping info for `custom/setLogVerbosity`
inline const NotificationInfo<std::shared_ptr<lsproto::SetLogVerbosityParams>> CustomSetLogVerbosityInfo{MethodCustomSetLogVerbosity};

} // namespace tsc::lsp::lsproto
