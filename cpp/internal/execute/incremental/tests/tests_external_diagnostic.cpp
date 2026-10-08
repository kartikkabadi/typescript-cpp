// tests_external_diagnostic.cpp — port of
// tsc/internal/execute/incremental/external_diagnostic_test.go.
#include <string>

#include "internal/ast/ast.h"
#include "internal/execute/incremental/incremental.h"
#include "internal/gostd/testing.h"
#include "internal/parser/parser.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc {
namespace {
using namespace ::tsc::execute::incremental;


using gostd::testing::T;

void TestExternalDiagnosticBuildInfoRoundTrip(T* t) {
	t->Parallel();
	auto* file = parseSourceFile(
	    SourceFileParseOptions{.FileName = "/app.vue",
	                           .Path = "/app.vue"},
	    "", ScriptKind::TS);

	auto* diagnostic =
	    newExternalDiagnostic(file, TextRange{1, 2}, "vue",
	                          DiagnosticCategory::Warning, 1001,
	                          "mapper warning");

	auto* serialized = astDiagToBuildInfoDiag(diagnostic);
	if (serialized == nullptr) {
		t->Fatal({std::string(
		    "expected buildInfoDiagnosticWithFileName, got nil")});
	}
	if (serialized->source != "vue") {
		t->Errorf("expected source %q, got %q", {"vue",
		                                       serialized->source});
	}
	if (serialized->messageText != "mapper warning") {
		t->Errorf("expected messageText %q, got %q",
		          {"mapper warning", serialized->messageText});
	}

	auto* restored = serialized->toDiagnostic(nullptr, file);
	if (restored->Source() != "vue") {
		t->Errorf("expected restored source %q, got %q",
		          {"vue", restored->Source()});
	}
	// Go asserts restored.Localize(locale.Default) == "mapper warning";
	// Diagnostic::Localize is not ported, but for an external diagnostic
	// (message == nil) Go's Localize returns messageText verbatim.
	if (restored->MessageText() != "mapper warning") {
		t->Errorf("expected restored message %q, got %q",
		          {"mapper warning", restored->MessageText()});
	}
}
REGISTER_UNIT_TEST("incremental.TestExternalDiagnosticBuildInfoRoundTrip",
                   TestExternalDiagnosticBuildInfoRoundTrip);

} // namespace
} // namespace tsc
