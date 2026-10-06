// sourcemap_recorder.go — the baseline source-map record writer stack:
// writerAggregator, sourceMapDecoder, sourceMapSpanWriter, recordedSpanWriter.
#include "internal/testutil/harnessutil/sourcemap_recorder.h"

#include "internal/core/text.h"
#include "internal/json/json.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::testutil::harnessutil {

// === writerAggregator methods — sourcemap_recorder.go:22-33 ===

void writerAggregator::WriteStringf(std::string_view format,
                                    std::initializer_list<gostd::fmtArg> args) {
	b += gostd::sprintf(format, args);
}

void writerAggregator::WriteLine(std::string_view s) {
	b += s;
	b += "\r\n";
}

void writerAggregator::WriteLinef(std::string_view format,
                                  std::initializer_list<gostd::fmtArg> args) {
	std::string f(format);
	f += "\r\n";
	b += gostd::sprintf(f, args);
}

// === sourceMapDecoder — sourcemap_recorder.go:51-79 ===

sourceMapDecoder* newSourceMapDecoder(sourcemap::RawSourceMap* sourceMap) {
	auto* d = new sourceMapDecoder();
	d->sourceMapMappings = sourceMap->Mappings;
	d->mappings = sourcemap::DecodeMappings(sourceMap->Mappings);
	return d;
}

decodedMapping* sourceMapDecoder::decodeNextEncodedSourceMapSpan() {
	auto [value, done] = mappings->Next();
	if (done) {
		auto* mapping = new decodedMapping();
		mapping->error = mappings->Error();
		mapping->sourceMapSpan = mappings->State();
		if (mapping->error.empty()) {
			mapping->error = "No encoded entry found";
		}
		return mapping;
	}
	auto* m = new decodedMapping();
	m->sourceMapSpan = value;
	return m;
}

bool sourceMapDecoder::hasCompletedDecoding() {
	return mappings->Pos() == (int)sourceMapMappings.size();
}

std::string_view sourceMapDecoder::getRemainingDecodeString() {
	return sourceMapMappings.substr(mappings->Pos());
}

// === newSourceMapSpanWriter — sourcemap_recorder.go:104 ===

sourceMapSpanWriter* newSourceMapSpanWriter(
    writerAggregator* sourceMapRecorder, sourcemap::RawSourceMap* sourceMap,
    TestFile* jsFile) {
	auto* writer = new sourceMapSpanWriter();
	writer->sourceMapRecorder = sourceMapRecorder;
	writer->sourceMapSources = sourceMap->Sources;
	writer->sourceMapNames = sourceMap->Names;
	writer->jsFile = jsFile;
	writer->jsLineMap = computeECMALineStarts(jsFile->Content);
	writer->prevWrittenSourcePos = 0;
	writer->nextJsLineToWrite = 0;
	writer->spanMarkerContinues = false;
	writer->sourceMapDecoder = newSourceMapDecoder(sourceMap);

	sourceMapRecorder->WriteLine(
	    "===================================================================");
	sourceMapRecorder->WriteLinef("JsFile: %s", {sourceMap->File});
	auto lineInfo = std::unique_ptr<sourcemap::ECMALineInfo>(
	    sourcemap::CreateECMALineInfo(jsFile->Content, writer->jsLineMap));
	sourceMapRecorder->WriteLinef(
	    "mapUrl: %s", {sourcemap::TryGetSourceMappingURL(lineInfo.get())});
	sourceMapRecorder->WriteLinef("sourceRoot: %s", {sourceMap->SourceRoot});
	std::string joined;
	for (size_t i = 0; i < sourceMap->Sources.size(); i++) {
		if (i) joined += ",";
		joined += sourceMap->Sources[i];
	}
	sourceMapRecorder->WriteLinef("sources: %s", {joined});
	if (sourceMap->SourcesContent.has_value() &&
	    !sourceMap->SourcesContent->empty()) {
		// json.Marshal(sourceMap.SourcesContent) — []json.RawMessage-ish:
		// each element is either a JSON string or null.
		std::vector<std::string> els;
		for (const auto& c : *sourceMap->SourcesContent) {
			els.push_back(c.has_value() ? json::marshalString(*c) : "null");
		}
		sourceMapRecorder->WriteLinef("sourcesContent: %s",
		                            {json::marshalArray(els)});
	}
	sourceMapRecorder->WriteLine(
	    "===================================================================");
	return writer;
}

// === getSourceMapSpanString — sourcemap_recorder.go:131 ===

std::string sourceMapSpanWriter::getSourceMapSpanString(
    sourcemap::Mapping* mapEntry, bool getAbsentNameIndex) {
	writerAggregator mapString;
	mapString.WriteStringf("Emitted(%d, %d)",
	                       {mapEntry->GeneratedLine + 1,
	                        (int64_t)mapEntry->GeneratedCharacter + 1});
	if (mapEntry->IsSourceMapping()) {
		mapString.WriteStringf(" Source(%d, %d) + SourceIndex(%d)",
		                       {mapEntry->SourceLine + 1,
		                        (int64_t)mapEntry->SourceCharacter + 1,
		                        (int64_t)mapEntry->SourceIndex});
		if (mapEntry->NameIndex >= 0 &&
		    mapEntry->NameIndex < (int)sourceMapNames.size()) {
			mapString.WriteStringf(
			    " name (%s)", {sourceMapNames[mapEntry->NameIndex]});
		} else {
			if (mapEntry->NameIndex != sourcemap::MissingName ||
			    getAbsentNameIndex) {
				mapString.WriteStringf(" nameIndex (%d)",
				                       {(int64_t)mapEntry->NameIndex});
			}
		}
	}
	return mapString.String();
}

// === recordSourceMapSpan — sourcemap_recorder.go:177 ===

void sourceMapSpanWriter::recordSourceMapSpan(
    sourcemap::Mapping* sourceMapSpan) {
	// verify the decoded span is same as the new span
	decodedMapping* decodeResult =
	    sourceMapDecoder->decodeNextEncodedSourceMapSpan();
	std::vector<std::string> decodeErrors;
	if (!decodeResult->error.empty() ||
	    !decodeResult->sourceMapSpan->Equals(sourceMapSpan)) {
		if (!decodeResult->error.empty()) {
			decodeErrors = {
			    "!!^^ !!^^ There was decoding error in the sourcemap at "
			    "this location: " +
			    decodeResult->error};
		} else {
			decodeErrors = {
			    "!!^^ !!^^ The decoded span from sourcemap's mapping entry "
			    "does not match what was encoded for this span:"};
		}
		decodeErrors.push_back(
		    "!!^^ !!^^ Decoded span from sourcemap's mappings entry: " +
		    getSourceMapSpanString(decodeResult->sourceMapSpan,
		                           true /*getAbsentNameIndex*/) +
		    " Span encoded by the emitter:" +
		    getSourceMapSpanString(sourceMapSpan,
		                           true /*getAbsentNameIndex*/));
	}

	if (!spansOnSingleLine.empty() &&
	    spansOnSingleLine[0].sourceMapSpan->GeneratedLine !=
	        sourceMapSpan->GeneratedLine) {
		// On different line from the one that we have been recording till now,
		writeRecordedSpans();
		spansOnSingleLine.clear();
	}
	spansOnSingleLine.push_back(sourceMapSpanWithDecodeErrors{
	    sourceMapSpan, std::move(decodeErrors)});
}

// === recordNewSourceFileSpan — sourcemap_recorder.go:210 ===

void sourceMapSpanWriter::recordNewSourceFileSpan(
    sourcemap::Mapping* sourceMapSpan, const std::string& newSourceFileCode) {
	bool continuesLine = false;
	if (!spansOnSingleLine.empty() &&
	    (int)spansOnSingleLine[0].sourceMapSpan->GeneratedCharacter ==
	        sourceMapSpan->GeneratedLine) { // !!! char == line seems like a
		                                    // bug in Strada?
		writeRecordedSpans();
		spansOnSingleLine.clear();
		nextJsLineToWrite--; // walk back one line to reprint the line
		continuesLine = true;
	}

	recordSourceMapSpan(sourceMapSpan);

	if (spansOnSingleLine.size() != 1) {
		TSC_UNREACHABLE("expected a single span");
	}

	sourceMapRecorder->WriteLine(
	    "-------------------------------------------------------------------");
	if (continuesLine) {
		sourceMapRecorder->WriteLinef(
		    "emittedFile:%s (%d, %d)",
		    {jsFile->UnitName, sourceMapSpan->GeneratedLine + 1,
		     (int64_t)sourceMapSpan->GeneratedCharacter + 1});
	} else {
		sourceMapRecorder->WriteLinef("emittedFile:%s", {jsFile->UnitName});
	}
	sourceMapRecorder->WriteLinef(
	    "sourceFile:%s",
	    {sourceMapSources[spansOnSingleLine[0].sourceMapSpan->SourceIndex]});
	sourceMapRecorder->WriteLine(
	    "-------------------------------------------------------------------");

	tsLineMap = computeECMALineStarts(newSourceFileCode);
	tsCode = newSourceFileCode;
	prevWrittenSourcePos = 0;
}

// === close — sourcemap_recorder.go:237 ===

void sourceMapSpanWriter::close() {
	// Write the lines pending on the single line
	writeRecordedSpans();

	if (!sourceMapDecoder->hasCompletedDecoding()) {
		sourceMapRecorder->WriteLine(
		    "!!!! **** There are more source map entries in the sourceMap's "
		    "mapping than what was encoded");
		sourceMapRecorder->WriteLinef("!!!! **** Remaining decoded string: %s",
		                            {sourceMapDecoder->getRemainingDecodeString()});
	}

	// write remaining js lines
	writeJsFileLines((int)jsLineMap.size());
}

// === getTextOfLine — sourcemap_recorder.go:251 ===

std::string_view sourceMapSpanWriter::getTextOfLine(
    int line, const std::vector<TextPos>& lineMap, const std::string& code) {
	TextPos startPos = lineMap[line];
	TextPos endPos;
	if (line + 1 < (int)lineMap.size()) {
		endPos = lineMap[line + 1];
	} else {
		endPos = (TextPos)code.size();
	}
	std::string_view text =
	    std::string_view(code).substr(startPos, endPos - startPos);
	if (line == 0) {
		return removeByteOrderMark(text);
	}
	return text;
}

// === writeJsFileLines — sourcemap_recorder.go:264 ===

void sourceMapSpanWriter::writeJsFileLines(int endJsLine) {
	for (; nextJsLineToWrite < endJsLine; nextJsLineToWrite++) {
		sourceMapRecorder->WriteStringf(
		    ">>>%s", {getTextOfLine(nextJsLineToWrite, jsLineMap,
		                          jsFile->Content)});
	}
}

// === writeRecordedSpans — sourcemap_recorder.go:270 ===

// recordedSpanWriter — sourcemap_recorder.go:275.
struct recordedSpanWriter {
	std::vector<std::string> markerIds;
	int prevEmittedCol = 0;
	sourceMapSpanWriter* w = nullptr;

	// getMarkerId — sourcemap_recorder.go:281.
	std::string getMarkerId(int markerIndex);
	// iterateSpans — sourcemap_recorder.go:297.
	template <class F>
	void iterateSpans(F&& fn);
	// writeSourceMapIndent — sourcemap_recorder.go:306.
	void writeSourceMapIndent(int indentLength, std::string_view indentPrefix);
	// writeSourceMapMarker — sourcemap_recorder.go:313.
	void writeSourceMapMarker(sourceMapSpanWithDecodeErrors* currentSpan,
	                          int index);
	// writeSourceMapMarkerEx — sourcemap_recorder.go:317.
	void writeSourceMapMarkerEx(sourceMapSpanWithDecodeErrors* currentSpan,
	                            int index, int endColumn, bool endContinues);
	// writeSourceMapSourceText — sourcemap_recorder.go:330.
	void writeSourceMapSourceText(sourceMapSpanWithDecodeErrors* currentSpan,
	                              int index);
	// writeSpanDetails — sourcemap_recorder.go:363.
	void writeSpanDetails(sourceMapSpanWithDecodeErrors* currentSpan,
	                      int index);
	// writeRecordedSpans — sourcemap_recorder.go:367.
	void writeRecordedSpans();
};

std::string recordedSpanWriter::getMarkerId(int markerIndex) {
	std::string markerId;
	if (w->spanMarkerContinues) {
		if (markerIndex != 0) {
			TSC_UNREACHABLE("expected markerIndex to be 0");
		}
		markerId = "1->";
	} else {
		markerId = std::to_string(markerIndex + 1);
		if (markerId.size() < 2) {
			markerId += " ";
		}
		markerId += ">";
	}
	return markerId;
}

template <class F>
void recordedSpanWriter::iterateSpans(F&& fn) {
	prevEmittedCol = 0;
	for (size_t i = 0; i < w->spansOnSingleLine.size(); i++) {
		fn(&w->spansOnSingleLine[i], (int)i);
		prevEmittedCol =
		    (int)w->spansOnSingleLine[i].sourceMapSpan->GeneratedCharacter;
	}
}

void recordedSpanWriter::writeSourceMapIndent(int indentLength,
                                              std::string_view indentPrefix) {
	w->sourceMapRecorder->WriteString(indentPrefix);
	for (int i = 0; i < indentLength; i++) {
		w->sourceMapRecorder->WriteString(" ");
	}
}

void recordedSpanWriter::writeSourceMapMarker(
    sourceMapSpanWithDecodeErrors* currentSpan, int index) {
	writeSourceMapMarkerEx(
	    currentSpan, index, (int)currentSpan->sourceMapSpan->GeneratedCharacter,
	    false /*endContinues*/);
}

void recordedSpanWriter::writeSourceMapMarkerEx(
    sourceMapSpanWithDecodeErrors* currentSpan, int index, int endColumn,
    bool endContinues) {
	std::string markerId = getMarkerId(index);
	markerIds.push_back(markerId);
	writeSourceMapIndent(prevEmittedCol, markerId);
	for (int i = prevEmittedCol; i < endColumn; i++) {
		w->sourceMapRecorder->WriteString("^");
	}
	if (endContinues) {
		w->sourceMapRecorder->WriteString("->");
	}
	w->sourceMapRecorder->WriteLine("");
	w->spanMarkerContinues = endContinues;
}

void recordedSpanWriter::writeSourceMapSourceText(
    sourceMapSpanWithDecodeErrors* currentSpan, int index) {
	// Convert UTF-16 character offset from the source map to a byte position.
	int sourcePos = computePositionOfLineAndUTF16Character(
	    w->tsLineMap, currentSpan->sourceMapSpan->SourceLine,
	    (int)currentSpan->sourceMapSpan->SourceCharacter, w->tsCode,
	    true /*allowEdits*/);
	std::string_view sourceText;
	if (w->prevWrittenSourcePos < sourcePos) {
		// Position that goes forward, get text
		sourceText = std::string_view(w->tsCode)
		                 .substr(w->prevWrittenSourcePos,
		                         sourcePos - w->prevWrittenSourcePos);
	}

	// If there are decode errors, write
	for (const auto& decodeError : currentSpan->decodeErrors) {
		writeSourceMapIndent(prevEmittedCol, markerIds[index]);
		w->sourceMapRecorder->WriteLine(decodeError);
	}

	auto tsCodeLineMap = computeECMALineStarts(sourceText);
	std::string sourceTextStr(sourceText);
	for (size_t i = 0; i < tsCodeLineMap.size(); i++) {
		if (i == 0) {
			writeSourceMapIndent(prevEmittedCol, markerIds[index]);
		} else {
			writeSourceMapIndent(prevEmittedCol, "  >");
		}
		w->sourceMapRecorder->WriteString(
		    w->getTextOfLine((int)i, tsCodeLineMap, sourceTextStr));
		if (i == tsCodeLineMap.size() - 1) {
			w->sourceMapRecorder->WriteLine("");
		}
	}

	w->prevWrittenSourcePos = sourcePos;
}

void recordedSpanWriter::writeSpanDetails(
    sourceMapSpanWithDecodeErrors* currentSpan, int index) {
	w->sourceMapRecorder->WriteLinef(
	    "%s%s",
	    {markerIds[index],
	     w->getSourceMapSpanString(currentSpan->sourceMapSpan,
	                               false /*getAbsentNameIndex*/)});
}

void recordedSpanWriter::writeRecordedSpans() {
	auto* w = this->w;
	if (!w->spansOnSingleLine.empty()) {
		int currentJsLine =
		    w->spansOnSingleLine[0].sourceMapSpan->GeneratedLine;

		// Write js line
		w->writeJsFileLines(currentJsLine + 1);

		// Emit markers
		iterateSpans([this](sourceMapSpanWithDecodeErrors* s, int i) {
			writeSourceMapMarker(s, i);
		});

		auto jsFileText = w->getTextOfLine(currentJsLine + 1, w->jsLineMap,
		                                 w->jsFile->Content); // TODO: Strada is
		                                                     // wrong here, we
		                                                     // should be looking
		                                                     // at `currentJsLine`,
		                                                     // not
		                                                     // `currentJsLine+1`
		if (prevEmittedCol < (int)jsFileText.size() - 1) {
			// There is remaining text on this line that will be part of next
			// source span so write marker that continues
			writeSourceMapMarkerEx(
			    nullptr /*currentSpan*/, (int)w->spansOnSingleLine.size(),
			    (int)jsFileText.size() - 1 /*endColumn*/,
			    true /*endContinues*/);
		}

		// Emit Source text
		iterateSpans([this](sourceMapSpanWithDecodeErrors* s, int i) {
			writeSourceMapSourceText(s, i);
		});

		// Emit column number etc
		iterateSpans([this](sourceMapSpanWithDecodeErrors* s, int i) {
			writeSpanDetails(s, i);
		});

		w->sourceMapRecorder->WriteLine("---");
	}
}

void sourceMapSpanWriter::writeRecordedSpans() {
	recordedSpanWriter rsw;
	rsw.w = this;
	rsw.writeRecordedSpans();
}

}  // namespace tsc::testutil::harnessutil
