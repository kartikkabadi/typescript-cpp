// tests_ordered_map.cpp — port of tsc/internal/collections/ordered_map_test.go.
// Size-hint alloc tests (TestOrderedMapWithSizeHint) are not ported:
// testing.AllocsPerRun has no C++ equivalent (the test asserts a Go-specific
// allocation ceiling).
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using tsc::collections::OrderedMap;

namespace {

// padInt — fmt.Sprintf("%10d", n).
std::string padInt(int n) {
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%10d", n);
	return buf;
}

void TestOrderedMap(T* t) {
	t->Parallel();

	OrderedMap<int, std::string> m;

	assert::Assert(t, !m.Has(1));

	const int N = 1000;
	const int start = 1;
	const int end = start + N;

	// Seed the map with ascending keys and values for easier testing.
	for (int i = start; i < end; i++) {
		m.Set(i, padInt(i));
	}

	assert::Equal(t, m.Size(), size_t(N));

	// Attempt to overwrite existing keys in reverse order.
	for (int i = end - 1; i >= start; i--) {
		m.Set(i, padInt(i));
	}

	assert::Equal(t, m.Size(), size_t(N));

	for (int i = start; i < end; i++) {
		auto [v, ok] = m.Get(i);
		assert::Assert(t, ok);
		assert::Equal(t, *v, padInt(i));
	}

	for (auto& [k, v] : m.Entries()) {
		assert::Equal(t, v, padInt(k));
	}

	auto keys = m.Keys();
	assert::Equal(t, keys.size(), size_t(N));
	assert::Assert(t, std::is_sorted(keys.begin(), keys.end()));

	auto values = m.Values();
	assert::Equal(t, values.size(), size_t(N));
	assert::Assert(t, std::is_sorted(values.begin(), values.end()));

	int firstKey = 0;
	for (auto k : m.Keys()) {
		firstKey = k;
		break;
	}
	assert::Equal(t, firstKey, start);

	std::string firstValue;
	for (auto& v : m.Values()) {
		firstValue = v;
		break;
	}
	assert::Equal(t, firstValue, padInt(start));

	for (auto& [k, v] : m.Entries()) {
		firstKey = k;
		firstValue = v;
		break;
	}

	assert::Equal(t, firstKey, start);
	assert::Equal(t, firstValue, padInt(start));

	for (int i = start + 1; i < end; i++) {
		auto [v, ok] = m.Delete(i);
		assert::Assert(t, ok);
		assert::Equal(t, v, padInt(i));
		assert::Assert(t, !m.Has(i));

		auto [v2, ok2] = m.Get(i);
		assert::Assert(t, !ok2);
		assert::Equal(t, v2 == nullptr ? "" : *v2, std::string(""));

		auto [v3, ok3] = m.Delete(i);
		assert::Assert(t, !ok3);
		assert::Equal(t, v3, std::string(""));
	}

	assert::Equal(t, m.Size(), size_t(1));
	assert::Assert(t, m.Has(start));

	auto [v, ok] = m.Delete(start);
	assert::Assert(t, ok);
	assert::Equal(t, v, padInt(start));

	assert::Equal(t, m.Size(), size_t(0));
}

void TestOrderedMapClone(T* t) {
	t->Parallel();

	OrderedMap<int, std::string> m;
	m.Set(1, "one");
	m.Set(2, "two");

	auto clone = m.Clone();

	assert::Equal(t, clone.Size(), size_t(2));
	assert::Assert(t, clone.Keys() == std::vector<int>({1, 2}));
	assert::Assert(t, clone.Values() ==
	                    std::vector<std::string>({"one", "two"}));

	auto [v, ok] = clone.Get(1);
	assert::Assert(t, ok);
	assert::Equal(t, *v, std::string("one"));

	m.Delete(1);

	assert::Equal(t, m.Size(), size_t(1));
	assert::Equal(t, clone.Size(), size_t(2));
	assert::Assert(t, clone.Keys() == std::vector<int>({1, 2}));
	assert::Assert(t, clone.Values() ==
	                    std::vector<std::string>({"one", "two"}));
}

void TestOrderedMapClear(T* t) {
	t->Parallel();

	OrderedMap<int, std::string> m;
	m.Set(1, "one");
	m.Set(2, "two");

	m.Clear();

	assert::Equal(t, m.Size(), size_t(0));
}

void TestOrderedMapUnmarshalJSON(T* t) {
	t->Parallel();

	t->Run("UnmarshalJSONV2", [](T* t) {
		t->Parallel();
		// Go `map[string]any` values decode via the DOM substrate; the
		// C++ port stores raw json.Value (raw source text) — "1" for
		// Go's float64(1).
		OrderedMap<std::string, tsc::json::Value> m;
		auto err = tsc::json::unmarshal(
		    R"({"a": 1, "b": "two", "c": { "d": 4 } })", &m);
		assert::Assert(t, err.empty(), err);

		assert::Equal(t, m.Size(), size_t(3));
		assert::Equal(t, std::string(m.GetOrZero("a")), std::string("1"));

		err = tsc::json::unmarshal("null", &m);
		assert::Assert(t, err.empty(), err);

		err = tsc::json::unmarshal("\"foo\"", &m);
		assert::Assert(t,
		               err.find("cannot unmarshal non-object JSON "
		                        "value into Map") != std::string::npos,
		               err);

		OrderedMap<int, tsc::json::Value> invalidMap;
		err = tsc::json::unmarshal(R"({"a": 1, "b": "two"})",
		                           &invalidMap);
		assert::Assert(t, err.find("unmarshal") != std::string::npos,
		               err);
	});
}

} // namespace

REGISTER_UNIT_TEST("collections.TestOrderedMap", TestOrderedMap);
REGISTER_UNIT_TEST("collections.TestOrderedMapClone", TestOrderedMapClone);
REGISTER_UNIT_TEST("collections.TestOrderedMapClear", TestOrderedMapClear);
REGISTER_UNIT_TEST("collections.TestOrderedMapUnmarshalJSON",
                   TestOrderedMapUnmarshalJSON);
