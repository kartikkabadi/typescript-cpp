// Port of tsc/internal/project/snapshot_test.go (internal package test).
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/ls/ls.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/project/compilerhost.h"
#include "internal/project/filechange.h"
#include "internal/project/project.h"
#include "internal/project/session.h"
#include "internal/project/sessiontypes.h"
#include "internal/project/snapshot.h"
#include "internal/project/snapshotfs.h"
#include "internal/project/snapshothost.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace collections = tsc::collections;
namespace gostd = tsc::gostd;
namespace lsproto = tsc::lsp::lsproto;
namespace project = tsc::project;
namespace tspath = tsc::tspath;
namespace vfstest = tsc::vfs::vfstest;
using tsc::gostd::testing::T;

void TestSnapshot(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto setup = [](const std::unordered_map<
	                std::string, vfstest::MapFileInput>& files)
	    -> project::Session* {
		auto fs = tsc::bundled::WrapFS(vfstest::FromMap(
		    files, false /*useCaseSensitiveFileNames*/));
		project::SessionInit init;
		init.BackgroundCtx = gostd::contextBackground();
		auto options = std::make_shared<project::SessionOptions>();
		options->CurrentDirectory = "/";
		options->DefaultLibraryPath = tsc::bundled::LibPath();
		options->TypingsLocation =
		    "/home/src/Library/Caches/typescript";
		options->PositionEncoding =
		    lsproto::PositionEncodingKindUTF8;
		options->WatchEnabled = false;
		options->LoggingEnabled = false;
		init.Options = options.get();
		init.FS = fs;
		init.KeepAlive = {fs, options};
		auto* session = project::NewSession(&init);
		return session;
	};

	t->Run("creates and removes synthetic programs", [&](T* t) {
		t->Parallel();
		auto* session = setup({
		    {"/a.ts", "export const a = 1;"},
		    {"/b.ts", "export const b = 1;"},
		});
		t->Cleanup([session] { session->Close(); });

		auto ctx = t->Context();
		auto* options = new tsc::CompilerOptions();
		options->NoLib = tsc::Tristate::True;
		project::APICreateProgramRequest reqA;
		reqA.RootFileNames = {"/a.ts"};
		reqA.CompilerOptions = options;
		project::APICreateProgramRequest reqB;
		reqB.RootFileNames = {"/b.ts"};
		reqB.CompilerOptions = options;
		project::APISnapshotRequest createRequest;
		createRequest.CreatePrograms = {&reqA, &reqB};
		auto createPair = session->snapshotHost->CloneSnapshot(
		    ctx, session->Snapshot(), project::FileChangeSummary{},
		    &createRequest);
		auto* createdSnapshot = createPair.first;
		auto& err = createPair.second;
		assert::NilError(t, err);
		t->Cleanup([createdSnapshot] { createdSnapshot->Deref(); });
		auto& createdPrograms = createdSnapshot->CreatedPrograms();
		assert::Equal(
		    t, static_cast<int64_t>(createdPrograms.size()),
		    int64_t(2));
		auto* firstProject = createdPrograms[0];
		auto* secondProject = createdPrograms[1];
		assert::Equal(t, firstProject->configFilePath,
		              tspath::Path(""));
		assert::Equal(t, secondProject->configFilePath,
		              tspath::Path(""));

		auto [firstProgramID, ok] =
		    project::idSynthetic(firstProject->ID());
		assert::Assert(t, ok);
		assert::Equal(t, std::string(firstProgramID),
		              std::string("/dev/null/synthetic/1"));
		auto removeSet = collections::NewSetFromItems<project::SyntheticProjectID>(firstProgramID);
		project::APISnapshotRequest removeRequest;
		removeRequest.RemovePrograms = &removeSet;
		auto removePair = session->snapshotHost->CloneSnapshot(
		    ctx, createdSnapshot, project::FileChangeSummary{},
		    &removeRequest);
		auto* removedSnapshot = removePair.first;
		auto& err2 = removePair.second;
		assert::NilError(t, err2);
		t->Cleanup([removedSnapshot] { removedSnapshot->Deref(); });

		assert::Assert(t, firstProject != nullptr);
		assert::Assert(t, secondProject != nullptr);
		assert::Assert(t, firstProject->ID() != secondProject->ID());
		assert::DeepEqual(t,
		                  firstProject->CommandLine->FileNames(),
		                  std::vector<std::string>{"/a.ts"});
		assert::DeepEqual(t,
		                  secondProject->CommandLine->FileNames(),
		                  std::vector<std::string>{"/b.ts"});
		assert::Assert(
		    t,
		    createdSnapshot->ProjectCollection->InferredProject() ==
		        nullptr);
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        createdSnapshot->ProjectCollection
		            ->SyntheticProjects()
		            .size()),
		    int64_t(2));
		assert::Equal(
		    t,
		    static_cast<int64_t>(createdSnapshot->ProjectCollection
		                             ->LanguageServiceProjects()
		                             .size()),
		    int64_t(0));
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        createdSnapshot
		            ->GetLanguageServiceProjectsContainingFile(
		                lsproto::DocumentUri("file:///a.ts"))
		            .size()),
		    int64_t(0));
		assert::Assert(
		    t,
		    createdSnapshot->ProjectCollection->GetDefaultProject(
		        createdSnapshot->toPath("/a.ts")) == nullptr);
		assert::Equal(
		    t,
		    createdSnapshot->ProjectCollection->GetProject(
		        firstProject->ID()),
		    firstProject);

		std::unordered_map<tspath::Path, std::string> openFilesMap{
		    {createdSnapshot->toPath("/a.ts"), "/a.ts"},
		};
		project::APISnapshotRequest openRequest;
		openRequest.OpenFiles = &openFilesMap;
		auto openPair = session->snapshotHost->CloneSnapshot(
		    ctx, createdSnapshot, project::FileChangeSummary{},
		    &openRequest);
		auto* openedSnapshot = openPair.first;
		auto& err3 = openPair.second;
		assert::NilError(t, err3);
		t->Cleanup([openedSnapshot] { openedSnapshot->Deref(); });
		auto* inferredProject =
		    openedSnapshot->ProjectCollection->InferredProject();
		assert::Assert(t, inferredProject != nullptr);
		auto [inferredID, ok2] =
		    project::idInferred(inferredProject->ID());
		assert::Assert(t, ok2);
		assert::Equal(t, inferredProject->configFilePath,
		              tspath::Path(""));
		assert::Equal(
		    t,
		    static_cast<int64_t>(openedSnapshot->ProjectCollection
		                             ->LanguageServiceProjects()
		                             .size()),
		    int64_t(1));
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        openedSnapshot
		            ->GetLanguageServiceProjectsContainingFile(
		                lsproto::DocumentUri("file:///a.ts"))
		            .size()),
		    int64_t(1));
		assert::Equal(
		    t,
		    openedSnapshot->ProjectCollection->GetDefaultProject(
		        openedSnapshot->toPath("/a.ts")),
		    openedSnapshot->ProjectCollection->InferredProject());
		assert::Equal(
		    t,
		    openedSnapshot->ProjectCollection->GetProject(
		        firstProject->ID()),
		    firstProject);

		assert::Assert(
		    t,
		    removedSnapshot->ProjectCollection->GetProject(
		        firstProject->ID()) == nullptr);
		assert::Equal(
		    t,
		    removedSnapshot->ProjectCollection->GetProject(
		        secondProject->ID()),
		    secondProject);
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        removedSnapshot->ProjectCollection
		            ->SyntheticProjects()
		            .size()),
		    int64_t(1));
	});

	t->Run("failed API update is not adopted", [&](T* t) {
		t->Parallel();
		auto* session = setup({
		    {"/a.ts", "export const a = 1;"},
		});
		t->Cleanup([session] { session->Close(); });

		auto* baseSnapshot = session->Snapshot();
		auto removeSet = collections::NewSetFromItems<project::SyntheticProjectID>(
		    project::NewSyntheticProjectID(1));
		project::APISnapshotRequest removeRequest;
		removeRequest.RemovePrograms = &removeSet;
		auto failedPair = session->snapshotHost->CloneSnapshot(
		    t->Context(), baseSnapshot, project::FileChangeSummary{},
		    &removeRequest);
		auto* failedSnapshot = failedPair.first;
		auto& err = failedPair.second;
		if (failedSnapshot != nullptr) {
			t->Cleanup(
			    [failedSnapshot] { failedSnapshot->Deref(); });
		}

		assert::ErrorContains(
		    t, err, "synthetic program not found for removal");
		assert::Equal(t, session->Snapshot(), baseSnapshot);
		// Go: assert the clone on an API-failed snapshot panics
		// (recovered). The C++ panic model (tscUnreachable) exits the
		// process with code 2, so the check runs in a forked child.
		bool panicked = false;
		{
			fflush(nullptr);
			pid_t pid = fork();
			if (pid == 0) {
				auto pair2 = session->snapshotHost->CloneSnapshot(
				    t->Context(), failedSnapshot,
				    project::FileChangeSummary{}, nullptr);
				(void)pair2;
				fflush(nullptr);
				_exit(0);
			}
			int status = 0;
			waitpid(pid, &status, 0);
			panicked = WIFEXITED(status) && WEXITSTATUS(status) == 2;
		}
		assert::Assert(t, panicked);
	});

	t->Run("failed API update preserves flushed host changes",
	       [&](T* t) {
		    t->Parallel();
		    auto* session = setup({
		        {"/a.ts", "export const a = 1;"},
		    });
		    t->Cleanup([session] { session->Close(); });

		    auto* baseSnapshot = session->Snapshot();
		    session->pendingFileChangesMu.lock();
		    session->pendingFileChanges.push_back(
		        project::FileChange{
		            .Kind =
		                project::FileChangeKindWatchChange,
		            .URI =
		                lsproto::DocumentUri("file:///a.ts"),
		        });
		    session->pendingFileChangesMu.unlock();
		    auto removeSet =
		        collections::NewSetFromItems<project::SyntheticProjectID>(
		            project::NewSyntheticProjectID(1));
		    project::APISnapshotRequest removeRequest;
		    removeRequest.RemovePrograms = &removeSet;
		    auto failedPair = session->APIUpdate(
		        t->Context(), project::FileChangeSummary{},
		        &removeRequest);
		    auto* failedSnapshot = failedPair.first;
		    auto& err = failedPair.second;

		    assert::ErrorContains(
		        t, err, "synthetic program not found for removal");
		    assert::Assert(t, failedSnapshot == nullptr);
		    assert::Assert(t,
		                   session->Snapshot() != baseSnapshot);
	    });

	t->Run("compilerHost gets frozen with snapshot's FS only once",
	       [&](T* t) {
		    t->Parallel();
		    std::unordered_map<std::string, vfstest::MapFileInput>
		        files{
		            {"/home/projects/TS/p1/tsconfig.json", "{}"},
		            {"/home/projects/TS/p1/index.ts",
		             "console.log('Hello, world!');"},
		        };
		    auto* session = setup(files);
		    t->Cleanup([session] { session->Close(); });
		    session->DidOpenFile(
		        t->Context(),
		        "file:///home/projects/TS/p1/index.ts", 1,
		        "console.log('Hello, world!');",
		        lsproto::LanguageKindTypeScript);
		    session->DidOpenFile(t->Context(), "untitled:Untitled-1",
		                         1, "",
		                         lsproto::LanguageKindTypeScript);
		    auto* snapshotBefore = session->Snapshot();

		    {
			    lsproto::
			        TextDocumentContentChangePartialOrWholeDocument
			            change;
			    change.Partial = std::make_shared<
			        lsproto::
			            TextDocumentContentChangePartial>();
			    change.Partial->Text = "\n";
			    change.Partial->Range = lsproto::Range{
			        .Start =
			            lsproto::Position{0, 24},
			        .End = lsproto::Position{0, 24},
			    };
			    session->DidChangeFile(
			        t->Context(),
			        "file:///home/projects/TS/p1/index.ts", 2,
			        {change});
		    }
		    auto [ls, err] = session->GetLanguageService(
		        t->Context(),
		        "file:///home/projects/TS/p1/index.ts");
		    assert::NilError(t, err);
		    delete ls;
		    auto* snapshotAfter = session->Snapshot();

		    // Configured project was updated by a clone
		    assert::Equal(
		        t,
		        snapshotAfter->ProjectCollection
		            ->ConfiguredProject(
		                tspath::Path(
		                    "/home/projects/ts/p1/tsconfig.json"))
		            ->ProgramUpdateKind,
		        project::ProgramUpdateKindCloned);
		    // Inferred project wasn't updated last snapshot change, so its program update kind is still NewFiles
		    assert::Equal(
		        t,
		        snapshotBefore->ProjectCollection
		            ->InferredProject(),
		        snapshotAfter->ProjectCollection
		            ->InferredProject());
		    assert::Equal(
		        t,
		        snapshotAfter->ProjectCollection
		            ->InferredProject()
		            ->ProgramUpdateKind,
		        project::ProgramUpdateKindNewFiles);
		    // host for inferred project should not change
		    assert::Equal(
		        t, snapshotAfter->ProjectCollection
		               ->InferredProject()
		               ->host->sourceFS->source,
		        snapshotBefore->fs);
	    });

	t->Run("cached disk files are cleaned up", [&](T* t) {
		t->Parallel();
		std::unordered_map<std::string, vfstest::MapFileInput>
		    files{
		        {"/home/projects/TS/p1/tsconfig.json", "{}"},
		        {"/home/projects/TS/p1/index.ts",
		         "import { a } from './a'; console.log(a);"},
		        {"/home/projects/TS/p1/a.ts",
		         "export const a = 1;"},
		        {"/home/projects/TS/p2/tsconfig.json", "{}"},
		        {"/home/projects/TS/p2/index.ts",
		         "import { b } from './b'; console.log(b);"},
		        {"/home/projects/TS/p2/b.ts",
		         "export const b = 2;"},
		    };
		auto* session = setup(files);
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(
		    t->Context(), "file:///home/projects/TS/p1/index.ts",
		    1, "import { a } from './a'; console.log(a);",
		    lsproto::LanguageKindTypeScript);
		session->DidOpenFile(
		    t->Context(), "file:///home/projects/TS/p2/index.ts",
		    1, "import { b } from './b'; console.log(b);",
		    lsproto::LanguageKindTypeScript);
		auto* snapshotBefore = session->Snapshot();

		// a.ts and b.ts are cached
		assert::Check(
		    t,
		    snapshotBefore->fs->cacheFiles.count(
		        tspath::Path("/home/projects/ts/p1/a.ts")) != 0);
		assert::Check(
		    t,
		    snapshotBefore->fs->cacheFiles.count(
		        tspath::Path("/home/projects/ts/p2/b.ts")) != 0);

		// Close p1's only open file
		session->DidCloseFile(
		    t->Context(),
		    "file:///home/projects/TS/p1/index.ts");
		// Next open file is unrelated to p1, triggers p1 closing and file cache cleanup
		session->DidOpenFile(t->Context(), "untitled:Untitled-1",
		                     1, "",
		                     lsproto::LanguageKindTypeScript);
		auto* snapshotAfter = session->Snapshot();

		// a.ts is cleaned up, b.ts is still cached
		assert::Check(
		    t,
		    snapshotAfter->fs->cacheFiles.count(
		        tspath::Path("/home/projects/ts/p1/a.ts")) == 0);
		assert::Check(
		    t,
		    snapshotAfter->fs->cacheFiles.count(
		        tspath::Path("/home/projects/ts/p2/b.ts")) != 0);
	});

	t->Run("GetFile returns nil for non-existent files", [&](T* t) {
		t->Parallel();
		std::unordered_map<std::string, vfstest::MapFileInput>
		    files{
		        {"/home/projects/TS/p1/tsconfig.json", "{}"},
		        {"/home/projects/TS/p1/index.ts",
		         "console.log('Hello, world!');"},
		    };
		auto* session = setup(files);
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(
		    t->Context(), "file:///home/projects/TS/p1/index.ts",
		    1, "console.log('Hello, world!');",
		    lsproto::LanguageKindTypeScript);
		auto* snapshot = session->Snapshot();

		auto* handle = snapshot->GetFile(
		    "/home/projects/TS/p1/nonexistent.ts");
		assert::Check(t, handle == nullptr,
		              "GetFile should return nil for non-existent file");

		// Test that ReadFile returns false for non-existent file
		auto [contents, ok] = snapshot->ReadFile(
		    "/home/projects/TS/p1/nonexistent.ts");
		assert::Check(
		    t, !ok,
		    "ReadFile should return false for non-existent file");
	});

	t->Run(
	    "program change loads node_modules dependency and auto-imports includes it",
	    [&](T* t) {
		    t->Parallel();
		    std::unordered_map<std::string, vfstest::MapFileInput>
		        files{
		            {"/home/projects/otherproject/tsconfig.json",
		             std::string(R"({
				"compilerOptions": {
					"module": "commonjs"
				}
			})")},
		            {"/home/projects/otherproject/index.ts", ""},
		            {"/home/projects/node_modules/foo/package.json",
		             std::string(R"({
				"types": "index.d.ts",
				"typesVersions": {
					"*": {
						"bar/*": ["dist/*"],
						"exact-match": ["dist/index.d.ts"],
						"foo/*": ["dist/*"],
						"*": ["dist/*"]
					}
				}
			})")},
		            {"/home/projects/node_modules/foo/nope.d.ts",
		             "export const nope = 0;"},
		            {"/home/projects/node_modules/foo/dist/index.d.ts",
		             "export const index = 0;"},
		            {"/home/projects/node_modules/foo/dist/blah.d.ts",
		             "export const blah = 0;"},
		            {"/home/projects/node_modules/foo/dist/foo/onlyInFooFolder.d.ts",
		             "export const foo = 0;"},
		            {"/home/projects/node_modules/foo/dist/subfolder/one.d.ts",
		             "export const one = 0;"},
		        };
		    auto* session = setup(files);
		    t->Cleanup([session] { session->Close(); });
		    auto ctx = t->Context();
		    lsproto::DocumentUri otherIndexURI =
		        "file:///home/projects/otherproject/index.ts";

		    // Open the file
		    session->DidOpenFile(ctx, otherIndexURI, 1, "",
		                         lsproto::LanguageKindTypeScript);

		    // Insert import statement:
		    // This will trigger both a program rebuild which will include the node_modules files,
		    // and an auto-import collection which should find the exports from those files.
		    {
			    lsproto::
			        TextDocumentContentChangePartialOrWholeDocument
			            change;
			    change.Partial = std::make_shared<
			        lsproto::
			            TextDocumentContentChangePartial>();
			    change.Partial->Text =
			        "import {} from \"foo/foo/subfolder/one\";";
			    change.Partial->Range = lsproto::Range{
			        .Start = lsproto::Position{0, 0},
			        .End = lsproto::Position{0, 0},
			    };
			    session->DidChangeFile(ctx, otherIndexURI, 2,
			                           {change});
		    }

		    // Now trigger snapshot clone with both program update and auto-imports registry building.
		    auto [ls, err] =
		        session
		            ->GetCurrentLanguageServiceWithAutoImports(
		                ctx, otherIndexURI);
		    assert::NilError(t, err);
		    delete ls;
	    });

	t->Run(
	    "fallback rebuild with recomputed parse options is safe for later clone",
	    [&](T* t) {
		    t->Parallel();

		    std::unordered_map<std::string, vfstest::MapFileInput>
		        testFiles{
		            {"/project/node_modules/pkg/index.ts",
		             "export const pkg = 0;"},
		            {"/project/src/other.ts",
		             "export const other = 1;"},
		        };
		    auto* session = setup(testFiles);
		    t->Cleanup([session] { session->Close(); });
		    lsproto::DocumentUri pkgURI =
		        "file:///project/node_modules/pkg/index.ts";
		    lsproto::DocumentUri otherURI =
		        "file:///project/src/other.ts";

		    session->DidOpenFile(
		        t->Context(), pkgURI, 1,
		        "export const pkg = 0;",
		        lsproto::LanguageKindTypeScript);
		    session->DidOpenFile(
		        t->Context(), otherURI, 1,
		        "export const other = 1;",
		        lsproto::LanguageKindTypeScript);
		    auto [ls, err] =
		        session->GetLanguageService(t->Context(), pkgURI);
		    assert::NilError(t, err);
		    delete ls;

		    auto werr = session->fs->WriteFile(
		        "/project/node_modules/pkg/package.json",
		        "{ \"type\": \"module\" }");
		    assert::Assert(t, werr.impl() == nullptr);
		    {
			    lsproto::
			        TextDocumentContentChangePartialOrWholeDocument
			            change;
			    change.WholeDocument = std::make_shared<
			        lsproto::
			            TextDocumentContentChangeWholeDocument>();
			    change.WholeDocument->Text =
			        "import \"./missing\"; export const pkg = 1;";
			    session->DidChangeFile(t->Context(), pkgURI, 2,
			                           {change});
		    }
		    auto [ls2, err2] =
		        session->GetLanguageService(t->Context(), pkgURI);
		    assert::NilError(t, err2);
		    delete ls2;

		    {
			    lsproto::
			        TextDocumentContentChangePartialOrWholeDocument
			            change;
			    change.WholeDocument = std::make_shared<
			        lsproto::
			            TextDocumentContentChangeWholeDocument>();
			    change.WholeDocument->Text =
			        "export const other = 2;";
			    session->DidChangeFile(t->Context(), otherURI, 2,
			                           {change});
		    }
		    auto [ls3, err3] = session->GetLanguageService(
		        t->Context(), otherURI);
		    assert::NilError(t, err3);
		    delete ls3;
	    });

	t->Run(
	    "auto-import snapshot is adopted when session snapshot is unchanged",
	    [&](T* t) {
		    t->Parallel();
		    std::unordered_map<std::string, vfstest::MapFileInput>
		        files{
		            {"/home/projects/TS/p1/tsconfig.json", "{}"},
		            {"/home/projects/TS/p1/index.ts",
		             "const value = foo;"},
		            {"/home/projects/TS/p1/foo.ts",
		             "export const foo = 1;"},
		        };
		    auto* session = setup(files);
		    t->Cleanup([session] { session->Close(); });
		    auto ctx = t->Context();
		    lsproto::DocumentUri uri =
		        "file:///home/projects/TS/p1/index.ts";

		    session->DidOpenFile(ctx, uri, 1,
		                         "const value = foo;",
		                         lsproto::LanguageKindTypeScript);
		    auto [ls, err] =
		        session->GetLanguageService(ctx, uri);
		    assert::NilError(t, err);
		    delete ls;

		    auto* baseSnapshot = session->Snapshot();
		    auto* preparedSnapshot =
		        session->snapshotHost
		            ->CloneSnapshotWithAutoImports(
		                ctx, baseSnapshot, uri, nullptr);
		    session->TryAdoptSnapshotInBackground(
		        baseSnapshot, preparedSnapshot);
		    t->Cleanup(
		        [preparedSnapshot] { preparedSnapshot->Deref(); });

		    session->WaitForBackgroundTasks();
		    assert::Equal(t, session->Snapshot(),
		                  preparedSnapshot);
	    });

	t->Run("no-op watch change does not rebuild program", [&](T* t) {
		t->Parallel();
		std::unordered_map<std::string, vfstest::MapFileInput>
		    files{
		        {"/home/projects/TS/p1/tsconfig.json", "{}"},
		        {"/home/projects/TS/p1/index.ts",
		         "import { a } from './a'; console.log(a);"},
		        {"/home/projects/TS/p1/a.ts",
		         "export const a = 1;"},
		    };
		auto* session = setup(files);
		t->Cleanup([session] { session->Close(); });
		auto ctx = t->Context();
		lsproto::DocumentUri uri =
		    "file:///home/projects/TS/p1/index.ts";
		auto configPath =
		    tspath::Path("/home/projects/ts/p1/tsconfig.json");

		session->DidOpenFile(
		    ctx, uri, 1,
		    "import { a } from './a'; console.log(a);",
		    lsproto::LanguageKindTypeScript);
		auto [ls, err] = session->GetLanguageService(ctx, uri);
		assert::NilError(t, err);
		delete ls;

		auto* programBefore =
		    session->Snapshot()
		        ->ProjectCollection
		        ->ConfiguredProject(configPath)
		        ->Program;

		// Send a watch change event for a project file whose content on disk is unchanged.
		// This should not invalidate the program or trigger a recheck.
		session->pendingFileChangesMu.lock();
		session->pendingFileChanges.push_back(project::FileChange{
		    .Kind = project::FileChangeKindWatchChange,
		    .URI =
		        lsproto::DocumentUri(
		            "file:///home/projects/TS/p1/a.ts"),
		});
		session->pendingFileChangesMu.unlock();
		auto [ls2, err2] = session->GetLanguageService(ctx, uri);
		assert::NilError(t, err2);
		delete ls2;

		auto* programAfter =
		    session->Snapshot()
		        ->ProjectCollection
		        ->ConfiguredProject(configPath)
		        ->Program;
		assert::Equal(
		    t, programBefore, programAfter,
		    "no-op watch change should not rebuild the program");

		// A watch change that reflects an actual content change on disk must still
		// rebuild the program.
		auto werr = session->fs->WriteFile(
		    "/home/projects/TS/p1/a.ts", "export const a = 2;");
		assert::Assert(t, werr.impl() == nullptr);
		session->pendingFileChangesMu.lock();
		session->pendingFileChanges.push_back(project::FileChange{
		    .Kind = project::FileChangeKindWatchChange,
		    .URI =
		        lsproto::DocumentUri(
		            "file:///home/projects/TS/p1/a.ts"),
		});
		session->pendingFileChangesMu.unlock();
		auto [ls3, err3] = session->GetLanguageService(ctx, uri);
		assert::NilError(t, err3);
		delete ls3;

		auto* programChanged =
		    session->Snapshot()
		        ->ProjectCollection
		        ->ConfiguredProject(configPath)
		        ->Program;
		assert::Assert(
		    t, programBefore != programChanged,
		    "real watch change should rebuild the program");
	});
}

void TestProjectIDNarrowing(T* t) {
	t->Parallel();

	project::ID configured{"/project/tsconfig.json"};
	auto [configuredID, ok] = project::idConfigured(configured);
	assert::Assert(t, ok);
	assert::Equal(t, configuredID,
	              project::ConfiguredProjectID(
	                  "/project/tsconfig.json"));
	auto [i1, ok1] = project::idInferred(configured);
	assert::Assert(t, !ok1);
	auto [s1, ok2] = project::idSynthetic(configured);
	assert::Assert(t, !ok2);

	auto inferred = project::inferredProjectID.AsID();
	auto [inferredID, ok3] = project::idInferred(inferred);
	assert::Assert(t, ok3);
	assert::Equal(t, inferredID, project::inferredProjectID);
	auto [c1, ok4] = project::idConfigured(inferred);
	assert::Assert(t, !ok4);

	auto synthetic = project::NewSyntheticProjectID(1).AsID();
	auto [syntheticID, ok5] = project::idSynthetic(synthetic);
	assert::Assert(t, ok5);
	assert::Equal(t, syntheticID,
	              project::NewSyntheticProjectID(1));
	auto [c2, ok6] = project::idConfigured(synthetic);
	assert::Assert(t, !ok6);

	auto [canonicalSyntheticID, ok7] = project::idSynthetic(
	    project::ID("/dev/null/synthetic/01"));
	assert::Assert(t, ok7);
	assert::Equal(t, canonicalSyntheticID,
	              project::NewSyntheticProjectID(1));

	auto [c3, ok8] = project::idConfigured(
	    project::ID("/dev/null/synthetic/invalid"));
	assert::Assert(t, ok8);

	auto [c4, ok9] = project::ParseConfiguredProjectID(
	    tspath::Path(project::inferredProjectName));
	assert::Assert(t, !ok9);
	auto [c5, ok10] = project::ParseConfiguredProjectID(
	    tspath::Path(project::NewSyntheticProjectID(1)));
	assert::Assert(t, !ok10);
}

REGISTER_UNIT_TEST("project.TestSnapshot", TestSnapshot);
REGISTER_UNIT_TEST("project.TestProjectIDNarrowing",
                   TestProjectIDNarrowing);

}  // namespace
