// emittestutil.h — port of tsc/internal/testutil/emittestutil/
// emittestutil.go: assert that pretty-printing a SourceFile reproduces the
// expected output (and reparses cleanly).
#pragma once

#include <string_view>

#include "internal/ast/ast.h"
#include "internal/gostd/testing.h"
#include "internal/printer/printer.h"

namespace tsc::testutil::emittestutil {

// CheckEmit — emittestutil.go:15. Checks that pretty-printing the given
// file matches the expected output.
void CheckEmit(gostd::testing::T* t, printer::EmitContext* emitContext,
               SourceFile* file, std::string_view expected);

}  // namespace tsc::testutil::emittestutil
