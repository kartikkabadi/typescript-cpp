// lsp — port of tsc/internal/lsp: the LSP server loop — request dispatch,
// dynamic request queue, progress reporting, logging, stack sanitizing.
// Sources: server.go (lsp_server.cpp), dynamic_queue.go (this header —
// generic template), progress.go (lsp_progress.cpp), logger.go
// (lsp_logger.cpp), stack_sanitizer.go (lsp_stack_sanitizer.cpp).
#pragma once

#include <algorithm>
#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/core/context.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/gostd/gostd.h"
#include "internal/jsonrpc/jsonrpc.h"
#include "internal/locale/locale.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/pprof/pprof.h"
#include "internal/project/logging/logging.h"
#include "internal/project/client.h"
#include "internal/project/project.h"
#include "internal/project/session.h"
#include "internal/project/sessiontypes.h"
#include "internal/vfs/vfs.h"

namespace tsc::api { class Session; }
namespace tsc::contentmapper { struct Mapper; struct Spawner; }
namespace tsc::compiler { struct WriteFileData; struct EmitOptions; }
namespace tsc::ls {
class LanguageService;
struct Project;
struct CrossProjectOrchestrator;
namespace lsutil { struct UserPreferences; struct JsonAny; }
}
namespace tsc::lsconv { class Converters; }
namespace tsc::lsp::lspwatcher { struct Watcher; }

namespace tsc::lsp {

// ---------------------------------------------------------------------------
// detail — helpers shared across the lsp translation units.
// ---------------------------------------------------------------------------
namespace detail {

// sprint — fmt.Sprint. Every ported call site passes a string operand at each
// boundary, so Go's "space between two non-string operands" rule never
// inserts a space: plain concatenation is faithful.
inline std::string sprint(const std::vector<gostd::fmtArg>& args) {
	std::string out;
	for (const auto& a : args) {
		out += a.text;
	}
	return out;
}

// sprintArgs — diagnostics.StringifyArgs ([]any -> []string); fmtArg's text
// is already the %v form.
inline std::vector<std::string> stringifyArgs(
	const std::vector<gostd::fmtArg>& args) {
	std::vector<std::string> out;
	out.reserve(args.size());
	for (const auto& a : args) {
		out.push_back(a.text);
	}
	return out;
}

// unixMilliNow — time.Now().UnixMilli().
inline int64_t unixMilliNow() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
	           std::chrono::system_clock::now().time_since_epoch())
	    .count();
}

// unixNanoNow — time.Now().UnixNano().
inline int64_t unixNanoNow() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
	           std::chrono::system_clock::now().time_since_epoch())
	    .count();
}

// goNew — Go `new(v)` where the target is a pointer-to-value field.
template <class T>
inline std::shared_ptr<std::decay_t<T>> goNew(T&& v) {
	return std::make_shared<std::decay_t<T>>(std::forward<T>(v));
}

// goTimer — time.Timer equivalent for the progress delay: fires f after d
// unless stopped. The timer thread holds the shared state, mirroring Go's GC
// keeping a pending timer's channel + closure alive.
struct goTimer {
	struct Shared {
		std::mutex mu;
		std::condition_variable cv;
		bool stopped = false;
	};
	std::shared_ptr<Shared> st = std::make_shared<Shared>();

	static std::shared_ptr<goTimer>
	afterFunc(gostd::Duration d, std::function<void()> f) {
		auto t = std::make_shared<goTimer>();
		auto st = t->st;
		std::thread([st, d, f = std::move(f)] {
			bool fired;
			{
				std::unique_lock<std::mutex> lk(st->mu);
				fired = !st->cv.wait_for(lk, d, [&] { return st->stopped; });
			}
			if (fired) {
				f();
			}
		}).detach();
		return t;
	}

	// time.Timer.Stop — returns whether the timer was still pending.
	bool stop() {
		std::lock_guard<std::mutex> lk(st->mu);
		bool wasPending = !st->stopped;
		st->stopped = true;
		st->cv.notify_all();
		return wasPending;
	}
};

// closeSignal — `chan struct{}` closed once (initComplete). Broadcast wake on
// close; wait()/isClosed() are the <-ch / non-blocking recv.
struct closeSignal {
	std::mutex mu;
	std::condition_variable cv;
	bool closed = false;

	void close() {
		{
			std::lock_guard<std::mutex> lk(mu);
			closed = true;
		}
		cv.notify_all();
	}
	bool isClosed() {
		std::lock_guard<std::mutex> lk(mu);
		return closed;
	}
	void wait() {
		std::unique_lock<std::mutex> lk(mu);
		cv.wait(lk, [&] { return closed; });
	}
};

} // namespace detail

// goPanic — a throw carrying a panic's %v text. Go `panic(v)` is recoverable
// via recover() (and here via Server::recover), unlike tscUnreachable which
// is a hard exit — panic sites use this.
struct goPanic : std::exception {
	std::string value;
	explicit goPanic(std::string v) : value(std::move(v)) {}
	const char* what() const noexcept override { return value.c_str(); }
};

// errorIsCode — errors.Is(err, lsproto.ErrorCodeX): the chain wraps the code
// as *ErrorCodeError (the C++ form of ErrorCode-as-error).
inline bool errorIsCode(const gostd::Error& err, lsproto::ErrorCode code) {
	if (err == nullptr) {
		return false;
	}
	if (auto* e = gostd::errorAs<lsproto::ErrorCodeError*>(err)) {
		return e->code == code;
	}
	return false;
}

// errorCodeAs — errors.AsType[lsproto.ErrorCode]: the wrapped code, if any.
inline std::optional<lsproto::ErrorCode> errorCodeAs(const gostd::Error& err) {
	if (err == nullptr) {
		return std::nullopt;
	}
	if (auto* e = gostd::errorAs<lsproto::ErrorCodeError*>(err)) {
		return e->code;
	}
	return std::nullopt;
}

// errorf2 — fmt.Errorf("%w: %w", code, err) shorthand used throughout.
inline gostd::Error wrapCodeError(lsproto::ErrorCode code,
                                  const gostd::Error& err) {
	return gostd::errorf("%w: %w", {gostd::fmtArg(lsproto::errorCodeErr(code)),
	                                gostd::fmtArg(err)});
}

// formatGoDuration — time.Duration.String() for log lines.
std::string formatGoDuration(gostd::Duration d);

// ---------------------------------------------------------------------------
// dynamic_queue.go — port of the whole file.
// ---------------------------------------------------------------------------

// dynamicQueue — dynamic_queue.go:17. A state machine where each state is a
// channel, "idle" or "ready". Only one caller ever owns the state at a time:
// Put takes it from either channel, appends, and returns it on "ready"; Get
// waits for "ready", pops one item, and returns the state to "idle" when
// empty. Any method can be cancelled while waiting for the state.
template <class T>
class dynamicQueue {
	enum class Loc : char { idle, ready, taken };
	struct State {
		std::mutex mu;
		std::condition_variable cv;
		Loc loc = Loc::idle; // newDynamicQueue: q.idle <- &dynamicQueueState{}
		std::deque<T> items;
	};
	std::shared_ptr<State> st = std::make_shared<State>();

public:
	dynamicQueue() = default;

	// Put — dynamic_queue.go:35.
	gostd::Error Put(const gostd::Context& ctx, const T& item) {
		if (auto err = gostd::ctxErr(ctx); err != nullptr) {
			return err;
		}
		std::unique_lock<std::mutex> lk(st->mu);
		// getAny — the state arrives on whichever channel holds it.
		while (st->loc == Loc::taken) {
			if (waitForCtx(st, ctx, lk)) {
				return gostd::ctxErr(ctx);
			}
		}
		st->loc = Loc::taken;
		st->items.push_back(item); // state.items = append(state.items, item)
		st->loc = Loc::ready;      // q.ready <- state
		lk.unlock();
		st->cv.notify_all();
		return nullptr;
	}

	// Get — dynamic_queue.go:50.
	std::pair<std::optional<T>, gostd::Error> Get(const gostd::Context& ctx) {
		if (auto err = gostd::ctxErr(ctx); err != nullptr) {
			return {std::nullopt, err};
		}
		std::unique_lock<std::mutex> lk(st->mu);
		// getReady — wait for the state on the ready channel.
		while (st->loc != Loc::ready) {
			if (waitForCtx(st, ctx, lk)) {
				return {std::nullopt, gostd::ctxErr(ctx)};
			}
		}
		st->loc = Loc::taken;
		T item = std::move(st->items.front());
		st->items.pop_front();
		if (st->items.empty()) {
			st->loc = Loc::idle; // q.idle <- state
		} else {
			st->loc = Loc::ready; // q.ready <- state
		}
		lk.unlock();
		st->cv.notify_all();
		return {std::move(item), nullptr};
	}

	// --- internal test hooks (dynamic_queue_test.go drives the channel
	// machine directly; Go's `q.getAny(ctx)` / `q.idle <- state`) ---

	// acquireStateForTest — getAny: wait for the state on either channel
	// and take it. While held, Put/Get wait.
	gostd::Error acquireStateForTest(const gostd::Context& ctx) {
		if (auto err = gostd::ctxErr(ctx); err != nullptr) {
			return err;
		}
		std::unique_lock<std::mutex> lk(st->mu);
		while (st->loc == Loc::taken) {
			if (waitForCtx(st, ctx, lk)) {
				return gostd::ctxErr(ctx);
			}
		}
		st->loc = Loc::taken;
		return nullptr;
	}

	// releaseStateToIdleForTest — `q.idle <- state`.
	void releaseStateToIdleForTest() {
		{
			std::lock_guard<std::mutex> lk(st->mu);
			st->loc = Loc::idle;
		}
		st->cv.notify_all();
	}

private:
	// Waits on the state cv (woken by another method returning the state, or
	// by the ctx watcher); returns true when ctx was cancelled — Go's
	// `case <-ctx.Done()` select arm. The watcher holds the shared state so a
	// late cancellation cannot touch freed memory.
	static bool waitForCtx(const std::shared_ptr<State>& st,
	                       const gostd::Context& ctx,
	                       std::unique_lock<std::mutex>& lk) {
		auto disarm = gostd::contextAfterFunc(ctx, [st] {
			std::lock_guard<std::mutex> g(st->mu);
			st->cv.notify_all();
		});
		st->cv.wait(lk);
		disarm();
		return gostd::ctxErr(ctx) != nullptr;
	}
};

// ---------------------------------------------------------------------------
// server.go:102 pendingClientRequest / Reader / Writer / lspReader /
// lspWriter / messageMarshalError.
// ---------------------------------------------------------------------------

struct pendingClientRequest {
	std::shared_ptr<lsproto::RequestMessage> req;
	std::function<void()> cancel;
};

// pendingServerRequest — the `chan *lsproto.ResponseMessage` (capacity 1)
// registered in pendingServerRequests. send is the buffered `ch <- resp`;
// recv waits on the channel or ctx (Go `select`).
struct pendingServerRequest {
	struct State {
		std::mutex mu;
		std::condition_variable cv;
		std::shared_ptr<lsproto::ResponseMessage> resp;
		bool hasResp = false;
		bool closed = false;
	};
	std::shared_ptr<State> st = std::make_shared<State>();

	void send(std::shared_ptr<lsproto::ResponseMessage> r) {
		{
			std::lock_guard<std::mutex> lk(st->mu);
			st->resp = std::move(r);
			st->hasResp = true;
		}
		st->cv.notify_all();
	}
	void close() {
		{
			std::lock_guard<std::mutex> lk(st->mu);
			st->closed = true;
		}
		st->cv.notify_all();
	}
	std::pair<std::shared_ptr<lsproto::ResponseMessage>, bool> recv(
		const gostd::Context& ctx) {
		auto st = this->st;
		std::unique_lock<std::mutex> lk(st->mu);
		auto disarm = gostd::contextAfterFunc(ctx, [st] {
			std::lock_guard<std::mutex> g(st->mu);
			st->cv.notify_all();
		});
		st->cv.wait(lk, [&] {
			return st->hasResp || st->closed ||
			       gostd::ctxErr(ctx) != nullptr;
		});
		disarm();
		if (st->hasResp) {
			return {std::move(st->resp), true};
		}
		return {nullptr, false}; // closed channel / ctx done
	}
};

// Reader — server.go:107.
struct Reader {
	virtual ~Reader() = default;
	virtual std::pair<std::shared_ptr<lsproto::Message>, gostd::Error>
	Read() = 0;
};

// Writer — server.go:111.
struct Writer {
	virtual ~Writer() = default;
	virtual gostd::Error Write(
		const std::shared_ptr<lsproto::Message>& msg) = 0;
};

// lspReader — server.go:115. keepAlive pins the underlying io.Reader for the
// shared_ptr overloads of ToReader/ToWriter (Go: the GC keeps it reachable
// through the reader chain).
struct lspReader : Reader {
	lsproto::BaseReader r;
	std::shared_ptr<gostd::io::Reader> keepAlive;
	explicit lspReader(gostd::io::Reader* in) : r(lsproto::NewBaseReader(in)) {}
	explicit lspReader(std::shared_ptr<gostd::io::Reader> in)
	    : r(lsproto::NewBaseReader(in.get())), keepAlive(std::move(in)) {}
	std::pair<std::shared_ptr<lsproto::Message>, gostd::Error> Read() override;
};

// lspWriter — server.go:119.
struct lspWriter : Writer {
	lsproto::BaseWriter w;
	std::shared_ptr<gostd::io::Writer> keepAlive;
	explicit lspWriter(gostd::io::Writer* out) : w(lsproto::NewBaseWriter(out)) {}
	explicit lspWriter(std::shared_ptr<gostd::io::Writer> out)
	    : w(lsproto::NewBaseWriter(out.get())), keepAlive(std::move(out)) {}
	gostd::Error Write(const std::shared_ptr<lsproto::Message>& msg) override;
};

// messageMarshalError — server.go:123.
struct messageMarshalError : gostd::ErrObj {
	gostd::Error err;
	// Owned by the ErrObj so an ErrorCodeError* obtained through
	// errors.As stays valid for the error's lifetime (the Go
	// value-type equivalent is GC-pinned by the error chain).
	gostd::Error codeErr;
	explicit messageMarshalError(gostd::Error e)
	    : err(std::move(e)),
	      codeErr(lsproto::errorCodeErr(lsproto::ErrorCodeInternalError)) {}
	std::string Error() const override {
		return "failed to marshal message: " +
		       (err ? err->Error() : "<nil>");
	}
	std::vector<gostd::Error> unwrap() const override {
		return {codeErr, err};
	}
};

// ToReader — server.go:150.
std::shared_ptr<Reader> ToReader(gostd::io::Reader* r);
std::shared_ptr<Reader> ToReader(std::shared_ptr<gostd::io::Reader> r);
// ToWriter — server.go:162.
std::shared_ptr<Writer> ToWriter(gostd::io::Writer* w);
std::shared_ptr<Writer> ToWriter(std::shared_ptr<gostd::io::Writer> w);

// userFacingRequestFailedError — server.go:1103. A string-typed error whose
// Unwrap reports RequestFailed; the message is shown to the user.
struct userFacingRequestFailedError : gostd::ErrObj {
	std::string message;
	// Owned by the ErrObj: errors.As returns a raw pointer into
	// the unwrap() chain, so a freshly-created ErrorCodeError
	// would die with unwrap()'s return vector (Go pins it via GC).
	gostd::Error codeErr;
	explicit userFacingRequestFailedError(std::string msg)
	    : message(std::move(msg)),
	      codeErr(lsproto::errorCodeErr(lsproto::ErrorCodeRequestFailed)) {}
	std::string Error() const override { return message; }
	std::vector<gostd::Error> unwrap() const override { return {codeErr}; }
};

// ---------------------------------------------------------------------------
// logger.go — port of the whole file.
// ---------------------------------------------------------------------------

// maxVerbosityForMessageType — logger.go:28.
lsproto::LogVerbosity maxVerbosityForMessageType(lsproto::MessageType msgType);
// isValidLogVerbosity — logger.go:44.
bool isValidLogVerbosity(lsproto::LogVerbosity v);

// lspAnyToJsonAny / jsonAnyRepr — helpers shared by RequestConfiguration
// (lsp_server.cpp) and handleDidChangeWorkspaceConfiguration
// (lsp_handlers.cpp). lspAnyToJsonAny converts an lsproto::LSPAny tree into
// lsutil::JsonAny for lsutil::ParseUserPreferences; jsonAnyRepr formats a
// JsonAny like Go's `%+v` for logging.
ls::lsutil::JsonAny lspAnyToJsonAny(const lsproto::LSPAny& v);
std::string jsonAnyRepr(const ls::lsutil::JsonAny& v);

class Server;

// logger — logger.go:13. Implements project/logging's Logger interface plus
// the lsp-only SetVerbosity/IsTracing. Go's nil-receiver guards are preserved
// as null checks on the server member (the field is always set post-ctor,
// making the nil case unreachable — same as Go after NewServer).
class logger : public tsc::logging::Logger {
public:
	// Keep the base-class call-shape conveniences visible.
	using tsc::logging::Logger::Error;
	using tsc::logging::Logger::Errorf;
	using tsc::logging::Logger::Warn;
	using tsc::logging::Logger::Warnf;
	using tsc::logging::Logger::Info;
	using tsc::logging::Logger::Infof;
	using tsc::logging::Logger::Log;
	using tsc::logging::Logger::Logf;

	Server* server;                 // logger.go:14
	std::mutex mu;                  // guards verbosity (logger.go:15)
	lsproto::LogVerbosity verbosity; // logger.go:16

	explicit logger(Server* s);

	// sendLogMessage — logger.go:48.
	void sendLogMessage(lsproto::MessageType msgType, const std::string& message);

	// logging.Logger methods (logger.go:78-184).
	void Log(const std::vector<gostd::fmtArg>& msg) override;
	void Logf(std::string_view format,
	          const std::vector<gostd::fmtArg>& args) override;
	tsc::logging::Logger* Verbose() override;
	bool IsVerbose() override;
	void SetVerbose(bool enabled) override;
	void Error(const std::vector<gostd::fmtArg>& msg) override;
	void Errorf(std::string_view format,
	            const std::vector<gostd::fmtArg>& args) override;
	void Warn(const std::vector<gostd::fmtArg>& msg) override;
	void Warnf(std::string_view format,
	           const std::vector<gostd::fmtArg>& args) override;
	void Info(const std::vector<gostd::fmtArg>& msg) override;
	void Infof(std::string_view format,
	           const std::vector<gostd::fmtArg>& args) override;

	// IsTracing — logger.go:126.
	bool IsTracing();
	// SetVerbosity — logger.go:135.
	void SetVerbosity(lsproto::LogVerbosity v);
};

// ---------------------------------------------------------------------------
// progress.go — port of the whole file.
// ---------------------------------------------------------------------------

// progressEvent — progress.go:13.
struct progressEvent {
	const DiagnosticMessage* message = nullptr;
	std::vector<gostd::fmtArg> args;
	bool finish = false;
};

// progressReporter — progress.go:22. Abstracts the LSP transport operations
// needed by projectLoadingProgress so the progress logic can be tested
// without a full Server instance.
struct progressReporter {
	virtual ~progressReporter() = default;
	// done — the "channel" closed when the server is shutting down (the
	// server's background context).
	virtual gostd::Context done() = 0;
	// localize converts a diagnostic message to a display string.
	virtual std::string localize(const DiagnosticMessage* msg,
	                             const std::vector<gostd::fmtArg>& args) = 0;
	// createWorkDoneProgress asks the client to create a progress token.
	virtual void createWorkDoneProgress(const std::string& token) = 0;
	// sendProgress sends a $/progress notification.
	virtual void sendProgress(
		const std::string& token,
		lsproto::WorkDoneProgressBeginOrReportOrEnd value) = 0;
};

// serverProgressReporter — progress.go:34. Holds the Server weakly: the
// progress goroutine (which keeps the reporter alive) must not keep the
// Server alive — Server->projectProgress->reporter->Server would be a
// leak cycle. In Go, teardown drops progress events via the done()
// channel; here an expired weak_ptr is the same signal: every method
// becomes a no-op once the Server is gone.
struct serverProgressReporter : progressReporter {
	std::weak_ptr<Server> server;
	explicit serverProgressReporter(std::weak_ptr<Server> s)
	    : server(std::move(s)) {}
	gostd::Context done() override;
	std::string localize(const DiagnosticMessage* msg,
	                     const std::vector<gostd::fmtArg>& args) override;
	void createWorkDoneProgress(const std::string& token) override;
	void sendProgress(
		const std::string& token,
		lsproto::WorkDoneProgressBeginOrReportOrEnd value) override;
};

// projectLoadingProgress — progress.go:70. Manages LSP WorkDoneProgress
// indicators for long-running operations: a single persistent goroutine
// processes start/finish events, maintains a ref-counted map of active
// operations, and sends progress messages in order. The indicator is not
// shown until progressDelay has elapsed since the first start event.
class projectLoadingProgress
    : public std::enable_shared_from_this<projectLoadingProgress> {
	// ch — progress.go:72: a chan of capacity 64, plus the delay timer's
	// pending-fire flag, all under one mutex.
	struct chanState {
		std::mutex mu;
		std::condition_variable notFull;   // send side: size < 64
		std::condition_variable cv;        // run loop: events/timer
		std::deque<progressEvent> queue;
		bool delayFiredPending = false;
		// inFlight — events popped but not yet handled; lets internal
		// tests wait for quiescence like synctest.Wait() does in Go.
		int inFlight = 0;
		// runExited — set when the run loop returns (Shutdown* tests wait
		// on it so the detached thread can't outlive the object).
		bool runExited = false;
	};

	std::shared_ptr<progressReporter> reporter;
	gostd::Duration delay;
	std::shared_ptr<chanState> st = std::make_shared<chanState>();
	// delay timer state owned by run(): armed flag shared with the timer
	// thread so stop() can retract a pending fire.
	std::shared_ptr<std::atomic<bool>> delayArmed;

public:
	projectLoadingProgress(std::shared_ptr<progressReporter> reporter,
	                       gostd::Duration delay);

	// startRun — `go p.run()`: spawn the persistent event loop. Called by
	// the factory after make_shared so the thread can hold a shared_ptr
	// (Go's GC keeps p alive for the goroutine's lifetime).
	void startRun();

	// start — progress.go:90.
	void start(const DiagnosticMessage* message,
	           std::vector<gostd::fmtArg> args);
	// finish — progress.go:99.
	void finish(const DiagnosticMessage* message,
	            std::vector<gostd::fmtArg> args);

	// --- internal test hooks (progress_test.go drives the channel and
	// goroutine directly; Go's synctest.Wait / direct `p.ch <- ev`) ---

	// waitIdleForTest — synctest.Wait(): blocks until every queued event
	// and pending delay-fire has been handled by the run goroutine.
	void waitIdleForTest();
	// fillChannelForTest — `p.ch <- ev` × cap(p.ch), bypassing the
	// done() select (ShutdownDuringStartAndFinish fills the channel so
	// subsequent start/finish take the done() path).
	void fillChannelForTest();
	// waitRunExitForTest — blocks until the run goroutine has exited.
	void waitRunExitForTest();

private:
	// run — progress.go:110.
	void run();
	// beginOrReport — progress.go:200.
	bool beginOrReport(const std::string& token, const std::string& text,
	                   bool begun);
	// enqueue — the `p.ch <- event` select against done().
	void enqueue(const progressEvent& ev);
};

// newProjectLoadingProgress — progress.go:76.
std::shared_ptr<projectLoadingProgress>
newProjectLoadingProgress(Server* server, gostd::Duration delay);
// newProjectLoadingProgressFromReporter — progress.go:80.
std::shared_ptr<projectLoadingProgress> newProjectLoadingProgressFromReporter(
	std::shared_ptr<progressReporter> reporter, gostd::Duration delay);

// ---------------------------------------------------------------------------
// stack_sanitizer.go — port of the whole file.
// ---------------------------------------------------------------------------

// sanitizeStackTrace — stack_sanitizer.go:23.
std::string sanitizeStackTrace(const std::string& stack);
// writeSanitizedModuleOrPath — stack_sanitizer.go:64.
void writeSanitizedModuleOrPath(const std::string& line, std::string* result);
// defeatGenericSecretRegex — stack_sanitizer.go:19.
std::string defeatGenericSecretRegex(const std::string& s);

// ---------------------------------------------------------------------------
// server.go — Server.
// ---------------------------------------------------------------------------

// contentMapper*RegistrationID — server.go:323-350.
inline const std::string contentMapperDidOpenRegistrationID = "content-mapper-did-open";
inline const std::string contentMapperDidChangeRegistrationID = "content-mapper-did-change";
inline const std::string contentMapperDidCloseRegistrationID = "content-mapper-did-close";
inline const std::string contentMapperDiagnosticRegistrationID = "content-mapper-diagnostic";
inline const std::string contentMapperHoverRegistrationID = "content-mapper-hover";
inline const std::string contentMapperSignatureHelpRegistrationID = "content-mapper-signature-help";
inline const std::string contentMapperDefinitionRegistrationID = "content-mapper-definition";
inline const std::string contentMapperTypeDefinitionRegistrationID = "content-mapper-type-definition";
inline const std::string contentMapperImplementationRegistrationID = "content-mapper-implementation";
inline const std::string contentMapperReferencesRegistrationID = "content-mapper-references";
inline const std::string contentMapperDocumentHighlightRegistrationID = "content-mapper-document-highlight";
inline const std::string contentMapperCompletionRegistrationID = "content-mapper-completion";
inline const std::string contentMapperRenameRegistrationID = "content-mapper-rename";
inline const std::string contentMapperSemanticTokensRegistrationID = "content-mapper-semantic-tokens";
inline const std::string contentMapperDocumentSymbolRegistrationID = "content-mapper-document-symbol";
inline const std::string contentMapperFoldingRangeRegistrationID = "content-mapper-folding-range";
inline const std::string contentMapperSelectionRangeRegistrationID = "content-mapper-selection-range";
inline const std::string contentMapperInlayHintRegistrationID = "content-mapper-inlay-hint";
inline const std::string contentMapperCodeLensRegistrationID = "content-mapper-code-lens";
inline const std::string contentMapperCodeActionRegistrationID = "content-mapper-code-action";
inline const std::string contentMapperFormattingRegistrationID = "content-mapper-formatting";
inline const std::string contentMapperRangeFormattingRegistrationID = "content-mapper-range-formatting";
inline const std::string contentMapperOnTypeFormattingRegistrationID = "content-mapper-on-type-formatting";
inline const std::string contentMapperLinkedEditingRegistrationID = "content-mapper-linked-editing";
inline const std::string contentMapperCallHierarchyRegistrationID = "content-mapper-call-hierarchy";
inline const std::string contentMapperWillRenameFilesRegistrationID = "content-mapper-will-rename-files";

// supportedCodeActionKinds — server.go:352.
std::vector<lsproto::CodeActionKind> supportedCodeActionKinds();

// fileRenameFilters — server.go:90.
const std::vector<std::shared_ptr<lsproto::FileOperationFilter>>&
fileRenameFilters();

// ServerOptions — server.go:42.
struct ServerOptions {
	std::shared_ptr<Reader> In;
	std::shared_ptr<Writer> Out;
	gostd::io::Writer* Err = nullptr;

	std::string Cwd;
	std::shared_ptr<vfs::FS> FS;
	std::string DefaultLibraryPath;
	std::string TypingsLocation;
	std::shared_ptr<project::ParseCache> ParseCache;
	std::function<std::pair<std::vector<uint8_t>, gostd::Error>(
		const std::string& cwd, const std::vector<std::string>& args)>
		NpmInstall;
	// Spawn launches a child process, returning its stdio as an
	// io.ReadWriteCloser (Read is its stdout, Write is its stdin). It is nil
	// when the host cannot spawn processes. Currently used for content
	// mappers.
	std::function<std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>,
	                        gostd::Error>(
		const std::vector<std::string>& command, const std::string& dir,
		gostd::io::Writer* stderr_)>
		Spawn;
	gostd::Duration ProgressDelay{}; // delay before showing progress UI
	std::function<void(int)> SetParentProcessID;
};

// contentMapperFallbackResponse — server.go:1198.
std::pair<lsproto::AnyValue, bool>
contentMapperFallbackResponse(const lsproto::Method& method,
                              const gostd::Error& err);

// handlerMap — server.go:1227. Maps LSP method to a handler; the handler
// runs the synchronous work and returns async work to run after it.
using handlerMap = std::unordered_map<
	lsproto::Method,
	std::function<std::pair<std::function<gostd::Error()>, gostd::Error>(
		Server*, gostd::Context, std::shared_ptr<lsproto::RequestMessage>)>>;


// generateDiagnosticDiffString — server.go:1896.
std::string generateDiagnosticDiffString(
	const std::vector<std::shared_ptr<lsproto::Diagnostic>>& missingFromPre,
	const std::vector<std::shared_ptr<lsproto::Diagnostic>>& missingFromPost,
	const std::function<std::string(const std::shared_ptr<lsproto::Diagnostic>&)>&
	    stringifier);

// parseContentMapperContributions — server.go:2461.
std::pair<project::ContentMapperContributions, gostd::Error>
parseContentMapperContributions(
	const std::vector<std::shared_ptr<lsproto::ContentMapperContribution>>&
	    values);

// isValidContributedContentMapperExtension — server.go:2524.
bool isValidContributedContentMapperExtension(std::string_view extension);

// valueOrZero — server.go:2533.
template <class T>
T valueOrZero(const std::shared_ptr<T>& value) {
	if (value == nullptr) {
		return T{};
	}
	return *value;
}
template <class T>
T valueOrZero(const std::optional<T>& value) {
	if (!value.has_value()) {
		return T{};
	}
	return *value;
}

// Server — server.go:171. Implements project.Client and ata.NpmExecutor.
class Server : public project::Client,
               public tsc::ata::NpmExecutor,
               public std::enable_shared_from_this<Server> {
public:
	std::shared_ptr<Reader> r;   // server.go:172
	std::shared_ptr<Writer> w;   // server.go:173
	gostd::Context backgroundCtx; // server.go:174

	gostd::io::Writer* stderr_ = nullptr; // server.go:176 (stderr_: CRT macro dodge)

	std::shared_ptr<lsp::logger> logger;       // server.go:178
	std::atomic<bool> initStarted{false};      // server.go:179
	std::atomic<int32_t> clientSeq{0};         // server.go:180
	dynamicQueue<std::shared_ptr<lsproto::RequestMessage>>
		requestQueue;                          // server.go:181
	dynamicQueue<std::shared_ptr<lsproto::Message>>
		outgoingQueue;                         // server.go:182
	std::unordered_map<jsonrpc::ID, pendingClientRequest, jsonrpc::IDHash>
		pendingClientRequests;                 // server.go:183
	std::mutex pendingClientRequestsMu;        // server.go:184
	std::unordered_map<jsonrpc::ID,
	                   std::shared_ptr<pendingServerRequest>,
	                   jsonrpc::IDHash>
		pendingServerRequests;                 // server.go:185
	std::mutex pendingServerRequestsMu;        // server.go:186

	std::string cwd;                           // server.go:188
	std::shared_ptr<vfs::FS> fs;               // server.go:189
	std::string defaultLibraryPath;            // server.go:190
	std::string typingsLocation;               // server.go:191

	std::shared_ptr<lsproto::InitializeParams>
		initializeParams;                      // server.go:193
	std::shared_ptr<lsproto::InitializationOptions>
		initializationOptions;                 // server.go:194
	lsproto::ResolvedClientCapabilities
		clientCapabilities;                    // server.go:195
	lsproto::PositionEncodingKind positionEncoding; // server.go:196
	mutable std::shared_mutex localeMu;        // server.go:197
	tsc::locale::Locale locale;                // server.go:198
	// initLocale is the locale resolved from the initialize request; it is
	// used as the fallback when the user's locale preference is "auto".
	tsc::locale::Locale initLocale;            // server.go:201

	bool watchEnabled = false;                 // server.go:203
	bool telemetryEnabled = false;             // server.go:204
	std::atomic<uint32_t> watcherID{0};        // server.go:205
	collections::SyncSet<project::WatcherID> watchers; // server.go:206

	// contentMapperRegistrationMu serializes RegisterContentMapperExtensions
	// so the method is correctly synchronized on its own rather than relying
	// on callers to serialize it. It guards
	// contentMapperExtensionsRegistered and the ordered unregister/register
	// requests the method sends.
	std::mutex contentMapperRegistrationMu;    // server.go:211
	// contentMapperExtensionsRegistered records whether a content mapper
	// text document sync registration is currently active with the client,
	// so it can be replaced or removed.
	bool contentMapperExtensionsRegistered = false; // server.go:214
	// builtinWatcher is non-nil when the server is running its own
	// in-process file watcher instead of using LSP-based watching. It is
	// enabled when the client lacks DynamicRegistration for
	// workspace/didChangeWatchedFiles and the builtin watcher backend
	// supports efficient recursive watching (Windows or FSEvents).
	std::shared_ptr<lspwatcher::Watcher> builtinWatcher; // server.go:220

	std::atomic<int64_t> lastRequestTimeMs{0}; // server.go:222

	project::Session* session = nullptr;       // server.go:224

	// apiSessions holds active API sessions keyed by their ID.
	std::unordered_map<std::string, std::shared_ptr<api::Session>>
		apiSessions;                           // server.go:227
	std::mutex apiSessionsMu;                  // server.go:228

	// Test options for initializing session.
	std::shared_ptr<project::Client> client;   // server.go:231

	// sessionInitOwned keeps the shared objects handed to
	// project::SessionInit (raw pointers) alive for the session's lifetime.
	std::shared_ptr<project::SessionOptions> sessionInitOptions;
	std::shared_ptr<contentmapper::Spawner> sessionInitSpawner;
	std::shared_ptr<contentmapper::Logger> sessionInitContentMapperLogger;

	// initComplete is closed when handleInitialized completes. Used by tests
	// to wait for full initialization.
	std::shared_ptr<detail::closeSignal> initComplete; // server.go:235

	// !!! temporary; remove when we have
	// `handleDidChangeConfiguration`/implicit project config support
	CompilerOptions* compilerOptionsForInferredProjects = nullptr; // :238
	// parseCache can be passed in so separate tests can share ASTs.
	std::shared_ptr<project::ParseCache> parseCache; // server.go:240

	std::function<std::pair<std::vector<uint8_t>, gostd::Error>(
		const std::string&, const std::vector<std::string>&)>
		npmInstall;                              // server.go:242
	std::function<std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>,
	                        gostd::Error>(
		const std::vector<std::string>&, const std::string&,
		gostd::io::Writer*)>
		spawn;                                   // server.go:243

	pprof::CPUProfiler cpuProfiler;            // server.go:245

	gostd::Duration progressDelay{};           // server.go:247
	std::shared_ptr<projectLoadingProgress> projectProgress; // :248

	std::function<void(int)> startWatchdog;    // server.go:250

	lsproto::DiagnosticFlakeLogLevel flakeLogging =
		lsproto::DiagnosticFlakeLogLevelOff;   // server.go:252

	explicit Server(const ServerOptions& opts); // NewServer — server.go:60
	~Server() override = default;

	// Session — server.go:255.
	project::Session* Session() const { return session; }
	// InitComplete — server.go:260. The channel is closed when the server
	// has finished processing the initialized notification, including the
	// initial configuration exchange with the client.
	std::shared_ptr<detail::closeSignal> InitComplete() const {
		return initComplete;
	}

	// --- project.Client implementation (server.go:263-799) ---
	gostd::Error WatchFiles(
		const gostd::Context& ctx, project::WatcherID id,
		const std::vector<lsproto::FileSystemWatcher*>& watchers) override;
	gostd::Error UnwatchFiles(const gostd::Context& ctx,
	                        project::WatcherID id) override;
	gostd::Error RegisterContentMapperExtensions(
		const gostd::Context& ctx,
		const std::vector<std::string>& extensions) override;
	gostd::Error RefreshDiagnostics(const gostd::Context& ctx) override;
	gostd::Error PublishDiagnostics(
		const gostd::Context& ctx,
		lsproto::PublishDiagnosticsParams* params) override;
	gostd::Error RefreshInlayHints(const gostd::Context& ctx) override;
	gostd::Error RefreshCodeLens(const gostd::Context& ctx) override;
	void ProgressStart(const DiagnosticMessage* message,
	                   const std::vector<std::string>& args) override;
	void ProgressFinish(const DiagnosticMessage* message,
	                    const std::vector<std::string>& args) override;
	gostd::Error SendTelemetry(
		const gostd::Context& ctx,
		lsproto::TelemetryEvent telemetry) override;
	bool IsActive() override;
	void SetLocale(const std::string& localeString) override;
	tsc::locale::Locale GetLocale() override;

	// supportsContentMapperRegistration — server.go:362.
	bool supportsContentMapperRegistration(const std::string& id) const;

	// RequestConfiguration — server.go:801.
	std::pair<ls::lsutil::UserPreferences, gostd::Error>
	RequestConfiguration(gostd::Context ctx);

	// Run — server.go:859.
	gostd::Error Run(gostd::Context ctx);
	// readLoop — server.go:883.
	gostd::Error readLoop(gostd::Context ctx);
	// cancelRequest — server.go:954.
	void cancelRequest(lsproto::IntegerOrString rawID);
	// read — server.go:964.
	std::pair<std::shared_ptr<lsproto::Message>, gostd::Error> read();
	// dispatchLoop — server.go:968.
	gostd::Error dispatchLoop(gostd::Context ctx);
	// writeLoop — server.go:1029.
	gostd::Error writeLoop(gostd::Context ctx);

	// sendClientRequest — server.go:1053. Only safe from the async portion
	// of a request handler (otherwise a deadlock can occur).
	template <class Req, class Resp>
	std::pair<Resp, gostd::Error> sendClientRequest(
		const gostd::Context& ctx,
		const lsproto::RequestInfo<Req, Resp>& info, const Req& params) {
		auto id = std::make_shared<jsonrpc::ID>(jsonrpc::NewIDString(
		    gostd::sprintf("ts%d", {gostd::fmtArg(clientSeq.fetch_add(1) + 1)})));
		auto req = info.NewRequestMessage(id, params);

		auto responseChan = std::make_shared<pendingServerRequest>();
		{
			std::lock_guard<std::mutex> lk(pendingServerRequestsMu);
			pendingServerRequests[*id] = responseChan;
		}
		struct cleanup {
			Server* s;
			jsonrpc::ID id;
			~cleanup() {
				std::lock_guard<std::mutex> lk(s->pendingServerRequestsMu);
				auto it = s->pendingServerRequests.find(id);
				if (it != s->pendingServerRequests.end()) {
					it->second->close();
					s->pendingServerRequests.erase(it);
				}
			}
		} guard{this, *id};

		if (auto err = send(req->toMessage()); err != nullptr) {
			return {Resp{}, err};
		}

		// select { <-ctx.Done() | resp := <-responseChan }
		auto result = responseChan->recv(ctx);
		if (!result.second) {
			// ctx.Done() fired (the channel is otherwise only closed by the
			// destructor above — Go blocks forever in that case).
			return {Resp{}, gostd::ctxErr(ctx)};
		}
		auto& resp = result.first;
		if (resp->Error != nullptr) {
			return {Resp{}, gostd::errorf("request failed: %s",
			                              {gostd::fmtArg(resp->Error->String())})};
		}
		return info.UnmarshalResult(resp->Result);
	}

	// sendClientRequestFireAndForget — server.go:1090.
	template <class Req, class Resp>
	gostd::Error sendClientRequestFireAndForget(
		const lsproto::RequestInfo<Req, Resp>& info, const Req& params) {
		auto id = std::make_shared<jsonrpc::ID>(jsonrpc::NewIDString(
		    gostd::sprintf("ts%d", {gostd::fmtArg(clientSeq.fetch_add(1) + 1)})));
		auto req = info.NewRequestMessage(id, params);
		return send(req->toMessage());
	}

	// sendResult — server.go:1096.
	template <class Resp>
	gostd::Error sendResult(const std::shared_ptr<jsonrpc::ID>& id,
	                        const Resp& result) {
		auto resp = std::make_shared<lsproto::ResponseMessage>();
		resp->ID = id;
		resp->Result = lsproto::AnyValue::of(result);
		return sendResponse(resp);
	}

	// sendError — server.go:1108.
	gostd::Error sendError(const std::shared_ptr<jsonrpc::ID>& id,
	                       gostd::Error err);
	// sendNotification — server.go:1129.
	template <class Params>
	gostd::Error sendNotification(
		const lsproto::NotificationInfo<Params>& info, const Params& params) {
		return send(info.NewNotificationMessage(params)->toMessage());
	}
	// sendResponse — server.go:1133.
	gostd::Error sendResponse(
		const std::shared_ptr<lsproto::ResponseMessage>& resp);
	// send — server.go:1138.
	gostd::Error send(const std::shared_ptr<lsproto::Message>& msg);

	// handleRequestOrNotification — server.go:1144.
	std::pair<std::function<gostd::Error()>, gostd::Error>
	handleRequestOrNotification(gostd::Context ctx,
	                            const std::shared_ptr<lsproto::RequestMessage>&
	                                req);

	// recover — server.go:1477. Called from a catch block; inspects
	// std::current_exception() (null outside unwinding, like Go's recover()
	// outside a panic).
	void recover_(const std::shared_ptr<lsproto::RequestMessage>& req);

	// getLanguageServiceAndCrossProjectOrchestrator — server.go:1468.
	std::tuple<ls::LanguageService*,
	           std::shared_ptr<ls::CrossProjectOrchestrator>, gostd::Error>
	getLanguageServiceAndCrossProjectOrchestrator(
		gostd::Context ctx, lsproto::DocumentUri uri,
		const std::shared_ptr<lsproto::RequestMessage>& req);

	// handlers — server.go:1229 (sync.OnceValue). A static member so the
	// &ls::LanguageService::Provide* pointers it registers see the friend
	// grant on LanguageService.
	static const handlerMap& handlers();

	// --- request handlers (server.go:1501-2539) ---
	std::pair<lsproto::InitializeResponse, gostd::Error> handleInitialize(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::InitializeParams>& params,
		const std::shared_ptr<lsproto::RequestMessage>& req);
	gostd::Error handleInitialized(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::InitializedParams>& params);
	std::pair<lsproto::ShutdownResponse, gostd::Error> handleShutdown(
		gostd::Context ctx, const lsproto::NoParams& params,
		const std::shared_ptr<lsproto::RequestMessage>& req);
	gostd::Error handleExit(gostd::Context ctx, const lsproto::NoParams& params);
	gostd::Error handleDidChangeWorkspaceConfiguration(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::DidChangeConfigurationParams>& params);
	gostd::Error handleDidOpen(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::DidOpenTextDocumentParams>& params);
	gostd::Error handleDidChange(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::DidChangeTextDocumentParams>& params);
	gostd::Error handleDidSave(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::DidSaveTextDocumentParams>& params);
	gostd::Error handleDidClose(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::DidCloseTextDocumentParams>& params);
	gostd::Error handleDidChangeWatchedFiles(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::DidChangeWatchedFilesParams>& params);
	gostd::Error handleSetTrace(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::SetTraceParams>& params);
	gostd::Error handleSetLogVerbosity(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::SetLogVerbosityParams>& params);
	std::pair<lsproto::DocumentDiagnosticResponse, gostd::Error>
	handleDocumentDiagnostic(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::DocumentDiagnosticParams>& params);
	std::pair<lsproto::HoverResponse, gostd::Error> handleHover(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::HoverParams>& params);
	std::pair<lsproto::PrepareRenameResponse, gostd::Error> handlePrepareRename(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::PrepareRenameParams>& params);
	std::pair<lsproto::RenameResponse, gostd::Error> handleRename(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::RenameParams>& params,
		const std::shared_ptr<lsproto::RequestMessage>& req);
	std::pair<lsproto::WillRenameFilesResponse, gostd::Error>
	handleWillRenameFiles(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::RenameFilesParams>& params,
		const std::shared_ptr<lsproto::RequestMessage>& msg);
	std::pair<lsproto::WillRenameFilesResponse, gostd::Error>
	handleWillRenameFilesWorker(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::RenameFilesParams>& params,
		const std::shared_ptr<lsproto::RequestMessage>& msg,
		bool sendRenameFile);
	std::pair<lsproto::SignatureHelpResponse, gostd::Error> handleSignatureHelp(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::SignatureHelpParams>& params);
	std::pair<lsproto::FoldingRangeResponse, gostd::Error> handleFoldingRange(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::FoldingRangeParams>& params);
	std::pair<lsproto::VSOnAutoInsertResponse, gostd::Error>
	handleVSOnAutoInsert(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::VSOnAutoInsertParams>& params);
	std::pair<lsproto::LinkedEditingRangeResponse, gostd::Error>
	handleLinkedEditingRange(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::LinkedEditingRangeParams>& params);
	std::pair<lsproto::DefinitionResponse, gostd::Error> handleDefinition(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::DefinitionParams>& params);
	std::pair<lsproto::CustomTextDocumentSourceDefinitionResponse, gostd::Error>
	handleSourceDefinition(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::TextDocumentPositionParams>& params);
	std::pair<lsproto::TypeDefinitionResponse, gostd::Error>
	handleTypeDefinition(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::TypeDefinitionParams>& params);
	std::pair<lsproto::CompletionResponse, gostd::Error> handleCompletion(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::CompletionParams>& params);
	std::pair<lsproto::CompletionResolveResponse, gostd::Error>
	handleCompletionItemResolve(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::CompletionItem>& params,
		const std::shared_ptr<lsproto::RequestMessage>& reqMsg);
	std::pair<lsproto::DocumentFormattingResponse, gostd::Error>
	handleDocumentFormat(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::DocumentFormattingParams>& params);
	std::pair<lsproto::DocumentRangeFormattingResponse, gostd::Error>
	handleDocumentRangeFormat(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::DocumentRangeFormattingParams>& params);
	std::pair<lsproto::DocumentOnTypeFormattingResponse, gostd::Error>
	handleDocumentOnTypeFormat(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::DocumentOnTypeFormattingParams>& params);
	std::pair<lsproto::WorkspaceSymbolResponse, gostd::Error>
	handleWorkspaceSymbol(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::WorkspaceSymbolParams>& params,
		const std::shared_ptr<lsproto::RequestMessage>& reqMsg);
	std::pair<lsproto::DocumentSymbolResponse, gostd::Error>
	handleDocumentSymbol(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::DocumentSymbolParams>& params);
	std::pair<lsproto::DocumentHighlightResponse, gostd::Error>
	handleDocumentHighlight(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::DocumentHighlightParams>& params);
	std::pair<lsproto::CustomMultiDocumentHighlightResponse, gostd::Error>
	handleMultiDocumentHighlight(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::MultiDocumentHighlightParams>& params);
	std::pair<lsproto::SelectionRangeResponse, gostd::Error>
	handleSelectionRange(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::SelectionRangeParams>& params);
	std::pair<lsproto::CodeActionResponse, gostd::Error> handleCodeAction(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::CodeActionParams>& params);
	std::pair<lsproto::InlayHintResponse, gostd::Error> handleInlayHint(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::InlayHintParams>& params);
	std::pair<lsproto::CodeLensResponse, gostd::Error> handleCodeLens(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::CodeLensParams>& params);
	std::pair<std::shared_ptr<lsproto::CodeLens>, gostd::Error>
	handleCodeLensResolve(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::CodeLens>& codeLens,
		const std::shared_ptr<lsproto::RequestMessage>& reqMsg);
	std::pair<lsproto::CallHierarchyPrepareResponse, gostd::Error>
	handlePrepareCallHierarchy(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::CallHierarchyPrepareParams>& params);
	std::pair<lsproto::CallHierarchyIncomingCallsResponse, gostd::Error>
	handleCallHierarchyIncomingCalls(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::CallHierarchyIncomingCallsParams>& params,
		const std::shared_ptr<lsproto::RequestMessage>& reqMsg);
	std::pair<lsproto::CallHierarchyOutgoingCallsResponse, gostd::Error>
	handleCallHierarchyOutgoingCalls(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::CallHierarchyOutgoingCallsParams>& params,
		const std::shared_ptr<lsproto::RequestMessage>& reqMsg);
	std::pair<lsproto::SemanticTokensResponse, gostd::Error>
	handleSemanticTokensFull(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::SemanticTokensParams>& params);
	std::pair<lsproto::SemanticTokensRangeResponse, gostd::Error>
	handleSemanticTokensRange(
		gostd::Context ctx, ls::LanguageService* languageService,
		const std::shared_ptr<lsproto::SemanticTokensRangeParams>& params);
	std::pair<lsproto::CustomInitializeAPISessionResponse, gostd::Error>
	handleInitializeAPISession(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::InitializeAPISessionParams>& params,
		const std::shared_ptr<lsproto::RequestMessage>& req);
	std::string generateAPIPipePath();
	void removeAPISession(const std::string& id);
	// SetCompilerOptionsForInferredProjects — server.go:2363.
	void SetCompilerOptionsForInferredProjects(gostd::Context ctx,
	                                           CompilerOptions* options);
	// NpmInstall — server.go:2371 (ata.NpmExecutor).
	std::pair<std::string, gostd::Error> NpmInstall(
		const std::string& cwd,
		const std::vector<std::string>& args) override;
	// contentMapperSpawner — server.go:2377.
	std::shared_ptr<contentmapper::Spawner> contentMapperSpawner();
	// contentMapperLogger — server.go:2384.
	std::function<void(std::string_view)> contentMapperLogger();
	std::pair<lsproto::RunGCResponse, gostd::Error> handleRunGC(
		gostd::Context ctx, const lsproto::NoParams& params,
		const std::shared_ptr<lsproto::RequestMessage>& req);
	std::pair<std::shared_ptr<lsproto::ProfileResult>, gostd::Error>
	handleSaveHeapProfile(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::ProfileParams>& params,
		const std::shared_ptr<lsproto::RequestMessage>& req);
	std::pair<std::shared_ptr<lsproto::ProfileResult>, gostd::Error>
	handleSaveAllocProfile(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::ProfileParams>& params,
		const std::shared_ptr<lsproto::RequestMessage>& req);
	std::pair<lsproto::StartCPUProfileResponse, gostd::Error>
	handleStartCPUProfile(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::ProfileParams>& params,
		const std::shared_ptr<lsproto::RequestMessage>& req);
	std::pair<std::shared_ptr<lsproto::ProfileResult>, gostd::Error>
	handleStopCPUProfile(
		gostd::Context ctx, const lsproto::NoParams& params,
		const std::shared_ptr<lsproto::RequestMessage>& req);
	std::pair<lsproto::CustomProjectInfoResponse, gostd::Error>
	handleProjectInfo(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::ProjectInfoParams>& params,
		const std::shared_ptr<lsproto::RequestMessage>& req);
	std::pair<lsproto::CustomSetContentMapperContributionsResponse, gostd::Error>
	handleSetContentMapperContributions(
		gostd::Context ctx,
		const std::shared_ptr<lsproto::SetContentMapperContributionsParams>&
		    params,
		const std::shared_ptr<lsproto::RequestMessage>& req);
};

// NewServer — server.go:60.
std::shared_ptr<Server> NewServer(const ServerOptions& opts);

// crossProjectOrchestrator — server.go:1431 (declared here so the header's
// handler registrations can reference it; the class completes
// ls::CrossProjectOrchestrator — definition is in lsp_server.cpp because the
// base class lives in ls.h).

} // namespace tsc::lsp
