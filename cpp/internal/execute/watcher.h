#pragma once

// watcher.go — port of tsc/internal/execute/watcher.go: the watch-mode
// driver. Tracks fs events through WatchManager, re-parses tsconfig on
// change, and either reuses the program via the single-file fast path or
// performs a full rebuild.

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/execute/incremental/incremental.h"
#include "internal/execute/tsc/compile.h"
#include "internal/execute/tsc/diagnostics.h"
#include "internal/execute/tsc/extendedconfigcache.h"
#include "internal/execute/watchmanager/watchmanager.h"
#include "internal/gostd/gostd.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/vfs/vfs.h"

namespace tsc::execute {

// cachedSourceFile — watcher.go:26.
struct cachedSourceFile {
	SourceFile* file = nullptr;
	vfs::TimePoint modTime{};
};

// watchCompilerHost — watcher.go:31. Adds an mtime-keyed source file cache on
// top of CompilerHost::GetSourceFile so the fast path only reparses files the
// watcher actually saw change.
class watchCompilerHost : public compiler::CompilerHost {
public:
	collections::SyncMap<tspath::Path, cachedSourceFile*>* cache =
	    nullptr;

	SourceFile* GetSourceFile(const SourceFileParseOptions& opts) override;
};

// Watcher — watcher.go:56. Concrete watch-mode driver; implements
// tsc.Watcher (DoCycle).
class Watcher : public tsc::Watcher {
public:
	tsc::System* sys = nullptr;
	std::string configFileName;
	tsoptions::ParsedCommandLine* config = nullptr;
	const CompilerOptions* compilerOptionsFromCommandLine = nullptr;
	tsoptions::JsonObjectPtr commandLineRaw;
	tsc::DiagnosticReporter reportDiagnostic;
	tsc::DiagnosticsReporter reportErrorSummary;
	tsc::DiagnosticReporter reportWatchStatus;
	tsc::CommandLineTesting* testing = nullptr;

	// contentMapperHost transforms content-mapped files; it is created once
	// per watch session (when enabled) and reused across cycles. It closes
	// itself when the session context is cancelled (see contentmapper.New).
	std::shared_ptr<contentmapper::Host> contentMapperHost;
	std::shared_ptr<contentmapper::Project> contentMapperProject;

	incremental::Program* program = nullptr;
	tsc::ExtendedConfigCache* extendedConfigCache = nullptr;
	bool configModified = false;
	bool configHasErrors = false;
	std::vector<std::string> configFilePaths;

	std::unique_ptr<collections::SyncMap<tspath::Path, cachedSourceFile*>>
	    sourceFileCache;

	watchmanager::WatchManager* wm = nullptr;
	std::unique_ptr<collections::Set<tspath::Path>>
	    seenFiles; // all build dependencies (for event filtering)
	std::unordered_map<std::string, vfs::TimePoint> configMtimes;
	bool watchSetDirty = false;
	// forceFullRebuild records a reason that requires a full NewProgram
	// rebuild (e.g. an event overflow, a mid-cycle watch failure, a newly
	// appeared project file, or a changed non-source dependency). Unlike
	// watchSetDirty, which is only raised to recheck wildcard roots and may
	// be cleared once the file set is confirmed unchanged, this flag is
	// preserved until a full rebuild actually runs so the single-file fast
	// path cannot silently reuse a stale program.
	bool forceFullRebuild = false;
	bool programReady = false;

	// Test-only observability of which build path was taken.
	int fastPathBuilds = 0;
	int fullBuilds = 0;

	// start — watcher.go:124.
	void start(gostd::Context ctx);
	// replaceContentMapperProject — watcher.go:155.
	void replaceContentMapperProject(tsoptions::ParsedCommandLine* config);
	// contentMapperWatchedFiles — watcher.go:171.
	std::vector<std::string> contentMapperWatchedFiles();
	// computeDesiredWatches — watcher.go:189.
	std::unordered_map<std::string, bool> computeDesiredWatches(
	    const std::vector<std::string>& seenFilePaths);
	// reconcileWatches — watcher.go:246.
	gostd::Error reconcileWatches(
	    const std::vector<std::string>& seenFilePaths);
	// comparePathsOptions — watcher.go:251.
	tspath::ComparePathsOptions comparePathsOptions() const;
	// DoCycle — watcher.go:258.
	void DoCycle() override;
	// isRelevantChange — watcher.go:372.
	bool isRelevantChange(
	    const std::unordered_map<std::string, fswatch::EventKind>&
	        changedPaths);
	// doBuild — watcher.go:400.
	gostd::Error doBuild();
	// tryUpdateProgram — watcher.go:536.
	bool tryUpdateProgram(watchCompilerHost* host);
	// FastPathBuilds — watcher.go:568.
	int FastPathBuilds() const { return fastPathBuilds; }
	// FullBuilds — watcher.go:572.
	int FullBuilds() const { return fullBuilds; }
	// evictChangedSourceFiles — watcher.go:592.
	void evictChangedSourceFiles(
	    const std::unordered_map<std::string, fswatch::EventKind>&
	        changedPaths);
	// compileAndEmit — watcher.go:607.
	tsc::CompileAndEmitResult compileAndEmit();
	// contentMapperManifestChanged — watcher.go:622.
	bool contentMapperManifestChanged(
	    const std::unordered_map<std::string, fswatch::EventKind>&
	        changedPaths);
	// recheckTsConfig — watcher.go:635.
	bool recheckTsConfig(bool force);
	// parseConfigFile — watcher.go:669.
	tsoptions::ParsedCommandLine* parseConfigFile();
};

// equalJSXImplicitImport — watcher.go:574.
bool equalJSXImplicitImport(const CompilerOptions* options,
                            SourceFile* oldFile, SourceFile* newFile);

}  // namespace tsc::execute
