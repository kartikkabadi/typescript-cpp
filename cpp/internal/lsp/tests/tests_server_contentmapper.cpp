// Port of tsc/internal/lsp/server_contentmapper_test.go (package lsp_test).
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/bundled/bundled.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsp.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/testutil/contentmappertest/contentmappertest.h"
#include "internal/testutil/lsptestutil/lspclient.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::lsp {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace lsptestutil = tsc::testutil::lsptestutil;

std::vector<lsproto::CodeActionKind> expectedCodeActionKinds() {
	return {
	    lsproto::CodeActionKindQuickFix,
	    "source.organizeImports.ts",
	    "source.removeUnusedImports.ts",
	    "source.sortImports.ts",
	    "source.fixAll.ts",
	};
}

const std::string component = R"(<component name="ProfileCard">
<template><h1>{{ title }}</h1></template>
<script lang="ts">
export const title = "Profile";
</script>)";

} // namespace

// TestSetContentMapperContributionsBeforeDidOpen —
// server_contentmapper_test.go:19.
void TestSetContentMapperContributionsBeforeDidOpen(T* t) {
	t->Parallel();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}

	auto files = std::unordered_map<std::string, vfs::vfstest::MapFileInput>{
	    {"/home/project/tsconfig.json",
	     R"({
			"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler", "strict": true },
			"contentMappers": [ { "package": "mapper", "extensions": [".vue"] } ]
		})"},
	    {"/home/project/node_modules/mapper/package.json",
	     testutil::contentmappertest::PackageJSON(
	         testutil::contentmappertest::ComponentMapper)},
	    {"/home/project/ProfileCard.vue", component},
	};

	std::mutex mu;
	std::vector<std::shared_ptr<lsproto::Registration>> registrations;
	std::vector<std::shared_ptr<lsproto::Unregistration>> unregistrations;
	// unregisteredSignal — chan struct{} capacity 1; signaled state.
	std::mutex sigMu;
	std::condition_variable sigCv;
	bool unregisteredSignal = false;

	auto onServerRequest =
	    [&](const gostd::Context&,
	        const std::shared_ptr<lsproto::RequestMessage>& req)
	    -> std::shared_ptr<lsproto::ResponseMessage> {
		if (req->Method == lsproto::MethodWorkspaceConfiguration) {
			auto resp = std::make_shared<lsproto::ResponseMessage>();
			resp->ID = req->ID;
			resp->JSONRPC = req->JSONRPC;
			resp->Result =
			    lsproto::AnyValue::of(std::vector<lsproto::LSPAny>(4));
			return resp;
		}
		if (req->Method == lsproto::MethodClientRegisterCapability) {
			auto [params, err] =
			    req->UnmarshalParams<
			        std::shared_ptr<lsproto::RegistrationParams>>();
			assert::NilError(t, err);
			{
				std::lock_guard<std::mutex> lk(mu);
				for (auto& r : *params->Registrations) {
					registrations.push_back(r);
				}
			}
			auto resp = std::make_shared<lsproto::ResponseMessage>();
			resp->ID = req->ID;
			resp->JSONRPC = req->JSONRPC;
			resp->Result = lsproto::AnyValue::of(lsproto::Null{});
			return resp;
		}
		if (req->Method == lsproto::MethodClientUnregisterCapability) {
			auto [params, err] =
			    req->UnmarshalParams<
			        std::shared_ptr<lsproto::UnregistrationParams>>();
			assert::NilError(t, err);
			{
				std::lock_guard<std::mutex> lk(mu);
				for (auto& u : *params->Unregisterations) {
					unregistrations.push_back(u);
				}
			}
			{
				std::lock_guard<std::mutex> lk(sigMu);
				unregisteredSignal = true;
			}
			sigCv.notify_all();
			auto resp = std::make_shared<lsproto::ResponseMessage>();
			resp->ID = req->ID;
			resp->JSONRPC = req->JSONRPC;
			resp->Result = lsproto::AnyValue::of(lsproto::Null{});
			return resp;
		}
		return nullptr;
	};

	auto fs = bundled::WrapFS(vfs::vfstest::FromMap(files, false));
	auto* spawner = testutil::contentmappertest::NewSpawner();
	lsp::ServerOptions opts;
	opts.Err = gostd::io::discard();
	opts.Cwd = "/home/project";
	opts.FS = fs;
	opts.DefaultLibraryPath = bundled::LibPath();
	opts.Spawn = [spawner](const std::vector<std::string>& command,
	                       const std::string& dir, gostd::io::Writer* stderr) {
		return spawner->Spawn(command, dir, stderr);
	};
	auto [client, closeClient] =
	    lsptestutil::NewLSPClient(t, opts, onServerRequest);
	std::function<gostd::Error()> closeClientFn = closeClient;
	t->Cleanup([closeClientFn] { closeClientFn(); });

	auto caps = std::make_shared<lsproto::ClientCapabilities>();
	caps->Workspace =
	    std::make_shared<lsproto::WorkspaceClientCapabilities>();
	caps->Workspace->FileOperations =
	    std::make_shared<lsproto::FileOperationClientCapabilities>();
	caps->Workspace->FileOperations->DynamicRegistration = true;
	caps->Workspace->FileOperations->WillRename = true;
	caps->TextDocument =
	    std::make_shared<lsproto::TextDocumentClientCapabilities>();
	caps->TextDocument->Synchronization = std::make_shared<
	    lsproto::TextDocumentSyncClientCapabilities>();
	caps->TextDocument->Synchronization->DynamicRegistration = true;
	caps->TextDocument->DocumentSymbol =
	    std::make_shared<lsproto::DocumentSymbolClientCapabilities>();
	caps->TextDocument->DocumentSymbol->DynamicRegistration = true;
	caps->TextDocument->FoldingRange =
	    std::make_shared<lsproto::FoldingRangeClientCapabilities>();
	caps->TextDocument->FoldingRange->DynamicRegistration = true;
	caps->TextDocument->SelectionRange =
	    std::make_shared<lsproto::SelectionRangeClientCapabilities>();
	caps->TextDocument->SelectionRange->DynamicRegistration = true;
	caps->TextDocument->InlayHint =
	    std::make_shared<lsproto::InlayHintClientCapabilities>();
	caps->TextDocument->InlayHint->DynamicRegistration = true;
	caps->TextDocument->CodeLens =
	    std::make_shared<lsproto::CodeLensClientCapabilities>();
	caps->TextDocument->CodeLens->DynamicRegistration = true;
	caps->TextDocument->CodeAction =
	    std::make_shared<lsproto::CodeActionClientCapabilities>();
	caps->TextDocument->CodeAction->DynamicRegistration = true;
	caps->TextDocument->Formatting = std::make_shared<
	    lsproto::DocumentFormattingClientCapabilities>();
	caps->TextDocument->Formatting->DynamicRegistration = true;
	caps->TextDocument->RangeFormatting = std::make_shared<
	    lsproto::DocumentRangeFormattingClientCapabilities>();
	caps->TextDocument->RangeFormatting->DynamicRegistration = true;
	caps->TextDocument->OnTypeFormatting = std::make_shared<
	    lsproto::DocumentOnTypeFormattingClientCapabilities>();
	caps->TextDocument->OnTypeFormatting->DynamicRegistration = true;
	caps->TextDocument->LinkedEditingRange = std::make_shared<
	    lsproto::LinkedEditingRangeClientCapabilities>();
	caps->TextDocument->LinkedEditingRange->DynamicRegistration = true;
	caps->TextDocument->CallHierarchy =
	    std::make_shared<lsproto::CallHierarchyClientCapabilities>();
	caps->TextDocument->CallHierarchy->DynamicRegistration = true;
	caps->TextDocument->SemanticTokens =
	    std::make_shared<lsproto::SemanticTokensClientCapabilities>();
	caps->TextDocument->SemanticTokens->DynamicRegistration = true;
	caps->TextDocument->SemanticTokens->Requests =
	    std::make_shared<lsproto::ClientSemanticTokensRequestOptions>();
	caps->TextDocument->SemanticTokens->TokenTypes = {};
	caps->TextDocument->SemanticTokens->TokenModifiers = {};
	caps->TextDocument->SemanticTokens->Formats =
	    std::vector<lsproto::TokenFormat>{lsproto::TokenFormatRelative};

	auto initParams = std::make_shared<lsproto::InitializeParams>();
	initParams->Capabilities = caps;
	initParams->InitializationOptions =
	    std::make_shared<lsproto::InitializationOptionsOrNull>();
	initParams->InitializationOptions->InitializationOptions =
	    std::make_shared<lsproto::InitializationOptions>();
	initParams->InitializationOptions->InitializationOptions->RunExternalCode =
	    true;
	auto [initMsg, _1, ok] =
	    client->SendRequest(t, lsproto::InitializeInfo, initParams);
	assert::Assert(t, ok && initMsg->AsResponse()->Error == nullptr,
	               "initialize failed");
	client->SendNotification(t, lsproto::InitializedInfo,
	                         std::make_shared<lsproto::InitializedParams>());
	client->Server->InitComplete()->wait();

	auto uri = lsproto::DocumentUri("file:///home/project/ProfileCard.vue");
	auto setParams =
	    std::make_shared<lsproto::SetContentMapperContributionsParams>();
	setParams->OpenDocuments = {lsproto::TextDocumentIdentifier{uri}};
	auto contribution =
	    std::make_shared<lsproto::ContentMapperContribution>();
	contribution->ContributorId = "test";
	contribution->Extensions = {".vue", ".svelte"};
	setParams->Contributions = {contribution};
	auto [msg, _2, ok2] = client->SendRequest(
	    t, lsproto::CustomSetContentMapperContributionsInfo, setParams);
	assert::Assert(t, ok2 && msg->AsResponse()->Error == nullptr);

	std::vector<std::shared_ptr<lsproto::Registration>> registered;
	{
		std::lock_guard<std::mutex> lk(mu);
		registered = registrations;
	}
	assert::Assert(t, !registered.empty(), "expected dynamic registrations");
	std::unordered_map<std::string, bool> expectedMapperRegistrations = {
	    {"content-mapper-did-open", false},
	    {"content-mapper-did-change", false},
	    {"content-mapper-did-close", false},
	    {"content-mapper-semantic-tokens", false},
	    {"content-mapper-document-symbol", false},
	    {"content-mapper-folding-range", false},
	    {"content-mapper-selection-range", false},
	    {"content-mapper-inlay-hint", false},
	    {"content-mapper-code-lens", false},
	    {"content-mapper-code-action", false},
	    {"content-mapper-formatting", false},
	    {"content-mapper-range-formatting", false},
	    {"content-mapper-on-type-formatting", false},
	    {"content-mapper-linked-editing", false},
	    {"content-mapper-call-hierarchy", false},
	    {"content-mapper-will-rename-files", false},
	};
	for (auto& registration : registered) {
		auto it = expectedMapperRegistrations.find(registration->Id);
		if (it == expectedMapperRegistrations.end()) {
			assert::Assert(
			    t, !registration->Id.starts_with("content-mapper-"),
			    "unexpected unsupported content mapper registration");
		} else {
			it->second = true;
		}
		if (registration->Id == "content-mapper-did-open") {
			assert::Assert(t,
			               registration->RegisterOptions != nullptr &&
			                   registration->RegisterOptions
			                           ->TextDocumentDidOpen != nullptr);
			auto& selector = registration->RegisterOptions
			                     ->TextDocumentDidOpen->DocumentSelector
			                     .DocumentSelector;
			assert::Assert(t,
			               selector != nullptr && (*selector)->size() == 1);
			assert::Equal(
			    t, *(**selector)[0].Pattern->Pattern.Pattern,
			    std::string("**/*.vue"));
		}
		if (registration->Id == "content-mapper-semantic-tokens") {
			assert::Assert(t,
			               registration->RegisterOptions != nullptr &&
			                   registration->RegisterOptions
			                           ->TextDocumentSemanticTokens != nullptr);
			auto& selector = registration->RegisterOptions
			                     ->TextDocumentSemanticTokens->DocumentSelector
			                     .DocumentSelector;
			assert::Assert(t,
			               selector != nullptr && (*selector)->size() == 1);
			assert::Equal(
			    t, *(**selector)[0].Pattern->Pattern.Pattern,
			    std::string("**/*.vue"));
		}
		if (registration->Id == "content-mapper-code-action") {
			assert::Assert(t,
			               registration->RegisterOptions != nullptr &&
			                   registration->RegisterOptions
			                           ->TextDocumentCodeAction != nullptr);
			assert::DeepEqual(
			    t,
			    registration->RegisterOptions->TextDocumentCodeAction
			            ->CodeActionKinds->value_or(
			                std::vector<lsproto::CodeActionKind>{}),
			    expectedCodeActionKinds());
		}
	}
	for (auto& [id, found] : expectedMapperRegistrations) {
		assert::Assert(t, found, "expected registration for .vue: " + id);
	}

	auto openParams = std::make_shared<lsproto::DidOpenTextDocumentParams>();
	openParams->TextDocument =
	    std::make_shared<lsproto::TextDocumentItem>();
	openParams->TextDocument->Uri = uri;
	openParams->TextDocument->LanguageId = "vue";
	openParams->TextDocument->Version = 1;
	openParams->TextDocument->Text = component;
	client->SendNotification(t, lsproto::TextDocumentDidOpenInfo, openParams);
	auto hoverParams = std::make_shared<lsproto::HoverParams>();
	hoverParams->TextDocument = lsproto::TextDocumentIdentifier{uri};
	hoverParams->Position = lsproto::Position{3, 15};
	auto [hoverMsg, hover, ok3] =
	    client->SendRequest(t, lsproto::TextDocumentHoverInfo, hoverParams);
	assert::Assert(t, ok3 && hoverMsg->AsResponse()->Error == nullptr);
	assert::Assert(t, hover.Hover != nullptr,
	               "expected hover after first foreign didOpen");

	assert::Assert(t,
	               fs->WriteFile("/home/project/tsconfig.json", R"({
		"compilerOptions": { "target": "es2020", "module": "esnext", "moduleResolution": "bundler", "strict": true }
	})") == vfs::Error{},
	               "WriteFile failed");
	auto watchParams =
	    std::make_shared<lsproto::DidChangeWatchedFilesParams>();
	auto fe = std::make_shared<lsproto::FileEvent>();
	fe->Uri = "file:///home/project/tsconfig.json";
	fe->Type = lsproto::FileChangeTypeChanged;
	watchParams->Changes = {fe};
	client->SendNotification(t, lsproto::WorkspaceDidChangeWatchedFilesInfo,
	                         watchParams);
	auto [hoverMsg2, hover2, _ok4] = client->SendRequest(
	    t, lsproto::TextDocumentHoverInfo, hoverParams);
	assert::Assert(
	    t, hoverMsg2 != nullptr && hoverMsg2->AsResponse()->Error == nullptr,
	    "request before didClose should return a null result");
	assert::Assert(t, hover2.Hover == nullptr);
	auto ddParams = std::make_shared<lsproto::DocumentDiagnosticParams>();
	ddParams->TextDocument = lsproto::TextDocumentIdentifier{uri};
	auto [diagnosticMsg, diagnostics, ok5] = client->SendRequest(
	    t, lsproto::TextDocumentDiagnosticInfo, ddParams);
	assert::Assert(
	    t, ok5 && diagnosticMsg->AsResponse()->Error == nullptr,
	    "diagnostics before didClose should return an empty report");
	assert::Assert(t,
	               diagnostics.FullDocumentDiagnosticReport != nullptr);
	assert::Equal(
	    t,
	    (int)diagnostics.FullDocumentDiagnosticReport->Items.value_or(
	        std::vector<std::shared_ptr<lsproto::Diagnostic>>{})
	        .size(),
	    0);
	auto compParams = std::make_shared<lsproto::CompletionParams>();
	compParams->TextDocument = lsproto::TextDocumentIdentifier{uri};
	compParams->Position = lsproto::Position{3, 15};
	auto [completionMsg, completion, ok6] = client->SendRequest(
	    t, lsproto::TextDocumentCompletionInfo, compParams);
	assert::Assert(t, ok6 && completionMsg->AsResponse()->Error == nullptr);
	assert::Assert(t,
	               completion.Items == nullptr && completion.List == nullptr);
	auto refParams = std::make_shared<lsproto::ReferenceParams>();
	refParams->TextDocument = lsproto::TextDocumentIdentifier{uri};
	refParams->Position = lsproto::Position{3, 15};
	refParams->Context = std::make_shared<lsproto::ReferenceContext>();
	refParams->Context->IncludeDeclaration = true;
	auto [referencesMsg, references, ok7] = client->SendRequest(
	    t, lsproto::TextDocumentReferencesInfo, refParams);
	assert::Assert(t, ok7 && referencesMsg->AsResponse()->Error == nullptr);
	assert::Assert(t, references.Locations == nullptr);
	auto renParams = std::make_shared<lsproto::RenameParams>();
	renParams->TextDocument = lsproto::TextDocumentIdentifier{uri};
	renParams->Position = lsproto::Position{3, 15};
	renParams->NewName = "renamed";
	auto [renameMsg, rename, ok8] =
	    client->SendRequest(t, lsproto::TextDocumentRenameInfo, renParams);
	assert::Assert(t, ok8 && renameMsg->AsResponse()->Error == nullptr);
	assert::Assert(t, rename.WorkspaceEdit == nullptr);
	{
		std::unique_lock<std::mutex> lk(sigMu);
		sigCv.wait(lk, [&] { return unregisteredSignal; });
	}
	std::vector<std::shared_ptr<lsproto::Unregistration>> unregistered;
	{
		std::lock_guard<std::mutex> lk(mu);
		unregistered = unregistrations;
	}
	assert::Assert(t, !unregistered.empty(),
	               "expected dynamic unregistration");
	std::unordered_map<std::string, bool> expectedUnregistrations;
	for (auto& [id, _] : expectedMapperRegistrations) {
		expectedUnregistrations[id] = false;
	}
	for (auto& unregistration : unregistered) {
		auto it = expectedUnregistrations.find(unregistration->Id);
		if (it == expectedUnregistrations.end()) {
			assert::Assert(
			    t, !unregistration->Id.starts_with("content-mapper-"),
			    "unexpected unsupported content mapper unregistration");
		} else {
			it->second = true;
		}
	}
	for (auto& [id, found] : expectedUnregistrations) {
		assert::Assert(t, found, "expected unregistration: " + id);
	}

	auto closeParams =
	    std::make_shared<lsproto::DidCloseTextDocumentParams>();
	closeParams->TextDocument = lsproto::TextDocumentIdentifier{uri};
	client->SendNotification(t, lsproto::TextDocumentDidCloseInfo,
	                         closeParams);
}
REGISTER_UNIT_TEST("lsp.TestSetContentMapperContributionsBeforeDidOpen",
                   TestSetContentMapperContributionsBeforeDidOpen);

} // namespace tsc::lsp
