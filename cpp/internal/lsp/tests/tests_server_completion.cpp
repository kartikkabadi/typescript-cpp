// Port of tsc/internal/lsp/server_completion_test.go (package lsp_test).
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"
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

// marshal a value to an LSPAny (map[string]any/[]any payload material).
lsproto::LSPAny prefsToAny(const lsutil::UserPreferences& prefs) {
	auto [jsonText, merr] = json::marshal(prefs);
	lsproto::LSPAny out;
	if (merr.empty()) {
		auto dec = json::newDecoderFrom(jsonText);
		out.unmarshalJSONFrom(dec);
	}
	return out;
}

// initCompletionClient — server_completion_test.go:19.
std::shared_ptr<lsptestutil::LSPClient>
initCompletionClient(T* t,
                     const std::unordered_map<std::string, vfs::vfstest::MapFileInput>& files,
                     const std::shared_ptr<lsutil::UserPreferences>& prefs) {
	t->Helper();

	auto fs = bundled::WrapFS(vfs::vfstest::FromMap(files, false));

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

	auto [client, closeClient] =
	    lsptestutil::NewLSPClient(t,
	                              lsp::ServerOptions{.Err = gostd::io::discard(),
	                                                 .Cwd = "/home/projects",
	                                                 .FS = fs,
	                                                 .DefaultLibraryPath =
	                                                     bundled::LibPath()},
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

	return client;
}

// completionItems — server_completion_test.go:66.
std::vector<std::shared_ptr<lsproto::CompletionItem>>
completionItems(const lsproto::CompletionResponse& resp) {
	if (resp.List != nullptr) {
		return resp.List->Items.value_or(
		    std::vector<std::shared_ptr<lsproto::CompletionItem>>{});
	}
	if (resp.Items != nullptr) {
		return (*resp.Items).value_or(
		    std::vector<std::shared_ptr<lsproto::CompletionItem>>{});
	}
	return {};
}

// findCompletionItem — server_completion_test.go:76.
std::shared_ptr<lsproto::CompletionItem>
findCompletionItem(const std::vector<std::shared_ptr<lsproto::CompletionItem>>& items,
                   const std::string& label) {
	for (auto& item : items) {
		if (item->Label == label) {
			return item;
		}
	}
	return nullptr;
}

std::shared_ptr<lsutil::UserPreferences> autoImportPrefs() {
	return std::make_shared<lsutil::UserPreferences>(
	    [] {
		    lsutil::UserPreferences p;
		    p.IncludeCompletionsForModuleExports = Tristate::True;
		    p.IncludeCompletionsForImportStatements = Tristate::True;
		    return p;
	    }());
}

std::unordered_map<std::string, vfs::vfstest::MapFileInput> completionFiles() {
	return {
	    {"/home/projects/tsconfig.json",
	     "{\"compilerOptions\": {\"module\": \"esnext\", \"target\": \"esnext\"}}"},
	    {"/home/projects/a.ts", "export const someVar = 10;"},
	    {"/home/projects/b.ts", "s"},
	};
}

void openFile(lsptestutil::LSPClient* client, T* t,
              const lsproto::DocumentUri& uri, const std::string& text) {
	auto params = std::make_shared<lsproto::DidOpenTextDocumentParams>();
	params->TextDocument = std::make_shared<lsproto::TextDocumentItem>();
	params->TextDocument->Uri = uri;
	params->TextDocument->LanguageId = "typescript";
	params->TextDocument->Text = text;
	client->SendNotification(t, lsproto::TextDocumentDidOpenInfo, params);
}

std::shared_ptr<lsproto::DidCloseTextDocumentParams>
closeParams(const lsproto::DocumentUri& uri) {
	auto params = std::make_shared<lsproto::DidCloseTextDocumentParams>();
	params->TextDocument = lsproto::TextDocumentIdentifier{uri};
	return params;
}

std::shared_ptr<lsproto::CompletionParams>
completionParams(const lsproto::DocumentUri& uri, uint32_t line,
                 uint32_t character) {
	auto params = std::make_shared<lsproto::CompletionParams>();
	params->TextDocument = lsproto::TextDocumentIdentifier{uri};
	params->Position = lsproto::Position{line, character};
	params->Context = std::make_shared<lsproto::CompletionContext>();
	return params;
}

} // namespace

// TestCompletionAfterFileClose — server_completion_test.go:85.
void TestCompletionAfterFileClose(T* t) {
	t->Parallel();

	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto prefs = autoImportPrefs();
	auto client = initCompletionClient(t, completionFiles(), prefs);

	auto aURI = lsconv::FileNameToDocumentURI(std::string("/home/projects/a.ts"));
	auto bURI = lsconv::FileNameToDocumentURI(std::string("/home/projects/b.ts"));
	openFile(client.get(), t, aURI, "export const someVar = 10;");
	openFile(client.get(), t, bURI, "s");

	client->SendNotification(t, lsproto::TextDocumentDidCloseInfo,
	                         closeParams(bURI));

	auto [msg, resp, ok] =
	    client->SendRequest(t, lsproto::TextDocumentCompletionInfo,
	                        completionParams(bURI, 0, 1));
	assert::Assert(t, ok, "expected a response");
	assert::Assert(t, msg->AsResponse()->Error == nullptr);
	auto item = findCompletionItem(completionItems(resp), "someVar");
	assert::Assert(t, item != nullptr);
	assert::Assert(t, item->Data != nullptr && item->Data->AutoImport != nullptr);
	assert::Equal(t, item->Data->AutoImport->ModuleSpecifier, std::string("./a"));
}
REGISTER_UNIT_TEST("lsp.TestCompletionAfterFileClose",
                   TestCompletionAfterFileClose);

// TestCompletionWithConcurrentFileClose — server_completion_test.go:132.
void TestCompletionWithConcurrentFileClose(T* t) {
	t->Parallel();

	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto prefs = autoImportPrefs();
	auto client = initCompletionClient(t, completionFiles(), prefs);

	auto aURI = lsconv::FileNameToDocumentURI(std::string("/home/projects/a.ts"));
	auto bURI = lsconv::FileNameToDocumentURI(std::string("/home/projects/b.ts"));
	openFile(client.get(), t, aURI, "export const someVar = 10;");
	openFile(client.get(), t, bURI, "s");

	auto waitForCompletion =
	    client->SendRequestAsync(t, lsproto::TextDocumentCompletionInfo,
	                             completionParams(bURI, 0, 1));

	client->SendNotification(t, lsproto::TextDocumentDidCloseInfo,
	                         closeParams(bURI));

	auto [msg, resp, ok] = waitForCompletion();
	assert::Assert(t, ok, "expected a response");
	assert::Assert(t, msg->AsResponse()->Error == nullptr);
	auto item = findCompletionItem(completionItems(resp), "someVar");
	assert::Assert(t, item != nullptr);
	assert::Assert(t, item->Data != nullptr && item->Data->AutoImport != nullptr);
	assert::Equal(t, item->Data->AutoImport->ModuleSpecifier, std::string("./a"));
}
REGISTER_UNIT_TEST("lsp.TestCompletionWithConcurrentFileClose",
                   TestCompletionWithConcurrentFileClose);

// TestCompletionForUnopenedFile — server_completion_test.go:177.
void TestCompletionForUnopenedFile(T* t) {
	t->Parallel();

	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto prefs = std::make_shared<lsutil::UserPreferences>();
	auto client = initCompletionClient(
	    t,
	    {
	        {"/home/projects/tsconfig.json",
	         "{\"compilerOptions\": {\"module\": \"esnext\", \"target\": \"esnext\"}}"},
	        {"/home/projects/c.ts", "let xyz = 1;\nxy"},
	    },
	    prefs);

	auto cURI = lsconv::FileNameToDocumentURI(std::string("/home/projects/c.ts"));
	auto [msg, resp, ok] =
	    client->SendRequest(t, lsproto::TextDocumentCompletionInfo,
	                        completionParams(cURI, 1, 2));
	assert::Assert(t, ok, "expected a response");
	assert::Assert(t, msg->AsResponse()->Error == nullptr);
	assert::Assert(t,
	               findCompletionItem(completionItems(resp), "xyz") != nullptr);
}
REGISTER_UNIT_TEST("lsp.TestCompletionForUnopenedFile",
                   TestCompletionForUnopenedFile);

// TestAutoImportCompletionForUnopenedFile — server_completion_test.go:210.
void TestAutoImportCompletionForUnopenedFile(T* t) {
	t->Parallel();

	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto prefs = autoImportPrefs();
	auto client = initCompletionClient(
	    t,
	    {
	        {"/home/projects/tsconfig.json",
	         "{\"compilerOptions\": {\"module\": \"esnext\", \"target\": \"esnext\"}}"},
	        {"/home/projects/a.ts", "export const someVar = 10;"},
	        {"/home/projects/c.ts", "s"},
	    },
	    prefs);

	auto cURI = lsconv::FileNameToDocumentURI(std::string("/home/projects/c.ts"));
	auto [msg, resp, ok] =
	    client->SendRequest(t, lsproto::TextDocumentCompletionInfo,
	                        completionParams(cURI, 0, 1));
	assert::Assert(t, ok, "expected a response");
	assert::Assert(t, msg->AsResponse()->Error == nullptr);
	auto item = findCompletionItem(completionItems(resp), "someVar");
	assert::Assert(t, item != nullptr);
	assert::Assert(t, item->Data != nullptr && item->Data->AutoImport != nullptr);
	assert::Equal(t, item->Data->AutoImport->ModuleSpecifier, std::string("./a"));
}
REGISTER_UNIT_TEST("lsp.TestAutoImportCompletionForUnopenedFile",
                   TestAutoImportCompletionForUnopenedFile);

// TestCompletionSnapshotFreezing — server_completion_test.go:248.
void TestCompletionSnapshotFreezing(T* t) {
	t->Parallel();

	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto prefs = autoImportPrefs();
	auto client = initCompletionClient(
	    t,
	    {
	        {"/home/projects/tsconfig.json",
	         "{\"compilerOptions\": {\"module\": \"esnext\", \"target\": \"esnext\"}}"},
	        {"/home/projects/a.ts", "export const someVar = 10;"},
	        {"/home/projects/b.ts", "someV"},
	    },
	    prefs);

	auto aURI = lsconv::FileNameToDocumentURI(std::string("/home/projects/a.ts"));
	auto bURI = lsconv::FileNameToDocumentURI(std::string("/home/projects/b.ts"));
	openFile(client.get(), t, aURI, "export const someVar = 10;");
	openFile(client.get(), t, bURI, "someV");

	auto waitForCompletion =
	    client->SendRequestAsync(t, lsproto::TextDocumentCompletionInfo,
	                             completionParams(bURI, 0, 5));

	auto changeParams =
	    std::make_shared<lsproto::DidChangeTextDocumentParams>();
	changeParams->TextDocument = lsproto::VersionedTextDocumentIdentifier{bURI, 2};
	auto change = lsproto::TextDocumentContentChangePartialOrWholeDocument{};
	change.WholeDocument =
	    std::make_shared<lsproto::TextDocumentContentChangeWholeDocument>();
	change.WholeDocument->Text = "notMatching";
	changeParams->ContentChanges = {change};
	client->SendNotification(t, lsproto::TextDocumentDidChangeInfo,
	                         changeParams);

	auto [msg, resp, ok] = waitForCompletion();
	assert::Assert(t, ok, "expected a response");
	assert::Assert(t, msg->AsResponse()->Error == nullptr);
	auto item = findCompletionItem(completionItems(resp), "someVar");
	assert::Assert(
	    t, item != nullptr,
	    "expected someVar in completions (snapshot freezing should preserve original content)");
	assert::Assert(t, item->Data != nullptr && item->Data->AutoImport != nullptr);
	assert::Equal(t, item->Data->AutoImport->ModuleSpecifier, std::string("./a"));
}
REGISTER_UNIT_TEST("lsp.TestCompletionSnapshotFreezing",
                   TestCompletionSnapshotFreezing);

} // namespace tsc::lsp
