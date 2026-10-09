// fourslashrunner — runs the ported fourslash tests registered in
// internal/fourslash/tests/. Mirrors `go test`: per test a fresh
// gostd::testing::T, `PASS <name>` / `FAIL <name>` output, `-run <regex>`
// filter, `N/M pass` summary, exit 0 iff all selected tests pass.
//
// Each test runs in a forked child process: tscUnreachable() calls
// std::_Exit(2) (like an untrappable Go panic/runtime abort), so an
// in-process runner could not survive such a test and report the rest.
// go test similarly aborts the whole test binary on a runtime crash —
// we instead mark the crashing test FAIL and continue, which is the
// useful-oracle superset (a Go test crashing would also count as failed).
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include "internal/win32/w32compat.h"
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "internal/fourslash/tests/registry.h"
#include "internal/gostd/regexp.h"
#include "internal/gostd/testing.h"

namespace {

// `go test` defaults to -timeout 10m and fails the binary on expiry. A hung
// test child (e.g. a server-side panic leaving the client waiting on a
// response that will never come) is marked FAIL instead of stalling the
// whole batch.
constexpr unsigned kTestTimeoutSeconds = 600;

void onTestAlarm(int) {
	const char msg[] = "[timed out]\n";
	(void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
#ifdef _WIN32
	// TerminateProcess skips DLL_PROCESS_DETACH teardown that would
	// race the test's leaked detached threads (see --test-child).
	TerminateProcess(GetCurrentProcess(), 1);
#else
	_exit(1);
#endif
}

// Runs the test body and returns 0 pass / 1 fail / 3 skip — shared by the
// forked child (POSIX) and the --test-child respawn (Windows).
int runTestBody(const tsc::fourslash::tests::FourslashTestCase& tc) {
	signal(SIGALRM, onTestAlarm);
	alarm(kTestTimeoutSeconds);
	tsc::gostd::testing::T t{std::string(tc.name)};
	int code = 0;
	try {
		// Invoke the test fn on the runner's named T directly (not via
		// t.Run): T.Run intentionally does not mark the parent skipped for
		// a skipped subtest, so wrapping would hide this test's Skip from
		// the runner. t->Name() feeds getBaseFileNameFromTest for baseline
		// file naming, same as the subtest name did.
		tc.fn(&t);
	} catch (const tsc::gostd::testing::testGoexit&) {
	} catch (const std::exception& e) {
		t.Errorf("uncaught exception: %s", {e.what()});
	} catch (...) {
		t.Errorf("uncaught non-std::exception", {});
	}
	// Failed before Skipped: go test reports `--- FAIL` for a test that
	// called Errorf then Skip — a skip can't mask a failure. Skip is code
	// 3, not 2: tscUnreachable exits the child with code 2 (Go's panic
	// contract), which must report as FAIL.
	if (t.Failed()) code = 1;
	else if (t.Skipped()) code = 3;
	fflush(stdout);
	fflush(stderr);
	return code;
}

// Runs one test in a child process; returns 0 pass, 1 fail, and the
// child's captured stdout+stderr in `output`.
#ifdef _WIN32
int runOne(const tsc::fourslash::tests::FourslashTestCase& tc, std::string& output) {
	// Windows has no fork: respawn this image with --test-child <name> and
	// capture its stdout+stderr through a pipe (the parent's already-flushed
	// FILE buffers don't leak — the child is a fresh process).
	int pipefd[2];
	if (pipe(pipefd) != 0) {
		fprintf(stderr, "pipe failed\n");
		return 1;
	}
	w32::SpawnStdio io;
	io.stdoutFd = pipefd[1];
	io.mergeStderrToStdout = true;
	std::string self = w32::selfExePath();
	std::vector<std::string> argv{self, "--test-child", tc.name};
	pid_t pid = w32::spawnvp(argv, io);
	close(pipefd[1]);
	if (pid < 0) {
		fprintf(stderr, "spawn failed\n");
		close(pipefd[0]);
		return 1;
	}
	char buf[4096];
	ssize_t n;
	while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
		output.append(buf, static_cast<size_t>(n));
	}
	close(pipefd[0]);
	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	}
	if (WIFSIGNALED(status)) {
		output += "[killed by signal " + std::to_string(WTERMSIG(status)) + "]\n";
	}
	return 1;
}
#else
int runOne(const tsc::fourslash::tests::FourslashTestCase& tc, std::string& output) {
	int pipefd[2];
	if (pipe(pipefd) != 0) {
		fprintf(stderr, "pipe failed\n");
		return 1;
	}
	// Flush stdio before forking: the child inherits a copy of the FILE
	// buffers and would otherwise re-emit the parent's pending output
	// into the pipe.
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork failed\n");
		close(pipefd[0]); close(pipefd[1]);
		return 1;
	}
	if (pid == 0) {
		// Child: capture stdout+stderr.
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);
		// Avoid running atexit/IPC teardown twice.
		_exit(runTestBody(tc));
	}
	close(pipefd[1]);
	char buf[4096];
	ssize_t n;
	while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
		output.append(buf, static_cast<size_t>(n));
	}
	close(pipefd[0]);
	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	}
	if (WIFSIGNALED(status)) {
		output += "[killed by signal " + std::to_string(WTERMSIG(status)) + "]\n";
	}
	return 1;
}
#endif

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
	// Child side of runOne's respawn: run the named test, print its result
	// stream to stdout, exit with its code.
	if (argc >= 3 && std::string(argv[1]) == "--test-child") {
		w32::setBinaryStdio();
		const std::string target = argv[2];
		for (auto& tc : tsc::fourslash::tests::fourslashTestRegistry()) {
			if (tc.name == target) {
				// TerminateProcess, not return or even _exit: the POSIX
				// child calls _exit, but a Windows ExitProcess still runs
				// DLL_PROCESS_DETACH in every loaded DLL while the test's
				// leaked detached threads touch CRT/MSVCP internals —
				// a racy __fastfail that Go's process-exit semantics
				// never produce. TerminateProcess skips all user-mode
				// teardown, matching the fork-child's contract.
				fflush(nullptr);
				TerminateProcess(GetCurrentProcess(),
				                 (UINT)runTestBody(tc));
			}
		}
		fprintf(stderr, "unknown test %s\n", target.c_str());
		return 1;
	}
#endif
	std::string runFilter;
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "-run" && i + 1 < argc) {
			runFilter = argv[++i];
		} else if (arg.rfind("-run=", 0) == 0) {
			runFilter = arg.substr(5);
		} else if (arg == "-list") {
			for (auto& tc : tsc::fourslash::tests::fourslashTestRegistry()) {
				printf("%s\n", tc.name);
			}
			return 0;
		}
	}

	tsc::gostd::regexp::Regexp re(runFilter.empty() ? ".*" : runFilter);
	int total = 0, passed = 0;
	for (auto& tc : tsc::fourslash::tests::fourslashTestRegistry()) {
		std::string name = tc.name;
		if (!re.MatchString(name)) continue;
		++total;
		std::string output;
		int code = runOne(tc, output);
		if (code == 0) {
			++passed;
			printf("PASS %s\n", name.c_str());
		} else if (code == 3) {
			--total;  // SKIP: like go test, don't count toward N/M pass.
			// Print the captured skip reason (like `go test -v` does).
			std::string reason;
			{
				std::string line;
				for (size_t i = 0; i <= output.size(); ++i) {
					if (i == output.size() || output[i] == '\n') {
						if (!line.empty()) reason += "    " + line + "\n";
						line.clear();
					} else {
						line += output[i];
					}
				}
			}
			printf("SKIP %s\n%s", name.c_str(), reason.c_str());
		} else {
			printf("FAIL %s\n", name.c_str());
			std::string line;
			for (size_t i = 0; i <= output.size(); ++i) {
				if (i == output.size() || output[i] == '\n') {
					if (!line.empty()) printf("    %s\n", line.c_str());
					line.clear();
				} else {
					line += output[i];
				}
			}
		}
	}
	printf("%d/%d pass\n", passed, total);
	return passed == total ? 0 : 1;
}
