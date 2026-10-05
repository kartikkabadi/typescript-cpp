// outputpaths — decls for tsc/internal/outputpaths/outputpaths.go.
// === slice: modulespecifiers === (dep-stub decls; the package is not ported
// yet — definitions live in modulespecifiers/specifiers.cpp as
// TSC_UNREACHABLE stubs until an outputpaths slice lands.)
#pragma once

#include <string>
#include <string_view>

#include "internal/core/types.h"
#include "internal/tspath/tspath.h"

namespace tsc {
struct CompilerOptions;
namespace checker {
class Program;
}
} // namespace tsc

namespace tsc::outputpaths {

// outputpaths.go:101 — GetOutputJSFileNameWorker. Go's `host` is its
// OutputPathsHost interface; checker::Program is the concrete C++ type
// used everywhere it is instantiated.
std::string GetOutputJSFileNameWorker(const std::string& inputFileName,
                                      const CompilerOptions* options,
                                      checker::Program* host);

// outputpaths.go:108 — GetOutputDeclarationFileNameWorker.
std::string GetOutputDeclarationFileNameWorker(
    const std::string& inputFileName, const CompilerOptions* options,
    checker::Program* host);

// GetOutputExtension — outputpaths.go:116
inline std::string_view GetOutputExtension(std::string_view fileName,
                                           JsxEmit jsx) {
	using namespace tspath;
	if (fileExtensionIs(fileName, extensionJson)) {
		return extensionJson;
	}
	if (jsx == JsxEmit::Preserve &&
	    fileExtensionIsOneOf(fileName, {extensionJsx, extensionTsx})) {
		return extensionJsx;
	}
	if (fileExtensionIsOneOf(fileName, {extensionMts, extensionMjs})) {
		return extensionMjs;
	}
	if (fileExtensionIsOneOf(fileName, {extensionCts, extensionCjs})) {
		return extensionCjs;
	}
	return extensionJs;
}

} // namespace tsc::outputpaths
