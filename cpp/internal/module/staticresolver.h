// module/staticresolver.go — declarations; impl in staticresolver.cpp.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "internal/module/resolver.h"
#include "internal/module/types.h"

namespace tsc::module {

// StaticResolutionEntry — staticresolver.go.
struct StaticResolutionEntry {
	std::string ModuleName;
	std::string ContainingDirectory;
	const ResolutionMode* ResolutionMode_ = nullptr;
	std::shared_ptr<ResolvedModule> Result;
};

// staticResolutionKey — staticresolver.go.
struct staticResolutionKey {
	std::string moduleName;
	std::string directory;  // tspath.Path
	ResolutionMode mode = ResolutionModeNone;
	bool hasDirectory = false;
	bool hasMode = false;

	bool operator==(const staticResolutionKey&) const = default;
};

struct staticResolutionKeyHash {
	size_t operator()(const staticResolutionKey& k) const {
		size_t h = std::hash<std::string>{}(k.moduleName);
		h = h * 31 + std::hash<std::string>{}(k.directory);
		h = h * 31 + std::hash<int>{}(static_cast<int>(k.mode));
		h = h * 31 + (k.hasDirectory ? 1 : 0);
		h = h * 31 + (k.hasMode ? 1 : 0);
		return h;
	}
};

// StaticResolutions — staticresolver.go.
struct StaticResolutions {
	bool fallbackToResolver = false;
	std::unordered_map<staticResolutionKey, std::shared_ptr<ResolvedModule>,
	                   staticResolutionKeyHash>
	    entries;
	std::string currentDirectory;
	bool useCaseSensitiveFileNames = false;

	std::pair<std::shared_ptr<ResolvedModule>, bool> lookup(
	    const std::string& moduleName, const std::string& containingDirectory,
	    ResolutionMode resolutionMode);
};

// NewStaticResolutions — staticresolver.go. Returns nullptr (Go returns an
// error) on invalid input.
StaticResolutions* NewStaticResolutions(
    const std::vector<StaticResolutionEntry>& entries,
    bool fallbackToResolver, std::string_view currentDirectory,
    bool useCaseSensitiveFileNames);

// StaticResolver — staticresolver.go.
struct StaticResolver : Resolver {
	Resolver* fallback;
	StaticResolutions* resolutions;

	StaticResolver(Resolver* fallback_, StaticResolutions* resolutions_)
	    : fallback(fallback_), resolutions(resolutions_) {}

	std::pair<std::shared_ptr<ResolvedModule>, std::vector<DiagAndArgs>>
	ResolveModuleName(
	    std::string_view moduleName, std::string_view containingFile,
	    ResolutionMode resolutionMode,
	    const ResolvedProjectReference* redirectedReference) override;

	std::pair<std::shared_ptr<ResolvedModule>, std::vector<DiagAndArgs>>
	ResolveModuleNameFromDirectory(
	    std::string_view moduleName, std::string_view containingDirectory,
	    ResolutionMode resolutionMode) override;

	std::pair<std::shared_ptr<ResolvedModule>, std::vector<DiagAndArgs>>
	resolveModuleName(
	    const std::string& moduleName, const std::string& containingFile,
	    const std::string& containingDirectory, ResolutionMode resolutionMode,
	    const ResolvedProjectReference* redirectedReference);

	std::pair<std::shared_ptr<ResolvedTypeReferenceDirective>,
	          std::vector<DiagAndArgs>>
	ResolveTypeReferenceDirective(
	    std::string_view typeReferenceDirectiveName,
	    std::string_view containingFile, ResolutionMode resolutionMode,
	    const ResolvedProjectReference* redirectedReference) override;

	std::shared_ptr<packagejson::InfoCacheEntry> GetPackageScopeForPath(
	    const std::string& directory) override;

	void PackageJsonCacheEntries(
	    const std::function<bool(
	        const std::string&,
	        std::shared_ptr<packagejson::InfoCacheEntry>)>& f) override;

	std::shared_ptr<ResolvedModule> ResolvePackageDirectory(
	    std::string_view moduleName, std::string_view containingFile,
	    ResolutionMode resolutionMode,
	    const ResolvedProjectReference* redirectedReference) override;
};

StaticResolver* NewStaticResolver(Resolver* fallback,
                                  StaticResolutions* resolutions);

}  // namespace tsc::module
