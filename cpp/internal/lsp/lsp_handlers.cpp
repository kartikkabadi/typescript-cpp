// server.go (part 2) — port of tsc/internal/lsp/server.go: every handle*
// method, generateDiagnosticDiffString, parseContentMapperContributions,
// isValidContributedContentMapperExtension, valueOrZero, and the API-session
// plumbing.
#include "internal/lsp/lsp.h"

#include <algorithm>
#include <random>
#include <thread>

#include "internal/api/session.h"
#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/context.h"
#include "internal/core/version.h"
#include "internal/fswatch/fswatch.h"
#include "internal/ipc/ipc.h"
#include "internal/json/json.h"
#include "internal/ls/ls.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/lsp/lspwatcher/lspwatcher.h"
#include "internal/pprof/pprof.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

// rename.go:290 — defined in ls/rename.cpp; the header does not declare it.
namespace tsc::ls {
bool ClientSupportsWillRenameFiles(gostd::Context ctx);
}

namespace tsc::lsp {

// lspAnyToJsonAny — recursive LSPAny → lsutil.JsonAny conversion, used by
// RequestConfiguration to feed workspace/configuration results into
// lsutil.ParseUserPreferences.
ls::lsutil::JsonAny lspAnyToJsonAny(const lsproto::LSPAny& v) {
	using K = lsproto::LSPAny::K;
	switch (v.kind) {
		case K::Nil:
			return ls::lsutil::JsonAny{};
		case K::Bool:
			return ls::lsutil::JsonAny(v.b);
		case K::Int:
			return ls::lsutil::JsonAny(v.i);
		case K::Float:
			return ls::lsutil::JsonAny(v.f);
		case K::String:
			return ls::lsutil::JsonAny(std::string_view(v.s));
		case K::Array: {
			ls::lsutil::JsonAny out;
			out.kind = ls::lsutil::JsonAny::K::Array;
			out.arr.reserve(v.arr.size());
			for (const auto& e : v.arr) {
				out.arr.push_back(lspAnyToJsonAny(e));
			}
			return out;
		}
		case K::Object: {
			ls::lsutil::JsonAny out;
			out.kind = ls::lsutil::JsonAny::K::Object;
			for (const auto& [k, e] : v.obj) {
				out.obj[k] = lspAnyToJsonAny(e);
			}
			return out;
		}
	}
	TSC_UNREACHABLE("lspAnyToJsonAny: unknown LSPAny kind");
}

// jsonAnyRepr — `%+v` of the decoded config map value for logging. Go prints
// the Go representation; the port marshals the JsonAny to compact JSON.
std::string jsonAnyRepr(const ls::lsutil::JsonAny& v) {
	using K = ls::lsutil::JsonAny::K;
	switch (v.kind) {
		case K::Nil:
			return "null";
		case K::Bool:
			return v.b ? "true" : "false";
		case K::Int:
			return std::to_string(v.i);
		case K::Float:
			return gostd::sprintf("%v", {gostd::fmtArg(v.f)});
		case K::String:
			return v.s;
		case K::Array: {
			std::string out = "[";
			for (const auto& e : v.arr) {
				if (out.size() > 1) out += " ";
				out += jsonAnyRepr(e);
			}
			return out + "]";
		}
		case K::Object: {
			std::string out = "map[";
			bool first = true;
			for (const auto& [k, e] : v.obj) {
				if (!first) out += " ";
				first = false;
				out += k + ":" + jsonAnyRepr(e);
			}
			return out + "]";
		}
	}
	TSC_UNREACHABLE("jsonAnyRepr: unknown JsonAny kind");
}

// ---------------------------------------------------------------------------
// server.go:1501 — handleInitialize.
// ---------------------------------------------------------------------------
std::pair<lsproto::InitializeResponse, gostd::Error> Server::handleInitialize(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::InitializeParams>& params,
	const std::shared_ptr<lsproto::RequestMessage>& /*req*/) {
	if (initializeParams != nullptr) {
		return {nullptr, lsproto::errorCodeErr(lsproto::ErrorCodeInvalidRequest)};
	}

	initStarted.store(true);

	initializeParams = params;
	// The spec types initializationOptions as nullable; treat both null and an
	// absent value as empty options so the rest of the server can read fields
	// off s.initializationOptions without nil-checking the container.
	if (params->InitializationOptions != nullptr &&
	    params->InitializationOptions->InitializationOptions != nullptr) {
		initializationOptions =
			params->InitializationOptions->InitializationOptions;
	} else {
		initializationOptions =
			std::make_shared<lsproto::InitializationOptions>();
	}
	if (initializationOptions->LogVerbosity != nullptr) {
		auto v = *initializationOptions->LogVerbosity;
		if (isValidLogVerbosity(v)) {
			logger->SetVerbosity(v);
		}
	}
	if (initializationOptions->TrackFlakyDiagnostics != nullptr) {
		flakeLogging = *initializationOptions->TrackFlakyDiagnostics;
	}
	clientCapabilities = params->Capabilities->Resolve();
	if (clientCapabilities.Window.WorkDoneProgress) {
		projectProgress = newProjectLoadingProgressFromReporter(
			std::make_shared<serverProgressReporter>(shared_from_this()),
			progressDelay);
	}

	auto [capabilitiesJSON, merr] =
		json::marshalIndent(clientCapabilities, "", "\t");
	if (!merr.empty()) {
		return {nullptr, gostd::newError(merr)};
	}
	logger->Info({"Resolved client capabilities: " + capabilitiesJSON});

	positionEncoding = lsproto::PositionEncodingKindUTF16;
	if (std::find(
	        clientCapabilities.General.PositionEncodings->begin(),
	        clientCapabilities.General.PositionEncodings->end(),
	        lsproto::PositionEncodingKindUTF8) !=
	    clientCapabilities.General.PositionEncodings->end()) {
		positionEncoding = lsproto::PositionEncodingKindUTF8;
	}

	if (initializeParams->Locale.has_value()) {
		auto [l, ok] = tsc::locale::parse(*initializeParams->Locale);
		locale = l;
	}
	initLocale = locale;

	if (startWatchdog != nullptr && params->ProcessId.Integer != nullptr) {
		startWatchdog(static_cast<int>(*params->ProcessId.Integer));
	}

	auto response = std::make_shared<lsproto::InitializeResult>();
	response->ServerInfo = std::make_shared<lsproto::ServerInfo>();
	response->ServerInfo->Name = "typescript";
	response->ServerInfo->Version = std::string(tsc::version());
	response->Capabilities =
		std::make_shared<lsproto::ServerCapabilities>();
	auto& caps = *response->Capabilities;
	caps.PositionEncoding = std::make_shared<lsproto::PositionEncodingKind>(
		positionEncoding);
	caps.TextDocumentSync =
		std::make_shared<lsproto::TextDocumentSyncOptionsOrKind>();
	caps.TextDocumentSync->Options =
		std::make_shared<lsproto::TextDocumentSyncOptions>();
	caps.TextDocumentSync->Options->OpenClose = true;
	caps.TextDocumentSync->Options->Change =
		std::make_shared<lsproto::TextDocumentSyncKind>(
			lsproto::TextDocumentSyncKindIncremental);
	caps.TextDocumentSync->Options->Save =
		std::make_shared<lsproto::BooleanOrSaveOptions>();
	caps.TextDocumentSync->Options->Save->Boolean =
		detail::goNew(true);
	caps.HoverProvider =
		std::make_shared<lsproto::BooleanOrHoverOptions>();
	caps.HoverProvider->Boolean = detail::goNew(true);
	caps.DefinitionProvider =
		std::make_shared<lsproto::BooleanOrDefinitionOptions>();
	caps.DefinitionProvider->Boolean = detail::goNew(true);
	caps.TypeDefinitionProvider = std::make_shared<
		lsproto::
		    BooleanOrTypeDefinitionOptionsOrTypeDefinitionRegistrationOptions>();
	caps.TypeDefinitionProvider->Boolean = detail::goNew(true);
	caps.ReferencesProvider =
		std::make_shared<lsproto::BooleanOrReferenceOptions>();
	caps.ReferencesProvider->Boolean = detail::goNew(true);
	caps.ImplementationProvider = std::make_shared<
		lsproto::
		    BooleanOrImplementationOptionsOrImplementationRegistrationOptions>();
	caps.ImplementationProvider->Boolean = detail::goNew(true);
	caps.DiagnosticProvider =
		std::make_shared<lsproto::DiagnosticOptionsOrRegistrationOptions>();
	caps.DiagnosticProvider->Options =
		std::make_shared<lsproto::DiagnosticOptions>();
	caps.DiagnosticProvider->Options->Identifier =
		std::string("typescript");
	caps.DiagnosticProvider->Options->InterFileDependencies = true;
	caps.CompletionProvider =
		std::make_shared<lsproto::CompletionOptions>();
	caps.CompletionProvider->TriggerCharacters =
		std::make_shared<lsproto::Slice<std::string>>(
			ls::CompletionTriggerCharacters);
	caps.CompletionProvider->ResolveProvider = true;
	caps.CompletionProvider->CompletionItem =
		std::make_shared<lsproto::ServerCompletionItemOptions>();
	caps.CompletionProvider->CompletionItem->LabelDetailsSupport = true;
	caps.SignatureHelpProvider =
		std::make_shared<lsproto::SignatureHelpOptions>();
	caps.SignatureHelpProvider->TriggerCharacters =
		std::make_shared<lsproto::Slice<std::string>>(
			ls::SignatureHelpTriggerCharacters);
	caps.SignatureHelpProvider->RetriggerCharacters =
		std::make_shared<lsproto::Slice<std::string>>(
			ls::SignatureHelpRetriggerCharacters);
	caps.DocumentFormattingProvider =
		std::make_shared<lsproto::BooleanOrDocumentFormattingOptions>();
	caps.DocumentFormattingProvider->Boolean = detail::goNew(true);
	caps.DocumentRangeFormattingProvider = std::make_shared<
		lsproto::BooleanOrDocumentRangeFormattingOptions>();
	caps.DocumentRangeFormattingProvider->Boolean = detail::goNew(true);
	caps.DocumentOnTypeFormattingProvider =
		std::make_shared<lsproto::DocumentOnTypeFormattingOptions>();
	caps.DocumentOnTypeFormattingProvider->FirstTriggerCharacter = "{";
	caps.DocumentOnTypeFormattingProvider->MoreTriggerCharacter =
		std::make_shared<lsproto::Slice<std::string>>(
			std::vector<std::string>{"}", ";", "\n"});
	caps.WorkspaceSymbolProvider =
		std::make_shared<lsproto::BooleanOrWorkspaceSymbolOptions>();
	caps.WorkspaceSymbolProvider->Boolean = detail::goNew(true);
	caps.DocumentSymbolProvider =
		std::make_shared<lsproto::BooleanOrDocumentSymbolOptions>();
	caps.DocumentSymbolProvider->Boolean = detail::goNew(true);
	caps.FoldingRangeProvider = std::make_shared<
		lsproto::
		    BooleanOrFoldingRangeOptionsOrFoldingRangeRegistrationOptions>();
	caps.FoldingRangeProvider->Boolean = detail::goNew(true);
	caps.RenameProvider =
		std::make_shared<lsproto::BooleanOrRenameOptions>();
	caps.RenameProvider->RenameOptions =
		std::make_shared<lsproto::RenameOptions>();
	caps.RenameProvider->RenameOptions->PrepareProvider = true;
	caps.DocumentHighlightProvider =
		std::make_shared<lsproto::BooleanOrDocumentHighlightOptions>();
	caps.DocumentHighlightProvider->Boolean = detail::goNew(true);
	caps.SelectionRangeProvider = std::make_shared<
		lsproto::
		    BooleanOrSelectionRangeOptionsOrSelectionRangeRegistrationOptions>();
	caps.SelectionRangeProvider->Boolean = detail::goNew(true);
	caps.LinkedEditingRangeProvider = std::make_shared<
		lsproto::
		    BooleanOrLinkedEditingRangeOptionsOrLinkedEditingRangeRegistrationOptions>();
	caps.LinkedEditingRangeProvider->Boolean = detail::goNew(true);
	caps.InlayHintProvider = std::make_shared<
		lsproto::
		    BooleanOrInlayHintOptionsOrInlayHintRegistrationOptions>();
	caps.InlayHintProvider->Boolean = detail::goNew(true);
	caps.CodeLensProvider = std::make_shared<lsproto::CodeLensOptions>();
	caps.CodeLensProvider->ResolveProvider = true;
	caps.CodeActionProvider =
		std::make_shared<lsproto::BooleanOrCodeActionOptions>();
	caps.CodeActionProvider->CodeActionOptions =
		std::make_shared<lsproto::CodeActionOptions>();
	caps.CodeActionProvider->CodeActionOptions->CodeActionKinds =
		std::make_shared<lsproto::Slice<lsproto::CodeActionKind>>(
			supportedCodeActionKinds());
	caps.CallHierarchyProvider = std::make_shared<
		lsproto::
		    BooleanOrCallHierarchyOptionsOrCallHierarchyRegistrationOptions>();
	caps.CallHierarchyProvider->Boolean = detail::goNew(true);
	caps.Experimental =
		std::make_shared<lsproto::ExperimentalServerCapabilities>();
	caps.Experimental->CustomSourceDefinitionProvider = true;
	caps.Experimental->CustomMultiDocumentHighlightProvider = true;
	caps.VSReferencesProvider = true;
	caps.VSOnAutoInsertProvider =
		std::make_shared<lsproto::VSOnAutoInsertOptions>();
	caps.VSOnAutoInsertProvider->VSTriggerCharacters =
		std::vector<std::string>{">"};
	caps.Workspace = std::make_shared<lsproto::WorkspaceOptions>();
	caps.Workspace->FileOperations =
		std::make_shared<lsproto::FileOperationOptions>();
	caps.Workspace->FileOperations->WillRename =
		std::make_shared<lsproto::FileOperationRegistrationOptions>();
	caps.Workspace->FileOperations->WillRename->Filters =
		fileRenameFilters();
	caps.SemanticTokensProvider = std::make_shared<
		lsproto::SemanticTokensOptionsOrRegistrationOptions>();
	caps.SemanticTokensProvider->Options =
		std::make_shared<lsproto::SemanticTokensOptions>();
	caps.SemanticTokensProvider->Options->Legend =
		std::make_shared<lsproto::SemanticTokensLegend>(
			*ls::SemanticTokensLegend(
				clientCapabilities.TextDocument.SemanticTokens));
	caps.SemanticTokensProvider->Options->Full =
		std::make_shared<lsproto::BooleanOrSemanticTokensFullDelta>();
	caps.SemanticTokensProvider->Options->Full->Boolean =
		detail::goNew(true);
	caps.SemanticTokensProvider->Options->Range =
		std::make_shared<lsproto::BooleanOrEmptyObject>();
	caps.SemanticTokensProvider->Options->Range->Boolean =
		detail::goNew(true);

	return {response, nullptr};
}

// ---------------------------------------------------------------------------
// server.go:1670 — handleInitialized.
// ---------------------------------------------------------------------------
gostd::Error Server::handleInitialized(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::InitializedParams>& /*params*/) {
	bool disablePushDiagnostics = false;
	bool enableTelemetry = false;
	if (initializationOptions->DisablePushDiagnostics.has_value()) {
		disablePushDiagnostics =
			*initializationOptions->DisablePushDiagnostics;
	}
	if (initializationOptions->EnableTelemetry.has_value()) {
		enableTelemetry = *initializationOptions->EnableTelemetry;
	}
	bool runExternalCode = false;
	if (initializationOptions->RunExternalCode.has_value()) {
		runExternalCode = *initializationOptions->RunExternalCode;
	}
	bool hasDynamicWatchRegistration = clientCapabilities.Workspace
	                                       .DidChangeWatchedFiles
	                                       .DynamicRegistration;
	if (hasDynamicWatchRegistration) {
		logger->Logf(
			"file watching: using LSP client-side watching (client "
			"supports dynamic registration)",
			{});
		watchEnabled = true;
	} else if (fswatch::Default()->hasFastRecursiveBackend()) {
		// The client cannot watch files itself, but the builtin watcher has
		// a backend with efficient recursive watching (Windows or FSEvents),
		// so fall back to watching files in-process.
		logger->Logf(
			"file watching: using builtin in-process watcher (client lacks "
			"dynamic watch registration)",
			{});
		watchEnabled = true;
		auto* server = this;
		builtinWatcher = lspwatcher::New(
			fs,
			[server](
			    std::vector<std::shared_ptr<lsproto::FileEvent>> changes) {
				if (server->session != nullptr) {
					std::vector<lsproto::FileEvent*> raw;
					raw.reserve(changes.size());
					for (auto& ev : changes) {
						raw.push_back(ev.get());
					}
					server->session->DidChangeWatchedFiles(
						server->backgroundCtx, raw);
				}
			},
			logger);
	} else {
		// The client cannot watch files and the builtin watcher backend
		// lacks efficient recursive watching, so file watching is disabled.
		logger->Logf(
			"file watching: disabled (client lacks dynamic watch "
			"registration and builtin watcher backend is not "
			"fast-recursive)",
			{});
	}

	auto cwd = this->cwd;
	if (clientCapabilities.Workspace.WorkspaceFolders &&
	    initializeParams->WorkspaceFolders != nullptr &&
	    initializeParams->WorkspaceFolders->WorkspaceFolders != nullptr &&
	    (*initializeParams->WorkspaceFolders->WorkspaceFolders)->size() ==
	        1) {
		cwd = lsproto::documentUriFileName(
		    (*(*initializeParams->WorkspaceFolders->WorkspaceFolders))[0]->Uri);
	} else if (initializeParams->RootUri.DocumentUri != nullptr) {
		cwd = lsproto::documentUriFileName(*initializeParams->RootUri.DocumentUri);
	} else if (initializeParams->RootPath != nullptr &&
	           initializeParams->RootPath->String != nullptr) {
		cwd = *initializeParams->RootPath->String;
	}
	if (!tspath::pathIsAbsolute(cwd)) {
		cwd = this->cwd;
	}

	telemetryEnabled = enableTelemetry;

	project::SessionInit init;
	init.BackgroundCtx = lsproto::withClientCapabilities(
		backgroundCtx,
		std::shared_ptr<lsproto::ResolvedClientCapabilities>(
			std::shared_ptr<void>(shared_from_this()), &clientCapabilities));
	sessionInitOptions = std::make_shared<project::SessionOptions>();
	auto* sessionOpts = sessionInitOptions.get();
	sessionOpts->CurrentDirectory = cwd;
	sessionOpts->DefaultLibraryPath = defaultLibraryPath;
	sessionOpts->TypingsLocation = typingsLocation;
	sessionOpts->PositionEncoding = positionEncoding;
	sessionOpts->WatchEnabled = watchEnabled;
	sessionOpts->LoggingEnabled = true;
	sessionOpts->TelemetryEnabled = enableTelemetry;
	sessionOpts->DebounceDelay = std::chrono::milliseconds(500);
	sessionOpts->PushDiagnosticsEnabled = !disablePushDiagnostics;
	sessionOpts->RunExternalCode = runExternalCode;
	init.Options = sessionOpts;
	init.FS = fs;
	init.Logger = logger.get();
	init.Client = this;
	init.NpmExecutor = this;
	// Pin the Server for the session's lifetime: session background
	// workers call client->* / npmExecutor->* on detached threads and
	// must not see a torn-down Server (Go's GC holds it alive).
	init.ClientRef = shared_from_this();
	init.NpmExecutorRef = shared_from_this();
	sessionInitSpawner = contentMapperSpawner();
	init.Spawner = sessionInitSpawner.get();
	sessionInitContentMapperLogger =
		std::make_shared<contentmapper::Logger>(contentMapperLogger());
	init.ContentMapperLogger = sessionInitContentMapperLogger.get();
	init.ParseCache = parseCache.get();
	init.KeepAlive = {fs, logger, sessionInitSpawner,
	                  sessionInitContentMapperLogger, parseCache};
	session = project::NewSession(&init);

	auto [userPreferences, uerr] = RequestConfiguration(ctx);
	if (uerr != nullptr) {
		return uerr;
	}
	session->InitializeWithUserConfig(userPreferences);

	auto regParams = std::make_shared<lsproto::RegistrationParams>();
	auto reg = std::make_shared<lsproto::Registration>();
	reg->Id = "typescript-config-watch-id";
	reg->RegisterOptions =
		std::make_shared<lsproto::RegisterOptions>();
	reg->RegisterOptions->WorkspaceDidChangeConfiguration =
		std::make_shared<lsproto::DidChangeConfigurationRegistrationOptions>();
	reg->RegisterOptions->WorkspaceDidChangeConfiguration->Section =
		std::make_shared<lsproto::StringOrStrings>();
	reg->RegisterOptions->WorkspaceDidChangeConfiguration->Section
	    ->Strings = std::make_shared<lsproto::Slice<std::string>>(
		std::vector<std::string>{"js/ts", "typescript", "javascript",
		                         "editor"});
	regParams->Registrations = {reg};
	auto [_, cerr] =
		sendClientRequest(ctx, lsproto::ClientRegisterCapabilityInfo,
		                  regParams);
	if (cerr != nullptr) {
		return gostd::errorf(
			"failed to register configuration change watcher: %w",
			{gostd::fmtArg(cerr)});
	}

	// !!! temporary.
	// Remove when we have `handleDidChangeConfiguration`/implicit project
	// config support derived from 'js/ts.implicitProjectConfig.*'.
	if (compilerOptionsForInferredProjects != nullptr) {
		session->DidChangeCompilerOptionsForInferredProjects(
			ctx, compilerOptionsForInferredProjects);
	}

	session->StartPerformanceTelemetry();

	initComplete->close();
	return nullptr;
}

// ---------------------------------------------------------------------------
// server.go:1751 — handleShutdown / handleExit / lifecycle notifications.
// ---------------------------------------------------------------------------
std::pair<lsproto::ShutdownResponse, gostd::Error> Server::handleShutdown(
	gostd::Context /*ctx*/, const lsproto::NoParams& /*params*/,
	const std::shared_ptr<lsproto::RequestMessage>& /*req*/) {
	if (builtinWatcher != nullptr) {
		builtinWatcher->Close();
	}
	closeAPISessions();
	session->Close();
	return {lsproto::ShutdownResponse{}, nullptr};
}

gostd::Error Server::handleExit(gostd::Context /*ctx*/,
                                const lsproto::NoParams& /*params*/) {
	return gostd::io::errEOF;
}

// handleDidChangeWorkspaceConfiguration — server.go:1763.
gostd::Error Server::handleDidChangeWorkspaceConfiguration(
	gostd::Context /*ctx*/,
	const std::shared_ptr<lsproto::DidChangeConfigurationParams>& params) {
	if (params->Settings.kind == lsproto::LSPAny::K::Nil) {
		return nullptr;
	}
	if (params->Settings.kind == lsproto::LSPAny::K::Object) {
		ls::lsutil::JsonObject settings;
		for (const auto& [k, v] : params->Settings.obj) {
			settings[k] = lspAnyToJsonAny(v);
		}
		session->Configure(ls::lsutil::ParseUserPreferences(settings));
	}
	return nullptr;
}

// handleDidOpen — server.go:1771.
gostd::Error Server::handleDidOpen(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::DidOpenTextDocumentParams>& params) {
	session->DidOpenFile(ctx, params->TextDocument->Uri,
	                     params->TextDocument->Version,
	                     params->TextDocument->Text,
	                     params->TextDocument->LanguageId);
	return nullptr;
}

// handleDidChange — server.go:1776.
gostd::Error Server::handleDidChange(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::DidChangeTextDocumentParams>& params) {
	session->DidChangeFile(
		ctx, params->TextDocument.Uri, params->TextDocument.Version,
		params->ContentChanges.value_or(
			std::vector<lsproto::
			                TextDocumentContentChangePartialOrWholeDocument>{}));
	return nullptr;
}

// handleDidSave — server.go:1786.
gostd::Error Server::handleDidSave(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::DidSaveTextDocumentParams>& params) {
	session->DidSaveFile(ctx, params->TextDocument.Uri);
	return nullptr;
}

// handleDidClose — server.go:1790.
gostd::Error Server::handleDidClose(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::DidCloseTextDocumentParams>& params) {
	session->DidCloseFile(ctx, params->TextDocument.Uri);
	return nullptr;
}

// handleDidChangeWatchedFiles — server.go:1794.
gostd::Error Server::handleDidChangeWatchedFiles(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::DidChangeWatchedFilesParams>& params) {
	std::vector<lsproto::FileEvent*> changes;
	if (params->Changes) {
		for (auto& ev : *params->Changes) {
			changes.push_back(ev.get());
		}
	}
	session->DidChangeWatchedFiles(ctx, changes);
	return nullptr;
}

// handleSetTrace — server.go:1798.
gostd::Error Server::handleSetTrace(
	gostd::Context /*ctx*/,
	const std::shared_ptr<lsproto::SetTraceParams>& /*params*/) {
	// $/setTrace is sent by vscode-languageclient when trace settings change.
	// Server log verbosity is controlled separately by custom/setLogVerbosity,
	// so this handler is intentionally a no-op.
	return nullptr;
}

// handleSetLogVerbosity — server.go:1805.
gostd::Error Server::handleSetLogVerbosity(
	gostd::Context /*ctx*/,
	const std::shared_ptr<lsproto::SetLogVerbosityParams>& params) {
	if (!isValidLogVerbosity(params->Verbosity)) {
		return gostd::errorf(
			"%w: invalid log verbosity %d",
			{gostd::fmtArg(
			     lsproto::errorCodeErr(lsproto::ErrorCodeInvalidParams)),
			 gostd::fmtArg(static_cast<int32_t>(params->Verbosity))});
	}
	logger->SetVerbosity(params->Verbosity);
	return nullptr;
}

// ---------------------------------------------------------------------------
// server.go:1849 — handleDocumentDiagnostic (+ generateDiagnosticDiffString).
// ---------------------------------------------------------------------------
std::pair<lsproto::DocumentDiagnosticResponse, gostd::Error>
Server::handleDocumentDiagnostic(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::DocumentDiagnosticParams>& params) {
	ctx = core::WithCheckerLifetime(ctx, core::CheckerLifetimeDiagnostics);
	if (flakeLogging == lsproto::DiagnosticFlakeLogLevelOff) {
		return languageService->ProvideDiagnostics(ctx,
		                                           params->TextDocument.Uri);
	}
	auto [direct, err] =
		languageService->ProvideDiagnostics(ctx, params->TextDocument.Uri);
	if (err != nullptr) {
		return {std::move(direct), err};
	}
	compiler::EmitOptions emitOpts;
	emitOpts.WriteFile = [](const std::string& /*fileName*/,
	                        const std::string& /*text*/,
	                        compiler::WriteFileData* /*data*/)
	    -> std::optional<std::string> { return std::nullopt; };
	languageService->GetProgram()->Emit(&emitOpts);
	auto [secondary, err2] =
		languageService->ProvideDiagnostics(ctx, params->TextDocument.Uri);
	if (err2 != nullptr) {
		return {std::move(direct), err};
	}
	auto [missingFromPre, missingFromPost] = lsproto::CompareDiagnostics(
		direct.FullDocumentDiagnosticReport->Items.value_or(
			std::vector<std::shared_ptr<lsproto::Diagnostic>>{}),
		secondary.FullDocumentDiagnosticReport->Items.value_or(
			std::vector<std::shared_ptr<lsproto::Diagnostic>>{}));
	if (missingFromPre.empty() && missingFromPost.empty()) {
		return {std::move(direct), err};
	}

	auto diff = generateDiagnosticDiffString(
		missingFromPre, missingFromPost,
		[](const std::shared_ptr<lsproto::Diagnostic>& d) {
			return d->AsString();
		});

	logger->Error({diff});

	if (telemetryEnabled) {
		auto sanitizedDiff = generateDiagnosticDiffString(
			missingFromPre, missingFromPost,
			[](const std::shared_ptr<lsproto::Diagnostic>& d) {
				return d->CodeAsString();
			});
		lsproto::TelemetryEvent telemetry;
		telemetry.RequestFailureTelemetryEvent =
			std::make_shared<lsproto::RequestFailureTelemetryEvent>();
		telemetry.RequestFailureTelemetryEvent->Properties =
			std::make_shared<lsproto::RequestFailureTelemetryProperties>();
		telemetry.RequestFailureTelemetryEvent->Properties->ErrorCode =
			lsproto::String(lsproto::ErrorCodeInternalError);
		telemetry.RequestFailureTelemetryEvent->Properties->RequestMethod =
			"textDocument.diagnostic.flakeLog";
		telemetry.RequestFailureTelemetryEvent->Properties->Stack =
			sanitizedDiff;
		(void)sendNotification(lsproto::TelemetryEventInfo, telemetry);
	}

	if (flakeLogging == lsproto::DiagnosticFlakeLogLevelPanic) {
		throw goPanic{"flaky diagnostic(s) logged:\n" + diff};
	}
	return {std::move(direct), err};
}

// generateDiagnosticDiffString — server.go:1896.
std::string generateDiagnosticDiffString(
	const std::vector<std::shared_ptr<lsproto::Diagnostic>>& missingFromPre,
	const std::vector<std::shared_ptr<lsproto::Diagnostic>>& missingFromPost,
	const std::function<std::string(
		const std::shared_ptr<lsproto::Diagnostic>&)>& stringifier) {
	std::string b;
	for (const auto& elem : missingFromPre) {
		b += gostd::sprintf(
			"Diagnostic %v was present after emit but not before emit\n",
			{gostd::fmtArg(stringifier(elem))});
	}
	for (const auto& elem : missingFromPost) {
		b += gostd::sprintf(
			"Diagnostic %v was present before emit but not after emit\n",
			{gostd::fmtArg(stringifier(elem))});
	}
	return b;
}

// ---------------------------------------------------------------------------
// server.go:1907-… — language service handlers.
// ---------------------------------------------------------------------------

// handleHover — server.go:1907.
std::pair<lsproto::HoverResponse, gostd::Error> Server::handleHover(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::HoverParams>& params) {
	return {languageService->ProvideHover(ctx, params.get()), nullptr};
}

// handlePrepareRename — server.go:1911.
std::pair<lsproto::PrepareRenameResponse, gostd::Error>
Server::handlePrepareRename(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::PrepareRenameParams>& params) {
	auto info = languageService->GetRenameInfo(ctx, "" /*newName*/,
	                                           params->TextDocument.Uri,
	                                           params->Position);
	if (!info.CanRename) {
		return {lsproto::PrepareRenameResponse{},
		        gostd::Error(std::make_shared<userFacingRequestFailedError>(
		            info.LocalizedErrorMessage))};
	}
	lsproto::PrepareRenameResponse resp;
	resp.PrepareRenamePlaceholder =
		std::make_shared<lsproto::PrepareRenamePlaceholder>();
	resp.PrepareRenamePlaceholder->Range = info.TriggerSpan;
	resp.PrepareRenamePlaceholder->Placeholder = info.DisplayName;
	return {std::move(resp), nullptr};
}

// handleRename — server.go:1922.
std::pair<lsproto::RenameResponse, gostd::Error> Server::handleRename(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::RenameParams>& params,
	const std::shared_ptr<lsproto::RequestMessage>& req) {
	auto [defaultLs, orchestrator, err] =
		getLanguageServiceAndCrossProjectOrchestrator(
			ctx, params->TextDocument.Uri, req);
	if (err != nullptr) {
		return {lsproto::RenameResponse{}, err};
	}
	auto info = defaultLs->GetRenameInfo(ctx, params->NewName,
	                                     params->TextDocument.Uri,
	                                     params->Position);
	if (info.CanRename && !info.FileToRename.empty()) {
		// We send a `willRenameFiles` request if the client allows;
		// otherwise we directly compute the edits for renaming the file.
		if (ls::ClientSupportsWillRenameFiles(ctx)) {
			std::vector<
			    lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile>
			    documentChanges;
			{
				lsproto::
				    TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile change;
				change.RenameFile =
					std::make_shared<lsproto::RenameFile>();
				change.RenameFile->Kind = lsproto::StringLiteralRename{};
				change.RenameFile->OldUri =
					lsconv::FileNameToDocumentURI(info.FileToRename);
				change.RenameFile->NewUri =
					lsconv::FileNameToDocumentURI(info.NewFileName);
				documentChanges.push_back(std::move(change));
			}
			lsproto::WorkspaceEditOrNull resp;
			resp.WorkspaceEdit =
				std::make_shared<lsproto::WorkspaceEdit>();
			resp.WorkspaceEdit->DocumentChanges =
				std::make_shared<
				    lsproto::Slice<lsproto::
				                       TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile>>(
					std::move(documentChanges));
			return {resp, nullptr};
		}
		auto renameFilesParams =
			std::make_shared<lsproto::RenameFilesParams>();
		auto file = std::make_shared<lsproto::FileRename>();
		file->OldUri = lsconv::FileNameToDocumentURI(info.FileToRename);
		file->NewUri = lsconv::FileNameToDocumentURI(info.NewFileName);
		renameFilesParams->Files = {file};
		return handleWillRenameFilesWorker(ctx, renameFilesParams, req,
		                                   true /*sendRenameFile*/);
	}

	return defaultLs->ProvideRename(ctx, params.get(), orchestrator.get());
}

// handleWillRenameFiles — server.go:1961.
std::pair<lsproto::WillRenameFilesResponse, gostd::Error>
Server::handleWillRenameFiles(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::RenameFilesParams>& params,
	const std::shared_ptr<lsproto::RequestMessage>& msg) {
	return handleWillRenameFilesWorker(ctx, params, msg,
	                                   false /*sendRenameFile*/);
}

// handleWillRenameFilesWorker — server.go:1966. If `sendRenameFile` is true,
// the original `willRenameFiles` request is being handled as part of a rename
// operation where the client doesn't support `willRenameFiles`, so we should
// include the file rename in the edits we return.
std::pair<lsproto::WillRenameFilesResponse, gostd::Error>
Server::handleWillRenameFilesWorker(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::RenameFilesParams>& params,
	const std::shared_ptr<lsproto::RequestMessage>& /*msg*/,
	bool sendRenameFile) {
	if (!params->Files.has_value() || params->Files->empty()) {
		return {lsproto::WillRenameFilesResponse{}, nullptr};
	}

	std::vector<lsproto::DocumentUri> uris;
	uris.reserve(params->Files->size());
	for (const auto& file : *params->Files) {
		uris.push_back(file->OldUri);
	}

	if (uris.empty()) {
		return {lsproto::WillRenameFilesResponse{}, nullptr};
	}

	auto services =
		session->GetLanguageServicesForDocumentsLoadingProjectTree(ctx, uris);

	struct editKey {
		lsproto::DocumentUri uri;
		lsproto::Range range_;
		bool operator==(const editKey& other) const = default;
	};
	struct editKeyHash {
		size_t operator()(const editKey& k) const {
			auto h1 = std::hash<lsproto::DocumentUri>()(k.uri);
			auto h2 = std::hash<std::string>()(
				std::to_string(k.range_.Start.Line) + ":" +
				std::to_string(k.range_.Start.Character) + "-" +
				std::to_string(k.range_.End.Line) + ":" +
				std::to_string(k.range_.End.Character));
			return h1 ^ (h2 << 1);
		}
	};
	std::unordered_map<editKey, std::string, editKeyHash> seenEdits;
	std::unordered_set<lsproto::DocumentUri> seenRenames;
	std::vector<lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile>
		documentChanges;

	for (auto* languageService : services) {
		for (const auto& file : *params->Files) {
			auto changes = languageService->GetEditsForFileRename(
				ctx, file->OldUri, file->NewUri);
			for (auto& change : changes) {
				if (change.RenameFile != nullptr) {
					if (seenRenames.insert(change.RenameFile->OldUri).second) {
						documentChanges.push_back(change);
					}
				} else if (change.TextDocumentEdit != nullptr) {
					auto uri =
						change.TextDocumentEdit->TextDocument.Uri;
					std::vector<
					    lsproto::TextEditOrAnnotatedTextEditOrSnippetTextEdit>
					    deduped;
					for (const auto& edit :
					     *change.TextDocumentEdit->Edits) {
						if (edit.TextEdit != nullptr) {
							editKey key{uri, edit.TextEdit->Range};
							auto it = seenEdits.find(key);
							if (it != seenEdits.end() &&
							    it->second == edit.TextEdit->NewText) {
								continue;
							}
							seenEdits[key] = edit.TextEdit->NewText;
						}
						deduped.push_back(edit);
					}
					if (!deduped.empty()) {
						lsproto::
						    TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile
						        wrapped;
						wrapped.TextDocumentEdit =
							std::make_shared<lsproto::TextDocumentEdit>();
						wrapped.TextDocumentEdit->TextDocument =
							change.TextDocumentEdit->TextDocument;
						wrapped.TextDocumentEdit->Edits =
							std::move(deduped);
						documentChanges.push_back(std::move(wrapped));
					}
				}
			}
		}
	}

	if (sendRenameFile) {
		for (const auto& file : *params->Files) {
			lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile
			    change;
			change.RenameFile = std::make_shared<lsproto::RenameFile>();
			change.RenameFile->Kind = lsproto::StringLiteralRename{};
			change.RenameFile->OldUri = file->OldUri;
			change.RenameFile->NewUri = file->NewUri;
			documentChanges.push_back(std::move(change));
		}
	}

	if (documentChanges.empty()) {
		return {lsproto::WillRenameFilesResponse{}, nullptr};
	}

	lsproto::WillRenameFilesResponse resp;
	resp.WorkspaceEdit = std::make_shared<lsproto::WorkspaceEdit>();
	if (ls::ClientSupportsDocumentChanges(ctx)) {
		resp.WorkspaceEdit->DocumentChanges = std::make_shared<lsproto::Slice<
		    lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile>>(
			std::move(documentChanges));
		return {std::move(resp), nullptr};
	}

	lsproto::Map<lsproto::DocumentUri,
	             lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>
	    changes;
	for (const auto& change : documentChanges) {
		if (change.TextDocumentEdit != nullptr &&
		    change.TextDocumentEdit->Edits.has_value()) {
			auto uri = change.TextDocumentEdit->TextDocument.Uri;
			for (const auto& edit : *change.TextDocumentEdit->Edits) {
				if (edit.TextEdit != nullptr) {
					auto& slot = changes[uri];
					if (!slot.has_value()) {
						slot.emplace();
					}
					slot->push_back(edit.TextEdit);
				}
			}
		}
	}
	resp.WorkspaceEdit->Changes = std::make_shared<
	    lsproto::Map<lsproto::DocumentUri,
	                 lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>>(
		std::move(changes));
	return {std::move(resp), nullptr};
}

// handleSignatureHelp — server.go:2070.
std::pair<lsproto::SignatureHelpResponse, gostd::Error>
Server::handleSignatureHelp(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::SignatureHelpParams>& params) {
	return languageService->ProvideSignatureHelp(
		ctx, params->TextDocument.Uri, params->Position,
		params->Context.get());
}

// handleFoldingRange — server.go:2079.
std::pair<lsproto::FoldingRangeResponse, gostd::Error>
Server::handleFoldingRange(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::FoldingRangeParams>& params) {
	return {languageService->ProvideFoldingRange(ctx,
	                                            params->TextDocument.Uri),
	        nullptr};
}

// handleVSOnAutoInsert — server.go:2083.
std::pair<lsproto::VSOnAutoInsertResponse, gostd::Error>
Server::handleVSOnAutoInsert(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::VSOnAutoInsertParams>& params) {
	return languageService->ProvideOnAutoInsert(ctx, params.get());
}

// handleLinkedEditingRange — server.go:2087.
std::pair<lsproto::LinkedEditingRangeResponse, gostd::Error>
Server::handleLinkedEditingRange(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::LinkedEditingRangeParams>& params) {
	return languageService->ProvideLinkedEditingRange(ctx, params.get());
}

// handleDefinition — server.go:2091.
std::pair<lsproto::DefinitionResponse, gostd::Error> Server::handleDefinition(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::DefinitionParams>& params) {
	return {languageService->ProvideDefinition(ctx, params->TextDocument.Uri,
	                                          params->Position),
	        nullptr};
}

// handleSourceDefinition — server.go:2095.
std::pair<lsproto::CustomTextDocumentSourceDefinitionResponse, gostd::Error>
Server::handleSourceDefinition(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::TextDocumentPositionParams>& params) {
	auto resp = languageService->ProvideSourceDefinition(
		ctx, params->TextDocument.Uri, params->Position);
	return {std::make_shared<
	            lsproto::LocationOrLocationsOrDefinitionLinksOrNull>(
	            std::move(resp)),
	        nullptr};
}

// handleTypeDefinition — server.go:2103.
std::pair<lsproto::TypeDefinitionResponse, gostd::Error>
Server::handleTypeDefinition(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::TypeDefinitionParams>& params) {
	return {languageService->ProvideTypeDefinition(
		ctx, params->TextDocument.Uri, params->Position),
	        nullptr};
}

// handleCompletion — server.go:2107.
std::pair<lsproto::CompletionResponse, gostd::Error> Server::handleCompletion(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::CompletionParams>& params) {
	return languageService->ProvideCompletion(
		toContextPtr(ctx), params->TextDocument.Uri, params->Position,
		params->Context.get());
}

// handleCompletionItemResolve — server.go:2117.
std::pair<lsproto::CompletionResolveResponse, gostd::Error>
Server::handleCompletionItemResolve(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::CompletionItem>& params,
	const std::shared_ptr<lsproto::RequestMessage>& reqMsg) {
	auto* data = params->Data.get();
	if (data == nullptr) {
		return {nullptr, gostd::newError("completion item data is nil")};
	}
	if (!tspath::pathIsAbsolute(data->FileName)) {
		return {nullptr,
		        gostd::newError(
		            "completion item data fileName must be absolute")};
	}
	lsproto::DocumentUri uri;
	if (tspath::isDynamicFileName(data->FileName)) {
		auto [u, ok] =
		    lsproto::tryDynamicFileNameToDocumentUri(data->FileName);
		if (!ok) {
			return {nullptr,
			        gostd::newError(
			            "completion item data fileName must be a valid "
			            "dynamic path")};
		}
		uri = u;
	} else {
		uri = lsconv::FileNameToDocumentURI(data->FileName);
	}
	auto [languageService, err] = session->GetLanguageService(ctx, uri);
	if (err != nullptr) {
		return {nullptr, err};
	}
	// defer s.recover(reqMsg)
	lsproto::CompletionResolveResponse resp;
	gostd::Error lsErr;
	try {
		auto r = languageService->ResolveCompletionItem(
			toContextPtr(ctx), params.get(), data);
		// Go returns the same *CompletionItem as params (GC-shared). Alias
		// params' ownership so the response doesn't own a second control
		// block over the request item — a plain shared_ptr(r.first) would
		// delete it while params still holds it (writer-thread UAF).
		resp = std::shared_ptr<lsproto::CompletionItem>(params, r.first);
		lsErr = r.second;
	} catch (...) {
		recover_(reqMsg);
		return {nullptr, nullptr};
	}
	return {resp, lsErr};
}

// handleDocumentFormat — server.go:2130.
std::pair<lsproto::DocumentFormattingResponse, gostd::Error>
Server::handleDocumentFormat(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::DocumentFormattingParams>& params) {
	return {languageService->ProvideFormatDocument(ctx,
	                                              params->TextDocument.Uri,
	                                              params->Options.get()),
	        nullptr};
}

// handleDocumentRangeFormat — server.go:2138.
std::pair<lsproto::DocumentRangeFormattingResponse, gostd::Error>
Server::handleDocumentRangeFormat(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::DocumentRangeFormattingParams>& params) {
	return {languageService->ProvideFormatDocumentRange(
		        ctx, params->TextDocument.Uri, params->Options.get(),
		        params->Range),
	        nullptr};
}

// handleDocumentOnTypeFormat — server.go:2147.
std::pair<lsproto::DocumentOnTypeFormattingResponse, gostd::Error>
Server::handleDocumentOnTypeFormat(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::DocumentOnTypeFormattingParams>& params) {
	return {languageService->ProvideFormatDocumentOnType(
		        ctx, params->TextDocument.Uri, params->Options.get(),
		        params->Position, params->Ch),
	        nullptr};
}

// handleWorkspaceSymbol — server.go:2157.
std::pair<lsproto::WorkspaceSymbolResponse, gostd::Error>
Server::handleWorkspaceSymbol(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::WorkspaceSymbolParams>& params,
	const std::shared_ptr<lsproto::RequestMessage>& reqMsg) {
	lsproto::WorkspaceSymbolResponse resp;
	gostd::Error lsErr;
	auto provideSymbols = [&](project::Snapshot* snapshot,
	                          std::vector<compiler::SimpleProgram*> programs) {
		try {
			auto r = ls::ProvideWorkspaceSymbols(
				ctx, programs, snapshot->Converters(),
				snapshot->UserPreferences(), params->Query);
			resp = std::move(r.first);
			lsErr = std::move(r.second);
		} catch (...) {
			recover_(reqMsg); // defer s.recover(reqMsg)
		}
	};
	if (params->TextDocument != nullptr &&
	    session->Config().WorkspaceSymbolsScope ==
	        ls::lsutil::WorkspaceSymbolsScopeCurrentProject) {
		auto uri = params->TextDocument->Uri;
		session->WithSnapshotForDocument(ctx, uri,
		                                 [&](project::Snapshot* snapshot) {
			auto programs = mapVec<compiler::SimpleProgram*>(
				snapshot->GetLanguageServiceProjectsContainingFile(uri),
				[](ls::Project* p) { return p->GetProgram(); });
			provideSymbols(snapshot, programs);
		});
	} else {
		session->WithSnapshotLoadingProjectTree(
			ctx, nullptr, [&](project::Snapshot* snapshot) {
				auto projects =
					snapshot->ProjectCollection->LanguageServiceProjects();
				std::vector<ls::Project*> lsProjects(projects.begin(),
				                                     projects.end());
				auto programs = mapVec<compiler::SimpleProgram*>(
					lsProjects, [](ls::Project* p) { return p->GetProgram(); });
				provideSymbols(snapshot, programs);
			});
	}
	return {std::move(resp), lsErr};
}

// handleDocumentSymbol — server.go:2184.
std::pair<lsproto::DocumentSymbolResponse, gostd::Error>
Server::handleDocumentSymbol(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::DocumentSymbolParams>& params) {
	return {languageService->ProvideDocumentSymbols(ctx,
	                                               params->TextDocument.Uri),
	        nullptr};
}

// handleDocumentHighlight — server.go:2188.
std::pair<lsproto::DocumentHighlightResponse, gostd::Error>
Server::handleDocumentHighlight(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::DocumentHighlightParams>& params) {
	return {languageService->ProvideDocumentHighlights(
		ctx, params->TextDocument.Uri, params->Position),
	        nullptr};
}

// handleMultiDocumentHighlight — server.go:2192.
std::pair<lsproto::CustomMultiDocumentHighlightResponse, gostd::Error>
Server::handleMultiDocumentHighlight(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::MultiDocumentHighlightParams>& params) {
	return {languageService->ProvideMultiDocumentHighlights(
		ctx, params->TextDocument.Uri, params->Position,
		params->FilesToSearch.value_or(std::vector<lsproto::DocumentUri>{})),
	        nullptr};
}

// handleSelectionRange — server.go:2196.
std::pair<lsproto::SelectionRangeResponse, gostd::Error>
Server::handleSelectionRange(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::SelectionRangeParams>& params) {
	return {languageService->ProvideSelectionRanges(ctx, params.get()),
	        nullptr};
}

// handleCodeAction — server.go:2200.
std::pair<lsproto::CodeActionResponse, gostd::Error> Server::handleCodeAction(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::CodeActionParams>& params) {
	return languageService->ProvideCodeActions(ctx, params.get());
}

// handleInlayHint — server.go:2204.
std::pair<lsproto::InlayHintResponse, gostd::Error> Server::handleInlayHint(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::InlayHintParams>& params) {
	return {languageService->ProvideInlayHint(ctx, params.get()), nullptr};
}

// handleCodeLens — server.go:2212.
std::pair<lsproto::CodeLensResponse, gostd::Error> Server::handleCodeLens(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::CodeLensParams>& params) {
	return {languageService->ProvideCodeLenses(ctx, params->TextDocument.Uri),
	        nullptr};
}

// handleCodeLensResolve — server.go:2216.
std::pair<std::shared_ptr<lsproto::CodeLens>, gostd::Error>
Server::handleCodeLensResolve(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::CodeLens>& codeLens,
	const std::shared_ptr<lsproto::RequestMessage>& reqMsg) {
	auto [defaultLs, orchestrator, err] =
		getLanguageServiceAndCrossProjectOrchestrator(
			ctx, codeLens->Data->Uri, reqMsg);
	if (gostd::ctxErr(ctx) != nullptr) {
		return {nullptr, gostd::ctxErr(ctx)};
	}
	if (err != nullptr) {
		// This can happen if a codeLens/resolve request comes in after a
		// program change. While it's true that handlers should latch onto a
		// specific snapshot while processing requests, we just set `Data.Uri`
		// based on some older snapshot's contents. The content could have
		// been modified, or the file itself could have been removed from the
		// session entirely. Note this won't bail out on every change, but
		// will prevent crashing based on non-existent files and line maps
		// from shortened files.
		return {codeLens, lsproto::errorCodeErr(
			                   lsproto::ErrorCodeContentModified)};
	}
	// defer s.recover(reqMsg)
	try {
		auto r = defaultLs->ResolveCodeLens(
			ctx, codeLens.get(),
			initializationOptions->CodeLensShowLocationsCommandName.has_value()
			    ? &*initializationOptions->CodeLensShowLocationsCommandName
			    : nullptr,
			orchestrator.get());
		// Go returns the same *CodeLens as the request params (GC-shared).
		// Alias the params' ownership so the response doesn't own a second
		// control block over the request item — a plain shared_ptr(r.first)
		// would delete it while the request still holds it (double-free /
		// writer-thread UAF, same family as handleCompletionItemResolve).
		return {std::shared_ptr<lsproto::CodeLens>(codeLens, r.first),
		        r.second};
	} catch (...) {
		recover_(reqMsg);
		return {nullptr, nullptr};
	}
}

// handlePrepareCallHierarchy — server.go:2246.
std::pair<lsproto::CallHierarchyPrepareResponse, gostd::Error>
Server::handlePrepareCallHierarchy(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::CallHierarchyPrepareParams>& params) {
	return {languageService->ProvidePrepareCallHierarchy(
		ctx, params->TextDocument.Uri, params->Position),
	        nullptr};
}

// handleCallHierarchyIncomingCalls — server.go:2256.
std::pair<lsproto::CallHierarchyIncomingCallsResponse, gostd::Error>
Server::handleCallHierarchyIncomingCalls(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::CallHierarchyIncomingCallsParams>& params,
	const std::shared_ptr<lsproto::RequestMessage>& reqMsg) {
	auto [defaultLs, orchestrator, err] =
		getLanguageServiceAndCrossProjectOrchestrator(ctx, params->Item->Uri,
		                                              reqMsg);
	if (err != nullptr) {
		return {lsproto::CallHierarchyIncomingCallsOrNull{}, err};
	}
	return {defaultLs->ProvideCallHierarchyIncomingCalls(ctx,
	                                                    params->Item.get(),
	                                                    orchestrator.get()),
	        nullptr};
}

// handleCallHierarchyOutgoingCalls — server.go:2267.
std::pair<lsproto::CallHierarchyOutgoingCallsResponse, gostd::Error>
Server::handleCallHierarchyOutgoingCalls(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::CallHierarchyOutgoingCallsParams>& params,
	const std::shared_ptr<lsproto::RequestMessage>& /*req*/) {
	auto [languageService, err] =
		session->GetLanguageService(ctx, params->Item->Uri);
	if (err != nullptr) {
		return {lsproto::CallHierarchyOutgoingCallsOrNull{}, err};
	}
	return {languageService->ProvideCallHierarchyOutgoingCalls(
		        ctx, params->Item.get()),
	        nullptr};
}

// handleSemanticTokensFull — server.go:2278.
std::pair<lsproto::SemanticTokensResponse, gostd::Error>
Server::handleSemanticTokensFull(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::SemanticTokensParams>& params) {
	return {languageService->ProvideSemanticTokens(ctx,
	                                              params->TextDocument.Uri),
	        nullptr};
}

// handleSemanticTokensRange — server.go:2282.
std::pair<lsproto::SemanticTokensRangeResponse, gostd::Error>
Server::handleSemanticTokensRange(
	gostd::Context ctx, ls::LanguageService* languageService,
	const std::shared_ptr<lsproto::SemanticTokensRangeParams>& params) {
	return {languageService->ProvideSemanticTokensRange(
		        ctx, params->TextDocument.Uri, params->Range),
	        nullptr};
}

// ---------------------------------------------------------------------------
// server.go:2286 — handleInitializeAPISession + helpers.
// ---------------------------------------------------------------------------
std::pair<lsproto::CustomInitializeAPISessionResponse, gostd::Error>
Server::handleInitializeAPISession(
	gostd::Context /*ctx*/,
	const std::shared_ptr<lsproto::InitializeAPISessionParams>& params,
	const std::shared_ptr<lsproto::RequestMessage>& /*req*/) {
	std::lock_guard<std::mutex> lock(apiSessionsMu);

	if (apiSessions.empty()) {
		apiSessions = {};
	}

	auto apiSession = api::NewLSPSession(session, nullptr);

	// Use provided pipe path or generate a unique one
	std::string pipePath;
	if (params->Pipe.has_value() && !params->Pipe->empty()) {
		pipePath = *params->Pipe;
	} else {
		pipePath = generateAPIPipePath();
	}

	auto transportRes = ipc::NewPipeTransport(pipePath);
	if (transportRes.second != nullptr) {
		return {nullptr,
		        gostd::errorf("failed to create API transport: %w",
		                      {gostd::fmtArg(transportRes.second)})};
	}
	auto transport = std::move(transportRes.first);

	auto apiCtxAndCancel = gostd::contextWithCancel(backgroundCtx);
	auto apiCtx = apiCtxAndCancel.first;
	auto apiCancel = apiCtxAndCancel.second;
	auto state = std::make_shared<apiSessionState>();
	state->session = apiSession;
	state->transport = transport;
	state->cancel = apiCancel;
	apiSessions[apiSession->ID()] = state;

	// Start accepting connections in the background
	std::weak_ptr<Server> weakSelf = shared_from_this();
	std::thread([weakSelf, state, apiCtx] {
		auto s = weakSelf.lock();
		auto apiSession = state->session;
		// defer close(state.done)
		struct doneGuard {
			std::shared_ptr<apiSessionState> state;
			~doneGuard() { state->done.set_value(); }
		} doneGuard{state};
		// defer apiCancel()
		struct cancelGuard {
			gostd::CancelFunc f;
			~cancelGuard() { if (f) f(); }
		} cancelGuard{state->cancel};
		// defer { apiSession.Close(); s.removeAPISession(apiSession.ID()) }
		struct cleanupSession {
			std::shared_ptr<api::Session> apiSession;
			std::weak_ptr<Server> weakSelf;
			~cleanupSession() {
				apiSession->Close();
				if (auto s = weakSelf.lock()) {
					s->removeAPISession(apiSession->ID());
				}
			}
		} sessionGuard{apiSession, weakSelf};

		auto [rwc, acceptErr] = state->transport->Accept();
		if (acceptErr != nullptr) {
			if (s != nullptr) {
				s->logger->Errorf(
					"API session %s: failed to accept connection: %v",
					{gostd::fmtArg(apiSession->ID()),
					 gostd::fmtArg(acceptErr)});
			}
			return;
		}
		if (!state->attachConnection(rwc)) {
			return;
		}
		struct rwcGuard {
			std::shared_ptr<gostd::io::ReadWriteCloser> c;
			~rwcGuard() { (void)c->close(); }
		} connGuard{rwc};

		try {
			auto conn = ipc::NewAsyncConn(rwc, apiSession);
			apiSession->SetConnection(conn);
			if (auto apiErr = conn->Run(apiCtx); apiErr != nullptr) {
				if (s != nullptr) {
					s->logger->Errorf("API session %s: %v",
					                  {gostd::fmtArg(apiSession->ID()),
					                   gostd::fmtArg(apiErr)});
				}
			}
		} catch (...) {
			if (s != nullptr) {
				auto r = ipc::panicText(std::current_exception());
				auto stack = ipc::debugStack();
				s->logger->Errorf("API session %s: panic: %v\n%s",
				                  {gostd::fmtArg(apiSession->ID()),
				                   gostd::fmtArg(std::string_view(r)),
				                   gostd::fmtArg(std::string_view(stack))});
			}
		}
	}).detach();

	auto result = std::make_shared<lsproto::InitializeAPISessionResult>();
	result->SessionId = apiSession->ID();
	result->Pipe = pipePath;
	return {result, nullptr};
}

// generateAPIPipePath — server.go:2352.
std::string Server::generateAPIPipePath() {
	// Generate a high-entropy path using time and random source
	auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
	               std::chrono::system_clock::now().time_since_epoch())
	               .count();
	static std::mt19937_64 rnd{std::random_device{}()};
	return ipc::GeneratePipePath(
		gostd::sprintf("tsgo-api-%x-%x",
		               {gostd::fmtArg(now),
		                gostd::fmtArg(static_cast<uint64_t>(rnd()))}));
}

// removeAPISession — server.go:2414.
void Server::removeAPISession(const std::string& id) {
	std::lock_guard<std::mutex> lock(apiSessionsMu);
	apiSessions.erase(id);
}

// closeAPISessions — server.go:2420.
void Server::closeAPISessions() {
	std::vector<std::shared_ptr<apiSessionState>> sessions;
	{
		std::lock_guard<std::mutex> lock(apiSessionsMu);
		sessions.reserve(apiSessions.size());
		for (auto& [id, state] : apiSessions) {
			sessions.push_back(state);
		}
		apiSessions.clear();
	}
	for (auto& state : sessions) {
		state->stop();
	}
}

// SetCompilerOptionsForInferredProjects — server.go:2363.
// !!! temporary; remove when we have `handleDidChangeConfiguration`/implicit
// project config support
void Server::SetCompilerOptionsForInferredProjects(
	gostd::Context ctx, CompilerOptions* options) {
	compilerOptionsForInferredProjects = options;
	if (session != nullptr) {
		session->DidChangeCompilerOptionsForInferredProjects(ctx, options);
	}
}

// NpmInstall — server.go:2371 (ata.NpmExecutor).
std::pair<std::string, gostd::Error> Server::NpmInstall(
	const gostd::Context& ctx, const std::string& cwd,
	const std::vector<std::string>& args) {
	auto [out, err] = npmInstall(ctx, cwd, args);
	return {std::string(out.begin(), out.end()), err};
}

// contentMapperSpawner — server.go:2377. Adapts the server's spawn callback
// to a content mapper spawner, or returns nullptr when the server cannot
// spawn processes.
std::shared_ptr<contentmapper::Spawner> Server::contentMapperSpawner() {
	if (spawn == nullptr) {
		return nullptr;
	}
	return std::make_shared<contentmapper::SpawnerFunc>(spawn);
}

// contentMapperLogger — server.go:2384.
std::function<void(std::string_view)> Server::contentMapperLogger() {
	auto* server = this;
	return [server](std::string_view message) {
		if (server->logger->IsTracing()) {
			server->logger->Info({std::string(message)});
		}
	};
}

// ---------------------------------------------------------------------------
// server.go:2392 — developer/debugging command handlers.
// ---------------------------------------------------------------------------

// handleRunGC — server.go:2394.
std::pair<lsproto::RunGCResponse, gostd::Error> Server::handleRunGC(
	gostd::Context /*ctx*/, const lsproto::NoParams& /*params*/,
	const std::shared_ptr<lsproto::RequestMessage>& /*req*/) {
	pprof::runGC();
	logger->Info({"GC triggered"});
	return {lsproto::Null{}, nullptr};
}

// handleSaveHeapProfile — server.go:2400.
std::pair<std::shared_ptr<lsproto::ProfileResult>, gostd::Error>
Server::handleSaveHeapProfile(
	gostd::Context /*ctx*/,
	const std::shared_ptr<lsproto::ProfileParams>& params,
	const std::shared_ptr<lsproto::RequestMessage>& /*req*/) {
	auto [filePath, err] = pprof::saveHeapProfile(params->Dir);
	if (!err.empty()) {
		return {nullptr, gostd::newError(err)};
	}
	logger->Info({"Heap profile saved to: ", filePath});
	return {std::make_shared<lsproto::ProfileResult>(
	            lsproto::ProfileResult{filePath}),
	        nullptr};
}

// handleSaveAllocProfile — server.go:2409.
std::pair<std::shared_ptr<lsproto::ProfileResult>, gostd::Error>
Server::handleSaveAllocProfile(
	gostd::Context /*ctx*/,
	const std::shared_ptr<lsproto::ProfileParams>& params,
	const std::shared_ptr<lsproto::RequestMessage>& /*req*/) {
	auto [filePath, err] = pprof::saveAllocProfile(params->Dir);
	if (!err.empty()) {
		return {nullptr, gostd::newError(err)};
	}
	logger->Info({"Allocation profile saved to: ", filePath});
	return {std::make_shared<lsproto::ProfileResult>(
	            lsproto::ProfileResult{filePath}),
	        nullptr};
}

// handleStartCPUProfile — server.go:2418.
std::pair<lsproto::StartCPUProfileResponse, gostd::Error>
Server::handleStartCPUProfile(
	gostd::Context /*ctx*/,
	const std::shared_ptr<lsproto::ProfileParams>& params,
	const std::shared_ptr<lsproto::RequestMessage>& /*req*/) {
	auto err = cpuProfiler.startCPUProfile(params->Dir);
	if (!err.empty()) {
		return {lsproto::Null{}, gostd::newError(err)};
	}
	logger->Info({"CPU profiling started, will save to: ", params->Dir});
	return {lsproto::Null{}, nullptr};
}

// handleStopCPUProfile — server.go:2427.
std::pair<std::shared_ptr<lsproto::ProfileResult>, gostd::Error>
Server::handleStopCPUProfile(
	gostd::Context /*ctx*/, const lsproto::NoParams& /*params*/,
	const std::shared_ptr<lsproto::RequestMessage>& /*req*/) {
	auto [filePath, err] = cpuProfiler.stopCPUProfile();
	if (!err.empty()) {
		return {nullptr, gostd::newError(err)};
	}
	logger->Info({"CPU profile saved to: ", filePath});
	return {std::make_shared<lsproto::ProfileResult>(
	            lsproto::ProfileResult{filePath}),
	        nullptr};
}

// ---------------------------------------------------------------------------
// server.go:2437 — handleProjectInfo / handleSetContentMapperContributions.
// ---------------------------------------------------------------------------
std::pair<lsproto::CustomProjectInfoResponse, gostd::Error>
Server::handleProjectInfo(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::ProjectInfoParams>& params,
	const std::shared_ptr<lsproto::RequestMessage>& /*req*/) {
	auto uri = params->TextDocument.Uri;
	auto result = session->GetLanguageServiceAndProjectsForFile(ctx, uri);
	if (std::get<3>(result) != nullptr) {
		return {nullptr, std::get<3>(result)};
	}
	std::string configFilePath;
	if (std::get<0>(result) != nullptr &&
	    std::get<0>(result)->Kind == project::KindConfigured) {
		configFilePath = std::get<0>(result)->ConfigFileName();
	}
	return {std::make_shared<lsproto::ProjectInfoResult>(
	            lsproto::ProjectInfoResult{configFilePath}),
	        nullptr};
}

// handleSetContentMapperContributions — server.go:2454.
std::pair<lsproto::CustomSetContentMapperContributionsResponse, gostd::Error>
Server::handleSetContentMapperContributions(
	gostd::Context ctx,
	const std::shared_ptr<lsproto::SetContentMapperContributionsParams>&
	    params,
	const std::shared_ptr<lsproto::RequestMessage>& /*req*/) {
	auto [contributions, err] =
		parseContentMapperContributions(
			params->Contributions.value_or(
			    std::vector<std::shared_ptr<lsproto::ContentMapperContribution>>{}));
	if (err != nullptr) {
		return {lsproto::Null{}, err};
	}
	auto documents = mapVec<lsproto::DocumentUri>(
		params->OpenDocuments.value_or(
			std::vector<lsproto::TextDocumentIdentifier>{}),
		[](const lsproto::TextDocumentIdentifier& document) {
			return document.Uri;
		});
	session->SetContentMapperContributions(ctx, contributions, documents);
	return {lsproto::Null{}, nullptr};
}

// parseContentMapperContributions — server.go:2465.
std::pair<project::ContentMapperContributions, gostd::Error>
parseContentMapperContributions(
	const std::vector<std::shared_ptr<lsproto::ContentMapperContribution>>&
	    values) {
	project::ContentMapperContributions result;
	collections::Set<std::string> claimedExtensions;
	for (size_t index = 0; index < values.size(); index++) {
		const auto& value = values[index];
		if (value == nullptr || value->ContributorId.empty()) {
			return {result, gostd::newError(
			                    "content mapper contribution requires a "
			                    "contributorId")};
		}
		auto identity = gostd::sprintf(
			"%s[%d]", {gostd::fmtArg(std::string_view(value->ContributorId)),
			           gostd::fmtArg(static_cast<int64_t>(index))});
		std::vector<std::string> validExtensions;
		for (const auto& extension : value->Extensions.value_or(
		         std::vector<std::string>{})) {
			if (!isValidContributedContentMapperExtension(extension)) {
				return {result,
				        gostd::errorf(
				            "content mapper contribution %q has invalid "
				            "extension %q",
				            {gostd::fmtArg(std::string_view(identity)),
				             gostd::fmtArg(std::string_view(extension))})};
			}
			validExtensions.push_back(extension);
		}
		if (value->InferredProjectContribution == nullptr) {
			continue;
		}
		const auto& inferredProject = *value->InferredProjectContribution;
		auto& manifest = inferredProject.Manifest;
		if (manifest == nullptr || manifest->Name.empty() ||
		    !manifest->Exec.has_value() || manifest->Exec->empty()) {
			return {result, gostd::errorf(
			                    "content mapper contribution %q requires a "
			                    "manifest name and exec",
			                    {gostd::fmtArg(std::string_view(identity))})};
		}
		if (manifest->CompilerOptions != nullptr) {
			for (const auto& option : manifest->CompilerOptions->value_or(
			         std::vector<std::string>{})) {
				if (tsoptions::CommandLineCompilerOptionsMap().Get(option) ==
				    nullptr) {
					return {result,
					        gostd::errorf(
					            "content mapper contribution %q requests "
					            "unknown compiler option %q",
					            {gostd::fmtArg(std::string_view(identity)),
					             gostd::fmtArg(std::string_view(option))})};
				}
			}
		}
		for (const auto& extension : validExtensions) {
			std::string lower = extension;
			std::transform(lower.begin(), lower.end(), lower.begin(),
			               [](unsigned char c) { return std::tolower(c); });
			if (!claimedExtensions.AddIfAbsent(lower)) {
				return {result,
				        gostd::errorf(
				            "content mapper contributions both claim "
				            "extension %q",
				            {gostd::fmtArg(std::string_view(extension))})};
			}
			result.Extensions.push_back(extension);
		}
		json::Value options{"{}"};
		if (inferredProject.Options != nullptr) {
			auto [m, merr] = json::marshal(*inferredProject.Options);
			if (!merr.empty()) {
				return {result,
				        gostd::errorf("content mapper contribution %q has "
				                      "invalid options",
				                      {gostd::fmtArg(
				                          std::string_view(identity))})};
			}
			options = json::Value{m};
		}
		auto* mapper = new contentmapper::Mapper();
		mapper->Definition.Package = identity;
		mapper->Definition.Extensions = validExtensions;
		mapper->Definition.Options = options;
		mapper->Manifest.Name = manifest->Name;
		mapper->Manifest.Version = manifest->Version.value_or("");
		mapper->Manifest.Exec =
			manifest->Exec.value_or(std::vector<std::string>{});
		if (manifest->CompilerOptions != nullptr) {
			mapper->Manifest.CompilerOptions =
				manifest->CompilerOptions->value_or(
				    std::vector<std::string>{});
		}
		mapper->Manifest.DynamicConfig =
			manifest->DynamicConfig.value_or(false);
		mapper->ContributionID = identity;
		if (manifest->Cwd.has_value()) {
			if (!tspath::pathIsAbsolute(*manifest->Cwd)) {
				return {result,
				        gostd::errorf("content mapper contribution %q has "
				                      "non-absolute cwd",
				                      {gostd::fmtArg(
				                          std::string_view(identity))})};
			}
			mapper->PackageDirectory = *manifest->Cwd;
		}
		result.Mappers.push_back(mapper);
	}
	std::sort(result.Extensions.begin(), result.Extensions.end());
	return {std::move(result), nullptr};
}

// isValidContributedContentMapperExtension — server.go:2511.
bool isValidContributedContentMapperExtension(std::string_view extension) {
	if (extension.size() <= 1 || extension[0] != '.' ||
	    tspath::getAnyExtensionFromPath("file" + std::string(extension),
	                                    nullptr, false) != extension) {
		return false;
	}
	for (const auto& group : tspath::allSupportedExtensionsWithJson) {
		for (const auto& nativeExtension : group) {
			if (stringutil::EquateStringCaseInsensitive(nativeExtension,
			                                            extension)) {
				return false;
			}
		}
	}
	return true;
}

} // namespace tsc::lsp
