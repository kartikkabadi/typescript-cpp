// contentmapper_baseline.go — DoContentMapperBaseline: original +
// transformed text plus diagnostics for content-mapped files.
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/diagnosticwriter/diagnosticwriter.h"
#include "internal/gostd/gostd.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/tsbaseline/tsbaseline.h"
#include "internal/testutil/tsbaseline/tsbaselineutil.h"

namespace tsc::testutil::tsbaseline {

namespace {

// ansiEscape — contentmapper_baseline.go:19.
const gostd::regexp::Regexp& ansiEscape() {
	static const auto* r = new gostd::regexp::Regexp("\x1b\\[[0-9;]*m");
	return *r;
}

// contentMapperFormatOpts — contentmapper_baseline.go:21.
const diagnosticwriter::FormattingOptions* contentMapperFormatOpts() {
	static const auto* v = [] {
		auto* o = new diagnosticwriter::FormattingOptions();
		o->newLine = "\n";
		return o;
	}();
	return v;
}

// ScriptKind.String() — core/scriptkind_stringer_generated.go.
std::string scriptKindString(ScriptKind k) {
	switch (k) {
		case ScriptKind::Unknown: return "ScriptKindUnknown";
		case ScriptKind::JS: return "ScriptKindJS";
		case ScriptKind::JSX: return "ScriptKindJSX";
		case ScriptKind::TS: return "ScriptKindTS";
		case ScriptKind::TSX: return "ScriptKindTSX";
		case ScriptKind::JSON: return "ScriptKindJSON";
		default:
			return "ScriptKind(" + std::to_string((int)k) + ")";
	}
}

// ensureTrailingNewline — contentmapper_baseline.go:87.
std::string ensureTrailingNewline(std::string_view s) {
	if (s.empty() || s.back() == '\n') {
		return std::string(s);
	}
	return std::string(s) + "\n";
}

// getContentMapperBaseline — contentmapper_baseline.go:43.
std::string getContentMapperBaseline(
    compiler::ProgramLike* program,
    const std::vector<Diagnostic*>& diagnostics) {
	auto* prog = program->GetProgram();
	std::unordered_map<std::string, contentmapper::Mapper*> mapped;
	std::vector<SourceFile*> files;
	for (auto* file : program->GetSourceFiles()) {
		if (auto* mapper = prog->GetContentMapper(file); mapper != nullptr) {
			mapped[file->FileName()] = mapper;
			files.push_back(file);
		}
	}
	if (files.empty()) {
		return "";
	}

	std::string b;
	for (auto* file : files) {
		auto* mapper = mapped[file->FileName()];
		// %v on []string: [a b c]
		std::string extensions = "[";
		for (size_t i = 0; i < mapper->Definition.Extensions.size();
		     i++) {
			if (i) extensions += " ";
			extensions += mapper->Definition.Extensions[i];
		}
		extensions += "]";
		b += gostd::sprintf(
		    "//// [%s] (ScriptKind: %s, ContentMapper: %v)\n",
		    {removeTestPathPrefixes(file->FileName(), false),
		     scriptKindString(file->ScriptKind), extensions});
		b += "--- Original ---\n";
		b += ensureTrailingNewline(file->OriginalText());
		b += "--- Transformed ---\n";
		b += ensureTrailingNewline(file->text);
		b += "\n";
	}

	std::vector<Diagnostic*> fileDiagnostics;
	for (auto* d : diagnostics) {
		if (d->File() != nullptr &&
		    mapped.count(d->File()->FileName()) != 0) {
			fileDiagnostics.push_back(d);
		}
	}

	b += "=== Diagnostics ===\n\n";
	if (fileDiagnostics.empty()) {
		b += std::string(baseline::NoContent) + "\n";
		return b;
	}
	std::ostringstream rendered;
	auto wrapped = diagnosticwriter::wrapASTDiagnostics(fileDiagnostics);
	std::vector<diagnosticwriter::ASTDiagnostic*> ptrs;
	for (auto& d : wrapped) ptrs.push_back(d.get());
	auto all = diagnosticwriter::toDiagnostics<
	    diagnosticwriter::ASTDiagnostic>(
	    std::span<diagnosticwriter::ASTDiagnostic* const>(ptrs.data(),
	                                                      ptrs.size()));
	diagnosticwriter::formatDiagnosticsWithColorAndContext(
	    rendered, all, contentMapperFormatOpts());
	b += removeTestPathPrefixes(
	    ansiEscape().ReplaceAllString(rendered.str(), ""), false);
	return b;
}

}  // namespace

// DoContentMapperBaseline writes a baseline for content-mapped files that
// shows the original source, the transformed source the compiler actually
// checks, and the file's diagnostics. Diagnostics are rendered with the
// standard diagnostic writer, which maps each one to the text it belongs
// to: mapper-produced diagnostics render against the original source,
// compiler diagnostics on mappable code render against the original
// source, and compiler diagnostics on synthesized code render against the
// transformed source. If the program has no content-mapped files, no
// baseline is written.
// — contentmapper_baseline.go:26.
void DoContentMapperBaseline(
    gostd::testing::T* t, const std::string& baselinePath,
    compiler::ProgramLike* program,
    const std::vector<Diagnostic*>& diagnostics,
    const baseline::Options& opts) {
	auto content = getContentMapperBaseline(program, diagnostics);
	if (content.empty()) {
		return;
	}
	baseline::Run(t,
	              tsExtension().ReplaceAllString(baselinePath,
	                                             ".contentmapper"),
	              content, opts);
}

}  // namespace tsc::testutil::tsbaseline
