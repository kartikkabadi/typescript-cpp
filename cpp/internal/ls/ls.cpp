// === dep decls — owned by ls ===
// Stubs for ls free functions; see ls.h.
#include "internal/ls/ls.h"

namespace tsc::ls {

// ErrNeedsAutoImports (completions.go:35).
const gostd::Error ErrNeedsAutoImports =
	gostd::newError("completion list needs auto imports");

LanguageService* NewLanguageService(const std::string& /*projectID*/,
                                    compiler::SimpleProgram* /*program*/,
                                    Host* /*host*/,
                                    const std::string& /*activeFile*/) {
	TSC_UNREACHABLE("ls::NewLanguageService — owned by ls slice");
}

std::string GetSymbolDocumentationComment(checker::Checker* /*c*/,
                                          Symbol* /*symbol*/) {
	TSC_UNREACHABLE("ls::GetSymbolDocumentationComment — owned by ls slice");
}

std::vector<JSDocTagInfo> GetSymbolJSDocTags(Symbol* /*symbol*/) {
	TSC_UNREACHABLE("ls::GetSymbolJSDocTags — owned by ls slice");
}

} // namespace tsc::ls
