// === slice: project ===
// watch.go — glob-mapping helpers: createResolutionLookupGlobMapper,
// getTypingsLocationsGlobs, getPathComponentsForWatching,
// perceivedOsRootLengthForWatching.

#include <cstring>

#include "internal/project/watch.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::project {

// createResolutionLookupGlobMapper — watch.go:298.
//
// Complex resolution-lookup glob computation. Given a set of resolution
// lookup paths, produces glob patterns that capture the locations where
// file changes could invalidate resolutions.
std::function<PatternsAndIgnored(collections::SyncMap<tspath::Path, std::string>*)>
createResolutionLookupGlobMapper(
	const std::string& workspaceDirectory, const std::string& libDirectory,
	const std::string& currentDirectory, bool useCaseSensitiveFileNames) {
	auto workspaceDirectoryPath = tspath::toPath(
	    workspaceDirectory, currentDirectory, useCaseSensitiveFileNames);
	auto currentDirectoryPath = tspath::toPath(
	    currentDirectory, currentDirectory, useCaseSensitiveFileNames);
	auto libDirectoryPath = tspath::toPath(
	    libDirectory, currentDirectory, useCaseSensitiveFileNames);

	return [workspaceDirectoryPath, currentDirectoryPath, libDirectoryPath,
	        workspaceDirectory, currentDirectory, libDirectory,
	        useCaseSensitiveFileNames](
	           collections::SyncMap<tspath::Path, std::string>* data)
	           -> PatternsAndIgnored {
		std::unordered_set<std::string> ignored;
		collections::Set<tspath::Path> seenDirs;
		bool includeWorkspace = false;
		bool includeRoot = false;
		bool includeLib = false;
		std::unordered_map<tspath::Path, std::string>
		    nodeModulesDirectories;
		std::unordered_map<tspath::Path, std::string>
		    externalDirectories;

		if (data != nullptr) {
			data->Range([&](const tspath::Path& path,
		                       const std::string& fileName) -> bool {
				if (tspath::isDynamicFileName(path)) {
					return true;
				}
				// Assuming all of the input paths are file paths, we can
				// avoid duplicate work by only taking one file per dir,
				// since their outputs will always be the same.
				if (!seenDirs.AddIfAbsent(
				        tspath::getDirectoryPath(path))) {
					return true;
				}

				if (tspath::pathContainsPath(workspaceDirectoryPath,
				                             path)) {
					includeWorkspace = true;
				} else if (tspath::pathContainsPath(
				               currentDirectoryPath, path)) {
					includeRoot = true;
				} else if (tspath::pathContainsPath(libDirectoryPath,
				                                    path)) {
					includeLib = true;
				} else {
					auto canonicalComponents =
					    tspath::resolvePathComponents(path, "");
					auto fileNameComponents =
					    tspath::resolvePathComponents(fileName, "");
					bool isNodeModules = false;
					if (canonicalComponents.size() ==
					    fileNameComponents.size()) {
						for (size_t i = 0;
						     i < canonicalComponents.size(); i++) {
							if (canonicalComponents[i] ==
							    "node_modules") {
								auto nodeModulesDirectory =
								    tspath::getPathFromPathComponents(
								        {fileNameComponents.begin(),
								         fileNameComponents.begin() +
								             i + 1});
								nodeModulesDirectories[tspath::toPath(
								    nodeModulesDirectory,
								    currentDirectory,
								    useCaseSensitiveFileNames)] =
								    nodeModulesDirectory;
								isNodeModules = true;
								break;
							}
						}
					}
					if (isNodeModules) {
						return true;
					}
					externalDirectories[tspath::getDirectoryPath(
					    path)] = tspath::getDirectoryPath(fileName);
				}
				return true;
			});
		}

		std::vector<std::string> globs;
		if (includeWorkspace) {
			globs.push_back(
			    getRecursiveGlobPattern(workspaceDirectory));
		}
		if (includeRoot) {
			globs.push_back(getRecursiveGlobPattern(currentDirectory));
		}
		if (includeLib) {
			globs.push_back(getRecursiveGlobPattern(libDirectory));
		}
		if (!nodeModulesDirectories.empty()) {
			std::vector<std::string> nodeModulesGlobs;
			nodeModulesGlobs.reserve(nodeModulesDirectories.size());
			for (const auto& [_, dir] : nodeModulesDirectories) {
				nodeModulesGlobs.push_back(
				    getRecursiveGlobPattern(dir));
			}
			std::sort(nodeModulesGlobs.begin(),
			          nodeModulesGlobs.end());
			globs.insert(globs.end(), nodeModulesGlobs.begin(),
			             nodeModulesGlobs.end());
		}
		std::vector<std::string> outsideDirs;
		if (!externalDirectories.empty()) {
			std::vector<std::string> externalDirStrings;
			externalDirStrings.reserve(externalDirectories.size());
			for (const auto& [_, dir] : externalDirectories) {
				externalDirStrings.push_back(dir);
			}
			auto [externalDirectoryParents, ignoredExternalDirs] =
			    tspath::getCommonParents(
			        externalDirStrings, minWatchLocationDepth,
			        getPathComponentsForWatching,
			        tspath::ComparePathsOptions{
			            useCaseSensitiveFileNames, {}});
			std::sort(externalDirectoryParents.begin(),
			          externalDirectoryParents.end());
			ignored = std::move(ignoredExternalDirs);
			outsideDirs = std::move(externalDirectoryParents);
		}

		return PatternsAndIgnored{outsideDirs, globs, ignored};
	};
}

// getTypingsLocationsGlobs — watch.go:380.
PatternsAndIgnored getTypingsLocationsGlobs(
	const std::vector<std::string>& typingsFiles,
	const std::string& typingsLocation, const std::string& workspaceDirectory,
	const std::string& currentDirectory, bool useCaseSensitiveFileNames) {
	bool includeTypingsLocation = false;
	bool includeWorkspace = false;
	std::unordered_map<tspath::Path, std::string> externalDirectories;
	std::unordered_map<tspath::Path, std::string> globs;
	tspath::ComparePathsOptions comparePathsOptions{
	    useCaseSensitiveFileNames, currentDirectory};
	for (const auto& file : typingsFiles) {
		if (tspath::containsPath(typingsLocation, file,
		                         comparePathsOptions)) {
			includeTypingsLocation = true;
		} else if (!tspath::containsPath(workspaceDirectory, file,
		                                 comparePathsOptions)) {
			auto directory =
			    std::string(tspath::getDirectoryPath(file));
			externalDirectories[tspath::toPath(
			    directory, currentDirectory,
			    useCaseSensitiveFileNames)] = directory;
		} else {
			includeWorkspace = true;
		}
	}
	std::vector<std::string> externalDirList;
	externalDirList.reserve(externalDirectories.size());
	for (const auto& kv : externalDirectories) {
		externalDirList.push_back(kv.second);
	}
	auto [externalDirectoryParents, ignored] = tspath::getCommonParents(
	    externalDirList, minWatchLocationDepth,
	    getPathComponentsForWatching, comparePathsOptions);
	std::sort(externalDirectoryParents.begin(),
	          externalDirectoryParents.end());
	if (includeWorkspace) {
		globs[tspath::toPath(workspaceDirectory, currentDirectory,
		                     useCaseSensitiveFileNames)] =
		    getRecursiveGlobPattern(workspaceDirectory);
	}
	if (includeTypingsLocation) {
		globs[tspath::toPath(typingsLocation, currentDirectory,
		                     useCaseSensitiveFileNames)] =
		    getRecursiveGlobPattern(typingsLocation);
	}
	std::vector<std::string> globList;
	globList.reserve(globs.size());
	for (const auto& kv : globs) {
		globList.push_back(kv.second);
	}
	return PatternsAndIgnored{externalDirectoryParents, globList, ignored};
}

// getPathComponentsForWatching — watch.go:424.
std::vector<std::string> getPathComponentsForWatching(
	std::string_view path, std::string_view currentDirectory) {
	auto components = tspath::resolvePathComponents(path, currentDirectory);
	int rootLength = perceivedOsRootLengthForWatching(components);
	if (rootLength <= 1) {
		return components;
	}
	auto newRoot = tspath::combinePaths(
	    components[0],
	    std::vector<std::string_view>(components.begin() + 1,
	                                  components.begin() + rootLength));
	std::vector<std::string> result{newRoot};
	result.insert(result.end(), components.begin() + rootLength,
	              components.end());
	return result;
}

// perceivedOsRootLengthForWatching — watch.go:434.
int perceivedOsRootLengthForWatching(
	const std::vector<std::string>& pathComponents) {
	size_t length = pathComponents.size();
	if (length <= 1) {
		return static_cast<int>(length);
	}
	if (pathComponents[0].starts_with("//")) {
		// Group UNC roots (//server/share) into a single component
		return 2;
	}
	if (pathComponents[0].size() == 3 &&
	    tspath::isVolumeCharacter(pathComponents[0][0]) &&
	    pathComponents[0][1] == ':' && pathComponents[0][2] == '/') {
		// Windows-style volume
		if (stringutil::EquateStringCaseInsensitive(pathComponents[1],
		                                             "users")) {
			// Group C:/Users/username into a single component
			return std::min<size_t>(3, length);
		}
		return 1;
	}
	if (pathComponents[1] == "home") {
		// Group /home/username into a single component
		return std::min<size_t>(3, length);
	}
	return 1;
}

} // namespace tsc::project
