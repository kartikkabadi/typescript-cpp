// snapshothost.go — SnapshotHost + SourceFileLease.
//
// snapshothost.h
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "internal/ast/ast.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/context.h"
#include "internal/gostd/gostd.h"
#include "internal/project/extendedconfigcache.h"
#include "internal/project/filechange.h"
#include "internal/project/files.h"
#include "internal/project/parsecache.h"
#include "internal/project/programcounter.h"
#include "internal/project/sessiontypes.h"
#include "internal/project/snapshot.h"
#include "internal/project/watch.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc::project {

struct Snapshot;
struct APISnapshotRequest;

// SnapshotHost — snapshothost.go:20. Owns the services shared by a
// collection of immutable snapshots.
struct SnapshotHost {
	SessionOptions* options = nullptr;
	std::function<tspath::Path(const std::string&)> toPath;
	vfs::FS* fs = nullptr;

	ParseCache* parseCache = nullptr;
	ContentMappedParseCache* contentMappedParseCache = nullptr;
	ExtendedConfigCache* extendedConfigCache = nullptr;
	programCounter* programCounter = nullptr;
	std::shared_ptr<contentmapper::Host> contentMapperHost;

	std::atomic<uint64_t> snapshotID{0};

	// nextSnapshotID — snapshothost.go:45.
	uint64_t nextSnapshotID() { return snapshotID.fetch_add(1) + 1; }

	// AcquireSourceFile — snapshothost.go:49.
	struct SourceFileLease* AcquireSourceFile(
	    const SourceFileParseOptions& options,
	    const std::string& text, ScriptKind scriptKind);

	// AcquireExistingSourceFile — snapshothost.go:65.
	struct SourceFileLease* AcquireExistingSourceFile(
	    const ParseCacheKey& key);

	// NewRootSnapshot — snapshothost.go:92. Creates an independent
	// root snapshot.
	Snapshot* NewRootSnapshot() { return newRootSnapshot(0, false); }

	// RetainSnapshot — snapshothost.go:97. Adds a reference to a
	// snapshot owned by this host.
	void RetainSnapshot(Snapshot* snapshot);

	// internProjectID — C++ adapter: Go's autoimport maps are keyed by
	// the ProjectID interface value; we key by ProjectID* pointer, so
	// all ID→ProjectID* interning goes through the single canonical
	// ls::autoimport::InternProjectID cache.
	ls::autoimport::ProjectID* internProjectID(const ID& id);

	// CloneSnapshot — snapshothost.go:102. Derives a snapshot from
	// baseSnapshot without adopting it as any canonical session state
	// or performing session side effects.
	std::pair<Snapshot*, gostd::Error> CloneSnapshot(
	    const gostd::Context& ctx, Snapshot* baseSnapshot,
	    const FileChangeSummary& fileChanges,
	    APISnapshotRequest* apiRequest);

	// update — snapshothost.go:119. Derives a snapshot from
	// baseSnapshot without adopting it as any canonical session state
	// or performing session side effects.
	Snapshot* update(const gostd::Context& ctx,
	                 Snapshot* baseSnapshot,
	                 const SnapshotChange& change);

	// CloneSnapshotWithAutoImports — snapshothost.go:125. Derives a
	// snapshot with auto-import preparation without adopting the
	// clone in the background.
	Snapshot* CloneSnapshotWithAutoImports(
	    const gostd::Context& ctx, Snapshot* baseSnapshot,
	    const lsp::lsproto::DocumentUri& uri,
	    logging::Logger* logger);

	// newRootSnapshot — snapshothost.go:136.
	Snapshot* newRootSnapshot(uint64_t id,
	                        bool relativePatternSupport);

	// newSnapshot — snapshot.go:81.
	Snapshot* newSnapshot(
	    uint64_t id, SnapshotFS* fs, ConfigFileRegistry* registry,
	    CompilerOptions* compilerOptionsForInferredProjects,
	    const ls::lsutil::UserPreferences& userPreferences,
	    ls::autoimport::Registry* autoImports,
	    WatchedFiles<
	        std::unordered_map<tspath::Path, std::string>>*
	        autoImportsWatch);

	// FS — snapshothost.go:172.
	vfs::FS* FS() { return fs; }

	// GetCurrentDirectory — snapshothost.go:176.
	std::string GetCurrentDirectory() const {
		return options->CurrentDirectory;
	}

	// DefaultLibraryPath — snapshothost.go:180.
	std::string DefaultLibraryPath() const {
		return options->DefaultLibraryPath;
	}

	// Close — snapshothost.go:184.
	void Close() {
		if (contentMapperHost != nullptr) {
			contentMapperHost->Close();
		}
	}
};

// SourceFileLease — snapshothost.go:31.
struct SourceFileLease {
	ParseCache* cache = nullptr;
	ParseCacheKey key;
	SourceFile* sourceFile = nullptr;
	std::once_flag releaseOnce;

	// SourceFile — snapshothost.go:37.
	SourceFile* SourceFile_() const { return sourceFile; }

	// Release — snapshothost.go:41.
	void Release() {
		std::call_once(releaseOnce,
		               [&] { cache->Deref(key); });
	}
};

// NewSnapshotHost — snapshothost.go:64.
SnapshotHost* NewSnapshotHost(SessionInit* init);

} // namespace tsc::project
