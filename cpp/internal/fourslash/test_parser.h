// === slice: fourslash ===
// test_parser.go — fourslash test-data types and the marker/range parser.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/core/text.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/spanmap/spanmap.h"

namespace tsc::fourslash {

// Inserted in source files by surrounding desired text
// in a range with `[|` and `|]`. For example,
//
// [|text in range|]
//
// is a range with `text in range` "selected".
struct Marker;

// MarkerOrRange — test_parser.go:85.
struct MarkerOrRange {
	virtual ~MarkerOrRange() = default;
	virtual std::string FileName() const = 0;
	virtual lsproto::Position LSPos() const = 0;
	virtual std::shared_ptr<std::string> GetName() const = 0;
};

// RangeMarker — test_parser.go:26.
struct RangeMarker final : MarkerOrRange {
	std::string fileName;
	TextRange Range;
	lsproto::Range LSRange;
	std::shared_ptr<Marker> Marker;

	lsproto::Position LSPos() const override { return LSRange.Start; }
	std::string FileName() const override { return fileName; }
	std::shared_ptr<std::string> GetName() const override;
	// LSLocation — test_parser.go:48.
	lsproto::Location LSLocation() const;
};

// Marker — test_parser.go:55.
struct Marker final : MarkerOrRange {
	std::string fileName;
	int Position{};
	lsproto::Position LSPosition;
	std::shared_ptr<std::string> Name; // null for anonymous markers such as `{| "foo": "bar" |}`
	// Data — Go `map[string]any`; a parsed JSON object (never set to null by
	// the parser; null Data means "no object marker data").
	std::shared_ptr<json::Dom> Data;

	lsproto::Position LSPos() const override { return LSPosition; }
	std::string FileName() const override { return fileName; }
	std::shared_ptr<std::string> GetName() const override { return Name; }
	// MakerWithSymlink — test_parser.go:75.
	std::shared_ptr<class Marker> MakerWithSymlink(const std::string& fileName) const;
};

// TestData — test_parser.go:91.
struct TestData {
	std::vector<std::shared_ptr<struct TestFileInfo>> Files;
	std::unordered_map<std::string, std::shared_ptr<Marker>> MarkerPositions;
	std::vector<std::shared_ptr<Marker>> Markers;
	std::unordered_map<std::string, std::string> Symlinks;
	std::unordered_map<std::string, std::string> GlobalOptions;
	std::vector<std::shared_ptr<RangeMarker>> Ranges;

	// isStateBaseliningEnabled — test_parser.go:100.
	bool isStateBaseliningEnabled() const;
};

// testFileWithMarkers — test_parser.go:104.
struct testFileWithMarkers {
	std::shared_ptr<struct TestFileInfo> file;
	std::vector<std::shared_ptr<Marker>> markers;
	std::vector<std::shared_ptr<RangeMarker>> ranges;
};

// isStateBaseliningEnabled — test_parser.go:110.
inline bool isStateBaseliningEnabled(
	const std::unordered_map<std::string, std::string>& globalOptions) {
	auto it = globalOptions.find("statebaseline");
	return it != globalOptions.end() && it->second == "true";
}

// TestFileInfo — test_parser.go:200. Implements lsconv::Script (concept).
struct TestFileInfo {
	std::string fileName;
	// The contents of the file (with markers, etc stripped out)
	std::string Content;
	bool emit = false;
	bool open = false;

	std::string FileName() const { return fileName; }
	std::string OriginalFileName() const { return fileName; }
	const std::string& Text() const { return Content; }
	const std::string& OriginalText() const { return Content; }
	spanmap::SpanMap* SpanMap() const { return nullptr; }
};

inline constexpr std::string_view emitThisFileOption = "emitthisfile";
inline constexpr std::string_view noOpenFileOption = "noopen";

// ParseTestData — test_parser.go:114.
TestData ParseTestData(gostd::testing::T* t, const std::string& contents,
                       const std::string& fileName);

// Internal parser surface (kept for parity with the Go file):
// locationInformation/rangeLocationInformation, parserState
// (stateNone/stateInSlashStarMarker/stateInObjectMarker), parseFileContent,
// getObjectMarker, reportError/fourslashError, chompLeadingSpace,
// isConfigFile, hasUnsupportedGlobalOptionsWithConfig — all in
// test_parser.cpp.
} // namespace tsc::fourslash
