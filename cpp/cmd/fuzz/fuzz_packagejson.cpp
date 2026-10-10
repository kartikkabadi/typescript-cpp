// fuzz_packagejson — libFuzzer driver for package.json content parsing.
//
// Feeds raw bytes through tsc/internal/packagejson — the port of
// packagejson.go's Parse (strict JSON decoding into Fields) — then
// exercises the downstream consumers that the module resolver hits on
// real package.json files:
//   PackageJson::GetVersionPaths   — "typesVersions" semver-range keys
//     matched against the compiler version, path-map extraction
//   ExportsOrImports::IsSubpaths / IsImports / IsConditions —
//     object-kind classification on "exports"/"imports" values
//   DependencyFields::RangeDependencies / GetRuntimeDependencyNames
//   VersionPaths::GetPaths       — lazy path-map materialization
//
// package.json is strict JSON (Go uses encoding/json), unlike tsconfig's
// JSONC — so this surface complements fuzz_tsconfig rather than
// duplicating it.

#include <cstdint>
#include <string>
#include <string_view>

#include "internal/packagejson/packagejson.h"

#include "cmd/fuzz/fuzz_common.h"

namespace {

void run(const uint8_t* data, size_t size) {
	std::string_view text(reinterpret_cast<const char*>(data), size);
	auto [fields, ok] = tsc::packagejson::Parse(text);
	if (!ok) {
		return;
	}

	// Exercise the consumer paths the module resolver drives over the
	// parsed fields.
	tsc::packagejson::PackageJson pj;
	static_cast<tsc::packagejson::Fields&>(pj) = std::move(fields);
	pj.GetVersionPaths([](const tsc::DiagnosticMessage*,
	                      const std::vector<std::string>&) {});
	if (pj.versionPaths.Exists()) {
		(void)pj.versionPaths.GetPaths();
	}
	(void)pj.Exports.IsSubpaths();
	(void)pj.Exports.IsImports();
	(void)pj.Exports.IsConditions();
	(void)pj.Imports.IsSubpaths();
	(void)pj.Imports.IsImports();
	(void)pj.Imports.IsConditions();
	pj.RangeDependencies(
	    [](const std::string&, const std::string&, const std::string&) {
		    return true;
	    });
	(void)pj.GetRuntimeDependencyNames();
	(void)pj.HasDependency("pkg");
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size == 0) {
		return 0;
	}
	fuzz::runOnBigStack([](const uint8_t* d, size_t n) { run(d, n); }, data,
	                  size);
	return 0;
}
