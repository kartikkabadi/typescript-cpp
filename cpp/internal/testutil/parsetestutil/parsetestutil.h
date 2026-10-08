// parsetestutil.h — port of tsc/internal/testutil/parsetestutil/
// parsetestutil.go: parse a source string into a SourceFile, assert on its
// parse diagnostics, and strip synthetic locations from a subtree.
#pragma once

#include <string>
#include <string_view>

#include "internal/ast/ast.h"
#include "internal/gostd/testing.h"

namespace tsc::testutil::parsetestutil {

// ParseTypeScript — parsetestutil.go:15. Simplifies parsing an input string
// into a SourceFile for testing purposes.
SourceFile* ParseTypeScript(std::string_view text, bool jsx);

// CheckDiagnostics — parsetestutil.go:25. Asserts that the given file has
// no parse diagnostics.
void CheckDiagnostics(gostd::testing::T* t, SourceFile* file);

// CheckDiagnosticsMessage — parsetestutil.go:37. Asserts that the given
// file has no parse diagnostics and asserts the given message.
void CheckDiagnosticsMessage(gostd::testing::T* t, SourceFile* file,
                             std::string_view message);

// MarkSyntheticRecursive — parsetestutil.go:86. Sets the Loc of the given
// node and every Node in its subtree to an undefined TextRange (-1,-1).
void MarkSyntheticRecursive(Node* node);

}  // namespace tsc::testutil::parsetestutil
