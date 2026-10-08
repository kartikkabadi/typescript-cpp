// tests_syncmap.cpp — port of tsc/internal/collections/syncmap_test.go.
#include <any>
#include <string>

#include "internal/collections/collections.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;

namespace {

void TestSyncMapWithNil(T* t) {
	t->Parallel();

	tsc::collections::SyncMap<std::string, std::any> m;

	auto [got1, ok1] = m.Load("foo");
	assert::Assert(t, !ok1);
	assert::Assert(t, !got1.has_value());

	m.Store("foo", std::any{});

	auto [got2, ok2] = m.Load("foo");
	assert::Assert(t, ok2);
	assert::Assert(t, !got2.has_value());

	auto [too, loaded] = m.LoadOrStore("too", std::any{});
	assert::Assert(t, !loaded);
	assert::Assert(t, !too.has_value());

	m.Range([](const std::string&, const std::any&) { return true; });
}

} // namespace

REGISTER_UNIT_TEST("collections.TestSyncMapWithNil", TestSyncMapWithNil);
