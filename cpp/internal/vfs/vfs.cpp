// vfs.cpp — port of tsc/internal/vfs/walkdir.go plus the io/fs generic
// helpers (fs.Stat / fs.ReadDir / fs.ReadFile / fs.Sub / fs.ValidPath /
// fs.FileInfoToDirEntry).
#include "internal/vfs/vfs.h"

#include <algorithm>

#include "internal/ast/ast.h" // TSC_UNREACHABLE
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"

namespace tsc::vfs {

// --- UTF-8 decoding (strict Go utf8.DecodeRuneInString semantics) -------

// decodeRuneStrict — utf8.DecodeRuneInString. Unlike the lax
// decodeUtf8Rune, rejects overlongs, surrogates, and > U+10FFFF via Go's
// acceptRanges on the second byte; invalid -> (RuneError, 1).
static char32_t decodeRuneStrict(std::string_view s, int* width) {
	constexpr char32_t runeError = 0xFFFD;
	if (s.empty()) {
		*width = 0;
		return runeError;
	}
	auto b0 = static_cast<unsigned char>(s[0]);
	if (b0 < 0x80) {
		*width = 1;
		return b0;
	}
	int len = b0 < 0xC0 ? 1 : b0 < 0xE0 ? 2 : b0 < 0xF0 ? 3 : 4;
	if (len == 1 || s.size() < static_cast<size_t>(len)) {
		*width = 1;
		return runeError;
	}
	// Accept ranges for the second byte (utf8 acceptRanges).
	unsigned char lo = 0x80, hi = 0xBF;
	switch (b0) {
	case 0xE0:
		lo = 0xA0;
		break;
	case 0xED:
		hi = 0x9F;
		break;
	case 0xF0:
		lo = 0x90;
		break;
	case 0xF4:
		hi = 0x8F;
		break;
	}
	auto b1 = static_cast<unsigned char>(s[1]);
	if (b1 < lo || b1 > hi) {
		*width = 1;
		return runeError;
	}
	char32_t r = b0 & (0x7F >> len);
	r = (r << 6) | (b1 & 0x3F);
	for (int k = 2; k < len; k++) {
		auto bi = static_cast<unsigned char>(s[k]);
		if ((bi & 0xC0) != 0x80) {
			*width = 1;
			return runeError;
		}
		r = (r << 6) | (bi & 0x3F);
	}
	*width = len;
	return r;
}

// validUtf8 — utf8.ValidString.
static bool validUtf8(std::string_view s) {
	size_t i = 0;
	while (i < s.size()) {
		int w = 0;
		auto r = decodeRuneStrict(s.substr(i), &w);
		if (r == 0xFFFD && w == 1) {
			return false;
		}
		i += static_cast<size_t>(w);
	}
	return true;
}

// --- fs.ValidPath — fs.go -----------------------------------------------

bool validPath(const std::string& name) {
	if (!validUtf8(name)) {
		return false;
	}

	if (name == ".") {
		// special case
		return true;
	}

	// Iterate over elements in name, checking each.
	std::string_view rest{name};
	while (true) {
		size_t i = 0;
		while (i < rest.size() && rest[i] != '/') {
			i++;
		}
		auto elem = rest.substr(0, i);
		if (elem.empty() || elem == "." || elem == "..") {
			return false;
		}
		if (i == rest.size()) {
			return true; // reached clean ending
		}
		rest = rest.substr(i + 1);
	}
}

// --- fs.FileInfoToDirEntry — readdir.go ---------------------------------

namespace {

// dirInfo is a DirEntry based on a FileInfo.
struct dirInfo final : DirEntry {
	std::shared_ptr<FileInfo> fileInfo;

	explicit dirInfo(std::shared_ptr<FileInfo> info)
	    : fileInfo(std::move(info)) {}

	bool IsDir() const override { return fileInfo->IsDir(); }
	FileMode Type() const override { return fileInfo->Mode().Type(); }
	std::pair<std::shared_ptr<FileInfo>, Error> Info() const override {
		return {fileInfo, Error{}};
	}
	std::string Name() const override { return fileInfo->Name(); }
};

} // namespace

std::shared_ptr<DirEntry> fileInfoToDirEntry(
    const std::shared_ptr<FileInfo>& info) {
	if (!info) {
		return nullptr;
	}
	return std::make_shared<dirInfo>(info);
}

// --- fs.Stat — stat.go --------------------------------------------------

std::pair<std::shared_ptr<FileInfo>, Error> fsStat(
    const std::shared_ptr<IoFS>& fsys, const std::string& name) {
	if (auto statfs = std::dynamic_pointer_cast<StatFS>(fsys)) {
		return statfs->Stat(name);
	}

	auto [file, err] = fsys->Open(name);
	if (err) {
		return {nullptr, err};
	}
	auto stat = file->Stat();
	file->Close();
	return stat;
}

// --- fs.ReadDir — readdir.go --------------------------------------------

std::pair<std::vector<std::shared_ptr<DirEntry>>, Error> fsReadDir(
    const std::shared_ptr<IoFS>& fsys, const std::string& name) {
	if (auto readdirfs = std::dynamic_pointer_cast<ReadDirFS>(fsys)) {
		return readdirfs->ReadDir(name);
	}

	auto [file, err] = fsys->Open(name);
	if (err) {
		return {{}, err};
	}

	auto dir = std::dynamic_pointer_cast<ReadDirFile>(file);
	if (!dir) {
		file->Close();
		return {{},
		        makePathError("readdir", name,
		                      Error::newError("not implemented"))};
	}

	auto [list, rdErr] = dir->ReadDir(-1);
	file->Close();
	// fs.ReadDir sorts by filename.
	std::sort(list.begin(), list.end(),
	          [](const std::shared_ptr<DirEntry>& a,
	             const std::shared_ptr<DirEntry>& b) {
		          return a->Name() < b->Name();
	          });
	return {list, rdErr};
}

// --- fs.ReadFile — readfile.go ------------------------------------------

std::pair<std::string, Error> fsReadFile(
    const std::shared_ptr<IoFS>& fsys, const std::string& name) {
	if (auto readfilefs = std::dynamic_pointer_cast<ReadFileFS>(fsys)) {
		return readfilefs->ReadFile(name);
	}

	auto [file, err] = fsys->Open(name);
	if (err) {
		return {"", err};
	}

	size_t size = 0;
	if (auto [info, statErr] = file->Stat(); !statErr) {
		auto size64 = info->Size();
		if (static_cast<int64_t>(static_cast<size_t>(size64)) == size64) {
			size = static_cast<size_t>(size64);
		}
	}

	// Read all, growing the buffer like io.ReadAll does (capacity is
	// size+1 so a small final read still sees EOF correctly).
	std::string data;
	data.reserve(size + 1);
	char buf[4096];
	while (true) {
		auto [n, rerr] = file->Read(std::span<char>(buf, sizeof(buf)));
		data.append(buf, static_cast<size_t>(n));
		if (rerr) {
			file->Close();
			if (rerr == ErrEOF) {
				return {data, Error{}};
			}
			return {data, rerr};
		}
	}
}

// --- fs.Sub — sub.go ----------------------------------------------------

namespace {

// subFS — fs.subFS: implements Open/ReadDir/ReadFile/Stat/Sub over a
// prefixed name space, mirroring Go's delegation to richer interfaces.
struct subFS final : StatFS, ReadDirFS, ReadFileFS, SubFS {
	std::shared_ptr<IoFS> fsys;
	std::string dir;

	subFS(std::shared_ptr<IoFS> fsys, std::string dir)
	    : fsys(std::move(fsys)), dir(std::move(dir)) {}

	// fullName maps name to the fully-qualified name dir/name.
	std::pair<std::string, Error> fullName(const std::string& op,
	                                       const std::string& name) const {
		if (!validPath(name)) {
			return {"", makePathError(op, name, ErrInvalid)};
		}
		// path.Join(dir, name): name is a ValidPath, dir is clean —
		// but Join still cleans ("foo/." -> "foo").
		return {dir == "." ? name : pathJoin({dir, name}), Error{}};
	}

	// fixErr translates PathErrors back to the sub-FS's name space.
	Error fixErr(const Error& err) const {
		auto perr = err.as<PathErrorImpl>();
		if (!perr) {
			return err;
		}
		if (auto [rel, ok] = shorten(perr->path); ok) {
			auto copy = std::make_shared<PathErrorImpl>(*perr);
			copy->path = rel;
			copy->message =
			    copy->op + " " + copy->path + ": " +
			    Error{copy->wrapped}.str();
			return Error{copy};
		}
		return err;
	}

	// shorten maps name (which should start with dir) back to the suffix.
	std::pair<std::string, bool> shorten(const std::string& name) const {
		if (name == dir) {
			return {".", true};
		}
		if (name.size() >= dir.size() + 2 && name[dir.size()] == '/' &&
		    name.substr(0, dir.size()) == dir) {
			return {name.substr(dir.size() + 1), true};
		}
		return {name, false};
	}

	std::pair<std::shared_ptr<File>, Error>
	Open(const std::string& name) override {
		auto [full, err] = fullName("open", name);
		if (err) {
			return {nullptr, err};
		}
		auto [file, oerr] = fsys->Open(full);
		return {file, fixErr(oerr)};
	}

	std::pair<std::vector<std::shared_ptr<DirEntry>>, Error>
	ReadDir(const std::string& name) override {
		auto [full, err] = fullName("readdir", name);
		if (err) {
			return {{}, err};
		}
		auto [list, rerr] = fsReadDir(fsys, full);
		return {list, fixErr(rerr)};
	}

	std::pair<std::string, Error>
	ReadFile(const std::string& name) override {
		auto [full, err] = fullName("readfile", name);
		if (err) {
			return {"", err};
		}
		auto [data, rerr] = fsReadFile(fsys, full);
		return {data, fixErr(rerr)};
	}

	std::pair<std::shared_ptr<FileInfo>, Error>
	Stat(const std::string& name) override {
		auto [full, err] = fullName("stat", name);
		if (err) {
			return {nullptr, err};
		}
		auto [info, serr] = fsStat(fsys, full);
		return {info, fixErr(serr)};
	}

	std::pair<std::shared_ptr<IoFS>, Error>
	Sub(const std::string& d) override {
		auto [full, err] = fullName("sub", d);
		if (err) {
			return {nullptr, err};
		}
		return {std::shared_ptr<IoFS>(
		            std::make_shared<subFS>(fsys, full)),
		        Error{}};
	}
};

} // namespace

std::pair<std::shared_ptr<IoFS>, Error> fsSub(
    const std::shared_ptr<IoFS>& fsys, const std::string& dir) {
	if (!validPath(dir)) {
		return {nullptr, makePathError("sub", dir, ErrInvalid)};
	}
	if (dir == ".") {
		return {fsys, Error{}};
	}
	if (auto subfs = std::dynamic_pointer_cast<SubFS>(fsys)) {
		return subfs->Sub(dir);
	}
	return {std::shared_ptr<IoFS>(std::make_shared<subFS>(fsys, dir)),
	        Error{}};
}

// --- WalkDir — walkdir.go -------------------------------------------------

namespace {

struct walkDirEntry final : DirEntry {
	FS* fileSystem;
	std::string path;
	std::string name;
	FileMode mode;

	walkDirEntry(FS* fileSystem, std::string path, std::string name,
	             FileMode mode)
	    : fileSystem(fileSystem), path(std::move(path)),
	      name(std::move(name)), mode(mode) {}

	std::string Name() const override { return name; }
	bool IsDir() const override { return mode.IsDir(); }
	FileMode Type() const override { return mode.Type(); }
	std::pair<std::shared_ptr<FileInfo>, Error> Info() const override;
};

struct walkDirFileInfo final : FileInfo {
	std::string name;
	FileMode mode;

	walkDirFileInfo(std::string name, FileMode mode)
	    : name(std::move(name)), mode(mode) {}

	std::string Name() const override { return name; }
	int64_t Size() const override { return 0; }
	FileMode Mode() const override { return mode; }
	TimePoint ModTime() const override { return TimePoint{}; }
	bool IsDir() const override { return mode.IsDir(); }
	std::any Sys() const override { return std::any{}; }
};

std::pair<std::shared_ptr<FileInfo>, Error> walkDirEntry::Info() const {
	if (mode & ModeSymlink) {
		return {std::make_shared<walkDirFileInfo>(
		            walkDirFileInfo{name, mode}),
		        Error{}};
	}
	auto info = fileSystem->Stat(path);
	if (!info) {
		return {nullptr, ErrNotExist};
	}
	return {info, Error{}};
}

Error normalizeWalkDirError(const Error& err) {
	if (err.is(SkipDir) || err.is(SkipAll)) {
		return Error{};
	}
	return err;
}

} // namespace

Error WalkDir(FS& fileSystem, const std::string& root,
              const WalkDirFunc& walkFn) {
	auto rootInfo = fileSystem.Stat(root);
	if (!rootInfo) {
		return normalizeWalkDirError(walkFn(root, nullptr, ErrNotExist));
	}

	bool useCaseSensitiveFileNames = fileSystem.UseCaseSensitiveFileNames();
	std::string rootPrefix =
	    root.substr(0, tspath::getRootLength(root));
	auto sameRoot = [&](const std::string& path) {
		auto pathRootLength = tspath::getRootLength(path);
		tspath::ComparePathsOptions opts;
		opts.useCaseSensitiveFileNames = useCaseSensitiveFileNames;
		return pathRootLength == (int)rootPrefix.size() &&
		       tspath::comparePaths(
		           path.substr(0, pathRootLength), rootPrefix, opts) == 0;
	};
	auto equivalent = [&](std::string_view left, std::string_view right) {
		tspath::ComparePathsOptions opts;
		opts.useCaseSensitiveFileNames = useCaseSensitiveFileNames;
		return tspath::comparePaths(left, right, opts) == 0;
	};
	auto canonicalize = [&](const std::string& path) {
		return tspath::getCanonicalFileName(tspath::normalizePath(path),
		                                  useCaseSensitiveFileNames);
	};

	std::unordered_set<std::string> visited;
	std::function<Error(const std::string&, const std::shared_ptr<DirEntry>&,
	                    const std::string&)>
	    visit;
	visit = [&](const std::string& path,
	            const std::shared_ptr<DirEntry>& entry,
	            const std::string& realpath) -> Error {
		if (entry->IsDir()) {
			auto canonicalRealpath = canonicalize(realpath);
			if (visited.count(canonicalRealpath)) {
				return Error{};
			}
			visited.insert(canonicalRealpath);
		}

		if (auto err = walkFn(path, entry, Error{}); err) {
			if (err.is(SkipDir) && entry->IsDir()) {
				return Error{};
			}
			return err;
		}
		if (!entry->IsDir()) {
			return Error{};
		}

		auto entries = fileSystem.GetAccessibleEntries(path);
		std::unordered_set<std::string> directories;
		for (auto& name : entries.directories) {
			directories.insert(name);
		}
		std::vector<std::string> names = entries.directories;
		names.insert(names.end(), entries.files.begin(),
		             entries.files.end());
		std::sort(names.begin(), names.end());
		for (auto& name : names) {
			auto childPath = tspath::combinePaths(path, {name});
			if (!sameRoot(childPath)) {
				continue;
			}

			FileMode mode{};
			if (directories.count(name)) {
				mode = ModeDir;
			}
			std::string childRealpath;
			bool isSymlink = false;
			if (entries.symlinks.has_value()) {
				isSymlink = entries.symlinks->count(name) != 0;
				if (!isSymlink && mode.IsDir()) {
					childRealpath =
					    tspath::combinePaths(realpath, {name});
				}
			} else {
				childRealpath = fileSystem.Realpath(childPath);
				isSymlink = !equivalent(
				    childRealpath,
				    tspath::combinePaths(realpath, {name}));
			}
			if (isSymlink) {
				mode = ModeSymlink;
			}
			auto childEntry = std::make_shared<walkDirEntry>(
			    walkDirEntry{&fileSystem, childPath, name, mode});
			if (!mode.IsDir()) {
				if (auto err = visit(childPath, childEntry, ""); err) {
					if (err.is(SkipDir)) {
						return Error{};
					}
					return err;
				}
				continue;
			}

			if (childRealpath.empty()) {
				childRealpath = fileSystem.Realpath(childPath);
			}
			if (auto err = visit(childPath, childEntry, childRealpath);
			    err) {
				if (err.is(SkipDir)) {
					return Error{};
				}
				return err;
			}
		}
		return Error{};
	};

	std::shared_ptr<DirEntry> rootEntry = fileInfoToDirEntry(rootInfo);
	auto rootRealpath = fileSystem.Realpath(root);
	if (tspath::getRootLength(root) != (int)root.size()) {
		auto parent = tspath::getDirectoryPath(root);
		auto expectedRealpath = tspath::combinePaths(
		    fileSystem.Realpath(parent), {tspath::getBaseFileName(root)});
		if (!equivalent(rootRealpath, expectedRealpath)) {
			rootEntry = std::make_shared<walkDirEntry>(walkDirEntry{
			    &fileSystem, root,
			    std::string{tspath::getBaseFileName(root)},
			    ModeSymlink});
		}
	}
	return normalizeWalkDirError(visit(root, rootEntry, rootRealpath));
}

// --- path package — path/path.go -----------------------------------------

std::string pathClean(std::string_view path) {
	if (path.empty()) {
		return ".";
	}
	bool rooted = path[0] == '/';

	// lazybuf flattened into an always-materialized string; w is the write
	// cursor (buf may be longer than w after backtracking).
	std::string buf;
	size_t w = 0;
	auto append = [&](char c) {
		if (w < buf.size()) buf[w] = c;
		else buf.push_back(c);
		w++;
	};
	auto index = [&](size_t i) { return buf[i]; };

	size_t n = path.size();
	size_t r = 0, dotdot = 0;
	if (rooted) {
		append('/');
		r = 1;
		dotdot = 1;
	}

	while (r < n) {
		if (path[r] == '/') {
			// empty path element
			r++;
		} else if (path[r] == '.' && r + 1 == n) {
			// . element
			r++;
		} else if (path[r] == '.' && path[r + 1] == '.') {
			// .. element
			r += 2;
			if (w > dotdot) {
				// can backtrack
				w--;
				while (w > dotdot && index(w) != '/') {
					w--;
				}
			} else if (!rooted) {
				// cannot backtrack, but not rooted, so append .. element.
				if (w > 0) {
					append('/');
				}
				append('.');
				append('.');
				dotdot = w;
			}
		} else {
			// Real path element. Add slash if needed.
			if ((rooted && w != 1) || (!rooted && w != 0)) {
				append('/');
			}
			for (; r < n && path[r] != '/'; r++) {
				append(path[r]);
			}
		}
	}

	// Turn empty string into "."
	if (w == 0) {
		append('.');
	}
	buf.resize(w);
	return buf;
}

std::pair<std::string, std::string> pathSplit(std::string_view path) {
	// path.Split: scan back for the final '/'.
	size_t i = path.size();
	while (i > 0 && path[i - 1] != '/') {
		i--;
	}
	// i points past the last separator (or 0 if none).
	if (i == 0) {
		return {"", std::string{path}};
	}
	return {std::string{path.substr(0, i)},
	        std::string{path.substr(i)}};
}

std::string pathJoin(std::initializer_list<std::string_view> elems) {
	std::string joined;
	for (auto e : elems) {
		if (e.empty()) continue;
		if (!joined.empty()) joined += '/';
		joined += e;
	}
	if (joined.empty()) return {};
	return pathClean(joined);
}

std::string pathDir(std::string_view path) {
	auto [dir, _] = pathSplit(path);
	return pathClean(dir);
}

std::string pathBase(std::string_view path) {
	if (path.empty()) {
		return ".";
	}
	// Strip trailing slashes.
	while (!path.empty() && path.back() == '/') {
		path.remove_suffix(1);
	}
	// Find the last element.
	auto i = path.rfind('/');
	if (i != std::string_view::npos) {
		path = path.substr(i + 1);
	}
	// If empty now, it had only slashes.
	if (path.empty()) {
		return "/";
	}
	return std::string{path};
}

bool pathIsAbs(std::string_view path) {
	return !path.empty() && path[0] == '/';
}

// --- path.Match — path/match.go ------------------------------------------

namespace {

// scanChunk — path/match.go. Gets the next segment of pattern: a non-star
// string possibly preceded by a star.
inline std::tuple<bool, std::string_view, std::string_view> scanChunk(
    std::string_view pattern) {
	bool star = false;
	while (!pattern.empty() && pattern.front() == '*') {
		pattern.remove_prefix(1);
		star = true;
	}
	bool inrange = false;
	size_t i;
	for (i = 0; i < pattern.size(); i++) {
		switch (pattern[i]) {
		case '\\':
			// error check handled in matchChunk: bad pattern.
			if (i + 1 < pattern.size()) {
				i++;
			}
			break;
		case '[':
			inrange = true;
			break;
		case ']':
			inrange = false;
			break;
		case '*':
			if (!inrange) {
				return {star, pattern.substr(0, i),
				        pattern.substr(i)};
			}
			break;
		}
	}
	return {star, pattern.substr(0, i), pattern.substr(i)};
}

// getEsc — path/match.go. Possibly-escaped character from chunk, for a
// character class.
inline std::tuple<char32_t, std::string_view, Error> getEsc(
    std::string_view chunk) {
	if (chunk.empty() || chunk[0] == '-' || chunk[0] == ']') {
		return {0, "", ErrBadPattern};
	}
	if (chunk[0] == '\\') {
		chunk.remove_prefix(1);
		if (chunk.empty()) {
			return {0, "", ErrBadPattern};
		}
	}
	int n = 0;
	auto r = decodeRuneStrict(chunk, &n);
	if (r == 0xFFFD && n == 1) {
		return {0, "", ErrBadPattern};
	}
	chunk = chunk.substr(n);
	if (chunk.empty()) {
		return {0, "", ErrBadPattern};
	}
	return {r, chunk, Error{}};
}

// matchChunk — path/match.go. Whether chunk matches the beginning of s;
// returns the remainder of s after the match.
inline std::tuple<std::string_view, bool, Error> matchChunk(
    std::string_view chunk, std::string_view s) {
	// failed records whether the match has failed. After the match fails,
	// the loop continues on processing chunk, checking that the pattern is
	// well-formed but no longer reading s.
	bool failed = false;
	while (!chunk.empty()) {
		if (!failed && s.empty()) {
			failed = true;
		}
		switch (chunk[0]) {
		case '[': {
			// character class
			char32_t r = 0;
			if (!failed) {
				int n = 0;
				r = decodeRuneStrict(s, &n);
				s.remove_prefix(n);
			}
			chunk.remove_prefix(1);
			// possibly negated
			bool negated = false;
			if (!chunk.empty() && chunk[0] == '^') {
				negated = true;
				chunk.remove_prefix(1);
			}
			// parse all ranges
			bool match = false;
			int nrange = 0;
			for (;;) {
				if (!chunk.empty() && chunk[0] == ']' && nrange > 0) {
					chunk.remove_prefix(1);
					break;
				}
				char32_t lo, hi;
				std::tuple<char32_t, std::string_view, Error> res =
				    getEsc(chunk);
				if (std::get<2>(res)) {
					return {"", false, std::get<2>(res)};
				}
				lo = std::get<0>(res);
				chunk = std::get<1>(res);
				hi = lo;
				if (chunk[0] == '-') {
					res = getEsc(chunk.substr(1));
					if (std::get<2>(res)) {
						return {"", false, std::get<2>(res)};
					}
					hi = std::get<0>(res);
					chunk = std::get<1>(res);
				}
				if (lo <= r && r <= hi) {
					match = true;
				}
				nrange++;
			}
			if (match == negated) {
				failed = true;
			}
			break;
		}

		case '?':
			if (!failed) {
				if (s[0] == '/') {
					failed = true;
				}
				int n = 0;
				decodeRuneStrict(s, &n);
				s.remove_prefix(n);
			}
			chunk.remove_prefix(1);
			break;

		case '\\':
			chunk.remove_prefix(1);
			if (chunk.empty()) {
				return {"", false, ErrBadPattern};
			}
			[[fallthrough]];

		default:
			if (!failed) {
				if (chunk[0] != s[0]) {
					failed = true;
				}
				s.remove_prefix(1);
			}
			chunk.remove_prefix(1);
			break;
		}
	}
	if (failed) {
		return {"", false, Error{}};
	}
	return {s, true, Error{}};
}

} // namespace

// pathMatch — path.Match.
std::pair<bool, Error> pathMatch(const std::string& pattern,
                                 const std::string& name) {
	std::string_view p{pattern}, n{name};
	while (!p.empty()) {
		auto [star, chunk, rest] = scanChunk(p);
		p = rest;
		if (star && chunk.empty()) {
			// Trailing * matches rest of string unless it has a /.
			return {n.find('/') == std::string_view::npos, Error{}};
		}
		// Look for match at current position.
		auto [t, ok, err] = matchChunk(chunk, n);
		// if we're the last chunk, make sure we've exhausted the name
		// otherwise we'll give a false result even if we could still match
		// using the star
		if (ok && (t.empty() || !p.empty())) {
			n = t;
			continue;
		}
		if (err) {
			return {false, err};
		}
		if (star) {
			// Look for match skipping i+1 bytes.
			// Cannot skip /.
			bool continued = false;
			for (size_t i = 0; i < n.size() && n[i] != '/'; i++) {
				auto [t2, ok2, err2] =
				    matchChunk(chunk, n.substr(i + 1));
				if (ok2) {
					// if we're the last chunk, make sure we
					// exhausted the name
					if (p.empty() && !t2.empty()) {
						continue;
					}
					n = t2;
					continued = true;
					break;
				}
				if (err2) {
					return {false, err2};
				}
			}
			if (continued) {
				continue;
			}
		}
		// Before returning false with no error, check that the remainder
		// of the pattern is syntactically valid.
		while (!p.empty()) {
			auto [_, chunk2, rest2] = scanChunk(p);
			p = rest2;
			if (auto [t3, ok3, err3] = matchChunk(chunk2, "");
			    err3) {
				return {false, err3};
			}
		}
		return {false, Error{}};
	}
	return {n.empty(), Error{}};
}

// --- fs.Glob — io/fs/glob.go ----------------------------------------------

namespace {

constexpr int pathSeparatorsLimit = 10000;

bool hasMeta(const std::string& path) {
	return path.find_first_of("\\*?[") != std::string::npos;
}

std::string cleanGlobPath(std::string_view path) {
	if (path.empty()) {
		return ".";
	}
	if (path == "/") {
		return "/"; // do nothing to the path
	}
	return std::string{path.substr(0, path.size() - 1)};
}

// glob — io/fs/glob.go glob(): one directory level of matching.
std::pair<std::vector<std::string>, Error> globOne(
    const std::shared_ptr<IoFS>& fsys, const std::string& dir,
    const std::string& pattern, std::vector<std::string> matches) {
	auto [fi, serr] = fsStat(fsys, dir);
	if (serr || !fi->IsDir()) {
		return {matches, Error{}}; // ignore I/O errors
	}
	auto [d, oerr] = fsys->Open(dir);
	if (oerr) {
		return {matches, Error{}}; // ignore I/O errors
	}

	auto dirReadFile = std::dynamic_pointer_cast<ReadDirFile>(d);
	if (!dirReadFile) {
		d->Close();
		return {matches, Error{}}; // ignore I/O errors
	}
	auto [names, rerr] = dirReadFile->ReadDir(-1);
	d->Close();
	if (rerr) {
		return {matches, Error{}}; // ignore I/O errors
	}
	std::sort(names.begin(), names.end(),
	          [](const std::shared_ptr<DirEntry>& a,
	             const std::shared_ptr<DirEntry>& b) {
		          return a->Name() < b->Name();
	          });

	for (auto& entry : names) {
		auto [matched, merr] = pathMatch(pattern, entry->Name());
		if (merr) {
			return {matches, merr};
		}
		if (matched) {
			matches.push_back(pathJoin({dir, entry->Name()}));
		}
	}
	return {matches, Error{}};
}

// globWithLimit — io/fs/glob.go.
std::pair<std::vector<std::string>, Error> globWithLimit(
    const std::shared_ptr<IoFS>& fsys, const std::string& pattern,
    int depth) {
	if (depth == pathSeparatorsLimit) {
		return {{}, ErrInvalid};
	}
	if (auto globfs = std::dynamic_pointer_cast<GlobFS>(fsys)) {
		return globfs->Glob(pattern);
	}
	// Check pattern is well-formed.
	if (auto [_, err] = pathMatch(pattern, ""); err) {
		return {{}, err};
	}
	if (!hasMeta(pattern)) {
		auto [_, err] = fsStat(fsys, pattern);
		if (err) {
			return {{}, Error{}};
		}
		return {{pattern}, Error{}};
	}

	auto [dir0, file] = pathSplit(pattern);
	auto dir = cleanGlobPath(dir0);

	if (!hasMeta(dir)) {
		return globOne(fsys, dir, file, {});
	}

	// Prevent infinite recursion. See issue 15879.
	if (dir == pattern) {
		return {{}, ErrInvalid};
	}

	auto [m, gerr] = globWithLimit(fsys, dir, depth + 1);
	if (gerr) {
		return {{}, gerr};
	}
	std::vector<std::string> matches;
	for (auto& d : m) {
		auto res = globOne(fsys, d, file, std::move(matches));
		matches = std::move(res.first);
		if (res.second) {
			return {matches, res.second};
		}
	}
	return {matches, Error{}};
}

} // namespace

std::pair<std::vector<std::string>, Error> fsGlob(
    const std::shared_ptr<IoFS>& fsys, const std::string& pattern) {
	return globWithLimit(fsys, pattern, 0);
}

} // namespace tsc::vfs
