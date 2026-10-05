#pragma once

// osutil — osutil.go:1-11 + os_other.go:1-13 (non-Android path)
//
// Platform helpers for argv and the running executable path.

#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace tsc::osutil {

// Args returns the command-line arguments with platform-specific launcher
// details removed. (os_other.go: os.Args)
std::vector<std::string> args();

// Executable returns the path of the current executable, accounting for
// platform-specific launchers. (os_other.go: os.Executable)
std::pair<std::string, std::error_code> executable();

} // namespace tsc::osutil
