// Port of tsc/internal/lsp/progress_test.go (internal package test).
//
// The Go test runs inside testing/synctest bubbles: synctest.Sleep advances
// a fake clock and synctest.Wait blocks until every goroutine in the bubble
// is durably blocked. The C++ port has no fake clock — this port keeps the
// same event sequences with a real clock:
//   synctest.Wait()    -> p->waitIdleForTest()
//   synctest.Sleep(d)  -> sleep_for(d + margin); p->waitIdleForTest()
// The margin only covers the timer thread's wake jitter; the observable
// ordering assertions are identical.
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "internal/diagnostics/diagnostics.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/locale/locale.h"
#include "internal/lsp/lsp.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

namespace gostd = tsc::gostd;
namespace lsproto = tsc::lsp::lsproto;
using tsc::gostd::testing::T;
using tsc::lsp::progressReporter;
using tsc::lsp::projectLoadingProgress;
using tsc::lsp::newProjectLoadingProgressFromReporter;

struct progressCall {
	std::string method; // "create", "begin", "report", "end"
	std::string token;
	std::string title; // begin only
	std::string msg;   // begin/report only
};

struct fakeProgressReporter : progressReporter {
	std::mutex mu;
	std::vector<progressCall> calls;
	gostd::Context ctx;

	gostd::Context done() override { return ctx; }

	std::string localize(const tsc::DiagnosticMessage* msg,
	                     const std::vector<gostd::fmtArg>& args) override {
		// msg.Localize(locale.Default, args...)
		return tsc::localize(tsc::locale::Default, msg, "",
		                     tsc::lsp::detail::stringifyArgs(args));
	}

	void createWorkDoneProgress(const std::string& token) override {
		std::lock_guard<std::mutex> lk(mu);
		calls.push_back(progressCall{"create", token, "", ""});
	}

	void sendProgress(
	    const std::string& token,
	    lsproto::WorkDoneProgressBeginOrReportOrEnd value) override {
		std::lock_guard<std::mutex> lk(mu);
		if (value.Begin != nullptr) {
			std::string msg;
			if (value.Begin->Message.has_value()) {
				msg = *value.Begin->Message;
			}
			calls.push_back(
			    progressCall{"begin", token, value.Begin->Title, msg});
		} else if (value.Report != nullptr) {
			std::string msg;
			if (value.Report->Message.has_value()) {
				msg = *value.Report->Message;
			}
			calls.push_back(progressCall{"report", token, "", msg});
		} else if (value.End != nullptr) {
			calls.push_back(progressCall{"end", token, "", ""});
		}
	}

	std::vector<progressCall> getCalls() {
		std::lock_guard<std::mutex> lk(mu);
		return calls;
	}
};

// synctest.Sleep — advance the (real) clock past the delay timer, then wait
// for the run goroutine to drain the fired event.
static void synctestSleep(
    const std::shared_ptr<projectLoadingProgress>& p, gostd::Duration d) {
	std::this_thread::sleep_for(d + std::chrono::milliseconds(75));
	p->waitIdleForTest();
}

static std::string callsString(const std::vector<progressCall>& calls) {
	std::string out = "[";
	for (size_t i = 0; i < calls.size(); i++) {
		if (i) out += " ";
		out += "{" + calls[i].method + " " + calls[i].token + " " +
		       calls[i].title + " " + calls[i].msg + "}";
	}
	return out + "]";
}

void TestProgress(T* t) {
	t->Parallel();

	t->Run("StartFinishBeforeDelay", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
		struct scopeCancel {
			gostd::CancelFunc f;
			~scopeCancel() { f(); }
		} deferCancel{cancel};
		auto reporter = std::make_shared<fakeProgressReporter>();
		reporter->ctx = ctx;
		auto p = newProjectLoadingProgressFromReporter(
		    reporter, std::chrono::milliseconds(500));

		p->start(tsc::Project_0, {gostd::fmtArg("myProject")});
		p->waitIdleForTest();

		// Finish before the delay fires — no UI should appear.
		p->finish(tsc::Project_0, {gostd::fmtArg("myProject")});
		p->waitIdleForTest();

		// Advance time past the delay to ensure no progress is sent.
		synctestSleep(p, std::chrono::milliseconds(600));

		auto calls = reporter->getCalls();
		if (!calls.empty()) {
			t->Fatalf(
			    "expected no progress calls for fast operation, got %v",
			    {callsString(calls)});
			return;
		}

		cancel();
	});

	t->Run("ShowsAfterDelay", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
		struct scopeCancel {
			gostd::CancelFunc f;
			~scopeCancel() { f(); }
		} deferCancel{cancel};
		auto reporter = std::make_shared<fakeProgressReporter>();
		reporter->ctx = ctx;
		auto p = newProjectLoadingProgressFromReporter(
		    reporter, std::chrono::milliseconds(500));

		p->start(tsc::Project_0, {gostd::fmtArg("myProject")});
		p->waitIdleForTest();

		// Let the delay fire.
		synctestSleep(p, std::chrono::milliseconds(500));

		auto calls = reporter->getCalls();
		if (calls.size() != 2) {
			t->Fatalf("expected 2 calls (create + begin), got %v: %v",
			          {static_cast<int>(calls.size()), callsString(calls)});
			return;
		}
		if (calls[0].method != "create") {
			t->Fatalf("expected create, got %v", {callsString(calls)});
			return;
		}
		if (calls[1].method != "begin") {
			t->Fatalf("expected begin, got %v", {callsString(calls)});
			return;
		}
		if (calls[1].title != tsc::Loading->text) {
			t->Fatalf("expected title %q, got %q",
			          {tsc::Loading->text, calls[1].title});
			return;
		}

		// Finish the operation.
		p->finish(tsc::Project_0, {gostd::fmtArg("myProject")});
		p->waitIdleForTest();

		calls = reporter->getCalls();
		auto last = calls.back();
		if (last.method != "end") {
			t->Fatalf("expected end, got %v", {last.method});
			return;
		}

		cancel();
	});

	t->Run("ReportsMultipleOperations", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
		struct scopeCancel {
			gostd::CancelFunc f;
			~scopeCancel() { f(); }
		} deferCancel{cancel};
		auto reporter = std::make_shared<fakeProgressReporter>();
		reporter->ctx = ctx;
		auto p = newProjectLoadingProgressFromReporter(
		    reporter, std::chrono::milliseconds(100));

		// Start two different operations.
		p->start(tsc::Project_0, {gostd::fmtArg("projA")});
		p->start(tsc::Project_0, {gostd::fmtArg("projB")});
		p->waitIdleForTest();

		// Let the delay fire.
		synctestSleep(p, std::chrono::milliseconds(100));

		auto calls = reporter->getCalls();
		// Should have: create, begin (with first message).
		if (calls.size() < 2) {
			t->Fatalf("expected at least 2 calls, got %v: %v",
			          {static_cast<int>(calls.size()), callsString(calls)});
			return;
		}
		if (calls[0].method != "create") {
			t->Fatalf("expected create, got %v", {callsString(calls)});
			return;
		}
		if (calls[1].method != "begin") {
			t->Fatalf("expected begin, got %v", {callsString(calls)});
			return;
		}

		// Finish one — should send a report with the remaining operation.
		p->finish(tsc::Project_0, {gostd::fmtArg("projA")});
		p->waitIdleForTest();

		calls = reporter->getCalls();
		bool found = false;
		for (auto& c : calls) {
			if (c.method == "report") {
				found = true;
				break;
			}
		}
		if (!found) {
			t->Fatalf("expected a report after partial finish, got %v",
			          {callsString(calls)});
			return;
		}

		// Finish the second — should send end.
		p->finish(tsc::Project_0, {gostd::fmtArg("projB")});
		p->waitIdleForTest();

		calls = reporter->getCalls();
		auto last = calls.back();
		if (last.method != "end") {
			t->Fatalf("expected end, got %v", {last.method});
			return;
		}

		cancel();
	});

	t->Run("RefCounting", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
		struct scopeCancel {
			gostd::CancelFunc f;
			~scopeCancel() { f(); }
		} deferCancel{cancel};
		auto reporter = std::make_shared<fakeProgressReporter>();
		reporter->ctx = ctx;
		auto p = newProjectLoadingProgressFromReporter(
		    reporter, std::chrono::milliseconds(100));

		// Start the same operation twice (ref count = 2).
		p->start(tsc::Project_0, {gostd::fmtArg("proj")});
		p->start(tsc::Project_0, {gostd::fmtArg("proj")});
		p->waitIdleForTest();

		synctestSleep(p, std::chrono::milliseconds(100));

		// Finish once (ref count = 1) — should NOT end.
		p->finish(tsc::Project_0, {gostd::fmtArg("proj")});
		p->waitIdleForTest();

		auto calls = reporter->getCalls();
		for (auto& c : calls) {
			if (c.method == "end") {
				t->Fatalf("unexpected end with ref count > 0: %v",
				          {callsString(calls)});
				return;
			}
		}

		// Finish again (ref count = 0) — should end.
		p->finish(tsc::Project_0, {gostd::fmtArg("proj")});
		p->waitIdleForTest();

		calls = reporter->getCalls();
		auto last = calls.back();
		if (last.method != "end") {
			t->Fatalf("expected end when ref count reaches 0, got %v",
			          {last.method});
			return;
		}

		cancel();
	});

	t->Run("NewTokenAfterEnd", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
		struct scopeCancel {
			gostd::CancelFunc f;
			~scopeCancel() { f(); }
		} deferCancel{cancel};
		auto reporter = std::make_shared<fakeProgressReporter>();
		reporter->ctx = ctx;
		auto p = newProjectLoadingProgressFromReporter(
		    reporter, std::chrono::milliseconds(100));

		// First cycle.
		p->start(tsc::Project_0, {gostd::fmtArg("proj")});
		p->waitIdleForTest();
		synctestSleep(p, std::chrono::milliseconds(100));

		auto calls = reporter->getCalls();
		auto firstToken = calls[0].token;

		p->finish(tsc::Project_0, {gostd::fmtArg("proj")});
		p->waitIdleForTest();

		// Second cycle — should get a new token.
		p->start(tsc::Project_0, {gostd::fmtArg("proj2")});
		p->waitIdleForTest();
		synctestSleep(p, std::chrono::milliseconds(100));

		calls = reporter->getCalls();
		std::string secondToken;
		for (auto& c : calls) {
			if (c.method == "create" && c.token != firstToken) {
				secondToken = c.token;
				break;
			}
		}
		if (secondToken.empty()) {
			t->Fatalf(
			    "expected a new token for second cycle, got calls: %v",
			    {callsString(calls)});
			return;
		}
		if (firstToken == secondToken) {
			t->Fatalf("expected different tokens, both were %q",
			          {firstToken});
			return;
		}

		p->finish(tsc::Project_0, {gostd::fmtArg("proj2")});
		p->waitIdleForTest();

		cancel();
	});

	t->Run("StartBeforeDelayThenMoreAfterDelay", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
		struct scopeCancel {
			gostd::CancelFunc f;
			~scopeCancel() { f(); }
		} deferCancel{cancel};
		auto reporter = std::make_shared<fakeProgressReporter>();
		reporter->ctx = ctx;
		auto p = newProjectLoadingProgressFromReporter(
		    reporter, std::chrono::milliseconds(200));

		// Start before delay.
		p->start(tsc::Project_0, {gostd::fmtArg("projA")});
		p->waitIdleForTest();

		// Let delay fire.
		synctestSleep(p, std::chrono::milliseconds(200));

		auto calls = reporter->getCalls();
		if (calls.size() < 2) {
			t->Fatalf("expected create + begin after delay, got %v",
			          {callsString(calls)});
			return;
		}

		// Start another operation after delay — should send a report
		// immediately.
		p->start(tsc::Project_0, {gostd::fmtArg("projB")});
		p->waitIdleForTest();

		calls = reporter->getCalls();
		auto last = calls.back();
		if (last.method != "report") {
			t->Fatalf("expected report for new start after delay, got %v",
			          {last.method});
			return;
		}

		// Clean up.
		p->finish(tsc::Project_0, {gostd::fmtArg("projA")});
		p->finish(tsc::Project_0, {gostd::fmtArg("projB")});
		p->waitIdleForTest();

		cancel();
	});

	t->Run("FinishWithNoActiveToken", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
		struct scopeCancel {
			gostd::CancelFunc f;
			~scopeCancel() { f(); }
		} deferCancel{cancel};
		auto reporter = std::make_shared<fakeProgressReporter>();
		reporter->ctx = ctx;
		auto p = newProjectLoadingProgressFromReporter(
		    reporter, std::chrono::milliseconds(100));

		// Finish without any prior start — should be a no-op.
		p->finish(tsc::Project_0, {gostd::fmtArg("proj")});
		p->waitIdleForTest();

		auto calls = reporter->getCalls();
		if (!calls.empty()) {
			t->Fatalf("expected no calls for orphan finish, got %v",
			          {callsString(calls)});
			return;
		}

		cancel();
	});

	t->Run("ShutdownDuringStartAndFinish", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
		auto reporter = std::make_shared<fakeProgressReporter>();
		reporter->ctx = ctx;
		auto p = newProjectLoadingProgressFromReporter(
		    reporter, std::chrono::milliseconds(100));

		// Cancel context so the run goroutine exits.
		cancel();
		p->waitRunExitForTest();

		// Fill the channel buffer so start/finish block on send.
		p->fillChannelForTest();

		// These should return immediately via the done() path
		// since the channel is full and the context is cancelled.
		p->start(tsc::Project_0, {gostd::fmtArg("proj")});
		p->finish(tsc::Project_0, {gostd::fmtArg("proj")});
	});

	t->Run("ShutdownWithActiveTimer", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
		auto reporter = std::make_shared<fakeProgressReporter>();
		reporter->ctx = ctx;
		auto p = newProjectLoadingProgressFromReporter(
		    reporter, std::chrono::milliseconds(500));

		// Start an operation so the delay timer is created.
		p->start(tsc::Project_0, {gostd::fmtArg("proj")});
		p->waitIdleForTest();

		// Shutdown while the delay timer is still pending.
		cancel();
		p->waitRunExitForTest();
	});

	t->Run("ZeroDelay", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
		struct scopeCancel {
			gostd::CancelFunc f;
			~scopeCancel() { f(); }
		} deferCancel{cancel};
		auto reporter = std::make_shared<fakeProgressReporter>();
		reporter->ctx = ctx;
		auto p = newProjectLoadingProgressFromReporter(
		    reporter, gostd::Duration::zero());

		// With zero delay, progress should begin immediately.
		p->start(tsc::Project_0, {gostd::fmtArg("proj")});
		p->waitIdleForTest();

		auto calls = reporter->getCalls();
		if (calls.size() != 2) {
			t->Fatalf("expected 2 calls (create + begin), got %v: %v",
			          {static_cast<int>(calls.size()), callsString(calls)});
			return;
		}
		if (calls[0].method != "create") {
			t->Fatalf("expected create, got %v", {callsString(calls)});
			return;
		}
		if (calls[1].method != "begin") {
			t->Fatalf("expected begin, got %v", {callsString(calls)});
			return;
		}
		if (calls[1].msg != "Project 'proj'") {
			t->Fatalf("expected message %q, got %q",
			          {"Project 'proj'", calls[1].msg});
			return;
		}

		// Start+finish should still produce begin and end.
		p->finish(tsc::Project_0, {gostd::fmtArg("proj")});
		p->waitIdleForTest();

		calls = reporter->getCalls();
		auto last = calls.back();
		if (last.method != "end") {
			t->Fatalf("expected end, got %v", {last.method});
			return;
		}

		cancel();
	});

	t->Run("FinishBeforeDelayNoBegun", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
		struct scopeCancel {
			gostd::CancelFunc f;
			~scopeCancel() { f(); }
		} deferCancel{cancel};
		auto reporter = std::make_shared<fakeProgressReporter>();
		reporter->ctx = ctx;
		auto p = newProjectLoadingProgressFromReporter(
		    reporter, std::chrono::milliseconds(500));

		// Start, then finish before delay — begun is false, so no end is
		// sent.
		p->start(tsc::Project_0, {gostd::fmtArg("proj")});
		p->waitIdleForTest();
		p->finish(tsc::Project_0, {gostd::fmtArg("proj")});
		p->waitIdleForTest();

		auto calls = reporter->getCalls();
		for (auto& c : calls) {
			if (c.method == "end") {
				t->Fatalf("unexpected end when begun=false: %v",
				          {callsString(calls)});
				return;
			}
		}

		cancel();
	});
}
REGISTER_UNIT_TEST("lsp.TestProgress", TestProgress);

}  // namespace
