// tests_tracing.cpp — port of tsc/internal/tracing/tracing_test.go.
#include <any>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tracing/tracing.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace vfstest = tsc::vfs::vfstest;
namespace fstest = tsc::vfs::vfstest::fstest;
using namespace tsc;

namespace {

struct traceEvent {
	int64_t tid = 0;
	std::string ph;
	std::string cat;
	std::string name;
	std::unordered_map<std::string, json::Dom> args;
};

std::vector<traceEvent> decodeEvents(const json::Dom& dom) {
	std::vector<traceEvent> events;
	for (const auto& ev : dom.arr) {
		traceEvent e;
		if (auto* v = json::objGet(ev, "tid"))
			e.tid = json::asInt(*v, "int").first;
		if (auto* v = json::objGet(ev, "ph"))
			e.ph = v->strVal;
		if (auto* v = json::objGet(ev, "cat"))
			e.cat = v->strVal;
		if (auto* v = json::objGet(ev, "name"))
			e.name = v->strVal;
		if (auto* v = json::objGet(ev, "args"))
			for (const auto& [k, d] : v->obj) e.args[k] = d;
		events.push_back(std::move(e));
	}
	return events;
}

// argIs matches Go's `event.Args[argName] == argValue` for the argument
// types this suite uses: strings and float64 JSON numbers.
bool argIs(const json::Dom& d, const std::any& expected) {
	if (const auto* s = std::any_cast<std::string>(&expected))
		return d.kind == json::Dom::K::String && d.strVal == *s;
	if (const auto* c = std::any_cast<const char*>(&expected))
		return d.kind == json::Dom::K::String && d.strVal == *c;
	if (const auto* n = std::any_cast<double>(&expected))
		return d.kind == json::Dom::K::Number &&
		       json::asNumber(d, "float64").first == *n;
	return false;
}

const traceEvent& findEvent(T* t, const std::vector<traceEvent>& events,
                            const std::string& phase, const std::string& name,
                            const std::string& argName,
                            const std::any& argValue) {
	t->Helper();
	for (const auto& event : events) {
		auto it = event.args.find(argName);
		if (event.ph == phase && event.name == name && it != event.args.end() &&
		    argIs(it->second, argValue)) {
			return event;
		}
	}
	t->Fatalf("failed to find %s event %q", {phase, name});
	std::abort();
}

void assertThreadName(T* t, const std::vector<traceEvent>& events, int64_t tid,
                      const std::string& name) {
	t->Helper();
	for (const auto& event : events) {
		auto it = event.args.find("name");
		if (event.ph == "M" && event.name == "thread_name" &&
		    event.tid == tid && it != event.args.end() &&
		    it->second.strVal == name) {
			return;
		}
	}
	t->Fatalf(
	    "failed to find thread_name metadata for thread %d named %q",
	    {tid, name});
}

void assertDurationEventsAreWellNestedByThread(
    T* t, const std::vector<traceEvent>& events) {
	t->Helper();

	std::unordered_map<int64_t, std::vector<const traceEvent*>> stacks;
	for (const auto& event : events) {
		if (event.ph == "B") {
			stacks[event.tid].push_back(&event);
		} else if (event.ph == "E") {
			auto& stack = stacks[event.tid];
			assert::Assert(t, !stack.empty(),
			               "unmatched end event on thread " +
			                   std::to_string(event.tid));
			const auto* begin = stack.back();
			assert::Equal(t, begin->cat, event.cat);
			assert::Equal(t, begin->name, event.name);
			stack.pop_back();
		}
	}

	for (const auto& [tid, stack] : stacks) {
		assert::Assert(t, stack.empty(),
		               "thread " + std::to_string(tid) + " has " +
		                   std::to_string(stack.size()) +
		                   " unterminated events");
	}
}

void TestConcurrentDurationEventsUseSeparateThreadIDs(T* t) {
	t->Parallel();

	auto fsys = vfstest::FromMap(
	    {{"/trace",
	      std::shared_ptr<fstest::MapFile>(
	          new fstest::MapFile{"", vfs::ModeDir | vfs::FileMode{0777}})}},
	    true);

	auto [tr, err] = tracing::StartTracing(fsys.get(), "/trace", "",
	                                       true /*deterministic*/);
	assert::NilError(t, err);

	auto endA = tr->Push(tracing::PhaseParse, "createSourceFile",
	                     {{"path", std::string("/a.ts")}}, true);
	auto endB = tr->Push(tracing::PhaseParse, "createSourceFile",
	                     {{"path", std::string("/b.ts")}}, true);
	endA();
	endB();

	auto endCheck = tr->Push(tracing::PhaseCheck, "checkSourceFile",
	                         {{"checkerId", 0}, {"path", std::string("/a.ts")}},
	                         true);
	auto endVariance =
	    tr->Push(tracing::PhaseCheckTypes, "getVariancesWorker",
	             {{"checkerId", 0}, {"id", 1}}, true);
	endVariance();
	endCheck();

	assert::NilError(t, tracing::StopTracing(tr));

	auto [traceText, ok] = fsys->ReadFile("/trace/trace.json");
	assert::Assert(t, ok);

	auto [dom, perr] = json::parse(traceText);
	assert::NilError(t, perr);
	auto events = decodeEvents(dom);

	auto& aBegin = findEvent(t, events, "B", "createSourceFile", "path",
	                         "/a.ts");
	auto& aEnd =
	    findEvent(t, events, "E", "createSourceFile", "path", "/a.ts");
	auto& bBegin = findEvent(t, events, "B", "createSourceFile", "path",
	                         "/b.ts");
	auto& bEnd =
	    findEvent(t, events, "E", "createSourceFile", "path", "/b.ts");
	assert::Equal(t, aBegin.tid, aEnd.tid);
	assert::Equal(t, bBegin.tid, bEnd.tid);
	assert::Assert(t, aBegin.tid != bBegin.tid);
	assertThreadName(t, events, aBegin.tid, "file:/a.ts");
	assertThreadName(t, events, bBegin.tid, "file:/b.ts");

	auto& checkBegin =
	    findEvent(t, events, "B", "checkSourceFile", "path", "/a.ts");
	auto& varianceBegin = findEvent(t, events, "B", "getVariancesWorker",
	                                "id", double(1));
	assert::Equal(t, checkBegin.tid, varianceBegin.tid);
	assertThreadName(t, events, checkBegin.tid, "checker:0");

	assertDurationEventsAreWellNestedByThread(t, events);
}

std::unordered_map<std::string, int64_t>
traceThreadIDsForPaths(T* t, const std::vector<std::string>& paths) {
	t->Helper();

	auto fsys = vfstest::FromMap(
	    {{"/trace",
	      std::shared_ptr<fstest::MapFile>(
	          new fstest::MapFile{"", vfs::ModeDir | vfs::FileMode{0777}})}},
	    true);

	auto [tr, err] = tracing::StartTracing(fsys.get(), "/trace", "",
	                                       true /*deterministic*/);
	assert::NilError(t, err);

	for (const auto& path : paths) {
		auto end = tr->Push(tracing::PhaseParse, "createSourceFile",
		                    {{"path", std::string(path)}}, true);
		end();
	}

	assert::NilError(t, tracing::StopTracing(tr));

	auto [traceText, ok] = fsys->ReadFile("/trace/trace.json");
	assert::Assert(t, ok);

	auto [dom, perr] = json::parse(traceText);
	assert::NilError(t, perr);
	auto events = decodeEvents(dom);

	std::unordered_map<std::string, int64_t> threadIDs;
	for (const auto& path : paths) {
		threadIDs[path] = findEvent(t, events, "B", "createSourceFile",
		                            "path", path)
		                      .tid;
	}
	return threadIDs;
}

void TestThreadIDsAreStableAcrossFirstSeenOrder(T* t) {
	t->Parallel();

	auto first = traceThreadIDsForPaths(t, {"/a.ts", "/b.ts"});
	auto second = traceThreadIDsForPaths(t, {"/b.ts", "/a.ts"});

	assert::Assert(t, first == second);
}

} // namespace

REGISTER_UNIT_TEST("tracing.TestConcurrentDurationEventsUseSeparateThreadIDs",
                   TestConcurrentDurationEventsUseSeparateThreadIDs);
REGISTER_UNIT_TEST("tracing.TestThreadIDsAreStableAcrossFirstSeenOrder",
                   TestThreadIDsAreStableAcrossFirstSeenOrder);
