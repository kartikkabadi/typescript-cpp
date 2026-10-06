// fsbaselineutil — FSDiffer (differ.go). Ported for fourslash state
// baselines.
#pragma once

#include <functional>
#include <ostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/gostd/gostd.h"
#include "internal/vfs/iovfs/iovfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::testutil::fsbaselineutil {

// DiffEntry — differ.go:17.
struct DiffEntry {
	std::string Content;
	vfs::TimePoint MTime = {};
	bool IsWritten = false;
	std::string SymlinkTarget;
};

// Snapshot — differ.go:24.
struct Snapshot {
	std::unordered_map<std::string, std::shared_ptr<DiffEntry>> Snap;
	std::shared_ptr<collections::SyncSet<std::string>> DefaultLibs;
};

// FileChange represents a filesystem change detected between
// snapshots. differ.go:151.
struct FileChange {
	std::string Path;
	bool Deleted = false;
};

// FSDiffer — differ.go:29.
struct FSDiffer {
	vfs::iovfs::FsWithSys* FS = nullptr;
	std::function<collections::SyncSet<std::string>*()> DefaultLibs;
	std::shared_ptr<collections::SyncSet<std::string>> WrittenFiles;

	std::shared_ptr<Snapshot> serializedDiff;

	// MapFs — differ.go:37.
	vfs::vfstest::MapFS* MapFs();

	// SerializedDiff — differ.go:41.
	Snapshot* SerializedDiff() { return serializedDiff.get(); }

	// BaselineFSwithDiff — differ.go:45 (Go io.Writer).
	void BaselineFSwithDiff(gostd::io::Writer* baseline);

	// ChangedPaths — differ.go:157.
	std::vector<FileChange> ChangedPaths();

private:
	// addFsEntryDiff — differ.go:120.
	void addFsEntryDiff(
	    std::unordered_map<std::string, std::string>& diffs,
	    std::shared_ptr<DiffEntry> newDirContent,
	    const std::string& path);
};

// SanitizeInternalSymbolName replaces internal symbol names of shape
// \uFFFD@symbolName@123 with \uFFFD@symbolName@<symbolId>
// // to avoid baselining differences in symbol ids, which can change
// between runs. differ.go:113.
std::string SanitizeInternalSymbolName(const std::string& s);

} // namespace tsc::testutil::fsbaselineutil
