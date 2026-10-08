// api/requestfilesystem/requestfilesystem.h — requestfilesystem.go +
// filechanges.go + pathtree.go: a vfs.FS implemented from request-supplied
// files, directory listings, and symlinks, either total ("full") or as a
// layer over the session host filesystem.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/project/project.h"
#include "internal/project/filechange.h"
#include "internal/project/files.h"
#include "internal/project/overlayfs.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

// === slice: api ===
namespace tsc::json { class Decoder; class Encoder; }

namespace tsc::api::requestfilesystem {

// Kind controls how a request filesystem is used.
// Go: `type Kind string`.
using Kind = std::string;

// KindFull makes the supplied filesystem canonical and total.
inline const Kind KindFull = "full";
// KindLayer checks the supplied filesystem before falling back to the host.
inline const Kind KindLayer = "layer";

// RequestDirectoryEntries is a cached directory listing. Entry names are
// relative to the directory, matching vfs.GetAccessibleEntries.
struct RequestDirectoryEntries {
	std::vector<std::string> Files;       // nonnil
	std::vector<std::string> Directories; // nonnil

	// === slice: api ===
	// encoding/json Unmarshal (api/proto.cpp — tag names `files`, `directories`).
	std::string unmarshalJSONFrom(json::Decoder& dec);
	// === slice: api === — marshalJSONTo emits the tagged Go layout.
	std::string marshalJSONTo(json::Encoder& enc) const;
};

// RequestSymlink describes a symbolic link in a request filesystem.
struct RequestSymlink {
	// Target is resolved relative to the directory containing the link,
	// matching native symbolic-link semantics.
	std::string Target;
	// Host routes the target through the host filesystem. This is the only
	// way a full filesystem can access paths not supplied in the request
	// filesystem.
	bool Host{};

	// === slice: api ===
	// encoding/json Unmarshal (api/proto.cpp — tags `target`, `host`).
	std::string unmarshalJSONFrom(json::Decoder& dec);
	// === slice: api === — marshalJSONTo emits the tagged Go layout.
	std::string marshalJSONTo(json::Encoder& enc) const;
};

// RequestFileSystem supplies file contents and, optionally, directory
// listings for a request that creates a snapshot.
struct RequestFileSystem {
	Kind kind;
	// Files maps file names to their complete contents.
	std::map<std::string, std::string> Files; // nonnil
	// Directories maps directory names to complete listing results. Directory
	// structure implied by Files is derived when a listing is omitted.
	std::optional<std::map<std::string, RequestDirectoryEntries>> Directories;
	// Symlinks maps link paths to targets in this filesystem or the host
	// filesystem.
	std::optional<std::map<std::string, RequestSymlink>> Symlinks;
	// RemovedPaths lists files or directory trees that must be treated as
	// missing even when present in an underlying snapshot or host filesystem.
	std::optional<std::vector<std::string>> RemovedPaths;

	// === slice: api ===
	// encoding/json Unmarshal/Marshal — tags `kind`, `files`, `directories`,
	// `symlinks`, `removedPaths`.
	std::string unmarshalJSONFrom(json::Decoder& dec);
	std::string marshalJSONTo(json::Encoder& enc) const;
};

// NewForUpdate creates a request filesystem for a snapshot update. Layers
// over request filesystems are compacted eagerly so the result does not
// retain its base snapshot's filesystem.
std::pair<std::shared_ptr<vfs::FS>, gostd::Error> NewForUpdate(
    RequestFileSystem* params, const std::shared_ptr<vfs::FS>& base,
    const std::string& currentDirectory,
    project::FileChangeSummary* fileChanges);

// HasFullFileSystem reports whether fileSystem contains a complete request
// filesystem.
bool HasFullFileSystem(vfs::FS* fileSystem);
bool HasFullFileSystem(const std::shared_ptr<vfs::FS>& fileSystem);

// ---------------------------------------------------------------------------
// internals (requestfilesystem.go + pathtree.go) — declared here so all three
// .cpp files share them.
// ---------------------------------------------------------------------------

enum class requestFallback : uint8_t {
	Inherit,
	Allowed,
	Missing,
};

// requestEntry — pathtree.go:26.
struct requestEntry {
	virtual ~requestEntry() = default;
};

// requestFile (pathtree.go:30) — implements vfs::FileInfo + vfs::DirEntry.
struct requestFile final : requestEntry,
                           vfs::FileInfo,
                           vfs::DirEntry {
	std::string fileName;
	std::string content;

	std::string Name() const override;
	int64_t Size() const override;
	vfs::FileMode Mode() const override;
	vfs::TimePoint ModTime() const override;
	bool IsDir() const override;
	std::any Sys() const override;
	vfs::FileMode Type() const override;
	std::pair<std::shared_ptr<vfs::FileInfo>, vfs::Error>
	Info() const override;
};

// requestSymlink (pathtree.go:36).
struct requestSymlink final : requestEntry {
	std::string linkName;
	std::string target;
	bool host{};
};

// requestDirectory (pathtree.go:41) — implements vfs::FileInfo +
// vfs::DirEntry.
struct requestDirectory final : requestEntry,
                                vfs::FileInfo,
                                vfs::DirEntry {
	std::string directoryName;
	vfs::Entries* listing = nullptr;

	std::string Name() const override;
	int64_t Size() const override;
	vfs::FileMode Mode() const override;
	vfs::TimePoint ModTime() const override;
	bool IsDir() const override;
	std::any Sys() const override;
	vfs::FileMode Type() const override;
	std::pair<std::shared_ptr<vfs::FileInfo>, vfs::Error>
	Info() const override;
};

// requestPathNode (pathtree.go:77). Trees are immutable once composed
// (composeRequestPaths never mutates shared nodes), so children and entries
// are raw pointers allocated once and leaked — Go GC semantics.
struct requestPathNode {
	requestEntry* entry = nullptr;
	requestFallback fallback = requestFallback::Inherit;
	std::map<tspath::Path, requestPathNode*> children;
	bool hasSymlinks{};

	bool replacesSubtree() const;
	requestPathNode* ensure(tspath::Path path);
	std::pair<requestPathNode*, requestFallback> lookup(tspath::Path path);
	void walkSymlinks(
	    const std::function<void(tspath::Path, requestSymlink*)>& visit);
	std::pair<vfs::Entries, bool> entries();
	std::pair<tspath::Path, requestSymlink*> firstSymlink(tspath::Path path);
	bool containsFileAncestor(tspath::Path path);
};

std::vector<tspath::Path> requestPathAncestors(tspath::Path path);
requestPathNode* composeRequestPaths(requestPathNode* base,
                                     requestPathNode* overlay,
                                     requestFallback fallback,
                                     bool caseSensitive);
bool requestPathContains(tspath::Path parent, tspath::Path path);

// resolvedRequestPath (requestfilesystem.go:69).
struct resolvedRequestPath {
	std::string path;
	bool followedSymlink{};
	bool host{};
	bool ok{};
};

// requestPathLookup (requestfilesystem.go:76).
struct requestPathLookup {
	std::string path;
	vfs::FileInfo* info = nullptr; // request entries are leaked (GC semantics)
	vfs::FS* fileSystem = nullptr;
	bool followedSymlink{};
	bool ok{};
};

// requestFileSystem (requestfilesystem.go:60) — implements vfs::FS,
// project::FileHandleSource, project::LayeredFileSystem (Overlays) and
// project::RebasableFileSystem (BaseFileSystem/WithBaseFileSystem).
// LayeredFileSystem gives us vfs::FS + FileHandleSource via virtual bases;
// RebasableFileSystem adds BaseFileSystem/WithBaseFileSystem.
struct requestFileSystem final : project::LayeredFileSystem,
                                 project::RebasableFileSystem {
	Kind kind;
	std::shared_ptr<vfs::FS> base;
	std::string currentDirectory;
	bool useCaseSensitiveNames{};
	requestPathNode* paths = nullptr;

	vfs::FS* baseFileSystem() const { return base.get(); }

	vfs::FS* BaseFileSystem() override { return base.get(); }
	project::LayeredFileSystem* WithBaseFileSystem(
	    vfs::FS* base) override;
	std::unordered_map<tspath::Path, project::Overlay*>
	Overlays() override;

	std::shared_ptr<requestFileSystem> applyTo(
	    const requestFileSystem& base);
	bool blocksFallback(const std::string& path) const;
	std::string toAbsolutePath(const std::string& path) const;
	std::string toAbsolutePathFrom(const std::string& path,
	                               const std::string& currentDirectory) const;
	tspath::Path toPath(const std::string& path) const;
	void registerDirectory(std::string directoryName);
	resolvedRequestPath resolvePath(std::string path) const;
	bool isHostPath(const std::string& path) const;
	std::vector<std::string> aliasesForPath(const std::string& path) const;
	std::pair<vfs::FileInfo*, requestFallback>
	localPathInfo(const std::string& path) const;
	requestPathLookup lookupPath(const std::string& path) const;
	std::tuple<vfs::FS*, std::string, bool>
	mutationPath(const std::string& path) const;

	std::tuple<vfs::Entries, bool, bool>
	getLocalEntries(const std::string& directoryName) const;
	vfs::Entries filterLocalEntries(const std::string& directoryName,
	                                vfs::Entries entries) const;
	vfs::Entries removeEntries(const std::string& directoryName,
	                           vfs::Entries entries) const;
	vfs::Entries addSymlinkEntries(const std::string& directoryName,
	                               vfs::Entries entries);
	std::vector<std::string>
	deleteEntryName(std::vector<std::string> values,
	                const std::string& value) const;
	bool equalEntryNames(const std::string& left,
	                     const std::string& right) const;

	// project::FileChangeSummary expansion (filechanges.go).
	project::FileChangeSummary ExpandFileChanges(
	    project::FileChangeSummary summary);

	// vfs::FS.
	bool UseCaseSensitiveFileNames() override;
	project::FileHandle* GetFile(const std::string& fileName) override;
	project::FileHandle* GetFileByPath(const std::string& fileName,
	                                   const tspath::Path& path) override;
	std::pair<std::string, bool> ReadFile(const std::string& fileName) override;
	bool FileExists(const std::string& fileName) override;
	bool DirectoryExists(const std::string& directoryName) override;
	vfs::Entries GetAccessibleEntries(const std::string& directoryName) override;
	std::string Realpath(const std::string& path) override;
	vfs::Error WriteFile(const std::string& fileName,
	                     const std::string& data) override;
	vfs::Error AppendFile(const std::string& fileName,
	                      const std::string& data) override;
	vfs::Error Remove(const std::string& path) override;
	vfs::Error Chtimes(const std::string& path, vfs::TimePoint aTime,
	                   vfs::TimePoint mTime) override;
	std::shared_ptr<vfs::FileInfo> Stat(const std::string& path) override;
};

// getRequestFileSystem (requestfilesystem.go:83).
inline requestFileSystem* getRequestFileSystem(vfs::FS* fileSystem) {
	return dynamic_cast<requestFileSystem*>(fileSystem);
}
inline requestFileSystem* getRequestFileSystem(
    const std::shared_ptr<vfs::FS>& fs) {
	return dynamic_cast<requestFileSystem*>(fs.get());
}

// sharedFileInfo adapts a shared_ptr<FileInfo> (from another FS's Stat) into
// a leaked FileInfo so statFileSystem can return a uniform raw pointer.
struct sharedFileInfo final : vfs::FileInfo {
	std::shared_ptr<vfs::FileInfo> inner;
	explicit sharedFileInfo(std::shared_ptr<vfs::FileInfo> i)
	    : inner(std::move(i)) {}
	std::string Name() const override { return inner->Name(); }
	int64_t Size() const override { return inner->Size(); }
	vfs::FileMode Mode() const override { return inner->Mode(); }
	vfs::TimePoint ModTime() const override { return inner->ModTime(); }
	bool IsDir() const override { return inner->IsDir(); }
	std::any Sys() const override { return inner->Sys(); }
};

// newRequestFileSystemWorker (requestfilesystem.go:117).
std::pair<std::shared_ptr<requestFileSystem>, gostd::Error>
newRequestFileSystemWorker(RequestFileSystem* params,
                           const std::shared_ptr<vfs::FS>& base,
                           const std::string& currentDirectory);

// addFileChanges (filechanges.go:16).
void addFileChanges(project::FileChangeSummary* summary,
                    RequestFileSystem* request,
                    const std::shared_ptr<vfs::FS>& baseFS,
                    requestFileSystem* fileSystem,
                    const std::string& currentDirectory);

// statFileSystem (requestfilesystem.go:704) — Stat with FS fallbacks. The
// returned FileInfo is either borrowed from fileSystem->Stat or a leaked
// requestFile/requestDirectory, so a shared_ptr would double-delete; raw is
// fine under GC semantics.
vfs::FileInfo* statFileSystem(vfs::FS* fileSystem, const std::string& path);
vfs::Entries mergeEntries(vfs::Entries base, vfs::Entries overlay,
                          const std::function<bool(const std::string&,
                                                   const std::string&)>& equal);
vfs::Entries cloneEntries(const vfs::Entries& entries);

}  // namespace tsc::api::requestfilesystem
