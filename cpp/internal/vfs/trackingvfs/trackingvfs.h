// trackingvfs.h — port of tsc/internal/vfs/trackingvfs/trackingvfs.go: a
// vfs::FS wrapper that records every path accessed via read-like
// operations (for watch-mode dependency tracking).
#pragma once

#include "internal/collections/collections.h"
#include "internal/vfs/vfs.h"

#include <memory>
#include <string>

namespace tsc::vfs::trackingvfs {

// FS wraps a vfs.FS and records every path accessed via read-like
// operations. Write operations (WriteFile, Remove, Chtimes) are not
// tracked since they represent outputs, not dependencies.
struct FS final : vfs::FS {
	vfs::FS* Inner;
	collections::SyncSet<std::string> SeenFiles;

	explicit FS(vfs::FS* inner) : Inner(inner) {}

	std::pair<std::string, bool>
	ReadFile(const std::string& path) override {
		SeenFiles.Add(path);
		return Inner->ReadFile(path);
	}

	bool FileExists(const std::string& path) override {
		SeenFiles.Add(path);
		return Inner->FileExists(path);
	}

	bool UseCaseSensitiveFileNames() override {
		return Inner->UseCaseSensitiveFileNames();
	}

	Error WriteFile(const std::string& path,
	                const std::string& data) override {
		return Inner->WriteFile(path, data);
	}

	Error AppendFile(const std::string& path,
	                 const std::string& data) override {
		return Inner->AppendFile(path, data);
	}

	Error Remove(const std::string& path) override {
		return Inner->Remove(path);
	}

	Error Chtimes(const std::string& path, TimePoint aTime,
	              TimePoint mTime) override {
		return Inner->Chtimes(path, aTime, mTime);
	}

	bool DirectoryExists(const std::string& path) override {
		SeenFiles.Add(path);
		return Inner->DirectoryExists(path);
	}

	Entries GetAccessibleEntries(const std::string& path) override {
		SeenFiles.Add(path);
		return Inner->GetAccessibleEntries(path);
	}

	std::shared_ptr<FileInfo> Stat(const std::string& path) override {
		SeenFiles.Add(path);
		return Inner->Stat(path);
	}

	std::string Realpath(const std::string& path) override {
		SeenFiles.Add(path);
		return Inner->Realpath(path);
	}
};

} // namespace tsc::vfs::trackingvfs
