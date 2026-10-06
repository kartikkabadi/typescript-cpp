// === dep decls — owned by project ===
// Decls the api slice needs from tsc/internal/project (project.go, session.go,
// snapshot.go, snapshothost.go, projectcollection.go, filechange.go,
// overlayfs.go, api.go). Pure ID types and data-only request/summary structs
// are ported faithfully; all stateful machinery (Session, Snapshot,
// SnapshotHost, ProjectCollection, overlays) is stubbed in project.cpp with
// TSC_UNREACHABLE. The project slice should replace this file when it lands.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/gostd/gostd.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc {
enum class ScriptKind : int32_t; struct CompilerOptions; struct ProjectReference; struct DiagnosticMessage;
struct SourceFile; struct SourceFileParseOptions; struct Diagnostic;
namespace compiler { struct SimpleProgram; }
namespace tsoptions { struct ParsedCommandLine; }
namespace module { struct ResolverOptions; struct Resolver; }
namespace lsconv { struct Converters; struct LSPLineMap; }
namespace lsutil { struct UserPreferences; }
namespace sourcemap { struct ECMALineInfo; }
namespace json { struct Dom; class Decoder; class Encoder; }
namespace autoimport { struct Registry; }
// === slice: api === — Logger is a std::function alias in contentmapper.h;
// an alias re-declaration is legal where a struct fwd-decl was not.
namespace contentmapper { struct Spawner; using Logger = std::function<void(std::string_view)>; struct Host; }
namespace logging { struct Logger; }
} // namespace tsc

namespace tsc::project {

struct ID;
struct Session;
struct Snapshot;
struct SnapshotHost;
struct Project;
struct ProjectCollection;
struct SourceFileLease;
struct ModuleResolverFactory;
struct APICreateProgramRequest;
struct APIReconfigureProgramRequest;
struct APISnapshotRequest;

// project.go:32-96 — project ID types.
using ConfiguredProjectID = tspath::Path;
using InferredProjectID = std::string;
using SyntheticProjectID = std::string;

inline const std::string_view inferredProjectName = "/dev/null/inferredproject*";
inline const std::string_view syntheticProjectPrefix = "/dev/null/syntheticproject*";

struct ID {
	std::string v;

	ID() = default;
	ID(const char* s) : v(s) {}
	// tspath::Path / ConfiguredProjectID / InferredProjectID /
	// SyntheticProjectID are all std::string aliases — this covers them.
	ID(std::string s) : v(std::move(s)) {}

	const std::string& str() const { return v; }
	bool empty() const { return v.empty(); }
	operator const std::string&() const { return v; }
	bool operator==(const ID&) const = default;
	bool operator<(const ID& o) const { return v < o.v; }

	// ID.String (project.go:52).
	std::string String() const { return v; }

	// === slice: api === — project IDs marshal to/from JSON strings.
	std::string unmarshalJSONFrom(json::Decoder& dec);
	std::string marshalJSONTo(json::Encoder& enc) const;

	// ID.Configured (project.go:57).
	std::pair<ConfiguredProjectID, bool> Configured() const;
	// ID.Inferred (project.go:68).
	std::pair<InferredProjectID, bool> Inferred() const;
	// ID.Synthetic (project.go:72).
	std::pair<SyntheticProjectID, bool> Synthetic() const;
};

// ParseConfiguredProjectID (project.go:61).
std::pair<ConfiguredProjectID, bool> ParseConfiguredProjectID(tspath::Path value);
// ParseSyntheticProjectID (project.go:89).
std::pair<SyntheticProjectID, bool> ParseSyntheticProjectID(const std::string& value);
// NewSyntheticProjectID (project.go:45) — panics (TSC_UNREACHABLE) on id <= 0.
SyntheticProjectID NewSyntheticProjectID(int id);
// SyntheticProjectID.UnmarshalJSONFrom (project.go:76).
gostd::Error syntheticProjectIDUnmarshalJSONFrom(const json::Dom& v,
                                                SyntheticProjectID* out);

// Kind (project.go:104).
using Kind = int;
inline constexpr Kind KindInferred = 0;
inline constexpr Kind KindConfigured = 1;
inline constexpr Kind KindSynthetic = 2;

// ProgramUpdateKind (project.go:113).
using ProgramUpdateKind = int;
inline constexpr ProgramUpdateKind ProgramUpdateKindNone = 0;
inline constexpr ProgramUpdateKind ProgramUpdateKindCloned = 1;
inline constexpr ProgramUpdateKind ProgramUpdateKindSameFileNames = 2;
inline constexpr ProgramUpdateKind ProgramUpdateKindNewFiles = 3;

// --- filechange.go --------------------------------------------------------

// FileChangeKind (filechange.go:14).
using FileChangeKind = int;
inline constexpr FileChangeKind FileChangeKindOpen = 0;
inline constexpr FileChangeKind FileChangeKindClose = 1;
inline constexpr FileChangeKind FileChangeKindChange = 2;
inline constexpr FileChangeKind FileChangeKindSave = 3;
inline constexpr FileChangeKind FileChangeKindWatchCreate = 4;
inline constexpr FileChangeKind FileChangeKindWatchChange = 5;
inline constexpr FileChangeKind FileChangeKindWatchDelete = 6;

// FileChangeKind.IsWatchKind (filechange.go:26).
inline bool fileChangeKindIsWatchKind(FileChangeKind k) {
	return k == FileChangeKindWatchCreate || k == FileChangeKindWatchChange ||
	       k == FileChangeKindWatchDelete;
}

// FileChange (filechange.go:30).
struct FileChange {
	FileChangeKind Kind{};
	lsproto::DocumentUri URI;
	int32_t Version{};                                    // Only set for Open/Change
	std::string Content;                                  // Only set for Open
	lsproto::LanguageKind LanguageKind;                   // Only set for Open
	std::vector<lsproto::TextDocumentContentChangePartialOrWholeDocument> Changes; // Only set for Change
};

// FileChangeSummary (filechange.go:39).
struct FileChangeSummary {
	// Only one file can be opened at a time per request
	lsproto::DocumentUri Opened;
	// Reopened is set if a close and open occurred for the same file in a single batch of changes.
	lsproto::DocumentUri Reopened;
	collections::Set<lsproto::DocumentUri> Closed;
	collections::Set<lsproto::DocumentUri> Changed;
	// Only set when file watching is enabled
	collections::Set<lsproto::DocumentUri> Created;
	// Only set when file watching is enabled
	collections::Set<lsproto::DocumentUri> Deleted;

	// IncludesWatchChangeOutsideNodeModules is true if the summary includes a create, change, or delete watch
	// event of a file outside a node_modules directory.
	bool IncludesWatchChangeOutsideNodeModules{};
	// InvalidateAll indicates that all cached file state should be discarded.
	bool InvalidateAll{};

	// Clone (filechange.go:56).
	FileChangeSummary Clone() const;
	// IsEmpty (filechange.go:66).
	bool IsEmpty() const {
		return !InvalidateAll && Opened.empty() && Reopened.empty() &&
		       Closed.Size() == 0 && Changed.Size() == 0 && Created.Size() == 0 &&
		       Deleted.Size() == 0;
	}
	// HasExcessiveWatchEvents (filechange.go:70).
	bool HasExcessiveWatchEvents() const {
		return InvalidateAll || Created.Size() + Deleted.Size() + Changed.Size() >
		                          excessiveChangeThreshold;
	}
	// HasExcessiveNonCreateWatchEvents (filechange.go:74).
	bool HasExcessiveNonCreateWatchEvents() const {
		return InvalidateAll || Deleted.Size() + Changed.Size() > excessiveChangeThreshold;
	}

	inline static constexpr int excessiveChangeThreshold = 1000;
};

// --- overlayfs.go ---------------------------------------------------------

// FileHandle (overlayfs.go:27).
struct FileHandle {
	virtual ~FileHandle() = default;
	virtual std::string FileName() const = 0;
	virtual std::string Text() const = 0;
	virtual std::string OriginalText() const = 0;
	virtual int32_t Version() const = 0;
	virtual bool MatchesDiskText() const = 0;
	virtual bool IsOverlay() const = 0;
	virtual lsconv::LSPLineMap* LSPLineMap() = 0;
	virtual sourcemap::ECMALineInfo* ECMALineInfo() = 0;
	virtual int Kind() const = 0; // core.ScriptKind
};

// FileHandleSource (snapshotfs.go:21).
struct FileHandleSource {
	virtual ~FileHandleSource() = default;
	virtual std::shared_ptr<FileHandle> GetFile(const std::string& fileName) = 0;
	virtual std::shared_ptr<FileHandle> GetFileByPath(const std::string& fileName,
	                                                 tspath::Path path) = 0;
};

// Overlay (overlayfs.go:123).
struct Overlay {
	std::string fileName;
	std::string content;
	uint64_t hashHi{}, hashLo{}; // xxh3.Uint128
	int32_t version{};
	int kind{};                  // core.ScriptKind
	bool matchesDiskText{};

	std::string FileName() const { return fileName; }
};

// LayeredFileSystem (overlayfs.go:192).
struct LayeredFileSystem : virtual vfs::FS, virtual FileHandleSource {
	virtual std::map<tspath::Path, std::shared_ptr<Overlay>> Overlays() = 0;
};

// RebasableFileSystem (overlayfs.go:198).
struct RebasableFileSystem {
	virtual ~RebasableFileSystem() = default;
	virtual std::shared_ptr<vfs::FS> BaseFileSystem() = 0;
	virtual std::shared_ptr<LayeredFileSystem> WithBaseFileSystem(
	    std::shared_ptr<vfs::FS> base) = 0;
};

// NewCachedFileHandle (overlayfs.go:90).
std::shared_ptr<FileHandle> NewCachedFileHandle(std::string fileName,
                                                std::string content);

// --- session.go -----------------------------------------------------------

// SessionOptions (session.go:68).
struct SessionOptions {
	std::string CurrentDirectory;
	std::string DefaultLibraryPath;
	std::string TypingsLocation;
	lsproto::PositionEncodingKind PositionEncoding;
	bool WatchEnabled{};
	bool LoggingEnabled{};
	bool TelemetryEnabled{};
	bool PushDiagnosticsEnabled{};
	// RunExternalCode allows configured content mappers to run their (external) processes,
	// gated on workspace trust by the client. It corresponds to the --runExternalCode CLI flag.
	bool RunExternalCode{};
	gostd::Duration DebounceDelay{};
	// CheckerPoolOptions omitted — owned by project (checker pool config).
};

// SessionInit (session.go:84).
struct SessionInit {
	gostd::Context BackgroundCtx;
	std::shared_ptr<SessionOptions> Options;
	std::shared_ptr<vfs::FS> FS;
	// Client, Logger, NpmExecutor, Spawner, ContentMapperLogger, ParseCache,
	// ContentMappedParseCache — owned by project.
	std::shared_ptr<contentmapper::Spawner> Spawner;
	std::shared_ptr<contentmapper::Logger> ContentMapperLogger;
};

// Session (session.go:111) — manages the state of an LSP session.
struct Session {
	virtual ~Session() = default;

	// Owned by project — all stubbed.
	virtual std::shared_ptr<vfs::FS> FS() = 0;
	virtual std::string GetCurrentDirectory() = 0;
	virtual std::string DefaultLibraryPath() = 0;
	virtual gostd::Context WithCurrentLocale(gostd::Context ctx) = 0;
	virtual gostd::Error DidOpenFile(gostd::Context ctx, lsproto::DocumentUri uri,
	                                 int32_t version, const std::string& content,
	                                 lsproto::LanguageKind languageKind) = 0;
	virtual gostd::Error DidCloseFile(gostd::Context ctx, lsproto::DocumentUri uri) = 0;
	virtual gostd::Error DidChangeFile(
		gostd::Context ctx, lsproto::DocumentUri uri, int32_t version,
		const std::vector<lsproto::TextDocumentContentChangePartialOrWholeDocument>& changes) = 0;
	virtual gostd::Error DidSaveFile(gostd::Context ctx, lsproto::DocumentUri uri) = 0;
	virtual gostd::Error DidChangeWatchedFiles(
		gostd::Context ctx, const std::vector<lsproto::FileEvent>& changes) = 0;

	// APIUpdate (api.go:18).
	virtual std::pair<Snapshot*, gostd::Error> APIUpdate(
		gostd::Context ctx, FileChangeSummary apiFileChanges,
		APISnapshotRequest* apiRequest) = 0;
	// TryAdoptSnapshotInBackground (api.go:59).
	virtual void TryAdoptSnapshotInBackground(Snapshot* baseSnapshot,
	                                          Snapshot* newSnapshot) = 0;

	// session.go:264 — read-only after init.
	virtual lsutil::UserPreferences Config() = 0;

	project::SnapshotHost* SnapshotHost_ = nullptr; // embedded field
};

// --- snapshot.go ----------------------------------------------------------

// Snapshot (snapshot.go:31) — immutable project/session state.
struct Snapshot {
	virtual ~Snapshot() = default;

	// All methods owned by project — stubbed.
	virtual uint64_t ID() const = 0;
	virtual void ref() = 0;
	virtual bool tryRef() = 0;
	virtual void Deref() = 0;
	virtual std::shared_ptr<vfs::FS> FS() = 0;
	virtual std::string GetCurrentDirectory() = 0;
	virtual std::pair<std::string, bool> ReadFile(const std::string& fileName) = 0;
	virtual bool DirectoryExists(const std::string& path) = 0;
	virtual bool FileExists(const std::string& path) = 0;
	virtual std::vector<std::string> GetDirectories(const std::string& path) = 0;
	virtual std::vector<std::string> ReadDirectory(
		const std::string& currentDir, const std::string& path,
		const std::vector<std::string>& extensions,
		const std::vector<std::string>& excludes,
		const std::vector<std::string>& includes, int depth) = 0;
	virtual bool HasFileSystemOverride() const = 0;
	virtual std::vector<std::string> ContentMapperExtensions() = 0;
	virtual std::vector<Project*> CreatedPrograms() = 0;
	virtual Project* GetDefaultProject(lsproto::DocumentUri uri) = 0;
	virtual std::shared_ptr<FileHandle> GetFile(const std::string& fileName) = 0;
	virtual lsconv::LSPLineMap* LSPLineMap(const std::string& fileName) = 0;
	virtual sourcemap::ECMALineInfo* GetECMALineInfo(const std::string& fileName) = 0;
	virtual lsutil::UserPreferences GetPreferences(const std::string& activeFile) = 0;
	virtual lsutil::UserPreferences UserPreferences() = 0;
	virtual lsconv::Converters* Converters() = 0;
	virtual autoimport::Registry* AutoImportRegistry() = 0;
	virtual bool UseCaseSensitiveFileNames() = 0;
	virtual bool isOpenFile(const std::string& fileName) = 0;
	virtual tspath::Path toPath(const std::string& fileName) = 0;

	project::ProjectCollection* ProjectCollection = nullptr;
};

// --- project.go (Project) -------------------------------------------------

// Project (project.go) — a single TypeScript project.
struct Project {
	virtual ~Project() = default;

	int Kind{};                    // project Kind enum
	compiler::SimpleProgram* Program = nullptr;
	tsoptions::ParsedCommandLine* CommandLine = nullptr;

	// All methods owned by project — stubbed.
	virtual std::string CurrentDirectory() = 0;
	virtual std::string DisplayName(const std::string& cwd) = 0;
	virtual project::ID ID() = 0;   // project.go:333
	virtual std::string ConfigFileName() = 0;
	virtual tspath::Path ConfigFilePath() = 0;
	virtual std::string Id() = 0;
	virtual compiler::SimpleProgram* GetProgram() = 0;
	virtual bool IsDirty() = 0;
	virtual std::vector<Diagnostic*> GetProjectDiagnostics(gostd::Context ctx) = 0;
	virtual bool HasFile(const std::string& fileName) = 0;
};

// --- projectcollection.go -------------------------------------------------

// ProjectCollection (projectcollection.go:15).
struct ProjectCollection {
	virtual ~ProjectCollection() = default;

	virtual Project* ConfiguredProject(tspath::Path path) = 0;
	virtual Project* GetProject(ID id) = 0;
	virtual std::vector<Project*> ConfiguredProjects() = 0;
	virtual std::vector<Project*> SyntheticProjects() = 0;
	// ProjectsByID (projectcollection.go:120) — OrderedMap[ID]*Project.
	virtual collections::OrderedMap<ID, Project*>* ProjectsByID() = 0;
	virtual std::vector<Project*> Projects() = 0;
	virtual std::vector<Project*> LanguageServiceProjects() = 0;
	virtual Project* InferredProject() = 0;
	virtual collections::Set<ConfiguredProjectID>* GetOpenConfiguredProjects() = 0;
	virtual Project* GetDefaultProject(tspath::Path path) = 0;
};

// --- snapshothost.go ------------------------------------------------------

// SourceFileLease (snapshothost.go:34) — ref-counted parse-cache lease.
struct SourceFileLease {
	virtual ~SourceFileLease() = default;
	virtual SourceFile* SourceFile() = 0;
	virtual void Release() = 0;
};

// SnapshotHost (snapshothost.go:20).
struct SnapshotHost {
	virtual ~SnapshotHost() = default;

	virtual uint64_t nextSnapshotID() = 0;
	virtual std::shared_ptr<SourceFileLease> AcquireSourceFile(
		SourceFileParseOptions options, const std::string& text,
		int scriptKind /* core.ScriptKind */) = 0;
	virtual Snapshot* NewRootSnapshot() = 0;
	virtual void RetainSnapshot(Snapshot* snapshot) = 0;
	virtual std::pair<Snapshot*, gostd::Error> CloneSnapshot(
		gostd::Context ctx, Snapshot* baseSnapshot,
		FileChangeSummary fileChanges, APISnapshotRequest* apiRequest) = 0;
	virtual Snapshot* CloneSnapshotWithAutoImports(
		gostd::Context ctx, Snapshot* baseSnapshot,
		lsproto::DocumentUri uri, ::tsc::logging::Logger* logger) = 0;
	virtual std::shared_ptr<vfs::FS> FS() = 0;
	virtual std::string GetCurrentDirectory() = 0;
	virtual std::string DefaultLibraryPath() = 0;
	virtual void Close() = 0;
};

// NewSnapshotHost (snapshothost.go:65) — owned by project (stub).
SnapshotHost* NewSnapshotHost(SessionInit* init);

// NewSession (session.go:214-ish) — owned by project (stub).
Session* NewSession(SessionInit* init);

// --- snapshot.go (api request structs) ------------------------------------

// ModuleResolverFactory (snapshot.go:331).
struct ModuleResolverFactory {
	virtual ~ModuleResolverFactory() = default;
	virtual std::pair<module::Resolver*, std::function<void()>> NewResolver(
		const module::ResolverOptions& options) = 0;
};

// APICreateProgramRequest (snapshot.go:322).
struct APICreateProgramRequest {
	std::vector<std::string> RootFileNames;
	CompilerOptions* CompilerOptions = nullptr;
	std::vector<ProjectReference*> ProjectReferences;
	std::vector<Diagnostic*> ConfigFileParsingDiagnostics;
	std::shared_ptr<ModuleResolverFactory> ModuleResolverFactory;
	uint64_t ModuleResolverID{};
};

// APIReconfigureProgramRequest (snapshot.go:335). Go embeds
// APICreateProgramRequest; C++ inherits.
struct APIReconfigureProgramRequest : APICreateProgramRequest {
	SyntheticProjectID ProgramID;
};

// APISnapshotRequest (snapshot.go:340).
struct APISnapshotRequest {
	collections::Set<std::string>* OpenProjects = nullptr;
	collections::Set<tspath::Path>* CloseProjects = nullptr;
	std::map<tspath::Path, std::string> OpenFiles;
	collections::Set<tspath::Path>* CloseFiles = nullptr;
	std::vector<APICreateProgramRequest*> CreatePrograms;
	std::vector<APIReconfigureProgramRequest*> ReconfigurePrograms;
	collections::Set<SyntheticProjectID>* RemovePrograms = nullptr;
	collections::Set<ID>* EnsurePrograms = nullptr;
	bool EnsureAllPrograms{};
	std::map<tspath::Path, std::string> EnsureFiles;
	std::shared_ptr<vfs::FS> FileSystem;
	// ReplaceFileSystem indicates a total filesystem replacement. Layers use
	// per-path file changes instead of invalidating all inherited state.
	bool ReplaceFileSystem{};
};

} // namespace tsc::project

template <>
struct std::hash<tsc::project::ID> {
	size_t operator()(const tsc::project::ID& id) const noexcept {
		return std::hash<std::string>()(id.v);
	}
};
