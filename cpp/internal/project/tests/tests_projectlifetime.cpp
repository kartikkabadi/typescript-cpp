// Port of tsc/internal/project/projectlifetime_test.go.
#include <string>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/ls/ls.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/project/session.h"
#include "internal/project/snapshot.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace lsproto = tsc::lsp::lsproto;
namespace project = tsc::project;
namespace projecttestutil = tsc::testutil::projecttestutil;
namespace tspath = tsc::tspath;
using tsc::gostd::testing::T;

const std::string& plFileText(const projecttestutil::FileMap& files,
                              const std::string& name) {
	return std::get<std::string>(files.at(name));
}

lsproto::FileEvent* plFileEvent(const lsproto::DocumentUri& uri,
                                lsproto::FileChangeType type) {
	auto* ev = new lsproto::FileEvent();
	ev->Uri = uri;
	ev->Type = type;
	return ev;
}

void TestProjectLifetime(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	t->Run("configured project", [](T* t) {
		t->Parallel();
		projecttestutil::FileMap files{
		    {"/home/projects/TS/p1/tsconfig.json", std::string(R"({
				"compilerOptions": {
					"noLib": true,
					"module": "nodenext",
					"strict": true
				},
				"include": ["src"]
			})")},
		    {"/home/projects/TS/p1/src/index.ts",
		     std::string("import { x } from \"./x\";")},
		    {"/home/projects/TS/p1/src/x.ts",
		     std::string("export const x = 1;")},
		    {"/home/projects/TS/p1/config.ts",
		     std::string("let x = 1, y = 2;")},
		    {"/home/projects/TS/p2/tsconfig.json", std::string(R"({
				"compilerOptions": {
					"noLib": true,
					"module": "nodenext",
					"strict": true
				},
				"include": ["src"]
			})")},
		    {"/home/projects/TS/p2/src/index.ts",
		     std::string("import { x } from \"./x\";")},
		    {"/home/projects/TS/p2/src/x.ts",
		     std::string("export const x = 1;")},
		    {"/home/projects/TS/p2/config.ts",
		     std::string("let x = 1, y = 2;")},
		    {"/home/projects/TS/p3/tsconfig.json", std::string(R"({
				"compilerOptions": {
					"noLib": true,
					"module": "nodenext",
					"strict": true
				},
				"include": ["src"]
			})")},
		    {"/home/projects/TS/p3/src/index.ts",
		     std::string("import { x } from \"./x\";")},
		    {"/home/projects/TS/p3/src/x.ts",
		     std::string("export const x = 1;")},
		    {"/home/projects/TS/p3/config.ts",
		     std::string("let x = 1, y = 2;")},
		};
		auto [session, utils] = projecttestutil::Setup(files);
		auto* snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        snapshot->ProjectCollection->Projects().size()),
		    int64_t(0));

		// Open files in two projects
		lsproto::DocumentUri uri1 =
		    "file:///home/projects/TS/p1/src/index.ts";
		lsproto::DocumentUri uri2 =
		    "file:///home/projects/TS/p2/src/index.ts";
		session->DidOpenFile(
		    t->Context(), uri1, 1,
		    plFileText(files, "/home/projects/TS/p1/src/index.ts"),
		    lsproto::LanguageKindTypeScript);
		session->DidOpenFile(
		    t->Context(), uri2, 1,
		    plFileText(files, "/home/projects/TS/p2/src/index.ts"),
		    lsproto::LanguageKindTypeScript);
		session->WaitForBackgroundTasks();
		snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        snapshot->ProjectCollection->Projects().size()),
		    int64_t(2));
		assert::Assert(
		    t, snapshot->ProjectCollection->ConfiguredProject(
		           tspath::Path("/home/projects/ts/p1/tsconfig.json")) !=
		           nullptr);
		assert::Assert(
		    t, snapshot->ProjectCollection->ConfiguredProject(
		           tspath::Path("/home/projects/ts/p2/tsconfig.json")) !=
		           nullptr);
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        utils->Client()->WatchFilesCalls().size()),
		    int64_t(1));
		assert::Assert(
		    t, snapshot->ConfigFileRegistry->GetConfig(
		           tspath::Path("/home/projects/ts/p1/tsconfig.json")) !=
		           nullptr);
		assert::Assert(
		    t, snapshot->ConfigFileRegistry->GetConfig(
		           tspath::Path("/home/projects/ts/p2/tsconfig.json")) !=
		           nullptr);

		// Close p1 file and open p3 file
		session->DidCloseFile(t->Context(), uri1);
		lsproto::DocumentUri uri3 =
		    "file:///home/projects/TS/p3/src/index.ts";
		session->DidOpenFile(
		    t->Context(), uri3, 1,
		    plFileText(files, "/home/projects/TS/p3/src/index.ts"),
		    lsproto::LanguageKindTypeScript);
		session->WaitForBackgroundTasks();
		// Should still have two projects, but p1 replaced by p3
		snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        snapshot->ProjectCollection->Projects().size()),
		    int64_t(2));
		assert::Assert(
		    t, snapshot->ProjectCollection->ConfiguredProject(
		           tspath::Path("/home/projects/ts/p1/tsconfig.json")) ==
		           nullptr);
		assert::Assert(
		    t, snapshot->ProjectCollection->ConfiguredProject(
		           tspath::Path("/home/projects/ts/p2/tsconfig.json")) !=
		           nullptr);
		assert::Assert(
		    t, snapshot->ProjectCollection->ConfiguredProject(
		           tspath::Path("/home/projects/ts/p3/tsconfig.json")) !=
		           nullptr);
		assert::Assert(
		    t, snapshot->ConfigFileRegistry->GetConfig(
		           tspath::Path("/home/projects/ts/p1/tsconfig.json")) ==
		           nullptr);
		assert::Assert(
		    t, snapshot->ConfigFileRegistry->GetConfig(
		           tspath::Path("/home/projects/ts/p2/tsconfig.json")) !=
		           nullptr);
		assert::Assert(
		    t, snapshot->ConfigFileRegistry->GetConfig(
		           tspath::Path("/home/projects/ts/p3/tsconfig.json")) !=
		           nullptr);
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        utils->Client()->WatchFilesCalls().size()),
		    int64_t(1));
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        utils->Client()->UnwatchFilesCalls().size()),
		    int64_t(0));

		// Close p2 and p3 files, open p1 file again
		session->DidCloseFile(t->Context(), uri2);
		session->DidCloseFile(t->Context(), uri3);
		session->DidOpenFile(
		    t->Context(), uri1, 1,
		    plFileText(files, "/home/projects/TS/p1/src/index.ts"),
		    lsproto::LanguageKindTypeScript);
		session->WaitForBackgroundTasks();
		// Should have one project (p1)
		snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        snapshot->ProjectCollection->Projects().size()),
		    int64_t(1));
		assert::Assert(
		    t, snapshot->ProjectCollection->ConfiguredProject(
		           tspath::Path("/home/projects/ts/p1/tsconfig.json")) !=
		           nullptr);
		assert::Assert(
		    t, snapshot->ConfigFileRegistry->GetConfig(
		           tspath::Path("/home/projects/ts/p1/tsconfig.json")) !=
		           nullptr);
		assert::Assert(
		    t, snapshot->ConfigFileRegistry->GetConfig(
		           tspath::Path("/home/projects/ts/p2/tsconfig.json")) ==
		           nullptr);
		assert::Assert(
		    t, snapshot->ConfigFileRegistry->GetConfig(
		           tspath::Path("/home/projects/ts/p3/tsconfig.json")) ==
		           nullptr);
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        utils->Client()->WatchFilesCalls().size()),
		    int64_t(1));
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        utils->Client()->UnwatchFilesCalls().size()),
		    int64_t(0));
	});

	t->Run("unrooted inferred projects", [](T* t) {
		t->Parallel();
		projecttestutil::FileMap files{
		    {"/home/projects/TS/p1/src/index.ts",
		     std::string("import { x } from \"./x\";")},
		    {"/home/projects/TS/p1/src/x.ts",
		     std::string("export const x = 1;")},
		    {"/home/projects/TS/p1/config.ts",
		     std::string("let x = 1, y = 2;")},
		    {"/home/projects/TS/p2/src/index.ts",
		     std::string("import { x } from \"./x\";")},
		    {"/home/projects/TS/p2/src/x.ts",
		     std::string("export const x = 1;")},
		    {"/home/projects/TS/p2/config.ts",
		     std::string("let x = 1, y = 2;")},
		    {"/home/projects/TS/p3/src/index.ts",
		     std::string("import { x } from \"./x\";")},
		    {"/home/projects/TS/p3/src/x.ts",
		     std::string("export const x = 1;")},
		    {"/home/projects/TS/p3/config.ts",
		     std::string("let x = 1, y = 2;")},
		};
		auto [session, utils] = projecttestutil::Setup(files);
		auto* snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        snapshot->ProjectCollection->Projects().size()),
		    int64_t(0));

		// Open files without workspace roots (empty string) - should create single inferred project
		lsproto::DocumentUri uri1 =
		    "file:///home/projects/TS/p1/src/index.ts";
		lsproto::DocumentUri uri2 =
		    "file:///home/projects/TS/p2/src/index.ts";
		session->DidOpenFile(
		    t->Context(), uri1, 1,
		    plFileText(files, "/home/projects/TS/p1/src/index.ts"),
		    lsproto::LanguageKindTypeScript);
		session->DidOpenFile(
		    t->Context(), uri2, 1,
		    plFileText(files, "/home/projects/TS/p2/src/index.ts"),
		    lsproto::LanguageKindTypeScript);

		// Should have one inferred project
		snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        snapshot->ProjectCollection->Projects().size()),
		    int64_t(1));
		assert::Assert(
		    t, snapshot->ProjectCollection->InferredProject() !=
		           nullptr);

		// Close p1 file and open p3 file
		session->DidCloseFile(t->Context(), uri1);
		lsproto::DocumentUri uri3 =
		    "file:///home/projects/TS/p3/src/index.ts";
		session->DidOpenFile(
		    t->Context(), uri3, 1,
		    plFileText(files, "/home/projects/TS/p3/src/index.ts"),
		    lsproto::LanguageKindTypeScript);

		// Should still have one inferred project
		snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        snapshot->ProjectCollection->Projects().size()),
		    int64_t(1));
		assert::Assert(
		    t, snapshot->ProjectCollection->InferredProject() !=
		           nullptr);

		// Close p2 and p3 files, open p1 file again
		session->DidCloseFile(t->Context(), uri2);
		session->DidCloseFile(t->Context(), uri3);
		session->DidOpenFile(
		    t->Context(), uri1, 1,
		    plFileText(files, "/home/projects/TS/p1/src/index.ts"),
		    lsproto::LanguageKindTypeScript);

		// Should still have one inferred project
		snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        snapshot->ProjectCollection->Projects().size()),
		    int64_t(1));
		assert::Assert(
		    t, snapshot->ProjectCollection->InferredProject() !=
		           nullptr);
	});

	t->Run("file moves from inferred to configured project", [](T* t) {
		t->Parallel();
		projecttestutil::FileMap files{
		    {"/home/projects/ts/foo.ts",
		     std::string("export const foo = 1;")},
		    {"/home/projects/ts/p1/tsconfig.json", std::string(R"({
				"compilerOptions": {
					"noLib": true,
					"module": "nodenext",
					"strict": true
				},
				"include": ["main.ts"]
			})")},
		    {"/home/projects/ts/p1/main.ts",
		     std::string(
		         "import { foo } from \"../foo\"; console.log(foo);")},
		};
		auto [session, utils] = projecttestutil::Setup(files);

		// Open foo.ts first - should create inferred project since no tsconfig found initially
		lsproto::DocumentUri fooUri =
		    "file:///home/projects/ts/foo.ts";
		session->DidOpenFile(t->Context(), fooUri, 1,
		                     plFileText(files, "/home/projects/ts/foo.ts"),
		                     lsproto::LanguageKindTypeScript);

		// Should have one inferred project
		auto* snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        snapshot->ProjectCollection->Projects().size()),
		    int64_t(1));
		assert::Assert(
		    t, snapshot->ProjectCollection->InferredProject() !=
		           nullptr);
		assert::Assert(
		    t, snapshot->ProjectCollection->ConfiguredProject(
		           tspath::Path("/home/projects/ts/p1/tsconfig.json")) ==
		           nullptr);

		// Now open main.ts - should trigger discovery of tsconfig.json and move foo.ts to configured project
		lsproto::DocumentUri mainUri =
		    "file:///home/projects/ts/p1/main.ts";
		session->DidOpenFile(
		    t->Context(), mainUri, 1,
		    plFileText(files, "/home/projects/ts/p1/main.ts"),
		    lsproto::LanguageKindTypeScript);

		// Should now have one configured project and no inferred project
		snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        snapshot->ProjectCollection->Projects().size()),
		    int64_t(1));
		assert::Assert(
		    t, snapshot->ProjectCollection->InferredProject() ==
		           nullptr);
		assert::Assert(
		    t, snapshot->ProjectCollection->ConfiguredProject(
		           tspath::Path("/home/projects/ts/p1/tsconfig.json")) !=
		           nullptr);

		// Config file should be present
		assert::Assert(
		    t, snapshot->ConfigFileRegistry->GetConfig(
		           tspath::Path("/home/projects/ts/p1/tsconfig.json")) !=
		           nullptr);

		// Close main.ts - configured project should remain because foo.ts is still open
		session->DidCloseFile(t->Context(), mainUri);
		snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        snapshot->ProjectCollection->Projects().size()),
		    int64_t(1));
		assert::Assert(
		    t, snapshot->ProjectCollection->ConfiguredProject(
		           tspath::Path("/home/projects/ts/p1/tsconfig.json")) !=
		           nullptr);

		// Close foo.ts - configured project should be retained until next file open
		session->DidCloseFile(t->Context(), fooUri);
		snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        snapshot->ProjectCollection->Projects().size()),
		    int64_t(1));
		assert::Assert(
		    t, snapshot->ConfigFileRegistry->GetConfig(
		           tspath::Path("/home/projects/ts/p1/tsconfig.json")) !=
		           nullptr);
	});

	t->Run(
	    "file move from inferred to configured via didOpen/didClose sequence",
	    [](T* t) {
		    t->Parallel();
		    // Start with tsconfig.json that includes "src" but file is at root level
		    projecttestutil::FileMap files{
		        {"/home/projects/TS/p1/tsconfig.json", std::string(R"({
				"compilerOptions": {
					"noLib": true
				},
				"include": ["src"]
			})")},
		        {"/home/projects/TS/p1/index.ts",
		         std::string("export const x = 1;")},
		    };
		    auto [session, utils] = projecttestutil::Setup(files);

		    // Open index.ts at root level - should create inferred project since it's not under src/
		    // Creates config file registry entry, but has no files
		    lsproto::DocumentUri indexUri =
		        "file:///home/projects/TS/p1/index.ts";
		    session->DidOpenFile(
		        t->Context(), indexUri, 1,
		        plFileText(files, "/home/projects/TS/p1/index.ts"),
		        lsproto::LanguageKindTypeScript);

		    // Should have one inferred project only (file is not included by tsconfig)
		    auto* snapshot = session->Snapshot();
		    assert::Equal(
		        t,
		        static_cast<int64_t>(
		            snapshot->ProjectCollection->Projects().size()),
		        int64_t(1));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() !=
		               nullptr);
		    assert::Assert(
		        t, snapshot->ProjectCollection->ConfiguredProject(
		               tspath::Path(
		                   "/home/projects/ts/p1/tsconfig.json")) ==
		               nullptr);

		    // Simulate file move: create src/index.ts on disk
		    auto werr = utils->FS()->WriteFile(
		        "/home/projects/TS/p1/src/index.ts",
		        plFileText(files, "/home/projects/TS/p1/index.ts"));
		    assert::Assert(t, werr.impl() == nullptr);
		    werr = utils->FS()->Remove("/home/projects/TS/p1/index.ts");
		    assert::Assert(t, werr.impl() == nullptr);

		    // Simulate file move sequence as it would happen in an editor:
		    // 1. didOpen src/index.ts (new location)
		    // Open comes in before file create event, so the config file is not marked as needing a file name reload,
		    // so it's not turned into a configured project yet. This is probably not ideal, but it should sort itself
		    // out momentarily after the file watcher events are processed. When we try the config file, we mark it
		    // as "retained by src/index.ts" so the config entry doesn't get deleted before src/index.ts is closed.
		    // Even though we currently think src/index.ts doesn't belong to the config, the config is in its directory
		    // path, so we'll always see it as a candidate for containing src/index.ts.
		    lsproto::DocumentUri srcIndexUri =
		        "file:///home/projects/TS/p1/src/index.ts";
		    session->DidOpenFile(
		        t->Context(), srcIndexUri, 1,
		        plFileText(files, "/home/projects/TS/p1/index.ts"),
		        lsproto::LanguageKindTypeScript);

		    // 2. didClose index.ts (old location)
		    session->DidCloseFile(t->Context(), indexUri);

		    // 3. didChangeWatchedFiles: create src/index.ts and delete index.ts
		    // The creation event for src/index.ts now hits the config file registry, and we should notice we
		    // got a creation event for a file that retained the config, triggering a filename reload.
		    session->DidChangeWatchedFiles(
		        t->Context(),
		        {plFileEvent(srcIndexUri,
		                     lsproto::FileChangeTypeCreated),
		         plFileEvent(indexUri,
		                     lsproto::FileChangeTypeDeleted)});

		    // Should now have one configured project only (file is now under src/)
		    auto [ls, err] =
		        session->GetLanguageService(t->Context(), srcIndexUri);
		    assert::NilError(t, err);
		    delete ls;
		    snapshot = session->Snapshot();
		    assert::Equal(
		        t,
		        static_cast<int64_t>(
		            snapshot->ProjectCollection->Projects().size()),
		        int64_t(1));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() ==
		               nullptr);
		    assert::Assert(
		        t, snapshot->ProjectCollection->ConfiguredProject(
		               tspath::Path(
		                   "/home/projects/ts/p1/tsconfig.json")) !=
		               nullptr);
	    });

	// Regression test for https://github.com/microsoft/TypeScript/tsc/issues/3733
	//
	// "./"-prefixed include specs combined with a dot-directory exclude ("**/.*/")
	// used to drop all wildcard directories for the config, so a newly created file
	// never triggered a root file reload. The stale nested config never claimed the
	// file, and default-project resolution assigned it to the ancestor project
	// (parsed fresh from disk, so it did contain the file) — or the inferred project
	// when no ancestor config matched — until the session was restarted. The file
	// then had the wrong compiler options (e.g. no Temporal from lib ESNext).
	// The failure was independent of the order in which didOpen and the create
	// watch event arrived, so both orderings are covered.
	auto runNewFileScenario = [](T* t, bool openBeforeCreateEvent) {
		projecttestutil::FileMap files{
		    {"/home/projects/TS/monorepo/tsconfig.json", std::string(R"({
				"compilerOptions": {
					"target": "ES2015",
					"lib": ["DOM"]
				},
				"include": ["apps"]
			})")},
		    {"/home/projects/TS/monorepo/apps/web/tsconfig.json",
		     std::string(R"({
				"compilerOptions": {
					"target": "ESNext",
					"lib": ["ESNext"]
				},
				"include": ["./app/**/*.ts"],
				"exclude": ["**/.*/"]
			})")},
		    {"/home/projects/TS/monorepo/apps/web/app/existing.ts",
		     std::string("export const existing = 1;")},
		};
		auto setupPair = projecttestutil::Setup(files);
		// Structured bindings can't be captured by lambdas on clang-15;
		// rebind to plain names.
		auto* session = setupPair.first;
		auto utils = setupPair.second;

		// Open an existing file so the nested configured project loads.
		lsproto::DocumentUri existingUri =
		    "file:///home/projects/TS/monorepo/apps/web/app/existing.ts";
		session->DidOpenFile(
		    t->Context(), existingUri, 1,
		    plFileText(
		        files,
		        "/home/projects/TS/monorepo/apps/web/app/existing.ts"),
		    lsproto::LanguageKindTypeScript);
		auto [ls, err] =
		    session->GetLanguageService(t->Context(), existingUri);
		assert::NilError(t, err);
		assert::Equal(t, ls->GetProgram()->Options()->Target,
		              tsc::ScriptTarget::ESNext);
		delete ls;

		// Create a new file on disk in a directory matched by the nested config's
		// wildcard include, then simulate the editor events in the given order.
		std::string newFileContent = "export const newFile = 1;";
		lsproto::DocumentUri newFileUri =
		    "file:///home/projects/TS/monorepo/apps/web/app/subdir/new.ts";
		assert::Assert(
		    t, utils
		           ->FS()
		           ->WriteFile(
		               "/home/projects/TS/monorepo/apps/web/app/subdir/new.ts",
		               newFileContent)
		           .impl() == nullptr);
		auto openNewFile = [&]() {
			session->DidOpenFile(t->Context(), newFileUri, 1,
			                     newFileContent,
			                     lsproto::LanguageKindTypeScript);
		};
		auto sendCreateEvent = [&]() {
			session->DidChangeWatchedFiles(
			    t->Context(),
			    {plFileEvent(newFileUri,
			                 lsproto::FileChangeTypeCreated)});
		};
		if (openBeforeCreateEvent) {
			openNewFile();
			sendCreateEvent();
		} else {
			sendCreateEvent();
			openNewFile();
		}

		// The new file should be served by the nested configured project, not the
		// root project (ES2015 target) or an inferred project.
		auto [ls2, err2] =
		    session->GetLanguageService(t->Context(), newFileUri);
		assert::NilError(t, err2);
		assert::Equal(t, ls2->GetProgram()->Options()->Target,
		              tsc::ScriptTarget::ESNext);
		assert::Assert(
		    t,
		    session->Snapshot()->ProjectCollection->InferredProject() ==
		        nullptr);
		delete ls2;
	};

	t->Run(
	    "newly created file joins configured project with ./-prefixed includes (didOpen before create event)",
	    [&](T* t) {
		    t->Parallel();
		    runNewFileScenario(t, true);
	    });

	t->Run(
	    "newly created file joins configured project with ./-prefixed includes (create event before didOpen)",
	    [&](T* t) {
		    t->Parallel();
		    runNewFileScenario(t, false);
	    });

	t->Run(
	    "tsconfig move from subdirectory to parent via didChangeWatchedFiles",
	    [](T* t) {
		    t->Parallel();
		    // Start with tsconfig.json in src/ that includes "src" - file won't be included initially
		    projecttestutil::FileMap files{
		        {"/home/projects/TS/p1/src/tsconfig.json", std::string(R"({
				"compilerOptions": {
					"noLib": true
				},
				"include": ["src"]
			})")},
		        {"/home/projects/TS/p1/src/index.ts",
		         std::string("export const x = 1;")},
		        {"/home/projects/TS/p1/src/other.ts",
		         std::string("export const y = 2;")},
		    };
		    auto [session, utils] = projecttestutil::Setup(files);

		    // Open src/index.ts - should create inferred project since tsconfig.json includes "src"
		    // relative to its location (src/src/ which doesn't exist)
		    lsproto::DocumentUri indexUri =
		        "file:///home/projects/TS/p1/src/index.ts";
		    session->DidOpenFile(
		        t->Context(), indexUri, 1,
		        plFileText(files, "/home/projects/TS/p1/src/index.ts"),
		        lsproto::LanguageKindTypeScript);

		    // Should have one inferred project only (file is not included by tsconfig at src/tsconfig.json)
		    auto* snapshot = session->Snapshot();
		    assert::Equal(
		        t,
		        static_cast<int64_t>(
		            snapshot->ProjectCollection->Projects().size()),
		        int64_t(1));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() !=
		               nullptr);
		    assert::Assert(
		        t, snapshot->ProjectCollection->ConfiguredProject(
		               tspath::Path(
		                   "/home/projects/ts/p1/src/tsconfig.json")) ==
		               nullptr);

		    // Simulate tsconfig.json move: create tsconfig.json at parent level, delete from src/
		    auto tsconfigContent =
		        plFileText(files, "/home/projects/TS/p1/src/tsconfig.json");
		    auto werr = utils->FS()->WriteFile(
		        "/home/projects/TS/p1/tsconfig.json", tsconfigContent);
		    assert::Assert(t, werr.impl() == nullptr);
		    werr = utils->FS()->Remove(
		        "/home/projects/TS/p1/src/tsconfig.json");
		    assert::Assert(t, werr.impl() == nullptr);

		    // Simulate file move via didChangeWatchedFiles
		    lsproto::DocumentUri newTsconfigUri =
		        "file:///home/projects/TS/p1/tsconfig.json";
		    lsproto::DocumentUri oldTsconfigUri =
		        "file:///home/projects/TS/p1/src/tsconfig.json";
		    session->DidChangeWatchedFiles(
		        t->Context(),
		        {plFileEvent(newTsconfigUri,
		                     lsproto::FileChangeTypeCreated),
		         plFileEvent(oldTsconfigUri,
		                     lsproto::FileChangeTypeDeleted)});
		    session->WaitForBackgroundTasks();

		    // The background update should route index.ts to the new configured project,
		    // but project cleanup is deferred until the next file open.
		    auto [ls, err] =
		        session->GetLanguageService(t->Context(), indexUri);
		    assert::NilError(t, err);
		    delete ls;
		    snapshot = session->Snapshot();
		    assert::Equal(
		        t,
		        static_cast<int64_t>(
		            snapshot->ProjectCollection->Projects().size()),
		        int64_t(2));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() !=
		               nullptr);
		    assert::Equal(
		        t,
		        snapshot->GetDefaultProject(indexUri)
		            ->ConfigFileName(),
		        std::string("/home/projects/TS/p1/tsconfig.json"));

		    lsproto::DocumentUri otherUri =
		        "file:///home/projects/TS/p1/src/other.ts";
		    session->DidOpenFile(
		        t->Context(), otherUri, 1,
		        plFileText(files, "/home/projects/TS/p1/src/other.ts"),
		        lsproto::LanguageKindTypeScript);
		    snapshot = session->Snapshot();
		    assert::Equal(
		        t,
		        static_cast<int64_t>(
		            snapshot->ProjectCollection->Projects().size()),
		        int64_t(1));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() ==
		               nullptr);
		    assert::Assert(
		        t, snapshot->ProjectCollection->ConfiguredProject(
		               tspath::Path(
		                   "/home/projects/ts/p1/tsconfig.json")) !=
		               nullptr);
	    });

	t->Run("deleted open file remains in project until closed",
	       [](T* t) {
		    t->Parallel();
		    // Scenario:
		    // 1. Start with two files included by a tsconfig, both open
		    // 2. In a single batch change, delete one of the files but leave it open, and create a new file included by the tsconfig
		    // 3. Request a LS for the deleted but open file
		    // 4. Project should include both the new file and the deleted open file
		    // 5. Close the deleted file
		    // 6. On next LS request, the project should exclude the deleted file

		    projecttestutil::FileMap files{
		        {"/home/projects/TS/p1/tsconfig.json", std::string(R"({
				"compilerOptions": {
					"noLib": true
				},
				"include": ["src"]
			})")},
		        {"/home/projects/TS/p1/src/index.ts", std::string("")},
		        {"/home/projects/TS/p1/src/x.ts",
		         std::string("export const x = 1;")},
		    };
		    auto [session, utils] = projecttestutil::Setup(files);

		    // Step 1: Open both files
		    lsproto::DocumentUri indexUri =
		        "file:///home/projects/TS/p1/src/index.ts";
		    lsproto::DocumentUri xUri =
		        "file:///home/projects/TS/p1/src/x.ts";
		    session->DidOpenFile(
		        t->Context(), indexUri, 1,
		        plFileText(files, "/home/projects/TS/p1/src/index.ts"),
		        lsproto::LanguageKindTypeScript);
		    session->DidOpenFile(
		        t->Context(), xUri, 1,
		        plFileText(files, "/home/projects/TS/p1/src/x.ts"),
		        lsproto::LanguageKindTypeScript);

		    // Verify initial state - both files should be in the project
		    auto [ls, err] = session->GetLanguageService(
		        t->Context(), indexUri);
		    assert::NilError(t, err);
		    auto* program = ls->GetProgram();
		    assert::Assert(
		        t,
		        program->GetSourceFile(
		            "/home/projects/TS/p1/src/index.ts") != nullptr,
		        "index.ts should be in project");
		    assert::Assert(
		        t,
		        program->GetSourceFile(
		            "/home/projects/TS/p1/src/x.ts") != nullptr,
		        "x.ts should be in project");
		    delete ls;

		    // Step 2: In a single batch change:
		    // - Delete x.ts from disk (but leave it open)
		    // - Create a new file y.ts on disk
		    auto werr = utils->FS()->Remove(
		        "/home/projects/TS/p1/src/x.ts");
		    assert::Assert(t, werr.impl() == nullptr);
		    werr = utils->FS()->WriteFile(
		        "/home/projects/TS/p1/src/y.ts",
		        "export const y = 2;");
		    assert::Assert(t, werr.impl() == nullptr);

		    // Send both events in a single batch
		    session->DidChangeWatchedFiles(
		        t->Context(),
		        {plFileEvent(xUri, lsproto::FileChangeTypeDeleted),
		         plFileEvent(
		             "file:///home/projects/TS/p1/src/y.ts",
		             lsproto::FileChangeTypeCreated)});

		    // Step 3 & 4: Request LS for the deleted but still open file
		    // Project should include: index.ts, x.ts (open overlay), y.ts (new disk file)
		    auto [ls2, err2] =
		        session->GetLanguageService(t->Context(), xUri);
		    assert::NilError(t, err2);
		    program = ls2->GetProgram();
		    assert::Assert(
		        t,
		        program->GetSourceFile(
		            "/home/projects/TS/p1/src/index.ts") != nullptr,
		        "index.ts should still be in project");
		    assert::Assert(
		        t,
		        program->GetSourceFile(
		            "/home/projects/TS/p1/src/x.ts") != nullptr,
		        "x.ts should still be in project (open overlay)");
		    assert::Assert(
		        t,
		        program->GetSourceFile(
		            "/home/projects/TS/p1/src/y.ts") != nullptr,
		        "y.ts should be in project (new file)");
		    delete ls2;

		    // Step 5: Close the deleted file
		    session->DidCloseFile(t->Context(), xUri);

		    // Step 6: On next LS request, x.ts should be excluded
		    auto [ls3, err3] = session->GetLanguageService(
		        t->Context(), indexUri);
		    assert::NilError(t, err3);
		    program = ls3->GetProgram();
		    assert::Assert(
		        t,
		        program->GetSourceFile(
		            "/home/projects/TS/p1/src/index.ts") != nullptr,
		        "index.ts should still be in project");
		    assert::Assert(
		        t,
		        program->GetSourceFile(
		            "/home/projects/TS/p1/src/x.ts") == nullptr,
		        "x.ts should no longer be in project (closed and deleted)");
		    assert::Assert(
		        t,
		        program->GetSourceFile(
		            "/home/projects/TS/p1/src/y.ts") != nullptr,
		        "y.ts should still be in project");
		    delete ls3;
	    });
}

REGISTER_UNIT_TEST("project.TestProjectLifetime", TestProjectLifetime);

}  // namespace
