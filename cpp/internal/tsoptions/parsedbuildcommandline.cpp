// Port of tsc/internal/tsoptions/parsedbuildcommandline.go and
// parsedoptions.go (type-only file — ParsedOptions lives in the header).
#include "internal/tsoptions/tsoptions.h"

#include "internal/tspath/tspath.h"

namespace tsc::tsoptions {

// ResolvedProjectPaths — parsedbuildcommandline.go:29.
std::vector<std::string> ParsedBuildCommandLine::ResolvedProjectPaths() {
	std::call_once(resolvedProjectPathsOnce, [&] {
		resolvedProjectPaths.clear();
		for (const auto& project : Projects) {
			resolvedProjectPaths.push_back(
			    ResolveConfigFileNameOfProjectReference(tspath::resolvePath(
			        comparePathsOptions.currentDirectory, {project})));
		}
	});
	return resolvedProjectPaths;
}

// Locale — parsedbuildcommandline.go:40.
locale::Locale ParsedBuildCommandLine::Locale() {
	std::call_once(localeOnce, [&] {
		locale_ = locale::parse(CompilerOptions->Locale).first;
	});
	return locale_;
}

}  // namespace tsc::tsoptions
