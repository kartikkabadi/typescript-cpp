// Port of tsc/internal/project/refcountcache_test.go.
#include <future>
#include <memory>
#include <thread>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/bundled/bundled.h"
#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/parser/parser.h"
#include "internal/project/extendedconfigcache.h"
#include "internal/project/overlayfs.h"
#include "internal/project/parsecache.h"
#include "internal/project/project.h"
#include "internal/project/projectcollection.h"
#include "internal/project/refcountcache.h"
#include "internal/project/session.h"
#include "internal/project/sessiontypes.h"
#include "internal/project/snapshot.h"
#include "internal/project/snapshothost.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfstest/vfstest.h"
#include "internal/xxh3/xxh3.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace lsproto = tsc::lsp::lsproto;
namespace project = tsc::project;
namespace projecttestutil = tsc::testutil::projecttestutil;
using tsc::SourceFile;
using tsc::SourceFileParseOptions;
using tsc::gostd::testing::T;

void TestContentMappedParseCacheBundleLifetime(T* t) {
	t->Parallel();
	auto* cache =
	    project::newContentMappedParseCache(project::RefCountCacheOptions{});
	project::ContentMappedParseCacheKey key{
	    .sourceFileParseOptions =
	        {
	            .FileName = "/component.vue",
	            .Path = "/component.vue",
	        },
	};
	auto* canonical = new SourceFile();
	auto* supplemental = new SourceFile();
	tsc::contentmapper::SourceFiles produced{
	    .Canonical = canonical,
	    .Supplemental = {supplemental},
	};

	// The cache owns the complete transform result as one value, so reuse
	// preserves every file's identity.
	auto [acquired, err] = cache->AcquireOrError(
	    key,
	    [&]() -> std::pair<tsc::contentmapper::SourceFiles, tsc::gostd::Error> {
		    return {produced, nullptr};
	    });
	assert::NilError(t, err);
	assert::Assert(t, acquired.Canonical == canonical);
	assert::Assert(t, acquired.Supplemental[0] == supplemental);
	auto [reused, err2] = cache->AcquireOrError(
	    key,
	    [&]() -> std::pair<tsc::contentmapper::SourceFiles, tsc::gostd::Error> {
		    tsc::tscUnreachable("cached bundle should be reused");
	    });
	assert::NilError(t, err2);
	assert::Assert(t, reused.Canonical == canonical);
	assert::Assert(t, reused.Supplemental[0] == supplemental);

	// Canonical and supplemental files share the bundle's refcount and
	// disappear after its final release.
	cache->Deref(key);
	assert::Assert(t, cache->Has(key));
	cache->Deref(key);
	assert::Assert(t, !cache->Has(key));
}

void TestContentMappedParseCacheKeyReconstruction(T* t) {
	t->Parallel();
	SourceFileParseOptions acquireOptions{.FileName = "/component.box",
	                                      .Path = "/component.box"};
	auto mappedOptions = acquireOptions;
	mappedOptions.ExternalModuleIndicatorOptions.Force = true;
	auto hash = tsc::xxh3::hash128("cache key");
	auto* file = tsc::parseSourceFile(mappedOptions, "export {};",
	                                tsc::ScriptKind::TS);
	file->Hash = tsc::Uint128{.lo = hash.Lo, .hi = hash.Hi};
	tsc::ContentMapperSourceFileInfo mapperInfo;
	mapperInfo.ContentMapper = "mapper";
	mapperInfo.ParseOptions = acquireOptions;
	file->SetContentMapperInfo(mapperInfo);
	project::ContentMappedParseCacheKey expected{
	    .sourceFileParseOptions = acquireOptions,
	    .hash = hash,
	};
	assert::DeepEqual(t, project::contentMappedParseCacheKeyForFile(file),
	                  expected);

	tsc::compiler::DuplicateSourceFile duplicate{
	    .ParseOptions = mappedOptions,
	    .ContentMapperParseOptions = acquireOptions,
	    .Hash = hash,
	    .ContentMapper = "mapper",
	};
	assert::DeepEqual(
	    t, project::contentMappedParseCacheKeyForDuplicate(&duplicate),
	    expected);
}

void TestParseCacheBindsBeforePublishing(T* t) {
	t->Parallel();

	const std::string fileName = "/index.js";
	auto* fileHandle = project::newOverlay(
	    fileName, "module.exports = 0;", 1, tsc::ScriptKind::JS);
	SourceFileParseOptions parseOptions{
	    .FileName = fileName,
	    .Path = std::string(tsc::tspath::Path(fileName)),
	};
	auto key = project::newParseCacheKey(parseOptions, fileHandle->Hash(),
	                                     fileHandle->Kind());
	auto* cache = project::newParseCache(project::RefCountCacheOptions{});

	auto* file = cache->Acquire(key, fileHandle);
	// Cleanup runs at ~T after this function returns — capture by value.
	t->Cleanup([cache, key] { cache->Deref(key); });

	assert::Assert(t, file->IsBound());
	assert::Assert(t, file->CommonJSModuleIndicator != nullptr);
}

project::Session* setupRefCountSession(
    const projecttestutil::FileMap& files) {
	auto fs =
	    tsc::bundled::WrapFS(tsc::vfs::vfstest::FromMap(files, false));
	// The session holds init->FS.get() only — the shared_ptr must outlive
	// it (Go relies on GC here).
	static std::vector<std::shared_ptr<tsc::vfs::FS>> keepAlive;
	keepAlive.push_back(fs);
	project::SessionInit init;
	init.BackgroundCtx = tsc::gostd::contextBackground();
	// The session retains this pointer; one fresh options per session.
	auto* options = new project::SessionOptions();
	options->CurrentDirectory = "/";
	options->DefaultLibraryPath = tsc::bundled::LibPath();
	options->TypingsLocation = "/home/src/Library/Caches/typescript";
	options->PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options->WatchEnabled = false;
	options->LoggingEnabled = false;
	init.Options = options;
	init.FS = fs;
	return project::NewSession(&init);
}

std::string mapFileText(const projecttestutil::FileMap& files,
                        const std::string& path) {
	return std::get<std::string>(files.at(path));
}

project::ParseCacheKey fileKey(const SourceFile* f) {
	return project::newParseCacheKey(f->ParseOptions(),
	                                 {f->Hash.hi, f->Hash.lo}, f->ScriptKind);
}

void TestRefCountingCaches(T* t) {
	t->Parallel();

	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto ctx = tsc::gostd::contextBackground();

	t->Run("parseCache", [&](T* t) {
		t->Parallel();

		projecttestutil::FileMap files{
		    {"/user/username/projects/myproject/src/main.ts",
		     std::string("const x = 1;")},
		    {"/user/username/projects/myproject/src/utils.ts",
		     std::string("export function util() {}")},
		};

		t->Run("reuse unchanged file", [&](T* t) {
			t->Parallel();

			auto* session = setupRefCountSession(files);
			session->DidOpenFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/main.ts", 1,
			    mapFileText(
			        files,
			        "/user/username/projects/myproject/src/main.ts"),
			    lsproto::LanguageKindTypeScript);
			session->DidOpenFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/utils.ts", 1,
			    mapFileText(
			        files,
			        "/user/username/projects/myproject/src/utils.ts"),
			    lsproto::LanguageKindTypeScript);
			auto* snapshot = session->Snapshot();
			auto* program = snapshot->ProjectCollection->InferredProject()
			                    ->Program;
			auto* main = program->GetSourceFile(
			    "/user/username/projects/myproject/src/main.ts");
			auto* utils = program->GetSourceFile(
			    "/user/username/projects/myproject/src/utils.ts");
			auto [mainEntry, _1] =
			    session->snapshotHost->parseCache->entries.Load(
			        fileKey(main));
			auto [utilsEntry, _2] =
			    session->snapshotHost->parseCache->entries.Load(
			        fileKey(utils));
			assert::Equal(t, mainEntry->refCount, 1);
			assert::Equal(t, utilsEntry->refCount, 1);

			auto partial =
			    std::make_shared<lsproto::TextDocumentContentChangePartial>();
			partial->Range = lsproto::Range{
			    .Start = lsproto::Position{.Line = 0, .Character = 0},
			    .End = lsproto::Position{.Line = 0, .Character = 12}};
			partial->Text = "const x = 2;";
			session->DidChangeFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/main.ts", 2,
			    {lsproto::
			         TextDocumentContentChangePartialOrWholeDocument{
			             .Partial = partial}});
			auto [ls, err] = session->GetLanguageService(
			    ctx,
			    "file:///user/username/projects/myproject/src/main.ts");
			assert::NilError(t, err);
			session->WaitForBackgroundTasks();
			auto* newMain = ls->GetProgram()->GetSourceFile(
			    "/user/username/projects/myproject/src/main.ts");
			auto [newMainEntry, _3] =
			    session->snapshotHost->parseCache->entries.Load(
			        fileKey(newMain));
			assert::Assert(t, newMain != main);
			assert::Assert(t, newMainEntry != mainEntry);
			assert::Equal(
			    t,
			    ls->GetProgram()->GetSourceFile(
			        "/user/username/projects/myproject/src/utils.ts"),
			    utils);
			// Old snapshot is deref'd immediately when replaced by
			// UpdateSnapshot, so old mainEntry is already disposed and
			// utils refCount is already 1.
			assert::Equal(t, mainEntry->refCount, 0);
			assert::Equal(t, newMainEntry->refCount, 1);
			assert::Equal(t, utilsEntry->refCount, 1);
			delete ls;
		});

		t->Run("release file on close", [&](T* t) {
			t->Parallel();

			auto* session = setupRefCountSession(files);
			session->DidOpenFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/main.ts", 1,
			    mapFileText(
			        files,
			        "/user/username/projects/myproject/src/main.ts"),
			    lsproto::LanguageKindTypeScript);
			session->DidOpenFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/utils.ts", 1,
			    mapFileText(
			        files,
			        "/user/username/projects/myproject/src/utils.ts"),
			    lsproto::LanguageKindTypeScript);
			auto* snapshot = session->Snapshot();
			auto* program = snapshot->ProjectCollection->InferredProject()
			                    ->Program;
			auto* main = program->GetSourceFile(
			    "/user/username/projects/myproject/src/main.ts");
			auto* utils = program->GetSourceFile(
			    "/user/username/projects/myproject/src/utils.ts");
			auto mainKey = fileKey(main);
			auto [mainEntry, _1] =
			    session->snapshotHost->parseCache->entries.Load(mainKey);
			auto [utilsEntry, _2] =
			    session->snapshotHost->parseCache->entries.Load(
			        fileKey(utils));
			assert::Equal(t, mainEntry->refCount, 1);
			assert::Equal(t, utilsEntry->refCount, 1);

			session->DidCloseFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/main.ts");
			auto [_ls, err] = session->GetLanguageService(
			    ctx,
			    "file:///user/username/projects/myproject/src/utils.ts");
			assert::NilError(t, err);
			delete _ls;
			session->WaitForBackgroundTasks();
			assert::Equal(t, utilsEntry->refCount, 1);
			assert::Equal(t, mainEntry->refCount, 0);
			auto [mainEntry2, ok] =
			    session->snapshotHost->parseCache->entries.Load(mainKey);
			assert::Equal(t, ok, false);
		});

		t->Run("unchanged program does not over-ref", [&](T* t) {
			t->Parallel();

			// When a program is reused across snapshots without changes, we
			// should not accumulate extra refs. The ref count should stay at
			// 1 per source file until the program is finally disposed.
			auto* session = setupRefCountSession(files);
			session->DidOpenFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/main.ts", 1,
			    mapFileText(
			        files,
			        "/user/username/projects/myproject/src/main.ts"),
			    lsproto::LanguageKindTypeScript);
			session->DidOpenFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/utils.ts", 1,
			    mapFileText(
			        files,
			        "/user/username/projects/myproject/src/utils.ts"),
			    lsproto::LanguageKindTypeScript);

			// Get first snapshot and capture the program/entries
			auto* snapshot1 = session->Snapshot();
			auto* program1 = snapshot1->ProjectCollection->InferredProject()
			                     ->Program;
			auto* main = program1->GetSourceFile(
			    "/user/username/projects/myproject/src/main.ts");
			auto mainKey = fileKey(main);
			auto [mainEntry, _1] =
			    session->snapshotHost->parseCache->entries.Load(mainKey);
			assert::Equal(t, mainEntry->refCount, 1,
			              "initial refCount should be 1");

			// Change utils.ts to trigger a new snapshot, but main.ts stays
			// the same so main's source file should be reused.
			auto partial =
			    std::make_shared<lsproto::TextDocumentContentChangePartial>();
			partial->Range = lsproto::Range{
			    .Start = lsproto::Position{.Line = 0, .Character = 0},
			    .End = lsproto::Position{.Line = 0, .Character = 25}};
			partial->Text = "export function util2() {}";
			session->DidChangeFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/utils.ts", 2,
			    {lsproto::
			         TextDocumentContentChangePartialOrWholeDocument{
			             .Partial = partial}});

			// Get second snapshot - main.ts should be reused (program is
			// new but shares source files)
			auto [ls, err] = session->GetLanguageService(
			    ctx,
			    "file:///user/username/projects/myproject/src/main.ts");
			assert::NilError(t, err);
			session->WaitForBackgroundTasks();
			auto* program2 = ls->GetProgram();
			auto* main2 = program2->GetSourceFile(
			    "/user/username/projects/myproject/src/main.ts");
			assert::Equal(t, main, main2,
			              "main.ts source file should be reused");

			// main.ts refCount should be 1: the old snapshot was
			// immediately deref'd when replaced, so only the new snapshot
			// holds a ref.
			auto [mainEntry2, _2] =
			    session->snapshotHost->parseCache->entries.Load(mainKey);
			assert::Equal(t, mainEntry2->refCount, 1,
			              "refCount should be 1 (only new snapshot)");
			// Go relies on GC for the language service; it never refs the
			// snapshot. In C++ the LS host refs it, so release it here.
			delete ls;

			// Close files to trigger cleanup
			session->DidCloseFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/main.ts");
			session->DidCloseFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/utils.ts");
			session->DidOpenFile(ctx, "untitled:Untitled-1", 1, "",
			                     lsproto::LanguageKindTypeScript);
			session->WaitForBackgroundTasks();

			// Entry should now be gone (refCount 0, deleted)
			auto [mainEntry3, ok] =
			    session->snapshotHost->parseCache->entries.Load(mainKey);
			if (ok) {
				t->Logf("Entry still exists with refCount=%d",
				        {mainEntry3->refCount});
			}
			assert::Assert(
			    t, !ok,
			    "entry should be deleted after program is disposed");
		});

		t->Run("fallback rebuild does not double-ref changed file",
		       [&](T* t) {
			       t->Parallel();

			       projecttestutil::FileMap testFiles{
			           {"/user/username/projects/myproject/src/main.ts",
			            std::string("const x = 1;")},
			           {"/user/username/projects/myproject/src/utils.ts",
			            std::string("export const util = 1;")},
			       };
			       auto* session = setupRefCountSession(testFiles);
			       const lsproto::DocumentUri mainURI =
			           "file:///user/username/projects/myproject/src/main.ts";
			       session->DidOpenFile(
			           ctx, mainURI, 1,
			           mapFileText(
			               testFiles,
			               "/user/username/projects/myproject/src/main.ts"),
			           lsproto::LanguageKindTypeScript);

			       auto [_ls, err] =
			           session->GetLanguageService(ctx, mainURI);
			       assert::NilError(t, err);

		       delete _ls;
			       auto wholeDoc = std::make_shared<
			           lsproto::TextDocumentContentChangeWholeDocument>();
			       wholeDoc->Text =
			           "import { util } from \"./utils\";\nconst x = util;";
			       session->DidChangeFile(
			           ctx, mainURI, 2,
			           {lsproto::
			                TextDocumentContentChangePartialOrWholeDocument{
			                    .WholeDocument = wholeDoc}});

			       auto [lsAfter, err2] =
			           session->GetLanguageService(ctx, mainURI);
			       assert::NilError(t, err2);
			       session->WaitForBackgroundTasks();

			       auto* proj = session->Snapshot()
			                        ->ProjectCollection->InferredProject();
			       assert::Assert(t, proj != nullptr);
			       assert::Equal(t, proj->ProgramUpdateKind,
			                     project::ProgramUpdateKindNewFiles);

			       auto* main = lsAfter->GetProgram()->GetSourceFile(
			           "/user/username/projects/myproject/src/main.ts");
			       auto mainKey = fileKey(main);
			       auto [mainEntry, ok] =
			           session->snapshotHost->parseCache->entries.Load(
			               mainKey);
			       assert::Assert(t, ok);
			       assert::Equal(t, mainEntry->refCount, 1);
		       delete lsAfter;

			       session->DidCloseFile(ctx, mainURI);
			       session->DidOpenFile(ctx, "untitled:Untitled-1",
			                            1, "",
			                            lsproto::LanguageKindTypeScript);
			       session->WaitForBackgroundTasks();

			       auto [_e, ok2] =
			           session->snapshotHost->parseCache->entries.Load(
			               mainKey);
			       assert::Assert(t, !ok2);
		       });

		t->Run("case-only duplicate loads are released on dispose",
		       [&](T* t) {
			       t->Parallel();

			       projecttestutil::FileMap testFiles{
			           {"/user/username/projects/myproject/src/main.ts",
			            std::string(
			                "import { util as a } from \"./utils\";\nimport "
			                "{ util as b } from \"./UTILS\";\nconst x = a + "
			                "b;")},
			           {"/user/username/projects/myproject/src/utils.ts",
			            std::string("export const util = 1;")},
			       };
			       auto* session = setupRefCountSession(testFiles);
			       const lsproto::DocumentUri mainURI =
			           "file:///user/username/projects/myproject/src/main.ts";
			       session->DidOpenFile(
			           ctx, mainURI, 1,
			           mapFileText(
			               testFiles,
			               "/user/username/projects/myproject/src/main.ts"),
			           lsproto::LanguageKindTypeScript);

			       auto [ls, err] =
			           session->GetLanguageService(ctx, mainURI);
			       assert::NilError(t, err);

			       int projectEntries = 0;
			       session->snapshotHost->parseCache->entries.Range(
			           [&](const project::ParseCacheKey& key,
			               const std::shared_ptr<project::refCountCacheEntry<
			                                     SourceFile*>>&)
			               -> bool {
				       if (key.sourceFileParseOptions.FileName
				               .starts_with(
				                   "/user/username/projects/myproject/src/")) {
					       projectEntries++;
				       }
				       return true;
			           });
			       assert::Equal(t, projectEntries, 3);

			       auto* utils = ls->GetProgram()->GetSourceFile(
			           "/user/username/projects/myproject/src/utils.ts");
			       assert::Assert(t, utils != nullptr);
			       // Release the language service's snapshot
			       // ref (Go GC never pins it).
			       delete ls;

			       session->DidCloseFile(ctx, mainURI);
			       session->DidOpenFile(ctx, "untitled:Untitled-1",
			                            1, "",
			                            lsproto::LanguageKindTypeScript);
			       session->WaitForBackgroundTasks();
			       projectEntries = 0;
			       session->snapshotHost->parseCache->entries.Range(
			           [&](const project::ParseCacheKey& key,
			               const std::shared_ptr<project::refCountCacheEntry<
			                                     SourceFile*>>&)
			               -> bool {
				       if (key.sourceFileParseOptions.FileName
				               .starts_with(
				                   "/user/username/projects/myproject/src/")) {
					       projectEntries++;
				       }
				       return true;
			           });
		       assert::Equal(t, projectEntries, 0);
		       });

		t->Run(
		    "case-only duplicate imported from multiple files is "
		    "refcounted once",
		    [&](T* t) {
			    t->Parallel();

			    // A file reached through a case-only-different file name
			    // from more than one import site is parsed and acquired in
			    // the parse cache exactly once (same-casing loads dedupe),
			    // but it must also be recorded as a duplicate exactly once.
			    // Recording it once per import site would release it from
			    // the parse cache more times than it was acquired,
			    // deleting the live entry out from under a program that
			    // still references it and panicking the next time it is
			    // ref'd during a clone.
			    projecttestutil::FileMap testFiles{
			        // entry.ts imports the canonical casing first, then
			        // pulls in a.ts and b.ts, which both import the same
			        // file through an upper-cased name.
			        {"/user/username/projects/myproject/src/entry.ts",
			         std::string(
			             "import { dep } from './sub/dep';\nimport "
			             "'./a';\nimport './b';\nexport const e = dep;")},
			        {"/user/username/projects/myproject/src/a.ts",
			         std::string(
			             "import { dep } from './sub/DEP';\nexport const a "
			             "= dep;")},
			        {"/user/username/projects/myproject/src/b.ts",
			         std::string(
			             "import { dep } from './sub/DEP';\nexport const b "
			             "= dep;")},
			        {"/user/username/projects/myproject/src/sub/dep.ts",
			         std::string("export const dep = 1;")},
			        {"/user/username/projects/myproject/src/c.ts",
			         std::string("export const c = 1;")},
			    };
			    auto* session = setupRefCountSession(testFiles);
			    const lsproto::DocumentUri entryURI =
			        "file:///user/username/projects/myproject/src/entry.ts";
			    session->DidOpenFile(
			        ctx, entryURI, 1,
			        mapFileText(
			            testFiles,
			            "/user/username/projects/myproject/src/entry.ts"),
			        lsproto::LanguageKindTypeScript);

			    auto [ls, err] =
			        session->GetLanguageService(ctx, entryURI);
			    assert::NilError(t, err);

			    // The upper-cased name is recorded as a duplicate, and it
			    // should appear exactly once.
			    auto* program = ls->GetProgram();
			    std::vector<project::ParseCacheKey> dupKeys;
			    for (auto* dup : program->DuplicateSourceFiles()) {
				    if (std::string(dup->ParseOptions.FileName)
				            .ends_with("/sub/DEP.ts")) {
					    dupKeys.push_back(project::newParseCacheKey(
					        dup->ParseOptions, dup->Hash,
					        dup->ScriptKind));
				    }
			    }
			    assert::Equal(t, dupKeys.size(), size_t(1),
			                  "case-only duplicate should be recorded "
			                  "exactly once");
			    auto [dupEntry, ok] =
			        session->snapshotHost->parseCache->entries.Load(
			            dupKeys[0]);
			    assert::Assert(t, ok,
			                   "duplicate entry should exist in the parse "
			                   "cache");
		    assert::Equal(t, dupEntry->refCount, 1);
		    // Release the language service's snapshot ref.
		    delete ls;

			    // Force a full program rebuild (adding an import changes
			    // the file's module structure). The old snapshot is
			    // disposed, releasing each of its source and duplicate
			    // files exactly once. If the duplicate were recorded
			    // twice, the shared cache entry would be released to zero
			    // and deleted here even though the new program still
			    // references it.
			    auto wholeDoc = std::make_shared<
			        lsproto::TextDocumentContentChangeWholeDocument>();
			    wholeDoc->Text =
			        "import { dep } from \"./sub/dep\";\nimport "
			        "\"./a\";\nimport \"./b\";\nimport \"./c\";\nexport "
			        "const e = dep;";
			    session->DidChangeFile(
			        ctx, entryURI, 2,
			        {lsproto::
			             TextDocumentContentChangePartialOrWholeDocument{
			                 .WholeDocument = wholeDoc}});
			    auto [lsAfterRebuild, err2] =
			        session->GetLanguageService(ctx, entryURI);
			    assert::NilError(t, err2);
			    session->WaitForBackgroundTasks();

			    // Every parse-cache key referenced by the live program
			    // must still exist.
			    auto* rebuiltProgram = lsAfterRebuild->GetProgram();
			    auto assertKeyAlive =
			        [&](const project::ParseCacheKey& key) {
				        auto [_e, alive] = session->snapshotHost
				                               ->parseCache->entries.Load(key);
				        assert::Assert(
				            t, alive,
				            "live program references a deleted "
				            "parse-cache entry: " +
				                key.sourceFileParseOptions.FileName);
			        };
			    for (auto* file : rebuiltProgram->SourceFiles()) {
				    assertKeyAlive(fileKey(file));
			    }
			    for (auto* dup : rebuiltProgram->DuplicateSourceFiles()) {
				    assertKeyAlive(project::newParseCacheKey(
				        dup->ParseOptions, dup->Hash, dup->ScriptKind));
			    }
		    delete lsAfterRebuild;

			    // An incremental (clone) update re-references the
			    // duplicate files; this must not panic with "cache entry
			    // not found".
			    auto wholeDoc2 = std::make_shared<
			        lsproto::TextDocumentContentChangeWholeDocument>();
			    wholeDoc2->Text =
			        "import { dep } from './sub/dep';\nimport "
			        "'./a';\nimport './b';\nimport './c';\nexport const e "
			        "= dep + 0;";
			    session->DidChangeFile(
			        ctx, entryURI, 3,
			        {lsproto::
			             TextDocumentContentChangePartialOrWholeDocument{
			                 .WholeDocument = wholeDoc2}});
			    auto [_ls2, err3] =
			        session->GetLanguageService(ctx, entryURI);
			    assert::NilError(t, err3);
		    delete _ls2;
			    session->WaitForBackgroundTasks();

			    // Closing the project releases everything cleanly.
			    // (The configured project is not disposed until another
			    // file in another project is opened, so we open an
			    // untitled file to trigger that.)
			    session->DidCloseFile(ctx, entryURI);
			    session->DidOpenFile(ctx, "untitled:Untitled-1", 1, "",
			                         lsproto::LanguageKindTypeScript);
			    session->WaitForBackgroundTasks();

			    int projectEntries = 0;
			    session->snapshotHost->parseCache->entries.Range(
			        [&](const project::ParseCacheKey& key,
			            const std::shared_ptr<project::refCountCacheEntry<
			                                  SourceFile*>>&)
			            -> bool {
				    if (key.sourceFileParseOptions.FileName
				            .starts_with(
				                "/user/username/projects/myproject/src/")) {
					    projectEntries++;
				    }
				    return true;
			        });
		    assert::Equal(t, projectEntries, 0);
		    });
	});

	t->Run("extendedConfigCache", [&](T* t) {
		projecttestutil::FileMap files{
		    {"/user/username/projects/myproject/tsconfig.json",
		     std::string("{\n\t\t\t\t\"extends\": "
		                 "\"./tsconfig.base.json\"\n\t\t\t}")},
		    {"/user/username/projects/myproject/tsconfig.base.json",
		     std::string("{\n\t\t\t\t\"compilerOptions\": {}\n\t\t\t}")},
		    {"/user/username/projects/myproject/src/main.ts",
		     std::string("const x = 1;")},
		};

		t->Run("release extended configs with project close", [&](T* t) {
			t->Parallel();

			auto* session = setupRefCountSession(files);
			session->DidOpenFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/main.ts", 1,
			    mapFileText(
			        files,
			        "/user/username/projects/myproject/src/main.ts"),
			    lsproto::LanguageKindTypeScript);
			auto* snapshot = session->Snapshot();
			auto* config = snapshot->ConfigFileRegistry->GetConfig(
			    tsc::tspath::Path(
			        "/user/username/projects/myproject/tsconfig.json"));
			assert::Equal(
			    t, config->ExtendedSourceFiles()[0],
			    std::string(
			        "/user/username/projects/myproject/"
			        "tsconfig.base.json"));
			auto [extendedConfigEntry, _1] = session->snapshotHost
			                                     ->extendedConfigCache
			                                     ->entries.Load(
			                                         tsc::tspath::Path(
			                                             "/user/username/"
			                                             "projects/"
			                                             "myproject/"
			                                             "tsconfig.base."
			                                             "json"));
			assert::Equal(t, extendedConfigEntry->owners.size(),
			              size_t(1));

			session->DidCloseFile(
			    ctx,
			    "file:///user/username/projects/myproject/src/main.ts");
			session->DidOpenFile(ctx, "untitled:Untitled-1", 1, "",
			                     lsproto::LanguageKindTypeScript);
			session->WaitForBackgroundTasks();
			auto [_e, ok] = session->snapshotHost->extendedConfigCache
			                    ->entries.Load(tsc::tspath::Path(
			                        "/user/username/projects/myproject/"
			                        "tsconfig.base.json"));
			assert::Equal(t, ok, false);
		});

		t->Run("release cache entries for unretained clone", [&](T* t) {
			t->Parallel();

			auto* session = setupRefCountSession(files);
			const lsproto::DocumentUri uri =
			    "file:///user/username/projects/myproject/src/main.ts";
			auto* baseSnapshot = session->Snapshot();
			auto extendedConfigPath = tsc::tspath::Path(
			    "/user/username/projects/myproject/tsconfig.base.json");
			project::SnapshotChange change;
			change.reason = project::
			    UpdateReasonRequestedLanguageServiceProjectNotLoaded;
			change.Documents = {uri};
			auto* clone = baseSnapshot->Clone(ctx, change,
			                                  baseSnapshot->overlays(),
			                                  nullptr, nullptr);

			auto* proj = clone->GetDefaultProject(uri);
			assert::Assert(t, proj != nullptr);
			assert::Equal(t, proj->ProgramLastUpdate, clone->id);

			auto* main = proj->Program->GetSourceFile(
			    "/user/username/projects/myproject/src/main.ts");
			auto mainKey = fileKey(main);
			auto [mainEntry, ok] =
			    session->snapshotHost->parseCache->entries.Load(mainKey);
			assert::Assert(t, ok);
			assert::Equal(t, mainEntry->refCount, 1);

			auto [extendedConfigEntry, ok2] =
			    session->snapshotHost->extendedConfigCache->entries.Load(
			        extendedConfigPath);
			assert::Assert(t, ok2);
			assert::Equal(t, extendedConfigEntry->owners.size(),
			              size_t(1));

			clone->Deref();

			auto [_e1, ok3] =
			    session->snapshotHost->parseCache->entries.Load(mainKey);
			assert::Assert(t, !ok3);

			auto [_e2, ok4] =
			    session->snapshotHost->extendedConfigCache->entries.Load(
			        extendedConfigPath);
			assert::Assert(t, !ok4);
		});

		t->Run(
		    "createProgram retains and reloads extended configs from "
		    "referenced projects",
		    [&](T* t) {
			    t->Parallel();

			    const std::string appConfigPath =
			        "/user/username/projects/app/tsconfig.json";
			    const std::string appFilePath =
			        "/user/username/projects/app/index.ts";
			    const std::string libConfigPath =
			        "/user/username/projects/lib/tsconfig.json";
			    const std::string libBaseConfigPath =
			        "/user/username/projects/lib/tsconfig.base.json";
			    const std::string libFilePath =
			        "/user/username/projects/lib/index.ts";
			    auto* session = setupRefCountSession(
			        {{appConfigPath,
			          std::string("{\"compilerOptions\":{\"noLib\":true},"
			                      "\"files\":[\"index.ts\"],\"references\":"
			                      "[{\"path\":\"../lib\"}]}")},
			         {appFilePath, std::string("export const app = 1;")},
			         {libConfigPath,
			          std::string("{\"extends\":\"./tsconfig.base.json\","
			                      "\"files\":[\"index.ts\"]}")},
			         {libBaseConfigPath,
			          std::string("{\"compilerOptions\":{\"composite\":"
			                      "true,\"noLib\":true}}")},
			         {libFilePath, std::string("export const lib = 1;")}});
			    t->Cleanup([session] { session->Close(); });

			    auto openSet =
			        tsc::collections::NewSetFromItems<std::string>(
			            appConfigPath);
			    project::APISnapshotRequest apiRequest;
			    apiRequest.OpenProjects = &openSet;
			    auto [baseSnapshot, err] = session->APIUpdate(
			        ctx, project::FileChangeSummary{}, &apiRequest);
			    assert::NilError(t, err);
			    auto* baseSnapshotForCleanup = baseSnapshot;
			    t->Cleanup([baseSnapshotForCleanup] {
				    baseSnapshotForCleanup->Deref();
			    });
			    auto* appProject =
			        baseSnapshot->ProjectCollection->GetProject(
			            project::ConfiguredProjectID(
			                baseSnapshot->toPath(appConfigPath))
			                .AsID());
			    assert::Assert(t, appProject != nullptr);

			    auto* createRequest =
			        new project::APICreateProgramRequest();
			    createRequest->RootFileNames =
			        appProject->CommandLine->FileNames();
			    createRequest->CompilerOptions =
			        appProject->CommandLine->CompilerOptions();
			    createRequest->ProjectReferences =
			        appProject->CommandLine->ProjectReferences();
			    createRequest->ConfigFileParsingDiagnostics =
			        appProject->CommandLine->Errors;
			    project::APISnapshotRequest createSnapshotReq;
			    createSnapshotReq.CreatePrograms = {createRequest};
			    auto [programSnapshot, err2] =
			        session->snapshotHost->CloneSnapshot(
			            ctx, baseSnapshot, project::FileChangeSummary{},
			            &createSnapshotReq);
			    assert::NilError(t, err2);
			    auto* programSnapshotForCleanup = programSnapshot;
			    t->Cleanup([programSnapshotForCleanup] {
				    programSnapshotForCleanup->Deref();
			    });
			    auto* programProject =
			        programSnapshot->CreatedPrograms()[0];
			    assert::Assert(t, programProject != nullptr);
			    assert::Assert(t,
			                   programProject->Program != appProject->Program);

			    auto [extendedConfigEntry, ok] =
			        session->snapshotHost->extendedConfigCache
			            ->entries.Load(tsc::tspath::Path(libBaseConfigPath));
			    assert::Assert(t, ok);
			    extendedConfigEntry->mu.lock();
			    bool ownedByBaseSnapshot =
			        extendedConfigEntry->owners.count(baseSnapshot->id) !=
			        0;
			    bool ownedByProgramSnapshot =
			        extendedConfigEntry->owners.count(
			            programSnapshot->id) != 0;
			    size_t ownerCount = extendedConfigEntry->owners.size();
			    extendedConfigEntry->mu.unlock();
			    assert::Assert(t, ownedByBaseSnapshot);
			    assert::Assert(t, ownedByProgramSnapshot);
			    assert::Equal(t, ownerCount, size_t(2));

			    assert::Assert(
			        t,
			        !session->fs->WriteFile(
			            libBaseConfigPath,
			            "{\"compilerOptions\":{\"composite\":true,\"noLib\":"
			            "true,\"strict\":true}}"));
			    project::FileChangeSummary fileChanges;
			    fileChanges.Changed.Add(
			        lsproto::DocumentUri("file://" + libBaseConfigPath));
			    auto ensureSet = tsc::collections::NewSetFromItems<
			        project::ID>(programProject->ID());
			    project::APISnapshotRequest updateRequest;
			    updateRequest.EnsurePrograms = &ensureSet;
			    auto [updatedProgramSnapshot, err3] =
			        session->snapshotHost->CloneSnapshot(
			            ctx, programSnapshot, fileChanges, &updateRequest);
			    assert::NilError(t, err3);
			    auto* updatedProgramSnapshotForCleanup =
			        updatedProgramSnapshot;
			    t->Cleanup([updatedProgramSnapshotForCleanup] {
				    updatedProgramSnapshotForCleanup->Deref();
			    });
			    auto* updatedProgramProject =
			        updatedProgramSnapshot->ProjectCollection
			            ->GetProject(programProject->ID());
			    assert::Assert(t, updatedProgramProject != nullptr);
			    assert::Assert(
			        t, updatedProgramProject->Program !=
			               programProject->Program);
			    auto updatedReferences =
			        updatedProgramProject->Program
			            ->GetResolvedProjectReferences();
			    assert::Equal(t, updatedReferences.size(), size_t(1));
			    assert::Equal(
			        t, updatedReferences[0]->CompilerOptions()->Strict,
			        tsc::Tristate::True);
		    });
	});
}

void TestParseCacheAcquireExistingUsesFullKey(T* t) {
	t->Parallel();

	const std::string fileName = "/index.ts";
	auto* fileHandle = project::newCachedFileHandle(fileName, "export {};");
	SourceFileParseOptions parseOptions{
	    .FileName = fileName,
	    .Path = std::string(tsc::tspath::Path(fileName)),
	};
	auto key = project::newParseCacheKey(parseOptions, fileHandle->Hash(),
	                                     tsc::ScriptKind::TS);
	auto* cache = project::newParseCache(project::RefCountCacheOptions{});
	auto* file = cache->Acquire(key, fileHandle);

	auto [acquired, ok] = cache->AcquireExisting(key);
	assert::Assert(t, ok);
	assert::Assert(t, acquired == file);
	cache->Deref(key);

	std::vector<std::pair<std::string, project::ParseCacheKey>> mismatches;
	{
		auto m = key;
		m.sourceFileParseOptions.FileName = "/INDEX.ts";
		mismatches.emplace_back("file name", m);
	}
	{
		auto m = key;
		m.sourceFileParseOptions.Path = "/INDEX.ts";
		mismatches.emplace_back("path", m);
	}
	{
		auto m = key;
		m.hash = tsc::xxh3::hash128("different");
		mismatches.emplace_back("hash", m);
	}
	{
		auto m = key;
		m.scriptKind = tsc::ScriptKind::TSX;
		mismatches.emplace_back("script kind", m);
	}
	{
		auto m = key;
		m.sourceFileParseOptions.ExternalModuleIndicatorOptions.JSX =
		    tsc::JsxEmit::ReactJSX;
		mismatches.emplace_back("jsx parse option", m);
	}
	{
		auto m = key;
		m.sourceFileParseOptions.ExternalModuleIndicatorOptions.Force = true;
		mismatches.emplace_back("force parse option", m);
	}
	for (auto& entry : mismatches) {
		auto mismatch = entry.second;
		t->Run(entry.first, [cache, mismatch](T* t) {
			t->Parallel();
			auto [_v, found] = cache->AcquireExisting(mismatch);
			assert::Assert(t, !found);
			assert::Assert(t, !cache->Has(mismatch));
		});
	}

	cache->Deref(key);
	assert::Assert(t, !cache->Has(key));
}

void TestRefCountCacheAcquireExisting(T* t) {
	t->Parallel();

	int parseCount = 0;
	auto* cache = project::newRefCountCache<std::string, int, int>(
	    project::RefCountCacheOptions{},
	    [&parseCount](const std::string&, const int& value) {
		    parseCount++;
		    return value;
	    });

	auto [value, ok] = cache->AcquireExisting("missing");
	assert::Equal(t, value, 0);
	assert::Assert(t, !ok);
	assert::Equal(t, parseCount, 0);

	assert::Equal(t, cache->Acquire("key", 1), 1);
	std::tie(value, ok) = cache->AcquireExisting("key");
	assert::Assert(t, ok);
	assert::Equal(t, value, 1);
	assert::Equal(t, parseCount, 1);

	cache->Deref("key");
	assert::Assert(t, cache->Has("key"));
	cache->Deref("key");
	assert::Assert(t, !cache->Has("key"));

	std::tie(value, ok) = cache->AcquireExisting("key");
	assert::Equal(t, value, 0);
	assert::Assert(t, !ok);
	assert::Equal(t, parseCount, 1);
}

void TestRefCountCacheAcquireExistingRacesFinalRelease(T* t) {
	t->Parallel();

	for (int i = 0; i < 100; i++) {
		auto* cache = project::newRefCountCache<std::string, int*, int*>(
		    project::RefCountCacheOptions{},
		    [](const std::string&, int* const& value) { return value; });
		int value = 1;
		cache->Acquire("key", &value);

		std::promise<void> start;
		auto ready = start.get_future().share();
		std::promise<bool> acquired;
		auto acquiredFuture = acquired.get_future();
		std::thread worker([&] {
			ready.wait();
			auto [_v, ok] = cache->AcquireExisting("key");
			acquired.set_value(ok);
		});
		start.set_value();
		cache->Deref("key");
		if (acquiredFuture.get()) {
			cache->Deref("key");
		}
		worker.join();
		assert::Assert(t, !cache->Has("key"));
	}
}

}  // namespace

REGISTER_UNIT_TEST("project.TestContentMappedParseCacheBundleLifetime",
                   TestContentMappedParseCacheBundleLifetime);
REGISTER_UNIT_TEST("project.TestContentMappedParseCacheKeyReconstruction",
                   TestContentMappedParseCacheKeyReconstruction);
REGISTER_UNIT_TEST("project.TestParseCacheBindsBeforePublishing",
                   TestParseCacheBindsBeforePublishing);
REGISTER_UNIT_TEST("project.TestParseCacheAcquireExistingUsesFullKey",
                   TestParseCacheAcquireExistingUsesFullKey);
REGISTER_UNIT_TEST("project.TestRefCountCacheAcquireExisting",
                   TestRefCountCacheAcquireExisting);
REGISTER_UNIT_TEST("project.TestRefCountCacheAcquireExistingRacesFinalRelease",
                   TestRefCountCacheAcquireExistingRacesFinalRelease);
REGISTER_UNIT_TEST("project.TestRefCountingCaches", TestRefCountingCaches);
