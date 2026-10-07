// tsc/cmd/tsc/lsp.go — `tsc --lsp` subcommand: run the LSP server over
// stdio. Function-by-function port: runLSP, newParentProcessWatchdog,
// startParentProcessWatchdog, isProcessAlive.

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include "internal/win32/w32compat.h"
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "internal/bundled/bundled.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/lsp/lsp.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/osvfs/osvfs.h"

#include "cmd/tscpp/notify.h"
#include "cmd/tscpp/stdio.h"
#include "cmd/tscpp/sys.h"

using namespace tsc;

namespace {

// ---------------------------------------------------------------------------
// flag.FlagSet("lsp", flag.ContinueOnError) — the five registered flags,
// Go flag-pkg parse semantics (flag.go:1102-1214 failf/parseOne).
// ---------------------------------------------------------------------------

struct lspFlags {
	bool stdio = false;
	std::string pprofDir;
	std::string pipe;
	std::string socket;
	int clientProcessID = 0;
};

constexpr const char* lspUsage =
    "Usage of lsp:\n"
    "  -clientProcessId int\n"
    "    \tuse the given PID for the parent process watchdog\n"
    "  -pipe string\n"
    "    \tuse named pipe for communication\n"
    "  -pprofDir string\n"
    "    \tGenerate pprof CPU/memory profiles to the given directory.\n"
    "  -socket string\n"
    "    \tuse socket for communication\n"
    "  -stdio\n"
    "    \tuse stdio for communication\n";

bool lspFlagBoolValue(const std::string& v, bool* out) {
	if (v == "1" || v == "t" || v == "T" || v == "true" || v == "TRUE" ||
	    v == "True") {
		*out = true;
		return true;
	}
	if (v == "0" || v == "f" || v == "F" || v == "false" || v == "FALSE" ||
	    v == "False") {
		*out = false;
		return true;
	}
	return false;
}

// parseLSPFlags — flag.FlagSet.Parse over the lsp flag set. Errors print
// Go's failf line + usage to stderr; returns false on any parse error
// (including -h/-help, which prints only usage — flag.ErrHelp).
bool parseLSPFlags(const std::vector<std::string>& args, lspFlags* f) {
	auto fail = [](const std::string& msg) {
		std::fprintf(stderr, "%s\n%s", msg.c_str(), lspUsage);
		return false;
	};
	for (size_t i = 0; i < args.size(); i++) {
		const std::string& arg = args[i];
		if (arg.size() < 2 || arg[0] != '-') break; // non-flag: done
		size_t numMinuses = arg[1] == '-' ? 2 : 1;
		std::string name = arg.substr(numMinuses);
		if (name.empty() || name[0] == '-' || name[0] == '=') {
			if (arg == "--") break; // "--" terminates flags
			return fail("bad flag syntax: " + arg);
		}
		std::string value;
		bool hasValue = false;
		if (auto eq = name.find('='); eq != std::string::npos) {
			value = name.substr(eq + 1);
			name = name.substr(0, eq);
			hasValue = true;
		}
		if (name == "h" || name == "help") {
			std::fprintf(stderr, "%s", lspUsage);
			return false; // flag.ErrHelp
		}
		if (name != "stdio" && name != "pprofDir" && name != "pipe" &&
		    name != "socket" && name != "clientProcessId") {
			return fail("flag provided but not defined: -" + name);
		}
		if (name == "stdio") { // bool flag: =value optional
			if (hasValue) {
				if (!lspFlagBoolValue(value, &f->stdio)) {
					return fail("invalid value \"" + value +
					            "\" for flag -stdio: parse error");
				}
			} else {
				f->stdio = true;
			}
			continue;
		}
		// non-bool flags need a value
		if (!hasValue) {
			if (i + 1 < args.size()) {
				value = args[++i];
			} else {
				return fail("flag needs an argument: -" + name);
			}
		}
		if (name == "pprofDir") f->pprofDir = value;
		else if (name == "pipe") f->pipe = value;
		else if (name == "socket") f->socket = value;
		else { // clientProcessId
			char* end = nullptr;
			errno = 0;
			long v = std::strtol(value.c_str(), &end, 10);
			if (errno != 0 || end == value.c_str() || *end != '\0') {
				return fail("invalid value \"" + value +
				            "\" for flag -clientProcessId: parse error");
			}
			f->clientProcessID = (int)v;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// NpmInstall — lsp.go:58: exec.Command("npm", args...) in cwd; cmd.Output()
// captures stdout.
// ---------------------------------------------------------------------------

std::pair<std::vector<uint8_t>, gostd::Error>
npmInstall(const std::string& cwd, const std::vector<std::string>& args) {
	int outPipe[2];
	if (::pipe(outPipe) != 0) {
#ifdef _WIN32
		return {{}, gostd::newError(w32::errnoText(errno))};
#else
		return {{}, gostd::newError(std::strerror(errno))};
#endif
	}
#ifdef _WIN32
	// exec.Command("npm", args...) + cmd.Output: Go's LookPath resolves npm
	// via PATHEXT (npm.cmd on Windows). spawnvp mirrors LookPath; a resolved
	// .cmd can't be run by CreateProcess, so this fails like Go's post-1.21
	// refusal to exec batch files directly — documented in WINDOWS_PARITY.md.
	w32::SpawnStdio io;
	io.stdoutFd = outPipe[1];
	io.stderrFd = -2;
	io.stdinFd = -2;
	std::vector<std::string> argv{"npm"};
	argv.insert(argv.end(), args.begin(), args.end());
	pid_t pid = w32::spawnvp(argv, io, cwd);
#else
	pid_t pid = ::fork();
#endif
	if (pid < 0) {
		::close(outPipe[0]);
		::close(outPipe[1]);
		return {{}, gostd::newError(
#ifdef _WIN32
		                 w32::errnoText(errno)
#else
		                 std::strerror(errno)
#endif
		             )};
	}
#ifndef _WIN32
	if (pid == 0) {
		::chdir(cwd.c_str());
		::dup2(outPipe[1], STDOUT_FILENO);
		::close(outPipe[0]);
		::close(outPipe[1]);
		std::vector<char*> argv;
		argv.push_back(const_cast<char*>("npm"));
		for (auto& a : const_cast<std::vector<std::string>&>(args)) {
			argv.push_back(a.data());
		}
		argv.push_back(nullptr);
		::execvp("npm", argv.data());
		_exit(127);
	}
#endif
	::close(outPipe[1]);
	std::vector<uint8_t> out;
	char buf[8192];
	for (;;) {
		auto n = ::read(outPipe[0], buf, sizeof(buf));
		if (n <= 0) break;
		out.insert(out.end(), buf, buf + n);
	}
	::close(outPipe[0]);
	int status = 0;
	::waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		// exec.Command(...).Output() error — ExitError string form is
		// "exit status N"; signal death is "signal: <name>".
		if (WIFSIGNALED(status)) {
			return {out, gostd::newError(
			                 "signal: " +
			                 std::string(strsignal(WTERMSIG(status))))};
		}
		return {out,
		        gostd::newError("exit status " +
		                        std::to_string(WEXITSTATUS(status)))};
	}
	return {out, nullptr};
}

// ---------------------------------------------------------------------------
// isProcessAlive — isprocessalive_unix.go:15. kill(pid, 0): nil or EPERM =
// alive.
// ---------------------------------------------------------------------------

bool isProcessAlive(int pid) {
	if (::kill(pid, 0) == 0) return true;
	return errno == EPERM;
}  // kill(pid, 0) is presence-probe on both platforms (w32::isProcessAlive)

// ---------------------------------------------------------------------------
// startParentProcessWatchdog — lsp.go:95. Polls the parent every 5s; when it
// dies, prints the message and stops the ctx.
// ---------------------------------------------------------------------------

void startParentProcessWatchdog(gostd::Context ctx,
                                gostd::CancelFunc stop, int parentPID) {
	if (parentPID <= 0) return;
	std::thread([ctx, stop = std::move(stop), parentPID] {
		while (true) {
			for (int i = 0; i < 50; i++) {
				if (!ctx) return;
				if (gostd::ctxErr(ctx)) return; // <-ctx.Done()
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			if (!isProcessAlive(parentPID)) {
				std::fprintf(
				    stderr,
				    "Parent process %d has exited, shutting down.\n",
				    parentPID);
				stop();
				return;
			}
		}
	}).detach();
}

// newParentProcessWatchdog — lsp.go:76. Unix: processAliveSupported.
std::function<void(int)>
newParentProcessWatchdog(gostd::Context ctx, gostd::CancelFunc stop,
                         int clientProcessID) {
	if (clientProcessID > 0) {
		startParentProcessWatchdog(ctx, stop, clientProcessID);
		return nullptr;
	}
	return [ctx, stop](int parentPID) {
		startParentProcessWatchdog(ctx, stop, parentPID);
	};
}

} // namespace

// runLSP — lsp.go:20.
int runLSP(const std::vector<std::string>& args) {
	lspFlags flags;
	if (!parseLSPFlags(args, &flags)) {
		return 2;
	}
	if (!flags.stdio) {
		std::fprintf(stderr, "only stdio is supported\n");
		return 1;
	}
	if (!flags.pprofDir.empty()) {
		std::fprintf(stderr, "pprof profiles will be written to: %s\n",
		             flags.pprofDir.c_str());
		// pprof.BeginProfiling — Go runtime profiling has no C++
		// equivalent; the directory + notice are produced, the profiles
		// themselves are a Go-runtime artifact.
		::mkdir(flags.pprofDir.c_str(), 0755);
	}

	auto fs = bundled::WrapFS(
	    std::shared_ptr<vfs::FS>(vfs::osvfs::FS(), [](vfs::FS*) {}));
	auto defaultLibraryPath = bundled::LibPath();
	auto typingsLocation = vfs::osvfs::GetGlobalTypingsCacheLocation();

	auto [ctx, stop] = tsc::cmd_notify::signalNotifyContext();

	static tsc::cmd_stdio::stdinReader stdinR;
	static tsc::cmd_stdio::stdoutWriter stdoutW;
	static tsc::cmd_stdio::stderrWriter stderrW;

	lsp::ServerOptions opts{
	    .In = lsp::ToReader(&stdinR),
	    .Out = lsp::ToWriter(&stdoutW),
	    .Err = &stderrW,
	    .Cwd = {},
	    .FS = fs,
	    .DefaultLibraryPath = defaultLibraryPath,
	    .TypingsLocation = typingsLocation,
	    .ParseCache = nullptr,
	    .NpmInstall = [](const std::string& cwd,
	                     const std::vector<std::string>& a) {
		    return npmInstall(cwd, a);
	    },
	    .Spawn = [](const std::vector<std::string>& command,
	                const std::string& dir, gostd::io::Writer* stderrW2) {
		    return spawnProcess(command, dir, stderrW2);
	    },
	    .ProgressDelay = std::chrono::milliseconds(250),
	    .SetParentProcessID = newParentProcessWatchdog(ctx, stop,
	                                                   flags.clientProcessID),
	};
	{
		char buf[4096];
		if (::getcwd(buf, sizeof(buf)) == nullptr) {
			std::fprintf(
			    stderr, "Error getting current directory: %s\n",
#ifdef _WIN32
			    w32::errnoText(errno).c_str()
#else
			    std::strerror(errno)
#endif
			);
			return 1;
		}
		opts.Cwd = tspath::normalizePath(buf);
	}

	auto s = lsp::NewServer(opts);
	if (auto err = s->Run(ctx)) {
		std::fprintf(stderr, "%s\n", err->Error().c_str());
		return 1;
	}
	return 0;
}
