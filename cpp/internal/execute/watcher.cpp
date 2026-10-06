// watcher.go — port of tsc/internal/execute/watcher.go.

#include "internal/execute/watcher.h"

#include <algorithm>
#include <ostream>

#include "internal/execute/execute.h"
#include "internal/execute/tsc/emit.h"
#include "internal/compiler/program.h"
#include "internal/vfs/cachedvfs/cachedvfs.h"
#include "internal/vfs/trackingvfs/trackingvfs.h"

namespace tsc::execute {

namespace {

// core.go:80 Map.
template <typename T, typename U>
std::vector<U> mapSlice(const std::vector<T>& slice,
                        const std::function<U(const T&)>& f) {
	std::vector<U> result;
	result.reserve(slice.size());
	for (const auto& item : slice) {
		result.push_back(f(item));
	}
	return result;
}

// watchManager lock guard — Go `w.wm.Lock(); defer w.wm.Unlock()`.
struct wmLockGuard {
	watchmanager::WatchManager* wm;
	explicit wmLockGuard(watchmanager::WatchManager* wm) : wm(wm) {
		wm->Lock();
	}
	~wmLockGuard() { wm->Unlock(); }
	wmLockGuard(const wmLockGuard&) = delete;
	wmLockGuard& operator=(const wmLockGuard&) = delete;
};

// --- reflect.DeepEqual helpers for ParsedOptions (watcher.go:663) ---

// watchOptionsEqual — reflect.DeepEqual on *WatchOptions.
bool watchOptionsEqual(const WatchOptions* a, const WatchOptions* b) {
	if (a == b) {
		return true;
	}
	if (a == nullptr || b == nullptr) {
		return false;
	}
	return intPtrEqual(a->Interval, b->Interval) &&
	       a->FileKind == b->FileKind &&
	       a->DirectoryKind == b->DirectoryKind &&
	       a->FallbackPolling == b->FallbackPolling &&
	       a->SyncWatchDir == b->SyncWatchDir &&
	       a->ExcludeDir == b->ExcludeDir &&
	       a->ExcludeFiles == b->ExcludeFiles;
}

// projectReferencesEqual — DeepEqual on []ProjectReference.
bool projectReferencesEqual(const std::vector<ProjectReference*>& a,
                            const std::vector<ProjectReference*>& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); i++) {
		if ((a[i] == nullptr) != (b[i] == nullptr)) {
			return false;
		}
		if (a[i] != nullptr &&
		    (a[i]->Path != b[i]->Path ||
		     a[i]->OriginalPath != b[i]->OriginalPath ||
		     a[i]->Circular != b[i]->Circular)) {
			return false;
		}
	}
	return true;
}

// mapperManifestEqual — DeepEqual on contentmapper.Manifest.
bool mapperManifestEqual(const contentmapper::Manifest& a,
                         const contentmapper::Manifest& b) {
	return a.Name == b.Name && a.Version == b.Version && a.Exec == b.Exec &&
	       a.CompilerOptions == b.CompilerOptions &&
	       a.DynamicConfig == b.DynamicConfig;
}

// typeAcquisitionEqual — DeepEqual on *TypeAcquisition (nil-safe).
bool typeAcquisitionEqual(const TypeAcquisition* a,
                          const TypeAcquisition* b) {
	if (a == nullptr || b == nullptr) {
		return a == b;
	}
	return a->Equals(b);
}

// contentMappersEqual — DeepEqual on []contentmapper.Mapper.
bool contentMappersEqual(
    const std::vector<contentmapper::Mapper*>& a,
    const std::vector<contentmapper::Mapper*>& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); i++) {
		if ((a[i] == nullptr) != (b[i] == nullptr)) {
			return false;
		}
		if (a[i] == nullptr) {
			continue;
		}
		const auto& am = a[i];
		const auto& bm = b[i];
		if (am->Definition.Package != bm->Definition.Package ||
		    am->Definition.Extensions != bm->Definition.Extensions ||
		    am->Definition.Options != bm->Definition.Options ||
		    am->PackageDirectory != bm->PackageDirectory ||
		    am->ContributionID != bm->ContributionID ||
		    !mapperManifestEqual(am->Manifest, bm->Manifest)) {
			return false;
		}
	}
	return true;
}

// parsedOptionsDeepEqual — reflect.DeepEqual on *tsoptions.ParsedOptions
// (watcher.go:663). Note: TypeAcquisition has its own Equals covering the
// same fields.
bool parsedOptionsDeepEqual(const tsoptions::ParsedOptions* a,
                            const tsoptions::ParsedOptions* b) {
	if (a == b) {
		return true;
	}
	if (a == nullptr || b == nullptr) {
		return false;
	}
	return compilerOptionsDeepEqual(a->CompilerOptions, b->CompilerOptions) &&
	       watchOptionsEqual(a->WatchOptions, b->WatchOptions) &&
	       typeAcquisitionEqual(a->TypeAcquisition, b->TypeAcquisition) &&
	       a->FileNames == b->FileNames &&
	       projectReferencesEqual(a->ProjectReferences,
	                              b->ProjectReferences) &&
	       contentMappersEqual(a->ContentMappers, b->ContentMappers);
}

}  // namespace

// watchCompilerHost.GetSourceFile — watcher.go:36.
SourceFile* watchCompilerHost::GetSourceFile(
    const SourceFileParseOptions& opts) {
	auto info = FS()->Stat(opts.FileName);

	auto [cached, ok] = cache->Load(opts.Path);
	if (ok) {
		if (info != nullptr && info->ModTime() == cached->modTime) {
			return cached->file;
		}
	}

	auto* file = compiler::CompilerHost::GetSourceFile(opts);
	if (file != nullptr) {
		if (info != nullptr) {
			cache->Store(opts.Path, new cachedSourceFile{
			                         .file = file,
			                         .modTime = info->ModTime(),
			                     });
		}
	} else {
		cache->Delete(opts.Path);
	}
	return file;
}

// createWatcher — watcher.go:99.
Watcher* createWatcher(
    tsc::System* sys, tsoptions::ParsedCommandLine* configParseResult,
    const CompilerOptions* compilerOptionsFromCommandLine,
    const tsoptions::JsonObjectPtr& commandLineRaw,
    const tsc::DiagnosticReporter& reportDiagnostic,
    const tsc::DiagnosticsReporter& reportErrorSummary,
    tsc::CommandLineTesting* testing) {
	auto* wmp = new watchmanager::WatchManager(
	    sys->Writer(), [sys](const std::string& dir) {
		    return sys->fs()->DirectoryExists(dir);
	    });
	if (auto* t =
	        dynamic_cast<watchmanager::CommandLineTestingWithWatchBackend*>(
	            testing);
	    t != nullptr) {
		wmp->SetBackend(t->WatchBackend());
	}
	auto* w = new Watcher;
	w->sys = sys;
	w->config = configParseResult;
	w->compilerOptionsFromCommandLine = compilerOptionsFromCommandLine;
	w->commandLineRaw = commandLineRaw;
	w->reportDiagnostic = reportDiagnostic;
	w->reportErrorSummary = reportErrorSummary;
	w->reportWatchStatus = tsc::CreateWatchStatusReporter(
	    sys, configParseResult->Locale(),
	    configParseResult->CompilerOptions(), testing);
	w->testing = testing;
	w->sourceFileCache = std::make_unique<
	    collections::SyncMap<tspath::Path, cachedSourceFile*>>();
	w->wm = wmp;
	if (configParseResult->ConfigFile != nullptr) {
		w->configFileName =
		    configParseResult->ConfigFile->SourceFile->FileName();
	}
	return w;
}

// start — watcher.go:124.
void Watcher::start(gostd::Context ctx) {
	contentMapperHost =
	    tsc::NewContentMapperHost(ctx, sys, config->CompilerOptions());
	// Go: `if w.contentMapperHost != nil && w.testing == nil { defer
	// w.contentMapperHost.Close() }` — deferred to the end of start(), i.e.
	// after RunLoop returns.
	replaceContentMapperProject(config);
	wm->Lock();
	extendedConfigCache = new tsc::ExtendedConfigCache();
	auto* host = compiler::NewCompilerHost(
	    sys->GetCurrentDirectory(), sys->fs(), sys->DefaultLibraryPath(),
	    extendedConfigCache,
	    getTraceFromSys(sys, config->Locale(), testing),
	    contentMapperProject);
	program = incremental::ReadBuildInfoProgram(
	    config, incremental::NewBuildInfoReader(host), host);

	if (!configFileName.empty()) {
		configFilePaths.push_back(configFileName);
		for (auto& f : config->ExtendedSourceFiles()) {
			configFilePaths.push_back(f);
		}
	}

	if (auto [value, _] = sys->GetEnvironmentVariable("TS_WATCH_DEBUG");
	    !value.empty()) {
		wm->DebugLog = sys->Writer();
	}

	if (testing == nullptr) {
		wm->EnsureDefaultBackend();
	}

	reportWatchStatus(tsoptions::newCompilerDiagnostic(
	    Starting_compilation_in_watch_mode));
	watchSetDirty = true;
	auto buildErr = doBuild();
	if (buildErr) {
		wm->ForceOverflow();
	}
	wm->Unlock();

	if (testing == nullptr) {
		// The content mapper host closes itself when ctx is cancelled (see
		// contentmapper.New).
		wm->RunLoop(ctx, [this] { DoCycle(); });
	}
	if (contentMapperHost != nullptr && testing == nullptr) {
		// Go `defer w.contentMapperHost.Close()`.
		contentMapperHost->Close();
	}
}

// replaceContentMapperProject — watcher.go:155.
void Watcher::replaceContentMapperProject(
    tsoptions::ParsedCommandLine* config) {
	if (contentMapperHost == nullptr) {
		return;
	}
	contentmapper::ProjectSpec spec;
	spec.ConfigFileName = config->ConfigName();
	spec.Mappers = config->ContentMappers();
	spec.CompilerOptions = config->CompilerOptions();
	auto project = contentMapperHost->Project(spec);
	if (contentMapperProject != nullptr) {
		contentMapperProject->Close();
	}
	contentMapperProject = project;
}

// contentMapperWatchedFiles — watcher.go:171.
std::vector<std::string> Watcher::contentMapperWatchedFiles() {
	std::vector<std::string> files;
	for (auto* mapper : config->ContentMappers()) {
		if (!mapper->PackageDirectory.empty() &&
		    mapper->ContributionID.empty()) {
			files.push_back(tspath::combinePaths(mapper->PackageDirectory, {"package.json"}));
		}
	}
	if (contentMapperProject != nullptr) {
		auto [dynamicFiles, err] = contentMapperProject->WatchedFiles();
		if (err) {
			reportDiagnostic(compiler::ContentMapperProjectDiagnostic(err));
			return files;
		}
		files.insert(files.end(), dynamicFiles.begin(), dynamicFiles.end());
	}
	std::sort(files.begin(), files.end());
	files.erase(std::unique(files.begin(), files.end()), files.end());
	return files;
}

// computeDesiredWatches — watcher.go:189.
std::unordered_map<std::string, bool> Watcher::computeDesiredWatches(
    const std::vector<std::string>& seenFilePaths) {
	auto cwd = sys->GetCurrentDirectory();

	std::unordered_map<std::string, bool> desiredDirs; // dir → recursive

	// Wildcard directories from tsconfig (recursive or non-recursive)
	if (config->ConfigFile != nullptr) {
		if (auto* dirs = config->WildcardDirectories(); dirs != nullptr) {
			for (auto& [dir, recursive] : *dirs) {
				auto realDir = sys->fs()->Realpath(dir);
				desiredDirs[realDir] = recursive;
			}
		}
	}

	// For no-config CLI mode, ensure CWD is watched
	if (config->ConfigFile == nullptr && desiredDirs.empty()) {
		auto dir = sys->fs()->Realpath(cwd);
		desiredDirs[dir] = false;
	}

	// Config file parent directories as non-recursive watches
	for (auto& cfgPath : configFilePaths) {
		auto realPath = sys->fs()->Realpath(cfgPath);
		auto dir = tspath::getDirectoryPath(realPath);
		if (desiredDirs.find(dir) == desiredDirs.end()) {
			desiredDirs[dir] = false;
		}
	}

	// For no-config CLI mode, also watch the CLI-specified files'
	// directories
	if (config->ConfigFile == nullptr) {
		for (auto& fileName : config->FileNames()) {
			auto absPath =
			    tspath::getNormalizedAbsolutePath(fileName, cwd);
			auto realPath = sys->fs()->Realpath(absPath);
			auto dir = tspath::getDirectoryPath(realPath);
			if (desiredDirs.find(dir) == desiredDirs.end()) {
				desiredDirs[dir] = false;
			}
		}
	}

	// Add parent directories for seen files not covered by existing dir
	// watches. Resolve ancestor fallbacks first so coverage checks use
	// final dirs.
	auto resolvedDirs = wm->ResolveDesiredDirs(desiredDirs);

	watchmanager::DirWatchSet coverage(comparePathsOptions());
	for (auto& [dir, recursive] : resolvedDirs) {
		coverage.Set(dir, recursive);
	}
	for (auto& filePath : seenFilePaths) {
		auto dir = tspath::getDirectoryPath(filePath);
		if (!coverage.Covered(dir) &&
		    watchmanager::CanWatchDirectory(dir)) {
			coverage.Set(dir, false);
		}
	}

	// Re-resolve in case newly added dirs don't exist
	return wm->ResolveDesiredDirs(coverage.Dirs());
}

// reconcileWatches — watcher.go:246.
gostd::Error Watcher::reconcileWatches(
    const std::vector<std::string>& seenFilePaths) {
	auto desiredDirs = computeDesiredWatches(seenFilePaths);
	return wm->ReconcileWatches(desiredDirs);
}

// comparePathsOptions — watcher.go:251.
tspath::ComparePathsOptions Watcher::comparePathsOptions() const {
	return {sys->fs()->UseCaseSensitiveFileNames(), sys->GetCurrentDirectory()};
}

// DoCycle — watcher.go:258.
void Watcher::DoCycle() {
	wmLockGuard guard(wm);

	auto [changedPaths, overflow] = wm->DrainEvents();
	bool hasEvents = !changedPaths.empty() || overflow;

	if (recheckTsConfig(contentMapperManifestChanged(changedPaths))) {
		return;
	}

	if (hasEvents && !overflow && !configModified) {
		// Filter fswatch events against known dependencies
		if (isRelevantChange(changedPaths)) {
			evictChangedSourceFiles(changedPaths);
			bool caseSensitive = sys->fs()->UseCaseSensitiveFileNames();
			auto cwd = sys->GetCurrentDirectory();
			auto& programFiles =
			    program->GetProgram()->FilesByPath();
			collections::Set<tspath::Path> cmWatchedFiles;
			cmWatchedFiles.AddRange(mapSlice<std::string, tspath::Path>(
			    contentMapperWatchedFiles(),
			    [&](const std::string& fileName) {
				    return tspath::toPath(fileName, cwd, caseSensitive);
			    }));
			bool contentMapperConfigChanged = false;
			for (auto& [eventPath, kind] : changedPaths) {
				if (sys->fs()->DirectoryExists(eventPath)) {
					// A watched directory changed: the wildcard file set
					// may have changed, so reload file names on the next
					// build.
					watchSetDirty = true;
					continue;
				}
				auto p = tspath::toPath(eventPath, cwd, caseSensitive);
				if (cmWatchedFiles.Has(p)) {
					contentMapperConfigChanged = true;
					forceFullRebuild = true;
				}
				if (config->ConfigFile != nullptr &&
				    config->PossiblyMatchesFileName(eventPath)) {
					if (!seenFiles->Has(p)) {
						// A file that matches the project but was not
						// previously seen appeared: a structural change
						// that requires a full rebuild, not the
						// single-file fast path.
						watchSetDirty = true;
						forceFullRebuild = true;
						continue;
					}
				}
				auto it = programFiles.find(p);
				if (it != programFiles.end() &&
				    !it->second->ContentMapper().empty()) {
					// Canonical mapped files must be transformed again,
					// and supplemental paths are failed physical lookups
					// reserved for virtual files. Neither can use
					// single-file AST reuse.
					forceFullRebuild = true;
				} else if (it == programFiles.end() &&
				           seenFiles->Has(p)) {
					// A non-source build dependency changed. Such
					// dependencies (e.g. package.json or a
					// previously-missing module path) are tracked in
					// seenFiles but are not program source files, so a
					// missing sourceFileCache entry would not account for
					// them. Module resolution may now differ, so the
					// single-file fast path is unsafe; force a full
					// rebuild.
					forceFullRebuild = true;
				}
			}
			if (contentMapperConfigChanged &&
			    contentMapperProject != nullptr) {
				if (auto err = contentMapperProject->Refresh(); err) {
					reportDiagnostic(tsoptions::newCompilerDiagnostic(
					    
					        The_content_mapper_process_could_not_be_started_or_initialized));
					return;
				}
			}
		} else {
			if (wm->DebugLog != nullptr) {
				*wm->DebugLog << "[watch] DoCycle: "
				              << changedPaths.size()
				              << " event(s) not relevant to compilation, "
				                 "skipping rebuild\n";
			}
			if (testing != nullptr) {
				testing->OnProgram(program);
			}
			return;
		}
	} else if (overflow) {
		// Overflow: evict the entire source file cache and force a full
		// rebuild. The fast path must not run here: after clearing the
		// cache a one-file program would present exactly one cache miss
		// and be misread as a single-file content edit, silently reusing
		// a stale (e.g. unresolved import) program instead of
		// rediscovering the file graph.
		sourceFileCache = std::make_unique<
		    collections::SyncMap<tspath::Path, cachedSourceFile*>>();
		watchSetDirty = true;
		forceFullRebuild = true;
	} else if (!hasEvents && !configModified) {
		// No events and no config change
		if (wm->DebugLog != nullptr) {
			*wm->DebugLog << "[watch] DoCycle: no events, skipping\n";
		}
		if (testing != nullptr) {
			testing->OnProgram(program);
		}
		return;
	}

	reportWatchStatus(tsoptions::newCompilerDiagnostic(
	    
	        File_change_detected_Starting_incremental_compilation));
	if (auto err = doBuild(); err) {
		// Mid-cycle watch failure; force a full rebuild on the next event
		wm->ForceOverflow();
	}
}

// isRelevantChange — watcher.go:372.
bool Watcher::isRelevantChange(
    const std::unordered_map<std::string, fswatch::EventKind>&
        changedPaths) {
	bool caseSensitive = sys->fs()->UseCaseSensitiveFileNames();
	auto cwd = sys->GetCurrentDirectory();
	auto opts = comparePathsOptions();
	collections::Set<tspath::Path> cmWatchedFiles;
	cmWatchedFiles.AddRange(mapSlice<std::string, tspath::Path>(
	    contentMapperWatchedFiles(), [&](const std::string& fileName) {
		    return tspath::toPath(fileName, cwd, caseSensitive);
	    }));
	for (auto& [eventPath, kind] : changedPaths) {
		auto p = tspath::toPath(eventPath, cwd, caseSensitive);
		if (cmWatchedFiles.Has(p)) {
			return true;
		}
		if (seenFiles != nullptr && seenFiles->Has(p)) {
			return true;
		}
		if (config->ConfigFile != nullptr &&
		    config->PossiblyMatchesFileName(eventPath)) {
			return true;
		}
		if (config->ConfigFile != nullptr &&
		    config->PossiblyMatchesDirectoryName(p)) {
			return true;
		}
		if (sys->fs()->DirectoryExists(eventPath)) {
			if (wm->IsPathUnderWatch(eventPath, opts)) {
				return true;
			}
		}
	}
	return false;
}

// doBuild — watcher.go:400.
gostd::Error Watcher::doBuild() {
	if (configModified) {
		sourceFileCache = std::make_unique<
		    collections::SyncMap<tspath::Path, cachedSourceFile*>>();
		watchSetDirty = true;
	}

	bool reloadedFileNames = false;
	if (watchSetDirty) {
		if (config->ConfigFile != nullptr &&
		    config->WildcardDirectories() != nullptr &&
		    !config->WildcardDirectories()->empty()) {
			auto* newConfig =
			    config->ReloadFileNamesOfParsedCommandLine(sys->FS());
			reloadedFileNames = true;
			if (config->FileNames() != newConfig->FileNames()) {
				config = newConfig;
			} else {
				watchSetDirty = false;
				config = newConfig;
			}
		} else if (!configModified) {
			watchSetDirty = false;
		}
	}

	if (program != nullptr && programReady && !configModified &&
	    !watchSetDirty && !forceFullRebuild) {
		auto cached = vfs::cachedvfs::From(sys->fs().get());
		auto* innerHost = compiler::NewCompilerHost(
		    sys->GetCurrentDirectory(), cached, sys->DefaultLibraryPath(),
		    extendedConfigCache,
		    getTraceFromSys(sys, config->Locale(), testing),
		    contentMapperProject);
		auto* host = new watchCompilerHost();
		*static_cast<compiler::CompilerHost*>(host) = *innerHost;
		host->cache = sourceFileCache.get();

		if (tryUpdateProgram(host)) {
			fastPathBuilds++;
			auto result = compileAndEmit();
			cached->DisableAndClearCache();

			configMtimes.clear();
			configMtimes.reserve(configFilePaths.size());
			for (auto& cfgPath : configFilePaths) {
				if (auto s = sys->fs()->Stat(cfgPath); s != nullptr) {
					configMtimes[cfgPath] = s->ModTime();
				}
			}
			configModified = false;

			size_t errorCount = result.Diagnostics.size();
			if (errorCount == 1) {
				reportWatchStatus(tsoptions::newCompilerDiagnostic(
				    
				        Found_1_error_Watching_for_file_changes));
			} else {
				reportWatchStatus(tsoptions::newCompilerDiagnostic(
				    
				        Found_0_errors_Watching_for_file_changes,
				    {std::to_string(errorCount)}));
			}
			if (testing != nullptr) {
				testing->OnProgram(program);
			}
			return nullptr;
		}
		cached->DisableAndClearCache();
	}

	// `tfs->Inner` is a raw pointer — keep `cached` alive for the host's
	// lifetime (the port deliberately leaks per-build host state).
	auto* cachedKeepAlive =
	    new std::shared_ptr<vfs::cachedvfs::FS>(vfs::cachedvfs::From(sys->fs().get()));
	auto& cached = *cachedKeepAlive;
	auto* tfs = new vfs::trackingvfs::FS(cached.get());
	auto* innerHost = compiler::NewCompilerHost(
	    sys->GetCurrentDirectory(),
	    std::shared_ptr<vfs::FS>(tfs), sys->DefaultLibraryPath(),
	    extendedConfigCache,
	    getTraceFromSys(sys, config->Locale(), testing),
	    contentMapperProject);
	auto* host = new watchCompilerHost();
	*static_cast<compiler::CompilerHost*>(host) = *innerHost;
	host->cache = sourceFileCache.get();

	if (config->ConfigFile != nullptr) {
		if (auto* dirs = config->WildcardDirectories(); dirs != nullptr) {
			for (auto& [dir, recursive] : *dirs) {
				tfs->SeenFiles.Add(dir);
			}
		}
		if (!reloadedFileNames && !watchSetDirty &&
		    config->WildcardDirectories() != nullptr &&
		    !config->WildcardDirectories()->empty()) {
			config =
			    config->ReloadFileNamesOfParsedCommandLine(sys->FS());
		}
	}
	for (auto& path : configFilePaths) {
		tfs->SeenFiles.Add(path);
	}
	for (auto& path : contentMapperWatchedFiles()) {
		tfs->SeenFiles.Add(path);
	}

	compiler::ProgramOptions programOptions;
	programOptions.Config = config;
	programOptions.Host = host;
	program = incremental::NewProgram(
	    compiler::NewProgram(programOptions), program, nullptr,
	    [this] { return sys->Now(); }, testing != nullptr);
	programReady = true;
	fullBuilds++;

	auto result = compileAndEmit();
	cached->DisableAndClearCache();

	bool caseSensitive = sys->fs()->UseCaseSensitiveFileNames();
	auto cwd = sys->GetCurrentDirectory();
	auto seenSlice = tfs->SeenFiles.ToSlice();
	seenFiles = std::make_unique<collections::Set<tspath::Path>>();
	for (auto& p : seenSlice) {
		seenFiles->Add(tspath::toPath(p, cwd, caseSensitive));
	}

	configMtimes.clear();
	configMtimes.reserve(configFilePaths.size());
	for (auto& cfgPath : configFilePaths) {
		if (auto s = sys->fs()->Stat(cfgPath); s != nullptr) {
			configMtimes[cfgPath] = s->ModTime();
		}
	}

	if (auto err = reconcileWatches(seenSlice); err) {
		*sys->Writer() << err->Error() << '\n';
		return err;
	}
	watchSetDirty = false;
	configModified = false;
	forceFullRebuild = false;

	auto& programFiles = program->GetProgram()->FilesByPath();
	sourceFileCache->Range([&](tspath::Path path, cachedSourceFile*) {
		if (programFiles.find(path) == programFiles.end()) {
			sourceFileCache->Delete(path);
		}
		return true;
	});

	size_t errorCount = result.Diagnostics.size();
	if (errorCount == 1) {
		reportWatchStatus(tsoptions::newCompilerDiagnostic(
		    Found_1_error_Watching_for_file_changes));
	} else {
		reportWatchStatus(tsoptions::newCompilerDiagnostic(
		    Found_0_errors_Watching_for_file_changes,
		    {std::to_string(errorCount)}));
	}

	if (testing != nullptr) {
		testing->OnProgram(program);
	}
	return nullptr;
}

// tryUpdateProgram — watcher.go:536.
bool Watcher::tryUpdateProgram(watchCompilerHost* host) {
	auto* oldProgram = program->GetProgram();

	tspath::Path changedPath;
	int changedCount = 0;
	for (auto& [path, file] : oldProgram->FilesByPath()) {
		if (!file->ContentMapper().empty()) {
			continue;
		}
		if (auto [cached, ok] = sourceFileCache->Load(path); !ok) {
			changedPath = path;
			changedCount++;
			if (changedCount > 1) {
				return false;
			}
		}
	}
	if (changedCount == 0) {
		return false;
	}

	if (auto* oldFile = [&]() -> SourceFile* {
		    auto it = oldProgram->FilesByPath().find(changedPath);
		    return it != oldProgram->FilesByPath().end() ? it->second
		                                                 : nullptr;
	    }();
	    oldFile != nullptr) {
		if (auto* newFile =
		        host->GetSourceFile(oldFile->ParseOptions());
		    newFile != nullptr) {
			if (!equalJSXImplicitImport(oldProgram->Options(), oldFile,
			                            newFile)) {
				return false;
			}
		}
	}

	auto [newProgram, _, reused] =
	    oldProgram->ReuseProgram(changedPath, host, nullptr, nullptr);
	if (reused) {
		program = incremental::NewProgram(
		    newProgram, program, nullptr,
		    [this] { return sys->Now(); }, testing != nullptr);
	}
	return reused;
}

// equalJSXImplicitImport — watcher.go:574.
bool equalJSXImplicitImport(const CompilerOptions* options,
                            SourceFile* oldFile, SourceFile* newFile) {
	auto isJSX = [](const SourceFile* file) {
		return file->ScriptKind == ScriptKind::JSX ||
		       file->ScriptKind == ScriptKind::TSX;
	};
	if (!isJSX(oldFile) && !isJSX(newFile)) {
		return true;
	}
	auto oldImport = getJSXRuntimeImport(
	    getJSXImplicitImportBase(options, oldFile), options);
	auto newImport = getJSXRuntimeImport(
	    getJSXImplicitImportBase(options, newFile), options);
	return oldImport == newImport;
}

// evictChangedSourceFiles — watcher.go:592.
void Watcher::evictChangedSourceFiles(
    const std::unordered_map<std::string, fswatch::EventKind>&
        changedPaths) {
	bool caseSensitive = sys->fs()->UseCaseSensitiveFileNames();
	auto cwd = sys->GetCurrentDirectory();
	for (auto& [eventPath, kind] : changedPaths) {
		auto p = tspath::toPath(eventPath, cwd, caseSensitive);
		if (auto [cached, ok] = sourceFileCache->Load(p); ok) {
			if (wm->DebugLog != nullptr) {
				*wm->DebugLog << "[watch] evicting cached source file: " << p
				              << '\n';
			}
			sourceFileCache->Delete(p);
		}
	}
}

// compileAndEmit — watcher.go:607.
tsc::CompileAndEmitResult Watcher::compileAndEmit() {
	tsc::EmitInput emitInput;
	emitInput.Sys = sys;
	emitInput.ProgramLike = program;
	emitInput.Program = program->GetProgram();
	emitInput.Config = config;
	emitInput.ReportDiagnostic = reportDiagnostic;
	emitInput.ReportErrorSummary = reportErrorSummary;
	emitInput.Writer = sys->Writer();
	emitInput.CompileTimes = new tsc::CompileTimes();
	emitInput.Testing = testing;
	return tsc::EmitFilesAndReportErrors(emitInput);
}

// contentMapperManifestChanged — watcher.go:622.
bool Watcher::contentMapperManifestChanged(
    const std::unordered_map<std::string, fswatch::EventKind>&
        changedPaths) {
	for (auto* mapper : config->ContentMappers()) {
		if (mapper->PackageDirectory.empty() ||
		    !mapper->ContributionID.empty()) {
			continue;
		}
		if (changedPaths.count(tspath::combinePaths(mapper->PackageDirectory, {"package.json"})) != 0) {
			return true;
		}
	}
	return false;
}

// recheckTsConfig — watcher.go:635.
bool Watcher::recheckTsConfig(bool force) {
	if (configFileName.empty()) {
		return false;
	}

	if (!force && !configHasErrors && !configFilePaths.empty()) {
		bool changed = false;
		for (auto& path : configFilePaths) {
			auto oldIt = configMtimes.find(path);
			auto s = sys->fs()->Stat(path);
			if (oldIt == configMtimes.end()) {
				if (s != nullptr) {
					changed = true;
					break;
				}
			} else if (s == nullptr || s->ModTime() != oldIt->second) {
				changed = true;
				break;
			}
		}
		if (!changed) {
			return false;
		}
	}

	auto* configParseResult = parseConfigFile();
	if (configParseResult == nullptr) {
		return true;
	}
	if (configHasErrors) {
		configModified = true;
	}
	configHasErrors = false;
	configFilePaths.clear();
	configFilePaths.push_back(configFileName);
	for (auto& f : configParseResult->ExtendedSourceFiles()) {
		configFilePaths.push_back(f);
	}
	if (!parsedOptionsDeepEqual(config->ParsedConfig,
	                            configParseResult->ParsedConfig)) {
		configModified = true;
	}
	replaceContentMapperProject(configParseResult);
	config = configParseResult;
	return false;
}

// parseConfigFile — watcher.go:669.
tsoptions::ParsedCommandLine* Watcher::parseConfigFile() {
	auto* extendedConfigCacheNew = new tsc::ExtendedConfigCache();
	auto [configParseResult, errors] =
	    tsoptions::GetParsedCommandLineOfConfigFile(
	        configFileName,
	        const_cast<CompilerOptions*>(compilerOptionsFromCommandLine),
	        commandLineRaw, sys, extendedConfigCacheNew);
	if (!errors.empty()) {
		for (auto* e : errors) {
			reportDiagnostic(e);
		}
		configHasErrors = true;
		size_t errorCount = errors.size();
		if (errorCount == 1) {
			reportWatchStatus(tsoptions::newCompilerDiagnostic(
			    Found_1_error_Watching_for_file_changes));
		} else {
			reportWatchStatus(tsoptions::newCompilerDiagnostic(
			    Found_0_errors_Watching_for_file_changes,
			    {std::to_string(errorCount)}));
		}
		return nullptr;
	}
	extendedConfigCache = extendedConfigCacheNew;
	return configParseResult;
}

}  // namespace tsc::execute
