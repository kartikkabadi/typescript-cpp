// tests_timing.cpp — port of tsc/internal/ipc/timing_test.go.
#include <string>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/ipc/ipc.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::ipc {
namespace {

using gostd::testing::T;

void TestTimingCollector(T* t) {
	t->Parallel();

	t->Run("accumulates totals and records recent requests", [](T* t) {
		t->Parallel();
		auto c = newTimingCollector();
		c->record("getSourceFile", gostd::Duration(2000000));
		c->record("getSymbolAtPosition", gostd::Duration(500000));

		auto snap = c->snapshot();
		if (snap.enabled != true)
			t->Error({"expected enabled"});
		if (snap.totals.requestCount != uint64_t(2))
			t->Error({"expected 2 requests"});
		if (snap.totals.totalProcessingTimeMs != 2.5)
			t->Error({gostd::sprintf(
			    "total = %v, want 2.5",
			    {snap.totals.totalProcessingTimeMs})});
		if (snap.recentRequests.size() != 2)
			t->Error({"expected 2 recent"});
		if (snap.recentRequests[0].method != "getSourceFile")
			t->Error({"expected getSourceFile"});
		if (snap.recentRequests[0].processingTimeMs != 2.0)
			t->Error({"expected 2.0"});
		if (snap.recentRequests[1].method != "getSymbolAtPosition")
			t->Error({"expected getSymbolAtPosition"});
		if (snap.recentRequests[1].processingTimeMs != 0.5)
			t->Error({"expected 0.5"});
	});

	t->Run("ring buffer retains only the most recent requests, oldest to "
	       "newest",
	       [](T* t) {
		       t->Parallel();
		       auto c = newTimingCollector();
		       std::vector<std::string> methods = {"a", "b", "c", "d",
		                                           "e", "f", "g"};
		       for (auto& m : methods) {
			       c->record(m, gostd::Duration(1000000));
		       }

		       auto snap = c->snapshot();
		       if (snap.totals.requestCount != uint64_t(7))
			       t->Error({"expected 7 requests"});
		       if ((int)snap.recentRequests.size() !=
		           serverRecentRequestCapacity)
			       t->Error({"expected capacity-limited ring"});

		       auto want = std::vector<std::string>(
		           methods.end() - serverRecentRequestCapacity,
		           methods.end());
		       for (size_t i = 0; i < want.size(); i++) {
			       if (snap.recentRequests[i].method != want[i])
				       t->Error({gostd::sprintf(
				           "recent[%d] = %s, want %s",
				           {(int)i, snap.recentRequests[i].method,
				            want[i]})});
		       }
	       });

	t->Run("negative durations clamp to zero", [](T* t) {
		t->Parallel();
		auto c = newTimingCollector();
		c->record("x", -5 * gostd::second());
		auto snap = c->snapshot();
		if (snap.totals.totalProcessingTimeMs != 0.0)
			t->Error({"expected 0.0"});
		if (snap.recentRequests[0].processingTimeMs != 0.0)
			t->Error({"expected 0.0"});
	});
}
REGISTER_UNIT_TEST("ipc.TestTimingCollector", TestTimingCollector);

void TestServerTimingSnapshotDisabled(T* t) {
	t->Parallel();
	// The port marshals inside serverTimingSnapshot; parse the Value to
	// read the same fields Go asserts on the info struct.
	auto [snap, parseErr] = json::parse(serverTimingSnapshot(nullptr));
	if (parseErr)
		t->Fatal({parseErr->Error()});
	auto* enabled = json::objGet(snap, "enabled");
	auto* totals = json::objGet(snap, "totals");
	auto* recent = json::objGet(snap, "recentRequests");
	if (enabled == nullptr)
		t->Fatal({"missing enabled"});
	if (totals == nullptr)
		t->Fatal({"missing totals"});
	if (recent == nullptr)
		t->Fatal({"missing recentRequests"});
	auto [en, _] = json::asBool(*enabled, "bool");
	if (en != false)
		t->Error({"expected disabled"});
	auto* count = json::objGet(*totals, "requestCount");
	if (count == nullptr)
		t->Fatal({"missing requestCount"});
	auto [n, _2] = json::asInt(*count, "uint64");
	if (n != 0)
		t->Error({"expected 0 requests"});
	if (recent->kind != json::Dom::K::Array || !recent->arr.empty())
		t->Error({"expected no recent requests"});
}
REGISTER_UNIT_TEST("ipc.TestServerTimingSnapshotDisabled",
                   TestServerTimingSnapshotDisabled);

void TestTimingCollectorReset(T* t) {
	t->Parallel();
	auto c = newTimingCollector();
	c->record("a", gostd::Duration(1000000));
	c->record("b", gostd::Duration(1000000));
	c->reset();

	auto snap = c->snapshot();
	if (snap.enabled != true)
		t->Error({"expected enabled"});
	if (snap.totals.requestCount != uint64_t(0))
		t->Error({"expected 0 requests"});
	if (snap.totals.totalProcessingTimeMs != 0.0)
		t->Error({"expected 0.0"});
	if (!snap.recentRequests.empty())
		t->Error({"expected no recent requests"});

	// The collector remains usable after a reset.
	c->record("c", gostd::Duration(2000000));
	snap = c->snapshot();
	if (snap.totals.requestCount != uint64_t(1))
		t->Error({"expected 1 request"});
	if (snap.recentRequests[0].method != "c")
		t->Error({"expected c"});
}
REGISTER_UNIT_TEST("ipc.TestTimingCollectorReset", TestTimingCollectorReset);

void TestDurationToMillis(T* t) {
	t->Parallel();
	if (durationToMillis(gostd::Duration(1500000)) != 1.5)
		t->Error({"expected 1.5"});
	if (durationToMillis(gostd::Duration(0)) != 0.0)
		t->Error({"expected 0.0"});
	if (durationToMillis(-5 * gostd::second()) != 0.0)
		t->Error({"expected 0.0"});
	// Sub-microsecond durations retain precision rather than truncating to
	// 0.
	if (durationToMillis(gostd::Duration(500)) != 0.0005)
		t->Error({"expected 0.0005"});
	if (durationToMillis(gostd::Duration(1234)) != 0.001234)
		t->Error({"expected 0.001234"});
}
REGISTER_UNIT_TEST("ipc.TestDurationToMillis", TestDurationToMillis);

} // namespace
} // namespace tsc::ipc
