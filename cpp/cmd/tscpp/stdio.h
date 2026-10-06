// tsc/cmd/tsc — os.Stdin/os.Stdout/os.Stderr adapters over gostd's io
// interfaces, for --lsp (lsp.go:52-56) and --api (api.go:67-69) which wire
// stdio into ToReader/ToWriter and StdioServerOptions.
#pragma once

#include <cerrno>
#include <cstring>
#include <memory>
#include <unistd.h>

#include "internal/gostd/gostd.h"

namespace tsc::cmd_stdio {

// os.Stdin — lsp.go:54 In: lsp.ToReader(os.Stdin); api.go In = os.Stdin.
struct stdinReader : gostd::io::Reader {
	std::pair<int, gostd::Error> read(std::span<char> buf) override {
		for (;;) {
			auto n = ::read(0, buf.data(), buf.size());
			if (n < 0 && errno == EINTR) continue;
			if (n < 0) {
				return {0, gostd::newError(std::strerror(errno))};
			}
			if (n == 0) {
				return {0, gostd::io::errEOF};
			}
			return {(int)n, nullptr};
		}
	}
};

struct fdWriterBase : gostd::io::Writer {
	int fd;
	explicit fdWriterBase(int f) : fd(f) {}
	std::pair<int, gostd::Error> write(std::string_view data) override {
		size_t off = 0;
		while (off < data.size()) {
			auto n = ::write(fd, data.data() + off, data.size() - off);
			if (n < 0 && errno == EINTR) continue;
			if (n < 0) {
				return {(int)off, gostd::newError(std::strerror(errno))};
			}
			off += (size_t)n;
		}
		return {(int)data.size(), nullptr};
	}
};

// os.Stdout — lsp.go:55 Out; api.go Out.
struct stdoutWriter : fdWriterBase {
	stdoutWriter() : fdWriterBase(1) {}
};

// os.Stderr — lsp.go:56 Err; api.go Err.
struct stderrWriter : fdWriterBase {
	stderrWriter() : fdWriterBase(2) {}
};

// Closable stdio handles for api.StdioServerOptions
// (In/Out are io.ReadCloser/io.WriteCloser — server.go:17-18). Closing
// is a no-op on the shared stdio fds (Go likewise never actually closes
// os.Stdin/os.Stdout for the lifetime of the server).
struct osStdin final : gostd::io::ReadCloser {
	stdinReader r;
	std::pair<int, gostd::Error> read(std::span<char> buf) override {
		return r.read(buf);
	}
	gostd::Error close() override { return nullptr; }
};
struct osStdout final : gostd::io::WriteCloser {
	stdoutWriter w;
	std::pair<int, gostd::Error> write(std::string_view data) override {
		return w.write(data);
	}
	gostd::Error close() override { return nullptr; }
};

} // namespace tsc::cmd_stdio
