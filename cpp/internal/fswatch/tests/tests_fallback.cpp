// Port of fallback_test.go — fallbackWatcher routing.

#include <algorithm>
#include <mutex>

#include "internal/fswatch/tests/util.h"

namespace tsc::fswatch {

namespace {

struct fakeFallbackWatcher;
struct fakeFallbackWatch : Watch {
	fakeFallbackWatcher* watcher;
	std::string dir;

	gostd::Error close() override;
	void unexported() override {}
};

struct fakeFallbackWatcher : Watcher {
	std::mutex mu;
	std::string name_;
	gostd::Error failWith;
	std::function<bool(const std::string&)> failDir;
	std::vector<std::string> watched;
	std::vector<std::string> closed;

	std::string name() override { return name_; }
	bool available() override { return true; }
	bool hasFastRecursiveBackend() override { return false; }
	void unexported() override {}

	bool shouldFail(const std::string& dir) {
		return failWith != nullptr && (!failDir || failDir(dir));
	}

	std::pair<std::shared_ptr<Watch>, gostd::Error>
	watchDirectory(const std::string& dir, const WatchCallback&,
	               std::vector<std::shared_ptr<WatchOption>>) override {
		std::lock_guard<std::mutex> lk(mu);
		if (shouldFail(dir)) {
			return {nullptr, gostd::errorf("%s: %w", {name_, failWith})};
		}
		watched.push_back(dir);
		auto w = std::make_shared<fakeFallbackWatch>();
		w->watcher = this;
		w->dir = dir;
		return {w, nullptr};
	}

	std::pair<std::vector<std::shared_ptr<Watch>>, gostd::Error>
	watchDirectories(std::vector<WatchDirectoryRequest> requests) override {
		std::lock_guard<std::mutex> lk(mu);
		for (auto& request : requests) {
			if (shouldFail(request.dir)) {
				return {{},
				        gostd::errorf("%s: %w", {name_, failWith})};
			}
		}
		std::vector<std::shared_ptr<Watch>> watches;
		watches.reserve(requests.size());
		for (auto& request : requests) {
			watched.push_back(request.dir);
			auto w = std::make_shared<fakeFallbackWatch>();
			w->watcher = this;
			w->dir = request.dir;
			watches.push_back(std::move(w));
		}
		return {watches, nullptr};
	}

	std::pair<std::shared_ptr<Watch>, gostd::Error>
	watchFile(const std::string& path, const WatchCallback& fn) override {
		return watchDirectory(path, fn, {});
	}

	std::vector<std::string> watchedDirs() {
		std::lock_guard<std::mutex> lk(mu);
		auto dirs = watched;
		std::sort(dirs.begin(), dirs.end());
		return dirs;
	}

	std::vector<std::string> closedDirs() {
		std::lock_guard<std::mutex> lk(mu);
		auto dirs = closed;
		std::sort(dirs.begin(), dirs.end());
		return dirs;
	}
};

gostd::Error fakeFallbackWatch::close() {
	std::lock_guard<std::mutex> lk(watcher->mu);
	watcher->closed.push_back(dir);
	return nullptr;
}

WatchCallback noopCallback() {
	return [](std::vector<Event>, gostd::Error) {};
}

void TestFallbackWatcherRoutesUnsupportedDirectories(T* t) {
	t->Parallel();

	auto* primary = new fakeFallbackWatcher;
	primary->name_ = "fanotify";
	primary->failWith = ErrFilesystemUnsupported;
	primary->failDir = [](const std::string& dir) {
		return dir.rfind("/mnt/fuse", 0) == 0;
	};
	auto* secondary = new fakeFallbackWatcher;
	secondary->name_ = "inotify";
	fallbackWatcher watcher{primary, secondary};

	std::vector<WatchDirectoryRequest> requests{
	    {"/project", noopCallback(), {}},
	    {"/project/src", noopCallback(), {}},
	    {"/mnt/fuse/deps", noopCallback(), {}},
	    {"/mnt/fuse/deps/a", noopCallback(), {}},
	};
	auto [watches0, err] = watcher.watchDirectories(std::move(requests));
	if (err != nullptr) {
		t->Fatalf("WatchDirectories: %v", {err});
	}
	auto watches = watches0;
	t->Cleanup([watches] {
		for (auto& w : watches)
			(void)w->close();
	});

	if (auto got = primary->watchedDirs();
	    !equalStringSlices(got, {"/project", "/project/src"})) {
		t->Errorf("primary watched %d dirs, want /project /project/src",
		          {(int)got.size()});
	}
	if (auto got = secondary->watchedDirs(); !equalStringSlices(
	        got, {"/mnt/fuse/deps", "/mnt/fuse/deps/a"})) {
		t->Errorf("secondary watched %d dirs, want /mnt/fuse/deps "
		          "/mnt/fuse/deps/a",
		          {(int)got.size()});
	}
}
REGISTER_UNIT_TEST("fswatch.TestFallbackWatcherRoutesUnsupportedDirectories",
                   TestFallbackWatcherRoutesUnsupportedDirectories);

void TestFallbackWatcherDoesNotFallbackForUnrelatedError(T* t) {
	t->Parallel();

	auto* primary = new fakeFallbackWatcher;
	primary->name_ = "fanotify";
	primary->failWith = ErrUnavailable;
	auto* secondary = new fakeFallbackWatcher;
	secondary->name_ = "inotify";
	fallbackWatcher watcher{primary, secondary};

	auto [_, err] = watcher.watchDirectory("/project", noopCallback(), {});
	if (!gostd::errorIs(err, ErrUnavailable)) {
		t->Fatalf("WatchDirectory error = %v, want ErrUnavailable", {err});
	}
	if (auto got = secondary->watchedDirs(); !got.empty()) {
		t->Errorf("secondary watched %d dirs, want no watches",
		          {(int)got.size()});
	}
}
REGISTER_UNIT_TEST(
    "fswatch.TestFallbackWatcherDoesNotFallbackForUnrelatedError",
    TestFallbackWatcherDoesNotFallbackForUnrelatedError);

void TestFallbackWatcherDoesNotUseSecondaryOnHappyPath(T* t) {
	t->Parallel();

	auto* primary = new fakeFallbackWatcher;
	primary->name_ = "fanotify";
	auto* secondary = new fakeFallbackWatcher;
	secondary->name_ = "inotify";
	secondary->failWith = gostd::newError("secondary should not be used");
	fallbackWatcher watcher{primary, secondary};

	auto [watches, err] = watcher.watchDirectories({
	    {"/project", noopCallback(), {}},
	    {"/project/src", noopCallback(), {}},
	});
	if (err != nullptr) {
		t->Fatalf("WatchDirectories: %v", {err});
	}
	auto watchesKeep = watches;
	t->Cleanup([watchesKeep] {
		for (auto& w : watchesKeep)
			(void)w->close();
	});
	if (auto got = secondary->watchedDirs(); !got.empty()) {
		t->Errorf("secondary watched %d dirs, want no watches",
		          {(int)got.size()});
	}
}
REGISTER_UNIT_TEST("fswatch.TestFallbackWatcherDoesNotUseSecondaryOnHappyPath",
                   TestFallbackWatcherDoesNotUseSecondaryOnHappyPath);

void TestFallbackWatcherRollsBackRoutedWatchesOnFailure(T* t) {
	t->Parallel();

	auto* primary = new fakeFallbackWatcher;
	primary->name_ = "fanotify";
	primary->failWith = ErrFilesystemUnsupported;
	primary->failDir = [](const std::string& dir) { return dir != "/project"; };
	auto* secondary = new fakeFallbackWatcher;
	secondary->name_ = "inotify";
	secondary->failWith = ErrUnavailable;
	secondary->failDir = [](const std::string& dir) { return dir == "/broken"; };
	fallbackWatcher watcher{primary, secondary};

	auto [watches, err] = watcher.watchDirectories({
	    {"/project", noopCallback(), {}},
	    {"/mnt/fuse", noopCallback(), {}},
	    {"/broken", noopCallback(), {}},
	});
	if (!gostd::errorIs(err, ErrUnavailable)) {
		t->Fatalf("WatchDirectories error = %v, want ErrUnavailable", {err});
	}
	if (auto got = primary->closedDirs(); !equalStringSlices(got, {"/project"})) {
		t->Errorf("primary closed %d dirs, want /project", {(int)got.size()});
	}
	if (auto got = secondary->closedDirs();
	    !equalStringSlices(got, {"/mnt/fuse"})) {
		t->Errorf("secondary closed %d dirs, want /mnt/fuse",
		          {(int)got.size()});
	}
}
REGISTER_UNIT_TEST("fswatch.TestFallbackWatcherRollsBackRoutedWatchesOnFailure",
                   TestFallbackWatcherRollsBackRoutedWatchesOnFailure);

void TestFanotifyUsesInternalInotifyFallback(T* t) {
	t->Parallel();

	auto* fallback = dynamic_cast<fallbackWatcher*>(Fanotify());
	if (fallback == nullptr) {
		t->Fatalf("Fanotify() want *fallbackWatcher", {});
	}
	if (fallback->primary != &fanotifyWatcher()) {
		t->Error({"Fanotify primary want package fanotify watcher"});
	}
	if (fallback->secondary != &inotifyWatcher()) {
		t->Error({"Fanotify secondary want package inotify watcher"});
	}
}
REGISTER_UNIT_TEST("fswatch.TestFanotifyUsesInternalInotifyFallback",
                   TestFanotifyUsesInternalInotifyFallback);

} // namespace
} // namespace tsc::fswatch
