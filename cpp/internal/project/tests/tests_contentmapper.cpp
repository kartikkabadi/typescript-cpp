// Port of tsc/internal/project/contentmapper_test.go.
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/bundled/bundled.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/locale/locale.h"
#include "internal/ls/ls.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/project/session.h"
#include "internal/project/snapshot.h"
#include "internal/testutil/contentmappertest/contentmappertest.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace contentmapper = tsc::contentmapper;
namespace contentmappertest = tsc::testutil::contentmappertest;
namespace gostd = tsc::gostd;
namespace locale = tsc::locale;
namespace ls = tsc::ls;
namespace lsproto = tsc::lsp::lsproto;
namespace project = tsc::project;
namespace projecttestutil = tsc::testutil::projecttestutil;
namespace tspath = tsc::tspath;
using tsc::gostd::testing::T;

const std::string& cmFileText(const projecttestutil::FileMap& files,
                              const std::string& name) {
	return std::get<std::string>(files.at(name));
}

lsproto::FileEvent* cmFileEvent(const lsproto::DocumentUri& uri,
                                lsproto::FileChangeType type) {
	auto* ev = new lsproto::FileEvent();
	ev->Uri = uri;
	ev->Type = type;
	return ev;
}

lsproto::TextDocumentContentChangePartialOrWholeDocument
wholeDocChange(const std::string& text) {
	lsproto::TextDocumentContentChangePartialOrWholeDocument change;
	change.WholeDocument =
	    std::make_shared<
	        lsproto::TextDocumentContentChangeWholeDocument>();
	change.WholeDocument->Text = text;
	return change;
}

// recordingContentMapperProcess — contentmapper_test.go:37.
struct recordingContentMapperProcess : gostd::io::ReadWriteCloser {
	std::shared_ptr<gostd::io::ReadWriteCloser> inner;
	std::atomic<int32_t>* closes;
	std::once_flag once;

	std::pair<int, gostd::Error> read(std::span<char> buf) override {
		return inner->read(buf);
	}
	std::pair<int, gostd::Error> write(std::string_view data) override {
		return inner->write(data);
	}
	gostd::Error close() override {
		std::call_once(once, [this] { closes->fetch_add(1); });
		return inner->close();
	}
};

// recordingContentMapperSpawner — contentmapper_test.go:27.
struct recordingContentMapperSpawner : contentmapper::Spawner {
	std::shared_ptr<contentmapper::Spawner> inner;
	std::atomic<int32_t> spawns{0};
	std::atomic<int32_t> closes{0};

	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Spawn(const std::vector<std::string>& command, const std::string& dir,
	      gostd::io::Writer* stderr_) override {
		auto [process, err] = inner->Spawn(command, dir, stderr_);
		if (err != nullptr) {
			return {nullptr, err};
		}
		spawns.fetch_add(1);
		auto* proc = new recordingContentMapperProcess();
		proc->inner = process;
		proc->closes = &closes;
		return {std::shared_ptr<gostd::io::ReadWriteCloser>(proc),
		        nullptr};
	}
};

std::shared_ptr<projecttestutil::SessionUtils> nullUtils() {
	return nullptr;
}

projecttestutil::FileMap cmInProjectFiles() {
	return {
	    {"/home/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler", "strict": true },
			"contentMappers": [ { "package": "mapper", "extensions": [".box"] } ]
		})")},
	    {"/home/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::TransformingMapper)},
	    {"/home/project/app.box",
	     std::string("export const version = #{target};\n")},
	    {"/home/project/main.ts",
	     std::string(
	         "import { version } from \"./app.box\";\nexport const twice: "
	         "number = version * 2;\n")},
	};
}

void TestContentMapperProjectWithoutMappedFiles(T* t) {
	t->Parallel();
	for (bool hasMapper : {false, true}) {
		t->Run(gostd::sprintf("hasMapper=%t", {hasMapper}), [hasMapper](T* t) {
			t->Parallel();
			std::string config = R"({"compilerOptions": {"noLib": true}})";
			if (hasMapper) {
				config = R"({
					"compilerOptions": { "noLib": true },
					"contentMappers": [ { "package": "mapper", "extensions": [".box"] } ]
				})";
			}
			projecttestutil::FileMap files{
			    {"/home/project/tsconfig.json", config},
			    {"/home/project/node_modules/mapper/package.json",
			     contentmappertest::PackageJSON(
			         contentmappertest::TransformingMapper)},
			    {"/home/project/main.ts", std::string("export {};")},
			};
			project::SessionOptions options;
			options.CurrentDirectory = "/home/project";
			options.DefaultLibraryPath = tsc::bundled::LibPath();
			options.PositionEncoding =
			    lsproto::PositionEncodingKindUTF8;
			options.RunExternalCode = true;
			auto [init, utils] =
			    projecttestutil::GetSessionInitOptions(files, &options,
			                                           nullptr);
			auto spawner = std::make_shared<
			    recordingContentMapperSpawner>();
			spawner->inner = std::shared_ptr<contentmapper::Spawner>(
			    contentmappertest::NewSpawner());
			init->Spawner = spawner.get();
			init->KeepAlive.push_back(spawner);
			init->KeepAlive.push_back(spawner->inner);
			auto* session = project::NewSession(init.get());
			t->Cleanup([session] { session->Close(); });

			auto ctx = t->Context();
			session->DidOpenFile(ctx, "file:///home/project/main.ts", 1,
			                     cmFileText(files, "/home/project/main.ts"),
			                     lsproto::LanguageKindTypeScript);
			auto [languageService, err] =
			    session->GetLanguageService(ctx,
			                                "file:///home/project/main.ts");
			assert::NilError(t, err);
			// Access after freezing must not try to initialize using
			// the cleared builder.
			auto* program = languageService->GetProgram();
			auto* mapperProject = program->ContentMapperProject();
			assert::Equal(t, mapperProject != nullptr, hasMapper);
			assert::Equal(t, program->ContentMapperProject(),
			              mapperProject);
			assert::Equal(t, spawner->spawns.load(), int32_t(0));
			delete languageService;
		});
	}
}

void TestContentMapperParallelFileLoading(T* t) {
	t->Parallel();
	projecttestutil::FileMap files{
	    {"/home/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "target": "es2020", "noLib": true },
			"contentMappers": [ { "package": "mapper", "extensions": [".box"] } ]
		})")},
	    {"/home/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::TransformingMapper)},
	    {"/home/project/main.ts", std::string("export {};")},
	};
	// Parallel parsing reads the mapper project identity while another
	// file initializes it.
	const int fileCount = 32;
	for (int i = 0; i < fileCount; i++) {
		files[gostd::sprintf("/home/project/file%d.box", {i})] =
		    std::string("export const version = #{target};\n");
	}
	project::SessionOptions options;
	options.CurrentDirectory = "/home/project";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	std::shared_ptr<contentmapper::Spawner> spawner(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	auto ctx = t->Context();
	session->DidOpenFile(ctx, "file:///home/project/main.ts", 1,
	                     cmFileText(files, "/home/project/main.ts"),
	                     lsproto::LanguageKindTypeScript);
	auto [languageService, err] =
	    session->GetLanguageService(ctx, "file:///home/project/main.ts");
	assert::NilError(t, err);
	auto* mapperProject =
	    languageService->GetProgram()->ContentMapperProject();
	assert::Assert(t, mapperProject != nullptr);
	assert::Equal(t, languageService->GetProgram()->ContentMapperProject(),
	              mapperProject);
	for (int i = 0; i < fileCount; i++) {
		auto fileName = gostd::sprintf("/home/project/file%d.box", {i});
		auto* file = languageService->GetProgram()->GetSourceFile(fileName);
		assert::Assert(t, file != nullptr, gostd::sprintf("expected %s to be loaded", {fileName}));
		assert::Equal(
		    t, file->Text(),
		    std::string(
		        "const __VERSION = \"1.0.0\";\nexport const version = "
		        "7;\n"));
	}
	delete languageService;
}

void TestContentMapperInProject(T* t) {
	t->Parallel();
	auto files = cmInProjectFiles();

	auto newSession = [t, &files](bool trusted)
	    -> std::pair<project::Session*,
	                 std::shared_ptr<projecttestutil::SessionUtils>> {
		project::SessionOptions options;
		options.CurrentDirectory = "/home/project";
		options.DefaultLibraryPath = tsc::bundled::LibPath();
		options.TypingsLocation =
		    std::string(projecttestutil::TestTypingsLocation);
		options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
		options.LoggingEnabled = true;
		options.RunExternalCode = trusted;
		auto [init, utils] = projecttestutil::GetSessionInitOptions(
		    files, &options, nullptr);
		std::shared_ptr<contentmapper::Spawner> spawner(
		    contentmappertest::NewSpawner());
		init->Spawner = spawner.get();
		init->KeepAlive.push_back(spawner);
		// Keep the options alive for the session's lifetime.
		init->KeepAlive.push_back(
		    std::shared_ptr<void>(
		        new project::SessionOptions(options)));
		init->Options =
		    static_cast<project::SessionOptions*>(
		        init->KeepAlive.back().get());
		return {project::NewSession(init.get()), utils};
	};

	t->Run("trusted workspace transforms the content-mapped file",
	       [&](T* t) {
		       t->Parallel();
		       auto newPair = newSession(true);
		auto* session = newPair.first;
		auto& utils = newPair.second;
		       t->Cleanup([session] { session->Close(); });

		       session->DidOpenFile(
		           t->Context(), "file:///home/project/main.ts", 1,
		           cmFileText(files, "/home/project/main.ts"),
		           lsproto::LanguageKindTypeScript);
		       auto [ls_, err] = session->GetLanguageService(
		           t->Context(), "file:///home/project/main.ts");
		       assert::NilError(t, err);

		       auto* boxFile = ls_->GetProgram()->GetSourceFile(
		           "/home/project/app.box");
		       assert::Assert(t, boxFile != nullptr,
		                      "expected app.box to be loaded into the "
		                      "program");
		       // The #{target} token was substituted with the es2020
		       // target value (7) by the content mapper.
		       assert::Assert(
		           t,
		           boxFile->Text().find("export const version = 7;") !=
		               std::string::npos, gostd::sprintf("app.box was not transformed: %q", {boxFile->Text()}));
		       delete ls_;

		       // The config's .box mapper should have been registered
		       // for text document synchronization.
		       session->WaitForBackgroundTasks();
		       auto calls =
		           utils->Client()
		               ->RegisterContentMapperExtensionsCalls();
		       assert::Assert(
		           t, !calls.empty(),
		           "expected RegisterContentMapperExtensions to be "
		           "called");
		       assert::DeepEqual(t, calls.back().Extensions,
		                         std::vector<std::string>{".box"});
	       });

	t->Run("untrusted workspace does not run the content mapper",
	       [&](T* t) {
		       t->Parallel();
		       auto newPair = newSession(false);
		auto* session = newPair.first;
		auto& utils = newPair.second;
		       t->Cleanup([session] { session->Close(); });

		       session->DidOpenFile(
		           t->Context(), "file:///home/project/main.ts", 1,
		           cmFileText(files, "/home/project/main.ts"),
		           lsproto::LanguageKindTypeScript);
		       auto [ls_, err] = session->GetLanguageService(
		           t->Context(), "file:///home/project/main.ts");
		       assert::NilError(t, err);

		       // Without workspace trust, the content mapper gate drops
		       // the mappers, so .box is not a recognized extension and
		       // app.box never enters the program.
		       auto* boxFile = ls_->GetProgram()->GetSourceFile(
		           "/home/project/app.box");
		       assert::Assert(
		           t, boxFile == nullptr,
		           "app.box should not be loaded without trust");
		       delete ls_;

		       // No content mapper extensions should be registered
		       // without trust.
		       session->WaitForBackgroundTasks();
		       for (auto& call : utils->Client()
		                ->RegisterContentMapperExtensionsCalls()) {
			       assert::Equal(
			           t, int64_t(call.Extensions.size()),
			           int64_t(0),
			           "expected no content mapper extensions to be "
			           "registered without trust");
		       }
	       });

	t->Run(
	    "editing an open content-mapped file reparses it through the "
	    "mapper",
	    [&](T* t) {
		    t->Parallel();
		    auto newPair = newSession(true);
		auto* session = newPair.first;
		auto& utils = newPair.second;
		    t->Cleanup([session] { session->Close(); });

		    auto ctx = t->Context();
		    session->DidOpenFile(
		        ctx, "file:///home/project/main.ts", 1,
		        cmFileText(files, "/home/project/main.ts"),
		        lsproto::LanguageKindTypeScript);
		    // Open the .box with its content-mapped language id so its
		    // overlay script kind is Unknown, matching how an editor
		    // opens a content-mapped file. This is what made the
		    // incremental reparse panic.
		    session->DidOpenFile(
		        ctx, "file:///home/project/app.box", 1,
		        cmFileText(files, "/home/project/app.box"),
		        lsproto::LanguageKind("box"));
		   	auto [ls1, err1] = session->GetLanguageService(
		        ctx, "file:///home/project/main.ts");
		    assert::NilError(t, err1);
		    delete ls1;

		    // Editing the open .box file drives the single-file
		    // incremental reparse path (Program.UpdateProgram), which
		    // must re-run the content mapper transform rather than
		    // parse the raw source text.
		    session->DidChangeFile(
		        ctx, "file:///home/project/app.box", 2,
		        {wholeDocChange(
		            "export const version = #{target};\nexport const "
		            "extra = 1;\n")});
		    auto [ls_, err] = session->GetLanguageService(
		        ctx, "file:///home/project/main.ts");
		    assert::NilError(t, err);

		    auto* boxFile = ls_->GetProgram()->GetSourceFile(
		        "/home/project/app.box");
		    assert::Assert(t, boxFile != nullptr,
		                   "expected app.box to be loaded");
		    assert::Assert(
		        t,
		        boxFile->Text().find("export const version = 7;") !=
		            std::string::npos, gostd::sprintf("reparsed app.box was not transformed: %q", {boxFile->Text()}));
		    assert::Assert(
		        t,
		        boxFile->Text().find("export const extra = 1;") !=
		            std::string::npos, gostd::sprintf("reparsed app.box missing the edit: %q", {boxFile->Text()}));;
		    delete ls_;
	    });

	t->Run("watch change to a content-mapped file updates the program",
	       [&](T* t) {
		       t->Parallel();
		       auto newPair = newSession(true);
		auto* session = newPair.first;
		auto& utils = newPair.second;
		       t->Cleanup([session] { session->Close(); });

		       auto ctx = t->Context();
		       lsproto::DocumentUri mainURI =
		           "file:///home/project/main.ts";
		       session->DidOpenFile(
		           ctx, mainURI, 1,
		           cmFileText(files, "/home/project/main.ts"),
		           lsproto::LanguageKindTypeScript);
		       auto [languageService, err] =
		           session->GetLanguageService(ctx, mainURI);
		       assert::NilError(t, err);
		       auto* original =
		           languageService->GetProgram()->GetSourceFile(
		               "/home/project/app.box");
		       assert::Assert(t, original != nullptr,
		                      "expected app.box to be loaded");
		       delete languageService;

		       // Wait until the configured extension set has been
		       // published; watch filtering uses the set captured when
		       // the snapshot change is created.
		       session->WaitForBackgroundTasks();
		       std::string updatedContent =
		           "export const version = #{target};\nexport const "
		           "watched = true;\n";
		       auto werr = utils->FS()->WriteFile("/home/project/app.box",
		                                     updatedContent);
	assert::Assert(t, werr.impl() == nullptr);
		       session->DidChangeWatchedFiles(
		           ctx, {cmFileEvent("file:///home/project/app.box",
		                             lsproto::FileChangeTypeChanged)});

		       auto [languageService2, err2] =
		           session->GetLanguageService(ctx, mainURI);
		       assert::NilError(t, err2);
		       auto* updatedSnapshot = session->Snapshot();
		       auto* configuredProject =
		           updatedSnapshot->GetDefaultProject(mainURI);
		       assert::Assert(t, configuredProject != nullptr,
		                      "expected configured project");
		       assert::Equal(
		           t, configuredProject->ProgramUpdateKind,
		           project::ProgramUpdateKindCloned);
		       assert::Equal(t, configuredProject->ProgramLastUpdate,
		                     updatedSnapshot->ID());
		       auto* updated =
		           languageService2->GetProgram()->GetSourceFile(
		               "/home/project/app.box");
		       assert::Assert(t, updated != nullptr,
		                      "expected app.box to remain loaded");
		       assert::Assert(
		           t, updated != original,
		           "expected the watched content-mapped file to be "
		           "reparsed");
		       assert::Assert(
		           t,
		           updated->Text().find("export const version = 7;") !=
		               std::string::npos, gostd::sprintf("updated app.box was not transformed: %q", {updated->Text()}));
		       assert::Assert(
		           t,
		           updated->Text().find(
		               "export const watched = true;") !=
		               std::string::npos, gostd::sprintf("updated app.box missing watched change: %q", {updated->Text()}));;
		       delete languageService2;
	       });

	t->Run(
	    "unchanged content-mapped file is reused from the cache across a "
	    "full rebuild",
	    [&](T* t) {
		    t->Parallel();
		    auto newPair = newSession(true);
		auto* session = newPair.first;
		auto& utils = newPair.second;
		    t->Cleanup([session] { session->Close(); });

		    auto ctx = t->Context();
		    session->DidOpenFile(
		        ctx, "file:///home/project/main.ts", 1,
		        cmFileText(files, "/home/project/main.ts"),
		        lsproto::LanguageKindTypeScript);
		    auto [ls_, err] = session->GetLanguageService(
		        ctx, "file:///home/project/main.ts");
		    assert::NilError(t, err);
		    auto* boxFile = ls_->GetProgram()->GetSourceFile(
		        "/home/project/app.box");
		    assert::Assert(t, boxFile != nullptr,
		                   "expected app.box to be loaded");
		    assert::Assert(
		        t,
		        boxFile->Text().find("export const version = 7;") !=
		            std::string::npos, gostd::sprintf("app.box was not transformed: %q", {boxFile->Text()}));
		    delete ls_;

		    // Changing a compiler option the mapper does not depend on
		    // (strict) forces a full program rebuild while leaving
		    // app.box's content and the mapper's transform identity
		    // unchanged, so the transformed file must be served from
		    // the parse cache rather than re-transformed.
		    auto werr2 = utils->FS()->WriteFile(
		        "/home/project/tsconfig.json", R"({
			"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler", "strict": false },
			"contentMappers": [ { "package": "mapper", "extensions": [".box"] } ]
		})");
	assert::Assert(t, werr2.impl() == nullptr);
		    assert::NilError(t, err);
		    session->DidChangeWatchedFiles(
		        ctx,
		        {cmFileEvent("file:///home/project/tsconfig.json",
		                     lsproto::FileChangeTypeChanged)});

		    auto [ls2, err2] = session->GetLanguageService(
		        ctx, "file:///home/project/main.ts");
		    assert::NilError(t, err2);
		    auto* rebuiltBox = ls2->GetProgram()->GetSourceFile(
		        "/home/project/app.box");
		    assert::Assert(
		        t, rebuiltBox == boxFile, "expected the unchanged content-mapped file to be "
		        "reused from the parse cache, not re-transformed");
		    delete ls2;
	    });
}

void TestContentMapperPackageManifestChangeReloadsConfig(T* t) {
	t->Parallel();
	const std::string packageJsonPath = "/home/mapper/package.json";
	projecttestutil::FileMap files{
	    {"/home/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler" },
			"contentMappers": [{ "package": "mapper", "extensions": [".box"] }]
		})")},
	    {"/home/project/node_modules/mapper",
	     tsc::vfs::vfstest::Symlink("/home/mapper")},
	    {packageJsonPath,
	     std::string(R"({
			"name": "mapper",
			"version": "1.0.0",
			"typescript": { "contentMapper": { "exec": ["compiler-test-mapper"] } }
		})")},
	    {"/home/project/app.box",
	     std::string("export const version = #{target};\n")},
	    {"/home/project/main.ts",
	     std::string("import { version } from \"./app.box\";\n")},
	};
	auto caps = std::make_shared<lsproto::ResolvedClientCapabilities>();
	caps->Workspace.DidChangeWatchedFiles.RelativePatternSupport = true;
	auto ctx = lsproto::withClientCapabilities(
	    gostd::contextBackground(), caps);
	project::SessionOptions options;
	options.CurrentDirectory = "/home/project";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	options.WatchEnabled = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	init->BackgroundCtx = ctx;
	std::shared_ptr<contentmapper::Spawner> spawner(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	lsproto::DocumentUri mainURI = "file:///home/project/main.ts";
	session->DidOpenFile(ctx, mainURI, 1,
	                     cmFileText(files, "/home/project/main.ts"),
	                     lsproto::LanguageKindTypeScript);
	auto [ls1, err] = session->GetLanguageService(ctx, mainURI);
	assert::NilError(t, err);
	delete ls1;
	auto* configuredProject = session->Snapshot()->GetDefaultProject(mainURI);
	assert::Assert(t, configuredProject != nullptr);
	auto mappers = configuredProject->CommandLine->ContentMappers();
	assert::Equal(t, int64_t(mappers.size()), int64_t(1));
	assert::Equal(t, mappers[0]->PackageDirectory,
	              std::string("/home/mapper"));
	session->WaitForBackgroundTasks();
	assert::Assert(
	    t, utils->WatchesFile(packageJsonPath),
	    "expected the invalid mapper package manifest to be watched");
	bool foundWatcher = false;
	for (auto& call : utils->Client()->WatchFilesCalls()) {
		for (auto* watcher : call.Watchers) {
			auto* relative = watcher->GlobPattern.RelativePattern.get();
			if (relative != nullptr && relative->BaseUri.URI != nullptr &&
			    std::string(*relative->BaseUri.URI) ==
			        "file:///home/mapper" &&
			    relative->Pattern == "**/*") {
				foundWatcher = true;
			}
		}
	}
	assert::Assert(
	    t, foundWatcher,
	    "expected an external relative-pattern watcher for the mapper "
	    "package");

	auto fixedManifest = std::string(
	    contentmappertest::PackageJSON(contentmappertest::TransformingMapper));
	auto pos = fixedManifest.find("\"version\": \"1.0.0\"");
	assert::Assert(t, pos != std::string::npos);
	fixedManifest.replace(pos, std::string("\"version\": \"1.0.0\"").size(),
	                      "\"version\": \"2.0.0\"");
	assert::Assert(t, fixedManifest.find("\"version\": \"2.0.0\"") !=
	                    std::string::npos);
	auto werr = utils->FS()->WriteFile(packageJsonPath, fixedManifest);
	assert::Assert(t, werr.impl() == nullptr);
	session->DidChangeWatchedFiles(
	    ctx, {cmFileEvent("file://" + lsproto::DocumentUri(packageJsonPath),
	                      lsproto::FileChangeTypeChanged)});

	auto [languageService, err2] =
	    session->GetLanguageService(ctx, mainURI);
	assert::NilError(t, err2);
	auto* boxFile = languageService->GetProgram()->GetSourceFile(
	    "/home/project/app.box");
	assert::Assert(t, boxFile != nullptr,
	               "expected app.box in the rebuilt program");
	assert::Assert(
	    t,
	    boxFile->Text().find("export const version = 7;") !=
	        std::string::npos, gostd::sprintf("expected fixed mapper manifest to be reloaded: %q", {boxFile->Text()}));
	delete languageService;
}

void TestContentMapperSupplementalFileClonedOnEdit(T* t) {
	t->Parallel();
	projecttestutil::FileMap files{
	    {"/home/project/tsconfig.json",
	     std::string(
	         "{ \"compilerOptions\": { \"strict\": true }, "
	         "\"contentMappers\": [{ \"package\": \"mapper\", "
	         "\"extensions\": [\".box\"] }] }")},
	    {"/home/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::SupplementalMapper)},
	    {"/home/project/app.box",
	     std::string("declare const supplementalValue: number;\n")},
	    {"/home/project/extra.d.ts",
	     std::string("interface Extra {}\n")},
	    {"/home/project/main.ts",
	     std::string("const value: number = supplementalValue;\n")},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home/project";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	std::shared_ptr<contentmapper::Spawner> spawner(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	auto ctx = t->Context();
	lsproto::DocumentUri mainURI = "file:///home/project/main.ts";
	session->DidOpenFile(ctx, mainURI, 1,
	                     cmFileText(files, "/home/project/main.ts"),
	                     lsproto::LanguageKindTypeScript);
	auto [languageService, err] =
	    session->GetLanguageService(ctx, mainURI);
	assert::NilError(t, err);
	auto* oldProgram = languageService->GetProgram();
	auto* oldCanonical =
	    oldProgram->GetSourceFile("/home/project/app.box");
	auto* oldSupplemental = oldCanonical->SupplementalSourceFiles();
	assert::Equal(t, int64_t(oldSupplemental->size()), int64_t(1));
	assert::Equal(t, (*oldSupplemental)[0]->FileName(),
	              std::string("/home/project/app.box.0.ts"));
	assert::Equal(
	    t, std::string((*oldSupplemental)[0]->Path()),
	    std::string("/home/project/app.box.0.ts"));
	assert::Assert(
	    t, (*oldSupplemental)[0]->Hash == oldCanonical->Hash);
	assert::Assert(
	    t, oldProgram->GetSourceFileByPath((*oldSupplemental)[0]->Path()) ==
	           (*oldSupplemental)[0]);
	assert::Assert(t,
	               oldProgram->FilesByPath().at(
	                   (*oldSupplemental)[0]->Path()) ==
	                   (*oldSupplemental)[0]);
	delete languageService;

	auto werr = utils->FS()->WriteFile(
	           "/home/project/app.box",
	           "declare const supplementalValue: string;\n");
	assert::Assert(t, werr.impl() == nullptr);
	session->DidChangeWatchedFiles(
	    ctx, {cmFileEvent("file:///home/project/app.box",
	                      lsproto::FileChangeTypeChanged)});
	auto [languageService2, err2] =
	    session->GetLanguageService(ctx, mainURI);
	assert::NilError(t, err2);
	auto* updatedSnapshot = session->Snapshot();
	auto* configuredProject =
	    updatedSnapshot->GetDefaultProject(mainURI);
	assert::Equal(t, configuredProject->ProgramUpdateKind,
	              project::ProgramUpdateKindCloned);

	auto* newProgram = languageService2->GetProgram();
	auto* newCanonical =
	    newProgram->GetSourceFile("/home/project/app.box");
	auto* newSupplemental = newCanonical->SupplementalSourceFiles();
	assert::Equal(t, int64_t(newSupplemental->size()), int64_t(1));
	assert::Assert(t, (*newSupplemental)[0]->Path() ==
	                      (*oldSupplemental)[0]->Path());
	assert::Assert(t, newCanonical != oldCanonical);
	assert::Assert(t, (*newSupplemental)[0] != (*oldSupplemental)[0]);
	assert::Assert(
	    t, (*newSupplemental)[0]->Hash == newCanonical->Hash);
	assert::Assert(t,
	               (*newSupplemental)[0]->Hash !=
	                   (*oldSupplemental)[0]->Hash);
	assert::Assert(t,
	               newProgram->FilesByPath().at(
	                   (*newSupplemental)[0]->Path()) ==
	                   (*newSupplemental)[0]);
	assert::Assert(t,
	               (*newSupplemental)[0]->Text().find(
	                   "supplementalValue: string") != std::string::npos);
	auto* mainFile = newProgram->GetSourceFile("/home/project/main.ts");
	auto diagnostics = newProgram->GetSemanticDiagnostics(mainFile);
	assert::Assert(
	    t, std::any_of(diagnostics.begin(), diagnostics.end(),
	                   [](auto* diagnostic) {
		                   return diagnostic->Code() == 2322;
	                   }));

	// Changing the supplemental file's reference graph must fall back
	// from cloning to a full rebuild.
	auto werr2 = utils->FS()->WriteFile(
	           "/home/project/app.box",
	           "/// <reference path=\"./extra.d.ts\" />\ndeclare const "
	           "supplementalValue: string;\n");
	assert::Assert(t, werr2.impl() == nullptr);
	session->DidChangeWatchedFiles(
	    ctx, {cmFileEvent("file:///home/project/app.box",
	                      lsproto::FileChangeTypeChanged)});
	delete languageService2;
	auto [ls3, err3] = session->GetLanguageService(ctx, mainURI);
	assert::NilError(t, err3);
	configuredProject =
	    session->Snapshot()->GetDefaultProject(mainURI);
	assert::Equal(t, configuredProject->ProgramUpdateKind,
	              project::ProgramUpdateKindSameFileNames);
	delete ls3;
}

void TestContentMapperModuleExtensionClonedOnUnrelatedEdit(T* t) {
	t->Parallel();
	projecttestutil::FileMap files{
	    {"/home/project/tsconfig.json",
	     std::string(
	         "{ \"contentMappers\": [{ \"package\": \"mapper\", "
	         "\"extensions\": [\".box\"] }] }")},
	    {"/home/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::ModuleVerbatimMapper)},
	    {"/home/project/app.box",
	     std::string("export const value = 1;\n")},
	    {"/home/project/main.ts",
	     std::string(
	         "import { value } from \"./app.box\"; value;")},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home/project";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	std::shared_ptr<contentmapper::Spawner> spawner(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	auto ctx = t->Context();
	lsproto::DocumentUri mainURI = "file:///home/project/main.ts";
	session->DidOpenFile(ctx, mainURI, 1,
	                     cmFileText(files, "/home/project/main.ts"),
	                     lsproto::LanguageKindTypeScript);
	auto [languageService, err] =
	    session->GetLanguageService(ctx, mainURI);
	assert::NilError(t, err);
	auto* mappedFile = languageService->GetProgram()->GetSourceFile(
	    "/home/project/app.box");
	assert::Assert(t, mappedFile != nullptr);
	assert::Equal(t, mappedFile->VirtualFileName(),
	              std::string("/home/project/app.box.mts"));
	assert::Assert(
	    t, mappedFile->ParseOptions()
	           .ExternalModuleIndicatorOptions.Force);

	session->DidChangeFile(ctx, mainURI, 2,
	                       {wholeDocChange(
	                           "import { value } from \"./app.box\"; value "
	                           "+ 1;")});
	auto [languageService2, err2] =
	    session->GetLanguageService(ctx, mainURI);
	assert::NilError(t, err2);
	auto* configuredProject =
	    session->Snapshot()->GetDefaultProject(mainURI);
	assert::Equal(t, configuredProject->ProgramUpdateKind,
	              project::ProgramUpdateKindCloned);
	assert::Assert(
	    t, languageService2->GetProgram()->GetSourceFile(
	           "/home/project/app.box") == mappedFile);
	delete languageService;
	delete languageService2;
}

void TestContentMapperLocaleChange(T* t) {
	t->Parallel();
	projecttestutil::FileMap files{
	    {"/home/project/tsconfig.json",
	     std::string(
	         "{ \"contentMappers\": [ { \"package\": \"mapper\", "
	         "\"extensions\": [\".box\"] } ] }")},
	    {"/home/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::VerbatimMapper)},
	    {"/home/project/app.box",
	     std::string("export const value = 1;\n")},
	    {"/home/project/main.ts",
	     std::string(
	         "import { value } from \"./app.box\"; value;")},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home/project";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	auto spawner = std::make_shared<recordingContentMapperSpawner>();
	spawner->inner = std::shared_ptr<contentmapper::Spawner>(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	init->KeepAlive.push_back(spawner->inner);

	// localeMu/currentLocale emulate the client's mutable locale.
	auto localeMu = std::make_shared<std::mutex>();
	auto currentLocale =
	    std::make_shared<locale::Locale>(locale::Default);
	utils->Client()->GetLocaleFunc = [localeMu, currentLocale] {
		std::lock_guard<std::mutex> lock(*localeMu);
		return *currentLocale;
	};
	utils->Client()->SetLocaleFunc =
	    [t, localeMu, currentLocale](const std::string& value) {
		    auto [updated, ok] = locale::parse(value);
		    assert::Assert(t, ok);
		    std::lock_guard<std::mutex> lock(*localeMu);
		    *currentLocale = updated;
	    };

	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });
	auto ctx =
	    locale::withLocale(gostd::contextBackground(), locale::Default);
	auto localeReads =
	    utils->Client()->GetLocaleCalls().size();
	session->DidOpenFile(ctx, "file:///home/project/main.ts", 1,
	                     cmFileText(files, "/home/project/main.ts"),
	                     lsproto::LanguageKindTypeScript);
	auto [ls1, err] = session->GetLanguageService(
	    ctx, "file:///home/project/main.ts");
	assert::NilError(t, err);
	delete ls1;
	// Snapshot adoption reads the current locale for its background
	// work; project construction should not.
	assert::Equal(t, int64_t(utils->Client()->GetLocaleCalls().size()),
	              int64_t(localeReads) + 1);
	assert::Equal(t, spawner->spawns.load(), int32_t(1));

	auto preferences = session->Config();
	preferences.Locale = "fr";
	session->Configure(preferences);
	assert::Equal(t, spawner->closes.load(), int32_t(1));

	auto [ls2, err2] = session->GetLanguageService(
	    gostd::contextBackground(), "file:///home/project/main.ts");
	assert::NilError(t, err2);
	assert::Equal(t, spawner->spawns.load(), int32_t(2));
	delete ls2;
}

void TestDynamicContentMapperInProject(T* t) {
	t->Parallel();
	projecttestutil::FileMap files{
	    {"/home/project/tsconfig.json",
	     std::string(
	         "{ \"contentMappers\": [ { \"package\": \"mapper\", "
	         "\"extensions\": [\".box\"], \"options\": { \"mode\": "
	         "\"project\" } } ] }")},
	    {"/home/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::DynamicVerbatimMapper)},
	    {"/home/project/mapper.config.json",
	     std::string("{ \"version\": 1 }")},
	    {"/home/project/app.box",
	     std::string("export const value = 1;\n")},
	    {"/home/project/main.ts",
	     std::string(
	         "import { value } from \"./app.box\"; value;")},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home/project";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	std::shared_ptr<contentmapper::Spawner> spawner(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	session->DidOpenFile(t->Context(), "file:///home/project/main.ts", 1,
	                     cmFileText(files, "/home/project/main.ts"),
	                     lsproto::LanguageKindTypeScript);
	auto [languageService, err] = session->GetLanguageService(
	    t->Context(), "file:///home/project/main.ts");
	assert::NilError(t, err);
	auto* mappedFile = languageService->GetProgram()->GetSourceFile(
	    "/home/project/app.box");
	assert::Assert(t, mappedFile != nullptr);
	assert::Assert(t, !mappedFile->IsContentMapperFailureStub());

	auto* program = languageService->GetProgram();
	auto werr = utils->FS()->WriteFile(
	                      "/home/project/mapper.config.json",
	                      "{ \"version\": 2 }");
	assert::Assert(t, werr.impl() == nullptr);
	assert::Assert(t, werr.impl() == nullptr);
	session->DidChangeWatchedFiles(
	    t->Context(),
	    {cmFileEvent("file:///home/project/mapper.config.json",
	                 lsproto::FileChangeTypeChanged)});
	auto [languageService2, err2] = session->GetLanguageService(
	    t->Context(), "file:///home/project/main.ts");
	assert::NilError(t, err2);
	assert::Assert(t, languageService2->GetProgram() != program);
	mappedFile = languageService2->GetProgram()->GetSourceFile(
	    "/home/project/app.box");
	assert::Assert(t, mappedFile != nullptr);
	assert::Assert(t, !mappedFile->IsContentMapperFailureStub());
	delete languageService;
	delete languageService2;
}

void TestDynamicContentMapperRefreshesForMixedWatchBatches(T* t) {
	t->Parallel();
	struct testCase {
		std::string name;
		std::function<std::vector<lsproto::FileEvent*>()> events;
	};
	std::vector<testCase> tests{
	    {"excessive events",
	     [] {
		     std::vector<lsproto::FileEvent*> events;
		     for (int i = 0; i < 1001; i++) {
			     events.push_back(cmFileEvent(
			         gostd::sprintf("file:///home/project/noise-%d.ts", {i}),
			         lsproto::FileChangeTypeChanged));
		     }
		     events[0] = cmFileEvent(
		         "file:///home/project/mapper.config.json",
		         lsproto::FileChangeTypeChanged);
		     events[1] =
		         cmFileEvent("file:///home/project/main.ts",
		                     lsproto::FileChangeTypeChanged);
		     return events;
	     }},
	    {"changed files make project fully dirty before mapper deletion",
	     [] {
		     return std::vector<lsproto::FileEvent*>{
		         cmFileEvent("file:///home/project/main.ts",
		                     lsproto::FileChangeTypeChanged),
		         cmFileEvent("file:///home/project/app.box",
		                     lsproto::FileChangeTypeChanged),
		         cmFileEvent("file:///home/project/mapper.config.json",
		                     lsproto::FileChangeTypeDeleted),
		     };
	     }},
	    {"changed files make project fully dirty before mapper creation",
	     [] {
		     return std::vector<lsproto::FileEvent*>{
		         cmFileEvent("file:///home/project/main.ts",
		                     lsproto::FileChangeTypeChanged),
		         cmFileEvent("file:///home/project/app.box",
		                     lsproto::FileChangeTypeChanged),
		         cmFileEvent("file:///home/project/mapper.config.json",
		                     lsproto::FileChangeTypeCreated),
		     };
	     }},
	};
	for (auto& test : tests) {
		t->Run(test.name, [&test](T* t) {
			t->Parallel();
			projecttestutil::FileMap files{
			    {"/home/project/tsconfig.json",
			     std::string(
			         "{ \"contentMappers\": [{ \"package\": \"mapper\", "
			         "\"extensions\": [\".box\"] }] }")},
			    {"/home/project/node_modules/mapper/package.json",
			     contentmappertest::PackageJSON(
			         contentmappertest::DynamicVerbatimMapper)},
			    {"/home/project/mapper.config.json",
			     std::string("{ \"version\": 1 }")},
			    {"/home/project/app.box",
			     std::string("export const value = 1;\n")},
			    {"/home/project/main.ts",
			     std::string(
			         "import { value } from \"./app.box\"; value;")},
			};
			project::SessionOptions options;
			options.CurrentDirectory = "/home/project";
			options.DefaultLibraryPath = tsc::bundled::LibPath();
			options.TypingsLocation =
			    std::string(projecttestutil::TestTypingsLocation);
			options.PositionEncoding =
			    lsproto::PositionEncodingKindUTF8;
			options.RunExternalCode = true;
			auto [init, utils] =
			    projecttestutil::GetSessionInitOptions(files, &options,
			                                           nullptr);
			auto* lifecycle =
			    new contentmappertest::ProjectLifecycle();
			auto* spawner =
			    contentmappertest::NewSpawnerWithProjectLifecycle(
			        lifecycle);
			init->Spawner = spawner;
			init->KeepAlive.push_back(
			    std::shared_ptr<void>(lifecycle));
			init->KeepAlive.push_back(
			    std::shared_ptr<contentmapper::Spawner>(spawner));
			auto* session = project::NewSession(init.get());
			t->Cleanup([session] { session->Close(); });

			auto ctx = t->Context();
			lsproto::DocumentUri mainURI =
			    "file:///home/project/main.ts";
			session->DidOpenFile(
			    ctx, mainURI, 1,
			    cmFileText(files, "/home/project/main.ts"),
			    lsproto::LanguageKindTypeScript);
			auto [ls1, err] =
			    session->GetLanguageService(ctx, mainURI);
			assert::NilError(t, err);
			delete ls1;
			assert::Equal(t, lifecycle->Opens.load(), int32_t(1));
			assert::Equal(t, lifecycle->Closes.load(), int32_t(0));

			auto events = test.events();
			session->DidChangeWatchedFiles(ctx, events);
			session->WaitForBackgroundTasks();
			auto [ls2, err2] =
			    session->GetLanguageService(ctx, mainURI);
			assert::NilError(t, err2);
			delete ls2;
			assert::Equal(t, lifecycle->Closes.load(), int32_t(1));
			assert::Equal(t, lifecycle->Opens.load(), int32_t(2));
		});
	}
}

void TestUnusedDynamicContentMapperIsNotOpened(T* t) {
	t->Parallel();
	projecttestutil::FileMap files{
	    {"/home/project/tsconfig.json",
	     std::string(
	         "{ \"contentMappers\": [{ \"package\": \"mapper\", "
	         "\"extensions\": [\".box\"] }] }")},
	    {"/home/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::DynamicVerbatimMapper)},
	    {"/home/project/main.ts",
	     std::string("export const value = 1;")},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home/project";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	auto* lifecycle = new contentmappertest::ProjectLifecycle();
	auto* spawner =
	    contentmappertest::NewSpawnerWithProjectLifecycle(lifecycle);
	init->Spawner = spawner;
	init->KeepAlive.push_back(std::shared_ptr<void>(lifecycle));
	init->KeepAlive.push_back(
	    std::shared_ptr<contentmapper::Spawner>(spawner));
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	lsproto::DocumentUri uri = "file:///home/project/main.ts";
	session->DidOpenFile(t->Context(), uri, 1,
	                     cmFileText(files, "/home/project/main.ts"),
	                     lsproto::LanguageKindTypeScript);
	auto [ls1, err] = session->GetLanguageService(t->Context(), uri);
	assert::NilError(t, err);
	delete ls1;
	assert::Equal(t, lifecycle->Opens.load(), int32_t(0));
}

void TestContentMappersInParallelProjectReferences(T* t) {
	t->Parallel();
	projecttestutil::FileMap files{
	    {"/home/project/tsconfig.json",
	     std::string(R"({
			"files": ["src/index.d.ts"],
			"references": [{ "path": "./a" }, { "path": "./b" }]
		})")},
	    {"/home/project/src/index.d.ts", std::string("export {};")},
	    {"/home/project/a/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "composite": true },
			"files": ["../src/index.d.ts"],
			"contentMappers": [{ "package": "mapper", "extensions": [".vue"] }]
		})")},
	    {"/home/project/b/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "composite": true },
			"files": ["../src/index.d.ts"],
			"contentMappers": [{ "package": "mapper", "extensions": [".svelte"] }]
		})")},
	    {"/home/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::TransformingMapper)},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home/project";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	std::shared_ptr<contentmapper::Spawner> spawner(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	lsproto::DocumentUri uri = "file:///home/project/src/index.d.ts";
	session->DidOpenFile(t->Context(), uri, 1,
	                     cmFileText(files, "/home/project/src/index.d.ts"),
	                     lsproto::LanguageKindTypeScript);
	session->WaitForBackgroundTasks();
	auto calls =
	    utils->Client()->RegisterContentMapperExtensionsCalls();
	assert::Assert(t, !calls.empty());
	auto extensions = calls.back().Extensions;
	std::sort(extensions.begin(), extensions.end());
	assert::DeepEqual(t, extensions,
	                  std::vector<std::string>{".svelte", ".vue"});
}

void TestContentMapperOpenFileExcludedByConfigChange(T* t) {
	t->Parallel();
	projecttestutil::FileMap files{
	    {"/home/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler", "strict": true },
			"include": ["src"],
			"contentMappers": [ { "package": "mapper", "extensions": [".box"] } ]
		})")},
	    {"/home/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::TransformingMapper)},
	    {"/home/project/src/app.box",
	     std::string("export const version = #{target};\n")},
	    {"/home/project/src/main.ts",
	     std::string("export const main = true;\n")},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home/project";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	std::shared_ptr<contentmapper::Spawner> spawner(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	auto ctx = t->Context();
	lsproto::DocumentUri boxURI = "file:///home/project/src/app.box";
	auto* mapper = new contentmapper::Mapper();
	mapper->Definition.Package = "test.extension";
	mapper->Definition.Extensions = {".box"};
	mapper->Manifest.Name = "mapper";
	mapper->Manifest.Version = "1.0.0";
	mapper->Manifest.Exec = {
	    std::string(contentmappertest::TransformingMapper)};
	mapper->Manifest.CompilerOptions =
	    contentmappertest::DeclaredOptions;
	mapper->PackageDirectory = "/home/project";
	mapper->ContributionID = "test.extension[0]";
	project::ContentMapperContributions contributions;
	contributions.Mappers = {mapper};
	contributions.Extensions = {".box"};
	session->SetContentMapperContributions(ctx, contributions, {});
	session->DidOpenFile(
	    ctx, boxURI, 1,
	    cmFileText(files, "/home/project/src/app.box"),
	    lsproto::LanguageKind("box"));
	auto [languageService, err] =
	    session->GetLanguageService(ctx, boxURI);
	assert::NilError(t, err);
	assert::Assert(t,
	               languageService->GetProgram()->GetSourceFile(
	                   "/home/project/src/app.box") != nullptr);
	delete languageService;

	auto werr = utils->FS()->WriteFile(
	                      "/home/project/tsconfig.json", R"({
		"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler", "strict": true },
		"include": ["src/**/*.ts"],
		"contentMappers": [ { "package": "mapper", "extensions": [".box"] } ]
	})");
	assert::Assert(t, werr.impl() == nullptr);
	assert::Assert(t, werr.impl() == nullptr);
	session->DidChangeWatchedFiles(
	    ctx, {cmFileEvent("file:///home/project/tsconfig.json",
	                      lsproto::FileChangeTypeChanged)});
	session->WaitForBackgroundTasks();

	// The background update removes app.box from the configured project,
	// but inferred project cleanup is deferred until the next file open.
	assert::Assert(t,
	               session->Snapshot()->GetDefaultProject(boxURI) ==
	                   nullptr);
	lsproto::DocumentUri mainURI = "file:///home/project/src/main.ts";
	session->DidOpenFile(
	    ctx, mainURI, 1,
	    cmFileText(files, "/home/project/src/main.ts"),
	    lsproto::LanguageKindTypeScript);

	auto [languageService2, err2] =
	    session->GetLanguageService(ctx, boxURI);
	assert::NilError(t, err2);
	auto* defaultProject = session->Snapshot()->GetDefaultProject(boxURI);
	assert::Assert(t, defaultProject != nullptr,
	               "expected a default project for the open app.box");
	assert::Equal(t, defaultProject->Kind, project::KindInferred);
	auto* boxFile = languageService2->GetProgram()->GetSourceFile(
	    "/home/project/src/app.box");
	assert::Assert(t, boxFile != nullptr,
	               "expected the open app.box in the inferred project");
	assert::Assert(t, boxFile->ContentMapper() != "",
	               "expected app.box to retain its content mapper");
	assert::Assert(t,
	               boxFile->Text().find("#{target}") == std::string::npos, gostd::sprintf("expected app.box to be transformed: %q", {boxFile->Text()}));
	delete languageService2;
}

void TestContentMapperRemovalWithOpenFile(T* t) {
	t->Parallel();
	projecttestutil::FileMap files{
	    {"/home/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler" },
			"contentMappers": [ { "package": "mapper", "extensions": [".box"] } ]
		})")},
	    {"/home/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::TransformingMapper)},
	    {"/home/project/app.box",
	     std::string("export const version = #{target};\n")},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home/project";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	auto spawner = std::make_shared<recordingContentMapperSpawner>();
	spawner->inner = std::shared_ptr<contentmapper::Spawner>(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	init->KeepAlive.push_back(spawner->inner);
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	auto ctx = t->Context();
	lsproto::DocumentUri boxURI = "file:///home/project/app.box";
	session->DidOpenFile(ctx, boxURI, 1,
	                     cmFileText(files, "/home/project/app.box"),
	                     lsproto::LanguageKind("box"));
	auto [languageService, err] =
	    session->GetLanguageService(ctx, boxURI);
	assert::NilError(t, err);
	assert::Assert(t,
	               languageService->GetProgram()
	                       ->GetSourceFile("/home/project/app.box")
	                       ->ContentMapper() != "");
	assert::Equal(t, spawner->spawns.load(), int32_t(1));
	assert::Equal(t, spawner->closes.load(), int32_t(0));
	for (int32_t version = 2; version <= 4; version++) {
		session->DidChangeFile(
		    ctx, boxURI, version,
		    {wholeDocChange(gostd::sprintf("export const version = %d;\n", {version}))});
		auto [ls2, err2] = session->GetLanguageService(ctx, boxURI);
		assert::NilError(t, err2);
		delete ls2;
		assert::Equal(t, spawner->spawns.load(), int32_t(1),
		              "snapshot clone should reuse the mapper process");
		assert::Equal(
		    t, spawner->closes.load(), int32_t(0),
		    "snapshot clone should preserve overlapping ownership");
	}
	auto [releaseOldSnapshot, err3] =
	    session->WithLanguageServiceAndSnapshot(
	        ctx, boxURI,
	        [](ls::LanguageService* ls_,
	           project::Snapshot*)
	            -> std::pair<std::function<gostd::Error()>, gostd::Error> {
		        // The LanguageService owns its host's snapshot ref;
		        // Go's fn ignores it, so release it here.
		        delete ls_;
		        return {[] { return gostd::Error(nullptr); },
		                gostd::Error(nullptr)};
	        });
	assert::NilError(t, err3);
	delete languageService;

	auto werr = utils->FS()->WriteFile(
	                      "/home/project/tsconfig.json", R"({
		"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler" }
	})");
	assert::Assert(t, werr.impl() == nullptr);
	assert::Assert(t, werr.impl() == nullptr);
	session->DidChangeWatchedFiles(
	    ctx, {cmFileEvent("file:///home/project/tsconfig.json",
	                      lsproto::FileChangeTypeChanged)});

	auto [ls4, err4] = session->GetLanguageService(ctx, boxURI);
	assert::ErrorContains(t, err4, "no project found");
	assert::Assert(
	    t, session->Snapshot()->GetFile("/home/project/app.box") !=
	           nullptr,
	    "overlay should remain until didClose");
	assert::Assert(
	    t, session->Snapshot()->GetDefaultProject(boxURI) == nullptr,
	    "unsupported file should not be in a project");

	session->WaitForBackgroundTasks();
	assert::Equal(t, spawner->closes.load(), int32_t(0),
	              "live old snapshot should retain the mapper process");
	assert::NilError(t, releaseOldSnapshot());
	assert::Equal(t, spawner->closes.load(), int32_t(1),
	              "process should close after the final live snapshot is "
	              "released");
	auto calls =
	    utils->Client()->RegisterContentMapperExtensionsCalls();
	assert::Assert(t, !calls.empty(),
	               "expected content mapper registration updates");
	assert::Equal(t, int64_t(calls.back().Extensions.size()),
	              int64_t(0),
	              "expected content mapper extensions to be unregistered");

	session->DidCloseFile(ctx, boxURI);
	auto [ls5, err5] = session->GetLanguageService(ctx, boxURI);
	assert::ErrorContains(t, err5, "no project found");
}

void TestContentMapperProcessSharedAcrossProjects(T* t) {
	t->Parallel();
	auto config = [](const std::string& extension) {
		return gostd::sprintf(R"({
			"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler" },
			"contentMappers": [ { "package": "mapper", "extensions": [%q] } ]
		})", {extension.c_str()});
	};
	projecttestutil::FileMap files{
	    {"/home/a/tsconfig.json", config(".box")},
	    {"/home/a/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::TransformingMapper)},
	    {"/home/a/app.box", std::string("export const a = 1;\n")},
	    {"/home/b/tsconfig.json", config(".panel")},
	    {"/home/b/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::TransformingMapper)},
	    {"/home/b/app.panel", std::string("export const b = 1;\n")},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	auto spawner = std::make_shared<recordingContentMapperSpawner>();
	spawner->inner = std::shared_ptr<contentmapper::Spawner>(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	init->KeepAlive.push_back(spawner->inner);
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	auto ctx = t->Context();
	lsproto::DocumentUri aURI = "file:///home/a/app.box";
	lsproto::DocumentUri bURI = "file:///home/b/app.panel";
	session->DidOpenFile(ctx, aURI, 1,
	                     cmFileText(files, "/home/a/app.box"),
	                     lsproto::LanguageKind("box"));
	auto [ls1, err] = session->GetLanguageService(ctx, aURI);
	assert::NilError(t, err);
	delete ls1;
	session->DidOpenFile(ctx, bURI, 1,
	                     cmFileText(files, "/home/b/app.panel"),
	                     lsproto::LanguageKind("panel"));
	auto [ls2, err2] = session->GetLanguageService(ctx, bURI);
	assert::NilError(t, err2);
	delete ls2;
	assert::Equal(t, spawner->spawns.load(), int32_t(1),
	              "same mapper identity should share one process");

	auto werr = utils->FS()->WriteFile("/home/a/tsconfig.json", "{}");
	assert::Assert(t, werr.impl() == nullptr);
	session->DidChangeWatchedFiles(
	    ctx, {cmFileEvent("file:///home/a/tsconfig.json",
	                      lsproto::FileChangeTypeChanged)});
	auto [ls3, err3] = session->GetLanguageService(ctx, aURI);
	assert::ErrorContains(t, err3, "no project found");
	assert::Equal(t, spawner->closes.load(), int32_t(0),
	              "second project still owns the shared process");

	auto werr2 = utils->FS()->WriteFile("/home/b/tsconfig.json", "{}");
	assert::Assert(t, werr2.impl() == nullptr);
	session->DidChangeWatchedFiles(
	    ctx, {cmFileEvent("file:///home/b/tsconfig.json",
	                      lsproto::FileChangeTypeChanged)});
	auto [ls4, err4] = session->GetLanguageService(ctx, bURI);
	assert::ErrorContains(t, err4, "no project found");
	assert::Equal(t, spawner->closes.load(), int32_t(1),
	              "final project owner should close the shared process");
}

void TestContentMapperInferredProjectUsesExtensionContributions(T* t) {
	t->Parallel();
	projecttestutil::FileMap files{
	    {"/home/configured/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler" },
			"contentMappers": [ { "package": "mapper", "extensions": [".box"] } ]
		})")},
	    {"/home/configured/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::TransformingMapper)},
	    {"/home/configured/main.ts",
	     std::string("export const main = true;\n")},
	    {"/home/loose/app.box",
	     std::string("export const version = #{target};\n")},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	std::shared_ptr<contentmapper::Spawner> spawner(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	auto ctx = t->Context();
	lsproto::DocumentUri configuredURI =
	    "file:///home/configured/main.ts";
	session->DidOpenFile(ctx, configuredURI, 1,
	                     cmFileText(files, "/home/configured/main.ts"),
	                     lsproto::LanguageKindTypeScript);
	auto [ls1, err] = session->GetLanguageService(ctx, configuredURI);
	assert::NilError(t, err);
	delete ls1;

	lsproto::DocumentUri boxURI = "file:///home/loose/app.box";
	session->DidOpenFile(ctx, boxURI, 1,
	                     cmFileText(files, "/home/loose/app.box"),
	                     lsproto::LanguageKind("box"));
	auto [ls2, err2] = session->GetLanguageService(ctx, boxURI);
	assert::ErrorContains(
	    t, err2, "no project found",
	    "configured mapper must not leak into inferred projects");
	auto* mapper = new contentmapper::Mapper();
	mapper->Definition.Package = "test.extension";
	mapper->Definition.Extensions = {".box"};
	mapper->Manifest.Name = "mapper";
	mapper->Manifest.Version = "1.0.0";
	mapper->Manifest.Exec = {
	    std::string(contentmappertest::TransformingMapper)};
	mapper->Manifest.CompilerOptions =
	    contentmappertest::DeclaredOptions;
	mapper->PackageDirectory = "/home";
	mapper->ContributionID = "test.extension[0]";
	project::ContentMapperContributions contributions;
	contributions.Mappers = {mapper};
	contributions.Extensions = {".box"};
	session->SetContentMapperContributions(ctx, contributions, {boxURI});
	auto [languageService, err3] =
	    session->GetLanguageService(ctx, boxURI);
	assert::NilError(t, err3);
	auto* defaultProject = session->Snapshot()->GetDefaultProject(boxURI);
	assert::Assert(t, defaultProject != nullptr,
	               "expected a default project for the loose app.box");
	assert::Equal(t, defaultProject->Kind, project::KindInferred);
	auto* boxFile = languageService->GetProgram()->GetSourceFile(
	    "/home/loose/app.box");
	assert::Assert(t, boxFile != nullptr,
	               "expected loose app.box in the inferred project");
	assert::Assert(
	    t, boxFile->ContentMapper() != "",
	    "expected loose app.box to use the extension contribution");
	assert::Assert(t,
	               boxFile->Text().find("#{target}") == std::string::npos, gostd::sprintf("expected loose app.box to be transformed: %q", {boxFile->Text()}));
	delete languageService;
}

void TestContentMapperInferredProjectSurvivesTypingsInstall(T* t) {
	t->Parallel();
	// A loose content-mapped file lands in the inferred project with an
	// extension content mapper. When ATA finishes installing typings,
	// the inferred program rebuilds with the typings-augmented command
	// line; if that command line drops the content mappers, the
	// otherwise unsupported root file is parsed as plain TypeScript with
	// an unknown script kind and the server panics.
	projecttestutil::FileMap files{
	    {"/home/configured/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler" },
			"contentMappers": [ { "package": "mapper", "extensions": [".box"] } ]
		})")},
	    {"/home/configured/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::TransformingMapper)},
	    {"/home/configured/main.ts",
	     std::string("export const main = true;\n")},
	    {"/home/loose/app.box",
	     std::string("export const version = #{target};\n")},
	    {"/home/package.json",
	     std::string(
	         "{\"name\":\"loose\",\"dependencies\":{\"jquery\":\"^3.1.0\"}}")},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.LoggingEnabled = true;
	options.RunExternalCode = true;
	auto tiOptions =
	    std::make_shared<projecttestutil::TypingsInstallerOptions>();
	tiOptions->PackageToFile = {
	    {"jquery", "declare const $: { x: number }"},
	};
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, tiOptions);
	std::shared_ptr<contentmapper::Spawner> spawner(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	auto ctx = t->Context();
	auto* mapper = new contentmapper::Mapper();
	mapper->Definition.Package = "test.extension";
	mapper->Definition.Extensions = {".box"};
	mapper->Manifest.Name = "mapper";
	mapper->Manifest.Version = "1.0.0";
	mapper->Manifest.Exec = {
	    std::string(contentmappertest::TransformingMapper)};
	mapper->Manifest.CompilerOptions =
	    contentmappertest::DeclaredOptions;
	mapper->PackageDirectory = "/home";
	mapper->ContributionID = "test.extension[0]";
	project::ContentMapperContributions contributions;
	contributions.Mappers = {mapper};
	contributions.Extensions = {".box"};
	session->SetContentMapperContributions(ctx, contributions, {});
	lsproto::DocumentUri configuredURI =
	    "file:///home/configured/main.ts";
	session->DidOpenFile(ctx, configuredURI, 1,
	                     cmFileText(files, "/home/configured/main.ts"),
	                     lsproto::LanguageKindTypeScript);
	auto [ls1, err] = session->GetLanguageService(ctx, configuredURI);
	assert::NilError(t, err);
	delete ls1;

	lsproto::DocumentUri boxURI = "file:///home/loose/app.box";
	session->DidOpenFile(ctx, boxURI, 1,
	                     cmFileText(files, "/home/loose/app.box"),
	                     lsproto::LanguageKind("box"));
	auto [ls2, err2] = session->GetLanguageService(ctx, boxURI);
	assert::NilError(t, err2);
	delete ls2;
	assert::Equal(
	    t, session->Snapshot()->GetDefaultProject(boxURI)->Kind,
	    project::KindInferred);

	// Let ATA install the typings in the background.
	session->WaitForBackgroundTasks();
	assert::Assert(
	    t, !utils->NpmExecutor()->NpmInstallCalls().empty(),
	    "expected ATA to install typings");

	// Applying the typings change rebuilds the inferred program with
	// the typings-augmented command line. The content mappers must
	// survive that rebuild.
	auto [languageService, err3] =
	    session->GetLanguageService(ctx, boxURI);
	assert::NilError(t, err3);
	auto* boxFile = languageService->GetProgram()->GetSourceFile(
	    "/home/loose/app.box");
	assert::Assert(
	    t, boxFile != nullptr,
	    "expected loose app.box in the inferred project after typings "
	    "install");
	assert::Assert(
	    t, boxFile->ContentMapper() != "",
	    "expected loose app.box to keep its content mapper after "
	    "typings install");
	assert::Assert(
	    t, boxFile->Text().find("#{target}") == std::string::npos, gostd::sprintf("expected loose app.box to be transformed after typings "
	    "install: %q", {boxFile->Text()}));;
	tsc::SourceFile* typingsFile = nullptr;
	for (auto* file : languageService->GetProgram()->SourceFiles()) {
		if (file->FileName().ends_with("@types/jquery/index.d.ts")) {
			typingsFile = file;
			break;
		}
	}
	assert::Assert(
	    t, typingsFile != nullptr,
	    "expected installed typings in the inferred program (the "
	    "typings-augmented rebuild did not happen)");
	delete languageService;
}

void TestContentMapperCreatedFileAdoptedByConfiguredProject(T* t) {
	t->Parallel();
	// A content-mapped file created while the server is running must be
	// adopted by the configured project: the created-file root matching
	// has to account for the content mapper extensions, otherwise the
	// file falls into the inferred project until a full project reload.
	projecttestutil::FileMap files{
	    {"/home/project/tsconfig.json",
	     std::string(R"({
			"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler" },
			"contentMappers": [ { "package": "mapper", "extensions": [".box"] } ]
		})")},
	    {"/home/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(
	         contentmappertest::TransformingMapper)},
	    {"/home/project/main.ts",
	     std::string("export const main = true;\n")},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home/project";
	options.DefaultLibraryPath = tsc::bundled::LibPath();
	options.TypingsLocation =
	    std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto [init, utils] =
	    projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	std::shared_ptr<contentmapper::Spawner> spawner(
	    contentmappertest::NewSpawner());
	init->Spawner = spawner.get();
	init->KeepAlive.push_back(spawner);
	auto* session = project::NewSession(init.get());
	t->Cleanup([session] { session->Close(); });

	auto ctx = t->Context();
	lsproto::DocumentUri mainURI = "file:///home/project/main.ts";
	session->DidOpenFile(ctx, mainURI, 1,
	                     cmFileText(files, "/home/project/main.ts"),
	                     lsproto::LanguageKindTypeScript);
	auto [ls1, err] = session->GetLanguageService(ctx, mainURI);
	assert::NilError(t, err);
	delete ls1;

	auto werr = utils->FS()->WriteFile(
	           "/home/project/new.box",
	           "export const version = #{target};\n");
	assert::Assert(t, werr.impl() == nullptr);
	session->DidChangeWatchedFiles(
	    ctx, {cmFileEvent("file:///home/project/new.box",
	                      lsproto::FileChangeTypeCreated)});
	lsproto::DocumentUri boxURI = "file:///home/project/new.box";
	session->DidOpenFile(ctx, boxURI, 1,
	                     "export const version = #{target};\n",
	                     lsproto::LanguageKind("box"));
	auto [languageService, err2] =
	    session->GetLanguageService(ctx, boxURI);
	assert::NilError(t, err2);
	auto* defaultProject = session->Snapshot()->GetDefaultProject(boxURI);
	assert::Assert(t, defaultProject != nullptr,
	               "expected a default project for the created new.box");
	assert::Equal(t, defaultProject->Kind, project::KindConfigured);
	auto* boxFile = languageService->GetProgram()->GetSourceFile(
	    "/home/project/new.box");
	assert::Assert(t, boxFile != nullptr,
	               "expected new.box in the configured project");
	assert::Assert(t,
	               boxFile->Text().find("#{target}") == std::string::npos, gostd::sprintf("expected new.box to be transformed: %q", {boxFile->Text()}));
	delete languageService;
}

REGISTER_UNIT_TEST("project.TestContentMapperProjectWithoutMappedFiles",
                   TestContentMapperProjectWithoutMappedFiles);
REGISTER_UNIT_TEST("project.TestContentMapperParallelFileLoading",
                   TestContentMapperParallelFileLoading);
REGISTER_UNIT_TEST("project.TestContentMapperInProject",
                   TestContentMapperInProject);
REGISTER_UNIT_TEST("project.TestContentMapperPackageManifestChangeReloadsConfig",
                   TestContentMapperPackageManifestChangeReloadsConfig);
REGISTER_UNIT_TEST("project.TestContentMapperSupplementalFileClonedOnEdit",
                   TestContentMapperSupplementalFileClonedOnEdit);
REGISTER_UNIT_TEST("project.TestContentMapperModuleExtensionClonedOnUnrelatedEdit",
                   TestContentMapperModuleExtensionClonedOnUnrelatedEdit);
REGISTER_UNIT_TEST("project.TestContentMapperLocaleChange",
                   TestContentMapperLocaleChange);
REGISTER_UNIT_TEST("project.TestDynamicContentMapperInProject",
                   TestDynamicContentMapperInProject);
REGISTER_UNIT_TEST(
    "project.TestDynamicContentMapperRefreshesForMixedWatchBatches",
    TestDynamicContentMapperRefreshesForMixedWatchBatches);
REGISTER_UNIT_TEST("project.TestUnusedDynamicContentMapperIsNotOpened",
                   TestUnusedDynamicContentMapperIsNotOpened);
REGISTER_UNIT_TEST("project.TestContentMappersInParallelProjectReferences",
                   TestContentMappersInParallelProjectReferences);
REGISTER_UNIT_TEST("project.TestContentMapperOpenFileExcludedByConfigChange",
                   TestContentMapperOpenFileExcludedByConfigChange);
REGISTER_UNIT_TEST("project.TestContentMapperRemovalWithOpenFile",
                   TestContentMapperRemovalWithOpenFile);
REGISTER_UNIT_TEST("project.TestContentMapperProcessSharedAcrossProjects",
                   TestContentMapperProcessSharedAcrossProjects);
REGISTER_UNIT_TEST(
    "project.TestContentMapperInferredProjectUsesExtensionContributions",
    TestContentMapperInferredProjectUsesExtensionContributions);
REGISTER_UNIT_TEST(
    "project.TestContentMapperInferredProjectSurvivesTypingsInstall",
    TestContentMapperInferredProjectSurvivesTypingsInstall);
REGISTER_UNIT_TEST(
    "project.TestContentMapperCreatedFileAdoptedByConfiguredProject",
    TestContentMapperCreatedFileAdoptedByConfiguredProject);

}  // namespace
