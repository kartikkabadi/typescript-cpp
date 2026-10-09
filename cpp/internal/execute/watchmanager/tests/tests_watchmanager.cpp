// tests_watchmanager.cpp — port of
// tsc/internal/execute/watchmanager/watchmanager_test.go.
#include <unordered_map>
#include <vector>

#include "internal/execute/watchmanager/watchmanager.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

namespace tsc {
namespace {
using namespace ::tsc::execute::watchmanager;


using gostd::testing::T;
using tspath::ComparePathsOptions;

inline const ComparePathsOptions caseSensitiveOpts{
    /*UseCaseSensitiveFileNames*/ true, /*CurrentDirectory*/ "/repo"};
inline const ComparePathsOptions caseInsensitiveOpts{
    /*UseCaseSensitiveFileNames*/ false, /*CurrentDirectory*/ "/repo"};

// TestDirWatchSetCoverage checks the core coverage rules: a recursive watch
// covers itself and all descendants, while a non-recursive watch covers only
// itself. Ancestors and unrelated paths are never covered.
void TestDirWatchSetCoverage(T* t) {
	t->Parallel();

	DirWatchSet set{caseSensitiveOpts};
	set.Set("/repo/src", true);             // recursive
	set.Set("/repo/config", false);         // non-recursive
	set.Set("/repo/node_modules/a", false); // non-recursive

	struct {
		std::string dir;
		bool want;
	} tests[] = {
	    {"/repo/src", true},             // exact recursive
	    {"/repo/src/nested", true},      // descendant of recursive
	    {"/repo/src/nested/deep", true}, // deep descendant of recursive
	    {"/repo/config", true},          // exact non-recursive
	    {"/repo/config/nested", false},  // descendant of non-recursive: NOT covered
	    {"/repo/node_modules/a", true},  // exact non-recursive
	    {"/repo/node_modules/b", false}, // sibling, absent
	    {"/repo", false},                // ancestor of watched dirs: NOT covered
	    {"/other", false},               // unrelated
	};
	for (auto& tt : tests) {
		if (auto got = set.Covered(tt.dir); got != tt.want) {
			t->Errorf("Covered(%q) = %v, want %v",
			          {tt.dir, got, tt.want});
		}
	}
}
REGISTER_UNIT_TEST("watchmanager.TestDirWatchSetCoverage",
                   TestDirWatchSetCoverage);

// TestDirWatchSetCaseSensitive verifies that on a case-sensitive filesystem a
// differently-cased directory is a distinct, uncovered directory.
void TestDirWatchSetCaseSensitive(T* t) {
	t->Parallel();

	DirWatchSet set{caseSensitiveOpts};
	set.Set("/repo/node_modules/a", false);
	set.Set("/repo/Src", true);

	if (!set.Covered("/repo/node_modules/a")) {
		t->Error({"expected /repo/node_modules/a covered"});
	}
	if (set.Covered("/repo/node_modules/A")) {
		t->Error({"case-sensitive FS must not cover differently-cased dir"});
	}
	if (!set.Covered("/repo/Src/nested")) {
		t->Error({"recursive descendant with matching case is covered"});
	}
	if (set.Covered("/repo/src/nested")) {
		t->Error({"case-sensitive FS must not cover differently-cased "
		          "descendant"});
	}
}
REGISTER_UNIT_TEST("watchmanager.TestDirWatchSetCaseSensitive",
                   TestDirWatchSetCaseSensitive);

// TestDirWatchSetCaseInsensitive verifies that on a case-insensitive
// filesystem coverage ignores casing for both exact matches and recursive
// containment.
void TestDirWatchSetCaseInsensitive(T* t) {
	t->Parallel();

	DirWatchSet set{caseInsensitiveOpts};
	set.Set("/repo/node_modules/a", false);
	set.Set("/repo/Src", true);

	if (!set.Covered("/repo/node_modules/A")) {
		t->Error({"exact match should be case-insensitive"});
	}
	if (!set.Covered("/REPO/NODE_MODULES/a")) {
		t->Error({"exact match should be case-insensitive across components"});
	}
	if (!set.Covered("/repo/src/nested/deep")) {
		t->Error({"recursive containment should be case-insensitive"});
	}
}
REGISTER_UNIT_TEST("watchmanager.TestDirWatchSetCaseInsensitive",
                   TestDirWatchSetCaseInsensitive);

// TestDirWatchSetCanonicalDedup verifies that on a case-insensitive
// filesystem directories that differ only by casing collapse to a single
// canonical entry, while a case-sensitive filesystem keeps them distinct.
void TestDirWatchSetCanonicalDedup(T* t) {
	t->Parallel();

	DirWatchSet insensitive{caseInsensitiveOpts};
	insensitive.Set("/repo/Node_Modules/PkgName", false);
	insensitive.Set("/repo/node_modules/pkgname", false); // same dir, different casing

	auto dirs = insensitive.Dirs();
	if (dirs.size() != 1) {
		t->Errorf("differently-cased dirs must collapse to one entry, got %d",
		          {(int)dirs.size()});
	}
	if (dirs.find("/repo/Node_Modules/PkgName") == dirs.end()) {
		t->Error({"Dirs must retain the original spelling used for "
		          "registration"});
	}

	DirWatchSet sensitive{caseSensitiveOpts};
	sensitive.Set("/repo/Node_Modules/PkgName", false);
	sensitive.Set("/repo/node_modules/pkgname", false); // distinct dirs when case-sensitive
	if (sensitive.Dirs().size() != 2) {
		t->Error({"case-sensitive FS keeps differently-cased dirs distinct"});
	}
}
REGISTER_UNIT_TEST("watchmanager.TestDirWatchSetCanonicalDedup",
                   TestDirWatchSetCanonicalDedup);

// TestDirWatchSetUpgradeToRecursive verifies that upgrading a directory from
// non-recursive to recursive begins covering its descendants.
void TestDirWatchSetUpgradeToRecursive(T* t) {
	t->Parallel();

	DirWatchSet set{caseSensitiveOpts};
	set.Set("/repo/src", false);
	if (!set.Covered("/repo/src")) {
		t->Error({"expected /repo/src covered"});
	}
	if (set.Covered("/repo/src/nested")) {
		t->Error({"descendant not covered while non-recursive"});
	}

	set.Set("/repo/src", true);
	if (!set.Covered("/repo/src/nested")) {
		t->Error({"descendant covered after upgrade to recursive"});
	}
	if (set.Dirs()["/repo/src"] != true) {
		t->Error({"Dirs[/repo/src] should be true"});
	}
}
REGISTER_UNIT_TEST("watchmanager.TestDirWatchSetUpgradeToRecursive",
                   TestDirWatchSetUpgradeToRecursive);

// TestDirWatchSetNeverDowngrades verifies a recursive watch is not downgraded
// by a subsequent non-recursive Set of the same directory.
void TestDirWatchSetNeverDowngrades(T* t) {
	t->Parallel();

	DirWatchSet set{caseSensitiveOpts};
	set.Set("/repo/src", true);
	set.Set("/repo/src", false);

	if (set.Dirs()["/repo/src"] != true) {
		t->Error({"Dirs[/repo/src] should be true"});
	}
	if (!set.Covered("/repo/src/nested")) {
		t->Error({"recursive coverage retained after non-recursive Set"});
	}
}
REGISTER_UNIT_TEST("watchmanager.TestDirWatchSetNeverDowngrades",
                   TestDirWatchSetNeverDowngrades);

// TestDirWatchSetDirs verifies the emitted map reflects every added
// directory with the expected recursive flags.
void TestDirWatchSetDirs(T* t) {
	t->Parallel();

	DirWatchSet set{caseSensitiveOpts};
	set.Set("/repo/a", false);
	set.Set("/repo/b", true);
	set.Set("/repo/a", false); // duplicate non-recursive add is idempotent

	auto dirs = set.Dirs();
	if (dirs.size() != 2) {
		t->Errorf("len(Dirs()) = %d, want 2", {(int)dirs.size()});
	}
	if (dirs["/repo/a"] != false) {
		t->Error({"Dirs[/repo/a] should be false"});
	}
	if (dirs["/repo/b"] != true) {
		t->Error({"Dirs[/repo/b] should be true"});
	}
}
REGISTER_UNIT_TEST("watchmanager.TestDirWatchSetDirs", TestDirWatchSetDirs);


// TestResolveDesiredDirsShallowProject — watchmanager_test.go: a directory
// that exists and was asked for is watched at any depth; a project close to
// the filesystem root (/app, /srv/app, a Docker WORKDIR) must not be
// silently ignored.
void TestResolveDesiredDirsShallowProject(T* t) {
	t->Parallel();

	std::unordered_map<std::string, bool> existing{
	    {"/", true},
	    {"/app", true},
	    {"/app/src", true},
	    {"/srv", true},
	    {"/srv/app", true},
	    {"/home", true},
	    {"/home/user", true},
	    {"/home/user/project", true},
	};
	WatchManager wm{nullptr, [&](const std::string& dir) {
		                return existing[dir];
	                }};

	auto resolved = wm.ResolveDesiredDirs({
	    {"/app", true},
	    {"/app/src", false},
	    {"/srv/app", true},
	    {"/home/user/project", true},
	});

	std::unordered_map<std::string, bool> want{
	    {"/app", true},
	    {"/app/src", false},
	    {"/srv/app", true},
	    {"/home/user/project", true},
	};
	if (resolved != want) {
		t->Errorf("ResolveDesiredDirs mismatch: got %v entries",
		          {resolved.size()});
	}
}
REGISTER_UNIT_TEST("watchmanager.TestResolveDesiredDirsShallowProject",
                   TestResolveDesiredDirsShallowProject);

// TestResolveDesiredDirsAncestorFallback — watchmanager_test.go: the depth
// check still guards the fallback to an ancestor, so a missing directory
// never turns into a watch on something too generic like /, /home or
// /home/user.
void TestResolveDesiredDirsAncestorFallback(T* t) {
	t->Parallel();

	std::unordered_map<std::string, bool> existing{
	    {"/", true},          {"/app", true},    {"/home", true},
	    {"/home/user", true}, {"/repo", true},   {"/repo/a", true},
	    {"/repo/a/b", true},  {"/repo/a/b/c", true},
	};
	WatchManager wm{nullptr, [&](const std::string& dir) {
		                return existing[dir];
	                }};

	auto resolved = wm.ResolveDesiredDirs({
	    {"/app/missing", true},             // ancestor /app is too shallow
	    {"/home/user/missing", true},       // ancestor /home/user too shallow
	    {"/repo/a/b/c/missing/deep", true}, // /repo/a/b/c deep enough
	    {"/nothing/exists/anywhere/", true},// no existing ancestor except /
	});

	std::unordered_map<std::string, bool> want{{"/repo/a/b/c", false}};
	if (resolved != want) {
		t->Errorf("ResolveDesiredDirs mismatch: got %v entries",
		          {resolved.size()});
	}
}
REGISTER_UNIT_TEST("watchmanager.TestResolveDesiredDirsAncestorFallback",
                   TestResolveDesiredDirsAncestorFallback);

// TestResolveDesiredDirsSkipsNonDiskPaths — watchmanager_test.go: a directory
// that is not on disk, such as the embedded libs (bundled:///libs), is never
// watched, even though the wrapped FS reports that it exists.
void TestResolveDesiredDirsSkipsNonDiskPaths(T* t) {
	t->Parallel();

	WatchManager wm{nullptr, [](const std::string&) { return true; }};

	auto resolved = wm.ResolveDesiredDirs({
	    {"bundled:///libs", false},
	    {"/app", true},
	});

	std::unordered_map<std::string, bool> want{{"/app", true}};
	if (resolved != want) {
		t->Errorf("ResolveDesiredDirs mismatch: got %v entries",
		          {resolved.size()});
	}
}
REGISTER_UNIT_TEST("watchmanager.TestResolveDesiredDirsSkipsNonDiskPaths",
                   TestResolveDesiredDirsSkipsNonDiskPaths);

} // namespace
} // namespace tsc
