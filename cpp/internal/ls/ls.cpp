// ls.cpp — the LanguageService base plumbing has its canonical home in
// api.cpp (ctor/accessors/views) and the slice files (source_map.cpp,
// findallreferences.cpp, utilities.cpp, displaypartswriter.cpp, hover.cpp,
// crossproject.cpp). The lsp::lsproto dep stubs moved to the canonical
// lsproto port (documentUriFileName / getClientCapabilities), and
// SimpleProgram::GetTypeChecker is defined inline in program.h.
// hovericon.go's fns are ported for real in hovericon.cpp.
#include "internal/ls/ls.h"

namespace tsc::ls {
} // namespace tsc::ls
