// Port of tsc/internal/tsoptions/parsedbuildcommandline.go and
// parsedoptions.go (type-only file — ParsedOptions lives in the header).
#include "internal/tsoptions/tsoptions.h"

#include "internal/tspath/tspath.h"

namespace tsc::tsoptions {

// ParsedOptions::Equals — parsedoptions.go. Field-by-field comparison using
// each option type's equality semantics.
bool ParsedOptions::Equals(const ParsedOptions* other) const {
	if (this == other) {
		return true;
	}
	if (other == nullptr) {
		return false;
	}
	if (!CompilerOptions->Equals(other->CompilerOptions) ||
	    !TypeAcquisition->Equals(other->TypeAcquisition)) {
		return false;
	}
	if (FileNames != other->FileNames) {
		return false;
	}
	if (ProjectReferences.size() != other->ProjectReferences.size()) {
		return false;
	}
	for (size_t i = 0; i < ProjectReferences.size(); i++) {
		auto* a = ProjectReferences[i];
		auto* b = other->ProjectReferences[i];
		if (a == nullptr || b == nullptr) {
			if (a != b) {
				return false;
			}
			continue;
		}
		if (a->Path != b->Path || a->OriginalPath != b->OriginalPath ||
		    a->Circular != b->Circular) {
			return false;
		}
	}
	if (ContentMappers.size() != other->ContentMappers.size()) {
		return false;
	}
	for (size_t i = 0; i < ContentMappers.size(); i++) {
		auto* a = ContentMappers[i];
		auto* b = other->ContentMappers[i];
		if (a == nullptr || b == nullptr) {
			if (a != b) {
				return false;
			}
			continue;
		}
		if (!a->Equals(b)) {
			return false;
		}
	}
	return true;
}

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
