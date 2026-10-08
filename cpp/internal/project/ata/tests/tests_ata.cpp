// Port of tsc/internal/project/ata/ata_test.go.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/ls/ls.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/project/session.h"
#include "internal/project/sessiontypes.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace gostd = tsc::gostd;
namespace lsp = tsc::lsp;
namespace ls = tsc::ls;
namespace lsproto = tsc::lsp::lsproto;
namespace lsutil = tsc::ls::lsutil;
namespace project = tsc::project;
namespace projecttestutil = tsc::testutil::projecttestutil;
using tsc::gostd::testing::T;
using projecttestutil::FileMap;

static bool argsContain(const std::vector<std::string>& args,
                        const std::string& val) {
	return std::find(args.begin(), args.end(), val) != args.end();
}

void TestATA(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	t->Run("local module should not be picked up", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/user/username/projects/project/app.js",
		     "const c = require('./config');"},
		    {"/user/username/projects/project/config.js",
		     "export let x = 1"},
		    {"/user/username/projects/project/jsconfig.json",
		     R"({
					"compilerOptions": { "moduleResolution": "commonjs" },
					"typeAcquisition": { "enable": true }
			})"},
		};

		auto testOptions = std::make_shared<
		    projecttestutil::TypingsInstallerOptions>();
		testOptions->TypesRegistry = {"config"};

		auto p_0 =
		    projecttestutil::SetupWithTypingsInstaller(files, testOptions);
	project::Session* session = p_0.first;
	auto& utils = p_0.second;
		t->Cleanup([session] { session->Close(); });
		lsproto::DocumentUri uri(
		    "file:///user/username/projects/project/app.js");
		std::string content =
		    std::get<std::string>(files["/user/username/projects/project/app.js"]);

		// Open the file
		session->DidOpenFile(t->Context(), uri, 1, content,
		                     lsproto::LanguageKindJavaScript);
		session->WaitForBackgroundTasks();
		auto p_1 = session->GetLanguageService(t->Context(), uri);
	auto* ls_ = p_1.first;
	auto& err = p_1.second;
		assert::NilError(t, err);
		// Verify the local config.js file is included in the program
		auto* program = ls_->GetProgram();
		assert::Assert(t, program != nullptr);
		auto* configFile = program->GetSourceFile(
		    "/user/username/projects/project/config.js");
		assert::Assert(t, configFile != nullptr,
		               "local config.js should be included");
		delete ls_;

		// Verify that only types-registry was installed (no @types/config
		// since it's a local module)
		auto npmCalls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(npmCalls.size()), int64_t(1));
		assert::Equal(t, npmCalls[0].Args[2],
		              std::string("types-registry@latest"));
	});

	t->Run("configured projects", [&](T* t) {
		t->Parallel();

		FileMap files{
		    {"/user/username/projects/project/app.js", ""},
		    {"/user/username/projects/project/tsconfig.json",
		     R"({
				"compilerOptions": { "allowJs": true },
				"typeAcquisition": { "enable": true },
			})"},
		    {"/user/username/projects/project/package.json",
		     R"({
				"name": "test",
				"dependencies": {
					"jquery": "^3.1.0"
				}
			})"},
		};

		auto tiOptions = std::make_shared<
		    projecttestutil::TypingsInstallerOptions>();
		tiOptions->PackageToFile = {
		    {"jquery", "declare const $: { x: number }"},
		};
		auto p_2 =
		    projecttestutil::SetupWithTypingsInstaller(files, tiOptions);
	project::Session* session = p_2.first;
	auto& utils = p_2.second;
		t->Cleanup([session] { session->Close(); });

		session->DidOpenFile(
		    t->Context(),
		    lsproto::DocumentUri(
		        "file:///user/username/projects/project/app.js"),
		    1,
		    std::get<std::string>(
		        files["/user/username/projects/project/app.js"]),
		    lsproto::LanguageKindJavaScript);
		session->WaitForBackgroundTasks();
		auto npmCalls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(npmCalls.size()), int64_t(2));
		assert::Equal(t, npmCalls[0].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::Equal(t, npmCalls[0].Args[2],
		              std::string("types-registry@latest"));
		assert::Equal(t, npmCalls[1].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::Assert(t,
		               argsContain(npmCalls[1].Args,
		                           "@types/jquery@latest"));
		assert::Equal(
		    t, int64_t(utils->Client()->RefreshDiagnosticsCalls().size()),
		    int64_t(1));
	});

	t->Run("inferred projects", [&](T* t) {
		t->Parallel();

		FileMap files{
		    {"/user/username/projects/project/app.js", ""},
		    {"/user/username/projects/project/package.json",
		     R"({
				"name": "test",
				"dependencies": {
					"jquery": "^3.1.0"
				}
			})"},
		};

		auto tiOptions = std::make_shared<
		    projecttestutil::TypingsInstallerOptions>();
		tiOptions->PackageToFile = {
		    {"jquery", "declare const $: { x: number }"},
		};
		auto p_3 =
		    projecttestutil::SetupWithTypingsInstaller(files, tiOptions);
	project::Session* session = p_3.first;
	auto& utils = p_3.second;
		t->Cleanup([session] { session->Close(); });

		session->DidOpenFile(
		    t->Context(),
		    lsproto::DocumentUri(
		        "file:///user/username/projects/project/app.js"),
		    1,
		    std::get<std::string>(
		        files["/user/username/projects/project/app.js"]),
		    lsproto::LanguageKindJavaScript);
		session->WaitForBackgroundTasks();
		// Check that npm install was called twice
		auto calls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(calls.size()), int64_t(2),
		              "Expected exactly 2 npm install calls");
		assert::Equal(t, calls[0].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::DeepEqual(
		    t, calls[0].Args,
		    std::vector<std::string>{"install", "--ignore-scripts",
		                             "types-registry@latest"});
		assert::Equal(t, calls[1].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::Equal(t, calls[1].Args[2],
		              std::string("@types/jquery@latest"));

		// Verify the types file was installed
		auto [ls_, err] = session->GetLanguageService(
		    t->Context(), lsproto::DocumentUri(
		                      "file:///user/username/projects/project/app.js"));
		assert::NilError(t, err);
		auto* program = ls_->GetProgram();
		auto* jqueryTypesFile = program->GetSourceFile(
		    std::string(projecttestutil::TestTypingsLocation) +
		    "/node_modules/@types/jquery/index.d.ts");
		assert::Assert(t, jqueryTypesFile != nullptr,
		               "jquery types should be installed");
		delete ls_;
	});

	t->Run("type acquisition with "
	       "disableFilenameBasedTypeAcquisition:true",
	       [&](T* t) {
		       t->Parallel();

		       FileMap files{
		           {"/user/username/projects/project/jquery.js", ""},
		           {"/user/username/projects/project/tsconfig.json",
		            R"({
				"compilerOptions": { "allowJs": true },
				"typeAcquisition": { "enable": true, "disableFilenameBasedTypeAcquisition": true }
			})"},
		       };

		       auto tiOptions = std::make_shared<
		           projecttestutil::TypingsInstallerOptions>();
		       tiOptions->TypesRegistry = {"jquery"};
		       auto p_4 =
		           projecttestutil::SetupWithTypingsInstaller(files,
		                                                      tiOptions);
	project::Session* session = p_4.first;
	auto& utils = p_4.second;
		       t->Cleanup([session] { session->Close(); });

		       // Should only get types-registry install, no jquery install
		       // since filename-based acquisition is disabled
		       session->DidOpenFile(
		           t->Context(),
		           lsproto::DocumentUri(
		               "file:///user/username/projects/project/jquery.js"),
		           1,
		           std::get<std::string>(
		               files["/user/username/projects/project/jquery.js"]),
		           lsproto::LanguageKindJavaScript);
		       session->WaitForBackgroundTasks();

		       // Check that npm install was called once (only
		       // types-registry)
		       auto calls = utils->NpmExecutor()->NpmInstallCalls();
		       assert::Equal(t, int64_t(calls.size()), int64_t(1),
		                     "Expected exactly 1 npm install call");
		       assert::Equal(
		           t, calls[0].Cwd,
		           std::string(projecttestutil::TestTypingsLocation));
		       assert::DeepEqual(
		           t, calls[0].Args,
		           std::vector<std::string>{"install", "--ignore-scripts",
		                                    "types-registry@latest"});
	       });

	t->Run("discover from node_modules", [&](T* t) {
		t->Parallel();

		FileMap files{
		    {"/user/username/projects/project/app.js", ""},
		    {"/user/username/projects/project/package.json",
		     R"({
			    "dependencies": {
					"jquery": "1.0.0"
				}
			})"},
		    {"/user/username/projects/project/jsconfig.json", "{}"},
		    {"/user/username/projects/project/node_modules/commander/"
		     "index.js",
		     ""},
		    {"/user/username/projects/project/node_modules/commander/"
		     "package.json",
		     "{ \"name\": \"commander\" }"},
		    {"/user/username/projects/project/node_modules/jquery/index.js",
		     ""},
		    {"/user/username/projects/project/node_modules/jquery/"
		     "package.json",
		     "{ \"name\": \"jquery\" }"},
		    {"/user/username/projects/project/node_modules/jquery/nested/"
		     "package.json",
		     "{ \"name\": \"nested\" }"},
		};

		auto tiOptions = std::make_shared<
		    projecttestutil::TypingsInstallerOptions>();
		tiOptions->TypesRegistry = {"nested", "commander"};
		tiOptions->PackageToFile = {
		    {"jquery", "declare const jquery: { x: number }"},
		};
		auto p_5 =
		    projecttestutil::SetupWithTypingsInstaller(files, tiOptions);
	project::Session* session = p_5.first;
	auto& utils = p_5.second;
		t->Cleanup([session] { session->Close(); });

		session->DidOpenFile(
		    t->Context(),
		    lsproto::DocumentUri(
		        "file:///user/username/projects/project/app.js"),
		    1,
		    std::get<std::string>(
		        files["/user/username/projects/project/app.js"]),
		    lsproto::LanguageKindJavaScript);
		session->WaitForBackgroundTasks();

		// Check that npm install was called twice
		auto calls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(calls.size()), int64_t(2),
		              "Expected exactly 2 npm install calls");
		assert::Equal(t, calls[0].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::DeepEqual(
		    t, calls[0].Args,
		    std::vector<std::string>{"install", "--ignore-scripts",
		                             "types-registry@latest"});
		assert::Equal(t, calls[1].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::Equal(t, calls[1].Args[2],
		              std::string("@types/jquery@latest"));
	});

	t->Run("discover from node_modules empty types", [&](T* t) {
		t->Parallel();

		FileMap files{
		    {"/user/username/projects/project/app.js", ""},
		    {"/user/username/projects/project/package.json",
		     "{\"dependencies\": {\"jquery\": \"1.0.0\"}}"},
		    {"/user/username/projects/project/jsconfig.json",
		     "{\"compilerOptions\": {\"types\": []}}"},
		    {"/user/username/projects/project/node_modules/commander/"
		     "index.js",
		     ""},
		    {"/user/username/projects/project/node_modules/commander/"
		     "package.json",
		     "{ \"name\": \"commander\" }"},
		    {"/user/username/projects/project/node_modules/jquery/index.js",
		     ""},
		    {"/user/username/projects/project/node_modules/jquery/"
		     "package.json",
		     "{ \"name\": \"jquery\" }"},
		    {"/user/username/projects/project/node_modules/jquery/nested/"
		     "package.json",
		     "{ \"name\": \"nested\" }"},
		};

		auto tiOptions = std::make_shared<
		    projecttestutil::TypingsInstallerOptions>();
		tiOptions->TypesRegistry = {"nested", "commander"};
		tiOptions->PackageToFile = {
		    {"jquery", "declare const jquery: { x: number }"},
		};
		auto p_6 =
		    projecttestutil::SetupWithTypingsInstaller(files, tiOptions);
	project::Session* session = p_6.first;
	auto& utils = p_6.second;
		t->Cleanup([session] { session->Close(); });

		session->DidOpenFile(
		    t->Context(),
		    lsproto::DocumentUri(
		        "file:///user/username/projects/project/app.js"),
		    1,
		    std::get<std::string>(
		        files["/user/username/projects/project/app.js"]),
		    lsproto::LanguageKindJavaScript);
		session->WaitForBackgroundTasks();

		// Only types-registry should be installed
		auto calls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(calls.size()), int64_t(1));
		assert::DeepEqual(
		    t, calls[0].Args,
		    std::vector<std::string>{"install", "--ignore-scripts",
		                             "types-registry@latest"});
	});

	t->Run("discover from node_modules explicit types", [&](T* t) {
		t->Parallel();

		FileMap files{
		    {"/user/username/projects/project/app.js", ""},
		    {"/user/username/projects/project/package.json",
		     "{\"dependencies\": {\"jquery\": \"1.0.0\"}}"},
		    {"/user/username/projects/project/jsconfig.json",
		     "{\"compilerOptions\": {\"types\": [\"jquery\"]}}"},
		    {"/user/username/projects/project/node_modules/commander/"
		     "index.js",
		     ""},
		    {"/user/username/projects/project/node_modules/commander/"
		     "package.json",
		     "{ \"name\": \"commander\" }"},
		    {"/user/username/projects/project/node_modules/jquery/index.js",
		     ""},
		    {"/user/username/projects/project/node_modules/jquery/"
		     "package.json",
		     "{ \"name\": \"jquery\" }"},
		    {"/user/username/projects/project/node_modules/jquery/nested/"
		     "package.json",
		     "{ \"name\": \"nested\" }"},
		};

		auto tiOptions = std::make_shared<
		    projecttestutil::TypingsInstallerOptions>();
		tiOptions->TypesRegistry = {"nested", "commander"};
		tiOptions->PackageToFile = {
		    {"jquery", "declare const jquery: { x: number }"},
		};
		auto p_7 =
		    projecttestutil::SetupWithTypingsInstaller(files, tiOptions);
	project::Session* session = p_7.first;
	auto& utils = p_7.second;
		t->Cleanup([session] { session->Close(); });

		session->DidOpenFile(
		    t->Context(),
		    lsproto::DocumentUri(
		        "file:///user/username/projects/project/app.js"),
		    1,
		    std::get<std::string>(
		        files["/user/username/projects/project/app.js"]),
		    lsproto::LanguageKindJavaScript);
		session->WaitForBackgroundTasks();

		// Only types-registry should be installed
		auto calls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(calls.size()), int64_t(1));
		assert::DeepEqual(
		    t, calls[0].Args,
		    std::vector<std::string>{"install", "--ignore-scripts",
		                             "types-registry@latest"});
	});

	t->Run("discover from node_modules empty types has import",
	       [&](T* t) {
		       t->Parallel();

		       FileMap files{
		           {"/user/username/projects/project/app.js",
		            "import \"jquery\";"},
		           {"/user/username/projects/project/package.json",
		            "{\"dependencies\": {\"jquery\": \"1.0.0\"}}"},
		           {"/user/username/projects/project/jsconfig.json",
		            "{\"compilerOptions\": {\"types\": []}}"},
		           {"/user/username/projects/project/node_modules/commander/"
		            "index.js",
		            ""},
		           {"/user/username/projects/project/node_modules/commander/"
		            "package.json",
		            "{ \"name\": \"commander\" }"},
		           {"/user/username/projects/project/node_modules/jquery/"
		            "index.js",
		            ""},
		           {"/user/username/projects/project/node_modules/jquery/"
		            "package.json",
		            "{ \"name\": \"jquery\" }"},
		           {"/user/username/projects/project/node_modules/jquery/"
		            "nested/package.json",
		            "{ \"name\": \"nested\" }"},
		       };

		       auto tiOptions = std::make_shared<
		           projecttestutil::TypingsInstallerOptions>();
		       tiOptions->TypesRegistry = {"nested", "commander"};
		       tiOptions->PackageToFile = {
		           {"jquery", "declare const jquery: { x: number }"},
		       };
		       auto p_8 =
		           projecttestutil::SetupWithTypingsInstaller(files,
		                                                      tiOptions);
	project::Session* session = p_8.first;
	auto& utils = p_8.second;
		       t->Cleanup([session] { session->Close(); });

		       session->DidOpenFile(
		           t->Context(),
		           lsproto::DocumentUri(
		               "file:///user/username/projects/project/app.js"),
		           1,
		           std::get<std::string>(
		               files["/user/username/projects/project/app.js"]),
		           lsproto::LanguageKindJavaScript);
		       session->WaitForBackgroundTasks();

		       // types-registry + jquery types
		       auto calls = utils->NpmExecutor()->NpmInstallCalls();
		       assert::Equal(t, int64_t(calls.size()), int64_t(2));
		       assert::DeepEqual(
		           t, calls[0].Args,
		           std::vector<std::string>{"install", "--ignore-scripts",
		                                    "types-registry@latest"});
		       assert::Assert(t, argsContain(calls[1].Args,
		                                     "@types/jquery@latest"));
	       });

	t->Run("discover from bower_components", [&](T* t) {
		t->Parallel();

		FileMap files{
		    {"/user/username/projects/project/app.js", ""},
		    {"/user/username/projects/project/jsconfig.json", "{}"},
		    {"/user/username/projects/project/bower_components/jquery/"
		     "index.js",
		     ""},
		    {"/user/username/projects/project/bower_components/jquery/"
		     "bower.json",
		     "{ \"name\": \"jquery\" }"},
		};

		auto tiOptions = std::make_shared<
		    projecttestutil::TypingsInstallerOptions>();
		tiOptions->PackageToFile = {
		    {"jquery", "declare const jquery: { x: number }"},
		};
		auto p_9 =
		    projecttestutil::SetupWithTypingsInstaller(files, tiOptions);
	project::Session* session = p_9.first;
	auto& utils = p_9.second;
		t->Cleanup([session] { session->Close(); });

		session->DidOpenFile(
		    t->Context(),
		    lsproto::DocumentUri(
		        "file:///user/username/projects/project/app.js"),
		    1,
		    std::get<std::string>(
		        files["/user/username/projects/project/app.js"]),
		    lsproto::LanguageKindJavaScript);
		session->WaitForBackgroundTasks();

		// Check that npm install was called twice
		auto calls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(calls.size()), int64_t(2),
		              "Expected exactly 2 npm install calls");
		assert::Equal(t, calls[0].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::DeepEqual(
		    t, calls[0].Args,
		    std::vector<std::string>{"install", "--ignore-scripts",
		                             "types-registry@latest"});
		assert::Equal(t, calls[1].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::Equal(t, calls[1].Args[2],
		              std::string("@types/jquery@latest"));

		// Verify the types file was installed
		auto [ls_, err] = session->GetLanguageService(
		    t->Context(), lsproto::DocumentUri(
		                      "file:///user/username/projects/project/app.js"));
		assert::NilError(t, err);
		auto* jqueryTypesFile = ls_->GetProgram()->GetSourceFile(
		    std::string(projecttestutil::TestTypingsLocation) +
		    "/node_modules/@types/jquery/index.d.ts");
		assert::Assert(t, jqueryTypesFile != nullptr,
		               "jquery types should be installed");
		delete ls_;
	});

	t->Run("discover from bower.json", [&](T* t) {
		t->Parallel();

		FileMap files{
		    {"/user/username/projects/project/app.js", ""},
		    {"/user/username/projects/project/jsconfig.json", "{}"},
		    {"/user/username/projects/project/bower.json",
		     R"({
				"dependencies": {
                    "jquery": "^3.1.0"
                }
			})"},
		};

		auto tiOptions = std::make_shared<
		    projecttestutil::TypingsInstallerOptions>();
		tiOptions->PackageToFile = {
		    {"jquery", "declare const jquery: { x: number }"},
		};
		auto p_10 =
		    projecttestutil::SetupWithTypingsInstaller(files, tiOptions);
	project::Session* session = p_10.first;
	auto& utils = p_10.second;
		t->Cleanup([session] { session->Close(); });

		session->DidOpenFile(
		    t->Context(),
		    lsproto::DocumentUri(
		        "file:///user/username/projects/project/app.js"),
		    1,
		    std::get<std::string>(
		        files["/user/username/projects/project/app.js"]),
		    lsproto::LanguageKindJavaScript);
		session->WaitForBackgroundTasks();

		// Check that npm install was called twice
		auto calls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(calls.size()), int64_t(2),
		              "Expected exactly 2 npm install calls");
		assert::Equal(t, calls[0].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::DeepEqual(
		    t, calls[0].Args,
		    std::vector<std::string>{"install", "--ignore-scripts",
		                             "types-registry@latest"});
		assert::Equal(t, calls[1].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::Equal(t, calls[1].Args[2],
		              std::string("@types/jquery@latest"));

		// Verify the types file was installed
		auto [ls_, err] = session->GetLanguageService(
		    t->Context(), lsproto::DocumentUri(
		                      "file:///user/username/projects/project/app.js"));
		assert::NilError(t, err);
		auto* jqueryTypesFile = ls_->GetProgram()->GetSourceFile(
		    std::string(projecttestutil::TestTypingsLocation) +
		    "/node_modules/@types/jquery/index.d.ts");
		assert::Assert(t, jqueryTypesFile != nullptr,
		               "jquery types should be installed");
		delete ls_;
	});

	t->Run("Malformed package.json should be watched", [&](T* t) {
		t->Parallel();

		FileMap files{
		    {"/user/username/projects/project/app.js", ""},
		    {"/user/username/projects/project/package.json",
		     "{\"dependencies\": { \"co } }"},
		};

		auto tiOptions = std::make_shared<
		    projecttestutil::TypingsInstallerOptions>();
		tiOptions->PackageToFile = {
		    {"commander", "export let x: number"},
		};
		auto p_11 =
		    projecttestutil::SetupWithTypingsInstaller(files, tiOptions);
	project::Session* session = p_11.first;
	auto& utils = p_11.second;
		t->Cleanup([session] { session->Close(); });

		lsproto::DocumentUri uri(
		    "file:///user/username/projects/project/app.js");
		session->DidOpenFile(
		    t->Context(), uri, 1,
		    std::get<std::string>(
		        files["/user/username/projects/project/app.js"]),
		    lsproto::LanguageKindJavaScript);
		session->WaitForBackgroundTasks();

		// Initially only types-registry update attempted
		auto calls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(calls.size()), int64_t(1));
		assert::DeepEqual(
		    t, calls[0].Args,
		    std::vector<std::string>{"install", "--ignore-scripts",
		                             "types-registry@latest"});

		// Fix package.json and notify watcher
		auto werr = utils->FS()->WriteFile(
		    "/user/username/projects/project/package.json",
		    "{ \"dependencies\": { \"commander\": \"0.0.2\" } }");
		assert::Assert(t, werr.impl() == nullptr);
		lsproto::FileEvent fe;
		fe.Type = lsproto::FileChangeTypeChanged;
		fe.Uri = lsproto::DocumentUri(
		    "file:///user/username/projects/project/package.json");
		session->DidChangeWatchedFiles(t->Context(), {&fe});
		// diagnostics refresh triggered - simulate by getting the
		// language service
		{
			auto p = session->GetLanguageService(t->Context(), uri);
			delete p.first;
		}
		session->WaitForBackgroundTasks();

		calls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(calls.size()), int64_t(2));
		assert::Assert(t, argsContain(calls[1].Args,
		                              "@types/commander@latest"));

		// Verify types file present
		auto p_12 = session->GetLanguageService(t->Context(), uri);
	auto* ls_ = p_12.first;
	auto& err = p_12.second;
		assert::NilError(t, err);
		auto* program = ls_->GetProgram();
		assert::Assert(t,
		               program->GetSourceFile(
		                   std::string(
		                       projecttestutil::TestTypingsLocation) +
		                   "/node_modules/@types/commander/index.d.ts") !=
		                   nullptr);
		delete ls_;
	});

	t->Run("should redo resolution that resolved to '.js' file after "
	       "typings are installed",
	       [&](T* t) {
		       t->Parallel();

		       FileMap files{
		           {"/user/username/projects/project/app.js",
		            "\n                import * as commander from "
		            "\"commander\";\n            "},
		           {"/user/username/projects/node_modules/commander/index.js",
		            "module.exports = 0"},
		       };

		       auto tiOptions = std::make_shared<
		           projecttestutil::TypingsInstallerOptions>();
		       tiOptions->PackageToFile = {
		           {"commander", "export let commander: number"},
		       };
		       auto p_13 =
		           projecttestutil::SetupWithTypingsInstaller(files,
		                                                      tiOptions);
	project::Session* session = p_13.first;
	auto& utils = p_13.second;
		       t->Cleanup([session] { session->Close(); });

		       lsproto::DocumentUri uri(
		           "file:///user/username/projects/project/app.js");
		       session->DidOpenFile(
		           t->Context(), uri, 1,
		           std::get<std::string>(
		               files["/user/username/projects/project/app.js"]),
		           lsproto::LanguageKindJavaScript);
		       session->WaitForBackgroundTasks();

		       auto calls = utils->NpmExecutor()->NpmInstallCalls();
		       assert::Equal(t, int64_t(calls.size()), int64_t(2));
		       assert::Assert(t, argsContain(calls[1].Args,
		                                     "@types/commander@latest"));

		       auto p_14 =
		           session->GetLanguageService(t->Context(), uri);
	auto* ls_ = p_14.first;
	auto& err = p_14.second;
		       assert::NilError(t, err);
		       auto* program = ls_->GetProgram();
		       // Types file present
		       assert::Assert(
		           t,
		           program->GetSourceFile(
		               std::string(
		                   projecttestutil::TestTypingsLocation) +
		               "/node_modules/@types/commander/index.d.ts") !=
		               nullptr);
		       // JS resolution should be dropped
		       assert::Assert(
		           t,
		           program->GetSourceFile("/user/username/projects/"
		                                  "node_modules/commander/index.js") ==
		               nullptr);
		       delete ls_;
	       });

	t->Run("expired cache entry (inferred project, should install "
	       "typings)",
	       [&](T* t) {
		       t->Parallel();
		       std::string ttl(projecttestutil::TestTypingsLocation);

		       FileMap files{
		           {"/user/username/projects/project/app.js", ""},
		           {"/user/username/projects/project/package.json",
		            "{\"name\":\"test\",\"dependencies\":{\"jquery\":"
		            "\"^3.1.0\"}}"},
		           {ttl + "/node_modules/@types/jquery/index.d.ts",
		            "export const x = 10;"},
		           {ttl + "/package.json",
		            "{\"dependencies\":{\"types-registry\":\"^0.1.317\"},"
		            "\"devDependencies\":{\"@types/jquery\":\"^1.0.0\"}}"},
		           {ttl + "/package-lock.json",
		            "{\"dependencies\":{\"@types/jquery\":{\"version\":"
		            "\"1.0.0\"}}}"},
		       };

		       auto tiOptions = std::make_shared<
		           projecttestutil::TypingsInstallerOptions>();
		       tiOptions->PackageToFile = {
		           {"jquery", "export const y = 10"},
		       };
		       auto pair_ =
		           projecttestutil::SetupWithTypingsInstaller(files,
		                                                      tiOptions);
		       project::Session* session = pair_.first;
		       t->Cleanup([session] { session->Close(); });

		       lsproto::DocumentUri uri(
		           "file:///user/username/projects/project/app.js");
		       session->DidOpenFile(
		           t->Context(), uri, 1,
		           std::get<std::string>(
		               files["/user/username/projects/project/app.js"]),
		           lsproto::LanguageKindJavaScript);
		       session->WaitForBackgroundTasks();

		       auto p_15 =
		           session->GetLanguageService(t->Context(), uri);
	auto* ls_ = p_15.first;
	auto& err = p_15.second;
		       assert::NilError(t, err);
		       auto* program = ls_->GetProgram();
		       // Expect updated content from installed typings
		       assert::Equal(
		           t, program
		                  ->GetSourceFile(
		                      ttl +
		                      "/node_modules/@types/jquery/index.d.ts")
		                  ->Text(),
		           std::string("export const y = 10"));
		       delete ls_;
	       });

	t->Run("non-expired cache entry (inferred project, should not "
	       "install typings)",
	       [&](T* t) {
		       t->Parallel();
		       std::string ttl(projecttestutil::TestTypingsLocation);

		       FileMap files{
		           {"/user/username/projects/project/app.js", ""},
		           {"/user/username/projects/project/package.json",
		            "{\"name\":\"test\",\"dependencies\":{\"jquery\":"
		            "\"^3.1.0\"}}"},
		           {ttl + "/node_modules/@types/jquery/index.d.ts",
		            "export const x = 10;"},
		           {ttl + "/package.json",
		            "{\"dependencies\":{\"types-registry\":\"^0.1.317\"},"
		            "\"devDependencies\":{\"@types/jquery\":\"^1.3.0\"}}"},
		           {ttl + "/package-lock.json",
		            "{\"dependencies\":{\"@types/jquery\":{\"version\":"
		            "\"1.3.0\"}}}"},
		       };

		       auto tiOptions = std::make_shared<
		           projecttestutil::TypingsInstallerOptions>();
		       tiOptions->TypesRegistry = {"jquery"};
		       auto pair_ =
		           projecttestutil::SetupWithTypingsInstaller(files,
		                                                      tiOptions);
		       project::Session* session = pair_.first;
		       t->Cleanup([session] { session->Close(); });

		       lsproto::DocumentUri uri(
		           "file:///user/username/projects/project/app.js");
		       session->DidOpenFile(
		           t->Context(), uri, 1,
		           std::get<std::string>(
		               files["/user/username/projects/project/app.js"]),
		           lsproto::LanguageKindJavaScript);
		       session->WaitForBackgroundTasks();

		       auto p_16 =
		           session->GetLanguageService(t->Context(), uri);
	auto* ls_ = p_16.first;
	auto& err = p_16.second;
		       assert::NilError(t, err);
		       auto* program = ls_->GetProgram();
		       // Expect existing content unchanged
		       assert::Equal(
		           t, program
		                  ->GetSourceFile(
		                      ttl +
		                      "/node_modules/@types/jquery/index.d.ts")
		                  ->Text(),
		           std::string("export const x = 10;"));
		       delete ls_;
	       });

	t->Run("deduplicate from local @types packages", [&](T* t) {
		t->Skip({
		    "Todo - implement removing local @types from include list"});
		t->Parallel();

		FileMap files{
		    {"/user/username/projects/project/app.js", ""},
		    {"/user/username/projects/project/node_modules/@types/node/"
		     "index.d.ts",
		     "declare var node;"},
		    {"/user/username/projects/project/jsconfig.json",
		     R"({
				"typeAcquisition": { "include": ["node"] }
			})"},
		};

		auto tiOptions = std::make_shared<
		    projecttestutil::TypingsInstallerOptions>();
		tiOptions->TypesRegistry = {"node"};
		auto p_17 =
		    projecttestutil::SetupWithTypingsInstaller(files, tiOptions);
	project::Session* session = p_17.first;
	auto& utils = p_17.second;
		t->Cleanup([session] { session->Close(); });

		lsproto::DocumentUri uri(
		    "file:///user/username/projects/project/app.js");
		session->DidOpenFile(
		    t->Context(), uri, 1,
		    std::get<std::string>(
		        files["/user/username/projects/project/app.js"]),
		    lsproto::LanguageKindJavaScript);
		session->WaitForBackgroundTasks();

		// Only the types-registry should be installed; @types/node should
		// NOT be installed since it exists locally
		auto npmCalls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(npmCalls.size()), int64_t(1));
		assert::Equal(t, npmCalls[0].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::DeepEqual(
		    t, npmCalls[0].Args,
		    std::vector<std::string>{"install", "--ignore-scripts",
		                             "types-registry@latest"});

		// And the program should include the local @types/node
		// declaration file
		auto p_18 = session->GetLanguageService(t->Context(), uri);
	auto* ls_ = p_18.first;
	auto& err = p_18.second;
		assert::NilError(t, err);
		auto* program = ls_->GetProgram();
		assert::Assert(
		    t,
		    program->GetSourceFile("/user/username/projects/project/"
		                           "node_modules/@types/node/index.d.ts") !=
		        nullptr);
		delete ls_;
	});

	t->Run("expired cache entry (inferred project, should install "
	       "typings) lockfile3",
	       [&](T* t) {
		       t->Parallel();
		       std::string ttl(projecttestutil::TestTypingsLocation);

		       FileMap files{
		           {"/user/username/projects/project/app.js", ""},
		           {"/user/username/projects/project/package.json",
		            "{\"name\":\"test\",\"dependencies\":{\"jquery\":"
		            "\"^3.1.0\"}}"},
		           {ttl + "/node_modules/@types/jquery/index.d.ts",
		            "export const x = 10;"},
		           {ttl + "/package.json",
		            "{\"dependencies\":{\"types-registry\":\"^0.1.317\"},"
		            "\"devDependencies\":{\"@types/jquery\":\"^1.0.0\"}}"},
		           {ttl + "/package-lock.json",
		            "{\"packages\":{\"node_modules/@types/jquery\":"
		            "{\"version\":\"1.0.0\"}}}"},
		       };

		       auto tiOptions = std::make_shared<
		           projecttestutil::TypingsInstallerOptions>();
		       tiOptions->PackageToFile = {
		           {"jquery", "export const y = 10"},
		       };
		       auto pair_ =
		           projecttestutil::SetupWithTypingsInstaller(files,
		                                                      tiOptions);
		       project::Session* session = pair_.first;
		       t->Cleanup([session] { session->Close(); });

		       lsproto::DocumentUri uri(
		           "file:///user/username/projects/project/app.js");
		       session->DidOpenFile(
		           t->Context(), uri, 1,
		           std::get<std::string>(
		               files["/user/username/projects/project/app.js"]),
		           lsproto::LanguageKindJavaScript);
		       session->WaitForBackgroundTasks();

		       auto p_19 =
		           session->GetLanguageService(t->Context(), uri);
	auto* ls_ = p_19.first;
	auto& err = p_19.second;
		       assert::NilError(t, err);
		       auto* program = ls_->GetProgram();
		       // Expect updated content from installed typings
		       assert::Equal(
		           t, program
		                  ->GetSourceFile(
		                      ttl +
		                      "/node_modules/@types/jquery/index.d.ts")
		                  ->Text(),
		           std::string("export const y = 10"));
		       delete ls_;
	       });

	t->Run("non-expired cache entry (inferred project, should not "
	       "install typings) lockfile3",
	       [&](T* t) {
		       t->Parallel();
		       std::string ttl(projecttestutil::TestTypingsLocation);

		       FileMap files{
		           {"/user/username/projects/project/app.js", ""},
		           {"/user/username/projects/project/package.json",
		            "{\"name\":\"test\",\"dependencies\":{\"jquery\":"
		            "\"^3.1.0\"}}"},
		           {ttl + "/node_modules/@types/jquery/index.d.ts",
		            "export const x = 10;"},
		           {ttl + "/package.json",
		            "{\"dependencies\":{\"types-registry\":\"^0.1.317\"},"
		            "\"devDependencies\":{\"@types/jquery\":\"^1.3.0\"}}"},
		           {ttl + "/package-lock.json",
		            "{\"packages\":{\"node_modules/@types/jquery\":"
		            "{\"version\":\"1.3.0\"}}}"},
		       };

		       auto tiOptions = std::make_shared<
		           projecttestutil::TypingsInstallerOptions>();
		       tiOptions->TypesRegistry = {"jquery"};
		       auto pair_ =
		           projecttestutil::SetupWithTypingsInstaller(files,
		                                                      tiOptions);
		       project::Session* session = pair_.first;
		       t->Cleanup([session] { session->Close(); });

		       lsproto::DocumentUri uri(
		           "file:///user/username/projects/project/app.js");
		       session->DidOpenFile(
		           t->Context(), uri, 1,
		           std::get<std::string>(
		               files["/user/username/projects/project/app.js"]),
		           lsproto::LanguageKindJavaScript);
		       session->WaitForBackgroundTasks();

		       auto p_20 =
		           session->GetLanguageService(t->Context(), uri);
	auto* ls_ = p_20.first;
	auto& err = p_20.second;
		       assert::NilError(t, err);
		       auto* program = ls_->GetProgram();
		       // Expect existing content unchanged
		       assert::Equal(
		           t, program
		                  ->GetSourceFile(
		                      ttl +
		                      "/node_modules/@types/jquery/index.d.ts")
		                  ->Text(),
		           std::string("export const x = 10;"));
		       delete ls_;
	       });

	t->Run("should install typings for unresolved imports", [&](T* t) {
		t->Parallel();

		FileMap files{
		    {"/user/username/projects/project/app.js",
		     "\n				import * as fs from \"fs\";\n                "
		     "import * as commander from \"commander\";\n                "
		     "import * as component from \"@ember/component\";\n			"},
		};

		auto tiOptions = std::make_shared<
		    projecttestutil::TypingsInstallerOptions>();
		tiOptions->PackageToFile = {
		    {"node", "export let node: number"},
		    {"commander", "export let commander: number"},
		    {"ember__component", "export let ember__component: number"},
		};
		auto p_21 =
		    projecttestutil::SetupWithTypingsInstaller(files, tiOptions);
	project::Session* session = p_21.first;
	auto& utils = p_21.second;
		t->Cleanup([session] { session->Close(); });

		session->DidOpenFile(
		    t->Context(),
		    lsproto::DocumentUri(
		        "file:///user/username/projects/project/app.js"),
		    1,
		    std::get<std::string>(
		        files["/user/username/projects/project/app.js"]),
		    lsproto::LanguageKindJavaScript);
		session->WaitForBackgroundTasks();

		// Check that npm install was called twice
		auto calls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(calls.size()), int64_t(2),
		              "Expected exactly 2 npm install calls");
		assert::Equal(t, calls[0].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::DeepEqual(
		    t, calls[0].Args,
		    std::vector<std::string>{"install", "--ignore-scripts",
		                             "types-registry@latest"});

		// The second call should install all three packages at once
		assert::Equal(t, calls[1].Cwd,
		              std::string(projecttestutil::TestTypingsLocation));
		assert::Equal(t, calls[1].Args[0], std::string("install"));
		assert::Equal(t, calls[1].Args[1],
		              std::string("--ignore-scripts"));
		// Check that all three packages are in the install command
		auto& installArgs = calls[1].Args;
		assert::Assert(t, argsContain(installArgs,
		                              "@types/ember__component@latest"));
		assert::Assert(t, argsContain(installArgs,
		                              "@types/commander@latest"));
		assert::Assert(t, argsContain(installArgs, "@types/node@latest"));

		// Verify the types files were installed
		auto [ls_, err] = session->GetLanguageService(
		    t->Context(), lsproto::DocumentUri(
		                      "file:///user/username/projects/project/app.js"));
		assert::NilError(t, err);
		auto* program = ls_->GetProgram();
		std::string ttl(projecttestutil::TestTypingsLocation);
		auto* nodeTypesFile = program->GetSourceFile(
		    ttl + "/node_modules/@types/node/index.d.ts");
		assert::Assert(t, nodeTypesFile != nullptr,
		               "node types should be installed");
		auto* commanderTypesFile = program->GetSourceFile(
		    ttl + "/node_modules/@types/commander/index.d.ts");
		assert::Assert(t, commanderTypesFile != nullptr,
		               "commander types should be installed");
		auto* emberComponentTypesFile = program->GetSourceFile(
		    ttl + "/node_modules/@types/ember__component/index.d.ts");
		assert::Assert(t, emberComponentTypesFile != nullptr,
		               "ember__component types should be installed");
		delete ls_;
	});

	// Test that ATA works correctly when `WatchEnabled` is false but
	// `TypingsLocation` is set.
	// Previously if `WatchEnabled` was false but `TypingsLocation` was
	// set, ATA would run but crash when cloning file-watcher data for a
	// new snapshot.
	t->Run("ATA with WatchEnabled false should not panic", [&](T* t) {
		t->Parallel();

		FileMap files{
		    {"/user/username/projects/project/app.js", ""},
		    {"/user/username/projects/project/package.json",
		     R"({
				"name": "test",
				"dependencies": {
					"jquery": "^3.1.0"
				}
			})"},
		};

		project::SessionOptions options;
		options.CurrentDirectory = "/";
		options.DefaultLibraryPath = tsc::bundled::LibPath();
		options.TypingsLocation =
		    std::string(projecttestutil::TestTypingsLocation);
		options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
		options.WatchEnabled = false;
		options.LoggingEnabled = true;

		auto tiOptions = std::make_shared<
		    projecttestutil::TypingsInstallerOptions>();
		tiOptions->PackageToFile = {
		    {"jquery", "declare const $: { x: number }"},
		};
		auto p_22 =
		    projecttestutil::SetupWithOptionsAndTypingsInstaller(
		        files, &options, tiOptions);
	project::Session* session = p_22.first;
	auto& utils = p_22.second;
		t->Cleanup([session] { session->Close(); });

		// Open a file to trigger project creation and ATA.
		session->DidOpenFile(
		    t->Context(),
		    lsproto::DocumentUri(
		        "file:///user/username/projects/project/app.js"),
		    1,
		    std::get<std::string>(
		        files["/user/username/projects/project/app.js"]),
		    lsproto::LanguageKindJavaScript);
		session->WaitForBackgroundTasks();

		// ATA should have run
		auto calls = utils->NpmExecutor()->NpmInstallCalls();
		assert::Equal(t, int64_t(calls.size()), int64_t(2),
		              "Expected exactly 2 npm install calls");

		// Getting the language service should not panic after
		// applying ATA changes and grabbing the latest snapshot.
		auto [ls_, err] = session->GetLanguageService(
		    t->Context(), lsproto::DocumentUri(
		                      "file:///user/username/projects/project/app.js"));
		assert::NilError(t, err);
		assert::Assert(t, ls_ != nullptr);
		delete ls_;
	});

	struct ATADisabledCase {
		std::string name;
		lsutil::JsonObject config;
	};
	std::vector<ATADisabledCase> ataDisabledCases{
	    {"unified setting",
	     {{"js/ts",
	       lsutil::JsonObject{
	           {"tsserver",
	            lsutil::JsonObject{
	                {"automaticTypeAcquisition",
	                 lsutil::JsonObject{{"enabled", false}}}}}}}}},
	    {"deprecated setting",
	     {{"typescript",
	       lsutil::JsonObject{
	           {"disableAutomaticTypeAcquisition", true}}}}},
	};

	for (auto& tc : ataDisabledCases) {
		t->Run("ATA disabled via " + tc.name, [&](T* t) {
			t->Parallel();

			FileMap files{
			    {"/user/username/projects/project/app.js", ""},
			    {"/user/username/projects/project/package.json",
			     R"({
					"name": "test",
					"dependencies": {
						"jquery": "^3.1.0"
					}
				})"},
			};

			auto tiOptions = std::make_shared<
			    projecttestutil::TypingsInstallerOptions>();
			tiOptions->PackageToFile = {
			    {"jquery", "declare const $: { x: number }"},
			};
			auto p_23 =
			    projecttestutil::SetupWithTypingsInstaller(files,
			                                             tiOptions);
	project::Session* session = p_23.first;
	auto& utils = p_23.second;
			t->Cleanup([session] { session->Close(); });

			session->Configure(
			    lsutil::ParseUserPreferences(tc.config));
			session->DidOpenFile(
			    t->Context(),
			    lsproto::DocumentUri(
			        "file:///user/username/projects/project/app.js"),
			    1,
			    std::get<std::string>(
			        files["/user/username/projects/project/app.js"]),
			    lsproto::LanguageKindJavaScript);
			session->WaitForBackgroundTasks();

			auto calls = utils->NpmExecutor()->NpmInstallCalls();
			assert::Equal(t, int64_t(calls.size()), int64_t(0),
			              "Expected no npm install calls when ATA is "
			              "disabled via " +
			                  tc.name);
		});
	}

	t->Run("ATA re-enabled after being disabled triggers diagnostics "
	       "refresh",
	       [&](T* t) {
		       t->Parallel();

		       FileMap files{
		           {"/user/username/projects/project/app.js", ""},
		           {"/user/username/projects/project/package.json",
		            R"({
					"name": "test",
					"dependencies": {
						"jquery": "^3.1.0"
					}
				})"},
		       };

		       auto tiOptions = std::make_shared<
		           projecttestutil::TypingsInstallerOptions>();
		       tiOptions->PackageToFile = {
		           {"jquery", "declare const $: { x: number }"},
		       };
		       auto p_24 =
		           projecttestutil::SetupWithTypingsInstaller(files,
		                                                      tiOptions);
	project::Session* session = p_24.first;
	auto& utils = p_24.second;
		       t->Cleanup([session] { session->Close(); });

		       // Disable ATA
		       session->Configure(lsutil::ParseUserPreferences(
		           lsutil::JsonObject{
		               {"js/ts",
		                lsutil::JsonObject{
		                    {"tsserver",
		                     lsutil::JsonObject{
		                         {"automaticTypeAcquisition",
		                          lsutil::JsonObject{{"enabled",
		                                             false}}}}}}}}));

		       session->DidOpenFile(
		           t->Context(),
		           lsproto::DocumentUri(
		               "file:///user/username/projects/project/app.js"),
		           1,
		           std::get<std::string>(
		               files["/user/username/projects/project/app.js"]),
		           lsproto::LanguageKindJavaScript);
		       session->WaitForBackgroundTasks();

		       auto calls = utils->NpmExecutor()->NpmInstallCalls();
		       assert::Equal(
		           t, int64_t(calls.size()), int64_t(0),
		           "Expected no npm install calls when ATA is disabled");

		       int64_t baselineRefreshCount = int64_t(
		           utils->Client()->RefreshDiagnosticsCalls().size());

		       // Re-enable ATA
		       session->Configure(
		           lsutil::ParseUserPreferences(lsutil::JsonObject{}));
		       session->WaitForBackgroundTasks();

		       int64_t refreshCount = int64_t(
		           utils->Client()->RefreshDiagnosticsCalls().size());
		       assert::Assert(
		           t, refreshCount > baselineRefreshCount,
		           "Expected RefreshDiagnostics call after ATA "
		           "re-enabled");
	       });
}

REGISTER_UNIT_TEST("project/ata.TestATA", TestATA);

}  // namespace
