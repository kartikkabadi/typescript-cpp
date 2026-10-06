// clientmock_generated.cpp — port of tsc/internal/testutil/projecttestutil/
// clientmock_generated.go (moq-generated ClientMock). A nil Func field
// returns the method's zero values, matching moq's generated behavior.
#include "internal/testutil/projecttestutil/clientmock_generated.h"

#include <mutex>

namespace tsc::testutil::projecttestutil {

// GetLocale — clientmock_generated.go:205.
locale::Locale ClientMock::GetLocale() {
	{
		std::unique_lock lk(lockGetLocale);
		calls.GetLocale.emplace_back();
	}
	if (!GetLocaleFunc) {
		return {};
	}
	return GetLocaleFunc();
}

// GetLocaleCalls — clientmock_generated.go:221.
std::vector<ClientMockGetLocaleCall> ClientMock::GetLocaleCalls() const {
	std::shared_lock lk(lockGetLocale);
	return calls.GetLocale;
}

// IsActive — clientmock_generated.go:230.
bool ClientMock::IsActive() {
	{
		std::unique_lock lk(lockIsActive);
		calls.IsActive.emplace_back();
	}
	if (!IsActiveFunc) {
		return false;
	}
	return IsActiveFunc();
}

// IsActiveCalls — clientmock_generated.go:246.
std::vector<ClientMockIsActiveCall> ClientMock::IsActiveCalls() const {
	std::shared_lock lk(lockIsActive);
	return calls.IsActive;
}

// ProgressFinish — clientmock_generated.go:295.
void ClientMock::ProgressFinish(const tsc::DiagnosticMessage* message,
                                const std::vector<std::any>& args) {
	{
		std::unique_lock lk(lockProgressFinish);
		calls.ProgressFinish.push_back(ClientMockProgressFinishCall{
		    .Message = message, .Args = args});
	}
	if (!ProgressFinishFunc) {
		return;
	}
	ProgressFinishFunc(message, args);
}

// ProgressFinishCalls — clientmock_generated.go:317.
std::vector<ClientMockProgressFinishCall>
ClientMock::ProgressFinishCalls() const {
	std::shared_lock lk(lockProgressFinish);
	return calls.ProgressFinish;
}

// ProgressStart — clientmock_generated.go:288ish.
void ClientMock::ProgressStart(const tsc::DiagnosticMessage* message,
                               const std::vector<std::any>& args) {
	{
		std::unique_lock lk(lockProgressStart);
		calls.ProgressStart.push_back(ClientMockProgressStartCall{
		    .Message = message, .Args = args});
	}
	if (!ProgressStartFunc) {
		return;
	}
	ProgressStartFunc(message, args);
}

// ProgressStartCalls — clientmock_generated.go:317.
std::vector<ClientMockProgressStartCall>
ClientMock::ProgressStartCalls() const {
	std::shared_lock lk(lockProgressStart);
	return calls.ProgressStart;
}

// PublishDiagnostics — clientmock_generated.go:327.
gostd::Error ClientMock::PublishDiagnostics(
    gostd::Context ctx,
    const std::shared_ptr<lsproto::PublishDiagnosticsParams>& params) {
	{
		std::unique_lock lk(lockPublishDiagnostics);
		calls.PublishDiagnostics.push_back(ClientMockPublishDiagnosticsCall{
		    .Ctx = ctx, .Params = params});
	}
	if (!PublishDiagnosticsFunc) {
		return nullptr;
	}
	return PublishDiagnosticsFunc(ctx, params);
}

// PublishDiagnosticsCalls — clientmock_generated.go:349.
std::vector<ClientMockPublishDiagnosticsCall>
ClientMock::PublishDiagnosticsCalls() const {
	std::shared_lock lk(lockPublishDiagnostics);
	return calls.PublishDiagnostics;
}

// RefreshCodeLens — clientmock_generated.go:364.
gostd::Error ClientMock::RefreshCodeLens(gostd::Context ctx) {
	{
		std::unique_lock lk(lockRefreshCodeLens);
		calls.RefreshCodeLens.push_back(
		    ClientMockRefreshCodeLensCall{.Ctx = ctx});
	}
	if (!RefreshCodeLensFunc) {
		return nullptr;
	}
	return RefreshCodeLensFunc(ctx);
}

// RefreshCodeLensCalls — clientmock_generated.go:384.
std::vector<ClientMockRefreshCodeLensCall>
ClientMock::RefreshCodeLensCalls() const {
	std::shared_lock lk(lockRefreshCodeLens);
	return calls.RefreshCodeLens;
}

// RefreshDiagnostics — clientmock_generated.go:397.
gostd::Error ClientMock::RefreshDiagnostics(gostd::Context ctx) {
	{
		std::unique_lock lk(lockRefreshDiagnostics);
		calls.RefreshDiagnostics.push_back(
		    ClientMockRefreshDiagnosticsCall{.Ctx = ctx});
	}
	if (!RefreshDiagnosticsFunc) {
		return nullptr;
	}
	return RefreshDiagnosticsFunc(ctx);
}

// RefreshDiagnosticsCalls — clientmock_generated.go:417.
std::vector<ClientMockRefreshDiagnosticsCall>
ClientMock::RefreshDiagnosticsCalls() const {
	std::shared_lock lk(lockRefreshDiagnostics);
	return calls.RefreshDiagnostics;
}

// RefreshInlayHints — clientmock_generated.go:430.
gostd::Error ClientMock::RefreshInlayHints(gostd::Context ctx) {
	{
		std::unique_lock lk(lockRefreshInlayHints);
		calls.RefreshInlayHints.push_back(
		    ClientMockRefreshInlayHintsCall{.Ctx = ctx});
	}
	if (!RefreshInlayHintsFunc) {
		return nullptr;
	}
	return RefreshInlayHintsFunc(ctx);
}

// RefreshInlayHintsCalls — clientmock_generated.go:450.
std::vector<ClientMockRefreshInlayHintsCall>
ClientMock::RefreshInlayHintsCalls() const {
	std::shared_lock lk(lockRefreshInlayHints);
	return calls.RefreshInlayHints;
}

// RegisterContentMapperExtensions — clientmock_generated.go:463.
gostd::Error ClientMock::RegisterContentMapperExtensions(
    gostd::Context ctx, const std::vector<std::string>& extensions) {
	{
		std::unique_lock lk(lockRegisterContentMapperExtensions);
		calls.RegisterContentMapperExtensions.push_back(
		    ClientMockRegisterContentMapperExtensionsCall{
		        .Ctx = ctx, .Extensions = extensions});
	}
	if (!RegisterContentMapperExtensionsFunc) {
		return nullptr;
	}
	return RegisterContentMapperExtensionsFunc(ctx, extensions);
}

// RegisterContentMapperExtensionsCalls — clientmock_generated.go:485.
std::vector<ClientMockRegisterContentMapperExtensionsCall>
ClientMock::RegisterContentMapperExtensionsCalls() const {
	std::shared_lock lk(lockRegisterContentMapperExtensions);
	return calls.RegisterContentMapperExtensions;
}

// SendTelemetry — clientmock_generated.go:500.
gostd::Error ClientMock::SendTelemetry(
    gostd::Context ctx, const lsproto::TelemetryEvent& telemetry) {
	{
		std::unique_lock lk(lockSendTelemetry);
		calls.SendTelemetry.push_back(ClientMockSendTelemetryCall{
		    .Ctx = ctx, .Telemetry = telemetry});
	}
	if (!SendTelemetryFunc) {
		return nullptr;
	}
	return SendTelemetryFunc(ctx, telemetry);
}

// SendTelemetryCalls — clientmock_generated.go:522.
std::vector<ClientMockSendTelemetryCall> ClientMock::SendTelemetryCalls()
    const {
	std::shared_lock lk(lockSendTelemetry);
	return calls.SendTelemetry;
}

// SetLocale — clientmock_generated.go:537.
void ClientMock::SetLocale(const std::string& localeMoqParam) {
	{
		std::unique_lock lk(lockSetLocale);
		calls.SetLocale.push_back(
		    ClientMockSetLocaleCall{.LocaleMoqParam = localeMoqParam});
	}
	if (!SetLocaleFunc) {
		return;
	}
	SetLocaleFunc(localeMoqParam);
}

// SetLocaleCalls — clientmock_generated.go:556.
std::vector<ClientMockSetLocaleCall> ClientMock::SetLocaleCalls() const {
	std::shared_lock lk(lockSetLocale);
	return calls.SetLocale;
}

// UnwatchFiles — clientmock_generated.go:569.
gostd::Error ClientMock::UnwatchFiles(gostd::Context ctx,
                                      project::WatcherID id) {
	{
		std::unique_lock lk(lockUnwatchFiles);
		calls.UnwatchFiles.push_back(
		    ClientMockUnwatchFilesCall{.Ctx = ctx, .ID = id});
	}
	if (!UnwatchFilesFunc) {
		return nullptr;
	}
	return UnwatchFilesFunc(ctx, id);
}

// UnwatchFilesCalls — clientmock_generated.go:591.
std::vector<ClientMockUnwatchFilesCall> ClientMock::UnwatchFilesCalls()
    const {
	std::shared_lock lk(lockUnwatchFiles);
	return calls.UnwatchFiles;
}

// WatchFiles — clientmock_generated.go:606.
gostd::Error ClientMock::WatchFiles(
    gostd::Context ctx, project::WatcherID id,
    const std::vector<std::shared_ptr<lsproto::FileSystemWatcher>>&
        watchers) {
	{
		std::unique_lock lk(lockWatchFiles);
		calls.WatchFiles.push_back(ClientMockWatchFilesCall{
		    .Ctx = ctx, .ID = id, .Watchers = watchers});
	}
	if (!WatchFilesFunc) {
		return nullptr;
	}
	return WatchFilesFunc(ctx, id, watchers);
}

// WatchFilesCalls — clientmock_generated.go:630.
std::vector<ClientMockWatchFilesCall> ClientMock::WatchFilesCalls() const {
	std::shared_lock lk(lockWatchFiles);
	return calls.WatchFiles;
}

}  // namespace tsc::testutil::projecttestutil
