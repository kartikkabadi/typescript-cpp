// unittestrunner — runs ported Go *_test.go unit tests registered via
// REGISTER_UNIT_TEST in internal/<pkg>/tests/. Mirrors `go test`: per test a
// fresh gostd::testing::T, `PASS <name>` / `FAIL <name>` / `SKIP <name>`
// output, `-run <regex>` filter, `N/M pass` summary, exit 0 iff all
// selected tests pass.
//
// Each test runs in a forked child process (same rationale as
// fourslashrunner): tscUnreachable() exits hard like an untrappable Go
// panic, so an in-process runner could not survive such a test.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <regex>
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

#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"

namespace {

// `go test` defaults to -timeout 10m and fails the binary on expiry.
constexpr unsigned kTestTimeoutSeconds = 180;

void onTestAlarm(int) {
	const char msg[] = "[timed out]\n";
	(void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
	_exit(1);
}

// Runs the test body; returns 0 pass / 1 fail / 2 skip — shared by the
// forked child (POSIX) and the --test-child respawn (Windows).
int runTestBody(const tsc::testutil::unittests::UnitTestCase& tc) {
	signal(SIGALRM, onTestAlarm);
	alarm(kTestTimeoutSeconds);
	tsc::gostd::testing::T t;
	int code = 0;
	try {
		t.Run(tc.name, tc.fn);
	} catch (const std::exception& e) {
		t.Errorf("uncaught exception: %s", {e.what()});
	} catch (...) {
		t.Errorf("uncaught non-std::exception", {});
	}
	if (t.Skipped()) code = 2;
	else if (t.Failed()) code = 1;
	fflush(stdout);
	fflush(stderr);
	return code;
}

#ifdef _WIN32
int runOne(const tsc::testutil::unittests::UnitTestCase& tc, std::string& output) {
	// Windows has no fork: respawn this image with --test-child <name> and
	// capture its stdout+stderr through a pipe.
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
int runOne(const tsc::testutil::unittests::UnitTestCase& tc, std::string& output) {
	int pipefd[2];
	if (pipe(pipefd) != 0) {
		fprintf(stderr, "pipe failed\n");
		return 1;
	}
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork failed\n");
		close(pipefd[0]); close(pipefd[1]);
		return 1;
	}
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);
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
	if (argc >= 3 && std::string(argv[1]) == "--test-child") {
		w32::setBinaryStdio();
		const std::string target = argv[2];
		for (auto& tc : tsc::testutil::unittests::unitTestRegistry()) {
			if (tc.name == target) {
				return runTestBody(tc);
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
			for (auto& tc : tsc::testutil::unittests::unitTestRegistry()) {
				printf("%s\n", tc.name);
			}
			return 0;
		}
	}

	std::regex re(runFilter.empty() ? ".*" : runFilter);
	int total = 0, passed = 0;
	for (auto& tc : tsc::testutil::unittests::unitTestRegistry()) {
		std::string name = tc.name;
		if (!std::regex_search(name, re)) continue;
		++total;
		std::string output;
		int code = runOne(tc, output);
		if (code == 0) {
			++passed;
			printf("PASS %s\n", name.c_str());
		} else if (code == 2) {
			--total;  // SKIP: like go test, don't count toward N/M pass.
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
