// transport_windows.cpp — Port of tsc/internal/ipc/transport_windows.go
// (+build windows): named-pipe plumbing for PipeTransport.
//
// Go uses go-winio's ListenPipe: CreateNamedPipe with
// FILE_FLAG_FIRST_PIPE_INSTANCE|FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE;
// Accept does ConnectNamedPipe (overlapped) then creates the next instance.
// Our PipeTransport keeps the fd-shaped API: newPipeListener mints a fake fd
// key into a listener registry; PipeTransport::Accept/Close (defined here,
// like transport_unix.cpp owns the unix ones) drive it.
#ifdef _WIN32

#include "internal/ipc/ipc.h"
#include "internal/win32/w32compat.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace tsc::ipc {

namespace {

std::string goErrnoText(int eno) {
	return w32::errnoText(eno);
}

gostd::Error goErrnoError(std::string_view op, std::string_view path,
                        int eno) {
	std::string msg(op);
	msg += " pipe";
	if (!path.empty()) {
		msg += ' ';
		msg += path;
	}
	msg += ": ";
	msg += goErrnoText(eno);
	return gostd::newError(msg);
}

// goWin32Error — same message shape, but the raw Win32 code's
// FormatMessage text (Go's net.OpError wrapping syscall.Errno).
gostd::Error goWin32Error(std::string_view op, std::string_view path,
                        unsigned long win32err) {
	std::string msg(op);
	msg += " pipe";
	if (!path.empty()) {
		msg += ' ';
		msg += path;
	}
	msg += ": ";
	msg += w32::win32Text(win32err);
	return gostd::newError(msg);
}

struct pipeListener {
	std::string path;
	HANDLE pending = INVALID_HANDLE_VALUE; // instance awaiting connection
	HANDLE event = nullptr;                // overlapped event for connect
	OVERLAPPED ov{};
	std::atomic<bool> connected{false};
	std::atomic<bool> closed{false};
};

std::mutex g_listenerMu;
std::unordered_map<int, std::shared_ptr<pipeListener>> g_listeners;
int g_nextListenerFd = 0x40000000;

HANDLE createPipeInstance(const std::wstring& wpath, bool first) {
	DWORD flags = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
	if (first) {
		flags |= FILE_FLAG_FIRST_PIPE_INSTANCE;
	}
	return CreateNamedPipeW(wpath.c_str(), flags,
	                        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE |
	                            PIPE_WAIT,
	                        PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0,
	                        nullptr);
}

// Arm ConnectNamedPipe on the pending instance. Returns true when a client
// is already connected or the wait is armed.
bool armConnect(const std::shared_ptr<pipeListener>& l, DWORD* errOut) {
	ResetEvent(l->event);
	memset(&l->ov, 0, sizeof(l->ov));
	l->ov.hEvent = l->event;
	if (ConnectNamedPipe(l->pending, &l->ov)) {
		l->connected = true;
		return true;
	}
	DWORD e = GetLastError();
	switch (e) {
	case ERROR_IO_PENDING:
		return true;
	case ERROR_PIPE_CONNECTED:
		l->connected = true;
		return true;
	default:
		*errOut = e;
		return false;
	}
}

} // namespace

// newPipeListener — transport_windows.go:9. winio.ListenPipe(path, nil):
// first instance created now; instances armed per Accept.
std::pair<int, gostd::Error> newPipeListener(std::string_view path) {
	auto l = std::make_shared<pipeListener>();
	l->path = std::string(path);
	l->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (l->event == nullptr) {
		return {-1, goErrnoError("listen", path,
		                         w32::errnoFromWin32(GetLastError()))};
	}
	l->pending = createPipeInstance(w32::widen(l->path), true);
	if (l->pending == INVALID_HANDLE_VALUE) {
		DWORD e = GetLastError();
		CloseHandle(l->event);
		return {-1, goWin32Error("listen", path, e)};
	}
	DWORD err = 0;
	if (!armConnect(l, &err)) {
		CloseHandle(l->pending);
		CloseHandle(l->event);
		return {-1, goWin32Error("listen", path, err)};
	}
	int fd;
	{
		std::lock_guard lk(g_listenerMu);
		fd = g_nextListenerFd++;
		g_listeners[fd] = l;
	}
	return {fd, nullptr};
}

// winPipeConn — the net.Conn analog: byte pipe connection handle.
class winPipeConn : public gostd::io::ReadWriteCloser {
public:
	explicit winPipeConn(HANDLE h) : h_(h) {
		ovRead_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		ovWrite_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	}
	~winPipeConn() override { (void)close(); }

	std::pair<int, gostd::Error> read(std::span<char> b) override {
		for (;;) {
			DWORD n = 0;
			if (ReadFile(h_, b.data(), static_cast<DWORD>(b.size()), &n,
			             &ovRead_)) {
				if (n == 0) return {0, gostd::io::errEOF};
				return {static_cast<int>(n), nullptr};
			}
			DWORD e = GetLastError();
			if (e == ERROR_IO_PENDING) {
				if (!GetOverlappedResult(h_, &ovRead_, &n, TRUE)) {
					e = GetLastError();
				} else {
					if (n == 0) return {0, gostd::io::errEOF};
					return {static_cast<int>(n), nullptr};
				}
			}
			if (e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED ||
			    e == ERROR_OPERATION_ABORTED) {
				return {0, gostd::io::errEOF};
			}
			return {0, goWin32Error("read", "", e)};
		}
	}
	std::pair<int, gostd::Error> write(std::string_view b) override {
		size_t sent = 0;
		while (sent < b.size()) {
			DWORD n = 0;
			if (!WriteFile(h_, b.data() + sent,
			               static_cast<DWORD>(b.size() - sent), &n,
			               &ovWrite_)) {
				DWORD e = GetLastError();
				if (e == ERROR_IO_PENDING) {
					if (!GetOverlappedResult(h_, &ovWrite_, &n, TRUE)) {
						e = GetLastError();
						n = 0;
					}
				}
				if (n == 0 && e != ERROR_IO_PENDING) {
					gostd::Error err =
					    (e == ERROR_BROKEN_PIPE ||
					     e == ERROR_NO_DATA)
					        ? goErrnoError("write", "", EPIPE)
					        : goWin32Error("write", "", e);
					if (sent > 0) {
						return {static_cast<int>(sent), err};
					}
					return {0, err};
				}
			}
			if (n == 0) {
				return {static_cast<int>(sent),
				        gostd::newError("short write")};
			}
			sent += n;
		}
		return {static_cast<int>(sent), nullptr};
	}
	gostd::Error close() override {
		if (h_ == INVALID_HANDLE_VALUE) {
			return nullptr;
		}
		HANDLE h = h_;
		h_ = INVALID_HANDLE_VALUE;
		FlushFileBuffers(h);
		DisconnectNamedPipe(h);
		CloseHandle(h);
		if (ovRead_.hEvent) {
			CloseHandle(ovRead_.hEvent);
			ovRead_.hEvent = nullptr;
		}
		if (ovWrite_.hEvent) {
			CloseHandle(ovWrite_.hEvent);
			ovWrite_.hEvent = nullptr;
		}
		return nullptr;
	}

private:
	HANDLE h_;
	OVERLAPPED ovRead_{};
	OVERLAPPED ovWrite_{};
};

// PipeTransport::Accept — transport.go:30. Waits for and returns the next
// connection; arms the next listener instance.
std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
PipeTransport::Accept() {
	std::shared_ptr<pipeListener> l;
	{
		std::lock_guard lk(g_listenerMu);
		auto it = g_listeners.find(listenerFd);
		if (it != g_listeners.end()) l = it->second;
	}
	if (!l) {
		return {nullptr, goErrnoError("accept", "", EBADF)};
	}
	// Wait for a client (or close) on the pending instance.
	for (;;) {
		{
			std::lock_guard lk(g_listenerMu);
			if (l->closed) {
				return {nullptr, goErrnoError("accept", "", EINVAL)};
			}
		}
		if (l->connected) {
			break;
		}
		DWORD r = WaitForSingleObject(l->event, 50);
		if (r == WAIT_OBJECT_0) {
			l->connected = true;
			break;
		}
		if (r == WAIT_FAILED) {
			return {nullptr, goErrnoError("accept", "", EIO)};
		}
	}
	HANDLE conn = l->pending;
	// Arm the next instance before delivering this connection.
	l->pending = createPipeInstance(w32::widen(l->path), false);
	l->connected = false;
	if (l->pending != INVALID_HANDLE_VALUE) {
		DWORD err = 0;
		if (!armConnect(l, &err)) {
			CloseHandle(l->pending);
			l->pending = INVALID_HANDLE_VALUE;
		}
	}
	return {std::make_shared<winPipeConn>(conn), nullptr};
}

// PipeTransport::Close — transport.go:40.
gostd::Error PipeTransport::Close() {
	if (listenerFd < 0) {
		return nullptr;
	}
	int fd = listenerFd;
	listenerFd = -1;
	std::shared_ptr<pipeListener> l;
	{
		std::lock_guard lk(g_listenerMu);
		auto it = g_listeners.find(fd);
		if (it != g_listeners.end()) {
			l = it->second;
			g_listeners.erase(it);
		}
	}
	if (!l) return nullptr;
	l->closed = true;
	CancelIoEx(l->pending, &l->ov);
	SetEvent(l->event); // wake the Accept wait loop
	if (l->pending != INVALID_HANDLE_VALUE) {
		DisconnectNamedPipe(l->pending);
		CloseHandle(l->pending);
		l->pending = INVALID_HANDLE_VALUE;
	}
	CloseHandle(l->event);
	return nullptr;
}

// GeneratePipePath — transport_windows.go:16: `\\.\pipe\` + name.
std::string GeneratePipePath(std::string_view name) {
	return "\\\\.\\pipe\\" + std::string(name);
}

} // namespace tsc::ipc

#endif // _WIN32
