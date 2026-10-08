// Port of tsc/internal/project/extendedconfigcache_test.go.
#include <memory>
#include <string>
#include <unordered_map>

#include "internal/bundled/bundled.h"
#include "internal/gostd/testing.h"
#include "internal/locale/locale.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/module/types.h"
#include "internal/project/configfileregistry.h"
#include "internal/project/extendedconfigcache.h"
#include "internal/project/logging/logging.h"
#include "internal/project/session.h"
#include "internal/project/snapshot.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace lsproto = tsc::lsp::lsproto;
namespace project = tsc::project;
namespace projecttestutil = tsc::testutil::projecttestutil;
namespace tsoptions = tsc::tsoptions;
namespace tspath = tsc::tspath;
namespace vfstest = tsc::vfs::vfstest;
namespace gostd = tsc::gostd;
using tsc::gostd::testing::T;

struct noopClient final : project::Client {
	gostd::Error WatchFiles(
	    const gostd::Context& ctx, project::WatcherID id,
	    const std::vector<lsproto::FileSystemWatcher*>& watchers)
	    override {
		return {};
	}

	gostd::Error UnwatchFiles(const gostd::Context& ctx,
	                          project::WatcherID id) override {
		return {};
	}

	gostd::Error RegisterContentMapperExtensions(
	    const gostd::Context& ctx,
	    const std::vector<std::string>& extensions) override {
		return {};
	}

	gostd::Error RefreshDiagnostics(const gostd::Context& ctx) override {
		return {};
	}

	gostd::Error PublishDiagnostics(
	    const gostd::Context& ctx,
	    lsproto::PublishDiagnosticsParams* params) override {
		return {};
	}

	gostd::Error RefreshInlayHints(const gostd::Context& ctx) override {
		return {};
	}

	gostd::Error RefreshCodeLens(const gostd::Context& ctx) override {
		return {};
	}

	void ProgressStart(const tsc::DiagnosticMessage* message,
	                   const std::vector<std::string>& args) override {}

	void ProgressFinish(const tsc::DiagnosticMessage* message,
	                    const std::vector<std::string>& args) override {}

	gostd::Error SendTelemetry(
	    const gostd::Context& ctx,
	    lsproto::TelemetryEvent telemetry) override {
		return {};
	}

	bool IsActive() override { return true; }

	tsc::locale::Locale GetLocale() override {
		return tsc::locale::Default;
	}

	void SetLocale(const std::string& locale) override {}
};

// testParseConfigHost — minimal ParseConfigHost (module.ResolutionHost
// folded into vfs.FS in the port).
struct testParseConfigHost final : tsoptions::ParseConfigHost {
	std::shared_ptr<tsc::vfs::FS> fs;
	std::string cwd;

	bool FileExists(std::string_view path) override {
		return fs->FileExists(std::string(path));
	}

	bool DirectoryExists(std::string_view path) override {
		return fs->DirectoryExists(std::string(path));
	}

	std::optional<std::string> ReadFile(
	    std::string_view path) override {
		auto [data, ok] = fs->ReadFile(std::string(path));
		if (!ok) return std::nullopt;
		return data;
	}

	std::string Realpath(std::string_view path) override {
		return fs->Realpath(std::string(path));
	}

	std::string GetCurrentDirectory() override { return cwd; }

	bool UseCaseSensitiveFileNames() override {
		return fs->UseCaseSensitiveFileNames();
	}

	AccessibleEntries GetAccessibleEntries(
	    std::string_view path) override {
		auto entries = fs->GetAccessibleEntries(std::string(path));
		return {std::move(entries.files), std::move(entries.directories),
		        std::move(entries.symlinks)};
	}
};

// TestExtendedConfigCacheOwnership tests the invariant that each ExtendedSourceFile
// of a config in the ConfigFileRegistry is owned exactly once per snapshot that
// references it, and released exactly once when that snapshot is removed.
void TestExtendedConfigCacheOwnership(T* t) {
	t->Parallel();

	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto setup = [](const projecttestutil::FileMap& files)
	    -> std::pair<project::Session*,
	                 std::shared_ptr<tsc::vfs::FS>> {
		auto fsFromMap = vfstest::FromMap(
		    files, false /*useCaseSensitiveFileNames*/);
		auto fs = tsc::bundled::WrapFS(fsFromMap);
		auto client = std::make_shared<noopClient>();
		auto logger = std::shared_ptr<tsc::logging::LogCollector>(
		    tsc::logging::newTestLogger());
		project::SessionInit init;
		init.BackgroundCtx = gostd::contextBackground();
		init.Options = new project::SessionOptions{
		    .CurrentDirectory = "/",
		    .DefaultLibraryPath = tsc::bundled::LibPath(),
		    .TypingsLocation = "/home/src/Library/Caches/typescript",
		    .PositionEncoding = lsproto::PositionEncodingKindUTF8,
		    .WatchEnabled = false,
		    .LoggingEnabled = false,
		};
		init.FS = fs;
		init.Client = client.get();
		init.Logger = logger.get();
		init.KeepAlive = {fs, client, logger};
		auto* session = project::NewSession(&init);
		return {session, fs};
	};

	auto openUntitled = [t, seq = 0](project::Session* session) mutable {
		seq++;
		lsproto::DocumentUri uri = gostd::sprintf(
		    "untitled:Untitled-%d", {int64_t(seq)});
		session->DidOpenFile(t->Context(), uri, 1, "",
		                     lsproto::LanguageKindTypeScript);
	};

	// flushCloseProject is the canonical way to ensure project close work is applied.
	// Close the file, then open an unrelated file.
	auto flushCloseProject = [&](project::Session* session,
	                             const lsproto::DocumentUri& fileURI) {
		session->DidCloseFile(t->Context(), fileURI);
		openUntitled(session);
	};

	auto ownerCount = [](project::Session* session,
	                     const tspath::Path& path) -> int64_t {
		auto [entry, ok] =
		    session->snapshotHost->extendedConfigCache->entries.Load(
		        path);
		if (!ok) {
			return 0;
		}
		return static_cast<int64_t>(entry->owners.size());
	};

	auto assertNoEntry = [](T* t, project::Session* session,
	                        const std::string& fileName) {
		t->Helper();
		auto path = session->toPath(fileName);
		auto [entry, ok] =
		    session->snapshotHost->extendedConfigCache->entries.Load(
		        path);
		assert::Equal(t, ok, false);
	};

	auto expectedExtendedOwnerCounts =
	    [](project::Session* session,
	       project::Snapshot* snapshot)
	    -> std::unordered_map<tspath::Path, int64_t> {
		std::unordered_map<tspath::Path, int64_t> result;
		for (auto& [path, cfg] :
		     snapshot->ConfigFileRegistry->configs) {
			if (cfg->commandLine == nullptr ||
			    cfg->commandLine->ConfigFile == nullptr) {
				continue;
			}
			for (auto& file :
			     cfg->commandLine->ExtendedSourceFiles()) {
				result[session->toPath(file)]++;
			}
		}
		return result;
	};

	auto assertExtendedOwnerCountsMatchRegistry =
	    [&expectedExtendedOwnerCounts, &ownerCount](
	        T* t, project::Session* session,
	        project::Snapshot* snapshot) {
		t->Helper();
		auto expected =
		    expectedExtendedOwnerCounts(session, snapshot);
		for (auto& [path, want] : expected) {
			auto got = ownerCount(session, path);
			assert::Equal(
			    t, got, want,
			    gostd::sprintf(
			        "extended config %s owner count mismatch",
			        {path}));
		}
	};

	t->Run(
	    "multi-extends shared ancestor counted once",
	    [&](T* t) {
		    t->Parallel();

		    // One config extends *two* configs; both extend a shared root.
		    // Expected behavior: ExtendedSourceFiles() is deduped, so the shared root should only
		    // be ref'd once for this config.
		    projecttestutil::FileMap files{
		        {"/project/tsconfig.json", std::string(R"({
				"extends": ["./tsconfig.base1.json", "./tsconfig.base2.json"]
			})")},
		        {"/project/tsconfig.base1.json", std::string(R"({
				"extends": "./tsconfig.root.json",
				"compilerOptions": {"strict": true}
			})")},
		        {"/project/tsconfig.base2.json", std::string(R"({
				"extends": "./tsconfig.root.json",
				"compilerOptions": {"noImplicitAny": true}
			})")},
		        {"/project/tsconfig.root.json", std::string(R"({
				"compilerOptions": {"target": "ES2020"}
			})")},
		        {"/project/src/main.ts",
		         std::string("export const x = 1;")},
		    };

		    auto [session, fs] = setup(files);
		    session->DidOpenFile(
		        t->Context(),
		        lsproto::DocumentUri("file:///project/src/main.ts"), 1,
		        std::get<std::string>(files.at("/project/src/main.ts")),
		        lsproto::LanguageKindTypeScript);
		    auto* snapshot = session->Snapshot();

		    auto* config = snapshot->ConfigFileRegistry->GetConfig(
		        tspath::Path("/project/tsconfig.json"));
		    assert::Assert(t, config != nullptr);
		    // Shared root should only appear once in the flattened list.
		    int rootCount = 0;
		    for (auto& f : config->ExtendedSourceFiles()) {
			    if (f == "/project/tsconfig.root.json") {
				    rootCount++;
			    }
		    }
		    assert::Equal(t, static_cast<int64_t>(rootCount),
		                  int64_t(1));

		    // And the cache owner counts should match the registry's deduped list.
		    assertExtendedOwnerCountsMatchRegistry(t, session, snapshot);

		    flushCloseProject(
		        session,
		        lsproto::DocumentUri("file:///project/src/main.ts"));
		    assertNoEntry(t, session, "/project/tsconfig.base1.json");
		    assertNoEntry(t, session, "/project/tsconfig.base2.json");
		    assertNoEntry(t, session, "/project/tsconfig.root.json");
	    });

	t->Run(
	    "ExtendedSourceFiles can contain same path twice (case-insensitive)",
	    [](T* t) {
		    t->Parallel();

		    // This test is descriptive, not prescriptive. This seems bad and unintentional,
		    // but is here to show that while the problem exists in the underlying config parsing
		    // API, it doesn't disrupt cache ownership.
		    projecttestutil::FileMap files{
		        {"/project/tsconfig.json", std::string(R"({
				"extends": ["./Shared.json", "./shared.json"]
			})")},
		        {"/project/shared.json", std::string(R"({
				"compilerOptions": {"strict": true}
			})")},
		    };

		    // This test intentionally bypasses the project system's ExtendedConfigCache so we can
		    // observe how ExtendedSourceFiles behaves when the same underlying file is referenced
		    // with different casing on a case-insensitive FS.
		    auto fsFromMap = vfstest::FromMap(
		        files, false /*useCaseSensitiveFileNames*/);
		    auto fs = tsc::bundled::WrapFS(fsFromMap);

		    // Minimal ParseConfigHost implementation.
		    testParseConfigHost h;
	    h.fs = fs;
	    h.cwd = "/";
		    auto [cmd, diags] =
		        tsoptions::GetParsedCommandLineOfConfigFile(
		            "/project/tsconfig.json", nullptr, nullptr, &h,
		            nullptr /*extendedConfigCache*/);
		    assert::Equal(t, static_cast<int64_t>(diags.size()),
		                  int64_t(0));
		    assert::Assert(t, cmd != nullptr);

		    auto extended = cmd->ExtendedSourceFiles();
		    assert::Equal(t,
		                  static_cast<int64_t>(extended.size()),
		                  int64_t(2));
		    assert::Equal(t, extended[0],
		                  std::string("/project/Shared.json"));
		    assert::Equal(t, extended[1],
		                  std::string("/project/shared.json"));
	    });

	t->Run(
	    "project system dedupes case-only extends via cache",
	    [&](T* t) {
		    t->Parallel();

		    projecttestutil::FileMap files{
		        {"/project/tsconfig.json", std::string(R"({
				"extends": ["./Shared.json", "./shared.json"]
			})")},
		        {"/project/shared.json", std::string(R"({
				"compilerOptions": {"strict": true}
			})")},
		        {"/project/src/main.ts",
		         std::string("export const x = 1;")},
		    };

		    auto [session, fs] = setup(files);
		    session->DidOpenFile(
		        t->Context(),
		        lsproto::DocumentUri("file:///project/src/main.ts"), 1,
		        std::get<std::string>(files.at("/project/src/main.ts")),
		        lsproto::LanguageKindTypeScript);
		    auto* snapshot = session->Snapshot();

		    auto* config = snapshot->ConfigFileRegistry->GetConfig(
		        tspath::Path("/project/tsconfig.json"));
		    assert::Assert(t, config != nullptr);
		    auto extended = config->ExtendedSourceFiles();
		    assert::Equal(t,
		                  static_cast<int64_t>(extended.size()),
		                  int64_t(1));
		    assert::Equal(t, session->toPath(extended[0]),
		                  session->toPath("/project/shared.json"));
	    });

	t->Run(
	    "transitive extended config ownership with new project",
	    [&](T* t) {
		    t->Parallel();

		    // Scenario: transitive extends chain where a new project reuses a cached
		    // extended config without reparsing it, which should still acquire the transitive deps.
		    //
		    // projectA/tsconfig.json extends shared/tsconfig.base.json extends shared/tsconfig.common.json
		    // projectB/tsconfig.json extends shared/tsconfig.base.json extends shared/tsconfig.common.json
		    //
		    // When projectB is opened AFTER projectA, tsconfig.base.json is retrieved from cache
		    // (not reparsed), so tsconfig.common.json still needs projectB's snapshot ownership.
		    projecttestutil::FileMap files{
		        {"/user/username/projects/shared/tsconfig.common.json",
		         std::string(R"({
					"compilerOptions": { "strict": true }
				})")},
		        {"/user/username/projects/shared/tsconfig.base.json",
		         std::string(R"({
					"extends": "./tsconfig.common.json",
					"compilerOptions": { "target": "ES2020" }
				})")},
		        {"/user/username/projects/projectA/tsconfig.json",
		         std::string(R"({
					"extends": "../shared/tsconfig.base.json"
				})")},
		        {"/user/username/projects/projectA/src/main.ts",
		         std::string("const a = 1;")},
		        {"/user/username/projects/projectB/tsconfig.json",
		         std::string(R"({
					"extends": "../shared/tsconfig.base.json"
				})")},
		        {"/user/username/projects/projectB/src/main.ts",
		         std::string("const b = 2;")},
		        {"/user/username/projects/other/src/main.ts",
		         std::string("const other = 3;")},
		    };

		    auto [session, fs] = setup(files);

		    // Step 1: Open file in projectA - this parses the full extends chain
		    session->DidOpenFile(
		        t->Context(),
		        "file:///user/username/projects/projectA/src/main.ts",
		        1,
		        std::get<std::string>(files.at(
		            "/user/username/projects/projectA/src/main.ts")),
		        lsproto::LanguageKindTypeScript);

		    // Verify extended configs are in cache with correct owner counts
		    auto [baseEntry, baseOk] =
		        session->snapshotHost->extendedConfigCache->entries.Load(
		            "/user/username/projects/shared/tsconfig.base.json");
		    auto [commonEntry, commonOk] =
		        session->snapshotHost->extendedConfigCache->entries.Load(
		            "/user/username/projects/shared/tsconfig.common.json");
		    assert::Assert(t, baseOk,
		                   "tsconfig.base.json should be in cache");
		    assert::Assert(t, commonOk,
		                   "tsconfig.common.json should be in cache");
		    assert::Equal(
		        t, static_cast<int64_t>(baseEntry->owners.size()),
		        int64_t(1));
		    assert::Equal(
		        t, static_cast<int64_t>(commonEntry->owners.size()),
		        int64_t(1));

		    // Step 2: Open file in projectB - this should acquire tsconfig.base.json from cache
		    // (not reparse it), and should also acquire tsconfig.common.json.
		    session->DidOpenFile(
		        t->Context(),
		        "file:///user/username/projects/projectB/src/main.ts",
		        1,
		        std::get<std::string>(files.at(
		            "/user/username/projects/projectB/src/main.ts")),
		        lsproto::LanguageKindTypeScript);

		    // Step 3: Close projectA file and open an unrelated file to force projectA cleanup
		    session->DidCloseFile(
		        t->Context(),
		        "file:///user/username/projects/projectA/src/main.ts");
		    // Opening another file triggers cleanup of closed projects
		    session->DidOpenFile(
		        t->Context(),
		        "file:///user/username/projects/other/src/main.ts", 1,
		        std::get<std::string>(files.at(
		            "/user/username/projects/other/src/main.ts")),
		        lsproto::LanguageKindTypeScript);

		    // Close the other file too so only projectB remains
		    session->DidCloseFile(
		        t->Context(),
		        "file:///user/username/projects/other/src/main.ts");

		    // Step 4: Trigger another snapshot clone for projectB
		    lsproto::TextDocumentContentChangePartialOrWholeDocument
		        change;
		    change.Partial = std::make_shared<
		        lsproto::TextDocumentContentChangePartial>();
		    change.Partial->Range = lsproto::Range{
		        .Start = lsproto::Position{.Line = 0, .Character = 0},
		        .End = lsproto::Position{.Line = 0, .Character = 12},
		    };
		    change.Partial->Text = "const b = 3;";
		    session->DidChangeFile(
		        t->Context(),
		        "file:///user/username/projects/projectB/src/main.ts",
		        2, {change});
		    // This call triggered the panic
		    auto [ls, err] = session->GetLanguageService(
		        t->Context(),
		        "file:///user/username/projects/projectB/src/main.ts");
		    assert::NilError(t, err);
		    delete ls;
	    });
}

REGISTER_UNIT_TEST("project.TestExtendedConfigCacheOwnership",
                   TestExtendedConfigCacheOwnership);

}  // namespace
