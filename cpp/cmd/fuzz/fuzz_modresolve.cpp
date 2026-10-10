// fuzz_modresolve — libFuzzer driver for module specifier resolution.
//
// Exercises tsc/internal/module resolution end-to-end over an in-memory
// FS whose package.json files serve the fuzz bytes — the same path real
// auto-imports and `import "pkg/sub"` resolution take:
//   DefaultResolver::ResolveModuleName /
//   ResolveModuleNameFromDirectory / ResolveTypeReferenceDirective —
//   node_modules walks, package.json "exports"/"imports"/"typesVersions"
//   field handling, wildcard "*" pattern matching, self-name and
//   "#imports" lookups, extension probing.
//
// Input layout:
//   data[0]        — mode: picks ModuleResolution (Node10/16/Next/
//                    Bundler), resolution mode (ESM/CJS), specifier form,
//                    paths/customConditions/trace flags
//   data[1..2]     — LE length L of the specifier fragment
//   data[3..3+L)   — specifier fragment (joined under "pkg/", "#", "./",
//                    or used raw depending on mode)
//   data[3+L..]    — package.json content served at /repo/package.json,
//                    /repo/node_modules/pkg/package.json and
//                    /repo/node_modules/@scope/pkg/package.json
//
// The FS is bounded (fixed file set + the three package.json paths), so
// resolver walks terminate.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/core/types.h"
#include "internal/module/resolver.h"
#include "internal/module/types.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

#include "cmd/fuzz/fuzz_common.h"

namespace {

// resolutionHostStub mirrors tests_resolver.cpp — ResolutionHost over a
// vfs::FS.
struct resolutionHostStub : tsc::module::ResolutionHost {
	std::shared_ptr<tsc::vfs::FS> fs;
	std::string cwd;

	bool FileExists(std::string_view path) override {
		return fs->FileExists(std::string(path));
	}
	bool DirectoryExists(std::string_view path) override {
		return fs->DirectoryExists(std::string(path));
	}
	std::optional<std::string> ReadFile(std::string_view path) override {
		auto [content, ok] = fs->ReadFile(std::string(path));
		if (!ok) return std::nullopt;
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
		tsc::vfs::Entries e = fs->GetAccessibleEntries(std::string(path));
		return {std::move(e.files), std::move(e.directories),
		        std::move(e.symlinks)};
	}
};

void run(const uint8_t* data, size_t size) {
	uint8_t mode = data[0];
	size_t specLen = data[1] | (static_cast<size_t>(data[2]) << 8);
	size_t avail = size - 3;
	if (specLen > avail) specLen = avail;
	std::string spec(reinterpret_cast<const char*>(data + 3), specLen);
	std::string pkgJson(
	    reinterpret_cast<const char*>(data + 3 + specLen), avail - specLen);

	// Specifier form: bare pkg, pkg/subpath, #-imports, relative, or raw.
	std::string moduleName;
	switch ((mode >> 2) & 3) {
	case 0: moduleName = "pkg"; break;
	case 1: moduleName = "pkg/" + spec; break;
	case 2: moduleName = "#" + spec; break;
	default: moduleName = "./" + spec; break;
	}
	if (mode & 0x40) {
		moduleName = spec;
	}

	std::unordered_map<std::string, tsc::vfs::vfstest::MapFileInput> files;
	auto put = [&files](std::string path, std::string contents) {
		files.emplace(std::move(path), std::move(contents));
	};
	// The fuzz bytes are served as the package.json of the importer
	// (self-name / "#imports") and of the dependency (exports / main /
	// types / typesVersions).
	put("/repo/package.json", pkgJson);
	put("/repo/node_modules/pkg/package.json", pkgJson);
	put("/repo/node_modules/@scope/pkg/package.json", pkgJson);
	put("/repo/node_modules/pkg/index.d.ts", "export const x: number;");
	put("/repo/node_modules/pkg/index.js", "exports.x = 1;");
	put("/repo/node_modules/pkg/main.d.ts", "export const x: number;");
	put("/repo/node_modules/pkg/main.js", "exports.x = 1;");
	put("/repo/node_modules/pkg/sub/b.d.mts", "export const y: number;");
	put("/repo/node_modules/pkg/sub/c.d.cts", "export const z: number;");
	put("/repo/node_modules/pkg/features/y.d.ts",
	    "export const y: number;");
	put("/repo/node_modules/pkg/ts3.1/x.d.ts", "export const x: number;");
	put("/repo/index.d.ts", "export const r: number;");
	put("/repo/index.js", "exports.r = 1;");
	put("/repo/src/file.ts", "import 'x';");
	auto fs = tsc::vfs::vfstest::FromMap(std::move(files), true);

	resolutionHostStub host;
	host.fs = fs;
	host.cwd = "/repo";

	tsc::CompilerOptions opts;
	switch (mode & 3) {
	case 0:
		opts.ModuleResolution = tsc::ModuleResolutionKind::Bundler;
		opts.Module = tsc::ModuleKind::ESNext;
		break;
	case 1:
		opts.ModuleResolution = tsc::ModuleResolutionKind::NodeNext;
		opts.Module = tsc::ModuleKind::NodeNext;
		break;
	case 2:
		opts.ModuleResolution = tsc::ModuleResolutionKind::Node16;
		opts.Module = tsc::ModuleKind::Node16;
		break;
	default:
		opts.ModuleResolution = tsc::ModuleResolutionKind::Node10;
		opts.Module = tsc::ModuleKind::CommonJS;
		break;
	}
	opts.Target = tsc::ScriptTarget::ESNext;
	if (mode & 0x80) {
		opts.TraceResolution = tsc::Tristate::True;
	}
	// Path-mappings surface: fuzzed keys/values reach TryParsePatterns
	// wildcard handling.
	if (mode & 0x20) {
		opts.Paths = {
		    {"*", {"./node_modules/pkg/*"}},
		    {"pkg/*", {"./node_modules/pkg/*"}},
		    {spec, {"./index.d.ts"}},
		};
	}
	if (mode & 0x10) {
		opts.CustomConditions = {"browser", "development"};
	}

	tsc::module::DefaultResolver resolver(
	    tsc::module::ResolverOptions{.Host = &host,
	                                 .CompilerOptions = &opts});
	tsc::ResolutionMode resMode =
	    (mode & 4) ? tsc::ResolutionModeESM : tsc::ResolutionModeCommonJS;
	(void)resolver.ResolveModuleName(moduleName, "/repo/src/file.ts",
	                                 resMode, nullptr);
	(void)resolver.ResolveModuleNameFromDirectory(moduleName, "/repo/src",
	                                              resMode);
	(void)resolver.ResolveTypeReferenceDirective(spec, "/repo/src/file.ts",
	                                             resMode, nullptr);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size < 4) {
		return 0;
	}
	fuzz::runOnBigStack([](const uint8_t* d, size_t n) { run(d, n); }, data,
	                  size);
	return 0;
}
