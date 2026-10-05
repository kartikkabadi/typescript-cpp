// wrapvfs.h — port of tsc/internal/vfs/wrapvfs/wrapvfs.go: a vfs::FS with
// selectively replaced methods.
#pragma once

#include "internal/vfs/vfs.h"

#include <functional>
#include <memory>
#include <string>

namespace tsc::vfs::wrapvfs {

// Replacements — wrapvfs.go. A null function falls through to the wrapped
// FS.
struct Replacements {
	std::function<bool()> UseCaseSensitiveFileNames;
	std::function<bool(const std::string&)> FileExists;
	std::function<std::pair<std::string, bool>(const std::string&)>
	    ReadFile;
	std::function<Error(const std::string&, const std::string&)>
	    WriteFile;
	std::function<Error(const std::string&, const std::string&)>
	    AppendFile;
	std::function<Error(const std::string&)> Remove;
	std::function<Error(const std::string&, TimePoint, TimePoint)>
	    Chtimes;
	std::function<bool(const std::string&)> DirectoryExists;
	std::function<Entries(const std::string&)> GetAccessibleEntries;
	std::function<std::shared_ptr<FileInfo>(const std::string&)> Stat;
	std::function<std::string(const std::string&)> Realpath;
};

// wrappedFS — wrapvfs.go.
struct wrappedFS final : vfs::FS {
	std::shared_ptr<vfs::FS> fs;
	Replacements replacements;

	wrappedFS(std::shared_ptr<vfs::FS> fs, Replacements replacements)
	    : fs(std::move(fs)), replacements(std::move(replacements)) {}

	bool UseCaseSensitiveFileNames() override {
		if (replacements.UseCaseSensitiveFileNames) {
			return replacements.UseCaseSensitiveFileNames();
		}
		return fs->UseCaseSensitiveFileNames();
	}

	bool FileExists(const std::string& path) override {
		if (replacements.FileExists) {
			return replacements.FileExists(path);
		}
		return fs->FileExists(path);
	}

	std::pair<std::string, bool>
	ReadFile(const std::string& path) override {
		if (replacements.ReadFile) {
			return replacements.ReadFile(path);
		}
		return fs->ReadFile(path);
	}

	Error WriteFile(const std::string& path,
	                const std::string& data) override {
		if (replacements.WriteFile) {
			return replacements.WriteFile(path, data);
		}
		return fs->WriteFile(path, data);
	}

	Error AppendFile(const std::string& path,
	                 const std::string& data) override {
		if (replacements.AppendFile) {
			return replacements.AppendFile(path, data);
		}
		return fs->AppendFile(path, data);
	}

	Error Remove(const std::string& path) override {
		if (replacements.Remove) {
			return replacements.Remove(path);
		}
		return fs->Remove(path);
	}

	Error Chtimes(const std::string& path, TimePoint aTime,
	              TimePoint mTime) override {
		if (replacements.Chtimes) {
			return replacements.Chtimes(path, aTime, mTime);
		}
		return fs->Chtimes(path, aTime, mTime);
	}

	bool DirectoryExists(const std::string& path) override {
		if (replacements.DirectoryExists) {
			return replacements.DirectoryExists(path);
		}
		return fs->DirectoryExists(path);
	}

	Entries GetAccessibleEntries(const std::string& path) override {
		if (replacements.GetAccessibleEntries) {
			return replacements.GetAccessibleEntries(path);
		}
		return fs->GetAccessibleEntries(path);
	}

	std::shared_ptr<FileInfo> Stat(const std::string& path) override {
		if (replacements.Stat) {
			return replacements.Stat(path);
		}
		return fs->Stat(path);
	}

	std::string Realpath(const std::string& path) override {
		if (replacements.Realpath) {
			return replacements.Realpath(path);
		}
		return fs->Realpath(path);
	}
};

// Wrap — wrapvfs.go Wrap.
inline std::shared_ptr<vfs::FS> Wrap(std::shared_ptr<vfs::FS> fs,
                                     Replacements replacements) {
	return std::make_shared<wrappedFS>(std::move(fs),
	                                   std::move(replacements));
}

} // namespace tsc::vfs::wrapvfs
