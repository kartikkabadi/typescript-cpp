// Port of tsc/internal/tspath/ignoredpaths_test.go (package tspath).
#include <string>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
using namespace tsc;
using tsc::tspath::containsIgnoredPath;

static void TestContainsIgnoredPath(T* t) {
	t->Parallel();
	struct testCase {
		const char* name;
		const char* path;
		bool expected;
	};
	static const testCase tests[] = {
	    {"node_modules dot path", "/project/node_modules/.pnpm/file.ts", true},
	    {"git directory", "/project/.git/hooks/pre-commit", true},
	    {"emacs lock file", "/project/src/file.ts.#", true},
	    {"regular file path", "/project/src/file.ts", false},
	    {"node_modules without dot", "/project/node_modules/lodash/index.js",
	     false},
	    {"empty path", "", false},
	    {"path with multiple ignored patterns",
	     "/project/node_modules/.pnpm/.git/.#file.ts", true},
	    {"case sensitive test", "/project/NODE_MODULES/.PNPM/file.ts", false},
	    {"path with ignored pattern in middle",
	     "/project/src/node_modules/.pnpm/dist/file.js", true},
	    {"path with ignored pattern at end", "/project/src/file.ts.#", true},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&tt](T* t) {
			t->Parallel();
			bool result = containsIgnoredPath(tt.path);
			if (result != tt.expected) {
				t->Errorf("ContainsIgnoredPath(%s) = %v, expected %v",
				          {tt.path, result, tt.expected});
			}
		});
	}
}

static void TestIgnoredPathsPatterns(T* t) {
	t->Parallel();
	// Test that all expected patterns are present
	static const char* expectedPatterns[] = {"/node_modules/.", "/.git", ".#"};

	for (auto* pattern : expectedPatterns) {
		std::string testPath = std::string("/test") + pattern + "/file.ts";
		if (!containsIgnoredPath(testPath)) {
			t->Errorf("Expected pattern '%s' to be detected in path '%s'",
			          {pattern, testPath});
		}
	}
}

static void TestIgnoredPathsEdgeCases(T* t) {
	t->Parallel();
	struct testCase {
		const char* name;
		const char* path;
		bool expected;
	};
	static const testCase tests[] = {
	    // Pattern is "/node_modules/." not "/node_modules."
	    {"pattern at start", "/node_modules./file.ts", false},
	    {"pattern at end", "/project/file.ts.#", true},
	    {"multiple occurrences", "/project/.git/node_modules./.git/file.ts",
	     true},
	    {"no slashes", "node_modules.file.ts", false},
	    {"single slash", "/file.ts", false},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&tt](T* t) {
			t->Parallel();
			bool result = containsIgnoredPath(tt.path);
			if (result != tt.expected) {
				t->Errorf("ContainsIgnoredPath(%s) = %v, expected %v",
				          {tt.path, result, tt.expected});
			}
		});
	}
}

REGISTER_UNIT_TEST("tspath.TestContainsIgnoredPath", TestContainsIgnoredPath);
REGISTER_UNIT_TEST("tspath.TestIgnoredPathsPatterns", TestIgnoredPathsPatterns);
REGISTER_UNIT_TEST("tspath.TestIgnoredPathsEdgeCases",
				   TestIgnoredPathsEdgeCases);
