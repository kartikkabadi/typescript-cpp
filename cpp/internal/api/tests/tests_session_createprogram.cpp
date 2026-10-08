// Port of tsc/internal/api/session_createprogram_test.go (package api).
#include <memory>
#include <string>
#include <vector>

#include "internal/api/proto.h"
#include "internal/api/session.h"
#include "internal/bundled/bundled.h"
#include "internal/compiler/program.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/project/project.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::api {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace projecttestutil = tsc::testutil::projecttestutil;

void TestCreateSnapshotUsesIndependentRoots(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto [init, utils] = projecttestutil::GetSessionInitOptions(
	    projecttestutil::FileMap{
	        {"/home/projects/p/src/index.ts", "export const x = 1;"},
	    },
	    nullptr, std::make_shared<projecttestutil::TypingsInstallerOptions>());
	auto session = NewStandaloneSession(init.get(), nullptr);
	struct closer {
		std::shared_ptr<Session> s;
		~closer() { s->Close(); }
	} c{session};

	CreateSnapshotParams params;
	params.CreatePrograms.emplace().push_back(
	    std::make_shared<CreateSnapshotProgramParams>(CreateSnapshotProgramParams{
	        .RootFiles = {DocumentIdentifier{
	            .FileName = "/home/projects/p/src/index.ts"}},
	        .CompilerOptions = tsc::CompilerOptions{.NoLib =
	                                                  Tristate::True},
	    }));
	auto [firstResponse, err] =
	    session->handleCreateSnapshot(gostd::contextBackground(), &params);
	assert::NilError(t, err);
	assert::Equal(t, firstResponse->Snapshot, SnapshotID(1));
	assert::Equal(t, int(firstResponse->Projects.size()), 1);

	CreateSnapshotParams params2;
	auto [response, err2] =
	    session->handleCreateSnapshot(gostd::contextBackground(), &params2);
	assert::NilError(t, err2);
	assert::Equal(t, response->Snapshot, SnapshotID(2));
	assert::Equal(t, int(response->Projects.size()), 0);
	assert::Equal(t, int(firstResponse->Projects.size()), 1);
}
REGISTER_UNIT_TEST("api.TestCreateSnapshotUsesIndependentRoots",
                   TestCreateSnapshotUsesIndependentRoots);

void TestCreateSnapshotCreatesPrograms(T* t) {
	t->Parallel();

	const std::string fileA = "/home/projects/p/a.ts";
	const std::string fileB = "/home/projects/p/b.ts";
	auto [projectSession, utils] =
	    projecttestutil::Setup(projecttestutil::FileMap{
	        {fileA, "export const a = 1;"},
	        {fileB, "export const b = 1;"},
	    });
	struct pclose {
		project::Session* s;
		~pclose() { s->Close(); }
	} pc{projectSession};

	auto session = NewLSPSession(projectSession, nullptr);
	struct closer {
		std::shared_ptr<Session> s;
		~closer() { s->Close(); }
	} c{session};

	CreateSnapshotParams params;
	params.CreatePrograms = std::vector<std::shared_ptr<CreateSnapshotProgramParams>>{
	    std::make_shared<CreateSnapshotProgramParams>(
	        CreateSnapshotProgramParams{
	            .RootFiles = {DocumentIdentifier{.FileName = fileA},
	                          DocumentIdentifier{.FileName = fileB}},
	            .CompilerOptions = tsc::CompilerOptions{
	                .NoLib = Tristate::True, .Strict = Tristate::True},
	        }),
	    std::make_shared<CreateSnapshotProgramParams>(
	        CreateSnapshotProgramParams{
	            .RootFiles = {DocumentIdentifier{.FileName = fileB}},
	            .CompilerOptions =
	                tsc::CompilerOptions{.NoLib = Tristate::True},
	        }),
	};
	auto [response, err] =
	    session->handleCreateSnapshot(gostd::contextBackground(), &params);
	assert::NilError(t, err);
	assert::Equal(t, int(response->Projects.size()), 2);
	assert::DeepEqual(
	    t, *response->Operation->CreatedPrograms,
	    std::vector<project::SyntheticProjectID>{
	        project::NewSyntheticProjectID(1),
	        project::NewSyntheticProjectID(2)});
	assert::Equal(t, response->Projects[0]->ConfigFileName,
	              std::string());
	assert::Equal(t, response->Projects[1]->ConfigFileName,
	              std::string());
	assert::DeepEqual(t, response->Projects[0]->RootFiles,
	                  std::vector<std::string>({fileA, fileB}));
	assert::Equal(t, response->Projects[0]->CompilerOptions->Strict,
	              Tristate::True);
	assert::DeepEqual(t, response->Projects[1]->RootFiles,
	                  std::vector<std::string>{fileB});

	auto [snapshot, err2] =
	    session->getSnapshotData(response->Snapshot);
	assert::NilError(t, err2);
	assert::Equal(t, int(snapshot->snapshot->CreatedPrograms().size()),
	              2);
	for (auto& projectResponse : response->Projects) {
		assert::Assert(t, snapshot->snapshot->ProjectCollection
		                      ->GetProject(projectResponse->Id) != nullptr);
	}
}
REGISTER_UNIT_TEST("api.TestCreateSnapshotCreatesPrograms",
                   TestCreateSnapshotCreatesPrograms);

void TestCreateSnapshotPreservesWindowsRootDriveLetterCase(T* t) {
	t->Parallel();

	const std::string fileName = "D:/repo/index.ts";
	auto [init, utils] = projecttestutil::GetSessionInitOptions(
	    projecttestutil::FileMap{
	        {fileName, "export const value = 1;"},
	    },
	    nullptr, std::make_shared<projecttestutil::TypingsInstallerOptions>());
	init->Options->CurrentDirectory = "D:/repo";
	auto session = NewStandaloneSession(init.get(), nullptr);
	struct closer {
		std::shared_ptr<Session> s;
		~closer() { s->Close(); }
	} c{session};

	CreateSnapshotParams params;
	params.CreatePrograms.emplace().push_back(
	    std::make_shared<CreateSnapshotProgramParams>(CreateSnapshotProgramParams{
	        .RootFiles = {DocumentIdentifier{
	            .URI = lsproto::DocumentUri("file:///D%3A/repo/index.ts")}},
	        .CompilerOptions = tsc::CompilerOptions{.NoLib =
	                                                  Tristate::True},
	    }));
	auto [response, err] =
	    session->handleCreateSnapshot(gostd::contextBackground(), &params);
	assert::NilError(t, err);

	auto [snapshot, err2] =
	    session->getSnapshotData(response->Snapshot);
	assert::NilError(t, err2);
	auto [program, err3] = snapshot->getProgram(response->Projects[0]->Id);
	assert::NilError(t, err3);
	assert::Equal(t, program->GetSourceFile(fileName)->FileName(),
	              fileName);
}
REGISTER_UNIT_TEST("api.TestCreateSnapshotPreservesWindowsRootDriveLetterCase",
                   TestCreateSnapshotPreservesWindowsRootDriveLetterCase);

void TestSnapshotOperationResponseOmitsUnrequestedFields(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup({});
	struct pclose {
		project::Session* s;
		~pclose() { s->Close(); }
	} pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	struct closer {
		std::shared_ptr<Session> s;
		~closer() { s->Close(); }
	} c{session};

	CreateSnapshotParams params;
	auto [response, err] =
	    session->handleCreateSnapshot(gostd::contextBackground(), &params);
	assert::NilError(t, err);
	auto encoded = json::marshal(*response->Operation);
	assert::Equal(t, encoded.first, std::string("{}"));
	assert::Assert(t, encoded.second.empty());

	CreateSnapshotParams params2;
	params2.CreatePrograms.emplace();
	params2.ReconfigurePrograms = {};
	params2.OpenFiles.emplace();
	auto [response2, err2] = session->handleCreateSnapshot(
	    gostd::contextBackground(), &params2);
	assert::NilError(t, err2);
	encoded = json::marshal(*response2->Operation);
	assert::Assert(t, encoded.second.empty());
	assert::Equal(t, encoded.first,
	              std::string("{\"createdPrograms\":[],\"openedFiles\":[]}"));
}
REGISTER_UNIT_TEST("api.TestSnapshotOperationResponseOmitsUnrequestedFields",
                   TestSnapshotOperationResponseOmitsUnrequestedFields);

void TestUpdateSnapshotReconfiguresSyntheticProgram(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/home/projects/p/a.ts", "export const a = 1;"},
	        {"/home/projects/p/b.ts", "export const b = 2;"},
	    });
	struct pclose {
		project::Session* s;
		~pclose() { s->Close(); }
	} pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	struct closer {
		std::shared_ptr<Session> s;
		~closer() { s->Close(); }
	} c{session};

	CreateSnapshotParams params;
	params.CreatePrograms.emplace().push_back(
	    std::make_shared<CreateSnapshotProgramParams>(CreateSnapshotProgramParams{
	        .RootFiles = {DocumentIdentifier{
	            .FileName = "/home/projects/p/a.ts"}},
	        .CompilerOptions = tsc::CompilerOptions{.NoLib =
	                                                  Tristate::True},
	    }));
	auto [created, err] =
	    session->handleCreateSnapshot(gostd::contextBackground(), &params);
	assert::NilError(t, err);
	auto programID = (*created->Operation->CreatedPrograms)[0];

	auto changes = std::make_shared<CreateSnapshotParams>();
	changes->ReconfigurePrograms.push_back(
	    std::make_shared<ReconfigureSnapshotProgramParams>(
	        ReconfigureSnapshotProgramParams{
	            .Id = programID,
	            .RootFiles = {DocumentIdentifier{
	                .FileName = "/home/projects/p/b.ts"}},
	            .CompilerOptions = tsc::CompilerOptions{
	                .NoLib = Tristate::True, .Strict = Tristate::True},
	        }));
	UpdateSnapshotParams updateParams{
	    .Snapshot = created->Snapshot,
	    .Changes = changes,
	};
	auto [reconfigured, err2] = session->handleUpdateSnapshot(
	    gostd::contextBackground(), &updateParams);
	assert::NilError(t, err2);
	assert::Equal(t, reconfigured->Projects[0]->Id,
	              project::ID(programID));
	assert::DeepEqual(t, reconfigured->Projects[0]->RootFiles,
	                  std::vector<std::string>{"/home/projects/p/b.ts"});
	assert::Equal(t, reconfigured->Projects[0]->CompilerOptions->Strict,
	              Tristate::True);
}
REGISTER_UNIT_TEST("api.TestUpdateSnapshotReconfiguresSyntheticProgram",
                   TestUpdateSnapshotReconfiguresSyntheticProgram);

void TestReconfigureSyntheticProgramValidation(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup({});
	struct pclose {
		project::Session* s;
		~pclose() { s->Close(); }
	} pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	struct closer {
		std::shared_ptr<Session> s;
		~closer() { s->Close(); }
	} c{session};

	auto program =
	    std::make_shared<ReconfigureSnapshotProgramParams>(
	        ReconfigureSnapshotProgramParams{
	            .Id = project::SyntheticProjectID("/dev/null/synthetic/1")});
	SnapshotRequestChangesParams nullReconfigure;
	assert::Assert(
	    t, json::unmarshal("{\"reconfigurePrograms\":[null]}",
	                       &nullReconfigure)
	           .empty());
	auto [r0, err] = session->toAPISnapshotRequest(
	    gostd::contextBackground(), &nullReconfigure);
	assert::ErrorContains(t, err,
	                      "reconfigurePrograms[0] must not be null");

	SnapshotRequestChangesParams badId;
	badId.ReconfigurePrograms = {
	    std::make_shared<ReconfigureSnapshotProgramParams>(
	        ReconfigureSnapshotProgramParams{
	            .Id = project::SyntheticProjectID("/tsconfig.json")})};
	auto [r1, err2] = session->toAPISnapshotRequest(
	    gostd::contextBackground(), &badId);
	assert::ErrorContains(t, err2, "invalid synthetic project handle");

	SnapshotRequestChangesParams dup;
	dup.ReconfigurePrograms = {program, program};
	auto [r2, err3] =
	    session->toAPISnapshotRequest(gostd::contextBackground(), &dup);
	assert::ErrorContains(t, err3, "reconfigured more than once");

	SnapshotRequestChangesParams reconfAndRemove;
	reconfAndRemove.ReconfigurePrograms = {program};
	reconfAndRemove.RemovePrograms = {program->Id};
	auto [r3, err4] = session->toAPISnapshotRequest(
	    gostd::contextBackground(), &reconfAndRemove);
	assert::ErrorContains(t, err4,
	                      "cannot be reconfigured and removed");

	CreateSnapshotParams createParams;
	createParams.CreatePrograms = std::vector<std::shared_ptr<CreateSnapshotProgramParams>>{
	    std::make_shared<CreateSnapshotProgramParams>()};
	createParams.ReconfigurePrograms = {program};
	auto [r4, err5] = session->handleCreateSnapshot(
	    gostd::contextBackground(), &createParams);
	assert::ErrorContains(t, err5, "not found for reconfiguration");
}
REGISTER_UNIT_TEST("api.TestReconfigureSyntheticProgramValidation",
                   TestReconfigureSyntheticProgramValidation);

void TestCreateSyntheticProgramValidation(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup({});
	struct pclose {
		project::Session* s;
		~pclose() { s->Close(); }
	} pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	struct closer {
		std::shared_ptr<Session> s;
		~closer() { s->Close(); }
	} c{session};

	SnapshotRequestChangesParams nullCreate;
	assert::Assert(t, json::unmarshal("{\"createPrograms\":[null]}",
	                                  &nullCreate)
	                      .empty());
	auto [r, err] = session->toAPISnapshotRequest(
	    gostd::contextBackground(), &nullCreate);
	assert::ErrorContains(t, err, "createPrograms[0] must not be null");
	assert::ErrorIs(t, err, ErrClientError);
}
REGISTER_UNIT_TEST("api.TestCreateSyntheticProgramValidation",
                   TestCreateSyntheticProgramValidation);

void TestCreateSnapshotRejectsRemovingProgramFromIndependentRoot(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup({});
	struct pclose {
		project::Session* s;
		~pclose() { s->Close(); }
	} pc{projectSession};

	auto session = NewLSPSession(projectSession, nullptr);
	struct closer {
		std::shared_ptr<Session> s;
		~closer() { s->Close(); }
	} c{session};

	CreateSnapshotParams params;
	params.RemovePrograms = {project::NewSyntheticProjectID(1)};
	auto [r, err] = session->handleCreateSnapshot(
	    gostd::contextBackground(), &params);
	assert::ErrorContains(
	    t, err,
	    "synthetic program not found for removal: /dev/null/synthetic/1");
}
REGISTER_UNIT_TEST("api.TestCreateSnapshotRejectsRemovingProgramFromIndependentRoot",
                   TestCreateSnapshotRejectsRemovingProgramFromIndependentRoot);

void TestUpdateSnapshotEnsuresSyntheticProgram(T* t) {
	t->Parallel();

	const std::string fileName = "/home/projects/p/index.ts";
	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {fileName, "export const value = 1;"},
	    });
	struct pclose {
		project::Session* s;
		~pclose() { s->Close(); }
	} pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	struct closer {
		std::shared_ptr<Session> s;
		~closer() { s->Close(); }
	} c{session};

	CreateSnapshotParams params;
	params.CreatePrograms.emplace().push_back(
	    std::make_shared<CreateSnapshotProgramParams>(CreateSnapshotProgramParams{
	        .RootFiles = {DocumentIdentifier{.FileName = fileName}},
	        .CompilerOptions = tsc::CompilerOptions{.NoLib =
	                                                  Tristate::True},
	    }));
	auto [created, err] =
	    session->handleCreateSnapshot(gostd::contextBackground(), &params);
	assert::NilError(t, err);
	assert::Equal(t, created->Projects[0]->Dirty, false);
	auto projectID = created->Projects[0]->Id;

	assert::Assert(t, utils->FS()
	                      ->WriteFile(fileName, "export const value = 2;")
	                      .impl() == nullptr);
	auto changes = std::make_shared<CreateSnapshotParams>();
	changes->FileNotifications =
	    std::make_shared<FileNotifications>(FileNotifications{
	        .Changed = {DocumentIdentifier{.FileName = fileName}}});
	UpdateSnapshotParams updateParams{
	    .Snapshot = created->Snapshot,
	    .Changes = changes,
	};
	auto [dirty, err2] = session->handleUpdateSnapshot(
	    gostd::contextBackground(), &updateParams);
	assert::NilError(t, err2);
	assert::Equal(t, dirty->Projects[0]->Dirty, true);

	auto changes2 = std::make_shared<CreateSnapshotParams>();
	changes2->EnsurePrograms =
	    std::make_shared<EnsurePrograms>(EnsurePrograms{
	        .Projects = {projectID}});
	UpdateSnapshotParams updateParams2{
	    .Snapshot = dirty->Snapshot,
	    .Changes = changes2,
	};
	auto [ensured, err3] = session->handleUpdateSnapshot(
	    gostd::contextBackground(), &updateParams2);
	assert::NilError(t, err3);
	assert::Equal(t, ensured->Projects[0]->Dirty, false);
}
REGISTER_UNIT_TEST("api.TestUpdateSnapshotEnsuresSyntheticProgram",
                   TestUpdateSnapshotEnsuresSyntheticProgram);

void TestCreateProgramReportsNonCompositeProjectReference(T* t) {
	t->Parallel();

	const std::string root = "/home/projects/p/src/index.ts";
	const std::string referenced = "/home/projects/p/lib/tsconfig.json";
	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {root, "export const x = 1;"},
	        {referenced, "{ \"compilerOptions\": { \"strict\": true } }"},
	        {"/home/projects/p/lib/a.ts", "export const a = 1;"},
	    });
	struct pclose {
		project::Session* s;
		~pclose() { s->Close(); }
	} pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	struct closer {
		std::shared_ptr<Session> s;
		~closer() { s->Close(); }
	} c{session};

	CreateSnapshotParams params;
	params.CreatePrograms.emplace().push_back(
	    std::make_shared<CreateSnapshotProgramParams>(
	        CreateSnapshotProgramParams{
	            .RootFiles = {DocumentIdentifier{.FileName = root}},
	            .CompilerOptions =
	                tsc::CompilerOptions{.NoLib = Tristate::True},
	            .Options = std::make_shared<CreateProgramOptions>(
	                CreateProgramOptions{
	                    .ProjectReferences =
	                        {std::make_shared<tsc::ProjectReference>(
	                            tsc::ProjectReference{
	                                .Path = referenced,
	                                .OriginalPath = "../lib",
	                            })},
	                }),
	        }));
	auto [response, err] = session->handleCreateSnapshot(
	    gostd::contextBackground(), &params);
	assert::NilError(t, err);
	auto projectID = (*response->Operation->CreatedPrograms)[0].AsID();
	auto [diagnostics, err2] = session->handleGetProgramDiagnostics(
	    gostd::contextBackground(),
	    &GetProjectDiagnosticsParams{
	        .Snapshot = response->Snapshot,
	        .Project = projectID,
	    });
	assert::NilError(t, err2);
	std::vector<int32_t> codes;
	codes.reserve(diagnostics.size());
	for (const auto& diagnostic : diagnostics) {
		codes.push_back(diagnostic->Code);
	}
	assert::DeepEqual(t, codes, std::vector<int32_t>{6306});
}
REGISTER_UNIT_TEST("api.TestCreateProgramReportsNonCompositeProjectReference",
                   TestCreateProgramReportsNonCompositeProjectReference);

}  // namespace
}  // namespace tsc::api
