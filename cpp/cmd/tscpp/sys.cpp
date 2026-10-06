// tsc/cmd/tsc/sys.go — the os-backed tsc::System implementation used by the
// production tsc binary. Ported function-by-function: osSys fields/methods in
// file order, then spawnProcess + childProcess.
//
//   Writer()             → stdout (Go os.Stdout)
//   ErrorWriter()        → stderr (Go os.Stderr)
//   FS()                 → bundled.WrapFS(osvfs.FS())
//   DefaultLibraryPath() → bundled.LibPath()
//   Now/SinceStart       → system_clock
//   Spawn                → fork/exec with stdin/stdout pipes and an
//                          io.Writer-pumped stderr (Go exec.Cmd)

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "internal/bundled/bundled.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/execute/execute.h"
#include "internal/execute/tsc/compile.h"
#include "internal/gostd/gostd.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/osvfs/osvfs.h"
#include "internal/vfs/vfs.h"

using namespace tsc;

namespace {

// spawnProcess launches a process and adapts its stdio to an io.ReadWriteCloser
// (Read is its stdout, Write is its stdin).
class childProcess final : public gostd::io::ReadWriteCloser,
                           public contentmapper::processExitState {
public:
	pid_t pid = -1;
	int stdinFd = -1;
	int stdoutFd = -1;
	std::thread stderrPump;
	std::atomic<bool> reaped{false};
	int exitStatus = 0;

	childProcess(pid_t pid, int stdinFd, int stdoutFd, std::thread&& pump)
	    : pid(pid), stdinFd(stdinFd), stdoutFd(stdoutFd),
	      stderrPump(std::move(pump)) {}

	~childProcess() override {
		if (stderrPump.joinable()) {
			stderrPump.join();
		}
		if (stdinFd >= 0) {
			::close(stdinFd);
		}
		if (stdoutFd >= 0) {
			::close(stdoutFd);
		}
	}

	// Read — child stdout.
	std::pair<int, gostd::Error> read(std::span<char> buf) override {
		ssize_t n = ::read(stdoutFd, buf.data(), buf.size());
		if (n < 0) {
			return {0, gostd::newError(std::string("read: ") +
			                         std::strerror(errno))};
		}
		if (n == 0) {
			return {0, gostd::io::errEOF};
		}
		return {static_cast<int>(n), nullptr};
	}

	// Write — child stdin.
	std::pair<int, gostd::Error> write(std::string_view data) override {
		ssize_t n = ::write(stdinFd, data.data(), data.size());
		if (n < 0) {
			return {0, gostd::newError(std::string("write: ") +
			                         std::strerror(errno))};
		}
		return {static_cast<int>(n), nullptr};
	}

	// ExitCode — Go (*exec.Cmd).ProcessState.ExitCode(): the process's exit
	// status, or -1 when killed by a signal; (0, false) while still running.
	std::pair<int, bool> ExitCode() override {
		if (!reaped.load()) {
			int status = 0;
			pid_t r = ::waitpid(pid, &status, WNOHANG);
			if (r == 0) {
				return {0, false};
			}
			if (r != pid) {
				return {0, false};
			}
			exitStatus = status;
			reaped.store(true);
		}
		if (WIFEXITED(exitStatus)) {
			return {WEXITSTATUS(exitStatus), true};
		}
		return {-1, true};
	}

	// Close — Go: stdin closes, cmd.WaitDelay (1s) lets the process exit on
	// its own once its stdio is gone, then the process is killed and reaped;
	// an *exec.ExitError from Wait maps to nil.
	gostd::Error close() override {
		if (stdinFd >= 0) {
			::close(stdinFd);
			stdinFd = -1;
		}
		// Go cmd.WaitDelay = time.Second: give the child one second to exit
		// after its pipes close before killing it.
		const auto deadline =
		    std::chrono::steady_clock::now() + std::chrono::seconds(1);
		while (std::chrono::steady_clock::now() < deadline) {
			int status = 0;
			pid_t r = ::waitpid(pid, &status, WNOHANG);
			if (r == pid) {
				exitStatus = status;
				reaped.store(true);
				break;
			}
			if (r < 0 && errno != EINTR) {
				reaped.store(true);
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		if (!reaped.load()) {
			::kill(pid, SIGKILL);
			int status = 0;
			while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
			}
			exitStatus = status;
			reaped.store(true);
		}
		if (stderrPump.joinable()) {
			stderrPump.join();
		}
		return nullptr;
	}
};

std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
spawnProcess(const std::vector<std::string>& command, const std::string& dir,
             gostd::io::Writer* stderr) {
	// Go: exec.Command(command[0], command[1:]...), StdinPipe + StdoutPipe,
	// cmd.Stderr = stderr (streamed into the io.Writer), cmd.Dir = dir.
	int stdinPipe[2] = {-1, -1};
	int stdoutPipe[2] = {-1, -1};
	int stderrPipe[2] = {-1, -1};
	// O_CLOEXEC (Go os/exec uses CLOEXEC pipes): without it, a second spawned
	// process inherits the first's parent-side fds and pins its stdin open,
	// so the first child never sees EOF.
	if (::pipe2(stdinPipe, O_CLOEXEC) != 0 ||
	    ::pipe2(stdoutPipe, O_CLOEXEC) != 0 ||
	    ::pipe2(stderrPipe, O_CLOEXEC) != 0) {
		return {nullptr,
		        gostd::newError(std::string("pipe: ") + std::strerror(errno))};
	}
	// Build argv before fork: malloc in a forked child of a multi-threaded
	// parent can deadlock on a lock held by another thread.
	std::vector<char*> argv;
	argv.reserve(command.size() + 1);
	for (const auto& a : command) {
		argv.push_back(const_cast<char*>(a.c_str()));
	}
	argv.push_back(nullptr);
	pid_t pid = ::fork();
	if (pid < 0) {
		return {nullptr,
		        gostd::newError(std::string("fork: ") + std::strerror(errno))};
	}
	if (pid == 0) {
		// Child: stdin ← pipe read, stdout → pipe write, stderr → pump pipe.
		::dup2(stdinPipe[0], STDIN_FILENO);
		::dup2(stdoutPipe[1], STDOUT_FILENO);
		::dup2(stderrPipe[1], STDERR_FILENO);
		::close(stdinPipe[0]);
		::close(stdinPipe[1]);
		::close(stdoutPipe[0]);
		::close(stdoutPipe[1]);
		::close(stderrPipe[0]);
		::close(stderrPipe[1]);
		if (!dir.empty() && ::chdir(dir.c_str()) != 0) {
			// Go's exec.Cmd.Start reports the chdir error; the closest we can
			// get post-fork is a nonzero exit with the reason on stderr.
			std::string msg =
			    std::string("chdir: ") + std::strerror(errno) + "\n";
			(void)!::write(STDERR_FILENO, msg.data(), msg.size());
			::_exit(1);
		}
		::execvp(argv[0], argv.data());
		::_exit(127);
	}
	::close(stdinPipe[0]);
	::close(stdoutPipe[1]);
	::close(stderrPipe[1]);

	// cmd.Stderr = stderr (io.Writer): pump the child's stderr bytes into it.
	std::thread pump([fd = stderrPipe[0], stderr]() mutable {
		std::array<char, 4096> buf{};
		if (stderr == nullptr) {
			char discard[4096];
			while (::read(fd, discard, sizeof(discard)) > 0) {
			}
			::close(fd);
			return;
		}
		for (;;) {
			ssize_t n = ::read(fd, buf.data(), buf.size());
			if (n <= 0) {
				break;
			}
			stderr->write(std::string_view(buf.data(), n));
		}
		::close(fd);
	});

	return {std::shared_ptr<gostd::io::ReadWriteCloser>(
	            new childProcess(pid, stdinPipe[1], stdoutPipe[0],
	                             std::move(pump))),
	        nullptr};
}

// osSys — sys.go:18. The production System: real stdout/stderr, the bundled
// os filesystem, process clock.
class osSys final : public tsc::execute::tsc::System {
public:
	std::ostream* writer;
	std::shared_ptr<vfs::FS> filesystem;
	std::string defaultLibraryPath;
	std::string cwd;
	vfs::TimePoint start;

	std::ostream* Writer() override { return writer; }
	std::ostream* ErrorWriter() override { return &std::cerr; }
	std::shared_ptr<vfs::FS> fs() override { return filesystem; }
	std::string DefaultLibraryPath() override { return defaultLibraryPath; }
	std::string GetCurrentDirectory() override { return cwd; }
	vfs::TimePoint Now() override { return std::chrono::system_clock::now(); }
	gostd::Duration SinceStart() override {
		return std::chrono::duration_cast<gostd::Duration>(
		    std::chrono::system_clock::now() - start);
	}
	// term.IsTerminal(stdout) / term.GetSize(stdout).
	bool WriteOutputIsTTY() override { return ::isatty(STDOUT_FILENO) != 0; }
	int GetWidthOfTerminal() override {
		struct winsize w {};
		if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0) {
			return 0;
		}
		return w.ws_col;
	}
	// os.LookupEnv.
	std::pair<std::string, bool>
	GetEnvironmentVariable(std::string_view name) override {
		std::string n(name);
		const char* v = std::getenv(n.c_str());
		if (v == nullptr) {
			return {"", false};
		}
		return {v, true};
	}
	// spawnProcess — contentmapper::Spawner.
	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Spawn(const std::vector<std::string>& command, const std::string& dir,
	      gostd::io::Writer* stderr) override {
		return spawnProcess(command, dir, stderr);
	}
};

} // namespace

// newSystem — sys.go:124.
tsc::execute::tsc::System* newSystem() {
	char buf[4096];
	if (::getcwd(buf, sizeof(buf)) == nullptr) {
		std::cerr << "Error getting current directory: "
		          << std::strerror(errno) << "\n";
		std::exit(static_cast<int>(
		    execute::tsc::ExitStatusInvalidProject_OutputsSkipped));
	}
	auto* s = new osSys();
	s->cwd = tspath::normalizePath(buf);
	s->filesystem = bundled::WrapFS(
	    std::shared_ptr<vfs::FS>(vfs::osvfs::FS(), [](vfs::FS*) {}));
	s->defaultLibraryPath = bundled::LibPath();
	s->writer = &std::cout;
	s->start = std::chrono::system_clock::now();
	return s;
}
