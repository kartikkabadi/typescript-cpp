// debounce.go — port of tsc/internal/fswatch/debounce.go: the resettable
// latch + coalescing timer that batches event callbacks per backend.
//
// The Go version uses two channels (waitCh = persistent gate closed on the
// first trigger of a cycle, triggerCh = per-trigger channel replaced on each
// trigger). Here each channel close/replacement is modelled as a generation
// counter bump on a shared condvar: latchWait blocks until waitGen advances,
// coalesceWait does a timed wait on triggerGen.

#include "internal/fswatch/fswatch.h"

#include <thread>

namespace tsc::fswatch {

// newDebounce — debounce.go:41-47.
debounce* newDebounce() {
	auto* d = new debounce();
	std::thread([d] { d->loop(); }).detach();
	return d;
}

// add registers a callback under key.
void debounce::add(dirWatch* key, std::function<void()> cb) {
	std::lock_guard<std::mutex> lk(mu);
	callbacks[key] = std::move(cb);
}

// remove deregisters the callback for key.
void debounce::remove(dirWatch* key) {
	std::lock_guard<std::mutex> lk(mu);
	callbacks.erase(key);
}

// trigger wakes the debounce loop.
void debounce::trigger() {
	std::lock_guard<std::mutex> lk(latchMu);
	if (!notified) {
		notified = true;
		waitGen++; // close(waitChLocked())
	}
	triggerGen++; // close(triggerChLocked()); triggerCh = make(chan)
	latchCv.notify_all();
}

void debounce::loop() {
	while (true) {
		latchWait();
		notifyIfReady();
	}
}

void debounce::notifyIfReady() {
	std::unique_lock<std::mutex> lk(mu);
	gostd::Time now = gostd::now();
	gostd::Duration gap = now - lastTime;
	if (gap > maxWaitTime) {
		lastTime = now;
		lk.unlock();
		fireCallbacks();
		return;
	}
	lk.unlock();
	coalesceWait();
}

void debounce::coalesceWait() {
	std::unique_lock<std::mutex> lk(latchMu);
	uint64_t gen = triggerGen; // ch := d.triggerChLocked()
	// select <-ch vs time.After(minWaitTime).
	bool triggered =
	    latchCv.wait_for(lk, minWaitTime, [&] { return triggerGen != gen; });
	lk.unlock();
	if (!triggered) {
		fireCallbacks();
	}
	// triggered: do nothing; new event triggered, fire on the next tick.
}

// fireCallbacks snapshots and invokes all registered callbacks.
void debounce::fireCallbacks() {
	std::vector<std::function<void()>> cbs;
	{
		std::lock_guard<std::mutex> lk(mu);
		lastTime = gostd::now();
		cbs.reserve(callbacks.size());
		for (auto& kv : callbacks) {
			cbs.push_back(kv.second);
		}
	}
	latchReset();
	for (auto& cb : cbs) {
		cb();
	}
}

// latchWait — <-waitCh: block until the current wait channel is closed.
void debounce::latchWait() {
	std::unique_lock<std::mutex> lk(latchMu);
	uint64_t gen = waitGen;
	latchCv.wait(lk, [&] { return waitGen != gen; });
}

// latchReset — if notified, reopen waitCh (new channel = bumped generation).
void debounce::latchReset() {
	std::lock_guard<std::mutex> lk(latchMu);
	if (notified) {
		notified = false;
		waitGen++; // d.waitCh = make(chan struct{})
	}
}

} // namespace tsc::fswatch
