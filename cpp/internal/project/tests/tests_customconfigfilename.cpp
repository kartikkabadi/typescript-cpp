// Port of tsc/internal/project/customconfigfilename_test.go.
#include <string>

#include "internal/bundled/bundled.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsutil/lsutil.h"
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
namespace lsutil = tsc::ls::lsutil;
namespace lsproto = tsc::lsp::lsproto;
namespace project = tsc::project;
namespace projecttestutil = tsc::testutil::projecttestutil;
using tsc::gostd::testing::T;

const projecttestutil::FileMap& customConfigFiles() {
	static projecttestutil::FileMap files{
	    {"/src/tsconfig.json",
	     std::string("{\"compilerOptions\": {\"strict\": false}}")},
	    {"/src/tsconfig.all.json",
	     std::string("{\"compilerOptions\": {\"strict\": true}}")},
	    {"/src/index.ts", std::string("export const x = 1;")},
	};
	return files;
}

const std::string& customConfigFileText(
    const projecttestutil::FileMap& files, const std::string& name) {
	return std::get<std::string>(files.at(name));
}

void TestCustomConfigFileName(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto& files = customConfigFiles();
	lsproto::DocumentUri uri = "file:///src/index.ts";

	t->Run("picks up custom config and switches on preference change",
	       [&files, &uri](T* t) {
		t->Parallel();
		auto [session, utils] = projecttestutil::Setup(files);

		session->DidOpenFile(
		    t->Context(), uri, 1,
		    customConfigFileText(files, "/src/index.ts"),
		    lsproto::LanguageKindTypeScript);
		auto [ls, err] = session->GetLanguageService(t->Context(), uri);
		assert::NilError(t, err);

		auto* snapshot = session->Snapshot();
		assert::Equal(
		    t, snapshot->GetDefaultProject(uri)->ConfigFileName(),
		    std::string("/src/tsconfig.json"));
		assert::Equal(t, ls->GetProgram()->Options()->Strict,
		              tsc::Tristate::False);

		auto prefs = lsutil::NewDefaultUserPreferences();
		prefs.CustomConfigFileName = "tsconfig.all.json";
		session->Configure(prefs);

		auto [ls2, err2] = session->GetLanguageService(t->Context(), uri);
		assert::NilError(t, err2);

		snapshot = session->Snapshot();
		assert::Equal(
		    t, snapshot->GetDefaultProject(uri)->ConfigFileName(),
		    std::string("/src/tsconfig.all.json"));
		assert::Equal(t, ls2->GetProgram()->Options()->Strict,
		              tsc::Tristate::True);
		delete ls;
		delete ls2;
	});

	t->Run("uses tsconfig.json when customConfigFileName is empty",
	       [&files, &uri](T* t) {
		t->Parallel();
		auto [session, utils] = projecttestutil::Setup(files);

		auto prefs = lsutil::NewDefaultUserPreferences();
		// default for CustomConfigFileName is "".
		assert::Equal(t, prefs.CustomConfigFileName, std::string(""));
		session->Configure(prefs);

		session->DidOpenFile(
		    t->Context(), uri, 1,
		    customConfigFileText(files, "/src/index.ts"),
		    lsproto::LanguageKindTypeScript);
		auto [ls, err] = session->GetLanguageService(t->Context(), uri);
		assert::NilError(t, err);

		auto* snapshot = session->Snapshot();
		assert::Equal(
		    t, snapshot->GetDefaultProject(uri)->ConfigFileName(),
		    std::string("/src/tsconfig.json"));
		delete ls;
	});

	t->Run("falls back to tsconfig.json when custom config missing",
	       [&files, &uri](T* t) {
		t->Parallel();
		auto [session, utils] = projecttestutil::Setup(files);

		auto prefs = lsutil::NewDefaultUserPreferences();
		prefs.CustomConfigFileName = "tsconfig.nonexistent.json";
		session->Configure(prefs);

		session->DidOpenFile(
		    t->Context(), uri, 1,
		    customConfigFileText(files, "/src/index.ts"),
		    lsproto::LanguageKindTypeScript);
		auto [ls, err] = session->GetLanguageService(t->Context(), uri);
		assert::NilError(t, err);

		auto* snapshot = session->Snapshot();
		assert::Equal(
		    t, snapshot->GetDefaultProject(uri)->ConfigFileName(),
		    std::string("/src/tsconfig.json"));
		delete ls;
	});

	t->Run("reverts to tsconfig.json when custom config preference is cleared",
	       [&files, &uri](T* t) {
		t->Parallel();
		auto [session, utils] = projecttestutil::Setup(files);

		// Step 1: Open file, verify it uses tsconfig.json (strict: false)
		session->DidOpenFile(
		    t->Context(), uri, 1,
		    customConfigFileText(files, "/src/index.ts"),
		    lsproto::LanguageKindTypeScript);
		auto [ls, err] = session->GetLanguageService(t->Context(), uri);
		assert::NilError(t, err);

		auto* snapshot = session->Snapshot();
		assert::Equal(
		    t, snapshot->GetDefaultProject(uri)->ConfigFileName(),
		    std::string("/src/tsconfig.json"));
		assert::Equal(t, ls->GetProgram()->Options()->Strict,
		              tsc::Tristate::False);
		delete ls;

		// Step 2: Switch to custom config (strict: true)
		auto prefs = lsutil::NewDefaultUserPreferences();
		prefs.CustomConfigFileName = "tsconfig.all.json";
		session->Configure(prefs);

		auto [ls2, err2] = session->GetLanguageService(t->Context(), uri);
		assert::NilError(t, err2);

		snapshot = session->Snapshot();
		assert::Equal(
		    t, snapshot->GetDefaultProject(uri)->ConfigFileName(),
		    std::string("/src/tsconfig.all.json"));
		assert::Equal(t, ls2->GetProgram()->Options()->Strict,
		              tsc::Tristate::True);
		delete ls2;

		// Step 3: Clear custom config preference, should revert to tsconfig.json (strict: false)
		prefs = lsutil::NewDefaultUserPreferences();
		prefs.CustomConfigFileName = "";
		session->Configure(prefs);

		auto [ls3, err3] = session->GetLanguageService(t->Context(), uri);
		assert::NilError(t, err3);

		snapshot = session->Snapshot();
		assert::Equal(
		    t, snapshot->GetDefaultProject(uri)->ConfigFileName(),
		    std::string("/src/tsconfig.json"));
		assert::Equal(t, ls3->GetProgram()->Options()->Strict,
		              tsc::Tristate::False);
		delete ls3;
	});

	// This test demonstrates the bug reported in #2020: after changing
	// customConfigFileName, the server does not schedule a diagnostics refresh,
	// so the VS Code client never knows to re-pull diagnostics and shows stale results.
	t->Run("schedules diagnostics refresh when custom config preference changes",
	       [&files, &uri](T* t) {
		t->Parallel();
		auto [session, utils] = projecttestutil::Setup(files);

		session->DidOpenFile(
		    t->Context(), uri, 1,
		    customConfigFileText(files, "/src/index.ts"),
		    lsproto::LanguageKindTypeScript);
		auto [ls, err] = session->GetLanguageService(t->Context(), uri);
		assert::NilError(t, err);
		session->WaitForBackgroundTasks();

		// Record baseline refresh call count
		int64_t baselineRefreshCount =
		    utils->Client()->RefreshDiagnosticsCalls().size();

		// Change the custom config preference
		auto prefs = lsutil::NewDefaultUserPreferences();
		prefs.CustomConfigFileName = "tsconfig.all.json";
		session->Configure(prefs);

		// GetLanguageService triggers the snapshot update with the new config
		auto [ls2, err2] = session->GetLanguageService(t->Context(), uri);
		assert::NilError(t, err2);
		session->WaitForBackgroundTasks();

		// The server should have scheduled a diagnostics refresh to tell the client
		// to re-pull diagnostics with the new project configuration.
		int64_t refreshCount =
		    utils->Client()->RefreshDiagnosticsCalls().size();
		assert::Assert(
		    t, refreshCount > baselineRefreshCount,
		    gostd::sprintf(
		        "expected RefreshDiagnostics to be called after customConfigFileName change, got %d calls (baseline %d)",
		        {refreshCount, baselineRefreshCount}));
		delete ls;
		delete ls2;
	});

	t->Run("rejects path traversal in customConfigFileName", [](T* t) {
		t->Parallel();
		for (const char* invalidName : {
		         "/etc/passwd",
		         "../tsconfig.json",
		         "configs/tsconfig.all.json",
		         "..\\tsconfig.json",
		         "sub\\dir\\tsconfig.json",
		         "..",
		         ".",
		     }) {
			auto prefs = lsutil::ParseUserPreferences({
			    {"js/ts",
			     lsutil::JsonAny{std::map<std::string, lsutil::JsonAny>{
			         {"customConfigFileName", invalidName}}}},
			});
			assert::Equal(
			    t, prefs.CustomConfigFileName, std::string(""),
			    gostd::sprintf(
			        "expected customConfigFileName to be cleared for invalid value %q",
			        {std::string(invalidName)}));
		}
	});

	t->Run("accepts plain base file names in customConfigFileName",
	       [](T* t) {
		t->Parallel();
		for (const char* validName : {
		         "tsconfig.all.json",
		         "tsconfig.editor.json",
		         "jsconfig.custom.json",
		     }) {
			auto prefs = lsutil::ParseUserPreferences({
			    {"js/ts",
			     lsutil::JsonAny{std::map<std::string, lsutil::JsonAny>{
			         {"customConfigFileName", validName}}}},
			});
			assert::Equal(
			    t, prefs.CustomConfigFileName, std::string(validName),
			    gostd::sprintf(
			        "expected customConfigFileName to be %q",
			        {std::string(validName)}));
		}
	});

	t->Run("cleans up inferred project when custom config covers file",
	       [](T* t) {
		t->Parallel();

		// Start without any tsconfig.json so file goes into inferred project, then
		// add a custom config that covers the file and verify it moves out of the
		// inferred project (not just getting a new default, but actually cleaned up).
		projecttestutil::FileMap filesNoConfig{
		    {"/src/tsconfig.all.json",
		     std::string(
		         "{\"compilerOptions\": {\"strict\": true}, \"include\": [\"./**/*\"]}")},
		    {"/src/index.ts", std::string("export const x = 1;")},
		};
		lsproto::DocumentUri uriLocal = "file:///src/index.ts";
		auto [session, utils] = projecttestutil::Setup(filesNoConfig);

		session->DidOpenFile(
		    t->Context(), uriLocal, 1,
		    customConfigFileText(filesNoConfig, "/src/index.ts"),
		    lsproto::LanguageKindTypeScript);
		auto [ls, err] =
		    session->GetLanguageService(t->Context(), uriLocal);
		assert::NilError(t, err);

		// Without any config, the file should be in the inferred project only.
		auto* snapshot = session->Snapshot();
		assert::Equal(
		    t, snapshot->GetDefaultProject(uriLocal)->ID(),
		    project::ID(project::inferredProjectName));
		auto projects =
		    snapshot->GetLanguageServiceProjectsContainingFile(
		        uriLocal);
		assert::Equal(
		    t, static_cast<int64_t>(projects.size()), int64_t(1),
		    gostd::sprintf(
		        "expected file to be in exactly 1 project before config change, got %d",
		        {static_cast<int64_t>(projects.size())}));

		// Now set custom config to pick up tsconfig.all.json
		auto prefs = lsutil::NewDefaultUserPreferences();
		prefs.CustomConfigFileName = "tsconfig.all.json";
		session->Configure(prefs);

		auto [ls2, err2] =
		    session->GetLanguageService(t->Context(), uriLocal);
		assert::NilError(t, err2);

		// File should now be in the configured project only, not duplicated in inferred.
		snapshot = session->Snapshot();
		assert::Equal(
		    t, snapshot->GetDefaultProject(uriLocal)->ConfigFileName(),
		    std::string("/src/tsconfig.all.json"));
		projects =
		    snapshot->GetLanguageServiceProjectsContainingFile(
		        uriLocal);
		assert::Equal(
		    t, static_cast<int64_t>(projects.size()), int64_t(1),
		    gostd::sprintf(
		        "expected file to be in exactly 1 project after config change, got %d",
		        {static_cast<int64_t>(projects.size())}));
		delete ls;
		delete ls2;
	});
}

REGISTER_UNIT_TEST("project.TestCustomConfigFileName",
                   TestCustomConfigFileName);

}  // namespace
