// logging — dep-stubs for the ls-autoimport slice.
#include "internal/project/logging/logging.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <sstream>

namespace tsc::logging {

// === dep stubs — removed when owner slice lands ===

void LogTree::Logf(std::string_view) {
	TSC_UNREACHABLE("LogTree::Logf — owned by project");
}

LogTree* LogTree::Fork() {
	TSC_UNREACHABLE("LogTree::Fork — owned by project");
}

// === slice: testutil-leaves === — logger.go + logcollector.go.

namespace {

// anyToString — fmt's %v for the common `any` shapes tests log.
std::string anyToString(const std::any& a) {
	if (!a.has_value()) return "<nil>";
	if (a.type() == typeid(std::string)) return std::any_cast<std::string>(a);
	if (a.type() == typeid(const char*)) return std::any_cast<const char*>(a);
	if (a.type() == typeid(std::string_view)) {
		return std::string(std::any_cast<std::string_view>(a));
	}
	if (a.type() == typeid(int)) return std::to_string(std::any_cast<int>(a));
	if (a.type() == typeid(int64_t)) {
		return std::to_string(std::any_cast<int64_t>(a));
	}
	if (a.type() == typeid(uint64_t)) {
		return std::to_string(std::any_cast<uint64_t>(a));
	}
	if (a.type() == typeid(double)) {
		return std::to_string(std::any_cast<double>(a));
	}
	if (a.type() == typeid(bool)) {
		return std::any_cast<bool>(a) ? "true" : "false";
	}
	if (a.type() == typeid(gostd::Error)) {
		auto e = std::any_cast<gostd::Error>(a);
		return e ? e->Error() : "<nil>";
	}
	return a.type().name();
}

bool isStringAny(const std::any& a) {
	return a.type() == typeid(std::string) ||
	       a.type() == typeid(const char*) ||
	       a.type() == typeid(std::string_view);
}

// fmt.Sprint — spaces are added between operands when neither is a string.
std::string sprintAny(const std::vector<std::any>& args) {
	std::string out;
	for (size_t i = 0; i < args.size(); i++) {
		if (i > 0 && !isStringAny(args[i - 1]) && !isStringAny(args[i])) {
			out += ' ';
		}
		out += anyToString(args[i]);
	}
	return out;
}

// fmt.Sprintf — same verb subset as gostd::sprintf over `any` args.
std::string sprintfAny(std::string_view format,
                       const std::vector<std::any>& args) {
	std::vector<gostd::fmtArg> fmtArgs;
	fmtArgs.reserve(args.size());
	for (const auto& a : args) {
		fmtArgs.emplace_back(anyToString(a));
	}
	return gostd::sprintf(format, fmtArgs);
}

// formatTime — logger.go:133 ("[%s]" of t.Format("15:04:05.000"), local
// time like Go's time.Format on a local time.Time).
std::string formatTime(std::chrono::system_clock::time_point t) {
	std::time_t tt = std::chrono::system_clock::to_time_t(t);
	std::tm tm{};
	localtime_r(&tt, &tm);
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	              t.time_since_epoch())
	              .count() %
	          1000;
	if (ms < 0) ms += 1000;
	char buf[32];
	std::snprintf(buf, sizeof buf, "[%02d:%02d:%02d.%03d]", tm.tm_hour,
	              tm.tm_min, tm.tm_sec, (int)ms);
	return buf;
}

// logger — logger.go:44. `writer` non-owning, like Go's interface value.
struct logger : virtual Logger {
	std::mutex mu;
	bool verbose = false;
	gostd::io::Writer* writer = nullptr;
	std::function<std::string()> prefix;

	void Log(const std::vector<std::any>& msg) override {
		std::lock_guard<std::mutex> lk(mu);
		if (writer == nullptr) return;
		// fmt.Fprintln(l.writer, l.prefix(), fmt.Sprint(msg...))
		writer->write(prefix() + " " + sprintAny(msg) + "\n");
	}

	void Logf(std::string_view format,
	          const std::vector<std::any>& args) override {
		std::lock_guard<std::mutex> lk(mu);
		if (writer == nullptr) return;
		// fmt.Fprintf(l.writer, "%s %s\n", l.prefix(), fmt.Sprintf(format, args...))
		writer->write(prefix() + " " + sprintfAny(format, args) + "\n");
	}

	Logger* Verbose() override {
		std::lock_guard<std::mutex> lk(mu);
		if (!verbose) {
			return nullptr;
		}
		return this;
	}

	bool IsVerbose() override {
		std::lock_guard<std::mutex> lk(mu);
		return verbose;
	}

	void SetVerbose(bool verbose) override {
		std::lock_guard<std::mutex> lk(mu);
		this->verbose = verbose;
	}
};

// nopLogger models NewNopLogger's (*logger)(nil): every method is a no-op
// because Go's nil-receiver checks make them so.
struct nopLogger final : Logger {
	void Log(const std::vector<std::any>&) override {}
	void Logf(std::string_view, const std::vector<std::any>&) override {}
	Logger* Verbose() override { return nullptr; }
	bool IsVerbose() override { return false; }
	void SetVerbose(bool) override {}
};

// logCollector — logcollector.go:12.
struct logCollector final : logger, LogCollector {
	std::ostringstream builder;

	// writerAdapter adapts the collector's builder to io.Writer.
	struct writerAdapter final : gostd::io::Writer {
		std::ostringstream* b;
		explicit writerAdapter(std::ostringstream* b) : b(b) {}
		std::pair<int, gostd::Error> write(std::string_view data) override {
			*b << data;
			return {(int)data.size(), nullptr};
		}
	};
	writerAdapter adapter{&builder};

	logCollector() { writer = &adapter; }

	std::string String() const override { return builder.str(); }
};

}  // namespace

// NewLogger — logger.go:120.
std::shared_ptr<Logger> NewLogger(gostd::io::Writer* output) {
	auto l = std::make_shared<logger>();
	l->writer = output;
	l->prefix = [] {
		return formatTime(std::chrono::system_clock::now());
	};
	return l;
}

// NewNopLogger — logger.go:129.
std::shared_ptr<Logger> NewNopLogger() {
	return std::make_shared<nopLogger>();
}

// NewTestLogger — logcollector.go:23.
std::shared_ptr<LogCollector> NewTestLogger() {
	static const std::string fixedPrefix = formatTime(
	    std::chrono::system_clock::from_time_t(1349085672));
	auto lc = std::make_shared<logCollector>();
	lc->prefix = [] { return fixedPrefix; };
	return lc;
}

// === end slice: testutil-leaves ===

} // namespace tsc::logging
