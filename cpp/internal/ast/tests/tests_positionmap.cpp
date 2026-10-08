// Port of tsc/internal/ast/positionmap_test.go.
// BenchmarkComputePositionMap_* / BenchmarkUTF8* benchmarks are not ported —
// they do not run under `go test`.
#include <string>

#include "internal/ast/ast.h"
#include "internal/gostd/testing.h"
#include "internal/stringutil/stringutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;

static void TestPositionMapASCII(T* t) {
	t->Parallel();
	std::string text = "const x = 1;";
	PositionMap* pm = computePositionMap(text);
	if (!pm->IsAsciiOnly()) {
		t->Fatal({"expected ASCII-only"});
	}
	for (int i = 0; i <= (int)text.size(); i++) {
		if (int got = pm->UTF8ToUTF16(i); got != i) {
			t->Errorf("UTF8ToUTF16(%d) = %d, want %d", {i, got, i});
		}
		if (int got = pm->UTF16ToUTF8(i); got != i) {
			t->Errorf("UTF16ToUTF8(%d) = %d, want %d", {i, got, i});
		}
	}
}

static void TestPositionMapTwoByte(T* t) {
	t->Parallel();
	// "café" — é (U+00E9) is 2 bytes UTF-8, 1 code unit UTF-16
	std::string text = "const café = 1;\nconst x = 2;";
	PositionMap* pm = computePositionMap(text);
	if (pm->IsAsciiOnly()) {
		t->Fatal({"expected non-ASCII"});
	}

	// Everything before é (byte offset 9) should be identity
	for (int i = 0; i < 10; i++) {
		if (int got = pm->UTF8ToUTF16(i); got != i) {
			t->Errorf("before é: UTF8ToUTF16(%d) = %d, want %d", {i, got, i});
		}
	}

	// é starts at UTF-8 byte 9, UTF-16 offset 9: same
	if (int got = pm->UTF8ToUTF16(9); got != 9) {
		t->Errorf("at é: UTF8ToUTF16(9) = %d, want 9", {got});
	}

	// After é (byte 11 in UTF-8 = code unit 10 in UTF-16), delta is 1
	// ' ' after café: UTF-8 byte 11, UTF-16 offset 10
	if (int got = pm->UTF8ToUTF16(11); got != 10) {
		t->Errorf("after é: UTF8ToUTF16(11) = %d, want 10", {got});
	}

	// 'x' on second line: UTF-8 byte 23, UTF-16 offset 22
	int xUTF8 = (int)text.rfind("x");
	if (int got = pm->UTF8ToUTF16(xUTF8); got != xUTF8 - 1) {
		t->Errorf("at x: UTF8ToUTF16(%d) = %d, want %d", {xUTF8, got, xUTF8 - 1});
	}

	// Reverse: UTF-16 offset 22 should map to UTF-8 byte 23
	int xUTF16 = xUTF8 - 1;
	if (int got = pm->UTF16ToUTF8(xUTF16); got != xUTF8) {
		t->Errorf("reverse at x: UTF16ToUTF8(%d) = %d, want %d", {xUTF16, got,
				  xUTF8});
	}
}

static void TestPositionMapFourByte(T* t) {
	t->Parallel();
	// 🎉 (U+1F389) is 4 bytes UTF-8, 2 code units UTF-16
	std::string text = std::string("const a = \"🎉\";") + "\nconst b = 2;";
	PositionMap* pm = computePositionMap(text);
	if (pm->IsAsciiOnly()) {
		t->Fatal({"expected non-ASCII"});
	}

	// 🎉 starts at byte 11 (after `const a = "`)
	// UTF-8: bytes 11-14 (4 bytes), UTF-16: units 11-12 (2 code units)
	// After 🎉: UTF-8 byte 15, UTF-16 offset 13. Delta = 2.

	// 'b' on second line
	int bUTF8 = (int)text.rfind("b");
	int bUTF16 = bUTF8 - 2; // delta of 2 from emoji
	if (int got = pm->UTF8ToUTF16(bUTF8); got != bUTF16) {
		t->Errorf("at b: UTF8ToUTF16(%d) = %d, want %d", {bUTF8, got, bUTF16});
	}
	if (int got = pm->UTF16ToUTF8(bUTF16); got != bUTF8) {
		t->Errorf("reverse at b: UTF16ToUTF8(%d) = %d, want %d", {bUTF16, got,
				  bUTF8});
	}
}

static void TestPositionMapMultipleNonASCII(T* t) {
	t->Parallel();
	// Mix of 2-byte and 4-byte characters
	// "à" (U+00E0) = 2 bytes UTF-8, 1 code unit UTF-16 (delta +1)
	// "🎉" (U+1F389) = 4 bytes UTF-8, 2 code units UTF-16 (delta +2)
	std::string text = "à🎉x";
	PositionMap* pm = computePositionMap(text);

	// à: UTF-8 [0,2), UTF-16 [0,1)
	// 🎉: UTF-8 [2,6), UTF-16 [1,3)
	// x: UTF-8 [6,7), UTF-16 [3,4)
	struct {
		int utf8;
		int utf16;
	} tests[] = {
		{0, 0},
		{2, 1}, // start of 🎉
		{6, 3}, // x
		{7, 4}, // end
	};
	for (auto& tt : tests) {
		if (int got = pm->UTF8ToUTF16(tt.utf8); got != tt.utf16) {
			t->Errorf("UTF8ToUTF16(%d) = %d, want %d", {tt.utf8, got, tt.utf16});
		}
		if (int got = pm->UTF16ToUTF8(tt.utf16); got != tt.utf8) {
			t->Errorf("UTF16ToUTF8(%d) = %d, want %d", {tt.utf16, got, tt.utf8});
		}
	}
}

static void TestPositionMapLoneSurrogateSentinel(T* t) {
	t->Parallel();
	char buf[4];
	int n = encodeJSStringRune(0xD800, buf);
	std::string text = std::string("a") + std::string(buf, n) + "b";
	PositionMap* pm = computePositionMap(text);
	if (pm->IsAsciiOnly()) {
		t->Fatal({"expected non-ASCII"});
	}

	if (int got = pm->UTF8ToUTF16((int)text.size()); got != 3) {
		t->Errorf("UTF8ToUTF16(%d) = %d, want 3", {(int)text.size(), got});
	}
	if (int got = pm->UTF16ToUTF8(2); got != (int)text.size() - 1) {
		t->Errorf("UTF16ToUTF8(2) = %d, want %d", {got, (int)text.size() - 1});
	}
}

static void TestPositionMapRoundtrip(T* t) {
	t->Parallel();
	std::string text = "let café = \"🎉\"; // naïve";
	PositionMap* pm = computePositionMap(text);

	// Convert every valid UTF-16 position to UTF-8 and back
	int utf16Len = pm->UTF8ToUTF16((int)text.size());
	for (int i = 0; i <= utf16Len; i++) {
		int utf8Pos = pm->UTF16ToUTF8(i);
		int back = pm->UTF8ToUTF16(utf8Pos);
		if (back != i) {
			t->Errorf("roundtrip UTF16->UTF8->UTF16: %d -> %d -> %d", {i, utf8Pos,
					  back});
		}
	}
}

REGISTER_UNIT_TEST("ast.TestPositionMapASCII", TestPositionMapASCII);
REGISTER_UNIT_TEST("ast.TestPositionMapTwoByte", TestPositionMapTwoByte);
REGISTER_UNIT_TEST("ast.TestPositionMapFourByte", TestPositionMapFourByte);
REGISTER_UNIT_TEST("ast.TestPositionMapMultipleNonASCII",
				   TestPositionMapMultipleNonASCII);
REGISTER_UNIT_TEST("ast.TestPositionMapLoneSurrogateSentinel",
				   TestPositionMapLoneSurrogateSentinel);
REGISTER_UNIT_TEST("ast.TestPositionMapRoundtrip", TestPositionMapRoundtrip);
