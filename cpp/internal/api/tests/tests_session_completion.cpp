// Port of tsc/internal/api/session_completion_test.go (package api).
#include <chrono>
#include <future>
#include <memory>
#include <string>

#include "internal/api/proto.h"
#include "internal/api/session.h"
#include "internal/bundled/bundled.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::api {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace projecttestutil = tsc::testutil::projecttestutil;

struct projectSessionCloser {
	project::Session* s = nullptr;
	~projectSessionCloser() {
		if (s) s->Close();
	}
};
struct apiSessionCloser {
	std::shared_ptr<Session> s;
	~apiSessionCloser() {
		if (s) s->Close();
	}
};

} // namespace

// TestCompletionSymbolTypeIsResolvable — session_completion_test.go:26.
void TestCompletionSymbolTypeIsResolvable(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	const std::string fileName = "/home/projects/p/src/index.ts";
	// The caret sits right after `people.`, requesting members of `string[]`.
	const std::string content = "declare const people: string[];\npeople.";

	projecttestutil::FileMap files = {
	    {"/home/projects/p/tsconfig.json",
	     "{ \"compilerOptions\": { \"strict\": true } }"},
	    {fileName, content},
	};
	auto [projectSession, utils] = projecttestutil::Setup(files);
	projectSessionCloser ps{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};

	auto [snapshotResp, err] = session->handleCreateSnapshot(
	    t->Context(), new CreateSnapshotParams{
	                      {.OpenFiles = std::vector<DocumentIdentifier>{DocumentIdentifier{
	                           .FileName = fileName}}}});
	assert::NilError(t, err);

	auto [proj, err2] = session->handleGetDefaultProjectForFile(
	    t->Context(), new GetDefaultProjectForFileParams{
	                      .Snapshot = snapshotResp->Snapshot,
	                      .File = DocumentIdentifier{.FileName = fileName}});
	assert::NilError(t, err2);
	assert::Assert(t, proj != nullptr,
	               "file should resolve to a default project");

	// content is pure ASCII, so the UTF-16 caret offset equals the byte length.
	auto [completions, err3] =
	    session->handleGetCompletionsAtPosition(
	        t->Context(), new GetCompletionsAtPositionParams{
	                          .Snapshot = snapshotResp->Snapshot,
	                          .Project = proj->Id,
	                          .File = DocumentIdentifier{.FileName = fileName},
	                          .Position = (uint32_t)content.size(),
	                          .IncludeSymbol = true});
	assert::NilError(t, err3);
	assert::Assert(t, completions != nullptr,
	               "expected a completion list for array members");

	// Resolving the type of every completion symbol must not panic, and known
	// members like `push` must produce a concrete type.
	bool sawSymbol = false, sawPush = false;
	for (auto& entry : completions->Entries) {
		if (entry->Symbol == nullptr) {
			continue;
		}
		sawSymbol = true;
		auto [typeResp, err4] = session->handleGetTypeOfSymbol(
		    t->Context(), new GetTypeOfSymbolParams{
		                      .Snapshot = snapshotResp->Snapshot,
		                      .Project = proj->Id,
		                      .Symbol = entry->Symbol->Reference});
		assert::NilError(t, err4);
		assert::Assert(t, typeResp != nullptr,
		               "type of completion symbol " + entry->Name +
		                   " should resolve");
		if (entry->Name == "push") {
			sawPush = true;
		}
	}
	assert::Assert(t, sawSymbol,
	               "completion entries should include resolvable symbols");
	assert::Assert(t, sawPush,
	               "array member completions should include `push`");
}

// TestCompletionOnInferredProject — session_completion_test.go:99.
void TestCompletionOnInferredProject(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	// No tsconfig.json anywhere, so this file belongs to an inferred project.
	const std::string fileName = "/home/projects/p/src/index.ts";
	const std::string content = "declare const people: string[];\npeople.";

	projecttestutil::FileMap files = {
	    {fileName, content},
	};
	auto [projectSession, utils] = projecttestutil::Setup(files);
	projectSessionCloser ps{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};

	auto [snapshotResp, err] = session->handleCreateSnapshot(
	    t->Context(), new CreateSnapshotParams{
	                      {.OpenFiles = std::vector<DocumentIdentifier>{DocumentIdentifier{
	                           .FileName = fileName}}}});
	assert::NilError(t, err);

	auto [proj, err2] = session->handleGetDefaultProjectForFile(
	    t->Context(), new GetDefaultProjectForFileParams{
	                      .Snapshot = snapshotResp->Snapshot,
	                      .File = DocumentIdentifier{.FileName = fileName}});
	assert::NilError(t, err2);
	assert::Assert(t, proj != nullptr,
	               "file should resolve to an inferred default project");

	// This request previously panicked in setupLanguageService.
	// content is pure ASCII, so the UTF-16 caret offset equals the byte length.
	auto [completions, err3] =
	    session->handleGetCompletionsAtPosition(
	        t->Context(), new GetCompletionsAtPositionParams{
	                          .Snapshot = snapshotResp->Snapshot,
	                          .Project = proj->Id,
	                          .File = DocumentIdentifier{.FileName = fileName},
	                          .Position = (uint32_t)content.size()});
	assert::NilError(t, err3);
	assert::Assert(t, completions != nullptr,
	               "expected a completion list for array members");
}

// TestCompletionRetriesWithAutoImports — session_completion_test.go:141.
void TestCompletionRetriesWithAutoImports(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	const std::string fileName = "/home/projects/p/src/index.ts";
	const std::string content = "someV";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {"/home/projects/p/tsconfig.json",
	     "{ \"compilerOptions\": { \"module\": \"esnext\", \"target\": \"esnext\" "
	     "} }"},
	    {"/home/projects/p/src/export.ts", "export const someValue = 1;"},
	    {fileName, content},
	});
	projectSessionCloser ps{projectSession};
	projectSession->Configure(lsutil::UserPreferences{
	    .IncludeCompletionsForModuleExports = Tristate::True,
	    .IncludeCompletionsForImportStatements = Tristate::True,
	});

	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};

	auto [snapshotResp, err] = session->handleCreateSnapshot(
	    t->Context(), new CreateSnapshotParams{
	                      {.OpenFiles = std::vector<DocumentIdentifier>{DocumentIdentifier{
	                           .FileName = fileName}}}});
	assert::NilError(t, err);
	auto [proj, err2] = session->handleGetDefaultProjectForFile(
	    t->Context(), new GetDefaultProjectForFileParams{
	                      .Snapshot = snapshotResp->Snapshot,
	                      .File = DocumentIdentifier{.FileName = fileName}});
	assert::NilError(t, err2);
	assert::Assert(t, proj != nullptr,
	               "file should resolve to a default project");

	auto [completions, err3] =
	    session->handleGetCompletionsAtPosition(
	        t->Context(), new GetCompletionsAtPositionParams{
	                          .Snapshot = snapshotResp->Snapshot,
	                          .Project = proj->Id,
	                          .File = DocumentIdentifier{.FileName = fileName},
	                          .Position = (uint32_t)content.size()});
	assert::NilError(t, err3);
	assert::Assert(t, completions != nullptr, "expected a completion list");
	for (auto& entry : completions->Entries) {
		if (entry->Name == "someValue") {
			return;
		}
	}
	t->Fatal({"expected auto-import completion for someValue"});
}

// TestCompletionWithSymbolsRequiresPreparedAutoImports —
// session_completion_test.go:192.
void TestCompletionWithSymbolsRequiresPreparedAutoImports(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	const std::string fileName = "/home/projects/p/src/index.ts";
	const std::string content = "someV";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {"/home/projects/p/tsconfig.json",
	     "{ \"compilerOptions\": { \"module\": \"esnext\", \"target\": "
	     "\"esnext\" } }"},
	    {"/home/projects/p/src/export.ts",
	     "export const someValue = 1;"},
	    {fileName, content},
	});
	projectSessionCloser ps{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};

	auto prefs = std::make_shared<lsutil::UserPreferences>(
	    lsutil::UserPreferences{
	        .IncludeCompletionsForModuleExports = Tristate::True});
	auto [snapshotResp, err] = session->handleCreateSnapshot(
	    t->Context(), new CreateSnapshotParams{
	                      {.OpenFiles = std::vector<DocumentIdentifier>{
	                           DocumentIdentifier{.FileName = fileName}}},
	                      .UserPreferences = prefs});
	assert::NilError(t, err);
	auto [proj, err2] = session->handleGetDefaultProjectForFile(
	    t->Context(), new GetDefaultProjectForFileParams{
	                      .Snapshot = snapshotResp->Snapshot,
	                      .File = DocumentIdentifier{.FileName = fileName}});
	assert::NilError(t, err2);

	auto [completions, err3] =
	    session->handleGetCompletionsAtPosition(
	        t->Context(), new GetCompletionsAtPositionParams{
	                          .Snapshot = snapshotResp->Snapshot,
	                          .Project = proj->Id,
	                          .File =
	                              DocumentIdentifier{.FileName = fileName},
	                          .Position = (uint32_t)content.size(),
	                          .IncludeSymbol = true});
	assert::ErrorContains(t, err3,
	                      "snapshot is not prepared for auto-imports");
}

// TestCompletionWithSymbolsUsesPreparedSnapshot —
// session_completion_test.go:230.
void TestCompletionWithSymbolsUsesPreparedSnapshot(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	const std::string fileName = "/home/projects/p/src/index.ts";
	const std::string content = "someV";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {"/home/projects/p/tsconfig.json",
	     "{ \"compilerOptions\": { \"module\": \"esnext\", \"target\": "
	     "\"esnext\" } }"},
	    {"/home/projects/p/src/export.ts",
	     "export const someValue = 1;"},
	    {fileName, content},
	});
	projectSessionCloser ps{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};

	auto prefs = std::make_shared<lsutil::UserPreferences>(
	    lsutil::UserPreferences{
	        .IncludeCompletionsForModuleExports = Tristate::True});
	auto [snapshotResp, err] = session->handleCreateSnapshot(
	    t->Context(),
	    new CreateSnapshotParams{
	        {.OpenFiles = std::vector<DocumentIdentifier>{DocumentIdentifier{
	             .FileName = fileName}}},
	        .UserPreferences = prefs,
	        .PrepareAutoImports = std::make_shared<DocumentIdentifier>(
	            DocumentIdentifier{.FileName = fileName})});
	assert::NilError(t, err);
	auto [proj, err2] = session->handleGetDefaultProjectForFile(
	    t->Context(), new GetDefaultProjectForFileParams{
	                      .Snapshot = snapshotResp->Snapshot,
	                      .File = DocumentIdentifier{.FileName = fileName}});
	assert::NilError(t, err2);

	auto [completions, err3] =
	    session->handleGetCompletionsAtPosition(
	        t->Context(), new GetCompletionsAtPositionParams{
	                          .Snapshot = snapshotResp->Snapshot,
	                          .Project = proj->Id,
	                          .File =
	                              DocumentIdentifier{.FileName = fileName},
	                          .Position = (uint32_t)content.size(),
	                          .IncludeSymbol = true});
	assert::NilError(t, err3);
	assert::Assert(t, completions != nullptr);
	for (auto& entry : completions->Entries) {
		if (entry->Name == "someValue") {
			return;
		}
	}
	t->Fatal({"expected auto-import completion for someValue"});
}

// TestCompletionUsesSnapshotPreferences —
// session_completion_test.go:267.
void TestCompletionUsesSnapshotPreferences(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	const std::string fileName = "/home/projects/p/src/index.ts";
	const std::string content = "someV";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {"/home/projects/p/tsconfig.json",
	     "{ \"compilerOptions\": { \"module\": \"esnext\", \"target\": "
	     "\"esnext\" } }"},
	    {"/home/projects/p/src/export.ts",
	     "export const someValue = 1;"},
	    {fileName, content},
	});
	projectSessionCloser ps{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};

	auto prefs = std::make_shared<lsutil::UserPreferences>(
	    lsutil::UserPreferences{
	        .IncludeCompletionsForModuleExports = Tristate::False});
	auto [snapshotResp, err] = session->handleCreateSnapshot(
	    t->Context(), new CreateSnapshotParams{
	                      {.OpenFiles = std::vector<DocumentIdentifier>{
	                           DocumentIdentifier{.FileName = fileName}}},
	                      .UserPreferences = prefs});
	assert::NilError(t, err);
	auto [proj, err2] = session->handleGetDefaultProjectForFile(
	    t->Context(), new GetDefaultProjectForFileParams{
	                      .Snapshot = snapshotResp->Snapshot,
	                      .File = DocumentIdentifier{.FileName = fileName}});
	assert::NilError(t, err2);

	auto [completions, err3] =
	    session->handleGetCompletionsAtPosition(
	        t->Context(), new GetCompletionsAtPositionParams{
	                          .Snapshot = snapshotResp->Snapshot,
	                          .Project = proj->Id,
	                          .File =
	                              DocumentIdentifier{.FileName = fileName},
	                          .Position = (uint32_t)content.size(),
	                          .IncludeSymbol = true});
	assert::NilError(t, err3);
	assert::Assert(t, completions != nullptr);
	for (auto& entry : completions->Entries) {
		assert::Assert(t, entry->Name != "someValue",
		               "snapshot preferences should disable auto-import "
		               "completions");
	}
}

// TestSnapshotCreatesProgramsAndPreparesAutoImportsInOneClone —
// session_completion_test.go:304.
void TestSnapshotCreatesProgramsAndPreparesAutoImportsInOneClone(T* t) {
	t->Parallel();
	for (bool update : {false, true}) {
		t->Run(update ? "update" : "create", [update](T* t) {
			t->Parallel();
			testutil::withRecoverAndFail(
			    t, "snapshot creation panicked", [t, update] {
				    const std::string fileName =
				        "/home/projects/p/index.ts";
				    auto [projectSession, utils] =
				        projecttestutil::Setup({
				            {"/home/projects/p/tsconfig.json", "{}"},
				            {fileName, "someV"},
				            {"/home/projects/p/export.ts",
				             "export const someValue = 1;"},
				            {"/home/projects/synthetic.ts",
				             "export const x = 1;"},
				        });
				    projectSessionCloser ps{projectSession};
				    auto session =
				        NewLSPSession(projectSession, nullptr);
				    apiSessionCloser as{session};
				    auto* params = new CreateSnapshotParams{
				        {.OpenProjects =
				             std::vector<DocumentIdentifier>{
				                 DocumentIdentifier{
				                     .FileName = "/home/projects/p/"
				                                 "tsconfig.json"}},
				         .CreatePrograms =
				             std::vector<
				                 std::shared_ptr<
				                     CreateSnapshotProgramParams>>{
				                 std::make_shared<
				                     CreateSnapshotProgramParams>(
				                     CreateSnapshotProgramParams{
				                         .RootFiles =
				                             std::vector<
				                                 DocumentIdentifier>{
				                                 DocumentIdentifier{
				                                     .FileName =
				                                         "/home/projects/"
				                                         "synthetic.ts"}},
				                         .CompilerOptions =
				                             tsc::CompilerOptions{
				                                 .NoLib =
				                                     Tristate::True}})}},
				        .PrepareAutoImports =
				            std::make_shared<DocumentIdentifier>(
				                DocumentIdentifier{
				                    .FileName = fileName})};
				    std::unique_ptr<CreateSnapshotResponse> response;
				    gostd::Error err;
				    SnapshotID expectedID = 1;
				    if (update) {
					    auto [base, e] =
					        session->handleCreateSnapshot(
					            t->Context(),
					            new CreateSnapshotParams{});
					    assert::NilError(t, e);
					    expectedID = base->Snapshot + 1;
					    auto [r, ue] =
					        session->handleUpdateSnapshot(
					            t->Context(),
					            new UpdateSnapshotParams{
					                .Snapshot = base->Snapshot,
					                .Changes = std::shared_ptr<
					                    CreateSnapshotParams>(
					                    params)});
					    response = std::move(r);
					    err = ue;
				    } else {
					    auto [r, ce] =
					        session->handleCreateSnapshot(
					            t->Context(), params);
					    response = std::move(r);
					    err = ce;
				    }
				    assert::NilError(t, err);
				    assert::Equal(t, response->Snapshot,
				                  expectedID);
				    assert::Equal(
				        t,
				        (int)response->Operation->CreatedPrograms
				            ->size(),
				        1);
				    auto* snapshot =
				        session->snapshots[response->Snapshot]
				            ->snapshot;
				    auto* proj = snapshot->GetDefaultProject(
				        "file:///home/projects/p/index.ts");
				    assert::Assert(
				        t,
				        ls::autoimport::IsPreparedForImportingFile(
				            snapshot->AutoImportRegistry(), fileName,
				            ls::autoimport::InternProjectID(
				                project::idString(proj->ID())),
				            snapshot->UserPreferences()));
			    });
		});
	}
}

// TestPreparedIndependentSnapshotPreservesLSPOverlays —
// session_completion_test.go:349.
void TestPreparedIndependentSnapshotPreservesLSPOverlays(T* t) {
	t->Parallel();
	const std::string fileName = "/home/projects/p/index.ts";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {"/home/projects/p/tsconfig.json", "{}"},
	    {fileName, "diskOnly"},
	    {"/home/projects/p/package.json",
	     "{\"dependencies\":{\"my-pkg\":\"1.0.0\"}}"},
	    {"/home/projects/p/node_modules/my-pkg/package.json",
	     "{\"name\":\"my-pkg\",\"version\":\"1.0.0\",\"types\":\""
	     "index.d.ts\"}"},
	    {"/home/projects/p/node_modules/my-pkg/index.d.ts",
	     "export declare const packageValue: number;"},
	});
	projectSessionCloser ps{projectSession};
	projectSession->DidOpenFile(t->Context(),
	                          "file:///home/projects/p/index.ts", 1,
	                          "packageV",
	                          lsproto::LanguageKindTypeScript);
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};
	auto [base, err] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        t->Context(), new GetCurrentLanguageServerSnapshotParams{});
	assert::NilError(t, err);
	auto [prepared, err2] = session->handleUpdateSnapshot(
	    t->Context(),
	    new UpdateSnapshotParams{
	        .Snapshot = base->Snapshot,
	        .Changes = std::make_shared<CreateSnapshotParams>(
	            CreateSnapshotParams{
	                {},
	                .PrepareAutoImports =
	                    std::make_shared<DocumentIdentifier>(
	                        DocumentIdentifier{.FileName = fileName})})});
	assert::NilError(t, err2);
	auto* snapshot = session->snapshots[prepared->Snapshot]->snapshot;
	auto [content, ok] = snapshot->ReadFile(fileName);
	assert::Assert(t, ok);
	assert::Equal(t, content, std::string("packageV"));
	auto* proj = snapshot->GetDefaultProject(
	    "file:///home/projects/p/index.ts");
	assert::Equal(t,
	              proj->GetProgram()->GetSourceFile(fileName)->Text(),
	              std::string("packageV"));
	auto [completions, err3] =
	    session->handleGetCompletionsAtPosition(
	        t->Context(), new GetCompletionsAtPositionParams{
	                          .Snapshot = prepared->Snapshot,
	                          .Project = proj->ID(),
	                          .File =
	                              DocumentIdentifier{.FileName = fileName},
	                          .Position = 8,
	                          .IncludeSymbol = true});
	assert::NilError(t, err3);
	for (auto& entry : completions->Entries) {
		if (entry->Name == "packageValue") {
			return;
		}
	}
	t->Fatal({"expected dependency auto-import completion from an "
	          "LSP-derived snapshot"});
}

// TestFreshSnapshotIncludesDependencyAutoImports —
// session_completion_test.go:387.
void TestFreshSnapshotIncludesDependencyAutoImports(T* t) {
	t->Parallel();
	for (bool prepare : {false, true}) {
		t->Run(prepare ? "prepared" : "retry", [prepare](T* t) {
			t->Parallel();
			const std::string fileName =
			    "/home/projects/MixedCase/index.ts";
			auto [projectSession, utils] = projecttestutil::Setup({
			    {"/home/projects/MixedCase/tsconfig.json", "{}"},
			    {fileName, "packageV"},
			    {"/home/projects/MixedCase/package.json",
			     "{\"dependencies\":{\"my-pkg\":\"1.0.0\"}}"},
			    {"/home/projects/MixedCase/node_modules/my-pkg/"
			     "package.json",
			     "{\"name\":\"my-pkg\",\"version\":\"1.0.0\",\""
			     "types\":\"index.d.ts\"}"},
			    {"/home/projects/MixedCase/node_modules/my-pkg/"
			     "index.d.ts",
			     "export declare const packageValue: number;"},
			});
			projectSessionCloser ps{projectSession};
			projectSession->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/MixedCase/index.ts", 1,
			    "editorOnly", lsproto::LanguageKindTypeScript);
			auto session = NewLSPSession(projectSession, nullptr);
			apiSessionCloser as{session};
			auto params = std::make_unique<CreateSnapshotParams>();
			params->OpenProjects = std::vector<DocumentIdentifier>{
			    DocumentIdentifier{
			        .FileName =
			            "/home/projects/MixedCase/tsconfig.json"}};
			if (prepare) {
				params->PrepareAutoImports =
				    std::make_shared<DocumentIdentifier>(
				        DocumentIdentifier{.FileName = fileName});
			}
			auto [response, err] =
			    session->handleCreateSnapshot(
			        t->Context(), params.release());
			assert::NilError(t, err);
			auto* snapshot =
			    session->snapshots[response->Snapshot]->snapshot;
			auto [content, ok] = snapshot->ReadFile(fileName);
			assert::Assert(t, ok);
			assert::Equal(t, content, std::string("packageV"));
			auto* proj = snapshot->GetDefaultProject(
			    "file:///home/projects/MixedCase/index.ts");
			auto [completions, err2] =
			    session->handleGetCompletionsAtPosition(
			        t->Context(),
			        new GetCompletionsAtPositionParams{
			            .Snapshot = response->Snapshot,
			            .Project = proj->ID(),
			            .File =
			                DocumentIdentifier{.FileName = fileName},
			            .Position = 8,
			            .IncludeSymbol = prepare});
			assert::NilError(t, err2);
			assert::Assert(t, completions != nullptr);
			for (auto& entry : completions->Entries) {
				if (entry->Name == "packageValue") {
					return;
				}
			}
			t->Fatal({"expected dependency auto-import completion from "
			          "a fresh API snapshot"});
		});
	}
}

// TestCompletionWithSymbolsAndExistingImportDoesNotDeadlock —
// session_completion_test.go:492.
void TestCompletionWithSymbolsAndExistingImportDoesNotDeadlock(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	const std::string fileName = "/home/projects/p/src/index.ts";
	const std::string content =
	    "import { otherValue } from \"./export\";\nsomeV";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {"/home/projects/p/tsconfig.json",
	     "{ \"compilerOptions\": { \"module\": \"esnext\", \"target\": \"esnext\" "
	     "} }"},
	    {"/home/projects/p/src/export.ts",
	     "export const otherValue = 0; export const someValue = 1;"},
	    {fileName, content},
	});
	projectSessionCloser ps{projectSession};
	projectSession->Configure(lsutil::UserPreferences{
	    .IncludeCompletionsForModuleExports = Tristate::True,
	    .IncludeCompletionsForImportStatements = Tristate::True,
	});

	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};

	auto [snapshotResp, err] = session->handleCreateSnapshot(
	    t->Context(),
	    new CreateSnapshotParams{
	        {.OpenFiles = std::vector<DocumentIdentifier>{DocumentIdentifier{
	             .FileName = fileName}}},
	        .PrepareAutoImports = std::make_shared<DocumentIdentifier>(
	            DocumentIdentifier{.FileName = fileName})});
	assert::NilError(t, err);
	auto [proj, err2] = session->handleGetDefaultProjectForFile(
	    t->Context(), new GetDefaultProjectForFileParams{
	                      .Snapshot = snapshotResp->Snapshot,
	                      .File = DocumentIdentifier{.FileName = fileName}});
	assert::NilError(t, err2);
	assert::Assert(t, proj != nullptr,
	               "file should resolve to a default project");

	// IncludeSymbol pins completion to the single persistent API checker. When
	// ranking the auto-import completion, the existing import makes the view
	// consult that checker. This used to try to acquire the same checker again
	// and deadlock.
	auto session_ = session;
	auto snapshotID = snapshotResp->Snapshot;
	auto projID = proj->Id;
	auto ctx = t->Context();
	auto fut = std::async(std::launch::async, [=] {
		return session_->handleGetCompletionsAtPosition(
		    ctx, new GetCompletionsAtPositionParams{
		             .Snapshot = snapshotID,
		             .Project = projID,
		             .File = DocumentIdentifier{.FileName = fileName},
		             .Position = (uint32_t)content.size(),
		             .IncludeSymbol = true});
	});

	if (fut.wait_for(std::chrono::seconds(10)) ==
	    std::future_status::timeout) {
		t->Fatal({"completion request deadlocked while examining an existing "
		          "import"});
	}
	auto [completions, err3] = fut.get();
	assert::NilError(t, err3);
	assert::Assert(t, completions != nullptr, "expected a completion list");
	for (auto& entry : completions->Entries) {
		if (entry->Name == "someValue") {
			return;
		}
	}
	t->Fatal({"expected auto-import completion for someValue"});
}

REGISTER_UNIT_TEST("api.TestCompletionSymbolTypeIsResolvable",
                   TestCompletionSymbolTypeIsResolvable);
REGISTER_UNIT_TEST("api.TestCompletionOnInferredProject",
                   TestCompletionOnInferredProject);
REGISTER_UNIT_TEST("api.TestCompletionRetriesWithAutoImports",
                   TestCompletionRetriesWithAutoImports);
REGISTER_UNIT_TEST(
    "api.TestCompletionWithSymbolsRequiresPreparedAutoImports",
    TestCompletionWithSymbolsRequiresPreparedAutoImports);
REGISTER_UNIT_TEST("api.TestCompletionWithSymbolsUsesPreparedSnapshot",
                   TestCompletionWithSymbolsUsesPreparedSnapshot);
REGISTER_UNIT_TEST("api.TestCompletionUsesSnapshotPreferences",
                   TestCompletionUsesSnapshotPreferences);
REGISTER_UNIT_TEST(
    "api.TestSnapshotCreatesProgramsAndPreparesAutoImportsInOneClone",
    TestSnapshotCreatesProgramsAndPreparesAutoImportsInOneClone);
REGISTER_UNIT_TEST(
    "api.TestPreparedIndependentSnapshotPreservesLSPOverlays",
    TestPreparedIndependentSnapshotPreservesLSPOverlays);
REGISTER_UNIT_TEST("api.TestFreshSnapshotIncludesDependencyAutoImports",
                   TestFreshSnapshotIncludesDependencyAutoImports);
REGISTER_UNIT_TEST(
    "api.TestCompletionWithSymbolsAndExistingImportDoesNotDeadlock",
    TestCompletionWithSymbolsAndExistingImportDoesNotDeadlock);

} // namespace tsc::api
