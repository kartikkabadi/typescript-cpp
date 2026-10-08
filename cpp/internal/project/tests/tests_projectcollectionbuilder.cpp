// Port of tsc/internal/project/projectcollectionbuilder_test.go.
#include <map>
#include <string>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/ls/ls.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/project/project.h"
#include "internal/project/session.h"
#include "internal/project/snapshot.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace gostd = tsc::gostd;
namespace lsproto = tsc::lsp::lsproto;
namespace project = tsc::project;
namespace projecttestutil = tsc::testutil::projecttestutil;
namespace tspath = tsc::tspath;
using tsc::gostd::testing::T;

const std::string& pcbFileText(const projecttestutil::FileMap& files,
                               const std::string& name) {
	return std::get<std::string>(files.at(name));
}

int64_t pcbProjectCount(project::Snapshot* snapshot) {
	return static_cast<int64_t>(
	    snapshot->ProjectCollection->Projects().size());
}

std::string join(const std::vector<std::string>& v,
                 const std::string& sep) {
	std::string out;
	for (size_t i = 0; i < v.size(); i++) {
		if (i > 0) out += sep;
		out += v[i];
	}
	return out;
}

// filesForSolutionConfigFile
projecttestutil::FileMap filesForSolutionConfigFile(
    std::vector<std::string> solutionRefs, const std::string& compilerOptions,
    std::vector<std::string> ownFiles) {
	std::string compilerOptionsStr;
	if (!compilerOptions.empty()) {
		compilerOptionsStr = gostd::sprintf(
		    R"("compilerOptions": {
			%s
		},)", {compilerOptions});
	}
	std::string ownFilesStr;
	if (!ownFiles.empty()) {
		ownFilesStr = join(ownFiles, ",");
	}
	std::vector<std::string> refStrs;
	for (auto& ref : solutionRefs) {
		refStrs.push_back(
		    gostd::sprintf(R"({ "path": "%s" })", {ref}));
	}
	return projecttestutil::FileMap{
	    {"/user/username/projects/myproject/tsconfig.json",
	     gostd::sprintf(
	         R"({
			%s
			"files": [%s],
			"references": [
				%s
			]
		})",
	         {compilerOptionsStr, ownFilesStr, join(refStrs, ",")})},
	    {"/user/username/projects/myproject/tsconfig-src.json",
	     std::string(R"({
			"compilerOptions": {
				"composite": true,
				"outDir": "./target",
			},
			"include": ["./src/**/*"]
		})")},
	    {"/user/username/projects/myproject/src/main.ts", std::string(R"(
			import { foo } from './helpers/functions';
			export { foo };)")},
	    {"/user/username/projects/myproject/src/helpers/functions.ts",
	     std::string("export const foo = 1;")},
	};
}

// filesForIndirectProject
projecttestutil::FileMap filesForIndirectProject(
    int projectIndex, const std::string& compilerOptions) {
	return projecttestutil::FileMap{
	    {gostd::sprintf(
	         "/user/username/projects/myproject/tsconfig-indirect%d.json",
	         {int64_t(projectIndex)}),
	     gostd::sprintf(
	         R"({
			"compilerOptions": {
				"composite": true,
				"outDir": "./target/",
				%s
			},
			"files": [
				"./indirect%d/main.ts"
			],
			"references": [
				{
				"path": "./tsconfig-src.json"
				}
			]
		})",
	         {compilerOptions, int64_t(projectIndex)})},
	    {gostd::sprintf(
	         "/user/username/projects/myproject/indirect%d/main.ts",
	         {int64_t(projectIndex)}),
	     std::string("export const indirect = 1;")},
	};
}

// applyIndirectProjectFiles
void applyIndirectProjectFiles(projecttestutil::FileMap& files,
                               int projectIndex,
                               const std::string& compilerOptions) {
	for (auto& [k, v] :
	     filesForIndirectProject(projectIndex, compilerOptions)) {
		files[k] = v;
	}
}

void TestProjectCollectionBuilder(T* t) {
	t->Parallel();

	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	t->Run(
	    "when project found is solution referencing default project directly",
	    [](T* t) {
		    t->Parallel();
		    auto files = filesForSolutionConfigFile(
		        {"./tsconfig-src.json"}, "", {});
		    auto [session, utils] = projecttestutil::Setup(files);
		    lsproto::DocumentUri uri =
		        "file:///user/username/projects/myproject/src/main.ts";
		    auto& content = pcbFileText(
		        files,
		        "/user/username/projects/myproject/src/main.ts");

		    // Ensure configured project is found for open file
		    session->DidOpenFile(t->Context(), uri, 1, content,
		                         lsproto::LanguageKindTypeScript);
		    auto* snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    assert::Assert(
		        t,
		        snapshot->ProjectCollection->ConfiguredProject(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) !=
		            nullptr);

		    // Ensure request can use existing snapshot
		    auto [ls, err] =
		        session->GetLanguageService(t->Context(), uri);
		    assert::NilError(t, err);
		    delete ls;
		    auto* requestSnapshot = session->Snapshot();
		    assert::Equal(t, requestSnapshot, snapshot);

		    // Searched configs should be present while file is open
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json")) !=
		            nullptr,
		        "solution config should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) !=
		            nullptr,
		        "direct reference should be present");

		    // Close the file and open one in an inferred project
		    session->DidCloseFile(t->Context(), uri);
		    lsproto::DocumentUri dummyUri =
		        "file:///user/username/workspaces/dummy/dummy.ts";
		    session->DidOpenFile(t->Context(), dummyUri, 1,
		                         "const x = 1;",
		                         lsproto::LanguageKindTypeScript);
		    snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() !=
		               nullptr);

		    // Config files should have been released
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) ==
		            nullptr);
	    });

	t->Run(
	    "when project found is solution referencing default project indirectly",
	    [](T* t) {
		    t->Parallel();
		    auto files = filesForSolutionConfigFile(
		        {"./tsconfig-indirect1.json", "./tsconfig-indirect2.json"},
		        "", {});
		    applyIndirectProjectFiles(files, 1, "");
		    applyIndirectProjectFiles(files, 2, "");
		    auto [session, utils] = projecttestutil::Setup(files);
		    lsproto::DocumentUri uri =
		        "file:///user/username/projects/myproject/src/main.ts";
		    auto& content = pcbFileText(
		        files,
		        "/user/username/projects/myproject/src/main.ts");

		    // Ensure configured project is found for open file
		    session->DidOpenFile(t->Context(), uri, 1, content,
		                         lsproto::LanguageKindTypeScript);
		    auto* snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    auto* srcProject =
		        snapshot->ProjectCollection->ConfiguredProject(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json"));
		    assert::Assert(t, srcProject != nullptr);

		    // Verify the default project is the source project
		    auto* defaultProject = snapshot->GetDefaultProject(uri);
		    assert::Equal(t, defaultProject, srcProject);

		    // Searched configs should be present while file is open
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json")) !=
		            nullptr,
		        "solution config should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-indirect1.json")) !=
		            nullptr,
		        "direct reference should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) !=
		            nullptr,
		        "indirect reference should be present");

		    // Close the file and open one in an inferred project
		    session->DidCloseFile(t->Context(), uri);
		    lsproto::DocumentUri dummyUri =
		        "file:///user/username/workspaces/dummy/dummy.ts";
		    session->DidOpenFile(t->Context(), dummyUri, 1,
		                         "const x = 1;",
		                         lsproto::LanguageKindTypeScript);
		    snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() !=
		               nullptr);

		    // Config files should be released
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-indirect1.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-indirect2.json")) ==
		            nullptr);
	    });

	t->Run(
	    "when project found is solution with disableReferencedProjectLoad referencing default project directly",
	    [](T* t) {
		    t->Parallel();
		    auto files = filesForSolutionConfigFile(
		        {"./tsconfig-src.json"},
		        "\"disableReferencedProjectLoad\": true", {});
		    auto [session, utils] = projecttestutil::Setup(files);
		    lsproto::DocumentUri uri =
		        "file:///user/username/projects/myproject/src/main.ts";
		    auto& content = pcbFileText(
		        files,
		        "/user/username/projects/myproject/src/main.ts");

		    // Ensure no configured project is created due to disableReferencedProjectLoad
		    session->DidOpenFile(t->Context(), uri, 1, content,
		                         lsproto::LanguageKindTypeScript);
		    auto* snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    assert::Assert(
		        t,
		        snapshot->ProjectCollection->ConfiguredProject(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) ==
		            nullptr);

		    // Should use inferred project instead
		    auto* defaultProject = snapshot->GetDefaultProject(uri);
		    assert::Assert(t, defaultProject != nullptr);
		    assert::Equal(t, defaultProject->Kind,
		                  project::KindInferred);

		    // Searched configs should be present while file is open
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json")) !=
		            nullptr,
		        "solution config should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) ==
		            nullptr,
		        "direct reference should not be present");

		    // Close the file and open another one in the inferred project
		    session->DidCloseFile(t->Context(), uri);
		    lsproto::DocumentUri dummyUri =
		        "file:///user/username/workspaces/dummy/dummy.ts";
		    session->DidOpenFile(t->Context(), dummyUri, 1,
		                         "const x = 1;",
		                         lsproto::LanguageKindTypeScript);
		    snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() !=
		               nullptr);

		    // Config files should be released
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) ==
		            nullptr);
	    });

	t->Run(
	    "when project found is solution referencing default project indirectly through disableReferencedProjectLoad",
	    [](T* t) {
		    t->Parallel();
		    auto files = filesForSolutionConfigFile(
		        {"./tsconfig-indirect1.json"}, "", {});
		    applyIndirectProjectFiles(
		        files, 1,
		        "\"disableReferencedProjectLoad\": true");
		    auto [session, utils] = projecttestutil::Setup(files);
		    lsproto::DocumentUri uri =
		        "file:///user/username/projects/myproject/src/main.ts";
		    auto& content = pcbFileText(
		        files,
		        "/user/username/projects/myproject/src/main.ts");

		    // Ensure no configured project is created due to disableReferencedProjectLoad in indirect project
		    session->DidOpenFile(t->Context(), uri, 1, content,
		                         lsproto::LanguageKindTypeScript);
		    auto* snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    assert::Assert(
		        t,
		        snapshot->ProjectCollection->ConfiguredProject(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) ==
		            nullptr);

		    // Should use inferred project instead
		    auto* defaultProject = snapshot->GetDefaultProject(uri);
		    assert::Assert(t, defaultProject != nullptr);
		    assert::Equal(t, defaultProject->Kind,
		                  project::KindInferred);

		    // Searched configs should be present while file is open
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json")) !=
		            nullptr,
		        "solution config should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-indirect1.json")) !=
		            nullptr,
		        "solution direct reference should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) ==
		            nullptr,
		        "indirect reference should not be present");

		    // Close the file and open another one in the inferred project
		    session->DidCloseFile(t->Context(), uri);
		    lsproto::DocumentUri dummyUri =
		        "file:///user/username/workspaces/dummy/dummy.ts";
		    session->DidOpenFile(t->Context(), dummyUri, 1,
		                         "const x = 1;",
		                         lsproto::LanguageKindTypeScript);
		    snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() !=
		               nullptr);

		    // Config files should be released
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-indirect1.json")) ==
		            nullptr);
	    });

	t->Run(
	    "when project found is solution referencing default project indirectly through disableReferencedProjectLoad in one but without it in another",
	    [](T* t) {
		    t->Parallel();
		    auto files = filesForSolutionConfigFile(
		        {"./tsconfig-indirect1.json", "./tsconfig-indirect2.json"},
		        "", {});
		    applyIndirectProjectFiles(
		        files, 1,
		        "\"disableReferencedProjectLoad\": true");
		    applyIndirectProjectFiles(files, 2, "");
		    auto [session, utils] = projecttestutil::Setup(files);
		    lsproto::DocumentUri uri =
		        "file:///user/username/projects/myproject/src/main.ts";
		    auto& content = pcbFileText(
		        files,
		        "/user/username/projects/myproject/src/main.ts");

		    // Ensure configured project is found through the indirect project without disableReferencedProjectLoad
		    session->DidOpenFile(t->Context(), uri, 1, content,
		                         lsproto::LanguageKindTypeScript);
		    auto* snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    auto* srcProject =
		        snapshot->ProjectCollection->ConfiguredProject(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json"));
		    assert::Assert(t, srcProject != nullptr);

		    // Verify the default project is the source project (found through indirect2, not indirect1)
		    auto* defaultProject = snapshot->GetDefaultProject(uri);
		    assert::Equal(t, defaultProject, srcProject);

		    // Searched configs should be present while file is open
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json")) !=
		            nullptr,
		        "solution config should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-indirect1.json")) !=
		            nullptr,
		        "direct reference 1 should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-indirect2.json")) !=
		            nullptr,
		        "direct reference 2 should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) !=
		            nullptr,
		        "indirect reference should be present");

		    // Close the file and open another one in the inferred project
		    session->DidCloseFile(t->Context(), uri);
		    lsproto::DocumentUri dummyUri =
		        "file:///user/username/workspaces/dummy/dummy.ts";
		    session->DidOpenFile(t->Context(), dummyUri, 1,
		                         "const x = 1;",
		                         lsproto::LanguageKindTypeScript);
		    snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() !=
		               nullptr);

		    // Config files should be released
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-indirect1.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-indirect2.json")) ==
		            nullptr);
	    });

	t->Run(
	    "when project found is project with own files referencing the file from referenced project",
	    [](T* t) {
		    t->Parallel();
		    auto files = filesForSolutionConfigFile(
		        {"./tsconfig-src.json"}, "", {"\"./own/main.ts\""});
		    files["/user/username/projects/myproject/own/main.ts"] =
		        std::string(R"(
			import { foo } from '../src/main';
			foo;
			export function bar() {}
		)");
		    auto [session, utils] = projecttestutil::Setup(files);
		    lsproto::DocumentUri uri =
		        "file:///user/username/projects/myproject/src/main.ts";
		    auto& content = pcbFileText(
		        files,
		        "/user/username/projects/myproject/src/main.ts");

		    // Ensure configured project is found for open file - should load both projects
		    session->DidOpenFile(t->Context(), uri, 1, content,
		                         lsproto::LanguageKindTypeScript);
		    auto* snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(2));
		    auto* srcProject =
		        snapshot->ProjectCollection->ConfiguredProject(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json"));
		    assert::Assert(t, srcProject != nullptr);
		    auto* ancestorProject =
		        snapshot->ProjectCollection->ConfiguredProject(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json"));
		    assert::Assert(t, ancestorProject != nullptr);

		    // Verify the default project is the source project
		    auto* defaultProject = snapshot->GetDefaultProject(uri);
		    assert::Equal(t, defaultProject, srcProject);

		    // Searched configs should be present while file is open
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json")) !=
		            nullptr,
		        "solution config should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) !=
		            nullptr,
		        "direct reference should be present");

		    // Close the file and open another one in the inferred project
		    session->DidCloseFile(t->Context(), uri);
		    lsproto::DocumentUri dummyUri =
		        "file:///user/username/workspaces/dummy/dummy.ts";
		    session->DidOpenFile(t->Context(), dummyUri, 1,
		                         "const x = 1;",
		                         lsproto::LanguageKindTypeScript);
		    snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() !=
		               nullptr);

		    // Config files should be released
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/user/username/projects/myproject/tsconfig-src.json")) ==
		            nullptr);
	    });

	t->Run(
	    "when file is not part of first config tree found, looks into ancestor folder and its references to find default project",
	    [](T* t) {
		    t->Parallel();
		    projecttestutil::FileMap files{
		        {"/home/src/projects/project/app/Component-demos.ts",
		         std::string(R"(
                import * as helpers from 'demos/helpers';
                export const demo = () => {
                    helpers;
                }
            )")},
		        {"/home/src/projects/project/app/Component.ts",
		         std::string("export const Component = () => {}")},
		        {"/home/src/projects/project/app/tsconfig.json",
		         std::string(R"({
				"compilerOptions": {
					"composite": true,
					"outDir": "../app-dist/",
				},
				"include": ["**/*"],
				"exclude": ["**/*-demos.*"],
			})")},
		        {"/home/src/projects/project/demos/helpers.ts",
		         std::string("export const foo = 1;")},
		        {"/home/src/projects/project/demos/tsconfig.json",
		         std::string(R"({
				"compilerOptions": {
					"composite": true,
					"rootDir": "../",
					"outDir": "../demos-dist/",
					"paths": {
						"demos/*": ["./*"],
					},
				},
				"include": [
					"**/*",
					"../app/**/*-demos.*",
				],
			})")},
		        {"/home/src/projects/project/tsconfig.json",
		         std::string(R"({
				"compilerOptions": {
					"outDir": "./dist/",
				},
				"references": [
					{ "path": "./demos/tsconfig.json" },
					{ "path": "./app/tsconfig.json" },
				],
				"files": []
			})")},
		    };
		    auto [session, utils] = projecttestutil::Setup(files);
		    lsproto::DocumentUri uri =
		        "file:///home/src/projects/project/app/Component-demos.ts";
		    auto& content = pcbFileText(
		        files,
		        "/home/src/projects/project/app/Component-demos.ts");

		    // Ensure configured project is found for open file
		    session->DidOpenFile(t->Context(), uri, 1, content,
		                         lsproto::LanguageKindTypeScript);
		    auto* snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(2));
		    auto* demoProject =
		        snapshot->ProjectCollection->ConfiguredProject(
		            tspath::Path(
		                "/home/src/projects/project/demos/tsconfig.json"));
		    assert::Assert(t, demoProject != nullptr);
		    auto* solutionProject =
		        snapshot->ProjectCollection->ConfiguredProject(
		            tspath::Path(
		                "/home/src/projects/project/tsconfig.json"));
		    assert::Assert(t, solutionProject != nullptr);

		    // Verify the default project is the demos project (not the app project that excludes demos files)
		    auto* defaultProject = snapshot->GetDefaultProject(uri);
		    assert::Equal(t, defaultProject, demoProject);

		    // Searched configs should be present while file is open
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/home/src/projects/project/app/tsconfig.json")) !=
		            nullptr,
		        "app config should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/home/src/projects/project/demos/tsconfig.json")) !=
		            nullptr,
		        "demos config should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/home/src/projects/project/tsconfig.json")) !=
		            nullptr,
		        "solution config should be present");

		    // Close the file and open another one in the inferred project
		    session->DidCloseFile(t->Context(), uri);
		    lsproto::DocumentUri dummyUri =
		        "file:///user/username/workspaces/dummy/dummy.ts";
		    session->DidOpenFile(t->Context(), dummyUri, 1,
		                         "const x = 1;",
		                         lsproto::LanguageKindTypeScript);
		    snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() !=
		               nullptr);

		    // Config files should be released
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/home/src/projects/project/app/tsconfig.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/home/src/projects/project/demos/tsconfig.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/home/src/projects/project/tsconfig.json")) ==
		            nullptr);
	    });

	t->Run(
	    "when dts file is next to ts file and included as root in referenced project",
	    [](T* t) {
		    t->Parallel();
		    projecttestutil::FileMap files{
		        {"/home/src/projects/project/src/index.d.ts",
		         std::string(R"(
                 declare global {
                    interface Window {
                        electron: ElectronAPI
                        api: unknown
                    }
                }
            )")},
		        {"/home/src/projects/project/src/index.ts",
		         std::string("const api = {}")},
		        {"/home/src/projects/project/tsconfig.json",
		         std::string(R"({
				"include": [
					"src/*.d.ts",
				],
				"references": [{ "path": "./tsconfig.node.json" }],
			})")},
		        {"/home/src/projects/project/tsconfig.node.json",
		         std::string(R"({
				"include": ["src/**/*"],
                "compilerOptions": {
                    "composite": true,
                },
			})")},
		    };
		    auto [session, utils] = projecttestutil::Setup(files);
		    lsproto::DocumentUri uri =
		        "file:///home/src/projects/project/src/index.d.ts";
		    auto& content = pcbFileText(
		        files, "/home/src/projects/project/src/index.d.ts");

		    // Ensure configured projects are found for open file
		    session->DidOpenFile(t->Context(), uri, 1, content,
		                         lsproto::LanguageKindTypeScript);
		    auto* snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(2));
		    auto* rootProject =
		        snapshot->ProjectCollection->ConfiguredProject(
		            tspath::Path(
		                "/home/src/projects/project/tsconfig.json"));
		    assert::Assert(t, rootProject != nullptr);

		    // Verify the default project is inferred
		    auto* defaultProject = snapshot->GetDefaultProject(uri);
		    assert::Assert(t, defaultProject != nullptr);
		    assert::Equal(t, defaultProject->Kind,
		                  project::KindInferred);

		    // Searched configs should be present while file is open
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/home/src/projects/project/tsconfig.json")) !=
		            nullptr,
		        "root config should be present");
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/home/src/projects/project/tsconfig.node.json")) !=
		            nullptr,
		        "node config should be present");

		    // Close the file and open another one in the inferred project
		    session->DidCloseFile(t->Context(), uri);
		    lsproto::DocumentUri dummyUri =
		        "file:///user/username/workspaces/dummy/dummy.ts";
		    session->DidOpenFile(t->Context(), dummyUri, 1,
		                         "const x = 1;",
		                         lsproto::LanguageKindTypeScript);
		    snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    assert::Assert(
		        t, snapshot->ProjectCollection->InferredProject() !=
		               nullptr);

		    // Config files should be released
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/home/src/projects/project/tsconfig.json")) ==
		            nullptr);
		    assert::Assert(
		        t,
		        snapshot->ConfigFileRegistry->GetConfig(
		            tspath::Path(
		                "/home/src/projects/project/tsconfig.node.json")) ==
		            nullptr);
	    });

	t->Run("#1630", [](T* t) {
		t->Parallel();
		projecttestutil::FileMap files{
		    {"/project/lib/tsconfig.json", std::string(R"({
				"files": ["a.ts"]
			})")},
		    {"/project/lib/a.ts", std::string("export const a = 1;")},
		    {"/project/lib/b.ts", std::string("export const b = 1;")},
		    {"/project/tsconfig.json", std::string(R"({
				"files": [],
				"references": [{ "path": "./lib" }],
				"compilerOptions": {
					"disableReferencedProjectLoad": true
				}
			})")},
		    {"/project/index.ts", std::string("")},
		};

		auto [session, utils] = projecttestutil::Setup(files);

		// opening b.ts puts /project/lib/tsconfig.json in the config file registry and creates the project,
		// but the project is ultimately not a match
		session->DidOpenFile(t->Context(), "file:///project/lib/b.ts",
		                     1,
		                     pcbFileText(files, "/project/lib/b.ts"),
		                     lsproto::LanguageKindTypeScript);
		// opening an unrelated file triggers cleanup of /project/lib/tsconfig.json since no open file is part of that project,
		// but will keep the config file in the registry since lib/b.ts is still open
		session->DidOpenFile(t->Context(), "untitled:Untitled-1", 1,
		                     "", lsproto::LanguageKindTypeScript);
		// Opening index.ts searches /project/tsconfig.json and then checks /project/lib/tsconfig.json without opening it.
		// No early return on config file existence means we try to find an already open project, which returns nil,
		// triggering a crash.
		session->DidOpenFile(t->Context(), "file:///project/index.ts",
		                     1,
		                     pcbFileText(files, "/project/index.ts"),
		                     lsproto::LanguageKindTypeScript);
	});

	t->Run("inferred project root files are in stable order",
	       [](T* t) {
		    t->Parallel();
		    projecttestutil::FileMap files{
		        {"/project/a.ts",
		         std::string("export const a = 1;")},
		        {"/project/b.ts",
		         std::string("export const b = 1;")},
		        {"/project/c.ts",
		         std::string("export const c = 1;")},
		    };

		    auto [session, utils] = projecttestutil::Setup(files);

		    // b, c, a
		    session->DidOpenFile(
		        t->Context(), "file:///project/b.ts", 1,
		        pcbFileText(files, "/project/b.ts"),
		        lsproto::LanguageKindTypeScript);
		    session->DidOpenFile(
		        t->Context(), "file:///project/c.ts", 1,
		        pcbFileText(files, "/project/c.ts"),
		        lsproto::LanguageKindTypeScript);
		    session->DidOpenFile(
		        t->Context(), "file:///project/a.ts", 1,
		        pcbFileText(files, "/project/a.ts"),
		        lsproto::LanguageKindTypeScript);

		    auto* snapshot = session->Snapshot();
		    assert::Equal(t, pcbProjectCount(snapshot), int64_t(1));
		    auto* inferredProject =
		        snapshot->ProjectCollection->InferredProject();
		    assert::Assert(t, inferredProject != nullptr);
		    // It's more bookkeeping to maintain order of opening, since any file can move into or out of
		    // the inferred project due to changes in other projects. Order shouldn't matter for correctness,
		    // we just want it to be consistent, in case there are observable type ordering issues.
		    assert::DeepEqual(
		        t, inferredProject->Program->CommandLine()->FileNames(),
		        std::vector<std::string>{
		            "/project/a.ts",
		            "/project/b.ts",
		            "/project/c.ts",
		        });
	    });

	t->Run("project lookup terminates", [](T* t) {
		t->Parallel();
		projecttestutil::FileMap files{
		    {"/tsconfig.json", std::string(R"({
				"files": [],
				"references": [
					{
						"path": "./packages/pkg1"
					},
					{
						"path": "./packages/pkg2"
					},
				]
			})")},
		    {"/packages/pkg1/tsconfig.json", std::string(R"({
				"include": ["src/**/*.ts"],
				"compilerOptions": {
					"composite": true,
				},
				"references": [
					{
						"path": "../pkg2"
					},
				]
			})")},
		    {"/packages/pkg2/tsconfig.json", std::string(R"({
				"include": ["src/**/*.ts"],
				"compilerOptions": {
					"composite": true,
				},
				"references": [
					{
						"path": "../pkg1"
					},
				]
			})")},
		    {"/script.ts", std::string("export const a = 1;")},
		};
		auto [session, utils] = projecttestutil::Setup(files);
		session->DidOpenFile(t->Context(), "file:///script.ts", 1,
		                     pcbFileText(files, "/script.ts"),
		                     lsproto::LanguageKindTypeScript);
		// Test should terminate
	});

	t->Run("file moves to inferred project after import is deleted",
	       [](T* t) {
		    t->Parallel();
		    // This test verifies that when a node_modules dependency file is open and its import
		    // is deleted from the project root, requesting language service for the dependency
		    // correctly moves it to an inferred project.
		    projecttestutil::FileMap files{
		        {"/project/tsconfig.json",
		         std::string(
		             R"({"compilerOptions": {"strict": true}})")},
		        {"/project/index.ts",
		         std::string(
		             "import { helper } from \"./node_modules/dep/index\";")},
		        {"/project/node_modules/dep/index.d.ts",
		         std::string(
		             "export declare function helper(): void;")},
		    };
		    auto [session, utils] = projecttestutil::Setup(files);

		    // Step 1: Open the project root file
		    lsproto::DocumentUri rootUri =
		        "file:///project/index.ts";
		    session->DidOpenFile(
		        t->Context(), rootUri, 1,
		        pcbFileText(files, "/project/index.ts"),
		        lsproto::LanguageKindTypeScript);
		    auto [ls0, err0] =
		        session->GetLanguageService(t->Context(), rootUri);
		    assert::NilError(t, err0);
		    delete ls0;

		    // Step 2: Open the node_modules dependency file - should be in the configured project
		    lsproto::DocumentUri depUri =
		        "file:///project/node_modules/dep/index.d.ts";
		    session->DidOpenFile(
		        t->Context(), depUri, 1,
		        pcbFileText(files,
		                    "/project/node_modules/dep/index.d.ts"),
		        lsproto::LanguageKindTypeScript);

		    auto* snapshot = session->Snapshot();
		    auto* configuredProject =
		        snapshot->ProjectCollection->ConfiguredProject(
		            tspath::Path("/project/tsconfig.json"));
		    assert::Assert(t, configuredProject != nullptr,
		                   "configured project should exist");
		    auto* defaultProject = snapshot->GetDefaultProject(depUri);
		    assert::Equal(
		        t, defaultProject, configuredProject,
		        "dependency should be in the configured project initially");

		    // Step 3: Delete the import from the root file
		    lsproto::TextDocumentContentChangePartialOrWholeDocument
		        change;
		    change.WholeDocument =
		        std::make_shared<
		            lsproto::
		                TextDocumentContentChangeWholeDocument>();
		    change.WholeDocument->Text = "// import removed";
		    session->DidChangeFile(t->Context(), rootUri, 2,
		                           {change});

		    // Step 4: Request language service for the dependency - it should now be in an inferred project
		    auto [ls, err] =
		        session->GetLanguageService(t->Context(), depUri);
		    assert::NilError(t, err);
		    assert::Assert(
		        t, ls != nullptr,
		        "language service should be available for dependency");

		    snapshot = session->Snapshot();
		    defaultProject = snapshot->GetDefaultProject(depUri);
		    assert::Assert(t, defaultProject != nullptr,
		                   "dependency should have a default project");
		    assert::Equal(
		        t, defaultProject->Kind, project::KindInferred,
		        "dependency should be in an inferred project after import is deleted");
		    delete ls;
	    });

	t->Run("should update project on package.json change", [](T* t) {
		t->Parallel();
		// Set up a project with package.json "imports" that affect module resolution.
		// The package.json is not a program file, but it IS an affecting location.
		// When it changes, the project should be marked dirty and the program should be rebuilt.
		projecttestutil::FileMap packageJsonFiles{
		    {"/home/projects/myproject/tsconfig.json",
		     std::string(R"({
				"compilerOptions": {
					"module": "nodenext",
					"moduleResolution": "nodenext",
					"noLib": true,
					"noEmit": true
				}
			})")},
		    {"/home/projects/myproject/package.json",
		     std::string(R"({
				"name": "myproject",
				"type": "module",
				"imports": {
					"#utils": "./src/utils.ts"
				}
			})")},
		    {"/home/projects/myproject/src/index.ts",
		     std::string("import { add } from \"#utils\";")},
		    {"/home/projects/myproject/src/utils.ts",
		     std::string(
		         "export function add(a: number, b: number) { return a + b; }")},
		};

		auto [session, utils] = projecttestutil::Setup(packageJsonFiles);
		lsproto::DocumentUri indexUri =
		    "file:///home/projects/myproject/src/index.ts";
		session->DidOpenFile(
		    t->Context(), indexUri, 1,
		    pcbFileText(packageJsonFiles,
		                "/home/projects/myproject/src/index.ts"),
		    lsproto::LanguageKindTypeScript);

		// Verify initial state: #utils resolves to utils.ts, so utils.ts is in the program
		auto [ls, err] =
		    session->GetLanguageService(t->Context(), indexUri);
		assert::NilError(t, err);
		auto* program = ls->GetProgram();
		assert::Equal(
		    t,
		    static_cast<int64_t>(program->GetSemanticDiagnostics(nullptr)
		                         .size()),
		    int64_t(0),
		    "should have no diagnostics with correct package.json");
		delete ls;

		// Now change the package.json to point #utils at a non-existent file
		auto werr = utils->FS()->WriteFile(
		    "/home/projects/myproject/package.json", R"({
			"name": "myproject",
			"type": "module",
			"imports": {
				"#utils": "./src/nonexistent.ts"
			}
		})");
		assert::Assert(t, werr.impl() == nullptr);
		lsproto::FileEvent ev;
		ev.Uri = lsproto::DocumentUri(
		    "file:///home/projects/myproject/package.json");
		ev.Type = lsproto::FileChangeTypeChanged;
		session->DidChangeWatchedFiles(t->Context(), {&ev});

		auto [ls2, err2] =
		    session->GetLanguageService(t->Context(), indexUri);
		assert::NilError(t, err2);
		auto* updatedProgram = ls2->GetProgram();
		assert::Equal(
		    t,
		    static_cast<int64_t>(
		        updatedProgram
		            ->GetSemanticDiagnostics(nullptr)
		            .size()),
		    int64_t(1),
		    "should have diagnostics after package.json change");
		delete ls2;
	});
}

REGISTER_UNIT_TEST("project.TestProjectCollectionBuilder",
                   TestProjectCollectionBuilder);

}  // namespace
