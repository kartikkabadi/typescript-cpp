// osvfs.cpp — port of tsc/internal/vfs/osvfs/os.go plus the Linux/POSIX
// pieces it delegates to: os.DirFS, nativepath.Realpath (O_PATH +
// /proc/self/fd), nativepath.IsSymlinkOrReparsePoint, osutil.Executable,
// and core.LimitedSemaphore (file-local until the core slice lands).
#include "internal/vfs/osvfs/osvfs.h"

#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits.h>
#include <mutex>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "internal/core/version.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/internal/internal.h"
#include "internal/ast/ast.h" // TSC_UNREACHABLE

namespace tsc::vfs::osvfs {

// --- core/semaphore.go --------------------------------------------------

// LimitedSemaphore — core/semaphore.go. Acquire() returns a release func;
// modeled as an RAII guard.
class LimitedSemaphore {
public:
	explicit LimitedSemaphore(int maxConcurrency) : count_(maxConcurrency) {
		if (maxConcurrency <= 0) {
			TSC_UNREACHABLE("maxConcurrency must be positive");
		}
	}

	// Acquire blocks until a slot is free, then returns a guard whose
	// destruction releases the slot (Go: `defer sema.Acquire()()`).
	[[nodiscard]] std::unique_ptr<void, void (*)(void*)> Acquire() {
		std::unique_lock lock(mu_);
		cv_.wait(lock, [&] { return count_ > 0; });
		count_--;
		return {this, [](void* self) {
			        static_cast<LimitedSemaphore*>(self)->releaseOne();
		        }};
	}

private:
	void releaseOne() {
		{
			std::lock_guard lock(mu_);
			count_++;
		}
		cv_.notify_one();
	}

	std::mutex mu_;
	std::condition_variable cv_;
	int count_;
};

// Semaphore for operations that are effectively blocking syscalls.
static LimitedSemaphore blockingOpSema{128};
// Semaphore for file reads.
static LimitedSemaphore readSema{128};
// Semaphore for file writes.
static LimitedSemaphore writeSema{32};

// --- errno -> vfs::Error ------------------------------------------------

// errnoError — syscall.Errno.Is mapping: ENOENT -> fs.ErrNotExist,
// EEXIST/ENOTEMPTY -> fs.ErrExist, EACCES/EPERM -> fs.ErrPermission,
// EINVAL -> fs.ErrInvalid; everything else wraps the errno text.
static Error errnoError(int e) {
	switch (e) {
	case ENOENT:
		return ErrNotExist;
	case EEXIST:
	case ENOTEMPTY:
		return ErrExist;
	case EACCES:
	case EPERM:
		return ErrPermission;
	case EINVAL:
		return ErrInvalid;
	default:
		return Error::newError(std::strerror(e));
	}
}

static Error pathErrno(const char* op, const std::string& path, int e) {
	return makePathError(op, path, errnoError(e));
}

// --- os file metadata ---------------------------------------------------

static TimePoint fromTimespec(const timespec& ts) {
	return TimePoint{std::chrono::seconds{ts.tv_sec} +
	                 std::chrono::nanoseconds{ts.tv_nsec}};
}

// modeFromStat — Go fileStat.mode mapping (os/stat.go).
static FileMode modeFromStat(mode_t m) {
	FileMode mode{static_cast<uint32_t>(m & 0777)};
	switch (m & S_IFMT) {
	case S_IFBLK:
		mode = mode | ModeDevice;
		break;
	case S_IFCHR:
		mode = mode | ModeDevice | ModeCharDevice;
		break;
	case S_IFDIR:
		mode = mode | ModeDir;
		break;
	case S_IFIFO:
		mode = mode | ModeNamedPipe;
		break;
	case S_IFLNK:
		mode = mode | ModeSymlink;
		break;
	case S_IFREG:
		break; // nothing to set
	case S_IFSOCK:
		mode = mode | ModeSocket;
		break;
	default:
		mode = mode | ModeIrregular;
		break;
	}
	if (m & S_ISUID) {
		mode = mode | ModeSetuid;
	}
	if (m & S_ISGID) {
		mode = mode | ModeSetgid;
	}
	if (m & S_ISVTX) {
		mode = mode | ModeSticky;
	}
	return mode;
}

struct osFileInfo final : FileInfo {
	std::string name;
	struct stat st;

	std::string Name() const override { return name; }
	int64_t Size() const override { return static_cast<int64_t>(st.st_size); }
	FileMode Mode() const override { return modeFromStat(st.st_mode); }
	TimePoint ModTime() const override {
		return fromTimespec(st.st_mtim);
	}
	bool IsDir() const override { return S_ISDIR(st.st_mode); }
	std::any Sys() const override { return st; }
};

// basename — os/stat.go basename(): strip trailing slashes, then leading
// directory names (no cleaning).
static std::string osBasename(const std::string& name) {
	auto v = std::string_view{name};
	// Remove trailing slashes
	while (!v.empty() && v.back() == '/') {
		v.remove_suffix(1);
	}
	// Remove leading directory name
	auto i = v.rfind('/');
	if (i != std::string_view::npos) {
		v = v.substr(i + 1);
	}
	return std::string{v};
}

static std::shared_ptr<FileInfo> statToFileInfo(const std::string& path,
                                                const struct stat& st) {
	auto fi = std::make_shared<osFileInfo>();
	fi->name = osBasename(path);
	fi->st = st;
	return fi;
}

// --- os.File analog -----------------------------------------------------

struct osDirEntry final : DirEntry {
	std::string dirPath; // directory the entry lives in
	std::string name;
	FileMode type;

	osDirEntry(std::string dirPath, std::string name, FileMode type)
	    : dirPath(std::move(dirPath)), name(std::move(name)),
	      type(type) {}

	std::string Name() const override { return name; }
	bool IsDir() const override { return type.IsDir(); }
	FileMode Type() const override { return type; }

	std::pair<std::shared_ptr<FileInfo>, Error> Info() const override {
		struct stat st;
		auto full = dirPath + "/" + name;
		if (::lstat(full.c_str(), &st) != 0) {
			return {nullptr, pathErrno("lstat", full, errno)};
		}
		return {statToFileInfo(full, st), Error{}};
	}
};

static FileMode direntType(unsigned char typ) {
	switch (typ) {
	case DT_BLK:
		return ModeDevice;
	case DT_CHR:
		return ModeDevice | ModeCharDevice;
	case DT_DIR:
		return ModeDir;
	case DT_FIFO:
		return ModeNamedPipe;
	case DT_LNK:
		return ModeSymlink;
	case DT_REG:
		return FileMode{};
	case DT_SOCK:
		return ModeSocket;
	default:
		return ModeIrregular;
	}
}

// osFile — os.File analog over a POSIX fd; ReadDir lazily fdopendir's a
// duplicate fd (Go reuses the fd's getdents offset; we keep a DIR*).
struct osFile final : ReadDirFile {
	std::string name; // full path (Go's f.name for error paths)
	int fd = -1;
	DIR* dirp = nullptr;

	explicit osFile(std::string name, int fd)
	    : name(std::move(name)), fd(fd) {}
	~osFile() override { closeResources(); }

	void closeResources() {
		if (dirp) {
			::closedir(dirp);
			dirp = nullptr;
		}
		if (fd >= 0) {
			::close(fd);
			fd = -1;
		}
	}

	std::pair<std::shared_ptr<FileInfo>, Error> Stat() override {
		if (fd < 0) {
			return {nullptr,
			        makePathError("stat", name, ErrClosed)};
		}
		struct stat st;
		if (::fstat(fd, &st) != 0) {
			return {nullptr, pathErrno("stat", name, errno)};
		}
		return {statToFileInfo(name, st), Error{}};
	}

	std::pair<int, Error> Read(std::span<char> b) override {
		if (fd < 0) {
			return {0, makePathError("read", name, ErrClosed)};
		}
		for (;;) {
			ssize_t n = ::read(fd, b.data(), b.size());
			if (n < 0) {
				if (errno == EINTR) {
					continue;
				}
				return {0, pathErrno("read", name, errno)};
			}
			if (n == 0) {
				return {0, ErrEOF};
			}
			return {static_cast<int>(n), Error{}};
		}
	}

	std::pair<std::vector<std::shared_ptr<DirEntry>>, Error>
	ReadDir(int n) override {
		if (fd < 0) {
			return {{}, makePathError("readdir", name, ErrClosed)};
		}
		if (!dirp) {
			int dupfd = ::dup(fd);
			if (dupfd < 0) {
				return {{}, pathErrno("readdir", name, errno)};
			}
			dirp = ::fdopendir(dupfd);
			if (!dirp) {
				int e = errno;
				::close(dupfd);
				return {{}, pathErrno("readdir", name, e)};
			}
		}
		std::vector<std::shared_ptr<DirEntry>> entries;
		for (;;) {
			errno = 0;
			dirent* d = ::readdir(dirp);
			if (d == nullptr) {
				if (errno != 0) {
					return {entries, pathErrno("readdir", name, errno)};
				}
				// End of directory.
				if (n > 0) {
					return {entries, ErrEOF};
				}
				return {entries, Error{}};
			}
			auto nm = std::string_view{d->d_name};
			if (nm == "." || nm == "..") {
				continue;
			}
			entries.push_back(std::make_shared<osDirEntry>(osDirEntry{
			    name /*dirPath*/, std::string{nm},
			    direntType(d->d_type)}));
			if (n > 0 && (int)entries.size() >= n) {
				return {entries, Error{}};
			}
		}
	}

	Error Close() override {
		if (fd < 0 && !dirp) {
			return ErrClosed;
		}
		closeResources();
		return Error{};
	}
};

// --- os.DirFS analog ----------------------------------------------------

// dirFS — os.dirFS: joins names onto a root directory and performs POSIX
// syscalls. join() = filepathlite.Localize (fs.ValidPath + no NUL) + concat.
struct dirFS final : StatFS, ReadDirFS, ReadFileFS, ReadLinkFS {
	std::string dir;

	explicit dirFS(std::string dir) : dir(std::move(dir)) {}

	std::pair<std::string, Error> join(const std::string& name) const {
		if (dir.empty()) {
			return {"",
			        Error::newError("os: DirFS with empty root")};
		}
		if (!validPath(name)) {
			return {"", ErrInvalid};
		}
		if (name.find('\0') != std::string::npos) {
			return {"", ErrInvalid};
		}
		if (dir.back() == '/') {
			return {dir + name, Error{}};
		}
		return {dir + "/" + name, Error{}};
	}

	std::pair<std::shared_ptr<File>, Error>
	Open(const std::string& name) override {
		auto [fullname, jerr] = join(name);
		if (jerr) {
			return {nullptr, makePathError("open", name, jerr)};
		}
		int fd;
		for (;;) {
			fd = ::open(fullname.c_str(), O_RDONLY | O_CLOEXEC);
			if (fd < 0 && errno == EINTR) {
				continue;
			}
			break;
		}
		if (fd < 0) {
			// DirFS takes a string appropriate for GOOS, while the name
			// argument here is always slash separated. dir.join will have
			// mixed the two; undo that for error reporting.
			return {nullptr, pathErrno("open", name, errno)};
		}
		return {std::shared_ptr<File>(
		            new osFile(fullname, fd)),
		        Error{}};
	}

	std::pair<std::string, Error>
	ReadFile(const std::string& name) override {
		auto [fullname, jerr] = join(name);
		if (jerr) {
			return {"", makePathError("readfile", name, jerr)};
		}
		auto [file, err] = Open(name);
		if (err) {
			return {"", err};
		}
		size_t size = 0;
		if (auto [info, serr] = file->Stat();
		    !serr && info->Mode().IsRegular()) {
			size = static_cast<size_t>(info->Size()) + 1;
		}
		std::string data;
		data.reserve(size);
		char buf[4096];
		while (true) {
			auto [n, rerr] = file->Read(std::span<char>(buf, sizeof(buf)));
			data.append(buf, static_cast<size_t>(n));
			if (rerr) {
				file->Close();
				if (rerr == ErrEOF) {
					return {data, Error{}};
				}
				if (auto pe = rerr.as<PathErrorImpl>()) {
					pe->path = name;
					pe->message = pe->op + " " + pe->path + ": " +
					              Error{pe->wrapped}.str();
				}
				return {"", rerr};
			}
		}
	}

	std::pair<std::vector<std::shared_ptr<DirEntry>>, Error>
	ReadDir(const std::string& name) override {
		auto [fullname, jerr] = join(name);
		if (jerr) {
			return {{}, makePathError("readdir", name, jerr)};
		}
		auto [file, oerr] = Open(name);
		if (oerr) {
			return {{}, oerr};
		}
		auto dir = std::dynamic_pointer_cast<ReadDirFile>(file);
		auto [entries, rerr] = dir->ReadDir(-1);
		file->Close();
		if (rerr) {
			return {{}, rerr};
		}
		std::sort(entries.begin(), entries.end(),
		          [](const std::shared_ptr<DirEntry>& a,
		             const std::shared_ptr<DirEntry>& b) {
			          return a->Name() < b->Name();
		          });
		return {entries, Error{}};
	}

	std::pair<std::shared_ptr<FileInfo>, Error>
	Stat(const std::string& name) override {
		auto [fullname, jerr] = join(name);
		if (jerr) {
			return {nullptr, makePathError("stat", name, jerr)};
		}
		struct stat st;
		if (::stat(fullname.c_str(), &st) != 0) {
			return {nullptr, pathErrno("stat", name, errno)};
		}
		// os.Stat's FileInfo.Name() is basename(fullname); that equals
		// basename(name) up to the trailing "." special case of join.
		auto fi = std::make_shared<osFileInfo>();
		fi->name = osBasename(fullname);
		fi->st = st;
		return {fi, Error{}};
	}

	std::pair<std::shared_ptr<FileInfo>, Error>
	Lstat(const std::string& name) override {
		auto [fullname, jerr] = join(name);
		if (jerr) {
			return {nullptr, makePathError("lstat", name, jerr)};
		}
		struct stat st;
		if (::lstat(fullname.c_str(), &st) != 0) {
			return {nullptr, pathErrno("lstat", name, errno)};
		}
		return {statToFileInfo(fullname, st), Error{}};
	}

	std::pair<std::string, Error>
	ReadLink(const std::string& name) override {
		auto [fullname, jerr] = join(name);
		if (jerr) {
			return {"", makePathError("readlink", name, jerr)};
		}
		char buf[4096];
		for (;;) {
			ssize_t n = ::readlink(fullname.c_str(), buf, sizeof(buf) - 1);
			if (n < 0) {
				if (errno == EINTR) continue;
				return {"", pathErrno("readlink", fullname, errno)};
			}
			buf[n] = '\0';
			return {std::string{buf, static_cast<size_t>(n)}, Error{}};
		}
	}
};

// --- nativepath.Realpath (linux) ----------------------------------------

// ignoringEINTR — nativepath/eintr_unix.go.
template <typename F>
static auto ignoringEINTR(F fn) -> decltype(fn()) {
	for (;;) {
		auto res = fn();
		if (res.second != EINTR) {
			return res;
		}
	}
}

// nativepathRealpath — nativepath/realpath_linux.go: O_PATH + /proc/self/fd
// trick, with EvalSymlinks-style fallback when procfs is absent.
static std::pair<std::string, int> nativepathRealpath(
    const std::string& path) {
	static const bool hasProcSelfFD = [] {
		struct stat st;
		return ::stat("/proc/self/fd/", &st) == 0;
	}();

	if (!hasProcSelfFD) {
		// filepath.EvalSymlinks fallback (realpath(3) equivalent).
		char* resolved = ::realpath(path.c_str(), nullptr);
		if (!resolved) {
			return {"", errno};
		}
		std::string out{resolved};
		::free(resolved);
		return {out, 0};
	}

	auto [fd, openErr] = ignoringEINTR([&] {
		int f = ::open(path.c_str(), O_CLOEXEC | O_PATH, 0);
		return std::pair<int, int>{f, f < 0 ? errno : 0};
	});
	if (fd < 0) {
		return {"", openErr};
	}

	std::string procPath =
	    "/proc/self/fd/" + std::to_string(fd);
	std::string buf(256, '\0');
	for (;;) {
		auto [n, rlErr] = ignoringEINTR([&] {
			ssize_t nn =
			    ::readlink(procPath.c_str(), buf.data(), buf.size());
			return std::pair<ssize_t, int>{nn, nn < 0 ? errno : 0};
		});
		if (n < 0) {
			::close(fd);
			return {"", rlErr};
		}
		if (static_cast<size_t>(n) < buf.size()) {
			::close(fd);
			return {buf.substr(0, n), 0};
		}
		buf.resize(buf.size() * 2);
	}
}

// filepathAbs — filepath.Abs: rooted path -> Clean; else cwd + path -> Clean.
static std::pair<std::string, Error> filepathAbs(const std::string& path) {
	if (path.empty()) {
		char cwd[PATH_MAX];
		if (::getcwd(cwd, sizeof(cwd)) == nullptr) {
			return {"", errnoError(errno)};
		}
		return {std::string{cwd}, Error{}};
	}
	std::string combined;
	if (path[0] == '/') {
		combined = path;
	} else {
		char cwd[PATH_MAX];
		if (::getcwd(cwd, sizeof(cwd)) == nullptr) {
			return {"", errnoError(errno)};
		}
		combined = std::string{cwd} + "/" + path;
	}
	return {pathClean(combined), Error{}};
}

// --- osutil/os.go --------------------------------------------------------

// executable — os.Executable on Linux: readlink("/proc/self/exe") minus a
// " (deleted)" suffix.
static std::pair<std::string, Error> executable() {
	char buf[PATH_MAX];
	ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n < 0) {
		return {"", errnoError(errno)};
	}
	buf[n] = '\0';
	std::string path{buf, static_cast<size_t>(n)};
	static constexpr std::string_view suffix = " (deleted)";
	if (path.size() >= suffix.size() &&
	    path.compare(path.size() - suffix.size(), suffix.size(),
	                 suffix) == 0) {
		path.resize(path.size() - suffix.size());
	}
	return {path, Error{}};
}

// --- os.MkdirAll / os.Remove / os.Chtimes --------------------------------

static Error osMkdir(const std::string& path, FileMode perm) {
	if (::mkdir(path.c_str(), static_cast<mode_t>(perm.v)) != 0) {
		return pathErrno("mkdir", path, errno);
	}
	return Error{};
}

// mkdirAll — os.MkdirAll.
static Error mkdirAll(const std::string& path, FileMode perm) {
	struct stat st;
	if (::stat(path.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			return Error{};
		}
		return makePathError("mkdir", path, errnoError(ENOTDIR));
	}

	// Slow path: make sure parent exists and then call Mkdir for path.
	// Skip trailing path separator.
	auto v = std::string_view{path};
	size_t i = v.size();
	while (i > 0 && v[i - 1] == '/') {
		i--;
	}
	size_t j = i;
	// Scan backward over element.
	while (j > 0 && v[j - 1] != '/') {
		j--;
	}
	if (j > 1) {
		// Create parent.
		auto parent = std::string{v.substr(0, j - 1)};
		if (auto err = mkdirAll(parent, perm); err) {
			return err;
		}
	}

	// Parent now exists; invoke Mkdir and use its result.
	if (auto err = osMkdir(path, perm); err) {
		// Handle arguments like "foo/." by double-checking that directory
		// doesn't exist.
		if (::lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
			return Error{};
		}
		return err;
	}
	return Error{};
}

// --- isFileSystemCaseSensitive probe ------------------------------------

// swapCase — os.go:77-86: lowercase <-> uppercase per-rune.
static std::string swapCase(const std::string& str) {
	std::string out;
	out.reserve(str.size());
	size_t i = 0;
	while (i < str.size()) {
		int w = 0;
		auto r = tsc::decodeUtf8Rune(std::string_view(str).substr(i), &w);
		auto upper = stringutil::toUpperRune(r);
		if (upper == r) {
			upper = stringutil::toLowerRune(r);
		}
		char buf[4];
		int n = encodeUtf8Rune(upper, buf);
		out.append(buf, n);
		i += w;
	}
	return out;
}

// We do this right at startup to minimize the chance that executable gets
// moved or deleted.
static bool isFileSystemCaseSensitive() {
	auto [exe, err] = executable();
	if (err) {
		TSC_UNREACHABLE(("vfs: failed to get executable path: " +
		                err.str()).c_str());
	}

	// If the current executable exists under a different case, we must be
	// case-insensitive.
	auto swapped = swapCase(exe);
	struct stat st;
	if (::stat(swapped.c_str(), &st) != 0) {
		if (errno == ENOENT) {
			return true;
		}
		TSC_UNREACHABLE(("vfs: failed to stat \"" + swapped + "\": " +
		                std::strerror(errno)).c_str());
	}
	return false;
}

// --- osFS -----------------------------------------------------------------

// osFSRealpath — os.go:122-136.
static std::string osFSRealpath(const std::string& path) {
	internal::rootLength(path); // Assert path is rooted

	std::string orig = path;
	// filepath.FromSlash is a no-op on Linux.
	auto [rp, err] = nativepathRealpath(path);
	if (err != 0) {
		return orig;
	}
	auto [abs, aerr] = filepathAbs(rp);
	if (aerr) {
		return orig;
	}
	return tspath::normalizeSlashes(abs);
}

// isReparsePoint — nativepath.IsSymlinkOrReparsePoint (non-Windows):
// lstat + ModeSymlink.
static bool isReparsePoint(const std::string& path) {
	struct stat st;
	return ::lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
}

struct osFS final : vfs::FS {
	internal::Common common;

	osFS() {
		common.RootFor = [](const std::string& root) {
			return std::shared_ptr<IoFS>{
			    std::make_shared<dirFS>(root)};
		};
		common.IsReparsePoint = [](const std::string& path) {
			return isReparsePoint(path);
		};
	}

	bool UseCaseSensitiveFileNames() override {
		static const bool value = isFileSystemCaseSensitive();
		return value;
	}

	std::pair<std::string, bool>
	ReadFile(const std::string& path) override {
		auto guard = readSema.Acquire();
		return common.ReadFile(path);
	}

	bool DirectoryExists(const std::string& path) override {
		auto guard = blockingOpSema.Acquire();
		return common.DirectoryExists(path);
	}

	bool FileExists(const std::string& path) override {
		auto guard = blockingOpSema.Acquire();
		return common.FileExists(path);
	}

	Entries GetAccessibleEntries(const std::string& path) override {
		auto guard = blockingOpSema.Acquire();
		return common.GetAccessibleEntries(path);
	}

	std::shared_ptr<FileInfo> Stat(const std::string& path) override {
		auto guard = blockingOpSema.Acquire();
		return common.Stat(path);
	}

	std::string Realpath(const std::string& path) override {
		auto guard = blockingOpSema.Acquire();
		return osFSRealpath(path);
	}

	Error writeFileWithFlag(const std::string& path,
	                         const std::string& content, int flag) {
		auto guard = writeSema.Acquire();

		int fd;
		for (;;) {
			fd = ::open(path.c_str(), flag, 0666);
			if (fd < 0 && errno == EINTR) {
				continue;
			}
			break;
		}
		if (fd < 0) {
			return pathErrno("open", path, errno);
		}
		// WriteString: loop until all bytes are written.
		size_t off = 0;
		while (off < content.size()) {
			ssize_t n = ::write(fd, content.data() + off,
			                    content.size() - off);
			if (n < 0) {
				if (errno == EINTR) {
					continue;
				}
				int e = errno;
				::close(fd);
				return pathErrno("write", path, e);
			}
			off += static_cast<size_t>(n);
		}
		::close(fd);
		return Error{};
	}

	Error ensureDirectoryExists(const std::string& directoryPath) {
		auto guard = blockingOpSema.Acquire();
		return mkdirAll(directoryPath, FileMode{0777});
	}

	Error writeFileEnsuringDir(const std::string& path,
	                            const std::string& content, int flag) {
		internal::rootLength(path); // Assert path is rooted
		if (auto err = writeFileWithFlag(path, content, flag); !err) {
			return Error{};
		}
		if (auto err = ensureDirectoryExists(tspath::getDirectoryPath(
		        tspath::normalizePath(path)));
		    err) {
			return err;
		}
		return writeFileWithFlag(path, content, flag);
	}

	Error WriteFile(const std::string& path,
	                const std::string& content) override {
		return writeFileEnsuringDir(path, content,
		                            O_WRONLY | O_CREAT | O_TRUNC);
	}

	Error AppendFile(const std::string& path,
	                 const std::string& content) override {
		return writeFileEnsuringDir(path, content,
		                            O_WRONLY | O_CREAT | O_APPEND);
	}

	Error Remove(const std::string& path) override {
		auto guard = blockingOpSema.Acquire();
		// os.RemoveAll: removing a missing path is not an error.
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
		if (ec) {
			return makePathError("removeall", path,
			                     errnoError(ec.value()));
		}
		return Error{};
	}

	Error Chtimes(const std::string& path, TimePoint aTime,
	              TimePoint mTime) override {
		auto guard = blockingOpSema.Acquire();
		timespec times[2];
		times[0] = timespec{
		    std::chrono::duration_cast<std::chrono::seconds>(
		        aTime.time_since_epoch())
		        .count(),
		    std::chrono::duration_cast<std::chrono::nanoseconds>(
		        aTime.time_since_epoch() % std::chrono::seconds{1})
		        .count()};
		times[1] = timespec{
		    std::chrono::duration_cast<std::chrono::seconds>(
		        mTime.time_since_epoch())
		        .count(),
		    std::chrono::duration_cast<std::chrono::nanoseconds>(
		        mTime.time_since_epoch() % std::chrono::seconds{1})
		        .count()};
		if (::utimensat(AT_FDCWD, path.c_str(), times, 0) != 0) {
			return pathErrno("chtimes", path, errno);
		}
		return Error{};
	}
};

// FS — os.go:30-39.
vfs::FS* FS() {
	static osFS instance;
	return &instance;
}

// GetGlobalTypingsCacheLocation — os.go:193-206.
std::string GetGlobalTypingsCacheLocation() {
	// os.UserCacheDir: $XDG_CACHE_HOME or $HOME/.cache; os.TempDir() fallback.
	std::string cacheDir;
	if (const char* xdg = ::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
		cacheDir = xdg;
	} else if (const char* home = ::getenv("HOME"); home && *home) {
		cacheDir = std::string{home} + "/.cache";
	}
	if (cacheDir.empty()) {
		if (const char* tmp = ::getenv("TMPDIR"); tmp && *tmp) {
			cacheDir = tmp;
		} else {
			cacheDir = "/tmp";
		}
	}
	return tspath::combinePaths(cacheDir,
	                            {"typescript", tsc::versionMajorMinor()});
}

} // namespace tsc::vfs::osvfs
