// tests_knownsymlinks.cpp — port of tsc/internal/symlinks/knownsymlinks_test.go.
// Package-internal test: knownsymlinks_test.go is `package symlinks` and
// touches unexported members; the C++ port exposes the same surface as
// public with a comment.
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/module/types.h"
#include "internal/symlinks/knownsymlinks.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
namespace module = tsc::module;
namespace tspath = tsc::tspath;
using tsc::symlinks::KnownDirectoryLink;
using tsc::symlinks::KnownSymlinks;
using tsc::symlinks::NewKnownSymlink;

namespace {

void TestNewKnownSymlink(T* t) {
	t->Parallel();
	std::unique_ptr<KnownSymlinks> cache(
	    NewKnownSymlink("/test/dir", true));
	if (cache == nullptr) {
		t->Fatal({"Expected non-nil cache"});
	}
	if (cache->cwd != "/test/dir") {
		t->Errorf("Expected cwd to be '/test/dir', got '%s'",
		          {cache->cwd});
	}
	if (!cache->useCaseSensitiveFileNames) {
		t->Error({"Expected useCaseSensitiveFileNames to be true"});
	}
}

void TestSetDirectory(T* t) {
	t->Parallel();
	std::unique_ptr<KnownSymlinks> cache(
	    NewKnownSymlink("/test/dir", true));
	auto symlinkPath =
	    tspath::ensureTrailingDirectorySeparator(
	        tspath::toPath("/test/symlink", "/test/dir", true));
	auto realDirectory = std::make_shared<KnownDirectoryLink>();
	realDirectory->Real = "/real/path/";
	realDirectory->RealPath = tspath::ensureTrailingDirectorySeparator(
	    tspath::toPath("/real/path", "/test/dir", true));

	cache->SetDirectory("/test/symlink", symlinkPath, realDirectory);

	// Check that directory was stored
	auto [stored, ok] = cache->Directories()->Load(symlinkPath);
	if (!ok) {
		t->Fatal({"Expected directory to be stored"});
	}
	if (stored->Real != realDirectory->Real) {
		t->Errorf("Expected Real to be '%s', got '%s'",
		          {realDirectory->Real, stored->Real});
	}
	if (stored->RealPath != realDirectory->RealPath) {
		t->Errorf("Expected RealPath to be '%s', got '%s'",
		          {realDirectory->RealPath, stored->RealPath});
	}

	// Check that realpath mapping was created
	auto [set, ok2] =
	    cache->DirectoriesByRealpath()->Load(realDirectory->RealPath);
	if (!ok2 || set->Size() == 0) {
		t->Fatal({"Expected realpath mapping to be created"});
	}
	if (!set->Has("/test/symlink")) {
		t->Error({"Expected symlink '/test/symlink' to be in set"});
	}
}

void TestSetFile(T* t) {
	t->Parallel();
	std::unique_ptr<KnownSymlinks> cache(
	    NewKnownSymlink("/test/dir", true));
	std::string symlink = "/test/symlink/file.ts";
	auto symlinkPath = tspath::toPath(symlink, "/test/dir", true);
	std::string realpath = "/real/path/file.ts";

	cache->SetFile(symlink, symlinkPath, realpath);

	auto [stored, ok] = cache->Files()->Load(symlinkPath);
	if (!ok) {
		t->Fatal({"Expected file to be stored"});
	}
	if (stored != realpath) {
		t->Errorf("Expected realpath to be '%s', got '%s'",
		          {realpath, stored});
	}
}

void TestProcessResolution(T* t) {
	t->Parallel();
	std::unique_ptr<KnownSymlinks> cache(
	    NewKnownSymlink("/test/dir", true));

	// Test with empty paths
	cache->ProcessResolution("", "");
	cache->ProcessResolution("original", "");
	cache->ProcessResolution("", "resolved");

	// Test with valid paths
	std::string originalPath = "/test/original/file.ts";
	std::string resolvedPath = "/test/resolved/file.ts";
	cache->ProcessResolution(originalPath, resolvedPath);

	// Check that file was stored
	auto symlinkPath =
	    tspath::toPath(originalPath, "/test/dir", true);
	auto [stored, ok] = cache->Files()->Load(symlinkPath);
	if (!ok) {
		t->Fatal({"Expected file to be stored"});
	}
	if (stored != resolvedPath) {
		t->Errorf("Expected resolved path to be '%s', got '%s'",
		          {resolvedPath, stored});
	}
}

void TestGuessDirectorySymlink(T* t) {
	t->Parallel();
	std::unique_ptr<KnownSymlinks> cache(
	    NewKnownSymlink("/test/dir", true));

	struct {
		std::string name;
		std::string a;
		std::string b;
		std::string cwd;
		std::string expected[2]; // [commonResolved, commonOriginal]
	} tests[] = {
	    {.name = "identical paths",
	     .a = "/test/path/file.ts",
	     .b = "/test/path/file.ts",
	     .cwd = "/test/dir",
	     .expected = {"/", "/"}},
	    {.name = "different files same directory",
	     .a = "/test/path/file1.ts",
	     .b = "/test/path/file2.ts",
	     .cwd = "/test/dir",
	     .expected = {"", ""}},
	    {.name = "different directories",
	     .a = "/test/path1/file.ts",
	     .b = "/test/path2/file.ts",
	     .cwd = "/test/dir",
	     .expected = {"/test/path1", "/test/path2"}},
	    {.name = "node_modules paths",
	     .a = "/test/node_modules/pkg/file.ts",
	     .b = "/test/node_modules/pkg/file.ts",
	     .cwd = "/test/dir",
	     .expected = {"/test/node_modules/pkg",
	                  "/test/node_modules/pkg"}},
	    {.name = "scoped package paths",
	     .a = "/test/node_modules/@scope/pkg/file.ts",
	     .b = "/test/node_modules/@scope/pkg/file.ts",
	     .cwd = "/test/dir",
	     .expected = {"/test/node_modules/@scope/pkg",
	                  "/test/node_modules/@scope/pkg"}},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&cache, tt](T* t) {
			t->Parallel();
			auto [commonResolved, commonOriginal] =
			    cache->guessDirectorySymlink(tt.a, tt.b, tt.cwd);
			if (commonResolved != tt.expected[0]) {
				t->Errorf(
				    "Expected commonResolved to be '%s', got '%s'",
				    {tt.expected[0], commonResolved});
			}
			if (commonOriginal != tt.expected[1]) {
				t->Errorf(
				    "Expected commonOriginal to be '%s', got '%s'",
				    {tt.expected[1], commonOriginal});
			}
		});
	}
}

void TestIsNodeModulesOrScopedPackageDirectory(T* t) {
	t->Parallel();
	std::unique_ptr<KnownSymlinks> cache(
	    NewKnownSymlink("/test/dir", true));

	struct {
		std::string name;
		std::string dir;
		bool expected;
	} tests[] = {
	    {"node_modules", "node_modules", true},
	    {"scoped package", "@scope", true},
	    {"regular directory", "src", false},
	    {"empty string", "", false},
	    // The function is case sensitive
	    {"case insensitive node_modules", "NODE_MODULES", false},
	    {"case insensitive scoped", "@SCOPE", true},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&cache, tt](T* t) {
			t->Parallel();
			auto result =
			    cache->isNodeModulesOrScopedPackageDirectory(tt.dir);
			if (result != tt.expected) {
				t->Errorf(
				    "Expected %v, got %v for directory '%s'",
				    {tt.expected, result, tt.dir});
			}
		});
	}
}

void TestSetSymlinksFromResolutions(T* t) {
	t->Parallel();
	std::unique_ptr<KnownSymlinks> cache(
	    NewKnownSymlink("/test/dir", true));

	// Mock resolution data
	struct {
		std::string originalPath;
		std::string resolvedPath;
		std::string moduleName;
		tsc::ResolutionMode mode;
		tspath::Path filePath;
	} resolvedModules[] = {
	    {.originalPath = "/test/original/file1.ts",
	     .resolvedPath = "/test/resolved/file1.ts",
	     .moduleName = "module1",
	     .mode = tsc::ResolutionModeNone,
	     .filePath =
	         tspath::toPath("/test/source.ts", "/test/dir", true)},
	    {.originalPath = "/test/original/file2.ts",
	     .resolvedPath = "/test/resolved/file2.ts",
	     .moduleName = "module2",
	     .mode = tsc::ResolutionModeNone,
	     .filePath =
	         tspath::toPath("/test/source.ts", "/test/dir", true)},
	};

	// Mock callbacks
	auto forEachResolvedModule =
	    [&](const std::function<void(module::ResolvedModule*,
	                                 std::string_view,
	                                 tsc::ResolutionMode,
	                                 tspath::Path)>& callback,
	        tsc::SourceFile*) {
		    for (auto& res : resolvedModules) {
			    module::ResolvedModule resolution;
			    resolution.OriginalPath = res.originalPath;
			    resolution.ResolvedFileName = res.resolvedPath;
			    callback(&resolution, res.moduleName, res.mode,
			             res.filePath);
		    }
	    };

	auto forEachResolvedTypeReferenceDirective =
	    [&](const std::function<void(
	            module::ResolvedTypeReferenceDirective*,
	            std::string_view, tsc::ResolutionMode,
	            tspath::Path)>&,
	        tsc::SourceFile*) {
		    // No type reference directives for this test
	    };

	cache->SetSymlinksFromResolutions(
	    forEachResolvedModule, forEachResolvedTypeReferenceDirective);

	// Check that files were stored
	for (auto& res : resolvedModules) {
		auto symlinkPath =
		    tspath::toPath(res.originalPath, "/test/dir", true);
		auto [stored, ok] = cache->Files()->Load(symlinkPath);
		if (!ok) {
			t->Errorf("Expected file '%s' to be stored",
			          {res.originalPath});
			continue;
		}
		if (stored != res.resolvedPath) {
			t->Errorf("Expected resolved path to be '%s', got '%s'",
			          {res.resolvedPath, stored});
		}
	}
}

void TestKnownSymlinksThreadSafety(T* t) {
	t->Parallel();
	std::unique_ptr<KnownSymlinks> cache(
	    NewKnownSymlink("/test/dir", true));

	// Test concurrent access
	std::vector<std::thread> threads;
	std::mutex failuresMu;
	std::vector<std::string> failures;

	for (int i = 0; i < 10; i++) {
		// T is not thread-safe the way *testing.T is; worker
		// threads record mismatches and the test body reports
		// them after join (identical pass/fail semantics).
		threads.emplace_back([cache = cache.get(), i, &failures,
	                      &failuresMu] {
			auto id = static_cast<char>('0' + i);
			auto symlinkPath =
			    tspath::ensureTrailingDirectorySeparator(
			        tspath::toPath("/test/symlink" +
			                           std::string(1, id),
			                       "/test/dir", true));
			auto realDirectory =
			    std::make_shared<KnownDirectoryLink>();
			realDirectory->Real =
			    "/real/path" + std::string(1, id) + "/";
			realDirectory->RealPath =
			    tspath::ensureTrailingDirectorySeparator(
			        tspath::toPath("/real/path" +
			                           std::string(1, id),
			                       "/test/dir", true));

			cache->SetDirectory("/test/symlink" +
			                        std::string(1, id),
			                    symlinkPath, realDirectory);

			// Read back
			auto [stored, ok] =
			    cache->Directories()->Load(symlinkPath);
			if (!ok) {
				std::lock_guard<std::mutex> lock(failuresMu);
				failures.push_back(
				    "Goroutine " + std::to_string(i) +
				    ": Expected directory to be stored");
				return;
			}
			if (stored->Real != realDirectory->Real) {
				std::lock_guard<std::mutex> lock(failuresMu);
				failures.push_back(
				    "Goroutine " + std::to_string(i) +
				    ": Expected Real to be '" + realDirectory->Real +
				    "', got '" + stored->Real + "'");
			}
		});
	}

	// Wait for all goroutines to complete
	for (auto& th : threads) {
		th.join();
	}

	for (auto& f : failures) {
		t->Error({f});
	}

	// Verify all directories were stored
	if (cache->Directories()->Size() != 10) {
		t->Errorf("Expected 10 directories to be stored, got %d",
		          {cache->Directories()->Size()});
	}
}

} // namespace

REGISTER_UNIT_TEST("symlinks.TestNewKnownSymlink", TestNewKnownSymlink);
REGISTER_UNIT_TEST("symlinks.TestSetDirectory", TestSetDirectory);
REGISTER_UNIT_TEST("symlinks.TestSetFile", TestSetFile);
REGISTER_UNIT_TEST("symlinks.TestProcessResolution",
                   TestProcessResolution);
REGISTER_UNIT_TEST("symlinks.TestGuessDirectorySymlink",
                   TestGuessDirectorySymlink);
REGISTER_UNIT_TEST("symlinks.TestIsNodeModulesOrScopedPackageDirectory",
                   TestIsNodeModulesOrScopedPackageDirectory);
REGISTER_UNIT_TEST("symlinks.TestSetSymlinksFromResolutions",
                   TestSetSymlinksFromResolutions);
REGISTER_UNIT_TEST("symlinks.TestKnownSymlinksThreadSafety",
                   TestKnownSymlinksThreadSafety);
