// --- host.go — program slice ---
// host.go:35 compilerHost — all file access goes through `fs` (e.g.
// bundled.WrapFS(osvfs.FS())), exactly like Go; the vfs layer decodes
// BOM/UTF-16 (vfs/internal/internal.go).
#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/parser/parser.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"
#include "internal/vfs/cachedvfs/cachedvfs.h"

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

// host.go: GetSourceFile — ReadFile + ParseSourceFile. metaData is tracked
// in the program's sourceFileMetaDatas, not by the host — forwards to the
// virtual 1-arg overload so watchCompilerHost's cache intercepts fileLoader
// loads too (Go's CompilerHost iface has only the 1-arg method).
SourceFile* CompilerHost::GetSourceFile(const SourceFileParseOptions& opts,
                                      SourceFileMetaData metaData) {
	return GetSourceFile(opts);
}

// === slice: execute-tsc ===

// host.go:45 NewCachedFSCompilerHost — wraps fs in cachedvfs.From.
CompilerHost* NewCachedFSCompilerHost(
    std::string currentDirectory, const std::shared_ptr<vfs::FS>& fs,
    std::string defaultLibraryPath,
    tsoptions::ExtendedConfigCache* extendedConfigCache,
    std::function<void(const DiagnosticMessage*,
                       const std::vector<std::string>&)>
        trace,
    const std::shared_ptr<contentmapper::Project>& contentMapperProject) {
	auto cached = vfs::cachedvfs::From(fs.get());
	return NewCompilerHost(std::move(currentDirectory),
	                       std::move(cached),
	                       std::move(defaultLibraryPath), extendedConfigCache,
	                       std::move(trace), contentMapperProject);
}

// host.go:57 NewCompilerHost.
CompilerHost* NewCompilerHost(
    std::string currentDirectory, const std::shared_ptr<vfs::FS>& fs,
    std::string defaultLibraryPath,
    tsoptions::ExtendedConfigCache* extendedConfigCache,
    std::function<void(const DiagnosticMessage*,
                       const std::vector<std::string>&)>
        trace,
    const std::shared_ptr<contentmapper::Project>& contentMapperProject) {
	if (trace == nullptr) {
		trace = [](const DiagnosticMessage*,
		           const std::vector<std::string>&) {};
	}
	auto* h = new CompilerHost();
	h->currentDirectory = std::move(currentDirectory);
	h->fs = fs;
	h->defaultLibraryPath = std::move(defaultLibraryPath);
	h->extendedConfigCache = extendedConfigCache;
	h->trace = std::move(trace);
	h->contentMapperProject = contentMapperProject;
	return h;
}

// host.go Trace.
void CompilerHost::Trace(const DiagnosticMessage* msg,
                         const std::vector<std::string>& args) {
	trace(msg, args);
}

// host.go GetSourceFile — ReadFile + ParseSourceFile (single-argument form).
SourceFile* CompilerHost::GetSourceFile(const SourceFileParseOptions& opts) {
	auto text = ReadFile(opts.FileName);
	if (!text) {
		return nullptr;
	}
	return parseSourceFile(opts, *text,
	                       ensureScriptKindFromFileName(opts.FileName));
}

// host.go GetContentMappedSourceFiles.
std::pair<contentmapper::SourceFiles, gostd::Error>
CompilerHost::GetContentMappedSourceFiles(
    const SourceFileParseOptions& parseOptions, contentmapper::Mapper* mapper) {
	if (contentMapperProject == nullptr) {
		return {contentmapper::SourceFiles{},
		        contentmapper::ErrProjectUnavailable};
	}
	auto content = ReadFile(parseOptions.FileName);
	if (!content) {
		return {contentmapper::SourceFiles{}, nullptr};
	}
	auto [files, err] = contentmapper::TransformAndParse(
	    parseOptions, *content, mapper, contentMapperProject);
	if (!err) {
		err = contentmapper::CheckSupplementalFileNameCollisions(
		    files, [this](std::string_view name) { return FileExists(name); });
	}
	return {files, err};
}

// host.go GetResolvedProjectReference.
tsoptions::ParsedCommandLine* CompilerHost::GetResolvedProjectReference(
    const std::string& fileName, const tspath::Path& path) {
	auto [commandLine, _] = tsoptions::GetParsedCommandLineOfConfigFilePath(
	    fileName, path, nullptr, /*optionsRaw*/ tsoptions::JsonObjectPtr{}, this,
	    extendedConfigCache);
	return commandLine;
}

// === end slice: execute-tsc ===

}  // namespace tsc::compiler
