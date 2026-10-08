// Port of tsc/internal/lsp/server_flakydiagnostics_test.go (package lsp_test).
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsp.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/testutil/lsptestutil/lspclient.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::lsp {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace lsptestutil = tsc::testutil::lsptestutil;

} // namespace

// TestFlakyDiagnosticTrackingParallelEmit — server_flakydiagnostics_test.go:17.
void TestFlakyDiagnosticTrackingParallelEmit(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	for (bool noEmitOnError : {false, true}) {
		t->Run(noEmitOnError ? "noEmitOnError=true" : "noEmitOnError=false",
		       [noEmitOnError](T* t) {
			t->Parallel();
			std::unordered_map<std::string, vfs::vfstest::MapFileInput>
			    files;
			files["/src/tsconfig.json"] =
			    std::string(
			        "{\n\t\t\"compilerOptions\": { \"strict\": true, "
			        "\"declaration\": true, \"noEmitOnError\": ") +
			    (noEmitOnError ? "true" : "false") +
			    ", \"outDir\": \"out\" }\n}";
			files["/src/a.ts"] =
			    "export function box<T>(value: T) { return { value }; }";
			files["/src/b.ts"] =
			    "import { box } from \"./a\"; export const b = box(\"b\");";
			files["/src/c.ts"] =
			    "import { box } from \"./a\"; export const c = box(1);";
			auto fs = bundled::WrapFS(vfs::vfstest::FromMap(files, false));

			auto onServerRequest =
			    [](const gostd::Context&,
			       const std::shared_ptr<lsproto::RequestMessage>& req)
			    -> std::shared_ptr<lsproto::ResponseMessage> {
				if (req->Method == lsproto::MethodClientRegisterCapability ||
				    req->Method ==
				        lsproto::MethodClientUnregisterCapability ||
				    req->Method ==
				        lsproto::MethodWindowWorkDoneProgressCreate) {
					auto resp =
					    std::make_shared<lsproto::ResponseMessage>();
					resp->ID = req->ID;
					resp->JSONRPC = req->JSONRPC;
					resp->Result = lsproto::AnyValue::of(lsproto::Null{});
					return resp;
				}
				return nullptr;
			};

			auto [clientR, closeClientR] = lsptestutil::NewLSPClient(
			    t,
			    lsp::ServerOptions{.Err = gostd::io::discard(),
			                       .Cwd = "/src",
			                       .FS = fs,
			                       .DefaultLibraryPath =
			                           bundled::LibPath()},
			    onServerRequest);
			auto client = clientR;
			auto closeClient = closeClientR;
			t->Cleanup([closeClient] { (void)closeClient(); });

			auto initParams =
			    std::make_shared<lsproto::InitializeParams>();
			initParams->Capabilities =
			    std::make_shared<lsproto::ClientCapabilities>();
			initParams->InitializationOptions = std::make_shared<
			    lsproto::InitializationOptionsOrNull>(
			    lsproto::InitializationOptionsOrNull{
			        .InitializationOptions = std::make_shared<
			            lsproto::InitializationOptions>(
			            lsproto::InitializationOptions{
			                .TrackFlakyDiagnostics = std::make_shared<
			                    lsproto::DiagnosticFlakeLogLevel>(
			                    lsproto::DiagnosticFlakeLogLevelPanic)})});
			auto [msg, _r1, ok] = client->SendRequest(
			    t, lsproto::InitializeInfo, initParams);
			assert::Assert(t,
			               ok && msg->AsResponse()->Error == nullptr,
			               "initialize failed");
			client->SendNotification(
			    t, lsproto::InitializedInfo,
			    std::make_shared<lsproto::InitializedParams>());
			client->Server->InitComplete()->wait();

			auto uri = lsproto::DocumentUri("file:///src/a.ts");
			auto openParams =
			    std::make_shared<lsproto::DidOpenTextDocumentParams>();
			openParams->TextDocument =
			    std::make_shared<lsproto::TextDocumentItem>();
			openParams->TextDocument->Uri = uri;
			openParams->TextDocument->LanguageId =
			    lsproto::LanguageKindTypeScript;
			openParams->TextDocument->Text =
				std::get<std::string>(files["/src/a.ts"]);
			client->SendNotification(
			    t, lsproto::TextDocumentDidOpenInfo, openParams);

			auto diagParams =
			    std::make_shared<lsproto::DocumentDiagnosticParams>();
			diagParams->TextDocument =
			    lsproto::TextDocumentIdentifier{uri};
			auto [msg2, diagnostics, ok2] = client->SendRequest(
			    t, lsproto::TextDocumentDiagnosticInfo, diagParams);
			assert::Assert(t,
			               ok2 && msg2->AsResponse()->Error == nullptr,
			               "diagnostics request failed");
			assert::Assert(t,
			               diagnostics.FullDocumentDiagnosticReport !=
			                   nullptr);
			assert::Equal(
			    t, (int)diagnostics.FullDocumentDiagnosticReport->Items
			           ->size(),
			    0);
		});
	}
}
REGISTER_UNIT_TEST("lsp.TestFlakyDiagnosticTrackingParallelEmit",
                   TestFlakyDiagnosticTrackingParallelEmit);

} // namespace tsc::lsp
