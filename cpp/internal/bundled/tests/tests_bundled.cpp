// tests_bundled.cpp — port of tsc/internal/bundled/bundled_test.go.
#include <algorithm>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/osvfs/osvfs.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;

namespace {

bool statExists(const std::string& path) {
	struct stat st;
	return ::stat(path.c_str(), &st) == 0;
}

void TestTestingLibPath(T* t) {
	t->Parallel();

	auto p = bundled::TestingLibPath();
	assert::Assert(t, statExists(p),
	               "expected TestingLibPath to exist: " + p);

	auto libdts = p + "/lib.d.ts";
	assert::Assert(t, statExists(libdts),
	               "expected lib.d.ts to exist: " + libdts);
}

void TestEmbeddedLibs(T* t) {
	t->Parallel();

	auto fs = bundled::WrapFS(std::shared_ptr<vfs::FS>(
	    tsc::vfs::osvfs::FS(), [](vfs::FS*) {}));
	auto files = fs->GetAccessibleEntries(bundled::LibPath()).files;
	std::sort(files.begin(), files.end());
	assert::Assert(t, files == bundled::LibNames);
}

} // namespace

REGISTER_UNIT_TEST("bundled.TestTestingLibPath", TestTestingLibPath);
REGISTER_UNIT_TEST("bundled.TestEmbeddedLibs", TestEmbeddedLibs);
