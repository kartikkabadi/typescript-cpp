// Port of tsc/internal/project/snapshotfs_test.go (internal package test).
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/project/autoimport.h"
#include "internal/project/dirty/dirty.h"
#include "internal/project/filechange.h"
#include "internal/project/overlayfs.h"
#include "internal/project/snapshotfs.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace collections = tsc::collections;
namespace dirty = tsc::dirty;
namespace gostd = tsc::gostd;
namespace vfs = tsc::vfs;
namespace lsproto = tsc::lsp::lsproto;
namespace project = tsc::project;
namespace tspath = tsc::tspath;
namespace vfstest = tsc::vfs::vfstest;
using tsc::gostd::testing::T;

using project::FileChangeSummary;
using project::SnapshotFS;
using project::cachedFile;
using project::Overlay;
using project::snapshotFSBuilder;
using project::sourceFS;

static tspath::Path toPath(const std::string& fileName) {
	return tspath::Path(fileName);
}

using overlayMap = std::unordered_map<tspath::Path, Overlay*>;
using cacheFileMap = std::unordered_map<tspath::Path, cachedFile*>;
using cacheDirMap =
    std::unordered_map<tspath::Path,
                       dirty::CloneableMap<tspath::Path, std::string>>;
using aliasMap =
    std::unordered_map<tspath::Path, project::realpathAliasSet*>;
using fileMap = std::unordered_map<std::string, vfstest::MapFileInput>;
using openFilesMap =
    std::unordered_map<tspath::Path, project::FileHandle*>;

static snapshotFSBuilder* newSnapshotFSBuilder(
	vfs::FS* fs, overlayMap overlays, cacheFileMap cacheFiles,
	cacheDirMap cacheDirectories, aliasMap nodeModulesRealpathAliases) {
	auto* layered = project::layerOverlayFileSystem(
	    fs, std::move(overlays), lsproto::PositionEncodingKindUTF16,
	    toPath);
	return project::newSnapshotFSBuilderFromSource(
	    layered, std::move(cacheFiles), std::move(cacheDirectories),
	    std::move(nodeModulesRealpathAliases), toPath);
}

static project::LayeredFileSystem* newTestLayeredFileSystem(vfs::FS* fs) {
	return project::newOverlayFS(fs, {}, lsproto::PositionEncodingKindUTF16,
	                             toPath);
}

static cachedFile* newCachedFileH(const std::string& fileName,
                                  const std::string& content) {
	return new cachedFile(fileName, content);
}

static SnapshotFS* newTestSnapshotFS(
	project::LayeredFileSystem* fs, cacheFileMap cacheFiles = {},
	cacheDirMap cacheDirectories = {}) {
	auto* snapshot = new SnapshotFS();
	snapshot->toPath = toPath;
	snapshot->fs = fs;
	snapshot->cacheFiles = std::move(cacheFiles);
	snapshot->cacheDirectories = std::move(cacheDirectories);
	return snapshot;
}

// countingHandleFileSystem — snapshotfs_test.go.
struct countingHandleFileSystem : project::LayeredFileSystem {
	project::LayeredFileSystem* inner;
	std::string content;
	int getFileByPathCalls = 0;
	int readFileCalls = 0;

	explicit countingHandleFileSystem(project::LayeredFileSystem* inner)
	    : inner(inner) {}

	project::FileHandle* GetFile(const std::string& fileName) override {
		return GetFileByPath(fileName, tspath::Path(fileName));
	}
	project::FileHandle*
	GetFileByPath(const std::string& fileName,
	              const tspath::Path& path) override {
		getFileByPathCalls++;
		return project::newCachedFileHandle(fileName, content);
	}
	std::pair<std::string, bool>
	ReadFile(const std::string& path) override {
		readFileCalls++;
		return {content, true};
	}

	// LayeredFileSystem plumbing: delegate everything else.
	std::unordered_map<tspath::Path, Overlay*> Overlays() override {
		return inner->Overlays();
	}
	bool DirectoryExists(const std::string& path) override {
		return inner->DirectoryExists(path);
	}
	bool FileExists(const std::string& path) override {
		return inner->FileExists(path);
	}
	vfs::Entries
	GetAccessibleEntries(const std::string& path) override {
		return inner->GetAccessibleEntries(path);
	}
	vfs::Error WriteFile(const std::string& path,
	                     const std::string& data) override {
		return inner->WriteFile(path, data);
	}
	vfs::Error AppendFile(const std::string& path,
	                      const std::string& data) override {
		return inner->AppendFile(path, data);
	}
	vfs::Error Remove(const std::string& path) override {
		return inner->Remove(path);
	}
	vfs::Error Chtimes(const std::string& path, vfs::TimePoint atime,
	                   vfs::TimePoint mtime) override {
		return inner->Chtimes(path, atime, mtime);
	}
	std::shared_ptr<vfs::FileInfo>
	Stat(const std::string& path) override {
		return inner->Stat(path);
	}
	std::string Realpath(const std::string& path) override {
		return inner->Realpath(path);
	}
	bool UseCaseSensitiveFileNames() override {
		return inner->UseCaseSensitiveFileNames();
	}
};

static bool contains(const std::vector<std::string>& v,
                     const std::string& s) {
	return std::find(v.begin(), v.end(), s) != v.end();
}

void TestSnapshotFSBuilderCachesReturnedSourceHandle(T* t) {
	t->Parallel();
	auto* fileSystem = new countingHandleFileSystem(
	    newTestLayeredFileSystem(vfstest::FromMap({}, true).get()));
	fileSystem->content = "export const value = 1;";
	auto* builder = project::newSnapshotFSBuilderFromSource(
	    fileSystem, {}, {}, {}, toPath);

	auto* file = builder->GetFile("/src/index.ts");
	assert::Assert(t, file != nullptr);
	assert::Equal(t, file->Content(), fileSystem->content);
	assert::Assert(t, builder->GetFile("/src/index.ts") == file);
	assert::Equal(t, fileSystem->getFileByPathCalls, 1);
	assert::Equal(t, fileSystem->readFileCalls, 0);
}

void TestSnapshotFSBuilder(T* t) {
	t->Parallel();

	t->Run("builds directory tree on file add", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "const foo = 1;"}}, false);
		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});

		auto* fh = builder->GetFile("/src/foo.ts");
		assert::Assert(t, fh != nullptr, {"file should exist"});
		assert::Equal(t, fh->Content(), std::string("const foo = 1;"));

		auto pr = builder->Finalize();
		auto* snapshot = pr.first;
		assert::Assert(t, pr.second, {"should have changed"});

		auto srcDirIt = snapshot->cacheDirectories.find("/src");
		assert::Assert(t, srcDirIt != snapshot->cacheDirectories.end(),
		               {"/src directory should exist"});
		assert::Assert(t,
		               srcDirIt->second.count("/src/foo.ts") != 0,
		               {"/src should contain /src/foo.ts"});

		auto rootDirIt = snapshot->cacheDirectories.find("/");
		assert::Assert(t, rootDirIt != snapshot->cacheDirectories.end(),
		               {"/ directory should exist"});
		assert::Assert(t, rootDirIt->second.count("/src") != 0,
		               {"/ should contain /src"});
	});

	t->Run("builds nested directory tree", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/nested/deep/file.ts", "export const x = 1;"}},
		    false);
		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});

		auto* fh = builder->GetFile("/src/nested/deep/file.ts");
		assert::Assert(t, fh != nullptr, {"file should exist"});

		auto pr = builder->Finalize();
		auto* snapshot = pr.first;
		assert::Assert(t, pr.second, {"should have changed"});

		assert::Assert(
		    t, snapshot->cacheDirectories["/src/nested/deep"].count(
		           "/src/nested/deep/file.ts") != 0);
		assert::Assert(t, snapshot->cacheDirectories["/src/nested"].count(
		                      "/src/nested/deep") != 0);
		assert::Assert(t, snapshot->cacheDirectories["/src"].count(
		                      "/src/nested") != 0);
		assert::Assert(
		    t, snapshot->cacheDirectories["/"].count("/src") != 0);
	});

	t->Run("removes directory entries on file delete", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "const foo = 1;"}}, false);

		cacheFileMap existingCacheFiles{
		    {"/src/foo.ts", newCachedFileH("/src/foo.ts", "const foo = 1;")},
		};
		cacheDirMap existingDirs{
		    {"/", {{"/src", "src"}}},
		    {"/src", {{"/src/foo.ts", "foo.ts"}}},
		};

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, std::move(existingCacheFiles),
		    std::move(existingDirs), {});

		if (auto [entry, ok] =
		        builder->cacheFiles->Load("/src/foo.ts");
		    ok) {
			entry->Delete();
		}

		auto pr = builder->Finalize();
		auto* snapshot = pr.first;
		assert::Assert(t, pr.second, {"should have changed"});

		assert::Assert(t, snapshot->cacheFiles.count("/src/foo.ts") == 0,
		               {"file should be deleted"});
		assert::Assert(t,
		               snapshot->cacheDirectories.count("/src") == 0,
		               {"/src directory should be removed"});
		assert::Assert(t, snapshot->cacheDirectories.count("/") == 0,
		               {"root directory should be removed"});
	});

	t->Run("removes only empty directories on file delete", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "const foo = 1;"},
		            {"/src/bar.ts", "const bar = 2;"}},
		    false);

		cacheFileMap existingCacheFiles{
		    {"/src/foo.ts", newCachedFileH("/src/foo.ts", "const foo = 1;")},
		    {"/src/bar.ts", newCachedFileH("/src/bar.ts", "const bar = 2;")},
		};
		cacheDirMap existingDirs{
		    {"/", {{"/src", "src"}}},
		    {"/src",
		     {{"/src/foo.ts", "foo.ts"}, {"/src/bar.ts", "bar.ts"}}},
		};

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, std::move(existingCacheFiles),
		    std::move(existingDirs), {});

		if (auto [entry, ok] =
		        builder->cacheFiles->Load("/src/foo.ts");
		    ok) {
			entry->Delete();
		}

		auto pr = builder->Finalize();
		auto* snapshot = pr.first;
		assert::Assert(t, pr.second, {"should have changed"});

		assert::Assert(t, snapshot->cacheFiles.count("/src/foo.ts") == 0,
		               {"foo.ts should be deleted"});
		assert::Assert(t, snapshot->cacheFiles.count("/src/bar.ts") != 0,
		               {"bar.ts should still exist"});

		auto srcDirIt = snapshot->cacheDirectories.find("/src");
		assert::Assert(t, srcDirIt != snapshot->cacheDirectories.end(),
		               {"/src directory should still exist"});
		assert::Assert(t, srcDirIt->second.count("/src/foo.ts") == 0,
		               {"/src should not contain foo.ts"});
		assert::Assert(t, srcDirIt->second.count("/src/bar.ts") != 0,
		               {"/src should contain bar.ts"});

		auto rootDirIt = snapshot->cacheDirectories.find("/");
		assert::Assert(t, rootDirIt != snapshot->cacheDirectories.end(),
		               {"root directory should still exist"});
		assert::Assert(t, rootDirIt->second.count("/src") != 0,
		               {"root should contain /src"});
	});

	t->Run("adds file to existing directory", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "const foo = 1;"},
		            {"/src/bar.ts", "const bar = 2;"}},
		    false);

		cacheFileMap existingCacheFiles{
		    {"/src/foo.ts", newCachedFileH("/src/foo.ts", "const foo = 1;")},
		};
		cacheDirMap existingDirs{
		    {"/", {{"/src", "src"}}},
		    {"/src", {{"/src/foo.ts", "foo.ts"}}},
		};

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, std::move(existingCacheFiles),
		    std::move(existingDirs), {});

		auto* fh = builder->GetFile("/src/bar.ts");
		assert::Assert(t, fh != nullptr, {"bar.ts should exist"});

		auto pr = builder->Finalize();
		auto* snapshot = pr.first;
		assert::Assert(t, pr.second, {"should have changed"});

		auto& srcDir = snapshot->cacheDirectories["/src"];
		assert::Assert(t, srcDir.count("/src/foo.ts") != 0,
		               {"/src should contain foo.ts"});
		assert::Assert(t, srcDir.count("/src/bar.ts") != 0,
		               {"/src should contain bar.ts"});
	});

	t->Run("no change when no files added or deleted", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "const foo = 1;"}}, false);

		cacheFileMap existingCacheFiles{
		    {"/src/foo.ts", newCachedFileH("/src/foo.ts", "const foo = 1;")},
		};
		cacheDirMap existingDirs{
		    {"/", {{"/src", "src"}}},
		    {"/src", {{"/src/foo.ts", "foo.ts"}}},
		};

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, std::move(existingCacheFiles),
		    std::move(existingDirs), {});

		auto pr = builder->Finalize();
		auto* snapshot = pr.first;
		assert::Assert(t, !pr.second, {"should not have changed"});

		auto& srcDir = snapshot->cacheDirectories["/src"];
		assert::Assert(t, srcDir.count("/src/foo.ts") != 0);
	});

	t->Run("overlay files are returned over disk files", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "const foo = 1;"}}, false);

		overlayMap overlays{
		    {"/src/foo.ts",
		     project::newOverlay("/src/foo.ts", "const foo = 999;", 0,
		                         tsc::ScriptKind::Unknown)},
		};

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), std::move(overlays), {}, {}, {});

		auto* fh = builder->GetFile("/src/foo.ts");
		assert::Assert(t, fh != nullptr);
		assert::Equal(t, fh->Content(),
		              std::string("const foo = 999;"));
	});

	t->Run("multiple files added and deleted in single cycle", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/a.ts", "const a = 1;"},
		            {"/src/b.ts", "const b = 2;"},
		            {"/lib/utils.ts", "export const util = 1;"},
		            {"/lib/helpers.ts", "export const helper = 1;"},
		            {"/other/single.ts", "const single = 1;"}},
		    false);

		cacheFileMap existingCacheFiles{
		    {"/src/a.ts", newCachedFileH("/src/a.ts", "const a = 1;")},
		    {"/other/single.ts",
		     newCachedFileH("/other/single.ts", "const single = 1;")},
		};
		cacheDirMap existingDirs{
		    {"/", {{"/src", "src"}, {"/other", "other"}}},
		    {"/src", {{"/src/a.ts", "a.ts"}}},
		    {"/other", {{"/other/single.ts", "single.ts"}}},
		};

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, std::move(existingCacheFiles),
		    std::move(existingDirs), {});

		auto* fh = builder->GetFile("/src/b.ts");
		assert::Assert(t, fh != nullptr);
		fh = builder->GetFile("/lib/utils.ts");
		assert::Assert(t, fh != nullptr);
		fh = builder->GetFile("/lib/helpers.ts");
		assert::Assert(t, fh != nullptr);

		if (auto [entry, ok] = builder->cacheFiles->Load("/src/a.ts");
		    ok) {
			entry->Delete();
		}
		if (auto [entry, ok] =
		        builder->cacheFiles->Load("/other/single.ts");
		    ok) {
			entry->Delete();
		}

		auto pr = builder->Finalize();
		auto* snapshot = pr.first;
		assert::Assert(t, pr.second, {"should have changed"});

		assert::Assert(t, snapshot->cacheFiles.count("/src/a.ts") == 0,
		               {"/src/a.ts should be deleted"});
		assert::Assert(t,
		               snapshot->cacheFiles.count("/other/single.ts") == 0,
		               {"/other/single.ts should be deleted"});

		assert::Assert(t, snapshot->cacheFiles.count("/src/b.ts") != 0,
		               {"/src/b.ts should exist"});
		assert::Assert(
		    t, snapshot->cacheFiles.count("/lib/utils.ts") != 0,
		    {"/lib/utils.ts should exist"});
		assert::Assert(
		    t, snapshot->cacheFiles.count("/lib/helpers.ts") != 0,
		    {"/lib/helpers.ts should exist"});

		assert::Assert(t,
		               snapshot->cacheDirectories.count("/other") == 0,
		               {"/other directory should be removed"});

		auto srcDirIt = snapshot->cacheDirectories.find("/src");
		assert::Assert(t, srcDirIt != snapshot->cacheDirectories.end(),
		               {"/src directory should exist"});
		assert::Assert(t, srcDirIt->second.count("/src/a.ts") == 0,
		               {"/src should not contain a.ts"});
		assert::Assert(t, srcDirIt->second.count("/src/b.ts") != 0,
		               {"/src should contain b.ts"});

		auto libDirIt = snapshot->cacheDirectories.find("/lib");
		assert::Assert(t, libDirIt != snapshot->cacheDirectories.end(),
		               {"/lib directory should exist"});
		assert::Assert(t, libDirIt->second.count("/lib/utils.ts") != 0,
		               {"/lib should contain utils.ts"});
		assert::Assert(
		    t, libDirIt->second.count("/lib/helpers.ts") != 0,
		    {"/lib should contain helpers.ts"});

		auto& rootDir = snapshot->cacheDirectories["/"];
		assert::Assert(t, rootDir.count("/src") != 0,
		               {"root should contain /src"});
		assert::Assert(t, rootDir.count("/lib") != 0,
		               {"root should contain /lib"});
		assert::Assert(t, rootDir.count("/other") == 0,
		               {"root should not contain /other"});
	});

	t->Run("overlay directories are computed from overlays", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(fileMap{}, false);

		overlayMap overlays{
		    {"/src/overlay.ts",
		     project::newOverlay("/src/overlay.ts", "const x = 1;", 0,
		                         tsc::ScriptKind::Unknown)},
		    {"/src/nested/deep.ts",
		     project::newOverlay("/src/nested/deep.ts", "const y = 2;", 0,
		                         tsc::ScriptKind::Unknown)},
		};

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), std::move(overlays), {}, {}, {});

		auto srcEntries = builder->GetAccessibleEntries("/src");
		assert::Assert(t, contains(srcEntries.files, "overlay.ts"),
		               {"/src should contain overlay.ts"});
		assert::Assert(t, contains(srcEntries.directories, "nested"),
		               {"/src should contain nested/"});

		auto nestedEntries =
		    builder->GetAccessibleEntries("/src/nested");
		assert::Assert(t, contains(nestedEntries.files, "deep.ts"),
		               {"/src/nested should contain deep.ts"});

		auto rootEntries = builder->GetAccessibleEntries("/");
		assert::Assert(t, contains(rootEntries.directories, "src"),
		               {"/ should contain /src"});
	});

	t->Run("GetAccessibleEntries combines disk and overlay", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/disk.ts", "const disk = 1;"}}, false);

		overlayMap overlays{
		    {"/src/overlay.ts",
		     project::newOverlay("/src/overlay.ts", "const overlay = 1;",
		                         0, tsc::ScriptKind::Unknown)},
		};

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), std::move(overlays), {}, {}, {});

		auto entries = builder->GetAccessibleEntries("/src");

		assert::Assert(t, contains(entries.files, "disk.ts"),
		               {"should contain disk.ts"});
		assert::Assert(t, contains(entries.files, "overlay.ts"),
		               {"should contain overlay.ts"});
	});

	t->Run("GetAccessibleEntries is safe under concurrent calls",
	       [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/a.ts", ""},
		            {"/src/b.ts", ""},
		            {"/src/c.ts", ""},
		            {"/src/d.ts", ""},
		            {"/src/e.ts", ""}},
		    false);

		overlayMap overlays{
		    {"/src/overlay.ts",
		     project::newOverlay("/src/overlay.ts", "", 0,
		                         tsc::ScriptKind::Unknown)},
		};

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), std::move(overlays), {}, {}, {});

		builder->fs->GetAccessibleEntries("/src");

		std::atomic<int> started{0};
		std::atomic<bool> go{false};
		std::vector<std::thread> threads;
		threads.reserve(50);
		for (int i = 0; i < 50; i++) {
			threads.emplace_back([&] {
				started++;
				while (!go.load()) {
				}
				builder->GetAccessibleEntries("/src");
			});
		}
		while (started.load() < 50) {
		}
		go = true;
		for (auto& th : threads) {
			th.join();
		}

		// Sanity: the overlay is still reported after all the concurrent
		// calls.
		auto entries = builder->GetAccessibleEntries("/src");
		assert::Assert(t, contains(entries.files, "overlay.ts"),
		               {"should contain overlay.ts"});
	});
}

void TestSnapshotFS(T* t) {
	t->Parallel();

	t->Run("GetFile returns overlay file", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "disk content"}}, false);

		overlayMap overlays{
		    {"/src/foo.ts",
		     project::newOverlay("/src/foo.ts", "overlay content", 0,
		                         tsc::ScriptKind::Unknown)},
		};
		auto* overlayfs = project::newOverlayFS(
		    testFS.get(), std::move(overlays),
		    lsproto::PositionEncodingKindUTF16, toPath);

		auto* snapshot = newTestSnapshotFS(overlayfs);

		auto* fh = snapshot->GetFile("/src/foo.ts");
		assert::Assert(t, fh != nullptr);
		assert::Equal(t, fh->Content(), std::string("overlay content"));
	});

	t->Run("GetFile returns disk file when not in overlay", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "disk content"}}, false);

		cacheFileMap cacheFiles{
		    {"/src/foo.ts",
		     newCachedFileH("/src/foo.ts", "disk content")},
		};

		auto* snapshot = newTestSnapshotFS(
		    newTestLayeredFileSystem(testFS.get()),
		    std::move(cacheFiles));

		auto* fh = snapshot->GetFile("/src/foo.ts");
		assert::Assert(t, fh != nullptr);
		assert::Equal(t, fh->Content(), std::string("disk content"));
	});

	t->Run("GetFile reads from fs when not cached", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "fs content"}}, false);

		auto* snapshot = newTestSnapshotFS(
		    newTestLayeredFileSystem(testFS.get()));

		auto* fh = snapshot->GetFile("/src/foo.ts");
		assert::Assert(t, fh != nullptr);
		assert::Equal(t, fh->Content(), std::string("fs content"));
	});

	t->Run("GetFile returns nil for non-existent file", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(fileMap{}, false);

		auto* snapshot = newTestSnapshotFS(
		    newTestLayeredFileSystem(testFS.get()));

		auto* fh = snapshot->GetFile("/src/nonexistent.ts");
		assert::Assert(t, fh == nullptr,
		               {"should return nil for non-existent file"});
	});

	t->Run("isOpenFile returns true for overlays", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(fileMap{}, false);

		overlayMap overlays{
		    {"/src/foo.ts",
		     project::newOverlay("/src/foo.ts", "overlay content", 0,
		                         tsc::ScriptKind::Unknown)},
		};

		auto* overlayfs = project::newOverlayFS(
		    testFS.get(), std::move(overlays),
		    lsproto::PositionEncodingKindUTF16, toPath);
		assert::Assert(
		    t, overlayfs->GetFile("/src/foo.ts")->IsOverlay(),
		    {"overlay file should be open"});
		assert::Assert(t, overlayfs->GetFile("/src/bar.ts") == nullptr,
		               {"non-overlay file should not be open"});
	});

	t->Run("GetFileByPath uses provided path", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "disk content"}}, false);

		overlayMap overlays{
		    {"/src/foo.ts",
		     project::newOverlay("/src/foo.ts", "overlay content", 0,
		                         tsc::ScriptKind::Unknown)},
		};
		auto* overlayfs = project::newOverlayFS(
		    testFS.get(), std::move(overlays),
		    lsproto::PositionEncodingKindUTF16, toPath);

		auto* snapshot = newTestSnapshotFS(overlayfs);

		auto* fh = snapshot->GetFileByPath("/src/foo.ts",
		                                   "/src/foo.ts");
		assert::Assert(t, fh != nullptr);
		assert::Equal(t, fh->Content(), std::string("overlay content"));
	});

	t->Run("GetAccessibleEntries combines disk and overlay directories",
	       [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(fileMap{}, false);

		overlayMap overlays{
		    {"/src/overlay.ts",
		     project::newOverlay("/src/overlay.ts", "overlay content", 0,
		                         tsc::ScriptKind::Unknown)},
		};
		auto* overlayfs = project::newOverlayFS(
		    testFS.get(), std::move(overlays),
		    lsproto::PositionEncodingKindUTF16, toPath);
		cacheFileMap cacheFiles{
		    {"/src/disk.ts",
		     newCachedFileH("/src/disk.ts", "disk content")},
		};
		cacheDirMap cacheDirectories{
		    {"/", {{"/src", "src"}}},
		    {"/src", {{"/src/disk.ts", "disk.ts"}}},
		};

		auto* snapshot = newTestSnapshotFS(
		    overlayfs, std::move(cacheFiles),
		    std::move(cacheDirectories));

		auto entries = snapshot->GetAccessibleEntries("/src");

		assert::Assert(t, contains(entries.files, "disk.ts"),
		               {"should contain disk.ts"});
		assert::Assert(t, contains(entries.files, "overlay.ts"),
		               {"should contain overlay.ts"});
	});
}

void TestSourceFS(T* t) {
	t->Parallel();

	t->Run("tracks files when tracking enabled", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "content"}}, false);

		auto* snapshot = newTestSnapshotFS(
		    newTestLayeredFileSystem(testFS.get()));

		auto* sfs = project::newSourceFS(true, snapshot, toPath);

		assert::Assert(t, !sfs->SeenFile("/src/foo.ts"));

		auto* fh = sfs->GetFile("/src/foo.ts");
		assert::Assert(t, fh != nullptr);

		assert::Assert(t, sfs->SeenFile("/src/foo.ts"));
	});

	t->Run("does not track files when tracking disabled", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "content"}}, false);

		auto* snapshot = newTestSnapshotFS(
		    newTestLayeredFileSystem(testFS.get()));

		auto* sfs = project::newSourceFS(false, snapshot, toPath);

		auto* fh = sfs->GetFile("/src/foo.ts");
		assert::Assert(t, fh != nullptr);

		assert::Assert(t, !sfs->SeenFile("/src/foo.ts"));
	});

	t->Run("DisableTracking stops tracking", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "content"}, {"/src/bar.ts", "content"}},
		    false);

		auto* snapshot = newTestSnapshotFS(
		    newTestLayeredFileSystem(testFS.get()));

		auto* sfs = project::newSourceFS(true, snapshot, toPath);

		sfs->GetFile("/src/foo.ts");
		assert::Assert(t, sfs->SeenFile("/src/foo.ts"));

		sfs->DisableTracking();

		sfs->GetFile("/src/bar.ts");
		assert::Assert(t, !sfs->SeenFile("/src/bar.ts"));
	});

	t->Run("FileExists returns true for files in source", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "content"}}, false);

		auto* snapshot = newTestSnapshotFS(
		    newTestLayeredFileSystem(testFS.get()));

		auto* sfs = project::newSourceFS(false, snapshot, toPath);

		assert::Assert(t, sfs->FileExists("/src/foo.ts"));
		assert::Assert(t, !sfs->FileExists("/src/nonexistent.ts"));
	});

	t->Run("ReadFile returns content for files in source", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "file content"}}, false);

		auto* snapshot = newTestSnapshotFS(
		    newTestLayeredFileSystem(testFS.get()));

		auto* sfs = project::newSourceFS(false, snapshot, toPath);

		auto [content, ok] = sfs->ReadFile("/src/foo.ts");
		assert::Assert(t, ok);
		assert::Equal(t, content, std::string("file content"));

		auto pr2 = sfs->ReadFile("/src/nonexistent.ts");
		assert::Assert(t, !pr2.second);
	});
}

void TestAutoImportBuilderFS(T* t) {
	t->Parallel();

	t->Run(
	    "symlink cache mismatch: file cached at symlink path, missed at realpath after deletion",
	    [](T* t) {
		t->Parallel();

		// Create a VFS with a real file and a symlinked directory pointing
		// to it. /real/pkg/index.d.ts is the real file.
		// /project/node_modules/pkg is a symlink to /real/pkg.
		auto testFS = vfstest::FromMap(
		    fileMap{{"/real/pkg/index.d.ts",
		             "export declare const x: number;"},
		            {"/project/node_modules/pkg",
		             vfstest::Symlink("/real/pkg")}},
		    true);

		// Verify symlink works as expected
		std::string symlinkPath =
		    "/project/node_modules/pkg/index.d.ts";
		auto realpathPath = testFS->Realpath(symlinkPath);
		assert::Equal(t, realpathPath,
		              std::string("/real/pkg/index.d.ts"));

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});

		auto* autoImportFS = new project::autoImportBuilderFS();
		autoImportFS->snapshotFSBuilder_ = builder;

		// Step 1: Read the file via its symlink path. This caches the file
		// in untrackedFiles at the symlink path key.
		auto* fh = autoImportFS->GetFile(symlinkPath);
		assert::Assert(t, fh != nullptr,
		               {"File should be readable via symlink path"});
		assert::Equal(t, fh->Content(),
		              std::string("export declare const x: number;"));

		// Step 2: Simulate a file deletion from disk.
		auto err = testFS->Remove("/real/pkg/index.d.ts");
		assert::Assert(t, err.impl() == nullptr);

		// Step 3: Request the file by its realpath. This bypasses the
		// symlink-path cache entry because the realpath has a different
		// key.
		auto* fh2 = autoImportFS->GetFile(realpathPath);
		assert::Assert(
		    t, fh2 == nullptr,
		    {"File should be nil when accessed by realpath after "
		     "deletion from disk"});
	});
}

void TestRealpathAliasLifecycle(T* t) {
	t->Parallel();

	t->Run("alias recorded when reading symlinked node_modules file",
	       [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{
		        {"/project/node_modules/mylib",
		         vfstest::Symlink("/packages/mylib")},
		        {"/packages/mylib/package.json",
		         R"({"name": "mylib", "main": "index.js"})"},
		        {"/packages/mylib/index.d.ts",
		         "export declare const x: number;"},
		        {"/project/node_modules/nolink/package.json",
		         R"({"name": "nolink"})"},
		    },
		    false);

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});

		// Read a file through the symlink — should record an alias.
		auto* fh =
		    builder->GetFile("/project/node_modules/mylib/package.json");
		assert::Assert(t, fh != nullptr);
		assert::Equal(t, fh->Content(),
		              std::string(
		                  R"({"name": "mylib", "main": "index.js"})"));

		// Read a non-symlinked node_modules file — should NOT record an
		// alias.
		auto* fh2 = builder->GetFile(
		    "/project/node_modules/nolink/package.json");
		assert::Assert(t, fh2 != nullptr);

		auto pr = builder->Finalize();
		auto* snapshot = pr.first;

		auto aliasIt = snapshot->nodeModulesRealpathAliases.find(
		    "/packages/mylib/package.json");
		assert::Assert(
		    t, aliasIt != snapshot->nodeModulesRealpathAliases.end(),
		    {"alias should exist for realpath of symlinked file"});
		assert::Assert(t,
		               aliasIt->second->paths.Has(
		                   "/project/node_modules/mylib/package.json"));

		assert::Assert(
		    t,
		    snapshot->nodeModulesRealpathAliases.count(
		        "/project/node_modules/nolink/package.json") == 0,
		    {"no alias should exist for non-symlinked file"});
	});

	t->Run("no alias recorded for files outside node_modules", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/link", vfstest::Symlink("/elsewhere")},
		            {"/elsewhere/index.ts", "export const x = 1;"}},
		    false);

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});

		auto* fh = builder->GetFile("/project/link/index.ts");
		assert::Assert(t, fh != nullptr);

		auto pr = builder->Finalize();
		assert::Equal(
		    t,
		    int64_t(pr.first->nodeModulesRealpathAliases.size()),
		    int64_t(0));
	});

	t->Run("aliases carried over across snapshots", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/node_modules/mylib",
		             vfstest::Symlink("/packages/mylib")},
		            {"/packages/mylib/package.json",
		             R"({"name": "mylib"})"}},
		    false);

		// Build first snapshot.
		auto* builder1 = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});
		builder1->GetFile("/project/node_modules/mylib/package.json");
		auto pr1 = builder1->Finalize();
		auto* snapshot1 = pr1.first;

		// Build second snapshot from the first, without reading the file
		// again.
		auto* builder2 = newSnapshotFSBuilder(
		    testFS.get(), {}, snapshot1->cacheFiles,
		    snapshot1->cacheDirectories,
		    snapshot1->nodeModulesRealpathAliases);
		auto pr2 = builder2->Finalize();
		auto* snapshot2 = pr2.first;

		auto aliasIt = snapshot2->nodeModulesRealpathAliases.find(
		    "/packages/mylib/package.json");
		assert::Assert(
		    t, aliasIt != snapshot2->nodeModulesRealpathAliases.end(),
		    {"alias should survive across snapshots"});
		assert::Assert(t,
		               aliasIt->second->paths.Has(
		                   "/project/node_modules/mylib/package.json"));
	});

	t->Run("alias pruned when symlinked file is deleted", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/node_modules/mylib",
		             vfstest::Symlink("/packages/mylib")},
		            {"/packages/mylib/package.json",
		             R"({"name": "mylib"})"},
		            {"/packages/mylib/index.d.ts",
		             "export declare const x: number;"}},
		    false);

		// Build first snapshot — read both files.
		auto* builder1 = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});
		builder1->GetFile("/project/node_modules/mylib/package.json");
		builder1->GetFile("/project/node_modules/mylib/index.d.ts");
		auto pr1 = builder1->Finalize();
		auto* snapshot1 = pr1.first;

		assert::Assert(
		    t,
		    snapshot1->nodeModulesRealpathAliases.count(
		        "/packages/mylib/package.json") != 0);
		assert::Assert(
		    t,
		    snapshot1->nodeModulesRealpathAliases.count(
		        "/packages/mylib/index.d.ts") != 0);

		// Build second snapshot — delete one file.
		auto* builder2 = newSnapshotFSBuilder(
		    testFS.get(), {}, snapshot1->cacheFiles,
		    snapshot1->cacheDirectories,
		    snapshot1->nodeModulesRealpathAliases);

		// Simulate deletion of index.d.ts from the disk file cache.
		if (auto [entry, ok] = builder2->cacheFiles->Load(
		        "/project/node_modules/mylib/index.d.ts");
		    ok) {
			entry->Delete();
		}

		auto pr2 = builder2->Finalize();
		auto* snapshot2 = pr2.first;

		auto aliasIt = snapshot2->nodeModulesRealpathAliases.find(
		    "/packages/mylib/package.json");
		assert::Assert(
		    t, aliasIt != snapshot2->nodeModulesRealpathAliases.end(),
		    {"package.json alias should survive"});
		assert::Assert(t,
		               aliasIt->second->paths.Has(
		                   "/project/node_modules/mylib/package.json"));

		assert::Assert(
		    t,
		    snapshot2->nodeModulesRealpathAliases.count(
		        "/packages/mylib/index.d.ts") == 0,
		    {"index.d.ts alias should be pruned after deletion"});
	});

	t->Run("multiple symlinks to same realpath", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/node_modules/mylib",
		             vfstest::Symlink("/packages/mylib")},
		            {"/project/node_modules/alias",
		             vfstest::Symlink("/packages/mylib")},
		            {"/packages/mylib/package.json",
		             R"({"name": "mylib"})"}},
		    false);

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});

		auto* fh1 =
		    builder->GetFile("/project/node_modules/mylib/package.json");
		assert::Assert(t, fh1 != nullptr);
		auto* fh2 = builder->GetFile(
		    "/project/node_modules/alias/package.json");
		assert::Assert(t, fh2 != nullptr);

		auto pr = builder->Finalize();
		auto* snapshot = pr.first;

		auto aliasIt = snapshot->nodeModulesRealpathAliases.find(
		    "/packages/mylib/package.json");
		assert::Assert(
		    t, aliasIt != snapshot->nodeModulesRealpathAliases.end(),
		    {"alias should exist"});
		assert::Assert(t,
		               aliasIt->second->paths.Has(
		                   "/project/node_modules/mylib/package.json"));
		assert::Assert(t,
		               aliasIt->second->paths.Has(
		                   "/project/node_modules/alias/package.json"));
	});

	t->Run("multiple symlinks pruned individually", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/node_modules/mylib",
		             vfstest::Symlink("/packages/mylib")},
		            {"/project/node_modules/alias",
		             vfstest::Symlink("/packages/mylib")},
		            {"/packages/mylib/package.json",
		             R"({"name": "mylib"})"}},
		    false);

		// Build first snapshot – read via both symlinks.
		auto* builder1 = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});
		builder1->GetFile("/project/node_modules/mylib/package.json");
		builder1->GetFile("/project/node_modules/alias/package.json");
		auto pr1 = builder1->Finalize();
		auto* snapshot1 = pr1.first;

		// Build second snapshot – delete ONE of the symlink disk
		// entries.
		auto* builder2 = newSnapshotFSBuilder(
		    testFS.get(), {}, snapshot1->cacheFiles,
		    snapshot1->cacheDirectories,
		    snapshot1->nodeModulesRealpathAliases);
		if (auto [entry, ok] = builder2->cacheFiles->Load(
		        "/project/node_modules/alias/package.json");
		    ok) {
			entry->Delete();
		}
		auto pr2 = builder2->Finalize();
		auto* snapshot2 = pr2.first;

		auto aliasIt = snapshot2->nodeModulesRealpathAliases.find(
		    "/packages/mylib/package.json");
		assert::Assert(
		    t, aliasIt != snapshot2->nodeModulesRealpathAliases.end(),
		    {"alias set should still exist"});
		assert::Assert(
		    t,
		    aliasIt->second->paths.Has(
		        "/project/node_modules/mylib/package.json"),
		    {"surviving symlink should remain"});
		assert::Assert(
		    t,
		    !aliasIt->second->paths.Has(
		        "/project/node_modules/alias/package.json"),
		    {"deleted symlink should be pruned"});
	});

	t->Run("expandRealpathAliases expands change events", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/node_modules/mylib",
		             vfstest::Symlink("/packages/mylib")},
		            {"/packages/mylib/package.json",
		             R"({"name": "mylib"})"}},
		    false);

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});
		builder->GetFile("/project/node_modules/mylib/package.json");
		auto pr = builder->Finalize();
		auto* snapshot = pr.first;

		FileChangeSummary change;
		change.Changed.Add("file:///packages/mylib/package.json");

		auto expanded = snapshot->expandRealpathAliases(change);

		assert::Assert(
		    t,
		    expanded.Changed.Has(
		        "file:///packages/mylib/package.json"),
		    {"original event should remain"});
		assert::Assert(
		    t,
		    expanded.Changed.Has(
		        "file:///project/node_modules/mylib/package.json"),
		    {"symlink event should be added"});
	});

	t->Run("expandRealpathAliases expands delete events", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/node_modules/mylib",
		             vfstest::Symlink("/packages/mylib")},
		            {"/packages/mylib/package.json",
		             R"({"name": "mylib"})"}},
		    false);

		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});
		builder->GetFile("/project/node_modules/mylib/package.json");
		auto pr = builder->Finalize();
		auto* snapshot = pr.first;

		FileChangeSummary change;
		change.Deleted.Add("file:///packages/mylib/package.json");

		auto expanded = snapshot->expandRealpathAliases(change);

		assert::Assert(
		    t,
		    expanded.Deleted.Has(
		        "file:///project/node_modules/mylib/package.json"),
		    {"symlink deletion should be added"});
	});

	t->Run("expandRealpathAliases is a no-op with no aliases", [](T* t) {
		t->Parallel();
		auto* snapshot = new SnapshotFS();
		snapshot->toPath = toPath;

		FileChangeSummary change;
		change.Changed.Add("file:///some/file.ts");

		auto expanded = snapshot->expandRealpathAliases(change);
		assert::Equal(t, int64_t(expanded.Changed.Len()), int64_t(1));
		assert::Assert(t,
		               expanded.Changed.Has("file:///some/file.ts"));
	});

	t->Run("markDirtyFiles invalidates symlinked file via realpath event",
	       [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/node_modules/mylib",
		             vfstest::Symlink("/packages/mylib")},
		            {"/packages/mylib/package.json",
		             R"({"name": "mylib", "main": "index.js"})"}},
		    false);

		// Build first snapshot — read the symlinked file.
		auto* builder1 = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});
		auto* fh =
		    builder1->GetFile("/project/node_modules/mylib/package.json");
		assert::Assert(t, fh != nullptr);
		assert::Equal(t, fh->Content(),
		              std::string(
		                  R"({"name": "mylib", "main": "index.js"})"));
		auto pr1 = builder1->Finalize();
		auto* snapshot1 = pr1.first;

		// Modify the real file on disk.
		auto err = testFS->WriteFile("/packages/mylib/package.json",
		                             R"({"name": "mylib"})");
		assert::Assert(t, err.impl() == nullptr);

		// Build second snapshot — simulate realpath change event,
		// expanded via aliases.
		auto* builder2 = newSnapshotFSBuilder(
		    testFS.get(), {}, snapshot1->cacheFiles,
		    snapshot1->cacheDirectories,
		    snapshot1->nodeModulesRealpathAliases);

		FileChangeSummary change;
		change.Changed.Add("file:///packages/mylib/package.json");

		// Expand the realpath event to include the symlink path.
		change = snapshot1->expandRealpathAliases(change);
		// Now mark dirty — should find the file under the symlink key.
		builder2->markDirtyFiles(change);

		// Trigger reload by reading the file.
		fh = builder2->GetFile("/project/node_modules/mylib/package.json");
		assert::Assert(t, fh != nullptr);
		assert::Equal(t, fh->Content(),
		              std::string(R"({"name": "mylib"})"));

		auto pr2 = builder2->Finalize();
		auto* snapshot2 = pr2.first;

		auto fileIt = snapshot2->cacheFiles.find(
		    "/project/node_modules/mylib/package.json");
		assert::Assert(t, fileIt != snapshot2->cacheFiles.end(),
		               {"file should still be in cacheFiles"});
		assert::Equal(t, fileIt->second->Content(),
		              std::string(R"({"name": "mylib"})"));
	});

	t->Run("alias clone isolation between snapshots", [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/node_modules/mylib",
		             vfstest::Symlink("/packages/mylib")},
		            {"/project/node_modules/other",
		             vfstest::Symlink("/packages/other")},
		            {"/packages/mylib/package.json",
		             R"({"name": "mylib"})"},
		            {"/packages/other/package.json",
		             R"({"name": "other"})"}},
		    false);

		// Build first snapshot — read only mylib.
		auto* builder1 = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});
		builder1->GetFile("/project/node_modules/mylib/package.json");
		auto pr1 = builder1->Finalize();
		auto* snapshot1 = pr1.first;

		// Build second snapshot — also read other.
		auto* builder2 = newSnapshotFSBuilder(
		    testFS.get(), {}, snapshot1->cacheFiles,
		    snapshot1->cacheDirectories,
		    snapshot1->nodeModulesRealpathAliases);
		builder2->GetFile("/project/node_modules/other/package.json");
		auto pr2 = builder2->Finalize();
		auto* snapshot2 = pr2.first;

		assert::Assert(
		    t,
		    snapshot1->nodeModulesRealpathAliases.count(
		        "/packages/mylib/package.json") != 0,
		    {"snapshot1 should have mylib alias"});
		assert::Assert(
		    t,
		    snapshot1->nodeModulesRealpathAliases.count(
		        "/packages/other/package.json") == 0,
		    {"snapshot1 should NOT have other alias — it was added in "
		     "a later snapshot"});

		assert::Assert(
		    t,
		    snapshot2->nodeModulesRealpathAliases.count(
		        "/packages/mylib/package.json") != 0,
		    {"snapshot2 should have mylib alias"});
		assert::Assert(
		    t,
		    snapshot2->nodeModulesRealpathAliases.count(
		        "/packages/other/package.json") != 0,
		    {"snapshot2 should have other alias"});
	});

	t->Run(
	    "adding symlink to inherited realpath key does not mutate previous snapshot",
	    [](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/node_modules/mylib",
		             vfstest::Symlink("/packages/mylib")},
		            {"/project/node_modules/alias",
		             vfstest::Symlink("/packages/mylib")},
		            {"/packages/mylib/package.json",
		             R"({"name": "mylib"})"}},
		    false);

		// Snapshot 1: read via one symlink only.
		auto* builder1 = newSnapshotFSBuilder(
		    testFS.get(), {}, {}, {}, {});
		builder1->GetFile("/project/node_modules/mylib/package.json");
		auto pr1 = builder1->Finalize();
		auto* snapshot1 = pr1.first;

		auto aliasIt1 = snapshot1->nodeModulesRealpathAliases.find(
		    "/packages/mylib/package.json");
		assert::Assert(
		    t, aliasIt1 != snapshot1->nodeModulesRealpathAliases.end());
		assert::Equal(t, int64_t(aliasIt1->second->paths.Len()),
		              int64_t(1));
		assert::Assert(t,
		               aliasIt1->second->paths.Has(
		                   "/project/node_modules/mylib/package.json"));

		// Snapshot 2: read via the SECOND symlink, which maps to the
		// same realpath. This exercises the case where LoadOrStore finds
		// the key in the base map and must clone-on-write rather than
		// mutating the shared set.
		auto* builder2 = newSnapshotFSBuilder(
		    testFS.get(), {}, snapshot1->cacheFiles,
		    snapshot1->cacheDirectories,
		    snapshot1->nodeModulesRealpathAliases);
		builder2->GetFile("/project/node_modules/alias/package.json");
		auto pr2 = builder2->Finalize();
		auto* snapshot2 = pr2.first;

		auto aliasIt2 = snapshot2->nodeModulesRealpathAliases.find(
		    "/packages/mylib/package.json");
		assert::Assert(
		    t, aliasIt2 != snapshot2->nodeModulesRealpathAliases.end());
		assert::Equal(t, int64_t(aliasIt2->second->paths.Len()),
		              int64_t(2));
		assert::Assert(t,
		               aliasIt2->second->paths.Has(
		                   "/project/node_modules/mylib/package.json"));
		assert::Assert(t,
		               aliasIt2->second->paths.Has(
		                   "/project/node_modules/alias/package.json"));

		// Snapshot 1 must NOT have been mutated.
		assert::Equal(
		    t, int64_t(aliasIt1->second->paths.Len()), int64_t(1),
		    {"snapshot1 alias set must not be mutated by snapshot2"});
		assert::Assert(
		    t,
		    !aliasIt1->second->paths.Has(
		        "/project/node_modules/alias/package.json"),
		    {"snapshot1 must not contain alias added in snapshot2"});
	});
}

void TestExpandAndFilterWatchEvents(T* t) {
	t->Parallel();

	auto newBuilder = [](vfs::FS* testFS) {
		return newSnapshotFSBuilder(testFS, {}, {}, {}, {});
	};

	t->Run("preserves node_modules directory deletion even when untracked",
	       [&](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/index.ts", "export const x = 1;"}},
		    false);
		auto* builder = newBuilder(testFS.get());

		FileChangeSummary change;
		change.Deleted.Add("file:///project/node_modules");

		auto expanded = builder->expandAndFilterWatchEvents(
		    change, {}, nullptr, {}, {});
		assert::Assert(
		    t,
		    expanded.Deleted.Has("file:///project/node_modules"),
		    {"bare node_modules directory deletion should be "
		     "preserved"});
	});

	t->Run("preserves deletion of a package directory inside node_modules",
	       [&](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/index.ts", "export const x = 1;"}},
		    false);
		auto* builder = newBuilder(testFS.get());

		FileChangeSummary change;
		change.Deleted.Add("file:///project/node_modules/@scope/pkg");

		auto expanded = builder->expandAndFilterWatchEvents(
		    change, {}, nullptr, {}, {});
		assert::Assert(
		    t,
		    expanded.Deleted.Has(
		        "file:///project/node_modules/@scope/pkg"),
		    {"package directory deletion inside node_modules should be "
		     "preserved"});
	});

	t->Run("drops irrelevant untracked deletion outside node_modules",
	       [&](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/index.ts", "export const x = 1;"}},
		    false);
		auto* builder = newBuilder(testFS.get());

		FileChangeSummary change;
		change.Deleted.Add("file:///project/build");

		auto expanded = builder->expandAndFilterWatchEvents(
		    change, {}, nullptr, {}, {});
		assert::Equal(
		    t, int64_t(expanded.Deleted.Len()), int64_t(0),
		    {"untracked non-node_modules directory deletion should be "
		     "dropped"});
	});

	t->Run("preserves exact content mapper dependencies", [&](T* t) {
		t->Parallel();
		auto testFS = vfstest::FromMap(
		    fileMap{{"/project/index.ts", "export const x = 1;"}},
		    false);
		auto* builder = newBuilder(testFS.get());
		auto watched = collections::NewSetFromItems<tspath::Path>(
		    tspath::Path("/project/mapper.config"));
		FileChangeSummary change;
		change.Changed.Add("file:///project/mapper.config");
		change.Deleted.Add("file:///project/mapper.config");

		auto expanded = builder->expandAndFilterWatchEvents(
		    change, {}, &watched, {}, {});
		assert::Assert(t, expanded.Changed.Has(
		                    "file:///project/mapper.config"));
		assert::Assert(t, expanded.Deleted.Has(
		                    "file:///project/mapper.config"));
	});

	t->Run("expands tracked directory deletion into file deletions",
	       [&](T* t) {
		t->Parallel();
		cacheFileMap existingCacheFiles{
		    {"/src/foo.ts",
		     newCachedFileH("/src/foo.ts", "const foo = 1;")},
		};
		cacheDirMap existingDirs{
		    {"/", {{"/src", "src"}}},
		    {"/src", {{"/src/foo.ts", "foo.ts"}}},
		};
		auto testFS = vfstest::FromMap(
		    fileMap{{"/src/foo.ts", "const foo = 1;"}}, false);
		auto* builder = newSnapshotFSBuilder(
		    testFS.get(), {}, std::move(existingCacheFiles),
		    std::move(existingDirs), {});

		FileChangeSummary change;
		change.Deleted.Add("file:///src");

		auto expanded = builder->expandAndFilterWatchEvents(
		    change, {}, nullptr, {}, {});
		assert::Assert(
		    t, expanded.Deleted.Has("file:///src/foo.ts"),
		    {"tracked directory deletion should expand to contained "
		     "file deletions"});
		assert::Assert(
		    t, !expanded.Deleted.Has("file:///src"),
		    {"the directory URI itself should be replaced by its "
		     "files"});
	});
}

} // namespace

REGISTER_UNIT_TEST("project.TestSnapshotFSBuilderCachesReturnedSourceHandle",
                   TestSnapshotFSBuilderCachesReturnedSourceHandle);
REGISTER_UNIT_TEST("project.TestSnapshotFSBuilder", TestSnapshotFSBuilder);
REGISTER_UNIT_TEST("project.TestSnapshotFS", TestSnapshotFS);
REGISTER_UNIT_TEST("project.TestSourceFS", TestSourceFS);
REGISTER_UNIT_TEST("project.TestAutoImportBuilderFS",
                   TestAutoImportBuilderFS);
REGISTER_UNIT_TEST("project.TestRealpathAliasLifecycle",
                   TestRealpathAliasLifecycle);
REGISTER_UNIT_TEST("project.TestExpandAndFilterWatchEvents",
                   TestExpandAndFilterWatchEvents);
