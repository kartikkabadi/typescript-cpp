// osvfs.h — port of tsc/internal/vfs/osvfs/os.go: the real OS filesystem
// (Linux/POSIX) implementation of vfs::FS.
#pragma once

#include "internal/vfs/vfs.h"

#include <string>

namespace tsc::vfs::osvfs {

// FS returns the singleton vfs::FS backed by the OS file system.
FS* FS();

// GetGlobalTypingsCacheLocation — os.go:193-206.
std::string GetGlobalTypingsCacheLocation();

} // namespace tsc::vfs::osvfs
