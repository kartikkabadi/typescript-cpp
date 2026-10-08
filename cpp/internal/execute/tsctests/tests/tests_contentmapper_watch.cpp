// tests_contentmapper_watch.cpp — port of
// tsc/internal/execute/tsctests/contentmapper_watch_test.go. Registered in
// unittestrunner (not tsctestrunner): these drive CommandLine/watch cycles
// directly rather than baseline scenarios.
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "internal/contentmapper/contentmapper.h"
#include "internal/execute/execute.h"
#include "internal/execute/tsc/compile.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/execute/watcher.h"
#include "internal/fswatch/fswatch.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/contentmappertest/contentmappertest.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc {
namespace {
using namespace ::tsc::execute::tsctests;
namespace contentmappertest = ::tsc::testutil::contentmappertest;


using gostd::testing::T;

// stringsReplace — strings.Replace(s, old, new, n): n<0 = ReplaceAll.
inline std::string stringsReplace(std::string_view s, std::string_view old_,
                                  std::string_view new_, int n) {
	std::string out;
	size_t i = 0;
	int replaced = 0;
	while (i < s.size()) {
		auto pos = s.find(old_, i);
		if (pos == std::string_view::npos ||
		    (n >= 0 && replaced >= n)) {
			out.append(s.substr(i));
			break;
		}
		out.append(s.substr(i, pos - i));
		out.append(new_);
		i = pos + old_.size();
		replaced++;
	}
	return out;
}

// chanStruct — Go's buffered chan struct{} used as a close signal.
struct closedChan {
	std::mutex mu;
	std::condition_variable cv;
	int count = 0;

	void post() {
		std::lock_guard<std::mutex> lock(mu);
		count++;
		cv.notify_one();
	}
	// take — `<-closed` (timeout < 0 blocks forever; otherwise a select
	// with time.After).
	bool take(int timeoutMs = -1) {
		std::unique_lock<std::mutex> lock(mu);
		if (timeoutMs >= 0) {
			if (!cv.wait_for(
			        lock, std::chrono::milliseconds(timeoutMs),
			        [&] { return count > 0; })) {
				return false;
			}
		} else {
			cv.wait(lock, [&] { return count > 0; });
		}
		count--;
		return true;
	}
};

// recordingContentMapperSystem — Go embeds *TestSys; TestSys is final here,
// so this wraps it and forwards every etsc::System member except Spawn.
struct recordingContentMapperSystem : etsc::System {
	std::unique_ptr<TestSys> inner;
	struct recordingContentMapperSpawner* spawner;

	recordingContentMapperSystem(
	    std::unique_ptr<TestSys> inner,
	    recordingContentMapperSpawner* spawner);

	std::ostream* Writer() override { return inner->Writer(); }
	std::ostream* ErrorWriter() override { return inner->ErrorWriter(); }
	std::shared_ptr<vfs::FS> fs() override { return inner->fs(); }
	std::string DefaultLibraryPath() override {
		return inner->DefaultLibraryPath();
	}
	std::string GetCurrentDirectory() override {
		return inner->GetCurrentDirectory();
	}
	bool WriteOutputIsTTY() override {
		return inner->WriteOutputIsTTY();
	}
	int GetWidthOfTerminal() override { return inner->GetWidthOfTerminal(); }
	std::pair<std::string, bool>
	GetEnvironmentVariable(std::string_view name) override {
		return inner->GetEnvironmentVariable(name);
	}
	vfs::TimePoint Now() override { return inner->Now(); }
	gostd::Duration SinceStart() override { return inner->SinceStart(); }
	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Spawn(const std::vector<std::string>& command, const std::string& dir,
	      gostd::io::Writer* stderr) override;
};

struct recordingContentMapperSpawner {
	contentmapper::Spawner* inner;
	std::atomic<int32_t> spawns{0};
	std::atomic<int32_t> closes{0};
	closedChan* closed = nullptr;

	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Spawn(const std::vector<std::string>& command, const std::string& dir,
	      gostd::io::Writer* stderr);
};

// recordingContentMapperProcess — Go wraps io.ReadWriteCloser with a
// once-guarded Close that records the close and signals `closed`.
struct recordingContentMapperProcess : gostd::io::ReadWriteCloser {
	std::shared_ptr<gostd::io::ReadWriteCloser> inner;
	std::atomic<int32_t>* closes;
	closedChan* closed;
	std::once_flag once;

	recordingContentMapperProcess(
	    std::shared_ptr<gostd::io::ReadWriteCloser> inner,
	    std::atomic<int32_t>* closes, closedChan* closed)
	    : inner(std::move(inner)), closes(closes), closed(closed) {}

	std::pair<int, gostd::Error> read(std::span<char> buf) override {
		return inner->read(buf);
	}
	std::pair<int, gostd::Error> write(std::string_view data) override {
		return inner->write(data);
	}
	gostd::Error close() override {
		gostd::Error err;
		std::call_once(once, [&] {
			closes->fetch_add(1);
			err = inner->close();
			if (closed != nullptr) {
				closed->post();
			}
		});
		return err;
	}
};

std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
recordingContentMapperSpawner::Spawn(
    const std::vector<std::string>& command, const std::string& dir,
    gostd::io::Writer* stderr) {
	auto [process, err] = inner->Spawn(command, dir, stderr);
	if (err) {
		return {nullptr, err};
	}
	spawns.fetch_add(1);
	return {std::shared_ptr<gostd::io::ReadWriteCloser>(
	            new recordingContentMapperProcess{process, &closes, closed}),
	        gostd::Error()};
}

recordingContentMapperSystem::recordingContentMapperSystem(
    std::unique_ptr<TestSys> inner,
    recordingContentMapperSpawner* spawner)
    : inner(std::move(inner)), spawner(spawner) {}

std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
recordingContentMapperSystem::Spawn(
    const std::vector<std::string>& command, const std::string& dir,
    gostd::io::Writer* stderr) {
	return spawner->Spawn(command, dir, stderr);
}

void TestContentMapperBuildLifecycle(T* t) {
	t->Parallel();
	auto* input = new tscInput{.files = FileMap{
	    {"/home/src/workspaces/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "composite": true },
			"contentMappers": [{ "package": "mapper", "extensions": [".vue"] }]
		})")},
	    {"/home/src/workspaces/project/app.vue", "export const app = 1;"},
	    {"/home/src/workspaces/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(contentmappertest::VerbatimMapper)},
	}};
	auto testSys = newTestSys(input, false);
	auto* spawner = new recordingContentMapperSpawner{
	    .inner = contentmappertest::NewSpawner()};
	auto* sys =
	    new recordingContentMapperSystem(std::move(testSys), spawner);
	auto* testSysPtr = sys->inner.get();

	auto result = execute::CommandLine(
	    t->Context(), sys, {"--build", "--runExternalCode"}, testSysPtr);
	if (result.Watcher != nullptr)
		t->Error({"expected nil Watcher"});
	if (spawner->spawns.load() != 1)
		t->Error({"expected 1 spawn"});
	if (spawner->closes.load() != 1)
		t->Error({"expected 1 close"});
}
REGISTER_UNIT_TEST("tsctests.TestContentMapperBuildLifecycle",
                   TestContentMapperBuildLifecycle);

void TestContentMapperSupplementalDiagnosticUsesOriginalFileName(T* t) {
	t->Parallel();
	auto* input = new tscInput{.files = FileMap{
	    {"/home/src/workspaces/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "noEmit": true },
			"contentMappers": [{ "package": "mapper", "extensions": [".astro"] }]
		})")},
	    {"/home/src/workspaces/project/app.astro",
	     "const value: string = 1;"},
	    {"/home/src/workspaces/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::SupplementalDiagnosticsMapper)},
	}};
	auto testSys = newTestSys(input, false);
	auto* sys = new recordingContentMapperSystem(
	    std::move(testSys),
	    new recordingContentMapperSpawner{
	        .inner = contentmappertest::NewSpawner()});
	auto* testSysPtr = sys->inner.get();

	auto result = execute::CommandLine(
	    t->Context(), sys,
	    {"--pretty", "false", "--runExternalCode"}, testSysPtr);
	if (result.Status != etsc::ExitStatusDiagnosticsPresent_OutputsGenerated)
		t->Error({"unexpected status"});
	std::string output = testSysPtr->currentWrite->String();
	if (output.find("app.astro(1,1): error TS2304") ==
	    std::string::npos)
		t->Error({output});
	if (output.find("app.astro(1,7): error TS2322") ==
	    std::string::npos)
		t->Error({output});
	if (output.find("app.astro.0.ts") != std::string::npos)
		t->Error({output});
}
REGISTER_UNIT_TEST(
    "tsctests.TestContentMapperSupplementalDiagnosticUsesOriginalFileName",
    TestContentMapperSupplementalDiagnosticUsesOriginalFileName);

void TestContentMapperBuildDetectsNewPhysicalSupplementalFile(T* t) {
	t->Parallel();
	const std::string supplementalFileName =
	    "/home/src/workspaces/project/app.vue.0.ts";
	auto* input = new tscInput{.files = FileMap{
	    {"/home/src/workspaces/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "incremental": true },
			"files": ["app.vue"],
			"contentMappers": [{ "package": "mapper", "extensions": [".vue"] }]
		})")},
	    {"/home/src/workspaces/project/app.vue",
	     "declare const value: number;"},
	    {"/home/src/workspaces/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::SupplementalMapper)},
	}};
	auto testSys = newTestSys(input, false);
	auto* sys = new recordingContentMapperSystem(
	    std::move(testSys),
	    new recordingContentMapperSpawner{
	        .inner = contentmappertest::NewSpawner()});
	auto* testSysPtr = sys->inner.get();
	std::vector<std::string> args{"--build", "--pretty", "false",
	                              "--runExternalCode"};
	auto result =
	    execute::CommandLine(t->Context(), sys, args, testSysPtr);
	if (result.Status != etsc::ExitStatusSuccess)
		t->Error({testSysPtr->currentWrite->String()});

	testSysPtr->clearOutput();
	testSysPtr->writeFileNoError(supplementalFileName, "export {};\n");
	result = execute::CommandLine(t->Context(), sys, args, testSysPtr);
	if (result.Status != etsc::ExitStatusDiagnosticsPresent_OutputsGenerated)
		t->Error({"unexpected status"});
	if (testSysPtr->currentWrite->String().find("TS18069") ==
	    std::string::npos)
		t->Error({testSysPtr->currentWrite->String()});
	if (testSysPtr->currentWrite->String().find(
	        "conflicts with an existing file") == std::string::npos)
		t->Error({testSysPtr->currentWrite->String()});
}
REGISTER_UNIT_TEST(
    "tsctests.TestContentMapperBuildDetectsNewPhysicalSupplementalFile",
    TestContentMapperBuildDetectsNewPhysicalSupplementalFile);

void TestContentMapperBuildIdentityFailureExitStatus(T* t) {
	t->Parallel();
	const std::string packageJSONPath =
	    "/home/src/workspaces/project/node_modules/mapper/package.json";
	auto* input = new tscInput{.files = FileMap{
	    {"/home/src/workspaces/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "composite": true },
			"contentMappers": [{ "package": "mapper", "extensions": [".vue"] }]
		})")},
	    {"/home/src/workspaces/project/app.vue", "export const app = 1;"},
	    {packageJSONPath,
	     contentmappertest::PackageJSON(
	         contentmappertest::DynamicVerbatimMapper)},
	}};
	auto testSys = newTestSys(input, false);
	auto* sys = new recordingContentMapperSystem(
	    std::move(testSys),
	    new recordingContentMapperSpawner{
	        .inner = contentmappertest::NewSpawner()});
	auto* testSysPtr = sys->inner.get();
	std::vector<std::string> args{"--build", "--runExternalCode"};
	auto result =
	    execute::CommandLine(t->Context(), sys, args, testSysPtr);
	if (result.Status != etsc::ExitStatusSuccess)
		t->Error({"unexpected status"});

	testSysPtr->writeFileNoError(
	    packageJSONPath,
	    R"({
		"name": "mapper",
		"version": "1.0.0",
		"typescript": { "contentMapper": { "exec": ["missing-mapper"], "dynamicConfig": true } }
	})");
	result = execute::CommandLine(t->Context(), sys, args, testSysPtr);
	if (result.Status != etsc::ExitStatusDiagnosticsPresent_OutputsSkipped)
		t->Error({"unexpected status"});
}
REGISTER_UNIT_TEST("tsctests.TestContentMapperBuildIdentityFailureExitStatus",
                   TestContentMapperBuildIdentityFailureExitStatus);

void TestContentMapperWatchLifecycle(T* t) {
	t->Parallel();
	struct watchTest {
		std::string name;
		std::vector<std::string> args;
	};
	for (auto& test : std::vector<watchTest>{
	         {"watch", {"--watch", "--runExternalCode"}},
	         {"build watch",
	          {"--build", "--watch", "--runExternalCode"}},
	     }) {
		auto testCopy = test;
		t->Run(test.name, [testCopy](T* t) {
			t->Parallel();
			const std::string configFileName =
			    "/home/src/workspaces/project/tsconfig.json";
			auto* input = new tscInput{.files = FileMap{
			    {configFileName, std::string(R"({
					"compilerOptions": { "composite": true },
					"contentMappers": [{ "package": "mapper-a", "extensions": [".vue"] }]
				})")},
			    {"/home/src/workspaces/project/app.vue",
			     "export const app = 1;"},
			    {"/home/src/workspaces/project/node_modules/mapper-a/package.json",
			     contentmappertest::PackageJSON(
			         contentmappertest::VerbatimMapper)},
			    {"/home/src/workspaces/project/node_modules/mapper-b/package.json",
			     stringsReplace(
			         contentmappertest::PackageJSON(
			             contentmappertest::VerbatimMapper),
			         "\"version\": \"1.0.0\"", "\"version\": \"2.0.0\"", 1)},
			}};
			auto testSys = newTestSys(input, false);
			auto* closed = new closedChan();
			auto* spawner = new recordingContentMapperSpawner{
			    .inner = contentmappertest::NewSpawner(),
			    .closed = closed};
			auto* sys = new recordingContentMapperSystem(
			    std::move(testSys), spawner);
			auto* testSysPtr = sys->inner.get();
			auto cc = gostd::contextWithCancel(t->Context());
			auto ctx = cc.first;
			auto cancel = cc.second;
			t->Cleanup([cancel] { cancel(); });

			auto result = execute::CommandLine(ctx, sys, testCopy.args,
			                                   testSysPtr);
			if (result.Watcher == nullptr)
				t->Fatal({"expected non-nil Watcher"});
			if (spawner->spawns.load() != 1)
				t->Error({"expected 1 spawn"});
			if (spawner->closes.load() != 0)
				t->Error({"expected 0 closes"});

			testSysPtr->writeFileNoError(configFileName, R"({
				"compilerOptions": { "composite": true },
				"contentMappers": [{ "package": "mapper-b", "extensions": [".vue"] }]
			})");
			testSysPtr->mockWatchBackend->SendEvents(
			    {fswatch::Event{fswatch::EventKind::EventUpdate,
			                    configFileName}});
			result.Watcher->DoCycle();

			if (spawner->spawns.load() != 2)
				t->Error({"expected 2 spawns"});
			if (spawner->closes.load() != 1)
				t->Error({"expected 1 close"});
			closed->take();

			testSysPtr->writeFileNoError(
			    configFileName,
			    R"({ "compilerOptions": { "composite": true } })");
			testSysPtr->mockWatchBackend->SendEvents(
			    {fswatch::Event{fswatch::EventKind::EventUpdate,
			                    configFileName}});
			result.Watcher->DoCycle();

			if (spawner->closes.load() != 2)
				t->Error({"expected 2 closes"});
			closed->take();

			testSysPtr->writeFileNoError(configFileName, R"({
				"compilerOptions": { "composite": true },
				"contentMappers": [{ "package": "mapper-a", "extensions": [".vue"] }]
			})");
			testSysPtr->mockWatchBackend->SendEvents(
			    {fswatch::Event{fswatch::EventKind::EventUpdate,
			                    configFileName}});
			result.Watcher->DoCycle();

			if (spawner->spawns.load() != 3)
				t->Error({"expected 3 spawns"});
			if (spawner->closes.load() != 2)
				t->Error({"expected 2 closes"});
			cancel();
			bool closedAfterCancellation = closed->take(1000);
			if (!closedAfterCancellation)
				t->Error(
				    {"content mapper process was not closed after "
				     "cancellation"});
			if (spawner->closes.load() != 3)
				t->Error({"expected 3 closes"});
		});
	}
}
REGISTER_UNIT_TEST("tsctests.TestContentMapperWatchLifecycle",
                   TestContentMapperWatchLifecycle);

void TestContentMapperSupplementalCollisionWatch(T* t) {
	t->Parallel();
	const std::string supplementalFileName =
	    "/home/src/workspaces/project/app.vue.0.ts";
	auto* input = new tscInput{.files = FileMap{
	    {"/home/src/workspaces/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "noLib": true },
			"contentMappers": [{ "package": "mapper", "extensions": [".vue"] }]
		})")},
	    {"/home/src/workspaces/project/app.vue",
	     "declare const value: number;"},
	    {"/home/src/workspaces/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::SupplementalMapper)},
	}};
	auto testSys = newTestSys(input, false);
	auto* spawner = new recordingContentMapperSpawner{
	    .inner = contentmappertest::NewSpawner()};
	auto* sys =
	    new recordingContentMapperSystem(std::move(testSys), spawner);
	auto* testSysPtr = sys->inner.get();
	auto cc = gostd::contextWithCancel(t->Context());
	auto ctx = cc.first;
	auto cancel = cc.second;
	t->Cleanup([cancel] { cancel(); });

	auto result = execute::CommandLine(
	    ctx, sys, {"--watch", "--runExternalCode"}, testSysPtr);
	auto* w = static_cast<execute::Watcher*>(result.Watcher);
	int fullBuilds = w->FullBuilds();

	testSysPtr->writeFileNoError(supplementalFileName, "export {};\n");
	testSysPtr->mockWatchBackend->SendEvents(
	    {fswatch::Event{fswatch::EventKind::EventUpdate,
	                    supplementalFileName}});
	w->DoCycle();
	if (w->FullBuilds() != fullBuilds + 1)
		t->Error({"creating a supplemental filename collision must force "
		          "a full rebuild"});

	if (auto err =
	        testSysPtr->fsFromFileMap()->Remove(supplementalFileName))
		t->Fatal({err.str()});
	testSysPtr->mockWatchBackend->SendEvents(
	    {fswatch::Event{fswatch::EventKind::EventUpdate,
	                    supplementalFileName}});
	w->DoCycle();
	if (w->FullBuilds() != fullBuilds + 2)
		t->Error({"removing a supplemental filename collision must force "
		          "a full rebuild"});
}
REGISTER_UNIT_TEST("tsctests.TestContentMapperSupplementalCollisionWatch",
                   TestContentMapperSupplementalCollisionWatch);

void TestDynamicContentMapperWatchDependency(T* t) {
	t->Parallel();
	const std::string mapperConfigFileName =
	    "/home/src/workspaces/project/mapper.config.json";
	auto* input = new tscInput{.files = FileMap{
	    {"/home/src/workspaces/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "composite": true },
			"contentMappers": [{ "package": "mapper", "extensions": [".vue"] }]
		})")},
	    {mapperConfigFileName, R"({ "version": 1 })"},
	    {"/home/src/workspaces/project/app.vue", "export const app = 1;"},
	    {"/home/src/workspaces/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::DynamicVerbatimMapper)},
	}};
	auto testSys = newTestSys(input, false);
	auto* lifecycle = new contentmappertest::ProjectLifecycle();
	auto* spawner = new recordingContentMapperSpawner{
	    .inner =
	        contentmappertest::NewSpawnerWithProjectLifecycle(lifecycle)};
	auto* sys =
	    new recordingContentMapperSystem(std::move(testSys), spawner);
	auto* testSysPtr = sys->inner.get();
	auto cc = gostd::contextWithCancel(t->Context());
	auto ctx = cc.first;
	auto cancel = cc.second;
	t->Cleanup([cancel] { cancel(); });

	auto result = execute::CommandLine(
	    ctx, sys, {"--watch", "--runExternalCode"}, testSysPtr);
	auto* w = static_cast<execute::Watcher*>(result.Watcher);
	int fullBuilds = w->FullBuilds();

	testSysPtr->writeFileNoError(mapperConfigFileName,
	                             R"({ "version": 2 })");
	testSysPtr->mockWatchBackend->SendEvents(
	    {fswatch::Event{fswatch::EventKind::EventUpdate,
	                    mapperConfigFileName}});
	w->DoCycle();

	if (w->FullBuilds() != fullBuilds + 1)
		t->Error({"expected full rebuild"});
	if (lifecycle->Opens.load() != 2)
		t->Error({"expected 2 opens"});
	if (lifecycle->Closes.load() != 1)
		t->Error({"expected 1 close"});
	if (spawner->spawns.load() != 1)
		t->Error({"expected 1 spawn"});
	if (spawner->closes.load() != 0)
		t->Error({"expected 0 closes"});
}
REGISTER_UNIT_TEST("tsctests.TestDynamicContentMapperWatchDependency",
                   TestDynamicContentMapperWatchDependency);

void TestContentMapperMixedWatchBatchForcesFullRebuild(T* t) {
	t->Parallel();
	const std::string mappedFileName =
	    "/home/src/workspaces/project/app.vue";
	const std::string mainFileName =
	    "/home/src/workspaces/project/main.ts";
	auto* input = new tscInput{.files = FileMap{
	    {"/home/src/workspaces/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "noLib": true },
			"contentMappers": [{ "package": "mapper", "extensions": [".vue"] }]
		})")},
	    {mappedFileName, "export const marker = 1 as const;"},
	    {mainFileName,
	     "import { marker } from \"./app.vue\"; const check: 1 = marker;"},
	    {"/home/src/workspaces/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(contentmappertest::VerbatimMapper)},
	}};
	auto testSys = newTestSys(input, false);
	testSys->currentWrite->Reset();
	auto* sys = new recordingContentMapperSystem(
	    std::move(testSys),
	    new recordingContentMapperSpawner{
	        .inner = contentmappertest::NewSpawner()});
	auto* testSysPtr = sys->inner.get();
	auto cc = gostd::contextWithCancel(t->Context());
	auto ctx = cc.first;
	auto cancel = cc.second;
	t->Cleanup([cancel] { cancel(); });

	auto result = execute::CommandLine(
	    ctx, sys,
	    {"--watch", "--pretty", "false", "--runExternalCode"}, testSysPtr);
	auto* w = static_cast<execute::Watcher*>(result.Watcher);
	int fastBuilds = w->FastPathBuilds(), fullBuilds = w->FullBuilds();
	testSysPtr->currentWrite->Reset();
	testSysPtr->writeFileNoError(mappedFileName,
	                             "export const marker = 2 as const;");
	testSysPtr->writeFileNoError(
	    mainFileName,
	    "import { marker } from \"./app.vue\"; const check: 2 = marker;");
	testSysPtr->mockWatchBackend->SendEvents({
	    fswatch::Event{fswatch::EventKind::EventUpdate, mappedFileName},
	    fswatch::Event{fswatch::EventKind::EventUpdate, mainFileName},
	});
	w->DoCycle();

	if (w->FullBuilds() != fullBuilds + 1)
		t->Error({"expected full rebuild"});
	if (w->FastPathBuilds() != fastBuilds)
		t->Error({"expected no fast-path builds"});
	if (testSysPtr->currentWrite->String().find(
	        "Type '1' is not assignable to type '2'") !=
	    std::string::npos)
		t->Error({testSysPtr->currentWrite->String()});
}
REGISTER_UNIT_TEST("tsctests.TestContentMapperMixedWatchBatchForcesFullRebuild",
                   TestContentMapperMixedWatchBatchForcesFullRebuild);

void TestDynamicContentMapperBuildWatchDependency(T* t) {
	t->Parallel();
	const std::string mapperConfigFileName =
	    "/home/src/workspaces/project/mapper.config.json";
	auto* input = new tscInput{.files = FileMap{
	    {"/home/src/workspaces/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "composite": true },
			"contentMappers": [{ "package": "mapper", "extensions": [".vue"] }]
		})")},
	    {mapperConfigFileName, R"({ "version": 1 })"},
	    {"/home/src/workspaces/project/app.vue", "export const app = 1;"},
	    {"/home/src/workspaces/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::DynamicVerbatimMapper)},
	}};
	auto testSys = newTestSys(input, false);
	auto* lifecycle = new contentmappertest::ProjectLifecycle();
	auto* spawner = new recordingContentMapperSpawner{
	    .inner =
	        contentmappertest::NewSpawnerWithProjectLifecycle(lifecycle)};
	auto* sys =
	    new recordingContentMapperSystem(std::move(testSys), spawner);
	auto* testSysPtr = sys->inner.get();
	auto cc = gostd::contextWithCancel(t->Context());
	auto ctx = cc.first;
	auto cancel = cc.second;
	t->Cleanup([cancel] { cancel(); });

	auto result = execute::CommandLine(
	    ctx, sys, {"--build", "--watch", "--runExternalCode"}, testSysPtr);
	if (lifecycle->Opens.load() != 1)
		t->Error({"expected 1 open"});

	testSysPtr->writeFileNoError(mapperConfigFileName,
	                             R"({ "version": 2 })");
	testSysPtr->mockWatchBackend->SendEvents(
	    {fswatch::Event{fswatch::EventKind::EventUpdate,
	                    mapperConfigFileName}});
	result.Watcher->DoCycle();

	if (lifecycle->Opens.load() != 2)
		t->Error({"expected 2 opens"});
	if (lifecycle->Closes.load() != 1)
		t->Error({"expected 1 close"});
	if (spawner->spawns.load() != 1)
		t->Error({"expected 1 spawn"});
	if (spawner->closes.load() != 0)
		t->Error({"expected 0 closes"});
}
REGISTER_UNIT_TEST("tsctests.TestDynamicContentMapperBuildWatchDependency",
                   TestDynamicContentMapperBuildWatchDependency);

void TestContentMapperBuildWatchSymlinkedManifestChange(T* t) {
	t->Parallel();
	const std::string manifestTarget =
	    "/home/src/workspaces/mapper/package.json";
	auto* input = new tscInput{.files = FileMap{
	    {"/home/src/workspaces/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "composite": true },
			"contentMappers": [{ "package": "mapper", "extensions": [".vue"] }]
		})")},
	    {"/home/src/workspaces/project/app.vue", "export const app = 1;"},
	    {"/home/src/workspaces/project/node_modules/mapper",
	     vfs::vfstest::Symlink("/home/src/workspaces/mapper")},
	    {manifestTarget,
	     contentmappertest::PackageJSON(contentmappertest::VerbatimMapper)},
	}};
	auto testSys = newTestSys(input, false);
	auto* spawner = new recordingContentMapperSpawner{
	    .inner = contentmappertest::NewSpawner()};
	auto* sys =
	    new recordingContentMapperSystem(std::move(testSys), spawner);
	auto* testSysPtr = sys->inner.get();
	auto cc = gostd::contextWithCancel(t->Context());
	auto ctx = cc.first;
	auto cancel = cc.second;
	t->Cleanup([cancel] { cancel(); });

	auto result = execute::CommandLine(
	    ctx, sys, {"--build", "--watch", "--runExternalCode"}, testSysPtr);
	if (spawner->spawns.load() != 1)
		t->Error({"expected 1 spawn"});
	if (spawner->closes.load() != 0)
		t->Error({"expected 0 closes"});

	std::string updatedManifest = stringsReplace(
	    contentmappertest::PackageJSON(contentmappertest::VerbatimMapper),
	    "\"version\": \"1.0.0\"", "\"version\": \"2.0.0\"", 1);
	testSysPtr->writeFileNoError(manifestTarget, updatedManifest);
	testSysPtr->mockWatchBackend->SendEvents(
	    {fswatch::Event{fswatch::EventKind::EventUpdate, manifestTarget}});
	result.Watcher->DoCycle();

	if (spawner->spawns.load() != 2)
		t->Error({"expected 2 spawns"});
	if (spawner->closes.load() != 1)
		t->Error({"expected 1 close"});
}
REGISTER_UNIT_TEST(
    "tsctests.TestContentMapperBuildWatchSymlinkedManifestChange",
    TestContentMapperBuildWatchSymlinkedManifestChange);

void TestContentMapperBuildWatchSymlinkedManifestDelete(T* t) {
	t->Parallel();
	const std::string manifestTarget =
	    "/home/src/workspaces/mapper/package.json";
	auto* input = new tscInput{.files = FileMap{
	    {"/home/src/workspaces/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "composite": true },
			"contentMappers": [{ "package": "mapper", "extensions": [".vue"] }]
		})")},
	    {"/home/src/workspaces/project/app.vue", "export const app = 1;"},
	    {"/home/src/workspaces/project/node_modules/mapper",
	     vfs::vfstest::Symlink("/home/src/workspaces/mapper")},
	    {manifestTarget,
	     contentmappertest::PackageJSON(contentmappertest::VerbatimMapper)},
	}};
	auto testSys = newTestSys(input, false);
	auto* spawner = new recordingContentMapperSpawner{
	    .inner = contentmappertest::NewSpawner()};
	auto* sys =
	    new recordingContentMapperSystem(std::move(testSys), spawner);
	auto* testSysPtr = sys->inner.get();
	auto cc = gostd::contextWithCancel(t->Context());
	auto ctx = cc.first;
	auto cancel = cc.second;
	t->Cleanup([cancel] { cancel(); });

	auto result = execute::CommandLine(
	    ctx, sys, {"--build", "--watch", "--runExternalCode"}, testSysPtr);
	if (spawner->spawns.load() != 1)
		t->Error({"expected 1 spawn"});
	if (spawner->closes.load() != 0)
		t->Error({"expected 0 closes"});

	testSysPtr->clearOutput();
	if (auto err = testSysPtr->fsFromFileMap()->Remove(manifestTarget))
		t->Fatal({err.str()});
	testSysPtr->mockWatchBackend->SendEvents(
	    {fswatch::Event{fswatch::EventKind::EventDelete, manifestTarget}});
	result.Watcher->DoCycle();

	if (spawner->spawns.load() != 1)
		t->Error({"expected 1 spawn"});
	if (spawner->closes.load() != 1)
		t->Error({"expected 1 close"});
	if (testSysPtr->currentWrite->String().find(
	        "The content mapper package 'mapper' could not be resolved.") ==
	    std::string::npos)
		t->Error({testSysPtr->currentWrite->String()});
}
REGISTER_UNIT_TEST(
    "tsctests.TestContentMapperBuildWatchSymlinkedManifestDelete",
    TestContentMapperBuildWatchSymlinkedManifestDelete);

void TestContentMapperBuildWatchSharedLifecycle(T* t) {
	t->Parallel();
	const std::string mapperConfig = R"({
		"compilerOptions": { "composite": true },
		"contentMappers": [{ "package": "mapper", "extensions": [".vue"] }]
	})";
	auto* input = new tscInput{.files = FileMap{
	    {"/home/src/workspaces/project/tsconfig.json",
	     std::string(R"({
			"files": [],
			"references": [{ "path": "a" }, { "path": "b" }]
		})")},
	    {"/home/src/workspaces/project/a/tsconfig.json", mapperConfig},
	    {"/home/src/workspaces/project/a/app.vue", "export const a = 1;"},
	    {"/home/src/workspaces/project/b/tsconfig.json", mapperConfig},
	    {"/home/src/workspaces/project/b/app.vue", "export const b = 1;"},
	    {"/home/src/workspaces/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(contentmappertest::VerbatimMapper)},
	}};
	auto testSys = newTestSys(input, false);
	auto* spawner = new recordingContentMapperSpawner{
	    .inner = contentmappertest::NewSpawner()};
	auto* sys =
	    new recordingContentMapperSystem(std::move(testSys), spawner);
	auto* testSysPtr = sys->inner.get();
	auto cc = gostd::contextWithCancel(t->Context());
	auto ctx = cc.first;
	auto cancel = cc.second;
	t->Cleanup([cancel] { cancel(); });

	auto result = execute::CommandLine(
	    ctx, sys, {"--build", "--watch", "--runExternalCode"}, testSysPtr);
	if (result.Watcher == nullptr)
		t->Fatal({"expected non-nil Watcher"});
	if (spawner->spawns.load() != 1)
		t->Error({"expected 1 spawn"});
	if (spawner->closes.load() != 0)
		t->Error({"expected 0 closes"});

	for (const char* project : {"a", "b"}) {
		std::string configFileName =
		    "/home/src/workspaces/project/" + std::string(project) +
		    "/tsconfig.json";
		testSysPtr->writeFileNoError(
		    configFileName,
		    R"({ "compilerOptions": { "composite": true } })");
		testSysPtr->mockWatchBackend->SendEvents(
		    {fswatch::Event{fswatch::EventKind::EventUpdate,
		                    configFileName}});
		result.Watcher->DoCycle();
		if (std::string(project) == "a") {
			if (spawner->closes.load() != 0)
				t->Error({"expected 0 closes"});
		} else {
			if (spawner->closes.load() != 1)
				t->Error({"expected 1 close"});
		}
	}
}
REGISTER_UNIT_TEST("tsctests.TestContentMapperBuildWatchSharedLifecycle",
                   TestContentMapperBuildWatchSharedLifecycle);

} // namespace
} // namespace tsc
