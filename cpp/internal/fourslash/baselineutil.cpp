// baselineutil.go — baseline emission for fourslash tests.
// Port of tsc/internal/fourslash/baselineutil.go (package fourslash).
#include "internal/bundled/bundled.h"
#include "internal/fourslash/fourslash.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace tsc::fourslash {

// ===========================================================================
// baselineutil.go:48-94 — baseline command plumbing
// ===========================================================================

// addResultToBaseline — baselineutil.go:48.
void FourslashTest::addResultToBaseline(gostd::testing::T* t,
                                        const baselineCommand& command,
                                        const std::string& actual) {
	t->Helper();
	gostr::Builder* b = nullptr;
	if (testData->isStateBaseliningEnabled()) {
		// Single baseline for all commands
		b = &stateBaseline_->baseline;
	} else {
		auto it = baselines.find(command);
		if (it != baselines.end()) {
			b = it->second.get();
		} else {
			auto builder = std::make_shared<gostr::Builder>();
			baselines[command] = builder;
			b = builder.get();
		}
	}
	if (b->Len() != 0) {
		b->WriteString("\n\n\n\n");
	}
	b->WriteString("// === ");
	b->WriteString(std::string(command));
	b->WriteString(" ===\n");
	b->WriteString(actual);
}

// writeToBaseline — baselineutil.go:68.
void FourslashTest::writeToBaseline(const baselineCommand& command,
                                    const std::string& content) {
	gostr::Builder* b = nullptr;
	auto it = baselines.find(command);
	if (it == baselines.end()) {
		auto builder = std::make_shared<gostr::Builder>();
		baselines[command] = builder;
		b = builder.get();
	} else {
		b = it->second.get();
	}
	b->WriteString(content);
}

// getBaselineFileName — baselineutil.go:77.
std::string getBaselineFileName(gostd::testing::T* t,
                                const baselineCommand& command) {
	return getBaseFileNameFromTest(t) + "." + getBaselineExtension(command);
}

// getBaselineExtension — baselineutil.go:81.
std::string getBaselineExtension(const baselineCommand& command) {
	if (command == quickInfoCmd || command == vsQuickInfoCmd ||
	    command == signatureHelpCmd || command == smartSelectionCmd ||
	    command == inlayHintsCmd || command == nonSuggestionDiagnosticsCmd ||
	    command == documentSymbolsCmd || command == closingTagCmd ||
	    command == vsFindAllReferencesCmd) {
		return "baseline";
	}
	if (command == callHierarchyCmd) {
		return "callHierarchy.txt";
	}
	if (command == autoImportsCmd) {
		return "baseline.md";
	}
	if (command == linkedEditingCmd) {
		return "linkedEditing.txt";
	}
	return "baseline.jsonc";
}

// getBaselineOptions — baselineutil.go:96.
testutil::baseline::Options FourslashTest::getBaselineOptions(
    const baselineCommand& command, const std::string& testPath) {
	return testutil::baseline::Options{
	    .Subfolder =
	        "fourslash/" + normalizeCommandName(std::string(command)),
	};
}

// dropTrailingEmptyLines — baselineutil.go:102.
std::vector<std::string> dropTrailingEmptyLines(
    std::vector<std::string> ss) {
	ss.erase(
	    std::find_if(ss.rbegin(), ss.rend(),
	                 [](const std::string& s) { return !s.empty(); })
	        .base(),
	    ss.end());
	return ss;
}

// normalizeCommandName — baselineutil.go:106.
std::string normalizeCommandName(const std::string& command) {
	auto words = gostr::fields(command);
	return gostr::lowerFirstChar(gostr::join(words, ""));
}

// ===========================================================================
// baselineutil.go:112-272 — location baselines
// ===========================================================================

// documentSpan lives in fourslash.h (baselineutil.go:112).

// locationToSpan — baselineutil.go:134.
documentSpan locationToSpan(const lsproto::Location& loc) {
	return documentSpan{
	    .uri = loc.Uri,
	    .textSpan = loc.Range,
	};
}

// getBaselineForLocationsWithFileContents — baselineutil.go:141.
std::string FourslashTest::getBaselineForLocationsWithFileContents(
    const std::vector<lsproto::Location>& locations,
    const baselineFourslashLocationsOptions& options) {
	return getBaselineForSpansWithFileContents(
	    gostr::coreMap(locations, locationToSpan), options);
}

// getBaselineForSpansWithFileContents — baselineutil.go:148.
std::string FourslashTest::getBaselineForSpansWithFileContents(
    const std::vector<documentSpan>& spans,
    const baselineFourslashLocationsOptions& options) {
	// collections.GroupBy preserves encounter order in Go.
	auto groupedRanges =
	    std::make_shared<collections::MultiMap<lsproto::DocumentUri,
	                                           documentSpan>>();
	for (auto& span : spans) {
		groupedRanges->Add(span.uri, span);
	}
	auto opts = options;
	if (opts.preserveResultOrder) {
		opts.orderedFiles = uniqueFilesInSpanOrder(spans);
	}
	return getBaselineForGroupedSpansWithFileContents(groupedRanges.get(),
	                                                opts);
}

// getBaselineForGroupedSpansWithFileContents — baselineutil.go:159.
std::string FourslashTest::getBaselineForGroupedSpansWithFileContents(
    collections::MultiMap<lsproto::DocumentUri, documentSpan>*
        groupedRanges,
    const baselineFourslashLocationsOptions& options) {
	// We must always print the file containing the marker,
	// but don't want to print it twice at the end if it already
	// found in a file with ranges.
	bool foundMarker = false;
	bool foundAdditionalLocation = false;
	std::unordered_map<documentSpan, int, documentSpanHash>
	    spanToContextId;

	std::vector<std::string> baselineEntries;
	auto addFileEntry = [&](const std::string& path) {
		auto fileName = lsconv::FileNameToDocumentURI(path);
		auto ranges = groupedRanges->Get(fileName);
		for (auto& r : ranges) {
		}
		if (ranges.empty()) {
			return;
		}

		auto [content, ok] = textOfFile(path);
		if (!ok) {
			return;
		}

		if (options.marker != nullptr &&
		    options.marker->FileName() == path) {
			foundMarker = true;
		}

		if (options.additionalSpan != nullptr &&
		    options.additionalSpan->uri == fileName) {
			foundAdditionalLocation = true;
		}

		baselineEntries.push_back(getBaselineContentForFile(
		    path, content, ranges, &spanToContextId, options));
	};
	if (options.preserveResultOrder) {
		for (auto& uri : options.orderedFiles) {
			addFileEntry(lsproto::documentUriFileName(uri));
		}
	} else {
		for (auto& path : getAccessibleFilePaths(vfs.get(), "/")) {
			addFileEntry(path);
		}
		for (auto& path :
		     getAccessibleFilePaths(vfs.get(), bundled::LibPath())) {
			addFileEntry(path);
		}
	}

	// In Strada, there is a bug where we only ever add additional spans to
	// baselines if we haven't already added the file to the baseline.
	if (options.additionalSpan != nullptr && !foundAdditionalLocation) {
		std::string fileName =
		    lsproto::documentUriFileName(options.additionalSpan->uri);
		if (auto [content, ok] = textOfFile(fileName); ok) {
			baselineEntries.push_back(getBaselineContentForFile(
			    fileName, content, {*options.additionalSpan},
			    &spanToContextId, options));
			if (options.marker != nullptr &&
			    options.marker->FileName() == fileName) {
				foundMarker = true;
			}
		}
	}

	if (!foundMarker && options.marker != nullptr) {
		// If we didn't find the marker in any file, we need to add it.
		auto markerFileName = options.marker->FileName();
		if (auto [content, ok] = textOfFile(markerFileName); ok) {
			baselineEntries.push_back(getBaselineContentForFile(
			    markerFileName, content, {}, &spanToContextId,
			    options));
		}
	}

	// !!! skipDocumentContainingOnlyMarker

	return gostr::join(baselineEntries, "\n\n");
}

// getAccessibleFilePaths — baselineutil.go:231.
std::vector<std::string> getAccessibleFilePaths(vfs::FS* fileSystem,
                                              const std::string& root) {
	if (!fileSystem->DirectoryExists(root)) {
		return {};
	}
	std::vector<std::string> files;
	auto err = vfs::WalkDir(
	    *fileSystem, root,
	    [&](const std::string& path,
	        const std::shared_ptr<vfs::DirEntry>& entry,
	        const vfs::Error& err) -> vfs::Error {
		    if (err) {
			    return err;
		    }
		    if (entry->Type().IsRegular()) {
			    files.push_back(path);
		    }
		    return {};
	    });
	if (err) {
		TSC_UNREACHABLE(("walkdir error during fourslash baseline: " +
		                 err.str())
		                    .c_str());
	}
	return files;
}

// uniqueFilesInSpanOrder — baselineutil.go:251.
std::vector<lsproto::DocumentUri> uniqueFilesInSpanOrder(
    const std::vector<documentSpan>& spans) {
	if (spans.empty()) {
		return {};
	}
	std::set<lsproto::DocumentUri> seen;
	std::vector<lsproto::DocumentUri> result;
	result.reserve(spans.size());
	for (auto& span : spans) {
		if (seen.count(span.uri)) {
			continue;
		}
		seen.insert(span.uri);
		result.push_back(span.uri);
	}
	return result;
}

// textOfFile — baselineutil.go:267.
std::pair<std::string, bool> FourslashTest::textOfFile(
    const std::string& fileName) {
	if (openFiles.count(fileName)) {
		return {getScriptInfo(fileName)->content, true};
	}
	return vfs->ReadFile(fileName);
}

// ===========================================================================
// baselineutil.go:274-317 — baselineDetail
// ===========================================================================

// getRange — baselineutil.go:299.
lsproto::Range baselineDetail::getRange() const {
	switch (kind) {
	case detailKind::contextStart:
	case detailKind::contextEnd:
		return *span->contextSpan;
	case detailKind::textStart:
	case detailKind::textEnd:
		return span->textSpan;
	case detailKind::marker:
		return lsproto::Range{pos, pos};
	default:
		TSC_UNREACHABLE("unknown detail kind");
	}
}

// ===========================================================================
// baselineutil.go:319-525 — getBaselineContentForFile
// ===========================================================================

std::string FourslashTest::getBaselineContentForFile(
    const std::string& fileName, const std::string& content,
    const std::vector<documentSpan>& spansInFile,
    std::unordered_map<documentSpan, int, documentSpanHash>*
        spanToContextId,
    const baselineFourslashLocationsOptions& options) {
	std::vector<std::shared_ptr<baselineDetail>> details;
	std::unordered_map<baselineDetail*, std::string> detailPrefixes;
	std::unordered_map<baselineDetail*, std::string> detailSuffixes;
	bool canDetermineContextIdInline = true;

	if (options.marker != nullptr &&
	    options.marker->FileName() == fileName) {
		auto d = std::make_shared<baselineDetail>();
		d->pos = options.marker->LSPos();
		d->positionMarker = options.markerName;
		details.push_back(d);
	}

	for (auto& span : spansInFile) {
		auto spanPtr = std::make_shared<documentSpan>(span);
		int contextSpanIndex = (int)details.size();

		// Add context span markers if present
		if (span.contextSpan != nullptr) {
			auto d = std::make_shared<baselineDetail>();
			d->pos = span.contextSpan->Start;
			d->positionMarker = "<|";
			d->span = spanPtr;
			d->kind = detailKind::contextStart;
			details.push_back(d);

			// Check if context span starts after text span
			if (lsproto::ComparePositions(span.contextSpan->Start,
			                              span.textSpan.Start) > 0) {
				canDetermineContextIdInline = false;
			}
		}

		int textSpanIndex = (int)details.size();
		std::string startMarker = "[|";
		if (options.getLocationData) {
			startMarker += options.getLocationData(span);
		}
		{
			auto d1 = std::make_shared<baselineDetail>();
			d1->pos = span.textSpan.Start;
			d1->positionMarker = startMarker;
			d1->span = spanPtr;
			d1->kind = detailKind::textStart;
			details.push_back(d1);
			auto d2 = std::make_shared<baselineDetail>();
			d2->pos = span.textSpan.End;
			d2->positionMarker =
			    options.endMarker.empty() ? "|]" : options.endMarker;
			d2->span = spanPtr;
			d2->kind = detailKind::textEnd;
			details.push_back(d2);
		}

		if (span.contextSpan != nullptr) {
			auto d = std::make_shared<baselineDetail>();
			d->pos = span.contextSpan->End;
			d->positionMarker = "|>";
			d->span = spanPtr;
			d->kind = detailKind::contextEnd;
			details.push_back(d);
		}

		if (options.startMarkerPrefix) {
			auto startPrefix = options.startMarkerPrefix(span);
			if (startPrefix != nullptr) {
				// Special case: if this span starts at the same
				// position as the provided marker, we want the span's
				// prefix to appear before the marker name.
				// i.e. We want `/*START PREFIX*/A:
				// /*RENAME*/[|ARENAME|]`, not
				// `/*RENAME*//*START PREFIX*/A: [|ARENAME|]`
				if (options.marker != nullptr &&
				    fileName == options.marker->FileName() &&
				    span.textSpan.Start ==
				        options.marker->LSPos()) {
					auto it = detailPrefixes.find(
					    details[0].get());
					tsc::debug::assert(
					    it == detailPrefixes.end(),
					    "Expected only single prefix at "
					    "marker location");
					detailPrefixes[details[0].get()] =
					    *startPrefix;
				} else if (span.contextSpan != nullptr &&
				           span.contextSpan->Start ==
				               span.textSpan.Start) {
					detailPrefixes
					    [details[contextSpanIndex].get()] =
					        *startPrefix;
				} else {
					detailPrefixes[details[textSpanIndex]
					                   .get()] =
					    *startPrefix;
				}
			}
		}

		if (options.endMarkerSuffix) {
			auto endSuffix = options.endMarkerSuffix(span);
			if (endSuffix != nullptr) {
				// Same as above for suffixes:
				if (options.marker != nullptr &&
				    fileName == options.marker->FileName() &&
				    span.textSpan.End == options.marker->LSPos()) {
					detailSuffixes[details[0].get()] =
					    *endSuffix;
				} else if (span.contextSpan != nullptr &&
				           span.contextSpan->End ==
				               span.textSpan.End) {
					detailSuffixes[details[textSpanIndex + 2]
					                   .get()] = *endSuffix;
				} else {
					detailSuffixes[details[textSpanIndex + 1]
					                   .get()] = *endSuffix;
				}
			}
		}
	}

	// Our preferred way to write markers is
	// /*MARKER*/[| some text |]
	// [| some /*MARKER*/ text |]
	// [| some text |]/*MARKER*/
	std::stable_sort(
	    details.begin(), details.end(),
	    [](const std::shared_ptr<baselineDetail>& d1,
	       const std::shared_ptr<baselineDetail>& d2) {
		    int c = lsproto::ComparePositions(d1->pos, d2->pos);
		    if (c != 0 ||
		        (d1->kind == detailKind::marker &&
		         d2->kind == detailKind::marker)) {
			    return c < 0;
		    }

		    // /*MARKER*/[| some text |]
		    if (d1->kind == detailKind::marker &&
		        isStart(d2->kind)) {
			    return true;
		    }
		    if (d2->kind == detailKind::marker &&
		        isStart(d1->kind)) {
			    return false;
		    }

		    // [| some text |]/*MARKER*/
		    if (d1->kind == detailKind::marker && isEnd(d2->kind)) {
			    return false;
		    }
		    if (d2->kind == detailKind::marker && isEnd(d1->kind)) {
			    return true;
		    }

		    // [||] or <||>
		    if (d1->span == d2->span) {
			    return (int)d1->kind < (int)d2->kind;
		    }

		    // ...|><|...
		    if (isStart(d1->kind) && isEnd(d2->kind)) {
			    return false;
		    }
		    if (isEnd(d1->kind) && isStart(d2->kind)) {
			    return true;
		    }

		    // <| ... [| ... |]|>
		    if (isEnd(d1->kind) && isEnd(d2->kind)) {
			    int c2 = lsproto::ComparePositions(
			        d2->getRange().Start, d1->getRange().Start);
			    if (c2 != 0) {
				    return c2 < 0;
			    }
			    return (int)d1->kind < (int)d2->kind;
		    }

		    // <|[| ... |] ... |>
		    if (isStart(d1->kind) && isStart(d2->kind)) {
			    int c2 = lsproto::ComparePositions(
			        d2->getRange().End, d2->getRange().End);
			    if (c2 != 0) {
				    return c2 < 0;
			    }
			    return (int)d1->kind < (int)d2->kind;
		    }

		    return false;
	    });
	// !!! if canDetermineContextIdInline

	auto twc = newTextWithContext(fileName, content);
	for (int index = 0; index < (int)details.size(); index++) {
		auto& detail = details[index];
		twc->add(detail.get());
		twc->pos = detail->pos;
		// Prefix
		auto prefixIt = detailPrefixes.find(detail.get());
		if (prefixIt != detailPrefixes.end() &&
		    !prefixIt->second.empty()) {
			twc->newContent->WriteString(prefixIt->second);
		}
		twc->newContent->WriteString(detail->positionMarker);
		if (detail->span != nullptr) {
			switch (detail->kind) {
			case detailKind::textStart: {
				std::string text;
				auto idIt =
				    spanToContextId->find(*detail->span);
				if (idIt != spanToContextId->end()) {
					int contextId = idIt->second;
					bool isAfterContextStart = false;
					for (int textStartIndex = index - 1;
					     textStartIndex >= 0;
					     textStartIndex--) {
						auto& textStartDetail =
						    details[textStartIndex];
						if (textStartDetail->kind ==
						            detailKind::contextStart &&
						    textStartDetail->span ==
						        detail->span) {
							isAfterContextStart =
							    true;
							break;
						}
						// Marker is ok to skip over
						if (textStartDetail->span !=
						    nullptr) {
							break;
						}
					}
					// Skip contextId on span thats
					// surrounded by context span
					// immediately
					if (!isAfterContextStart) {
						if (text.empty()) {
							text = gostd::sprintf(
							    "contextId: %v",
							    {contextId});
						} else {
							text = gostd::sprintf(
							           "contextId: %v",
							           {contextId}) +
							       ", " + text;
						}
					}
				}
				if (!text.empty()) {
					twc->newContent->WriteString(
					    "{ ");
					twc->newContent->WriteString(text);
					twc->newContent->WriteString(
					    " |}");
				}
				break;
			}
			case detailKind::contextStart:
				if (canDetermineContextIdInline) {
					(*spanToContextId)[*detail->span] =
					    (int)spanToContextId->size();
				}
				break;
			default:
				break;
			}
		}
		auto suffixIt = detailSuffixes.find(detail.get());
		if (suffixIt != detailSuffixes.end()) {
			twc->newContent->WriteString(suffixIt->second);
		}
	}
	twc->add(nullptr);
	if (twc->newContent->Len() != 0) {
		twc->readableContents->WriteString("\n");
		twc->readableJsoncBaseline(twc->newContent->String());
	}
	return twc->readableContents->String();
}

// ===========================================================================
// baselineutil.go:527-664 — textWithContext
// ===========================================================================

// splitIntoLines — baselineutil.go:527 (lineSplitter `\r?\n`).
std::vector<std::string> splitIntoLines(const std::string& text) {
	std::vector<std::string> lines;
	size_t start = 0;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '\n') {
			size_t end = i;
			if (end > start && text[end - 1] == '\r') {
				end--;
			}
			lines.push_back(text.substr(start, end - start));
			start = i + 1;
		}
	}
	lines.push_back(text.substr(start));
	return lines;
}

// newTextWithContext — baselineutil.go:566.
std::shared_ptr<textWithContext> newTextWithContext(
    const std::string& fileName, const std::string& content) {
	auto t = std::make_shared<textWithContext>();
	t->nLinesContext = 4;
	t->readableContents = std::make_shared<gostr::Builder>();
	t->isLibFile_ = isLibFile(fileName);
	t->newContent = std::make_shared<gostr::Builder>();
	t->pos = lsproto::Position{0, 0};
	t->fileName = fileName;
	t->content = content;
	t->lineStarts = lsconv::ComputeLSPLineStarts(content);

	t->converters = std::make_shared<testConverters>(
	    lsproto::PositionEncodingKindUTF8,
	    [t](const std::string&) -> lsconv::LSPLineMap* {
		    return t->lineStarts;
	    });
	t->readableContents->WriteString("// === ");
	t->readableContents->WriteString(fileName);
	t->readableContents->WriteString(" ===");
	return t;
}

// add — baselineutil.go:589.
void textWithContext::add(baselineDetail* detail) {
	if (newContent->Len() == 0 && detail == nullptr) {
		TSC_UNREACHABLE("Unsupported");
	}
	if (detail == nullptr ||
	    (detail->kind != detailKind::textEnd &&
	     detail->kind != detailKind::contextEnd)) {
		// Calculate pos to location number of lines
		int posLineIndex = lineInfo;
		if (posInfo == nullptr || !(*posInfo == pos)) {
			posLineIndex = lineStarts->ComputeIndexOfLineStart(
			    (int)converters->LineAndCharacterToPosition(this,
			                                              pos));
		}

		int locationLineIndex = (int)lineStarts->LineStarts.size() - 1;
		if (detail != nullptr) {
			locationLineIndex = lineStarts->ComputeIndexOfLineStart(
			    (int)converters->LineAndCharacterToPosition(
			        this, detail->pos));
			posInfo = std::make_shared<lsproto::Position>(detail->pos);
			lineInfo = locationLineIndex;
		}

		int nLines = 0;
		if (newContent->Len() != 0) {
			nLines += nLinesContext + 1;
		}
		if (detail != nullptr) {
			nLines += nLinesContext + 1;
		}
		// first nLinesContext and last nLinesContext
		if (locationLineIndex - posLineIndex > nLines) {
			if (newContent->Len() != 0) {
				std::string skippedString;
				if (isLibFile_) {
					skippedString =
					    "--- (line: --) skipped ---\n";
				} else {
					skippedString = gostd::sprintf(
					    "--- (line: %v) skipped ---",
					    {posLineIndex + nLinesContext +
					     1});
				}

				readableContents->WriteString("\n");
				auto fromIdx = sliceOfContent(
				    getIndex(pos),
				    getIndex(TextPos(
				        lineStarts->LineStarts
				            [posLineIndex +
				             nLinesContext])));
				readableJsoncBaseline(
				    newContent->String() + fromIdx +
				    skippedString);

				if (detail != nullptr) {
					readableContents->WriteString("\n");
				}
				newContent->Reset();
			}
			if (detail != nullptr) {
				if (isLibFile_) {
					newContent->WriteString(
					    "--- (line: --) skipped ---\n");
				} else {
					newContent->WriteString(gostd::sprintf(
					    "--- (line: %v) skipped ---\n",
					    {locationLineIndex -
					         nLinesContext + 1}));
				}
				newContent->WriteString(sliceOfContent(
				    getIndex(TextPos(
				        lineStarts->LineStarts
				            [locationLineIndex -
				             nLinesContext + 1])),
				    getIndex(detail->pos)));
			}
			return;
		}
	}
	if (detail == nullptr) {
		newContent->WriteString(
		    sliceOfContent(getIndex(pos), std::nullopt));
	} else {
		newContent->WriteString(
		    sliceOfContent(getIndex(pos), getIndex(detail->pos)));
	}
}

// readableJsoncBaseline — baselineutil.go:656.
void textWithContext::readableJsoncBaseline(const std::string& text) {
	auto lines = splitIntoLines(text);
	for (size_t i = 0; i < lines.size(); i++) {
		if (i > 0) {
			readableContents->WriteString("\n");
		}
		readableContents->WriteString("// ");
		readableContents->WriteString(lines[i]);
	}
}

// sliceOfContent — baselineutil.go:763.
std::string textWithContext::sliceOfContent(std::optional<int> start,
                                            std::optional<int> end) {
	int s = start.value_or(-1);
	int e = end.value_or(-1);
	if (!start.has_value() || s < 0) {
		s = 0;
	}
	if (!end.has_value() || e > (int)content.size()) {
		e = (int)content.size();
	}
	if (s > e) {
		return "";
	}
	return content.substr((size_t)s, (size_t)(e - s));
}

// getIndex(lsproto::Position) — baselineutil.go:779 (any-switch).
int textWithContext::getIndex(const lsproto::Position& p) {
	return (int)converters->LineAndCharacterToPosition(this, p);
}

// ===========================================================================
// baselineutil.go:797-803 — helpers
// ===========================================================================

// codeFence — baselineutil.go:797.
std::string codeFence(const std::string& lang, const std::string& code) {
	return "```" + lang + "\n" + code + "\n```";
}

// symbolInformationToData — baselineutil.go:801.
std::string symbolInformationToData(
    const std::shared_ptr<lsproto::SymbolInformation>& symbol) {
	return gostd::sprintf(
	    "{| name: %s, kind: %s |}",
	    {symbol->Name, lsproto::String(symbol->Kind)});
}

} // namespace tsc::fourslash
