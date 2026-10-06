#pragma once

// client.go — the Client interface (LSP host callbacks) + WatcherID.

#include <string>
#include <vector>

#include "internal/diagnostics/diagnostics.h"
#include "internal/gostd/gostd.h"
#include "internal/locale/locale.h"
#include "internal/ls/lsutil/lsutil.h" // lsproto dep decls

namespace tsc::project {

// WatcherID — watch.go:155 (`type WatcherID string`).
using WatcherID = std::string;

// Client — client.go.
struct Client {
	virtual ~Client() = default;
	virtual gostd::Error WatchFiles(
		const gostd::Context& ctx, WatcherID id,
		const std::vector<lsp::lsproto::FileSystemWatcher*>& watchers) = 0;
	virtual gostd::Error UnwatchFiles(const gostd::Context& ctx,
	                                  WatcherID id) = 0;
	virtual gostd::Error RegisterContentMapperExtensions(
		const gostd::Context& ctx,
		const std::vector<std::string>& extensions) = 0;
	virtual gostd::Error RefreshDiagnostics(const gostd::Context& ctx) = 0;
	virtual gostd::Error PublishDiagnostics(
		const gostd::Context& ctx,
		lsp::lsproto::PublishDiagnosticsParams* params) = 0;
	virtual gostd::Error RefreshInlayHints(const gostd::Context& ctx) = 0;
	virtual gostd::Error RefreshCodeLens(const gostd::Context& ctx) = 0;
	// `*diagnostics.Message` -> `const DiagnosticMessage*`; `args ...any` are
	// pre-stringified (the Go StringifyArgs step happens at call sites).
	virtual void ProgressStart(
		const DiagnosticMessage* message,
		const std::vector<std::string>& args) = 0;
	virtual void ProgressFinish(
		const DiagnosticMessage* message,
		const std::vector<std::string>& args) = 0;
	virtual gostd::Error SendTelemetry(
		const gostd::Context& ctx,
		lsp::lsproto::TelemetryEvent telemetry) = 0;
	virtual bool IsActive() = 0;
	// SetLocale updates the locale used for diagnostic messages.
	virtual void SetLocale(const std::string& locale) = 0;
	// GetLocale returns the current display locale for diagnostic messages.
	virtual locale::Locale GetLocale() = 0;
};

} // namespace tsc::project
