// === slice: fourslash ===
// test_parser.go — fourslash test-data types and the marker/range parser.
#include "internal/fourslash/test_parser.h"
#include "internal/fourslash/fourslash.h"
#include "internal/fourslash/goutil.h"

#include "internal/json/json.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/stringutil/stringutil.h"
#include "internal/testrunner/testrunner.h"
#include "internal/tspath/tspath.h"

#include <algorithm>

namespace tsc::fourslash {

// Forward decls for the file-local helpers Go declares mid-file.
struct locationInformation;
static gostd::Error reportError(const std::string& fileName, int line,
                                int col, const std::string& message);
static std::string chompLeadingSpace(const std::string& content);
static bool isConfigFile(const std::string& fileName);
static bool hasUnsupportedGlobalOptionsWithConfig(
    const std::unordered_map<std::string, std::string>& globalOptions);
static std::pair<std::shared_ptr<testFileWithMarkers>, gostd::Error>
parseFileContent(
    const std::string& fileName, const std::string& content,
    const std::unordered_map<std::string, std::string>& fileOptions);
static std::pair<std::shared_ptr<Marker>, gostd::Error> getObjectMarker(
    const std::string& fileName, locationInformation* location,
    const std::string& text);

// --- RangeMarker/Marker methods — test_parser.go:31-82 ---

std::shared_ptr<std::string> RangeMarker::GetName() const {
	if (Marker == nullptr) {
		return nullptr;
	}
	return Marker->Name;
}

// test_parser.go:47.
lsproto::Location RangeMarker::LSLocation() const {
	return lsproto::Location{
	    .Uri = lsconv::FileNameToDocumentURI(fileName),
	    .Range = LSRange,
	};
}

// MakerWithSymlink — test_parser.go:75.
std::shared_ptr<Marker> Marker::MakerWithSymlink(
    const std::string& fileName) const {
	auto m = std::make_shared<Marker>();
	m->fileName = fileName;
	m->Position = Position;
	m->LSPosition = LSPosition;
	m->Name = Name;
	m->Data = Data;
	return m;
}

// --- TestData::isStateBaseliningEnabled — test_parser.go:100 ---

bool TestData::isStateBaseliningEnabled() const {
	return fourslash::isStateBaseliningEnabled(GlobalOptions);
}

// --- fourslashError — test_parser.go:517 ---

struct fourslashErrorImpl final : gostd::ErrObj {
	std::string err;
	explicit fourslashErrorImpl(std::string e) : err(std::move(e)) {}
	std::string Error() const override { return err; }
};

// --- ParseTestData — test_parser.go:114 ---

TestData ParseTestData(gostd::testing::T* t, const std::string& contents,
                       const std::string& fileName) {
	// List of all the subfiles we've parsed out
	std::vector<std::shared_ptr<TestFileInfo>> files;

	std::unordered_map<std::string, std::shared_ptr<Marker>> markerPositions;
	std::vector<std::shared_ptr<Marker>> markers;
	std::vector<std::shared_ptr<RangeMarker>> ranges;

	auto parseResult =
	    testrunner::ParseTestFilesAndSymlinksWithOptions<
	        std::shared_ptr<testFileWithMarkers>>(
	        contents, fileName,
	        [](const std::string& fileName, const std::string& content,
	           const std::unordered_map<std::string, std::string>&
	               fileOptions)
	            -> std::pair<std::shared_ptr<testFileWithMarkers>,
	                         gostd::Error> {
		        return parseFileContent(fileName, content, fileOptions);
	        },
	        testrunner::ParseTestFilesOptions{
	            .AllowImplicitFirstFile = true,
	        });
	if (parseResult.err != nullptr) {
		t->Fatalf("Error parsing fourslash data: %s",
		          {parseResult.err->Error()});
	}

	bool hasTSConfig = false;
	for (const auto& file : parseResult.units) {
		files.push_back(file->file);
		hasTSConfig = hasTSConfig || isConfigFile(file->file->fileName);

		markers.insert(markers.end(), file->markers.begin(),
		               file->markers.end());
		ranges.insert(ranges.end(), file->ranges.begin(), file->ranges.end());
		for (const auto& marker : file->markers) {
			if (marker->Name == nullptr) {
				if (marker->Data != nullptr) {
					// The marker is an anonymous object marker, which does not need a name. Markers are only set into markerPositions if they have a name
					continue;
				}
				t->Fatalf("Marker at position %v is unnamed",
				          {marker->Position});
			}
			auto existing = markerPositions.find(*marker->Name);
			if (existing != markerPositions.end()) {
				t->Fatalf(
				    "Duplicate marker name: \"%s\" at %v and %v",
				    {*marker->Name, marker->Position,
				     existing->second->Position});
			}
			markerPositions[*marker->Name] = marker;
		}
	}

	if (hasTSConfig &&
	    hasUnsupportedGlobalOptionsWithConfig(parseResult.globalOptions) &&
	    !isStateBaseliningEnabled(parseResult.globalOptions)) {
		t->Fatalf(
		    "It is not allowed to use global options along with config "
		    "files.", {});
	}

	return TestData{
	    .Files = std::move(files),
	    .MarkerPositions = std::move(markerPositions),
	    .Markers = std::move(markers),
	    .Symlinks = std::move(parseResult.symlinks),
	    .GlobalOptions = std::move(parseResult.globalOptions),
	    .Ranges = std::move(ranges),
	};
}

// hasUnsupportedGlobalOptionsWithConfig — test_parser.go:167.
static bool hasUnsupportedGlobalOptionsWithConfig(
    const std::unordered_map<std::string, std::string>& globalOptions) {
	for (const auto& option : globalOptions) {
		auto lower = gostr::toLower(option.first);
		if (lower == "symlink" || lower == "link" ||
		    lower == "usecasesensitivefilenames") {
			continue;
		}
		return true;
	}
	return false;
}

// isConfigFile — test_parser.go:179.
static bool isConfigFile(const std::string& fileName) {
	auto lower = gostr::toLower(fileName);
	return gostr::hasSuffix(lower, "tsconfig.json") ||
	       gostr::hasSuffix(lower, "jsconfig.json");
}

// locationInformation — test_parser.go:185.
struct locationInformation {
	int position = 0;
	int sourcePosition = 0;
	int sourceLine = 0;
	int sourceColumn = 0;
};

// rangeLocationInformation — test_parser.go:192.
struct rangeLocationInformation : locationInformation {
	std::shared_ptr<Marker> marker;
};

// parserState — test_parser.go:231.
enum class parserState {
	stateNone,
	stateInSlashStarMarker,
	stateInObjectMarker,
};

// parseFileContent — test_parser.go:241.
std::pair<std::shared_ptr<testFileWithMarkers>, gostd::Error>
parseFileContent(
    const std::string& fileName, const std::string& contentIn,
    const std::unordered_map<std::string, std::string>& fileOptions) {
	auto fileName_ = tspath::getNormalizedAbsolutePath(fileName, "/");
	auto content = chompLeadingSpace(contentIn);

	// The file content (minus metacharacters) so far
	gostr::Builder output;

	std::vector<std::shared_ptr<Marker>> markers;

	/// A stack of the open range markers that are still unclosed
	std::vector<rangeLocationInformation> openRanges;
	/// A list of closed ranges we've collected so far
	std::vector<std::shared_ptr<RangeMarker>> rangeMarkers;

	// The total number of metacharacters removed from the file (so far)
	int difference = 0;

	// One-based current position data
	int line = 1;
	int column = 1;

	// The current marker (or maybe multi-line comment?) we're parsing,
	// possibly
	locationInformation* openMarker = nullptr;
	locationInformation openMarkerStorage;

	// The latest position of the start of an unflushed plain text area
	size_t lastNormalCharPosition = 0;

	auto flush = [&](int64_t lastSafeCharIndex) {
		if (lastSafeCharIndex != -1) {
			output.WriteString(content.substr(
			    lastNormalCharPosition,
			    static_cast<size_t>(lastSafeCharIndex) -
			        lastNormalCharPosition));
		} else {
			output.WriteString(content.substr(lastNormalCharPosition));
		}
	};

	auto state = parserState::stateNone;
	auto decoded0 = gostr::decodeRuneInString(content);
	char32_t previousCharacter = decoded0.first;
	int i = decoded0.second;
	int size = 0;
	char32_t currentCharacter = 0;
	for (; i < static_cast<int>(content.size()); i = i + size) {
		auto decoded = gostr::decodeRuneInString(content.substr(i));
		currentCharacter = decoded.first;
		size = decoded.second;
		switch (state) {
		case parserState::stateNone:
			if (previousCharacter == U'[' && currentCharacter == U'|') {
				// found a range start
				openRanges.push_back(rangeLocationInformation{
				    {.position = static_cast<int>(i - 1) - difference,
				     .sourcePosition = static_cast<int>(i - 1),
				     .sourceLine = line,
				     .sourceColumn = column},
				    nullptr,
				});
				// copy all text up to marker position
				flush(i - 1);
				lastNormalCharPosition = i + 1;
				difference += 2;
			} else if (previousCharacter == U'|' &&
			           currentCharacter == U']') {
				// found a range end
				if (openRanges.empty()) {
					return {nullptr,
					        reportError(fileName_, line, column,
					                    "Found range end with no "
					                    "matching start.")};
				}
				auto rangeStart = openRanges.back();
				openRanges.pop_back();

				auto closedRange = std::make_shared<RangeMarker>();
				closedRange->fileName = fileName_;
				closedRange->Range = TextRange{
				    static_cast<TextPos>(rangeStart.position),
				    static_cast<TextPos>((i - 1) - difference)};
				closedRange->Marker = rangeStart.marker;

				rangeMarkers.push_back(closedRange);

				// copy all text up to range marker position
				flush(i - 1);
				lastNormalCharPosition = i + 1;
				difference += 2;
			} else if (previousCharacter == U'/' &&
			           currentCharacter == U'*' &&
			           (i + 1 >= (int)content.size() || content[i + 1] != '/')) {
				// found a possible marker start
				state = parserState::stateInSlashStarMarker;
				openMarkerStorage = locationInformation{
				    .position = static_cast<int>(i - 1) - difference,
				    .sourcePosition = static_cast<int>(i - 1),
				    .sourceLine = line,
				    .sourceColumn = column - 1,
				};
				openMarker = &openMarkerStorage;
			} else if (previousCharacter == U'{' &&
			           currentCharacter == U'|') {
				// found an object marker start
				state = parserState::stateInObjectMarker;
				openMarkerStorage = locationInformation{
				    .position = static_cast<int>(i - 1) - difference,
				    .sourcePosition = static_cast<int>(i - 1),
				    .sourceLine = line,
				    .sourceColumn = column,
				};
				openMarker = &openMarkerStorage;
				flush(i - 1);
			}
			break;
		case parserState::stateInObjectMarker:
			// Object markers are only ever terminated by |} and have no
			// content restrictions
			if (previousCharacter == U'|' && currentCharacter == U'}') {
				auto objectMarkerData = gostr::trimSpace(content.substr(
				    openMarker->sourcePosition + 2, i - 1 -
				                                    (openMarker->sourcePosition + 2)));
				auto [marker, e] = getObjectMarker(
				    fileName_, openMarker, objectMarkerData);
				if (e != nullptr) {
					return {nullptr, e};
				}

				if (!openRanges.empty()) {
					openRanges.back().marker = marker;
				}
				markers.push_back(marker);

				// Set the current start to point to the end of the
				// current marker to ignore its text
				lastNormalCharPosition = i + 1;
				difference += i + 1 - openMarker->sourcePosition;

				// Reset the state
				openMarker = nullptr;
				state = parserState::stateNone;
			}
			break;
		case parserState::stateInSlashStarMarker:
			if (previousCharacter == U'*' && currentCharacter == U'/') {
				// Record the marker
				// start + 2 to ignore the */, -1 on the end to ignore the
				// * (/ is next)
				auto markerNameText = gostr::trimSpace(content.substr(
				    openMarker->sourcePosition + 2, i - 1 -
				                                    (openMarker->sourcePosition + 2)));
				auto marker = std::make_shared<Marker>();
				marker->fileName = fileName_;
				marker->Position = openMarker->position;
				marker->Name =
				    std::make_shared<std::string>(markerNameText);
				if (!openRanges.empty()) {
					openRanges.back().marker = marker;
				}
				markers.push_back(marker);

				// Set the current start to point to the end of the
				// current marker to ignore its text
				flush(openMarker->sourcePosition);
				lastNormalCharPosition = i + 1;
				difference += i + 1 - openMarker->sourcePosition;

				// Reset the state
				openMarker = nullptr;
				state = parserState::stateNone;
			} else if (!(isDigit(currentCharacter) ||
			             isASCIILetter(currentCharacter) ||
			             currentCharacter == U'$' ||
			             currentCharacter ==
			                 U'_')) { // Invalid marker character
				if (currentCharacter == U'*' &&
				    i < (int)content.size() - 1 && content[i + 1] == '/') {
					// The marker is about to be closed, ignore the
					// 'invalid' char
				} else {
					// We've hit a non-valid marker character, so we
					// were actually in a block comment
					// Bail out the text we've gathered so far back into
					// the output
					flush(i);
					lastNormalCharPosition = i;
					openMarker = nullptr;
					state = parserState::stateNone;
				}
			}
			break;
		}
		if (currentCharacter == U'\n' && previousCharacter == U'\r') {
			// Ignore trailing \n after \r
			continue;
		} else if (currentCharacter == U'\n' ||
		           currentCharacter == U'\r') {
			line++;
			column = 1;
			continue;
		}
		column++;
		if (static_cast<size_t>(i) >= lastNormalCharPosition) {
			previousCharacter = currentCharacter;
		} else {
			previousCharacter =
			    gostr::runeError; // reset to avoid accidentally reusing
			                      // marker delimiters as part of other
			                      // markers
		}
	}

	// Add the remaining text
	flush(-1);

	if (!openRanges.empty()) {
		auto openRange = openRanges[0];
		return {nullptr,
		        reportError(fileName_, openRange.sourceLine,
		                    openRange.sourceColumn, "Unterminated range.")};
	}

	if (openMarker != nullptr) {
		return {nullptr,
		        reportError(fileName_, openMarker->sourceLine,
		                    openMarker->sourceColumn,
		                    "Unterminated marker.")};
	}

	auto outputString = output.String();
	// Set LS positions for markers
	auto lineMap = lsconv::ComputeLSPLineStarts(outputString);
	auto converters = newTestConverters(
	    lsproto::PositionEncodingKindUTF8,
	    [lineMap](const std::string&) -> lsconv::LSPLineMap* {
		    return lineMap;
	    });

	bool emit = [&] {
		auto it = fileOptions.find(std::string(emitThisFileOption));
		return it != fileOptions.end() && it->second == "true";
	}();

	auto testFileInfo = std::make_shared<TestFileInfo>();
	testFileInfo->fileName = fileName_;
	testFileInfo->Content = outputString;
	testFileInfo->emit = emit;
	testFileInfo->open = [&] {
		auto it = fileOptions.find(std::string(noOpenFileOption));
		return !(it != fileOptions.end() && it->second == "true");
	}();

	gostr::sortStableFunc(
	    rangeMarkers, [](const std::shared_ptr<RangeMarker>& a,
	                     const std::shared_ptr<RangeMarker>& b) {
		    if (a->Range.pos() != b->Range.pos()) {
			    return a->Range.pos() - b->Range.pos();
		    }
		    return b->Range.end() - a->Range.end();
	    });

	for (const auto& marker : markers) {
		marker->LSPosition = converters->PositionToLineAndCharacter(
		    testFileInfo.get(), TextPos(marker->Position));
	}
	for (const auto& rangeMarker : rangeMarkers) {
		rangeMarker->LSRange = lsproto::Range{
		    .Start = converters->PositionToLineAndCharacter(
		        testFileInfo.get(), TextPos(rangeMarker->Range.pos())),
		    .End = converters->PositionToLineAndCharacter(
		        testFileInfo.get(), TextPos(rangeMarker->Range.end())),
		};
	}

	auto result = std::make_shared<testFileWithMarkers>();
	result->file = testFileInfo;
	result->markers = std::move(markers);
	result->ranges = std::move(rangeMarkers);
	return {result, nullptr};
}

// getObjectMarker — test_parser.go:480.
std::pair<std::shared_ptr<Marker>, gostd::Error>
getObjectMarker(const std::string& fileName, locationInformation* location,
                const std::string& text) {
	// Attempt to parse the marker value as JSON
	auto [v, e] = json::parse("{ " + text + " }");

	if (e != nullptr) {
		return {nullptr,
		        reportError(fileName, location->sourceLine,
		                    location->sourceColumn,
		                    "Unable to parse marker text " + text)};
	}
	if (v.kind != json::Dom::K::Object || v.obj.empty()) {
		return {nullptr,
		        reportError(fileName, location->sourceLine,
		                    location->sourceColumn,
		                    "Object markers can not be empty")};
	}
	auto markerValue = std::make_shared<json::Dom>(v);

	auto marker = std::make_shared<Marker>();
	marker->fileName = fileName;
	marker->Position = location->position;
	marker->Data = markerValue;

	// Object markers can be anonymous
	if (auto nameDom = json::objGet(*markerValue, "name");
	    nameDom != nullptr && nameDom->kind == json::Dom::K::String &&
	    !nameDom->strVal.empty()) {
		marker->Name =
		    std::make_shared<std::string>(nameDom->strVal);
	}

	return {marker, nullptr};
}

// reportError — test_parser.go:505.
gostd::Error reportError(const std::string& fileName, int line, int col,
                         const std::string& message) {
	return std::make_shared<fourslashErrorImpl>(
	    gostd::sprintf("%v (%v,%v): %v", {fileName, line, col, message}));
}

// chompLeadingSpace — test_parser.go:509.
std::string chompLeadingSpace(const std::string& content) {
	auto lines = gostr::split(content, "\n");
	for (const auto& line : lines) {
		if (!line.empty() && line[0] != ' ') {
			return content;
		}
	}

	std::vector<std::string> result(lines.size());
	for (size_t i = 0; i < lines.size(); i++) {
		if (!lines[i].empty()) {
			result[i] = lines[i].substr(1);
		}
	}
	return gostr::join(result, "\n");
}

} // namespace tsc::fourslash
