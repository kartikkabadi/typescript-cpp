// w32proc.cpp — process management for the Go-on-Windows port:
// CreateProcess spawn (fork+execvp analog), waitpid, kill, isProcessAlive
// (isprocessalive_windows.go), LookPath (os/exec windows), RemoveAll
// (os/removeall_windows.go), argv quoting (syscall appendEscapeArg).
#include "internal/win32/w32compat.h"
#ifdef _WIN32

#include <algorithm>
#include <mutex>
#include <unordered_map>

#include <direct.h>
#include <io.h>
#include <process.h>

namespace {

// pid -> HANDLE registry for spawned children (waitpid needs the handle).
std::mutex g_procMu;
std::unordered_map<pid_t, HANDLE> g_procs;

void registerProc(pid_t pid, HANDLE h) {
	std::lock_guard lk(g_procMu);
	g_procs[pid] = h;
}

HANDLE takeProc(pid_t pid) {
	std::lock_guard lk(g_procMu);
	auto it = g_procs.find(pid);
	if (it == g_procs.end()) return INVALID_HANDLE_VALUE;
	HANDLE h = it->second;
	g_procs.erase(it);
	return h;
}

// Map a win32 exit code to a wait-status signal number for crashes.
int exceptionToSignal(DWORD code) {
	switch (code) {
	case 0xC0000005: // ACCESS_VIOLATION
	case 0xC00000FD: // STACK_OVERFLOW
		return SIGSEGV;
	case 0xC000001D:
		return SIGILL;
	case 0x80000003:
		return SIGTRAP;
	case 0xC000013A: // CTRL_C_EXIT
	case 0xC0000131: // CTRL_LOGOFF
		return SIGINT;
	case 0xC0000142: // DLL init failure
	case 0xC0000409: // stack buffer overrun / __fastfail
		return SIGABRT;
	case 0xE0434352: // .NET exception
		return SIGABRT;
	default:
		if (code & 0xC0000000) return SIGABRT;
		return -1;
	}
}

} // namespace

pid_t waitpid(pid_t pid, int* status, int options) {
	if (pid <= 0) {
		errno = ECHILD;
		return -1;
	}
	HANDLE h = takeProc(pid);
	if (h == INVALID_HANDLE_VALUE) {
		errno = ECHILD;
		return -1;
	}
	DWORD wait = (options & WNOHANG) ? 0 : INFINITE;
	DWORD r = WaitForSingleObject(h, wait);
	if (r == WAIT_TIMEOUT) {
		// Still running; put the handle back.
		std::lock_guard lk(g_procMu);
		g_procs[pid] = h;
		return 0;
	}
	if (r == WAIT_FAILED) {
		CloseHandle(h);
		errno = ECHILD;
		return -1;
	}
	DWORD code = 0;
	GetExitCodeProcess(h, &code);
	CloseHandle(h);
	if (status) {
		int sig = exceptionToSignal(code);
		if (sig > 0) {
			*status = sig & 0x7f; // WIFSIGNALED
		} else {
			*status = (code & 0xff) << 8; // WIFEXITED
		}
	}
	return pid;
}

int kill(pid_t pid, int sig) {
	if (sig == 0) {
		return w32::isProcessAlive(pid) ? 0
		                              : w32::setErrnoPair(ESRCH,
		                                                  ERROR_INVALID_PARAMETER);
	}
	// Go os.Process.Kill -> TerminateProcess(1). Signal numbers collapse to
	// termination (the only Signal Go supports on Windows is Kill).
	HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE,
	                       static_cast<DWORD>(pid));
	if (h == nullptr) {
		DWORD e = GetLastError();
		if (e == ERROR_INVALID_PARAMETER) {
			return w32::setErrnoPair(ESRCH, e);
		}
		return w32::setErrnoPair(EPERM, e);
	}
	if (!TerminateProcess(h, 1)) {
		DWORD e = GetLastError();
		CloseHandle(h);
		return w32::setErrFromWin32(e);
	}
	CloseHandle(h);
	return 0;
}

int killpg(pid_t pgid, int sig) { return kill(pgid, sig); }

int w32_kill(pid_t pid, int sig) { return kill(pid, sig); }
int w32_waitpid(pid_t pid, int* status, int options) {
	return waitpid(pid, status, options);
}

namespace w32 {

bool isProcessAlive(pid_t pid) {
	// isprocessalive_windows.go: OpenProcess(SYNCHRONIZE) + wait(0).
	HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
	if (h == nullptr) {
		return false;
	}
	DWORD ret = WaitForSingleObject(h, 0);
	CloseHandle(h);
	return ret == WAIT_TIMEOUT;
}

std::string selfExePath() {
	wchar_t buf[4096];
	DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
	if (n == 0 || n >= 4096) return {};
	return narrow(std::wstring_view{buf, n});
}

std::string executablePath() { return selfExePath(); }

// appendEscapeArg — syscall/exec_windows.go quoting rules.
std::string makeCmdLine(const std::vector<std::string>& argv) {
	std::string b;
	for (size_t ai = 0; ai < argv.size(); ++ai) {
		if (ai) b.push_back(' ');
		const std::string& s = argv[ai];
		if (s.empty()) {
			b += "\"\"";
			continue;
		}
		bool needsBackslash = false, hasSpace = false;
		for (char c : s) {
			switch (c) {
			case '"':
			case '\\': needsBackslash = true; break;
			case ' ':
			case '\t': hasSpace = true; break;
			}
		}
		if (!needsBackslash && !hasSpace) {
			b += s;
			continue;
		}
		if (!needsBackslash) {
			b += '"';
			b += s;
			b += '"';
			continue;
		}
		if (hasSpace) b += '"';
		int slashes = 0;
		for (size_t i = 0; i < s.size(); ++i) {
			char c = s[i];
			switch (c) {
			default:
				slashes = 0;
				b.push_back(c);
				break;
			case '\\':
				slashes++;
				b.push_back(c);
				break;
			case '"': {
				for (; slashes > 0; --slashes) b.push_back('\\');
				b += "\\\"";
				break;
			}
			}
		}
		if (hasSpace) {
			for (; slashes > 0; --slashes) b.push_back('\\');
			b += '"';
		}
	}
	return b;
}

// Go exec.LookPath on Windows: if the name contains a path separator the
// PATHEXT entries are tried on it; otherwise search PATH dirs. Extensions
// from PATHEXT (default .COM;.EXE;.BAT;.CMD).
static bool fileExistsNonDir(const std::wstring& w) {
	DWORD a = GetFileAttributesW(w.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::pair<std::string, int>
lookPath(const std::string& file, const std::vector<std::string>& exts) {
	std::vector<std::string> pathext = exts;
	if (pathext.empty()) {
		const char* pe = getenv("PATHEXT");
		std::string def = pe ? pe : ".COM;.EXE;.BAT;.CMD";
		size_t i = 0;
		while (i < def.size()) {
			size_t j = def.find(';', i);
			pathext.push_back(def.substr(i, j == std::string::npos
			                                     ? std::string::npos
			                                     : j - i));
			if (j == std::string::npos) break;
			i = j + 1;
		}
	}
	// Split dirs from PATH.
	const char* penv = getenv("PATH");
	std::string pathEnv = penv ? penv : "";
	std::vector<std::string> dirs;
	bool hasSep = file.find_first_of("/\\:") != std::string::npos;
	if (hasSep) {
		dirs.push_back("");
	} else {
		size_t i = 0;
		while (i <= pathEnv.size()) {
			size_t j = pathEnv.find(';', i);
			dirs.push_back(pathEnv.substr(i, j == std::string::npos
			                                      ? std::string::npos
			                                      : j - i));
			if (j == std::string::npos) break;
			i = j + 1;
		}
		if (dirs.empty()) dirs.push_back(".");
	}
	std::wstring wfile = widen(file);
	bool hasExt = file.find_last_of('.') != std::string::npos &&
	              file.find_last_of('.') > file.find_last_of("/\\");
	for (const auto& dir : dirs) {
		std::string base =
		    dir.empty() ? file : (dir.back() == '/' || dir.back() == '\\'
		                              ? dir + file
		                              : dir + "\\" + file);
		std::wstring wbase = widen(base);
		if (hasExt) {
			if (fileExistsNonDir(wbase)) return {base, 0};
		} else {
			for (const auto& e : pathext) {
				std::wstring cand = wbase + widen(e);
				if (fileExistsNonDir(cand)) {
					return {base + e, 0};
				}
			}
			if (fileExistsNonDir(wbase)) return {base, 0};
		}
	}
	return {"", ENOENT};
}

pid_t spawnvp(const std::vector<std::string>& argv, const SpawnStdio& stdio,
              const std::string& cwd, const std::vector<std::string>& env) {
	if (argv.empty()) {
		setErrnoPair(EINVAL, ERROR_INVALID_PARAMETER);
		return -1;
	}
	// Resolve argv[0] like Go's exec.LookPath unless already a full path.
	std::vector<std::string> a = argv;
	bool hasSep = a[0].find_first_of("/\\:") != std::string::npos;
	if (!hasSep) {
		auto [resolved, err] = lookPath(a[0], {});
		if (err) {
			errno = err;
			return -1;
		}
		a[0] = resolved;
	} else {
		// Go: a path without an extension still tries PATHEXT suffixes.
		bool hasExt = a[0].find_last_of('.') != std::string::npos &&
		              a[0].find_last_of('.') > a[0].find_last_of("/\\");
		if (!hasExt) {
			auto [resolved, err] = lookPath(a[0], {});
			if (!err) a[0] = resolved;
		}
	}
	std::string cmdline = makeCmdLine(a);
	std::wstring wcmd = widen(cmdline);

	// Stdio handles.
	auto fdToHandle = [](int fd, DWORD stdid) -> HANDLE {
		if (fd == -2) {
			return GetStdHandle(stdid);
		}
		return handleFromFd(fd);
	};
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	HANDLE hIn = fdToHandle(stdio.stdinFd, STD_INPUT_HANDLE);
	HANDLE hOut = fdToHandle(stdio.stdoutFd, STD_OUTPUT_HANDLE);
	HANDLE hErr = stdio.mergeStderrToStdout
	                  ? hOut
	                  : fdToHandle(stdio.stderrFd, STD_ERROR_HANDLE);
	si.hStdInput = hIn;
	si.hStdOutput = hOut;
	si.hStdError = hErr;
	// Make the specific handles inheritable for the child.
	SetHandleInformation(hIn, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	SetHandleInformation(hOut, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	if (hErr != hOut) {
		SetHandleInformation(hErr, HANDLE_FLAG_INHERIT,
		                     HANDLE_FLAG_INHERIT);
	}

	// Environment block: inherit unless env list given.
	std::wstring envBlock;
	void* envPtr = nullptr;
	if (!env.empty()) {
		for (const auto& kv : env) {
			envBlock += widen(kv);
			envBlock.push_back(L'\0');
		}
		envBlock.push_back(L'\0');
		envPtr = envBlock.data();
	}
	std::wstring wcwd = cwd.empty() ? std::wstring{} : widen(cwd);

	PROCESS_INFORMATION pi{};
	// CREATE_UNICODE_ENVIRONMENT required when passing a wchar env block.
	DWORD cflags = envPtr ? CREATE_UNICODE_ENVIRONMENT : 0;
	std::vector<wchar_t> cmdBuf(wcmd.begin(), wcmd.end());
	cmdBuf.push_back(L'\0');
	BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
	                       TRUE /*inherit*/, cflags, envPtr,
	                       wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
	// Restore inheritability.
	SetHandleInformation(hIn, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(hOut, HANDLE_FLAG_INHERIT, 0);
	if (hErr != hOut) SetHandleInformation(hErr, HANDLE_FLAG_INHERIT, 0);
	if (!ok) {
		return setErrFromWin32(GetLastError());
	}
	CloseHandle(pi.hThread);
	registerProc(static_cast<pid_t>(pi.dwProcessId), pi.hProcess);
	return static_cast<pid_t>(pi.dwProcessId);
}

// os.RemoveAll — removeall_windows.go: try delete; on ACCESS_DENIED clear
// readonly and retry; dirs are emptied recursively.
int removeAll(const std::string& path) {
	if (path.empty()) return 0;
	std::wstring w = widen(path);
	// Stat via lstat semantics: don't follow surrogate reparse points —
	// RemoveAll removes the link itself, not the target.
	WIN32_FILE_ATTRIBUTE_DATA fad{};
	if (!GetFileAttributesExW(w.c_str(), GetFileExInfoStandard, &fad)) {
		DWORD e = GetLastError();
		if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) {
			return 0; // absent -> success
		}
		return setErrFromWin32(e);
	}
	bool isDir = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
	             !(fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
	if (!isDir) {
		if (DeleteFileW(w.c_str())) return 0;
		DWORD e = GetLastError();
		if (e == ERROR_ACCESS_DENIED) {
			SetFileAttributesW(w.c_str(),
			                 fad.dwFileAttributes &
			                     ~FILE_ATTRIBUTE_READONLY);
			if (DeleteFileW(w.c_str())) return 0;
			e = GetLastError();
		}
		if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return 0;
		return setErrFromWin32(e);
	}
	// Directory: enumerate children.
	{
		std::wstring pat = w;
		if (!pat.empty() && pat.back() != L'\\') pat += L'\\';
		pat += L'*';
		WIN32_FIND_DATAW fd{};
		HANDLE sh = FindFirstFileW(pat.c_str(), &fd);
		if (sh != INVALID_HANDLE_VALUE) {
			do {
				std::wstring name = fd.cFileName;
				if (name == L"." || name == L"..") continue;
				std::string child = narrow(w) + "\\" + narrow(name);
				removeAll(child);
			} while (FindNextFileW(sh, &fd));
			FindClose(sh);
		}
	}
	if (RemoveDirectoryW(w.c_str())) return 0;
	DWORD e = GetLastError();
	if (e == ERROR_ACCESS_DENIED) {
		SetFileAttributesW(w.c_str(),
		                 fad.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
		if (RemoveDirectoryW(w.c_str())) return 0;
		e = GetLastError();
	}
	if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ||
	    e == ERROR_DIRECTORY) {
		return 0;
	}
	return setErrFromWin32(e);
}

} // namespace w32

#endif // _WIN32
