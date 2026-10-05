// Port of tsc/internal/tsoptions/parsedcommandline.go.
#include "internal/tsoptions/tsoptions.h"


#include "internal/ast/ast.h"
#include "internal/core/spelling.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/module/types.h"
#include "internal/module/vfsmatch.h"
#include "internal/tspath/tspath.h"

namespace tsc::tsoptions {

namespace {

constexpr std::string_view fileGlobPattern =
    "*.{js,jsx,mjs,cjs,ts,tsx,mts,cts,json}";
constexpr std::string_view recursiveFileGlobPattern =
    "**/*.{js,jsx,mjs,cjs,ts,tsx,mts,cts,json}";

std::string joinComma(const std::vector<std::string>& v) {
	std::string out;
	for (size_t i = 0; i < v.size(); i++) {
		if (i) {
			out += ",";
		}
		out += v[i];
	}
	return out;
}

}  // namespace

// fileGlobPatterns — parsedcommandline.go:31.
std::pair<std::string, std::string> ParsedCommandLine::fileGlobPatterns() {
	std::vector<std::string> mapperExtensions = ContentMapperExtensions();
	if (mapperExtensions.empty()) {
		return {std::string(fileGlobPattern),
		        std::string(recursiveFileGlobPattern)};
	}
	std::vector<std::string> extensions;
	extensions.reserve(9 + mapperExtensions.size());
	for (const char* e :
	     {"js", "jsx", "mjs", "cjs", "ts", "tsx", "mts", "cts", "json"}) {
		extensions.emplace_back(e);
	}
	for (auto& extension : mapperExtensions) {
		extensions.push_back(extension.starts_with('.')
		                         ? extension.substr(1)
		                         : extension);
	}
	std::string fileGlob = "*.{" + joinComma(extensions) + "}";
	return {fileGlob, "**/" + fileGlob};
}

// NewParsedCommandLine — parsedcommandline.go:77.
ParsedCommandLine* NewParsedCommandLine(
    tsc::CompilerOptions* compilerOptions,
    const std::vector<std::string>& rootFileNames,
    const std::vector<ProjectReference*>& projectReferences,
    const tspath::ComparePathsOptions& comparePathsOptions) {
	auto* parsed = new ParsedCommandLine{};
	parsed->ParsedConfig = new ParsedOptions{
	    .CompilerOptions = compilerOptions,
	    .FileNames = rootFileNames,
	    .ProjectReferences = projectReferences,
	};
	parsed->comparePathsOptions = comparePathsOptions;
	return parsed;
}

// WithFileNames — parsedcommandline.go:93.
ParsedCommandLine* ParsedCommandLine::WithFileNames(
    const std::vector<std::string>& fileNames) {
	auto* parsedConfig = new ParsedOptions(*ParsedConfig);
	parsedConfig->FileNames = fileNames;
	auto* parsed = new ParsedCommandLine{};
	parsed->ParsedConfig = parsedConfig;
	parsed->ConfigFile = ConfigFile;
	parsed->Errors = Errors;
	parsed->Raw = Raw;
	parsed->CompileOnSave = CompileOnSave;
	parsed->comparePathsOptions = comparePathsOptions;
	parsed->wildcardDirectories = wildcardDirectories;
	parsed->includeGlobs = includeGlobs;
	parsed->literalFileNamesLen = literalFileNamesLen;
	return parsed;
}

// ConfigName — parsedcommandline.go:120.
std::string ParsedCommandLine::ConfigName() const {
	if (this == nullptr || ConfigFile == nullptr) {
		return "";
	}
	return ConfigFile->SourceFile->FileName();
}

// ParseInputOutputNames — parsedcommandline.go:135.
void ParsedCommandLine::ParseInputOutputNames() {
	std::call_once(sourceAndOutputMapsOnce, [&] {
		auto sourceToOutput = std::make_shared<std::unordered_map<
		    tspath::Path, SourceOutputAndProjectReference*>>();
		auto outputDtsToSource = std::make_shared<std::unordered_map<
		    tspath::Path, SourceOutputAndProjectReference*>>();

		getOutputDeclarationAndSourceFileNames(
		    [&](std::string_view outputDts, std::string_view source) -> bool {
			    tspath::Path path = tspath::toPath(
			        source, GetCurrentDirectory(),
			        UseCaseSensitiveFileNames());
			    auto* projectReference = new SourceOutputAndProjectReference{
			        .Source = std::string(source),
			        .OutputDts = std::string(outputDts),
			        .Resolved = this,
			    };
			    if (!outputDts.empty()) {
				    (*outputDtsToSource)[tspath::toPath(
				        outputDts, GetCurrentDirectory(),
				        UseCaseSensitiveFileNames())] = projectReference;
			    }
			    (*sourceToOutput)[path] = projectReference;
			    return true;
		    });
		outputDtsToProjectReference = outputDtsToSource;
		sourceToProjectReference = sourceToOutput;
	});
}

// CommonSourceDirectory — parsedcommandline.go:157.
std::string ParsedCommandLine::CommonSourceDirectory() {
	std::call_once(commonSourceDirectoryOnce, [&] {
		auto files = [&] {
			std::vector<std::string> result;
			for (const auto& file : ParsedConfig->FileNames) {
				if (!(ParsedConfig->CompilerOptions
				              ->NoEmitForJsFiles == Tristate::True && tspath::hasJSFileExtension(file)) &&
				    !tspath::isDeclarationFileName(file)) {
					result.push_back(file);
				}
			}
			return result;
		};

		commonSourceDirectory = outputpaths::GetCommonSourceDirectory(
		    ParsedConfig->CompilerOptions, files,
		    GetCurrentDirectory(),
		    UseCaseSensitiveFileNames(),
		    [this](const std::vector<std::string>& sourceFiles,
		           std::string_view rootDirectory) {
			    return checkSourceFilesBelongToPath(sourceFiles,
			                                      rootDirectory);
		    });
	});
	return commonSourceDirectory;
}

// checkSourceFilesBelongToPath — parsedcommandline.go:176.
bool ParsedCommandLine::checkSourceFilesBelongToPath(
    const std::vector<std::string>& sourceFiles,
    std::string_view rootDirectory) {
	bool allFilesBelongToPath = true;
	for (const auto& file : sourceFiles) {
		std::string absoluteSourceFilePath = tspath::getCanonicalFileName(
		    tspath::getNormalizedAbsolutePath(file, GetCurrentDirectory()),
		    UseCaseSensitiveFileNames());
		if (!tspath::containsPath(rootDirectory, file,
		                          comparePathsOptions)) {
			Errors.push_back(newCompilerDiagnostic(
			    File_0_is_not_under_rootDir_1_rootDir_is_expected_to_contain_all_source_files,
			    {absoluteSourceFilePath, std::string(rootDirectory)}));
			allFilesBelongToPath = false;
		}
	}

	return allFilesBelongToPath;
}

// getOutputDeclarationAndSourceFileNames — parsedcommandline.go:197.
void ParsedCommandLine::getOutputDeclarationAndSourceFileNames(
    const std::function<bool(std::string_view, std::string_view)>& yield) {
	for (const auto& fileName : ParsedConfig->FileNames) {
		std::string outputDts;
		if (!tspath::isDeclarationFileName(fileName) &&
		    !tspath::fileExtensionIs(fileName, tspath::extensionJson)) {
			outputDts = outputpaths::GetOutputDeclarationFileNameWorker(
			    fileName, CompilerOptions(), this);
		}
		if (!yield(outputDts, fileName)) {
			return;
		}
	}
}

// GetOutputFileNames — parsedcommandline.go:211.
void ParsedCommandLine::GetOutputFileNames(
    const std::function<bool(std::string_view)>& yield) {
	for (const auto& fileName : ParsedConfig->FileNames) {
		if (tspath::isDeclarationFileName(fileName)) {
			continue;
		}
		std::string jsFileName = outputpaths::GetOutputJSFileName(
		    fileName, CompilerOptions(), this);
		bool isJson =
		    tspath::fileExtensionIs(fileName, tspath::extensionJson);
		if (!jsFileName.empty()) {
			if (!yield(jsFileName)) {
				return;
			}
			if (!isJson) {
				std::string sourceMap =
				    outputpaths::GetSourceMapFilePath(jsFileName,
				                                      CompilerOptions());
				if (!sourceMap.empty()) {
					if (!yield(sourceMap)) {
						return;
					}
				}
			}
		}
		if (isJson) {
			continue;
		}
		if (CompilerOptions()->GetEmitDeclarations()) {
			std::string dtsFileName =
			    outputpaths::GetOutputDeclarationFileNameWorker(
			        fileName, CompilerOptions(), this);
			if (!dtsFileName.empty()) {
				if (!yield(dtsFileName)) {
					return;
				}
				if (GetContentMapperForFileName(fileName) == nullptr && CompilerOptions()->GetAreDeclarationMapsEnabled()) {
					std::string declarationMap = dtsFileName + ".map";
					if (!yield(declarationMap)) {
						return;
					}
				}
			}
		}
	}
}

// GetBuildInfoFileName — parsedcommandline.go:253.
std::string ParsedCommandLine::GetBuildInfoFileName() {
	return outputpaths::GetBuildInfoFileName(CompilerOptions(),
	                                       comparePathsOptions);
}

// WildcardDirectories — parsedcommandline.go:258.
std::unordered_map<std::string, bool>*
ParsedCommandLine::WildcardDirectories() {
	if (this == nullptr) {
		return nullptr;
	}

	std::call_once(wildcardDirectoriesOnce, [&] {
		if (wildcardDirectories == nullptr) {
			wildcardDirectories =
			    std::make_shared<std::unordered_map<std::string, bool>>(
			        getWildcardDirectories(
			            ConfigFile->configFileSpecs->validatedIncludeSpecs,
			            ConfigFile->configFileSpecs->validatedExcludeSpecs,
			            comparePathsOptions));
		}
	});

	return wildcardDirectories.get();
}

// WildcardDirectoryGlobs — parsedcommandline.go:276.
std::vector<glob::Glob*>* ParsedCommandLine::WildcardDirectoryGlobs() {
	auto* wildcardDirectories = WildcardDirectories();
	if (wildcardDirectories == nullptr) {
		return nullptr;
	}

	std::call_once(includeGlobsOnce, [&] {
		if (includeGlobs == nullptr) {
			auto [fileGlob, recursiveFileGlob] = fileGlobPatterns();
			auto globs = std::make_shared<std::vector<glob::Glob*>>();
			globs->reserve(wildcardDirectories->size());
			for (const auto& [dir, recursive] : *wildcardDirectories) {
				std::string pattern = tspath::normalizePath(dir) + "/" +
				    (recursive ? recursiveFileGlob : fileGlob);
				if (auto [parsed, err] = glob::Parse(pattern); err) {
					globs->push_back(parsed);
				}
			}
			includeGlobs = globs;
		}
	});

	return includeGlobs.get();
}

// LiteralFileNames — parsedcommandline.go:299.
std::vector<std::string> ParsedCommandLine::LiteralFileNames() {
	if (this != nullptr && ConfigFile != nullptr) {
		auto names = FileNames();
		return {names.begin(), names.begin() + literalFileNamesLen};
	}
	return {};
}

// FileNamesByPath — parsedcommandline.go:334.
std::unordered_map<tspath::Path, std::string>*
ParsedCommandLine::FileNamesByPath() {
	std::call_once(fileNamesByPathOnce, [&] {
		fileNamesByPath = std::make_shared<
		    std::unordered_map<tspath::Path, std::string>>(
		    ParsedConfig->FileNames.size());
		for (const auto& fileName : ParsedConfig->FileNames) {
			tspath::Path path =
			    tspath::toPath(fileName, GetCurrentDirectory(),
			                   UseCaseSensitiveFileNames());
			(*fileNamesByPath)[path] = fileName;
		}
	});
	return fileNamesByPath.get();
}

// ContentMappers — parsedcommandline.go:349.
std::vector<contentmapper::Mapper*> ParsedCommandLine::ContentMappers() {
	if (this == nullptr || ParsedConfig == nullptr) {
		return {};
	}
	return ParsedConfig->ContentMappers;
}

// ContentMapperExtensions — parsedcommandline.go:358.
std::vector<std::string> ParsedCommandLine::ContentMapperExtensions() {
	std::vector<std::string> result;
	for (const auto* m : ContentMappers()) {
		for (const auto& e : m->Definition.Extensions) {
			result.push_back(e);
		}
	}
	return result;
}

// GetContentMapperForFileName — parsedcommandline.go:366.
contentmapper::Mapper* ParsedCommandLine::GetContentMapperForFileName(
    std::string_view fileName) {
	bool ignoreCase = !UseCaseSensitiveFileNames();
	auto exts = ContentMapperExtensions();
	std::vector<std::string_view> extViews(exts.begin(), exts.end());
	std::string_view extension = tspath::getLongestExtensionFromPath(
	    fileName, extViews, ignoreCase);
	for (auto* mapper : ContentMappers()) {
		for (const auto& mapperExtension : mapper->Definition.Extensions) {
			if (extension == mapperExtension ||
			    (ignoreCase && utf8detail::equalFold(extension, mapperExtension))) {
				return mapper;
			}
		}
	}
	return nullptr;
}

// ResolvedProjectReferencePaths — parsedcommandline.go:379.
std::vector<std::string> ParsedCommandLine::ResolvedProjectReferencePaths() {
	std::call_once(resolvedProjectReferencePathsOnce, [&] {
		resolvedProjectReferencePaths.clear();
		for (const auto* ref : ParsedConfig->ProjectReferences) {
			resolvedProjectReferencePaths.push_back(
			    ResolveProjectReferencePath(*ref));
		}
	});
	return resolvedProjectReferencePaths;
}

// ExtendedSourceFiles — parsedcommandline.go:386.
std::vector<std::string> ParsedCommandLine::ExtendedSourceFiles() {
	if (this == nullptr || ConfigFile == nullptr) {
		return {};
	}
	return ConfigFile->ExtendedSourceFiles;
}

// GetConfigFileParsingDiagnostics — parsedcommandline.go:393.
std::vector<Diagnostic*> ParsedCommandLine::GetConfigFileParsingDiagnostics() {
	if (ConfigFile != nullptr) {
		// todo: !!! should be ConfigFile.ParseDiagnostics, check if they
		// are the same
		auto result = ConfigFile->SourceFile->diagnostics;
		result.insert(result.end(), Errors.begin(), Errors.end());
		return result;
	}
	return Errors;
}

// PossiblyMatchesFileName — parsedcommandline.go:403.
bool ParsedCommandLine::PossiblyMatchesFileName(std::string_view fileName) {
	tspath::Path path = tspath::toPath(fileName, GetCurrentDirectory(),
	                                   UseCaseSensitiveFileNames());
	auto* namesByPath = FileNamesByPath();
	if (namesByPath != nullptr && namesByPath->find(path) != namesByPath->end()) {
		return true;
	}

	for (const auto& include :
	     ConfigFile->configFileSpecs->validatedIncludeSpecs) {
		if (include.find_first_of("*?") == std::string::npos &&
		    !module::vfsmatch::IsImplicitGlob(include)) {
			tspath::Path includePath =
			    tspath::toPath(include, GetCurrentDirectory(),
			                   UseCaseSensitiveFileNames());
			if (includePath == path) {
				return true;
			}
		}
	}
	if (GetContentMapperForFileName(fileName) != nullptr) {
		tspath::Path directoryPath =
		    std::string(tspath::getDirectoryPath(path));
		if (PossiblyMatchesDirectoryName(directoryPath)) {
			return true;
		}
	}
	if (auto* wildcardDirectoryGlobs = WildcardDirectoryGlobs();
	    wildcardDirectoryGlobs != nullptr &&
	    !wildcardDirectoryGlobs->empty()) {
		for (auto* glob : *wildcardDirectoryGlobs) {
			if (glob->Match(fileName)) {
				return true;
			}
		}
	}
	return false;
}

// PossiblyMatchesDirectoryName — parsedcommandline.go:433.
bool ParsedCommandLine::PossiblyMatchesDirectoryName(
    const tspath::Path& directoryPath) {
	auto* wildcardDirectories = WildcardDirectories();
	if (wildcardDirectories == nullptr) {
		return false;
	}
	for (const auto& [wildcardDir, recursive] : *wildcardDirectories) {
		tspath::Path wildcardDirPath =
		    tspath::toPath(wildcardDir, GetCurrentDirectory(),
		                   UseCaseSensitiveFileNames());
		if (recursive) {
			if (tspath::containsPath(wildcardDirPath, directoryPath,
			                         comparePathsOptions)) {
				return true;
			}
		} else {
			if (wildcardDirPath == directoryPath) {
				return true;
			}
		}
	}
	return false;
}

// GetMatchedFileSpec — parsedcommandline.go:449.
std::string ParsedCommandLine::GetMatchedFileSpec(std::string_view fileName) {
	return ConfigFile->configFileSpecs->getMatchedFileSpec(
	    fileName, comparePathsOptions);
}

// GetMatchedIncludeSpec — parsedcommandline.go:453.
std::pair<std::string, bool> ParsedCommandLine::GetMatchedIncludeSpec(
    std::string_view fileName) {
	if (ConfigFile->configFileSpecs->validatedIncludeSpecs.empty()) {
		return {"", false};
	}

	if (ConfigFile->configFileSpecs->isDefaultIncludeSpec) {
		return {ConfigFile->configFileSpecs->validatedIncludeSpecs[0], true};
	}

	return {ConfigFile->configFileSpecs->getMatchedIncludeSpec(
	            fileName, comparePathsOptions),
	        false};
}

// ReloadFileNamesOfParsedCommandLine — parsedcommandline.go:465.
ParsedCommandLine* ParsedCommandLine::ReloadFileNamesOfParsedCommandLine(
    module::ResolutionHost* fs) {
	auto* parsedConfig = new ParsedOptions(*ParsedConfig);
	auto [fileNames, newLiteralFileNamesLen] = getFileNamesFromConfigSpecs(
	    *ConfigFile->configFileSpecs, GetCurrentDirectory(),
	    CompilerOptions(), fs, ContentMapperExtensions());
	parsedConfig->FileNames = fileNames;
	auto* parsedCommandLine = new ParsedCommandLine{};
	parsedCommandLine->ParsedConfig = parsedConfig;
	parsedCommandLine->ConfigFile = ConfigFile;
	parsedCommandLine->Errors = Errors;
	parsedCommandLine->Raw = Raw;
	parsedCommandLine->CompileOnSave = CompileOnSave;
	parsedCommandLine->comparePathsOptions = comparePathsOptions;
	parsedCommandLine->wildcardDirectories = wildcardDirectories;
	parsedCommandLine->includeGlobs = includeGlobs;
	parsedCommandLine->literalFileNamesLen = newLiteralFileNamesLen;
	return parsedCommandLine;
}

// Locale — parsedcommandline.go:489.
locale::Locale ParsedCommandLine::Locale() {
	std::call_once(localeOnce, [&] {
		locale_ = locale::Parse(CompilerOptions()->Locale).first;
	});
	return locale_;
}

}  // namespace tsc::tsoptions
