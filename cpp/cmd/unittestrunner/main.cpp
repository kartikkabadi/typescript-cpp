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
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/contentmappertest/contentmappertest.h"
#include "internal/testutil/unittests/registry.h"

namespace {

// `go test` defaults to -timeout 10m and fails the binary on expiry.
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

struct RunCtx {
	const tsc::testutil::unittests::UnitTestCase* tc;
	int code = 0;
};

void* runTestImpl(void* arg) {
	auto* rc = static_cast<RunCtx*>(arg);
	tsc::gostd::testing::T t;
	// Invoke the test fn on the runner's T directly (not via t.Run): a
	// t.Skip() in the test body must mark the top-level test skipped,
	// while a skip inside a t.Run subtest must not — Go reports
	// `--- SKIP: TestX/child`, `--- PASS: TestX`.
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
	// Failed before Skipped: go test reports `--- FAIL` for a test
	// that called Errorf then Skip — a skip can't mask a failure.
	if (t.Failed()) rc->code = 1;
	else if (t.Skipped()) rc->code = 3;
	return nullptr;
}

#ifdef _WIN32
DWORD WINAPI runTestImplW32(LPVOID arg) {
	runTestImpl(arg);
	return 0;
}
#endif

// Runs the test body; returns 0 pass / 1 fail / 3 skip — shared by the
// forked child (POSIX) and the --test-child respawn (Windows).
// Tests run on a 64MB-stack thread (same convention as tscpp's parse
// workers): Go tests rely on goroutine stacks that grow dynamically,
// and deeply nested inputs (e.g. TestSelectionRangeDepthIsLimited's
// 12k parens) need more than the default stack.
int runTestBody(const tsc::testutil::unittests::UnitTestCase& tc) {
	signal(SIGALRM, onTestAlarm);
	alarm(kTestTimeoutSeconds);
	RunCtx ctx{&tc, 0};
#ifdef _WIN32
	// CreateThread takes the stack reserve directly (64MB like POSIX).
	HANDLE h = CreateThread(nullptr, SIZE_T{64} << 20, runTestImplW32,
	                        &ctx, 0, nullptr);
	if (h == nullptr) {
		runTestImpl(&ctx);
	} else {
		WaitForSingleObject(h, INFINITE);
		CloseHandle(h);
	}
#else
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, size_t{64} << 20);
	pthread_t thread;
	if (pthread_create(&thread, &attr, runTestImpl, &ctx) != 0) {
		runTestImpl(&ctx);
	} else {
		pthread_join(thread, nullptr);
	}
	pthread_attr_destroy(&attr);
#endif
	fflush(stdout);
	fflush(stderr);
	return ctx.code;
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
#ifdef _WIN32
		// The mapper protocol is binary — fds must not translate \r\n.
		w32::setBinaryStdio();
#endif
		auto rwc = std::make_shared<stdioRwc>();
		(void)tsc::testutil::contentmappertest::Serve(
		    tsc::gostd::contextBackground(), rwc);
		return 0;
	}

#ifdef _WIN32
	if (argc >= 3 && std::string(argv[1]) == "--test-child") {
		w32::setBinaryStdio();
		const std::string target = argv[2];
		for (auto& tc : tsc::testutil::unittests::unitTestRegistry()) {
			if (tc.name == target) {
				// TerminateProcess, not return or _exit: ExitProcess
				// runs DLL_PROCESS_DETACH in every DLL while the
				// test's leaked detached threads touch CRT/MSVCP
				// internals — a racy __fastfail Go never produces
				// (same contract as fourslashrunner's --test-child).
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
			for (auto& tc : tsc::testutil::unittests::unitTestRegistry()) {
				printf("%s\n", tc.name);
			}
			return 0;
		}
	}

	std::regex re;
	try {
		re = std::regex(runFilter.empty() ? ".*" : runFilter);
	} catch (const std::regex_error& e) {
		fprintf(stderr, "invalid -run regex: %s\n", e.what());
		return 2;
	}
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
