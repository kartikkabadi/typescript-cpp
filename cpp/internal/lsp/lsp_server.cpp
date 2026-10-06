// server.go (part 1) — port of tsc/internal/lsp/server.go: Server
// construction, the project.Client/ata.NpmExecutor implementation, the
// read/write/dispatch loops, the send path, request dispatch, the handler
// table, and crossProjectOrchestrator. Request handler methods live in
// lsp_handlers.cpp.
#include "internal/lsp/lsp.h"

#include <thread>

#include "internal/api/session.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/fswatch/fswatch.h"
#include "internal/ipc/ipc.h"
#include "internal/json/json.h"
#include "internal/ls/ls.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/lsp/lspwatcher/lspwatcher.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

namespace tsc::lsp {

// ---------------------------------------------------------------------------
// formatGoDuration — time.Duration.String().
// ---------------------------------------------------------------------------
std::string formatGoDuration(gostd::Duration d) {
	auto ns = d.count();
	if (ns == 0) {
		return "0s";
	}
	bool neg = ns < 0;
	if (neg) {
		ns = -ns;
	}
	std::string out = neg ? "-" : "";
	const int64_t hour = 3600LL * 1000 * 1000 * 1000;
	const int64_t minute = 60LL * 1000 * 1000 * 1000;
	const int64_t sec = 1000LL * 1000 * 1000;
	if (ns >= hour) {
		out += std::to_string(ns / hour) + "h";
		ns %= hour;
	}
	if (ns >= minute) {
		out += std::to_string(ns / minute) + "m";
		ns %= minute;
	}
	if (out.size() > (neg ? 1u : 0u)) {
		if (ns > 0) {
			auto whole = ns / sec;
			auto frac = ns % sec;
			out += std::to_string(whole);
			if (frac > 0) {
				char buf[16];
				std::snprintf(buf, sizeof buf, ".%09lld", (long long)frac);
				std::string f(buf);
				while (!f.empty() && f.back() == '0') f.pop_back();
				out += f;
			}
			out += "s";
		}
		return out;
	}
	if (ns >= sec) {
		auto whole = ns / sec;
		auto frac = ns % sec;
		out += std::to_string(whole);
		if (frac > 0) {
			char buf[16];
			std::snprintf(buf, sizeof buf, ".%09lld", (long long)frac);
			std::string f(buf);
			while (!f.empty() && f.back() == '0') f.pop_back();
			out += f;
		}
		out += "s";
		return out;
	}
	if (ns >= 1000LL * 1000) {
		auto whole = ns / (1000LL * 1000);
		auto frac = ns % (1000LL * 1000);
		out += std::to_string(whole);
		if (frac > 0) {
			char buf[16];
			std::snprintf(buf, sizeof buf, ".%06lld", (long long)frac);
			std::string f(buf);
			while (!f.empty() && f.back() == '0') f.pop_back();
			out += f;
		}
		out += "ms";
		return out;
	}
	if (ns >= 1000) {
		auto whole = ns / 1000;
		auto frac = ns % 1000;
		out += std::to_string(whole);
		if (frac > 0) {
			char buf[8];
			std::snprintf(buf, sizeof buf, ".%03lld", (long long)frac);
			std::string f(buf);
			while (!f.empty() && f.back() == '0') f.pop_back();
			out += f;
		}
		out += "µs";
		return out;
	}
	out += std::to_string(ns) + "ns";
	return out;
}

// ---------------------------------------------------------------------------
// server.go:133-164 — lspReader.Read / lspWriter.Write / ToReader / ToWriter.
// ---------------------------------------------------------------------------

// Read — server.go:133.
std::pair<std::shared_ptr<lsproto::Message>, gostd::Error> lspReader::Read() {
	auto [data, err] = r.Read();
	if (err != nullptr) {
		return {nullptr, err};
	}

	auto req = std::make_shared<lsproto::Message>();
	if (auto uerr = json::unmarshal(data, req.get()); !uerr.empty()) {
		// Go wraps ErrorCodeInvalidParams when the unmarshal error chain
		// contains it; Message::unmarshalJSON only ever wraps InvalidRequest,
		// so that branch is unreachable and only InvalidRequest is wrapped.
		return {nullptr, wrapCodeError(lsproto::ErrorCodeInvalidRequest,
		                             gostd::newError(uerr))};
	}
	return {req, nullptr};
}

// Write — server.go:154.
gostd::Error lspWriter::Write(
	const std::shared_ptr<lsproto::Message>& msg) {
	auto [data, err] = json::marshal(*msg);
	if (!err.empty()) {
		return std::make_shared<messageMarshalError>(gostd::newError(err));
	}
	return w.Write(data);
}

// ToReader — server.go:150.
std::shared_ptr<Reader> ToReader(gostd::io::Reader* r) {
	return std::make_shared<lspReader>(r);
}

// ToWriter — server.go:162.
std::shared_ptr<Writer> ToWriter(gostd::io::Writer* w) {
	return std::make_shared<lspWriter>(w);
}

// ---------------------------------------------------------------------------
// server.go:60 — NewServer.
// ---------------------------------------------------------------------------
Server::Server(const ServerOptions& opts) {
	if (opts.Cwd.empty()) {
		throw goPanic{"Cwd is required"}; // panic("Cwd is required")
	}
	r = opts.In;
	w = opts.Out;
	stderr = opts.Err;
	cwd = opts.Cwd;
	fs = opts.FS;
	defaultLibraryPath = opts.DefaultLibraryPath;
	typingsLocation = opts.TypingsLocation;
	parseCache = opts.ParseCache;
	npmInstall = opts.NpmInstall;
	spawn = opts.Spawn;
	startWatchdog = opts.SetParentProcessID;
	initComplete = std::make_shared<detail::closeSignal>();
	progressDelay = opts.ProgressDelay;
	logger = std::make_shared<lsp::logger>(this);
}

// NewServer — server.go:60.
std::shared_ptr<Server> NewServer(const ServerOptions& opts) {
	return std::make_shared<Server>(opts);
}

// fileRenameFilters — server.go:90.
const std::vector<std::shared_ptr<lsproto::FileOperationFilter>>&
fileRenameFilters() {
	static const auto filters = [] {
		auto f = std::make_shared<lsproto::FileOperationFilter>();
		f->Scheme = std::string("file");
		f->Pattern = std::make_shared<lsproto::FileOperationPattern>();
		f->Pattern->Glob = "**/*.{ts,tsx,js,jsx,cts,cjs,mts,mjs,json}";
		return std::vector<std::shared_ptr<lsproto::FileOperationFilter>>{
			std::move(f)};
	}();
	return filters;
}

// supportedCodeActionKinds — server.go:352.
std::vector<lsproto::CodeActionKind> supportedCodeActionKinds() {
	return {
		lsproto::CodeActionKindQuickFix,
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		lsproto::CodeActionKindSourceRemoveUnusedImportsTs,
		lsproto::CodeActionKindSourceSortImportsTs,
		lsproto::CodeActionKindSourceFixAllTs,
	};
}

// WatchFiles — server.go:263 (project.Client).
gostd::Error Server::WatchFiles(
	const gostd::Context& ctx, project::WatcherID id,
	const std::vector<lsproto::FileSystemWatcher*>& watchers) {
	// The project layer hands us borrowed pointers; re-wrap them
	// non-owning for the shared_ptr-based interfaces below.
	std::vector<std::shared_ptr<lsproto::FileSystemWatcher>>
	    sharedWatchers;
	sharedWatchers.reserve(watchers.size());
	for (auto* watcher : watchers) {
		sharedWatchers.emplace_back(
		    watcher, [](lsproto::FileSystemWatcher*) {});
	}
	if (builtinWatcher != nullptr) {
		if (auto err = builtinWatcher->WatchFiles(std::string(id),
		                                          sharedWatchers);
		    err != nullptr) {
			return gostd::errorf("failed to register file watcher: %w",
			                     {gostd::fmtArg(err)});
		}
		this->watchers.Add(id);
		return nullptr;
	}
	auto params = std::make_shared<lsproto::RegistrationParams>();
	auto reg = std::make_shared<lsproto::Registration>();
	reg->Id = std::string(id);
	reg->RegisterOptions = std::make_shared<lsproto::RegisterOptions>();
	reg->RegisterOptions->WorkspaceDidChangeWatchedFiles =
		std::make_shared<lsproto::DidChangeWatchedFilesRegistrationOptions>();
	reg->RegisterOptions->WorkspaceDidChangeWatchedFiles->Watchers =
		sharedWatchers;
	params->Registrations = {reg};
	auto [_, err] =
		sendClientRequest(ctx, lsproto::ClientRegisterCapabilityInfo, params);
	if (err != nullptr) {
		return gostd::errorf("failed to register file watcher: %w",
		                     {gostd::fmtArg(err)});
	}

	this->watchers.Add(id);
	return nullptr;
}

// UnwatchFiles — server.go:292 (project.Client).
gostd::Error Server::UnwatchFiles(const gostd::Context& ctx,
                                  project::WatcherID id) {
	if (builtinWatcher != nullptr) {
		if (!watchers.Has(id)) {
			return gostd::errorf("no file watcher exists with ID %s",
			                     {gostd::fmtArg(std::string_view(id))});
		}
		if (auto err = builtinWatcher->UnwatchFiles(std::string(id));
		    err != nullptr) {
			return gostd::errorf("failed to unregister file watcher: %w",
			                     {gostd::fmtArg(err)});
		}
		watchers.Delete(id);
		return nullptr;
	}
	if (watchers.Has(id)) {
		auto params = std::make_shared<lsproto::UnregistrationParams>();
		auto unreg = std::make_shared<lsproto::Unregistration>();
		unreg->Id = std::string(id);
		unreg->Method =
			std::string(lsproto::MethodWorkspaceDidChangeWatchedFiles);
		params->Unregisterations = {unreg};
		auto [_, err] = sendClientRequest(
			ctx, lsproto::ClientUnregisterCapabilityInfo, params);
		if (err != nullptr) {
			return gostd::errorf("failed to unregister file watcher: %w",
			                     {gostd::fmtArg(err)});
		}
		watchers.Delete(id);
		return nullptr;
	}
	return gostd::errorf("no file watcher exists with ID %s",
	                     {gostd::fmtArg(std::string_view(id))});
}

// supportsContentMapperRegistration — server.go:362.
bool Server::supportsContentMapperRegistration(const std::string& id) const {
	const auto& td = clientCapabilities.TextDocument;
	if (id == contentMapperDidOpenRegistrationID ||
	    id == contentMapperDidChangeRegistrationID ||
	    id == contentMapperDidCloseRegistrationID) {
		return td.Synchronization.DynamicRegistration;
	}
	if (id == contentMapperDiagnosticRegistrationID) {
		return td.Diagnostic.DynamicRegistration;
	}
	if (id == contentMapperHoverRegistrationID) {
		return td.Hover.DynamicRegistration;
	}
	if (id == contentMapperSignatureHelpRegistrationID) {
		return td.SignatureHelp.DynamicRegistration;
	}
	if (id == contentMapperDefinitionRegistrationID) {
		return td.Definition.DynamicRegistration;
	}
	if (id == contentMapperTypeDefinitionRegistrationID) {
		return td.TypeDefinition.DynamicRegistration;
	}
	if (id == contentMapperImplementationRegistrationID) {
		return td.Implementation.DynamicRegistration;
	}
	if (id == contentMapperReferencesRegistrationID) {
		return td.References.DynamicRegistration;
	}
	if (id == contentMapperDocumentHighlightRegistrationID) {
		return td.DocumentHighlight.DynamicRegistration;
	}
	if (id == contentMapperCompletionRegistrationID) {
		return td.Completion.DynamicRegistration;
	}
	if (id == contentMapperRenameRegistrationID) {
		return td.Rename.DynamicRegistration;
	}
	if (id == contentMapperSemanticTokensRegistrationID) {
		return td.SemanticTokens.DynamicRegistration;
	}
	if (id == contentMapperDocumentSymbolRegistrationID) {
		return td.DocumentSymbol.DynamicRegistration;
	}
	if (id == contentMapperFoldingRangeRegistrationID) {
		return td.FoldingRange.DynamicRegistration;
	}
	if (id == contentMapperSelectionRangeRegistrationID) {
		return td.SelectionRange.DynamicRegistration;
	}
	if (id == contentMapperInlayHintRegistrationID) {
		return td.InlayHint.DynamicRegistration;
	}
	if (id == contentMapperCodeLensRegistrationID) {
		return td.CodeLens.DynamicRegistration;
	}
	if (id == contentMapperCodeActionRegistrationID) {
		return td.CodeAction.DynamicRegistration;
	}
	if (id == contentMapperFormattingRegistrationID) {
		return td.Formatting.DynamicRegistration;
	}
	if (id == contentMapperRangeFormattingRegistrationID) {
		return td.RangeFormatting.DynamicRegistration;
	}
	if (id == contentMapperOnTypeFormattingRegistrationID) {
		return td.OnTypeFormatting.DynamicRegistration;
	}
	if (id == contentMapperLinkedEditingRegistrationID) {
		return td.LinkedEditingRange.DynamicRegistration;
	}
	if (id == contentMapperCallHierarchyRegistrationID) {
		return td.CallHierarchy.DynamicRegistration;
	}
	if (id == contentMapperWillRenameFilesRegistrationID) {
		return clientCapabilities.Workspace.FileOperations
		           .DynamicRegistration &&
		       clientCapabilities.Workspace.FileOperations.WillRename;
	}
	return false;
}

// RegisterContentMapperExtensions — server.go:422 (project.Client).
// Dynamically registers text document synchronization and pull diagnostics
// for the given otherwise unsupported file extensions so the editor forwards
// their open/change/close notifications to the server and requests
// diagnostics for them. It is called with the full desired set each time it
// changes; an empty slice removes any prior registration.
gostd::Error Server::RegisterContentMapperExtensions(
	const gostd::Context& ctx, const std::vector<std::string>& extensions) {
	if (!clientCapabilities.TextDocument.Synchronization
	         .DynamicRegistration) {
		return nullptr;
	}

	std::lock_guard<std::mutex> lock(contentMapperRegistrationMu);

	auto unregistrationFor = [](std::string_view id,
	                            lsproto::Method method) {
		auto u = std::make_shared<lsproto::Unregistration>();
		u->Id = std::string(id);
		u->Method = std::string(method);
		return u;
	};

	if (contentMapperExtensionsRegistered) {
		std::vector<std::shared_ptr<lsproto::Unregistration>> unregistrations{
			unregistrationFor(contentMapperDidOpenRegistrationID,
			                  lsproto::MethodTextDocumentDidOpen),
			unregistrationFor(contentMapperDidChangeRegistrationID,
			                  lsproto::MethodTextDocumentDidChange),
			unregistrationFor(contentMapperDidCloseRegistrationID,
			                  lsproto::MethodTextDocumentDidClose),
			unregistrationFor(contentMapperDiagnosticRegistrationID,
			                  lsproto::MethodTextDocumentDiagnostic),
			unregistrationFor(contentMapperHoverRegistrationID,
			                  lsproto::MethodTextDocumentHover),
			unregistrationFor(contentMapperSignatureHelpRegistrationID,
			                  lsproto::MethodTextDocumentSignatureHelp),
			unregistrationFor(contentMapperDefinitionRegistrationID,
			                  lsproto::MethodTextDocumentDefinition),
			unregistrationFor(contentMapperTypeDefinitionRegistrationID,
			                  lsproto::MethodTextDocumentTypeDefinition),
			unregistrationFor(contentMapperImplementationRegistrationID,
			                  lsproto::MethodTextDocumentImplementation),
			unregistrationFor(contentMapperReferencesRegistrationID,
			                  lsproto::MethodTextDocumentReferences),
			unregistrationFor(contentMapperDocumentHighlightRegistrationID,
			                  lsproto::MethodTextDocumentDocumentHighlight),
			unregistrationFor(contentMapperCompletionRegistrationID,
			                  lsproto::MethodTextDocumentCompletion),
			unregistrationFor(contentMapperRenameRegistrationID,
			                  lsproto::MethodTextDocumentRename),
			unregistrationFor(contentMapperSemanticTokensRegistrationID,
			                  lsproto::MethodTextDocumentSemanticTokens),
			unregistrationFor(contentMapperDocumentSymbolRegistrationID,
			                  lsproto::MethodTextDocumentDocumentSymbol),
			unregistrationFor(contentMapperFoldingRangeRegistrationID,
			                  lsproto::MethodTextDocumentFoldingRange),
			unregistrationFor(contentMapperSelectionRangeRegistrationID,
			                  lsproto::MethodTextDocumentSelectionRange),
			unregistrationFor(contentMapperInlayHintRegistrationID,
			                  lsproto::MethodTextDocumentInlayHint),
			unregistrationFor(contentMapperCodeLensRegistrationID,
			                  lsproto::MethodTextDocumentCodeLens),
			unregistrationFor(contentMapperCodeActionRegistrationID,
			                  lsproto::MethodTextDocumentCodeAction),
			unregistrationFor(contentMapperFormattingRegistrationID,
			                  lsproto::MethodTextDocumentFormatting),
			unregistrationFor(contentMapperRangeFormattingRegistrationID,
			                  lsproto::MethodTextDocumentRangeFormatting),
			unregistrationFor(contentMapperOnTypeFormattingRegistrationID,
			                  lsproto::MethodTextDocumentOnTypeFormatting),
			unregistrationFor(contentMapperLinkedEditingRegistrationID,
			                  lsproto::MethodTextDocumentLinkedEditingRange),
			unregistrationFor(contentMapperCallHierarchyRegistrationID,
			                  lsproto::MethodTextDocumentPrepareCallHierarchy),
			unregistrationFor(contentMapperWillRenameFilesRegistrationID,
			                  lsproto::MethodWorkspaceWillRenameFiles),
		};
		// slices.DeleteFunc(unregistrations, !supportsContentMapperRegistration)
		std::erase_if(unregistrations,
		              [this](
		                  const std::shared_ptr<lsproto::Unregistration>& u) {
			              return !supportsContentMapperRegistration(u->Id);
		              });
		auto params = std::make_shared<lsproto::UnregistrationParams>();
		params->Unregisterations = std::move(unregistrations);
		auto [_, err] = sendClientRequest(
			ctx, lsproto::ClientUnregisterCapabilityInfo, params);
		if (err != nullptr) {
			return gostd::errorf(
				"failed to unregister content mapper text document sync: %w",
				{gostd::fmtArg(err)});
		}
		contentMapperExtensionsRegistered = false;
	}

	if (extensions.empty()) {
		return nullptr;
	}

	std::vector<lsproto::TextDocumentFilterLanguageOrSchemeOrPattern> filters;
	filters.reserve(extensions.size());
	for (const auto& ext : extensions) {
		lsproto::TextDocumentFilterLanguageOrSchemeOrPattern f;
		f.Pattern = std::make_shared<lsproto::TextDocumentFilterPattern>();
		f.Pattern->Pattern.Pattern = detail::goNew("**/*" + ext);
		filters.push_back(std::move(f));
	}
	lsproto::DocumentSelectorOrNull selector;
	selector.DocumentSelector = std::make_shared<
		lsproto::Slice<
		    lsproto::TextDocumentFilterLanguageOrSchemeOrPattern>>(
		std::move(filters));
	std::vector<std::shared_ptr<lsproto::FileOperationFilter>>
		contentMapperFileRenameFilters;
	contentMapperFileRenameFilters.reserve(extensions.size());
	for (const auto& extension : extensions) {
		auto f = std::make_shared<lsproto::FileOperationFilter>();
		f->Scheme = std::string("file");
		f->Pattern = std::make_shared<lsproto::FileOperationPattern>();
		f->Pattern->Glob = "**/*" + extension;
		contentMapperFileRenameFilters.push_back(std::move(f));
	}

	auto regOpt =
	    [](std::string_view id,
	       std::shared_ptr<lsproto::RegisterOptions> options) {
		    auto r = std::make_shared<lsproto::Registration>();
		    r->Id = std::string(id);
		    r->RegisterOptions = std::move(options);
		    return r;
	    };
	auto makeOptions = [] { return std::make_shared<lsproto::RegisterOptions>(); };

	std::vector<std::shared_ptr<lsproto::Registration>> registrations;
	{
		auto options = makeOptions();
		options->TextDocumentDidOpen =
			std::make_shared<lsproto::TextDocumentRegistrationOptions>();
		options->TextDocumentDidOpen->DocumentSelector = selector;
		registrations.push_back(
			regOpt(contentMapperDidOpenRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentDidChange = std::make_shared<
			lsproto::TextDocumentChangeRegistrationOptions>();
		options->TextDocumentDidChange->DocumentSelector = selector;
		options->TextDocumentDidChange->SyncKind =
			lsproto::TextDocumentSyncKindIncremental;
		registrations.push_back(
			regOpt(contentMapperDidChangeRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentDidClose =
			std::make_shared<lsproto::TextDocumentRegistrationOptions>();
		options->TextDocumentDidClose->DocumentSelector = selector;
		registrations.push_back(
			regOpt(contentMapperDidCloseRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentDiagnostic =
			std::make_shared<lsproto::DiagnosticRegistrationOptions>();
		options->TextDocumentDiagnostic->DocumentSelector = selector;
		options->TextDocumentDiagnostic->Identifier =
			std::string("typescript");
		options->TextDocumentDiagnostic->InterFileDependencies = true;
		registrations.push_back(regOpt(contentMapperDiagnosticRegistrationID,
		                               std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentHover =
			std::make_shared<lsproto::HoverRegistrationOptions>();
		options->TextDocumentHover->DocumentSelector = selector;
		registrations.push_back(
			regOpt(contentMapperHoverRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentSignatureHelp =
			std::make_shared<lsproto::SignatureHelpRegistrationOptions>();
		options->TextDocumentSignatureHelp->DocumentSelector = selector;
		options->TextDocumentSignatureHelp->TriggerCharacters =
			std::make_shared<lsproto::Slice<std::string>>(
				ls::SignatureHelpTriggerCharacters);
		options->TextDocumentSignatureHelp->RetriggerCharacters =
			std::make_shared<lsproto::Slice<std::string>>(
				ls::SignatureHelpRetriggerCharacters);
		registrations.push_back(regOpt(
			contentMapperSignatureHelpRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentDefinition =
			std::make_shared<lsproto::DefinitionRegistrationOptions>();
		options->TextDocumentDefinition->DocumentSelector = selector;
		registrations.push_back(regOpt(contentMapperDefinitionRegistrationID,
		                               std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentTypeDefinition =
			std::make_shared<lsproto::TypeDefinitionRegistrationOptions>();
		options->TextDocumentTypeDefinition->DocumentSelector = selector;
		registrations.push_back(regOpt(
			contentMapperTypeDefinitionRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentImplementation =
			std::make_shared<lsproto::ImplementationRegistrationOptions>();
		options->TextDocumentImplementation->DocumentSelector = selector;
		registrations.push_back(regOpt(
			contentMapperImplementationRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentReferences =
			std::make_shared<lsproto::ReferenceRegistrationOptions>();
		options->TextDocumentReferences->DocumentSelector = selector;
		registrations.push_back(regOpt(contentMapperReferencesRegistrationID,
		                               std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentDocumentHighlight =
			std::make_shared<lsproto::DocumentHighlightRegistrationOptions>();
		options->TextDocumentDocumentHighlight->DocumentSelector = selector;
		registrations.push_back(regOpt(
			contentMapperDocumentHighlightRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentCompletion =
			std::make_shared<lsproto::CompletionRegistrationOptions>();
		options->TextDocumentCompletion->DocumentSelector = selector;
		options->TextDocumentCompletion->TriggerCharacters =
			std::make_shared<lsproto::Slice<std::string>>(
				ls::CompletionTriggerCharacters);
		options->TextDocumentCompletion->ResolveProvider = true;
		options->TextDocumentCompletion->CompletionItem =
			std::make_shared<lsproto::ServerCompletionItemOptions>();
		options->TextDocumentCompletion->CompletionItem->LabelDetailsSupport =
			true;
		registrations.push_back(regOpt(contentMapperCompletionRegistrationID,
		                               std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentRename =
			std::make_shared<lsproto::RenameRegistrationOptions>();
		options->TextDocumentRename->DocumentSelector = selector;
		options->TextDocumentRename->PrepareProvider = true;
		registrations.push_back(
			regOpt(contentMapperRenameRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentSemanticTokens =
			std::make_shared<lsproto::SemanticTokensRegistrationOptions>();
		options->TextDocumentSemanticTokens->DocumentSelector = selector;
		options->TextDocumentSemanticTokens->Legend =
			std::make_shared<lsproto::SemanticTokensLegend>(
				*ls::SemanticTokensLegend(
					clientCapabilities.TextDocument.SemanticTokens));
		options->TextDocumentSemanticTokens->Full =
			std::make_shared<lsproto::BooleanOrSemanticTokensFullDelta>();
		options->TextDocumentSemanticTokens->Full->Boolean =
			detail::goNew(true);
		options->TextDocumentSemanticTokens->Range =
			std::make_shared<lsproto::BooleanOrEmptyObject>();
		options->TextDocumentSemanticTokens->Range->Boolean =
			detail::goNew(true);
		registrations.push_back(regOpt(
			contentMapperSemanticTokensRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentDocumentSymbol =
			std::make_shared<lsproto::DocumentSymbolRegistrationOptions>();
		options->TextDocumentDocumentSymbol->DocumentSelector = selector;
		registrations.push_back(regOpt(
			contentMapperDocumentSymbolRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentFoldingRange =
			std::make_shared<lsproto::FoldingRangeRegistrationOptions>();
		options->TextDocumentFoldingRange->DocumentSelector = selector;
		registrations.push_back(regOpt(
			contentMapperFoldingRangeRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentSelectionRange =
			std::make_shared<lsproto::SelectionRangeRegistrationOptions>();
		options->TextDocumentSelectionRange->DocumentSelector = selector;
		registrations.push_back(regOpt(
			contentMapperSelectionRangeRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentInlayHint =
			std::make_shared<lsproto::InlayHintRegistrationOptions>();
		options->TextDocumentInlayHint->DocumentSelector = selector;
		registrations.push_back(
			regOpt(contentMapperInlayHintRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentCodeLens =
			std::make_shared<lsproto::CodeLensRegistrationOptions>();
		options->TextDocumentCodeLens->DocumentSelector = selector;
		options->TextDocumentCodeLens->ResolveProvider = true;
		registrations.push_back(
			regOpt(contentMapperCodeLensRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentCodeAction =
			std::make_shared<lsproto::CodeActionRegistrationOptions>();
		options->TextDocumentCodeAction->DocumentSelector = selector;
		options->TextDocumentCodeAction->CodeActionKinds = std::make_shared<
			lsproto::Slice<lsproto::CodeActionKind>>(
			supportedCodeActionKinds());
		registrations.push_back(regOpt(contentMapperCodeActionRegistrationID,
		                               std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentFormatting =
			std::make_shared<lsproto::DocumentFormattingRegistrationOptions>();
		options->TextDocumentFormatting->DocumentSelector = selector;
		registrations.push_back(regOpt(contentMapperFormattingRegistrationID,
		                               std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentRangeFormatting = std::make_shared<
			lsproto::DocumentRangeFormattingRegistrationOptions>();
		options->TextDocumentRangeFormatting->DocumentSelector = selector;
		registrations.push_back(regOpt(
			contentMapperRangeFormattingRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentOnTypeFormatting = std::make_shared<
			lsproto::DocumentOnTypeFormattingRegistrationOptions>();
		options->TextDocumentOnTypeFormatting->DocumentSelector = selector;
		options->TextDocumentOnTypeFormatting->FirstTriggerCharacter = "{";
		options->TextDocumentOnTypeFormatting->MoreTriggerCharacter =
			std::make_shared<lsproto::Slice<std::string>>(
				std::vector<std::string>{"}", ";", "\n"});
		registrations.push_back(regOpt(
			contentMapperOnTypeFormattingRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentLinkedEditingRange =
			std::make_shared<lsproto::LinkedEditingRangeRegistrationOptions>();
		options->TextDocumentLinkedEditingRange->DocumentSelector = selector;
		registrations.push_back(regOpt(
			contentMapperLinkedEditingRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->TextDocumentPrepareCallHierarchy =
			std::make_shared<lsproto::CallHierarchyRegistrationOptions>();
		options->TextDocumentPrepareCallHierarchy->DocumentSelector = selector;
		registrations.push_back(regOpt(
			contentMapperCallHierarchyRegistrationID, std::move(options)));
	}
	{
		auto options = makeOptions();
		options->WorkspaceWillRenameFiles =
			std::make_shared<lsproto::FileOperationRegistrationOptions>();
		options->WorkspaceWillRenameFiles->Filters =
			std::move(contentMapperFileRenameFilters);
		registrations.push_back(regOpt(
			contentMapperWillRenameFilesRegistrationID, std::move(options)));
	}

	// slices.DeleteFunc(registrations, !supportsContentMapperRegistration)
	std::erase_if(registrations,
	              [this](const std::shared_ptr<lsproto::Registration>& reg) {
		              return !supportsContentMapperRegistration(reg->Id);
	              });
	{
		auto params = std::make_shared<lsproto::RegistrationParams>();
		params->Registrations = std::move(registrations);
		auto [_, err] = sendClientRequest(
			ctx, lsproto::ClientRegisterCapabilityInfo, params);
		if (err != nullptr) {
			return gostd::errorf(
				"failed to register content mapper text document sync: %w",
				{gostd::fmtArg(err)});
		}
	}

	contentMapperExtensionsRegistered = true;
	return nullptr;
}

// RefreshDiagnostics — server.go:705 (project.Client).
gostd::Error Server::RefreshDiagnostics(const gostd::Context& ctx) {
	if (!clientCapabilities.Workspace.Diagnostics.RefreshSupport) {
		return nullptr;
	}
	if (auto err = gostd::ctxErr(ctx); err != nullptr) {
		return err;
	}
	// Fire-and-forget: the client always returns null, and waiting for the
	// response can cause the server to hang if the client is slow or
	// unresponsive. Any response from the client will be silently ignored by
	// the read loop.
	if (auto err = sendClientRequestFireAndForget(
	        lsproto::WorkspaceDiagnosticRefreshInfo, lsproto::NoParams{});
	    err != nullptr) {
		return gostd::errorf("failed to refresh diagnostics: %w",
		                     {gostd::fmtArg(err)});
	}
	return nullptr;
}

// PublishDiagnostics — server.go:725 (project.Client).
gostd::Error Server::PublishDiagnostics(
	const gostd::Context& ctx, lsproto::PublishDiagnosticsParams* params) {
	// Go params is *lsproto.PublishDiagnosticsParams — aliasing shared_ptr
	// (non-owning) preserves that the caller retains ownership.
	return sendNotification(
		lsproto::TextDocumentPublishDiagnosticsInfo,
		std::shared_ptr<lsproto::PublishDiagnosticsParams>(
			params, [](lsproto::PublishDiagnosticsParams*) {}));
}

// SendTelemetry — server.go:730 (project.Client).
gostd::Error Server::SendTelemetry(
	const gostd::Context& ctx, lsproto::TelemetryEvent telemetry) {
	if (!telemetryEnabled) {
		throw goPanic{"SendTelemetry called with telemetry disabled"};
	}
	return sendNotification(lsproto::TelemetryEventInfo, telemetry);
}

// IsActive — server.go:738 (project.Client).
bool Server::IsActive() {
	auto last = lastRequestTimeMs.load();
	return last == 0 ||
	       (detail::unixMilliNow() - last) <=
	           std::chrono::duration_cast<std::chrono::milliseconds>(
	               std::chrono::minutes(1))
	               .count();
}

// RefreshInlayHints — server.go:743.
gostd::Error Server::RefreshInlayHints(const gostd::Context& ctx) {
	if (!clientCapabilities.Workspace.InlayHint.RefreshSupport) {
		return nullptr;
	}
	if (auto err = sendClientRequestFireAndForget(
	        lsproto::WorkspaceInlayHintRefreshInfo, lsproto::NoParams{});
	    err != nullptr) {
		return gostd::errorf("failed to refresh inlay hints: %w",
		                     {gostd::fmtArg(err)});
	}
	return nullptr;
}

// RefreshCodeLens — server.go:754.
gostd::Error Server::RefreshCodeLens(const gostd::Context& ctx) {
	if (!clientCapabilities.Workspace.CodeLens.RefreshSupport) {
		return nullptr;
	}
	if (auto err = sendClientRequestFireAndForget(
	        lsproto::WorkspaceCodeLensRefreshInfo, lsproto::NoParams{});
	    err != nullptr) {
		return gostd::errorf("failed to refresh code lens: %w",
		                     {gostd::fmtArg(err)});
	}
	return nullptr;
}

// ProgressStart — server.go:766 (project.Client).
void Server::ProgressStart(const DiagnosticMessage* message,
                           const std::vector<std::string>& args) {
	if (projectProgress != nullptr) {
		projectProgress->start(
		    message, std::vector<gostd::fmtArg>(args.begin(), args.end()));
	}
}

// ProgressFinish — server.go:773 (project.Client).
void Server::ProgressFinish(const DiagnosticMessage* message,
                            const std::vector<std::string>& args) {
	if (projectProgress != nullptr) {
		projectProgress->finish(
		    message, std::vector<gostd::fmtArg>(args.begin(), args.end()));
	}
}

// GetLocale — server.go:780 (project.Client).
tsc::locale::Locale Server::GetLocale() {
	std::shared_lock lk(localeMu);
	return locale;
}

// SetLocale — server.go:787 (project.Client).
void Server::SetLocale(const std::string& localeString) {
	auto newLocale = initLocale;
	if (localeString != "auto") {
		auto [parsed, ok] = tsc::locale::parse(localeString);
		if (!ok) {
			return;
		}
		newLocale = parsed;
	}
	std::unique_lock lk(localeMu);
	locale = newLocale;
}

// RequestConfiguration — server.go:801.
std::pair<ls::lsutil::UserPreferences, gostd::Error>
Server::RequestConfiguration(gostd::Context ctx) {
	auto caps = lsproto::getClientCapabilities(ctx);
	if (!caps->Workspace.Configuration) {
		if (initializationOptions != nullptr &&
		    initializationOptions->UserPreferences != nullptr) {
			const auto& userPrefs = *initializationOptions->UserPreferences;
			// Go logs `%T\n%+v` — the Go type name and value; the port logs
			// the decoded variant's kind and its marshalled value.
			logger->Logf(
				"received formatting options from initialization: %s\n%s",
				{gostd::fmtArg(std::string_view(
					 userPrefs.kind == lsproto::LSPAny::K::Object
					     ? "map[string]any"
					     : "any")),
				 gostd::fmtArg(
					 json::marshal(userPrefs).first)});
			if (userPrefs.kind == lsproto::LSPAny::K::Object) {
				// lsutil.ParseUserPreferences(map[string]any{"js/ts": config})
				ls::lsutil::JsonObject obj;
				ls::lsutil::JsonObject jsTs;
				for (const auto& [k, v] : userPrefs.obj) {
					jsTs[k] = lspAnyToJsonAny(v);
				}
				obj["js/ts"] = ls::lsutil::JsonAny(std::move(jsTs));
				return {ls::lsutil::ParseUserPreferences(obj), nullptr};
			}
		}
		return {ls::lsutil::NewDefaultUserPreferences(), nullptr};
	}
	auto params = std::make_shared<lsproto::ConfigurationParams>();
	params->Items = {};
	for (const char* section : {"js/ts", "typescript", "javascript",
	                            "editor"}) {
		auto item = std::make_shared<lsproto::ConfigurationItem>();
		item->Section = std::string(section);
		params->Items->push_back(std::move(item));
	}
	auto [configs, err] =
		sendClientRequest(ctx, lsproto::WorkspaceConfigurationInfo, params);
	if (err != nullptr) {
		return {ls::lsutil::UserPreferences{},
		        gostd::errorf("configure request failed: %w",
		                      {gostd::fmtArg(err)})};
	}
	// configMap — map[string]any in Go; JsonObject here (ParseUserPreferences
	// consumes JsonAny).
	ls::lsutil::JsonObject configMap;
	static const char* keys[] = {"js/ts", "typescript", "javascript", "editor"};
	if (configs.has_value()) {
		for (size_t i = 0; i < configs->size() && i < 4; i++) {
			configMap[keys[i]] = lspAnyToJsonAny((*configs)[i]);
		}
	}
	logger->Logf(
		"received options from workspace/configuration request:\njs/ts: "
		"%s\n\ntypescript: %s\n\njavascript: %s\n\neditor: %s\n",
		{gostd::fmtArg(jsonAnyRepr(configMap.count("js/ts")
		                               ? configMap["js/ts"]
		                               : ls::lsutil::JsonAny{})),
		 gostd::fmtArg(jsonAnyRepr(configMap.count("typescript")
		                               ? configMap["typescript"]
		                               : ls::lsutil::JsonAny{})),
		 gostd::fmtArg(jsonAnyRepr(configMap.count("javascript")
		                               ? configMap["javascript"]
		                               : ls::lsutil::JsonAny{})),
		 gostd::fmtArg(jsonAnyRepr(configMap.count("editor")
		                               ? configMap["editor"]
		                               : ls::lsutil::JsonAny{}))});
	return {ls::lsutil::ParseUserPreferences(configMap), nullptr};
}

// ---------------------------------------------------------------------------
// server.go:859 — Run / readLoop / cancelRequest / read / dispatchLoop /
// writeLoop.
// ---------------------------------------------------------------------------

// Run — server.go:859.
gostd::Error Server::Run(gostd::Context ctx) {
	// errgroup.WithContext(ctx): derived ctx cancels on first error return.
	gostd::Context gctx;
	gostd::CancelFunc gcancel;
	std::tie(gctx, gcancel) = gostd::contextWithCancel(ctx);
	backgroundCtx = gctx;

	struct errgroup {
		std::mutex mu;
		std::condition_variable cv;
		gostd::Error firstErr;
		int pending = 0;
		std::function<void()> cancel;
		void go(std::function<gostd::Error()> f) {
			{
				std::lock_guard<std::mutex> lk(mu);
				pending++;
			}
			std::thread([this, f = std::move(f)] {
				auto err = f();
				{
					std::unique_lock<std::mutex> lk(mu);
					if (err != nullptr && firstErr == nullptr) {
						firstErr = err;
					}
					bool done = --pending == 0;
					lk.unlock();
					if (err != nullptr) {
						cancel();
					}
					if (done) {
						cv.notify_all();
					}
				}
			}).detach();
		}
		gostd::Error wait() {
			std::unique_lock<std::mutex> lk(mu);
			cv.wait(lk, [&] { return pending == 0; });
			return firstErr;
		}
	};
	errgroup g;
	g.cancel = gcancel;

	auto s = this;
	g.go([s, gctx] { return s->dispatchLoop(gctx); });
	g.go([s, gctx] { return s->writeLoop(gctx); });

	// Don't run readLoop in the group, as it blocks on stdin read and cannot
	// be cancelled.
	struct readLoopChan {
		std::mutex mu;
		std::condition_variable cv;
		gostd::Error err;
		bool has = false;
	};
	auto rl = std::make_shared<readLoopChan>();
	g.go([s, gctx, rl] {
		// select { <-ctx.Done() | err := <-readLoopErr }
		std::unique_lock<std::mutex> lk(rl->mu);
		auto disarm = gostd::contextAfterFunc(gctx, [rl] {
			std::lock_guard<std::mutex> g(rl->mu);
			rl->cv.notify_all();
		});
		rl->cv.wait(lk, [&] {
			return rl->has || gostd::ctxErr(gctx) != nullptr;
		});
		disarm();
		if (auto e = gostd::ctxErr(gctx); e != nullptr) {
			return e;
		}
		return rl->err;
	});
	std::thread([s, gctx, rl] {
		auto err = s->readLoop(gctx);
		{
			std::lock_guard<std::mutex> lk(rl->mu);
			rl->err = err;
			rl->has = true;
		}
		rl->cv.notify_all();
	}).detach();

	auto err = g.wait();
	gcancel(); // errgroup: Wait cancels the derived context.
	if (err != nullptr && !gostd::errorIs(err, gostd::io::errEOF) &&
	    gostd::ctxErr(gctx) != nullptr) {
		return err;
	}
	return nullptr;
}

// readLoop — server.go:883.
gostd::Error Server::readLoop(gostd::Context ctx) {
	for (;;) {
		if (auto err = gostd::ctxErr(ctx); err != nullptr) {
			return err;
		}
		auto [msg, err] = read();
		if (err != nullptr) {
			if (errorIsCode(err, lsproto::ErrorCodeInvalidRequest) ||
			    errorIsCode(err, lsproto::ErrorCodeInvalidParams)) {
				std::shared_ptr<jsonrpc::ID> id;
				if (errorIsCode(err, lsproto::ErrorCodeInvalidParams)) {
					if (msg != nullptr &&
					    msg->Kind == jsonrpc::MessageKind::Request) {
						id = msg->AsRequest()->ID;
					}
				}
				if (auto serr = sendError(id, err); serr != nullptr) {
					return serr;
				}
				continue;
			}
			return err;
		}

		if (initializeParams == nullptr &&
		    msg->Kind == jsonrpc::MessageKind::Request) {
			auto req = msg->AsRequest();
			if (req->Method == lsproto::MethodInitialize) {
				auto [params, perr] =
					req->UnmarshalParams<
						std::shared_ptr<lsproto::InitializeParams>>();
				if (perr != nullptr) {
					if (auto serr = sendError(req->ID, perr);
					    serr != nullptr) {
						return serr;
					}
					continue;
				}
				auto [resp, herr] = handleInitialize(ctx, params, req);
				if (herr != nullptr) {
					return herr;
				}
				if (auto serr = sendResult(req->ID, resp);
				    serr != nullptr) {
					return serr;
				}
			} else {
				if (auto serr = sendError(
				        req->ID, lsproto::errorCodeErr(
				                     lsproto::ErrorCodeServerNotInitialized));
				    serr != nullptr) {
					return serr;
				}
			}
			continue;
		}

		if (msg->Kind == jsonrpc::MessageKind::Response) {
			auto resp = msg->AsResponse();
			std::lock_guard<std::mutex> lk(pendingServerRequestsMu);
			if (auto it = pendingServerRequests.find(*resp->ID);
			    it != pendingServerRequests.end()) {
				it->second->send(resp);
				it->second->close();
				pendingServerRequests.erase(it);
			}
		} else {
			auto req = msg->AsRequest();
			if (req->Method == lsproto::MethodCancelRequest) {
				auto [params, perr] =
					req->UnmarshalParams<
						std::shared_ptr<lsproto::CancelParams>>();
				if (perr == nullptr && params != nullptr) {
					cancelRequest(params->Id);
				}
			} else {
				if (auto perr = requestQueue.Put(ctx, req);
				    perr != nullptr) {
					return perr;
				}
			}
		}
	}
}

// cancelRequest — server.go:954.
void Server::cancelRequest(lsproto::IntegerOrString rawID) {
	auto id = lsproto::NewID(rawID);
	std::lock_guard<std::mutex> lk(pendingClientRequestsMu);
	if (auto it = pendingClientRequests.find(*id);
	    it != pendingClientRequests.end()) {
		it->second.cancel();
		pendingClientRequests.erase(it);
	}
}

// read — server.go:964.
std::pair<std::shared_ptr<lsproto::Message>, gostd::Error> Server::read() {
	return r->Read();
}

// dispatchLoop — server.go:968.
gostd::Error Server::dispatchLoop(gostd::Context ctx) {
	gostd::Context dctx;
	std::function<void(const gostd::Error&)> lspExit;
	std::tie(dctx, lspExit) = gostd::contextWithCancelCause(ctx);
	// defer lspExit(nil)
	struct deferLspExit {
		std::function<void(const gostd::Error&)>* f;
		~deferLspExit() { (*f)(nullptr); }
	} exitGuard{&lspExit};

	for (;;) {
		auto [reqOpt, err] = requestQueue.Get(dctx);
		if (err != nullptr) {
			return err;
		}
		auto req = std::move(*reqOpt);

		lastRequestTimeMs.store(detail::unixMilliNow());
		auto requestCtx = tsc::locale::withLocale(dctx, GetLocale());
		std::function<void()> cancel;
		if (req->ID != nullptr) {
			gostd::Context cctx;
			gostd::CancelFunc ccancel;
			std::tie(cctx, ccancel) = gostd::contextWithCancel(
				core::WithRequestID(requestCtx, req->ID->String()));
			requestCtx = cctx;
			cancel = ccancel;
			std::lock_guard<std::mutex> lk(pendingClientRequestsMu);
			pendingClientRequests[*req->ID] =
				pendingClientRequest{req, cancel};
		}

		auto handleError = [&](const gostd::Error& err) {
			if (gostd::errorIs(err, gostd::errCanceled)) {
				if (auto serr = sendError(
				        req->ID,
				        lsproto::errorCodeErr(
				            lsproto::ErrorCodeRequestCancelled));
				    serr != nullptr) {
					lspExit(serr);
				}
			} else if (gostd::errorIs(err, gostd::io::errEOF)) {
				lspExit(nullptr);
			} else {
				if (auto serr = sendError(req->ID, err); serr != nullptr) {
					lspExit(serr);
				}
			}
		};

		auto removeRequest = [&] {
			if (req->ID != nullptr) {
				struct deferCancel {
					std::function<void()>* f;
					~deferCancel() { (*f)(); }
				} guard{&cancel};
				std::lock_guard<std::mutex> lk(pendingClientRequestsMu);
				pendingClientRequests.erase(*req->ID);
			}
		};

		auto [doAsyncWork, herr] =
			handleRequestOrNotification(requestCtx, req);
		if (herr != nullptr) {
			handleError(herr);
			removeRequest();
		} else if (doAsyncWork != nullptr) {
			// Captures req/cancel/lspExit by value: the detached goroutine
			// outlives this iteration's stack frame.
			auto req2 = req;
			auto cancel2 = cancel;
			auto lspExit2 = lspExit;
			std::thread([this, doAsyncWork = std::move(doAsyncWork), req2,
			             cancel2, lspExit2]() mutable {
				auto handleError = [&](const gostd::Error& err) {
					if (gostd::errorIs(err, gostd::errCanceled)) {
						if (auto serr = sendError(
						        req2->ID,
						        lsproto::errorCodeErr(
						            lsproto::ErrorCodeRequestCancelled));
						    serr != nullptr) {
							lspExit2(serr);
						}
					} else if (gostd::errorIs(err, gostd::io::errEOF)) {
						lspExit2(nullptr);
					} else {
						if (auto serr = sendError(req2->ID, err);
						    serr != nullptr) {
							lspExit2(serr);
						}
					}
				};
				auto removeRequest = [&] {
					if (req2->ID != nullptr) {
						struct deferCancel {
							std::function<void()>* f;
							~deferCancel() { (*f)(); }
						} guard{&cancel2};
						std::lock_guard<std::mutex> lk(
							pendingClientRequestsMu);
						pendingClientRequests.erase(*req2->ID);
					}
				};
				if (auto lsError = doAsyncWork(); lsError != nullptr) {
					handleError(lsError);
				}
				removeRequest();
			}).detach();
		} else {
			removeRequest();
		}
	}
}

// writeLoop — server.go:1029.
gostd::Error Server::writeLoop(gostd::Context ctx) {
	for (;;) {
		auto [msgOpt, err] = outgoingQueue.Get(ctx);
		if (err != nullptr) {
			return err;
		}
		auto& msg = *msgOpt;
		if (auto werr = w->Write(msg); werr != nullptr) {
			if (auto* marshalErr =
			        gostd::errorAs<messageMarshalError*>(werr);
			    marshalErr != nullptr &&
			    msg->Kind == jsonrpc::MessageKind::Response) {
				if (auto resp = msg->AsResponse();
				    resp->ID != nullptr && resp->Error == nullptr) {
					logger->Errorf(
						"failed to marshal response for request %s: %v",
						{gostd::fmtArg(resp->ID->String()),
						 gostd::fmtArg(marshalErr->Error())});
					if (auto sendErr =
					        sendError(resp->ID, werr); // marshalErr
					    sendErr != nullptr) {
						return sendErr;
					}
					continue;
				}
			}
			return gostd::errorf("failed to write message: %w",
			                     {gostd::fmtArg(werr)});
		}
	}
}

// ---------------------------------------------------------------------------
// server.go:1096-1141 — sendResult / sendError / sendResponse / send.
// ---------------------------------------------------------------------------

// sendError — server.go:1108.
gostd::Error Server::sendError(
	const std::shared_ptr<jsonrpc::ID>& id, gostd::Error err) {
	// Do not send error response for notifications, except for parse errors
	// which may occur before determining if the message is a request or
	// notification.
	if (id == nullptr && !errorIsCode(err, lsproto::ErrorCodeInvalidRequest)) {
		logger->Errorf("error handling notification: %s",
		               {gostd::fmtArg(err)});
		return nullptr;
	}
	auto code = lsproto::ErrorCodeInternalError;
	if (auto errCode = errorCodeAs(err); errCode.has_value()) {
		code = *errCode;
	}
	// TODO(jakebailey): error data
	auto resp = std::make_shared<lsproto::ResponseMessage>();
	resp->ID = id;
	resp->Error = std::make_shared<jsonrpc::ResponseError>();
	resp->Error->Code = static_cast<int32_t>(code);
	resp->Error->Message = err != nullptr ? err->Error() : "<nil>";
	return sendResponse(resp);
}

// sendResponse — server.go:1133.
gostd::Error Server::sendResponse(
	const std::shared_ptr<lsproto::ResponseMessage>& resp) {
	return send(resp->toMessage());
}

// send — server.go:1138. Writes a message to the outgoing queue, respecting
// context cancellation.
gostd::Error Server::send(
	const std::shared_ptr<lsproto::Message>& msg) {
	return outgoingQueue.Put(backgroundCtx, msg);
}

// ---------------------------------------------------------------------------
// server.go:1144 — handleRequestOrNotification.
// ---------------------------------------------------------------------------

// contentMapperFallbackResponse — server.go:1198. Returns an empty response
// for requests made for unknown file types not handled by any content
// mapper. This typically serves a short window in time between when the
// server has unregistered content mapper extensions and when the client has
// stopped sending requests for those file types.
std::pair<lsproto::AnyValue, bool>
contentMapperFallbackResponse(const lsproto::Method& method,
                              const gostd::Error& err) {
	if (!gostd::errorIs(err, project::ErrNoProjectForUnknownScriptKind)) {
		return {lsproto::AnyValue{}, false};
	}
	if (method == lsproto::MethodTextDocumentDiagnostic) {
		lsproto::DocumentDiagnosticResponse resp;
		resp.FullDocumentDiagnosticReport = std::make_shared<
			lsproto::RelatedFullDocumentDiagnosticReport>();
		resp.FullDocumentDiagnosticReport->Items =
			std::vector<std::shared_ptr<lsproto::Diagnostic>>{};
		return {lsproto::AnyValue::of(std::move(resp)), true};
	}
	if (method == lsproto::MethodTextDocumentHover ||
	    method == lsproto::MethodTextDocumentSignatureHelp ||
	    method == lsproto::MethodTextDocumentDefinition ||
	    method == lsproto::MethodTextDocumentTypeDefinition ||
	    method == lsproto::MethodTextDocumentImplementation ||
	    method == lsproto::MethodTextDocumentReferences ||
	    method == lsproto::MethodTextDocumentDocumentHighlight ||
	    method == lsproto::MethodTextDocumentCompletion ||
	    method == lsproto::MethodTextDocumentRename) {
		return {lsproto::AnyValue::of(lsproto::Null{}), true};
	}
	return {lsproto::AnyValue{}, false};
}

// handleRequestOrNotification — server.go:1144. Looks up the handler for the
// given request or notification, executes its synchronous work and returns
// any asynchronous work as a function to be executed by the caller.
std::pair<std::function<gostd::Error()>, gostd::Error>
Server::handleRequestOrNotification(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::RequestMessage>& req) {
	// Go passes &s.clientCapabilities (live field); the aliasing shared_ptr
	// keeps that aliasing semantics.
	ctx = lsproto::withClientCapabilities(
		ctx, std::shared_ptr<lsproto::ResolvedClientCapabilities>(
		         std::shared_ptr<void>(shared_from_this()),
		         &clientCapabilities));

	if (auto it = handlers().find(req->Method); it != handlers().end()) {
		auto& handler = it->second;
		auto start = gostd::now();
		auto [doAsyncWork, err] = handler(this, ctx, req);
		std::string idStr;
		if (req->ID != nullptr) {
			idStr = " (" + req->ID->String() + ")";
		}
		if (err != nullptr) {
			if (auto [resp, ok] =
			        contentMapperFallbackResponse(req->Method, err);
			    ok) {
				if (!logger->IsTracing()) {
					logger->Info({"handled method '", req->Method, "'",
					              idStr, " in ",
					              formatGoDuration(gostd::since(start))});
				}
				return {nullptr, sendResult(req->ID, resp)};
			}
			if (gostd::errorAs<userFacingRequestFailedError*>(err) ==
			    nullptr) {
				logger->Error({"error handling method '", req->Method, "'",
				               idStr, ": ", err});
			} else if (!logger->IsTracing()) {
				logger->Info({"handled method '", req->Method, "'", idStr,
				              " in ",
				              formatGoDuration(gostd::since(start))});
			}
			return {nullptr, err};
		}
		if (doAsyncWork != nullptr) {
			auto s = this;
			return {[s, doAsyncWork = std::move(doAsyncWork), method =
			                                                       req->Method,
			         idStr, start]() -> gostd::Error {
				// note: ctx.Err() has to be checked in the async work to
				// allow async handlers to cleanup resources correctly
				auto asyncWorkErr = doAsyncWork();
				bool isUserFacing =
					gostd::errorAs<userFacingRequestFailedError*>(
					    asyncWorkErr) != nullptr;
				bool isRealError =
					asyncWorkErr != nullptr && !isUserFacing;
				if (isRealError) {
					s->logger->Info(
						{"error handling method '", method, "'", idStr,
						 " in ", formatGoDuration(gostd::since(start))});
				} else if (!s->logger->IsTracing()) {
					s->logger->Info({"handled method '", method, "'",
					                 idStr, " in ",
					                 formatGoDuration(gostd::since(start))});
				}
				return asyncWorkErr;
			},
			        nullptr};
		}
		if (!logger->IsTracing()) {
			logger->Info({"handled method '", req->Method, "'", idStr,
			              " in ", formatGoDuration(gostd::since(start))});
		}
		return {nullptr, nullptr};
	}
	logger->Warn({"unknown method '", req->Method, "'"});
	if (req->ID != nullptr) {
		return {nullptr,
		        sendError(req->ID, lsproto::errorCodeErr(
		                               lsproto::ErrorCodeInvalidRequest))};
	}
	return {nullptr, nullptr};
}

// ---------------------------------------------------------------------------
// server.go:1300-1428 — the register*Handler helpers.
// ---------------------------------------------------------------------------
namespace {

using handlerFn = std::function<
	std::pair<std::function<gostd::Error()>, gostd::Error>(
		Server*, gostd::Context,
		std::shared_ptr<lsproto::RequestMessage>)>;

// registerNotificationHandler — server.go:1300.
template <class Req>
void registerNotificationHandler(
	handlerMap& m, const lsproto::NotificationInfo<Req>& info,
	gostd::Error (Server::*fn)(gostd::Context, const Req&)) {
	m[info.Method] = [fn](Server* s, gostd::Context ctx,
	                      std::shared_ptr<lsproto::RequestMessage> req)
	    -> std::pair<std::function<gostd::Error()>, gostd::Error> {
		if (s->session == nullptr &&
		    req->Method != lsproto::MethodInitialized) {
			return {nullptr, lsproto::errorCodeErr(
			                     lsproto::ErrorCodeServerNotInitialized)};
		}
		auto [params, err] = req->UnmarshalParams<Req>();
		if (err != nullptr) {
			return {nullptr, err};
		}
		if (auto ferr = (s->*fn)(ctx, params); ferr != nullptr) {
			return {nullptr, ferr};
		}
		return {nullptr, gostd::ctxErr(ctx)};
	};
}

// registerRequestHandler — server.go:1317.
template <class Req, class Resp>
void registerRequestHandler(
	handlerMap& m, const lsproto::RequestInfo<Req, Resp>& info,
	std::pair<Resp, gostd::Error> (Server::*fn)(
		gostd::Context, const Req&,
		const std::shared_ptr<lsproto::RequestMessage>&)) {
	m[info.Method] = [fn](Server* s, gostd::Context ctx,
	                      std::shared_ptr<lsproto::RequestMessage> req)
	    -> std::pair<std::function<gostd::Error()>, gostd::Error> {
		if (s->session == nullptr &&
		    req->Method != lsproto::MethodInitialize) {
			return {nullptr, lsproto::errorCodeErr(
			                     lsproto::ErrorCodeServerNotInitialized)};
		}
		auto [params, err] = req->UnmarshalParams<Req>();
		if (err != nullptr) {
			return {nullptr, err};
		}
		auto [resp, rerr] = (s->*fn)(ctx, params, req);
		if (rerr != nullptr) {
			return {nullptr, rerr};
		}
		if (gostd::ctxErr(ctx) != nullptr) {
			return {nullptr, gostd::ctxErr(ctx)};
		}
		return {nullptr, s->sendResult(req->ID, resp)};
	};
}

// registerLanguageServiceDocumentRequestHandler — server.go:1341.
template <class Req, class Resp>
void registerLanguageServiceDocumentRequestHandler(
	handlerMap& m, const lsproto::RequestInfo<Req, Resp>& info,
	std::pair<Resp, gostd::Error> (Server::*fn)(
		gostd::Context, ls::LanguageService*, const Req&)) {
	m[info.Method] = [fn](Server* s, gostd::Context ctx,
	                      std::shared_ptr<lsproto::RequestMessage> req)
	    -> std::pair<std::function<gostd::Error()>, gostd::Error> {
		auto paramsErr = req->UnmarshalParams<Req>();
		if (paramsErr.second != nullptr) {
			return {nullptr, paramsErr.second};
		}
		Req params = std::move(paramsErr.first);
		auto lsRes = s->session->GetLanguageService(
			ctx, params->TextDocumentURI());
		if (lsRes.second != nullptr) {
			return {nullptr, lsRes.second};
		}
		ls::LanguageService* languageService = lsRes.first;
		return {[s, ctx, req, params, fn,
		         languageService]() -> gostd::Error {
			try {
				auto [resp, lsErr] =
					(s->*fn)(ctx, languageService, params);
				// After any language service request, check if new global
				// diagnostics were discovered during checking and push
				// updated tsconfig diagnostics if so.
				s->session->EnqueuePublishGlobalDiagnostics();
				if (lsErr != nullptr) {
					return lsErr;
				}
				if (gostd::ctxErr(ctx) != nullptr) {
					return gostd::ctxErr(ctx);
				}
				return s->sendResult(req->ID, resp);
			} catch (...) {
				s->recover_(req); // defer s.recover(req)
				return nullptr;
			}
		},
		        nullptr};
	};
}

// registerLanguageServiceWithAutoImportsRequestHandler — server.go:1368.
template <class Req, class Resp>
void registerLanguageServiceWithAutoImportsRequestHandler(
	handlerMap& m, const lsproto::RequestInfo<Req, Resp>& info,
	std::pair<Resp, gostd::Error> (Server::*fn)(
		gostd::Context, ls::LanguageService*, const Req&)) {
	m[info.Method] = [info, fn](Server* s, gostd::Context ctx,
	                            std::shared_ptr<lsproto::RequestMessage> req)
	    -> std::pair<std::function<gostd::Error()>, gostd::Error> {
		auto paramsErr = req->UnmarshalParams<Req>();
		if (paramsErr.second != nullptr) {
			return {nullptr, paramsErr.second};
		}
		Req params = std::move(paramsErr.first);
		return s->session->WithLanguageServiceAndSnapshot(
			ctx, params->TextDocumentURI(),
			[s, ctx, req, params, fn,
		     info](ls::LanguageService* languageService,
		           project::Snapshot* snapshot)
			    -> std::pair<std::function<gostd::Error()>, gostd::Error> {
				return {[s, ctx, req, params, fn, info,
				         languageService,
				         snapshot]() mutable -> gostd::Error {
					try {
						auto [resp, lsErr] =
							(s->*fn)(ctx, languageService, params);
						if (gostd::errorIs(lsErr,
						                   ls::ErrNeedsAutoImports)) {
							auto retry =
								s->session
								    ->GetLanguageServiceWithAutoImports(
									    ctx, snapshot,
									    params->TextDocumentURI());
							languageService = retry.first;
							lsErr = retry.second;
							if (lsErr != nullptr) {
								return lsErr;
							}
							if (gostd::ctxErr(ctx) != nullptr) {
								return gostd::ctxErr(ctx);
							}
							auto r2 = (s->*fn)(ctx, languageService, params);
							resp = std::move(r2.first);
							lsErr = std::move(r2.second);
							if (gostd::errorIs(lsErr,
							                   ls::ErrNeedsAutoImports)) {
								throw goPanic{
									info.Method +
									" returned ErrNeedsAutoImports even "
									"after enabling auto imports"};
							}
						}
						if (lsErr != nullptr) {
							return lsErr;
						}
						if (gostd::ctxErr(ctx) != nullptr) {
							return gostd::ctxErr(ctx);
						}
						return s->sendResult(req->ID, resp);
					} catch (...) {
						s->recover_(req);
						return nullptr;
					}
				},
				        nullptr};
			});
	};
}

// registerMultiProjectReferenceRequestHandler — server.go:1403.
template <class Req, class P, class Resp>
void registerMultiProjectReferenceRequestHandler(
	handlerMap& m, const lsproto::RequestInfo<Req, Resp>& info,
	std::pair<Resp, gostd::Error> (ls::LanguageService::*fn)(
		const gostd::Context&, P, ls::CrossProjectOrchestrator*)) {
	m[info.Method] = [fn](Server* s, gostd::Context ctx,
	                      std::shared_ptr<lsproto::RequestMessage> req)
	    -> std::pair<std::function<gostd::Error()>, gostd::Error> {
		auto paramsErr = req->UnmarshalParams<Req>();
		if (paramsErr.second != nullptr) {
			return {nullptr, paramsErr.second};
		}
		Req params = std::move(paramsErr.first);
		// !!! sheetal: multiple projects that contain the file through
		// symlinks
		auto lsOrchRes = s->getLanguageServiceAndCrossProjectOrchestrator(
			ctx, params->TextDocumentURI(), req);
		if (std::get<2>(lsOrchRes) != nullptr) {
			return {nullptr, std::get<2>(lsOrchRes)};
		}
		ls::LanguageService* defaultLs = std::get<0>(lsOrchRes);
		std::shared_ptr<ls::CrossProjectOrchestrator> orchestrator =
			std::get<1>(lsOrchRes);
		return {[s, ctx, req, params, fn, defaultLs,
		         orchestrator]() -> gostd::Error {
			try {
				auto [resp, lsErr] =
					(defaultLs->*fn)(ctx, params.get(),
					                 orchestrator.get());
				if (lsErr != nullptr) {
					return lsErr;
				}
				if (gostd::ctxErr(ctx) != nullptr) {
					return gostd::ctxErr(ctx);
				}
				return s->sendResult(req->ID, resp);
			} catch (...) {
				s->recover_(req);
				return nullptr;
			}
		},
		        nullptr};
	};
}

} // namespace

// ---------------------------------------------------------------------------
// server.go:1229 — handlers (sync.OnceValue → function-local static).
// ---------------------------------------------------------------------------
const handlerMap& Server::handlers() {
	static const handlerMap m = [] {
		handlerMap m;
		registerRequestHandler(m, lsproto::InitializeInfo,
		                       &Server::handleInitialize);
		registerNotificationHandler(m, lsproto::InitializedInfo,
		                            &Server::handleInitialized);
		registerRequestHandler(m, lsproto::ShutdownInfo,
		                       &Server::handleShutdown);
		registerNotificationHandler(m, lsproto::ExitInfo, &Server::handleExit);
		registerNotificationHandler(m, lsproto::WorkspaceDidChangeConfigurationInfo,
		                            &Server::handleDidChangeWorkspaceConfiguration);
		registerNotificationHandler(m, lsproto::TextDocumentDidOpenInfo,
		                            &Server::handleDidOpen);
		registerNotificationHandler(m, lsproto::TextDocumentDidChangeInfo,
		                            &Server::handleDidChange);
		registerNotificationHandler(m, lsproto::TextDocumentDidSaveInfo,
		                            &Server::handleDidSave);
		registerNotificationHandler(m, lsproto::TextDocumentDidCloseInfo,
		                            &Server::handleDidClose);
		registerNotificationHandler(m, lsproto::WorkspaceDidChangeWatchedFilesInfo,
		                            &Server::handleDidChangeWatchedFiles);
		registerNotificationHandler(m, lsproto::SetTraceInfo,
		                            &Server::handleSetTrace);
		registerNotificationHandler(m, lsproto::CustomSetLogVerbosityInfo,
		                            &Server::handleSetLogVerbosity);
		registerRequestHandler(m, lsproto::WorkspaceWillRenameFilesInfo,
		                       &Server::handleWillRenameFiles);

		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentDiagnosticInfo,
			&Server::handleDocumentDiagnostic);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentHoverInfo, &Server::handleHover);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentDefinitionInfo, &Server::handleDefinition);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::CustomTextDocumentSourceDefinitionInfo,
			&Server::handleSourceDefinition);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentTypeDefinitionInfo,
			&Server::handleTypeDefinition);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentSignatureHelpInfo,
			&Server::handleSignatureHelp);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentFormattingInfo,
			&Server::handleDocumentFormat);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentRangeFormattingInfo,
			&Server::handleDocumentRangeFormat);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentOnTypeFormattingInfo,
			&Server::handleDocumentOnTypeFormat);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentDocumentSymbolInfo,
			&Server::handleDocumentSymbol);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentDocumentHighlightInfo,
			&Server::handleDocumentHighlight);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::CustomTextDocumentMultiDocumentHighlightInfo,
			&Server::handleMultiDocumentHighlight);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentSelectionRangeInfo,
			&Server::handleSelectionRange);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentInlayHintInfo, &Server::handleInlayHint);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentCodeLensInfo, &Server::handleCodeLens);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentCodeActionInfo, &Server::handleCodeAction);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentPrepareCallHierarchyInfo,
			&Server::handlePrepareCallHierarchy);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentFoldingRangeInfo,
			&Server::handleFoldingRange);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentPrepareRenameInfo,
			&Server::handlePrepareRename);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentLinkedEditingRangeInfo,
			&Server::handleLinkedEditingRange);

		registerLanguageServiceWithAutoImportsRequestHandler(
			m, lsproto::TextDocumentCompletionInfo, &Server::handleCompletion);
		registerLanguageServiceWithAutoImportsRequestHandler(
			m, lsproto::TextDocumentCodeActionInfo, &Server::handleCodeAction);

		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentVSOnAutoInsertInfo,
			&Server::handleVSOnAutoInsert);

		registerMultiProjectReferenceRequestHandler(
			m, lsproto::TextDocumentReferencesInfo,
			&ls::LanguageService::ProvideReferences);
		registerMultiProjectReferenceRequestHandler(
			m, lsproto::TextDocumentVSReferencesInfo,
			&ls::LanguageService::ProvideVSReferences);
		registerRequestHandler(m, lsproto::TextDocumentRenameInfo,
		                       &Server::handleRename);
		registerMultiProjectReferenceRequestHandler(
			m, lsproto::TextDocumentImplementationInfo,
			&ls::LanguageService::ProvideImplementations);

		registerRequestHandler(m, lsproto::CallHierarchyIncomingCallsInfo,
		                       &Server::handleCallHierarchyIncomingCalls);
		registerRequestHandler(m, lsproto::CallHierarchyOutgoingCallsInfo,
		                       &Server::handleCallHierarchyOutgoingCalls);
		registerRequestHandler(m, lsproto::WorkspaceSymbolInfo,
		                       &Server::handleWorkspaceSymbol);
		registerRequestHandler(m, lsproto::CompletionItemResolveInfo,
		                       &Server::handleCompletionItemResolve);
		registerRequestHandler(m, lsproto::CodeLensResolveInfo,
		                       &Server::handleCodeLensResolve);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentSemanticTokensFullInfo,
			&Server::handleSemanticTokensFull);
		registerLanguageServiceDocumentRequestHandler(
			m, lsproto::TextDocumentSemanticTokensRangeInfo,
			&Server::handleSemanticTokensRange);

		// Developer/debugging commands
		registerRequestHandler(m, lsproto::CustomRunGCInfo,
		                       &Server::handleRunGC);
		registerRequestHandler(m, lsproto::CustomSaveHeapProfileInfo,
		                       &Server::handleSaveHeapProfile);
		registerRequestHandler(m, lsproto::CustomSaveAllocProfileInfo,
		                       &Server::handleSaveAllocProfile);
		registerRequestHandler(m, lsproto::CustomStartCPUProfileInfo,
		                       &Server::handleStartCPUProfile);
		registerRequestHandler(m, lsproto::CustomStopCPUProfileInfo,
		                       &Server::handleStopCPUProfile);

		registerRequestHandler(m, lsproto::CustomInitializeAPISessionInfo,
		                       &Server::handleInitializeAPISession);
		registerRequestHandler(m, lsproto::CustomProjectInfoInfo,
		                       &Server::handleProjectInfo);
		registerRequestHandler(m, lsproto::CustomSetContentMapperContributionsInfo,
		                       &Server::handleSetContentMapperContributions);
		return m;
	}();
	return m;
}

// ---------------------------------------------------------------------------
// server.go:1431-1475 — crossProjectOrchestrator +
// getLanguageServiceAndCrossProjectOrchestrator.
// ---------------------------------------------------------------------------
namespace {

// crossProjectOrchestrator — server.go:1431.
class crossProjectOrchestrator : public ls::CrossProjectOrchestrator {
public:
	Server* server;
	std::shared_ptr<lsproto::RequestMessage> req;
	project::Project* defaultProject;
	std::vector<ls::Project*> allProjects;

	crossProjectOrchestrator(
		Server* s, std::shared_ptr<lsproto::RequestMessage> req,
		project::Project* defaultProject,
		std::vector<ls::Project*> allProjects)
	    : server(s), req(std::move(req)), defaultProject(defaultProject),
	      allProjects(std::move(allProjects)) {}

	// GetDefaultProject — server.go:1440.
	ls::Project* GetDefaultProject() override { return defaultProject; }

	// GetAllProjectsForInitialRequest — server.go:1444.
	std::vector<ls::Project*> GetAllProjectsForInitialRequest() override {
		return allProjects;
	}

	// GetLanguageServiceForProjectWithFile — server.go:1448.
	ls::LanguageService* GetLanguageServiceForProjectWithFile(
		const gostd::Context& ctx, ls::Project* p,
		lsproto::DocumentUri uri) override {
		return server->session->GetLanguageServiceForProjectWithFile(
			ctx, static_cast<project::Project*>(p), uri);
	}

	// GetProjectsForFile — server.go:1452.
	std::pair<std::vector<ls::Project*>, gostd::Error> GetProjectsForFile(
		const gostd::Context& ctx, lsproto::DocumentUri uri) override {
		return server->session->GetProjectsForFile(ctx, uri);
	}

	// GetProjectsLoadingProjectTree — server.go:1456. Go returns iter.Seq —
	// the ls::CrossProjectOrchestrator decl already takes yield directly.
	void GetProjectsLoadingProjectTree(
		const gostd::Context& ctx,
		collections::Set<tspath::Path>* requestedProjectTrees,
		const std::function<bool(ls::Project*)>& yield) override {
		server->session->WithSnapshotLoadingProjectTree(
			ctx, requestedProjectTrees, [&](project::Snapshot* snapshot) {
				for (auto* p : snapshot->ProjectCollection
				                    ->LanguageServiceProjects()) {
					if (!yield(p)) {
						return;
					}
				}
			});
	}
};

} // namespace

// getLanguageServiceAndCrossProjectOrchestrator — server.go:1468.
std::tuple<ls::LanguageService*, std::shared_ptr<ls::CrossProjectOrchestrator>,
           gostd::Error>
Server::getLanguageServiceAndCrossProjectOrchestrator(
	gostd::Context ctx, lsproto::DocumentUri uri,
	const std::shared_ptr<lsproto::RequestMessage>& req) {
	auto result = session->GetLanguageServiceAndProjectsForFile(ctx, uri);
	std::shared_ptr<ls::CrossProjectOrchestrator> orchestrator;
	if (std::get<3>(result) == nullptr) {
		orchestrator = std::make_shared<crossProjectOrchestrator>(
			this, req, std::get<0>(result), std::move(std::get<2>(result)));
	}
	return {std::get<1>(result), std::move(orchestrator),
	        std::move(std::get<3>(result))};
}

// recover — server.go:1477. Called from catch blocks; the current exception
// plays Go's recovered panic value.
void Server::recover_(const std::shared_ptr<lsproto::RequestMessage>& req) {
	auto ep = std::current_exception();
	if (ep == nullptr) {
		return; // Go `recover()` outside a panic returns nil.
	}
	auto r = ipc::panicText(ep);
	auto stack = ipc::debugStack();
	logger->Errorf("panic handling request %s: %v\n%s",
	               {gostd::fmtArg(std::string_view(req->Method)),
	                gostd::fmtArg(std::string_view(r)),
	                gostd::fmtArg(std::string_view(stack))});
	if (req->ID != nullptr) {
		(void)sendError(
			req->ID,
			gostd::errorf("%w: panic handling request %s: %v",
			              {gostd::fmtArg(lsproto::errorCodeErr(
				                   lsproto::ErrorCodeInternalError)),
			               gostd::fmtArg(std::string_view(req->Method)),
			               gostd::fmtArg(std::string_view(r))}));
	} else {
		logger->Error({"unhandled panic in notification", req->Method, r});
	}

	if (telemetryEnabled) {
		lsproto::TelemetryEvent telemetry;
		telemetry.RequestFailureTelemetryEvent =
			std::make_shared<lsproto::RequestFailureTelemetryEvent>();
		telemetry.RequestFailureTelemetryEvent->Properties =
			std::make_shared<lsproto::RequestFailureTelemetryProperties>();
		telemetry.RequestFailureTelemetryEvent->Properties->ErrorCode =
			lsproto::String(lsproto::ErrorCodeInternalError);
		telemetry.RequestFailureTelemetryEvent->Properties->RequestMethod =
			[&] {
				auto m = std::string(req->Method);
				std::replace(m.begin(), m.end(), '/', '.');
				return m;
			}();
		telemetry.RequestFailureTelemetryEvent->Properties->Stack =
			sanitizeStackTrace(stack);
		(void)sendNotification(lsproto::TelemetryEventInfo, telemetry);
	}
}

} // namespace tsc::lsp
