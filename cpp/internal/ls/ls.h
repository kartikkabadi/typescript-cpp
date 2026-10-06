// === slice: ls-coreC ===
// ls — the navigation/features surface of the language service:
// hover.go, callhierarchy.go, inlay_hints.go, documenthighlights.go,
// sourcedefinition.go, symbols.go, folding.go, semantictokens.go,
// definition.go, rename.go, selectionranges.go, format.go, codelens.go.
//
// Also declares (as dep decls/stubs) the shared ls surface this slice needs:
// the LanguageService struct itself (languageservice.go — shared file, its
// trivial accessors are ported for real; the rest is stubbed), the lsproto
// protocol types (owned by the lsp slice), extra lsconv.Converters methods
// (owned by the lsp slice — extend the stub in ls/change/change.h), the
// findallreferences.go data types (owned by ls-coreB), CrossProjectOrchestrator
// (crossproject.go — owned by ls-coreB), and utilities.go free helpers
// (owned by ls-coreA).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/astnav/tokens.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/core/text.h"
#include "internal/core/textchange.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/gostd/gostd.h"
#include "internal/ls/change/change.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/printer/printer.h"
#include "internal/scanner/scanner.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/spanmap/spanmap.h"
#include "internal/tspath/tspath.h"

namespace tsc::vfs::vfsmatch {
struct SpecMatcher;
}
namespace tsc::format {
struct FormatRequestContext;
}
namespace tsc::nodebuilder {
struct NodeBuilder;
}

namespace tsc {
// === dep decls — owned by the ast slice (tsc/internal/ast/utilities.go) ===
// utilities.go:2240 — SemanticMeaning
using SemanticMeaning = int32_t;
inline constexpr SemanticMeaning SemanticMeaningNone = 0;
inline constexpr SemanticMeaning SemanticMeaningValue = 1 << 0;
inline constexpr SemanticMeaning SemanticMeaningType = 1 << 1;
inline constexpr SemanticMeaning SemanticMeaningNamespace = 1 << 2;
inline constexpr SemanticMeaning SemanticMeaningAll =
	SemanticMeaningValue | SemanticMeaningType | SemanticMeaningNamespace;
// === end dep decls ===
} // namespace tsc

namespace tsc::autoimport {
// === dep decls — owned by the autoimport slice ===
// registry.go:32 — ProjectID is a fmt.Stringer interface; the port models it
// as a string (the only ProjectID implementations are string-like).
using ProjectID = std::string;
struct Registry;
struct View;
// === end dep decls ===
} // namespace tsc::autoimport

// ============================================================================
// dep decls — tsc::lsp::lsproto (owned by the lsp slice)
// Extends the minimal Position/Range/TextEdit/FormattingOptions decls already
// in lsutil.h with the protocol types this slice needs.
// ============================================================================
namespace tsc::lsp::lsproto {

// DocumentUri / Location / LocationLink are declared in lsutil.h's shared
// dep block (needed by lsconv.Converters in change.h).

// --- enums / literal types (lsp_generated.go) ---
using MarkupKind = std::string;
inline const MarkupKind MarkupKindPlainText{"plaintext"};
inline const MarkupKind MarkupKindMarkdown{"markdown"};

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

using SymbolTag = uint32_t;
inline constexpr SymbolTag SymbolTagDeprecated = 1;

using DocumentHighlightKind = uint32_t;
inline constexpr DocumentHighlightKind DocumentHighlightKindText = 1;
inline constexpr DocumentHighlightKind DocumentHighlightKindRead = 2;
inline constexpr DocumentHighlightKind DocumentHighlightKindWrite = 3;

using FoldingRangeKind = std::string;
inline const FoldingRangeKind FoldingRangeKindComment{"comment"};
inline const FoldingRangeKind FoldingRangeKindImports{"imports"};
inline const FoldingRangeKind FoldingRangeKindRegion{"region"};

using InlayHintKind = uint32_t;
inline constexpr InlayHintKind InlayHintKindType = 1;
inline constexpr InlayHintKind InlayHintKindParameter = 2;

using ResourceOperationKind = std::string;
inline const ResourceOperationKind ResourceOperationKindCreate{"create"};
inline const ResourceOperationKind ResourceOperationKindRename{"rename"};
inline const ResourceOperationKind ResourceOperationKindDelete{"delete"};

using FailureHandlingKind = std::string;

using CodeLensKind = std::string;
inline const CodeLensKind CodeLensKindReferences{"references"};
inline const CodeLensKind CodeLensKindImplementations{"implementations"};

using PositionEncodingKind = std::string;
using TokenFormat = std::string;
inline const TokenFormat TokenFormatRelative{"relative"};

using SemanticTokenType = std::string;
inline const SemanticTokenType SemanticTokenTypeNamespace{"namespace"};
inline const SemanticTokenType SemanticTokenTypeType{"type"};
inline const SemanticTokenType SemanticTokenTypeClass{"class"};
inline const SemanticTokenType SemanticTokenTypeEnum{"enum"};
inline const SemanticTokenType SemanticTokenTypeInterface{"interface"};
inline const SemanticTokenType SemanticTokenTypeStruct{"struct"};
inline const SemanticTokenType SemanticTokenTypeTypeParameter{"typeParameter"};
inline const SemanticTokenType SemanticTokenTypeParameter{"parameter"};
inline const SemanticTokenType SemanticTokenTypeVariable{"variable"};
inline const SemanticTokenType SemanticTokenTypeProperty{"property"};
inline const SemanticTokenType SemanticTokenTypeEnumMember{"enumMember"};
inline const SemanticTokenType SemanticTokenTypeEvent{"event"};
inline const SemanticTokenType SemanticTokenTypeFunction{"function"};
inline const SemanticTokenType SemanticTokenTypeMethod{"method"};
inline const SemanticTokenType SemanticTokenTypeMacro{"macro"};
inline const SemanticTokenType SemanticTokenTypeKeyword{"keyword"};
inline const SemanticTokenType SemanticTokenTypeModifier{"modifier"};
inline const SemanticTokenType SemanticTokenTypeComment{"comment"};
inline const SemanticTokenType SemanticTokenTypeString{"string"};
inline const SemanticTokenType SemanticTokenTypeNumber{"number"};
inline const SemanticTokenType SemanticTokenTypeRegexp{"regexp"};
inline const SemanticTokenType SemanticTokenTypeOperator{"operator"};
inline const SemanticTokenType SemanticTokenTypeDecorator{"decorator"};
inline const SemanticTokenType SemanticTokenTypeLabel{"label"};

using SemanticTokenModifier = std::string;
inline const SemanticTokenModifier SemanticTokenModifierDeclaration{"declaration"};
inline const SemanticTokenModifier SemanticTokenModifierDefinition{"definition"};
inline const SemanticTokenModifier SemanticTokenModifierReadonly{"readonly"};
inline const SemanticTokenModifier SemanticTokenModifierStatic{"static"};
inline const SemanticTokenModifier SemanticTokenModifierDeprecated{"deprecated"};
inline const SemanticTokenModifier SemanticTokenModifierAbstract{"abstract"};
inline const SemanticTokenModifier SemanticTokenModifierAsync{"async"};
inline const SemanticTokenModifier SemanticTokenModifierModification{"modification"};
inline const SemanticTokenModifier SemanticTokenModifierDocumentation{"documentation"};
inline const SemanticTokenModifier SemanticTokenModifierDefaultLibrary{"defaultLibrary"};

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

using VSContainerElementStyle = int32_t;
inline constexpr VSContainerElementStyle VSContainerElementStyleWrapped = 0;
inline constexpr VSContainerElementStyle VSContainerElementStyleStacked = 1;

// --- lsp_generated.go union/struct types (fields-only) ---

struct IntegerOrString {
	int32_t* Integer = nullptr;
	std::string* String = nullptr;
};

struct IntegerOrNull {
	int32_t* Integer = nullptr;
};

struct UintegerOrNull {
	uint32_t* Uinteger = nullptr;
};

struct TextDocumentIdentifier {
	DocumentUri Uri;
};

struct OptionalVersionedTextDocumentIdentifier {
	DocumentUri Uri;
	IntegerOrNull Version;
};

struct MarkupContent {
	MarkupKind Kind;
	std::string Value;
};

struct MarkedStringWithLanguage {
	std::string Language;
	std::string Value;
};

struct StringOrMarkedStringWithLanguage {
	std::string* String = nullptr;
	MarkedStringWithLanguage* MarkedStringWithLanguage = nullptr;
};

struct MarkupContentOrStringOrMarkedStringWithLanguageOrMarkedStrings {
	MarkupContent* MarkupContent = nullptr;
	std::string* String = nullptr;
	MarkedStringWithLanguage* MarkedStringWithLanguage = nullptr;
	std::vector<StringOrMarkedStringWithLanguage>* MarkedStrings = nullptr;
};

// --- VS adornment model (lsp_generated.go) ---
// StringLiteral* marker types marshal a fixed discriminator string.
struct StringLiteralClassifiedTextRun {};
struct StringLiteralClassifiedTextElement {};
struct StringLiteralImageElement {};
struct StringLiteralImageId {};
struct StringLiteralContainerElement {};
struct StringLiteralRename {};
struct StringLiteralCreate {};
struct StringLiteralDelete {};

struct VSClassifiedTextRun {
	ClassificationTypeName ClassificationTypeName;
	std::string Text;
	std::string* MarkerTagType = nullptr;
	int32_t Style = 0;
	StringLiteralClassifiedTextRun VSType;
};

struct VSImageId {
	std::string Guid;
	int32_t Id = 0;
	StringLiteralImageId VSType;
};

struct VSImageElement {
	VSImageId* ImageId = nullptr;
	StringLiteralImageElement VSType;
};

struct VSImageElementOrClassifiedTextElementOrContainerElement;

struct VSContainerElement {
	VSContainerElementStyle Style = VSContainerElementStyleWrapped;
	std::vector<VSImageElementOrClassifiedTextElementOrContainerElement> Elements;
	StringLiteralContainerElement VSType;
};

struct VSClassifiedTextElement;

struct VSImageElementOrClassifiedTextElementOrContainerElement {
	VSImageElement* ImageElement = nullptr;
	VSClassifiedTextElement* ClassifiedTextElement = nullptr;
	VSContainerElement* ContainerElement = nullptr;
};

struct VSClassifiedTextElement {
	std::vector<VSClassifiedTextRun*> Runs;
	StringLiteralClassifiedTextElement VSType;
};

struct Hover {
	MarkupContentOrStringOrMarkedStringWithLanguageOrMarkedStrings Contents;
	Range* Range = nullptr;
	bool CanIncreaseVerbosity = false;
	VSContainerElement* VSRawContent = nullptr;
};

struct HoverParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	IntegerOrString* WorkDoneToken = nullptr;
	int32_t* VerbosityLevel = nullptr;
};

struct DocumentSymbol {
	std::string Name;
	std::string* Detail = nullptr;
	SymbolKind Kind = 0;
	std::vector<SymbolTag>* Tags = nullptr;
	bool* Deprecated = nullptr;
	lsproto::Range Range;
	lsproto::Range SelectionRange;
	std::vector<DocumentSymbol*>* Children = nullptr;
};

struct SymbolInformation {
	std::string Name;
	SymbolKind Kind = 0;
	std::vector<SymbolTag>* Tags = nullptr;
	std::string* ContainerName = nullptr;
	bool* Deprecated = nullptr;
	Location Location;
};

struct DocumentHighlight {
	Range Range;
	DocumentHighlightKind* Kind = nullptr;
};

struct FoldingRange {
	uint32_t StartLine = 0;
	uint32_t* StartCharacter = nullptr;
	uint32_t EndLine = 0;
	uint32_t* EndCharacter = nullptr;
	FoldingRangeKind* Kind = nullptr;
	std::string* CollapsedText = nullptr;
};

struct SemanticTokens {
	std::string* ResultId = nullptr;
	std::vector<uint32_t> Data;
};

struct SemanticTokensLegend {
	std::vector<std::string> TokenTypes;
	std::vector<std::string> TokenModifiers;
};

struct Command;

struct InlayHintLabelPart {
	std::string Value;
	struct StringOrMarkupContent* Tooltip = nullptr;
	Location* Location = nullptr;
	Command* Command = nullptr;
};

struct StringOrInlayHintLabelParts {
	std::string* String = nullptr;
	std::vector<InlayHintLabelPart*>* InlayHintLabelParts = nullptr;
};

struct StringOrMarkupContent {
	std::string* String = nullptr;
	MarkupContent* MarkupContent = nullptr;
};

struct InlayHintData {};

struct InlayHint {
	Position Position;
	StringOrInlayHintLabelParts Label;
	InlayHintKind* Kind = nullptr;
	std::vector<TextEdit*>* TextEdits = nullptr;
	StringOrMarkupContent* Tooltip = nullptr;
	bool* PaddingLeft = nullptr;
	bool* PaddingRight = nullptr;
	InlayHintData* Data = nullptr;
};

struct InlayHintParams {
	IntegerOrString* WorkDoneToken = nullptr;
	TextDocumentIdentifier TextDocument;
	Range Range;
};

struct CallHierarchyItemData {};

struct CallHierarchyItem {
	std::string Name;
	SymbolKind Kind = 0;
	std::vector<SymbolTag>* Tags = nullptr;
	std::string* Detail = nullptr;
	DocumentUri Uri;
	lsproto::Range Range;
	lsproto::Range SelectionRange;
	CallHierarchyItemData* Data = nullptr;
};

struct CallHierarchyIncomingCall {
	CallHierarchyItem* From = nullptr;
	std::vector<Range> FromRanges;
};

struct CallHierarchyOutgoingCall {
	CallHierarchyItem* To = nullptr;
	std::vector<Range> FromRanges;
};

struct Command {
	std::string Title;
	std::string* Tooltip = nullptr;
	std::string Command;
	std::vector<std::any>* Arguments = nullptr;
};

struct CodeLensData {
	CodeLensKind Kind;
	DocumentUri Uri;
	int32_t Position = 0;
	int32_t* SupplementalFileIndex = nullptr;
};

struct CodeLens {
	Range Range;
	Command* Command = nullptr;
	CodeLensData* Data = nullptr;
};

struct RenameParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	IntegerOrString* WorkDoneToken = nullptr;
	std::string NewName;
};

struct RenameFileOptions {
	bool* Overwrite = nullptr;
	bool* IgnoreIfExists = nullptr;
};

struct RenameFile {
	StringLiteralRename Kind;
	std::string* AnnotationId = nullptr;
	DocumentUri OldUri;
	DocumentUri NewUri;
	RenameFileOptions* Options = nullptr;
};

struct CreateFileOptions {
	bool* Overwrite = nullptr;
	bool* IgnoreIfExists = nullptr;
};

struct CreateFile {
	StringLiteralCreate Kind;
	std::string* AnnotationId = nullptr;
	DocumentUri Uri;
	CreateFileOptions* Options = nullptr;
};

struct DeleteFileOptions {
	bool* Recursive = nullptr;
	bool* IgnoreIfNotExists = nullptr;
};

struct DeleteFile {
	StringLiteralDelete Kind;
	std::string* AnnotationId = nullptr;
	DocumentUri Uri;
	DeleteFileOptions* Options = nullptr;
};

struct AnnotatedTextEdit {
	Range Range;
	std::string NewText;
	std::string AnnotationId;
};

struct SnippetTextEdit {
	Range Range;
	std::string NewText;
	std::string* AnnotationId = nullptr;
};

struct TextEditOrAnnotatedTextEditOrSnippetTextEdit {
	TextEdit* TextEdit = nullptr;
	AnnotatedTextEdit* AnnotatedTextEdit = nullptr;
	SnippetTextEdit* SnippetTextEdit = nullptr;
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

struct ChangeAnnotation {
	std::string Label;
	bool* NeedsConfirmation = nullptr;
	std::string* Description = nullptr;
};

struct WorkspaceEdit {
	std::map<DocumentUri, std::vector<TextEdit*>>* Changes = nullptr;
	std::vector<TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile>* DocumentChanges = nullptr;
	std::map<std::string, ChangeAnnotation*>* ChangeAnnotations = nullptr;
};

struct SelectionRange {
	Range Range;
	SelectionRange* Parent = nullptr;
};

struct SelectionRangeParams {
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	TextDocumentIdentifier TextDocument;
	std::vector<Position> Positions;
};

struct ReferenceContext {
	bool IncludeDeclaration = false;
};

struct ReferenceParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	ReferenceContext* Context = nullptr;
};

struct ImplementationParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
};

struct TypeDefinitionParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
};

struct TextDocumentPositionParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
};

struct DocumentSymbolParams {
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	TextDocumentIdentifier TextDocument;
};

struct FoldingRangeParams {
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	TextDocumentIdentifier TextDocument;
};

struct SemanticTokensParams {
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	TextDocumentIdentifier TextDocument;
};

struct SemanticTokensRangeParams {
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	TextDocumentIdentifier TextDocument;
	Range Range;
};

struct DocumentFormattingParams {
	IntegerOrString* WorkDoneToken = nullptr;
	TextDocumentIdentifier TextDocument;
	FormattingOptions* Options = nullptr;
};

struct DocumentRangeFormattingParams {
	IntegerOrString* WorkDoneToken = nullptr;
	TextDocumentIdentifier TextDocument;
	Range Range;
	FormattingOptions* Options = nullptr;
};

struct DocumentOnTypeFormattingParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	std::string Ch;
	FormattingOptions* Options = nullptr;
};

struct CodeLensParams {
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	TextDocumentIdentifier TextDocument;
};

struct CallHierarchyPrepareParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	IntegerOrString* WorkDoneToken = nullptr;
};

struct CallHierarchyIncomingCallsParams {
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	CallHierarchyItem* Item = nullptr;
};

struct CallHierarchyOutgoingCallsParams {
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	CallHierarchyItem* Item = nullptr;
};

struct WorkspaceSymbolParams {
	IntegerOrString* WorkDoneToken = nullptr;
	IntegerOrString* PartialResultToken = nullptr;
	std::string Query;
	TextDocumentIdentifier* TextDocument = nullptr;
};

struct PrepareRenameParams {
	TextDocumentIdentifier TextDocument;
	Position Position;
	IntegerOrString* WorkDoneToken = nullptr;
};

struct MultiDocumentHighlight {
	DocumentUri Uri;
	std::vector<DocumentHighlight*> Highlights;
};

struct WorkspaceSymbol {
	// fields not needed by this slice
};

// --- resolved client capabilities (fields used by this slice only) ---
struct ResolvedWorkspaceEditClientCapabilities {
	bool DocumentChanges = false;
	std::vector<ResourceOperationKind> ResourceOperations;
	FailureHandlingKind FailureHandling;
	bool NormalizesLineEndings = false;
	bool MetadataSupport = false;
	bool SnippetEditSupport = false;
};

struct ResolvedFileOperationClientCapabilities {
	bool DynamicRegistration = false;
	bool DidCreate = false;
	bool WillCreate = false;
	bool DidRename = false;
	bool WillRename = false;
	bool DidDelete = false;
	bool WillDelete = false;
};

struct ResolvedWorkspaceClientCapabilities {
	bool ApplyEdit = false;
	ResolvedWorkspaceEditClientCapabilities WorkspaceEdit;
	ResolvedFileOperationClientCapabilities FileOperations;
};

struct ResolvedHoverClientCapabilities {
	bool DynamicRegistration = false;
	std::vector<MarkupKind> ContentFormat;
};

struct ResolvedDefinitionClientCapabilities {
	bool DynamicRegistration = false;
	bool LinkSupport = false;
};

struct ResolvedTypeDefinitionClientCapabilities {
	bool DynamicRegistration = false;
	bool LinkSupport = false;
};

struct ResolvedImplementationClientCapabilities {
	bool DynamicRegistration = false;
	bool LinkSupport = false;
};

struct ResolvedDocumentSymbolClientCapabilities {
	bool DynamicRegistration = false;
	bool HierarchicalDocumentSymbolSupport = false;
};

struct ResolvedClientFoldingRangeOptions {
	bool CollapsedText = false;
};

struct ResolvedFoldingRangeClientCapabilities {
	bool DynamicRegistration = false;
	uint32_t RangeLimit = 0;
	bool LineFoldingOnly = false;
	ResolvedClientFoldingRangeOptions FoldingRange;
};

struct ResolvedClientSemanticTokensRequestOptions {
	bool Range = false;
	bool Full = false;
};

struct ResolvedSemanticTokensClientCapabilities {
	bool DynamicRegistration = false;
	ResolvedClientSemanticTokensRequestOptions Requests;
	std::vector<std::string> TokenTypes;
	std::vector<std::string> TokenModifiers;
	std::vector<TokenFormat> Formats;
	bool OverlappingTokenSupport = false;
	bool MultilineTokenSupport = false;
	bool ServerCancelSupport = false;
	bool AugmentsSyntaxTokens = false;
};

struct ResolvedTextDocumentClientCapabilities {
	ResolvedHoverClientCapabilities Hover;
	ResolvedDefinitionClientCapabilities Definition;
	ResolvedTypeDefinitionClientCapabilities TypeDefinition;
	ResolvedImplementationClientCapabilities Implementation;
	ResolvedDocumentSymbolClientCapabilities DocumentSymbol;
	ResolvedFoldingRangeClientCapabilities FoldingRange;
	ResolvedSemanticTokensClientCapabilities SemanticTokens;
};

struct ResolvedExperimentalClientCapabilities {
	bool HoverVerbosityLevel = false;
};

struct ResolvedClientCapabilities {
	ResolvedWorkspaceClientCapabilities Workspace;
	ResolvedTextDocumentClientCapabilities TextDocument;
	ResolvedExperimentalClientCapabilities Experimental;
	bool VSSupportsVisualStudioExtensions = false;
};

// lsp.go:293 — GetClientCapabilities. gostd::Context cannot carry values,
// so this port always takes the Go code's empty-caps path.
const ResolvedClientCapabilities* GetClientCapabilities(gostd::Context ctx);

// lsp.go:302 — PreferredMarkupKind.
inline MarkupKind PreferredMarkupKind(const std::vector<MarkupKind>& formats) {
	if (!formats.empty()) {
		return formats[0];
	}
	return MarkupKindPlainText;
}

// --- response union types ---
struct WorkspaceEditOrNull {
	WorkspaceEdit* WorkspaceEdit = nullptr;
};

struct LocationOrLocationsOrDefinitionLinksOrNull {
	lsproto::Location* Location = nullptr;
	std::vector<lsproto::Location>* Locations = nullptr;
	std::vector<lsproto::LocationLink*>* DefinitionLinks = nullptr;
};

// lsp_generated.go:14253
struct LocationsOrNull {
	std::vector<Location>* Locations = nullptr;
};

// lsp_generated.go:10625 — _vs_kind entry.
using VSReferenceKind = int32_t;
inline constexpr VSReferenceKind VSReferenceKindInactive = 0;
inline constexpr VSReferenceKind VSReferenceKindComment = 1;
inline constexpr VSReferenceKind VSReferenceKindString = 2;
inline constexpr VSReferenceKind VSReferenceKindRead = 3;
inline constexpr VSReferenceKind VSReferenceKindWrite = 4;

// lsp_generated.go:8928 — VS-code private reference item.
struct VSReferenceItem {
	int32_t VSId;
	int32_t* VSDefinitionId = nullptr;
	std::vector<VSReferenceKind>* VSKind = nullptr;
	Location VSLocation;
	VSClassifiedTextElement* VSDefinitionText = nullptr;
	std::string* VSProjectName = nullptr;
	std::string* VSContainingType = nullptr;
};

// lsp_generated.go:14635
struct VSReferenceItemsOrNull {
	std::vector<VSReferenceItem*>* VSReferenceItems = nullptr;
};

struct DocumentHighlightsOrNull {
	std::vector<DocumentHighlight*>* DocumentHighlights = nullptr;
};

struct MultiDocumentHighlightsOrNull {
	std::vector<MultiDocumentHighlight*>* MultiDocumentHighlights = nullptr;
};

struct SymbolInformationsOrDocumentSymbolsOrNull {
	std::vector<SymbolInformation*>* SymbolInformations = nullptr;
	std::vector<DocumentSymbol*>* DocumentSymbols = nullptr;
};

struct SymbolInformationsOrWorkspaceSymbolsOrNull {
	std::vector<SymbolInformation*>* SymbolInformations = nullptr;
	std::vector<WorkspaceSymbol*>* WorkspaceSymbols = nullptr;
};

struct FoldingRangesOrNull {
	std::vector<FoldingRange*>* FoldingRanges = nullptr;
};

struct SelectionRangesOrNull {
	std::vector<SelectionRange*>* SelectionRanges = nullptr;
};

struct CallHierarchyItemsOrNull {
	std::vector<CallHierarchyItem*>* CallHierarchyItems = nullptr;
};

struct CallHierarchyIncomingCallsOrNull {
	std::vector<CallHierarchyIncomingCall*>* CallHierarchyIncomingCalls = nullptr;
};

struct CallHierarchyOutgoingCallsOrNull {
	std::vector<CallHierarchyOutgoingCall*>* CallHierarchyOutgoingCalls = nullptr;
};

struct SemanticTokensOrNull {
	SemanticTokens* SemanticTokens = nullptr;
};

struct InlayHintsOrNull {
	std::vector<InlayHint*>* InlayHints = nullptr;
};

struct TextEditsOrNull {
	std::vector<TextEdit*>* TextEdits = nullptr;
};

struct CodeLensesOrNull {
	std::vector<CodeLens*>* CodeLenses = nullptr;
};

struct HoverOrNull {
	Hover* Hover = nullptr;
};

// Go response aliases — `type XResponse = XUnion`.
using HoverResponse = HoverOrNull;
using DefinitionResponse = LocationOrLocationsOrDefinitionLinksOrNull;
using TypeDefinitionResponse = LocationOrLocationsOrDefinitionLinksOrNull;
using CodeLensResponse = CodeLensesOrNull;
using DocumentSymbolResponse = SymbolInformationsOrDocumentSymbolsOrNull;
using FoldingRangeResponse = FoldingRangesOrNull;
using SemanticTokensResponse = SemanticTokensOrNull;
using SemanticTokensRangeResponse = SemanticTokensOrNull;
using InlayHintResponse = InlayHintsOrNull;
using CallHierarchyPrepareResponse = CallHierarchyItemsOrNull;
using CallHierarchyIncomingCallsResponse = CallHierarchyIncomingCallsOrNull;
using CallHierarchyOutgoingCallsResponse = CallHierarchyOutgoingCallsOrNull;
using SelectionRangeResponse = SelectionRangesOrNull;
using DocumentHighlightResponse = DocumentHighlightsOrNull;
using CustomMultiDocumentHighlightResponse = MultiDocumentHighlightsOrNull;
using DocumentFormattingResponse = TextEditsOrNull;
using DocumentRangeFormattingResponse = TextEditsOrNull;
using DocumentOnTypeFormattingResponse = TextEditsOrNull;
using WorkspaceSymbolResponse = SymbolInformationsOrWorkspaceSymbolsOrNull;
using ReferencesResponse = LocationsOrNull;
using ImplementationResponse = LocationOrLocationsOrDefinitionLinksOrNull;
using VSReferencesResponse = VSReferenceItemsOrNull;

// lsp.go:70 — HasTextDocumentPosition / HasLocations / HasLocation are Go
// interfaces satisfied via C++ member functions of the same names.
struct HasTextDocumentPosition {
	virtual ~HasTextDocumentPosition() = default;
	virtual DocumentUri TextDocumentURI() const = 0;
	virtual Position TextDocumentPosition() const = 0;
};

// === end dep decls ===

} // namespace tsc::lsp::lsproto

namespace std {
template <>
struct hash<tsc::lsp::lsproto::DocumentUri> : hash<std::string> {};
template <>
struct hash<tsc::lsp::lsproto::Position> {
	size_t operator()(const tsc::lsp::lsproto::Position& p) const {
		return hash<uint32_t>{}(p.Line) * 31u + hash<uint32_t>{}(p.Character);
	}
};
template <>
struct hash<tsc::lsp::lsproto::Range> {
	size_t operator()(const tsc::lsp::lsproto::Range& r) const {
		return hash<tsc::lsp::lsproto::Position>{}(r.Start) * 31u +
			   hash<tsc::lsp::lsproto::Position>{}(r.End);
	}
};
template <>
struct hash<tsc::lsp::lsproto::Location> {
	size_t operator()(const tsc::lsp::lsproto::Location& l) const {
		return hash<tsc::lsp::lsproto::DocumentUri>{}(l.Uri) * 31u +
			   hash<tsc::lsp::lsproto::Range>{}(l.Range);
	}
};
} // namespace std

namespace tsc::ls {

// ============================================================================
// === slice: ls-coreC === — forward decls for file-local types referenced by
// LanguageService signatures (full defs live in the owning .cpp).
// ============================================================================
struct sourceDefResolver; // sourcedefinition.go:132
struct incomingEntry;	  // callhierarchy.go:593
struct callSite;		  // callhierarchy.go:533
struct semanticToken;	  // semantictokens.go:215
struct script;			  // sourcedefinition.go:144
struct refInfo;			  // findallreferences.go (ls-coreB)
// hover.go:204 — documentationLocationMapper
using documentationLocationMapper =
	std::function<std::pair<lsp::lsproto::Location, spanmap::Fidelity>(SourceFile*, TextRange)>;

// rename.go:25 — RenameInfo (ported — declared here since GetRenameInfo returns it).
struct RenameInfo {
	bool CanRename = false;
	std::string LocalizedErrorMessage;
	std::string DisplayName;
	lsp::lsproto::Range TriggerSpan;
	std::string FileToRename;
	std::string NewFileName;
};

// ============================================================================
// host.go — the language service Host interface (shared file; ported for real
// as it is pure declaration).
// ============================================================================
struct Host {
	virtual ~Host() = default;
	virtual bool UseCaseSensitiveFileNames() = 0;
	virtual std::pair<std::string, bool> ReadFile(std::string_view fileName) = 0;
	virtual lsconv::Converters* Converters() = 0;
	virtual lsutil::UserPreferences GetPreferences(SourceFile* activeFile) = 0;
	virtual sourcemap::ECMALineInfo* GetECMALineInfo(std::string_view fileName) = 0;
	virtual autoimport::Registry* AutoImportRegistry() = 0;
	virtual std::vector<std::string> ReadDirectory(std::string_view rootDir, std::string_view path,
													 std::vector<std::string> extensions,
													 std::vector<std::string>* excludes,
													 std::vector<std::string> includes, int depth) = 0;
	virtual std::vector<std::string> GetDirectories(std::string_view path) = 0;
	virtual bool DirectoryExists(std::string_view path) = 0;
	virtual bool FileExists(std::string_view path) = 0;
};

// ============================================================================
// crossproject.go — dep decls (owned by ls-coreB)
// ============================================================================
struct Project;
struct LanguageService;
struct CrossProjectOrchestrator {
	virtual ~CrossProjectOrchestrator() = default;
	virtual std::vector<Project*> GetAllProjectsForInitialRequest() = 0;
	virtual LanguageService* GetLanguageServiceForProjectWithFile(
		gostd::Context ctx, Project* project, lsp::lsproto::DocumentUri uri) = 0;
	virtual std::pair<std::vector<Project*>, gostd::Error> GetProjectsForFile(
		gostd::Context ctx, lsp::lsproto::DocumentUri uri) = 0;
	virtual void GetProjectsLoadingProjectTree(
		gostd::Context ctx, collections::Set<tspath::Path>* requestedProjectTrees,
		const std::function<bool(Project*)>& yield) = 0;
};

// ============================================================================
// findallreferences.go — dep decls (owned by ls-coreB)
// ============================================================================

// findallreferences.go:71
using DefinitionKind = int;
inline constexpr DefinitionKind definitionKindSymbol = 0;
inline constexpr DefinitionKind definitionKindLabel = 1;
inline constexpr DefinitionKind definitionKindKeyword = 2;
inline constexpr DefinitionKind definitionKindThis = 3;
inline constexpr DefinitionKind definitionKindString = 4;
inline constexpr DefinitionKind definitionKindTripleSlashReference = 5;

// findallreferences.go:88
struct tripleSlashDefinition {
	FileReference* reference = nullptr;
	SourceFile* file = nullptr;
};

// findallreferences.go:82
struct Definition {
	DefinitionKind Kind = 0;
	Symbol* symbol = nullptr;
	Node* node = nullptr;
	tripleSlashDefinition* tripleSlashFileRef = nullptr;
};

// findallreferences.go:93
using entryKind = int;
inline constexpr entryKind entryKindNone = 0;
inline constexpr entryKind entryKindRange = 1;
inline constexpr entryKind entryKindNode = 2;
inline constexpr entryKind entryKindStringLiteral = 3;
inline constexpr entryKind entryKindSearchedLocalFoundProperty = 4;
inline constexpr entryKind entryKindSearchedPropertyFoundLocal = 5;

// findallreferences.go:104
struct ReferenceEntry {
	entryKind kind = 0;
	Node* node = nullptr;
	Node* context = nullptr; // !!! ContextWithStartAndEndNode, optional
	SourceFile* sourceFile = nullptr;
	TextRange* textRange = nullptr;
	lsp::lsproto::Location* lspRange = nullptr;
	bool unmappable = false;

	// findallreferences.go:119 — Node returns the AST node for this reference entry.
	Node* Node() { return node; }
	// findallreferences.go:124 — IsNodeEntry returns true if this is a node-backed reference entry.
	bool IsNodeEntry() { return node != nullptr; }
};

// findallreferences.go:55
struct SymbolAndEntries {
	Definition* definition = nullptr;
	std::vector<ReferenceEntry*> references;

	// findallreferences.go:128
	std::vector<ReferenceEntry*>& References() { return references; }
	// findallreferences.go:133
	Node* DefinitionNode() {
		if (definition == nullptr) {
			return nullptr;
		}
		if (definition->node != nullptr) {
			return definition->node;
		}
		if (definition->symbol != nullptr && !definition->symbol->declarations.empty()) {
			return definition->symbol->declarations[0];
		}
		return nullptr;
	}
	// findallreferences.go:146
	Symbol* DefinitionSymbol() {
		if (definition == nullptr) {
			return nullptr;
		}
		return definition->symbol;
	}
	// findallreferences.go:153
	bool canUseDefinitionSymbol() {
		if (definition == nullptr) {
			return false;
		}
		switch (definition->Kind) {
		case definitionKindSymbol:
		case definitionKindThis:
			return definition->symbol != nullptr;
		case definitionKindTripleSlashReference:
			// !!! TODO : need to find file reference instead?
			// May need to return true to indicate this to be file search instead and might need to do for import stuff as well
			// For now
			return false;
		default:
			return false;
		}
	}
};

// findallreferences.go:59 — NewSymbolAndEntries
inline SymbolAndEntries* NewSymbolAndEntries(DefinitionKind kind, ::tsc::Node* node, Symbol* symbol,
											 std::vector<ReferenceEntry*> references) {
	auto* sae = new SymbolAndEntries();
	sae->definition = new Definition{kind, symbol, node, nullptr};
	sae->references = std::move(references);
	return sae;
}

// findallreferences.go:480 — `position` implements lsproto.HasTextDocumentPosition.
struct position : lsp::lsproto::HasTextDocumentPosition {
	lsp::lsproto::DocumentUri uri;
	lsp::lsproto::Position pos;

	lsp::lsproto::DocumentUri TextDocumentURI() const override { return uri; }
	lsp::lsproto::Position TextDocumentPosition() const override { return pos; }
};

// findallreferences.go:490
struct nonLocalDefinition : position {
	std::function<lsp::lsproto::HasTextDocumentPosition*()> GetSourcePosition;
	std::function<lsp::lsproto::HasTextDocumentPosition*()> GetGeneratedPosition;
};

// findallreferences.go:631
struct symbolEntryTransformOptions {
	// Force the result to be Location objects.
	bool requireLocationsResult = false;
	// Omit node(s) containing the original position.
	bool dropOriginNodes = false;
};

// findallreferences.go:638
struct SymbolAndEntriesData {
	::tsc::Node* OriginalNode = nullptr;
	std::vector<SymbolAndEntries*> SymbolsAndEntries;
	int Position = 0;
};

// findallreferences.go:1210 — SignatureUsage represents a single usage of a
// signature declaration, pairing the reference name node with its containing
// call expression (if any).
struct SignatureUsage {
	::tsc::Node* Name = nullptr; // The identifier reference node
	::tsc::Node* Call = nullptr; // The containing call expression, or nil if not a call usage
};

// findallreferences.go:495 — getFileAndStartPosFromDeclaration (dep stub)
std::pair<SourceFile*, TextPos> getFileAndStartPosFromDeclaration(::tsc::Node* declaration);
// findallreferences.go:504 — LanguageService::getNonLocalDefinition (dep stub)
// findallreferences.go — free helpers used by this slice (dep stubs)
std::vector<SymbolAndEntries*> combineSymbolAndEntries(std::vector<SymbolAndEntries*> a,
													   std::vector<SymbolAndEntries*> b);

// ============================================================================
// languageservice.go — LanguageService struct + base methods.
// This file is shared between slices; the struct and its trivial accessors are
// ported here for real. Methods on it that live in sibling-owned files are
// dep-declared and stubbed at the bottom of ls.cpp.
// ============================================================================
struct LanguageService : sourcemap::Host {
	autoimport::ProjectID projectID;
	// NB: `Host` unqualified here would name the injected base-class name
	// sourcemap::Host — qualify to keep tsc::ls::Host.
	::tsc::ls::Host* host = nullptr;
	compiler::SimpleProgram* program = nullptr;
	lsconv::Converters* converters = nullptr;
	lsutil::UserPreferences activeConfig;
	std::unordered_map<std::string, sourcemap::DocumentPositionMapper*> documentPositionMappers;

	// languageservice.go:41 — toPath
	tspath::Path toPath(std::string_view fileName);
	// languageservice.go:45 — GetProgram
	compiler::SimpleProgram* GetProgram() { return program; }
	// languageservice.go:49 — UserPreferences
	lsutil::UserPreferences UserPreferences() { return activeConfig; }
	// languageservice.go:53 — FormatOptions
	lsutil::FormatCodeSettings FormatOptions() { return activeConfig.FormatCodeSettings; }
	// languageservice.go:57 — tryGetProgramAndFile
	std::pair<compiler::SimpleProgram*, SourceFile*> tryGetProgramAndFile(std::string_view fileName);
	// languageservice.go:63 — getProgramAndFile (panics when the file is missing)
	std::pair<compiler::SimpleProgram*, SourceFile*> getProgramAndFile(lsp::lsproto::DocumentUri documentURI);
	// languageservice.go:72 — GetDocumentPositionMapper
	sourcemap::DocumentPositionMapper* GetDocumentPositionMapper(std::string_view fileName);
	// sourcemap::Host (languageservice.go:81-92)
	std::pair<std::string, bool> ReadFile(std::string_view fileName) override;
	bool UseCaseSensitiveFileNames() override;
	sourcemap::ECMALineInfo* GetECMALineInfo(std::string_view fileName) override;
	// languageservice.go:95 — getPreparedAutoImportView (dep stub — autoimport slice)
	autoimport::View* getPreparedAutoImportView(SourceFile* fromFile, checker::Checker* typeChecker);
	// languageservice.go:111 — getCurrentAutoImportView (dep stub — autoimport slice)
	autoimport::View* getCurrentAutoImportView(SourceFile* fromFile, checker::Checker* typeChecker);
	// languageservice.go:123 — DirectoryExists
	bool DirectoryExists(std::string_view path);
	// languageservice.go:128 — ReadDirectory
	std::vector<std::string> ReadDirectory(std::string_view path, std::vector<std::string> extensions,
										   std::vector<std::string> includes);
	// languageservice.go:132 — GetDirectories
	std::vector<std::string> GetDirectories(std::string_view path);

	// languageservice.go:30 — NewLanguageService
	LanguageService(autoimport::ProjectID projectID, ::tsc::ls::Host* host,
					compiler::SimpleProgram* program, SourceFile* activeFile);

	// --- source_map.go (dep stubs — owned by ls-coreA) ---
	// source_map.go:18
	std::pair<lsp::lsproto::Location, spanmap::Fidelity> sourceFileRangeToLSPLocation(
		SourceFile* file, TextRange fileRange);
	// source_map.go:28
	std::pair<lsp::lsproto::Location, spanmap::Fidelity> sourceFileRangeToLSPLocationForFeature(
		SourceFile* file, TextRange fileRange, spanmap::Feature feature);
	// source_map.go:38
	std::pair<lsp::lsproto::Location, spanmap::Fidelity> getMappedLocation(std::string_view fileName,
																		 TextRange fileRange);
	// source_map.go:85
	script* getScript(std::string_view fileName);
	// source_map.go:93
	sourcemap::DocumentPosition* tryGetSourcePosition(std::string_view fileName, TextPos position);
	// source_map.go:106
	sourcemap::DocumentPosition* tryGetSourcePositionWorker(std::string_view fileName,
														  TextPos position);
	// source_map.go:125
	sourcemap::DocumentPosition* tryGetGeneratedPosition(std::string_view fileName, TextPos position);
	// source_map.go:138
	sourcemap::DocumentPosition* tryGetGeneratedPositionWorker(std::string_view fileName,
															 TextPos position);
	// utilities.go:288-308 — createLsp* methods (dep stubs)
	std::pair<lsp::lsproto::Range, spanmap::Fidelity> createLspRangeFromNode(
		::tsc::Node* node, SourceFile* file);
	std::pair<lsp::lsproto::Range, spanmap::Fidelity> createLspRangeFromNodeForFeature(
		::tsc::Node* node, SourceFile* file, spanmap::Feature feature);
	std::pair<lsp::lsproto::Range, spanmap::Fidelity> createLspRangeFromBounds(
		int start, int end, SourceFile* file);
	std::pair<lsp::lsproto::Range, spanmap::Fidelity> createLspRangeFromRange(
		TextRange textRange, script* s);
	std::pair<lsp::lsproto::Position, spanmap::Fidelity> createLspPosition(int position,
																		 SourceFile* file);

	// findallreferences.go methods this slice calls (dep stubs — ls-coreB)
	lsp::lsproto::Range getRangeOfEntry(ReferenceEntry* entry);
	std::pair<lsp::lsproto::Range, bool> getRangeOfEntryForFeature(ReferenceEntry* entry,
																 spanmap::Feature feature);
	lsp::lsproto::DocumentUri getFileNameOfEntry(ReferenceEntry* entry);
	std::pair<lsp::lsproto::Location, bool> getLocationOfEntryForFeature(ReferenceEntry* entry,
																	   spanmap::Feature feature);
	void resolveEntrySource(ReferenceEntry* entry);
	ReferenceEntry* resolveEntry(ReferenceEntry* entry);
	nonLocalDefinition* getNonLocalDefinition(gostd::Context ctx, SymbolAndEntries* entry);
	std::pair<SymbolAndEntriesData, bool> provideSymbolsAndEntries(
		gostd::Context ctx, lsp::lsproto::DocumentUri uri, lsp::lsproto::Position documentPosition,
		bool isRename, bool implementations);
	std::pair<SymbolAndEntriesData, bool> provideSymbolsAndEntriesAtPosition(
		gostd::Context ctx, compiler::SimpleProgram* program, SourceFile* sourceFile, int position,
		bool isRename, bool implementations);
	SymbolAndEntriesData getSymbolAndEntries(gostd::Context ctx, SourceFile* sourceFile, int position,
											 bool isRename, bool implementations);
	std::vector<SymbolAndEntries*> GetReferencedSymbolsForNode(gostd::Context ctx, int pos, ::tsc::Node* node,
															 std::vector<SourceFile*> sourceFiles);
	std::vector<ReferenceEntry*> getReferencedSymbolsForSymbol(gostd::Context ctx, Symbol* symbol,
															 std::vector<::tsc::Node*> excludeDeclaration,
															 SourceFile* sourceFile,
															 std::vector<SourceFile*> sourceFiles);
	// findallreferences.go:761
	lsp::lsproto::ReferencesResponse provideReferencesFromData(
		gostd::Context ctx, lsp::lsproto::ReferenceParams* params,
		CrossProjectOrchestrator* orchestrator, SymbolAndEntriesData data);
	// findallreferences.go:1034
	lsp::lsproto::ImplementationResponse provideImplementationsFromData(
		gostd::Context ctx, lsp::lsproto::ImplementationParams* params,
		symbolEntryTransformOptions options, CrossProjectOrchestrator* orchestrator,
		SymbolAndEntriesData data);

	// crossproject.go (dep stub — ls-coreB)
	template <class Req, class Resp>
	std::pair<Resp, gostd::Error> handleCrossProject(
		gostd::Context ctx,
		Req params,
		CrossProjectOrchestrator* orchestrator,
		std::function<std::pair<Resp, gostd::Error>(LanguageService*, gostd::Context, Req,
													SymbolAndEntriesData, symbolEntryTransformOptions)>
			symbolAndEntriesToResp,
		std::function<Resp(std::function<void(std::function<bool(Resp)>)>)> combineResults,
		bool isRename,
		bool implementations,
		symbolEntryTransformOptions options,
		SymbolAndEntriesData* defaultProjectData) {
		TSC_UNREACHABLE("handleCrossProject — owned by ls-coreB slice");
	}

	// === slice: ls-coreC — format.go ===
	std::vector<lsp::lsproto::TextEdit*> toLSProtoTextEdits(SourceFile* file,
														  std::vector<TextChange> changes);
	lsp::lsproto::DocumentFormattingResponse ProvideFormatDocument(
		gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
		lsp::lsproto::FormattingOptions* options);
	std::vector<lsp::lsproto::TextEdit*> getFormattingEditsForMappedRange(
		gostd::Context ctx, SourceFile* file, lsutil::FormatCodeSettings options,
		TextRange originalRange);
	lsp::lsproto::DocumentRangeFormattingResponse ProvideFormatDocumentRange(
		gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
		lsp::lsproto::FormattingOptions* options, lsp::lsproto::Range r);
	lsp::lsproto::DocumentOnTypeFormattingResponse ProvideFormatDocumentOnType(
		gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
		lsp::lsproto::FormattingOptions* options, lsp::lsproto::Position position,
		std::string character);
	std::vector<TextChange> getFormattingEditsForRange(gostd::Context ctx, SourceFile* file,
													 lsutil::FormatCodeSettings options,
													 TextRange r);
	std::vector<TextChange> getFormattingEditsForDocument(gostd::Context ctx, SourceFile* file,
														lsutil::FormatCodeSettings options);
	std::vector<TextChange> getFormattingEditsAfterKeystroke(gostd::Context ctx, SourceFile* file,
														   lsutil::FormatCodeSettings options,
														   int position, std::string key);

	// === slice: ls-coreC — codelens.go ===
	lsp::lsproto::CodeLensResponse ProvideCodeLenses(gostd::Context ctx,
													 lsp::lsproto::DocumentUri documentURI);
	std::pair<lsp::lsproto::CodeLens*, gostd::Error> ResolveCodeLens(
		gostd::Context ctx, lsp::lsproto::CodeLens* codeLens,
		std::string* showLocationsCommandName, CrossProjectOrchestrator* orchestrator);
	lsp::lsproto::CodeLens* newCodeLensForNode(lsp::lsproto::DocumentUri fileUri, SourceFile* file,
											   ::tsc::Node* node, lsp::lsproto::CodeLensKind kind);

	// === slice: ls-coreC — selectionranges.go ===
	lsp::lsproto::SelectionRangeResponse ProvideSelectionRanges(
		gostd::Context ctx, lsp::lsproto::SelectionRangeParams* params);

	// === slice: ls-coreC — definition.go ===
	lsp::lsproto::DefinitionResponse ProvideDefinition(gostd::Context ctx,
													   lsp::lsproto::DocumentUri documentURI,
													   lsp::lsproto::Position position);
	lsp::lsproto::DefinitionResponse provideDefinitionWorker(
		gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
		lsp::lsproto::Position position);
	lsp::lsproto::DefinitionResponse provideDefinitionAtPosition(
		gostd::Context ctx, compiler::SimpleProgram* program, SourceFile* file, TextPos textPos,
		bool clientSupportsLink);
	lsp::lsproto::TypeDefinitionResponse ProvideTypeDefinition(gostd::Context ctx,
															 lsp::lsproto::DocumentUri documentURI,
															 lsp::lsproto::Position position);
	lsp::lsproto::TypeDefinitionResponse provideTypeDefinitionAtPosition(
		gostd::Context ctx, compiler::SimpleProgram* program, SourceFile* file, TextPos textPos,
		bool clientSupportsLink);
	lsp::lsproto::DefinitionResponse createDefinitionLocations(
		lsp::lsproto::Range originSelectionRange, bool clientSupportsLink,
		std::vector<::tsc::Node*> declarations, refInfo* reference, spanmap::Feature feature);
	lsp::lsproto::DefinitionResponse createLocationFromFileAndRange(SourceFile* file,
																  TextRange textRange,
																  spanmap::Feature feature);

	// === slice: ls-coreC — rename.go ===
	std::pair<lsp::lsproto::WorkspaceEditOrNull, gostd::Error> ProvideRename(
		gostd::Context ctx, lsp::lsproto::RenameParams* params,
		CrossProjectOrchestrator* orchestrator);
	RenameInfo GetRenameInfo(gostd::Context ctx, std::string newName,
							 lsp::lsproto::DocumentUri documentURI, lsp::lsproto::Position position);
	std::pair<lsp::lsproto::WorkspaceEditOrNull, gostd::Error> symbolAndEntriesToRename(
		gostd::Context ctx, lsp::lsproto::RenameParams* params, SymbolAndEntriesData data,
		symbolEntryTransformOptions options);
	std::pair<lsp::lsproto::Range, bool> renameEditRange(ReferenceEntry* entry);
	std::pair<RenameInfo, bool> getRenameInfoForNode(gostd::Context ctx, std::string newName,
													 ::tsc::Node* node, SourceFile* sourceFile,
													 compiler::SimpleProgram* program);
	const DiagnosticMessage* renameBlockedReason(SourceFile* sourceFile,
															  ::tsc::Node* node, Symbol* symbol,
															  checker::Checker* ch,
															  compiler::SimpleProgram* program);
	std::pair<RenameInfo, bool> getRenameInfoForModule(gostd::Context ctx, std::string newName,
													   ::tsc::Node* specifier, SourceFile* sourceFile,
													   Symbol* moduleSymbol);
	std::string getNewFileNameForModuleRename(std::string oldPath, std::string specifierText,
											  std::string newName);
	std::string getTextForRename(::tsc::Node* originalNode, ReferenceEntry* entry,
								 std::string newText, checker::Checker* ch,
								 lsutil::QuotePreference quotePreference, bool useAliasesForRename);

	// === slice: ls-coreC — folding.go ===
	lsp::lsproto::FoldingRangeResponse ProvideFoldingRange(gostd::Context ctx,
														 lsp::lsproto::DocumentUri documentURI);
	std::vector<lsp::lsproto::FoldingRange*> adjustFoldingEnd(
		std::vector<lsp::lsproto::FoldingRange*> ranges, SourceFile* sourceFile);
	std::vector<lsp::lsproto::FoldingRange*> addNodeOutliningSpans(gostd::Context ctx,
																 SourceFile* sourceFile);
	std::vector<lsp::lsproto::FoldingRange*> addRegionOutliningSpans(gostd::Context ctx,
																   SourceFile* sourceFile);
	std::pair<lsp::lsproto::Range, spanmap::Fidelity> createFoldingRangeFromBounds(int start, int end,
																				 SourceFile* sourceFile);

	// === slice: ls-coreC — sourcedefinition.go ===
	lsp::lsproto::DefinitionResponse ProvideSourceDefinition(gostd::Context ctx,
															 lsp::lsproto::DocumentUri documentURI,
															 lsp::lsproto::Position position);
	std::pair<lsp::lsproto::DefinitionResponse, gostd::Error> provideSourceDefinitionAtPosition(
		gostd::Context ctx, compiler::SimpleProgram* program, SourceFile* file, TextPos textPos);
	sourceDefResolver* newSourceDefResolver(compiler::SimpleProgram* program, std::string resolveFrom);

	// === slice: ls-coreC — semantictokens.go ===
	lsp::lsproto::SemanticTokensResponse ProvideSemanticTokens(gostd::Context ctx,
															   lsp::lsproto::DocumentUri documentURI);
	lsp::lsproto::SemanticTokensRangeResponse ProvideSemanticTokensRange(
		gostd::Context ctx, lsp::lsproto::DocumentUri documentURI, lsp::lsproto::Range rng);
	std::vector<semanticToken> collectSemanticTokens(gostd::Context ctx, checker::Checker* c,
													 SourceFile* file,
													 compiler::SimpleProgram* program);
	std::vector<semanticToken> collectSemanticTokensInRange(gostd::Context ctx, checker::Checker* c,
															SourceFile* file,
															compiler::SimpleProgram* program,
															int spanStart, int spanEnd);

	// === slice: ls-coreC — symbols.go ===
	lsp::lsproto::DocumentSymbolResponse ProvideDocumentSymbols(gostd::Context ctx,
																lsp::lsproto::DocumentUri documentURI);
	std::vector<lsp::lsproto::SymbolInformation> getDocumentSymbolInformations(
		gostd::Context ctx, SourceFile* file, lsp::lsproto::DocumentUri documentURI);
	std::vector<lsp::lsproto::DocumentSymbol*> getDocumentSymbolsForChildren(gostd::Context ctx,
																		   ::tsc::Node* node,
																		   SourceFile* file);
	lsp::lsproto::DocumentSymbol* newDocumentSymbol(
		::tsc::Node* node, ::tsc::Node* name, std::vector<lsp::lsproto::DocumentSymbol*> children);

	// === slice: ls-coreC — documenthighlights.go ===
	lsp::lsproto::DocumentHighlightResponse ProvideDocumentHighlights(
		gostd::Context ctx, lsp::lsproto::DocumentUri documentUri,
		lsp::lsproto::Position documentPosition);
	lsp::lsproto::CustomMultiDocumentHighlightResponse ProvideMultiDocumentHighlights(
		gostd::Context ctx, lsp::lsproto::DocumentUri documentUri,
		lsp::lsproto::Position documentPosition,
		std::vector<lsp::lsproto::DocumentUri> filesToSearch);
	std::pair<lsp::lsproto::MultiDocumentHighlightsOrNull, gostd::Error>
	provideDocumentHighlightsWorker(gostd::Context ctx, lsp::lsproto::DocumentUri documentUri,
									lsp::lsproto::Position documentPosition,
									std::vector<lsp::lsproto::DocumentUri> filesToSearch);
	lsp::lsproto::MultiDocumentHighlightsOrNull provideDocumentHighlightsAtPosition(
		gostd::Context ctx, lsp::lsproto::DocumentUri documentUri, int position,
		compiler::SimpleProgram* program, SourceFile* sourceFile,
		std::vector<lsp::lsproto::DocumentUri> filesToSearch);
	std::vector<lsp::lsproto::MultiDocumentHighlight*> getSemanticDocumentHighlights(
		gostd::Context ctx, int position, ::tsc::Node* node, compiler::SimpleProgram* program,
		std::vector<SourceFile*> sourceFiles);
	std::pair<std::string, lsp::lsproto::DocumentHighlight*> toDocumentHighlight(ReferenceEntry* entry);
	std::vector<lsp::lsproto::DocumentHighlight*> getSyntacticDocumentHighlights(::tsc::Node* node,
																			   SourceFile* sourceFile);
	std::vector<lsp::lsproto::DocumentHighlight*> useParent(
		::tsc::Node* node, std::function<bool(::tsc::Node*)> nodeTest,
		std::function<std::vector<::tsc::Node*>(::tsc::Node*, SourceFile*)> getNodes,
		SourceFile* sourceFile);
	std::vector<lsp::lsproto::DocumentHighlight*> highlightSpans(std::vector<::tsc::Node*> nodes,
															   SourceFile* sourceFile);
	std::vector<lsp::lsproto::DocumentHighlight*> getFromAllDeclarations(
		std::function<bool(::tsc::Node*)> nodeTest, std::vector<Kind> keywords, ::tsc::Node* node,
		SourceFile* sourceFile);
	std::vector<lsp::lsproto::DocumentHighlight*> getIfElseOccurrences(IfStatement* ifStatement,
																	 SourceFile* sourceFile);

	// === slice: ls-coreC — inlay_hints.go ===
	lsp::lsproto::InlayHintResponse ProvideInlayHint(gostd::Context ctx,
												   lsp::lsproto::InlayHintParams* params);

	// === slice: ls-coreC — callhierarchy.go ===
	lsp::lsproto::CallHierarchyItem* createCallHierarchyItem(compiler::SimpleProgram* program,
														   ::tsc::Node* node);
	lsp::lsproto::CallHierarchyIncomingCall* convertCallSiteGroupToIncomingCall(
		compiler::SimpleProgram* program, std::vector<callSite*> entries);
	lsp::lsproto::CallHierarchyOutgoingCall* convertCallSiteGroupToOutgoingCall(
		compiler::SimpleProgram* program, std::vector<callSite*> entries);
	std::pair<lsp::lsproto::CallHierarchyIncomingCallsResponse, gostd::Error> getIncomingCalls(
		gostd::Context ctx, compiler::SimpleProgram* program, ::tsc::Node* declaration,
		CrossProjectOrchestrator* orchestrator);
	std::pair<lsp::lsproto::CallHierarchyIncomingCallsResponse, gostd::Error>
	symbolAndEntriesToIncomingCalls(gostd::Context ctx, incomingEntry* params,
									SymbolAndEntriesData data, symbolEntryTransformOptions options);
	std::vector<lsp::lsproto::CallHierarchyOutgoingCall*> getOutgoingCalls(
		compiler::SimpleProgram* program, ::tsc::Node* declaration);
	lsp::lsproto::CallHierarchyPrepareResponse ProvidePrepareCallHierarchy(
		gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
		lsp::lsproto::Position position);
	lsp::lsproto::CallHierarchyIncomingCallsResponse ProvideCallHierarchyIncomingCalls(
		gostd::Context ctx, lsp::lsproto::CallHierarchyItem* item,
		CrossProjectOrchestrator* orchestrator);
	lsp::lsproto::CallHierarchyOutgoingCallsResponse ProvideCallHierarchyOutgoingCalls(
		gostd::Context ctx, lsp::lsproto::CallHierarchyItem* item);
	std::vector<::tsc::Node*> callHierarchyDeclarations(SourceFile* file,
														lsp::lsproto::Position position,
														compiler::SimpleProgram* program,
														bool allowSourceFile);

	// === slice: ls-coreC — hover.go ===
	lsp::lsproto::HoverResponse ProvideHover(gostd::Context ctx, lsp::lsproto::HoverParams* params);
	// Returns (quickInfo, documentation, requiredChars, vsRuns) — Go's 4-tuple.
	std::tuple<std::string, std::string, std::string,
			   std::vector<lsp::lsproto::VSClassifiedTextRun*>>
	getQuickInfoAndDocumentationForSymbol(checker::Checker* c, Symbol* symbol, ::tsc::Node* node,
										  lsp::lsproto::MarkupKind contentFormat,
										  checker::VerbosityContext* vc, bool vsCapability);
	::tsc::ls::documentationLocationMapper documentationLocationMapper(spanmap::Feature feature);
};

// ============================================================================
// source_map.go — dep decls (owned by ls-coreA)
// ============================================================================
// source_map.go:65 — `script` implements lsconv.Script over raw file text.
struct script;

// ============================================================================
// utilities.go — dep decls (owned by ls-coreA)
// ============================================================================
// utilities.go:95
CommentRange* isInComment(SourceFile* file, int position, ::tsc::Node* tokenAtPosition);
// utilities.go:353
bool isLiteralNameOfPropertyDeclarationOrIndexAccess(::tsc::Node* node);
// utilities.go:377
bool isObjectBindingElementWithoutPropertyName(::tsc::Node* bindingElement);
// utilities.go:459
::tsc::Node* getAdjustedLocation(::tsc::Node* node, bool forRename, SourceFile* sourceFile);
// utilities.go:807
SemanticMeaning getMeaningFromLocation(::tsc::Node* node);
// utilities.go:1011
::tsc::Node* getTargetLabel(::tsc::Node* referenceNode, std::string_view labelName);
// utilities.go:1212 — returns iter.Seq[ast.CommentRange] → yield-callback fn
void getLeadingCommentRangesOfNode(::tsc::Node* node, SourceFile* file,
								   const std::function<bool(const CommentRange&)>& yield);
// utilities.go:1220
std::vector<::tsc::Node*> getChildrenFromNonJSDocNode(::tsc::Node* node, SourceFile* sourceFile);
// utilities.go:1261
::tsc::Node* getContainingObjectLiteralElement(::tsc::Node* node);
// utilities.go:1300
TextRange* toContextRange(TextRange* textRange, SourceFile* contextFile, ::tsc::Node* context);
// utilities.go:1381
checker::Type* getContextualTypeFromParentOrAncestorTypeNode(::tsc::Node* node,
															 checker::Checker* typeChecker);
// utilities.go:296 — createRangeFromNode (dep stub)
TextRange createRangeFromNode(::tsc::Node* node, SourceFile* file);
// getRangeOfNode (dep stub — owned by ls-coreB findallreferences.go or utilities.go)
TextRange getRangeOfNode(::tsc::Node* node, SourceFile* file, ::tsc::Node* endNode);
// findallreferences.go:286 — getContextNode (dep stub — ls-coreB)
::tsc::Node* getContextNode(::tsc::Node* node);

// findallreferences.go:48 — refInfo (dep decl — owned by ls-coreB)
struct refInfo {
	SourceFile* file = nullptr;
	std::string fileName;
	FileReference* reference = nullptr;
	bool unverified = false;
};
// utilities.go:1312
refInfo* getReferenceAtPosition(SourceFile* sourceFile, int position,
								compiler::SimpleProgram* program);
// definition.go — shared with sourcedefinition.cpp
lsp::lsproto::DefinitionResponse combineDefinitionResponses(
	const std::vector<lsp::lsproto::DefinitionResponse>& results, bool links);
std::vector<::tsc::Node*> getDeclarationsFromLocation(checker::Checker* c,
													::tsc::Node* node);
::tsc::Node* tryGetSignatureDeclaration(checker::Checker* typeChecker,
										::tsc::Node* node);
// utilities.go:448 (dep stub — ls-coreA)
::tsc::Node* getContainerNode(::tsc::Node* node);
// === slice: ls-coreC — helpers missing from already-ported slices ===
// ast.go:3087 — GetDeclarationName (not yet in cpp/internal/ast)
std::string getDeclarationName(::tsc::Node* declaration);
// ast.go:2968 — SourceFile.GetDeclarationMap (not yet in cpp/internal/ast)
const std::unordered_map<std::string, std::vector<::tsc::Node*>>&
getDeclarationMap(SourceFile* file);
// stringutil/util.go:256 — TruncateByRunes (not yet in cpp/internal/stringutil)
std::string truncateByRunes(std::string_view str, int maxLength);
// symbols.go:669 — shared by symbols.cpp + callhierarchy.cpp
lsp::lsproto::SymbolKind getSymbolKindFromNode(::tsc::Node* node);
// definition.go:281 — shared by definition.cpp + callhierarchy.cpp
bool lspRangeContains(lsp::lsproto::Range outer, lsp::lsproto::Range inner);
// jsdoc.go:168 (dep stub — ls-coreA)
::tsc::Node* getJSDocOrTag(checker::Checker* c, ::tsc::Node* node,
						  collections::Set<Symbol*>* seenSymbols);
// displaypartswriter.go — ported in ls.cpp (implements
// printer::EmitTextWriter; unlisted sibling file needed by hover.go).
struct displayPartsWriter : printer::EmitTextWriter {
	std::string builder; // Go strings.Builder
	std::vector<lsp::lsproto::VSClassifiedTextRun*> runs;
	bool vsCapability = false;
	std::string lastWritten;

	void addRun(lsp::lsproto::ClassificationTypeName classification,
				std::string_view text);
	void WriteClassified(std::string_view text,
						 lsp::lsproto::ClassificationTypeName classification);
	void WriteFrom(displayPartsWriter* other);
	std::vector<lsp::lsproto::VSClassifiedTextRun*> GetRuns();

	// EmitTextWriter
	void Write(const std::string& s) override;
	void WriteTrailingSemicolon(const std::string& text) override;
	void WriteComment(const std::string& text) override;
	void WriteKeyword(const std::string& text) override;
	void WriteOperator(const std::string& text) override;
	void WritePunctuation(const std::string& text) override;
	void WriteSpace(const std::string& text) override;
	void WriteStringLiteral(const std::string& text) override;
	void WriteParameter(const std::string& text) override;
	void WriteProperty(const std::string& text) override;
	void WriteSymbol(const std::string& text, Symbol* symbol) override;
	void WriteLine() override;
	void WriteLineForce(bool force) override;
	void IncreaseIndent() override {}
	void DecreaseIndent() override {}
	void Clear() override;
	std::string String() override;
	void RawWrite(const std::string& s) override;
	void WriteLiteral(const std::string& s) override;
	int GetTextPos() override;
	int GetLine() override;
	TextPos GetColumn() override;
	int GetIndent() override;
	bool IsAtStartOfLine() override;
	bool HasTrailingComment() override;
	bool HasTrailingWhitespace() override;
};
// displaypartswriter.go:26
displayPartsWriter* newDisplayPartsWriter(bool vsCapability);
// hovericon.go:56 (dep stub — ls-coreA)
lsp::lsproto::VSImageId* getVSHoverImageId(lsutil::ScriptElementKind kind,
										 lsutil::ScriptElementKindModifier modifiers);
// hovericon.go:132 (dep stub — ls-coreA)
lsp::lsproto::VSContainerElement* buildVSHoverRawContent(
	lsp::lsproto::VSImageId* imageId, std::vector<lsp::lsproto::VSClassifiedTextRun*> quickInfoRuns,
	std::vector<lsp::lsproto::VSClassifiedTextRun*> documentationRuns);
// completions.go:139 (dep stub — ls-coreA)
int32_t* supplementalFileIndex(SourceFile* file);
// completions.go:152 (dep stub — ls-coreA)
SourceFile* sourceFileForSupplementalFileIndex(SourceFile* file, int32_t* index);

// ============================================================================
// === slice: ls-coreC === — exported package-level fns
// ============================================================================
// semantictokens.go:109
lsp::lsproto::SemanticTokensLegend* SemanticTokensLegend(
	lsp::lsproto::ResolvedSemanticTokensClientCapabilities clientCapabilities);
// symbols.go:546
std::pair<lsp::lsproto::WorkspaceSymbolResponse, gostd::Error> ProvideWorkspaceSymbols(
	gostd::Context ctx, std::vector<compiler::SimpleProgram*> programs,
	lsconv::Converters* converters, lsutil::UserPreferences preferences, std::string query);
// completions.go:3033 (dep stub — ls-coreA)
int getLineEndOfPosition(SourceFile* file, int pos);
// completions.go:3014 — trivial helper, ported for real (small enough to keep inline)
inline std::string* strPtrTo(std::string v) {
	if (v.empty()) {
		return nullptr;
	}
	return new std::string(std::move(v));
}
// rename.go:290
bool ClientSupportsWillRenameFiles(gostd::Context ctx);
// rename.go:294
bool ClientSupportsDocumentChanges(gostd::Context ctx);
// rename.go:298
bool ClientSupportsRenameResourceOperations(gostd::Context ctx);
// symbols.go:619 — isInsideNodeModules (owned by this slice, defined in symbols.cpp)
bool isInsideNodeModules(std::string_view fileName);
// crossproject.go:367 (dep stub — ls-coreB)
lsp::lsproto::WorkspaceEditOrNull combineRenameResponse(
	std::function<void(std::function<bool(lsp::lsproto::WorkspaceEditOrNull)>)> results);

// ============================================================================
// format.go — slice types
// ============================================================================
// format.go:107 — mappedFormattingRange
struct mappedFormattingRange {
	SourceFile* projection = nullptr;
	spanmap::Segment segment;
	TextRange originalRange;
};
// format.go:130 — nonOverlappingFormattingRanges
std::vector<mappedFormattingRange> nonOverlappingFormattingRanges(std::vector<mappedFormattingRange> candidates);
// format.go:262 — getRangeOfEnclosingComment
CommentRange* getRangeOfEnclosingComment(SourceFile* file, int position, ::tsc::Node* precedingToken,
										 ::tsc::Node* tokenAtPosition);

} // namespace tsc::ls
