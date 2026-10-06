// registry.h — static self-registration of ported tsctests scenarios.
// Each ported Go test function registers one TscTestCase per tscInput it
// builds; cpp/cmd/tsctestrunner iterates the registry, filters by -run
// regex, and reports PASS/FAIL per case.
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "internal/gostd/testing.h"

namespace tsc::execute::tsctests::tests {

// TscTestCase — {name, fn}. fn drives one tscInput::run(...) call (which
// itself creates a "<subFolder>/<subScenario>" subtest on the given T).
struct TscTestCase {
	std::string name;
	std::function<void(gostd::testing::T*)> fn;
};

// tscTestRegistry — function-local static so registration order across
// TUs is deterministic and there is no static-init-order dependency.
inline std::vector<TscTestCase>& tscTestRegistry() {
	static std::vector<TscTestCase> r;
	return r;
}

struct TscTestRegistrar {
	TscTestRegistrar(std::string name,
	                 std::function<void(gostd::testing::T*)> fn) {
		tscTestRegistry().push_back({std::move(name), std::move(fn)});
	}
};

}  // namespace tsc::execute::tsctests::tests

#define TSCTESTS_CONCAT_INNER(a, b) a##b
#define TSCTESTS_CONCAT(a, b) TSCTESTS_CONCAT_INNER(a, b)

// REGISTER_TSCTEST(name, fn) — register `fn` (a callable taking
// gostd::testing::T*) under `name` (convention: "<GoTestFunc>::<subScenario>").
// fn is __VA_ARGS__: lambda bodies contain braced initializer lists whose
// commas must not be read as macro argument separators.
#define REGISTER_TSCTEST(name, ...)                                       \
	static ::tsc::execute::tsctests::tests::TscTestRegistrar              \
	    TSCTESTS_CONCAT(_tsctest_registrar_, __LINE__)(name, __VA_ARGS__)
