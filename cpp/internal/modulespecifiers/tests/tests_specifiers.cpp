// tests_specifiers.cpp — port of tsc/internal/modulespecifiers/specifiers_test.go.
//
// Go's ModuleSpecifierGenerationHost is an interface satisfied by a small
// mock; the C++ port hardcodes checker::Program, so the mock below is a
// Program subclass implementing every pure virtual with the same stubbed
// answers the Go mock gives (and overriding the defaulted members where the
// mock supplies non-default values).
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/module/types.h"
#include "internal/modulespecifiers/types.h"
#include "internal/packagejson/packagejson.h"
#include "internal/symlinks/knownsymlinks.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

namespace {

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace ms = tsc::modulespecifiers;
namespace tspath = tsc::tspath;

// Mock host for testing.
struct mockModuleSpecifierGenerationHost : tsc::checker::Program {
	std::string currentDir;
	std::vector<std::string> contentMapperExtensions;
	bool useCaseSensitiveFileNames = false;
	tsc::symlinks::KnownSymlinks* symlinkCache = nullptr;

	std::string GetCurrentDirectory() override { return currentDir; }
	bool UseCaseSensitiveFileNames() override {
		return useCaseSensitiveFileNames;
	}
	tsc::symlinks::KnownSymlinks* GetSymlinkCache() override {
		return symlinkCache;
	}
	std::optional<tsc::checker::ResolvedModule> GetResolvedModule(
	    tsc::SourceFile* file, const std::string& moduleReference,
	    tsc::ResolutionMode mode) override {
		return std::nullopt;
	}
	std::string GetGlobalTypingsCacheLocation() override { return ""; }
	std::string CommonSourceDirectory() override { return currentDir; }
	std::vector<std::string> ContentMapperExtensions() override {
		return contentMapperExtensions;
	}
	tsc::checker::SourceOutputAndProjectReference*
	GetProjectReferenceFromSource(const tspath::Path& path) override {
		return nullptr;
	}
	std::vector<std::string> GetRedirectTargets(
	    const tspath::Path& path) override {
		return {};
	}
	std::string GetSourceOfProjectReferenceIfOutputIncluded(
	    tsc::SourceFile* file) override {
		return file->FileName();
	}
	bool FileExists(const std::string& path) override { return true; }
	std::string GetNearestAncestorDirectoryWithPackageJson(
	    const std::string& dirname) override {
		return "";
	}
	std::shared_ptr<tsc::packagejson::InfoCacheEntry> GetPackageJsonInfo(
	    const std::string& pkgJsonPath) override {
		return nullptr;
	}
	tsc::ResolutionMode GetDefaultResolutionModeForFile(
	    tsc::SourceFile* file) override {
		return tsc::ResolutionModeNone;
	}
	tsc::module::ResolvedModule* GetResolvedModuleFromModuleSpecifier(
	    tsc::SourceFile* file, tsc::Node* moduleSpecifier) override {
		return nullptr;
	}
	tsc::ResolutionMode GetModeForUsageLocation(
	    tsc::SourceFile* file, tsc::Node* location) override {
		return tsc::ResolutionModeNone;
	}

	// Remaining checker::Program pure virtuals — the Go interface has no
	// counterparts; stubs return zero values.
	const tsc::CompilerOptions* Options() override { return nullptr; }
	std::vector<tsc::SourceFile*> SourceFiles() override { return {}; }
	void BindSourceFiles() override {}
	tsc::SourceFile* GetSourceFile(const std::string& fileName) override {
		return nullptr;
	}
	tsc::SourceFile* GetSourceFileForResolvedModule(
	    const std::string& fileName) override {
		return nullptr;
	}
	tsc::ModuleKind GetEmitModuleFormatOfFile(
	    tsc::SourceFile* sourceFile) override {
		return tsc::ModuleKind::None;
	}
	tsc::ResolutionMode GetEmitSyntaxForUsageLocation(
	    tsc::SourceFile* sourceFile, tsc::Node* usageLocation) override {
		return tsc::ResolutionModeNone;
	}
	tsc::ModuleKind GetImpliedNodeFormatForEmit(
	    tsc::SourceFile* sourceFile) override {
		return tsc::ModuleKind::None;
	}
	bool SourceFileMayBeEmitted(tsc::SourceFile* sourceFile,
	                            bool forceDtsEmit) override {
		return false;
	}
	bool IsSourceFileFromExternalLibrary(
	    tsc::SourceFile* file) const override {
		return false;
	}
};

void TestGetEachFileNameOfModule(T* t) {
	t->Parallel();
	struct {
		std::string name;
		std::string importingFile;
		std::string importedFile;
		bool preferSymlinks;
		int expectedCount;
		std::vector<std::string> expectedPaths;
	} tests[] = {
	    {"basic file path", "/project/src/main.ts", "/project/lib/utils.ts",
	     false, 1, {"/project/lib/utils.ts"}},
	    {"symlink preference false", "/project/src/main.ts",
	     "/project/lib/utils.ts", false, 1, {}},
	    {"symlink preference true", "/project/src/main.ts",
	     "/project/lib/utils.ts", true, 1, {}},
	    {"ignored path with no alternatives", "/project/src/main.ts",
	     "/project/node_modules/.pnpm/file.ts", false, 1, {}},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&tt](T* t) {
			t->Parallel();
			mockModuleSpecifierGenerationHost host;
			host.currentDir = "/project";
			host.useCaseSensitiveFileNames = true;
			host.symlinkCache =
			    tsc::symlinks::NewKnownSymlink("/project", true);

			auto result = ms::GetEachFileNameOfModule(
			    tt.importingFile, tt.importedFile, &host,
			    tt.preferSymlinks);

			if ((int)result.size() != tt.expectedCount) {
				t->Errorf("Expected %d paths, got %d",
				          {tt.expectedCount, (int)result.size()});
			}

			if (!tt.expectedPaths.empty()) {
				for (size_t i = 0; i < tt.expectedPaths.size(); i++) {
					auto& expectedPath = tt.expectedPaths[i];
					if (i >= result.size()) {
						t->Errorf(
						    "Expected path %d: %s, but result has "
						    "only %d paths",
						    {(int)i, expectedPath,
						     (int)result.size()});
						continue;
					}
					if (result[i].FileName != expectedPath) {
						t->Errorf(
						    "Expected path %d to be %s, got %s",
						    {(int)i, expectedPath,
						     result[i].FileName});
					}
				}
			}

			for (size_t i = 0; i < result.size(); i++) {
				if (result[i].FileName.empty()) {
					t->Errorf("Path %d has empty FileName", {(int)i});
				}
			}
			delete host.symlinkCache;
		});
	}
}

void TestGetEachFileNameOfModuleWithSymlinks(T* t) {
	t->Parallel();
	mockModuleSpecifierGenerationHost host;
	host.currentDir = "/project";
	host.useCaseSensitiveFileNames = true;
	host.symlinkCache = tsc::symlinks::NewKnownSymlink("/project", true);

	auto symlinkPath = tspath::ensureTrailingDirectorySeparator(
	    tspath::toPath("/project/symlink", "/project", true));
	auto realDirectory = std::make_shared<tsc::symlinks::KnownDirectoryLink>(
	    tsc::symlinks::KnownDirectoryLink{
	        {},
	        "/real/path/",
	        tspath::ensureTrailingDirectorySeparator(
	            tspath::toPath("/real/path", "/project", true))});
	host.symlinkCache->SetDirectory("/project/symlink", symlinkPath,
	                                realDirectory);

	auto result = ms::GetEachFileNameOfModule("/project/src/main.ts",
	                                          "/real/path/file.ts", &host,
	                                          true);

	// Should find the symlink path
	bool found = false;
	for (auto& path : result) {
		if (path.FileName == "/project/symlink/file.ts") {
			found = true;
			break;
		}
	}

	if (!found) {
		t->Error({"Expected to find symlink path /project/symlink/file.ts"});
	}
	delete host.symlinkCache;
}

void TestContainsNodeModules(T* t) {
	t->Parallel();
	struct {
		std::string name;
		std::string path;
		bool expected;
	} tests[] = {
	    {"contains node_modules", "/project/node_modules/lodash/index.js",
	     true},
	    {"does not contain node_modules", "/project/src/utils.ts", false},
	    {"node_modules in middle",
	     "/project/packages/node_modules/pkg/file.js", true},
	    {"empty path", "", false},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&tt](T* t) {
			t->Parallel();
			bool result = ms::ContainsNodeModules(tt.path);
			if (result != tt.expected) {
				t->Errorf("ContainsNodeModules(%q) = %v, expected %v",
				          {tt.path, result, tt.expected});
			}
		});
	}
}

void TestContainsIgnoredPath(T* t) {
	t->Parallel();
	struct {
		std::string name;
		std::string path;
		bool expected;
	} tests[] = {
	    {"ignored path", "/project/node_modules/.pnpm/file.ts", true},
	    {"not ignored path", "/project/src/file.ts", false},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&tt](T* t) {
			t->Parallel();
			bool result = ms::containsIgnoredPath(tt.path);
			if (result != tt.expected) {
				t->Errorf("containsIgnoredPath(%q) = %v, expected %v",
				          {tt.path, result, tt.expected});
			}
		});
	}
}

void TestTryGetRealFileNameForNonJSDeclarationFileName(T* t) {
	t->Parallel();
	struct {
		std::string name;
		std::string fileName;
		std::string expected;
	} tests[] = {
	    {"json declaration file", "/project/foo.d.json.ts",
	     "/project/foo.json"},
	    {"multi-dot source extension declaration file",
	     "/project/foo.module.d.css.ts", "/project/foo.module.css"},
	    {"plain dts file ignored", "/project/foo.d.ts", ""},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&tt](T* t) {
			t->Parallel();
			if (auto got =
			        ms::TryGetRealFileNameForNonJSDeclarationFileName(
			            tt.fileName);
			    got != tt.expected) {
				t->Errorf(
				    "TryGetRealFileNameForNonJSDeclarationFileName(%q) "
				    "= %q, expected %q",
				    {tt.fileName, got, tt.expected});
			}
		});
	}
}

void TestTryGetModuleNameFromExportsOrImports(T* t) {
	t->Parallel();
	t->Run("with exports pattern", [](T* t) {
		t->Parallel();

		struct {
			std::string name;
			std::string targetFilePath;
			std::string expected;
		} tests[] = {
		    {"match", "/pkg/src/things/thing1/index.ts",
		     "./src/things/thing1"},
		    {"mismatch with matching leading and trailing strings",
		     "/pkg/src/things/index.ts", ""},
		};

		for (auto& tt : tests) {
			t->Run(tt.name, [&tt](T* t) {
				t->Parallel();
				tsc::CompilerOptions options;
				mockModuleSpecifierGenerationHost host;
				tsc::packagejson::ExportsOrImports exports;
				exports.type =
				    tsc::packagejson::JSONValueType::String;
				exports.str = "./src/things/*/index.js";
				auto result =
				    ms::tryGetModuleNameFromExportsOrImports(
				        &options, &host, tt.targetFilePath, "/pkg",
				        "./src/things/*", exports, {},
				        ms::MatchingMode::Pattern, false, false);
				if (result != tt.expected) {
					t->Errorf(
					    "tryGetModuleNameFromExportsOrImports("
					    "targetFilePath = %q) = %v, expected %v",
					    {tt.targetFilePath, result, tt.expected});
				}
			});
		}
	});
}

REGISTER_UNIT_TEST("modulespecifiers.TestGetEachFileNameOfModule",
                   TestGetEachFileNameOfModule);
REGISTER_UNIT_TEST("modulespecifiers.TestGetEachFileNameOfModuleWithSymlinks",
                   TestGetEachFileNameOfModuleWithSymlinks);
REGISTER_UNIT_TEST("modulespecifiers.TestContainsNodeModules",
                   TestContainsNodeModules);
REGISTER_UNIT_TEST("modulespecifiers.TestContainsIgnoredPath",
                   TestContainsIgnoredPath);
REGISTER_UNIT_TEST(
    "modulespecifiers.TestTryGetRealFileNameForNonJSDeclarationFileName",
    TestTryGetRealFileNameForNonJSDeclarationFileName);
REGISTER_UNIT_TEST("modulespecifiers.TestTryGetModuleNameFromExportsOrImports",
                   TestTryGetModuleNameFromExportsOrImports);

} // namespace
