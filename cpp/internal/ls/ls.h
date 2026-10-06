// === dep decls — owned by ls ===
// Decls the api slice needs from tsc/internal/ls (languageservice.go, host.go,
// completions.go, findallreferences.go, jsdoc.go). All types ported faithfully;
// all function/method bodies are stubbed in ls.cpp with TSC_UNREACHABLE. The
// ls slice should replace this file when it lands.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "internal/format/format.h" // lsutil::UserPreferences
#include "internal/gostd/gostd.h"
#include "internal/lsp/lsproto/lsproto.h"

namespace tsc {
struct Node; struct SourceFile; struct Symbol;
namespace compiler { struct SimpleProgram; }
namespace checker { struct Checker; }
namespace lsconv { struct Converters; struct LSPLineMap; }
namespace sourcemap { struct ECMALineInfo; }
namespace autoimport { struct Registry; using ProjectID = std::string; }
} // namespace tsc

namespace tsc::ls {

// Host (host.go) — the snapshot-facing service host.
struct Host {
	virtual ~Host() = default;
	virtual bool UseCaseSensitiveFileNames() = 0;
	virtual std::pair<std::string, bool> ReadFile(const std::string& fileName) = 0;
	virtual lsconv::Converters* Converters() = 0;
	virtual lsutil::UserPreferences GetPreferences(const std::string& activeFile) = 0;
	virtual sourcemap::ECMALineInfo* GetECMALineInfo(const std::string& fileName) = 0;
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

// CompletionItemLabelDetails (lsproto, needed by ls::CompletionItem).
struct CompletionItemLabelDetails {
	std::string Detail;
	std::string Description;
};

// CompletionItem (completions.go:110 + lsproto fields used by the api).
struct CompletionItem {
	std::string Label;
	lsproto::CompletionItemKind Kind{};    // 0 = unset
	bool hasKind{};
	std::string SortText;
	std::string InsertText;
	std::string FilterText;
	std::string Detail;
	CompletionItemLabelDetails* LabelDetails = nullptr; // owned by Items' storage
	Symbol* Symbol = nullptr;
};

// CompletionList (completions.go:115; lsproto fields the api reads).
struct CompletionList {
	bool IsIncomplete{};
	std::vector<std::unique_ptr<CompletionItemLabelDetails>> labelDetailStorage;
	std::vector<std::unique_ptr<CompletionItem>> Items;
};

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

// JSDocTagInfo (jsdoc.go:18).
struct JSDocTagInfo {
	std::string Name;
	std::string Text;
};

// ErrNeedsAutoImports (completions.go:35).
extern const gostd::Error ErrNeedsAutoImports;

// LanguageService (languageservice.go:16) — owned by ls; methods stubbed.
struct LanguageService {
	virtual ~LanguageService() = default;

	virtual std::pair<CompletionList*, gostd::Error> GetCompletionsAtPosition(
		gostd::Context ctx, SourceFile* file, int position,
		const std::string* triggerCharacter, bool includeSymbols) = 0;
	virtual std::vector<SignatureUsage> GetSignatureUsages(
		gostd::Context ctx, Node* signatureDecl) = 0;
	virtual std::vector<SymbolAndEntries*> GetReferencedSymbolsForNode(
		gostd::Context ctx, int position, Node* node,
		const std::vector<SourceFile*>& sourceFiles) = 0;
};

// NewLanguageService (languageservice.go:25) — owned by ls (stub).
LanguageService* NewLanguageService(const std::string& projectID,
                                    compiler::SimpleProgram* program,
                                    Host* host,
                                    const std::string& activeFile);

// GetSymbolDocumentationComment (jsdoc.go:28) — owned by ls (stub).
std::string GetSymbolDocumentationComment(checker::Checker* c, Symbol* symbol);

// GetSymbolJSDocTags (jsdoc.go:51) — owned by ls (stub).
std::vector<JSDocTagInfo> GetSymbolJSDocTags(Symbol* symbol);

} // namespace tsc::ls
