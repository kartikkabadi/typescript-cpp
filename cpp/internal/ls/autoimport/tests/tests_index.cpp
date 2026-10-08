// Port of tsc/internal/ls/autoimport/index_test.go.
//
// The Go test instantiates the generic `Index[T]` on a `*testEntry` local
// type; the C++ port specializes Index on `std::shared_ptr<Export>` (the only
// instantiation the Go program makes in practice), so testEntry fields map
// onto Export: name -> localName (drives Name()), package_ -> PackageName.
#include <memory>
#include <string>

#include "internal/gostd/testing.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc::ls::autoimport;
using namespace tsc;

namespace {

std::shared_ptr<Export> newTestEntry(const std::string& name,
                                     const std::string& package_) {
	auto e = std::make_shared<Export>();
	e->localName = name;
	e->PackageName = package_;
	return e;
}

}  // namespace

static void TestIndexClone(T* t) {
	t->Parallel();

	t->Run("filters entries by package", [](T* t) {
		t->Parallel();

		Index idx;
		idx.insertAsWords(newTestEntry("fooBar", "pkg-a"));
		idx.insertAsWords(newTestEntry("bazQux", "pkg-b"));
		idx.insertAsWords(newTestEntry("fooQux", "pkg-a"));

		// Clone excluding pkg-b
		auto cloned = Clone(&idx, [](const std::shared_ptr<Export>& e) {
			return e->PackageName != "pkg-b";
		});

		// Original should have all 3 entries
		gotest::assert::Equal(t, int(idx.entries.size()), 3);

		// Cloned should have 2 entries (only pkg-a)
		gotest::assert::Equal(t, int(cloned->entries.size()), 2);

		// Search should work on cloned index
		auto results = cloned->Find("fooBar", true);
		gotest::assert::Equal(t, int(results.size()), 1);
		gotest::assert::Equal(t, results[0]->Name(),
		                      std::string("fooBar"));

		// bazQux should not be in cloned index
		results = cloned->Find("bazQux", true);
		gotest::assert::Equal(t, int(results.size()), 0);

		// Word prefix search should work
		results = cloned->SearchWordPrefix("foo");
		gotest::assert::Equal(t, int(results.size()), 2);
	});

	t->Run("handles nil index", [](T* t) {
		t->Parallel();

		Index* idx = nullptr;
		auto cloned = Clone(idx,
		    [](const std::shared_ptr<Export>&) { return true; });
		gotest::assert::Assert(t, cloned == nullptr);
	});

	t->Run("handles empty index", [](T* t) {
		t->Parallel();

		Index idx;
		auto cloned = Clone(&idx, 
		    [](const std::shared_ptr<Export>&) { return true; });
		gotest::assert::Equal(t, int(cloned->entries.size()), 0);
	});

	t->Run("filters all entries", [](T* t) {
		t->Parallel();

		Index idx;
		idx.insertAsWords(newTestEntry("fooBar", "pkg-a"));
		idx.insertAsWords(newTestEntry("bazQux", "pkg-b"));

		auto cloned = Clone(&idx, 
		    [](const std::shared_ptr<Export>&) { return false; });
		gotest::assert::Equal(t, int(cloned->entries.size()), 0);
		gotest::assert::Equal(t, int(cloned->index.size()), 0);
	});
}
REGISTER_UNIT_TEST("ls/autoimport.TestIndexClone", TestIndexClone);
