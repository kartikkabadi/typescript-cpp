// Port of tsc/internal/project/dirty/syncmap_test.go.
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "internal/gostd/testing.h"
#include "internal/project/dirty/dirty.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

// testValue is a simple cloneable type for testing.
struct testValue {
	std::string data;
	testValue* Clone() const { return new testValue{data}; }
};

using Entry = tsc::dirty::SyncMapEntry<std::string, testValue*>;
using EntryPtr = std::shared_ptr<Entry>;
using Map = std::unordered_map<std::string, testValue*>;

EntryPtr mustLoad(tsc::dirty::SyncMap<std::string, testValue*>* m,
                  const std::string& key) {
	return m->Load(key).first;
}

void TestSyncMapProxyFor(tsc::gostd::testing::T* t) {
	namespace assert = tsc::gotest::assert;

	t->Parallel();

	t->Run("proxy for race condition", [](tsc::gostd::testing::T* t) {
		t->Parallel();

		// Create a sync map with a base value
		Map base{
		    {"key1", new testValue{"original"}},
		};
		auto* syncMap = tsc::dirty::newSyncMap<std::string, testValue*>(base);

		// Load the same entry from multiple goroutines to simulate race
		// condition.
		EntryPtr entry1, entry2;
		// wg.Add(2)
		std::thread g1([&] {
			try {
				auto [e, ok] = syncMap->Load("key1");
				entry1 = e;
				assert::Assert(t, ok, "entry1 should be loaded");
			} catch (const tsc::gostd::testing::testGoexit&) {
			}
		});
		std::thread g2([&] {
			try {
				auto [e, ok] = syncMap->Load("key1");
				entry2 = e;
				assert::Assert(t, ok, "entry2 should be loaded");
			} catch (const tsc::gostd::testing::testGoexit&) {
			}
		});
		g1.join();
		g2.join();

		// Both entries should exist and have the same initial value
		assert::Equal(t, std::string("original"), entry1->Value()->data);
		assert::Equal(t, std::string("original"), entry2->Value()->data);
		assert::Equal(t, false, entry1->Dirty());
		assert::Equal(t, false, entry2->Dirty());

		// Now try to change both entries concurrently to trigger the proxy
		// mechanism. (This change doesn't actually have to be concurrent to
		// test the proxy behavior, but might exercise concurrency safety in
		// -race mode.)
		std::thread c1([&] {
			entry1->Change(
			    [](testValue*& v) { v->data = "changed_by_entry1"; });
		});
		std::thread c2([&] {
			entry2->Change(
			    [](testValue*& v) { v->data = "changed_by_entry2"; });
		});
		c1.join();
		c2.join();

		// After the race, one entry should have proxyFor set and both
		// should reflect the same final state. The exact final value depends
		// on which goroutine wins the race, but both entries should be
		// consistent.
		std::string finalValue1 = entry1->Value()->data;
		std::string finalValue2 = entry2->Value()->data;
		assert::Equal(t, finalValue1, finalValue2,
		              "both entries should have the same final value");

		// Both entries should be marked as dirty
		assert::Equal(t, true, entry1->Dirty());
		assert::Equal(t, true, entry2->Dirty());

		// At least one entry should have proxyFor set (the one that lost
		// the race)
		bool hasProxy =
		    (entry1->proxyFor != nullptr) || (entry2->proxyFor != nullptr);
		assert::Assert(t, hasProxy,
		               "at least one entry should have proxyFor set");

		// If entry1 has a proxy, it should point to entry2, and vice versa
		if (entry1->proxyFor != nullptr) {
			assert::Equal(t, entry2, entry1->proxyFor,
			              "entry1 should proxy to entry2");
		}
		if (entry2->proxyFor != nullptr) {
			assert::Equal(t, entry1, entry2->proxyFor,
			              "entry2 should proxy to entry1");
		}
	});

	t->Run("proxy operations delegation", [](tsc::gostd::testing::T* t) {
		t->Parallel();

		Map base{
		    {"key1", new testValue{"original"}},
		};
		auto* syncMap = tsc::dirty::newSyncMap<std::string, testValue*>(base);

		// Load two entries for the same key
		auto [entry1, ok1] = syncMap->Load("key1");
		assert::Assert(t, ok1);
		auto [entry2, ok2] = syncMap->Load("key1");
		assert::Assert(t, ok2);

		// Force one to become a proxy by making them both dirty in sequence
		entry1->Change(
		    [](testValue*& v) { v->data = "changed_by_entry1"; });
		entry2->Change(
		    [](testValue*& v) { v->data = "changed_by_entry2"; });

		// Determine which is the proxy and which is the target
		EntryPtr proxy, target;
		if (entry1->proxyFor != nullptr) {
			proxy = entry1;
			target = entry2;
		} else {
			proxy = entry2;
			target = entry1;
		}

		// Test that proxy operations are delegated to the target.
		// Change through proxy should affect target.
		proxy->Change(
		    [](testValue*& v) { v->data = "changed_through_proxy"; });
		assert::Equal(t, std::string("changed_through_proxy"),
		              target->Value()->data);
		assert::Equal(t, std::string("changed_through_proxy"),
		              proxy->Value()->data);

		// ChangeIf through proxy should work
		bool changed = proxy->ChangeIf(
		    [](const testValue* v) {
			    return v->data == "changed_through_proxy";
		    },
		    [](testValue*& v) { v->data = "conditional_change"; });
		assert::Assert(t, changed);
		assert::Equal(t, std::string("conditional_change"),
		              target->Value()->data);
		assert::Equal(t, std::string("conditional_change"),
		              proxy->Value()->data);

		// Dirty status should be consistent
		assert::Equal(t, target->Dirty(), proxy->Dirty());

		// Locked operations should work through proxy
		proxy->Locked([](tsc::dirty::IValue<testValue*>* v) {
			v->Change([](testValue*& val) { val->data = "locked_change"; });
		});
		assert::Equal(t, std::string("locked_change"), target->Value()->data);
		assert::Equal(t, std::string("locked_change"), proxy->Value()->data);
	});

	t->Run("proxy delete operations", [](tsc::gostd::testing::T* t) {
		t->Parallel();

		Map base{
		    {"key1", new testValue{"original"}},
		};
		auto* syncMap = tsc::dirty::newSyncMap<std::string, testValue*>(base);

		// Load two entries and make one a proxy
		auto entry1 = mustLoad(syncMap, "key1");
		auto entry2 = mustLoad(syncMap, "key1");

		entry1->Change([](testValue*& v) { v->data = "modified"; });
		entry2->Change([](testValue*& v) { v->data = "modified2"; });

		// Determine which is the proxy
		EntryPtr proxy;
		if (entry1->proxyFor != nullptr) {
			proxy = entry1;
		} else {
			proxy = entry2;
		}

		// Delete through proxy should affect target
		proxy->Delete();

		// Both should reflect the deletion
		auto [e, exists] = syncMap->Load("key1");
		assert::Equal(t, false, exists,
		              "key should be deleted from sync map");

		// DeleteIf through proxy should work
		Map base2{
		    {"key2", new testValue{"test"}},
		};
		auto* syncMap2 =
		    tsc::dirty::newSyncMap<std::string, testValue*>(base2);

		auto entry3 = mustLoad(syncMap2, "key2");
		auto entry4 = mustLoad(syncMap2, "key2");

		entry3->Change([](testValue*& v) { v->data = "modified"; });
		entry4->Change([](testValue*& v) { v->data = "modified2"; });

		EntryPtr proxy2;
		if (entry3->proxyFor != nullptr) {
			proxy2 = entry3;
		} else {
			proxy2 = entry4;
		}

		proxy2->DeleteIf([](const testValue* v) {
			return v->data == "modified2" || v->data == "modified";
		});

		auto [e2, exists2] = syncMap2->Load("key2");
		assert::Equal(t, false, exists2,
		              "key2 should be deleted conditionally");
	});

	t->Run("no proxy when no race", [](tsc::gostd::testing::T* t) {
		t->Parallel();

		Map base{
		    {"key1", new testValue{"original"}},
		};
		auto* syncMap = tsc::dirty::newSyncMap<std::string, testValue*>(base);

		// Load and modify a single entry - no race condition
		auto [entry, ok] = syncMap->Load("key1");
		assert::Assert(t, ok);

		entry->Change([](testValue*& v) { v->data = "changed"; });

		// Should not have a proxy since there was no race
		assert::Assert(t, entry->proxyFor == nullptr,
		               "entry should not have proxyFor when no race occurs");
		assert::Equal(t, true, entry->Dirty());
		assert::Equal(t, std::string("changed"), entry->Value()->data);
	});
}

}  // namespace

REGISTER_UNIT_TEST("project/dirty.TestSyncMapProxyFor", TestSyncMapProxyFor);
