// Port of tsc/internal/tsoptions/wildcarddirectories_test.go (package tsoptions).
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

namespace {

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace tsoptions = tsc::tsoptions;
namespace tspath = tsc::tspath;

// TestGetWildcardDirectories_DotPrefixedIncludeWithDotDirExclude —
// wildcarddirectories_test.go:10.
void TestGetWildcardDirectories_DotPrefixedIncludeWithDotDirExclude(T* t) {
	// https://github.com/microsoft/TypeScript/tsc/issues/3733
	// "./"-prefixed include specs must be fully normalized before being tested
	// against exclude patterns; otherwise the leftover literal "." path segment
	// matches dot-directory excludes like "**/.*/", silently dropping every
	// wildcard directory (and with them, root file watching for the config).
	auto result = tsoptions::getWildcardDirectories(
	    {"./app/**/*.ts", "./app/**/*.tsx"},
	    {"**/node_modules", "**/.*/", "./build"},
	    tspath::ComparePathsOptions{
	        .currentDirectory = "/home/projects/monorepo/apps/web",
	        .useCaseSensitiveFileNames = true,
	    });
	assert::DeepEqual(
	    t, result,
	    std::unordered_map<std::string, bool>{
	        {"/home/projects/monorepo/apps/web/app", true}});
}

// TestGetWildcardDirectories_NonASCIICharacters —
// wildcarddirectories_test.go:31.
void TestGetWildcardDirectories_NonASCIICharacters(T* t) {
	struct Case {
		std::string name;
		std::vector<std::string> include, exclude;
		std::string currentDirectory;
		bool useCaseSensitiveFileNames;
	};
	std::vector<Case> tests = {
	    {
	        .name = "Norwegian character æ in path",
	        .include =
	            {"src/**/*.test.ts", "src/**/*.stories.ts", "src/**/*.mdx"},
	        .exclude = {"node_modules"},
	        .currentDirectory =
	            "C:/Users/TobiasLægreid/dev/app/frontend/packages/react",
	        .useCaseSensitiveFileNames = false,
	    },
	    {
	        .name = "Japanese characters in path",
	        .include = {"src/**/*.ts"},
	        .exclude = {"テスト"},
	        .currentDirectory = "/Users/ユーザー/プロジェクト",
	        .useCaseSensitiveFileNames = true,
	    },
	    {
	        .name = "Chinese characters in path",
	        .include = {"源代码/**/*.js"},
	        .exclude = {"节点模块"},
	        .currentDirectory = "/home/用户/项目",
	        .useCaseSensitiveFileNames = true,
	    },
	    {
	        .name = "Various Unicode characters",
	        .include = {"src/**/*.ts"},
	        .exclude = {"node_modules"},
	        .currentDirectory = "/Users/Müller/café/naïve/résumé",
	        .useCaseSensitiveFileNames = false,
	    },
	};

	for (const auto& tt : tests) {
		t->Run(tt.name, [&](T* t) {
			tspath::ComparePathsOptions comparePathsOptions{
			    .currentDirectory = tt.currentDirectory,
			    .useCaseSensitiveFileNames = tt.useCaseSensitiveFileNames,
			};
			auto result = tsoptions::getWildcardDirectories(
			    tt.include, tt.exclude, comparePathsOptions);
			assert::Assert(t, !result.empty(), "expected non-nil result");
		});
	}
}

}  // namespace

REGISTER_UNIT_TEST(
    "tsoptions.TestGetWildcardDirectories_DotPrefixedIncludeWithDotDirExclude",
    TestGetWildcardDirectories_DotPrefixedIncludeWithDotDirExclude);
REGISTER_UNIT_TEST("tsoptions.TestGetWildcardDirectories_NonASCIICharacters",
                   TestGetWildcardDirectories_NonASCIICharacters);
