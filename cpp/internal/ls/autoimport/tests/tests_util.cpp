// Port of tsc/internal/ls/autoimport/util_test.go.
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
using namespace tsc::ls::autoimport;
using namespace tsc;

static void TestWordIndices(T* t) {
	t->Parallel();
	struct testCase {
		std::string input;
		std::vector<std::string> expectedWords;
	};
	std::vector<testCase> tests = {
	    // Basic camelCase
	    {"camelCase", {"camelCase", "Case"}},
	    // snake_case
	    {"snake_case", {"snake_case", "case"}},
	    // ParseURL - uppercase sequence followed by lowercase
	    {"ParseURL", {"ParseURL", "URL"}},
	    // XMLHttpRequest - multiple uppercase sequences
	    {"XMLHttpRequest",
	     {"XMLHttpRequest", "HttpRequest", "Request"}},
	    // Single word lowercase
	    {"hello", {"hello"}},
	    // Single word uppercase
	    {"HELLO", {"HELLO"}},
	    // Mixed with numbers
	    {"parseHTML5Parser",
	     {"parseHTML5Parser", "HTML5Parser", "Parser"}},
	    // Underscore variations
	    {"__proto__", {"__proto__", "proto__"}},
	    {"_private_member", {"_private_member", "member"}},
	    // Single character
	    {"a", {"a"}},
	    {"A", {"A"}},
	    // Consecutive underscores
	    {"test__double__underscore",
	     {"test__double__underscore", "double__underscore",
	      "underscore"}},
	};

	for (auto& tt : tests) {
		t->Run(tt.input, [tt](T* t) {
			t->Parallel();
			auto indices = wordIndices(tt.input);

			// Convert indices to actual word slices for comparison
			std::vector<std::string> actualWords;
			for (int idx : indices) {
				actualWords.push_back(tt.input.substr(idx));
			}

			gotest::assert::Equal(t, actualWords, tt.expectedWords);
		});
	}
}
REGISTER_UNIT_TEST("ls/autoimport.TestWordIndices", TestWordIndices);

// TestGetPackageRealpathFuncs_FollowsNodeModulesSymlinks tests that
// toRealpath correctly follows symlinks for files outside the package
// directory (e.g. node_modules entries). Without this, the module resolver
// uses unresolved symlink paths as cache keys, causing the same file to be
// loaded multiple times and triggering massive memory usage when barrel
// files re-export from many symlinked packages (issue #2780).
static void TestGetPackageRealpathFuncs_FollowsNodeModulesSymlinks(T* t) {
	t->Parallel();

	// Simulate a layout where the package directory is itself a symlink
	// (e.g. Bazel's convenience symlinks or pnpm's virtual store):
	//   /symlink-bin/pkg/              -> symlink to /real/bin/pkg/
	//   /real/bin/pkg/node_modules/dep -> symlink to /real/dep/
	//
	// When toRealpath is used as the module resolver's Realpath, it must
	// follow the node_modules symlink so that
	// /real/bin/pkg/node_modules/dep/index.d.ts resolves to
	// /real/dep/index.d.ts — otherwise the same dep file gets different
	// cache keys depending on which path it was reached through.
	auto fs = vfs::vfstest::FromMap(
	    {{"/symlink-bin/pkg", vfs::vfstest::Symlink("/real/bin/pkg")},
	     {"/real/bin/pkg/index.d.ts",
	      "export declare const a: number;"},
	     {"/real/bin/pkg/node_modules/.package-lock.json", "{}"},
	     {"/real/bin/pkg/node_modules/dep",
	      vfs::vfstest::Symlink("/real/dep")},
	     {"/real/bin/pkg/node_modules/@scope/dep",
	      vfs::vfstest::Symlink("/real/scoped-dep")},
	     {"/real/dep/index.d.ts", "export declare const b: number;"},
	     {"/real/dep/src/utils/helper.d.ts",
	      "export declare const c: number;"},
	     {"/real/scoped-dep/index.d.ts",
	      "export declare const d: number;"}},
	    true);

	auto [toRealpath, _p] =
	    getPackageRealpathFuncs(fs.get(), "/symlink-bin/pkg");

	// Files directly within node_modules must not seed a cache entry
	// that prevents a later package-root directory from following its
	// symlink.
	gotest::assert::Equal(
	    t,
	    toRealpath(
	        "/real/bin/pkg/node_modules/.package-lock.json"),
	    std::string("/real/bin/pkg/node_modules/.package-lock.json"));

	// Files inside the package should be converted via string replacement
	// (fast path).
	gotest::assert::Equal(
	    t, toRealpath("/symlink-bin/pkg/index.d.ts"),
	    std::string("/real/bin/pkg/index.d.ts"),
	    "package files should be converted via prefix replacement");

	// A sibling whose name starts with the package name is not inside
	// the package.
	gotest::assert::Equal(
	    t, toRealpath("/symlink-bin/pkg2/index.d.ts"),
	    std::string("/symlink-bin/pkg2/index.d.ts"),
	    "sibling package paths must not use prefix replacement");

	// Files outside the package (e.g. node_modules symlinks) should be
	// resolved via fs.Realpath so the cache key is the canonical realpath,
	// not the symlink path.
	gotest::assert::Equal(
	    t, toRealpath("/real/bin/pkg/node_modules/dep/index.d.ts"),
	    std::string("/real/dep/index.d.ts"),
	    "node_modules symlinks must be followed so the same file gets a "
	    "consistent cache key");

	// The module resolver also uses toRealpath while traversing
	// directories.
	gotest::assert::Equal(
	    t, toRealpath("/real/bin/pkg/node_modules/dep"),
	    std::string("/real/dep"),
	    "package-root directories should follow their node_modules "
	    "symlink");

	// Walking the scope directory first must not seed a cache entry
	// that prevents a nested scoped package from following its symlink.
	gotest::assert::Equal(
	    t, toRealpath("/real/bin/pkg/node_modules/@scope"),
	    std::string("/real/bin/pkg/node_modules/@scope"));
	gotest::assert::Equal(
	    t, toRealpath("/real/bin/pkg/node_modules/@scope/dep"),
	    std::string("/real/scoped-dep"),
	    "scoped package-root directories should follow their "
	    "node_modules symlink");

	// Files in subdirectories of an already-resolved external package
	// should use the cached prefix mapping without additional realpath
	// calls.
	gotest::assert::Equal(
	    t,
	    toRealpath(
	        "/real/bin/pkg/node_modules/dep/src/utils/helper.d.ts"),
	    std::string("/real/dep/src/utils/helper.d.ts"),
	    "subdirectories of a resolved external package should use cached "
	    "prefix mapping");
}
REGISTER_UNIT_TEST(
    "ls/autoimport.TestGetPackageRealpathFuncs_FollowsNodeModulesSymlinks",
    TestGetPackageRealpathFuncs_FollowsNodeModulesSymlinks);

// TestGetPackageRealpathFuncs_DuplicateCacheKeys demonstrates how the broken
// toRealpath causes the same physical file to get different cache keys when
// reached through different symlink paths. In pnpm/Bazel monorepos, multiple
// packages may have node_modules symlinks that point to the same physical
// dependency. Because toRealpath doesn't follow symlinks for files outside
// the package directory, each path is treated as distinct, leading to
// duplicate file loads and memory bloat (issue #2780).
static void TestGetPackageRealpathFuncs_DuplicateCacheKeys(T* t) {
	t->Parallel();

	// Simulate two packages (app-a, app-b) that each have a node_modules
	// symlink to the same shared dependency. This is a typical pnpm/Bazel
	// layout:
	//   /workspace/packages/app-a/              -> symlink to /store/app-a/
	//   /workspace/packages/app-b/              -> symlink to /store/app-b/
	//   /store/app-a/node_modules/shared-lib  -> symlink to
	//       /store/shared-lib/
	//   /store/app-b/node_modules/shared-lib  -> symlink to
	//       /store/shared-lib/
	auto fs = vfs::vfstest::FromMap(
	    {{"/workspace/packages/app-a",
	      vfs::vfstest::Symlink("/store/app-a")},
	     {"/workspace/packages/app-b",
	      vfs::vfstest::Symlink("/store/app-b")},
	     {"/store/app-a/index.d.ts",
	      "export declare const a: number;"},
	     {"/store/app-b/index.d.ts",
	      "export declare const b: number;"},
	     {"/store/app-a/node_modules/shared-lib",
	      vfs::vfstest::Symlink("/store/shared-lib")},
	     {"/store/app-b/node_modules/shared-lib",
	      vfs::vfstest::Symlink("/store/shared-lib")},
	     {"/store/shared-lib/index.d.ts",
	      "export declare const shared: string;"}},
	    true);

	auto [toRealpathA, _a] =
	    getPackageRealpathFuncs(fs.get(), "/workspace/packages/app-a");
	auto [toRealpathB, _b] =
	    getPackageRealpathFuncs(fs.get(), "/workspace/packages/app-b");

	std::string sharedFileViaA =
	    "/store/app-a/node_modules/shared-lib/index.d.ts";
	std::string sharedFileViaB =
	    "/store/app-b/node_modules/shared-lib/index.d.ts";

	auto resolvedA = toRealpathA(sharedFileViaA);
	auto resolvedB = toRealpathB(sharedFileViaB);

	// Both should resolve to the same canonical realpath so the module
	// resolver uses a single cache key for the shared dependency, avoiding
	// duplicate loads.
	std::string expectedRealpath = "/store/shared-lib/index.d.ts";
	gotest::assert::Equal(
	    t, resolvedA, expectedRealpath,
	    "app-a's toRealpath should follow the node_modules symlink to the "
	    "realpath");
	gotest::assert::Equal(
	    t, resolvedB, expectedRealpath,
	    "app-b's toRealpath should follow the node_modules symlink to the "
	    "realpath");
}
REGISTER_UNIT_TEST(
    "ls/autoimport.TestGetPackageRealpathFuncs_DuplicateCacheKeys",
    TestGetPackageRealpathFuncs_DuplicateCacheKeys);

// TestGetPackageRealpathFuncs_NonSymlinkedPackageWithSymlinkedDeps tests
// that even when the package directory itself is NOT a symlink, toRealpath
// still follows symlinks for files outside the package (e.g. re-exports
// reaching into symlinked node_modules dependencies).
static void TestGetPackageRealpathFuncs_NonSymlinkedPackageWithSymlinkedDeps(
    T* t) {
	t->Parallel();

	auto fs = vfs::vfstest::FromMap(
	    {{"/real/my-pkg/index.d.ts",
	      "export declare const a: number;"},
	     {"/real/my-pkg/node_modules/dep",
	      vfs::vfstest::Symlink("/real/dep")},
	     {"/real/dep/index.d.ts", "export declare const b: number;"}},
	    true);

	auto [toRealpath, _p] =
	    getPackageRealpathFuncs(fs.get(), "/real/my-pkg");

	// Files inside the (non-symlinked) package should be returned
	// unchanged.
	gotest::assert::Equal(t,
	                      toRealpath("/real/my-pkg/index.d.ts"),
	                      std::string("/real/my-pkg/index.d.ts"));

	// Files outside the package reached via symlinked node_modules should
	// still be resolved.
	gotest::assert::Equal(
	    t, toRealpath("/real/my-pkg/node_modules/dep/index.d.ts"),
	    std::string("/real/dep/index.d.ts"),
	    "symlinked deps must be resolved even when the package dir itself "
	    "is not a symlink");
}
REGISTER_UNIT_TEST(
    "ls/autoimport.TestGetPackageRealpathFuncs_NonSymlinkedPackageWithSymlinkedDeps",
    TestGetPackageRealpathFuncs_NonSymlinkedPackageWithSymlinkedDeps);
