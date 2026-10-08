// outputpaths — port of tsc/internal/outputpaths/outputpaths.go +
// commonsourcedirectory.go.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "internal/core/types.h"
#include "internal/tspath/tspath.h"

namespace tsc {
struct CompilerOptions;
class SourceFile;
} // namespace tsc

namespace tsc::outputpaths {

// OutputPathsHost — outputpaths.go:9
struct OutputPathsHost {
	virtual ~OutputPathsHost() = default;
	virtual std::string CommonSourceDirectory() = 0;
	virtual std::vector<std::string> ContentMapperExtensions() = 0;
	virtual std::string GetCurrentDirectory() = 0;
	virtual bool UseCaseSensitiveFileNames() = 0;
};

// OutputPaths — outputpaths.go:16
struct OutputPaths {
	std::string jsFilePath;
	std::string sourceMapFilePath;
	std::string declarationFilePath;
	std::string declarationMapPath;

	std::string DeclarationFilePath() const { return declarationFilePath; }
	std::string JsFilePath() const { return jsFilePath; }
	std::string SourceMapFilePath() const { return sourceMapFilePath; }
	std::string DeclarationMapPath() const { return declarationMapPath; }
};

// ForceEmitPaths — outputpaths.go:41
struct ForceEmitPaths {
	bool Dts = false;
	bool Js = false;
	bool DeclarationMap = false;
};

// GetOutputPathsFor — outputpaths.go:47 (Go returns *OutputPaths, always
// non-nil; C++ returns by value)
OutputPaths GetOutputPathsFor(SourceFile* sourceFile,
                              const CompilerOptions* options,
                              OutputPathsHost* host, ForceEmitPaths force);

// ForEachEmittedFile — outputpaths.go:73
bool ForEachEmittedFile(
    OutputPathsHost* host, const CompilerOptions* options,
    const std::function<bool(OutputPaths* emitFileNames,
                             SourceFile* sourceFile)>& action,
    const std::vector<SourceFile*>& sourceFiles, bool forceDtsEmit);

// GetOutputJSFileName — outputpaths.go:81
std::string GetOutputJSFileName(std::string_view inputFileName,
                                const CompilerOptions* options,
                                OutputPathsHost* host);

// GetOutputJSFileNameWorker — outputpaths.go:101
std::string GetOutputJSFileNameWorker(std::string_view inputFileName,
                                      const CompilerOptions* options,
                                      OutputPathsHost* host);

// GetOutputDeclarationFileNameWorker — outputpaths.go:108
std::string GetOutputDeclarationFileNameWorker(std::string_view inputFileName,
                                               const CompilerOptions* options,
                                               OutputPathsHost* host);

// GetOutputExtension — outputpaths.go:116
inline std::string_view GetOutputExtension(std::string_view fileName,
                                           JsxEmit jsx) {
	using namespace tspath;
	if (fileExtensionIs(fileName, extensionJson)) {
		return extensionJson;
	}
	if (jsx == JsxEmit::Preserve &&
	    fileExtensionIsOneOf(fileName, {extensionJsx, extensionTsx})) {
		return extensionJsx;
	}
	if (fileExtensionIsOneOf(fileName, {extensionMts, extensionMjs})) {
		return extensionMjs;
	}
	if (fileExtensionIsOneOf(fileName, {extensionCts, extensionCjs})) {
		return extensionCjs;
	}
	return extensionJs;
}

// GetDeclarationEmitOutputFilePath — outputpaths.go:128
std::string GetDeclarationEmitOutputFilePath(std::string_view file,
                                             const CompilerOptions* options,
                                             OutputPathsHost* host);

// ChangeToDeclarationExtension — outputpaths.go:141
std::string ChangeToDeclarationExtension(std::string_view path,
                                         OutputPathsHost* host);

// GetSourceFilePathInNewDir — outputpaths.go:161
std::string GetSourceFilePathInNewDir(std::string_view fileName,
                                      std::string_view newDirPath,
                                      std::string_view currentDirectory,
                                      std::string_view commonSourceDirectory,
                                      bool useCaseSensitiveFileNames);

// GetSourceFilePathInNewDirWorker — outputpaths.go:175
std::string GetSourceFilePathInNewDirWorker(std::string_view fileName,
                                            std::string_view newDirPath,
                                            std::string_view currentDirectory,
                                            std::string_view commonSourceDirectory,
                                            bool useCaseSensitiveFileNames);

// GetSourceMapFilePath — outputpaths.go:200
std::string GetSourceMapFilePath(std::string_view jsFilePath,
                                 const CompilerOptions* options);

// GetBuildInfoFileName — outputpaths.go:206
std::string GetBuildInfoFileName(const CompilerOptions* options,
                                 const tspath::ComparePathsOptions& opts);

// GetComputedCommonSourceDirectory — commonsourcedirectory.go:50
std::string GetComputedCommonSourceDirectory(
    const std::vector<std::string>& emittedFiles,
    std::string_view currentDirectory, bool useCaseSensitiveFileNames);

// GetCommonSourceDirectory — commonsourcedirectory.go:59
std::string GetCommonSourceDirectory(
    const CompilerOptions* options,
    const std::function<std::vector<std::string>()>& files,
    std::string_view currentDirectory, bool useCaseSensitiveFileNames,
    const std::function<bool(const std::vector<std::string>&,
                             std::string_view)>& checkSourceFilesBelongToPath);

} // namespace tsc::outputpaths
