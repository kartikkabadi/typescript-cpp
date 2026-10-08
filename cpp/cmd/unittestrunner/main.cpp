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

#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/contentmappertest/contentmappertest.h"
#include "internal/testutil/unittests/registry.h"

namespace {

// `go test` defaults to -timeout 10m and fails the binary on expiry.
constexpr unsigned kTestTimeoutSeconds = 180;

void onTestAlarm(int) {
	const char msg[] = "[timed out]\n";
	(void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
	_exit(1);
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
		alarm(kTestTimeoutSeconds);
		// Tests run on a 64MB-stack thread (same convention as
		// tscpp's parse workers): Go tests rely on goroutine stacks
		// that grow dynamically, and deeply nested inputs (e.g.
		// TestSelectionRangeDepthIsLimited's 12k parens) need more
		// than the default 8MB main-thread stack.
		struct RunCtx {
			const tsc::testutil::unittests::UnitTestCase* tc;
			int code = 0;
		};
		auto runTest = [](void* arg) -> void* {
			auto* rc = static_cast<RunCtx*>(arg);
			tsc::gostd::testing::T t;
			// Invoke the test fn on the runner's T directly (not via
			// t.Run): a t.Skip() in the test body must mark the top-level
			// test skipped, while a skip inside a t.Run subtest must not
			// — Go reports `--- SKIP: TestX/child`, `--- PASS: TestX`.
			try {
				rc->tc->fn(&t);
			} catch (const tsc::gostd::testing::testGoexit&) {
			} catch (const std::exception& e) {
				t.Errorf("uncaught exception: %s", {e.what()});
			} catch (...) {
				t.Errorf("uncaught non-std::exception", {});
			}
			// Skip sentinel is 3, not 2: tscUnreachable exits the child with
			// code 2 (Go panic contract), which must report as FAIL.
			if (t.Skipped()) rc->code = 3;
			else if (t.Failed()) rc->code = 1;
			return nullptr;
		};
		RunCtx ctx{&tc, 0};
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, size_t{64} << 20);
		pthread_t thread;
		if (pthread_create(&thread, &attr, runTest, &ctx) != 0) {
			runTest(&ctx);
		} else {
			pthread_join(thread, nullptr);
		}
		pthread_attr_destroy(&attr);
		fflush(stdout);
		fflush(stderr);
		_exit(ctx.code);
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

// stdio adapts the process's stdin/stdout to a ReadWriteCloser for the
// content-mapper server (mapper_test.go's TestMain helper mode).
struct stdioRwc : tsc::gostd::io::ReadWriteCloser {
	std::pair<int, tsc::gostd::Error> read(std::span<char> buf) override {
		ssize_t n = ::read(STDIN_FILENO, buf.data(), buf.size());
		if (n < 0) return {0, tsc::gostd::newError("read stdin failed")};
		return {(int)n, nullptr};
	}
	std::pair<int, tsc::gostd::Error> write(std::string_view data) override {
		size_t off = 0;
		while (off < data.size()) {
			ssize_t n = ::write(STDOUT_FILENO, data.data() + off,
			                    data.size() - off);
			if (n < 0) {
				return {(int)off,
				        tsc::gostd::newError("write stdout failed")};
			}
			off += (size_t)n;
		}
		return {(int)off, nullptr};
	}
	tsc::gostd::Error close() override { return nullptr; }
};

int main(int argc, char** argv) {
	// TSGO_CONTENT_MAPPER_HELPER, when set, makes the test binary act as the
	// mapper subprocess instead of running tests. This lets the
	// out-of-process test spawn a real subprocess (itself) that speaks the
	// mapper protocol over stdio — mapper_test.go's TestMain.
	if (const char* helper = std::getenv("TSGO_CONTENT_MAPPER_HELPER");
	    helper && std::string(helper) == "1") {
		auto rwc = std::make_shared<stdioRwc>();
		(void)tsc::testutil::contentmappertest::Serve(
		    tsc::gostd::contextBackground(), rwc);
		return 0;
	}

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
		} else if (code == 3) {
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
