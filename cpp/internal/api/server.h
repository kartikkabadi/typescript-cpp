// StdioServer — server.go.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "internal/contentmapper/contentmapper.h"
#include "internal/gostd/gostd.h"

namespace tsc::api {

// StdioServerOptions configures the STDIO-based API server — server.go:14.
struct StdioServerOptions {
	std::shared_ptr<gostd::io::ReadCloser> In;
	std::shared_ptr<gostd::io::WriteCloser> Out;
	gostd::io::Writer* Err = nullptr;
	std::string Cwd;
	std::string DefaultLibraryPath;
	// PipePath, if set, listens on a named pipe (Windows) or Unix domain
	// socket instead of using In/Out for communication.
	std::string PipePath;
	// Callbacks specifies which filesystem operations should be delegated
	// to the client (e.g., "readFile", "fileExists"). Empty means no callbacks.
	std::vector<std::string> Callbacks;
	// UseCaseSensitiveFileNames overrides the base filesystem's case
	// sensitivity when set.
	std::optional<bool> UseCaseSensitiveFileNames;
	// Async enables JSON-RPC protocol with async connection handling.
	// When false (default), uses MessagePack protocol with sync connection.
	bool Async = false;
	// CollectTiming enables per-request server processing-time measurement.
	// When enabled, the server accumulates each request's processing time into
	// running totals and a recent-request ring buffer. Response messages are
	// left unchanged; the client folds this data into its own timing snapshot
	// on demand via getServerTiming / resetServerTiming requests.
	bool CollectTiming = false;
	// RunExternalCode allows configured content mappers to execute.
	bool RunExternalCode = false;
	std::shared_ptr<contentmapper::Spawner> ContentMapperSpawner;
};

// StdioServer runs an API session over STDIO using MessagePack protocol.
// This is the entry point for the synchronous STDIO-based API used by
// native TypeScript tooling integration — server.go:38.
struct StdioServer {
	explicit StdioServer(StdioServerOptions* options) : options(options) {}

	// Run starts the server and blocks until the connection closes.
	gostd::Error Run(gostd::Context ctx);

	StdioServerOptions* options;
};

// NewStdioServer creates a new STDIO-based API server — server.go:44.
StdioServer* NewStdioServer(StdioServerOptions* options);

gostd::Error serverRunError(gostd::Context ctx, gostd::Error err);

} // namespace tsc::api
