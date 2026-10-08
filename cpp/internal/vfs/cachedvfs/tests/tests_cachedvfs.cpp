// Port of tsc/internal/vfs/cachedvfs/cachedvfs_test.go.
#include <memory>
#include <string>
#include <unordered_map>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/cachedvfs/cachedvfs.h"
#include "internal/vfs/vfsmock/vfsmock.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;

namespace {

// createMockFS — cachedvfs_test.go.
std::shared_ptr<tsc::vfs::vfsmock::FSMock> createMockFS() {
	// The mock's funcs capture the base FS pointer; keep it alive by
	// stashing it on a leaked shared_ptr (process-lifetime bound, like a
	// Go object held by the closure).
	static auto base = tsc::vfs::vfstest::FromMap(
	    std::unordered_map<std::string,
	                       tsc::vfs::vfstest::MapFileInput>{
	        {"/some/path/file.txt", "hello world"},
	    },
	    true);
	return tsc::vfs::vfsmock::Wrap(base.get());
}

void TestDirectoryExists(T* t) {
	t->Parallel();

	auto underlying = createMockFS();
	auto cached = tsc::vfs::cachedvfs::From(underlying.get());

	cached->DirectoryExists("/some/path");
	assert::Equal(t, 1, underlying->DirectoryExistsCalls().size());

	cached->DirectoryExists("/some/path");
	assert::Equal(t, 1, underlying->DirectoryExistsCalls().size());

	cached->ClearCache();
	cached->DirectoryExists("/some/path");
	assert::Equal(t, 2, underlying->DirectoryExistsCalls().size());

	cached->DirectoryExists("/other/path");
	assert::Equal(t, 3, underlying->DirectoryExistsCalls().size());

	cached->DisableAndClearCache();
	cached->DirectoryExists("/some/path");
	assert::Equal(t, 4, underlying->DirectoryExistsCalls().size());

	cached->DirectoryExists("/some/path");
	assert::Equal(t, 5, underlying->DirectoryExistsCalls().size());

	cached->Enable();
	cached->DirectoryExists("/some/path");
	assert::Equal(t, 6, underlying->DirectoryExistsCalls().size());

	cached->DirectoryExists("/some/path");
	assert::Equal(t, 6, underlying->DirectoryExistsCalls().size());
}

void TestFileExists(T* t) {
	t->Parallel();

	auto underlying = createMockFS();
	auto cached = tsc::vfs::cachedvfs::From(underlying.get());

	cached->FileExists("/some/path/file.txt");
	assert::Equal(t, 1, underlying->FileExistsCalls().size());

	cached->FileExists("/some/path/file.txt");
	assert::Equal(t, 1, underlying->FileExistsCalls().size());

	cached->ClearCache();
	cached->FileExists("/some/path/file.txt");
	assert::Equal(t, 2, underlying->FileExistsCalls().size());

	cached->FileExists("/other/path/file.txt");
	assert::Equal(t, 3, underlying->FileExistsCalls().size());

	cached->DisableAndClearCache();
	cached->FileExists("/some/path/file.txt");
	assert::Equal(t, 4, underlying->FileExistsCalls().size());

	cached->FileExists("/some/path/file.txt");
	assert::Equal(t, 5, underlying->FileExistsCalls().size());

	cached->Enable();
	cached->FileExists("/some/path/file.txt");
	assert::Equal(t, 6, underlying->FileExistsCalls().size());

	cached->FileExists("/some/path/file.txt");
	assert::Equal(t, 6, underlying->FileExistsCalls().size());
}

void TestGetAccessibleEntries(T* t) {
	t->Parallel();

	auto underlying = createMockFS();
	auto cached = tsc::vfs::cachedvfs::From(underlying.get());

	cached->GetAccessibleEntries("/some/path");
	assert::Equal(t, 1,
	              underlying->GetAccessibleEntriesCalls().size());

	cached->GetAccessibleEntries("/some/path");
	assert::Equal(t, 1,
	              underlying->GetAccessibleEntriesCalls().size());

	cached->ClearCache();
	cached->GetAccessibleEntries("/some/path");
	assert::Equal(t, 2,
	              underlying->GetAccessibleEntriesCalls().size());

	cached->GetAccessibleEntries("/other/path");
	assert::Equal(t, 3,
	              underlying->GetAccessibleEntriesCalls().size());

	cached->DisableAndClearCache();
	cached->GetAccessibleEntries("/some/path");
	assert::Equal(t, 4,
	              underlying->GetAccessibleEntriesCalls().size());

	cached->GetAccessibleEntries("/some/path");
	assert::Equal(t, 5,
	              underlying->GetAccessibleEntriesCalls().size());

	cached->Enable();
	cached->GetAccessibleEntries("/some/path");
	assert::Equal(t, 6,
	              underlying->GetAccessibleEntriesCalls().size());

	cached->GetAccessibleEntries("/some/path");
	assert::Equal(t, 6,
	              underlying->GetAccessibleEntriesCalls().size());
}

void TestRealpath(T* t) {
	t->Parallel();

	auto underlying = createMockFS();
	auto cached = tsc::vfs::cachedvfs::From(underlying.get());

	cached->Realpath("/some/path");
	assert::Equal(t, 1, underlying->RealpathCalls().size());

	cached->Realpath("/some/path");
	assert::Equal(t, 1, underlying->RealpathCalls().size());

	cached->ClearCache();
	cached->Realpath("/some/path");
	assert::Equal(t, 2, underlying->RealpathCalls().size());

	cached->Realpath("/other/path");
	assert::Equal(t, 3, underlying->RealpathCalls().size());

	cached->DisableAndClearCache();
	cached->Realpath("/some/path");
	assert::Equal(t, 4, underlying->RealpathCalls().size());

	cached->Realpath("/some/path");
	assert::Equal(t, 5, underlying->RealpathCalls().size());

	cached->Enable();
	cached->Realpath("/some/path");
	assert::Equal(t, 6, underlying->RealpathCalls().size());

	cached->Realpath("/some/path");
	assert::Equal(t, 6, underlying->RealpathCalls().size());
}

void TestStat(T* t) {
	t->Parallel();

	auto underlying = createMockFS();
	auto cached = tsc::vfs::cachedvfs::From(underlying.get());

	cached->Stat("/some/path");
	assert::Equal(t, 1, underlying->StatCalls().size());

	cached->Stat("/some/path");
	assert::Equal(t, 1, underlying->StatCalls().size());

	cached->ClearCache();
	cached->Stat("/some/path");
	assert::Equal(t, 2, underlying->StatCalls().size());

	cached->Stat("/other/path");
	assert::Equal(t, 3, underlying->StatCalls().size());

	cached->DisableAndClearCache();
	cached->Stat("/some/path");
	assert::Equal(t, 4, underlying->StatCalls().size());

	cached->Stat("/some/path");
	assert::Equal(t, 5, underlying->StatCalls().size());

	cached->Enable();
	cached->Stat("/some/path");
	assert::Equal(t, 6, underlying->StatCalls().size());

	cached->Stat("/some/path");
	assert::Equal(t, 6, underlying->StatCalls().size());
}

void TestReadFile(T* t) {
	t->Parallel();

	auto underlying = createMockFS();
	auto cached = tsc::vfs::cachedvfs::From(underlying.get());

	cached->ReadFile("/some/path/file.txt");
	assert::Equal(t, 1, underlying->ReadFileCalls().size());

	cached->ReadFile("/some/path/file.txt");
	assert::Equal(t, 2, underlying->ReadFileCalls().size());

	cached->ClearCache();
	cached->ReadFile("/some/path/file.txt");
	assert::Equal(t, 3, underlying->ReadFileCalls().size());

	cached->DisableAndClearCache();
	cached->ReadFile("/some/path/file.txt");
	assert::Equal(t, 4, underlying->ReadFileCalls().size());

	cached->ReadFile("/some/path/file.txt");
	assert::Equal(t, 5, underlying->ReadFileCalls().size());

	cached->Enable();
	cached->ReadFile("/some/path/file.txt");
	assert::Equal(t, 6, underlying->ReadFileCalls().size());

	cached->ReadFile("/some/path/file.txt");
	assert::Equal(t, 7, underlying->ReadFileCalls().size());
}

void TestUseCaseSensitiveFileNames(T* t) {
	t->Parallel();

	auto underlying = createMockFS();
	auto cached = tsc::vfs::cachedvfs::From(underlying.get());

	cached->UseCaseSensitiveFileNames();
	assert::Equal(
	    t, 1, underlying->UseCaseSensitiveFileNamesCalls().size());

	cached->UseCaseSensitiveFileNames();
	assert::Equal(
	    t, 2, underlying->UseCaseSensitiveFileNamesCalls().size());

	cached->ClearCache();
	cached->UseCaseSensitiveFileNames();
	assert::Equal(
	    t, 3, underlying->UseCaseSensitiveFileNamesCalls().size());

	cached->DisableAndClearCache();
	cached->UseCaseSensitiveFileNames();
	assert::Equal(
	    t, 4, underlying->UseCaseSensitiveFileNamesCalls().size());

	cached->UseCaseSensitiveFileNames();
	assert::Equal(
	    t, 5, underlying->UseCaseSensitiveFileNamesCalls().size());

	cached->Enable();
	cached->UseCaseSensitiveFileNames();
	assert::Equal(
	    t, 6, underlying->UseCaseSensitiveFileNamesCalls().size());

	cached->UseCaseSensitiveFileNames();
	assert::Equal(
	    t, 7, underlying->UseCaseSensitiveFileNamesCalls().size());
}

void TestRemove(T* t) {
	t->Parallel();

	auto underlying = createMockFS();
	auto cached = tsc::vfs::cachedvfs::From(underlying.get());

	cached->Remove("/some/path/file.txt");
	assert::Equal(t, 1, underlying->RemoveCalls().size());

	cached->Remove("/some/path/file.txt");
	assert::Equal(t, 2, underlying->RemoveCalls().size());

	cached->ClearCache();
	cached->Remove("/some/path/file.txt");
	assert::Equal(t, 3, underlying->RemoveCalls().size());

	cached->DisableAndClearCache();
	cached->Remove("/some/path/file.txt");
	assert::Equal(t, 4, underlying->RemoveCalls().size());

	cached->Remove("/some/path/file.txt");
	assert::Equal(t, 5, underlying->RemoveCalls().size());

	cached->Enable();
	cached->Remove("/some/path/file.txt");
	assert::Equal(t, 6, underlying->RemoveCalls().size());

	cached->Remove("/some/path/file.txt");
	assert::Equal(t, 7, underlying->RemoveCalls().size());
}

void TestWriteFile(T* t) {
	t->Parallel();

	auto underlying = createMockFS();
	auto cached = tsc::vfs::cachedvfs::From(underlying.get());

	cached->WriteFile("/some/path/file.txt", "new content");
	assert::Equal(t, 1, underlying->WriteFileCalls().size());

	cached->WriteFile("/some/path/file.txt", "another content");
	assert::Equal(t, 2, underlying->WriteFileCalls().size());

	cached->ClearCache();
	cached->WriteFile("/some/path/file.txt", "third content");
	assert::Equal(t, 3, underlying->WriteFileCalls().size());

	auto call = underlying->WriteFileCalls()[2];
	assert::Equal(t, "/some/path/file.txt", call.Path);
	assert::Equal(t, "third content", call.Data);

	cached->DisableAndClearCache();
	cached->WriteFile("/some/path/file.txt", "fourth content");
	assert::Equal(t, 4, underlying->WriteFileCalls().size());

	cached->WriteFile("/some/path/file.txt", "fifth content");
	assert::Equal(t, 5, underlying->WriteFileCalls().size());

	cached->Enable();
	cached->WriteFile("/some/path/file.txt", "sixth content");
	assert::Equal(t, 6, underlying->WriteFileCalls().size());

	cached->WriteFile("/some/path/file.txt", "seventh content");
	assert::Equal(t, 7, underlying->WriteFileCalls().size());
}

} // namespace

REGISTER_UNIT_TEST("cachedvfs.TestDirectoryExists", TestDirectoryExists);
REGISTER_UNIT_TEST("cachedvfs.TestFileExists", TestFileExists);
REGISTER_UNIT_TEST("cachedvfs.TestGetAccessibleEntries",
                   TestGetAccessibleEntries);
REGISTER_UNIT_TEST("cachedvfs.TestRealpath", TestRealpath);
REGISTER_UNIT_TEST("cachedvfs.TestStat", TestStat);
REGISTER_UNIT_TEST("cachedvfs.TestReadFile", TestReadFile);
REGISTER_UNIT_TEST("cachedvfs.TestUseCaseSensitiveFileNames",
                   TestUseCaseSensitiveFileNames);
REGISTER_UNIT_TEST("cachedvfs.TestRemove", TestRemove);
REGISTER_UNIT_TEST("cachedvfs.TestWriteFile", TestWriteFile);
