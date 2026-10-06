// ls.cpp — intentionally near-empty: every definition that once lived here
// was a dep-stub shadow of a real port. The canonical definitions live in
// the per-Go-file TUs: languageservice.cpp (languageservice.go accessors +
// NewLanguageService), api.cpp (api.go), source_map.cpp (source_map.go),
// displaypartswriter.cpp (displaypartswriter.go), hovericon.cpp
// (hovericon.go), findallreferences.cpp, crossproject.cpp,
// completions.cpp, utilities.cpp, lsproto.cpp (getClientCapabilities /
// documentUriFileName), program.cpp (SimpleProgram::GetTypeChecker).
#include "internal/ls/ls.h"

namespace tsc::ls {
} // namespace tsc::ls
