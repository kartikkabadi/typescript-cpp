// gostd.h — minimal Go standard-library shims shared by the ports.
// Errors (errors.New/Is/As/Join, fmt.Errorf %w), context.Context
// (Background/WithCancel/WithTimeout/AfterFunc), io reader/writer/closer
// interfaces, sync.OnceFunc, and time helpers.
#pragma once

#include <any>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// Declaration of ast.h's unreachable hook so leaf headers (json, jsonrpc,
// gostd itself) need not include ast.h; the definition lives in ast.h and is
// emitted by TUs that include it.
namespace tsc {
[[noreturn]] void tscUnreachable(const char* msg);
} // namespace tsc
#ifndef TSC_UNREACHABLE
#define TSC_UNREACHABLE(msg) ::tsc::tscUnreachable(msg)
#endif

namespace tsc::gostd {

// ---------------------------------------------------------------------------
// errors
// ---------------------------------------------------------------------------

// ErrObj is the Go `error` interface: one method, Error() string.
struct ErrObj {
	virtual ~ErrObj() = default;
	virtual std::string Error() const = 0;
	// Unwrap() error / Unwrap() []error — the wrapped chain.
	virtual std::vector<std::shared_ptr<ErrObj>> unwrap() const { return {}; }
};

using Error = std::shared_ptr<ErrObj>;

namespace detail {

struct errorString : ErrObj {
	std::string s;
	explicit errorString(std::string_view v) : s(v) {}
	std::string Error() const override { return s; }
};

struct wrapError : ErrObj {
	std::string msg;
	gostd::Error err;
	wrapError(std::string m, gostd::Error e)
	    : msg(std::move(m)), err(std::move(e)) {}
	std::string Error() const override { return msg; }
	std::vector<gostd::Error> unwrap() const override { return {err}; }
};

struct joinError : ErrObj {
	std::vector<gostd::Error> errs;
	std::string msg;
	explicit joinError(std::vector<gostd::Error> e) : errs(std::move(e)) {
		for (size_t i = 0; i < errs.size(); i++) {
			if (i) msg += '\n';
			msg += errs[i]->Error();
		}
	}
	std::string Error() const override { return msg; }
	std::vector<gostd::Error> unwrap() const override { return errs; }
};

} // namespace detail

// errors.New
inline Error newError(std::string_view text) {
	return std::make_shared<detail::errorString>(text);
}

// errors.Join(errs...) — nil when every entry is nil.
inline Error joinError(std::vector<Error> errs) {
	std::vector<Error> nonNil;
	for (auto& e : errs) {
		if (e != nullptr) {
			nonNil.push_back(e);
		}
	}
	if (nonNil.empty()) {
		return nullptr;
	}
	return std::make_shared<detail::joinError>(std::move(nonNil));
}

// errors.Is — identity equality through the unwrap chain.
inline bool errorIs(const Error& err, const Error& target) {
	if (err == nullptr || target == nullptr) {
		return false;
	}
	if (err == target) {
		return true;
	}
	for (const auto& u : err->unwrap()) {
		if (errorIs(u, target)) {
			return true;
		}
	}
	return false;
}

// errors.AsType[T] — first error in the chain that dynamic_casts to T.
template <typename T>
inline T errorAs(const Error& err) {
	if (err == nullptr) {
		return nullptr;
	}
	if (auto* p = dynamic_cast<T>(err.get()); p != nullptr) {
		return p;
	}
	for (const auto& u : err->unwrap()) {
		if (auto p = errorAs<T>(u); p != nullptr) {
			return p;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// fmt-style formatting: %v %s %d %q %w %p %x
// ---------------------------------------------------------------------------

// fmtArg is one formatting argument. %v/%s/%d print text; %w additionally marks
// the wrapped error for errorf.
struct fmtArg {
	std::string text;
	Error err;
	fmtArg(const char* s) : text(s == nullptr ? "<nil>" : s) {}
	fmtArg(std::string_view s) : text(s) {}
	fmtArg(const std::string& s) : text(s) {}
	fmtArg(const Error& e) : text(e == nullptr ? "<nil>" : e->Error()), err(e) {}
	fmtArg(const ErrObj& e) : text(e.Error()) {}
	fmtArg(int v) : text(std::to_string(v)) {}
	fmtArg(uint32_t v) : text(std::to_string(v)) {}
	fmtArg(short v) : text(std::to_string(v)) {}
	fmtArg(unsigned short v) : text(std::to_string(v)) {}
	fmtArg(int64_t v) : text(std::to_string(v)) {}
	fmtArg(uint64_t v) : text(std::to_string(v)) {}
	fmtArg(double v) : text(std::to_string(v)) {}
	fmtArg(bool v) : text(v ? "true" : "false") {}
	fmtArg(char c) : text(1, c) {}
	static fmtArg ptr(const void* p) {
		char buf[32];
		std::snprintf(buf, sizeof buf, "%#llx",
		              (unsigned long long)(uintptr_t)p);
		return fmtArg(std::string(buf));
	}
};

namespace detail {

// strconv.Quote-compatible escaping for %q.
inline std::string quoteGo(std::string_view s) {
	std::string out;
	out.reserve(s.size() + 2);
	out += '"';
	for (size_t i = 0; i < s.size(); i++) {
		unsigned char c = (unsigned char)s[i];
		switch (c) {
		case '\a': out += "\\a"; continue;
		case '\b': out += "\\b"; continue;
		case '\f': out += "\\f"; continue;
		case '\n': out += "\\n"; continue;
		case '\r': out += "\\r"; continue;
		case '\t': out += "\\t"; continue;
		case '\v': out += "\\v"; continue;
		case '\\': out += "\\\\"; continue;
		case '"': out += "\\\""; continue;
		}
		if (c >= 0x20 && c < 0x7f) {
			out += (char)c;
		} else if (c < 0x80) {
			char buf[8];
			std::snprintf(buf, sizeof buf, "\\x%02x", c);
			out += buf;
		} else {
			// Multi-byte UTF-8: emit literal (printable runes) — adequate for
			// mapper names; control/non-printable runes are rare here.
			out += (char)c;
		}
	}
	out += '"';
	return out;
}

inline std::string sprintfImpl(std::string_view fmt,
                               const std::vector<fmtArg>& args,
                               std::vector<Error>* wrapped) {
	std::string out;
	out.reserve(fmt.size() + 16);
	size_t arg = 0;
	for (size_t i = 0; i < fmt.size(); i++) {
		char c = fmt[i];
		if (c != '%' || i + 1 >= fmt.size()) {
			out += c;
			continue;
		}
		char verb = fmt[++i];
		if (verb == '%') {
			out += '%';
			continue;
		}
		if (arg >= args.size()) {
			out += '%';
			out += verb;
			continue;
		}
		const fmtArg& a = args[arg++];
		switch (verb) {
		case 'q': out += quoteGo(a.text); break;
		case 'x': {
			for (unsigned char b : a.text) {
				char buf[4];
				std::snprintf(buf, sizeof buf, "%02x", b);
				out += buf;
			}
			break;
		}
		case 'w':
			out += a.text;
			if (wrapped != nullptr && a.err != nullptr) {
				wrapped->push_back(a.err);
			}
			break;
		default: out += a.text; break; // %v %s %d %p and others
		}
	}
	return out;
}

} // namespace detail

// fmt.Sprintf — Go verb subset.
inline std::string sprintf(std::string_view fmt,
                           std::initializer_list<fmtArg> args) {
	return detail::sprintfImpl(fmt, std::vector<fmtArg>(args), nullptr);
}
inline std::string sprintf(std::string_view fmt, const std::vector<fmtArg>& args) {
	return detail::sprintfImpl(fmt, args, nullptr);
}

// fmt.Errorf — %w produces an unwrap chain entry; returns nil-like Error never.
inline Error errorf(std::string_view fmt, std::initializer_list<fmtArg> args) {
	std::vector<Error> wrapped;
	std::string msg = detail::sprintfImpl(fmt, std::vector<fmtArg>(args), &wrapped);
	if (wrapped.empty()) {
		return newError(msg);
	}
	// Go wraps the LAST %w operand for the common single-%w case.
	if (wrapped.size() == 1) {
		return std::make_shared<detail::wrapError>(msg, wrapped[0]);
	}
	return std::make_shared<detail::joinError>(wrapped);
}

// ---------------------------------------------------------------------------
// context.Context
// ---------------------------------------------------------------------------

inline const Error errCanceled = newError("context canceled");
inline const Error errDeadlineExceeded =
	newError("context deadline exceeded");

struct ContextImpl {
	std::mutex mu;
	std::condition_variable cv;
	bool done = false;
	Error err;
	std::vector<std::function<void()>> afterFuncs;
	// Go context.WithValue storage: each WithValue call produces a child
	// context whose map is the parent's merged with the new key.
	std::unordered_map<const void*, std::any> values;

	// Value returns the value stored under `key`, or nullptr if absent.
	const std::any* value(const void* key) const {
		auto it = values.find(key);
		return it == values.end() ? nullptr : &it->second;
	}
};

using Context = std::shared_ptr<ContextImpl>;
using CancelFunc = std::function<void()>;

// context.WithValue — returns a context carrying key=value (a shallow copy
// of the parent that shadows the key, matching Go's child-wins lookup).
inline Context contextWithValue(const Context& parent, const void* key,
                                std::any value) {
	auto child = std::make_shared<ContextImpl>();
	if (parent) {
		child->values = parent->values;
	}
	child->values[key] = std::move(value);
	return child;
}

inline Context contextBackground() {
	static const Context bg = std::make_shared<ContextImpl>();
	return bg;
}

inline void ctxCancel(const Context& c, const Error& err) {
	std::vector<std::function<void()>> fns;
	{
		std::lock_guard<std::mutex> lk(c->mu);
		if (c->done) {
			return;
		}
		c->done = true;
		c->err = err;
		fns.swap(c->afterFuncs);
	}
	c->cv.notify_all();
	// Go runs each AfterFunc in its own goroutine.
	for (auto& f : fns) {
		std::thread(std::move(f)).detach();
	}
}

inline Error ctxErr(const Context& c) {
	std::lock_guard<std::mutex> lk(c->mu);
	return c->err;
}

// context.AfterFunc — returns the stop function (true if it stopped f before
// it started; false if f already began).
inline std::function<bool()> contextAfterFunc(const Context& c,
                                              std::function<void()> f) {
	auto flag = std::make_shared<std::atomic<bool>>(false);
	{
		std::lock_guard<std::mutex> lk(c->mu);
		if (!c->done) {
			c->afterFuncs.push_back([flag, f] {
				if (!flag->exchange(true)) {
					f();
				}
			});
			return [flag] { return !flag->exchange(true); };
		}
	}
	std::thread([flag, f] {
		if (!flag->exchange(true)) {
			f();
		}
	}).detach();
	return [flag] { return !flag->exchange(true); };
}

// context.WithCancel — child cancels with errCanceled when the parent finishes.
inline std::pair<Context, CancelFunc> contextWithCancel(const Context& parent) {
	auto c = std::make_shared<ContextImpl>();
	std::weak_ptr<ContextImpl> w = c;
	contextAfterFunc(parent, [w] {
		if (auto s = w.lock()) {
			ctxCancel(s, errCanceled);
		}
	});
	return {c, [c] { ctxCancel(c, errCanceled); }};
}

// context.WithTimeout(parent, d) — as WithCancel plus a deadline that cancels
// with errDeadlineExceeded.
inline std::pair<Context, CancelFunc>
contextWithTimeout(const Context& parent,
                   std::chrono::nanoseconds timeout) {
	auto [c, cancel] = contextWithCancel(parent);
	std::weak_ptr<ContextImpl> w = c;
	std::thread([w, timeout] {
		if (auto s = w.lock()) {
			std::unique_lock<std::mutex> lk(s->mu);
			s->cv.wait_for(lk, timeout, [&] { return s->done; });
		}
		if (auto s = w.lock()) {
			ctxCancel(s, errDeadlineExceeded);
		}
	}).detach();
	return {c, cancel};
}

// ---------------------------------------------------------------------------
// io
// ---------------------------------------------------------------------------

namespace io {

struct Reader {
	virtual ~Reader() = default;
	virtual std::pair<int, Error> read(std::span<char> buf) = 0;
};
struct Writer {
	virtual ~Writer() = default;
	virtual std::pair<int, Error> write(std::string_view data) = 0;
};
struct Closer {
	virtual ~Closer() = default;
	virtual Error close() = 0;
};
struct ReadWriter : Reader, Writer {};
struct ReadWriteCloser : Reader, Writer, Closer {};

// === slice: ipc ===
// io.EOF / io.ErrUnexpectedEOF sentinels and the single-purpose closer
// interfaces (used by the jsonrpc base-protocol reader and the ipc stdio
// transport).
inline const Error errEOF = newError("EOF");
inline const Error errUnexpectedEOF = newError("unexpected EOF");
struct ReadCloser : Reader, Closer {};
struct WriteCloser : Writer, Closer {};
// === end slice: ipc ===

namespace detail {
struct discardWriter : Writer {
	std::pair<int, Error> write(std::string_view data) override {
		return {(int)data.size(), nullptr};
	}
};
} // namespace detail

// io.Discard
inline Writer* discard() {
	static detail::discardWriter w;
	return &w;
}

} // namespace io

// ---------------------------------------------------------------------------
// sync.OnceFunc
// ---------------------------------------------------------------------------

inline std::function<void()> onceFunc(std::function<void()> f) {
	auto flag = std::make_shared<std::once_flag>();
	return [flag, f = std::move(f)]() mutable { std::call_once(*flag, f); };
}

// ---------------------------------------------------------------------------
// time
// ---------------------------------------------------------------------------

using Duration = std::chrono::nanoseconds;
using Time = std::chrono::steady_clock::time_point;
using Clock = std::chrono::steady_clock;

inline Time now() { return Clock::now(); }
inline Duration since(Time t) { return Clock::now() - t; }
constexpr Duration second() { return Duration(std::chrono::seconds(1)); }

} // namespace tsc::gostd
