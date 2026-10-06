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
#include <regex>
#include <string>
#include <vector>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "internal/fourslash/tests/registry.h"
#include "internal/gostd/testing.h"

namespace {

// `go test` defaults to -timeout 10m and fails the binary on expiry. A hung
// test child (e.g. a server-side panic leaving the client waiting on a
// response that will never come) is marked FAIL instead of stalling the
// whole batch.
constexpr unsigned kTestTimeoutSeconds = 180;

void onTestAlarm(int) {
	const char msg[] = "[timed out]\n";
	(void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
	_exit(1);
}

// Runs one test in a child process; returns 0 pass, 1 fail, and the
// child's captured stdout+stderr in `output`.
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
		signal(SIGALRM, onTestAlarm);
		alarm(kTestTimeoutSeconds);
		tsc::gostd::testing::T t;
		int code = 0;
		try {
			// Run as a named subtest, like go test does: t->Name() feeds
			// getBaseFileNameFromTest for baseline file naming.
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
		// Avoid running atexit/IPC teardown twice.
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
			for (auto& tc : tsc::fourslash::tests::fourslashTestRegistry()) {
				printf("%s\n", tc.name);
			}
			return 0;
		}
	}

	std::regex re(runFilter.empty() ? ".*" : runFilter);
	int total = 0, passed = 0;
	for (auto& tc : tsc::fourslash::tests::fourslashTestRegistry()) {
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
