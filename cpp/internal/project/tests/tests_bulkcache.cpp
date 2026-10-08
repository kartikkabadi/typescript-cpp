// Port of tsc/internal/project/bulkcache_test.go.
#include <memory>
#include <string>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/ls/ls.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/project/project.h"
#include "internal/project/session.h"
#include "internal/project/snapshot.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace gostd = tsc::gostd;
namespace lsproto = tsc::lsp::lsproto;
namespace project = tsc::project;
namespace projecttestutil = tsc::testutil::projecttestutil;
using tsc::gostd::testing::T;

// generateFileEvents — helper to generate excessive file change events.
std::vector<lsproto::FileEvent*> generateFileEvents(
    int count, const std::string& pathTemplate,
    lsproto::FileChangeType changeType) {
	std::vector<lsproto::FileEvent*> events;
	for (int i = 0; i < count; i++) {
		auto* ev = new lsproto::FileEvent();
		ev->Uri = lsproto::DocumentUri(gostd::sprintf(
		    pathTemplate, {int64_t(i)}));
		ev->Type = changeType;
		events.push_back(ev);
	}
	return events;
}

const projecttestutil::FileMap& bulkCacheBaseFiles() {
	static projecttestutil::FileMap files{
	    {"/project/tsconfig.json", std::string(R"({
			"compilerOptions": {
				"strict": true,
				"target": "es2015",
				"types": ["node"]
			},
			"include": ["src/**/*"]
		})")},
	    {"/project/src/index.ts",
	     std::string(
	         "import { helper } from \"./helper\"; console.log(helper);")},
	    {"/project/src/helper.ts",
	     std::string("export const helper = \"test\";")},
	    {"/project/src/utils/lib.ts",
	     std::string("export function util() { return \"util\"; }")},
	    {"/project/node_modules/@types/node/index.d.ts",
	     std::string("import \"./fs\"; import \"./console\";")},
	    {"/project/node_modules/@types/node/fs.d.ts", std::string("")},
	    {"/project/node_modules/@types/node/console.d.ts",
	     std::string("")},
	};
	return files;
}

const std::string& bulkFileText(const projecttestutil::FileMap& files,
                                const std::string& name) {
	return std::get<std::string>(files.at(name));
}

void TestBulkCacheInvalidation(T* t) {
	t->Parallel();

	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto& baseFiles = bulkCacheBaseFiles();

	t->Run(
	    "large number of node_modules changes invalidates only node_modules cache",
	    [&baseFiles](T* t) {
		    t->Parallel();
		    auto test = [&baseFiles](
		        T* t, std::vector<lsproto::FileEvent*> fileEvents,
		        bool expectNodeModulesInvalidation) {
			    auto [session, utils] =
			        projecttestutil::Setup(baseFiles);

			    // Open a file to create the project
			    session->DidOpenFile(
			        t->Context(), "file:///project/src/index.ts", 1,
			        bulkFileText(baseFiles, "/project/src/index.ts"),
			        lsproto::LanguageKindTypeScript);

			    // Get initial snapshot and verify config
			    auto [ls, err] = session->GetLanguageService(
			        t->Context(), "file:///project/src/index.ts");
			    assert::NilError(t, err);
			    assert::Equal(t, ls->GetProgram()->Options()->Target,
			                  tsc::ScriptTarget::ES2015);

			    auto* snapshotBefore = session->Snapshot();
			    auto* configBefore =
			        snapshotBefore->ConfigFileRegistry;

			    // Update tsconfig.json on disk to test that configs don't get reloaded
			    auto werr = utils->FS()->WriteFile(
			        "/project/tsconfig.json", R"({
			"compilerOptions": {
				"strict": true,
				"target": "esnext",
				"types": ["node"]
			},
			"include": ["src/**/*"]
		})");
			    assert::Assert(t, werr.impl() == nullptr);
			    // Update fs.d.ts in node_modules
			    werr = utils->FS()->WriteFile(
			        "/project/node_modules/@types/node/fs.d.ts",
			        "new text");
			    assert::Assert(t, werr.impl() == nullptr);

			    // Process the excessive node_modules changes
			    session->DidChangeWatchedFiles(t->Context(),
			                                   fileEvents);

			    // Get language service again to trigger snapshot update
			    auto [ls2, err2] = session->GetLanguageService(
			        t->Context(), "file:///project/src/index.ts");
			    assert::NilError(t, err2);

			    auto* snapshotAfter = session->Snapshot();
			    auto* configAfter =
			        snapshotAfter->ConfigFileRegistry;

			    // Config should NOT have been reloaded (target should remain ES2015, not esnext)
			    assert::Equal(
			        t, ls2->GetProgram()->Options()->Target,
			        tsc::ScriptTarget::ES2015,
			        "Config should not have been reloaded for node_modules-only changes");

			    // Config registry should be the same instance (no configs reloaded)
			    assert::Equal(
			        t, configBefore, configAfter,
			        "Config registry should not have changed for node_modules-only changes");

			    auto* fsDtsFile = snapshotAfter->GetFile(
			        "/project/node_modules/@types/node/fs.d.ts");
			    auto fsDtsText = fsDtsFile->Content();
			    if (expectNodeModulesInvalidation) {
				    assert::Equal(t, fsDtsText,
				                  std::string("new text"));
			    } else {
				    assert::Equal(t, fsDtsText, std::string(""));
			    }
			    delete ls;
			    delete ls2;
			    for (auto* ev : fileEvents) delete ev;
		    };

		    t->Run("with file existing in cache", [&](T* t) {
			    t->Parallel();
			    auto fileEvents = generateFileEvents(
			        1001,
			        "file:///project/node_modules/generated/file%d.js",
			        lsproto::FileChangeTypeCreated);
			    // Include two files in the program to trigger a full program creation.
			    // Exclude fs.d.ts to show that its content still gets invalidated.
			    {
				    auto* ev = new lsproto::FileEvent();
				    ev->Uri = "file:///project/node_modules/@types/node/index.d.ts";
				    ev->Type = lsproto::FileChangeTypeChanged;
				    fileEvents.push_back(ev);
			    }
			    {
				    auto* ev = new lsproto::FileEvent();
				    ev->Uri = "file:///project/node_modules/@types/node/console.d.ts";
				    ev->Type = lsproto::FileChangeTypeChanged;
				    fileEvents.push_back(ev);
			    }

			    test(t, fileEvents, true);
		    });

		    t->Run("without file existing in cache", [&](T* t) {
			    t->Parallel();
			    auto fileEvents = generateFileEvents(
			        1001,
			        "file:///project/node_modules/generated/file%d.js",
			        lsproto::FileChangeTypeCreated);
			    test(t, fileEvents, false);
		    });
	    });

	t->Run("large number of changes outside node_modules",
	       [&baseFiles](T* t) {
		    t->Parallel();
		    auto test = [&baseFiles](
		        T* t, std::vector<lsproto::FileEvent*> fileEvents,
		        bool expectConfigReload) {
			    auto [session, utils] =
			        projecttestutil::Setup(baseFiles);

			    // Open a file to create the project
			    session->DidOpenFile(
			        t->Context(), "file:///project/src/index.ts", 1,
			        bulkFileText(baseFiles, "/project/src/index.ts"),
			        lsproto::LanguageKindTypeScript);

			    // Get initial state
			    auto [ls, err] = session->GetLanguageService(
			        t->Context(), "file:///project/src/index.ts");
			    assert::NilError(t, err);
			    assert::Equal(t, ls->GetProgram()->Options()->Target,
			                  tsc::ScriptTarget::ES2015);

			    // Update tsconfig.json on disk
			    auto werr = utils->FS()->WriteFile(
			        "/project/tsconfig.json", R"({
			"compilerOptions": {
				"strict": true,
				"target": "esnext",
				"types": ["node"]
			},
			"include": ["src/**/*"]
		})");
			    assert::Assert(t, werr.impl() == nullptr);
			    // Add root file
			    werr = utils->FS()->WriteFile(
			        "/project/src/rootFile.ts",
			        "console.log(\"root file\")");
			    assert::Assert(t, werr.impl() == nullptr);

			    session->DidChangeWatchedFiles(t->Context(),
			                                   fileEvents);
			    auto [ls2, err2] = session->GetLanguageService(
			        t->Context(), "file:///project/src/index.ts");
			    assert::NilError(t, err2);

			    if (expectConfigReload) {
				    assert::Equal(
				        t, ls2->GetProgram()->Options()->Target,
				        tsc::ScriptTarget::ESNext,
				        "Config should have been reloaded for changes outside node_modules");
				    assert::Check(
				        t,
				        ls2->GetProgram()->GetSourceFile(
				            "/project/src/rootFile.ts") != nullptr,
				        "New root file should be present");
			    } else {
				    assert::Equal(
				        t, ls2->GetProgram()->Options()->Target,
				        tsc::ScriptTarget::ES2015,
				        "Config should not have been reloaded for changes outside node_modules");
				    assert::Check(
				        t,
				        ls2->GetProgram()->GetSourceFile(
				            "/project/src/rootFile.ts") == nullptr,
				        "New root file should not be present");
			    }
			    delete ls;
			    delete ls2;
			    for (auto* ev : fileEvents) delete ev;
		    };

		    t->Run("with event matching include glob", [&](T* t) {
			    t->Parallel();
			    auto fileEvents = generateFileEvents(
			        1001, "file:///project/generated/file%d.ts",
			        lsproto::FileChangeTypeCreated);
			    {
				    auto* ev = new lsproto::FileEvent();
				    ev->Uri = "file:///project/src/rootFile.ts";
				    ev->Type = lsproto::FileChangeTypeCreated;
				    fileEvents.push_back(ev);
			    }
			    test(t, fileEvents, true);
		    });

		    t->Run("without event matching include glob", [&](T* t) {
			    t->Parallel();
			    auto fileEvents = generateFileEvents(
			        1001, "file:///project/generated/file%d.ts",
			        lsproto::FileChangeTypeCreated);
			    test(t, fileEvents, false);
		    });
	    });

	t->Run(
	    "large number of changes outside node_modules causes project reevaluation",
	    [&baseFiles](T* t) {
		    t->Parallel();
		    auto [session, utils] = projecttestutil::Setup(baseFiles);

		    // Open a file that will initially use the root tsconfig
		    session->DidOpenFile(
		        t->Context(), "file:///project/src/utils/lib.ts", 1,
		        bulkFileText(baseFiles, "/project/src/utils/lib.ts"),
		        lsproto::LanguageKindTypeScript);

		    // Initially, the file should use the root project (strict mode)
		    auto* snapshot = session->Snapshot();
		    auto* initialProject = snapshot->GetDefaultProject(
		        "file:///project/src/utils/lib.ts");
		    assert::Equal(
		        t, initialProject->ConfigFileName(),
		        std::string("/project/tsconfig.json"),
		        "Should initially use root tsconfig");

		    // Get language service to verify initial strict mode
		    auto [ls, err] = session->GetLanguageService(
		        t->Context(), "file:///project/src/utils/lib.ts");
		    assert::NilError(t, err);
		    assert::Equal(
		        t, ls->GetProgram()->Options()->Strict,
		        tsc::Tristate::True,
		        "Should initially use strict mode from root config");

		    // Now create the nested tsconfig (this would normally be detected, but we'll simulate a missed event)
		    auto werr = utils->FS()->WriteFile(
		        "/project/src/utils/tsconfig.json", R"({
			"compilerOptions": {
				"strict": false,
				"target": "esnext"
			}
		})");
		    assert::Assert(t, werr.impl() == nullptr);

		    // Create excessive changes to trigger bulk invalidation
		    auto fileEvents = generateFileEvents(
		        1001, "file:///project/src/generated/file%d.ts",
		        lsproto::FileChangeTypeCreated);

		    // Process the excessive changes - this should trigger project reevaluation
		    session->DidChangeWatchedFiles(t->Context(), fileEvents);

		    // Get language service - this should now find the nested config and switch projects
		    auto [ls2, err2] = session->GetLanguageService(
		        t->Context(), "file:///project/src/utils/lib.ts");
		    assert::NilError(t, err2);

		    snapshot = session->Snapshot();
		    auto* newProject = snapshot->GetDefaultProject(
		        "file:///project/src/utils/lib.ts");

		    // The file should now use the nested tsconfig
		    assert::Equal(
		        t, newProject->ConfigFileName(),
		        std::string("/project/src/utils/tsconfig.json"),
		        "Should now use nested tsconfig after bulk invalidation");
		    assert::Equal(
		        t, ls2->GetProgram()->Options()->Strict,
		        tsc::Tristate::False,
		        "Should now use non-strict mode from nested config");
		    assert::Equal(
		        t, ls2->GetProgram()->Options()->Target,
		        tsc::ScriptTarget::ESNext,
		        "Should use esnext target from nested config");
		    delete ls;
		    delete ls2;
		    for (auto* ev : fileEvents) delete ev;
	    });

	t->Run("config file names cache", [](T* t) {
		t->Parallel();
		auto test = [](T* t,
		               std::vector<lsproto::FileEvent*> fileEvents,
		               bool expectConfigDiscovery,
		               const std::string& testName) {
			projecttestutil::FileMap files{
			    {"/project/src/index.ts",
			     std::string(
			         "console.log(\"test\");")},  // No tsconfig initially
			};
			auto [session, utils] = projecttestutil::Setup(files);

			// Open file without tsconfig - should create inferred project
			session->DidOpenFile(
			    t->Context(), "file:///project/src/index.ts", 1,
			    bulkFileText(files, "/project/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			auto* snapshot = session->Snapshot();
			assert::Assert(
			    t,
			    snapshot->ProjectCollection->InferredProject() !=
			        nullptr,
			    "Should have inferred project");
			assert::Equal(
			    t,
			    snapshot
			        ->GetDefaultProject(
			            "file:///project/src/index.ts")
			        ->Kind,
			    project::KindInferred);

			// Create a tsconfig that would affect this file (simulating a missed creation event)
			auto werr = utils->FS()->WriteFile(
			    "/project/tsconfig.json", R"({
		"compilerOptions": {
			"strict": true
		},
		"include": ["src/**/*"]
	})");
			assert::Assert(t, werr.impl() == nullptr);

			// Process the changes
			session->DidChangeWatchedFiles(t->Context(),
			                               fileEvents);

			// Get language service to trigger config discovery
			auto [ls, err] = session->GetLanguageService(
			    t->Context(), "file:///project/src/index.ts");
			assert::NilError(t, err);

			snapshot = session->Snapshot();
			auto* newProject = snapshot->GetDefaultProject(
			    "file:///project/src/index.ts");

			// Check expected behavior
			if (expectConfigDiscovery) {
				// Should now use configured project instead of inferred
				assert::Equal(
				    t, newProject->Kind,
				    project::KindConfigured,
				    "Should now use configured project after cache invalidation");
				assert::Equal(
				    t, newProject->ConfigFileName(),
				    std::string("/project/tsconfig.json"),
				    "Should use the newly discovered tsconfig");
			} else {
				// Should still use inferred project (config file names cache not cleared)
				assert::Assert(
				    t,
				    newProject ==
				        snapshot->ProjectCollection
				            ->InferredProject(),
				    "Should still use inferred project after node_modules-only changes");
			}
			delete ls;
			for (auto* ev : fileEvents) delete ev;
		};

		t->Run(
		    "excessive changes only in node_modules does not affect config file names cache",
		    [&](T* t) {
			    t->Parallel();
			    auto fileEvents = generateFileEvents(
			        1001,
			        "file:///project/node_modules/generated/file%d.js",
			        lsproto::FileChangeTypeCreated);
			    test(t, fileEvents, false,
			         "node_modules changes should not clear config cache");
		    });

		t->Run(
		    "excessive changes outside node_modules clears config file names cache",
		    [&](T* t) {
			    t->Parallel();
			    auto fileEvents = generateFileEvents(
			        1001,
			        "file:///project/src/generated/file%d.ts",
			        lsproto::FileChangeTypeCreated);
			    // Presence of any tsconfig.json file event triggers rediscovery for config for all open files
			    {
				    auto* ev = new lsproto::FileEvent();
				    ev->Uri = lsproto::DocumentUri(
				        "file:///project/src/generated/tsconfig.json");
				    ev->Type = lsproto::FileChangeTypeCreated;
				    fileEvents.push_back(ev);
			    }
			    test(t, fileEvents, true,
			         "non-node_modules changes should clear config cache");
		    });
	});

	// Simulate external build tool changing files in dist/ (not included by any project)
	t->Run("excessive changes in dist folder do not invalidate",
	       [](T* t) {
		    t->Parallel();
		    projecttestutil::FileMap files{
		        {"/project/src/index.ts",
		         std::string(
		             "console.log(\"test\");")},  // No tsconfig initially
		    };
		    auto [session, utils] = projecttestutil::Setup(files);

		    // Open file without tsconfig - should create inferred project
		    session->DidOpenFile(
		        t->Context(), "file:///project/src/index.ts", 1,
		        bulkFileText(files, "/project/src/index.ts"),
		        lsproto::LanguageKindTypeScript);

		    auto* snapshot = session->Snapshot();
		    assert::Equal(
		        t,
		        snapshot
		            ->GetDefaultProject(
		                "file:///project/src/index.ts")
		            ->Kind,
		        project::KindInferred);

		    // Create a tsconfig that would affect this file (simulating a missed creation event)
		    // This should NOT be discovered after dist-folder changes
		    auto werr = utils->FS()->WriteFile(
		        "/project/tsconfig.json", R"({
			"compilerOptions": {
				"strict": true
			},
			"include": ["src/**/*"]
		})");
		    assert::Assert(t, werr.impl() == nullptr);

		    // Create excessive changes in dist folder only
		    auto fileEvents = generateFileEvents(
		        1001, "file:///project/dist/generated/file%d.js",
		        lsproto::FileChangeTypeCreated);
		    session->DidChangeWatchedFiles(t->Context(), fileEvents);

		    // File should still use inferred project (config file names cache NOT cleared for dist changes)
		    auto [ls, err] = session->GetLanguageService(
		        t->Context(), "file:///project/src/index.ts");
		    assert::NilError(t, err);

		    snapshot = session->Snapshot();
		    auto* newProject = snapshot->GetDefaultProject(
		        "file:///project/src/index.ts");
		    assert::Equal(
		        t, newProject->Kind, project::KindInferred,
		        "dist-folder changes should not cause config discovery");
		    // This assertion will fail until we implement logic to ignore dist folder changes
		    delete ls;
		    for (auto* ev : fileEvents) delete ev;
	    });

	// Regression test for https://github.com/microsoft/TypeScript/tsc/issues/4545
	//
	// A config file entry can be retained (here, by an open file whose default
	// project search fanned out to a referenced config) while its commandLine is
	// nil, because the referenced config file does not exist. A bulk cache
	// invalidation triggered by an excessive number of watch events ranges over
	// all config entries and used to dereference entry.commandLine.ConfigFile
	// without a nil check, crashing.
	//
	// The nil-check-avoiding short circuit `!ok || text != entry.commandLine...`
	// only protects the case where the file cannot be read, so the config file
	// must exist on disk (readable) while the entry's commandLine is still nil to
	// reach the crash.
	t->Run(
	    "bulk invalidation with retained config whose command line is nil",
	    [](T* t) {
		    t->Parallel();
		    std::string appConfig = R"({
			"compilerOptions": { "composite": true, "target": "esnext" },
			"include": ["**/*"]
		})";
		    projecttestutil::FileMap files{
		        // Solution config references ./app, but app/tsconfig.json does not exist.
		        {"/project/tsconfig.json", std::string(R"({
				"compilerOptions": { "composite": true },
				"files": [],
				"references": [{ "path": "./app" }]
			})")},
		        {"/project/app/main.ts",
		         std::string("export const main = 1;")},
		    };

		    auto [session, utils] = projecttestutil::Setup(files);

		    // Open a file in the (non-existent) referenced project. The default project
		    // search fans out to app/tsconfig.json, creating a retained config entry
		    // with commandLine == nil and pendingReload == None.
		    session->DidOpenFile(
		        t->Context(), "file:///project/app/main.ts", 1,
		        bulkFileText(files, "/project/app/main.ts"),
		        lsproto::LanguageKindTypeScript);
		    auto [ls, err] = session->GetLanguageService(
		        t->Context(), "file:///project/app/main.ts");
		    assert::NilError(t, err);
		    delete ls;

		    // Create the referenced config on disk WITHOUT notifying, so the
		    // nil-commandLine entry is not reloaded (pendingReload stays None) but the
		    // file becomes readable.
		    auto werr = utils->FS()->WriteFile(
		        "/project/app/tsconfig.json", appConfig);
		    assert::Assert(t, werr.impl() == nullptr);

		    // Trigger a bulk cache invalidation with an excessive number of watch events.
		    // The creation of a config file drives the excessive-change path into
		    // invalidateCache, which ranges over all config entries -- including the one
		    // whose commandLine is nil -- and used to crash.
		    auto fileEvents = generateFileEvents(
		        1001, "file:///project/app/generated/file%d.ts",
		        lsproto::FileChangeTypeCreated);
		    {
			    auto* ev = new lsproto::FileEvent();
			    ev->Uri = "file:///project/newdir/tsconfig.json";
			    ev->Type = lsproto::FileChangeTypeCreated;
			    fileEvents.push_back(ev);
		    }
		    session->DidChangeWatchedFiles(t->Context(), fileEvents);
		    auto [ls2, err2] = session->GetLanguageService(
		        t->Context(), "file:///project/app/main.ts");
		    assert::NilError(t, err2);
		    delete ls2;
		    for (auto* ev : fileEvents) delete ev;
	    });
}

REGISTER_UNIT_TEST("project.TestBulkCacheInvalidation",
                   TestBulkCacheInvalidation);

}  // namespace
