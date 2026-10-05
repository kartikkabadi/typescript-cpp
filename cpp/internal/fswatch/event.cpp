// event.go — port of tsc/internal/fswatch/event.go: event kinds and the
// eventList coalescing buffer.

#include "internal/fswatch/fswatch.h"

#include <algorithm>

namespace tsc::fswatch {

// EventKind.String — event.go:13-22.
std::string_view eventKindString(EventKind k) {
	switch (k) {
	case EventKind::EventUpdate:
		return "update";
	case EventKind::EventDelete:
		return "delete";
	default:
		return "unknown";
	}
}

// create records a new-file event for path. Both create and update
// produce EventUpdate externally; sequence state tracks coalescing
// (create+delete within a batch cancels out).
void eventList::create(const std::string& path) {
	std::lock_guard<std::mutex> lk(mu);
	uint64_t seq = nextSeqLocked();
	createLocked(path, seq);
}

void eventList::createAt(const std::string& path, uint64_t seq) {
	std::lock_guard<std::mutex> lk(mu);
	advanceSeqLocked(seq);
	createLocked(path, seq);
}

void eventList::createLocked(const std::string& path, uint64_t seq) {
	eventEntry& entry = getOrCreate(path);
	if (entry.isDeleted()) {
		// Rapid delete+recreate: clear both flags so the entry
		// emits EventUpdate (the default for non-deleted entries).
		// https://github.com/parcel-bundler/watcher/issues/72
		entry.deletedSeq = 0;
		entry.createdSeq = 0;
		entry.updatedSeq = seq;
	} else {
		entry.createdSeq = seq;
	}
}

// update records an update event for path.
void eventList::update(const std::string& path) {
	std::lock_guard<std::mutex> lk(mu);
	uint64_t seq = nextSeqLocked();
	updateLocked(path, seq);
}

void eventList::updateAt(const std::string& path, uint64_t seq) {
	std::lock_guard<std::mutex> lk(mu);
	advanceSeqLocked(seq);
	updateLocked(path, seq);
}

void eventList::updateWatchRootAt(const std::string& path, uint64_t seq) {
	std::lock_guard<std::mutex> lk(mu);
	advanceSeqLocked(seq);
	updateLocked(path, seq);
	getOrCreate(path).includedWatchRoot = true;
}

void eventList::updateLocked(const std::string& path, uint64_t seq) {
	getOrCreate(path).updatedSeq = seq;
}

// remove records a delete event for path.
void eventList::remove(const std::string& path) {
	std::lock_guard<std::mutex> lk(mu);
	uint64_t seq = nextSeqLocked();
	removeLocked(path, seq);
}

uint64_t eventList::removeAndGetSequence(const std::string& path) {
	std::lock_guard<std::mutex> lk(mu);
	uint64_t seq = nextSeqLocked();
	removeLocked(path, seq);
	return seq;
}

void eventList::removeAt(const std::string& path, uint64_t seq) {
	std::lock_guard<std::mutex> lk(mu);
	advanceSeqLocked(seq);
	removeLocked(path, seq);
}

void eventList::removeWatchRootAt(const std::string& path, uint64_t seq) {
	std::lock_guard<std::mutex> lk(mu);
	advanceSeqLocked(seq);
	removeLocked(path, seq);
	getOrCreate(path).includedWatchRoot = true;
}

void eventList::removeLocked(const std::string& path, uint64_t seq) {
	eventEntry& entry = getOrCreate(path);
	entry.deletedSeq = seq;
}

// size returns the number of tracked entries (including ones that may
// cancel out in getEvents).
size_t eventList::size() {
	std::lock_guard<std::mutex> lk(mu);
	return entries.size();
}

// snapshotLocked returns the current set of pending events with
// create+delete pairs filtered out. Caller must hold el.mu.
std::vector<Event> eventList::snapshotLocked() {
	return snapshotSinceLocked(0);
}

std::vector<Event> eventList::snapshotSinceLocked(uint64_t startSeq) {
	std::vector<Event> out;
	out.reserve(entries.size());
	for (auto& kv : entries) {
		auto [kind, ok] = kv.second.kindSince(startSeq);
		if (!ok) {
			continue;
		}
		out.push_back(Event{kind, kv.first, kv.second.includedWatchRoot});
	}
	return out;
}

// getEvents returns a snapshot of events, skipping entries that were both
// created and deleted. Order is not guaranteed.
std::vector<Event> eventList::getEvents() {
	std::lock_guard<std::mutex> lk(mu);
	return snapshotLocked();
}

// drain atomically snapshots all pending events and the stored error,
// then clears the list. This prevents events added between a separate
// getEvents+clear from being silently dropped.
std::pair<std::vector<Event>, gostd::Error> eventList::drain() {
	std::lock_guard<std::mutex> lk(mu);
	std::vector<Event> out = snapshotLocked();
	gostd::Error errOut = err;
	entries.clear();
	err = nullptr;
	return {out, errOut};
}

std::pair<std::vector<std::vector<Event>>, gostd::Error>
eventList::drainForSequences(const std::vector<uint64_t>& startSeqs) {
	std::lock_guard<std::mutex> lk(mu);
	std::vector<std::vector<Event>> out(startSeqs.size());
	for (size_t i = 0; i < startSeqs.size(); i++) {
		out[i] = snapshotSinceLocked(startSeqs[i]);
	}
	gostd::Error errOut = err;
	entries.clear();
	err = nullptr;
	return {out, errOut};
}

// setError stores the first error encountered (later errors are ignored).
void eventList::setError(const gostd::Error& e) {
	std::lock_guard<std::mutex> lk(mu);
	if (err == nullptr) {
		err = e;
	}
}

// hasError reports whether an error has been recorded.
bool eventList::hasError() {
	std::lock_guard<std::mutex> lk(mu);
	return err != nullptr;
}

// getError returns the stored error (or nil if none).
gostd::Error eventList::getError() {
	std::lock_guard<std::mutex> lk(mu);
	return err;
}

eventEntry& eventList::getOrCreate(const std::string& path) {
	return entries[path];
}

uint64_t eventList::sequence() {
	std::lock_guard<std::mutex> lk(mu);
	return seq;
}

uint64_t eventList::nextSeqLocked() {
	seq++;
	return seq;
}

void eventList::advanceSeqLocked(uint64_t s) {
	if (s > seq) {
		seq = s;
	}
}

bool eventEntry::isDeleted() const {
	return deletedSeq > createdSeq && deletedSeq > updatedSeq;
}

std::pair<EventKind, bool> eventEntry::kindSince(uint64_t startSeq) const {
	if (deletedSeq > startSeq) {
		if (createdSeq > startSeq && createdSeq < deletedSeq &&
		    updatedSeq < deletedSeq) {
			return {EventKind{}, false};
		}
		return {EventKind::EventDelete, true};
	}
	uint64_t seq = std::max(createdSeq, updatedSeq);
	if (seq > startSeq) {
		return {EventKind::EventUpdate, true};
	}
	return {EventKind{}, false};
}

} // namespace tsc::fswatch
