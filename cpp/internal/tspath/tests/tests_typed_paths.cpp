// Port of tsc/internal/tspath/typed_paths_test.go (package tspath).
#include <string>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/typed_paths.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
using namespace tsc;
using namespace tsc::tspath;

namespace {

// CaseSensitivity instances mirroring the Go package-level vars.
const CaseSensitivity CaseSensitive_ = CaseSensitivity::CaseSensitive();
const CaseSensitivity CaseInsensitive_ = CaseSensitivity::CaseInsensitive();

void TestToRootedFilePath(T* t) {
	t->Parallel();
	gotest::assert::Equal(
	    t, toRootedFilePath("./src/../src/a.ts", "/project").AsString(),
	    "/project/src/a.ts");
	gotest::assert::Equal(
	    t, toRootedFilePath("/project/src/", "/ignored").AsString(),
	    "/project/src");
	gotest::assert::Equal(
	    t, toRootedFilePath("/", "/ignored").AsString(), "/");
	gotest::assert::Equal(
	    t, toRootedFilePath("file:///project/src/a.ts", "/ignored").AsString(),
	    "file:///project/src/a.ts");
	gotest::assert::Equal(
	    t,
	    toRootedFilePath("^/untitled/ts-nul-authority/Untitled-1", "/ignored")
	        .AsString(),
	    "^/untitled/ts-nul-authority/Untitled-1");
	struct MapCase {
		std::string_view input, expected;
	};
	for (auto [input, expected] : std::vector<MapCase>{
	         {"c:", "c:/"},
	         {"//server", "//server/"},
	         {"file://server", "file://server/"},
	         {"^/~ts-uri~/custom/ts-nul-authority",
	          "^/~ts-uri~/custom/ts-nul-authority/"},
	         {"^/~ts-uri~/custom/authority?query",
	          "^/~ts-uri~/custom/authority?query/"},
	     }) {
		gotest::assert::Equal(
		    t, toRootedPath(input, "/ignored").AsString(), expected);
		gotest::assert::Equal(
		    t, toRootedFilePath(input, "/ignored").AsString(), expected);
		gotest::assert::Equal(
		    t, toRootedDirectoryPath(input, "/ignored").AsString(), expected);
	}
	auto diskWithSchemeText = toRootedPath("/a://b?x/../y", "/ignored");
	gotest::assert::Equal(t, diskWithSchemeText.AsString(), "/a:/y");
	auto [_r0, ok0] = tryRootedPathFromNormalized(diskWithSchemeText.AsString());
	gotest::assert::Assert(t, ok0);
	for (auto input : {"http://server?query#fragment", "http://server?x/../y",
	                  "file:///c:?query/path"}) {
		testutil::AssertPanics(
		    t, [&] { toRootedPath(input, "/ignored"); },
		    std::string("path must not contain a URL query or fragment"));
		auto [_r1, ok1] = tryRootedPathFromAbsolute(input);
		gotest::assert::Assert(t, !ok1);
		auto [_r2, ok2] = tryRootedPathFromNormalized(input);
		gotest::assert::Assert(t, !ok2);
	}
	auto urlDirectory =
	    RootedDirectoryPath(rootedPathFromNormalized("http://server/base"));
	// panics: URL suffixes may not be mutated like ordinary paths
	auto urlPathPanic = [&](const auto& f) {
		testutil::AssertPanics(
		    t, f,
		    std::string(
		        "relative URL path must not contain a query or fragment"));
	};
	urlPathPanic([&] { urlDirectory.ResolveFile("file.ts?query/.."); });
	urlPathPanic([&] { urlDirectory.ResolveFile("file.ts?query"); });
	urlPathPanic(
	    [&] { urlDirectory.ResolveRelativeFile(RelativePath("file.ts?query")); });
	urlPathPanic(
	    [&] { urlDirectory.ResolveFileFromNormalizedRelative("file.ts?query"); });
	testutil::AssertPanics(
	    t,
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized(
		        "http://server/file.ts")).AppendSuffix("?query");
	    },
	    std::string(
	        "path must be rooted and normalized: "
	        "http://server/file.ts?query"));
	testutil::AssertPanics(
	    t,
	    [&] {
		    PathKey("http://server/file.ts").AppendCanonicalSuffix("#fragment");
	    },
	    std::string("path must be normalized"));
	testutil::AssertPanics(
	    t,
	    [&] {
		    PathKey("http://server/base").AppendCanonicalComponent(
		        "file.ts?query");
	    },
	    std::string("path must be normalized"));
	testutil::AssertPanics(
	    t,
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized(
		        "http://server/file.ts")).ChangeExtension(".js?query");
	    },
	    std::string("path must not contain a URL query or fragment"));
	testutil::AssertPanics(
	    t,
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized(
		        "http://server/file.ts")).ChangeFullExtension(".js#fragment");
	    },
	    std::string("path must not contain a URL query or fragment"));
	testutil::AssertPanics(
	    t,
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized(
		        "http://server/file.ts"))
		        .ChangeAnyExtension(".js?query", {".ts"}, CaseSensitive_);
	    },
	    std::string("path must not contain a URL query or fragment"));
	testutil::AssertPanics(
	    t, [&] { toRootedPath("file.ts?query/..", urlDirectory); },
	    std::string(
	        "relative URL path must not contain a query or fragment"));
	gotest::assert::Equal(
	    t, std::string(urlDirectory.ResolveFile("/disk/file?name.ts")),
	    std::string("/disk/file?name.ts"));
	gotest::assert::Equal(
	    t, std::string(
	           urlDirectory.ResolveDirectory("^/~ts-uri~/custom/authority?query/")),
	    std::string("^/~ts-uri~/custom/authority?query/"));
}

void TestEncodedDynamicPathsPreserveOpaqueIdentity(T* t) {
	t->Parallel();
	const std::string root = "^/~ts-uri~/custom/authority";
	RootedPath rootWithSeparator(root + "/");
	auto upper = rootedFilePathFromNormalized(root + "/Foo.ts");
	auto lower = rootedFilePathFromNormalized(root + "/foo.ts");

	gotest::assert::Equal(t, getRootLength(root), root.size());
	gotest::assert::Equal(
	    t, std::string(CaseInsensitive_.pathKey(RootedPath(root))),
	    std::string(CaseInsensitive_.pathKey(rootWithSeparator)));
	gotest::assert::Assert(
	    t, CaseInsensitive_.compareFilePaths(upper, lower) != 0);
	gotest::assert::Assert(
	    t,
	    !CaseInsensitive_.containsFilePath(
	        RootedDirectoryPath(root + "/Folder"),
	        RootedFilePath(root + "/folder/file.ts")));
	auto [relative, ok] = CaseInsensitive_.relativeFilePathFromDirectory(
	    RootedDirectoryPath(root), upper);
	gotest::assert::Assert(t, ok);
	gotest::assert::Equal(t, std::string(relative), "Foo.ts");
	auto [_rp, ok2] = CaseInsensitive_.relativePathFromPath(
	    RootedDirectoryPath("^/~ts-uri~/custom/Authority/src"),
	    RootedPath("^/~ts-uri~/custom/authority/lib/x.ts"));
	gotest::assert::Assert(t, !ok2);
	gotest::assert::Assert(
	    t,
	    !PathKey("^/~ts-uri~/custom/Authority/src")
	         .ContainsPath(
	             PathKey("^/~ts-uri~/custom/authority/src/file.ts")));
}

void TestPathKeyFromCanonicalRejectsURLSuffix(T* t) {
	t->Parallel();
	const std::string path = "http://server/?x/../y";
	auto [_k, ok] = tryPathKeyFromCanonical(path);
	gotest::assert::Assert(t, !ok);
}

void TestDynamicPathKeyCaseInsensitiveKeyPreservesIdentity(T* t) {
	t->Parallel();
	auto upper = CaseInsensitive_.pathKey(rootedPathFromNormalized(
	    "^/~ts-uri~/custom/ts-nul-authority/Foo.ts"));
	auto lower = CaseInsensitive_.pathKey(rootedPathFromNormalized(
	    "^/~ts-uri~/custom/ts-nul-authority/foo.ts"));
	gotest::assert::Equal(t, std::string(upper.CaseInsensitiveKey()),
	                      std::string(upper));
	gotest::assert::Equal(t, std::string(lower.CaseInsensitiveKey()),
	                      std::string(lower));
	gotest::assert::Assert(
	    t, upper.CaseInsensitiveKey() != lower.CaseInsensitiveKey());
}

void TestExtensionMutationsPreserveNormalizedInvariant(T* t) {
	t->Parallel();
	auto invariantPanic = [&](const auto& f) {
		testutil::AssertPanics(
		    t, f,
		    std::string(
		        "file extension change must preserve path normalization"));
	};
	testutil::AssertPanics(
	    t,
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized("/project/.ts"))
		        .RemoveFileExtension()
		        .AppendSuffix("");
	    },
	    std::string("path must be rooted and normalized: /project/"));
	testutil::AssertPanics(
	    t,
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized("/project/.ts"))
		        .RemoveExtension(".ts")
		        .AppendSuffix("");
	    },
	    std::string("path must be rooted and normalized: /project/"));
	invariantPanic(
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized("/project/.ts"))
		        .ChangeExtension("");
	    });
	invariantPanic(
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized("/project/.d.ts"))
		        .ChangeFullExtension("");
	    });
	invariantPanic(
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized("/project/.ts"))
		        .ChangeAnyExtension("", {".ts"}, CaseSensitive_);
	    });
	testutil::AssertPanics(
	    t,
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized("/project/..ts"))
		        .RemoveFileExtension()
		        .AppendSuffix("");
	    },
	    std::string("path must be rooted and normalized: /project/."));
	testutil::AssertPanics(
	    t,
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized("/project/..ts"))
		        .RemoveExtension(".ts")
		        .AppendSuffix("");
	    },
	    std::string("path must be rooted and normalized: /project/."));
	invariantPanic(
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized("/project/..ts"))
		        .ChangeExtension("");
	    });
	invariantPanic(
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized("/project/...ts"))
		        .ChangeFullExtension("");
	    });
	invariantPanic(
	    [&] {
		    RootedFilePath(rootedFilePathFromNormalized("/project/..ts"))
		        .ChangeAnyExtension("", {".ts"}, CaseSensitive_);
	    });
	testutil::AssertPanics(
	    t,
	    [&] {
		    RootedFilePath(
		        rootedFilePathFromNormalized("http://example.com/file.ts"))
		        .ChangeAnyExtension("", {".com/file.ts"}, CaseSensitive_);
	    },
	    std::string(
	        "file extension must not contain a directory separator"));

	struct SplitCase {
		RootedFilePath fileName;
		std::string_view component;
	};
	for (auto& test : std::vector<SplitCase>{
	         {"//node_modules/pkg/index.d.ts", "node_modules"},
	         {"http://node_modules/pkg/index.d.ts", "node_modules"},
	         {"file:///c:/pkg/index.d.ts", "c:"},
	         {"^/~ts-uri~/https/node_modules/file.ts", "node_modules"},
	     }) {
		auto [_a, _b, ok] = test.fileName.SplitAtComponent(test.component);
		gotest::assert::Assert(t, !ok);
		auto [_c, _d, ok2] = test.fileName.SplitAtLastComponent(test.component);
		gotest::assert::Assert(t, !ok2);
		auto [_e, _f, ok3] =
		    CaseSensitive_.pathKey(test.fileName.AsPath())
		        .SplitAtCanonicalComponent(test.component);
		gotest::assert::Assert(t, !ok3);
	}
	auto [_g, _h, ok4] =
	    PathKey("").SplitAtCanonicalComponent("node_modules");
	gotest::assert::Assert(t, !ok4);
	auto [_i, _j, ok5] =
	    RootedFilePath("").SplitAtComponent("node_modules");
	gotest::assert::Assert(t, !ok5);
}

void TestFileNameStemsPreserveFilenamePrefixes(T* t) {
	t->Parallel();
	struct StemCase {
		RootedFilePath fileName;
		FileNameStem stem;
		RootedFilePath output;
	};
	for (auto& test : std::vector<StemCase>{
	         {"", "", ""},
	         {"/project/file.ts", "/project/file", "/project/file.js"},
	         {"/project/.ts", "/project/", "/project/.js"},
	         {"/project/..ts", "/project/.", "/project/..js"},
	         {"/project/...ts", "/project/..", "/project/...js"},
	         {"/project/.d.ts", "/project/", "/project/.js"},
	         {"/.ts", "/", "/.js"},
	         {"c:/.ts", "c:/", "c:/.js"},
	         {"//server/.ts", "//server/", "//server/.js"},
	         {"file:///.ts", "file:///", "file:///.js"},
	         {"^/~ts-uri~/custom/ts-nul-authority/.ts",
	          "^/~ts-uri~/custom/ts-nul-authority/",
	          "^/~ts-uri~/custom/ts-nul-authority/.js"},
	     }) {
		t->Run(test.fileName.AsString(), [&](T* st) {
			gotest::assert::Equal(st, test.fileName.AsStem().AsString(),
			                      test.fileName.AsString());
			gotest::assert::Equal(
			    st, std::string(test.fileName.AsStem().AppendSuffix("")),
			    std::string(test.fileName));
			FileNameStem stem = test.fileName.RemoveFileExtension();
			gotest::assert::Equal(st, std::string(stem),
			                      std::string(test.stem));
			if (test.fileName.empty()) {
				gotest::assert::Equal(
				    st, std::string(test.stem.AppendSuffix("")),
				    std::string(test.output));
				testutil::AssertPanics(
				    st, [&] { test.stem.AppendSuffix(".js"); },
				    std::string(
				        "cannot append a suffix to an empty file name"));
			} else {
				auto output = test.stem.AppendSuffix(".js");
				gotest::assert::Equal(st, std::string(output),
				                      std::string(test.output));
				gotest::assert::Equal(
				    st,
				    std::string(
				        rootedFilePathFromNormalized(output.AsString())),
				    std::string(output));
				gotest::assert::Equal(
				    st,
				    std::string(test.fileName.RemoveExtension(".ts")
				                    .AppendSuffix(".ts")),
				    std::string(test.fileName));
			}
		});
	}
	for (auto& pr : std::vector<std::pair<const char*, const char*>>{
	         {"/project/.ts", "/project/"},
	         {"/project/..ts", "/project/."},
	         {"/project/...ts", "/project/.."},
	     }) {
		auto fileName = pr.first;
		auto stemResult = pr.second;
		testutil::AssertPanics(
		    t,
		    [&] {
			    RootedFilePath(fileName)
			        .RemoveFileExtension()
			        .AppendSuffix("");
		    },
		    std::string(
		        "path must be rooted and normalized: ") +
		        std::string(stemResult));
	}
	FileNameStem stem =
	    RootedFilePath("/project/file.ts").RemoveFileExtension();
	testutil::AssertPanics(
	    t, [&] { stem.AppendSuffix("/other.js"); },
	    std::string(
	        "file name suffix must not contain a directory separator"));
	testutil::AssertPanics(
	    t, [&] { stem.AppendSuffix("\\other.js"); },
	    std::string(
	        "file name suffix must not contain a directory separator"));
	testutil::AssertPanics(
	    t,
	    [&] {
		    RootedFilePath("http://example.com/.ts")
		        .RemoveFileExtension()
		        .AppendSuffix("?query");
	    },
	    std::string(
	        "path must be rooted and normalized: "
	        "http://example.com/?query"));
	testutil::AssertPanics(
	    t,
	    [&] { RootedFilePath("/project/file.ts").RemoveExtension(".js"); },
	    std::string("file name does not have extension: .js"));
	testutil::AssertPanics(
	    t,
	    [&] { RootedFilePath("/project/file.ts").RemoveExtension("/file.ts"); },
	    std::string(
	        "file extension must not contain a directory separator"));
}

void TestCompareFileNameStemsKeepsLiteralDotComponentsAndCasingPolicy(T* t) {
	t->Parallel();
	struct CmpCase {
		RootedFilePath a, b;
		bool equalSensitive, equalInsensitive;
	};
	for (auto& test : std::vector<CmpCase>{
	         {"/project/.ts", "/project/.d.ts", true, true},
	         {"/project/..ts", "/project.ts", false, false},
	         {"/project/...ts", "/.ts", false, false},
	         {"c:/project/.ts", "C:/project/.d.ts", true, true},
	         {"/project/File.ts", "/project/file.js", false, true},
	         {"^/~ts-uri~/custom/ts-nul-authority/Foo.ts",
	          "^/~ts-uri~/custom/ts-nul-authority/foo.js", false, false},
	     }) {
		auto a = test.a.RemoveFileExtension();
		auto b = test.b.RemoveFileExtension();
		gotest::assert::Equal(
		    t, CaseSensitive_.compareFileNameStems(a, b) == 0,
		    test.equalSensitive);
		gotest::assert::Equal(
		    t, CaseInsensitive_.compareFileNameStems(a, b) == 0,
		    test.equalInsensitive);
	}
}

void TestToRootedFilePathRequiresRoot(T* t) {
	t->Parallel();
	testutil::AssertPanics(
	    t, [&] { toRootedFilePath("", "/project"); },
	    std::string("path must not be empty"));
	testutil::AssertPanics(
	    t, [&] { toRootedFilePath("src/a.ts", ""); },
	    std::string("path must be rooted"));
}

void TestRootedFilePathFromAbsolute(T* t) {
	t->Parallel();
	gotest::assert::Equal(
	    t,
	    std::string(rootedFilePathFromAbsolute("C:\\project\\src\\..\\a.ts")),
	    std::string(rootedFilePathFromNormalized("C:/project/a.ts")));
	testutil::AssertPanics(
	    t, [&] { rootedFilePathFromAbsolute("src/a.ts"); },
	    std::string("path must be absolute"));
	auto [_a, ok1] = tryRootedFilePathFromAbsolute("src/a.ts");
	gotest::assert::Assert(t, !ok1);
	auto [absolute, ok2] =
	    tryRootedFilePathFromAbsolute("/project/src/../a.ts");
	gotest::assert::Assert(t, ok2);
	gotest::assert::Equal(
	    t, std::string(absolute),
	    std::string(rootedFilePathFromNormalized("/project/a.ts")));
	for (auto [input, expected] : std::vector<
	         std::pair<std::string_view, std::string_view>>{
	         {"c:", "c:/"},
	         {"//server", "//server/"},
	         {"file://server", "file://server/"},
	     }) {
		gotest::assert::Equal(
		    t, rootedFilePathFromAbsolute(input).AsString(), expected);
	}
}

void TestRootedFilePathFromNormalized(T* t) {
	t->Parallel();
	gotest::assert::Equal(
	    t, rootedFilePathFromNormalized("/project/src/a.ts").AsString(),
	    "/project/src/a.ts");
	gotest::assert::Equal(
	    t, rootedFilePathFromNormalized("c:/").AsString(), "c:/");
	gotest::assert::Equal(
	    t, rootedFilePathFromNormalized("//server/").AsString(), "//server/");
	gotest::assert::Equal(
	    t, rootedFilePathFromNormalized("file://server/").AsString(),
	    "file://server/");
	auto invalid = std::string(
	    "path must be rooted and normalized: ");
	testutil::AssertPanics(
	    t, [&] { rootedFilePathFromNormalized("/project/src/../a.ts"); },
	    invalid + "/project/src/../a.ts");
	testutil::AssertPanics(
	    t, [&] { rootedFilePathFromNormalized("src/a.ts"); },
	    invalid + "src/a.ts");
	testutil::AssertPanics(
	    t, [&] { rootedFilePathFromNormalized("/project/src/"); },
	    invalid + "/project/src/");
	testutil::AssertPanics(
	    t, [&] { rootedFilePathFromNormalized("/project\\src\\a.ts"); },
	    invalid + "/project\\src\\a.ts");
	testutil::AssertPanics(
	    t, [&] { rootedFilePathFromNormalized("/project//src/a.ts"); },
	    invalid + "/project//src/a.ts");
	testutil::AssertPanics(
	    t, [&] { rootedFilePathFromNormalized("c:"); }, invalid + "c:");
	testutil::AssertPanics(
	    t, [&] { rootedFilePathFromNormalized("//server"); },
	    invalid + "//server");
	testutil::AssertPanics(
	    t, [&] { rootedFilePathFromNormalized("file://server"); },
	    invalid + "file://server");
	for (auto fileName : {"/project/src/a.ts", "c:/", "//server/",
	                      "file://server/"}) {
		auto [result, ok] = tryRootedFilePathFromNormalized(fileName);
		gotest::assert::Assert(t, ok);
		gotest::assert::Equal(t, result.AsString(),
		                      std::string_view(fileName));
	}
	for (auto fileName : {"", "/project/src/../a.ts", "src/a.ts",
	                      "/project/src/", "/project\\src\\a.ts",
	                      "/project//src/a.ts", "c://project/src/a.ts",
	                      "//server//share/a.ts", "file://server//a.ts", "c:",
	                      "//server", "file://server"}) {
		auto [_r, ok] = tryRootedFilePathFromNormalized(fileName);
		gotest::assert::Assert(t, !ok);
	}
}

void TestTypedPathConstructorsAndDecoders(T* t) {
	t->Parallel();
	gotest::assert::Equal(
	    t, toRootedDirectoryPath("./src", "/project").AsString(),
	    "/project/src");
	gotest::assert::Equal(
	    t, rootedDirectoryPathFromAbsolute("/project/src/../lib/").AsString(),
	    "/project/lib");
	gotest::assert::Equal(
	    t,
	    rootedDirectoryPathFromAbsolute("c:\\project\\src\\..\\lib\\")
	        .AsString(),
	    "c:/project/lib");
	gotest::assert::Equal(
	    t,
	    rootedDirectoryPathFromAbsolute("file:///project/src/../lib/")
	        .AsString(),
	    "file:///project/lib");
	gotest::assert::Equal(
	    t, rootedDirectoryPathFromNormalized("/project/src").AsString(),
	    "/project/src");
	gotest::assert::Equal(
	    t, std::string(pathKeyFromCanonical("/project/src")), "/project/src");
	gotest::assert::Equal(
	    t,
	    std::string(CaseSensitive_.pathKey(
	        rootedPathFromAbsolute("/project/src/"))),
	    std::string(PathKey("/project/src")));
	auto [path, ok] = tryPathKeyFromCanonical("/project/src");
	gotest::assert::Assert(t, ok);
	gotest::assert::Equal(t, std::string(path), "/project/src");
	gotest::assert::Equal(t, toModuleSpecifier("./src").AsString(), "./src");
	testutil::AssertPanics(
	    t, [&] { rootedDirectoryPathFromAbsolute("project/src"); },
	    std::string("path must be absolute"));
	testutil::AssertPanics(
	    t, [&] { rootedDirectoryPathFromNormalized("/project/src/"); },
	    std::string("path must be rooted and normalized: /project/src/"));
	testutil::AssertPanics(
	    t, [&] { pathKeyFromCanonical("/project/../src"); },
	    std::string("path must be normalized"));
	testutil::AssertPanics(
	    t, [&] { pathKeyFromCanonical("project/src"); },
	    std::string("path must be normalized"));
	for (auto value : {"/project/../src", "project/src", "c:", "//server",
	                   "http://server", "file:///c:"}) {
		auto [_r, ok2] = tryPathKeyFromCanonical(value);
		gotest::assert::Assert(t, !ok2);
	}
	testutil::AssertPanics(
	    t,
	    [&] { CaseSensitive_.pathKey(toRootedPath("project/src", "")); },
	    std::string("path must be rooted"));
}

void TestRelativePath(T* t) {
	t->Parallel();
	gotest::assert::Equal(
	    t, toRelativePath(".\\src\\..\\lib\\file.ts").AsString(),
	    "lib/file.ts");
	gotest::assert::Equal(
	    t, relativePathFromNormalized("../lib/file.ts").AsString(),
	    "../lib/file.ts");
	gotest::assert::Equal(t, relativePathFromNormalized("").AsString(), "");
	testutil::AssertPanics(
	    t, [&] { relativePathFromNormalized("./lib/file.ts"); },
	    std::string(
	        "relative path must be relative and normalized: ./lib/file.ts"));
	testutil::AssertPanics(
	    t, [&] { toRelativePath("/lib/file.ts"); },
	    std::string("relative path must not be rooted"));
	gotest::assert::Equal(
	    t,
	    std::string(relativePathFromNormalized("lib/file.ts")
	                    .AsModuleSpecifier()
	                    .AsString()),
	    "./lib/file.ts");
	gotest::assert::Equal(
	    t,
	    std::string(relativePathFromNormalized("../lib/file.ts")
	                    .AsModuleSpecifier()
	                    .AsString()),
	    "../lib/file.ts");
	gotest::assert::Equal(
	    t,
	    std::string(
	        RootedFilePath(rootedFilePathFromNormalized(
	            "/project/lib/file.ts")).AsModuleSpecifier()),
	    std::string(ModuleSpecifier("/project/lib/file.ts")));
	gotest::assert::Assert(t, toModuleSpecifier("./lib/file.ts").IsRelative());
	gotest::assert::Assert(t, !toModuleSpecifier("lib").IsRelative());
	gotest::assert::Assert(t, toModuleSpecifier("/project/lib").IsAbsolute());
	gotest::assert::Assert(t, !toModuleSpecifier("lib").IsAbsolute());
	gotest::assert::Equal(
	    t,
	    std::string(
	        toModuleSpecifier("pkg").Resolve({"./dist", "file.js"})),
	    std::string(ModuleSpecifier("pkg/dist/file.js")));
	gotest::assert::Equal(
	    t,
	    std::string(toModuleSpecifier("pkg").ResolveRelative(
	        relativePathFromNormalized("lib/file.js"))),
	    std::string(ModuleSpecifier("pkg/lib/file.js")));
	gotest::assert::Equal(
	    t,
	    std::string(toModuleSpecifier("pkg").CombineRelative(
	        relativePathFromNormalized("lib/file.js"))),
	    std::string(ModuleSpecifier("pkg/lib/file.js")));
	gotest::assert::Equal(
	    t,
	    std::string(
	        toModuleSpecifier("pkg/lib/file.d.ts").RemoveFileExtension()),
	    std::string(ModuleSpecifier("pkg/lib/file")));
	gotest::assert::Assert(
	    t, relativePathFromNormalized("../lib/file.ts").IsParentRelative());
	gotest::assert::Assert(
	    t, !relativePathFromNormalized("..file.ts").IsParentRelative());
	gotest::assert::Assert(
	    t, relativePathFromNormalized("lib/").HasTrailingDirectorySeparator_());
	gotest::assert::Equal(
	    t,
	    std::string(relativePathFromNormalized("lib")
	                    .WithTrailingDirectorySeparator()),
	    std::string(RelativePath("lib/")));
	gotest::assert::Equal(
	    t,
	    std::string(
	        RootedDirectoryPath(
	            rootedDirectoryPathFromNormalized("/project/dist"))
	            .ResolveRelativeFile(
	                relativePathFromNormalized("../src/file.ts"))),
	    std::string(rootedFilePathFromNormalized("/project/src/file.ts")));
}

void TestTypedPathComponents(T* t) {
	t->Parallel();
	auto fileName =
	    rootedFilePathFromNormalized("/project/node_modules/pkg/index.ts");
	gotest::assert::Assert(
	    t, fileName.ContainsLowercaseDirectorySequence("/node_modules/"));
	gotest::assert::Assert(
	    t,
	    !RootedFilePath(
	         rootedFilePathFromNormalized("/project/node_modules"))
	         .ContainsLowercaseDirectorySequence("/node_modules/"));
	gotest::assert::Assert(
	    t,
	    !RootedFilePath(rootedFilePathFromNormalized(
	         "/project/not_node_modules/pkg/index.ts"))
	         .ContainsLowercaseDirectorySequence("/node_modules/"));
	auto path = pathKeyFromCanonical(
	    "/project/node_modules/@types/node/index.d.ts");
	gotest::assert::Assert(
	    t, path.ContainsLowercaseDirectorySequence(
	           "/node_modules/@types/node/"));
	gotest::assert::Assert(
	    t,
	    !PathKey(pathKeyFromCanonical("/project/node_modules/@types/node"))
	         .ContainsLowercaseDirectorySequence(
	             "/node_modules/@types/node/"));
}

void TestTryRelativePathBetweenFilePaths(T* t) {
	t->Parallel();
	auto from = rootedDirectoryPathFromNormalized("/project/src");
	auto to = rootedFilePathFromNormalized("/project/lib/file.ts");
	auto [relative, ok] =
	    CaseSensitive_.relativePathFromDirectory(from, to);
	gotest::assert::Assert(t, ok);
	gotest::assert::Equal(t, relative.AsString(), "../lib/file.ts");
	auto [_r, ok2] = CaseSensitive_.relativePathFromDirectory(
	    rootedDirectoryPathFromNormalized("c:/project/src"),
	    rootedFilePathFromNormalized("d:/project/lib/file.ts"));
	gotest::assert::Assert(t, !ok2);
}

void TestRootedFilePathDirectory(T* t) {
	t->Parallel();
	gotest::assert::Equal(
	    t,
	    RootedFilePath(rootedFilePathFromNormalized("/project/src/a.ts"))
	        .Directory()
	        .AsString(),
	    "/project/src");
	gotest::assert::Equal(
	    t, RootedFilePath(rootedFilePathFromNormalized("/")).Directory().AsString(),
	    "/");
	gotest::assert::Equal(
	    t,
	    RootedFilePath(rootedFilePathFromNormalized("c:/project/src/a.ts"))
	        .Directory()
	        .AsString(),
	    "c:/project/src");
	gotest::assert::Equal(
	    t,
	    RootedFilePath(
	        rootedFilePathFromNormalized("file:///project/src/a.ts"))
	        .Directory()
	        .AsString(),
	    "file:///project/src");
}

void TestRootedFilePathWithoutRoot(T* t) {
	t->Parallel();
	gotest::assert::Equal(
	    t,
	    RootedFilePath(rootedFilePathFromNormalized("/project/src/a.ts"))
	        .WithoutRoot(),
	    "project/src/a.ts");
	gotest::assert::Equal(
	    t,
	    RootedFilePath(rootedFilePathFromNormalized("c:/project/src/a.ts"))
	        .WithoutRoot(),
	    "project/src/a.ts");
	gotest::assert::Equal(
	    t,
	    RootedFilePath(
	        rootedFilePathFromNormalized("file:///project/src/a.ts"))
	        .WithoutRoot(),
	    "project/src/a.ts");
}

void TestRootedFilePathRootAndRelativePath(T* t) {
	t->Parallel();
	auto rootAndRelative =
	    RootedFilePath(
	        rootedFilePathFromNormalized("file:///project/src/a.ts"))
	        .RootAndRelativePath();
	RootedDirectoryPath root = rootAndRelative.first;
	std::string relative = rootAndRelative.second;
	gotest::assert::Equal(t, root.AsString(), "file:///");
	gotest::assert::Equal(t, relative, "project/src/a.ts");
	gotest::assert::Equal(
	    t, std::string(root.ResolveFileFromNormalizedRelative(relative)),
	    std::string(rootedFilePathFromNormalized("file:///project/src/a.ts")));
	gotest::assert::Equal(
	    t,
	    std::string(
	        RootedDirectoryPath(rootedDirectoryPathFromNormalized("/"))
	            .ResolveFileFromNormalizedRelative("C:/src/a.ts")),
	    std::string(rootedFilePathFromNormalized("/C:/src/a.ts")));
	testutil::AssertPanics(
	    t, [&] { root.ResolveFileFromNormalizedRelative(""); },
	    std::string("path must not be empty"));
	testutil::AssertPanics(
	    t, [&] { root.ResolveFileFromNormalizedRelative("../a.ts"); },
	    std::string("path must be relative and normalized: ../a.ts"));
}

void TestCommonDirectoryOfFiles(T* t) {
	t->Parallel();
	std::vector<RootedFilePath> fileNames{
	    rootedFilePathFromNormalized("/Project/src/a.ts"),
	    rootedFilePathFromNormalized("/project/src/nested/b.ts"),
	    rootedFilePathFromNormalized("/project/test/c.ts"),
	};
	gotest::assert::Equal(
	    t,
	    std::string(CaseInsensitive_.commonDirectoryOfFiles(fileNames)),
	    std::string(rootedDirectoryPathFromNormalized("/Project")));
	gotest::assert::Equal(
	    t, std::string(CaseSensitive_.commonDirectoryOfFiles(fileNames)),
	    std::string(rootedDirectoryPathFromNormalized("/")));
	std::vector<RootedFilePath> turkish{
	    rootedFilePathFromNormalized("/repo/İproject/a.ts"),
	    rootedFilePathFromNormalized("/repo/iproject/b.ts"),
	};
	gotest::assert::Equal(
	    t, std::string(CaseInsensitive_.commonDirectoryOfFiles(turkish)),
	    std::string(rootedDirectoryPathFromNormalized("/repo")));
}

void TestRelativePathsFromTypedPaths(T* t) {
	t->Parallel();
	auto fromDirectory = rootedDirectoryPathFromNormalized("/project/src");
	auto fromFile = rootedFilePathFromNormalized("/project/src/index.ts");
	auto toFile = rootedFilePathFromNormalized("/project/lib/util.ts");
	auto [relative, ok] =
	    CaseSensitive_.relativePathFromDirectory(fromDirectory, toFile);
	gotest::assert::Assert(t, ok);
	gotest::assert::Equal(t, relative.AsString(), "../lib/util.ts");
	auto [relative2, ok2] =
	    CaseSensitive_.relativePathFromFile(fromFile, toFile);
	gotest::assert::Assert(t, ok2);
	gotest::assert::Equal(t, relative2.AsString(), "../lib/util.ts");
	auto [relative3, ok3] = CaseInsensitive_.relativePathFromDirectory(
	    rootedDirectoryPathFromNormalized("/PROJECT/src"), toFile);
	gotest::assert::Assert(t, ok3);
	gotest::assert::Equal(t, relative3.AsString(), "../lib/util.ts");
	auto [relative4, ok4] = CaseInsensitive_.relativeFilePathFromDirectory(
	    rootedDirectoryPathFromNormalized("/repo/K"),
	    rootedFilePathFromNormalized("/repo/k/a.ts"));
	gotest::assert::Assert(t, ok4);
	gotest::assert::Equal(t, relative4.AsString(), "a.ts");
}

void TestContainsFilePath(T* t) {
	t->Parallel();
	struct ContainCase {
		std::string_view name;
		bool caseSensitive;
		std::string_view directory, fileName, relative;
		bool contained;
	};
	for (auto& test : std::vector<ContainCase>{
	         {"same path", true, "/project/src", "/project/src", "", true},
	         {"child", true, "/project/src", "/project/src/a.ts", "a.ts", true},
	         {"prefix sibling", true, "/project/src", "/project/source/a.ts",
	          "", false},
	         {"root", true, "/", "/project/src/a.ts", "project/src/a.ts", true},
	         {"case sensitive mismatch", true, "/PROJECT", "/project/a.ts", "",
	          false},
	         {"case insensitive", false, "/PROJECT", "/project/a.ts", "a.ts",
	          true},
	         {"case folded rune", false, "/repo/K", "/repo/k/a.ts", "a.ts",
	          true},
	         {"drive root casing", true, "C:/project", "c:/project/a.ts",
	          "a.ts", true},
	         {"file URL scheme and drive casing", true, "FILE:///C:/project",
	          "file:///c:/project/a.ts", "a.ts", true},
	         {"file URL localhost and drive casing", true,
	          "file://LOCALHOST/C:/project",
	          "file://localhost/c:/project/a.ts", "a.ts", true},
	         {"different root", false, "c:/project", "d:/project/a.ts", "",
	          false},
	     }) {
		t->Run(std::string(test.name), [&](T* st) {
			CaseSensitivity cs{(uint8_t)(test.caseSensitive
			                             ? CaseSensitivity::CaseSensitiveV
			                             : CaseSensitivity::CaseInsensitiveV)};
			auto directory =
			    rootedDirectoryPathFromNormalized(test.directory);
			auto fileName =
			    rootedFilePathFromNormalized(test.fileName);
			gotest::assert::Equal(
			    st, cs.containsFilePath(directory, RootedFilePath(fileName)),
			    test.contained);
			gotest::assert::Equal(
			    st,
			    cs.containsPath(directory, RootedFilePath(fileName).AsPath()),
			    test.contained);
			gotest::assert::Equal(
			    st,
			    cs.startsWithDirectory(RootedFilePath(fileName), directory),
			    test.contained && !test.relative.empty());
			auto [relative, ok] = cs.relativeFilePathFromDirectory(
			    directory, RootedFilePath(fileName));
			gotest::assert::Equal(st, ok, test.contained);
			if (ok) {
				gotest::assert::Equal(st, relative.AsString(),
				                      test.relative);
			}
		});
	}
	auto parentKey = CaseSensitive_.pathKey(
	    RootedDirectoryPath(rootedDirectoryPathFromNormalized("C:/project"))
	        .AsPath());
	auto childKey = CaseSensitive_.pathKey(
	    rootedFilePathFromNormalized("c:/project/a.ts").AsPath());
	gotest::assert::Assert(t, parentKey.ContainsPath(childKey));
	auto [relative, ok] =
	    RootedFilePath(rootedFilePathFromNormalized("c:/project/a.ts"))
	        .RelativeTo(rootedDirectoryPathFromNormalized("C:/project"));
	gotest::assert::Assert(t, ok);
	gotest::assert::Equal(t, std::string(relative),
	                      std::string(RelativePath("a.ts")));
}

void TestRootedPath(T* t) {
	t->Parallel();
	auto path = toRootedPath(
	    "src\\config.json",
	    RootedDirectoryPath(rootedDirectoryPathFromNormalized("/project")));
	gotest::assert::Equal(t, path.AsString(), "/project/src/config.json");
	gotest::assert::Equal(
	    t, std::string(rootedFilePathFromPath(path)),
	    std::string(rootedFilePathFromNormalized("/project/src/config.json")));
	gotest::assert::Equal(
	    t, std::string(rootedDirectoryPathFromPath(path)),
	    std::string(
	        rootedDirectoryPathFromNormalized("/project/src/config.json")));
	gotest::assert::Equal(
	    t,
	    std::string(
	        RootedPath(rootedDirectoryPathFromNormalized("/project/src"))),
	    std::string(rootedPathFromNormalized("/project/src")));
}

void TestRootedPathCompare(T* t) {
	t->Parallel();
	gotest::assert::Equal(
	    t,
	    rootedPathFromNormalized("/a").Compare(rootedPathFromNormalized("/b")),
	    -1);
	gotest::assert::Equal(
	    t,
	    rootedFilePathFromNormalized("/a").Compare(
	        rootedFilePathFromNormalized("/a")),
	    0);
	gotest::assert::Equal(
	    t,
	    rootedDirectoryPathFromNormalized("/b").Compare(
	        rootedDirectoryPathFromNormalized("/a")),
	    1);
}

void TestPathOperationsSeparateRooting(T* t) {
	t->Parallel();
	gotest::assert::Equal(
	    t,
	    comparePaths("src/a.ts", "/project/src/a.ts", CaseSensitive_), -1);
	gotest::assert::Equal(
	    t,
	    comparePathsRelativeTo("src/a.ts", "/project/src/a.ts",
	                                          RootedDirectoryPath("/project"),
	                                          CaseSensitive_),
	    0);
	gotest::assert::Assert(
	    t, containsPath("src", "src/a.ts", CaseSensitive_));
	gotest::assert::Assert(
	    t, !containsPath("/project/src", "src/a.ts", CaseSensitive_));
	gotest::assert::Equal(
	    t,
	    getRelativePathFromDirectory("src", "lib/a.ts", CaseSensitive_),
	    "../lib/a.ts");
	gotest::assert::Equal(
	    t,
	    resolveRelativePathFromDirectory("src", "lib/a.ts",
	                                                    RootedDirectoryPath("/project"),
	                                                    CaseSensitive_),
	    "../lib/a.ts");
}

void TestRootedFilePathExtensionOperationsPreserveInvariants(T* t) {
	t->Parallel();
	auto fileName = rootedFilePathFromNormalized("/project/src/file.ts");
	gotest::assert::Equal(
	    t, std::string(fileName.RemoveFileExtension()),
	    std::string(FileNameStem("/project/src/file")));
	gotest::assert::Equal(
	    t, std::string(fileName.ChangeExtension(".js")),
	    std::string(rootedFilePathFromNormalized("/project/src/file.js")));
	gotest::assert::Equal(
	    t,
	    std::string(rootedFilePathFromNormalized("/project/src/file.d.ts")
	                    .ChangeFullExtension("")),
	    std::string(rootedFilePathFromNormalized("/project/src/file")));
	testutil::AssertPanics(
	    t, [&] { fileName.ChangeExtension("../other"); },
	    std::string(
	        "file extension must not contain a directory separator"));
	testutil::AssertPanics(
	    t, [&] { fileName.ChangeFullExtension("\\nested"); },
	    std::string(
	        "file extension must not contain a directory separator"));
	testutil::AssertPanics(
	    t, [&] { fileName.RemoveExtension(".js"); },
	    std::string("file name does not have extension: .js"));
}

void TestForEachAncestorDirectoryPath(T* t) {
	t->Parallel();
	std::vector<RootedDirectoryPath> ancestors;
	auto start = rootedDirectoryPathFromNormalized("/project/src/lib");
	start.ForEachAncestorDirectory<RootedDirectoryPath>(
	    [&](RootedDirectoryPath dir) {
		    ancestors.push_back(dir);
		    return std::pair{dir, false};
	    });
	std::vector<RootedDirectoryPath> expected{
	    rootedDirectoryPathFromNormalized("/project/src/lib"),
	    rootedDirectoryPathFromNormalized("/project/src"),
	    rootedDirectoryPathFromNormalized("/project"),
	    rootedDirectoryPathFromNormalized("/"),
	};
	gotest::assert::Equal(t, ancestors.size(), expected.size());
	for (size_t i = 0; i < ancestors.size(); i++) {
		gotest::assert::Equal(t, std::string(ancestors[i]),
		                      std::string(expected[i]));
	}
}

void TestRootedFilePathComponents(T* t) {
	t->Parallel();
	auto fileName = rootedFilePathFromNormalized(
	    "/store/node_modules/pkg/node_modules/dep/index.d.ts");
	gotest::assert::Equal(
	    t, std::string(fileName.DirectoryBefore(23)),
	    std::string(rootedDirectoryPathFromNormalized(
	        "/store/node_modules/pkg")));
	gotest::assert::Equal(
	    t, std::string(fileName.SuffixAfterSeparator(23)),
	    "node_modules/dep/index.d.ts");
	auto [relative, ok] = fileName.RelativeTo(
	    rootedDirectoryPathFromNormalized("/store/node_modules/pkg"));
	gotest::assert::Assert(t, ok);
	gotest::assert::Equal(t, relative.AsString(),
	                      "node_modules/dep/index.d.ts");
	auto [_r0, ok2] = fileName.RelativeTo(
	    rootedDirectoryPathFromNormalized("/other"));
	gotest::assert::Assert(t, !ok2);
	auto [before, through, ok3] =
	    fileName.SplitAtComponent("node_modules");
	gotest::assert::Assert(t, ok3);
	gotest::assert::Equal(
	    t, std::string(before),
	    std::string(rootedDirectoryPathFromNormalized("/store")));
	gotest::assert::Equal(
	    t, std::string(through),
	    std::string(rootedDirectoryPathFromNormalized(
	        "/store/node_modules")));
	auto [before2, through2, ok4] =
	    fileName.SplitAtLastComponent("node_modules");
	gotest::assert::Assert(t, ok4);
	gotest::assert::Equal(
	    t, std::string(before2),
	    std::string(rootedDirectoryPathFromNormalized(
	        "/store/node_modules/pkg")));
	gotest::assert::Equal(
	    t, std::string(through2),
	    std::string(rootedDirectoryPathFromNormalized(
	        "/store/node_modules/pkg/node_modules")));
	testutil::AssertPanics(
	    t, [&] { fileName.DirectoryBefore(22); },
	    std::string("directory boundary must be at a path separator"));
	testutil::AssertPanics(
	    t, [&] { fileName.SuffixAfterSeparator(22); },
	    std::string("suffix boundary must be at a path separator"));
}

void TestCaseSensitivityKey(T* t) {
	t->Parallel();
	auto fileName = rootedFilePathFromNormalized("/Project/SRC/a.ts");
	gotest::assert::Equal(
	    t,
	    std::string(CaseSensitive_.pathKey(RootedPath(fileName))),
	    "/Project/SRC/a.ts");
	gotest::assert::Equal(
	    t,
	    std::string(CaseInsensitive_.pathKey(RootedPath(fileName))),
	    "/project/src/a.ts");
	gotest::assert::Equal(
	    t,
	    CaseSensitive_.compareFilePaths(
	        fileName,
	        rootedFilePathFromNormalized("/Project/SRC/b.ts")),
	    -1);
	gotest::assert::Equal(
	    t,
	    CaseInsensitive_.compareFilePaths(
	        fileName,
	        rootedFilePathFromNormalized("/project/src/A.ts")),
	    0);
	gotest::assert::Equal(t, fileName.DirectorySeparatorCount(), 3);
}

void TestPathKeyConstructionMethods(T* t) {
	t->Parallel();
	PathKey path("/project/src");
	gotest::assert::Equal(
	    t, std::string(path.AppendCanonicalComponent("node_modules")),
	    "/project/src/node_modules");
	gotest::assert::Equal(
	    t, std::string(path.AppendCanonicalSuffix(".0.ts")),
	    "/project/src.0.ts");
	gotest::assert::Equal(
	    t, std::string(path.AppendCanonicalSuffix(".ts").Extension()), ".ts");
	auto [before, through, ok] =
	    PathKey("/project/node_modules/pkg/index.d.ts")
	        .SplitAtCanonicalComponent("node_modules");
	gotest::assert::Assert(t, ok);
	gotest::assert::Equal(t, std::string(before), "/project");
	gotest::assert::Equal(t, std::string(through), "/project/node_modules");
	auto [_a, _b, ok2] =
	    PathKey("/project/not_node_modules/pkg")
	        .SplitAtCanonicalComponent("node_modules");
	gotest::assert::Assert(t, !ok2);
	testutil::AssertPanics(
	    t,
	    [&] { path.AppendCanonicalComponent("../src"); },
	    std::string("invalid canonical path component"));
	testutil::AssertPanics(
	    t,
	    [&] { path.AppendCanonicalSuffix("/src"); },
	    std::string(
	        "path suffix must not contain a directory separator"));
	testutil::AssertPanics(
	    t,
	    [&] { PathKey("").AppendCanonicalComponent("src"); },
	    std::string("cannot append a component to an empty path key"));
	testutil::AssertPanics(
	    t,
	    [&] { PathKey("").AppendCanonicalSuffix(".ts"); },
	    std::string("cannot append a suffix to an empty path key"));
	testutil::AssertPanics(
	    t,
	    [&] { path.SplitAtCanonicalComponent("../node_modules"); },
	    std::string("invalid canonical path component"));
	testutil::AssertPanics(
	    t,
	    [&] { RootedFilePath("").AppendSuffix(".ts"); },
	    std::string("cannot append a suffix to an empty file name"));
	testutil::AssertPanics(
	    t,
	    [&] { RootedDirectoryPath("").ResolveFile("file.ts"); },
	    std::string("cannot resolve from an empty directory name"));
	gotest::assert::Equal(
	    t,
	    rootedDirectoryPathFromNormalized("/project/src")
	        .ResolveDirectory("types")
	        .AsString(),
	    "/project/src/types");
	gotest::assert::Equal(
	    t,
	    rootedDirectoryPathFromNormalized("/project/src")
	        .ResolveDirectory("types/")
	        .AsString(),
	    "/project/src/types");
	gotest::assert::Equal(
	    t,
	    rootedDirectoryPathFromNormalized("/project/src")
	        .ResolveDirectory("")
	        .AsString(),
	    "/project/src");
	gotest::assert::Equal(
	    t,
	    rootedDirectoryPathFromNormalized("/project/src")
	        .ResolveFile("file.ts/")
	        .AsString(),
	    "/project/src/file.ts");
	gotest::assert::Equal(
	    t,
	    rootedDirectoryPathFromNormalized("/project/src")
	        .ResolveFile("")
	        .AsString(),
	    "/project/src");
	testutil::AssertPanics(
	    t,
	    [&] {
		    RootedDirectoryPath("").ResolveDirectory("types");
	    },
	    std::string("cannot resolve from an empty directory name"));
}

void TestRootedDirectoryPathResolutionMatchesGeneralRooting(T* t) {
	t->Parallel();
	auto base = rootedDirectoryPathFromNormalized("/project/src");
	for (auto path : {"file.ts", "nested/file.ts", "./file.ts",
	                  "../file.ts", "nested\\file.ts", "nested/file.ts/",
	                  "/absolute/file.ts", "c:/absolute/file.ts",
	                  "file:///absolute/file.ts"}) {
		t->Run(std::string(path), [&](T* st) {
			gotest::assert::Equal(
			    st, std::string(base.ResolveFile(path)),
			    std::string(toRootedFilePath(path, base)));
			gotest::assert::Equal(
			    st, std::string(base.ResolveDirectory(path)),
			    std::string(toRootedDirectoryPath(path, base)));
		});
	}
}

void TestSplitAtRootLevelComponentKeepsRoot(T* t) {
	t->Parallel();
	for (auto fileName : std::vector<RootedFilePath>{
	         rootedFilePathFromNormalized(
	             "/node_modules/pkg/index.d.ts"),
	         rootedFilePathFromNormalized(
	             "c:/node_modules/pkg/index.d.ts"),
	         rootedFilePathFromNormalized(
	             "file:///node_modules/pkg/index.d.ts"),
	     }) {
		auto [before, through, ok] =
		    fileName.SplitAtComponent("node_modules");
		gotest::assert::Assert(t, ok);
		auto [root, _rel] = fileName.RootAndRelativePath();
		gotest::assert::Equal(t, std::string(before), std::string(root));
		gotest::assert::Equal(
		    t, std::string(through),
		    std::string(root.ResolveDirectory("node_modules")));

		auto [before2, through2, ok2] =
		    fileName.SplitAtLastComponent("node_modules");
		gotest::assert::Assert(t, ok2);
		gotest::assert::Equal(t, std::string(before2), std::string(root));
		gotest::assert::Equal(
		    t, std::string(through2),
		    std::string(root.ResolveDirectory("node_modules")));

		auto [keyBefore, keyThrough, ok3] =
		    CaseSensitive_.pathKey(fileName.AsPath())
		        .SplitAtCanonicalComponent("node_modules");
		gotest::assert::Assert(t, ok3);
		gotest::assert::Equal(
		    t, std::string(keyBefore),
		    std::string(CaseSensitive_.pathKey(root.AsPath())));
		gotest::assert::Equal(
		    t, std::string(keyThrough),
		    std::string(CaseSensitive_.pathKey(
		        root.ResolveDirectory("node_modules").AsPath())));
	}
}

REGISTER_UNIT_TEST("tspath.TestToRootedFilePath", TestToRootedFilePath);
REGISTER_UNIT_TEST("tspath.TestEncodedDynamicPathsPreserveOpaqueIdentity",
                   TestEncodedDynamicPathsPreserveOpaqueIdentity);
REGISTER_UNIT_TEST("tspath.TestPathKeyFromCanonicalRejectsURLSuffix",
                   TestPathKeyFromCanonicalRejectsURLSuffix);
REGISTER_UNIT_TEST(
    "tspath.TestDynamicPathKeyCaseInsensitiveKeyPreservesIdentity",
    TestDynamicPathKeyCaseInsensitiveKeyPreservesIdentity);
REGISTER_UNIT_TEST("tspath.TestExtensionMutationsPreserveNormalizedInvariant",
                   TestExtensionMutationsPreserveNormalizedInvariant);
REGISTER_UNIT_TEST("tspath.TestFileNameStemsPreserveFilenamePrefixes",
                   TestFileNameStemsPreserveFilenamePrefixes);
REGISTER_UNIT_TEST(
    "tspath.TestCompareFileNameStemsKeepsLiteralDotComponentsAndCasingPolicy",
    TestCompareFileNameStemsKeepsLiteralDotComponentsAndCasingPolicy);
REGISTER_UNIT_TEST("tspath.TestToRootedFilePathRequiresRoot",
                   TestToRootedFilePathRequiresRoot);
REGISTER_UNIT_TEST("tspath.TestRootedFilePathFromAbsolute",
                   TestRootedFilePathFromAbsolute);
REGISTER_UNIT_TEST("tspath.TestRootedFilePathFromNormalized",
                   TestRootedFilePathFromNormalized);
REGISTER_UNIT_TEST("tspath.TestTypedPathConstructorsAndDecoders",
                   TestTypedPathConstructorsAndDecoders);
REGISTER_UNIT_TEST("tspath.TestRelativePath", TestRelativePath);
REGISTER_UNIT_TEST("tspath.TestTypedPathComponents", TestTypedPathComponents);
REGISTER_UNIT_TEST("tspath.TestTryRelativePathBetweenFilePaths",
                   TestTryRelativePathBetweenFilePaths);
REGISTER_UNIT_TEST("tspath.TestRootedFilePathDirectory",
                   TestRootedFilePathDirectory);
REGISTER_UNIT_TEST("tspath.TestRootedFilePathWithoutRoot",
                   TestRootedFilePathWithoutRoot);
REGISTER_UNIT_TEST("tspath.TestRootedFilePathRootAndRelativePath",
                   TestRootedFilePathRootAndRelativePath);
REGISTER_UNIT_TEST("tspath.TestCommonDirectoryOfFiles",
                   TestCommonDirectoryOfFiles);
REGISTER_UNIT_TEST("tspath.TestRelativePathsFromTypedPaths",
                   TestRelativePathsFromTypedPaths);
REGISTER_UNIT_TEST("tspath.TestContainsFilePath", TestContainsFilePath);
REGISTER_UNIT_TEST("tspath.TestRootedPath", TestRootedPath);
REGISTER_UNIT_TEST("tspath.TestRootedPathCompare", TestRootedPathCompare);
REGISTER_UNIT_TEST("tspath.TestPathOperationsSeparateRooting",
                   TestPathOperationsSeparateRooting);
REGISTER_UNIT_TEST(
    "tspath.TestRootedFilePathExtensionOperationsPreserveInvariants",
    TestRootedFilePathExtensionOperationsPreserveInvariants);
REGISTER_UNIT_TEST("tspath.TestForEachAncestorDirectoryPath",
                   TestForEachAncestorDirectoryPath);
REGISTER_UNIT_TEST("tspath.TestRootedFilePathComponents",
                   TestRootedFilePathComponents);
REGISTER_UNIT_TEST("tspath.TestCaseSensitivityKey", TestCaseSensitivityKey);
REGISTER_UNIT_TEST("tspath.TestPathKeyConstructionMethods",
                   TestPathKeyConstructionMethods);
REGISTER_UNIT_TEST(
    "tspath.TestRootedDirectoryPathResolutionMatchesGeneralRooting",
    TestRootedDirectoryPathResolutionMatchesGeneralRooting);
REGISTER_UNIT_TEST("tspath.TestSplitAtRootLevelComponentKeepsRoot",
                   TestSplitAtRootLevelComponentKeepsRoot);

}  // namespace
