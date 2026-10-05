// compileroptions.go — CompilerOptions methods that need tspath.
// === slice: module ===

#include "internal/core/types.h"
#include "internal/tspath/tspath.h"

namespace tsc {

// compileroptions.go — GetEffectiveTypeRoots.
std::pair<std::vector<std::string>, bool> CompilerOptions::GetEffectiveTypeRoots(
    std::string_view currentDirectory) const {
	if (!TypeRoots.empty()) {
		// Note: Go distinguishes nil vs empty TypeRoots; C++ cannot.
		return {TypeRoots, true};
	}
	std::string baseDir;
	if (!ConfigFilePath.empty()) {
		baseDir = tspath::getDirectoryPath(ConfigFilePath);
	} else {
		baseDir = std::string{currentDirectory};
		if (baseDir.empty()) {
			// panic: cannot get effective type roots without a config file
			// path or current directory
			__builtin_trap();
		}
	}

	std::vector<std::string> typeRoots;
	tspath::forEachAncestorDirectory<bool>(
	    baseDir, [&](std::string_view dir) -> std::pair<bool, bool> {
		    typeRoots.emplace_back(
		        tspath::combinePaths(dir, {"node_modules", "@types"}));
		    return {false, false};
	    });
	return {typeRoots, false};
}

// compileroptions.go — GetPathsBasePath.
std::string CompilerOptions::GetPathsBasePath(
    std::string_view currentDirectory) const {
	if (Paths.empty()) {
		return "";
	}
	if (!PathsBasePath.empty()) {
		return PathsBasePath;
	}
	return std::string{currentDirectory};
}

}  // namespace tsc
