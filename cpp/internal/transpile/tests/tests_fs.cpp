// tests_fs.cpp — port of tsc/internal/transpile/fs_test.go.
#include <string>
#include <unordered_map>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/transpile/transpile.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;

namespace {

void TestTranspileFSRejectsDirectoryAccess(T* t) {
	t->Parallel();

	transpile::transpileFS fs;
	fs.files["/src/module.ts"] = "";
	testutil::AssertPanics(t, [&] { fs.DirectoryExists("/src"); },
	                       std::string(
	                           "unexpected directory existence check for "
	                           "\"/src\""));
	testutil::AssertPanics(t, [&] { fs.Realpath("/src/module.ts"); },
	                       std::string(
	                           "unexpected realpath request for "
	                           "\"/src/module.ts\""));
}

} // namespace

REGISTER_UNIT_TEST("transpile.TestTranspileFSRejectsDirectoryAccess",
                   TestTranspileFSRejectsDirectoryAccess);
