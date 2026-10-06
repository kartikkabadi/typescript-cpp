// Internal declarations shared between harnessutil.cpp (GetSourceMapRecord)
// and sourcemap_recorder.cpp — the Go file boundary is the same (the types in
// sourcemap_recorder.go are package-private and used by harnessutil.go).
#pragma once

#include <string>
#include <vector>

#include "internal/core/text.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/testutil/harnessutil/harnessutil.h"

namespace tsc::testutil::harnessutil {

// writerAggregator — sourcemap_recorder.go:18 (strings.Builder + helpers).
struct writerAggregator {
	std::string b;

	void WriteString(std::string_view s) { b += s; }
	const std::string& String() const { return b; }
	// WriteStringf — sourcemap_recorder.go:22.
	void WriteStringf(std::string_view format,
	                  std::initializer_list<gostd::fmtArg> args);
	// WriteLine — sourcemap_recorder.go:26 (appends \r\n).
	void WriteLine(std::string_view s);
	// WriteLinef — sourcemap_recorder.go:31.
	void WriteLinef(std::string_view format,
	                std::initializer_list<gostd::fmtArg> args);
};

// sourceMapSpanWithDecodeErrors — sourcemap_recorder.go:36.
struct sourceMapSpanWithDecodeErrors {
	sourcemap::Mapping* sourceMapSpan = nullptr;
	std::vector<std::string> decodeErrors;
};

// decodedMapping — sourcemap_recorder.go:41.
struct decodedMapping {
	sourcemap::Mapping* sourceMapSpan = nullptr;
	std::string error;
};

// sourceMapDecoder — sourcemap_recorder.go:46.
struct sourceMapDecoder {
	std::string_view sourceMapMappings;
	sourcemap::MappingsDecoder* mappings = nullptr;

	// decodeNextEncodedSourceMapSpan — sourcemap_recorder.go:59.
	decodedMapping* decodeNextEncodedSourceMapSpan();
	// hasCompletedDecoding — sourcemap_recorder.go:73.
	bool hasCompletedDecoding();
	// getRemainingDecodeString — sourcemap_recorder.go:77.
	std::string_view getRemainingDecodeString();
};

// newSourceMapDecoder — sourcemap_recorder.go:51.
sourceMapDecoder* newSourceMapDecoder(sourcemap::RawSourceMap* sourceMap);

// sourceMapSpanWriter — sourcemap_recorder.go:87.
struct sourceMapSpanWriter {
	writerAggregator* sourceMapRecorder = nullptr;
	std::vector<std::string> sourceMapSources;
	std::vector<std::string> sourceMapNames;
	TestFile* jsFile = nullptr;
	std::vector<TextPos> jsLineMap;
	std::string tsCode;
	std::vector<TextPos> tsLineMap;
	std::vector<sourceMapSpanWithDecodeErrors> spansOnSingleLine;
	int prevWrittenSourcePos = 0;
	int nextJsLineToWrite = 0;
	bool spanMarkerContinues = false;
	sourceMapDecoder* sourceMapDecoder = nullptr;

	// getSourceMapSpanString — sourcemap_recorder.go:131.
	std::string getSourceMapSpanString(sourcemap::Mapping* mapEntry,
	                                   bool getAbsentNameIndex);
	// recordSourceMapSpan — sourcemap_recorder.go:177.
	void recordSourceMapSpan(sourcemap::Mapping* sourceMapSpan);
	// recordNewSourceFileSpan — sourcemap_recorder.go:210.
	void recordNewSourceFileSpan(sourcemap::Mapping* sourceMapSpan,
	                             const std::string& newSourceFileCode);
	// close — sourcemap_recorder.go:237.
	void close();
	// getTextOfLine — sourcemap_recorder.go:251.
	std::string_view getTextOfLine(int line,
	                               const std::vector<TextPos>& lineMap,
	                               const std::string& code);
	// writeJsFileLines — sourcemap_recorder.go:264.
	void writeJsFileLines(int endJsLine);
	// writeRecordedSpans — sourcemap_recorder.go:270.
	void writeRecordedSpans();
};

// newSourceMapSpanWriter — sourcemap_recorder.go:104.
sourceMapSpanWriter* newSourceMapSpanWriter(
    writerAggregator* sourceMapRecorder, sourcemap::RawSourceMap* sourceMap,
    TestFile* jsFile);

}  // namespace tsc::testutil::harnessutil
