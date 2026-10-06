// api/requestfilesystem/pathtree.cpp — pathtree.go: the request path tree
// (per-path entries with fallback markers), plus the requestEntry types.

#include "internal/api/requestfilesystem/requestfilesystem.h"

#include <algorithm>

namespace tsc::api::requestfilesystem {

// requestFile — vfs.FileInfo + vfs.DirEntry impls (pathtree.go:45-52).

std::string requestFile::Name() const {
	return std::string(tspath::getBaseFileName(fileName));
}
int64_t requestFile::Size() const {
	return static_cast<int64_t>(content.size());
}
vfs::FileMode requestFile::Mode() const { return vfs::FileMode{0444}; }
vfs::TimePoint requestFile::ModTime() const { return vfs::TimePoint{}; }
bool requestFile::IsDir() const { return false; }
std::any requestFile::Sys() const { return std::any{}; }
vfs::FileMode requestFile::Type() const {
	return Mode() & vfs::ModeType;
}
std::pair<std::shared_ptr<vfs::FileInfo>, vfs::Error>
requestFile::Info() const {
	return {std::shared_ptr<vfs::FileInfo>(
	            const_cast<requestFile*>(this), [](vfs::FileInfo*) {}),
	        vfs::Error{}};
}

std::string requestDirectory::Name() const {
	return std::string(tspath::getBaseFileName(directoryName));
}
int64_t requestDirectory::Size() const { return 0; }
vfs::FileMode requestDirectory::Mode() const {
	return vfs::FileMode{vfs::ModeDir.v | 0555};
}
vfs::TimePoint requestDirectory::ModTime() const { return vfs::TimePoint{}; }
bool requestDirectory::IsDir() const { return true; }
std::any requestDirectory::Sys() const { return std::any{}; }
vfs::FileMode requestDirectory::Type() const {
	return Mode() & vfs::ModeType;
}
std::pair<std::shared_ptr<vfs::FileInfo>, vfs::Error>
requestDirectory::Info() const {
	return {std::shared_ptr<vfs::FileInfo>(
	            const_cast<requestDirectory*>(this), [](vfs::FileInfo*) {}),
	        vfs::Error{}};
}

// requestPathNode — pathtree.go.

bool requestPathNode::replacesSubtree() const {
	return dynamic_cast<requestFile*>(entry) != nullptr ||
	       dynamic_cast<requestSymlink*>(entry) != nullptr;
}

std::vector<tspath::Path> requestPathAncestors(tspath::Path path) {
	std::vector<tspath::Path> paths;
	for (;;) {
		paths.push_back(path);
		tspath::Path parent =
		    tspath::Path(tspath::getDirectoryPath(std::string(path)));
		if (parent == path) {
			break;
		}
		path = parent;
	}
	std::reverse(paths.begin(), paths.end());
	return paths;
}

requestPathNode* requestPathNode::ensure(tspath::Path path) {
	requestPathNode* node = this;
	for (auto& ancestor : requestPathAncestors(path)) {
		auto it = node->children.find(ancestor);
		if (it == node->children.end() || it->second == nullptr) {
			auto* child = new requestPathNode();
			node->children[ancestor] = child;
			node = child;
		} else {
			node = it->second;
		}
	}
	return node;
}

std::pair<requestPathNode*, requestFallback>
requestPathNode::lookup(tspath::Path path) {
	requestFallback fallback = requestFallback::Inherit;
	requestPathNode* node = this;
	for (auto& ancestor : requestPathAncestors(path)) {
		if (node == nullptr) {
			break;
		}
		if (node->fallback != requestFallback::Inherit) {
			fallback = node->fallback;
		}
		auto it = node->children.find(ancestor);
		node = (it == node->children.end()) ? nullptr : it->second;
	}
	if (node != nullptr && node->fallback != requestFallback::Inherit) {
		fallback = node->fallback;
	}
	return {node, fallback};
}

void requestPathNode::walkSymlinks(
    const std::function<void(tspath::Path, requestSymlink*)>& visit) {
	if (!hasSymlinks) {
		return;
	}
	for (auto& [path, child] : children) {
		if (auto* symlink = dynamic_cast<requestSymlink*>(child->entry)) {
			visit(path, symlink);
		}
		child->walkSymlinks(visit);
	}
}

std::pair<vfs::Entries, bool> requestPathNode::entries() {
	auto* directory = dynamic_cast<requestDirectory*>(entry);
	if (directory == nullptr) {
		return {vfs::Entries{}, false};
	}
	if (directory->listing != nullptr) {
		return {cloneEntries(*directory->listing), true};
	}
	vfs::Entries entries;
	for (auto& [path, child] : children) {
		if (auto* f = dynamic_cast<requestFile*>(child->entry)) {
			entries.files.push_back(std::string(tspath::getBaseFileName(f->fileName)));
		} else if (auto* d =
		               dynamic_cast<requestDirectory*>(child->entry)) {
			entries.directories.push_back(
			    std::string(tspath::getBaseFileName(d->directoryName)));
		}
	}
	std::sort(entries.files.begin(), entries.files.end());
	std::sort(entries.directories.begin(), entries.directories.end());
	return {entries, true};
}

requestPathNode* composeRequestPaths(requestPathNode* base,
                                     requestPathNode* overlay,
                                     requestFallback fallback,
                                     bool caseSensitive) {
	if (overlay == nullptr) {
		return base;
	}
	if (overlay->fallback != requestFallback::Inherit) {
		fallback = overlay->fallback;
		base = nullptr;
	}
	if (overlay->replacesSubtree()) {
		base = nullptr;
	}
	auto* result = new requestPathNode();
	if (base != nullptr) {
		result->entry = base->entry;
		result->fallback = base->fallback;
		result->children = base->children;
		result->hasSymlinks = base->hasSymlinks;
	}
	// maps.Clone of children: our assignment above already copies the map;
	// shared child nodes are never mutated post-composition.
	if (overlay->fallback != requestFallback::Inherit ||
	    overlay->replacesSubtree()) {
		result->fallback = fallback;
	}
	auto* previousDirectory =
	    result->entry != nullptr ? dynamic_cast<requestDirectory*>(result->entry)
	                             : nullptr;
	auto* overlayDirectory =
	    overlay->entry != nullptr ? dynamic_cast<requestDirectory*>(overlay->entry)
	                              : nullptr;
	if (overlay->entry != nullptr) {
		result->entry = overlay->entry;
		if (overlayDirectory != nullptr && overlayDirectory->listing == nullptr &&
		    previousDirectory != nullptr) {
			auto* d = new requestDirectory();
			d->directoryName = overlayDirectory->directoryName;
			d->listing = previousDirectory->listing;
			result->entry = d;
		}
	}
	for (auto& [path, child] : overlay->children) {
		auto it = result->children.find(path);
		result->children[path] = composeRequestPaths(
		    it == result->children.end() ? nullptr : it->second, child,
		    fallback, caseSensitive);
	}
	auto* resultDirectory =
	    result->entry != nullptr ? dynamic_cast<requestDirectory*>(result->entry)
	                             : nullptr;
	if (resultDirectory != nullptr && resultDirectory->listing != nullptr &&
	    (overlayDirectory == nullptr || overlayDirectory->listing == nullptr)) {
		vfs::Entries entries = cloneEntries(*resultDirectory->listing);
		auto equal = [caseSensitive](const std::string& left,
		                             const std::string& right) {
			return tspath::getCanonicalFileName(left, caseSensitive) ==
			       tspath::getCanonicalFileName(right, caseSensitive);
		};
		for (auto& [path, child] : overlay->children) {
			std::string name =
			    std::string(tspath::getBaseFileName(std::string(path)));
			if (child->fallback == requestFallback::Missing ||
			    child->replacesSubtree()) {
				std::erase_if(entries.files, [&](const std::string& e) {
					return equal(e, name);
				});
				std::erase_if(entries.directories,
				              [&](const std::string& e) {
					              return equal(e, name);
				              });
				if (entries.symlinks.has_value()) {
					for (auto it = entries.symlinks->begin();
					     it != entries.symlinks->end();) {
						if (equal(*it, name)) {
							it = entries.symlinks->erase(it);
						} else {
							++it;
						}
					}
				}
			}
			if (auto* f = dynamic_cast<requestFile*>(child->entry)) {
				vfs::Entries o;
				o.files.push_back(std::string(tspath::getBaseFileName(f->fileName)));
				entries = mergeEntries(entries, o, equal);
			} else if (auto* d = dynamic_cast<requestDirectory*>(
			               child->entry)) {
				vfs::Entries o;
				o.directories.push_back(
				    std::string(tspath::getBaseFileName(d->directoryName)));
				entries = mergeEntries(entries, o, equal);
			}
		}
		auto* d = new requestDirectory();
		d->directoryName = resultDirectory->directoryName;
		auto* listing = new vfs::Entries(std::move(entries));
		d->listing = listing;
		result->entry = d;
	}
	result->hasSymlinks =
	    dynamic_cast<requestSymlink*>(result->entry) != nullptr;
	for (auto& [path, child] : result->children) {
		result->hasSymlinks = result->hasSymlinks || child->hasSymlinks;
	}
	return result;
}

std::pair<tspath::Path, requestSymlink*>
requestPathNode::firstSymlink(tspath::Path path) {
	requestPathNode* node = this;
	for (auto& ancestor : requestPathAncestors(path)) {
		if (node == nullptr) {
			break;
		}
		auto it = node->children.find(ancestor);
		node = (it == node->children.end()) ? nullptr : it->second;
		if (node != nullptr) {
			if (auto* symlink =
			        dynamic_cast<requestSymlink*>(node->entry)) {
				return {ancestor, symlink};
			}
		}
	}
	return {tspath::Path{}, nullptr};
}

bool requestPathNode::containsFileAncestor(tspath::Path path) {
	requestPathNode* node = this;
	for (auto& ancestor : requestPathAncestors(path)) {
		if (node == nullptr) {
			return false;
		}
		auto it = node->children.find(ancestor);
		node = (it == node->children.end()) ? nullptr : it->second;
		if (ancestor != path && node != nullptr) {
			if (dynamic_cast<requestFile*>(node->entry) != nullptr) {
				return true;
			}
		}
	}
	return false;
}

bool requestPathContains(tspath::Path parent, tspath::Path path) {
	return path == parent ||
	       std::string(path).starts_with(
	           tspath::ensureTrailingDirectorySeparator(std::string(parent)));
}

}  // namespace tsc::api::requestfilesystem
