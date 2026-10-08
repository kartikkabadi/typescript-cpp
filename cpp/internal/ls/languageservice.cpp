// languageservice.go — the LanguageService type and its Host-facing plumbing.
//
// The member functions now have their canonical homes in api.cpp
// (ctor/accessors/views) and ls.cpp (sourcemap.Host impls); this file keeps
// only the Go-level `NewLanguageService` entry point, delegating to the
// class constructor in ls.h.
#include "internal/ls/ls.h"
#include "internal/compiler/program.h"

namespace tsc::ls {

// NewLanguageService — languageservice.go:25.
LanguageService* NewLanguageService(autoimport::ProjectID* projectID,
                                    compiler::SimpleProgram* program,
                                    Host* host,
                                    const std::string& activeFile) {
	return new LanguageService(projectID, program, host, activeFile);
}

} // namespace tsc::ls
