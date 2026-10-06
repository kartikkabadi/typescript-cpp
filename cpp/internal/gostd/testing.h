// Minimal Go `testing` package analog for the C++ port: just enough of
// testing.T for the testrunner slice (and future test-infra slices) to
// express subtests, parallel marks, skips and fatal failures.
//
// Go semantics mapped:
//   t.Run(name, fn)     — runs fn synchronously on a child T; a child failure
//                         marks the parent failed. Returns !child.Failed().
//   t.Parallel()        — sequential port; recorded no-op.
//   t.Fatal/Fatalf      — mark failed and unwind the test goroutine
//                         (runtime.Goexit). Modeled by throwing testGoexit,
//                         which unwinds C++ destructors like Goexit unwinds
//                         deferred calls and is caught by T::Run.
//   t.Skip/Skipf        — mark skipped and Goexit (same unwind model).
//   t.Context()         — the test's context; canceled tests aren't modeled,
//                         so the background context is returned.
//
// testGoexit is deliberately NOT catchable by recover(): RecoverAndFail
// ignores it, matching Go where recover() cannot stop Goexit.
#pragma once

#include <chrono>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "internal/gostd/gostd.h"

namespace tsc::gostd::testing {

// testGoexit models runtime.Goexit: thrown by Fatal*/Skip* to unwind the
// test function. It is not a panic and is never "recovered".
struct testGoexit {};

namespace detail {
inline std::string joinArgs(std::initializer_list<fmtArg> args) {
	std::string out;
	bool first = true;
	for (const auto& a : args) {
		if (!first) out += ' ';
		first = false;
		out += a.text;
	}
	return out;
}
}  // namespace detail

class T {
public:
	virtual ~T() {
		// Go runs Cleanup functions when the test function returns,
		// LIFO. A test T lives exactly as long as its test function
		// (subtests run on stack children inside Run), so the
		// destructor is the same point.
		runCleanups();
	}

	// Run runs fn as a subtest named `name`, synchronously (the port has no
	// parallel test scheduler). Child failure propagates to the parent like
	// Go's testing framework. Returns whether the subtest succeeded.
	virtual bool Run(std::string_view name,
	                 const std::function<void(T*)>& fn) {
		T child;
		child.name_ = std::string(name);
		child.parent_ = this;
		try {
			fn(&child);
		} catch (const testGoexit&) {
		}
		if (child.failed_) {
			failed_ = true;
		}
		// Go doesn't mark the parent skipped for a skipped subtest, but
		// the runner needs the result: propagate it for reporting.
		if (child.skipped_) {
			skipped_ = true;
		}
		return !child.failed_;
	}

	// Parallel marks the test as able to run in parallel. The port runs
	// tests sequentially, so this is a no-op.
	virtual void Parallel() {}

	// Helper marks the calling function as a test helper (frame is skipped
	// in Go failure output). No-op here.
	virtual void Helper() {}

	// Context returns the test's context (background in this port).
	Context Context() const { return contextBackground(); }

	// === slice: testutil-leaves ===
	// Cleanup — testing.T.Cleanup: registers fn to run when the test
	// ends; functions run LIFO like Go.
	virtual void Cleanup(std::function<void()> fn) {
		cleanups_.push_back(std::move(fn));
	}

	// TempDir — testing.T.TempDir: creates a unique temporary
	// directory under the OS temp dir and registers its removal with
	// Cleanup. Go names it with the test name sanitized; uniqueness
	// is what callers rely on, so the timestamped name suffices.
	virtual std::string TempDir() {
		auto base = std::filesystem::temp_directory_path();
		auto stamp = std::to_string(
			std::chrono::steady_clock::now().time_since_epoch().count());
		for (int i = 0;; i++) {
			auto dir = base / ("go-test-" + stamp + "-" +
			                   std::to_string(i));
			std::error_code ec;
			if (std::filesystem::create_directory(dir, ec)) {
				std::string path = dir.string();
				Cleanup([path] {
					std::error_code ec;
					std::filesystem::remove_all(path, ec);
				});
				return path;
			}
		}
	}
	// === end slice: testutil-leaves ===

	bool Failed() const { return failed_; }
	bool Skipped() const { return skipped_; }
	const std::string& Name() const { return name_; }

	void Log(std::initializer_list<fmtArg> args) {
		std::cerr << detail::joinArgs(args) << '\n';
	}
	void Logf(std::string_view format, std::initializer_list<fmtArg> args) {
		std::cerr << sprintf(format, args) << '\n';
	}
	void Error(std::initializer_list<fmtArg> args) {
		failed_ = true;
		std::cerr << detail::joinArgs(args) << '\n';
	}
	void Errorf(std::string_view format, std::initializer_list<fmtArg> args) {
		failed_ = true;
		std::cerr << sprintf(format, args) << '\n';
	}

	// Fatal is equivalent to Log + FailNow: marks the test failed and
	// unwinds like runtime.Goexit.
	[[noreturn]] void Fatal(std::initializer_list<fmtArg> args) {
		failed_ = true;
		std::cerr << detail::joinArgs(args) << '\n';
		throw testGoexit{};
	}
	[[noreturn]] void Fatalf(std::string_view format,
	                         std::initializer_list<fmtArg> args) {
		failed_ = true;
		std::cerr << sprintf(format, args) << '\n';
		throw testGoexit{};
	}
	[[noreturn]] void FailNow() {
		failed_ = true;
		throw testGoexit{};
	}
	[[noreturn]] void Skip(std::initializer_list<fmtArg> args) {
		skipped_ = true;
		std::cerr << detail::joinArgs(args) << '\n';
		throw testGoexit{};
	}
	[[noreturn]] void Skipf(std::string_view format,
	                        std::initializer_list<fmtArg> args) {
		skipped_ = true;
		std::cerr << sprintf(format, args) << '\n';
		throw testGoexit{};
	}

private:
	void runCleanups() {
		for (auto it = cleanups_.rbegin(); it != cleanups_.rend(); ++it) {
			(*it)();
		}
		cleanups_.clear();
	}

	bool failed_ = false;
	bool skipped_ = false;
	T* parent_ = nullptr;
	std::string name_;
	std::vector<std::function<void()>> cleanups_;
};

}  // namespace tsc::gostd::testing
