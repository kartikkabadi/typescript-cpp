// w32compat.cpp — implementation of the POSIX-shaped Go-on-Windows layer.
// Each function ports the corresponding Go stdlib path (os/stat_windows.go,
// os/file_windows.go, os/removeall_windows.go, syscall/syscall_windows.go)
// rather than POSIX semantics.
#include "internal/win32/w32compat.h"
#ifdef _WIN32

#include <stdarg.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>

#include <direct.h>   // _wgetcwd, _wchdir
#include <fcntl.h>    // _O_BINARY & friends
#include <io.h>       // _open_osfhandle, _get_osfhandle, _read, _write, _close,
                      // _lseeki64, _dup, _dup2, _isatty, _commit, _chsize_s
#include <winioctl.h> // FSCTL_GET_REPARSE_POINT, REPARSE_DATA_BUFFER

namespace {

// REPARSE_DATA_BUFFER is not exposed by this SDK's winioctl.h; define the
// documented layout locally (matches Windows SDK _REPARSE_DATA_BUFFER).
struct w32ReparseDataBuffer {
	ULONG ReparseTag;
	USHORT ReparseDataLength;
	USHORT Reserved;
	union {
		struct {
			USHORT SubstituteNameOffset;
			USHORT SubstituteNameLength;
			USHORT PrintNameOffset;
			USHORT PrintNameLength;
			ULONG Flags;
			WCHAR PathBuffer[1];
		} SymbolicLinkReparseBuffer;
		struct {
			USHORT SubstituteNameOffset;
			USHORT SubstituteNameLength;
			USHORT PrintNameOffset;
			USHORT PrintNameLength;
			WCHAR PathBuffer[1];
		} MountPointReparseBuffer;
		struct {
			UCHAR DataBuffer[1];
		} GenericReparseBuffer;
	};
};

// FILETIME (100ns since 1601-01-01 UTC) -> unix timespec.
void filetimeToTimespec(const FILETIME& ft, struct timespec* ts) {
	unsigned long long t =
	    (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) |
	    ft.dwLowDateTime;
	if (t == 0) {
		ts->tv_sec = 0;
		ts->tv_nsec = 0;
		return;
	}
	unsigned long long sec = t / 10000000ULL;
	unsigned long long nsec = (t % 10000000ULL) * 100ULL;
	ts->tv_sec = static_cast<long>(sec - 11644473600ULL);
	ts->tv_nsec = static_cast<long>(nsec);
}

void timespecToFiletime(const struct timespec& ts, FILETIME* ft) {
	unsigned long long t =
	    (static_cast<unsigned long long>(ts.tv_sec) + 11644473600ULL) *
	            10000000ULL +
	    static_cast<unsigned long long>(ts.tv_nsec) / 100ULL;
	ft->dwLowDateTime = static_cast<DWORD>(t & 0xffffffffULL);
	ft->dwHighDateTime = static_cast<DWORD>(t >> 32);
}

thread_local unsigned long tlsLastWin32Error = 0;
thread_local int tlsPosixErrno = 0;

} // namespace

namespace w32 {

int errnoFromWin32(unsigned long e) {
	switch (e) {
	case ERROR_FILE_NOT_FOUND:
	case ERROR_BAD_NETPATH: // Errno.Is(ErrNotExist)
		return ENOENT;
	case ERROR_PATH_NOT_FOUND:
		// Go's ENOTDIR is literally ERROR_PATH_NOT_FOUND (a raw code,
		// not an invented APPLICATION_ERROR) — keep it distinct so
		// errno==ENOTDIR checks behave like GOOS=windows.
		return ENOTDIR;
	case ERROR_FILE_EXISTS:
	case ERROR_ALREADY_EXISTS:
		return EEXIST;
	case ERROR_DIR_NOT_EMPTY:
		return ENOTEMPTY;
	case ERROR_ACCESS_DENIED:
	case ERROR_CURRENT_DIRECTORY:
		return EACCES;
	case ERROR_WRITE_PROTECT:
	case ERROR_LOCK_VIOLATION:
	case ERROR_SHARING_VIOLATION:
		return EACCES;
	case ERROR_INVALID_PARAMETER:
	case ERROR_INVALID_NAME:
	case ERROR_BAD_PATHNAME:
	case ERROR_INVALID_DRIVE:
	case ERROR_FILENAME_EXCED_RANGE:
		return EINVAL;
	case ERROR_DIRECTORY: // "The directory name is invalid" -> ENOTDIR
		return ENOTDIR;
	case ERROR_BROKEN_PIPE:
	case ERROR_NO_DATA:
		return EPIPE;
	case ERROR_DISK_FULL:
		return ENOSPC;
	case ERROR_INVALID_HANDLE:
		return EBADF;
	case ERROR_NOT_ENOUGH_MEMORY:
	case ERROR_OUTOFMEMORY:
		return ENOMEM;
	case ERROR_BUSY_DRIVE:
	case ERROR_BUSY:
	case ERROR_LOCK_FAILED:
		return EBUSY;
	case ERROR_INVALID_FUNCTION:
		return ENOSYS;
	case ERROR_CALL_NOT_IMPLEMENTED:
	case ERROR_NOT_SUPPORTED:
		return ENOSYS;
	case ERROR_SEM_TIMEOUT:
		return EAGAIN;
	case ERROR_CANT_RESOLVE_FILENAME: // symlink loop
		return ELOOP;
	default:
		return EIO;
	}
}

int setErrFromWin32(unsigned long win32err) {
	if (win32err == 0) {
		win32err = ERROR_OPERATION_ABORTED;
	}
	tlsLastWin32Error = win32err;
	errno = errnoFromWin32(win32err);
	tlsPosixErrno = errno;
	return -1;
}

int setErrnoPair(int posixErr, unsigned long rawWin32) {
	tlsLastWin32Error = rawWin32;
	errno = posixErr;
	tlsPosixErrno = posixErr;
	return -1;
}

unsigned long lastWin32Error() { return tlsLastWin32Error; }

// win32Text — FormatMessage for a raw Win32 error code (what
// syscall.Errno.Error() does for raw codes).
std::string win32Text(unsigned long raw) {
	LPWSTR msg = nullptr;
	DWORD n = FormatMessageW(
	    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
	        FORMAT_MESSAGE_IGNORE_INSERTS,
	    nullptr, raw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
	    reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
	std::string out;
	if (n > 0 && msg) {
		std::wstring w(msg, n);
		LocalFree(msg);
		while (!w.empty() && (w.back() == L'\r' || w.back() == L'\n')) {
			w.pop_back();
		}
		out = w32::narrow(w);
	} else {
		if (msg) LocalFree(msg);
		char tmp[128];
		snprintf(tmp, sizeof(tmp), "win32 error %lu", raw);
		out = tmp;
	}
	return out;
}

std::string errnoText(int e) {
	unsigned long raw = tlsLastWin32Error;
	if (raw == 0 || tlsPosixErrno != e) {
		// No recorded win32 code — translate the posix errno back into the
		// text Go's Errno.Error() gives for the invented APPLICATION_ERROR
		// values (error strings in zerrors_windows.go).
		switch (e) {
		case EACCES: return "permission denied";
		case EEXIST: return "file exists";
		case EINTR: return "interrupted system call";
		case EINVAL: return "invalid argument";
		case EISDIR: return "is a directory";
		case EMFILE: return "too many open files";
		case ENOENT: return "The system cannot find the file specified.";
		case ENOMEM: return "cannot allocate memory";
		case ENOSPC: return "no space left on device";
		case ENOSYS: return "function not implemented";
		case ENOTEMPTY: return "directory not empty";
		case ENOTDIR: return "The system cannot find the path specified.";
		case EPERM: return "operation not permitted";
		case EPIPE: return "broken pipe";
		case EROFS: return "read-only file system";
		case ESPIPE: return "illegal seek";
		case EBUSY: return "device or resource busy";
		default: return std::strerror(e);
		}
	}
	return win32Text(raw);
}

// --- UTF conversion ------------------------------------------------------

std::wstring widen(std::string_view s) {
	if (s.empty()) return {};
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
	                            static_cast<int>(s.size()), nullptr, 0);
	if (n <= 0) {
		// MB_ERR_INVALID_CHARS failed on malformed input; fall back to the
		// lenient conversion (matches Go's U+FFFD replacement).
		n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
		                        static_cast<int>(s.size()), nullptr, 0);
	}
	if (n <= 0) return {};
	std::wstring out(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
	                    out.data(), n);
	return out;
}

std::string narrow(std::wstring_view w) {
	if (w.empty()) return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
	                            static_cast<int>(w.size()), nullptr, 0,
	                            nullptr, nullptr);
	if (n <= 0) return {};
	std::string out(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
	                    out.data(), n, nullptr, nullptr);
	return out;
}

// fixLongPath — os/path_windows.go: absolute paths longer than ~247 chars
// get the \\?\ prefix; UNC becomes \\?\UNC\server\share.
std::wstring winPath(std::string_view path) {
	std::wstring w = widen(path);
	// Normalise '/' to '\' — Win32 accepts both but canonical form is '\'.
	for (auto& c : w) {
		if (c == L'/') c = L'\\';
	}
	if (w.empty()) return w;
	if (w.rfind(L"\\\\?\\", 0) == 0 || w.rfind(L"\\??\\", 0) == 0) {
		return w;
	}
	if (w.size() > 1 && w[0] == L'\\' && w[1] == L'\\') {
		if (w.size() < 248) {
			return w; // short UNC path passes through
		}
		return L"\\\\?\\UNC\\" + w.substr(2);
	}
	if (w.size() >= 248) {
		return L"\\\\?\\" + w;
	}
	return w;
}

// Go os gets absolute canonical paths back from Win32 calls with the
// \\?\ prefix; strip it like postCleanPath/GetFinalPathNameByHandle users do.
std::string fromWin32Path(std::wstring_view w) {
	std::wstring v{w};
	if (v.rfind(L"\\\\?\\UNC\\", 0) == 0) {
		v = L"\\\\" + v.substr(8);
	} else if (v.rfind(L"\\\\?\\", 0) == 0) {
		v = v.substr(4);
	}
	return narrow(v);
}

// --- fd table ------------------------------------------------------------

int fdFromHandle(HANDLE h) {
	int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h), _O_BINARY);
	if (fd < 0) {
		CloseHandle(h);
		return setErrnoPair(EBADF, ERROR_INVALID_HANDLE);
	}
	return fd;
}

HANDLE handleFromFd(int fd) {
	intptr_t h = _get_osfhandle(fd);
	if (h == -1 || h == -2) {
		return INVALID_HANDLE_VALUE;
	}
	return reinterpret_cast<HANDLE>(h);
}

bool setFdInheritable(int fd, bool inheritable) {
	HANDLE h = handleFromFd(fd);
	if (h == INVALID_HANDLE_VALUE) return false;
	return SetHandleInformation(h, HANDLE_FLAG_INHERIT,
	                            inheritable ? HANDLE_FLAG_INHERIT : 0) != 0;
}

int makePipe(int fds[2]) {
	HANDLE rh = nullptr, wh = nullptr;
	// Go's os.Pipe on Windows: CreatePipe with default (non-inheritable).
	if (!CreatePipe(&rh, &wh, nullptr, 0)) {
		return setErrFromWin32(GetLastError());
	}
	int rfd = _open_osfhandle(reinterpret_cast<intptr_t>(rh), _O_RDONLY | _O_BINARY);
	if (rfd < 0) {
		CloseHandle(rh);
		CloseHandle(wh);
		return setErrnoPair(EMFILE, ERROR_TOO_MANY_OPEN_FILES);
	}
	int wfd = _open_osfhandle(reinterpret_cast<intptr_t>(wh), _O_WRONLY | _O_BINARY);
	if (wfd < 0) {
		_close(rfd);
		CloseHandle(wh);
		return setErrnoPair(EMFILE, ERROR_TOO_MANY_OPEN_FILES);
	}
	fds[0] = rfd;
	fds[1] = wfd;
	return 0;
}

} // namespace w32

// --- POSIX-shaped syscalls ------------------------------------------------

int open(const char* path, int oflag, ...) {
	int mode = 0666;
	if (oflag & O_CREAT) {
		va_list ap;
		va_start(ap, oflag);
		mode = va_arg(ap, int);
		va_end(ap);
	}
	DWORD access = 0;
	switch (oflag & O_ACCMODE) {
	case O_RDONLY: access = GENERIC_READ; break;
	case O_WRONLY: access = GENERIC_WRITE; break;
	case O_RDWR: access = GENERIC_READ | GENERIC_WRITE; break;
	}
	if (oflag & O_APPEND) {
		access &= ~GENERIC_WRITE;
		access |= FILE_APPEND_DATA;
	}
	DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
	DWORD dispo;
	if ((oflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
		dispo = CREATE_NEW;
	} else if ((oflag & (O_CREAT | O_TRUNC)) == (O_CREAT | O_TRUNC)) {
		dispo = CREATE_ALWAYS;
	} else if (oflag & O_CREAT) {
		dispo = OPEN_ALWAYS;
	} else if (oflag & O_TRUNC) {
		dispo = TRUNCATE_EXISTING;
	} else {
		dispo = OPEN_EXISTING;
	}
	DWORD flags = FILE_FLAG_BACKUP_SEMANTICS; // like Go os.Open: dirs openable
	if (oflag & O_NOFOLLOW) {
		flags |= FILE_FLAG_OPEN_REPARSE_POINT;
	}
	if (oflag & O_SEQUENTIAL) flags |= FILE_FLAG_SEQUENTIAL_SCAN;
	if (oflag & O_RANDOM) flags |= FILE_FLAG_RANDOM_ACCESS;
	if (oflag & O_TEMPORARY) {
		flags |= FILE_FLAG_DELETE_ON_CLOSE;
	}
	// O_CLOEXEC: CRT fds are non-inheritable by default — matches CLOEXEC.
	std::wstring wpath = w32::widen(path);
	HANDLE h = CreateFileW(wpath.c_str(), access, share, nullptr, dispo,
	                       flags, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return w32::setErrFromWin32(GetLastError());
	}
	if (oflag & O_DIRECTORY) {
		BY_HANDLE_FILE_INFORMATION bi{};
		if (!GetFileInformationByHandle(h, &bi) ||
		    !(bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			CloseHandle(h);
			return w32::setErrnoPair(ENOTDIR, ERROR_DIRECTORY);
		}
	}
	return w32::fdFromHandle(h);
}

int creat(const char* path, int mode) {
	return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int close(int fd) { return _close(fd); }

ssize_t read(int fd, void* buf, size_t n) {
	// CRT _read handles console/pipe/file handles uniformly.
	int r = _read(fd, buf, static_cast<unsigned int>(
	                     n > 0x7fffffff ? 0x7fffffff : n));
	if (r < 0) {
		// CRT sets POSIX errno; record a plausible win32 code.
		switch (errno) {
		case EBADF:
			return w32::setErrnoPair(EBADF, ERROR_INVALID_HANDLE);
		case EACCES:
			return w32::setErrnoPair(EACCES, ERROR_ACCESS_DENIED);
		case EPIPE:
			return w32::setErrnoPair(EPIPE, ERROR_BROKEN_PIPE);
		default:
			return -1;
		}
	}
	return r;
}

ssize_t write(int fd, const void* buf, size_t n) {
	int r = _write(fd, buf, static_cast<unsigned int>(
	                      n > 0x7fffffff ? 0x7fffffff : n));
	if (r < 0) {
		switch (errno) {
		case EBADF:
			return w32::setErrnoPair(EBADF, ERROR_INVALID_HANDLE);
		case ENOSPC:
			return w32::setErrnoPair(ENOSPC, ERROR_DISK_FULL);
		case EPIPE:
			return w32::setErrnoPair(EPIPE, ERROR_BROKEN_PIPE);
		default:
			return -1;
		}
	}
	return r;
}

long long lseek(int fd, long long off, int whence) {
	return _lseeki64(fd, off, whence);
}

int dup(int fd) {
	int nfd = _dup(fd);
	if (nfd < 0) return w32::setErrnoPair(EBADF, ERROR_INVALID_HANDLE);
	return nfd;
}

int dup2(int fd, int fd2) {
	int r = _dup2(fd, fd2);
	if (r < 0) return w32::setErrnoPair(EBADF, ERROR_INVALID_HANDLE);
	return r;
}

int pipe(int fds[2]) { return w32::makePipe(fds); }

int isatty(int fd) { return _isatty(fd); }

int ioctl(int fd, unsigned long request, void* argp) {
	HANDLE h = w32::handleFromFd(fd);
	if (h == INVALID_HANDLE_VALUE) {
		return w32::setErrnoPair(EBADF, ERROR_INVALID_HANDLE);
	}
	switch (request) {
	case TIOCGWINSZ: {
		auto* ws = static_cast<winsize*>(argp);
		CONSOLE_SCREEN_BUFFER_INFO info{};
		if (GetFileType(h) == FILE_TYPE_CHAR &&
		    GetConsoleScreenBufferInfo(h, &info)) {
			ws->ws_row = static_cast<unsigned short>(info.srWindow.Bottom -
			                                         info.srWindow.Top + 1);
			ws->ws_col = static_cast<unsigned short>(info.srWindow.Right -
			                                         info.srWindow.Left + 1);
			return 0;
		}
		ws->ws_row = 0;
		ws->ws_col = 0;
		// Go returns ENOTTY ("inappropriate ioctl for device") for non-tty.
		return w32::setErrnoPair(ENOTTY, ERROR_INVALID_FUNCTION);
	}
	case TIOCINQ: {
		DWORD avail = 0;
		if (GetFileType(h) == FILE_TYPE_PIPE) {
			PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr);
		}
		*static_cast<unsigned long*>(argp) = avail;
		return 0;
	}
	default:
		return w32::setErrnoPair(ENOTTY, ERROR_INVALID_FUNCTION);
	}
}

int fcntl(int fd, int cmd, ...) {
	switch (cmd) {
	case F_DUPFD:
	case F_DUPFD_CLOEXEC:
		return dup(fd);
	case F_GETFD:
		return 0; // our fds are never inheritable -> always CLOEXEC-ish
	case F_SETFD:
		return 0;
	case F_GETFL:
		return O_BINARY;
	case F_SETFL:
		return 0;
	default:
		return w32::setErrnoPair(EINVAL, ERROR_INVALID_PARAMETER);
	}
}

// --- stat ------------------------------------------------------------------

namespace {

// fileStat equivalent: attrs+times+size+reparse tag, and the derived st_mode.
void fillStatFromAttrs(struct stat* st, unsigned long attrs,
                       unsigned long reparseTag, long long size,
                       const FILETIME& ctime, const FILETIME& atime,
                       const FILETIME& mtime, int filetype) {
	// Go fileStat.mode() (os/types_windows.go:177-230): perm bits first;
	// the dir/filetype block only runs for non-name-surrogate entries;
	// the reparse switch then OR's type bits (so non-surrogate reparse
	// points keep their dir/file bits plus ModeIrregular).
	unsigned int perm = (attrs & FILE_ATTRIBUTE_READONLY) ? 0444 : 0666;
	bool surrogate =
	    (attrs & FILE_ATTRIBUTE_REPARSE_POINT) &&
	    (reparseTag & 0x20000000) /*IO_REPARSE_TAG_SURROGATE_MASK*/;
	unsigned int m = perm;
	if (!surrogate) {
		if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
			m = S_IFDIR | (perm | 0111);
		} else if (filetype == FILE_TYPE_PIPE) {
			m = S_IFIFO | perm;
		} else if (filetype == FILE_TYPE_CHAR) {
			m = S_IFCHR | perm;
		} else {
			m = S_IFREG | perm;
		}
	}
	if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
		switch (reparseTag) {
		case IO_REPARSE_TAG_SYMLINK:
			m |= S_IFLNK;
			break;
		case IO_REPARSE_TAG_AF_UNIX:
			m |= S_IFSOCK;
			break;
		case 0x80000013 /*IO_REPARSE_TAG_DEDUP*/:
			// DEDUP files stay regular — Go deliberately adds nothing.
			break;
		default:
			// Junctions/mount points and all other reparse tags:
			// ModeIrregular (Go 1.23+; the pre-1.23 MOUNT_POINT->Symlink
			// rule is gone). S_IFMT stays 0 for surrogate-only entries
			// (perm bits alone) so S_IS* probes all fail, matching Go's
			// ModeType==Irregular.
			m |= S_IIRREGULAR;
			break;
		}
	}
	st->st_mode = m;
	st->st_size = size;
	st->st_nlink = 1;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_dev = 0;
	st->st_rdev = 0;
	st->st_ino = 0;
	st->st_attr = attrs;
	st->st_reparse_tag = reparseTag;
	filetimeToTimespec(ctime, &st->st_ctim);
	filetimeToTimespec(atime, &st->st_atim);
	filetimeToTimespec(mtime, &st->st_mtim);
}

void fillStatFromFindData(struct stat* st, const WIN32_FIND_DATAW& fd) {
	long long size =
	    (static_cast<long long>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
	unsigned long tag =
	    (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
	        ? fd.dwReserved0
	        : 0;
	fillStatFromAttrs(st, fd.dwFileAttributes, tag, size, fd.ftCreationTime,
	                  fd.ftLastAccessTime, fd.ftLastWriteTime,
	                  FILE_TYPE_DISK);
}

bool statHandle(struct stat* st, HANDLE h) {
	DWORD ft = GetFileType(h);
	if (ft == FILE_TYPE_PIPE || ft == FILE_TYPE_CHAR) {
		fillStatFromAttrs(st, 0, 0, 0, FILETIME{}, FILETIME{}, FILETIME{},
		                  ft);
		return true;
	}
	BY_HANDLE_FILE_INFORMATION bi{};
	if (!GetFileInformationByHandle(h, &bi)) {
		return false;
	}
	unsigned long tag = 0;
	if (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
		FILE_ATTRIBUTE_TAG_INFO ti{};
		if (GetFileInformationByHandleEx(h, FileAttributeTagInfo, &ti,
		                               sizeof(ti))) {
			tag = ti.ReparseTag;
		}
	}
	long long size =
	    (static_cast<long long>(bi.nFileSizeHigh) << 32) | bi.nFileSizeLow;
	fillStatFromAttrs(st, bi.dwFileAttributes, tag, size, bi.ftCreationTime,
	                  bi.ftLastAccessTime, bi.ftLastWriteTime, ft);
	st->st_dev = bi.dwVolumeSerialNumber;
	st->st_ino =
	    (static_cast<unsigned long long>(bi.nFileIndexHigh) << 32) |
	    bi.nFileIndexLow;
	st->st_nlink = static_cast<short>(bi.nNumberOfLinks);
	return true;
}

int statImpl(const char* path, struct stat* st, bool followSurrogates) {
	if (!path || !*path) {
		return w32::setErrnoPair(ENOENT, ERROR_PATH_NOT_FOUND);
	}
	std::wstring wp = w32::widen(path);
	// Fast path: GetFileAttributesEx (Go stat()).
	WIN32_FILE_ATTRIBUTE_DATA fad{};
	BOOL ok = GetFileAttributesExW(wp.c_str(), GetFileExInfoStandard, &fad);
	DWORD err = ok ? 0 : GetLastError();
	if (!ok && err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND &&
	    err != ERROR_BAD_NETPATH && err == ERROR_SHARING_VIOLATION) {
		// Sharing violation (pagefile.sys etc.): FindFirstFile fallback.
		WIN32_FIND_DATAW fd{};
		HANDLE sh = FindFirstFileW(wp.c_str(), &fd);
		if (sh == INVALID_HANDLE_VALUE) {
			return w32::setErrFromWin32(GetLastError());
		}
		FindClose(sh);
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
			fillStatFromFindData(st, fd);
			return 0;
		}
	}
	if (ok && !(fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
		WIN32_FIND_DATAW fd{};
		fd.dwFileAttributes = fad.dwFileAttributes;
		fd.dwReserved0 = 0;
		fd.nFileSizeHigh = fad.nFileSizeHigh;
		fd.nFileSizeLow = fad.nFileSizeLow;
		fd.ftCreationTime = fad.ftCreationTime;
		fd.ftLastAccessTime = fad.ftLastAccessTime;
		fd.ftLastWriteTime = fad.ftLastWriteTime;
		fillStatFromFindData(st, fd);
		return 0;
	}
	if (!ok) {
		// err is not a sharing violation and attrs path failed.
		// ERROR_ACCESS_DENIED paths still proceed to the open-based route
		// only when reparse flagged, which we cannot see — report error.
		if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND &&
		    err != ERROR_BAD_NETPATH && err != ERROR_ACCESS_DENIED &&
		    err != ERROR_DIRECTORY) {
			// Try to learn whether it's a reparse point anyway.
		}
		return w32::setErrFromWin32(err);
	}
	// Reparse point: open with OPEN_REPARSE_POINT and check the tag.
	DWORD flags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
	HANDLE h = CreateFileW(wp.c_str(), 0, 0, nullptr, OPEN_EXISTING, flags,
	                       nullptr);
	if (h == INVALID_HANDLE_VALUE &&
	    GetLastError() == ERROR_INVALID_PARAMETER) {
		// Console handles like \\.\con need GENERIC_READ.
		h = CreateFileW(wp.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
		                flags, nullptr);
	}
	if (h == INVALID_HANDLE_VALUE) {
		return w32::setErrFromWin32(GetLastError());
	}
	if (!statHandle(st, h)) {
		DWORD e = GetLastError();
		CloseHandle(h);
		return w32::setErrFromWin32(e);
	}
	bool surrogate =
	    (st->st_attr & FILE_ATTRIBUTE_REPARSE_POINT) &&
	    (st->st_reparse_tag & 0x20000000);
	if (followSurrogates && surrogate) {
		CloseHandle(h);
		h = CreateFileW(wp.c_str(), 0, 0, nullptr, OPEN_EXISTING,
		                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (h == INVALID_HANDLE_VALUE) {
			return w32::setErrFromWin32(GetLastError());
		}
		bool ok2 = statHandle(st, h);
		DWORD e = GetLastError();
		CloseHandle(h);
		if (!ok2) {
			return w32::setErrFromWin32(e);
		}
		return 0;
	}
	CloseHandle(h);
	return 0;
}

} // namespace

int stat(const char* path, struct stat* st) {
	return statImpl(path, st, true);
}

int lstat(const char* path, struct stat* st) {
	bool follow = false;
	if (path && *path) {
		size_t n = strlen(path);
		char c = path[n - 1];
		follow = (c == '/' || c == '\\');
	}
	return statImpl(path, st, follow);
}

int fstat(int fd, struct stat* st) {
	HANDLE h = w32::handleFromFd(fd);
	if (h == INVALID_HANDLE_VALUE) {
		return w32::setErrnoPair(EBADF, ERROR_INVALID_HANDLE);
	}
	if (!statHandle(st, h)) {
		return w32::setErrFromWin32(GetLastError());
	}
	return 0;
}

int fsync(int fd) {
	HANDLE h = w32::handleFromFd(fd);
	if (h == INVALID_HANDLE_VALUE || !FlushFileBuffers(h)) {
		return w32::setErrFromWin32(GetLastError());
	}
	return 0;
}

int ftruncate(int fd, long long size) {
	long long cur = _lseeki64(fd, 0, SEEK_CUR);
	if (_lseeki64(fd, size, SEEK_SET) < 0) {
		return w32::setErrnoPair(EINVAL, ERROR_INVALID_PARAMETER);
	}
	BOOL ok = SetEndOfFile(w32::handleFromFd(fd));
	_lseeki64(fd, cur, SEEK_SET);
	if (!ok) {
		return w32::setErrFromWin32(GetLastError());
	}
	return 0;
}

long long w32::fileLength(int fd) {
	LARGE_INTEGER sz{};
	if (!GetFileSizeEx(w32::handleFromFd(fd), &sz)) {
		w32::setErrFromWin32(GetLastError());
		return -1;
	}
	return sz.QuadPart;
}

// --- directory entries ----------------------------------------------------

struct DIR {
	HANDLE h = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATAW data{};
	bool firstPending = false;
	bool done = false;
	int err = 0;
	long tell = 0;
	std::wstring pattern;
	struct dirent entry{};
};

static unsigned char direntTypeFromAttrs(unsigned long attrs,
                                         unsigned long tag) {
	if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
		if (tag == IO_REPARSE_TAG_SYMLINK || (tag & 0x20000000)) {
			return DT_LNK;
		}
		if (tag == IO_REPARSE_TAG_AF_UNIX) return DT_SOCK;
		return DT_UNKNOWN;
	}
	if (attrs & FILE_ATTRIBUTE_DIRECTORY) return DT_DIR;
	if (attrs & FILE_ATTRIBUTE_DEVICE) return DT_CHR;
	return DT_REG;
}

DIR* opendir(const char* name) {
	if (!name || !*name) {
		w32::setErrnoPair(ENOENT, ERROR_PATH_NOT_FOUND);
		return nullptr;
	}
	std::wstring w = w32::widen(name);
	// Trailing separator: strip then append "\*".
	while (!w.empty() && (w.back() == L'\\' || w.back() == L'/')) {
		w.pop_back();
	}
	std::wstring pat = w + L"\\*";
	auto* d = new (std::nothrow) DIR();
	if (!d) {
		errno = ENOMEM;
		return nullptr;
	}
	d->pattern = std::move(pat);
	d->h = FindFirstFileExW(d->pattern.c_str(), FindExInfoBasic, &d->data,
	                        FindExSearchNameMatch, nullptr, 0);
	if (d->h == INVALID_HANDLE_VALUE) {
		DWORD e = GetLastError();
		delete d;
		w32::setErrFromWin32(e == ERROR_FILE_NOT_FOUND ? ERROR_PATH_NOT_FOUND
		                                               : e);
		return nullptr;
	}
	d->firstPending = true;
	return d;
}

DIR* fdopendir(int fd) {
	HANDLE h = w32::handleFromFd(fd);
	if (h == INVALID_HANDLE_VALUE) {
		w32::setErrnoPair(EBADF, ERROR_INVALID_HANDLE);
		return nullptr;
	}
	wchar_t buf[4096];
	DWORD n = GetFinalPathNameByHandleW(h, buf, 4096,
	                                  FILE_NAME_NORMALIZED |
	                                      VOLUME_NAME_DOS);
	if (n == 0 || n >= 4096) {
		w32::setErrFromWin32(n == 0 ? GetLastError() : ERROR_BUFFER_OVERFLOW);
		return nullptr;
	}
	std::string path = w32::fromWin32Path(std::wstring_view{buf, n});
	return opendir(path.c_str());
}

struct dirent* readdir(DIR* d) {
	for (;;) {
		if (d->done) {
			return nullptr;
		}
		if (d->firstPending) {
			d->firstPending = false;
		} else if (!FindNextFileW(d->h, &d->data)) {
			DWORD e = GetLastError();
			d->done = true;
			d->err = (e == ERROR_NO_MORE_FILES) ? 0 : w32::errnoFromWin32(e);
			if (d->err) errno = d->err;
			return nullptr;
		}
		std::string nm = w32::narrow(d->data.cFileName);
		if (nm == "." || nm == "..") continue;
		strncpy(d->entry.d_name, nm.c_str(), sizeof(d->entry.d_name) - 1);
		d->entry.d_name[sizeof(d->entry.d_name) - 1] = '\0';
		unsigned long tag =
		    (d->data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		        ? d->data.dwReserved0
		        : 0;
		d->entry.d_type =
		    direntTypeFromAttrs(d->data.dwFileAttributes, tag);
		d->entry.d_ino = 0;
		d->entry.d_reclen = 0;
		d->tell++;
		return &d->entry;
	}
}

void rewinddir(DIR* d) {
	if (d->h != INVALID_HANDLE_VALUE) {
		FindClose(d->h);
	}
	d->h = FindFirstFileExW(d->pattern.c_str(), FindExInfoBasic, &d->data,
	                        FindExSearchNameMatch, nullptr, 0);
	d->firstPending = (d->h != INVALID_HANDLE_VALUE);
	d->done = (d->h == INVALID_HANDLE_VALUE);
	d->err = 0;
	d->tell = 0;
}

int closedir(DIR* d) {
	if (!d) {
		errno = EBADF;
		return -1;
	}
	if (d->h != INVALID_HANDLE_VALUE) {
		FindClose(d->h);
	}
	delete d;
	return 0;
}

long telldir(DIR* d) { return d ? d->tell : -1; }

void seekdir(DIR* d, long loc) {
	// Portable-ish implementation: rewind and re-read loc entries.
	if (!d) return;
	rewinddir(d);
	while (loc-- > 0 && readdir(d)) {
	}
}

// --- path ops --------------------------------------------------------------

int access(const char* path, int mode) {
	std::wstring w = w32::widen(path);
	DWORD attrs = GetFileAttributesW(w.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES) {
		return w32::setErrFromWin32(GetLastError());
	}
	if ((mode & W_OK) && (attrs & FILE_ATTRIBUTE_READONLY)) {
		return w32::setErrnoPair(EACCES, ERROR_ACCESS_DENIED);
	}
	return 0;
}

int chmod(const char* path, int mode) {
	std::wstring w = w32::widen(path);
	DWORD attrs = GetFileAttributesW(w.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES) {
		return w32::setErrFromWin32(GetLastError());
	}
	if (mode & _S_IWRITE) {
		attrs &= ~FILE_ATTRIBUTE_READONLY;
	} else {
		attrs |= FILE_ATTRIBUTE_READONLY;
	}
	if (!SetFileAttributesW(w.c_str(), attrs)) {
		return w32::setErrFromWin32(GetLastError());
	}
	return 0;
}

int mkdir(const char* path, int mode) {
	(void)mode; // Go ignores perm bits on Windows too (ACLs govern).
	if (!CreateDirectoryW(w32::widen(path).c_str(), nullptr)) {
		return w32::setErrFromWin32(GetLastError());
	}
	return 0;
}

int rmdir(const char* path) {
	if (!RemoveDirectoryW(w32::widen(path).c_str())) {
		return w32::setErrFromWin32(GetLastError());
	}
	return 0;
}

int rmdir_(const char* path) { return rmdir(path); }

int unlink(const char* path) {
	// os.Remove file path on Windows: DeleteFile; on ACCESS_DENIED clear the
	// readonly attribute and retry once (Go removeall does the same).
	std::wstring w = w32::widen(path);
	if (DeleteFileW(w.c_str())) {
		return 0;
	}
	DWORD e = GetLastError();
	if (e == ERROR_ACCESS_DENIED) {
		DWORD attrs = GetFileAttributesW(w.c_str());
		if (attrs != INVALID_FILE_ATTRIBUTES &&
		    (attrs & FILE_ATTRIBUTE_READONLY)) {
			SetFileAttributesW(w.c_str(),
			                 attrs & ~FILE_ATTRIBUTE_READONLY);
			if (DeleteFileW(w.c_str())) {
				return 0;
			}
			e = GetLastError();
		}
	}
	return w32::setErrFromWin32(e);
}

int rename(const char* oldp, const char* newp) {
	// Go syscall.Rename -> MoveFileEx(REPLACE_EXISTING|COPY_ALLOWED).
	if (!MoveFileExW(w32::widen(oldp).c_str(), w32::widen(newp).c_str(),
	                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
		return w32::setErrFromWin32(GetLastError());
	}
	return 0;
}

int symlink(const char* target, const char* linkpath) {
	// os.Symlink: stat target to pick SYMBOLIC_LINK_FLAG_DIRECTORY.
	std::wstring wlink = w32::widen(linkpath);
	std::wstring wtgt = w32::widen(target);
	DWORD flags = 0x2 /*SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE*/;
	struct stat st{};
	// If target is relative, resolve against the link's directory like Go
	// does (os.Stat on linkdir + target).
	std::string abs = target;
	bool rooted = abs.size() >= 2 &&
	              ((abs[0] >= 'A' && abs[0] <= 'Z') ||
	               (abs[0] >= 'a' && abs[0] <= 'z')) &&
	              abs[1] == ':';
	if (!rooted && abs.rfind("/", 0) != 0 && abs.rfind("\\", 0) != 0) {
		std::string lp = linkpath;
		size_t slash = lp.find_last_of("/\\");
		if (slash != std::string::npos) {
			abs = lp.substr(0, slash + 1) + abs;
		}
	}
	if (stat(abs.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
		flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
	}
	if (!CreateSymbolicLinkW(wlink.c_str(), wtgt.c_str(), flags)) {
		return w32::setErrFromWin32(GetLastError());
	}
	return 0;
}

ssize_t readlink(const char* path, char* buf, size_t bufsize) {
	// Port of syscall.Readlink: open reparse point, read reparse data.
	std::wstring w = w32::widen(path);
	DWORD flags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
	HANDLE h = CreateFileW(w.c_str(), 0, 0, nullptr, OPEN_EXISTING, flags,
	                       nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return w32::setErrFromWin32(GetLastError());
	}
	BYTE rdb[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
	DWORD bytes = 0;
	if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, rdb,
	                     sizeof(rdb), &bytes, nullptr)) {
		DWORD e = GetLastError();
		CloseHandle(h);
		return w32::setErrFromWin32(e);
	}
	CloseHandle(h);
	auto* rdbuf = reinterpret_cast<w32ReparseDataBuffer*>(rdb);
	const wchar_t* name = nullptr;
	ULONG nameLen = 0;
	if (rdbuf->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
		name = rdbuf->SymbolicLinkReparseBuffer.PathBuffer +
		       rdbuf->SymbolicLinkReparseBuffer.SubstituteNameOffset /
		           sizeof(wchar_t);
		nameLen = rdbuf->SymbolicLinkReparseBuffer.SubstituteNameLength /
		          sizeof(wchar_t);
	} else if (rdbuf->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
		name = rdbuf->MountPointReparseBuffer.PathBuffer +
		       rdbuf->MountPointReparseBuffer.SubstituteNameOffset /
		           sizeof(wchar_t);
		nameLen = rdbuf->MountPointReparseBuffer.SubstituteNameLength /
		          sizeof(wchar_t);
	} else {
		return w32::setErrnoPair(EINVAL, ERROR_NOT_A_REPARSE_POINT);
	}
	// SubstituteName: \??\C:\x or \??\UNC\server\share -> C:\x / \\server\share
	std::wstring out{name, nameLen};
	if (out.rfind(L"\\??\\UNC\\", 0) == 0) {
		out = L"\\\\" + out.substr(8);
	} else if (out.rfind(L"\\??\\", 0) == 0) {
		out = out.substr(4);
	} else if (out.rfind(L"\\\\?\\", 0) == 0) {
		out = L"\\\\" + out.substr(4);
	}
	std::string u8 = w32::narrow(out);
	if (u8.size() > bufsize) {
		return w32::setErrnoPair(EINVAL, ERROR_INSUFFICIENT_BUFFER);
	}
	memcpy(buf, u8.data(), u8.size());
	return static_cast<ssize_t>(u8.size());
}

// realpath — nativepath/realpath_windows.go: open metadata handle,
// GetFinalPathNameByHandle(VOLUME_NAME_DOS), strip \\?\, UNC -> \\.
char* realpath(const char* path, char* resolved) {
	if (!path || !*path) {
		w32::setErrnoPair(ENOENT, ERROR_PATH_NOT_FOUND);
		return nullptr;
	}
	// openMetadata (nativepath/realpath_windows.go): FILE_FLAG_BACKUP_
	// SEMANTICS opens dirs too; winPath adds the \\?\ prefix at >=248
	// chars (what os.Open's fixLongPath does for the >=248 branch).
	std::wstring w = w32::winPath(path);
	HANDLE h = CreateFileW(w.c_str(), 0, 0, nullptr, OPEN_EXISTING,
	                       FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (h == INVALID_HANDLE_VALUE &&
	    GetLastError() == ERROR_INVALID_PARAMETER) {
		h = CreateFileW(w.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
		                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	}
	if (h == INVALID_HANDLE_VALUE) {
		w32::setErrFromWin32(GetLastError());
		return nullptr;
	}
	DWORD cap = 256;
	std::wstring buf;
	for (;;) {
		buf.resize(cap);
		DWORD n = GetFinalPathNameByHandleW(h, buf.data(), cap,
		                                    FILE_NAME_NORMALIZED |
		                                        VOLUME_NAME_DOS);
		if (n == 0) {
			DWORD e = GetLastError();
			CloseHandle(h);
			w32::setErrFromWin32(e);
			return nullptr;
		}
		if (n < cap) {
			buf.resize(n);
			break;
		}
		cap = n + 1;
	}
	CloseHandle(h);
	std::string out = w32::fromWin32Path(buf);
	if (resolved) {
		memcpy(resolved, out.data(), out.size() + 1);
		return resolved;
	}
	char* r = static_cast<char*>(malloc(out.size() + 1));
	if (!r) {
		errno = ENOMEM;
		return nullptr;
	}
	memcpy(r, out.data(), out.size() + 1);
	return r;
}

int utimensat(int dirfd, const char* path, const struct timespec times[2],
              int flags) {
	(void)dirfd;
	// os.Chtimes: FILE_WRITE_ATTRIBUTES + BACKUP_SEMANTICS (+ OPEN_REPARSE
	// when AT_SYMLINK_NOFOLLOW).
	DWORD attrs = FILE_FLAG_BACKUP_SEMANTICS;
	if (flags & AT_SYMLINK_NOFOLLOW) {
		attrs |= FILE_FLAG_OPEN_REPARSE_POINT;
	}
	HANDLE h = CreateFileW(w32::widen(path).c_str(), FILE_WRITE_ATTRIBUTES,
	                       FILE_SHARE_READ | FILE_SHARE_WRITE |
	                           FILE_SHARE_DELETE,
	                       nullptr, OPEN_EXISTING, attrs, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return w32::setErrFromWin32(GetLastError());
	}
	FILETIME at{}, mt{};
	FILETIME* pa = nullptr;
	FILETIME* pm = nullptr;
	if (times) {
		if (times[0].tv_nsec == UTIME_OMIT) {
			pa = nullptr;
		} else {
			if (times[0].tv_nsec == UTIME_NOW) {
				GetSystemTimeAsFileTime(&at);
			} else {
				timespecToFiletime(times[0], &at);
			}
			pa = &at;
		}
		if (times[1].tv_nsec == UTIME_OMIT) {
			pm = nullptr;
		} else {
			if (times[1].tv_nsec == UTIME_NOW) {
				GetSystemTimeAsFileTime(&mt);
			} else {
				timespecToFiletime(times[1], &mt);
			}
			pm = &mt;
		}
	}
	if (!SetFileTime(h, nullptr, pa, pm)) {
		DWORD e = GetLastError();
		CloseHandle(h);
		return w32::setErrFromWin32(e);
	}
	CloseHandle(h);
	return 0;
}

// --- cwd / env / time -------------------------------------------------------

char* getcwd(char* buf, int size) {
	wchar_t wbuf[4096];
	DWORD n = GetCurrentDirectoryW(4096, wbuf);
	if (n == 0) {
		w32::setErrFromWin32(GetLastError());
		return nullptr;
	}
	std::string out = w32::narrow(std::wstring_view{wbuf, n});
	if ((int)out.size() + 1 > size) {
		errno = ERANGE;
		return nullptr;
	}
	memcpy(buf, out.data(), out.size() + 1);
	return buf;
}

int chdir(const char* path) {
	if (!SetCurrentDirectoryW(w32::widen(path).c_str())) {
		return w32::setErrFromWin32(GetLastError());
	}
	return 0;
}

// getcwdString / realpath stay at global scope (the POSIX spelling call
// sites use); the helpers they call live in namespace w32.

std::string getcwdString() {
	wchar_t wbuf[4096];
	DWORD n = GetCurrentDirectoryW(4096, wbuf);
	if (n == 0) {
		w32::setErrFromWin32(GetLastError());
		return {};
	}
	return w32::narrow(std::wstring_view{wbuf, n});
}

// realpath — string form for nativepath.Realpath callers.
std::pair<std::string, int> realpath(const std::string& path) {
	std::wstring w = w32::winPath(path);
	HANDLE h = CreateFileW(w.c_str(), 0, 0, nullptr, OPEN_EXISTING,
	                       FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (h == INVALID_HANDLE_VALUE &&
	    GetLastError() == ERROR_INVALID_PARAMETER) {
		h = CreateFileW(w.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
		                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	}
	if (h == INVALID_HANDLE_VALUE) {
		unsigned long e = GetLastError();
		w32::setErrFromWin32(e);
		return {"", errno};
	}
	DWORD cap = 256;
	std::wstring buf;
	for (;;) {
		buf.resize(cap);
		DWORD n = GetFinalPathNameByHandleW(h, buf.data(), cap,
		                                    FILE_NAME_NORMALIZED |
		                                        VOLUME_NAME_DOS);
		if (n == 0) {
			DWORD e = GetLastError();
			CloseHandle(h);
			w32::setErrFromWin32(e);
			return {"", errno};
		}
		if (n < cap) {
			buf.resize(n);
			break;
		}
		cap = n + 1;
	}
	CloseHandle(h);
	return {w32::fromWin32Path(buf), 0};
}

pid_t getpid(void) { return static_cast<pid_t>(GetCurrentProcessId()); }

pid_t getppid(void) { return 0; }
uid_t getuid(void) { return 0; }
gid_t getgid(void) { return 0; }

unsigned int sleep(unsigned int seconds) {
	Sleep(static_cast<DWORD>(seconds) * 1000);
	return 0;
}

int usleep(unsigned long usec) {
	// Windows Sleep granularity is ms; round up.
	Sleep(static_cast<DWORD>((usec + 999) / 1000));
	return 0;
}

int nanosleep(const struct timespec* req, struct timespec* rem) {
	(void)rem;
	if (!req) return -1;
	long long ms = req->tv_sec * 1000 + (req->tv_nsec + 999999) / 1000000;
	Sleep(static_cast<DWORD>(ms));
	return 0;
}

int clock_gettime(int clockid, struct timespec* ts) {
	switch (clockid) {
	case CLOCK_REALTIME: {
		FILETIME ft;
		GetSystemTimePreciseAsFileTime(&ft);
		filetimeToTimespec(ft, ts);
		return 0;
	}
	case CLOCK_MONOTONIC: {
		unsigned long long freq = 0, cnt = 0;
		QueryPerformanceFrequency(
		    reinterpret_cast<LARGE_INTEGER*>(&freq));
		QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&cnt));
		ts->tv_sec = static_cast<long>(cnt / freq);
		ts->tv_nsec =
		    static_cast<long>((cnt % freq) * 1000000000ULL / freq);
		return 0;
	}
	case CLOCK_PROCESS_CPUTIME_ID:
	case CLOCK_THREAD_CPUTIME_ID: {
		FILETIME c, e, k, u;
		HANDLE h = clockid == CLOCK_PROCESS_CPUTIME_ID
		               ? GetCurrentProcess()
		               : GetCurrentThread();
		if (!GetProcessTimes(h, &c, &e, &k, &u)) {
			return w32::setErrFromWin32(GetLastError());
		}
		unsigned long long t =
		    ((static_cast<unsigned long long>(k.dwHighDateTime) << 32) |
		     k.dwLowDateTime) +
		    ((static_cast<unsigned long long>(u.dwHighDateTime) << 32) |
		     u.dwLowDateTime);
		ts->tv_sec = static_cast<long>(t / 10000000ULL);
		ts->tv_nsec = static_cast<long>((t % 10000000ULL) * 100ULL);
		return 0;
	}
	default:
		return w32::setErrnoPair(EINVAL, ERROR_INVALID_PARAMETER);
	}
}

int setenv(const char* name, const char* value, int overwrite) {
	if (!overwrite && getenv(name)) return 0;
	return _putenv_s(name, value);
}

int unsetenv(const char* name) { return _putenv_s(name, ""); }

// --- signals ---------------------------------------------------------------

namespace w32 {

namespace {

SigHandler handlerTable[NSIG + 16]{};
std::atomic<unsigned int> alarmEpoch{0};

bool crtSupports(int sig) {
	switch (sig) {
	case SIGINT:
	case SIGTERM:
	case SIGABRT:
	case SIGFPE:
	case SIGILL:
	case SIGSEGV:
		return true;
	default:
		return false;
	}
}

} // namespace

SigHandler signalImpl(int sig, SigHandler handler) {
	if (sig < 0 || sig >= NSIG + 16) {
		errno = EINVAL;
		return SIG_ERR;
	}
	SigHandler old = handlerTable[sig];
	if (crtSupports(sig)) {
		SigHandler r = ::signal(sig, handler);
		if (r == SIG_ERR) return SIG_ERR;
		handlerTable[sig] = handler;
		return old ? old : r;
	}
	handlerTable[sig] = handler;
	return old;
}

int raiseImpl(int sig) {
	if (sig >= 0 && sig < NSIG + 16 && handlerTable[sig] &&
	    handlerTable[sig] != SIG_DFL && handlerTable[sig] != SIG_IGN) {
		handlerTable[sig](sig);
		return 0;
	}
	if (crtSupports(sig)) {
		return ::raise(sig);
	}
	if (handlerTable[sig] == SIG_IGN) return 0;
	// Unhandled non-CRT signal: terminate like a default disposition.
	if (sig == SIGKILL || sig == SIGTERM || sig == SIGINT) {
		_exit(128 + sig);
	}
	return 0;
}

unsigned int alarmImpl(unsigned int seconds) {
	unsigned int epoch = ++alarmEpoch;
	if (seconds == 0) return 0;
	std::thread([epoch, seconds] {
		std::this_thread::sleep_for(std::chrono::seconds(seconds));
		if (alarmEpoch.load() == epoch) {
			raiseImpl(SIGALRM);
		}
	}).detach();
	return 0;
}

const char* strsignalImpl(int sig) {
	switch (sig) {
	case SIGINT: return "interrupt";
	case SIGTERM: return "terminated";
	case SIGKILL: return "killed";
	case SIGALRM: return "alarm clock";
	case SIGSEGV: return "segmentation fault";
	case SIGABRT: return "abort";
	case SIGFPE: return "floating point exception";
	case SIGILL: return "illegal instruction";
	case SIGHUP: return "hangup";
	case SIGQUIT: return "quit";
	case SIGPIPE: return "broken pipe";
	case SIGUSR1: return "user defined signal 1";
	case SIGUSR2: return "user defined signal 2";
	case SIGCHLD: return "child exited";
	case SIGTSTP: return "stopped";
	case SIGCONT: return "continued";
	case SIGBREAK: return "Ctrl-Break";
	default: return "unknown signal";
	}
}

void enableVirtualTerminalProcessing() {
	// enablevtprocessing_windows.go: for each std handle that is a console,
	// OR in ENABLE_VIRTUAL_TERMINAL_PROCESSING.
	for (DWORD fdNum : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
		HANDLE h = GetStdHandle(fdNum);
		if (h == nullptr || h == INVALID_HANDLE_VALUE) continue;
		DWORD mode = 0;
		if (GetConsoleMode(h, &mode)) {
			SetConsoleMode(h,
			               mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
			                   DISABLE_NEWLINE_AUTO_RETURN);
		}
	}
}

void setBinaryStdio() {
	// Go on Windows performs no CRLF translation on stdio; mirror with
	// O_BINARY on fds 0-2.
	_setmode(0, _O_BINARY);
	_setmode(1, _O_BINARY);
	_setmode(2, _O_BINARY);
}

} // namespace w32

int sigaction(int sig, const struct sigaction* act,
              struct sigaction* oldact) {
	if (oldact) {
		memset(oldact, 0, sizeof(*oldact));
	}
	if (!act) return 0;
	if (act->sa_handler) {
		w32::signalImpl(sig, act->sa_handler);
	} else if (act->sa_flags & SA_SIGINFO) {
		// sa_sigaction form unused by the port; degrade to handler-less.
	}
	return 0;
}

int sigemptyset(sigset_t* set) {
	if (set) *set = 0;
	return 0;
}
int sigfillset(sigset_t* set) {
	if (set) *set = ~0ULL;
	return 0;
}
int sigaddset(sigset_t* set, int sig) {
	if (set && sig >= 0 && sig < 64) *set |= (1ULL << sig);
	return 0;
}
int sigdelset(sigset_t* set, int sig) {
	if (set && sig >= 0 && sig < 64) *set &= ~(1ULL << sig);
	return 0;
}
int sigismember(const sigset_t* set, int sig) {
	return (set && sig >= 0 && sig < 64 && (*set & (1ULL << sig))) ? 1 : 0;
}
int sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
	// Single-threaded CRT approximation: process mask is a no-op record.
	static sigset_t cur = 0;
	if (oldset) *oldset = cur;
	if (set) {
		switch (how) {
		case SIG_BLOCK: cur |= *set; break;
		case SIG_UNBLOCK: cur &= ~*set; break;
		case SIG_SETMASK: cur = *set; break;
		}
	}
	return 0;
}
int sigpending(sigset_t* set) {
	if (set) *set = 0;
	return 0;
}

// execvp should never run on Windows paths (call sites spawn instead).
int execvp(const char* file, char* const argv[]) {
	(void)file;
	(void)argv;
	return w32::setErrnoPair(ENOSYS, ERROR_CALL_NOT_IMPLEMENTED);
}

#endif // _WIN32
