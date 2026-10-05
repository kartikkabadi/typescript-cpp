// --- host.go — program slice ---
// host.go:35 compilerHost — all file access goes through `fs` (e.g.
// bundled.WrapFS(osvfs.FS())), exactly like Go; the vfs layer decodes
// BOM/UTF-16 (vfs/internal/internal.go).
#include "internal/compiler/program.h"
#include "internal/parser/parser.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::compiler {

bool CompilerHost::FileExists(std::string_view fileName) {
	return fs->FileExists(std::string(fileName));
}

bool CompilerHost::DirectoryExists(std::string_view directory) {
	return fs->DirectoryExists(std::string(directory));
}

std::optional<std::string> CompilerHost::ReadFile(std::string_view fileName) {
	auto [text, ok] = fs->ReadFile(std::string(fileName));
	if (!ok)
		return std::nullopt;
	return text;
}

// vfs WriteFile. Go returns error; std::nullopt == nil.
std::optional<std::string> CompilerHost::WriteFile(std::string_view fileName,
                                                   std::string_view text) {
	if (auto err = fs->WriteFile(std::string(fileName), std::string(text))) {
		return err.str();
	}
	return std::nullopt;
}

std::string CompilerHost::Realpath(std::string_view path) {
	return fs->Realpath(std::string(path));
}

module::ResolutionHost::AccessibleEntries CompilerHost::GetAccessibleEntries(
    std::string_view directory) {
	auto entries = fs->GetAccessibleEntries(std::string(directory));
	AccessibleEntries result;
	result.files = std::move(entries.files);
	result.directories = std::move(entries.directories);
	result.symlinks = std::move(entries.symlinks);
	return result;
}

// host.go: GetSourceFile — ReadFile + ParseSourceFile.
SourceFile* CompilerHost::GetSourceFile(const SourceFileParseOptions& opts,
                                      SourceFileMetaData metaData) {
	auto text = ReadFile(opts.FileName);
	if (!text)
		return nullptr;
	SourceFile* file = parseSourceFile(
	    opts, *text, ensureScriptKindFromFileName(opts.FileName));
	(void)metaData; // metaData is tracked in the program's sourceFileMetaDatas
	return file;
}

// host.go:100 GetContentMappedSourceFiles.
std::pair<contentmapper::SourceFiles, gostd::Error>
CompilerHost::GetContentMappedSourceFiles(
    const SourceFileParseOptions& parseOptions, contentmapper::Mapper* mapper) {
	if (contentMapperProject == nullptr) {
		return {contentmapper::SourceFiles{},
		        contentmapper::ErrProjectUnavailable};
	}
	auto content = fs->ReadFile(parseOptions.FileName);
	if (!content.second) {
		return {contentmapper::SourceFiles{}, nullptr};
	}
	auto [files, err] = contentmapper::TransformAndParse(
	    parseOptions, content.first, mapper, contentMapperProject);
	if (err == nullptr) {
		err = contentmapper::CheckSupplementalFileNameCollisions(
		    files, [this](std::string_view f) {
			    return fs->FileExists(std::string(f));
		    });
	}
	return {files, err};
}

// host.go:119 GetResolvedProjectReference.
tsoptions::ParsedCommandLine* CompilerHost::GetResolvedProjectReference(
    const std::string& fileName, const tspath::Path& path) {
	auto [commandLine, _] = tsoptions::GetParsedCommandLineOfConfigFilePath(
	    fileName, path, nullptr, tsoptions::JsonObjectPtr{}, this,
	    extendedConfigCache);
	return commandLine;
}

}  // namespace tsc::compiler
