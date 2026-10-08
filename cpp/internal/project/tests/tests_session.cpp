// Port of tsc/internal/project/session_test.go.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/locale/locale.h"
#include "internal/ls/ls.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/project/project.h"
#include "internal/project/session.h"
#include "internal/project/snapshot.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/iovfs/iovfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace gostd = tsc::gostd;
namespace core = tsc::core;
namespace locale = tsc::locale;
namespace lsproto = tsc::lsp::lsproto;
namespace lsutil = tsc::ls::lsutil;
namespace project = tsc::project;
namespace projecttestutil = tsc::testutil::projecttestutil;
namespace vfstest = tsc::vfs::vfstest;
using tsc::gostd::testing::T;
using projecttestutil::FileMap;

static lsproto::FileEvent fileEvent(lsproto::FileChangeType type,
                                    const char* uri) {
	lsproto::FileEvent fe;
	fe.Type = type;
	fe.Uri = lsproto::DocumentUri(uri);
	return fe;
}

static lsproto::TextDocumentContentChangePartialOrWholeDocument
partialChange(uint32_t startChar, uint32_t endChar,
              const std::string& text) {
	auto change =
	    std::make_shared<lsproto::TextDocumentContentChangePartial>();
	change->Range.Start.Line = 0;
	change->Range.Start.Character = startChar;
	change->Range.End.Line = 0;
	change->Range.End.Character = endChar;
	change->Text = text;
	lsproto::TextDocumentContentChangePartialOrWholeDocument ch;
	ch.Partial = change;
	return ch;
}

static bool fileNamesContain(
    const std::vector<std::string>& fileNames, const std::string& p) {
	return std::find(fileNames.begin(), fileNames.end(), p) !=
	       fileNames.end();
}

static std::string fileText(const FileMap& files,
                            const std::string& name) {
	return std::get<std::string>(files.at(name));
}

void TestSession(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	FileMap defaultFiles{
	    {"/home/projects/TS/p1/tsconfig.json",
	     R"({
			"compilerOptions": {
				"noLib": true,
				"module": "nodenext",
				"strict": true
			},
			"include": ["src"]
		})"},
	    {"/home/projects/TS/p1/src/index.ts",
	     "import { x } from \"./x\";"},
	    {"/home/projects/TS/p1/src/x.ts", "export const x = 1;"},
	    {"/home/projects/TS/p1/config.ts", "let x = 1, y = 2;"},
	};

	t->Run("DidOpenFile", [&](T* t) {
		t->Parallel();
		t->Run("create configured project", [&](T* t) {
			t->Parallel();
			auto p0 = projecttestutil::Setup(defaultFiles);
			auto* session = p0.first;
			t->Cleanup([session] { session->Close(); });
			auto* snapshot = session->Snapshot();
			assert::Equal(
			    t,
			    int64_t(snapshot->ProjectCollection->Projects().size()),
			    int64_t(0));

			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(defaultFiles,
			             "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			snapshot = session->Snapshot();
			assert::Equal(
			    t,
			    int64_t(snapshot->ProjectCollection->Projects().size()),
			    int64_t(1));

			auto* configuredProject =
			    snapshot->ProjectCollection->ConfiguredProject(
			        tsc::tspath::Path("/home/projects/ts/p1/tsconfig.json"));
			assert::Assert(t, configuredProject != nullptr);

			// Get language service to access the program
			auto lsP = session->GetLanguageService(
			    t->Context(), "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP.second);
			auto* program = lsP.first->GetProgram();
			assert::Assert(
			    t,
			    program->GetSourceFile(
			        "/home/projects/TS/p1/src/x.ts") != nullptr);
			assert::Equal(
			    t,
			    program->GetSourceFile("/home/projects/TS/p1/src/x.ts")
			        ->Text(),
			    std::string("export const x = 1;"));
			delete lsP.first;
		});

		t->Run("create inferred project", [&](T* t) {
			t->Parallel();
			auto p0 = projecttestutil::Setup(defaultFiles);
			auto* session = p0.first;
			t->Cleanup([session] { session->Close(); });

			session->DidOpenFile(
			    t->Context(), "file:///home/projects/TS/p1/config.ts", 1,
			    fileText(defaultFiles, "/home/projects/TS/p1/config.ts"),
			    lsproto::LanguageKindTypeScript);

			// Find tsconfig, load, notice config.ts is not included,
			// create inferred project
			auto* snapshot = session->Snapshot();
			assert::Equal(
			    t,
			    int64_t(snapshot->ProjectCollection->Projects().size()),
			    int64_t(2));

			// Should have both configured project (for tsconfig.json)
			// and inferred project
			auto* configuredProject =
			    snapshot->ProjectCollection->ConfiguredProject(
			        tsc::tspath::Path("/home/projects/ts/p1/tsconfig.json"));
			auto* inferredProject =
			    snapshot->ProjectCollection->InferredProject();
			assert::Assert(t, configuredProject != nullptr);
			assert::Assert(t, inferredProject != nullptr);
		});

		t->Run("inferred project for in-memory files", [&](T* t) {
			t->Parallel();
			auto p0 = projecttestutil::Setup(defaultFiles);
			auto* session = p0.first;
			t->Cleanup([session] { session->Close(); });

			session->DidOpenFile(
			    t->Context(), "file:///home/projects/TS/p1/config.ts", 1,
			    fileText(defaultFiles, "/home/projects/TS/p1/config.ts"),
			    lsproto::LanguageKindTypeScript);
			session->DidOpenFile(t->Context(), "untitled:Untitled-1", 1,
			                     "x", lsproto::LanguageKindTypeScript);
			session->DidOpenFile(t->Context(), "untitled:Untitled-2", 1,
			                     "y", lsproto::LanguageKindTypeScript);

			auto* snapshot = session->Snapshot();

			assert::Equal(
			    t,
			    int64_t(snapshot->ProjectCollection->Projects().size()),
			    int64_t(1));
			assert::Assert(
			    t,
			    snapshot->ProjectCollection->InferredProject() !=
			        nullptr);
		});

		t->Run("inferred project JS file", [&](T* t) {
			t->Parallel();
			FileMap jsFiles{
			    {"/home/projects/TS/p1/index.js",
			     "import { x } from \"./x\";"},
			};
			auto p0 = projecttestutil::Setup(jsFiles);
			auto* session = p0.first;
			t->Cleanup([session] { session->Close(); });

			session->DidOpenFile(
			    t->Context(), "file:///home/projects/TS/p1/index.js", 1,
			    fileText(jsFiles, "/home/projects/TS/p1/index.js"),
			    lsproto::LanguageKindJavaScript);

			auto* snapshot = session->Snapshot();
			assert::Equal(
			    t,
			    int64_t(snapshot->ProjectCollection->Projects().size()),
			    int64_t(1));

			auto lsP = session->GetLanguageService(
			    t->Context(), "file:///home/projects/TS/p1/index.js");
			assert::NilError(t, lsP.second);
			auto* program = lsP.first->GetProgram();
			assert::Assert(
			    t,
			    program->GetSourceFile(
			        "/home/projects/TS/p1/index.js") != nullptr);
			delete lsP.first;
		});

		t->Run("inferred project extensionless file", [&](T* t) {
			t->Parallel();
			FileMap files{
			    {"/home/projects/TS/p1/script", "const x = 1;"},
			};
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			t->Cleanup([session] { session->Close(); });

			session->DidOpenFile(
			    t->Context(), "file:///home/projects/TS/p1/script", 1,
			    fileText(files, "/home/projects/TS/p1/script"),
			    lsproto::LanguageKind("plaintext"));

			auto* snapshot = session->Snapshot();
			assert::Equal(
			    t,
			    int64_t(snapshot->ProjectCollection->Projects().size()),
			    int64_t(1));
			assert::Assert(
			    t,
			    snapshot->ProjectCollection->InferredProject() !=
			        nullptr);

			auto lsP = session->GetLanguageService(
			    t->Context(), "file:///home/projects/TS/p1/script");
			assert::NilError(t, lsP.second);
			auto* program = lsP.first->GetProgram();
			auto* file =
			    program->GetSourceFile("/home/projects/TS/p1/script");
			assert::Assert(t, file != nullptr);
			assert::Equal(t, int32_t(file->ScriptKind),
			              int32_t(tsc::ScriptKind::TS));
			delete lsP.first;
		});
	});

	t->Run("watchChange and didOpen in same batch rebuilds program",
	       [&](T* t) {
		       t->Parallel();
		       FileMap files{
		           {"/home/projects/TS/p1/tsconfig.json",
		            R"({
				"compilerOptions": {
					"noLib": true,
					"strict": true
				}
			})"},
		           {"/home/projects/TS/p1/src/a.ts", "export const a = 1;\n"},
		           {"/home/projects/TS/p1/src/b.ts", "export const b = 1;\n"},
		       };
		       auto p0 = projecttestutil::Setup(files);
		       auto* session = p0.first;
		       auto& utils = p0.second;
		       t->Cleanup([session] { session->Close(); });
		       std::string oldContent =
		           fileText(files, "/home/projects/TS/p1/src/a.ts");

		       // Open b.ts to create the project; a.ts is included via
		       // tsconfig.
		       session->DidOpenFile(
		           t->Context(), "file:///home/projects/TS/p1/src/b.ts", 1,
		           fileText(files, "/home/projects/TS/p1/src/b.ts"),
		           lsproto::LanguageKindTypeScript);

		       // Verify a.ts is in the program with the original
		       // content.
		       auto lsP = session->GetLanguageService(
		           t->Context(), "file:///home/projects/TS/p1/src/b.ts");
		       assert::NilError(t, lsP.second);
		       assert::Equal(
		           t,
		           lsP.first->GetProgram()
		               ->GetSourceFile("/home/projects/TS/p1/src/a.ts")
		               ->Text(),
		           oldContent);
		       delete lsP.first;

		       // Modify a.ts on disk (simulate a build tool or git
		       // checkout).
		       std::string newContent =
		           "export const a = 2;\nexport const extra = true;\n";
		       auto werr = utils->FS()->WriteFile(
		           "/home/projects/TS/p1/src/a.ts", newContent);
		       assert::Assert(t, werr.impl() == nullptr);

		       // Queue a watch event for the disk change (not flushed
		       // yet).
		       {
			       lsproto::FileEvent fe =
			           fileEvent(lsproto::FileChangeTypeChanged,
			                     "file:///home/projects/TS/p1/src/a.ts");
			       session->DidChangeWatchedFiles(t->Context(),
			                                    {&fe});
		       }

		       // Open a.ts in the editor—flushes both watch event and
		       // didOpen together.
		       // Before the fix, processChanges would discard the watch
		       // event, leaving the project with a stale SourceFile
		       // and a mismatched line map.
		       session->DidOpenFile(
		           t->Context(), "file:///home/projects/TS/p1/src/a.ts", 1,
		           newContent, lsproto::LanguageKindTypeScript);

		       // The program's SourceFile must reflect the overlay
		       // (new) content.
		       auto lsP2 = session->GetLanguageService(
		           t->Context(), "file:///home/projects/TS/p1/src/a.ts");
		       assert::NilError(t, lsP2.second);
		       assert::Equal(
		           t,
		           lsP2.first->GetProgram()
		               ->GetSourceFile("/home/projects/TS/p1/src/a.ts")
		               ->Text(),
		           newContent);
		       delete lsP2.first;
	       });

	t->Run("DidChangeFile", [&](T* t) {
		t->Parallel();
		t->Run("update file and program", [&](T* t) {
			t->Parallel();
			auto p0 = projecttestutil::Setup(defaultFiles);
			auto* session = p0.first;
			t->Cleanup([session] { session->Close(); });

			session->DidOpenFile(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts", 1,
			    fileText(defaultFiles, "/home/projects/TS/p1/src/x.ts"),
			    lsproto::LanguageKindTypeScript);

			auto lsP = session->GetLanguageService(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts");
			assert::NilError(t, lsP.second);
			auto* programBefore = lsP.first->GetProgram();

			session->DidChangeFile(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts", 2,
			    {partialChange(17, 18, "2")});

			auto lsP2 = session->GetLanguageService(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts");
			assert::NilError(t, lsP2.second);
			auto* programAfter = lsP2.first->GetProgram();

			// Program should change due to the file content change
			assert::Check(t, programAfter != programBefore);
			assert::Equal(
			    t,
			    programAfter
			        ->GetSourceFile("/home/projects/TS/p1/src/x.ts")
			        ->Text(),
			    std::string("export const x = 2;"));
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("update untitled file", [&](T* t) {
			t->Parallel();
			auto p0 = projecttestutil::Setup(defaultFiles);
			auto* session = p0.first;
			t->Cleanup([session] { session->Close(); });

			session->DidOpenFile(t->Context(), "untitled:Untitled-1", 1,
			                     "let x = 1;",
			                     lsproto::LanguageKindTypeScript);

			auto lsP = session->GetLanguageService(t->Context(),
			                                     "untitled:Untitled-1");
			assert::NilError(t, lsP.second);
			auto* programBefore = lsP.first->GetProgram();
			std::string untitledFileName =
			    lsproto::documentUriFileName(
			        lsproto::DocumentUri("untitled:Untitled-1"));
			assert::Equal(
			    t,
			    programBefore->GetSourceFile(untitledFileName)->Text(),
			    std::string("let x = 1;"));

			session->DidChangeFile(t->Context(), "untitled:Untitled-1",
			                       2, {partialChange(8, 9, "2")});

			auto lsP2 = session->GetLanguageService(t->Context(),
			                                      "untitled:Untitled-1");
			assert::NilError(t, lsP2.second);
			auto* programAfter = lsP2.first->GetProgram();

			assert::Check(t, programAfter != programBefore);
			assert::Equal(
			    t,
			    programAfter->GetSourceFile(untitledFileName)->Text(),
			    std::string("let x = 2;"));
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("unchanged source files are reused", [&](T* t) {
			t->Parallel();
			auto p0 = projecttestutil::Setup(defaultFiles);
			auto* session = p0.first;
			t->Cleanup([session] { session->Close(); });

			session->DidOpenFile(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts", 1,
			    fileText(defaultFiles, "/home/projects/TS/p1/src/x.ts"),
			    lsproto::LanguageKindTypeScript);

			auto lsP = session->GetLanguageService(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts");
			assert::NilError(t, lsP.second);
			auto* programBefore = lsP.first->GetProgram();
			auto* indexFileBefore = programBefore->GetSourceFile(
			    "/home/projects/TS/p1/src/index.ts");

			session->DidChangeFile(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts", 2,
			    {partialChange(0, 0, ";")});

			auto lsP2 = session->GetLanguageService(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts");
			assert::NilError(t, lsP2.second);
			auto* programAfter = lsP2.first->GetProgram();

			// Unchanged file should be reused
			assert::Equal(
			    t,
			    programAfter->GetSourceFile(
			        "/home/projects/TS/p1/src/index.ts"),
			    indexFileBefore);
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("change can pull in new files", [&](T* t) {
			t->Parallel();
			FileMap files(defaultFiles);
			files["/home/projects/TS/p1/y.ts"] = "export const y = 2;";
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			t->Cleanup([session] { session->Close(); });

			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			// Verify y.ts is not initially in the program
			auto lsP = session->GetLanguageService(
			    t->Context(), "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP.second);
			auto* programBefore = lsP.first->GetProgram();
			assert::Check(t,
			              programBefore->GetSourceFile(
			                  "/home/projects/TS/p1/y.ts") == nullptr);

			session->DidChangeFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 2,
			    {partialChange(0, 0, "import { y } from \"../y\";\n")});

			auto lsP2 = session->GetLanguageService(
			    t->Context(), "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP2.second);
			auto* programAfter = lsP2.first->GetProgram();

			// y.ts should now be included in the program
			assert::Assert(t,
			               programAfter->GetSourceFile(
			                   "/home/projects/TS/p1/y.ts") != nullptr);
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("single-file change followed by config change reloads "
		       "program",
		       [&](T* t) {
			       t->Parallel();
			       FileMap files(defaultFiles);
			       files["/home/projects/TS/p1/tsconfig.json"] =
			           R"({
				"compilerOptions": {
					"noLib": true,
					"module": "nodenext",
					"strict": true
				},
				"include": ["src/index.ts"]
			})";
			       auto p0 = projecttestutil::Setup(files);
			       auto* session = p0.first;
			       auto& utils = p0.second;
			       t->Cleanup([session] { session->Close(); });

			       session->DidOpenFile(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts", 1,
			           fileText(files,
			                    "/home/projects/TS/p1/src/index.ts"),
			           lsproto::LanguageKindTypeScript);

			       auto lsP = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts");
			       assert::NilError(t, lsP.second);
			       auto* programBefore = lsP.first->GetProgram();
			       assert::Equal(
			           t,
			           int64_t(programBefore->GetSourceFiles().size()),
			           int64_t(2));

			       session->DidChangeFile(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts", 2,
			           {partialChange(0, 0, "\n")});

			       auto werr = utils->FS()->WriteFile(
			           "/home/projects/TS/p1/tsconfig.json",
			           R"({
				"compilerOptions": {
					"noLib": true,
					"module": "nodenext",
					"strict": true
				},
				"include": ["./**/*"]
			})");
			       assert::Assert(t, werr.impl() == nullptr);

			       {
				       lsproto::FileEvent fe = fileEvent(
				           lsproto::FileChangeTypeChanged,
				           "file:///home/projects/TS/p1/tsconfig.json");
				       session->DidChangeWatchedFiles(
				           t->Context(), {&fe});
			       }

			       auto lsP2 = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts");
			       assert::NilError(t, lsP2.second);
			       auto* programAfter = lsP2.first->GetProgram();
			       assert::Equal(
			           t,
			           int64_t(programAfter->GetSourceFiles().size()),
			           int64_t(3));
			       delete lsP.first;
			       delete lsP2.first;
		       });
	});

	t->Run("DidCloseFile", [&](T* t) {
		t->Parallel();
		t->Run("Configured projects", [&](T* t) {
			t->Parallel();
			t->Run("delete a file, close it, recreate it", [&](T* t) {
				t->Parallel();
				FileMap files(defaultFiles);
				auto p0 = projecttestutil::Setup(files);
				auto* session = p0.first;
				auto& utils = p0.second;
				t->Cleanup([session] { session->Close(); });

				session->DidOpenFile(
				    t->Context(),
				    "file:///home/projects/TS/p1/src/x.ts", 1,
				    fileText(files,
				             "/home/projects/TS/p1/src/x.ts"),
				    lsproto::LanguageKindTypeScript);
				session->DidOpenFile(
				    t->Context(),
				    "file:///home/projects/TS/p1/src/index.ts",
				    1,
				    fileText(files,
				             "/home/projects/TS/p1/src/index.ts"),
				    lsproto::LanguageKindTypeScript);

				auto rerr = utils->FS()->Remove(
				    "/home/projects/TS/p1/src/x.ts");
				assert::Assert(t, rerr.impl() == nullptr);

				session->DidCloseFile(
				    t->Context(),
				    "file:///home/projects/TS/p1/src/x.ts");
				auto lsP = session->GetLanguageService(
				    t->Context(),
				    "file:///home/projects/TS/p1/src/index.ts");
				assert::NilError(t, lsP.second);
				auto* program = lsP.first->GetProgram();
				assert::Check(t,
				              program->GetSourceFile(
				                  "/home/projects/TS/p1/src/x.ts") ==
				                  nullptr);
				delete lsP.first;

				auto werr = utils->FS()->WriteFile(
				    "/home/projects/TS/p1/src/x.ts", "");
				assert::Assert(t, werr.impl() == nullptr);

				session->DidOpenFile(
				    t->Context(),
				    "file:///home/projects/TS/p1/src/x.ts", 1,
				    "", lsproto::LanguageKindTypeScript);

				auto lsP2 = session->GetLanguageService(
				    t->Context(),
				    "file:///home/projects/TS/p1/src/x.ts");
				assert::NilError(t, lsP2.second);
				program = lsP2.first->GetProgram();
				assert::Assert(
				    t,
				    program->GetSourceFile(
				        "/home/projects/TS/p1/src/x.ts") !=
				        nullptr);
				assert::Equal(
				    t,
				    program
				        ->GetSourceFile(
				            "/home/projects/TS/p1/src/x.ts")
				        ->Text(),
				    std::string(""));
				delete lsP2.first;
			});
		});

		t->Run("Inferred projects", [&](T* t) {
			t->Parallel();
			t->Run("delete a file, close it, recreate it", [&](T* t) {
				t->Parallel();
				FileMap files(defaultFiles);
				files.erase("/home/projects/TS/p1/tsconfig.json");
				auto p0 = projecttestutil::Setup(files);
				auto* session = p0.first;
				auto& utils = p0.second;
				t->Cleanup([session] { session->Close(); });

				session->DidOpenFile(
				    t->Context(),
				    "file:///home/projects/TS/p1/src/x.ts", 1,
				    fileText(files,
				             "/home/projects/TS/p1/src/x.ts"),
				    lsproto::LanguageKindTypeScript);
				session->DidOpenFile(
				    t->Context(),
				    "file:///home/projects/TS/p1/src/index.ts",
				    1,
				    fileText(files,
				             "/home/projects/TS/p1/src/index.ts"),
				    lsproto::LanguageKindTypeScript);

				auto rerr = utils->FS()->Remove(
				    "/home/projects/TS/p1/src/x.ts");
				assert::Assert(t, rerr.impl() == nullptr);

				session->DidCloseFile(
				    t->Context(),
				    "file:///home/projects/TS/p1/src/x.ts");

				auto lsP = session->GetLanguageService(
				    t->Context(),
				    "file:///home/projects/TS/p1/src/index.ts");
				assert::NilError(t, lsP.second);
				auto* program = lsP.first->GetProgram();
				assert::Check(t,
				              program->GetSourceFile(
				                  "/home/projects/TS/p1/src/x.ts") ==
				                  nullptr);
				delete lsP.first;

				auto werr = utils->FS()->WriteFile(
				    "/home/projects/TS/p1/src/x.ts", "");
				assert::Assert(t, werr.impl() == nullptr);

				session->DidOpenFile(
				    t->Context(),
				    "file:///home/projects/TS/p1/src/x.ts", 1,
				    "", lsproto::LanguageKindTypeScript);

				auto lsP2 = session->GetLanguageService(
				    t->Context(),
				    "file:///home/projects/TS/p1/src/x.ts");
				assert::NilError(t, lsP2.second);
				program = lsP2.first->GetProgram();
				assert::Assert(
				    t,
				    program->GetSourceFile(
				        "/home/projects/TS/p1/src/x.ts") !=
				        nullptr);
				assert::Equal(
				    t,
				    program
				        ->GetSourceFile(
				            "/home/projects/TS/p1/src/x.ts")
				        ->Text(),
				    std::string(""));
				delete lsP2.first;
			});

			t->Run("close untitled file", [&](T* t) {
				t->Parallel();
				auto p0 = projecttestutil::Setup(defaultFiles);
				auto* session = p0.first;
				t->Cleanup([session] { session->Close(); });

				session->DidOpenFile(t->Context(),
				                     "untitled:Untitled-1", 1,
				                     "let x = 1;",
				                     lsproto::LanguageKindTypeScript);
				session->DidCloseFile(t->Context(),
				                      "untitled:Untitled-1");
				session->DidOpenFile(t->Context(),
				                     "untitled:Untitled-2", 1, "",
				                     lsproto::LanguageKindTypeScript);
			});
		});
	});

	t->Run("DidSaveFile", [&](T* t) {
		t->Parallel();
		t->Run("save event first", [&](T* t) {
			t->Parallel();
			auto p0 = projecttestutil::Setup(defaultFiles);
			auto* session = p0.first;
			t->Cleanup([session] { session->Close(); });
			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(defaultFiles,
			             "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			auto* snapshot = session->Snapshot();
			assert::Equal(t, int64_t(snapshot->ID()), int64_t(1));

			session->DidSaveFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeChanged,
				    "file:///home/projects/TS/p1/src/index.ts");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}

			session->WaitForBackgroundTasks();
			snapshot = session->Snapshot();
			// We didn't need a snapshot change, but the session
			// overlays should be updated.
			assert::Equal(t, int64_t(snapshot->ID()), int64_t(1));

			// Open another file to force a snapshot update so we can
			// see the changes.
			session->DidOpenFile(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts", 1,
			    fileText(defaultFiles,
			             "/home/projects/TS/p1/src/x.ts"),
			    lsproto::LanguageKindTypeScript);
			snapshot = session->Snapshot();
			assert::Equal(t,
			              snapshot->GetFile(
			                  "/home/projects/TS/p1/src/index.ts")
			                  ->MatchesDiskText(),
			              true);
		});

		t->Run("watch event first", [&](T* t) {
			t->Parallel();
			auto p0 = projecttestutil::Setup(defaultFiles);
			auto* session = p0.first;
			t->Cleanup([session] { session->Close(); });
			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(defaultFiles,
			             "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			auto* snapshot = session->Snapshot();
			assert::Equal(t, int64_t(snapshot->ID()), int64_t(1));

			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeChanged,
				    "file:///home/projects/TS/p1/src/index.ts");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}
			session->DidSaveFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");

			session->WaitForBackgroundTasks();
			snapshot = session->Snapshot();
			// We didn't need a snapshot change, but the session
			// overlays should be updated.
			assert::Equal(t, int64_t(snapshot->ID()), int64_t(1));

			// Open another file to force a snapshot update so we can
			// see the changes.
			session->DidOpenFile(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts", 1,
			    fileText(defaultFiles,
			             "/home/projects/TS/p1/src/x.ts"),
			    lsproto::LanguageKindTypeScript);
			snapshot = session->Snapshot();
			assert::Equal(t,
			              snapshot->GetFile(
			                  "/home/projects/TS/p1/src/index.ts")
			                  ->MatchesDiskText(),
			              true);
		});
	});

	t->Run("Source file sharing", [&](T* t) {
		t->Parallel();
		t->Run("projects with similar options share source files",
		       [&](T* t) {
			       t->Parallel();
			       FileMap files(defaultFiles);
			       files["/home/projects/TS/p2/tsconfig.json"] =
			           R"({
				"compilerOptions": {
					"noLib": true,
					"module": "nodenext",
					"strict": true,
					"noCheck": true
				}
			})";
			       files["/home/projects/TS/p2/src/index.ts"] =
			           "import { x } from \"../../p1/src/x\";";
			       auto p0 = projecttestutil::Setup(files);
			       auto* session = p0.first;
			       t->Cleanup([session] { session->Close(); });

			       session->DidOpenFile(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts", 1,
			           fileText(files,
			                    "/home/projects/TS/p1/src/index.ts"),
			           lsproto::LanguageKindTypeScript);
			       session->DidOpenFile(
			           t->Context(),
			           "file:///home/projects/TS/p2/src/index.ts", 1,
			           fileText(files,
			                    "/home/projects/TS/p2/src/index.ts"),
			           lsproto::LanguageKindTypeScript);

			       auto* snapshot = session->Snapshot();
			       assert::Equal(
			           t,
			           int64_t(snapshot->ProjectCollection->Projects()
			                       .size()),
			           int64_t(2));

			       auto lsP1 = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts");
			       assert::NilError(t, lsP1.second);
			       auto* program1 = lsP1.first->GetProgram();

			       auto lsP2 = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p2/src/index.ts");
			       assert::NilError(t, lsP2.second);
			       auto* program2 = lsP2.first->GetProgram();

			       assert::Equal(
			           t,
			           program1->GetSourceFile(
			               "/home/projects/TS/p1/src/x.ts"),
			           program2->GetSourceFile(
			               "/home/projects/TS/p1/src/x.ts"));
			       delete lsP1.first;
			       delete lsP2.first;
		       });

		t->Run("projects with different options do not share source "
		       "files",
		       [&](T* t) {
			       t->Parallel();
			       FileMap files(defaultFiles);
			       files["/home/projects/TS/p2/tsconfig.json"] =
			           R"({
				"compilerOptions": {
					"noLib": true,
					"module": "nodenext",
					"strict": true,
					"moduleDetection": "auto"
				},
				"include": ["src"]
			})";
			       files["/home/projects/TS/p2/src/index.ts"] =
			           "import { x } from \"../../p1/src/x\";";
			       auto p0 = projecttestutil::Setup(files);
			       auto* session = p0.first;
			       t->Cleanup([session] { session->Close(); });

			       session->DidOpenFile(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts", 1,
			           fileText(files,
			                    "/home/projects/TS/p1/src/index.ts"),
			           lsproto::LanguageKindTypeScript);
			       session->DidOpenFile(
			           t->Context(),
			           "file:///home/projects/TS/p2/src/index.ts", 1,
			           fileText(files,
			                    "/home/projects/TS/p2/src/index.ts"),
			           lsproto::LanguageKindTypeScript);

			       auto* snapshot = session->Snapshot();
			       assert::Equal(
			           t,
			           int64_t(snapshot->ProjectCollection->Projects()
			                       .size()),
			           int64_t(2));

			       auto lsP1 = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts");
			       assert::NilError(t, lsP1.second);
			       auto* program1 = lsP1.first->GetProgram();

			       auto lsP2 = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p2/src/index.ts");
			       assert::NilError(t, lsP2.second);
			       auto* program2 = lsP2.first->GetProgram();

			       auto* x1 = program1->GetSourceFile(
			           "/home/projects/TS/p1/src/x.ts");
			       auto* x2 = program2->GetSourceFile(
			           "/home/projects/TS/p1/src/x.ts");
			       assert::Assert(t, x1 != nullptr && x2 != nullptr);
			       assert::Assert(t, x1 != x2);
			       delete lsP1.first;
			       delete lsP2.first;
		       });
	});

	t->Run("DidChangeWatchedFiles", [&](T* t) {
		t->Parallel();

		t->Run("change open file", [&](T* t) {
			t->Parallel();
			FileMap files(defaultFiles);
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });

			session->DidOpenFile(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/x.ts"),
			    lsproto::LanguageKindTypeScript);
			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			auto lsP = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP.second);
			auto* programBefore = lsP.first->GetProgram();

			auto werr = utils->FS()->WriteFile(
			    "/home/projects/TS/p1/src/x.ts", "export const x = 2;");
			assert::Assert(t, werr.impl() == nullptr);

			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeChanged,
				    "file:///home/projects/TS/p1/src/x.ts");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}

			auto lsP2 = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP2.second);
			// Program should remain the same since the file is open and
			// changes are handled through DidChangeTextDocument
			assert::Equal(t, programBefore, lsP2.first->GetProgram());
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("change closed program file", [&](T* t) {
			t->Parallel();
			FileMap files(defaultFiles);
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });

			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			auto lsP = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP.second);
			auto* programBefore = lsP.first->GetProgram();

			auto werr = utils->FS()->WriteFile(
			    "/home/projects/TS/p1/src/x.ts", "export const x = 2;");
			assert::Assert(t, werr.impl() == nullptr);

			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeChanged,
				    "file:///home/projects/TS/p1/src/x.ts");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}

			auto lsP2 = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP2.second);
			assert::Check(t, lsP2.first->GetProgram() != programBefore);
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("change program file not in tsconfig root files",
		       [&](T* t) {
			       t->Parallel();
			       for (std::string workspaceDir :
			            {"/", "/home/projects/TS/p1",
			             "/somewhere/else/entirely"}) {
				       std::string repl = workspaceDir;
				       std::replace(repl.begin(), repl.end(), '/',
				                    '_');
				       t->Run("workspaceDir=" + repl, [&](T* t) {
					       t->Parallel();
					       FileMap files{
					           {"/home/projects/TS/p1/tsconfig.json",
					            R"({
							"compilerOptions": {
								"noLib": true,
								"module": "nodenext",
								"strict": true
							},
							"files": ["src/index.ts"]
						})"},
					           {"/home/projects/TS/p1/src/index.ts",
					            "import { x } from \"../../x\";"},
					           {"/home/projects/TS/x.ts",
					            "export const x = 1;"},
					       };

					       project::SessionOptions options;
					       options.CurrentDirectory = workspaceDir;
					       options.DefaultLibraryPath =
					           tsc::bundled::LibPath();
					       options.TypingsLocation =
					           std::string(
					               projecttestutil::TestTypingsLocation);
					       options.PositionEncoding =
					           lsproto::PositionEncodingKindUTF8;
					       options.WatchEnabled = true;
					       options.LoggingEnabled = true;
					       auto p0 = projecttestutil::
					           SetupWithOptions(files, &options);
					       auto* session = p0.first;
					       auto& utils = p0.second;
					       t->Cleanup([session] {
						       session->Close();
					       });
					       session->DidOpenFile(
					           t->Context(),
					           "file:///home/projects/TS/p1/"
					           "src/index.ts",
					           1,
					           fileText(files,
					                    "/home/projects/TS/p1/"
					                    "src/index.ts"),
					           lsproto::LanguageKindTypeScript);
					       auto lsP =
					           session->GetLanguageService(
					               t->Context(),
					               "file:///home/projects/TS/"
					               "p1/src/index.ts");
					       assert::NilError(t, lsP.second);
					       auto* programBefore =
					           lsP.first->GetProgram();
					       session->WaitForBackgroundTasks();

					       assert::Check(
					           t, utils->WatchesFile(
					                  "/home/projects/ts/x.ts"));

					       auto werr = utils->FS()->WriteFile(
					           "/home/projects/TS/x.ts",
					           "export const x = 2;");
					       assert::Assert(
					           t, werr.impl() == nullptr);

					       {
						       lsproto::FileEvent fe =
						           fileEvent(
						               lsproto::
						                   FileChangeTypeChanged,
						               "file:///home/projects/"
						               "TS/x.ts");
						       session->
						       DidChangeWatchedFiles(
						           t->Context(), {&fe});
					       }

					       auto lsP2 =
					           session->GetLanguageService(
					               t->Context(),
					               "file:///home/projects/TS/"
					               "p1/src/index.ts");
					       assert::NilError(t, lsP2.second);
					       assert::Check(
					           t,
					           lsP2.first->GetProgram() !=
					               programBefore);
					       delete lsP.first;
					       delete lsP2.first;
				       });
			       }
		       });

		t->Run("change config file", [&](T* t) {
			t->Parallel();
			FileMap files{
			    {"/home/projects/TS/p1/tsconfig.json",
			     R"({
					"compilerOptions": {
						"noLib": true,
						"strict": false
					}
				})"},
			    {"/home/projects/TS/p1/src/x.ts",
			     "export declare const x: number | undefined;"},
			    {"/home/projects/TS/p1/src/index.ts",
			     "\n					import { x } from "
			     "\"./x\";\n					let y: number = x;"},
			};

			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });
			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			auto lsP = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP.second);
			auto* program = lsP.first->GetProgram();
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(0));

			auto werr = utils->FS()->WriteFile(
			    "/home/projects/TS/p1/tsconfig.json",
			    R"({
				"compilerOptions": {
					"noLib": false,
					"strict": true
				}
			})");
			assert::Assert(t, werr.impl() == nullptr);

			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeChanged,
				    "file:///home/projects/TS/p1/tsconfig.json");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}

			auto lsP2 = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP2.second);
			program = lsP2.first->GetProgram();
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(1));
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("delete explicitly included file", [&](T* t) {
			t->Parallel();
			FileMap files{
			    {"/home/projects/TS/p1/tsconfig.json",
			     R"({
					"compilerOptions": {
						"noLib": true
					},
					"files": ["src/index.ts", "src/x.ts"]
				})"},
			    {"/home/projects/TS/p1/src/x.ts",
			     "export declare const x: number | undefined;"},
			    {"/home/projects/TS/p1/src/index.ts",
			     "import { x } from \"./x\";"},
			};
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });
			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			auto lsP = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP.second);
			auto* program = lsP.first->GetProgram();
			assert::Check(
			    t,
			    fileNamesContain(
			        program->CommandLine()->ParsedConfig->FileNames,
			        "/home/projects/TS/p1/src/x.ts"));
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(0));

			auto rerr = utils->FS()->Remove(
			    "/home/projects/TS/p1/src/x.ts");
			assert::Assert(t, rerr.impl() == nullptr);

			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeDeleted,
				    "file:///home/projects/TS/p1/src/x.ts");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}

			auto lsP2 = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP2.second);
			program = lsP2.first->GetProgram();
			// File name is still in the command line, was explicitly
			// included
			assert::Check(
			    t,
			    fileNamesContain(
			        program->CommandLine()->ParsedConfig->FileNames,
			        "/home/projects/TS/p1/src/x.ts"));
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(1));
			assert::Check(
			    t, program->GetSourceFile(
			           "/home/projects/TS/p1/src/x.ts") == nullptr);
			delete lsP.first;
			delete lsP2.first;

			// Open file to trigger cleanup
			session->DidOpenFile(t->Context(), "untitled:Untitled-1", 1,
			                     "", lsproto::LanguageKindTypeScript);
			auto* snapshot = session->Snapshot();
			assert::Check(t,
			              snapshot->GetFile(
			                  "/home/projects/TS/p1/src/x.ts") == nullptr);
		});

		t->Run("delete wildcard included file", [&](T* t) {
			t->Parallel();
			FileMap files{
			    {"/home/projects/TS/p1/tsconfig.json",
			     R"({
					"compilerOptions": {
						"noLib": true
					},
					"include": ["src"]
				})"},
			    {"/home/projects/TS/p1/src/index.ts", "let x = 2;"},
			    {"/home/projects/TS/p1/src/x.ts", "let y = x;"},
			};
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });
			session->DidOpenFile(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/x.ts"),
			    lsproto::LanguageKindTypeScript);

			auto lsP = session->GetLanguageService(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts");
			assert::NilError(t, lsP.second);
			auto* program = lsP.first->GetProgram();
			assert::Check(
			    t,
			    fileNamesContain(
			        program->CommandLine()->ParsedConfig->FileNames,
			        "/home/projects/TS/p1/src/index.ts"));
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/x.ts"))
			            .size()),
			    int64_t(0));

			auto rerr = utils->FS()->Remove(
			    "/home/projects/TS/p1/src/index.ts");
			assert::Assert(t, rerr.impl() == nullptr);

			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeDeleted,
				    "file:///home/projects/TS/p1/src/index.ts");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}

			auto lsP2 = session->GetLanguageService(
			    t->Context(), "file:///home/projects/TS/p1/src/x.ts");
			assert::NilError(t, lsP2.second);
			program = lsP2.first->GetProgram();
			// File name is gone from the command line, was originally
			// included via wildcard
			assert::Check(
			    t,
			    !fileNamesContain(
			        program->CommandLine()->ParsedConfig->FileNames,
			        "/home/projects/TS/p1/src/index.ts"));
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/x.ts"))
			            .size()),
			    int64_t(1));
			delete lsP.first;
			delete lsP2.first;

			// Open file to trigger cleanup
			session->DidOpenFile(t->Context(), "untitled:Untitled-1", 1,
			                     "", lsproto::LanguageKindTypeScript);
			auto* snapshot = session->Snapshot();
			assert::Check(t,
			              snapshot->GetFile(
			                  "/home/projects/TS/p1/src/index.ts") ==
			                  nullptr);
		});

		t->Run("delete directory with wildcard included files",
		       [&](T* t) {
			       t->Parallel();
			       FileMap files{
			           {"/home/projects/TS/p1/tsconfig.json",
			            R"({
					"compilerOptions": {
						"noLib": true
					},
					"include": ["src"]
				})"},
			           {"/home/projects/TS/p1/src/index.ts",
			            "import { x } from \"./sub/x\";"},
			           {"/home/projects/TS/p1/src/sub/x.ts",
			            "export const x = 1;"},
			       };
			       auto p0 = projecttestutil::Setup(files);
			       auto* session = p0.first;
			       auto& utils = p0.second;
			       t->Cleanup([session] { session->Close(); });
			       session->DidOpenFile(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts", 1,
			           fileText(files,
			                    "/home/projects/TS/p1/src/index.ts"),
			           lsproto::LanguageKindTypeScript);

			       auto lsP = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts");
			       assert::NilError(t, lsP.second);
			       auto* program = lsP.first->GetProgram();
			       assert::Check(
			           t,
			           fileNamesContain(
			               program->CommandLine()
			                   ->ParsedConfig->FileNames,
			               "/home/projects/TS/p1/src/sub/x.ts"));
			       assert::Equal(
			           t,
			           int64_t(program
			                       ->GetSemanticDiagnostics(
			                           program->GetSourceFile(
			                               "/home/projects/TS/p1/src/"
			                               "index.ts"))
			                       .size()),
			           int64_t(0));

			       // Delete the entire subdirectory from the file
			       // system.
			       auto rerr = utils->FS()->Remove(
			           "/home/projects/TS/p1/src/sub");
			       assert::Assert(t, rerr.impl() == nullptr);

			       // When a directory is deleted, the client
			       // typically sends a single deletion event for the
			       // directory itself. Because the registered glob
			       // pattern includes file extensions (e.g.
			       // **/*.{ts,...}), the directory path does not
			       // match and the event is filtered out, so the
			       // server is never notified. Simulate this by
			       // sending a delete event for the directory URI.
			       {
				       lsproto::FileEvent fe = fileEvent(
				           lsproto::FileChangeTypeDeleted,
				           "file:///home/projects/TS/p1/src/sub");
				       session->DidChangeWatchedFiles(
				           t->Context(), {&fe});
			       }

			       auto lsP2 = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts");
			       assert::NilError(t, lsP2.second);
			       program = lsP2.first->GetProgram();
			       // The directory was deleted, so the file should
			       // no longer be resolvable.
			       assert::Check(
			           t,
			           program->GetSourceFile(
			               "/home/projects/TS/p1/src/sub/x.ts") ==
			               nullptr);
			       // File name is gone from the command line, was
			       // originally included via wildcard
			       assert::Check(
			           t,
			           !fileNamesContain(
			               program->CommandLine()
			                   ->ParsedConfig->FileNames,
			               "/home/projects/TS/p1/src/sub/x.ts"));
			       // The import should now be an error since the
			       // module is missing.
			       assert::Equal(
			           t,
			           int64_t(program
			                       ->GetSemanticDiagnostics(
			                           program->GetSourceFile(
			                               "/home/projects/TS/p1/src/"
			                               "index.ts"))
			                       .size()),
			           int64_t(1));
			       delete lsP.first;
			       delete lsP2.first;
		       });

		t->Run("delete directory with program-only files", [&](T* t) {
			t->Parallel();
			FileMap files{
			    {"/home/projects/TS/p1/tsconfig.json",
			     R"({
					"compilerOptions": {
						"noLib": true
					},
					"files": ["src/index.ts"]
				})"},
			    {"/home/projects/TS/p1/src/index.ts",
			     "import { x } from \"./sub/x\";"},
			    {"/home/projects/TS/p1/src/sub/x.ts",
			     "export const x = 1;"},
			};
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });
			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			auto lsP = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP.second);
			auto* program = lsP.first->GetProgram();
			assert::Check(
			    t,
			    fileNamesContain(
			        program->CommandLine()->ParsedConfig->FileNames,
			        "/home/projects/TS/p1/src/index.ts"));
			// x.ts is not in "files" but is pulled in via the import.
			assert::Check(
			    t, program->GetSourceFile(
			           "/home/projects/TS/p1/src/sub/x.ts") != nullptr);
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(0));

			// Delete the entire subdirectory from the file system.
			auto rerr =
			    utils->FS()->Remove("/home/projects/TS/p1/src/sub");
			assert::Assert(t, rerr.impl() == nullptr);

			// Send a delete event for the directory URI.
			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeDeleted,
				    "file:///home/projects/TS/p1/src/sub");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}

			auto lsP2 = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP2.second);
			program = lsP2.first->GetProgram();
			// The directory was deleted, so the file should no longer
			// be resolvable.
			assert::Check(
			    t, program->GetSourceFile(
			           "/home/projects/TS/p1/src/sub/x.ts") == nullptr);
			// The import should now be an error since the module is
			// missing.
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(1));
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("delete sibling folder schedules diagnostics refresh",
		       [&](T* t) {
			       t->Parallel();
			       FileMap files{
			           {"/home/projects/TS/p1/tsconfig.json",
			            R"({
					"compilerOptions": {
						"noLib": true
					},
					"files": ["index.ts"]
				})"},
			           {"/home/projects/TS/p1/index.ts",
			            "import { content } from \"./f/content\";\n\nexport "
			            "const value = content;"},
			           {"/home/projects/TS/p1/f/content.ts",
			            "export const content = 1;"},
			       };
			       auto p0 = projecttestutil::Setup(files);
			       auto* session = p0.first;
			       auto& utils = p0.second;
			       t->Cleanup([session] { session->Close(); });
			       lsproto::DocumentUri contentURI(
			           "file:///home/projects/TS/p1/f/content.ts");
			       session->DidOpenFile(
			           t->Context(),
			           "file:///home/projects/TS/p1/index.ts", 1,
			           fileText(files, "/home/projects/TS/p1/index.ts"),
			           lsproto::LanguageKindTypeScript);
			       session->DidOpenFile(
			           t->Context(), contentURI, 1,
			           fileText(files,
			                    "/home/projects/TS/p1/f/content.ts"),
			           lsproto::LanguageKindTypeScript);

			       {
				       auto gP = session->GetLanguageService(
				           t->Context(),
				           "file:///home/projects/TS/p1/index.ts");
				       assert::NilError(t, gP.second);
				       delete gP.first;
			       }
			       session->WaitForBackgroundTasks();

			       int64_t baselineRefreshCount = int64_t(
			           utils->Client()
			               ->RefreshDiagnosticsCalls()
			               .size());

			       auto rerr =
			           utils->FS()->Remove("/home/projects/TS/p1/f");
			       assert::Assert(t, rerr.impl() == nullptr);

			       {
				       lsproto::FileEvent fe = fileEvent(
				           lsproto::FileChangeTypeDeleted,
				           "file:///home/projects/TS/p1/f");
				       session->DidChangeWatchedFiles(
				           t->Context(), {&fe});
			       }
			       session->DidCloseFile(t->Context(), contentURI);
			       session->WaitForBackgroundTasks();

			       int64_t refreshCount = int64_t(
			           utils->Client()
			               ->RefreshDiagnosticsCalls()
			               .size());
			       assert::Assert(
			           t, refreshCount > baselineRefreshCount,
			           gostd::sprintf(
			               "expected RefreshDiagnostics to be called after "
			               "deleting /home/projects/TS/p1/f, got %d calls "
			               "(baseline %d)",
			               {refreshCount, baselineRefreshCount}));
		       });

		t->Run("delete sibling folder schedules diagnostics refresh "
		       "after opening third file",
		       [&](T* t) {
			       t->Parallel();
			       FileMap files{
			           {"/home/projects/TS/p1/tsconfig.json",
			            R"({
					"compilerOptions": {
						"noLib": true
					},
					"files": ["index.ts", "third.ts"]
				})"},
			           {"/home/projects/TS/p1/index.ts",
			            "import { content } from \"./f/content\";\n\nexport "
			            "const value = content;"},
			           {"/home/projects/TS/p1/f/content.ts",
			            "export const content = 1;"},
			           {"/home/projects/TS/p1/third.ts",
			            "export const third = 3;"},
			       };
			       auto p0 = projecttestutil::Setup(files);
			       auto* session = p0.first;
			       auto& utils = p0.second;
			       t->Cleanup([session] { session->Close(); });
			       lsproto::DocumentUri contentURI(
			           "file:///home/projects/TS/p1/f/content.ts");
			       lsproto::DocumentUri thirdURI(
			           "file:///home/projects/TS/p1/third.ts");
			       session->DidOpenFile(
			           t->Context(),
			           "file:///home/projects/TS/p1/index.ts", 1,
			           fileText(files, "/home/projects/TS/p1/index.ts"),
			           lsproto::LanguageKindTypeScript);
			       session->DidOpenFile(
			           t->Context(), contentURI, 1,
			           fileText(files,
			                    "/home/projects/TS/p1/f/content.ts"),
			           lsproto::LanguageKindTypeScript);

			       {
				       auto gP = session->GetLanguageService(
				           t->Context(),
				           "file:///home/projects/TS/p1/index.ts");
				       assert::NilError(t, gP.second);
				       delete gP.first;
			       }
			       session->WaitForBackgroundTasks();

			       int64_t baselineRefreshCount = int64_t(
			           utils->Client()
			               ->RefreshDiagnosticsCalls()
			               .size());

			       auto rerr =
			           utils->FS()->Remove("/home/projects/TS/p1/f");
			       assert::Assert(t, rerr.impl() == nullptr);

			       {
				       lsproto::FileEvent fe = fileEvent(
				           lsproto::FileChangeTypeDeleted,
				           "file:///home/projects/TS/p1/f");
				       session->DidChangeWatchedFiles(
				           t->Context(), {&fe});
			       }
			       session->DidOpenFile(
			           t->Context(), thirdURI, 1,
			           fileText(files, "/home/projects/TS/p1/third.ts"),
			           lsproto::LanguageKindTypeScript);
			       session->WaitForBackgroundTasks();

			       int64_t refreshCount = int64_t(
			           utils->Client()
			               ->RefreshDiagnosticsCalls()
			               .size());
			       assert::Assert(
			           t, refreshCount > baselineRefreshCount,
			           gostd::sprintf(
			               "expected RefreshDiagnostics to be called after "
			               "deleting /home/projects/TS/p1/f and opening "
			               "/home/projects/TS/p1/third.ts, got %d calls "
			               "(baseline %d)",
			               {refreshCount, baselineRefreshCount}));
		       });

		t->Run("create explicitly included file", [&](T* t) {
			t->Parallel();
			FileMap files{
			    {"/home/projects/TS/p1/tsconfig.json",
			     R"({
					"compilerOptions": {
						"noLib": true
					},
					"files": ["src/index.ts", "src/y.ts"]
				})"},
			    {"/home/projects/TS/p1/src/index.ts",
			     "import { y } from \"./y\";"},
			};
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });
			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			auto lsP = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP.second);
			auto* program = lsP.first->GetProgram();

			// Initially should have an error because y.ts is missing
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(1));

			// Add the missing file
			auto werr = utils->FS()->WriteFile(
			    "/home/projects/TS/p1/src/y.ts",
			    "export const y = 1;");
			assert::Assert(t, werr.impl() == nullptr);

			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/TS/p1/src/y.ts");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}

			// Error should be resolved
			auto lsP2 = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP2.second);
			program = lsP2.first->GetProgram();
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(0));
			assert::Check(
			    t, program->GetSourceFile(
			           "/home/projects/TS/p1/src/y.ts") != nullptr);
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("create failed lookup location", [&](T* t) {
			t->Parallel();
			FileMap files{
			    {"/home/projects/TS/p1/tsconfig.json",
			     R"({
					"compilerOptions": {
						"noLib": true
					},
					"files": ["src/index.ts"]
				})"},
			    {"/home/projects/TS/p1/src/index.ts",
			     "import { z } from \"./z\";"},
			};
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });
			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			auto lsP = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP.second);
			auto* program = lsP.first->GetProgram();

			// Initially should have an error because z.ts is missing
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(1));

			// Add a new file through failed lookup watch
			auto werr = utils->FS()->WriteFile(
			    "/home/projects/TS/p1/src/z.ts",
			    "export const z = 1;");
			assert::Assert(t, werr.impl() == nullptr);

			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/TS/p1/src/z.ts");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}

			// Error should be resolved and the new file should be
			// included in the program
			auto lsP2 = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP2.second);
			program = lsP2.first->GetProgram();
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(0));
			assert::Check(
			    t, program->GetSourceFile(
			           "/home/projects/TS/p1/src/z.ts") != nullptr);
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("create wildcard included file", [&](T* t) {
			t->Parallel();
			FileMap files{
			    {"/home/projects/TS/p1/tsconfig.json",
			     R"({
					"compilerOptions": {
						"noLib": true
					},
					"include": ["src"]
				})"},
			    {"/home/projects/TS/p1/src/index.ts", "a;"},
			};
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });
			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			auto lsP = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP.second);
			auto* program = lsP.first->GetProgram();

			// Initially should have an error because declaration for
			// 'a' is missing
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(1));

			// Add a new file through wildcard watch
			auto werr = utils->FS()->WriteFile(
			    "/home/projects/TS/p1/src/a.ts", "const a = 1;");
			assert::Assert(t, werr.impl() == nullptr);

			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/TS/p1/src/a.ts");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}

			// Error should be resolved and the new file should be
			// included in the program
			auto lsP2 = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP2.second);
			program = lsP2.first->GetProgram();
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(0));
			assert::Check(
			    t, program->GetSourceFile(
			           "/home/projects/TS/p1/src/a.ts") != nullptr);
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("irrelevant extension changes are filtered out",
		       [&](T* t) {
			       t->Parallel();
			       FileMap files{
			           {"/home/projects/TS/p1/tsconfig.json",
			            R"({
					"compilerOptions": {
						"noLib": true
					},
					"include": ["src"]
				})"},
			           {"/home/projects/TS/p1/src/index.ts",
			            "export const x = 1;"},
			           {"/home/projects/TS/p1/src/data.txt",
			            "some text"},
			       };
			       auto p0 = projecttestutil::Setup(files);
			       auto* session = p0.first;
			       auto& utils = p0.second;
			       t->Cleanup([session] { session->Close(); });
			       session->DidOpenFile(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts", 1,
			           fileText(files,
			                    "/home/projects/TS/p1/src/index.ts"),
			           lsproto::LanguageKindTypeScript);

			       auto lsP = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts");
			       assert::NilError(t, lsP.second);
			       auto* program = lsP.first->GetProgram();
			       assert::Equal(
			           t,
			           int64_t(program
			                       ->GetSemanticDiagnostics(
			                           program->GetSourceFile(
			                               "/home/projects/TS/p1/src/"
			                               "index.ts"))
			                       .size()),
			           int64_t(0));
			       auto* oldProgram = program;

			       // Modify an irrelevant file and send
			       // change/create events for files with extensions
			       // that are not relevant to TypeScript
			       // compilation.
			       auto werr = utils->FS()->WriteFile(
			           "/home/projects/TS/p1/src/data.txt",
			           "updated text");
			       assert::Assert(t, werr.impl() == nullptr);

			       {
				       lsproto::FileEvent fe1 = fileEvent(
				           lsproto::FileChangeTypeChanged,
				           "file:///home/projects/TS/p1/src/"
				           "data.txt");
				       lsproto::FileEvent fe2 = fileEvent(
				           lsproto::FileChangeTypeCreated,
				           "file:///home/projects/TS/p1/src/"
				           "styles.css");
				       lsproto::FileEvent fe3 = fileEvent(
				           lsproto::FileChangeTypeCreated,
				           "file:///home/projects/TS/p1/src/"
				           "image.png");
				       session->DidChangeWatchedFiles(
				           t->Context(), {&fe1, &fe2, &fe3});
			       }

			       // The program should not have been rebuilt
			       // since all events had irrelevant extensions.
			       auto lsP2 = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts");
			       assert::NilError(t, lsP2.second);
			       program = lsP2.first->GetProgram();
			       assert::Equal(
			           t, program, oldProgram,
			           "program should not be rebuilt for irrelevant "
			           "extension changes");
			       delete lsP.first;
			       delete lsP2.first;
		       });

		t->Run("pnpm install links local package", [&](T* t) {
			t->Parallel();
			FileMap files{
			    {"/home/projects/pnpm/pnpm-workspace.yaml",
			     "packages:\n  - 'packages/*'"},
			    {"/home/projects/pnpm/packages/alpha/package.json",
			     "{ \"name\": \"@repo/alpha\", \"main\": "
			     "\"index.ts\" }"},
			    {"/home/projects/pnpm/packages/alpha/tsconfig.json",
			     R"({
					"compilerOptions": { "noLib": true, "composite": true }
				})"},
			    {"/home/projects/pnpm/packages/alpha/index.ts",
			     "export const alpha = 1;"},
			    {"/home/projects/pnpm/packages/beta/package.json",
			     "{ \"name\": \"@repo/beta\" }"},
			    {"/home/projects/pnpm/packages/beta/tsconfig.json",
			     R"({
					"compilerOptions": { "noLib": true }
				})"},
			    {"/home/projects/pnpm/packages/beta/index.ts",
			     "import { alpha } from \"@repo/alpha\";"},
			};
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });
			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/pnpm/packages/beta/index.ts",
			    1,
			    fileText(files,
			             "/home/projects/pnpm/packages/beta/index.ts"),
			    lsproto::LanguageKindTypeScript);

			// Before pnpm install: the import is unresolved because
			// node_modules/@repo/alpha doesn't exist.
			auto lsP = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/pnpm/packages/beta/index.ts");
			assert::NilError(t, lsP.second);
			auto* program = lsP.first->GetProgram();
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/pnpm/packages/beta/"
			                    "index.ts"))
			            .size()),
			    int64_t(1));

			// Simulate pnpm install: create a symlink from beta's
			// node_modules/@repo/alpha to packages/alpha.
			auto* mapFS = dynamic_cast<vfstest::MapFS*>(
			    utils->FsFromFileMap()->FSys().get());
			auto merr = mapFS->MkdirAll(
			    "home/projects/pnpm/packages/beta/node_modules/@repo",
			    tsc::vfs::ModePerm);
			assert::Assert(t, merr.impl() == nullptr);
			mapFS->AddSymlink(
			    "home/projects/pnpm/packages/beta/node_modules/"
			    "@repo/alpha",
			    "home/projects/pnpm/packages/alpha");

			// Fire watch events mimicking what VS Code sends for a pnpm
			// install.
			{
				lsproto::FileEvent fes[6];
				fes[0] = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/pnpm/packages/"
				    "beta/node_modules");
				fes[1] = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/pnpm/packages/"
				    "beta/node_modules/%40repo");
				fes[2] = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/pnpm/packages/"
				    "beta/node_modules/%40repo/alpha");
				fes[3] = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/pnpm/"
				    "pnpm-lock.yaml");
				fes[4] = fileEvent(
				    lsproto::FileChangeTypeChanged,
				    "file:///home/projects/pnpm/packages/"
				    "beta/node_modules/.bin/tsc");
				fes[5] = fileEvent(
				    lsproto::FileChangeTypeChanged,
				    "file:///home/projects/pnpm/packages/"
				    "beta/node_modules/.bin/tsserver");
				session->DidChangeWatchedFiles(
				    t->Context(),
				    {&fes[0], &fes[1], &fes[2], &fes[3],
				     &fes[4], &fes[5]});
			}

			// After pnpm install: the import should resolve.
			auto lsP2 = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/pnpm/packages/beta/index.ts");
			assert::NilError(t, lsP2.second);
			program = lsP2.first->GetProgram();
			auto diags = program->GetSemanticDiagnostics(
			    program->GetSourceFile(
			        "/home/projects/pnpm/packages/beta/index.ts"));
			for (auto* d : diags) {
				t->Logf("diagnostic: %s", {std::string(d->MessageText())});
			}
			assert::Equal(t, int64_t(diags.size()), int64_t(0));
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("symlinked node_modules package.json change invalidates "
		       "resolution",
		       [&](T* t) {
			       t->Parallel();
			       // Set up a project that imports a symlinked
			       // node_modules package. The package resolves
			       // via "main" in package.json to
			       // dist/index.js.
			       FileMap files{
			           {"/home/projects/myproject/tsconfig.json",
			            R"({
					"compilerOptions": {
						"noLib": true,
						"module": "nodenext",
						"moduleResolution": "nodenext"
					},
					"files": ["src/index.ts"]
				})"},
			           {"/home/projects/myproject/src/index.ts",
			            "import { foo } from \"mylib\";"},
			           // The real package lives as a sibling directory
			           {"/home/projects/mylib/package.json",
			            R"({
					"name": "mylib",
					"main": "dist/index.js"
				})"},
			           {"/home/projects/mylib/dist/index.js",
			            "exports.foo = function() { return 1; };"},
			           {"/home/projects/mylib/dist/index.d.ts",
			            "export declare function foo(): number;"},
			           // node_modules/mylib is a symlink to the sibling
			           {"/home/projects/myproject/node_modules/mylib",
			            vfstest::Symlink("/home/projects/mylib")},
			       };

			       project::SessionOptions options;
			       options.CurrentDirectory =
			           "/home/projects/myproject";
			       options.DefaultLibraryPath =
			           tsc::bundled::LibPath();
			       options.TypingsLocation =
			           std::string(
			               projecttestutil::TestTypingsLocation);
			       options.PositionEncoding =
			           lsproto::PositionEncodingKindUTF8;
			       options.WatchEnabled = true;
			       options.LoggingEnabled = true;
			       auto p0 = projecttestutil::SetupWithOptions(
			           files, &options);
			       auto* session = p0.first;
			       auto& utils = p0.second;
			       t->Cleanup([session] { session->Close(); });
			       session->DidOpenFile(
			           t->Context(),
			           "file:///home/projects/myproject/src/index.ts", 1,
			           fileText(files,
			                    "/home/projects/myproject/src/index.ts"),
			           lsproto::LanguageKindTypeScript);

			       // Initial state: import resolves successfully
			       // via package.json main -> dist/index.d.ts
			       auto lsP = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/myproject/src/index.ts");
			       assert::NilError(t, lsP.second);
			       auto* program = lsP.first->GetProgram();
			       session->WaitForBackgroundTasks();
			       auto diags = program->GetSemanticDiagnostics(
			           program->GetSourceFile(
			               "/home/projects/myproject/src/index.ts"));
			       for (auto* d : diags) {
				       t->Logf("initial diagnostic: %s",
				               {std::string(d->MessageText())});
			       }
			       assert::Equal(t, int64_t(diags.size()),
			                     int64_t(0),
			                     "import should resolve initially");

			       // Assert: watched file globs cover the realpath
			       // of package.json and dist/index.d.ts. With a
			       // workspace dir set, watchers use
			       // RelativePattern with a base URI.
			       assert::Check(
			           t,
			           utils->WatchesFile(
			               "/home/projects/mylib/package.json"),
			           "realpath of package.json should be watched");
			       assert::Check(
			           t,
			           utils->WatchesFile(
			               "/home/projects/mylib/dist/index.d.ts"),
			           "realpath of dist/index.d.ts should be "
			           "watched");

			       // Edit package.json to remove "main" field
			       auto werr = utils->FS()->WriteFile(
			           "/home/projects/mylib/package.json",
			           "{\n				\"name\": \"mylib\"\n			}");
			       assert::Assert(t, werr.impl() == nullptr);

			       // Fire watch event for the realpath of the
			       // changed package.json. A real editor would
			       // fire this for the realpath since it watches
			       // realpaths.
			       {
				       lsproto::FileEvent fe = fileEvent(
				           lsproto::FileChangeTypeChanged,
				           "file:///home/projects/mylib/"
				           "package.json");
				       session->DidChangeWatchedFiles(
				           t->Context(), {&fe});
			       }

			       // After removing "main" from package.json, the
			       // import should no longer resolve.
			       auto lsP2 = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/myproject/src/index.ts");
			       assert::NilError(t, lsP2.second);
			       program = lsP2.first->GetProgram();
			       diags = program->GetSemanticDiagnostics(
			           program->GetSourceFile(
			               "/home/projects/myproject/src/index.ts"));
			       assert::Assert(
			           t, int64_t(diags.size()) > 0,
			           "import should fail after removing main from "
			           "package.json");
			       delete lsP.first;
			       delete lsP2.first;
		       });

		t->Run("create file in non-existent directory", [&](T* t) {
			t->Parallel();
			FileMap files{
			    {"/home/projects/TS/p1/tsconfig.json",
			     R"({
					"compilerOptions": {
						"noLib": true
					},
					"files": ["src/index.ts"]
				})"},
			    {"/home/projects/TS/p1/src/index.ts",
			     "import { helper } from \"./lib/helper\";"},
			};
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });
			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);

			// Initially should have an error because lib/helper.ts
			// doesn't exist and src/lib/ directory doesn't exist
			// either.
			auto lsP = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP.second);
			auto* program = lsP.first->GetProgram();
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(1));

			// Create the directory and file.
			auto werr = utils->FS()->WriteFile(
			    "/home/projects/TS/p1/src/lib/helper.ts",
			    "export const helper = 1;");
			assert::Assert(t, werr.impl() == nullptr);

			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/TS/p1/src/lib/"
				    "helper.ts");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}

			// Error should be resolved.
			auto lsP2 = session->GetLanguageService(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts");
			assert::NilError(t, lsP2.second);
			program = lsP2.first->GetProgram();
			assert::Equal(
			    t,
			    int64_t(
			        program
			            ->GetSemanticDiagnostics(
			                program->GetSourceFile(
			                    "/home/projects/TS/p1/src/index.ts"))
			            .size()),
			    int64_t(0));
			assert::Check(
			    t, program->GetSourceFile(
			           "/home/projects/TS/p1/src/lib/helper.ts") !=
			           nullptr);
			delete lsP.first;
			delete lsP2.first;
		});

		t->Run("create symlink directory matching include pattern",
		       [&](T* t) {
			       t->Parallel();
			       FileMap files{
			           {"/home/projects/TS/p1/tsconfig.json",
			            R"({
					"compilerOptions": {
						"noLib": true
					},
					"include": ["src"]
				})"},
			           {"/home/projects/TS/p1/src/index.ts",
			            "export const x = 1;"},
			           {"/home/projects/TS/shared/utils.ts",
			            "export const util = \"hello\";"},
			           {"/home/projects/TS/shared/helpers.ts",
			            "export const helper = 42;"},
			       };
			       auto p0 = projecttestutil::Setup(files);
			       auto* session = p0.first;
			       auto& utils = p0.second;
			       t->Cleanup([session] { session->Close(); });
			       session->DidOpenFile(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts", 1,
			           fileText(files,
			                    "/home/projects/TS/p1/src/index.ts"),
			           lsproto::LanguageKindTypeScript);

			       auto lsP = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts");
			       assert::NilError(t, lsP.second);
			       auto* program = lsP.first->GetProgram();

			       // Initially, project only has the one file in
			       // src/.
			       assert::Check(
			           t,
			           fileNamesContain(
			               program->CommandLine()
			                   ->ParsedConfig->FileNames,
			               "/home/projects/TS/p1/src/index.ts"));
			       assert::Check(
			           t,
			           !fileNamesContain(
			               program->CommandLine()
			                   ->ParsedConfig->FileNames,
			               "/home/projects/TS/p1/src/linked/utils.ts"));
			       assert::Check(
			           t,
			           !fileNamesContain(
			               program->CommandLine()
			                   ->ParsedConfig->FileNames,
			               "/home/projects/TS/p1/src/linked/"
			               "helpers.ts"));

			       // Create a symlink directory inside src/ that
			       // points to the shared directory.
			       auto* mapFS = dynamic_cast<vfstest::MapFS*>(
			           utils->FsFromFileMap()->FSys().get());
			       mapFS->AddSymlink(
			           "home/projects/TS/p1/src/linked",
			           "home/projects/TS/shared");

			       // Send directory creation event (what VS Code
			       // sends when a symlink directory appears).
			       {
				       lsproto::FileEvent fe = fileEvent(
				           lsproto::FileChangeTypeCreated,
				           "file:///home/projects/TS/p1/src/"
				           "linked");
				       session->DidChangeWatchedFiles(
				           t->Context(), {&fe});
			       }

			       // After the symlink directory is created, the
			       // files inside it should be picked up by the
			       // wildcard include pattern.
			       auto lsP2 = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts");
			       assert::NilError(t, lsP2.second);
			       program = lsP2.first->GetProgram();
			       assert::Check(
			           t,
			           fileNamesContain(
			               program->CommandLine()
			                   ->ParsedConfig->FileNames,
			               "/home/projects/TS/p1/src/index.ts"));
			       assert::Check(
			           t,
			           fileNamesContain(
			               program->CommandLine()
			                   ->ParsedConfig->FileNames,
			               "/home/projects/TS/p1/src/linked/utils.ts"));
			       assert::Check(
			           t,
			           fileNamesContain(
			               program->CommandLine()
			                   ->ParsedConfig->FileNames,
			               "/home/projects/TS/p1/src/linked/"
			               "helpers.ts"));
			       delete lsP.first;
			       delete lsP2.first;
		       });

		t->Run("skips irrelevant extensions", [&](T* t) {
			t->Parallel();
			FileMap files{
			    {"/home/projects/TS/p1/tsconfig.json",
			     R"({
					"compilerOptions": {},
					"include": ["src"]
				})"},
			    {"/home/projects/TS/p1/src/index.ts",
			     "export const x = 1;"},
			};
			auto p0 = projecttestutil::Setup(files);
			auto* session = p0.first;
			auto& utils = p0.second;
			t->Cleanup([session] { session->Close(); });

			session->DidOpenFile(
			    t->Context(),
			    "file:///home/projects/TS/p1/src/index.ts", 1,
			    fileText(files, "/home/projects/TS/p1/src/index.ts"),
			    lsproto::LanguageKindTypeScript);
			session->WaitForBackgroundTasks();

			int64_t baselineRefreshCount = int64_t(
			    utils->Client()->RefreshDiagnosticsCalls().size());

			// Scenario A: irrelevant .svg
			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/TS/p1/icon.svg");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}
			session->WaitForBackgroundTasks();
			int64_t refreshCount = int64_t(
			    utils->Client()->RefreshDiagnosticsCalls().size());
			assert::Equal(t, refreshCount, baselineRefreshCount,
			              "irrelevant .svg should not trigger refresh");

			// Scenario B: relevant .ts
			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/TS/p1/src/new.ts");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}
			session->WaitForBackgroundTasks();
			refreshCount = int64_t(
			    utils->Client()->RefreshDiagnosticsCalls().size());
			assert::Assert(t, refreshCount > baselineRefreshCount,
			               "relevant .ts should trigger refresh");
			baselineRefreshCount = refreshCount;

			// Scenario C: tsconfig.json
			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeChanged,
				    "file:///home/projects/TS/p1/tsconfig.json");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}
			session->WaitForBackgroundTasks();
			refreshCount = int64_t(
			    utils->Client()->RefreshDiagnosticsCalls().size());
			assert::Assert(t, refreshCount > baselineRefreshCount,
			               "tsconfig.json should trigger refresh");
			baselineRefreshCount = refreshCount;

			// Scenario D: directory creation (no extension)
			auto* mapFS = dynamic_cast<vfstest::MapFS*>(
			    utils->FsFromFileMap()->FSys().get());
			auto merr = mapFS->MkdirAll(
			    "home/projects/TS/p1/node_modules/@types",
			    tsc::vfs::ModePerm);
			assert::Assert(t, merr.impl() == nullptr);
			{
				lsproto::FileEvent fe = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/TS/p1/"
				    "node_modules/@types");
				session->DidChangeWatchedFiles(t->Context(), {&fe});
			}
			session->WaitForBackgroundTasks();
			refreshCount = int64_t(
			    utils->Client()->RefreshDiagnosticsCalls().size());
			assert::Assert(
			    t, refreshCount > baselineRefreshCount,
			    "directory change should trigger refresh");
			baselineRefreshCount = refreshCount;

			// Scenario E: mixed batch
			{
				lsproto::FileEvent fe1 = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/TS/p1/icon.png");
				lsproto::FileEvent fe2 = fileEvent(
				    lsproto::FileChangeTypeChanged,
				    "file:///home/projects/TS/p1/src/index.ts");
				session->DidChangeWatchedFiles(t->Context(),
				                               {&fe1, &fe2});
			}
			session->WaitForBackgroundTasks();
			refreshCount = int64_t(
			    utils->Client()->RefreshDiagnosticsCalls().size());
			assert::Assert(
			    t, refreshCount > baselineRefreshCount,
			    "mixed batch with relevant file should trigger "
			    "refresh");
			baselineRefreshCount = refreshCount;

			// Scenario F: package install noise
			{
				lsproto::FileEvent fes[4];
				fes[0] = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/TS/p1/"
				    "node_modules/pkg/LICENSE");
				fes[1] = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/TS/p1/README.md");
				fes[2] = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/TS/p1/"
				    "LICENSE.txt");
				fes[3] = fileEvent(
				    lsproto::FileChangeTypeCreated,
				    "file:///home/projects/TS/p1/style.css");
				session->DidChangeWatchedFiles(
				    t->Context(),
				    {&fes[0], &fes[1], &fes[2], &fes[3]});
			}
			session->WaitForBackgroundTasks();
			refreshCount = int64_t(
			    utils->Client()->RefreshDiagnosticsCalls().size());
			assert::Equal(
			    t, refreshCount, baselineRefreshCount,
			    "package install noise should not trigger "
			    "refresh");
		});
	});

	t->Run("refreshes code lenses and inlay hints when relevant user "
	       "preferences change",
	       [&](T* t) {
		       t->Parallel();
		       FileMap files{
		           {"/src/tsconfig.json", "{}"},
		           {"/src/index.ts", "export const x = 1;"},
		       };
		       auto p0 = projecttestutil::Setup(files);
		       auto* session = p0.first;
		       auto& utils = p0.second;
		       t->Cleanup([session] { session->Close(); });
		       session->DidOpenFile(
		           t->Context(), "file:///src/index.ts", 1,
		           fileText(files, "/src/index.ts"),
		           lsproto::LanguageKindTypeScript);
		       {
			       auto gP = session->GetLanguageService(
			           t->Context(),
			           lsproto::DocumentUri("file:///src/index.ts"));
			       assert::NilError(t, gP.second);
			       delete gP.first;
		       }

		       session->Configure(lsutil::NewDefaultUserPreferences());
		       // Change user preferences for code lens and inlay hints.
		       auto newPrefs = session->Config();
		       newPrefs.CodeLensUserPreferences.ReferencesCodeLensEnabled =
		           tsc::Tristate::True;
		       newPrefs.InlayHintsPreferences
		           .IncludeInlayFunctionLikeReturnTypeHints =
		           tsc::Tristate::True;

		       session->Configure(newPrefs);

		       auto codeLensRefreshCalls =
		           utils->Client()->RefreshCodeLensCalls();
		       auto inlayHintsRefreshCalls =
		           utils->Client()->RefreshInlayHintsCalls();
		       assert::Equal(
		           t, int64_t(codeLensRefreshCalls.size()),
		           int64_t(1),
		           "expected one RefreshCodeLens call after code lens "
		           "preference change");
		       assert::Equal(
		           t, int64_t(inlayHintsRefreshCalls.size()),
		           int64_t(1),
		           "expected one RefreshInlayHints call after inlay "
		           "hints preference change");
	       });

	t->Run("sets locale when configured", [&](T* t) {
		t->Parallel();
		auto p0 = projecttestutil::Setup(FileMap{});
		auto* session = p0.first;
		auto& utils = p0.second;
		t->Cleanup([session] { session->Close(); });
		auto prefs = lsutil::NewDefaultUserPreferences();
		prefs.Locale = "fr";

		session->Configure(prefs);

		auto setLocaleCalls = utils->Client()->SetLocaleCalls();
		assert::Equal(t, int64_t(setLocaleCalls.size()), int64_t(1));
		assert::Equal(t, setLocaleCalls[0].LocaleMoqParam,
		              std::string("fr"));
	});

	t->Run("locale change invalidates programs", [&](T* t) {
		t->Parallel();
		FileMap files{
		    {"/src/tsconfig.json", "{}"},
		    {"/src/index.ts", "export const x = 1;"},
		};
		auto p0 = projecttestutil::Setup(files);
		auto* session = p0.first;
		t->Cleanup([session] { session->Close(); });
		auto ctx = t->Context();
		lsproto::DocumentUri uri("file:///src/index.ts");
		tsc::tspath::Path configPath("/src/tsconfig.json");
		session->DidOpenFile(t->Context(), uri, 1,
		                     fileText(files, "/src/index.ts"),
		                     lsproto::LanguageKindTypeScript);
		{
			auto gP = session->GetLanguageService(ctx, uri);
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		auto* initialProgram = session->Snapshot()
		                           ->ProjectCollection
		                           ->ConfiguredProject(configPath)
		                           ->Program;

		auto preferences = session->Config();
		preferences.CodeLensUserPreferences.ReferencesCodeLensEnabled =
		    tsc::Tristate::True;
		session->Configure(preferences);
		{
			auto gP = session->GetLanguageService(ctx, uri);
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		auto* programAfterCodeLensChange = session->Snapshot()
		                                       ->ProjectCollection
		                                       ->ConfiguredProject(
		                                           configPath)
		                                       ->Program;
		assert::Equal(t, programAfterCodeLensChange, initialProgram);

		preferences.Locale = "fr";
		session->Configure(preferences);
		{
			auto gP = session->GetLanguageService(ctx, uri);
			assert::NilError(t, gP.second);
			delete gP.first;
		}
		auto* programAfterLocaleChange = session->Snapshot()
		                                     ->ProjectCollection
		                                     ->ConfiguredProject(
		                                         configPath)
		                                     ->Program;
		assert::Assert(t, programAfterLocaleChange != initialProgram);
	});

	t->Run("adds locale to background contexts", [&](T* t) {
		t->Parallel();
		auto p0 = projecttestutil::Setup(FileMap{});
		auto* session = p0.first;
		auto& utils = p0.second;
		t->Cleanup([session] { session->Close(); });
		auto frPr = locale::parse("fr");
		auto fr = std::get<0>(frPr);
		assert::Assert(t, std::get<1>(frPr));
		utils->Client()->GetLocaleFunc =
		    [fr]() -> locale::Locale { return fr; };
		utils->Client()->RefreshCodeLensFunc =
		    [t, fr](const gostd::Context& ctx) -> gostd::Error {
			assert::Equal(t, locale::fromContext(ctx), fr);
			return gostd::Error(nullptr);
		};
		auto prefs = lsutil::NewDefaultUserPreferences();
		prefs.CodeLensUserPreferences.ReferencesCodeLensEnabled =
		    tsc::Tristate::True;

		session->Configure(prefs);

		assert::Equal(
		    t,
		    int64_t(utils->Client()->RefreshCodeLensCalls().size()),
		    int64_t(1));
	});

	t->Run("schedules diagnostics refresh when "
	       "reportStyleChecksAsWarnings changes",
	       [&](T* t) {
		       t->Parallel();
		       FileMap files{
		           {"/src/tsconfig.json", "{}"},
		           {"/src/index.ts", "export const x = 1;"},
		       };
		       auto p0 = projecttestutil::Setup(files);
		       auto* session = p0.first;
		       auto& utils = p0.second;
		       t->Cleanup([session] { session->Close(); });
		       session->DidOpenFile(
		           t->Context(), "file:///src/index.ts", 1,
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

		       // Record the baseline count of RefreshDiagnostics
		       // calls.
		       int64_t baselineRefreshCount = int64_t(
		           utils->Client()->RefreshDiagnosticsCalls().size());

		       // Toggle reportStyleChecksAsWarnings (default is true,
		       // so set it to false).
		       auto prefs = lsutil::NewDefaultUserPreferences();
		       prefs.ReportStyleChecksAsWarnings =
		           tsc::Tristate::False;
		       session->Configure(prefs);
		       session->WaitForBackgroundTasks();

		       int64_t refreshCount = int64_t(
		           utils->Client()->RefreshDiagnosticsCalls().size());
		       assert::Assert(
		           t, refreshCount > baselineRefreshCount,
		           gostd::sprintf(
		               "expected RefreshDiagnostics to be called after "
		               "reportStyleChecksAsWarnings change, got %d "
		               "calls (baseline %d)",
		               {refreshCount, baselineRefreshCount}));
	       });

	t->Run("config parsing", [&](T* t) {
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

		lsutil::JsonObject configMap1{
		    {"preferences",
		     lsutil::JsonObject{
		         {"useAliasesForRenames", true},
		         {"quoteStyle", "single"}}},
		    {"unstable",
		     lsutil::JsonObject{
		         {"organizeImportsSort", "ordinalIgnoreCase"}}},
		};
		session->Configure(lsutil::ParseUserPreferences(
		    lsutil::JsonObject{{"js/ts", configMap1}}));
		auto actualConfig1 = session->Config();
		auto expectedPrefs1 = lsutil::NewDefaultUserPreferences();
		expectedPrefs1.ProvidePrefixAndSuffixTextForRename = tsc::Tristate::True;
		expectedPrefs1.QuotePreference = lsutil::QuotePreferenceSingle;
		expectedPrefs1.OrganizeImportsSort =
		    lsutil::OrganizeImportsSortOrdinalIgnoreCase;

		assert::DeepEqual(
		    t,
		    tsc::json::marshal(actualConfig1,
		                       {tsc::json::deterministic(true)})
		        .first,
		    tsc::json::marshal(expectedPrefs1,
		                       {tsc::json::deterministic(true)})
		        .first);

		lsutil::JsonObject configMap2{
		    {"preferences",
		     lsutil::JsonObject{
		         {"useAliasesForRenames", false},
		         {"quoteStyle", "double"}}},
		    {"unstable",
		     lsutil::JsonObject{{"organizeImportsSort", "ordinal"}}},
		};
		session->Configure(lsutil::ParseUserPreferences(
		    lsutil::JsonObject{{"js/ts", configMap2}}));
		auto actualConfig2 = session->Config();
		auto expectedPrefs2 = lsutil::NewDefaultUserPreferences();
		expectedPrefs2.ProvidePrefixAndSuffixTextForRename = tsc::Tristate::False;
		expectedPrefs2.QuotePreference = lsutil::QuotePreferenceDouble;
		expectedPrefs2.OrganizeImportsSort =
		    lsutil::OrganizeImportsSortOrdinal;

		assert::DeepEqual(
		    t,
		    tsc::json::marshal(actualConfig2,
		                       {tsc::json::deterministic(true)})
		        .first,
		    tsc::json::marshal(expectedPrefs2,
		                       {tsc::json::deterministic(true)})
		        .first);
	});

	t->Run("language service for closed files", [&](T* t) {
		t->Parallel();

		t->Run("closed file in configured project not yet opened",
		       [&](T* t) {
			       t->Parallel();
			       // Set up a project where a tsconfig exists but
			       // no files have been opened. Requesting
			       // language service for a file covered by that
			       // tsconfig should work even though the file
			       // was never opened via didOpen.
			       FileMap files{
			           {"/home/projects/TS/p1/tsconfig.json",
			            R"({
					"compilerOptions": {
						"noLib": true,
						"strict": true
					},
					"include": ["src"]
				})"},
			           {"/home/projects/TS/p1/src/index.ts",
			            "export const x: number = 1;"},
			       };
			       auto p0 = projecttestutil::Setup(files);
			       auto* session = p0.first;
			       t->Cleanup([session] { session->Close(); });

			       // Do NOT open any file. Directly request
			       // language service for a closed file that
			       // belongs to the configured project.
			       auto lsP = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/p1/src/index.ts");
			       assert::NilError(t, lsP.second);
			       assert::Assert(t, lsP.first != nullptr);
			       auto* program = lsP.first->GetProgram();
			       assert::Assert(t, program != nullptr);
			       auto* sourceFile = program->GetSourceFile(
			           "/home/projects/TS/p1/src/index.ts");
			       assert::Assert(t, sourceFile != nullptr);
			       assert::Equal(
			           t, sourceFile->Text(),
			           std::string("export const x: number = 1;"));
			       delete lsP.first;
		       });

		t->Run("closed file with no configured project creates inferred "
		       "project",
		       [&](T* t) {
			       t->Parallel();
			       // Set up a file that has no tsconfig.
			       // Requesting language service for it should
			       // create an inferred project even though the
			       // file was never opened.
			       FileMap files{
			           {"/home/projects/TS/loose/index.ts",
			            "const greeting: string = \"hello\";"},
			       };
			       auto p0 = projecttestutil::Setup(files);
			       auto* session = p0.first;
			       t->Cleanup([session] { session->Close(); });

			       // Do NOT open any file. Directly request
			       // language service for a closed file that
			       // has no configured project.
			       auto lsP = session->GetLanguageService(
			           t->Context(),
			           "file:///home/projects/TS/loose/index.ts");
			       assert::NilError(t, lsP.second);
			       assert::Assert(t, lsP.first != nullptr);
			       auto* program = lsP.first->GetProgram();
			       assert::Assert(t, program != nullptr);
			       auto* sourceFile = program->GetSourceFile(
			           "/home/projects/TS/loose/index.ts");
			       assert::Assert(t, sourceFile != nullptr);
			       assert::Equal(
			           t, sourceFile->Text(),
			           std::string(
			               "const greeting: string = \"hello\";"));
			       delete lsP.first;
		       });
	});

	t->Run("jsconfig.json used for JS files when tsconfig.json exists "
	       "in same directory",
	       [&](T* t) {
		       t->Parallel();
		       FileMap files{
		           {"/home/projects/TS/p1/tsconfig.json",
		            R"({
				"compilerOptions": {
					"noLib": true,
					"strict": true
				}
			})"},
		           {"/home/projects/TS/p1/jsconfig.json",
		            R"({
				"compilerOptions": {
					"noLib": true,
					"checkJs": true
				}
			})"},
		           {"/home/projects/TS/p1/index.ts",
		            "export const x: number = 1;"},
		           {"/home/projects/TS/p1/app.js",
		            "/** @type {number} */ var y = \"not a number\";"},
		       };
		       auto p0 = projecttestutil::Setup(files);
		       auto* session = p0.first;
		       t->Cleanup([session] { session->Close(); });

		       // Open the JS file - it should be assigned to the
		       // jsconfig.json project, not tsconfig.json
		       session->DidOpenFile(
		           t->Context(), "file:///home/projects/TS/p1/app.js", 1,
		           fileText(files, "/home/projects/TS/p1/app.js"),
		           lsproto::LanguageKindJavaScript);

		       auto* snapshot = session->Snapshot();
		       lsproto::DocumentUri jsURI(
		           "file:///home/projects/TS/p1/app.js");
		       auto* defaultProject =
		           snapshot->GetDefaultProject(jsURI);
		       assert::Assert(t, defaultProject != nullptr,
		                      "JS file should have a default project");
		       assert::Equal(
		           t, defaultProject->ConfigFileName(),
		           std::string("/home/projects/TS/p1/jsconfig.json"),
		           "JS file should belong to jsconfig.json project, "
		           "not tsconfig.json");

		       // Open the TS file - it should be assigned to
		       // tsconfig.json project
		       session->DidOpenFile(
		           t->Context(), "file:///home/projects/TS/p1/index.ts",
		           1, fileText(files, "/home/projects/TS/p1/index.ts"),
		           lsproto::LanguageKindTypeScript);

		       snapshot = session->Snapshot();
		       lsproto::DocumentUri tsURI(
		           "file:///home/projects/TS/p1/index.ts");
		       auto* defaultTSProject =
		           snapshot->GetDefaultProject(tsURI);
		       assert::Assert(t, defaultTSProject != nullptr,
		                      "TS file should have a default project");
		       assert::Equal(
		           t, defaultTSProject->ConfigFileName(),
		           std::string("/home/projects/TS/p1/tsconfig.json"),
		           "TS file should belong to tsconfig.json project");
	       });
}

REGISTER_UNIT_TEST("project.TestSession", TestSession);

}  // namespace
