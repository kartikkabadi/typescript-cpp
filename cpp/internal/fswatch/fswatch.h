#pragma once

// === dep stubs — removed when owner slice lands ===
// fswatch.h — dep-stub decls for tsc/internal/fswatch, owned by the fswatch
// slice. Only what execute/build references today; real implementations land
// with the fswatch slice.

#include <functional>
#include <string>
#include <vector>

namespace tsc::fswatch {

// event.go — EventKind classifies a filesystem change.
using EventKind = int;
inline constexpr EventKind EventUpdate = 1;
inline constexpr EventKind EventDelete = 2;

// event.go — Event is a single filesystem change notification.
struct Event {
	std::string Path;
	EventKind Kind = 0;
};

// watcher.go — WatchCallback receives batched filesystem events.
using WatchCallback = std::function<void(const std::vector<Event>&)>;

} // namespace tsc::fswatch
