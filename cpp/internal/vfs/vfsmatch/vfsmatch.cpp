// vfsmatch.cpp — port of
// tsc/internal/vfs/vfsmatch/stringer_generated.go (Usage.String()).
#include "internal/vfs/vfsmatch/vfsmatch.h"

namespace tsc::vfs::vfsmatch {

// Usage.String() — stringer-generated in Go.
std::string usageString(Usage u) {
	static constexpr std::string_view usageName =
	    "FilesDirectoriesExclude";
	static constexpr int usageIndex[] = {0, 5, 16, 23};
	int idx = static_cast<int>(u);
	if (idx < 0 || idx >= 3) {
		return "Usage(" + std::to_string(idx) + ")";
	}
	return std::string{
	    usageName.substr(usageIndex[idx],
	                     usageIndex[idx + 1] - usageIndex[idx])};
}

} // namespace tsc::vfs::vfsmatch
