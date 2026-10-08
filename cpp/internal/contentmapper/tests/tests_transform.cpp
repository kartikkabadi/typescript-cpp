// tests_transform.cpp — port of tsc/internal/contentmapper/transform_test.go.
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/spanmap/spanmap.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace cm = tsc::contentmapper;
using namespace tsc;

namespace {

cm::Mapper* extensionsMapper(std::vector<std::string> extensions) {
	auto* m = new cm::Mapper();
	m->Definition.Extensions = std::move(extensions);
	m->Manifest.Name = "mapper";
	return m;
}

void TestParseResultSupplementalFileExtensions(T* t) {
	t->Parallel();
	auto* mappings = spanmap::New({});
	cm::Result result;
	result.VirtualExtension = ".ts";
	result.Mappings = mappings;
	result.Supplemental = {
	    {"", ".js", mappings},
	    {"", ".jsx", mappings},
	    {"", ".ts", mappings},
	    {"", ".tsx", mappings},
	    {"", ".mts", mappings},
	    {"", ".cts", mappings},
	    {"", ".json", mappings},
	};
	auto [files, err] = cm::ParseResult(
	    {.FileName = "/component.astro", .Path = "/component.astro"},
	    "", extensionsMapper({".astro"}), "transform-identity", result);
	assert::NilError(t, err);
	assert::Equal(t, files.Canonical->ContentMapperTransformIdentity(),
	              std::string("transform-identity"));
	auto* canonicalSupplementals = files.Canonical->SupplementalSourceFiles();
	assert::Equal(t, canonicalSupplementals->size(),
	              files.Supplemental.size());

	struct {
		const char* fileName;
		ScriptKind scriptKind;
	} expected[] = {
	    {"/component.astro.0.js", ScriptKind::JS},
	    {"/component.astro.1.jsx", ScriptKind::JSX},
	    {"/component.astro.2.ts", ScriptKind::TS},
	    {"/component.astro.3.tsx", ScriptKind::TSX},
	    {"/component.astro.4.mts", ScriptKind::TS},
	    {"/component.astro.5.cts", ScriptKind::TS},
	    {"/component.astro.6.json", ScriptKind::JSON},
	};
	assert::Equal(t, files.Supplemental.size(),
	              sizeof(expected) / sizeof(expected[0]));
	for (size_t i = 0; i < files.Supplemental.size(); i++) {
		assert::Equal(t, files.Supplemental[i]->FileName(),
		              std::string(expected[i].fileName));
		assert::Equal(t, files.Supplemental[i]->Path(),
		              tspath::Path(expected[i].fileName));
		assert::Assert(t, files.Supplemental[i]->ScriptKind ==
		                  expected[i].scriptKind);
		assert::Equal(
		    t, files.Supplemental[i]->ContentMapperTransformIdentity(),
		    std::string("transform-identity"));
		assert::Assert(t, (*canonicalSupplementals)[i] ==
		                  files.Supplemental[i]);
		assert::Assert(t, files.Supplemental[i]->CanonicalSourceFile() ==
		                  files.Canonical);
	}
}

void TestParseResultAllowsSupplementalModules(T* t) {
	t->Parallel();
	auto* mappings = spanmap::New({});
	cm::Result result;
	result.Text = "export {};";
	result.VirtualExtension = ".ts";
	result.Mappings = mappings;
	cm::MappedResult supp;
	supp.Text = "export const value = 1;";
	supp.VirtualExtension = ".mts";
	supp.Mappings = mappings;
	result.Supplemental = {supp};
	auto [files, err] = cm::ParseResult(
	    {.FileName = "/component.astro", .Path = "/component.astro"},
	    "", extensionsMapper({".astro"}), "", result);
	assert::NilError(t, err);
	assert::Assert(t, isExternalModule(files.Supplemental[0]));
}

void TestParseResultDoesNotLeakCanonicalModuleForcingToSupplementals(
    T* t) {
	t->Parallel();
	auto* mappings = spanmap::New({});
	cm::Result result;
	result.Text = "const canonical = 1;";
	result.VirtualExtension = ".mts";
	result.Mappings = mappings;
	cm::MappedResult supp;
	supp.Text = "const supplemental = 1;";
	supp.VirtualExtension = ".ts";
	supp.Mappings = mappings;
	result.Supplemental = {supp};
	auto* m = new cm::Mapper();
	m->Manifest.Name = "mapper";
	auto [files, err] = cm::ParseResult(
	    {.FileName = "/component.astro", .Path = "/component.astro"},
	    "", m, "", result);
	assert::NilError(t, err);
	assert::Assert(t, files.Canonical->ParseOptions()
	                      .ExternalModuleIndicatorOptions.Force);
	assert::Assert(t, !files.Supplemental[0]->ParseOptions()
	                       .ExternalModuleIndicatorOptions.Force);
}

} // namespace

REGISTER_UNIT_TEST("contentmapper.TestParseResultSupplementalFileExtensions",
                   TestParseResultSupplementalFileExtensions);
REGISTER_UNIT_TEST("contentmapper.TestParseResultAllowsSupplementalModules",
                   TestParseResultAllowsSupplementalModules);
REGISTER_UNIT_TEST(
    "contentmapper.TestParseResultDoesNotLeakCanonicalModuleForcingToSupplementals",
    TestParseResultDoesNotLeakCanonicalModuleForcingToSupplementals);
