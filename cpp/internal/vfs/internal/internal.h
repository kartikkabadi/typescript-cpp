// internal.h — port of tsc/internal/vfs/internal/internal.go: shared helpers
// for FS implementations (root splitting, stat, entry enumeration, BOM-aware
// file decoding).
#pragma once

#include "internal/vfs/vfs.h"

#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace tsc::vfs::internal {

// RootLength — the length of the root portion of an absolute path; panics
// on non-absolute paths, and returns ~l for "volume-relative" roots (l < 0).
int rootLength(const std::string& p);

// SplitPath splits a path into (rootName, rest), with rest stripped of its
// trailing directory separator.
std::pair<std::string, std::string> splitPath(const std::string& p);

// Common — the shared implementation of FS methods used by ioFS and osFS.
struct Common {
	// RootFor maps a root name (e.g. "/", "c:/") to an fs.FS rooted there;
	// returns nullptr if the root is unknown.
	std::function<std::shared_ptr<IoFS>(const std::string& root)> RootFor;
	// IsReparsePoint detects Windows junctions/reparse points; nullptr on
	// systems without reparse points.
	std::function<bool(const std::string& path)> IsReparsePoint;

	std::tuple<std::shared_ptr<IoFS>, std::string, std::string>
	RootAndPath(const std::string& path) const;

	std::shared_ptr<FileInfo> Stat(const std::string& path) const;
	bool FileExists(const std::string& path) const;
	bool DirectoryExists(const std::string& path) const;
	Entries GetAccessibleEntries(const std::string& path) const;
	std::pair<std::string, bool> ReadFile(const std::string& path) const;

private:
	std::vector<std::shared_ptr<DirEntry>> getEntries(
	    const std::string& path) const;
};

} // namespace tsc::vfs::internal
