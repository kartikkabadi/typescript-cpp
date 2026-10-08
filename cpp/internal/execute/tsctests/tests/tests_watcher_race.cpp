// tests_watcher_race.cpp — port of
// tsc/internal/execute/tsctests/watcher_race_test.go.
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "internal/execute/execute.h"
#include "internal/execute/tsc/compile.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/execute/watcher.h"
#include "internal/fswatch/fswatch.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc {
namespace {
using namespace ::tsc::execute::tsctests;


using gostd::testing::T;

// closedResultChannel — Go `make(chan tsc.CommandLineResult, 1)`.
struct closedResultChannel {
	std::mutex mu;
	std::condition_variable cv;
	std::optional<etsc::CommandLineResult> value;

	void post(etsc::CommandLineResult r) {
		std::lock_guard<std::mutex> lock(mu);
		value = r;
		cv.notify_one();
	}
	std::optional<etsc::CommandLineResult> take(int timeoutMs) {
		std::unique_lock<std::mutex> lock(mu);
		if (!cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
		                 [&] { return value.has_value(); })) {
			return std::nullopt;
		}
		return value;
	}
};

// waitGroup — sync.WaitGroup: launch with go(fn), join all with wait().
struct waitGroup {
	std::vector<std::thread> threads;
	void go(std::function<void()> fn) {
		threads.emplace_back(std::move(fn));
	}
	void wait() {
		for (auto& t : threads)
			t.join();
	}
};

// createTestWatcher — sets up a minimal project with a tsconfig and
// returns a Watcher ready for concurrent testing, plus the TestSys
// for file manipulation.
std::pair<execute::Watcher*, TestSys*> createTestWatcher(T* t) {
	t->Helper();
	auto* input = new tscInput{
	    .commandLineArgs = {"--watch"},
	    .files = FileMap{
	        {"/home/src/workspaces/project/a.ts", "const a: number = 1;"},
	        {"/home/src/workspaces/project/b.ts",
	         "import { a } from \"./a\"; export const b = a;"},
	        {"/home/src/workspaces/project/tsconfig.json", "{}"},
	    },
	};
	auto sys = newTestSys(input, false);
	auto* sysPtr = sys.get();
	auto result = execute::CommandLine(gostd::contextBackground(), sysPtr,
	                                   {"--watch"}, sysPtr);
	if (result.Watcher == nullptr) {
		t->Fatal({"expected Watcher to be non-nil in watch mode"});
	}
	auto* w = static_cast<execute::Watcher*>(result.Watcher);
	// Leak like the Go GC lifetime: watchers reference the sys/fs state.
	sys.release();
	return {w, sysPtr};
}

void TestWatcherConcurrentDoCycle(T* t) {
	t->Parallel();
	auto created = createTestWatcher(t);
	auto* w = created.first;
	auto* sys = created.second;

	waitGroup wg;
	for (int i = 0; i < 8; i++) {
		wg.go([w, sys, i] {
			for (int j = 0; j < 10; j++) {
				sys->fsFromFileMap()->WriteFile(
				    "/home/src/workspaces/project/a.ts",
				    gostd::sprintf("const a: number = %d;",
				                   {i * 10 + j}));
				w->DoCycle();
			}
		});
	}
	wg.wait();
}
REGISTER_UNIT_TEST("tsctests.TestWatcherConcurrentDoCycle",
                   TestWatcherConcurrentDoCycle);

void TestWatcherDoCycleWithConcurrentStateReads(T* t) {
	t->Parallel();
	auto created = createTestWatcher(t);
	auto* w = created.first;
	auto* sys = created.second;

	waitGroup wg;
	// DoCycle goroutines
	for (int i = 0; i < 4; i++) {
		wg.go([w, sys, i] {
			for (int j = 0; j < 15; j++) {
				sys->fsFromFileMap()->WriteFile(
				    "/home/src/workspaces/project/a.ts",
				    gostd::sprintf("const a: number = %d;",
				                   {i * 15 + j}));
				w->DoCycle();
			}
		});
	}
	// State reader goroutines
	for (int i = 0; i < 8; i++) {
		wg.go([w] {
			for (int j = 0; j < 50; j++) {
				w->DoCycle();
				w->DoCycle();
				w->DoCycle();
				w->DoCycle();
			}
		});
	}
	wg.wait();
}
REGISTER_UNIT_TEST("tsctests.TestWatcherDoCycleWithConcurrentStateReads",
                   TestWatcherDoCycleWithConcurrentStateReads);

void TestWatcherConcurrentFileChangesAndDoCycle(T* t) {
	t->Parallel();
	auto created = createTestWatcher(t);
	auto* w = created.first;
	auto* sys = created.second;

	waitGroup wg;
	// File creators
	for (int i = 0; i < 4; i++) {
		wg.go([sys, i] {
			for (int j = 0; j < 20; j++) {
				std::string path = gostd::sprintf(
				    "/home/src/workspaces/project/gen_%d_%d.ts",
				    {i, j});
				sys->fsFromFileMap()->WriteFile(
				    path,
				    gostd::sprintf("export const x%d_%d = %d;",
				                   {i, j, j}));
			}
		});
	}
	// File deleters
	wg.go([sys] {
		for (int j = 0; j < 20; j++) {
			sys->fsFromFileMap()->Remove(gostd::sprintf(
			    "/home/src/workspaces/project/gen_0_%d.ts", {j}));
		}
	});
	// DoCycle callers
	for (int i = 0; i < 4; i++) {
		wg.go([w] {
			for (int j = 0; j < 10; j++) {
				w->DoCycle();
			}
		});
	}
	wg.wait();
}
REGISTER_UNIT_TEST("tsctests.TestWatcherConcurrentFileChangesAndDoCycle",
                   TestWatcherConcurrentFileChangesAndDoCycle);

void TestWatcherRapidConfigChanges(T* t) {
	t->Parallel();
	auto created = createTestWatcher(t);
	auto* w = created.first;
	auto* sys = created.second;

	waitGroup wg;
	const std::vector<std::string> configs{
	    "{}",
	    R"({"compilerOptions": {"strict": true}})",
	    R"({"compilerOptions": {"target": "ES2020"}})",
	    R"({"compilerOptions": {"noEmit": true}})",
	};

	// Config modifiers + DoCycle
	for (int i = 0; i < 3; i++) {
		wg.go([w, sys, i, &configs] {
			for (int j = 0; j < 10; j++) {
				sys->fsFromFileMap()->WriteFile(
				    "/home/src/workspaces/project/tsconfig.json",
				    configs[(i + j) % configs.size()]);
				w->DoCycle();
			}
		});
	}
	// Concurrent source file modifications
	for (int i = 0; i < 2; i++) {
		wg.go([w, sys, i] {
			for (int j = 0; j < 15; j++) {
				sys->fsFromFileMap()->WriteFile(
				    "/home/src/workspaces/project/a.ts",
				    gostd::sprintf("const a: number = %d;",
				                   {i * 15 + j}));
				w->DoCycle();
			}
		});
	}
	// State readers
	for (int i = 0; i < 4; i++) {
		wg.go([w] {
			for (int j = 0; j < 30; j++) {
				w->DoCycle();
				w->DoCycle();
			}
		});
	}
	wg.wait();
}
REGISTER_UNIT_TEST("tsctests.TestWatcherRapidConfigChanges",
                   TestWatcherRapidConfigChanges);

void TestWatcherConcurrentDoCycleNoChanges(T* t) {
	t->Parallel();
	auto created = createTestWatcher(t);
	auto* w = created.first;
	auto* sys = created.second;

	waitGroup wg;
	for (int i = 0; i < 16; i++) {
		wg.go([w] {
			for (int j = 0; j < 50; j++) {
				w->DoCycle();
			}
		});
	}
	wg.wait();
}
REGISTER_UNIT_TEST("tsctests.TestWatcherConcurrentDoCycleNoChanges",
                   TestWatcherConcurrentDoCycleNoChanges);

void TestWatcherAlternatingModifyAndDoCycle(T* t) {
	t->Parallel();
	auto created = createTestWatcher(t);
	auto* w = created.first;
	auto* sys = created.second;

	waitGroup wg;
	// Writer goroutine: continuously modifies files
	wg.go([sys] {
		for (int j = 0; j < 100; j++) {
			sys->fsFromFileMap()->WriteFile(
			    "/home/src/workspaces/project/a.ts",
			    gostd::sprintf("const a: number = %d;", {j}));
		}
	});
	// Multiple DoCycle goroutines
	for (int i = 0; i < 4; i++) {
		wg.go([w] {
			for (int j = 0; j < 25; j++) {
				w->DoCycle();
			}
		});
	}
	// State reader goroutines
	for (int i = 0; i < 4; i++) {
		wg.go([w] {
			for (int j = 0; j < 100; j++) {
				w->DoCycle();
			}
		});
	}
	wg.wait();
}
REGISTER_UNIT_TEST("tsctests.TestWatcherAlternatingModifyAndDoCycle",
                   TestWatcherAlternatingModifyAndDoCycle);

void TestBuildWatchStopsWhenContextIsCancelled(T* t) {
	t->Parallel();

	auto sys = newTestSys(new tscInput{.files = FileMap{
	    {"/home/src/workspaces/project/tsconfig.json",
	     R"({"compilerOptions":{"composite":true},"files":["index.ts"]})"},
	    {"/home/src/workspaces/project/index.ts", "export const x = 1;"},
	}}, false);
	auto* sysPtr = sys.get();
	auto cc = gostd::contextWithCancel(gostd::contextBackground());
	auto ctx = cc.first;
	auto cancel = cc.second;
	cancel();

	auto* resultCh = new closedResultChannel();
	std::thread([ctx, sysPtr, resultCh] {
		resultCh->post(execute::CommandLine(
		    ctx, sysPtr,
		    {"--build", "--watch"},
		    sysPtr));
	}).detach();

	sys.release();
	// select resultCh / time.After(2s).
	auto result = resultCh->take(2000);
	if (!result.has_value()) {
		t->Fatal({"build watch did not stop after context cancellation"});
	}
	if (result->Status != etsc::ExitStatusSuccess)
		t->Error({"unexpected status"});
	if (result->Watcher == nullptr)
		t->Error({"expected non-nil Watcher"});
}

REGISTER_UNIT_TEST("tsctests.TestBuildWatchStopsWhenContextIsCancelled",
                   TestBuildWatchStopsWhenContextIsCancelled);

void TestWatcherStartsFromExistingBuildInfo(T* t) {
	t->Parallel();
	auto* input = new tscInput{
	    .files = FileMap{
	        {"/home/src/workspaces/project/index.ts",
	         "export const x: number = 1;"},
	        {"/home/src/workspaces/project/tsconfig.json",
	         R"({"compilerOptions":{"composite":true},"files":["index.ts"]})"},
	    },
	};
	auto sys = newTestSys(input, false);
	auto* sysPtr = sys.get();

	auto result = execute::CommandLine(
	    gostd::contextBackground(), sysPtr,
	    {"-p", "tsconfig.json", "--pretty", "false"}, sysPtr);
	if (result.Status != etsc::ExitStatusSuccess)
		t->Error({"unexpected status"});
	if (!sysPtr->fsFromFileMap()->FileExists(
	        "/home/src/workspaces/project/tsconfig.tsbuildinfo"))
		t->Error({"expected tsconfig.tsbuildinfo"});

	sysPtr->clearOutput();
	// Go wraps the watch startup in recover(); C++ just lets it run.
	result = execute::CommandLine(
	    gostd::contextBackground(), sysPtr,
	    {"--watch", "--noEmit", "--pretty", "false"}, sysPtr);
	if (result.Status != etsc::ExitStatusSuccess)
		t->Error({"unexpected status"});
	if (result.Watcher == nullptr)
		t->Error({"expected non-nil Watcher"});
	sys.release();
}
REGISTER_UNIT_TEST("tsctests.TestWatcherStartsFromExistingBuildInfo",
                   TestWatcherStartsFromExistingBuildInfo);

void TestWatcherRebuildsWhenJsxImportSourcePragmaChanges(T* t) {
	t->Parallel();
	auto* input = new tscInput{
	    .commandLineArgs = {"--watch"},
	    .files = FileMap{
	        {"/home/src/workspaces/project/index.tsx",
	         "/** @jsxImportSource foo */\nexport const x = <div />;"},
	        {"/home/src/workspaces/project/tsconfig.json",
	         std::string(R"({
				"compilerOptions":{"jsx":"react-jsx","module":"esnext","moduleResolution":"bundler","noEmit":true},
				"files":["index.tsx"]
			})")},
	    },
	};
	auto sys = newTestSys(input, false);
	auto* sysPtr = sys.get();
	auto result = execute::CommandLine(
	    gostd::contextBackground(), sysPtr, {"--watch", "--pretty", "false"},
	    sysPtr);
	if (result.Watcher == nullptr) {
		t->Fatal({"expected Watcher to be non-nil in watch mode"});
	}
	auto* w = static_cast<execute::Watcher*>(result.Watcher);

	sysPtr->currentWrite->Reset();
	sysPtr->fsFromFileMap()->WriteFile(
	    "/home/src/workspaces/project/index.tsx",
	    "/** @jsxImportSource bar */\nexport const x = <div />;");
	sysPtr->mockWatchBackend->SendEvents(
	    {fswatch::Event{fswatch::EventKind::EventUpdate,
	                    "/home/src/workspaces/project/index.tsx"}});
	w->DoCycle();

	std::string out = sysPtr->currentWrite->String();
	if (out.find("bar/jsx-runtime") == std::string::npos)
		t->Errorf("expected updated JSX runtime diagnostic, got: %s",
		          {out});
	if (out.find("foo/jsx-runtime") != std::string::npos)
		t->Errorf("expected stale JSX runtime diagnostic to be gone, "
		          "got: %s",
		          {out});
	sys.release();
}
REGISTER_UNIT_TEST(
    "tsctests.TestWatcherRebuildsWhenJsxImportSourcePragmaChanges",
    TestWatcherRebuildsWhenJsxImportSourcePragmaChanges);

void TestWatcherUpdateProgramFastPath(T* t) {
	t->Parallel();

	auto* input = new tscInput{
	    .commandLineArgs = {"--watch"},
	    .files = FileMap{
	        {"/home/src/workspaces/project/a.ts",
	         "export const a: number = 1;"},
	        {"/home/src/workspaces/project/b.ts",
	         "import { a } from \"./a\"; export const b = a;"},
	        {"/home/src/workspaces/project/c.ts",
	         "export const c: number = 10;"},
	        {"/home/src/workspaces/project/tsconfig.json", "{}"},
	    },
	};
	auto sys = newTestSys(input, false);
	auto* sysPtr = sys.get();
	auto result =
	    execute::CommandLine(gostd::contextBackground(), sysPtr, {"--watch"},
	                         sysPtr);
	if (result.Watcher == nullptr) {
		t->Fatal({"expected Watcher to be non-nil in watch mode"});
	}
	auto* w = static_cast<execute::Watcher*>(result.Watcher);

	// Helper to write a file, send the event, cycle, and return output
	auto editAndCycle = [&](const std::string& path,
	                        const std::string& content) {
		sysPtr->currentWrite->Reset();
		sysPtr->fsFromFileMap()->WriteFile(path, content);
		sysPtr->mockWatchBackend->SendEvents(
		    {fswatch::Event{fswatch::EventKind::EventUpdate, path}});
		w->DoCycle();
		return sysPtr->currentWrite->String();
	};

	// Body-only edit — should use UpdateProgram fast path, no errors
	int fast = w->FastPathBuilds(), full = w->FullBuilds();
	std::string out = editAndCycle("/home/src/workspaces/project/a.ts",
	                               "export const a: number = 2;");
	if (out.find("Found 0 errors") == std::string::npos)
		t->Errorf("expected 0 errors after body edit, got: %s", {out});
	if (w->FastPathBuilds() != fast + 1)
		t->Error(
		    {"body-only edit should take the UpdateProgram fast path"});
	if (w->FullBuilds() != full)
		t->Error({"body-only edit should not trigger a full rebuild"});

	// Introduce a type error via body-only edit — fast path should detect
	// it
	fast = w->FastPathBuilds();
	full = w->FullBuilds();
	out = editAndCycle("/home/src/workspaces/project/a.ts",
	                   "export const a: number = \"not a number\";");
	if (out.find("Found 0 errors") != std::string::npos)
		t->Errorf("expected errors after type error, got: %s", {out});
	if (w->FastPathBuilds() != fast + 1)
		t->Error({"type error via body edit should still take the fast "
		          "path"});
	if (w->FullBuilds() != full)
		t->Error({"type error via body edit should not trigger a full "
		          "rebuild"});

	// Fix the type error — fast path should clear it
	out = editAndCycle("/home/src/workspaces/project/a.ts",
	                   "export const a: number = 3;");
	if (out.find("Found 0 errors") == std::string::npos)
		t->Errorf("expected 0 errors after fix, got: %s", {out});

	// Change b.ts's imported module (./a -> ./c). The set of imported
	// module specifiers changes, so the file cannot be replaced in place
	// and the build must fall back to a full NewProgram rebuild.
	fast = w->FastPathBuilds();
	full = w->FullBuilds();
	out = editAndCycle("/home/src/workspaces/project/b.ts",
	                   "import { c } from \"./c\"; export const b = c;");
	if (out.find("Found 0 errors") == std::string::npos)
		t->Errorf("expected 0 errors after import change, got: %s", {out});
	if (w->FullBuilds() != full + 1)
		t->Error({"changing the imported module should fall back to a "
		          "full NewProgram rebuild"});
	if (w->FastPathBuilds() != fast)
		t->Error({"changing the imported module should not take the "
		          "fast path"});

	// Body edit after import change — should use fast path again, no
	// errors
	fast = w->FastPathBuilds();
	full = w->FullBuilds();
	out = editAndCycle("/home/src/workspaces/project/b.ts",
	                   "import { c } from \"./c\"; export const b = c + 1;");
	if (out.find("Found 0 errors") == std::string::npos)
		t->Errorf("expected 0 errors after body edit "
		          "post-import-change, got: %s",
		          {out});
	if (w->FastPathBuilds() != fast + 1)
		t->Error({"body edit after an import change should take the "
		          "fast path again"});
	if (w->FullBuilds() != full)
		t->Error({"body edit after an import change should not trigger "
		          "a full rebuild"});
	sys.release();
}
REGISTER_UNIT_TEST("tsctests.TestWatcherUpdateProgramFastPath",
                   TestWatcherUpdateProgramFastPath);

void TestWatcherOverflowForcesFullRebuild(T* t) {
	t->Parallel();

	auto* input = new tscInput{
	    .commandLineArgs = {"--watch"},
	    .files = FileMap{
	        {"/home/src/workspaces/project/index.ts",
	         "import { dep } from \"./dep\"; export const x = dep;"},
	        {"/home/src/workspaces/project/tsconfig.json",
	         std::string(R"({
				"compilerOptions":{"noLib":true,"moduleResolution":"bundler","module":"esnext","outDir":"out"},
				"files":["index.ts"]
			})")},
	    },
	};
	auto sys = newTestSys(input, false);
	auto* sysPtr = sys.get();
	auto result = execute::CommandLine(
	    gostd::contextBackground(), sysPtr, {"--watch", "--pretty", "false"},
	    sysPtr);
	if (result.Watcher == nullptr) {
		t->Fatal({"expected Watcher to be non-nil in watch mode"});
	}
	auto* w = static_cast<execute::Watcher*>(result.Watcher);
	auto fs = sysPtr->fsFromFileMap();

	// The import is initially unresolved, so the dependency is not part
	// of the program and produces no emitted output.
	if (fs->FileExists("/home/src/workspaces/project/out/dep.js"))
		t->Error(
		    {"dep.js should not exist while ./dep is unresolved"});

	// Create the missing dependency, but deliver an overflow instead of
	// a precise event (as if the create event were dropped by the
	// kernel queue).
	fs->WriteFile("/home/src/workspaces/project/dep.ts",
	              "export const dep: number = 1;");
	int full = w->FullBuilds();
	sysPtr->mockWatchBackend->SendOverflow();
	w->DoCycle();

	if (w->FullBuilds() != full + 1)
		t->Error({"overflow must force a full rebuild, not the "
		          "single-file fast path"});
	if (!fs->FileExists("/home/src/workspaces/project/out/dep.js"))
		t->Error({"overflow rebuild should rediscover the created "
		          "dependency and emit dep.js"});
	sys.release();
}
REGISTER_UNIT_TEST("tsctests.TestWatcherOverflowForcesFullRebuild",
                   TestWatcherOverflowForcesFullRebuild);

void TestWatcherNonSourceDependencyForcesFullRebuild(T* t) {
	t->Parallel();

	auto* input = new tscInput{
	    .commandLineArgs = {"--watch"},
	    .files = FileMap{
	        {"/home/src/workspaces/project/index.ts",
	         "import { dep } from \"./dep\"; export const x = dep;"},
	        {"/home/src/workspaces/project/tsconfig.json",
	         std::string(R"({
				"compilerOptions":{"noLib":true,"moduleResolution":"bundler","module":"esnext","outDir":"out"},
				"files":["index.ts"]
			})")},
	    },
	};
	auto sys = newTestSys(input, false);
	auto* sysPtr = sys.get();
	auto result = execute::CommandLine(
	    gostd::contextBackground(), sysPtr, {"--watch", "--pretty", "false"},
	    sysPtr);
	if (result.Watcher == nullptr) {
		t->Fatal({"expected Watcher to be non-nil in watch mode"});
	}
	auto* w = static_cast<execute::Watcher*>(result.Watcher);
	auto fs = sysPtr->fsFromFileMap();

	// "./dep" is unresolved initially; the failed resolution probes
	// dep.ts, recording it as a (missing) non-source dependency in
	// seenFiles.
	if (fs->FileExists("/home/src/workspaces/project/out/dep.js"))
		t->Error(
		    {"dep.js should not exist while ./dep is unresolved"});

	// In a single cycle, edit index.ts's body (a lone source-cache
	// miss) and create the previously-missing dependency. The batched
	// dependency change must reject the fast path so the new module
	// resolution is discovered.
	fs->WriteFile("/home/src/workspaces/project/index.ts",
	              "import { dep } from \"./dep\"; export const x = dep + 0;");
	fs->WriteFile("/home/src/workspaces/project/dep.ts",
	              "export const dep: number = 1;");
	int full = w->FullBuilds();
	sysPtr->mockWatchBackend->SendEvents({
	    fswatch::Event{fswatch::EventKind::EventUpdate,
	                   "/home/src/workspaces/project/index.ts"},
	    fswatch::Event{fswatch::EventKind::EventUpdate,
	                   "/home/src/workspaces/project/dep.ts"},
	});
	w->DoCycle();

	if (w->FullBuilds() != full + 1)
		t->Error({"a changed non-source dependency must force a full "
		          "rebuild, not the fast path"});
	if (!fs->FileExists("/home/src/workspaces/project/out/dep.js"))
		t->Error({"full rebuild should resolve the created dependency "
		          "and emit dep.js"});
	sys.release();
}
REGISTER_UNIT_TEST("tsctests.TestWatcherNonSourceDependencyForcesFullRebuild",
                   TestWatcherNonSourceDependencyForcesFullRebuild);

} // namespace
} // namespace tsc
