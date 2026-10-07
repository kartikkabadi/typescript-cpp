// === slice: fourslash (dep-stub bodies) ===
// Bodies for the fourslash dep declarations in fourslash_deps.h.
// The lsp (server) and project (parsecache/session/projectcollection/
// configfileregistry) slices have landed — their real ports live in
// internal/lsp/ and internal/project/.

#include "internal/fourslash/fourslash_deps.h"

#include "internal/core/types.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::tsctests {

// getFileMapWithBuild adapts fourslash's map[string]any testfs to the
// real tsctests::GetFileMapWithBuild (FileMap = map<string,MapFileInput>)
// and merges the build's output back — Go mutates the map in place.
gostd::Error getFileMapWithBuild(
	std::unordered_map<std::string, std::any>& testfs,
	const std::vector<std::string>& args) {
	execute::tsctests::FileMap fm;
	for (auto& [k, v] : testfs) {
		if (auto* s = std::any_cast<std::string>(&v)) {
			fm[k] = *s;
		} else if (auto* b = std::any_cast<std::vector<uint8_t>>(&v)) {
			fm[k] = *b;
		} else if (auto* f = std::any_cast<std::shared_ptr<
		               vfs::vfstest::fstest::MapFile>>(&v)) {
			fm[k] = *f;
		} else {
			TSC_UNREACHABLE("unexpected testfs entry type");
		}
	}
	fm = execute::tsctests::GetFileMapWithBuild(std::move(fm), args);
	for (auto& [k, v] : fm) {
		if (auto* s = std::get_if<std::string>(&v)) {
			testfs[k] = *s;
		} else if (auto* b = std::get_if<std::vector<uint8_t>>(&v)) {
			testfs[k] = *b;
		} else {
			testfs[k] = std::get<
			    std::shared_ptr<vfs::vfstest::fstest::MapFile>>(v);
		}
	}
	return nullptr;
}

} // namespace tsc::tsctests
