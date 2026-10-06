// ata.cpp — port of tsc/internal/project/ata/ata.go.
#include "internal/project/ata/ata.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "internal/json/json.h"
#include "internal/module/resolver.h"
#include "internal/module/util.h"
#include "internal/tspath/tspath.h"

namespace tsc::ata {
namespace {

// fmtStringList — fmt.Sprintf("%v", []string): "[a b c]".
std::string fmtStringList(const std::vector<std::string>& v) {
	std::string out = "[";
	for (size_t i = 0; i < v.size(); i++) {
		if (i != 0) {
			out += ' ';
		}
		out += v[i];
	}
	out += ']';
	return out;
}

// errGroup — golang.org/x/sync/errgroup.Group: Go spawns a goroutine;
// Wait blocks until all finish and returns the first non-nil error.
struct errGroup {
	std::mutex mu;
	std::condition_variable cv;
	int count = 0;
	gostd::Error firstErr;

	void go(std::function<gostd::Error()> fn) {
		{
			std::lock_guard<std::mutex> lk(mu);
			++count;
		}
		std::thread([this, fn = std::move(fn)] {
			gostd::Error err = fn();
			std::unique_lock<std::mutex> lk(mu);
			if (err != nullptr && firstErr == nullptr) {
				firstErr = err;
			}
			if (--count == 0) {
				lk.unlock();
				cv.notify_all();
			}
		}).detach();
	}

	gostd::Error wait() {
		std::unique_lock<std::mutex> lk(mu);
		cv.wait(lk, [this] { return count == 0; });
		return firstErr;
	}
};

// throttleGroup — core.ThrottleGroup (workgroup.go): like errgroup but with
// concurrency limiting via the semaphore.
struct throttleGroup {
	countingSemaphore* semaphore;
	errGroup group;

	void go(std::function<gostd::Error()> fn) {
		group.go([this, fn = std::move(fn)]() -> gostd::Error {
			// Acquire semaphore slot — blocks until a slot is available.
			semaphore->acquire();
			// Release semaphore slot when done.
			struct Releaser {
				countingSemaphore* s;
				~Releaser() { s->release(); }
			} releaser{semaphore};
			return fn();
		});
	}

	gostd::Error wait() { return group.wait(); }
};

// installNpmPackages — batches package names into <8000-char npm commands
// and runs each via the throttle group.
gostd::Error installNpmPackages(
    gostd::Context ctx, const std::vector<std::string>& packageNames,
    countingSemaphore* semaphore,
    const std::function<gostd::Error(const std::vector<std::string>&)>&
        installPackages) {
	(void)ctx; // errgroup.WithContext(ctx) — ctx is unused by Wait.
	throttleGroup tg{semaphore, {}};

	size_t currentCommandStart = 0;
	size_t currentCommandEnd = 0;
	int currentCommandSize = 100;

	for (const std::string& packageName : packageNames) {
		currentCommandSize += (int)packageName.size() + 1;
		if (currentCommandSize < 8000) {
			currentCommandEnd++;
		} else {
			std::vector<std::string> packages(
			    packageNames.begin() + currentCommandStart,
			    packageNames.begin() + currentCommandEnd);
			tg.go([installPackages, packages = std::move(packages)] {
				return installPackages(packages);
			});
			currentCommandStart = currentCommandEnd;
			currentCommandSize = 100 + (int)packageName.size() + 1;
			currentCommandEnd++;
		}
	}

	// Handle the final batch
	if (currentCommandStart < packageNames.size()) {
		std::vector<std::string> packages(
		    packageNames.begin() + currentCommandStart,
		    packageNames.begin() + currentCommandEnd);
		tg.go([installPackages, packages = std::move(packages)] {
			return installPackages(packages);
		});
	}

	return tg.wait();
}

// npmConfig — package.json devDependencies only (ata.go npmConfig).
// npmLock — package-lock.json dependencies/packages (ata.go npmLock).
struct npmLockData {
	std::unordered_map<std::string, std::string> dependencies;
	std::unordered_map<std::string, std::string> packages;
};

// readVersionFields — for each member object `name: {"version": "x"}` of
// `obj` fill dst[name] = "x". Non-object members are skipped (json.Unmarshal
// ignores fill errors in ata.go).
void readVersionFields(const json::Dom& obj,
                       std::unordered_map<std::string, std::string>* dst) {
	if (obj.kind != json::Dom::K::Object) {
		return;
	}
	for (const auto& [name, entry] : obj.obj) {
		if (entry.kind != json::Dom::K::Object) {
			continue;
		}
		if (const json::Dom* v = json::objGet(entry, "version")) {
			if (v->kind == json::Dom::K::String) {
				auto [s, err] = json::asString(*v, "string");
				if (err == nullptr) {
					(*dst)[name] = s;
				}
			}
		}
	}
}

// parseNpmConfigOrLock — returns raw contents; fields best-effort parsed.
// DevDependencies keys → devDeps; npmLock dependencies/packages version
// maps → lock. (parseNpmConfigOrLock[T] in ata.go, split here by shape.)
std::string parseNpmConfigOrLock(
    vfs::FS* fs, logging::Logger* logger, const std::string& location,
    std::vector<std::string>* devDeps, npmLockData* lock) {
	auto [contents, ok] = fs->ReadFile(location);
	auto [dom, err] = json::parse(contents);
	if (err != nullptr || dom.kind != json::Dom::K::Object) {
		return contents;
	}
	if (devDeps != nullptr) {
		if (const json::Dom* dd = json::objGet(dom, "devDependencies")) {
			if (dd->kind == json::Dom::K::Object) {
				for (const auto& [k, _] : dd->obj) {
					devDeps->push_back(k);
				}
			}
		}
	}
	if (lock != nullptr) {
		if (const json::Dom* deps = json::objGet(dom, "dependencies")) {
			readVersionFields(*deps, &lock->dependencies);
		}
		if (const json::Dom* pkgs = json::objGet(dom, "packages")) {
			readVersionFields(*pkgs, &lock->packages);
		}
	}
	return contents;
}

} // namespace

// === TypingsInstaller ===

bool TypingsInstaller::IsKnownTypesPackageName(
    ProjectID* projectID, const std::string& name, vfs::FS* fs,
    logging::Logger* logger) {
	// We want to avoid looking this up in the registry as that is expensive.
	// So first check that it's actually an NPM package.
	auto validationResult = ValidatePackageName(name);
	if (validationResult.result != NameOk) {
		return false;
	}
	// Strada did this lazily - is that needed here to not waiting on and
	// returning false on first request
	init(projectID->String(), fs, logger);
	return typesRegistry.find(name) != typesRegistry.end();
}

std::pair<std::unique_ptr<TypingsInstallResult>, gostd::Error>
TypingsInstaller::InstallTypings(const TypingsInstallRequest* request) {
	auto [result, err] = discoverAndInstallTypings(request);
	if (err == nullptr) {
		std::sort(result->TypingsFiles.begin(), result->TypingsFiles.end());
		std::sort(result->FilesToWatch.begin(), result->FilesToWatch.end());
		logging::log(request->Logger,
		             "ATA:: Got install request for: " +
		                 request->ProjectID->String());
	}
	return {std::move(result), err};
}

std::pair<std::unique_ptr<TypingsInstallResult>, gostd::Error>
TypingsInstaller::discoverAndInstallTypings(
    const TypingsInstallRequest* request) {
	init(request->ProjectID->String(), request->FS, request->Logger);

	auto discovered = DiscoverTypings(
	    request->FS, request->Logger, request->TypingsInfo,
	    request->FileNames, request->ProjectRootPath,
	    &packageNameToTypingLocation, typesRegistry);

	int32_t requestId = ++installRunCount;
	// install typings
	if (!discovered.newTypingNames.empty()) {
		auto filteredTypings =
		    filterTypings(request->Logger, discovered.newTypingNames);
		if (!filteredTypings.empty()) {
			auto [typingsFiles, err] =
			    installTypings(requestId, discovered.cachedTypingPaths,
			                   filteredTypings, request->Logger);
			if (err != nullptr) {
				return {nullptr, err};
			}
			auto result = std::make_unique<TypingsInstallResult>();
			result->TypingsFiles = std::move(typingsFiles);
			result->FilesToWatch = discovered.filesToWatch;
			return {std::move(result), nullptr};
		}
		logging::log(request->Logger,
		             "ATA:: All typings are known to be missing or invalid "
		             "- no need to install more typings");
	} else {
		logging::log(request->Logger,
		             "ATA:: No new typings were requested as a result of "
		             "typings discovery");
	}

	auto result = std::make_unique<TypingsInstallResult>();
	result->TypingsFiles = discovered.cachedTypingPaths;
	result->FilesToWatch = discovered.filesToWatch;
	return {std::move(result), nullptr};
	// !!! sheetal events to send
	// this.event(response, "setTypings");
}

std::pair<std::vector<std::string>, gostd::Error>
TypingsInstaller::installTypings(
    int32_t requestID,
    const std::vector<std::string>& currentlyCachedTypings,
    const std::vector<std::string>& filteredTypings,
    logging::Logger* logger) {
	// !!! sheetal events to send
	// send progress event
	// this.sendResponse({
	// 	kind: EventBeginInstallTypes,
	// 	eventId: requestId,
	// 	typingsInstallerVersion: version,
	// 	projectName: req.projectName,
	// } as BeginInstallTypes);

	// const body: protocol.BeginInstallTypesEventBody = {
	// 	eventId: response.eventId,
	// 	packages: response.packagesToInstall,
	// };
	// const eventName: protocol.BeginInstallTypesEventName = "beginInstallTypes";
	// this.event(body, eventName);

	std::vector<std::string> scopedTypings(filteredTypings.size());
	for (size_t i = 0; i < filteredTypings.size(); i++) {
		scopedTypings[i] =
		    gostd::sprintf("@types/%s@%s",
		                   {filteredTypings[i].c_str(),
		                    std::string(tsVersionToUse)}); // @tscore.VersionMajorMinor) // This is normally @tsVersionMajorMinor but for now lets use latest
	}

	auto [packageNames, ok] =
	    installWorker(requestID, scopedTypings, logger);
	if (ok) {
		logging::logf(logger, "ATA:: Installed typings %v",
		              fmtStringList(packageNames));
		std::vector<std::string> installedTypingFiles;
		CompilerOptions resolveOptions;
		resolveOptions.ModuleResolution = ModuleResolutionKind::NodeNext;
		std::unique_ptr<module::DefaultResolver> resolver(
		    module::NewResolver(module::ResolverOptions{
		        .Host = host,
		        .CompilerOptions = &resolveOptions,
		    }));
		for (const auto& packageName : filteredTypings) {
			std::string typingFile =
			    typingToFileName(resolver.get(), packageName);
			if (typingFile.empty()) {
				logging::logf(
				    logger,
				    "ATA:: Failed to find typing file for "
				    "package '%s'",
				    packageName.c_str());
				missingTypingsSet.Store(packageName, true);
				continue;
			}

			// packageName is guaranteed to exist in typesRegistry by
			// filterTypings
			auto regEntry = typesRegistry.find(packageName);
			static const std::unordered_map<std::string, std::string>
			    emptyDistTags;
			const auto& distTags = regEntry != typesRegistry.end()
			                         ? regEntry->second
			                         : emptyDistTags;
			std::string useVersion;
			auto tagsIt = distTags.find(
			    "ts" + std::string(versionMajorMinor()));
			if (tagsIt != distTags.end()) {
				useVersion = tagsIt->second;
			} else if (auto latestIt = distTags.find("latest");
			           latestIt != distTags.end()) {
				useVersion = latestIt->second;
			}
			auto newTyping = std::make_shared<CachedTyping>();
			newTyping->TypingsLocation = typingFile;
			newTyping->Version = std::make_shared<semver::Version>(
			    semver::MustParse(useVersion));
			packageNameToTypingLocation.Store(packageName, newTyping);
			installedTypingFiles.push_back(typingFile);
		}
		logging::logf(logger, "ATA:: Installed typing files %v",
		              fmtStringList(installedTypingFiles));

		auto out = currentlyCachedTypings;
		out.insert(out.end(), installedTypingFiles.begin(),
		           installedTypingFiles.end());
		return {std::move(out), nullptr};
	}

	// DO we really need these events
	// this.event(response, "setTypings");
	logging::logf(logger,
	              "ATA:: install request failed, marking packages as "
	              "missing to prevent repeated requests: %v",
	              fmtStringList(filteredTypings));
	for (const auto& typing : filteredTypings) {
		missingTypingsSet.Store(typing, true);
	}

	return {{}, gostd::newError("npm install failed")};

	// !!! sheetal events to send
	// const response: EndInstallTypes = {
	// 	kind: EventEndInstallTypes,
	// 	eventId: requestId,
	// 	projectName: req.projectName,
	// 	packagesToInstall: scopedTypings,
	// 	installSuccess: ok,
	// 	typingsInstallerVersion: version,
	// };
	// this.sendResponse(response);

	// if (this.telemetryEnabled) {
	// 	const body: protocol.TypingsInstalledTelemetryEventBody = {
	// 		telemetryEventName: "typingsInstalled",
	// 		payload: {
	// 			installedPackages: response.packagesToInstall.join(","),
	// 			installSuccess: response.installSuccess,
	// 			typingsInstallerVersion: response.typingsInstallerVersion,
	// 		},
	// 	};
	// 	const eventName: protocol.TelemetryEventName = "telemetry";
	// 	this.event(body, eventName);
	// }

	// const body: protocol.EndInstallTypesEventBody = {
	// 	eventId: response.eventId,
	// 	success: response.installSuccess,
	// };
	// const eventName: protocol.EndInstallTypesEventName = "endInstallTypes";
}

std::pair<std::vector<std::string>, bool> TypingsInstaller::installWorker(
    int32_t requestId, const std::vector<std::string>& packageNames,
    logging::Logger* logger) {
	logging::logf(logger, "ATA:: #%d with cwd: %s arguments: %v", requestId,
	              typingsLocation.c_str(), fmtStringList(packageNames));
	auto ctx = gostd::contextBackground();
	gostd::Error err = installNpmPackages(
	    ctx, packageNames, &concurrencySemaphore,
	    [this, logger](const std::vector<std::string>& packageNames)
	        -> gostd::Error {
		    std::vector<std::string> npmArgs;
		    npmArgs.push_back("install");
		    npmArgs.push_back("--ignore-scripts");
		    npmArgs.insert(npmArgs.end(), packageNames.begin(),
		                   packageNames.end());
		    npmArgs.push_back("--save-dev");
		    npmArgs.push_back("--user-agent=\"typesInstaller/" +
		                      std::string(version()) + "\"");
		    auto [output, err] =
		        host->NpmInstall(typingsLocation, npmArgs);
		    if (err != nullptr) {
			    logging::logf(logger, "ATA:: Output is: %s",
			                  output.c_str());
			    return err;
		    }
		    return nullptr;
	    });
	logging::logf(logger, "TI:: npm install #%d completed", requestId);
	return {packageNames, err == nullptr};
}

std::vector<std::string> TypingsInstaller::filterTypings(
    logging::Logger* logger,
    const std::vector<std::string>& typingsToInstall) {
	std::vector<std::string> result;
	for (const auto& typing : typingsToInstall) {
		std::string typingKey = module::MangleScopedPackageName(typing);
		if (missingTypingsSet.Load(typingKey).second) {
			logging::logf(logger,
			              "ATA:: '%s':: '%s' is in missingTypingsSet - "
			              "skipping...",
			              typing.c_str(), typingKey.c_str());
			continue;
		}
		auto validationResult = ValidatePackageName(typing);
		if (validationResult.result != NameOk) {
			// add typing name to missing set so we won't process it again
			missingTypingsSet.Store(typingKey, true);
			logging::log(logger,
			             "ATA:: " + renderPackageNameValidationFailure(
			                 typing, validationResult.result,
			                 validationResult.name,
			                 validationResult.isScopeName));
			continue;
		}
		auto regIt = typesRegistry.find(typingKey);
		if (regIt == typesRegistry.end()) {
			logging::logf(logger,
			              "ATA:: '%s':: Entry for package '%s' does not "
			              "exist in local types registry - skipping...",
			              typing.c_str(), typingKey.c_str());
			continue;
		}
		auto [typingLocation, ok] =
		    packageNameToTypingLocation.Load(typingKey);
		if (ok && isTypingUpToDate(typingLocation.get(), regIt->second)) {
			logging::logf(logger,
			              "ATA:: '%s':: '%s' already has an up-to-date "
			              "typing - skipping...",
			              typing.c_str(), typingKey.c_str());
			continue;
		}
		result.push_back(typingKey);
	}
	return result;
}

void TypingsInstaller::init(const std::string& projectID, vfs::FS* fs,
                            logging::Logger* logger) {
	std::call_once(initOnce, [this, &projectID, fs, logger] {
		logging::log(logger, "ATA:: Global cache location '" +
		                         typingsLocation +
		                         "'"); //, safe file path '" + safeListPath + "', types map path '" + typesMapLocation + "`")
		processCacheLocation(projectID, fs, logger);

		// !!! sheetal handle npm path here if we would support it
		//     // If the NPM path contains spaces and isn't wrapped in quotes, do so.
		//     if (this.npmPath.includes(" ") && this.npmPath[0] !== `"`) {
		//         this.npmPath = `"${this.npmPath}"`;
		//     }
		//     if (this.log.isEnabled()) {
		//         this.log.writeLine(`Process id: ${process.pid}`);
		//         this.log.writeLine(`NPM location: ${this.npmPath} (explicit '${ts.server.Arguments.NpmLocation}' ${npmLocation === undefined ? "not " : ""} provided)`);
		//         this.log.writeLine(`validateDefaultNpmLocation: ${validateDefaultNpmLocation}`);
		//     }

		ensureTypingsLocationExists(fs, logger);
		logging::log(logger,
		             "ATA:: Updating types-registry@latest npm package...");
		auto [_, err] = host->NpmInstall(
		    typingsLocation,
		    {"install", "--ignore-scripts", "types-registry@latest"});
		if (err == nullptr) {
			logging::log(logger,
			             "ATA:: Updated types-registry npm package");
		} else {
			logging::logf(logger,
			              "ATA:: Error updating types-registry package: %v",
			              err);
			// !!! sheetal events to send
			//         // store error info to report it later when it is known that server is already listening to events from typings installer
			//         this.delayedInitializationError = {
			//             kind: "event::initializationFailed",
			//             message: (e as Error).message,
			//             stack: (e as Error).stack,
			//         };

			// const body: protocol.TypesInstallerInitializationFailedEventBody = {
			// 	message: response.message,
			// };
			// const eventName: protocol.TypesInstallerInitializationFailedEventName = "typesInstallerInitializationFailed";
			// this.event(body, eventName);
		}

		typesRegistry = loadTypesRegistryFile(fs, logger);
	});
}

void TypingsInstaller::processCacheLocation(const std::string& projectID,
                                            vfs::FS* fs,
                                            logging::Logger* logger) {
	logging::log(logger, "ATA:: Processing cache location " + typingsLocation);
	std::string packageJson =
	    tspath::combinePaths(typingsLocation, {"package.json"});
	std::string packageLockJson =
	    tspath::combinePaths(typingsLocation, {"package-lock.json"});
	logging::log(logger, "ATA:: Trying to find '" + packageJson + "'...");
	if (fs->FileExists(packageJson) && fs->FileExists(packageLockJson)) {
		std::vector<std::string> devDeps;
		std::string npmConfigContents = parseNpmConfigOrLock(
		    fs, logger, packageJson, &devDeps, nullptr);
		npmLockData npmLock;
		std::string npmLockContents = parseNpmConfigOrLock(
		    fs, logger, packageLockJson, nullptr, &npmLock);

		logging::log(logger, "ATA:: Loaded content of " + packageJson +
		                         ": " + npmConfigContents);
		logging::log(logger, "ATA:: Loaded content of " + packageLockJson +
		                         ": " + npmLockContents);

		// !!! sheetal strada uses Node10
		CompilerOptions resolveOptions;
		resolveOptions.ModuleResolution = ModuleResolutionKind::NodeNext;
		std::unique_ptr<module::DefaultResolver> resolver(
		    module::NewResolver(module::ResolverOptions{
		        .Host = host,
		        .CompilerOptions = &resolveOptions,
		    }));
		if (!devDeps.empty() &&
		    (!npmLock.packages.empty() || !npmLock.dependencies.empty())) {
			for (const auto& key : devDeps) {
				std::string npmLockValue;
				bool npmLockValueExists;
				auto pkgIt =
				    npmLock.packages.find("node_modules/" + key);
				if (pkgIt != npmLock.packages.end()) {
					npmLockValue = pkgIt->second;
					npmLockValueExists = true;
				} else {
					auto depIt = npmLock.dependencies.find(key);
					if (depIt != npmLock.dependencies.end()) {
						npmLockValue = depIt->second;
						npmLockValueExists = true;
					} else {
						npmLockValueExists = false;
					}
				}
				if (!npmLockValueExists) {
					// if package in package.json but not package-lock.json,
					// skip adding to cache so it is reinstalled on next use
					continue;
				}
				// key is @types/<package name>
				std::string packageName{
				    tspath::getBaseFileName(key)};
				if (packageName.empty()) {
					continue;
				}
				std::string typingFile =
				    typingToFileName(resolver.get(), packageName);
				if (typingFile.empty()) {
					missingTypingsSet.Store(packageName, true);
					continue;
				}
				auto [existingTypingFile, existingTypingsFilePresent] =
				    packageNameToTypingLocation.Load(packageName);
				if (existingTypingsFilePresent) {
					if (existingTypingFile->TypingsLocation ==
					    typingFile) {
						continue;
					}
					logging::log(
					    logger,
					    "ATA:: New typing for package " + packageName +
					        " from " + typingFile +
					        " conflicts with existing typing file " +
					        existingTypingFile->TypingsLocation);
				}
				logging::log(logger, "ATA:: Adding entry into typings "
				                     "cache: " +
				                         packageName + " => " + typingFile);
				const std::string& version = npmLockValue;
				if (version.empty()) {
					continue;
				}
				auto newTyping = std::make_shared<CachedTyping>();
				newTyping->TypingsLocation = typingFile;
				newTyping->Version = std::make_shared<semver::Version>(
				    semver::MustParse(version));
				packageNameToTypingLocation.Store(packageName, newTyping);
			}
		}
	}
	logging::log(logger, "ATA:: Finished processing cache location " +
	                         typingsLocation);
}

void TypingsInstaller::ensureTypingsLocationExists(vfs::FS* fs,
                                                   logging::Logger* logger) {
	std::string npmConfigPath =
	    tspath::combinePaths(typingsLocation, {"package.json"});
	logging::log(logger, "ATA:: Npm config file: " + npmConfigPath);

	if (!fs->FileExists(npmConfigPath)) {
		logging::logf(logger,
		              "ATA:: Npm config file: '%s' is missing, creating "
		              "new one...",
		              npmConfigPath.c_str());
		vfs::Error err =
		    fs->WriteFile(npmConfigPath, "{ \"private\": true }");
		if (err) {
			logging::logf(logger, "ATA:: Npm config file write failed: %v",
			              err.str());
		}
	}
}

std::string TypingsInstaller::typingToFileName(
    module::DefaultResolver* resolver, const std::string& packageName) {
	auto [result, _diags] =
	    resolver->ResolveModuleName(packageName,
	                                tspath::combinePaths(
	                                    typingsLocation, {"index.d.ts"}),
	                                ModuleKind::None, nullptr);
	return result->ResolvedFileName;
}

std::unordered_map<std::string,
                   std::unordered_map<std::string, std::string>>
TypingsInstaller::loadTypesRegistryFile(vfs::FS* fs,
                                        logging::Logger* logger) {
	std::string typesRegistryFile = tspath::combinePaths(
	    typingsLocation, {"node_modules/types-registry/index.json"});
	auto [typesRegistryFileContents, ok] = fs->ReadFile(typesRegistryFile);
	if (ok) {
		// entries map[string]map[string]map[string]string
		auto [dom, err] = json::parse(typesRegistryFileContents);
		if (err == nullptr) {
			if (const json::Dom* entries = json::objGet(dom, "entries");
			    entries != nullptr && entries->kind == json::Dom::K::Object) {
				std::unordered_map<
				    std::string,
				    std::unordered_map<std::string, std::string>>
				    typesRegistryOut;
				bool bad = false;
				for (const auto& [pkg, tagsDom] : entries->obj) {
					if (tagsDom.kind != json::Dom::K::Object) {
						bad = true;
						break;
					}
					auto& tags = typesRegistryOut[pkg];
					for (const auto& [tag, vDom] : tagsDom.obj) {
						if (vDom.kind != json::Dom::K::String) {
							bad = true;
							break;
						}
						auto [s, serr] = json::asString(vDom, "string");
						if (serr != nullptr) {
							bad = true;
							break;
						}
						tags[tag] = s;
					}
					if (bad) break;
				}
				if (!bad) {
					return typesRegistryOut;
				}
				err = gostd::newError(
				    "json: cannot unmarshal types registry");
			}
		}
		logging::logf(logger,
		              "ATA:: Error when loading types registry file "
		              "'%s': %v",
		              typesRegistryFile.c_str(), err);
	} else {
		logging::logf(logger,
		              "ATA:: Error reading types registry file '%s'",
		              typesRegistryFile.c_str());
	}
	return {};
}

} // namespace tsc::ata
