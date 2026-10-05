// outputpaths — port of tsc/internal/outputpaths/outputpaths.go +
// commonsourcedirectory.go.
#include "internal/outputpaths/outputpaths.h"

#include <algorithm>

#include "internal/ast/ast.h"

namespace tsc::outputpaths {

using namespace tsc::tspath;

static std::vector<std::string_view> asViews(
    const std::vector<std::string>& v) {
	return {v.begin(), v.end()};
}

// getOwnEmitOutputFilePath — outputpaths.go:183
static std::string getOwnEmitOutputFilePath(std::string_view fileName,
                                            const CompilerOptions* options,
                                            OutputPathsHost* host,
                                            std::string_view extension) {
	std::string emitOutputFilePathWithoutExtension;
	if (!options->OutDir.empty()) {
		std::string currentDirectory = host->GetCurrentDirectory();
		emitOutputFilePathWithoutExtension = removeFileExtension(
		    GetSourceFilePathInNewDir(fileName, options->OutDir,
		                              currentDirectory,
		                              host->CommonSourceDirectory(),
		                              host->UseCaseSensitiveFileNames()));
	} else {
		emitOutputFilePathWithoutExtension = removeFileExtension(fileName);
	}
	return emitOutputFilePathWithoutExtension + std::string(extension);
}

// isContentMappedFileName — outputpaths.go:97
static bool isContentMappedFileName(std::string_view fileName,
                                    OutputPathsHost* host) {
	return !getLongestExtensionFromPath(
	            fileName, asViews(host->ContentMapperExtensions()),
	            !host->UseCaseSensitiveFileNames())
	            .empty();
}

// getOutputPathWithoutChangingExtension — outputpaths.go:165
static std::string getOutputPathWithoutChangingExtension(
    std::string_view inputFileName, std::string_view outputDirectory,
    OutputPathsHost* host) {
	if (!outputDirectory.empty()) {
		return resolvePath(
		    outputDirectory,
		    {getRelativePathFromDirectory(
		        host->CommonSourceDirectory(), inputFileName,
		        ComparePathsOptions{
		            .currentDirectory = host->GetCurrentDirectory(),
		            .useCaseSensitiveFileNames =
		                host->UseCaseSensitiveFileNames(),
		        })});
	}
	return std::string(inputFileName);
}

// GetOutputPathsFor — outputpaths.go:47
OutputPaths GetOutputPathsFor(SourceFile* sourceFile,
                              const CompilerOptions* options,
                              OutputPathsHost* host, ForceEmitPaths force) {
	std::string ownOutputFilePath = getOwnEmitOutputFilePath(
	    sourceFile->FileName(), options, host,
	    GetOutputExtension(sourceFile->FileName(), options->Jsx));
	bool isJsonFile = isJsonSourceFile(sourceFile);
	// If json file emits to the same location skip writing it, if
	// emitDeclarationOnly skip writing it
	bool isJsonEmittedToSameLocation =
	    isJsonFile &&
	    comparePaths(
	        sourceFile->FileName(), ownOutputFilePath,
	        ComparePathsOptions{
	            .currentDirectory = host->GetCurrentDirectory(),
	            .useCaseSensitiveFileNames = host->UseCaseSensitiveFileNames(),
	        }) == 0;
	OutputPaths paths;
	if (sourceFile->ContentMapper().empty() &&
	    (force.Js || options->EmitDeclarationOnly != Tristate::True) &&
	    !isJsonEmittedToSameLocation) {
		paths.jsFilePath = ownOutputFilePath;
		if (!isJsonSourceFile(sourceFile)) {
			paths.sourceMapFilePath =
			    GetSourceMapFilePath(paths.jsFilePath, options);
		}
	}
	if (force.Dts || (options->GetEmitDeclarations() && !isJsonFile)) {
		paths.declarationFilePath = GetDeclarationEmitOutputFilePath(
		    sourceFile->FileName(), options, host);
		if (options->GetAreDeclarationMapsEnabled() ||
		    (force.DeclarationMap && options->DeclarationMap == Tristate::True)) {
			paths.declarationMapPath = paths.declarationFilePath + ".map";
		}
	}
	return paths;
}

// ForEachEmittedFile — outputpaths.go:73
bool ForEachEmittedFile(
    OutputPathsHost* host, const CompilerOptions* options,
    const std::function<bool(OutputPaths* emitFileNames,
                             SourceFile* sourceFile)>& action,
    const std::vector<SourceFile*>& sourceFiles, bool forceDtsEmit) {
	for (SourceFile* sourceFile : sourceFiles) {
		OutputPaths emitFileNames = GetOutputPathsFor(
		    sourceFile, options, host, ForceEmitPaths{.Dts = forceDtsEmit});
		if (action(&emitFileNames, sourceFile)) {
			return true;
		}
	}
	return false;
}

// GetOutputJSFileName — outputpaths.go:81
std::string GetOutputJSFileName(std::string_view inputFileName,
                                const CompilerOptions* options,
                                OutputPathsHost* host) {
	if (options->EmitDeclarationOnly == Tristate::True ||
	    isContentMappedFileName(inputFileName, host)) {
		return "";
	}
	std::string outputFileName =
	    GetOutputJSFileNameWorker(inputFileName, options, host);
	if (!fileExtensionIs(outputFileName, extensionJson) ||
	    comparePaths(inputFileName, outputFileName,
	                 ComparePathsOptions{
	                     .currentDirectory = host->GetCurrentDirectory(),
	                     .useCaseSensitiveFileNames =
	                         host->UseCaseSensitiveFileNames(),
	                 }) != 0) {
		return outputFileName;
	}
	return "";
}

// GetOutputJSFileNameWorker — outputpaths.go:101
std::string GetOutputJSFileNameWorker(std::string_view inputFileName,
                                      const CompilerOptions* options,
                                      OutputPathsHost* host) {
	return changeExtension(
	    getOutputPathWithoutChangingExtension(inputFileName, options->OutDir,
	                                          host),
	    GetOutputExtension(inputFileName, options->Jsx));
}

// GetOutputDeclarationFileNameWorker — outputpaths.go:108
std::string GetOutputDeclarationFileNameWorker(std::string_view inputFileName,
                                               const CompilerOptions* options,
                                               OutputPathsHost* host) {
	std::string_view dir = options->DeclarationDir;
	if (dir.empty()) {
		dir = options->OutDir;
	}
	return ChangeToDeclarationExtension(
	    getOutputPathWithoutChangingExtension(inputFileName, dir, host), host);
}

// GetDeclarationEmitOutputFilePath — outputpaths.go:128
std::string GetDeclarationEmitOutputFilePath(std::string_view file,
                                             const CompilerOptions* options,
                                             OutputPathsHost* host) {
	const std::string* outputDir = nullptr;
	if (!options->DeclarationDir.empty()) {
		outputDir = &options->DeclarationDir;
	} else if (!options->OutDir.empty()) {
		outputDir = &options->OutDir;
	}

	std::string path;
	if (outputDir != nullptr) {
		path = GetSourceFilePathInNewDirWorker(
		    file, *outputDir, host->GetCurrentDirectory(),
		    host->CommonSourceDirectory(), host->UseCaseSensitiveFileNames());
	} else {
		path = file;
	}
	return ChangeToDeclarationExtension(path, host);
}

// ChangeToDeclarationExtension — outputpaths.go:141
std::string ChangeToDeclarationExtension(std::string_view path,
                                         OutputPathsHost* host) {
	std::string_view extension = getLongestExtensionFromPath(
	    path, asViews(host->ContentMapperExtensions()),
	    /*ignoreCase*/ false);
	if (!extension.empty()) {
		return std::string(removeExtension(path, extension)) + ".d" +
		       std::string(extension) + ".ts";
	}
	std::string_view pathWithoutExtension = removeFileExtension(path);
	if (pathWithoutExtension == path) {
		if (std::string_view extension2 =
		        getAnyExtensionFromPath(path, nullptr, false);
		    !extension2.empty()) {
			pathWithoutExtension = removeExtension(path, extension2);
		}
	}
	return std::string(pathWithoutExtension) +
	       getDeclarationEmitExtensionForPathString(path);
}

// GetSourceFilePathInNewDir — outputpaths.go:161
std::string GetSourceFilePathInNewDir(std::string_view fileName,
                                      std::string_view newDirPath,
                                      std::string_view currentDirectory,
                                      std::string_view commonSourceDirectory,
                                      bool useCaseSensitiveFileNames) {
	return GetSourceFilePathInNewDirWorker(fileName, newDirPath,
	                                       currentDirectory,
	                                       commonSourceDirectory,
	                                       useCaseSensitiveFileNames);
}

// GetSourceFilePathInNewDirWorker — outputpaths.go:175
std::string GetSourceFilePathInNewDirWorker(std::string_view fileName,
                                            std::string_view newDirPath,
                                            std::string_view currentDirectory,
                                            std::string_view commonSourceDirectory,
                                            bool useCaseSensitiveFileNames) {
	std::string sourceFilePath =
	    getNormalizedAbsolutePath(fileName, currentDirectory);
	if (auto [trimmed, ok] = trimFilePathPrefix(sourceFilePath,
	                                          commonSourceDirectory,
	                                          useCaseSensitiveFileNames);
	    ok) {
		sourceFilePath = trimmed;
	}
	return combinePaths(newDirPath, {sourceFilePath});
}

// GetSourceMapFilePath — outputpaths.go:200
std::string GetSourceMapFilePath(std::string_view jsFilePath,
                                 const CompilerOptions* options) {
	if (options->SourceMap == Tristate::True &&
	    options->InlineSourceMap != Tristate::True) {
		return std::string(jsFilePath) + ".map";
	}
	return "";
}

// GetBuildInfoFileName — outputpaths.go:206
std::string GetBuildInfoFileName(const CompilerOptions* options,
                                 const ComparePathsOptions& opts) {
	if (!options->IsIncremental() && options->Build != Tristate::True) {
		return "";
	}
	if (!options->TsBuildInfoFile.empty()) {
		return options->TsBuildInfoFile;
	}
	if (options->ConfigFilePath.empty()) {
		return "";
	}
	std::string configFileExtensionLess =
	    std::string(removeFileExtension(options->ConfigFilePath));
	std::string buildInfoExtensionLess;
	if (!options->OutDir.empty()) {
		if (!options->RootDir.empty()) {
			buildInfoExtensionLess = resolvePath(
			    options->OutDir,
			    {getRelativePathFromDirectory(options->RootDir,
			                                  configFileExtensionLess, opts)});
		} else {
			buildInfoExtensionLess = combinePaths(
			    options->OutDir, {getBaseFileName(configFileExtensionLess)});
		}
	} else {
		buildInfoExtensionLess = configFileExtensionLess;
	}
	return buildInfoExtensionLess + std::string(extensionTsBuildInfo);
}

// computeCommonSourceDirectoryOfFilenames — commonsourcedirectory.go:6
static std::string computeCommonSourceDirectoryOfFilenames(
    const std::vector<std::string>& fileNames,
    std::string_view currentDirectory, bool useCaseSensitiveFileNames) {
	std::vector<std::string> commonPathComponents;
	bool first = true;
	for (const std::string& sourceFile : fileNames) {
		// Each file contributes into common source file path
		auto sourcePathComponents = getNormalizedPathComponents(
		    sourceFile, currentDirectory);

		// The base file name is not part of the common directory path
		sourcePathComponents.pop_back();

		if (first) {
			// first file
			commonPathComponents = sourcePathComponents;
			first = false;
			continue;
		}

		size_t n =
		    std::min(commonPathComponents.size(), sourcePathComponents.size());
		for (size_t i = 0; i < n; i++) {
			if (getCanonicalFileName(commonPathComponents[i],
			                         useCaseSensitiveFileNames) !=
			    getCanonicalFileName(sourcePathComponents[i],
			                         useCaseSensitiveFileNames)) {
				if (i == 0) {
					// Failed to find any common path component
					return "";
				}
				// New common path found that is 0 -> i-1
				commonPathComponents.resize(i);
				break;
			}
		}

		// If the sourcePathComponents was shorter than the
		// commonPathComponents, truncate to the sourcePathComponents
		if (sourcePathComponents.size() < commonPathComponents.size()) {
			commonPathComponents.resize(sourcePathComponents.size());
		}
	}

	if (commonPathComponents.empty()) {
		// Can happen when all input files are .d.ts files
		return std::string(currentDirectory);
	}

	std::vector<std::string_view> views(commonPathComponents.begin(),
	                                    commonPathComponents.end());
	return getPathFromPathComponents(views);
}

// GetComputedCommonSourceDirectory — commonsourcedirectory.go:50
std::string GetComputedCommonSourceDirectory(
    const std::vector<std::string>& emittedFiles,
    std::string_view currentDirectory, bool useCaseSensitiveFileNames) {
	std::string commonSourceDirectory = computeCommonSourceDirectoryOfFilenames(
	    emittedFiles, currentDirectory, useCaseSensitiveFileNames);
	if (!commonSourceDirectory.empty()) {
		commonSourceDirectory =
		    ensureTrailingDirectorySeparator(commonSourceDirectory);
	}
	return commonSourceDirectory;
}

// GetCommonSourceDirectory — commonsourcedirectory.go:59
std::string GetCommonSourceDirectory(
    const CompilerOptions* options,
    const std::function<std::vector<std::string>()>& files,
    std::string_view currentDirectory, bool useCaseSensitiveFileNames,
    const std::function<bool(const std::vector<std::string>&,
                             std::string_view)>& checkSourceFilesBelongToPath) {
	std::string commonSourceDirectory;
	if (!options->RootDir.empty()) {
		// If a rootDir is specified use it as the commonSourceDirectory
		commonSourceDirectory = options->RootDir;
		if (checkSourceFilesBelongToPath) {
			checkSourceFilesBelongToPath(files(), options->RootDir);
		}
	} else if (!options->ConfigFilePath.empty()) {
		// If the rootDir is not specified, then the common source directory is
		// the directory of the config file.
		commonSourceDirectory = getDirectoryPath(options->ConfigFilePath);
		if (checkSourceFilesBelongToPath) {
			checkSourceFilesBelongToPath(files(), commonSourceDirectory);
		}
	} else {
		commonSourceDirectory = computeCommonSourceDirectoryOfFilenames(
		    files(), currentDirectory, useCaseSensitiveFileNames);
	}

	if (!commonSourceDirectory.empty()) {
		// Make sure directory path ends with directory separator so this string
		// can directly be used to replace with "" to get the relative path of
		// the source file and the relative path doesn't start with / making it
		// rooted path
		commonSourceDirectory =
		    ensureTrailingDirectorySeparator(commonSourceDirectory);
	}

	return commonSourceDirectory;
}

} // namespace tsc::outputpaths
