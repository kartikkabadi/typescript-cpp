// Port of tsc/internal/api/jsonvalue_test.go (package api).
#include <memory>
#include <string>
#include <vector>

#include "internal/api/proto.h"
#include "internal/collections/collections.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/packagejson/packagejson.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"

namespace tsc::api {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;

} // namespace

// TestJSONValueToAny — jsonvalue_test.go:13.
void TestJSONValueToAny(T* t) {
	t->Parallel();

	packagejson::JSONValue value;
	auto err = json::unmarshal(
	    std::string_view(
	        R"J({"z":1,"a":{"y":2,"x":3},"m":[{"b":4,"a":5},null],"e":[]})J"),
	    &value);
	assert::Assert(t, err.empty(), "unmarshal failed: " + err);

	auto root = jsonValueToAny(value);
	auto* rootObj = std::get_if<tsoptions::JsonObjectPtr>(&root.v);
	assert::Assert(t, rootObj != nullptr);
	assert::DeepEqual(t, (*rootObj)->keys,
	                  std::vector<std::string>{"z", "a", "m", "e"});
	auto z = (*rootObj)->GetOrZero("z");
	auto* zNum = std::get_if<double>(&z.v);
	assert::Assert(t, zNum != nullptr && *zNum == 1.0);

	auto a = (*rootObj)->GetOrZero("a");
	auto* nested = std::get_if<tsoptions::JsonObjectPtr>(&a.v);
	assert::Assert(t, nested != nullptr);
	assert::DeepEqual(t, (*nested)->keys,
	                  std::vector<std::string>{"y", "x"});

	auto m = (*rootObj)->GetOrZero("m");
	auto* array = std::get_if<tsoptions::JsonArray>(&m.v);
	assert::Assert(t, array != nullptr);
	auto* arrayObject =
	    std::get_if<tsoptions::JsonObjectPtr>(&(*array)[0].v);
	assert::Assert(t, arrayObject != nullptr);
	assert::DeepEqual(t, (*arrayObject)->keys,
	                  std::vector<std::string>{"b", "a"});
	assert::Assert(t,
	               std::holds_alternative<std::monostate>((*array)[1].v));

	auto e = (*rootObj)->GetOrZero("e");
	auto* empty = std::get_if<tsoptions::JsonArray>(&e.v);
	assert::Assert(t, empty != nullptr);
	assert::Equal(t, (int)empty->size(), 0);
}
REGISTER_UNIT_TEST("api.TestJSONValueToAny", TestJSONValueToAny);

} // namespace tsc::api
