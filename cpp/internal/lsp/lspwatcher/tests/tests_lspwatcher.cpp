// Port of tsc/internal/lsp/lspwatcher/lspwatcher_test.go.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/fswatch/fswatch.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/lsp/lspwatcher/lspwatcher.h"
#include "internal/project/logging/logging.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/osvfs/osvfs.h"

namespace tsc::lsp::lspwatcher {

namespace {

using gostd::testing::T;
using namespace std::chrono_literals;

struct stderrWriter : gostd::io::Writer {
	std::pair<int, gostd::Error> write(std::string_view data) override {
		std::cerr << data;
		return {(int)data.size(), nullptr};
	}
};

stderrWriter g_stderr;
std::shared_ptr<vfs::FS> osFS() {
	return std::shared_ptr<vfs::FS>(vfs::osvfs::FS(), [](vfs::FS*) {});
}
std::shared_ptr<logging::Logger> testLogger() {
	return std::shared_ptr<logging::Logger>(logging::newLogger(&g_stderr));
}

void waitFor(T* t, const std::function<bool()>& cond, std::string_view msg) {
	t->Helper();
	auto deadline = std::chrono::steady_clock::now() + 5s;
	while (std::chrono::steady_clock::now() < deadline) {
		if (cond()) {
			return;
		}
		std::this_thread::sleep_for(20ms);
	}
	t->Fatalf("timed out waiting for %s", {std::string(msg)});
}

std::string joinDirs(const std::vector<std::string>& dirs) {
	std::string out = "[";
	for (size_t i = 0; i < dirs.size(); i++) {
		if (i) out += " ";
		out += dirs[i];
	}
	return out + "]";
}

// os.WriteFile — writes data to path, creating/truncating.
gostd::Error osWriteFile(const std::string& path, std::string_view data,
                       std::filesystem::perms perm) {
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f.is_open()) {
		return gostd::newError("open " + path);
	}
	f << data;
	f.close();
	std::error_code ec;
	std::filesystem::permissions(path, perm, ec);
	return nullptr;
}

// os.MkdirAll
gostd::Error osMkdirAll(const std::string& path) {
	std::error_code ec;
	std::filesystem::create_directories(path, ec);
	if (ec) {
		return gostd::newError("mkdirall " + path + ": " + ec.message());
	}
	return nullptr;
}

// os.Remove
gostd::Error osRemove(const std::string& path) {
	std::error_code ec;
	std::filesystem::remove(path, ec);
	if (ec) {
		return gostd::newError("remove " + path + ": " + ec.message());
	}
	return nullptr;
}

// os.RemoveAll
gostd::Error osRemoveAll(const std::string& path) {
	std::error_code ec;
	std::filesystem::remove_all(path, ec);
	if (ec) {
		return gostd::newError("removeall " + path + ": " + ec.message());
	}
	return nullptr;
}

std::shared_ptr<lsproto::FileSystemWatcher>
watcher(std::string pattern, std::shared_ptr<lsproto::WatchKind> kind = {}) {
	auto w = std::make_shared<lsproto::FileSystemWatcher>();
	w->GlobPattern.Pattern = std::make_shared<std::string>(std::move(pattern));
	w->Kind = std::move(kind);
	return w;
}

// WatchKindCreate | WatchKindChange | WatchKindDelete
std::shared_ptr<lsproto::WatchKind> allWatchKinds() {
	return std::make_shared<lsproto::WatchKind>(static_cast<lsproto::WatchKind>(
	    static_cast<uint32_t>(lsproto::WatchKindCreate) |
	    static_cast<uint32_t>(lsproto::WatchKindChange) |
	    static_cast<uint32_t>(lsproto::WatchKindDelete)));
}

// --- fakeBackend (lspwatcher_test.go:176) ---

struct fakeWatch : fswatch::Watch {
	std::function<gostd::Error()> closeFn;
	gostd::Error close() override { return closeFn(); }
	void unexported() override {}
};

struct fakeBackend : watcherBackend {
	std::mutex mu;
	std::unordered_map<std::string, fswatch::WatchCallback> byDir;
	std::unordered_map<std::string, int> closed;
	std::unordered_map<std::string, int> optCount;
	std::unordered_map<std::string, gostd::Error> failDirs;

	std::pair<std::shared_ptr<fswatch::Watch>, gostd::Error>
	watchDirectory(const std::string& dir, const fswatch::WatchCallback& fn,
	               std::vector<std::shared_ptr<fswatch::WatchOption>> opts)
	    override {
		std::lock_guard<std::mutex> lk(mu);
		if (auto it = failDirs.find(dir); it != failDirs.end()) {
			return {nullptr, it->second};
		}
		byDir[dir] = fn;
		optCount[dir] = (int)opts.size();
		auto w = std::make_shared<fakeWatch>();
		w->closeFn = [this, dir]() -> gostd::Error {
			std::lock_guard<std::mutex> lk(mu);
			byDir.erase(dir);
			closed[dir]++;
			return nullptr;
		};
		return {w, nullptr};
	}

	std::vector<std::string> watchedDirs() {
		std::lock_guard<std::mutex> lk(mu);
		std::vector<std::string> dirs;
		for (const auto& [d, _] : byDir) dirs.push_back(d);
		return dirs;
	}

	bool isWatching(const std::string& dir) {
		std::lock_guard<std::mutex> lk(mu);
		return byDir.find(dir) != byDir.end();
	}

	void emit(const std::string& dir, std::vector<fswatch::Event> events,
	          gostd::Error err) {
		fswatch::WatchCallback cb;
		{
			std::lock_guard<std::mutex> lk(mu);
			auto it = byDir.find(dir);
			cb = it != byDir.end() ? it->second : nullptr;
		}
		if (cb != nullptr) {
			cb(std::move(events), err);
		}
	}

	void emitAll(std::vector<fswatch::Event> events, gostd::Error err) {
		std::vector<fswatch::WatchCallback> cbs;
		{
			std::lock_guard<std::mutex> lk(mu);
			for (const auto& [_, cb] : byDir) cbs.push_back(cb);
		}
		for (auto& cb : cbs) {
			cb(events, err);
		}
	}
};

std::shared_ptr<fakeBackend> newFakeBackend() {
	return std::make_shared<fakeBackend>();
}

// --- blockingBackend (lspwatcher_test.go:811) ---

struct blockingBackend : watcherBackend {
	std::mutex mu;
	std::condition_variable cv;
	bool entered = false;
	bool released = false;

	std::pair<std::shared_ptr<fswatch::Watch>, gostd::Error>
	watchDirectory(const std::string& /*dir*/,
	               const fswatch::WatchCallback& /*fn*/,
	               std::vector<std::shared_ptr<fswatch::WatchOption>> /*opts*/)
	    override {
		{
			std::lock_guard<std::mutex> lk(mu);
			entered = true;
		}
		cv.notify_all();
		std::unique_lock<std::mutex> lk(mu);
		cv.wait(lk, [&] { return released; });
		auto w = std::make_shared<fakeWatch>();
		w->closeFn = [] { return gostd::Error(nullptr); };
		return {w, nullptr};
	}
};

} // namespace

// TestWatcher_CreateChangeDelete — lspwatcher_test.go:33.
void TestWatcher_CreateChangeDelete(T* t) {
	t->Parallel();
	auto dir = t->TempDir();

	std::mutex mu;
	std::vector<std::vector<std::shared_ptr<lsproto::FileEvent>>> batches;
	auto w = New(bundled::WrapFS(osFS()),
	             [&](std::vector<std::shared_ptr<lsproto::FileEvent>> changes) {
		             std::lock_guard<std::mutex> lk(mu);
		             batches.push_back(std::move(changes));
	             },
	             testLogger());
	t->Cleanup([w] { w->Close(); });

	auto pattern = tspath::normalizeSlashes(dir) + "/**/*";
	auto kind = allWatchKinds();
	if (auto err = w->WatchFiles("test", {watcher(pattern, kind)});
	    err != nullptr) {
		t->Fatal({err});
	}

	std::this_thread::sleep_for(200ms);

	auto file = tspath::combinePaths(dir, {"a.ts"});
	if (auto err = osWriteFile(file, "export {}", std::filesystem::perms::owner_all |
	                                             std::filesystem::perms::group_read |
	                                             std::filesystem::perms::others_read);
	    err != nullptr) {
		t->Fatal({err});
	}

	auto collected = [&]() -> std::vector<std::shared_ptr<lsproto::FileEvent>> {
		std::lock_guard<std::mutex> lk(mu);
		std::vector<std::shared_ptr<lsproto::FileEvent>> all;
		for (auto& b : batches) {
			all.insert(all.end(), b.begin(), b.end());
		}
		return all;
	};

	waitFor(t,
	        [&] {
		        for (auto& e : collected()) {
			        if (e->Type == lsproto::FileChangeTypeChanged) {
				        return true;
			        }
		        }
		        return false;
	        },
	        "update event");

	if (auto err = osRemove(file); err != nullptr) {
		t->Fatal({err});
	}
	waitFor(t,
	        [&] {
		        for (auto& e : collected()) {
			        if (e->Type == lsproto::FileChangeTypeDeleted) {
				        return true;
			        }
		        }
		        return false;
	        },
	        "delete event");

	if (auto err = w->UnwatchFiles("test"); err != nullptr) {
		t->Fatal({err});
	}
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_CreateChangeDelete",
                   TestWatcher_CreateChangeDelete);

// TestWatcher_KindFilter — lspwatcher_test.go:100.
void TestWatcher_KindFilter(T* t) {
	t->Parallel();
	auto dir = t->TempDir();
	auto dirNorm = tspath::normalizeSlashes(dir);

	std::mutex mu;
	std::vector<std::shared_ptr<lsproto::FileEvent>> got;
	auto backend = newFakeBackend();
	auto w = newWithBackend(bundled::WrapFS(osFS()), backend,
	                        [&](std::vector<std::shared_ptr<lsproto::FileEvent>> changes) {
		                        std::lock_guard<std::mutex> lk(mu);
		                        got.insert(got.end(), changes.begin(),
		                                   changes.end());
	                        },
	                        testLogger());
	t->Cleanup([w] { w->Close(); });

	auto pattern = dirNorm + "/**/*";
	auto kind = std::make_shared<lsproto::WatchKind>(lsproto::WatchKindDelete);
	if (auto err = w->WatchFiles("test", {watcher(pattern, kind)});
	    err != nullptr) {
		t->Fatal({err});
	}
	backend->emitAll({fswatch::Event{fswatch::EventKind::EventUpdate,
	                               tspath::combinePaths(dirNorm, {"x.ts"})},
	                  fswatch::Event{fswatch::EventKind::EventDelete,
	                               tspath::combinePaths(dirNorm, {"x.ts"})}},
	                 nullptr);

	waitFor(t,
	        [&] {
		        std::lock_guard<std::mutex> lk(mu);
		        for (auto& e : got) {
			        if (e->Type == lsproto::FileChangeTypeDeleted) {
				        return true;
			        }
		        }
		        return false;
	        },
	        "delete event");

	{
		std::lock_guard<std::mutex> lk(mu);
		for (auto& e : got) {
			if (e->Type != lsproto::FileChangeTypeDeleted) {
				t->Errorf("unexpected non-delete event: %+v", {e->Uri});
			}
		}
	}
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_KindFilter", TestWatcher_KindFilter);

// TestRootFromGlob — lspwatcher_test.go:150.
void TestRootFromGlob(T* t) {
	t->Parallel();
	struct C {
		std::string pattern;
		std::string want;
	};
	for (auto& c : std::vector<C>{
	         {"/abs/path/**/*", "/abs/path"},
	         {"/abs/path/", "/abs/path"},
	         {"/abs/path/?.ts", "/abs/path"},
	         {"/abs/path/{a,b}/*", "/abs/path"},
	     }) {
		if (auto got = rootFromGlob(c.pattern); got != c.want) {
			t->Errorf("rootFromGlob(%q) = %q, want %q",
			          {c.pattern, got, c.want});
		}
	}
}
REGISTER_UNIT_TEST("lspwatcher.TestRootFromGlob", TestRootFromGlob);

// TestWatcher_BookkeepingAndOverflow — lspwatcher_test.go:245.
void TestWatcher_BookkeepingAndOverflow(T* t) {
	t->Parallel();

	auto dir = t->TempDir();
	auto dirNorm = tspath::normalizeSlashes(dir);
	auto pattern = dirNorm + "/**/*";

	auto fs = bundled::WrapFS(osFS());
	auto backend = newFakeBackend();
	std::mutex mu;
	std::vector<std::shared_ptr<lsproto::FileEvent>> got;
	auto w = newWithBackend(fs, backend,
	                        [&](std::vector<std::shared_ptr<lsproto::FileEvent>> changes) {
		                        std::lock_guard<std::mutex> lk(mu);
		                        got.insert(got.end(), changes.begin(),
		                                   changes.end());
	                        },
	                        testLogger());

	if (auto err = w->WatchFiles("id", {watcher(pattern)}); err != nullptr) {
		t->Fatal({err});
	}
	if (auto err = w->WatchFiles("id", {watcher(pattern)}); err == nullptr) {
		t->Fatal({"expected duplicate-id error"});
	}

	backend->emitAll({fswatch::Event{fswatch::EventKind::EventUpdate,
	                               tspath::combinePaths(dirNorm, {"a.ts"})}},
	                 fswatch::ErrOverflow);
	waitFor(t, [&] {
		std::lock_guard<std::mutex> lk(mu);
		return !got.empty();
	}, "events after overflow");

	if (auto err = w->UnwatchFiles("missing"); err == nullptr) {
		t->Fatal({"expected unknown-id error"});
	}
	if (auto err = w->UnwatchFiles("id"); err != nullptr) {
		t->Fatal({err});
	}
	if (auto err = w->WatchFiles("id2", {}); err != nullptr) {
		t->Fatal({err});
	}
	w->Close();
	if (auto err = w->WatchFiles("id3", {}); err == nullptr) {
		t->Fatal({"expected closed error"});
	}
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_BookkeepingAndOverflow",
                   TestWatcher_BookkeepingAndOverflow);

// TestWatcher_NonRecursiveGlobIsNotRecursive — lspwatcher_test.go:299.
void TestWatcher_NonRecursiveGlobIsNotRecursive(T* t) {
	t->Parallel();

	auto dir = t->TempDir();
	auto dirNorm = tspath::normalizeSlashes(dir);
	if (auto err = osMkdirAll(tspath::combinePaths(dir, {"sub"}));
	    err != nullptr) {
		t->Fatal({err});
	}
	auto subNorm = tspath::normalizeSlashes(tspath::combinePaths(dir, {"sub"}));

	auto fs = bundled::WrapFS(osFS());
	auto backend = newFakeBackend();
	auto w = newWithBackend(fs, backend,
	                        [](std::vector<std::shared_ptr<lsproto::FileEvent>>) {},
	                        testLogger());
	t->Cleanup([w] { w->Close(); });

	auto recursive = dirNorm + "/**/*";
	auto nonRecursive = subNorm + "/*";
	if (auto err = w->WatchFiles("id", {watcher(recursive), watcher(nonRecursive)});
	    err != nullptr) {
		t->Fatal({err});
	}

	{
		std::lock_guard<std::mutex> lk(backend->mu);
		if (auto got = backend->optCount[dirNorm]; got != 1) {
			t->Errorf(
			    "recursive glob %q: expected 1 watch option (WithRecursive), got %d",
			    {recursive, got});
		}
		if (auto got = backend->optCount[subNorm]; got != 0) {
			t->Errorf("non-recursive glob %q: expected 0 watch options, got %d",
			          {nonRecursive, got});
		}
	}
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_NonRecursiveGlobIsNotRecursive",
                   TestWatcher_NonRecursiveGlobIsNotRecursive);

// TestWatcher_RealBackend_MissingThenCreate — lspwatcher_test.go:333.
void TestWatcher_RealBackend_MissingThenCreate(T* t) {
	t->Parallel();
	auto base = t->TempDir();
	auto fs = bundled::WrapFS(osFS());

	std::mutex mu;
	std::vector<std::vector<std::shared_ptr<lsproto::FileEvent>>> batches;
	auto w = New(fs,
	             [&](std::vector<std::shared_ptr<lsproto::FileEvent>> changes) {
		             std::lock_guard<std::mutex> lk(mu);
		             batches.push_back(std::move(changes));
	             },
	             testLogger());
	t->Cleanup([w] { w->Close(); });

	// Watch a directory that does not exist yet.
	auto target = tspath::normalizeSlashes(tspath::combinePaths(base, {"pkg"}));
	auto pattern = target + "/*";
	auto kind = allWatchKinds();
	if (auto err = w->WatchFiles("test", {watcher(pattern, kind)});
	    err != nullptr) {
		t->Fatal({err});
	}

	// Give the ancestor watch time to install.
	std::this_thread::sleep_for(200ms);

	// Create the target directory and a file inside it. The real backend's
	// ancestor watch should fire, promote to the target, and
	// the file should ultimately surface.
	if (auto err = osMkdirAll(tspath::combinePaths(base, {"pkg"}));
	    err != nullptr) {
		t->Fatal({err});
	}
	if (auto err = osWriteFile(tspath::combinePaths(base, {"pkg", "index.ts"}),
	                           "export {}", std::filesystem::perms::owner_all |
	                                            std::filesystem::perms::group_read |
	                                            std::filesystem::perms::others_read);
	    err != nullptr) {
		t->Fatal({err});
	}

	auto collected = [&]() -> std::vector<std::shared_ptr<lsproto::FileEvent>> {
		std::lock_guard<std::mutex> lk(mu);
		std::vector<std::shared_ptr<lsproto::FileEvent>> all;
		for (auto& b : batches) {
			all.insert(all.end(), b.begin(), b.end());
		}
		return all;
	};

	waitFor(t,
	        [&] {
		        for (auto& e : collected()) {
			        if (e->Uri.ends_with("/pkg/index.ts")) {
				        return true;
			        }
		        }
		        return false;
	        },
	        "event for file created in a previously-missing directory");
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_RealBackend_MissingThenCreate",
                   TestWatcher_RealBackend_MissingThenCreate);

// TestWatcher_MissingDirectoryTracksAncestor — lspwatcher_test.go:393.
void TestWatcher_MissingDirectoryTracksAncestor(T* t) {
	t->Parallel();

	auto fs = bundled::WrapFS(osFS());
	auto backend = newFakeBackend();
	auto w = newWithBackend(fs, backend,
	                        [](std::vector<std::shared_ptr<lsproto::FileEvent>>) {},
	                        testLogger());
	t->Cleanup([w] { w->Close(); });

	auto base = t->TempDir();
	auto baseNorm = tspath::normalizeSlashes(base);
	auto target = tspath::normalizeSlashes(tspath::combinePaths(base, {"pkg"}));
	auto pattern = target + "/*";

	if (auto err = w->WatchFiles("id", {watcher(pattern)}); err != nullptr) {
		t->Fatal({err});
	}

	// A missing target directory installs an ancestor watch on the nearest
	// existing ancestor (the base dir), not on the target.
	if (!backend->isWatching(baseNorm)) {
		t->Fatalf("expected ancestor watch on ancestor %q, watched: %v",
		          {baseNorm, joinDirs(backend->watchedDirs())});
	}
	if (auto dirs = backend->watchedDirs(); dirs.size() != 1) {
		t->Fatalf("expected exactly one (ancestor) watch, got %v",
		          {joinDirs(dirs)});
	}

	if (auto err = w->UnwatchFiles("id"); err != nullptr) {
		t->Fatal({err});
	}
	if (auto dirs = backend->watchedDirs(); !dirs.empty()) {
		t->Fatalf("expected all watches closed after unwatch, got %v",
		          {joinDirs(dirs)});
	}
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_MissingDirectoryTracksAncestor",
                   TestWatcher_MissingDirectoryTracksAncestor);

// TestWatcher_MissingDirectoryPromotesOnCreate — lspwatcher_test.go:429.
void TestWatcher_MissingDirectoryPromotesOnCreate(T* t) {
	t->Parallel();

	auto fs = bundled::WrapFS(osFS());
	auto backend = newFakeBackend();
	std::mutex mu;
	std::vector<std::shared_ptr<lsproto::FileEvent>> got;
	auto w = newWithBackend(fs, backend,
	                        [&](std::vector<std::shared_ptr<lsproto::FileEvent>> changes) {
		                        std::lock_guard<std::mutex> lk(mu);
		                        got.insert(got.end(), changes.begin(),
		                                   changes.end());
	                        },
	                        testLogger());
	t->Cleanup([w] { w->Close(); });

	auto base = t->TempDir();
	auto baseNorm = tspath::normalizeSlashes(base);
	auto target = tspath::normalizeSlashes(tspath::combinePaths(base, {"pkg"}));
	auto pattern = target + "/*";
	auto kind = allWatchKinds();

	if (auto err = w->WatchFiles("id", {watcher(pattern, kind)});
	    err != nullptr) {
		t->Fatal({err});
	}

	// Create the target directory with a file, then notify the ancestor watch.
	if (auto err = osMkdirAll(tspath::combinePaths(base, {"pkg"}));
	    err != nullptr) {
		t->Fatal({err});
	}
	if (auto err = osWriteFile(tspath::combinePaths(base, {"pkg", "index.ts"}),
	                           "export {}", std::filesystem::perms::owner_all |
	                                            std::filesystem::perms::group_read |
	                                            std::filesystem::perms::others_read);
	    err != nullptr) {
		t->Fatal({err});
	}
	backend->emit(baseNorm,
	              {fswatch::Event{fswatch::EventKind::EventUpdate,
	                              tspath::combinePaths(base, {"pkg"})}},
	              nullptr);

	waitFor(t, [&] { return backend->isWatching(target); },
	        "promotion to target watch");

	// Synthetic creates must cover the target dir and its immediate child so
	// the session re-resolves files created before the watch was installed.
	waitFor(
	    t,
	    [&] {
		    std::lock_guard<std::mutex> lk(mu);
		    bool sawDir = false, sawChild = false;
		    for (auto& e : got) {
			    if (e->Type != lsproto::FileChangeTypeCreated) {
				    continue;
			    }
			    const std::string& s = e->Uri;
			    if (s.ends_with("/pkg")) {
				    sawDir = true;
			    }
			    if (s.ends_with("/pkg/index.ts")) {
				    sawChild = true;
			    }
		    }
		    return sawDir && sawChild;
	    },
	    "synthetic create events for target and child");
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_MissingDirectoryPromotesOnCreate",
                   TestWatcher_MissingDirectoryPromotesOnCreate);

// TestWatcher_MultiLevelDescend — lspwatcher_test.go:493.
void TestWatcher_MultiLevelDescend(T* t) {
	t->Parallel();

	auto fs = bundled::WrapFS(osFS());
	auto backend = newFakeBackend();
	auto w = newWithBackend(fs, backend,
	                        [](std::vector<std::shared_ptr<lsproto::FileEvent>>) {},
	                        testLogger());
	t->Cleanup([w] { w->Close(); });

	auto base = t->TempDir();
	auto baseNorm = tspath::normalizeSlashes(base);
	auto target =
	    tspath::normalizeSlashes(tspath::combinePaths(base, {"a", "b", "c"}));
	auto pattern = target + "/*";

	if (auto err = w->WatchFiles("id", {watcher(pattern)}); err != nullptr) {
		t->Fatal({err});
	}
	if (!backend->isWatching(baseNorm)) {
		t->Fatalf("expected initial ancestor watch on %q, got %v",
		          {baseNorm, joinDirs(backend->watchedDirs())});
	}

	// Reveal one path component at a time; the ancestor watch should descend.
	auto mkdirAndPath = [&](const std::string& rel) {
		auto p = tspath::combinePaths(base, {rel});
		if (auto err = osMkdirAll(p); err != nullptr) {
			t->Fatal({err});
		}
		return tspath::normalizeSlashes(p);
	};

	auto aDir = mkdirAndPath("a");
	backend->emit(baseNorm,
	              {fswatch::Event{fswatch::EventKind::EventUpdate,
	                              tspath::combinePaths(base, {"a"})}},
	              nullptr);
	waitFor(t, [&] { return backend->isWatching(aDir); }, "descend to a");

	auto abDir = mkdirAndPath("a/b");
	backend->emit(aDir,
	              {fswatch::Event{fswatch::EventKind::EventUpdate,
	                              tspath::combinePaths(base, {"a", "b"})}},
	              nullptr);
	waitFor(t, [&] { return backend->isWatching(abDir); }, "descend to a/b");

	auto abcDir = mkdirAndPath("a/b/c");
	backend->emit(abDir,
	              {fswatch::Event{fswatch::EventKind::EventUpdate,
	                              tspath::combinePaths(base, {"a", "b", "c"})}},
	              nullptr);
	waitFor(t, [&] { return backend->isWatching(abcDir); },
	        "promote to target a/b/c");
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_MultiLevelDescend",
                   TestWatcher_MultiLevelDescend);

// TestWatcher_AtomicTreeCreateRace — lspwatcher_test.go:537.
void TestWatcher_AtomicTreeCreateRace(T* t) {
	t->Parallel();

	auto fs = bundled::WrapFS(osFS());
	auto backend = newFakeBackend();
	auto w = newWithBackend(fs, backend,
	                        [](std::vector<std::shared_ptr<lsproto::FileEvent>>) {},
	                        testLogger());
	t->Cleanup([w] { w->Close(); });

	auto base = t->TempDir();
	auto baseNorm = tspath::normalizeSlashes(base);
	auto target =
	    tspath::normalizeSlashes(tspath::combinePaths(base, {"a", "b", "c"}));
	auto pattern = target + "/*";

	if (auto err = w->WatchFiles("id", {watcher(pattern)}); err != nullptr) {
		t->Fatal({err});
	}

	// The whole tree appears at once (e.g. an extraction/symlink). A single
	// notification on the base ancestor watch must descend all the way and
	// promote to the target in one reconcile pass.
	if (auto err = osMkdirAll(tspath::combinePaths(base, {"a", "b", "c"}));
	    err != nullptr) {
		t->Fatal({err});
	}
	backend->emit(baseNorm,
	              {fswatch::Event{fswatch::EventKind::EventUpdate,
	                              tspath::combinePaths(base, {"a"})}},
	              nullptr);

	waitFor(t, [&] { return backend->isWatching(target); },
	        "promote to target in one pass");
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_AtomicTreeCreateRace",
                   TestWatcher_AtomicTreeCreateRace);

// TestWatcher_SyntheticCreateDepth — lspwatcher_test.go:567.
void TestWatcher_SyntheticCreateDepth(T* t) {
	t->Parallel();

	for (auto recursive : {false, true}) {
		auto subName = recursive ? "recursive" : "non-recursive";
		t->Run(subName, [recursive](T* t) {
			t->Parallel();
			auto fs = bundled::WrapFS(osFS());
			auto backend = newFakeBackend();
			std::mutex mu;
			std::vector<std::shared_ptr<lsproto::FileEvent>> got;
			auto w = newWithBackend(
			    fs, backend,
			    [&](std::vector<std::shared_ptr<lsproto::FileEvent>> changes) {
				    std::lock_guard<std::mutex> lk(mu);
				    got.insert(got.end(), changes.begin(), changes.end());
			    },
			    testLogger());
			t->Cleanup([w] { w->Close(); });

			auto base = t->TempDir();
			auto baseNorm = tspath::normalizeSlashes(base);
			auto target =
			    tspath::normalizeSlashes(tspath::combinePaths(base, {"pkg"}));
			auto kind = allWatchKinds();
			std::string pattern;
			if (recursive) {
				pattern = target + "/**/*";
			} else {
				pattern = target + "/*";
			}

			if (auto err = w->WatchFiles("id", {watcher(pattern, kind)});
			    err != nullptr) {
				t->Fatal({err});
			}

			// Materialize the target with a nested file under a subdirectory.
			if (auto err =
			        osMkdirAll(tspath::combinePaths(base, {"pkg", "sub"}));
			    err != nullptr) {
				t->Fatal({err});
			}
			if (auto err = osWriteFile(
			        tspath::combinePaths(base, {"pkg", "top.ts"}), "export {}",
			        std::filesystem::perms::owner_all |
			            std::filesystem::perms::group_read |
			            std::filesystem::perms::others_read);
			    err != nullptr) {
				t->Fatal({err});
			}
			if (auto err = osWriteFile(
			        tspath::combinePaths(base, {"pkg", "sub", "deep.ts"}),
			        "export {}",
			        std::filesystem::perms::owner_all |
			            std::filesystem::perms::group_read |
			            std::filesystem::perms::others_read);
			    err != nullptr) {
				t->Fatal({err});
			}
			backend->emit(baseNorm,
			              {fswatch::Event{fswatch::EventKind::EventUpdate,
			                              tspath::combinePaths(base, {"pkg"})}},
			              nullptr);

			waitFor(t, [&] { return backend->isWatching(target); },
			        "promotion");

			auto created = [&]() -> std::unordered_map<std::string, bool> {
				std::lock_guard<std::mutex> lk(mu);
				std::unordered_map<std::string, bool> m;
				for (auto& e : got) {
					if (e->Type == lsproto::FileChangeTypeCreated) {
						m[e->Uri] = true;
					}
				}
				return m;
			};

			// Both modes must synthesize the immediate child.
			waitFor(
			    t,
			    [&] {
				    for (auto& [s, _] : created()) {
					    if (s.ends_with("/pkg/top.ts")) {
						    return true;
					    }
				    }
				    return false;
			    },
			    "synthetic create for immediate child");

			// Only the recursive watch should synthesize the deep descendant.
			auto deadline = std::chrono::steady_clock::now() + 1s;
			auto sawDeep = [&] {
				for (auto& [s, _] : created()) {
					if (s.ends_with("/pkg/sub/deep.ts")) {
						return true;
					}
				}
				return false;
			};
			auto createdKeys = [&]() -> std::string {
				std::string out;
				for (auto& [s, _] : created()) {
					out += s + " ";
				}
				return out;
			};
			if (recursive) {
				while (std::chrono::steady_clock::now() < deadline &&
				       !sawDeep()) {
					std::this_thread::sleep_for(20ms);
				}
				if (!sawDeep()) {
					t->Errorf(
					    "recursive watch should synthesize deep descendant; got %v",
					    {createdKeys()});
				}
			} else {
				// allow any erroneous deep event to arrive
				std::this_thread::sleep_for(300ms);
				if (sawDeep()) {
					t->Errorf(
					    "non-recursive watch must not synthesize deep descendant; got %v",
					    {createdKeys()});
				}
			}
		});
	}
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_SyntheticCreateDepth",
                   TestWatcher_SyntheticCreateDepth);

// TestWatcher_TerminatedFallsBackAndRecovers — lspwatcher_test.go:667.
void TestWatcher_TerminatedFallsBackAndRecovers(T* t) {
	t->Parallel();

	auto fs = bundled::WrapFS(osFS());
	auto backend = newFakeBackend();
	std::mutex mu;
	std::vector<std::shared_ptr<lsproto::FileEvent>> got;
	auto w = newWithBackend(fs, backend,
	                        [&](std::vector<std::shared_ptr<lsproto::FileEvent>> changes) {
		                        std::lock_guard<std::mutex> lk(mu);
		                        got.insert(got.end(), changes.begin(),
		                                   changes.end());
	                        },
	                        testLogger());
	t->Cleanup([w] { w->Close(); });

	auto base = t->TempDir();
	auto baseNorm = tspath::normalizeSlashes(base);
	auto target = tspath::normalizeSlashes(tspath::combinePaths(base, {"pkg"}));
	if (auto err = osMkdirAll(tspath::combinePaths(base, {"pkg"}));
	    err != nullptr) {
		t->Fatal({err});
	}
	auto pattern = target + "/*";
	auto kind = allWatchKinds();

	if (auto err = w->WatchFiles("id", {watcher(pattern, kind)});
	    err != nullptr) {
		t->Fatal({err});
	}
	if (!backend->isWatching(target)) {
		t->Fatalf("expected target watch on %q, got %v",
		          {target, joinDirs(backend->watchedDirs())});
	}

	// Delete the directory and deliver ErrWatchTerminated together with the
	// directory's own delete event (as the real backends do).
	if (auto err = osRemoveAll(tspath::combinePaths(base, {"pkg"}));
	    err != nullptr) {
		t->Fatal({err});
	}
	backend->emit(target,
	              {fswatch::Event{fswatch::EventKind::EventDelete,
	                              tspath::combinePaths(base, {"pkg"})}},
	              gostd::joinError({fswatch::ErrWatchTerminated,
	                                gostd::newError("removed")}));

	// The delete must be forwarded, and the watch must fall back to watching
	// the ancestor.
	waitFor(
	    t,
	    [&] {
		    std::lock_guard<std::mutex> lk(mu);
		    for (auto& e : got) {
			    if (e->Type == lsproto::FileChangeTypeDeleted &&
			        e->Uri.ends_with("/pkg")) {
				    return true;
			    }
		    }
		    return false;
	    },
	    "forwarded delete of terminated dir");
	waitFor(
	    t,
	    [&] {
		    return backend->isWatching(baseNorm) && !backend->isWatching(target);
	    },
	    "fallback to ancestor watch");

	// Recreate the directory; the ancestor watch must promote back to target.
	if (auto err = osMkdirAll(tspath::combinePaths(base, {"pkg"}));
	    err != nullptr) {
		t->Fatal({err});
	}
	backend->emit(baseNorm,
	              {fswatch::Event{fswatch::EventKind::EventUpdate,
	                              tspath::combinePaths(base, {"pkg"})}},
	              nullptr);
	waitFor(t, [&] { return backend->isWatching(target); },
	        "recovery to target watch after recreation");
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_TerminatedFallsBackAndRecovers",
                   TestWatcher_TerminatedFallsBackAndRecovers);

// TestWatcher_GenuineFailureRollsBackForRetry — lspwatcher_test.go:733.
void TestWatcher_GenuineFailureRollsBackForRetry(T* t) {
	t->Parallel();

	auto fs = bundled::WrapFS(osFS());
	auto backend = newFakeBackend();
	auto w = newWithBackend(fs, backend,
	                        [](std::vector<std::shared_ptr<lsproto::FileEvent>>) {},
	                        testLogger());
	t->Cleanup([w] { w->Close(); });

	auto dir = t->TempDir();
	auto dirNorm = tspath::normalizeSlashes(dir);
	auto pattern = dirNorm + "/*";

	// Inject a genuine backend failure for the existing directory.
	{
		std::lock_guard<std::mutex> lk(backend->mu);
		backend->failDirs[dirNorm] = gostd::newError("too many open files");
	}

	auto err = w->WatchFiles("id", {watcher(pattern)});
	if (err == nullptr) {
		t->Fatal({"expected error from genuine backend failure"});
	}

	// The id must have been rolled back so a retry can re-register cleanly
	// (rather than hitting the duplicate-id error). Clear the injected failure
	// to simulate the resource pressure easing on retry.
	{
		std::lock_guard<std::mutex> lk(backend->mu);
		backend->failDirs.erase(dirNorm);
	}

	if (auto err = w->WatchFiles("id", {watcher(pattern)}); err != nullptr) {
		t->Fatalf("retry after rollback should succeed, got %v", {err});
	}
	if (!backend->isWatching(dirNorm)) {
		t->Fatalf("expected watch on %q after successful retry, got %v",
		          {dirNorm, joinDirs(backend->watchedDirs())});
	}
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_GenuineFailureRollsBackForRetry",
                   TestWatcher_GenuineFailureRollsBackForRetry);

// TestWatcher_WatchTerminatedDoesNotDropEvents — lspwatcher_test.go:774.
void TestWatcher_WatchTerminatedDoesNotDropEvents(T* t) {
	t->Parallel();

	auto dir = t->TempDir();
	auto dirNorm = tspath::normalizeSlashes(dir);
	auto fs = bundled::WrapFS(osFS());
	auto backend = newFakeBackend();
	std::vector<std::shared_ptr<lsproto::FileEvent>> got;
	std::mutex mu;
	auto w = newWithBackend(fs, backend,
	                        [&](std::vector<std::shared_ptr<lsproto::FileEvent>> changes) {
		                        std::lock_guard<std::mutex> lk(mu);
		                        got.insert(got.end(), changes.begin(),
		                                   changes.end());
	                        },
	                        testLogger());

	auto pattern = dirNorm + "/**/*";
	if (auto err = w->WatchFiles("id", {watcher(pattern)}); err != nullptr) {
		t->Fatal({err});
	}

	backend->emitAll({fswatch::Event{fswatch::EventKind::EventUpdate,
	                               tspath::combinePaths(dirNorm, {"b.ts"})}},
	                 gostd::joinError({fswatch::ErrWatchTerminated,
	                                   gostd::newError("simulated")}));

	waitFor(t,
	        [&] {
		        std::lock_guard<std::mutex> lk(mu);
		        return !got.empty();
	        },
	        "events with watch-terminated error");
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_WatchTerminatedDoesNotDropEvents",
                   TestWatcher_WatchTerminatedDoesNotDropEvents);

// TestWatcher_CloseWhileWatchFilesReconciles — lspwatcher_test.go:822.
void TestWatcher_CloseWhileWatchFilesReconciles(T* t) {
	t->Parallel();

	auto dir = t->TempDir();
	auto pattern = tspath::normalizeSlashes(dir) + "/**/*";
	auto backend = std::make_shared<blockingBackend>();
	auto w = newWithBackend(bundled::WrapFS(osFS()), backend,
	                        [](std::vector<std::shared_ptr<lsproto::FileEvent>>) {},
	                        testLogger());

	// done := make(chan error, 1)
	struct chanErr {
		std::mutex m;
		std::condition_variable cv;
		bool set = false;
		gostd::Error err;
	};
	auto done = std::make_shared<chanErr>();
	auto watchThread = std::thread([w, &pattern, done] {
		auto err = w->WatchFiles("id", {watcher(pattern)});
		std::lock_guard<std::mutex> lk(done->m);
		done->err = err;
		done->set = true;
		done->cv.notify_all();
	});

	{
		std::unique_lock<std::mutex> lk(backend->mu);
		backend->cv.wait(lk, [&] { return backend->entered; });
	}
	w->Close();
	{
		std::lock_guard<std::mutex> lk(backend->mu);
		backend->released = true;
	}
	backend->cv.notify_all();

	gostd::Error doneErr;
	{
		std::unique_lock<std::mutex> lk(done->m);
		done->cv.wait(lk, [&] { return done->set; });
		doneErr = done->err;
	}
	watchThread.join();
	if (doneErr == nullptr) {
		t->Fatal(
		    {"expected WatchFiles to report that the watcher was closed"});
	}
}
REGISTER_UNIT_TEST("lspwatcher.TestWatcher_CloseWhileWatchFilesReconciles",
                   TestWatcher_CloseWhileWatchFilesReconciles);

} // namespace tsc::lsp::lspwatcher
