// mock_watch_backend.cpp — port of tsctests/mock_watch_backend.go.
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/execute/tsctests/tsctests.h"

namespace tsc::execute::tsctests {

namespace {
// io.Closer returned to callers — borrows the map-owned MockWatch so Close
// flips Closed on the registered watch (Go returns the watch itself).
struct mockWatchCloser final : gostd::io::Closer {
	std::shared_ptr<MockWatch> w;
	explicit mockWatchCloser(std::shared_ptr<MockWatch> watch)
	    : w(std::move(watch)) {}
	gostd::Error close() override { return w->close(); }
};
}  // namespace

// Close — mock_watch_backend.go:54.
gostd::Error MockWatch::close() {
	Closed = true;
	return nullptr;
}

// NewMockWatchBackend — mock_watch_backend.go:32.
MockWatchBackend* NewMockWatchBackend() {
	return new MockWatchBackend();
}

// HasWatches — mock_watch_backend.go:39.
bool MockWatchBackend::HasWatches() {
	std::lock_guard<std::mutex> lock(mu);
	return !Dirs.empty();
}

// WatchDirectory — mock_watch_backend.go:59.
std::pair<std::unique_ptr<gostd::io::Closer>, gostd::Error>
MockWatchBackend::WatchDirectory(const std::string& dir,
                                 const fswatch::WatchCallback& fn,
                                 bool recursive,
                                 std::function<bool(const std::string&)>
                                     ignore) {
	auto [closers, err] =
	    WatchDirectories({watchmanager::WatchDirectoryRequest{
		    dir, fn, recursive, std::move(ignore)}});
	if (err) {
		return {nullptr, err};
	}
	return {std::move(closers[0]), nullptr};
}

// WatchDirectories — mock_watch_backend.go:72.
std::pair<std::vector<std::unique_ptr<gostd::io::Closer>>, gostd::Error>
MockWatchBackend::WatchDirectories(
    const std::vector<watchmanager::WatchDirectoryRequest>& requests) {
	std::lock_guard<std::mutex> lock(mu);
	for (const auto& request : requests) {
		if (DirectoryExists && !DirectoryExists(request.Dir)) {
			return {
			    std::vector<std::unique_ptr<gostd::io::Closer>>(),
			    gostd::errorf("directory does not exist: %s",
			                  {request.Dir})};
		}
	}
	std::vector<std::unique_ptr<gostd::io::Closer>> closers(requests.size());
	for (size_t i = 0; i < requests.size(); i++) {
		const auto& request = requests[i];
		auto w = std::make_shared<MockWatch>();
		w->Path = request.Dir;
		w->Callback = request.Callback;
		w->Recursive = request.Recursive;
		w->Ignore = request.Ignore;
		Dirs[request.Dir] = w;
		closers[i] = std::make_unique<mockWatchCloser>(std::move(w));
	}
	return {std::move(closers), nullptr};
}

// SendEvents — mock_watch_backend.go:95. Snapshot callbacks under the lock,
// then invoke outside the lock to avoid deadlock if the callback re-enters
// the mock.
void MockWatchBackend::SendEvents(
    const std::vector<fswatch::Event>& events) {
	mu.lock();
	struct target {
		fswatch::WatchCallback cb;
		std::vector<fswatch::Event> events;
	};
	std::unordered_map<MockWatch*, target> targets;

	for (const auto& e : events) {
		// Check directory watches.
		for (auto& [dir, w] : Dirs) {
			if (w->Closed) {
				continue;
			}
			if (w->Ignore && w->Ignore(e.path)) {
				continue;
			}
			if (!pathIsUnder(e.path, w->Path, w->Recursive,
			                 UseCaseSensitiveFileNames)) {
				continue;
			}
			if (auto it = targets.find(w.get()); it != targets.end()) {
				it->second.events.push_back(e);
			} else {
				targets.emplace(w.get(),
				                target{w->Callback, {e}});
			}
		}
	}
	mu.unlock();

	for (auto& [ptr, t] : targets) {
		t.cb(t.events, nullptr);
	}
}

// SendOverflow — mock_watch_backend.go:134. Simulates a kernel event-queue
// overflow by invoking every active watch callback with fswatch.ErrOverflow.
void MockWatchBackend::SendOverflow() {
	mu.lock();
	std::vector<fswatch::WatchCallback> cbs;
	for (auto& [dir, w] : Dirs) {
		if (!w->Closed) {
			cbs.push_back(w->Callback);
		}
	}
	mu.unlock();
	for (auto& cb : cbs) {
		cb({}, fswatch::ErrOverflow);
	}
}

// SendChangedPaths — mock_watch_backend.go:153. Converts file changes into
// fswatch events; also emits update events for parent directories,
// simulating how real filesystem watchers report directory events.
void MockWatchBackend::SendChangedPaths(
    const std::vector<testutil::fsbaselineutil::FileChange>& changes) {
	std::vector<fswatch::Event> events;
	events.reserve(changes.size() * 2);
	std::unordered_set<std::string> seenDirs;
	for (const auto& c : changes) {
		fswatch::EventKind kind = fswatch::EventKind::EventUpdate;
		if (c.Deleted) {
			kind = fswatch::EventKind::EventDelete;
		}
		events.push_back(fswatch::Event{kind, c.Path, false});
		// Emit update events for parent directories of changed files.
		std::string dir = vfs::vfstest::dirName(c.Path);
		while (dir != "" && dir != "/" && dir != ".") {
			if (seenDirs.count(dir)) {
				break;
			}
			seenDirs.insert(dir);
			events.push_back(
			    fswatch::Event{fswatch::EventKind::EventUpdate, dir, false});
			std::string parent = vfs::vfstest::dirName(dir);
			if (parent == dir) {
				break;
			}
			dir = std::move(parent);
		}
	}
	SendEvents(events);
}

// pathIsUnder — mock_watch_backend.go:184. Reports whether eventPath is
// inside dir. If recursive is false, only direct children match.
bool pathIsUnder(const std::string& eventPath_, const std::string& dir_,
                 bool recursive, bool useCaseSensitiveFileNames) {
	std::string eventPath = eventPath_;
	std::string dir = dir_;
	if (!useCaseSensitiveFileNames) {
		eventPath = std::string(
		    tspath::getCanonicalFileName(eventPath, false));
		dir = std::string(tspath::getCanonicalFileName(dir, false));
	}
	if (!eventPath.starts_with(dir)) {
		return false;
	}
	std::string_view rest = std::string_view(eventPath).substr(dir.size());
	if (rest.empty()) {
		return false; // exact match = the dir itself, not a child
	}
	if (rest[0] != '/') {
		return false; // e.g. dir="/foo", path="/foobar"
	}
	if (!recursive) {
		// Direct child only: no further '/' after the separator.
		return rest.substr(1).find('/') == std::string_view::npos;
	}
	return true;
}

// WatchState — mock_watch_backend.go:209. Deterministic, human-readable
// summary of all active watches for test baselines.
std::string MockWatchBackend::WatchState() {
	std::lock_guard<std::mutex> lock(mu);

	std::string b;
	b += "Watch Registrations::\n";

	// Directory watches, sorted by path.
	std::vector<std::string> dirs;
	for (auto& [dir, w] : Dirs) {
		if (!w->Closed) {
			dirs.push_back(dir);
		}
	}
	std::sort(dirs.begin(), dirs.end());

	b += "Directory watches::\n";
	if (dirs.empty()) {
		b += "  (none)\n";
	}
	for (const auto& d : dirs) {
		auto& w = Dirs[d];
		if (w->Recursive) {
			b += gostd::sprintf("  %s (recursive)\n", {d});
		} else {
			b += gostd::sprintf("  %s\n", {d});
		}
	}

	return b;
}

}  // namespace tsc::execute::tsctests
