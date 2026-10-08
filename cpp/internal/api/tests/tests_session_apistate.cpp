// Port of tsc/internal/api/session_apistate_test.go (package api).
#include <memory>
#include <string>
#include <vector>

#include "internal/api/proto.h"
#include "internal/api/session.h"
#include "internal/bundled/bundled.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/project/project.h"
#include "internal/project/session.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc::api {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace projecttestutil = tsc::testutil::projecttestutil;

project::ID configuredProjectID(const std::string& path) {
	return project::ConfiguredProjectID(tspath::Path(path)).AsID();
}

project::ID inferredProjectID() {
	return project::ID("/dev/null/inferred");
}

project::SyntheticProjectID syntheticProjectID(int id) {
	return project::NewSyntheticProjectID(id);
}

// sessionCloser — Go "defer projectSession.Close()".
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

void changeWatched(project::Session* session, const std::string& fileName) {
	lsproto::FileEvent ev;
	ev.Uri = DocumentIdentifier{.FileName = fileName}.ToURI(
	    session->GetCurrentDirectory());
	ev.Type = lsproto::FileChangeTypeChanged;
	session->DidChangeWatchedFiles(gostd::contextBackground(), {&ev});
}

} // namespace

// TestGetCurrentLanguageServerSnapshotAdoptsChanges —
// session_apistate_test.go:28.
void TestGetCurrentLanguageServerSnapshotAdoptsChanges(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	const std::string configFileName = "/home/projects/p/tsconfig.json";
	const std::string fileName = "/home/projects/p/src/index.ts";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {configFileName, "{ \"compilerOptions\": { \"strict\": true } }"},
	    {fileName, "export const x = 1;"},
	});
	projectSessionCloser ps{projectSession};

	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};
	auto [response, err] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .Changes =
	                std::make_shared<LanguageServerSnapshotChanges>(
	                    LanguageServerSnapshotChanges{
	                        {.OpenProjects = {DocumentIdentifier{
	                             .FileName = configFileName}}}})});
	assert::NilError(t, err);
	assert::Equal(t, (int)session->openProjects.Len(), 1);
	assert::Equal(t, response->Snapshot,
	              snapshotHandle(projectSession->Snapshot()));
	assert::Assert(t, projectSession->Snapshot()
	                      ->ProjectCollection
	                      ->ConfiguredProject(tspath::Path(configFileName)) !=
	                      nullptr);
	assert::Assert(
	    t, utils->FS()->WriteFile(fileName, "export const x = 2;").impl() ==
	           nullptr);
	changeWatched(projectSession, fileName);
	auto [dirty, err2] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .BaseSnapshot = response->Snapshot});
	assert::NilError(t, err2);
	assert::Equal(t, projectSession->Snapshot()
	                     ->ProjectCollection
	                     ->ConfiguredProject(tspath::Path(configFileName))
	                     ->IsDirty(),
	              true);

	auto [unchanged, err3] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .BaseSnapshot = dirty->Snapshot,
	            .Changes =
	                std::make_shared<LanguageServerSnapshotChanges>(
	                    LanguageServerSnapshotChanges{
	                        {.OpenProjects = {DocumentIdentifier{
	                             .FileName = configFileName}}}})});
	assert::NilError(t, err3);
	assert::Equal(t, unchanged->Projects[0]->Dirty, false);
	assert::Equal(t, (int)session->openProjects.Len(), 1);

	auto [removed, err4] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .BaseSnapshot = unchanged->Snapshot,
	            .Changes =
	                std::make_shared<LanguageServerSnapshotChanges>(
	                    LanguageServerSnapshotChanges{
	                        {.CloseProjects = {DocumentIdentifier{
	                             .FileName = configFileName}}}})});
	assert::NilError(t, err4);
	assert::Equal(t, (int)removed->Projects.size(), 0);
	assert::DeepEqual(t, removed->Changes->RemovedProjects,
	                  std::vector<project::ID>{
	                      configuredProjectID(configFileName)});
	assert::Equal(t, (int)session->openProjects.Len(), 0);

	session->Close();
	assert::Equal(t, (int)session->openProjects.Len(), 0);
	assert::Assert(t, projectSession->Snapshot()
	                      ->ProjectCollection
	                      ->ConfiguredProject(tspath::Path(configFileName)) ==
	                      nullptr);
}

// TestGetCurrentLanguageServerSnapshotRejectsStandaloneSession —
// session_apistate_test.go:87.
void TestGetCurrentLanguageServerSnapshotRejectsStandaloneSession(T* t) {
	t->Parallel();

	auto [init, utils] = projecttestutil::GetSessionInitOptions(
	    {}, nullptr, std::make_shared<projecttestutil::TypingsInstallerOptions>());
	auto session = NewStandaloneSession(init.get(), nullptr);
	apiSessionCloser as{session};

	auto [res, err] = session->handleGetCurrentLanguageServerSnapshot(
	    gostd::contextBackground(), new GetCurrentLanguageServerSnapshotParams{});
	assert::ErrorContains(t, err, "requires an LSP-connected API session");
}

// TestOpenProjectRejectsReservedProjectID — session_apistate_test.go:98.
void TestOpenProjectRejectsReservedProjectID(T* t) {
	t->Parallel();

	auto [init, utils] = projecttestutil::GetSessionInitOptions(
	    {}, nullptr, std::make_shared<projecttestutil::TypingsInstallerOptions>());
	auto session = NewStandaloneSession(init.get(), nullptr);
	apiSessionCloser as{session};

	auto [res, err] = session->toAPISnapshotRequest(
	    gostd::contextBackground(),
	    new SnapshotRequestChangesParams{
	        .OpenProjects = {DocumentIdentifier{.FileName =
	                                            "/dev/null/inferred"}}});
	assert::ErrorContains(t, err, "invalid configured project ID");
}

// TestGetCurrentLanguageServerSnapshotCloseAndReopenProject —
// session_apistate_test.go:111.
void TestGetCurrentLanguageServerSnapshotCloseAndReopenProject(T* t) {
	t->Parallel();

	const std::string configFileName = "/home/projects/p/tsconfig.json";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {configFileName, "{}"},
	});
	projectSessionCloser ps{projectSession};
	auto ctx = gostd::contextBackground();

	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};
	DocumentIdentifier open{.FileName = configFileName};
	auto [r1, err] = session->handleGetCurrentLanguageServerSnapshot(
	    ctx, new GetCurrentLanguageServerSnapshotParams{
	             .Changes = std::make_shared<LanguageServerSnapshotChanges>(
	                 LanguageServerSnapshotChanges{
	                     {.OpenProjects = {open}}})});
	assert::NilError(t, err);

	auto [r2, err2] = session->handleGetCurrentLanguageServerSnapshot(
	    ctx, new GetCurrentLanguageServerSnapshotParams{
	             .Changes = std::make_shared<LanguageServerSnapshotChanges>(
	                 LanguageServerSnapshotChanges{
	                     {.OpenProjects = {open}, .CloseProjects = {open}}})});
	assert::NilError(t, err2);
	assert::Equal(t, (int)session->openProjects.Len(), 1);
	assert::Assert(t, projectSession->Snapshot()
	                      ->ProjectCollection
	                      ->ConfiguredProject(tspath::Path(configFileName)) !=
	                      nullptr);

	auto [r3, err3] = session->handleGetCurrentLanguageServerSnapshot(
	    ctx, new GetCurrentLanguageServerSnapshotParams{
	             .Changes = std::make_shared<LanguageServerSnapshotChanges>(
	                 LanguageServerSnapshotChanges{
	                     {.CloseProjects = {open}}})});
	assert::NilError(t, err3);
	assert::Equal(t, (int)session->openProjects.Len(), 0);
}

// TestGetCurrentLanguageServerSnapshotCloseAndReopenFile —
// session_apistate_test.go:144.
void TestGetCurrentLanguageServerSnapshotCloseAndReopenFile(T* t) {
	t->Parallel();

	const std::string fileName = "/home/projects/p/index.ts";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {"/home/projects/p/tsconfig.json", "{}"},
	    {fileName, "export const value = 1;"},
	});
	projectSessionCloser ps{projectSession};
	auto ctx = gostd::contextBackground();
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};
	DocumentIdentifier open{.FileName = fileName};
	auto [r1, err] = session->handleGetCurrentLanguageServerSnapshot(
	    ctx, new GetCurrentLanguageServerSnapshotParams{
	             .Changes = std::make_shared<LanguageServerSnapshotChanges>(
	                 LanguageServerSnapshotChanges{
	                     {.OpenFiles = std::vector<DocumentIdentifier>{open}}})});
	assert::NilError(t, err);

	auto [r2, err2] = session->handleGetCurrentLanguageServerSnapshot(
	    ctx, new GetCurrentLanguageServerSnapshotParams{
	             .Changes = std::make_shared<LanguageServerSnapshotChanges>(
	                 LanguageServerSnapshotChanges{
	                     {.OpenFiles = std::vector<DocumentIdentifier>{open}, .CloseFiles = {open}}})});
	assert::NilError(t, err2);
	assert::Equal(t, (int)session->openFiles.Len(), 1);
	assert::Assert(t, projectSession->Snapshot()->GetDefaultProject(
	                      open.ToURI(projectSession->GetCurrentDirectory())) !=
	                      nullptr);

	auto [r3, err3] = session->handleGetCurrentLanguageServerSnapshot(
	    ctx, new GetCurrentLanguageServerSnapshotParams{
	             .Changes = std::make_shared<LanguageServerSnapshotChanges>(
	                 LanguageServerSnapshotChanges{
	                     {.CloseFiles = {open}}})});
	assert::NilError(t, err3);
	assert::Equal(t, (int)session->openFiles.Len(), 0);
}

// TestGetCurrentLanguageServerSnapshotFlushesPendingLSPChanges —
// session_apistate_test.go:177.
void TestGetCurrentLanguageServerSnapshotFlushesPendingLSPChanges(T* t) {
	t->Parallel();

	const std::string fileName = "/home/projects/p/index.ts";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {fileName, "export const value: string = 1;"},
	});
	projectSessionCloser ps{projectSession};

	projectSession->DidOpenFile(
	    gostd::contextBackground(),
	    DocumentIdentifier{.FileName = fileName}.ToURI(
	        projectSession->GetCurrentDirectory()),
	    1,
	    "export const value: string = \"ok\";",
	    lsproto::LanguageKindTypeScript);

	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};
	auto [response, err] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{});
	assert::NilError(t, err);
	assert::Equal(t, response->Snapshot,
	              snapshotHandle(projectSession->Snapshot()));

	auto [snapshot, err2] = session->getSnapshotData(response->Snapshot);
	assert::NilError(t, err2);
	assert::Equal(t, snapshot->snapshot->GetFile(fileName)->Content(),
	              std::string("export const value: string = \"ok\";"));
}

// TestGetCurrentLanguageServerSnapshotReportsOpenedFilesInRequestOrder —
// session_apistate_test.go:205.
void TestGetCurrentLanguageServerSnapshotReportsOpenedFilesInRequestOrder(
    T* t) {
	t->Parallel();

	const std::string configuredFile = "/home/projects/p/index.ts";
	const std::string inferredFile = "/home/projects/loose.ts";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {"/home/projects/p/tsconfig.json", "{}"},
	    {configuredFile, "export const configured = 1;"},
	    {inferredFile, "export const inferred = 1;"},
	});
	projectSessionCloser ps{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};

	auto changes = std::make_shared<LanguageServerSnapshotChanges>(
	    LanguageServerSnapshotChanges{
	        {.OpenFiles = std::vector<DocumentIdentifier>{DocumentIdentifier{.FileName = inferredFile},
	                       DocumentIdentifier{.FileName = configuredFile}}}});
	auto [first, err] = session->handleGetCurrentLanguageServerSnapshot(
	    gostd::contextBackground(),
	    new GetCurrentLanguageServerSnapshotParams{.Changes = changes});
	assert::NilError(t, err);
	assert::Equal(t, (int)first->Operation->OpenedFiles->size(), 2);
	assert::Equal(t, (*first->Operation->OpenedFiles)[0]->Project,
	              inferredProjectID());
	assert::Equal(t, (*first->Operation->OpenedFiles)[1]->Project,
	              configuredProjectID("/home/projects/p/tsconfig.json"));
	assert::Assert(t, utils->FS()
	                          ->WriteFile(configuredFile,
	                                      "export const configured = 2;")
	                          .impl() == nullptr);
	changeWatched(projectSession, configuredFile);
	auto [dirty, err2] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .BaseSnapshot = first->Snapshot});
	assert::NilError(t, err2);
	assert::Equal(t, projectSession->Snapshot()
	                     ->ProjectCollection
	                     ->ConfiguredProject(
	                         tspath::Path("/home/projects/p/tsconfig.json"))
	                     ->IsDirty(),
	              true);

	auto [reopened, err3] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .BaseSnapshot = dirty->Snapshot, .Changes = changes});
	assert::NilError(t, err3);
	// DeepEqual dereferences pointers — OpenedFileOperationResult is a
	// single-field record, so compare Project per element.
	{
		auto& reopenedFiles = *reopened->Operation->OpenedFiles;
		auto& firstFiles = *first->Operation->OpenedFiles;
		assert::Equal(t, (int)reopenedFiles.size(), (int)firstFiles.size());
		for (size_t i = 0; i < reopenedFiles.size() &&
		                    i < firstFiles.size();
		     ++i) {
			assert::Equal(t, reopenedFiles[i]->Project,
			              firstFiles[i]->Project);
		}
	}
	assert::Equal(t, projectSession->Snapshot()
	                     ->ProjectCollection
	                     ->ConfiguredProject(
	                         tspath::Path("/home/projects/p/tsconfig.json"))
	                     ->IsDirty(),
	              false);
	assert::Equal(t, (int)session->openFiles.Len(), 2);
}

// TestGetCurrentLanguageServerSnapshotCreatesAndRemovesPrograms —
// session_apistate_test.go:246.
void TestGetCurrentLanguageServerSnapshotCreatesAndRemovesPrograms(T* t) {
	t->Parallel();

	const std::string fileName = "/home/projects/p/index.ts";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {fileName, "export const value = 1;"},
	});
	projectSessionCloser ps{projectSession};

	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};
	auto [created, err] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .Changes =
	                std::make_shared<LanguageServerSnapshotChanges>(
	                    LanguageServerSnapshotChanges{
	                        {.CreatePrograms = std::vector<std::shared_ptr<CreateSnapshotProgramParams>>{
	                             std::make_shared<CreateSnapshotProgramParams>(
	                                 CreateSnapshotProgramParams{
	                                     .RootFiles = {DocumentIdentifier{
	                                         .FileName = fileName}},
	                                     .CompilerOptions = CompilerOptions{
	                                         .NoLib = Tristate::True}})}}})});
	assert::NilError(t, err);
	assert::Equal(t, (int)created->Projects.size(), 1);
	assert::Equal(t, (int)projectSession->Snapshot()
	                      ->ProjectCollection
	                      ->SyntheticProjects()
	                      .size(),
	              1);

	auto [removed, err2] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .Changes =
	                std::make_shared<LanguageServerSnapshotChanges>(
	                    LanguageServerSnapshotChanges{
	                        {.RemovePrograms = {syntheticProjectID(1),
	                                            syntheticProjectID(1)}}})});
	assert::NilError(t, err2);
	assert::Equal(t, (int)removed->Projects.size(), 0);
	assert::Equal(t, (int)projectSession->Snapshot()
	                      ->ProjectCollection
	                      ->SyntheticProjects()
	                      .size(),
	              0);
}

// TestOpenFilePreservesWindowsDriveLetterCase — session_apistate_test.go:277.
void TestOpenFilePreservesWindowsDriveLetterCase(T* t) {
	t->Parallel();

	const std::string fileName = "D:/repo/index.ts";
	auto [init, utils] = projecttestutil::GetSessionInitOptions(
	    {
	        {"D:/repo/tsconfig.json", "{}"},
	        {fileName, "export const value = 1;"},
	    },
	    nullptr, std::make_shared<projecttestutil::TypingsInstallerOptions>());
	init->Options->CurrentDirectory = "D:/repo";
	project::Session* projectSession = project::NewSession(init.get());
	projectSessionCloser ps{projectSession};

	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};

	auto [response, err] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .Changes =
	                std::make_shared<LanguageServerSnapshotChanges>(
	                    LanguageServerSnapshotChanges{
	                        {.OpenFiles = std::vector<DocumentIdentifier>{DocumentIdentifier{
	                             .FileName = fileName}}}})});
	assert::NilError(t, err);

	auto project_ = response->Projects[0];
	auto [snapshot, err2] = session->getSnapshotData(response->Snapshot);
	assert::NilError(t, err2);
	auto [program, err3] = snapshot->getProgram(project_->Id);
	assert::NilError(t, err3);
	assert::Equal(t, program->GetSourceFile(fileName)->FileName(), fileName);
}

// TestClosingAPISessionRemovesCreatedLanguageServerPrograms —
// session_apistate_test.go:307.
void TestClosingAPISessionRemovesCreatedLanguageServerPrograms(T* t) {
	t->Parallel();

	const std::string fileName = "/home/projects/p/index.ts";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {fileName, "export const value = 1;"},
	});
	projectSessionCloser ps{projectSession};

	auto session = NewLSPSession(projectSession, nullptr);
	auto [created, err] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .Changes =
	                std::make_shared<LanguageServerSnapshotChanges>(
	                    LanguageServerSnapshotChanges{
	                        {.CreatePrograms = std::vector<std::shared_ptr<CreateSnapshotProgramParams>>{
	                             std::make_shared<CreateSnapshotProgramParams>(
	                                 CreateSnapshotProgramParams{
	                                     .RootFiles = {DocumentIdentifier{
	                                         .FileName = fileName}},
	                                     .CompilerOptions = CompilerOptions{
	                                         .NoLib = Tristate::True}})}}})});
	assert::NilError(t, err);
	assert::Equal(t, (int)projectSession->Snapshot()
	                      ->ProjectCollection
	                      ->SyntheticProjects()
	                      .size(),
	              1);

	session->Close();
	assert::Equal(t, (int)projectSession->Snapshot()
	                      ->ProjectCollection
	                      ->SyntheticProjects()
	                      .size(),
	              0);
}

// TestLanguageServerProgramOwnershipIsIsolatedByAPISession —
// session_apistate_test.go:330.
void TestLanguageServerProgramOwnershipIsIsolatedByAPISession(T* t) {
	t->Parallel();

	const std::string fileName = "/home/projects/p/index.ts";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {fileName, "export const value = 1;"},
	});
	projectSessionCloser ps{projectSession};

	auto owner = NewLSPSession(projectSession, nullptr);
	auto [r, err] = owner->handleGetCurrentLanguageServerSnapshot(
	    gostd::contextBackground(),
	    new GetCurrentLanguageServerSnapshotParams{
	        .Changes = std::make_shared<LanguageServerSnapshotChanges>(
	            LanguageServerSnapshotChanges{
	                {.CreatePrograms = std::vector<std::shared_ptr<CreateSnapshotProgramParams>>{
	                     std::make_shared<CreateSnapshotProgramParams>(
	                         CreateSnapshotProgramParams{
	                             .RootFiles = {DocumentIdentifier{
	                                 .FileName = fileName}},
	                             .CompilerOptions = CompilerOptions{
	                                 .NoLib = Tristate::True}})}}})});
	assert::NilError(t, err);

	auto other = NewLSPSession(projectSession, nullptr);
	auto [r2, err2] = other->handleGetCurrentLanguageServerSnapshot(
	    gostd::contextBackground(),
	    new GetCurrentLanguageServerSnapshotParams{
	        .Changes = std::make_shared<LanguageServerSnapshotChanges>(
	            LanguageServerSnapshotChanges{
	                {.RemovePrograms = {syntheticProjectID(1)}}})});
	assert::NilError(t, err2);
	assert::Equal(t, (int)projectSession->Snapshot()
	                      ->ProjectCollection
	                      ->SyntheticProjects()
	                      .size(),
	              1);

	other->Close();
	assert::Equal(t, (int)projectSession->Snapshot()
	                      ->ProjectCollection
	                      ->SyntheticProjects()
	                      .size(),
	              1);
	owner->Close();
	assert::Equal(t, (int)projectSession->Snapshot()
	                      ->ProjectCollection
	                      ->SyntheticProjects()
	                      .size(),
	              0);
}

// TestLanguageServerProgramReconfigurationIsIsolatedByAPISession —
// session_apistate_test.go:363.
void TestLanguageServerProgramReconfigurationIsIsolatedByAPISession(T* t) {
	t->Parallel();

	const std::string fileName = "/home/projects/p/index.ts";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {fileName, "export const value = 1;"},
	});
	projectSessionCloser ps{projectSession};

	auto owner = NewLSPSession(projectSession, nullptr);
	apiSessionCloser ownerCloser{owner};
	auto [created, err] = owner->handleGetCurrentLanguageServerSnapshot(
	    gostd::contextBackground(),
	    new GetCurrentLanguageServerSnapshotParams{
	        .Changes = std::make_shared<LanguageServerSnapshotChanges>(
	            LanguageServerSnapshotChanges{
	                {.CreatePrograms = std::vector<std::shared_ptr<CreateSnapshotProgramParams>>{
	                     std::make_shared<CreateSnapshotProgramParams>(
	                         CreateSnapshotProgramParams{
	                             .RootFiles = {DocumentIdentifier{
	                                 .FileName = fileName}},
	                             .CompilerOptions = CompilerOptions{
	                                 .NoLib = Tristate::True}})}}})});
	assert::NilError(t, err);
	project::SyntheticProjectID programID{
	    created->Projects[0]->Id};

	auto other = NewLSPSession(projectSession, nullptr);
	apiSessionCloser otherCloser{other};
	auto [r2, err2] = other->handleGetCurrentLanguageServerSnapshot(
	    gostd::contextBackground(),
	    new GetCurrentLanguageServerSnapshotParams{
	        .Changes = std::make_shared<LanguageServerSnapshotChanges>(
	            LanguageServerSnapshotChanges{
	                {.ReconfigurePrograms = {
	                     std::make_shared<ReconfigureSnapshotProgramParams>(
	                         ReconfigureSnapshotProgramParams{
	                             .Id = programID,
	                             .RootFiles = {DocumentIdentifier{
	                                 .FileName = fileName}},
	                             .CompilerOptions = CompilerOptions{
	                                 .NoLib = Tristate::True,
	                                 .Strict = Tristate::True}})}}})});
	assert::ErrorContains(t, err2, "not owned by this API session");
}

// TestOpeningProjectOwnedByAnotherAPISessionEnsuresProgram —
// session_apistate_test.go:397.
void TestOpeningProjectOwnedByAnotherAPISessionEnsuresProgram(T* t) {
	t->Parallel();

	const std::string configFileName = "/home/projects/p/tsconfig.json";
	const std::string fileName = "/home/projects/p/index.ts";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {configFileName, "{}"},
	    {fileName, "export const value = 1;"},
	});
	projectSessionCloser ps{projectSession};
	auto openProject = std::make_shared<LanguageServerSnapshotChanges>(
	    LanguageServerSnapshotChanges{
	        {.OpenProjects = {DocumentIdentifier{
	             .FileName = configFileName}}}});

	auto owner = NewLSPSession(projectSession, nullptr);
	auto [r, err] = owner->handleGetCurrentLanguageServerSnapshot(
	    gostd::contextBackground(),
	    new GetCurrentLanguageServerSnapshotParams{.Changes = openProject});
	assert::NilError(t, err);
	assert::Assert(t, utils->FS()
	                          ->WriteFile(fileName, "export const value = 2;")
	                          .impl() == nullptr);
	changeWatched(projectSession, fileName);
	auto [r2, err2] = owner->handleGetCurrentLanguageServerSnapshot(
	    gostd::contextBackground(), new GetCurrentLanguageServerSnapshotParams{});
	assert::NilError(t, err2);
	assert::Equal(t, projectSession->Snapshot()
	                     ->ProjectCollection
	                     ->ConfiguredProject(tspath::Path(configFileName))
	                     ->IsDirty(),
	              true);

	auto other = NewLSPSession(projectSession, nullptr);
	auto [opened, err3] = other->handleGetCurrentLanguageServerSnapshot(
	    gostd::contextBackground(),
	    new GetCurrentLanguageServerSnapshotParams{.Changes = openProject});
	assert::NilError(t, err3);
	assert::Equal(t, opened->Projects[0]->Dirty, false);
	assert::Equal(t, (int)other->openProjects.Len(), 1);

	other->Close();
	assert::Assert(t, projectSession->Snapshot()
	                      ->ProjectCollection
	                      ->ConfiguredProject(tspath::Path(configFileName)) !=
	                      nullptr);
	owner->Close();
	assert::Assert(t, projectSession->Snapshot()
	                      ->ProjectCollection
	                      ->ConfiguredProject(tspath::Path(configFileName)) ==
	                      nullptr);
}

// TestFailedLanguageServerSnapshotOpenIsNotAdopted —
// session_apistate_test.go:435.
void TestFailedLanguageServerSnapshotOpenIsNotAdopted(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup({
	    {"/notes.txt", "text"},
	});
	projectSessionCloser ps{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};

	auto* baseSnapshot = projectSession->Snapshot();
	auto [response, err] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .Changes =
	                std::make_shared<LanguageServerSnapshotChanges>(
	                    LanguageServerSnapshotChanges{
	                        {.OpenFiles = std::vector<DocumentIdentifier>{DocumentIdentifier{
	                             .FileName = "/notes.txt"}}}})});

	assert::Assert(t, response == nullptr);
	assert::ErrorContains(t, err, "no project found for opened file");
	assert::Assert(t, projectSession->Snapshot() == baseSnapshot);
	assert::Equal(t, (int)session->openFiles.Len(), 0);
}

// TestGetCurrentLanguageServerSnapshotOpeningLSPFileEnsuresConfiguredProgram —
// session_apistate_test.go:458.
void TestGetCurrentLanguageServerSnapshotOpeningLSPFileEnsuresConfiguredProgram(
    T* t) {
	t->Parallel();

	const std::string configFileName = "/home/projects/p/tsconfig.json";
	const std::string fileName = "/home/projects/p/index.ts";
	auto [projectSession, utils] = projecttestutil::Setup({
	    {configFileName, "{}"},
	    {fileName, "export const value = 1;"},
	});
	projectSessionCloser ps{projectSession};
	auto uri = DocumentIdentifier{.FileName = fileName}.ToURI(
	    projectSession->GetCurrentDirectory());
	projectSession->DidOpenFile(gostd::contextBackground(), uri, 1,
	                            "export const value = 1;",
	                            lsproto::LanguageKindTypeScript);

	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser as{session};
	auto [initial, err] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{});
	assert::NilError(t, err);
	assert::Equal(t, initial->Projects[0]->Dirty, false);
	project::ID projectID = initial->Projects[0]->Id;

	lsproto::TextDocumentContentChangePartialOrWholeDocument change;
	change.WholeDocument =
	    std::make_shared<lsproto::TextDocumentContentChangeWholeDocument>();
	change.WholeDocument->Text = "export const value = 2;";
	projectSession->DidChangeFile(gostd::contextBackground(), uri, 2, {change});
	auto [dirty, err2] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .BaseSnapshot = initial->Snapshot});
	assert::NilError(t, err2);
	assert::Equal(t, dirty->Projects[0]->Dirty, true);

	auto [ensured, err3] =
	    session->handleGetCurrentLanguageServerSnapshot(
	        gostd::contextBackground(),
	        new GetCurrentLanguageServerSnapshotParams{
	            .BaseSnapshot = dirty->Snapshot,
	            .Changes =
	                std::make_shared<LanguageServerSnapshotChanges>(
	                    LanguageServerSnapshotChanges{
	                        {.OpenFiles = std::vector<DocumentIdentifier>{DocumentIdentifier{
	                             .FileName = fileName}}}})});
	assert::NilError(t, err3);
	assert::Equal(t, ensured->Projects[0]->Dirty, false);
	assert::Equal(t, (*ensured->Operation->OpenedFiles)[0]->Project, projectID);
}

REGISTER_UNIT_TEST(
    "api.TestGetCurrentLanguageServerSnapshotAdoptsChanges",
    TestGetCurrentLanguageServerSnapshotAdoptsChanges);
REGISTER_UNIT_TEST(
    "api.TestGetCurrentLanguageServerSnapshotRejectsStandaloneSession",
    TestGetCurrentLanguageServerSnapshotRejectsStandaloneSession);
REGISTER_UNIT_TEST("api.TestOpenProjectRejectsReservedProjectID",
                   TestOpenProjectRejectsReservedProjectID);
REGISTER_UNIT_TEST(
    "api.TestGetCurrentLanguageServerSnapshotCloseAndReopenProject",
    TestGetCurrentLanguageServerSnapshotCloseAndReopenProject);
REGISTER_UNIT_TEST(
    "api.TestGetCurrentLanguageServerSnapshotCloseAndReopenFile",
    TestGetCurrentLanguageServerSnapshotCloseAndReopenFile);
REGISTER_UNIT_TEST(
    "api.TestGetCurrentLanguageServerSnapshotFlushesPendingLSPChanges",
    TestGetCurrentLanguageServerSnapshotFlushesPendingLSPChanges);
REGISTER_UNIT_TEST(
    "api.TestGetCurrentLanguageServerSnapshotReportsOpenedFilesInRequestOrder",
    TestGetCurrentLanguageServerSnapshotReportsOpenedFilesInRequestOrder);
REGISTER_UNIT_TEST(
    "api.TestGetCurrentLanguageServerSnapshotCreatesAndRemovesPrograms",
    TestGetCurrentLanguageServerSnapshotCreatesAndRemovesPrograms);
REGISTER_UNIT_TEST("api.TestOpenFilePreservesWindowsDriveLetterCase",
                   TestOpenFilePreservesWindowsDriveLetterCase);
REGISTER_UNIT_TEST(
    "api.TestClosingAPISessionRemovesCreatedLanguageServerPrograms",
    TestClosingAPISessionRemovesCreatedLanguageServerPrograms);
REGISTER_UNIT_TEST(
    "api.TestLanguageServerProgramOwnershipIsIsolatedByAPISession",
    TestLanguageServerProgramOwnershipIsIsolatedByAPISession);
REGISTER_UNIT_TEST(
    "api.TestLanguageServerProgramReconfigurationIsIsolatedByAPISession",
    TestLanguageServerProgramReconfigurationIsIsolatedByAPISession);
REGISTER_UNIT_TEST(
    "api.TestOpeningProjectOwnedByAnotherAPISessionEnsuresProgram",
    TestOpeningProjectOwnedByAnotherAPISessionEnsuresProgram);
REGISTER_UNIT_TEST("api.TestFailedLanguageServerSnapshotOpenIsNotAdopted",
                   TestFailedLanguageServerSnapshotOpenIsNotAdopted);
REGISTER_UNIT_TEST(
    "api.TestGetCurrentLanguageServerSnapshotOpeningLSPFileEnsuresConfigured"
    "Program",
    TestGetCurrentLanguageServerSnapshotOpeningLSPFileEnsuresConfiguredProgram);

} // namespace tsc::api
