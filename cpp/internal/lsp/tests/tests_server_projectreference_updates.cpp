// Port of tsc/internal/lsp/server_projectreference_updates_test.go
// (package lsp_test).
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/lsp/lsp.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/testutil/lsptestutil/lspclient.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/iovfs/iovfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::lsp {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace lsptestutil = tsc::testutil::lsptestutil;

lsproto::LSPAny prefsToAny(const lsutil::UserPreferences& prefs) {
	auto [jsonText, merr] = json::marshal(prefs);
	lsproto::LSPAny out;
	if (merr.empty()) {
		auto dec = json::newDecoderFrom(jsonText);
		out.unmarshalJSONFrom(dec);
	}
	return out;
}

// initMutableLSPClient — server_projectreference_updates_test.go:16.
std::pair<std::shared_ptr<lsptestutil::LSPClient>, std::shared_ptr<vfs::vfstest::MapFS>>
initMutableLSPClient(T* t,
                     const std::unordered_map<std::string, vfs::vfstest::MapFileInput>& files,
                     const std::shared_ptr<lsutil::UserPreferences>& prefs) {
	t->Helper();

	auto base = vfs::vfstest::FromMap(files, false);
	auto baseFS = std::dynamic_pointer_cast<vfs::vfstest::MapFS>(
	    std::dynamic_pointer_cast<vfs::iovfs::FsWithSys>(base)->FSys());
	auto fs = bundled::WrapFS(base);

	auto prefsAny = prefsToAny(*prefs);
	auto onServerRequest =
	    [prefsAny](const gostd::Context&,
	               const std::shared_ptr<lsproto::RequestMessage>& req)
	    -> std::shared_ptr<lsproto::ResponseMessage> {
		if (req->Method == lsproto::MethodWorkspaceConfiguration) {
			auto resp = std::make_shared<lsproto::ResponseMessage>();
			resp->ID = req->ID;
			resp->JSONRPC = req->JSONRPC;
			resp->Result = lsproto::AnyValue::of(
			    std::vector<lsproto::LSPAny>{prefsAny});
			return resp;
		}
		if (req->Method == lsproto::MethodClientRegisterCapability ||
		    req->Method == lsproto::MethodClientUnregisterCapability) {
			auto resp = std::make_shared<lsproto::ResponseMessage>();
			resp->ID = req->ID;
			resp->JSONRPC = req->JSONRPC;
			resp->Result = lsproto::AnyValue::of(lsproto::Null{});
			return resp;
		}
		return nullptr;
	};

	auto [client, closeClient] = lsptestutil::NewLSPClient(
	    t,
	    lsp::ServerOptions{.Err = gostd::io::discard(),
	                       .Cwd = "/root",
	                       .FS = fs,
	                       .DefaultLibraryPath = bundled::LibPath()},
	    onServerRequest);
	std::function<gostd::Error()> closeClientFn = closeClient;
	t->Cleanup([closeClientFn] { closeClientFn(); });

	auto initParams = std::make_shared<lsproto::InitializeParams>();
	initParams->Capabilities =
	    std::make_shared<lsproto::ClientCapabilities>();
	auto [initMsg, _1, ok] =
	    client->SendRequest(t, lsproto::InitializeInfo, initParams);
	assert::Assert(t, ok && initMsg->AsResponse()->Error == nullptr,
	               "Initialize failed");
	client->SendNotification(t, lsproto::InitializedInfo,
	                         std::make_shared<lsproto::InitializedParams>());
	client->Server->InitComplete()->wait();

	auto settings = std::make_shared<lsproto::DidChangeConfigurationParams>();
	settings->Settings = lsproto::LSPAny(
	    std::map<std::string, lsproto::LSPAny>{{"typescript", prefsAny}});
	client->SendNotification(t, lsproto::WorkspaceDidChangeConfigurationInfo,
	                         settings);

	return {client, baseFS};
}

} // namespace

// TestReferencesAfterAncestorProjectConfigDeletion —
// server_projectreference_updates_test.go:68.
void TestReferencesAfterAncestorProjectConfigDeletion1(T* t) {
	t->Parallel();

	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto [client, fs] = initMutableLSPClient(
	    t,
	    {
	        {"/root/tsconfig.json",
	         R"({
			"files": [],
			"references": [{ "path": "./project" }]
		})"},
	        {"/root/project/tsconfig.json",
	         R"({
			"compilerOptions": { "composite": true },
			"include": ["src/**/*.ts"]
		})"},
	        {"/root/project/src/main.ts",
	         "export function helloWorld() {}\nhelloWorld()\n"},
	    },
	    std::make_shared<lsutil::UserPreferences>());

	auto mainURI = lsconv::FileNameToDocumentURI(std::string("/root/project/src/main.ts"));
	auto openParams = std::make_shared<lsproto::DidOpenTextDocumentParams>();
	openParams->TextDocument =
	    std::make_shared<lsproto::TextDocumentItem>();
	openParams->TextDocument->Uri = mainURI;
	openParams->TextDocument->LanguageId = "typescript";
	openParams->TextDocument->Text =
	    "export function helloWorld() {}\nhelloWorld()\n";
	client->SendNotification(t, lsproto::TextDocumentDidOpenInfo, openParams);

	// Prime the child project so opening a file creates the ancestor
	// configured-project placeholder.
	auto dsParams = std::make_shared<lsproto::DocumentSymbolParams>();
	dsParams->TextDocument = lsproto::TextDocumentIdentifier{mainURI};
	auto [msg, _1, ok] =
	    client->SendRequest(t, lsproto::TextDocumentDocumentSymbolInfo,
	                        dsParams);
	assert::Assert(t, ok, "expected response");
	assert::Assert(t, msg->AsResponse()->Error == nullptr);

	assert::Assert(t, fs->Remove("root/tsconfig.json") == vfs::Error{}, "Remove failed");
	auto watchParams =
	    std::make_shared<lsproto::DidChangeWatchedFilesParams>();
	auto fe = std::make_shared<lsproto::FileEvent>();
	fe->Uri = lsconv::FileNameToDocumentURI(std::string("/root/tsconfig.json"));
	fe->Type = lsproto::FileChangeTypeDeleted;
	watchParams->Changes = {fe};
	client->SendNotification(t, lsproto::WorkspaceDidChangeWatchedFilesInfo,
	                         watchParams);

	auto refParams = std::make_shared<lsproto::ReferenceParams>();
	refParams->TextDocument = lsproto::TextDocumentIdentifier{mainURI};
	refParams->Position = lsproto::Position{1, 3};
	refParams->Context = std::make_shared<lsproto::ReferenceContext>();
	refParams->Context->IncludeDeclaration = true;
	auto [msg2, resp, ok2] =
	    client->SendRequest(t, lsproto::TextDocumentReferencesInfo, refParams);
	assert::Assert(t, ok2, "expected response");
	assert::Assert(t, msg2->AsResponse()->Error == nullptr);
	assert::Assert(t, resp.Locations != nullptr);
	assert::Equal(t, (int)(*resp.Locations)->size(), 2);
	std::vector<lsproto::Location> expected = {
	    lsproto::Location{mainURI,
	                      lsproto::Range{lsproto::Position{0, 16},
	                                     lsproto::Position{0, 26}}},
	    lsproto::Location{mainURI,
	                      lsproto::Range{lsproto::Position{1, 0},
	                                     lsproto::Position{1, 10}}},
	};
	assert::DeepEqual(t, expected, **resp.Locations);
}
REGISTER_UNIT_TEST("lsp.TestReferencesAfterAncestorProjectConfigDeletion1",
                   TestReferencesAfterAncestorProjectConfigDeletion1);

} // namespace tsc::lsp
