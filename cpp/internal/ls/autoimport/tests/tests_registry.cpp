// Port of tsc/internal/ls/autoimport/registry_test.go.
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/collections/collections.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/project/session.h"
#include "internal/project/sessiontypes.h"
#include "internal/testutil/autoimporttestutil/autoimporttestutil.h"
#include "internal/testutil/contentmappertest/contentmappertest.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
using namespace tsc;

namespace autoimport = tsc::ls::autoimport;
namespace lsproto = tsc::lsp::lsproto;
namespace projecttestutil = tsc::testutil::projecttestutil;
namespace autoimporttestutil = tsc::testutil::autoimporttestutil;
namespace contentmappertest = tsc::testutil::contentmappertest;

namespace {

constexpr std::string_view lifecycleProjectRoot = "/home/src/autoimport-lifecycle";
constexpr std::string_view monorepoProjectRoot = "/home/src/autoimport-monorepo";

autoimport::CacheStats* autoImportStats(T* t, project::Session* session) {
	t->Helper();
	auto* snapshot = session->Snapshot();
	auto* registry = snapshot->AutoImportRegistry();
	if (registry == nullptr) {
		t->Fatal({"auto import registry not initialized"});
	}
	return registry->GetCacheStats();
}

autoimport::BucketStats
singleBucket(T* t, const std::vector<autoimport::BucketStats>& buckets) {
	t->Helper();
	if (buckets.size() != 1) {
		t->Fatalf("expected 1 bucket, got %d", {static_cast<int>(buckets.size())});
	}
	return buckets[0];
}

// nilVfsErr is assert.NilError for vfs::Error (a distinct type from
// gostd::Error in the port).
void nilVfsErr(T* t, const vfs::Error& err) {
	t->Helper();
	if (err) {
		t->Fatalf("assert.NilError failed: %v", {err.str()});
	}
}

// wholeDocChange builds the single-element []TextDocumentContentChangePartialOrWholeDocument
// Go spells `{WholeDocument: &lsproto.TextDocumentContentChangeWholeDocument{Text: text}}`.
std::vector<lsproto::TextDocumentContentChangePartialOrWholeDocument>
wholeDocChange(const std::string& text) {
	lsproto::TextDocumentContentChangePartialOrWholeDocument c;
	c.WholeDocument = std::make_shared<lsproto::TextDocumentContentChangeWholeDocument>();
	c.WholeDocument->Text = text;
	return {c};
}

void TestRegistryLifecycle(T* t) {
	t->Parallel();
	t->Run("builds project and node_modules buckets", [](T* t) {
		t->Parallel();
		auto fixture = autoimporttestutil::SetupLifecycleSession(t, std::string(lifecycleProjectRoot), 1);
		auto* session = fixture->Session();
		auto project = fixture->SingleProject();
		auto mainFile = project.File(0);
		session->DidOpenFile(gostd::contextBackground(), mainFile.URI(), 1, mainFile.Content(),
		                   lsproto::LanguageKindTypeScript);

		auto* stats = autoImportStats(t, session);
		auto projectBucket = singleBucket(t, stats->ProjectBuckets);
		auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, true, projectBucket.State.Dirty());
		gotest::assert::Equal(t, 0, projectBucket.FileCount);
		gotest::assert::Equal(t, true, nodeModulesBucket.State.Dirty());
		gotest::assert::Equal(t, 0, nodeModulesBucket.FileCount);

		auto pair = session->GetCurrentLanguageServiceWithAutoImports(gostd::contextBackground(), mainFile.URI());
		gotest::assert::NilError(t, pair.second);

		stats = autoImportStats(t, session);
		projectBucket = singleBucket(t, stats->ProjectBuckets);
		nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, false, projectBucket.State.Dirty());
		gotest::assert::Assert(t, projectBucket.ExportCount > 0);
		gotest::assert::Equal(t, false, nodeModulesBucket.State.Dirty());
		gotest::assert::Assert(t, nodeModulesBucket.ExportCount > 0);
	});

	t->Run("bucket does not rebuild on same-file change", [](T* t) {
		t->Parallel();
		auto fixture = autoimporttestutil::SetupLifecycleSession(t, std::string(lifecycleProjectRoot), 2);
		auto* session = fixture->Session();
		auto utils = fixture->Utils();
		auto project = fixture->SingleProject();
		auto mainFile = project.File(0);
		auto secondaryFile = project.File(1);
		session->DidOpenFile(gostd::contextBackground(), mainFile.URI(), 1, mainFile.Content(),
		                   lsproto::LanguageKindTypeScript);
		session->DidOpenFile(gostd::contextBackground(), secondaryFile.URI(), 1, secondaryFile.Content(),
		                   lsproto::LanguageKindTypeScript);
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(gostd::contextBackground(), mainFile.URI());
		gotest::assert::NilError(t, pair.second);

		std::string updatedContent = mainFile.Content() + "// change\n";
		session->DidChangeFile(gostd::contextBackground(), mainFile.URI(), 2,
		                       wholeDocChange(updatedContent));

		auto lsPair = session->GetLanguageService(gostd::contextBackground(), mainFile.URI());
		gotest::assert::NilError(t, lsPair.second);

		auto* stats = autoImportStats(t, session);
		auto projectBucket = singleBucket(t, stats->ProjectBuckets);
		auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, projectBucket.State.Dirty(), true);
		gotest::assert::Equal(t, projectBucket.State.DirtyFile(), utils->ToPath(mainFile.FileName()));
		gotest::assert::Equal(t, nodeModulesBucket.State.Dirty(), false);
		gotest::assert::Equal(t, nodeModulesBucket.State.DirtyFile(), tspath::Path(""));

		// Bucket should not recompute when requesting same file changed
		pair = session->GetCurrentLanguageServiceWithAutoImports(gostd::contextBackground(), mainFile.URI());
		gotest::assert::NilError(t, pair.second);
		stats = autoImportStats(t, session);
		projectBucket = singleBucket(t, stats->ProjectBuckets);
		gotest::assert::Equal(t, projectBucket.State.Dirty(), true);
		gotest::assert::Equal(t, projectBucket.State.DirtyFile(), utils->ToPath(mainFile.FileName()));

		// Bucket should recompute when other file has changed
		session->DidChangeFile(gostd::contextBackground(), secondaryFile.URI(), 1,
		                       wholeDocChange("// new content"));
		pair = session->GetCurrentLanguageServiceWithAutoImports(gostd::contextBackground(), mainFile.URI());
		gotest::assert::NilError(t, pair.second);
		stats = autoImportStats(t, session);
		projectBucket = singleBucket(t, stats->ProjectBuckets);
		gotest::assert::Equal(t, projectBucket.State.Dirty(), false);
	});

	t->Run("bucket updates on same-file change when new files added to the program", [](T* t) {
		t->Parallel();
		std::string projectRoot = "/home/src/explicit-files-project";
		projecttestutil::FileMap files = {
		    {projectRoot + "/tsconfig.json",
		     "{\n"
		     "    \"compilerOptions\": {\n"
		     "        \"module\": \"esnext\",\n"
		     "        \"target\": \"esnext\",\n"
		     "        \"strict\": true\n"
		     "    },\n"
		     "    \"files\": [\"index.ts\"]\n"
		     "}"},
		    {projectRoot + "/index.ts", ""},
		    {projectRoot + "/utils.ts", "export const foo = 1;\nexport const bar = 2;"},
		};
		auto setupPair = projecttestutil::Setup(files);
		auto* session = setupPair.first;
		auto utils = setupPair.second;
		t->Cleanup([session, utils] { session->Close(); session->WaitForBackgroundTasks(); });

		auto ctx = gostd::contextBackground();
		auto indexURI = lsproto::DocumentUri("file://" + projectRoot + "/index.ts");

		// Open the index.ts file
		session->DidOpenFile(ctx, indexURI, 1, "", lsproto::LanguageKindTypeScript);
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, indexURI);
		gotest::assert::NilError(t, pair.second);
		auto* stats = autoImportStats(t, session);
		auto projectBucket = singleBucket(t, stats->ProjectBuckets);
		gotest::assert::Equal(t, 1, projectBucket.FileCount);

		// Edit index.ts to import foo from utils.ts
		std::string newContent = "import { foo } from \"./utils\";";
		session->DidChangeFile(ctx, indexURI, 2, wholeDocChange(newContent));

		// Bucket should be rebuilt because new files were added
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, indexURI);
		gotest::assert::NilError(t, pair.second);
		stats = autoImportStats(t, session);
		projectBucket = singleBucket(t, stats->ProjectBuckets);
		gotest::assert::Equal(t, 2, projectBucket.FileCount);
	});

	t->Run("package.json dependency changes invalidate node_modules buckets", [](T* t) {
		t->Parallel();
		auto fixture = autoimporttestutil::SetupLifecycleSession(t, std::string(lifecycleProjectRoot), 1);
		auto* session = fixture->Session();
		auto sessionUtils = fixture->Utils();
		auto project = fixture->SingleProject();
		auto mainFile = project.File(0);
		auto nodePackage = project.NodeModules()[0];
		auto packageJSON = project.PackageJSONFile();
		auto ctx = gostd::contextBackground();

		session->DidOpenFile(ctx, mainFile.URI(), 1, mainFile.Content(),
		                   lsproto::LanguageKindTypeScript);
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, mainFile.URI());
		gotest::assert::NilError(t, pair.second);
		auto* stats = autoImportStats(t, session);
		auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, nodeModulesBucket.State.Dirty(), false);

		auto fs = sessionUtils->FS();
		auto updatePackageJSON = [&](const std::string& content) {
			nilVfsErr(t, fs->WriteFile(packageJSON.FileName(), content));
			lsproto::FileEvent ev;
			ev.Type = lsproto::FileChangeTypeChanged;
			ev.Uri = packageJSON.URI();
			session->DidChangeWatchedFiles(ctx, {&ev});
		};

		std::string sameDepsContent = gostd::sprintf(
		    "{\n  \"name\": \"local-project-stable\",\n  \"dependencies\": {\n    \"%s\": \"*\"\n  }\n}\n",
		    {std::string(nodePackage.Name)});
		updatePackageJSON(sameDepsContent);
		auto lsPair = session->GetLanguageService(ctx, mainFile.URI());
		gotest::assert::NilError(t, lsPair.second);
		stats = autoImportStats(t, session);
		nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, nodeModulesBucket.State.Dirty(), false);

		std::string differentDepsContent = gostd::sprintf(
		    "{\n  \"name\": \"local-project-stable\",\n  \"dependencies\": {\n    \"%s\": \"*\",\n    \"newpkg\": \"*\"\n  }\n}\n",
		    {std::string(nodePackage.Name)});
		updatePackageJSON(differentDepsContent);
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, mainFile.URI());
		gotest::assert::NilError(t, pair.second);
		stats = autoImportStats(t, session);
		auto* depNames = singleBucket(t, stats->NodeModulesBuckets).DependencyNames;
		gotest::assert::Assert(t, depNames != nullptr && depNames->Has("newpkg"));
	});

	t->Run("node_modules buckets get deleted when no open files can reference them", [](T* t) {
		t->Parallel();
		auto fixture = autoimporttestutil::SetupMonorepoLifecycleSession(
		    t, autoimporttestutil::MonorepoSetupConfig{
		           .Root = std::string(monorepoProjectRoot),
		           .Template = {.Name = "monorepo", .NodeModuleNames = {"pkg-root"}},
		           .Packages = {{.FileCount = 1,
		                        .Template = {.Name = "package-a",
		                                     .NodeModuleNames = {"pkg-a"}}},
		                        {.FileCount = 1,
		                         .Template = {.Name = "package-b",
		                                      .NodeModuleNames = {"pkg-b"}}}},
		       });
		auto* session = fixture->Session();
		auto monorepo = fixture->Monorepo();
		auto pkgA = monorepo.Package(0);
		auto pkgB = monorepo.Package(1);
		auto fileA = pkgA.File(0);
		auto fileB = pkgB.File(0);
		auto ctx = gostd::contextBackground();

		// Open file in package-a, should create buckets for root and package-a node_modules
		session->DidOpenFile(ctx, fileA.URI(), 1, fileA.Content(),
		                   lsproto::LanguageKindTypeScript);
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, fileA.URI());
		gotest::assert::NilError(t, pair.second);

		// Open file in package-b, should also create buckets for package-b
		session->DidOpenFile(ctx, fileB.URI(), 1, fileB.Content(),
		                   lsproto::LanguageKindTypeScript);
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, fileB.URI());
		gotest::assert::NilError(t, pair.second);
		auto* stats = autoImportStats(t, session);
		gotest::assert::Equal(t, static_cast<int>(stats->NodeModulesBuckets.size()), 3);
		gotest::assert::Equal(t, static_cast<int>(stats->ProjectBuckets.size()), 2);

		// Close file in package-a, package-a's node_modules bucket and project bucket should be removed
		session->DidCloseFile(ctx, fileA.URI());
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, fileB.URI());
		gotest::assert::NilError(t, pair.second);
		stats = autoImportStats(t, session);
		gotest::assert::Equal(t, static_cast<int>(stats->NodeModulesBuckets.size()), 2);
		gotest::assert::Equal(t, static_cast<int>(stats->ProjectBuckets.size()), 1);
	});

	t->Run("deleting node_modules leaves the registry prepared for importing", [](T* t) {
		t->Parallel();
		auto fixture = autoimporttestutil::SetupLifecycleSession(t, std::string(lifecycleProjectRoot), 1);
		auto* session = fixture->Session();
		auto sessionUtils = fixture->Utils();
		auto project = fixture->SingleProject();
		auto mainFile = project.File(0);
		auto ctx = gostd::contextBackground();

		auto preferences = ls::lsutil::NewDefaultUserPreferences();
		preferences.IncludeCompletionsForModuleExports = Tristate::True;
		preferences.IncludeCompletionsForImportStatements = Tristate::True;

		// Build auto-imports once so both buckets are clean and prepared.
		session->DidOpenFile(ctx, mainFile.URI(), 1, mainFile.Content(),
		                   lsproto::LanguageKindTypeScript);
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, mainFile.URI());
		gotest::assert::NilError(t, pair.second);

		auto* snapshot = session->Snapshot();
		auto* defaultProject = snapshot->GetDefaultProject(mainFile.URI());
		gotest::assert::Assert(t, defaultProject != nullptr);
		auto* projectID = autoimport::InternProjectID(project::idString(defaultProject->ID()));
		gotest::assert::Assert(t, autoimport::IsPreparedForImportingFile(snapshot->AutoImportRegistry(), mainFile.FileName(), projectID, preferences));
		gotest::assert::Equal(t, static_cast<int>(autoImportStats(t, session)->NodeModulesBuckets.size()), 1);

		// Simulate the user deleting node_modules: remove the directory from disk
		// and notify the session of the deletion, which marks the node_modules
		// bucket dirty.
		std::string nodeModulesDir = tspath::combinePaths(project.Root(), {"node_modules"});
		nilVfsErr(t, sessionUtils->FS()->Remove(nodeModulesDir));
		lsproto::FileEvent ev;
		ev.Type = lsproto::FileChangeTypeDeleted;
		ev.Uri = lsconv::FileNameToDocumentURI(nodeModulesDir);
		session->DidChangeWatchedFiles(ctx, {&ev});

		// Re-preparing auto-imports must succeed and leave the registry prepared.
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, mainFile.URI());
		gotest::assert::NilError(t, pair.second);

		snapshot = session->Snapshot();
		gotest::assert::Assert(t, autoimport::IsPreparedForImportingFile(snapshot->AutoImportRegistry(), mainFile.FileName(), projectID, preferences),
		                       "registry should be prepared after node_modules is deleted");
		// The node_modules bucket should be removed entirely, not left behind as an
		// empty bucket.
		gotest::assert::Equal(t, static_cast<int>(autoImportStats(t, session)->NodeModulesBuckets.size()), 0);
	});

	t->Run("deleting node_modules alongside a package.json change removes the bucket", [](T* t) {
		t->Parallel();
		auto fixture = autoimporttestutil::SetupLifecycleSession(t, std::string(lifecycleProjectRoot), 1);
		auto* session = fixture->Session();
		auto sessionUtils = fixture->Utils();
		auto project = fixture->SingleProject();
		auto mainFile = project.File(0);
		auto packageJSON = project.PackageJSONFile();
		auto ctx = gostd::contextBackground();

		auto preferences = ls::lsutil::NewDefaultUserPreferences();
		preferences.IncludeCompletionsForModuleExports = Tristate::True;
		preferences.IncludeCompletionsForImportStatements = Tristate::True;

		session->DidOpenFile(ctx, mainFile.URI(), 1, mainFile.Content(),
		                   lsproto::LanguageKindTypeScript);
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, mainFile.URI());
		gotest::assert::NilError(t, pair.second);

		auto* snapshot = session->Snapshot();
		auto* defaultProject = snapshot->GetDefaultProject(mainFile.URI());
		gotest::assert::Assert(t, defaultProject != nullptr);
		auto* projectID = autoimport::InternProjectID(project::idString(defaultProject->ID()));
		gotest::assert::Equal(t, static_cast<int>(autoImportStats(t, session)->NodeModulesBuckets.size()), 1);

		// In a single changeset, edit package.json AND delete node_modules. The
		// package.json change must not prevent the now-missing node_modules bucket
		// from being removed.
		nilVfsErr(t, sessionUtils->FS()->WriteFile(packageJSON.FileName(),
		                                         "{\"name\": \"app\", \"dependencies\": {}}"));
		std::string nodeModulesDir = tspath::combinePaths(project.Root(), {"node_modules"});
		nilVfsErr(t, sessionUtils->FS()->Remove(nodeModulesDir));
		lsproto::FileEvent ev1;
		ev1.Type = lsproto::FileChangeTypeChanged;
		ev1.Uri = packageJSON.URI();
		lsproto::FileEvent ev2;
		ev2.Type = lsproto::FileChangeTypeDeleted;
		ev2.Uri = lsconv::FileNameToDocumentURI(nodeModulesDir);
		session->DidChangeWatchedFiles(ctx, {&ev1, &ev2});

		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, mainFile.URI());
		gotest::assert::NilError(t, pair.second);

		snapshot = session->Snapshot();
		gotest::assert::Assert(t, autoimport::IsPreparedForImportingFile(snapshot->AutoImportRegistry(), mainFile.FileName(), projectID, preferences));
		gotest::assert::Equal(t, static_cast<int>(autoImportStats(t, session)->NodeModulesBuckets.size()), 0);
	});

	t->Run("deleting a package directory inside node_modules invalidates the bucket", [](T* t) {
		t->Parallel();
		auto fixture = autoimporttestutil::SetupLifecycleSession(t, std::string(lifecycleProjectRoot), 1);
		auto* session = fixture->Session();
		auto sessionUtils = fixture->Utils();
		auto project = fixture->SingleProject();
		auto mainFile = project.File(0);
		auto nodePackage = project.NodeModules()[0];
		auto ctx = gostd::contextBackground();

		session->DidOpenFile(ctx, mainFile.URI(), 1, mainFile.Content(),
		                   lsproto::LanguageKindTypeScript);
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, mainFile.URI());
		gotest::assert::NilError(t, pair.second);
		gotest::assert::Assert(t, singleBucket(t, autoImportStats(t, session)->NodeModulesBuckets).ExportCount > 0);

		// Delete just the package directory, leaving node_modules itself in place.
		nilVfsErr(t, sessionUtils->FS()->Remove(std::string(nodePackage.Directory)));
		lsproto::FileEvent ev;
		ev.Type = lsproto::FileChangeTypeDeleted;
		ev.Uri = lsconv::FileNameToDocumentURI(std::string(nodePackage.Directory));
		session->DidChangeWatchedFiles(ctx, {&ev});

		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, mainFile.URI());
		gotest::assert::NilError(t, pair.second);
		gotest::assert::Equal(t, singleBucket(t, autoImportStats(t, session)->NodeModulesBuckets).ExportCount, 0);
	});

	t->Run("node_modules bucket dependency selection changes with open files", [](T* t) {
		t->Parallel();
		std::string monorepoRoot = "/home/src/monorepo";
		std::string packageADir = tspath::combinePaths(monorepoRoot, {"packages", "a"});
		std::string monorepoIndex = tspath::combinePaths(monorepoRoot, {"index.js"});
		std::string packageAIndex = tspath::combinePaths(packageADir, {"index.js"});

		auto fixture = autoimporttestutil::SetupMonorepoLifecycleSession(
		    t, autoimporttestutil::MonorepoSetupConfig{
		           .Root = monorepoRoot,
		           .Template = {.Name = "monorepo",
		                        .NodeModuleNames = {"pkg1", "pkg2", "pkg3"},
		                        .DependencyNames = {"pkg1"}},
		           .Packages = {{.FileCount = 0,
		                        .Template = {.Name = "a",
		                                     .DependencyNames = {"pkg1", "pkg2"}}}},
		           .ExtraFiles = {{.Path = monorepoIndex,
		                          .Content = "export const monorepoIndex = 1;\n"},
		                         {.Path = packageAIndex,
		                          .Content = "export const pkgA = 2;\n"}},
		       });
		auto* session = fixture->Session();
		auto monorepoHandle = fixture->ExtraFile(monorepoIndex);
		auto packageAHandle = fixture->ExtraFile(packageAIndex);

		auto ctx = gostd::contextBackground();

		// Open monorepo root file: expect dependencies restricted to pkg1
		session->DidOpenFile(ctx, monorepoHandle.URI(), 1, monorepoHandle.Content(),
		                   lsproto::LanguageKindJavaScript);
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, monorepoHandle.URI());
		gotest::assert::NilError(t, pair.second);
		auto* stats = autoImportStats(t, session);
		auto* depNames = singleBucket(t, stats->NodeModulesBuckets).DependencyNames;
		gotest::assert::Assert(t, depNames != nullptr &&
		                          depNames->Equals(collections::NewSetFromItems<std::string>("pkg1")));

		// Open package-a file: pkg2 should be added to existing bucket
		session->DidOpenFile(ctx, packageAHandle.URI(), 1, packageAHandle.Content(),
		                   lsproto::LanguageKindJavaScript);
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, packageAHandle.URI());
		gotest::assert::NilError(t, pair.second);
		stats = autoImportStats(t, session);
		depNames = singleBucket(t, stats->NodeModulesBuckets).DependencyNames;
		gotest::assert::Assert(t, depNames != nullptr &&
		                          depNames->Equals(collections::NewSetFromItems<std::string>("pkg1", "pkg2")));

		// Close package-a file; only monorepo bucket should remain
		session->DidCloseFile(ctx, packageAHandle.URI());
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, monorepoHandle.URI());
		gotest::assert::NilError(t, pair.second);
		stats = autoImportStats(t, session);
		depNames = singleBucket(t, stats->NodeModulesBuckets).DependencyNames;
		gotest::assert::Assert(t, depNames != nullptr &&
		                          depNames->Equals(collections::NewSetFromItems<std::string>("pkg1")));

		// Close monorepo file; no node_modules buckets should remain
		session->DidCloseFile(ctx, monorepoHandle.URI());
		session->DidOpenFile(ctx, "untitled:Untitled-1", 0, "", lsproto::LanguageKindTypeScript);
		auto lsPair = session->GetLanguageService(ctx, "untitled:Untitled-1");
		gotest::assert::NilError(t, lsPair.second);
		stats = autoImportStats(t, session);
		gotest::assert::Equal(t, static_cast<int>(stats->NodeModulesBuckets.size()), 0);
	});

	t->Run("node_modules bucket includes resolved packages from all projects", [](T* t) {
		// This test verifies that when multiple projects share a node_modules directory,
		// the node_modules bucket includes resolved package names from ALL projects,
		// not just the currently requested file's project.
		t->Parallel();
		std::string monorepoRoot = "/home/src/cross-project-deps";
		std::string packageADir = tspath::combinePaths(monorepoRoot, {"packages", "a"});
		std::string packageBDir = tspath::combinePaths(monorepoRoot, {"packages", "b"});
		std::string packageAIndex = tspath::combinePaths(packageADir, {"index.ts"});
		std::string packageBIndex = tspath::combinePaths(packageBDir, {"index.ts"});

		auto fixture = autoimporttestutil::SetupMonorepoLifecycleSession(
		    t, autoimporttestutil::MonorepoSetupConfig{
		           .Root = monorepoRoot,
		           .Template = {.Name = "monorepo",
		                        // Both pkg-listed and pkg-unlisted exist in node_modules
		                        .NodeModuleNames = {"pkg-listed", "pkg-unlisted"},
		                        // But only pkg-listed is in the root package.json dependencies
		                        .DependencyNames = {"pkg-listed"}},
		           .Packages = {
		               {.FileCount = 0,
		                .Template = {.Name = "a",
		                             // package-a only lists pkg-listed in its package.json
		                             .DependencyNames = {"pkg-listed"}}},
		               {.FileCount = 0,
		                .Template = {.Name = "b",
		                             // package-b also only lists pkg-listed in its package.json
		                             .DependencyNames = {"pkg-listed"}}},
		           },
		           .ExtraFiles = {
		               // project-a directly imports pkg-unlisted (not in package.json)
		               {.Path = packageAIndex,
		                .Content = "import { pkg_unlisted_value } from \"pkg-unlisted\";\nexport const a = pkg_unlisted_value;\n"},
		               // project-b does not import pkg-unlisted
		               {.Path = packageBIndex, .Content = "export const b = 1;\n"},
		           },
		       });
		auto* session = fixture->Session();
		auto packageAHandle = fixture->ExtraFile(packageAIndex);
		auto packageBHandle = fixture->ExtraFile(packageBIndex);

		auto ctx = gostd::contextBackground();

		// Open file in project-a (which imports pkg-unlisted)
		session->DidOpenFile(ctx, packageAHandle.URI(), 1, packageAHandle.Content(),
		                   lsproto::LanguageKindTypeScript);
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, packageAHandle.URI());
		gotest::assert::NilError(t, pair.second);

		// Open file in project-b (which does not import pkg-unlisted)
		session->DidOpenFile(ctx, packageBHandle.URI(), 1, packageBHandle.Content(),
		                   lsproto::LanguageKindTypeScript);
		// Request auto-imports for project-b
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, packageBHandle.URI());
		gotest::assert::NilError(t, pair.second);

		// Verify that the node_modules bucket includes pkg-unlisted
		// even though we requested auto-imports for project-b which doesn't list it.
		auto* stats = autoImportStats(t, session);
		auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Assert(t, nodeModulesBucket.DependencyNames != nullptr &&
		                          nodeModulesBucket.DependencyNames->Has("pkg-listed"),
		                       "pkg-listed should be in dependencies");
		gotest::assert::Assert(t, nodeModulesBucket.DependencyNames != nullptr &&
		                          nodeModulesBucket.DependencyNames->Has("pkg-unlisted"),
		                       "pkg-unlisted should be in dependencies because project-a imports it");
	});

	t->Run("symlinked monorepo invalidates on source file change", [](T* t) {
		// This test verifies that when a source file in a symlinked project reference
		// is modified, the node_modules bucket is properly invalidated.
		t->Parallel();
		std::string monorepoRoot = "/home/src/symlinked-monorepo-invalidation";
		std::string projectADir = tspath::combinePaths(monorepoRoot, {"packages", "project-a"});
		std::string projectBDir = tspath::combinePaths(monorepoRoot, {"packages", "project-b"});
		std::string projectAIndex = tspath::combinePaths(projectADir, {"src", "index.ts"});
		std::string projectBSrcIndex = tspath::combinePaths(projectBDir, {"src", "index.ts"});
		std::string projectBDistIndex = tspath::combinePaths(projectBDir, {"dist", "index.d.ts"});
		std::string otherPkgDir = tspath::combinePaths(projectADir, {"node_modules", "other-pkg"});

		projecttestutil::FileMap files = {
		    // project-b: the library package
		    {tspath::combinePaths(projectBDir, {"tsconfig.json"}),
		     "{\n"
		     "    \"compilerOptions\": {\n"
		     "        \"composite\": true,\n"
		     "        \"outDir\": \"./dist\",\n"
		     "        \"rootDir\": \"./src\",\n"
		     "        \"declaration\": true,\n"
		     "        \"module\": \"esnext\",\n"
		     "        \"strict\": true\n"
		     "    },\n"
		     "    \"include\": [\"src\"]\n"
		     "}"},
		    {tspath::combinePaths(projectBDir, {"package.json"}),
		     "{\n"
		     "    \"name\": \"project-b\",\n"
		     "    \"version\": \"1.0.0\",\n"
		     "    \"main\": \"dist/index.js\",\n"
		     "    \"types\": \"dist/index.d.ts\"\n"
		     "}"},
		    {projectBSrcIndex,
		     "export function projectBFunction(): string { return \"hello\"; }\n"
		     "export const projectBValue: number = 42;"},
		    {projectBDistIndex,
		     "export declare function projectBFunction(): string;\n"
		     "export declare const projectBValue: number;"},
		    // other-pkg: a regular (non-symlinked) package
		    {tspath::combinePaths(otherPkgDir, {"package.json"}),
		     "{\n"
		     "    \"name\": \"other-pkg\",\n"
		     "    \"version\": \"1.0.0\",\n"
		     "    \"main\": \"index.js\",\n"
		     "    \"types\": \"index.d.ts\"\n"
		     "}"},
		    {tspath::combinePaths(otherPkgDir, {"index.d.ts"}),
		     "export declare function otherFunction(): void;\n"
		     "export declare const otherValue: string;"},
		    // project-a: the consumer package
		    {tspath::combinePaths(projectADir, {"tsconfig.json"}),
		     "{\n"
		     "    \"compilerOptions\": {\n"
		     "        \"module\": \"esnext\",\n"
		     "        \"strict\": true,\n"
		     "        \"outDir\": \"./dist\",\n"
		     "        \"rootDir\": \"./src\"\n"
		     "    },\n"
		     "    \"include\": [\"src\"],\n"
		     "    \"references\": [{ \"path\": \"../project-b\" }]\n"
		     "}"},
		    {tspath::combinePaths(projectADir, {"package.json"}),
		     "{\n"
		     "    \"name\": \"project-a\",\n"
		     "    \"dependencies\": { \"project-b\": \"*\", \"other-pkg\": \"*\" }\n"
		     "}"},
		    {projectAIndex, "console.log(\"hello\");\n"},
		    // Symlink: project-b is accessible via node_modules
		    {tspath::combinePaths(projectADir, {"node_modules", "project-b"}),
		     vfs::vfstest::Symlink(projectBDir)},
		};

		auto setupPair = projecttestutil::Setup(files);
		auto* session = setupPair.first;
		auto utils = setupPair.second;
		t->Cleanup([session, utils] { session->Close(); session->WaitForBackgroundTasks(); });
		auto ctx = gostd::contextBackground();

		// Open project-a's index file and get initial auto-imports
		auto projectAURI = lsconv::FileNameToDocumentURI(projectAIndex);
		std::string projectAContent = std::get<std::string>(files[projectAIndex]);
		session->DidOpenFile(ctx, projectAURI, 1, projectAContent,
		                   lsproto::LanguageKindTypeScript);
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, projectAURI);
		gotest::assert::NilError(t, pair.second);

		// Verify initial state: bucket is clean with files
		auto* stats = autoImportStats(t, session);
		auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		int initialFileCount = nodeModulesBucket.FileCount;
		gotest::assert::Equal(t, nodeModulesBucket.State.Dirty(), false,
		                      "bucket should be clean initially");
		gotest::assert::Assert(t, initialFileCount > 0, "bucket should have files initially");

		// Open project-b's source file
		auto projectBURI = lsconv::FileNameToDocumentURI(projectBSrcIndex);
		std::string projectBContent = std::get<std::string>(files[projectBSrcIndex]);
		session->DidOpenFile(ctx, projectBURI, 1, projectBContent,
		                   lsproto::LanguageKindTypeScript);

		// Modify the file (delete one export)
		std::string newProjectBContent = "export const projectBValue: number = 42;";
		session->DidChangeFile(ctx, projectBURI, 2, wholeDocChange(newProjectBContent));

		// Check that the node_modules bucket is now dirty
		auto lsPair = session->GetLanguageService(ctx, projectAURI);
		gotest::assert::NilError(t, lsPair.second);
		stats = autoImportStats(t, session);
		nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, nodeModulesBucket.State.Dirty(), true,
		                      "bucket should be dirty after source file change");

		// Verify that only project-b is marked for update, not other-pkg.
		auto* dirtyPackages = nodeModulesBucket.State.DirtyPackages();
		gotest::assert::Assert(t, dirtyPackages != nullptr, "dirty packages should be tracked");
		gotest::assert::Assert(t, dirtyPackages->Has("project-b"),
		                       "project-b should be in dirty packages");
		gotest::assert::Assert(t, !dirtyPackages->Has("other-pkg"),
		                       "other-pkg should NOT be in dirty packages");
		gotest::assert::Equal(t, static_cast<int>(dirtyPackages->Len()), 1,
		                      "only one package should be dirty");

		// Rebuild by requesting auto-imports again.
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, projectAURI);
		gotest::assert::NilError(t, pair.second);

		// Verify bucket is clean again after rebuild
		stats = autoImportStats(t, session);
		nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, nodeModulesBucket.State.Dirty(), false,
		                      "bucket should be clean after rebuild");
	});

	t->Run("pnpm-style symlinks only grant granular updates to workspace packages", [](T* t) {
		// In pnpm, every package in node_modules is symlinked — registry packages
		// are symlinked into node_modules/.pnpm/<pkg>@<version>/node_modules/<pkg>.
		// Only local workspace packages (whose realpaths are outside node_modules
		// and within the workspace root) should be eligible for granular updates.
		// Registry packages should trigger full bucket rebuilds.
		t->Parallel();
		std::string monorepoRoot = "/home/src/pnpm-monorepo";
		std::string projectADir = tspath::combinePaths(monorepoRoot, {"packages", "project-a"});
		std::string projectBDir = tspath::combinePaths(monorepoRoot, {"packages", "project-b"});
		std::string projectAIndex = tspath::combinePaths(projectADir, {"src", "index.ts"});
		std::string projectBSrcIndex = tspath::combinePaths(projectBDir, {"src", "index.ts"});
		std::string projectBDistIndex = tspath::combinePaths(projectBDir, {"dist", "index.d.ts"});

		// Simulated pnpm virtual store for a registry package (inside project-a's node_modules).
		std::string pnpmStoreDir = tspath::combinePaths(
		    projectADir, {"node_modules", ".pnpm-store", "other-pkg@1.0.0"});
		std::string otherPkgIndex = tspath::combinePaths(pnpmStoreDir, {"index.d.ts"});

		projecttestutil::FileMap files = {
		    // project-b: a local workspace package
		    {tspath::combinePaths(projectBDir, {"tsconfig.json"}),
		     "{\n"
		     "    \"compilerOptions\": {\n"
		     "        \"composite\": true,\n"
		     "        \"outDir\": \"./dist\",\n"
		     "        \"rootDir\": \"./src\",\n"
		     "        \"declaration\": true,\n"
		     "        \"module\": \"esnext\",\n"
		     "        \"strict\": true\n"
		     "    },\n"
		     "    \"include\": [\"src\"]\n"
		     "}"},
		    {tspath::combinePaths(projectBDir, {"package.json"}),
		     "{\n"
		     "    \"name\": \"project-b\",\n"
		     "    \"version\": \"1.0.0\",\n"
		     "    \"main\": \"dist/index.js\",\n"
		     "    \"types\": \"dist/index.d.ts\"\n"
		     "}"},
		    {projectBSrcIndex,
		     "export function projectBFunction(): string { return \"hello\"; }\n"
		     "export const projectBValue: number = 42;"},
		    {projectBDistIndex,
		     "export declare function projectBFunction(): string;\n"
		     "export declare const projectBValue: number;"},
		    // other-pkg: a registry package in pnpm's virtual store
		    {tspath::combinePaths(pnpmStoreDir, {"package.json"}),
		     "{\n"
		     "    \"name\": \"other-pkg\",\n"
		     "    \"version\": \"1.0.0\",\n"
		     "    \"main\": \"index.js\",\n"
		     "    \"types\": \"index.d.ts\"\n"
		     "}"},
		    {otherPkgIndex,
		     "export declare function otherFunction(): void;\n"
		     "export declare const otherValue: string;"},
		    // project-a: the consumer package
		    {tspath::combinePaths(projectADir, {"tsconfig.json"}),
		     "{\n"
		     "    \"compilerOptions\": {\n"
		     "        \"module\": \"esnext\",\n"
		     "        \"strict\": true,\n"
		     "        \"outDir\": \"./dist\",\n"
		     "        \"rootDir\": \"./src\"\n"
		     "    },\n"
		     "    \"include\": [\"src\"],\n"
		     "    \"references\": [{ \"path\": \"../project-b\" }]\n"
		     "}"},
		    {tspath::combinePaths(projectADir, {"package.json"}),
		     "{\n"
		     "    \"name\": \"project-a\",\n"
		     "    \"dependencies\": { \"project-b\": \"*\", \"other-pkg\": \"*\" }\n"
		     "}"},
		    {projectAIndex, "console.log(\"hello\");\n"},
		    // Symlink: local workspace package (realpath outside node_modules)
		    {tspath::combinePaths(projectADir, {"node_modules", "project-b"}),
		     vfs::vfstest::Symlink(projectBDir)},
		    // Symlink: pnpm-style registry package (realpath inside node_modules/.pnpm)
		    {tspath::combinePaths(projectADir, {"node_modules", "other-pkg"}),
		     vfs::vfstest::Symlink(pnpmStoreDir)},
		};

		project::SessionOptions options;
		options.CurrentDirectory = monorepoRoot;
		options.DefaultLibraryPath = bundled::LibPath();
		options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
		options.WatchEnabled = true;
		options.LoggingEnabled = true;
		options.PushDiagnosticsEnabled = true;
		auto setupPair = projecttestutil::SetupWithOptions(files, &options);
		auto* session = setupPair.first;
		auto utils = setupPair.second;
		t->Cleanup([session, utils] { session->Close(); session->WaitForBackgroundTasks(); });
		auto ctx = gostd::contextBackground();

		// Open project-a's index file and build auto-imports
		auto projectAURI = lsconv::FileNameToDocumentURI(projectAIndex);
		std::string projectAContent = std::get<std::string>(files[projectAIndex]);
		session->DidOpenFile(ctx, projectAURI, 1, projectAContent,
		                   lsproto::LanguageKindTypeScript);
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, projectAURI);
		gotest::assert::NilError(t, pair.second);

		// Verify initial state: bucket is clean
		auto* stats = autoImportStats(t, session);
		auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, nodeModulesBucket.State.Dirty(), false,
		                      "bucket should be clean initially");

		// Modify project-b's source file (local workspace package)
		auto projectBURI = lsconv::FileNameToDocumentURI(projectBSrcIndex);
		std::string projectBContent = std::get<std::string>(files[projectBSrcIndex]);
		session->DidOpenFile(ctx, projectBURI, 1, projectBContent,
		                   lsproto::LanguageKindTypeScript);
		session->DidChangeFile(ctx, projectBURI, 2,
		                       wholeDocChange("export const projectBValue: number = 42;"));

		// project-b should get a granular update (tracked in dirtyPackages)
		auto lsPair = session->GetLanguageService(ctx, projectAURI);
		gotest::assert::NilError(t, lsPair.second);
		stats = autoImportStats(t, session);
		nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, nodeModulesBucket.State.Dirty(), true,
		                      "bucket should be dirty after workspace package change");
		auto* dirtyPackages = nodeModulesBucket.State.DirtyPackages();
		gotest::assert::Assert(t, dirtyPackages != nullptr,
		                       "dirty packages should be tracked for workspace package");
		gotest::assert::Assert(t, dirtyPackages->Has("project-b"),
		                       "project-b should be in dirty packages");
		gotest::assert::Equal(t, static_cast<int>(dirtyPackages->Len()), 1,
		                      "only project-b should be dirty");

		// Rebuild to clear dirty state
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, projectAURI);
		gotest::assert::NilError(t, pair.second);
		stats = autoImportStats(t, session);
		nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, nodeModulesBucket.State.Dirty(), false,
		                      "bucket should be clean after rebuild");

		// Now modify other-pkg (pnpm registry package, realpath inside node_modules/.pnpm)
		auto otherPkgURI = lsconv::FileNameToDocumentURI(otherPkgIndex);
		std::string otherPkgContent = std::get<std::string>(files[otherPkgIndex]);
		session->DidOpenFile(ctx, otherPkgURI, 1, otherPkgContent,
		                   lsproto::LanguageKindTypeScript);
		session->DidChangeFile(ctx, otherPkgURI, 2,
		                       wholeDocChange("export declare function otherFunction(): void;"));

		// other-pkg should trigger a full rebuild (multipleFilesDirty), not a granular
		// update. Read stats from the request-time snapshot (held by ref via
		// WithLanguageServiceAndSnapshot) rather than session->Snapshot(), so a
		// background auto-import warm task cannot race with the assertion.
		auto snapPair = session->WithLanguageServiceAndSnapshot(
		    ctx, projectAURI,
		    [&stats](ls::LanguageService*,
		             project::Snapshot* snapshot)
		        -> std::pair<std::function<gostd::Error()>, gostd::Error> {
			    stats = snapshot->AutoImportRegistry()->GetCacheStats();
			    return {nullptr, nullptr};
		    });
		gotest::assert::NilError(t, snapPair.second);
		nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, nodeModulesBucket.State.Dirty(), true,
		                      "bucket should be dirty after registry package change");
		dirtyPackages = nodeModulesBucket.State.DirtyPackages();
		// A full rebuild means dirtyPackages is nil (multipleFilesDirty takes precedence)
		// or dirtyPackages doesn't contain "other-pkg" as a granular entry
		if (dirtyPackages != nullptr) {
			gotest::assert::Assert(t, !dirtyPackages->Has("other-pkg"),
			                       "other-pkg should NOT be in dirty packages (should trigger full rebuild)");
		}
	});

	t->Run("circular workspace symlinks do not exclude local project files", [](T* t) {
		t->Parallel();

		std::string monorepoRoot = "/home/src/circular-workspaces";
		std::string packageADir = tspath::combinePaths(monorepoRoot, {"packages", "pkg-a"});
		std::string packageBDir = tspath::combinePaths(monorepoRoot, {"packages", "pkg-b"});
		std::string consumerA = tspath::combinePaths(packageADir, {"consumer.ts"});
		std::string helperA = tspath::combinePaths(packageADir, {"helper.ts"});

		projecttestutil::FileMap files = {
		    {tspath::combinePaths(packageADir, {"tsconfig.json"}),
		     "{\n"
		     "    \"compilerOptions\": {\n"
		     "        \"module\": \"esnext\",\n"
		     "        \"strict\": true\n"
		     "    }\n"
		     "}"},
		    {tspath::combinePaths(packageADir, {"package.json"}),
		     "{\n"
		     "    \"name\": \"pkg-a\",\n"
		     "    \"dependencies\": { \"pkg-b\": \"*\" }\n"
		     "}"},
		    {tspath::combinePaths(packageADir, {"index.ts"}),
		     "import { b } from \"pkg-b\";\n"
		     "export const a = b;\n"},
		    {consumerA, "export const usesHelper = uniqueHelperValueFromHelperA;\n"},
		    {helperA, "export const uniqueHelperValueFromHelperA = 1;\n"},
		    {tspath::combinePaths(packageBDir, {"tsconfig.json"}),
		     "{\n"
		     "    \"compilerOptions\": {\n"
		     "        \"module\": \"esnext\",\n"
		     "        \"strict\": true\n"
		     "    }\n"
		     "}"},
		    {tspath::combinePaths(packageBDir, {"package.json"}),
		     "{\n"
		     "    \"name\": \"pkg-b\",\n"
		     "    \"dependencies\": { \"pkg-a\": \"*\" }\n"
		     "}"},
		    {tspath::combinePaths(packageBDir, {"index.ts"}),
		     "import { a } from \"pkg-a\";\n"
		     "export const b = a;\n"},
		    // Circular workspace links
		    {tspath::combinePaths(packageADir, {"node_modules", "pkg-b"}),
		     vfs::vfstest::Symlink(packageBDir)},
		    {tspath::combinePaths(packageBDir, {"node_modules", "pkg-a"}),
		     vfs::vfstest::Symlink(packageADir)},
		};

		auto setupPair = projecttestutil::Setup(files);
		auto* session = setupPair.first;
		auto utils = setupPair.second;
		t->Cleanup([session, utils] { session->Close(); session->WaitForBackgroundTasks(); });
		auto ctx = gostd::contextBackground();
		auto consumerAURI = lsconv::FileNameToDocumentURI(consumerA);
		session->DidOpenFile(ctx, consumerAURI, 1, std::get<std::string>(files[consumerA]),
		                   lsproto::LanguageKindTypeScript);

		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, consumerAURI);
		gotest::assert::NilError(t, pair.second);

		auto* stats = autoImportStats(t, session);
		auto projectBucket = singleBucket(t, stats->ProjectBuckets);
		gotest::assert::Equal(t, 3, projectBucket.FileCount,
		                      "expected all pkg-a project files despite circular workspace symlinks");
	});

	t->Run("changed fileExcludePatterns triggers bucket rebuild", [](T* t) {
		t->Parallel();
		auto fixture = autoimporttestutil::SetupLifecycleSession(t, std::string(lifecycleProjectRoot), 1);
		auto* session = fixture->Session();
		auto project = fixture->SingleProject();
		auto mainFile = project.File(0);

		auto ctx = gostd::contextBackground();

		// Open file and build auto-imports initially
		session->DidOpenFile(ctx, mainFile.URI(), 1, mainFile.Content(),
		                   lsproto::LanguageKindTypeScript);
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, mainFile.URI());
		gotest::assert::NilError(t, pair.second);

		// Verify buckets are clean after initial build
		auto* stats = autoImportStats(t, session);
		auto projectBucket = singleBucket(t, stats->ProjectBuckets);
		auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, false, projectBucket.State.Dirty());
		gotest::assert::Equal(t, false, nodeModulesBucket.State.Dirty());

		// IsPreparedForImportingFile should return true with no exclude patterns
		auto* snapshot = session->Snapshot();
		auto* defaultProject = snapshot->GetDefaultProject(mainFile.URI());
		gotest::assert::Assert(t, defaultProject != nullptr);
		auto* projectID = autoimport::InternProjectID(project::idString(defaultProject->ID()));
		auto preferences = ls::lsutil::NewDefaultUserPreferences();
		preferences.IncludeCompletionsForModuleExports = Tristate::True;
		preferences.IncludeCompletionsForImportStatements = Tristate::True;
		bool isPrepared = autoimport::IsPreparedForImportingFile(snapshot->AutoImportRegistry(), mainFile.FileName(), projectID, preferences);
		gotest::assert::Assert(t, isPrepared);

		// Change the file exclude patterns preference
		auto newPreferences = ls::lsutil::NewDefaultUserPreferences();
		newPreferences.IncludeCompletionsForModuleExports = Tristate::True;
		newPreferences.IncludeCompletionsForImportStatements = Tristate::True;
		newPreferences.AutoImportFileExcludePatterns = {"**/node_modules/**/*.d.ts"};
		session->Configure(newPreferences);

		// IsPreparedForImportingFile should return false since exclude patterns changed
		auto* snapshot2 = session->Snapshot();
		bool isPrepared2 = autoimport::IsPreparedForImportingFile(snapshot2->AutoImportRegistry(), mainFile.FileName(), projectID, newPreferences);
		gotest::assert::Assert(t, !isPrepared2);

		// After GetCurrentLanguageServiceWithAutoImports, buckets should be rebuilt
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, mainFile.URI());
		gotest::assert::NilError(t, pair.second);

		// IsPreparedForImportingFile should return true now that buckets are rebuilt
		auto* snapshot3 = session->Snapshot();
		bool isPrepared3 = autoimport::IsPreparedForImportingFile(snapshot3->AutoImportRegistry(), mainFile.FileName(), projectID, newPreferences);
		gotest::assert::Assert(t, isPrepared3,
		                       "IsPreparedForImportingFile should return true after bucket rebuild with new fileExcludePatterns");
	});

	t->Run("dedupes packages that resolve to same realpath across ancestor node_modules buckets", [](T* t) {
		t->Parallel();

		std::string repoRoot = "/home/src/autoimport-realpath-dedupe";
		std::string appDir = tspath::combinePaths(repoRoot, {"apps", "web"});
		std::string sharedPkgDir = tspath::combinePaths(repoRoot, {"node_modules", "shared"});
		std::string appIndex = tspath::combinePaths(appDir, {"src", "index.ts"});

		projecttestutil::FileMap files = {
		    {tspath::combinePaths(repoRoot, {"package.json"}),
		     "{\n"
		     "    \"name\": \"repo-root\",\n"
		     "    \"private\": true,\n"
		     "    \"dependencies\": { \"shared\": \"*\" }\n"
		     "}"},
		    {tspath::combinePaths(repoRoot, {"tsconfig.json"}),
		     "{\n"
		     "    \"compilerOptions\": {\n"
		     "        \"module\": \"esnext\",\n"
		     "        \"target\": \"esnext\",\n"
		     "        \"strict\": true\n"
		     "    },\n"
		     "    \"include\": [\"apps/**/*\"]\n"
		     "}"},
		    {tspath::combinePaths(appDir, {"package.json"}),
		     "{\n"
		     "    \"name\": \"web\",\n"
		     "    \"private\": true,\n"
		     "    \"dependencies\": { \"shared\": \"*\" }\n"
		     "}"},
		    {tspath::combinePaths(appDir, {"tsconfig.json"}),
		     "{\n"
		     "    \"compilerOptions\": {\n"
		     "        \"module\": \"esnext\",\n"
		     "        \"target\": \"esnext\",\n"
		     "        \"strict\": true\n"
		     "    },\n"
		     "    \"include\": [\"src\"]\n"
		     "}"},
		    {appIndex, "export const app = 1;\n"},
		    {tspath::combinePaths(sharedPkgDir, {"package.json"}),
		     "{\n"
		     "    \"name\": \"shared\",\n"
		     "    \"version\": \"1.0.0\",\n"
		     "    \"types\": \"index.d.ts\"\n"
		     "}"},
		    {tspath::combinePaths(sharedPkgDir, {"index.d.ts"}),
		     "export declare const sharedValue: 1;\n"},
		    {tspath::combinePaths(appDir, {"node_modules", "shared"}),
		     vfs::vfstest::Symlink(sharedPkgDir)},
		};

		auto setupPair = projecttestutil::Setup(files);
		auto* session = setupPair.first;
		auto utils = setupPair.second;
		t->Cleanup([session, utils] { session->Close(); session->WaitForBackgroundTasks(); });

		auto ctx = gostd::contextBackground();
		auto appURI = lsconv::FileNameToDocumentURI(appIndex);
		session->DidOpenFile(ctx, appURI, 1, std::get<std::string>(files[appIndex]),
		                   lsproto::LanguageKindTypeScript);

		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, appURI);
		gotest::assert::NilError(t, pair.second);

		auto* stats = autoImportStats(t, session);
		gotest::assert::Equal(t, static_cast<int>(stats->NodeModulesBuckets.size()), 2,
		                      "expected both app and repo node_modules buckets");
		gotest::assert::Equal(t, stats->UniquePackageCount, 1,
		                      "expected one unique package after realpath dedup");
	});
}

void TestContentMappedNodeModulesFileUsesProjectBucket(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	projecttestutil::FileMap files = {
	    {"/home/project/tsconfig.json",
	     "{\n"
	     "    \"compilerOptions\": { \"module\": \"esnext\", \"moduleResolution\": \"bundler\", \"strict\": true, \"skipLibCheck\": true },\n"
	     "    \"contentMappers\": [ { \"package\": \"mapper\", \"extensions\": [\".vue\"] } ]\n"
	     "}"},
	    {"/home/project/node_modules/mapper/package.json",
	     contentmappertest::PackageJSON(contentmappertest::ComponentMapper)},
	    {"/home/project/node_modules/profile-package/ProfileCard.vue",
	     "<component name=\"ProfileCard\">\n"
	     "<script lang=\"ts\">\n"
	     "export const profileTitle = \"Profile\";\n"
	     "</script>"},
	    {"/home/project/node_modules/profile-package/HiddenCard.vue",
	     "<component name=\"HiddenCard\">\n"
	     "<script lang=\"ts\">\n"
	     "export const hiddenTitle = \"Hidden\";\n"
	     "</script>"},
	    {"/home/project/node_modules/profile-package/ordinary.ts",
	     "export const ordinary = true;"},
	    {"/home/project/load.ts",
	     "import \"profile-package/ProfileCard.vue\";\n"
	     "import \"profile-package/ordinary\";"},
	    {"/home/project/main.ts", "profileTitle;"},
	};
	project::SessionOptions options;
	options.CurrentDirectory = "/home/project";
	options.DefaultLibraryPath = bundled::LibPath();
	options.TypingsLocation = std::string(projecttestutil::TestTypingsLocation);
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.RunExternalCode = true;
	auto initPair = projecttestutil::GetSessionInitOptions(files, &options, nullptr);
	initPair.first->Spawner = contentmappertest::NewSpawner();
	auto* session = project::NewSession(initPair.first.get());
	auto utils = initPair.second;
	t->Cleanup([session, utils] { session->Close(); session->WaitForBackgroundTasks(); });

	auto ctx = gostd::contextBackground();
	auto mainURI = lsproto::DocumentUri("file:///home/project/main.ts");
	session->DidOpenFile(ctx, mainURI, 1, std::get<std::string>(files["/home/project/main.ts"]),
	                   lsproto::LanguageKindTypeScript);
	auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, mainURI);
	gotest::assert::NilError(t, pair.second);
	session->WaitForBackgroundTasks();

	auto projectBucket = singleBucket(t, autoImportStats(t, session)->ProjectBuckets);
	gotest::assert::Equal(t, projectBucket.FileCount, 3,
	                      "expected the two project roots and referenced mapped package file");
}

void TestHiddenDirectoriesInNodeModules(T* t) {
	t->Parallel();
	t->Run("deep import through subdirectory package.json in hidden store", [](T* t) {
		// Simulates a realistic scenario where:
		// 1. A package is symlinked from node_modules into a hidden store directory
		// 2. The user does a deep import like `import { debug } from "some-pkg/debug"`
		// 3. The package has NO "exports" field, so resolution uses the nested
		//    package.json at some-pkg/debug/package.json
		// 4. That nested package.json has no "name" or "version" (just {"main":"..."}),
		//    which is completely normal for subdirectory package.json files
		// 5. getPackageId uses the nested package.json (not the root), fails to get
		//    a name/version, so PackageId is empty
		// 6. collectPackageNames falls through to GetPackageNameFromDirectory, which
		//    extracts ".yarn-store" from the realpath after /node_modules/
		t->Parallel();
		std::string projectRoot = "/home/src/fuse-project";
		std::string storeDir = projectRoot + "/node_modules/.yarn-store";
		std::string pkgStoreDir = storeDir + "/some-pkg-npm-1.0.0-abc123/package";

		projecttestutil::FileMap files = {
		    {projectRoot + "/tsconfig.json",
		     "{\n"
		     "    \"compilerOptions\": {\n"
		     "        \"module\": \"commonjs\",\n"
		     "        \"target\": \"es2020\",\n"
		     "        \"strict\": true\n"
		     "    }\n"
		     "}"},
		    {projectRoot + "/package.json",
		     "{\n"
		     "    \"name\": \"test-project\",\n"
		     "    \"dependencies\": {\n"
		     "        \"some-pkg\": \"*\",\n"
		     "        \"real-package\": \"*\"\n"
		     "    }\n"
		     "}"},
		    // Deep import: "some-pkg/debug" — resolves through the subdirectory package.json
		    {projectRoot + "/index.ts", "import { debug } from \"some-pkg/debug\";"},

		    // Real package that should be indexed normally
		    {projectRoot + "/node_modules/real-package/package.json",
		     "{\"name\":\"real-package\",\"version\":\"1.0.0\",\"types\":\"index.d.ts\"}"},
		    {projectRoot + "/node_modules/real-package/index.d.ts",
		     "export declare const realExport: number;\n"},

		    // Symlink: node_modules/some-pkg -> .yarn-store/.../package/
		    {projectRoot + "/node_modules/some-pkg", vfs::vfstest::Symlink(pkgStoreDir)},

		    // Root package.json with name+version but NO "exports" field.
		    {pkgStoreDir + "/package.json",
		     "{\"name\":\"some-pkg\",\"version\":\"1.0.0\",\"types\":\"index.d.ts\"}"},
		    {pkgStoreDir + "/index.d.ts",
		     "export declare const something: number;\n"},
		    // Subdirectory package.json for the deep import — no name or version,
		    // just a main field.
		    {pkgStoreDir + "/debug/package.json",
		     "{\"main\":\"./debug.js\",\"types\":\"./debug.d.ts\"}"},
		    {pkgStoreDir + "/debug/debug.d.ts",
		     "export declare function debug(msg: string): void;\n"},
		    {pkgStoreDir + "/debug/debug.js",
		     "exports.debug = function(msg) { console.log(msg); };\n"},

		    // Other content in the hidden store that should never be crawled
		    {storeDir + "/other-pkg-npm-2.0.0-def456/package/package.json",
		     "{\"name\":\"other-pkg\",\"version\":\"1.0.0\",\"types\":\"index.d.ts\"}"},
		    {storeDir + "/other-pkg-npm-2.0.0-def456/package/index.d.ts",
		     "export declare const other: string;\n"},
		};

		auto setupPair = projecttestutil::Setup(files);
		auto* session = setupPair.first;
		auto utils = setupPair.second;
		t->Cleanup([session, utils] { session->Close(); session->WaitForBackgroundTasks(); });

		auto ctx = gostd::contextBackground();
		auto indexURI = lsproto::DocumentUri("file://" + projectRoot + "/index.ts");
		session->DidOpenFile(ctx, indexURI, 1, std::get<std::string>(files[projectRoot + "/index.ts"]),
		                   lsproto::LanguageKindTypeScript);

		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, indexURI);
		gotest::assert::NilError(t, pair.second);

		auto* stats = autoImportStats(t, session);
		auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);

		// .yarn-store must not appear as a dependency name. If it does,
		// extractPackages will try to process the entire hidden directory
		// (ReadDirectory **/*), which is the CPU/memory blowup.
		gotest::assert::Assert(t, nodeModulesBucket.DependencyNames != nullptr,
		                       "DependencyNames should not be nil");
		for (auto& name : nodeModulesBucket.DependencyNames->Keys()) {
			gotest::assert::Assert(t, name[0] != '.',
			                       "hidden directory should not appear as a dependency name");
		}
	});
}

void TestAutoImportEntrypointDirectorySearch(T* t) {
	t->Parallel();

	std::string projectRoot = "/home/src/entrypoint-search";
	std::string nodeModulesDir = projectRoot + "/node_modules";
	std::string pkgDir = nodeModulesDir + "/my-pkg";

	auto makeFiles = [&]() -> projecttestutil::FileMap {
		return {
		    {projectRoot + "/tsconfig.json",
		     "{\n"
		     "    \"compilerOptions\": {\n"
		     "        \"module\": \"commonjs\",\n"
		     "        \"target\": \"es2020\"\n"
		     "    }\n"
		     "}"},
		    {projectRoot + "/package.json",
		     "{\n"
		     "    \"name\": \"test-project\",\n"
		     "    \"dependencies\": { \"my-pkg\": \"*\" }\n"
		     "}"},
		    {projectRoot + "/index.ts", "import { main } from \"my-pkg\";"},
		    // Package with NO "exports" field and multiple files.
		    // Without the preference, only the main entrypoint is found.
		    // With the preference, directory search finds all .d.ts files.
		    {pkgDir + "/package.json", "{\"name\":\"my-pkg\",\"version\":\"1.0.0\",\"types\":\"index.d.ts\"}"},
		    {pkgDir + "/index.d.ts", "export declare const main: number;\n"},
		    {pkgDir + "/extra.d.ts", "export declare const extra: string;\n"},
		    {pkgDir + "/nested/deep.d.ts", "export declare const deep: boolean;\n"},
		    {pkgDir + "/nested/deeper.d.ts", "export declare const deeper: boolean;\n"},
		};
	};
	projecttestutil::FileMap files = makeFiles();
	std::string indexFile = projectRoot + "/index.ts";
	auto indexURI = lsproto::DocumentUri("file://" + indexFile);

	t->Run("default limits to main entrypoint", [files, indexURI, indexFile](T* t) {
		t->Parallel();
		auto setupPair = projecttestutil::Setup(files);
		auto* session = setupPair.first;
		auto utils = setupPair.second;
		t->Cleanup([session, utils] { session->Close(); session->WaitForBackgroundTasks(); });

		auto ctx = gostd::contextBackground();
		session->DidOpenFile(ctx, indexURI, 1, std::get<std::string>(files.at(indexFile)),
		                   lsproto::LanguageKindTypeScript);

		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, indexURI);
		gotest::assert::NilError(t, pair.second);

		auto* stats = autoImportStats(t, session);
		auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		// Without the preference, only the main entrypoint (index.d.ts) should be found
		gotest::assert::Equal(t, 1, nodeModulesBucket.FileCount,
		                      "expected only 1 file (main entrypoint) by default");
	});

	t->Run("autoImportEntrypointDirectorySearch enables all files", [files, indexURI, indexFile](T* t) {
		t->Parallel();
		auto setupPair = projecttestutil::Setup(files);
		auto* session = setupPair.first;
		auto utils = setupPair.second;
		t->Cleanup([session, utils] { session->Close(); session->WaitForBackgroundTasks(); });

		auto prefs = ls::lsutil::NewDefaultUserPreferences();
		prefs.AutoImportEntrypointDirectorySearch = Tristate::True;
		session->Configure(prefs);

		auto ctx = gostd::contextBackground();
		session->DidOpenFile(ctx, indexURI, 1, std::get<std::string>(files.at(indexFile)),
		                   lsproto::LanguageKindTypeScript);

		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, indexURI);
		gotest::assert::NilError(t, pair.second);

		auto* stats = autoImportStats(t, session);
		auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		// With the preference, all 4 .d.ts files should be found via directory search
		gotest::assert::Assert(t, nodeModulesBucket.FileCount >= 4,
		                       "expected at least 4 files from directory search");
	});

	t->Run("changing preference triggers rebuild", [files, indexURI, indexFile](T* t) {
		t->Parallel();
		auto setupPair = projecttestutil::Setup(files);
		auto* session = setupPair.first;
		auto utils = setupPair.second;
		t->Cleanup([session, utils] { session->Close(); session->WaitForBackgroundTasks(); });

		auto ctx = gostd::contextBackground();
		session->DidOpenFile(ctx, indexURI, 1, std::get<std::string>(files.at(indexFile)),
		                   lsproto::LanguageKindTypeScript);

		// Build auto-imports with default preferences (directory search disabled)
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, indexURI);
		gotest::assert::NilError(t, pair.second);

		auto* stats = autoImportStats(t, session);
		auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, 1, nodeModulesBucket.FileCount, "expected only 1 file initially");

		// Now enable directory search
		auto prefs = ls::lsutil::NewDefaultUserPreferences();
		prefs.AutoImportEntrypointDirectorySearch = Tristate::True;
		session->Configure(prefs);

		// Registry should report not prepared (preference changed)
		auto* snapshot = session->Snapshot();
		auto* defaultProject = snapshot->GetDefaultProject(indexURI);
		gotest::assert::Assert(t, defaultProject != nullptr);
		auto* projectID = autoimport::InternProjectID(project::idString(defaultProject->ID()));
		bool isPrepared = autoimport::IsPreparedForImportingFile(snapshot->AutoImportRegistry(), indexFile, projectID, prefs);
		gotest::assert::Assert(t, !isPrepared,
		                       "registry should not be prepared after preference change");

		// Rebuild
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, indexURI);
		gotest::assert::NilError(t, pair.second);

		stats = autoImportStats(t, session);
		nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Assert(t, nodeModulesBucket.FileCount >= 4,
		                       "expected at least 4 files after rebuild with directory search enabled");
	});

	t->Run("deep import from program update enables recursive search for that package", [files, indexURI, indexFile](T* t) {
		t->Parallel();
		auto setupPair = projecttestutil::Setup(files);
		auto* session = setupPair.first;
		auto utils = setupPair.second;
		t->Cleanup([session, utils] { session->Close(); session->WaitForBackgroundTasks(); });

		auto ctx = gostd::contextBackground();
		session->DidOpenFile(ctx, indexURI, 1, std::get<std::string>(files.at(indexFile)),
		                   lsproto::LanguageKindTypeScript);

		// Initial build with top-level import only ("my-pkg", not a deep import)
		auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, indexURI);
		gotest::assert::NilError(t, pair.second);

		auto* stats = autoImportStats(t, session);
		auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Equal(t, 1, nodeModulesBucket.FileCount,
		                      "expected only 1 file (main entrypoint) before deep import");

		// Now update the program to add a deep import from the same package
		std::string newContent =
		    "import { main } from \"my-pkg\";\n"
		    "import { deep } from \"my-pkg/nested/deep\";\n";
		session->DidChangeFile(ctx, indexURI, 2, wholeDocChange(newContent));

		// After the program update, auto-imports should detect the deep import and
		// enable recursive directory search for my-pkg, finding all .d.ts files.
		pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, indexURI);
		gotest::assert::NilError(t, pair.second);

		stats = autoImportStats(t, session);
		nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
		gotest::assert::Assert(t, nodeModulesBucket.FileCount >= 4,
		                       "expected at least 4 files after deep import triggers recursive search");
	});
}

void TestUpdateIndexesConcurrentMapSafety(T* t) {
	t->Parallel();
	std::string projectRoot = "/home/src/autoimport-fallback-race";
	constexpr int packageCount = 40;

	projecttestutil::FileMap files = {
	    {projectRoot + "/tsconfig.json",
	     "{\n"
	     "    \"compilerOptions\": { \"module\": \"esnext\", \"target\": \"esnext\", \"strict\": true }\n"
	     "}"},
	    {projectRoot + "/index.ts", "export {};\n"},
	};
	for (int i = 0; i < packageCount; i++) {
		std::string pkgDir = gostd::sprintf("%s/node_modules/pkg%d", {projectRoot, i});
		files[pkgDir + "/package.json"] = gostd::sprintf(
		    "{\"name\":\"pkg%d\",\"version\":\"1.0.0\",\"main\":\"index.js\"}", {i});
		files[pkgDir + "/index.js"] = "module.exports = {};\n";
		std::string typesDir = gostd::sprintf("%s/node_modules/@types/pkg%d", {projectRoot, i});
		files[typesDir + "/package.json"] = gostd::sprintf(
		    "{\"name\":\"@types/pkg%d\",\"version\":\"1.0.0\",\"types\":\"index.d.ts\"}", {i});
		files[typesDir + "/index.d.ts"] =
		    gostd::sprintf("export declare const foo%d: number;\n", {i});
	}

	auto setupPair = projecttestutil::Setup(files);
	auto* session = setupPair.first;
	auto utils = setupPair.second;
	t->Cleanup([session, utils] { session->Close(); session->WaitForBackgroundTasks(); });

	auto ctx = gostd::contextBackground();
	auto indexURI = lsproto::DocumentUri("file://" + projectRoot + "/index.ts");
	session->DidOpenFile(ctx, indexURI, 1, "export {};\n", lsproto::LanguageKindTypeScript);

	auto pair = session->GetCurrentLanguageServiceWithAutoImports(ctx, indexURI);
	gotest::assert::NilError(t, pair.second);

	auto* stats = autoImportStats(t, session);
	auto nodeModulesBucket = singleBucket(t, stats->NodeModulesBuckets);
	gotest::assert::Equal(t, nodeModulesBucket.ExportCount, packageCount);
}

}  // namespace

REGISTER_UNIT_TEST("ls/autoimport.TestRegistryLifecycle", TestRegistryLifecycle);
REGISTER_UNIT_TEST("ls/autoimport.TestContentMappedNodeModulesFileUsesProjectBucket",
                   TestContentMappedNodeModulesFileUsesProjectBucket);
REGISTER_UNIT_TEST("ls/autoimport.TestHiddenDirectoriesInNodeModules",
                   TestHiddenDirectoriesInNodeModules);
REGISTER_UNIT_TEST("ls/autoimport.TestAutoImportEntrypointDirectorySearch",
                   TestAutoImportEntrypointDirectorySearch);
REGISTER_UNIT_TEST("ls/autoimport.TestUpdateIndexesConcurrentMapSafety",
                   TestUpdateIndexesConcurrentMapSafety);
