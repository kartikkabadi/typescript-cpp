// util.h — small shared helpers for the ported tsctests scenarios:
// the newTscEdit convenience (tscwatch_test.go:670) plus using-decl
// conveniences for the FileMap/tscEdit/tscInput types.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "internal/execute/tsctests/tsctests.h"
#include "internal/testutil/stringtestutil/stringtestutil.h"

namespace tsc::execute::tsctests::tests {

using ::tsc::testutil::stringtestutil::Dedent;

// newTscEdit — tscwatch_test.go:670.
inline tscEdit* newTscEdit(const std::string& name,
                           std::function<void(TestSys*)> edit) {
	return new tscEdit{name, std::nullopt, std::move(edit), ""};
}

// commandLineArgsV — marks an explicitly-empty (non-nil) commandLineArgs
// override on a tscEdit, matching Go `commandLineArgs: []string{...}`.
inline std::vector<std::string> commandLineArgsV(
    std::initializer_list<std::string> args) {
	return std::vector<std::string>(args);
}

}  // namespace tsc::execute::tsctests::tests
