// logging — dep-decl for the ls-autoimport slice: project/logging's LogTree
// (logtree.go). Only the shape the autoimport registry sees is declared;
// logging is owned by the project slice — methods are dep-stubs.
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/gostd/gostd.h"

namespace tsc::logging {

// === slice: lsp-server — dep decls for the lsp Server's logger ===
// (project/logging is owned by the project slice; that slice owns the real
// implementations.)

// Logger — project/logging/logger.go:10.
struct Logger {
	virtual ~Logger() = default;
	// Error logs an error message.
	virtual void Error(std::initializer_list<gostd::fmtArg> msg) = 0;
	// Errorf logs a formatted error message.
	virtual void Errorf(std::string_view format,
	                    std::initializer_list<gostd::fmtArg> args) = 0;
	// Warn logs a warning message.
	virtual void Warn(std::initializer_list<gostd::fmtArg> msg) = 0;
	// Warnf logs a formatted warning message.
	virtual void Warnf(std::string_view format,
	                   std::initializer_list<gostd::fmtArg> args) = 0;
	// Info logs an info message.
	virtual void Info(std::initializer_list<gostd::fmtArg> msg) = 0;
	// Infof logs a formatted info message.
	virtual void Infof(std::string_view format,
	                   std::initializer_list<gostd::fmtArg> args) = 0;
	// Log prints a line to the output writer with a header.
	virtual void Log(std::initializer_list<gostd::fmtArg> msg) = 0;
	// Logf prints a formatted line to the output writer with a header.
	virtual void Logf(std::string_view format,
	                  std::initializer_list<gostd::fmtArg> args) = 0;
	// Verbose returns the logger if verbose logging is enabled, else nil.
	virtual Logger* Verbose() = 0;
	virtual bool IsVerbose() = 0;
	virtual void SetVerbose(bool enabled) = 0;
};

// === end slice: lsp-server ===

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

} // namespace tsc::logging
