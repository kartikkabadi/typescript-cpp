// C++23 port of tsc/internal/testrunner — the test-case parser and the
// compiler/transpile baseline runners. Functions mirror Go names +
// tsc-internal links in section banners.
#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/compiler/program.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/regexp.h"
#include "internal/gostd/testing.h"
#include "internal/repo/paths.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"
#include "internal/testutil/harnessutil/harnessutil.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

namespace tsc::testrunner {

// === runner.go ===

// Runner — runner.go:7.
class Runner {
public:
	virtual ~Runner() = default;
	virtual std::vector<std::string> EnumerateTestFiles() = 0;
	virtual void RunTests(gostd::testing::T* t) = 0;
};

// runTests — runner.go:12.
void runTests(gostd::testing::T* t, std::vector<Runner*> runners);

// compiler_test_filter.go — setCompilerTestRunPattern stands in for Go's
// flag.Lookup("test.run"): runners that link compiler-test surfaces pass
// their -run value through it.
void setCompilerTestRunPattern(std::string pattern);
std::string_view compilerTestRunPattern();
std::function<bool(std::string_view)> compilerTestFileFilter(
    const std::string& pattern);

// === test_case_parser.go ===

namespace detail {
// Package-level Go regexes (test_case_parser.go:18,41,44). Accessors keep
// initialization order safe.
const gostd::regexp::Regexp& lineDelimiter();
const gostd::regexp::Regexp& optionRegex();
const gostd::regexp::Regexp& linkRegex();
// fourslashDirectives — test_case_parser.go:47.
const std::vector<std::string>& fourslashDirectives();

// strings.ToLower — Go lowercases per rune via the simple case mapping;
// invalid UTF-8 bytes become U+FFFD (range-over-string semantics).
inline std::string toLowerGo(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	int w = 0;
	for (std::string_view rest = s; !rest.empty(); rest.remove_prefix(w)) {
		char32_t r = decodeUtf8Rune(rest, &w);
		out += utf8String(stringutil::toLowerRune(r));
	}
	return out;
}
}  // namespace detail

// rawCompilerSettings — test_case_parser.go:25.
using rawCompilerSettings = std::unordered_map<std::string, std::string>;

// testUnit — test_case_parser.go:28.
struct testUnit {
	std::string content;
	std::string name;
};

// testCaseContent — test_case_parser.go:33.
struct testCaseContent {
	std::vector<testUnit*> testUnitData;
	tsoptions::ParsedCommandLine* tsConfig = nullptr;
	testUnit* tsConfigFileUnitData = nullptr;
	std::unordered_map<std::string, std::string> symlinks;
};

// makeUnitsFromTest — test_case_parser.go:51.
testCaseContent makeUnitsFromTest(const std::string& code,
                                  const std::string& fileName);

// ParseTestFilesOptions — test_case_parser.go:120.
struct ParseTestFilesOptions {
	// If true, allows test content to appear before the first @Filename
	// directive. In this case, an implicit first file is created using the
	// fileName parameter. This matches the behavior of the TypeScript
	// fourslash test harness.
	bool AllowImplicitFirstFile = false;
};

// parseFile — the Go generic call's `func(filename, content, fileOptions)
// (T, error)` argument.
template <typename T>
using ParseFileFn = std::function<std::pair<T, gostd::Error>(
    const std::string& filename, const std::string& content,
    const std::unordered_map<std::string, std::string>& fileOptions)>;

// ParseTestFilesResult — the Go (units, symlinks, currentDir, globalOptions,
// error) result tuple.
template <typename T>
struct ParseTestFilesResult {
	std::vector<T> units;
	std::unordered_map<std::string, std::string> symlinks;
	std::string currentDir;
	std::unordered_map<std::string, std::string> globalOptions;
	gostd::Error err;
};

// parseSymlinkFromTest — test_case_parser.go:291.
bool parseSymlinkFromTest(
    const std::string& line,
    std::unordered_map<std::string, std::string>& symlinks);

// extractCompilerSettings — test_case_parser.go:281.
rawCompilerSettings extractCompilerSettings(const std::string& content);

// ParseTestFilesAndSymlinksWithOptions — test_case_parser.go:138.
template <typename T>
ParseTestFilesResult<T> ParseTestFilesAndSymlinksWithOptions(
    const std::string& code, const std::string& fileName,
    const ParseFileFn<T>& parseFile, const ParseTestFilesOptions& options) {
	// List of all the subfiles we've parsed out
	std::vector<T> testUnits;

	auto lines = detail::lineDelimiter().Split(code, -1);

	// Stuff related to the subfile we're parsing
	std::string currentFileContent;  // strings.Builder
	std::string currentFileName;
	bool seenContentLine = false;
	bool hasSeenFile = false;
	if (options.AllowImplicitFirstFile) {
		// For fourslash tests, initialize currentFileName to the fileName
		// parameter so content before the first @Filename directive goes
		// into an implicit first file
		currentFileName = fileName;
	}
	std::string currentDirectory;
	gostd::Error parseError;
	std::unordered_map<std::string, std::string> currentFileOptions;
	std::unordered_map<std::string, std::string> symlinks;
	std::unordered_map<std::string, std::string> globalOptions;

	for (auto& line : lines) {
		if (parseSymlinkFromTest(line, symlinks)) {
			continue;
		}
		if (auto testMetaData = detail::optionRegex().FindStringSubmatch(line);
		    !testMetaData.empty()) {
			// Comment line, check for global/file @options and record them
			auto metaDataName = detail::toLowerGo(testMetaData[1]);
			auto metaDataValue =
			    std::string(trimSpace(testMetaData[2]));
			if (metaDataName == "currentdirectory") {
				currentDirectory = metaDataValue;
			}
			if (metaDataName != "filename") {
				if (metaDataName == "symlink" && !currentFileName.empty()) {
					// strings.SplitSeq(metaDataValue, ",")
					size_t start = 0;
					for (;;) {
						size_t comma = metaDataValue.find(',', start);
						auto link = trimSpace(
						    std::string_view(metaDataValue)
						        .substr(start,
						                comma == std::string::npos
						                    ? comma
						                    : comma - start));
						if (!link.empty()) {
							symlinks[std::string(link)] = currentFileName;
						}
						if (comma == std::string::npos) break;
						start = comma + 1;
					}
				} else if (std::ranges::find(
				               detail::fourslashDirectives(),
				               metaDataName) !=
				           detail::fourslashDirectives().end()) {
					// File-specific option
					currentFileOptions[metaDataName] = metaDataValue;
				} else {
					// Global option
					if (auto it = globalOptions.find(metaDataName);
					    it != globalOptions.end() &&
					    it->second != metaDataValue) {
						// !!! This would break existing baseline tests
						// panic("Duplicate global option: " + metaDataName)
					}
					globalOptions[metaDataName] = metaDataValue;
				}
				continue;
			}

			// New metadata statement after having collected some code to go
			// with the previous metadata
			if (!currentFileName.empty()) {
				// Store result file - always save for regular tests, but skip
				// empty implicit first file for fourslash
				bool shouldSaveFile = !options.AllowImplicitFirstFile ||
				                      !currentFileContent.empty() ||
				                      hasSeenFile;
				if (shouldSaveFile) {
					hasSeenFile = true;
					auto [newTestFile, e] = parseFile(
					    currentFileName, currentFileContent,
					    currentFileOptions);
					if (e != nullptr) {
						parseError = e;
						break;
					}
					testUnits.push_back(std::move(newTestFile));
				}

				// Reset local data
				currentFileContent.clear();
				seenContentLine = false;
				currentFileName = metaDataValue;
				currentFileOptions.clear();
			} else {
				// First metadata marker in the file
				bool hasContentBeforeFirstFilename =
				    !currentFileContent.empty() &&
				    skipTrivia(currentFileContent, 0) !=
				        (int)currentFileContent.size();
				if (hasContentBeforeFirstFilename &&
				    !options.AllowImplicitFirstFile) {
					TSC_UNREACHABLE(
					    "Non-comment test content appears before the first "
					    "'// @Filename' directive");
				}

				// If we have content before the first @Filename and
				// AllowImplicitFirstFile is true, we need to save it as an
				// implicit first file before starting the new file
				if (hasContentBeforeFirstFilename &&
				    options.AllowImplicitFirstFile &&
				    !currentFileName.empty()) {
					// Store the implicit first file
					hasSeenFile = true;
					auto [newTestFile, e] = parseFile(
					    currentFileName, currentFileContent,
					    currentFileOptions);
					if (e != nullptr) {
						parseError = e;
						break;
					}
					testUnits.push_back(std::move(newTestFile));
				}

				// Reset for the new file
				currentFileContent.clear();
				seenContentLine = false;
				currentFileName =
				    std::string(trimSpace(testMetaData[2]));
				currentFileOptions.clear();
			}
		} else {
			// Subfile content line
			// Append to the current subfile content, inserting a newline if
			// needed. For fourslash tests, use seenContentLine to preserve
			// leading blank lines (matching TS fourslash's //// content
			// markers). For compiler tests, use Len() != 0 which drops
			// leading blanks (matching TS's harness behavior).
			if (options.AllowImplicitFirstFile) {
				if (seenContentLine) {
					currentFileContent += '\n';
				}
				seenContentLine = true;
			} else {
				if (!currentFileContent.empty()) {
					currentFileContent += '\n';
				}
			}
			currentFileContent += line;
		}
	}

	// normalize the fileName for the single file case
	if (testUnits.empty() && currentFileName.empty()) {
		currentFileName =
		    std::string(tspath::getBaseFileName(fileName));
	}

	// if there are no parse errors so far, parse the rest of the file
	if (parseError == nullptr) {
		// EOF, push whatever remains
		auto [newTestFile2, e] = parseFile(currentFileName, currentFileContent,
		                                   currentFileOptions);

		parseError = e;
		testUnits.push_back(std::move(newTestFile2));
	}

	return {std::move(testUnits), std::move(symlinks), currentDirectory,
	        std::move(globalOptions), parseError};
}

// ParseTestFilesAndSymlinks — test_case_parser.go:130.
template <typename T>
ParseTestFilesResult<T> ParseTestFilesAndSymlinks(
    const std::string& code, const std::string& fileName,
    const ParseFileFn<T>& parseFile) {
	return ParseTestFilesAndSymlinksWithOptions<T>(code, fileName, parseFile,
	                                             ParseTestFilesOptions{});
}

// === compiler_runner.go / transpile_runner.go globals ===

// srcFolder — compiler_runner.go:34.
inline const std::string srcFolder = "/.src";

// localBasePath — compiler_runner.go:138.
const std::string& localBasePath();

// === compiler_runner.go ===

// CompilerTestType — compiler_runner.go:36.
enum class CompilerTestType { Conformance = 0, Regression = 1 };

// compilerFileBasedTest — compiler_runner.go:215.
struct compilerFileBasedTest {
	std::string filename;
	std::string content;
	std::vector<testutil::harnessutil::NamedTestConfiguration*>
	    configurations;
};

// testCaseContentWithConfig — compiler_runner.go:250.
struct testCaseContentWithConfig : testCaseContent {
	testutil::harnessutil::TestConfiguration configuration;
};

// compilerTest — compiler_runner.go:235.
struct compilerTest {
	std::string testName;
	std::string filename;
	std::string basename;
	// configuredName — name with configuration description, e.g. `file`.
	std::string configuredName;
	std::string currentDirectory;
	CompilerOptions* options = nullptr;
	testutil::harnessutil::HarnessOptions* harnessOptions = nullptr;
	testutil::harnessutil::CompilationResult* result = nullptr;
	std::vector<testutil::harnessutil::TestFile*> tsConfigFiles;
	// equivalent to the files that will be passed on the command line
	std::vector<testutil::harnessutil::TestFile*> toBeCompiled;
	// equivalent to other files on the file system not directly passed to
	// the compiler (ie things that are referenced by other files)
	std::vector<testutil::harnessutil::TestFile*> otherFiles;
	bool hasNonDtsFiles = false;

	// compiler_runner.go verify methods (compiler_runner.cpp).
	void verifyDiagnostics(gostd::testing::T* t, std::string_view suiteName);
	void verifyContentMapper(gostd::testing::T* t,
	                         std::string_view suiteName);
	void verifyJavaScriptOutput(gostd::testing::T* t,
	                            std::string_view suiteName);
	void verifySourceMapOutput(gostd::testing::T* t,
	                           std::string_view suiteName);
	void verifySourceMapRecord(gostd::testing::T* t,
	                           std::string_view suiteName);
	void verifyTypesAndSymbols(gostd::testing::T* t,
	                           std::string_view suiteName);
	void verifyModuleResolution(gostd::testing::T* t,
	                            std::string_view suiteName);
	void verifyUnionOrdering(gostd::testing::T* t);
	void verifyParentPointers(gostd::testing::T* t);
	// contentMappedFileNames — compiler_runner.go:415: the set of absolute
	// file names produced by a content mapper (empty = none, like a nil
	// Go map).
	std::unordered_map<std::string, bool> contentMappedFileNames();
};

// getCompilerFileBasedTest — compiler_runner.go:221.
compilerFileBasedTest* getCompilerFileBasedTest(
    gostd::testing::T* t, const std::string& filename);

// newCompilerTest — compiler_runner.go:255.
compilerTest* newCompilerTest(
    gostd::testing::T* t, const std::string& testName,
    const std::string& filename, testCaseContent* testContent,
    testutil::harnessutil::NamedTestConfiguration* namedConfiguration);

// CompilerBaselineRunner — compiler_runner.go:50.
class CompilerBaselineRunner : public Runner {
public:
	CompilerBaselineRunner(std::string testSuitName,
	                       CompilerTestType testType);
	std::vector<std::string> EnumerateTestFiles() override;
	void RunTests(gostd::testing::T* t) override;

private:
	void runTest(gostd::testing::T* t, const std::string& filename);
	void runSingleConfigTest(
	    gostd::testing::T* t, const std::string& testName,
	    compilerFileBasedTest* test,
	    testutil::harnessutil::NamedTestConfiguration* config);
	void cleanUpLocal(gostd::testing::T* t);

	std::vector<std::string> testFiles_;
	std::string basePath_;
	std::string testSuitName_;
};

// NewCompilerBaselineRunner — compiler_runner.go:160.
CompilerBaselineRunner* NewCompilerBaselineRunner(CompilerTestType testType);

// === transpile_runner.go ===

// TranspileBaselineRunner — transpile_runner.go:30.
class TranspileBaselineRunner : public Runner {
public:
	TranspileBaselineRunner();
	std::vector<std::string> EnumerateTestFiles() override;
	void RunTests(gostd::testing::T* t) override;

private:
	void runTest(gostd::testing::T* t, const std::string& filename);
	void runKind(gostd::testing::T* t, const std::string& configuredName,
	             std::string_view extension,
	             const std::vector<testUnit*>& units,
	             CompilerOptions* options,
	             testutil::harnessutil::HarnessOptions* harnessOptions,
	             bool declaration);

	std::vector<std::string> testFiles_;
	std::string basePath_;
};

// formatTranspileConfigurationName — transpile_runner.go:97.
std::string formatTranspileConfigurationName(const std::string& name);

// appendTranspileSection — transpile_runner.go:170.
void appendTranspileSection(std::string& result, std::string_view fileName,
                            std::string_view content);

// cleanTranspileBaselines — transpile_runner.go:178.
void cleanTranspileBaselines();

// RunTranspileTests — transpile_runner.go:184.
void RunTranspileTests(gostd::testing::T* t);

// === compiler_runner.go helpers ===

// createHarnessTestFile — compiler_runner.go:509.
testutil::harnessutil::TestFile* createHarnessTestFile(
    const testUnit* unit, const std::string& currentDirectory);

}  // namespace tsc::testrunner
