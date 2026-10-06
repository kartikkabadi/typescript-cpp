// === dep decls — owned by the ls-coreB slice (findallreferences.go) ===
// Called by the api slice; stubbed until ls-coreB lands.
#include "internal/ls/ls.h"

namespace tsc::ls {

std::vector<SignatureUsage> LanguageService::GetSignatureUsages(
    const ContextPtr& /*ctx*/, Node* /*signatureDecl*/) {
	TSC_UNREACHABLE("ls::GetSignatureUsages — owned by ls-coreB slice");
}

std::vector<SymbolAndEntries*> LanguageService::GetReferencedSymbolsForNode(
    const ContextPtr& /*ctx*/, int /*position*/, Node* /*node*/,
    const std::vector<SourceFile*>& /*sourceFiles*/) {
	TSC_UNREACHABLE("ls::GetReferencedSymbolsForNode — owned by ls-coreB slice");
}

} // namespace tsc::ls
