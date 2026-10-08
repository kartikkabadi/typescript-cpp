// Port of tsc/internal/tsoptions/parsinghelpers_test.go (package tsoptions).
#include <string>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"

namespace {

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace tsoptions = tsc::tsoptions;
using tsc::CompilerOptions;

// TestParseCompilerOptionNoMissingFields — parsinghelpers_test.go:12.
// Go iterates reflect.VisibleFields(core.CompilerOptions) and uses each
// field's json tag name; the C++ port's compilerOptionFieldInfos() table is
// the same enumeration (Go field name + json tag name).
void TestParseCompilerOptionNoMissingFields(T* t) {
	std::vector<std::string> missingKeys;
	for (const auto& field : tsoptions::compilerOptionFieldInfos()) {
		std::string keyName(field.name);
		if (!field.jsonName.empty()) {
			keyName = std::string(field.jsonName);
		}
		CompilerOptions co{};
		bool found =
		    tsoptions::parseCompilerOptions(keyName, field.get(&co),
		                                    &co);
		if (!found) {
			missingKeys.push_back(keyName);
		}
	}
	if (!missingKeys.empty()) {
		std::string joined;
		for (size_t i = 0; i < missingKeys.size(); i++) {
			if (i) joined += ", ";
			joined += missingKeys[i];
		}
		t->Error({std::string(
		    "The following keys are missing entries in the "
		    "ParseCompilerOptions switch statement:\n") +
		    joined});
	}
}

}  // namespace

REGISTER_UNIT_TEST("tsoptions.TestParseCompilerOptionNoMissingFields",
                   TestParseCompilerOptionNoMissingFields);
