// Port of tsc/internal/project/ata — automatic type acquisition (ATA):
// typings installer, discovery, validation and the safe-file-name map.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/core/version.h"
#include "internal/gostd/gostd.h"
#include "internal/module/types.h"
#include "internal/project/logging/logging.h"
#include "internal/semver/semver.h"
#include "internal/vfs/vfs.h"

namespace tsc::module {
class DefaultResolver;
}

namespace tsc::ata {

struct TypingsInfo {
	TypeAcquisition* TypeAcquisition = nullptr;
	CompilerOptions* CompilerOptions = nullptr;
	collections::Set<std::string>* UnresolvedImports = nullptr;

	// Equals — TypingsInfo.Equals: nil-safe field comparisons.
	bool Equals(const TypingsInfo& other) const {
		bool taEq = TypeAcquisition == other.TypeAcquisition ||
		    (TypeAcquisition != nullptr &&
		     other.TypeAcquisition != nullptr &&
		     TypeAcquisition->Equals(other.TypeAcquisition));
		return taEq &&
		       CompilerOptions->GetAllowJS() ==
		           other.CompilerOptions->GetAllowJS() &&
		       collections::Set<std::string>::EqualsPtr(
		           UnresolvedImports, other.UnresolvedImports);
	}
};

struct CachedTyping {
	std::string TypingsLocation;
	std::shared_ptr<semver::Version> Version;
};

struct TypingsInstallerOptions {
	std::string TypingsLocation;
	int ThrottleLimit = 0;
};

// NpmExecutor — ata.go.
struct NpmExecutor {
	virtual ~NpmExecutor() = default;
	virtual std::pair<std::string, gostd::Error>
	NpmInstall(const gostd::Context& ctx, const std::string& cwd,
	           const std::vector<std::string>& args) = 0;
};

// TypingsInstallerHost — NpmExecutor + module.ResolutionHost.
struct TypingsInstallerHost : NpmExecutor, module::ResolutionHost {};

// ProjectID — ata.go: fmt.Stringer equivalent.
struct ProjectID {
	virtual ~ProjectID() = default;
	virtual std::string String() const = 0;
};

// !!! sheetal currently we use latest instead of core.VersionMajorMinor()
inline constexpr std::string_view tsVersionToUse = "latest";

struct TypingsInstallRequest {
	ProjectID* ProjectID = nullptr;
	TypingsInfo* TypingsInfo = nullptr;
	std::vector<std::string> FileNames;
	std::string ProjectRootPath;
	CompilerOptions* CompilerOptions = nullptr;
	std::string CurrentDirectory;
	std::function<ScriptKind(std::string_view)> GetScriptKind;
	vfs::FS* FS = nullptr;
	logging::Logger* Logger = nullptr;
};

struct TypingsInstallResult {
	std::vector<std::string> TypingsFiles;
	std::vector<std::string> FilesToWatch;
};

// countingSemaphore — Go `chan struct{}` with capacity (ThrottleLimit).
struct countingSemaphore {
	countingSemaphore(const countingSemaphore&) = delete;
	countingSemaphore& operator=(const countingSemaphore&) = delete;

	explicit countingSemaphore(int capacity) : slots(capacity) {}
	std::mutex mu;
	std::condition_variable cv;
	int slots;

	void acquire() {
		std::unique_lock<std::mutex> lk(mu);
		cv.wait(lk, [this] { return slots > 0; });
		--slots;
	}
	void release() {
		{
			std::lock_guard<std::mutex> lk(mu);
			++slots;
		}
		cv.notify_one();
	}
};

class TypingsInstaller {
	std::string typingsLocation;
	TypingsInstallerHost* host;

	std::once_flag initOnce;

	collections::SyncMap<std::string, std::shared_ptr<CachedTyping>>
	    packageNameToTypingLocation;
	collections::SyncMap<std::string, bool> missingTypingsSet;

	std::unordered_map<std::string,
	                   std::unordered_map<std::string, std::string>>
	    typesRegistry;

	std::atomic<int32_t> installRunCount{0};
	countingSemaphore concurrencySemaphore;

public:
	TypingsInstaller(const TypingsInstallerOptions* options,
	                 TypingsInstallerHost* host)
	    : typingsLocation(options->TypingsLocation),
	      host(host),
	      concurrencySemaphore(options->ThrottleLimit) {}

	bool IsKnownTypesPackageName(ProjectID* projectID,
	                             const std::string& name, vfs::FS* fs,
	                             logging::Logger* logger);

	std::pair<std::unique_ptr<TypingsInstallResult>, gostd::Error>
	InstallTypings(const gostd::Context& ctx,
	               const TypingsInstallRequest* request);

private:
	std::pair<std::unique_ptr<TypingsInstallResult>, gostd::Error>
	discoverAndInstallTypings(const gostd::Context& ctx,
	                          const TypingsInstallRequest* request);

	std::pair<std::vector<std::string>, gostd::Error> installTypings(
	    const gostd::Context& ctx,
	    int32_t requestID,
	    const std::vector<std::string>& currentlyCachedTypings,
	    const std::vector<std::string>& filteredTypings,
	    logging::Logger* logger);

	std::pair<std::vector<std::string>, bool> installWorker(
	    const gostd::Context& ctx,
	    int32_t requestId, const std::vector<std::string>& packageNames,
	    logging::Logger* logger);

	std::vector<std::string> filterTypings(
	    logging::Logger* logger,
	    const std::vector<std::string>& typingsToInstall);

	void init(const gostd::Context& ctx, const std::string& projectID,
	          vfs::FS* fs,
	          logging::Logger* logger);

	void processCacheLocation(const std::string& projectID, vfs::FS* fs,
	                          logging::Logger* logger);

	void ensureTypingsLocationExists(vfs::FS* fs, logging::Logger* logger);

	std::string typingToFileName(module::DefaultResolver* resolver,
	                             const std::string& packageName);

	std::unordered_map<std::string,
	                   std::unordered_map<std::string, std::string>>
	loadTypesRegistryFile(vfs::FS* fs, logging::Logger* logger);
};

// --- discovertypings.go ---

// DiscoverTypings — returns (cachedTypingPaths, newTypingNames, filesToWatch).
struct DiscoverTypingsResult {
	std::vector<std::string> cachedTypingPaths;
	std::vector<std::string> newTypingNames;
	std::vector<std::string> filesToWatch;
};

// installNpmPackages — ata.go (Go package-internal function; declared
// here so package-internal tests can call it). Batches package names into
// <8000-char npm commands and runs each via the throttle group.
gostd::Error installNpmPackages(
    gostd::Context ctx, const std::vector<std::string>& packageNames,
    countingSemaphore* semaphore,
    const std::function<gostd::Error(const std::vector<std::string>&)>&
        installPackages);

DiscoverTypingsResult DiscoverTypings(
    vfs::FS* fs, logging::Logger* logger, const TypingsInfo* typingsInfo,
    const std::vector<std::string>& fileNames,
    const std::string& projectRootPath,
    collections::SyncMap<std::string, std::shared_ptr<CachedTyping>>*
        packageNameToTypingLocation,
    const std::unordered_map<
        std::string, std::unordered_map<std::string, std::string>>&
        typesRegistry);

bool isTypingUpToDate(
    const CachedTyping* cachedTyping,
    const std::unordered_map<std::string, std::string>&
        availableTypingVersions);

// --- validatepackagename.go ---

enum NameValidationResult : int32_t {
	NameOk = 0,
	EmptyName,
	NameTooLong,
	NameStartsWithDot,
	NameStartsWithUnderscore,
	NameContainsNonURISafeCharacters,
};

inline constexpr int maxPackageNameLength = 214;

struct ValidatePackageNameResult {
	NameValidationResult result;
	std::string name;
	bool isScopeName;
};

// ValidatePackageName — package name rules per npmjs.com/files/package.json.
ValidatePackageNameResult ValidatePackageName(const std::string& packageName);

std::string renderPackageNameValidationFailure(
    const std::string& typing, NameValidationResult result,
    const std::string& name, bool isScopeName);

// safeFileNameToTypeName — typesmap.go.
extern const std::unordered_map<std::string, std::string>
    safeFileNameToTypeName;

} // namespace tsc::ata
