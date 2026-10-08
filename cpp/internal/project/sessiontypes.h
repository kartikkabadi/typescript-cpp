// session.go — the small types the rest of the project slice needs:
// UpdateReason, ErrNoProjectForUnknownScriptKind,
// ContentMapperContributions, SessionOptions, SessionInit. Kept in
// their own header so compilerhost/snapshothost/etc. needn't pull in
// the Session class.
//
// sessiontypes.h
#pragma once

#include <chrono>
#include <string>

#include "internal/contentmapper/contentmapper.h"
#include "internal/gostd/gostd.h"
#include "internal/ls/lsutil/lsutil.h" // lsproto decls (dep stubs)
#include "internal/project/checkerpool.h"
#include "internal/project/parsecache.h"
#include "internal/vfs/vfs.h"

namespace tsc::project {

// UpdateReason — session.go:36.
enum class UpdateReason : int {
	Unknown = 0,
	DidOpenFile,
	DidCloseFile,
	DidChangeCompilerOptionsForInferredProjects,
	RequestedLanguageServicePendingChanges,
	RequestedLanguageServiceProjectNotLoaded,
	RequestedLanguageServiceForFileNotOpen,
	RequestedLanguageServiceProjectDirty,
	RequestedLoadProjectTree,
	RequestedLanguageServiceWithAutoImports,
	IdleCleanDiskCache,
	DidChangeConfigFile,
	DidChangeContentMapperContributions,
};
inline constexpr UpdateReason UpdateReasonUnknown =
    UpdateReason::Unknown;
inline constexpr UpdateReason UpdateReasonDidOpenFile =
    UpdateReason::DidOpenFile;
inline constexpr UpdateReason UpdateReasonDidCloseFile =
    UpdateReason::DidCloseFile;
inline constexpr UpdateReason
    UpdateReasonDidChangeCompilerOptionsForInferredProjects =
        UpdateReason::DidChangeCompilerOptionsForInferredProjects;
inline constexpr UpdateReason
    UpdateReasonRequestedLanguageServicePendingChanges =
        UpdateReason::RequestedLanguageServicePendingChanges;
inline constexpr UpdateReason
    UpdateReasonRequestedLanguageServiceProjectNotLoaded =
        UpdateReason::RequestedLanguageServiceProjectNotLoaded;
inline constexpr UpdateReason
    UpdateReasonRequestedLanguageServiceForFileNotOpen =
        UpdateReason::RequestedLanguageServiceForFileNotOpen;
inline constexpr UpdateReason
    UpdateReasonRequestedLanguageServiceProjectDirty =
        UpdateReason::RequestedLanguageServiceProjectDirty;
inline constexpr UpdateReason UpdateReasonRequestedLoadProjectTree =
    UpdateReason::RequestedLoadProjectTree;
inline constexpr UpdateReason
    UpdateReasonRequestedLanguageServiceWithAutoImports =
        UpdateReason::RequestedLanguageServiceWithAutoImports;
inline constexpr UpdateReason UpdateReasonIdleCleanDiskCache =
    UpdateReason::IdleCleanDiskCache;
inline constexpr UpdateReason UpdateReasonDidChangeConfigFile =
    UpdateReason::DidChangeConfigFile;
inline constexpr UpdateReason
    UpdateReasonDidChangeContentMapperContributions =
        UpdateReason::DidChangeContentMapperContributions;

// ErrNoProjectForUnknownScriptKind — session.go:39.
inline const gostd::Error ErrNoProjectForUnknownScriptKind =
    gostd::newError("no project for unknown script kind");

// watchRequestTimeout — session.go:64. Max time to wait for the
// client to respond to a WatchFiles/UnwatchFiles request while
// holding the watches mutex.
inline constexpr std::chrono::nanoseconds watchRequestTimeout =
    std::chrono::seconds(1);

// idleCacheCleanDelay — session.go:661.
inline constexpr std::chrono::nanoseconds idleCacheCleanDelay =
    std::chrono::seconds(30);

// performanceTelemetryInterval — session.go:703.
inline constexpr std::chrono::nanoseconds performanceTelemetryInterval =
    std::chrono::minutes(5);

// projectLoadKind — projectcollectionbuilder.go:26. Declared in
// sessiontypes.h because configfileregistrybuilder.h needs it and the
// collection builder includes it.
enum class projectLoadKind : int {
	// projectLoadKindFind — return an existing project if it exists.
	Find,
	// projectLoadKindCreate — always create a new project for the config.
	Create,
};
inline constexpr projectLoadKind projectLoadKindFind =
    projectLoadKind::Find;
inline constexpr projectLoadKind projectLoadKindCreate =
    projectLoadKind::Create;

// ContentMapperContributions — session.go:57.
struct ContentMapperContributions {
	std::vector<contentmapper::Mapper*> Mappers;
	std::vector<std::string> Extensions;
};

// SessionOptions — session.go:68. Immutable initialization options;
// snapshots reference it by pointer since it never changes.
struct SessionOptions {
	std::string CurrentDirectory;
	std::string DefaultLibraryPath;
	std::string TypingsLocation;
	lsp::lsproto::PositionEncodingKind PositionEncoding;
	bool WatchEnabled = false;
	bool LoggingEnabled = false;
	bool TelemetryEnabled = false;
	bool PushDiagnosticsEnabled = false;
	// RunExternalCode — allows configured content mappers to run
	// their (external) processes, gated on workspace trust by the
	// client; corresponds to --runExternalCode.
	bool RunExternalCode = false;
	std::chrono::nanoseconds DebounceDelay{0};
	CheckerPoolOptions CheckerPoolOptions;
};

} // namespace tsc::project

namespace tsc::logging {
struct Logger;
struct LogTree;
} // namespace tsc::logging
namespace tsc::ata {
struct NpmExecutor;
} // namespace tsc::ata

namespace tsc::project {
struct Client;

// SessionInit — session.go:84.
struct SessionInit {
	gostd::Context BackgroundCtx;
	SessionOptions* Options = nullptr;
	std::shared_ptr<vfs::FS> FS;
	Client* Client = nullptr;
	logging::Logger* Logger = nullptr;
	// Keep-alive for the owners of every borrowed pointer above
	// (Client, Logger, NpmExecutor, Spawner, ContentMapperLogger,
	// ParseCache, ContentMappedParseCache, the FS graph). Go relies on
	// GC: the session stores references and goroutines spawned by the
	// session dereference them, so they outlive the session's
	// background work. The session retains this list for its lifetime
	// so callers can drop their own handles once NewSession returns.
	std::vector<std::shared_ptr<void>> KeepAlive;
	ata::NpmExecutor* NpmExecutor = nullptr;
	// Lifetime pins for the raw Client/NpmExecutor above: Go's GC keeps
	// the server object alive as long as any session goroutine (its
	// backgroundQueue workers) can reach it; a detached std::thread
	// calling client->PublishDiagnostics() or npmExecutor->NpmInstall()
	// after server teardown would otherwise touch freed memory. The
	// session keeps these refs for its whole lifetime.
	std::shared_ptr<tsc::project::Client> ClientRef;
	std::shared_ptr<ata::NpmExecutor> NpmExecutorRef;
	// Spawner launches content mapper processes; nil when the host
	// cannot spawn processes.
	contentmapper::Spawner* Spawner = nullptr;
	contentmapper::Logger* ContentMapperLogger = nullptr;
	ParseCache* ParseCache = nullptr;
	ContentMappedParseCache* ContentMappedParseCache = nullptr;
};

} // namespace tsc::project
