// tests_staticresolver.cpp — port of
// tsc/internal/module/staticresolver_test.go.
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/module/resolver.h"
#include "internal/module/staticresolver.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;

namespace {

std::shared_ptr<module::ResolvedModule> resolvedModule(
    const std::string& fileName) {
	auto m = std::make_shared<module::ResolvedModule>();
	m->ResolvedFileName = fileName;
	return m;
}

struct resolutionHostStub : module::ResolutionHost {
	std::shared_ptr<vfs::FS> fs;
	std::string cwd;

	resolutionHostStub(std::shared_ptr<vfs::FS> fs, std::string cwd)
	    : fs(std::move(fs)), cwd(std::move(cwd)) {}

	bool FileExists(std::string_view path) override {
		return fs->FileExists(std::string(path));
	}
	bool DirectoryExists(std::string_view path) override {
		return fs->DirectoryExists(std::string(path));
	}
	std::optional<std::string> ReadFile(std::string_view path) override {
		auto [content, ok] = fs->ReadFile(std::string(path));
		if (!ok) {
			return std::nullopt;
		}
		return content;
	}
	std::string Realpath(std::string_view path) override {
		return fs->Realpath(std::string(path));
	}
	std::string GetCurrentDirectory() override { return cwd; }
	bool UseCaseSensitiveFileNames() override {
		return fs->UseCaseSensitiveFileNames();
	}
	AccessibleEntries GetAccessibleEntries(
	    std::string_view path) override {
		vfs::Entries e = fs->GetAccessibleEntries(std::string(path));
		return {std::move(e.files), std::move(e.directories),
		        std::move(e.symlinks)};
	}
};

void TestStaticResolver(T* t) {
	t->Parallel();

	auto fs = vfs::vfstest::FromMap(
	    {
	        {"/repo/node_modules/fallback/package.json",
	         R"({"name":"fallback","types":"index.d.ts"})"},
	        {"/repo/node_modules/fallback/index.d.ts", "export {};"},
	    },
	    true);
	auto* host = new resolutionHostStub(fs, "/repo");
	auto* opts = new CompilerOptions();
	opts->Module = ModuleKind::ESNext;
	opts->ModuleResolution = ModuleResolutionKind::Bundler;
	auto* fallback =
	    module::NewResolver(module::ResolverOptions{
		    .Host = host,
		    .CompilerOptions = opts,
	    });
	ResolutionMode esm = ResolutionModeESM;
	auto* resolutions = module::NewStaticResolutions(
	    {
	        {/*.ModuleName*/ "provided", "", nullptr,
	         resolvedModule(R"(/global.d.ts)")},
	        {/*.ModuleName*/ "provided", "/repo/src", nullptr,
	         resolvedModule(R"(/directory.d.ts)")},
	        {/*.ModuleName*/ "provided", "", &esm,
	         resolvedModule(R"(/esm.d.ts)")},
	        {/*.ModuleName*/ "provided", "/repo/src", &esm,
	         resolvedModule(R"(/directory-esm.d.ts)")},
	        {/*.ModuleName*/ "unresolved"},
	    },
	    true, "/repo", true);
	assert::Assert(t, resolutions != nullptr);
	auto* resolver = module::NewStaticResolver(fallback, resolutions);

	struct {
		const char* name;
		const char* containingFile;
		ResolutionMode mode;
		const char* resolvedFileName;
	} tests[] = {
	    {"provided", "/repo/src/index.ts", ResolutionModeESM,
	     "/directory-esm.d.ts"},
	    {"provided", "/repo/src/index.ts", ResolutionModeCommonJS,
	     "/directory.d.ts"},
	    {"provided", "/repo/other/index.ts", ResolutionModeESM,
	     "/esm.d.ts"},
	    {"provided", "/repo/other/index.ts", ResolutionModeCommonJS,
	     "/global.d.ts"},
	    {"fallback", "/repo/src/index.ts", ResolutionModeESM,
	     "/repo/node_modules/fallback/index.d.ts"},
	    {"unresolved", "/repo/src/index.ts", ResolutionModeESM, ""},
	};
	for (auto& test : tests) {
		auto [result, _] = resolver->ResolveModuleName(
		    test.name, test.containingFile, test.mode, nullptr);
		if (*test.resolvedFileName == '\0') {
			assert::Assert(t, result == nullptr);
		} else {
			assert::Equal(t, result->ResolvedFileName,
			              std::string(test.resolvedFileName));
		}
	}
}

} // namespace

REGISTER_UNIT_TEST("module.TestStaticResolver", TestStaticResolver);
