#pragma once

// Port of tsc/internal/module/resolver.go — program slice.
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "internal/module/types.h"

namespace tsc::module {

// resolver.go: caches subset — resolution caches keyed by containing dir.
struct caches {
	std::unordered_map<std::string, InfoCacheEntry*> packageJsonInfoCache;
	std::unordered_map<std::string, InfoCacheEntry*> packageJsonInfoCacheNonExisting;
};

struct ResolverOptions {
	ResolutionHost* host{};
	CompilerOptions* compilerOptions{};
	std::string typingsLocation;
	std::string projectName;
	std::vector<std::string> extraExtensions;
};

struct DefaultResolver {
	caches caches_;
	ResolutionHost* host{};
	CompilerOptions* compilerOptions{};
	std::string typingsLocation;
	std::string projectName;
	std::vector<std::string> extraExtensions;

	ResolvedModule* ResolveModuleName(
	    const std::string& moduleName, const std::string& containingFile,
	    ResolutionMode resolutionMode,
	    ResolvedProjectReference* redirectedReference);
	ResolvedModule* ResolveModuleNameFromDirectory(
	    const std::string& moduleName, const std::string& containingDirectory,
	    ResolutionMode resolutionMode);
	// Go's private resolveModuleName worker (containingFile already split).
	ResolvedModule* resolveModuleName(
	    const std::string& moduleName, const std::string& containingDirectory,
	    ResolutionMode resolutionMode);
	ResolvedTypeReferenceDirective* ResolveTypeReferenceDirective(
	    const std::string& typeReferenceDirectiveName,
	    const std::string& containingFile, ResolutionMode resolutionMode,
	    ResolvedProjectReference* redirectedReference);
	InfoCacheEntry* GetPackageScopeForPath(const std::string& directory);
	ResolvedModule* ResolvePackageDirectory(
	    const std::string& moduleName, const std::string& containingFile,
	    ResolutionMode resolutionMode,
	    ResolvedProjectReference* redirectedReference);

	// Owned resolution results (arena-style; alive for the resolver lifetime).
	std::vector<std::unique_ptr<ResolvedModule>> resolvedModules_;
	std::vector<std::unique_ptr<ResolvedTypeReferenceDirective>> resolvedTypeRefs_;
	std::vector<std::unique_ptr<InfoCacheEntry>> infoCacheEntries_;
};

// util.go: GetResolutionDiagnostic — nil returned as nullptr.
const DiagnosticMessage* getResolutionDiagnostic(
    const CompilerOptions* options, const ResolvedModule& resolvedModule,
    SourceFile* file);

// resolver.go: GetConditions
std::vector<std::string> getConditions(const CompilerOptions* options,
                                       ResolutionMode resolutionMode);

// resolver.go: GetAutomaticTypeDirectiveNames
std::vector<std::string> getAutomaticTypeDirectiveNames(
    const CompilerOptions* options, ResolutionHost* host);

// resolver.go: NewResolver
std::unique_ptr<DefaultResolver> newResolver(const ResolverOptions& opts);

}  // namespace tsc::module
