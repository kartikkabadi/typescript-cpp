// source_map.go — content-mapper span maps and declaration source-map
// following for cross-file LS results.
#include "internal/ls/ls.h"

#include "internal/ast/ast.h"
#include "internal/compiler/program.h"
#include "internal/outputpaths/outputpaths.h"

namespace tsc::ls {

// sourceFileRangeToLSPLocation — source_map.go:18. Maps a range from an
// arbitrary program SourceFile to an LSP location, composing content-mapper
// span maps and declaration source maps as needed. LS features should use
// this for cross-file results instead of calling getMappedLocation or
// lsconv.ToLSPLocation directly. This unfiltered form is appropriate for
// diagnostics and text edits.
std::pair<lsproto::Location, spanmap::Fidelity>
LanguageService::sourceFileRangeToLSPLocation(SourceFile* file,
                                              TextRange fileRange) {
	if (file->ContentMapper() != "") {
		return converters->ToLSPLocation(file, fileRange);
	}
	return getMappedLocation(file->FileName(), fileRange);
}

// sourceFileRangeToLSPLocationForFeature — source_map.go:28. The preferred
// conversion for visible LS results that may come from another file. It
// applies content-mapper feature filtering and follows declaration source
// maps. Do not use it for diagnostics or text edits.
std::pair<lsproto::Location, spanmap::Fidelity>
LanguageService::sourceFileRangeToLSPLocationForFeature(
    SourceFile* file, TextRange fileRange, spanmap::Feature feature) {
	if (file->ContentMapper() != "") {
		return converters->ToLSPLocationForFeature(file, fileRange, feature);
	}
	return getMappedLocation(file->FileName(), fileRange);
}

// getMappedLocation — source_map.go:38. Follows declaration source maps from
// a .d.ts range to its source location. It is an implementation detail of
// sourceFileRangeToLSPLocation; LS features should not call it directly,
// because it does not preserve a content-mapper projection or apply span-map
// feature filtering.
std::pair<lsproto::Location, spanmap::Fidelity>
LanguageService::getMappedLocation(const std::string& fileName,
                                   TextRange fileRange) {
	auto* startPos = tryGetSourcePosition(fileName, fileRange.pos());
	if (startPos == nullptr) {
		auto [lspRange, fidelity] =
		    createLspRangeFromRange(fileRange, getScript(fileName));
		return {lsproto::Location{
		            .Uri = lsconv::FileNameToDocumentURI(fileName),
		            .Range = lspRange,
		        },
		        fidelity};
	}
	auto* endPos = tryGetSourcePosition(fileName, fileRange.end());
	if (endPos == nullptr || endPos->FileName != startPos->FileName ||
	    endPos->Pos < startPos->Pos) {
		// When end doesn't map, maps to a different source file (e.g. in a
		// .d.ts with a multi-source source map from --outFile compilation), or
		// maps to a position before start (non-monotonic source map mappings),
		// approximate the end position.
		endPos = new sourcemap::DocumentPosition{
		    .FileName = startPos->FileName,
		    .Pos = startPos->Pos + fileRange.len(),
		};
	}
	auto newRange = TextRange{startPos->Pos, endPos->Pos};
	auto [lspRange, fidelity] =
	    createLspRangeFromRange(newRange, getScript(startPos->FileName));
	return {lsproto::Location{
	            .Uri = lsconv::FileNameToDocumentURI(startPos->FileName),
	            .Range = lspRange,
	        },
	        fidelity};
}

// getScript — source_map.go:85.
script* LanguageService::getScript(const std::string& fileName) {
	auto [text, ok] = host->ReadFile(fileName);
	if (!ok) {
		return nullptr;
	}
	return new script{fileName, text};
}

// tryGetSourcePosition — source_map.go:93.
sourcemap::DocumentPosition* LanguageService::tryGetSourcePosition(
    const std::string& fileName, TextPos position) {
	auto* newPos = tryGetSourcePositionWorker(fileName, position);
	if (newPos != nullptr) {
		if (auto [_, ok] = ReadFile(newPos->FileName); !ok) {
			// File doesn't exist
			return nullptr;
		}
	}
	return newPos;
}

// tryGetSourcePositionWorker — source_map.go:106.
sourcemap::DocumentPosition* LanguageService::tryGetSourcePositionWorker(
    const std::string& fileName, TextPos position) {
	if (!tspath::isDeclarationFileName(fileName)) {
		return nullptr;
	}

	auto* positionMapper = GetDocumentPositionMapper(fileName);
	auto* documentPos = sourcemap::GetSourcePosition(
	    positionMapper, new sourcemap::DocumentPosition{
	                        .FileName = fileName, .Pos = (int)position});
	if (documentPos == nullptr) {
		return nullptr;
	}
	if (auto* newPos = tryGetSourcePositionWorker(
	        documentPos->FileName, TextPos(documentPos->Pos));
	    newPos != nullptr) {
		return newPos;
	}
	return documentPos;
}

// tryGetGeneratedPosition — source_map.go:125.
sourcemap::DocumentPosition* LanguageService::tryGetGeneratedPosition(
    const std::string& fileName, TextPos position) {
	auto* newPos = tryGetGeneratedPositionWorker(fileName, position);
	if (newPos != nullptr) {
		if (auto [_, ok] = ReadFile(newPos->FileName); !ok) {
			// File doesn't exist
			return nullptr;
		}
	}
	return newPos;
}

// tryGetGeneratedPositionWorker — source_map.go:138.
sourcemap::DocumentPosition* LanguageService::tryGetGeneratedPositionWorker(
    const std::string& fileName, TextPos position) {
	if (tspath::isDeclarationFileName(fileName)) {
		return nullptr;
	}

	auto* program = GetProgram();
	if (program == nullptr || program->GetSourceFile(fileName) == nullptr) {
		return nullptr;
	}

	auto path = toPath(fileName);
	// If this is source file of project reference source (instead of
	// redirect) there is no generated position
	if (program->IsSourceFromProjectReference(path)) {
		return nullptr;
	}

	auto declarationFileName =
	    outputpaths::GetOutputDeclarationFileNameWorker(fileName,
	                                                    program->Options(),
	                                                    program);
	auto* positionMapper = GetDocumentPositionMapper(declarationFileName);
	auto* documentPos = sourcemap::GetGeneratedPosition(
	    positionMapper, new sourcemap::DocumentPosition{
	                        .FileName = fileName, .Pos = (int)position});
	if (documentPos == nullptr) {
		return nullptr;
	}
	if (auto* newPos = tryGetGeneratedPositionWorker(
	        documentPos->FileName, TextPos(documentPos->Pos));
	    newPos != nullptr) {
		return newPos;
	}
	return documentPos;
}

} // namespace tsc::ls
