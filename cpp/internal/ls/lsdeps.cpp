// === slice: ls-coreA (dep-stub bodies) ===
// Dep-stubs for sibling packages (lsutil, change, autoimport) that
// have not landed on this branch. Every body is TSC_UNREACHABLE; the owner
// slices provide real ports.
//
// The lsconv section below is the REAL port of converters.go's SourceFile
// projections; the templated members live in lsdeps.h.

#include "internal/ls/lsdeps.h"

// ls.h is included (after lsdeps.h resolves its own decls) so the tsc::ls
// dep-stub decls near the end of ls.h get bodies here.
#include "internal/ls/ls.h"


namespace tsc::lsconv {

namespace {

// sourceFileProjections — converters.go:232.
std::vector<SourceFile*> sourceFileProjections(SourceFile* file) {
	const auto* supplemental = file->SupplementalSourceFiles();
	std::vector<SourceFile*> files;
	files.reserve(1 + (supplemental != nullptr ? supplemental->size() : 0));
	files.push_back(file);
	if (supplemental != nullptr) {
		files.insert(files.end(), supplemental->begin(), supplemental->end());
	}
	return files;
}

} // namespace

// converters.go:218 FromLSPPositionForSourceFile.
std::vector<MappedPosition<SourceFile*>>
Converters::FromLSPPositionForSourceFile(SourceFile* file,
                                         lsproto::Position position,
                                         spanmap::Feature feature) {
	return lspPositionToVirtualForScripts(sourceFileProjections(file),
	                                      position, feature);
}

// converters.go:134 FromLSPRangeForSourceFile.
std::vector<MappedSpan<SourceFile*>> Converters::FromLSPRangeForSourceFile(
    SourceFile* file, lsproto::Range textRange, spanmap::Feature feature) {
	return lspRangeToVirtualForScripts(sourceFileProjections(file), textRange,
	                                   feature);
}

// converters.go:149 FromLSPRangeIntersectingForSourceFile.
std::vector<MappedSpan<SourceFile*>>
Converters::FromLSPRangeIntersectingForSourceFile(
    SourceFile* file, lsproto::Range textRange, spanmap::Feature feature) {
	auto files = sourceFileProjections(file);
	std::vector<MappedSpan<SourceFile*>> result;
	result.reserve(files.size());
	for (auto* script : files) {
		spanmap::SpanMap* spans = script->SpanMap();
		if (spans == nullptr) {
			MappedSpan<SourceFile*> ms;
			ms.Script = script;
			ms.Span = TextRange{
			    lineAndCharacterToPosition(script, textRange.Start),
			    lineAndCharacterToPosition(script, textRange.End)};
			ms.Fidelity = spanmap::FidelityExact;
			result.push_back(ms);
			continue;
		}
		originalTextScript original{script->OriginalFileName(),
		                            script->OriginalText()};
		TextRange originalRange{
		    lineAndCharacterToPosition(&original, textRange.Start),
		    lineAndCharacterToPosition(&original, textRange.End)};
		for (const auto& mapped :
		     spanmap::OriginalToVirtualIntersectingSpans(spans, originalRange,
		                                               feature)) {
			result.push_back(MappedSpan<SourceFile*>{mapped, script});
		}
	}
	return result;
}

// converters.go:43 NewConverters.
Converters* NewConverters(
    lsproto::PositionEncodingKind positionEncoding,
    std::function<LSPLineMap*(const std::string&)> getLineMap) {
	return new Converters(std::move(positionEncoding), std::move(getLineMap));
}

} // namespace tsc::lsconv



namespace tsc::ls {

// === dep-stubs for sibling ls-slice items declared in ls.h ===

argumentListInfo* getImmediatelyContainingArgumentInfo(
    Node* node, int position, SourceFile* sourceFile, checker::Checker* c);

missingMemberFixer* newMissingMemberFixer(
    change::Tracker* changeTracker, compiler::SimpleProgram* program,
    checker::Checker* typeChecker, const lsutil::UserPreferences& preferences,
    autoimport::ImportAdder* importAdder, locale::Locale locale) {
	TSC_UNREACHABLE(
	    "newMissingMemberFixer — ls/codeactions_missingmemberfixer slice");
}


} // namespace tsc::ls

