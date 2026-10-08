// tests_util.cpp — port of tsc/internal/stringutil/util_test.go.
#include <string>
#include <string_view>

#include "internal/gostd/testing.h"
#include "internal/stringutil/stringutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using tsc::encodeURI;

namespace {

void TestEncodeURI(T* t) {
	t->Parallel();

	struct {
		std::string_view name;
		std::string_view input;
		std::string_view expected;
	} tests[] = {
	    {.name = "encodes spaces as percent20",
	     .input = "a b",
	     .expected = "a%20b"},
	    {.name = "preserves reserved uri characters",
	     .input = ";/?:@&=+$,#",
	     .expected = ";/?:@&=+$,#"},
	    {.name = "encodes brackets and unicode using utf8 bytes",
	     .input = "①Ⅻㄨㄩ U1[abc]",
	     .expected = "%E2%91%A0%E2%85%AB%E3%84%A8%E3%84%A9%20U1%5Babc%5D"},
	};

	for (auto& tt : tests) {
		t->Run(std::string(tt.name), [tt](T* t) {
			t->Parallel();
			auto got = encodeURI(tt.input);
			if (got != tt.expected) {
				t->Fatalf("EncodeURI(%q) = %q, expected %q",
				          {std::string(tt.input), got,
				           std::string(tt.expected)});
			}
		});
	}
}

} // namespace

REGISTER_UNIT_TEST("stringutil.TestEncodeURI", TestEncodeURI);
