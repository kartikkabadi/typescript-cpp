// fsevents_darwin.go + fsevents_darwin_ffi.go — the macOS FSEvents watcher
// backend.
//
// Go drives FSEvents through cgo-free assembly trampolines + a pipe per
// stream because Go cannot call CoreFoundation directly. In C++ the
// FSEventStream callback is a plain C function pointer, so the pipe
// plumbing has no counterpart: the stream's serial dispatch queue invokes
// fsEventsCallback directly, which gives the identical serialization
// (one callback at a time per stream).
//
// Event classification (same as Go):
//   - flagMustScanSubDirs → ErrOverflow with detail (user/kernel/too-many).
//   - removed&&!created → remove + ErrWatchTerminated for the watch root.
//   - renamed||(removed&&created) → lstat probe → update-or-remove.
//   - otherwise → update.
//
// Root deletion: detected in the callback; the logical watch is marked
// terminated and receives ErrWatchTerminated. The shared stream remains
// active for other watches until the owner reconciles the terminated watch.

#ifdef __APPLE__

#include "internal/fswatch/fswatch.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace tsc::fswatch {

namespace {

// ----- FSEvents flag bits (from FSEvents.h) --------------------------------

constexpr uint32_t flagMustScanSubDirs = 0x00000001;
constexpr uint32_t flagUserDropped = 0x00000002;
constexpr uint32_t flagKernelDropped = 0x00000004;
constexpr uint32_t flagHistoryDone = 0x00000010;

constexpr uint32_t flagItemCreated = 0x00000100;
constexpr uint32_t flagItemRemoved = 0x00000200;
constexpr uint32_t flagItemInodeMetaMod = 0x00000400;
constexpr uint32_t flagItemRenamed = 0x00000800;
constexpr uint32_t flagItemModified = 0x00001000;
constexpr uint32_t flagItemFinderInfoMod = 0x00002000;
constexpr uint32_t flagItemChangeOwner = 0x00004000;
constexpr uint32_t flagItemXattrMod = 0x00008000;
constexpr uint32_t flagItemIsFile = 0x00010000;
constexpr uint32_t flagItemIsDir = 0x00020000;
constexpr uint32_t flagItemIsSymlink = 0x00040000;
constexpr uint32_t flagItemIsHardlink = 0x00100000;
constexpr uint32_t flagItemIsLastHardlink = 0x00200000;
constexpr uint32_t flagItemCloned = 0x00400000;

constexpr uint32_t ignoredFlags = flagItemIsHardlink | flagItemIsLastHardlink |
    flagItemIsSymlink | flagItemIsDir | flagItemIsFile | flagItemCloned;

const gostd::Error errCFStringCreateNull =
    gostd::newError("CFStringCreate returned NULL");
const gostd::Error errCFArrayCreateNull =
    gostd::newError("CFArrayCreate returned NULL");
const gostd::Error errStreamCreateNull =
    gostd::newError("FSEventStreamCreate returned NULL");
const gostd::Error errStreamStartFailed =
    gostd::newError("error starting FSEvents stream");

const gostd::Error errFSEventsUserDropped = gostd::errorf(
    "events were dropped by the FSEvents client: %w", {ErrOverflow});
const gostd::Error errFSEventsKernelDropped = gostd::errorf(
    "events were dropped by the kernel: %w", {ErrOverflow});
const gostd::Error errFSEventsTooMany =
    gostd::errorf("too many events: %w", {ErrOverflow});

constexpr size_t fseventsPathsPerStream = 512;

// cfStringToNFC — fsevents_darwin_ffi.go: the FSEvents paths CFString →
// UTF-8, normalized to NFC.
std::string cfStringToNFC(CFStringRef cf) {
	if (cf == nullptr) {
		return "";
	}
	CFStringEncoding enc = kCFStringEncodingUTF8;
	CFIndex len = CFStringGetLength(cf);
	CFIndex used = 0;
	CFStringGetBytes(cf, CFRangeMake(0, len), enc, '?', false, nullptr, 0,
	                 &used);
	std::string out(static_cast<size_t>(used), '\0');
	CFStringGetBytes(cf, CFRangeMake(0, len), enc, '?', false,
	                 reinterpret_cast<UInt8*>(out.data()),
	                 static_cast<CFIndex>(out.size()), &used);
	out.resize(static_cast<size_t>(used));
	return normalizeNFC(out);
}

struct fseventsState {
	std::atomic<bool> terminated{false};
};

struct fseventsWatchSnapshot {
	std::shared_ptr<dirWatch> w;
	std::shared_ptr<fseventsState> state;
};

struct fseventsStream;

// Forward decls — used inside fsEventsCallback.
std::pair<std::string, bool>
fseventsDisplayPathPrepared(dirWatch* w, comparisonPath* rawPath);
bool fseventsOverflowMatchesPrepared(dirWatch* w, comparisonPath* rawPath);

// fseventsStream — fsevents_darwin_ffi.go. One FSEventStream + its serial
// dispatch queue + the watch snapshot it routes events to.
struct fseventsStream {
	FSEventStreamRef stream = nullptr;
	dispatch_queue_t queue = nullptr;
	std::vector<fseventsWatchSnapshot> watches;
};

// fsEventsCallback — fsevents_darwin.go. Called on the stream's serial
// dispatch queue; `stream` holds the watch snapshot captured when the
// stream was created.
void fsEventsCallback(fseventsStream* stream,
                      size_t numEvents,
                      CFArrayRef paths,
                      const FSEventStreamEventFlags* flags,
                      const FSEventStreamEventId* ids) {
	if (paths == nullptr || flags == nullptr || ids == nullptr) {
		return;
	}
	std::unordered_set<dirWatch*> touched;
	auto& watches = stream->watches;

	for (size_t i = 0; i < numEvents; i++) {
		uint32_t flag = flags[i];
		uint64_t eventID = ids[i];
		auto cfPath = static_cast<CFStringRef>(
		    CFArrayGetValueAtIndex(paths, static_cast<CFIndex>(i)));
		std::string path = cfStringToNFC(cfPath);
		if (path.empty()) {
			continue;
		}
		comparisonPath comparison;
		comparison.path = path;

		bool isRemoved = (flag & flagItemRemoved) != 0;
		bool isRenamed = (flag & flagItemRenamed) != 0;
		bool isCreated = (flag & flagItemCreated) != 0;
		bool isDone = (flag & flagHistoryDone) != 0;

		if ((flag & flagMustScanSubDirs) != 0) {
			gostd::Error overflow;
			if ((flag & flagUserDropped) != 0) {
				overflow = errFSEventsUserDropped;
			} else if ((flag & flagKernelDropped) != 0) {
				overflow = errFSEventsKernelDropped;
			} else {
				overflow = errFSEventsTooMany;
			}
			for (auto& watch : watches) {
				if (watch.state->terminated.load()) {
					continue;
				}
				dirWatch* w = watch.w.get();
				if (fseventsOverflowMatchesPrepared(w, &comparison)) {
					w->events.setError(overflow);
					touched.insert(w);
				}
			}
		}

		if (isDone) {
			break;
		}

		if ((flag & ~ignoredFlags) == 0) {
			continue;
		}

		std::string rawPath = path;
		bool pathExists = false;
		bool pathExistsKnown = false;

		for (auto& watch : watches) {
			if (watch.state->terminated.load()) {
				continue;
			}
			dirWatch* w = watch.w.get();
			auto [displayPath, ok] =
			    fseventsDisplayPathPrepared(w, &comparison);
			if (!ok) {
				continue;
			}

			// Skip events for the watched directory itself unless it's
			// been removed. fseventsd reports a change on the watched
			// dir when a child is added or removed; subscribers observe
			// changes *within* the directory, not the dir's own
			// metadata churn. (A removal of the dir is still propagated
			// because Watcher relies on it to tear down the stream.)
			if (displayPath == w->dir && !isRemoved && !isRenamed) {
				continue;
			}

			gostd::Error removedErr = gostd::errorf(
			    "%w: watched directory removed", {ErrWatchTerminated});
			if (isRemoved && !isCreated) {
				if (displayPath == w->dir) {
					w->events.removeWatchRootAt(displayPath, eventID);
				} else {
					w->events.removeAt(displayPath, eventID);
				}
				if (w->terminateCallbacksForDeletedRoot(displayPath,
				                                      eventID, removedErr)) {
					touched.insert(w);
				}
				if (displayPath == w->dir) {
					watch.state->terminated.store(true);
					w->events.setError(removedErr);
				}
			} else if (isRenamed || (isRemoved && isCreated)) {
				if (!pathExistsKnown) {
					struct stat st;
					pathExists = ::lstat(rawPath.c_str(), &st) == 0;
					pathExistsKnown = true;
				}
				if (pathExists) {
					if (displayPath == w->dir) {
						w->events.updateWatchRootAt(displayPath,
						                            eventID);
					} else {
						w->events.updateAt(displayPath, eventID);
					}
				} else {
					if (displayPath == w->dir) {
						w->events.removeWatchRootAt(displayPath,
						                            eventID);
					} else {
						w->events.removeAt(displayPath, eventID);
					}
					if (w->terminateCallbacksForDeletedRoot(
					        displayPath, eventID, removedErr)) {
						touched.insert(w);
					}
					if (displayPath == w->dir) {
						watch.state->terminated.store(true);
						w->events.setError(removedErr);
					}
				}
			} else {
				if (displayPath == w->dir) {
					w->events.updateWatchRootAt(displayPath, eventID);
				} else {
					w->events.updateAt(displayPath, eventID);
				}
			}
			touched.insert(w);
		}
	}

	for (dirWatch* w : touched) {
		w->notify();
	}
}

// cEventCallback — the C entry point FSEvents calls (the asm trampoline's
// counterpart). info is the fseventsStream* this stream was created for.
void cEventCallback(ConstFSEventStreamRef /*streamRef*/, void* info,
                    size_t numEvents, void* eventPaths,
                    const FSEventStreamEventFlags* flags,
                    const FSEventStreamEventId* ids) {
	fsEventsCallback(static_cast<fseventsStream*>(info), numEvents,
	                 static_cast<CFArrayRef>(eventPaths), flags, ids);
}

// fseventsDisplayPath — fsevents_darwin.go:626.
std::pair<std::string, bool>
fseventsDisplayPathPrepared(dirWatch* w, comparisonPath* rawPath) {
	comparisonPath physical;
	physical.path = w->physicalDir;
	physical.folded = w->physicalDirFold;
	physical.ready = !w->physicalDirFold.empty();
	if (auto [p, ok] =
	        w->comparer.rebasePrepared(rawPath, physical, w->dir);
	    ok) {
		return {p, true};
	}
	if (w->physicalDir != w->dir) {
		comparisonPath logical;
		logical.path = w->dir;
		logical.folded = w->dirFold;
		logical.ready = !w->dirFold.empty();
		return w->comparer.rebasePrepared(rawPath, logical, w->dir);
	}
	return {"", false};
}

// fseventsOverflowMatches — fsevents_darwin.go:648.
bool fseventsOverflowMatchesPrepared(dirWatch* w, comparisonPath* rawPath) {
	comparisonPath physical;
	physical.path = w->physicalDir;
	physical.folded = w->physicalDirFold;
	physical.ready = !w->physicalDirFold.empty();
	if (w->comparer.suffixPrepared(physical, rawPath).second) {
		return true;
	}
	comparisonPath rawCopy = *rawPath;
	if (w->comparer.suffixPrepared(rawCopy, &physical).second) {
		return true;
	}
	if (w->physicalDir != w->dir) {
		comparisonPath logical;
		logical.path = w->dir;
		logical.folded = w->dirFold;
		logical.ready = !w->dirFold.empty();
		if (w->comparer.suffixPrepared(logical, rawPath).second) {
			return true;
		}
	}
	return false;
}

// teardownStream — fsevents_darwin_ffi.go: Stop → Invalidate → drain the
// queue (dispatch_sync with a noop so any in-flight callback finishes)
// → release.
void stopFSEventsStream(fseventsStream* stream) {
	FSEventStreamStop(stream->stream);
	FSEventStreamInvalidate(stream->stream);
	dispatch_sync(stream->queue, ^{
	});
	FSEventStreamRelease(stream->stream); // not CFRelease: FSEventStream
	                                      // is opaque, not a CFType
	dispatch_release(stream->queue);
}

void stopFSEventsStreams(std::vector<std::shared_ptr<fseventsStream>>& streams) {
	for (auto& s : streams) {
		stopFSEventsStream(s.get());
	}
	streams.clear();
}

// fsEventsBackend — fsevents_darwin.go:172.
struct fsEventsBackend : watcherBase {
	std::mutex fseventsMu;
	std::unordered_map<std::shared_ptr<dirWatch>,
	                   std::shared_ptr<fseventsState>>
	    watches;
	std::vector<std::shared_ptr<fseventsStream>> streams;

	fsEventsBackend() { init(); }

	// start — fsevents_darwin.go:195.
	gostd::Error start() override {
		notifyStarted();
		return nullptr;
	}

	// checkWatcher — fsevents_darwin.go:201.
	static gostd::Error checkWatcher(const std::shared_ptr<dirWatch>& w) {
		auto [info, err] = osStat(w->physicalDir);
		if (err != nullptr) {
			return std::make_shared<dirWatchErrorObj>(err, w);
		}
		if (!info.isDir) {
			return std::make_shared<dirWatchErrorObj>(errnoError(ENOTDIR),
			                                          w);
		}
		return nullptr;
	}

	std::vector<fseventsWatchSnapshot> activeWatchesLocked() {
		std::vector<fseventsWatchSnapshot> out;
		out.reserve(watches.size());
		for (auto& [w, state] : watches) {
			if (state->terminated.load()) {
				continue;
			}
			out.push_back({w, state});
		}
		return out;
	}

	// watchesForFSEventsPaths — fsevents_darwin.go:284.
	static std::vector<fseventsWatchSnapshot> watchesForFSEventsPaths(
	    const std::vector<fseventsWatchSnapshot>& all,
	    const std::vector<std::string>& paths) {
		if (paths.empty()) {
			return {};
		}
		std::vector<fseventsWatchSnapshot> filtered;
		filtered.reserve(all.size());
		for (auto& watch : all) {
			if (std::binary_search(paths.begin(), paths.end(),
			                       watch.w->physicalDir)) {
				filtered.push_back(watch);
			}
		}
		return filtered;
	}

	// startStream — fsevents_darwin.go:298.
	std::pair<std::shared_ptr<fseventsStream>, gostd::Error>
	startStream(const std::vector<std::string>& paths,
	            const std::vector<fseventsWatchSnapshot>& snapshot) {
		if (paths.empty()) {
			return {nullptr, nullptr};
		}
		CFMutableArrayRef cfStrings =
		    CFArrayCreateMutable(nullptr, (CFIndex)paths.size(),
		                         &kCFTypeArrayCallBacks);
		if (cfStrings == nullptr) {
			return {nullptr, errCFArrayCreateNull};
		}
		for (auto& p : paths) {
			CFStringRef s = CFStringCreateWithCString(
			    nullptr, p.c_str(), kCFStringEncodingUTF8);
			if (s == nullptr) {
				CFRelease(cfStrings);
				return {nullptr, errCFStringCreateNull};
			}
			CFArrayAppendValue(cfStrings, s);
			CFRelease(s);
		}
		CFArrayRef pathsToWatch =
		    CFArrayCreateCopy(nullptr, cfStrings);
		CFRelease(cfStrings);
		if (pathsToWatch == nullptr) {
			return {nullptr, errCFArrayCreateNull};
		}

		auto stream = std::make_shared<fseventsStream>();
		stream->watches = snapshot;

		FSEventStreamContext ctx{};
		ctx.info = stream.get();
		FSEventStreamRef ref = FSEventStreamCreate(
		    nullptr, &cEventCallback, &ctx, pathsToWatch,
		    kFSEventStreamEventIdSinceNow, 0.001,
		    kFSEventStreamCreateFlagUseCFTypes |
		        kFSEventStreamCreateFlagFileEvents);
		CFRelease(pathsToWatch);
		if (ref == nullptr) {
			return {nullptr, errStreamCreateNull};
		}
		stream->stream = ref;

		// Per-stream serial dispatch queue (Go's
		// dispatch_queue_create(..., DISPATCH_QUEUE_SERIAL)).
		stream->queue =
		    dispatch_queue_create("fswatch.fsevents", nullptr);
		FSEventStreamSetDispatchQueue(ref, stream->queue);
		if (!FSEventStreamStart(ref)) {
			stopFSEventsStream(stream.get());
			return {nullptr, errStreamStartFailed};
		}
		FSEventStreamFlushSync(ref);
		return {stream, nullptr};
	}

	// startFSEventsStreams — fsevents_darwin.go:247.
	gostd::Error startStreams(
	    const std::vector<fseventsWatchSnapshot>& snapshot,
	    std::vector<std::shared_ptr<fseventsStream>>& out) {
		out.clear();
		if (snapshot.empty()) {
			return nullptr;
		}
		std::unordered_set<std::string> seen;
		std::vector<std::string> paths;
		for (auto& watch : snapshot) {
			if (seen.insert(watch.w->physicalDir).second) {
				paths.push_back(watch.w->physicalDir);
			}
		}
		std::sort(paths.begin(), paths.end());

		auto [stream, err] = startStream(paths, snapshot);
		if (err == nullptr) {
			if (stream != nullptr) {
				out.push_back(std::move(stream));
			}
			return nullptr;
		}

		// Fall back to chunked streams of 512 paths each.
		size_t off = 0;
		while (off < paths.size()) {
			size_t n =
			    std::min(paths.size() - off, fseventsPathsPerStream);
			std::vector<std::string> chunk(paths.begin() + (long)off,
			                               paths.begin() +
			                                   (long)(off + n));
			auto [s, e] =
			    startStream(chunk, watchesForFSEventsPaths(snapshot, chunk));
			if (e != nullptr) {
				stopFSEventsStreams(out);
				return e;
			}
			if (s != nullptr) {
				out.push_back(std::move(s));
			}
			off += n;
		}
		return nullptr;
	}

	// subscribe — fsevents_darwin.go:404.
	gostd::Error subscribe(std::shared_ptr<dirWatch> w) override {
		return subscribeMany({w});
	}

	// subscribeMany — fsevents_darwin.go:408.
	bool supportsSubscribeMany() const override { return true; }
	gostd::Error subscribeMany(
	    const std::vector<std::shared_ptr<dirWatch>>& watchesToAdd)
	    override {
		if (watchesToAdd.empty()) {
			return nullptr;
		}
		std::unordered_map<std::shared_ptr<dirWatch>,
		                   std::shared_ptr<fseventsState>>
		    states;
		for (auto& w : watchesToAdd) {
			if (auto err = checkWatcher(w); err != nullptr) {
				return err;
			}
			states[w] = std::make_shared<fseventsState>();
		}

		std::vector<fseventsWatchSnapshot> snapshot;
		{
			std::lock_guard<std::mutex> lk(fseventsMu);
			for (auto& [w, state] : states) {
				w->state = std::any(state);
				watches[w] = state;
			}
			snapshot = activeWatchesLocked();
		}

		std::vector<std::shared_ptr<fseventsStream>> newStreams;
		if (auto err = startStreams(snapshot, newStreams);
		    err != nullptr) {
			std::lock_guard<std::mutex> lk(fseventsMu);
			for (auto& [w, state] : states) {
				auto it = watches.find(w);
				if (it != watches.end() && it->second == state) {
					watches.erase(it);
					w->state = std::any{};
				}
			}
			return std::make_shared<dirWatchErrorObj>(
			    err, watchesToAdd[0]);
		}

		std::vector<std::shared_ptr<fseventsStream>> oldStreams;
		{
			std::lock_guard<std::mutex> lk(fseventsMu);
			oldStreams.swap(streams);
			streams = std::move(newStreams);
		}
		stopFSEventsStreams(oldStreams);
		return nullptr;
	}

	// closeWatch — fsevents_darwin.go:450.
	gostd::Error closeWatch(std::shared_ptr<dirWatch> w) override {
		std::shared_ptr<fseventsState> state;
		if (auto* p =
		        std::any_cast<std::shared_ptr<fseventsState>>(&w->state);
		    p != nullptr) {
			state = *p;
		}
		w->state = std::any{};
		if (state == nullptr) {
			return nullptr;
		}
		state->terminated.store(true);

		std::vector<fseventsWatchSnapshot> snapshot;
		{
			std::lock_guard<std::mutex> lk(fseventsMu);
			watches.erase(w);
			snapshot = activeWatchesLocked();
		}

		std::vector<std::shared_ptr<fseventsStream>> newStreams;
		if (auto err = startStreams(snapshot, newStreams);
		    err != nullptr) {
			return err;
		}
		std::vector<std::shared_ptr<fseventsStream>> oldStreams;
		{
			std::lock_guard<std::mutex> lk(fseventsMu);
			oldStreams.swap(streams);
			streams = std::move(newStreams);
		}
		stopFSEventsStreams(oldStreams);
		return nullptr;
	}
};

fsEventsBackend* newFSEventsBackend() { return new fsEventsBackend(); }

} // namespace

// Go's init() (fsevents_darwin.go:182): register the factory + event-ID
// sequence source on the package watcher.
watcher& fseventsWatcher() {
	static watcher w{"fsevents"};
	static bool registered = [] {
		w.factory = []() -> watcherImpl* { return newFSEventsBackend(); };
		w.sequence = []() -> uint64_t {
			return FSEventsGetCurrentEventId();
		};
		return true;
	}();
	(void)registered;
	return w;
}

} // namespace tsc::fswatch

#endif // __APPLE__
