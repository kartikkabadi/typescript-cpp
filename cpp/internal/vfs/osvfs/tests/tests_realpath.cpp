// Port of tsc/internal/vfs/osvfs/realpath_test.go + the mklink helper from
// helpers_test.go (package-internal helpers). BenchmarkRealpath is not
// ported (benchmarks are out of scope).
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/osvfs/osvfs.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace fsns = std::filesystem;

namespace {

// mklink — osvfs/helpers_test.go (Linux path: plain os.Symlink; the Windows
// junction/privilege branches do not apply).
void mklink(T* t, const std::string& target, const std::string& link,
            bool isDir) {
	t->Helper();
	std::error_code ec;
	fsns::create_symlink(target, link, ec);
	assert::Assert(t, !ec);
}

// setupSymlinks — realpath_test.go.
std::pair<std::string, std::string> setupSymlinks(T* t) {
	t->Helper();

	auto tmp = t->TempDir();

	auto target = tmp + "/target";
	auto targetFile = target + "/file";

	auto link = tmp + "/link";
	auto linkFile = link + "/file";

	std::error_code ec;
	fsns::create_directories(target, ec);
	assert::Assert(t, !ec);
	{
		std::ofstream out(targetFile);
		out << "hello";
	}

	mklink(t, target, link, true);

	return {targetFile, linkFile};
}

void TestSymlinkRealpath(T* t) {
	t->Parallel();

	auto [targetFile, linkFile] = setupSymlinks(t);

	std::ifstream in(linkFile);
	assert::Assert(t, in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	assert::Equal(t, ss.str(), "hello");

	auto* fs = tsc::vfs::osvfs::FS();

	auto targetRealpath =
	    fs->Realpath(tsc::tspath::normalizePath(targetFile));
	auto linkRealpath =
	    fs->Realpath(tsc::tspath::normalizePath(linkFile));

	if (targetRealpath != linkRealpath) {
		t->Errorf(
		    "expected realpath of target and link to be equal, got %q "
		    "and %q",
		    {targetRealpath, linkRealpath});
		// Go runs node for extra diagnostics; node is not assumed
		// installed here (SkipIfNoNodeJS elsewhere), so that step is
		// omitted — the equality failure above is the assertion.
	}
}

void TestGetAccessibleEntries(T* t) {
	t->Parallel();

	auto tmp = t->TempDir();
	auto target = tmp + "/target";
	auto link = tmp + "/link";

	std::error_code ec;
	fsns::create_directories(target, ec);
	assert::Assert(t, !ec);
	fsns::create_directories(link, ec);
	assert::Assert(t, !ec);

	auto targetFile1 = target + "/file1";
	auto targetFile2 = target + "/file2";

	{
		std::ofstream out(targetFile1);
		out << "hello";
	}
	{
		std::ofstream out(targetFile2);
		out << "world";
	}

	auto targetDir1 = target + "/dir1";
	auto targetDir2 = target + "/dir2";

	fsns::create_directories(targetDir1, ec);
	assert::Assert(t, !ec);
	fsns::create_directories(targetDir2, ec);
	assert::Assert(t, !ec);

	mklink(t, targetFile1, link + "/file1", false);
	mklink(t, targetFile2, link + "/file2", false);
	mklink(t, targetDir1, link + "/dir1", true);
	mklink(t, targetDir2, link + "/dir2", true);

	auto* fs = tsc::vfs::osvfs::FS();

	auto entries = fs->GetAccessibleEntries(tsc::tspath::normalizePath(link));

	assert::Equal(t, entries.directories,
	              std::vector<std::string>{"dir1", "dir2"});
	assert::Equal(t, entries.files,
	              std::vector<std::string>{"file1", "file2"});
	assert::Check(t, entries.symlinks.has_value(),
	              "expected Symlinks to be set for directory with "
	              "symlinks");
	assert::Equal(t, entries.symlinks->size(), size_t{4});
	for (auto name : {"file1", "file2", "dir1", "dir2"}) {
		assert::Check(
		    t, entries.symlinks->count(std::string(name)) != 0,
		    "expected name to be in Symlinks");
	}

	// Non-symlink directory should have empty Symlinks.
	entries =
	    fs->GetAccessibleEntries(tsc::tspath::normalizePath(target));
	assert::Equal(t, entries.directories,
	              std::vector<std::string>{"dir1", "dir2"});
	assert::Equal(t, entries.files,
	              std::vector<std::string>{"file1", "file2"});
	assert::Check(
	    t, entries.symlinks.has_value(),
	    "expected Symlinks to be non-nil for directory without symlinks");
	assert::Equal(t, entries.symlinks->size(), size_t{0});
}

} // namespace

REGISTER_UNIT_TEST("osvfs.TestSymlinkRealpath", TestSymlinkRealpath);
REGISTER_UNIT_TEST("osvfs.TestGetAccessibleEntries",
                   TestGetAccessibleEntries);
