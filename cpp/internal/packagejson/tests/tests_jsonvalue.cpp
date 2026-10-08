// tests_jsonvalue.cpp — port of tsc/internal/packagejson/jsonvalue_test.go.
#include <map>
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

void testJSONValue(T* t) {
	// Go: var p struct{Private/False/Name/Version/Exports/Imports/
	// NotPresent JSONValue}
	std::string jsonString = R"({
		"private": true,
		"false": false,
		"name": "test",
		"version": 2,
		"exports": {
			".": {
				"import": "./test.ts",
				"default": "./test.ts"
			},
			"./test": [
				"./test1.ts",
				"./test2.ts",
				null
			],
			"./null": null
		},
		"imports": null
	})";

	std::map<std::string, packagejson::JSONValue> obj;
	std::string err = tsc::json::unmarshal(jsonString, &obj);
	assert::Assert(t, err.empty(), err);

	auto member = [&obj](const char* key) -> const packagejson::JSONValue& {
		static const packagejson::JSONValue absent{};
		auto it = obj.find(key);
		return it != obj.end() ? it->second : absent;
	};

	const auto& pPrivate = member("private");
	const auto& pName = member("name");
	const auto& pVersion = member("version");
	const auto& pExports = member("exports");
	const auto& pImports = member("imports");
	const auto& pNotPresent = member("notPresent");

	assert::Equal(t, pPrivate.type,
	              packagejson::JSONValueType::Boolean);
	assert::Equal(t, pPrivate.boolean, true);

	assert::Equal(t, pName.type, packagejson::JSONValueType::String);
	assert::Equal(t, pName.str, std::string("test"));

	assert::Equal(t, pVersion.type,
	              packagejson::JSONValueType::Number);
	assert::Equal(t, pVersion.num, 2.0);

	assert::Equal(t, pExports.type,
	              packagejson::JSONValueType::Object);
	assert::Equal(t, pExports.AsObject()->Size(), size_t{3});
	assert::Equal(
	    t, pExports.AsObject()->GetOrZero(".").type,
	    packagejson::JSONValueType::Object);
	assert::Equal(t,
	              pExports.AsObject()
	                  ->GetOrZero(".")
	                  .AsObject()
	                  ->GetOrZero("import")
	                  .str,
	              std::string("./test.ts"));

	assert::Equal(
	    t, pExports.AsObject()->GetOrZero("./test").type,
	    packagejson::JSONValueType::Array);
	assert::Equal(
	    t,
	    pExports.AsObject()->GetOrZero("./test").AsArray()->size(),
	    size_t{3});
	assert::Equal(
	    t,
	    pExports.AsObject()->GetOrZero("./test").AsArray()->at(0).str,
	    std::string("./test1.ts"));
	assert::Equal(
	    t,
	    pExports.AsObject()->GetOrZero("./test").AsArray()->at(1).str,
	    std::string("./test2.ts"));
	assert::Equal(
	    t,
	    pExports.AsObject()->GetOrZero("./test").AsArray()->at(2).type,
	    packagejson::JSONValueType::Null);

	assert::Equal(
	    t, pExports.AsObject()->GetOrZero("./null").type,
	    packagejson::JSONValueType::Null);

	assert::Equal(t, pImports.type, packagejson::JSONValueType::Null);
	// Go: p.Imports.Value == nil — the null slot carries no payload.
	assert::Assert(t, pImports.object == nullptr &&
	                  pImports.array == nullptr);

	assert::Equal(t, pNotPresent.type,
	              packagejson::JSONValueType::NotPresent);
	assert::Assert(t, pNotPresent.object == nullptr &&
	                  pNotPresent.array == nullptr);
}

void TestJSONValue(T* t) {
	t->Parallel();

	t->Run("UnmarshalJSONV2", [](T* t) {
		t->Parallel();
		testJSONValue(t);
	});
}
REGISTER_UNIT_TEST("packagejson.TestJSONValue", TestJSONValue);

}  // namespace
