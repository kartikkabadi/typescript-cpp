// tests_mapper.cpp — port of tsc/internal/testutil/contentmappertest/mapper_test.go.
//
// The Go test re-executes the test binary as a mapper subprocess (guarded by
// TSGO_CONTENT_MAPPER_HELPER) and drives it over stdio through the production
// content mapper host. The C++ port mirrors that: the runner binary enters
// helper mode in main() when the env var is set, and execSpawner below
// fork/exec's it with pipes on stdin/stdout.
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cerrno>
#include <cstring>
#include <signal.h>

#ifdef _WIN32
#include "internal/win32/w32compat.h"
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "internal/contentmapper/contentmapper.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/locale/locale.h"
#include "internal/testutil/contentmappertest/contentmappertest.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace cm = tsc::contentmapper;
namespace gostd = tsc::gostd;
namespace contentmappertest = tsc::testutil::contentmappertest;
using namespace tsc;

cm::Mapper* testMapper() {
	auto* m = new cm::Mapper();
	m->Definition.Package = std::string(contentmappertest::PackageName);
	m->Definition.Extensions = {".box"};
	m->Manifest.Name = std::string(contentmappertest::PackageName);
	m->Manifest.Version = "1.0.0";
	m->Manifest.Exec = {std::string(contentmappertest::TransformingMapper)};
	m->Manifest.CompilerOptions = contentmappertest::DeclaredOptions;
	m->PackageDirectory =
	    "/node_modules/" + std::string(contentmappertest::PackageName);
	return m;
}

cm::Request transformRequest() {
	return cm::Request{"/app.box", "export const version = #{target};\n"};
}

// process adapts a spawned subprocess's stdio to a ReadWriteCloser: reads
// come from its stdout, writes go to its stdin, and close() tears the process
// down.
struct process : gostd::io::ReadWriteCloser {
	pid_t pid;
	int stdinFd;
	int stdoutFd;

	std::pair<int, gostd::Error> read(std::span<char> buf) override {
		ssize_t n = ::read(stdoutFd, buf.data(), buf.size());
		if (n < 0) {
			return {0, gostd::newError("read: " +
			                         std::string(strerror(errno)))};
		}
		return {(int)n, nullptr};
	}
	std::pair<int, gostd::Error> write(std::string_view data) override {
		size_t off = 0;
		while (off < data.size()) {
			ssize_t n = ::write(stdinFd, data.data() + off,
			                    data.size() - off);
			if (n < 0) {
				return {(int)off,
				        gostd::newError("write: " +
				                        std::string(strerror(errno)))};
			}
			off += (size_t)n;
		}
		return {(int)off, nullptr};
	}
	gostd::Error close() override {
		::close(stdinFd);
		::close(stdoutFd);
		::kill(pid, SIGKILL);
		int status;
		::waitpid(pid, &status, 0);
		return nullptr;
	}
};

// execSpawner spawns the test binary itself as the mapper subprocess
// (guarded by the helper env var), so the test talks to a genuinely separate
// process over real pipes.
struct execSpawner : cm::Spawner {
	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Spawn(const std::vector<std::string>& command, const std::string& dir,
	      gostd::io::Writer* stderr_) override {
		int inPipe[2], outPipe[2];
		if (::pipe(inPipe) != 0 || ::pipe(outPipe) != 0) {
			return {nullptr, gostd::newError("pipe failed")};
		}
#ifdef _WIN32
		// fork+execv -> spawnvp: pipe ends become the child's stdin/stdout
		// (stderr inherited, like the POSIX dup2(STDERR,STDERR)).
		w32::SpawnStdio io;
		io.stdinFd = inPipe[0];
		io.stdoutFd = outPipe[1];
		::setenv("TSGO_CONTENT_MAPPER_HELPER", "1", 1);
		std::vector<std::string> argv{w32::selfExePath()};
		pid_t pid = w32::spawnvp(argv, io);
		::unsetenv("TSGO_CONTENT_MAPPER_HELPER");
		::close(inPipe[0]);
		::close(outPipe[1]);
		if (pid < 0) {
			::close(inPipe[1]);
			::close(outPipe[0]);
			return {nullptr, gostd::newError("spawn failed")};
		}
#else
		pid_t pid = ::fork();
		if (pid < 0) {
			return {nullptr, gostd::newError("fork failed")};
		}
		if (pid == 0) {
			::dup2(inPipe[0], STDIN_FILENO);
			::dup2(outPipe[1], STDOUT_FILENO);
			::dup2(STDERR_FILENO, STDERR_FILENO); // inherit parent stderr
			::close(inPipe[0]); ::close(inPipe[1]);
			::close(outPipe[0]); ::close(outPipe[1]);
			::setenv("TSGO_CONTENT_MAPPER_HELPER", "1", 1);
			char selfPath[4096];
			ssize_t n = ::readlink("/proc/self/exe", selfPath,
			                       sizeof(selfPath) - 1);
			if (n <= 0) _exit(127);
			selfPath[n] = '\0';
			char* args[] = {selfPath, nullptr};
			::execv(selfPath, args);
			_exit(127);
		}
		::close(inPipe[0]);
		::close(outPipe[1]);
#endif
		auto* p = new process();
		p->pid = pid;
		p->stdinFd = inPipe[1];
		p->stdoutFd = outPipe[0];
		return {std::shared_ptr<gostd::io::ReadWriteCloser>(p), nullptr};
	}
};

// TestOutOfProcess exercises the real out-of-process IPC path: it spawns the
// test binary as a mapper subprocess and drives it over stdio through the
// production content mapper host.
void TestOutOfProcess(T* t) {
	t->Parallel();
	execSpawner spawner;
	auto host = cm::NewHost(t->Context(), &spawner, locale::Default);
	auto* mapper = testMapper();
	auto request = transformRequest();
	auto* compilerOptions = new tsc::CompilerOptions();
	compilerOptions->Target = tsc::ScriptTarget::ES2020;
	auto project = host->Project(
	    {.ConfigFileName = "/tsconfig.json",
	     .Mappers = {mapper},
	     .CompilerOptions = compilerOptions});

	auto [result, err] = project->Transform(mapper, request);
	assert::NilError(t, err);
	assert::Assert(
	    t, result.Text.find("export const version = 7;") != std::string::npos,
	    "got \"" + result.Text + "\"");
	assert::Assert(t, result.Mappings != nullptr);

	project->Close();
	host->Close();
}

REGISTER_UNIT_TEST("contentmappertest.TestOutOfProcess", TestOutOfProcess);

} // namespace
