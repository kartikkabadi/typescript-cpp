// Port of tsc/internal/lsp/dynamic_queue_test.go (internal package test).
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsp.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace gostd = tsc::gostd;
namespace lsp = tsc::lsp;
using tsc::gostd::testing::T;

void TestDynamicQueueFIFO(T* t) {
	t->Parallel();

	auto ctx = t->Context();
	lsp::dynamicQueue<int> q;

	for (int i = 0; i < 1000; i++) {
		if (auto err = q.Put(ctx, i); err != nullptr) {
			t->Fatalf("Put() error = %v", {err});
		}
	}

	for (int i = 0; i < 1000; i++) {
		auto [got, err] = q.Get(ctx);
		if (err != nullptr) {
			t->Fatalf("Get() error = %v", {err});
		}
		if (*got != i) {
			t->Fatalf("Get() = %d, want %d", {*got, i});
		}
	}
}
REGISTER_UNIT_TEST("lsp.TestDynamicQueueFIFO", TestDynamicQueueFIFO);

void TestDynamicQueueGetCancellation(T* t) {
	t->Parallel();

	auto [ctx, cancel] = gostd::contextWithCancel(t->Context());
	cancel();

	lsp::dynamicQueue<int> q;
	auto [got, err] = q.Get(ctx);
	if (!gostd::errorIs(err, gostd::errCanceled)) {
		t->Fatalf("Get() error = %v, want %v", {err, gostd::errCanceled});
	}
	if (got.has_value() && *got != 0) {
		t->Fatalf("Get() = %d, want zero value", {*got});
	}
}
REGISTER_UNIT_TEST("lsp.TestDynamicQueueGetCancellation",
                   TestDynamicQueueGetCancellation);

void TestDynamicQueuePutCancellationWhileStateUnavailable(T* t) {
	t->Parallel();

	lsp::dynamicQueue<int> q;
	auto err = q.acquireStateForTest(t->Context());
	if (err != nullptr) {
		t->Fatalf("getAny() error = %v", {err});
	}

	auto [ctx, cancel] = gostd::contextWithCancel(t->Context());
	cancel();

	auto putErr = q.Put(ctx, 1);
	if (!gostd::errorIs(putErr, gostd::errCanceled)) {
		t->Fatalf("Put() error = %v, want %v", {putErr, gostd::errCanceled});
	}

	q.releaseStateToIdleForTest();

	err = q.Put(t->Context(), 2);
	if (err != nullptr) {
		t->Fatalf("Put() error = %v", {err});
	}
	auto [got, getErr] = q.Get(t->Context());
	if (getErr != nullptr) {
		t->Fatalf("Get() error = %v", {getErr});
	}
	if (*got != 2) {
		t->Fatalf("Get() = %d, want 2", {*got});
	}
}
REGISTER_UNIT_TEST("lsp.TestDynamicQueuePutCancellationWhileStateUnavailable",
                   TestDynamicQueuePutCancellationWhileStateUnavailable);

}  // namespace
