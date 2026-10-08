// Port of tsc/internal/ast/diagnostic_test.go (package ast — in-package test;
// Go creates bare SourceFile values and calls package constructors directly).
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/diagnostics_util.h"
#include "internal/core/text.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
using namespace tsc;

// Go: NewCompilerDiagnostic(message, args...) =
// NewDiagnostic(nil, core.UndefinedTextRange(), message, args...).
static Diagnostic* NewCompilerDiagnostic(
	const DiagnosticMessage* message, const std::vector<std::string>& args) {
	return newDiagnostic(nullptr, TextRange::undefined(), message, args);
}

// Go: NewDiagnostic(file, loc, message, args...).
static Diagnostic* NewDiagnostic(SourceFile* file, TextRange loc,
								 const DiagnosticMessage* message,
								 const std::vector<std::string>& args) {
	return newDiagnostic(file, loc, message, args);
}

// Go: NewExternalDiagnostic(file, loc, source, category, code, messageText).
static Diagnostic* NewExternalDiagnostic(SourceFile* file, TextRange loc,
										 std::string_view source,
										 DiagnosticCategory category,
										 int32_t code,
										 std::string_view messageText) {
	return newExternalDiagnostic(file, loc, source, category, code,
								 messageText);
}

static SourceFile* newSourceFileWithPath(std::string_view path) {
	auto* f = new SourceFile();
	f->parseOptions.FileName = std::string(path);
	f->parseOptions.Path = std::string(path);
	return f;
}

static void TestDiagnosticsCollectionDeduplicatesExactDiagnosticsOnAdd(T* t) {
	t->Parallel();

	DiagnosticsCollection collection;
	Diagnostic* first =
		NewCompilerDiagnostic(Cannot_find_name_0, {"x"})
			->AddRelatedInfo(NewCompilerDiagnostic(
				X_0_is_declared_here, {"first"}));
	Diagnostic* second =
		NewCompilerDiagnostic(Cannot_find_name_0, {"x"})
			->AddRelatedInfo(NewCompilerDiagnostic(
				X_0_is_declared_here, {"first"}));
	Diagnostic* different =
		NewCompilerDiagnostic(Cannot_find_name_0, {"x"})
			->AddRelatedInfo(NewCompilerDiagnostic(
				X_0_is_declared_here, {"second"}));

	Diagnostic* gotFirst = collection.Add(first);
	if (gotFirst != first) {
		t->Fatalf("first Add() returned %p, want %p", {tsc::gostd::fmtArg::ptr(gotFirst),
				  tsc::gostd::fmtArg::ptr(first)});
	}
	Diagnostic* canonical = collection.Add(second);
	if (canonical != first) {
		t->Fatalf("second Add() returned %p, want canonical %p", {tsc::gostd::fmtArg::ptr(canonical), tsc::gostd::fmtArg::ptr(first)});
	}
	Diagnostic* gotDifferent = collection.Add(different);
	if (gotDifferent != different) {
		t->Fatalf("different Add() returned %p, want %p", {tsc::gostd::fmtArg::ptr(gotDifferent),
				  tsc::gostd::fmtArg::ptr(different)});
	}

	canonical->AddRelatedInfo(
		NewCompilerDiagnostic(X_0_is_declared_here, {"third"}));
	auto collected = collection.GetGlobalDiagnostics();
	if (collected.size() != 2) {
		t->Fatalf("GetGlobalDiagnostics() returned %d diagnostics, want 2", {(int)collected.size()});
	}
	if (int got = (int)first->RelatedInformation().size(); got != 2) {
		t->Fatalf("canonical diagnostic has %d related diagnostics, want 2", {got});
	}
}

static void TestDiagnosticsCollectionPreservesDistinctAdHocMessages(T* t) {
	t->Parallel();

	DiagnosticsCollection collection;
	Diagnostic* first =
		NewCompilerDiagnostic(NewAdHocMessage("first"), {});
	Diagnostic* second =
		NewCompilerDiagnostic(NewAdHocMessage("second"), {});

	collection.Add(first);
	collection.Add(second);
	auto collected = collection.GetGlobalDiagnostics();
	if (collected.size() != 2) {
		t->Fatalf("GetGlobalDiagnostics() returned %d diagnostics, want 2", {(int)collected.size()});
	}
}

static void TestDiagnosticsCollectionGetsDiagnosticsForEquivalentSourceFile(
	T* t) {
	t->Parallel();

	tspath::Path path = tspath::Path("/src/file.ts");
	SourceFile* diagnosticFile = newSourceFileWithPath(path);
	SourceFile* requestedFile = newSourceFileWithPath(path);
	Diagnostic* diagnostic = NewDiagnostic(
		diagnosticFile, TextRange{}, Cannot_find_name_0, {"x"});

	DiagnosticsCollection collection;
	collection.Add(diagnostic);

	auto collected = collection.GetDiagnosticsForFile(requestedFile);
	if (collected.size() != 1 || collected[0] != diagnostic) {
		t->Fatalf(
			"GetDiagnosticsForFile() returned %d diagnostics, want diagnostic "
			"for equivalent source file",
			{(int)collected.size()});
	}
}

static void TestExternalDiagnosticIdentity(T* t) {
	t->Parallel();
	SourceFile* file = newSourceFileWithPath("/src/file.vue");
	TextRange loc{1, 2};
	Diagnostic* first = NewExternalDiagnostic(file, loc, "mapper-a",
											  DiagnosticCategory::Error, 0,
											  "first");
	std::vector<Diagnostic*> diagnosticsList = {
		first,
		NewExternalDiagnostic(file, loc, "mapper-a", DiagnosticCategory::Error,
							  0, "second"),
		NewExternalDiagnostic(file, loc, "mapper-b", DiagnosticCategory::Error,
							  0, "first"),
		NewExternalDiagnostic(file, loc, "mapper-a",
							  DiagnosticCategory::Warning, 0, "first"),
	};

	DiagnosticsCollection collection;
	for (Diagnostic* diagnostic : diagnosticsList) {
		gotest::assert::Assert(
			t, !EqualDiagnosticsNoRelatedInfo(first, diagnostic) ||
				diagnostic == first);
		gotest::assert::Assert(
			t, CompareDiagnostics(first, diagnostic) != 0 || diagnostic == first);
		collection.Add(diagnostic);
	}
	gotest::assert::Equal(t, (int)collection.GetDiagnostics().size(),
						  (int)diagnosticsList.size());
}

REGISTER_UNIT_TEST(
	"ast.TestDiagnosticsCollectionDeduplicatesExactDiagnosticsOnAdd",
	TestDiagnosticsCollectionDeduplicatesExactDiagnosticsOnAdd);
REGISTER_UNIT_TEST("ast.TestDiagnosticsCollectionPreservesDistinctAdHocMessages",
				   TestDiagnosticsCollectionPreservesDistinctAdHocMessages);
REGISTER_UNIT_TEST(
	"ast.TestDiagnosticsCollectionGetsDiagnosticsForEquivalentSourceFile",
	TestDiagnosticsCollectionGetsDiagnosticsForEquivalentSourceFile);
REGISTER_UNIT_TEST("ast.TestExternalDiagnosticIdentity",
				   TestExternalDiagnosticIdentity);
