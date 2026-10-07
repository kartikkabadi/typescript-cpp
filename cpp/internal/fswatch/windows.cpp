// windows.cpp — port of tsc/internal/fswatch/windows.go: the Windows
// ReadDirectoryChangesW watcher backend. Each dirWatch owns a
// windowsSubscription whose run thread loops on overlapped
// ReadDirectoryChangesW; closeWatch signals stopCh -> CancelIoEx -> run
// exits and doneCh closes.
#ifdef _WIN32

#include "internal/fswatch/fswatch.h"
#include "internal/win32/w32compat.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace tsc::fswatch {

namespace {

const gostd::Error errGetFileInfo =
    gostd::newError("could not get file information");
const gostd::Error errReadChanges =
    gostd::newError("failed to read changes");
const gostd::Error errGetOverlappedResult =
    gostd::newError("GetOverlappedResult failed");
const gostd::Error errUnknown = gostd::newError("unknown error");

constexpr size_t defaultBufSize = 1024 * 1024;
constexpr size_t networkBufSize = 64 * 1024;

constexpr DWORD notifyChangeFilter =
    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
    FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;

class windowsBackend;

struct windowsRead {
	std::vector<BYTE> buf;
	OVERLAPPED overlapped{};
	HANDLE event = nullptr;
};

struct windowsSubscription {
	std::mutex mu;
	windowsBackend* watcherImpl;
	std::shared_ptr<dirWatch> dw;
	HANDLE handle = INVALID_HANDLE_VALUE;
	bool stopped = false;
	std::atomic<bool> stopFlag{false};
	std::atomic<bool> doneFlag{false};
	size_t bufBytes = defaultBufSize;
	std::unique_ptr<windowsRead> first;

	void fatal(const gostd::Error& err);
	void stop();
	void stopLocked();
	bool processCompletion(unsigned long callErr,
	                       const std::vector<BYTE>& buf, DWORD bytes);
	void processOne(DWORD action, const std::string& name);
	void run();
	std::unique_ptr<windowsRead> beginRead();
};

class windowsBackend final : public watcherBase {
public:
	windowsBackend() { watcherBase::init(); }

	gostd::Error start() override {
		notifyStarted();
		return nullptr;
	}

	gostd::Error subscribe(std::shared_ptr<dirWatch> w) override {
		auto sub = std::make_shared<windowsSubscription>();
		sub->watcherImpl = this;
		sub->dw = w;
		std::wstring wp = w32::widen(w->physicalDir);
		sub->handle = CreateFileW(
		    wp.c_str(), FILE_LIST_DIRECTORY,
		    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		    nullptr, OPEN_EXISTING,
		    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
		if (sub->handle == INVALID_HANDLE_VALUE) {
			return std::make_shared<dirWatchErrorObj>(
			    gostd::errorf("invalid handle: %w",
			                  {w32::win32Text(GetLastError())}),
			    w);
		}
		BY_HANDLE_FILE_INFORMATION info{};
		if (!GetFileInformationByHandle(sub->handle, &info)) {
			CloseHandle(sub->handle);
			return std::make_shared<dirWatchErrorObj>(errGetFileInfo, w);
		}
		if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			CloseHandle(sub->handle);
			return std::make_shared<dirWatchErrorObj>(errnoError(ENOTDIR),
			                                          w);
		}
		// Arm the first ReadDirectoryChangesW synchronously so a filesystem
		// op after subscribe returns is guaranteed observed.
		sub->first = sub->beginRead();
		if (sub->first == nullptr) {
			CloseHandle(sub->handle);
			return std::make_shared<dirWatchErrorObj>(errReadChanges, w);
		}
		w->state = sub;
		auto weak = std::weak_ptr<windowsSubscription>(sub);
		std::thread([weak] {
			if (auto s = weak.lock()) s->run();
		}).detach();
		return nullptr;
	}

	gostd::Error closeWatch(std::shared_ptr<dirWatch> w) override {
		windowsSubscription* sub = nullptr;
		if (auto* p = std::any_cast<std::shared_ptr<windowsSubscription>>(
		        &w->state)) {
			sub = p->get();
		}
		w->state.reset();
		if (sub == nullptr) {
			return nullptr;
		}
		sub->stop();
		// Wait for the run thread to close the handle.
		while (!sub->doneFlag.load()) {
			std::this_thread::yield();
		}
		return nullptr;
	}

	void shutdown() override {
		// Each watch owns its goroutine; nothing shared to tear down.
	}
};

std::unique_ptr<windowsRead> windowsSubscription::beginRead() {
	size_t bufSize;
	{
		std::lock_guard lk(mu);
		if (stopped) {
			return nullptr;
		}
		bufSize = bufBytes;
	}
	auto req = std::make_unique<windowsRead>();
	req->buf.resize(bufSize);
	req->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (req->event == nullptr) {
		return nullptr;
	}
	req->overlapped.hEvent = req->event;
	DWORD bytesReturned = 0;
	if (!ReadDirectoryChangesW(handle, req->buf.data(),
	                           static_cast<DWORD>(req->buf.size()),
	                           dw->recursive ? TRUE : FALSE,
	                           notifyChangeFilter, &bytesReturned,
	                           &req->overlapped, nullptr)) {
		DWORD e = GetLastError();
		CloseHandle(req->event);
		if (e == ERROR_INVALID_PARAMETER) {
			std::lock_guard lk(mu);
			bufBytes = networkBufSize;
		}
		return nullptr;
	}
	return req;
}

void windowsSubscription::run() {
	// The invariant: subscribe armed the first read before spawning us.
	std::unique_ptr<windowsRead> current = std::move(first);
	if (current == nullptr) {
		fatal(gostd::newError("fswatch: windows: missing initial read"));
		doneFlag = true;
		return;
	}
	for (;;) {
		// wait(): wait on the overlapped event; cancel via stopFlag.
		DWORD bytes = 0;
		bool waitErr = false;
		DWORD gErr = 0;
		{
			// Poll the event so we can also observe stopFlag without a
			// helper thread (Go uses a helper goroutine + CancelIoEx).
			for (;;) {
				DWORD r = WaitForSingleObject(current->event, 50);
				if (r == WAIT_OBJECT_0 || r == WAIT_FAILED) break;
				if (stopFlag.load()) {
					CancelIoEx(handle, &current->overlapped);
					WaitForSingleObject(current->event, INFINITE);
					break;
				}
			}
			if (!GetOverlappedResult(handle, &current->overlapped, &bytes,
			                         FALSE)) {
				gErr = GetLastError();
			}
			CloseHandle(current->event);
		}

		{
			std::lock_guard lk(mu);
			if (stopped) {
				doneFlag = true;
				return;
			}
		}

		if (gErr != 0) {
			if (processCompletion(gErr, current->buf, bytes)) {
				doneFlag = true;
				return;
			}
			current = beginRead();
			if (current == nullptr) {
				doneFlag = true;
				return;
			}
			continue;
		}

		auto next = beginRead();
		if (next == nullptr) {
			doneFlag = true;
			return;
		}
		if (processCompletion(0, current->buf, bytes)) {
			doneFlag = true;
			return;
		}
		current = std::move(next);
	}
}

bool windowsSubscription::processCompletion(unsigned long callErr,
                                            const std::vector<BYTE>& buf,
                                            DWORD bytes) {
	if (callErr != 0) {
		switch (callErr) {
		case ERROR_OPERATION_ABORTED:
			return true;
		case ERROR_INVALID_PARAMETER:
			{
				std::lock_guard lk(mu);
				bufBytes = networkBufSize;
			}
			return false;
		case ERROR_NOTIFY_ENUM_DIR:
			dw->events.setError(ErrOverflow);
			dw->notify();
			return false;
		case ERROR_ACCESS_DENIED: {
			DWORD attrs = GetFileAttributesW(w32::widen(dw->physicalDir).c_str());
			if (attrs == INVALID_FILE_ATTRIBUTES ||
			    !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
				dw->events.remove(dw->dir);
				dw->events.setError(gostd::errorf(
				    "%w: watched directory removed", {ErrWatchTerminated}));
				dw->notify();
				stop();
				return true;
			}
			[[fallthrough]];
		}
		default:
			fatal(errUnknown);
			return true;
		}
	}

	DWORD offset = 0;
	if (bytes == 0) {
		bytes = static_cast<DWORD>(buf.size());
	}
	while (offset < bytes) {
		auto* fni = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
		    buf.data() + offset);
		int nameLen = static_cast<int>(fni->FileNameLength) / 2;
		std::string name = w32::narrow(std::wstring_view(
		    reinterpret_cast<const wchar_t*>(fni->FileName), nameLen));
		processOne(fni->Action, name);
		if (fni->NextEntryOffset == 0) break;
		offset += fni->NextEntryOffset;
	}
	dw->notify();
	return false;
}

void windowsSubscription::processOne(DWORD action, const std::string& name) {
	std::string path = dw->dir + "\\" + name;
	std::string watchPath = dw->physicalDir + "\\" + name;
	switch (action) {
	case FILE_ACTION_ADDED:
	case FILE_ACTION_RENAMED_NEW_NAME:
		dw->events.create(path);
		break;
	case FILE_ACTION_MODIFIED: {
		WIN32_FILE_ATTRIBUTE_DATA data{};
		if (GetFileAttributesExW(w32::widen(watchPath).c_str(),
		                       GetFileExInfoStandard, &data)) {
			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				dw->events.update(path);
			}
		}
		break;
	}
	case FILE_ACTION_REMOVED:
	case FILE_ACTION_RENAMED_OLD_NAME: {
		uint64_t seq = dw->events.removeAndGetSequence(path);
		if (dw->terminateCallbacksForDeletedRoot(
		        path, seq,
		        gostd::errorf("%w: watched directory removed",
		                      {ErrWatchTerminated}))) {
			dw->notify();
		}
		break;
	}
	}
}

void windowsSubscription::fatal(const gostd::Error& err) {
	auto werr = std::make_shared<dirWatchErrorObj>(err, dw);
	auto* impl = watcherImpl;
	std::thread([impl, werr] { impl->handleWatcherError(werr); }).detach();
	stop();
}

void windowsSubscription::stopLocked() {
	if (stopped) return;
	stopped = true;
	stopFlag = true;
	CancelIoEx(handle, nullptr);
}

void windowsSubscription::stop() {
	std::lock_guard lk(mu);
	stopLocked();
}

} // namespace

watcher& windowsWatcher() {
	static watcher w{"windows"};
	static std::once_flag once;
	std::call_once(once, [] {
		w.factory = []() -> watcherImpl* { return new windowsBackend(); };
	});
	return w;
}

} // namespace tsc::fswatch

#endif // _WIN32
