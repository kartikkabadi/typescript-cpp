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
	stdioConn(std::shared_ptr<gostd::io::ReadCloser> in_,
	          std::shared_ptr<gostd::io::WriteCloser> out_)
	    : stdin_(std::move(in_)), stdout_(std::move(out_)) {}

	std::pair<int, gostd::Error> read(std::span<char> b) override {
		return stdin_->read(b);
	}
	std::pair<int, gostd::Error> write(std::string_view b) override {
		return stdout_->write(b);
	}
	gostd::Error close() override {
		// os.Stdin.Close(); os.Stdout.Close() — nil receivers are valid.
		gostd::Error inErr, outErr;
		if (stdin_ != nullptr) {
			inErr = stdin_->close();
		}
		if (stdout_ != nullptr) {
			outErr = stdout_->close();
		}
		if (inErr != nullptr) {
			return inErr;
		}
		return outErr;
	}

private:
	// underscore suffix: stdin/stdout are macros under MSVC's CRT.
	std::shared_ptr<gostd::io::ReadCloser> stdin_;
	std::shared_ptr<gostd::io::WriteCloser> stdout_;
};

// NewStdioTransport — transport.go:55.
std::shared_ptr<StdioTransport> NewStdioTransport(
    std::shared_ptr<gostd::io::ReadCloser> stdin_,
    std::shared_ptr<gostd::io::WriteCloser> stdout_) {
	return std::make_shared<StdioTransport>(std::move(stdin_),
	                                        std::move(stdout_));
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
	return {std::make_shared<stdioConn>(stdin_, stdout_), nullptr};
}

// StdioTransport::Close — transport.go:74.
gostd::Error StdioTransport::Close() { return nullptr; }

} // namespace tsc::ipc
