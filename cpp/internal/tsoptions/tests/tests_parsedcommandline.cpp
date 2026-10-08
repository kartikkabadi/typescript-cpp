// Port of tsc/internal/tsoptions/parsedcommandline_test.go (package tsoptions_test).
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tsoptions/tsoptionstest/tsoptionstest.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace {

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace tsoptions = tsc::tsoptions;
namespace tsoptionstest = tsc::tsoptions::tsoptionstest;
namespace tspath = tsc::tspath;
namespace vfstest = tsc::vfs::vfstest;

template <typename MapT>
void assertMatches(T* t, tsoptions::ParsedCommandLine* parsedCommandLine,
                   const MapT& files,
                   const std::vector<std::string>& matches) {
	t->Helper();
	for (const auto& [fileName, _] : files) {
		bool actual = parsedCommandLine->PossiblyMatchesFileName(fileName);
		bool expected =
		    std::find(matches.begin(), matches.end(), fileName) !=
		    matches.end();
		assert::Equal(t, actual, expected, "fileName: " + fileName);
	}
	for (const auto& fileName : matches) {
		if (files.find(fileName) == files.end()) {
			bool actual =
			    parsedCommandLine->PossiblyMatchesFileName(fileName);
			assert::Equal(t, actual, true, "fileName: " + fileName);
		}
	}
}

// TestParsedCommandLine — parsedcommandline_test.go:15.
void TestParsedCommandLine(T* t) {
	t->Run("PossiblyMatchesFileName", [&](T* t) {
		std::unordered_map<std::string, vfstest::MapFileInput> noFiles;
		auto noFilesFS = vfstest::FromMap(noFiles, true);

		std::unordered_map<std::string, std::string> files = {
		    {"/dev/a.ts", ""},
		    {"/dev/a.d.ts", ""},
		    {"/dev/a.js", ""},
		    {"/dev/b.ts", ""},
		    {"/dev/b.js", ""},
		    {"/dev/c.d.ts", ""},
		    {"/dev/z/a.ts", ""},
		    {"/dev/z/abz.ts", ""},
		    {"/dev/z/aba.ts", ""},
		    {"/dev/z/b.ts", ""},
		    {"/dev/z/bbz.ts", ""},
		    {"/dev/z/bba.ts", ""},
		    {"/dev/x/a.ts", ""},
		    {"/dev/x/aa.ts", ""},
		    {"/dev/x/b.ts", ""},
		    {"/dev/x/y/a.ts", ""},
		    {"/dev/x/y/b.ts", ""},
		    {"/dev/js/a.js", ""},
		    {"/dev/js/b.js", ""},
		    {"/dev/js/d.min.js", ""},
		    {"/dev/js/ab.min.js", ""},
		    {"/ext/ext.ts", ""},
		    {"/ext/b/a..b.ts", ""},
		};

		// Go: ReloadFileNamesOfParsedCommandLine(noFilesFS vfs.FS); the C++
		// signature takes module::ResolutionHost* — wrap in a
		// VfsParseConfigHost over the same FS.
		tsoptions::tsoptionstest::VfsParseConfigHost noFilesHost;
		noFilesHost.Vfs = noFilesFS;
		noFilesHost.CurrentDirectory = "/dev";

		t->Run("with literal file list", [&](T* t) {
			t->Run("without exclude", [&](T* t) {
				auto* parsedCommandLine = tsoptionstest::GetParsedCommandLine(
				    R"({
						"files": [
							"a.ts",
							"b.ts"
						]
					})",
				    files, "/dev", true);

				assertMatches(t, parsedCommandLine, files,
				              {"/dev/a.ts", "/dev/b.ts"});
			});

			t->Run("are not removed due to excludes", [&](T* t) {
				auto* parsedCommandLine = tsoptionstest::GetParsedCommandLine(
				    R"({
						"files": [
							"a.ts",
							"b.ts"
						],
						"exclude": [
							"b.ts"
						]
					})",
				    files, "/dev", true);

				assertMatches(t, parsedCommandLine, files,
				              {"/dev/a.ts", "/dev/b.ts"});

				auto* emptyParsedCommandLine =
				    parsedCommandLine
				        ->ReloadFileNamesOfParsedCommandLine(
				            noFilesHost.FS());
				assertMatches(t, emptyParsedCommandLine, noFiles,
				              {"/dev/a.ts", "/dev/b.ts"});
			});

			t->Run("duplicates", [&](T* t) {
				auto* parsedCommandLine = tsoptionstest::GetParsedCommandLine(
				    R"({
						"files": [
							"a.ts",
							"a.ts",
							"b.ts",
						]
					})",
				    files, "/dev", true);

				assert::DeepEqual(
				    t, parsedCommandLine->LiteralFileNames(),
				    std::vector<std::string>{"/dev/a.ts", "/dev/b.ts"});
			});
		});

		t->Run("with literal include list", [&](T* t) {
			t->Run("without exclude", [&](T* t) {
				auto* parsedCommandLine = tsoptionstest::GetParsedCommandLine(
				    R"({
						"include": [
							"a.ts",
							"b.ts"
						]
					})",
				    files, "/dev", true);

				assertMatches(t, parsedCommandLine, files,
				              {"/dev/a.ts", "/dev/b.ts"});

				auto* emptyParsedCommandLine =
				    parsedCommandLine
				        ->ReloadFileNamesOfParsedCommandLine(
				            noFilesHost.FS());
				assertMatches(t, emptyParsedCommandLine, noFiles,
				              {"/dev/a.ts", "/dev/b.ts"});
			});
		});

		t->Run(
		    "PossiblyMatchesFileName with content mapper extensions",
		    [&](T* t) {
			    auto host = tsoptionstest::NewVFSParseConfigHost(
			        {
			            {"/dev/node_modules/mapper/package.json",
			             R"({ "name": "mapper", "version": "1.0.0", "typescript": { "contentMapper": { "exec": ["mapper"] } } })"},
			        },
			        "/dev", true);
			    std::string configFileName = "/dev/tsconfig.json";
			    std::string jsonText = R"({
			"include": ["src"],
			"contentMappers": [ { "package": "mapper", "extensions": [".box"] } ]
		})";
			    auto* tsconfigSourceFile =
			        tsoptions::NewTsconfigSourceFileFromFilePath(
			            configFileName, "/dev/tsconfig.json", jsonText);
			    auto* parsedCommandLine =
			        tsoptions::ParseJsonSourceFileConfigFileContent(
			            tsconfigSourceFile, host.get(), "/dev",
			            new tsc::CompilerOptions{
			                .RunExternalCode = tsc::Tristate::True},
			            nullptr, configFileName, {}, nullptr);

			    // A created content-mapped file under an included directory must be recognized as a
			    // possible root file, or the config's root files are never reloaded for it.
			    assert::Assert(
			        t, parsedCommandLine->PossiblyMatchesFileName(
			               "/dev/src/new.box"));
			    assert::Assert(
			        t, parsedCommandLine->PossiblyMatchesFileName(
			               "/dev/src/new.ts"));
			    assert::Assert(
			        t, !parsedCommandLine->PossiblyMatchesFileName(
			                "/dev/src/new.vue"));
			    assert::Assert(
			        t, !parsedCommandLine->PossiblyMatchesFileName(
			                "/dev/other/new.box"));

			    auto insensitiveHost =
			        tsoptionstest::NewVFSParseConfigHost(
			            {
			                {"/dev/node_modules/mapper/package.json",
			                 R"({ "name": "mapper", "version": "1.0.0", "typescript": { "contentMapper": { "exec": ["mapper"] } } })"},
			            },
			            "/dev", false);
			    auto* insensitiveCommandLine =
			        tsoptions::ParseJsonSourceFileConfigFileContent(
			            tsconfigSourceFile, insensitiveHost.get(), "/dev",
			            new tsc::CompilerOptions{
			                .RunExternalCode = tsc::Tristate::True},
			            nullptr, configFileName, {}, nullptr);
			    assert::Assert(
			        t, insensitiveCommandLine->PossiblyMatchesFileName(
			               "/dev/src/new.BOX"));
		    });

		t->Run("WithFileNames preserves config identity", [&](T* t) {
			std::string configFileName = "/dev/tsconfig.json";
			auto* tsconfigSourceFile =
			    tsoptions::NewTsconfigSourceFileFromFilePath(
			        configFileName, tspath::Path(configFileName), "{}");
			auto emptyHost = tsoptionstest::NewVFSParseConfigHost(
			    {}, "/dev", true);
			auto* parsedCommandLine =
			    tsoptions::ParseJsonSourceFileConfigFileContent(
			        tsconfigSourceFile, emptyHost.get(), "/dev", nullptr,
			        nullptr, configFileName, {}, nullptr);

			auto* withTypings = parsedCommandLine->WithFileNames(
			    {"/dev/index.ts", "/cache/@types/pkg/index.d.ts"});
			assert::Equal(t, withTypings->ConfigName(), configFileName);
			assert::DeepEqual(
			    t, withTypings->FileNames(),
			    std::vector<std::string>{
			        "/dev/index.ts", "/cache/@types/pkg/index.d.ts"});
		});
	});
}

}  // namespace

REGISTER_UNIT_TEST("tsoptions.TestParsedCommandLine", TestParsedCommandLine);
