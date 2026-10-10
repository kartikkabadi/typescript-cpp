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

#ifdef _WIN32
#include "internal/win32/w32compat.h"
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

// w32ErrText — Go os.PathError/syscall.Errno.Error() text: FormatMessage on
// Windows, strerror elsewhere.
static std::string w32ErrText(int e) {
#ifdef _WIN32
	return w32::errnoText(e);
#else
	return std::strerror(e);
#endif
}

// spawnProcess launches a process and adapts its stdio to an io.ReadWriteCloser
// (Read is its stdout, Write is its stdin).
class childProcess final : public gostd::io::ReadWriteCloser,
                           public contentmapper::processExitState {
public:
	pid_t pid = -1;
	int stdinFd = -1;
	int stdoutFd = -1;
	int stderrFd = -1;
	std::thread stderrPump;
	std::atomic<bool> reaped{false};
	std::atomic<bool> stopPump{false};
	int exitStatus = 0;

	childProcess(pid_t pid, int stdinFd, int stdoutFd, int stderrFd,
	             std::thread&& pump)
	    : pid(pid), stdinFd(stdinFd), stdoutFd(stdoutFd),
	      stderrFd(stderrFd), stderrPump(std::move(pump)) {}

	// stopPumpAndJoin — bounds the pump wait: a descendant that inherited
	// the stderr write end keeps the pipe open past the child's exit, so
	// joining without this can hang forever (Go's cmd.WaitDelay case).
	void stopPumpAndJoin() {
		stopPump.store(true);
		if (stderrFd >= 0) {
			::close(stderrFd);
			stderrFd = -1;
		}
		if (stderrPump.joinable()) {
			stderrPump.join();
		}
	}

	~childProcess() override {
		stopPumpAndJoin();
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
			                         w32ErrText(errno))};
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
			                         w32ErrText(errno))};
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
		stopPumpAndJoin();
		return nullptr;
	}
};

} // namespace

// spawnProcess — sys.go:74. Exported (non-static) for cmd/tscpp/lsp.cpp's
// ServerOptions.Spawn wiring (lsp.go:64); declared in sys.h.
std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
spawnProcess(const std::vector<std::string>& command, const std::string& dir,
             gostd::io::Writer* stderr_) {
	// Go: exec.Command(command[0], command[1:]...), StdinPipe + StdoutPipe,
	// cmd.Stderr = stderr (streamed into the io.Writer), cmd.Dir = dir.
	int stdinPipe[2] = {-1, -1};
	int stdoutPipe[2] = {-1, -1};
	int stderrPipe[2] = {-1, -1};
	// errPipe reports chdir/execvp failures to the parent — Go's
	// exec.Cmd.Start returns them via exactly this CLOEXEC-pipe trick:
	// a successful exec closes the write end (EOF), a failure writes
	// errno before _exit.
	int errPipe[2] = {-1, -1};
	// O_CLOEXEC (Go os/exec uses CLOEXEC pipes): without it, a second spawned
	// process inherits the first's parent-side fds and pins its stdin open,
	// so the first child never sees EOF. Windows: CreatePipe handles are
	// non-inheritable by default (CLOEXEC semantics) and w32::spawnvp only
	// marks the selected stdio handles inheritable in the child.
#if defined(_WIN32) || defined(__APPLE__)
	// Windows: CreatePipe handles are non-inheritable (CLOEXEC
	// semantics). darwin has no pipe2: pipe() + FD_CLOEXEC via fcntl —
	// the same CLOEXEC contract Go's os/exec relies on.
	auto pipeCloexec = [](int fds[2]) {
		if (::pipe(fds) != 0) {
			return -1;
		}
		::fcntl(fds[0], F_SETFD, ::fcntl(fds[0], F_GETFD) | FD_CLOEXEC);
		::fcntl(fds[1], F_SETFD, ::fcntl(fds[1], F_GETFD) | FD_CLOEXEC);
		return 0;
	};
	if (pipeCloexec(stdinPipe) != 0 || pipeCloexec(stdoutPipe) != 0 ||
	    pipeCloexec(stderrPipe) != 0 || pipeCloexec(errPipe) != 0) {
#else
	if (::pipe2(stdinPipe, O_CLOEXEC) != 0 ||
	    ::pipe2(stdoutPipe, O_CLOEXEC) != 0 ||
	    ::pipe2(stderrPipe, O_CLOEXEC) != 0 ||
	    ::pipe2(errPipe, O_CLOEXEC) != 0) {
#endif
		return {nullptr,
		        gostd::newError(std::string("pipe: ") + w32ErrText(errno))};
	}
#ifdef _WIN32
	w32::SpawnStdio io;
	io.stdinFd = stdinPipe[0];
	io.stdoutFd = stdoutPipe[1];
	io.stderrFd = stderrPipe[1];
	pid_t pid = w32::spawnvp(command, io, dir);
	// errPipe only exists for the fork/exec path — close it on Windows.
	::close(errPipe[0]);
	::close(errPipe[1]);
	if (pid < 0) {
		return {nullptr,
		        gostd::newError(std::string("spawn: ") +
		                        w32ErrText(errno))};
	}
#else
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
		        gostd::newError(std::string("fork: ") +
		                        w32ErrText(errno))};
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
		::close(errPipe[0]);
		if (!dir.empty() && ::chdir(dir.c_str()) != 0) {
			int e = errno;
			std::string msg =
			    std::string("chdir: ") + w32ErrText(e) + "\n";
			(void)!::write(STDERR_FILENO, msg.data(), msg.size());
			(void)!::write(errPipe[1], &e, sizeof(e));
			::_exit(1);
		}
		::execvp(argv[0], argv.data());
		{
			int e = errno;
			(void)!::write(errPipe[1], &e, sizeof(e));
		}
		::_exit(127);
	}
#endif // !_WIN32
	::close(stdinPipe[0]);
	::close(stdoutPipe[1]);
	::close(stderrPipe[1]);
#ifndef _WIN32
	// Await the child's exec result (what Go's cmd.Start waits on):
	// CLOEXEC makes a clean exec read as EOF; anything else is errno.
	::close(errPipe[1]);
	int childErr = 0;
	{
		ssize_t n;
		do {
			n = ::read(errPipe[0], &childErr, sizeof(childErr));
		} while (n < 0 && errno == EINTR);
	}
	::close(errPipe[0]);
	if (childErr != 0) {
		int status;
		while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		}
		::close(stdinPipe[1]);
		::close(stdoutPipe[0]);
		::close(stderrPipe[0]);
		return {nullptr,
		        gostd::newError(std::string("spawn: ") +
		                        w32ErrText(childErr))};
	}
#endif

	// cmd.Stderr = stderr (io.Writer): pump the child's stderr bytes into
	// it. poll() + stopPump bounds the wait when a descendant inherits the
	// write end — read() alone could block past the child's exit.
	auto* proc = new childProcess(pid, stdinPipe[1], stdoutPipe[0],
	                              stderrPipe[0], std::thread());
	std::thread pump([fd = stderrPipe[0], stderr_,
	                  stop = &proc->stopPump]() mutable {
#ifndef _WIN32
		std::array<char, 4096> buf{};
		auto drain = [&](auto emit) {
			for (;;) {
				pollfd pfd{fd, POLLIN, 0};
				int pr = ::poll(&pfd, 1, 50);
				if (pr <= 0) {
					if (pr == 0 && stop->load()) {
						return;
					}
					if (pr < 0 && errno == EINTR) {
						continue;
					}
					if (pr < 0) {
						return;
					}
					continue;
				}
				ssize_t n = ::read(fd, buf.data(), buf.size());
				if (n <= 0) {
					return;
				}
				emit(n);
			}
		};
		if (stderr_ == nullptr) {
			drain([](ssize_t) {});
		} else {
			drain([&](ssize_t n) {
				stderr_->write(std::string_view(buf.data(), n));
			});
		}
#else
		std::array<char, 4096> buf{};
		for (;;) {
			ssize_t n = ::read(fd, buf.data(), buf.size());
			if (n <= 0) {
				break;
			}
			if (stderr_ != nullptr) {
				stderr_->write(std::string_view(buf.data(), n));
			}
		}
#endif
	});
	proc->stderrPump = std::move(pump);

	return {std::shared_ptr<gostd::io::ReadWriteCloser>(proc), nullptr};
}

namespace {

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
	      gostd::io::Writer* stderr_) override {
		return spawnProcess(command, dir, stderr_);
	}
};

} // namespace

// newSystem — sys.go:124.
tsc::execute::tsc::System* newSystem() {
	char buf[4096];
	if (::getcwd(buf, sizeof(buf)) == nullptr) {
		std::cerr << "Error getting current directory: "
		          << w32ErrText(errno) << "\n";
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
