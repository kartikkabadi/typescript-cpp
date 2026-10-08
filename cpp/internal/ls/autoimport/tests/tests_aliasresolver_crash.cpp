// Port of tsc/internal/ls/autoimport/aliasresolver_crash_test.go.
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/binder/binder.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/module/resolver.h"
#include "internal/module/types.h"
#include "internal/packagejson/packagejson.h"
#include "internal/parser/parser.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
using namespace tsc::ls::autoimport;
using namespace tsc;

namespace {

struct fakeCloneHost : RegistryCloneHost {
	std::shared_ptr<vfs::FS> fs;

	std::shared_ptr<vfs::FS> FS() override { return fs; }
	std::string GetCurrentDirectory() override { return "/"; }
	std::pair<ProjectID*, compiler::SimpleProgram*> GetDefaultProject(
	    const tspath::Path&) override {
		return {nullptr, nullptr};
	}
	compiler::SimpleProgram* GetProgramForProject(
	    ProjectID*) override {
		return nullptr;
	}
	std::shared_ptr<packagejson::InfoCacheEntry> GetPackageJson(
	    const std::string&) override {
		return nullptr;
	}
	SourceFile* GetSourceFile(const std::string&,
	                          const tspath::Path&) override {
		return nullptr;
	}
	void Dispose() override {}

	// module::ResolutionHost — Go's interface is just FS() +
	// GetCurrentDirectory(); the C++ port widened it, so the remaining
	// methods delegate to fs like resolutionHost does.
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

}  // namespace

// Regression test for microsoft/typescript-go#4322.
//
// During auto-import export extraction, the checker is built on top of an
// aliasResolver standing in for a real program. This file has a type error,
// and extracting exports should still complete without crashing.
static void TestAliasResolverGetDiagnosticsDoesNotPanic(T* t) {
	t->Parallel();

	const std::string fileName = "/pkg/index.ts";
	std::string text =
	    "declare function f(arg: { a: string }): () => void;\n"
	    "export const x = f({ a: 1 });\n";

	auto fs = vfs::vfstest::FromMap({{fileName, text}},
	                                true /*useCaseSensitiveFileNames*/);
	fakeCloneHost host;
	host.fs = fs;

	auto* sourceFile = parseSourceFile(
	    SourceFileParseOptions{.FileName = fileName,
	                           .Path = tspath::Path(fileName)},
	    text, ScriptKind::TS);
	bindSourceFile(sourceFile);

	CompilerOptions compilerOptions{};
	auto* resolver = module::NewResolver(module::ResolverOptions{
	    .Host = &host, .CompilerOptions = &compilerOptions});
	auto r = newAliasResolver(
	    std::vector<SourceFile*>{sourceFile},
	    {} /* symlinks */, &host, resolver,
	    [](const std::string& f) -> tspath::Path {
		    return tspath::Path(f);
	    },
	    [](SourceFile*, const std::string&) {});

	checker::Checker ch;
	ch.init(r.get());

	// Type-checking this file's diagnostics must not panic.
	ch.GetDiagnostics(sourceFile);
}
REGISTER_UNIT_TEST("ls/autoimport.TestAliasResolverGetDiagnosticsDoesNotPanic",
                   TestAliasResolverGetDiagnosticsDoesNotPanic);
