// Shared helpers for the libFuzzer drivers in cpp/cmd/fuzz/.
//
// The Go compiler treats panics as ordinary runtime failures: every
// "unreachable" in Go source is a `panic`/`debug.Assert`, and tscpp maps
// each to tscUnreachable() → _Exit(2). A fuzz harness must instead keep
// going, so fuzz builds define TSC_FUZZ_UNREACHABLE_THROW and every unit of
// work runs inside runOnBigStack, which catches the thrown abort and counts
// it as faithful. std::bad_alloc / std::length_error map to Go's own
// "out of memory" / "makeslice: len out of range" runtime panics and are
// counted the same way; anything else (ASan/UBSan reports, SIGSEGV,
// timeouts) is a real finding and is left to kill the process.
//
// Work runs on a 64 MiB-stack pthread — the same budget tscpp gives its
// parse-all workers — so a stack-overflow report means the input exceeds
// the port's documented recursion budget, not the driver's.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <new>
#include <span>
#include <string_view>

#include <pthread.h>

#include "internal/ast/ast.h"   // tscUnreachableThrown
#include "internal/lsp/lsp.h"   // goPanic — Go panic parity
#include "internal/core/arena.h" // Arena
#include "internal/core/types.h" // ScriptKind / ScriptTarget / LanguageVariant
#include "internal/gostd/gostd.h" // gostd::io::Reader / errEOF

namespace fuzz {

// Release a parsed SourceFile and everything reachable only through it.
// The SourceFile owns its node arena while living inside it, so it can't be
// `delete`d: move the arenas out into stack locals first, run ~SourceFile
// to release non-arena members (text, diagnostic vectors), then clear()
// the moved arenas — the block the SourceFile occupied is freed only after
// nothing references it.
inline void releaseSourceFile(tsc::SourceFile* f) {
	for (auto* d : f->diagnostics)
		delete d;
	for (auto* d : f->jsDiagnostics)
		delete d;
	for (auto* d : f->jsdocDiagnostics)
		delete d;
	tsc::Arena nodes = std::move(f->nodeArena);
	tsc::Arena jsdoc = std::move(f->jsdocArena);
	f->~SourceFile();
	nodes.clear();
	jsdoc.clear();
}

struct Stats {
	std::atomic<uint64_t> iterations{0};
	std::atomic<uint64_t> faithfulAborts{0};  // TSC_UNREACHABLE throws
	std::atomic<uint64_t> allocPanics{0};     // bad_alloc / length_error
	std::atomic<uint64_t> otherExceptions{0}; // unexpected — review these
};

inline Stats& stats() {
	static Stats s;
	return s;
}

// Printed when the fuzzer exits cleanly (libFuzzer calls atexit handlers on
// -max_total_time expiry and on completion of minimization runs).
inline struct StatsPrinter {
	~StatsPrinter() {
		auto& s = stats();
		std::fprintf(stderr,
		             "[fuzz-stats] iterations=%llu faithfulAborts=%llu "
		             "allocPanics=%llu otherExceptions=%llu\n",
		             (unsigned long long)s.iterations.load(),
		             (unsigned long long)s.faithfulAborts.load(),
		             (unsigned long long)s.allocPanics.load(),
		             (unsigned long long)s.otherExceptions.load());
	}
} statsPrinter;

// One persistent 64 MiB-stack worker thread per process. The parser pool
// (g_parserPool) and other reusable state are thread_local, so per-iteration
// threads would leak a pooled Parser on every exec; a single worker keeps
// that state stable and gives the thread the same stack budget tscpp gives
// its parse-all workers, so a stack-overflow report means the input exceeds
// the port's documented recursion budget, not the driver's.
struct BigStackRunner {
	std::function<void(const uint8_t*, size_t)> fn;
	const uint8_t* data = nullptr;
	size_t size = 0;
	bool hasWork = false;
	bool workDone = true;
	pthread_mutex_t mu;
	pthread_cond_t cvHas;
	pthread_cond_t cvDone;
	pthread_t th;

	BigStackRunner() {
		pthread_mutex_init(&mu, nullptr);
		pthread_cond_init(&cvHas, nullptr);
		pthread_cond_init(&cvDone, nullptr);
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, size_t{64} << 20);
		if (pthread_create(&th, &attr, loop, this) != 0)
			abort();
		pthread_attr_destroy(&attr);
	}

	static void* loop(void* arg) {
		auto& r = *static_cast<BigStackRunner*>(arg);
		pthread_mutex_lock(&r.mu);
		for (;;) {
			while (!r.hasWork)
				pthread_cond_wait(&r.cvHas, &r.mu);
			const uint8_t* data = r.data;
			size_t size = r.size;
			pthread_mutex_unlock(&r.mu);
			try {
				r.fn(data, size);
			} catch (const tsc::tscUnreachableThrown&) {
				stats().faithfulAborts++;
			} catch (const tsc::lsp::goPanic&) {
				// Go panic() parity — faithful abort, not a bug.
				stats().faithfulAborts++;
			} catch (const std::bad_alloc&) {
				stats().allocPanics++;
			} catch (const std::length_error&) {
				stats().allocPanics++;
			} catch (...) {
				stats().otherExceptions++;
			}
			pthread_mutex_lock(&r.mu);
			r.hasWork = false;
			r.workDone = true;
			pthread_cond_signal(&r.cvDone);
		}
		return nullptr;
	}

	void run(const std::function<void(const uint8_t*, size_t)>& f,
	         const uint8_t* d, size_t n) {
		pthread_mutex_lock(&mu);
		fn = f;
		data = d;
		size = n;
		hasWork = true;
		workDone = false;
		pthread_cond_signal(&cvHas);
		while (!workDone)
			pthread_cond_wait(&cvDone, &mu);
		pthread_mutex_unlock(&mu);
	}
};

inline BigStackRunner& runner() {
	static BigStackRunner r;
	return r;
}

inline void runOnBigStack(
    const std::function<void(const uint8_t*, size_t)>& fn,
    const uint8_t* data, size_t size) {
	stats().iterations++;
	runner().run(fn, data, size);
}

// Deterministic ScriptKind / ScriptTarget / LanguageVariant selection from
// the first input byte, so one corpus exercises every grammar variant.
inline tsc::ScriptKind scriptKindFor(uint8_t b, const char** fileName) {
	static const tsc::ScriptKind kinds[] = {
	    tsc::ScriptKind::TS, tsc::ScriptKind::TSX, tsc::ScriptKind::JS,
	    tsc::ScriptKind::JSX, tsc::ScriptKind::JSON};
	static const char* names[] = {"f.ts", "f.tsx", "f.js", "f.jsx",
	                              "f.json"};
	size_t i = static_cast<size_t>(b) % 5;
	*fileName = names[i];
	return kinds[i];
}

inline tsc::ScriptTarget scriptTargetFor(uint8_t b) {
	static const tsc::ScriptTarget targets[] = {
	    tsc::ScriptTarget::ES5,    tsc::ScriptTarget::ES2015,
	    tsc::ScriptTarget::ES2018, tsc::ScriptTarget::ES2020,
	    tsc::ScriptTarget::ESNext};
	return targets[static_cast<size_t>(b) % 5];
}

// gostd::io::Reader backed by the fuzz input — what the LSP/ipc transports
// see over stdio. Returns the rest of the buffer in one read (bounded by
// the caller's span), then io.EOF.
struct SliceReader : tsc::gostd::io::Reader {
	const uint8_t* data;
	size_t size;
	size_t pos = 0;

	std::pair<int, tsc::gostd::Error> read(std::span<char> buf) override {
		size_t avail = size - pos;
		size_t n = avail < buf.size() ? avail : buf.size();
		if (n == 0) {
			return {0, tsc::gostd::io::errEOF};
		}
		std::memcpy(buf.data(), data + pos, n);
		pos += n;
		return {static_cast<int>(n), nullptr};
	}
};

} // namespace fuzz
