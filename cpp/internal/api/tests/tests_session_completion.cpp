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

// TestCompletionWithSymbolsAndExistingImportDoesNotDeadlock —
// session_completion_test.go:190.
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
    "api.TestCompletionWithSymbolsAndExistingImportDoesNotDeadlock",
    TestCompletionWithSymbolsAndExistingImportDoesNotDeadlock);

} // namespace tsc::api
