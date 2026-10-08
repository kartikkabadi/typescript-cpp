// Port of tsc/internal/vfs/iovfs/iofs_test.go.
#include <memory>
#include <string>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/iovfs/iovfs.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace fstest = tsc::vfs::vfstest::fstest;
namespace vfstest = tsc::vfs::vfstest;

namespace {

void TestIOFS(T* t) {
	t->Parallel();

	// Go uses fstest.MapFS; vfstest::convertMapFS wraps it in the
	// vfs::FS-convertible MapFS.
	fstest::MapFS testfs;
	auto addFile = [&testfs](const std::string& name,
	                         const std::string& data) {
		auto f = std::make_shared<fstest::MapFile>();
		f->Data = data;
		testfs.files[name] = f;
	};
	addFile("foo.ts", "hello, world");
	addFile("dir1/file1.ts", "export const foo = 42;");
	addFile("dir1/file2.ts", "export const foo = 42;");
	addFile("dir2/file1.ts", "export const foo = 42;");

	auto clock = std::make_shared<vfstest::clockImpl>();
	clock->start = std::chrono::system_clock::now();
	auto fs = tsc::vfs::iovfs::From(
	    vfstest::convertMapFS(testfs, true, clock), true);

	t->Run("ReadFile", [fs](T* t) {
		t->Parallel();

		auto [content, ok] = fs->ReadFile("/foo.ts");
		assert::Assert(t, ok);
		assert::Equal(t, content, "hello, world");

		std::string content2;
		bool ok2;
		std::tie(content2, ok2) = fs->ReadFile("/does/not/exist.ts");
		assert::Assert(t, !ok2);
		assert::Equal(t, content2, "");
	});

	t->Run("ReadFileUnrooted", [fs](T* t) {
		t->Parallel();

		tsc::testutil::AssertPanics(
		    t, [&] { fs->ReadFile("bar"); },
		    std::any(std::string("vfs: path \"bar\" is not absolute")));
	});

	t->Run("FileExists", [fs](T* t) {
		t->Parallel();

		assert::Assert(t, fs->FileExists("/foo.ts"));
		assert::Assert(t, !fs->FileExists("/bar"));
	});

	t->Run("DirectoryExists", [fs](T* t) {
		t->Parallel();

		assert::Assert(t, fs->DirectoryExists("/"));
		assert::Assert(t, fs->DirectoryExists("/dir1"));
		assert::Assert(t, fs->DirectoryExists("/dir1/"));
		assert::Assert(t, fs->DirectoryExists("/dir1/./"));
		assert::Assert(t, !fs->DirectoryExists("/bar"));
	});

	t->Run("GetAccessibleEntries", [fs](T* t) {
		t->Parallel();

		auto entries = fs->GetAccessibleEntries("/");
		assert::Equal(t, entries.directories,
		              std::vector<std::string>{"dir1", "dir2"});
		assert::Equal(t, entries.files,
		              std::vector<std::string>{"foo.ts"});
	});

	t->Run("Realpath", [fs](T* t) {
		t->Parallel();

		auto realpath = fs->Realpath("/foo.ts");
		assert::Equal(t, realpath, "/foo.ts");
	});

	t->Run("UseCaseSensitiveFileNames", [fs](T* t) {
		t->Parallel();

		assert::Assert(t, fs->UseCaseSensitiveFileNames());
	});
}

} // namespace

REGISTER_UNIT_TEST("iovfs.TestIOFS", TestIOFS);
