#pragma once

// watchbackend.go — port of tsc/internal/execute/watchmanager/watchbackend.go:
// the fswatch.Watcher abstraction used by the CLI watcher and build mode.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "internal/gostd/gostd.h"

// ---------------------------------------------------------------------------
// dep-stub: fswatch — owned by fswatch
// Declarations for the parts of tsc/internal/fswatch the watchmanager reaches.
// fswatch::Default (platform backend construction) is stubbed with
// TSC_UNREACHABLE; the option factories and the Event/Watcher/Watches types
// below carry their real signatures so call sites are faithful.
// ---------------------------------------------------------------------------
namespace tsc::fswatch {

// EventKind — fswatch/event.go:6.
enum class EventKind : int {
	Update = 1,
	Delete = 2,
};
inline constexpr EventKind EventUpdate = EventKind::Update;
inline constexpr EventKind EventDelete = EventKind::Delete;

// EventKind.String — fswatch/event.go:13.
inline std::string_view eventKindString(EventKind k) {
	switch (k) {
	case EventKind::Update:
		return "update";
	case EventKind::Delete:
		return "delete";
	}
	return "unknown";
}

// Event — fswatch/event.go:25.
struct Event {
	EventKind Kind = EventKind::Update;
	std::string Path;
};

// WatchCallback — fswatch/watcher.go:185.
using WatchCallback =
    std::function<void(const std::vector<Event>& events, gostd::Error err)>;

// watchOptions — fswatch/watcher.go:110 (package-internal options accumulator).
struct watchOptions {
	std::function<bool(const std::string&)> ignore;
	bool recursive = false;
	std::string file;
};

// WatchOption — fswatch/watcher.go:106.
using WatchOption = std::function<void(watchOptions*)>;

// WithIgnore — fswatch/watcher.go:136.
inline WatchOption WithIgnore(std::function<bool(const std::string&)> fn) {
	return [fn = std::move(fn)](watchOptions* opts) {
		opts->ignore = fn;
	};
}

// WithRecursive — fswatch/watcher.go:166.
inline WatchOption WithRecursive() {
	return [](watchOptions* opts) { opts->recursive = true; };
}

// WatchDirectoryRequest — fswatch/watcher.go:115.
struct WatchDirectoryRequest {
	std::string Dir;
	WatchCallback Callback;
	std::vector<WatchOption> Options;
};

// Watch — fswatch/watcher.go:172.
struct Watch {
	virtual ~Watch() = default;
	virtual gostd::Error Close() = 0;
};

// Watcher — fswatch/watcher.go:58.
struct Watcher {
	virtual ~Watcher() = default;
	virtual std::string Name() = 0;
	virtual bool Available() = 0;
	virtual bool HasFastRecursiveBackend() = 0;
	virtual std::pair<std::unique_ptr<Watch>, gostd::Error>
	WatchDirectory(const std::string& dir, const WatchCallback& fn,
	               const std::vector<WatchOption>& opts) = 0;
	virtual std::pair<std::vector<std::unique_ptr<Watch>>, gostd::Error>
	WatchDirectories(const std::vector<WatchDirectoryRequest>& requests) = 0;
	virtual std::pair<std::unique_ptr<Watch>, gostd::Error>
	WatchFile(const std::string& path, const WatchCallback& fn) = 0;
};

// Sentinel errors — fswatch/watcher.go:31-46.
extern const gostd::Error ErrOverflow;
extern const gostd::Error ErrWatchTerminated;
extern const gostd::Error ErrUnavailable;
extern const gostd::Error ErrFilesystemUnsupported;

// Default — fswatch/watcher.go:230.
// dep-stub: fswatch — owned by fswatch.
Watcher* Default();

}  // namespace tsc::fswatch

namespace tsc::execute::watchmanager {

// WatchDirectoryRequest — watchbackend.go:16.
struct WatchDirectoryRequest {
	std::string Dir;
	fswatch::WatchCallback Callback;
	bool Recursive = false;
	std::function<bool(const std::string&)> Ignore;
};

// WatchBackend — watchbackend.go:11. Abstracts fswatch.Watcher for testing.
struct WatchBackend {
	virtual ~WatchBackend() = default;
	virtual std::pair<std::unique_ptr<gostd::io::Closer>, gostd::Error>
	WatchDirectory(const std::string& dir, const fswatch::WatchCallback& fn,
	               bool recursive,
	               std::function<bool(const std::string&)> ignore) = 0;
	virtual std::pair<std::vector<std::unique_ptr<gostd::io::Closer>>,
	                  gostd::Error>
	WatchDirectories(const std::vector<WatchDirectoryRequest>& requests) = 0;
};

// CommandLineTestingWithWatchBackend — watchbackend.go:24. Optional
// extension of CommandLineTesting that supplies a WatchBackend for test mode.
struct CommandLineTestingWithWatchBackend {
	virtual ~CommandLineTestingWithWatchBackend() = default;
	virtual WatchBackend* WatchBackend() = 0;
};

// FSWatchBackend — watchbackend.go:29. `struct FSWatchBackend{Inner fswatch.Watcher}`.
struct FSWatchBackend : WatchBackend {
	fswatch::Watcher* Inner;

	std::pair<std::unique_ptr<gostd::io::Closer>, gostd::Error>
	WatchDirectory(const std::string& dir, const fswatch::WatchCallback& fn,
	               bool recursive,
	               std::function<bool(const std::string&)> ignore) override;
	std::pair<std::vector<std::unique_ptr<gostd::io::Closer>>, gostd::Error>
	WatchDirectories(const std::vector<WatchDirectoryRequest>& requests)
	    override;
};

// ShouldIgnoreWatchPath — watchbackend.go:65.
bool ShouldIgnoreWatchPath(const std::string& path);

// CanWatchDirectory — watchbackend.go:74.
bool CanWatchDirectory(std::string_view dir);

// PerceivedOsRootLengthForWatching — watchbackend.go:85.
int PerceivedOsRootLengthForWatching(
    const std::vector<std::string>& components);

}  // namespace tsc::execute::watchmanager
