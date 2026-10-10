// configfileregistry.go — ConfigFileRegistry + configFileEntry +
// configFileNames + configuredContentMappers + changeFileResult.
//
// configfileregistry.h
#pragma once

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/core/utilities.h"
#include "internal/gostd/goseq.h"
#include "internal/project/project.h"
#include "internal/project/watch.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

namespace tsc::project {

// configuredContentMappers — configfileregistry.go:28.
struct configuredContentMappers {
	std::vector<std::string> extensions;
};

// collectConfiguredContentMappers — configfileregistry.go:32.
inline configuredContentMappers* collectConfiguredContentMappers(
    const std::vector<tsoptions::ParsedCommandLine*>& commandLines) {
	collections::Set<std::string> seenExtensions;
	std::vector<std::string> extensions;
	for (auto* commandLine : commandLines) {
		for (auto* mapper : commandLine->ContentMappers()) {
			for (auto& extension : mapper->Definition.Extensions) {
				if (seenExtensions.AddIfAbsent(extension)) {
					extensions.push_back(extension);
				}
			}
		}
	}
	std::sort(extensions.begin(), extensions.end());
	return new configuredContentMappers{extensions};
}

// configFileEntry — configfileregistry.go:61.
struct configFileEntry {
	std::string fileName;
	PendingReload pendingReload = PendingReloadNone;
	tsoptions::ParsedCommandLine* commandLine = nullptr;
	// retainingProjects is the set of projects that have called
	// acquireConfig without releasing it. A config file entry may be
	// acquired by a project either because it is the config for that
	// project or because it is the config for a referenced project.
	std::unordered_map<ID, std::monostate> retainingProjects;
	// retainingOpenFiles is the set of open files that caused this
	// config to load during project collection building. This config
	// file may or may not end up being the config for the default
	// project for these files, but determining the default project
	// loaded this config as a candidate, so subsequent calls to
	// `projectCollectionBuilder.findDefaultConfiguredProject` will
	// use this config as part of the search, so it must be retained.
	std::unordered_map<tspath::Path, std::monostate> retainingOpenFiles;
	// retainingConfigs is the set of config files that extend this
	// one. This provides a cheap reverse mapping for a project
	// config's `commandLine.ExtendedSourceFiles()` that can be used
	// to notify the extending projects when this config changes. An
	// extended config file may or may not also be used directly by a
	// project, so it's possible that when this is set, no other
	// fields will be used.
	std::unordered_map<tspath::Path, std::monostate> retainingConfigs;
	// rootFilesWatch is a watch for the root files of this config
	// file.
	WatchedFiles<PatternsAndIgnored>* rootFilesWatch = nullptr;

	// Clone — configfileregistry.go:109. !!! eagerly cloning these
	// maps makes everything more convenient, but it could be avoided
	// if needed.
	configFileEntry* Clone() const {
		auto* c = new configFileEntry{};
		c->fileName = fileName;
		c->pendingReload = pendingReload;
		c->commandLine = commandLine;
		c->retainingProjects = retainingProjects;
		c->retainingOpenFiles = retainingOpenFiles;
		c->retainingConfigs = retainingConfigs;
		c->rootFilesWatch = rootFilesWatch;
		return c;
	}
};

// newConfigFileEntry — configfileregistry.go:88.
inline configFileEntry* newConfigFileEntry(
    bool hasRelativePatternCapability, const std::string& fileName) {
	auto* e = new configFileEntry{};
	e->fileName = fileName;
	e->pendingReload = PendingReloadFull;
	e->rootFilesWatch = newWatchedFiles(
	    "root files for " + fileName,
	    lsp::lsproto::WatchKindCreate |
	        lsp::lsproto::WatchKindChange |
	        lsp::lsproto::WatchKindDelete,
	    hasRelativePatternCapability,
	    std::function<PatternsAndIgnored(PatternsAndIgnored)>(
	        Identity<PatternsAndIgnored>));
	return e;
}

// newExtendedConfigFileEntry — configfileregistry.go:101.
inline configFileEntry* newExtendedConfigFileEntry(
    const std::string& fileName,
    const tspath::Path& extendingConfigPath) {
	auto* e = new configFileEntry{};
	e->fileName = fileName;
	e->pendingReload = PendingReloadFull;
	e->retainingConfigs.emplace(extendingConfigPath, std::monostate{});
	return e;
}

// configFileNames — configfileregistry.go:226.
struct configFileNames {
	// nearestConfigFileName is the file name of the nearest ancestor
	// config file.
	std::string nearestConfigFileName;
	// ancestors is a map from one ancestor config file path to the
	// next. For example, if `/a`, `/a/b`, and `/a/b/c` all contain
	// config files, the fully loaded map will look like:
	//		{
	//			"/a/b/c/tsconfig.json": "/a/b/tsconfig.json",
	//			"/a/b/tsconfig.json": "/a/tsconfig.json"
	//		}
	std::unordered_map<std::string, std::string> ancestors;

	// Clone — configfileregistry.go:239.
	configFileNames* Clone() const { return new configFileNames(*this); }
};

// TestConfigEntry — configfileregistry.go:160 (For testing).
struct TestConfigEntry {
	std::string FileName;
	goseq::Seq<project::ID> RetainingProjects;
	goseq::Seq<tspath::Path> RetainingOpenFiles;
	goseq::Seq<tspath::Path> RetainingConfigs;
};

// TestConfigFileNamesEntry — configfileregistry.go:195 (For testing).
struct TestConfigFileNamesEntry {
	std::string NearestConfigFileName;
	std::unordered_map<std::string, std::string> Ancestors;
};

// ConfigFileRegistry — configfileregistry.go:15.
struct ConfigFileRegistry {
	// configs is a map of config file paths to their entries.
	std::unordered_map<tspath::Path, configFileEntry*> configs;
	// configFileNames is a map of open file paths to information
	// about their ancestor config file names. It is only used as a
	// cache during [building].
	std::unordered_map<tspath::Path, configFileNames*> configFileNames;
	// customConfigFileName is the custom config file name preference
	// that was used when building this registry's configFileNames
	// cache.
	std::string customConfigFileName;
	configuredContentMappers* allConfiguredContentMappers = nullptr;

	// contentMappers — configfileregistry.go:48.
	configuredContentMappers* contentMappers() const {
		if (allConfiguredContentMappers != nullptr) {
			return allConfiguredContentMappers;
		}
		std::vector<tsoptions::ParsedCommandLine*> commandLines;
		for (auto& [path, entry] : configs) {
			if (entry->commandLine != nullptr) {
				commandLines.push_back(entry->commandLine);
			}
		}
		return collectConfiguredContentMappers(commandLines);
	}

	// GetConfig — configfileregistry.go:123.
	tsoptions::ParsedCommandLine*
	GetConfig(const tspath::Path& path) const {
		auto it = configs.find(path);
		if (it != configs.end()) {
			return it->second->commandLine;
		}
		return nullptr;
	}

	// isTracked — configfileregistry.go:130.
	bool isTracked(const tspath::Path& path) const {
		return configs.count(path) != 0;
	}

	// GetConfigFileName — configfileregistry.go:135.
	std::string
	GetConfigFileName(const tspath::Path& path) const {
		auto it = configFileNames.find(path);
		if (it != configFileNames.end()) {
			return it->second->nearestConfigFileName;
		}
		return "";
	}

	// GetAncestorConfigFileName — configfileregistry.go:142.
	std::string GetAncestorConfigFileName(
	    const tspath::Path& path,
	    const std::string& higherThanConfig) const {
		auto it = configFileNames.find(path);
		if (it != configFileNames.end()) {
			auto jt = it->second->ancestors.find(higherThanConfig);
			if (jt != it->second->ancestors.end()) {
				return jt->second;
			}
		}
		return "";
	}

	// clone — configfileregistry.go:150. Shallow copy.
	ConfigFileRegistry* clone() const {
		auto* r = new ConfigFileRegistry{};
		r->configs = configs;
		r->configFileNames = configFileNames;
		r->customConfigFileName = customConfigFileName;
		r->allConfiguredContentMappers = allConfiguredContentMappers;
		return r;
	}

	// ForEachTestConfigEntry — configfileregistry.go:168 (For testing).
	// Go tolerates a nil receiver (`if c != nil`) — callers that may hold
	// a null registry guard at the call site (statebaseline.cpp).
	template <typename Cb>
	void ForEachTestConfigEntry(Cb&& cb) const {
		for (auto& [path, entry] : configs) {
			cb(path, new TestConfigEntry{
			           entry->fileName,
			           goseq::keysSeq(entry->retainingProjects),
			           goseq::keysSeq(entry->retainingOpenFiles),
			           goseq::keysSeq(entry->retainingConfigs),
			       });
		}
	}

	// GetTestConfigEntry — configfileregistry.go:182 (For testing).
	TestConfigEntry* GetTestConfigEntry(const tspath::Path& path) const {
		if (auto it = configs.find(path); it != configs.end()) {
			auto* entry = it->second;
			return new TestConfigEntry{
			    entry->fileName,
			    goseq::keysSeq(entry->retainingProjects),
			    goseq::keysSeq(entry->retainingOpenFiles),
			    goseq::keysSeq(entry->retainingConfigs),
			};
		}
		return nullptr;
	}

	// ForEachTestConfigFileNamesEntry — configfileregistry.go:198
	// (For testing).
	template <typename Cb>
	void ForEachTestConfigFileNamesEntry(Cb&& cb) const {
		for (auto& [path, entry] : configFileNames) {
			cb(path, new TestConfigFileNamesEntry{
			           entry->nearestConfigFileName, entry->ancestors,
			       });
		}
	}

	// GetTestConfigFileNamesEntry — configfileregistry.go:210
	// (For testing).
	TestConfigFileNamesEntry*
	GetTestConfigFileNamesEntry(const tspath::Path& path) const {
		if (auto it = configFileNames.find(path);
		    it != configFileNames.end()) {
			return new TestConfigFileNamesEntry{
			    it->second->nearestConfigFileName,
			    it->second->ancestors,
			};
		}
		return nullptr;
	}
};

// changeFileResult — configfileregistrybuilder.go:395.
struct changeFileResult {
	std::unordered_map<ID, std::monostate>* affectedProjects = nullptr;
	std::unordered_map<tspath::Path, std::monostate>* affectedFiles =
	    nullptr;

	// IsEmpty — configfileregistrybuilder.go:400.
	bool IsEmpty() const {
		return (affectedProjects == nullptr ||
		        affectedProjects->empty()) &&
		       (affectedFiles == nullptr || affectedFiles->empty());
	}
};

} // namespace tsc::project
