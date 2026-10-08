// compilerhost.cpp — port of tsc/internal/execute/build/compilerHost.go.
#include "internal/execute/build/build.h"

namespace tsc::execute::build {

// compilerHost.go:21 FS — h.host.FS(); C++ CompilerHost::fs field is set to
// the inner host's FS at construction (buildtask.cpp).
// compilerHost.go:25 DefaultLibraryPath / :29 GetCurrentDirectory — inherited
// CompilerHost fields mirror the inner host's.

// compilerHost.go:33 Trace — inline in build.h.

// compilerHost.go:37 GetSourceFile.
SourceFile* compilerHost::GetSourceFile(const SourceFileParseOptions& opts,
                                        SourceFileMetaData metaData) {
	return host_->GetSourceFile(opts);
}

// compilerHost.go:41 GetContentMappedSourceFiles.
std::pair<contentmapper::SourceFiles, gostd::Error>
compilerHost::GetContentMappedSourceFiles(
    const SourceFileParseOptions& parseOptions, contentmapper::Mapper* mapper) {
	if (contentMapperProject == nullptr) {
		return {contentmapper::SourceFiles{},
		        contentmapper::ErrProjectUnavailable};
	}
	auto [content, ok] = fs->ReadFile(parseOptions.FileName);
	if (!ok) {
		return {contentmapper::SourceFiles{}, nullptr};
	}
	auto [files, err] = contentmapper::TransformAndParse(
	    parseOptions, content, mapper, contentMapperProject);
	if (err == nullptr) {
		err = contentmapper::CheckSupplementalFileNameCollisions(
		    files, [this](std::string_view f) {
			    return fs->FileExists(std::string(f));
		    });
	}
	return {files, err};
}

// compilerHost.go:56 ContentMapperProject.
contentmapper::Project* compilerHost::ContentMapperProject() const {
	return contentMapperProject.get();
}

// compilerHost.go:60 GetResolvedProjectReference.
tsoptions::ParsedCommandLine*
compilerHost::GetResolvedProjectReference(const std::string& fileName,
                                          const tspath::Path& path) {
	return host_->GetResolvedProjectReference(fileName, path);
}

} // namespace tsc::execute::build
