// === slice: project ===
// overlayfs.go — the non-trivial bodies: createOverlayDirectories,
// GetAccessibleEntries, Stat, GetFileByPath, processChanges.

#include "internal/project/overlayfs.h"
#include "internal/ls/lsdeps.h" // lsconv::convertersFromLSPRange

#include <algorithm>
#include <cstring>

namespace tsc::project {

// createOverlayDirectories — overlayfs.go:384.
std::unordered_map<tspath::Path,
                   std::unordered_map<tspath::Path, std::string>>
createOverlayDirectories(
	const std::unordered_map<tspath::Path, Overlay*>& overlays) {
	std::unordered_map<tspath::Path,
	                   std::unordered_map<tspath::Path, std::string>>
	    overlayDirectories;
	for (const auto& kv : overlays) {
		const auto& path = kv.first;
		auto* overlay = kv.second;
		auto childPath = path;
		auto child = overlay->FileName();
		for (;;) {
			auto parentPath = tspath::getDirectoryPath(childPath);
			auto parent = tspath::getDirectoryPath(child);
			if (childPath == parentPath) {
				break;
			}
			auto& directory = overlayDirectories[parentPath];
			directory[childPath] =
			    std::string(tspath::getBaseFileName(child));
			childPath = parentPath;
			child = parent;
		}
	}
	return overlayDirectories;
}

// GetFileByPath — overlayfs.go:247.
FileHandle* overlayFS::GetFileByPath(const std::string& fileName,
                                     const tspath::Path& path) {
	std::shared_lock<std::shared_mutex> lk(mu);
	Overlay* overlay = nullptr;
	if (auto it = overlays.find(path); it != overlays.end()) {
		overlay = it->second;
	}
	bool directory = overlayDirectories.count(path) != 0;
	lk.unlock();
	if (overlay != nullptr) {
		return overlay;
	}
	if (directory) {
		return nullptr;
	}

	if (auto* source = dynamic_cast<FileHandleSource*>(host)) {
		return source->GetFileByPath(fileName, path);
	}
	auto [content, ok] = host->ReadFile(fileName);
	if (!ok) {
		return nullptr;
	}
	return new cachedFile(fileName, content);
}

// GetAccessibleEntries — overlayfs.go:306.
vfs::Entries
overlayFS::GetAccessibleEntries(const std::string& directoryName) {
	std::shared_lock<std::shared_mutex> lk(mu);
	auto path = toPath(directoryName);
	bool file = overlays.count(path) != 0;
	std::unordered_map<tspath::Path, std::string> directory;
	if (auto it = overlayDirectories.find(path);
	    it != overlayDirectories.end()) {
		directory = it->second; // maps.Clone
	}
	auto overlaysSnapshot = overlays;
	lk.unlock();
	if (file) {
		return vfs::Entries{};
	}
	auto hostEntries = host->GetAccessibleEntries(directoryName);
	vfs::Entries entries;
	entries.files = hostEntries.files;
	entries.directories = hostEntries.directories;
	entries.symlinks = hostEntries.symlinks;
	auto equalName = [&](const std::string& left,
	                     const std::string& right) {
		return tspath::getCanonicalFileName(
		           left, UseCaseSensitiveFileNames()) ==
		       tspath::getCanonicalFileName(
		           right, UseCaseSensitiveFileNames());
	};
	for (const auto& kv : directory) {
		const auto& childPath = kv.first;
		const auto& childName = kv.second;
		entries.files.erase(
		    std::remove_if(entries.files.begin(), entries.files.end(),
		                   [&](const std::string& name) {
			                   return equalName(name, childName);
		                   }),
		    entries.files.end());
		entries.directories.erase(
		    std::remove_if(entries.directories.begin(),
		                   entries.directories.end(),
		                   [&](const std::string& name) {
			                   return equalName(name, childName);
		                   }),
		    entries.directories.end());
		if (entries.symlinks) {
			for (auto it = entries.symlinks->begin();
			     it != entries.symlinks->end();) {
				if (equalName(*it, childName)) {
					it = entries.symlinks->erase(it);
				} else {
					++it;
				}
			}
		}
		if (overlaysSnapshot.count(childPath) != 0) {
			entries.files.push_back(childName);
		} else {
			entries.directories.push_back(childName);
		}
	}
	return entries;
}

// Stat — overlayfs.go:345.
std::shared_ptr<vfs::FileInfo> overlayFS::Stat(const std::string& path) {
	std::shared_lock<std::shared_mutex> lk(mu);
	auto canonicalPath = toPath(path);
	Overlay* overlay = nullptr;
	if (auto it = overlays.find(canonicalPath); it != overlays.end()) {
		overlay = it->second;
	}
	bool directory = overlayDirectories.count(canonicalPath) != 0;
	lk.unlock();
	if (overlay != nullptr) {
		return std::make_shared<overlayFileInfo>(overlay);
	}
	if (directory) {
		return std::make_shared<overlayDirectoryInfo>(
		    std::string(tspath::getBaseFileName(path)));
	}
	return host->Stat(path);
}

// processChanges — overlayfs.go:407.
std::pair<FileChangeSummary, std::unordered_map<tspath::Path, Overlay*>>
overlayFS::processChanges(const std::vector<FileChange>& changes) {
	std::unique_lock<std::shared_mutex> lk(mu);

	FileChangeSummary result;
	auto newOverlays = overlays; // maps.Clone

	// Reduced collection of changes that occurred on a single file
	struct fileEvents {
		const FileChange* openChange = nullptr;
		const FileChange* closeChange = nullptr;
		bool watchChanged = false;
		std::vector<const FileChange*> changes;
		bool saved = false;
		bool created = false;
		bool deleted = false;
	};

	std::unordered_map<lsp::lsproto::DocumentUri, fileEvents*> fileEventMap;

	for (const auto& change : changes) {
		const auto& uri = change.URI;
		fileEvents* events;
		if (auto it = fileEventMap.find(uri); it != fileEventMap.end()) {
			events = it->second;
			if (events->openChange != nullptr) {
				TSC_UNREACHABLE("should see no changes after open");
			}
		} else {
			events = new fileEvents();
			fileEventMap[uri] = events;
		}

		if (!result.IncludesWatchChangeOutsideNodeModules &&
		    fileChangeKindIsWatchKind(change.Kind) &&
		    uri.find("/node_modules/") == std::string::npos) {
			result.IncludesWatchChangeOutsideNodeModules = true;
		}

		switch (change.Kind) {
		case FileChangeKindOpen:
			if (events->closeChange != nullptr) {
				events->closeChange = nullptr;
			}
			events->openChange = &change;
			events->watchChanged = false;
			events->changes.clear();
			events->saved = false;
			events->created = false;
			events->deleted = false;
			break;
		case FileChangeKindClose:
			events->closeChange = &change;
			events->changes.clear();
			events->saved = false;
			events->watchChanged = false;
			break;
		case FileChangeKindChange:
			if (events->closeChange != nullptr) {
				TSC_UNREACHABLE("should see no changes after close");
			}
			events->changes.push_back(&change);
			events->saved = false;
			events->watchChanged = false;
			break;
		case FileChangeKindSave:
			events->saved = true;
			break;
		case FileChangeKindWatchCreate:
			if (events->deleted) {
				// Delete followed by create becomes a change
				events->deleted = false;
				events->watchChanged = true;
			} else {
				events->created = true;
			}
			break;
		case FileChangeKindWatchChange:
			if (!events->created) {
				events->watchChanged = true;
				events->saved = false;
			}
			break;
		case FileChangeKindWatchDelete:
			events->watchChanged = false;
			events->saved = false;
			// Delete after create cancels out
			if (events->created) {
				events->created = false;
			} else {
				events->deleted = true;
			}
			break;
		}
	}

	// Process deduplicated events per file
	for (const auto& kv : fileEventMap) {
		const auto& uri = kv.first;
		auto* events = kv.second;
		auto path = lsp::lsproto::documentUriPath(
		    uri, host->UseCaseSensitiveFileNames());
		Overlay* o = nullptr;
		if (auto it = newOverlays.find(path); it != newOverlays.end()) {
			o = it->second;
		}

		if (events->openChange != nullptr) {
			if (!result.Opened.empty() || !result.Reopened.empty()) {
				TSC_UNREACHABLE(
				    "can only process one file open event at a time");
			}
			if (o != nullptr && o->Content() != events->openChange->Content) {
				result.Changed.Add(uri);
			} else if (o == nullptr) {
				result.Opened = uri;
			} else {
				result.Reopened = uri;
			}
			auto scriptKind = lsconv::LanguageKindToScriptKind(
			    events->openChange->LanguageKind);
			if (scriptKind == ScriptKind::Unknown) {
				scriptKind = getScriptKindFromFileName(
				    lsp::lsproto::documentUriFileName(uri));
			}
			newOverlays[path] = newOverlay(
			    lsp::lsproto::documentUriFileName(uri),
			    events->openChange->Content,
			    events->openChange->Version, scriptKind);
			continue;
		}

		if (events->closeChange != nullptr && o != nullptr) {
			result.Closed.Add(uri);
			newOverlays.erase(path);
			o = nullptr;
		}

		if (events->watchChanged) {
			if (o == nullptr) {
				result.Changed.Add(uri);
			} else if (!events->saved) {
				auto [matchesDiskText, _1] =
				    o->computeMatchesDiskText(host);
				if (matchesDiskText != o->MatchesDiskText()) {
					o = newOverlay(o->FileName(), o->Content(),
					               o->Version(), o->kind);
					o->matchesDiskText = matchesDiskText;
					newOverlays[path] = o;
				}
			}
		}

		if (!events->changes.empty() && o != nullptr) {
			result.Changed.Add(uri);
			for (auto* change : events->changes) {
				auto* converters = lsconv::NewConverters(
				    positionEncoding,
				    [&](const std::string&) {
					    return o->LSPLineMap();
				    });
				for (const auto& textChange : change->Changes) {
					if (auto partialChange = textChange.Partial;
					    partialChange != nullptr) {
						auto ranges =
						    lsconv::convertersFromLSPRange(
						        converters, o,
						        partialChange->Range,
						        spanmap::FeatureAll);
						debug::assert(
						    ranges.size() == 1,
						    "expected exactly one range for partial change");
						TextChange tc{ranges[0].Span,
						              partialChange->Text};
						auto newContent = tc.ApplyTo(o->content);
						o = newOverlay(o->fileName, newContent,
						               change->Version, o->kind);
					} else if (auto wholeChange =
					               textChange.WholeDocument;
					           wholeChange != nullptr) {
						o = newOverlay(o->fileName,
						               wholeChange->Text,
						               change->Version, o->kind);
					}
				}
				if (!change->Changes.empty()) {
					o->version = change->Version;
					o->hash = xxh3::hash128(o->content);
					o->matchesDiskText = false;
					newOverlays[path] = o;
				}
			}
		}

		if (events->saved) {
			if (o != nullptr) {
				o = newOverlay(o->FileName(), o->Content(), o->Version(),
				               o->kind);
				o->matchesDiskText = true;
				newOverlays[path] = o;
			} else if (!events->watchChanged) {
				// File was saved but never opened via didOpen; treat as
				// a disk change.
				result.Changed.Add(uri);
			}
		}

		if (events->created && o == nullptr) {
			result.Created.Add(uri);
		}

		if (events->deleted && o == nullptr) {
			result.Deleted.Add(uri);
		}
	}

	for (auto& kv : fileEventMap) {
		delete kv.second;
	}

	overlays = newOverlays;
	overlayDirectories = createOverlayDirectories(newOverlays);
	return {result, newOverlays};
}

} // namespace tsc::project
