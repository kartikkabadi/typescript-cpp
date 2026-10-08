// tests_exportsorimports.cpp — port of
// tsc/internal/packagejson/exportsorimports_test.go.
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

// jsonValueToEOI — mirrors jsonValueToExportsOrImports in packagejson.cpp;
// the Go test decodes ExportsOrImports directly via reflection.
packagejson::ExportsOrImports jsonValueToEOI(
    const packagejson::JSONValue& v) {
	packagejson::ExportsOrImports out;
	out.type = v.type;
	out.str = v.str;
	out.num = v.num;
	out.boolean = v.boolean;
	if (v.type == packagejson::JSONValueType::Array && v.array) {
		auto arr =
		    std::make_shared<std::vector<packagejson::ExportsOrImports>>();
		arr->reserve(v.array->size());
		for (auto& e : *v.array) {
			arr->push_back(jsonValueToEOI(e));
		}
		out.array = arr;
	} else if (v.type == packagejson::JSONValueType::Object && v.object) {
		auto obj = std::make_shared<tsc::collections::OrderedMap<
		    std::string, packagejson::ExportsOrImports>>();
		for (auto& key : v.object->Keys()) {
			auto [val, ok] = v.object->Get(key);
			if (ok) obj->Set(key, jsonValueToEOI(*val));
		}
		out.object = obj;
	}
	return out;
}

void testExports(T* t) {
	// Go: var e struct{Imports/Exports ExportsOrImports}
	std::string jsonString = R"({
		"imports": {
			"#foo": {
				"import": "./foo.ts"
			}
		},
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
		}
	})";

	std::map<std::string, packagejson::JSONValue> obj;
	std::string err = tsc::json::unmarshal(jsonString, &obj);
	assert::Assert(t, err.empty(), err);

	packagejson::ExportsOrImports eImports;
	packagejson::ExportsOrImports eExports;
	if (auto it = obj.find("imports"); it != obj.end())
		eImports = jsonValueToEOI(it->second);
	if (auto it = obj.find("exports"); it != obj.end())
		eExports = jsonValueToEOI(it->second);

	assert::Assert(t, eExports.IsSubpaths());
	assert::Equal(t, eExports.AsObject()->Size(), size_t{3});
	assert::Assert(t, eExports.AsObject()->GetOrZero(".").IsConditions());
	assert::Assert(
	    t,
	    eExports.AsObject()
	            ->GetOrZero(".")
	            .AsObject()
	            ->GetOrZero("import")
	            .type == packagejson::JSONValueType::String);
	assert::Equal(
	    t,
	    eExports.AsObject()->GetOrZero("./test").AsArray()->at(2).type,
	    packagejson::JSONValueType::Null);
	assert::Assert(t,
	               eExports.AsObject()->GetOrZero("./null").type ==
	                   packagejson::JSONValueType::Null);

	assert::Assert(t, eImports.IsImports());
	assert::Equal(t, eImports.AsObject()->Size(), size_t{1});
	assert::Assert(
	    t, eImports.AsObject()->GetOrZero("#foo").IsConditions());
	assert::Assert(
	    t,
	    eImports.AsObject()
	            ->GetOrZero("#foo")
	            .AsObject()
	            ->GetOrZero("import")
	            .type == packagejson::JSONValueType::String);
}

void TestExports(T* t) {
	t->Parallel();

	t->Run("UnmarshalJSONV2", [](T* t) {
		t->Parallel();
		testExports(t);
	});
}
REGISTER_UNIT_TEST("packagejson.TestExports", TestExports);

}  // namespace
