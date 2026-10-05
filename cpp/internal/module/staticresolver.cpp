// module/staticresolver.go — port of tsc/internal/module/staticresolver.go.
// === slice: module ===

#include "internal/module/staticresolver.h"

#include <cstdio>

#include "internal/tspath/tspath.h"

namespace tsc::module {

// StaticResolutions::lookup — staticresolver.go.
std::pair<std::shared_ptr<ResolvedModule>, bool> StaticResolutions::lookup(
    const std::string& moduleName, const std::string& containingDirectory,
    ResolutionMode resolutionMode) {
	auto directory = tspath::toPath(containingDirectory, currentDirectory,
	                                useCaseSensitiveFileNames);
	staticResolutionKey keys[4] = {
	    {moduleName, directory, resolutionMode, true, true},
	    {moduleName, directory, ResolutionModeNone, true, false},
	    {moduleName, "", resolutionMode, false, true},
	    {moduleName, "", ResolutionModeNone, false, false},
	};
	for (auto& key : keys) {
		if (auto it = entries.find(key); it != entries.end()) {
			return {it->second, true};
		}
	}
	return {nullptr, false};
}

// NewStaticResolutions — staticresolver.go.
StaticResolutions* NewStaticResolutions(
    const std::vector<StaticResolutionEntry>& entries_,
    bool fallbackToResolver, std::string_view currentDirectory,
    bool useCaseSensitiveFileNames) {
	auto resolutions = std::make_unique<StaticResolutions>();
	resolutions->fallbackToResolver = fallbackToResolver;
	resolutions->entries.reserve(entries_.size());
	resolutions->currentDirectory = currentDirectory;
	resolutions->useCaseSensitiveFileNames = useCaseSensitiveFileNames;
	for (auto& entry : entries_) {
		if (entry.ModuleName.empty()) {
			fprintf(stderr, "module name is empty\n");
			return nullptr;
		}
		staticResolutionKey key;
		key.moduleName = entry.ModuleName;
		if (!entry.ContainingDirectory.empty()) {
			key.directory = tspath::toPath(
			    entry.ContainingDirectory, currentDirectory,
			    useCaseSensitiveFileNames);
			key.hasDirectory = true;
		}
		if (entry.ResolutionMode_ != nullptr) {
			key.mode = *entry.ResolutionMode_;
			key.hasMode = true;
		}
		if (resolutions->entries.count(key)) {
			fprintf(stderr,
			        "duplicate static module resolution for \"%s\"\n",
			        entry.ModuleName.c_str());
			return nullptr;
		}
		resolutions->entries.emplace(key, entry.Result);
	}
	return resolutions.release();
}

// StaticResolver — staticresolver.go.
std::pair<std::shared_ptr<ResolvedModule>, std::vector<DiagAndArgs>>
StaticResolver::ResolveModuleName(
    std::string_view moduleName, std::string_view containingFile,
    ResolutionMode resolutionMode,
    const ResolvedProjectReference* redirectedReference) {
	return resolveModuleName(
	    std::string{moduleName}, std::string{containingFile},
	    tspath::getDirectoryPath(containingFile), resolutionMode,
	    redirectedReference);
}

std::pair<std::shared_ptr<ResolvedModule>, std::vector<DiagAndArgs>>
StaticResolver::ResolveModuleNameFromDirectory(
    std::string_view moduleName, std::string_view containingDirectory,
    ResolutionMode resolutionMode) {
	if (auto [result, found] = resolutions->lookup(
	        std::string{moduleName}, std::string{containingDirectory},
	        resolutionMode);
	    found) {
		return {result, {}};
	}
	if (!resolutions->fallbackToResolver) {
		return {nullptr, {}};
	}
	return fallback->ResolveModuleNameFromDirectory(
	    moduleName, containingDirectory, resolutionMode);
}

std::pair<std::shared_ptr<ResolvedModule>, std::vector<DiagAndArgs>>
StaticResolver::resolveModuleName(
    const std::string& moduleName, const std::string& containingFile,
    const std::string& containingDirectory, ResolutionMode resolutionMode,
    const ResolvedProjectReference* redirectedReference) {
	if (auto [result, found] =
	        resolutions->lookup(moduleName, containingDirectory, resolutionMode);
	    found) {
		return {result, {}};
	}
	if (!resolutions->fallbackToResolver) {
		return {nullptr, {}};
	}
	return fallback->ResolveModuleName(moduleName, containingFile,
	                                   resolutionMode, redirectedReference);
}

std::pair<std::shared_ptr<ResolvedTypeReferenceDirective>,
          std::vector<DiagAndArgs>>
StaticResolver::ResolveTypeReferenceDirective(
    std::string_view typeReferenceDirectiveName,
    std::string_view containingFile, ResolutionMode resolutionMode,
    const ResolvedProjectReference* redirectedReference) {
	return fallback->ResolveTypeReferenceDirective(
	    typeReferenceDirectiveName, containingFile, resolutionMode,
	    redirectedReference);
}

std::shared_ptr<packagejson::InfoCacheEntry>
StaticResolver::GetPackageScopeForPath(const std::string& directory) {
	return fallback->GetPackageScopeForPath(directory);
}

void StaticResolver::PackageJsonCacheEntries(
    const std::function<bool(
        const std::string&,
        std::shared_ptr<packagejson::InfoCacheEntry>)>& f) {
	fallback->PackageJsonCacheEntries(f);
}

std::shared_ptr<ResolvedModule> StaticResolver::ResolvePackageDirectory(
    std::string_view moduleName, std::string_view containingFile,
    ResolutionMode resolutionMode,
    const ResolvedProjectReference* redirectedReference) {
	return fallback->ResolvePackageDirectory(
	    moduleName, containingFile, resolutionMode, redirectedReference);
}

StaticResolver* NewStaticResolver(Resolver* fallback,
                                  StaticResolutions* resolutions) {
	return new StaticResolver(fallback, resolutions);
}

}  // namespace tsc::module
