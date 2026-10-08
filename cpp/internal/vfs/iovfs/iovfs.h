// iovfs.h — port of tsc/internal/vfs/iovfs/iofs.go: build a vfs::FS over an
// io/fs file system.
#pragma once

#include "internal/vfs/vfs.h"

#include <memory>
#include <string>

namespace tsc::vfs::iovfs {

// RealpathFS — fs.FS + Realpath (iofs.go:14-17).
struct RealpathFS : virtual IoFS {
	virtual std::pair<std::string, Error>
	Realpath(const std::string& path) = 0;
};

// WritableFS — fs.FS + mutating operations (iofs.go:19-27).
struct WritableFS : virtual IoFS {
	virtual Error WriteFile(const std::string& path,
	                        const std::string& data, FileMode perm) = 0;
	virtual Error AppendFile(const std::string& path,
	                         const std::string& data, FileMode perm) = 0;
	virtual Error MkdirAll(const std::string& path, FileMode perm) = 0;
	// Removes `path` and all its contents. Will return the first error it
	// encounters.
	virtual Error Remove(const std::string& path) = 0;
	virtual Error Chtimes(const std::string& path, TimePoint aTime,
	                      TimePoint mTime) = 0;
};

// FsWithSys — vfs.FS + FSys (iofs.go:29-32).
struct FsWithSys : virtual vfs::FS {
	virtual std::shared_ptr<IoFS> FSys() = 0;
};

// From creates a new FS from an fs.FS.
//
// For paths like `c:/foo/bar`, fsys will be used as though it's rooted at
// `/` and the path is `/c:/foo/bar`.
//
// If the provided fs.FS implements RealpathFS, it will be used to implement
// the Realpath method. If it implements WritableFS, it will be used to
// implement the WriteFile method.
//
// From does not actually handle case-insensitivity; ensure the passed in
// fs.FS respects case-insensitive file names if needed. Consider using
// vfstest::FromMap for testing.
std::shared_ptr<FsWithSys> From(std::shared_ptr<IoFS> fsys,
                                bool useCaseSensitiveFileNames);

} // namespace tsc::vfs::iovfs
