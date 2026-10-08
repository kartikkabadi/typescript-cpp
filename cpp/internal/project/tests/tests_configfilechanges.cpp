// Port of tsc/internal/project/configfilechanges_test.go.
#include <memory>
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

namespace {

namespace assert = tsc::gotest::assert;
namespace lsproto = tsc::lsp::lsproto;
namespace projecttestutil = tsc::testutil::projecttestutil;
using tsc::gostd::testing::T;

const projecttestutil::FileMap& configChangeFiles() {
	static const projecttestutil::FileMap files{
	    {"/tsconfig.more-base.json", std::string("{}")},
	    {"/tsconfig.base.json",
	     std::string(
	         "{\"extends\": \"../tsconfig.more-base.json\", "
	         "\"compilerOptions\": {\"strict\": true}}")},
	    {"/src/tsconfig.json",
	     std::string("{\"extends\": \"../tsconfig.base.json\", "
	                 "\"compilerOptions\": {\"target\": \"es6\"}, "
	                 "\"references\": [{\"path\": \"../utils\"}]}")},
	    {"/src/index.ts",
	     std::string("console.log(\"Hello, world!\");")},
	    {"/src/subfolder/foo.ts",
	     std::string("export const foo = \"bar\";")},
	    {"/utils/tsconfig.json",
	     std::string("{\"compilerOptions\": {\"composite\": true}}")},
	    {"/utils/index.ts",
	     std::string("console.log(\"Hello, test!\");")},
	};
	return files;
}

std::string fileText(const projecttestutil::FileMap& files,
                     const std::string& name) {
	return std::get<std::string>(files.at(name));
}

void TestConfigFileChanges(T* t) {
	t->Parallel();

	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto& files = configChangeFiles();

	t->Run(
	    "should update program options on config file change",
	    [&](T* t) {
		    t->Parallel();
		    auto [session, utils] = projecttestutil::Setup(files);
		    session->DidOpenFile(
		        t->Context(), "file:///src/index.ts", 1,
		        fileText(files, "/src/index.ts"),
		        lsproto::LanguageKindTypeScript);
		    session->WaitForBackgroundTasks();

		    auto err = utils->FS()->WriteFile(
		        "/src/tsconfig.json",
		        "{\"extends\": \"../tsconfig.base.json\", "
		        "\"compilerOptions\": {\"target\": \"esnext\"}, "
		        "\"references\": [{\"path\": \"../utils\"}]}");
		    assert::Assert(t, err.impl() == nullptr);
		    lsproto::FileEvent ev{
		        .Uri = "file:///src/tsconfig.json",
		        .Type = lsproto::FileChangeTypeChanged,
		    };
		    session->DidChangeWatchedFiles(t->Context(), {&ev});
		    session->WaitForBackgroundTasks();

		    auto [ls, err2] = session->GetLanguageService(
		        t->Context(), "file:///src/index.ts");
		    assert::NilError(t, err2);
		    assert::Equal(t, ls->GetProgram()->Options()->Target,
		                  tsc::ScriptTarget::ESNext);
		    delete ls;
	    });

	t->Run(
	    "should update project on extended config file change",
	    [&](T* t) {
		    t->Parallel();
		    auto [session, utils] = projecttestutil::Setup(files);
		    session->DidOpenFile(
		        t->Context(), "file:///src/index.ts", 1,
		        fileText(files, "/src/index.ts"),
		        lsproto::LanguageKindTypeScript);

		    auto err = utils->FS()->WriteFile(
		        "/tsconfig.base.json",
		        "{\"compilerOptions\": {\"strict\": false}}");
		    assert::Assert(t, err.impl() == nullptr);
		    lsproto::FileEvent ev{
		        .Uri = "file:///tsconfig.base.json",
		        .Type = lsproto::FileChangeTypeChanged,
		    };
		    session->DidChangeWatchedFiles(t->Context(), {&ev});

		    auto [ls, err2] = session->GetLanguageService(
		        t->Context(), "file:///src/index.ts");
		    assert::NilError(t, err2);
		    assert::Equal(t, ls->GetProgram()->Options()->Strict,
		                  tsc::Tristate::False);
		    delete ls;
	    });

	t->Run(
	    "should update project on doubly extended config file "
	    "change",
	    [&](T* t) {
		    t->Parallel();
		    auto [session, utils] = projecttestutil::Setup(files);
		    session->DidOpenFile(
		        t->Context(), "file:///src/index.ts", 1,
		        fileText(files, "/src/index.ts"),
		        lsproto::LanguageKindTypeScript);

		    auto err = utils->FS()->WriteFile(
		        "/tsconfig.more-base.json",
		        "{\"compilerOptions\": {\"verbatimModuleSyntax\": "
		        "true}}");
		    assert::Assert(t, err.impl() == nullptr);
		    lsproto::FileEvent ev{
		        .Uri = "file:///tsconfig.more-base.json",
		        .Type = lsproto::FileChangeTypeChanged,
		    };
		    session->DidChangeWatchedFiles(t->Context(), {&ev});

		    auto [ls, err2] = session->GetLanguageService(
		        t->Context(), "file:///src/index.ts");
		    assert::NilError(t, err2);
		    assert::Equal(
		        t, ls->GetProgram()->Options()->VerbatimModuleSyntax,
		        tsc::Tristate::True);
		    delete ls;
	    });

	t->Run(
	    "should update project on referenced config file change",
	    [&](T* t) {
		    t->Parallel();
		    auto [session, utils] = projecttestutil::Setup(files);
		    session->DidOpenFile(
		        t->Context(), "file:///src/index.ts", 1,
		        fileText(files, "/src/index.ts"),
		        lsproto::LanguageKindTypeScript);
		    auto* snapshotBefore = session->Snapshot();

		    auto err = utils->FS()->WriteFile(
		        "/utils/tsconfig.json",
		        "{\"compilerOptions\": {\"composite\": true, "
		        "\"target\": \"esnext\"}}");
		    assert::Assert(t, err.impl() == nullptr);
		    lsproto::FileEvent ev{
		        .Uri = "file:///utils/tsconfig.json",
		        .Type = lsproto::FileChangeTypeChanged,
		    };
		    session->DidChangeWatchedFiles(t->Context(), {&ev});

		    auto [ls, err2] = session->GetLanguageService(
		        t->Context(), "file:///src/index.ts");
		    assert::NilError(t, err2);
		    delete ls;
		    auto* snapshotAfter = session->Snapshot();
		    assert::Assert(
		        t, snapshotAfter != snapshotBefore,
		        "Snapshot should be updated after config file change");
	    });

	t->Run(
	    "should close project on config file deletion", [&](T* t) {
		    t->Parallel();
		    auto [session, utils] = projecttestutil::Setup(files);
		    session->DidOpenFile(
		        t->Context(), "file:///src/index.ts", 1,
		        fileText(files, "/src/index.ts"),
		        lsproto::LanguageKindTypeScript);

		    auto err = utils->FS()->Remove("/src/tsconfig.json");
		    assert::Assert(t, err.impl() == nullptr);
		    lsproto::FileEvent ev{
		        .Uri = "file:///src/tsconfig.json",
		        .Type = lsproto::FileChangeTypeDeleted,
		    };
		    session->DidChangeWatchedFiles(t->Context(), {&ev});

		    auto [ls, err2] = session->GetLanguageService(
		        t->Context(), "file:///src/index.ts");
		    assert::NilError(t, err2);
		    delete ls;
		    auto* snapshot = session->Snapshot();
		    assert::Assert(
		        t, snapshot->ProjectCollection->Projects().size() == 1);
		    assert::Assert(
		        t,
		        snapshot->ProjectCollection->InferredProject() != nullptr);
	    });

	t->Run("config file creation then deletion", [&](T* t) {
		t->Parallel();
		auto [session, utils] = projecttestutil::Setup(files);
		session->DidOpenFile(
		    t->Context(), "file:///src/subfolder/foo.ts", 1,
		    fileText(files, "/src/subfolder/foo.ts"),
		    lsproto::LanguageKindTypeScript);

		auto err = utils->FS()->WriteFile("/src/subfolder/tsconfig.json",
		                                  "{}");
		assert::Assert(t, err.impl() == nullptr);
		lsproto::FileEvent ev{
		    .Uri = "file:///src/subfolder/tsconfig.json",
		    .Type = lsproto::FileChangeTypeCreated,
		};
		session->DidChangeWatchedFiles(t->Context(), {&ev});

		auto [ls, err2] = session->GetLanguageService(
		    t->Context(), "file:///src/subfolder/foo.ts");
		assert::NilError(t, err2);
		delete ls;
		auto* snapshot = session->Snapshot();
		assert::Equal(t, snapshot->ProjectCollection->Projects().size(),
		              (int64_t)2);
		assert::Equal(
		    t,
		    snapshot->GetDefaultProject("file:///src/subfolder/foo.ts")
		        ->ConfigFileName(),
		    std::string("/src/subfolder/tsconfig.json"));

		err = utils->FS()->Remove("/src/subfolder/tsconfig.json");
		assert::Assert(t, err.impl() == nullptr);
		lsproto::FileEvent ev2{
		    .Uri = "file:///src/subfolder/tsconfig.json",
		    .Type = lsproto::FileChangeTypeDeleted,
		};
		session->DidChangeWatchedFiles(t->Context(), {&ev2});

		auto [ls2, err3] = session->GetLanguageService(
		    t->Context(), "file:///src/subfolder/foo.ts");
		assert::NilError(t, err3);
		delete ls2;
		snapshot = session->Snapshot();
		assert::Equal(
		    t,
		    snapshot->GetDefaultProject("file:///src/subfolder/foo.ts")
		        ->ConfigFileName(),
		    std::string("/src/tsconfig.json"));
		assert::Equal(
		    t, snapshot->ProjectCollection->Projects().size(),
		    (int64_t)2); // Old project will be cleaned up on next
		                 // file open

		session->DidOpenFile(t->Context(), "file:///src/index.ts", 1,
		                     fileText(files, "/src/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		snapshot = session->Snapshot();
		assert::Equal(t, snapshot->ProjectCollection->Projects().size(),
		              (int64_t)1);
	});

	t->Run(
	    "should update project when missing extended config is "
	    "created",
	    [&](T* t) {
		    t->Parallel();
		    // Start with a project whose tsconfig extends a base config
		    // that doesn't exist yet
		    projecttestutil::FileMap missingBaseFiles;
		    for (auto& [k, v] : files) {
			    if (k == "/tsconfig.base.json") {
				    continue;
			    }
			    missingBaseFiles[k] = v;
		    }

		    auto [session, utils] =
		        projecttestutil::Setup(missingBaseFiles);
		    session->DidOpenFile(
		        t->Context(), "file:///src/index.ts", 1,
		        fileText(missingBaseFiles, "/src/index.ts"),
		        lsproto::LanguageKindTypeScript);

		    // Create the previously-missing base config file that is
		    // extended by /src/tsconfig.json
		    auto err = utils->FS()->WriteFile(
		        "/tsconfig.base.json",
		        "{\"compilerOptions\": {\"strict\": true}}");
		    assert::Assert(t, err.impl() == nullptr);
		    lsproto::FileEvent ev{
		        .Uri = "file:///tsconfig.base.json",
		        .Type = lsproto::FileChangeTypeCreated,
		    };
		    session->DidChangeWatchedFiles(t->Context(), {&ev});

		    // Accessing the language service should trigger project
		    // update
		    auto [ls, err2] = session->GetLanguageService(
		        t->Context(), "file:///src/index.ts");
		    assert::NilError(t, err2);
		    assert::Equal(t, ls->GetProgram()->Options()->Strict,
		                  tsc::Tristate::True);
		    delete ls;
	    });
}

REGISTER_UNIT_TEST("project.TestConfigFileChanges",
                   TestConfigFileChanges);

} // namespace
