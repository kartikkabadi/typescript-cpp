// languageservice.go — the LanguageService type and its Host-facing plumbing.
#include "internal/ls/ls.h"
#include "internal/compiler/program.h"

#include "internal/vfs/vfsmatch/vfsmatch.h"

namespace tsc::ls {

// NewLanguageService — languageservice.go:25.
LanguageService* NewLanguageService(autoimport::ProjectID projectID,
                                    compiler::SimpleProgram* program,
                                    Host* host,
                                    const std::string& activeFile) {
	auto* l = new LanguageService();
	l->projectID = projectID;
	l->host = host;
	l->program = program;
	l->converters = host->Converters();
	l->activeConfig = host->GetPreferences(activeFile);
	l->documentPositionMappers = {};
	return l;
}

// toPath — languageservice.go:41.
tspath::Path LanguageService::toPath(const std::string& fileName) {
	return tspath::toPath(fileName, program->GetCurrentDirectory(),
	                      UseCaseSensitiveFileNames());
}

// GetProgram — languageservice.go:45.
compiler::SimpleProgram* LanguageService::GetProgram() {
	return program;
}

// UserPreferences — languageservice.go:49.
lsutil::UserPreferences LanguageService::UserPreferences() {
	return activeConfig;
}

// FormatOptions — languageservice.go:53.
lsutil::FormatCodeSettings LanguageService::FormatOptions() {
	return activeConfig.FormatCodeSettings;
}

// tryGetProgramAndFile — languageservice.go:57.
std::pair<compiler::SimpleProgram*, SourceFile*>
LanguageService::tryGetProgramAndFile(const std::string& fileName) {
	auto* program = GetProgram();
	auto* file = program->GetSourceFile(fileName);
	return {program, file};
}

// getProgramAndFile — languageservice.go:63.
std::pair<compiler::SimpleProgram*, SourceFile*>
LanguageService::getProgramAndFile(lsproto::DocumentUri documentURI) {
	auto fileName = documentURI.FileName();
	auto [program, file] = tryGetProgramAndFile(fileName);
	if (file == nullptr) {
		TSC_UNREACHABLE(("file not found: " + fileName).c_str());
	}
	return {program, file};
}

// GetDocumentPositionMapper — languageservice.go:72.
sourcemap::DocumentPositionMapper* LanguageService::GetDocumentPositionMapper(
    const std::string& fileName) {
	auto it = documentPositionMappers.find(fileName);
	sourcemap::DocumentPositionMapper* d;
	if (it == documentPositionMappers.end()) {
		d = sourcemap::GetDocumentPositionMapper(this, fileName);
		documentPositionMappers[fileName] = d;
	} else {
		d = it->second;
	}
	return d;
}

// ReadFile — languageservice.go:81.
std::pair<std::string, bool> LanguageService::ReadFile(
    std::string_view fileName) {
	return host->ReadFile(std::string(fileName));
}

// UseCaseSensitiveFileNames — languageservice.go:85.
bool LanguageService::UseCaseSensitiveFileNames() {
	return host->UseCaseSensitiveFileNames();
}

// GetECMALineInfo — languageservice.go:89.
sourcemap::ECMALineInfo* LanguageService::GetECMALineInfo(
    std::string_view fileName) {
	return host->GetECMALineInfo(std::string(fileName));
}

// getPreparedAutoImportView — languageservice.go:95. Returns an auto-import
// view for the given file if the registry is prepared to provide up-to-date
// auto-imports for it. If not, it returns ErrNeedsAutoImports.
std::pair<autoimport::View*, gostd::Error>
LanguageService::getPreparedAutoImportView(
    SourceFile* fromFile, checker::Checker* typeChecker) {
	auto* registry = host->AutoImportRegistry();
	auto* registryFile = fromFile;
	if (auto* canonical = fromFile->CanonicalSourceFile(); canonical != nullptr) {
		registryFile = canonical;
	}
	if (!registry->IsPreparedForImportingFile(registryFile->FileName(),
	                                          projectID, UserPreferences())) {
		return {nullptr, ErrNeedsAutoImports};
	}

	auto* view = autoimport::NewView(registry, fromFile, projectID, program,
	                                 typeChecker,
	                                 UserPreferences().ModuleSpecifierPreferences());
	return {view, nullptr};
}

// getCurrentAutoImportView — languageservice.go:111. Returns an auto-import
// view for the given file, based on the current state of the auto-import
// registry, which may or may not be up-to-date.
autoimport::View* LanguageService::getCurrentAutoImportView(
    SourceFile* fromFile, checker::Checker* typeChecker) {
	return autoimport::NewView(host->AutoImportRegistry(), fromFile, projectID,
	                           program, typeChecker,
	                           UserPreferences().ModuleSpecifierPreferences());
}

// DirectoryExists — languageservice.go:123. Used for module specifier
// completions.
bool LanguageService::DirectoryExists(const std::string& path) {
	return host->DirectoryExists(path);
}

// ReadDirectory — languageservice.go:128. Used for module specifier
// completions.
std::vector<std::string> LanguageService::ReadDirectory(
    const std::string& path, const std::vector<std::string>& extensions,
    const std::vector<std::string>& includes) {
	return host->ReadDirectory(program->GetCurrentDirectory(), path,
	                           extensions, {} /*excludes*/, includes,
	                           vfs::vfsmatch::UnlimitedDepth);
}

// GetDirectories — languageservice.go:132.
std::vector<std::string> LanguageService::GetDirectories(
    const std::string& path) {
	return host->GetDirectories(path);
}

} // namespace tsc::ls
