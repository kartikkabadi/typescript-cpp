// tests_resolver.cpp — port of tsc/internal/module/resolver_test.go.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/module/resolver.h"
#include "internal/module/util.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;

namespace {

struct resolutionHostStub : module::ResolutionHost {
	std::shared_ptr<vfs::FS> fs;
	std::string cwd;

	resolutionHostStub(std::shared_ptr<vfs::FS> fs, std::string cwd)
	    : fs(std::move(fs)), cwd(std::move(cwd)) {}

	bool FileExists(std::string_view path) override {
		return fs->FileExists(std::string(path));
	}
	bool DirectoryExists(std::string_view path) override {
		return fs->DirectoryExists(std::string(path));
	}
	std::optional<std::string> ReadFile(std::string_view path) override {
		auto [content, ok] = fs->ReadFile(std::string(path));
		if (!ok) {
			return std::nullopt;
		}
		return content;
	}
	std::string Realpath(std::string_view path) override {
		return fs->Realpath(std::string(path));
	}
	std::string GetCurrentDirectory() override { return cwd; }
	bool UseCaseSensitiveFileNames() override {
		return fs->UseCaseSensitiveFileNames();
	}
	AccessibleEntries GetAccessibleEntries(
	    std::string_view path) override {
		vfs::Entries e = fs->GetAccessibleEntries(std::string(path));
		return {std::move(e.files), std::move(e.directories),
		        std::move(e.symlinks)};
	}
};

// gate mirrors `chan struct{}` released by close(): wait() blocks until
// close() is called.
struct gate {
	std::mutex mu;
	std::condition_variable cv;
	bool closed = false;

	void close() {
		{
			std::lock_guard<std::mutex> l(mu);
			closed = true;
		}
		cv.notify_all();
	}

	void wait() {
		std::unique_lock<std::mutex> l(mu);
		cv.wait(l, [&] { return closed; });
	}
};

// arrival mirrors a channel the test waits on (close() / buffered send).
struct arrival {
	std::mutex mu;
	std::condition_variable cv;
	int count = 0;

	void send() {
		{
			std::lock_guard<std::mutex> l(mu);
			count++;
		}
		cv.notify_all();
	}

	void close() { send(); }

	// recv waits for one arrival; recv(2) waits for two.
	void waitFor(int n = 1) {
		std::unique_lock<std::mutex> l(mu);
		cv.wait(l, [&] { return count >= n; });
		count -= n;
	}
};

// waitForSignal mirrors the Go helper: converts a deadlock into a
// deterministic failure (Go relies on t.Context()/test timeout).
void waitForSignal(T* t, const std::function<void()>& wait,
                   const char* description) {
	std::atomic<bool> done{false};
	std::thread w([&] {
		wait();
		done.store(true);
	});
	for (int i = 0; i < 600 && !done.load(); i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	if (!done.load()) {
		w.detach();
		t->Fatalf("timed out waiting for %s", {description});
	}
	w.join();
}

// blockingFS wraps a vfs::FS and forces FileExists calls for `targetPath`
// to block on `gate` until released. Each caller sends on `arrived` when it
// reaches the gate.
struct blockingFS : vfs::FS {
	std::shared_ptr<vfs::FS> inner;
	std::string targetPath;
	gate g;
	arrival arrived;

	bool UseCaseSensitiveFileNames() override {
		return inner->UseCaseSensitiveFileNames();
	}
	bool FileExists(const std::string& path) override {
		if (path == targetPath) {
			arrived.send();
			g.wait();
		}
		return inner->FileExists(path);
	}
	std::pair<std::string, bool> ReadFile(
	    const std::string& path) override {
		return inner->ReadFile(path);
	}
	vfs::Error WriteFile(const std::string& path,
	                const std::string& data) override {
		return inner->WriteFile(path, data);
	}
	vfs::Error AppendFile(const std::string& path,
	                 const std::string& data) override {
		return inner->AppendFile(path, data);
	}
	vfs::Error Remove(const std::string& path) override {
		return inner->Remove(path);
	}
	vfs::Error Chtimes(const std::string& path, vfs::TimePoint aTime,
	              vfs::TimePoint mTime) override {
		return inner->Chtimes(path, aTime, mTime);
	}
	bool DirectoryExists(const std::string& path) override {
		return inner->DirectoryExists(path);
	}
	vfs::Entries GetAccessibleEntries(const std::string& path) override {
		return inner->GetAccessibleEntries(path);
	}
	std::shared_ptr<vfs::FileInfo> Stat(const std::string& path) override {
		return inner->Stat(path);
	}
	std::string Realpath(const std::string& path) override {
		return inner->Realpath(path);
	}
};

// flipFileExistsFS returns false for the first FileExists call to
// `targetPath`, then true for the second. Both calls signal arrival then
// block until released via their respective gates. ReadFile for the target
// path also signals arrival then blocks.
struct flipFileExistsFS : vfs::FS {
	std::shared_ptr<vfs::FS> inner;
	std::string targetPath;
	std::atomic<int32_t> callCount{0};
	arrival firstArrived;
	arrival secondArrived;
	gate firstGate;
	gate secondGate;
	arrival readArrived;
	gate readGate;

	bool UseCaseSensitiveFileNames() override {
		return inner->UseCaseSensitiveFileNames();
	}
	bool FileExists(const std::string& path) override {
		if (path == targetPath) {
			int32_t n = callCount.fetch_add(1) + 1;
			if (n == 1) {
				firstArrived.close();
				firstGate.wait();
				return false; // first caller: file not yet visible
			}
			if (n == 2) {
				secondArrived.close();
				secondGate.wait();
				return inner->FileExists(path);
			}
		}
		return inner->FileExists(path);
	}
	std::pair<std::string, bool> ReadFile(
	    const std::string& path) override {
		if (path == targetPath) {
			readArrived.close();
			readGate.wait();
		}
		return inner->ReadFile(path);
	}
	vfs::Error WriteFile(const std::string& path,
	                const std::string& data) override {
		return inner->WriteFile(path, data);
	}
	vfs::Error AppendFile(const std::string& path,
	                 const std::string& data) override {
		return inner->AppendFile(path, data);
	}
	vfs::Error Remove(const std::string& path) override {
		return inner->Remove(path);
	}
	vfs::Error Chtimes(const std::string& path, vfs::TimePoint aTime,
	              vfs::TimePoint mTime) override {
		return inner->Chtimes(path, aTime, mTime);
	}
	bool DirectoryExists(const std::string& path) override {
		return inner->DirectoryExists(path);
	}
	vfs::Entries GetAccessibleEntries(const std::string& path) override {
		return inner->GetAccessibleEntries(path);
	}
	std::shared_ptr<vfs::FileInfo> Stat(const std::string& path) override {
		return inner->Stat(path);
	}
	std::string Realpath(const std::string& path) override {
		return inner->Realpath(path);
	}
};

module::DefaultResolver* newTestResolver(
    std::shared_ptr<vfs::FS> fs) {
	// Leaked intentionally: the resolver borrows host/options for life.
	auto* host = new resolutionHostStub(fs, "/repo");
	auto* opts = new CompilerOptions();
	opts->Module = ModuleKind::ESNext;
	opts->ModuleResolution = ModuleResolutionKind::Bundler;
	opts->Target = ScriptTarget::ESNext;
	return module::NewResolver(module::ResolverOptions{
	    .Host = host,
	    .CompilerOptions = opts,
	});
}

// Regression test for https://github.com/microsoft/TypeScript/tsc/issues/3526.
//
// Resolving a node_modules import with a trailing slash (e.g. `pkg/`) must
// produce the same result as without one.
void TestResolveModuleNameTrailingSlash(T* t) {
	t->Parallel();

	auto fs = vfs::vfstest::FromMap(
	    {
	        {"/repo/node_modules/pkg/package.json",
	         R"({"name":"pkg","main":"main.js","types":"main.d.ts"})"},
	        {"/repo/node_modules/pkg/main.d.ts",
	         "export const x: number;"},
	        {"/repo/node_modules/pkg/main.js", "exports.x = 1;"},
	        {"/repo/src/file.ts", ""},
	    },
	    true);
	auto resolver = newTestResolver(fs);

	for (auto* name : {"pkg", "pkg/"}) {
		auto [r, _] = resolver->ResolveModuleName(
		    name, "/repo/src/file.ts", ModuleKind::ESNext, nullptr);
		if (!r->IsResolved()) {
			t->Errorf("%q failed to resolve", {name});
		}
	}
}

// Regression test for https://github.com/microsoft/TypeScript/tsc/issues/3526.
//
// Two goroutines resolve the same package via specifiers that differ only by
// a trailing slash (`pkg` and `pkg/`). A blocking FS holds both at the
// `FileExists` check for `package.json` — *after* each has confirmed a
// `package.json` info-cache miss but *before* either has called `Set`. When
// released, both proceed to `LoadOrStore` and one of them loses. Without the
// fix, the loser receives the winner's `InfoCacheEntry` whose
// `PackageDirectory` doesn't match its own `candidate` (because one spelling
// has a trailing slash and the other doesn't), and
// `loadNodeModuleFromDirectoryWorker`'s `ComparePaths` check skips loading
// the package's `main`/`types`. With no `index.*` present, resolution falls
// through to "unresolved" — the phantom TS2307 the issue describes. This
// test deterministically fails when the fix is reverted.
void TestResolveModuleNameTrailingSlashRace(T* t) {
	t->Parallel();

	const std::string pkgJSONPath = "/repo/node_modules/pkg/package.json";
	std::unordered_map<std::string, vfs::vfstest::MapFileInput> files = {
	    // `types` points at a file that is not discoverable through any
	    // fallback path: there is no `index.*` and no `main`. The only way
	    // to resolve `pkg` (or `pkg/`) is via the package.json `types` field
	    // inside `loadNodeModuleFromDirectoryWorker`, which is exactly the
	    // step that the bug skips when `candidate` and
	    // `packageInfo.PackageDirectory` mismatch.
	    {pkgJSONPath,
	     R"({"name":"pkg","types":"./typings/index.d.ts"})"},
	    {"/repo/node_modules/pkg/typings/index.d.ts",
	     "export const x: number;"},
	    // Distinct containing files so each `ResolveModuleName` call has a
	    // unique module-resolution-cache key.
	    {"/repo/src/a/file.ts", ""},
	    {"/repo/src/b/file.ts", ""},
	};
	auto fs = std::make_shared<blockingFS>();
	fs->inner = vfs::vfstest::FromMap(files, true);
	fs->targetPath = pkgJSONPath;
	auto resolver = newTestResolver(fs);

	struct resolutionResult {
		std::string name;
		bool resolved;
	};
	std::mutex resultsMu;
	std::vector<resolutionResult> results;
	std::vector<std::thread> threads;
	for (auto* name : {"pkg", "pkg/"}) {
		std::string containingFile = "/repo/src/a/file.ts";
		if (std::string(name).back() == '/') {
			containingFile = "/repo/src/b/file.ts";
		}
		threads.emplace_back([&, name, containingFile] {
			auto [r, _] = resolver->ResolveModuleName(
			    name, containingFile, ModuleKind::ESNext, nullptr);
			std::lock_guard<std::mutex> l(resultsMu);
			results.push_back({name, r->IsResolved()});
		});
	}

	// Wait for both goroutines to reach the FileExists gate, guaranteeing
	// both have observed a package.json info-cache miss.
	waitForSignal(t, [&] { fs->arrived.waitFor(1); },
	              "first FileExists gate arrival");
	waitForSignal(t, [&] { fs->arrived.waitFor(1); },
	              "second FileExists gate arrival");
	fs->g.close();

	for (auto& th : threads) {
		th.join();
	}
	for (auto& r : results) {
		if (!r.resolved) {
			t->Errorf("%q failed to resolve", {r.name});
		}
	}
}

// Regression test for https://github.com/microsoft/TypeScript/tsc/issues/1290.
//
// Two goroutines resolve `pkg/sub` concurrently. Both miss the package.json
// info-cache for the root package directory. A `flipFileExistsFS` forces the
// first goroutine's `FileExists` to return false (simulating the file not yet
// being visible), so it stores a nil-Contents cache entry. The second
// goroutine's `FileExists` returns true, but its `Set` call (`LoadOrStore`)
// returns the first goroutine's nil-Contents entry. Without the `Exists()`
// guard on the `typesVersions` lookup, `packageInfo.Contents.GetVersionPaths`
// dereferences nil and panics. With the guard the nil-Contents entry is
// safely skipped.
void TestResolveSubpathNilContentsRace(T* t) {
	t->Parallel();

	const std::string rootPkgJSON = "/repo/node_modules/pkg/package.json";
	std::unordered_map<std::string, vfs::vfstest::MapFileInput> files = {
	    {rootPkgJSON, R"({"name":"pkg","version":"1.0.0"})"},
	    {"/repo/node_modules/pkg/sub/index.d.ts",
	     "export declare const sub: number;"},
	    {"/repo/node_modules/pkg/sub/index.js", "exports.sub = 1;"},
	    {"/repo/src/a/file.ts", ""},
	    {"/repo/src/b/file.ts", ""},
	};
	auto fs = std::make_shared<flipFileExistsFS>();
	fs->inner = vfs::vfstest::FromMap(files, true);
	fs->targetPath = rootPkgJSON;
	auto resolver = newTestResolver(fs);

	std::atomic<bool> panicked{false};
	struct resolutionResult {
		std::string containingFile;
		bool resolved;
	};
	std::mutex resultsMu;
	std::vector<resolutionResult> results;
	std::vector<std::thread> threads;
	// Two goroutines both resolve "pkg/sub". Each calls getPackageJsonInfo
	// for the root package directory, reaching FileExists for rootPkgJSON.
	for (auto* containingFile :
	     {"/repo/src/a/file.ts", "/repo/src/b/file.ts"}) {
		threads.emplace_back([&, containingFile] {
			bool resolved = false;
			try {
				auto [r, _] = resolver->ResolveModuleName(
				    "pkg/sub", containingFile, ModuleKind::ESNext,
				    nullptr);
				resolved = r->IsResolved();
			} catch (...) {
				panicked.store(true);
			}
			std::lock_guard<std::mutex> l(resultsMu);
			results.push_back({containingFile, resolved});
		});
	}

	// Phase 1: Wait for both goroutines to reach FileExists for the root
	// package.json, guaranteeing both have observed a cache miss.
	waitForSignal(t, [&] { fs->firstArrived.waitFor(1); },
	              "first root package.json FileExists arrival");
	waitForSignal(t, [&] { fs->secondArrived.waitFor(1); },
	              "second root package.json FileExists arrival");

	// Phase 2: Release the first FileExists caller (returns false).
	// It enters the "file not found" branch and stores a nil-Contents
	// entry via Set — this is nearly instant (no ReadFile).
	fs->firstGate.close();

	// Phase 3: Release the second FileExists caller (returns true).
	// It proceeds to ReadFile, which we gate separately to ensure the
	// first goroutine's nil-Contents Set has completed.
	fs->secondGate.close();

	// Phase 4: Wait for the second goroutine to reach ReadFile, then
	// release. By this point the first goroutine has stored its
	// nil-Contents entry. The second goroutine's Set (LoadOrStore) will
	// return that stale entry.
	waitForSignal(t, [&] { fs->readArrived.waitFor(1); },
	              "root package.json ReadFile arrival");
	fs->readGate.close();

	for (auto& th : threads) {
		th.join();
	}
	if (panicked.load()) {
		t->Fatal(
		    {"resolver panicked due to nil Contents dereference in "
		     "loadModuleFromSpecificNodeModulesDirectory"});
	}
	for (auto& r : results) {
		if (!r.resolved) {
			t->Fatalf("%q failed to resolve pkg/sub",
			          {r.containingFile});
		}
	}
}

void TestParseNodeModuleFromPath(T* t) {
	t->Parallel();

	struct {
		const char* name;
		const char* path;
		bool isFolder;
		const char* want;
	} tests[] = {
	    {"file in package", "/a/node_modules/b/lib/index.d.ts", false,
	     "/a/node_modules/b"},
	    {"file in scoped package",
	     "/a/node_modules/@scope/b/lib/index.d.ts", false,
	     "/a/node_modules/@scope/b"},
	    {"folder subpath", "/a/node_modules/b/lib/File", true,
	     "/a/node_modules/b"},
	    {"folder subpath scoped", "/a/node_modules/@scope/b/lib/File",
	     true, "/a/node_modules/@scope/b"},
	    {"package root folder", "/a/node_modules/b", true,
	     "/a/node_modules/b"},
	    {"scoped package root folder", "/a/node_modules/@scope/b", true,
	     "/a/node_modules/@scope/b"},
	    // A bare scope directory has no package name; must not panic
	    // (https://github.com/microsoft/TypeScript/tsc/issues/4373).
	    {"scope-only folder", "/a/node_modules/@scope", true,
	     "/a/node_modules/@scope"},
	    {"types scope-only folder", "/a/node_modules/@types", true,
	     "/a/node_modules/@types"},
	    {"not in node_modules", "/a/src/index.ts", false, ""},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [tt](T* t) {
			t->Parallel();
			if (auto got = module::ParseNodeModuleFromPath(
			        tt.path, tt.isFolder);
			    got != tt.want) {
				t->Errorf(
				    "ParseNodeModuleFromPath(%q, %v) = %q, want %q",
				    {tt.path, tt.isFolder ? "true" : "false",
				     got, tt.want});
			}
		});
	}
}

// Regression test for https://github.com/microsoft/TypeScript/tsc/issues/4478.
//
// While resolving a package with peerDependencies, two goroutines look up
// the peer package's package.json concurrently. A `flipFileExistsFS` forces
// the first lookup to cache a nil-Contents entry and the second lookup to
// receive that stale entry from `Set`. The resolver must not dereference the
// peer package.json contents unless the entry actually exists.
void TestResolvePeerDependencyNilContentsRace(T* t) {
	t->Parallel();

	const std::string peerPkgJSON = "/repo/node_modules/peer/package.json";
	std::unordered_map<std::string, vfs::vfstest::MapFileInput> files = {
	    {"/repo/node_modules/pkg/package.json",
	     R"({"name":"pkg","version":"1.0.0","types":"index.d.ts","peerDependencies":{"peer":"*"}})"},
	    {"/repo/node_modules/pkg/index.d.ts",
	     "export declare const x: number;"},
	    {peerPkgJSON, R"({"name":"peer","version":"2.0.0"})"},
	    {"/repo/src/a/file.ts", ""},
	    {"/repo/src/b/file.ts", ""},
	};
	auto fs = std::make_shared<flipFileExistsFS>();
	fs->inner = vfs::vfstest::FromMap(files, true);
	fs->targetPath = peerPkgJSON;
	auto resolver = newTestResolver(fs);

	std::atomic<bool> panicked{false};
	struct resolutionResult {
		std::string containingFile;
		bool resolved;
	};
	std::mutex resultsMu;
	std::vector<resolutionResult> results;
	std::vector<std::thread> threads;
	for (auto* containingFile :
	     {"/repo/src/a/file.ts", "/repo/src/b/file.ts"}) {
		threads.emplace_back([&, containingFile] {
			bool resolved = false;
			try {
				auto [r, _] = resolver->ResolveModuleName(
				    "pkg", containingFile, ModuleKind::ESNext,
				    nullptr);
				resolved = r->IsResolved();
			} catch (...) {
				panicked.store(true);
			}
			std::lock_guard<std::mutex> l(resultsMu);
			results.push_back({containingFile, resolved});
		});
	}

	waitForSignal(t, [&] { fs->firstArrived.waitFor(1); },
	              "first peer package.json FileExists arrival");
	waitForSignal(t, [&] { fs->secondArrived.waitFor(1); },
	              "second peer package.json FileExists arrival");
	fs->firstGate.close();
	std::optional<resolutionResult> firstResult;
	waitForSignal(
	    t,
	    [&] {
		    for (;;) {
			    {
				    std::lock_guard<std::mutex> l(resultsMu);
				    if (!results.empty()) {
					    return;
				    }
			    }
			    std::this_thread::sleep_for(
			        std::chrono::milliseconds(10));
		    }
	    },
	    "first peer package.json lookup to finish");
	{
		std::lock_guard<std::mutex> l(resultsMu);
		firstResult = results.front();
	}
	fs->secondGate.close();
	waitForSignal(t, [&] { fs->readArrived.waitFor(1); },
	              "peer package.json ReadFile arrival");
	fs->readGate.close();

	for (auto& th : threads) {
		th.join();
	}
	if (panicked.load()) {
		t->Fatal(
		    {"resolver panicked due to nil Contents dereference in "
		     "readPackageJsonPeerDependencies"});
	}
	if (!firstResult->resolved) {
		t->Fatalf("%q failed to resolve pkg",
		          {firstResult->containingFile});
	}
	for (size_t i = 1; i < results.size(); i++) {
		if (!results[i].resolved) {
			t->Fatalf("%q failed to resolve pkg",
			          {results[i].containingFile});
		}
	}
}

} // namespace

REGISTER_UNIT_TEST("module.TestResolveModuleNameTrailingSlash",
                   TestResolveModuleNameTrailingSlash);
REGISTER_UNIT_TEST("module.TestResolveModuleNameTrailingSlashRace",
                   TestResolveModuleNameTrailingSlashRace);
REGISTER_UNIT_TEST("module.TestResolveSubpathNilContentsRace",
                   TestResolveSubpathNilContentsRace);
REGISTER_UNIT_TEST("module.TestParseNodeModuleFromPath",
                   TestParseNodeModuleFromPath);
REGISTER_UNIT_TEST("module.TestResolvePeerDependencyNilContentsRace",
                   TestResolvePeerDependencyNilContentsRace);
