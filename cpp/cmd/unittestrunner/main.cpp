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

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <execinfo.h>

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

void onCrash(int sig) {
	const char msg[] = "[crash backtrace]\n";
	(void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
	void* bt[64];
	int n = backtrace(bt, 64);
	backtrace_symbols_fd(bt, n, STDERR_FILENO);
	signal(sig, SIG_DFL);
	raise(sig);
}

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
		signal(SIGALRM, onTestAlarm);
		signal(SIGABRT, onCrash);
		signal(SIGSEGV, onCrash);
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
		_exit(code);
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

}  // namespace

int main(int argc, char** argv) {
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
