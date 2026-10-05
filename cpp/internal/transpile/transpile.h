// transpile — transpile.go + fs.go: single-file JavaScript and declaration
// emit over an in-memory FS.
#pragma once

#include <string>
#include <vector>

#include "internal/core/types.h"

namespace tsc {
struct Diagnostic;
}

namespace tsc::transpile {

// Options — transpile.go:17. Configures single-file transpilation.
struct Options {
	// CompilerOptions are the base compiler options to use for the
	// transpilation. If nil, a default set of compiler options is used.
	// Regardless of what is provided, a number of options are
	// unconditionally overridden; see TranspileModule and
	// TranspileDeclaration.
	CompilerOptions* CompilerOptions = nullptr;

	// FileName is the name given to the synthesized input file. It only
	// needs to be provided if the source text relies on characteristics
	// implied by the file's extension or path, e.g. its extension
	// controls whether the file is parsed as a script or module, whether
	// JSX syntax is allowed, etc.
	// Defaults to "module.ts", or "module.tsx" if CompilerOptions.Jsx is
	// set.
	std::string FileName;

	// ReportDiagnostics indicates whether syntactic and compiler option
	// diagnostics should be included in the result. Regardless of this
	// setting, diagnostics produced while emitting (including
	// declaration emit errors such as those produced by isolated
	// declarations) are always included.
	bool ReportDiagnostics = false;
};

// Output — transpile.go:37.
struct Output {
	std::string OutputText;
	std::vector<Diagnostic*> Diagnostics;
	std::string SourceMapText;
};

// TranspileModule — transpile.go:82. Transpiles a single file of source
// text to JavaScript using the specified options. Returns nullptr when
// emission cannot complete (Go returns nil when the context is canceled;
// our port drops context.Context).
Output* TranspileModule(const std::string& input, const Options& options);

// TranspileDeclaration — transpile.go:115. Creates a declaration (.d.ts)
// file from a single file of source text.
Output* TranspileDeclaration(const std::string& input, const Options& options);

} // namespace tsc::transpile
