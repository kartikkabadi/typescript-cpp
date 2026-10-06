// Port of tsc/internal/ls api.go + languageservice.go + host.go +
// constants.go — LanguageService plumbing.
// === slice: ls-coreA ===

#include "internal/ls/ls.h"

#include "internal/astnav/tokens.h"
#include "internal/vfs/vfsmatch/vfsmatch.h"

namespace tsc::ls {

// --- api.go:12-15 ---

const gostd::Error ErrNoSourceFile = gostd::newError("source file not found");
const gostd::Error ErrNoTokenAtPosition =
    gostd::newError("no token found at position");
// completions.go:35 — declared here with the other sentinels.
const gostd::Error ErrNeedsAutoImports =
    gostd::newError("auto-imports are not ready");

// --- api.go:18 GetSymbolAtPosition ---

std::pair<Symbol*, gostd::Error> LanguageService::GetSymbolAtPosition(
    const ContextPtr& ctx, const std::string& fileName, int position) {
	auto [program, file] = tryGetProgramAndFile(fileName);
	if (file == nullptr) {
		return {nullptr,
		        gostd::errorf("%w: %s", {ErrNoSourceFile, fileName})};
	}
	Node* node = astnav::getTokenAtPosition(file, position);
	if (node == nullptr) {
		return {nullptr, gostd::errorf("%w: %s:%d",
		                               {ErrNoTokenAtPosition, fileName,
		                                position})};
	}
	auto [checker, done] = program->GetTypeCheckerForFile(ctx, file);
	auto* symbol = checker->GetSymbolAtLocation(node);
	done(); // defer done()
	return {symbol, nullptr};
}

// --- api.go:31 GetSymbolAtLocation ---

Symbol* LanguageService::GetSymbolAtLocation(const ContextPtr& ctx,
                                             Node* node) {
	compiler::SimpleProgram* program = GetProgram();
	auto [checker, done] = program->GetTypeCheckerForFile(
	    ctx, getSourceFileOfNode(node));
	auto* symbol = checker->GetSymbolAtLocation(node);
	done(); // defer done()
	return symbol;
}

// --- api.go:38 GetTypeOfSymbol ---

checker::Type* LanguageService::GetTypeOfSymbol(const ContextPtr& ctx,
                                                Symbol* symbol) {
	compiler::SimpleProgram* program = GetProgram();
	auto [checker, done] = program->GetTypeChecker(ctx);
	auto* type = checker->GetTypeOfSymbolAtLocation(symbol, nullptr);
	done(); // defer done()
	return type;
}

// --- languageservice.go ---

// languageservice.go:25 NewLanguageService.
LanguageService::LanguageService(autoimport::ProjectID* projectID,
                                 compiler::SimpleProgram* program,
                                 ls::Host* host,
                                 const std::string& activeFile)
    : projectID_(projectID), host(host), program(program),
      converters(host->Converters()),
      activeConfig(host->GetPreferences(activeFile)) {}

// languageservice.go:41 toPath.
tspath::Path LanguageService::toPath(const std::string& fileName) {
	return tspath::toPath(fileName, program->GetCurrentDirectory(),
	                      UseCaseSensitiveFileNames());
}

// languageservice.go:57 tryGetProgramAndFile.
std::pair<compiler::SimpleProgram*, SourceFile*>
LanguageService::tryGetProgramAndFile(const std::string& fileName) {
	compiler::SimpleProgram* program = GetProgram();
	SourceFile* file = program->GetSourceFile(fileName);
	return {program, file};
}

// languageservice.go:63 getProgramAndFile.
std::pair<compiler::SimpleProgram*, SourceFile*>
LanguageService::getProgramAndFile(const lsproto::DocumentUri& documentURI) {
	std::string fileName = documentURI.FileName();
	auto [program, file] = tryGetProgramAndFile(fileName);
	if (file == nullptr) {
		TSC_UNREACHABLE(("file not found: " + fileName).c_str());
	}
	return {program, file};
}

// languageservice.go:72 GetDocumentPositionMapper.
sourcemap::DocumentPositionMapper* LanguageService::GetDocumentPositionMapper(
    const std::string& fileName) {
	auto it = documentPositionMappers.find(fileName);
	if (it == documentPositionMappers.end()) {
		auto* d = sourcemap::GetDocumentPositionMapper(this, fileName);
		documentPositionMappers[fileName] = d;
		return d;
	}
	return it->second;
}

// languageservice.go:97 getPreparedAutoImportView.
std::pair<autoimport::View*, gostd::Error>
LanguageService::getPreparedAutoImportView(SourceFile* fromFile,
                                           checker::Checker* typeChecker) {
	autoimport::Registry* registry = host->AutoImportRegistry();
	SourceFile* registryFile = fromFile;
	if (SourceFile* canonical = fromFile->CanonicalSourceFile();
	    canonical != nullptr) {
		registryFile = canonical;
	}
	if (!registry->IsPreparedForImportingFile(registryFile->FileName(),
	                                          projectID_, UserPreferences())) {
		return {nullptr, ErrNeedsAutoImports};
	}

	autoimport::View* view = autoimport::NewView(
	    registry, fromFile, projectID_, program, typeChecker,
	    UserPreferences().ModuleSpecifierPreferences());
	return {view, nullptr};
}

// languageservice.go:115 getCurrentAutoImportView.
autoimport::View* LanguageService::getCurrentAutoImportView(
    SourceFile* fromFile, checker::Checker* typeChecker) {
	return autoimport::NewView(host->AutoImportRegistry(), fromFile,
	                           projectID_, program, typeChecker,
	                           UserPreferences().ModuleSpecifierPreferences());
}

// languageservice.go:128 DirectoryExists — used for module specifier
// completions.
bool LanguageService::DirectoryExists(const std::string& path) {
	return host->DirectoryExists(path);
}

// languageservice.go:133 ReadDirectory — used for module specifier
// completions.
std::vector<std::string> LanguageService::ReadDirectory(
    const std::string& path, const std::vector<std::string>& extensions,
    const std::vector<std::string>& includes) {
	return host->ReadDirectory(program->GetCurrentDirectory(), path, extensions,
	                           nullptr /*excludes*/, includes,
	                           vfs::vfsmatch::UnlimitedDepth);
}

// languageservice.go:138 GetDirectories.
std::vector<std::string> LanguageService::GetDirectories(
    const std::string& path) {
	return host->GetDirectories(path);
}

} // namespace tsc::ls
