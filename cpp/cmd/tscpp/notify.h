// tsc/cmd/tsc — signal.NotifyContext: a gostd Context cancelled when the
// process receives SIGINT or SIGTERM (main.go:27, lsp.go:49, api.go:71).
// Implemented as a self-pipe: the (async-signal-safe) handler writes a byte;
// a detached watcher thread reads it and calls the ctx's cancel func.
#pragma once

#include <atomic>
#include <csignal>
#include <memory>
#include <thread>
#ifdef _WIN32
#include "internal/win32/w32compat.h"
#else
#include <unistd.h>
#endif

#include "internal/gostd/gostd.h"

namespace tsc::cmd_notify {

inline std::atomic<int> signalPipe{-1};
inline std::atomic<gostd::CancelFunc*> pendingStop{nullptr};

inline void notifySignalHandler(int sig) {
	// Async-signal-safe: a single byte to wake the watcher thread.
	int fd = signalPipe.load(std::memory_order_relaxed);
	if (fd >= 0) {
		(void)!::write(fd, &sig, 1);
	}
}

// signalNotifyContext — signal.NotifyContext(context.Background(),
// os.Interrupt, SIGTERM). Returns the ctx and the stop func; stop() is
// idempotent (mirroring Go's Stop).
inline std::pair<gostd::Context, gostd::CancelFunc> signalNotifyContext() {
	auto [ctx, cancel] = gostd::contextWithCancel(gostd::contextBackground());
	static int pipeFds[2] = {-1, -1};
	if (signalPipe.load() < 0) {
		if (::pipe(pipeFds) != 0) {
			return {ctx, cancel};
		}
		signalPipe.store(pipeFds[1]);
		struct sigaction sa {};
		sa.sa_handler = notifySignalHandler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART;
		sigaction(SIGINT, &sa, nullptr);
		sigaction(SIGTERM, &sa, nullptr);
	}
	auto* stop = new gostd::CancelFunc(cancel);
	pendingStop.store(stop);
	std::thread([readFd = pipeFds[0]] {
		char b;
		(void)!::read(readFd, &b, 1);
		if (auto* s = pendingStop.load()) {
			(*s)();
		}
	}).detach();
	return {ctx, [stop] { (*stop)(); }};
}

} // namespace tsc::cmd_notify
