// === slice: ls-coreA (dep-stub bodies) ===
// Dep-stubs for sibling packages (lsutil, lsconv, change, autoimport) that
// have not landed on this branch. Every body is TSC_UNREACHABLE; the owner
// slices provide real ports.

#include "internal/ls/lsdeps.h"

// ls.h is included (after lsdeps.h resolves its own decls) so the tsc::ls
// dep-stub decls near the end of ls.h get bodies here.
#include "internal/ls/ls.h"


namespace tsc::lsconv {

std::vector<MappedPosition<SourceFile*>>
Converters::FromLSPPositionForSourceFile(SourceFile* file,
                                         lsproto::Position position,
                                         spanmap::Feature feature) {
	TSC_UNREACHABLE("Converters::FromLSPPositionForSourceFile — lsconv slice");
}
std::vector<MappedSpan<SourceFile*>> Converters::FromLSPRangeForSourceFile(
    SourceFile* file, lsproto::Range textRange, spanmap::Feature feature) {
	TSC_UNREACHABLE("Converters::FromLSPRangeForSourceFile — lsconv slice");
}
std::vector<MappedSpan<SourceFile*>>
Converters::FromLSPRangeIntersectingForSourceFile(
    SourceFile* file, lsproto::Range textRange, spanmap::Feature feature) {
	TSC_UNREACHABLE(
	    "Converters::FromLSPRangeIntersectingForSourceFile — lsconv slice");
}
TextRange Converters::FromLSPRangeToOriginal(SourceFile* script,
                                             lsproto::Range textRange) {
	TSC_UNREACHABLE("Converters::FromLSPRangeToOriginal — lsconv slice");
}

// Templated member dep-stubs.
template <Script T>
std::pair<lsproto::Range, spanmap::Fidelity> Converters::ToLSPRange(
    T script, TextRange textRange) {
	TSC_UNREACHABLE("Converters::ToLSPRange — lsconv slice");
}
template <Script T>
std::pair<lsproto::Range, spanmap::Fidelity> Converters::ToLSPRangeForFeature(
    T script, TextRange textRange, spanmap::Feature feature) {
	TSC_UNREACHABLE("Converters::ToLSPRangeForFeature — lsconv slice");
}
template <Script T>
std::pair<lsproto::Position, spanmap::Fidelity> Converters::ToLSPPosition(
    T script, TextPos position) {
	TSC_UNREACHABLE("Converters::ToLSPPosition — lsconv slice");
}
template <Script T>
std::pair<lsproto::Position, spanmap::Fidelity>
Converters::ToLSPPositionForFeature(T script, TextPos position,
                                    spanmap::Feature feature) {
	TSC_UNREACHABLE("Converters::ToLSPPositionForFeature — lsconv slice");
}
template <Script T>
std::pair<lsproto::Location, spanmap::Fidelity> Converters::ToLSPLocation(
    T script, TextRange rng) {
	TSC_UNREACHABLE("Converters::ToLSPLocation — lsconv slice");
}
template <Script T>
std::pair<lsproto::Location, spanmap::Fidelity>
Converters::ToLSPLocationForFeature(T script, TextRange rng,
                                    spanmap::Feature feature) {
	TSC_UNREACHABLE("Converters::ToLSPLocationForFeature — lsconv slice");
}

// Explicit instantiations for the templated members (SourceFile* is the only
// script type used by this slice).
template std::pair<lsproto::Range, spanmap::Fidelity>
Converters::ToLSPRange<SourceFile*>(SourceFile*, TextRange);
template std::pair<lsproto::Range, spanmap::Fidelity>
Converters::ToLSPRangeForFeature<SourceFile*>(SourceFile*, TextRange,
                                              spanmap::Feature);
template std::pair<lsproto::Position, spanmap::Fidelity>
Converters::ToLSPPosition<SourceFile*>(SourceFile*, TextPos);
template std::pair<lsproto::Position, spanmap::Fidelity>
Converters::ToLSPPositionForFeature<SourceFile*>(SourceFile*, TextPos,
                                                 spanmap::Feature);
template std::pair<lsproto::Location, spanmap::Fidelity>
Converters::ToLSPLocation<SourceFile*>(SourceFile*, TextRange);
template std::pair<lsproto::Location, spanmap::Fidelity>
Converters::ToLSPLocationForFeature<SourceFile*>(SourceFile*, TextRange,
                                                 spanmap::Feature);

} // namespace tsc::lsconv



namespace tsc::ls {

// === dep-stubs for sibling ls-slice items declared in ls.h ===

argumentListInfo* getImmediatelyContainingArgumentInfo(
    Node* node, int position, SourceFile* sourceFile, checker::Checker* c) {
	TSC_UNREACHABLE(
	    "getImmediatelyContainingArgumentInfo — ls/signaturehelp slice");
}

missingMemberFixer* newMissingMemberFixer(
    change::Tracker* changeTracker, compiler::SimpleProgram* program,
    checker::Checker* typeChecker, const lsutil::UserPreferences& preferences,
    autoimport::ImportAdder* importAdder, locale::Locale locale) {
	TSC_UNREACHABLE(
	    "newMissingMemberFixer — ls/codeactions_missingmemberfixer slice");
}


} // namespace tsc::ls

