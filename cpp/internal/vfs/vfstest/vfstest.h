// vfstest.h — port of tsc/internal/vfs/vfstest/vfstest.go: an in-memory
// MapFS for tests, built over an analog of Go's testing/fstest.MapFS.
// `fstest` holds the stdlib analog; `vfstest` holds the package itself.
#pragma once

#include "internal/vfs/iovfs/iovfs.h"
#include "internal/vfs/vfs.h"

#include <any>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace tsc::vfs::vfstest {

// fstest — analog of Go's testing/fstest (mapfs.go). Only the surface
// vfstest uses is ported: MapFile, MapFS.Open, resolveSymlinks.
namespace fstest {

// MapFile describes a single file in a MapFS.
struct MapFile {
	std::string Data;      // file content or symlink destination
	FileMode Mode = {};    // fs.FileInfo.Mode
	TimePoint ModTime = {};// fs.FileInfo.ModTime
	std::any Sys;          // fs.FileInfo.Sys
};

// MapFS is a map of paths to MapFiles. The map need not include parent
// directories; those are synthesized on the fly, as in fstest.MapFS.
struct MapFS {
	std::unordered_map<std::string, std::shared_ptr<MapFile>> files;

	// Open opens the named file after following any symbolic links.
	std::pair<std::shared_ptr<File>, Error>
	Open(const std::string& name) const;

	// resolveSymlinks — fstest.MapFS.resolveSymlinks.
	std::pair<std::string, bool>
	resolveSymlinks(const std::string& name) const;
};

} // namespace fstest

// Clock — vfstest.go Clock interface.
struct Clock {
	virtual ~Clock() = default;
	virtual TimePoint Now() = 0;
	virtual Duration SinceStart() = 0;
};

// clockImpl — vfstest.go.
struct clockImpl final : Clock {
	TimePoint start;
	TimePoint Now() override;
	Duration SinceStart() override;
};

// sys is stored in each MapFile.Sys by vfstest::MapFS.setEntry.
struct sys {
	std::any original;
	std::string realpath;
};

using canonicalPath = std::string;

// MapFS — vfstest.go: a mutable in-memory FS usable through iovfs.From.
// Implements iovfs.RealpathFS + iovfs.WritableFS.
struct MapFS final : iovfs::RealpathFS, iovfs::WritableFS {
	// mu protects m. A single mutex is sufficient as we only use the
	// inner map's Open method.
	mutable std::shared_mutex mu;

	// keys in m.files are canonicalPaths
	fstest::MapFS m;

	bool useCaseSensitiveFileNames;

	std::unordered_map<canonicalPath, canonicalPath> symlinks;

	std::shared_ptr<Clock> clock;

	// --- internal helpers (vfstest.go) ---
	canonicalPath getCanonicalPath(std::string_view p) const;
	std::pair<std::shared_ptr<File>, Error>
	open(const canonicalPath& p) const;
	Error remove(const std::string& path);
	std::tuple<std::shared_ptr<fstest::MapFile>, canonicalPath, Error>
	getFollowingSymlinks(const canonicalPath& p) const;
	std::tuple<std::shared_ptr<fstest::MapFile>, canonicalPath, Error>
	getFollowingSymlinksWorker(const canonicalPath& p,
	                           const canonicalPath& symlinkFrom,
	                           const canonicalPath& symlinkTo) const;
	void set(const canonicalPath& p,
	         const std::shared_ptr<fstest::MapFile>& file);
	void setEntry(const std::string& realpath, const canonicalPath& canonical,
	              fstest::MapFile file);
	Error mkdirAll(const std::string& p, FileMode perm);

	// --- io/fs + extra API ---
	std::pair<std::shared_ptr<File>, Error>
	Open(const std::string& name) override;
	std::pair<std::string, Error>
	Realpath(const std::string& name) override;
	Error MkdirAll(const std::string& path, FileMode perm) override;
	void AddSymlink(const std::string& path, const std::string& target);
	Error WriteFile(const std::string& path, const std::string& data,
	                FileMode perm) override;
	Error AppendFile(const std::string& path, const std::string& data,
	                 FileMode perm) override;
	Error Remove(const std::string& path) override;
	Error Chtimes(const std::string& path, TimePoint aTime,
	              TimePoint mTime) override;
	std::pair<std::string, bool>
	GetTargetOfSymlink(const std::string& path);
	TimePoint GetModTime(const std::string& path);
	// Entries — vfstest.go iter.Seq2; returns sorted (realpath, file)
	// pairs.
	std::vector<std::pair<std::string, std::shared_ptr<fstest::MapFile>>>
	Entries();
	std::shared_ptr<fstest::MapFile> GetFileInfo(const std::string& path);
};

// Symlink returns a MapFile describing a symbolic link with the given
// target — vfstest.go Symlink.
std::shared_ptr<fstest::MapFile> Symlink(std::string_view target);

// MapFileInput is the `File` type parameter of FromMap[File]: string,
// []byte, or *fstest.MapFile in Go.
using MapFileInput = std::variant<std::string, std::vector<uint8_t>,
                                  std::shared_ptr<fstest::MapFile>>;

// FromMap creates a new vfs::FS from a map of paths to file contents —
// vfstest.go FromMap.
//
// The paths must be normalized absolute paths according to the tspath
// package, without trailing directory separators. The paths must be all
// POSIX-style or all Windows-style, but not both.
std::shared_ptr<vfs::FS>
FromMap(const std::unordered_map<std::string, MapFileInput>& m,
        bool useCaseSensitiveFileNames);

// FromMapWithClock — vfstest.go; same as FromMap with a custom Clock.
std::shared_ptr<vfs::FS>
FromMapWithClock(const std::unordered_map<std::string, MapFileInput>& m,
                 bool useCaseSensitiveFileNames,
                 std::shared_ptr<Clock> clock);

// convertMapFS — vfstest.go.
std::shared_ptr<MapFS>
convertMapFS(const fstest::MapFS& input, bool useCaseSensitiveFileNames,
             std::shared_ptr<Clock> clock);

// BrokenSymlinkError — vfstest.go brokenSymlinkError.
struct BrokenSymlinkErrorImpl : Error::Impl {
	canonicalPath from, to;
	BrokenSymlinkErrorImpl(canonicalPath from, canonicalPath to);
};

// isBrokenSymlinkError — vfstest.go.
bool isBrokenSymlinkError(const Error& err);

// splitPath — vfstest.go (note: returns the whole string as `before`
// when no '/' is found past offset).
std::pair<std::string, std::string> splitPath(const std::string& s,
                                            int offset);

// dirName / baseName — path.Dir/path.Base wrappers from vfstest.go.
std::string dirName(std::string_view p);
std::string baseName(std::string_view p);

// comparePathsByParts — vfstest.go.
int comparePathsByParts(std::string_view a, std::string_view b);

// umask — vfstest.go.
inline constexpr uint32_t kUmask = 0022u;

} // namespace tsc::vfs::vfstest
