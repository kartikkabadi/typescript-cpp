#pragma once

// Port of tsc/internal/compiler — program slice (file loading + Program for
// `tsc --noEmit <files>`). No emit, no build mode, no content mappers, no
// incremental state, no project references.
#include <atomic>
#include <deque>
#include <functional>
#include <iosfwd>
#include <memory>
#include <mutex>
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
#include "internal/compiler/checkerpool.h"
#include "internal/collections/collections.h" // === slice: ls-autoimport ===
#include "internal/core/context.h"
#include "internal/core/types.h"
#include "internal/module/resolver.h"
#include "internal/module/types.h"
#include "internal/symlinks/knownsymlinks.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc::sourcemap {
struct RawSourceMap;
}
namespace tsc::tracing {
class Tracing;
}
namespace tsc {
// core/workgroup.go — forward decl so filesParser can hold one without
// pulling utilities.h in; destructor lives in fileloader.cpp.
struct workGroup;
}

namespace tsc::compiler {

class SimpleProgram;
// checkerpool.go — CheckerPool interface + the built-in checkerPool
// (full decls in checkerpool.h; pointers suffice here).
class CheckerPool;
class checkerPool;
// === slice: project ===
// fileloader.go:89 DuplicateSourceFile — the full struct is
// dep-declared in project/parsecache.h (which needs its fields); this
// fwd decl lets SimpleProgram::DuplicateSourceFiles name the type.
struct DuplicateSourceFile;
// program.go:37 ProgramOptions — full def is in the `slice: execute-tsc`
// block below (its old home); filesLoader and the project-reference
// machinery keep it by pointer.
struct ProgramOptions;
// projectreferencefilemapper.go — full def below the filesLoader block.
struct projectReferenceFileMapper;

// program.go:56 lazyValue — a once-computed cell whose value may be
// shared (not recomputed) when a program is reused: tryReuse copies the
// source's computed value, and getValue keeps any value already present
// (reused or computed) instead of overwriting it. shared_ptr preserves
// Go's shared *T (both programs observe the same value).
template <typename T>
struct lazyValue {
	std::shared_ptr<T> value;
	std::once_flag once;
	std::atomic<bool> initialized{false};

	// getValue — program.go:58. `compute` runs at most once; a value
	// already installed by tryReuse is returned as-is.
	T* getValue(const std::function<T*()>& compute) {
		std::call_once(once, [this, &compute] {
			if (value == nullptr) {
				value.reset(compute());
			}
			initialized.store(true, std::memory_order_release);
		});
		return value.get();
	}

	// tryReuse — program.go:68. Adopts `from`'s value only when `from`
	// already ran its computation.
	void tryReuse(const lazyValue* from) {
		if (from->initialized.load(std::memory_order_acquire)) {
			value = from->value;
			initialized.store(true, std::memory_order_release);
		}
	}
};
// === end slice: project ===

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

	virtual SourceFile* GetSourceFile(const SourceFileParseOptions& opts,
	                                  SourceFileMetaData metaData);

	// === slice: incremental ===
	// host.go GetSourceFile — ReadFile + ParseSourceFile + EnsureScriptKind.
	// Virtual so execute/watcher's watchCompilerHost can intercept every
	// load with its mtime cache (watcher.go watchCompilerHost).
	virtual SourceFile* GetSourceFile(const SourceFileParseOptions& opts);
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
	// host.go Trace — part of Go's compiler.CompilerHost interface:
	// virtual so project/build hosts' overrides are dispatched.
	virtual void Trace(const DiagnosticMessage* msg,
	                   const std::vector<std::string>& args);
	// host.go ContentMapperProject — returns the field installed by
	// execute/tsc when the command line has content mappers. Virtual:
	// execute/build's compilerHost overrides it (compilerHost.go).
	virtual contentmapper::Project* ContentMapperProject() const {
		return contentMapperProject.get();
	}
	// host.go GetContentMappedSourceFiles. Virtual for the same reason.
	virtual std::pair<contentmapper::SourceFiles, gostd::Error>
	GetContentMappedSourceFiles(const SourceFileParseOptions& parseOptions,
	                            contentmapper::Mapper* mapper);
	// host.go GetResolvedProjectReference. Virtual for the same reason.
	virtual tsoptions::ParsedCommandLine* GetResolvedProjectReference(
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
	// arenaMu guards the three arenas below — parse workers allocate
	// reasons/diagnostics concurrently inside their per-path data.mu
	// critical sections (Go: heap alloc under GC).
	std::mutex arenaMu;
	std::unordered_map<tspath::Path,
	                   std::vector<const FileIncludeReason*>>
	    fileIncludeReasons;
	std::vector<processingDiagnostic*> processingDiagnostics;

	// arenas for stable pointers — shared_ptr because ReuseProgram's
	// splice shares the old program's processor with the new one (Go: the
	// processedFiles embed carries the *includeProcessor pointer, and the
	// GC keeps the reasons alive across both programs).
	std::shared_ptr<std::vector<std::unique_ptr<FileIncludeReason>>>
	    reasonArena = std::make_shared<
	        std::vector<std::unique_ptr<FileIncludeReason>>>();
	std::shared_ptr<std::vector<std::unique_ptr<processingDiagnostic>>>
	    processingDiagArena = std::make_shared<
	        std::vector<std::unique_ptr<processingDiagnostic>>>();
	std::shared_ptr<std::vector<std::unique_ptr<Diagnostic>>> diagArena =
	    std::make_shared<std::vector<std::unique_ptr<Diagnostic>>>();

	const FileIncludeReason* newReason(FileIncludeKind kind,
	                                   FileIncludeReason::DataV data) {
		std::lock_guard<std::mutex> lock(arenaMu);
		return reasonArena
		    ->emplace_back(
		        std::make_unique<FileIncludeReason>(FileIncludeReason{kind,
		                                                            data}))
		    .get();
	}
	processingDiagnostic* newProcessingDiagnostic(
	    processingDiagnosticKind k,
	    std::variant<const FileIncludeReason*, includeExplainingDiagnostic>
	        d) {
		std::lock_guard<std::mutex> lock(arenaMu);
		return processingDiagArena
		    ->emplace_back(
		        std::make_unique<processingDiagnostic>(
		            processingDiagnostic{k, std::move(d)}))
		    .get();
	}

	DiagnosticsCollection* getDiagnostics(SimpleProgram* p);
	referenceFileLocation getReferenceLocation(const FileIncludeReason* r,
	                                           SimpleProgram* p);
	Diagnostic* getRelatedInfo(const FileIncludeReason* r, SimpleProgram* p);
	// includeprocessor.go:98 getCompilerOptionsObjectLiteralSyntax —
	// once-computed (bool, not once_flag: the struct is move-copied by
	// updateFileIncludeProcessor's container). nullptr after compute ==
	// no compilerOptions object literal in the config.
	ObjectLiteralExpression* getCompilerOptionsObjectLiteralSyntax(
	    SimpleProgram* p);
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
	// compilerOptionsSyntax + once flag (includeprocessor.go:24-25).
	ObjectLiteralExpression* compilerOptionsSyntax{};
	bool compilerOptionsSyntaxComputed{};
};

// includeprocessor.go:28 updateFileIncludeProcessor — ReuseProgram
// re-key helper (see includeprocessor.cpp).
void updateFileIncludeProcessor(SimpleProgram* p);

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

struct filesLoader;
struct parseTask;
struct parseTaskData {
	// filesparser.go:27 — one mutex per path's data; every read/write of the
	// fields below happens under it on the parallel parse path (Go: data.mu).
	std::mutex mu;
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
	const tspath::Path& Path() const { return path; }

	// filesparser.go:185 redirect — swap this task's work for a task
	// that parses the project-reference redirect target instead. The
	// redirectedParseTask keeps libFile/includeReason; increaseDepth and
	// elideOnDepth are intentionally not copied (they would double-count).
	void redirect(filesLoader* loader, const std::string& fileName);
};

struct filesLoader;

// filesParser — DFS file graph walker. Runs the file graph in parallel
// through a work group when the program isn't single-threaded (Go:
// filesParser.start queues each task on core.WorkGroup).
struct filesParser {
	filesLoader* loader{};
	int maxDepth{};
	bool singleThreaded{};
	~filesParser(); // out-of-line: tsc::workGroup is incomplete here
	// filesparser.go:20 — Go's collections.SyncMap[path]*parseTaskData;
	// the map itself is only touched under taskDataMapMu (LoadOrStore).
	std::unordered_map<tspath::Path, std::unique_ptr<parseTaskData>>
	    taskDataByPath;
	std::mutex taskDataMapMu;
	// arenaMu guards both allocation arenas: newTask runs inside per-path
	// data.mu critical sections that overlap across paths (Go: GC).
	std::mutex arenaMu;
	std::vector<std::unique_ptr<parseTask>> taskArena;
	NodeFactory factory; // synthetic imports only (guarded by filesLoader::factoryMu)
	// arena for empty ResolvedModule sentinels (Go: &module.ResolvedModule{})
	std::deque<module::ResolvedModule> resolvedModuleArena;
	// Keep-alive for shared_ptr<ResolvedModule> results stored as raw
	// pointers in per-file resolutionsInFile maps. The resolver cache
	// uses LoadOrStore, so a result that loses a concurrent store isn't
	// owned by the cache (Go: GC keeps every result alive). Moved to
	// SimpleProgram when loading completes.
	std::vector<std::shared_ptr<module::ResolvedModule>>
	    resolvedModuleKeepAlive;
	// filesparser.go:21 — Go's w.wg; created in getProcessedFiles.
	std::unique_ptr<tsc::workGroup> wg;

	filesParser() = default;
	filesParser(const filesParser&) = delete;

	// task arena helpers
	parseTask* newTask(const std::string& normalizedFilePath) {
		std::lock_guard<std::mutex> lock(arenaMu);
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
	// filesparser.go:335,354 — parsed-but-deduplicated source files the
	// program must release on snapshot disposal, and the dts-path ->
	// source-name map written only when the program cannot use
	// project-reference sources. (DuplicateSourceFile is fwd-declared;
	// shared_ptr lets the vector live here without the full def.)
	std::vector<std::shared_ptr<DuplicateSourceFile>>
	    duplicateSourceFiles;
	std::unordered_map<tspath::Path, std::string>
	    outputFileToProjectReferenceSource;

	void collectFiles(const std::vector<parseTask*>& tasks);
	void addIncludeReason(parseTask* task, const FileIncludeReason* reason);
	// filesparser.go:330 getProcessedFiles — parse + collect + tail
	void getProcessedFiles(const std::vector<parseTask*>& rootTasks);
};

// filesLoader — fileloader.go's fileLoader struct.
struct filesLoader {
	// fileloader.go:60 — the ProgramOptions this loader was constructed
	// with (borrowed; points at the owning program's opts_ or the ctor's
	// synthesized ProgramOptions — valid through processAllProgramFiles).
	const ProgramOptions* opts{};
	CompilerHost* host{};
	CompilerOptions* compilerOptions{};
	// fileloader.go:77 — module.Resolver interface value (the default
	// resolver or opts.CreateModuleResolver's); resolverOwned keeps the
	// created resolver alive for the program to adopt.
	module::Resolver* resolver{};
	std::shared_ptr<module::Resolver> resolverOwned;
	filesParser* parser{};
	SimpleProgram* program{};
	// fileloader.go:66 — declaration directories of project references
	// (filled by projectReferenceParser::initMapperWorker). Go's
	// collections.Set is a map — a reference type: the faking vfs at
	// projectreferencedtsfakinghost.go:29 copies the map header and
	// keeps the shared map alive past the loader's lifetime. Mirrored
	// with a shared_ptr so the post-ctor resolution host doesn't
	// dereference the destroyed stack loader's field.
	std::shared_ptr<collections::Set<tspath::Path>> dtsDirectories =
	    std::make_shared<collections::Set<tspath::Path>>();
	// fileloader.go:67 — the source/dts <-> project-reference mapping;
	// shared with the produced program. Its loader/host links are
	// released after parsing (fileloader.go:223).
	std::shared_ptr<projectReferenceFileMapper> projectReferenceFileMapper;
	// fileloader.go:78 — first module-resolution error. (The C++
	// module::Resolver interface does not surface errors, so this stays
	// empty; the slot exists so processedFiles.moduleResolutionError is
	// populated like Go.)
	gostd::Error moduleResolutionError;
	// fileloader.go:58-59 — counts feeding map/vector capacity hints
	// (atomic.Int32 in Go; parse workers increment them in parallel).
	std::atomic<int32_t> totalFileCount{0};
	std::atomic<int32_t> libFileCount{0};
	// factoryMu — fileloader.go:58. Guards parser->factory during synthetic
	// import creation on parallel parse workers.
	std::mutex factoryMu;
	// libMu serializes pathForLibFile's cache fills (Go: the
	// pathForLibFileCache/pathForLibFileResolutions SyncMaps); only a
	// handful of lib files, so one lock for both maps.
	std::mutex libMu;

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

	// fileloader.go — p.opts.Tracing; seeded from the program's opts
	// before loadFiles runs.
	tracing::Tracing* tracing{};

	void addRootTask(const std::string& fileName, LibFile* libFile,
	                 const FileIncludeReason* includeReason);
	void addRootFileTask(const std::string& fileName, LibFile* libFile,
	                     const FileIncludeReason* includeReason);
	void addAutomaticTypeDirectiveTasks();
	// fileloader.go:340 addProjectReferenceTasks — build the mapper and
	// parse every referenced project's config.
	void addProjectReferenceTasks(bool singleThreaded);
	// fileloader.go:152 processAllProgramFiles — fills program state
	void processAllProgramFiles(const std::vector<std::string>& rootFileNames,
	                            bool singleThreaded);
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

// ===========================================================================
// projectreferencefilemapper.go / projectreferenceparser.go /
// projectreferencedtsfakinghost.go
// ===========================================================================

// projectreferenceparser.go:13.
struct projectReferenceParser;
struct projectReferenceParseTask {
	std::string configName;
	tsoptions::ParsedCommandLine* resolved{};
	std::vector<projectReferenceParseTask*> subTasks;

	// projectreferenceparser.go:19 — resolve the config through the
	// loader's host and build sub-tasks for its own project references.
	void parse(projectReferenceParser* parser);
};

// projectreferencefilemapper.go:14 — the source/dts <->
// project-reference mapping built by projectReferenceParser; the loader
// and host links are released once parsing finishes.
struct projectReferenceFileMapper {
	const ProgramOptions* opts{};
	module::ResolutionHost* host{};
	// Only present during populating the mapper and parsing, released
	// after that (fileloader.go:223).
	filesLoader* loader{};

	// All the resolved references needed (config path -> resolved config;
	// a null value marks a reference that failed to resolve).
	std::unordered_map<tspath::Path, tsoptions::ParsedCommandLine*>
	    configToProjectReference;
	// Map of config file to its references.
	std::unordered_map<tspath::Path, std::vector<tspath::Path>>
	    referencesInConfigFile;
	// Source file path -> project reference.
	std::unordered_map<tspath::Path,
	                   tsoptions::SourceOutputAndProjectReference*>
	    sourceToProjectReference;
	// Declared output file path -> project reference.
	std::unordered_map<tspath::Path,
	                   tsoptions::SourceOutputAndProjectReference*>
	    outputDtsToProjectReference;

	// projectreferencefilemapper.go:25 — realpath dts -> source memo
	// (needed only while parsing); a stored nullptr marks probed misses.
	collections::SyncMap<tspath::Path,
	                     tsoptions::SourceOutputAndProjectReference*>
	    realpathDtsToSource;

	// The faking host (+ its vfs and cached wrapper) when
	// canUseProjectReferenceSource() — owned here so the resolver that
	// captured mapper->host keeps a live host after parse clears the
	// mapper->host link.
	std::unique_ptr<struct projectReferenceDtsFakingVfs> fakingVfsImpl;
	std::shared_ptr<vfs::FS> fakingVfs;
	std::unique_ptr<struct projectReferenceDtsFakingHost> fakingHost;

	tspath::Path rootConfigPath() const;
	std::string getParseFileRedirect(const HasFileName& file);
	std::vector<tsoptions::ParsedCommandLine*>
	getResolvedProjectReferences();
	tsoptions::SourceOutputAndProjectReference*
	getProjectReferenceFromSource(const tspath::Path& path);
	tsoptions::SourceOutputAndProjectReference*
	getProjectReferenceFromOutputDts(const tspath::Path& path);
	bool isSourceFromProjectReference(const tspath::Path& path);
	const CompilerOptions*
	getCompilerOptionsForFile(const HasFileName& file);
	tsoptions::ParsedCommandLine*
	getRedirectParsedCommandLineForResolution(const HasFileName& file);
	std::pair<tsoptions::ParsedCommandLine*, std::string>
	getRedirectForResolution(const HasFileName& file);
	std::pair<tsoptions::ParsedCommandLine*, bool>
	getResolvedReferenceFor(const tspath::Path& path);
	bool rangeResolvedProjectReference(
	    const std::function<bool(tspath::Path,
	                             tsoptions::ParsedCommandLine*,
	                             tsoptions::ParsedCommandLine*, int)>& f);
	// projectreferencefilemapper.go:129 — recursive worker shared by
	// both range entry points.
	bool rangeResolvedReferenceWorker(
	    const std::vector<tspath::Path>& references,
	    const std::function<bool(tspath::Path,
	                             tsoptions::ParsedCommandLine*,
	                             tsoptions::ParsedCommandLine*, int)>& f,
	    tsoptions::ParsedCommandLine* parent,
	    collections::Set<tspath::Path>* seenRef);
	bool rangeResolvedProjectReferenceInChildConfig(
	    tsoptions::ParsedCommandLine* childConfig,
	    const std::function<bool(tspath::Path,
	                             tsoptions::ParsedCommandLine*,
	                             tsoptions::ParsedCommandLine*, int)>& f);
	tsoptions::SourceOutputAndProjectReference*
	getSourceToDtsIfSymlink(const HasFileName& file);
};

// projectreferencedtsfakinghost.go:46 — a vfs.FS that fakes
// project-reference .d.ts existence from their source files so the
// resolver can resolve through them when useSourceOfProjectReference is
// on. Wrapped in cachedvfs like Go.
struct projectReferenceDtsFakingVfs : vfs::FS {
	projectReferenceFileMapper* mapper{};
	// projectreferencedtsfakinghost.go:29 — Go copies the map header,
	// sharing the underlying Set and keeping it alive via GC. A raw
	// pointer into the stack-local loader used to dangle here: post-ctor
	// resolutions (auto-import specifiers -> DirectoryExists -> Keys())
	// iterated a dead unordered_set — the paths_stripSrc/paths_toDist
	// SIGSEGVs. shared_ptr mirrors Go's shared-reference lifetime.
	std::shared_ptr<collections::Set<tspath::Path>> dtsDirectories;
	// A fresh KnownSymlinks (Go: `symlinks.KnownSymlinks{}` — empty,
	// zero-value cwd/case flag).
	symlinks::KnownSymlinks knownSymlinks{"", false};

	// projectreferencedtsfakinghost.go:75/81 — out-of-line: ProgramOptions
	// is only forward-declared at this point in the header.
	bool UseCaseSensitiveFileNames() override;
	bool FileExists(const std::string& path) override;
	// ReadFile — passthrough: cannot mimick a dts read (Go comments).
	std::pair<std::string, bool>
	ReadFile(const std::string& path) override;
	vfs::Error WriteFile(const std::string& /*path*/,
	                     const std::string& /*data*/) override {
		TSC_UNREACHABLE(
		    "projectReferenceDtsFakingVfs::WriteFile — should not be "
		    "called by resolver");
	}
	vfs::Error AppendFile(const std::string& /*path*/,
	                      const std::string& /*data*/) override {
		TSC_UNREACHABLE(
		    "projectReferenceDtsFakingVfs::AppendFile — should not be "
		    "called by resolver");
	}
	vfs::Error Remove(const std::string& /*path*/) override {
		TSC_UNREACHABLE(
		    "projectReferenceDtsFakingVfs::Remove — should not be called "
		    "by resolver");
	}
	vfs::Error Chtimes(const std::string& /*path*/, vfs::TimePoint,
	                   vfs::TimePoint) override {
		TSC_UNREACHABLE(
		    "projectReferenceDtsFakingVfs::Chtimes — should not be "
		    "called by resolver");
	}
	bool DirectoryExists(const std::string& path) override;
	vfs::Entries GetAccessibleEntries(const std::string& /*path*/) override {
		TSC_UNREACHABLE(
		    "projectReferenceDtsFakingVfs::GetAccessibleEntries — should "
		    "not be called by resolver");
	}
	std::shared_ptr<vfs::FileInfo>
	Stat(const std::string& /*path*/) override {
		TSC_UNREACHABLE(
		    "projectReferenceDtsFakingVfs::Stat — should not be called "
		    "by resolver");
	}
	std::string Realpath(const std::string& path) override;

	tspath::Path toPath(const std::string& path) const;
	void handleDirectoryCouldBeSymlink(const std::string& directory);
	bool fileOrDirectoryExistsUsingSource(const std::string& fileOrDirectory,
	                                      bool isFile);
	Tristate fileExistsIfProjectReferenceDts(const std::string& file);
	Tristate directoryExistsIfProjectReferenceDeclDir(const std::string& dir);
};

// projectreferencedtsfakinghost.go:16 — a module.ResolutionHost whose
// file ops run over the faking vfs; GetCurrentDirectory defers to the
// real host.
struct projectReferenceDtsFakingHost : module::ResolutionHost {
	CompilerHost* host{};
	vfs::FS* fs{}; // the cachedvfs-wrapped projectReferenceDtsFakingVfs

	bool FileExists(std::string_view path) override {
		return fs->FileExists(std::string(path));
	}
	bool DirectoryExists(std::string_view path) override {
		return fs->DirectoryExists(std::string(path));
	}
	std::optional<std::string> ReadFile(std::string_view path) override {
		auto [text, ok] = fs->ReadFile(std::string(path));
		if (!ok) {
			return std::nullopt;
		}
		return text;
	}
	std::string Realpath(std::string_view path) override {
		return fs->Realpath(std::string(path));
	}
	std::string GetCurrentDirectory() override {
		return host->GetCurrentDirectory();
	}
	bool UseCaseSensitiveFileNames() override {
		return fs->UseCaseSensitiveFileNames();
	}
	AccessibleEntries GetAccessibleEntries(
	    std::string_view /*path*/) override {
		TSC_UNREACHABLE(
		    "projectReferenceDtsFakingHost::GetAccessibleEntries — the "
		    "faking vfs has none");
	}
};

// projectreferenceparser.go:42 projectReferenceParser — defined in
// fileloader.cpp (the only TU that uses it): it owns a
// std::unique_ptr<workGroup>, and naming tsc::workGroup in this header
// would hijack `struct workGroup` elaborated specifiers in TUs that
// define a same-named local type (execute/build).

// projectreferenceparser.go:34 createProjectReferenceParseTasks.
std::vector<projectReferenceParseTask*> createProjectReferenceParseTasks(
    const std::vector<std::string>& projectReferences,
    std::deque<std::unique_ptr<projectReferenceParseTask>>& arena);

// === slice: execute-tsc ===
// program.go:37 ProgramOptions.
struct ProgramOptions {
	CompilerHost* Host = nullptr;
	tsoptions::ParsedCommandLine* Config = nullptr;
	bool UseSourceOfProjectReference = false;
	Tristate SingleThreaded;
	// CreateCheckerPool — Go `func(*Program) CheckerPool`. When null, the
	// program constructs the built-in checkerPool itself (initCheckerPool).
	std::function<CheckerPool*(SimpleProgram*)> CreateCheckerPool;
	std::string TypingsLocation;
	std::string ProjectName;
	tracing::Tracing* Tracing = nullptr;
	// CreateModuleResolver — Go `func(module.ResolverOptions) module.Resolver`.
	std::function<module::Resolver*(const module::ResolverOptions&)>
	    CreateModuleResolver;
	// SkipModuleResolution avoids all module and type reference resolution while
	// still collecting import metadata needed for emit.
	bool SkipModuleResolution = false;

	// program.go:52 canUseProjectReferenceSource — resolve project
	// references to their source files unless explicitly disabled.
	bool canUseProjectReferenceSource() const {
		return UseSourceOfProjectReference && Config != nullptr &&
		       !tristateIsTrue(Config->CompilerOptions()
		                           ->DisableSourceOfProjectReferenceRedirect);
	}
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
	// filesParser::factory after file loading completes; shared_ptr so a
	// program reused via ReuseProgram keeps the nodes alive (Go: GC).
	std::shared_ptr<Arena> syntheticImportArena;
	std::unordered_set<tspath::Path> sourceFilesFoundSearchingNodeModules;
	std::unordered_map<tspath::Path, LibFile*> libFiles;
	// The LibFile objects libFiles points into are owned by the loader's
	// pathForLibFileCache (Go: GC keeps them alive for the program's
	// lifetime). Moved into the program when loading completes — without
	// this the libFiles values dangle after createProgram returns.
	std::unordered_map<std::string, std::unique_ptr<LibFile>>
	    pathForLibFileCache;
	// Unresolved-resolution placeholders created during file loading
	// (raw ResolvedModule* live in per-file resolutionsInFile maps;
	// Go: GC keeps them alive). Moved in from filesParser when loading
	// completes — deque so element addresses are stable across moves.
	std::deque<module::ResolvedModule> resolvedModuleArena;
	std::vector<std::shared_ptr<module::ResolvedModule>>
	    resolvedModuleKeepAlive;
	std::vector<std::string> missingFiles;
	std::unordered_map<tspath::Path, std::vector<std::string>>
	    redirectTargetsMap;
	std::unordered_map<tspath::Path, redirectsFile> redirectFilesByPath;
	std::vector<std::string> fileNameList; // ordered root file names

	// === slice: project ===
	// processedFiles fields (fileloader.go:113-145) — populated in the
	// ctor from the parser/loader tail and shared verbatim by
	// ReuseProgram (program.go:408).
	std::vector<std::shared_ptr<DuplicateSourceFile>>
	    duplicateSourceFiles;
	// filesparser.go:581 outputFileToProjectReferenceSource — the dts
	// path -> source name map; only populated when
	// !opts.canUseProjectReferenceSource().
	std::unordered_map<tspath::Path, std::string>
	    outputFileToProjectReferenceSource;
	// fileloader.go:124 projectReferenceFileMapper — always non-null
	// after construction (the loader creates it unconditionally).
	std::shared_ptr<projectReferenceFileMapper>
	    projectReferenceFileMapper_;
	// fileloader.go:144 finishedProcessing — set once getProcessedFiles
	// has run.
	bool finishedProcessing = false;
	// === end slice: project ===

	includeProcessor includeProcessor_;
	// program.go:120 resolver — module.Resolver; shared with a reused
	// program (processedFiles.resolver).
	std::shared_ptr<module::Resolver> resolver_;
	std::vector<Diagnostic*> programDiagnostics;
	// program.go:76 contentMapperDiagnostics — diagnostics reported once per
	// content mapper when it fails fatally (moved in from the loader).
	std::vector<Diagnostic*> contentMapperDiagnostics;
	// program.go:107 contentMapperOptionDiagnostics — option diagnostics
	// from the host's content-mapper project.
	std::vector<Diagnostic*> contentMapperOptionDiagnostics;
	std::vector<Diagnostic*> configFileParsingDiagnostics;

	std::unique_ptr<checker::Checker> checker_;
	mutable std::optional<std::string> commonSourceDirectory_;
	mutable OnceFlag commonSourceDirectoryOnce_;
	mutable OnceFlag packagesMapOnce_;

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
	// Go's Set is a shared map reference — the ReuseProgram literal
	// copies the map header, so both programs observe later writes;
	// shared_ptr reproduces that sharing.
	std::shared_ptr<std::unordered_set<tspath::Path>>
	    hasEmitBlockingDiagnostics =
	        std::make_shared<std::unordered_set<tspath::Path>>();
	bool sourceFilesToEmitComputed_{};
	std::vector<SourceFile*> sourceFilesToEmit_;
	// declarationDiagnosticCache — program.go:103 collections.SyncMap
	// (Load/LoadOrStore): concurrent declaration diagnostics collect into
	// it per file.
	mutable collections::SyncMap<SourceFile*, std::vector<Diagnostic*>>
	    declarationDiagnosticCache;

	// program.go:405 &Program{...} — the ReuseProgram splice constructs a
	// bare program and copies fields; no loading runs.
	SimpleProgram() = default;
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
	// ParsedCommandLine from `options`/`rootFileNames`. `tracing` seeds
	// tr_ BEFORE files load (Go program.go:302 — opts.Tracing is set on
	// the program before loadFiles runs). `popts`, when non-null, is the
	// caller's full ProgramOptions and seeds opts_ BEFORE the loader runs
	// (Go's `p := &Program{opts: opts}` — the loader/mapper read fields
	// like UseSourceOfProjectReference during processAllProgramFiles).
	SimpleProgram(CompilerHost* host, const CompilerOptions& options,
	              std::vector<std::string> rootFileNames,
	              tsoptions::ParsedCommandLine* config,
	              bool skipModuleResolution,
	              tracing::Tracing* tracing = nullptr,
	              const ProgramOptions* programOpts = nullptr);

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
	// program.go:532 — read live off opts.Config (the C++ field was never
	// populated for fresh programs, silently dropping config-file
	// diagnostics like TS5023/TS6046/TS18002/TS5083).
	std::vector<Diagnostic*> GetConfigFileParsingDiagnostics() {
		if (commandLine_ != nullptr) {
			return commandLine_->GetConfigFileParsingDiagnostics();
		}
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
	// === slice: ls-coreA ===
	// program.go:590 GetTypeChecker / :607 GetTypeCheckerForFile — Go's
	// checker pool hands out per-file checkers; the built-in pool returns
	// the file's associated checker, non-exclusive (getChecker is the
	// first pool checker, matching Go's checkers[0]).
	std::pair<checker::Checker*, std::function<void()>> GetTypeChecker(
	    const ContextPtr& ctx) {
		return GetTypeCheckerForFileExclusive(nullptr);
	}
	std::pair<checker::Checker*, std::function<void()>> GetTypeCheckerForFile(
	    const ContextPtr& ctx, SourceFile* file) {
		return getTypeCheckerForFileNonExclusive(file);
	}
	// program.go:2171 GetResolvedTypeReferenceDirectiveFromTypeReferenceDirective.
	module::ResolvedTypeReferenceDirective*
	GetResolvedTypeReferenceDirectiveFromTypeReferenceDirective(
	    const FileReference* typeRef, SourceFile* sourceFile) {
		// getModeForTypeReferenceDirectiveInFile inlined below (program.go:2188).
		ResolutionMode mode = typeRef->ResolutionMode != ResolutionModeNone
		                        ? typeRef->ResolutionMode
		                        : GetDefaultResolutionModeForFile(sourceFile);
		return GetResolvedTypeReferenceDirective(sourceFile, typeRef->FileName,
		                                         mode);
	}
	// === end slice: ls-coreA ===
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
	// === slice: tsctests ===
	// GetIncludeReasonsMap — program.go:2109 GetIncludeReasons — the whole
	// include-reason map (tsctests dumps it in OnProgram; the per-path
	// overload above is the lookup spelling other callers use).
	const std::unordered_map<tspath::Path,
	                         std::vector<const FileIncludeReason*>>&
	GetIncludeReasonsMap() const {
		return includeProcessor_.fileIncludeReasons;
	}
	// IsMissingPath — program.go:2114 (Go marks it "Testing only").
	bool IsMissingPath(const tspath::Path& path) const {
		return std::any_of(missingFiles.begin(), missingFiles.end(),
		                   [&](const std::string& missingPath) {
			                   return toPath(missingPath) == path;
		                   });
	}
	// === end slice: tsctests ===
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
	    SourceFile* sourceFile, bool concurrent,
	    const std::function<std::vector<Diagnostic*>(SourceFile*)>& collect);
	std::vector<std::vector<Diagnostic*>> collectDiagnosticsFromFiles(
	    const std::vector<SourceFile*>& sourceFiles, bool concurrent,
	    const std::function<std::vector<Diagnostic*>(SourceFile*)>& collect);
	std::vector<Diagnostic*> collectCheckerDiagnostics(
	    SourceFile* sourceFile,
	    const std::function<std::vector<Diagnostic*>(checker::Checker*,
	                                               SourceFile*)>& collect);
	std::vector<std::vector<Diagnostic*>> collectCheckerDiagnosticsFromFiles(
	    const std::vector<SourceFile*>& sourceFiles,
	    const std::function<std::vector<Diagnostic*>(checker::Checker*,
	                                               SourceFile*)>& collect);
	std::vector<Diagnostic*> getSemanticDiagnosticsWithChecker(
	    checker::Checker* fileChecker, SourceFile* sourceFile);
	std::vector<Diagnostic*> getSuggestionDiagnosticsWithChecker(
	    checker::Checker* fileChecker, SourceFile* sourceFile);
	// program.go:1490 — includeDeferredGlobals == Go's
	// getBindAndCheckDiagnosticsWithChecker(file, true): defers global
	// diagnostics until the file's check, then appends the delta.
	std::vector<Diagnostic*> getBindAndCheckDiagnosticsWithChecker(
	    checker::Checker* fileChecker, SourceFile* sourceFile,
	    bool includeDeferredGlobals = false);
	// program.go:1618 getDeclarationDiagnosticsForFile — memoized
	// declaration diagnostics per file (SyncMap cache).
	std::vector<Diagnostic*> getDeclarationDiagnosticsForFile(
	    SourceFile* sourceFile);
	// getTypeCheckerForFileNonExclusive — program.go:607
	// GetTypeCheckerForFile for the ContextPtr (value-only) callers: the
	// built-in pool returns the file's checker without locking; an external
	// pool checks the checker out through the CheckerPool interface.
	std::pair<checker::Checker*, std::function<void()>>
	getTypeCheckerForFileNonExclusive(SourceFile* file);
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
	// program.go:616 GetTypeCheckerForFileExclusive — the built-in pool
	// locks the file's checker; the release callback unlocks it. A null
	// file takes the non-exclusive first checker (Go has no nil-file
	// caller; keeps the port's previous shared-checker semantics).
	std::pair<checker::Checker*, std::function<void()>>
	GetTypeCheckerForFileExclusive(SourceFile* file);
	// === slice: ls-coreC ===
	// program.go ContentMapperExtensions — extensions registered by the parsed
	// command line (empty when built through the plain ctor). GetTypeChecker /
	// GetGlobalTypingsCacheLocation already declared in the ls-autoimport block.
	std::vector<std::string> ContentMapperExtensions() override {
		return commandLine_ != nullptr ? commandLine_->ContentMapperExtensions()
									   : std::vector<std::string>{};
	}
	// === end slice: ls-coreC ===
	// program.go:211 GetParseFileRedirect — mapper lookup; the
	// project-reference source when useSourceOfProjectReference is on,
	// else the reference's declared output dts.
	std::string GetParseFileRedirect(const std::string& fileName) {
		return projectReferenceFileMapper_ != nullptr
		           ? projectReferenceFileMapper_->getParseFileRedirect(
		                 HasFileName{fileName, toPath(fileName)})
		           : std::string{};
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
	// === slice: api ===
	// program.go ModuleResolutionError (fileloader.go moduleResolutionError).
	// The C++ compiler slice does not yet populate it; the field exists so the
	// api slice's moduleResolutionError helper reads the same shape.
	gostd::Error ModuleResolutionError() const {
		return moduleResolutionError_;
	}
	void setModuleResolutionError(gostd::Error err) {
		moduleResolutionError_ = err;
	}
	// === end slice: api ===
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
	    const std::function<CheckerPool*(SimpleProgram*)>& createCheckerPool,
	    const std::function<module::Resolver*(const module::ResolverOptions&)>&
	        createModuleResolver);
	// program.go statistics helpers — aggregate counts across files.
	// LineCount — program.go (statistics.go statisticsFromProgram).
	int LineCount() const;
	int IdentifierCount() const;
	int SymbolCount() const;
	uint32_t TypeCount();
	uint64_t InstantiationCount();

	// === slice: project ===
	// program.go:113-117 — lazily-computed program state shared across a
	// ReuseProgram via lazyValue::tryReuse.
	lazyValue<collections::Set<std::string>> unresolvedImports;
	lazyValue<symlinks::KnownSymlinks> knownSymlinks;
	// (packageNames_ is the lazyValue in the ls-autoimport block below.)

	// ReuseProgram helpers — program.go:435-509.
	void initCheckerPool();
	bool canReplaceFileInProgram(SourceFile* file1, SourceFile* file2);
	bool needsImportHelpersImportSpecifier(SourceFile* file);
	// program.go:486 jsxRuntimeImportSpecifier — renamed so the member
	// doesn't collide with the jsxRuntimeImportSpecifier struct name.
	std::string jsxRuntimeImportSpecifierForFile(SourceFile* file);
	// program.go:542 / :555 — extractUnresolvedImports(+FromSourceFile).
	collections::Set<std::string>* extractUnresolvedImports();
	std::vector<std::string> extractUnresolvedImportsFromSourceFile(
	    SourceFile* file);
	// program.go:2300 GetSymlinkCache — lazily built via knownSymlinks
	// (from resolutions + package.json dependency probes).
	symlinks::KnownSymlinks* GetSymlinkCache() override;
	// program.go:2357-2371 — forEachResolution over resolvedModules /
	// typeResolutionsInFile; file == nullptr walks every file's cache.
	template <typename R>
	void forEachResolution(
	    std::unordered_map<tspath::Path, module::ModeAwareCache<R*>>&
	        resolutions,
	    const std::function<void(R*, std::string_view, ResolutionMode,
	                             tspath::Path)>& callback,
	    SourceFile* file) {
		if (file != nullptr) {
			auto it = resolutions.find(file->Path());
			if (it != resolutions.end()) {
				for (auto& [key, resolution] : it->second) {
					callback(resolution, key.Name, key.Mode,
					         file->Path());
				}
			}
			return;
		}
		for (auto& [filePath, resolutionsInFile] : resolutions) {
			for (auto& [key, resolution] : resolutionsInFile) {
				callback(resolution, key.Name, key.Mode, filePath);
			}
		}
	}
	void ForEachResolvedModule(
	    const std::function<void(module::ResolvedModule*, std::string_view,
	                             ResolutionMode, tspath::Path)>& callback,
	    SourceFile* file) {
		forEachResolution(resolvedModules, callback, file);
	}
	void ForEachResolvedTypeReferenceDirective(
	    const std::function<void(module::ResolvedTypeReferenceDirective*,
	                             std::string_view, ResolutionMode,
	                             tspath::Path)>& callback,
	    SourceFile* file) {
		forEachResolution(typeResolutionsInFile, callback, file);
	}

	// Go's *tsoptions.ParsedCommandLine satisfies both checker::
	// RedirectInfo and checker::ProjectReference structurally; the C++
	// interfaces are distinct types (multiple-inheritance of both would
	// make CompilerOptions() ambiguous), so each resolved config gets
	// one cached pair of adapters.
	struct parsedCommandLineAdapter {
		struct redirectInfoAdapter : checker::RedirectInfo {
			tsoptions::ParsedCommandLine* ref{};
			std::string CommonSourceDirectory() override {
				return ref->CommonSourceDirectory();
			}
			// `const struct CompilerOptions*` — the member name
			// hides the struct in class scope.
			const struct CompilerOptions* CompilerOptions() override {
				return ref->CompilerOptions();
			}
		};
		struct projectReferenceAdapter : checker::ProjectReference {
			tsoptions::ParsedCommandLine* ref{};
			const struct CompilerOptions* CompilerOptions() override {
				return ref->CompilerOptions();
			}
		};
		tsoptions::ParsedCommandLine* ref{};
		redirectInfoAdapter redirectInfo;
		projectReferenceAdapter projectRef;
	};
	std::deque<parsedCommandLineAdapter> parsedCommandLineAdapters_;
	std::unordered_map<tsoptions::ParsedCommandLine*,
	                   parsedCommandLineAdapter*>
	    parsedCommandLineAdapterIndex_;
	// tsoptions -> checker SourceOutputAndProjectReference
	// materializations (node-stable for the returned pointers).
	std::unordered_map<tsoptions::SourceOutputAndProjectReference*,
	                   checker::SourceOutputAndProjectReference>
	    checkerRefCache_;

	parsedCommandLineAdapter* adapterFor(
	    tsoptions::ParsedCommandLine* ref);
	checker::SourceOutputAndProjectReference* toCheckerRef(
	    tsoptions::SourceOutputAndProjectReference* ref);

	// program.go:189-206 — project-reference lookups (mapper delegates).
	checker::SourceOutputAndProjectReference* GetProjectReferenceFromSource(
	    const tspath::Path& path) override;
	const checker::SourceOutputAndProjectReference*
	GetProjectReferenceFromOutputDts(const std::string& path) override;
	checker::RedirectInfo* GetRedirectForResolution(
	    SourceFile* file) override;
	// program.go:202 GetResolvedProjectReferenceFor.
	std::pair<tsoptions::ParsedCommandLine*, bool>
	GetResolvedProjectReferenceFor(const tspath::Path& path);
	// program.go:181 GetSourceOfProjectReferenceIfOutputIncluded — the
	// HasFileName twin; the SourceFile override reduces to it.
	std::string GetSourceOfProjectReferenceIfOutputIncluded(
	    const HasFileName& file);
	std::string GetSourceOfProjectReferenceIfOutputIncluded(
	    SourceFile* file) override {
		return GetSourceOfProjectReferenceIfOutputIncluded(
		    HasFileName{file->FileName(), file->Path()});
	}
	// === end slice: project ===
	// === end slice: execute-tsc ===
	// program.go:570 SingleThreaded — opts.SingleThreaded defaults to
	// options.SingleThreaded when unset.
	bool SingleThreaded() {
		return tristateIsTrue(opts_.SingleThreaded != Tristate::Unknown
		                          ? opts_.SingleThreaded
		                          : options.SingleThreaded);
	}
	// program.go:804 GetSemanticDiagnosticsForIncremental — per-file
	// bind+check diagnostics with deferred globals, sorted/deduped.
	// === slice: api ===
	// fileloader.go moduleResolutionError — see ModuleResolutionError above.
	gostd::Error moduleResolutionError_;
	// === end slice: api ===
	std::vector<std::pair<SourceFile*, std::vector<Diagnostic*>>>
	GetSemanticDiagnosticsForIncremental(
	    const std::vector<SourceFile*>& sourceFiles);
	// === end slice: incremental ===

	// === slice: ls-autoimport ===
	// program.go:79 packageNamesInfo + collectPackageNames (:2226) —
	// computed lazily like Go's lazyValue; the lazyValue wrapper shares
	// the computed info across a ReuseProgram.
	struct packageNamesInfo {
		collections::Set<std::string> resolved;
		collections::Set<std::string> unresolved;
		collections::Set<std::string> deepImportPackages;
	};
	lazyValue<packageNamesInfo> packageNames_;
	packageNamesInfo* collectPackageNames();
	collections::Set<std::string>* ResolvedPackageNames() {
		return &collectPackageNames()->resolved;
	}
	collections::Set<std::string>* UnresolvedPackageNames() {
		return &collectPackageNames()->unresolved;
	}
	collections::Set<std::string>* DeepImportPackageNames() {
		return &collectPackageNames()->deepImportPackages;
	}
	// program.go:1785 IsGlobalTypingsFile.
	bool IsGlobalTypingsFile(const std::string& fileName) const;
	// program.go:143 GetGlobalTypingsCacheLocation — opts.TypingsLocation.
	std::string GetGlobalTypingsCacheLocation() override {
		return opts_.TypingsLocation;
	}
	// program.go:215 GetResolvedProjectReferences — the root config's
	// resolved references in reference order (mapper delegate).
	std::vector<tsoptions::ParsedCommandLine*>
	GetResolvedProjectReferences() {
		return projectReferenceFileMapper_ != nullptr
		           ? projectReferenceFileMapper_
		                 ->getResolvedProjectReferences()
		           : std::vector<tsoptions::ParsedCommandLine*>{};
	}
	// program.go:590 GetTypeChecker — the built-in pool's non-exclusive
	// first checker; an external pool checks one out via GetChecker.
	std::pair<checker::Checker*, std::function<void()>> GetTypeChecker(
		gostd::Context ctx);
	// === end slice: ls-autoimport ===

	// === slice: testrunner ===
	// program.go:517 GetContentMapper — the content mapper that produced
	// the given source file, or nullptr when the file was not produced by
	// a content mapper.
	contentmapper::Mapper* GetContentMapper(SourceFile* file);
	// program.go:597 ForEachCheckerParallel — grouped parallel iteration
	// over the built-in pool (no-op under an external pool, like Go).
	void ForEachCheckerParallel(
	    const std::function<void(int, checker::Checker*)>& cb) const;
	// === end slice: testrunner ===

	// === slice: project ===
	// program.go:85-92 checkerPool / compilerCheckerPool — checkerPool is
	// whichever pool initCheckerPool installed (built-in or the external
	// pool opts_.CreateCheckerPool returned); compilerCheckerPool is set
	// ONLY for the built-in pool (Go program.go:443-445).
	CheckerPool* checkerPool_ = nullptr;
	checkerPool* compilerCheckerPool_ = nullptr;
	std::unique_ptr<checkerPool> ownedCheckerPool_;
	CheckerPool* GetCheckerPool() { return checkerPool_; }
	// program.go:305 UpdateProgram — ReuseProgram fast path, else a
	// fresh program with the updated host/factories.
	std::tuple<SimpleProgram*, SourceFile*, bool> UpdateProgram(
	    const tspath::Path& changedFilePath, CompilerHost* newHost,
	    const std::function<CheckerPool*(SimpleProgram*)>& createCheckerPool,
	    const std::function<module::Resolver*(
	        const module::ResolverOptions&)>& createModuleResolver);
	// program.go:2095 HasSameFileNames — FileName equality over
	// filesByPath plus redirectFilesByPath equality.
	bool HasSameFileNames(SimpleProgram* other);
	// program.go:512 DuplicateSourceFiles — the parsed-but-deduplicated
	// source files a snapshot must release. Returns a view of the shared
	// storage (callers only iterate).
	std::vector<DuplicateSourceFile*> DuplicateSourceFiles() {
		std::vector<DuplicateSourceFile*> out;
		out.reserve(duplicateSourceFiles.size());
		for (auto& d : duplicateSourceFiles) {
			out.push_back(d.get());
		}
		return out;
	}
	// program.go:538 GetUnresolvedImports — the lazyValue
	// unresolvedImports set (shared across a ReuseProgram).
	collections::Set<std::string>* GetUnresolvedImports() {
		return unresolvedImports.getValue(
		    [this]() { return extractUnresolvedImports(); });
	}
	// program.go:219 RangeResolvedProjectReference — mapper delegate.
	bool RangeResolvedProjectReference(
	    const std::function<bool(tspath::Path,
	                             tsoptions::ParsedCommandLine*,
	                             tsoptions::ParsedCommandLine*, int)>& f) {
		return projectReferenceFileMapper_->rangeResolvedProjectReference(
		    f);
	}
	// program.go:223 RangeResolvedProjectReferenceInChildConfig — mapper
	// delegate.
	bool RangeResolvedProjectReferenceInChildConfig(
	    tsoptions::ParsedCommandLine* childConfig,
	    const std::function<bool(tspath::Path,
	                             tsoptions::ParsedCommandLine*,
	                             tsoptions::ParsedCommandLine*, int)>& f) {
		return projectReferenceFileMapper_
		    ->rangeResolvedProjectReferenceInChildConfig(childConfig, f);
	}
	// === end slice: project ===
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

// fileloader.go:630 ContentMapperProjectDiagnostic — fileless diagnostic
// for project setup or mapper initialization.
Diagnostic* ContentMapperProjectDiagnostic(const gostd::Error& err);

}  // namespace tsc::compiler
