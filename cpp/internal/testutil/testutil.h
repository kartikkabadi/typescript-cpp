// Declarations + tiny dep-impls for tsc/internal/testutil (testutil.go) and
// gotest.tools/v3/assert — consumed by the testrunner slice. The dep-impls
// are real (they sit on the ported code's control-flow paths); delete them
// when the owning slice lands real headers.
#pragma once

#include <any>
#include <exception>
#include <string>
#include <string_view>
#include <typeinfo>

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

// Assert — fwd decl for AssertPanics below (defined at file end; the
// default argument lives there).
inline void Assert(gostd::testing::T* t, bool condition,
                   std::string_view msg);

namespace detail {

// anyEqual — reflect.DeepEqual(got, expected) for the common `any`
// shapes tests compare: string, int/int64, bool, and Error.
inline bool anyEqual(const std::any& a, const std::any& b) {
	if (a.type() != b.type()) {
		return false;
	}
	if (!a.has_value()) {
		return true;
	}
	if (a.type() == typeid(std::string)) {
		return std::any_cast<std::string>(a) == std::any_cast<std::string>(b);
	}
	if (a.type() == typeid(const char*)) {
		return std::string(std::any_cast<const char*>(a)) ==
		       std::any_cast<const char*>(b);
	}
	if (a.type() == typeid(std::string_view)) {
		return std::any_cast<std::string_view>(a) ==
		       std::any_cast<std::string_view>(b);
	}
	if (a.type() == typeid(int)) {
		return std::any_cast<int>(a) == std::any_cast<int>(b);
	}
	if (a.type() == typeid(int64_t)) {
		return std::any_cast<int64_t>(a) == std::any_cast<int64_t>(b);
	}
	if (a.type() == typeid(bool)) {
		return std::any_cast<bool>(a) == std::any_cast<bool>(b);
	}
	if (a.type() == typeid(double)) {
		return std::any_cast<double>(a) == std::any_cast<double>(b);
	}
	if (a.type() == typeid(gostd::Error)) {
		return std::any_cast<gostd::Error>(a) == std::any_cast<gostd::Error>(b);
	}
	return false;
}

}  // namespace detail

// Equal — gotest.tools/v3/assert.Equal: fails the test when got !=
// expected (reflect.DeepEqual on the Go side). For `std::any` operands the
// common shapes above are compared; unequal types fail.
template <class A, class B>
void Equal(gostd::testing::T* t, const A& got, const B& expected,
           std::string_view msg = "") {
	bool ok;
	if constexpr (std::is_same_v<A, std::any> &&
	              std::is_same_v<B, std::any>) {
		ok = detail::anyEqual(got, expected);
	} else {
		ok = (got == expected);
	}
	if (!ok) {
		t->Fatalf("assert.Equal failed%s%s", {msg.empty() ? "" : ": ",
	                                        std::string(msg)});
	}
}

// NilError — gotest.tools/v3/assert.NilError: fails the test when err is
// non-nil.
inline void NilError(gostd::testing::T* t, const gostd::Error& err,
                     std::string_view msg = "") {
	if (err != nullptr) {
		if (msg.empty()) {
			t->Fatalf("assert.NilError failed: %v", {err});
		} else {
			t->Fatalf("assert.NilError failed (%s): %v",
			          {std::string(msg), err});
		}
	}
}

// Check — gotest.tools/v3/assert.Check: like Assert but non-fatal (Go logs
// the failure and the test continues).
inline void Check(gostd::testing::T* t, bool condition,
                  std::string_view msg = "") {
	if (!condition) {
		if (msg.empty()) {
			t->Error({"assertion failed"});
		} else {
			t->Error({std::string(msg)});
		}
	}
}

// DeepEqual — gotest.tools/v3/assert.DeepEqual: fails the test when
// reflect.DeepEqual(got, expected) is false. C++ port relies on operator==
// doing memberwise comparison (all ported structs it feeds have one).
template <class A, class B>
void DeepEqual(gostd::testing::T* t, const A& got, const B& expected,
               std::string_view msg = "") {
	bool ok;
	if constexpr (std::is_same_v<A, std::any> &&
	              std::is_same_v<B, std::any>) {
		ok = detail::anyEqual(got, expected);
	} else {
		ok = (got == expected);
	}
	if (!ok) {
		t->Fatalf("assert.DeepEqual failed%s%s", {msg.empty() ? "" : ": ",
		                                        std::string(msg)});
	}
}

// Error — gotest.tools/v3/assert.Error: fails when err is nil or its
// message differs from expected.
inline void Error(gostd::testing::T* t, const gostd::Error& err,
                  const std::string& expected, std::string_view msg = "") {
	if (err == nullptr) {
		t->Fatalf("assert.Error failed: expected error %q, got nil%s%s",
		          {expected, msg.empty() ? "" : ": ", std::string(msg)});
		return;
	}
	if (err->Error() != expected) {
		t->Fatalf("assert.Error failed: got %q, expected %q%s%s",
		          {err->Error(), expected, msg.empty() ? "" : ": ",
		           std::string(msg)});
	}
}

// ErrorContains — gotest.tools/v3/assert.ErrorContains: fails when err is
// nil or its message doesn't contain the expected substring.
inline void ErrorContains(gostd::testing::T* t, const gostd::Error& err,
                          const std::string& expected,
                          std::string_view msg = "") {
	if (err == nullptr) {
		t->Fatalf("assert.ErrorContains failed: expected error containing "
		          "%q, got nil%s%s",
		          {expected, msg.empty() ? "" : ": ", std::string(msg)});
		return;
	}
	if (err->Error().find(expected) == std::string::npos) {
		t->Fatalf("assert.ErrorContains failed: %q does not contain %q%s%s",
		          {err->Error(), expected, msg.empty() ? "" : ": ",
		           std::string(msg)});
	}
}

// ErrorIs — gotest.tools/v3/assert.ErrorIs: fails when errorIs(err, target)
// is false (walks the wrapped-error chain like errors.Is).
inline void ErrorIs(gostd::testing::T* t, const gostd::Error& err,
                    const gostd::Error& target, std::string_view msg = "") {
	if (!gostd::errorIs(err, target)) {
		t->Fatalf("assert.ErrorIs failed%s%s", {msg.empty() ? "" : ": ",
		                                      std::string(msg)});
	}
}

}  // namespace tsc::gotest::assert

namespace tsc::testutil {

// AssertPanics — testutil.go:14. Go recovers the panic value from fn and
// asserts it equals `expected`. C++ panics are exceptions: `got` captures
// the thrown value — std::string for exception what()s and thrown
// strings — which anyEqual compares.
template <typename F>
void AssertPanics(gostd::testing::T* tb, F&& fn, const std::any& expected,
                  std::string_view msg = "") {
	tb->Helper();
	std::any got;
	try {
		fn();
	} catch (const gostd::testing::testGoexit&) {
		throw;
	} catch (const std::exception& e) {
		got = std::string(e.what());
	} catch (const std::string& s) {
		got = s;
	} catch (const char* s) {
		got = std::string(s);
	} catch (...) {
		got = std::string("<panic>");
	}
	gotest::assert::Assert(tb, got.has_value(), std::string(msg));
	gotest::assert::Equal(tb, got, expected, std::string(msg));
}

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

// Check — gotest.tools/v3/assert.Check: like Assert but non-fatal (t.Error,
// the test continues).
inline void Check(gostd::testing::T* t, bool condition,
                  std::string_view msg = "") {
	if (!condition) {
		if (msg.empty()) {
			t->Error({"assertion failed"});
		} else {
			t->Error({std::string(msg)});
		}
	}
}

}  // namespace tsc::gotest::assert
