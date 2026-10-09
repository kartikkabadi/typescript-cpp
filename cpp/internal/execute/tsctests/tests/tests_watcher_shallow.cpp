// watch_shallow.cpp — port of
// tsc/internal/execute/tsctests/watch_shallow_test.go.
#include <memory>
#include <string>

#include "internal/execute/execute.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/execute/watcher.h"
#include "internal/fswatch/fswatch.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc {
namespace {
using namespace ::tsc::execute::tsctests;

using gostd::testing::T;

// shallowProjectFiles — watch_shallow_test.go: a project close to the
// filesystem root (a common Docker WORKDIR) that imports a file from a
// sibling directory which is not part of "include", plus a bare import
// whose failed lookups walk up to /node_modules.
FileMap shallowProjectFiles(std::string_view compilerOptions) {
	return FileMap{
	    {"/app/tsconfig.json", std::string{"{\"compilerOptions\":{"} +
	                               std::string(compilerOptions) +
	                               "\"rootDir\":\"..\",\"outDir\":\"out\",\"noLib\":true},\"include\":[\"*.ts\"]}"},
	    {"/app/index.ts",
	     "import { s } from \"../shared/s\";\n"
	     "// @ts-ignore\n"
	     "import \"missing-package\";\n"
	     "export const x = s;"},
	    {"/shared/s.ts", "export const s = 1;"},
	};
}

// isWatched — watch_shallow_test.go: the mock keeps closed watches in
// Dirs, so presence alone is not enough.
bool isWatched(TestSys* sys, const std::string& dir) {
	auto it = sys->mockWatchBackend->Dirs.find(dir);
	return it != sys->mockWatchBackend->Dirs.end() && !it->second->Closed;
}

void assertShallowProjectWatches(T* t, TestSys* sys) {
	t->Helper();
	if (!isWatched(sys, "/app")) {
		t->Error({"the tsconfig directory /app must be watched"});
	}
	if (!isWatched(sys, "/shared")) {
		t->Error(
		    {"the directory of the imported program file /shared/s.ts must be watched"});
	}
	// Failed lookups of "missing-package" reach /node_modules. They must
	// not turn into a watch on /.
	if (isWatched(sys, "/")) {
		t->Error({"/ must never be watched"});
	}
}

// editShallowProjectFiles — watch_shallow_test.go.
void editShallowProjectFiles(T* t, TestSys* sys, etsc::Watcher* w) {
	t->Helper();
	auto fs = sys->fsFromFileMap();

	sys->writeFileNoError("/shared/s.ts", "export const s = 2;");
	sys->mockWatchBackend->SendEvents(
	    {fswatch::Event{fswatch::EventKind::EventUpdate, "/shared/s.ts"}});
	w->DoCycle();
	auto out = fs->ReadFile("/app/out/shared/s.js");
	if (out.first.find("s = 2") == std::string::npos) {
		t->Errorf("editing /shared/s.ts must rebuild, got:\n%s", {out.first});
	}

	sys->writeFileNoError(
	    "/app/index.ts",
	    "import { s } from \"../shared/s\"; export const y = s;");
	sys->mockWatchBackend->SendEvents(
	    {fswatch::Event{fswatch::EventKind::EventUpdate, "/app/index.ts"}});
	w->DoCycle();
	out = fs->ReadFile("/app/out/app/index.js");
	if (out.first.find("y = ") == std::string::npos) {
		t->Errorf("editing /app/index.ts must rebuild, got:\n%s", {out.first});
	}
}

// TestWatchShallowProjectWithImportedFile — watch_shallow_test.go:
// tsc --watch rebuilds a project near the filesystem root, for its own
// files and for a file it imports from outside "include".
void TestWatchShallowProjectWithImportedFile(T* t) {
	t->Parallel();
	auto sys = newTestSys(new tscInput{
	    .files = shallowProjectFiles(""),
	    .cwd = "/app",
	}, false);
	auto* sysPtr = sys.get();
	auto result = execute::CommandLine(gostd::contextBackground(), sysPtr,
	                                   {"--watch"}, sysPtr);
	if (result.Watcher == nullptr) {
		t->Fatal({"expected Watcher to be non-nil in watch mode"});
	}
	sys.release();

	assertShallowProjectWatches(t, sysPtr);
	editShallowProjectFiles(t, sysPtr, result.Watcher);
}
REGISTER_UNIT_TEST("tsctests.TestWatchShallowProjectWithImportedFile",
                   TestWatchShallowProjectWithImportedFile);

// TestBuildWatchShallowProjectWithImportedFile — watch_shallow_test.go:
// the tsc -b --watch variant.
void TestBuildWatchShallowProjectWithImportedFile(T* t) {
	t->Parallel();
	auto sys = newTestSys(new tscInput{
	    .files = shallowProjectFiles("\"composite\":true,"),
	    .cwd = "/app",
	}, false);
	auto* sysPtr = sys.get();
	auto result = execute::CommandLine(gostd::contextBackground(), sysPtr,
	                                   {"--build", "--watch"}, sysPtr);
	if (result.Watcher == nullptr) {
		t->Fatal({"expected Watcher to be non-nil in watch mode"});
	}
	sys.release();

	assertShallowProjectWatches(t, sysPtr);
	editShallowProjectFiles(t, sysPtr, result.Watcher);
}
REGISTER_UNIT_TEST("tsctests.TestBuildWatchShallowProjectWithImportedFile",
                   TestBuildWatchShallowProjectWithImportedFile);

// shallowRootFileProjectFiles — watch_shallow_test.go: a project in /app
// whose "files" list a root file in the sibling directory /shared.
FileMap shallowRootFileProjectFiles(std::string_view compilerOptions) {
	return FileMap{
	    {"/app/tsconfig.json", std::string{"{\"compilerOptions\":{"} +
	                               std::string(compilerOptions) +
	                               "\"rootDir\":\"..\",\"outDir\":\"out\",\"noLib\":true},\"files\":[\"index.ts\",\"../shared/root.ts\"]}"},
	    {"/app/index.ts", "export const x = 1;"},
	    {"/shared/root.ts", "export const r = 1;"},
	};
}

// deleteAndRecreateShallowRootFile — watch_shallow_test.go: deletes
// /shared/root.ts and writes it back. While the file is missing it is not
// part of the program, but it is still a root file, so /shared must stay
// watched for the rebuild on recreation.
void deleteAndRecreateShallowRootFile(T* t, TestSys* sys,
                                      etsc::Watcher* w) {
	t->Helper();
	auto fs = sys->fsFromFileMap();
	if (!isWatched(sys, "/shared")) {
		t->Error(
		    {"the directory of the root file /shared/root.ts must be watched"});
	}

	sys->removeNoError("/shared/root.ts");
	sys->mockWatchBackend->SendEvents(
	    {fswatch::Event{fswatch::EventKind::EventDelete, "/shared/root.ts"}});
	w->DoCycle();
	if (!isWatched(sys, "/shared")) {
		t->Error(
		    {"/shared must stay watched while the root file /shared/root.ts is missing"});
	}
	if (isWatched(sys, "/")) {
		t->Error({"/ must never be watched"});
	}

	sys->writeFileNoError("/shared/root.ts", "export const r = 2;");
	sys->mockWatchBackend->SendEvents(
	    {fswatch::Event{fswatch::EventKind::EventUpdate, "/shared/root.ts"}});
	w->DoCycle();
	auto out = fs->ReadFile("/app/out/shared/root.js");
	if (out.first.find("r = 2") == std::string::npos) {
		t->Errorf("recreating /shared/root.ts must rebuild, got:\n%s",
		          {out.first});
	}
}

// TestWatchShallowProjectRecreatedRootFile — watch_shallow_test.go:
// tsc --watch rebuilds when a root file near the filesystem root is
// deleted and created again.
void TestWatchShallowProjectRecreatedRootFile(T* t) {
	t->Parallel();
	auto sys = newTestSys(new tscInput{
	    .files = shallowRootFileProjectFiles(""),
	    .cwd = "/app",
	}, false);
	auto* sysPtr = sys.get();
	auto result = execute::CommandLine(gostd::contextBackground(), sysPtr,
	                                   {"--watch"}, sysPtr);
	if (result.Watcher == nullptr) {
		t->Fatal({"expected Watcher to be non-nil in watch mode"});
	}
	sys.release();

	deleteAndRecreateShallowRootFile(t, sysPtr, result.Watcher);
}
REGISTER_UNIT_TEST("tsctests.TestWatchShallowProjectRecreatedRootFile",
                   TestWatchShallowProjectRecreatedRootFile);

// TestBuildWatchShallowProjectRecreatedRootFile — watch_shallow_test.go:
// the tsc -b --watch variant.
void TestBuildWatchShallowProjectRecreatedRootFile(T* t) {
	t->Parallel();
	auto sys = newTestSys(new tscInput{
	    .files = shallowRootFileProjectFiles("\"composite\":true,"),
	    .cwd = "/app",
	}, false);
	auto* sysPtr = sys.get();
	auto result = execute::CommandLine(gostd::contextBackground(), sysPtr,
	                                   {"--build", "--watch"}, sysPtr);
	if (result.Watcher == nullptr) {
		t->Fatal({"expected Watcher to be non-nil in watch mode"});
	}
	sys.release();

	deleteAndRecreateShallowRootFile(t, sysPtr, result.Watcher);
}
REGISTER_UNIT_TEST("tsctests.TestBuildWatchShallowProjectRecreatedRootFile",
                   TestBuildWatchShallowProjectRecreatedRootFile);

}  // namespace
}  // namespace tsc
