// outputpaths — decls for tsc/internal/outputpaths/outputpaths.go.
// === slice: modulespecifiers === (dep-stub decls; the package is not ported
// yet — definitions live in modulespecifiers/specifiers.cpp as
// TSC_UNREACHABLE stubs until an outputpaths slice lands.)
#pragma once

#include <string>

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

} // namespace tsc::outputpaths
