// tests_extendedconfigcache.cpp — port of
// tsc/internal/execute/tsc/extendedconfigcache_test.go.
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "internal/execute/tsc/extendedconfigcache.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc {
namespace {
using namespace ::tsc::execute::tsc;
namespace vfstest = ::tsc::vfs::vfstest;


using gostd::testing::T;

// testParseConfigHost — Go's {fs vfs.FS, cwd string}.
struct testParseConfigHost : tsoptions::ParseConfigHost {
	std::shared_ptr<vfs::FS> fs;
	std::string cwd;

	testParseConfigHost(std::shared_ptr<vfs::FS> fs, std::string cwd)
	    : fs(std::move(fs)), cwd(std::move(cwd)) {}

	bool FileExists(std::string_view path) override {
		return fs->FileExists(std::string(path));
	}
	bool DirectoryExists(std::string_view path) override {
		return fs->DirectoryExists(std::string(path));
	}
	std::optional<std::string> ReadFile(std::string_view path) override {
		auto [text, ok] = fs->ReadFile(std::string(path));
		if (!ok)
			return std::nullopt;
		return text;
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
		auto e = fs->GetAccessibleEntries(std::string(path));
		return {std::move(e.files), std::move(e.directories),
		        std::move(e.symlinks)};
	}
};

void assertHasCircularityDiagnostic(T* t,
                                    tsoptions::ParsedCommandLine* cmd) {
	t->Helper();
	for (auto* d : cmd->Errors) {
		if (d != nullptr && d->Code() == 18000) {
			return;
		}
	}
	t->Error(
	    {"expected circularity diagnostic (code 18000), but none was "
	     "found"});
}

void TestExtendedConfigCacheExtendsCircularity(T* t) {
	t->Parallel();

	t->Run("self-referencing extends", [](T* t) {
		t->Parallel();

		// Regression test: a tsconfig extends cycle should produce an
		// error, not a deadlock when using the tsc ExtendedConfigCache.
		std::unordered_map<std::string, vfstest::MapFileInput> files{
		    {"/project/tsconfig.json",
		     std::string(R"({"extends": "./base.json"})")},
		    {"/project/base.json",
		     std::string(R"({"extends": "./base.json"})")},
		    {"/project/main.ts", "// Hello World!"},
		};

		auto fs = vfstest::FromMap(files, false);
		auto* host = new testParseConfigHost(fs, "/project");
		auto* cache = new ExtendedConfigCache();

		auto [cmd, errors] = tsoptions::GetParsedCommandLineOfConfigFile(
		    "/project/tsconfig.json", nullptr, {}, host, cache);
		if (cmd == nullptr) {
			t->Fatal({"expected non-nil ParsedCommandLine"});
		}
		assertHasCircularityDiagnostic(t, cmd);
	});

	t->Run("mutual extends cycle", [](T* t) {
		t->Parallel();

		// Two config files that extend each other.
		std::unordered_map<std::string, vfstest::MapFileInput> files{
		    {"/project/tsconfig.json",
		     std::string(R"({"extends": "./other.json"})")},
		    {"/project/other.json",
		     std::string(R"({"extends": "./tsconfig.json"})")},
		    {"/project/main.ts", "// Hello World!"},
		};

		auto fs = vfstest::FromMap(files, false);
		auto* host = new testParseConfigHost(fs, "/project");
		auto* cache = new ExtendedConfigCache();

		auto [cmd, errors] = tsoptions::GetParsedCommandLineOfConfigFile(
		    "/project/tsconfig.json", nullptr, {}, host, cache);
		if (cmd == nullptr) {
			t->Fatal({"expected non-nil ParsedCommandLine"});
		}
		assertHasCircularityDiagnostic(t, cmd);
	});

	t->Run("case-insensitive self-referencing extends", [](T* t) {
		t->Parallel();

		// On a case-insensitive FS, ./Base.json and ./base.json resolve
		// to the same cache entry. The cycle check must use canonical
		// paths to avoid deadlock.
		std::unordered_map<std::string, vfstest::MapFileInput> files{
		    {"/project/tsconfig.json",
		     std::string(R"({"extends": "./Base.json"})")},
		    {"/project/base.json",
		     std::string(R"({"extends": "./base.json"})")},
		    {"/project/main.ts", "// Hello World!"},
		};

		auto fs = vfstest::FromMap(files, false);
		auto* host = new testParseConfigHost(fs, "/project");
		auto* cache = new ExtendedConfigCache();

		auto [cmd, errors] = tsoptions::GetParsedCommandLineOfConfigFile(
		    "/project/tsconfig.json", nullptr, {}, host, cache);
		if (cmd == nullptr) {
			t->Fatal({"expected non-nil ParsedCommandLine"});
		}
		assertHasCircularityDiagnostic(t, cmd);
	});
}
REGISTER_UNIT_TEST("tsc.TestExtendedConfigCacheExtendsCircularity",
                   TestExtendedConfigCacheExtendsCircularity);

void TestExtendedConfigCacheNullExtendsDoesNotPanic(T* t) {
	t->Parallel();

	std::unordered_map<std::string, vfstest::MapFileInput> files{
	    {"/project/tsconfig.json", std::string(R"({"extends": null})")},
	    {"/project/main.ts", "// Hello World!"},
	};

	auto fs = vfstest::FromMap(files, false);
	auto* host = new testParseConfigHost(fs, "/project");
	auto* cache = new ExtendedConfigCache();

	auto [cmd, errors] = tsoptions::GetParsedCommandLineOfConfigFile(
	    "/project/tsconfig.json", nullptr, {}, host, cache);
	if (cmd == nullptr) {
		t->Fatal({"expected non-nil ParsedCommandLine"});
	}
	if (cmd->Errors.empty()) {
		t->Fatal({"expected diagnostics for invalid null extends"});
	}
}
REGISTER_UNIT_TEST("tsc.TestExtendedConfigCacheNullExtendsDoesNotPanic",
                   TestExtendedConfigCacheNullExtendsDoesNotPanic);

} // namespace
} // namespace tsc
