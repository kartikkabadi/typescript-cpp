// tests_js_case.cpp — port of tsc/internal/stringutil/js_case_test.go.
#include <string>

#include "internal/gostd/testing.h"
#include "internal/stringutil/stringutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using tsc::stringutil::ToLowerJS;
using tsc::stringutil::ToUpperJS;

namespace {

// EncodeJSStringRune — util.go:323: Go returns the encoded string.
std::string EncodeJSStringRune(char32_t ch) {
	char buf[8];
	int n = tsc::encodeJSStringRune(ch, buf);
	return {buf, static_cast<size_t>(n)};
}

void TestJSCasing(T* t) {
	t->Parallel();

	struct {
		std::string name;
		std::string got;
		std::string want;
	} tests[] = {
	    {"ascii lowercase", ToLowerJS("HELLO"), "hello"},
	    {"ascii uppercase", ToUpperJS("hello"), "HELLO"},
	    {"lowercase dotted i", ToLowerJS("İSPANYOL"), "i̇spanyol"},
	    {"lowercase lone sigma", ToLowerJS("Σ"), "σ"},
	    {"lowercase final sigma", ToLowerJS("ΟΣ"), "ος"},
	    {"lowercase non-sigma greek", ToLowerJS("Ω"), "ω"},
	    {"uppercase sharp s", ToUpperJS("ßfoo"), "SSFOO"},
	    {"uppercase non-ascii simple mapping", ToUpperJS("ω"), "Ω"},
	    {"uppercase ligature", ToUpperJS("ﬁoo"), "FIOO"},
	    {"capitalize-style uppercase", ToUpperJS("ß") + "foo", "SSfoo"},
	    {"uncapitalize-style lowercase", ToLowerJS("İ") + "foo",
	     "i̇foo"},
	    {"lowercase final sigma after lowercase letter without "
	     "uppercase mapping",
	     ToLowerJS("ʕΣ"), "ʕς"},
	    {"lowercase sigma after modifier letter", ToLowerJS("ʰΣ"),
	     "ʰσ"},
	    {"lowercase sigma after case ignorable ypogegrammeni",
	     ToLowerJS("ͅΣ"), "ͅσ"},
	    {"lowercase final sigma after feminine ordinal indicator",
	     ToLowerJS("ªΣ"), "ªς"},
	    {"lowercase final sigma after masculine ordinal indicator",
	     ToLowerJS("ºΣ"), "ºς"},
	    {"lowercase final sigma after roman numeral", ToLowerJS("ⅠΣ"),
	     "ⅰς"},
	    {"lowercase sigma after uppercase property added after "
	     "unicode 15",
	     ToLowerJS("\u1C89Σ"), "\u1C89σ"},
	    {"lowercase sigma after uppercase property skewed from local "
	     "v8 unicode data",
	     ToLowerJS("\uA7CBΣ"), "\uA7CBσ"},
	    {"lowercase sigma before immediate latin letter",
	     ToLowerJS("ΣA"), "σa"},
	    {"lowercase sigma before immediate roman numeral letter",
	     ToLowerJS("ΣⅠ"), "σⅰ"},
	    {"lowercase sigma before case ignorable then latin letter",
	     ToLowerJS("ΣͅA"), "σͅa"},
	    {"uppercase lone surrogate",
	     ToUpperJS(EncodeJSStringRune(0xD800)),
	     EncodeJSStringRune(0xD800)},
	    {"lowercase lone surrogate",
	     ToLowerJS("A" + EncodeJSStringRune(0xD800) + "B"),
	     "a" + EncodeJSStringRune(0xD800) + "b"},
	    {"uppercase lone low surrogate with text",
	     ToUpperJS(EncodeJSStringRune(0xDC00) + "x"),
	     EncodeJSStringRune(0xDC00) + "X"},
	    {"lowercase lone surrogate before sigma",
	     ToLowerJS(EncodeJSStringRune(0xD800) + "Σ"),
	     EncodeJSStringRune(0xD800) + "σ"},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [tt](T* t) {
			t->Parallel();
			if (tt.got != tt.want) {
				t->Fatalf("got %q, want %q", {tt.got, tt.want});
			}
		});
	}
}

} // namespace

REGISTER_UNIT_TEST("stringutil.TestJSCasing", TestJSCasing);
