// Port of tsc/internal/lsp/server_contentmapper_internal_test.go
// (package lsp — exercises unexported internals).
#include <memory>
#include <string>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsp.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::lsp {

namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;

// strings.Builder analog writing into a std::string.
struct stringBuilderWriter : gostd::io::Writer {
	std::string s;
	std::pair<int, gostd::Error> write(std::string_view data) override {
		s.append(data);
		return {(int)data.size(), nullptr};
	}
};

} // namespace

// TestContentMapperLoggerRequiresTrace — server_contentmapper_internal_test.go:10.
void TestContentMapperLoggerRequiresTrace(T* t) {
	t->Parallel();
	stringBuilderWriter output;
	Server server(ServerOptions{.Err = &output,
	                            .Cwd = "/",
	                            .FS = vfs::vfstest::FromMap({}, false)});
	server.logger = std::make_shared<logger>(&server);
	auto cmlogger = server.contentMapperLogger();
	cmlogger("hidden");
	assert::Equal(t, output.s, std::string(""));
	server.logger->SetVerbosity(lsproto::LogVerbosityTrace);
	cmlogger("visible");
	assert::Assert(t, output.s.find("visible") != std::string::npos);
}
REGISTER_UNIT_TEST("lsp.TestContentMapperLoggerRequiresTrace",
                   TestContentMapperLoggerRequiresTrace);

// TestParseContentMapperContributions — server_contentmapper_internal_test.go:21.
void TestParseContentMapperContributions(T* t) {
	t->Parallel();
	std::string version = "2.3.4";
	std::string cwd = "/workspace/mapper";
	auto compilerOptions = std::make_shared<lsproto::Slice<std::string>>(
	    std::vector<std::string>{"strict"});
	auto options = std::make_shared<lsproto::Map<std::string, lsproto::LSPAny>>(
	    lsproto::Map<std::string, lsproto::LSPAny>{{"mode", "embedded"}});

	auto contrib1 = std::make_shared<lsproto::ContentMapperContribution>();
	contrib1->ContributorId = "publisher.extension";
	contrib1->Extensions = {".vue"};
	contrib1->InferredProjectContribution =
	    std::make_shared<lsproto::InferredProjectContentMapperContribution>();
	contrib1->InferredProjectContribution->Options = options;
	contrib1->InferredProjectContribution->Manifest =
	    std::make_shared<lsproto::ContentMapperManifest>();
	contrib1->InferredProjectContribution->Manifest->Name = "Vue mapper";
	contrib1->InferredProjectContribution->Manifest->Version = version;
	contrib1->InferredProjectContribution->Manifest->Exec = {"node",
	                                                       "mapper.js"};
	contrib1->InferredProjectContribution->Manifest->Cwd = cwd;
	contrib1->InferredProjectContribution->Manifest->CompilerOptions =
	    compilerOptions;

	auto contrib2 = std::make_shared<lsproto::ContentMapperContribution>();
	contrib2->ContributorId = "publisher.extension";
	contrib2->Extensions = {".svelte"};

	auto [contributions, err] =
	    parseContentMapperContributions({contrib1, contrib2});
	assert::NilError(t, err);
	assert::Equal(t, (int)contributions.Mappers.size(), 1);
	assert::DeepEqual(t, contributions.Extensions,
	                  std::vector<std::string>{".vue"});
	auto* mapper = contributions.Mappers[0];
	assert::Equal(t, mapper->Identity(),
	              std::string("publisher.extension[0] (Vue mapper@2.3.4)"));
	assert::Equal(t, mapper->PackageDirectory, cwd);
	assert::Equal(t, std::string(mapper->Definition.Options),
	              std::string("{\"mode\":\"embedded\"}"));
}
REGISTER_UNIT_TEST("lsp.TestParseContentMapperContributions",
                   TestParseContentMapperContributions);

// TestParseContentMapperContributionsRejectsConflictingInlineMappers —
// server_contentmapper_internal_test.go:59.
void TestParseContentMapperContributionsRejectsConflictingInlineMappers(T* t) {
	t->Parallel();
	auto inferredProjectContribution =
	    [](const std::string& name)
	    -> std::shared_ptr<lsproto::InferredProjectContentMapperContribution> {
		auto c = std::make_shared<
		    lsproto::InferredProjectContentMapperContribution>();
		c->Manifest = std::make_shared<lsproto::ContentMapperManifest>();
		c->Manifest->Name = name;
		c->Manifest->Exec = {name};
		return c;
	};
	auto mk = [&](const std::string& id, const std::string& ext,
	              const std::string& name) {
		auto c = std::make_shared<lsproto::ContentMapperContribution>();
		c->ContributorId = id;
		c->Extensions = {ext};
		c->InferredProjectContribution = inferredProjectContribution(name);
		return c;
	};
	auto [_, err] = parseContentMapperContributions(
	    {mk("first", ".vue", "first"), mk("second", ".vue", "second")});
	assert::ErrorContains(t, err, "both claim extension \".vue\"");
}
REGISTER_UNIT_TEST(
    "lsp.TestParseContentMapperContributionsRejectsConflictingInlineMappers",
    TestParseContentMapperContributionsRejectsConflictingInlineMappers);

// TestParseContentMapperContributionsUsesCaseInsensitiveExtensions —
// server_contentmapper_internal_test.go:73.
void TestParseContentMapperContributionsUsesCaseInsensitiveExtensions(T* t) {
	t->Parallel();
	auto inferredProjectContribution =
	    [](const std::string& name)
	    -> std::shared_ptr<lsproto::InferredProjectContentMapperContribution> {
		auto c = std::make_shared<
		    lsproto::InferredProjectContentMapperContribution>();
		c->Manifest = std::make_shared<lsproto::ContentMapperManifest>();
		c->Manifest->Name = name;
		c->Manifest->Exec = {name};
		return c;
	};
	auto mk = [&](const std::string& id, const std::string& ext,
	              const std::string& name) {
		auto c = std::make_shared<lsproto::ContentMapperContribution>();
		c->ContributorId = id;
		c->Extensions = {ext};
		c->InferredProjectContribution = inferredProjectContribution(name);
		return c;
	};
	auto [_, err] = parseContentMapperContributions(
	    {mk("first", ".vue", "first"), mk("second", ".VUE", "second")});
	assert::ErrorContains(t, err, "both claim extension \".VUE\"");

	auto builtin = std::make_shared<lsproto::ContentMapperContribution>();
	builtin->ContributorId = "built-in";
	builtin->Extensions = {".TS"};
	auto [_2, err2] = parseContentMapperContributions({builtin});
	assert::ErrorContains(t, err2, "invalid extension \".TS\"");
}
REGISTER_UNIT_TEST(
    "lsp.TestParseContentMapperContributionsUsesCaseInsensitiveExtensions",
    TestParseContentMapperContributionsUsesCaseInsensitiveExtensions);

// TestParseContentMapperContributionsDefaultsOptionsToObject —
// server_contentmapper_internal_test.go:91.
void TestParseContentMapperContributionsDefaultsOptionsToObject(T* t) {
	t->Parallel();
	auto contrib = std::make_shared<lsproto::ContentMapperContribution>();
	contrib->ContributorId = "publisher.extension";
	contrib->Extensions = {".vue"};
	contrib->InferredProjectContribution =
	    std::make_shared<lsproto::InferredProjectContentMapperContribution>();
	contrib->InferredProjectContribution->Manifest =
	    std::make_shared<lsproto::ContentMapperManifest>();
	contrib->InferredProjectContribution->Manifest->Name = "mapper";
	contrib->InferredProjectContribution->Manifest->Exec = {"mapper"};

	auto [contributions, err] = parseContentMapperContributions({contrib});
	assert::NilError(t, err);
	assert::Equal(t,
	              std::string(contributions.Mappers[0]->Definition.Options),
	              std::string("{}"));
}
REGISTER_UNIT_TEST("lsp.TestParseContentMapperContributionsDefaultsOptionsToObject",
                   TestParseContentMapperContributionsDefaultsOptionsToObject);

} // namespace tsc::lsp
