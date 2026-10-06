// Port of tsc/internal/ls — declarations for the ls-coreA slice
// (languageservice.go, host.go, api.go, constants.go, utilities.go,
// completions.go, string_completions.go, jsdoc.go, jsdoc_snippet.go,
// displaypartswriter.go). See PORTING.md for conventions.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/gostd/gostd.h"
#include "internal/ls/lsdeps.h"
#include "internal/locale/locale.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/module/types.h"
#include "internal/packagejson/packagejson.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/printer/printer.h"
#include "internal/sourcemap/sourcemap.h"

namespace tsc::ls::autoimport {
struct ProjectID;
struct Registry;
struct View;
struct ImportAdder;
struct Fix;
struct FixAndExport;
} // namespace tsc::ls::autoimport

namespace tsc::ls {

// newPtr — Go 1.26 `new(v)` (allocate + initialize to a copy of v).
template <class T>
T* newPtr(T v) {
	return new T(std::move(v));
}

// --- completions.go (cross-file types) ---

// completions.go:110 CompletionItem — Go embeds `*lsproto.CompletionItem`
// (field renamed: member==class name is illegal).
struct CompletionItem {
	lsproto::CompletionItem* completionItem = nullptr;
	// non-nil for symbol completions when IncludeSymbols is set; nil
	// otherwise
	Symbol* Symbol = nullptr;
};

// completions.go:115 CompletionList.
struct CompletionList {
	bool IsIncomplete = false;
	lsproto::CompletionItemDefaults* ItemDefaults = nullptr;
	lsproto::CompletionItemApplyKinds* ApplyKind = nullptr;
	std::vector<CompletionItem*> Items;
	lsproto::CompletionList* toLSP() const;
};

// --- host.go ---

// host.go:10 — `type Host interface`.
struct Host {
	virtual ~Host() = default;
	virtual bool UseCaseSensitiveFileNames() = 0;
	virtual std::pair<std::string, bool> ReadFile(const std::string& path) = 0;
	virtual lsconv::Converters* Converters() = 0;
	virtual lsutil::UserPreferences GetPreferences(
	    const std::string& activeFile) = 0;
	virtual sourcemap::ECMALineInfo* GetECMALineInfo(
	    const std::string& fileName) = 0;
	virtual autoimport::Registry* AutoImportRegistry() = 0;

	// Used for module specifier completions.
	// ! Do not use for anything else, as this violates the principle that
	// the host is a snapshot-in-time.
	virtual std::vector<std::string> ReadDirectory(
	    const std::string& currentDir, const std::string& path,
	    const std::vector<std::string>& extensions,
	    const std::vector<std::string>* excludes,
	    const std::vector<std::string>& includes, int depth) = 0;
	virtual std::vector<std::string> GetDirectories(
	    const std::string& path) = 0;
	virtual bool DirectoryExists(const std::string& path) = 0;
	virtual bool FileExists(const std::string& path) = 0;
};

// --- completions.go (types + constants) ---

// completions.go:164 `type completionData = any`.
struct completionDataData;
// Forward decls for LanguageService method signatures (defs below the
// class).
struct moduleCompletionNameAndKind;
struct moduleCompletionNameAndKindSet;
struct extensionOptions;
enum class referenceKind : int32_t;
struct snippetPrinter;
struct stringLiteralCompletions;
struct completionDataKeyword;
struct completionDataJSDocTagName {};
struct completionDataJSDocTag {};
struct completionDataJSDocParameterName {
	Node* tag = nullptr; // *ast.JSDocParameterOrPropertyTag
};
using completionData = std::variant<std::monostate, // Go nil `any`
                                    completionDataData*,
                                    completionDataKeyword*,
                                    completionDataJSDocTagName*,
                                    completionDataJSDocTag*,
                                    completionDataJSDocParameterName*>;

// completions.go:209 importStatementCompletionInfo.
struct importStatementCompletionInfo {
	bool isKeywordOnlyCompletion = false;
	Kind keywordCompletion = Kind::Unknown;
	bool isNewIdentifierLocation = false;
	bool isTopLevelTypeOnly = false;
	bool couldBeTypeOnlyImportSpecifier = false;
	lsproto::Range* replacementSpan = nullptr;
};

// completions.go:219 jsxInitializer.
struct jsxInitializer {
	bool isInitializer = false;
	Node* initializer = nullptr;
};

// completions.go:224 KeywordCompletionFilters.
using KeywordCompletionFilters = int32_t;
inline constexpr KeywordCompletionFilters KeywordCompletionFiltersNone = 0;
inline constexpr KeywordCompletionFilters KeywordCompletionFiltersAll = 1;
inline constexpr KeywordCompletionFilters KeywordCompletionFiltersClassElementKeywords = 2;
inline constexpr KeywordCompletionFilters KeywordCompletionFiltersInterfaceElementKeywords = 3;
inline constexpr KeywordCompletionFilters KeywordCompletionFiltersConstructorParameterKeywords = 4;
inline constexpr KeywordCompletionFilters KeywordCompletionFiltersFunctionLikeBodyKeywords = 5;
inline constexpr KeywordCompletionFilters KeywordCompletionFiltersTypeAssertionKeywords = 6;
inline constexpr KeywordCompletionFilters KeywordCompletionFiltersTypeKeywords = 7;
inline constexpr KeywordCompletionFilters KeywordCompletionFiltersTypeKeyword = 8;
inline constexpr KeywordCompletionFilters KeywordCompletionFiltersLast =
    KeywordCompletionFiltersTypeKeyword;

// completions.go:239 keywordFiltersFromSyntaxKind.
KeywordCompletionFilters keywordFiltersFromSyntaxKind(Kind keywordCompletion);

// completions.go:248 CompletionKind.
using CompletionKind = int32_t;
inline constexpr CompletionKind CompletionKindNone = 0;
inline constexpr CompletionKind CompletionKindObjectPropertyDeclaration = 1;
inline constexpr CompletionKind CompletionKindGlobal = 2;
inline constexpr CompletionKind CompletionKindPropertyAccess = 3;
inline constexpr CompletionKind CompletionKindMemberLike = 4;
inline constexpr CompletionKind CompletionKindString = 5;

// completions.go:259 CompletionTriggerCharacters.
inline const std::vector<std::string> CompletionTriggerCharacters{
    ".", "\"", "'", "`", "/", "@", "<", "#", " ", "*"};

// completions.go:262 allCommitCharacters.
inline const std::vector<std::string> allCommitCharacters{".", ",", ";"};
// completions.go:265 noCommaCommitCharacters.
inline const std::vector<std::string> noCommaCommitCharacters{".", ";"};
// completions.go:267 emptyCommitCharacters.
inline const std::vector<std::string> emptyCommitCharacters{};

// completions.go:269 `type SortText string`.
using SortText = std::string;
inline const SortText SortTextLocalDeclarationPriority{"10"};
inline const SortText SortTextLocationPriority{"11"};
inline const SortText SortTextOptionalMember{"12"};
inline const SortText SortTextMemberDeclaredBySpreadAssignment{"13"};
inline const SortText SortTextSuggestedClassMembers{"14"};
inline const SortText SortTextGlobalsOrKeywords{"15"};
inline const SortText SortTextAutoImportSuggestions{"16"};
inline const SortText SortTextClassMemberSnippets{"17"};
inline const SortText SortTextJavascriptIdentifiers{"18"};

// completions.go:283 DeprecateSortText.
SortText DeprecateSortText(const SortText& original);
// completions.go:287 ObjectLiteralPropertySortText.
SortText ObjectLiteralPropertySortText(const SortText& presetSortText,
                                       const std::string& symbolDisplayName);
// completions.go:291 SortBelow.
SortText SortBelow(const SortText& original);

// completions.go:295 symbolOriginInfoKind.
using symbolOriginInfoKind = int32_t;
inline constexpr symbolOriginInfoKind symbolOriginInfoKindThisType = 1 << 0;
inline constexpr symbolOriginInfoKind symbolOriginInfoKindSymbolMember = 1 << 1;
inline constexpr symbolOriginInfoKind symbolOriginInfoKindPromise = 1 << 2;
inline constexpr symbolOriginInfoKind symbolOriginInfoKindNullable = 1 << 3;
inline constexpr symbolOriginInfoKind symbolOriginInfoKindTypeOnlyAlias =
    1 << 4;
inline constexpr symbolOriginInfoKind symbolOriginInfoKindObjectLiteralMethod =
    1 << 5;
inline constexpr symbolOriginInfoKind symbolOriginInfoKindIgnore = 1 << 6;
inline constexpr symbolOriginInfoKind
    symbolOriginInfoKindComputedPropertyName = 1 << 7;

// completions.go:325 symbolOriginInfoObjectLiteralMethod.
struct symbolOriginInfoObjectLiteralMethod {
	std::string insertText;
	lsproto::CompletionItemLabelDetails* labelDetails = nullptr;
	bool isSnippet = false;
};

// completions.go:335 symbolOriginInfoTypeOnlyAlias.
struct symbolOriginInfoTypeOnlyAlias {
	Node* declaration = nullptr; // *ast.TypeOnlyImportDeclaration
};

// completions.go:339 symbolOriginInfoComputedPropertyName.
struct symbolOriginInfoComputedPropertyName {
	std::string symbolName;
};

// completions.go:308 symbolOriginInfo.data (`any` — one of the three
// payloads above).
using symbolOriginInfoData =
    std::variant<std::monostate, symbolOriginInfoObjectLiteralMethod*,
                 symbolOriginInfoTypeOnlyAlias*,
                 symbolOriginInfoComputedPropertyName*>;

// completions.go:308 symbolOriginInfo.
struct symbolOriginInfo {
	symbolOriginInfoKind kind = 0;
	bool isDefaultExport = false;
	bool isFromPackageJson = false;
	std::string fileName;
	symbolOriginInfoData data;

	// completions.go:310 symbolName.
	std::string symbolName();
	// completions.go:328 asObjectLiteralMethod.
	symbolOriginInfoObjectLiteralMethod* asObjectLiteralMethod();
};

// completions.go:352 `type completionSource string`.
using completionSource = std::string;
inline const completionSource completionSourceThisProperty{"ThisProperty/"};
inline const completionSource completionSourceClassMemberSnippet{
    "ClassMemberSnippet/"};
inline const completionSource completionSourceTypeOnlyAlias{"TypeOnlyAlias/"};
inline const completionSource completionSourceObjectLiteralMethodSnippet{
    "ObjectLiteralMethodSnippet/"};
inline const completionSource completionSourceSwitchCases{"SwitchCases/"};
inline const completionSource completionSourceObjectLiteralMemberWithComma{
    "ObjectLiteralMemberWithComma/"};

// completions.go:371 `type uniqueNamesMap = map[string]bool`.
using uniqueNamesMap = std::unordered_map<std::string, bool>;

// completions.go:3296 — `type CompletionsTriggerCharacter = string`.
using CompletionsTriggerCharacter = std::string;

// completions.go:374 literalValue — `string | jsnum.Number | jsnum.PseudoBigInt`
// (Go `any`); equals checker::LiteralValue's payload.
using literalValue = checker::LiteralValue;

// completions.go:376 globalsSearch.
using globalsSearch = int32_t;
inline constexpr globalsSearch globalsSearchContinue = 0;
inline constexpr globalsSearch globalsSearchSuccess = 1;
inline constexpr globalsSearch globalsSearchFail = 2;

// completions.go:166 completionDataData.
struct completionDataData {
	std::vector<Symbol*> symbols;
	std::vector<autoimport::FixAndExport*> autoImports;
	CompletionKind completionKind = CompletionKindNone;
	bool isInSnippetScope = false;
	Node* propertyAccessToConvert = nullptr;
	bool isNewIdentifierLocation = false;
	Node* location = nullptr;
	KeywordCompletionFilters keywordFilters = KeywordCompletionFiltersNone;
	std::vector<literalValue> literals;
	std::unordered_map<int, symbolOriginInfo*> symbolToOriginInfoMap;
	std::unordered_map<SymbolId, SortText> symbolToSortTextMap;
	Symbol* recommendedCompletion = nullptr;
	Node* previousToken = nullptr;
	Node* contextToken = nullptr;
	jsxInitializer jsxInitializer;
	bool insideJSDocTagTypeExpression = false;
	bool isTypeOnlyLocation = false;
	bool isJsxIdentifierExpected = false;
	bool isRightOfOpenTag = false;
	bool isRightOfDotOrQuestionDot = false;
	importStatementCompletionInfo* importStatementCompletion = nullptr;
	bool hasUnresolvedAutoImports = false;
	// Go: []string with a nil-check for "unset" (completionInfoFromData).
	std::optional<std::vector<std::string>> defaultCommitCharacters;
};

// completions.go:195 completionDataKeyword.
struct completionDataKeyword {
	std::vector<CompletionItem*> keywordCompletions;
	bool isNewIdentifierLocation = false;
};

// completions.go:2474 memberCompletionEntry.
struct memberCompletionEntry {
	std::string insertText;
	std::string filterText;
	bool isSnippet = false;
	std::vector<lsproto::TextEdit*> additionalTextEdits;
};

// completions.go:2585 objectLiteralMethodSymbol.
struct objectLiteralMethodSymbol {
	Symbol* symbol = nullptr;
	symbolOriginInfo* origin = nullptr;
};

// completions.go:2760 presentMemberModifiers.
struct presentMemberModifiers {
	ModifierFlags modifiers = ModifierFlagsNone;
	std::vector<Node*> decorators;
	lsproto::Range* eraseRange = nullptr;
};

// completions.go:5447 argumentInfoForCompletions.
struct argumentInfoForCompletions {
	Node* invocation = nullptr; // ast.CallLikeExpression
	int argumentIndex = 0;
	int argumentCount = 0;
};

// completions.go:5655 detailsData.
struct symbolDetails;
struct detailsData {
	symbolDetails* symbol = nullptr;
	completionData* request = nullptr;
	literalValue* literal = nullptr;
	bool cases = false; // Go `cases *struct{}` — presence flag.
};

// completions.go:5666 symbolDetails.
struct symbolDetails {
	Symbol* symbol = nullptr;
	Node* location = nullptr;
	symbolOriginInfo* origin = nullptr;
	Node* previousToken = nullptr;
	Node* contextToken = nullptr;
	jsxInitializer jsxInitializer;
	bool isTypeOnlyLocation = false;
};

// completions.go:5693 codeAction.
struct codeAction {
	std::string description;
	std::vector<lsproto::TextEdit*> changes;
};

// --- string_completions.go (types + functions) ---

// string_completions.go:37 completionsFromTypes.
struct completionsFromTypes {
	std::vector<checker::StringLiteralType*> types;
	bool isNewIdentifier = false;
};

// string_completions.go:42 completionsFromProperties.
struct completionsFromProperties {
	std::vector<Symbol*> symbols;
	bool hasIndexSignature = false;
};

// string_completions.go:47 pathCompletion.
struct pathCompletion {
	std::string name;
	lsutil::ScriptElementKind kind = lsutil::ScriptElementKindUnknown;
	std::string extension;
};

// string_completions.go:51 pathCompletions.
struct pathCompletions {
	std::vector<pathCompletion*> entries;
	lsproto::Range* replacementSpan = nullptr;
};

// string_completions.go:53 stringLiteralCompletions — `union` of the above
// three; exactly one is non-nil.
struct stringLiteralCompletions {
	completionsFromTypes* fromTypes = nullptr;
	completionsFromProperties* fromProperties = nullptr;
	pathCompletions* fromPaths = nullptr;
};

// --- languageservice.go ---

// languageservice.go:17 — `type LanguageService struct`. Implements
// sourcemap.Host (l is passed to GetDocumentPositionMapper).
// === dep decls — owned by the ls-coreB slice (findallreferences.go) ===

// ReferenceEntry (findallreferences.go:104).
struct ReferenceEntry {
	virtual ~ReferenceEntry() = default;
	virtual Node* Node() = 0;
	virtual bool IsNodeEntry() = 0;
};

// SymbolAndEntries (findallreferences.go:55).
struct SymbolAndEntries {
	virtual ~SymbolAndEntries() = default;
	virtual std::vector<ReferenceEntry*> References() = 0;
	virtual Node* DefinitionNode() = 0;
	virtual Symbol* DefinitionSymbol() = 0;
};

// SignatureUsage (findallreferences.go:1210).
struct SignatureUsage {
	Node* Name = nullptr; // The identifier reference node
	Node* Call = nullptr; // The containing call expression, or nil if not a call usage
};

class LanguageService : public sourcemap::Host {
public:
	// languageservice.go:25 NewLanguageService.
	LanguageService(autoimport::ProjectID* projectID,
	                compiler::SimpleProgram* program, ls::Host* host,
	                const std::string& activeFile);

	// languageservice.go:41 toPath.
	tspath::Path toPath(const std::string& fileName);

	// languageservice.go:45 GetProgram.
	compiler::SimpleProgram* GetProgram() { return program; }

	// languageservice.go:49 UserPreferences.
	const lsutil::UserPreferences& UserPreferences() { return activeConfig; }

	// languageservice.go:53 FormatOptions.
	lsutil::FormatCodeSettings FormatOptions() {
		return activeConfig.FormatCodeSettings;
	}

	// languageservice.go:57 tryGetProgramAndFile.
	std::pair<compiler::SimpleProgram*, SourceFile*> tryGetProgramAndFile(
	    const std::string& fileName);
	// languageservice.go:63 getProgramAndFile — panics when the file is
	// missing.
	std::pair<compiler::SimpleProgram*, SourceFile*> getProgramAndFile(
	    const lsproto::DocumentUri& documentURI);

	// languageservice.go:72 GetDocumentPositionMapper.
	sourcemap::DocumentPositionMapper* GetDocumentPositionMapper(
	    const std::string& fileName);

	// sourcemap.Host impls (languageservice.go:80-92).
	std::pair<std::string, bool> ReadFile(std::string_view fileName) override {
		return host->ReadFile(std::string(fileName));
	}
	bool UseCaseSensitiveFileNames() override {
		return host->UseCaseSensitiveFileNames();
	}
	sourcemap::ECMALineInfo* GetECMALineInfo(
	    std::string_view fileName) override {
		return host->GetECMALineInfo(std::string(fileName));
	}

	// languageservice.go:97 getPreparedAutoImportView — (nil, ErrNeedsAutoImports)
	// when the registry isn't prepared.
	std::pair<autoimport::View*, gostd::Error> getPreparedAutoImportView(
	    SourceFile* fromFile, checker::Checker* typeChecker);

	// languageservice.go:115 getCurrentAutoImportView.
	autoimport::View* getCurrentAutoImportView(SourceFile* fromFile,
	                                           checker::Checker* typeChecker);

	// languageservice.go:128 DirectoryExists — for module specifier
	// completions.
	bool DirectoryExists(const std::string& path);
	// languageservice.go:133 ReadDirectory — for module specifier
	// completions.
	std::vector<std::string> ReadDirectory(
	    const std::string& path, const std::vector<std::string>& extensions,
	    const std::vector<std::string>& includes);
	// languageservice.go:138 GetDirectories.
	std::vector<std::string> GetDirectories(const std::string& path);

	// --- api.go ---

	// api.go:18 GetSymbolAtPosition.
	std::pair<Symbol*, gostd::Error> GetSymbolAtPosition(
	    const ContextPtr& ctx, const std::string& fileName, int position);
	// api.go:31 GetSymbolAtLocation.
	Symbol* GetSymbolAtLocation(const ContextPtr& ctx, Node* node);
	// api.go:38 GetTypeOfSymbol.
	checker::Type* GetTypeOfSymbol(const ContextPtr& ctx, Symbol* symbol);

	// --- completions.go ---

	// completions.go:45 ProvideCompletion.
	std::pair<lsproto::CompletionResponse, gostd::Error> ProvideCompletion(
	    const ContextPtr& ctx, const lsproto::DocumentUri& documentURI,
	    lsproto::Position position, lsproto::CompletionContext* context);

	// completions.go:78 filterContentMappedAutoImports.
	void filterContentMappedAutoImports(const ContextPtr& ctx,
	                                    compiler::SimpleProgram* program,
	                                    SourceFile* file,
	                                    lsproto::CompletionList* list);
	// completions.go:106 GetCompletionsAtPosition — public API entry.
	std::pair<CompletionList*, gostd::Error> GetCompletionsAtPosition(
	    const ContextPtr& ctx, SourceFile* file, int position,
	    std::string* triggerCharacter, bool includeSymbols);

	// === dep decls — owned by the ls-coreB slice (findallreferences.go) ===
	// Called by the api slice; stubbed in ls.cpp until ls-coreB lands.
	std::vector<SignatureUsage> GetSignatureUsages(
	    const ContextPtr& ctx, Node* signatureDecl);
	std::vector<SymbolAndEntries*> GetReferencedSymbolsForNode(
	    const ContextPtr& ctx, int position, Node* node,
	    const std::vector<SourceFile*>& sourceFiles);
	// completions.go:402 getCompletionsAtPosition.
	std::pair<CompletionList*, gostd::Error> getCompletionsAtPosition(
	    const ContextPtr& ctx, SourceFile* file, int position,
	    std::string* triggerCharacter, bool includeSymbols);
	// completions.go:528 getCompletionData.
	std::pair<completionData, gostd::Error> getCompletionData(
	    const ContextPtr& ctx, checker::Checker* typeChecker, SourceFile* file,
	    int position, const lsutil::UserPreferences& preferences,
	    bool forItemResolve);
	// completions.go:1820 completionInfoFromData.
	std::pair<CompletionList*, gostd::Error> completionInfoFromData(
	    const ContextPtr& ctx, checker::Checker* typeChecker, SourceFile* file,
	    const CompilerOptions* compilerOptions, completionDataData* data,
	    int position, lsproto::Range* optionalReplacementSpan,
	    bool includeSymbols);
	// completions.go:1955 getCompletionEntriesFromSymbols.
	std::tuple<collections::Set<std::string>, std::vector<CompletionItem*>,
	           gostd::Error>
	getCompletionEntriesFromSymbols(const ContextPtr& ctx,
	                                checker::Checker* typeChecker,
	                                completionDataData* data,
	                                Node* replacementToken, int position,
	                                SourceFile* file,
	                                const CompilerOptions* compilerOptions,
	                                bool includeSymbols);
	// completions.go:2178 createCompletionItem.
	std::pair<lsproto::CompletionItem*, gostd::Error> createCompletionItem(
	    const ContextPtr& ctx, checker::Checker* typeChecker, Symbol* symbol,
	    SortText sortText, Node* replacementToken,
	    completionDataData* data, int position, SourceFile* file,
	    std::string name, bool needsConvertPropertyAccess,
	    symbolOriginInfo* origin, bool useSemicolons,
	    const CompilerOptions* compilerOptions, bool isMemberCompletion);
	// completions.go:2474 getEntryForObjectLiteralMethodCompletion.
	symbolOriginInfoObjectLiteralMethod*
	getEntryForObjectLiteralMethodCompletion(const ContextPtr& ctx,
	                                         checker::Checker* typeChecker,
	                                         Symbol* symbol,
	                                         Node* enclosingDeclaration,
	                                         SourceFile* file);
	// completions.go:2499 createObjectLiteralMethod.
	Node* createObjectLiteralMethod(snippetPrinter* snippetPrinter,
	                                checker::Checker* typeChecker,
	                                Symbol* symbol, Node* enclosingDeclaration,
	                                SourceFile* file, bool isSnippet);
	// completions.go:2590 collectObjectLiteralMethodSymbols.
	std::vector<objectLiteralMethodSymbol> collectObjectLiteralMethodSymbols(
	    const ContextPtr& ctx, checker::Checker* typeChecker,
	    const std::vector<Symbol*>& members, Node* enclosingDeclaration,
	    SourceFile* file);
	// completions.go:2624 printObjectLiteralMethodLabelDetail.
	std::string printObjectLiteralMethodLabelDetail(Node* method,
	                                                SourceFile* file,
	                                                NodeFactory* factory);
	// completions.go:2643 getEntryForMemberCompletion.
	std::pair<memberCompletionEntry*, gostd::Error> getEntryForMemberCompletion(
	    const ContextPtr& ctx, checker::Checker* typeChecker, Symbol* symbol,
	    const std::string& name, Node* location, int position,
	    Node* contextToken, SourceFile* file);
	// completions.go:2766 getPresentMemberModifiers.
	presentMemberModifiers getPresentMemberModifiers(Node* contextToken,
	                                                 SourceFile* file,
	                                                 int position);
	// completions.go:2856 createImportAdder.
	std::pair<autoimport::ImportAdder*, gostd::Error> createImportAdder(
	    const ContextPtr& ctx, checker::Checker* typeChecker, SourceFile* file);
	// completions.go:3664 getReplacementRangeForContextToken.
	lsproto::Range* getReplacementRangeForContextToken(SourceFile* file,
	                                                 Node* contextToken,
	                                                 int position);
	// completions.go:3692 createRangeFromStringLiteralLikeContent.
	lsproto::Range* createRangeFromStringLiteralLikeContent(
	    SourceFile* file, Node* node, int position);
	// completions.go:4840 setItemDefaults.
	lsproto::CompletionItemDefaults* setItemDefaults(
	    const ContextPtr& ctx, int position, SourceFile* file,
	    std::vector<CompletionItem*> items,
	    std::vector<std::string>* defaultCommitCharacters,
	    lsproto::Range* optionalReplacementSpan);
	// completions.go:4890 specificKeywordCompletionInfo.
	CompletionList* specificKeywordCompletionInfo(
	    const ContextPtr& ctx, int position, SourceFile* file,
	    std::vector<CompletionItem*> items, bool isNewIdentifierLocation,
	    lsproto::Range* optionalReplacementSpan);
	// completions.go:4913 getJsxClosingTagCompletion.
	CompletionList* getJsxClosingTagCompletion(const ContextPtr& ctx,
	                                          Node* location, SourceFile* file,
	                                          int position);
	// completions.go:5023 createLSPCompletionItem.
	lsproto::CompletionItem* createLSPCompletionItem(
	    const ContextPtr& ctx, const std::string& name,
	    const std::string& insertText, const std::string& filterText,
	    const SortText& sortText, lsutil::ScriptElementKind elementKind,
	    lsutil::ScriptElementKindModifier kindModifiers,
	    lsproto::Range* replacementSpan,
	    std::vector<std::string>* commitCharacters,
	    lsproto::CompletionItemLabelDetails* labelDetails, SourceFile* file,
	    int position, bool isMemberCompletion, bool isSnippet, bool hasAction,
	    bool preselect, const std::string& source,
	    lsproto::AutoImportFix* autoImportFix,
	    std::vector<lsproto::TextEdit*>* additionalTextEdits,
	    std::string* detail);
	// completions.go:5119 getLabelCompletionsAtPosition.
	CompletionList* getLabelCompletionsAtPosition(
	    const ContextPtr& ctx, Node* node, SourceFile* file, int position,
	    lsproto::Range* optionalReplacementSpan);
	// completions.go:5143 getLabelStatementCompletions.
	std::vector<CompletionItem*> getLabelStatementCompletions(
	    const ContextPtr& ctx, Node* node, SourceFile* file, int position);
	// completions.go:4062 getJSCompletionEntries.
	std::vector<CompletionItem*> getJSCompletionEntries(
	    const ContextPtr& ctx, SourceFile* file, int position,
	    collections::Set<std::string>* uniqueNames,
	    std::vector<CompletionItem*> sortedEntries);
	// completions.go:4079 getOptionalReplacementSpan.
	lsproto::Range* getOptionalReplacementSpan(Node* location,
	                                           SourceFile* file);
	// completions.go:5489 ResolveCompletionItem.
	std::pair<lsproto::CompletionItem*, gostd::Error> ResolveCompletionItem(
	    const ContextPtr& ctx, lsproto::CompletionItem* item,
	    lsproto::CompletionItemData* data);
	// completions.go:5513 getCompletionItemDetails.
	lsproto::CompletionItem* getCompletionItemDetails(
	    const ContextPtr& ctx, compiler::SimpleProgram* program,
	    checker::Checker* checker, int position, SourceFile* file,
	    lsproto::CompletionItem* item, lsproto::CompletionItemData* data);
	// completions.go:5637 getSymbolCompletionFromItemData.
	detailsData getSymbolCompletionFromItemData(
	    const ContextPtr& ctx, checker::Checker* ch, SourceFile* file,
	    int position, lsproto::CompletionItemData* itemData);
	// completions.go:5727 createCompletionDetailsForSymbol.
	lsproto::CompletionItem* createCompletionDetailsForSymbol(
	    lsproto::CompletionItem* item, Symbol* symbol,
	    checker::Checker* checker, Node* location, int position,
	    lsproto::MarkupKind docFormat);
	// completions.go:5746 getImportStatementCompletionInfo.
	importStatementCompletionInfo getImportStatementCompletionInfo(
	    Node* contextToken, SourceFile* sourceFile);
	// completions.go:5806 getSingleLineReplacementSpanForImportCompletionNode.
	lsproto::Range* getSingleLineReplacementSpanForImportCompletionNode(
	    Node* node);
	// completions.go:5983 jsDocCompletionInfo.
	CompletionList* jsDocCompletionInfo(const ContextPtr& ctx, int position,
	                                    SourceFile* file,
	                                    std::vector<CompletionItem*> items);
	// completions.go:6568 getExhaustiveCaseSnippets.
	std::pair<lsproto::CompletionItem*, gostd::Error>
	getExhaustiveCaseSnippets(const ContextPtr& ctx, Node* caseBlock,
	                        SourceFile* file, int position,
	                        const CompilerOptions* options,
	                        compiler::SimpleProgram* program,
	                        checker::Checker* c);

	// hover.go:174 getQuickInfoAndDocumentationForSymbol — dep-stub
	// (body in lsdeps.cpp; owned by the hover sibling slice).
	// Returns (quickInfo, documentation, tags, classifiedRuns).
	std::tuple<std::string, std::string, std::string,
	           std::vector<lsproto::VSClassifiedTextRun*>>
	getQuickInfoAndDocumentationForSymbol(
	    checker::Checker* c, Symbol* symbol, Node* node,
	    lsproto::MarkupKind contentFormat, checker::VerbosityContext* vc,
	    bool vsCapability);

	// --- string_completions.go ---

	// string_completions.go:57 getStringLiteralCompletions.
	CompletionList* getStringLiteralCompletions(
	    const ContextPtr& ctx, SourceFile* sourceFile, int position,
	    Node* previousToken, checker::Checker* typeChecker,
	    const CompilerOptions* compilerOptions, bool includeSymbols);
	// string_completions.go:95 convertStringLiteralCompletions.
	CompletionList* convertStringLiteralCompletions(
	    const ContextPtr& ctx, stringLiteralCompletions* completion,
	    SourceFile* sourceFile, checker::Checker* typeChecker,
	    const CompilerOptions* options, Node* contextToken, int position,
	    bool includeSymbols);
	// string_completions.go:206 convertPathCompletions.
	CompletionList* convertPathCompletions(
	    const ContextPtr& ctx, pathCompletions* pathCompletions,
	    SourceFile* sourceFile, int position);
	// string_completions.go:263 getStringLiteralCompletionEntries.
	stringLiteralCompletions* getStringLiteralCompletionEntries(
	    const ContextPtr& ctx, SourceFile* file, Node* node, int position,
	    checker::Checker* typeChecker);
	// string_completions.go:587 getStringLiteralCompletionsFromModuleNames.
	stringLiteralCompletions* getStringLiteralCompletionsFromModuleNames(
	    SourceFile* file, Node* node, compiler::SimpleProgram* program,
	    checker::Checker* checker);
	// string_completions.go:622 pathCompletionReplacementSpan.
	std::pair<lsproto::Range*, bool> pathCompletionReplacementSpan(
	    SourceFile* file, TextRange* textRange);
	// string_completions.go:663 getStringLiteralCompletionsFromModuleNamesWorker.
	std::vector<moduleCompletionNameAndKind>
	getStringLiteralCompletionsFromModuleNamesWorker(
	    SourceFile* file, Node* node, compiler::SimpleProgram* program,
	    checker::Checker* checker);
	// string_completions.go:707 getCompletionEntriesForNonRelativeModules.
	std::vector<moduleCompletionNameAndKind>
	getCompletionEntriesForNonRelativeModules(
	    const std::string& fragment, const std::string& scriptPath,
	    ResolutionMode mode, compiler::SimpleProgram* program,
	    checker::Checker* typeChecker, extensionOptions* extensionOptions);
	// string_completions.go:931 getCompletionEntriesFromTypings.
	void getCompletionEntriesFromTypings(
	    compiler::SimpleProgram* program, const std::string& scriptPath,
	    const std::string& fragmentDirectory,
	    extensionOptions* extensionOptions,
	    moduleCompletionNameAndKindSet* result);
	// string_completions.go:955 getCompletionEntriesFromTypingsDirectories.
	void getCompletionEntriesFromTypingsDirectories(
	    const std::string& directory, const CompilerOptions* options,
	    const std::string& fragmentDirectory,
	    extensionOptions* extensionOptions, compiler::SimpleProgram* program,
	    std::unordered_map<std::string, bool>* seen,
	    moduleCompletionNameAndKindSet* result);
	// string_completions.go:1011 enumerateNodeModulesVisibleToScript.
	std::vector<std::string> enumerateNodeModulesVisibleToScript(
	    const std::string& scriptPath);
	// string_completions.go:1032 getExtensionOptions.
	ls::extensionOptions* getExtensionOptions(
	    const CompilerOptions* options, referenceKind refKind,
	    SourceFile* file, ResolutionMode mode, checker::Checker* checker);
	// string_completions.go:1084 getCompletionEntriesForRelativeModules.
	std::vector<moduleCompletionNameAndKind>
	getCompletionEntriesForRelativeModules(
	    const std::string& literalValue, const std::string& scriptDirectory,
	    compiler::SimpleProgram* program, tspath::Path scriptPath,
	    extensionOptions* extensionOptions);
	// string_completions.go:1115 getCompletionEntriesForDirectoryFragmentWithRootDirs.
	std::vector<moduleCompletionNameAndKind>
	getCompletionEntriesForDirectoryFragmentWithRootDirs(
	    const std::vector<std::string>& rootDirs,
	    const std::string& fragment, const std::string& scriptDirectory,
	    compiler::SimpleProgram* program, const std::string& exclude,
	    extensionOptions* extensionOptions);
	// string_completions.go:1272 getCompletionEntriesForDirectoryFragment.
	moduleCompletionNameAndKindSet* getCompletionEntriesForDirectoryFragment(
	    const std::string& fragment, const std::string& scriptDirectory,
	    extensionOptions* extensionOptions,
	    compiler::SimpleProgram* program, bool moduleSpecifierIsRelative,
	    const std::string& exclude,
	    moduleCompletionNameAndKindSet* result);
	// string_completions.go:1373 addCompletionEntriesFromPaths.
	bool addCompletionEntriesFromPaths(
	    moduleCompletionNameAndKindSet* result,
	    compiler::SimpleProgram* program, const std::string& fragment,
	    const std::string& baseDirectory,
	    extensionOptions* extensionOptions,
	    const std::vector<std::pair<std::string,
	                                std::vector<std::string>>>* paths);
	// string_completions.go:1413 addCompletionEntriesFromPathsOrExportsOrImports.
	bool addCompletionEntriesFromPathsOrExportsOrImports(
	    moduleCompletionNameAndKindSet* result,
	    compiler::SimpleProgram* program, bool isExports, bool isImports,
	    const std::string& fragment, const std::string& baseDirectory,
	    extensionOptions* extensionOptions,
	    const std::vector<std::string>& keys,
	    const std::function<std::vector<std::string>(const std::string&)>&
	        getPatternsForKey,
	    const std::function<int(const std::string&, const std::string&)>&
	        comparePaths);
	// string_completions.go:1500 getCompletionsForPathMapping.
	std::vector<moduleCompletionNameAndKind> getCompletionsForPathMapping(
	    const std::string& path, const std::vector<std::string>& patterns,
	    const std::string& fragment, const std::string& packageDirectory,
	    bool isExports, bool isImports, extensionOptions* extensionOptions,
	    compiler::SimpleProgram* program);
	// string_completions.go:1611 getModulesForPathsPattern.
	std::vector<moduleCompletionNameAndKind> getModulesForPathsPattern(
	    const std::string& fragment, const std::string& packageDirectory,
	    const std::string& pattern, bool isExports, bool isImports,
	    extensionOptions* extensionOptions,
	    compiler::SimpleProgram* program);
	// string_completions.go:2072 stringLiteralCompletionDetails.
	lsproto::CompletionItem* stringLiteralCompletionDetails(
	    lsproto::CompletionItem* item, const std::string& name,
	    Node* location, int position, stringLiteralCompletions* completion,
	    SourceFile* file, checker::Checker* checker,
	    lsproto::MarkupKind docFormat);
	// string_completions.go:2188 getTripleSlashReferenceCompletions.
	pathCompletions* getTripleSlashReferenceCompletions(
	    SourceFile* file, int position, compiler::SimpleProgram* program,
	    checker::Checker* checker);
	// string_completions.go:2046 getStringLiteralCompletionDetails (method).
	lsproto::CompletionItem* getStringLiteralCompletionDetails(
	    const ContextPtr& ctx, checker::Checker* typeChecker,
	    lsproto::CompletionItem* item, const std::string& name,
	    SourceFile* file, int position, Node* contextToken,
	    lsproto::MarkupKind docFormat);

	// --- jsdoc_snippet.go ---

// jsdoc_snippet.go:29 getJSDocSnippetCompletion.
	CompletionList* getJSDocSnippetCompletion(const ContextPtr& ctx,
	                                          SourceFile* file, int position);
	// jsdoc_snippet.go:87 getJSDocSnippetCompletionRange.
	lsproto::TextEditOrInsertReplaceEdit* getJSDocSnippetCompletionRange(
	    const ContextPtr& ctx, SourceFile* file, int position,
	    const std::string& newText);

	// --- utilities.go ---

	// utilities.go:288 createLspRangeFromNode.
	std::pair<lsproto::Range, spanmap::Fidelity> createLspRangeFromNode(
	    Node* node, SourceFile* file);
	// utilities.go:292 createLspRangeFromNodeForFeature.
	std::pair<lsproto::Range, spanmap::Fidelity> createLspRangeFromNodeForFeature(
	    Node* node, SourceFile* file, spanmap::Feature feature);
	// utilities.go:300 createLspRangeFromBounds.
	std::pair<lsproto::Range, spanmap::Fidelity> createLspRangeFromBounds(
	    int start, int end, SourceFile* file);
	// utilities.go:304 createLspRangeFromRange — `script lsconv.Script` is a
	// Go interface ⇒ template on the lsconv::Script concept.
	template <lsconv::Script T>
	std::pair<lsproto::Range, spanmap::Fidelity> createLspRangeFromRange(
	    TextRange textRange, T script) {
		return converters->ToLSPRange(script, textRange);
	}
	// utilities.go:308 createLspPosition.
	std::pair<lsproto::Position, spanmap::Fidelity> createLspPosition(
	    int position, SourceFile* file);

	// internals shared across this slice's files are declared below in the
	// `ls-internals` section — appended during the port of each Go file.

	autoimport::ProjectID* projectID() { return projectID_; }

private:
	autoimport::ProjectID* projectID_;
	ls::Host* host;
	lsutil::UserPreferences activeConfig;
	compiler::SimpleProgram* program;
	lsconv::Converters* converters;
	std::unordered_map<std::string, sourcemap::DocumentPositionMapper*>
	    documentPositionMappers;
};

// jsdoc_snippet.go:74 isPotentiallyValidJSDocSnippetCompletionPosition —
// body lives in jsdoc_snippet.cpp.
bool isPotentiallyValidJSDocSnippetCompletionPosition(SourceFile* file,
                                                    int position);

// --- api.go sentinels ---
extern const gostd::Error ErrNoSourceFile;      // api.go:13
extern const gostd::Error ErrNoTokenAtPosition; // api.go:14
// completions.go:35
extern const gostd::Error ErrNeedsAutoImports;

// --- constants.go ---
inline constexpr int moduleSpecifierResolutionLimit = 100;             // :4
inline constexpr int moduleSpecifierResolutionCacheAttemptLimit = 1000; // :5

// --- jsdoc.go ---

// JSDocTagInfo mirrors Strada's `JSDocTagInfo`, but renders the tag's text as
// a plain string instead of `SymbolDisplayPart[]`. jsdoc.go:18
struct JSDocTagInfo {
	std::string Name;
	std::string Text;
};

// GetSymbolDocumentationComment renders a symbol's documentation comment as
// plain text. It backs the API's Symbol.getDocumentationComment and mirrors
// Strada's getJsDocCommentsFromDeclarations: comments are gathered from each
// unique declaration, deduplicated, and joined with line breaks. Like Strada,
// it does not resolve aliases — consumers resolve aliases themselves (via
// getAliasedSymbol) and re-query if desired. jsdoc.go:28
std::string GetSymbolDocumentationComment(checker::Checker* c, Symbol* symbol);

// GetSymbolJSDocTags collects a symbol's JSDoc tags. It backs the API's
// Symbol.getJsDocTags and mirrors Strada's getJsDocTagsFromDeclarations,
// except each tag's text is rendered as a plain string rather than
// SymbolDisplayPart[]. Tags with no text have an empty Text field. jsdoc.go:51
std::vector<JSDocTagInfo> GetSymbolJSDocTags(Symbol* symbol);

// hover.go:204 documentationLocationMapper — declared here because jsdoc.go's
// noMappedLocation implements it (and sibling files consume it).
using documentationLocationMapper =
    std::function<std::pair<lsproto::Location, spanmap::Fidelity>(
        SourceFile*, TextRange)>;

// jsdoc.go:310 noMappedLocation.
std::pair<lsproto::Location, spanmap::Fidelity>
noMappedLocation(SourceFile* file, TextRange range);

// jsdoc.go:168 getJSDocOrTag.
Node* getJSDocOrTag(checker::Checker* c, Node* node,
                    collections::Set<Symbol*>* seenSymbols);

// dep-stub — owned by hover.go (ls sibling slice). jsdoc.go:41 calls it.
std::string getDocumentationFromDeclaration(
    documentationLocationMapper getMappedLocation, checker::Checker* c,
    Symbol* symbol, Node* declaration, Node* location,
    lsproto::MarkupKind contentFormat, bool commentOnly);

// --- utilities.go ---

// utilities.go:28 IsInString.
bool IsInString(SourceFile* sourceFile, int position, Node* previousToken);
// utilities.go:48 isModuleSpecifierLike.
bool isModuleSpecifierLike(Node* node);
// utilities.go:62 getNonModuleSymbolOfMergedModuleSymbol.
Symbol* getNonModuleSymbolOfMergedModuleSymbol(Symbol* symbol);
// utilities.go:73 getLocalSymbolForExportSpecifier.
Symbol* getLocalSymbolForExportSpecifier(Node* referenceLocation,
                                         Symbol* referenceSymbol,
                                         ExportSpecifier* exportSpecifier,
                                         checker::Checker* ch);
// utilities.go:82 isExportSpecifierAlias.
bool isExportSpecifierAlias(Node* referenceLocation,
                            ExportSpecifier* exportSpecifier);
// utilities.go:95 isInComment.
CommentRange* isInComment(SourceFile* file, int position,
                          Node* tokenAtPosition);
// utilities.go:99 positionBelongsToNode.
bool positionBelongsToNode(Node* candidate, int position, SourceFile* file);

// utilities.go:103 PossibleTypeArgumentInfo.
struct PossibleTypeArgumentInfo {
	Node* called = nullptr;
	int nTypeArguments = 0;
};

// utilities.go:109 getPossibleTypeArgumentsInfo.
PossibleTypeArgumentInfo* getPossibleTypeArgumentsInfo(Node* tokenIn,
                                                       SourceFile* sourceFile);
// utilities.go:191 isNameOfModuleDeclaration.
bool isNameOfModuleDeclaration(Node* node);
// utilities.go:198 isExpressionOfExternalModuleImportEqualsDeclaration.
bool isExpressionOfExternalModuleImportEqualsDeclaration(Node* node);
// utilities.go:202 isNamespaceReference.
bool isNamespaceReference(Node* node);
// utilities.go:206 isQualifiedNameNamespaceReference.
bool isQualifiedNameNamespaceReference(Node* node);
// utilities.go:220 isPropertyAccessNamespaceReference.
bool isPropertyAccessNamespaceReference(Node* node);
// utilities.go:240 isThis.
bool isThis(Node* node);
// utilities.go:253 isTypeReference.
bool isTypeReference(Node* node);
// utilities.go:277 isInRightSideOfInternalImportEqualsDeclaration.
bool isInRightSideOfInternalImportEqualsDeclaration(Node* node);
// utilities.go:296 createRangeFromNode.
TextRange createRangeFromNode(Node* node, SourceFile* file);
// utilities.go:312 quote.
std::string quote(SourceFile* file, const lsutil::UserPreferences& preferences,
                  const std::string& text);
// utilities.go:345 isTypeKeyword.
bool isTypeKeyword(Kind kind);
// utilities.go:349 isSeparator.
bool isSeparator(Node* node, Node* candidate);
// utilities.go:353 isLiteralNameOfPropertyDeclarationOrIndexAccess.
bool isLiteralNameOfPropertyDeclarationOrIndexAccess(Node* node);
// utilities.go:377 isObjectBindingElementWithoutPropertyName.
bool isObjectBindingElementWithoutPropertyName(Node* bindingElement);
// utilities.go:384 isRightSideOfPropertyAccess.
bool isRightSideOfPropertyAccess(Node* node);
// utilities.go:388 isStaticSymbol.
bool isStaticSymbol(Symbol* symbol);
// utilities.go:396 isImplementation.
bool isImplementation(Node* node);
// utilities.go:409 isImplementationExpression.
bool isImplementationExpression(Node* node);
// utilities.go:420 isReadonlyTypeOperator.
bool isReadonlyTypeOperator(Node* node);
// utilities.go:424 isJumpStatementTarget.
bool isJumpStatementTarget(Node* node);
// utilities.go:428 isLabelOfLabeledStatement.
bool isLabelOfLabeledStatement(Node* node);
// utilities.go:432 findReferenceInPosition.
FileReference* findReferenceInPosition(
    const std::vector<FileReference*>& refs, int pos);
// utilities.go:436 getContainingNodeIfInHeritageClause.
Node* getContainingNodeIfInHeritageClause(Node* node);
// utilities.go:448 getContainerNode.
Node* getContainerNode(Node* node);
// utilities.go:459 getAdjustedLocation.
Node* getAdjustedLocation(Node* node, bool forRename, SourceFile* sourceFile);
// utilities.go:696 getAdjustedLocationForDeclaration.
Node* getAdjustedLocationForDeclaration(Node* node, bool forRename,
                                        SourceFile* sourceFile);
// utilities.go:720 getAdjustedLocationForImportDeclaration.
Node* getAdjustedLocationForImportDeclaration(ImportDeclaration* node,
                                              bool forRename);
// utilities.go:763 getAdjustedLocationForExportDeclaration.
Node* getAdjustedLocationForExportDeclaration(ExportDeclaration* node,
                                              bool forRename);
// utilities.go:791 symbolFlagsHaveMeaning.
bool symbolFlagsHaveMeaning(SymbolFlags flags, SemanticMeaning meaning);
// utilities.go:807 getMeaningFromLocation.
SemanticMeaning getMeaningFromLocation(Node* node);
// utilities.go:846 getMeaningFromDeclaration.
SemanticMeaning getMeaningFromDeclaration(Node* node);
// utilities.go:881 getIntersectingMeaningFromDeclarations.
SemanticMeaning getIntersectingMeaningFromDeclarations(
    Node* node, Symbol* symbol, SemanticMeaning defaultMeaning);
// utilities.go:922 getAllSuperTypeNodes.
std::vector<Node*> getAllSuperTypeNodes(Node* node);
// utilities.go:935 getParentSymbolsOfPropertyAccess.
std::vector<Symbol*> getParentSymbolsOfPropertyAccess(Node* location,
                                                    Symbol* symbol,
                                                    checker::Checker* ch);
// utilities.go:963 getPropertySymbolsFromBaseTypes.
Symbol* getPropertySymbolsFromBaseTypes(
    Symbol* symbol, const std::string& propertyName, checker::Checker* c,
    const std::function<Symbol*(Symbol*)>& cb);
// utilities.go:996 getPropertySymbolFromBindingElement.
Symbol* getPropertySymbolFromBindingElement(checker::Checker* ch,
                                            Node* bindingElement);
// utilities.go:1003 getPropertySymbolOfObjectBindingPatternWithoutPropertyName.
Symbol* getPropertySymbolOfObjectBindingPatternWithoutPropertyName(
    Symbol* symbol, checker::Checker* ch);
// utilities.go:1011 getTargetLabel.
Node* getTargetLabel(Node* referenceNode, const std::string& labelName);
// utilities.go:1022 skipConstraint.
checker::Type* skipConstraint(checker::Type* t, checker::Checker* typeChecker);

// utilities.go:1044 caseClauseTracker.
struct caseClauseTracker {
	virtual ~caseClauseTracker() = default;
	virtual void addValue(const checker::LiteralValue& value) = 0;
	virtual bool hasValue(const checker::LiteralValue& value) = 0;
};

// utilities.go:1073 newCaseClauseTracker.
caseClauseTracker* newCaseClauseTracker(checker::Checker* typeChecker,
                                        const std::vector<Node*>& clauses);

// utilities.go:1105 RangeContainsRange.
bool RangeContainsRange(const TextRange& r1, const TextRange& r2);
// utilities.go:1109 startEndContainsRange.
bool startEndContainsRange(int start, int end, const TextRange& textRange);
// utilities.go:1113 getPossibleGenericSignatures.
std::vector<checker::Signature*> getPossibleGenericSignatures(
    Node* called, int typeArgumentCount, checker::Checker* c);
// utilities.go:1129 removeOptionality.
checker::Type* removeOptionality(checker::Type* t, bool isOptionalExpression,
                                 bool isOptionalChain, checker::Checker* c);
// utilities.go:1138 isNoSubstitutionTemplateLiteral.
bool isNoSubstitutionTemplateLiteral(Node* node);
// utilities.go:1142 isTaggedTemplateExpression.
bool isTaggedTemplateExpression(Node* node);
// utilities.go:1146 isInsideTemplateLiteral.
bool isInsideTemplateLiteral(Node* node, int position, SourceFile* sourceFile);
// utilities.go:1151 isTemplateHead.
bool isTemplateHead(Node* node);
// utilities.go:1155 isTemplateTail.
bool isTemplateTail(Node* node);
// utilities.go:1159 findPrecedingMatchingToken.
Node* findPrecedingMatchingToken(Node* token, Kind matchingTokenKind,
                                 SourceFile* sourceFile);
// utilities.go:1195 findContainingList.
NodeList* findContainingList(Node* node, SourceFile* file);
// utilities.go:1212 getLeadingCommentRangesOfNode — Go returns iter.Seq;
// callers only range over it, so a vector keeps the same call sites.
std::vector<CommentRange> getLeadingCommentRangesOfNode(Node* node,
                                                        SourceFile* file);
// utilities.go:1220 getChildrenFromNonJSDocNode.
std::vector<Node*> getChildrenFromNonJSDocNode(Node* node,
                                               SourceFile* sourceFile);
// utilities.go:1261 getContainingObjectLiteralElement.
Node* getContainingObjectLiteralElement(Node* node);
// utilities.go:1269 getContainingObjectLiteralElementWorker.
Node* getContainingObjectLiteralElementWorker(Node* node);
// utilities.go:1287 isObjectLiteralOrJsxElement.
bool isObjectLiteralOrJsxElement(Node* node);
// utilities.go:1292 nodeSeenTracker.
std::function<bool(Node*)> nodeSeenTracker();
// utilities.go:1300 toContextRange.
TextRange* toContextRange(TextRange* textRange, SourceFile* contextFile,
                          Node* context);

// findallreferences.go:48 refInfo — owned by findallreferences.go (sibling
// slice); declared here because utilities.go:1312 getReferenceAtPosition
// returns it.
struct refInfo {
	SourceFile* file = nullptr;
	std::string fileName;
	FileReference* reference = nullptr;
	bool unverified = false;
};

// utilities.go:1312 getReferenceAtPosition.
refInfo* getReferenceAtPosition(SourceFile* sourceFile, int position,
                                compiler::SimpleProgram* program);
// utilities.go:1362 getContextualTypeFromParent.
checker::Type* getContextualTypeFromParent(Node* node,
                                           checker::Checker* typeChecker,
                                           checker::ContextFlags contextFlags);
// utilities.go:1381 getContextualTypeFromParentOrAncestorTypeNode.
checker::Type* getContextualTypeFromParentOrAncestorTypeNode(
    Node* node, checker::Checker* typeChecker);
// utilities.go:1398 getAncestorTypeNode.
Node* getAncestorTypeNode(Node* node);
// utilities.go:1409 isSourceFileWithGlobalExports.
bool isSourceFileWithGlobalExports(Node* node);

// dep-stubs — owned by sibling ls files.

// format.go:257 getRangeOfEnclosingComment — ls/format.go (sibling slice);
// utilities.go:95 isInComment calls it.
CommentRange* getRangeOfEnclosingComment(SourceFile* file, int position,
                                         Node* precedingToken,
                                         Node* tokenAtPosition);
// findallreferences.go:335 getRangeOfNode — sibling slice;
// utilities.go:1300 toContextRange calls it.
TextRange getRangeOfNode(Node* node, SourceFile* sourceFile, Node* endNode);

// --- completions.go (cross-file helpers) ---

// completions.go:5427 clientSupportsItemSnippet.
bool clientSupportsItemSnippet(const ContextPtr& ctx);
// completions.go:5431 clientSupportsItemCommitCharacters.
bool clientSupportsItemCommitCharacters(const ContextPtr& ctx);
// completions.go:5435 clientSupportsItemInsertReplace.
bool clientSupportsItemInsertReplace(const ContextPtr& ctx);
// completions.go:3734 escapeSnippetText.
std::string escapeSnippetText(const std::string& text);
// completions.go:3028 getLineOfPosition.
int getLineOfPosition(SourceFile* file, int pos);
// completions.go:3033 getLineEndOfPosition.
int getLineEndOfPosition(SourceFile* file, int pos);
// completions.go:3557 getSwitchedType — defined in completions.cpp;
// utilities.go:1362 getContextualTypeFromParent calls it.
checker::Type* getSwitchedType(Node* caseClause,
                               checker::Checker* typeChecker);
// completions.go:3561 isEqualityOperatorKind.
bool isEqualityOperatorKind(Kind kind);

// --- displaypartswriter.go ---

// displayPartsWriter implements EmitTextWriter and captures classified text
// runs for VS colorized labels, while also building a plain string.
// When vsCapability is false, only the plain string is built; runs are
// skipped. displaypartswriter.go:19
class displayPartsWriter : public printer::EmitTextWriter {
public:
	// displaypartswriter.go:26 newDisplayPartsWriter.
	displayPartsWriter(bool vsCapability) : vsCapability_(vsCapability) {}

	// displaypartswriter.go:45 WriteClassified — writes text with an
	// explicit classification type.
	void WriteClassified(const std::string& text,
	                     lsproto::ClassificationTypeName classification);
	// displaypartswriter.go:50 WriteFrom — copies the accumulated content
	// from another displayPartsWriter.
	void WriteFrom(displayPartsWriter* other);
	// displaypartswriter.go:60 GetRuns.
	std::vector<lsproto::VSClassifiedTextRun*>& GetRuns() { return runs; }
	const std::vector<lsproto::VSClassifiedTextRun*>& GetRuns() const {
		return runs;
	}

	// --- printer::EmitTextWriter ---
	std::string String() override { return builder; }
	void Clear() override {
		lastWritten.clear();
		builder.clear();
		runs.clear();
	}
	void DecreaseIndent() override {}
	TextPos GetColumn() override { return 0; }
	int GetIndent() override { return 0; }
	int GetLine() override { return 0; }
	int GetTextPos() override { return static_cast<int>(builder.size()); }
	bool HasTrailingComment() override { return false; }
	bool HasTrailingWhitespace() override;
	void IncreaseIndent() override {}
	bool IsAtStartOfLine() override { return false; }
	void RawWrite(const std::string& s) override {
		addRun(lsproto::ClassificationTypeNameText, s);
	}
	void Write(const std::string& s) override {
		addRun(lsproto::ClassificationTypeNameText, s);
	}
	void WriteComment(const std::string& text) override;
	void WriteKeyword(const std::string& text) override {
		addRun(lsproto::ClassificationTypeNameKeyword, text);
	}
	void WriteLine() override {
		addRun(lsproto::ClassificationTypeNameWhiteSpace, " ");
	}
	void WriteLineForce(bool force) override {
		addRun(lsproto::ClassificationTypeNameWhiteSpace, " ");
	}
	void WriteLiteral(const std::string& s) override {
		// Strada's writeLiteral → SymbolDisplayPartKind.stringLiteral →
		// "string"
		addRun(lsproto::ClassificationTypeNameString, s);
	}
	void WriteOperator(const std::string& text) override {
		addRun(lsproto::ClassificationTypeNameOperator, text);
	}
	void WriteParameter(const std::string& text) override {
		addRun(lsproto::ClassificationTypeNameParameterName, text);
	}
	void WriteProperty(const std::string& text) override {
		addRun(lsproto::ClassificationTypeNamePropertyName, text);
	}
	void WritePunctuation(const std::string& text) override {
		addRun(lsproto::ClassificationTypeNamePunctuation, text);
	}
	void WriteSpace(const std::string& text) override {
		addRun(lsproto::ClassificationTypeNameWhiteSpace, text);
	}
	void WriteStringLiteral(const std::string& text) override {
		addRun(lsproto::ClassificationTypeNameString, text);
	}
	void WriteSymbol(const std::string& text, Symbol* symbol) override;
	void WriteTrailingSemicolon(const std::string& text) override {
		addRun(lsproto::ClassificationTypeNamePunctuation, text);
	}

private:
	// displaypartswriter.go:30 addRun.
	void addRun(lsproto::ClassificationTypeName classification,
	            const std::string& text);

	std::string builder;
	std::vector<lsproto::VSClassifiedTextRun*> runs;
	bool vsCapability_ = false;
	std::string lastWritten;
};

// displaypartswriter.go:26 newDisplayPartsWriter.
inline displayPartsWriter* newDisplayPartsWriter(bool vsCapability) {
	return new displayPartsWriter(vsCapability);
}

// --- completions.go (functions) ---

// completions.go:376 cloneItems.
std::vector<CompletionItem*> cloneItems(
    const std::vector<lsproto::CompletionItem*>& items);
// completions.go:139 supplementalFileIndex.
int32_t* supplementalFileIndex(SourceFile* file);
// completions.go:152 sourceFileForSupplementalFileIndex.
SourceFile* sourceFileForSupplementalFileIndex(SourceFile* file,
                                             int32_t* index);
// completions.go:384 toLSP — CompletionList method.
// (declared on CompletionList below via member decl)
// completions.go:182 getRelevantTokens... (see below)
// completions.go:1802 keywordCompletionData.
completionDataKeyword* keywordCompletionData(
    KeywordCompletionFilters keywordFilters, bool filterOutTSOnlyKeywords,
    bool isNewIdentifierLocation);
// completions.go:1813 getDefaultCommitCharacters.
std::vector<std::string> getDefaultCommitCharacters(
    bool isNewIdentifierLocation);
// completions.go:2126 completionNameForLiteral.
std::string completionNameForLiteral(
    SourceFile* file, const lsutil::UserPreferences& preferences,
    const literalValue& literal);
// completions.go:2143 getInsertTextAndReplacementSpanForImportCompletion.
std::pair<std::string, lsproto::Range*>
getInsertTextAndReplacementSpanForImportCompletion(
    autoimport::Fix* fix, lsproto::ImportKind importKind,
    importStatementCompletionInfo* importStatementCompletion,
    bool useSemicolons, SourceFile* file,
    const lsutil::UserPreferences& preferences, bool isSnippet);
// completions.go:2165 createCompletionItemForLiteral.
lsproto::CompletionItem* createCompletionItemForLiteral(
    SourceFile* file, const lsutil::UserPreferences& preferences,
    const literalValue& literal);
// completions.go:2474 memberCompletionEntry (type above).
// completions.go:2573 isObjectLiteralMethodCompletionCandidateDeclaration.
bool isObjectLiteralMethodCompletionCandidateDeclaration(Node* declaration);
// completions.go:2620 isObjectLiteralMethodSymbol.
bool isObjectLiteralMethodSymbol(Symbol* symbol);
// completions.go:2819 modifierLikeKind.
Kind modifierLikeKind(Node* node);
// completions.go:2835 createModifierList.
ModifierList* createModifierList(NodeFactory* factory, ModifierFlags flags,
                                 const std::vector<Node*>& decorators);
// completions.go:2847 createSnippetTabStopBody.
Node* createSnippetTabStopBody(NodeFactory* factory,
                               printer::EmitContext* emitContext);
// completions.go:2870 isRecommendedCompletionMatch.
bool isRecommendedCompletionMatch(Symbol* localSymbol,
                                  Symbol* recommendedCompletion,
                                  checker::Checker* typeChecker);
// completions.go:2883 getWordLengthAndStart.
std::pair<int, char32_t> getWordLengthAndStart(SourceFile* sourceFile,
                                             int position);
// completions.go:2906 trimElementAccess.
std::string trimElementAccess(const std::string& text);
// completions.go:2919 getFilterText.
std::string getFilterText(SourceFile* file, int position,
                          const std::string& insertText,
                          const std::string& label, char32_t wordStart,
                          const std::string& dotAccessor);
// completions.go:2993 getDotAccessor.
std::string getDotAccessor(SourceFile* file, int position);
// completions.go:3007 strPtrIsEmpty.
bool strPtrIsEmpty(const std::string* ptr);
// completions.go:3014 strPtrTo.
std::string* strPtrTo(const std::string& v);
// completions.go:3021 boolToPtr.
bool* boolToPtr(bool v);
// completions.go:3049 isClassLikeMemberCompletion.
bool isClassLikeMemberCompletion(Symbol* symbol, Node* location,
                                 SourceFile* file);
// completions.go:3060 symbolAppearsToBeTypeOnly.
bool symbolAppearsToBeTypeOnly(Symbol* symbol, checker::Checker* typeChecker);
// completions.go:3066 shouldIncludeSymbol.
bool shouldIncludeSymbol(Symbol* symbol, completionDataData* data,
                         Node* closestSymbolDeclaration, SourceFile* file,
                         checker::Checker* typeChecker,
                         const CompilerOptions* compilerOptions);
// completions.go:3158 getCompletionEntryDisplayNameForSymbol.
std::pair<std::string, bool> getCompletionEntryDisplayNameForSymbol(
    SourceFile* file, const lsutil::UserPreferences& preferences,
    Symbol* symbol, symbolOriginInfo* origin, CompletionKind completionKind,
    bool isJsxIdentifierExpected);
// completions.go:3220 originIsIgnore.
bool originIsIgnore(symbolOriginInfo* origin);
// completions.go:3224 originIncludesSymbolName.
bool originIncludesSymbolName(symbolOriginInfo* origin);
// completions.go:3228 originIsComputedPropertyName.
bool originIsComputedPropertyName(symbolOriginInfo* origin);
// completions.go:3232 originIsObjectLiteralMethod.
bool originIsObjectLiteralMethod(symbolOriginInfo* origin);
// completions.go:3236 originIsThisTypeNode.
bool originIsThisTypeNode(symbolOriginInfo* origin);
// completions.go:3240 originIsTypeOnlyAlias.
bool originIsTypeOnlyAlias(symbolOriginInfo* origin);
// completions.go:3244 originIsSymbolMember.
bool originIsSymbolMember(symbolOriginInfo* origin);
// completions.go:3248 originIsNullableMember.
bool originIsNullableMember(symbolOriginInfo* origin);
// completions.go:3252 originIsPromise.
bool originIsPromise(symbolOriginInfo* origin);
// completions.go:3256 getSourceFromOrigin.
std::string getSourceFromOrigin(symbolOriginInfo* origin);
// completions.go:3271 getRelevantTokens.
std::pair<Node*, Node*> getRelevantTokens(int position, SourceFile* file);
// completions.go:3283 isValidTrigger.
bool isValidTrigger(SourceFile* file, const std::string& triggerCharacter,
                    Node* contextToken, int position);
// completions.go:3320 isStringLiteralOrTemplate.
bool isStringLiteralOrTemplate(Node* node);
// completions.go:3328 binaryExpressionMayBeOpenTag.
bool binaryExpressionMayBeOpenTag(BinaryExpression* binaryExpression);
// completions.go:3332 isCheckedFile.
bool isCheckedFile(SourceFile* file, const CompilerOptions* compilerOptions);
// completions.go:3336 isContextTokenValueLocation.
bool isContextTokenValueLocation(Node* contextToken);
// completions.go:3342 isPossiblyTypeArgumentPosition.
bool isPossiblyTypeArgumentPosition(Node* token, SourceFile* sourceFile,
                                    checker::Checker* typeChecker);
// completions.go:3348 isContextTokenTypeLocation.
bool isContextTokenTypeLocation(Node* contextToken);
// completions.go:3382 symbolCanBeReferencedAtTypeLocation.
bool symbolCanBeReferencedAtTypeLocation(
    Symbol* symbol, checker::Checker* typeChecker,
    collections::Set<SymbolId> seenModules);
// completions.go:3393 nonAliasCanBeReferencedAtTypeLocation.
bool nonAliasCanBeReferencedAtTypeLocation(
    Symbol* symbol, checker::Checker* typeChecker,
    collections::Set<SymbolId> seenModules);
// completions.go:3403 getPropertiesForCompletion.
std::vector<Symbol*> getPropertiesForCompletion(checker::Type* t,
                                                checker::Checker* typeChecker);
// completions.go:3413 getLeftMostName.
Identifier* getLeftMostName(Node* e);
// completions.go:3421 getFirstSymbolInChain.
Symbol* getFirstSymbolInChain(Symbol* symbol, Node* enclosingDeclaration,
                              checker::Checker* typeChecker);
// completions.go:3440 isModuleSymbol.
bool isModuleSymbol(Symbol* symbol);
// completions.go:3444 getNullableSymbolOriginInfoKind.
symbolOriginInfoKind getNullableSymbolOriginInfoKind(symbolOriginInfoKind kind,
                                                    bool insertQuestionDot);
// completions.go:3451 isStaticProperty.
bool isStaticProperty(Symbol* symbol);
// completions.go:3458 getContextualTypeForConditionalExpression.
checker::Type* getContextualTypeForConditionalExpression(
    Node* conditionalExpr, int position, SourceFile* file,
    checker::Checker* typeChecker);
// completions.go:3470 getContextualType.
checker::Type* getContextualType(Node* previousToken, int position,
                                 SourceFile* file,
                                 checker::Checker* typeChecker);
// completions.go:3564 isLiteral.
bool isLiteral(checker::Type* t);
// completions.go:3568 getRecommendedCompletion.
Symbol* getRecommendedCompletion(Node* previousToken,
                                 checker::Type* contextualType,
                                 checker::Checker* typeChecker);
// completions.go:3593 isAbstractConstructorSymbol.
bool isAbstractConstructorSymbol(Symbol* symbol);
// completions.go:3601 startsWithQuote.
bool startsWithQuote(const std::string& s);
// completions.go:3606 getClosestSymbolDeclaration.
Node* getClosestSymbolDeclaration(Node* contextToken, Node* location);
// completions.go:3634 isArrowFunctionBody.
bool isArrowFunctionBody(Node* node);
// completions.go:3642 isInTypeParameterDefault.
bool isInTypeParameterDefault(Node* contextToken);
// completions.go:3659 isDeprecated.
bool isDeprecated(Symbol* symbol, checker::Checker* typeChecker);
// completions.go:3710 quotePropertyName.
std::string quotePropertyName(SourceFile* file,
                              const lsutil::UserPreferences& preferences,
                              const std::string& name);
// completions.go:3719 isStringAndEmptyAnonymousObjectIntersection.
bool isStringAndEmptyAnonymousObjectIntersection(
    checker::Checker* typeChecker, checker::Type* t);
// completions.go:3729 areIntersectedTypesAvoidingStringReduction.
bool areIntersectedTypesAvoidingStringReduction(checker::Checker* typeChecker,
                                                checker::Type* t1,
                                                checker::Type* t2);
// completions.go:3738 isNamedImportsOrExports.
bool isNamedImportsOrExports(Node* node);
// completions.go:3742 generateIdentifierForArbitraryString.
std::string generateIdentifierForArbitraryString(const std::string& text);
// completions.go:3774 getCompletionsSymbolKind.
lsproto::CompletionItemKind getCompletionsSymbolKind(
    lsutil::ScriptElementKind kind);
// completions.go:3831 CompareCompletionEntries.
int CompareCompletionEntries(lsproto::CompletionItem* a,
                             lsproto::CompletionItem* b);
// completions.go:3891 getKeywordCompletions.
std::vector<CompletionItem*> getKeywordCompletions(
    KeywordCompletionFilters keywordFilter, bool filterOutTsOnlyKeywords);
// completions.go:3913 getTypescriptKeywordCompletions.
std::vector<lsproto::CompletionItem*> getTypescriptKeywordCompletions(
    KeywordCompletionFilters keywordFilter);
// completions.go:3953 isTypeScriptOnlyKeyword.
bool isTypeScriptOnlyKeyword(Kind kind);
// completions.go:3994 isFunctionLikeBodyKeyword.
bool isFunctionLikeBodyKeyword(Kind kind);
// completions.go:4005 isClassMemberCompletionKeyword.
bool isClassMemberCompletionKeyword(Kind kind);
// completions.go:4014 isInterfaceOrTypeLiteralCompletionKeyword.
bool isInterfaceOrTypeLiteralCompletionKeyword(Kind kind);
// completions.go:4018 isContextualKeywordInAutoImportableExpressionSpace.
bool isContextualKeywordInAutoImportableExpressionSpace(
    const std::string& keyword);
// completions.go:4031 getContextualKeywords.
std::vector<lsproto::CompletionItem*> getContextualKeywords(
    SourceFile* file, Node* contextToken, int position);
// completions.go:4087 isMemberCompletionKind.
bool isMemberCompletionKind(CompletionKind kind);
// completions.go:4093 tryGetFunctionLikeBodyCompletionContainer.
Node* tryGetFunctionLikeBodyCompletionContainer(Node* contextToken);
// completions.go:4109 computeCommitCharactersAndIsNewIdentifier.
std::pair<bool, std::vector<std::string>>
computeCommitCharactersAndIsNewIdentifier(Node* contextToken, SourceFile* file,
                                          int position);
// completions.go:4201 keywordForNode.
Kind keywordForNode(Node* node);
// completions.go:4208 getScopeNode.
Node* getScopeNode(Node* initialToken, int position, SourceFile* file);
// completions.go:4216 isSnippetScope.
bool isSnippetScope(Node* scopeNode);
// completions.go:4228 isProbablyGlobalType.
bool isProbablyGlobalType(checker::Type* t, SourceFile* file,
                          checker::Checker* typeChecker);
// completions.go:4246 tryGetTypeLiteralNode.
Node* tryGetTypeLiteralNode(Node* node);
// completions.go:4263 getConstraintOfTypeArgumentProperty.
checker::Type* getConstraintOfTypeArgumentProperty(
    Node* node, checker::Checker* typeChecker);
// completions.go:4308 tryGetObjectLikeCompletionContainer.
Node* tryGetObjectLikeCompletionContainer(Node* contextToken, int position,
                                          SourceFile* file);
// completions.go:4361 tryGetObjectLiteralContextualType.
checker::Type* tryGetObjectLiteralContextualType(
    Node* node, checker::Checker* typeChecker);
// completions.go:4382 getPropertiesForObjectExpression.
std::vector<Symbol*> getPropertiesForObjectExpression(
    checker::Type* contextualType, checker::Type* completionsType, Node* obj,
    checker::Checker* typeChecker);
// completions.go:4428 getApparentProperties.
std::vector<Symbol*> getApparentProperties(checker::Type* t, Node* node,
                                           checker::Checker* typeChecker);
// completions.go:4440 containsNonPublicProperties.
bool containsNonPublicProperties(const std::vector<Symbol*>& props);
// completions.go:4447 filterObjectMembersList.
std::pair<std::vector<Symbol*>, collections::Set<std::string>>
filterObjectMembersList(std::vector<Symbol*> contextualMemberSymbols,
                        std::vector<Node*> existingMembers,
                        SourceFile* file, int position,
                        checker::Checker* typeChecker);
// completions.go:4508 isCurrentlyEditingNode.
bool isCurrentlyEditingNode(Node* node, SourceFile* file, int position);
// completions.go:4513 setMemberDeclaredBySpreadAssignment.
void setMemberDeclaredBySpreadAssignment(
    Node* declaration, collections::Set<std::string>* members,
    checker::Checker* typeChecker);
// completions.go:4531 tryGetConstructorLikeCompletionContainer.
Node* tryGetConstructorLikeCompletionContainer(Node* contextToken);
// completions.go:4551 isConstructorParameterCompletion.
bool isConstructorParameterCompletion(Node* node);
// completions.go:4558 tryGetObjectTypeDeclarationCompletionContainer.
Node* tryGetObjectTypeDeclarationCompletionContainer(
    SourceFile* file, Node* contextToken, Node* location, int position);
// completions.go:4641 isFromObjectTypeDeclaration.
bool isFromObjectTypeDeclaration(Node* node);
// completions.go:4646 filterClassMembersList.
std::vector<Symbol*> filterClassMembersList(
    std::vector<Symbol*> baseSymbols,
    const std::vector<Node*>& existingMembers,
    ModifierFlags classElementModifierFlags, SourceFile* file, int position);
// completions.go:4688 tryGetContainingJsxElement.
Node* tryGetContainingJsxElement(Node* contextToken, SourceFile* file);
// completions.go:4751 filterJsxAttributes.
std::pair<std::vector<Symbol*>, collections::Set<std::string>>
filterJsxAttributes(std::vector<Symbol*> symbols,
                  const std::vector<Node*>& attributes, SourceFile* file,
                  int position, checker::Checker* typeChecker);
// completions.go:4781 isTypeKeywordTokenOrIdentifier.
bool isTypeKeywordTokenOrIdentifier(Node* node);
// completions.go:4988 isCompletionListBlocker.
bool isCompletionListBlocker(Node* contextToken, Node* previousToken,
                             Node* location, SourceFile* file, int position,
                             checker::Checker* typeChecker);
// completions.go:4997 isInStringOrRegularExpressionOrTemplateLiteral.
bool isInStringOrRegularExpressionOrTemplateLiteral(Node* contextToken,
                                                    int position);
// completions.go:5008 isSolelyIdentifierDefinitionLocation.
bool isSolelyIdentifierDefinitionLocation(Node* contextToken,
                                          Node* previousToken,
                                          SourceFile* file, int position,
                                          checker::Checker* typeChecker);
// completions.go:5152 isVariableDeclarationListButNotTypeArgument.
bool isVariableDeclarationListButNotTypeArgument(
    Node* node, SourceFile* file, checker::Checker* typeChecker);
// completions.go:5157 isFunctionLikeButNotConstructor.
bool isFunctionLikeButNotConstructor(Kind kind);
// completions.go:5161 isPreviousPropertyDeclarationTerminated.
bool isPreviousPropertyDeclarationTerminated(Node* contextToken,
                                             SourceFile* file, int position);
// completions.go:5168 isDotOfNumericLiteral.
bool isDotOfNumericLiteral(Node* contextToken, SourceFile* file);
// completions.go:5178 isInJsxText.
bool isInJsxText(Node* contextToken, Node* location);
// completions.go:5423 clientSupportsItemLabelDetails.
bool clientSupportsItemLabelDetails(const ContextPtr& ctx);
// completions.go:5439 clientSupportsDefaultCommitCharacters.
bool clientSupportsDefaultCommitCharacters(const ContextPtr& ctx);
// completions.go:5443 clientSupportsDefaultEditRange.
bool clientSupportsDefaultEditRange(const ContextPtr& ctx);
// completions.go:5453 getArgumentInfoForCompletions.
argumentInfoForCompletions* getArgumentInfoForCompletions(
    Node* node, int position, SourceFile* file,
    checker::Checker* typeChecker);
// completions.go:5698 createSimpleDetails.
lsproto::CompletionItem* createSimpleDetails(lsproto::CompletionItem* item,
                                             const std::string& name,
                                             lsproto::MarkupKind docFormat);
// completions.go:5705 createCompletionDetails.
lsproto::CompletionItem* createCompletionDetails(
    lsproto::CompletionItem* item, const std::string& detail,
    const std::string& documentation, lsproto::MarkupKind docFormat);
// completions.go:5754 couldBeTypeOnlyImportSpecifier.
bool couldBeTypeOnlyImportSpecifier(Node* importSpecifier, Node* contextToken);
// completions.go:5758 canCompleteFromNamedBindings.
bool canCompleteFromNamedBindings(Node* namedBindings);
// completions.go:5808 getPotentiallyInvalidImportSpecifier.
Node* getPotentiallyInvalidImportSpecifier(Node* namedBindings);
// completions.go:5820 isModuleSpecifierMissingOrEmpty.
bool isModuleSpecifierMissingOrEmpty(Node* specifier);
// completions.go:5832 hasDocComment.
bool hasDocComment(SourceFile* file, int position);
// completions.go:5838 getJSDocTagAtPosition.
Node* getJSDocTagAtPosition(Node* node, int position);
// completions.go:5850 tryGetTypeExpressionFromTag.
Node* tryGetTypeExpressionFromTag(Node* tag);
// completions.go:5867 isTagWithTypeExpression.
bool isTagWithTypeExpression(Node* tag);
// completions.go:6093 jsDocTagNames.
extern const std::vector<std::string> jsDocTagNames;
// completions.go:6178 getJSDocTagNameCompletions.
std::vector<CompletionItem*> getJSDocTagNameCompletions();
// completions.go:6182 getJSDocTagCompletions.
std::vector<CompletionItem*> getJSDocTagCompletions();
// completions.go:6186 getJSDocParameterCompletions.
std::vector<CompletionItem*> getJSDocParameterCompletions(
    const ContextPtr& ctx, SourceFile* file, int position,
    checker::Checker* typeChecker, const CompilerOptions* options,
    const lsutil::UserPreferences& preferences, bool tagNameOnly);
// completions.go:6348 getJSDocParamAnnotation.
std::string getJSDocParamAnnotation(
    const std::string& paramName, Node* initializer,
    Node* dotDotDotToken, bool isJS, bool isObject, bool isSnippet,
    checker::Checker* typeChecker, const CompilerOptions* options,
    const lsutil::UserPreferences& preferences, int* tabstopCounter);
// completions.go:6412 getJSDocParamNameWithInitializer.
std::string getJSDocParamNameWithInitializer(const std::string& paramName,
                                             Node* initializer);
// completions.go:6420 generateJSDocParamTagsForDestructuring.
std::vector<std::string> generateJSDocParamTagsForDestructuring(
    const std::string& path, BindingPattern* pattern,
    Node* initializer, Node* dotDotDotToken, bool isJS,
    bool isSnippet, checker::Checker* typeChecker,
    const CompilerOptions* options,
    const lsutil::UserPreferences& preferences);
// completions.go:6453 jsDocParamPatternWorker.
std::vector<std::string> jsDocParamPatternWorker(
    const std::string& path, BindingPattern* pattern,
    Node* initializer, Node* dotDotDotToken, bool isJS,
    bool isSnippet, checker::Checker* typeChecker,
    const CompilerOptions* options,
    const lsutil::UserPreferences& preferences, int* counter);
// completions.go:6510 jsDocParamElementWorker.
std::vector<std::string> jsDocParamElementWorker(
    const std::string& path, BindingElement* element,
    Node* initializer, Node* dotDotDotToken, bool isJS,
    bool isSnippet, checker::Checker* typeChecker,
    const CompilerOptions* options,
    const lsutil::UserPreferences& preferences, int* counter);
// completions.go:6560 getJSDocParameterNameCompletions.
std::vector<CompletionItem*> getJSDocParameterNameCompletions(Node* tag);
// completions.go:6706 typeNodeToExpression.
Node* typeNodeToExpression(Node* typeNode, ScriptTarget target,
                           lsutil::QuotePreference quotePreference,
                           NodeFactory* factory);
// completions.go:6768 entityNameToExpression.
Node* entityNameToExpression(Node* entityName, ScriptTarget target,
                             lsutil::QuotePreference quotePreference,
                             NodeFactory* factory);

// completions.go:6785 snippetPrinter.
struct snippetPrinter {
	printer::ChangeTrackerWriter* baseWriter = nullptr;
	printer::EmitContext* emitContext = nullptr;
	printer::Printer* printer_ = nullptr;
	struct snippetEmitTextWriter* writer = nullptr;
	NodeFactory* factory = nullptr;

	// completions.go:6794 printNode.
	std::string printNode(Node* node);
	// completions.go:6802 printUnescapedNode.
	std::string printUnescapedNode(Node* node);
	// completions.go:6809 printAndFormatNode.
	std::string printAndFormatNode(const ContextPtr& ctx, Node* node,
	                               SourceFile* sourceFile);
	// completions.go:6813 printAndFormatNodeWithSettings.
	std::string printAndFormatNodeWithSettings(
	    const ContextPtr& ctx, Node* node, SourceFile* sourceFile,
	    lsutil::FormatCodeSettings formatOptions);
};

// completions.go:6855 snippetEmitTextWriter — embeds
// *printer.ChangeTrackerWriter in Go; since Go embeds the POINTER, C++ keeps
// it as a `base` member and forwards every EmitTextWriter virtual so that
// all state stays shared with `snippetPrinter::baseWriter`.
struct snippetEmitTextWriter : public printer::EmitTextWriter {
	printer::ChangeTrackerWriter* base = nullptr;
	std::vector<TextChange> escapes;

	// completions.go:6860 Write.
	void Write(const std::string& s) override;
	// completions.go:6864 WriteComment.
	void WriteComment(const std::string& text) override;
	// completions.go:6868 WriteStringLiteral.
	void WriteStringLiteral(const std::string& text) override;
	// completions.go:6872 WriteParameter.
	void WriteParameter(const std::string& text) override;
	// completions.go:6876 WriteProperty.
	void WriteProperty(const std::string& text) override;
	// completions.go:6880 WriteSymbol.
	void WriteSymbol(const std::string& text, Symbol* symbol) override;
	// completions.go:6886 escapingWrite.
	void escapingWrite(const std::string& s, const std::function<void()>& write);

	// EmitTextWriter virtuals not overridden by snippet-escaping — forward
	// to the embedded base (promoted methods in Go).
	void WriteTrailingSemicolon(const std::string& text) override {
		base->WriteTrailingSemicolon(text);
	}
	void WriteKeyword(const std::string& text) override {
		base->WriteKeyword(text);
	}
	void WriteOperator(const std::string& text) override {
		base->WriteOperator(text);
	}
	void WritePunctuation(const std::string& text) override {
		base->WritePunctuation(text);
	}
	void WriteSpace(const std::string& text) override {
		base->WriteSpace(text);
	}
	void WriteLine() override { base->WriteLine(); }
	void WriteLineForce(bool force) override { base->WriteLineForce(force); }
	void IncreaseIndent() override { base->IncreaseIndent(); }
	void DecreaseIndent() override { base->DecreaseIndent(); }
	void Clear() override { base->Clear(); }
	std::string String() override { return base->String(); }
	void RawWrite(const std::string& s) override { base->RawWrite(s); }
	void WriteLiteral(const std::string& s) override {
		base->WriteLiteral(s);
	}
	int GetTextPos() override { return base->GetTextPos(); }
	int GetLine() override { return base->GetLine(); }
	TextPos GetColumn() override { return base->GetColumn(); }
	int GetIndent() override { return base->GetIndent(); }
	bool IsAtStartOfLine() override { return base->IsAtStartOfLine(); }
	bool HasTrailingComment() override { return base->HasTrailingComment(); }
	bool HasTrailingWhitespace() override {
		return base->HasTrailingWhitespace();
	}
};

// completions.go:6839 createSnippetPrinter.
snippetPrinter* createSnippetPrinter(printer::PrinterOptions options,
                                     printer::EmitContext* emitContext);

// string_completions.go:1231 moduleCompletionKind.
using moduleCompletionKind = int32_t;
inline constexpr moduleCompletionKind moduleCompletionKindDirectory = 0;
inline constexpr moduleCompletionKind moduleCompletionKindFile = 1;
inline constexpr moduleCompletionKind moduleCompletionKindExternalModuleName =
    2;

// string_completions.go:1238 moduleCompletionNameAndKind.
struct moduleCompletionNameAndKind {
	std::string name;
	moduleCompletionKind kind = moduleCompletionKindDirectory;
	std::string extension;
};

// string_completions.go:1243 moduleCompletionNameAndKindSet.
struct moduleCompletionNameAndKindSet {
	std::unordered_map<std::string, moduleCompletionNameAndKind> names;
	void add(const moduleCompletionNameAndKind& item);
};

// string_completions.go:1264 referenceKind.
// (opaque-declared above for use in LanguageService methods)

// string_completions.go:1257 extensionOptions.
enum class referenceKind : int32_t { FileName, ModuleSpecifier };
struct extensionOptions {
	std::vector<std::string> extensionsToSearch;
	referenceKind referenceKind = referenceKind::FileName;
	SourceFile* importingSourceFile = nullptr;
	modulespecifiers::ImportModuleSpecifierEndingPreference endingPreference =
	    modulespecifiers::ImportModuleSpecifierEndingPreferenceAuto;
	ResolutionMode resolutionMode = ResolutionModeNone;
};

// string_completions.go:440 fromContextualType.
completionsFromTypes* fromContextualType(
    checker::ContextFlags contextFlags, Node* node,
    checker::Checker* typeChecker);
// string_completions.go:446 toCompletionsFromTypes.
completionsFromTypes* toCompletionsFromTypes(
    const std::vector<checker::StringLiteralType*>& types);
// string_completions.go:456 toStringLiteralCompletionsFromTypes.
stringLiteralCompletions* toStringLiteralCompletionsFromTypes(
    const std::vector<checker::StringLiteralType*>& types);
// string_completions.go:466 fromUnionableLiteralType.
stringLiteralCompletions* fromUnionableLiteralType(
    Node* grandparent, Node* parent, int position,
    checker::Checker* typeChecker);
// string_completions.go:555 stringLiteralCompletionsForObjectLiteral.
completionsFromProperties* stringLiteralCompletionsForObjectLiteral(
    checker::Checker* typeChecker, Node* objectLiteralExpression);
// string_completions.go:578 stringLiteralCompletionsFromProperties.
completionsFromProperties* stringLiteralCompletionsFromProperties(
    checker::Type* t, checker::Checker* typeChecker);
// string_completions.go:612 toPathCompletions.
std::vector<pathCompletion*> toPathCompletions(
    const std::vector<moduleCompletionNameAndKind>& names);
// string_completions.go:633 moduletToScriptElementKind.
lsutil::ScriptElementKind moduletToScriptElementKind(
    moduleCompletionKind kind);
// string_completions.go:645 isAnyDirectorySeparator.
bool isAnyDirectorySeparator(char32_t r);
// string_completions.go:650 getDirectoryFragmentRange.
TextRange* getDirectoryFragmentRange(const std::string& text, int textStart);
// string_completions.go:875 getFragmentDirectory.
std::string getFragmentDirectory(const std::string& fragment);
// string_completions.go:885 getPatternFromFirstMatchingCondition.
std::string getPatternFromFirstMatchingCondition(
    packagejson::ExportsOrImports* target,
    const std::vector<std::string>& conditions);
// string_completions.go:904 getAmbientModuleCompletions.
std::vector<std::string> getAmbientModuleCompletions(
    const std::string& fragment, const std::string& fragmentDirectory,
    checker::Checker* typeChecker);
// string_completions.go:923 getAmbientModuleName.
std::string getAmbientModuleName(Symbol* symbol);
// string_completions.go:1000 tryRemoveDirectoryPrefix.
std::string* tryRemoveDirectoryPrefix(const std::string& path,
                                      const std::string& prefix,
                                      bool useCaseSensitiveFileNames);
// string_completions.go:1050 getSupportedExtensionsForModuleResolution.
std::vector<std::string> getSupportedExtensionsForModuleResolution(
    const CompilerOptions* options,
    const std::vector<std::string>& extraExtensions,
    checker::Checker* checker);
// string_completions.go:1074 moduleResolutionUsesNodeModules.
bool moduleResolutionUsesNodeModules(
    ModuleResolutionKind moduleResolution);
// string_completions.go:1080 isPathRelativeToScript.
bool isPathRelativeToScript(const std::string& path);
// string_completions.go:1155 getBaseDirectoriesFromRootDirs.
std::vector<std::string> getBaseDirectoriesFromRootDirs(
    const std::vector<std::string>& rootDirs, const std::string& basePath,
    const std::string& scriptDirectory, bool ignoreCase);
// string_completions.go:1195 deduplicateStrings.
std::vector<std::string> deduplicateStrings(
    const std::vector<std::string>& list);
// string_completions.go:1210 deduplicateModuleCompletions.
std::vector<moduleCompletionNameAndKind> deduplicateModuleCompletions(
    const std::vector<moduleCompletionNameAndKind>& completions);
// string_completions.go:1598 getFileExtension.
std::string getFileExtension(const std::string& fileName);
// string_completions.go:1822 containsSlash.
bool containsSlash(const std::string& fragment);
// string_completions.go:1826 withoutStartAndEnd.
std::optional<std::string> withoutStartAndEnd(const std::string& s,
                                              const std::string& start,
                                              const std::string& end);
// string_completions.go:1834 removeLeadingDirectorySeparator.
std::string removeLeadingDirectorySeparator(const std::string& path);
// string_completions.go:1838 getPossibleOriginalInputPathWithoutChangingExt.
std::string getPossibleOriginalInputPathWithoutChangingExt(
    const std::string& filePath, bool ignoreCase,
    const std::string& outputDir,
    const std::function<std::string()>& getCommonSourceDirectory);
// string_completions.go:1855 getFilenameWithExtensionOption.
std::pair<std::string, std::string> getFilenameWithExtensionOption(
    const std::string& name, compiler::SimpleProgram* program,
    extensionOptions* extensionOptions, bool isExportsOrImportsWildcard);
// string_completions.go:1911 walkUpParentheses.
Node* walkUpParentheses(Node* node);
// string_completions.go:1922 getStringLiteralTypes.
std::vector<checker::StringLiteralType*> getStringLiteralTypes(
    checker::Type* t, collections::Set<std::string>* uniques,
    checker::Checker* typeChecker);
// string_completions.go:1943 getAlreadyUsedTypesInStringLiteralUnion.
std::vector<std::string> getAlreadyUsedTypesInStringLiteralUnion(
    Node* union_, Node* current);
// string_completions.go:1958 hasIndexSignature.
bool hasIndexSignature(checker::Type* t, checker::Checker* typeChecker);
// string_completions.go:1966 isRequireCallArgument.
bool isRequireCallArgument(Node* node);
// string_completions.go:1971 kindModifiersFromExtension.
lsutil::ScriptElementKindModifier kindModifiersFromExtension(
    const std::string& extension);
// string_completions.go:2004 getStringLiteralCompletionsFromSignature.
completionsFromTypes* getStringLiteralCompletionsFromSignature(
    Node* call, Node* arg, argumentInfoForCompletions* argumentInfo,
    checker::Checker* typeChecker);
// string_completions.go:2105 isInReferenceComment.
bool isInReferenceComment(SourceFile* file, int position);
// string_completions.go:2114 hasTripleSlashPrefix.
bool hasTripleSlashPrefix(std::string_view commentText);
// string_completions.go:2134 parseTripleSlashDirectiveFragment.
struct tripleSlashDirectiveResult {
	std::string prefix;
	std::string kind;
	std::string toComplete;
};
std::optional<tripleSlashDirectiveResult> parseTripleSlashDirectiveFragment(
    std::string_view text);

// dep-stubs for sibling-owned items (bodies in lsdeps.cpp):

// signaturehelp.go:912 argumentListInfo + invocation types.
struct callInvocation {
	Node* node = nullptr;
};
struct typeArgsInvocation {
	Identifier* called = nullptr;
};
struct contextualInvocation {
	checker::Signature* signature = nullptr;
	Node* node = nullptr;
	Symbol* symbol = nullptr;
};
struct invocation {
	callInvocation* callInvocation = nullptr;
	typeArgsInvocation* typeArgsInvocation = nullptr;
	contextualInvocation* contextualInvocation = nullptr;
};
struct argumentListInfo {
	bool isTypeParameterList = false;
	invocation* invocation_ = nullptr;
	TextRange argumentsSpan;
	int argumentIndex = 0;
	int argumentCount = 0;
};
// signaturehelp.go:923 getImmediatelyContainingArgumentInfo.
argumentListInfo* getImmediatelyContainingArgumentInfo(
    Node* node, int position, SourceFile* sourceFile, checker::Checker* c);

// codeactions_missingmemberfixer.go:20 preserveOptionalFlags.
using preserveOptionalFlags = int32_t;
inline constexpr preserveOptionalFlags preserveOptionalFlagsMethod = 1;
inline constexpr preserveOptionalFlags preserveOptionalFlagsProperty = 2;
inline constexpr preserveOptionalFlags preserveOptionalFlagsAll =
    preserveOptionalFlagsMethod | preserveOptionalFlagsProperty;

// codeactions_missingmemberfixer.go:25 missingMemberFixer.
struct missingMemberFixer {
	virtual ~missingMemberFixer() = default;
	// codeactions_missingmemberfixer.go:52 createMemberFromSymbol.
	virtual std::vector<Node*> createMemberFromSymbol(
	    Symbol* symbol, Node* enclosingDeclaration, SourceFile* sourceFile,
	    Node* body, preserveOptionalFlags preserveOptional,
	    bool abstract) = 0;
};
// codeactions_missingmemberfixer.go:35 newMissingMemberFixer.
missingMemberFixer* newMissingMemberFixer(
    change::Tracker* changeTracker, compiler::SimpleProgram* program,
    checker::Checker* typeChecker, const lsutil::UserPreferences& preferences,
    autoimport::ImportAdder* importAdder, locale::Locale locale);

} // namespace tsc::ls
