// fuzz_tspath — libFuzzer driver for path normalization/canonicalization.
//
// tspath is the canonicalization layer every file path in the compiler
// flows through; its inputs are user-controlled (import specifiers,
// tsconfig paths, URIs, dynamic-module encodings). The driver feeds raw
// bytes into the pure path functions — no FS involved:
//
//   normalizeSlashes / normalizePath / getDirectoryPath /
//   getBaseFileName / combinePaths / resolvePathComponents /
//   getPathFromPathComponents / getNormalizedAbsolutePath(-FromDirectory) /
//   getRelativePathFromDirectory / getRelativePathToDirectoryOrUrl /
//   convertToRelativePath / toPath (canonical form; hits
//   isEncodedDynamicFileName → canonicalDynamicURIPath) /
//   the encode/decode dynamic-URI family /
//   removeExtension / tryGetExtensionFromPath / tryExtractTSExtension /
//   ensurePathIsNonModuleName.
//
// Layout: data[0] selects case sensitivity; the rest of the buffer is
// split at the first NUL byte into two path inputs (the whole buffer
// when there is no NUL) so two-argument functions get independent halves.
//
// getRelativePathFromDirectory panics in Go when the two paths disagree
// on rootedness ("paths must either both be absolute or both be
// relative"); the port routes that panic through tscUnreachable, which
// the harness counts as a faithful abort.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "internal/tspath/tspath.h"

#include "cmd/fuzz/fuzz_common.h"

using namespace tsc;

namespace {

std::string_view sv(const uint8_t* d, size_t n) {
	return {reinterpret_cast<const char*>(d), n};
}

void run(const uint8_t* data, size_t size) {
	bool caseSensitive = (data[0] & 1) != 0;
	std::string_view rest = sv(data + 1, size - 1);

	size_t nul = rest.find('\0');
	std::string_view a =
	    rest.substr(0, nul == std::string_view::npos ? rest.size() : nul);
	std::string_view b =
	    nul == std::string_view::npos ? rest : rest.substr(nul + 1);

	// Single-arg normalization.
	std::string normalized = tspath::normalizePath(a);
	std::string slashes = tspath::normalizeSlashes(a);
	(void)tspath::getDirectoryPath(a);
	(void)tspath::getDirectoryPathFromNormalized(slashes);
	(void)tspath::getBaseFileName(a);
	(void)tspath::getBaseFileNameFromNormalized(slashes);
	(void)tspath::pathIsAbsolute(a);
	(void)tspath::isRootedDiskPath(a);
	(void)tspath::hasTrailingDirectorySeparator(a);
	(void)tspath::ensureTrailingDirectorySeparator(a);
	(void)tspath::removeTrailingDirectorySeparators(a);
	(void)tspath::tryGetExtensionFromPath(a);
	(void)tspath::tryExtractTSExtension(a);
	std::vector<std::string_view> wantedExts = {".ts", ".js", ".json"};
	(void)tspath::getAnyExtensionFromPath(a, &wantedExts, caseSensitive);
	(void)tspath::getAnyExtensionFromPath(a, nullptr, false);
	(void)tspath::getDeclarationFileExtension(a);
	(void)tspath::getDeclarationEmitExtensionForPath(a);
	(void)tspath::removeExtension(a, std::string_view(".d.ts"));
	(void)tspath::fileExtensionIs(a, ".ts");
	(void)tspath::fileExtensionIsOneOf(a, {".ts", ".js"});
	(void)tspath::hasExtension(a);

	// Component decomposition / reconstruction.
	auto comps = tspath::resolvePathComponents(a, "/cwd");
	std::vector<std::string_view> compViews(comps.begin(), comps.end());
	(void)tspath::getPathFromPathComponents(compViews);
	(void)tspath::getPathComponents(a);
	(void)tspath::getNormalizedAbsolutePath(a, "/cwd");
	(void)tspath::getNormalizedAbsolutePathFromNormalizedSlashes(slashes);
	(void)tspath::getNormalizedAbsolutePathFromDirectory(a, "/cwd");
	(void)tspath::getNormalizedAbsolutePathWithoutRoot(a, "/cwd");

	// Two-arg path math. getRelativePath* panics in Go when the two
	// sides disagree on rootedness; tscUnreachable counts as faithful.
	tspath::ComparePathsOptions opts;
	opts.useCaseSensitiveFileNames = caseSensitive;
	opts.currentDirectory = "/cwd";
	(void)tspath::combinePaths(a, {b});
	std::vector<std::string_view> rest2 = {b, "leaf.ts"};
	(void)tspath::combinePaths(a, rest2);
	(void)tspath::getPathComponentsRelativeTo(a, b, opts);
	(void)tspath::getRelativePathFromDirectory(a, b, opts);
	(void)tspath::getRelativePathFromFile(a, b, opts);
	(void)tspath::getRelativePathToDirectoryOrUrl(a, b, data[0] & 2, opts);
	(void)tspath::convertToRelativePath(a, opts);

	// Canonical Path form — hits isEncodedDynamicFileName →
	// canonicalDynamicURIPath for "^/"-prefixed input.
	(void)tspath::toPath(a, b, caseSensitive);

	// Dynamic URI encode/decode family (ts-server specifiers).
	std::string enc = tspath::encodeDynamicModuleSpecifier(a);
	(void)tspath::encodeDynamicDirectorySpecifier(a);
	(void)tspath::encodeDynamicURIDirectoryPath(a);
	(void)tspath::encodeDynamicRelativeURIPath(a);
	(void)tspath::encodeDynamicRelativeURIDirectoryPath(a);
	(void)tspath::encodeDynamicLogicalModuleSpecifier(a);
	(void)tspath::encodeDynamicURINoPath(a);
	(void)tspath::decodeDynamicURINoPath(a);
	(void)tspath::decodeDynamicURIPathSegment(a);
	(void)tspath::tryDecodeDynamicURIPathSegment(a);
	(void)tspath::decodeDynamicURIPath(a);
	(void)tspath::tryDecodeDynamicURIPath(a);
	(void)tspath::decodeDynamicURIPathForDisk(a);
	(void)tspath::isEncodedDynamicFileName(enc);
	(void)tspath::canonicalDynamicURIPath(a);
	(void)tspath::canonicalDynamicURIPath(enc);

	(void)tspath::ensurePathIsNonModuleName(a);
	(void)normalized;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size < 2) {
		return 0;
	}
	fuzz::runOnBigStack([](const uint8_t* d, size_t n) { run(d, n); }, data,
	                  size);
	return 0;
}
