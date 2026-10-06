// Declarations + tiny dep-impls for tsc/internal/testutil (testutil.go) and
// gotest.tools/v3/assert — consumed by the testrunner slice. The dep-impls
// are real (they sit on the ported code's control-flow paths); delete them
// when the owning slice lands real headers.
#pragma once

#include <exception>
#include <string>
#include <string_view>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"

namespace tsc::testutil {

// RecoverAndFail — testutil.go:31. Go recovers an in-flight panic and
// fails the test; recover() cannot stop runtime.Goexit, so testGoexit is
// re-thrown untouched. The panic value formatting matches
// "%s:\n%v\n<stack>" — C++ has no stack capture, so e.what() is used.
inline void RecoverAndFail(gostd::testing::T* t, const std::string& msg) {
	auto e = std::current_exception();
	if (!e) return;
	try {
		std::rethrow_exception(e);
	} catch (const gostd::testing::testGoexit&) {
		throw;
	} catch (const std::exception& x) {
		t->Fatalf("%s:\n%s", {msg, x.what()});
	} catch (...) {
		t->Fatalf("%s:\npanic", {msg});
	}
}

// withRecoverAndFail models `defer testutil.RecoverAndFail(t, msg)`: the
// guarded body runs inside a try; a propagating exception is the recovered
// panic. testGoexit (Go's Goexit) passes through, like Goexit does.
template <typename F>
void withRecoverAndFail(gostd::testing::T* t, const std::string& msg,
                        F&& body) {
	try {
		body();
	} catch (const gostd::testing::testGoexit&) {
		throw;
	} catch (...) {
		RecoverAndFail(t, msg);
	}
}

// TestProgramIsSingleThreaded — testutil.go:44.
bool TestProgramIsSingleThreaded();

}  // namespace tsc::testutil

namespace tsc::gotest::assert {

// Assert — gotest.tools/v3/assert. Fails the test when the condition is
// false; msgAndArgs are folded into one message here.
inline void Assert(gostd::testing::T* t, bool condition,
                   std::string_view msg = "") {
	if (!condition) {
		if (msg.empty()) {
			t->Fatal({"assertion failed"});
		} else {
			t->Fatal({std::string(msg)});
		}
	}
}

}  // namespace tsc::gotest::assert
