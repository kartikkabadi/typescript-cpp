// Port of tsc/internal/project/projectreferencesprogram_test.go.
#include <string>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/project/project.h"
#include "internal/project/session.h"
#include "internal/project/snapshot.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace gostd = tsc::gostd;
namespace lsconv = tsc::lsconv;
namespace lsproto = tsc::lsp::lsproto;
namespace project = tsc::project;
namespace projecttestutil = tsc::testutil::projecttestutil;
namespace tspath = tsc::tspath;
namespace vfstest = tsc::vfs::vfstest;
using tsc::gostd::testing::T;

const std::string& prpFileText(const projecttestutil::FileMap& files,
                               const std::string& name) {
	return std::get<std::string>(files.at(name));
}

int64_t prpProjectCount(project::Snapshot* snapshot) {
	return static_cast<int64_t>(
	    snapshot->ProjectCollection->Projects().size());
}

// filesForReferencedProjectProgram
projecttestutil::FileMap filesForReferencedProjectProgram(
    bool disableSourceOfProjectReferenceRedirect) {
	return projecttestutil::FileMap{
	    {"/user/username/projects/myproject/main/tsconfig.json",
	     gostd::sprintf(
	         R"({
			"compilerOptions": {
				"composite": true%s
			},
			"references": [{ "path": "../dependency" }]
		})",
	         {disableSourceOfProjectReferenceRedirect
	              ? std::string(
	                  ", \"disableSourceOfProjectReferenceRedirect\": true")
	              : std::string("")})},
	    {"/user/username/projects/myproject/main/main.ts", std::string(R"(
			import {
				fn1,
				fn2,
				fn3,
				fn4,
				fn5
			} from '../decls/fns'
			fn1();
			fn2();
			fn3();
			fn4();
			fn5();
		)")},
	    {"/user/username/projects/myproject/dependency/tsconfig.json",
	     std::string(R"({
			"compilerOptions": {
				"composite": true,
				"declarationDir": "../decls"
			},
		})")},
	    {"/user/username/projects/myproject/dependency/fns.ts",
	     std::string(R"(
			export function fn1() { }
			export function fn2() { }
			export function fn3() { }
			export function fn4() { }
			export function fn5() { }
		)")},
	};
}

// addConfigForPackage
void addConfigForPackage(projecttestutil::FileMap& files,
                         const std::string& packageName,
                         bool preserveSymlinks,
                         std::vector<std::string> references) {
	std::string compilerOptions = R"(
		"outDir":    "lib",
		"rootDir":   "src",
		"composite": true)";
	if (preserveSymlinks) {
		compilerOptions += R"(,
		"preserveSymlinks": true)";
	}
	std::string referencesJson = "[";
	bool first = true;
	for (auto& ref : references) {
		if (!first) referencesJson += ", ";
		referencesJson += gostd::sprintf(
		    R"({
			"path": "%s"
		})",
		    {ref});
		first = false;
	}
	referencesJson += "]";
	files[gostd::sprintf(
	    "/user/username/projects/myproject/packages/%s/tsconfig.json",
	    {packageName})] =
	    gostd::sprintf(
	        R"({
	"compilerOptions": {%s
	},
	"include":         ["src"],
	"references":      %s
})",
	        {compilerOptions, referencesJson});
}

struct SymlinkRefFiles {
	projecttestutil::FileMap files;
	std::string aTest;
	std::string bFoo;
	std::string bBar;
};

// filesForSymlinkReferences
SymlinkRefFiles filesForSymlinkReferences(bool preserveSymlinks,
                                          const std::string& scope) {
	SymlinkRefFiles r;
	r.aTest =
	    "/user/username/projects/myproject/packages/A/src/index.ts";
	r.bFoo =
	    "/user/username/projects/myproject/packages/B/src/index.ts";
	r.bBar = "/user/username/projects/myproject/packages/B/src/bar.ts";
	r.files = projecttestutil::FileMap{
	    {"/user/username/projects/myproject/packages/B/package.json",
	     std::string(R"({
			"main": "lib/index.js",
			"types": "lib/index.d.ts"
		})")},
	    {r.aTest,
	     gostd::sprintf(R"(
			import { foo } from '%sb';
			import { bar } from '%sb/lib/bar';
			foo();
			bar();
		)",
	                    {scope, scope})},
	    {r.bFoo, std::string("export function foo() { }")},
	    {r.bBar, std::string("export function bar() { }")},
	    {gostd::sprintf(
	         "/user/username/projects/myproject/node_modules/%sb",
	         {scope}),
	     vfstest::Symlink(
	         "/user/username/projects/myproject/packages/B")},
	};
	addConfigForPackage(r.files, "A", preserveSymlinks, {"../B"});
	addConfigForPackage(r.files, "B", preserveSymlinks, {});
	return r;
}

// filesForSymlinkReferencesInSubfolder
SymlinkRefFiles filesForSymlinkReferencesInSubfolder(
    bool preserveSymlinks, const std::string& scope) {
	SymlinkRefFiles r;
	r.aTest = "/user/username/projects/myproject/packages/A/src/test.ts";
	r.bFoo = "/user/username/projects/myproject/packages/B/src/foo.ts";
	r.bBar =
	    "/user/username/projects/myproject/packages/B/src/bar/foo.ts";
	r.files = projecttestutil::FileMap{
	    {"/user/username/projects/myproject/packages/B/package.json",
	     std::string(R"({})")},
	    {r.aTest,
	     gostd::sprintf(R"(
			import { foo } from '%sb/lib/foo';
			import { bar } from '%sb/lib/bar/foo';
			foo();
			bar();
		)",
	                    {scope, scope})},
	    {r.bFoo, std::string("export function foo() { }")},
	    {r.bBar, std::string("export function bar() { }")},
	    {gostd::sprintf(
	         "/user/username/projects/myproject/node_modules/%sb",
	         {scope}),
	     vfstest::Symlink(
	         "/user/username/projects/myproject/packages/B")},
	};
	addConfigForPackage(r.files, "A", preserveSymlinks, {"../B"});
	addConfigForPackage(r.files, "B", preserveSymlinks, {});
	return r;
}

// filesForDirectorySubpathSymlinkReferences
SymlinkRefFiles filesForDirectorySubpathSymlinkReferences(
    const std::string& scope) {
	SymlinkRefFiles r;
	r.aTest =
	    "/user/username/projects/myproject/packages/a/src/index.ts";
	r.bFoo =
	    "/user/username/projects/myproject/packages/b/src/File/index.ts";
	r.files = projecttestutil::FileMap{
	    {"/user/username/projects/myproject/packages/b/package.json",
	     std::string(R"({
			"main": "lib/index.js",
			"types": "lib/index.d.ts"
		})")},
	    {r.aTest,
	     gostd::sprintf(R"(
			import { helper } from "%sb/lib/File";
			export const result: number = helper();
		)",
	                    {scope})},
	    {r.bFoo,
	     std::string(
	         "export function helper(): number { return 1; }")},
	    {gostd::sprintf(
	         "/user/username/projects/myproject/node_modules/%sb",
	         {scope}),
	     vfstest::Symlink(
	         "/user/username/projects/myproject/packages/b")},
	};
	addConfigForPackage(r.files, "a", false, {"../b"});
	addConfigForPackage(r.files, "b", false, {});
	return r;
}

void TestProjectReferencesProgram(T* t) {
	t->Parallel();

	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	t->Run("program for referenced project", [](T* t) {
		t->Parallel();
		auto files = filesForReferencedProjectProgram(false);
		auto [session, utils] = projecttestutil::Setup(files);
		auto* snapshot = session->Snapshot();
		assert::Equal(t, prpProjectCount(snapshot), int64_t(0));

		lsproto::DocumentUri uri =
		    "file:///user/username/projects/myproject/main/main.ts";
		session->DidOpenFile(
		    t->Context(), uri, 1,
		    prpFileText(files,
		                "/user/username/projects/myproject/main/main.ts"),
		    lsproto::LanguageKindTypeScript);

		snapshot = session->Snapshot();
		assert::Equal(t, prpProjectCount(snapshot), int64_t(1));
		auto projects = snapshot->ProjectCollection->Projects();
		auto* p = projects[0];
		assert::Equal(t, p->Kind, project::KindConfigured);

		auto* file = p->Program->GetSourceFileByPath(tspath::Path(
		    "/user/username/projects/myproject/dependency/fns.ts"));
		assert::Assert(t, file != nullptr);
		auto* dtsFile = p->Program->GetSourceFileByPath(tspath::Path(
		    "/user/username/projects/myproject/decls/fns.d.ts"));
		assert::Assert(t, dtsFile == nullptr);
	});

	t->Run("program with disableSourceOfProjectReferenceRedirect",
	       [](T* t) {
		    t->Parallel();
		    auto files = filesForReferencedProjectProgram(true);
		    files["/user/username/projects/myproject/decls/fns.d.ts"] =
		        std::string(R"(
			export declare function fn1(): void;
			export declare function fn2(): void;
			export declare function fn3(): void;
			export declare function fn4(): void;
			export declare function fn5(): void;
		)");
		    auto [session, utils] = projecttestutil::Setup(files);
		    auto* snapshot = session->Snapshot();
		    assert::Equal(t, prpProjectCount(snapshot), int64_t(0));

		    lsproto::DocumentUri uri =
		        "file:///user/username/projects/myproject/main/main.ts";
		    session->DidOpenFile(
		        t->Context(), uri, 1,
		        prpFileText(
		            files,
		            "/user/username/projects/myproject/main/main.ts"),
		        lsproto::LanguageKindTypeScript);

		    snapshot = session->Snapshot();
		    assert::Equal(t, prpProjectCount(snapshot), int64_t(1));
		    auto projects = snapshot->ProjectCollection->Projects();
		    auto* p = projects[0];
		    assert::Equal(t, p->Kind, project::KindConfigured);

		    auto* file = p->Program->GetSourceFileByPath(tspath::Path(
		        "/user/username/projects/myproject/dependency/fns.ts"));
		    assert::Assert(t, file == nullptr);
		    auto* dtsFile =
		        p->Program->GetSourceFileByPath(tspath::Path(
		            "/user/username/projects/myproject/decls/fns.d.ts"));
		    assert::Assert(t, dtsFile != nullptr);
	    });

	auto runSymlinkIndexTest = [](T* t, bool preserveSymlinks,
	                              const std::string& scope) {
		auto r = filesForSymlinkReferences(preserveSymlinks, scope);
		auto [session, utils] = projecttestutil::Setup(r.files);
		auto* snapshot = session->Snapshot();
		assert::Equal(t, prpProjectCount(snapshot), int64_t(0));

		auto uri = lsconv::FileNameToDocumentURI(r.aTest);
		session->DidOpenFile(t->Context(), uri, 1,
		                     prpFileText(r.files, r.aTest),
		                     lsproto::LanguageKindTypeScript);

		snapshot = session->Snapshot();
		assert::Equal(t, prpProjectCount(snapshot), int64_t(1));
		auto projects = snapshot->ProjectCollection->Projects();
		auto* p = projects[0];
		assert::Equal(t, p->Kind, project::KindConfigured);

		auto* fooFile = p->Program->GetSourceFile(r.bFoo);
		assert::Assert(t, fooFile != nullptr);
		auto* barFile = p->Program->GetSourceFile(r.bBar);
		assert::Assert(t, barFile != nullptr);
	};

	t->Run("references through symlink with index and typings",
	       [&](T* t) {
		    t->Parallel();
		    runSymlinkIndexTest(t, false, "");
	    });

	t->Run(
	    "references through symlink with index and typings with preserveSymlinks",
	    [&](T* t) {
		    t->Parallel();
		    runSymlinkIndexTest(t, true, "");
	    });

	t->Run(
	    "references through symlink with index and typings scoped package",
	    [&](T* t) {
		    t->Parallel();
		    runSymlinkIndexTest(t, false, "@issue/");
	    });

	t->Run(
	    "references through symlink with index and typings with scoped package preserveSymlinks",
	    [&](T* t) {
		    t->Parallel();
		    runSymlinkIndexTest(t, true, "@issue/");
	    });

	auto runSymlinkSubfolderTest = [](T* t, bool preserveSymlinks,
	                                  const std::string& scope) {
		auto r = filesForSymlinkReferencesInSubfolder(preserveSymlinks,
		                                              scope);
		auto [session, utils] = projecttestutil::Setup(r.files);
		auto* snapshot = session->Snapshot();
		assert::Equal(t, prpProjectCount(snapshot), int64_t(0));

		auto uri = lsconv::FileNameToDocumentURI(r.aTest);
		session->DidOpenFile(t->Context(), uri, 1,
		                     prpFileText(r.files, r.aTest),
		                     lsproto::LanguageKindTypeScript);

		snapshot = session->Snapshot();
		assert::Equal(t, prpProjectCount(snapshot), int64_t(1));
		auto projects = snapshot->ProjectCollection->Projects();
		auto* p = projects[0];
		assert::Equal(t, p->Kind, project::KindConfigured);

		auto* fooFile = p->Program->GetSourceFile(r.bFoo);
		assert::Assert(t, fooFile != nullptr);
		auto* barFile = p->Program->GetSourceFile(r.bBar);
		assert::Assert(t, barFile != nullptr);
	};

	t->Run("references through symlink referencing from subFolder",
	       [&](T* t) {
		    t->Parallel();
		    runSymlinkSubfolderTest(t, false, "");
	    });

	t->Run(
	    "references through symlink referencing from subFolder with preserveSymlinks",
	    [&](T* t) {
		    t->Parallel();
		    runSymlinkSubfolderTest(t, true, "");
	    });

	t->Run(
	    "references through symlink referencing from subFolder scoped package",
	    [&](T* t) {
		    t->Parallel();
		    runSymlinkSubfolderTest(t, false, "@issue/");
	    });

	t->Run(
	    "references through symlink referencing from subFolder with scoped package preserveSymlinks",
	    [&](T* t) {
		    t->Parallel();
		    runSymlinkSubfolderTest(t, true, "@issue/");
	    });

	auto runDirectorySubpathTest = [](T* t, const std::string& scope) {
		auto r = filesForDirectorySubpathSymlinkReferences(scope);
		auto [session, utils] = projecttestutil::Setup(r.files);
		auto uri = lsconv::FileNameToDocumentURI(r.aTest);
		session->DidOpenFile(t->Context(), uri, 1,
		                     prpFileText(r.files, r.aTest),
		                     lsproto::LanguageKindTypeScript);

		auto* snapshot = session->Snapshot();
		assert::Equal(t, prpProjectCount(snapshot), int64_t(1));
		auto* p = snapshot->ProjectCollection->Projects()[0];
		assert::Equal(t, p->Kind, project::KindConfigured);

		// The import must redirect to source, so the source file is part of the program...
		assert::Assert(t,
		               p->Program->GetSourceFile(r.bFoo) != nullptr);
		// ...and there must be no `TS2307: Cannot find module 'b/lib/File'` diagnostic.
		auto diagnostics = p->Program->GetSemanticDiagnostics(
		    p->Program->GetSourceFile(r.aTest));
		assert::Equal(t,
		              static_cast<int64_t>(diagnostics.size()),
		              int64_t(0));
	};

	t->Run(
	    "references through symlink with directory index subpath (issue 4373)",
	    [&](T* t) {
		    t->Parallel();
		    runDirectorySubpathTest(t, "");
	    });

	t->Run(
	    "references through symlink with directory index subpath scoped package",
	    [&](T* t) {
		    t->Parallel();
		    runDirectorySubpathTest(t, "@issue/");
	    });

	t->Run("when new file is added to referenced project", [](T* t) {
		t->Parallel();
		auto files = filesForReferencedProjectProgram(false);
		auto [session, utils] = projecttestutil::Setup(files);
		lsproto::DocumentUri uri =
		    "file:///user/username/projects/myproject/main/main.ts";
		session->DidOpenFile(
		    t->Context(), uri, 1,
		    prpFileText(files,
		                "/user/username/projects/myproject/main/main.ts"),
		    lsproto::LanguageKindTypeScript);
		auto* snapshot = session->Snapshot();
		assert::Equal(t, prpProjectCount(snapshot), int64_t(1));
		auto* programBefore =
		    snapshot->ProjectCollection->Projects()[0]->Program;

		auto werr = utils->FS()->WriteFile(
		    "/user/username/projects/myproject/dependency/fns2.ts",
		    "export const x = 2;");
		assert::Assert(t, werr.impl() == nullptr);
		lsproto::FileEvent ev;
		ev.Type = lsproto::FileChangeTypeCreated;
		ev.Uri =
		    "file:///user/username/projects/myproject/dependency/fns2.ts";
		session->DidChangeWatchedFiles(t->Context(), {&ev});

		auto [ls, err] =
		    session->GetLanguageService(t->Context(), uri);
		assert::NilError(t, err);
		delete ls;
		snapshot = session->Snapshot();
		assert::Equal(t, prpProjectCount(snapshot), int64_t(1));
		assert::Check(
		    t, snapshot->ProjectCollection->Projects()[0]->Program !=
		           programBefore);
	});

	t->Run(
	    "dropped project reference does not crash on later change to the dropped config",
	    [](T* t) {
		    t->Parallel();
		    // Regression test for https://github.com/microsoft/TypeScript/tsc/issues/3942.
		    // A project reference dropped from a tsconfig used to leave a stale entry in
		    // the referenced config's retainingProjects. When the referencing project was
		    // later deleted and the referenced config subsequently changed, the stale entry
		    // named a project that no longer existed, crashing markProjectsAffectedByConfigChanges.
		    projecttestutil::FileMap files{
		        {"/user/username/projects/myproject/main/tsconfig.json",
		         std::string(R"({
				"compilerOptions": {
					"composite": true
				},
				"references": [{ "path": "../dependency" }]
			})")},
		        {"/user/username/projects/myproject/main/main.ts",
		         std::string(R"(
				import { fn1 } from '../dependency/fns'
				fn1();
			})")},
		        {"/user/username/projects/myproject/dependency/tsconfig.json",
		         std::string(R"({
				"compilerOptions": {
					"composite": true
				}
			})")},
		        {"/user/username/projects/myproject/dependency/fns.ts",
		         std::string(R"(
				export function fn1() { }
			})")},
		        {"/user/username/projects/myproject/other/tsconfig.json",
		         std::string(R"({
				"compilerOptions": {
					"composite": true
				}
			})")},
		        {"/user/username/projects/myproject/other/other.ts",
		         std::string("export const y = 1;")},
		    };

		    auto [session, utils] = projecttestutil::Setup(files);

		    lsproto::DocumentUri mainURI =
		        "file:///user/username/projects/myproject/main/main.ts";
		    auto& mainContent = prpFileText(
		        files, "/user/username/projects/myproject/main/main.ts");
		    lsproto::DocumentUri otherURI =
		        "file:///user/username/projects/myproject/other/other.ts";
		    auto& otherContent = prpFileText(
		        files,
		        "/user/username/projects/myproject/other/other.ts");

		    // 1. Open main.ts. The main project's program resolves the `../dependency`
		    //    project reference, so the dependency config's retainingProjects gains
		    //    the main project path.
		    session->DidOpenFile(t->Context(), mainURI, 1, mainContent,
		                         lsproto::LanguageKindTypeScript);
		    session->WaitForBackgroundTasks();
		    auto* snapshot = session->Snapshot();
		    assert::Equal(t, prpProjectCount(snapshot), int64_t(1));
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/dependency/tsconfig.json")) !=
		            nullptr);

		    // 2. Remove the project reference from main/tsconfig.json and rebuild.
		    //    The new program no longer references dependency.
		    auto werr = utils->FS()->WriteFile(
		        "/user/username/projects/myproject/main/tsconfig.json",
		        R"({
			"compilerOptions": {
				"composite": true
			}
		})");
		    assert::Assert(t, werr.impl() == nullptr);
		    {
			    lsproto::FileEvent ev;
			    ev.Type = lsproto::FileChangeTypeChanged;
			    ev.Uri =
			        "file:///user/username/projects/myproject/main/tsconfig.json";
			    session->DidChangeWatchedFiles(t->Context(), {&ev});
		    }
		    auto [ls, err] =
		        session->GetLanguageService(t->Context(), mainURI);
		    assert::NilError(t, err);
		    delete ls;

		    // 3. Close main.ts and open an unrelated file. Opening triggers cleanup of
		    //    orphaned configured projects, deleting the main project.
		    session->DidCloseFile(t->Context(), mainURI);
		    session->DidOpenFile(t->Context(), otherURI, 1,
		                         otherContent,
		                         lsproto::LanguageKindTypeScript);
		    session->WaitForBackgroundTasks();
		    snapshot = session->Snapshot();
		    assert::Assert(
		        t,
		        snapshot->ProjectCollection->ConfiguredProject(
		            tspath::Path(
		                "/user/username/projects/myproject/main/tsconfig.json")) ==
		            nullptr);
		    // Dropping the reference releases main from dependency's retainingProjects,
		    // so the now-unreferenced dependency config is cleaned up and no stale entry
		    // survives to crash a later config change.
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/dependency/tsconfig.json")) ==
		            nullptr);

		    // 4. Change dependency/tsconfig.json and flush. This used to copy the stale
		    //    retainingProjects into affectedProjects and crash
		    //    markProjectsAffectedByConfigChanges loading the deleted main project.
		    werr = utils->FS()->WriteFile(
		        "/user/username/projects/myproject/dependency/tsconfig.json",
		        R"({
			"compilerOptions": {
				"composite": true,
				"strict": true
			}
		})");
		    assert::Assert(t, werr.impl() == nullptr);
		    {
			    lsproto::FileEvent ev;
			    ev.Type = lsproto::FileChangeTypeChanged;
			    ev.Uri =
			        "file:///user/username/projects/myproject/dependency/tsconfig.json";
			    session->DidChangeWatchedFiles(t->Context(), {&ev});
		    }
		    auto [ls2, err2] =
		        session->GetLanguageService(t->Context(), otherURI);
		    assert::NilError(t, err2);
		    delete ls2;
		    session->WaitForBackgroundTasks();
	    });
}

REGISTER_UNIT_TEST("project.TestProjectReferencesProgram",
                   TestProjectReferencesProgram);

}  // namespace
