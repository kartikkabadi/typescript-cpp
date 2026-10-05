#pragma once

// nativepath — eintr_unix.go:1-14, realpath_linux.go:1-70,
//              realpath_other.go:1-9, symlink_other.go:1-10
//
// Linux path-resolution helpers. The O_PATH + /proc/self/fd realpath trick is
// implemented for __linux__; other non-Windows builds fall back to
// filepath.EvalSymlinks-equivalent resolution (realpath_other.go).

#include <string>
#include <system_error>
#include <utility>

namespace tsc::nativepath {

// Realpath resolves `path` to its canonical, fully symlink-resolved path.
// On Linux this is O(1) syscalls via O_PATH + readlink(/proc/self/fd/N);
// otherwise it falls back to per-component resolution.
// Go error return → (path, error_code); error_code{}.message() wraps the
// failing op like os.PathError (see .cpp).
std::pair<std::string, std::error_code> realpath(std::string_view path);

// IsSymlinkOrReparsePoint reports whether path is a symlink (non-Windows).
bool isSymlinkOrReparsePoint(std::string_view path);

} // namespace tsc::nativepath
