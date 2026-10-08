// Port of tsc/internal/api/requestfilesystem/filechanges_test.go (package
// requestfilesystem).
#include <memory>
#include <string>
#include <unordered_map>

#include "internal/api/requestfilesystem/requestfilesystem.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/project/filechange.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::api::requestfilesystem {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace lsproto = tsc::lsp::lsproto;

// newRequestFileSystem — test helper (requestfilesystem_test.go:35).
std::pair<std::shared_ptr<requestFileSystem>, gostd::Error>
newRequestFileSystem(RequestFileSystem* params,
                     const std::shared_ptr<vfs::FS>& base,
                     const std::string& currentDirectory) {
	project::FileChangeSummary fileChanges;
	auto [fileSystem, err] =
	    NewForUpdate(params, base, currentDirectory, &fileChanges);
	if (err != nullptr) {
		return {nullptr, err};
	}
	return {std::shared_ptr<requestFileSystem>(
	            fileSystem,
	            dynamic_cast<requestFileSystem*>(fileSystem.get())),
	        nullptr};
}

std::shared_ptr<vfs::FS> emptyHost() {
	return vfs::vfstest::FromMap(
	    std::unordered_map<std::string, vfs::vfstest::MapFileInput>{},
	    true);
}

void TestFileChangesIncludeDirectoryTombstones(T* t) {
	t->Parallel();

	RequestFileSystem baseParams{
	    .kind = KindFull,
	    .Files = {
	        {"/removed/nested/file.ts", "removed"},
	        {"/replaced.ts", "old"},
	    },
	    .Symlinks = std::map<std::string, RequestSymlink>{
	        {"/alias", {.Target = "/removed"}}},
	};
	auto [base, err] =
	    newRequestFileSystem(&baseParams, emptyHost(), "/");
	assert::NilError(t, err);

	project::FileChangeSummary summary;
	RequestFileSystem layerParams{
	    .kind = KindLayer,
	    .Files = {{"/replaced.ts", "new"}},
	    .RemovedPaths =
	        std::vector<std::string>{"removed", "/missing",
	                                 "/replaced.ts"},
	};
	auto [fs, err2] = NewForUpdate(&layerParams,
	                               std::shared_ptr<vfs::FS>(
	                                   base.get(), [](vfs::FS*) {}),
	                               "/", &summary);
	assert::NilError(t, err2);
	assert::Assert(t, !summary.InvalidateAll);
	assert::Assert(t, summary.IncludesWatchChangeOutsideNodeModules);
	assert::Equal(t, int(summary.Deleted.Len()), 2);
	assert::Assert(
	    t, summary.Deleted.Has(lsproto::DocumentUri("file:///removed")));
	assert::Assert(
	    t, summary.Deleted.Has(lsproto::DocumentUri("file:///alias")));
	assert::Equal(t, int(summary.Changed.Len()), 1);
	assert::Assert(t, summary.Changed.Has(
	                    lsproto::DocumentUri("file:///replaced.ts")));
}
REGISTER_UNIT_TEST("requestfilesystem.TestFileChangesIncludeDirectoryTombstones",
                   TestFileChangesIncludeDirectoryTombstones);

void TestFileChangesIncludeListingsAndSymlinks(T* t) {
	t->Parallel();

	auto base = vfs::vfstest::FromMap(
	    std::unordered_map<std::string, vfs::vfstest::MapFileInput>{
	        {"/dir/old.ts", "old listing"},
	        {"/link/old.ts", "old target"},
	    },
	    true);
	project::FileChangeSummary summary;
	RequestFileSystem params{
	    .kind = KindLayer,
	    .Directories = std::map<std::string, RequestDirectoryEntries>{
	        {"/dir", {}}},
	    .Symlinks = std::map<std::string, RequestSymlink>{
	        {"/link", {.Target = "/target"}},
	        {"/new", {.Target = "/host", .Host = true}}},
	};
	auto [fs, err] =
	    NewForUpdate(&params, base, "/", &summary);
	assert::NilError(t, err);
	assert::Assert(t, !summary.InvalidateAll);
	assert::Equal(t, int(summary.Deleted.Len()), 2);
	assert::Assert(t, summary.Deleted.Has(
	                    lsproto::DocumentUri("file:///dir")));
	assert::Assert(t, summary.Deleted.Has(
	                    lsproto::DocumentUri("file:///link")));
	assert::Equal(t, int(summary.Created.Len()), 3);
	assert::Assert(t, summary.Created.Has(
	                    lsproto::DocumentUri("file:///dir")));
	assert::Assert(t, summary.Created.Has(
	                    lsproto::DocumentUri("file:///link")));
	assert::Assert(t, summary.Created.Has(
	                    lsproto::DocumentUri("file:///new")));
}
REGISTER_UNIT_TEST("requestfilesystem.TestFileChangesIncludeListingsAndSymlinks",
                   TestFileChangesIncludeListingsAndSymlinks);

void TestFileChangesIncludeRecursiveSymlinkAliases(T* t) {
	t->Parallel();

	RequestFileSystem baseParams{
	    .kind = KindFull,
	    .Files = {{"/dir/file.ts", "old"}},
	    .Symlinks = std::map<std::string, RequestSymlink>{
	        {"/dir/link", {.Target = "/dir"}}},
	};
	auto [base, err] =
	    newRequestFileSystem(&baseParams, emptyHost(), "/");
	assert::NilError(t, err);

	project::FileChangeSummary summary;
	RequestFileSystem layerParams{
	    .kind = KindLayer,
	    .Files = {{"/dir/file.ts", "new"}},
	};
	auto [fs, err2] = NewForUpdate(&layerParams,
	                               std::shared_ptr<vfs::FS>(
	                                   base.get(), [](vfs::FS*) {}),
	                               "/", &summary);
	assert::NilError(t, err2);
	assert::Equal(t, int(summary.Changed.Len()), 2);
	assert::Assert(t, summary.Changed.Has(lsproto::DocumentUri(
	                    "file:///dir/file.ts")));
	assert::Assert(t, summary.Changed.Has(lsproto::DocumentUri(
	                    "file:///dir/link/file.ts")));
	assert::Equal(t, int(summary.Created.Len()), 0);
}
REGISTER_UNIT_TEST("requestfilesystem.TestFileChangesIncludeRecursiveSymlinkAliases",
                   TestFileChangesIncludeRecursiveSymlinkAliases);

void TestFileChangesIncludeRootSymlinkAliases(T* t) {
	t->Parallel();

	RequestFileSystem baseParams{
	    .kind = KindFull,
	    .Files = {{"/file.ts", "old"}},
	    .Symlinks = std::map<std::string, RequestSymlink>{
	        {"/link", {.Target = "/"}}},
	};
	auto [base, err] =
	    newRequestFileSystem(&baseParams, emptyHost(), "/");
	assert::NilError(t, err);
	auto [content, ok] = base->ReadFile("/link/file.ts");
	assert::Assert(t, ok);
	assert::Equal(t, content, std::string("old"));

	project::FileChangeSummary summary;
	RequestFileSystem layerParams{
	    .kind = KindLayer,
	    .Files = {{"/file.ts", "new"}},
	};
	auto [fs, err2] = NewForUpdate(&layerParams,
	                               std::shared_ptr<vfs::FS>(
	                                   base.get(), [](vfs::FS*) {}),
	                               "/", &summary);
	assert::NilError(t, err2);
	assert::Equal(t, int(summary.Changed.Len()), 2);
	assert::Assert(t, summary.Changed.Has(
	                    lsproto::DocumentUri("file:///file.ts")));
	assert::Assert(t, summary.Changed.Has(lsproto::DocumentUri(
	                    "file:///link/file.ts")));
	assert::Equal(t, int(summary.Created.Len()), 0);
}
REGISTER_UNIT_TEST("requestfilesystem.TestFileChangesIncludeRootSymlinkAliases",
                   TestFileChangesIncludeRootSymlinkAliases);

}  // namespace
}  // namespace tsc::api::requestfilesystem
