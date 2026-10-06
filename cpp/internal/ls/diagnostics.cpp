// diagnostics.go — pull diagnostics entry point and diagnostic conversion.
#include "internal/ls/ls.h"

#include "internal/ast/ast.h"
#include "internal/compiler/program.h"
#include "internal/diagnostics/messages_generated.h"

namespace tsc::ls {

// getAllDiagnostics — diagnostics.go:17. Collects all diagnostics for a file:
// syntactic, semantic, suggestion, and (when declarations are emitted)
// declaration diagnostics.
std::vector<Diagnostic*> getAllDiagnostics(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    SourceFile* file) {
	std::vector<Diagnostic*> diags;
	std::vector<SourceFile*> files{file};
	if (auto* supplemental = file->SupplementalSourceFiles()) {
		files.insert(files.end(), supplemental->begin(),
		             supplemental->end());
	}
	for (auto* sourceFile : files) {
		for (auto* d : program->GetSyntacticDiagnostics(sourceFile)) {
			diags.push_back(d);
		}
		for (auto* d : program->GetSemanticDiagnostics(sourceFile)) {
			diags.push_back(d);
		}
		for (auto* d : program->GetSuggestionDiagnostics(sourceFile)) {
			diags.push_back(d);
		}
		if (program->Options()->GetEmitDeclarations()) {
			for (auto* d : program->GetDeclarationDiagnostics(sourceFile)) {
				diags.push_back(d);
			}
		}
	}
	return diags;
}

// ProvideDiagnostics — diagnostics.go:30.
std::pair<lsproto::DocumentDiagnosticResponse, gostd::Error>
LanguageService::ProvideDiagnostics(const gostd::Context& ctx,
                                    lsproto::DocumentUri uri) {
	auto [program, file] = getProgramAndFile(uri);

	if (tristateIsFalse(UserPreferences().EnableValidation)) {
		auto diagnostics = std::vector<lsproto::Diagnostic*>{};
		return {
		    lsproto::DocumentDiagnosticResponse{
		        lsproto::
		            RelatedFullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport{
		                .FullDocumentDiagnosticReport =
		                    new lsproto::RelatedFullDocumentDiagnosticReport{
		                        .Items = diagnostics,
		                    },
		            }},
		    nullptr};
	}

	auto diagnostics = getAllDiagnostics(ctx, program, file);

	return {
	    lsproto::DocumentDiagnosticResponse{
	        lsproto::
	            RelatedFullDocumentDiagnosticReportOrUnchangedDocumentDiagnosticReport{
	                .FullDocumentDiagnosticReport =
	                    new lsproto::RelatedFullDocumentDiagnosticReport{
	                        .Items = toLSPDiagnostics(ctx, {diagnostics}),
	                    },
	            }},
	    nullptr};
}

// toLSPDiagnostics — diagnostics.go:49.
std::vector<lsproto::Diagnostic*> LanguageService::toLSPDiagnostics(
    const gostd::Context& ctx,
    std::vector<std::vector<Diagnostic*>> diagnostics) {
	bool reportStyleChecksAsWarnings =
	    tristateIsTrue(UserPreferences().ReportStyleChecksAsWarnings);
	size_t size = 0;
	for (auto& diagSlice : diagnostics) {
		size += diagSlice.size();
	}
	std::vector<lsproto::Diagnostic*> lspDiagnostics;
	lspDiagnostics.reserve(size);
	// Compiler diagnostics located entirely in a content-mapped file's
	// synthesized code have no location in the original file. Collect them per
	// file and surface them through a single aggregate at the top of the file
	// (with the real messages as related information) rather than dropping
	// them or scattering them at position 0.
	collections::OrderedMap<SourceFile*, std::vector<Diagnostic*>>
	    synthesizedByFile;
	for (auto& diagSlice : diagnostics) {
		for (auto* diag : diagSlice) {
			if (isSynthesizedContentMappedDiagnostic(diag)) {
				auto existing = synthesizedByFile.GetOrZero(diag->File());
				existing.push_back(diag);
				synthesizedByFile.Set(diag->File(), std::move(existing));
				continue;
			}
			lspDiagnostics.push_back(lsconv::DiagnosticToLSPPull(
			    ctx, converters, diag, reportStyleChecksAsWarnings));
		}
	}
	for (auto* file : synthesizedByFile.Keys()) {
		auto diags = synthesizedByFile.GetOrZero(file);
		auto* aggregate = aggregateSynthesizedDiagnostics(file, diags);
		lspDiagnostics.push_back(lsconv::DiagnosticToLSPPull(
		    ctx, converters, aggregate, reportStyleChecksAsWarnings));
	}
	return lspDiagnostics;
}

// isSynthesizedContentMappedDiagnostic — diagnostics.go:81. Reports whether
// diag is a compiler diagnostic on a content-mapped file whose location lies
// entirely in synthesized virtual code with no counterpart in the original
// file, and so has no meaningful position to report against the original file.
bool isSynthesizedContentMappedDiagnostic(Diagnostic* diag) {
	auto* file = diag->File();
	if (file == nullptr || file->SpanMap() == nullptr ||
	    diag->Source() != "") {
		return false;
	}
	auto [_, fidelity] =
	    spanmap::VirtualToOriginalSpan(file->SpanMap(), diag->Loc());
	return fidelity == spanmap::FidelityNone;
}

// aggregateSynthesizedDiagnostics — diagnostics.go:96. Builds a single
// diagnostic at the top of a content-mapped file standing in for compiler
// diagnostics located in synthesized code with no original location. The
// originals are attached as related information so their messages are
// surfaced rather than silently dropped. (A later change will point the
// related locations at a read-only view of the file's virtual TypeScript.)
Diagnostic* aggregateSynthesizedDiagnostics(
    SourceFile* file, std::vector<Diagnostic*> diags) {
	auto* aggregate = newDiagnostic(
	    file, TextRange{0, 0},
	    Virtual_code_produced_by_the_content_mapper_0_has_problems_with_no_corresponding_location_in_this_file,
	    {file->ContentMapper()});
	aggregate->SetRelatedInfo(diags);
	aggregate->SetCategory(worstCategory(diags));
	return aggregate;
}

// worstCategory — diagnostics.go:112.
DiagnosticCategory worstCategory(std::vector<Diagnostic*> diags) {
	auto worst = diags[0]->Category();
	for (auto* diag : diags) {
		switch (diag->Category()) {
		case DiagnosticCategory::Error:
			return DiagnosticCategory::Error;
		case DiagnosticCategory::Warning:
			worst = DiagnosticCategory::Warning;
			break;
		default:
			break;
		}
	}
	return worst;
}

} // namespace tsc::ls
