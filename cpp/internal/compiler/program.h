#pragma once

// Port of tsc/internal/compiler — program slice (file loading + Program for
// `tsc --noEmit <files>`). No emit, no build mode, no content mappers, no
// incremental state, no project references.
#include <atomic>
#include <deque>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <tuple>
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
#include "internal/vfs/vfs.h"

namespace tsc::sourcemap {
struct RawSourceMap;
}
namespace tsc::tracing {
class Tracing;
}

namespace tsc::compiler {

class SimpleProgram;

// === slice: incremental ===
// program.go:1957 ProgramLike — implemented by SimpleProgram and by
// execute/incremental::Program so HandleNoEmitOptions /
// getDiagnosticsOfAnyProgram can run against either program kind.
// (Go context.Context params are dropped.)
struct EmitResult;
struct EmitOptions;
class ProgramLike {
public:
	virtual ~ProgramLike() = default;
	virtual const CompilerOptions* Options() = 0;
	virtual SourceFile* GetSourceFile(const std::string& path) = 0;
	virtual std::vector<SourceFile*> GetSourceFiles() = 0;
	virtual std::vector<Diagnostic*> GetConfigFileParsingDiagnostics() = 0;
	virtual std::vector<Diagnostic*> GetSyntacticDiagnostics(
	    SourceFile* file) = 0;
	virtual std::vector<Diagnostic*> GetBindDiagnostics(SourceFile* file) = 0;
	virtual std::vector<Diagnostic*> GetProgramDiagnostics() = 0;
	virtual std::vector<Diagnostic*> GetGlobalDiagnostics() = 0;
	virtual std::vector<Diagnostic*> GetSemanticDiagnostics(
	    SourceFile* file) = 0;
	virtual std::vector<Diagnostic*> GetDeclarationDiagnostics(
	    SourceFile* file) = 0;
	virtual std::vector<Diagnostic*> GetSuggestionDiagnostics(
	    SourceFile* file) = 0;
	virtual EmitResult* Emit(EmitOptions* options) = 0;
	virtual std::string CommonSourceDirectory() = 0;
	virtual bool IsSourceFileDefaultLibrary(const tspath::Path& path) const = 0;
	// GetProgram — Go's `Program()` method (renamed: a member named
	// `Program` inside `class Program` would be a constructor in C++).
	virtual SimpleProgram* GetProgram() = 0;
};
// === end slice: incremental ===

// --- host.go: CompilerHost ---
// host.go:35 compilerHost — serves SourceFiles over `fs` (e.g.
// bundled.WrapFS(osvfs.FS())), keeping the bundled:/// name on the
// SourceFile exactly like the Go oracle.
class CompilerHost : public tsoptions::ParseConfigHost {
public:
	std::string currentDirectory;
	std::shared_ptr<vfs::FS> fs;
	// host.go:38 defaultLibraryPath — bundled.LibPath() "bundled:///libs"
	std::string defaultLibraryPath;
	CompilerOptions* compilerOptions{};

	std::string GetCurrentDirectory() override { return currentDirectory; }
	bool UseCaseSensitiveFileNames() override {
		return fs->UseCaseSensitiveFileNames();
	}

	// host.go: DefaultLibraryPath
	std::string DefaultLibraryPath() const { return defaultLibraryPath; }
	bool FileExists(std::string_view fileName) override;
	bool DirectoryExists(std::string_view directory) override;
	std::optional<std::string> ReadFile(std::string_view fileName) override;
	// emitHost.go: WriteFile — Go `Host().FS().WriteFile` (OS fs write).
	// Go returns error; std::nullopt == nil.
	std::optional<std::string> WriteFile(std::string_view fileName,
	                                     std::string_view text);
	std::string Realpath(std::string_view path) override;
	AccessibleEntries GetAccessibleEntries(std::string_view path) override;

	SourceFile* GetSourceFile(const SourceFileParseOptions& opts,
	                          SourceFileMetaData metaData);

	// === slice: incremental ===
	// host.go GetSourceFile — ReadFile + ParseSourceFile + EnsureScriptKind.
	// Virtual so execute/watcher's watchCompilerHost can intercept every
	// load with its mtime cache (watcher.go watchCompilerHost).
	virtual SourceFile* GetSourceFile(const SourceFileParseOptions& opts);
	// === end slice: incremental ===
	// host.go ContentMapperProject — returns the field installed by
	// execute/tsc when the command line has content mappers.
	contentmapper::Project* ContentMapperProject() const {
		return contentMapperProject.get();
	}
	// === end slice: incremental ===

	// === slice: execute-tsc ===
	// host.go:38 — the remaining compilerHost fields and methods.
	tsoptions::ExtendedConfigCache* extendedConfigCache = nullptr;
	// host.go:38 trace — Go `func(msg *diagnostics.Message, args ...any)`.
	std::function<void(const DiagnosticMessage*,
	                   const std::vector<std::string>&)>
	    trace;
	std::shared_ptr<contentmapper::Project> contentMapperProject;

	// host.go FS() — Go returns the vfs.FS itself.
	std::shared_ptr<vfs::FS> FS() { return fs; }
	// host.go Trace.
	void Trace(const DiagnosticMessage* msg,
	           const std::vector<std::string>& args);
	// host.go GetContentMappedSourceFiles.
	std::pair<contentmapper::SourceFiles, gostd::Error>
	GetContentMappedSourceFiles(const SourceFileParseOptions& parseOptions,
	                            contentmapper::Mapper* mapper);
	// host.go GetResolvedProjectReference.
	tsoptions::ParsedCommandLine* GetResolvedProjectReference(
	    const std::string& fileName, const tspath::Path& path);
	// === end slice: execute-tsc ===
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
	// fileloader.go:71-76 — content-mapper bookkeeping. contentMapperMu
	// guards the maps/vector, which Go may write concurrently from parse
	// workers; kept here for faithfulness even when parsing is serial.
	std::mutex contentMapperMu;
	std::unordered_map<contentmapper::Mapper*, int> contentMapperFailures;
	std::unordered_set<contentmapper::Mapper*> contentMapperInitFailed;
	// Program-level diagnostics reported when a content mapper fails
	// fatally (reported once per mapper).
	std::vector<Diagnostic*> contentMapperDiagnostics;
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
	// fileloader.go:438 parseContentMappedFile + helpers (:578-677).
	SourceFile* parseContentMappedFile(SourceFileParseOptions opts);
	std::string getContentMapperTransformIdentity(contentmapper::Mapper* mapper);
	SourceFile* emptyContentMappedFile(SourceFileParseOptions& opts,
	                                   const std::string& mapperIdentity,
	                                   const std::string& transformIdentity);
	bool contentMapperUnavailable(contentmapper::Mapper* mapper);
	void recordContentMapperInitializationFailure(contentmapper::Mapper* mapper,
	                                              const std::string& label,
	                                              const gostd::Error& err);
	bool recordContentMapperFailure(contentmapper::Mapper* mapper,
	                                const std::string& label);

	includeProcessor* ip{};
};

// === slice: execute-tsc ===
// program.go:37 ProgramOptions.
struct ProgramOptions {
	CompilerHost* Host = nullptr;
	tsoptions::ParsedCommandLine* Config = nullptr;
	bool UseSourceOfProjectReference = false;
	Tristate SingleThreaded;
	// CreateCheckerPool — Go `func(*Program) CheckerPool`; the pool type is
	// unported (single-threaded port creates one checker lazily in
	// getChecker).
	std::function<void*(SimpleProgram*)> CreateCheckerPool;
	std::string TypingsLocation;
	std::string ProjectName;
	tracing::Tracing* Tracing = nullptr;
	// CreateModuleResolver — Go `func(module.ResolverOptions) module.Resolver`.
	std::function<module::Resolver*(const module::ResolverOptions&)>
	    CreateModuleResolver;
	// SkipModuleResolution avoids all module and type reference resolution while
	// still collecting import metadata needed for emit.
	bool SkipModuleResolution = false;
};

// program.go:285 NewProgram — creates a SimpleProgram after running the
// PhaseProgram trace span.
SimpleProgram* NewProgram(const ProgramOptions& opts);
// === end slice: execute-tsc ===

// === class SimpleProgram — checker.h `Program` + program.go ===
class SimpleProgram : public checker::Program, public ProgramLike {
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
	// Arena owning the synthetic import specifier nodes created by the
	// fileLoader's factory (Go: the loader's ast.NodeFactory keeps them
	// alive for the program's lifetime via GC). Moved in from
	// filesParser::factory after file loading completes.
	Arena syntheticImportArena;
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
	// program.go:76 contentMapperDiagnostics — diagnostics reported once per
	// content mapper when it fails fatally (moved in from the loader).
	std::vector<Diagnostic*> contentMapperDiagnostics;
	// program.go:107 contentMapperOptionDiagnostics — option diagnostics
	// from the host's content-mapper project.
	std::vector<Diagnostic*> contentMapperOptionDiagnostics;
	std::vector<Diagnostic*> configFileParsingDiagnostics;

	std::unique_ptr<checker::Checker> checker_;
	bool bindDone_{};
	mutable std::optional<std::string> commonSourceDirectory_;

	// === slice: incremental ===
	// program.go — the program's ParsedCommandLine (opts.Config) and tracing
	// session (opts.Tracing). commandLine_ is borrowed when the program is
	// constructed from a caller-owned ParsedCommandLine (the config ctor),
	// or points into commandLineOwned_ when the program synthesizes one
	// (the options/fileNames ctor). Tracing is set by callers that run a
	// trace session (nullptr otherwise).
	tsoptions::ParsedCommandLine* commandLine_ = nullptr;
	std::unique_ptr<tsoptions::ParsedCommandLine> commandLineOwned_;
	tracing::Tracing* tr_ = nullptr;
	// === end slice: incremental ===

	// === slice: execute-tsc ===
	// program.go — the options the program was created with (opts_). Empty
	// when the program was built through the plain ctor (CommandLine() then
	// falls back to the synthesized commandLine_).
	ProgramOptions opts_;
	// === end slice: execute-tsc ===

	// === slice: ls-foundation ===
	// program.go:98 usesUriStyleNodeCoreModules — declared and copied by
	// updateProgram but never assigned anywhere in the oracle; always
	// TSUnknown.
	Tristate usesUriStyleNodeCoreModules{};
	// === end slice: ls-foundation ===

	// program.go: hasEmitBlockingDiagnostics / sourceFilesToEmit (+Once).
	std::unordered_set<tspath::Path> hasEmitBlockingDiagnostics;
	bool sourceFilesToEmitComputed_{};
	std::vector<SourceFile*> sourceFilesToEmit_;
	mutable std::unordered_map<SourceFile*, std::vector<Diagnostic*>>
	    declarationDiagnosticCache;

	SimpleProgram(CompilerHost* host, const CompilerOptions& options,
	              std::vector<std::string> rootFileNames,
	              bool skipModuleResolution = false);
	// program.go NewProgram(ProgramOptions{Config}) — the parsed command
	// line supplies compiler options, root file names, project references
	// and content mappers; the program borrows it (caller-owned).
	SimpleProgram(CompilerHost* host, tsoptions::ParsedCommandLine* config,
	              bool skipModuleResolution = false);
	// Shared implementation for the two public ctors: a non-null `config`
	// is borrowed as the program's opts.Config; nullptr synthesizes a bare
	// ParsedCommandLine from `options`/`rootFileNames`.
	SimpleProgram(CompilerHost* host, const CompilerOptions& options,
	              std::vector<std::string> rootFileNames,
	              tsoptions::ParsedCommandLine* config,
	              bool skipModuleResolution);

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
	// === slice: ls-foundation ===
	// program.go:235 UsesUriStyleNodeCoreModules
	Tristate UsesUriStyleNodeCoreModules() const {
		return usesUriStyleNodeCoreModules;
	}
	// === end slice: ls-foundation ===

	// GetRedirectForResolution / GetProjectReferenceFromSource: base-class
	// nullptr defaults — Go-equivalent (no project references).

	// --- program.go methods beyond the interface ---
	checker::Checker* getChecker(); // lazily created after bind
	std::vector<Diagnostic*> GetConfigFileParsingDiagnostics() {
		return configFileParsingDiagnostics;
	}
	// program.go:828 collectContentMapperOptionDiagnostics.
	void collectContentMapperOptionDiagnostics();
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

	// === slice: emit ===
	// program.go:1390 IsEmitBlocked / :1384 blockEmittingOfFile
	bool IsEmitBlocked(const std::string& file) const;
	void blockEmittingOfFile(const std::string& emitFileName,
	                       Diagnostic* diag);
	// program.go:875 getSourceFilesToEmit (member, memoized for the
	// nil-target case)
	std::vector<SourceFile*> getSourceFilesToEmit(
	    const std::vector<SourceFile*>* targetSourceFiles,
	    bool forceDtsEmit, bool forceJsEmit);
	// program.go:1867 Emit / :242 GetSourceFileFromReference — Emit returns
	// a heap EmitResult like Go (nullptr == Go nil).
	EmitResult* Emit(EmitOptions* options);
	SourceFile* GetSourceFileFromReference(SourceFile* origin,
	                                     FileReference* ref);
	CompilerHost* Host() { return host; }

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
	// program.go:1490 — includeDeferredGlobals == Go's
	// getBindAndCheckDiagnosticsWithChecker(file, true): defers global
	// diagnostics until the file's check, then appends the delta.
	std::vector<Diagnostic*> getBindAndCheckDiagnosticsWithChecker(
	    SourceFile* sourceFile, bool includeDeferredGlobals = false);
	std::pair<std::vector<Diagnostic*>,
	          std::unordered_map<int, CommentDirective>>
	getDiagnosticsWithPrecedingDirectives(
	    SourceFile* sourceFile, std::vector<Diagnostic*> diags);

	// === slice: incremental ===
	// --- ProgramLike members (see program.go:1957) ---
	std::vector<SourceFile*> GetSourceFiles() override { return files; }
	std::vector<Diagnostic*> GetSuggestionDiagnostics(
	    SourceFile* sourceFile) override;
	SimpleProgram* GetProgram() override { return this; }

	// --- program.go methods the incremental Program delegates to ---
	// program.go:616 GetTypeCheckerForFileExclusive — single checker; the
	// release callback is a no-op like Go's when sharing the one checker.
	std::pair<checker::Checker*, std::function<void()>>
	GetTypeCheckerForFileExclusive(SourceFile* file);
	// program.go:2068 GetParseFileRedirect — always "" (no content mappers).
	std::string GetParseFileRedirect(const std::string& fileName) {
		return "";
	}
	// program.go GetDefaultLibFile — libFiles lookup by path.
	LibFile* GetDefaultLibFile(const tspath::Path& path) {
		auto it = libFiles.find(path);
		return it != libFiles.end() ? it->second : nullptr;
	}
	// program.go GetResolvedTypeReferenceDirectives.
	const std::unordered_map<
	    tspath::Path,
	    module::ModeAwareCache<module::ResolvedTypeReferenceDirective*>>&
	GetResolvedTypeReferenceDirectives() { return typeResolutionsInFile; }
	// program.go — opts.Config / opts.Tracing accessors.
	tsoptions::ParsedCommandLine* CommandLine() { return commandLine_; }
	tracing::Tracing* Tracing() { return tr_; }
	void SetTracing(tracing::Tracing* t) { tr_ = t; }
	// program.go PackageJsonCacheEntries — delegates to the resolver's
	// package-json scope cache.
	void PackageJsonCacheEntries(
	    const std::function<bool(
	        tspath::Path,
	        const std::shared_ptr<packagejson::InfoCacheEntry>&)>& f);
	// program.go:138 ContentMapperProject — delegates to the host.
	contentmapper::Project* ContentMapperProject() {
		return host != nullptr ? host->ContentMapperProject() : nullptr;
	}
	// === slice: execute-tsc ===
	// program.go FilesByPath — the live path→file map (watch-mode fast
	// path + explainFiles).
	const std::unordered_map<tspath::Path, SourceFile*>& FilesByPath()
	    const {
		return filesByPath;
	}
	// program.go:2120 ExplainFiles.
	void ExplainFiles(std::ostream& w, const locale::Locale& locale);
	// program.go:332 ReuseProgram — the UpdateProgram single-file fast
	// path. The reuse machinery (processedFiles replay, lazyValue
	// program state, updateFileIncludeProcessor, checker pool) is not
	// ported; reaching it is a dep-stub.
	// dep-stub — owned by compiler.
	std::tuple<SimpleProgram*, SourceFile*, bool> ReuseProgram(
	    const tspath::Path& changedFilePath, CompilerHost* newHost,
	    const std::function<void*(SimpleProgram*)>& createCheckerPool,
	    const std::function<module::Resolver*(const module::ResolverOptions&)>&
	        createModuleResolver);
	// program.go statistics helpers — aggregate counts across files.
	// LineCount — program.go (statistics.go statisticsFromProgram).
	int LineCount() const;
	int IdentifierCount() const;
	int SymbolCount() const;
	uint32_t TypeCount();
	uint64_t InstantiationCount();
	// === end slice: execute-tsc ===
	// program.go SingleThreaded — options.SingleThreaded == TS true.
	bool SingleThreaded() { return options.SingleThreaded == Tristate::True; }
	// program.go:804 GetSemanticDiagnosticsForIncremental — per-file
	// bind+check diagnostics with deferred globals, sorted/deduped.
	std::vector<std::pair<SourceFile*, std::vector<Diagnostic*>>>
	GetSemanticDiagnosticsForIncremental(
	    const std::vector<SourceFile*>& sourceFiles);
	// === end slice: incremental ===
};

// program.go: GetDiagnosticsOfAnyProgram — generalized to ProgramLike for
// the incremental Program (execute/incremental). The bind/check callbacks
// default to the program's own methods when nullptr (Go callers pass them
// explicitly; nullptr preserves those semantics here).
std::vector<Diagnostic*> getDiagnosticsOfAnyProgram(
    ProgramLike* program, const std::vector<SourceFile*>& files,
    bool skipNoEmitCheckForDtsDiagnostics,
    std::function<std::vector<Diagnostic*>(SourceFile*)>
        getBindDiagnostics = nullptr,
    std::function<std::vector<Diagnostic*>(SourceFile*)>
        getSemanticDiagnostics = nullptr);

// === slice: execute-tsc ===
// host.go:45 NewCachedFSCompilerHost — wraps fs in cachedvfs.From.
CompilerHost* NewCachedFSCompilerHost(
    std::string currentDirectory, const std::shared_ptr<vfs::FS>& fs,
    std::string defaultLibraryPath,
    tsoptions::ExtendedConfigCache* extendedConfigCache,
    std::function<void(const DiagnosticMessage*,
                       const std::vector<std::string>&)>
        trace,
    const std::shared_ptr<contentmapper::Project>& contentMapperProject);

// host.go:57 NewCompilerHost.
CompilerHost* NewCompilerHost(
    std::string currentDirectory, const std::shared_ptr<vfs::FS>& fs,
    std::string defaultLibraryPath,
    tsoptions::ExtendedConfigCache* extendedConfigCache,
    std::function<void(const DiagnosticMessage*,
                       const std::vector<std::string>&)>
        trace,
    const std::shared_ptr<contentmapper::Project>& contentMapperProject);
// === end slice: execute-tsc ===

std::vector<Diagnostic*> sortAndDeduplicateDiagnostics(
    std::vector<Diagnostic*> diagnostics);
std::vector<Diagnostic*> filterAndSortDiagnostics(
    std::vector<Diagnostic*> diags);
std::vector<Diagnostic*> filterNoEmitSemanticDiagnostics(
    std::vector<Diagnostic*> diags, const CompilerOptions* options);
std::vector<Diagnostic*> getAdditionalJSSyntacticDiagnostics(
    SourceFile* file, const CompilerOptions* options);
// === slice: emit === — program.go:1837+ / emitter.go:24
// EmitOnly — emitter.go:24
enum class EmitOnly : uint8_t {
	EmitAll,
	EmitOnlyJs,
	EmitOnlyDts,
	EmitOnlyBuilderSignature,
};

// WriteFileData — program.go:1837. `BuildInfo` is Go `any` (the
// incremental slice's BuildInfo type); opaque pointer here.
struct WriteFileData {
	int SourceMapUrlPos = 0;
	void* BuildInfo = nullptr;
	std::vector<Diagnostic*> Diagnostics;
	bool SkippedDtsWrite = false;
	SourceFile* SourceFile = nullptr;
};

// WriteFile — program.go:1845. Go `error` -> std::optional<std::string>
// (nullopt == nil).
using WriteFile =
	std::function<std::optional<std::string>(const std::string& fileName,
	                                         const std::string& text,
	                                         WriteFileData* data)>;

// EmitOptions — program.go:1847. TargetSourceFiles empty == Go nil
// (emit all files).
struct EmitOptions {
	std::vector<SourceFile*> TargetSourceFiles;
	EmitOnly EmitOnly = EmitOnly::EmitAll;
	bool ForceEmit = false;
	WriteFile WriteFile;
};

// SourceMapEmitResult — program.go:1860
struct SourceMapEmitResult {
	std::vector<std::string> InputSourceFileNames;
	sourcemap::RawSourceMap* SourceMap = nullptr;
	std::string GeneratedFile;
};

// EmitResult — program.go:1854
struct EmitResult {
	bool EmitSkipped = false;
	std::vector<Diagnostic*> Diagnostics; // Contains declaration emit diagnostics
	std::vector<std::string> EmittedFiles; // Array of files the compiler wrote to disk
	std::vector<SourceMapEmitResult> SourceMaps; // Array of sourceMapData if compiler emitted sourcemaps
};

// emitter.go:493 sourceFileMayBeEmitted — the host is the Go
// SourceFileMayBeEmittedHost interface; its methods are a subset of
// checker::Program, which both SimpleProgram and emitHost satisfy.
bool sourceFileMayBeEmitted(SourceFile* sourceFile,
                            checker::Program* host, bool forceDtsEmit,
                            bool forceJsEmit);
// emitter.go:553 getSourceFilesToEmit — nullptr == Go nil (all files).
std::vector<SourceFile*> getSourceFilesToEmit(
    checker::Program* host,
    const std::vector<SourceFile*>* targetSourceFiles, bool forceDtsEmit,
    bool forceJsEmit);
// program.go:1941 CombineEmitResults — Go returns *EmitResult (never nil).
EmitResult* CombineEmitResults(const std::vector<EmitResult*>& results);
// program.go:1966 HandleNoEmitOptions — files nullptr == Go nil;
// emitBuildInfo nullptr == Go nil. Returns a heap EmitResult like Go
// (nullptr result == Go nil return).
EmitResult* HandleNoEmitOptions(
    ProgramLike* program, const std::vector<SourceFile*>* files,
    const std::function<EmitResult*()>& emitBuildInfo);

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
