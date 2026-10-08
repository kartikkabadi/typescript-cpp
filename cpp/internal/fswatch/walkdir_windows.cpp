// walkdir_windows.cpp — port of tsc/internal/fswatch/walkdir_windows.go:
// directory walking via FindFirstFile/FindNextFile. Entries that carry
// FILE_ATTRIBUTE_REPARSE_POINT are not recursed into (Go: isDir requires
// DIRECTORY && !REPARSE_POINT).
#ifdef _WIN32

#include "internal/fswatch/fswatch.h"
#include "internal/win32/w32compat.h"

#include <vector>

namespace tsc::fswatch {

// walkDir — walkdir_windows.go:15-71. Iterative DFS over a stack of
// directory paths; the root is validated with GetFileAttributesEx first.
gostd::Error walkDir(const std::string& dir, bool recursive,
                     const walkFn& fn) {
	std::wstring root = w32::widen(dir);
	WIN32_FILE_ATTRIBUTE_DATA rootData{};
	if (!GetFileAttributesExW(root.c_str(), GetFileExInfoStandard,
	                          &rootData)) {
		return gostd::errorf("error opening directory: %w",
		                     {errnoError(w32::errnoFromWin32(GetLastError()))});
	}
	if (!(rootData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
	    (rootData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
		return errnoError(ENOTDIR);
	}
	if (fn) {
		if (auto err = fn(dir, true); err != nullptr) {
			return err;
		}
	}

	std::vector<std::string> stack{dir};
	while (!stack.empty()) {
		std::string path = stack.back();
		stack.pop_back();

		std::wstring spec = w32::widen(path + "\\*");
		WIN32_FIND_DATAW ffd{};
		HANDLE hFind = FindFirstFileW(spec.c_str(), &ffd);
		if (hFind == INVALID_HANDLE_VALUE) {
			if (path == dir) {
				return gostd::errorf(
				    "error opening directory: %w",
				    {errnoError(w32::errnoFromWin32(GetLastError()))});
			}
			continue;
		}
		for (;;) {
			std::string name = w32::narrow(ffd.cFileName);
			if (name != "." && name != "..") {
				std::string fullPath = path + "\\" + name;
				bool isDir =
				    (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				    !(ffd.dwFileAttributes &
				      FILE_ATTRIBUTE_REPARSE_POINT);
				if (fn) {
					if (auto err = fn(fullPath, isDir); err != nullptr) {
						FindClose(hFind);
						return err;
					}
				}
				if (isDir && recursive) {
					stack.push_back(fullPath);
				}
			}
			if (!FindNextFileW(hFind, &ffd)) {
				break;
			}
		}
		FindClose(hFind);
	}
	return nullptr;
}

} // namespace tsc::fswatch

#endif // _WIN32
