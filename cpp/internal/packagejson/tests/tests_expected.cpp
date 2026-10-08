// tests_expected.cpp — port of tsc/internal/packagejson/expected_test.go.
#include <map>
#include <optional>
#include <string>

#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/packagejson/packagejson.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace packagejson = tsc::packagejson;

namespace {

// expectedFromNode — mirrors setExpectedFrom in packagejson.cpp (the C++
// decode of Expected.UnmarshalJSON), which the Go test reaches through
// encoding/json reflection on an ad-hoc struct.
template <typename T, typename Convert>
void expectedFromNode(const packagejson::JSONValue& node,
                      packagejson::Expected<T>& out,
                      const Convert& convert) {
	if (node.type == packagejson::JSONValueType::Null) {
		out = packagejson::Expected<T>{};
		out.Null = true;
		out.actualJSONType = "null";
		return;
	}
	out.actualJSONType = packagejson::JSONValueTypeString(node.type);
	if (auto value = convert(node); value.has_value()) {
		out.Value = std::move(*value);
		out.Valid = true;
	}
}

std::optional<std::string> nodeAsString(
    const packagejson::JSONValue& v) {
	if (v.type == packagejson::JSONValueType::String) return v.str;
	return std::nullopt;
}

void TestExpected(T* t) {
	t->Parallel();

	// Go: var p struct{Name/Version/Exports/Main Expected[...]}; the C++
	// equivalent decodes the object member-wise through JSONValue.
	std::string jsonString = R"({
		"name": "test",
		"version": 2,
		"exports": null
	})";

	std::map<std::string, packagejson::JSONValue> obj;
	std::string err = tsc::json::unmarshal(jsonString, &obj);
	assert::Assert(t, err.empty(), err);

	packagejson::Expected<std::string> pName;
	packagejson::Expected<std::string> pVersion;
	packagejson::Expected<packagejson::JSONValue> pExports;
	packagejson::Expected<std::string> pMain;

	auto member = [&obj](const char* key) -> const packagejson::JSONValue& {
		static const packagejson::JSONValue absent{};
		auto it = obj.find(key);
		return it != obj.end() ? it->second : absent;
	};

	expectedFromNode(member("name"), pName, nodeAsString);
	expectedFromNode(member("version"), pVersion, nodeAsString);
	expectedFromNode(
	    member("exports"), pExports,
	    [](const packagejson::JSONValue& v)
	        -> std::optional<packagejson::JSONValue> { return v; });
	expectedFromNode(member("main"), pMain, nodeAsString);

	assert::Equal(t, pName.Valid, true);
	assert::Equal(t, pName.Value, std::string("test"));

	assert::Equal(t, pVersion.Valid, false);
	assert::Equal(t, pVersion.Value, std::string(""));

	assert::Assert(t, pExports.Null);
	assert::Equal(t, pExports.Valid, false);

	assert::Equal(t, pMain.Valid, false);
	assert::Equal(t, pMain.Null, false);
	assert::Equal(t, pMain.Value, std::string(""));
}
REGISTER_UNIT_TEST("packagejson.TestExpected", TestExpected);

}  // namespace
