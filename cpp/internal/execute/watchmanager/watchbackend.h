#pragma once

// watchbackend.go — port of tsc/internal/execute/watchmanager/watchbackend.go:
// the fswatch.Watcher abstraction used by the CLI watcher and build mode.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "internal/fswatch/fswatch.h"
#include "internal/gostd/gostd.h"

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
