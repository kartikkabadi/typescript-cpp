// Port of tsc/internal/testrunner/test_case_parser.go
#include "internal/testrunner/testrunner.h"

#include "internal/parser/parser.h"
#include "internal/tsoptions/tsoptionstest/tsoptionstest.h"

namespace tsc::testrunner {

namespace detail {

// lineDelimiter — test_case_parser.go:18.
const gostd::regexp::Regexp& lineDelimiter() {
	static const gostd::regexp::Regexp re{"\r?\n"};
	return re;
}

// optionRegex — test_case_parser.go:41. Regex for parsing options in the
// format "@Alpha: Value of any sort".
const gostd::regexp::Regexp& optionRegex() {
	static const gostd::regexp::Regexp re{
	    R"((?m)^\/{2}\s*@(\w+)\s*:\s*([^\r\n]*))"};
	return re;
}

// linkRegex — test_case_parser.go:44. Regex for parsing @link option.
const gostd::regexp::Regexp& linkRegex() {
	static const gostd::regexp::Regexp re{
	    R"((?m)^\/{2}\s*@link\s*:\s*([^\r\n]*)\s*->\s*([^\r\n]*))"};
	return re;
}

// fourslashDirectives — test_case_parser.go:47. File-specific directives
// used by fourslash tests.
const std::vector<std::string>& fourslashDirectives() {
	static const std::vector<std::string> v{"emitthisfile", "noopen"};
	return v;
}

}  // namespace detail

// makeUnitsFromTest — test_case_parser.go:51-118.
// Given a test file containing // @FileName directives,
// return an array of named units of code to be added to an existing
// compiler instance.
testCaseContent makeUnitsFromTest(const std::string& code,
                                  const std::string& fileName) {
	auto parsed = ParseTestFilesAndSymlinks<testUnit*>(
	    code, fileName,
	    [](const std::string& filename, const std::string& content,
	       const std::unordered_map<std::string, std::string>& fileOptions)
	        -> std::pair<testUnit*, gostd::Error> {
		    (void)fileOptions;
		    return {new testUnit{content, filename}, nullptr};
	    });
	std::vector<testUnit*> testUnits = std::move(parsed.units);
	std::unordered_map<std::string, std::string> symlinks =
	    std::move(parsed.symlinks);
	std::string currentDirectory = parsed.currentDir;

	if (currentDirectory.empty()) {
		currentDirectory = srcFolder;
	}

	// unit tests always list files explicitly
	std::unordered_map<std::string, std::string> allFiles;
	for (auto* data : testUnits) {
		allFiles[tspath::getNormalizedAbsolutePath(data->name,
		                                          currentDirectory)] =
		    data->content;
	}
	auto parseConfigHost = tsoptions::tsoptionstest::
	    NewVFSParseConfigHostWithSymlinks(allFiles, symlinks, currentDirectory,
	                                      true /*useCaseSensitiveFileNames*/);

	// Content mappers are gated behind --runExternalCode, a
	// command-line-only option. A test opts in with a top-level
	// `// @runExternalCode: true`, which we surface to the config parse as
	// an existing option so the gate passes and the mappers register.
	CompilerOptions* existingOptions = nullptr;
	if (auto it = parsed.globalOptions.find("runexternalcode");
	    it != parsed.globalOptions.end() && it->second == "true") {
		existingOptions = new CompilerOptions();
		existingOptions->RunExternalCode = Tristate::True;
	}

	// check if project has tsconfig.json in the list of files
	tsoptions::ParsedCommandLine* tsConfig = nullptr;
	testUnit* tsConfigFileUnitData = nullptr;
	for (size_t i = 0; i < testUnits.size(); i++) {
		auto* data = testUnits[i];
		if (!testutil::harnessutil::GetConfigNameFromFileName(data->name)
		         .empty()) {
			std::string configFileName =
			    tspath::getNormalizedAbsolutePath(data->name,
			                                      currentDirectory);
			auto path = tspath::toPath(
			    data->name, parseConfigHost->GetCurrentDirectory(),
			    parseConfigHost->Vfs->UseCaseSensitiveFileNames());
			SourceFile* configJson = parseSourceFile(
			    SourceFileParseOptions{configFileName, path},
			    data->content, ScriptKind::JSON);
			auto* tsConfigSourceFile =
			    new tsoptions::TsConfigSourceFile{};
			tsConfigSourceFile->SourceFile = configJson;
			auto configDir = tspath::getDirectoryPath(configFileName);
			tsConfig = tsoptions::ParseJsonSourceFileConfigFileContent(
			    tsConfigSourceFile, parseConfigHost.get(), configDir,
			    existingOptions,
			    tsoptions::JsonObjectPtr{} /*existingOptionsRaw*/, configFileName,
			    {} /*resolutionStack*/, nullptr /*extendedConfigCache*/);
			tsConfigFileUnitData = data;

			// delete tsconfig file entry from the list
			testUnits.erase(testUnits.begin() + i);
			break;
		}
	}

	return testCaseContent{testUnits, tsConfig, tsConfigFileUnitData,
	                       symlinks};
}

// extractCompilerSettings — test_case_parser.go:281-289.
rawCompilerSettings extractCompilerSettings(const std::string& content) {
	rawCompilerSettings opts;

	for (auto& match :
	     detail::optionRegex().FindAllStringSubmatch(content, -1)) {
		// strings.TrimSuffix(strings.TrimSpace(match[2]), ";")
		auto value = trimSpace(match[2]);
		if (value.ends_with(';')) {
			value.remove_suffix(1);
		}
		opts[detail::toLowerGo(match[1])] = std::string(value);
	}

	return opts;
}

// parseSymlinkFromTest — test_case_parser.go:291-299.
bool parseSymlinkFromTest(
    const std::string& line,
    std::unordered_map<std::string, std::string>& symlinks) {
	auto linkMetaData = detail::linkRegex().FindStringSubmatch(line);
	if (linkMetaData.empty()) {
		return false;
	}

	symlinks[std::string(trimSpace(linkMetaData[2]))] =
	    std::string(trimSpace(linkMetaData[1]));
	return true;
}

}  // namespace tsc::testrunner
