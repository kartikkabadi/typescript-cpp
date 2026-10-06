// moduleResolverFactory / callbackModuleResolver / programResolutionContext —
// module_resolution.go. The callback resolver wraps the conn.Call invocation;
// Go's `(..., error)` resolver returns have no error slot in
// module::Resolver's C++ signature, so callback failures throw (Go panics are
// throw std::runtime_error throughout the port).
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "internal/api/session.h"
#include "internal/module/resolver.h"
#include "internal/module/staticresolver.h"
#include "internal/project/project.h"

namespace tsc::api {

// moduleResolverFactory — module_resolution.go:19.
struct moduleResolverFactory : project::ModuleResolverFactory {
	std::shared_ptr<moduleResolverRegistration> registration;
	Session* session = nullptr;
	std::shared_ptr<ipc::Conn> conn;
	gostd::Context ctx;
	std::string currentDirectory;

	std::pair<module::Resolver*, std::function<void()>> NewResolver(
	    const module::ResolverOptions& options) override;
};

// callbackModuleResolver — module_resolution.go:33.
struct callbackModuleResolver : module::Resolver {
	moduleResolverRegistration* registration = nullptr;
	std::shared_ptr<ipc::Conn> conn;
	gostd::Context ctx;
	std::string currentDirectory;
	SnapshotID snapshot{};
	uint64_t programResolutionContextID{};
	module::Resolver* fallbackResolver = nullptr;

	std::pair<std::shared_ptr<module::ResolvedModule>,
	          std::vector<module::DiagAndArgs>>
	ResolveModuleName(std::string_view moduleName,
	                  std::string_view containingFile,
	                  ResolutionMode resolutionMode,
	                  const module::ResolvedProjectReference* redirectedReference) override;
	std::pair<std::shared_ptr<module::ResolvedModule>,
	          std::vector<module::DiagAndArgs>>
	ResolveModuleNameFromDirectory(std::string_view moduleName,
	                               std::string_view containingDirectory,
	                               ResolutionMode resolutionMode) override;
	std::pair<std::shared_ptr<module::ResolvedModule>,
	          std::vector<module::DiagAndArgs>>
	resolveModuleName(const std::string& moduleName,
	                  const std::string& containingFile,
	                  const std::string& containingDirectory,
	                  ResolutionMode resolutionMode,
	                  const module::ResolvedProjectReference* redirectedReference);
	std::pair<std::shared_ptr<module::ResolvedTypeReferenceDirective>,
	          std::vector<module::DiagAndArgs>>
	ResolveTypeReferenceDirective(
	    std::string_view typeReferenceDirectiveName,
	    std::string_view containingFile, ResolutionMode resolutionMode,
	    const module::ResolvedProjectReference* redirectedReference) override;
	std::shared_ptr<packagejson::InfoCacheEntry> GetPackageScopeForPath(
	    const std::string& directory) override;
	void PackageJsonCacheEntries(
	    const std::function<bool(
	        const std::string&,
	        std::shared_ptr<packagejson::InfoCacheEntry>)>& f) override;
	std::shared_ptr<module::ResolvedModule> ResolvePackageDirectory(
	    std::string_view moduleName, std::string_view containingFile,
	    ResolutionMode resolutionMode,
	    const module::ResolvedProjectReference* redirectedReference) override;
};

} // namespace tsc::api
