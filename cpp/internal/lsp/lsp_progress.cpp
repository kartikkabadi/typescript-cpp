// progress.go — port of tsc/internal/lsp/progress.go.
#include "internal/lsp/lsp.h"

#include <thread>

namespace tsc::lsp {

// serverProgressReporter adapts *Server to the progressReporter interface.

// done — progress.go:38.
gostd::Context serverProgressReporter::done() {
	if (auto s = server.lock()) {
		return s->backgroundCtx;
	}
	// Server already torn down: behave as shut down (permanently
	// cancelled context) so the run loop exits instead of hanging.
	auto [c, cancel] = gostd::contextWithCancel(gostd::contextBackground());
	cancel();
	return c;
}

// localize — progress.go:42.
std::string serverProgressReporter::localize(
	const DiagnosticMessage* msg,
	const std::vector<gostd::fmtArg>& args) {
	auto s = server.lock();
	if (s == nullptr) {
		return {};
	}
	// msg.Localize(locale, args...) = Localize(locale, m, "", StringifyArgs)
	return tsc::localize(s->locale, msg, "",
	                     detail::stringifyArgs(args));
}

// createWorkDoneProgress — progress.go:46.
void serverProgressReporter::createWorkDoneProgress(const std::string& token) {
	auto s = server.lock();
	if (s == nullptr) {
		return;
	}
	auto params = std::make_shared<lsproto::WorkDoneProgressCreateParams>();
	params->Token.String = detail::goNew(token);
	(void)s->sendClientRequestFireAndForget(
		lsproto::WindowWorkDoneProgressCreateInfo, params);
}

// sendProgress — progress.go:52.
void serverProgressReporter::sendProgress(
	const std::string& token,
	lsproto::WorkDoneProgressBeginOrReportOrEnd value) {
	auto s = server.lock();
	if (s == nullptr) {
		return;
	}
	auto params = std::make_shared<lsproto::ProgressParams>();
	params->Token.String = detail::goNew(token);
	params->Value = std::move(value);
	(void)s->sendNotification(lsproto::ProgressInfo, params);
}

// newProjectLoadingProgress — progress.go:76.
std::shared_ptr<projectLoadingProgress>
newProjectLoadingProgress(Server* server, gostd::Duration delay) {
	return newProjectLoadingProgressFromReporter(
		std::make_shared<serverProgressReporter>(
			server->shared_from_this()),
		delay);
}

// newProjectLoadingProgressFromReporter — progress.go:80.
std::shared_ptr<projectLoadingProgress> newProjectLoadingProgressFromReporter(
	std::shared_ptr<progressReporter> reporter, gostd::Duration delay) {
	auto p = std::make_shared<projectLoadingProgress>(std::move(reporter),
	                                                delay);
	p->startRun(); // go p.run()
	return p;
}

projectLoadingProgress::projectLoadingProgress(
	std::shared_ptr<progressReporter> reporter, gostd::Duration delay)
    : reporter(std::move(reporter)), delay(delay) {}

void projectLoadingProgress::startRun() {
	// Hold self for the goroutine's lifetime — Go's GC does the same.
	std::thread([self = shared_from_this()] { self->run(); }).detach();
}

// enqueue — the `p.ch <- ev` select arm shared by start/finish
// (progress.go:91,100). `ch` has capacity 64; the send blocks while full and
// drops the event when the server is shutting down.
void projectLoadingProgress::enqueue(const progressEvent& ev) {
	auto st = this->st;
	auto done = reporter->done();
	std::unique_lock<std::mutex> lk(st->mu);
	auto disarm = gostd::contextAfterFunc(done, [st] {
		std::lock_guard<std::mutex> g(st->mu);
		st->notFull.notify_all();
	});
	st->notFull.wait(lk, [&] {
		return st->queue.size() < 64 || gostd::ctxErr(done) != nullptr;
	});
	disarm();
	if (st->queue.size() < 64) { // `case p.ch <- ev:` won
		st->queue.push_back(ev);
		lk.unlock();
		st->cv.notify_all();
	}
	// `<-p.reporter.done()`: server shutting down; drop the event.
}

// start — progress.go:90.
void projectLoadingProgress::start(const DiagnosticMessage* message,
                                   std::vector<gostd::fmtArg> args) {
	enqueue(progressEvent{message, std::move(args), false});
}

// finish — progress.go:99.
void projectLoadingProgress::finish(const DiagnosticMessage* message,
                                    std::vector<gostd::fmtArg> args) {
	enqueue(progressEvent{message, std::move(args), true});
}

// run — progress.go:110. The persistent goroutine that processes all
// progress events. It owns all mutable state: no external synchronization
// needed.
void projectLoadingProgress::run() {
	collections::OrderedMap<std::string, int> loading;
	std::string token; // current token; empty if no progress active
	int tokenID = 0;
	bool begun = false; // whether "begin" has been sent for the current token

	auto st = this->st;
	auto done = reporter->done();

	// delay — Go `*time.Timer`. delayArmed doubles as `delay != nil`; the
	// pending-fire flag retracts the channel value like Go abandoning delay.C.
	auto armDelay = [&](gostd::Duration d) {
		delayArmed = std::make_shared<std::atomic<bool>>(true);
		auto armed = delayArmed;
		std::thread([armed, st, d] {
			std::this_thread::sleep_for(d);
			std::lock_guard<std::mutex> lk(st->mu);
			if (armed->load()) {
				st->delayFiredPending = true;
				st->cv.notify_all();
			}
		}).detach();
	};
	auto stopDelay = [&] {
		if (delayArmed != nullptr) {
			std::lock_guard<std::mutex> lk(st->mu);
			delayArmed->store(false);
			st->delayFiredPending = false;
			delayArmed = nullptr;
		}
	};
	bool delayFired = false; // true after the delay timer fires

	for (;;) {
		progressEvent ev;
		bool gotEvent = false;
		bool firedDelay = false;
		bool gotDone = false;
		{
			std::unique_lock<std::mutex> lk(st->mu);
			auto disarm = gostd::contextAfterFunc(done, [st] {
				std::lock_guard<std::mutex> g(st->mu);
				st->cv.notify_all();
			});
			st->cv.wait(lk, [&] {
				return !st->queue.empty() || st->delayFiredPending ||
				       gostd::ctxErr(done) != nullptr;
			});
			disarm();
			// Go select picks randomly among ready cases; order here is
			// observably equivalent.
			if (!st->queue.empty()) {
				ev = std::move(st->queue.front());
				st->queue.pop_front();
				gotEvent = true;
				st->inFlight++;
				st->notFull.notify_all();
			} else if (st->delayFiredPending) {
				st->delayFiredPending = false;
				firedDelay = true;
				st->inFlight++;
			} else {
				gotDone = true;
			}
		}
		auto finishWork = [&] {
			if (gotEvent || firedDelay) {
				std::lock_guard<std::mutex> lk(st->mu);
				st->inFlight--;
				st->cv.notify_all();
			}
		};

		if (gotEvent) {
			auto text = reporter->localize(ev.message, ev.args);
			if (!ev.finish) {
				auto count = loading.GetOrZero(text);
				loading.Set(text, count + 1);
				if (token.empty()) {
					tokenID++;
					token = gostd::sprintf("tsgo-loading-%d",
					                       {gostd::fmtArg(tokenID)});
					begun = false;
					if (delay <= gostd::Duration::zero()) {
						delayFired = true;
						reporter->createWorkDoneProgress(token);
					} else {
						delayFired = false;
						armDelay(delay); // time.NewTimer(p.delay)
					}
				}
				if (delayFired) {
					begun = beginOrReport(token, text, begun);
				}
			} else {
				auto count = loading.GetOrZero(text);
				if (count <= 1) {
					loading.Delete(text);
				} else {
					loading.Set(text, count - 1);
				}
				if (token.empty()) {
					finishWork();
					continue;
				}
				if (loading.Size() == 0) {
					if (begun) {
						lsproto::WorkDoneProgressBeginOrReportOrEnd v;
						v.End = std::make_shared<lsproto::WorkDoneProgressEnd>();
						reporter->sendProgress(token, std::move(v));
					}
					stopDelay();
					token.clear();
				} else if (delayFired) {
					const auto& keys = loading.Keys();
					auto first = keys.empty() ? std::string() : keys[0];
					lsproto::WorkDoneProgressBeginOrReportOrEnd v;
					v.Report =
						std::make_shared<lsproto::WorkDoneProgressReport>();
					v.Report->Message = first;
					reporter->sendProgress(token, std::move(v));
				}
			}
		} else if (firedDelay) {
			delayFired = true;
			if (!token.empty() && loading.Size() > 0) {
				reporter->createWorkDoneProgress(token);
				const auto& keys = loading.Keys();
				auto first = keys.empty() ? std::string() : keys[0];
				begun = beginOrReport(token, first, begun);
			}
		} else if (gotDone) {
			stopDelay();
			{
				std::lock_guard<std::mutex> lk(st->mu);
				st->runExited = true;
				st->cv.notify_all();
			}
			return;
		}
		finishWork();
	}
}

// waitIdleForTest — synctest.Wait(): blocks until the run goroutine has
// handled every queued event and pending delay-fire.
void projectLoadingProgress::waitIdleForTest() {
	auto st = this->st;
	std::unique_lock<std::mutex> lk(st->mu);
	st->cv.wait(lk, [&] {
		return st->queue.empty() && !st->delayFiredPending &&
		       st->inFlight == 0;
	});
}

// fillChannelForTest — `p.ch <- ev` × cap(p.ch) (64), bypassing the
// done() select so start/finish then take the done() path.
void projectLoadingProgress::fillChannelForTest() {
	auto st = this->st;
	std::lock_guard<std::mutex> lk(st->mu);
	while (st->queue.size() < 64) {
		st->queue.push_back(progressEvent{tsc::Project_0,
		                                {gostd::fmtArg(std::string("fill"))},
		                                false});
	}
	st->cv.notify_all();
}

// waitRunExitForTest — blocks until the run goroutine has returned.
void projectLoadingProgress::waitRunExitForTest() {
	auto st = this->st;
	std::unique_lock<std::mutex> lk(st->mu);
	st->cv.wait(lk, [&] { return st->runExited; });
}

// beginOrReport — progress.go:200. Sends WorkDoneProgressBegin if not yet
// begun, otherwise sends WorkDoneProgressReport. Returns true to indicate
// begun state.
bool projectLoadingProgress::beginOrReport(const std::string& token,
                                           const std::string& text,
                                           bool begun) {
	if (!begun) {
		auto title = reporter->localize(Loading, {});
		lsproto::WorkDoneProgressBeginOrReportOrEnd v;
		v.Begin = std::make_shared<lsproto::WorkDoneProgressBegin>();
		v.Begin->Title = title;
		v.Begin->Message = text;
		reporter->sendProgress(token, std::move(v));
	} else {
		lsproto::WorkDoneProgressBeginOrReportOrEnd v;
		v.Report = std::make_shared<lsproto::WorkDoneProgressReport>();
		v.Report->Message = text;
		reporter->sendProgress(token, std::move(v));
	}
	return true;
}

} // namespace tsc::lsp
