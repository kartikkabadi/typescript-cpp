// tests_buildinfo_contentmapper.cpp — port of
// tsc/internal/execute/incremental/buildinfo_contentmapper_test.go.
#include <memory>
#include <string>
#include <vector>

#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/utilities.h"
#include "internal/execute/incremental/incremental.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc {
namespace {
using namespace ::tsc::execute::incremental;
namespace incremental = ::tsc::execute::incremental;
namespace vfstest = ::tsc::vfs::vfstest;


using gostd::testing::T;

tsoptions::ParsedCommandLine*
configWithMappers(std::vector<contentmapper::Mapper*> mappers) {
	auto* opts = new tsoptions::ParsedCommandLine();
	opts->ParsedConfig = new tsoptions::ParsedOptions();
	opts->ParsedConfig->CompilerOptions = new CompilerOptions();
	opts->ParsedConfig->ContentMappers = std::move(mappers);
	return opts;
}

void TestStaticContentMapperTransformIdentity(T* t) {
	t->Parallel();

	contentmapper::Mapper m1;
	m1.Manifest.Name = "vue";
	m1.Manifest.Version = "2.0.0";
	if (m1.Identity() != "vue@2.0.0")
		t->Error({gostd::sprintf("Identity() = %s, want vue@2.0.0",
		                         {m1.Identity()})});
	contentmapper::Mapper m2;
	m2.Definition.Package = "anon";
	if (m2.Identity() != "")
		t->Error({gostd::sprintf("Identity() = %s, want empty",
		                         {m2.Identity()})});

	auto* jsxMapper = new contentmapper::Mapper();
	jsxMapper->Definition.Package = "jsx";
	jsxMapper->Manifest.Name = "jsx";
	jsxMapper->Manifest.Version = "1.0.0";
	jsxMapper->Manifest.CompilerOptions = {"jsx"};
	CompilerOptions optsPreserve;
	optsPreserve.Jsx = JsxEmit::Preserve;
	CompilerOptions optsReact;
	optsReact.Jsx = JsxEmit::React;
	auto jsxPreserveIdentity = jsxMapper->TransformIdentity(&optsPreserve);
	auto jsxReactIdentity = jsxMapper->TransformIdentity(&optsReact);
	if (jsxPreserveIdentity == jsxReactIdentity)
		t->Error(
		    {"expected different identities for preserve vs react jsx"});

	auto* optionsA = new contentmapper::Mapper();
	optionsA->Definition.Package = "vue";
	optionsA->Definition.Options =
	    json::Value(std::string(R"({"mode":"a"})"));
	optionsA->Manifest.Name = "vue";
	optionsA->Manifest.Version = "1.0.0";
	auto* optionsB = new contentmapper::Mapper();
	optionsB->Definition.Package = "vue";
	optionsB->Definition.Options =
	    json::Value(std::string(R"({"mode":"b"})"));
	optionsB->Manifest.Name = "vue";
	optionsB->Manifest.Version = "1.0.0";
	CompilerOptions emptyOpts;
	if (optionsA->TransformIdentity(&emptyOpts) ==
	    optionsB->TransformIdentity(&emptyOpts))
		t->Error({"expected different identities for different options"});
}
REGISTER_UNIT_TEST("incremental.TestStaticContentMapperTransformIdentity",
                   TestStaticContentMapperTransformIdentity);

struct fakeBuildInfoReader : BuildInfoReader {
	BuildInfo* buildInfo;
	BuildInfo*
	ReadBuildInfo(tsoptions::ParsedCommandLine* config) override {
		return buildInfo;
	}
};

struct fakeContentMapperProject : contentmapper::Project {
	std::vector<std::string> identities;
	gostd::Error err;

	gostd::Error Refresh() override { return gostd::Error(); }
	std::pair<std::vector<std::string>, gostd::Error> Identities() override {
		return {identities, err};
	}
	std::pair<std::string, gostd::Error>
	Identity(contentmapper::Mapper* mapper) override {
		return {"", gostd::Error()};
	}
	std::pair<std::vector<std::string>, gostd::Error>
	WatchedFiles() override {
		return {std::vector<std::string>{}, gostd::Error()};
	}
	std::vector<contentmapper::OptionDiagnostic> Diagnostics() override {
		return {};
	}
	std::pair<contentmapper::Result, gostd::Error>
	Transform(contentmapper::Mapper* mapper,
	          const contentmapper::Request& request) override {
		return {contentmapper::Result{}, gostd::Error()};
	}
	gostd::Error Close() override { return gostd::Error(); }
};

void TestDynamicContentMapperIdentities(T* t) {
	t->Parallel();
	auto* mapper = new contentmapper::Mapper();
	mapper->Definition.Package = "dynamic";
	mapper->Manifest.Name = "dynamic";
	mapper->Manifest.Version = "1.0.0";
	mapper->Manifest.DynamicConfig = true;
	auto* config = configWithMappers({mapper});
	auto* project = new fakeContentMapperProject();
	project->identities = {"dynamic@1.0.0:opaque"};
	auto [identities, err] = ContentMapperIdentities(project);
	if (err)
		t->Error({gostd::sprintf("unexpected error: %s", {err->Error()})});
	if (identities != project->identities)
		t->Error({"expected identities to equal project.identities"});

	auto* buildInfo = new incremental::BuildInfo();
	buildInfo->Version = Version();
	buildInfo->FileNames = {"/src/a.ts"};
	buildInfo->ContentMapperIdentities = {"dynamic@1.0.0:old"};
	auto* host = compiler::NewCompilerHost(
	    "/", vfstest::FromMap({}, true), "", nullptr, nullptr,
	    std::shared_ptr<contentmapper::Project>(project));
	auto* reader = new fakeBuildInfoReader();
	reader->buildInfo = buildInfo;
	auto* program =
	    incremental::ReadBuildInfoProgram(config, reader, host);
	if (program != nullptr)
		t->Error(
		    {"expected opaque mapper identity changes to discard the old "
		     "program"});
}
REGISTER_UNIT_TEST("incremental.TestDynamicContentMapperIdentities",
                   TestDynamicContentMapperIdentities);

void TestContentMapperIdentityError(T* t) {
	t->Parallel();
	auto want = gostd::newError("identity failed");
	auto [identities, err] =
	    ContentMapperIdentities([&want] {
		auto* p = new fakeContentMapperProject();
		p->err = want;
		return p;
	}());
	if (!identities.empty())
		t->Error({"expected nil identities"});
	if (!gostd::errorIs(err, want))
		t->Errorf("error %v does not match %v", {err, want});
}
REGISTER_UNIT_TEST("incremental.TestContentMapperIdentityError",
                   TestContentMapperIdentityError);

void TestReadBuildInfoProgramContentMapperIdentityMismatch(T* t) {
	t->Parallel();

	// An otherwise-valid, incremental build info whose recorded mapper
	// identity differs from the current project cannot be reused: the old
	// program is discarded (nil) so the project is rebuilt.
	auto* buildInfo = new incremental::BuildInfo();
	buildInfo->Version = Version();
	buildInfo->FileNames = {"/src/a.ts"};
	buildInfo->ContentMapperIdentities = {"vue@1.0.0"};
	auto* mapper = new contentmapper::Mapper();
	mapper->Definition.Package = "vue";
	mapper->Definition.Extensions = {".vue"};
	mapper->Manifest.Name = "vue";
	mapper->Manifest.Version = "2.0.0";
	auto* config = configWithMappers({mapper});
	auto* project = new fakeContentMapperProject();
	project->identities = {"vue@2.0.0:current"};
	auto* host = compiler::NewCompilerHost(
	    "/", vfstest::FromMap({}, true), "", nullptr, nullptr,
	    std::shared_ptr<contentmapper::Project>(project));

	auto* reader = new fakeBuildInfoReader();
	reader->buildInfo = buildInfo;
	auto* program =
	    incremental::ReadBuildInfoProgram(config, reader, host);
	if (program != nullptr)
		t->Error({"expected the old program to be discarded when the "
		          "mapper identity changed"});
}
REGISTER_UNIT_TEST(
    "incremental.TestReadBuildInfoProgramContentMapperIdentityMismatch",
    TestReadBuildInfoProgramContentMapperIdentityMismatch);

} // namespace
} // namespace tsc
