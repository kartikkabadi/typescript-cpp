// Port of tsc/internal/api/session_module_resolution_test.go (package api).
#include <memory>
#include <string>
#include <vector>

#include "internal/api/module_resolution.h"
#include "internal/api/proto.h"
#include "internal/api/session.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/ipc/ipc.h"
#include "internal/json/json.h"
#include "internal/module/resolver.h"
#include "internal/project/session.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::api {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace projecttestutil = tsc::testutil::projecttestutil;

struct sessionCloser {
	std::shared_ptr<Session> s;
	~sessionCloser() { s->Close(); }
};

struct projectCloser {
	project::Session* s;
	~projectCloser() { s->Close(); }
};

// failingModuleResolutionConn — session_module_resolution_test.go:14.
struct failingModuleResolutionConn final : ipc::Conn {
	int calls = 0;

	gostd::Error Run(gostd::Context ctx) override { return nullptr; }

	std::pair<json::Value, gostd::Error> Call(
	    gostd::Context ctx, std::string_view method,
	    const json::Value& params) override {
		calls++;
		return {json::Value{}, gostd::newError("callback error")};
	}

	gostd::Error Notify(gostd::Context ctx, std::string_view method,
	                    const json::Value& params) override {
		return nullptr;
	}
};

// callbackTestConn — session_module_resolution_test.go (defined upstream in
// callbackfs_test.go, not yet vendored; kept here with the Go port).
struct callbackTestConn final : ipc::Conn {
	std::unordered_map<std::string, json::Value> responses;

	gostd::Error Run(gostd::Context ctx) override { return nullptr; }

	std::pair<json::Value, gostd::Error> Call(
	    gostd::Context ctx, std::string_view method,
	    const json::Value& params) override {
		return {responses[std::string(method)], nullptr};
	}

	gostd::Error Notify(gostd::Context ctx, std::string_view method,
	                    const json::Value& params) override {
		return nullptr;
	}
};

std::shared_ptr<ModuleResolutionEntry> mkStaticResolutionEntry(
    const std::string& moduleName, const std::string& directory,
    const ModuleKind* mode, const std::string& fileName) {
	auto entry = std::make_shared<ModuleResolutionEntry>();
	entry->ModuleName = moduleName;
	entry->Result = std::make_shared<StaticModuleResolution>(
	    StaticModuleResolution{
	        .ResolvedFileName = std::make_shared<DocumentIdentifier>(
	            DocumentIdentifier{.FileName = fileName}),
	    });
	if (mode != nullptr) {
		entry->ResolutionMode = api::ResolutionMode(*mode);
	}
	if (!directory.empty()) {
		entry->ContainingDirectory =
		    std::make_shared<DocumentIdentifier>(
		        DocumentIdentifier{.FileName = directory});
	}
	return entry;
}

void TestModuleResolverUsesSnapshotFileSystem(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/home/projects/p/node_modules/pkg/package.json",
	         "{\"name\":\"pkg\",\"version\":\"1.0.0\",\"exports\":{\".\":{"
	         "\"types\":\"./index.d.ts\",\"default\":\"./index.js\"}}}"},
	        {"/home/projects/p/node_modules/pkg/index.d.ts",
	         "export declare const value: string;"},
	        {"/home/projects/p/node_modules/pkg/index.js",
	         "exports.value = \"value\";"},
	    });
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};

	CreateSnapshotParams snapshotParams;
	auto [snapshot, err] = session->handleCreateSnapshot(
	    gostd::contextBackground(), &snapshotParams);
	assert::NilError(t, err);
	CreateModuleResolverParams resolverParams{
	    .CompilerOptions =
	        tsc::CompilerOptions{
	            .Module = ModuleKind::NodeNext,
	            .ModuleResolution = ModuleResolutionKind::NodeNext,
	            .TraceResolution = Tristate::True,
	        },
	};
	auto [resolver, err2] =
	    session->handleCreateModuleResolver(&resolverParams);
	assert::NilError(t, err2);
	ResolveModuleNameParams resolveParams{
	    .Snapshot = snapshot->Snapshot,
	    .Resolver = resolver,
	    .ModuleName = "pkg",
	    .ContainingDirectory =
	        DocumentIdentifier{.FileName = "/home/projects/p/src"},
	};
	auto [result, resolutionErr] = session->handleResolveModuleName(
	    gostd::contextBackground(), &resolveParams);
	assert::NilError(t, resolutionErr);
	assert::Equal(
	    t, result->ResolvedModule->ResolvedFileName,
	    std::string("/home/projects/p/node_modules/pkg/index.d.ts"));
	assert::Equal(t, result->ResolvedModule->PackageId->Name,
	              std::string("pkg"));
	assert::Assert(t, !result->Trace.empty());
}
REGISTER_UNIT_TEST("api.TestModuleResolverUsesSnapshotFileSystem",
                   TestModuleResolverUsesSnapshotFileSystem);

void TestStaticModuleResolutionSpecificityAndLifetime(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/home/projects/p/global.d.ts",
	         "export declare const value: \"global\";"},
	        {"/home/projects/p/mode.d.ts",
	         "export declare const value: \"mode\";"},
	        {"/home/projects/p/dir.d.ts",
	         "export declare const value: \"dir\";"},
	        {"/home/projects/p/exact.d.ts",
	         "export declare const value: \"exact\";"},
	        {"/home/projects/p/default.d.ts",
	         "export declare const value: \"default\";"},
	    });
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};

	CreateSnapshotParams snapshotParams;
	auto [snapshotOwner, err] = session->handleCreateSnapshot(
	    gostd::contextBackground(), &snapshotParams);
	assert::NilError(t, err);
	auto* snapshot = snapshotOwner.get();
	auto esm = ModuleKind::ESNext;
	auto spec = std::make_shared<ModuleResolutionSpec>(ModuleResolutionSpec{
	    .Fallback = ModuleResolutionFallbackUnresolved,
	    .Entries = {
	        mkStaticResolutionEntry("pkg", "", nullptr,
	                                    "/home/projects/p/global.d.ts"),
	        mkStaticResolutionEntry("pkg", "", &esm,
	                                    "/home/projects/p/mode.d.ts"),
	        mkStaticResolutionEntry("pkg", "/home/projects/p/src",
	                                    nullptr, "/home/projects/p/dir.d.ts"),
	        mkStaticResolutionEntry("pkg", "/home/projects/p/src",
	                                    &esm, "/home/projects/p/exact.d.ts"),
	    },
	});
	CreateModuleResolverParams resolverParams{
	    .CompilerOptions =
	        tsc::CompilerOptions{
	            .ModuleResolution = ModuleResolutionKind::NodeNext},
	    .ModuleResolutions = spec,
	};
	auto [resolverIDPair, err2] =
	    session->handleCreateModuleResolver(&resolverParams);
	assert::NilError(t, err2);
	auto resolverID = resolverIDPair;

	auto assertResolution = [&](const std::string& directory,
	                            ModuleKind mode,
	                            const std::string& expected) {
		t->Helper();
		auto resolutionMode = ResolutionMode(mode);
		ResolveModuleNameParams p{
		    .Snapshot = snapshot->Snapshot,
		    .Resolver = resolverID,
		    .ModuleName = "pkg",
		    .ContainingDirectory =
		        DocumentIdentifier{.FileName = directory},
		    .ResolutionMode = resolutionMode,
		};
		auto [result, resolutionErr] =
		    session->handleResolveModuleName(gostd::contextBackground(),
		                                     &p);
		assert::NilError(t, resolutionErr);
		assert::Equal(t, result->ResolvedModule->ResolvedFileName,
		              expected);
		assert::Equal(t, int(result->Trace.size()), 0);
	};
	assertResolution("/home/projects/p/src", ModuleKind::ESNext,
	                 "/home/projects/p/exact.d.ts");
	assertResolution("/home/projects/p/src", ModuleKind::CommonJS,
	                 "/home/projects/p/dir.d.ts");
	assertResolution("/home/projects/p/other", ModuleKind::ESNext,
	                 "/home/projects/p/mode.d.ts");
	assertResolution("/home/projects/p/other", ModuleKind::CommonJS,
	                 "/home/projects/p/global.d.ts");

	ResolveModuleNameParams unresolvedParams{
	    .Snapshot = snapshot->Snapshot,
	    .Resolver = resolverID,
	    .ModuleName = "other",
	    .ContainingDirectory =
	        DocumentIdentifier{.FileName = "/home/projects/p/src"},
	};
	auto [unresolved, err3] = session->handleResolveModuleName(
	    gostd::contextBackground(), &unresolvedParams);
	assert::NilError(t, err3);
	assert::Assert(t, unresolved->ResolvedModule == nullptr);

	assertResolution("/home/projects/p/src", ModuleKind::ESNext,
	                 "/home/projects/p/exact.d.ts");
}
REGISTER_UNIT_TEST("api.TestStaticModuleResolutionSpecificityAndLifetime",
                   TestStaticModuleResolutionSpecificityAndLifetime);

void TestCreateProgramUsesStaticModuleResolutions(T* t) {
	t->Parallel();

	const std::string root = "/home/projects/p/src/index.ts";
	const std::string provided = "/home/projects/p/provided.d.ts";
	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {root, "import { value } from \"pkg\"; export { value };"},
	        {provided, "export declare const value: string;"},
	    });
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};

	CreateModuleResolverParams resolverParams{
	    .CompilerOptions =
	        tsc::CompilerOptions{
	            .NoLib = Tristate::True,
	            .Module = ModuleKind::NodeNext,
	            .ModuleResolution = ModuleResolutionKind::NodeNext,
	        },
	    .ModuleResolutions =
	        std::make_shared<ModuleResolutionSpec>(ModuleResolutionSpec{
	            .Fallback = ModuleResolutionFallbackUnresolved,
	            .Entries = {mkStaticResolutionEntry("pkg", "", nullptr,
	                                                provided)},
	        }),
	};
	auto [resolver, err] =
	    session->handleCreateModuleResolver(&resolverParams);
	assert::NilError(t, err);

	CreateSnapshotParams params;
	params.CreatePrograms.emplace().push_back(
	    std::make_shared<CreateSnapshotProgramParams>(
	        CreateSnapshotProgramParams{
	            .RootFiles = {DocumentIdentifier{.FileName = root}},
	            .CompilerOptions =
	                tsc::CompilerOptions{
	                    .NoLib = Tristate::True,
	                    .Module = ModuleKind::NodeNext,
	                    .ModuleResolution =
	                        ModuleResolutionKind::NodeNext,
	                },
	            .Options = std::make_shared<CreateProgramOptions>(
	                CreateProgramOptions{.ModuleResolver = resolver}),
	        }));
	auto [response, err2] =
	    session->handleCreateSnapshot(gostd::contextBackground(), &params);
	assert::NilError(t, err2);
	auto projectID = (*response->Operation->CreatedPrograms)[0].AsID();
	GetSourceFileNamesParams namesParams{
	    .Snapshot = response->Snapshot,
	    .Project = projectID,
	};
	auto [fileNames, err3] = session->handleGetSourceFileNames(
	    gostd::contextBackground(), &namesParams);
	assert::NilError(t, err3);
	assert::DeepEqual(t, fileNames,
	                  std::vector<std::string>({provided, root}));
}
REGISTER_UNIT_TEST("api.TestCreateProgramUsesStaticModuleResolutions",
                   TestCreateProgramUsesStaticModuleResolutions);

void TestCustomModuleResolutionsSkipUnsafeRewriteDiagnostic(T* t) {
	t->Parallel();

	const std::string root = "/home/projects/p/src/a.ts";
	const std::string staticTarget = "/home/projects/p/src/b.ts";
	const std::string callbackTarget = "/home/projects/p/src/c.ts";
	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {root, "import { b } from \"./b.ts\"; import { c } from \"./c.ts\"; export const a = b + c;"},
	        {staticTarget, "export const b = 1;"},
	        {callbackTarget, "export const c = 2;"},
	    });
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto conn = std::make_shared<callbackTestConn>();
	conn->responses["resolveModuleName/1"] =
	    json::Value("{\"resolvedFileName\":\"" + callbackTarget + "\"}");
	session->conn = conn;
	auto compilerOptions = [] {
		return tsc::CompilerOptions{
		    .NoLib = Tristate::True,
		    .Module = ModuleKind::NodeNext,
		    .ModuleResolution = ModuleResolutionKind::NodeNext,
		    .RewriteRelativeImportExtensions = Tristate::True,
		    .OutDir = "/home/projects/p/out",
		};
	};
	CreateModuleResolverParams resolverParams{
	    .CompilerOptions = compilerOptions(),
	    .ModuleResolutions =
	        std::make_shared<ModuleResolutionSpec>(ModuleResolutionSpec{
	            .Fallback = ModuleResolutionFallbackResolve,
	            .Entries = {mkStaticResolutionEntry("./b.ts", "", nullptr,
	                                                staticTarget)},
	        }),
	    .ResolveModuleNameCallback = "resolveModuleName/1",
	};
	auto [resolver, err] =
	    session->handleCreateModuleResolver(&resolverParams);
	assert::NilError(t, err);

	CreateSnapshotParams params;
	params.CreatePrograms.emplace().push_back(
	    std::make_shared<CreateSnapshotProgramParams>(
	        CreateSnapshotProgramParams{
	            .RootFiles = {DocumentIdentifier{.FileName = root},
	                          DocumentIdentifier{.FileName = staticTarget},
	                          DocumentIdentifier{.FileName = callbackTarget}},
	            .CompilerOptions = compilerOptions(),
	            .Options = std::make_shared<CreateProgramOptions>(
	                CreateProgramOptions{.ModuleResolver = resolver}),
	        }));
	auto [response, err2] =
	    session->handleCreateSnapshot(gostd::contextBackground(), &params);
	assert::NilError(t, err2);
	GetDiagnosticsParams diagParams{
	    .Snapshot = response->Snapshot,
	    .Project = (*response->Operation->CreatedPrograms)[0].AsID(),
	    .Files = {DocumentIdentifier{.FileName = root}},
	};
	auto [diagnostics, err3] = session->handleGetSemanticDiagnostics(
	    gostd::contextBackground(), &diagParams);
	assert::NilError(t, err3);
	assert::Assert(t, diagnostics.empty(),
	               "handleGetSemanticDiagnostics reported diagnostics");
}
REGISTER_UNIT_TEST(
    "api.TestCustomModuleResolutionsSkipUnsafeRewriteDiagnostic",
    TestCustomModuleResolutionsSkipUnsafeRewriteDiagnostic);

void TestStaticModuleResolutionPreservesStaticIdentity(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup({});
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};

	CreateSnapshotParams snapshotParams;
	auto [snapshot, err] = session->handleCreateSnapshot(
	    gostd::contextBackground(), &snapshotParams);
	assert::NilError(t, err);
	auto entry = std::make_shared<ModuleResolutionEntry>();
	entry->ModuleName = "pkg";
	entry->Result = std::make_shared<StaticModuleResolution>(
	    StaticModuleResolution{
	        .ResolvedFileName = std::make_shared<DocumentIdentifier>(
	            DocumentIdentifier{
	                .FileName = "/store/pkg/index.d.ts"}),
	        .OriginalPath = std::make_shared<DocumentIdentifier>(
	            DocumentIdentifier{
	                .FileName = "/node_modules/pkg/index.d.ts"}),
	        .PackageID = std::make_shared<PackageId>(
	            PackageId{
	                .Name = "pkg",
	                .SubModuleName = "",
	                .Version = "1.2.3",
	            }),
	    });
	CreateModuleResolverParams resolverParams{
	    .CompilerOptions =
	        tsc::CompilerOptions{
	            .ModuleResolution = ModuleResolutionKind::NodeNext},
	    .ModuleResolutions =
	        std::make_shared<ModuleResolutionSpec>(ModuleResolutionSpec{
	            .Fallback = ModuleResolutionFallbackUnresolved,
	            .Entries = {entry},
	        }),
	};
	auto [resolver, err2] =
	    session->handleCreateModuleResolver(&resolverParams);
	assert::NilError(t, err2);
	ResolveModuleNameParams p{
	    .Snapshot = snapshot->Snapshot,
	    .Resolver = resolver,
	    .ModuleName = "pkg",
	    .ContainingDirectory = DocumentIdentifier{.FileName = "/src"},
	};
	auto [result, err3] = session->handleResolveModuleName(
	    gostd::contextBackground(), &p);
	assert::NilError(t, err3);
	assert::Equal(t, result->ResolvedModule->OriginalPath,
	              std::string("/node_modules/pkg/index.d.ts"));
	assert::Equal(t, result->ResolvedModule->PackageId->Name,
	              std::string("pkg"));
	assert::Equal(t, result->ResolvedModule->PackageId->Version,
	              std::string("1.2.3"));
	assert::Equal(t, result->ResolvedModule->IsExternalLibraryImport,
	              true);
}
REGISTER_UNIT_TEST("api.TestStaticModuleResolutionPreservesStaticIdentity",
                   TestStaticModuleResolutionPreservesStaticIdentity);

void TestModuleResolutionCallbackErrorsAreReturned(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup({});
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	auto conn = std::make_shared<failingModuleResolutionConn>();
	auto registration =
	    std::make_shared<moduleResolverRegistration>(
	        moduleResolverRegistration{
	            .id = 1,
	            .resolveModuleNameCallback = "resolveModuleName/1",
	        });
	auto factory = std::make_shared<moduleResolverFactory>();
	factory->registration = registration;
	factory->session = session.get();
	factory->conn = conn;
	factory->ctx = gostd::contextBackground();
	factory->currentDirectory = "/";
	// Go passes core.EmptyCompilerOptions (value); C++ takes a pointer.
	static const tsc::CompilerOptions emptyCompilerOptions{};
	auto [provider, cleanup] = factory->NewResolver(module::ResolverOptions{
	    .Host = session.get(),
	    .CompilerOptions = &emptyCompilerOptions,
	});
	for (int i = 0; i < 2; i++) {
		// Go's resolver returns an error; the C++ iface has no error slot
		// and callbackModuleResolver throws instead — the same failure.
		std::string errMsg;
		try {
			provider->ResolveModuleNameFromDirectory(
			    "pkg", "/src", ResolutionModeESM);
		} catch (const std::exception& e) {
			errMsg = e.what();
		}
		assert::Assert(
		    t, errMsg.find("callback error") != std::string::npos,
		    "callback error");
	}
	assert::Equal(t, conn->calls, 2);
	assert::Equal(t, int(session->programResolutionContexts.size()), 1);
	cleanup();
	assert::Equal(t, int(session->programResolutionContexts.size()), 0);
}
REGISTER_UNIT_TEST("api.TestModuleResolutionCallbackErrorsAreReturned",
                   TestModuleResolutionCallbackErrorsAreReturned);

void TestModuleResolutionCallbackErrorRejectsLanguageServerUpdate(T* t) {
	t->Parallel();

	auto [projectSession, utils] = projecttestutil::Setup(
	    projecttestutil::FileMap{
	        {"/src/index.ts", "import \"pkg\";"},
	    });
	projectCloser pc{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	sessionCloser c{session};
	session->conn = std::make_shared<failingModuleResolutionConn>();
	CreateModuleResolverParams resolverParams{
	    .CompilerOptions =
	        tsc::CompilerOptions{
	            .NoLib = Tristate::True,
	            .Module = ModuleKind::NodeNext,
	            .ModuleResolution = ModuleResolutionKind::NodeNext,
	        },
	    .ResolveModuleNameCallback = "resolveModuleName/1",
	};
	auto [resolver, err] =
	    session->handleCreateModuleResolver(&resolverParams);
	assert::NilError(t, err);
	auto* baseSnapshot = projectSession->Snapshot();

	auto changes = std::make_shared<LanguageServerSnapshotChanges>();
	changes->CreatePrograms = std::vector<std::shared_ptr<CreateSnapshotProgramParams>>{
	    std::make_shared<CreateSnapshotProgramParams>(
	        CreateSnapshotProgramParams{
	            .RootFiles = {DocumentIdentifier{.FileName =
	                                                "/src/index.ts"}},
	            .CompilerOptions =
	                tsc::CompilerOptions{
	                    .NoLib = Tristate::True,
	                    .Module = ModuleKind::NodeNext,
	                    .ModuleResolution =
	                        ModuleResolutionKind::NodeNext,
	                },
	            .Options = std::make_shared<CreateProgramOptions>(
	                CreateProgramOptions{.ModuleResolver = resolver}),
	        }),
	};
	GetCurrentLanguageServerSnapshotParams lsParams{
	    .Changes = changes,
	};
	auto [r, err2] = session->handleGetCurrentLanguageServerSnapshot(
	    gostd::contextBackground(), &lsParams);
	assert::ErrorContains(t, err2, "callback error");
	assert::Assert(t, projectSession->Snapshot() == baseSnapshot);
	assert::Equal(
	    t,
	    int(projectSession->Snapshot()
	            ->ProjectCollection->SyntheticProjects()
	            .size()),
	    0);
}
REGISTER_UNIT_TEST("api.TestModuleResolutionCallbackErrorRejectsLanguageServerUpdate",
                   TestModuleResolutionCallbackErrorRejectsLanguageServerUpdate);

}  // namespace
}  // namespace tsc::api
