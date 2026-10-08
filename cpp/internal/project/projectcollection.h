// projectcollection.go — ProjectCollection + APIState + apiOpenedFile +
// openFilePaths + findDefaultConfiguredProjectFromProgramInclusion.
//
// projectcollection.h
#pragma once

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/core/utilities.h"
#include "internal/compiler/program.h"
#include "internal/project/configfileregistry.h"
#include "internal/project/overlayfs.h"
#include "internal/project/project.h"
#include "internal/tspath/tspath.h"

namespace tsc::project {

// apiOpenedFile — projectcollection.go:53. Tracks a file kept open by
// API clients along with its ref count.
struct apiOpenedFile {
	std::string fileName;
	int refCount = 0;

	bool operator==(const apiOpenedFile&) const = default;
};

// APIState — projectcollection.go:39. Tracks the projects and files
// that API clients have explicitly opened. Opens and closes are
// ref-counted so multiple API clients don't clobber each other, and it
// is carried across snapshots so API-opened resources stay loaded.
struct APIState {
	// openProjects is the ref-counted set of projects to keep open for
	// API clients, keyed by config file path.
	std::unordered_map<tspath::Path, int> openProjects;
	// openFiles is the ref-counted set of files to keep open for API
	// clients, keyed by file path. Files with no configured project are
	// loaded into the inferred project.
	std::unordered_map<tspath::Path, apiOpenedFile> openFiles;

	// clone — projectcollection.go:45.
	APIState clone() const { return *this; }

	// equals — projectcollection.go:52.
	bool equals(const APIState& other) const {
		return openProjects == other.openProjects &&
		       openFiles == other.openFiles;
	}
};

struct ProjectCollection {
	std::function<tspath::Path(const std::string&)> toPath;
	ConfigFileRegistry* configFileRegistry = nullptr;
	// fileDefaultProjects is a map of file paths to the ID of the
	// default project for that file. This map contains quick lookups
	// for only the associations discovered during the latest snapshot
	// update.
	std::unordered_map<tspath::Path, ID> fileDefaultProjects;
	// configuredProjects is the set of loaded projects associated with
	// a tsconfig file, keyed by the config file path.
	std::unordered_map<ConfiguredProjectID, Project*>
	    configuredProjects;
	// syntheticProjects contains synthetic projects created explicitly
	// through the API.
	std::unordered_map<SyntheticProjectID, Project*> syntheticProjects;
	// openFiles is the set of open file paths associated with the
	// snapshot that owns this project collection.
	collections::Set<tspath::Path> openFiles;
	// inferredProject is a fallback project that is used when no
	// configured project can be found for an open file.
	Project* inferredProject = nullptr;
	// apiState tracks the projects and files that API clients have
	// explicitly opened so they are kept loaded across snapshots.
	APIState apiState;

	std::once_flag openConfiguredProjectsOnce;
	collections::Set<ConfiguredProjectID>* openConfiguredProjects =
	    nullptr;

	// ConfigFileRegistry — projectcollection.go:65.
	project::ConfigFileRegistry* ConfigFileRegistry() const {
		return configFileRegistry;
	}

	// ConfiguredProject — projectcollection.go:67.
	Project* ConfiguredProject(const tspath::Path& path) const {
		auto it = configuredProjects.find(ConfiguredProjectID{path});
		return it != configuredProjects.end() ? it->second : nullptr;
	}

	// GetProject — projectcollection.go:71.
	Project* GetProject(const ID& id) const {
		if (auto res = idInferred(id); res.second) {
			return inferredProject;
		}
		if (auto syntheticID = idSynthetic(id); syntheticID.second) {
			auto it = syntheticProjects.find(syntheticID.first);
			return it != syntheticProjects.end() ? it->second
			                                     : nullptr;
		}
		if (auto configuredID = idConfigured(id);
		    configuredID.second) {
			auto it = configuredProjects.find(configuredID.first);
			return it != configuredProjects.end() ? it->second
			                                      : nullptr;
		}
		return nullptr;
	}

	// fillConfiguredProjects — projectcollection.go:86.
	void fillConfiguredProjects(std::vector<Project*>* projects) const {
		for (auto& [id, p] : configuredProjects) {
			projects->push_back(p);
		}
		std::sort(projects->begin(), projects->end(),
		          [](Project* a, Project* b) {
			          return a->ID() < b->ID();
		          });
	}

	// ConfiguredProjects — projectcollection.go:81. All configured
	// projects in a stable order.
	std::vector<Project*> ConfiguredProjects() const {
		std::vector<Project*> projects;
		projects.reserve(configuredProjects.size());
		fillConfiguredProjects(&projects);
		return projects;
	}

	// SyntheticProjects — projectcollection.go:95. All synthetic
	// projects in a stable order.
	std::vector<Project*> SyntheticProjects() const {
		std::vector<Project*> projects;
		projects.reserve(syntheticProjects.size());
		for (auto& [id, p] : syntheticProjects) {
			projects.push_back(p);
		}
		std::sort(projects.begin(), projects.end(),
		          [](Project* a, Project* b) {
			          return a->ID() < b->ID();
		          });
		return projects;
	}

	// ProjectsByID — projectcollection.go:105. All projects keyed by
	// project ID in stable order.
	collections::OrderedMap<ID, Project*>* ProjectsByID() const {
		auto* projects = new collections::OrderedMap<ID, Project*>(
		    configuredProjects.size() + syntheticProjects.size() +
		    (inferredProject != nullptr ? 1 : 0));
		for (auto* project : ConfiguredProjects()) {
			projects->Set(project->ID(), project);
		}
		for (auto* project : SyntheticProjects()) {
			projects->Set(project->ID(), project);
		}
		if (inferredProject != nullptr) {
			projects->Set(inferredProject->ID(), inferredProject);
		}
		return projects;
	}

	// Projects — projectcollection.go:118. All configured, synthetic,
	// and inferred projects in a stable order.
	std::vector<Project*> Projects() const {
		std::vector<Project*> projects;
		projects.reserve(configuredProjects.size() +
		                 syntheticProjects.size() +
		                 (inferredProject != nullptr ? 1 : 0));
		fillConfiguredProjects(&projects);
		for (auto* p : SyntheticProjects()) {
			projects.push_back(p);
		}
		if (inferredProject != nullptr) {
			projects.push_back(inferredProject);
		}
		return projects;
	}

	// LanguageServiceProjects — projectcollection.go:130. Configured
	// and inferred projects in stable order. Synthetic projects are
	// accessed explicitly through the API and do not participate in
	// cross-project language service operations.
	std::vector<Project*> LanguageServiceProjects() const {
		std::vector<Project*> projects;
		projects.reserve(configuredProjects.size() +
		                 (inferredProject != nullptr ? 1 : 0));
		fillConfiguredProjects(&projects);
		if (inferredProject != nullptr) {
			projects.push_back(inferredProject);
		}
		return projects;
	}

	// InferredProject — projectcollection.go:139.
	Project* InferredProject() const { return inferredProject; }

	// GetLanguageServiceProjectsContainingFile —
	// projectcollection.go:143. Does not consider synthetic projects
	// (ones created by API via createProgram).
	std::vector<ls::Project*>
	GetLanguageServiceProjectsContainingFile(
	    const tspath::Path& path) const {
		std::vector<ls::Project*> projects;
		for (auto* project : ConfiguredProjects()) {
			if (project->containsFile(path)) {
				projects.push_back(project);
			}
		}
		if (inferredProject != nullptr &&
		    inferredProject->containsFile(path)) {
			projects.push_back(inferredProject);
		}
		return projects;
	}

	// GetOpenConfiguredProjects — projectcollection.go:155. Configured
	// projects containing at least one open file.
	collections::Set<ConfiguredProjectID>*
	GetOpenConfiguredProjects() {
		std::call_once(openConfiguredProjectsOnce, [&] {
			auto* openProjects =
			    new collections::Set<ConfiguredProjectID>(
			        configuredProjects.size());
			for (auto& path : openFiles.Keys()) {
				auto it = fileDefaultProjects.find(path);
				if (it != fileDefaultProjects.end()) {
					auto configuredID = idConfigured(it->second);
					if (configuredID.second &&
					    configuredProjects.count(
					        configuredID.first) != 0) {
						openProjects->Add(configuredID.first);
						continue;
					}
				}
				for (auto& [id, project] : configuredProjects) {
					if (project->containsFile(path)) {
						auto configuredID =
						    idConfigured(project->ID());
						openProjects->Add(configuredID.first);
					}
				}
			}
			openConfiguredProjects = openProjects;
		});
		return openConfiguredProjects;
	}

	// GetDefaultProject — projectcollection.go:212.
	Project* GetDefaultProject(const tspath::Path& path) const;

	// findDefaultConfiguredProject — projectcollection.go:256.
	Project* findDefaultConfiguredProject(const tspath::Path& path) const;

	// findDefaultConfiguredProjectWorker — projectcollection.go:263.
	Project* findDefaultConfiguredProjectWorker(
	    const tspath::Path& path, const std::string& configFileName,
	    collections::SyncSet<Project*>* visited,
	    Project* fallback) const;

	// clone — projectcollection.go:307. Shallow copy.
	ProjectCollection* clone() const {
		auto* c = new ProjectCollection{};
		c->toPath = toPath;
		c->configFileRegistry = configFileRegistry;
		c->configuredProjects = configuredProjects;
		c->syntheticProjects = syntheticProjects;
		c->openFiles = openFiles;
		c->inferredProject = inferredProject;
		c->fileDefaultProjects = fileDefaultProjects;
		c->apiState = apiState;
		return c;
	}
};

// openFilePaths — projectcollection.go:204.
inline collections::Set<tspath::Path>
openFilePaths(
    const std::unordered_map<tspath::Path, Overlay*>& overlays) {
	collections::Set<tspath::Path> openFiles(overlays.size());
	for (auto& [path, o] : overlays) {
		openFiles.Add(path);
	}
	return openFiles;
}

// findDefaultConfiguredProjectFromProgramInclusion —
// projectcollection.go:319. Finds the default configured project for
// a file based on the file's inclusion in existing projects. The
// projects should be sorted, as ties will be broken by slice order.
// `getProject` should return a project with an up-to-date program.
// Along with the resulting project path, a boolean is returned
// indicating whether there were multiple direct inclusions of the
// file in different projects, indicating that the caller may want to
// perform additional logic to determine the best project.
std::pair<tspath::Path, bool>
findDefaultConfiguredProjectFromProgramInclusion(
    const std::string& fileName, const tspath::Path& path,
    const std::vector<tspath::Path>& projectPaths,
    const std::function<Project*(const tspath::Path&)>& getProject);

} // namespace tsc::project
