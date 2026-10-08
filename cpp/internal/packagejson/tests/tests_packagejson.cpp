// tests_packagejson.cpp — port of tsc/internal/packagejson/packagejson_test.go.
// BenchmarkPackageJSON is not ported: it does not run under `go test`.
#include <string>
#include <vector>

#include "internal/gostd/testing.h"
#include "internal/packagejson/packagejson.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace packagejson = tsc::packagejson;

namespace {

// --- DeepEqual helpers (assert.DeepEqual with cmpopts.IgnoreUnexported) ---

template <typename T>
bool expectedEqual(const packagejson::Expected<T>& a,
                   const packagejson::Expected<T>& b) {
	return a.Valid == b.Valid && a.Null == b.Null && a.Value == b.Value;
}

template <typename T>
void assertExpectedEqual(T* t, const packagejson::Expected<T>& got,
                         const packagejson::Expected<T>& want,
                         const char* field) {
	assert::Assert(t, expectedEqual(got, want), field);
}

bool jsonValueEqual(const packagejson::JSONValue& a,
                    const packagejson::JSONValue& b) {
	if (a.type != b.type || a.str != b.str || a.num != b.num ||
	    a.boolean != b.boolean) {
		return false;
	}
	if (bool(a.array) != bool(b.array) ||
	    bool(a.object) != bool(b.object)) {
		return false;
	}
	if (a.array) {
		if (a.array->size() != b.array->size()) return false;
		for (size_t i = 0; i < a.array->size(); i++) {
			if (!jsonValueEqual((*a.array)[i], (*b.array)[i]))
				return false;
		}
	}
	if (a.object) {
		if (a.object->Size() != b.object->Size()) return false;
		for (auto& key : a.object->Keys()) {
			auto [av, aok] = a.object->Get(key);
			auto [bv, bok] = b.object->Get(key);
			if (!aok || !bok || !jsonValueEqual(*av, *bv))
				return false;
		}
	}
	return true;
}

bool eoiEqual(const packagejson::ExportsOrImports& a,
              const packagejson::ExportsOrImports& b) {
	// objKind is lazily initialized and unexported — excluded like Go's
	// cmpopts.IgnoreUnexported.
	if (a.type != b.type || a.str != b.str || a.num != b.num ||
	    a.boolean != b.boolean) {
		return false;
	}
	if (bool(a.array) != bool(b.array) ||
	    bool(a.object) != bool(b.object)) {
		return false;
	}
	if (a.array) {
		if (a.array->size() != b.array->size()) return false;
		for (size_t i = 0; i < a.array->size(); i++) {
			if (!eoiEqual((*a.array)[i], (*b.array)[i])) return false;
		}
	}
	if (a.object) {
		if (a.object->Size() != b.object->Size()) return false;
		for (auto& key : a.object->Keys()) {
			auto [av, aok] = a.object->Get(key);
			auto [bv, bok] = b.object->Get(key);
			if (!aok || !bok || !eoiEqual(*av, *bv)) return false;
		}
	}
	return true;
}

bool contentMapperFieldsEqual(
    const packagejson::ContentMapperFields& a,
    const packagejson::ContentMapperFields& b) {
	return expectedEqual(a.Exec, b.Exec) &&
	       expectedEqual(a.CompilerOptions, b.CompilerOptions) &&
	       expectedEqual(a.DynamicConfig, b.DynamicConfig);
}

bool fieldsEqual(const packagejson::Fields& a,
                 const packagejson::Fields& b) {
	return expectedEqual(a.Name, b.Name) &&
	       expectedEqual(a.Version, b.Version) &&
	       expectedEqual(a.Type, b.Type) &&
	       expectedEqual(a.TSConfig, b.TSConfig) &&
	       expectedEqual(a.Main, b.Main) &&
	       expectedEqual(a.Types, b.Types) &&
	       expectedEqual(a.Typings, b.Typings) &&
	       jsonValueEqual(a.TypesVersions, b.TypesVersions) &&
	       eoiEqual(a.Imports, b.Imports) &&
	       eoiEqual(a.Exports, b.Exports) &&
	       expectedEqual(a.Dependencies, b.Dependencies) &&
	       expectedEqual(a.DevDependencies, b.DevDependencies) &&
	       expectedEqual(a.PeerDependencies, b.PeerDependencies) &&
	       expectedEqual(a.OptionalDependencies,
	                     b.OptionalDependencies) &&
	       a.ContentMapper.Valid == b.ContentMapper.Valid &&
	       a.ContentMapper.Null == b.ContentMapper.Null &&
	       (!a.ContentMapper.Valid ||
	        contentMapperFieldsEqual(a.ContentMapper.Value,
	                                 b.ContentMapper.Value));
}

void TestParse(T* t) {
	t->Parallel();

	struct testCase {
		const char* name;
		const char* content;
		packagejson::Fields want;
	};

	auto expectedStr = [](const char* s) {
		return packagejson::ExpectedOf(std::string(s));
	};

	std::vector<testCase> tests;
	{
		packagejson::Fields want;
		want.Name = expectedStr("test-package");
		want.Version = expectedStr("1.0.0");
		tests.push_back({"duplicate names", R"({
			"name": "test-package",
			"name": "test-package",
			"version": "1.0.0"
		})",
		                 want});
	}
	{
		packagejson::Fields want;
		want.Name = expectedStr("test-package");
		packagejson::ContentMapperFields cm;
		cm.Exec = packagejson::ExpectedOf(
		    std::vector<std::string>{"mapper"});
		cm.DynamicConfig = packagejson::ExpectedOf(true);
		want.ContentMapper = packagejson::ExpectedOf(cm);
		tests.push_back({"content mapper", R"({
			"name": "test-package",
			"typescript": {
				"contentMapper": { "exec": ["mapper"], "dynamicConfig": true }
			}
		})",
		                 want});
	}
	{
		packagejson::Fields want;
		want.Name = expectedStr("test-package");
		tests.push_back({"invalid typescript field is ignored",
		                 R"({ "name": "test-package", "typescript": "invalid" })",
		                 want});
	}

	for (auto& tt : tests) {
		t->Run(tt.name, [&tt](T* t) {
			t->Parallel();

			auto [got, ok] = packagejson::Parse(tt.content);
			assert::Assert(t, ok);
			assert::Assert(t, fieldsEqual(got, tt.want));
		});
	}
}
REGISTER_UNIT_TEST("packagejson.TestParse", TestParse);

}  // namespace
