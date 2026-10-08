// Port of tsc/internal/api/session_requestfilesystem_test.go (package api).
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "internal/api/proto.h"
#include "internal/api/requestfilesystem/requestfilesystem.h"
#include "internal/api/session.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/project/project.h"
#include "internal/project/session.h"
#include "internal/project/snapshot.h"
#include "internal/project/snapshothost.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfs.h"

namespace tsc::api {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace projecttestutil = tsc::testutil::projecttestutil;
namespace rfs = tsc::api::requestfilesystem;

struct projectCloser {
	project::Session* s;
	~projectCloser() { s->Close(); }
};

struct sessionCloser {
	std::shared_ptr<Session> s;
	~sessionCloser() { s->Close(); }
};

// session.snapshots[id] — Go map lookup; returns nullptr when absent.
snapshotData* snapshotAt(Session* session, SnapshotID id) {
	auto it = session->snapshots.find(id);
	return it == session->snapshots.end() ? nullptr : it->second.get();
}

// updateCurrentLanguageServerSnapshot — session_requestfilesystem_test.go:19.
std::pair<std::unique_ptr<CreateSnapshotResponse>, gostd::Error>
updateCurrentLanguageServerSnapshot(const gostd::Context& ctx,
                                    const std::shared_ptr<Session>& session,
                                    const CreateSnapshotParams& changes) {
	auto lsChanges = std::make_shared<LanguageServerSnapshotChanges>();
	static_cast<SnapshotRequestChangesParams&>(*lsChanges) =
	    static_cast<const SnapshotRequestChangesParams&>(changes);
	GetCurrentLanguageServerSnapshotParams baseParams;
	baseParams.Changes = lsChanges;
	auto [base, err] =
	    session->handleGetCurrentLanguageServerSnapshot(ctx, &baseParams);
	if (err) {
		return {nullptr, err};
	}
	auto requestChanges = std::make_shared<CreateSnapshotParams>();
	static_cast<SnapshotRequestChangesParams&>(*requestChanges) =
	    static_cast<const SnapshotRequestChangesParams&>(changes);
	requestChanges->EnsurePrograms =
	    std::make_shared<EnsurePrograms>(EnsurePrograms{.All = true});
	requestChanges->FileSystem = changes.FileSystem;
	UpdateSnapshotParams updateParams;
	updateParams.Snapshot = base->Snapshot;
	updateParams.Changes = requestChanges;
	return session->handleUpdateSnapshot(ctx, &updateParams);
}

void didChangeWholeDocument(const gostd::Context& ctx,
                            project::Session* session,
                            const std::string& uri, int32_t version,
                            const std::string& text) {
	lsproto::TextDocumentContentChangePartialOrWholeDocument change;
	change.WholeDocument =
	    std::make_shared<lsproto::TextDocumentContentChangeWholeDocument>();
	change.WholeDocument->Text = text;
	session->DidChangeFile(ctx, uri, version, {change});
}

void TestEditorChangeInvalidatesRequestSymlinkAlias(T* t) {
	t->Parallel();

	auto ctx = gostd::contextBackground();
	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/tsconfig.json",
	         R"__rfs__({ "compilerOptions": { "noLib": true }, "files": ["alias.ts"] })__rfs__"},
	    });
	projectCloser pc{projectSession};
	projectSession->DidOpenFile(ctx, "file:///target.ts", 1, "old",
	                          lsproto::LanguageKindTypeScript);
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};

	auto symlinkFS = [] {
		auto fs = std::make_shared<rfs::RequestFileSystem>();
		fs->kind = rfs::KindLayer;
		fs->Symlinks = std::map<std::string, rfs::RequestSymlink>{
		    {"/alias.ts", rfs::RequestSymlink{.Target = "/target.ts"}},
		};
		return fs;
	};

	CreateSnapshotParams baseParams;
	baseParams.OpenProjects = {DocumentIdentifier{.FileName = "/tsconfig.json"}};
	baseParams.FileSystem = symlinkFS();
	auto [base, err] =
	    updateCurrentLanguageServerSnapshot(ctx, session, baseParams);
	assert::NilError(t, err);
	assert::Equal(t, snapshotAt(session.get(), base->Snapshot)
	                    ->snapshot->ProjectCollection
	                    ->GetProject(project::ID("/tsconfig.json"))
	                    ->GetProgram()
	                    ->GetSourceFile("/alias.ts")
	                    ->Text(),
	              std::string("old"));

	didChangeWholeDocument(ctx, projectSession, "file:///target.ts", 2, "new");
	CreateSnapshotParams updatedParams;
	updatedParams.OpenProjects = {DocumentIdentifier{.FileName = "/tsconfig.json"}};
	updatedParams.FileSystem = symlinkFS();
	auto [updated, err2] =
	    updateCurrentLanguageServerSnapshot(ctx, session, updatedParams);
	assert::NilError(t, err2);
	assert::Equal(t, snapshotAt(session.get(), updated->Snapshot)
	                    ->snapshot->ProjectCollection
	                    ->GetProject(project::ID("/tsconfig.json"))
	                    ->GetProgram()
	                    ->GetSourceFile("/alias.ts")
	                    ->Text(),
	              std::string("new"));
}
REGISTER_UNIT_TEST("api.TestEditorChangeInvalidatesRequestSymlinkAlias",
                   TestEditorChangeInvalidatesRequestSymlinkAlias);

void TestLargeRequestLayerUpdateRetainsChanges(T* t) {
	t->Parallel();

	const int fillerCount = 1000;
	std::map<std::string, std::string> baseFiles;
	std::map<std::string, std::string> updatedFiles;
	for (int i = 0; i < fillerCount; i++) {
		std::string fileName = "/unused/file" + std::to_string(i) + ".ts";
		baseFiles[fileName] = "old";
		updatedFiles[fileName] = "new";
	}
	updatedFiles["/index.ts"] = "new";

	auto ctx = gostd::contextBackground();
	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/tsconfig.json",
	         R"__rfs__({ "compilerOptions": { "noLib": true }, "files": ["index.ts"] })__rfs__"},
	    });
	projectCloser pc{projectSession};
	projectSession->DidOpenFile(ctx, "file:///index.ts", 1, "old",
	                          lsproto::LanguageKindTypeScript);
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};

	CreateSnapshotParams baseParams;
	baseParams.OpenProjects = {DocumentIdentifier{.FileName = "/tsconfig.json"}};
	auto baseFS = std::make_shared<rfs::RequestFileSystem>();
	baseFS->kind = rfs::KindLayer;
	baseFS->Files = baseFiles;
	baseParams.FileSystem = baseFS;
	auto [base, err] =
	    updateCurrentLanguageServerSnapshot(ctx, session, baseParams);
	assert::NilError(t, err);

	auto updatedFS = std::make_shared<rfs::RequestFileSystem>();
	updatedFS->kind = rfs::KindLayer;
	updatedFS->Files = updatedFiles;
	auto updatedChanges = std::make_shared<CreateSnapshotParams>();
	updatedChanges->EnsurePrograms =
	    std::make_shared<EnsurePrograms>(EnsurePrograms{.All = true});
	updatedChanges->FileSystem = updatedFS;
	UpdateSnapshotParams updateParams;
	updateParams.Snapshot = base->Snapshot;
	updateParams.Changes = updatedChanges;
	auto [updated, err2] =
	    session->handleUpdateSnapshot(ctx, &updateParams);
	assert::NilError(t, err2);
	auto [contents, ok] =
	    snapshotAt(session.get(), updated->Snapshot)->snapshot->ReadFile(
	        "/index.ts");
	assert::Assert(t, ok);
	assert::Equal(t, contents, std::string("new"));
	assert::Equal(t, snapshotAt(session.get(), updated->Snapshot)
	                    ->snapshot->ProjectCollection
	                    ->GetProject(project::ID("/tsconfig.json"))
	                    ->GetProgram()
	                    ->GetSourceFile("/index.ts")
	                    ->Text(),
	              std::string("new"));
}
REGISTER_UNIT_TEST("api.TestLargeRequestLayerUpdateRetainsChanges",
                   TestLargeRequestLayerUpdateRetainsChanges);

void TestAutoImportCloneRetainsRequestFileSystem(T* t) {
	t->Parallel();

	auto ctx = gostd::contextBackground();
	auto [projectSession, utils] =
	    projecttestutil::Setup(projecttestutil::FileMap{});
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};

	CreateSnapshotParams baseParams;
	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindFull;
	fs->Files = {{"/index.ts", "request"}};
	baseParams.FileSystem = fs;
	auto [base, err] =
	    updateCurrentLanguageServerSnapshot(ctx, session, baseParams);
	assert::NilError(t, err);
	auto* baseSnapshot = snapshotAt(session.get(), base->Snapshot)->snapshot;

	auto* clone = session->snapshotHost->CloneSnapshotWithAutoImports(
	    ctx, baseSnapshot, "file:///index.ts", nullptr);
	struct cloneReleaser {
		project::Snapshot* s;
		~cloneReleaser() { s->Deref(); }
	} cr{clone};
	auto [content, ok] = clone->ReadFile("/index.ts");
	assert::Assert(t, ok);
	assert::Equal(t, content, std::string("request"));
}
REGISTER_UNIT_TEST("api.TestAutoImportCloneRetainsRequestFileSystem",
                   TestAutoImportCloneRetainsRequestFileSystem);

void TestUnmaskedOverlayContinuesUpdating(T* t) {
	t->Parallel();

	auto ctx = gostd::contextBackground();
	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/tsconfig.json",
	         R"__rfs__({ "compilerOptions": { "noLib": true }, "files": ["index.ts"] })__rfs__"},
	        {"/index.ts", "host"},
	    });
	projectCloser pc{projectSession};
	projectSession->DidOpenFile(ctx, "file:///index.ts", 1, "overlay1",
	                          lsproto::LanguageKindTypeScript);
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};

	auto projectText = [&](SnapshotID id) {
		return snapshotAt(session.get(), id)
		    ->snapshot->ProjectCollection
		    ->GetProject(project::ID("/tsconfig.json"))
		    ->GetProgram()
		    ->GetSourceFile("/index.ts")
		    ->Text();
	};
	auto openProjectParams = [&] {
		CreateSnapshotParams p;
		p.OpenProjects = {DocumentIdentifier{.FileName = "/tsconfig.json"}};
		return p;
	};

	auto maskedParams = openProjectParams();
	auto maskFS = std::make_shared<rfs::RequestFileSystem>();
	maskFS->kind = rfs::KindLayer;
	maskFS->Files = {{"/index.ts", "request"}};
	maskedParams.FileSystem = maskFS;
	auto [masked, err] =
	    updateCurrentLanguageServerSnapshot(ctx, session, maskedParams);
	assert::NilError(t, err);
	assert::Equal(t, projectText(masked->Snapshot), std::string("request"));

	auto unmaskedParams = openProjectParams();
	auto [unmasked, err2] =
	    updateCurrentLanguageServerSnapshot(ctx, session, unmaskedParams);
	assert::NilError(t, err2);
	assert::Equal(t, projectText(unmasked->Snapshot), std::string("overlay1"));

	didChangeWholeDocument(ctx, projectSession, "file:///index.ts", 2,
	                       "overlay2");
	auto updatedParams = openProjectParams();
	auto [updated, err3] =
	    updateCurrentLanguageServerSnapshot(ctx, session, updatedParams);
	assert::NilError(t, err3);
	assert::Equal(t, projectText(updated->Snapshot), std::string("overlay2"));
}
REGISTER_UNIT_TEST("api.TestUnmaskedOverlayContinuesUpdating",
                   TestUnmaskedOverlayContinuesUpdating);

void TestRequestHostMountReadsEditorOverlays(T* t) {
	t->Parallel();

	for (const rfs::Kind& kind : {rfs::KindFull, rfs::KindLayer}) {
		std::string kindName = kind;
		t->Run(kindName, [kind](T* t) {
			t->Parallel();

			auto ctx = gostd::contextBackground();
			auto [projectSession, utils] = projecttestutil::Setup(
			    projecttestutil::FileMap{
			        {"/host/index.ts", "host"},
			    });
			projectCloser pc{projectSession};
			projectSession->DidOpenFile(ctx, "file:///host/index.ts", 1,
			                          "overlay",
			                          lsproto::LanguageKindTypeScript);
			projectSession->DidOpenFile(ctx, "file:///host/new.ts", 1,
			                          "new overlay",
			                          lsproto::LanguageKindTypeScript);
			auto session = NewLSPSession(projectSession, nullptr);
			sessionCloser c{session};

			CreateSnapshotParams params;
			auto fs = std::make_shared<rfs::RequestFileSystem>();
			fs->kind = kind;
			fs->Files = {
			    {"/host/index.ts", "request"},
			    {"/host/new.ts", "request"},
			};
			fs->Symlinks = std::map<std::string, rfs::RequestSymlink>{
			    {"/mounted",
			     rfs::RequestSymlink{.Target = "/host", .Host = true}},
			};
			params.FileSystem = fs;
			auto [base, err] =
			    updateCurrentLanguageServerSnapshot(ctx, session, params);
			assert::NilError(t, err);
			auto* snapshot =
			    snapshotAt(session.get(), base->Snapshot)->snapshot;
			auto [content, ok] = snapshot->ReadFile("/mounted/index.ts");
			assert::Assert(t, ok);
			assert::Equal(t, content, std::string("overlay"));
			auto [content2, ok2] = snapshot->ReadFile("/mounted/new.ts");
			assert::Assert(t, ok2);
			assert::Equal(t, content2, std::string("new overlay"));
		});
	}
}
REGISTER_UNIT_TEST("api.TestRequestHostMountReadsEditorOverlays",
                   TestRequestHostMountReadsEditorOverlays);

void TestCreateSnapshotUsesFullFileSystem(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/host.ts", "host"},
	    });
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto ctx = gostd::contextBackground();

	CreateSnapshotParams params;
	params.OpenProjects = {DocumentIdentifier{.FileName = "/tsconfig.json"}};
	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindFull;
	fs->Files = {
	    {"/tsconfig.json",
	     R"__rfs__({ "compilerOptions": { "noLib": true }, "files": ["src/index.ts"] })__rfs__"},
	    {"/src/index.ts", R"__rfs__(export const value = "memory";)__rfs__"},
	    {"/src/other.ts", R"__rfs__(export const other = true;)__rfs__"},
	};
	params.FileSystem = fs;
	auto [response, err] =
	    session->handleCreateSnapshot(ctx, &params);
	assert::NilError(t, err);
	assert::Equal(t, int(response->Projects.size()), 1);
	assert::Equal(t, response->Projects[0]->ConfigFileName,
	              std::string("/tsconfig.json"));

	auto* snapshot = snapshotAt(session.get(), response->Snapshot)->snapshot;
	auto [contents, ok] = snapshot->ReadFile("/src/index.ts");
	assert::Assert(t, ok);
	assert::Equal(t, contents, std::string(R"__rfs__(export const value = "memory";)__rfs__"));
	auto [_unused, ok2] = snapshot->ReadFile("/host.ts");
	assert::Assert(t, !ok2);

	// Carrying the same filesystem forward without a delta must preserve
	// incremental state instead of forcing a full program rebuild.
	auto* program = snapshot->ProjectCollection
	                    ->GetProject(project::ID("/tsconfig.json"))
	                    ->GetProgram();
	UpdateSnapshotParams unchangedParams;
	unchangedParams.Snapshot = response->Snapshot;
	auto [unchanged, err2] =
	    session->handleUpdateSnapshot(ctx, &unchangedParams);
	assert::NilError(t, err2);
	auto* unchangedSnapshot =
	    snapshotAt(session.get(), unchanged->Snapshot)->snapshot;
	assert::Assert(t, unchangedSnapshot->ProjectCollection
	                      ->GetProject(project::ID("/tsconfig.json"))
	                      ->GetProgram() == program);
	auto responseID = unchanged->Snapshot;

	// Supplying a new filesystem replaces inherited snapshot file caches even
	// when the caller does not redundantly list every file in FileChanges.
	auto fs2 = std::make_shared<rfs::RequestFileSystem>();
	fs2->kind = rfs::KindFull;
	fs2->Files = {
	    {"/tsconfig.json",
	     R"__rfs__({ "compilerOptions": { "noLib": true }, "files": ["src/index.ts", "src/other.ts"] })__rfs__"},
	    {"/src/index.ts", R"__rfs__(export const value = "updated";)__rfs__"},
	    {"/src/other.ts", R"__rfs__(export const other = true;)__rfs__"},
	};
	auto changes2 = std::make_shared<CreateSnapshotParams>();
	changes2->EnsurePrograms =
	    std::make_shared<EnsurePrograms>(EnsurePrograms{.All = true});
	changes2->FileSystem = fs2;
	UpdateSnapshotParams update2;
	update2.Snapshot = responseID;
	update2.Changes = changes2;
	auto [response2, err3] =
	    session->handleUpdateSnapshot(ctx, &update2);
	assert::NilError(t, err3);
	snapshot = snapshotAt(session.get(), response2->Snapshot)->snapshot;
	auto [contents2, ok3] = snapshot->ReadFile("/src/index.ts");
	assert::Assert(t, ok3);
	assert::Equal(t, contents2, std::string(R"__rfs__(export const value = "updated";)__rfs__"));

	// A new layer retains the base snapshot's supplied filesystem for every file
	// other than its override.
	auto fs3 = std::make_shared<rfs::RequestFileSystem>();
	fs3->kind = rfs::KindLayer;
	fs3->Files = {{"/src/index.ts", R"__rfs__(export const value = "temporary";)__rfs__"}};
	auto changes3 = std::make_shared<CreateSnapshotParams>();
	changes3->FileSystem = fs3;
	UpdateSnapshotParams update3;
	update3.Snapshot = response2->Snapshot;
	update3.Changes = changes3;
	auto [temporary, err4] =
	    session->handleUpdateSnapshot(ctx, &update3);
	assert::NilError(t, err4);
	auto* temporarySnapshot =
	    snapshotAt(session.get(), temporary->Snapshot)->snapshot;
	auto [contents3, ok4] = temporarySnapshot->ReadFile("/src/index.ts");
	assert::Assert(t, ok4);
	assert::Equal(t, contents3,
	              std::string(R"__rfs__(export const value = "temporary";)__rfs__"));
	auto [contents4, ok5] = temporarySnapshot->ReadFile("/src/other.ts");
	assert::Assert(t, ok5);
	assert::Equal(t, contents4, std::string(R"__rfs__(export const other = true;)__rfs__"));
}
REGISTER_UNIT_TEST("api.TestCreateSnapshotUsesFullFileSystem",
                   TestCreateSnapshotUsesFullFileSystem);

void TestUpdateSnapshotRequestFileOverridesOpenOverlay(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/index.ts", "host"},
	    });
	projectCloser pc{projectSession};
	projectSession->DidOpenFile(gostd::contextBackground(),
	                          "file:///index.ts", 1, "overlay",
	                          lsproto::LanguageKindTypeScript);

	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	CreateSnapshotParams params;
	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindLayer;
	fs->Files = {{"/index.ts", "request"}};
	params.FileSystem = fs;
	auto [response, err] = session->handleCreateSnapshot(
	    gostd::contextBackground(), &params);
	assert::NilError(t, err);

	auto* snapshot = snapshotAt(session.get(), response->Snapshot)->snapshot;
	auto [contents, ok] = snapshot->ReadFile("/index.ts");
	assert::Assert(t, ok);
	assert::Equal(t, contents, std::string("request"));
}
REGISTER_UNIT_TEST("api.TestUpdateSnapshotRequestFileOverridesOpenOverlay",
                   TestUpdateSnapshotRequestFileOverridesOpenOverlay);

void TestUpdateSnapshotRequestTombstoneRemovesOpenOverlay(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/index.ts", "host"},
	    });
	projectCloser pc{projectSession};
	projectSession->DidOpenFile(gostd::contextBackground(),
	                          "file:///index.ts", 1, "overlay",
	                          lsproto::LanguageKindTypeScript);

	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	CreateSnapshotParams params;
	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindLayer;
	fs->RemovedPaths = std::vector<std::string>{"/index.ts"};
	params.FileSystem = fs;
	auto [response, err] = session->handleCreateSnapshot(
	    gostd::contextBackground(), &params);
	assert::NilError(t, err);

	auto* snapshot = snapshotAt(session.get(), response->Snapshot)->snapshot;
	auto [_unused, ok] = snapshot->ReadFile("/index.ts");
	assert::Assert(t, !ok);
	assert::Assert(t,
	               snapshot->GetDefaultProject("file:///index.ts") ==
	                   nullptr);
}
REGISTER_UNIT_TEST("api.TestUpdateSnapshotRequestTombstoneRemovesOpenOverlay",
                   TestUpdateSnapshotRequestTombstoneRemovesOpenOverlay);

void TestUpdateSnapshotRequestTombstoneRemovesHostlessOpenOverlay(T* t) {
	t->Parallel();

	auto [projectSession, utils] =
	    projecttestutil::Setup(projecttestutil::FileMap{});
	projectCloser pc{projectSession};
	projectSession->DidOpenFile(gostd::contextBackground(),
	                          "file:///index.ts", 1, "overlay",
	                          lsproto::LanguageKindTypeScript);

	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	CreateSnapshotParams params;
	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindLayer;
	fs->RemovedPaths = std::vector<std::string>{"/index.ts"};
	params.FileSystem = fs;
	auto [response, err] = session->handleCreateSnapshot(
	    gostd::contextBackground(), &params);
	assert::NilError(t, err);

	auto* snapshot = snapshotAt(session.get(), response->Snapshot)->snapshot;
	auto [_unused, ok] = snapshot->ReadFile("/index.ts");
	assert::Assert(t, !ok);
	assert::Assert(t,
	               snapshot->GetDefaultProject("file:///index.ts") ==
	                   nullptr);
}
REGISTER_UNIT_TEST(
    "api.TestUpdateSnapshotRequestTombstoneRemovesHostlessOpenOverlay",
    TestUpdateSnapshotRequestTombstoneRemovesHostlessOpenOverlay);

void TestUpdateSnapshotRequestFileMasksHostlessOpenOverlayDirectory(T* t) {
	t->Parallel();

	auto [projectSession, utils] =
	    projecttestutil::Setup(projecttestutil::FileMap{});
	projectCloser pc{projectSession};
	projectSession->DidOpenFile(gostd::contextBackground(),
	                          "file:///src/index.ts", 1, "overlay",
	                          lsproto::LanguageKindTypeScript);

	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	CreateSnapshotParams params;
	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindLayer;
	fs->Files = {{"/src", "request"}};
	params.FileSystem = fs;
	auto [response, err] = session->handleCreateSnapshot(
	    gostd::contextBackground(), &params);
	assert::NilError(t, err);

	auto* snapshot = snapshotAt(session.get(), response->Snapshot)->snapshot;
	auto [contents, ok] = snapshot->ReadFile("/src");
	assert::Assert(t, ok);
	assert::Equal(t, contents, std::string("request"));
	auto [_unused, ok2] = snapshot->ReadFile("/src/index.ts");
	assert::Assert(t, !ok2);
	assert::Assert(t,
	               snapshot->GetDefaultProject("file:///src/index.ts") ==
	                   nullptr);
}
REGISTER_UNIT_TEST(
    "api.TestUpdateSnapshotRequestFileMasksHostlessOpenOverlayDirectory",
    TestUpdateSnapshotRequestFileMasksHostlessOpenOverlayDirectory);

void TestUpdateSnapshotRequestMaskUpdatesOpenConfiguredProjects(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/tsconfig.json",
	         R"__rfs__({ "compilerOptions": { "noLib": true }, "files": ["index.ts"] })__rfs__"},
	        {"/index.ts", "host"},
	    });
	projectCloser pc{projectSession};
	projectSession->DidOpenFile(gostd::contextBackground(),
	                          "file:///index.ts", 1, "overlay",
	                          lsproto::LanguageKindTypeScript);

	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto ctx = gostd::contextBackground();
	auto openChanges = std::make_shared<LanguageServerSnapshotChanges>();
	openChanges->OpenProjects = {DocumentIdentifier{.FileName = "/tsconfig.json"}};
	GetCurrentLanguageServerSnapshotParams baseParams;
	baseParams.Changes = openChanges;
	auto [base, err] =
	    session->handleGetCurrentLanguageServerSnapshot(ctx, &baseParams);
	assert::NilError(t, err);
	auto* baseSnapshot = snapshotAt(session.get(), base->Snapshot)->snapshot;
	assert::Assert(t, baseSnapshot->ProjectCollection
	                      ->GetOpenConfiguredProjects()
	                      ->Has(project::ConfiguredProjectID(
	                          tspath::Path("/tsconfig.json"))));

	auto maskFS = std::make_shared<rfs::RequestFileSystem>();
	maskFS->kind = rfs::KindLayer;
	maskFS->Files = {{"/index.ts", "request"}};
	auto maskChanges = std::make_shared<CreateSnapshotParams>();
	maskChanges->FileSystem = maskFS;
	UpdateSnapshotParams maskParams;
	maskParams.Snapshot = base->Snapshot;
	maskParams.Changes = maskChanges;
	auto [masked, err2] =
	    session->handleUpdateSnapshot(ctx, &maskParams);
	assert::NilError(t, err2);
	auto* maskedSnapshot =
	    snapshotAt(session.get(), masked->Snapshot)->snapshot;
	assert::Assert(t, !maskedSnapshot->ProjectCollection
	                      ->GetOpenConfiguredProjects()
	                      ->Has(project::ConfiguredProjectID(
	                          tspath::Path("/tsconfig.json"))));

	auto openChanges2 = std::make_shared<LanguageServerSnapshotChanges>();
	openChanges2->OpenProjects = {DocumentIdentifier{.FileName = "/tsconfig.json"}};
	GetCurrentLanguageServerSnapshotParams unmaskParams;
	unmaskParams.Changes = openChanges2;
	auto [unmasked, err3] =
	    session->handleGetCurrentLanguageServerSnapshot(ctx, &unmaskParams);
	assert::NilError(t, err3);
	auto* unmaskedSnapshot =
	    snapshotAt(session.get(), unmasked->Snapshot)->snapshot;
	assert::Assert(t, unmaskedSnapshot->ProjectCollection
	                      ->GetOpenConfiguredProjects()
	                      ->Has(project::ConfiguredProjectID(
	                          tspath::Path("/tsconfig.json"))));
}
REGISTER_UNIT_TEST("api.TestUpdateSnapshotRequestMaskUpdatesOpenConfiguredProjects",
                   TestUpdateSnapshotRequestMaskUpdatesOpenConfiguredProjects);

void TestUpdateSnapshotConfigChangeSkipsMaskedOpenOverlay(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/index.ts", "host"},
	    });
	projectCloser pc{projectSession};
	projectSession->DidOpenFile(gostd::contextBackground(),
	                          "file:///index.ts", 1, "overlay",
	                          lsproto::LanguageKindTypeScript);

	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto ctx = gostd::contextBackground();
	CreateSnapshotParams baseParams;
	auto [base, err] =
	    session->handleCreateSnapshot(ctx, &baseParams);
	assert::NilError(t, err);

	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindLayer;
	fs->Files = {
	    {"/tsconfig.json",
	     R"__rfs__({ "compilerOptions": { "noLib": true }, "files": ["index.ts"] })__rfs__"},
	};
	fs->RemovedPaths = std::vector<std::string>{"/index.ts"};
	auto changes = std::make_shared<CreateSnapshotParams>();
	changes->FileSystem = fs;
	UpdateSnapshotParams updateParams;
	updateParams.Snapshot = base->Snapshot;
	updateParams.Changes = changes;
	auto [updated, err2] =
	    session->handleUpdateSnapshot(ctx, &updateParams);
	assert::NilError(t, err2);

	auto* snapshot = snapshotAt(session.get(), updated->Snapshot)->snapshot;
	auto [_unused, ok] = snapshot->ReadFile("/index.ts");
	assert::Assert(t, !ok);
}
REGISTER_UNIT_TEST("api.TestUpdateSnapshotConfigChangeSkipsMaskedOpenOverlay",
                   TestUpdateSnapshotConfigChangeSkipsMaskedOpenOverlay);

void TestCreateProgramRetainsFullFileSystem(T* t) {
	t->Parallel();

	auto [projectSession, utils] =
	    projecttestutil::Setup(projecttestutil::FileMap{});
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};

	auto ctx = gostd::contextBackground();
	CreateSnapshotParams baseParams;
	baseParams.OpenFiles = {DocumentIdentifier{.FileName = "/old.ts"}};
	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindFull;
	fs->Files = {
	    {"/old.ts", R"__rfs__(export const oldValue = 1;)__rfs__"},
	    {"/new.ts", R"__rfs__(export const newValue = 2;)__rfs__"},
	};
	baseParams.FileSystem = fs;
	auto [base, err] =
	    session->handleCreateSnapshot(ctx, &baseParams);
	assert::NilError(t, err);
	assert::Equal(t, int(base->Projects.size()), 1);

	auto changes = std::make_shared<CreateSnapshotParams>();
	changes->CreatePrograms =
	    std::vector<std::shared_ptr<CreateSnapshotProgramParams>>{
	        std::make_shared<CreateSnapshotProgramParams>(
	            CreateSnapshotProgramParams{
	                .RootFiles = {DocumentIdentifier{.FileName = "/new.ts"}},
	                .CompilerOptions =
	                    tsc::CompilerOptions{.NoLib = Tristate::True},
	            }),
	    };
	UpdateSnapshotParams updateParams;
	updateParams.Snapshot = base->Snapshot;
	updateParams.Changes = changes;
	auto [created, err2] =
	    session->handleUpdateSnapshot(ctx, &updateParams);
	assert::NilError(t, err2);

	auto [snapshot, err3] = session->getSnapshotData(created->Snapshot);
	assert::NilError(t, err3);
	auto [program, err4] = snapshot->getProgram(
	    project::ID((*created->Operation->CreatedPrograms)[0]));
	assert::NilError(t, err4);
	assert::Assert(t, program->GetSourceFile("/new.ts") != nullptr);
}
REGISTER_UNIT_TEST("api.TestCreateProgramRetainsFullFileSystem",
                   TestCreateProgramRetainsFullFileSystem);

void TestSnapshotUpdateFullFileSystemIsTotal(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/host.ts", "host"},
	    });
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto ctx = gostd::contextBackground();

	CreateSnapshotParams baseParams;
	auto [base, err] =
	    session->handleCreateSnapshot(ctx, &baseParams);
	assert::NilError(t, err);
	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindFull;
	fs->Files = {{"/memory.ts", "memory"}};
	auto changes = std::make_shared<CreateSnapshotParams>();
	changes->FileSystem = fs;
	UpdateSnapshotParams updateParams;
	updateParams.Snapshot = base->Snapshot;
	updateParams.Changes = changes;
	auto [replaced, err2] =
	    session->handleUpdateSnapshot(ctx, &updateParams);
	assert::NilError(t, err2);

	auto* snapshot = snapshotAt(session.get(), replaced->Snapshot)->snapshot;
	auto [contents, ok] = snapshot->ReadFile("/memory.ts");
	assert::Assert(t, ok);
	assert::Equal(t, contents, std::string("memory"));
	auto [_unused, ok2] = snapshot->ReadFile("/host.ts");
	assert::Assert(t, !ok2);
}
REGISTER_UNIT_TEST("api.TestSnapshotUpdateFullFileSystemIsTotal",
                   TestSnapshotUpdateFullFileSystemIsTotal);

void TestSnapshotUpdateCarriesHostFileSystemWithoutOverride(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/tsconfig.json",
	         R"__rfs__({ "compilerOptions": { "noLib": true }, "files": ["index.ts"] })__rfs__"},
	        {"/index.ts", R"__rfs__(export const value = true;)__rfs__"},
	    });
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto ctx = gostd::contextBackground();

	CreateSnapshotParams baseParams;
	baseParams.OpenProjects = {DocumentIdentifier{.FileName = "/tsconfig.json"}};
	auto [base, err] =
	    session->handleCreateSnapshot(ctx, &baseParams);
	assert::NilError(t, err);
	auto* baseSnapshot = snapshotAt(session.get(), base->Snapshot)->snapshot;
	auto* program = baseSnapshot->ProjectCollection
	                    ->GetProject(project::ID("/tsconfig.json"))
	                    ->GetProgram();

	UpdateSnapshotParams updateParams;
	updateParams.Snapshot = base->Snapshot;
	auto [updated, err2] =
	    session->handleUpdateSnapshot(ctx, &updateParams);
	assert::NilError(t, err2);
	auto* updatedSnapshot =
	    snapshotAt(session.get(), updated->Snapshot)->snapshot;
	assert::Assert(t, updatedSnapshot->ProjectCollection
	                      ->GetProject(project::ID("/tsconfig.json"))
	                      ->GetProgram() == program);
}
REGISTER_UNIT_TEST("api.TestSnapshotUpdateCarriesHostFileSystemWithoutOverride",
                   TestSnapshotUpdateCarriesHostFileSystemWithoutOverride);

void TestSnapshotFileSystemLayersPreserveIncrementalState(T* t) {
	t->Parallel();

	for (const rfs::Kind& baseKind :
	     {rfs::Kind("host"), rfs::KindFull, rfs::KindLayer}) {
		std::string name = baseKind;
		t->Run(name, [baseKind](T* t) {
			t->Parallel();
			std::map<std::string, std::string> files{
			    {"/a/tsconfig.json",
			     R"__rfs__({ "compilerOptions": { "noLib": true }, "include": ["**/*.ts"] })__rfs__"},
			    {"/a/index.ts", R"__rfs__(export const value = 1;)__rfs__"},
			    {"/a/removed/nested.ts", R"__rfs__(export const nested = true;)__rfs__"},
			    {"/a/removed/deep/file.ts", R"__rfs__(export const deep = true;)__rfs__"},
			    {"/b/tsconfig.json",
			     R"__rfs__({ "compilerOptions": { "noLib": true }, "files": ["index.ts"] })__rfs__"},
			    {"/b/index.ts", R"__rfs__(export const unrelated = true;)__rfs__"},
			};
			projecttestutil::FileMap hostFiles;
			for (auto& [path, content] : files) {
				hostFiles[path] = content;
			}
			auto [projectSession, utils] = projecttestutil::Setup(hostFiles);
			projectCloser pc{projectSession};
			auto session = NewLSPSession(projectSession, nullptr);
			sessionCloser c{session};
			auto ctx = gostd::contextBackground();
			CreateSnapshotParams params;
			params.OpenProjects = {
			    DocumentIdentifier{.FileName = "/a/tsconfig.json"},
			    DocumentIdentifier{.FileName = "/b/tsconfig.json"},
			};
			if (baseKind != "host") {
				auto fs = std::make_shared<rfs::RequestFileSystem>();
				fs->kind = baseKind;
				fs->Files = files;
				params.FileSystem = fs;
			}
			auto [base, err] =
			    session->handleCreateSnapshot(ctx, &params);
			assert::NilError(t, err);
			auto* baseSnapshot =
			    snapshotAt(session.get(), base->Snapshot)->snapshot;
			auto* baseProgram = baseSnapshot->ProjectCollection
			                        ->GetProject(project::ID("/a/tsconfig.json"))
			                        ->GetProgram();
			auto* unrelatedProgram =
			    baseSnapshot->ProjectCollection
			        ->GetProject(project::ID("/b/tsconfig.json"))
			        ->GetProgram();
			auto* unrelatedFile = baseSnapshot->GetFile("/b/index.ts");

			auto unchangedFS = std::make_shared<rfs::RequestFileSystem>();
			unchangedFS->kind = rfs::KindLayer;
			unchangedFS->Files = {
			    {"/a/index.ts", files["/a/index.ts"]},
			};
			auto unchangedChanges =
			    std::make_shared<CreateSnapshotParams>();
			unchangedChanges->EnsurePrograms =
			    std::make_shared<EnsurePrograms>(
			        EnsurePrograms{.All = true});
			unchangedChanges->FileSystem = unchangedFS;
			UpdateSnapshotParams unchangedParams;
			unchangedParams.Snapshot = base->Snapshot;
			unchangedParams.Changes = unchangedChanges;
			auto [unchanged, err2] =
			    session->handleUpdateSnapshot(ctx, &unchangedParams);
			assert::NilError(t, err2);
			auto* unchangedSnapshot =
			    snapshotAt(session.get(), unchanged->Snapshot)->snapshot;
			assert::Assert(t, unchangedSnapshot->ProjectCollection
			                      ->GetProject(project::ID("/a/tsconfig.json"))
			                      ->GetProgram() == baseProgram);
			assert::Assert(t, unchangedSnapshot->ProjectCollection
			                      ->GetProject(project::ID("/b/tsconfig.json"))
			                      ->GetProgram() == unrelatedProgram);
			assert::Assert(t, unchangedSnapshot->GetFile("/b/index.ts") ==
			                      unrelatedFile);

			const std::string updatedText = R"__rfs__(export const value = 2;)__rfs__";
			auto updatedFS = std::make_shared<rfs::RequestFileSystem>();
			updatedFS->kind = rfs::KindLayer;
			updatedFS->Files = {{"/a/index.ts", updatedText}};
			auto updatedChanges = std::make_shared<CreateSnapshotParams>();
			updatedChanges->EnsurePrograms =
			    std::make_shared<EnsurePrograms>(
			        EnsurePrograms{.All = true});
			updatedChanges->FileSystem = updatedFS;
			UpdateSnapshotParams updatedParams;
			updatedParams.Snapshot = unchanged->Snapshot;
			updatedParams.Changes = updatedChanges;
			auto [updated, err3] =
			    session->handleUpdateSnapshot(ctx, &updatedParams);
			assert::NilError(t, err3);
			auto* updatedSnapshot =
			    snapshotAt(session.get(), updated->Snapshot)->snapshot;
			auto* updatedProject = updatedSnapshot->ProjectCollection
			                           ->GetProject(
			                               project::ID("/a/tsconfig.json"));
			assert::Assert(t, updatedProject->GetProgram() != baseProgram);
			assert::Equal(t, int(updatedProject->ProgramUpdateKind),
			              int(project::ProgramUpdateKindCloned));
			assert::Equal(t,
			              updatedProject->GetProgram()
			                  ->GetSourceFile("/a/index.ts")
			                  ->Text(),
			              updatedText);
			assert::Assert(t, updatedSnapshot->ProjectCollection
			                      ->GetProject(project::ID("/b/tsconfig.json"))
			                      ->GetProgram() == unrelatedProgram);
			assert::Assert(t, updatedSnapshot->GetFile("/b/index.ts") ==
			                      unrelatedFile);

			auto removedFS = std::make_shared<rfs::RequestFileSystem>();
			removedFS->kind = rfs::KindLayer;
			removedFS->RemovedPaths =
			    std::vector<std::string>{"/a/removed"};
			auto removedChanges = std::make_shared<CreateSnapshotParams>();
			removedChanges->EnsurePrograms =
			    std::make_shared<EnsurePrograms>(
			        EnsurePrograms{.All = true});
			removedChanges->FileSystem = removedFS;
			UpdateSnapshotParams removedParams;
			removedParams.Snapshot = updated->Snapshot;
			removedParams.Changes = removedChanges;
			auto [removed, err4] =
			    session->handleUpdateSnapshot(ctx, &removedParams);
			assert::NilError(t, err4);
			auto* removedSnapshot =
			    snapshotAt(session.get(), removed->Snapshot)->snapshot;
			auto* removedProgram = removedSnapshot->ProjectCollection
			                           ->GetProject(
			                               project::ID("/a/tsconfig.json"))
			                           ->GetProgram();
			for (auto& path :
			     {std::string("/a/removed/nested.ts"),
			      std::string("/a/removed/deep/file.ts")}) {
				assert::Assert(t,
				               removedProgram->GetSourceFile(path) ==
				                   nullptr,
				               path);
				assert::Assert(t,
				               removedSnapshot->GetFile(path) == nullptr,
				               path);
				assert::Assert(t,
				               baseProgram->GetSourceFile(path) != nullptr,
				               path);
			}
			assert::Assert(t, removedSnapshot->ProjectCollection
			                      ->GetProject(project::ID("/b/tsconfig.json"))
			                      ->GetProgram() == unrelatedProgram);
			assert::Assert(t, removedSnapshot->GetFile("/b/index.ts") ==
			                      unrelatedFile);

			// A request without a base snapshot returns to the host, so the old
			// layer's changed contents and directory tombstones must not survive.
			CreateSnapshotParams restoreParams;
			static_cast<SnapshotRequestChangesParams&>(restoreParams) =
			    static_cast<const SnapshotRequestChangesParams&>(params);
			auto [restored, err5] =
			    session->handleCreateSnapshot(ctx, &restoreParams);
			assert::NilError(t, err5);
			auto* restoredSnapshot =
			    snapshotAt(session.get(), restored->Snapshot)->snapshot;
			assert::Assert(t, !restoredSnapshot->HasFileSystemOverride());
			auto* restoredProgram =
			    restoredSnapshot->ProjectCollection
			        ->GetProject(project::ID("/a/tsconfig.json"))
			        ->GetProgram();
			assert::Equal(t,
			              restoredProgram->GetSourceFile("/a/index.ts")
			                  ->Text(),
			              files["/a/index.ts"]);
			assert::Assert(t,
			               restoredProgram->GetSourceFile(
			                   "/a/removed/deep/file.ts") != nullptr);
		});
	}
}
REGISTER_UNIT_TEST("api.TestSnapshotFileSystemLayersPreserveIncrementalState",
                   TestSnapshotFileSystemLayersPreserveIncrementalState);

void TestSnapshotFileSystemLayerWithoutBaseUpdatesHostState(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/tsconfig.json",
	         R"__rfs__({ "compilerOptions": { "noLib": true }, "files": ["index.ts"] })__rfs__"},
	        {"/index.ts", R"__rfs__(export const value = 1;)__rfs__"},
	    });
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto ctx = gostd::contextBackground();
	CreateSnapshotParams openParams;
	openParams.OpenProjects = {DocumentIdentifier{.FileName = "/tsconfig.json"}};
	auto [_unusedResp, err] =
	    session->handleCreateSnapshot(ctx, &openParams);
	assert::NilError(t, err);

	const std::string updatedText = R"__rfs__(export const value = 2;)__rfs__";
	CreateSnapshotParams updateParams;
	updateParams.OpenProjects = {DocumentIdentifier{.FileName = "/tsconfig.json"}};
	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindLayer;
	fs->Files = {{"/index.ts", updatedText}};
	updateParams.FileSystem = fs;
	auto [updated, err2] =
	    session->handleCreateSnapshot(ctx, &updateParams);
	assert::NilError(t, err2);
	auto* snapshot = snapshotAt(session.get(), updated->Snapshot)->snapshot;
	auto* updatedProject = snapshot->ProjectCollection->GetProject(
	    project::ID("/tsconfig.json"));
	assert::Equal(t,
	              updatedProject->GetProgram()
	                  ->GetSourceFile("/index.ts")
	                  ->Text(),
	              updatedText);
	assert::Equal(t, int(updatedProject->ProgramUpdateKind),
	              int(project::ProgramUpdateKindNewFiles));
}
REGISTER_UNIT_TEST("api.TestSnapshotFileSystemLayerWithoutBaseUpdatesHostState",
                   TestSnapshotFileSystemLayerWithoutBaseUpdatesHostState);

void TestEmitFromLayerOverFullFileSystemReturnsFileContents(T* t) {
	t->Parallel();

	auto [projectSession, utils] =
	    projecttestutil::Setup(projecttestutil::FileMap{});
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto ctx = gostd::contextBackground();

	CreateSnapshotParams baseParams;
	baseParams.OpenProjects = {DocumentIdentifier{.FileName = "/tsconfig.json"}};
	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindFull;
	fs->Files = {
	    {"/tsconfig.json",
	     R"__rfs__({ "compilerOptions": { "noLib": true, "outDir": "/out" }, "files": ["src/main.ts"] })__rfs__"},
	    {"/src/main.ts", R"__rfs__(export const value: number = 1;)__rfs__"},
	};
	baseParams.FileSystem = fs;
	auto [base, err] =
	    session->handleCreateSnapshot(ctx, &baseParams);
	assert::NilError(t, err);
	auto layerFS = std::make_shared<rfs::RequestFileSystem>();
	layerFS->kind = rfs::KindLayer;
	layerFS->Files = {};
	auto layerChanges = std::make_shared<CreateSnapshotParams>();
	layerChanges->FileSystem = layerFS;
	UpdateSnapshotParams layerParams;
	layerParams.Snapshot = base->Snapshot;
	layerParams.Changes = layerChanges;
	auto [layered, err2] =
	    session->handleUpdateSnapshot(ctx, &layerParams);
	assert::NilError(t, err2);
	assert::Equal(t, int(layered->Projects.size()), 0);

	EmitParams emitParams;
	emitParams.Snapshot = layered->Snapshot;
	emitParams.Project = base->Projects[0]->Id;
	auto [emitted, err3] = session->handleEmit(ctx, &emitParams);
	assert::NilError(t, err3);
	assert::DeepEqual(t, emitted->EmittedFiles,
	                  std::vector<std::string>({"/out/src/main.js"}));
	assert::DeepEqual(t, emitted->EmittedFilesContents,
	                  std::vector<std::string>({"export const value = 1;\n"}));

	ReleaseParams release;
	release.Snapshot = base->Snapshot;
	auto [_rv, err4] = session->handleRelease(ctx, &release);
	assert::NilError(t, err4);
	auto [emittedAfterRelease, err5] =
	    session->handleEmit(ctx, &emitParams);
	assert::NilError(t, err5);
	assert::DeepEqual(t, emittedAfterRelease->EmittedFiles,
	                  emitted->EmittedFiles);
	assert::DeepEqual(t, emittedAfterRelease->EmittedFilesContents,
	                  emitted->EmittedFilesContents);
}
REGISTER_UNIT_TEST("api.TestEmitFromLayerOverFullFileSystemReturnsFileContents",
                   TestEmitFromLayerOverFullFileSystemReturnsFileContents);

void TestReleaseSnapshotCompactsSoleLayeredFileSystem(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/host.ts", "host"},
	    });
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto ctx = gostd::contextBackground();

	CreateSnapshotParams baseParams;
	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindFull;
	fs->Files = {
	    {"/inherited.ts", "inherited"},
	    {"/changed.ts", "old"},
	    {"/removed.ts", "removed"},
	};
	baseParams.FileSystem = fs;
	auto [base, err] =
	    session->handleCreateSnapshot(ctx, &baseParams);
	assert::NilError(t, err);
	auto baseFileSystem =
	    snapshotAt(session.get(), base->Snapshot)->fileSystem;
	assert::Assert(t, baseFileSystem != nullptr);

	auto layerFS = std::make_shared<rfs::RequestFileSystem>();
	layerFS->kind = rfs::KindLayer;
	layerFS->Files = {
	    {"/changed.ts", "new"},
	    {"/added.ts", "added"},
	};
	layerFS->RemovedPaths = std::vector<std::string>{"/removed.ts"};
	auto layerChanges = std::make_shared<CreateSnapshotParams>();
	layerChanges->FileSystem = layerFS;
	UpdateSnapshotParams layerParams;
	layerParams.Snapshot = base->Snapshot;
	layerParams.Changes = layerChanges;
	auto [layered, err2] =
	    session->handleUpdateSnapshot(ctx, &layerParams);
	assert::NilError(t, err2);
	auto* layeredSnapshotData =
	    snapshotAt(session.get(), layered->Snapshot);
	auto* layeredSnapshot = layeredSnapshotData->snapshot;
	auto layeredFileSystem = layeredSnapshotData->fileSystem;
	assert::Assert(t, layeredFileSystem != nullptr);
	assert::Equal(t,
	              snapshotAt(session.get(), base->Snapshot)->refCount, 1);

	ReleaseParams release;
	release.Snapshot = base->Snapshot;
	auto [_rv, err3] = session->handleRelease(ctx, &release);
	assert::NilError(t, err3);
	assert::Assert(t, snapshotAt(session.get(), base->Snapshot) == nullptr);

	for (auto& [path, expected] : std::map<std::string, std::string>{
	         {"/inherited.ts", "inherited"},
	         {"/changed.ts", "new"},
	         {"/added.ts", "added"},
	     }) {
		auto [contents, readOK] = layeredSnapshot->ReadFile(path);
		assert::Assert(t, readOK, path);
		assert::Equal(t, contents, expected);
	}
	auto [_c1, ok] = layeredSnapshot->ReadFile("/removed.ts");
	assert::Assert(t, !ok);
	auto [_c2, ok2] = layeredSnapshot->ReadFile("/host.ts");
	assert::Assert(t, !ok2);
	assert::Assert(t, rfs::HasFullFileSystem(layeredFileSystem));
}
REGISTER_UNIT_TEST("api.TestReleaseSnapshotCompactsSoleLayeredFileSystem",
                   TestReleaseSnapshotCompactsSoleLayeredFileSystem);

void TestEagerSnapshotReleaseDoesNotRetainFileSystemHistory(T* t) {
	t->Parallel();

	auto [projectSession, utils] =
	    projecttestutil::Setup(projecttestutil::FileMap{});
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto ctx = gostd::contextBackground();

	CreateSnapshotParams params;
	auto fs = std::make_shared<rfs::RequestFileSystem>();
	fs->kind = rfs::KindFull;
	fs->Files = {{"/pkg/index.ts", ""}};
	params.FileSystem = fs;
	auto [response, err] =
	    session->handleCreateSnapshot(ctx, &params);
	assert::NilError(t, err);

	std::string content;
	for (char character : std::string("export const x = 1")) {
		SnapshotID oldSnapshot = response->Snapshot;
		content += std::string(1, character);
		auto layerFS = std::make_shared<rfs::RequestFileSystem>();
		layerFS->kind = rfs::KindLayer;
		layerFS->Files = {{"/pkg/index.ts", content}};
		auto changes = std::make_shared<CreateSnapshotParams>();
		changes->FileSystem = layerFS;
		UpdateSnapshotParams updateParams;
		updateParams.Snapshot = oldSnapshot;
		updateParams.Changes = changes;
		auto [updated, err2] =
		    session->handleUpdateSnapshot(ctx, &updateParams);
		assert::NilError(t, err2);
		response = std::move(updated);

		ReleaseParams release;
		release.Snapshot = oldSnapshot;
		auto [_rv, err3] = session->handleRelease(ctx, &release);
		assert::NilError(t, err3);

		assert::Equal(t, int(session->snapshots.size()), 1);
		auto* current = snapshotAt(session.get(), response->Snapshot);
		assert::Assert(t, current != nullptr);
		assert::Equal(t, current->refCount, 1);
		auto fileSystem = current->fileSystem;
		assert::Assert(t, fileSystem != nullptr);
		assert::Assert(t, rfs::HasFullFileSystem(fileSystem));
		auto [actual, ok] = current->snapshot->ReadFile("/pkg/index.ts");
		assert::Assert(t, ok);
		assert::Equal(t, actual, content);
	}
}
REGISTER_UNIT_TEST("api.TestEagerSnapshotReleaseDoesNotRetainFileSystemHistory",
                   TestEagerSnapshotReleaseDoesNotRetainFileSystemHistory);

void TestSnapshotReleaseCompactsChainedFileSystems(T* t) {
	t->Parallel();

	auto [projectSession, utils] =
	    projecttestutil::Setup(projecttestutil::FileMap{});
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto ctx = gostd::contextBackground();

	std::vector<std::unique_ptr<CreateSnapshotResponse>> responses(4);
	{
		CreateSnapshotParams params;
		auto fs = std::make_shared<rfs::RequestFileSystem>();
		fs->kind = rfs::KindFull;
		fs->Files = {{"/pkg/index.ts", "0"}};
		params.FileSystem = fs;
		auto [r0, err] =
		    session->handleCreateSnapshot(ctx, &params);
		assert::NilError(t, err);
		responses[0] = std::move(r0);
	}
	for (int i = 1; i < int(responses.size()); i++) {
		auto fs = std::make_shared<rfs::RequestFileSystem>();
		fs->kind = rfs::KindLayer;
		fs->Files = {{"/pkg/index.ts", std::to_string(i)}};
		auto changes = std::make_shared<CreateSnapshotParams>();
		changes->FileSystem = fs;
		UpdateSnapshotParams updateParams;
		updateParams.Snapshot = responses[i - 1]->Snapshot;
		updateParams.Changes = changes;
		auto [r, err2] =
		    session->handleUpdateSnapshot(ctx, &updateParams);
		assert::NilError(t, err2);
		responses[i] = std::move(r);
	}

	ReleaseParams release;
	release.Snapshot = responses[0]->Snapshot;
	auto [_rv, err] = session->handleRelease(ctx, &release);
	assert::NilError(t, err);
	assert::Assert(t,
	               snapshotAt(session.get(), responses[0]->Snapshot) ==
	                   nullptr);

	for (int i = 1; i < int(responses.size()); i++) {
		auto* current =
		    snapshotAt(session.get(), responses[i]->Snapshot);
		assert::Assert(t, current != nullptr);
		assert::Equal(t, current->refCount, 1);
		auto fileSystem = current->fileSystem;
		assert::Assert(t, fileSystem != nullptr);
		assert::Assert(t, rfs::HasFullFileSystem(fileSystem));
		auto [contents, ok] =
		    current->snapshot->ReadFile("/pkg/index.ts");
		assert::Assert(t, ok);
		assert::Equal(t, contents, std::to_string(i));
	}
}
REGISTER_UNIT_TEST("api.TestSnapshotReleaseCompactsChainedFileSystems",
                   TestSnapshotReleaseCompactsChainedFileSystems);

void TestTemporarySnapshotRetainsLayeredFileSystemHistory(T* t) {
	t->Parallel();

	auto [projectSession, utils] =
	    projecttestutil::Setup(projecttestutil::FileMap{});
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto ctx = gostd::contextBackground();

	auto makeLayer = [](const std::string& content) {
		auto fs = std::make_shared<rfs::RequestFileSystem>();
		fs->kind = rfs::KindLayer;
		fs->Files = {{"/pkg/index.ts", content}};
		return fs;
	};

	CreateSnapshotParams baseParams;
	auto baseFS = std::make_shared<rfs::RequestFileSystem>();
	baseFS->kind = rfs::KindFull;
	baseFS->Files = {{"/pkg/index.ts", "base"}};
	baseParams.FileSystem = baseFS;
	auto [base, err] =
	    session->handleCreateSnapshot(ctx, &baseParams);
	assert::NilError(t, err);
	auto layerChanges = std::make_shared<CreateSnapshotParams>();
	layerChanges->FileSystem = makeLayer("layered");
	UpdateSnapshotParams layerParams;
	layerParams.Snapshot = base->Snapshot;
	layerParams.Changes = layerChanges;
	auto [layered, err2] =
	    session->handleUpdateSnapshot(ctx, &layerParams);
	assert::NilError(t, err2);
	auto tempChanges = std::make_shared<CreateSnapshotParams>();
	tempChanges->FileSystem = makeLayer("temporary");
	UpdateSnapshotParams tempParams;
	tempParams.Snapshot = layered->Snapshot;
	tempParams.Changes = tempChanges;
	auto [temporary, err3] =
	    session->handleUpdateSnapshot(ctx, &tempParams);
	assert::NilError(t, err3);

	ReleaseParams release;
	release.Snapshot = layered->Snapshot;
	auto [_rv1, err4] = session->handleRelease(ctx, &release);
	assert::NilError(t, err4);
	release.Snapshot = base->Snapshot;
	auto [_rv2, err5] = session->handleRelease(ctx, &release);
	assert::NilError(t, err5);

	auto* current = snapshotAt(session.get(), temporary->Snapshot);
	assert::Assert(t, current != nullptr);
	auto fileSystem = current->fileSystem;
	assert::Assert(t, fileSystem != nullptr);
	assert::Assert(t, rfs::HasFullFileSystem(fileSystem));
	auto [contents, ok] =
	    current->snapshot->ReadFile("/pkg/index.ts");
	assert::Assert(t, ok);
	assert::Equal(t, contents, std::string("temporary"));
}
REGISTER_UNIT_TEST("api.TestTemporarySnapshotRetainsLayeredFileSystemHistory",
                   TestTemporarySnapshotRetainsLayeredFileSystemHistory);

void TestSnapshotReleaseCompactionSupportsConcurrentReaders(T* t) {
	t->Parallel();

	auto [projectSession, utils] =
	    projecttestutil::Setup(projecttestutil::FileMap{});
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto ctx = gostd::contextBackground();

	std::map<std::string, std::string> files;
	for (int index = 0; index < 1024; index++) {
		files["/pkg/file" + std::to_string(index) + ".ts"] =
		    std::to_string(index);
	}
	CreateSnapshotParams baseParams;
	auto baseFS = std::make_shared<rfs::RequestFileSystem>();
	baseFS->kind = rfs::KindFull;
	baseFS->Files = files;
	baseParams.FileSystem = baseFS;
	auto [base, err] =
	    session->handleCreateSnapshot(ctx, &baseParams);
	assert::NilError(t, err);
	auto layerFS = std::make_shared<rfs::RequestFileSystem>();
	layerFS->kind = rfs::KindLayer;
	layerFS->Files = {{"/pkg/file0.ts", "updated"}};
	auto layerChanges = std::make_shared<CreateSnapshotParams>();
	layerChanges->FileSystem = layerFS;
	UpdateSnapshotParams layerParams;
	layerParams.Snapshot = base->Snapshot;
	layerParams.Changes = layerChanges;
	auto [layered, err2] =
	    session->handleUpdateSnapshot(ctx, &layerParams);
	assert::NilError(t, err2);
	auto fileSystem =
	    snapshotAt(session.get(), layered->Snapshot)->fileSystem;

	std::atomic<bool> started{false};
	std::atomic<bool> done{false};
	std::mutex readerErrorMu;
	std::string readerError;
	std::thread reader([&] {
		started.store(true);
		while (!done.load()) {
			auto [contents, ok] = fileSystem->ReadFile("/pkg/file0.ts");
			if (!ok || contents != "updated") {
				std::lock_guard lk(readerErrorMu);
				readerError = "unexpected overridden file";
				return;
			}
			if (!fileSystem->FileExists("/pkg/file1023.ts")) {
				std::lock_guard lk(readerErrorMu);
				readerError = "inherited file disappeared";
				return;
			}
		}
	});
	while (!started.load()) {
		std::this_thread::yield();
	}
	ReleaseParams release;
	release.Snapshot = base->Snapshot;
	auto [_rv, err3] = session->handleRelease(ctx, &release);
	assert::NilError(t, err3);
	done.store(true);
	reader.join();
	assert::Equal(t, readerError, std::string());
}
REGISTER_UNIT_TEST("api.TestSnapshotReleaseCompactionSupportsConcurrentReaders",
                   TestSnapshotReleaseCompactionSupportsConcurrentReaders);

} // namespace
} // namespace tsc::api
