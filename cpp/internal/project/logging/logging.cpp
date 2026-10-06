// logging — port of tsc/internal/project/logging: logger.go, logtree.go,
// logcollector.go.
#include "internal/project/logging/logging.h"

#include <cstdio>
#include <ctime>

namespace tsc::logging {

namespace {

// fmt.Fprintln(w, prefix, msg) — space-separated operands + newline.
void fprintln(gostd::io::Writer* w, const std::string& prefix,
              std::string_view msg) {
	std::string line;
	line.reserve(prefix.size() + 1 + msg.size() + 1);
	line += prefix;
	line += ' ';
	line += msg;
	line += '\n';
	w->write(line);
}

} // namespace

// formatTime — logger.go. Go's t.Format("15:04:05.000") wrapped in brackets.
std::string formatTime(std::chrono::system_clock::time_point t) {
	std::time_t tt = std::chrono::system_clock::to_time_t(t);
	std::tm tm{};
	localtime_r(&tt, &tm);
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	              t.time_since_epoch())
	              .count() %
	          1000;
	if (ms < 0) {
		ms += 1000;
	}
	char buf[32];
	std::snprintf(buf, sizeof buf, "[%02d:%02d:%02d.%03d]", tm.tm_hour,
	              tm.tm_min, tm.tm_sec, (int)ms);
	return buf;
}

// ---------------------------------------------------------------------------
// logger — logger.go
// ---------------------------------------------------------------------------

void loggerImpl::Log(const std::vector<gostd::fmtArg>& msg) {
	std::lock_guard<std::mutex> lk(mu);
	std::string text;
	for (const auto& a : msg) {
		text += a.text;
	}
	fprintln(writer, prefix(), text);
}

void loggerImpl::Logf(std::string_view format,
                      const std::vector<gostd::fmtArg>& args) {
	std::lock_guard<std::mutex> lk(mu);
	fprintln(writer, prefix(), gostd::sprintf(format, args));
}

Logger* loggerImpl::Verbose() {
	std::lock_guard<std::mutex> lk(mu);
	if (!verbose) {
		return nullptr;
	}
	return this;
}

bool loggerImpl::IsVerbose() {
	std::lock_guard<std::mutex> lk(mu);
	return verbose;
}

void loggerImpl::SetVerbose(bool v) {
	std::lock_guard<std::mutex> lk(mu);
	verbose = v;
}

// NewLogger — logger.go.
Logger* newLogger(gostd::io::Writer* output) {
	auto* l = new loggerImpl();
	l->writer = output;
	l->prefix = [] {
		return formatTime(std::chrono::system_clock::now());
	};
	return l;
}

// ---------------------------------------------------------------------------
// LogTree — logtree.go
// ---------------------------------------------------------------------------

namespace {
std::atomic<uint64_t> seq{0};

logEntry* newLogEntry(LogTree* child, std::string message) {
	return new logEntry{seq.fetch_add(1) + 1,
	                    std::chrono::system_clock::now(), std::move(message),
	                    child};
}
} // namespace

void LogTree::add(logEntry* log) {
	// indent + header + message + newline
	root->stringLength.fetch_add(level + 15 + (int32_t)log->message.size() + 1);
	root->count.fetch_add(1);
	std::lock_guard<std::mutex> lk(mu);
	logs.push_back(log);
}

void LogTree::Log(const std::vector<gostd::fmtArg>& msg) {
	std::string text;
	for (const auto& a : msg) {
		text += a.text;
	}
	add(newLogEntry(nullptr, text));
}

void LogTree::Logf(std::string_view format,
                   const std::vector<gostd::fmtArg>& args) {
	add(newLogEntry(nullptr, gostd::sprintf(format, args)));
}

bool LogTree::IsVerbose() {
	return verbose;
}

void LogTree::SetVerbose(bool v) {
	verbose = v;
}

Logger* LogTree::Verbose() {
	if (!verbose) {
		return nullptr;
	}
	return this;
}

void LogTree::Embed(LogTree* logsTree) {
	int32_t count = logsTree->count.load();
	root->stringLength.fetch_add(logsTree->stringLength.load() +
	                             count * level);
	root->count.fetch_add(count);
	add(newLogEntry(logsTree, logsTree->name));
}

LogTree* LogTree::Fork(std::string_view message) {
	auto* child = new LogTree();
	child->level = level + 1;
	child->root = root;
	child->verbose = verbose;
	add(newLogEntry(child, std::string(message)));
	return child;
}

std::string LogTree::String() {
	if (root != this) {
		TSC_UNREACHABLE("can only call String on root LogTree");
	}
	std::string builder;
	std::string header =
		gostd::sprintf("======== %s ========\n", {name});
	builder.reserve(stringLength.load() + header.size());
	builder += header;
	writeLogsRecursive(builder, "");
	return builder;
}

void LogTree::writeLogsRecursive(std::string& builder,
                                 const std::string& indent) {
	for (auto* log : logs) {
		builder += indent;
		builder += formatTime(log->time);
		builder += ' ';
		builder += log->message;
		builder += '\n';
		if (log->child != nullptr) {
			log->child->writeLogsRecursive(builder, indent + '\t');
		}
	}
}

// NewLogTree — logtree.go.
LogTree* newLogTree(std::string_view name) {
	auto* lc = new LogTree();
	lc->name = name;
	lc->root = lc;
	return lc;
}

// ---------------------------------------------------------------------------
// logCollector — logcollector.go
// ---------------------------------------------------------------------------

namespace {
struct stringWriter final : gostd::io::Writer {
	std::string* buf;
	std::pair<int, gostd::Error> write(std::string_view data) override {
		buf->append(data);
		return {(int)data.size(), nullptr};
	}
};

struct logCollectorImpl final : LogCollector {
	loggerImpl inner; // embedded logger
	std::string builder;
	stringWriter w;

	logCollectorImpl() {
		w.buf = &builder;
		inner.writer = &w;
		inner.prefix = [] {
			// time.Unix(1349085672, 0)
			return formatTime(std::chrono::system_clock::from_time_t(
				1349085672));
		};
	}

	void Log(std::string_view msg) override { inner.Log(msg); }
	void Logf(std::string_view format,
	          const std::vector<gostd::fmtArg>& args) override {
		inner.Logf(format, args);
	}
	Logger* Verbose() override { return inner.Verbose(); }
	bool IsVerbose() override { return inner.IsVerbose(); }
	void SetVerbose(bool v) override { inner.SetVerbose(v); }
	std::string String() override { return builder; }
};
} // namespace

// NewTestLogger — logcollector.go.
LogCollector* newTestLogger() {
	return new logCollectorImpl();
}

} // namespace tsc::logging
