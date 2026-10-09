// Shared helpers for the fswatch package tests — ports testutil_test.go
// and the helper section of watcher_test.go.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "internal/fswatch/fswatch.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::fswatch {

namespace fs = std::filesystem;
using gostd::Error;
using gostd::testing::T;

// ----- os / filepath / time shims (Go stdlib surface used by tests) -------

inline std::string filepathJoin(std::string_view a, std::string_view b) {
	if (a.empty())
		return std::string(b);
	if (b.empty())
		return std::string(a);
	if (a.back() == '/')
		return std::string(a) + std::string(b);
	return std::string(a) + "/" + std::string(b);
}
inline std::string filepathJoin(std::string_view a, std::string_view b,
                                std::string_view c) {
	return filepathJoin(filepathJoin(a, b), c);
}
inline std::string filepathJoin(std::string_view a, std::string_view b,
                                std::string_view c, std::string_view d) {
	return filepathJoin(filepathJoin(filepathJoin(a, b), c), d);
}
inline std::string filepathJoin(std::string_view a, std::string_view b,
                                std::string_view c, std::string_view d,
                                std::string_view e) {
	return filepathJoin(filepathJoin(filepathJoin(a, b), c, d), e);
}
inline std::string filepathJoin(std::string_view a, std::string_view b,
                                std::string_view c, std::string_view d,
                                std::string_view e, std::string_view f) {
	return filepathJoin(filepathJoin(filepathJoin(a, b), c, d, e), f);
}

inline Error osErrno(const char* op) {
	return gostd::errorf("%s: %w", {op, errnoError(errno)});
}

inline Error osWriteFile(const std::string& path, std::string_view data,
                         int perm) {
	int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, perm);
	if (fd < 0)
		return osErrno("open");
	std::string_view rest = data;
	while (!rest.empty()) {
		ssize_t n = ::write(fd, rest.data(), rest.size());
		if (n < 0) {
			if (errno == EINTR)
				continue;
			[[maybe_unused]] int e = errno;
			(void)::close(fd);
			return osErrno("write");
		}
		rest.remove_prefix(n);
	}
	if (::close(fd) != 0)
		return osErrno("close");
	return nullptr;
}

inline Error osReadFile(const std::string& path, std::string* out) {
	int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return osErrno("open");
	out->clear();
	char buf[8192];
	for (;;) {
		ssize_t n = ::read(fd, buf, sizeof(buf));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			int e = errno;
			(void)::close(fd);
			errno = e;
			return osErrno("read");
		}
		if (n == 0)
			break;
		out->append(buf, n);
	}
	(void)::close(fd);
	return nullptr;
}

inline Error osMkdir(const std::string& path, int perm) {
	if (::mkdir(path.c_str(), perm) != 0)
		return osErrno("mkdir");
	return nullptr;
}

inline Error osMkdirAll(const std::string& path, int perm) {
	std::error_code ec;
	if (!std::filesystem::create_directories(path, ec))
		return gostd::errorf("mkdirall: %s",
		                     {gostd::Error(gostd::newError(ec.message()))});
	return nullptr;
}

inline Error osRemove(const std::string& path) {
	if (::remove(path.c_str()) != 0)
		return osErrno("remove");
	return nullptr;
}

inline Error osRemoveAll(const std::string& path) {
	std::error_code ec;
	std::filesystem::remove_all(path, ec);
	if (ec)
		return gostd::errorf("removeall: %s",
		                     {gostd::Error(gostd::newError(ec.message()))});
	return nullptr;
}

inline Error osRename(const std::string& from, const std::string& to) {
	if (::rename(from.c_str(), to.c_str()) != 0)
		return osErrno("rename");
	return nullptr;
}

inline Error osSymlink(const std::string& target, const std::string& link) {
	if (::symlink(target.c_str(), link.c_str()) != 0)
		return osErrno("symlink");
	return nullptr;
}

inline Error osChmod(const std::string& path, int mode) {
	if (::chmod(path.c_str(), mode) != 0)
		return osErrno("chmod");
	return nullptr;
}

inline Error osTruncate(const std::string& path, off_t size) {
	if (::truncate(path.c_str(), size) != 0)
		return osErrno("truncate");
	return nullptr;
}

// filepath.EvalSymlinks — resolves all symlinks; error if path missing.
inline std::pair<std::string, Error> evalSymlinks(const std::string& path) {
	char buf[PATH_MAX];
	if (::realpath(path.c_str(), buf) == nullptr)
		return {"", osErrno("realpath")};
	return {std::string(buf), nullptr};
}

inline void sleepFor(gostd::Duration d) {
	std::this_thread::sleep_for(std::chrono::nanoseconds(d));
}
inline gostd::Duration ms(int64_t n) {
	return std::chrono::milliseconds(n);
}
inline gostd::Duration sec(int64_t n) { return std::chrono::seconds(n); }
inline gostd::Duration ns(int64_t n) {
	return std::chrono::nanoseconds(n);
}

// ----- testingT — testutil_test.go ----------------------------------------
// Abstract interface matching Go's testingT so helper bodies work with both
// the real testing::T (via realT) and the retry wrapper (retryT).

struct TestI {
	virtual ~TestI() = default;
	virtual void Helper() = 0;
	virtual void Cleanup(std::function<void()> fn) = 0;
	virtual std::string TempDir() = 0;
	virtual std::string Name() = 0;

	virtual void Log(std::initializer_list<gostd::fmtArg> args) = 0;
	virtual void Logf(std::string_view format,
	                  std::initializer_list<gostd::fmtArg> args) = 0;
	virtual void Error(std::initializer_list<gostd::fmtArg> args) = 0;
	virtual void Errorf(std::string_view format,
	                    std::initializer_list<gostd::fmtArg> args) = 0;
	[[noreturn]] virtual void Fatal(std::initializer_list<gostd::fmtArg> args) = 0;
	[[noreturn]] virtual void Fatalf(
	    std::string_view format, std::initializer_list<gostd::fmtArg> args) = 0;
	[[noreturn]] virtual void Skip(std::initializer_list<gostd::fmtArg> args) = 0;
	[[noreturn]] virtual void Skipf(
	    std::string_view format, std::initializer_list<gostd::fmtArg> args) = 0;
	[[noreturn]] virtual void SkipNow() = 0;
	virtual bool Failed() = 0;
};

// realT adapts *testing.T to TestI (Go: *testing.T satisfies testingT).
struct realT : TestI {
	T* t;
	explicit realT(T* t_) : t(t_) {}
	void Helper() override { t->Helper(); }
	void Cleanup(std::function<void()> fn) override { t->Cleanup(std::move(fn)); }
	std::string TempDir() override { return t->TempDir(); }
	std::string Name() override { return t->Name(); }
	void Log(std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		t->Log(args);
	}
	void Logf(std::string_view format,
	          std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		t->Logf(format, args);
	}
	void Error(std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		t->Error(args);
	}
	void Errorf(std::string_view format,
	            std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		t->Errorf(format, args);
	}
	[[noreturn]] void Fatal(std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		t->Fatal(args);
	}
	[[noreturn]] void Fatalf(std::string_view format,
	                         std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		t->Fatalf(format, args);
	}
	[[noreturn]] void Skip(std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		t->Skip(args);
	}
	[[noreturn]] void Skipf(std::string_view format,
	                        std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		t->Skipf(format, args);
	}
	[[noreturn]] void SkipNow() override {
		t->Helper();
		t->Skip({});
	}
	bool Failed() override { return t->Failed(); }
};

// retryBail is thrown by retryT Fatal/Fatalf/Skip[Now/f] to abort the test
// body; the retry driver catches it and inspects retryT state.
struct retryBail {};

inline constexpr int retryAttempts = 3;

inline int retryTimeoutScale(int attempt) {
	switch (attempt) {
	case 1:
		return 1;
	case 2:
		return 5;
	default:
		return 15;
	}
}

struct retryT : TestI {
	T* t;
	int attempt = 1;
	bool failed = false;
	bool skipped = false;

	retryT(T* t_, int attempt_) : t(t_), attempt(attempt_) {}

	void Helper() override { t->Helper(); }
	void Cleanup(std::function<void()> fn) override { t->Cleanup(std::move(fn)); }
	std::string TempDir() override { return t->TempDir(); }
	std::string Name() override { return t->Name(); }
	bool Failed() override { return failed; }

	void Log(std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		t->Log(args);
	}
	void Logf(std::string_view format,
	          std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		t->Logf(format, args);
	}
	void Error(std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		failed = true;
		t->Log(args);
	}
	void Errorf(std::string_view format,
	            std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		failed = true;
		t->Logf(format, args);
	}
	[[noreturn]] void Fatal(std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		failed = true;
		t->Log(args);
		throw retryBail{};
	}
	[[noreturn]] void Fatalf(std::string_view format,
	                         std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		failed = true;
		t->Logf(format, args);
		throw retryBail{};
	}
	[[noreturn]] void Skip(std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		skipped = true;
		t->Log(args);
		throw retryBail{};
	}
	[[noreturn]] void Skipf(std::string_view format,
	                        std::initializer_list<gostd::fmtArg> args) override {
		t->Helper();
		skipped = true;
		t->Logf(format, args);
		throw retryBail{};
	}
	[[noreturn]] void SkipNow() override {
		skipped = true;
		throw retryBail{};
	}
};

inline void runWithRetry(T* t, const std::function<void(TestI*)>& body) {
	t->Helper();
	for (int attempt = 1; attempt <= retryAttempts; attempt++) {
		retryT r{t, attempt};
		try {
			body(&r);
		} catch (const retryBail&) {
			// Our panic; fall through to verdict check.
		}
		if (r.skipped) {
			t->Skip({});
		}
		if (!r.failed) {
			if (attempt > 1) {
				t->Logf("retry: succeeded on attempt %d/%d",
				        {attempt, retryAttempts});
			}
			return;
		}
		if (attempt < retryAttempts) {
			t->Logf("retry: attempt %d/%d failed, retrying with %d× timeout "
			        "scale",
			        {attempt, retryAttempts, retryTimeoutScale(attempt + 1)});
		}
	}
	t->Errorf("retry: gave up after %d attempts", {retryAttempts});
}

// ----- watcher helpers -----------------------------------------------------

inline gostd::Duration defaultEventTimeout() { return sec(1); }
inline gostd::Duration kqueueFSEventsTimeout() { return sec(2); }

inline int testAttempt(TestI* t) {
	if (auto* rt = dynamic_cast<retryT*>(t))
		return rt->attempt;
	return 1;
}

inline gostd::Duration scaledDeadline(TestI* t, gostd::Duration base) {
	return base * retryTimeoutScale(testAttempt(t));
}

inline gostd::Duration watcherEventTimeout(TestI* t, Watcher* w) {
	gostd::Duration base = defaultEventTimeout();
	if (w == FSEvents() || w == Kqueue())
		base = kqueueFSEventsTimeout();
	return base * retryTimeoutScale(testAttempt(t));
}

// fanotifyNoRenameWatcher — test-only backend exercising the
// FAN_MOVED_FROM/_TO fallback path (fanotify_linux_test.go).
inline watcher& fanotifyNoRenameWatcher() {
	static watcher w{"fanotify-no-rename"};
	static bool init = [] {
		if (fanotifyAvailable()) {
			w.factory =
			    []() -> watcherImpl* { return newFanotifyBackend(true); };
		}
		return true;
	}();
	(void)init;
	return w;
}

// availableWatchers — computed lazily so fanotify registration resolves at
// first use rather than during static init.
inline const std::vector<Watcher*>& availableWatchers() {
	static const std::vector<Watcher*> ws = [] {
		std::vector<Watcher*> out;
		for (auto* b : AllWatchers()) {
			if (b->available())
				out.push_back(b);
		}
		// additionalTestWatchers (Go: populated by platform test inits).
		auto* nrw = &fanotifyNoRenameWatcher();
		if (nrw->available())
			out.push_back(nrw);
		return out;
	}();
	return ws;
}

inline void runForEachWatcher(T* t,
                              const std::function<void(TestI*, Watcher*)>& fn) {
	t->Helper();
	for (auto* b : availableWatchers()) {
		t->Run(b->name(), [b, &fn](T* sub) {
			sub->Parallel();
			runWithRetry(sub, [&](TestI* rt) { fn(rt, b); });
		});
	}
}

inline std::string newTmpDir(TestI* t) {
	t->Helper();
	std::string d = t->TempDir();
	auto [resolved, err] = evalSymlinks(d);
	if (err != nullptr)
		t->Fatal({err});
	return resolved;
}

inline void makeDirSymlink(TestI* t, const std::string& target,
                           const std::string& link) {
	t->Helper();
	if (auto err = osSymlink(target, link); err != nullptr) {
		t->Skipf("directory symlink support is not available: %v", {err});
	}
}

inline std::atomic<uint64_t> nameCounter{0};
inline std::string uniqueName() {
	uint64_t n = ++nameCounter;
	static std::mt19937_64 rng{std::random_device{}()};
	return gostd::sprintf("test%d%d", {n, (int64_t)(rng() & 0x7fffffffffffffff)});
}
inline std::string uniqueName(const std::string& dir) {
	return filepathJoin(dir, uniqueName());
}
inline std::string subPath(const std::string& dir) { return uniqueName(dir); }

// newDirectWatcher creates a bare dirWatch for unit-testing tree/debounce
// helpers without going through the full backend subscribe path (Go:
// newDirectWatcher returns *dirWatch; here the shared_ptr keeps it alive
// until cleanup).
inline std::shared_ptr<dirWatch>
newDirectWatcherShared(TestI* t, const std::string& dir) {
	t->Helper();
	auto w = newDirWatch(dir, dir, newDebounce());
	w->recursive = true;
	t->Cleanup([w] { w->destroyDebounce(); });
	return w;
}

struct wantEvent {
	EventKind kind;
	std::string path;
	bool operator==(const wantEvent&) const = default;
};

struct recordingWatcher;

inline gostd::Duration settleSleep(Watcher* w) {
	if (w == FSEvents() || w == Kqueue())
		return ms(300);
	return ms(60);
}
inline gostd::Duration preSubscribeSleep(Watcher* w) {
	if (w == FSEvents() || w == Kqueue())
		return ms(50);
	return gostd::Duration{0};
}

// ----- recordingWatcher ----------------------------------------------------

struct recordingWatcher {
	TestI* t;
	Watcher* watcher = nullptr;
	std::mutex mu;
	std::condition_variable cond;
	std::vector<Event> buf;
	std::vector<Error> errs;
	// callback — a bound method value usable directly as a WatchCallback.
	WatchCallback callback;

	explicit recordingWatcher(TestI* t_) : t(t_) {
		callback = [this](std::vector<Event> events, Error err) {
			std::unique_lock<std::mutex> lk(mu);
			if (err != nullptr)
				errs.push_back(err);
			for (auto& e : events)
				buf.push_back(std::move(e));
			cond.notify_all();
		};
	}

	gostd::Duration deadline() {
		if (watcher == nullptr)
			return scaledDeadline(t, defaultEventTimeout());
		return watcherEventTimeout(t, watcher);
	}

	// next blocks for up to d for at least one event, then drains and
	// returns everything that has accumulated.
	std::vector<Event> next(gostd::Duration d) {
		t->Helper();
		auto deadline = std::chrono::steady_clock::now() + d;
		std::unique_lock<std::mutex> lk(mu);
		while (buf.empty()) {
			auto remaining = deadline - std::chrono::steady_clock::now();
			if (remaining <= std::chrono::steady_clock::duration{0})
				return {};
			cond.wait_for(lk, remaining);
		}
		auto out = std::move(buf);
		buf.clear();
		return out;
	}

	std::vector<Event> drainQuiet(gostd::Duration d) {
		t->Helper();
		{
			std::lock_guard<std::mutex> lk(mu);
			buf.clear();
		}
		sleepFor(d);
		std::lock_guard<std::mutex> lk(mu);
		auto out = std::move(buf);
		buf.clear();
		return out;
	}

	std::vector<Event> gather(gostd::Duration wait, gostd::Duration settle) {
		auto first = next(wait);
		if (first.empty())
			return {};
		sleepFor(settle);
		std::lock_guard<std::mutex> lk(mu);
		for (auto& e : buf)
			first.push_back(std::move(e));
		buf.clear();
		return first;
	}

	std::vector<Event> gatherUntilQuiet(gostd::Duration initialWait,
	                                    gostd::Duration quiet) {
		auto all = next(initialWait);
		if (all.empty())
			return {};
		for (;;) {
			auto more = next(quiet);
			if (more.empty())
				return all;
			for (auto& e : more)
				all.push_back(std::move(e));
		}
	}

	std::vector<Event>
	waitForEvent(gostd::Duration d,
	             const std::function<bool(const Event&)>& pred) {
		t->Helper();
		auto deadline = std::chrono::steady_clock::now() + d;
		for (;;) {
			std::unique_lock<std::mutex> lk(mu);
			if (std::any_of(buf.begin(), buf.end(), pred)) {
				auto out = std::move(buf);
				buf.clear();
				return out;
			}
			auto remaining = deadline - std::chrono::steady_clock::now();
			if (remaining <= std::chrono::steady_clock::duration{0}) {
				auto out = std::move(buf);
				buf.clear();
				return out;
			}
			cond.wait_for(lk, remaining);
		}
	}

	std::vector<Event> waitForAll(gostd::Duration d,
	                              const std::vector<wantEvent>& want);
};

inline bool haveAll(const std::vector<Event>& got,
                    const std::vector<wantEvent>& want) {
	for (auto& w : want) {
		bool found = false;
		for (auto& e : got) {
			if (e.kind == w.kind && e.path == w.path) {
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}
	return true;
}

inline std::vector<Event>
recordingWatcher::waitForAll(gostd::Duration d,
                             const std::vector<wantEvent>& want) {
	t->Helper();
	if (want.empty())
		return {};
	auto deadline = std::chrono::steady_clock::now() + d;
	std::vector<Event> collected;
	for (;;) {
		{
			std::lock_guard<std::mutex> lk(mu);
			for (auto& e : buf)
				collected.push_back(std::move(e));
			buf.clear();
		}
		if (haveAll(collected, want))
			return collected;
		auto remaining = deadline - std::chrono::steady_clock::now();
		if (remaining <= std::chrono::steady_clock::duration{0})
			return collected;
		std::unique_lock<std::mutex> lk(mu);
		if (!buf.empty())
			continue;
		cond.wait_for(lk, remaining);
	}
}

inline std::vector<wantEvent> toWantEvents(const std::vector<Event>& events) {
	std::vector<wantEvent> out;
	out.reserve(events.size());
	for (auto& e : events)
		out.push_back({e.kind, e.path});
	return out;
}

inline std::vector<Event>
filterToWantedPaths(const std::vector<Event>& got,
                    const std::vector<wantEvent>& want) {
	std::set<std::string> paths;
	for (auto& w : want)
		paths.insert(w.path);
	std::vector<Event> out;
	for (auto& e : got) {
		if (paths.count(e.path))
			out.push_back(e);
	}
	return out;
}

inline bool equalWantEvents(std::vector<wantEvent> a,
                            const std::vector<wantEvent>& b) {
	return a == b;
}

inline bool containsEvent(const std::vector<Event>& got, EventKind kind,
                          const std::string& path) {
	for (auto& e : got) {
		if (e.kind == kind && e.path == path)
			return true;
	}
	return false;
}

inline std::vector<Event>
filterEventsForPaths(const std::vector<Event>& events,
                     std::initializer_list<std::string> paths) {
	std::set<std::string> allow(paths.begin(), paths.end());
	std::vector<Event> out;
	for (auto& e : events) {
		if (allow.count(e.path))
			out.push_back(e);
	}
	return out;
}

inline std::vector<Event> replayEventList(const std::vector<Event>& events) {
	eventList el;
	for (auto& e : events) {
		switch (e.kind) {
		case EventKind::EventUpdate:
			el.update(e.path);
			break;
		case EventKind::EventDelete:
			el.remove(e.path);
			break;
		}
	}
	return el.getEvents();
}

// ----- assertion helpers ---------------------------------------------------

inline void assertEventSet(TestI* t, std::vector<Event> got,
                           std::vector<wantEvent> want) {
	t->Helper();
	got = filterToWantedPaths(got, want);
	auto gotW = toWantEvents(got);
	auto cmp = [](const wantEvent& a, const wantEvent& b) {
		if (a.kind != b.kind)
			return (int)a.kind < (int)b.kind;
		return a.path < b.path;
	};
	std::sort(gotW.begin(), gotW.end(), cmp);
	std::sort(want.begin(), want.end(), cmp);
	if (!(gotW == want)) {
		std::string gs, ws;
		for (auto& w : gotW)
			gs += gostd::sprintf(" {%v %s}", {(int)w.kind, w.path});
		for (auto& w : want)
			ws += gostd::sprintf(" {%v %s}", {(int)w.kind, w.path});
		t->Fatalf("event mismatch\nwant:%s\n got:%s", {ws, gs});
	}
}

inline void assertEventSequence(TestI* t, std::vector<Event> got,
                                const std::vector<wantEvent>& want) {
	t->Helper();
	got = filterToWantedPaths(got, want);
	auto gotW = toWantEvents(got);
	if (!(gotW == want)) {
		std::string gs, ws;
		for (auto& w : gotW)
			gs += gostd::sprintf(" {%v %s}", {(int)w.kind, w.path});
		for (auto& w : want)
			ws += gostd::sprintf(" {%v %s}", {(int)w.kind, w.path});
		t->Fatalf("event sequence mismatch\nwant:%s\n got:%s", {ws, gs});
	}
}

inline std::vector<Event> expectEventSet(TestI* t, recordingWatcher* r,
                                         const std::vector<wantEvent>& want) {
	t->Helper();
	auto got = r->waitForAll(r->deadline(), want);
	assertEventSet(t, got, want);
	return got;
}

inline std::vector<Event>
expectEventSequence(TestI* t, recordingWatcher* r,
                    const std::vector<wantEvent>& want) {
	t->Helper();
	auto got = r->waitForAll(r->deadline(), want);
	assertEventSequence(t, got, want);
	return got;
}

inline std::vector<Event> expectContains(TestI* t, recordingWatcher* r,
                                         EventKind kind,
                                         const std::string& path) {
	t->Helper();
	auto d = r->deadline();
	auto got = r->waitForEvent(
	    d, [&](const Event& e) { return e.kind == kind && e.path == path; });
	if (!containsEvent(got, kind, path)) {
		std::string gs;
		for (auto& w : toWantEvents(got))
			gs += gostd::sprintf(" {%v %s}", {(int)w.kind, w.path});
		t->Fatalf("expected event %v %s within %v, got%s",
		          {(int)kind, path, gostd::durationString(d), gs});
	}
	return got;
}

inline void expectNoBufferedEvents(TestI* t, recordingWatcher* r,
                                   const std::string& msg) {
	t->Helper();
	std::vector<Event> got;
	{
		std::lock_guard<std::mutex> lk(r->mu);
		got = std::move(r->buf);
		r->buf.clear();
	}
	if (!got.empty()) {
		std::string gs;
		for (auto& w : toWantEvents(got))
			gs += gostd::sprintf(" {%v %s}", {(int)w.kind, w.path});
		t->Fatalf("%s, got%s", {msg, gs});
	}
}

inline void assertNoEventsForPath(TestI* t, const std::vector<Event>& got,
                                  const std::string& path,
                                  const std::string& msg) {
	t->Helper();
	auto filtered = filterEventsForPaths(got, {path});
	if (!filtered.empty()) {
		std::string gs;
		for (auto& w : toWantEvents(filtered))
			gs += gostd::sprintf(" {%v %s}", {(int)w.kind, w.path});
		t->Fatalf("%s %s, got%s", {msg, path, gs});
	}
}

inline bool equalStringSlices(const std::vector<std::string>& a,
                              const std::vector<std::string>& b) {
	return a == b;
}

// ----- subscribe helpers ---------------------------------------------------

inline recordingWatcher* newRecorder(TestI* t) {
	return new recordingWatcher(t); // owned by the test body scope
}

// subscribeForOpts — recorder + WatchDirectory with options + cleanup.
// The recorder is heap-allocated and intentionally leaked until process
// exit (mirroring Go's GC; watches outlive the test via cleanup closes).
inline std::pair<recordingWatcher*, std::shared_ptr<Watch>>
subscribeForOpts(TestI* t, const std::string& dir, Watcher* watcherImpl,
                 std::vector<std::shared_ptr<WatchOption>> opts = {}) {
	t->Helper();
	if (auto d = preSubscribeSleep(watcherImpl); d > gostd::Duration{0})
		sleepFor(d);
	auto* r = new recordingWatcher(t);
	r->watcher = watcherImpl;
	auto [sub, err] = watcherImpl->watchDirectory(dir, r->callback, opts);
	if (err != nullptr) {
		delete r;
		t->Fatalf("subscribe: %v", {err});
	}
	std::shared_ptr<Watch> subKeep = sub;
	t->Cleanup([subKeep] { (void)subKeep->close(); });
	sleepFor(settleSleep(watcherImpl));
	return {r, sub};
}

inline std::pair<recordingWatcher*, std::shared_ptr<Watch>>
subscribeFor(TestI* t, const std::string& dir, Watcher* watcherImpl) {
	return subscribeForOpts(t, dir, watcherImpl, {WithRecursive()});
}

inline std::pair<recordingWatcher*, std::shared_ptr<Watch>>
subscribeFileFor(TestI* t, const std::string& path, Watcher* watcherImpl) {
	t->Helper();
	if (auto d = preSubscribeSleep(watcherImpl); d > gostd::Duration{0})
		sleepFor(d);
	auto* r = new recordingWatcher(t);
	r->watcher = watcherImpl;
	auto [sub, err] = watcherImpl->watchFile(path, r->callback);
	if (err != nullptr) {
		delete r;
		t->Fatalf("subscribeFile: %v", {err});
	}
	std::shared_ptr<Watch> subKeep = sub;
	t->Cleanup([subKeep] { (void)subKeep->close(); });
	sleepFor(settleSleep(watcherImpl));
	return {r, sub};
}

} // namespace tsc::fswatch
