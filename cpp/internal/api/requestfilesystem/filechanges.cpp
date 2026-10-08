// api/requestfilesystem/filechanges.cpp — filechanges.go.

#include "internal/api/requestfilesystem/requestfilesystem.h"

#include "internal/ls/lsconv/lsconv.h"

#include <set>

namespace tsc::api::requestfilesystem {

// ExpandFileChanges (filechanges.go:12).
project::FileChangeSummary requestFileSystem::ExpandFileChanges(
    project::FileChangeSummary summary) {
	auto expand = [this](collections::Set<lsproto::DocumentUri>* uris) {
		collections::Set<lsproto::DocumentUri> additional;
		for (auto& uri : uris->Keys()) {
			for (auto& alias : aliasesForPath(lsp::lsproto::documentUriFileName(uri))) {
				additional.Add(lsconv::FileNameToDocumentURI(alias));
			}
		}
		for (auto& uri : additional.Keys()) {
			uris->Add(uri);
		}
	};
	expand(&summary.Changed);
	expand(&summary.Created);
	expand(&summary.Deleted);
	return summary;
}

// addFileChanges (filechanges.go:16).
void addFileChanges(project::FileChangeSummary* summary,
                    RequestFileSystem* request,
                    const std::shared_ptr<vfs::FS>& baseFS,
                    requestFileSystem* fileSystem,
                    const std::string& currentDirectory) {
	auto toPath = [&](const std::string& fileName) {
		return tspath::toPath(fileName, currentDirectory,
		                      baseFS->UseCaseSensitiveFileNames());
	};
	requestFileSystem* baseRequestFS = getRequestFileSystem(baseFS);
	auto addChange = [&](const std::string& fileName, bool deleted) {
		lsproto::DocumentUri uri = lsconv::FileNameToDocumentURI(fileName);
		if (deleted) {
			if (baseFS->FileExists(fileName) ||
			    baseFS->DirectoryExists(fileName)) {
				summary->Deleted.Add(uri);
			}
			return;
		}
		if (baseFS->FileExists(fileName)) {
			summary->Changed.Add(uri);
		} else {
			summary->Created.Add(uri);
		}
	};
	auto addChangeAndAliases = [&](const std::string& fileName, bool deleted) {
		addChange(fileName, deleted);
		if (baseRequestFS != nullptr) {
			for (auto& alias : baseRequestFS->aliasesForPath(fileName)) {
				addChange(alias, deleted);
			}
		}
	};
	std::set<tspath::Path> overlayFiles;
	for (auto& [fileName, _] : request->Files) {
		std::string absoluteFileName =
		    tspath::getNormalizedAbsolutePath(fileName, currentDirectory);
		overlayFiles.insert(toPath(absoluteFileName));
		addChangeAndAliases(absoluteFileName, false);
	}
	if (request->RemovedPaths.has_value()) {
		for (auto& removedPath : *request->RemovedPaths) {
			std::string absoluteFileName =
			    tspath::getNormalizedAbsolutePath(removedPath,
			                                      currentDirectory);
			if (overlayFiles.count(toPath(absoluteFileName))) {
				continue;
			}
			addChangeAndAliases(absoluteFileName, true);
		}
	}
	// Replacing a listing or a symlink can change every cached descendant.
	// Delete events expand through the snapshot's cached directory tree and
	// create events that refresh wildcard roots and previously missing module
	// resolutions.
	auto addReplacement = [&](const std::string& path) {
		std::string absolutePath =
		    tspath::getNormalizedAbsolutePath(path, currentDirectory);
		addChangeAndAliases(absolutePath, true);
		summary->Created.Add(lsconv::FileNameToDocumentURI(absolutePath));
		if (baseRequestFS != nullptr) {
			for (auto& alias : baseRequestFS->aliasesForPath(absolutePath)) {
				summary->Created.Add(lsconv::FileNameToDocumentURI(alias));
			}
		}
	};
	if (request->Directories.has_value()) {
		for (auto& [directoryName, _] : *request->Directories) {
			addReplacement(directoryName);
		}
	}
	if (request->Symlinks.has_value()) {
		for (auto& [linkName, _] : *request->Symlinks) {
			addReplacement(linkName);
		}
	}
	if (auto* layeredBase =
	        dynamic_cast<project::LayeredFileSystem*>(baseFS.get())) {
		auto overlays = fileSystem->Overlays();
		for (auto& [path, overlay] : layeredBase->Overlays()) {
			if (overlays.count(path)) {
				continue;
			}
			lsproto::DocumentUri uri =
			    lsconv::FileNameToDocumentURI(overlay->FileName());
			if (summary->Closed.Has(uri)) {
				continue;
			}
			summary->Created.Delete(uri);
			if (fileSystem->FileExists(overlay->FileName())) {
				summary->Deleted.Delete(uri);
				summary->Changed.Add(uri);
			} else {
				summary->Changed.Delete(uri);
				summary->Deleted.Add(uri);
			}
		}
	}
	if (summary->Changed.Size() + summary->Created.Size() +
	        summary->Deleted.Size() >
	    0) {
		summary->IncludesWatchChangeOutsideNodeModules = true;
	}
}

}  // namespace tsc::api::requestfilesystem
