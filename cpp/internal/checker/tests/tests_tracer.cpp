// tests_tracer.cpp — port of tsc/internal/checker/tracer_test.go
// (package-internal test: tracer_test.go is `package checker`).
#include <any>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/checker/checker.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tracing/tracing.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;

namespace {

struct testTraceEvent {
	std::string ph;
	std::string name;
	const json::Dom* args; // nullptr when absent
};

testTraceEvent findTestTraceEvent(T* t,
                                  const std::vector<testTraceEvent>& events,
                                  const std::string& phase,
                                  const std::string& name) {
	t->Helper();
	for (auto& event : events) {
		if (event.ph == phase && event.name == name) {
			return event;
		}
	}
	t->Fatalf("failed to find %s event %q", {phase, name});
	return {};
}

void TestTracerPushPreservesEndArgMutations(T* t) {
	t->Parallel();

	auto mapFile = std::make_shared<vfs::vfstest::fstest::MapFile>();
	mapFile->Mode = vfs::ModeDir;
	auto fsys = vfs::vfstest::FromMap({{"/trace", mapFile}}, true);

	auto [tr, err] = tracing::StartTracing(fsys.get(), "/trace", "",
	                                       true /*deterministic*/);
	assert::NilError(t, err);

	auto args = std::make_shared<tracing::TraceArgs>();
	(*args)["id"] = 1;
	checker::Tracer tracer;
	tracer.tracing = tr;
	tracer.recorder = tr->NewTypeTracer(7);
	tracer.checkerIndex = 7;
	auto pop = tracer.Push(tracing::PhaseCheckTypes,
	                       "getVariancesWorker", args, true);
	assert::Assert(t, args->count("checkerId") == 0);

	(*args)["variances"] = std::vector<std::string>{"out"};
	pop();
	assert::Assert(t, args->count("checkerId") == 0);

	assert::NilError(t, tracing::StopTracing(tr));

	auto [traceText, ok] = fsys->ReadFile("/trace/trace.json");
	assert::Assert(t, ok);

	auto [dom, jerr] = json::parse(traceText);
	assert::NilError(t, jerr);
	std::vector<testTraceEvent> events;
	for (auto& e : dom.arr) {
		testTraceEvent ev;
		if (auto* ph = json::objGet(e, "ph")) {
			auto [s, serr] = json::asString(*ph, "string");
			assert::NilError(t, serr);
			ev.ph = s;
		}
		if (auto* name = json::objGet(e, "name")) {
			auto [s, serr] = json::asString(*name, "string");
			assert::NilError(t, serr);
			ev.name = s;
		}
		ev.args = json::objGet(e, "args");
		events.push_back(ev);
	}

	auto beginEvent =
	    findTestTraceEvent(t, events, "B", "getVariancesWorker");
	assert::Assert(t, beginEvent.args != nullptr);
	{
		auto* checkerId = json::objGet(*beginEvent.args, "checkerId");
		assert::Assert(t, checkerId != nullptr);
		auto [n, nerr] = json::asNumber(*checkerId, "float64");
		assert::NilError(t, nerr);
		assert::Equal(t, n, 7.0);
		assert::Assert(t, json::objGet(*beginEvent.args, "variances") ==
		                 nullptr);
	}

	auto endEvent =
	    findTestTraceEvent(t, events, "E", "getVariancesWorker");
	assert::Assert(t, endEvent.args != nullptr);
	{
		auto* checkerId = json::objGet(*endEvent.args, "checkerId");
		assert::Assert(t, checkerId != nullptr);
		auto [n, nerr] = json::asNumber(*checkerId, "float64");
		assert::NilError(t, nerr);
		assert::Equal(t, n, 7.0);
		auto* variances = json::objGet(*endEvent.args, "variances");
		assert::Assert(t, variances != nullptr);
		assert::Assert(t, variances->arr.size() == 1);
		auto [s, serr] = json::asString(variances->arr[0], "string");
		assert::NilError(t, serr);
		assert::Equal(t, s, std::string("out"));
	}
}

} // namespace

REGISTER_UNIT_TEST("checker.TestTracerPushPreservesEndArgMutations",
                   TestTracerPushPreservesEndArgMutations);
