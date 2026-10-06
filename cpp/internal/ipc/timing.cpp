// timing.cpp — Port of tsc/internal/ipc/timing.go: the connection-level
// server-side request-timing feature (timingCollector + the
// getServerTiming/resetServerTiming snapshot marshaling).
//
// The Go handlers marshal serverTimingInfo via encoding/json inside
// WriteResponse; the ported Protocol surface takes a pre-marshaled
// json::Value, so serverTimingSnapshot marshals here (field order = Go
// struct declaration order).

#include "internal/ipc/ipc.h"

#include <chrono>

namespace tsc::ipc {

namespace {

// unixMillis — time.Now().UnixMilli().
int64_t unixMillis() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
	           std::chrono::system_clock::now().time_since_epoch())
	    .count();
}

// marshalServerTimingInfo — json.Marshal(serverTimingInfo).
json::Value marshalServerTimingInfo(const serverTimingInfo& i) {
	json::Value totals = json::marshalObject(
	    {{"requestCount",
	      json::marshalInt64(static_cast<int64_t>(i.totals.requestCount))},
	     {"totalProcessingTimeMs",
	      json::detail::goFloat(i.totals.totalProcessingTimeMs)}});
	std::vector<std::string> recents;
	recents.reserve(i.recentRequests.size());
	for (const auto& r : i.recentRequests) {
		recents.push_back(json::marshalObject(
		    {{"method", json::marshalString(r.method)},
		     {"processingTimeMs",
		      json::detail::goFloat(r.processingTimeMs)},
		     {"timestamp", json::marshalInt64(r.timestamp)}}));
	}
	return json::marshalObject({{"enabled", json::marshalBool(i.enabled)},
	                            {"totals", std::move(totals)},
	                            {"recentRequests",
	                             json::marshalArray(recents)}});
}

// disabledServerTimingInfo — timing.go:135: the snapshot returned when
// timing collection is not enabled.
serverTimingInfo disabledServerTimingInfo() {
	return serverTimingInfo{false, serverTimingTotals{},
	                        std::vector<serverRequestTiming>{}};
}

} // namespace

// newTimingCollector — timing.go:69.
std::unique_ptr<timingCollector> newTimingCollector() {
	return std::make_unique<timingCollector>();
}

// timingCollector::record — timing.go:73. Adds a single request's
// processing time to the totals and ring buffer.
void timingCollector::record(std::string_view method, gostd::Duration d) {
	double processingMs = durationToMillis(d);

	std::lock_guard<std::mutex> lk(mu);

	totals.requestCount++;
	totals.totalProcessingTimeMs += processingMs;

	serverRequestTiming entry{std::string(method), processingMs,
	                          unixMillis()};
	if (ring.size() < serverRecentRequestCapacity) {
		ring.push_back(std::move(entry));
	} else {
		ring[head] = std::move(entry);
		head = (head + 1) % serverRecentRequestCapacity;
	}
}

// timingCollector::snapshot — timing.go:95. A copy of the collected timing,
// with recent requests ordered oldest to newest.
serverTimingInfo timingCollector::snapshot() {
	std::lock_guard<std::mutex> lk(mu);

	std::vector<serverRequestTiming> recent;
	recent.reserve(ring.size());
	for (size_t i = 0; i < ring.size(); i++) {
		recent.push_back(ring[(head + i) % ring.size()]);
	}
	return serverTimingInfo{true, totals, std::move(recent)};
}

// timingCollector::reset — timing.go:109. Clears all accumulated totals and
// recent-request history.
void timingCollector::reset() {
	std::lock_guard<std::mutex> lk(mu);

	totals = serverTimingTotals{};
	ring.clear();
	head = 0;
}

// serverTimingSnapshot — timing.go:126.
json::Value serverTimingSnapshot(timingCollector* c) {
	if (c == nullptr) {
		return marshalServerTimingInfo(disabledServerTimingInfo());
	}
	return marshalServerTimingInfo(c->snapshot());
}

// durationToMillis — timing.go:142. Converts a duration to fractional
// milliseconds, clamped to be non-negative. Preserves sub-microsecond
// precision by converting from the full nanosecond duration.
double durationToMillis(gostd::Duration d) {
	if (d.count() < 0) {
		return 0;
	}
	return static_cast<double>(d.count()) / 1e6; // float64(d)/time.Millisecond
}

} // namespace tsc::ipc
