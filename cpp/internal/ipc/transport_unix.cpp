// transport_unix.cpp — Port of tsc/internal/ipc/transport_unix.go
// (+build unix): Unix-domain-socket plumbing for PipeTransport.
//
// gostd has no net package, so the unix socket ops map directly to
// socket(2)/bind(2)/listen(2)/accept(2)/fcntl. Go's os.ExitError on a
// failed Remove is surfaced as an errors.New-style message; syscall errors
// carry the POSIX errno text like Go's os.PathError wrapping.

#include "internal/ipc/ipc.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace tsc::ipc {

namespace {

// goErrnoText — syscall.Errno.Error() text. Go uses its own errno table on
// Linux (lowercase, e.g. "no such file or directory"); glibc's strerror
// capitalizes the first letter, so normalize.
std::string goErrnoText(int eno) {
	std::string s = std::strerror(eno);
	if (!s.empty() && s[0] >= 'A' && s[0] <= 'Z') {
		s[0] = static_cast<char>(s[0] - 'A' + 'a');
	}
	return s;
}

// goErrnoError — a *net.OpError-shaped message: "<op> unix <path>: <errno
// text>" (or "<op> unix: <errno text>" when path is empty).
gostd::Error goErrnoError(std::string_view op, std::string_view path,
                        int eno) {
	std::string msg(op);
	msg += " unix";
	if (!path.empty()) {
		msg += ' ';
		msg += path;
	}
	msg += ": ";
	msg += goErrnoText(eno);
	return gostd::newError(msg);
}

// closeFd — os.File.Close-shaped error for a descriptor.
gostd::Error closeFd(int fd) {
	if (::close(fd) < 0) {
		std::string msg = "close ";
		msg += std::to_string(fd);
		msg += ": ";
		msg += goErrnoText(errno);
		return gostd::newError(msg);
	}
	return nullptr;
}

// setCloexec — Go's syscall.ForkLock'd CLOEXEC on every descriptor.
void setCloexec(int fd) {
	int flags = ::fcntl(fd, F_GETFD);
	if (flags >= 0) {
		(void)::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
	}
}

} // namespace

// newPipeListener — transport_unix.go:15.
std::pair<int, gostd::Error> newPipeListener(std::string_view path) {
	// Remove any existing socket file (Go: `_ = os.Remove(path)`).
	(void)::unlink(std::string(path).c_str());

	int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return {-1, goErrnoError("socket", path, errno)};
	}
	setCloexec(fd);

	sockaddr_un addr{};
	addr.sun_family = AF_UNIX;
	if (path.size() >= sizeof(addr.sun_path)) {
		closeFd(fd);
		return {-1, goErrnoError("listen", path, EINVAL)};
	}
	std::memcpy(addr.sun_path, path.data(), path.size());

	if (::bind(fd, reinterpret_cast<sockaddr*>(&addr),
	           sizeof(addr)) < 0) {
		int eno = errno;
		closeFd(fd);
		return {-1, goErrnoError("listen", path, eno)};
	}
	if (::listen(fd, 128) < 0) { // Go's net uses backlog 128
		int eno = errno;
		closeFd(fd);
		return {-1, goErrnoError("listen", path, eno)};
	}
	return {fd, nullptr};
}

// unixSocketConn — an io.ReadWriteCloser over a connected unix socket fd.
// Go's transport returns the net.Conn directly; the port wraps the fd.
class unixSocketConn : public gostd::io::ReadWriteCloser {
public:
	explicit unixSocketConn(int fd) : fd(fd) {}
	~unixSocketConn() override { (void)close(); }

	std::pair<int, gostd::Error> read(std::span<char> b) override {
		for (;;) {
			ssize_t n = ::recv(fd, b.data(), b.size(), 0);
			if (n > 0) {
				return {static_cast<int>(n), nullptr};
			}
			if (n == 0) {
				return {0, gostd::io::errEOF};
			}
			if (errno == EINTR) {
				continue;
			}
			return {0, goErrnoError("read", "", errno)};
		}
	}
	std::pair<int, gostd::Error> write(std::string_view b) override {
		size_t sent = 0;
		while (sent < b.size()) {
			ssize_t n = ::send(fd, b.data() + sent, b.size() - sent,
			                   MSG_NOSIGNAL);
			if (n > 0) {
				sent += static_cast<size_t>(n);
				continue;
			}
			if (n < 0 && errno == EINTR) {
				continue;
			}
			gostd::Error err =
			    n < 0 ? goErrnoError("write", "", errno)
			          : gostd::newError("short write");
			if (sent > 0) {
				return {static_cast<int>(sent), err};
			}
			return {0, err};
		}
		return {static_cast<int>(sent), nullptr};
	}
	gostd::Error close() override {
		if (fd < 0) {
			return nullptr;
		}
		int f = fd;
		fd = -1;
		// Go's net.Conn.Close unblocks a concurrent pending Read — shutdown
		// first so a blocked recv returns, then close the descriptor.
		(void)::shutdown(f, SHUT_RDWR);
		return closeFd(f);
	}

private:
	int fd;
};

// PipeTransport::Accept — transport.go:30. Waits for and returns the next
// connection.
std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
PipeTransport::Accept() {
	for (;;) {
		int connFd = ::accept(listenerFd, nullptr, nullptr);
		if (connFd >= 0) {
			setCloexec(connFd);
			return {std::make_shared<unixSocketConn>(connFd), nullptr};
		}
		if (errno == EINTR) {
			continue;
		}
		return {nullptr, goErrnoError("accept", "", errno)};
	}
}

// PipeTransport::Close — transport.go:40. Stops the transport from
// accepting new connections.
gostd::Error PipeTransport::Close() {
	if (listenerFd < 0) {
		return nullptr;
	}
	int fd = listenerFd;
	listenerFd = -1;
	// Go: return t.listener.Close() — the socket file itself is left for the
	// next newPipeListener's os.Remove.
	return closeFd(fd);
}

// GeneratePipePath — transport_unix.go:23. Returns a platform-appropriate
// pipe path for the given name.
std::string GeneratePipePath(std::string_view name) {
	// path.Join(os.TempDir(), name)
	std::string dir;
	if (const char* tmp = std::getenv("TMPDIR"); tmp != nullptr &&
	    *tmp != '\0') {
		dir = tmp;
	} else {
		dir = "/tmp";
	}
	if (dir.size() > 1 && dir.back() == '/') {
		dir.pop_back();
	}
	return dir + '/' + std::string(name);
}

} // namespace tsc::ipc
