// fuzz_modspec — libFuzzer driver for the modulespecifiers util surface.
//
// Auto-import plumbing turns user-controlled strings (import specifiers,
// autoImportSpecifierExcludeRegexes preferences, node_modules paths) into
// regexes and path-part decomposition. Driver feeds raw bytes into the
// exported util functions in modulespecifiers/types.h + util.cpp:
//
//   IsExcludedByRegex (specifier + fuzzed regex patterns — stringToRegex
//     RE2-parity path), PathIsBareSpecifier, GetNodeModulePathParts,
//     GetNodeModulesPackageName, replaceFirstStar, comparePathsByRedirect-
//     adjacent helpers, packageJsonPathsAreEqual, trimSuffix.
//
// Layout: split at first NUL — a = specifier/path, b = rest. Patterns
// for IsExcludedByRegex come from splitting b on '\n'.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "internal/modulespecifiers/types.h"

#include "cmd/fuzz/fuzz_common.h"

namespace {

void run(const uint8_t* data, size_t size) {
	std::string_view rest(reinterpret_cast<const char*>(data), size);
	size_t nul = rest.find('\0');
	std::string a(rest.substr(0, nul == std::string_view::npos
	                                 ? rest.size()
	                                 : nul));
	std::string_view b =
	    nul == std::string_view::npos ? rest : rest.substr(nul + 1);

	std::vector<std::string> patterns;
	size_t pos = 0;
	while (patterns.size() < 8) {
		size_t nl = b.find('\n', pos);
		patterns.emplace_back(b.substr(
		    pos, nl == std::string_view::npos ? b.size() - pos : nl - pos));
		if (nl == std::string_view::npos) break;
		pos = nl + 1;
	}

	(void)tsc::modulespecifiers::IsExcludedByRegex(a, patterns);
	(void)tsc::modulespecifiers::PathIsBareSpecifier(a);
	(void)tsc::modulespecifiers::GetNodeModulePathParts(a);
	(void)tsc::modulespecifiers::GetPackageNameFromDirectory(a);
	(void)tsc::modulespecifiers::GetJSExtensionForDeclarationFileExtension(
	    a);
	(void)tsc::modulespecifiers::
	    TryGetRealFileNameForNonJSDeclarationFileName(a);
	(void)tsc::modulespecifiers::replaceFirstStar(a, b.substr(0, 64));
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
