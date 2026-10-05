// osutil — osutil.go:1-11 + os_other.go:1-13
//
// Linux equivalents: os.Args → /proc/self/cmdline (NUL-separated argv);
// os.Executable → readlink(/proc/self/exe).

#include <cerrno>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>

#include "internal/osutil/osutil.h"

namespace tsc::osutil {

std::vector<std::string> args() {
    // os.Args — read argv[0..argc) from /proc/self/cmdline.
    std::vector<std::string> out;
    std::ifstream f("/proc/self/cmdline", std::ios::binary);
    if (!f) {
        return out;
    }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    while (pos < data.size()) {
        size_t nul = data.find('\0', pos);
        if (nul == std::string::npos) {
            nul = data.size();
        }
        if (nul > pos) {
            out.emplace_back(data, pos, nul - pos);
        }
        pos = nul + 1;
    }
    return out;
}

std::pair<std::string, std::error_code> executable() {
    // os.Executable — kernel-provided absolute path of the running binary.
    std::vector<char> buf(256);
    for (;;) {
        ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size());
        if (n < 0) {
            return {"", std::error_code(errno, std::generic_category())};
        }
        if (static_cast<size_t>(n) < buf.size()) {
            return {std::string(buf.data(), static_cast<size_t>(n)), std::error_code{}};
        }
        buf.resize(buf.size() * 2);
    }
}

} // namespace tsc::osutil
