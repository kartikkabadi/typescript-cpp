// Port of tsc/internal/project/background/queue_test.go.
#include <atomic>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/project/background/background.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

void TestQueue(tsc::gostd::testing::T* t) {
	using tsc::gostd::contextBackground;
	namespace assert = tsc::gotest::assert;

	t->Parallel();
	t->Run("BasicEnqueue", [](tsc::gostd::testing::T* t) {
		t->Parallel();
		auto* q = tsc::background::NewQueue();
		t->Cleanup([q] {
			q->Close();
			delete q;
		});

		bool executed = false;
		q->Enqueue(contextBackground(),
		           [&](tsc::gostd::Context ctx) { executed = true; });

		q->Wait();

		assert::Check(t, executed);
	});

	t->Run("MultipleTasksExecution", [](tsc::gostd::testing::T* t) {
		t->Parallel();
		auto* q = tsc::background::NewQueue();
		t->Cleanup([q] {
			q->Close();
			delete q;
		});

		std::atomic<int64_t> counter{0};
		int numTasks = 10;

		for (int i = 0; i < numTasks; i++) {
			q->Enqueue(contextBackground(),
			           [&](tsc::gostd::Context ctx) { counter++; });
		}

		q->Wait();

		assert::Equal(t, counter.load(), (int64_t)numTasks);
	});

	t->Run("NestedEnqueue", [](tsc::gostd::testing::T* t) {
		t->Parallel();
		auto* q = tsc::background::NewQueue();
		t->Cleanup([q] {
			q->Close();
			delete q;
		});

		std::vector<std::string> executed;
		std::mutex mu;

		q->Enqueue(contextBackground(), [&](tsc::gostd::Context ctx) {
			{
				std::lock_guard<std::mutex> lk(mu);
				executed.push_back("parent");
			}

			q->Enqueue(ctx, [&](tsc::gostd::Context childCtx) {
				std::lock_guard<std::mutex> lk(mu);
				executed.push_back("child");
			});
		});

		q->Wait();

		std::lock_guard<std::mutex> lk(mu);

		assert::Equal(t, (int)executed.size(), 2);
	});

	t->Run("ClosedQueueRejectsNewTasks", [](tsc::gostd::testing::T* t) {
		t->Parallel();
		auto* q = tsc::background::NewQueue();
		q->Close();
		t->Cleanup([q] { delete q; });

		bool executed = false;
		q->Enqueue(contextBackground(),
		           [&](tsc::gostd::Context ctx) { executed = true; });

		q->Wait();

		assert::Check(t, !executed,
		              "Task should not execute after queue is closed");
	});

	t->Run("CloseWaitsForActiveTasks", [](tsc::gostd::testing::T* t) {
		t->Parallel();
		auto* q = tsc::background::NewQueue();
		t->Cleanup([q] { delete q; });
		std::promise<void> started;
		std::promise<void> finish;
		std::promise<void> closeStarted;
		std::promise<void> closed;
		auto startedFut = started.get_future();
		auto finishFut = finish.get_future();
		auto closeStartedFut = closeStarted.get_future();
		auto closedFut = closed.get_future();

		q->Enqueue(contextBackground(),
		           [&](tsc::gostd::Context ctx) {
			           started.set_value();
			           finishFut.wait();
		           });
		startedFut.wait();

		std::thread([&] {
			closeStarted.set_value();
			q->Close();
			closed.set_value();
		}).detach();

		closeStartedFut.wait();
		bool closeReturned =
		    closedFut.wait_for(std::chrono::milliseconds(100)) ==
		    std::future_status::ready;
		assert::Check(
		    t, !closeReturned,
		    "Close returned before the active task completed");

		finish.set_value();
		closedFut.wait();
	});
}

}  // namespace

REGISTER_UNIT_TEST("project/background.TestQueue", TestQueue);
