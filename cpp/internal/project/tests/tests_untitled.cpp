// Port of tsc/internal/project/untitled_test.go.
#include <memory>
#include <string>

#include "internal/bundled/bundled.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/ls.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/project/session.h"
#include "internal/project/snapshot.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace lsconv = tsc::lsconv;
namespace lsproto = tsc::lsp::lsproto;
namespace projecttestutil = tsc::testutil::projecttestutil;
using tsc::gostd::testing::T;

void TestUntitledReferences(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	// First test the URI conversion functions to understand the issue
	lsproto::DocumentUri untitledURI = "untitled:Untitled-2";
	std::string convertedFileName =
	    lsproto::documentUriFileName(untitledURI);
	t->Logf("URI '%s' converts to filename '%s'",
	        {untitledURI, convertedFileName});

	lsproto::DocumentUri backToURI =
	    lsconv::FileNameToDocumentURI(convertedFileName);
	t->Logf("Filename '%s' converts back to URI '%s'",
	        {convertedFileName, backToURI});

	if (backToURI != untitledURI) {
		t->Errorf("Round-trip conversion failed: '%s' -> '%s' -> '%s'",
		          {untitledURI, convertedFileName, backToURI});
	}

	// Create a test case that simulates how untitled files should work
	std::string testContent = "let x = 42;\n\nx\n\nx++;";

	// Use the converted filename that DocumentURIToFileName would produce
	std::string untitledFileName =
	    convertedFileName; // "^/untitled/ts-nul-authority/Untitled-2"
	t->Logf("Would use untitled filename: %s", {untitledFileName});

	// Set up the file system with an untitled file -
	// But use a regular file first to see the current behavior
	projecttestutil::FileMap files{
	    {"/Untitled-2.ts", testContent},
	};

	auto [session, sessionUtils] = projecttestutil::Setup(files);

	auto ctx = projecttestutil::WithRequestID(t->Context());
	session->DidOpenFile(ctx, "file:///Untitled-2.ts", 1, testContent,
	                   lsproto::LanguageKindTypeScript);

	// Get language service
	auto [languageService, err] =
	    session->GetLanguageService(ctx, "file:///Untitled-2.ts");
	assert::NilError(t, err);

	// Test the filename that the source file reports
	auto* program = languageService->GetProgram();
	auto* sourceFile = program->GetSourceFile("/Untitled-2.ts");
	t->Logf("SourceFile.FileName() returns: '%s'",
	        {sourceFile->FileName()});

	// Call ProvideReferences using the LSP method
	lsproto::DocumentUri uri = "file:///Untitled-2.ts";
	lsproto::Position lspPosition{
	    .Line = 2,
	    .Character = 0,
	}; // Line 3, character 1 (0-indexed)

	lsproto::ReferenceParams refParams{
	    .TextDocument = lsproto::TextDocumentIdentifier{.Uri = uri},
	    .Position = lspPosition,
	    .Context = std::make_shared<lsproto::ReferenceContext>(
	        lsproto::ReferenceContext{.IncludeDeclaration = true}),
	};

	auto [resp, err2] =
	    languageService->ProvideReferences(ctx, &refParams, nullptr);
	assert::NilError(t, err2);

	auto& refs = resp.Locations->value();

	// Log the results
	t->Logf("Input file URI: %s", {uri});
	t->Logf("Number of references found: %d", {(int64_t)refs.size()});
	for (size_t i = 0; i < refs.size(); i++) {
		auto& ref = refs[i];
		t->Logf(
		    "Reference %d: URI=%s, Range=%d:%d-%d:%d",
		    {(int64_t)(i + 1), ref.Uri,
		     (int64_t)ref.Range.Start.Line,
		     (int64_t)ref.Range.Start.Character,
		     (int64_t)ref.Range.End.Line,
		     (int64_t)ref.Range.End.Character});
	}

	// We expect to find 3 references
	assert::Assert(t, refs.size() == 3, "Expected 3 references");

	// Also test definition using ProvideDefinition
	auto definition =
	    languageService->ProvideDefinition(ctx, uri, lspPosition);
	if (definition.Locations != nullptr &&
	    definition.Locations->has_value()) {
		t->Logf("Definition found: %d locations",
		        {(int64_t)definition.Locations->value().size()});
		for (size_t i = 0;
		     i < definition.Locations->value().size(); i++) {
			auto& loc = definition.Locations->value()[i];
			t->Logf(
			    "Definition %d: URI=%s, Range=%d:%d-%d:%d",
			    {(int64_t)(i + 1), loc.Uri,
			     (int64_t)loc.Range.Start.Line,
			     (int64_t)loc.Range.Start.Character,
			     (int64_t)loc.Range.End.Line,
			     (int64_t)loc.Range.End.Character});
		}
	}
	delete languageService;
}

void TestUntitledFileInInferredProject(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	// Test that untitled files are properly handled in inferred projects
	std::string testContent = "let x = 42;\n\nx\n\nx++;";

	auto [session, sessionUtils] =
	    projecttestutil::Setup(projecttestutil::FileMap{});

	auto ctx = projecttestutil::WithRequestID(t->Context());

	// Open untitled files - these should create an inferred project
	session->DidOpenFile(ctx, "untitled:Untitled-1", 1, "x\n\n",
	                   lsproto::LanguageKindTypeScript);
	session->DidOpenFile(ctx, "untitled:Untitled-2", 1, testContent,
	                   lsproto::LanguageKindTypeScript);

	auto* snapshot = session->Snapshot();

	// Should have an inferred project
	assert::Assert(
	    t, snapshot->ProjectCollection->InferredProject() != nullptr);

	// Get language service for the untitled file
	auto [languageService, err] =
	    session->GetLanguageService(ctx, "untitled:Untitled-2");
	assert::NilError(t, err);

	auto* program = languageService->GetProgram();
	std::string untitledFileName =
	    lsproto::documentUriFileName("untitled:Untitled-2");
	auto* sourceFile = program->GetSourceFile(untitledFileName);
	assert::Assert(t, sourceFile != nullptr);
	assert::Equal(t, sourceFile->Text(), testContent);

	// Test references on 'x' at position 13 (line 3, after "let x = 42;\n\n")
	lsproto::DocumentUri uri = "untitled:Untitled-2";
	lsproto::Position lspPosition{
	    .Line = 2,
	    .Character = 0,
	}; // Line 3, character 1 (0-indexed)

	lsproto::ReferenceParams refParams{
	    .TextDocument = lsproto::TextDocumentIdentifier{.Uri = uri},
	    .Position = lspPosition,
	    .Context = std::make_shared<lsproto::ReferenceContext>(
	        lsproto::ReferenceContext{.IncludeDeclaration = true}),
	};

	auto [resp, err2] =
	    languageService->ProvideReferences(ctx, &refParams, nullptr);
	assert::NilError(t, err2);

	auto& refs = resp.Locations->value();
	t->Logf("Number of references found: %d", {(int64_t)refs.size()});
	for (size_t i = 0; i < refs.size(); i++) {
		auto& ref = refs[i];
		t->Logf(
		    "Reference %d: URI=%s, Range=%d:%d-%d:%d",
		    {(int64_t)(i + 1), ref.Uri,
		     (int64_t)ref.Range.Start.Line,
		     (int64_t)ref.Range.Start.Character,
		     (int64_t)ref.Range.End.Line,
		     (int64_t)ref.Range.End.Character});
		// All URIs should be untitled: URIs, not file: URIs
		assert::Assert(t, ref.Uri.starts_with("untitled:"),
		               "Expected untitled: URI, got " + ref.Uri);
	}

	// We expect to find 4 references
	assert::Assert(t, refs.size() == 4, "Expected 4 references");
	delete languageService;
}

void TestImportsInUntitled(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	projecttestutil::FileMap files{
	    // Make sure typings directory exists so it would actually try to
	    // fetch typings from this location
	    {std::string(projecttestutil::TestTypingsLocation) +
	         "/node_modules/@types/somelib/index.d.ts",
	     std::string("export const x: number;")},
	};
	auto [session, sessionUtils] = projecttestutil::Setup(files);
	std::string content =
	    "import \"https://deno.land/std@0.208.0/path/mod.ts\"\n"
	    "		import  \"./relative\"\n";
	lsproto::DocumentUri uri1 = "untitled:Untitled-1";
	session->DidOpenFile(t->Context(), uri1, 1, content,
	                   lsproto::LanguageKindTypeScript);

	// 2) Wait for ATA/background tasks to finish, then get a language
	// service for the first file
	session->WaitForBackgroundTasks();
	auto [ls, err] =
	    session->GetLanguageService(t->Context(), uri1);
	assert::NilError(t, err);
	delete ls;
}

REGISTER_UNIT_TEST("project.TestUntitledReferences",
                   TestUntitledReferences);
REGISTER_UNIT_TEST("project.TestUntitledFileInInferredProject",
                   TestUntitledFileInInferredProject);
REGISTER_UNIT_TEST("project.TestImportsInUntitled",
                   TestImportsInUntitled);

} // namespace
