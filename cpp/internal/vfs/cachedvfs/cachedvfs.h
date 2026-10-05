// cachedvfs.h — port of tsc/internal/vfs/cachedvfs/cachedvfs.go: a
// memoizing vfs::FS wrapper.
#pragma once

#include "internal/collections/collections.h"
#include "internal/vfs/vfs.h"

#include <atomic>
#include <memory>
#include <string>

namespace tsc::vfs::cachedvfs {

// FS — cachedvfs.go. Caches read-only lookups behind an enabled flag.
struct FS final : vfs::FS {
	vfs::FS* fs;
	std::atomic<bool> enabled{false};

	collections::SyncMap<std::string, bool> directoryExistsCache;
	collections::SyncMap<std::string, bool> fileExistsCache;
	collections::SyncMap<std::string, vfs::Entries>
	    getAccessibleEntriesCache;
	collections::SyncMap<std::string, std::string> realpathCache;
	collections::SyncMap<std::string, std::shared_ptr<FileInfo>> statCache;

	explicit FS(vfs::FS* fs) : fs(fs) { enabled.store(true); }

	// DisableAndClearCache — disable caching and drop cached entries.
	void DisableAndClearCache() {
		bool expected = true;
		if (enabled.compare_exchange_strong(expected, false)) {
			ClearCache();
		}
	}

	void Enable() { enabled.store(true); }

	void ClearCache() {
		directoryExistsCache.Clear();
		fileExistsCache.Clear();
		getAccessibleEntriesCache.Clear();
		realpathCache.Clear();
		statCache.Clear();
	}

	bool DirectoryExists(const std::string& path) override {
		if (enabled.load()) {
			if (auto [ret, ok] = directoryExistsCache.Load(path); ok) {
				return ret;
			}
		}

		auto ret = fs->DirectoryExists(path);

		if (enabled.load()) {
			directoryExistsCache.Store(path, ret);
		}

		return ret;
	}

	bool FileExists(const std::string& path) override {
		if (enabled.load()) {
			if (auto [ret, ok] = fileExistsCache.Load(path); ok) {
				return ret;
			}
		}

		auto ret = fs->FileExists(path);

		if (enabled.load()) {
			fileExistsCache.Store(path, ret);
		}

		return ret;
	}

	Entries GetAccessibleEntries(const std::string& path) override {
		if (enabled.load()) {
			if (auto [ret, ok] = getAccessibleEntriesCache.Load(path);
			    ok) {
				return ret;
			}
		}

		auto ret = fs->GetAccessibleEntries(path);

		if (enabled.load()) {
			getAccessibleEntriesCache.Store(path, ret);
		}

		return ret;
	}

	std::pair<std::string, bool>
	ReadFile(const std::string& path) override {
		return fs->ReadFile(path);
	}

	std::string Realpath(const std::string& path) override {
		if (enabled.load()) {
			if (auto [ret, ok] = realpathCache.Load(path); ok) {
				return ret;
			}
		}

		auto ret = fs->Realpath(path);

		if (enabled.load()) {
			realpathCache.Store(path, ret);
		}

		return ret;
	}

	Error Remove(const std::string& path) override {
		return fs->Remove(path);
	}

	Error Chtimes(const std::string& path, TimePoint aTime,
	              TimePoint mTime) override {
		return fs->Chtimes(path, aTime, mTime);
	}

	std::shared_ptr<FileInfo> Stat(const std::string& path) override {
		if (enabled.load()) {
			if (auto [ret, ok] = statCache.Load(path); ok) {
				return ret;
			}
		}

		auto ret = fs->Stat(path);

		if (enabled.load()) {
			statCache.Store(path, ret);
		}

		return ret;
	}

	bool UseCaseSensitiveFileNames() override {
		return fs->UseCaseSensitiveFileNames();
	}

	Error WriteFile(const std::string& path,
	                const std::string& data) override {
		return fs->WriteFile(path, data);
	}

	Error AppendFile(const std::string& path,
	                 const std::string& data) override {
		return fs->AppendFile(path, data);
	}
};

// From — cachedvfs.go From: wrap an FS with caching (enabled on).
inline std::shared_ptr<FS> From(vfs::FS* fs) {
	return std::make_shared<FS>(fs);
}

} // namespace tsc::vfs::cachedvfs
