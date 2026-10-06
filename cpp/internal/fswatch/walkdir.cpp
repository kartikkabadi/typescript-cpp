// walkdir.go + walkdir_unix.go + walkdir_dirent_linux.go — port of
// tsc/internal/fswatch's directory walking. Linux fast path uses
// getdents64 so d_type drives isDir without a stat per entry.
// walkdir_dirent_fileno.go (BSDs) and walkdir_dirent_noreclen.go
// (DragonFly) are non-Linux variants — skipped per scope.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // O_DIRECTORY, O_NOFOLLOW
#endif

#include "internal/fswatch/fswatch.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace tsc::fswatch {

namespace {

struct fdGuard {
	int fd;
	~fdGuard() { ::close(fd); }
};

} // namespace

// walkDir — walkdir_unix.go:21-37.
gostd::Error walkDir(const std::string& dir, bool recursive,
                     const walkFn& fn) {
	constexpr int openFlags = O_RDONLY | O_CLOEXEC | O_DIRECTORY |
	    O_NOCTTY | O_NONBLOCK | O_NOFOLLOW;
	int fd = ::open(dir.c_str(), openFlags, 0);
	if (fd < 0) {
		// Fall back to a path-based open when O_DIRECTORY rejects a
		// non-directory: walkDir's contract is to return ENOTDIR.
		if (errno == ENOTDIR) {
			return errnoError(ENOTDIR);
		}
		return errnoError(errno);
	}
	fdGuard guard{fd};

	walkState st;
	st.buf.resize(8192);
	return iterateDir(&st, fd, dir, recursive, fn);
}

// iterateDir reads fd's entries, invokes fn for the dir and each entry,
// and recurses into subdirectories via openat(fd, name). fd is owned by
// the caller; iterateDir does not close it. Sharing fd as the openat
// anchor for children avoids reopening the parent path once for the
// listing and again for each child.
gostd::Error iterateDir(walkState* st, int fd, const std::string& dirname,
                        bool recursive, const walkFn& fn) {
	if (fn != nullptr) {
		if (auto err = fn(dirname, true); err != nullptr) {
			return err;
		}
	}
	auto [entries, err] = readDirEntries(fd, st->buf);
	if (err != nullptr) {
		return err;
	}

	constexpr int childOpenFlags = O_RDONLY | O_CLOEXEC | O_DIRECTORY |
	    O_NOCTTY | O_NONBLOCK | O_NOFOLLOW;
	for (auto& ent : entries) {
		std::string fullPath = dirname + "/" + ent.name;
		bool isDir = ent.typ == DT_DIR;
		if (ent.typ == DT_UNKNOWN) {
			struct stat attrib;
			if (::lstat(fullPath.c_str(), &attrib) != 0) {
				continue;
			}
			isDir = S_ISDIR(attrib.st_mode);
		}
		if (!isDir) {
			if (fn != nullptr) {
				if (auto err = fn(fullPath, false); err != nullptr) {
					return err;
				}
			}
			continue;
		}
		if (!recursive) {
			if (fn != nullptr) {
				if (auto err = fn(fullPath, true); err != nullptr) {
					return err;
				}
			}
			continue;
		}
		int childFD =
		    ::openat(fd, ent.name.c_str(), childOpenFlags, 0);
		if (childFD < 0) {
			int e = errno;
			if (e == EACCES || e == ENOTDIR || e == ENOENT) {
				continue;
			}
			return errnoError(e);
		}
		auto childErr = iterateDir(st, childFD, fullPath, recursive, fn);
		::close(childFD);
		if (childErr != nullptr) {
			return childErr;
		}
	}
	return nullptr;
}

// readDirEntries — walkdir_unix.go:108-145. Reads every entry on fd via
// getdents64, extracting d_type so callers can skip per-entry lstat on
// filesystems that support it. The supplied buf is reused for every
// getdents64 syscall in the loop and may be reused across calls.
std::pair<std::vector<unixDirent>, gostd::Error>
readDirEntries(int fd, std::vector<char>& buf) {
	std::vector<unixDirent> entries;
	for (;;) {
		long n = ::syscall(SYS_getdents64, fd, buf.data(), buf.size());
		if (n < 0) {
			return {{}, errnoError(errno)};
		}
		if (n <= 0) {
			break;
		}
		size_t dataLen = static_cast<size_t>(n);
		size_t off = 0;
		while (dataLen - off > 0) {
			auto* d = reinterpret_cast<const linuxDirent64*>(buf.data() + off);
			uint16_t reclen = reclenOf(d);
			if (reclen == 0 || reclen > dataLen - off) {
				break;
			}
			if (inoOf(d) != 0) {
				constexpr size_t nameOff = offsetof(linuxDirent64, d_name);
				std::string_view nameBytes(buf.data() + off + nameOff,
				                           reclen - nameOff);
				size_t nul = nameBytes.find('\0');
				if (nul != std::string_view::npos) {
					nameBytes = nameBytes.substr(0, nul);
				}
				if (nameBytes != "." && nameBytes != "..") {
					entries.push_back(
					    unixDirent{std::string(nameBytes), d->d_type});
				}
			}
			off += reclen;
		}
	}
	return {entries, nullptr};
}

// walkDirGeneric — walkdir.go:14-23. The portable walkDir implementation. It
// is used as the primary implementation on platforms without a native
// version, and is tested on all platforms.
gostd::Error walkDirGeneric(const std::string& dir, bool recursive,
                            const walkFn& fn) {
	auto [info, err] = osLstat(dir);
	if (err != nullptr) {
		return err;
	}
	if (!info.isDir) {
		return errnoError(ENOTDIR);
	}
	return walkDirGenericVisit(dir, recursive, fn);
}

// walkDirGenericVisit — walkdir.go:25-57.
gostd::Error walkDirGenericVisit(const std::string& dir, bool recursive,
                                 const walkFn& fn) {
	auto [entries, err] = osReadDir(dir);
	if (err != nullptr) {
		if (errIsFsPermission(err) || errIsFsNotExist(err)) {
			return nullptr;
		}
		return err;
	}
	if (fn != nullptr) {
		if (auto err = fn(dir, true); err != nullptr) {
			return err;
		}
	}
	for (auto& e : entries) {
		std::string path = dir + "/" + e.name;
		if (e.isDir) {
			if (recursive) {
				if (auto err =
				        walkDirGenericVisit(path, recursive, fn);
				    err != nullptr) {
					return err;
				}
			} else if (fn != nullptr) {
				if (auto err = fn(path, true); err != nullptr) {
					return err;
				}
			}
		} else if (fn != nullptr) {
			if (auto err = fn(path, false); err != nullptr) {
				return err;
			}
		}
	}
	return nullptr;
}

} // namespace tsc::fswatch
