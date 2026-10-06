// === slice: ls-autoimport ===
// registry.go — the autoimport registry: per-project and per-node_modules
// "buckets" of exportable symbols, built lazily and dirtied by file edits.
//
// Concurrency: Go uses sync.WaitGroup (`wg.Go`) to parallelize discovery,
// extraction, and bucket building. We execute those bodies sequentially on
// this thread — the same semantics with deterministic ordering; the mutexes
// the Go code held for shared maps are then trivially satisfied. Note at
// call sites: `wg.Go(func() { ... })` → the body runs inline.
//
// Ownership: Registry maps hold raw `directory*`/`RegistryBucket*`
// (GC-shared in Go, shared by clones via the dirty maps). We never delete
// them — the buckets are immutable once finalized and may be shared between
// an old and new registry — so ~Registry is a no-op by design.

#include "internal/ls/autoimport/autoimport.h"

#include <algorithm>
#include <thread>
#include <unordered_set>
#include <utility>

#include "internal/binder/binder.h"
#include "internal/module/util.h"

namespace tsc::ls::autoimport {

namespace {

// core.EmptyCompilerOptions — core/compileroptions.go:178.
const CompilerOptions* emptyCompilerOptions() {
	static const CompilerOptions opts;
	return &opts;
}

// core.UnorderedEqual — core.go:812.
bool unorderedEqualStrings(const std::vector<std::string>& s1,
                           const std::vector<std::string>& s2) {
	if (s1.size() != s2.size()) {
		return false;
	}
	std::unordered_multiset<std::string> m(s1.begin(), s1.end());
	for (const auto& x : s2) {
		auto it = m.find(x);
		if (it == m.end()) {
			return false;
		}
		m.erase(it);
	}
	return true;
}

// (p Path) ContainsPath — tspath/path.go:1087. Paths are already rooted,
// reduced, and case-canonicalized, so this is a plain prefix check.
bool pathContainsPath(const tspath::Path& p, const tspath::Path& child) {
	if (p.empty()) {
		return false;
	}
	return p == child ||
	       (child.size() > p.size() &&
	        child.compare(0, p.size(), p) == 0 &&
	        (p.back() == '/' || child[p.size()] == '/'));
}

}  // namespace

// === registry.go:36 ===

const collections::Set<std::string> knownRecursiveSearchPackages{
    "@material-ui/core",
    "@material-ui/icons",
    "@sap/cds",
    "@testing-library/react-native",
    "ajv",
    "asap",
    "async",
    "aws-sdk",
    "braintree-web",
    "core-js",
    "core-js-pure",
    "crypto-js",
    "cypress-mochawesome-reporter",
    "dd-trace",
    "dumi",
    "dva",
    "egg-mock",
    "electron-log",
    "es-abstract",
    "es6-promise",
    "eslint-config-taro",
    "expo",
    "expo-router",
    "flow-remove-types",
    "gatsby",
    "glamor",
    "gluegun",
    "graphology-indices",
    "graphology-traversal",
    "graphology-utils",
    "jest-expo",
    "lodash",
    "lodash-es",
    "moment",
    "mz",
    "next",
    "pdfjs-dist",
    "protobufjs",
    "react-app-polyfill",
    "react-dev-utils",
    "react-devtools-inline",
    "recast",
    "semver",
    "stylelint-config-html",
    "umi",
    "web3-provider-engine",
    "webpack",
};

// bucketBuildPreferences — registry.go:98.
bucketBuildPreferences bucketBuildPreferencesFromUserPreferences(
    const lsutil::UserPreferences& prefs) {
	return {prefs.AutoImportFileExcludePatterns,
	        prefs.AutoImportEntrypointDirectorySearch};
}

bool bucketBuildPreferences::Equal(const bucketBuildPreferences& other) const {
	return unorderedEqualStrings(fileExcludePatterns,
	                             other.fileExcludePatterns) &&
	       autoImportEntrypointDirectorySearch ==
	           other.autoImportEntrypointDirectorySearch;
}

bucketBuildPreferences bucketBuildPreferences::Clone() const {
	return *this;
}

// BucketState — registry.go:129.
BucketState BucketState::Clone() const {
	BucketState b = *this;
	b.buildPreferences = buildPreferences.Clone();
	b.dirtyPackages =
	    dirtyPackages ? std::make_shared<collections::Set<std::string>>(
	                        dirtyPackages->Clone())
	                  : nullptr;
	b.recursiveSearchPackages =
	    recursiveSearchPackages
	        ? std::make_shared<collections::Set<std::string>>(
	              recursiveSearchPackages->Clone())
	        : nullptr;
	return b;
}

bool BucketState::Dirty() const {
	return multipleFilesDirty || !dirtyFile.empty() ||
	       newProgramStructure > 0 ||
	       (dirtyPackages != nullptr && dirtyPackages->Len() > 0);
}

tspath::Path BucketState::DirtyFile() const {
	if (multipleFilesDirty) {
		return "";
	}
	return dirtyFile;
}

collections::Set<std::string>* BucketState::DirtyPackages() const {
	if (multipleFilesDirty) {
		return nullptr;
	}
	return dirtyPackages.get();
}

collections::Set<std::string>* BucketState::RecursiveSearchPackages() const {
	return recursiveSearchPackages.get();
}

bool BucketState::possiblyNeedsRebuildForFile(
    const tspath::Path& file,
    const lsutil::UserPreferences& preferences) const {
	return newProgramStructure > 0 || hasDirtyFileBesides(file) ||
	       !buildPreferences.Equal(
	           bucketBuildPreferencesFromUserPreferences(preferences)) ||
	       (dirtyPackages != nullptr && dirtyPackages->Len() > 0);
}

bool BucketState::hasDirtyFileBesides(const tspath::Path& file) const {
	return multipleFilesDirty ||
	       (!dirtyFile.empty() && dirtyFile != file);
}

// recursiveSearchSubset — registry.go:200. nil represents "all packages" —
// a superset of every concrete set.
bool recursiveSearchSubset(collections::Set<std::string>* target,
                           collections::Set<std::string>* current) {
	if (target == nullptr) {
		return current == nullptr;
	}
	if (current == nullptr) {
		return true;
	}
	return target->IsSubsetOf(*current);
}

// RegistryBucket — registry.go:212.
RegistryBucket* RegistryBucket::Clone() const {
	auto* b = new RegistryBucket();
	b->state = state.Clone();
	b->Paths = Paths;
	b->PackageFiles = PackageFiles;
	b->ResolvedPackageNames = ResolvedPackageNames;
	b->DependencyNames = DependencyNames;
	b->AmbientModuleNames = AmbientModuleNames;
	b->index = index ? std::make_unique<Index>(*index) : nullptr;
	return b;
}

// newRegistryBucket — registry.go:263.
RegistryBucket* newRegistryBucket() {
	auto* b = new RegistryBucket();
	b->state.multipleFilesDirty = true;
	b->state.newProgramStructure = newProgramStructureDifferentFileNames;
	return b;
}

// markProjectFileDirty — registry.go:287. Should only be called within a
// Change call on the dirty map.
void RegistryBucket::markProjectFileDirty(const tspath::Path& file) {
	if (state.hasDirtyFileBesides(file)) {
		state.multipleFilesDirty = true;
	} else {
		state.dirtyFile = file;
	}
}

// markNodeModulesDirty — registry.go:299. If packageName is non-empty, that
// package is marked for granular update; otherwise the whole bucket is
// marked dirty.
void RegistryBucket::markNodeModulesDirty(const std::string& packageName) {
	if (state.multipleFilesDirty) {
		return;
	}
	if (packageName.empty()) {
		state.multipleFilesDirty = true;
		return;
	}
	if (state.dirtyPackages == nullptr) {
		state.dirtyPackages =
		    std::make_shared<collections::Set<std::string>>();
	}
	state.dirtyPackages->Add(packageName);
}

// directory — registry.go:314.
directory* directory::Clone() const {
	return new directory(*this);
}

// NewRegistry — registry.go:346.
std::unique_ptr<Registry> NewRegistry(
    std::function<tspath::Path(const std::string&)> toPath,
    lsutil::UserPreferences preferences) {
	auto r = std::make_unique<Registry>();
	r->toPath = std::move(toPath);
	r->userPreferences = preferences;
	return r;
}

// ~Registry — see header comment on ownership: buckets and directories are
// shared across clones, so we intentionally do not free them here.
Registry::~Registry() = default;

// IsPreparedForImportingFile — registry.go:354.
bool Registry::IsPreparedForImportingFile(
    const std::string& fileName, ProjectID* projectID,
    const lsutil::UserPreferences& preferences) {
	auto it = projects.find(projectID);
	if (it == projects.end()) {
		return false;
	}
	RegistryBucket* projectBucket = it->second;
	tspath::Path path = toPath(fileName);
	if (projectBucket->state.possiblyNeedsRebuildForFile(path, preferences)) {
		return false;
	}

	tspath::Path dirPath = tspath::getDirectoryPath(path);
	for (;;) {
		auto dirIt = nodeModules.find(dirPath);
		if (dirIt != nodeModules.end()) {
			if (dirIt->second->state.possiblyNeedsRebuildForFile(path,
			                                                   preferences)) {
				return false;
			}
		}
		tspath::Path parent = tspath::getDirectoryPath(dirPath);
		if (parent == dirPath) {
			break;
		}
		dirPath = std::move(parent);
	}
	return true;
}

// NodeModulesDirectories — registry.go:383.
std::unordered_map<tspath::Path, std::string>
Registry::NodeModulesDirectories() {
	std::unordered_map<tspath::Path, std::string> dirs;
	for (auto& [dirPath, dir] : directories) {
		if (dir->hasNodeModules) {
			dirs[tspath::combinePaths(dirPath, {"node_modules"})] =
			    tspath::combinePaths(dir->name, {"node_modules"});
		}
	}
	return dirs;
}

// Clone — registry.go:393. Clones the registry, applying `change` lazily
// through the dirty maps so untouched buckets are shared.
std::pair<std::unique_ptr<Registry>, gostd::Error> Registry::Clone(
    gostd::Context ctx, RegistryChange& change, RegistryCloneHost* host,
    logging::LogTree* logger) {
	auto start = std::chrono::steady_clock::now();
	if (logger != nullptr) {
		logger = logging::fork(logger, "Building autoimport registry");
	}
	auto builder = newRegistryBuilder(this, host);
	if (change.UserPreferences != nullptr) {
		builder->userPreferences = *change.UserPreferences;
		if (!unorderedEqualStrings(
		        builder->userPreferences.AutoImportSpecifierExcludeRegexes,
		        userPreferences.AutoImportSpecifierExcludeRegexes)) {
			builder->specifierCache->Clear();
		}
	}
	builder->updateBucketAndDirectoryExistence(change, logger);
	builder->markBucketsDirty(change, logger);
	if (!change.RequestedFile.empty()) {
		builder->updateIndexes(ctx, change, logger);
	}
	if (logger != nullptr) {
		logging::logf(logger, "Built autoimport registry in %v",
		             gostd::durationString(std::chrono::steady_clock::now() - start));
	}
	Registry* registry = builder->Build();
	return {std::unique_ptr<Registry>(registry), {}};
}

// GetCacheStats — registry.go:432.
CacheStats* Registry::GetCacheStats() {
	auto* stats = new CacheStats();
	stats->UniquePackageCount = uniquePackageCount;

	for (auto& [projectID, bucket] : projects) {
		int exportCount = 0;
		if (bucket->index != nullptr) {
			exportCount = static_cast<int>(bucket->index->entries.size());
		}
		stats->ProjectBuckets.push_back(
		    BucketStats{projectID->String(), exportCount,
		                static_cast<int>(bucket->Paths.size()),
		                bucket->state, bucket->DependencyNames.get(),
		                nullptr});
	}

	for (auto& [path, bucket] : nodeModules) {
		int exportCount = 0;
		if (bucket->index != nullptr) {
			exportCount = static_cast<int>(bucket->index->entries.size());
		}
		// Derive PackageNames from PackageFiles keys
		auto packageNames = std::make_unique<collections::Set<std::string>>(
		    bucket->PackageFiles.size());
		int fileCount = 0;
		for (auto& [name, paths] : bucket->PackageFiles) {
			packageNames->Add(name);
			fileCount += static_cast<int>(paths.size());
		}
		stats->NodeModulesBuckets.push_back(BucketStats{
		    path, exportCount, fileCount, bucket->state,
		    bucket->DependencyNames.get(), packageNames.release()});
	}

	std::sort(stats->ProjectBuckets.begin(), stats->ProjectBuckets.end(),
	          [](const BucketStats& a, const BucketStats& b) {
		          return a.Name < b.Name;
	          });
	std::sort(stats->NodeModulesBuckets.begin(),
	          stats->NodeModulesBuckets.end(),
	          [](const BucketStats& a, const BucketStats& b) {
		          return a.Name < b.Name;
	          });

	return stats;
}

// newRegistryBuilder — registry.go:525.
std::unique_ptr<registryBuilder> newRegistryBuilder(Registry* registry,
                                                    RegistryCloneHost* host) {
	auto b = std::make_unique<registryBuilder>();
	b->host = host;
	b->base = registry;
	b->userPreferences = registry->userPreferences;
	b->directories =
	    std::unique_ptr<dirty::Map<tspath::Path, directory*>>(
	        dirty::newMap(registry->directories));
	b->nodeModules =
	    std::unique_ptr<dirty::Map<tspath::Path, RegistryBucket*>>(
	        dirty::newMap(registry->nodeModules));
	b->projects = std::unique_ptr<dirty::Map<ProjectID*, RegistryBucket*>>(
	    dirty::newMap(registry->projects));
	auto identityIn = [](const std::shared_ptr<
	                        collections::SyncMap<tspath::Path, std::string>>& v) {
		return v;
	};
	auto identityOut = identityIn;
	b->specifierCache = std::unique_ptr<dirty::MapBuilder<
	    tspath::Path,
	    std::shared_ptr<collections::SyncMap<tspath::Path, std::string>>,
	    std::shared_ptr<collections::SyncMap<tspath::Path, std::string>>>>(
	    dirty::newMapBuilder<
	        tspath::Path,
	        std::shared_ptr<collections::SyncMap<tspath::Path, std::string>>,
	        std::shared_ptr<collections::SyncMap<tspath::Path, std::string>>>(
	        registry->specifierCache, identityIn, identityOut));
	b->uniquePackageCount = registry->uniquePackageCount;
	auto identityVec = [](const std::vector<
	                      std::shared_ptr<module::ResolvedEntrypoint>>& v) {
		return v;
	};
	auto identityVecOut = identityVec;
	b->entrypoints = std::unique_ptr<dirty::MapBuilder<
	    tspath::Path, std::vector<std::shared_ptr<module::ResolvedEntrypoint>>,
	    std::vector<std::shared_ptr<module::ResolvedEntrypoint>>>>(
	    dirty::newMapBuilder<
	        tspath::Path,
	        std::vector<std::shared_ptr<module::ResolvedEntrypoint>>,
	        std::vector<std::shared_ptr<module::ResolvedEntrypoint>>>(
	        registry->entrypoints, identityVec, identityVecOut));
	return b;
}

// Build — registry.go:540.
Registry* registryBuilder::Build() {
	auto r = std::make_unique<Registry>();
	r->toPath = base->toPath;
	r->userPreferences = userPreferences;
	r->directories = directories->Finalize().first;
	r->nodeModules = nodeModules->Finalize().first;
	r->projects = projects->Finalize().first;
	r->specifierCache = specifierCache->Build();
	r->uniquePackageCount = uniquePackageCount;
	r->entrypoints = entrypoints->Build();
	return r.release();
}

// updateBucketAndDirectoryExistence — registry.go:553.
void registryBuilder::updateBucketAndDirectoryExistence(RegistryChange& change,
                                                      logging::LogTree* logger) {
	auto start = std::chrono::steady_clock::now();
	std::unordered_set<ProjectID*> neededProjects;
	std::unordered_map<tspath::Path, std::string> neededDirectories;
	for (auto& [path, fileName] : change.OpenFiles) {
		auto [projectID, _program] = host->GetDefaultProject(path);
		if (projectID != nullptr) {
			neededProjects.insert(projectID);
		}
		if (tspath::isDynamicFileName(fileName)) {
			continue;
		}
		std::string dir = fileName;
		tspath::Path dirPath = path;
		for (;;) {
			dir = tspath::getDirectoryPath(dir);
			tspath::Path lastDirPath = dirPath;
			dirPath = tspath::getDirectoryPath(dirPath);
			if (dirPath == lastDirPath) {
				break;
			}
			if (neededDirectories.count(dirPath)) {
				break;
			}
			neededDirectories[dirPath] = dir;
		}

		if (!specifierCache->Has(path)) {
			specifierCache->Set(
			    path, std::make_shared<
			              collections::SyncMap<tspath::Path, std::string>>());
		}
	}

	if (!change.RequestedFile.empty()) {
		auto [projectID, _program] =
		    host->GetDefaultProject(change.RequestedFile);
		if (projectID != nullptr) {
			neededProjects.insert(projectID);
		}
		if (!specifierCache->Has(change.RequestedFile)) {
			specifierCache->Set(
			    change.RequestedFile,
			    std::make_shared<
			        collections::SyncMap<tspath::Path, std::string>>());
		}
	}

	for (auto& [path, _sm] : base->specifierCache) {
		if (change.OpenFiles.find(path) == change.OpenFiles.end() &&
		    path != change.RequestedFile) {
			specifierCache->Delete(path);
		}
	}

	std::vector<ProjectID*> addedProjects, removedProjects;
	// core.DiffMapsFunc(base.projects, neededProjects, ...)
	for (auto& [projectID, bucket] : base->projects) {
		if (!neededProjects.count(projectID)) {
			projects->Delete(projectID);
			removedProjects.push_back(projectID);
		}
	}
	for (ProjectID* projectID : neededProjects) {
		if (!base->projects.count(projectID)) {
			projects->Add(projectID, newRegistryBucket());
			addedProjects.push_back(projectID);
		}
	}
	if (logger != nullptr) {
		for (ProjectID* projectID : addedProjects) {
			logging::logf(logger, "Added project: %s", projectID->String());
		}
		for (ProjectID* projectID : removedProjects) {
			logging::logf(logger, "Removed project: %s", projectID->String());
		}
	}

	auto packageJsonChanged = [&](const std::string& dirName) {
		lsp::lsproto::DocumentUri uri = lsconv::FileNameToDocumentURI(
		    tspath::combinePaths(dirName, {"package.json"}));
		return change.Changed.Has(uri) || change.Deleted.Has(uri) ||
		       change.Created.Has(uri);
	};

	std::function<void(const tspath::Path&, const std::string&, bool)>
	    updateDirectory =
	        [&](const tspath::Path& dirPath, const std::string& dirName,
	            bool packageJsonChangedForDir) {
		        std::string packageJsonFileName =
		            tspath::combinePaths(dirName, {"package.json"});
		        bool hasNodeModules = host->FS()->DirectoryExists(
		            tspath::combinePaths(dirName, {"node_modules"}));
		        auto [entry, ok] = directories->Get(dirPath);
		        if (ok) {
			        entry->ChangeIf(
			            [&](directory* dir) {
				            return packageJsonChangedForDir ||
				                   dir->hasNodeModules != hasNodeModules;
			            },
			            [&](directory* dir) {
				            dir->packageJson =
				                host->GetPackageJson(packageJsonFileName);
				            dir->hasNodeModules = hasNodeModules;
			            });
		        } else {
			        auto* dir = new directory();
			        dir->name = dirName;
			        dir->packageJson =
			            host->GetPackageJson(packageJsonFileName);
			        dir->hasNodeModules = hasNodeModules;
			        directories->Add(dirPath, dir);
		        }

		        if (hasNodeModules) {
			        if (!nodeModules->Get(dirPath).second) {
				        nodeModules->Add(dirPath, newRegistryBucket());
			        }
		        } else {
			        nodeModules->TryDelete(dirPath);
		        }
	        };

	std::vector<tspath::Path> addedNodeModulesDirs, removedNodeModulesDirs;
	// core.DiffMapsFunc(base.directories, neededDirectories, ...) —
	// onAdded: need and don't have; onRemoved: have and don't need;
	// onChanged: unchanged check short-circuits via equalValues.
	for (auto& [dirPath, dirName] : neededDirectories) {
		auto it = base->directories.find(dirPath);
		if (it == base->directories.end()) {
			bool hadNodeModules =
			    base->nodeModules.count(dirPath) &&
			    base->nodeModules[dirPath] != nullptr;
			updateDirectory(dirPath, dirName, false);
			if (logger != nullptr) {
				logging::logf(logger, "Added directory: %s", dirPath);
			}
			if (nodeModules->Get(dirPath).second && !hadNodeModules) {
				addedNodeModulesDirs.push_back(dirPath);
			}
		}
	}
	for (auto& [dirPath, dir] : base->directories) {
		auto ndIt = neededDirectories.find(dirPath);
		if (ndIt == neededDirectories.end()) {
			bool hadNodeModules =
			    base->nodeModules.count(dirPath) &&
			    base->nodeModules[dirPath] != nullptr;
			directories->Delete(dirPath);
			nodeModules->TryDelete(dirPath);
			if (logger != nullptr) {
				logging::logf(logger, "Removed directory: %s", dirPath);
			}
			if (hadNodeModules) {
				removedNodeModulesDirs.push_back(dirPath);
			}
		} else {
			const std::string& dirName = ndIt->second;
			// equalValues check: not changed when the package.json is
			// unchanged and hasNodeModules still matches.
			bool unchanged =
			    !packageJsonChanged(dirName) &&
			    dir->hasNodeModules ==
			        host->FS()->DirectoryExists(
			            tspath::combinePaths(dirName, {"node_modules"}));
			if (!unchanged) {
				updateDirectory(dirPath, dirName,
				                packageJsonChanged(dirName));
				if (logger != nullptr) {
					logging::logf(logger, "Changed directory: %s", dirPath);
				}
			}
		}
	}

	if (logger != nullptr) {
		for (auto& dirPath : addedNodeModulesDirs) {
			logging::logf(logger, "Added node_modules bucket: %s", dirPath);
		}
		for (auto& dirPath : removedNodeModulesDirs) {
			logging::logf(logger, "Removed node_modules bucket: %s", dirPath);
		}
		logging::logf(logger, "Updated buckets and directories in %v",
		             gostd::durationString(std::chrono::steady_clock::now() - start));
	}
}

// markBucketsDirty — registry.go:707.
void registryBuilder::markBucketsDirty(RegistryChange& change,
                                       logging::LogTree* logger) {
	(void)logger;
	// Mark new program structures
	for (auto& kv : change.RebuiltPrograms) {
		ProjectID* projectID = kv.first;
		bool newFileNames = kv.second;
		auto [bucket, ok] = projects->Get(projectID);
		if (ok) {
			bucket->Change([newFileNames](RegistryBucket* b) {
				b->state.newProgramStructure =
				    newFileNames ? newProgramStructureDifferentFileNames
				                 : newProgramStructureSameFileNames;
			});
		}
	}

	// Mark files dirty, bailing out if all buckets already have multiple
	// files dirty
	std::unordered_set<tspath::Path> cleanNodeModulesBuckets;
	std::unordered_set<ProjectID*> cleanProjectBuckets;
	nodeModules->Range(
	    [&](dirty::MapEntry<tspath::Path, RegistryBucket*>* entry) {
		    if (!entry->Value()->state.multipleFilesDirty) {
			    cleanNodeModulesBuckets.insert(entry->Key());
		    }
		    return true;
	    });
	projects->Range(
	    [&](dirty::MapEntry<ProjectID*, RegistryBucket*>* entry) {
		    if (!entry->Value()->state.multipleFilesDirty) {
			    cleanProjectBuckets.insert(entry->Key());
		    }
		    return true;
	    });

	auto markFilesDirty =
	    [&](const std::unordered_set<lsp::lsproto::DocumentUri>& uris) {
		    if (cleanNodeModulesBuckets.empty() &&
		        cleanProjectBuckets.empty()) {
			    return;
		    }
		    for (const auto& uri : uris) {
			    tspath::Path path =
			        base->toPath(lsp::lsproto::documentUriFileName(uri));
			    if (!cleanNodeModulesBuckets.empty()) {
				    // For node_modules, mark the bucket dirty if anything
				    // changes in the directory. The path could be either a
				    // symlink path (containing /node_modules/) or a realpath
				    // (for symlinked project references). Both are recorded
				    // in Paths for granular updates.
				    size_t nodeModulesIndex = path.find("/node_modules/");
				    if (nodeModulesIndex != std::string::npos) {
					    tspath::Path dirPath =
					        path.substr(0, nodeModulesIndex);
					    if (cleanNodeModulesBuckets.count(dirPath)) {
						    auto [entry, _] = nodeModules->Get(dirPath);
						    // Look up the package name for granular updates
						    std::string packageName;
						    auto pathsIt =
						        entry->Value()->Paths.find(path);
						    if (pathsIt != entry->Value()->Paths.end()) {
							    packageName = pathsIt->second;
						    }
						    entry->Change([&](RegistryBucket* b) {
							    b->markNodeModulesDirty(packageName);
						    });
						    if (!entry->Value()->state.multipleFilesDirty) {
							    cleanNodeModulesBuckets.erase(dirPath);
						    }
					    }
				    } else {
					    // Check if this path (possibly a realpath of a
					    // workspace package) is in any bucket's Paths.
					    for (const auto& bucketDirPath :
					         cleanNodeModulesBuckets) {
						    auto [entry, _] =
						        nodeModules->Get(bucketDirPath);
						    auto pit =
						        entry->Value()->Paths.find(path);
						    if (pit != entry->Value()->Paths.end()) {
							    std::string packageName = pit->second;
							    entry->Change([&](RegistryBucket* b) {
								    b->markNodeModulesDirty(packageName);
							    });
							    if (!entry->Value()
							             ->state.multipleFilesDirty) {
								    cleanNodeModulesBuckets.erase(
								        bucketDirPath);
							    }
						    }
					    }
				    }
			    }

			    // For projects, mark the bucket dirty if the bucket
			    // contains the file directly. Any other significant
			    // change, like a created failed lookup location, is
			    // handled by newProgramStructure.
			    for (const auto& projectDirPath : cleanProjectBuckets) {
				    auto [entry, _] = projects->Get(projectDirPath);
				    if (entry->Value()->Paths.count(path)) {
					    // Project buckets don't use package-based granular
					    // updates
					    entry->Change([&](RegistryBucket* b) {
						    b->markProjectFileDirty(path);
					    });
					    if (!entry->Value()->state.multipleFilesDirty) {
						    cleanProjectBuckets.erase(projectDirPath);
					    }
				    }
			    }
		    }
	    };

	markFilesDirty(change.Created.Keys());
	markFilesDirty(change.Deleted.Keys());
	markFilesDirty(change.Changed.Keys());
}

// nodeModulesBucketTask — registry.go:792.
struct nodeModulesBucketTask {
	std::shared_ptr<dirty::MapEntry<tspath::Path, RegistryBucket*>> entry;
	std::shared_ptr<collections::Set<std::string>> dependencyNames;
	std::string dirName;
	tspath::Path dirPath;

	// For granular updates.
	bool isUpdate = false;
	RegistryBucket* existingBucket = nullptr;
	std::shared_ptr<collections::Set<std::string>> dirtyPackages;

	// Filled by discovery. shared_ptr: Go *Set sharing — packageNames may
	// alias dirtyPackages directly (registry.go:897).
	std::shared_ptr<collections::Set<std::string>> packageNames;
	std::shared_ptr<collections::Set<std::string>> directoryPackageNames;
	std::vector<std::unique_ptr<discoveredPackage>> discovered;
};

// updateIndexes — registry.go:791. The three-phase pipeline: discovery
// (package.json + realpath), extraction (per unique realpath), and bucket
// building (per bucket). Go parallelizes each phase with wg.Go; we run the
// bodies sequentially — the shared extractionCache map is then trivially
// safe.
void registryBuilder::updateIndexes(gostd::Context ctx, RegistryChange& change,
                                    logging::LogTree* logger) {
	auto [projectID, _requestedProgram] =
	    host->GetDefaultProject(change.RequestedFile);
	if (projectID == nullptr) {
		return;
	}

	// Compute resolved package names and project reference output mappings
	// for all projects upfront.
	std::unordered_map<ProjectID*,
	                   std::shared_ptr<collections::Set<std::string>>>
	    allResolvedPackageNames;
	std::unordered_map<tspath::Path, std::string> projectReferenceOutputs;
	// Compute which packages have implicit deep imports (subpath imports in
	// packages without exports). These packages need recursive directory
	// search to discover all auto-importable files, even when the
	// preference is disabled.
	auto allDeepImportPackages =
	    std::make_unique<collections::Set<std::string>>();
	projects->Range([&](dirty::MapEntry<ProjectID*, RegistryBucket*>* entry) {
		compiler::SimpleProgram* program =
		    host->GetProgramForProject(entry->Key());
		if (program != nullptr) {
			allResolvedPackageNames[entry->Key()] =
			    getResolvedPackageNames(ctx, program);
			addProjectReferenceOutputMappings(program,
			                                  projectReferenceOutputs);
			for (const auto& name :
			     program->DeepImportPackageNames()->Keys()) {
				allDeepImportPackages->Add(name);
			}
		}
		return true;
	});

	auto fileExcludePatterns =
	    userPreferences.ParsedAutoImportFileExcludePatterns(
	        host->FS()->UseCaseSensitiveFileNames());

	// Determine which packages need recursive directory search for this
	// build. nullptr means all packages (preference is enabled for all).
	std::unique_ptr<collections::Set<std::string>> targetRecursivePackagesOwned;
	collections::Set<std::string>* targetRecursivePackages = nullptr;
	if (!tristateIsTrue(
	        userPreferences.AutoImportEntrypointDirectorySearch)) {
		targetRecursivePackagesOwned = std::move(allDeepImportPackages);
		targetRecursivePackages = targetRecursivePackagesOwned.get();
	}

	// --- Collect node_modules tasks ---
	std::vector<std::unique_ptr<nodeModulesBucketTask>> nodeModulesTasks;
	tspath::forEachAncestorDirectory<int>(
	    change.RequestedFile,
	    [&](std::string_view dirPathSv) -> std::pair<int, bool> {
		    tspath::Path dirPath{dirPathSv};
		    auto [nodeModulesBucket, ok] = nodeModules->Get(dirPath);
		    if (ok) {
			    std::string dirName =
			        directories->Get(dirPath).first->Value()->name;
			    std::shared_ptr<collections::Set<std::string>> dependencies =
			        computeDependenciesForNodeModulesDirectory(
			            change, allResolvedPackageNames, dirName, dirPath);
			    const BucketState& bucketState =
			        nodeModulesBucket->Value()->state;
			    // !!! Optimization: handle different dependency set via
			    // granular updates
			    bool needsFullRebuild =
			        bucketState.multipleFilesDirty ||
			        !collections::Set<std::string>::EqualsPtr(
			            nodeModulesBucket->Value()->DependencyNames.get(),
			            dependencies.get()) ||
			        !bucketState.buildPreferences.Equal(
			            bucketBuildPreferencesFromUserPreferences(
			                userPreferences)) ||
			        !recursiveSearchSubset(
			            targetRecursivePackages,
			            bucketState.recursiveSearchPackages.get());
			    std::shared_ptr<collections::Set<std::string>>
			        dirtyPackages =
			            bucketState.dirtyPackages;
			    if (bucketState.multipleFilesDirty) {
			        dirtyPackages = nullptr;
			    }
			    bool canDoGranularUpdate =
			        !needsFullRebuild &&
			        (dirtyPackages != nullptr && dirtyPackages->Len() > 0);

			    auto task = std::make_unique<nodeModulesBucketTask>();
			    task->entry = nodeModulesBucket;
			    task->dependencyNames = dependencies;
			    task->dirName = dirName;
			    task->dirPath = dirPath;
			    if (needsFullRebuild) {
				    nodeModulesTasks.push_back(std::move(task));
			    } else if (canDoGranularUpdate) {
				    task->isUpdate = true;
				    task->existingBucket = nodeModulesBucket->Value();
				    task->dirtyPackages = dirtyPackages;
				    nodeModulesTasks.push_back(std::move(task));
			    }
		    }
		    return {0, false};
	    });

	logging::LogTree* nodeModulesLogger = nullptr;
	std::vector<std::unique_ptr<logging::LogTree>> nodeModulesLoggerOwned;
	if (logger != nullptr && !nodeModulesTasks.empty()) {
		nodeModulesLoggerOwned.push_back(
		    std::unique_ptr<logging::LogTree>(logging::fork(
		        logger, "Building node_modules indexes")));
		nodeModulesLogger = nodeModulesLoggerOwned.back().get();
	}

	// --- Phase 1: Discovery (parallel per bucket) ---
	// Resolve package.json and realpath for each package in each bucket.
	auto discoveryStart = std::chrono::steady_clock::now();
	for (auto& task : nodeModulesTasks) {
		// wg.Go — run inline (see file header).
		if (task->isUpdate) {
			task->packageNames = task->dirtyPackages;
		} else {
			task->directoryPackageNames = getPackageNamesInNodeModules(
			    tspath::combinePaths(task->dirName, {"node_modules"}),
			    host->FS().get());
			// core.Coalesce
			task->packageNames = task->dependencyNames != nullptr
			                         ? task->dependencyNames
			                         : task->directoryPackageNames;
		}
		task->discovered = discoverBucketPackages(task->packageNames.get(),
		                                          task->dirName,
		                                          task->dirPath);
	}
	if (nodeModulesLogger != nullptr) {
		logging::logf(nodeModulesLogger, 
		    "Discovered packages: %v",
		    gostd::durationString(std::chrono::steady_clock::now() - discoveryStart));
	}

	// --- Phase 2: Extraction (parallel per unique realpath) ---
	// Extract from main packages first. If a main package has no TypeScript
	// entrypoints, we fall back to extracting from @types in a second pass.
	// Packages with no main package extract directly from @types in the
	// primary pass.
	auto extractionStart = std::chrono::steady_clock::now();
	std::unordered_set<std::string> seen;
	std::unordered_map<std::string,
	                   std::unique_ptr<perPackageExtractionResult>>
	    extractionCache;
	// Collect all packages that have an @types fallback. After the primary
	// pass, we filter to only those whose main extraction failed, then
	// deduplicate by typesRealpath.
	std::vector<discoveredPackage*> typesFallbackCandidates;
	for (auto& task : nodeModulesTasks) {
		for (auto& pkg : task->discovered) {
			if (!pkg->realpath.empty()) {
				if (!seen.count(pkg->realpath)) {
					seen.insert(pkg->realpath);
					bool enableDirSearch =
					    targetRecursivePackages == nullptr ||
					    targetRecursivePackages->Has(pkg->packageName) ||
					    knownRecursiveSearchPackages.Has(
					        pkg->packageName);
					// Record actual directory-searched packages so the
					// stored set reflects reality for rebuild detection
					// and stats.
					if (enableDirSearch &&
					    targetRecursivePackages != nullptr) {
						targetRecursivePackages->Add(pkg->packageName);
					}
					// wg.Go — run inline.
					if (gostd::ctxErr(ctx) == nullptr) {
						auto result = extractPackage(
						    ctx, pkg->packageJson, pkg->packageName,
						    projectReferenceOutputs,
						    fileExcludePatterns.get(), enableDirSearch);
						if (result != nullptr) {
							extractionCache[pkg->realpath] =
							    std::move(result);
						}
					}
				}
				if (!pkg->typesRealpath.empty()) {
					typesFallbackCandidates.push_back(pkg.get());
				}
			} else if (!pkg->typesRealpath.empty()) {
				if (!seen.count(pkg->typesRealpath)) {
					seen.insert(pkg->typesRealpath);
					// @types packages always get directory search
					if (targetRecursivePackages != nullptr) {
						targetRecursivePackages->Add(pkg->packageName);
					}
					// wg.Go — run inline.
					if (gostd::ctxErr(ctx) == nullptr) {
						auto result = extractPackage(
						    ctx, pkg->typesPackageJson,
						    pkg->packageName, projectReferenceOutputs,
						    fileExcludePatterns.get(),
						    /*enableDirectorySearch*/ true);
						if (result != nullptr) {
							extractionCache[pkg->typesRealpath] =
							    std::move(result);
						}
					}
				}
			}
		}
	}

	// For packages whose main extraction yielded nothing, fall back to
	// @types.
	for (discoveredPackage* pkg : typesFallbackCandidates) {
		bool mainExtracted = extractionCache.count(pkg->realpath) &&
		                     extractionCache[pkg->realpath] != nullptr;
		if (mainExtracted || seen.count(pkg->typesRealpath)) {
			continue;
		}
		seen.insert(pkg->typesRealpath);
		// @types fallback packages always get directory search
		if (targetRecursivePackages != nullptr) {
			targetRecursivePackages->Add(pkg->packageName);
		}
		// wg.Go — run inline.
		if (gostd::ctxErr(ctx) == nullptr) {
			auto result = extractPackage(
			    ctx, pkg->typesPackageJson, pkg->packageName,
			    projectReferenceOutputs, fileExcludePatterns.get(),
			    /*enableDirectorySearch*/ true);
			if (result != nullptr) {
				extractionCache[pkg->typesRealpath] = std::move(result);
			}
		}
	}
	if (nodeModulesLogger != nullptr) {
		logging::logf(nodeModulesLogger, "Extracted exports: %v (%d packages)",
		                        gostd::durationString(
		                            std::chrono::steady_clock::now() -
		                            extractionStart),
		                        seen.size());
	}
	uniquePackageCount = static_cast<int>(seen.size());

	// --- Phase 3: Bucket building (parallel per bucket) ---
	// Each bucket installs the shared extraction results and builds its
	// index.
	std::vector<std::unique_ptr<bucketBuildResult>> allResults;

	for (auto& task : nodeModulesTasks) {
		auto br = std::make_unique<bucketBuildResult>();
		auto entry = task->entry;
		br->replaceBucket = [entry](RegistryBucket* b) {
			entry->Replace(b);
		};
		br->resolutionPath = task->entry->Key();
		bucketBuildResult* brp = br.get();
		allResults.push_back(std::move(br));
		// wg.Go — run inline.
		if (task->isUpdate) {
			updateNodeModulesBucket(
			    ctx, brp, task->existingBucket, task->dirtyPackages.get(),
			    task->discovered, extractionCache,
			    targetRecursivePackages,
			    nodeModulesLogger != nullptr
			        ? logging::fork(nodeModulesLogger, task->dirName)
			        : nullptr);
		} else {
			buildNodeModulesBucket(
			    ctx, brp, task->dependencyNames, task->dirPath,
			    task->discovered, task->directoryPackageNames.get(),
			    extractionCache, targetRecursivePackages,
			    nodeModulesLogger != nullptr
			        ? logging::fork(nodeModulesLogger, task->dirName)
			        : nullptr);
		}
	}

	// Project bucket (not part of the three-phase pipeline — no
	// cross-bucket dedup needed).
	if (auto [project, hasProject] = projects->Get(projectID); hasProject) {
		compiler::SimpleProgram* program =
		    host->GetProgramForProject(projectID);
		std::shared_ptr<collections::Set<std::string>> resolvedPackageNames =
		    allResolvedPackageNames.count(projectID)
		        ? allResolvedPackageNames[projectID]
		        : nullptr;
		bool shouldRebuild =
		    project->Value()->state.hasDirtyFileBesides(
		        change.RequestedFile) ||
		    !project->Value()->state.buildPreferences.Equal(
		        bucketBuildPreferencesFromUserPreferences(
		            userPreferences));
		if (!shouldRebuild &&
		    project->Value()->state.newProgramStructure > 0) {
			if (!collections::Set<std::string>::EqualsPtr(
			        project->Value()->ResolvedPackageNames.get(),
			        resolvedPackageNames.get()) ||
			    hasNewNonNodeModulesFiles(program, project->Value())) {
				shouldRebuild = true;
			} else {
				project->Change([](RegistryBucket* b) {
					b->state.newProgramStructure =
					    newProgramStructureFalse;
				});
			}
		}
		if (shouldRebuild) {
			auto br = std::make_unique<bucketBuildResult>();
			auto projectEntry = project;
			br->replaceBucket = [projectEntry](RegistryBucket* b) {
				projectEntry->Replace(b);
			};
			br->resolutionPath =
			    base->toPath(program->GetCurrentDirectory());
			bucketBuildResult* brp = br.get();
			allResults.push_back(std::move(br));
			// wg.Go — run inline.
			buildProjectBucket(
			    ctx, brp, projectID, resolvedPackageNames,
			    logger != nullptr
			        ? logging::fork(logger,
			            "Building project bucket " + projectID->String())
			        : nullptr);
		}
	}

	for (auto& br : allResults) {
		if (br->err != nullptr) {
			continue;
		}
		for (const auto& path : br->removedEntrypointPaths) {
			entrypoints->Delete(path);
		}
		for (auto& [path, entries] : br->entrypoints) {
			entrypoints->Set(path, entries);
		}
		br->replaceBucket(br->bucket);
	}

	// If we failed to resolve any alias exports by ending up at a
	// non-relative module specifier that didn't resolve to another package,
	// it's probably an ambient module declared in another package. We
	// recorded these failures, along with the name of every ambient module
	// declared elsewhere, so we can do a second pass on the failed files,
	// this time including the ambient module declarations that were missing
	// the first time.
	auto secondPassStart = std::chrono::steady_clock::now();
	int secondPassFileCount = 0;
	for (auto& br : allResults) {
		if (br->err != nullptr) {
			continue;
		}
		if (br->possibleFailedAmbientModuleLookupTargets == nullptr) {
			continue;
		}
		std::unordered_map<std::string, SourceFile*> rootFiles;
		std::vector<std::string> failedTargets;
		br->possibleFailedAmbientModuleLookupTargets->Range(
		    [&](const std::string& t) {
			    failedTargets.push_back(t);
			    return true;
		    });
		for (const auto& target : failedTargets) {
			for (const auto& fileName :
			     resolveAmbientModuleName(target, br->resolutionPath)) {
				if (rootFiles.count(fileName)) {
					continue;
				}
				rootFiles[fileName] =
				    host->GetSourceFile(fileName,
				                        base->toPath(fileName));
				secondPassFileCount++;
			}
		}
		if (!rootFiles.empty()) {
			module::ResolverOptions resolverOptions = this->resolverOptions;
			resolverOptions.Host = host;
			resolverOptions.CompilerOptions = emptyCompilerOptions();
			std::unique_ptr<module::DefaultResolver> moduleResolver(
			    module::NewResolver(resolverOptions));
			std::vector<SourceFile*> rootFileVec;
			rootFileVec.reserve(rootFiles.size());
			for (auto& [_, f] : rootFiles) {
				rootFileVec.push_back(f);
			}
			auto aliasResolverPtr = newAliasResolver(
			    rootFileVec, {}, host, moduleResolver.get(),
			    base->toPath,
			    [](SourceFile*, const std::string&) {
				    // no-op
			    });
			auto ch = std::make_unique<checker::Checker>();
			ch->init(aliasResolverPtr.get());
			br->possibleFailedAmbientModuleLookupSources->Range(
			    [&](const tspath::Path& path,
			        const std::shared_ptr<failedAmbientModuleLookupSource>&
			            source) {
				    SourceFile* sourceFile =
				        aliasResolverPtr->GetSourceFile(
				            source->fileName);
				    auto extractor = newExportExtractor(
				        source->packageName, ch.get(),
				        moduleResolver.get(),
				        [fs = host->FS()](const std::string& s) {
					        return fs->Realpath(s);
				        });
				    auto fileExports =
				        extractor->extractFromFile(sourceFile);
				    for (auto& exp : fileExports) {
					    br->bucket->index->insertAsWords(exp);
				    }
				    (void)path;
				    return true;
			    });
		}
	}

	if (nodeModulesLogger != nullptr) {
		if (secondPassFileCount > 0) {
			logging::logf(nodeModulesLogger, 
			    "%d files required second pass, took %v",
			    secondPassFileCount,
			    gostd::durationString(std::chrono::steady_clock::now() - secondPassStart));
		}
		logging::logf(nodeModulesLogger, "Total: %v",
		                        gostd::durationString(
		                            std::chrono::steady_clock::now() -
		                            discoveryStart));
	}
}

// hasNewNonNodeModulesFiles — registry.go:1137.
bool hasNewNonNodeModulesFiles(compiler::SimpleProgram* program,
                               RegistryBucket* bucket) {
	if (bucket->state.newProgramStructure !=
	    newProgramStructureDifferentFileNames) {
		return false;
	}
	for (SourceFile* file : program->GetSourceFiles()) {
		if (file->IsContentMapperSupplemental() ||
		    file->FileName().find("/node_modules/") !=
		        std::string::npos ||
		    isIgnoredFile(program, file)) {
			continue;
		}
		if (!bucket->Paths.count(file->Path())) {
			return true;
		}
	}
	return false;
}

// isIgnoredFile — registry.go:1152.
bool isIgnoredFile(compiler::SimpleProgram* program, SourceFile* file) {
	return program->IsSourceFileDefaultLibrary(file->Path()) ||
	       program->IsGlobalTypingsFile(file->FileName());
}

// hasSymlinkToNodeModules — registry.go:1159.
bool hasSymlinkToNodeModules(const tspath::Path& filePath,
                             const tspath::Path& projectRootPath,
                             symlinks::KnownSymlinks* symlinkCache) {
	if (symlinkCache == nullptr) {
		return false;
	}
	// Keep files inside this project indexed in project buckets even if
	// they are reachable through a node_modules symlink from elsewhere.
	if (pathContainsPath(projectRootPath, filePath)) {
		return false;
	}

	// First check if the file itself has a symlink to node_modules
	if (auto* filesByRealpath = symlinkCache->FilesByRealpath()) {
		if (auto [symlinkPaths, ok] = filesByRealpath->Load(filePath); ok) {
			bool found = false;
			symlinkPaths->Range([&](const std::string& symlinkPath) {
				if (symlinkPath.find("/node_modules/") !=
				    std::string::npos) {
					found = true;
					return false; // stop ranging
				}
				return true;
			});
			if (found) {
				return true;
			}
		}
	}

	// Fall back to checking ancestor directories
	auto* directoriesByRealpath = symlinkCache->DirectoriesByRealpath();
	if (directoriesByRealpath == nullptr) {
		return false;
	}
	bool found = false;
	tspath::forEachAncestorDirectory<int>(
	    filePath,
	    [&](std::string_view dirPathSv) -> std::pair<int, bool> {
		    tspath::Path dirPath{dirPathSv};
		    auto [symlinkPaths, ok] = directoriesByRealpath->Load(
		        tspath::ensureTrailingDirectorySeparator(dirPath));
		    if (!ok) {
			    return {0, false};
		    }
		    // Check if any of the symlinks point to a node_modules
		    // directory
		    symlinkPaths->Range([&](const std::string& symlinkPath) {
			    if (symlinkPath.find("/node_modules/") !=
			        std::string::npos) {
				    found = true;
				    return false; // stop ranging
			    }
			    return true;
		    });
		    return {0, found}; // stop if we found a match
	    });
	return found;
}

// buildProjectBucket — registry.go:1234.
void registryBuilder::buildProjectBucket(
    gostd::Context ctx, bucketBuildResult* result, ProjectID* projectID,
    std::shared_ptr<collections::Set<std::string>> resolvedPackageNames,
    logging::LogTree* logger) {
	if (gostd::ctxErr(ctx) != nullptr) {
		result->err = gostd::ctxErr(ctx);
		return;
	}

	auto start = std::chrono::steady_clock::now();
	auto fileExcludePatterns =
	    userPreferences.ParsedAutoImportFileExcludePatterns(
	        host->FS()->UseCaseSensitiveFileNames());
	result->bucket = new RegistryBucket();
	module::ResolverOptions resolverOptions = this->resolverOptions;
	resolverOptions.Host = host;
	resolverOptions.CompilerOptions = emptyCompilerOptions();
	std::unique_ptr<module::DefaultResolver> moduleResolver(
	    module::NewResolver(resolverOptions));
	compiler::SimpleProgram* program =
	    host->GetProgramForProject(projectID);
	tspath::Path projectRootPath =
	    base->toPath(program->GetCurrentDirectory());
	symlinks::KnownSymlinks* symlinkCache = program->GetSymlinkCache();
	auto pool = std::make_unique<checkerPool>(program);
	struct PoolCloser {
		checkerPool* p;
		~PoolCloser() { p->closePool(); }
	} _poolCloser{pool.get()};
	std::unordered_map<tspath::Path, std::vector<std::shared_ptr<Export>>>
	    exports;
	int skippedFileCount = 0;
	extractorStats combinedStats;

	for (SourceFile* file : program->GetSourceFiles()) {
		if (file->IsContentMapperSupplemental() ||
		    isIgnoredFile(program, file)) {
			continue;
		}
		if (fileExcludePatterns != nullptr &&
		    fileExcludePatterns->MatchString(file->FileName())) {
			skippedFileCount++;
			continue;
		}
		// Ordinary node_modules files are owned by node_modules buckets.
		// Content-mapped files are not discovered by those buckets, but
		// files already transformed in the Program can be indexed here.
		if (file->ContentMapper().empty() &&
		    (file->FileName().find("/node_modules/") !=
		         std::string::npos ||
		     hasSymlinkToNodeModules(file->Path(), projectRootPath,
		                             symlinkCache))) {
			continue;
		}
		// wg.Go — run inline.
		if (gostd::ctxErr(ctx) == nullptr) {
			auto [ch, done] = pool->getChecker();
			struct DoneGuard {
				std::function<void()> f;
				~DoneGuard() {
					if (f) f();
				}
			} _done{done};
			auto extractor =
			    newExportExtractor("", ch, moduleResolver.get(), nullptr);
			auto fileExports = extractor->extractFromFile(file);
			exports[file->Path()] = std::move(fileExports);
			auto stats = extractor->Stats();
			combinedStats.exports += stats->exports.load();
			combinedStats.usedChecker += stats->usedChecker.load();
		}
	}

	auto indexStart = std::chrono::steady_clock::now();
	auto idx = std::make_unique<Index>();
	std::unordered_map<tspath::Path, std::string> paths;
	paths.reserve(exports.size());
	for (auto& [path, fileExports] : exports) {
		paths[path] = ""; // Empty string for project buckets
		for (auto& exp : fileExports) {
			idx->insertAsWords(exp);
		}
	}

	result->bucket->Paths = std::move(paths);
	result->bucket->index = std::move(idx);
	result->bucket->ResolvedPackageNames = resolvedPackageNames;
	result->bucket->state.buildPreferences =
	    bucketBuildPreferencesFromUserPreferences(userPreferences);

	if (logger != nullptr) {
		logging::logf(logger, 
		    "Extracted exports: %v (%d exports, %d used checker, %d "
		    "created checkers)",
		    gostd::durationString(indexStart - start), combinedStats.exports.load(),
		    combinedStats.usedChecker.load(), pool->getCreatedCount());
		if (skippedFileCount > 0) {
			logging::logf(logger, "Skipped %d files due to exclude patterns",
			             skippedFileCount);
		}
		logging::logf(logger, "Built index: %v",
		             gostd::durationString(std::chrono::steady_clock::now() - indexStart));
		logging::logf(logger, "Bucket total: %v",
		             gostd::durationString(std::chrono::steady_clock::now() - start));
	}
}

// computeDependenciesForNodeModulesDirectory — registry.go:1321. Returns a
// shared_ptr so a task and the built bucket can share it like Go's *Set.
std::shared_ptr<collections::Set<std::string>>
registryBuilder::computeDependenciesForNodeModulesDirectory(
    RegistryChange& change,
    std::unordered_map<ProjectID*,
                       std::shared_ptr<collections::Set<std::string>>>&
        allResolvedPackageNames,
    const std::string& dirName, const tspath::Path& dirPath) {
	(void)dirName;
	// If any open files are in scope of this directory but not in scope of
	// any package.json, we need to add all packages in this node_modules
	// directory.
	for (auto& [path, _fn] : change.OpenFiles) {
		if (pathContainsPath(dirPath, path) &&
		    getNearestAncestorDirectoryWithPackageJson(path) == nullptr) {
			return nullptr;
		}
	}

	// Get all package.jsons that have this node_modules directory in their
	// spine
	auto dependencies = std::make_shared<collections::Set<std::string>>();
	directories->Range(
	    [&](dirty::MapEntry<tspath::Path, directory*>* entry) {
		    if (entry->Value()->packageJson != nullptr &&
		        entry->Value()->packageJson->Exists() &&
		        pathContainsPath(dirPath, entry->Key())) {
			    addPackageJsonDependencies(
			        *entry->Value()->packageJson->Contents,
			        dependencies.get());
		    }
		    return true;
	    });

	// Add packages that are directly imported by programs but not listed in
	// package.json. This ensures node_modules files are always in
	// node_modules buckets. Include packages from all projects that have
	// this node_modules directory in their spine.
	for (auto& [_id, resolvedPackageNames] : allResolvedPackageNames) {
		for (const auto& name : resolvedPackageNames->Keys()) {
			dependencies->Add(name);
		}
	}

	return dependencies;
}

// discoverBucketPackages — registry.go:1397. Resolves the package.json and
// realpath for each package name in a node_modules directory.
std::vector<std::unique_ptr<discoveredPackage>>
registryBuilder::discoverBucketPackages(
    collections::Set<std::string>* packageNames, const std::string& dirName,
    const tspath::Path& dirPath) {
	std::vector<std::unique_ptr<discoveredPackage>> result;
	result.reserve(packageNames != nullptr ? packageNames->Len() : 0);
	if (packageNames == nullptr) {
		return result;
	}
	for (const auto& packageName : packageNames->Keys()) {
		std::string typesPackageName =
		    module::GetTypesPackageName(packageName);
		auto packageJson = host->GetPackageJson(tspath::combinePaths(
		    dirName, {"node_modules", packageName, "package.json"}));
		std::shared_ptr<packagejson::InfoCacheEntry> typesPackageJson;
		if (packageName != typesPackageName) {
			auto typesJson = host->GetPackageJson(tspath::combinePaths(
			    dirName, {"node_modules", typesPackageName, "package.json"}));
			if (typesJson != nullptr && typesJson->DirectoryExists) {
				typesPackageJson = typesJson;
			}
		}
		std::string realpath;
		if (packageJson != nullptr && packageJson->DirectoryExists) {
			realpath =
			    host->FS()->Realpath(packageJson->PackageDirectory);
		}
		std::string typesRealpath;
		if (typesPackageJson != nullptr) {
			typesRealpath =
			    host->FS()->Realpath(typesPackageJson->PackageDirectory);
		}
		bool isLocal =
		    !realpath.empty() &&
		    realpath.find("/node_modules/") == std::string::npos &&
		    tspath::containsPath(host->GetCurrentDirectory(), realpath,
		                         tspath::ComparePathsOptions{
		                             host->FS()
		                                 ->UseCaseSensitiveFileNames()});
		auto pkg = std::make_unique<discoveredPackage>();
		pkg->packageName = packageName;
		pkg->packageJson = packageJson;
		pkg->realpath = realpath;
		pkg->typesPackageJson = typesPackageJson;
		pkg->typesRealpath = typesRealpath;
		pkg->dirPath = dirPath;
		pkg->isLocal = isLocal;
		result.push_back(std::move(pkg));
	}
	return result;
}

// extractPackage — registry.go:1444. Extracts exports from a single
// package.json. Runs once per unique realpath during the extraction phase.
// Returns nullptr if the package has no extractable entrypoints.
std::unique_ptr<perPackageExtractionResult> registryBuilder::extractPackage(
    gostd::Context ctx,
    const std::shared_ptr<packagejson::InfoCacheEntry>& packageJson,
    const std::string& packageName,
    const std::unordered_map<tspath::Path, std::string>&
        projectReferenceOutputs,
    vfs::vfsmatch::SpecMatcher* fileExcludePatterns,
    bool enableDirectorySearch) {
	if (packageJson == nullptr || !packageJson->DirectoryExists) {
		return nullptr;
	}
	auto [toRealpath, toSymlink] =
	    getPackageRealpathFuncs(host->FS().get(),
	                            packageJson->PackageDirectory);
	std::unique_ptr<module::DefaultResolver> resolver(
	    getModuleResolver(host, toRealpath, resolverOptions));
	auto packageEntrypoints =
	    resolver->GetEntrypointsFromPackageJsonInfo(packageJson, packageName,
	                                              enableDirectorySearch);
	if (packageEntrypoints.empty()) {
		return nullptr;
	}

	int skippedEntrypoints = 0;
	if (fileExcludePatterns != nullptr) {
		size_t count = packageEntrypoints.size();
		std::erase_if(packageEntrypoints,
		              [&](const std::unique_ptr<module::ResolvedEntrypoint>&
		                      entrypoint) {
			              return fileExcludePatterns->MatchString(
			                  entrypoint->ResolvedFileName);
		              });
		skippedEntrypoints =
		    static_cast<int>(count - packageEntrypoints.size());
	}
	if (packageEntrypoints.empty()) {
		return nullptr;
	}

	auto result = std::make_unique<perPackageExtractionResult>();
	// vector<unique_ptr> -> vector<shared_ptr>: the Go slice shares the
	// *ResolvedEntrypoint pointers; shared_ptr preserves that sharing.
	result->entrypoints.reserve(packageEntrypoints.size());
	for (auto& ep : packageEntrypoints) {
		result->entrypoints.emplace_back(std::move(ep));
	}
	result->skippedEntrypoints = skippedEntrypoints;
	result->failedAmbientModuleLookupTargets =
	    std::make_unique<collections::Set<std::string>>();

	// Resolve entrypoint source files and build the alias resolver.
	collections::Set<tspath::Path> seenFiles(
	    result->entrypoints.size()); // SetWithSizeHint
	std::vector<SourceFile*> rootFiles(result->entrypoints.size());
	std::unordered_map<tspath::Path, pathAndFileName> symlinks;
	for (size_t i = 0; i < result->entrypoints.size(); i++) {
		auto& entrypoint = result->entrypoints[i];
		std::string fileName{entrypoint->SymlinkOrRealpath()};
		std::string realpathFileName = entrypoint->ResolvedFileName;
		tspath::Path realpathPath = base->toPath(realpathFileName);

		if (auto it = projectReferenceOutputs.find(realpathPath);
		    it != projectReferenceOutputs.end()) {
			const std::string& inputFileName = it->second;
			fileName = toSymlink(inputFileName);
			realpathFileName = inputFileName;
			realpathPath = base->toPath(realpathFileName);
		}

		if (!seenFiles.AddIfAbsent(realpathPath)) {
			continue;
		}
		if (fileName != realpathFileName) {
			tspath::Path symlinkPath = base->toPath(fileName);
			symlinks[realpathPath] =
			    pathAndFileName{symlinkPath, fileName};
			result->isSymlinked = true;
		}
		// wg.Go — run inline.
		SourceFile* file =
		    host->GetSourceFile(realpathFileName, realpathPath);
		if (file != nullptr) {
			tsc::bindSourceFile(file);
		}
		rootFiles[i] = file;
	}
	std::erase(rootFiles, nullptr);

	auto aliasResolverPtr = newAliasResolver(
	    rootFiles, std::move(symlinks), host, resolver.get(), base->toPath,
	    [&](SourceFile* source, const std::string& moduleName) {
		    result->failedAmbientModuleLookupTargets->Add(moduleName);
		    if (!result->failedAmbientModuleLookupSources.count(
		            source->Path())) {
			    auto src =
			        std::make_shared<failedAmbientModuleLookupSource>();
			    src->fileName = source->FileName();
			    result->failedAmbientModuleLookupSources[source->Path()] =
			        src;
		    }
	    });

	auto ch = std::make_unique<checker::Checker>();
	ch->init(aliasResolverPtr.get());
	auto extractor =
	    newExportExtractor(packageName, ch.get(), resolver.get(),
	                       toRealpath);

	collections::Set<tspath::Path> nonModuleFiles;
	for (SourceFile* entrypoint : aliasResolverPtr->rootFiles) {
		if (gostd::ctxErr(ctx) != nullptr) {
			return nullptr;
		}
		auto fileExports = extractor->extractFromFile(entrypoint);
		for (const auto& name : entrypoint->AmbientModuleNames) {
			result->ambientModules[name].push_back(
			    entrypoint->FileName());
		}
		result->packageFiles[entrypoint->Path()] =
		    entrypoint->FileName();
		auto symlinkIt = aliasResolverPtr->symlinks.find(
		    entrypoint->Path());
		bool hasSymlink = symlinkIt != aliasResolverPtr->symlinks.end();
		if (hasSymlink) {
			result->packageFiles[symlinkIt->second.path] =
			    symlinkIt->second.fileName;
		}

		bool hasExports = !fileExports.empty() &&
		                  entrypoint->ExternalModuleIndicator != nullptr;
		auto sourceIt = result->failedAmbientModuleLookupSources.find(
		    entrypoint->Path());
		if (sourceIt ==
		    result->failedAmbientModuleLookupSources.end()) {
			result->exports[entrypoint->Path()] = std::move(fileExports);
		} else {
			sourceIt->second->packageName = packageName;
			hasExports = entrypoint->ExternalModuleIndicator != nullptr;
		}

		if (!hasExports) {
			nonModuleFiles.Add(entrypoint->Path());
			if (hasSymlink) {
				nonModuleFiles.Add(symlinkIt->second.path);
			}
		}
	}

	// Discard entrypoints for non-module files and empty modules.
	std::erase_if(
	    result->entrypoints,
	    [&](const std::shared_ptr<module::ResolvedEntrypoint>& ep) {
		    return nonModuleFiles.Has(base->toPath(ep->ResolvedFileName));
	    });

	auto stats = extractor->Stats();
	result->statsExports = stats->exports.load();
	result->statsUsedChecker = stats->usedChecker.load();
	return result;
}

// installExtractions — registry.go:1575. Aggregates pre-extracted
// per-package results into a single packageExtractionResult for one bucket.
std::unique_ptr<packageExtractionResult> installExtractions(
    const std::vector<std::unique_ptr<discoveredPackage>>& discovered,
    std::unordered_map<std::string, std::unique_ptr<perPackageExtractionResult>>&
        extractionCache) {
	auto result = std::make_unique<packageExtractionResult>();
	result->workspacePackages =
	    std::make_unique<collections::Set<std::string>>();
	// possibleFailedAmbientModuleLookupSources/Targets are value members
	// (Go allocates them unconditionally).

	for (auto& pkg : discovered) {
		perPackageExtractionResult* extraction = nullptr;
		auto it = extractionCache.find(pkg->realpath);
		if (it != extractionCache.end()) {
			extraction = it->second.get();
		}
		if (extraction == nullptr) {
			auto tit = extractionCache.find(pkg->typesRealpath);
			if (tit != extractionCache.end()) {
				extraction = tit->second.get();
			}
		}
		if (extraction == nullptr) {
			continue;
		}
		// maps.Copy(result.exports, extraction.exports)
		for (auto& [path, exps] : extraction->exports) {
			result->exports[path] = exps;
		}
		if (result->packageFiles[pkg->packageName].empty()) {
			result->packageFiles[pkg->packageName].reserve(
			    extraction->packageFiles.size());
		}
		for (auto& [path, fileName] : extraction->packageFiles) {
			result->packageFiles[pkg->packageName][path] = fileName;
		}
		for (auto& [name, fileNames] : extraction->ambientModules) {
			auto& dest = result->ambientModuleNames[name];
			dest.insert(dest.end(), fileNames.begin(), fileNames.end());
		}
		if (!extraction->entrypoints.empty()) {
			result->entrypoints.push_back(extraction->entrypoints);
		}
		for (auto& [path, source] :
		     extraction->failedAmbientModuleLookupSources) {
			result->possibleFailedAmbientModuleLookupSources.LoadOrStore(
			    path, source);
		}
		for (const auto& target :
		     extraction->failedAmbientModuleLookupTargets->Keys()) {
			result->possibleFailedAmbientModuleLookupTargets.Add(target);
		}
		if (extraction->isSymlinked && pkg->isLocal) {
			result->workspacePackages->Add(pkg->packageName);
		}
		result->stats.exports += extraction->statsExports;
		result->stats.usedChecker += extraction->statsUsedChecker;
		result->skippedEntrypointsCount += extraction->skippedEntrypoints;
	}

	return result;
}

// buildNodeModulesBucket — registry.go:1624.
void registryBuilder::buildNodeModulesBucket(
    gostd::Context ctx, bucketBuildResult* result,
    std::shared_ptr<collections::Set<std::string>> dependencies,
    const tspath::Path& dirPath,
    std::vector<std::unique_ptr<discoveredPackage>>& discovered,
    collections::Set<std::string>* directoryPackageNames,
    std::unordered_map<std::string, std::unique_ptr<perPackageExtractionResult>>&
        extractionCache,
    collections::Set<std::string>* recursiveSearchPackages,
    logging::LogTree* logger) {
	if (gostd::ctxErr(ctx) != nullptr) {
		result->err = gostd::ctxErr(ctx);
		return;
	}

	auto extraction = installExtractions(discovered, extractionCache);

	auto indexStart = std::chrono::steady_clock::now();
	// Build PackageFiles with all directory package names; indexed packages
	// have non-empty maps, unindexed packages have empty maps.
	std::unordered_map<std::string,
	                   std::unordered_map<tspath::Path, std::string>>
	    allPackageFiles;
	allPackageFiles.reserve(
	    directoryPackageNames != nullptr ? directoryPackageNames->Len() : 0);
	if (directoryPackageNames != nullptr) {
		for (const auto& pkgName : directoryPackageNames->Keys()) {
			auto it = extraction->packageFiles.find(pkgName);
			allPackageFiles[pkgName] =
			    it != extraction->packageFiles.end()
			        ? it->second
			        : std::unordered_map<tspath::Path, std::string>{};
		}
	}

	// Build Paths as reverse mapping from path to package name.
	// Only include paths for local workspace packages (eligible for
	// granular updates).
	std::unordered_map<tspath::Path, std::string> paths;
	for (const auto& pkgName : extraction->workspacePackages->Keys()) {
		auto it = extraction->packageFiles.find(pkgName);
		if (it != extraction->packageFiles.end()) {
			for (auto& [path, _name] : it->second) {
				paths[path] = pkgName;
			}
		}
	}

	result->bucket = new RegistryBucket();
	result->bucket->index = std::make_unique<Index>();
	result->bucket->DependencyNames = dependencies;
	result->bucket->PackageFiles = std::move(allPackageFiles);
	result->bucket->AmbientModuleNames = extraction->ambientModuleNames;
	result->bucket->Paths = std::move(paths);
	result->bucket->state.buildPreferences =
	    bucketBuildPreferencesFromUserPreferences(userPreferences);
	result->bucket->state.recursiveSearchPackages =
	    recursiveSearchPackages != nullptr
	        ? std::make_shared<collections::Set<std::string>>(
	              recursiveSearchPackages->Clone())
	        : nullptr;
	result->entrypoints.reserve(extraction->exports.size());
	result->possibleFailedAmbientModuleLookupSources = std::make_unique<
	    collections::SyncMap<tspath::Path,
	                         std::shared_ptr<failedAmbientModuleLookupSource>>>(
	    std::move(extraction->possibleFailedAmbientModuleLookupSources));
	result->possibleFailedAmbientModuleLookupTargets =
	    std::make_unique<collections::SyncSet<std::string>>(
	        std::move(extraction->possibleFailedAmbientModuleLookupTargets));
	for (auto& [_path, fileExports] : extraction->exports) {
		for (auto& exp : fileExports) {
			result->bucket->index->insertAsWords(exp);
		}
	}
	for (auto& entrypointSet : extraction->entrypoints) {
		for (auto& entrypoint : entrypointSet) {
			tspath::Path path =
			    base->toPath(entrypoint->ResolvedFileName);
			result->entrypoints[path].push_back(entrypoint);
		}
	}

	// Compute old entrypoint paths to remove from the registry-level map.
	// For a full rebuild, all entrypoints belonging to the old bucket's
	// packages must be removed.
	if (auto [oldEntry, ok] = nodeModules->Get(dirPath); ok) {
		RegistryBucket* oldBucket = oldEntry->Value();
		for (auto& [_pkg, files] : oldBucket->PackageFiles) {
			for (auto& [path, _name] : files) {
				if (base->entrypoints.count(path)) {
					result->removedEntrypointPaths.push_back(path);
				}
			}
		}
	}

	if (logger != nullptr) {
		logging::logf(logger, "Installed %d exports (%d used checker)",
		             extraction->stats.exports.load(),
		             extraction->stats.usedChecker.load());
		if (extraction->skippedEntrypointsCount > 0) {
			logging::logf(logger, "Skipped %d entrypoints due to exclude patterns",
			             extraction->skippedEntrypointsCount);
		}
		logging::logf(logger, "Built index: %v",
		             gostd::durationString(std::chrono::steady_clock::now() - indexStart));
	}

	result->err = gostd::ctxErr(ctx);
}

// updateNodeModulesBucket — registry.go:1713. Granular update: re-extracts
// only the dirty packages and merges with the existing bucket.
void registryBuilder::updateNodeModulesBucket(
    gostd::Context ctx, bucketBuildResult* result,
    RegistryBucket* existingBucket,
    collections::Set<std::string>* dirtyPackages,
    std::vector<std::unique_ptr<discoveredPackage>>& discovered,
    std::unordered_map<std::string, std::unique_ptr<perPackageExtractionResult>>&
        extractionCache,
    collections::Set<std::string>* recursiveSearchPackages,
    logging::LogTree* logger) {
	if (gostd::ctxErr(ctx) != nullptr) {
		result->err = gostd::ctxErr(ctx);
		return;
	}

	auto start = std::chrono::steady_clock::now();
	auto extraction = installExtractions(discovered, extractionCache);

	auto indexStart = std::chrono::steady_clock::now();

	// Clone the existing index, excluding exports from dirty packages
	auto newIndex = existingBucket->index->Clone(
	    [&](const std::shared_ptr<Export>& exp) {
		    return dirtyPackages == nullptr ||
		           !dirtyPackages->Has(exp->PackageName);
	    });

	// Clone PackageFiles, removing dirty packages
	auto newPackageFiles = existingBucket->PackageFiles;
	if (dirtyPackages != nullptr) {
		for (const auto& pkgName : dirtyPackages->Keys()) {
			newPackageFiles.erase(pkgName);
		}
	}
	// Add newly extracted package files
	for (auto& [pkgName, files] : extraction->packageFiles) {
		newPackageFiles[pkgName] = files;
	}

	// Clone Paths, removing dirty package paths
	std::unordered_map<tspath::Path, std::string> newPaths;
	newPaths.reserve(existingBucket->Paths.size());
	for (auto& [path, pkgName] : existingBucket->Paths) {
		if (dirtyPackages != nullptr && dirtyPackages->Has(pkgName)) {
			continue;
		}
		newPaths[path] = pkgName;
	}
	// Add paths for newly extracted workspace packages
	for (const auto& pkgName : extraction->workspacePackages->Keys()) {
		auto it = extraction->packageFiles.find(pkgName);
		if (it != extraction->packageFiles.end()) {
			for (auto& [path, _name] : it->second) {
				newPaths[path] = pkgName;
			}
		}
	}

	// Clone AmbientModuleNames, removing dirty package entries
	std::unordered_map<std::string, std::vector<std::string>>
	    newAmbientModuleNames;
	newAmbientModuleNames.reserve(existingBucket->AmbientModuleNames.size());
	for (auto& [moduleName, fileNames] :
	     existingBucket->AmbientModuleNames) {
		// Filter out files from dirty packages
		std::vector<std::string> filtered;
		for (const auto& fileName : fileNames) {
			tspath::Path path = base->toPath(fileName);
			auto it = existingBucket->Paths.find(path);
			if (it != existingBucket->Paths.end() &&
			    dirtyPackages != nullptr &&
			    dirtyPackages->Has(it->second)) {
				continue;
			}
			filtered.push_back(fileName);
		}
		if (!filtered.empty()) {
			newAmbientModuleNames[moduleName] = std::move(filtered);
		}
	}
	// Add newly extracted ambient module names
	for (auto& [moduleName, fileNames] : extraction->ambientModuleNames) {
		auto& dest = newAmbientModuleNames[moduleName];
		dest.insert(dest.end(), fileNames.begin(), fileNames.end());
	}

	// Collect entrypoint paths that need to be removed from the
	// registry-level map (paths belonging to dirty packages)
	std::vector<tspath::Path> removedEntrypointPaths;
	for (auto& [path, _entries] : base->entrypoints) {
		auto it = existingBucket->Paths.find(path);
		if (it != existingBucket->Paths.end() && dirtyPackages != nullptr &&
		    dirtyPackages->Has(it->second)) {
			removedEntrypointPaths.push_back(path);
		}
	}
	// Build new entrypoints from extraction
	std::unordered_map<tspath::Path,
	                   std::vector<std::shared_ptr<module::ResolvedEntrypoint>>>
	    newEntrypoints;
	for (auto& entrypointSet : extraction->entrypoints) {
		for (auto& entrypoint : entrypointSet) {
			tspath::Path path =
			    base->toPath(entrypoint->ResolvedFileName);
			newEntrypoints[path].push_back(entrypoint);
		}
	}

	// Insert newly extracted exports into the index
	for (auto& [_path, fileExports] : extraction->exports) {
		for (auto& exp : fileExports) {
			newIndex->insertAsWords(exp);
		}
	}

	result->bucket = new RegistryBucket();
	result->bucket->index = std::move(newIndex);
	result->bucket->DependencyNames = existingBucket->DependencyNames;
	result->bucket->PackageFiles = std::move(newPackageFiles);
	result->bucket->AmbientModuleNames = std::move(newAmbientModuleNames);
	result->bucket->Paths = std::move(newPaths);
	result->bucket->state.buildPreferences =
	    bucketBuildPreferencesFromUserPreferences(userPreferences);
	result->bucket->state.recursiveSearchPackages =
	    recursiveSearchPackages != nullptr
	        ? std::make_shared<collections::Set<std::string>>(
	              recursiveSearchPackages->Clone())
	        : nullptr;
	result->entrypoints = std::move(newEntrypoints);
	result->removedEntrypointPaths = std::move(removedEntrypointPaths);
	result->possibleFailedAmbientModuleLookupSources = std::make_unique<
	    collections::SyncMap<tspath::Path,
	                         std::shared_ptr<failedAmbientModuleLookupSource>>>(
	    std::move(extraction->possibleFailedAmbientModuleLookupSources));
	result->possibleFailedAmbientModuleLookupTargets =
	    std::make_unique<collections::SyncSet<std::string>>(
	        std::move(extraction->possibleFailedAmbientModuleLookupTargets));

	if (logger != nullptr) {
		logging::logf(logger, "Granular update of %d packages: %v (%d exports)",
		             dirtyPackages != nullptr ? dirtyPackages->Len() : 0,
		             gostd::durationString(indexStart - start),
		             extraction->stats.exports.load());
		logging::logf(logger, "Built index: %v",
		             gostd::durationString(std::chrono::steady_clock::now() - indexStart));
	}

	result->err = gostd::ctxErr(ctx);
}

// getNearestAncestorDirectoryWithPackageJson — registry.go:1832.
directory* registryBuilder::getNearestAncestorDirectoryWithPackageJson(
    const tspath::Path& filePath) {
	return tspath::forEachAncestorDirectory<directory*>(
	           tspath::getDirectoryPath(filePath),
	           [&](std::string_view dirPathSv)
	               -> std::pair<directory*, bool> {
		           tspath::Path dirPath{dirPathSv};
		           auto [dirEntry, ok] = directories->Get(dirPath);
		           if (ok && dirEntry->Value()->packageJson != nullptr &&
		               dirEntry->Value()->packageJson->Exists()) {
			           return std::pair{dirEntry->Value(), true};
		           }
		           return std::pair{static_cast<directory*>(nullptr), false};
	           })
	    .first;
}

// resolveAmbientModuleName — registry.go:1841.
std::vector<std::string> registryBuilder::resolveAmbientModuleName(
    const std::string& moduleName, const tspath::Path& fromPath) {
	return tspath::forEachAncestorDirectory<std::vector<std::string>>(
	           fromPath,
	           [&](std::string_view dirPathSv)
	               -> std::pair<std::vector<std::string>, bool> {
		           tspath::Path dirPath{dirPathSv};
		           auto [bucket, ok] = nodeModules->Get(dirPath);
		           if (ok) {
			           auto it = bucket->Value()->AmbientModuleNames.find(
			               moduleName);
			           if (it != bucket->Value()->AmbientModuleNames.end()) {
				           return std::pair{it->second, true};
			           }
		           }
		           return std::pair{std::vector<std::string>{}, false};
	           })
	    .first;
}

}  // namespace tsc::ls::autoimport
