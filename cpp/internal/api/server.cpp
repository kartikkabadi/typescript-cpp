// StdioServer / serverRunError — server.go.
#include "internal/api/server.h"

#include "internal/api/callbackfs.h"
#include "internal/api/protocol_msgpack.h"
#include "internal/api/session.h"
#include "internal/bundled/bundled.h"
#include "internal/ipc/ipc.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/project/project.h"
#include "internal/vfs/osvfs/osvfs.h"

#include <stdexcept>

namespace tsc::api {

// NewStdioServer — server.go:44.
StdioServer* NewStdioServer(StdioServerOptions* options) {
	if (options->Cwd.empty()) {
		throw std::runtime_error("StdioServerOptions.Cwd is required");
	}
	return new StdioServer(options);
}

// Run — server.go:54.
gostd::Error StdioServer::Run(gostd::Context ctx) {
	std::shared_ptr<ipc::Transport> transport;
	if (!options->PipePath.empty()) {
		auto t = ipc::NewPipeTransport(options->PipePath);
		if (t.second) {
			return gostd::errorf("failed to create pipe transport: %w",
			                     {t.second});
		}
		transport = std::move(t.first);
	} else {
		transport = ipc::NewStdioTransport(options->In, options->Out);
	}
	auto transportGuard =
	    std::unique_ptr<ipc::Transport, std::function<void(ipc::Transport*)>>(
	        transport.get(), [](ipc::Transport* t) { (void)t->Close(); });

	// osvfs::FS() is a singleton — non-owning alias.
	std::shared_ptr<vfs::FS> fs = bundled::WrapFS(
	    std::shared_ptr<vfs::FS>(vfs::osvfs::FS(), [](vfs::FS*) {}));

	// Wrap the base FS with callbackFS if callbacks are requested
	std::shared_ptr<callbackFS> callbackFs;
	if (!options->Callbacks.empty()) {
		callbackFs = newCallbackFS(fs, options->Callbacks);
		fs = callbackFs;
	}

	project::SessionInit sessionInit;
	sessionInit.BackgroundCtx = ctx;
	// Logger: nil — TODO: Add logging support
	sessionInit.FS = fs;
	sessionInit.Options = new project::SessionOptions{
	    .CurrentDirectory = options->Cwd,
	    .DefaultLibraryPath = options->DefaultLibraryPath,
	    .PositionEncoding = lsproto::PositionEncodingKindUTF8,
	    .WatchEnabled = false,
	    .LoggingEnabled = false,
	    .RunExternalCode = options->RunExternalCode,
	};
	sessionInit.Spawner = options->ContentMapperSpawner.get();

	SessionOptions sessionOpts{
	    // Only msgpack uses binary responses
	    .UseBinaryResponses = !options->Async,
	};
	auto session = NewStandaloneSession(&sessionInit, &sessionOpts);
	auto sessionGuard =
	    std::unique_ptr<Session, std::function<void(Session*)>>(
	        session.get(), [](Session* s) { s->Close(); });

	// Accept connection from transport
	auto acceptResult = transport->Accept();
	if (acceptResult.second) {
		return gostd::errorf("failed to accept connection: %w",
		                     {acceptResult.second});
	}
	auto rwc = acceptResult.first;

	// Create protocol and connection based on async mode
	std::shared_ptr<ipc::Conn> conn;
	if (options->Async) {
		auto protocol = ipc::NewJSONRPCProtocol(rwc);
		auto asyncConn = std::static_pointer_cast<ipc::AsyncConn>(
		    ipc::NewAsyncConnWithProtocol(
		        rwc, protocol,
		        std::static_pointer_cast<ipc::Handler>(session)));
		asyncConn->SetCollectTiming(options->CollectTiming);
		conn = asyncConn;
	} else {
		auto protocol = NewMessagePackProtocol(rwc);
		auto syncConn = ipc::NewSyncConn(
		    rwc, protocol,
		    std::static_pointer_cast<ipc::Handler>(session));
		syncConn->SetCollectTiming(options->CollectTiming);
		conn = syncConn;
	}

	// If callbacks are enabled, set the connection on the FS
	if (callbackFs) {
		callbackFs->SetConnection(ctx, conn);
	}
	session->SetConnection(conn);

	return serverRunError(ctx, conn->Run(ctx));
}

// serverRunError — server.go:135.
gostd::Error serverRunError(gostd::Context ctx, gostd::Error err) {
	if (gostd::ctxErr(ctx) != nullptr) {
		return nullptr;
	}
	return err;
}

} // namespace tsc::api
