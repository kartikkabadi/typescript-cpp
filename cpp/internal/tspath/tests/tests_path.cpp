// Port of tsc/internal/tspath/path_test.go (package tspath).
// Benchmark*/Fuzz* functions are not ported — they do not run under `go test`.
// The *_old reference implementations used only by those are likewise omitted.
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
using namespace tsc;
using namespace tsc::tspath;

static void TestNormalizeSlashes(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, normalizeSlashes("a"), "a");
	gotest::assert::Equal(t, normalizeSlashes("a/b"), "a/b");
	gotest::assert::Equal(t, normalizeSlashes("a\\b"), "a/b");
	gotest::assert::Equal(t, normalizeSlashes("\\\\server\\path"), "//server/path");
}

static void TestGetRootLength(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, getRootLength("a"), 0);
	gotest::assert::Equal(t, getRootLength("/"), 1);
	gotest::assert::Equal(t, getRootLength("/path"), 1);
	gotest::assert::Equal(t, getRootLength("c:"), 2);
	gotest::assert::Equal(t, getRootLength("c:d"), 0);
	gotest::assert::Equal(t, getRootLength("c:/"), 3);
	gotest::assert::Equal(t, getRootLength("c:\\"), 3);
	gotest::assert::Equal(t, getRootLength("//server"), 8);
	gotest::assert::Equal(t, getRootLength("//server/share"), 9);
	gotest::assert::Equal(t, getRootLength("\\\\server"), 8);
	gotest::assert::Equal(t, getRootLength("\\\\server\\share"), 9);
	gotest::assert::Equal(t, getRootLength("file:///"), 8);
	gotest::assert::Equal(t, getRootLength("file:///path"), 8);
	gotest::assert::Equal(t, getRootLength("file:///c:"), 10);
	gotest::assert::Equal(t, getRootLength("file:///c:d"), 8);
	gotest::assert::Equal(t, getRootLength("file:///c:/path"), 11);
	gotest::assert::Equal(t, getRootLength("file:///c%3a"), 12);
	gotest::assert::Equal(t, getRootLength("file:///c%3ad"), 8);
	gotest::assert::Equal(t, getRootLength("file:///c%3a/path"), 13);
	gotest::assert::Equal(t, getRootLength("file:///c%3A"), 12);
	gotest::assert::Equal(t, getRootLength("file:///c%3Ad"), 8);
	gotest::assert::Equal(t, getRootLength("file:///c%3A/path"), 13);
	gotest::assert::Equal(t, getRootLength("file://localhost"), 16);
	gotest::assert::Equal(t, getRootLength("file://localhost/"), 17);
	gotest::assert::Equal(t, getRootLength("file://localhost/path"), 17);
	gotest::assert::Equal(t, getRootLength("file://localhost/c:"), 19);
	gotest::assert::Equal(t, getRootLength("file://localhost/c:d"), 17);
	gotest::assert::Equal(t, getRootLength("file://localhost/c:/path"), 20);
	gotest::assert::Equal(t, getRootLength("file://localhost/c%3a"), 21);
	gotest::assert::Equal(t, getRootLength("file://localhost/c%3ad"), 17);
	gotest::assert::Equal(t, getRootLength("file://localhost/c%3a/path"), 22);
	gotest::assert::Equal(t, getRootLength("file://localhost/c%3A"), 21);
	gotest::assert::Equal(t, getRootLength("file://localhost/c%3Ad"), 17);
	gotest::assert::Equal(t, getRootLength("file://localhost/c%3A/path"), 22);
	gotest::assert::Equal(t, getRootLength("FILE:///C:/path"), 11);
	gotest::assert::Equal(t, getRootLength("file://LOCALHOST/C%3A/path"), 22);
	gotest::assert::Equal(t, getRootLength("file://server"), 13);
	gotest::assert::Equal(t, getRootLength("file://server/"), 14);
	gotest::assert::Equal(t, getRootLength("file://server/path"), 14);
	gotest::assert::Equal(t, getRootLength("file://server/c:"), 14);
	gotest::assert::Equal(t, getRootLength("file://server/c:d"), 14);
	gotest::assert::Equal(t, getRootLength("file://server/c:/d"), 14);
	gotest::assert::Equal(t, getRootLength("file://server/c%3a"), 14);
	gotest::assert::Equal(t, getRootLength("file://server/c%3ad"), 14);
	gotest::assert::Equal(t, getRootLength("file://server/c%3a/d"), 14);
	gotest::assert::Equal(t, getRootLength("file://server/c%3A"), 14);
	gotest::assert::Equal(t, getRootLength("file://server/c%3Ad"), 14);
	gotest::assert::Equal(t, getRootLength("file://server/c%3A/d"), 14);
	gotest::assert::Equal(t, getRootLength("http://server"), 13);
	gotest::assert::Equal(t, getRootLength("http://server/path"), 14);
}

static void TestPathIsAbsolute(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, pathIsAbsolute("/path/to/file.ext"), true);
	gotest::assert::Equal(t, pathIsAbsolute("c:/path/to/file.ext"), true);
	gotest::assert::Equal(t, pathIsAbsolute("file:///path/to/file.ext"), true);
	gotest::assert::Equal(t, pathIsAbsolute("path/to/file.ext"), false);
	gotest::assert::Equal(t, pathIsAbsolute("./path/to/file.ext"), false);
}

static void TestIsUrl(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, isUrl("a"), false);
	gotest::assert::Equal(t, isUrl("/"), false);
	gotest::assert::Equal(t, isUrl("c:"), false);
	gotest::assert::Equal(t, isUrl("c:d"), false);
	gotest::assert::Equal(t, isUrl("c:/"), false);
	gotest::assert::Equal(t, isUrl("c:\\"), false);
	gotest::assert::Equal(t, isUrl("//server"), false);
	gotest::assert::Equal(t, isUrl("//server/share"), false);
	gotest::assert::Equal(t, isUrl("\\\\server"), false);
	gotest::assert::Equal(t, isUrl("\\\\server\\share"), false);
	gotest::assert::Equal(t, isUrl("file:///path"), true);
	gotest::assert::Equal(t, isUrl("file:///c:"), true);
	gotest::assert::Equal(t, isUrl("file:///c:d"), true);
	gotest::assert::Equal(t, isUrl("file:///c:/path"), true);
	gotest::assert::Equal(t, isUrl("file://server"), true);
	gotest::assert::Equal(t, isUrl("file://server/path"), true);
	gotest::assert::Equal(t, isUrl("http://server"), true);
	gotest::assert::Equal(t, isUrl("http://server/path"), true);
}

static void TestIsRootedDiskPath(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, isRootedDiskPath("a"), false);
	gotest::assert::Equal(t, isRootedDiskPath("/"), true);
	gotest::assert::Equal(t, isRootedDiskPath("c:"), true);
	gotest::assert::Equal(t, isRootedDiskPath("c:d"), false);
	gotest::assert::Equal(t, isRootedDiskPath("c:/"), true);
	gotest::assert::Equal(t, isRootedDiskPath("c:\\"), true);
	gotest::assert::Equal(t, isRootedDiskPath("//server"), true);
	gotest::assert::Equal(t, isRootedDiskPath("//server/share"), true);
	gotest::assert::Equal(t, isRootedDiskPath("\\\\server"), true);
	gotest::assert::Equal(t, isRootedDiskPath("\\\\server\\share"), true);
	gotest::assert::Equal(t, isRootedDiskPath("file:///path"), false);
	gotest::assert::Equal(t, isRootedDiskPath("file:///c:"), false);
	gotest::assert::Equal(t, isRootedDiskPath("file:///c:d"), false);
	gotest::assert::Equal(t, isRootedDiskPath("file:///c:/path"), false);
	gotest::assert::Equal(t, isRootedDiskPath("file://server"), false);
	gotest::assert::Equal(t, isRootedDiskPath("file://server/path"), false);
	gotest::assert::Equal(t, isRootedDiskPath("http://server"), false);
	gotest::assert::Equal(t, isRootedDiskPath("http://server/path"), false);
}

static void TestGetDirectoryPath(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, getDirectoryPath(""), "");
	gotest::assert::Equal(t, getDirectoryPath("a"), "");
	gotest::assert::Equal(t, getDirectoryPath("a/b"), "a");
	gotest::assert::Equal(t, getDirectoryPath("/"), "/");
	gotest::assert::Equal(t, getDirectoryPath("/a"), "/");
	gotest::assert::Equal(t, getDirectoryPath("/a/"), "/");
	gotest::assert::Equal(t, getDirectoryPath("/a/b"), "/a");
	gotest::assert::Equal(t, getDirectoryPath("/a/b/"), "/a");
	gotest::assert::Equal(t, getDirectoryPath("c:"), "c:");
	gotest::assert::Equal(t, getDirectoryPath("c:d"), "");
	gotest::assert::Equal(t, getDirectoryPath("c:/"), "c:/");
	gotest::assert::Equal(t, getDirectoryPath("c:/path"), "c:/");
	gotest::assert::Equal(t, getDirectoryPath("c:/path/"), "c:/");
	gotest::assert::Equal(t, getDirectoryPath("//server"), "//server");
	gotest::assert::Equal(t, getDirectoryPath("//server/"), "//server/");
	gotest::assert::Equal(t, getDirectoryPath("//server/share"), "//server/");
	gotest::assert::Equal(t, getDirectoryPath("//server/share/"), "//server/");
	gotest::assert::Equal(t, getDirectoryPath("\\\\server"), "//server");
	gotest::assert::Equal(t, getDirectoryPath("\\\\server\\"), "//server/");
	gotest::assert::Equal(t, getDirectoryPath("\\\\server\\share"), "//server/");
	gotest::assert::Equal(t, getDirectoryPath("\\\\server\\share\\"), "//server/");
	gotest::assert::Equal(t, getDirectoryPath("file:///"), "file:///");
	gotest::assert::Equal(t, getDirectoryPath("file:///path"), "file:///");
	gotest::assert::Equal(t, getDirectoryPath("file:///path/"), "file:///");
	gotest::assert::Equal(t, getDirectoryPath("file:///c:"), "file:///c:");
	gotest::assert::Equal(t, getDirectoryPath("file:///c:d"), "file:///");
	gotest::assert::Equal(t, getDirectoryPath("file:///c:/"), "file:///c:/");
	gotest::assert::Equal(t, getDirectoryPath("file:///c:/path"), "file:///c:/");
	gotest::assert::Equal(t, getDirectoryPath("file:///c:/path/"), "file:///c:/");
	gotest::assert::Equal(t, getDirectoryPath("file://server"), "file://server");
	gotest::assert::Equal(t, getDirectoryPath("file://server/"), "file://server/");
	gotest::assert::Equal(t, getDirectoryPath("file://server/path"), "file://server/");
	gotest::assert::Equal(t, getDirectoryPath("file://server/path/"), "file://server/");
	gotest::assert::Equal(t, getDirectoryPath("http://server"), "http://server");
	gotest::assert::Equal(t, getDirectoryPath("http://server/"), "http://server/");
	gotest::assert::Equal(t, getDirectoryPath("http://server/path"), "http://server/");
	gotest::assert::Equal(t, getDirectoryPath("http://server/path/"), "http://server/");
}

static void TestGetLongestExtensionFromPath(T* t) {
	t->Parallel();
	std::vector<std::string_view> extensions = {".z", ".y.z", ".other"};
	gotest::assert::Equal(t, getLongestExtensionFromPath("/src/Component.y.z", extensions, false), ".y.z");
	gotest::assert::Equal(t, getLongestExtensionFromPath("/src/Component.z", extensions, false), ".z");
	gotest::assert::Equal(t, getLongestExtensionFromPath("/src/Component.y.Z", extensions, false), "");
	gotest::assert::Equal(t, getLongestExtensionFromPath("/src/Component.y.Z", extensions, true), ".y.Z");
}

static void TestRemoveAnyFileExtension(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, removeAnyFileExtension("/src/Component.vue"), "/src/Component");
	gotest::assert::Equal(t, removeAnyFileExtension("/src/Component.d.ts"), "/src/Component");
	gotest::assert::Equal(t, removeAnyFileExtension("/src/Component"), "/src/Component");
}

static void TestGetPathComponents(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, getPathComponents(""),
	                      std::vector<std::string>{""});
	gotest::assert::Equal(t, getPathComponents("a"),
	                      std::vector<std::string>{"", "a"});
	gotest::assert::Equal(t, getPathComponents("./a"),
	                      std::vector<std::string>{"", ".", "a"});
	gotest::assert::Equal(t, getPathComponents("/"),
	                      std::vector<std::string>{"/"});
	gotest::assert::Equal(t, getPathComponents("/a"),
	                      std::vector<std::string>{"/", "a"});
	gotest::assert::Equal(t, getPathComponents("/a/"),
	                      std::vector<std::string>{"/", "a"});
	gotest::assert::Equal(t, getPathComponents("c:"),
	                      std::vector<std::string>{"c:"});
	gotest::assert::Equal(t, getPathComponents("c:d"),
	                      std::vector<std::string>{"", "c:d"});
	gotest::assert::Equal(t, getPathComponents("c:/"),
	                      std::vector<std::string>{"c:/"});
	gotest::assert::Equal(t, getPathComponents("c:/path"),
	                      std::vector<std::string>{"c:/", "path"});
	gotest::assert::Equal(t, getPathComponents("//server"),
	                      std::vector<std::string>{"//server"});
	gotest::assert::Equal(t, getPathComponents("//server/"),
	                      std::vector<std::string>{"//server/"});
	gotest::assert::Equal(t, getPathComponents("//server/share"),
	                      std::vector<std::string>{"//server/", "share"});
	gotest::assert::Equal(t, getPathComponents("file:///"),
	                      std::vector<std::string>{"file:///"});
	gotest::assert::Equal(t, getPathComponents("file:///path"),
	                      std::vector<std::string>{"file:///", "path"});
	gotest::assert::Equal(t, getPathComponents("file:///c:"),
	                      std::vector<std::string>{"file:///c:"});
	gotest::assert::Equal(t, getPathComponents("file:///c:d"),
	                      std::vector<std::string>{"file:///", "c:d"});
	gotest::assert::Equal(t, getPathComponents("file:///c:/"),
	                      std::vector<std::string>{"file:///c:/"});
	gotest::assert::Equal(t, getPathComponents("file:///c:/path"),
	                      std::vector<std::string>{"file:///c:/", "path"});
	gotest::assert::Equal(t, getPathComponents("file://server"),
	                      std::vector<std::string>{"file://server"});
	gotest::assert::Equal(t, getPathComponents("file://server/"),
	                      std::vector<std::string>{"file://server/"});
	gotest::assert::Equal(t, getPathComponents("file://server/path"),
	                      std::vector<std::string>{"file://server/", "path"});
	gotest::assert::Equal(t, getPathComponents("http://server"),
	                      std::vector<std::string>{"http://server"});
	gotest::assert::Equal(t, getPathComponents("http://server/"),
	                      std::vector<std::string>{"http://server/"});
	gotest::assert::Equal(t, getPathComponents("http://server/path"),
	                      std::vector<std::string>{"http://server/", "path"});
}

static void TestReducePathComponents(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, reducePathComponents({""}),
	                      std::vector<std::string>{""});
	gotest::assert::Equal(t, reducePathComponents({"", "."}),
	                      std::vector<std::string>{""});
	gotest::assert::Equal(t, reducePathComponents({"", ".", "a"}),
	                      std::vector<std::string>{"", "a"});
	gotest::assert::Equal(t, reducePathComponents({"", "a", "."}),
	                      std::vector<std::string>{"", "a"});
	gotest::assert::Equal(t, reducePathComponents({"", ".."}),
	                      std::vector<std::string>{"", ".."});
	gotest::assert::Equal(t, reducePathComponents({"", "..", ".."}),
	                      std::vector<std::string>{"", "..", ".."});
	gotest::assert::Equal(t, reducePathComponents({"", "..", ".", ".."}),
	                      std::vector<std::string>{"", "..", ".."});
	gotest::assert::Equal(t, reducePathComponents({"", "a", ".."}),
	                      std::vector<std::string>{""});
	gotest::assert::Equal(t, reducePathComponents({"", "..", "a"}),
	                      std::vector<std::string>{"", "..", "a"});
	gotest::assert::Equal(t, reducePathComponents({"/"}),
	                      std::vector<std::string>{"/"});
	gotest::assert::Equal(t, reducePathComponents({"/", "."}),
	                      std::vector<std::string>{"/"});
	gotest::assert::Equal(t, reducePathComponents({"/", ".."}),
	                      std::vector<std::string>{"/"});
	gotest::assert::Equal(t, reducePathComponents({"/", "a", ".."}),
	                      std::vector<std::string>{"/"});
}

static void TestCombinePaths(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, combinePaths("path", {"to", "file.ext"}), "path/to/file.ext");
	gotest::assert::Equal(t, combinePaths("path", {"dir", "..", "to", "file.ext"}), "path/dir/../to/file.ext");
	gotest::assert::Equal(t, combinePaths("/path", {"to", "file.ext"}), "/path/to/file.ext");
	gotest::assert::Equal(t, combinePaths("/path", {"/to", "file.ext"}), "/to/file.ext");
	gotest::assert::Equal(t, combinePaths("c:/path", {"to", "file.ext"}), "c:/path/to/file.ext");
	gotest::assert::Equal(t, combinePaths("c:/path", {"c:/to", "file.ext"}), "c:/to/file.ext");
	gotest::assert::Equal(t, combinePaths("file:///path", {"to", "file.ext"}), "file:///path/to/file.ext");
	gotest::assert::Equal(t, combinePaths("file:///path", {"file:///to", "file.ext"}), "file:///to/file.ext");
	gotest::assert::Equal(t, combinePaths("/", {"/node_modules/@types"}), "/node_modules/@types");
	gotest::assert::Equal(t, combinePaths("/a/..", {""}), "/a/..");
	gotest::assert::Equal(t, combinePaths("/a/..", {"b"}), "/a/../b");
	gotest::assert::Equal(t, combinePaths("/a/..", {"b/"}), "/a/../b/");
	gotest::assert::Equal(t, combinePaths("/a/..", {"/"}), "/");
	gotest::assert::Equal(t, combinePaths("/a/..", {"/b"}), "/b");
}

static void TestResolvePath(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, resolvePath("", {}), "");
	gotest::assert::Equal(t, resolvePath(".", {}), "");
	gotest::assert::Equal(t, resolvePath("./", {}), "");
	gotest::assert::Equal(t, resolvePath("..", {}), "..");
	gotest::assert::Equal(t, resolvePath("../", {}), "../");
	gotest::assert::Equal(t, resolvePath("/", {}), "/");
	gotest::assert::Equal(t, resolvePath("/.", {}), "/");
	gotest::assert::Equal(t, resolvePath("/./", {}), "/");
	gotest::assert::Equal(t, resolvePath("/../", {}), "/");
	gotest::assert::Equal(t, resolvePath("/a", {}), "/a");
	gotest::assert::Equal(t, resolvePath("/a/", {}), "/a/");
	gotest::assert::Equal(t, resolvePath("/a/.", {}), "/a");
	gotest::assert::Equal(t, resolvePath("/a/./", {}), "/a/");
	gotest::assert::Equal(t, resolvePath("/a/./b", {}), "/a/b");
	gotest::assert::Equal(t, resolvePath("/a/./b/", {}), "/a/b/");
	gotest::assert::Equal(t, resolvePath("/a/..", {}), "/");
	gotest::assert::Equal(t, resolvePath("/a/../", {}), "/");
	gotest::assert::Equal(t, resolvePath("/a/../b", {}), "/b");
	gotest::assert::Equal(t, resolvePath("/a/../b/", {}), "/b/");
	gotest::assert::Equal(t, resolvePath("/a/..", {"b"}), "/b");
	gotest::assert::Equal(t, resolvePath("/a/..", {"/"}), "/");
	gotest::assert::Equal(t, resolvePath("/a/..", {"b/"}), "/b/");
	gotest::assert::Equal(t, resolvePath("/a/..", {"/b"}), "/b");
	gotest::assert::Equal(t, resolvePath("/a/.", {"b"}), "/a/b");
	gotest::assert::Equal(t, resolvePath("/a/.", {"."}), "/a");
	gotest::assert::Equal(t, resolvePath("a", {"b", "c"}), "a/b/c");
	gotest::assert::Equal(t, resolvePath("a", {"b", "/c"}), "/c");
	gotest::assert::Equal(t, resolvePath("a", {"b", "../c"}), "a/c");
}

static void TestGetNormalizedAbsolutePath(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/.", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/./", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/../", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a", ""), "/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/", ""), "/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/.", ""), "/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/foo.", ""), "/a/foo.");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/./", ""), "/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/./b", ""), "/a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/./b/", ""), "/a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/..", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/../", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/../", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/../b", ""), "/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/../b/", ""), "/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/..", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/..", "/"), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/..", "b/"), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/..", "/b"), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/.", "b"), "/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/.", "."), "/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\.", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\.\\", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\..\\", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\.\\", ""), "/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\.\\b", ""), "/a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\.\\b\\", ""), "/a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\..", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\..\\", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\..\\", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\..\\b", ""), "/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\..\\b\\", ""), "/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\..", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\..", "\\"), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\..", "b\\"), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\..", "\\b"), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\.", "b"), "/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\.", "."), "/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("", ""), "");
	gotest::assert::Equal(t, getNormalizedAbsolutePath(".", ""), "");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("./", ""), "");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("..", ""), "..");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("../", ""), "..");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("", "/home"), "/home");
	gotest::assert::Equal(t, getNormalizedAbsolutePath(".", "/home"), "/home");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("./", "/home"), "/home");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("..", "/home"), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("../", "/home"), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a", "b"), "b/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a", "b/c"), "b/c/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath(".a", ""), ".a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("..a", ""), "..a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a.", ""), "a.");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a..", ""), "a..");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/base/./.a", ""), "/base/.a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/base/../.a", ""), "/.a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/base/./..a", ""), "/base/..a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/base/../..a", ""), "/..a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/base/./..a/b", ""), "/base/..a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/base/../..a/b", ""), "/..a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/base/./a.", ""), "/base/a.");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/base/../a.", ""), "/a.");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/base/./a..", ""), "/base/a..");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/base/../a..", ""), "/a..");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/base/./a../b", ""), "/base/a../b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/base/../a../b", ""), "/a../b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a/..", ""), "");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a//", ""), "/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("//a", "a"), "//a/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/\\", ""), "//");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a///", "a"), "a/a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/.//", ""), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("//\\\\", ""), "///");
	gotest::assert::Equal(t, getNormalizedAbsolutePath(".//a", "."), "a");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a/../..", ""), "..");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("../..", "\\a"), "/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a:", "b"), "a:/");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a/../..", ".."), "../..");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a/../..", "b"), "");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a//../..", ".."), "../..");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a//b", ""), "a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a///b", ""), "a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a/b//c", ""), "a/b/c");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("/a/b//c", ""), "/a/b/c");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("//a/b//c", ""), "//a/b/c");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a\\\\b", ""), "a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a\\\\\\b", ""), "a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a\\b\\\\c", ""), "a/b/c");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\b\\\\c", ""), "/a/b/c");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\\\a\\b\\\\c", ""), "//a/b/c");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a/\\b", ""), "a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a\\/b", ""), "a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a\\/\\b", ""), "a/b");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("a\\b//c", ""), "a/b/c");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\a\\b\\\\c", ""), "/a/b/c");
	gotest::assert::Equal(t, getNormalizedAbsolutePath("\\\\a\\b\\\\c", ""), "//a/b/c");
}

static void TestGetNormalizedAbsolutePathWithoutRoot(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, getNormalizedAbsolutePathWithoutRoot("/a/b/c.txt", "/a/b"), "a/b/c.txt");
	gotest::assert::Equal(t, getNormalizedAbsolutePathWithoutRoot("c:/work/hello.txt", "c:/work"), "work/hello.txt");
	gotest::assert::Equal(t, getNormalizedAbsolutePathWithoutRoot("c:/work/hello.txt", "d:/worspaces"), "work/hello.txt");
}

static void TestGetRelativePathToDirectoryOrUrl(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("/", "/", false, ComparePathsOptions{}), "");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("/a", "/a", false, ComparePathsOptions{}), "");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("/a/", "/a", false, ComparePathsOptions{}), "");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("/a", "/", false, ComparePathsOptions{}), "..");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("/a", "/b", false, ComparePathsOptions{}), "../b");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("/a/b", "/b", false, ComparePathsOptions{}), "../../b");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("/a/b/c", "/b", false, ComparePathsOptions{}), "../../../b");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("/a/b/c", "/b/c", false, ComparePathsOptions{}), "../../../b/c");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("/a/b/c", "/a/b", false, ComparePathsOptions{}), "..");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("c:", "d:", false, ComparePathsOptions{}), "d:/");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("file:///", "file:///", false, ComparePathsOptions{}), "");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("file:///a", "file:///a", false, ComparePathsOptions{}), "");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("file:///a/", "file:///a", false, ComparePathsOptions{}), "");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("file:///a", "file:///", false, ComparePathsOptions{}), "..");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("file:///a", "file:///b", false, ComparePathsOptions{}), "../b");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("file:///a/b", "file:///b", false, ComparePathsOptions{}), "../../b");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("file:///a/b/c", "file:///b", false, ComparePathsOptions{}), "../../../b");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("file:///a/b/c", "file:///b/c", false, ComparePathsOptions{}), "../../../b/c");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("file:///a/b/c", "file:///a/b", false, ComparePathsOptions{}), "..");
	gotest::assert::Equal(t, getRelativePathToDirectoryOrUrl("file:///c:", "file:///d:", false, ComparePathsOptions{}), "file:///d:/");
}

static void TestToFileNameLowerCase(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, toFileNameLowerCase("/user/UserName/projects/Project/file.ts"), "/user/username/projects/project/file.ts");
	gotest::assert::Equal(t, toFileNameLowerCase("/user/UserName/projects/projectß/file.ts"), "/user/username/projects/projectß/file.ts");
	gotest::assert::Equal(t, toFileNameLowerCase("/user/UserName/projects/İproject/file.ts"), "/user/username/projects/İproject/file.ts");
	gotest::assert::Equal(t, toFileNameLowerCase("/user/UserName/projects/ı/file.ts"), "/user/username/projects/ı/file.ts");
}

static void TestToPath(T* t) {
	t->Parallel();
	gotest::assert::Equal(t, std::string(toPath("file.ext", "path/to", false)), "path/to/file.ext");
	gotest::assert::Equal(t, std::string(toPath("file.ext", "/path/to", true)), "/path/to/file.ext");
	gotest::assert::Equal(t, std::string(toPath("/path/to/../file.ext", "path/to", true)), "/path/file.ext");
	gotest::assert::Equal(t, std::string(toPath("^/~ts-uri~/custom/ts-nul-authority/CaseSensitive.ts", "/", false)), "^/~ts-uri~/custom/ts-nul-authority/CaseSensitive.ts");
}

static void TestTrimFilePathPrefix(T* t) {
	t->Parallel();

	t->Run("case-sensitive exact match", [](T* t) {
		t->Parallel();
		auto [suffix, ok] = trimFilePathPrefix("/project/src/file.ts", "/project/src", true /*useCaseSensitiveFileNames*/);
		gotest::assert::Assert(t, ok, "ok");
		gotest::assert::Equal(t, suffix, "/file.ts");
	});

	t->Run("case-sensitive mismatch", [](T* t) {
		t->Parallel();
		auto [suffix, ok] = trimFilePathPrefix("/project/SRC/file.ts", "/project/src", true /*useCaseSensitiveFileNames*/);
		gotest::assert::Assert(t, !ok, "!ok");
		gotest::assert::Equal(t, suffix, "/project/SRC/file.ts");
	});

	t->Run("case-insensitive match", [](T* t) {
		t->Parallel();
		auto [suffix, ok] = trimFilePathPrefix("/project/SRC/file.ts", "/project/src", false /*useCaseSensitiveFileNames*/);
		gotest::assert::Assert(t, ok, "ok");
		gotest::assert::Equal(t, suffix, "/file.ts");
	});

	t->Run("no match", [](T* t) {
		t->Parallel();
		auto [suffix, ok] = trimFilePathPrefix("/other/file.ts", "/project/src", false /*useCaseSensitiveFileNames*/);
		gotest::assert::Assert(t, !ok, "!ok");
		gotest::assert::Equal(t, suffix, "/other/file.ts");
	});

	t->Run("case-folding shrinks prefix byte length without changing rune count", [](T* t) {
		t->Parallel();
		// Each Kelvin sign U+212A case-folds to the single-byte 'k', so the raw
		// (non-canonicalized) prefix is longer, in bytes, than the path it's a
		// case-insensitive prefix of. trimFilePathPrefix must clamp per-rune.
		auto [suffix, ok] = trimFilePathPrefix("/kkk/a.ts", "/\u212A\u212A\u212A", false /*useCaseSensitiveFileNames*/);
		gotest::assert::Assert(t, ok, "ok");
		gotest::assert::Equal(t, suffix, "/a.ts");
	});

	t->Run("path equal to prefix", [](T* t) {
		t->Parallel();
		auto [suffix, ok] = trimFilePathPrefix("/project/src", "/project/src", true /*useCaseSensitiveFileNames*/);
		gotest::assert::Assert(t, ok, "ok");
		gotest::assert::Equal(t, suffix, "");
	});
}

// shortenName — path_test.go:744.
static std::string shortenName(std::string_view name) {
	if (name.size() > 20) {
		return std::string(name.substr(0, 20)) + "...etc";
	}
	return std::string(name);
}

static void TestPathIsRelative(T* t) {
	t->Parallel();
	struct testCase {
		std::string p;
		bool isRelative;
	};
	// Go: strings.Repeat("foo/", 100)
	std::string foo100;
	for (int i = 0; i < 100; i++) foo100 += "foo/";
	std::vector<testCase> tests = {
	    // relative
	    {".", true},
	    {"..", true},
	    {"./", true},
	    {"../", true},
	    {"./foo/bar", true},
	    {"../foo/bar", true},
	    {"../" + foo100, true},
	    // non-relative
	    {"", false},
	    {"foo", false},
	    {"foo/bar", false},
	    {"/foo/bar", false},
	    {"c:/foo/bar", false},
	};
	// init(): duplicate each case with forward slashes replaced by backslashes.
	size_t n = tests.size();
	for (size_t i = 0; i < n; i++) {
		testCase tc = tests[i];
		std::replace(tc.p.begin(), tc.p.end(), '/', '\\');
		tests.push_back(tc);
	}
	for (auto& tt : tests) {
		t->Run(shortenName(tt.p), [&tt](T* t) {
			t->Parallel();
			gotest::assert::Equal(t, pathIsRelative(tt.p), tt.isRelative);
		});
	}
}

static void TestGetCommonParents(T* t) {
	t->Parallel();

	ComparePathsOptions opts{};

	t->Run("empty input", [&](T* t) {
		t->Parallel();
		std::vector<std::string> paths;
		auto [got, ignored] = getCommonParents(paths, 1, resolvePathComponents, opts);
		gotest::assert::Equal(t, (int)ignored.size(), 0);
		gotest::assert::Equal(t, got.empty(), true);
	});

	t->Run("single path returns itself", [&](T* t) {
		t->Parallel();
		std::vector<std::string> paths = {"/a/b/c/d"};
		auto [got, ignored] = getCommonParents(paths, 1, resolvePathComponents, opts);
		gotest::assert::Equal(t, (int)ignored.size(), 0);
		gotest::assert::Equal(t, got, std::vector<std::string>{paths[0]});
	});

	t->Run("paths shorter than minComponents are ignored", [&](T* t) {
		t->Parallel();
		std::vector<std::string> paths = {"/a/b/c/d", "/a/b/c/e", "/a/b/f/g", "/x/y"};
		auto [got, ignored] = getCommonParents(paths, 4, resolvePathComponents, opts);
		gotest::assert::Equal(t, ignored, std::unordered_set<std::string>{"/x/y"});
		gotest::assert::Equal(t, got, std::vector<std::string>({"/a/b/c", "/a/b/f/g"}));
	});

	t->Run("three paths share /a/b", [&](T* t) {
		t->Parallel();
		std::vector<std::string> paths = {"/a/b/c/d", "/a/b/c/e", "/a/b/f/g"};
		auto [got, ignored] = getCommonParents(paths, 1, resolvePathComponents, opts);
		gotest::assert::Equal(t, (int)ignored.size(), 0);
		gotest::assert::Equal(t, got, std::vector<std::string>({"/a/b"}));
	});

	t->Run("mixed with short path collapses to root when minComponents=1", [&](T* t) {
		t->Parallel();
		std::vector<std::string> paths = {"/a/b/c/d", "/a/b/c/e", "/a/b/f/g", "/x/y/z"};
		auto [got, ignored] = getCommonParents(paths, 1, resolvePathComponents, opts);
		gotest::assert::Equal(t, (int)ignored.size(), 0);
		gotest::assert::Equal(t, got, std::vector<std::string>({"/"}));
	});

	t->Run("mixed with short path preserves both when minComponents=3", [&](T* t) {
		t->Parallel();
		std::vector<std::string> paths = {"/a/b/c/d", "/a/b/c/e", "/a/b/f/g", "/x/y/z"};
		auto [got, ignored] = getCommonParents(paths, 3, resolvePathComponents, opts);
		gotest::assert::Equal(t, (int)ignored.size(), 0);
		gotest::assert::Equal(t, got, std::vector<std::string>({"/a/b", "/x/y/z"}));
	});

	t->Run("different volumes are returned individually", [&](T* t) {
		t->Parallel();
		std::vector<std::string> paths = {"c:/a/b/c/d", "d:/a/b/c/d"};
		auto [got, ignored] = getCommonParents(paths, 1, resolvePathComponents, opts);
		gotest::assert::Equal(t, (int)ignored.size(), 0);
		gotest::assert::Equal(t, got, std::vector<std::string>({paths[0], paths[1]}));
	});

	t->Run("duplicate paths deduplicate result", [&](T* t) {
		t->Parallel();
		std::vector<std::string> paths = {"/a/b/c/d", "/a/b/c/d"};
		auto [got, ignored] = getCommonParents(paths, 1, resolvePathComponents, opts);
		gotest::assert::Equal(t, (int)ignored.size(), 0);
		gotest::assert::Equal(t, got, std::vector<std::string>({paths[0]}));
	});

	t->Run("paths with few components are returned as-is when minComponents met", [&](T* t) {
		t->Parallel();
		std::vector<std::string> paths = {"/a/b/c/d", "/x/y"};
		auto [got, ignored] = getCommonParents(paths, 2, resolvePathComponents, opts);
		gotest::assert::Equal(t, (int)ignored.size(), 0);
		gotest::assert::Equal(t, got, std::vector<std::string>({"/a/b/c/d", "/x/y"}));
	});

	t->Run("minComponents=2", [&](T* t) {
		t->Parallel();
		std::vector<std::string> paths = {"/a/b/c/d", "/a/z/c/e", "/a/aaa/f/g", "/x/y/z"};
		auto [got, ignored] = getCommonParents(paths, 2, resolvePathComponents, opts);
		gotest::assert::Equal(t, (int)ignored.size(), 0);
		gotest::assert::Equal(t, got, std::vector<std::string>({"/a", "/x/y/z"}));
	});

	t->Run("trailing separators are handled", [&](T* t) {
		t->Parallel();
		std::vector<std::string> paths = {"/a/b/", "/a/b/c"};
		auto [got, ignored] = getCommonParents(paths, 1, resolvePathComponents, opts);
		gotest::assert::Equal(t, (int)ignored.size(), 0);
		gotest::assert::Equal(t, got, std::vector<std::string>({"/a/b"}));
	});

	t->Run("nested fan-out keeps every result", [&](T* t) {
		t->Parallel();
		std::vector<std::string> paths = {"/a/x/1/p", "/a/x/2/q", "/a/y/3/r"};
		auto [got, ignored] = getCommonParents(paths, 4, resolvePathComponents, opts);
		gotest::assert::Equal(t, (int)ignored.size(), 0);
		gotest::assert::Equal(t, got, std::vector<std::string>({"/a/x/1/p", "/a/x/2/q", "/a/y/3/r"}));
	});
}

REGISTER_UNIT_TEST("tspath.TestNormalizeSlashes", TestNormalizeSlashes);
REGISTER_UNIT_TEST("tspath.TestGetRootLength", TestGetRootLength);
REGISTER_UNIT_TEST("tspath.TestPathIsAbsolute", TestPathIsAbsolute);
REGISTER_UNIT_TEST("tspath.TestIsUrl", TestIsUrl);
REGISTER_UNIT_TEST("tspath.TestIsRootedDiskPath", TestIsRootedDiskPath);
REGISTER_UNIT_TEST("tspath.TestGetDirectoryPath", TestGetDirectoryPath);
REGISTER_UNIT_TEST("tspath.TestGetLongestExtensionFromPath", TestGetLongestExtensionFromPath);
REGISTER_UNIT_TEST("tspath.TestRemoveAnyFileExtension", TestRemoveAnyFileExtension);
REGISTER_UNIT_TEST("tspath.TestGetPathComponents", TestGetPathComponents);
REGISTER_UNIT_TEST("tspath.TestReducePathComponents", TestReducePathComponents);
REGISTER_UNIT_TEST("tspath.TestCombinePaths", TestCombinePaths);
REGISTER_UNIT_TEST("tspath.TestResolvePath", TestResolvePath);
REGISTER_UNIT_TEST("tspath.TestGetNormalizedAbsolutePath", TestGetNormalizedAbsolutePath);
REGISTER_UNIT_TEST("tspath.TestGetNormalizedAbsolutePathWithoutRoot", TestGetNormalizedAbsolutePathWithoutRoot);
REGISTER_UNIT_TEST("tspath.TestGetRelativePathToDirectoryOrUrl", TestGetRelativePathToDirectoryOrUrl);
REGISTER_UNIT_TEST("tspath.TestToFileNameLowerCase", TestToFileNameLowerCase);
REGISTER_UNIT_TEST("tspath.TestTrimFilePathPrefix", TestTrimFilePathPrefix);
REGISTER_UNIT_TEST("tspath.TestToPath", TestToPath);
REGISTER_UNIT_TEST("tspath.TestPathIsRelative", TestPathIsRelative);
REGISTER_UNIT_TEST("tspath.TestGetCommonParents", TestGetCommonParents);
