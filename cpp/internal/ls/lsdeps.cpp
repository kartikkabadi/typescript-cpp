// === slice: ls-coreA (dep-stub bodies) ===
// Dep-stubs for sibling packages (lsutil, lsconv, change, autoimport) that
// have not landed on this branch. Every body is TSC_UNREACHABLE; the owner
// slices provide real ports.

#include "internal/ls/lsdeps.h"

// ls.h is included (after lsdeps.h resolves its own decls) so the tsc::ls
// dep-stub decls near the end of ls.h get bodies here.
#include "internal/ls/ls.h"

namespace tsc::lsutil {

Node* GetLastChild(Node* node, SourceFile* sourceFile) {
	TSC_UNREACHABLE("lsutil::GetLastChild — lsutil slice");
}
Node* GetLastToken(Node* node, SourceFile* sourceFile) {
	TSC_UNREACHABLE("lsutil::GetLastToken — lsutil slice");
}
Node* GetFirstToken(Node* node, SourceFile* sourceFile) {
	TSC_UNREACHABLE("lsutil::GetFirstToken — lsutil slice");
}
bool PositionIsASICandidate(int pos, Node* context, SourceFile* file) {
	TSC_UNREACHABLE("lsutil::PositionIsASICandidate — lsutil slice");
}
bool ProbablyUsesSemicolons(SourceFile* file) {
	TSC_UNREACHABLE("lsutil::ProbablyUsesSemicolons — lsutil slice");
}
QuotePreference GetQuotePreference(SourceFile* sourceFile,
                                   const UserPreferences& preferences) {
	TSC_UNREACHABLE("lsutil::GetQuotePreference — lsutil slice");
}
bool IsNonContextualKeyword(Kind token) {
	TSC_UNREACHABLE("lsutil::IsNonContextualKeyword — lsutil slice");
}
ScriptElementKind GetSymbolKind(checker::Checker* typeChecker, Symbol* symbol,
                                Node* location) {
	TSC_UNREACHABLE("lsutil::GetSymbolKind — lsutil slice");
}
ScriptElementKindModifier GetSymbolModifiers(checker::Checker* typeChecker,
                                             Symbol* symbol) {
	TSC_UNREACHABLE("lsutil::GetSymbolModifiers — lsutil slice");
}

} // namespace tsc::lsutil

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

Converters* NewConverters(
    lsproto::PositionEncodingKind positionEncoding,
    std::function<LSPLineMap*(const std::string&)> getLineMap) {
	return new Converters(std::move(positionEncoding), std::move(getLineMap));
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

namespace tsc::autoimport {

bool Registry::IsPreparedForImportingFile(
    const std::string& fileName, ProjectID* projectID,
    const lsutil::UserPreferences& preferences) {
	TSC_UNREACHABLE("Registry::IsPreparedForImportingFile — autoimport slice");
}

std::vector<FixAndExport*> View::GetCompletions(const std::string& prefix,
                                              lsproto::Position position,
                                              bool forJSX,
                                              bool isTypeOnlyLocation) {
	TSC_UNREACHABLE("View::GetCompletions — autoimport slice");
}

View* NewView(Registry* registry, SourceFile* importingFile,
              ProjectID* projectID, compiler::SimpleProgram* program,
              checker::Checker* typeChecker,
              const modulespecifiers::UserPreferences& preferences) {
	TSC_UNREACHABLE("autoimport::NewView — autoimport slice");
}

ImportAdder* NewImportAdder(const ContextPtr& ctx,
                            compiler::SimpleProgram* program,
                            checker::Checker* checker, SourceFile* file,
                            View* view, lsutil::FormatCodeSettings formatOptions,
                            lsconv::Converters* converters,
                            const lsutil::UserPreferences& preferences) {
	TSC_UNREACHABLE("autoimport::NewImportAdder — autoimport slice");
}

Node* TypeToAutoImportableTypeNode(checker::Checker* c,
                                   ImportAdder* importAdder, checker::Type* t,
                                   Node* contextNode) {
	TSC_UNREACHABLE(
	    "autoimport::TypeToAutoImportableTypeNode — autoimport slice");
}

lsproto::ImportKind GetImportKindForImportStatement(
    SourceFile* importingFile, Export* export_,
    compiler::SimpleProgram* program) {
	TSC_UNREACHABLE(
	    "autoimport::GetImportKindForImportStatement — autoimport slice");
}

std::tuple<std::vector<lsproto::TextEdit*>, std::string, bool> Fix::Edits(
    const ContextPtr& ctx, SourceFile* file,
    const CompilerOptions* compilerOptions,
    lsutil::FormatCodeSettings formatOptions, lsconv::Converters* converters,
    const lsutil::UserPreferences& preferences) {
	TSC_UNREACHABLE("Fix::Edits — autoimport slice");
}

} // namespace tsc::autoimport

namespace tsc::change {

Tracker* NewTracker(const ContextPtr& ctx,
                    const CompilerOptions* compilerOptions,
                    lsutil::FormatCodeSettings formatOptions,
                    lsconv::Converters* converters) {
	TSC_UNREACHABLE("change::NewTracker — ls/change slice");
}

lsutil::FormatCodeSettings GetFormatCodeSettingsForWriting(
    lsutil::FormatCodeSettings options, SourceFile* sourceFile) {
	TSC_UNREACHABLE(
	    "change::GetFormatCodeSettingsForWriting — ls/change slice");
}

} // namespace tsc::change

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

std::tuple<std::string, std::string, std::string,
           std::vector<lsproto::VSClassifiedTextRun*>>
LanguageService::getQuickInfoAndDocumentationForSymbol(
    checker::Checker* c, Symbol* symbol, Node* node,
    lsproto::MarkupKind contentFormat, checker::VerbosityContext* vc,
    bool vsCapability) {
	TSC_UNREACHABLE(
	    "getQuickInfoAndDocumentationForSymbol — ls/hover slice");
}

} // namespace tsc::ls

namespace tsc::printer {

ChangeTrackerWriter::ChangeTrackerWriter(const std::string& newLine,
                                         int indentSize)
    : inner_(NewTextWriter(newLine, indentSize)) {
	// changetrackerwriter.go:29 — `ctw.textWriter.Clear()`.
	inner_->Clear();
}

PrintHandlers ChangeTrackerWriter::GetPrintHandlers() {
	TSC_UNREACHABLE("ChangeTrackerWriter::GetPrintHandlers — "
	                "changetrackerwriter slice");
}

Node* ChangeTrackerWriter::AssignPositionsToNode(
    Node* node, tsc::NodeFactory* factory) {
	TSC_UNREACHABLE("ChangeTrackerWriter::AssignPositionsToNode — "
	                "changetrackerwriter slice");
}

void ChangeTrackerWriter::Write(const std::string& s) { inner_->Write(s); }
void ChangeTrackerWriter::WriteTrailingSemicolon(const std::string& text) {
	inner_->WriteTrailingSemicolon(text);
}
void ChangeTrackerWriter::WriteComment(const std::string& text) {
	inner_->WriteComment(text);
}
void ChangeTrackerWriter::WriteKeyword(const std::string& text) {
	inner_->WriteKeyword(text);
}
void ChangeTrackerWriter::WriteOperator(const std::string& text) {
	inner_->WriteOperator(text);
}
void ChangeTrackerWriter::WritePunctuation(const std::string& text) {
	inner_->WritePunctuation(text);
}
void ChangeTrackerWriter::WriteSpace(const std::string& text) {
	inner_->WriteSpace(text);
}
void ChangeTrackerWriter::WriteStringLiteral(const std::string& text) {
	inner_->WriteStringLiteral(text);
}
void ChangeTrackerWriter::WriteParameter(const std::string& text) {
	inner_->WriteParameter(text);
}
void ChangeTrackerWriter::WriteProperty(const std::string& text) {
	inner_->WriteProperty(text);
}
void ChangeTrackerWriter::WriteSymbol(const std::string& text,
                                      Symbol* symbol) {
	inner_->WriteSymbol(text, symbol);
}
void ChangeTrackerWriter::WriteLine() { inner_->WriteLine(); }
void ChangeTrackerWriter::WriteLineForce(bool force) {
	inner_->WriteLineForce(force);
}
void ChangeTrackerWriter::IncreaseIndent() { inner_->IncreaseIndent(); }
void ChangeTrackerWriter::DecreaseIndent() { inner_->DecreaseIndent(); }
void ChangeTrackerWriter::Clear() { inner_->Clear(); }
std::string ChangeTrackerWriter::String() { return inner_->String(); }
void ChangeTrackerWriter::RawWrite(const std::string& s) {
	inner_->RawWrite(s);
}
void ChangeTrackerWriter::WriteLiteral(const std::string& s) {
	inner_->WriteLiteral(s);
}
int ChangeTrackerWriter::GetTextPos() { return inner_->GetTextPos(); }
int ChangeTrackerWriter::GetLine() { return inner_->GetLine(); }
TextPos ChangeTrackerWriter::GetColumn() { return inner_->GetColumn(); }
int ChangeTrackerWriter::GetIndent() { return inner_->GetIndent(); }
bool ChangeTrackerWriter::IsAtStartOfLine() {
	return inner_->IsAtStartOfLine();
}
bool ChangeTrackerWriter::HasTrailingComment() {
	return inner_->HasTrailingComment();
}
bool ChangeTrackerWriter::HasTrailingWhitespace() {
	return inner_->HasTrailingWhitespace();
}

ChangeTrackerWriter* NewChangeTrackerWriter(const std::string& newLine,
                                            int indentSize) {
	return new ChangeTrackerWriter(newLine, indentSize);
}

} // namespace tsc::printer
