// logging — dep-decl for the ls-autoimport slice: project/logging's LogTree
// (logtree.go). Only the shape the autoimport registry sees is declared;
// logging is owned by the project slice — methods are dep-stubs.
#pragma once

#include <any>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/gostd/gostd.h"

namespace tsc::logging {

// === dep decls for ls-autoimport — owned by project ===

// LogTree — project/logging/logtree.go.
struct LogTree {
	// Logf — dep-stub.
	void Logf(std::string_view format);
	template <typename... Args>
	void Logf(std::string_view format, Args&&...) {
		Logf(format);
	}
	// Fork — dep-stub.
	LogTree* Fork();
};

// === slice: testutil-leaves === — real ports of logger.go and
// logcollector.go (self-contained; consumed by projecttestutil's
// SessionUtils logger).

// Logger — logger.go:8.
struct Logger {
	virtual ~Logger() = default;
	virtual void Log(const std::vector<std::any>& msg) = 0;
	virtual void Logf(std::string_view format,
	                  const std::vector<std::any>& args) = 0;
	// Verbose returns the logger if verbose logging is enabled, else nil
	// (nullptr).
	virtual Logger* Verbose() = 0;
	virtual bool IsVerbose() = 0;
	virtual void SetVerbose(bool verbose) = 0;

	// Error/Warn/Info are thin wrappers over Log in Go (logger.go:104-122);
	// non-virtual here to share the forwarding.
	void Error(const std::vector<std::any>& msg) { Log(msg); }
	void Warn(const std::vector<std::any>& msg) { Log(msg); }
	void Info(const std::vector<std::any>& msg) { Log(msg); }
	void Errorf(std::string_view format, const std::vector<std::any>& args) {
		Logf(format, args);
	}
	void Warnf(std::string_view format, const std::vector<std::any>& args) {
		Logf(format, args);
	}
	void Infof(std::string_view format, const std::vector<std::any>& args) {
		Logf(format, args);
	}
};

// NewLogger — logger.go:120.
std::shared_ptr<Logger> NewLogger(gostd::io::Writer* output);

// NewNopLogger — logger.go:129. Go returns (*logger)(nil); this port
// returns a Logger whose methods are no-ops, which is what the nil-safe Go
// methods amount to.
std::shared_ptr<Logger> NewNopLogger();

// LogCollector — logcollector.go:9 (fmt.Stringer + Logger).
struct LogCollector : virtual Logger {
	virtual std::string String() const = 0;
};

// NewTestLogger — logcollector.go:23.
std::shared_ptr<LogCollector> NewTestLogger();

// === end slice: testutil-leaves ===

} // namespace tsc::logging
