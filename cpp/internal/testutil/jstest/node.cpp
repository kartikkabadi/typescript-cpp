// node.cpp — port of tsc/internal/testutil/jstest/node.go.
#include "internal/testutil/jstest/jstest.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#ifdef _WIN32
#include "internal/win32/w32compat.h"
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#include <fstream>
#include <sstream>
#include <vector>

namespace tsc::testutil::jstest {

namespace {

// strerror on POSIX; on Windows our errno text matching Go's zerrors table.
std::string w32ErrText(int e) {
#ifdef _WIN32
	return w32::errnoText(e);
#else
	return std::strerror(e);
#endif
}

// getNodeExeOnce — node.go:19 (sync.OnceValue).
std::string getNodeExeOnce() {
	static const std::string exe = [] {
		const char* exeName = "node";
#ifdef _WIN32
		// Go's exec.LookPath on windows: PATHEXT walk + ';' PATH split.
		auto [found, err] = w32::lookPath(exeName, {});
		return err ? std::string() : found;
#else
		// exec.LookPath: if the name contains a slash, try it
		// directly; otherwise search PATH.
		auto findExecutable = [](const std::string& file) -> std::string {
			struct stat st{};
			return stat(file.c_str(), &st) == 0 &&
			               (st.st_mode & S_IFMT) == S_IFREG &&
			               access(file.c_str(), X_OK) == 0
			           ? file
			           : "";
		};
		if (std::string_view(exeName).find('/') !=
		    std::string_view::npos) {
			return findExecutable(exeName);
		}
		const char* pathEnv = std::getenv("PATH");
		std::string path = pathEnv != nullptr ? pathEnv : "";
		size_t start = 0;
		for (;;) {
			size_t colon = path.find(':', start);
			std::string dir = path.substr(
			    start, colon == std::string::npos
			               ? std::string::npos
			               : colon - start);
			if (dir.empty()) dir = ".";
			if (std::string found = findExecutable(dir + "/" + exeName);
			    !found.empty()) {
				return found;
			}
			if (colon == std::string::npos) break;
			start = colon + 1;
		}
		return std::string();
#endif
	}();
	return exe;
}

// getNodeExe — node.go:89.
std::string getNodeExe(gostd::testing::T* t) {
	if (std::string exe = getNodeExeOnce(); !exe.empty()) {
		return exe;
	}
	t->Fatal({"Node.js not found"});
	return std::string();
}

// writeFile — os.WriteFile with perm 0o644.
gostd::Error writeFile(const std::string& path, std::string_view data) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		return gostd::errorf("open %s: no such file or directory", {path});
	}
	out << data;
	if (!out) {
		return gostd::errorf("write %s: I/O error", {path});
	}
	return nullptr;
}

// combinedOutput — exec.Cmd.CombinedOutput: runs exe with args in dir and
// captures stdout and stderr interleaved through one pipe.
std::pair<std::string, gostd::Error> combinedOutput(
    const std::string& exe, const std::vector<std::string>& args,
    const std::string& dir) {
	int fds[2];
	if (pipe(fds) != 0) {
		return {"", gostd::errorf("pipe: %s",
		                          {w32ErrText(errno)})};
	}
#ifdef _WIN32
	// fork+execvp doesn't exist on Windows: spawn the same image directly.
	w32::SpawnStdio io;
	io.stdoutFd = fds[1];
	io.mergeStderrToStdout = true;
	std::vector<std::string> argv{exe};
	argv.insert(argv.end(), args.begin(), args.end());
	pid_t pid = w32::spawnvp(argv, io, dir);
	close(fds[1]);
	if (pid < 0) {
		close(fds[0]);
		return {"", gostd::errorf("spawn: %s", {w32ErrText(errno)})};
	}
#else
	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return {"", gostd::errorf("fork: %s",
		                          {w32ErrText(errno)})};
	}
	if (pid == 0) {
		// Child: stdout+stderr both go to the pipe.
		dup2(fds[1], STDOUT_FILENO);
		dup2(fds[1], STDERR_FILENO);
		close(fds[0]);
		close(fds[1]);
		if (!dir.empty()) {
			if (chdir(dir.c_str()) != 0) {
				_exit(126);
			}
		}
		std::vector<char*> argv;
		argv.push_back(const_cast<char*>(exe.c_str()));
		for (const auto& a : args) {
			argv.push_back(const_cast<char*>(a.c_str()));
		}
		argv.push_back(nullptr);
		execvp(exe.c_str(), argv.data());
		_exit(127);
	}
	close(fds[1]);
#endif
	std::string output;
	char buf[4096];
	for (;;) {
		ssize_t n = read(fds[0], buf, sizeof buf);
		if (n <= 0) break;
		output.append(buf, (size_t)n);
	}
	close(fds[0]);
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		return {output,
		        gostd::errorf("exit status %d",
		                      {WIFEXITED(status)
		                           ? WEXITSTATUS(status)
		                           : 128 + WTERMSIG(status)})};
	}
	return {output, nullptr};
}

}  // namespace

// SkipIfNoNodeJS — node.go:53.
void SkipIfNoNodeJS(gostd::testing::T* t) {
	t->Helper();
	if (getNodeExeOnce().empty()) {
		t->Skip({"Node.js not found"});
	}
}

// evalNodeScript — node.go:60. Returns the raw JSON output for the
// template layer to unmarshal into T.
std::pair<std::string, gostd::Error> evalNodeScript(
    gostd::testing::T* t, std::string_view script, std::string_view loader,
    const std::string& dir, const std::vector<std::string>& args) {
	t->Helper();
	std::string exe = getNodeExe(t);
	std::string scriptPath = dir + "/script.mjs";
	if (gostd::Error err = writeFile(scriptPath, script); err != nullptr) {
		return {"", err};
	}
	std::string loaderPath = dir + "/loader.mjs";
	if (gostd::Error err = writeFile(loaderPath, loader); err != nullptr) {
		return {"", err};
	}

	std::vector<std::string> execArgs;
	execArgs.reserve(1 + args.size());
	execArgs.push_back(loaderPath);
	execArgs.insert(execArgs.end(), args.begin(), args.end());
	auto [output, err] = combinedOutput(exe, execArgs, dir);
	if (err != nullptr) {
		return {"", gostd::errorf("failed to run node: %w\n%s",
		                          {err, output})};
	}
	return {output, nullptr};
}

}  // namespace tsc::testutil::jstest
