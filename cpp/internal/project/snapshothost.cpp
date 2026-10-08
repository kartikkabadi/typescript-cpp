// snapshothost.go — SnapshotHost methods.
//
// === slice: project ===

#include "internal/project/snapshothost.h"

#include <algorithm>

#include "internal/project/logging/logging.h"
#include "internal/project/overlayfs.h"
#include "internal/project/session.h"
#include "internal/project/snapshot.h"
#include "internal/project/watch.h"
#include "internal/tspath/tspath.h"

namespace tsc::project {

// newContentMapperHost — session.go:197. Creates the session's shared
// content mapper host when the workspace is trusted and a spawner is
// available; otherwise it returns nil, and configured content mappers
// are rejected by the config-file gate.
std::shared_ptr<contentmapper::Host> newContentMapperHost(
    SessionInit* init) {
	if (!init->Options->RunExternalCode || init->Spawner == nullptr) {
		return nullptr;
	}
	auto diagnosticLocale = locale::Default;
	if (init->Client != nullptr) {
		diagnosticLocale = init->Client->GetLocale();
	}
	return contentmapper::NewHostWithOptions(
	    init->BackgroundCtx, init->Spawner, diagnosticLocale,
	    contentmapper::HostOptions{
	        .Logger = init->ContentMapperLogger != nullptr
	                      ? *init->ContentMapperLogger
	                      : contentmapper::Logger{}});
}

// AcquireSourceFile — snapshothost.go:49.
SourceFileLease* SnapshotHost::AcquireSourceFile(
    const SourceFileParseOptions& options, const std::string& text,
    ScriptKind scriptKind) {
	auto* fileHandle = newCachedFileHandle(options.FileName, text);
	auto key =
	    newParseCacheKey(options, fileHandle->Hash(), scriptKind);
	return new SourceFileLease{
	    .cache = parseCache,
	    .key = key,
	    .sourceFile = parseCache->Acquire(key, fileHandle),
	};
}

// AcquireExistingSourceFile — snapshothost.go:65.
SourceFileLease* SnapshotHost::AcquireExistingSourceFile(
    const ParseCacheKey& key) {
	auto [sourceFile, ok] = parseCache->AcquireExisting(key);
	if (!ok) {
		return nullptr;
	}
	return new SourceFileLease{
	    .cache = parseCache,
	    .key = key,
	    .sourceFile = sourceFile,
	};
}

// NewSnapshotHost — snapshothost.go:64.
SnapshotHost* NewSnapshotHost(SessionInit* init) {
	auto currentDirectory = init->Options->CurrentDirectory;
	auto useCaseSensitiveFileNames =
	    init->FS->UseCaseSensitiveFileNames();
	auto toPath = [currentDirectory,
	               useCaseSensitiveFileNames](
	                  const std::string& fileName) -> tspath::Path {
		return tspath::toPath(fileName, currentDirectory,
		                      useCaseSensitiveFileNames);
	};
	auto* parseCache = init->ParseCache;
	if (parseCache == nullptr) {
		parseCache = newParseCache(RefCountCacheOptions{});
	}
	auto* contentMappedParseCache = init->ContentMappedParseCache;
	if (contentMappedParseCache == nullptr) {
		contentMappedParseCache =
		    newContentMappedParseCache(RefCountCacheOptions{});
	}

	auto* host = new SnapshotHost();
	host->options = init->Options;
	host->toPath = toPath;
	host->fs = init->FS.get();
	host->parseCache = parseCache;
	host->contentMappedParseCache = contentMappedParseCache;
	host->extendedConfigCache = newExtendedConfigCache();
	host->programCounter = new programCounter();
	host->contentMapperHost = newContentMapperHost(init);
	return host;
}

// internProjectID — C++ adapter: interns project::ID →
// ls::autoimport::ProjectID so pointer identity matches Go's
// value-keyed maps. Delegates to ls::autoimport::InternProjectID —
// the single canonical cache shared by clone hosts, the Session, and
// the API layer.
ls::autoimport::ProjectID* SnapshotHost::internProjectID(const ID& id) {
	// Global intern — Go keys these maps by the ProjectID interface's
	// VALUE, so every site must produce the canonical pointer for a
	// given id string (see internID in autoimport.h).
	return ls::autoimport::InternProjectID(idString(id));
}

// RetainSnapshot — snapshothost.go:97.
void SnapshotHost::RetainSnapshot(Snapshot* snapshot) {
	snapshot->ref();
}

// CloneSnapshot — snapshothost.go:102.
std::pair<Snapshot*, gostd::Error> SnapshotHost::CloneSnapshot(
    const gostd::Context& ctx, Snapshot* baseSnapshot,
    const FileChangeSummary& fileChanges,
    APISnapshotRequest* apiRequest) {
	SnapshotChange change;
	change.apiRequest = apiRequest;
	change.fileChanges = fileChanges;
	if (apiRequest != nullptr) {
		change.fs = apiRequest->FileSystem;
		change.fileSystemOverride =
		    apiRequest->FileSystem != nullptr;
		change.replaceFileSystem = apiRequest->ReplaceFileSystem;
	}
	auto* snapshot = update(ctx, baseSnapshot, change);
	return {snapshot, snapshot->apiError};
}

// update — snapshothost.go:119.
Snapshot* SnapshotHost::update(const gostd::Context& ctx,
                               Snapshot* baseSnapshot,
                               const SnapshotChange& change) {
	return baseSnapshot->Clone(ctx, change,
	                           baseSnapshot->overlays(), nullptr,
	                           nullptr);
}

// CloneSnapshotWithAutoImports — snapshothost.go:125.
Snapshot* SnapshotHost::CloneSnapshotWithAutoImports(
    const gostd::Context& ctx, Snapshot* baseSnapshot,
    const lsp::lsproto::DocumentUri& uri, logging::Logger* logger) {
	SnapshotChange change;
	change.reason =
	    UpdateReasonRequestedLanguageServiceWithAutoImports;
	change.fs = baseSnapshot->fs->fs;
	change.fileSystemOverride = baseSnapshot->fileSystemOverride;
	static_cast<ResourceRequest&>(change) =
	    baseSnapshot->resourceRequestForDocument(uri);
	change.AutoImports = uri;
	return baseSnapshot->Clone(ctx, change, baseSnapshot->overlays(),
	                           logger, nullptr);
}

// newRootSnapshot — snapshothost.go:136.
Snapshot* SnapshotHost::newRootSnapshot(uint64_t id,
                                        bool relativePatternSupport) {
	auto* fileSystem = newOverlayFS(fs, {},
	                                options->PositionEncoding,
	                                toPath);
	auto* snapshotFS = new SnapshotFS();
	snapshotFS->toPath = toPath;
	snapshotFS->fs = fileSystem;
	auto* registry = new ConfigFileRegistry();
	auto* watch =
	    newWatchedFiles<std::unordered_map<tspath::Path, std::string>>(
	    "auto-import",
	    lsp::lsproto::WatchKindCreate |
	        lsp::lsproto::WatchKindChange |
	        lsp::lsproto::WatchKindDelete,
	    relativePatternSupport,
	    [](const std::unordered_map<tspath::Path, std::string>&
	           nodeModulesDirs) -> PatternsAndIgnored {
		    std::vector<std::string> patterns;
		    patterns.reserve(nodeModulesDirs.size());
		    for (auto& [dir, _] : nodeModulesDirs) {
			    patterns.push_back(getRecursiveGlobPattern(dir));
		    }
		    std::sort(patterns.begin(), patterns.end());
		    return PatternsAndIgnored{
		        .patternsInsideWorkspace = patterns};
	    });
	return newSnapshot(
	    id, snapshotFS, registry, nullptr,
	    lsutil::NewDefaultUserPreferences(), nullptr, watch);
}

} // namespace tsc::project
