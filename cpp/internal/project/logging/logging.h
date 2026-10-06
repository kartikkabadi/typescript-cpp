// logging — port of tsc/internal/project/logging (logger.go + logtree.go +
// logcollector.go): the Logger interface, the io.Writer-backed logger, the
// tree-structured LogTree used for project-builder logs, and the
// strings.Builder-backed test LogCollector.
//
// Go's nil-receiver-safe methods ((l *logger)(nil).Log etc.) are ported as
// nil-checked free functions: `logging::log(l, msg)`,
// `logging::logf(l, fmt, args)`, `logging::verbose(l)`, ... A nullptr Logger*
// is Go's NewNopLogger — every free function no-ops on it. Call the member
// functions directly only when the pointer is known non-null.
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "internal/gostd/gostd.h"

namespace tsc::logging {

// formatTime — logger.go. Go's t.Format("15:04:05.000") wrapped in brackets.
std::string formatTime(std::chrono::system_clock::time_point t);

// Logger — logger.go interface.
struct Logger {
	virtual ~Logger() = default;
	virtual void Log(std::string_view msg) = 0;
	virtual void Logf(std::string_view format,
	                  const std::vector<gostd::fmtArg>& args) = 0;
	// Verbose returns the logger instance if verbose logging is enabled, and
	// otherwise returns nil.
	virtual Logger* Verbose() = 0;
	virtual bool IsVerbose() = 0;
	virtual void SetVerbose(bool verbose) = 0;

	// Error/Warn/Info delegate to Log/Logf exactly as the Go methods do.
	void Error(std::string_view msg) { Log(msg); }
	void Errorf(std::string_view format,
	            const std::vector<gostd::fmtArg>& args) {
		Logf(format, args);
	}
	void Warn(std::string_view msg) { Log(msg); }
	void Warnf(std::string_view format,
	           const std::vector<gostd::fmtArg>& args) {
		Logf(format, args);
	}
	void Info(std::string_view msg) { Log(msg); }
	void Infof(std::string_view format,
	           const std::vector<gostd::fmtArg>& args) {
		Logf(format, args);
	}
};

// logger — the io.Writer-backed Logger impl (logger.go).
struct loggerImpl final : Logger {
	std::mutex mu;
	bool verbose = false;
	gostd::io::Writer* writer;
	std::function<std::string()> prefix;

	void Log(std::string_view msg) override;
	void Logf(std::string_view format,
	          const std::vector<gostd::fmtArg>& args) override;
	Logger* Verbose() override;
	bool IsVerbose() override;
	void SetVerbose(bool verbose) override;
};

// NewLogger — logger.go.
Logger* newLogger(gostd::io::Writer* output);

// NewNopLogger — logger.go: returns a nil-like logger. nullptr is safe to
// call the free logging::log/logf/etc. helpers on.
inline Logger* newNopLogger() { return nullptr; }

// LogTree — logtree.go.
struct LogTree;

struct logEntry {
	uint64_t seq;
	std::chrono::system_clock::time_point time;
	std::string message;
	LogTree* child;
};

struct LogTree final : Logger {
	std::string name;
	std::mutex mu;
	std::vector<logEntry*> logs;
	LogTree* root = nullptr;
	int level = 0;
	bool verbose = false;

	// Only set on root
	std::atomic<int32_t> count{0};
	std::atomic<int32_t> stringLength{0};

	void add(logEntry* log);
	void writeLogsRecursive(std::string& builder, const std::string& indent);

	void Log(std::string_view msg) override;
	void Logf(std::string_view format,
	          const std::vector<gostd::fmtArg>& args) override;
	Logger* Verbose() override;
	bool IsVerbose() override;
	void SetVerbose(bool verbose) override;
	void Embed(LogTree* logs);
	LogTree* Fork(std::string_view message);
	std::string String();
};

// NewLogTree — logtree.go.
LogTree* newLogTree(std::string_view name);

// LogCollector — logcollector.go (fmt.Stringer + Logger).
struct LogCollector : Logger {
	virtual std::string String() = 0;
};

// NewTestLogger — logcollector.go.
LogCollector* newTestLogger();

// ---------------------------------------------------------------------------
// Nil-safe dispatchers — Go's `l == nil` guards in every method. A nullptr
// Logger*/LogTree* is the no-op logger.
// ---------------------------------------------------------------------------

inline void log(Logger* l, std::string_view msg) {
	if (l == nullptr) return;
	l->Log(msg);
}
inline void log(LogTree* t, std::string_view msg) {
	if (t == nullptr) return;
	t->Log(msg);
}
// loggerIsVerbose — nil-safe IsVerbose for the dep-stubbed nop logger
// (nullptr).
inline bool loggerIsVerbose(Logger* l) {
	return l != nullptr && l->IsVerbose();
}

inline void logf(Logger* l, std::string_view format,
                 const std::vector<gostd::fmtArg>& args) {
	if (l == nullptr) return;
	l->Logf(format, args);
}
inline void logf(LogTree* t, std::string_view format,
                 const std::vector<gostd::fmtArg>& args) {
	if (t == nullptr) return;
	t->Logf(format, args);
}
template <typename... Args>
inline void logf(Logger* l, std::string_view format, Args&&... args) {
	if (l == nullptr) return;
	l->Logf(format,
	        std::vector<gostd::fmtArg>{gostd::fmtArg(
	            std::forward<Args>(args))...});
}
template <typename... Args>
inline void logf(LogTree* t, std::string_view format, Args&&... args) {
	if (t == nullptr) return;
	t->Logf(format,
	        std::vector<gostd::fmtArg>{gostd::fmtArg(
	            std::forward<Args>(args))...});
}
inline void error(Logger* l, std::string_view msg) { log(l, msg); }
inline void errorf(Logger* l, std::string_view format,
                   const std::vector<gostd::fmtArg>& args) {
	logf(l, format, args);
}
inline void warn(Logger* l, std::string_view msg) { log(l, msg); }
inline void warnf(Logger* l, std::string_view format,
                  const std::vector<gostd::fmtArg>& args) {
	logf(l, format, args);
}
inline void info(Logger* l, std::string_view msg) { log(l, msg); }
inline void infof(Logger* l, std::string_view format,
                  const std::vector<gostd::fmtArg>& args) {
	logf(l, format, args);
}
inline Logger* verbose(Logger* l) {
	if (l == nullptr) return nullptr;
	return l->Verbose();
}
inline Logger* verbose(LogTree* t) {
	if (t == nullptr) return nullptr;
	return t->Verbose();
}
inline bool isVerbose(Logger* l) {
	if (l == nullptr) return false;
	return l->IsVerbose();
}
inline bool isVerbose(LogTree* t) {
	if (t == nullptr) return false;
	return t->IsVerbose();
}
inline void setVerbose(Logger* l, bool v) {
	if (l == nullptr) return;
	l->SetVerbose(v);
}
inline void setVerbose(LogTree* t, bool v) {
	if (t == nullptr) return;
	t->SetVerbose(v);
}
inline void embed(LogTree* t, LogTree* logs) {
	if (t == nullptr) return;
	t->Embed(logs);
}
inline LogTree* fork(LogTree* t, std::string_view message) {
	if (t == nullptr) return nullptr;
	return t->Fork(message);
}
inline std::string logTreeString(LogTree* t) {
	return t->String();
}

} // namespace tsc::logging
