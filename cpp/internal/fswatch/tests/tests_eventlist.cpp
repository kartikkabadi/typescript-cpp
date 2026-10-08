// Port of eventlist_test.go — eventList coalescing and drain semantics.

#include "internal/fswatch/tests/util.h"

namespace tsc::fswatch {

namespace {

// clear is only used by tests (Go adds it as a test-only method on
// eventList); live code drains via drain() so the snapshot and the reset
// happen atomically.
void eventListClear(eventList& el) {
	std::lock_guard<std::mutex> lk(el.mu);
	el.entries.clear();
	el.err = nullptr;
}

void TestEventListCreateThenDelete(T* t) {
	t->Parallel();
	eventList el;
	el.create("a");
	el.remove("a");
	if (el.size() != 1) {
		t->Fatalf("size after create+remove want 1, got %d", {(int)el.size()});
	}
	if (auto got = el.getEvents(); !got.empty()) {
		t->Fatalf("getEvents should drop create+delete, got %d events",
		          {(int)got.size()});
	}
}
REGISTER_UNIT_TEST("fswatch.TestEventListCreateThenDelete",
                   TestEventListCreateThenDelete);

void TestEventListDeleteThenCreate(T* t) {
	t->Parallel();
	eventList el;
	el.remove("a");
	el.create("a");
	auto got = el.getEvents();
	if (got.size() != 1) {
		t->Fatalf("expected 1 event, got %d", {(int)got.size()});
	}
	// "Assume update event when rapidly removed and created".
	if (got[0].kind != EventKind::EventUpdate) {
		t->Fatalf("expected update, got %d", {(int)got[0].kind});
	}
}
REGISTER_UNIT_TEST("fswatch.TestEventListDeleteThenCreate",
                   TestEventListDeleteThenCreate);

void TestEventListCreateDeleteCreate(T* t) {
	t->Parallel();
	eventList el;
	el.create("a");
	el.remove("a");
	el.create("a");
	auto got = el.getEvents();
	if (got.size() != 1) {
		t->Fatalf("expected 1 event, got %d", {(int)got.size()});
	}
	if (got[0].kind != EventKind::EventUpdate) {
		t->Fatalf("create+delete+create should coalesce to update, got %d",
		          {(int)got[0].kind});
	}
}
REGISTER_UNIT_TEST("fswatch.TestEventListCreateDeleteCreate",
                   TestEventListCreateDeleteCreate);

void TestEventListErrorIsLatchedAndCleared(T* t) {
	t->Parallel();
	eventList el;
	if (el.hasError()) {
		t->Fatal({"fresh eventList should have no error"});
	}
	if (auto got = el.getError(); got != nullptr) {
		t->Fatalf("fresh getError want nil, got %v", {got});
	}
	el.setError(gostd::newError("first"));
	el.setError(gostd::newError("second")); // only first wins
	if (!el.hasError()) {
		t->Fatal({"hasError should be true after setError"});
	}
	if (auto got = el.getError(); got == nullptr || got->Error() != "first") {
		t->Fatalf("getError want first, got %v", {got});
	}
	eventListClear(el);
	if (el.hasError()) {
		t->Fatal({"clear should drop the error"});
	}
	if (auto got = el.getError(); got != nullptr) {
		t->Fatalf("post-clear getError want nil, got %v", {got});
	}
}
REGISTER_UNIT_TEST("fswatch.TestEventListErrorIsLatchedAndCleared",
                   TestEventListErrorIsLatchedAndCleared);

void TestEventListDrainIsAtomic(T* t) {
	t->Parallel();
	eventList el;
	el.create("a");
	el.update("b");
	el.setError(gostd::newError("oops"));

	auto [events, err] = el.drain();
	if (err == nullptr) {
		t->Fatal({"drain should return the error"});
	}
	if (events.size() != 2) {
		t->Fatalf("drain should return 2 events, got %d",
		          {(int)events.size()});
	}

	auto [events2, err2] = el.drain();
	if (err2 != nullptr) {
		t->Fatalf("second drain should have no error, got %v", {err2});
	}
	if (!events2.empty()) {
		t->Fatalf("second drain should be empty, got %d",
		          {(int)events2.size()});
	}
}
REGISTER_UNIT_TEST("fswatch.TestEventListDrainIsAtomic",
                   TestEventListDrainIsAtomic);

void TestEventListDrainReturnsErrorWithEvents(T* t) {
	t->Parallel();
	eventList el;
	el.create("file.txt");
	el.setError(gostd::newError("overflow"));

	auto [events, err] = el.drain();
	if (err == nullptr) {
		t->Fatal({"expected error from drain"});
	}
	if (events.size() != 1) {
		t->Fatalf("expected 1 event alongside error, got %d",
		          {(int)events.size()});
	}
}
REGISTER_UNIT_TEST("fswatch.TestEventListDrainReturnsErrorWithEvents",
                   TestEventListDrainReturnsErrorWithEvents);

void TestEventListDrainForSequences(T* t) {
	t->Parallel();
	eventList el;
	el.create("file.txt");
	uint64_t startAfterCreate = el.sequence();
	el.remove("file.txt");

	auto [eventsByCallback, err] =
	    el.drainForSequences({0, startAfterCreate});
	if (err != nullptr) {
		t->Fatal({err});
	}
	if (!eventsByCallback[0].empty()) {
		t->Fatalf("create+delete should cancel for original callback, got %d",
		          {(int)eventsByCallback[0].size()});
	}
	if (eventsByCallback[1].size() != 1) {
		t->Fatalf("expected delete for later callback, got %d",
		          {(int)eventsByCallback[1].size()});
	}
	auto& got = eventsByCallback[1][0];
	if (got.kind != EventKind::EventDelete || got.path != "file.txt") {
		t->Fatalf("expected delete for file.txt, got kind=%d path=%s",
		          {(int)got.kind, got.path});
	}
}
REGISTER_UNIT_TEST("fswatch.TestEventListDrainForSequences",
                   TestEventListDrainForSequences);

} // namespace
} // namespace tsc::fswatch
