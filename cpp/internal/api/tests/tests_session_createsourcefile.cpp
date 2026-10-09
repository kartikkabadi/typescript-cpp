// Port of tsc/internal/api/session_createsourcefile_test.go (package api).
#include <memory>
#include <mutex>
#include <string>

#include "internal/api/encoder/encoder.h"
#include "internal/api/proto.h"
#include "internal/api/session.h"
#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/project/project.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::api {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace projecttestutil = tsc::testutil::projecttestutil;

struct sessionCloser {
	project::Session* s = nullptr;
	~sessionCloser() {
		if (s) s->Close();
	}
};
struct apiSessionCloser {
	std::shared_ptr<Session> s;
	~apiSessionCloser() {
		if (s) s->Close();
	}
};

} // namespace

// TestCreateSourceFile — session_createsourcefile_test.go:12.
void TestCreateSourceFile(T* t) {
	t->Parallel();

	auto [projectSession, _utils] = projecttestutil::Setup(
	    {{"/src/input.ts", "export const fromFile = 1;"}});
	auto projectSessionPtr = projectSession;
	t->Cleanup([projectSessionPtr] { projectSessionPtr->Close(); });

	auto session = NewLSPSession(projectSession, nullptr);
	t->Cleanup([session] { session->Close(); });

	t->Run("text", [session](T* t) {
		t->Parallel();
		auto [lease, err] = session->createSourceFile(
		    "src/input.tsx", "export const element = <div />;",
		    CreateSourceFileOptions{});

		assert::NilError(t, err);
		auto leasePtr = lease;
		t->Cleanup([leasePtr] { leasePtr->Release(); });
		auto* sourceFile = lease->SourceFile_();
		assert::Equal(t, sourceFile->FileName(),
		              std::string("/src/input.tsx"));
		assert::Equal(t, std::string(sourceFile->Path()),
		              std::string("/src/input.tsx"));
		assert::Equal(t, sourceFile->Text(),
		              std::string("export const element = <div />;"));
		assert::Equal(t, (int)sourceFile->ScriptKind,
		              (int)ScriptKind::TSX);
		assert::Equal(t, (int)sourceFile->Statements->nodes.size(), 1);
		assert::Assert(t, sourceFile->IsBound());
	});

	t->Run("script kind override", [session](T* t) {
		t->Parallel();
		auto [lease, err] = session->createSourceFile(
		    "/src/component.txt", "export const element = <div />;",
		    CreateSourceFileOptions{ScriptKind::TSX});

		assert::NilError(t, err);
		auto leasePtr = lease;
		t->Cleanup([leasePtr] { leasePtr->Release(); });
		auto* sourceFile = lease->SourceFile_();
		assert::Equal(t, (int)sourceFile->ScriptKind,
		              (int)ScriptKind::TSX);
		assert::Equal(t, (int)sourceFile->diagnostics.size(), 0);
	});

	t->Run("shares parse cache with programs", [](T* t) {
		t->Parallel();

		const std::string fileName = "/src/shared.ts";
		const std::string sourceText = "export const shared = 1;";
		auto [cacheProjectSession, _u2] =
		    projecttestutil::Setup({{fileName, sourceText}});
		auto cacheProjectSessionPtr = cacheProjectSession;
		t->Cleanup([cacheProjectSessionPtr] { cacheProjectSessionPtr->Close(); });
		auto cacheSession = NewLSPSession(cacheProjectSession, nullptr);
		t->Cleanup([cacheSession] { cacheSession->Close(); });

		cacheProjectSession->DidOpenFile(
		    gostd::contextBackground(), "file:///src/shared.ts", 1,
		    sourceText, lsp::lsproto::LanguageKindTypeScript);
		auto [languageService, lerr] = cacheProjectSession->GetLanguageService(
		    gostd::contextBackground(), "file:///src/shared.ts");
		assert::NilError(t, lerr);
		cacheProjectSession->WaitForBackgroundTasks();

		auto* programFile =
		    languageService->GetProgram()->GetSourceFile(fileName);
		auto direct = cacheSession->acquireSourceFile(
		    programFile->ParseOptions(), sourceText,
		    programFile->ScriptKind);
		auto directPtr = direct;
		t->Cleanup([directPtr] { directPtr->Release(); });
		assert::Assert(t, programFile == direct->SourceFile_());
	});

	t->Run("lease release", [session](T* t) {
		t->Parallel();

		auto findLease = [session](const std::string& fileName) {
			std::lock_guard<std::mutex> lk(session->sourceFileLeasesMu);
			for (auto& [id, lease] : session->sourceFileLeases) {
				if (lease->SourceFile_()->FileName() == fileName) {
					return id;
				}
			}
			return SourceFileLeaseID{0};
		};

		CreateSourceFileParams p1;
		p1.FileName = "/src/lease-1.ts";
		p1.SourceText = "export {};";
		auto [first, err1] = session->handleCreateSourceFile(
		    gostd::contextBackground(), &p1);
		assert::NilError(t, err1);
		assert::Assert(t, !first.data.empty());
		auto firstLease = findLease("/src/lease-1.ts");
		assert::Assert(t, firstLease != SourceFileLeaseID{0});

		CreateSourceFileParams p2;
		p2.FileName = "/src/lease-2.ts";
		p2.SourceText = "export {};";
		auto [second, err2] = session->handleCreateSourceFile(
		    gostd::contextBackground(), &p2);
		assert::NilError(t, err2);
		assert::Assert(t, !second.data.empty());
		auto secondLease = findLease("/src/lease-2.ts");
		assert::Assert(t, secondLease != SourceFileLeaseID{0});
		assert::Assert(t, firstLease != secondLease);

		ReleaseSourceFileParams rp;
		rp.Lease = firstLease;
		auto [_r1, err3] = session->handleReleaseSourceFile(&rp);
		assert::NilError(t, err3);
		assert::Equal(t, (int64_t)findLease("/src/lease-1.ts"), 0);

		auto [_r2, err4] = session->handleReleaseSourceFile(&rp);
		assert::Assert(t, err4 != nullptr &&
		                      err4->Error().find("source file lease") !=
		                          std::string::npos);

		rp.Lease = secondLease;
		auto [_r3, err5] = session->handleReleaseSourceFile(&rp);
		assert::NilError(t, err5);
	});

	t->Run("retain by descriptor", [session](T* t) {
		t->Parallel();

		const std::string fileName = "/src/retained.ts";
		const std::string sourceText = "export const retained = true;";
		auto [created, err] = session->createSourceFile(
		    fileName, sourceText, CreateSourceFileOptions{});
		assert::NilError(t, err);
		auto* sourceFile = created->SourceFile_();
		auto descriptor = newSourceFileDescriptor(sourceFile);

		RetainSourceFileParams params;
		params.File = descriptor;
		auto [result, rerr] = session->handleRetainSourceFile(&params);
		assert::NilError(t, rerr);
		std::unordered_map<std::string, int64_t> retainResp;
		assert::Assert(
		    t, json::unmarshal(std::string_view(result.data), &retainResp)
		           .empty());
		SourceFileLeaseID lease{uint64_t(retainResp["lease"])};
		assert::Assert(t, lease != SourceFileLeaseID{0});
		{
			std::lock_guard<std::mutex> lk(session->sourceFileLeasesMu);
			auto* retainedSourceFile =
			    session->sourceFileLeases[lease]->SourceFile_();
			assert::Assert(t, retainedSourceFile == sourceFile);
		}

		created->Release();
		auto [key, kerr] = descriptor.parseCacheKey();
		assert::NilError(t, kerr);
		auto* acquired = session->snapshotHost->AcquireExistingSourceFile(key);
		assert::Assert(t, acquired != nullptr);
		acquired->Release();

		ReleaseSourceFileParams rp;
		rp.Lease = lease;
		auto [_r, relerr] = session->handleReleaseSourceFile(&rp);
		assert::NilError(t, relerr);
		assert::Assert(
		    t, session->snapshotHost->AcquireExistingSourceFile(key) ==
		           nullptr);
	});

	t->Run("retain cache miss", [session](T* t) {
		t->Parallel();

		SourceFileDescriptor descriptor{
		    .FileName = "/src/missing.ts",
		    .Path = "/src/missing.ts",
		    .ContentHash = "00000000000000000000000000000000",
		    .ParseOptionsKey = "0",
		    .ScriptKind = ScriptKind::TS,
		    .NodeID = "1",
		};
		RetainSourceFileParams params;
		params.File = descriptor;
		auto [_r, err] = session->handleRetainSourceFile(&params);
		assert::Assert(t, err != nullptr &&
		                      err->Error().find(
		                          "source file is not available") !=
		                          std::string::npos);
	});

	t->Run("declaration symbol lookup", [session](T* t) {
		t->Parallel();

		auto [created, err] = session->createSourceFile(
		    "/src/symbols.ts",
		    "function present() {}\nimport {} from './missing';",
		    CreateSourceFileOptions{});
		assert::NilError(t, err);
		auto createdPtr = created;
		t->Cleanup([createdPtr] { createdPtr->Release(); });

		SourceFile* sourceFile = created->SourceFile_();
		auto* table = encoder::GetNodeIndexTable(sourceFile);
		auto descriptor = newSourceFileDescriptor(sourceFile);

		GetSymbolOfDeclarationParams presentParams;
		presentParams.File = descriptor;
		presentParams.Index =
		    table->GetIndex(sourceFile->statements()[0]);
		auto [presentR, perr] =
		    session->handleGetSymbolOfDeclaration(&presentParams);
		assert::NilError(t, perr);
		assert::Assert(t, !presentR.data.empty());
		auto [dom, derr] = json::parse(std::string_view(presentR.data));
		assert::NilError(t, derr);
		auto* nameNode = json::objGet(dom, "name");
		assert::Assert(t, nameNode != nullptr);
		auto [nameStr, serr] = json::asString(*nameNode, "string");
		assert::NilError(t, serr);
		assert::Assert(t, nameStr == "present");
		auto* refNode = json::objGet(dom, "reference");
		assert::Assert(t, refNode != nullptr);
		auto* kindNode = json::objGet(*refNode, "kind");
		assert::Assert(t, kindNode != nullptr);
		auto [kind, kerr] = json::asInt32(*kindNode, "int");
		assert::NilError(t, kerr);
		assert::Assert(t, kind == static_cast<int32_t>(SymbolOwnerKind::File));

		GetSymbolOfDeclarationParams oobParams;
		oobParams.File = descriptor;
		oobParams.Index = 0;
		auto [_oob, oerr] =
		    session->handleGetSymbolOfDeclaration(&oobParams);
		assert::Assert(t, oerr != nullptr &&
		                      oerr->Error().find("out of range") !=
		                          std::string::npos);
	});

	t->Run("rejects stale node ID", [session](T* t) {
		t->Parallel();

		auto [created, err] = session->createSourceFile(
		    "/src/stale.ts", "export {};", CreateSourceFileOptions{});
		assert::NilError(t, err);
		auto createdPtr = created;
		t->Cleanup([createdPtr] { createdPtr->Release(); });
		auto descriptor = newSourceFileDescriptor(created->SourceFile_());
		descriptor.NodeID = "0";

		RetainSourceFileParams params;
		params.File = descriptor;
		auto [_r, rerr] = session->handleRetainSourceFile(&params);
		assert::Assert(t, rerr != nullptr &&
		                      rerr->Error().find("cached source file") !=
		                          std::string::npos);
	});

	t->Run("rejects an evicted file after equal-key recreation",
	       [session](T* t) {
		t->Parallel();

		const std::string fileName = "/src/recreated.ts";
		const std::string sourceText = "export {};";
		auto [first, err] = session->createSourceFile(
		    fileName, sourceText, CreateSourceFileOptions{});
		assert::NilError(t, err);
		auto staleDescriptor =
		    newSourceFileDescriptor(first->SourceFile_());
		first->Release();

		auto [second, err2] = session->createSourceFile(
		    fileName, sourceText, CreateSourceFileOptions{});
		assert::NilError(t, err2);
		auto secondPtr = second;
		t->Cleanup([secondPtr] { secondPtr->Release(); });
		assert::Assert(
		    t, newSourceFileDescriptor(second->SourceFile_()).NodeID !=
		           staleDescriptor.NodeID);

		RetainSourceFileParams params;
		params.File = staleDescriptor;
		auto [_r, rerr] = session->handleRetainSourceFile(&params);
		assert::Assert(t, rerr != nullptr &&
		                      rerr->Error().find("cached source file") !=
		                          std::string::npos);
	});

	t->Run("unknown extension defaults to TypeScript", [session](T* t) {
		t->Parallel();
		auto [lease, err] = session->createSourceFile(
		    "/src/component.txt", "export const value: string = \"ok\";",
		    CreateSourceFileOptions{});

		assert::NilError(t, err);
		auto leasePtr = lease;
		t->Cleanup([leasePtr] { leasePtr->Release(); });
		auto* sourceFile = lease->SourceFile_();
		assert::Equal(t, (int)sourceFile->ScriptKind, (int)ScriptKind::TS);
		assert::Equal(t, (int)sourceFile->diagnostics.size(), 0);
	});

	t->Run("from file", [session](T* t) {
		t->Parallel();
		CreateSourceFileFromFileParams params;
		params.FileName = "/src/input.ts";
		auto [result, err] = session->handleCreateSourceFileFromFile(
		    gostd::contextBackground(), &params);

		assert::NilError(t, err);
		assert::Assert(t, !result.data.empty());
	});

	t->Run("invalid script kind", [session](T* t) {
		t->Parallel();
		auto [_l, err] = session->createSourceFile(
		    "/src/input.ts", "",
		    CreateSourceFileOptions{static_cast<ScriptKind>(999)});

		assert::Assert(t, err != nullptr &&
		                      err->Error().find("invalid scriptKind 999") !=
		                          std::string::npos);
	});

	t->Run("missing file", [session](T* t) {
		t->Parallel();
		CreateSourceFileFromFileParams params;
		params.FileName = "/src/missing.ts";
		auto [_r, err] = session->handleCreateSourceFileFromFile(
		    gostd::contextBackground(), &params);

		assert::Assert(
		    t,
		    err != nullptr &&
		        err->Error().find("could not read file \"/src/missing.ts\"") !=
		            std::string::npos);
	});

	t->Run("empty file name", [session](T* t) {
		t->Parallel();
		CreateSourceFileParams params;
		auto [_r, err] = session->handleCreateSourceFile(
		    gostd::contextBackground(), &params);
		assert::Assert(
		    t, err != nullptr &&
		           err->Error().find("fileName must not be empty") !=
		               std::string::npos);

		CreateSourceFileFromFileParams params2;
		auto [_r2, err2] = session->handleCreateSourceFileFromFile(
		    gostd::contextBackground(), &params2);
		assert::Assert(
		    t, err2 != nullptr &&
		           err2->Error().find("fileName must not be empty") !=
		               std::string::npos);
	});
}
REGISTER_UNIT_TEST("api.TestCreateSourceFile", TestCreateSourceFile);

} // namespace tsc::api
