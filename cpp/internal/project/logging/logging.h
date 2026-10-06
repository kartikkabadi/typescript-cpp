// logging — dep-decl for the ls-autoimport slice: project/logging's LogTree
// (logtree.go). Only the shape the autoimport registry sees is declared;
// logging is owned by the project slice — methods are dep-stubs.
#pragma once

#include <string>
#include <string_view>

#include "internal/ast/ast.h" // tscUnreachable

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

} // namespace tsc::logging
