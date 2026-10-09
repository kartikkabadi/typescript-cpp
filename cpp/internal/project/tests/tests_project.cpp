// Port of tsc/internal/project/project_test.go.
#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/core/context.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/ls/ls.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/project/project.h"
#include "internal/project/session.h"
#include "internal/project/snapshot.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace baseline = tsc::testutil::baseline;
namespace core = tsc::core;
namespace gostd = tsc::gostd;
namespace json = tsc::json;
namespace lsproto = tsc::lsp::lsproto;
namespace lsutil = tsc::ls::lsutil;
namespace project = tsc::project;
namespace projecttestutil = tsc::testutil::projecttestutil;
using tsc::gostd::testing::T;
using projecttestutil::FileMap;

using publishDiagnosticsCall =
    tsc::testutil::projecttestutil::ClientMockPublishDiagnosticsCall;

// filterDiagnosticsByURI returns all PublishDiagnostics calls matching the
// given URI, starting from the given index.
static std::vector<publishDiagnosticsCall> filterDiagnosticsByURI(
    const std::vector<publishDiagnosticsCall>& calls,
    const lsproto::DocumentUri& uri, size_t from) {
	std::vector<publishDiagnosticsCall> result;
	for (size_t i = from; i < calls.size(); i++) {
		if (calls[i].Params->Uri == uri) {
			result.push_back(calls[i]);
		}
	}
	return result;
}


static int64_t diagSliceSize(
    const lsproto::Slice<std::shared_ptr<lsproto::Diagnostic>>& diags) {
	return diags ? int64_t(diags->size()) : 0;
}

static const std::vector<std::shared_ptr<lsproto::Diagnostic>>& diagSlice(
    const lsproto::Slice<std::shared_ptr<lsproto::Diagnostic>>& diags) {
	static const std::vector<std::shared_ptr<lsproto::Diagnostic>>
	    empty;
	return diags ? *diags : empty;
}

static std::string fileText(const FileMap& files, const char* name) {
	return std::get<std::string>(files.at(name));
}

static lsproto::FileEvent fileEvent(lsproto::FileChangeType type,
                                    const char* uri) {
	lsproto::FileEvent fe;
	fe.Type = type;
	fe.Uri = lsproto::DocumentUri(uri);
	return fe;
}

// These tests explicitly verify ProgramUpdateKind using subtests with shared
// helpers.
void TestProjectProgramUpdateKind(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	// Use the default session setup for tests.

	t->Run("NewFiles on initial build", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/src/tsconfig.json", "{}"},
		    {"/src/index.ts", "export const x = 1;"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(), "file:///src/index.ts", 1,
		                     fileText(files, "/src/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		auto* snapshot = session->Snapshot();
		auto* configured =
		    snapshot->ProjectCollection->ConfiguredProject(
		        tsc::tspath::Path("/src/tsconfig.json"));
		assert::Assert(t, configured != nullptr);
		assert::Equal(t, configured->ProgramUpdateKind,
		              project::ProgramUpdateKindNewFiles);
	});

	t->Run("Cloned on single-file change", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/src/tsconfig.json", "{}"},
		    {"/src/index.ts", "console.log('Hello');"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(), "file:///src/index.ts", 1,
		                     fileText(files, "/src/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		{
			lsproto::TextDocumentContentChangePartialOrWholeDocument
			    change;
			change.Partial = std::make_shared<
			    lsproto::TextDocumentContentChangePartial>();
			change.Partial->Text = "\n";
			change.Partial->Range.Start = lsproto::Position{0, 20};
			change.Partial->Range.End = lsproto::Position{0, 20};
			session->DidChangeFile(t->Context(), "file:///src/index.ts",
			                       2, {change});
		}
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		auto* snapshot = session->Snapshot();
		auto* configured =
		    snapshot->ProjectCollection->ConfiguredProject(
		        tsc::tspath::Path("/src/tsconfig.json"));
		assert::Assert(t, configured != nullptr);
		assert::Equal(t, configured->ProgramUpdateKind,
		              project::ProgramUpdateKindCloned);
	});

	t->Run("compiler options update inferred project", [&](T* t) {
		t->Parallel();
		const std::string fileName = "/src/index.ts";
		auto p0 = projecttestutil::Setup(FileMap{
		    {fileName, "export const x = 1;"},
		});
		auto* session = p0.first;
		t->Cleanup([session] { session->Close(); });
		lsproto::DocumentUri uri("file://" + fileName);
		session->DidOpenFile(t->Context(), uri, 1, "export const x = 1;",
		                     lsproto::LanguageKindTypeScript);
		auto* oldProject =
		    session->Snapshot()->ProjectCollection->InferredProject();
		assert::Assert(t, oldProject != nullptr);
		auto* oldProgram = oldProject->Program;
		assert::Equal(t, oldProgram->Options()->Strict,
		              tsc::Tristate::Unknown);

		tsc::CompilerOptions newOptions;
		newOptions.NoLib = tsc::Tristate::True;
		newOptions.Strict = tsc::Tristate::True;
		session->DidChangeCompilerOptionsForInferredProjects(t->Context(),
		                                                     &newOptions);
		{
			auto gP = session->GetLanguageService(t->Context(), uri);
			assert::NilError(t, gP.second);
			delete gP.first;
		}

		auto* updatedProject =
		    session->Snapshot()->ProjectCollection->InferredProject();
		assert::Assert(t, updatedProject != nullptr);
		assert::Equal(
		    t, updatedProject->CommandLine->CompilerOptions()->Strict,
		    tsc::Tristate::True);
		assert::Assert(t, updatedProject->Program != oldProgram);
		assert::Equal(t, updatedProject->Program->Options()->Strict,
		              tsc::Tristate::True);
		assert::Equal(t, oldProgram->Options()->Strict,
		              tsc::Tristate::Unknown);
	});

	t->Run("NewFiles when import resolution mode changes", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/src/tsconfig.json", R"({
				"compilerOptions":{"module":"preserve","moduleResolution":"bundler","noEmit":true},
				"files":["index.ts"]
			})"},
		    {"/src/index.ts", R"(import type { Value } from "pkg" with { "resolution-mode": "require" };
const value: Value = { mode: "require" };)"},
		    {"/src/node_modules/pkg/package.json", R"({
				"name": "pkg",
				"version": "1.0.0",
				"exports": {
					".": {
						"import": "./index.mjs",
						"require": "./index.js"
					}
				}
			})"},
		    {"/src/node_modules/pkg/index.d.mts",
		     R"(export interface Value { mode: "import" })"},
		    {"/src/node_modules/pkg/index.d.ts",
		     R"(export interface Value { mode: "require" })"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		t->Cleanup([session] { session->Close(); });
		lsproto::DocumentUri uri("file:///src/index.ts");
		session->DidOpenFile(t->Context(), uri, 1,
		                     fileText(files, "/src/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		auto lsP = session->GetLanguageService(t->Context(), uri);
		assert::NilError(t, lsP.second);
		auto* program = lsP.first->GetProgram();
		assert::Equal(
		    t,
		    int64_t(program
		                ->GetSemanticDiagnostics(
		                    projecttestutil::WithRequestID(
		                        t->Context()),
		                    program->GetSourceFile("/src/index.ts"))
		                .size()),
		    int64_t(0));

		{
			lsproto::TextDocumentContentChangePartialOrWholeDocument
			    change;
			change.WholeDocument = std::make_shared<
			    lsproto::TextDocumentContentChangeWholeDocument>();
			change.WholeDocument->Text =
			    R"(import type { Value } from "pkg" with { "resolution-mode": "import" };
const value: Value = { mode: "require" };)";
			session->DidChangeFile(t->Context(), uri, 2, {change});
		}
		auto lsP2 = session->GetLanguageService(t->Context(), uri);
		assert::NilError(t, lsP2.second);
		program = lsP2.first->GetProgram();
		auto diags = program->GetSemanticDiagnostics(
		    projecttestutil::WithRequestID(t->Context()),
		    program->GetSourceFile("/src/index.ts"));
		assert::Equal(t, int64_t(diags.size()), int64_t(1));
		assert::Equal(t, diags[0]->Code(),
		              tsc::Type_0_is_not_assignable_to_type_1
		                  ->code);

		auto* configured = session->Snapshot()
		                       ->ProjectCollection->ConfiguredProject(
		                           tsc::tspath::Path("/src/tsconfig.json"));
		assert::Assert(t, configured != nullptr);
		assert::Equal(t, configured->ProgramUpdateKind,
		              project::ProgramUpdateKindNewFiles);
		delete lsP.first;
		delete lsP2.first;
	});

	t->Run("SameFileNames on config change without root changes",
	       [&](T* t) {
		       t->Parallel();
		       FileMap files{
		           {"/src/tsconfig.json",
		            R"({"compilerOptions": {"strict": true}})"},
		           {"/src/index.ts", "export const x = 1;"},
		       };
		       auto p0 = projecttestutil::Setup(files);
		       auto* session = p0.first;
		       auto& utils = p0.second;
		       t->Cleanup([session] { session->Close(); });
		       session->DidOpenFile(t->Context(),
		                            "file:///src/index.ts", 1,
		                            fileText(files, "/src/index.ts"),
		                            lsproto::LanguageKindTypeScript);
		       {
			       auto gP = session->GetLanguageService(
			           t->Context(),
			           lsproto::DocumentUri("file:///src/index.ts"));
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }
		       auto werr =
		           utils->FS()->WriteFile("/src/tsconfig.json",
		                                  R"({"compilerOptions": {"strict": false}})");
		       assert::Assert(t, werr.impl() == nullptr);
		       {
			       lsproto::FileEvent fe = fileEvent(
			           lsproto::FileChangeTypeChanged,
			           "file:///src/tsconfig.json");
			       session->DidChangeWatchedFiles(t->Context(),
			                                    {&fe});
		       }
		       {
			       auto gP = session->GetLanguageService(
			           t->Context(),
			           lsproto::DocumentUri("file:///src/index.ts"));
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }
		       auto* snapshot = session->Snapshot();
		       auto* configured =
		           snapshot->ProjectCollection->ConfiguredProject(
		               tsc::tspath::Path("/src/tsconfig.json"));
		       assert::Assert(t, configured != nullptr);
		       assert::Equal(t, configured->ProgramUpdateKind,
		                     project::ProgramUpdateKindSameFileNames);
	       });

	t->Run("NewFiles on root addition", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/src/tsconfig.json", "{}"},
		    {"/src/index.ts", "export {}"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		auto& utils = p0.second;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(), "file:///src/index.ts", 1,
		                     fileText(files, "/src/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		std::string content = "export const y = 2;";
		auto werr = utils->FS()->WriteFile("/src/newfile.ts", content);
		assert::Assert(t, werr.impl() == nullptr);
		{
			lsproto::FileEvent fe =
			    fileEvent(lsproto::FileChangeTypeCreated,
			              "file:///src/newfile.ts");
			session->DidChangeWatchedFiles(t->Context(), {&fe});
		}
		session->DidOpenFile(t->Context(), "file:///src/newfile.ts", 1,
		                     content, lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src/newfile.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		auto* snapshot = session->Snapshot();
		auto* configured =
		    snapshot->ProjectCollection->ConfiguredProject(
		        tsc::tspath::Path("/src/tsconfig.json"));
		assert::Assert(t, configured != nullptr);
		assert::Equal(t, configured->ProgramUpdateKind,
		              project::ProgramUpdateKindNewFiles);
	});

	t->Run("SameFileNames when adding an unresolvable import with "
	       "multi-file change",
	       [&](T* t) {
		       t->Parallel();
		       FileMap files{
		           {"/src/tsconfig.json", "{}"},
		           {"/src/index.ts", "export const x = 1;"},
		           {"/src/other.ts", "export const z = 3;"},
		       };
		       auto p0 = projecttestutil::Setup(files);
		       auto* session = p0.first;
		       t->Cleanup([session] { session->Close(); });
		       session->DidOpenFile(t->Context(),
		                            "file:///src/index.ts", 1,
		                            fileText(files, "/src/index.ts"),
		                            lsproto::LanguageKindTypeScript);
		       {
			       auto gP = session->GetLanguageService(
			           t->Context(),
			           lsproto::DocumentUri("file:///src/index.ts"));
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }
		       // Change index.ts to add an unresolvable import
		       {
			       lsproto::TextDocumentContentChangePartialOrWholeDocument
			           change;
			       change.Partial = std::make_shared<
			           lsproto::TextDocumentContentChangePartial>();
			       change.Partial->Text =
			           "\nimport \"./does-not-exist\";\n";
			       change.Partial->Range.Start =
			           lsproto::Position{0, 0};
			       change.Partial->Range.End =
			           lsproto::Position{0, 0};
			       session->DidChangeFile(t->Context(),
			                              "file:///src/index.ts", 2,
			                              {change});
		       }
		       {
			       auto gP = session->GetLanguageService(
			           t->Context(),
			           lsproto::DocumentUri("file:///src/index.ts"));
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }
		       auto* snapshot = session->Snapshot();
		       auto* configured =
		           snapshot->ProjectCollection->ConfiguredProject(
		               tsc::tspath::Path("/src/tsconfig.json"));
		       assert::Assert(t, configured != nullptr);
		       assert::Equal(t, configured->ProgramUpdateKind,
		                     project::ProgramUpdateKindSameFileNames);
	       });
}

void TestProject(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	t->Run("commandLineWithTypingsFiles is reset on CommandLine change",
	       [&](T* t) {
		       t->Parallel();
		       FileMap files{
		           {"/user/username/projects/project1/app.js", ""},
		           {"/user/username/projects/project1/package.json",
		            R"({"name":"p1","dependencies":{"jquery":"^3.1.0"}})"},
		           {"/user/username/projects/project2/app.js", ""},
		       };

		       auto tiOptions =
		           std::make_shared<
		               projecttestutil::TypingsInstallerOptions>();
		       tiOptions->PackageToFile = {
		           // Provide typings content to be installed for jquery
		           // so ATA actually installs something
		           {"jquery", "declare const $: { x: number }"},
		       };
		       auto p0 =
		           projecttestutil::SetupWithTypingsInstaller(files,
		                                                      tiOptions);
		       auto* session = p0.first;
		       auto& utils = p0.second;
		       t->Cleanup([session] { session->Close(); });

		       // 1) Open an inferred project file that triggers ATA
		       lsproto::DocumentUri uri1(
		           "file:///user/username/projects/project1/app.js");
		       session->DidOpenFile(t->Context(), uri1, 1,
		                            fileText(
		                                files,
		                                "/user/username/projects/project1/"
		                                "app.js"),
		                            lsproto::LanguageKindJavaScript);

		       // 2) Wait for ATA/background tasks to finish, then get a
		       //    language service for the first file
		       session->WaitForBackgroundTasks();
		       // Sanity check: ensure ATA performed at least one install
		       auto npmCalls = utils->NpmExecutor()->NpmInstallCalls();
		       assert::Assert(
		           t, int64_t(npmCalls.size()) > 0,
		           "expected at least one npm install call from ATA");
		       {
			       auto gP =
			           session->GetLanguageService(t->Context(), uri1);
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }

		       // 3) Open another inferred project file
		       lsproto::DocumentUri uri2(
		           "file:///user/username/projects/project2/app.js");
		       session->DidOpenFile(t->Context(), uri2, 1, "",
		                            lsproto::LanguageKindJavaScript);

		       // 4) Get a language service for the second file
		       //    If commandLineWithTypingsFiles was not reset, the new
		       //    program command line won't include the newly opened
		       //    file and this will fail.
		       {
			       auto gP =
			           session->GetLanguageService(t->Context(), uri2);
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }
	       });

	t->Run("inferred project rebuilt twice in one snapshot after "
	       "typings install does not crash",
	       [&](T* t) {
		       t->Parallel();
		       // Reproduces the nil-command-line crash in
		       // (*Program).SingleThreaded (issue #3873).
		       //
		       // `getCommandLineWithTypingsFiles` memoizes
		       // `commandLineWithTypingsFiles` behind a `sync.Once`.
		       // When `CommandLine` (or the inferred roots) change, the
		       // cached value is reset to nil but the `Once` is NOT
		       // reset. `Project.Clone()` produces a fresh `Once`, so
		       // the first mutation of a project within a snapshot
		       // normally clears the staleness. But if the *same*
		       // in-snapshot clone has its program built, then its roots
		       // change, then its program is built again, the second
		       // build hits a consumed `Once` and reads the nil cached
		       // value, passing a nil command line to
		       // `compiler.NewProgram`.
		       //
		       // This sequence is produced within a single snapshot by
		       // `DidRequestFile`'s build -> cleanupInferredProject ->
		       // build path, when:
		       //   1. the inferred project has installed typings (so the
		       //      `Once` path is taken), and
		       //   2. a pending content change marks the inferred
		       //      project dirty (forcing build #1), and
		       //   3. a pending tsconfig creation moves another open
		       //      file out of the inferred project, so
		       //      `cleanupInferredProject` changes the inferred
		       //      roots between the two builds.
		       FileMap files{
		           {"/user/username/projects/project1/a.js", ""},
		           {"/user/username/projects/project1/package.json",
		            R"({"name":"p1","dependencies":{"jquery":"^3.1.0"}})"},
		           {"/user/username/projects/project1/b.ts",
		            "export const y = 1;"},
		       };

		       auto tiOptions =
		           std::make_shared<
		               projecttestutil::TypingsInstallerOptions>();
		       tiOptions->PackageToFile = {
		           {"jquery", "declare const $: { x: number }"},
		       };
		       auto p0 =
		           projecttestutil::SetupWithTypingsInstaller(files,
		                                                      tiOptions);
		       auto* session = p0.first;
		       auto& utils = p0.second;
		       t->Cleanup([session] { session->Close(); });

		       lsproto::DocumentUri aURI(
		           "file:///user/username/projects/project1/a.js");
		       lsproto::DocumentUri bURI(
		           "file:///user/username/projects/project1/b.ts");

		       // 1) Open the file that triggers ATA, plus another file
		       //    that joins the inferred project.
		       session->DidOpenFile(t->Context(), aURI, 1, "",
		                            lsproto::LanguageKindJavaScript);
		       session->DidOpenFile(
		           t->Context(), bURI, 1,
		           fileText(files,
		                    "/user/username/projects/project1/b.ts"),
		           lsproto::LanguageKindTypeScript);

		       // 2) Let ATA install jquery typings, then build the
		       //    inferred program so its typings files are populated.
		       session->WaitForBackgroundTasks();
		       auto npmCalls = utils->NpmExecutor()->NpmInstallCalls();
		       assert::Assert(
		           t, int64_t(npmCalls.size()) > 0,
		           "expected at least one npm install call from ATA");
		       {
			       auto gP =
			           session->GetLanguageService(t->Context(), aURI);
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }

		       // 3) Queue two pending changes that will be flushed
		       //    together in the next snapshot:
		       //    - a content change to a.js (marks the inferred
		       //      project dirty -> first program build)
		       //    - creation of a tsconfig.json that captures b.ts
		       //      (moves it out of the inferred project, so
		       //      cleanupInferredProject shrinks the inferred roots
		       //      between builds).
		       {
			       lsproto::TextDocumentContentChangePartialOrWholeDocument
			           change;
			       change.WholeDocument = std::make_shared<
			           lsproto::TextDocumentContentChangeWholeDocument>();
			       change.WholeDocument->Text = "// changed";
			       session->DidChangeFile(t->Context(), aURI, 2,
			                              {change});
		       }
		       auto werr = utils->FS()->WriteFile(
		           "/user/username/projects/project1/tsconfig.json",
		           R"({"compilerOptions":{},"files":["b.ts"]})");
		       assert::Assert(t, werr.impl() == nullptr);
		       {
			       lsproto::FileEvent fe = fileEvent(
			           lsproto::FileChangeTypeCreated,
			           "file:///user/username/projects/project1/"
			           "tsconfig.json");
			       session->DidChangeWatchedFiles(t->Context(),
			                                    {&fe});
		       }

		       // 4) Flush the pending changes and the request in a
		       //    single snapshot. With the bug, the inferred
		       //    project's program is built, its roots change, and
		       //    it is rebuilt with a nil command line, panicking in
		       //    (*Program).SingleThreaded.
		       auto gP =
		           session->GetLanguageService(t->Context(), aURI);
		       assert::NilError(t, gP.second);
		       delete gP.first;
	       });
}

void TestPushDiagnostics(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	t->Run("publishes program diagnostics on initial program creation",
	       [&](T* t) {
		       t->Parallel();
		       FileMap files{
		           {"/src/tsconfig.json",
		            R"({"compilerOptions": {"baseUrl": "."}})"},
		           {"/src/index.ts", "export const x = 1;"},
		       };
		       auto p0 = projecttestutil::Setup(files);
		       auto* session = p0.first;
		       auto& utils = p0.second;
		       t->Cleanup([session] { session->Close(); });
		       session->DidOpenFile(t->Context(),
		                            "file:///src/index.ts", 1,
		                            fileText(files, "/src/index.ts"),
		                            lsproto::LanguageKindTypeScript);
		       {
			       auto gP = session->GetLanguageService(
			           t->Context(),
			           lsproto::DocumentUri("file:///src/index.ts"));
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }

		       session->WaitForBackgroundTasks();

		       auto calls =
		           utils->Client()->PublishDiagnosticsCalls();
		       assert::Assert(
		           t, int64_t(calls.size()) > 0,
		           "expected at least one PublishDiagnostics call");

		       // Find the call for tsconfig.json
		       const publishDiagnosticsCall* tsconfigCall = nullptr;
		       for (size_t i = 0; i < calls.size(); i++) {
			       if (calls[i].Params->Uri ==
			           lsproto::DocumentUri(
			               "file:///src/tsconfig.json")) {
				       tsconfigCall = &calls[i];
				       break;
			       }
		       }
		       assert::Assert(
		           t, tsconfigCall != nullptr,
		           "expected PublishDiagnostics call for tsconfig.json");
		       assert::Assert(
		           t,
		           diagSliceSize(tsconfigCall->Params->Diagnostics) > 0,
		           "expected at least one diagnostic");
	       });

	t->Run("publishes config parsing diagnostics on initial program "
	       "creation",
	       [&](T* t) {
		       t->Parallel();
		       FileMap files{
		           {"/src/tsconfig.json", R"({
				"compilerOptions": {
					"target": "nope"
				}
			})"},
		           {"/src/index.ts", "export const x = 1;"},
		       };
		       auto p0 = projecttestutil::Setup(files);
		       auto* session = p0.first;
		       auto& utils = p0.second;
		       t->Cleanup([session] { session->Close(); });
		       session->DidOpenFile(t->Context(),
		                            "file:///src/index.ts", 1,
		                            fileText(files, "/src/index.ts"),
		                            lsproto::LanguageKindTypeScript);
		       {
			       auto gP = session->GetLanguageService(
			           t->Context(),
			           lsproto::DocumentUri("file:///src/index.ts"));
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }

		       session->WaitForBackgroundTasks();

		       auto calls =
		           utils->Client()->PublishDiagnosticsCalls();
		       auto tsconfigCalls = filterDiagnosticsByURI(
		           calls,
		           lsproto::DocumentUri("file:///src/tsconfig.json"), 0);
		       assert::Assert(
		           t, int64_t(tsconfigCalls.size()) > 0,
		           "expected PublishDiagnostics call for tsconfig.json");
		       auto& lastTsconfigCall =
		           tsconfigCalls[tsconfigCalls.size() - 1];

		       const std::string expectedMessage =
		           "Argument for '--target' option must be:";
		       assert::Assert(
		           t,
		           std::any_of(
		               diagSlice(lastTsconfigCall.Params->Diagnostics).begin(),
		               diagSlice(lastTsconfigCall.Params->Diagnostics).end(),
		               [&](const std::shared_ptr<lsproto::Diagnostic>&
		                       diag) {
			               return diag->Message.AsString().find(
			                          expectedMessage) !=
			                      std::string::npos;
		               }),
		           "expected invalid target diagnostic on "
		           "tsconfig.json");
	       });

	t->Run("clears diagnostics when project is removed", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/src/tsconfig.json",
		     R"({"compilerOptions": {"baseUrl": "."}})"},
		    {"/src/index.ts", "export const x = 1;"},
		    {"/src2/tsconfig.json", R"({"compilerOptions": {}})"},
		    {"/src2/index.ts", "export const y = 2;"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		auto& utils = p0.second;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(), "file:///src/index.ts", 1,
		                     fileText(files, "/src/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		session->WaitForBackgroundTasks();

		// Open a file in a different project to trigger cleanup of the
		// first
		session->DidCloseFile(t->Context(), "file:///src/index.ts");
		session->DidOpenFile(t->Context(), "file:///src2/index.ts", 1,
		                     fileText(files, "/src2/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src2/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		session->WaitForBackgroundTasks();

		auto calls = utils->Client()->PublishDiagnosticsCalls();
		// Should have at least one call for the first project with
		// diagnostics, and one clearing it after switching projects
		std::vector<publishDiagnosticsCall> firstProjectCalls;
		for (size_t i = 0; i < calls.size(); i++) {
			if (calls[i].Params->Uri ==
			    lsproto::DocumentUri("file:///src/tsconfig.json")) {
				firstProjectCalls.push_back(calls[i]);
			}
		}
		assert::Assert(
		    t, int64_t(firstProjectCalls.size()) >= 2,
		    "expected at least 2 PublishDiagnostics calls for first "
		    "project");
		// Last call should clear diagnostics
		auto& lastCall =
		    firstProjectCalls[firstProjectCalls.size() - 1];
		assert::Equal(
		    t, diagSliceSize(lastCall.Params->Diagnostics),
		    int64_t(0),
		    "expected empty diagnostics after project cleanup");
	});

	t->Run("updates diagnostics when program changes", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/src/tsconfig.json",
		     R"({"compilerOptions": {"baseUrl": "."}})"},
		    {"/src/index.ts", "export const x = 1;"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		auto& utils = p0.second;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(), "file:///src/index.ts", 1,
		                     fileText(files, "/src/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		session->WaitForBackgroundTasks();

		size_t initialCallCount =
		    utils->Client()->PublishDiagnosticsCalls().size();

		// Change the tsconfig to remove baseUrl
		auto werr = utils->FS()->WriteFile(
		    "/src/tsconfig.json", R"({"compilerOptions": {}})");
		assert::Assert(t, werr.impl() == nullptr);
		{
			lsproto::FileEvent fe = fileEvent(
			    lsproto::FileChangeTypeChanged,
			    "file:///src/tsconfig.json");
			session->DidChangeWatchedFiles(t->Context(), {&fe});
		}
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		session->WaitForBackgroundTasks();

		auto calls = utils->Client()->PublishDiagnosticsCalls();
		assert::Assert(
		    t, calls.size() > initialCallCount,
		    "expected additional PublishDiagnostics call after change");

		// Find the last call for tsconfig.json
		const publishDiagnosticsCall* lastTsconfigCall = nullptr;
		for (size_t i = calls.size(); i-- > 0;) {
			if (calls[i].Params->Uri ==
			    lsproto::DocumentUri("file:///src/tsconfig.json")) {
				lastTsconfigCall = &calls[i];
				break;
			}
		}
		assert::Assert(
		    t, lastTsconfigCall != nullptr,
		    "expected PublishDiagnostics call for tsconfig.json");
		// After fixing the error, there should be no program
		// diagnostics
		assert::Equal(
		    t,
		    diagSliceSize(lastTsconfigCall->Params->Diagnostics),
		    int64_t(0),
		    "expected no diagnostics after removing baseUrl option");
	});

	t->Run("updates diagnostics when a config file changes on disk with "
	       "no follow-up request",
	       [&](T* t) {
		       t->Parallel();
		       FileMap files{
		           {"/src/tsconfig.json", R"({"compilerOptions": {}})"},
		           {"/src/index.ts", "export const x = 1;"},
		       };
		       auto p0 = projecttestutil::Setup(files);
		       auto* session = p0.first;
		       auto& utils = p0.second;
		       t->Cleanup([session] { session->Close(); });
		       session->DidOpenFile(t->Context(),
		                            "file:///src/index.ts", 1,
		                            fileText(files, "/src/index.ts"),
		                            lsproto::LanguageKindTypeScript);
		       {
			       auto gP = session->GetLanguageService(
			           t->Context(),
			           lsproto::DocumentUri("file:///src/index.ts"));
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }
		       session->WaitForBackgroundTasks();

		       size_t callsBeforeChange =
		           utils->Client()->PublishDiagnosticsCalls().size();

		       // Editors do not attach the language server to JSON
		       // documents, so a config file edit only reaches the
		       // session through the file watcher. Config file
		       // diagnostics are pushed, so they must be republished
		       // without waiting for a client request.
		       assert::Assert(
		           t, utils->FS()->WriteFile(
		                  "/src/tsconfig.json",
		                  R"({"compilerOptions": {"target": "nope"}})")
		                  .impl() == nullptr);
		       {
			       lsproto::FileEvent fe = fileEvent(
			           lsproto::FileChangeTypeChanged,
			           "file:///src/tsconfig.json");
			       session->DidChangeWatchedFiles(t->Context(),
			                                    {&fe});
		       }
		       session->WaitForBackgroundTasks();

		       auto calls =
		           utils->Client()->PublishDiagnosticsCalls();
		       auto tsconfigCalls = filterDiagnosticsByURI(
		           calls,
		           lsproto::DocumentUri("file:///src/tsconfig.json"),
		           callsBeforeChange);
		       assert::Assert(
		           t, int64_t(tsconfigCalls.size()) > 0,
		           "expected PublishDiagnostics call for tsconfig.json "
		           "after watched file change");
		       auto& lastTsconfigCall =
		           tsconfigCalls[tsconfigCalls.size() - 1];

		       const std::string expectedMessage =
		           "Argument for '--target' option must be:";
		       assert::Assert(
		           t,
		           std::any_of(
		               diagSlice(lastTsconfigCall.Params->Diagnostics).begin(),
		               diagSlice(lastTsconfigCall.Params->Diagnostics).end(),
		               [&](const std::shared_ptr<lsproto::Diagnostic>&
		                       diag) {
			               return diag->Message.AsString().find(
			                          expectedMessage) !=
			                      std::string::npos;
		               }),
		           "expected invalid target diagnostic on "
		           "tsconfig.json");
	       });

	t->Run("does not publish for inferred projects", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/src/index.ts", "let x: number = 'not a number';"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		auto& utils = p0.second;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(), "file:///src/index.ts", 1,
		                     fileText(files, "/src/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		session->WaitForBackgroundTasks();

		auto calls = utils->Client()->PublishDiagnosticsCalls();
		// Should not have any calls since inferred projects don't have
		// tsconfig.json
		assert::Equal(t, int64_t(calls.size()), int64_t(0),
		              "expected no PublishDiagnostics calls for inferred "
		              "projects");
	});

	t->Run("does not publish when validation is disabled", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/src/tsconfig.json", R"({
				"compilerOptions": {
					"target": "nope"
				}
			})"},
		    {"/src/index.ts", "export const x = 1;"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		auto& utils = p0.second;
		t->Cleanup([session] { session->Close(); });
		auto prefs = lsutil::NewDefaultUserPreferences();
		prefs.ValidateEnabled = tsc::Tristate::False;
		session->Configure(prefs);
		session->DidOpenFile(t->Context(), "file:///src/index.ts", 1,
		                     fileText(files, "/src/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		session->WaitForBackgroundTasks();

		auto calls = utils->Client()->PublishDiagnosticsCalls();
		for (auto& call : calls) {
			assert::Equal(
			    t, diagSliceSize(call.Params->Diagnostics),
			    int64_t(0),
			    "expected only empty PublishDiagnostics calls when "
			    "validation is disabled");
		}
	});

	t->Run("clears diagnostics when validation is disabled", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/src/tsconfig.json", R"({
				"compilerOptions": {
					"target": "nope"
				}
			})"},
		    {"/src/index.ts", "export const x = 1;"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		auto& utils = p0.second;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(), "file:///src/index.ts", 1,
		                     fileText(files, "/src/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		session->WaitForBackgroundTasks();

		auto calls = utils->Client()->PublishDiagnosticsCalls();
		auto tsconfigCalls = filterDiagnosticsByURI(
		    calls, lsproto::DocumentUri("file:///src/tsconfig.json"), 0);
		assert::Assert(
		    t, int64_t(tsconfigCalls.size()) > 0,
		    "expected initial PublishDiagnostics call for "
		    "tsconfig.json");
		assert::Assert(
		    t,
		    diagSliceSize(
		        tsconfigCalls[tsconfigCalls.size() - 1]
		            .Params->Diagnostics) > 0,
		    "expected initial diagnostics");

		auto prefs = lsutil::NewDefaultUserPreferences();
		prefs.ValidateEnabled = tsc::Tristate::False;
		session->Configure(prefs);
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri("file:///src/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		session->WaitForBackgroundTasks();

		calls = utils->Client()->PublishDiagnosticsCalls();
		tsconfigCalls = filterDiagnosticsByURI(
		    calls, lsproto::DocumentUri("file:///src/tsconfig.json"),
		    calls.size() - 1);
		if (tsconfigCalls.empty()) {
			tsconfigCalls = filterDiagnosticsByURI(
			    calls,
			    lsproto::DocumentUri("file:///src/tsconfig.json"),
			    0);
		}
		auto& lastTsconfigCall =
		    tsconfigCalls[tsconfigCalls.size() - 1];
		assert::Equal(
		    t,
		    diagSliceSize(lastTsconfigCall.Params->Diagnostics),
		    int64_t(0),
		    "expected diagnostics to be cleared when validation is "
		    "disabled");
	});

	t->Run("publishes global diagnostics after checking", [&](T* t) {
		t->Parallel();
		// Use a target/lib that does not include Disposable, then write
		// code that needs it. This triggers a deferred "Cannot find
		// global type 'Disposable'" global diagnostic during checking,
		// which should be accumulated and published on the tsconfig URI.
		FileMap files{
		    {"/src/tsconfig.json", R"({
				"compilerOptions": {
					"target": "es2020"
				}
			})"},
		    {"/src/index.ts", R"(export function f() {
				using x = { [Symbol.dispose]() {} };
			})"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		auto& utils = p0.second;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(), "file:///src/index.ts", 1,
		                     fileText(files, "/src/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		// Request semantic diagnostics to trigger checking, which
		// triggers the global type resolvers.
		auto lsP = session->GetLanguageService(
		    projecttestutil::WithRequestID(t->Context()),
		    lsproto::DocumentUri("file:///src/index.ts"));
		assert::NilError(t, lsP.second);
		// Drain background tasks from DidOpenFile
		// (publishProgramDiagnostics, etc.) before triggering global
		// diagnostics, to avoid racing with publishGlobalDiagnostics.
		session->WaitForBackgroundTasks();

		for (int i = 0; i < 2; i++) {
			auto* program = lsP.first->GetProgram();
			auto* file = program->GetSourceFile("/src/index.ts");
			auto diags = program->GetSemanticDiagnostics(
			    core::WithCheckerLifetime(
			        projecttestutil::WithRequestID(
			            gostd::contextBackground()),
			        core::CheckerLifetimeDiagnostics),
			    file);
			assert::Assert(t, int64_t(diags.size()) > 0);
			for (auto* diag : diags) {
				assert::Equal(t, diag->File(), file);
			}

			auto rp = lsP.first->ProvideDiagnostics(
			    core::WithCheckerLifetime(
			        projecttestutil::WithRequestID(t->Context()),
			        core::CheckerLifetimeDiagnostics),
			    lsproto::DocumentUri("file:///src/index.ts"));
			assert::NilError(t, rp.second);
			assert::Assert(
			    t, rp.first.FullDocumentDiagnosticReport != nullptr);
			bool hasSourceDiag = false;
			for (auto& diag : diagSlice(
			         rp.first.FullDocumentDiagnosticReport->Items)) {
				assert::Assert(
				    t,
				    diag->Message.AsString().find(
				        "Cannot find global") ==
				        std::string::npos,
				    "global diagnostic should only be "
				    "published on tsconfig.json");
				if (diag->Code != nullptr &&
				    diag->Code->Integer != nullptr &&
				    *diag->Code->Integer == 2550) {
					hasSourceDiag = true;
				}
			}
			assert::Assert(
			    t, hasSourceDiag,
			    "expected the source diagnostic about "
			    "Symbol.dispose");
		}
		// Enqueue global diagnostics publishing (normally done by the
		// LSP server after each request).
		session->EnqueuePublishGlobalDiagnostics();
		session->WaitForBackgroundTasks();

		auto calls = utils->Client()->PublishDiagnosticsCalls();
		// Find the last call for tsconfig.json
		const publishDiagnosticsCall* lastTsconfigCall = nullptr;
		for (size_t i = calls.size(); i-- > 0;) {
			if (calls[i].Params->Uri ==
			    lsproto::DocumentUri("file:///src/tsconfig.json")) {
				lastTsconfigCall = &calls[i];
				break;
			}
		}
		assert::Assert(
		    t, lastTsconfigCall != nullptr,
		    "expected PublishDiagnostics call for tsconfig.json");
		// Should have global diagnostics (e.g., Cannot find global type
		// 'Disposable')
		bool hasGlobalDiag = false;
		for (auto& diag : diagSlice(lastTsconfigCall->Params->Diagnostics)) {
			if (diag->Message.AsString().find("Cannot find global") !=
			    std::string::npos) {
				hasGlobalDiag = true;
				break;
			}
		}
		assert::Assert(
		    t, hasGlobalDiag,
		    "expected a 'Cannot find global' diagnostic on "
		    "tsconfig.json");
		delete lsP.first;
	});

	for (bool checkFirst : {false, true}) {
		std::string name = checkFirst
		                       ? "query globals after semantic checking"
		                       : "query globals before semantic checking";
		t->Run(name, [&, name](T* t) {
			t->Parallel();
			const lsproto::DocumentUri uri("file:///src/repro.ts");
			const std::string source =
			    R"(type Json = string | Json[];
type Parsed<T> = T extends object ? { [K in keyof T]: Parsed<T[K]> } : T;
declare function wrap<T>(value: T): Parsed<T>;
export const value = wrap({ items: [] as Json[] });)";
			FileMap files{
			    {"/src/tsconfig.json",
			     R"({"compilerOptions":{"strict":true,"noEmit":true}})"},
			    {"/src/repro.ts", source},
			};
			auto ctx = t->Context();
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });
			session->DidOpenFile(ctx, uri, 1, source,
			                     lsproto::LanguageKindTypeScript);
			auto lsP = session->GetLanguageService(
			    projecttestutil::WithRequestID(ctx), uri);
			assert::NilError(t, lsP.second);
			auto* service = lsP.first;
			session->WaitForBackgroundTasks();

			std::ostringstream output;
			auto record = [&](const std::string& caption,
			                  const auto& value) {
				auto pr =
				    json::marshalIndent(value, "", "  ");
				assert::Assert(t, pr.second.empty());
				output << "// " << caption << "\n"
				       << pr.first << "\n\n";
			};
			auto check = [&]() {
				auto rp = service->ProvideDiagnostics(
				    core::WithCheckerLifetime(
				        projecttestutil::WithRequestID(ctx),
				        core::CheckerLifetimeDiagnostics),
				    uri);
				assert::NilError(t, rp.second);
				record("Document diagnostics",
				       rp.first.FullDocumentDiagnosticReport);
			};
			if (checkFirst) {
				check();
			}
			for (int i = 0; i < 2; i++) {
				size_t before =
				    utils->Client()
				        ->PublishDiagnosticsCalls()
				        .size();
				lsproto::HoverParams params;
				params.TextDocument.Uri = uri;
				params.Position = lsproto::Position{3, 14};
				auto hover = service->ProvideHover(
				    projecttestutil::WithRequestID(ctx),
				    &params);
				record("Hover on value", hover.Hover);
				session->EnqueuePublishGlobalDiagnostics();
				session->WaitForBackgroundTasks();
				std::vector<lsproto::
				                PublishDiagnosticsParams*>
				    published;
				auto calls =
				    utils->Client()
				        ->PublishDiagnosticsCalls();
				for (size_t ci = before; ci < calls.size();
				     ci++) {
					published.push_back(calls[ci].Params);
				}
				record("Published after hover", published);
			}
			check();
			std::string baselineName = name;
			std::replace(baselineName.begin(),
			             baselineName.end(), ' ', '-');
			baseline::Run(t, baselineName + ".jsonc",
			              output.str(),
			              baseline::Options{"project"});
		});
	}

	t->Run("cleans tsconfig diagnostics after TS files close and "
	       "restores them after TS file is reopened",
	       [&](T* t) {
		       t->Parallel();
		       FileMap files{
		           {"/src/tsconfig.json",
		            R"({"compilerOptions": {"baseUrl": "."}})"},
		           {"/src/index.ts", "export const x = 1;"},
		       };
		       auto p0 = projecttestutil::Setup(files);
		       auto* session = p0.first;
		       auto& utils = p0.second;
		       t->Cleanup([session] { session->Close(); });
		       lsproto::DocumentUri uri("file:///src/index.ts");
		       session->DidOpenFile(t->Context(), uri, 1,
		                            fileText(files, "/src/index.ts"),
		                            lsproto::LanguageKindTypeScript);
		       {
			       auto gP =
			           session->GetLanguageService(t->Context(), uri);
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }
		       session->WaitForBackgroundTasks();

		       auto calls =
		           utils->Client()->PublishDiagnosticsCalls();
		       auto tsconfigCalls = filterDiagnosticsByURI(
		           calls,
		           lsproto::DocumentUri("file:///src/tsconfig.json"),
		           0);
		       assert::Assert(
		           t, int64_t(tsconfigCalls.size()) > 0,
		           "expected PublishDiagnostics call for tsconfig.json "
		           "after opening file");
		       assert::Equal(
		           t,
		           diagSliceSize(
		               tsconfigCalls[0].Params->Diagnostics),
		           int64_t(1),
		           "expected one diagnostic on tsconfig.json after "
		           "opening file");

		       size_t callsBeforeClose = calls.size();

		       session->DidCloseFile(t->Context(), uri);
		       session->WaitForBackgroundTasks();

		       // Cleans up diagnostics after close
		       calls = utils->Client()->PublishDiagnosticsCalls();
		       auto clearCalls = filterDiagnosticsByURI(
		           calls,
		           lsproto::DocumentUri("file:///src/tsconfig.json"),
		           callsBeforeClose);
		       assert::Assert(
		           t, int64_t(clearCalls.size()) > 0,
		           "expected PublishDiagnostics call for tsconfig.json "
		           "after project close");
		       auto& lastClearCall =
		           clearCalls[clearCalls.size() - 1];
		       assert::Equal(
		           t,
		           diagSliceSize(lastClearCall.Params->Diagnostics),
		           int64_t(0),
		           "expected empty diagnostics after project close");

		       size_t callsBeforeReopen = calls.size();

		       session->DidOpenFile(t->Context(), uri, 2,
		                            fileText(files, "/src/index.ts"),
		                            lsproto::LanguageKindTypeScript);
		       {
			       auto gP =
			           session->GetLanguageService(t->Context(), uri);
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }
		       session->WaitForBackgroundTasks();

		       // Restores diagnostics after reopen
		       calls = utils->Client()->PublishDiagnosticsCalls();
		       auto reopenedCalls = filterDiagnosticsByURI(
		           calls,
		           lsproto::DocumentUri("file:///src/tsconfig.json"),
		           callsBeforeReopen);
		       assert::Assert(
		           t, int64_t(reopenedCalls.size()) > 0,
		           "expected PublishDiagnostics call for tsconfig.json "
		           "after reopening file");
		       auto& lastReopenedCall =
		           reopenedCalls[reopenedCalls.size() - 1];
		       assert::Equal(
		           t,
		           diagSliceSize(lastReopenedCall.Params->Diagnostics),
		           int64_t(1),
		           "expected one diagnostic on tsconfig.json after "
		           "reopening file");
	       });
}

void TestDisplayName(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	t->Run("configured project returns relative config path", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/home/projects/tsconfig.json", "{}"},
		    {"/home/projects/index.ts", "export const x = 1;"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(),
		                     "file:///home/projects/index.ts", 1,
		                     "export const x = 1;",
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(), lsproto::DocumentUri(
			                      "file:///home/projects/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}

		auto* snapshot = session->Snapshot();
		auto* configured =
		    snapshot->ProjectCollection->ConfiguredProject(
		        tsc::tspath::Path("/home/projects/tsconfig.json"));
		assert::Assert(t, configured != nullptr);
		assert::Equal(t, configured->DisplayName("/home/projects"),
		              std::string("tsconfig.json"));
	});

	t->Run("configured project with nested config", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/home/projects/sub/tsconfig.json", "{}"},
		    {"/home/projects/sub/index.ts", "export const x = 1;"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(),
		                     "file:///home/projects/sub/index.ts", 1,
		                     "export const x = 1;",
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(), lsproto::DocumentUri(
			                      "file:///home/projects/sub/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}

		auto* snapshot = session->Snapshot();
		auto* configured =
		    snapshot->ProjectCollection->ConfiguredProject(
		        tsc::tspath::Path(
		            "/home/projects/sub/tsconfig.json"));
		assert::Assert(t, configured != nullptr);
		assert::Equal(t, configured->DisplayName("/home/projects"),
		              std::string("sub/tsconfig.json"));
	});

	t->Run("configured project preserves config path casing", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/home/projects/Project/tsconfig.json", "{}"},
		    {"/home/projects/Project/index.ts", "export const x = 1;"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(),
		                     "file:///home/projects/Project/index.ts", 1,
		                     "export const x = 1;",
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(),
			    lsproto::DocumentUri(
			        "file:///home/projects/Project/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}

		auto* configured = session->Snapshot()
		                       ->ProjectCollection->ConfiguredProject(
		                           tsc::tspath::Path(
		                               "/home/projects/project/"
		                               "tsconfig.json"));
		assert::Assert(t, configured != nullptr);
		assert::Equal(t, configured->DisplayName("/home/projects"),
		              std::string("Project/tsconfig.json"));
	});

	t->Run("inferred project returns directory base name", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/home/projects/index.ts", "export const x = 1;"},
		};
		project::SessionOptions options;
		options.CurrentDirectory = "/home/projects";
		options.DefaultLibraryPath = tsc::bundled::LibPath();
		options.PositionEncoding =
		    lsproto::PositionEncodingKindUTF8;
		options.WatchEnabled = true;
		options.LoggingEnabled = true;
		options.PushDiagnosticsEnabled = true;
		auto p0 = projecttestutil::SetupWithOptions(files, &options);
		auto* session = p0.first;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(),
		                     "file:///home/projects/index.ts", 1,
		                     "export const x = 1;",
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(), lsproto::DocumentUri(
			                      "file:///home/projects/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}

		auto* snapshot = session->Snapshot();
		auto* inferred =
		    snapshot->ProjectCollection->InferredProject();
		assert::Assert(t, inferred != nullptr);
		auto name = inferred->DisplayName("/home");
		assert::Equal(t, name, std::string("projects"));
	});
}

void TestProgressNotifications(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	t->Run("emits progress for configured project loading", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/home/projects/tsconfig.json", "{}"},
		    {"/home/projects/index.ts", "export const x = 1;"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		auto& utils = p0.second;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(),
		                     "file:///home/projects/index.ts", 1,
		                     "export const x = 1;",
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(), lsproto::DocumentUri(
			                      "file:///home/projects/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}

		auto startCalls = utils->Client()->ProgressStartCalls();
		auto finishCalls = utils->Client()->ProgressFinishCalls();

		assert::Assert(t, int64_t(startCalls.size()) > 0,
		               "expected at least one ProgressStart call");
		assert::Assert(t, int64_t(finishCalls.size()) > 0,
		               "expected at least one ProgressFinish call");

		bool foundProjectStart = false;
		for (auto& call : startCalls) {
			if (call.Message == tsc::Project_0) {
				foundProjectStart = true;
				break;
			}
		}
		assert::Assert(t, foundProjectStart,
		               "expected ProgressStart with Project_0 message");

		bool foundProjectFinish = false;
		for (auto& call : finishCalls) {
			if (call.Message == tsc::Project_0) {
				foundProjectFinish = true;
				break;
			}
		}
		assert::Assert(t, foundProjectFinish,
		               "expected ProgressFinish with Project_0 message");
	});

	t->Run("emits progress for inferred project loading", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/home/projects/index.ts", "export const x = 1;"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		auto& utils = p0.second;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(),
		                     "file:///home/projects/index.ts", 1,
		                     "export const x = 1;",
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(), lsproto::DocumentUri(
			                      "file:///home/projects/index.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}

		auto startCalls = utils->Client()->ProgressStartCalls();
		auto finishCalls = utils->Client()->ProgressFinishCalls();

		assert::Assert(t, int64_t(startCalls.size()) > 0,
		               "expected at least one ProgressStart call");
		assert::Assert(t, int64_t(finishCalls.size()) > 0,
		               "expected at least one ProgressFinish call");

		bool foundProjectStart = false;
		for (auto& call : startCalls) {
			if (call.Message == tsc::Project_0) {
				foundProjectStart = true;
				break;
			}
		}
		assert::Assert(t, foundProjectStart,
		               "expected ProgressStart with Project_0 message");
	});

	t->Run("each start has a matching finish", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/home/projects/tsconfig.json", "{}"},
		    {"/home/projects/a.ts", "export const a = 1;"},
		    {"/home/projects/b.ts", "export const b = 2;"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		auto& utils = p0.second;
		t->Cleanup([session] { session->Close(); });
		session->DidOpenFile(t->Context(), "file:///home/projects/a.ts", 1,
		                     "export const a = 1;",
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(
			    t->Context(), lsproto::DocumentUri(
			                      "file:///home/projects/a.ts"));
			assert::NilError(t, gP.second);
			delete gP.first;
		}

		auto startCalls = utils->Client()->ProgressStartCalls();
		auto finishCalls = utils->Client()->ProgressFinishCalls();

		int64_t starts = 0;
		int64_t finishes = 0;
		for (auto& call : startCalls) {
			if (call.Message == tsc::Project_0) {
				starts++;
			}
		}
		for (auto& call : finishCalls) {
			if (call.Message == tsc::Project_0) {
				finishes++;
			}
		}
		assert::Equal(t, starts, finishes,
		              "ProgressStart and ProgressFinish calls for "
		              "Project_0 should be balanced");
	});
}

REGISTER_UNIT_TEST("project.TestProjectProgramUpdateKind",
                   TestProjectProgramUpdateKind);
REGISTER_UNIT_TEST("project.TestProject", TestProject);
REGISTER_UNIT_TEST("project.TestPushDiagnostics", TestPushDiagnostics);
REGISTER_UNIT_TEST("project.TestDisplayName", TestDisplayName);
REGISTER_UNIT_TEST("project.TestProgressNotifications",
                   TestProgressNotifications);

}  // namespace
