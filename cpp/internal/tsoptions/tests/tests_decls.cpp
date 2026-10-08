// Port of tsc/internal/tsoptions/decls_test.go (package tsoptions_test).
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"

namespace {

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace tsoptions = tsc::tsoptions;

std::string toLowerStr(std::string_view s) {
	std::string r(s);
	for (auto& c : r) {
		if (c >= 'A' && c <= 'Z') c += 32;
	}
	return r;
}

// checkCompilerOptionJsonTagName — decls_test.go:71. In C++ the
// compilerOptionFieldInfos table's jsonName plays the role of the Go json
// tag (which is always `name,omitzero`), so the check is jsonName == name.
void checkCompilerOptionJsonTagName(
    T* t, const tsoptions::compilerOptionFieldInfo& field,
    const std::string& name) {
	t->Helper();
	assert::Equal(t, std::string(field.jsonName), name,
	              "Field " + std::string(field.name) +
	                  " has json tag mismatch");
}

// TestCompilerOptionsDeclaration — decls_test.go:11.
void TestCompilerOptionsDeclaration(T* t) {
	std::unordered_map<std::string, const tsoptions::CommandLineOption*> decls;
	for (const auto* decl : tsoptions::OptionsDeclarations()) {
		decls[toLowerStr(decl->Name)] = decl;
	}

	std::vector<std::string> internalOptions = {
	    "allowNonTsExtensions", "build",      "configFilePath",
	    "noDtsResolution",      "noEmitForJsFiles", "pathsBasePath",
	    "suppressOutputPathCheck", "build",
	};

	std::unordered_map<std::string, std::string> internalOptionsMap;
	for (const auto& opt : internalOptions) {
		internalOptionsMap[toLowerStr(opt)] = opt;
	}

	for (const auto& field : tsoptions::compilerOptionFieldInfos()) {
		std::string lowerName = toLowerStr(field.name);

		auto it = decls.find(lowerName);
		const tsoptions::CommandLineOption* decl =
		    it != decls.end() ? it->second : nullptr;
		if (decl == nullptr) {
			auto iit = internalOptionsMap.find(lowerName);
			if (iit != internalOptionsMap.end()) {
				checkCompilerOptionJsonTagName(t, field, iit->second);
				continue;
			}
			t->Error({std::string("CompilerOptions.") +
			          std::string(field.name) +
			          " has no options declaration"});
			continue;
		}
		decls.erase(lowerName);

		checkCompilerOptionJsonTagName(t, field, decl->Name);
	}

	std::vector<std::string> skippedOptions = {
	    "plugins",
	};

	for (const auto& opt : skippedOptions) {
		decls.erase(toLowerStr(opt));
	}

	for (const auto& [_, decl] : decls) {
		t->Error({std::string("Option declaration ") + decl->Name +
		          " is not present in CompilerOptions"});
	}
}

}  // namespace

REGISTER_UNIT_TEST("tsoptions.TestCompilerOptionsDeclaration",
                   TestCompilerOptionsDeclaration);
