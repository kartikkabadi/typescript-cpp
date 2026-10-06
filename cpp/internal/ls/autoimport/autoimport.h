// Port of tsc/internal/ls/autoimport — auto-import export index, fix search,
// and module-specifier generation.
//
// Ownership model: Go relies on GC sharing of *Export across index clones.
// In C++, Index entries are shared_ptr<Export> so bucket clones share
// ownership exactly like the Go GC; borrowed Export* is used downstream.
#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/symbol.h"
#include "internal/binder/nameresolver.h"
#include "internal/checker/checker.h"
#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/locale/locale.h"
#include "internal/ls/change/change.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/module/cache.h"
#include "internal/module/resolver.h"
#include "internal/module/types.h"
#include "internal/modulespecifiers/types.h"
#include "internal/packagejson/packagejson.h"
#include "internal/project/dirty/dirty.h"
#include "internal/project/logging/logging.h"
#include "internal/scanner/scanner.h"
#include "internal/symlinks/knownsymlinks.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfsmatch/vfsmatch.h"

namespace tsc::ls::autoimport {

// === export.go — export type declarations ===

// ModuleID — export.go:17
using ModuleID = std::string;

// ExportID — export.go:19
struct ExportID {
	ModuleID ModuleID;
	std::string ExportName;

	bool operator==(const ExportID&) const = default;
};

struct ExportIDHash {
	size_t operator()(const ExportID& id) const {
		return std::hash<std::string>{}(id.ModuleID) ^
		       (std::hash<std::string>{}(id.ExportName) << 1);
	}
};

// ExportSyntax — export.go:24
enum class ExportSyntax : int {
	None = 0,
	Modifier,
	Named,
	DefaultModifier,
	DefaultDeclaration,
	Equals,
	UMD,
	Star,
	CommonJSModuleExports,
	CommonJSExportsProperty,
};

// ExportSyntax.String — export_stringer_generated.go
std::string_view exportSyntaxString(ExportSyntax i);

// Export — export.go:48
struct Export {
	ExportID exportID;
	std::string ModuleFileName;
	ExportSyntax Syntax = ExportSyntax::None;
	SymbolFlags Flags{};
	// localName is the name of the export in the module, if different from the exported name
	std::string localName;
	// through is the middle module for a re-export
	std::string through;
	// Target is the real target of a re-exported or aliased symbol
	ExportID Target;
	bool IsTypeOnly = false;
	lsutil::ScriptElementKind ScriptElementKind{};
	lsutil::ScriptElementKindModifier ScriptElementKindModifiers{};
	tspath::Path Path;
	std::string PackageName;

	// Name — export.go:71
	std::string Name() const;
	// IsRenameable — export.go:81
	bool IsRenameable() const;
	// AmbientModuleName — export.go:85
	std::string AmbientModuleName() const;
	// IsUnresolvedAlias — export.go:92
	bool IsUnresolvedAlias() const;
};

// SymbolToExport — export.go:96
std::unique_ptr<Export> SymbolToExport(Symbol* symbol, checker::Checker* ch);

// tryGetModuleExport — export.go:128
std::unique_ptr<Export> tryGetModuleExport(
	const std::string& exportName, Symbol* target, Symbol* moduleSymbol,
	checker::Checker* ch, const ModuleID& moduleID,
	const std::string& moduleFileName, SourceFile* file);

// extractFirstExport — export.go:136
std::unique_ptr<Export> extractFirstExport(
	Symbol* symbol, checker::Checker* ch, const ModuleID& moduleID,
	const std::string& moduleFileName, SourceFile* file);

// === index.go ===

// Index — index.go:19. Go: `Index[T Named]` instantiated only with `*Export`;
// specialized here on shared_ptr<Export> (GC-shared entries, cloned buckets).
struct Index {
	std::vector<std::shared_ptr<Export>> entries;
	std::unordered_map<char32_t, std::vector<int>> index;

	// Find — index.go:24
	std::vector<Export*> Find(const std::string& name, bool caseSensitive) const;
	// SearchWordPrefix — index.go:54
	std::vector<Export*> SearchWordPrefix(const std::string& prefix) const;
	// insertAsWords — index.go:114
	void insertAsWords(const std::shared_ptr<Export>& value);
	// Clone — index.go:152
	std::unique_ptr<Index> Clone(
	    const std::function<bool(const std::shared_ptr<Export>&)>& filter) const;
};

// === extract.go types (declared early: registry result types embed them) ===

// extractorStats — extract.go:34
struct extractorStats {
	std::atomic<int32_t> exports{0};
	std::atomic<int32_t> usedChecker{0};
};

// checkerLease — extract.go:43
struct checkerLease {
	bool used = false;
	checker::Checker* checker = nullptr;

	// GetChecker — extract.go:48
	checker::Checker* GetChecker();
	// TryChecker — extract.go:53
	checker::Checker* TryChecker();
};

// symbolExtractor — extract.go:16
struct symbolExtractor {
	std::string packageName;
	std::unique_ptr<extractorStats> stats;

	std::unique_ptr<binder::NameResolver> localNameResolver;
	checker::Checker* checker = nullptr;
	std::function<tspath::Path(const std::string&)> toPath;
	// realpath, if set, is used to resolve symlinks for ModuleID generation.
	std::function<std::string(const std::string&)> realpath;

	// getModuleID — extract.go:81
	ModuleID getModuleID(SourceFile* file);
	// getModuleIDForSymbol — extract.go:91
	std::pair<ModuleID, bool> getModuleIDForSymbol(Symbol* symbol);
	// extractFromSymbol — extract.go:179
	void extractFromSymbol(const std::string& name, Symbol* symbol,
	                       const ModuleID& moduleID,
	                       const std::string& moduleFileName, SourceFile* file,
	                       std::vector<std::shared_ptr<Export>>* exports);
	// createExport — extract.go:261
	std::pair<std::shared_ptr<Export>, Symbol*> createExport(
	    Symbol* symbol, const ModuleID& moduleID,
	    const std::string& moduleFileName, ExportSyntax syntax, SourceFile* file,
	    checkerLease* lease);
	// tryResolveSymbol — extract.go:366
	Symbol* tryResolveSymbol(Symbol* symbol, ExportSyntax syntax,
	                         checkerLease* lease);
};

// exportExtractor — extract.go:29
struct exportExtractor {
	std::unique_ptr<symbolExtractor> extractor;
	module::DefaultResolver* moduleResolver = nullptr;

	// Stats — extract.go:39
	extractorStats* Stats();
	// extractFromFile — extract.go:106
	std::vector<std::shared_ptr<Export>> extractFromFile(SourceFile* file);
	// extractFromModule — extract.go:137
	std::vector<std::shared_ptr<Export>> extractFromModule(SourceFile* file);
	// extractFromModuleDeclaration — extract.go:173
	void extractFromModuleDeclaration(
	    ModuleDeclaration* decl, SourceFile* file, const ModuleID& moduleID,
	    const std::string& moduleFileName,
	    std::vector<std::shared_ptr<Export>>* exports);
};

// newSymbolExtractor — extract.go:60
std::unique_ptr<symbolExtractor> newSymbolExtractor(
    const std::string& packageName, checker::Checker* ch,
    const std::function<tspath::Path(const std::string&)>& toPath,
    const std::function<std::string(const std::string&)>& realpath);

// isNonPatternAmbientModuleDeclaration — extract.go:128
bool isNonPatternAmbientModuleDeclaration(SourceFile* file,
                                          ModuleDeclaration* decl);

// shouldIgnoreSymbol — extract.go:410
bool shouldIgnoreSymbol(Symbol* symbol);

// getSyntax — extract.go:417
ExportSyntax getSyntax(Symbol* symbol);

// isUnusableName — extract.go:448
bool isUnusableName(const std::string& name);

// fileNameForDefaultExportName — extract.go:461
std::string fileNameForDefaultExportName(Symbol* targetSymbol,
                                         const std::string& moduleFileName,
                                         const ModuleID& moduleID);

// === registry.go types (declared before View which references them) ===

// ProjectID — registry.go:32. Go interface { String() string }; keyed by
// pointer identity in C++.
struct ProjectID {
	virtual ~ProjectID() = default;
	virtual std::string String() const = 0;
};

// InternProjectID — Go maps key ProjectID by interface (value) equality;
// canonical interning gives C++ pointer-keyed maps the same equality
// domain, so every adapter site must go through here.
ProjectID* InternProjectID(std::string id);

// newProgramStructure — registry.go:86
using newProgramStructure = int;
inline constexpr newProgramStructure newProgramStructureFalse = 0;
inline constexpr newProgramStructure newProgramStructureSameFileNames = 1;
inline constexpr newProgramStructure newProgramStructureDifferentFileNames = 2;

// bucketBuildPreferences — registry.go:98
struct bucketBuildPreferences {
	std::vector<std::string> fileExcludePatterns;
	Tristate autoImportEntrypointDirectorySearch = Tristate::Unknown;

	// Equal — registry.go:110
	bool Equal(const bucketBuildPreferences& other) const;
	// Clone — registry.go:115
	bucketBuildPreferences Clone() const;
};

// bucketBuildPreferencesFromUserPreferences — registry.go:103
bucketBuildPreferences bucketBuildPreferencesFromUserPreferences(
    const lsutil::UserPreferences& prefs);

// BucketState — registry.go:129
struct BucketState {
	// dirtyFile — see Go comment: not authoritative when multipleFilesDirty.
	tspath::Path dirtyFile;
	bool multipleFilesDirty = false;
	newProgramStructure newProgramStructure = newProgramStructureFalse;
	bucketBuildPreferences buildPreferences;
	// shared_ptr: Go *Set fields — plain struct copies share them (like
	// BucketStats.State), while Clone() deep-clones.
	std::shared_ptr<collections::Set<std::string>> dirtyPackages;
	std::shared_ptr<collections::Set<std::string>> recursiveSearchPackages;

	// Clone — registry.go:156
	BucketState Clone() const;
	// Dirty — registry.go:163
	bool Dirty() const;
	// DirtyFile — registry.go:167
	tspath::Path DirtyFile() const;
	// DirtyPackages — registry.go:174
	collections::Set<std::string>* DirtyPackages() const;
	// RecursiveSearchPackages — registry.go:181
	collections::Set<std::string>* RecursiveSearchPackages() const;
	// possiblyNeedsRebuildForFile — registry.go:185
	bool possiblyNeedsRebuildForFile(const tspath::Path& file,
	                                 const lsutil::UserPreferences& preferences) const;
	// hasDirtyFileBesides — registry.go:192
	bool hasDirtyFileBesides(const tspath::Path& file) const;
};

// recursiveSearchSubset — registry.go:200
bool recursiveSearchSubset(collections::Set<std::string>* target,
                           collections::Set<std::string>* current);

// RegistryBucket — registry.go:212
struct RegistryBucket {
	BucketState state;
	std::unordered_map<tspath::Path, std::string> Paths;
	std::unordered_map<std::string, std::unordered_map<tspath::Path, std::string>>
	    PackageFiles;
	// shared_ptr: Clone() shares these pointers like Go's shallow copy.
	std::shared_ptr<collections::Set<std::string>> ResolvedPackageNames;
	std::shared_ptr<collections::Set<std::string>> DependencyNames;
	std::unordered_map<std::string, std::vector<std::string>> AmbientModuleNames;
	std::unique_ptr<Index> index;

	// Clone — registry.go:272
	RegistryBucket* Clone() const;
	// markProjectFileDirty — registry.go:287
	void markProjectFileDirty(const tspath::Path& file);
	// markNodeModulesDirty — registry.go:299
	void markNodeModulesDirty(const std::string& packageName);
};

// newRegistryBucket — registry.go:263
RegistryBucket* newRegistryBucket();

// directory — registry.go:314
struct directory {
	std::string name;
	std::shared_ptr<packagejson::InfoCacheEntry> packageJson;
	bool hasNodeModules = false;

	// Clone — registry.go:320
	directory* Clone() const;
};

// Registry — registry.go:328
struct Registry {
	std::function<tspath::Path(const std::string&)> toPath;
	lsutil::UserPreferences userPreferences;

	std::unordered_map<tspath::Path, directory*> directories;
	std::unordered_map<tspath::Path, RegistryBucket*> nodeModules;
	std::unordered_map<ProjectID*, RegistryBucket*> projects;
	int uniquePackageCount = 0;
	std::unordered_map<tspath::Path,
	                   std::vector<std::shared_ptr<module::ResolvedEntrypoint>>>
	    entrypoints;
	// shared across clones like Go's map of *SyncMap references.
	std::unordered_map<
	    tspath::Path,
	    std::shared_ptr<collections::SyncMap<tspath::Path, std::string>>>
	    specifierCache;

	~Registry();
	// IsPreparedForImportingFile — registry.go:354
	bool IsPreparedForImportingFile(const std::string& fileName,
	                                ProjectID* projectID,
	                                const lsutil::UserPreferences& preferences);
	// NodeModulesDirectories — registry.go:383
	std::unordered_map<tspath::Path, std::string> NodeModulesDirectories();
	// Clone — registry.go:393
	std::pair<std::unique_ptr<Registry>, gostd::Error> Clone(
	    gostd::Context ctx, struct RegistryChange& change,
	    struct RegistryCloneHost* host, logging::LogTree* logger);
	// GetCacheStats — registry.go:432
	struct CacheStats* GetCacheStats();
};

// NewRegistry — registry.go:346
std::unique_ptr<Registry> NewRegistry(
    std::function<tspath::Path(const std::string&)> toPath,
    lsutil::UserPreferences preferences);

// BucketStats — registry.go:417
struct BucketStats {
	std::string Name;
	int ExportCount = 0;
	int FileCount = 0;
	BucketState State;
	collections::Set<std::string>* DependencyNames = nullptr;
	collections::Set<std::string>* PackageNames = nullptr;
};

// CacheStats — registry.go:426
struct CacheStats {
	std::vector<BucketStats> ProjectBuckets;
	std::vector<BucketStats> NodeModulesBuckets;
	int UniquePackageCount = 0;
};

// RegistryChange — registry.go:487
struct RegistryChange {
	tspath::Path RequestedFile;
	std::unordered_map<tspath::Path, std::string> OpenFiles;
	collections::Set<lsp::lsproto::DocumentUri> Changed;
	collections::Set<lsp::lsproto::DocumentUri> Created;
	collections::Set<lsp::lsproto::DocumentUri> Deleted;
	std::unordered_map<ProjectID*, bool> RebuiltPrograms;
	lsutil::UserPreferences* UserPreferences = nullptr;
};

// RegistryCloneHost — registry.go:500
struct RegistryCloneHost : module::ResolutionHost {
	virtual std::shared_ptr<vfs::FS> FS() = 0;
	virtual std::pair<ProjectID*, compiler::SimpleProgram*> GetDefaultProject(
	    const tspath::Path& path) = 0;
	virtual compiler::SimpleProgram* GetProgramForProject(
	    ProjectID* projectID) = 0;
	virtual std::shared_ptr<packagejson::InfoCacheEntry> GetPackageJson(
	    const std::string& fileName) = 0;
	virtual SourceFile* GetSourceFile(const std::string& fileName,
	                                  const tspath::Path& path) = 0;
	virtual void Dispose() = 0;
};

// failedAmbientModuleLookupSource — registry.go:1210. The Go struct has a
// sync.Mutex; we extract sequentially so it is unused but kept for parity.
struct failedAmbientModuleLookupSource {
	std::mutex mu;
	std::string fileName;
	std::string packageName;
};

// bucketBuildResult — registry.go:1216
struct bucketBuildResult {
	std::function<void(RegistryBucket*)> replaceBucket;
	tspath::Path resolutionPath;
	gostd::Error err;

	RegistryBucket* bucket = nullptr;
	std::unordered_map<tspath::Path,
	                   std::vector<std::shared_ptr<module::ResolvedEntrypoint>>>
	    entrypoints;
	std::vector<tspath::Path> removedEntrypointPaths;
	// nil-checked like Go: absent unless the build produced failures.
	std::unique_ptr<collections::SyncMap<
	    tspath::Path,
	    std::shared_ptr<failedAmbientModuleLookupSource>>>
	    possibleFailedAmbientModuleLookupSources;
	std::unique_ptr<collections::SyncSet<std::string>>
	    possibleFailedAmbientModuleLookupTargets;
};

// discoveredPackage — registry.go:1356
struct discoveredPackage {
	std::string packageName;
	std::shared_ptr<packagejson::InfoCacheEntry> packageJson;
	std::string realpath;
	std::shared_ptr<packagejson::InfoCacheEntry> typesPackageJson;
	std::string typesRealpath;
	tspath::Path dirPath;
	bool isLocal = false;
};

// perPackageExtractionResult — registry.go:1369
struct perPackageExtractionResult {
	std::unordered_map<tspath::Path, std::string> packageFiles;
	std::vector<std::shared_ptr<module::ResolvedEntrypoint>> entrypoints;
	std::unordered_map<tspath::Path, std::vector<std::shared_ptr<Export>>>
	    exports;
	std::unordered_map<std::string, std::vector<std::string>> ambientModules;
	int statsExports = 0;
	int statsUsedChecker = 0;
	int skippedEntrypoints = 0;
	bool isSymlinked = false;
	std::unordered_map<tspath::Path,
	                   std::shared_ptr<failedAmbientModuleLookupSource>>
	    failedAmbientModuleLookupSources;
	std::unique_ptr<collections::Set<std::string>>
	    failedAmbientModuleLookupTargets;
};

// packageExtractionResult — registry.go:1383
struct packageExtractionResult {
	std::unordered_map<tspath::Path, std::vector<std::shared_ptr<Export>>>
	    exports;
	std::unordered_map<std::string, std::unordered_map<tspath::Path, std::string>>
	    packageFiles;
	std::unordered_map<std::string, std::vector<std::string>> ambientModuleNames;
	std::vector<std::vector<std::shared_ptr<module::ResolvedEntrypoint>>>
	    entrypoints;
	std::unique_ptr<collections::Set<std::string>> workspacePackages;
	collections::SyncMap<tspath::Path,
	                     std::shared_ptr<failedAmbientModuleLookupSource>>
	    possibleFailedAmbientModuleLookupSources;
	collections::SyncSet<std::string> possibleFailedAmbientModuleLookupTargets;
	extractorStats stats;
	int skippedEntrypointsCount = 0;
};

// === view.go ===

struct Fix;

// QueryKind — view.go:73
using QueryKind = int;
inline constexpr QueryKind QueryKindWordPrefix = 0;
inline constexpr QueryKind QueryKindExactMatch = 1;
inline constexpr QueryKind QueryKindCaseInsensitiveMatch = 2;

// existingImport — fix.go:840 (declared early: View holds a MultiMap of them)
struct existingImport {
	Node* node = nullptr;
	std::string moduleSpecifier;
	int index = 0;
};

// View — view.go:21
struct View {
	Registry* registry;
	SourceFile* importingFile;
	tspath::Path importingFilePath;
	compiler::SimpleProgram* program;
	checker::Checker* checker;
	modulespecifiers::UserPreferences preferences;
	ProjectID* projectID;

	std::vector<modulespecifiers::ModuleSpecifierEnding> allowedEndings;
	bool allowedEndingsComputed = false;
	std::unique_ptr<collections::Set<std::string>> conditions;
	Tristate shouldUseUriStyleNodeCoreModules = Tristate::Unknown;
	std::unique_ptr<collections::MultiMap<ModuleID, existingImport>>
	    existingImports;
	std::optional<bool> shouldUseRequireForFixes;
	// ownedMerges — storage backing the merged Export records that
	// GetCompletions creates (Go GC-owned; view.go:217 slices.Replace).
	std::vector<std::shared_ptr<Export>> ownedMerges;

	// getAllowedEndings — view.go:58
	const std::vector<modulespecifiers::ModuleSpecifierEnding>& getAllowedEndings();
	// Search — view.go:81
	std::vector<Export*> Search(const std::string& query, QueryKind kind);
	// SearchByExportID — view.go:98
	std::vector<Export*> SearchByExportID(const ExportID& id);
	// search — view.go:108
	std::vector<Export*> search(
	    const std::function<std::vector<Export*>(RegistryBucket*)>& searchFn);
	// GetCompletions — view.go:180
	std::vector<std::unique_ptr<struct FixAndExport>> GetCompletions(
	    const std::string& prefix, const lsp::lsproto::Position& position,
	    bool forJSX, bool isTypeOnlyLocation);
	// GetModuleSpecifier — specifiers.go:9
	std::pair<std::string, modulespecifiers::ResultKind> GetModuleSpecifier(
	    Export* e, const modulespecifiers::UserPreferences& userPreferences);
	// GetFixes — fix.go:554
	std::vector<std::unique_ptr<Fix>> GetFixes(
	    Export* e, bool forJSX, bool isValidTypeOnlyUseSite,
	    const lsp::lsproto::Position* usagePosition);
	// tryUseExistingNamespaceImport — fix.go:636
	std::unique_ptr<Fix> tryUseExistingNamespaceImport(
	    Export* e, const lsp::lsproto::Position* usagePosition);
	// tryAddToExistingImport — fix.go:690
	std::unique_ptr<Fix> tryAddToExistingImport(
	    Export* e, bool isValidTypeOnlyUseSite);
	// getExistingImports — fix.go:846
	collections::MultiMap<ModuleID, existingImport>* getExistingImports();
	// shouldUseRequire — fix.go:875
	bool shouldUseRequire();
	// computeShouldUseRequire — fix.go:947
	bool computeShouldUseRequire();
	// CompareFixesForSorting — fix.go:1005
	int CompareFixesForSorting(Fix* a, Fix* b);
	// CompareFixesForRanking — fix.go:1015
	int CompareFixesForRanking(Fix* a, Fix* b);
	// compareModuleSpecifiersForRanking — fix.go:1026
	int compareModuleSpecifiersForRanking(Fix* a, Fix* b);
	// compareModuleSpecifiersForSorting — fix.go:1049
	int compareModuleSpecifiersForSorting(Fix* a, Fix* b);
	// compareNodeCoreModuleSpecifiers — fix.go:1070
	int compareNodeCoreModuleSpecifiers(const std::string& a, const std::string& b,
	                                    SourceFile* importingFile,
	                                    compiler::SimpleProgram* program);
};

// NewView — view.go:37
std::unique_ptr<View> NewView(Registry* registry, SourceFile* importingFile,
                              ProjectID* projectID,
                              compiler::SimpleProgram* program,
                              checker::Checker* typeChecker,
                              modulespecifiers::UserPreferences preferences);

// FixAndExport — view.go:175
struct FixAndExport {
	std::unique_ptr<Fix> Fix;
	Export* Export;
};

// === fix.go ===

// newImportBinding — fix.go:30
struct newImportBinding {
	lsp::lsproto::ImportKind kind = lsp::lsproto::ImportKindNamed;
	std::string propertyName;
	std::string name;
	lsp::lsproto::AddAsTypeOnly addAsTypeOnly = lsp::lsproto::AddAsTypeOnlyNotAllowed;
};

// Fix — fix.go:37 (Go: embeds *lsproto.AutoImportFix)
struct Fix {
	// Go embeds *lsproto.AutoImportFix (pointer embed — promoted field name
	// stays AutoImportFix).
	lsp::lsproto::AutoImportFix* AutoImportFix = nullptr;

	modulespecifiers::ResultKind ModuleSpecifierKind =
	    modulespecifiers::ResultKind::None;
	bool IsReExport = false;
	std::string ModuleFileName;
	Node* TypeOnlyAliasDeclaration = nullptr;

	// Edits — fix.go:56. Returns (edits, description, safe).
	std::tuple<lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>>,
	           std::string, bool>
	Edits(
	    gostd::Context ctx, SourceFile* file, const CompilerOptions* compilerOptions,
	    const lsutil::FormatCodeSettings& formatOptions,
	    lsconv::Converters* converters,
	    const lsutil::UserPreferences& preferences);
};

// addToExistingImportFix — fix.go:46
struct addToExistingImportFix {
	Node* importClauseOrBindingPattern = nullptr;
	// One of `defaultImport` or `namedImports` will be present
	std::unique_ptr<newImportBinding> defaultImport;
	std::unique_ptr<newImportBinding> namedImport;
};

// fileEdits — fix.go:133
std::pair<lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>>, bool>
fileEdits(
    change::Tracker* tracker, SourceFile* file);

// addImportType — fix.go:138
std::string addImportType(Fix* f, SourceFile* file,
                          const lsutil::UserPreferences& preferences,
                          change::Tracker* tracker, locale::Locale loc);

// addNamespaceQualifier — fix.go:152
std::string addNamespaceQualifier(Fix* f, change::Tracker* tracker,
                                  SourceFile* file, locale::Locale loc);

// getAddToExistingImportFix — fix.go:161
std::unique_ptr<addToExistingImportFix> getAddToExistingImportFix(
    SourceFile* file, Fix* fix);

// addToExistingImport — fix.go:198
void addToExistingImport(change::Tracker* ct, SourceFile* file,
                         Node* importClauseOrBindingPattern,
                         newImportBinding* defaultImport,
                         const std::vector<newImportBinding*>& namedImports,
                         const lsutil::UserPreferences& preferences);

// getTypeKeywordOfTypeOnlyImport — fix.go:321
Node* getTypeKeywordOfTypeOnlyImport(Node* importClause, SourceFile* sourceFile);

// addElementToBindingPattern — fix.go:331
void addElementToBindingPattern(change::Tracker* ct, SourceFile* file,
                                Node* bindingPattern, const std::string& name,
                                const std::string& propertyName);

// getNewImports — fix.go:346. Returns []AnyImportSyntax as Node*.
std::vector<Node*> getNewImports(
    change::Tracker* ct, const std::string& moduleSpecifier,
    lsutil::QuotePreference quotePreference, newImportBinding* defaultImport,
    const std::vector<newImportBinding*>& namedImports,
    newImportBinding* namespaceLikeImport, const CompilerOptions* compilerOptions,
    const lsutil::UserPreferences& preferences);

// getNewRequires — fix.go:415
std::vector<Node*> getNewRequires(
    change::Tracker* changeTracker, const std::string& moduleSpecifier,
    lsutil::QuotePreference quotePreference, newImportBinding* defaultImport,
    const std::vector<newImportBinding*>& namedImports,
    newImportBinding* namespaceLikeImport, const CompilerOptions* compilerOptions);

// createConstEqualsRequireDeclaration — fix.go:480
Node* createConstEqualsRequireDeclaration(change::Tracker* changeTracker,
                                          Node* name, Node* quotedModuleSpecifier);

// insertImports — fix.go:503
void insertImports(change::Tracker* ct, SourceFile* sourceFile,
                   const std::vector<Node*>& imports, bool blankLineBetween,
                   const lsutil::UserPreferences& preferences);

// makeImport — fix.go:542
Node* makeImport(change::Tracker* ct, Node* defaultImport,
                 const std::vector<Node*>& namedImports, Node* moduleSpecifier,
                 bool isTypeOnly);

// getAddAsTypeOnly — fix.go:623
lsp::lsproto::AddAsTypeOnly getAddAsTypeOnly(bool isValidTypeOnlyUseSite, Export* e,
                                        const CompilerOptions* compilerOptions);

// getNamespaceLikeImportText — fix.go:669
std::string getNamespaceLikeImportText(Node* declaration);

// GetImportKindForImportStatement — fix.go:796
lsp::lsproto::ImportKind GetImportKindForImportStatement(
    SourceFile* importingFile, Export* e, compiler::SimpleProgram* program);

// getImportKind — fix.go:800
lsp::lsproto::ImportKind getImportKind(SourceFile* importingFile, Export* e,
                                  compiler::SimpleProgram* program,
                                  bool forceImportKeyword);

// fileSyntaxKind — fix.go:885
using fileSyntaxKind = int;
inline constexpr fileSyntaxKind fileSyntaxKindAmbiguous = 0;
inline constexpr fileSyntaxKind fileSyntaxKindESM = 1;
inline constexpr fileSyntaxKind fileSyntaxKindCJS = 2;

// detectSyntax — fix.go:897
fileSyntaxKind detectSyntax(SourceFile* file, const CompilerOptions* options);

// detectSyntaxIndicators — fix.go:913
std::pair<bool, bool> detectSyntaxIndicators(SourceFile* file,
                                             const CompilerOptions* options);

// needsTypeOnly — fix.go:993
bool needsTypeOnly(lsp::lsproto::AddAsTypeOnly addAsTypeOnly);

// shouldUseTypeOnly — fix.go:997
bool shouldUseTypeOnly(lsp::lsproto::AddAsTypeOnly addAsTypeOnly,
                       const lsutil::UserPreferences& preferences);

// compareFixKinds — fix.go:1022
int compareFixKinds(lsp::lsproto::AutoImportFixKind a, lsp::lsproto::AutoImportFixKind b);

// isFixPossiblyReExportingImportingFile — fix.go:1094
bool isFixPossiblyReExportingImportingFile(Fix* fix,
                                           const std::string& importingFileName);

// isIndexFileName — fix.go:1102
bool isIndexFileName(const std::string& fileName);

// promoteFromTypeOnly — fix.go:1115
Node* promoteFromTypeOnly(
    change::Tracker* changes, Node* aliasDeclaration,
    const CompilerOptions* compilerOptions, SourceFile* sourceFile,
    const lsutil::UserPreferences& preferences);

// promoteImportClause — fix.go:1208
void promoteImportClause(
    change::Tracker* changes, ImportClause* importClause,
    const CompilerOptions* compilerOptions, SourceFile* sourceFile,
    const lsutil::UserPreferences& preferences,
    Tristate convertExistingToTypeOnly, Node* aliasDeclaration);

// deleteTypeKeyword — fix.go:1289
void deleteTypeKeyword(change::Tracker* changes, SourceFile* sourceFile,
                       int startPos);

// getModuleSpecifierText — fix.go:1304
std::string getModuleSpecifierText(Node* promotedDeclaration);

// compareModuleSpecifierRelativity — fix.go:1326
int compareModuleSpecifierRelativity(
    Fix* a, Fix* b, const modulespecifiers::UserPreferences& preferences);

// === import_adder.go ===

// ImportAdder — import_adder.go:24
struct ImportAdder {
	virtual ~ImportAdder() = default;
	virtual bool HasFixes() = 0;
	virtual void AddImportFromExportedSymbol(Symbol* symbol,
	                                         bool isValidTypeOnlyUseSite) = 0;
	virtual void AddImportFix(std::unique_ptr<Fix> fix) = 0;
	virtual lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>>
	Edits() = 0;
};

// addToExistingState — import_adder.go:32
struct addToExistingState {
	Node* importClauseOrBindingPattern = nullptr;
	std::unique_ptr<newImportBinding> defaultImport;
	std::unordered_map<std::string, std::unique_ptr<newImportBinding>>
	    namedImports;
};

// importsCollection — import_adder.go:39
struct importsCollection {
	std::unique_ptr<newImportBinding> defaultImport;
	std::unordered_map<std::string, std::unique_ptr<newImportBinding>>
	    namedImports;
	std::unique_ptr<newImportBinding> namespaceLikeImport;
	bool useRequire = false;
};

// newImportsKey — import_adder.go:46
std::string newImportsKey(const std::string& moduleSpecifier,
                          bool topLevelTypeOnly);

// importAdder — import_adder.go:53
struct importAdder : ImportAdder {
	// Context
	gostd::Context ctx;
	checker::Checker* checker;
	View* view;
	lsutil::FormatCodeSettings formatOptions;
	lsconv::Converters* converters;
	lsutil::UserPreferences preferences;

	// State
	std::vector<Fix*> addToNamespace; // owned via ownedFixes
	std::vector<Fix*> importType;     // owned via ownedFixes
	std::unordered_map<Node*, std::unique_ptr<addToExistingState>> addToExisting;
	std::unordered_map<std::string, std::unique_ptr<importsCollection>> newImports;
	std::deque<std::unique_ptr<Fix>> ownedFixes;

	// HasFixes — import_adder.go:94
	bool HasFixes() override;
	// AddImportFromExportedSymbol — import_adder.go:102
	void AddImportFromExportedSymbol(Symbol* exportedSymbol,
	                                 bool isValidTypeOnlyUseSite) override;
	// Edits — import_adder.go:118
	lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>> Edits()
	    override;
	// AddImportFix — import_adder.go:189
	void AddImportFix(std::unique_ptr<Fix> fix) override;
	// getNewImportEntry — import_adder.go:337
	importsCollection* getNewImportEntry(const std::string& moduleSpecifier,
	                                     lsp::lsproto::ImportKind importKind,
	                                     bool useRequire,
	                                     lsp::lsproto::AddAsTypeOnly addAsTypeOnly);
	// getAllExportsForSymbol — import_adder.go:375
	std::vector<Export*> getAllExportsForSymbol(Symbol* symbol);
	// getImportFixForSymbol — import_adder.go:491
	std::unique_ptr<Fix> getImportFixForSymbol(
	    View* view, const std::vector<Export*>& exports,
	    bool isValidTypeOnlyUseSite);
};

// NewImportAdder — import_adder.go:70
std::unique_ptr<importAdder> NewImportAdder(
    gostd::Context ctx, compiler::SimpleProgram* program, checker::Checker* ch,
    SourceFile* file, View* view, const lsutil::FormatCodeSettings& formatOptions,
    lsconv::Converters* converters, const lsutil::UserPreferences& preferences);

// sortedNamedImports — import_adder.go:178
std::vector<newImportBinding*> sortedNamedImports(
    const std::unordered_map<std::string, std::unique_ptr<newImportBinding>>& m);

// reduceAddAsTypeOnlyValues — import_adder.go:330
lsp::lsproto::AddAsTypeOnly reduceAddAsTypeOnlyValues(lsp::lsproto::AddAsTypeOnly prevValue,
                                               lsp::lsproto::AddAsTypeOnly newValue);

// TypeToAutoImportableTypeNode — import_adder.go:382
Node* TypeToAutoImportableTypeNode(checker::Checker* c, ImportAdder* adder,
                                   checker::Type* t, Node* contextNode);

// TypeNodeToAutoImportableTypeNode — import_adder.go:398
Node* TypeNodeToAutoImportableTypeNode(
    Node* typeNode, ImportAdder* adder,
    std::unordered_map<Node*, Symbol*>* idToSymbol);

// importSymbols — import_adder.go:415
void importSymbols(ImportAdder* adder, const std::vector<Symbol*>& symbols);

// TryGetAutoImportableReferenceFromTypeNode — import_adder.go:427
std::pair<Node*, std::vector<Symbol*>> TryGetAutoImportableReferenceFromTypeNode(
    Node* importTypeNode, std::unordered_map<Node*, Symbol*>* idToSymbol);

// getNameForExportedSymbol — import_adder.go:465
std::string getNameForExportedSymbol(Symbol* symbol, bool preferCapitalized);

// replaceFirstIdentifierOfEntityName — import_adder.go:481
Node* replaceFirstIdentifierOfEntityName(NodeFactory* factory, Node* name,
                                         Node* newIdentifier);

// === util.go ===

// tryGetModuleIDAndFileNameOfModuleSymbol — util.go:24
std::optional<std::pair<ModuleID, std::string>>
tryGetModuleIDAndFileNameOfModuleSymbol(Symbol* symbol);

// getModuleIDAndFileNameOfModuleSymbol — util.go:41
std::pair<ModuleID, std::string> getModuleIDAndFileNameOfModuleSymbol(
    Symbol* symbol);

// wordIndices — util.go:68
std::vector<int> wordIndices(const std::string& s);

// getPackageNamesInNodeModules — util.go:88
std::unique_ptr<collections::Set<std::string>> getPackageNamesInNodeModules(
    const std::string& nodeModulesDir, vfs::FS* fs);

// getDefaultLikeExportNameFromDeclaration — util.go:118
std::string getDefaultLikeExportNameFromDeclaration(Symbol* symbol);

// getResolvedPackageNames — util.go:145
std::unique_ptr<collections::Set<std::string>> getResolvedPackageNames(
    gostd::Context ctx, compiler::SimpleProgram* program);

// addProjectReferenceOutputMappings — util.go:183
void addProjectReferenceOutputMappings(
    compiler::SimpleProgram* program,
    std::unordered_map<tspath::Path, std::string>& result);

// checkerPool — util.go:199 createCheckerPool
struct checkerPool {
	checker::Program* program;
	int32_t maxSize;
	std::mutex mu;
	std::condition_variable cv;
	std::deque<std::shared_ptr<checker::Checker>> pool;
	std::atomic<int32_t> created{0};
	bool closed = false;

	explicit checkerPool(checker::Program* program);
	// getChecker — returns (checker, done); call done() to return it.
	std::pair<checker::Checker*, std::function<void()>> getChecker();
	void closePool();
	int32_t getCreatedCount() const { return created.load(); }
};

// addPackageJsonDependencies — util.go:234
void addPackageJsonDependencies(const packagejson::PackageJson& contents,
                                collections::Set<std::string>* deps);

// getPackageRealpathFuncs — util.go:253
std::pair<std::function<std::string(const std::string&)>,
          std::function<std::string(const std::string&)>>
getPackageRealpathFuncs(vfs::FS* fs, const std::string& packageDir);

// resolutionHost — util.go:302
struct resolutionHost : module::ResolutionHost {
	std::shared_ptr<vfs::FS> fs;
	std::string currentDirectory;

	resolutionHost(std::shared_ptr<vfs::FS> fs,
	               std::string currentDirectory)
	    : fs(std::move(fs)), currentDirectory(std::move(currentDirectory)) {}

	bool FileExists(std::string_view path) override;
	bool DirectoryExists(std::string_view path) override;
	std::optional<std::string> ReadFile(std::string_view path) override;
	std::string Realpath(std::string_view path) override;
	std::string GetCurrentDirectory() override;
	bool UseCaseSensitiveFileNames() override;
	AccessibleEntries GetAccessibleEntries(std::string_view path) override;
};

// getModuleResolver — util.go:317
module::DefaultResolver* getModuleResolver(
    RegistryCloneHost* host,
    const std::function<std::string(const std::string&)>& realpath,
    module::ResolverOptions opts);

// === aliasresolver.go ===

// pathAndFileName — aliasresolver.go:16
struct pathAndFileName {
	tspath::Path path;
	std::string fileName;
};

// aliasResolver — aliasresolver.go:21. Implements checker::Program.
struct aliasResolver : checker::Program {
	std::function<tspath::Path(const std::string&)> toPath;
	RegistryCloneHost* host;
	module::DefaultResolver* moduleResolver;

	std::vector<SourceFile*> rootFiles;
	// symlinks maps from realpath to symlinked path and file name
	std::unordered_map<tspath::Path, pathAndFileName> symlinks;
	std::function<void(SourceFile*, const std::string&)>
	    onFailedAmbientModuleLookup;
	collections::SyncMap<
	    tspath::Path,
	    std::shared_ptr<collections::SyncMap<
	        module::ModeAwareCacheKey,
	        std::shared_ptr<module::ResolvedModule>,
	        module::ModeAwareCacheKeyHash>>>
	    resolvedModules;

	// BindSourceFiles — aliasresolver.go:53
	void BindSourceFiles() override;
	// SourceFiles — aliasresolver.go:58
	std::vector<SourceFile*> SourceFiles() override;
	// Options — aliasresolver.go:63
	const CompilerOptions* Options() override;
	// GetCurrentDirectory — aliasresolver.go:70
	std::string GetCurrentDirectory() override;
	// UseCaseSensitiveFileNames — aliasresolver.go:75
	bool UseCaseSensitiveFileNames() override;
	// GetSourceFile — aliasresolver.go:80
	SourceFile* GetSourceFile(const std::string& fileName) override;
	// GetDefaultResolutionModeForFile — aliasresolver.go:91
	ResolutionMode GetDefaultResolutionModeForFile(SourceFile* file) override;
	// GetEmitModuleFormatOfFile — aliasresolver.go:96
	ModuleKind GetEmitModuleFormatOfFile(SourceFile* sourceFile) override;
	// GetEmitSyntaxForUsageLocation — aliasresolver.go:101
	ResolutionMode GetEmitSyntaxForUsageLocation(SourceFile* sourceFile,
	                                             Node* usageLocation) override;
	// GetModeForUsageLocation — aliasresolver.go:111
	ResolutionMode GetModeForUsageLocation(SourceFile* file,
	                                       Node* location) override;
	// GetImpliedNodeFormatForEmit — aliasresolver.go:106
	ModuleKind GetImpliedNodeFormatForEmit(SourceFile* sourceFile) override;
	// GetResolvedModule — aliasresolver.go:116
	std::optional<checker::ResolvedModule> GetResolvedModule(
	    SourceFile* currentSourceFile, const std::string& moduleReference,
	    ResolutionMode mode) override;
	// GetSourceFileForResolvedModule — aliasresolver.go:130
	SourceFile* GetSourceFileForResolvedModule(const std::string& fileName) override;
	// GetResolvedModules — aliasresolver.go:135
	std::vector<checker::ResolvedModule> GetResolvedModules() override;
	// GetSymlinkCache — aliasresolver.go:143
	symlinks::KnownSymlinks* GetSymlinkCache() override;
	// GetSourceFileMetaData — aliasresolver.go:148
	const SourceFileMetaData& GetSourceFileMetaData(
	    const std::string& path) const override;
	// CommonSourceDirectory — aliasresolver.go:153
	std::string CommonSourceDirectory() override;
	// ContentMapperExtensions — aliasresolver.go:158
	std::vector<std::string> ContentMapperExtensions() override;
	// FileExists — aliasresolver.go:163
	bool FileExists(const std::string& fileName) override;
	// GetGlobalTypingsCacheLocation — aliasresolver.go:168
	std::string GetGlobalTypingsCacheLocation() override;
	// GetImportHelpersImportSpecifier — aliasresolver.go:173
	Node* GetImportHelpersImportSpecifier(const std::string& path) override;
	// GetJSXRuntimeImportSpecifier — aliasresolver.go:178
	std::pair<std::string, Node*> GetJSXRuntimeImportSpecifier(
	    const std::string& path) override;
	// GetNearestAncestorDirectoryWithPackageJson — aliasresolver.go:183
	std::string GetNearestAncestorDirectoryWithPackageJson(
	    const std::string& dirname) override;
	// GetPackageJsonInfo — aliasresolver.go:188
	std::shared_ptr<packagejson::InfoCacheEntry> GetPackageJsonInfo(
	    const std::string& pkgJsonPath) override;
	// GetProjectReferenceFromOutputDts — aliasresolver.go:193
	const checker::SourceOutputAndProjectReference*
	GetProjectReferenceFromOutputDts(const std::string& path) override;
	// GetProjectReferenceFromSource — aliasresolver.go:198
	checker::SourceOutputAndProjectReference* GetProjectReferenceFromSource(
	    const tspath::Path& path) override;
	// GetRedirectForResolution — aliasresolver.go:203
	checker::RedirectInfo* GetRedirectForResolution(SourceFile* file) override;
	// GetRedirectTargets — aliasresolver.go:208
	std::vector<std::string> GetRedirectTargets(const tspath::Path& path) override;
	// GetResolvedModuleFromModuleSpecifier — aliasresolver.go:213
	module::ResolvedModule* GetResolvedModuleFromModuleSpecifier(
	    SourceFile* file, Node* moduleSpecifier) override;
	// GetSourceOfProjectReferenceIfOutputIncluded — aliasresolver.go:218
	std::string GetSourceOfProjectReferenceIfOutputIncluded(
	    SourceFile* file) override;
	// IsSourceFileDefaultLibrary — aliasresolver.go:223
	bool IsSourceFileDefaultLibrary(const std::string& path) const override;
	// IsSourceFromProjectReference — aliasresolver.go:228
	bool IsSourceFromProjectReference(const tspath::Path& path);
	// SourceFileMayBeEmitted — aliasresolver.go:233
	bool SourceFileMayBeEmitted(SourceFile* sourceFile, bool forceDtsEmit) override;
	// GetPackagesMap — aliasresolver.go:237
	const std::unordered_map<std::string, bool>& GetPackagesMap() override;
	// IsSourceFileFromExternalLibrary — checker.Program pure virtual (Go has no
	// such method; unreachable is faithful to panic-free delegation sites)
	bool IsSourceFileFromExternalLibrary(SourceFile* file) const override;
};

// newAliasResolver — aliasresolver.go:33
std::unique_ptr<aliasResolver> newAliasResolver(
    const std::vector<SourceFile*>& rootFiles,
    std::unordered_map<tspath::Path, pathAndFileName> symlinks,
    RegistryCloneHost* host, module::DefaultResolver* moduleResolver,
    std::function<tspath::Path(const std::string&)> toPath,
    std::function<void(SourceFile*, const std::string&)>
        onFailedAmbientModuleLookup);

// === registry.go (builder) ===

// knownRecursiveSearchPackages — registry.go:36
extern const collections::Set<std::string> knownRecursiveSearchPackages;

// registryBuilder — registry.go:510
struct registryBuilder {
	RegistryCloneHost* host;
	Registry* base;

	lsutil::UserPreferences userPreferences;
	std::unique_ptr<dirty::Map<tspath::Path, directory*>> directories;
	std::unique_ptr<dirty::Map<tspath::Path, RegistryBucket*>> nodeModules;
	std::unique_ptr<dirty::Map<ProjectID*, RegistryBucket*>> projects;
	std::unique_ptr<dirty::MapBuilder<
	    tspath::Path,
	    std::shared_ptr<collections::SyncMap<tspath::Path, std::string>>,
	    std::shared_ptr<collections::SyncMap<tspath::Path, std::string>>>>
	    specifierCache;
	module::ResolverOptions resolverOptions;

	int uniquePackageCount = 0;
	std::unique_ptr<dirty::MapBuilder<
	    tspath::Path, std::vector<std::shared_ptr<module::ResolvedEntrypoint>>,
	    std::vector<std::shared_ptr<module::ResolvedEntrypoint>>>>
	    entrypoints;

	// newExportExtractor — extract.go:73
	std::unique_ptr<exportExtractor> newExportExtractor(
	    const std::string& packageName, checker::Checker* ch,
	    module::DefaultResolver* moduleResolver,
	    const std::function<std::string(const std::string&)>& realpath);

	// Build — registry.go:540
	Registry* Build();
	// updateBucketAndDirectoryExistence — registry.go:553
	void updateBucketAndDirectoryExistence(RegistryChange& change,
	                                       logging::LogTree* logger);
	// markBucketsDirty — registry.go:707
	void markBucketsDirty(RegistryChange& change, logging::LogTree* logger);
	// updateIndexes — registry.go:791
	void updateIndexes(gostd::Context ctx, RegistryChange& change,
	                   logging::LogTree* logger);
	// buildProjectBucket — registry.go:1234
	void buildProjectBucket(
	    gostd::Context ctx, bucketBuildResult* result, ProjectID* projectID,
	    std::shared_ptr<collections::Set<std::string>> resolvedPackageNames,
	    logging::LogTree* logger);
	// computeDependenciesForNodeModulesDirectory — registry.go:1321
	std::shared_ptr<collections::Set<std::string>>
	computeDependenciesForNodeModulesDirectory(
	    RegistryChange& change,
	    std::unordered_map<ProjectID*, std::shared_ptr<collections::Set<std::string>>>&
	        allResolvedPackageNames,
	    const std::string& dirName, const tspath::Path& dirPath);
	// discoverBucketPackages — registry.go:1397
	std::vector<std::unique_ptr<discoveredPackage>> discoverBucketPackages(
	    collections::Set<std::string>* packageNames, const std::string& dirName,
	    const tspath::Path& dirPath);
	// extractPackage — registry.go:1444
	std::unique_ptr<perPackageExtractionResult> extractPackage(
	    gostd::Context ctx,
	    const std::shared_ptr<packagejson::InfoCacheEntry>& packageJson,
	    const std::string& packageName,
	    const std::unordered_map<tspath::Path, std::string>&
	        projectReferenceOutputs,
	    vfs::vfsmatch::SpecMatcher* fileExcludePatterns,
	    bool enableDirectorySearch);
	// buildNodeModulesBucket — registry.go:1624
	void buildNodeModulesBucket(
	    gostd::Context ctx, bucketBuildResult* result,
	    std::shared_ptr<collections::Set<std::string>> dependencies,
	    const tspath::Path& dirPath,
	    std::vector<std::unique_ptr<discoveredPackage>>& discovered,
	    collections::Set<std::string>* directoryPackageNames,
	    std::unordered_map<std::string, std::unique_ptr<perPackageExtractionResult>>&
	        extractionCache,
	    collections::Set<std::string>* recursiveSearchPackages,
	    logging::LogTree* logger);
	// updateNodeModulesBucket — registry.go:1713
	void updateNodeModulesBucket(
	    gostd::Context ctx, bucketBuildResult* result, RegistryBucket* existingBucket,
	    collections::Set<std::string>* dirtyPackages,
	    std::vector<std::unique_ptr<discoveredPackage>>& discovered,
	    std::unordered_map<std::string, std::unique_ptr<perPackageExtractionResult>>&
	        extractionCache,
	    collections::Set<std::string>* recursiveSearchPackages,
	    logging::LogTree* logger);
	// getNearestAncestorDirectoryWithPackageJson — registry.go:1832
	directory* getNearestAncestorDirectoryWithPackageJson(
	    const tspath::Path& filePath);
	// resolveAmbientModuleName — registry.go:1841
	std::vector<std::string> resolveAmbientModuleName(
	    const std::string& moduleName, const tspath::Path& fromPath);
};

// newRegistryBuilder — registry.go:525
std::unique_ptr<registryBuilder> newRegistryBuilder(Registry* registry,
                                                    RegistryCloneHost* host);

// hasNewNonNodeModulesFiles — registry.go:1137
bool hasNewNonNodeModulesFiles(compiler::SimpleProgram* program,
                               RegistryBucket* bucket);

// isIgnoredFile — registry.go:1152
bool isIgnoredFile(compiler::SimpleProgram* program, SourceFile* file);

// hasSymlinkToNodeModules — registry.go:1159
bool hasSymlinkToNodeModules(const tspath::Path& filePath,
                             const tspath::Path& projectRootPath,
                             symlinks::KnownSymlinks* symlinkCache);

// installExtractions — registry.go:1575
std::unique_ptr<packageExtractionResult> installExtractions(
    const std::vector<std::unique_ptr<discoveredPackage>>& discovered,
    std::unordered_map<std::string, std::unique_ptr<perPackageExtractionResult>>&
        extractionCache);

}  // namespace tsc::ls::autoimport
