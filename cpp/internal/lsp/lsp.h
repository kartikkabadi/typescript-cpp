// === dep decls — owned by lsp ===
// Decls the testutil-leaves slice needs from tsc/internal/lsp (server.go):
// the Reader/Writer message interfaces, ServerOptions, and the Server
// handle. Interface + data-only struct shapes are ported faithfully;
// ToReader/ToWriter/NewServer/Server methods are stubbed in lsp.cpp with
// TSC_UNREACHABLE. The lsp slice should replace this file when it lands.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "internal/contentmapper/contentmapper.h"
#include "internal/gostd/gostd.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/vfs/vfs.h"

namespace tsc {
struct CompilerOptions;
namespace project { struct ParseCache; }
}  // namespace tsc

namespace tsc::lsp {

// Reader — server.go:36.
struct Reader {
	virtual ~Reader() = default;
	virtual std::pair<std::shared_ptr<lsproto::Message>, gostd::Error>
	Read() = 0;
};

// Writer — server.go:40.
struct Writer {
	virtual ~Writer() = default;
	virtual gostd::Error
	Write(const std::shared_ptr<lsproto::Message>& msg) = 0;
};

// ServerOptions — server.go:116.
struct ServerOptions {
	std::shared_ptr<Reader> In;
	std::shared_ptr<Writer> Out;
	gostd::io::Writer* Err = nullptr;
	std::string Cwd;
	std::shared_ptr<vfs::FS> FS;
	std::string DefaultLibraryPath;
	std::string TypingsLocation;
	std::shared_ptr<project::ParseCache> ParseCache;
	std::function<std::pair<std::vector<uint8_t>, gostd::Error>(
	    const std::string& cwd, const std::vector<std::string>& args)>
	    NpmInstall;
	// Spawn launches a child process, returning its stdio as an io.ReadWriteCloser (Read is its stdout,
	// Write is its stdin). It is nil when the host cannot spawn processes. Currently used for content mappers.
	contentmapper::SpawnerFuncFn Spawn;
	gostd::Duration ProgressDelay{};
	std::function<void(int pid)> SetParentProcessID;
};

// Server — server.go:62 (interface shape; owned by lsp).
struct Server {
	virtual ~Server() = default;
	virtual gostd::Error Run(gostd::Context ctx) = 0;
	virtual void SetCompilerOptionsForInferredProjects(
	    gostd::Context ctx, CompilerOptions* options) = 0;
};

// ToReader — server.go:152. Dep-stub.
std::shared_ptr<Reader> ToReader(gostd::io::Reader* r);
// ToWriter — server.go:162. Dep-stub.
std::shared_ptr<Writer> ToWriter(gostd::io::Writer* w);
// NewServer — server.go:76. Dep-stub.
std::shared_ptr<Server> NewServer(ServerOptions* opts);

}  // namespace tsc::lsp
