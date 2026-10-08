// Port of tsc/internal/tsoptions/contentmappers_test.go (package tsoptions).
#include <string>
#include <vector>

#include "internal/contentmapper/contentmapper.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/diagnostics/messages_generated.h"
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
namespace contentmapper = tsc::contentmapper;
namespace diagnostics = tsc;
namespace tsoptions = tsc::tsoptions;
namespace tsoptionstest = tsc::tsoptions::tsoptionstest;
namespace tspath = tsc::tspath;
namespace vfstest = tsc::vfs::vfstest;

// TestGetContentMapperForFileNameUsesLongestExtension —
// contentmappers_test.go:20.
void TestGetContentMapperForFileNameUsesLongestExtension(T* t) {
	auto* zMapper = new contentmapper::Mapper{
	    .Definition = {.Package = "z", .Extensions = {".z"}}};
	auto* yzMapper = new contentmapper::Mapper{
	    .Definition = {.Package = "yz", .Extensions = {".y.z"}}};
	auto* parsedOptions = new tsoptions::ParsedOptions{};
	parsedOptions->ContentMappers = {zMapper, yzMapper};
	tsoptions::ParsedCommandLine commandLine{};
	commandLine.ParsedConfig = parsedOptions;

	assert::Equal(
	    t, commandLine.GetContentMapperForFileName("/src/Component.y.z"),
	    yzMapper);
	assert::Equal(
	    t, commandLine.GetContentMapperForFileName("/src/Component.z"),
	    zMapper);
}

// TestGetContentMapperForFileNameUsesHostCaseSensitivity —
// contentmappers_test.go:31.
void TestGetContentMapperForFileNameUsesHostCaseSensitivity(T* t) {
	auto* mapper = new contentmapper::Mapper{
	    .Definition = {.Extensions = {".vue"}}};
	tsoptions::ParsedCommandLine insensitive{};
	auto* insensitiveOptions = new tsoptions::ParsedOptions{};
	insensitiveOptions->ContentMappers = {mapper};
	insensitive.ParsedConfig = insensitiveOptions;
	insensitive.comparePathsOptions = tspath::ComparePathsOptions{
	    .useCaseSensitiveFileNames = false};
	tsoptions::ParsedCommandLine sensitive{};
	auto* sensitiveOptions = new tsoptions::ParsedOptions{};
	sensitiveOptions->ContentMappers = {mapper};
	sensitive.ParsedConfig = sensitiveOptions;
	sensitive.comparePathsOptions = tspath::ComparePathsOptions{
	    .useCaseSensitiveFileNames = true};

	assert::Equal(
	    t, insensitive.GetContentMapperForFileName("/src/Component.VUE"),
	    mapper);
	assert::Assert(
	    t, sensitive.GetContentMapperForFileName("/src/Component.VUE") ==
	           nullptr);
}

// TestGetOutputFileNamesExcludesMapperOwnedOutputs —
// contentmappers_test.go:52.
void TestGetOutputFileNamesExcludesMapperOwnedOutputs(T* t) {
	auto* mapper = new contentmapper::Mapper{
	    .Definition = {.Extensions = {".vue"}}};
	tsoptions::ParsedCommandLine* commandLine =
	    tsoptions::NewParsedCommandLine(
	        new tsc::CompilerOptions{
	            .OutDir = "/dist",
	            .Declaration = tsc::Tristate::True,
	            .DeclarationMap = tsc::Tristate::True,
	            .SourceMap = tsc::Tristate::True,
	        },
	        {"/src/Component.vue"}, {},
	        tspath::ComparePathsOptions{.currentDirectory = "/",
	                                    .useCaseSensitiveFileNames = true});
	commandLine->ParsedConfig->ContentMappers = {mapper};

	std::vector<std::string> outputs;
	commandLine->GetOutputFileNames(
	    [&](std::string_view outputName) {
		    outputs.emplace_back(outputName);
		    return true;
	    });
	assert::DeepEqual(t, outputs, std::vector<std::string>{"/dist/Component.d.vue.ts"});
}

// resolveContentMapperHost — contentmappers_test.go:15. Go's host is a
// ParseConfigHost whose FS() returns the embedded vfs.FS and whose
// GetCurrentDirectory() is "/home/project"; VfsParseConfigHost is exactly
// that shape.
// TestResolveContentMapperManifest — contentmappers_test.go:75.
void TestResolveContentMapperManifest(T* t) {
	tsoptions::tsoptionstest::VfsParseConfigHost host;
	host.Vfs = vfstest::FromMap(
	        {
	            {"/home/project/node_modules/vue-ts-mapper/package.json",
	             R"({
			"name": "vue-ts-mapper",
			"version": "1.2.3",
			"typescript": { "contentMapper": { "exec": ["node", "./dist/mapper.js"], "compilerOptions": ["target", "jsx"] } }
		})"},
	            {"/home/node_modules/@scope/noversion/package.json",
	             R"({
			"name": "@scope/noversion",
			"typescript": { "contentMapper": { "exec": ["run"] } }
		})"},
	            {"/home/project/node_modules/no-name/package.json",
	             R"({
			"version": "1.0.0"
		})"},
	            {"/home/project/node_modules/no-manifest/package.json",
	             R"({
			"name": "no-manifest"
		})"},
	            {"/home/project/node_modules/no-exec/package.json",
	             R"({
			"name": "no-exec",
			"typescript": { "contentMapper": {} }
		})"},
	            {"/home/project/node_modules/bad-exec/package.json",
	             R"({
			"name": "bad-exec",
			"typescript": { "contentMapper": { "exec": "node ./mapper.js" } }
		})"},
	        },
	        true);
	host.CurrentDirectory = "/home/project";

	// Name, version, and the verbatim exec argv are preserved.
	auto [manifest, packageDirectory, diagnostic] =
	    tsoptions::resolveContentMapperManifest(&host,
	                                            "/home/project/tsconfig.json",
	                                            "vue-ts-mapper");
	assert::Assert(t, diagnostic == nullptr);
	assert::Equal(t, manifest.Name, std::string("vue-ts-mapper"));
	assert::Equal(t, manifest.Version, std::string("1.2.3"));
	assert::Equal(t, packageDirectory,
	              std::string("/home/project/node_modules/vue-ts-mapper"));
	assert::DeepEqual(t, manifest.Exec,
	                  std::vector<std::string>{"node", "./dist/mapper.js"});
	assert::DeepEqual(t, manifest.CompilerOptions,
	                  std::vector<std::string>{"target", "jsx"});

	// Resolution walks up node_modules; a package with no version resolves to a name and empty version.
	auto [manifest2, _pd2, diagnostic2] =
	    tsoptions::resolveContentMapperManifest(
	        &host, "/home/project/src/tsconfig.json", "@scope/noversion");
	assert::Assert(t, diagnostic2 == nullptr);
	assert::Equal(t, manifest2.Name, std::string("@scope/noversion"));
	assert::Equal(t, manifest2.Version, std::string(""));

	// A package that is not installed reports a resolution diagnostic.
	auto [_m3, _pd3, diagnostic3] = tsoptions::resolveContentMapperManifest(
	    &host, "/home/project/tsconfig.json", "missing-mapper");
	assert::Assert(t, diagnostic3 != nullptr);
	assert::Equal(t, diagnostic3->Code(),
	              diagnostics::The_content_mapper_package_0_could_not_be_resolved
	                  ->code);

	// A package whose package.json has no name reports a diagnostic.
	auto [_m4, packageDirectory4, diagnostic4] =
	    tsoptions::resolveContentMapperManifest(
	        &host, "/home/project/tsconfig.json", "no-name");
	assert::Assert(t, diagnostic4 != nullptr);
	assert::Equal(t, packageDirectory4,
	              std::string("/home/project/node_modules/no-name"));
	assert::Equal(t, diagnostic4->Code(),
	              diagnostics::
	                  The_package_json_of_the_content_mapper_package_0_does_not_specify_a_name
	                      ->code);

	// A package that does not declare a "typescript.contentMapper" object reports a diagnostic.
	auto [_m5, _pd5, diagnostic5] = tsoptions::resolveContentMapperManifest(
	    &host, "/home/project/tsconfig.json", "no-manifest");
	assert::Assert(t, diagnostic5 != nullptr);
	assert::Equal(t, diagnostic5->Code(),
	              diagnostics::
	                  The_package_json_of_the_content_mapper_package_0_does_not_declare_a_typescript_contentMapper_object
	                      ->code);

	// A "typescript.contentMapper" with no "exec", or an "exec" of the wrong type, reports a diagnostic.
	for (const auto& pkg : {"no-exec", "bad-exec"}) {
		auto [_m6, _pd6, diagnostic6] =
		    tsoptions::resolveContentMapperManifest(
		        &host, "/home/project/tsconfig.json", pkg);
		assert::Assert(t, diagnostic6 != nullptr,
		               std::string("expected a diagnostic for ") + pkg);
		assert::Equal(t, diagnostic6->Code(),
		              diagnostics::
		                  The_typescript_contentMapper_exec_of_the_content_mapper_package_0_must_be_a_non_empty_array_of_strings
		                      ->code);
	}
}

}  // namespace

REGISTER_UNIT_TEST("tsoptions.TestGetContentMapperForFileNameUsesLongestExtension",
                   TestGetContentMapperForFileNameUsesLongestExtension);
REGISTER_UNIT_TEST("tsoptions.TestGetContentMapperForFileNameUsesHostCaseSensitivity",
                   TestGetContentMapperForFileNameUsesHostCaseSensitivity);
REGISTER_UNIT_TEST("tsoptions.TestGetOutputFileNamesExcludesMapperOwnedOutputs",
                   TestGetOutputFileNamesExcludesMapperOwnedOutputs);
REGISTER_UNIT_TEST("tsoptions.TestResolveContentMapperManifest",
                   TestResolveContentMapperManifest);
