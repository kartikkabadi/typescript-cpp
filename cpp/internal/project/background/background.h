// Port of tsc/internal/project/background/queue.go — background task queue.
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <thread>

#include "internal/gostd/gostd.h"

namespace tsc::background {

// Queue manages background tasks execution
class Queue {
	// wg — sync.WaitGroup equivalent.
	std::mutex wgMu;
	std::condition_variable wgCv;
	int wgCount = 0;

	std::shared_mutex mu;
	bool closed = false;

public:
	Queue() = default;
	Queue(const Queue&) = delete;
	Queue& operator=(const Queue&) = delete;

	// Enqueue returns false when the task was dropped (queue closed or
	// the context already cancelled) so callers can unwind any resources
	// they captured for it.
	bool Enqueue(const gostd::Context& ctx,
	             const std::function<void(gostd::Context)>& fn) {
		mu.lock_shared();
		struct unlockGuard {
			std::shared_mutex& m;
			bool held = true;
			void release() {
				if (held) {
					m.unlock_shared();
					held = false;
				}
			}
			~unlockGuard() { release(); }
		} ug{mu};
		if (closed) {
			return false;
		}

		// Don't start new tasks if context is already cancelled
		if (gostd::ctxErr(ctx) != nullptr) {
			return false;
		}

		// wg.Add(1) while still holding the read lock so Close cannot
		// observe a closed queue with a task it never waited on.
		{
			std::lock_guard<std::mutex> lk(wgMu);
			++wgCount;
		}
		ug.release();
		std::thread([this, ctx, fn] {
			struct doneGuard {
				Queue* q;
				~doneGuard() {
					std::unique_lock<std::mutex> lk(q->wgMu);
					if (--q->wgCount == 0) {
						lk.unlock();
						q->wgCv.notify_all();
					}
				}
			} dg{this};
			// Check context again before executing
			if (gostd::ctxErr(ctx) == nullptr) {
				fn(ctx);
			}
		}).detach();
		return true;
	}

	// Wait waits for all active tasks to complete.
	// It does not prevent new tasks from being enqueued while waiting.
	void Wait() {
		std::unique_lock<std::mutex> lk(wgMu);
		wgCv.wait(lk, [this] { return wgCount == 0; });
	}

	void Close() {
		{
			std::lock_guard<std::shared_mutex> lk(mu);
			closed = true;
		}
		Wait();
	}
};

// NewQueue creates a new background queue for managing background tasks
// execution.
inline Queue* NewQueue() { return new Queue(); }

} // namespace tsc::background
