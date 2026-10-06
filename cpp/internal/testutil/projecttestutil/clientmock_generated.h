// clientmock_generated.h — port of tsc/internal/testutil/projecttestutil/
// clientmock_generated.go (moq-generated ClientMock). Implements
// project::Client.
#pragma once

#include <any>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/locale/locale.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/project/project.h"
#include "internal/project/client.h"

namespace tsc::testutil::projecttestutil {

// Call records (clientmock_generated.go anon callInfo structs).
struct ClientMockGetLocaleCall {};
struct ClientMockIsActiveCall {};
struct ClientMockProgressFinishCall {
	// Message is the message argument value.
	const tsc::DiagnosticMessage* Message;
	// Args is the args argument value.
	std::vector<std::string> Args;
};
struct ClientMockProgressStartCall {
	// Message is the message argument value.
	const tsc::DiagnosticMessage* Message;
	// Args is the args argument value.
	std::vector<std::string> Args;
};
struct ClientMockPublishDiagnosticsCall {
	// Ctx is the ctx argument value.
	gostd::Context Ctx;
	// Params is the params argument value.
	lsp::lsproto::PublishDiagnosticsParams* Params;
};
struct ClientMockRefreshCodeLensCall {
	// Ctx is the ctx argument value.
	gostd::Context Ctx;
};
struct ClientMockRefreshDiagnosticsCall {
	// Ctx is the ctx argument value.
	gostd::Context Ctx;
};
struct ClientMockRefreshInlayHintsCall {
	// Ctx is the ctx argument value.
	gostd::Context Ctx;
};
struct ClientMockRegisterContentMapperExtensionsCall {
	// Ctx is the ctx argument value.
	gostd::Context Ctx;
	// Extensions is the extensions argument value.
	std::vector<std::string> Extensions;
};
struct ClientMockSendTelemetryCall {
	// Ctx is the ctx argument value.
	gostd::Context Ctx;
	// Telemetry is the telemetry argument value.
	lsproto::TelemetryEvent Telemetry;
};
struct ClientMockSetLocaleCall {
	// LocaleMoqParam is the localeMoqParam argument value.
	std::string LocaleMoqParam;
};
struct ClientMockUnwatchFilesCall {
	// Ctx is the ctx argument value.
	gostd::Context Ctx;
	// ID is the id argument value.
	project::WatcherID ID;
};
struct ClientMockWatchFilesCall {
	// Ctx is the ctx argument value.
	gostd::Context Ctx;
	// ID is the id argument value.
	project::WatcherID ID;
	// Watchers is the watchers argument value.
	std::vector<lsp::lsproto::FileSystemWatcher*> Watchers;
};

// ClientMock — clientmock_generated.go:56. A mock implementation of
// project.Client.
//
//	mockedClient := &ClientMock{
//		GetLocaleFunc: func() locale.Locale {
//			panic("mock out the GetLocale method")
//		},
//	}
struct ClientMock final : project::Client {
	// Func fields mock the methods.
	std::function<locale::Locale()> GetLocaleFunc;
	std::function<bool()> IsActiveFunc;
	std::function<void(const tsc::DiagnosticMessage*,
	                   const std::vector<std::string>&)>
	    ProgressFinishFunc;
	std::function<void(const tsc::DiagnosticMessage*,
	                   const std::vector<std::string>&)>
	    ProgressStartFunc;
	std::function<gostd::Error(
	    const gostd::Context&,
	    lsp::lsproto::PublishDiagnosticsParams*)>
	    PublishDiagnosticsFunc;
	std::function<gostd::Error(const gostd::Context&)> RefreshCodeLensFunc;
	std::function<gostd::Error(const gostd::Context&)> RefreshDiagnosticsFunc;
	std::function<gostd::Error(const gostd::Context&)> RefreshInlayHintsFunc;
	std::function<gostd::Error(const gostd::Context&,
	                           const std::vector<std::string>&)>
	    RegisterContentMapperExtensionsFunc;
	std::function<gostd::Error(const gostd::Context&,
	                           lsproto::TelemetryEvent)>
	    SendTelemetryFunc;
	std::function<void(const std::string&)> SetLocaleFunc;
	std::function<gostd::Error(const gostd::Context&,
	    project::WatcherID)>
	    UnwatchFilesFunc;
	std::function<gostd::Error(
	    const gostd::Context&, project::WatcherID,
	    const std::vector<lsp::lsproto::FileSystemWatcher*>&)>
	    WatchFilesFunc;

	// calls tracks calls to the methods.
	struct Calls {
		std::vector<ClientMockGetLocaleCall> GetLocale;
		std::vector<ClientMockIsActiveCall> IsActive;
		std::vector<ClientMockProgressFinishCall> ProgressFinish;
		std::vector<ClientMockProgressStartCall> ProgressStart;
		std::vector<ClientMockPublishDiagnosticsCall> PublishDiagnostics;
		std::vector<ClientMockRefreshCodeLensCall> RefreshCodeLens;
		std::vector<ClientMockRefreshDiagnosticsCall> RefreshDiagnostics;
		std::vector<ClientMockRefreshInlayHintsCall> RefreshInlayHints;
		std::vector<ClientMockRegisterContentMapperExtensionsCall>
		    RegisterContentMapperExtensions;
		std::vector<ClientMockSendTelemetryCall> SendTelemetry;
		std::vector<ClientMockSetLocaleCall> SetLocale;
		std::vector<ClientMockUnwatchFilesCall> UnwatchFiles;
		std::vector<ClientMockWatchFilesCall> WatchFiles;
	};
	Calls calls;

	// Per-method locks — sync.RWMutexes.
	mutable std::shared_mutex lockGetLocale;
	mutable std::shared_mutex lockIsActive;
	mutable std::shared_mutex lockProgressFinish;
	mutable std::shared_mutex lockProgressStart;
	mutable std::shared_mutex lockPublishDiagnostics;
	mutable std::shared_mutex lockRefreshCodeLens;
	mutable std::shared_mutex lockRefreshDiagnostics;
	mutable std::shared_mutex lockRefreshInlayHints;
	mutable std::shared_mutex lockRegisterContentMapperExtensions;
	mutable std::shared_mutex lockSendTelemetry;
	mutable std::shared_mutex lockSetLocale;
	mutable std::shared_mutex lockUnwatchFiles;
	mutable std::shared_mutex lockWatchFiles;

	// GetLocale — clientmock_generated.go:205.
	locale::Locale GetLocale() override;
	// IsActive — clientmock_generated.go:230.
	bool IsActive() override;
	// ProgressFinish — clientmock_generated.go:295.
	void ProgressFinish(const tsc::DiagnosticMessage* message,
	                    const std::vector<std::string>& args) override;
	// ProgressStart — clientmock_generated.go:288ish.
	void ProgressStart(const tsc::DiagnosticMessage* message,
	                   const std::vector<std::string>& args) override;
	// PublishDiagnostics — clientmock_generated.go:327.
	gostd::Error PublishDiagnostics(
	    const gostd::Context& ctx,
	    lsp::lsproto::PublishDiagnosticsParams* params) override;
	// RefreshCodeLens — clientmock_generated.go:364.
	gostd::Error RefreshCodeLens(const gostd::Context& ctx) override;
	// RefreshDiagnostics — clientmock_generated.go:397.
	gostd::Error RefreshDiagnostics(const gostd::Context& ctx) override;
	// RefreshInlayHints — clientmock_generated.go:430.
	gostd::Error RefreshInlayHints(const gostd::Context& ctx) override;
	// RegisterContentMapperExtensions — clientmock_generated.go:463.
	gostd::Error RegisterContentMapperExtensions(
	    const gostd::Context& ctx,
	    const std::vector<std::string>& extensions) override;
	// SendTelemetry — clientmock_generated.go:500.
	gostd::Error SendTelemetry(const gostd::Context& ctx,
	                           lsproto::TelemetryEvent telemetry) override;
	// SetLocale — clientmock_generated.go:537.
	void SetLocale(const std::string& locale) override;
	// UnwatchFiles — clientmock_generated.go:569.
	gostd::Error UnwatchFiles(const gostd::Context& ctx,
	                          project::WatcherID id) override;
	// WatchFiles — clientmock_generated.go:606.
	gostd::Error WatchFiles(
	    const gostd::Context& ctx, project::WatcherID id,
	    const std::vector<lsp::lsproto::FileSystemWatcher*>& watchers)
	    override;

	// Call accessors.
	std::vector<ClientMockGetLocaleCall> GetLocaleCalls() const;
	std::vector<ClientMockIsActiveCall> IsActiveCalls() const;
	std::vector<ClientMockProgressFinishCall> ProgressFinishCalls() const;
	std::vector<ClientMockProgressStartCall> ProgressStartCalls() const;
	std::vector<ClientMockPublishDiagnosticsCall>
	PublishDiagnosticsCalls() const;
	std::vector<ClientMockRefreshCodeLensCall> RefreshCodeLensCalls() const;
	std::vector<ClientMockRefreshDiagnosticsCall>
	RefreshDiagnosticsCalls() const;
	std::vector<ClientMockRefreshInlayHintsCall>
	RefreshInlayHintsCalls() const;
	std::vector<ClientMockRegisterContentMapperExtensionsCall>
	RegisterContentMapperExtensionsCalls() const;
	std::vector<ClientMockSendTelemetryCall> SendTelemetryCalls() const;
	std::vector<ClientMockSetLocaleCall> SetLocaleCalls() const;
	std::vector<ClientMockUnwatchFilesCall> UnwatchFilesCalls() const;
	std::vector<ClientMockWatchFilesCall> WatchFilesCalls() const;
};

}  // namespace tsc::testutil::projecttestutil
