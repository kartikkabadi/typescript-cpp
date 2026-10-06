// === slice: fourslash (dep-stub bodies) ===
// Bodies for the fourslash dep declarations in fourslash_deps.h.
// The lsp (server) and project (parsecache/session/projectcollection/
// configfileregistry) slices have landed — their real ports live in
// internal/lsp/ and internal/project/. Only tsctests remains a stub:
// TSC_UNREACHABLE("<name> — owned by <slice>").

#include "internal/fourslash/fourslash_deps.h"

#include "internal/core/types.h"

namespace tsc::tsctests {

// GetFileMapWithBuild — owned by execute/tsc.
gostd::Error getFileMapWithBuild(
	std::unordered_map<std::string, std::any>&,
	const std::vector<std::string>&) {
	TSC_UNREACHABLE("tsctests.GetFileMapWithBuild — owned by execute/tsc");
}

} // namespace tsc::tsctests
