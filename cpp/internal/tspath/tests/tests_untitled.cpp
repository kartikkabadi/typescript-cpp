// Port of tsc/internal/tspath/untitled_test.go (package tspath_test).
#include <string>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
using namespace tsc;

static void TestUntitledPathHandling(T* t) {
	t->Parallel();
	// Test that untitled paths are treated as rooted
	std::string untitledPath = "^/untitled/ts-nul-authority/Untitled-2";

	// GetEncodedRootLength should return 2 for "^/"
	int rootLength = tspath::getEncodedRootLength(untitledPath);
	gotest::assert::Equal(t, rootLength, 2,
	                      "GetEncodedRootLength should return 2 for untitled paths");

	// IsRootedDiskPath should return true
	bool isRooted = tspath::isRootedDiskPath(untitledPath);
	gotest::assert::Assert(t, isRooted,
	                       "IsRootedDiskPath should return true for untitled paths");

	// ToPath should not resolve untitled paths against current directory
	std::string currentDir = "/home/user/project";
	tspath::Path path = tspath::toPath(untitledPath, currentDir, true);
	// The path should be the original untitled path
	gotest::assert::Equal(t, path, "^/untitled/ts-nul-authority/Untitled-2");

	// Test GetNormalizedAbsolutePath doesn't resolve untitled paths
	std::string normalized =
		tspath::getNormalizedAbsolutePath(untitledPath, currentDir);
	gotest::assert::Equal(t, normalized,
	                      "^/untitled/ts-nul-authority/Untitled-2");
}

static void TestUntitledPathEdgeCases(T* t) {
	t->Parallel();
	struct testCase {
		const char* path;
		int expected;
		bool isRooted;
	};
	static const testCase testCases[] = {
	    {"^/", 2, true},       // Minimal untitled path
	    {"^/untitled/ts-nul-authority/test", 2, true},  // Normal untitled path
	    {"^", 0, false},       // Just ^ is not rooted
	    {"^x", 0, false},      // ^x is not untitled
	    {"^^/", 0, false},     // ^^/ is not untitled
	    {"x^/", 0, false},     // x^/ is not untitled (doesn't start with ^)
	    {"^/untitled/ts-nul-authority/path/with/deeper/structure", 2, true},
	};

	for (auto& tc : testCases) {
		t->Run(tc.path, [&tc](T* t) {
			t->Parallel();
			int rootLength = tspath::getEncodedRootLength(tc.path);
			gotest::assert::Equal(t, rootLength, tc.expected,
			                      "GetEncodedRootLength");

			bool isRooted = tspath::isRootedDiskPath(tc.path);
			gotest::assert::Equal(t, isRooted, tc.isRooted,
			                      "IsRootedDiskPath");
		});
	}
}

REGISTER_UNIT_TEST("tspath.TestUntitledPathHandling", TestUntitledPathHandling);
REGISTER_UNIT_TEST("tspath.TestUntitledPathEdgeCases",
				   TestUntitledPathEdgeCases);
