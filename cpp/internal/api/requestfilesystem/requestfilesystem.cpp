// api/requestfilesystem/requestfilesystem.cpp — requestfilesystem.go.

#include "internal/api/requestfilesystem/requestfilesystem.h"

#include "internal/ls/lsconv/lsconv.h"
#include "internal/json/json.h"

#include <cctype>
#include <functional>

#include <algorithm>
#include <deque>
#include <set>

namespace tsc::api::requestfilesystem {

// NewForUpdate (requestfilesystem.go:90).
std::pair<std::shared_ptr<vfs::FS>, gostd::Error> NewForUpdate(
    RequestFileSystem* params, const std::shared_ptr<vfs::FS>& base,
    const std::string& currentDirectory,
    project::FileChangeSummary* fileChanges) {
	if (params == nullptr) {
		return {base, nullptr};
	}
	std::shared_ptr<vfs::FS> baseFileSystem = base;
	if (params->kind == KindFull) {
		if (requestFileSystem* requestBase = getRequestFileSystem(base)) {
			baseFileSystem = requestBase->base;
		}
	}
	auto [fileSystem, err] =
	    newRequestFileSystemWorker(params, baseFileSystem, currentDirectory);
	if (err != nullptr) {
		return {nullptr, err};
	}
	requestFileSystem* baseRequestFileSystem =
	    getRequestFileSystem(baseFileSystem);
	if (baseRequestFileSystem != nullptr) {
		fileSystem = fileSystem->applyTo(*baseRequestFileSystem);
	}
	if (params->kind == KindLayer) {
		addFileChanges(fileChanges, params, baseFileSystem,
		               fileSystem.get(), currentDirectory);
	}
	return {fileSystem, nullptr};
}

// HasFullFileSystem (requestfilesystem.go:112).
bool HasFullFileSystem(vfs::FS* fileSystem) {
	requestFileSystem* rfs = getRequestFileSystem(fileSystem);
	return rfs != nullptr && rfs->kind == KindFull;
}
bool HasFullFileSystem(const std::shared_ptr<vfs::FS>& fileSystem) {
	return HasFullFileSystem(fileSystem.get());
}

// newRequestFileSystemWorker (requestfilesystem.go:117).
std::pair<std::shared_ptr<requestFileSystem>, gostd::Error>
newRequestFileSystemWorker(RequestFileSystem* params,
                           const std::shared_ptr<vfs::FS>& base,
                           const std::string& currentDirectory) {
	if (params->kind != KindFull && params->kind != KindLayer) {
		return {nullptr,
		        gostd::errorf("unknown request filesystem kind %q",
		                      {params->kind})};
	}

	auto result = std::make_shared<requestFileSystem>();
	result->kind = params->kind;
	result->base = base;
	result->currentDirectory = currentDirectory;
	result->useCaseSensitiveNames = base->UseCaseSensitiveFileNames();
	result->paths = new requestPathNode();
	result->registerDirectory(currentDirectory);

	std::vector<std::string> fileNames;
	fileNames.reserve(params->Files.size());
	for (auto& [fileName, _] : params->Files) {
		fileNames.push_back(fileName);
	}
	std::sort(fileNames.begin(), fileNames.end());
	for (auto& fileName : fileNames) {
		const std::string& content = params->Files[fileName];
		std::string absoluteFileName = result->toAbsolutePath(fileName);
		tspath::Path path = result->toPath(absoluteFileName);
		requestPathNode* node = result->paths->ensure(path);
		if (auto* existing = dynamic_cast<requestFile*>(node->entry)) {
			return {nullptr,
			        gostd::errorf(
			            "duplicate request filesystem file path %q and %q",
			            {existing->fileName, absoluteFileName})};
		}
		auto* f = new requestFile();
		f->fileName = absoluteFileName;
		f->content = content;
		node->entry = f;
		result->registerDirectory(
		    tspath::getDirectoryPath(absoluteFileName));
	}

	std::set<tspath::Path> seenDirectories;
	std::vector<std::string> listedDirectories;
	if (params->Directories.has_value()) {
		for (auto& [directoryName, entries] : *params->Directories) {
			std::string absoluteDirectoryName =
			    result->toAbsolutePath(directoryName);
			tspath::Path path = result->toPath(absoluteDirectoryName);
			requestPathNode* node = result->paths->ensure(path);
			if (seenDirectories.count(path)) {
				return {nullptr,
				        gostd::errorf(
				            "duplicate request filesystem directory path %q",
				            {absoluteDirectoryName})};
			}
			seenDirectories.insert(path);
			if (dynamic_cast<requestFile*>(node->entry) == nullptr) {
				auto* d = new requestDirectory();
				d->directoryName = absoluteDirectoryName;
				auto* listing = new vfs::Entries();
				listing->files = entries.Files;
				listing->directories = entries.Directories;
				d->listing = listing;
				node->entry = d;
			}
			result->registerDirectory(
			    tspath::getDirectoryPath(absoluteDirectoryName));
			for (auto& child : entries.Directories) {
				listedDirectories.push_back(tspath::combinePaths(
				    absoluteDirectoryName, {child}));
			}
		}
	}
	if (params->Symlinks.has_value()) {
		for (auto& [linkName, _] : *params->Symlinks) {
			result->registerDirectory(tspath::getDirectoryPath(
			    result->toAbsolutePath(linkName)));
		}
		std::map<tspath::Path, std::string> seenSymlinks;
		for (auto& [linkName, symlink] : *params->Symlinks) {
			std::string absoluteLinkName = result->toAbsolutePath(linkName);
			tspath::Path path = result->toPath(absoluteLinkName);
			requestPathNode* node = result->paths->ensure(path);
			if (auto it = seenSymlinks.find(path);
			    it != seenSymlinks.end()) {
				return {nullptr,
				        gostd::errorf(
				            "duplicate request filesystem symlink path %q "
				            "and %q",
				            {it->second, absoluteLinkName})};
			}
			seenSymlinks[path] = absoluteLinkName;
			std::string targetDirectory =
			    tspath::getDirectoryPath(absoluteLinkName);
			std::string absoluteTarget = result->toAbsolutePathFrom(
			    symlink.Target, targetDirectory);
			if (node->entry == nullptr) {
				auto* l = new requestSymlink();
				l->linkName = absoluteLinkName;
				l->target = absoluteTarget;
				l->host = symlink.Host;
				node->entry = l;
			}
		}
	}
	for (auto& directoryName : listedDirectories) {
		result->registerDirectory(directoryName);
	}
	if (params->RemovedPaths.has_value()) {
		for (auto& path : *params->RemovedPaths) {
			result->paths->ensure(
			    result->toPath(result->toAbsolutePath(path)))
			    ->fallback = requestFallback::Missing;
		}
	}
	result->paths =
	    composeRequestPaths(nullptr, result->paths, requestFallback::Allowed,
	                        result->useCaseSensitiveNames);
	return {result, nullptr};
}

project::LayeredFileSystem*
requestFileSystem::WithBaseFileSystem(vfs::FS* base) {
	auto* clone = new requestFileSystem(*this);
	// non-owning alias — Go reference semantics (base outlives via caller)
	clone->base = std::shared_ptr<vfs::FS>(base, [](vfs::FS*) {});
	return clone;
}

std::unordered_map<tspath::Path, project::Overlay*>
requestFileSystem::Overlays() {
	std::unordered_map<tspath::Path, project::Overlay*> result;
	auto* layered = dynamic_cast<project::LayeredFileSystem*>(base.get());
	if (layered == nullptr) {
		return result;
	}
	for (auto& [path, overlay] : layered->Overlays()) {
		requestPathLookup lookup = lookupPath(overlay->FileName());
		if (lookup.fileSystem == nullptr ||
		    toPath(lookup.path) != path) {
			continue;
		}
		result[path] = overlay;
	}
	return result;
}

// applyTo (requestfilesystem.go:240).
std::shared_ptr<requestFileSystem>
requestFileSystem::applyTo(const requestFileSystem& base) {
	auto s = std::make_shared<requestFileSystem>(*this);
	s->paths = composeRequestPaths(base.paths, s->paths,
	                               requestFallback::Allowed,
	                               s->useCaseSensitiveNames);
	s->kind = base.kind;
	s->base = base.base;
	return s;
}

bool requestFileSystem::blocksFallback(const std::string& path) const {
	auto [node, fallback] = paths->lookup(toPath(path));
	return fallback == requestFallback::Missing;
}

std::string
requestFileSystem::toAbsolutePath(const std::string& path) const {
	return toAbsolutePathFrom(path, currentDirectory);
}

std::string requestFileSystem::toAbsolutePathFrom(
    const std::string& path, const std::string& currentDirectory) const {
	std::string absolutePath =
	    tspath::getNormalizedAbsolutePath(path, currentDirectory);
	if (tspath::isDiskPathRoot(absolutePath)) {
		return absolutePath;
	}
	return std::string(tspath::removeTrailingDirectorySeparator(absolutePath));
}

tspath::Path
requestFileSystem::toPath(const std::string& path) const {
	return tspath::toPath(path, currentDirectory, useCaseSensitiveNames);
}

void requestFileSystem::registerDirectory(std::string directoryName) {
	directoryName = toAbsolutePath(directoryName);
	for (;;) {
		requestPathNode* node = paths->ensure(toPath(directoryName));
		if (node->entry != nullptr) {
			return;
		}
		auto* d = new requestDirectory();
		d->directoryName = directoryName;
		node->entry = d;
		std::string parentName = tspath::getDirectoryPath(directoryName);
		if (parentName == directoryName) {
			return;
		}
		directoryName = parentName;
	}
}

resolvedRequestPath
requestFileSystem::resolvePath(std::string path) const {
	path = toAbsolutePath(path);
	resolvedRequestPath result{path, false, false, true};
	std::set<tspath::Path> seen;
	for (;;) {
		tspath::Path canonicalPath = toPath(result.path);
		if (paths->containsFileAncestor(canonicalPath)) {
			result.ok = false;
			return result;
		}
		auto [matchPath, match] = paths->firstSymlink(canonicalPath);
		if (matchPath.empty()) {
			result.host = isHostPath(result.path);
			return result;
		}
		if (seen.count(matchPath)) {
			result.ok = false;
			return result;
		}
		seen.insert(matchPath);
		result.followedSymlink = true;
		auto [suffix, ok] = tspath::trimFilePathPrefix(result.path,
		                                             match->linkName,
		                                             useCaseSensitiveNames);
		if (!ok) {
			result.ok = false;
			return result;
		}
		std::string suf = suffix;
		if (suf.starts_with("/")) {
			suf = suf.substr(1);
		}
		result.path = toAbsolutePath(
		    tspath::combinePaths(match->target, {suf}));
		if (match->host) {
			result.host = true;
			return result;
		}
	}
}

bool requestFileSystem::isHostPath(const std::string& path) const {
	tspath::Path canonicalPath = toPath(path);
	bool found = false;
	paths->walkSymlinks(
	    [&](tspath::Path, requestSymlink* symlink) {
		    if (symlink->host &&
		        requestPathContains(toPath(symlink->target),
		                            canonicalPath)) {
			    found = true;
		    }
	    });
	return found;
}

std::vector<std::string>
requestFileSystem::aliasesForPath(const std::string& path) const {
	std::vector<requestSymlink> symlinks;
	paths->walkSymlinks(
	    [&](tspath::Path, requestSymlink* symlink) {
		    symlinks.push_back(*symlink);
	    });

	std::set<tspath::Path> seen;
	seen.insert(toPath(path));
	std::deque<std::string> queue;
	queue.push_back(toAbsolutePath(path));
	std::vector<std::string> aliases;
	while (!queue.empty()) {
		std::string candidate = queue.front();
		queue.pop_front();
		for (auto& symlink : symlinks) {
			auto [suffix, ok] = tspath::trimFilePathPrefix(
			    candidate, symlink.target, useCaseSensitiveNames);
			if (!ok || (!suffix.empty() &&
			            !tspath::hasTrailingDirectorySeparator(
			                symlink.target) &&
			            !suffix.starts_with("/"))) {
				continue;
			}
			std::string suf = suffix;
			if (suf.starts_with("/")) {
				suf = suf.substr(1);
			}
			std::string alias = toAbsolutePath(
			    tspath::combinePaths(symlink.linkName, {suf}));
			tspath::Path aliasPath = toPath(alias);
			if (seen.count(aliasPath)) {
				continue;
			}
			resolvedRequestPath resolved = resolvePath(alias);
			if (!resolved.ok) {
				continue;
			}
			seen.insert(aliasPath);
			aliases.push_back(alias);
			queue.push_back(alias);
		}
	}
	return aliases;
}

std::pair<vfs::FileInfo*, requestFallback>
requestFileSystem::localPathInfo(const std::string& path) const {
	auto [node, fallback] = paths->lookup(toPath(path));
	if (node == nullptr) {
		return {nullptr, fallback};
	}
	auto* info = dynamic_cast<vfs::FileInfo*>(node->entry);
	return {info, fallback};
}

requestPathLookup
requestFileSystem::lookupPath(const std::string& path) const {
	std::string absolutePath = toAbsolutePath(path);
	auto [info, pathFallback] = localPathInfo(absolutePath);
	if (info != nullptr) {
		return requestPathLookup{absolutePath, info, nullptr, false, true};
	}
	if (pathFallback == requestFallback::Missing) {
		return requestPathLookup{};
	}
	resolvedRequestPath resolved = resolvePath(path);
	if (!resolved.ok) {
		return requestPathLookup{};
	}
	requestPathLookup result{resolved.path, nullptr, nullptr,
	                         resolved.followedSymlink, true};
	auto [resolvedInfo, resolvedFallback] = localPathInfo(resolved.path);
	if (!resolved.host && resolvedInfo != nullptr) {
		result.info = resolvedInfo;
	} else if (resolved.host || kind == KindLayer) {
		if (resolvedFallback == requestFallback::Missing) {
			return requestPathLookup{};
		}
		result.fileSystem = base.get();
		result.ok = result.fileSystem != nullptr;
	}
	return result;
}

std::tuple<vfs::FS*, std::string, bool>
requestFileSystem::mutationPath(const std::string& path) const {
	if (kind != KindLayer) {
		return {nullptr, "", false};
	}
	resolvedRequestPath resolved = resolvePath(path);
	if (!resolved.ok) {
		return {nullptr, "", false};
	}
	return {base.get(), resolved.path, base != nullptr};
}

vfs::Entries cloneEntries(const vfs::Entries& entries) {
	vfs::Entries result{entries.files, entries.directories, std::nullopt};
	if (entries.symlinks.has_value()) {
		result.symlinks = std::unordered_set<std::string>(
		    entries.symlinks->begin(), entries.symlinks->end());
	}
	return result;
}

// vfs::FS implementation.

bool requestFileSystem::UseCaseSensitiveFileNames() {
	return useCaseSensitiveNames;
}

project::FileHandle*
requestFileSystem::GetFile(const std::string& fileName) {
	return GetFileByPath(fileName, toPath(fileName));
}

project::FileHandle*
requestFileSystem::GetFileByPath(const std::string& fileName,
                                 const tspath::Path& /*path*/) {
	requestPathLookup lookup = lookupPath(fileName);
	if (!lookup.ok || (lookup.info != nullptr && lookup.info->IsDir())) {
		return nullptr;
	}
	if (lookup.fileSystem != nullptr) {
		if (auto* source =
		        dynamic_cast<project::FileHandleSource*>(
		            lookup.fileSystem)) {
			return source->GetFile(lookup.path);
		}
		if (auto [content, ok] = lookup.fileSystem->ReadFile(lookup.path);
		    ok) {
			return project::newCachedFileHandle(fileName, content);
		}
		return nullptr;
	}
	if (auto* file = dynamic_cast<requestFile*>(lookup.info)) {
		return project::newCachedFileHandle(fileName, file->content);
	}
	return nullptr;
}

std::pair<std::string, bool>
requestFileSystem::ReadFile(const std::string& fileName) {
	requestPathLookup lookup = lookupPath(fileName);
	if (!lookup.ok || (lookup.info != nullptr && lookup.info->IsDir())) {
		return {"", false};
	}
	if (lookup.fileSystem != nullptr) {
		return lookup.fileSystem->ReadFile(lookup.path);
	}
	if (auto* file = dynamic_cast<requestFile*>(lookup.info)) {
		return {file->content, true};
	}
	return {"", false};
}

bool requestFileSystem::FileExists(const std::string& fileName) {
	requestPathLookup lookup = lookupPath(fileName);
	if (!lookup.ok || (lookup.info != nullptr && lookup.info->IsDir())) {
		return false;
	}
	return lookup.info != nullptr ||
	       (lookup.fileSystem != nullptr &&
	        lookup.fileSystem->FileExists(lookup.path));
}

bool requestFileSystem::DirectoryExists(const std::string& directoryName) {
	requestPathLookup lookup = lookupPath(directoryName);
	if (!lookup.ok || (lookup.info != nullptr && !lookup.info->IsDir())) {
		return false;
	}
	return lookup.info != nullptr ||
	       (lookup.fileSystem != nullptr &&
	        lookup.fileSystem->DirectoryExists(lookup.path));
}

vfs::Entries
requestFileSystem::GetAccessibleEntries(const std::string& directoryName) {
	requestPathLookup lookup = lookupPath(directoryName);
	if (!lookup.ok || (lookup.info != nullptr && !lookup.info->IsDir())) {
		vfs::Entries e;
		e.symlinks = std::unordered_set<std::string>{};
		return e;
	}
	vfs::Entries result;
	if (lookup.fileSystem != nullptr) {
		result = removeEntries(
		    lookup.path, lookup.fileSystem->GetAccessibleEntries(lookup.path));
	} else {
		auto [localEntries, explicit_, _ok] = getLocalEntries(lookup.path);
		result = localEntries;
		if (kind == KindLayer && !explicit_ &&
		    !blocksFallback(directoryName) && !blocksFallback(lookup.path)) {
			result = removeEntries(
			    lookup.path,
			    baseFileSystem()->GetAccessibleEntries(lookup.path));
			result = mergeEntries(result, localEntries,
			                      [this](const std::string& a,
			                             const std::string& b) {
				                      return equalEntryNames(a, b);
			                      });
		}
		result = addSymlinkEntries(lookup.path, result);
	}
	result = filterLocalEntries(directoryName, result);
	return result;
}

vfs::Entries
requestFileSystem::filterLocalEntries(const std::string& directoryName,
                                      vfs::Entries entries) const {
	vfs::Entries result = cloneEntries(entries);
	auto filter = [&](const std::vector<std::string>& values) {
		std::vector<std::string> out;
		for (auto& name : values) {
			std::string fileName =
			    tspath::combinePaths(directoryName, {name});
			if (auto [info, _] = localPathInfo(fileName);
			    info != nullptr) {
				out.push_back(name);
				continue;
			}
			if (!blocksFallback(fileName)) {
				out.push_back(name);
			}
		}
		return out;
	};
	result.files = filter(result.files);
	result.directories = filter(result.directories);
	if (result.symlinks.has_value()) {
		for (auto it = result.symlinks->begin();
		     it != result.symlinks->end();) {
			if (filter({*it}).empty()) {
				it = result.symlinks->erase(it);
			} else {
				++it;
			}
		}
	}
	return result;
}

std::tuple<vfs::Entries, bool, bool>
requestFileSystem::getLocalEntries(const std::string& directoryName) const {
	auto [node, _] = paths->lookup(toPath(directoryName));
	auto [entries, ok] =
	    node != nullptr ? node->entries()
	                    : std::pair<vfs::Entries, bool>{vfs::Entries{}, false};
	bool explicit_ = false;
	if (node != nullptr) {
		if (auto* directory =
		        dynamic_cast<requestDirectory*>(node->entry)) {
			explicit_ = directory->listing != nullptr;
		}
	}
	return {entries, explicit_, ok};
}

vfs::Entries mergeEntries(
    vfs::Entries base, vfs::Entries overlay,
    const std::function<bool(const std::string&, const std::string&)>& equal) {
	vfs::Entries result = cloneEntries(base);
	if (!result.symlinks.has_value()) {
		result.symlinks = std::unordered_set<std::string>{};
	}
	auto deleteSymlink = [&](const std::string& name) {
		for (auto it = result.symlinks->begin();
		     it != result.symlinks->end();) {
			if (equal(*it, name)) {
				it = result.symlinks->erase(it);
			} else {
				++it;
			}
		}
	};
	auto addFile = [&](const std::string& name) {
		std::erase_if(result.directories,
		              [&](const std::string& v) { return equal(v, name); });
		if (std::none_of(result.files.begin(), result.files.end(),
		                 [&](const std::string& v) {
			                 return equal(v, name);
		                 })) {
			result.files.push_back(name);
		}
		deleteSymlink(name);
	};
	auto addDirectory = [&](const std::string& name) {
		std::erase_if(result.files,
		              [&](const std::string& v) { return equal(v, name); });
		if (std::none_of(result.directories.begin(),
		                 result.directories.end(),
		                 [&](const std::string& v) {
			                 return equal(v, name);
		                 })) {
			result.directories.push_back(name);
		}
		deleteSymlink(name);
	};
	for (auto& name : overlay.files) {
		addFile(name);
	}
	for (auto& name : overlay.directories) {
		addDirectory(name);
	}
	for (auto& name : overlay.symlinks.value_or(
	         std::unordered_set<std::string>{})) {
		result.symlinks->insert(name);
	}
	std::sort(result.files.begin(), result.files.end());
	std::sort(result.directories.begin(), result.directories.end());
	return result;
}

vfs::Entries
requestFileSystem::removeEntries(const std::string& directoryName,
                                 vfs::Entries entries) const {
	vfs::Entries result = cloneEntries(entries);
	auto filter = [&](const std::vector<std::string>& values) {
		std::vector<std::string> out;
		for (auto& name : values) {
			if (!blocksFallback(
			        tspath::combinePaths(directoryName, {name}))) {
				out.push_back(name);
			}
		}
		return out;
	};
	result.files = filter(result.files);
	result.directories = filter(result.directories);
	if (result.symlinks.has_value()) {
		for (auto it = result.symlinks->begin();
		     it != result.symlinks->end();) {
			if (blocksFallback(
			        tspath::combinePaths(directoryName, {*it}))) {
				it = result.symlinks->erase(it);
			} else {
				++it;
			}
		}
	}
	return result;
}

vfs::Entries
requestFileSystem::addSymlinkEntries(const std::string& directoryName,
                                     vfs::Entries entries) {
	vfs::Entries result = cloneEntries(entries);
	if (!result.symlinks.has_value()) {
		result.symlinks = std::unordered_set<std::string>{};
	}

	tspath::Path directoryPath = toPath(directoryName);
	std::vector<requestSymlink> links;
	auto [node, _fb] = paths->lookup(directoryPath);
	if (node != nullptr) {
		for (auto& [p, child] : node->children) {
			if (auto* symlink =
			        dynamic_cast<requestSymlink*>(child->entry)) {
				links.push_back(*symlink);
			}
		}
	}
	if (links.empty()) {
		return result;
	}
	for (auto& symlink : links) {
		std::string name = std::string(tspath::getBaseFileName(symlink.linkName));
		result.files = deleteEntryName(result.files, name);
		result.directories = deleteEntryName(result.directories, name);
		for (auto it = result.symlinks->begin();
		     it != result.symlinks->end();) {
			if (equalEntryNames(*it, name)) {
				it = result.symlinks->erase(it);
			} else {
				++it;
			}
		}
		if (DirectoryExists(symlink.linkName)) {
			result.directories.push_back(name);
			result.symlinks->insert(name);
		} else if (FileExists(symlink.linkName)) {
			result.files.push_back(name);
			result.symlinks->insert(name);
		}
	}
	std::sort(result.files.begin(), result.files.end());
	std::sort(result.directories.begin(), result.directories.end());
	return result;
}

std::vector<std::string>
requestFileSystem::deleteEntryName(std::vector<std::string> values,
                                   const std::string& value) const {
	std::erase_if(values, [&](const std::string& candidate) {
		return equalEntryNames(candidate, value);
	});
	return values;
}

bool requestFileSystem::equalEntryNames(const std::string& left,
                                        const std::string& right) const {
	return tspath::getCanonicalFileName(left, useCaseSensitiveNames) ==
	       tspath::getCanonicalFileName(right, useCaseSensitiveNames);
}

std::string requestFileSystem::Realpath(const std::string& path) {
	requestPathLookup lookup = lookupPath(path);
	if (!lookup.ok) {
		return path;
	}
	if (lookup.fileSystem != nullptr) {
		return lookup.fileSystem->Realpath(lookup.path);
	}
	if (lookup.info != nullptr || !lookup.followedSymlink) {
		return lookup.path;
	}
	return path;
}

vfs::Error requestFileSystem::WriteFile(const std::string& fileName,
                                        const std::string& data) {
	auto [host, path, ok] = mutationPath(fileName);
	if (!ok) {
		return vfs::ErrInvalid;
	}
	return host->WriteFile(path, data);
}

vfs::Error requestFileSystem::AppendFile(const std::string& fileName,
                                         const std::string& data) {
	auto [host, path, ok] = mutationPath(fileName);
	if (!ok) {
		return vfs::ErrInvalid;
	}
	return host->AppendFile(path, data);
}

vfs::Error requestFileSystem::Remove(const std::string& path) {
	auto [host, rpath, ok] = mutationPath(path);
	if (!ok) {
		return vfs::ErrInvalid;
	}
	return host->Remove(rpath);
}

vfs::Error requestFileSystem::Chtimes(const std::string& path,
                                      vfs::TimePoint aTime,
                                      vfs::TimePoint mTime) {
	auto [host, rpath, ok] = mutationPath(path);
	if (!ok) {
		return vfs::ErrInvalid;
	}
	return host->Chtimes(rpath, aTime, mTime);
}

std::shared_ptr<vfs::FileInfo>
requestFileSystem::Stat(const std::string& path) {
	requestPathLookup lookup = lookupPath(path);
	if (!lookup.ok) {
		return nullptr;
	}
	if (lookup.fileSystem != nullptr) {
		return std::shared_ptr<vfs::FileInfo>(
		    statFileSystem(lookup.fileSystem, lookup.path),
		    [](vfs::FileInfo*) {});
	}
	return std::shared_ptr<vfs::FileInfo>(lookup.info,
	                                    [](vfs::FileInfo*) {});
}

vfs::FileInfo* statFileSystem(vfs::FS* fileSystem, const std::string& path) {
	if (fileSystem == nullptr) {
		return nullptr;
	}
	if (auto info = fileSystem->Stat(path); info != nullptr) {
		// Return the FileInfo object itself — Go returns the interface
		// unchanged, so callers observe the host's FileInfo identity (a
		// sharedFileInfo wrapper would break it). Keep the shared_ptr
		// alive by leaking it; matches the GC-owned request entries.
		static std::mutex leakedMu;
		static std::vector<std::shared_ptr<vfs::FileInfo>> leaked;
		vfs::FileInfo* ptr = info.get();
		std::lock_guard<std::mutex> lock(leakedMu);
		leaked.push_back(std::move(info));
		return ptr;
	}
	if (fileSystem->DirectoryExists(path)) {
		auto* d = new requestDirectory();
		d->directoryName = path;
		return d;
	}
	if (fileSystem->FileExists(path)) {
		auto* f = new requestFile();
		f->fileName = path;
		return f;
	}
	return nullptr;
}

}  // namespace tsc::api::requestfilesystem

// === slice: api === — encoding/json Unmarshal bodies (requestfilesystem.go
// `json:` tags; used by api CreateSnapshotParams.FileSystem).

namespace tsc::api::requestfilesystem {

namespace {
// tag matching per encoding/json: exact or case-folded.
bool fieldIs(std::string_view name, std::string_view tag) {
	if (name == tag) return true;
	if (name.size() != tag.size()) return false;
	for (size_t i = 0; i < name.size(); i++) {
		if (std::tolower((unsigned char)name[i]) != std::tolower((unsigned char)tag[i])) {
			return false;
		}
	}
	return true;
}

std::string readFields(json::Decoder& dec, const std::function<std::string(std::string_view, json::Decoder&)>& readField) {
	if (dec.peekKind() != '{') {
		return "json: cannot unmarshal non-object";
	}
	if (auto [t, e] = dec.readToken(); !e.empty()) return e;
	while (dec.peekKind() != '}') {
		auto [key, e] = dec.readToken();
		if (!e.empty()) return e;
		if (auto e2 = readField(key.string(), dec); !e2.empty()) return e2;
	}
	if (auto [t, e] = dec.readToken(); !e.empty()) return e;
	return {};
}
}  // namespace

std::string RequestDirectoryEntries::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		if (fieldIs(n, "files")) return json::unmarshalDecode(d, &Files);
		if (fieldIs(n, "directories")) return json::unmarshalDecode(d, &Directories);
		return d.skipValue();
	});
}

std::string RequestSymlink::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		if (fieldIs(n, "target")) return json::unmarshalDecode(d, &Target);
		if (fieldIs(n, "host")) return json::unmarshalDecode(d, &Host);
		return d.skipValue();
	});
}

std::string RequestFileSystem::unmarshalJSONFrom(json::Decoder& dec) {
	return readFields(dec, [this](std::string_view n, json::Decoder& d) -> std::string {
		if (fieldIs(n, "kind")) return json::unmarshalDecode(d, &kind);
		if (fieldIs(n, "files")) return json::unmarshalDecode(d, &Files);
		if (fieldIs(n, "directories")) return json::unmarshalDecode(d, &Directories);
		if (fieldIs(n, "symlinks")) return json::unmarshalDecode(d, &Symlinks);
		if (fieldIs(n, "removedPaths")) return json::unmarshalDecode(d, &RemovedPaths);
		return d.skipValue();
	});
}

std::string RequestDirectoryEntries::marshalJSONTo(json::Encoder& enc) const {
	if (auto e = enc.writeToken(json::BeginObject); !e.empty()) return e;
	if (auto e = enc.writeValue(json::marshalString("files")); !e.empty()) return e;
	if (auto e = json::marshalEncode(enc, Files); !e.empty()) return e;
	if (auto e = enc.writeValue(json::marshalString("directories")); !e.empty()) return e;
	if (auto e = json::marshalEncode(enc, Directories); !e.empty()) return e;
	return enc.writeToken(json::EndObject);
}

std::string RequestSymlink::marshalJSONTo(json::Encoder& enc) const {
	if (auto e = enc.writeToken(json::BeginObject); !e.empty()) return e;
	if (auto e = enc.writeValue(json::marshalString("target")); !e.empty()) return e;
	if (auto e = json::marshalEncode(enc, Target); !e.empty()) return e;
	if (Host) {
		if (auto e = enc.writeValue(json::marshalString("host")); !e.empty()) return e;
		if (auto e = json::marshalEncode(enc, Host); !e.empty()) return e;
	}
	return enc.writeToken(json::EndObject);
}

std::string RequestFileSystem::marshalJSONTo(json::Encoder& enc) const {
	if (auto e = enc.writeToken(json::BeginObject); !e.empty()) return e;
	if (auto e = enc.writeValue(json::marshalString("kind")); !e.empty()) return e;
	if (auto e = json::marshalEncode(enc, kind); !e.empty()) return e;
	if (auto e = enc.writeValue(json::marshalString("files")); !e.empty()) return e;
	if (auto e = json::marshalEncode(enc, Files); !e.empty()) return e;
	if (Directories.has_value() && !Directories->empty()) {
		if (auto e = enc.writeValue(json::marshalString("directories")); !e.empty()) return e;
		if (auto e = json::marshalEncode(enc, *Directories); !e.empty()) return e;
	}
	if (Symlinks.has_value() && !Symlinks->empty()) {
		if (auto e = enc.writeValue(json::marshalString("symlinks")); !e.empty()) return e;
		if (auto e = json::marshalEncode(enc, *Symlinks); !e.empty()) return e;
	}
	if (RemovedPaths.has_value() && !RemovedPaths->empty()) {
		if (auto e = enc.writeValue(json::marshalString("removedPaths")); !e.empty()) return e;
		if (auto e = json::marshalEncode(enc, *RemovedPaths); !e.empty()) return e;
	}
	return enc.writeToken(json::EndObject);
}

}  // namespace tsc::api::requestfilesystem
