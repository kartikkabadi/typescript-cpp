// Port of tsc/internal/lsp/server_semantictokens_test.go (package lsp_test).
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

std::vector<std::string> semanticTokenTypes() {
	return {"namespace", "type",         "class",   "enum",
	        "interface", "struct",       "typeParameter",
	        "parameter", "variable",     "property", "enumMember",
	        "event",     "function",     "method",   "macro",
	        "keyword",   "modifier",     "comment",  "string",
	        "number",    "regexp",       "operator", "decorator"};
}

std::vector<std::string> semanticTokenModifiers() {
	return {"declaration", "definition",    "readonly",
	        "static",      "deprecated",    "abstract",
	        "async",       "modification",  "documentation",
	        "defaultLibrary", "local"};
}

std::shared_ptr<lsproto::SemanticTokensClientCapabilities>
semanticTokensCaps() {
	auto caps = std::make_shared<lsproto::SemanticTokensClientCapabilities>();
	caps->Requests =
	    std::make_shared<lsproto::ClientSemanticTokensRequestOptions>();
	caps->Requests->Full = std::make_shared<
	    lsproto::BooleanOrClientSemanticTokensRequestFullDelta>();
	caps->Requests->Full->Boolean = std::make_shared<bool>(true);
	caps->TokenTypes = semanticTokenTypes();
	caps->TokenModifiers = semanticTokenModifiers();
	return caps;
}

std::shared_ptr<lsproto::SemanticTokensParams>
semanticTokensParams(const lsproto::DocumentUri& uri) {
	auto params = std::make_shared<lsproto::SemanticTokensParams>();
	params->TextDocument = lsproto::TextDocumentIdentifier{uri};
	return params;
}

void sendInitialize(lsptestutil::LSPClient* client, T* t) {
	auto initParams = std::make_shared<lsproto::InitializeParams>();
	initParams->Capabilities =
	    std::make_shared<lsproto::ClientCapabilities>();
	initParams->Capabilities->TextDocument =
	    std::make_shared<lsproto::TextDocumentClientCapabilities>();
	initParams->Capabilities->TextDocument->SemanticTokens =
	    semanticTokensCaps();
	auto [initMsg, _1, ok] =
	    client->SendRequest(t, lsproto::InitializeInfo, initParams);
	assert::Assert(t, ok && initMsg->AsResponse()->Error == nullptr,
	               "Initialize failed");
	client->SendNotification(t, lsproto::InitializedInfo,
	                         std::make_shared<lsproto::InitializedParams>());
	client->Server->InitComplete()->wait();
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

} // namespace

// TestSemanticTokensCRLF — server_semantictokens_test.go:21.
void TestSemanticTokensCRLF(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	// Enough lines so the cumulative \r\n vs \n offset difference
	// causes an LF-based position to land on a \r in the CRLF text.
	std::string fileOnDisk =
	    "var x\nvar x\nvar x\nvar x\nvar x\nvar x\nconst a = 1\n";
	std::string fileFromEditor = fileOnDisk;
	{
		size_t pos = 0;
		while ((pos = fileFromEditor.find('\n', pos)) != std::string::npos) {
			fileFromEditor.replace(pos, 1, "\r\n");
			pos += 2;
		}
	}

	auto files = std::unordered_map<std::string, vfs::vfstest::MapFileInput>{
	    {"/home/projects/tsconfig.json", "{}"},
	    {"/home/projects/test.ts", fileOnDisk},
	    {"/home/projects/other.ts", "export {}"},
	};
	auto fs = bundled::WrapFS(vfs::vfstest::FromMap(files, false));

	auto onServerRequest =
	    [](const gostd::Context&,
	       const std::shared_ptr<lsproto::RequestMessage>& req)
	    -> std::shared_ptr<lsproto::ResponseMessage> {
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
	                       .Cwd = "/home/projects",
	                       .FS = fs,
	                       .DefaultLibraryPath = bundled::LibPath()},
	    onServerRequest);
	std::function<gostd::Error()> closeClientFn = closeClient;
	t->Cleanup([closeClientFn] { closeClientFn(); });

	sendInitialize(client.get(), t);

	// Open another project file to force the project to load test.ts from
	// disk (LF).
	auto otherUri = lsproto::DocumentUri("file:///home/projects/other.ts");
	openFile(client.get(), t, otherUri, files["/home/projects/other.ts"].index() == 0 ? std::get<0>(files["/home/projects/other.ts"]) : std::string{});
	auto [msg1, _1, _ok1] =
	    client->SendRequest(t, lsproto::TextDocumentSemanticTokensFullInfo,
	                        semanticTokensParams(otherUri));
	assert::Assert(t, msg1->AsResponse()->Error == nullptr,
	               "Initial request failed");

	// Open test.ts with CRLF content; the project already parsed it from
	// disk (LF).
	auto uri = lsproto::DocumentUri("file:///home/projects/test.ts");
	openFile(client.get(), t, uri, fileFromEditor);

	// This panics: AST positions are LF-based but the line map is
	// CRLF-based.
	auto [msg, _2, _ok2] =
	    client->SendRequest(t, lsproto::TextDocumentSemanticTokensFullInfo,
	                        semanticTokensParams(uri));
	if (msg->AsResponse()->Error != nullptr) {
		t->Fatalf("Semantic tokens request failed: %s",
		          {msg->AsResponse()->Error->Message});
	}
}
REGISTER_UNIT_TEST("lsp.TestSemanticTokensCRLF", TestSemanticTokensCRLF);

// TestSemanticTokensDefaultLibraryCaseInsensitive —
// server_semantictokens_test.go:103.
void TestSemanticTokensDefaultLibraryCaseInsensitive(T* t) {
	t->Parallel();

	std::string libContent = R"(/// <reference no-default-lib="true"/>
interface Boolean {}
interface Function {}
interface CallableFunction {}
interface NewableFunction {}
interface IArguments {}
interface Number { toExponential: any; }
interface Object {}
interface RegExp {}
interface String { charAt: any; }
interface Array<T> { length: number; [n: number]: T; }
interface ReadonlyArray<T> {}
declare const console: { log(msg: any): void; };
)";

	auto files = std::unordered_map<std::string, vfs::vfstest::MapFileInput>{
	    {"/home/projects/tsconfig.json",
	     "{\"compilerOptions\": {\"lib\": [\"es5\"]}}"},
	    {"/home/projects/test.ts", "console.log(\"hi\");"},
	    {"/TSLib/lib.es5.d.ts", libContent},
	};
	// Case-insensitive VFS: canonical paths are lowercased, so the lib's
	// canonical path (/tslib/lib.es5.d.ts) differs from its file name.
	auto fs = vfs::vfstest::FromMap(files, false);

	auto onServerRequest =
	    [](const gostd::Context&,
	       const std::shared_ptr<lsproto::RequestMessage>& req)
	    -> std::shared_ptr<lsproto::ResponseMessage> {
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
	                       .Cwd = "/home/projects",
	                       .FS = fs,
	                       .DefaultLibraryPath = "/TSLib"},
	    onServerRequest);
	std::function<gostd::Error()> closeClientFn = closeClient;
	t->Cleanup([closeClientFn] { closeClientFn(); });

	sendInitialize(client.get(), t);

	auto uri = lsproto::DocumentUri("file:///home/projects/test.ts");
	openFile(client.get(), t, uri, files["/home/projects/test.ts"].index() == 0 ? std::get<0>(files["/home/projects/test.ts"]) : std::string{});

	auto [msg, result, ok] =
	    client->SendRequest(t, lsproto::TextDocumentSemanticTokensFullInfo,
	                        semanticTokensParams(uri));
	assert::Assert(t, ok, "Semantic tokens request did not return a result");
	if (msg->AsResponse()->Error != nullptr) {
		t->Fatalf("Semantic tokens request failed: %s",
		          {msg->AsResponse()->Error->Message});
	}
	assert::Assert(t, result.SemanticTokens != nullptr,
	               "Expected non-null semantic tokens");

	const auto data =
	    result.SemanticTokens->Data.value_or(std::vector<uint32_t>{});
	assert::Assert(t, data.size() >= 5 && data.size() % 5 == 0,
	               "Malformed semantic tokens data");

	// Tokens are encoded as 5 ints each; the 5th is the modifier bitset.
	// defaultLibrary is index 9 in the TokenModifiers legend above.
	const uint32_t defaultLibraryBit = 1 << 9;
	bool hasDefaultLibrary = false;
	for (size_t i = 4; i < data.size(); i += 5) {
		if (data[i] & defaultLibraryBit) {
			hasDefaultLibrary = true;
			break;
		}
	}
	assert::Assert(
	    t, hasDefaultLibrary,
	    "Expected at least one token with the defaultLibrary modifier (console is declared in the default library)");
}
REGISTER_UNIT_TEST("lsp.TestSemanticTokensDefaultLibraryCaseInsensitive",
                   TestSemanticTokensDefaultLibraryCaseInsensitive);

} // namespace tsc::lsp
