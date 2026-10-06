// languageservice.go — LanguageService construction. All other
// languageservice.go members are inline in ls.h or live in api.cpp.
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
