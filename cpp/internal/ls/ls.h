// ls.h — shared declarations for the `ls` package port (slice: ls-coreB —
// references / codeactions). Mirrors tsc/internal/ls/*.go public surface:
// LanguageService, Host, and the types shared across .cpp files in this slice.
// File-local types stay in their .cpp.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/ast/flags.h"
#include "internal/ast/kind.h"
#include "internal/ast/symbol.h"
#include "internal/collections/collections.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/locale/locale.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/modulespecifiers/types.h"
#include "internal/nodebuilder/types.h"
#include "internal/printer/printer.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/spanmap/spanmap.h"
#include "internal/tspath/tspath.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/ls/change/change.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"

namespace tsc {
enum class DiagnosticCategory : int32_t;
struct Node;
struct SourceFile;
struct Symbol;
struct Diagnostic;
struct FileReference;
using Statement = Node;
using Expression = Node;
struct ParameterList;
struct ModifierList;
struct PropertyName;
struct FunctionBody;
struct TypeNode;
struct IdentifierNode;
struct NodeFactory;
struct NodeList;
struct CommentRange;
struct ObjectLiteralExpression;
struct HeritageClauseElement;
struct ImportAttributesNode;
struct PropertyAssignment;
struct SpreadElement;
struct ExportSpecifier;
namespace checker {
class Checker;
struct Signature;
struct Type;
struct NodeBuilder;
struct Program;
struct VerbosityContext;
} // namespace checker
namespace compiler {
class SimpleProgram;
}
namespace printer {
struct Printer;
struct EmitTextWriter;
struct EmitContext;
} // namespace printer
namespace locale {
class Locale;
}
namespace scanner {
struct Scanner;
}
} // namespace tsc

namespace tsc::ls {

class LanguageService;

// === constants.go ===
inline constexpr int moduleSpecifierResolutionLimit = 100;
inline constexpr int moduleSpecifierResolutionCacheAttemptLimit = 1000;

// SemanticMeaning — ast/utilities.go:2240. Not yet ported into cpp/ast;
// needed by findallreferences.cpp and the hover dep-stub signatures.
using SemanticMeaning = int32_t;
inline constexpr SemanticMeaning SemanticMeaningNone = 0;
inline constexpr SemanticMeaning SemanticMeaningValue = 1 << 0;
inline constexpr SemanticMeaning SemanticMeaningType = 1 << 1;
inline constexpr SemanticMeaning SemanticMeaningNamespace = 1 << 2;
inline constexpr SemanticMeaning SemanticMeaningAll =
    SemanticMeaningValue | SemanticMeaningType | SemanticMeaningNamespace;

// === completions.go (sibling slice) ===
// ErrNeedsAutoImports sentinel — completions.go:35. The canonical definition
// is owned by ls-coreA; this inline var is the dep-stub for use inside this
// slice only.
inline const gostd::Error ErrNeedsAutoImports =
	gostd::newError("completion list needs auto imports");

// === host.go ===
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
	virtual std::vector<std::string> ReadDirectory(
	    const std::string& currentDir, const std::string& path,
	    const std::vector<std::string>& extensions,
	    const std::vector<std::string>& excludes,
	    const std::vector<std::string>& includes, int depth) = 0;
	virtual std::vector<std::string> GetDirectories(const std::string& path) = 0;
	virtual bool DirectoryExists(const std::string& path) = 0;
	virtual bool FileExists(const std::string& path) = 0;
};

// === findallreferences.go — shared result types ===

using referenceUse = int;
inline constexpr referenceUse referenceUseNone = 0;
inline constexpr referenceUse referenceUseOther = 1;
inline constexpr referenceUse referenceUseReferences = 2;
inline constexpr referenceUse referenceUseRename = 3;

struct refOptions {
	bool findInStrings = false;
	bool findInComments = false;
	referenceUse use = 0;
	bool implementations = false;
	bool useAliasesForRename = false;
};

struct refInfo {
	SourceFile* file = nullptr;
	std::string fileName;
	FileReference* reference = nullptr;
	bool unverified = false;
};

using DefinitionKind = int;
inline constexpr DefinitionKind definitionKindSymbol = 0;
inline constexpr DefinitionKind definitionKindLabel = 1;
inline constexpr DefinitionKind definitionKindKeyword = 2;
inline constexpr DefinitionKind definitionKindThis = 3;
inline constexpr DefinitionKind definitionKindString = 4;
inline constexpr DefinitionKind definitionKindTripleSlashReference = 5;

struct tripleSlashDefinition {
	FileReference* reference = nullptr;
	SourceFile* file = nullptr;
};

struct Definition {
	DefinitionKind Kind = definitionKindSymbol;
	Symbol* symbol = nullptr;
	Node* node = nullptr;
	tripleSlashDefinition* tripleSlashFileRef = nullptr;
};

using entryKind = int;
inline constexpr entryKind entryKindNone = 0;
inline constexpr entryKind entryKindRange = 1;
inline constexpr entryKind entryKindNode = 2;
inline constexpr entryKind entryKindStringLiteral = 3;
inline constexpr entryKind entryKindSearchedLocalFoundProperty = 4;
inline constexpr entryKind entryKindSearchedPropertyFoundLocal = 5;

struct ReferenceEntry {
	entryKind kind = entryKindNone;
	Node* node = nullptr;
	Node* context = nullptr;
	SourceFile* sourceFile = nullptr;
	TextRange* textRange = nullptr;
	lsproto::Location* lspRange = nullptr;
	bool unmappable = false;

	Node* Node_() const { return node; } // Node() — method renamed (field clash)
	bool IsNodeEntry() const { return node != nullptr; }
};

struct SymbolAndEntries {
	Definition* definition = nullptr;
	std::vector<ReferenceEntry*> references;

	std::vector<ReferenceEntry*> References() const { return references; }
	Node* DefinitionNode() const;
	Symbol* DefinitionSymbol() const;
	bool canUseDefinitionSymbol() const;
};

SymbolAndEntries* NewSymbolAndEntries(
    DefinitionKind kind, Node* node, Symbol* symbol,
    std::vector<ReferenceEntry*> references);

// position / nonLocalDefinition — findallreferences.go:480.
struct position {
	lsproto::DocumentUri uri;
	lsproto::Position pos;

	lsproto::DocumentUri TextDocumentURI() const { return uri; }
	lsproto::Position TextDocumentPosition() const { return pos; }
};

struct nonLocalDefinition : position {
	// Go: func fields returning lsproto.HasTextDocumentPosition (nil-able).
	std::function<position*()> GetSourcePosition;
	std::function<position*()> GetGeneratedPosition;
};

// symbolEntryTransformOptions / SymbolAndEntriesData — findallreferences.go:631.
struct symbolEntryTransformOptions {
	bool requireLocationsResult = false;
	bool dropOriginNodes = false;
};

struct SymbolAndEntriesData {
	Node* OriginalNode = nullptr;
	std::vector<SymbolAndEntries*> SymbolsAndEntries;
	int Position = 0;
};

// referencedSymbolDefinitionInfo — findallreferences.go:866.
struct referencedSymbolDefinitionInfo {
	Node* node = nullptr;
	lsproto::Location location;
	lsproto::VSClassifiedTextElement* displayText = nullptr;
};

// SignatureUsage — findallreferences.go:1210.
struct SignatureUsage {
	Node* Name = nullptr;
	Node* Call = nullptr;
};

// === importTracker.go — shared types ===

using ImpExpKind = int32_t;
inline constexpr ImpExpKind ImpExpKindUnknown = 0;
inline constexpr ImpExpKind ImpExpKindImport = 1;
inline constexpr ImpExpKind ImpExpKindExport = 2;

struct ExportInfo;

struct ImportExportSymbol {
	ImpExpKind kind = ImpExpKindUnknown;
	Symbol* symbol = nullptr;
	ExportInfo* exportInfo = nullptr;
};

using ExportKind = int;
inline constexpr ExportKind ExportKindNamed = 0;
inline constexpr ExportKind ExportKindDefault = 1;
inline constexpr ExportKind ExportKindExportEquals = 2;
inline constexpr ExportKind ExportKindUMD = 3;
inline constexpr ExportKind ExportKindModule = 4;

struct ExportInfo {
	Symbol* exportingModuleSymbol = nullptr;
	ExportKind exportKind = ExportKindNamed;
};

struct LocationAndSymbol {
	Node* importLocation = nullptr;
	Symbol* importSymbol = nullptr;
};

struct ImportsResult {
	std::vector<LocationAndSymbol> importSearches;
	std::vector<Node*> singleReferences;
	std::vector<SourceFile*> indirectUsers;
};

// ImportTracker — importTracker.go:55.
using ImportTracker =
    std::function<ImportsResult*(Symbol* exportSymbol, ExportInfo* exportInfo,
                                 bool isForRename)>;

using ModuleReferenceKind = int32_t;
inline constexpr ModuleReferenceKind ModuleReferenceKindImport = 0;
inline constexpr ModuleReferenceKind ModuleReferenceKindReference = 1;
inline constexpr ModuleReferenceKind ModuleReferenceKindImplicit = 2;

// ModuleReference — importTracker.go:66.
struct ModuleReference {
	ModuleReferenceKind kind = ModuleReferenceKindImport;
	Node* literal = nullptr; // for import and implicit kinds (StringLiteralLike)
	SourceFile* referencingFile = nullptr;
	FileReference* ref = nullptr; // for reference kind
};

ImportTracker createImportTracker(const gostd::Context& ctx,
                                  compiler::SimpleProgram* program,
                                  const std::vector<SourceFile*>& sourceFiles,
                                  collections::Set<std::string>* sourceFilesSet,
                                  checker::Checker* checker);

std::vector<ModuleReference> findModuleReferences(
    compiler::SimpleProgram* program,
    const std::vector<SourceFile*>& sourceFiles, Symbol* searchModuleSymbol,
    checker::Checker* checker);

ImportExportSymbol* getImportOrExportSymbol(Node* node, Symbol* symbol,
                                            checker::Checker* ch,
                                            bool comingFromExport);
ExportInfo* getExportInfo(Symbol* exportSymbol, ExportKind exportKind,
                          checker::Checker* ch);

// utilities.go — ls-coreA (dep-stub)
bool isSourceFileWithGlobalExports(Node* node);
Symbol* getPropertySymbolOfObjectBindingPatternWithoutPropertyName(
    Symbol* symbol, checker::Checker* ch);

// === codeactions_missingmemberfixer.go — types ===
using preserveOptionalFlags = int;
inline constexpr preserveOptionalFlags preserveOptionalFlagsMethod = 1;
inline constexpr preserveOptionalFlags preserveOptionalFlagsProperty = 2;
inline constexpr preserveOptionalFlags preserveOptionalFlagsAll =
    preserveOptionalFlagsMethod | preserveOptionalFlagsProperty;

// missingMemberFixer — codeactions_missingmemberfixer.go:26.
struct missingMemberFixer {
	change::Tracker* changeTracker = nullptr;
	checker::Checker* typeChecker = nullptr;
	compiler::SimpleProgram* program = nullptr;
	lsutil::UserPreferences preferences;
	autoimport::ImportAdder* importAdder = nullptr;
	locale::Locale loc;

	std::pair<checker::NodeBuilder*,
	          std::unordered_map<Node*, Symbol*>>
	createNodeBuilder();
	std::vector<Node*> createMemberFromSymbol(
	    Symbol* symbol, Node* enclosingDeclaration, SourceFile* sourceFile,
	    Node* body, preserveOptionalFlags preserveOptional, bool abstract);
	std::vector<checker::Signature*> getCallSignatures(checker::Type* t);
	Node* createTypeNode(
	    checker::Type* t, Node* enclosingDeclaration,
	    nodebuilder::Flags flags, checker::NodeBuilder* nodeBuilder,
	    std::unordered_map<Node*, Symbol*>* idToSymbol);
	ModifierList* createModifiers(Symbol* symbol, Node* declaration);
	bool shouldAddOverrideKeyword(Node* declaration);
	Node* createSignatureDeclarationFromSignature(
	    checker::Signature* signature, Kind kind, SourceFile* sourceFile,
	    Node* enclosingDeclaration, Node* body, ModifierList* modifiers,
	    Node* name, bool optional);
	Node* createSignatureDeclarationFromSignatures(
	    const std::vector<checker::Signature*>& signatures, Node* name,
	    bool optional, ModifierList* modifiers,
	    lsutil::QuotePreference quotePreference, Node* body,
	    Node* enclosingDeclaration);
	Node* getReturnTypeFromSignatures(
	    const std::vector<checker::Signature*>& signatures,
	    Node* enclosingDeclaration, checker::NodeBuilder* nodeBuilder,
	    std::unordered_map<Node*, Symbol*>* idToSymbol);
	Node* importTypeNode(Node* typeNode,
	                     std::unordered_map<Node*, Symbol*>* idToSymbol);
	Symbol* getExportedSymbol(Symbol* symbol);
	Node* createIndexSignatureDeclarationFromType(
	    Node* classDeclaration, checker::Type* implementedType,
	    checker::Type* keyType);
	Node* createBody(Node* body, lsutil::QuotePreference quotePreference,
	                 bool signatureOnly);
	Node* createStubbedMethodBody(lsutil::QuotePreference quotePreference);
};

missingMemberFixer* newMissingMemberFixer(
    change::Tracker* changeTracker, compiler::SimpleProgram* program,
    checker::Checker* typeChecker, const lsutil::UserPreferences& preferences,
    autoimport::ImportAdder* importAdder, const locale::Locale& loc);

// === codeactions_fixmissingtypeannotation.go — types ===

// typePrintMode — codeactions_fixmissingtypeannotation.go:80.
using typePrintMode = int;
inline constexpr typePrintMode typePrintModeFull = 0;
inline constexpr typePrintMode typePrintModeRelative = 1;
inline constexpr typePrintMode typePrintModeWidened = 2;

// isolatedDeclarationsFixer — codeactions_fixmissingtypeannotation.go:216.
struct isolatedDeclarationsFixer {
	SourceFile* sourceFile = nullptr;
	compiler::SimpleProgram* program = nullptr;
	checker::Checker* checker = nullptr;
	change::Tracker* changeTracker = nullptr;
	autoimport::ImportAdder* importAdder = nullptr;
	locale::Locale loc;
	std::unordered_map<Node*, bool> fixedNodes;
	typePrintMode typePrintMode = typePrintModeFull;
	std::vector<Symbol*> symbolsToImport;
	bool mutatedTarget = false;

	std::string addTypeAnnotation(TextRange span);
	std::string createNamespaceForExpandoProperties(Node* expandoFunc);
	std::string addInlineAssertion(TextRange span);
	std::string extractAsVariable(TextRange span);
	std::string fixIsolatedDeclarationError(Node* node);
	std::string addTypeToSignatureDeclaration(Node* funcNode);
	std::string transformExportAssignment(Node* defaultExport);
	std::string transformExtendsClauseWithExpression(Node* classDecl);
	std::string transformDestructuringPatterns(Node* bindingPattern);
	void extractBindingElements(Node* bindingPattern, Node* baseExpr,
	                            std::vector<Node*>* newNodes,
	                            Node* enclosingVarStmt);
	void emitBindingElementVariable(NodeFactory* factory, Node* name,
	                                BindingElement* be, Node* accessExpr,
	                                std::vector<Node*>* newNodes,
	                                Node* enclosingVarStmt);
	ModifierList* getExportModifier(Node* enclosingVarStmt);
	Node* inferType(Node* node, ::tsc::checker::Type* variableType);
	nodebuilder::Flags getExtraFlags(Node* node, ::tsc::checker::Type* t);
	Node* createTypeOfFromEntityNameExpression(Node* node);
	Node* typeFromArraySpreadElements(ArrayLiteralExpression* node,
	                                  const std::string& name);
	Node* typeFromObjectSpreadAssignment(ObjectLiteralExpression* node,
	                                     const std::string& name);
	Node* typeFromSpreads(
	    Node* node, const std::string& name, bool isInConstContext,
	    const std::function<std::vector<Node*>(Node*)>& getChildren,
	    const std::function<bool(Node*)>& isSpread,
	    const std::function<Node*(Node*)>& createSpread,
	    const std::function<Node*(std::vector<Node*>)>& makeNodeOfKind,
	    const std::function<Node*(std::vector<Node*>)>& finalType);
	void makeSpreadVariable(
	    NodeFactory* factory, const std::string& name,
	    bool isInConstContext, Node* statement,
	    const std::function<Node*(Node*)>& createSpread, Node* expression,
	    std::vector<Node*>* intersectionTypes,
	    std::vector<Node*>* newSpreads);
	void finalizesVariablePart(
	    NodeFactory* factory, const std::string& name,
	    bool isInConstContext, Node* statement,
	    const std::function<Node*(std::vector<Node*>)>& makeNodeOfKind,
	    const std::function<Node*(Node*)>& createSpread,
	    std::vector<Node*>* currentVariableProperties,
	    std::vector<Node*>* intersectionTypes,
	    std::vector<Node*>* newSpreads);
	Node* relativeType(Node* node);
	Node* typeToMinimizedReferenceType(::tsc::checker::Type* t,
	                                   Node* enclosingDecl,
	                                   nodebuilder::Flags flags);
	std::string addTypeToVariableLike(Node* decl);
	void addSymbolToExistingImport(Symbol* sym);
};

// === codeactions.go — shared types ===

struct CodeAction {
	std::string Description;
	std::vector<lsproto::TextEdit*> Changes;
	std::string FixID;
	std::string FixAllDescription;

	int Compare(const CodeAction* b) const;
};

struct CombinedCodeActions {
	std::string Description;
	std::vector<lsproto::TextEdit*> Changes;
};

struct CodeFixContext;

struct CodeFixProvider {
	std::vector<int32_t> ErrorCodes;
	std::function<std::pair<std::vector<CodeAction*>, gostd::Error>(
	    const gostd::Context& ctx, CodeFixContext* fixContext)>
		GetCodeActions;
	std::vector<std::string> FixIds;
	std::function<std::pair<CombinedCodeActions*, gostd::Error>(
	    const gostd::Context& ctx, CodeFixContext* fixContext)>
		GetAllCodeActions;
};

struct CodeFixContext {
	SourceFile* SourceFile = nullptr;
	TextRange Span = TextRange::undefined();
	int32_t ErrorCode = 0;
	compiler::SimpleProgram* Program = nullptr;
	LanguageService* LS = nullptr;
	lsproto::Diagnostic* Diagnostic = nullptr;
	lsproto::CodeActionParams* Params = nullptr;
};

// Providers defined by this slice (codeactions_importfixes.go etc.) and by
// sibling slices; the codeFixProviders table in codeactions.cpp references
// them.
extern CodeFixProvider* ImportFixProvider;
extern CodeFixProvider* IsolatedDeclarationsFixProvider;
extern CodeFixProvider* FixClassIncorrectlyImplementsInterfaceProvider;

// === displaypartswriter.go ===
// displayPartsWriter implements printer::EmitTextWriter and captures
// classified text runs for VS colorized labels, while also building a plain
// string. When vsCapability is false, only the plain string is built; runs
// are skipped.
struct displayPartsWriter : printer::EmitTextWriter {
	std::string builder;
	std::vector<lsproto::VSClassifiedTextRun*> runs;
	bool vsCapability = false;
	std::string lastWritten;

	void addRun(lsproto::ClassificationTypeName classification,
	            const std::string& text);
	void WriteClassified(const std::string& text,
	                     lsproto::ClassificationTypeName classification);
	void WriteFrom(displayPartsWriter* other);
	std::vector<lsproto::VSClassifiedTextRun*> GetRuns() const;

	// --- EmitTextWriter ---
	std::string String() override;
	void Clear() override;
	void DecreaseIndent() override;
	TextPos GetColumn() override;
	int GetIndent() override;
	int GetLine() override;
	int GetTextPos() override;
	bool HasTrailingComment() override;
	bool HasTrailingWhitespace() override;
	void IncreaseIndent() override;
	bool IsAtStartOfLine() override;
	void RawWrite(const std::string& s) override;
	void Write(const std::string& s) override;
	void WriteComment(const std::string& text) override;
	void WriteKeyword(const std::string& text) override;
	void WriteLine() override;
	void WriteLineForce(bool force) override;
	void WriteLiteral(const std::string& s) override;
	void WriteOperator(const std::string& text) override;
	void WriteParameter(const std::string& text) override;
	void WriteProperty(const std::string& text) override;
	void WritePunctuation(const std::string& text) override;
	void WriteSpace(const std::string& text) override;
	void WriteStringLiteral(const std::string& text) override;
	void WriteSymbol(const std::string& text, Symbol* symbol) override;
	void WriteTrailingSemicolon(const std::string& text) override;
};

displayPartsWriter* newDisplayPartsWriter(bool vsCapability);

// classificationForSymbol — displaypartswriter.go:168.
lsproto::ClassificationTypeName classificationForSymbol(Symbol* symbol);
bool isFirstDeclarationOfSymbolParameter(Symbol* symbol);

// === source_map.go ===
struct script {
	std::string fileName;
	std::string text;

	std::string FileName() const { return fileName; }
	std::string OriginalFileName() const { return fileName; }
	std::string Text() const { return text; }
	std::string OriginalText() const { return text; }
	spanmap::SpanMap* SpanMap() const { return nullptr; }
};

// === crossproject.go ===
struct Project {
	virtual ~Project() = default;
	virtual std::string Id() = 0;
	virtual compiler::SimpleProgram* GetProgram() = 0;
	virtual bool HasFile(const std::string& fileName) = 0;
};

struct projectAndTextDocumentPosition {
	Project* project = nullptr;
	LanguageService* ls = nullptr;
	lsproto::DocumentUri Uri;
	lsproto::Position Position;
	SymbolAndEntriesData* symbolData = nullptr;
	bool forOriginalLocation = false;
};

template <typename Resp>
struct response {
	bool complete = false;
	Resp result;
	bool forOriginalLocation = false;
};

struct CrossProjectOrchestrator {
	virtual ~CrossProjectOrchestrator() = default;
	virtual Project* GetDefaultProject() = 0;
	virtual std::vector<Project*> GetAllProjectsForInitialRequest() = 0;
	virtual LanguageService* GetLanguageServiceForProjectWithFile(
	    const gostd::Context& ctx, Project* project,
	    lsproto::DocumentUri uri) = 0;
	virtual std::pair<std::vector<Project*>, gostd::Error> GetProjectsForFile(
	    const gostd::Context& ctx, lsproto::DocumentUri uri) = 0;
	// iter.Seq[Project] — pull-based: call yield until it returns false.
	virtual void GetProjectsLoadingProjectTree(
	    const gostd::Context& ctx,
	    collections::Set<tspath::Path>* requestedProjectTrees,
	    const std::function<bool(Project*)>& yield) = 0;
};

// combine helpers — crossproject.go.

// combineLocationArray — crossproject.go:298.
template <typename T>
std::vector<T> combineLocationArray(
    std::vector<T> combined, std::vector<T>* locations,
    collections::Set<lsproto::Location>* seen) {
	for (auto& loc : *locations) {
		lsproto::Location key;
		if constexpr (std::is_pointer_v<T>) {
			key = loc->GetLocation();
		} else {
			key = loc.GetLocation();
		}
		if (!seen->Has(key)) {
			seen->Add(key);
			combined.push_back(loc);
		}
	}
	return combined;
}

template <lsproto::HasLocations T>
std::vector<lsproto::Location>* combineResponseLocations(
    const std::function<void(const std::function<bool(T&)>&)>& resultsSeq) {
	auto* combined = new std::vector<lsproto::Location>();
	collections::Set<lsproto::Location> seenLocations;
	resultsSeq([&](T& resp) -> bool {
		if (auto* locations = resp.GetLocations(); locations != nullptr) {
			*combined = combineLocationArray(*combined, locations,
			                                 &seenLocations);
		}
		return true;
	});
	return combined;
}

lsproto::ReferencesResponse combineReferences(
    const std::function<void(
        const std::function<bool(lsproto::ReferencesResponse&)>&)>& results);
lsproto::VSReferencesResponse combineVSReferences(
    const std::function<void(
        const std::function<bool(lsproto::VSReferencesResponse&)>&)>& results);
lsproto::ImplementationResponse combineImplementations(
    const std::function<void(const std::function<bool(
        lsproto::ImplementationResponse&)>&)>& results);
lsproto::RenameResponse combineRenameResponse(
    const std::function<void(
        const std::function<bool(lsproto::RenameResponse&)>&)>& results);
lsproto::CallHierarchyIncomingCallsResponse combineIncomingCalls(
    const std::function<void(const std::function<bool(
        lsproto::CallHierarchyIncomingCallsResponse&)>&)>& results);

// === file_rename.go types ===
using pathUpdater = std::function<std::pair<std::string, bool>(
    const std::string& path)>;

struct toImport {
	std::string newFileName;
	bool updated = false;
};

struct movedFile {
	SourceFile* sourceFile = nullptr;
	std::string newFileName;
};

// signaturehelp.go:24 — trigger / retrigger chars shared by the static
// server capabilities and the dynamic content-mapper registration.
extern const std::vector<std::string> SignatureHelpTriggerCharacters;
extern const std::vector<std::string> SignatureHelpRetriggerCharacters;

// === signaturehelp.go — value types needed by method decls ===
struct signatureInformation {
	std::string Label;
	std::string* Documentation = nullptr;
	std::vector<struct signatureHelpParameter> Parameters;
	bool IsVariadic = false;
	std::vector<lsproto::VSClassifiedTextRun*> ColorizedRuns;
};

struct signatureHelpParameter {
	lsproto::ParameterInformation* parameterInfo = nullptr;
	bool isRest = false;
	bool isOptional = false;
};

struct signatureHelpItemInfo {
	bool isVariadic = false;
	std::vector<signatureHelpParameter> parameters;
	displayPartsWriter* writer = nullptr;
};

struct displayPartsWriter;

// === signaturehelp.go — invocation/argument-info types ===

// callInvocation / typeArgsInvocation / contextualInvocation —
// signaturehelp.go:29,33,37.
struct callInvocation {
	Node* node = nullptr;
};
struct typeArgsInvocation {
	Node* called = nullptr;
};
struct contextualInvocation {
	checker::Signature* signature = nullptr;
	Node* node = nullptr; // Just for enclosingDeclaration for printing types
	Symbol* symbol = nullptr;
};
struct invocation {
	callInvocation* callInvocation = nullptr;
	typeArgsInvocation* typeArgsInvocation = nullptr;
	contextualInvocation* contextualInvocation = nullptr;
};

// argumentListInfo — signaturehelp.go:906.
struct argumentListInfo {
	bool isTypeParameterList = false;
	invocation* invocation = nullptr;
	TextRange argumentsSpan = TextRange::undefined();
	int argumentIndex = 0;
	/** argumentCount is the *apparent* number of arguments. */
	int argumentCount = 0;
};

// candidateInfo / CandidateOrTypeInfo — signaturehelp.go:732,736.
struct candidateInfo {
	std::vector<checker::Signature*> candidates;
	checker::Signature* resolvedSignature = nullptr;
};
struct CandidateOrTypeInfo {
	candidateInfo* candidateInfo = nullptr;
	Symbol* typeInfo = nullptr;
};

// argumentOrParameterListInfo / argumentOrParameterListAndIndex —
// signaturehelp.go:1104,1157.
struct argumentOrParameterListInfo {
	NodeList* list = nullptr;
	int argumentIndex = 0;
	int argumentCount = 0;
	TextRange argumentsSpan = TextRange::undefined();
};
struct argumentOrParameterListAndIndex {
	NodeList* list = nullptr;
	int argumentIndex = 0;
};

// contextualSignatureLocationInfo — signaturehelp.go:1041.
struct contextualSignatureLocationInfo {
	checker::Type* contextualType = nullptr;
	int argumentIndex = 0;
	int argumentCount = 0;
	TextRange argumentsSpan = TextRange::undefined();
};

// PossibleTypeArgumentInfo — utilities.go:103 (ls-coreA).
struct PossibleTypeArgumentInfo {
	Node* called = nullptr;
	int nTypeArguments = 0;
};

// === sibling-owned free functions (dep-stubbed; ls.h decls so calls compile) ===
// utilities.go — ls-coreA
Node* getAdjustedLocation(Node* node, bool forRename, SourceFile* sourceFile);
Node* getAdjustedRenameLocation(Node* node);
checker::Type* getContextualTypeFromParentOrAncestorTypeNode(
    Node* node, checker::Checker* checker);
bool IsInString(SourceFile* sourceFile, int position, Node* previousToken);
CommentRange* isInComment(SourceFile* file, int position,
                          Node* tokenAtPosition);
std::vector<Node*> getChildrenFromNonJSDocNode(Node* node,
                                             SourceFile* sourceFile);
PossibleTypeArgumentInfo* getPossibleTypeArgumentsInfo(
    Node* tokenIn, SourceFile* sourceFile);
std::vector<checker::Signature*> getPossibleGenericSignatures(
    Node* called, int typeArgumentCount, checker::Checker* c);
bool isNoSubstitutionTemplateLiteral(Node* node);
bool isTaggedTemplateExpression(Node* node);
bool isInsideTemplateLiteral(Node* node, int position,
                             SourceFile* sourceFile);
bool isTemplateHead(Node* node);
bool isTemplateTail(Node* node);
NodeList* findContainingList(Node* node, SourceFile* file);
// rename.go — ls-coreC
bool nodeIsEligibleForRename(Node* node);
// hover.go — ls-coreC (displayPartWriter mapping for docs)
// utilities.go — ls-coreA (additional free fns used by findallreferences.go)
SemanticMeaning getMeaningFromLocation(Node* node);
SemanticMeaning getIntersectingMeaningFromDeclarations(
    Node* node, Symbol* symbol, SemanticMeaning defaultMeaning);
Node* getContainingObjectLiteralElement(Node* node);
bool isImplementation(Node* node);
bool isImplementationExpression(Node* node);
Node* getContainingNodeIfInHeritageClause(Node* node);
Symbol* getPropertySymbolsFromBaseTypes(
    Symbol* symbol, const std::string& propertyName, checker::Checker* checker,
    const std::function<Symbol*(Symbol*)>& cb);
std::vector<Symbol*> getParentSymbolsOfPropertyAccess(
    Node* location, Symbol* symbol, checker::Checker* ch);
Symbol* getNonModuleSymbolOfMergedModuleSymbol(Symbol* symbol);
Symbol* getPropertySymbolFromBindingElement(checker::Checker* checker,
                                            Node* bindingElement);
Node* getContainerNode(Node* node);
bool isTypeKeyword(Kind kind);
bool isReadonlyTypeOperator(Node* node);
bool isJumpStatementTarget(Node* node);
Node* getTargetLabel(Node* referenceNode, const std::string& labelName);
bool isLabelOfLabeledStatement(Node* node);
bool isThis(Node* node);
bool isLiteralNameOfPropertyDeclarationOrIndexAccess(Node* node);
bool isNameOfModuleDeclaration(Node* node);
bool isExpressionOfExternalModuleImportEqualsDeclaration(Node* node);
bool isObjectBindingElementWithoutPropertyName(Node* bindingElement);
bool isStaticSymbol(Symbol* symbol);
refInfo* getReferenceAtPosition(SourceFile* sourceFile, int position,
                                compiler::SimpleProgram* program);
Symbol* getLocalSymbolForExportSpecifier(Node* referenceLocation,
                                         Symbol* referenceSymbol,
                                         ExportSpecifier* exportSpecifier,
                                         checker::Checker* ch);
TextRange* toContextRange(TextRange* textRange, SourceFile* contextFile,
                          Node* context);
bool isModuleSpecifierLike(Node* node);

// symbolDisplayInfo — hover.go:418 (owned by ls-coreC). Only displayParts
// is read by findallreferences.cpp.
struct symbolDisplayInfo {
	displayPartsWriter* displayParts = nullptr;
};
symbolDisplayInfo getQuickInfoAndDeclarationAtLocation(
    checker::Checker* c, Symbol* symbol, Node* node,
    checker::VerbosityContext* vc, bool vsCapability,
    SemanticMeaning meaning);

// hover.go:204 — documentationLocationMapper (the callable type).
using documentationLocationMapper = std::function<
    std::pair<lsproto::Location, spanmap::Fidelity>(SourceFile*, TextRange)>;
// hover.go:306 — getDocumentationFromDeclaration (ls-coreC).
std::string getDocumentationFromDeclaration(
    documentationLocationMapper getMappedLocation, checker::Checker* c,
    Symbol* symbol, Node* declaration, Node* location,
    lsproto::MarkupKind contentFormat, bool commentOnly);

// === languageservice.go ===
// LanguageService also satisfies sourcemap::Host (source_map.go passes `l`
// to sourcemap.GetDocumentPositionMapper).
class LanguageService : public sourcemap::Host {
public:
	autoimport::ProjectID projectID;
	ls::Host* host = nullptr; // ls:: qualified: base-class injection makes 'Host' mean sourcemap::Host here
	lsutil::UserPreferences activeConfig;
	compiler::SimpleProgram* program = nullptr;
	lsconv::Converters* converters = nullptr;
	std::unordered_map<std::string, sourcemap::DocumentPositionMapper*>
		documentPositionMappers;

	// --- languageservice.go ---
	tspath::Path toPath(const std::string& fileName);
	compiler::SimpleProgram* GetProgram();
	lsutil::UserPreferences UserPreferences();
	lsutil::FormatCodeSettings FormatOptions();
	std::pair<compiler::SimpleProgram*, SourceFile*> tryGetProgramAndFile(
	    const std::string& fileName);
	std::pair<compiler::SimpleProgram*, SourceFile*> getProgramAndFile(
	    lsproto::DocumentUri documentURI);
	sourcemap::DocumentPositionMapper* GetDocumentPositionMapper(
	    const std::string& fileName);
	std::pair<std::string, bool> ReadFile(
	    std::string_view fileName) override;
	bool UseCaseSensitiveFileNames() override;
	sourcemap::ECMALineInfo* GetECMALineInfo(
	    std::string_view fileName) override;
	std::pair<autoimport::View*, gostd::Error> getPreparedAutoImportView(
	    SourceFile* fromFile, checker::Checker* typeChecker);
	autoimport::View* getCurrentAutoImportView(SourceFile* fromFile,
	                                           checker::Checker* typeChecker);
	bool DirectoryExists(const std::string& path);
	std::vector<std::string> ReadDirectory(
	    const std::string& path,
	    const std::vector<std::string>& extensions,
	    const std::vector<std::string>& includes);
	std::vector<std::string> GetDirectories(const std::string& path);

	// --- diagnostics.go ---
	std::pair<lsproto::DocumentDiagnosticResponse, gostd::Error>
	ProvideDiagnostics(const gostd::Context& ctx, lsproto::DocumentUri uri);
	std::vector<lsproto::Diagnostic*> toLSPDiagnostics(
	    const gostd::Context& ctx,
	    std::vector<std::vector<Diagnostic*>> diagnostics);

	// --- source_map.go ---
	std::pair<lsproto::Location, spanmap::Fidelity>
	sourceFileRangeToLSPLocation(SourceFile* file, TextRange fileRange);
	std::pair<lsproto::Location, spanmap::Fidelity>
	sourceFileRangeToLSPLocationForFeature(SourceFile* file,
	                                       TextRange fileRange,
	                                       spanmap::Feature feature);
	std::pair<lsproto::Location, spanmap::Fidelity> getMappedLocation(
	    const std::string& fileName, TextRange fileRange);
	script* getScript(const std::string& fileName);
	sourcemap::DocumentPosition* tryGetSourcePosition(
	    const std::string& fileName, TextPos position);
	sourcemap::DocumentPosition* tryGetSourcePositionWorker(
	    const std::string& fileName, TextPos position);
	sourcemap::DocumentPosition* tryGetGeneratedPosition(
	    const std::string& fileName, TextPos position);
	sourcemap::DocumentPosition* tryGetGeneratedPositionWorker(
	    const std::string& fileName, TextPos position);

	// --- codeactions.go ---
	std::pair<lsproto::CommandOrCodeActionArrayOrNull, gostd::Error>
	ProvideCodeActions(const gostd::Context& ctx,
	                   lsproto::CodeActionParams* params);
	std::pair<std::vector<lsproto::CommandOrCodeAction>, gostd::Error>
	getFixAllQuickFixes(
	    const gostd::Context& ctx, compiler::SimpleProgram* program,
	    SourceFile* file, lsproto::DocumentUri uri,
	    const std::unordered_map<std::string, CodeFixProvider*>& fixIdSeen);
	std::pair<lsproto::CommandOrCodeAction*, gostd::Error> createFixAllAction(
	    const gostd::Context& ctx, compiler::SimpleProgram* program,
	    SourceFile* file, lsproto::DocumentUri uri);
	lsproto::CommandOrCodeAction* createOrganizeImportsAction(
	    const gostd::Context& ctx, compiler::SimpleProgram* program,
	    SourceFile* file, lsproto::CodeActionKind kind);

	// --- signaturehelp.go ---
	std::pair<lsproto::SignatureHelpOrNull, gostd::Error> ProvideSignatureHelp(
	    const gostd::Context& ctx, lsproto::DocumentUri documentURI,
	    lsproto::Position position, lsproto::SignatureHelpContext* context);
	lsproto::SignatureHelp* GetSignatureHelpItems(
	    const gostd::Context& ctx, int position,
	    compiler::SimpleProgram* program, SourceFile* sourceFile,
	    lsproto::SignatureHelpContext* context);
	lsproto::SignatureHelp* createJSSignatureHelpItems(
	    const gostd::Context& ctx, argumentListInfo* argumentInfo,
	    compiler::SimpleProgram* program, checker::Checker* c);
	lsproto::SignatureHelp* findSignatureHelpFromNamedDeclarations(
	    const gostd::Context& ctx, SourceFile* sourceFile,
	    const std::string& name, argumentListInfo* argumentInfo,
	    checker::Checker* c);
	lsproto::SignatureHelp* createSignatureHelpItems(
	    const gostd::Context& ctx,
	    const std::vector<checker::Signature*>& candidates,
	    checker::Signature* resolvedSignature,
	    argumentListInfo* argumentInfo, SourceFile* sourceFile,
	    checker::Checker* c, bool useFullPrefix);
	lsproto::UintegerOrNull* computeActiveParameter(
	    signatureInformation sig, int argumentIndex, bool supportsNull);
	std::vector<signatureInformation> getSignatureHelpItem(
	    checker::Signature* candidate, bool isTypeParameterList,
	    const std::string& callTargetSymbol, Symbol* callTargetSym,
	    Node* enclosingDeclaration, SourceFile* sourceFile,
	    checker::Checker* c, lsproto::MarkupKind docFormat,
	    bool vsCapability);
	std::vector<signatureHelpItemInfo*> itemInfoForTypeParameters(
	    checker::Signature* candidateSignature, checker::Checker* c,
	    Node* enclosingDeclaration, SourceFile* sourceFile,
	    lsproto::MarkupKind docFormat, bool vsCapability);
	std::vector<signatureHelpItemInfo*> itemInfoForParameters(
	    checker::Signature* candidateSignature, checker::Checker* c,
	    Node* enclosingDeclaratipn, SourceFile* sourceFile,
	    lsproto::MarkupKind docFormat, bool vsCapability);
	signatureHelpParameter createSignatureHelpParameterFromLabel(
	    Symbol* parameter, const std::string& label, checker::Checker* c,
	    lsproto::MarkupKind docFormat);
	signatureHelpParameter createSignatureHelpParameterForParameter(
	    Symbol* parameter, Node* enclosingDeclaration, printer::Printer* p,
	    SourceFile* sourceFile, checker::Checker* c,
	    lsproto::MarkupKind docFormat);

	// hover.go:206 — documentationLocationMapper (ls-coreC).
	documentationLocationMapper documentationLocationMapper(
	    spanmap::Feature feature);

	// --- findallreferences.go ---
	lsproto::Range getRangeOfEntry(ReferenceEntry* entry);
	std::pair<lsproto::Range, bool> getRangeOfEntryForFeature(
	    ReferenceEntry* entry, spanmap::Feature feature);
	lsproto::DocumentUri getFileNameOfEntry(ReferenceEntry* entry);
	std::pair<lsproto::Location, bool> getLocationOfEntryForFeature(
	    ReferenceEntry* entry, spanmap::Feature feature);
	void resolveEntrySource(ReferenceEntry* entry);
	ReferenceEntry* resolveEntry(ReferenceEntry* entry);
	nonLocalDefinition* getNonLocalDefinition(const gostd::Context& ctx,
	                                          SymbolAndEntries* entry);
	void forEachOriginalDefinitionLocation(
	    const gostd::Context& ctx, SymbolAndEntries* entry,
	    const std::function<void(lsproto::DocumentUri, lsproto::Position)>& cb);
	std::pair<SymbolAndEntriesData, bool> provideSymbolsAndEntries(
	    const gostd::Context& ctx, lsproto::DocumentUri uri,
	    lsproto::Position documentPosition, bool isRename,
	    bool implementations);
	std::pair<SymbolAndEntriesData, bool> provideSymbolsAndEntriesAtPosition(
	    const gostd::Context& ctx, compiler::SimpleProgram* program,
	    SourceFile* sourceFile, int position, bool isRename,
	    bool implementations);
	std::vector<SymbolAndEntries*> getSymbolAndEntries(
	    const gostd::Context& ctx, int position, Node* node,
	    compiler::SimpleProgram* program, bool isRename,
	    bool implementations);
	std::pair<lsproto::ReferencesResponse, gostd::Error> ProvideReferences(
	    const gostd::Context& ctx, lsproto::ReferenceParams* params,
	    CrossProjectOrchestrator* orchestrator);
	std::pair<lsproto::ReferencesResponse, gostd::Error>
	provideReferencesFromData(const gostd::Context& ctx,
	                          lsproto::ReferenceParams* params,
	                          CrossProjectOrchestrator* orchestrator,
	                          SymbolAndEntriesData data);
	std::pair<lsproto::VSReferencesResponse, gostd::Error> ProvideVSReferences(
	    const gostd::Context& ctx, lsproto::ReferenceParams* params,
	    CrossProjectOrchestrator* orchestrator);
	std::pair<lsproto::ReferencesResponse, gostd::Error>
	symbolAndEntriesToReferences(const gostd::Context& ctx,
	                             lsproto::ReferenceParams* params,
	                             SymbolAndEntriesData data,
	                             symbolEntryTransformOptions options);
	std::pair<lsproto::VSReferencesResponse, gostd::Error>
	symbolAndEntriesToVSReferences(const gostd::Context& ctx,
	                               lsproto::ReferenceParams* params,
	                               SymbolAndEntriesData data,
	                               symbolEntryTransformOptions options);
	referencedSymbolDefinitionInfo* definitionToReferencedSymbolDefinitionInfo(
	    const gostd::Context& ctx, Definition* def, Node* originalNode,
	    bool vsCapability, spanmap::Feature feature);
	lsproto::VSClassifiedTextElement* getDefinitionKindAndDisplayParts(
	    const gostd::Context& ctx, Symbol* symbol, Node* originalNode,
	    bool vsCapability);
	std::pair<lsproto::ImplementationResponse, gostd::Error>
	ProvideImplementations(const gostd::Context& ctx,
	                       lsproto::ImplementationParams* params,
	                       CrossProjectOrchestrator* orchestrator);
	std::pair<lsproto::ImplementationResponse, gostd::Error>
	provideImplementationsEx(const gostd::Context& ctx,
	                         lsproto::ImplementationParams* params,
	                         symbolEntryTransformOptions options,
	                         CrossProjectOrchestrator* orchestrator);
	std::pair<lsproto::ImplementationResponse, gostd::Error>
	provideImplementationsFromData(
	    const gostd::Context& ctx, lsproto::ImplementationParams* params,
	    symbolEntryTransformOptions options,
	    CrossProjectOrchestrator* orchestrator, SymbolAndEntriesData data);
	std::pair<lsproto::ImplementationResponse, gostd::Error>
	symbolAndEntriesToImplementations(
	    const gostd::Context& ctx, lsproto::ImplementationParams* params,
	    SymbolAndEntriesData data, symbolEntryTransformOptions options);
	std::vector<lsproto::Location> convertSymbolAndEntriesToLocations(
	    SymbolAndEntries* s, bool includeDeclarations,
	    spanmap::Feature feature);
	std::vector<lsproto::Location> convertEntriesToLocations(
	    const std::vector<ReferenceEntry*>& entries,
	    spanmap::Feature feature);
	std::vector<lsproto::LocationLink*> convertEntriesToLocationLinks(
	    const std::vector<ReferenceEntry*>& entries,
	    spanmap::Feature feature);
	std::vector<SymbolAndEntries*> mergeReferences(
	    compiler::SimpleProgram* program,
	    const std::vector<std::vector<SymbolAndEntries*>>& referencesToMerge);
	std::vector<SymbolAndEntries*> GetReferencedSymbolsForNode(
	    const gostd::Context& ctx, int position, Node* node,
	    const std::vector<SourceFile*>& sourceFiles);
	std::vector<SignatureUsage> GetSignatureUsages(
	    const gostd::Context& ctx, Node* signatureDecl);
	std::vector<SymbolAndEntries*> getReferencedSymbolsForNode(
	    const gostd::Context& ctx, int position, Node* node,
	    compiler::SimpleProgram* program,
	    const std::vector<SourceFile*>& sourceFiles, refOptions options);
	std::vector<SymbolAndEntries*> getReferencesForStringLiteral(
	    const gostd::Context& ctx, Node* node,
	    const std::vector<SourceFile*>& sourceFiles,
	    checker::Checker* checker);
	std::vector<SymbolAndEntries*>
	getReferencedSymbolsForModuleIfDeclaredBySourceFile(
	    const gostd::Context& ctx, Symbol* symbol,
	    compiler::SimpleProgram* program,
	    const std::vector<SourceFile*>& sourceFiles, checker::Checker* checker,
	    refOptions options, collections::Set<std::string>* sourceFilesSet);
	std::vector<SymbolAndEntries*> getReferencedSymbolsForModule(
	    const gostd::Context& ctx, compiler::SimpleProgram* program,
	    Symbol* symbol, bool excludeImportTypeOfExportEquals,
	    const std::vector<SourceFile*>& sourceFiles,
	    collections::Set<std::string>* sourceFilesSet);

	// --- crossproject.go ---
	// handleCrossProject — the Go generic is a C++20 template constrained on
	// lsproto::HasTextDocumentPosition.
	template <lsproto::HasTextDocumentPosition Req, typename Resp>
	std::pair<Resp, gostd::Error> handleCrossProject(
	    const gostd::Context& ctx, Req* params,
	    CrossProjectOrchestrator* orchestrator,
	    const std::function<
	        std::pair<Resp, gostd::Error>(LanguageService*,
	                                    const gostd::Context&, Req*,
	                                    SymbolAndEntriesData,
	                                    symbolEntryTransformOptions)>&
	        symbolAndEntriesToResp,
	    const std::function<Resp(
	        const std::function<void(const std::function<bool(Resp&)>&)>&)>&
	        combineResults,
	    bool isRename, bool implementations,
	    symbolEntryTransformOptions options,
	    SymbolAndEntriesData* defaultProjectData);

	// --- file_rename.go ---
	std::vector<lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile>
	GetEditsForFileRename(const gostd::Context& ctx,
	                      lsproto::DocumentUri oldURI,
	                      lsproto::DocumentUri newURI);
	pathUpdater createPathUpdater(const std::string& oldPath,
	                              const std::string& newPath);
	void updateTsconfigFiles(compiler::SimpleProgram* program,
	                         change::Tracker* changeTracker,
	                         pathUpdater oldToNew, const std::string& oldPath,
	                         const std::string& newPath);
	std::string updateRelativePath(pathUpdater oldToNew,
	                               const std::string& oldImportFromPath,
	                               const std::string& newImportFromPath,
	                               const std::string& relativeSpecifier);
	void updateImportsForFileRename(compiler::SimpleProgram* program,
	                                change::Tracker* changeTracker,
	                                pathUpdater oldToNew);
	std::string getUpdatedImportSpecifier(
	    compiler::SimpleProgram* program, checker::Checker* ch,
	    SourceFile* sourceFile, Node* importLiteral,
	    pathUpdater oldToNew, const std::vector<movedFile>& movedFiles,
	    const std::string& newImportFromPath,
	    bool importingSourceFileMoved,
	    const modulespecifiers::UserPreferences& userPreferences);

	// --- organizeimports.go ---
	std::unordered_map<std::string, std::vector<lsproto::TextEdit*>>
	OrganizeImports(const gostd::Context& ctx, SourceFile* sourceFile,
	                compiler::SimpleProgram* program,
	                lsproto::CodeActionKind kind);

	// --- sibling-owned methods (dep-stubs) ---
	// utilities.go — ls-coreA
	template <lsconv::Script S>
	std::pair<lsproto::Range, spanmap::Fidelity> createLspRangeFromRange(
	    TextRange textRange, S* s) {
		TSC_UNREACHABLE(
		    "LanguageService::createLspRangeFromRange — owned by ls-coreA");
	}

};

// NewLanguageService — languageservice.go:25.
LanguageService* NewLanguageService(autoimport::ProjectID projectID,
                                    compiler::SimpleProgram* program,
                                    Host* host, const std::string& activeFile);

namespace detail {
// local workGroup — core.NewWorkGroup(runSequential=false): Queue collects
// tasks, RunAndWait spawns them in parallel and drains the queue, including
// items enqueued while the run is in flight.
struct workGroup {
	std::vector<std::function<void()>> queue;
	size_t next = 0;
	std::mutex mu;

	void Queue(std::function<void()> f) {
		std::lock_guard<std::mutex> lk(mu);
		queue.push_back(std::move(f));
	}

	void RunAndWait() {
		for (;;) {
			std::vector<std::thread> ts;
			{
				std::lock_guard<std::mutex> lk(mu);
				for (; next < queue.size(); next++) {
					ts.emplace_back(queue[next]);
				}
			}
			for (auto& t : ts) {
				t.join();
			}
			{
				std::lock_guard<std::mutex> lk(mu);
				if (next >= queue.size()) {
					queue.clear();
					next = 0;
					return;
				}
			}
		}
	}

	void Reset() {
		std::lock_guard<std::mutex> lk(mu);
		queue.clear();
		next = 0;
	}
};
} // namespace detail

// handleCrossProject — crossproject.go:46. Defined out-of-class since it is
// a member template.
template <lsproto::HasTextDocumentPosition Req, typename Resp>
inline std::pair<Resp, gostd::Error> LanguageService::handleCrossProject(
    const gostd::Context& ctx, Req* params,
    CrossProjectOrchestrator* orchestrator,
    const std::function<
        std::pair<Resp, gostd::Error>(LanguageService*,
                                    const gostd::Context&, Req*,
                                    SymbolAndEntriesData,
                                    symbolEntryTransformOptions)>&
        symbolAndEntriesToResp,
    const std::function<Resp(
        const std::function<void(const std::function<bool(Resp&)>&)>&)>&
        combineResults,
    bool isRename, bool implementations,
    symbolEntryTransformOptions options,
    SymbolAndEntriesData* defaultProjectData) {
	Resp resp{};
	gostd::Error err;

	// Single project
	if (orchestrator == nullptr) {
		SymbolAndEntriesData data;
		if (defaultProjectData != nullptr) {
			data = *defaultProjectData;
		} else {
			auto [d, _ok] = provideSymbolsAndEntries(
			    ctx, params->TextDocumentURI(),
			    params->TextDocumentPosition(), isRename, implementations);
			data = d;
		}
		return symbolAndEntriesToResp(this, ctx, params, data, options);
	}

	auto* defaultProject = orchestrator->GetDefaultProject();
	auto allProjects = orchestrator->GetAllProjectsForInitialRequest();
	collections::SyncMap<std::string, response<Resp>*> results;
	nonLocalDefinition* defaultDefinition = nullptr;
	auto canSearchProject = [&](Project* project) {
		auto [_, searched] = results.Load(project->Id());
		return !searched;
	};
	detail::workGroup wg;
	std::mutex errMu;
	std::vector<std::string> panicsOccurred;
	std::mutex panicMu;
	std::function<void(const projectAndTextDocumentPosition&)> enqueueItem =
	    [&](const projectAndTextDocumentPosition& item) {
		    auto* respPtr = new response<Resp>();
		    if (auto [_, loaded] =
		            results.LoadOrStore(item.project->Id(), respPtr);
		        loaded) {
			    delete respPtr;
			    return;
		    }
		    wg.Queue([&, item, respPtr] {
			    if (gostd::ctxErr(ctx) != nullptr) {
				    return;
			    }
			    try {
				    // Process the item
				    LanguageService* ls = item.ls;
				    if (ls == nullptr) {
					    // Get it now
					    ls = orchestrator
					             ->GetLanguageServiceForProjectWithFile(
					                 ctx, item.project, item.Uri);
					    if (ls == nullptr) {
						    return;
					    }
				    }
				    SymbolAndEntriesData data;
				    bool ok;
				    if (item.symbolData != nullptr) {
					    data = *item.symbolData;
					    ok = true;
				    } else {
					    auto [d, o] =
					        ls->provideSymbolsAndEntries(
					            ctx, item.Uri, item.Position,
					            isRename, implementations);
					    data = d;
					    ok = o;
				    }
				    if (gostd::ctxErr(ctx) != nullptr) {
					    return;
				    }
				    if (ok) {
					    for (auto* entry : data.SymbolsAndEntries) {
						    // Find the default definition that can be
						    // in another project
						    // Later we will use this load ancestor tree
						    // that references this location and expand
						    // search
						    if (item.project == defaultProject &&
						        defaultDefinition == nullptr) {
							    defaultDefinition =
							        ls->getNonLocalDefinition(ctx,
							                                  entry);
						    }
						    ls->forEachOriginalDefinitionLocation(
						        ctx, entry,
						        [&](lsproto::DocumentUri uri,
						            lsproto::Position position) {
							        // Get default configured project
							        // for this file
							        auto [defProjects, errProjects] =
							            orchestrator
							                ->GetProjectsForFile(ctx,
							                                     uri);
							        if (errProjects != nullptr) {
								        return;
							        }
							        for (auto* defProject :
							             defProjects) {
								        // Optimization: don't enqueue
								        // if will be discarded
								        if (canSearchProject(
								                defProject)) {
									        enqueueItem(
									            projectAndTextDocumentPosition{
									                .project =
									                    defProject,
									                .Uri = uri,
									                .Position =
									                    position,
									                .forOriginalLocation =
									                    true,
									            });
								        }
							        }
						        });
					    }
				    }

				    auto [result, errSearch] = symbolAndEntriesToResp(
				        ls, ctx, params, data, options);
				    if (errSearch == nullptr) {
					    respPtr->complete = true;
					    respPtr->result = result;
					    respPtr->forOriginalLocation =
					        item.forOriginalLocation;
				    } else {
					    std::lock_guard<std::mutex> lk(errMu);
					    if (err == nullptr) {
						    err = errSearch;
					    }
				    }
			    } catch (const std::exception& e) {
				    // recover() in the Go deferred func
				    std::lock_guard<std::mutex> lk(panicMu);
				    panicsOccurred.push_back(
				        std::string("panic handling request: ") +
				        e.what());
			    } catch (...) {
				    std::lock_guard<std::mutex> lk(panicMu);
				    panicsOccurred.push_back(
				        "panic handling request: <unknown>");
			    }
		    });
	    };

	// Initial set of projects and locations in the queue, starting with
	// default project
	projectAndTextDocumentPosition initialItem{
	    .project = defaultProject,
	    .ls = this,
	    .Uri = params->TextDocumentURI(),
	    .Position = params->TextDocumentPosition(),
	};
	initialItem.symbolData = defaultProjectData;
	enqueueItem(initialItem);
	for (auto* project : allProjects) {
		if (project != defaultProject) {
			enqueueItem(projectAndTextDocumentPosition{
			    .project = project,
			    // TODO!! symlinks need to change the URI
			    .Uri = params->TextDocumentURI(),
			    .Position = params->TextDocumentPosition(),
			});
		}
	}

	auto getResultsIterator =
	    [&]() -> std::function<void(
	                const std::function<bool(Resp&)>&)> {
		return [&](const std::function<bool(Resp&)>& yield) {
			collections::SyncSet<std::string> seenProjects;
			if (auto [response, loaded] =
			        results.Load(defaultProject->Id());
			    loaded && response->complete) {
				if (!yield(response->result)) {
					return;
				}
			}
			seenProjects.Add(defaultProject->Id());
			for (auto* project : allProjects) {
				if (seenProjects.AddIfAbsent(project->Id())) {
					if (auto [response, loaded] =
					        results.Load(project->Id());
					    loaded && response->complete) {
						if (!yield(response->result)) {
							return;
						}
					}
				}
			}
			// Prefer the searches from locations for default
			// definition
			results.Range(
			    [&](const std::string& key,
			        response<Resp>* const& response) -> bool {
				    if (!response->forOriginalLocation &&
				        seenProjects.AddIfAbsent(key) &&
				        response->complete) {
					    return yield(response->result);
				    }
				    return true;
			    });
			// Then the searches from original locations
			results.Range(
			    [&](const std::string& key,
			        response<Resp>* const& response) -> bool {
				    if (response->forOriginalLocation &&
				        seenProjects.AddIfAbsent(key) &&
				        response->complete) {
					    return yield(response->result);
				    }
				    return true;
			    });
		};
	};

	// Outer loop - to complete work if more is added after completing
	// existing queue
	for (;;) {
		// Process existing known projects first
		wg.RunAndWait();
		// No need to use mu here since we are not in parallel at this
		// point
		if (!panicsOccurred.empty()) {
			std::string msg =
			    "Panics occurred during cross-project handling:";
			for (auto& p : panicsOccurred) {
				msg += " " + p;
			}
			TSC_UNREACHABLE(msg.c_str());
		}
		if (gostd::ctxErr(ctx) != nullptr) {
			return {resp, gostd::ctxErr(ctx)};
		}
		if (err != nullptr) {
			return {resp, err};
		}

		wg.Reset(); // Go: wg = core.NewWorkGroup(false)
		bool hasMoreWork = false;
		if (defaultDefinition != nullptr) {
			collections::Set<tspath::Path> requestedProjectTrees;
			results.Range(
			    [&](const std::string& key,
			        response<Resp>* const& response) -> bool {
				    if (response->complete) {
					    requestedProjectTrees.Add(tspath::Path(key));
				    }
				    return true;
			    });

			// Load more projects based on default definition found
			orchestrator->GetProjectsLoadingProjectTree(
			    ctx, &requestedProjectTrees,
			    [&](Project* loadedProject) -> bool {
				    if (gostd::ctxErr(ctx) != nullptr) {
					    return false;
				    }

				    // Can loop forever without this (enqueue here,
				    // dequeue above, repeat)
				    if (!canSearchProject(loadedProject) ||
				        loadedProject->GetProgram() == nullptr) {
					    return true;
				    }

				    // Enqueue the project and location for further
				    // processing
				    if (loadedProject->HasFile(
				            defaultDefinition->TextDocumentURI()
				                .FileName())) {
					    enqueueItem(
					        projectAndTextDocumentPosition{
					            .project = loadedProject,
					            .Uri = defaultDefinition
					                       ->TextDocumentURI(),
					            .Position = defaultDefinition
					                            ->TextDocumentPosition(),
					        });
					    hasMoreWork = true;
				    } else if (auto* sourcePos =
				                   defaultDefinition
				                       ->GetSourcePosition();
				               sourcePos != nullptr &&
				               loadedProject->HasFile(
				                   sourcePos->TextDocumentURI()
				                       .FileName())) {
					    enqueueItem(
					        projectAndTextDocumentPosition{
					            .project = loadedProject,
					            .Uri = sourcePos
					                       ->TextDocumentURI(),
					            .Position = sourcePos
					                            ->TextDocumentPosition(),
					        });
					    hasMoreWork = true;
				    } else if (auto* generatedPos =
				                   defaultDefinition
				                       ->GetGeneratedPosition();
				               generatedPos != nullptr &&
				               loadedProject->HasFile(
				                   generatedPos
				                       ->TextDocumentURI()
				                       .FileName())) {
					    enqueueItem(
					        projectAndTextDocumentPosition{
					            .project = loadedProject,
					            .Uri = generatedPos
					                       ->TextDocumentURI(),
					            .Position = generatedPos
					                            ->TextDocumentPosition(),
					        });
					    hasMoreWork = true;
				    }
				    return true;
			    });
			if (gostd::ctxErr(ctx) != nullptr) {
				return {resp, gostd::ctxErr(ctx)};
			}
		}
		if (!hasMoreWork) {
			break;
		}
	}

	if (results.Size() > 1) {
		resp = combineResults(getResultsIterator());
	} else {
		// Single result, return that directly
		getResultsIterator()([&](Resp& value) -> bool {
			resp = value;
			return false;
		});
	}
	return {resp, nullptr};
}

// === diagnostics.go free functions ===
std::vector<Diagnostic*> getAllDiagnostics(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    SourceFile* file);
bool isSynthesizedContentMappedDiagnostic(Diagnostic* diag);
Diagnostic* aggregateSynthesizedDiagnostics(
    SourceFile* file, std::vector<Diagnostic*> diags);
DiagnosticCategory worstCategory(std::vector<Diagnostic*> diags);

// === codeactions.go free functions ===
extern std::vector<CodeFixProvider*> codeFixProviders;
bool hasMultipleFixableDiagnostics(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    SourceFile* file, const std::vector<int32_t>& errorCodes);
bool codeFixProviderMatchesLSPDiagnostic(CodeFixProvider* provider,
                                         lsproto::Diagnostic* diagnostic);
bool isFixableDiagnostic(Diagnostic* diagnostic,
                         const std::vector<int32_t>& errorCodes);
bool isFixAllKind(lsproto::CodeActionKind kind);
bool wantsQuickFixes(std::vector<lsproto::CodeActionKind>* only);
std::string getOrganizeImportsActionTitle(const gostd::Context& ctx,
                                          lsproto::CodeActionKind kind);
std::vector<lsproto::CodeActionKind> getOrganizeImportsActionsForKind(
    lsproto::CodeActionKind requestedKind);
bool containsErrorCode(const std::vector<int32_t>& codes, int32_t code);
lsproto::CommandOrCodeAction convertToLSPCodeAction(
    CodeAction* action, lsproto::Diagnostic* diag, lsproto::DocumentUri uri);

} // namespace tsc::ls
