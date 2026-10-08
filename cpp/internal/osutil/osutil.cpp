// osutil — osutil.go:1-11 + os_other.go:1-13
//
// Linux equivalents: os.Args → /proc/self/cmdline (NUL-separated argv);
// os.Executable → readlink(/proc/self/exe).
// Windows equivalents: os.Args → GetCommandLineW + CommandLineToArgvW
// (Go's runtime uses the same quoting rules in reverse via
// syscall.EscapeArg); os.Executable → GetModuleFileNameW.

#include <cerrno>
#include <cstring>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include "internal/win32/w32compat.h"
#include <shellapi.h>
#else
#include <fstream>
#include <unistd.h>
#endif

#include "internal/osutil/osutil.h"

namespace tsc::osutil {

std::vector<std::string> args() {
#ifdef _WIN32
	// os.Args — the process command line parsed with MSVCRT quoting
	// rules, which is exactly what Go's appendEscapeArg produces.
	std::vector<std::string> out;
	int argc = 0;
	LPWSTR* argv =
	    CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == nullptr) {
		return out;
	}
	for (int i = 0; i < argc; i++) {
		out.push_back(w32::narrow(argv[i]));
	}
	LocalFree(argv);
	return out;
#else
	// os.Args — read argv[0..argc) from /proc/self/cmdline.
	std::vector<std::string> out;
	std::ifstream f("/proc/self/cmdline", std::ios::binary);
	if (!f) {
		return out;
	}
	std::string data((std::istreambuf_iterator<char>(f)),
	                 std::istreambuf_iterator<char>());
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
#endif
}

std::pair<std::string, std::error_code> executable() {
#ifdef _WIN32
	// os.Executable — kernel-provided absolute path of the running image.
	std::string p = w32::selfExePath();
	if (p.empty()) {
		return {"", std::error_code(w32::errnoFromWin32(GetLastError()),
		                            std::generic_category())};
	}
	return {p, std::error_code{}};
#else
	// os.Executable — kernel-provided absolute path of the running binary.
	std::vector<char> buf(256);
	for (;;) {
		ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size());
		if (n < 0) {
			return {"", std::error_code(errno, std::generic_category())};
		}
		if (static_cast<size_t>(n) < buf.size()) {
			return {std::string(buf.data(), static_cast<size_t>(n)),
			        std::error_code{}};
		}
		buf.resize(buf.size() * 2);
	}
#endif
}

} // namespace tsc::osutil
