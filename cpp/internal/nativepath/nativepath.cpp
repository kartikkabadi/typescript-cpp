// nativepath — eintr_unix.go, realpath_linux.go, realpath_other.go,
//              symlink_other.go

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include "internal/win32/w32compat.h"
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "internal/nativepath/nativepath.h"

namespace tsc::nativepath {

namespace {

// ignoringEINTR — eintr_unix.go: retry fn() while it fails with EINTR.
// fn returns (value, errno-or-0).
template <class F>
auto ignoringEINTR(F&& fn) -> decltype(fn()) {
    for (;;) {
        auto [v, err] = fn();
        if (err != EINTR) {
            return std::make_pair(v, err);
        }
    }
}

// os.PathError{Op, Path, Err} → errno as std::error_code. (The op/path
// context of os.PathError has no error_code equivalent; errno fidelity is
// what callers need.)
std::error_code pathError(std::string_view /*op*/, std::string_view /*path*/, int err) {
    return std::error_code(err, std::generic_category());
}

#ifndef _WIN32
// filepath.EvalSymlinks fallback — resolves all symlinks like realpath(3).
std::pair<std::string, std::error_code> evalSymlinks(std::string_view path) {
    std::string p(path);
    std::vector<char> buf(4096);
    char* resolved = ::realpath(p.c_str(), buf.data());
    if (resolved == nullptr) {
        return {"", std::error_code(errno, std::generic_category())};
    }
    return {std::string(resolved), std::error_code{}};
}
#endif

#ifdef __linux__

constexpr std::string_view procSelfFD = "/proc/self/fd/";

bool hasProcSelfFD() {
    // sync.OnceValue
    static const bool has = [] {
        struct stat st;
        return ::stat(procSelfFD.data(), &st) == 0;
    }();
    return has;
}

std::pair<std::string, std::error_code> realpathLinux(std::string_view path) {
    if (!hasProcSelfFD()) {
        return evalSymlinks(path);
    }

    std::string p(path);
    auto [fd, openErr] = ignoringEINTR([&]() -> std::pair<int, int> {
        int r = ::open(p.c_str(), O_CLOEXEC | O_PATH, 0);
        return {r, r < 0 ? errno : 0};
    });
    if (fd < 0) {
        return {"", pathError("open", path, openErr)};
    }

    char procBuf[procSelfFD.size() + 20]; // 20 digits is enough for any int64 fd
    std::snprintf(procBuf, sizeof(procBuf), "%s%d", procSelfFD.data(), fd);

    std::vector<char> buf(256);
    for (;;) {
        auto [nn, err] = ignoringEINTR([&]() -> std::pair<int, int> {
            int r = static_cast<int>(::readlink(procBuf, buf.data(), buf.size()));
            return {r, r < 0 ? errno : 0};
        });
        if (nn < 0) {
            ::close(fd);
            return {"", pathError("readlink", path, err)};
        }
        if (static_cast<size_t>(nn) < buf.size()) {
            ::close(fd);
            return {std::string(buf.data(), static_cast<size_t>(nn)), std::error_code{}};
        }
        buf.resize(buf.size() * 2);
    }
}

#endif // __linux__

} // namespace

std::pair<std::string, std::error_code> realpath(std::string_view path) {
#if defined(_WIN32)
    // realpath_windows.go: openMetadata (BACKUP_SEMANTICS) +
    // GetFinalPathNameByHandle(VOLUME_NAME_DOS) + \\?\ prefix strip.
    // Returns the native (backslash) form; callers normalize.
    auto [out, e] = ::realpath(std::string(path));
    if (e != 0) {
        return {"", pathError("CreateFile", path, e)};
    }
    return {std::move(out), std::error_code{}};
#elif defined(__linux__)
    return realpathLinux(path);
#else
    // realpath_other.go (!windows && !linux)
    return evalSymlinks(path);
#endif
}

bool isSymlinkOrReparsePoint(std::string_view path) {
#if defined(_WIN32)
    // symlink_windows.go: GetFileAttributesEx + FILE_ATTRIBUTE_REPARSE_POINT
    // (any reparse tag counts — junctions included).
    std::string p(path);
    struct stat st;
    return ::lstat(p.c_str(), &st) == 0 &&
        (st.st_attr & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    // symlink_other.go (!windows)
    std::string p(path);
    struct stat st;
    return ::lstat(p.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
#endif
}

} // namespace tsc::nativepath
