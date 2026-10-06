// projectcollection.go — ProjectCollection methods.
//
// === slice: project ===

#include "internal/project/projectcollection.h"

#include "internal/core/utilities.h"

namespace tsc::project {

// GetDefaultProject — projectcollection.go:212. !!! result could be
// cached.
Project* ProjectCollection::GetDefaultProject(
    const tspath::Path& path) const {
	auto it = fileDefaultProjects.find(path);
	if (it != fileDefaultProjects.end()) {
		auto& result = it->second;
		if (idInferred(result).second) {
			return inferredProject;
		}
		auto configuredID = idConfigured(result);
		auto jt = configuredProjects.find(configuredID.first);
		return jt != configuredProjects.end() ? jt->second : nullptr;
	}

	std::vector<Project*> containingProjects;
	Project* firstConfiguredProject = nullptr;
	Project* firstNonSourceOfProjectReferenceRedirect = nullptr;
	bool multipleDirectInclusions = false;
	for (auto* p : ConfiguredProjects()) {
		if (p->containsFile(path)) {
			containingProjects.push_back(p);
			if (!multipleDirectInclusions &&
			    !p->IsSourceFromProjectReference(path)) {
				if (firstNonSourceOfProjectReferenceRedirect ==
				    nullptr) {
					firstNonSourceOfProjectReferenceRedirect = p;
				} else {
					multipleDirectInclusions = true;
				}
			}
			if (firstConfiguredProject == nullptr) {
				firstConfiguredProject = p;
			}
		}
	}
	if (containingProjects.size() == 1) {
		return containingProjects[0];
	}
	if (containingProjects.empty()) {
		if (inferredProject != nullptr &&
		    inferredProject->containsFile(path)) {
			return inferredProject;
		}
		return nullptr;
	}
	if (!multipleDirectInclusions) {
		if (firstNonSourceOfProjectReferenceRedirect != nullptr) {
			// Multiple projects include the file, but only one is a
			// direct inclusion.
			return firstNonSourceOfProjectReferenceRedirect;
		}
		// Multiple projects include the file, and none are direct
		// inclusions.
		return firstConfiguredProject;
	}
	// Multiple projects include the file directly.
	if (auto* defaultProject = findDefaultConfiguredProject(path);
	    defaultProject != nullptr) {
		return defaultProject;
	}
	return firstConfiguredProject;
}

// findDefaultConfiguredProject — projectcollection.go:256.
Project* ProjectCollection::findDefaultConfiguredProject(
    const tspath::Path& path) const {
	if (auto configFileName =
	        configFileRegistry->GetConfigFileName(path);
	    !configFileName.empty()) {
		return findDefaultConfiguredProjectWorker(
		    path, configFileName, nullptr, nullptr);
	}
	return nullptr;
}

// findDefaultConfiguredProjectWorker — projectcollection.go:263.
Project* ProjectCollection::findDefaultConfiguredProjectWorker(
    const tspath::Path& path, const std::string& configFileName,
    collections::SyncSet<Project*>* visited,
    Project* fallback) const {
	auto configFilePath = toPath(configFileName);
	auto pit = configuredProjects.find(
	    ConfiguredProjectID{configFilePath});
	if (pit == configuredProjects.end()) {
		return nullptr;
	}
	auto* project = pit->second;
	collections::SyncSet<Project*> ownedVisited;
	if (visited == nullptr) {
		visited = &ownedVisited;
	}

	// Look in the config's project and its references recursively.
	auto search = tsc::BreadthFirstSearchParallelEx<Project*, Project*>(
	    project,
	    [&](Project* p) {
		    if (p->CommandLine == nullptr) {
			    return std::vector<Project*>{};
		    }
		    // A referenced project may not be loaded if
		    // `disableReferencedProjectLoad` is true.
		    return tsc::MapNonNil<std::string, Project*>(
		        p->CommandLine->ResolvedProjectReferencePaths(),
		        [&](const std::string& refConfigFileName) {
			        auto jt = configuredProjects.find(
			            ConfiguredProjectID{
			                toPath(refConfigFileName)});
			        return jt != configuredProjects.end()
			                   ? jt->second
			                   : nullptr;
		        });
	    },
	    [&](Project* p) {
		    if (p->containsFile(path)) {
			    return std::pair{true,
			                     !p->IsSourceFromProjectReference(path)};
		    }
		    return std::pair{false, false};
	    },
	    tsc::BreadthFirstSearchOptions<Project*, Project*>{
	        .Visited = visited,
	    },
	    tsc::Identity<Project*>);

	if (search.Stopped) {
		// If we found a project that directly contains the file,
		// return it.
		return search.Path[0];
	}
	if (!search.Path.empty() && fallback == nullptr) {
		// If we found a project that contains the file, but it is a
		// source from a project reference, record it as a fallback.
		fallback = search.Path[0];
	}

	// Look for tsconfig.json files higher up the directory tree and do
	// the same. This handles the common case where a higher-level
	// "solution" tsconfig.json contains all projects in a workspace.
	if (auto* config = configFileRegistry->GetConfig(path);
	    config != nullptr &&
	    config->CompilerOptions()->DisableSolutionSearching == Tristate::True) {
		return fallback;
	}
	if (auto ancestorConfigName =
	        configFileRegistry->GetAncestorConfigFileName(
	            path, configFileName);
	    !ancestorConfigName.empty()) {
		return findDefaultConfiguredProjectWorker(
		    path, ancestorConfigName, visited, fallback);
	}
	return fallback;
}

// findDefaultConfiguredProjectFromProgramInclusion —
// projectcollection.go:319.
std::pair<tspath::Path, bool>
findDefaultConfiguredProjectFromProgramInclusion(
    const std::string& fileName, const tspath::Path& path,
    const std::vector<tspath::Path>& projectPaths,
    const std::function<Project*(const tspath::Path&)>& getProject) {
	std::vector<tspath::Path> containingProjects;
	tspath::Path firstConfiguredProject;
	tspath::Path firstNonSourceOfProjectReferenceRedirect;
	bool multipleDirectInclusions = false;

	for (auto& projectPath : projectPaths) {
		auto* p = getProject(projectPath);
		if (p->containsFile(path)) {
			containingProjects.push_back(projectPath);
			if (!multipleDirectInclusions &&
			    !p->IsSourceFromProjectReference(path)) {
				if (firstNonSourceOfProjectReferenceRedirect
				        .empty()) {
					firstNonSourceOfProjectReferenceRedirect =
					    projectPath;
				} else {
					multipleDirectInclusions = true;
				}
			}
			if (firstConfiguredProject.empty()) {
				firstConfiguredProject = projectPath;
			}
		}
	}

	if (containingProjects.size() == 1) {
		return {containingProjects[0], false};
	}
	if (!multipleDirectInclusions) {
		if (!firstNonSourceOfProjectReferenceRedirect.empty()) {
			// Multiple projects include the file, but only one is a
			// direct inclusion.
			return {firstNonSourceOfProjectReferenceRedirect, false};
		}
		// Multiple projects include the file, and none are direct
		// inclusions.
		return {firstConfiguredProject, false};
	}
	// Multiple projects include the file directly.
	return {firstConfiguredProject, true};
}

} // namespace tsc::project
