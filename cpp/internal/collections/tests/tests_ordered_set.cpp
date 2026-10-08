// tests_ordered_set.cpp — port of tsc/internal/collections/ordered_set_test.go.
// TestOrderedSetWithSizeHint is not ported: it asserts a Go-specific
// allocation ceiling via testing.AllocsPerRun, which has no C++ equivalent.
#include <algorithm>

#include "internal/collections/collections.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;

namespace {

void TestOrderedSet(T* t) {
	t->Parallel();

	tsc::collections::OrderedSet<int> s;

	s.Add(1);
	s.Add(2);
	s.Add(3);

	assert::Assert(t, s.Has(1));
	assert::Assert(t, s.Has(2));
	assert::Assert(t, s.Has(3));

	assert::Assert(t, s.Delete(2));

	// slices.Collect(s.Values()) — Values() is already a vector.
	auto values = s.Values();
	assert::Equal(t, values.size(), size_t(2));
	assert::Assert(t, std::is_sorted(values.begin(), values.end()));

	s.Clear();

	assert::Equal(t, s.Size(), size_t(0));
	assert::Assert(t, !s.Has(1));
	assert::Assert(t, !s.Has(2));
	assert::Assert(t, !s.Has(3));

	auto s2 = s.Clone();
	assert::Equal(t, s2.Size(), size_t(0));
}

} // namespace

REGISTER_UNIT_TEST("collections.TestOrderedSet", TestOrderedSet);
