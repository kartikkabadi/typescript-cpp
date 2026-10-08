// Port of tsc/internal/sourcemap — source-map (base64-VLQ) generation,
// decoding, and position mapping. Field/method names mirror the Go package.
#pragma once

#include "internal/gostd/gostd.h"
#include "internal/ast/ast.h" // TSC_UNREACHABLE / TSC_ASSERT
#include "internal/core/arena.h"
#include "internal/core/text.h" // TextPos, UTF16Offset, ECMALineStarts
#include "internal/tspath/tspath.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tsc::sourcemap {

// ---------------------------------------------------------------------------
// source.go
// ---------------------------------------------------------------------------

// Source (source.go:5) — a source of positions for source maps.
struct Source {
	virtual ~Source() = default;
	virtual std::string_view FileName() = 0;
	virtual std::string_view Text() = 0;
	virtual const std::vector<TextPos>& ECMALineMap() = 0;
};

// ---------------------------------------------------------------------------
// lineinfo.go
// ---------------------------------------------------------------------------

// ECMALineInfo (lineinfo.go:5)
struct ECMALineInfo {
	std::string text;
	ECMALineStarts lineStarts;

	int LineCount() const;
	std::string_view LineText(int line) const;
};

ECMALineInfo* CreateECMALineInfo(std::string_view text, ECMALineStarts lineStarts);

// ---------------------------------------------------------------------------
// generator.go
// ---------------------------------------------------------------------------

using SourceIndex = int32_t; // Go `SourceIndex int`
using NameIndex = int32_t;   // Go `NameIndex int`

constexpr SourceIndex sourceIndexNotSet = -1;
constexpr NameIndex nameIndexNotSet = -1;
constexpr int notSet = -1;
constexpr UTF16Offset notSetUTF16 = -1;

// RawSourceMap (generator.go:56). SourcesContent mirrors
// `json:"sourcesContent,omitzero"`: std::nullopt omits the field entirely.
struct RawSourceMap {
	int Version = 0;                                              // json:"version"
	std::string File;                                             // json:"file"
	std::string SourceRoot;                                       // json:"sourceRoot"
	std::vector<std::string> Sources;                             // json:"sources"
	std::vector<std::string> Names;                               // json:"names"
	std::string Mappings;                                         // json:"mappings"
	std::optional<std::vector<std::optional<std::string>>> SourcesContent; // json:"sourcesContent,omitzero"

	bool operator==(const RawSourceMap&) const = default;
};

// Generator (generator.go:26). Go `error` results are returned as
// gostd::Error (nullptr == nil); callers only use truthiness.
struct Generator {
	tspath::ComparePathsOptions pathOptions;
	std::string file;
	std::string sourceRoot;
	std::string sourcesDirectoryPath;
	std::vector<std::string> rawSources;
	std::vector<std::string> sources;
	std::unordered_map<std::string, SourceIndex> sourceToSourceIndexMap;
	std::vector<std::optional<std::string>> sourcesContent; // []*string
	std::vector<std::string> names;
	std::unordered_map<std::string, NameIndex> nameToNameIndexMap;
	std::string mappings; // strings.Builder
	int lastGeneratedLine = 0;
	UTF16Offset lastGeneratedCharacter = 0;
	SourceIndex lastSourceIndex = 0;
	int lastSourceLine = 0;
	UTF16Offset lastSourceCharacter = 0;
	NameIndex lastNameIndex = 0;
	bool hasLast = false;
	int pendingGeneratedLine = 0;
	UTF16Offset pendingGeneratedCharacter = 0;
	SourceIndex pendingSourceIndex = 0;
	int pendingSourceLine = 0;
	UTF16Offset pendingSourceCharacter = 0;
	NameIndex pendingNameIndex = 0;
	bool hasPending = false;
	bool hasPendingSource = false;
	bool hasPendingName = false;

	const std::vector<std::string>& Sources() const { return rawSources; }
	SourceIndex AddSource(std::string_view fileName);
	gostd::Error SetSourceContent(SourceIndex sourceIndex, std::string_view content);
	NameIndex AddName(std::string_view name);
	gostd::Error AddGeneratedMapping(int generatedLine, UTF16Offset generatedCharacter);
	gostd::Error AddSourceMapping(int generatedLine, UTF16Offset generatedCharacter,
	                     SourceIndex sourceIndex, int sourceLine,
	                     UTF16Offset sourceCharacter);
	gostd::Error AddNamedSourceMapping(int generatedLine, UTF16Offset generatedCharacter,
	                          SourceIndex sourceIndex, int sourceLine,
	                          UTF16Offset sourceCharacter, NameIndex nameIndex);
	struct RawSourceMap* RawSourceMap();
	std::string String();
	std::string Base64DataURL();

private:
	bool isNewGeneratedPosition(int generatedLine,
	                            UTF16Offset generatedCharacter) const;
	bool isBacktrackingSourcePosition(SourceIndex sourceIndex, int sourceLine,
	                                  UTF16Offset sourceCharacter) const;
	bool shouldCommitMapping() const;
	void appendMappingCharCode(char32_t charCode);
	void appendBase64VLQ(int64_t inValue);
	void commitPendingMapping();
	void addMapping(int generatedLine, UTF16Offset generatedCharacter,
	                SourceIndex sourceIndex, int sourceLine,
	                UTF16Offset sourceCharacter, NameIndex nameIndex);
	std::string bytes();
};

Generator* NewGenerator(std::string_view file, std::string_view sourceRoot,
                        std::string_view sourcesDirectoryPath,
                        const tspath::ComparePathsOptions& options);

// ---------------------------------------------------------------------------
// decoder.go
// ---------------------------------------------------------------------------

// Mapping (decoder.go:10)
struct Mapping {
	int GeneratedLine = 0;
	UTF16Offset GeneratedCharacter = 0;
	SourceIndex SourceIndex = 0;
	int SourceLine = 0;
	UTF16Offset SourceCharacter = 0;
	NameIndex NameIndex = 0;

	bool Equals(const Mapping* other) const;
	bool IsSourceMapping() const;
};

constexpr SourceIndex MissingSource = -1;
constexpr NameIndex MissingName = -1;
constexpr int MissingLineOrColumn = -1;
constexpr UTF16Offset MissingUTF16Column = -1;

// MappingsDecoder (decoder.go:41). `error` is the stored Go `error`; empty ==
// nil. Values() mirrors iter.Seq[*Mapping] as a yield callback.
struct MappingsDecoder {
	std::string mappings;
	bool done = false;
	int pos = 0;
	int generatedLine = 0;
	int64_t generatedCharacter = 0; // core.UTF16Offset (Go int width)
	int64_t sourceIndex = 0;        // SourceIndex (Go int width)
	int64_t sourceLine = 0;
	int64_t sourceCharacter = 0; // core.UTF16Offset
	int64_t nameIndex = 0;       // NameIndex
	std::string error;
	tsc::Arena mappingArena; // core.Arena[Mapping]

	explicit MappingsDecoder(std::string_view mappings) : mappings(mappings) {}

	const std::string& MappingsString() const { return mappings; }
	int Pos() const { return pos; }
	const std::string& Error() const { return error; }
	Mapping* State();

	// Values — decoder.go:75 (iter.Seq[*Mapping])
	template <class Yield>
	void Values(Yield&& yield) {
		for (;;) {
			auto result = Next();
			if (result.second) {
				break;
			}
			if (!yield(result.first)) {
				break;
			}
		}
	}

	std::pair<Mapping*, bool> Next();

private:
	Mapping* captureMapping(bool hasSource, bool hasName);
	std::pair<Mapping*, bool> stopIterating();
	void setError(std::string_view err);
	std::pair<Mapping*, bool> setErrorAndStopIterating(std::string_view err);
	bool hasReportedError() const { return !error.empty(); }
	bool isSourceMappingSegmentEnd() const;
	int64_t base64VLQFormatDecode();
};

MappingsDecoder* DecodeMappings(std::string_view mappings);

// ---------------------------------------------------------------------------
// source_mapper.go
// ---------------------------------------------------------------------------

// Host (source_mapper.go:16)
struct Host {
	virtual ~Host() = default;
	virtual bool UseCaseSensitiveFileNames() = 0;
	virtual ECMALineInfo* GetECMALineInfo(std::string_view fileName) = 0;
	virtual std::pair<std::string, bool> ReadFile(std::string_view fileName) = 0;
};

constexpr int missingPosition = -1; // source_mapper.go:31

// MappedPosition (source_mapper.go:23) — like `Mapping`, but position-based.
struct MappedPosition {
	int generatedPosition = 0;
	int sourcePosition = 0;
	SourceIndex sourceIndex = 0;
	NameIndex nameIndex = 0;

	bool isSourceMappedPosition() const {
		return sourceIndex != MissingSource && sourcePosition != missingPosition;
	}
};

using SourceMappedPosition = MappedPosition;

// DocumentPosition (source_mapper.go:164)
struct DocumentPosition {
	std::string FileName;
	int Pos = 0;
};

// DocumentPositionMapper (source_mapper.go:41) — maps source positions to
// generated positions and vice versa.
struct DocumentPositionMapper {
	bool useCaseSensitiveFileNames = false;
	std::vector<std::string> sourceFileAbsolutePaths;
	std::unordered_map<std::string, SourceIndex> sourceToSourceIndexMap;
	std::string generatedAbsoluteFilePath;
	std::vector<MappedPosition*> generatedMappings;
	std::unordered_map<SourceIndex, std::vector<SourceMappedPosition*>> sourceMappings;

};

// Free functions (spanmap convention): Go callers may invoke these on a nil
// *documentPositionMapper — nil-safe by construction.
DocumentPosition* GetSourcePosition(const DocumentPositionMapper* m,
                                    const DocumentPosition* loc);
DocumentPosition* GetGeneratedPosition(const DocumentPositionMapper* m,
                                       const DocumentPosition* loc);

DocumentPositionMapper* GetDocumentPositionMapper(Host* host,
                                                  std::string_view generatedFileName);

// ---------------------------------------------------------------------------
// util.go
// ---------------------------------------------------------------------------

// TryGetSourceMappingURL (util.go:11) — finds the sourceMappingURL comment at
// the end of a file.
std::string TryGetSourceMappingURL(ECMALineInfo* lineInfo);

} // namespace tsc::sourcemap
