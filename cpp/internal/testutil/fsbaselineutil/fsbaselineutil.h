// fsbaselineutil.h — port of tsc/internal/testutil/fsbaselineutil/
// differ.go: FSDiffer snapshots a vfstest::MapFS and prints per-path diffs
// for baselines.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/gostd/gostd.h"
#include "internal/vfs/iovfs/iovfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::testutil::fsbaselineutil {

namespace iovfs = ::tsc::vfs::iovfs;
namespace vfstest = ::tsc::vfs::vfstest;

// DiffEntry — differ.go:18.
struct DiffEntry {
	std::string Content;
	vfs::TimePoint MTime{};
	bool IsWritten = false;
	std::string SymlinkTarget;
};

// Snapshot — differ.go:25.
struct Snapshot {
	std::unordered_map<std::string, std::shared_ptr<DiffEntry>> Snap;
	// Go `*collections.SyncSet[string]` — owned by the snapshot.
	std::unique_ptr<collections::SyncSet<std::string>> DefaultLibs;
};

// FileChange — differ.go:141.
struct FileChange {
	std::string Path;
	bool Deleted = false;
};

// FSDiffer — differ.go:30.
struct FSDiffer {
	std::shared_ptr<iovfs::FsWithSys> FS;
	std::function<collections::SyncSet<std::string>*()> DefaultLibs;
	collections::SyncSet<std::string>* WrittenFiles = nullptr;

	std::unique_ptr<Snapshot> serializedDiff;

	// MapFs — differ.go:38.
	std::shared_ptr<vfstest::MapFS> MapFs();
	// SerializedDiff — differ.go:42.
	Snapshot* SerializedDiff() { return serializedDiff.get(); }
	// BaselineFSwithDiff — differ.go:46.
	void BaselineFSwithDiff(gostd::io::Writer* baseline);
	// ChangedPaths — differ.go:147.
	std::vector<FileChange> ChangedPaths();

private:
	// addFsEntryDiff — differ.go:111.
	void addFsEntryDiff(std::unordered_map<std::string, std::string>& diffs,
	                    const std::shared_ptr<DiffEntry>& newDirContent,
	                    const std::string& path);
};

// SanitizeInternalSymbolName — differ.go:101.
std::string SanitizeInternalSymbolName(std::string_view s);

}  // namespace tsc::testutil::fsbaselineutil
