// Port of tsc/internal/vfs/osvfs/os_test.go.
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "internal/gostd/testing.h"
#include "internal/repo/paths.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/osvfs/osvfs.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;

namespace {

void TestOS(T* t) {
	t->Parallel();

	auto* fs = tsc::vfs::osvfs::FS();

	t->Run("ReadFile", [fs](T* t) {
		t->Parallel();

		auto goMod = tsc::repo::rootPath() + "/go.mod";
		auto goModPath = tsc::tspath::normalizePath(goMod);

		std::ifstream in(goMod, std::ios::binary);
		assert::Assert(t, in.good());
		std::ostringstream ss;
		ss << in.rdbuf();
		auto expected = ss.str();

		auto [contents, ok] = fs->ReadFile(goModPath);
		assert::Assert(t, ok);
		assert::Equal(t, contents, expected);
	});

	t->Run("Realpath", [fs](T* t) {
		t->Parallel();

		const char* homeEnv = std::getenv("HOME");
		if (homeEnv == nullptr) {
			t->Skip({""});
			return;
		}
		auto home = tsc::tspath::normalizePath(homeEnv);

		auto expected = home;
		// Linux/macOS: realpath returns the path unchanged apart from
		// canonicalization (Go's windows drive-letter uppercase branch is
		// N/A here).
		auto realpath = fs->Realpath(home);
		assert::Equal(t, realpath, expected);
	});

	t->Run("UseCaseSensitiveFileNames", [fs](T* t) {
		t->Parallel();

		// Just check that it works.
		fs->UseCaseSensitiveFileNames();

		// runtime.GOOS == "linux" here.
		assert::Assert(t, fs->UseCaseSensitiveFileNames());
	});
}

} // namespace

REGISTER_UNIT_TEST("osvfs.TestOS", TestOS);
