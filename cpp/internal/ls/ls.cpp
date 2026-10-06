// === slice: ls-coreC ===
// ls.cpp — the LanguageService base plumbing has its canonical home in
// api.cpp (ctor/accessors/views) and the slice files (source_map.cpp,
// findallreferences.cpp, utilities.cpp, displaypartswriter.cpp, hover.cpp,
// crossproject.cpp). The lsp::lsproto dep stubs moved to the canonical
// lsproto port (documentUriFileName / getClientCapabilities), and
// SimpleProgram::GetTypeChecker is defined inline in program.h.
// What remains here are dep stubs for the still-unported hovericon.go fns.
#include "internal/ls/ls.h"

namespace tsc::ls {

// === dep stubs — removed when owner slice lands ===

// --- hovericon.go — owned by ls-coreA ---
lsp::lsproto::VSImageId* getVSHoverImageId(lsutil::ScriptElementKind kind,
										 lsutil::ScriptElementKindModifier modifiers) {
	TSC_UNREACHABLE("getVSHoverImageId — owned by ls-coreA slice");
}
lsp::lsproto::VSContainerElement* buildVSHoverRawContent(
	lsp::lsproto::VSImageId* imageId,
	lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::VSClassifiedTextRun>> quickInfoRuns,
	lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::VSClassifiedTextRun>> documentationRuns) {
	TSC_UNREACHABLE("buildVSHoverRawContent — owned by ls-coreA slice");
}
// === end dep stubs ===

} // namespace tsc::ls
