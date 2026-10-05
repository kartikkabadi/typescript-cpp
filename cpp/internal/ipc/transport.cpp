// transport.cpp — Port of tsc/internal/ipc/transport.go: the Transport
// interface impls (PipeTransport constructor, StdioTransport, stdioConn).
// PipeTransport::Accept/Close and the unix-socket plumbing are in
// transport_unix.cpp.

#include "internal/ipc/ipc.h"

namespace tsc::ipc {

// NewPipeTransport — transport.go:23. On Unix, creates a Unix domain socket.
std::pair<std::shared_ptr<PipeTransport>, gostd::Error>
NewPipeTransport(std::string_view path) {
	auto [fd, err] = newPipeListener(path);
	if (err != nullptr) {
		return {nullptr, err};
	}
	return {std::make_shared<PipeTransport>(fd, std::string(path)), nullptr};
}

// stdioConn — transport.go:80. An io.ReadWriteCloser wrapping stdin/stdout.
class stdioConn : public gostd::io::ReadWriteCloser {
public:
	stdioConn(std::shared_ptr<gostd::io::ReadCloser> stdin_,
	          std::shared_ptr<gostd::io::WriteCloser> stdout_)
	    : stdin(std::move(stdin_)), stdout(std::move(stdout_)) {}

	std::pair<int, gostd::Error> read(std::span<char> b) override {
		return stdin->read(b);
	}
	std::pair<int, gostd::Error> write(std::string_view b) override {
		return stdout->write(b);
	}
	gostd::Error close() override {
		// os.Stdin.Close(); os.Stdout.Close() — nil receivers are valid.
		gostd::Error inErr, outErr;
		if (stdin != nullptr) {
			inErr = stdin->close();
		}
		if (stdout != nullptr) {
			outErr = stdout->close();
		}
		if (inErr != nullptr) {
			return inErr;
		}
		return outErr;
	}

private:
	std::shared_ptr<gostd::io::ReadCloser> stdin;
	std::shared_ptr<gostd::io::WriteCloser> stdout;
};

// NewStdioTransport — transport.go:55.
std::shared_ptr<StdioTransport> NewStdioTransport(
    std::shared_ptr<gostd::io::ReadCloser> stdin,
    std::shared_ptr<gostd::io::WriteCloser> stdout) {
	return std::make_shared<StdioTransport>(std::move(stdin),
	                                        std::move(stdout));
}

// StdioTransport::Accept — transport.go:64. Accept returns the
// stdin/stdout connection (only once).
std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
StdioTransport::Accept() {
	if (used) {
		return {nullptr,
		        gostd::io::errEOF}; // Only one connection allowed
	}
	used = true;
	return {std::make_shared<stdioConn>(stdin, stdout), nullptr};
}

// StdioTransport::Close — transport.go:74.
gostd::Error StdioTransport::Close() { return nullptr; }

} // namespace tsc::ipc
