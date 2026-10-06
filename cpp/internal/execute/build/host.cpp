// host.cpp — port of tsc/internal/execute/build/host.go.
#include "internal/execute/build/build.h"
#include "internal/tsoptions/tsoptions.h"

namespace tsc::execute::build {

// host.go:38 FS — Go returns h.host.FS() (vfs.FS); the C++ incremental::Host
// adaptation returns the compiler host (declared inline in build.h).

// host.go:42 DefaultLibraryPath — inline in build.h.
// host.go:46 GetCurrentDirectory — inline in build.h.

// host.go:50 Trace — this host does not support tracing.
void host::Trace(const DiagnosticMessage* msg,
                 const std::vector<std::string>& args) {
	TSC_UNREACHABLE(
	    "build.Orchestrator.host does not support tracing; use a different "
	    "host for tracing");
}

// host.go:54 GetSourceFile.
SourceFile* host::GetSourceFile(const SourceFileParseOptions& opts) {
	if (tspath::isDeclarationFileName(opts.FileName) ||
	    tspath::fileExtensionIs(opts.FileName, tspath::extensionJson)) {
		// Cache dts and json files as they will be reused
		return sourceFiles.loadOrStore(
		    opts,
		    [this](const SourceFileParseOptions& o) {
			    return host_->GetSourceFile(o, {});
		    },
		    false /* allowZero */);
	}
	return host_->GetSourceFile(opts, {});
}

// host.go:62 GetContentMappedSourceFiles.
std::pair<contentmapper::SourceFiles, gostd::Error>
host::GetContentMappedSourceFiles(
    const SourceFileParseOptions& parseOptions, contentmapper::Mapper* mapper) {
	return {contentmapper::SourceFiles{}, contentmapper::ErrProjectUnavailable};
}

// host.go:66 ContentMapperProject.
contentmapper::Project* host::ContentMapperProject() {
	TSC_UNREACHABLE(
	    "build.Orchestrator.host does not support content mapper project; "
	    "use an individual project's compiler host instead");
}

// host.go:70 GetResolvedProjectReference.
tsoptions::ParsedCommandLine*
host::GetResolvedProjectReference(const std::string& fileName,
                                  const tspath::Path& path) {
	return resolvedReferences.loadOrStore(
	    path,
	    [this, &fileName](const tspath::Path& p)
	        -> tsoptions::ParsedCommandLine* {
		    auto configStart = orchestrator->opts.Sys->Now();
		    // Wrap command line options in "compilerOptions" key to match
		    // tsconfig.json structure
		    tsoptions::JsonObjectPtr commandLineRaw;
		    if (auto* raw = orchestrator->opts.Command->Raw
		            .get<tsoptions::JsonObjectPtr>();
		    raw != nullptr) {
			    auto wrapped = std::make_shared<tsoptions::JsonObject>();
			    wrapped->Set("compilerOptions", *raw);
			    commandLineRaw = wrapped;
		    }
		    auto [commandLine, _] =
		        tsoptions::GetParsedCommandLineOfConfigFilePath(
		            fileName, p,
		            orchestrator->opts.Command->CompilerOptions,
		            commandLineRaw, host_, extendedConfigCache.get());
		    auto configTime = std::chrono::duration_cast<gostd::Duration>(
		        orchestrator->opts.Sys->Now() - configStart);
		    configTimes.Store(p, configTime);
		    return commandLine;
	    },
	    true /* allowZero */);
}

// host.go:87 ReadBuildInfo.
incremental::BuildInfo*
host::ReadBuildInfo(tsoptions::ParsedCommandLine* config) {
	auto configPath = orchestrator->toPath(config->ConfigName());
	auto* task = orchestrator->getTask(configPath);
	auto [buildInfo, _] =
	    task->loadOrStoreBuildInfo(orchestrator, configPath,
	                               config->GetBuildInfoFileName());
	return buildInfo;
}

// host.go:94 GetMTime — inline in build.h.

// host.go:98 SetMTime.
std::optional<std::string>
host::SetMTime(const std::string& file, std::filesystem::file_time_type mTime) {
	if (auto err = host_->fs->Chtimes(
	        file, vfs::TimePoint{},
	        std::chrono::file_clock::to_sys(mTime))) {
		return err.str();
	}
	return std::nullopt;
}

// host.go:102 loadOrStoreMTime.
fileTime host::loadOrStoreMTime(
    const std::string& file,
    collections::SyncMap<tspath::Path, fileTime>* oldCache, bool store) {
	auto path = orchestrator->toPath(file);
	if (auto [existing, loaded] = mTimes->Load(path); loaded) {
		return existing;
	}
	bool found = false;
	fileTime mTime{};
	if (oldCache != nullptr) {
		auto [v, ok] = oldCache->Load(path);
		mTime = v;
		found = ok;
	}
	if (!found) {
		mTime = incremental::GetMTime(host_, file);
	}
	if (store) {
		mTime = mTimes->LoadOrStore(path, mTime).first;
	}
	return mTime;
}

// host.go:121 storeMTime.
void host::storeMTime(const std::string& file, fileTime mTime) {
	auto path = orchestrator->toPath(file);
	mTimes->Store(path, mTime);
}

// host.go:126 storeMTimeFromOldCache.
void host::storeMTimeFromOldCache(
    const std::string& file,
    collections::SyncMap<tspath::Path, fileTime>* oldCache) {
	auto path = orchestrator->toPath(file);
	if (auto [mTime, found] = oldCache->Load(path); found) {
		mTimes->Store(path, mTime);
	}
}

} // namespace tsc::execute::build
