#pragma once

// Port of tsc/internal/compiler — program slice (file loading + Program for
// `tsc --noEmit <files>`). No emit, no build mode, no content mappers, no
// incremental state, no project references.
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/diagnostics_util.h"
#include "internal/checker/checker.h"
#include "internal/core/types.h"
#include "internal/module/resolver.h"
#include "internal/module/types.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

namespace tsc::compiler {

class SimpleProgram;

// --- host.go: CompilerHost ---
// Real-FS host; `bundled:///libs/<name>` reads from <bundledLibsRoot>/<name>
// (tsc/internal/bundled/libs on disk) while keeping the bundled:/// name on
// the SourceFile — matching the Go oracle's bundled.WrapFS naming.
class CompilerHost : public module::ResolutionHost {
public:
	std::string currentDirectory;
	std::string bundledLibsRoot;
	CompilerOptions* compilerOptions{};

	std::string GetCurrentDirectory() override { return currentDirectory; }
	bool UseCaseSensitiveFileNames() override { return true; }

	// host.go: DefaultLibraryPath — bundled.LibPath() "bundled:///libs"
	std::string DefaultLibraryPath() const {
		return "bundled:///libs";
	}

	std::string bundledPath(std::string_view fileName) const;
	bool FileExists(std::string_view fileName) override;
	bool DirectoryExists(std::string_view directory) override;
	std::optional<std::string> ReadFile(std::string_view fileName) override;
	std::string Realpath(std::string_view path) override;
	AccessibleEntries GetAccessibleEntries(std::string_view path) override;

	SourceFile* GetSourceFile(const SourceFileParseOptions& opts,
	                          SourceFileMetaData metaData);
};

// --- program.go: LibFile / redirectsFile ---
struct LibFile {
	std::string name;
	std::string path;
	bool replaced{};
};

// fileloader.go: libResolution
struct libResolution {
	std::string libraryName;
	module::ResolvedModule* resolution{};
	std::vector<module::DiagAndArgs> trace;
};

struct redirectsFile {
	int index{};
	std::string fileName;
	tspath::Path path;
	tspath::Path target;
};

// --- fileInclude.go ---
enum class FileIncludeKind : int32_t {
	Import = 0,
	ReferenceFile = 1,
	TypeReferenceDirective = 2,
	LibReferenceDirective = 3,
	RootFile = 4,
	LibFile = 5,
	AutomaticTypeDirectiveFile = 6,
	ContentMapperSupplemental = 7,
};

// referencedFileData — shared data for the 4 "referenced file" kinds.
struct referencedFileData {
	tspath::Path file;
	int index{};
	// synthetic is a StringLiteralNode not present in file->Imports()
	Node* synthetic{};
};

struct automaticTypeDirectiveFileData {
	std::string typeReference;
	module::PackageId PackageId;
};

// referenceFileLocation — resolved location of a referenced-file reason.
struct referenceFileLocation {
	SourceFile* file{};
	Node* node{};
	const FileReference* ref{};
	module::PackageId PackageId;
	bool isSynthetic{};

	std::string text() const;
	Diagnostic* diagnosticAt(const DiagnosticMessage* message,
	                       std::vector<std::string> args = {}) const;
};

struct FileIncludeReason {
	using DataV =
	    std::variant<std::monostate, int, referencedFileData,
	                 automaticTypeDirectiveFileData, tspath::Path>;
	FileIncludeKind kind{};
	DataV data;

	bool isReferencedFile() const {
		return static_cast<int>(kind) <=
		       static_cast<int>(FileIncludeKind::LibReferenceDirective);
	}
	const referencedFileData* asReferencedFileData() const {
		return std::get_if<referencedFileData>(&data);
	}
	referenceFileLocation getReferencedLocation(SimpleProgram* p) const;
	Diagnostic* toDiagnostic(SimpleProgram* p, bool relativeFileName) const;
	Diagnostic* computeDiagnostic(
	    SimpleProgram* p,
	    const std::function<std::string(std::string_view)>& toFileName) const;
	Diagnostic* computeReferenceFileDiagnostic(
	    SimpleProgram* p,
	    const std::function<std::string(std::string_view)>& toFileName) const;
	Diagnostic* toRelatedInfo(SimpleProgram* p) const;
};

// --- processingDiagnostic.go ---
struct includeExplainingDiagnostic {
	tspath::Path file;
	const FileIncludeReason* diagnosticReason{};
	const DiagnosticMessage* message{};
	std::vector<std::string> args;
};

enum class processingDiagnosticKind : int32_t {
	UnknownReference = 0,
	ExplainingFileInclude = 1,
};

struct processingDiagnostic {
	processingDiagnosticKind kind{};
	// UnknownReference -> FileIncludeReason*; ExplainingFileInclude ->
	// includeExplainingDiagnostic value.
	std::variant<const FileIncludeReason*, includeExplainingDiagnostic> data;

	Diagnostic* toDiagnostic(SimpleProgram* p);
	Diagnostic* createDiagnosticExplainingFile(SimpleProgram* p);
};

// --- includeprocessor.go ---
struct includeProcessor {
	std::unordered_map<tspath::Path,
	                   std::vector<const FileIncludeReason*>>
	    fileIncludeReasons;
	std::vector<processingDiagnostic*> processingDiagnostics;

	// arenas for stable pointers
	std::vector<std::unique_ptr<FileIncludeReason>> reasonArena;
	std::vector<std::unique_ptr<processingDiagnostic>> processingDiagArena;
	std::vector<std::unique_ptr<Diagnostic>> diagArena;

	const FileIncludeReason* newReason(FileIncludeKind kind,
	                                   FileIncludeReason::DataV data) {
		return reasonArena
		    .emplace_back(
		        std::make_unique<FileIncludeReason>(FileIncludeReason{kind,
		                                                            data}))
		    .get();
	}
	processingDiagnostic* newProcessingDiagnostic(
	    processingDiagnosticKind k,
	    std::variant<const FileIncludeReason*, includeExplainingDiagnostic>
	        d) {
		return processingDiagArena
		    .emplace_back(
		        std::make_unique<processingDiagnostic>(
		            processingDiagnostic{k, std::move(d)}))
		    .get();
	}

	DiagnosticsCollection* getDiagnostics(SimpleProgram* p);
	referenceFileLocation getReferenceLocation(const FileIncludeReason* r,
	                                           SimpleProgram* p);
	Diagnostic* getRelatedInfo(const FileIncludeReason* r, SimpleProgram* p);
	void addProcessingDiagnosticsForFileCasing(
	    tspath::Path file, const std::string& existingCasing,
	    const std::string& currentCasing, const FileIncludeReason* reason);
	// includeprocessor.go:59 addProcessingDiagnostic
	void addProcessingDiagnostic(processingDiagnostic* d) {
		processingDiagnostics.push_back(d);
	}
	std::vector<Diagnostic*> explainRedirectAndImpliedFormat(
	    SimpleProgram* p, const tspath::Path& filePath,
	    const std::function<std::string(std::string_view)>& toFileName);

	std::unique_ptr<DiagnosticsCollection> computedDiagnostics_;
	std::unordered_map<const FileIncludeReason*, referenceFileLocation>
	    reasonToReferenceLocation;
	std::unordered_map<const FileIncludeReason*, Diagnostic*>
	    includeReasonToRelatedInfo;
	std::unordered_map<tspath::Path, std::vector<Diagnostic*>>
	    redirectAndFileFormat;
};

// --- filesparser.go: parseTask ---
struct jsxRuntimeImportSpecifier {
	std::string moduleReference;
	Node* specifier{};
};

struct resolvedRef {
	std::string fileName;
	bool increaseDepth{};
	bool elideOnDepth{};
	const FileIncludeReason* includeReason{};
	module::PackageId PackageId;
};

struct parseTask;
struct parseTaskData {
	// map of tasks by file casing
	std::unordered_map<std::string, parseTask*> tasks;
	int lowestDepth = INT32_MAX;
	bool startedSubTasks{};
	module::PackageId PackageId;
};

struct parseTask {
	std::string normalizedFilePath;
	tspath::Path path;
	SourceFile* file{};
	LibFile* libFile{};
	parseTask* redirectedParseTask{};
	std::vector<parseTask*> subTasks;
	bool loaded{};
	bool startedSubTasksTask{}; // parseTask-level flag (not shared data)
	bool isForAutomaticTypeDirective{};
	bool isContentMapperSupplemental{};
	bool failedLookup{};
	const FileIncludeReason* includeReason{};
	module::PackageId PackageId;

	SourceFileMetaData metadata;
	module::ModeAwareCache<module::ResolvedModule*> resolutionsInFile;
	std::vector<module::DiagAndArgs> resolutionsTrace;
	module::ModeAwareCache<module::ResolvedTypeReferenceDirective*>
	    typeResolutionsInFile;
	std::vector<module::DiagAndArgs> typeResolutionsTrace;
	std::vector<processingDiagnostic*> processingDiagnostics;
	Node* importHelpersImportSpecifier{};
	jsxRuntimeImportSpecifier jsxRuntime;
	bool hasJsxRuntime{};
	parseTask* loadedTask{};

	bool increaseDepth{};
	bool elideOnDepth{};

	const std::string& FileName() const { return normalizedFilePath; }
};

struct filesLoader;

// filesParser — DFS file graph walker.
struct filesParser {
	filesLoader* loader{};
	int maxDepth{};
	std::unordered_map<tspath::Path, std::unique_ptr<parseTaskData>>
	    taskDataByPath;
	std::vector<std::unique_ptr<parseTask>> taskArena;
	NodeFactory factory; // synthetic imports only
	// arena for empty ResolvedModule sentinels (Go: &module.ResolvedModule{})
	std::deque<module::ResolvedModule> resolvedModuleArena;

	// task arena helpers
	parseTask* newTask(const std::string& normalizedFilePath) {
		auto* t = taskArena.emplace_back(std::make_unique<parseTask>()).get();
		t->normalizedFilePath = normalizedFilePath;
		return t;
	}

	void start(const std::vector<parseTask*>& tasks, int depth);
	void load(parseTask* t);

	// collected output
	std::vector<SourceFile*> files;
	std::unordered_map<tspath::Path, SourceFile*> filesByPath;
	std::unordered_map<tspath::Path,
	                   module::ModeAwareCache<module::ResolvedModule*>>
	    resolvedModules;
	std::unordered_map<tspath::Path,
	                   module::ModeAwareCache<module::ResolvedTypeReferenceDirective*>>
	    typeResolutionsInFile;
	std::unordered_map<tspath::Path, SourceFileMetaData> sourceFileMetaDatas;
	std::unordered_map<tspath::Path, jsxRuntimeImportSpecifier>
	    jsxRuntimeImportSpecifiers;
	std::unordered_map<tspath::Path, Node*> importHelpersImportSpecifiers;
	std::unordered_set<tspath::Path> sourceFilesFoundSearchingNodeModules;
	std::unordered_map<tspath::Path, LibFile*> libFiles;
	std::vector<SourceFile*> libFileList;
	std::vector<std::string> missingFiles;
	std::unordered_map<tspath::Path, std::vector<std::string>>
	    redirectTargetsMap;
	std::unordered_map<tspath::Path, redirectsFile> redirectFilesByPath;

	void collectFiles(const std::vector<parseTask*>& tasks);
	void addIncludeReason(parseTask* task, const FileIncludeReason* reason);
	// filesparser.go:330 getProcessedFiles — parse + collect + tail
	void getProcessedFiles(const std::vector<parseTask*>& rootTasks);
};

// filesLoader — fileloader.go's fileLoader struct.
struct filesLoader {
	CompilerHost* host{};
	CompilerOptions* compilerOptions{};
	module::DefaultResolver* resolver{};
	filesParser* parser{};
	SimpleProgram* program{};

	std::string defaultLibraryPath;
	bool useCaseSensitiveFileNames{};
	std::vector<std::string> contentMapperExtensions;
	std::vector<std::vector<std::string_view>> supportedExtensions;
	std::vector<std::vector<std::string_view>>
	    supportedExtensionsWithJsonIfResolveJsonModule;
	bool skipModuleResolution{};

	std::vector<parseTask*> rootTasks;
	std::unordered_map<std::string, std::unique_ptr<LibFile>>
	    pathForLibFileCache;
	std::unordered_map<tspath::Path, libResolution>
	    pathForLibFileResolutions;

	tspath::Path toPath(const std::string& file) const {
		return tspath::toPath(file, host->GetCurrentDirectory(),
		                      useCaseSensitiveFileNames);
	}
	tspath::ComparePathsOptions comparePathsOptions() const {
		return {useCaseSensitiveFileNames, host->GetCurrentDirectory()};
	}

	void addRootTask(const std::string& fileName, LibFile* libFile,
	                 const FileIncludeReason* includeReason);
	void addRootFileTask(const std::string& fileName, LibFile* libFile,
	                     const FileIncludeReason* includeReason);
	void addAutomaticTypeDirectiveTasks();
	// fileloader.go:152 processAllProgramFiles — fills program state
	void processAllProgramFiles(const std::vector<std::string>& rootFileNames);
	void sortLibs(std::vector<SourceFile*>& libFiles);
	int getDefaultLibFilePriority(SourceFile* file);
	LibFile* pathForLibFile(const std::string& name);
	Node* createSyntheticImport(const std::string& text, SourceFile* file);

	bool isSupportedExtension(const std::string& canonicalFileName) const;
	std::pair<std::string, processingDiagnostic*> getSourceFileFromReference(
	    const std::string& fileName, const std::string& referenceText,
	    const std::string& containingFile,
	    const FileIncludeReason* includeReason);
	std::pair<resolvedRef, processingDiagnostic*>
	resolveTripleslashPathReference(const std::string& moduleName,
	                                const std::string& containingFile,
	                                int index);
	void resolveTypeReferenceDirectives(parseTask* t);
	void resolveImportsAndModuleAugmentations(parseTask* t);
	void resolveAutomaticTypeDirectives(parseTask* t);
	SourceFileMetaData loadSourceFileMetaData(const std::string& fileName);
	SourceFile* parseSourceFile(parseTask* t);

	includeProcessor* ip{};
};

// === class SimpleProgram — checker.h `Program` + program.go ===
class SimpleProgram : public checker::Program {
public:
	CompilerHost* host{};
	CompilerOptions options;
	bool skipModuleResolution{};

	// Loaded state.
	std::vector<SourceFile*> files;
	std::unordered_map<tspath::Path, SourceFile*> filesByPath;
	std::unordered_map<tspath::Path,
	                   module::ModeAwareCache<module::ResolvedModule*>>
	    resolvedModules;
	std::optional<std::unordered_map<std::string, bool>> packagesMap;
	std::unordered_map<tspath::Path,
	                   module::ModeAwareCache<module::ResolvedTypeReferenceDirective*>>
	    typeResolutionsInFile;
	std::unordered_map<tspath::Path, SourceFileMetaData> sourceFileMetaDatas;
	std::unordered_map<tspath::Path, jsxRuntimeImportSpecifier>
	    jsxRuntimeImportSpecifiers;
	std::unordered_map<tspath::Path, Node*> importHelpersImportSpecifiers;
	std::unordered_set<tspath::Path> sourceFilesFoundSearchingNodeModules;
	std::unordered_map<tspath::Path, LibFile*> libFiles;
	std::vector<std::string> missingFiles;
	std::unordered_map<tspath::Path, std::vector<std::string>>
	    redirectTargetsMap;
	std::unordered_map<tspath::Path, redirectsFile> redirectFilesByPath;
	std::vector<std::string> fileNameList; // ordered root file names

	includeProcessor includeProcessor_;
	std::unique_ptr<module::DefaultResolver> resolver_;
	std::vector<Diagnostic*> programDiagnostics;
	std::vector<Diagnostic*> configFileParsingDiagnostics;

	std::unique_ptr<checker::Checker> checker_;
	bool bindDone_{};
	mutable std::optional<std::string> commonSourceDirectory_;

	SimpleProgram(CompilerHost* host, const CompilerOptions& options,
	              std::vector<std::string> rootFileNames,
	              bool skipModuleResolution = false);

	// --- checker.Program interface ---
	const CompilerOptions* Options() override { return &options; }
	std::vector<SourceFile*> SourceFiles() override { return files; }
	void BindSourceFiles() override;
	bool FileExists(const std::string& fileName) override;
	SourceFile* GetSourceFile(const std::string& fileName) override;
	SourceFile* GetSourceFileForResolvedModule(
	    const std::string& fileName) override;
	ModuleKind GetEmitModuleFormatOfFile(SourceFile* sourceFile) override;
	ResolutionMode GetEmitSyntaxForUsageLocation(SourceFile* sourceFile,
	                                           Node* usageLocation) override;
	ModuleKind GetImpliedNodeFormatForEmit(SourceFile* sourceFile) override;
	bool SourceFileMayBeEmitted(SourceFile* sourceFile,
	                            bool forceDtsEmit) override;
	std::string CommonSourceDirectory() override;
	std::optional<checker::ResolvedModule> GetResolvedModule(
	    SourceFile* file, const std::string& moduleReference,
	    ResolutionMode mode) override;
	std::vector<checker::ResolvedModule> GetResolvedModules() override;
	const std::unordered_map<std::string, bool>& GetPackagesMap() override;
	ResolutionMode GetModeForUsageLocation(SourceFile* file,
	                                       Node* location) override;
	ResolutionMode GetDefaultResolutionModeForFile(
	    SourceFile* file) override;
	std::string GetCurrentDirectory() override;
	bool UseCaseSensitiveFileNames() override;
	// program.go GetJSXRuntimeImportSpecifier — consult
	// jsxRuntimeImportSpecifiers (jsx slice).
	std::pair<std::string, Node*> GetJSXRuntimeImportSpecifier(
	    const std::string& path) override {
		auto it = jsxRuntimeImportSpecifiers.find(path);
		if (it == jsxRuntimeImportSpecifiers.end()) {
			return {"", nullptr};
		}
		return {it->second.moduleReference, it->second.specifier};
	}
	// GetRedirectForResolution / GetProjectReferenceFromSource: base-class
	// nullptr defaults — Go-equivalent (no project references).

	// --- program.go methods beyond the interface ---
	checker::Checker* getChecker(); // lazily created after bind
	std::vector<Diagnostic*> GetConfigFileParsingDiagnostics() {
		return configFileParsingDiagnostics;
	}
	std::vector<Diagnostic*> GetProgramDiagnostics();
	std::vector<Diagnostic*> GetGlobalDiagnostics();
	std::vector<Diagnostic*> GetSyntacticDiagnostics(SourceFile* sourceFile);
	std::vector<Diagnostic*> GetBindDiagnostics(SourceFile* sourceFile);
	std::vector<Diagnostic*> GetSemanticDiagnostics(SourceFile* sourceFile);
	std::vector<Diagnostic*> GetIncludeProcessorDiagnostics(
	    SourceFile* sourceFile);
	std::vector<Diagnostic*> GetDeclarationDiagnostics(SourceFile* sourceFile);
	bool SkipTypeChecking(SourceFile* sourceFile, bool ignoreNoCheck);
	bool canIncludeBindAndCheckDiagnostics(SourceFile* sourceFile);
	bool IsSourceFileDefaultLibrary(const tspath::Path& path) const override;
	bool IsLibFile(SourceFile* file) const;
	bool IsSourceFileFromExternalLibrary(SourceFile* file) const;
	const SourceFileMetaData& GetSourceFileMetaData(
	    const tspath::Path& path) const;
	SourceFile* GetSourceFileByPath(const tspath::Path& path) const {
		auto it = filesByPath.find(path);
		return it != filesByPath.end() ? it->second : nullptr;
	}
	module::ResolvedTypeReferenceDirective*
	GetResolvedTypeReferenceDirective(SourceFile* file,
	                                  const std::string& typeDirectiveName,
	                                  ResolutionMode mode);
	module::ResolvedModule* GetResolvedModuleFromModuleSpecifier(
	    SourceFile* file, Node* moduleSpecifier) override;
	// === slice: modulespecifiers ===
	// program.go GetRedirectTargets — redirectTargetsMap lookup (empty when
	// the path has no package-id redirects).
	std::vector<std::string> GetRedirectTargets(
	    const tspath::Path& path) override {
		auto it = redirectTargetsMap.find(path);
		return it != redirectTargetsMap.end() ? it->second
		                                    : std::vector<std::string>{};
	}
	// program.go GetNearestAncestorDirectoryWithPackageJson /
	// GetPackageJsonInfo — backed by the resolver's package-json scope cache.
	std::string GetNearestAncestorDirectoryWithPackageJson(
	    const std::string& dirname) override;
	std::shared_ptr<packagejson::InfoCacheEntry> GetPackageJsonInfo(
	    const std::string& pkgJsonPath) override;
	// === end slice: modulespecifiers ===
	const std::vector<const FileIncludeReason*>* GetIncludeReasons(
	    const tspath::Path& path) const;
	tspath::Path toPath(const std::string& fileName) const;
	tspath::ComparePathsOptions comparePathsOptions() const;
	void verifyCompilerOptions();
	void verifyProjectReferences();

	module::ResolvedModule* getResolvedModuleByPath(
	    const tspath::Path& path, const std::string& moduleReference,
	    ResolutionMode mode);
	SourceFile* GetLibFileFromReference(const FileReference* ref);
	bool IsSourceFromProjectReference(const tspath::Path& path) const;
	ResolutionMode GetModeForResolutionAtIndex(SourceFile* sourceFile,
	                                           int index);
	bool checkSourceFilesBelongToPath(
	    const std::vector<std::string>& sourceFiles,
	    const std::string& rootDirectory);

	std::vector<Diagnostic*> collectDiagnostics(
	    SourceFile* sourceFile,
	    const std::function<std::vector<Diagnostic*>(SourceFile*)>& collect);
	std::vector<Diagnostic*> collectCheckerDiagnostics(
	    SourceFile* sourceFile,
	    const std::function<std::vector<Diagnostic*>(SourceFile*)>& collect);
	std::vector<Diagnostic*> getSemanticDiagnosticsWithChecker(
	    SourceFile* sourceFile);
	std::vector<Diagnostic*> getBindAndCheckDiagnosticsWithChecker(
	    SourceFile* sourceFile);
	std::pair<std::vector<Diagnostic*>,
	          std::unordered_map<int, CommentDirective>>
	getDiagnosticsWithPrecedingDirectives(
	    SourceFile* sourceFile, std::vector<Diagnostic*> diags);
};

// program.go: GetDiagnosticsOfAnyProgram.
std::vector<Diagnostic*> getDiagnosticsOfAnyProgram(
    SimpleProgram* program, const std::vector<SourceFile*>& files,
    bool skipNoEmitCheckForDtsDiagnostics);

std::vector<Diagnostic*> sortAndDeduplicateDiagnostics(
    std::vector<Diagnostic*> diagnostics);
std::vector<Diagnostic*> filterAndSortDiagnostics(
    std::vector<Diagnostic*> diags);
std::vector<Diagnostic*> filterNoEmitSemanticDiagnostics(
    std::vector<Diagnostic*> diags, const CompilerOptions* options);
std::vector<Diagnostic*> getAdditionalJSSyntacticDiagnostics(
    SourceFile* file, const CompilerOptions* options);
// emitter.go:493
bool sourceFileMayBeEmitted(SourceFile* sourceFile, SimpleProgram* host,
                            bool forceDtsEmit, bool forceJsEmit);

// fileloader.go mode workers (file-level).
ResolutionMode getEmitSyntaxForUsageLocationWorker(
    std::string_view fileName, const SourceFileMetaData& sourceFileMetaData,
    Node* location, const CompilerOptions* compilerOptions);
ResolutionMode getModeForUsageLocation(
    std::string_view fileName, const SourceFileMetaData& sourceFileMetaData,
    Node* location, const CompilerOptions* compilerOptions);
ResolutionMode getDefaultResolutionModeForFile(
    std::string_view fileName, const SourceFileMetaData& sourceFileMetaData,
    const CompilerOptions* compilerOptions);
bool importSyntaxAffectsModuleResolution(const CompilerOptions* options);

}  // namespace tsc::compiler
