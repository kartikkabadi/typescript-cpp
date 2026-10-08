// tests_outputpaths.cpp — port of tsc/internal/outputpaths/outputpaths_test.go.
#include <string>

#include "internal/gostd/testing.h"
#include "internal/outputpaths/outputpaths.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;

namespace {

void TestGetSourceFilePathInNewDirSourceMatchesCommonDirectory(T* t) {
	t->Parallel();

	auto actual = outputpaths::GetSourceFilePathInNewDir(
	    "/project/src", "/project/out", "/project", "/project/src/", true);
	assert::Equal(t, actual, std::string("/project/src"));
}

void TestGetSourceFilePathInNewDirCanonicalizationShrinksCommonDirectory(
    T* t) {
	t->Parallel();

	// Each Kelvin sign 'K' case-folds to the single-byte 'k', so the raw
	// (non-canonicalized) commonSourceDirectory is longer, in bytes, than the
	// source file path it's a case-insensitive prefix of, even though the file
	// path itself is longer overall once its own (already-lowercase) suffix is
	// included. Slicing sourceFilePath by len(commonSourceDirectory) bytes
	// would still panic here ([14:11]); this must clamp per-rune instead, like
	// the reference implementation's substring does.
	auto actual = outputpaths::GetSourceFilePathInNewDir(
	    "/kkkk/a.ts", "/out", "/", "/\xE2\x84\xAA\xE2\x84\xAA\xE2\x84\xAA\xE2\x84\xAA/",
	    false);
	assert::Equal(t, actual, std::string("/out/a.ts"));
}

} // namespace

REGISTER_UNIT_TEST(
    "outputpaths.TestGetSourceFilePathInNewDirSourceMatchesCommonDirectory",
    TestGetSourceFilePathInNewDirSourceMatchesCommonDirectory);
REGISTER_UNIT_TEST(
    "outputpaths.TestGetSourceFilePathInNewDirCanonicalizationShrinksCommonDirectory",
    TestGetSourceFilePathInNewDirCanonicalizationShrinksCommonDirectory);
