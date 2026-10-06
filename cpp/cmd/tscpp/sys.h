// tsc/cmd/tsc — shared declarations for the tscpp driver's tsc-mode
// support files (sys.cpp, lsp.cpp, api.cpp).
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "internal/execute/tsc/compile.h"
#include "internal/gostd/gostd.h"

// newSystem — sys.go:124 (os-backed System; defined in sys.cpp).
tsc::execute::tsc::System* newSystem();

// spawnProcess — sys.go:74 (sys.cpp). Launches a process, returning its
// stdio as an io.ReadWriteCloser (Read is its stdout, Write is its stdin).
// Used by osSys::Spawn and by --lsp's ServerOptions.Spawn (lsp.go:64).
std::pair<std::shared_ptr<tsc::gostd::io::ReadWriteCloser>, tsc::gostd::Error>
spawnProcess(const std::vector<std::string>& command, const std::string& dir,
             tsc::gostd::io::Writer* stderr);

// runLSP — lsp.go:20 (lsp.cpp). `tsc --lsp` subcommand.
int runLSP(const std::vector<std::string>& args);

// runAPI — api.go:41 (api.cpp). `tsc --api` subcommand.
int runAPI(const std::vector<std::string>& args);
