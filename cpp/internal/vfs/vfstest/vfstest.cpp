// vfstest.cpp — port of tsc/internal/vfs/vfstest/vfstest.go, plus the
// testing/fstest/mapfs.go pieces it relies on (fstest::MapFS.Open,
// resolveSymlinks, mapFileInfo, openMapFile, mapDir).
#include "internal/vfs/vfstest/vfstest.h"

#include "internal/ast/ast.h" // TSC_UNREACHABLE
#include "internal/tspath/tspath.h"

#include <algorithm>

namespace tsc::vfs::vfstest {

namespace {

// mapFileInfo implements FileInfo and DirEntry for a given map file —
// fstest/mapfs.go.
struct mapFileInfo : FileInfo,
                     DirEntry,
                     std::enable_shared_from_this<mapFileInfo> {
	std::string name;
	std::shared_ptr<fstest::MapFile> f;

	std::string Name() const override { return vfs::pathBase(name); }
	int64_t Size() const override {
		return static_cast<int64_t>(f->Data.size());
	}
	FileMode Mode() const override { return f->Mode; }
	FileMode Type() const override { return f->Mode.Type(); }
	TimePoint ModTime() const override { return f->ModTime; }
	bool IsDir() const override { return f->Mode.IsDir(); }
	std::any Sys() const override { return f->Sys; }
	std::pair<std::shared_ptr<FileInfo>, Error> Info() const override {
		return {std::const_pointer_cast<mapFileInfo>(shared_from_this()),
		        Error{}};
	}
};

// openMapFile — a regular (non-directory) File open for reading.
struct openMapFile final : File {
	std::string path;
	std::shared_ptr<mapFileInfo> info;
	int64_t offset = 0;

	std::pair<std::shared_ptr<FileInfo>, Error> Stat() override {
		return {info, Error{}};
	}
	Error Close() override { return Error{}; }
	std::pair<int, Error> Read(std::span<char> b) override {
		if (offset >= static_cast<int64_t>(info->f->Data.size())) {
			return {0, vfs::ErrEOF};
		}
		if (offset < 0) {
			return {0, vfs::makePathError("read", path,
			                              vfs::ErrInvalid)};
		}
		size_t n = std::min(
		    b.size(), info->f->Data.size() - static_cast<size_t>(offset));
		std::copy_n(info->f->Data.data() + offset, n, b.data());
		offset += static_cast<int64_t>(n);
		return {static_cast<int>(n), Error{}};
	}
};

// mapDir — a directory File (also ReadDirFile) open for reading.
struct mapDir final : ReadDirFile {
	std::string path;
	std::shared_ptr<mapFileInfo> info;
	std::vector<std::shared_ptr<mapFileInfo>> entry;
	size_t offset = 0;

	std::pair<std::shared_ptr<FileInfo>, Error> Stat() override {
		return {info, Error{}};
	}
	Error Close() override { return Error{}; }
	std::pair<int, Error> Read(std::span<char>) override {
		return {0, vfs::makePathError("read", path, vfs::ErrInvalid)};
	}
	std::pair<std::vector<std::shared_ptr<DirEntry>>, Error>
	ReadDir(int count) override {
		size_t n = entry.size() - offset;
		if (n == 0 && count > 0) {
			return {{}, vfs::ErrEOF};
		}
		if (count > 0 && n > static_cast<size_t>(count)) {
			n = static_cast<size_t>(count);
		}
		std::vector<std::shared_ptr<DirEntry>> list;
		list.reserve(n);
		for (size_t i = 0; i < n; i++) {
			list.push_back(entry[offset + i]);
		}
		offset += n;
		return {list, Error{}};
	}
};

} // namespace

// fstest::MapFS::resolveSymlinks — mapfs.go.
std::pair<std::string, bool>
fstest::MapFS::resolveSymlinks(const std::string& name) const {
	// Fast path: if a symlink is in the map, resolve it.
	if (auto it = files.find(name);
	    it != files.end() &&
	    it->second->Mode.Type() == vfs::ModeSymlink) {
		std::string target = it->second->Data;
		if (vfs::pathIsAbs(target)) {
			return {"", false};
		}
		return resolveSymlinks(
		    vfs::pathJoin({vfs::pathDir(name), target}));
	}

	// Check if each parent directory (starting at root) is a symlink.
	size_t i = 0;
	while (i < name.size()) {
		auto j = name.find('/', i); // name[i:] IndexByte "/"
		std::string dir;
		if (j == std::string::npos) {
			dir = name;
			i = name.size();
		} else {
			dir = name.substr(0, j); // name[:i+j]
			i = j;
		}
		if (auto it = files.find(dir);
		    it != files.end() &&
		    it->second->Mode.Type() == vfs::ModeSymlink) {
			std::string target = it->second->Data;
			if (vfs::pathIsAbs(target)) {
				return {"", false};
			}
			return resolveSymlinks(
			    vfs::pathJoin({vfs::pathDir(dir), target}) +
			    name.substr(i));
		}
		i += 1; // i += len("/")
	}
	return {name, vfs::validPath(name)};
}

// fstest::MapFS::Open — mapfs.go.
std::pair<std::shared_ptr<File>, Error>
fstest::MapFS::Open(const std::string& name) const {
	if (!vfs::validPath(name)) {
		return {nullptr,
		        vfs::makePathError("open", name, vfs::ErrNotExist)};
	}
	auto [realName, ok] = resolveSymlinks(name);
	if (!ok) {
		return {nullptr,
		        vfs::makePathError("open", name, vfs::ErrNotExist)};
	}

	std::shared_ptr<MapFile> file;
	if (auto it = files.find(realName); it != files.end()) {
		file = it->second;
	}
	if (file && !(file->Mode.v & FileMode::kDir)) {
		// Ordinary file.
		auto info = std::make_shared<mapFileInfo>();
		info->name = vfs::pathBase(name);
		info->f = file;
		auto of = std::make_shared<openMapFile>();
		of->path = name;
		of->info = info;
		return {of, Error{}};
	}

	// Directory, possibly synthesized. Note that file can be null here:
	// the map need not contain explicit parent directories for all its
	// files. Either way, construct the list of children.
	std::vector<std::shared_ptr<mapFileInfo>> list;
	std::unordered_map<std::string, bool> need;
	if (realName == ".") {
		for (auto& [fname, f] : files) {
			auto i = fname.find('/');
			if (i == std::string::npos) {
				if (fname != ".") {
					auto mi = std::make_shared<mapFileInfo>();
					mi->name = fname;
					mi->f = f;
					list.push_back(mi);
				}
			} else {
				need[fname.substr(0, i)] = true;
			}
		}
	} else {
		std::string prefix = realName + "/";
		for (auto& [fname, f] : files) {
			if (fname.starts_with(prefix)) {
				auto felem = fname.substr(prefix.size());
				auto i = felem.find('/');
				if (i == std::string::npos) {
					auto mi = std::make_shared<mapFileInfo>();
					mi->name = felem;
					mi->f = f;
					list.push_back(mi);
				} else {
					need[fname.substr(prefix.size(), i)] = true;
				}
			}
		}
		// If the directory name is not in the map, and there are no
		// children of the name in the map, then the directory is
		// treated as not existing.
		if (!file && list.empty() && need.empty()) {
			return {nullptr, vfs::makePathError("open", name,
			                                  vfs::ErrNotExist)};
		}
	}
	for (auto& fi : list) {
		need.erase(fi->name);
	}
	for (auto& [n, _] : need) {
		auto mi = std::make_shared<mapFileInfo>();
		mi->name = n;
		mi->f = std::make_shared<MapFile>();
		mi->f->Mode = vfs::ModeDir | FileMode{0555};
		list.push_back(mi);
	}
	std::sort(list.begin(), list.end(),
	          [](const auto& a, const auto& b) {
		          return a->name < b->name;
	          });

	if (!file) {
		file = std::make_shared<MapFile>();
		file->Mode = vfs::ModeDir | FileMode{0555};
	}
	std::string elem;
	if (name == ".") {
		elem = ".";
	} else {
		auto li = name.find_last_of('/');
		elem = name.substr(li == std::string::npos ? 0 : li + 1);
	}
	auto d = std::make_shared<mapDir>();
	d->path = name;
	d->info = std::make_shared<mapFileInfo>();
	d->info->name = elem;
	d->info->f = file;
	d->entry = std::move(list);
	return {d, Error{}};
}

// --- vfstest.go ----------------------------------------------------------

TimePoint clockImpl::Now() {
	return std::chrono::system_clock::now();
}

Duration clockImpl::SinceStart() {
	return std::chrono::duration_cast<Duration>(
	    std::chrono::system_clock::now() - start);
}

canonicalPath MapFS::getCanonicalPath(std::string_view p) const {
	return tspath::getCanonicalFileName(p, useCaseSensitiveFileNames);
}

std::pair<std::shared_ptr<File>, Error>
MapFS::open(const canonicalPath& p) const {
	return m.Open(p);
}

Error MapFS::remove(const std::string& path) {
	canonicalPath canonical = getCanonicalPath(path);
	std::string canonicalString = canonical;
	auto it = m.files.find(canonicalString);
	if (it == m.files.end()) {
		// file does not exist
		return Error{};
	}
	auto fileInfo = it->second;
	m.files.erase(canonicalString);
	symlinks.erase(canonical);

	if (fileInfo->Mode.IsDir()) {
		canonicalString += "/";
		std::vector<std::string> toDelete;
		for (auto& [p, f] : m.files) {
			if (p.starts_with(canonicalString)) {
				toDelete.push_back(p);
			}
		}
		for (auto& p : toDelete) {
			m.files.erase(p);
			symlinks.erase(canonicalPath(p));
		}
	}
	return Error{};
}

std::shared_ptr<fstest::MapFile> Symlink(std::string_view target) {
	auto f = std::make_shared<fstest::MapFile>();
	f->Data = std::string{target};
	f->Mode = vfs::ModeSymlink;
	return f;
}

BrokenSymlinkErrorImpl::BrokenSymlinkErrorImpl(canonicalPath from,
                                             canonicalPath to)
    : Error::Impl{"broken symlink \"" + from + "\" -> \"" + to + "\""},
      from(std::move(from)), to(std::move(to)) {}

bool isBrokenSymlinkError(const Error& err) {
	return err.as<BrokenSymlinkErrorImpl>() != nullptr;
}

std::tuple<std::shared_ptr<fstest::MapFile>, canonicalPath, Error>
MapFS::getFollowingSymlinks(const canonicalPath& p) const {
	return getFollowingSymlinksWorker(p, "", "");
}

std::tuple<std::shared_ptr<fstest::MapFile>, canonicalPath, Error>
MapFS::getFollowingSymlinksWorker(const canonicalPath& p,
                                  const canonicalPath& symlinkFrom,
                                  const canonicalPath& symlinkTo) const {
	if (auto it = m.files.find(p);
	    it != m.files.end() &&
	    !(it->second->Mode.v & FileMode::kSymlink)) {
		return {it->second, p, Error{}};
	}

	if (auto it = symlinks.find(p); it != symlinks.end()) {
		return getFollowingSymlinksWorker(it->second, p, it->second);
	}

	// This could be a path underneath a symlinked directory.
	for (auto& [other, target] : symlinks) {
		if (other.size() < p.size() &&
		    other == p.substr(0, other.size()) &&
		    p[other.size()] == '/') {
			return getFollowingSymlinksWorker(
			    target + p.substr(other.size()), other, target);
		}
	}

	Error err = vfs::ErrNotExist;
	if (!symlinkFrom.empty()) {
		err = Error{std::make_shared<BrokenSymlinkErrorImpl>(
		    symlinkFrom, symlinkTo)};
	}
	return {nullptr, p, err};
}

void MapFS::set(const canonicalPath& p,
                const std::shared_ptr<fstest::MapFile>& file) {
	m.files[p] = file;
}

void MapFS::setEntry(const std::string& realpath,
                     const canonicalPath& canonical,
                     fstest::MapFile file) {
	if (realpath.empty() || canonical.empty()) {
		TSC_UNREACHABLE("empty path");
	}

	auto s = std::make_shared<sys>();
	s->original = file.Sys;
	s->realpath = realpath;
	file.Sys = s;
	set(canonical, std::make_shared<fstest::MapFile>(std::move(file)));

	auto& stored = m.files[canonical];
	if (stored->Mode.v & FileMode::kSymlink) {
		symlinks[canonical] = getCanonicalPath(stored->Data);
	}
}

std::pair<std::string, std::string> splitPath(const std::string& s,
                                            int offset) {
	auto idx = s.find('/', static_cast<size_t>(offset));
	if (idx == std::string::npos) {
		return {s, ""};
	}
	return {s.substr(0, idx), s.substr(idx + 1)};
}

std::string dirName(std::string_view p) {
	auto [dir, _file] = vfs::pathSplit(p);
	std::string d = dir;
	if (d.ends_with("/")) {
		d.pop_back();
	}
	return d;
}

std::string baseName(std::string_view p) {
	auto [_dir, file] = vfs::pathSplit(p);
	return file;
}

Error MapFS::mkdirAll(const std::string& p0, FileMode perm) {
	std::string p = p0;
	if (p.empty()) {
		TSC_UNREACHABLE("empty path");
	}

	// Fast path; already exists.
	{
		auto [other, _path, err] =
		    getFollowingSymlinks(getCanonicalPath(p));
		if (!err) {
			if (!other->Mode.IsDir()) {
				return Error::newError("mkdir \"" + p +
				                       "\": path exists but is not a "
				                       "directory");
			}
			return Error{};
		}
	}

	std::vector<std::string> toCreate;
	int offset = 0;
	while (true) {
		auto [dir, rest] = splitPath(p, offset);
		canonicalPath canonical = getCanonicalPath(dir);
		auto [other, otherPath, err] = getFollowingSymlinks(canonical);
		if (err) {
			if (!err.is(vfs::ErrNotExist)) {
				return err;
			}
			toCreate.push_back(dir);
		} else {
			if (!other->Mode.IsDir()) {
				return Error::newError("mkdir \"" + otherPath +
				                       "\": path exists but is not a "
				                       "directory");
			}
			if (canonical != otherPath) {
				// We have a symlinked parent, reset and start
				// again.
				auto s = std::any_cast<std::shared_ptr<sys>>(
				    other->Sys);
				p = s->realpath + "/" + rest;
				toCreate.clear();
				offset = 0;
				continue;
			}
		}
		if (rest.empty()) {
			break;
		}
		offset = static_cast<int>(dir.size()) + 1;
	}

	for (auto& dir : toCreate) {
		fstest::MapFile f;
		f.Mode = vfs::ModeDir | FileMode{perm.v & ~kUmask};
		f.ModTime = clock->Now();
		setEntry(dir, getCanonicalPath(dir), std::move(f));
	}

	return Error{};
}

namespace {

// fileInfo wraps an inner FileInfo, overriding Name/Sys — vfstest.go.
struct testFileInfo final : FileInfo {
	std::shared_ptr<FileInfo> inner;
	std::any sys;
	std::string realpath;

	std::string Name() const override { return baseName(realpath); }
	int64_t Size() const override { return inner->Size(); }
	FileMode Mode() const override { return inner->Mode(); }
	TimePoint ModTime() const override { return inner->ModTime(); }
	bool IsDir() const override { return inner->IsDir(); }
	std::any Sys() const override { return sys; }
};

// convertInfo — vfstest.go.
std::pair<std::shared_ptr<testFileInfo>, bool>
convertInfo(const std::shared_ptr<FileInfo>& info) {
	auto sysAny = info->Sys();
	auto spp = std::any_cast<std::shared_ptr<sys>>(&sysAny);
	if (!spp || !*spp) {
		return {nullptr, false};
	}
	auto sp = *spp;
	auto fi = std::make_shared<testFileInfo>();
	fi->inner = info;
	fi->sys = sp->original;
	fi->realpath = sp->realpath;
	return {fi, true};
}

// file — vfstest.go: File wrapper whose Stat returns the wrapped info.
struct testFile final : File {
	std::shared_ptr<File> inner;
	std::shared_ptr<FileInfo> fileInfo;

	std::pair<std::shared_ptr<FileInfo>, Error> Stat() override {
		return {fileInfo, Error{}};
	}
	std::pair<int, Error> Read(std::span<char> b) override {
		return inner->Read(b);
	}
	Error Close() override { return inner->Close(); }
};

// readDirFile — vfstest.go: ReadDirFile wrapper; ReadDir converts each
// entry's info.
struct testReadDirFile final : ReadDirFile {
	std::shared_ptr<ReadDirFile> inner;
	std::shared_ptr<FileInfo> fileInfo;

	std::pair<std::shared_ptr<FileInfo>, Error> Stat() override {
		return {fileInfo, Error{}};
	}
	std::pair<int, Error> Read(std::span<char> b) override {
		return inner->Read(b);
	}
	Error Close() override { return inner->Close(); }
	std::pair<std::vector<std::shared_ptr<DirEntry>>, Error>
	ReadDir(int n) override {
		auto [list, err] = inner->ReadDir(n);
		if (err) {
			return {{}, err};
		}
		std::vector<std::shared_ptr<DirEntry>> entries(list.size());
		for (size_t i = 0; i < list.size(); i++) {
			auto [info, ierr] = list[i]->Info();
			if (ierr) {
				TSC_UNREACHABLE(ierr.str().c_str());
			}
			auto [newInfo, ok] = convertInfo(info);
			if (!ok) {
				TSC_UNREACHABLE(
				    ("unexpected synthesized dir: \"" +
				     info->Name() + "\"")
				        .c_str());
			}
			entries[i] = vfs::fileInfoToDirEntry(newInfo);
		}
		return {entries, Error{}};
	}
};

} // namespace

// MapFS::Open — vfstest.go.
std::pair<std::shared_ptr<File>, Error>
MapFS::Open(const std::string& name) {
	std::shared_lock lock(mu);

	auto [_f, cp, _e] = getFollowingSymlinks(getCanonicalPath(name));
	auto [f, err] = open(cp);
	if (err) {
		return {nullptr, err};
	}

	auto [info, serr] = f->Stat();
	if (serr) {
		TSC_UNREACHABLE(serr.str().c_str());
	}

	auto [newInfo, ok] = convertInfo(info);
	if (!ok) {
		// This is a synthesized dir.
		if (name != ".") {
			TSC_UNREACHABLE(("unexpected synthesized dir: \"" + name +
			                 "\"")
			                    .c_str());
		}
		auto fi = std::make_shared<testFileInfo>();
		fi->inner = info;
		fi->sys = info->Sys();
		fi->realpath = ".";
		auto rdf = std::make_shared<testReadDirFile>();
		rdf->inner = std::dynamic_pointer_cast<ReadDirFile>(f);
		rdf->fileInfo = fi;
		return {rdf, Error{}};
	}

	if (auto rdf0 = std::dynamic_pointer_cast<ReadDirFile>(f)) {
		auto rdf = std::make_shared<testReadDirFile>();
		rdf->inner = rdf0;
		rdf->fileInfo = newInfo;
		return {rdf, Error{}};
	}

	auto tf = std::make_shared<testFile>();
	tf->inner = f;
	tf->fileInfo = newInfo;
	return {tf, Error{}};
}

// MapFS::Realpath — vfstest.go.
std::pair<std::string, Error>
MapFS::Realpath(const std::string& name) {
	std::shared_lock lock(mu);

	auto [file, _p, err] =
	    getFollowingSymlinks(getCanonicalPath(name));
	if (err) {
		return {"", err};
	}
	return {std::any_cast<std::shared_ptr<sys>>(file->Sys)->realpath,
	        Error{}};
}

Error MapFS::MkdirAll(const std::string& path, FileMode perm) {
	std::unique_lock lock(mu);
	return mkdirAll(path, perm);
}

void MapFS::AddSymlink(const std::string& path,
                       const std::string& target) {
	std::unique_lock lock(mu);
	canonicalPath canonical = getCanonicalPath(path);
	fstest::MapFile f;
	f.Data = target;
	f.Mode = vfs::ModeSymlink;
	setEntry(path, canonical, std::move(f));
}

Error MapFS::WriteFile(const std::string& path, const std::string& data,
                       FileMode perm) {
	std::unique_lock lock(mu);

	if (auto parent = dirName(path); !parent.empty()) {
		canonicalPath canonical = getCanonicalPath(parent);
		auto [parentFile, _p, err] = getFollowingSymlinks(canonical);
		if (err) {
			return Error::wrap(
			    "write \"" + path + "\": " + err.str(), err);
		}
		if (!parentFile->Mode.IsDir()) {
			return Error::newError("write \"" + path +
			                       "\": parent path exists but is not "
			                       "a directory");
		}
	}

	auto [file, cp, err] =
	    getFollowingSymlinks(getCanonicalPath(path));
	if (err) {
		if (!err.is(vfs::ErrNotExist) && !isBrokenSymlinkError(err)) {
			// No other errors are possible.
			TSC_UNREACHABLE(err.str().c_str());
		}
	} else {
		if (!file->Mode.IsRegular()) {
			return Error::newError("write \"" + path +
			                       "\": path exists but is not a "
			                       "regular file");
		}
	}

	fstest::MapFile f;
	f.Data = data;
	f.ModTime = clock->Now();
	f.Mode = FileMode{perm.v & ~kUmask};
	setEntry(path, cp, std::move(f));
	return Error{};
}

Error MapFS::AppendFile(const std::string& path, const std::string& data,
                        FileMode perm) {
	std::unique_lock lock(mu);

	if (auto parent = dirName(path); !parent.empty()) {
		canonicalPath canonical = getCanonicalPath(parent);
		auto [parentFile, _p, err] = getFollowingSymlinks(canonical);
		if (err) {
			return Error::wrap(
			    "append \"" + path + "\": " + err.str(), err);
		}
		if (!parentFile->Mode.IsDir()) {
			return Error::newError("append \"" + path +
			                       "\": parent path exists but is not "
			                       "a directory");
		}
	}

	std::string existing;
	FileMode existingMode{};
	auto [file, cp, err] =
	    getFollowingSymlinks(getCanonicalPath(path));
	if (err) {
		if (!err.is(vfs::ErrNotExist) && !isBrokenSymlinkError(err)) {
			// No other errors are possible.
			TSC_UNREACHABLE(err.str().c_str());
		}
	} else {
		if (!file->Mode.IsRegular()) {
			return Error::newError("append \"" + path +
			                       "\": path exists but is not a "
			                       "regular file");
		}
		existing = file->Data;
		existingMode = file->Mode;
	}

	std::string combined = existing + data;
	FileMode mode = existingMode;
	if (mode.v == 0) {
		mode = FileMode{perm.v & ~kUmask};
	}

	fstest::MapFile f;
	f.Data = std::move(combined);
	f.ModTime = clock->Now();
	f.Mode = mode;
	setEntry(path, cp, std::move(f));
	return Error{};
}

Error MapFS::Remove(const std::string& path) {
	std::unique_lock lock(mu);
	return remove(path);
}

Error MapFS::Chtimes(const std::string& path, TimePoint aTime,
                     TimePoint mTime) {
	std::unique_lock lock(mu);
	canonicalPath canonical = getCanonicalPath(path);
	auto it = m.files.find(canonical);
	if (it == m.files.end()) {
		// file does not exist
		return vfs::ErrNotExist;
	}
	it->second->ModTime = mTime;
	return Error{};
}

std::pair<std::string, bool>
MapFS::GetTargetOfSymlink(const std::string& path) {
	auto p = path.starts_with('/') ? path.substr(1) : path;
	std::shared_lock lock(mu);
	canonicalPath canonical = getCanonicalPath(p);
	if (auto it = m.files.find(canonical); it != m.files.end()) {
		if (it->second->Mode.v & FileMode::kSymlink) {
			return {"/" + it->second->Data, true};
		}
	}
	return {"", false};
}

TimePoint MapFS::GetModTime(const std::string& path) {
	auto p = path.starts_with('/') ? path.substr(1) : path;
	std::shared_lock lock(mu);
	canonicalPath canonical = getCanonicalPath(p);
	if (auto it = m.files.find(canonical); it != m.files.end()) {
		return it->second->ModTime;
	}
	return TimePoint{};
}

std::vector<std::pair<std::string, std::shared_ptr<fstest::MapFile>>>
MapFS::Entries() {
	std::shared_lock lock(mu);
	std::vector<std::string> inputKeys;
	inputKeys.reserve(m.files.size());
	for (auto& [p, f] : m.files) {
		inputKeys.push_back(p);
	}
	std::sort(inputKeys.begin(), inputKeys.end(),
	          [](const std::string& a, const std::string& b) {
		          return comparePathsByParts(a, b) < 0;
	          });

	std::vector<std::pair<std::string, std::shared_ptr<fstest::MapFile>>>
	    out;
	for (auto& p : inputKeys) {
		auto file = m.files[p];
		std::string path = std::any_cast<std::shared_ptr<sys>>(
		                       file->Sys)
		                       ->realpath;
		if (!tspath::pathIsAbsolute(path)) {
			path = "/" + path;
		}
		out.emplace_back(std::move(path), file);
	}
	return out;
}

std::shared_ptr<fstest::MapFile>
MapFS::GetFileInfo(const std::string& path) {
	auto p = path.starts_with('/') ? path.substr(1) : path;
	std::shared_lock lock(mu);
	canonicalPath canonical = getCanonicalPath(p);
	if (auto it = m.files.find(canonical); it != m.files.end()) {
		return it->second;
	}
	return nullptr;
}

int comparePathsByParts(std::string_view a, std::string_view b) {
	while (true) {
		auto aIdx = a.find('/');
		auto bIdx = b.find('/');
		if (aIdx == std::string_view::npos ||
		    bIdx == std::string_view::npos) {
			return a.compare(b);
		}
		if (int r = a.substr(0, aIdx).compare(b.substr(0, bIdx));
		    r != 0) {
			return r;
		}
		a = a.substr(aIdx + 1);
		b = b.substr(bIdx + 1);
	}
}

// convertMapFS — vfstest.go.
std::shared_ptr<MapFS>
convertMapFS(const fstest::MapFS& input, bool useCaseSensitiveFileNames,
             std::shared_ptr<Clock> clock) {
	if (!clock) {
		auto c = std::make_shared<clockImpl>();
		c->start = std::chrono::system_clock::now();
		clock = std::move(c);
	}
	auto m = std::make_shared<MapFS>();
	m->useCaseSensitiveFileNames = useCaseSensitiveFileNames;
	m->clock = clock;

	// Verify that the input is well-formed.
	std::unordered_map<canonicalPath, std::string> canonicalPaths;
	for (auto& [path, _] : input.files) {
		canonicalPath canonical = m->getCanonicalPath(path);
		if (auto it = canonicalPaths.find(canonical);
		    it != canonicalPaths.end()) {
			// Ensure consistent panic messages.
			auto& other = it->second;
			auto lo = std::min(path, other);
			auto hi = std::max(path, other);
			TSC_UNREACHABLE(("duplicate path: \"" + lo +
			                 "\" and \"" + hi +
			                 "\" have the same canonical path")
			                    .c_str());
		}
		canonicalPaths[canonical] = path;
	}

	// Sort the input by depth and path so we ensure parent dirs are
	// created before their children, if explicitly specified by the
	// input.
	std::vector<std::string> inputKeys;
	inputKeys.reserve(input.files.size());
	for (auto& [p, _] : input.files) {
		inputKeys.push_back(p);
	}
	std::sort(inputKeys.begin(), inputKeys.end(),
	          [](const std::string& a, const std::string& b) {
		          return comparePathsByParts(a, b) < 0;
	          });

	for (auto& p : inputKeys) {
		auto& file = input.files.at(p);

		// Create all missing intermediate directories so we can attach
		// the realpath to each of them.
		if (auto dir = dirName(p); !dir.empty()) {
			if (auto err = m->mkdirAll(dir, FileMode{0777}); err) {
				TSC_UNREACHABLE(
				    ("failed to create intermediate directories "
				     "for \"" +
				     p + "\": " + err.str())
				        .c_str());
			}
		}
		m->setEntry(p, m->getCanonicalPath(p), *file);
	}

	return m;
}

namespace {

void checkPath(std::string_view p, bool& posix, bool& windows) {
	if (!tspath::isRootedDiskPath(p)) {
		TSC_UNREACHABLE(("non-rooted path \"" + std::string{p} + "\"")
		                    .c_str());
	}
	if (tspath::removeTrailingDirectorySeparator(
	        tspath::normalizePath(p)) != p) {
		TSC_UNREACHABLE(("non-normalized path \"" + std::string{p} +
		                 "\"")
		                    .c_str());
	}
	if (p.starts_with('/')) {
		posix = true;
	} else {
		windows = true;
	}
}

} // namespace

// FromMap / FromMapWithClock — vfstest.go.
std::shared_ptr<vfs::FS>
FromMap(const std::unordered_map<std::string, MapFileInput>& m,
        bool useCaseSensitiveFileNames) {
	auto c = std::make_shared<clockImpl>();
	c->start = std::chrono::system_clock::now();
	return FromMapWithClock(m, useCaseSensitiveFileNames, c);
}

std::shared_ptr<vfs::FS>
FromMapWithClock(const std::unordered_map<std::string, MapFileInput>& m,
                 bool useCaseSensitiveFileNames,
                 std::shared_ptr<Clock> clock) {
	bool posix = false;
	bool windows = false;

	fstest::MapFS mfs;
	mfs.files.reserve(m.size());
	// Sorted creation to ensure times are always guaranteed to be in
	// order.
	std::vector<std::string> keys;
	keys.reserve(m.size());
	for (auto& [p, _] : m) {
		keys.push_back(p);
	}
	std::sort(keys.begin(), keys.end(),
	          [](const std::string& a, const std::string& b) {
		          return comparePathsByParts(a, b) < 0;
	          });
	for (auto& p : keys) {
		auto& f = m.at(p);
		checkPath(p, posix, windows);

		fstest::MapFile file;
		if (auto s = std::get_if<std::string>(&f)) {
			file.Data = *s;
			file.ModTime = clock->Now();
		} else if (auto b = std::get_if<std::vector<uint8_t>>(&f)) {
			file.Data.assign(b->begin(), b->end());
			file.ModTime = clock->Now();
		} else if (auto mf =
		               std::get_if<std::shared_ptr<fstest::MapFile>>(
		                   &f)) {
			file = **mf;
			file.ModTime = clock->Now();
		} else {
			TSC_UNREACHABLE("invalid file type");
		}

		if (file.Mode.v & FileMode::kSymlink) {
			std::string target = file.Data;
			checkPath(target, posix, windows);

			if (target.starts_with('/')) {
				target = target.substr(1);
			}
			file.Data = target;
		}

		std::string key =
		    p.starts_with('/') ? p.substr(1) : p;
		mfs.files[key] =
		    std::make_shared<fstest::MapFile>(std::move(file));
	}

	if (posix && windows) {
		TSC_UNREACHABLE("mixed posix and windows paths");
	}

	return iovfs::From(
	    convertMapFS(mfs, useCaseSensitiveFileNames, clock),
	    useCaseSensitiveFileNames);
}

} // namespace tsc::vfs::vfstest
