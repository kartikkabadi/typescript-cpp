// Port of tsc/internal/ls/file_rename_test.go.
#include <string>

#include "internal/gostd/testing.h"
#include "internal/ls/ls.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/ls/tests/testaccess.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc::ls;
using namespace tsc;

namespace {

// caseInsensitiveHost is a minimal Host implementation for tests that only
// exercise case-insensitivity-dependent path logic; every other method
// panics if called.
struct caseInsensitiveHost : Host {
	bool UseCaseSensitiveFileNames() override { return false; }
	std::pair<std::string, bool> ReadFile(const std::string&) override {
		TSC_UNREACHABLE("not implemented");
	}
	lsconv::Converters* Converters() override {
		TSC_UNREACHABLE("not implemented");
	}
	lsutil::UserPreferences GetPreferences(const std::string&) override {
		TSC_UNREACHABLE("not implemented");
	}
	sourcemap::ECMALineInfo* GetECMALineInfo(
	    const std::string&) override {
		TSC_UNREACHABLE("not implemented");
	}
	autoimport::Registry* AutoImportRegistry() override {
		TSC_UNREACHABLE("not implemented");
	}
	std::vector<std::string> ReadDirectory(
	    const std::string&, const std::string&,
	    const std::vector<std::string>&,
	    const std::vector<std::string>*,
	    const std::vector<std::string>&, int) override {
		TSC_UNREACHABLE("not implemented");
	}
	std::vector<std::string> GetDirectories(const std::string&) override {
		TSC_UNREACHABLE("not implemented");
	}
	bool DirectoryExists(const std::string&) override {
		TSC_UNREACHABLE("not implemented");
	}
	bool FileExists(const std::string&) override {
		TSC_UNREACHABLE("not implemented");
	}
};

}  // namespace

// TestCreatePathUpdaterCaseFoldingShrinksOldPath reproduces a panic that
// used to occur when createPathUpdater confirmed a case-insensitive
// directory match via tspath.StartsWithDirectory, then sliced the raw
// (non-canonicalized) file path using the raw byte length of oldPath. Each
// Kelvin sign 'K' below case-folds to the single-byte 'k', so the raw
// oldPath is longer in bytes (15) than path (12), even though path's
// canonical form is case-insensitively prefixed by oldPath's canonical
// form. Slicing path[len(oldPath):] used to panic with "slice bounds out of
// range [15:12]"; createPathUpdater must instead trim by rune count via
// tspath.TrimFilePathPrefix.
static void TestCreatePathUpdaterCaseFoldingShrinksOldPath(T* t) {
	t->Parallel();

	caseInsensitiveHost host;
	auto* l = LSTestAccess::NewWithHost(&host);
	std::string oldPath = "/a/\xE2\x84\xAA\xE2\x84\xAA\xE2\x84\xAA\xE2\x84\xAA"; // 'K' x4
	std::string newPath = "/a/new";
	auto updater = LSTestAccess::createPathUpdater(l, oldPath, newPath);

	auto [updated, ok] = updater("/a/kkkk/x.ts");
	gotest::assert::Assert(t, ok);
	gotest::assert::Equal(t, updated, std::string("/a/new/x.ts"));
}
REGISTER_UNIT_TEST("ls.TestCreatePathUpdaterCaseFoldingShrinksOldPath",
                   TestCreatePathUpdaterCaseFoldingShrinksOldPath);
