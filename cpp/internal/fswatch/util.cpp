// util.cpp — Go stdlib analogs used by the fswatch port: syscall.Errno
// (errnoErrorObj), os.PathError, os.Stat/os.Lstat, os.ReadDir, utf8 and
// strings.EqualFold helpers. Unix semantics throughout.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // pipe2, name_to_handle_at, O_DIRECTORY, O_NOFOLLOW
#endif

#include "internal/fswatch/fswatch.h"

#include "internal/stringutil/stringutil.h"
#include "internal/vfs/vfs.h"

#ifdef _WIN32
#include "internal/win32/w32compat.h"
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#include <string.h>

#include <algorithm>
#include <cstring>

namespace tsc::fswatch {

// --- syscall.Errno.Error() — linux errstr table -----------------------------

#ifndef _WIN32
// Go's errno text table (go/src/syscall/zerrors_linux_amd64.go) is lowercase;
// glibc strerror capitalizes. Cover every errno this package can surface, and
// fall back to strerror for anything else. On Windows Go's Errno.Error() is
// FormatMessage/raw-code driven — w32::errnoText reproduces it.
static const char* errnoErrstr(int e) {
	switch (e) {
	case EPERM:
		return "operation not permitted";
	case ENOENT:
		return "no such file or directory";
	case ESRCH:
		return "no such process";
	case EINTR:
		return "interrupted system call";
	case EIO:
		return "input/output error";
	case ENXIO:
		return "no such device or address";
	case E2BIG:
		return "argument list too long";
	case ENOEXEC:
		return "exec format error";
	case EBADF:
		return "bad file descriptor";
	case ECHILD:
		return "no child processes";
	case EAGAIN: // EWOULDBLOCK
		return "resource temporarily unavailable";
	case ENOMEM:
		return "cannot allocate memory";
	case EACCES:
		return "permission denied";
	case EFAULT:
		return "bad address";
	case EBUSY:
		return "device or resource busy";
	case EEXIST:
		return "file exists";
	case EXDEV:
		return "invalid cross-device link";
	case ENODEV:
		return "no such device";
	case ENOTDIR:
		return "not a directory";
	case EISDIR:
		return "is a directory";
	case EINVAL:
		return "invalid argument";
	case ENFILE:
		return "too many open files in system";
	case EMFILE:
		return "too many open files";
	case ENOSPC:
		return "no space left on device";
	case ESPIPE:
		return "illegal seek";
	case EROFS:
		return "read-only file system";
	case EMLINK:
		return "too many links";
	case EPIPE:
		return "broken pipe";
	case ENAMETOOLONG:
		return "file name too long";
	case ENOSYS:
		return "function not implemented";
	case ENOTEMPTY:
		return "directory not empty";
	case ELOOP:
		return "too many levels of symbolic links";
	case ENOMSG:
		return "no message of desired type";
	case EOPNOTSUPP: // ENOTSUP
		return "operation not supported";
	default:
		return ::strerror(e);
	}
}
#endif // !_WIN32

std::string errnoErrorObj::Error() const {
#ifdef _WIN32
	return w32::errnoText(e);
#else
	return errnoErrstr(e);
#endif
}

gostd::Error errnoError(int e) {
	return std::make_shared<errnoErrorObj>(e);
}

// errors.Is(err, syscall.Errno(e)) — find the first errnoErrorObj in the
// unwrap chain and compare errno values.
bool errnoIs(const gostd::Error& err, int e) {
	if (err == nullptr) {
		return false;
	}
	if (auto* p = dynamic_cast<errnoErrorObj*>(err.get()); p != nullptr) {
		if (p->e == e) {
			return true;
		}
	}
	for (const auto& u : err->unwrap()) {
		if (errnoIs(u, e)) {
			return true;
		}
	}
	return false;
}

// --- fs sentinels — syscall.Errno.Is mapping --------------------------------

// errors.Is(err, fs.ErrPermission): Errno.Is(ErrPermission) is
// EACCES || EPERM.
bool errIsFsPermission(const gostd::Error& err) {
	return gostd::errorIs(err, fsErrPermission) || errnoIs(err, EACCES) ||
	    errnoIs(err, EPERM);
}

// errors.Is(err, fs.ErrNotExist): Errno.Is(ErrNotExist) is ENOENT.
bool errIsFsNotExist(const gostd::Error& err) {
	return gostd::errorIs(err, fsErrNotExist) || errnoIs(err, ENOENT);
}

// --- os.PathError ------------------------------------------------------------

namespace {

struct pathErrorObj : gostd::ErrObj {
	std::string op;
	std::string path;
	gostd::Error err;
	std::string Error() const override {
		return op + " " + path + ": " + err->Error();
	}
	std::vector<gostd::Error> unwrap() const override { return {err}; }
};

} // namespace

gostd::Error osPathError(std::string_view op, const std::string& path, int e) {
	auto p = std::make_shared<pathErrorObj>();
	p->op = std::string(op);
	p->path = path;
	p->err = errnoError(e);
	return p;
}

// --- os.Stat / os.Lstat ------------------------------------------------------

std::pair<osFileInfo, gostd::Error> osStat(const std::string& path) {
	struct stat st;
	if (::stat(path.c_str(), &st) != 0) {
		return {{}, osPathError("stat", path, errno)};
	}
	return {{S_ISDIR(st.st_mode)}, nullptr};
}

std::pair<osFileInfo, gostd::Error> osLstat(const std::string& path) {
	struct stat st;
	if (::lstat(path.c_str(), &st) != 0) {
		return {{}, osPathError("lstat", path, errno)};
	}
	return {{S_ISDIR(st.st_mode)}, nullptr};
}

// --- os.ReadDir --------------------------------------------------------------
// Go os.ReadDir: Open(O_RDONLY|O_CLOEXEC) + File.ReadDir(-1) + sort by name.
// Entries whose d_type is DT_UNKNOWN (or otherwise unrecognized, ^FileMode(0)
// in Go) are resolved with an lstat — IsNotExist entries are skipped — matching
// newUnixDirent in os/file_unix.go. On Windows, os.File.readdir is
// FindFirstFile/FindNextFile via the compat DIR* — same result shape.

std::pair<std::vector<osDirEntry>, gostd::Error>
osReadDir(const std::string& path) {
	int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC, 0);
	if (fd < 0) {
		return {{}, osPathError("open", path, errno)};
	}
	struct fdGuard {
		int fd;
		~fdGuard() { ::close(fd); }
	} guard{fd};

#ifdef _WIN32
	DIR* dirp = ::fdopendir(fd);
	if (dirp == nullptr) {
		return {{}, osPathError("readdirent", path, errno)};
	}
	guard.fd = -1; // closedir owns the fd now (POSIX fdopendir rule).
	struct dirGuard {
		DIR* p;
		~dirGuard() { ::closedir(p); }
	} dguard{dirp};

	std::vector<osDirEntry> entries;
	for (;;) {
		errno = 0;
		struct dirent* d = ::readdir(dirp);
		if (d == nullptr) {
			if (errno != 0) {
				return {{}, osPathError("readdirent", path, errno)};
			}
			break;
		}
		std::string_view nameBytes(d->d_name);
		if (nameBytes == "." || nameBytes == "..") {
			continue;
		}
		std::string name(nameBytes);
		bool isDir;
		if (d->d_type == DT_DIR) {
			isDir = true;
		} else if (d->d_type == DT_UNKNOWN ||
		           (d->d_type != DT_REG && d->d_type != DT_LNK &&
		            d->d_type != DT_FIFO && d->d_type != DT_SOCK &&
		            d->d_type != DT_CHR && d->d_type != DT_BLK)) {
			// ^FileMode(0) → lstat to resolve the type.
			auto [info, lerr] = osLstat(path + "/" + name);
			if (lerr != nullptr) {
				if (errIsFsNotExist(lerr)) {
					// Disappeared between readdir and stat.
					continue;
				}
				return {{}, lerr};
			}
			isDir = info.isDir;
		} else {
			isDir = false;
		}
		entries.push_back(osDirEntry{std::move(name), isDir});
	}
	std::sort(entries.begin(), entries.end(),
	          [](const osDirEntry& a, const osDirEntry& b) {
		          return a.name < b.name;
	          });
	return {entries, nullptr};
#else
	std::vector<char> buf(8192);
	std::vector<osDirEntry> entries;
	for (;;) {
		long n = ::syscall(SYS_getdents64, fd, buf.data(), buf.size());
		if (n < 0) {
			return {{}, osPathError("readdirent", path, errno)};
		}
		if (n <= 0) {
			break;
		}
		size_t offset = 0;
		while (offset < static_cast<size_t>(n)) {
			auto* d = reinterpret_cast<const linuxDirent64*>(buf.data() +
			                                                 offset);
			uint16_t reclen = d->d_reclen;
			if (reclen == 0 || offset + reclen > static_cast<size_t>(n)) {
				break;
			}
			offset += reclen;
			// Linux keeps zero-inode entries (old XFS/FUSE may report
			// valid files with ino 0); Go's readdir skips them only on
			// non-linux/non-wasip1 platforms.
			size_t nameOff = offsetof(linuxDirent64, d_name);
			std::string_view nameBytes(buf.data() + nameOff +
			                               (offset - reclen),
			                           reclen - nameOff);
			size_t nul = nameBytes.find('\0');
			if (nul != std::string_view::npos) {
				nameBytes = nameBytes.substr(0, nul);
			}
			if (nameBytes == "." || nameBytes == "..") {
				continue;
			}
			std::string name(nameBytes);
			bool isDir;
			if (d->d_type == DT_DIR) {
				isDir = true;
			} else if (d->d_type == DT_UNKNOWN ||
			           (d->d_type != DT_REG && d->d_type != DT_LNK &&
			            d->d_type != DT_FIFO && d->d_type != DT_SOCK &&
			            d->d_type != DT_CHR && d->d_type != DT_BLK)) {
				// ^FileMode(0) → lstat to resolve the type.
				auto [info, lerr] = osLstat(path + "/" + name);
				if (lerr != nullptr) {
					if (errIsFsNotExist(lerr)) {
						// Disappeared between readdir and stat.
						continue;
					}
					return {{}, lerr};
				}
				isDir = info.isDir;
			} else {
				isDir = false;
			}
			entries.push_back(osDirEntry{std::move(name), isDir});
		}
	}
	std::sort(entries.begin(), entries.end(),
	          [](const osDirEntry& a, const osDirEntry& b) {
		          return a.name < b.name;
	          });
	return {entries, nullptr};
#endif
}

// --- utf8.ValidString ---------------------------------------------------------

bool validUtf8(std::string_view s) {
	// Fast path: ASCII bytes.
	while (!s.empty()) {
		uint8_t c = s[0];
		if (c < tsc::kRuneSelf) {
			s = s.substr(1);
			continue;
		}
		int w = 0;
		char32_t r = tsc::decodeUtf8RuneStrict(s, &w);
		// Go: r == RuneError && size == 1 on invalid; a properly encoded
		// U+FFFD decodes with size 3 and is valid.
		if (r == tsc::kRuneError && w == 1) {
			return false;
		}
		s = s.substr(static_cast<size_t>(w));
	}
	return true;
}

// --- strings.EqualFold ---------------------------------------------------------

// strings.EqualFold — per-rune comparison with ASCII fast path and
// unicode.SimpleFold orbit check. The repo's stringutil::simpleFold returns
// the orbit's canonical (minimum) member; "tr ∈ orbit(sr)" is equivalent to
// orbit(sr) == orbit(tr), i.e. equal canonical keys.
bool equalFold(std::string_view s, std::string_view t) {
	while (!s.empty() && !t.empty()) {
		char32_t sr, tr;
		int sw = 0, tw = 0;
		if (static_cast<uint8_t>(s[0]) < tsc::kRuneSelf) {
			sr = static_cast<uint8_t>(s[0]);
			sw = 1;
		} else {
			sr = tsc::decodeUtf8RuneStrict(s, &sw);
		}
		if (static_cast<uint8_t>(t[0]) < tsc::kRuneSelf) {
			tr = static_cast<uint8_t>(t[0]);
			tw = 1;
		} else {
			tr = tsc::decodeUtf8RuneStrict(t, &tw);
		}
		s = s.substr(static_cast<size_t>(sw));
		t = t.substr(static_cast<size_t>(tw));
		if (tr == sr) {
			continue;
		}
		if (tr < sr) {
			std::swap(tr, sr);
		}
		if (tr < tsc::kRuneSelf) {
			// ASCII-only fast path: fold 'A'-'Z' onto 'a'-'z'.
			if ('A' <= sr && sr <= 'Z' && tr == sr + ('a' - 'A')) {
				continue;
			}
			return false;
		}
		if (stringutil::simpleFold(sr) == stringutil::simpleFold(tr)) {
			continue;
		}
		return false;
	}
	return s == t;
}

} // namespace tsc::fswatch
