// Port of tsc/internal/tsoptions/tsconfigparsing_test.go (package tsoptions_test).
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/types.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/diagnosticwriter/diagnosticwriter.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/parser/parser.h"
#include "internal/repo/paths.h"
#include "internal/scanner/scanner.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tsoptions/tsoptionstest/tsoptionstest.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/osvfs/osvfs.h"
#include "internal/vfs/vfs.h"

namespace {

using tsc::gostd::testing::T;
using tsc::CompilerOptions;
using tsc::Diagnostic;
using tsc::tsoptions::JsonArray;
using tsc::tsoptions::JsonGoMap;
using tsc::tsoptions::JsonGoMapPtr;
using tsc::tsoptions::JsonObjectPtr;
using tsc::tsoptions::JsonStrList;
using tsc::ScriptKind;
using tsc::SourceFile;
using tsc::SourceFileParseOptions;
using tsc::TextRange;
using tsc::Tristate;
namespace assert = tsc::gotest::assert;
namespace baseline = tsc::testutil::baseline;
namespace contentmapper = tsc::contentmapper;
namespace collections = tsc::collections;
namespace diagnostics = tsc;
namespace diagnosticwriter = tsc::diagnosticwriter;
namespace json = tsc::json;
namespace osvfs = tsc::vfs::osvfs;
namespace repo = tsc::repo;
namespace tsoptions = tsc::tsoptions;
namespace tsoptionstest = tsc::tsoptions::tsoptionstest;
namespace tspath = tsc::tspath;
namespace vfs = tsc::vfs;

struct testConfig {
	std::string jsonText;
	std::string configFileName;
	std::string basePath;
	std::unordered_map<std::string, std::string> allFileList;
	CompilerOptions* existingOptions = nullptr;
};

std::string join(const std::vector<std::string>& v, std::string_view sep) {
	std::string out;
	for (size_t i = 0; i < v.size(); i++) {
		if (i) out += sep;
		out += v[i];
	}
	return out;
}

bool contains(const std::vector<std::string>& v, std::string_view s) {
	return std::find(v.begin(), v.end(), s) != v.end();
}

template <typename Pred>
bool containsFunc(const std::vector<Diagnostic*>& v, Pred pred) {
	for (auto* d : v) {
		if (pred(d)) return true;
	}
	return false;
}

template <typename Pred>
Diagnostic* findDiag(const std::vector<Diagnostic*>& v, Pred pred) {
	for (auto* d : v) {
		if (pred(d)) return d;
	}
	return nullptr;
}

std::string formatDiagErrors(const std::vector<Diagnostic*>& errors) {
	auto wrapped = diagnosticwriter::fromASTDiagnostics(errors);
	std::vector<diagnosticwriter::Diagnostic*> diags;
	diags.reserve(wrapped.size());
	for (auto& w : wrapped) diags.push_back(w.get());
	std::ostringstream out;
	diagnosticwriter::FormattingOptions opts{.newLine = "\n"};
	diagnosticwriter::writeFormatDiagnostics(out, diags, &opts);
	return out.str();
}

// writeJsonReadableText — tsconfigparsing_test.go:1552.
std::string writeJsonReadableText(std::ostream& output,
                                  const tsoptions::CompilerOptionsValue& input) {
	return json::marshalIndentWrite(output, input, "", "  ");
}

// printFS — tsconfigparsing_test.go:1652.
vfs::Error printFS(std::ostream& output, vfs::FS& files,
                    const std::string& root) {
	return vfs::WalkDir(
	    files, root,
	    [&](const std::string& path,
	        const std::shared_ptr<vfs::DirEntry>& entry,
	        const vfs::Error& err) -> vfs::Error {
		    if (err) {
			    return err;
		    }
		    if (entry->Type().IsRegular()) {
			    auto content = files.ReadFile(path);
			    if (!content.second) {
				    return vfs::Error::newError("failed to read file " + path);
			    }
			    output << "//// [" << path << "]\r\n" << content.first
			           << "\r\n\r\n";
		    }
		    return vfs::Error{};
	    });
}

// getParsedWithJsonApi — tsconfigparsing_test.go:949.
tsoptions::ParsedCommandLine* getParsedWithJsonApi(
    const testConfig& config, tsoptions::ParseConfigHost* host,
    std::string_view basePath) {
	std::string configFileName = tspath::getNormalizedAbsolutePath(
	    config.configFileName, basePath);
	auto path = tspath::toPath(config.configFileName, basePath,
	                           host->FS()->UseCaseSensitiveFileNames());
	auto [parsed, parseErrors] = tsoptions::ParseConfigFileTextToJson(
	    configFileName, path, config.jsonText);
	return tsoptions::ParseJsonConfigFileContent(
	    parsed, host, basePath, config.existingOptions, configFileName,
	    /*resolutionStack*/ {},
	    /*extendedConfigCache*/ nullptr);
}

// getParsedWithJsonSourceFileApi — tsconfigparsing_test.go:1478.
tsoptions::ParsedCommandLine* getParsedWithJsonSourceFileApi(
    const testConfig& config, tsoptions::ParseConfigHost* host,
    std::string_view basePath) {
	std::string configFileName = tspath::getNormalizedAbsolutePath(
	    config.configFileName, basePath);
	auto path = tspath::toPath(config.configFileName, basePath,
	                           host->FS()->UseCaseSensitiveFileNames());
	auto* parsed = tsc::parseSourceFile(
	    SourceFileParseOptions{configFileName, path, {}}, config.jsonText,
	    ScriptKind::JSON);
	auto* tsConfigSourceFile =
	    new tsoptions::TsConfigSourceFile{.SourceFile = parsed};
	return tsoptions::ParseJsonSourceFileConfigFileContent(
	    tsConfigSourceFile, host, host->GetCurrentDirectory(),
	    config.existingOptions, nullptr, configFileName,
	    /*resolutionStack*/ {},
	    /*extendedConfigCache*/ nullptr);
}

using getParsedFn = std::function<tsoptions::ParsedCommandLine*(
    const testConfig&, tsoptions::ParseConfigHost*, std::string_view)>;

// baselineParseConfigWith — tsconfigparsing_test.go:1500.
void baselineParseConfigWith(T* t, const std::string& baselineFileName,
                             bool includeCompilerOptions,
                             const std::vector<testConfig>& input,
                             const getParsedFn& getParsed) {
	t->Helper();
	std::ostringstream baselineContent;
	for (size_t i = 0; i < input.size(); i++) {
		const testConfig& config = input[i];
		std::string basePath = config.basePath;
		if (basePath.empty()) {
			basePath = tspath::getNormalizedAbsolutePath(
			    tspath::getDirectoryPath(config.configFileName), "");
		}
		std::string configFileName =
		    tspath::combinePaths(basePath, {config.configFileName});
		std::unordered_map<std::string, std::string> allFileLists;
		allFileLists.reserve(config.allFileList.size() + 1);
		allFileLists.insert(config.allFileList.begin(),
		                    config.allFileList.end());
		allFileLists[configFileName] = config.jsonText;
		auto host = tsoptionstest::NewVFSParseConfigHost(
		    allFileLists, config.basePath, /*useCaseSensitiveFileNames*/ true);
		auto* parsedConfigFileContent = getParsed(config, host.get(), basePath);

		baselineContent << "Fs::\n";
		auto fsErr = printFS(baselineContent, *host->Vfs, "/");
		if (fsErr) {
			t->Fatal({fsErr.str()});
		}
		baselineContent << "\n";
		baselineContent << "configFileName:: " << config.configFileName
		                << "\n";
		if (includeCompilerOptions) {
			baselineContent << "CompilerOptions::\n";
			std::string err = json::marshalIndentWrite(
			    baselineContent,
			    parsedConfigFileContent->ParsedConfig->CompilerOptions, "",
			    "  ");
			if (!err.empty()) {
				t->Fatalf("Failed to write JSON text: %s", {err});
			}
			baselineContent << "\n\n";

			if (parsedConfigFileContent->ParsedConfig->TypeAcquisition !=
			    nullptr) {
				baselineContent << "TypeAcquisition::\n";
				err = json::marshalIndentWrite(
				    baselineContent,
				    parsedConfigFileContent->ParsedConfig
				        ->TypeAcquisition,
				    "", "  ");
				if (!err.empty()) {
					t->Fatalf("Failed to write JSON text: %s", {err});
				}
				baselineContent << "\n\n";
			}
		}
		baselineContent << "FileNames::\n";
		baselineContent << join(parsedConfigFileContent->ParsedConfig->FileNames,
		                        ",")
		                << "\n";
		baselineContent << "Errors::\n";
		auto wrapped = diagnosticwriter::fromASTDiagnostics(
		    parsedConfigFileContent->Errors);
		std::vector<diagnosticwriter::Diagnostic*> diags;
		diags.reserve(wrapped.size());
		for (auto& w : wrapped) diags.push_back(w.get());
		diagnosticwriter::FormattingOptions formatOpts{.newLine = "\r\n"};
		formatOpts.comparePathsOptions.currentDirectory = basePath;
		formatOpts.comparePathsOptions.useCaseSensitiveFileNames = true;
		diagnosticwriter::formatDiagnosticsWithColorAndContext(
		    baselineContent, diags, &formatOpts);
		baselineContent << "\n";
		if (i != input.size() - 1) {
			baselineContent << "\n";
		}
	}
	baseline::Run(t, baselineFileName, baselineContent.str(),
	              baseline::Options{.Subfolder = "config/tsconfigParsing"});
}

// --- parseConfigFileTextToJsonTests — tsconfigparsing_test.go:39 -------------
const std::vector<std::pair<std::string, std::vector<std::string>>>
    parseConfigFileTextToJsonTests = {
        {"returns empty config for file with only whitespaces", {"", " "}},
        {"returns empty config for file with comments only",
         {"// Comment", "/* Comment*/"}},
        {"returns empty config when config is empty object", {R"({})"}},
        {"returns config object without comments",
         {R"({ // Excluded files
            "exclude": [
                // Exclude d.ts
                "file.d.ts"
            ]
        })",
          R"({
            /* Excluded
                    Files
            */
            "exclude": [
                /* multiline comments can be in the middle of a line */"file.d.ts"
            ]
        })"}},
        {"keeps string content untouched",
         {R"({
            "exclude": [
                "xx//file.d.ts"
            ]
        })",
          R"({
            "exclude": [
                "xx/*file.d.ts*/"
            ]
        })"}},
        {"handles escaped characters in strings correctly",
         {R"({
            "exclude": [
                "xx\"//files"
            ]
        })",
          R"({
            "exclude": [
                "xx\\" // end of line comment
            ]
        })"}},
        {"returns object when users correctly specify library",
         {R"({
            "compilerOptions": {
                "lib": ["es5"]
            }
        })",
          R"({
            "compilerOptions": {
                "lib": ["es5", "es6"]
            }
        })"}},
};

// TestParseConfigFileTextToJson — tsconfigparsing_test.go:129.
void TestParseConfigFileTextToJson(T* t) {
	t->Parallel();
	for (auto& rec : parseConfigFileTextToJsonTests) {
		t->Run(rec.first, [&](T* t) {
			t->Parallel();
			std::ostringstream baselineContent;
			for (size_t i = 0; i < rec.second.size(); i++) {
				baselineContent << "Input::\n";
				baselineContent << rec.second[i];
				baselineContent << "\n";
				auto [parsed, errors] =
				    tsoptions::ParseConfigFileTextToJson(
				        "/apath/tsconfig.json", "/apath", rec.second[i]);
				baselineContent << "Config::\n";
				std::string werr =
				    writeJsonReadableText(baselineContent, parsed);
				if (!werr.empty()) {
					t->Fatalf("Failed to write JSON text: %s", {werr});
				}
				baselineContent << "\n";
				baselineContent << "Errors::\n";
				auto wrapped =
				    diagnosticwriter::fromASTDiagnostics(errors);
				std::vector<diagnosticwriter::Diagnostic*> diags;
				diags.reserve(wrapped.size());
				for (auto& w : wrapped) diags.push_back(w.get());
				diagnosticwriter::FormattingOptions opts{.newLine = "\n"};
				opts.comparePathsOptions.currentDirectory = "/";
				opts.comparePathsOptions.useCaseSensitiveFileNames = true;
				diagnosticwriter::formatDiagnosticsWithColorAndContext(
				    baselineContent, diags, &opts);
				baselineContent << "\n";
				if (i != rec.second.size() - 1) {
					baselineContent << "\n";
				}
			}
			baseline::Run(
			    t, rec.first + " jsonParse.js", baselineContent.str(),
			    baseline::Options{.Subfolder = "config/tsconfigParsing"});
		});
	}
}

// --- parseJsonConfigFileTests — tsconfigparsing_test.go:159 -------------------
struct parseJsonConfigTestCase {
	std::string title;
	bool includeCompilerOptions = false;
	std::vector<testConfig> input;
};

const std::string tsconfigWithExtends = R"({
  "files": ["/src/index.ts", "/src/app.ts"],
  "include": ["/src/**/*"],
  "exclude": [],
  "ts-node": {
    "compilerOptions": {
      "module": "commonjs"
    },
    "transpileOnly": true
  }
})";

const std::string tsconfigWithoutConfigDir = R"({
  "compilerOptions": {
    "outDir": "bin"
  }
})";

const std::string tsconfigWithConfigDir = R"({
  "compilerOptions": {
    "outDir": "${configDir}/bin"
  }
})";

const std::string tsconfigWithExtendsAndConfigDir = R"({
  "compilerOptions": {
    "outFile": "${configDir}/outFile",
    "outDir": "${configDir}/outDir",
    "rootDir": "${configDir}/rootDir",
    "tsBuildInfoFile": "${configDir}/tsBuildInfoFile",
    "baseUrl": "${configDir}/baseUrl",
    "declarationDir": "${configDir}/declarationDir",
  }
})";

const std::vector<parseJsonConfigTestCase> parseJsonConfigFileTests = {
    {
        .title = "ignore dotted files and folders",
        .input = {{
            .jsonText = "{}",
            .configFileName = "tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/test.ts", ""},
                            {"/apath/.git/a.ts", ""},
                            {"/apath/.b.ts", ""},
                            {"/apath/..c.ts", ""}},
        }},
    },
    {
        .title =
            "allow dotted files and folders when explicitly requested",
        .input = {{
            .jsonText = R"({
                    "files": ["/apath/.git/a.ts", "/apath/.b.ts", "/apath/..c.ts"]
                })",
            .configFileName = "tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/test.ts", ""},
                            {"/apath/.git/a.ts", ""},
                            {"/apath/.b.ts", ""},
                            {"/apath/..c.ts", ""}},
        }},
    },
    {
        .title = "implicitly exclude common package folders",
        .input = {{
            .jsonText = "{}",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList = {{"/node_modules/a.ts", ""},
                            {"/bower_components/b.ts", ""},
                            {"/jspm_packages/c.ts", ""},
                            {"/d.ts", ""},
                            {"/folder/e.ts", ""}},
        }},
    },
    {
        .title = "generates errors for empty files list",
        .input = {{
            .jsonText = R"({
                "files": []
            })",
            .configFileName = "/apath/tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/a.ts", ""}},
        }},
    },
    {
        .title =
            "generates errors for empty files list when no references are provided",
        .input = {{
            .jsonText = R"({
                "files": [],
                "references": []
            })",
            .configFileName = "/apath/tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/a.ts", ""}},
        }},
    },
    {
        .title = "generates errors for directory with no .ts files",
        .input = {{
            .jsonText = R"({
            })",
            .configFileName = "/apath/tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/a.js", ""}},
        }},
    },
    {
        .title = "generates errors for empty include",
        .input = {{
            .jsonText = R"({
                "include": []
            })",
            .configFileName = "/apath/tsconfig.json",
            .basePath = "tests/cases/unittests",
            .allFileList = {{"/apath/a.ts", ""}},
        }},
    },
    {
        .title =
            "generates errors for include with parent directory after recursive wildcard",
        .input = {{
            .jsonText = R"({
                "include": ["**/../*.ts"]
            })",
            .configFileName = "/apath/tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/main.ts", ""}},
        }},
    },
    {
        .title =
            "parses tsconfig with compilerOptions, files, include, and exclude",
        .includeCompilerOptions = true,
        .input = {{
            .jsonText = R"({
  "compilerOptions": {
    "outDir": "./dist",
    "strict": true,
    "noImplicitAny": true,
    "target": "ES2017",
    "module": "ESNext",
    "moduleResolution": "bundler",
    "moduleDetection": "auto",
    "jsx": "react",
	"maxNodeModuleJsDepth": 1,
	"paths": {
      "jquery": ["./vendor/jquery/dist/jquery"]
    }
  },
  "files": ["/apath/src/index.ts", "/apath/src/app.ts"],
  "include": ["/apath/src/**/*"],
  "exclude": ["/apath/node_modules", "/apath/dist"]
})",
            .configFileName = "/apath/tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/src/index.ts", ""},
                            {"/apath/src/app.ts", ""},
                            {"/apath/node_modules/module.ts", ""},
                            {"/apath/dist/output.js", ""}},
        }},
    },
    {
        .title = "generates errors when commandline option is in tsconfig",
        .input = {{
            .jsonText = R"({
  "compilerOptions": {
    "help": true
  }
})",
            .configFileName = "/apath/tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/a.ts", ""}},
        }},
    },
    {
        .title =
            "does not generate errors for empty files list when one or more references are provided",
        .input = {{
            .jsonText = R"({
                "files": [],
                "references": [{ "path": "/apath" }]
            })",
            .configFileName = "/apath/tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/a.ts", ""}},
        }},
    },
    {
        .title = "exclude outDir unless overridden",
        .input =
            {
                {
                    .jsonText = R"({
                "compilerOptions": {
                    "outDir": "bin"
                }
            })",
                    .configFileName = "tsconfig.json",
                    .basePath = "/",
                    .allFileList = {{"/bin/a.ts", ""}, {"/b.ts", ""}},
                },
                {
                    .jsonText = R"({
                "compilerOptions": {
                    "outDir": "bin"
                },
                "exclude": [ "obj" ]
            })",
                    .configFileName = "tsconfig.json",
                    .basePath = "/",
                    .allFileList = {{"/bin/a.ts", ""}, {"/b.ts", ""}},
                },
            },
    },
    {
        .title = "exclude declarationDir unless overridden",
        .input =
            {
                {
                    .jsonText = R"({
                "compilerOptions": {
                    "declarationDir": "declarations"
                }
            })",
                    .configFileName = "tsconfig.json",
                    .basePath = "/",
                    .allFileList = {{"/declarations/a.d.ts", ""},
                                    {"/a.ts", ""}},
                },
                {
                    .jsonText = R"({
                "compilerOptions": {
                    "declarationDir": "declarations"
                },
                "exclude": [ "types" ]
            })",
                    .configFileName = "tsconfig.json",
                    .basePath = "/",
                    .allFileList = {{"/declarations/a.d.ts", ""},
                                    {"/a.ts", ""}},
                },
            },
    },
    {
        .title = "generates errors for empty directory",
        .input = {{
            .jsonText = R"({
                "compilerOptions": {
                    "allowJs": true
                }
            })",
            .configFileName = "/apath/tsconfig.json",
            .basePath = "/apath",
            .allFileList = {},
        }},
    },
    {
        .title = "generates errors for includes with outDir",
        .input = {{
            .jsonText = R"({
                "compilerOptions": {
                    "outDir": "./"
                },
                "include": ["**/*"]
            })",
            .configFileName = "/apath/tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/a.ts", ""}},
        }},
    },
    {
        .title = "generates errors when include is not string",
        .input = {{
            .jsonText = R"({
  "include": [
    [
      "./**/*.ts"
    ]
  ]
})",
            .configFileName = "/apath/tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/a.ts", ""}},
        }},
    },
    {
        .title = "generates errors when files is not string",
        .input = {{
            .jsonText = R"({
  "files": [
    {
      "compilerOptions": {
        "experimentalDecorators": true,
        "allowJs": true
      }
    }
  ]
})",
            .configFileName = "/apath/tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/a.ts", ""}},
        }},
    },
    {
        .title = "with outDir from base tsconfig",
        .input =
            {
                {
                    .jsonText = R"({
  "extends": "./tsconfigWithoutConfigDir.json"
})",
                    .configFileName = "tsconfig.json",
                    .basePath = "/",
                    .allFileList =
                        {{"/tsconfigWithoutConfigDir.json",
                          tsconfigWithoutConfigDir},
                         {"/bin/a.ts", ""},
                         {"/b.ts", ""}},
                },
                {
                    .jsonText = R"({
  "extends": "./tsconfigWithConfigDir.json"
})",
                    .configFileName = "tsconfig.json",
                    .basePath = "/",
                    .allFileList = {{"/tsconfigWithConfigDir.json",
                                     tsconfigWithConfigDir},
                                    {"/bin/a.ts", ""},
                                    {"/b.ts", ""}},
                },
            },
    },
    {
        .title = "returns error when tsconfig have excludes",
        .input = {{
            .jsonText = R"({
                    "compilerOptions": {
                        "lib": ["es5"]
                    },
                    "excludes": [
                        "foge.ts"
                    ]
                })",
            .configFileName = "tsconfig.json",
            .basePath = "/apath",
            .allFileList = {{"/apath/test.ts", ""}, {"/apath/foge.ts", ""}},
        }},
    },
    {
        .title =
            "parses tsconfig with extends, files, include and other options",
        .includeCompilerOptions = true,
        .input = {{
            .jsonText = R"({
				"extends": "./tsconfigWithExtends.json",
				"compilerOptions": {
				    "outDir": "./dist",
    				"strict": true,
    				"noImplicitAny": true,
					"baseUrl": "",
				},
			})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList = {{"/tsconfigWithExtends.json", tsconfigWithExtends},
                            {"/src/index.ts", ""},
                            {"/src/app.ts", ""},
                            {"/node_modules/module.ts", ""},
                            {"/dist/output.js", ""}},
        }},
    },
    {
        .title = "parses tsconfig with extends and configDir",
        .includeCompilerOptions = true,
        .input = {{
            .jsonText = R"({
				"extends": "./tsconfig.base.json"
			})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList = {{"/tsconfig.base.json",
                              tsconfigWithExtendsAndConfigDir},
                            {"/src/index.ts", ""},
                            {"/src/app.ts", ""},
                            {"/node_modules/module.ts", ""},
                            {"/dist/output.js", ""}},
        }},
    },
    {
        .title = "reports error for an unknown option",
        .input = {{
            .jsonText = R"({
			    "compilerOptions": {
				"unknown": true
			    }
			})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList = {{"/app.ts", ""}},
        }},
    },
    {
        .title = "reports spelling suggestion for an unknown option",
        .input = {{
            .jsonText = R"({
			    "compilerOptions": {
				"targt": 1
			    }
			})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList = {{"/app.ts", ""}},
        }},
    },
    {
        .title =
            "reports errors for wrong type option and invalid enum value",
        .input = {{
            .jsonText = R"({
			    "compilerOptions": {
				"target": "invalid value",
				"removeComments": "should be a boolean",
				"moduleResolution": "invalid value"
			    }
			})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList = {{"/app.ts", ""}},
        }},
    },
    {
        .title = "reports errors for incorrectly cased option names",
        .includeCompilerOptions = true,
        .input = {{
            .jsonText = R"({
			    "compilerOptions": {
				"sourcemap": true,
				"declarationmap": true,
				"nouncheckedindexedaccess": true,
				"exactoptionalpropertytypes": true,
				"verbatimmodulesyntax": true,
				"isolatedmodules": true,
				"nouncheckedsideeffectimports": true,
				"moduledetection": "force",
				"skiplibcheck": true,
				"checkjs": true
			    }
			})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList = {{"/app.ts", ""}},
        }},
    },
    {
        .title = "handles empty types array",
        .includeCompilerOptions = true,
        .input = {{
            .jsonText = R"({
			    "compilerOptions": {
					"types": []
				}
			})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList = {{"/app.ts", ""}},
        }},
    },
    {
        .title = "issue 1267 scenario - extended files not picked up",
        .includeCompilerOptions = true,
        .input = {{
            .jsonText = R"({
  "extends": "./tsconfig-base/backend.json",
  "compilerOptions": {
    "baseUrl": "./",
    "outDir": "dist",
    "rootDir": "src",
    "resolveJsonModule": true
  },
  "exclude": ["node_modules", "dist"],
  "include": ["src/**/*"]
})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList =
                {
                    {"/tsconfig-base/backend.json", R"({
  "$schema": "https://json.schemastore.org/tsconfig",
  "display": "Backend",
  "compilerOptions": {
    "allowJs": true,
    "module": "nodenext",
    "removeComments": true,
    "emitDecoratorMetadata": true,
    "experimentalDecorators": true,
    "allowSyntheticDefaultImports": true,
    "target": "esnext",
    "lib": ["ESNext"],
    "incremental": false,
    "esModuleInterop": true,
    "noImplicitAny": true,
    "moduleResolution": "nodenext",
    "types": ["node", "vitest/globals"],
    "sourceMap": true,
    "strictPropertyInitialization": false
  },
  "files": [
    "types/ical2json.d.ts",
    "types/express.d.ts",
    "types/multer.d.ts",
    "types/reset.d.ts",
    "types/stripe-custom-typings.d.ts",
    "types/nestjs-modules.d.ts",
    "types/luxon.d.ts",
    "types/nestjs-pino.d.ts"
  ],
  "ts-node": {
    "files": true
  }
})"},
                    {"/tsconfig-base/types/ical2json.d.ts", "export {}"},
                    {"/tsconfig-base/types/express.d.ts", "export {}"},
                    {"/tsconfig-base/types/multer.d.ts", "export {}"},
                    {"/tsconfig-base/types/reset.d.ts", "export {}"},
                    {"/tsconfig-base/types/stripe-custom-typings.d.ts",
                     "export {}"},
                    {"/tsconfig-base/types/nestjs-modules.d.ts", "export {}"},
                    {"/tsconfig-base/types/luxon.d.ts",
                     R"(declare module 'luxon' {
  interface TSSettings {
    throwOnInvalid: true
  }
}
export {})"},
                    {"/tsconfig-base/types/nestjs-pino.d.ts", "export {}"},
                    {"/src/main.ts", "export {}"},
                    {"/src/utils.ts", "export {}"},
                },
        }},
    },
    {
        .title = "null overrides in extended tsconfig - array fields",
        .includeCompilerOptions = true,
        .input = {{
            .jsonText = R"({
  "extends": "./tsconfig-base.json",
  "compilerOptions": {
    "types": null,
    "lib": null,
    "typeRoots": null
  }
})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList =
                {
                    {"/tsconfig-base.json", R"({
  "compilerOptions": {
    "types": ["node", "@types/jest"],
    "lib": ["es2020", "dom"],
    "typeRoots": ["./types", "./node_modules/@types"]
  }
})"},
                    {"/app.ts", ""},
                },
        }},
    },
    {
        .title = "null overrides in extended tsconfig - string fields",
        .includeCompilerOptions = true,
        .input = {{
            .jsonText = R"({
  "extends": "./tsconfig-base.json",
  "compilerOptions": {
    "outDir": null,
    "baseUrl": null,
    "rootDir": null
  }
})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList =
                {
                    {"/tsconfig-base.json", R"({
  "compilerOptions": {
    "outDir": "./dist",
    "baseUrl": "./src",
    "rootDir": "./src"
  }
})"},
                    {"/app.ts", ""},
                },
        }},
    },
    {
        .title = "null overrides in extended tsconfig - mixed field types",
        .includeCompilerOptions = true,
        .input = {{
            .jsonText = R"({
  "extends": "./tsconfig-base.json",
  "compilerOptions": {
    "types": null,
    "outDir": null,
    "strict": false,
    "lib": ["es2022"],
    "allowJs": null
  }
})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList =
                {
                    {"/tsconfig-base.json", R"({
  "compilerOptions": {
    "types": ["node"],
    "lib": ["es2020", "dom"],
    "outDir": "./dist",
    "strict": true,
    "allowJs": true,
    "target": "es2020"
  }
})"},
                    {"/app.ts", ""},
                },
        }},
    },
    {
        .title = "null overrides with multiple extends levels",
        .includeCompilerOptions = true,
        .input = {{
            .jsonText = R"({
  "extends": "./tsconfig-middle.json",
  "compilerOptions": {
    "types": null,
    "lib": null
  }
})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList =
                {
                    {"/tsconfig-middle.json", R"({
  "extends": "./tsconfig-base.json",
  "compilerOptions": {
    "types": ["jest"],
    "outDir": "./build"
  }
})"},
                    {"/tsconfig-base.json", R"({
  "compilerOptions": {
    "types": ["node"],
    "lib": ["es2020"],
    "outDir": "./dist",
    "strict": true
  }
})"},
                    {"/app.ts", ""},
                },
        }},
    },
    {
        .title = "null overrides in middle level of extends chain",
        .includeCompilerOptions = true,
        .input = {{
            .jsonText = R"({
  "extends": "./tsconfig-middle.json",
  "compilerOptions": {
    "outDir": "./final"
  }
})",
            .configFileName = "tsconfig.json",
            .basePath = "/",
            .allFileList =
                {
                    {"/tsconfig-middle.json", R"({
  "extends": "./tsconfig-base.json",
  "compilerOptions": {
    "types": null,
    "lib": null,
    "outDir": "./middle"
  }
})"},
                    {"/tsconfig-base.json", R"({
  "compilerOptions": {
    "types": ["node"],
    "lib": ["es2020"],
    "outDir": "./base",
    "strict": true
  }
})"},
                    {"/app.ts", ""},
                },
        }},
    },
};

// TestParseJsonConfigFileContent — tsconfigparsing_test.go:818.
void TestParseJsonConfigFileContent(T* t) {
	t->Parallel();
	for (auto& rec : parseJsonConfigFileTests) {
		t->Run(rec.title + " with json api", [&](T* t) {
			t->Parallel();
			baselineParseConfigWith(t, rec.title + " with json api.js",
			                        rec.includeCompilerOptions, rec.input,
			                        getParsedWithJsonApi);
		});
	}
}

// TestParseJsonConfigFileContentAcceptsJsonRepresentations —
// tsconfigparsing_test.go:828.
void TestParseJsonConfigFileContentAcceptsJsonRepresentations(T* t) {
	t->Parallel();

	auto host = tsoptionstest::NewVFSParseConfigHost(
	    {{"/project/index.ts", "export {};"}}, "/project",
	    /*useCaseSensitiveFileNames*/ true);

	auto [orderedMap, parseErrors] = tsoptions::ParseConfigFileTextToJson(
	    "/project/tsconfig.json", "/project/tsconfig.json",
	    R"({"compilerOptions":{"strict":true},"files":["index.ts"]})");
	assert::Equal(t, (int)parseErrors.size(), 0);

	auto orderedMapWithTypedSlices =
	    std::make_shared<collections::OrderedMap<std::string,
	                                             tsoptions::CompilerOptionsValue>>();
	orderedMapWithTypedSlices->Set(
	    "compilerOptions",
	    tsoptions::CompilerOptionsValue{JsonGoMapPtr(new JsonGoMap{
	        {"strict", tsoptions::CompilerOptionsValue{true}}})});
	orderedMapWithTypedSlices->Set(
	    "files", tsoptions::CompilerOptionsValue{JsonStrList{"index.ts"}});

	std::vector<std::pair<std::string, tsoptions::CompilerOptionsValue>>
	    tests = {
	        {"ordered map", orderedMap},
	        {"ordered map with typed slices",
	         tsoptions::CompilerOptionsValue{JsonObjectPtr(
	             orderedMapWithTypedSlices)}},
	        {"plain map",
	         tsoptions::CompilerOptionsValue{JsonGoMapPtr(new JsonGoMap{
	             {"compilerOptions",
	              tsoptions::CompilerOptionsValue{JsonGoMapPtr(
	                  new JsonGoMap{{"strict",
	                                 tsoptions::CompilerOptionsValue{true}}})}},
	             {"files",
	              tsoptions::CompilerOptionsValue{JsonArray{
	                  tsoptions::CompilerOptionsValue{"index.ts"}}}}})}},
	        {"typed slices",
	         tsoptions::CompilerOptionsValue{JsonGoMapPtr(new JsonGoMap{
	             {"compilerOptions",
	              tsoptions::CompilerOptionsValue{JsonGoMapPtr(
	                  new JsonGoMap{{"strict",
	                                 tsoptions::CompilerOptionsValue{true}}})}},
	             {"files",
	              tsoptions::CompilerOptionsValue{JsonStrList{"index.ts"}}}})}},
	};
	for (auto& [name, json] : tests) {
		t->Run(name, [&, name = name, json = json](T* t) {
			t->Parallel();
			auto* parsed = tsoptions::ParseJsonConfigFileContent(
			    json, host.get(), "/project", nullptr,
			    "/project/tsconfig.json",
			    /*resolutionStack*/ {},
			    /*extendedConfigCache*/ nullptr);
			assert::DeepEqual(t, parsed->FileNames(),
			                  std::vector<std::string>{"/project/index.ts"});
			assert::Assert(
			    t, tsc::tristateIsTrue(parsed->CompilerOptions()->Strict), "");
			assert::Equal(t, (int)parsed->Errors.size(), 0);
		});
	}
}

// TestParseJsonConfigFileContentPreservesRaw — tsconfigparsing_test.go:877.
void TestParseJsonConfigFileContentPreservesRaw(T* t) {
	t->Parallel();

	auto host = tsoptionstest::NewVFSParseConfigHost(
	    {{"/project/index.ts", "export {};"}}, "/project",
	    /*useCaseSensitiveFileNames*/ true);

	auto* parsed = tsoptions::ParseJsonConfigFileContent(
	    tsoptions::CompilerOptionsValue{JsonGoMapPtr(new JsonGoMap{
	        {"files", tsoptions::CompilerOptionsValue{JsonArray{
	                      tsoptions::CompilerOptionsValue{"index.ts"}}}},
	        {"customSetting",
	         tsoptions::CompilerOptionsValue{JsonGoMapPtr(
	             new JsonGoMap{{"enabled",
	                            tsoptions::CompilerOptionsValue{true}}})}},
	        {"compileOnSave", tsoptions::CompilerOptionsValue{true}}})},
	    host.get(), "/project", nullptr, "/project/tsconfig.json",
	    /*resolutionStack*/ {},
	    /*extendedConfigCache*/ nullptr);

	assert::Equal(t, (int)parsed->Errors.size(), 0);
	assert::Assert(t, parsed->CompileOnSave != nullptr &&
	                      *parsed->CompileOnSave,
	               "");

	auto* raw = std::get_if<JsonObjectPtr>(&parsed->Raw.v);
	assert::Assert(t, raw != nullptr && *raw != nullptr,
	               "raw is not an ordered map");
	assert::DeepEqual(
	    t, std::vector<std::string>((*raw)->Keys()),
	    std::vector<std::string>{"compileOnSave", "customSetting", "files"});
	assert::Assert(t, (*raw)->Has("customSetting"), "");
}

// TestParseJsonConfigFileContentHandlesNullArrayElements —
// tsconfigparsing_test.go:906.
void TestParseJsonConfigFileContentHandlesNullArrayElements(T* t) {
	t->Parallel();

	auto host = tsoptionstest::NewVFSParseConfigHost(
	    {{"/project/index.ts", "export {};"}}, "/project",
	    /*useCaseSensitiveFileNames*/ true);
	for (auto& property : {"files", "include", "exclude"}) {
		t->Run(property, [&, property = std::string(property)](T* t) {
			t->Parallel();
			auto* parsed = tsoptions::ParseJsonConfigFileContent(
			    tsoptions::CompilerOptionsValue{JsonGoMapPtr(new JsonGoMap{
			        {property,
			         tsoptions::CompilerOptionsValue{JsonArray{
			             tsoptions::CompilerOptionsValue{}}}}})},
			    host.get(), "/project", nullptr, "/project/tsconfig.json",
			    /*resolutionStack*/ {},
			    /*extendedConfigCache*/ nullptr);
			assert::Assert(t, parsed->Errors.size() > 0, "");
			assert::Equal(
			    t, parsed->Errors[0]->Code(),
			    diagnostics::Compiler_option_0_requires_a_value_of_type_1
			        ->code);
		});
	}
}

// TestParseJsonConfigFileContentDefaultsCompileOnSaveToFalse —
// tsconfigparsing_test.go:930.
void TestParseJsonConfigFileContentDefaultsCompileOnSaveToFalse(T* t) {
	t->Parallel();

	auto host = tsoptionstest::NewVFSParseConfigHost(
	    {{"/project/index.ts", "export {};"}}, "/project",
	    /*useCaseSensitiveFileNames*/ true);
	auto* parsed = tsoptions::ParseJsonConfigFileContent(
	    tsoptions::CompilerOptionsValue{JsonGoMapPtr(new JsonGoMap{
	        {"files", tsoptions::CompilerOptionsValue{JsonStrList{"index.ts"}}}})},
	    host.get(), "/project", nullptr, "/project/tsconfig.json",
	    /*resolutionStack*/ {},
	    /*extendedConfigCache*/ nullptr);
	assert::Assert(t, parsed->CompileOnSave != nullptr, "");
	assert::Equal(t, *parsed->CompileOnSave, false);
}

// TestParseJsonSourceFileConfigFileContent — tsconfigparsing_test.go:964.
void TestParseJsonSourceFileConfigFileContent(T* t) {
	t->Parallel();
	for (auto& rec : parseJsonConfigFileTests) {
		t->Run(rec.title + " with jsonSourceFile api", [&](T* t) {
			t->Parallel();
			baselineParseConfigWith(t,
			                        rec.title + " with jsonSourceFile api.js",
			                        rec.includeCompilerOptions, rec.input,
			                        getParsedWithJsonSourceFileApi);
		});
	}
}

// TestParseJsonSourceFileConfigFileContentReportsInvalidExtendedConfig —
// tsconfigparsing_test.go:974.
void TestParseJsonSourceFileConfigFileContentReportsInvalidExtendedConfig(
    T* t) {
	t->Parallel();
	std::unordered_map<std::string, std::string> files = {
	    {"/project/tsconfig.json", R"({
  "extends": "./bad.json"
})"},
	    // The parser recovers from this as object-like JSON, producing
	    // expected-token errors for ':', ',', ',', and '}'.
	    {"/project/bad.json", "{ this is not json"},
	    {"/project/main.ts", "export const x = 1;"},
	};
	auto host = tsoptionstest::NewVFSParseConfigHost(files, "/project",
	                                               /*useCaseSensitiveFileNames*/
	                                               true);
	std::string configFileName = "/project/tsconfig.json";
	auto* configFile = tsoptions::NewTsconfigSourceFileFromFilePath(
	    configFileName,
	    tspath::toPath(configFileName, host->GetCurrentDirectory(),
	                   host->FS()->UseCaseSensitiveFileNames()),
	    files[configFileName]);

	auto* parsed = tsoptions::ParseJsonSourceFileConfigFileContent(
	    configFile, host.get(), host->GetCurrentDirectory(), nullptr, nullptr,
	    configFileName, {}, nullptr);

	auto parseErrors = tsc::Filter(
	    parsed->Errors, [](Diagnostic* diagnostic) {
		    return diagnostic->Code() == diagnostics::X_0_expected->code;
	    });
	std::vector<std::string> expectedParseErrorMessages = {":", ",", ",",
	                                                     "}"};
	std::vector<int> expectedParseErrorPositions = {7, 10, 14, 18};
	assert::Equal(t, (int)expectedParseErrorMessages.size(),
	              (int)parseErrors.size());
	assert::DeepEqual(
	    t,
	    tsc::mapVec<std::string>(
	        parseErrors,
	        [](Diagnostic* diagnostic) {
		        return diagnostic->MessageArgs()[0];
	        }),
	    expectedParseErrorMessages);
	assert::DeepEqual(
	    t,
	    tsc::mapVec<int>(parseErrors,
	                     [](Diagnostic* d) { return d->Pos(); }),
	    expectedParseErrorPositions);
	for (auto* diagnostic : parseErrors) {
		assert::Equal(t, diagnostic->File()->FileName(),
		              "/project/bad.json");
	}
}

// TestParseJsonSourceFileConfigFileContentWithEmptyExtendedConfig —
// tsconfigparsing_test.go:1020.
void TestParseJsonSourceFileConfigFileContentWithEmptyExtendedConfig(T* t) {
	t->Parallel();
	std::unordered_map<std::string, std::string> files = {
	    {"/project/tsconfig.json", R"({
  "extends": "./base.json"
})"},
	    {"/project/base.json", ""},
	    {"/project/main.ts", "export const x = 1;"},
	};
	auto host = tsoptionstest::NewVFSParseConfigHost(files, "/project",
	                                               /*useCaseSensitiveFileNames*/
	                                               true);
	std::string configFileName = "/project/tsconfig.json";
	auto* configFile = tsoptions::NewTsconfigSourceFileFromFilePath(
	    configFileName,
	    tspath::toPath(configFileName, host->GetCurrentDirectory(),
	                   host->FS()->UseCaseSensitiveFileNames()),
	    files[configFileName]);

	auto* parsed = tsoptions::ParseJsonSourceFileConfigFileContent(
	    configFile, host.get(), host->GetCurrentDirectory(), nullptr, nullptr,
	    configFileName, {}, nullptr);

	assert::Assert(t, parsed != nullptr, "");
	assert::DeepEqual(t, parsed->FileNames(),
	                  std::vector<std::string>{"/project/main.ts"});
}

// TestParseJsonSourceFileConfigFileContentDoesNotDuplicateUnquotedKeyDiagnostics
// — tsconfigparsing_test.go:1051.
void TestParseJsonSourceFileConfigFileContentDoesNotDuplicateUnquotedKeyDiagnostics(
    T* t) {
	t->Parallel();
	auto* parsed = tsoptionstest::GetParsedCommandLine(
	    R"({
  compilerOptions: {
    strict: true
  }
})",
	    {{"/main.ts", "export const x = 1;"}}, "/",
	    /*useCaseSensitiveFileNames*/ true);

	auto diags = parsed->GetConfigFileParsingDiagnostics();
	assert::Equal(t, (int)diags.size(), 2);
	std::vector<std::pair<int, int>> expectedLocations = {
	    {1, 2},
	    {2, 4},
	};
	for (size_t index = 0; index < diags.size(); index++) {
		auto* diagnostic = diags[index];
		assert::Equal(
		    t, diagnostic->Code(),
		    diagnostics::String_literal_with_double_quotes_expected->code);
		auto [line, character] =
		    tsc::getECMALineAndUTF16CharacterOfPosition(
		        diagnostic->File(), diagnostic->Pos());
		assert::Equal(t, line, expectedLocations[index].first);
		assert::Equal(t, (int)character, expectedLocations[index].second);
	}
}

// TestParseJsonSourceFileConfigFileContentReportsQuestionTokenDiagnostics —
// tsconfigparsing_test.go:1076.
void TestParseJsonSourceFileConfigFileContentReportsQuestionTokenDiagnostics(
    T* t) {
	t->Parallel();
	auto* parsed = tsoptionstest::GetParsedCommandLine(
	    R"({
  compilerOptions?: {
    strict?: true
  }
})",
	    {{"/main.ts", "export const x = 1;"}}, "/",
	    /*useCaseSensitiveFileNames*/ true);

	std::vector<Diagnostic*> questionTokenDiagnostics;
	for (auto* diagnostic : parsed->GetConfigFileParsingDiagnostics()) {
		if (diagnostic->Code() ==
		    diagnostics::The_0_modifier_can_only_be_used_in_TypeScript_files
		        ->code) {
			questionTokenDiagnostics.push_back(diagnostic);
		}
	}
	assert::Equal(t, (int)questionTokenDiagnostics.size(), 2);
	std::vector<std::pair<int, int>> expectedLocations = {
	    {1, 17},
	    {2, 10},
	};
	for (size_t index = 0; index < questionTokenDiagnostics.size(); index++) {
		auto* diagnostic = questionTokenDiagnostics[index];
		auto [line, character] =
		    tsc::getECMALineAndUTF16CharacterOfPosition(
		        diagnostic->File(), diagnostic->Pos());
		assert::Equal(t, line, expectedLocations[index].first);
		assert::Equal(t, (int)character, expectedLocations[index].second);
	}
}

// TestParseNullEnumCompilerOptions — tsconfigparsing_test.go:1105.
void TestParseNullEnumCompilerOptions(T* t) {
	t->Parallel();

	testConfig config{
	    .jsonText = R"({
			"compilerOptions": {
				"target": null,
				"module": null
			}
		})",
	    .configFileName = "tsconfig.json",
	    .basePath = "/",
	    .allFileList = {{"/app.ts", ""}},
	};
	for (auto& [name, getParsed] : std::vector<std::pair<std::string, getParsedFn>>{
	         {"json api", getParsedWithJsonApi},
	         {"jsonSourceFile api", getParsedWithJsonSourceFileApi},
	     }) {
		t->Run(name, [&, name = name, getParsed = getParsed](T* t) {
			t->Parallel();
			std::unordered_map<std::string, std::string> allFileLists(
			    config.allFileList);
			allFileLists["/tsconfig.json"] = config.jsonText;
			auto host = tsoptionstest::NewVFSParseConfigHost(
			    allFileLists, config.basePath,
			    /*useCaseSensitiveFileNames*/ true);
			auto* parsedConfigFileContent =
			    getParsed(config, host.get(), config.basePath);
			assert::Equal(t, (int)parsedConfigFileContent->Errors.size(), 0);
		});
	}
}

// TestContentMappers — tsconfigparsing_test.go:1136.
void TestContentMappers(T* t) {
	t->Parallel();

	testConfig config{
	    .jsonText = R"({
			"contentMappers": [
				{ "package": "vue-mapper", "extensions": [".vue"], "options": { "strictTemplates": true } }
			],
			"include": ["src"]
		})",
	    .configFileName = "tsconfig.json",
	    .basePath = "/",
	    .allFileList =
	        {
	            {"/src/app.ts", "export {}"},
	            {"/src/Component.vue", "<template></template>"},
	            {"/node_modules/vue-mapper/package.json",
	             R"({ "name": "vue-mapper", "version": "1.2.3", "typescript": { "contentMapper": { "exec": ["node", "./mapper.js"], "dynamicConfig": true } } })"},
	        },
	    .existingOptions =
	        new CompilerOptions{.RunExternalCode = Tristate::True},
	};
	for (auto& [name, getParsed] : std::vector<std::pair<std::string, getParsedFn>>{
	         {"json api", getParsedWithJsonApi},
	         {"jsonSourceFile api", getParsedWithJsonSourceFileApi},
	     }) {
		t->Run(name, [&, name = name, getParsed = getParsed](T* t) {
			t->Parallel();
			std::unordered_map<std::string, std::string> allFileLists(
			    config.allFileList);
			allFileLists["/tsconfig.json"] = config.jsonText;
			auto host = tsoptionstest::NewVFSParseConfigHost(
			    allFileLists, config.basePath,
			    /*useCaseSensitiveFileNames*/ true);
			auto* parsed = getParsed(config, host.get(), config.basePath);

			assert::Equal(t, (int)parsed->Errors.size(), 0);

			auto mappers = parsed->ContentMappers();
			assert::Equal(t, (int)mappers.size(), 1);
			assert::Equal(t, mappers[0]->Definition.Package, "vue-mapper");
			assert::DeepEqual(t, mappers[0]->Definition.Extensions,
			                  std::vector<std::string>{".vue"});
			assert::Equal(t, std::string(mappers[0]->Definition.Options),
			              R"({"strictTemplates":true})");
			assert::DeepEqual(t, parsed->ContentMapperExtensions(),
			                  std::vector<std::string>{".vue"});

			// The package.json is resolved during parsing, populating
			// name, version, and exec.
			assert::Equal(t, mappers[0]->Manifest.Name, "vue-mapper");
			assert::Equal(t, mappers[0]->Manifest.Version, "1.2.3");
			assert::DeepEqual(t, mappers[0]->Manifest.Exec,
			                  std::vector<std::string>{"node", "./mapper.js"});
			assert::Equal(t, mappers[0]->Manifest.DynamicConfig, true);

			// File list picks up .vue through the mapper extension.
			assert::Assert(
			    t, contains(parsed->FileNames(), "/src/Component.vue"), "");
		});
	}
}

// TestContentMapperOptionDiagnosticLocation — tsconfigparsing_test.go:1191.
void TestContentMapperOptionDiagnosticLocation(T* t) {
	t->Parallel();
	testConfig config{
	    .jsonText = R"({
			"contentMappers": [{
				"package": "mapper",
				"extensions": [".vue"],
				"options": { "plugins": [{ "name": 1 }] }
			}]
		})",
	    .configFileName = "tsconfig.json",
	    .basePath = "/",
	    .allFileList =
	        {
	            {"/index.ts", "export {};"},
	            {"/node_modules/mapper/package.json",
	             R"({ "name": "mapper", "version": "1.0.0", "typescript": { "contentMapper": { "exec": ["mapper"] } } })"},
	        },
	    .existingOptions =
	        new CompilerOptions{.RunExternalCode = Tristate::True},
	};
	auto host = tsoptionstest::NewVFSParseConfigHost(config.allFileList,
	                                               config.basePath,
	                                               /*useCaseSensitiveFileNames*/
	                                               true);
	auto* parsed =
	    getParsedWithJsonSourceFileApi(config, host.get(), config.basePath);
	auto [file, loc] = tsoptions::GetContentMapperOptionDiagnosticLocation(
	    parsed, parsed->ContentMappers()[0],
	    std::vector<contentmapper::OptionPathSegment>{
	        {.Property = "plugins"},
	        {.Index = 0, .IsIndex = true},
	        {.Property = "name"},
	    });
	assert::Equal(t, file->Text().substr(loc.pos(), loc.end() - loc.pos()), "1");
}

// TestContentMappersAreInheritedFromExtendedConfig —
// tsconfigparsing_test.go:1219.
void TestContentMappersAreInheritedFromExtendedConfig(T* t) {
	t->Parallel();
	testConfig config{
	    .jsonText = R"({ "extends": "./base.json" })",
	    .configFileName = "tsconfig.json",
	    .basePath = "/project",
	    .allFileList =
	        {
	            {"/project/base.json",
	             R"({ "contentMappers": [{ "package": "vue-mapper", "extensions": [".vue"] }], "include": ["src"] })"},
	            {"/project/src/index.ts", "export {};"},
	            {"/project/src/component.vue", "<template></template>"},
	            {"/project/node_modules/vue-mapper/package.json",
	             R"({ "name": "vue-mapper", "version": "1.2.3", "typescript": { "contentMapper": { "exec": ["node", "./mapper.js"] } } })"},
	        },
	    .existingOptions =
	        new CompilerOptions{.RunExternalCode = Tristate::True},
	};
	for (auto& [name, getParsed] : std::vector<std::pair<std::string, getParsedFn>>{
	         {"json api", getParsedWithJsonApi},
	         {"jsonSourceFile api", getParsedWithJsonSourceFileApi},
	     }) {
		t->Run(name, [&, name = name, getParsed = getParsed](T* t) {
			t->Parallel();
			auto host = tsoptionstest::NewVFSParseConfigHost(
			    config.allFileList, config.basePath,
			    /*useCaseSensitiveFileNames*/ true);
			auto* parsed = getParsed(config, host.get(), config.basePath);
			assert::Equal(t, (int)parsed->Errors.size(), 0);
			assert::Equal(t, (int)parsed->ContentMappers().size(), 1);
			assert::Equal(t, parsed->ContentMappers()[0]->Definition.Package,
			              "vue-mapper");
			assert::DeepEqual(t, parsed->ContentMapperExtensions(),
			                  std::vector<std::string>{".vue"});
			assert::Assert(
			    t,
			    contains(parsed->FileNames(), "/project/src/component.vue"),
			    "");
		});
	}
}

// TestContentMappersRequireFlag — tsconfigparsing_test.go:1250.
void TestContentMappersRequireFlag(T* t) {
	t->Parallel();

	testConfig config{
	    .jsonText =
	        R"({ "contentMappers": [{ "package": "vue-mapper", "extensions": [".vue"] }] })",
	    .configFileName = "tsconfig.json",
	    .basePath = "/",
	    .allFileList = {{"/app.ts", "export {}"}},
	    // existingOptions omitted: --runExternalCode is not set.
	};
	auto expectedCode =
	    diagnostics::
	        Content_mappers_require_the_runExternalCode_command_line_flag_to_be_enabled
	            ->code;
	for (auto& [name, getParsed] : std::vector<std::pair<std::string, getParsedFn>>{
	         {"json api", getParsedWithJsonApi},
	         {"jsonSourceFile api", getParsedWithJsonSourceFileApi},
	     }) {
		t->Run(name, [&, name = name, getParsed = getParsed](T* t) {
			t->Parallel();
			std::unordered_map<std::string, std::string> allFileLists{
			    {"/tsconfig.json", config.jsonText}};
			allFileLists.insert(config.allFileList.begin(),
			                    config.allFileList.end());
			auto host = tsoptionstest::NewVFSParseConfigHost(
			    allFileLists, config.basePath,
			    /*useCaseSensitiveFileNames*/ true);
			auto* parsed = getParsed(config, host.get(), config.basePath);
			bool found = containsFunc(parsed->Errors, [&](Diagnostic* d) {
				return d->Code() == expectedCode;
			});
			assert::Assert(
			    t, found,
			    "expected diagnostic " + std::to_string(expectedCode) +
			        ", got errors: " + formatDiagErrors(parsed->Errors));
		});
	}
}

// TestUnresolvedContentMapperDoesNotRegisterExtensions —
// tsconfigparsing_test.go:1279.
void TestUnresolvedContentMapperDoesNotRegisterExtensions(T* t) {
	t->Parallel();

	testConfig config{
	    .jsonText =
	        R"({ "contentMappers": [{ "package": "missing-mapper", "extensions": [".vue"] }], "include": ["src"] })",
	    .configFileName = "tsconfig.json",
	    .basePath = "/",
	    .allFileList = {{"/src/app.ts", "export {}"},
	                    {"/src/Component.vue", "<template />"}},
	    .existingOptions =
	        new CompilerOptions{.RunExternalCode = Tristate::True},
	};
	for (auto& [name, getParsed] : std::vector<std::pair<std::string, getParsedFn>>{
	         {"json api", getParsedWithJsonApi},
	         {"jsonSourceFile api", getParsedWithJsonSourceFileApi},
	     }) {
		t->Run(name, [&, name = name, getParsed = getParsed](T* t) {
			t->Parallel();
			auto host = tsoptionstest::NewVFSParseConfigHost(
			    config.allFileList, config.basePath,
			    /*useCaseSensitiveFileNames*/ true);
			auto* parsed = getParsed(config, host.get(), config.basePath);

			assert::Equal(t, (int)parsed->ContentMappers().size(), 0);
			assert::Equal(t, (int)parsed->ContentMapperExtensions().size(), 0);
			assert::Assert(
			    t, !contains(parsed->FileNames(), "/src/Component.vue"), "");
			assert::Assert(
			    t, contains(parsed->FileNames(), "/src/app.ts"), "");
		});
	}
}

// TestContentMappersValidation — tsconfigparsing_test.go:1306.
void TestContentMappersValidation(T* t) {
	t->Parallel();

	struct contentMapperCase {
		std::string name;
		std::string contentMappers;
		int32_t expectedCode;
	};
	std::vector<contentMapperCase> tests = {
	    {"extension without leading dot",
	     R"([{ "package": "vue-mapper", "extensions": ["vue"] }])",
	     diagnostics::Content_mapper_file_extension_0_must_begin_with_a
	         ->code},
	    {"built-in extension",
	     R"([{ "package": "x", "extensions": [".ts"] }])",
	     diagnostics::
	         Content_mapper_file_extension_0_is_a_built_in_extension_and_cannot_be_registered_by_a_content_mapper
	             ->code},
	    {"missing extensions",
	     R"([{ "package": "x" }])",
	     diagnostics::Compiler_option_0_requires_a_value_of_type_1->code},
	    {"duplicate extension across mappers",
	     R"([{ "package": "a", "extensions": [".vue"] }, { "package": "b", "extensions": [".vue"] }])",
	     diagnostics::
	         Content_mapper_file_extension_0_is_registered_by_more_than_one_content_mapper
	             ->code},
	    {"extensions is not an array",
	     R"([{ "package": "x", "extensions": ".vue" }])",
	     diagnostics::Compiler_option_0_requires_a_value_of_type_1->code},
	    {"extensions contains a non-string",
	     R"([{ "package": "x", "extensions": [".vue", 1] }])",
	     diagnostics::Compiler_option_0_requires_a_value_of_type_1->code},
	    {"package is not a string",
	     R"([{ "package": ["x"], "extensions": [".vue"] }])",
	     diagnostics::Compiler_option_0_requires_a_value_of_type_1->code},
	    {"missing package",
	     R"([{ "extensions": [".vue"] }])",
	     diagnostics::Compiler_option_0_requires_a_value_of_type_1->code},
	    {"options is not an object",
	     R"([{ "package": "x", "extensions": [".vue"], "options": ["strict"] }])",
	     diagnostics::Compiler_option_0_requires_a_value_of_type_1->code},
	};

	for (auto& test : tests) {
		t->Run(test.name, [&, test = test](T* t) {
			t->Parallel();
			testConfig config{
			    .jsonText =
			        "{ \"contentMappers\": " + test.contentMappers + " }",
			    .configFileName = "tsconfig.json",
			    .basePath = "/",
			    .allFileList = {{"/app.ts", "export {}"}},
			    .existingOptions =
			        new CompilerOptions{.RunExternalCode = Tristate::True},
			};
			if (test.name == "duplicate extension across mappers") {
				config.allFileList["/node_modules/a/package.json"] =
				    R"({ "name": "a", "version": "1.0.0", "typescript": { "contentMapper": { "exec": ["a"] } } })";
				config.allFileList["/node_modules/b/package.json"] =
				    R"({ "name": "b", "version": "1.0.0", "typescript": { "contentMapper": { "exec": ["b"] } } })";
			}
			for (auto& [apiName, getParsed] :
			     std::vector<std::pair<std::string, getParsedFn>>{
			         {"json api", getParsedWithJsonApi},
			         {"jsonSourceFile api", getParsedWithJsonSourceFileApi},
			     }) {
				t->Run(apiName, [&, apiName = apiName,
			                 getParsed = getParsed](T* t) {
					t->Parallel();
					std::unordered_map<std::string, std::string>
					    allFileLists{{"/tsconfig.json", config.jsonText}};
					allFileLists.insert(config.allFileList.begin(),
					                    config.allFileList.end());
					auto host = tsoptionstest::NewVFSParseConfigHost(
					    allFileLists, config.basePath,
					    /*useCaseSensitiveFileNames*/ true);
					auto* parsed =
					    getParsed(config, host.get(), config.basePath);
					auto* diagnostic = findDiag(
					    parsed->Errors, [&](Diagnostic* d) {
						    return d->Code() == test.expectedCode;
					    });
					assert::Assert(
					    t, diagnostic != nullptr,
					    "expected diagnostic " +
					        std::to_string(test.expectedCode) +
					        ", got errors: " +
					        formatDiagErrors(parsed->Errors));
					if (test.name == "built-in extension") {
						assert::Equal(
						    t, (int)parsed->ContentMappers().size(), 0);
						assert::Equal(
						    t,
						    (int)parsed->ContentMapperExtensions().size(),
						    0);
					} else if (test.name ==
					           "duplicate extension across mappers") {
						assert::Equal(
						    t, (int)parsed->ContentMappers().size(), 2);
						assert::DeepEqual(
						    t,
						    parsed->ContentMappers()[0]
						        ->Definition.Extensions,
						    std::vector<std::string>{".vue"});
						assert::Equal(
						    t,
						    (int)parsed->ContentMappers()[1]
						        ->Definition.Extensions.size(),
						    0);
						assert::DeepEqual(
						    t, parsed->ContentMapperExtensions(),
						    std::vector<std::string>{".vue"});
					} else if (
					    test.name == "missing extensions" ||
					    test.name == "extensions is not an array" ||
					    test.name ==
					        "extensions contains a non-string" ||
					    test.name == "package is not a string" ||
					    test.name == "missing package" ||
					    test.name == "options is not an object") {
						assert::Equal(
						    t, (int)parsed->ContentMappers().size(), 0);
					}

					// With the jsonSourceFile API the diagnostic is
					// located at the offending tsconfig syntax.
					if (apiName == "jsonSourceFile api") {
						assert::Assert(
						    t, diagnostic->File() != nullptr,
						    "expected diagnostic " +
						        std::to_string(test.expectedCode) +
						        " to have a source file");
						assert::Assert(
						    t, diagnostic->Len() > 0,
						    "expected diagnostic " +
						        std::to_string(test.expectedCode) +
						        " to have a non-empty location");
					}
				});
			}
		});
	}
}

// TestContentMapperExtensionValidationUsesHostCaseSensitivity —
// tsconfigparsing_test.go:1413.
void TestContentMapperExtensionValidationUsesHostCaseSensitivity(T* t) {
	t->Parallel();

	struct caseSensitivityCase {
		std::string name;
		bool useCaseSensitiveFileNames;
		std::string contentMappers;
		int32_t expectedCode = 0;
	};
	std::vector<caseSensitivityCase> tests = {
	    {
	        .name = "built-in extension on case-insensitive host",
	        .useCaseSensitiveFileNames = false,
	        .contentMappers =
	            R"([{ "package": "mapper", "extensions": [".TS"] }])",
	        .expectedCode =
	            diagnostics::
	                Content_mapper_file_extension_0_is_a_built_in_extension_and_cannot_be_registered_by_a_content_mapper
	                    ->code,
	    },
	    {
	        .name = "duplicate extension on case-insensitive host",
	        .useCaseSensitiveFileNames = false,
	        .contentMappers =
	            R"([{ "package": "a", "extensions": [".vue"] }, { "package": "b", "extensions": [".VUE"] }])",
	        .expectedCode =
	            diagnostics::
	                Content_mapper_file_extension_0_is_registered_by_more_than_one_content_mapper
	                    ->code,
	    },
	    {
	        .name = "built-in extension on case-sensitive host",
	        .useCaseSensitiveFileNames = true,
	        .contentMappers =
	            R"([{ "package": "mapper", "extensions": [".TS"] }])",
	        .expectedCode =
	            diagnostics::
	                Content_mapper_file_extension_0_is_a_built_in_extension_and_cannot_be_registered_by_a_content_mapper
	                    ->code,
	    },
	    {
	        .name = "mapper extension casing is distinct on case-sensitive host",
	        .useCaseSensitiveFileNames = true,
	        .contentMappers =
	            R"([{ "package": "a", "extensions": [".vue"] }, { "package": "b", "extensions": [".VUE"] }])",
	    },
	};

	for (auto& test : tests) {
		t->Run(test.name, [&, test = test](T* t) {
			t->Parallel();
			std::unordered_map<std::string, std::string> files = {
			    {"/tsconfig.json",
			     "{ \"contentMappers\": " + test.contentMappers + " }"},
			    {"/app.ts", "export {};"},
			    {"/node_modules/mapper/package.json",
			     R"({ "name": "mapper", "version": "1.0.0", "typescript": { "contentMapper": { "exec": ["mapper"] } } })"},
			    {"/node_modules/a/package.json",
			     R"({ "name": "a", "version": "1.0.0", "typescript": { "contentMapper": { "exec": ["a"] } } })"},
			    {"/node_modules/b/package.json",
			     R"({ "name": "b", "version": "1.0.0", "typescript": { "contentMapper": { "exec": ["b"] } } })"},
			};
			auto host = tsoptionstest::NewVFSParseConfigHost(
			    files, "/", test.useCaseSensitiveFileNames);
			testConfig config{
			    .jsonText = files["/tsconfig.json"],
			    .configFileName = "tsconfig.json",
			    .basePath = "/",
			    .allFileList = files,
			    .existingOptions =
			        new CompilerOptions{.RunExternalCode = Tristate::True},
			};
			auto* parsed = getParsedWithJsonSourceFileApi(
			    config, host.get(), config.basePath);
			if (test.expectedCode == 0) {
				assert::Equal(
				    t, (int)parsed->Errors.size(), 0,
				    "unexpected errors: " +
				        formatDiagErrors(parsed->Errors));
			} else {
				bool found =
				    containsFunc(parsed->Errors, [&](Diagnostic* diagnostic) {
					    return diagnostic->Code() == test.expectedCode;
				    });
				assert::Assert(
				    t, found,
				    "expected diagnostic " +
				        std::to_string(test.expectedCode) +
				        ", got errors: " +
				        formatDiagErrors(parsed->Errors));
			}
		});
	}
}

// --- TestParseTypeAcquisition — tsconfigparsing_test.go:1556 ------------------
void TestParseTypeAcquisition(T* t) {
	t->Parallel();
	struct typeAcquisitionCase {
		std::string title;
		std::string configName;
		std::string config;
	};
	std::vector<typeAcquisitionCase> cases = {
	    {
	        .title =
	            "Convert correctly format tsconfig.json to typeAcquisition ",
	        .configName = "tsconfig.json",
	        .config = R"({
	"typeAcquisition": {
		"enable": true,
		"include": ["0.d.ts", "1.d.ts"],
		"exclude": ["0.js", "1.js"],
	},
})",
	    },
	    {
	        .title =
	            "Convert incorrect format tsconfig.json to typeAcquisition ",
	        .configName = "tsconfig.json",
	        .config = R"({
	"typeAcquisition": {
		"enableAutoDiscovy": true,
	}
})",
	    },
	    {
	        .title =
	            "Convert default tsconfig.json to typeAcquisition ",
	        .configName = "tsconfig.json",
	        .config = R"({})",
	    },
	    {
	        .title =
	            "Convert tsconfig.json with only enable property to typeAcquisition ",
	        .configName = "tsconfig.json",
	        .config = R"({
	"typeAcquisition": {
		"enable": true,
	},
})",
	    },
	    // jsconfig.json
	    {
	        .title = "Convert jsconfig.json to typeAcquisition ",
	        .configName = "jsconfig.json",
	        .config = R"({
	"typeAcquisition": {
		"enable": false,
		"include": ["0.d.ts"],
		"exclude": ["0.js"],
	},
})",
	    },
	    {
	        .title = "Convert default jsconfig.json to typeAcquisition ",
	        .configName = "jsconfig.json",
	        .config = R"({})",
	    },
	    {
	        .title =
	            "Convert incorrect format jsconfig.json to typeAcquisition ",
	        .configName = "jsconfig.json",
	        .config = R"({
	"typeAcquisition": {
		"enableAutoDiscovy": true,
	},
})",
	    },
	    {
	        .title =
	            "Convert jsconfig.json with only enable property to typeAcquisition ",
	        .configName = "jsconfig.json",
	        .config = R"({
	"typeAcquisition": {
		"enable": false,
	},
})",
	    },
	};
	for (auto& test : cases) {
		std::string withJsonApiName = test.title + " with json api";
		std::vector<testConfig> input = {
		    {
		        .jsonText = test.config,
		        .configFileName = test.configName,
		        .basePath = "/apath",
		        .allFileList =
		            {
		                {"/apath/a.ts", ""},
		                {"/apath/b.ts", ""},
		            },
		    },
		};
		t->Run(withJsonApiName, [&, withJsonApiName, input](T* t) {
			t->Parallel();
			baselineParseConfigWith(t, withJsonApiName + ".js", true, input,
			                        getParsedWithJsonApi);
		});
		std::string withJsonSourceFileApiName =
		    test.title + " with jsonSourceFile api";
		t->Run(withJsonSourceFileApiName,
		       [&, withJsonSourceFileApiName, input](T* t) {
			       t->Parallel();
			       baselineParseConfigWith(
			           t, withJsonSourceFileApiName + ".js", true, input,
			           getParsedWithJsonSourceFileApi);
		       });
	}
}

// parseSrcCompiler — tsconfigparsing_test.go:1668.
tsoptions::ParsedCommandLine* parseSrcCompiler(T* t) {
	t->Helper();

	std::string compilerDir = tspath::normalizeSlashes(
	    tspath::combinePaths(repo::testDataPath(), {"fixtures/compiler"}));
	std::string tsconfigFileName =
	    tspath::combinePaths(compilerDir, {"tsconfig.json"});
	vfs::FS* fs = vfs::osvfs::FS();
	std::shared_ptr<vfs::FS> fsShared(fs, [](vfs::FS*) {});
	tsoptions::tsoptionstest::VfsParseConfigHost host;
	host.Vfs = fsShared;
	host.CurrentDirectory = compilerDir;
	auto [jsonText, ok] = fs->ReadFile(tsconfigFileName);
	assert::Assert(t, ok, "");
	auto* configFile = tsoptions::NewTsconfigSourceFileFromFilePath(
	    tsconfigFileName,
	    tspath::toPath(tsconfigFileName, compilerDir,
	                   fs->UseCaseSensitiveFileNames()),
	    jsonText);
	auto* parsed = tsoptions::ParseJsonSourceFileConfigFileContent(
	    configFile, &host, host.GetCurrentDirectory(), nullptr, nullptr,
	    tsconfigFileName, {}, nullptr);
	assert::Equal(t, (int)parsed->Errors.size(), 0,
	              "Expected no errors in parsed command line");
	return parsed;
}

// TestParseSrcCompiler — tsconfigparsing_test.go:1699.
void TestParseSrcCompiler(T* t) {
	t->Parallel();

	auto* parsed = parseSrcCompiler(t);
	auto* opts = parsed->CompilerOptions();
	assert::DeepEqual(t, opts->Types, std::vector<std::string>{});
	assert::Equal(t, opts->Module, tsc::ModuleKind::NodeNext);
	assert::Equal(t, opts->ModuleResolution,
	              tsc::ModuleResolutionKind::NodeNext);
	assert::Equal(t, opts->Target, tsc::ScriptTarget::ES2020);
	assert::Equal(t, (int)parsed->FileNames().size(), 79);
	for (auto file : {"checker.ts", "diagnosticInformationMap.generated.ts",
	                  "node.d.ts", "program.ts"}) {
		assert::Assert(
		    t,
		    contains(parsed->FileNames(),
		             tspath::combinePaths(
		                 tspath::getDirectoryPath(opts->ConfigFilePath),
		                 {file})),
		    "");
	}
}

// memoCache — tsconfigparsing_test.go:1747. A minimal memoizing
// ExtendedConfigCache used to simulate cache hits across multiple parses of
// configs that extend a common base.
struct memoCache : tsoptions::ExtendedConfigCache {
	std::unordered_map<tspath::Path, tsoptions::ExtendedConfigCacheEntry*> m;

	tsoptions::ExtendedConfigCacheEntry* GetExtendedConfig(
	    std::string_view fileName, const tspath::Path& path,
	    const std::vector<tspath::Path>& resolutionStack,
	    tsoptions::ParseConfigHost* host) override {
		auto it = m.find(path);
		if (it != m.end()) {
			return it->second;
		}
		auto* e = tsoptions::ParseExtendedConfig(fileName, path,
		                                         resolutionStack, host, this);
		m[path] = e;
		return e;
	}
};

// TestExtendedConfigErrorsAppearOnCacheHit — tsconfigparsing_test.go:1767.
void TestExtendedConfigErrorsAppearOnCacheHit(T* t) {
	t->Parallel();

	t->Run("single config parsed twice", [&](T* t) {
		t->Parallel();
		std::unordered_map<std::string, std::string> files = {
		    {"/tsconfig.json", R"({
  "extends": "./base.json"
})"},
		    // 'excludes' instead of 'exclude' triggers diagnostic
		    {"/base.json", R"({
  "excludes": ["**/*.ts"]
})"},
		    {"/app.ts", "export {}"},
		};

		auto host = tsoptionstest::NewVFSParseConfigHost(
		    files, "/", /*useCaseSensitiveFileNames*/ true);

		auto parseConfig = [&](std::string_view configFileName,
		                       tsoptions::ExtendedConfigCache* cache)
		    -> tsoptions::ParsedCommandLine* {
			auto cfgPath = tspath::toPath(
			    configFileName, host->GetCurrentDirectory(),
			    host->FS()->UseCaseSensitiveFileNames());
			auto jsonText = host->FS()->ReadFile(configFileName);
			assert::Assert(t, jsonText.has_value(),
			               "missing " + std::string(configFileName) +
			                   " in test fs");
			auto* tsConfigSourceFile = new tsoptions::TsConfigSourceFile{
			    .SourceFile = tsc::parseSourceFile(
			        SourceFileParseOptions{std::string(configFileName),
			                               cfgPath, {}},
			        *jsonText, ScriptKind::JSON),
			};
			return tsoptions::ParseJsonSourceFileConfigFileContent(
			    tsConfigSourceFile, host.get(),
			    host->GetCurrentDirectory(), nullptr, nullptr,
			    std::string(configFileName), {}, cache);
		};

		memoCache cache;
		auto* first = parseConfig("/tsconfig.json", &cache);
		assert::Assert(t, first->Errors.size() > 0,
		               "expected diagnostics on first parse, got 0");
		auto* second = parseConfig("/tsconfig.json", &cache);
		assert::Assert(t, second->Errors.size() > 0,
		               "expected diagnostics on second parse (cache hit), "
		               "got 0");
	});

	t->Run("two configs share same base", [&](T* t) {
		t->Parallel();
		std::unordered_map<std::string, std::string> files = {
		    {"/base.json", R"({
  "excludes": ["**/*.ts"]
})"},
		    {"/projA/tsconfig.json", R"({
  "extends": "../base.json"
})"},
		    {"/projB/tsconfig.json", R"({
  "extends": "../base.json"
})"},
		    {"/projA/app.ts", "export {}"},
		    {"/projB/app.ts", "export {}"},
		};

		auto host = tsoptionstest::NewVFSParseConfigHost(
		    files, "/", /*useCaseSensitiveFileNames*/ true);

		auto parseConfig = [&](std::string_view configFileName,
		                       tsoptions::ExtendedConfigCache* cache)
		    -> tsoptions::ParsedCommandLine* {
			auto cfgPath = tspath::toPath(
			    configFileName, host->GetCurrentDirectory(),
			    host->FS()->UseCaseSensitiveFileNames());
			auto jsonText = host->FS()->ReadFile(configFileName);
			assert::Assert(t, jsonText.has_value(),
			               "missing " + std::string(configFileName) +
			                   " in test fs");
			auto* tsConfigSourceFile = new tsoptions::TsConfigSourceFile{
			    .SourceFile = tsc::parseSourceFile(
			        SourceFileParseOptions{std::string(configFileName),
			                               cfgPath, {}},
			        *jsonText, ScriptKind::JSON),
			};
			return tsoptions::ParseJsonSourceFileConfigFileContent(
			    tsConfigSourceFile, host.get(),
			    host->GetCurrentDirectory(), nullptr, nullptr,
			    std::string(configFileName), {}, cache);
		};

		memoCache cache;
		auto* first = parseConfig("/projA/tsconfig.json", &cache);
		assert::Assert(t, first->Errors.size() > 0,
		               "expected diagnostics for projA parse, got 0");
		auto* second = parseConfig("/projB/tsconfig.json", &cache);
		assert::Assert(t, second->Errors.size() > 0,
		               "expected diagnostics for projB parse (cache hit on "
		               "base), got 0");
	});
}

// TestExtendedConfigConfigDirPathsAreNotCached —
// tsconfigparsing_test.go:1856.
void TestExtendedConfigConfigDirPathsAreNotCached(T* t) {
	t->Parallel();

	std::unordered_map<std::string, std::string> files = {
	    {"/tsconfig.base.json", R"({
  "compilerOptions": {
    "paths": {
      "@pkg/*": ["${configDir}/src/*"]
    }
  }
})"},
	    {"/packages/a/tsconfig.json", R"({
  "extends": "../../tsconfig.base.json"
})"},
	    {"/packages/b/tsconfig.json", R"({
  "extends": "../../tsconfig.base.json"
})"},
	    {"/packages/a/index.ts", "export {}"},
	    {"/packages/b/index.ts", "export {}"},
	};

	auto host = tsoptionstest::NewVFSParseConfigHost(files, "/",
	                                               /*useCaseSensitiveFileNames*/
	                                               true);
	memoCache cache;

	auto parseConfig = [&](std::string_view configFileName)
	    -> tsoptions::ParsedCommandLine* {
		auto [parsed, errors] =
		    tsoptions::GetParsedCommandLineOfConfigFile(
		        std::string(configFileName), nullptr, nullptr, host.get(),
		        &cache);
		assert::Assert(
		    t, errors.empty(),
		    "unexpected errors parsing " + std::string(configFileName));
		return parsed;
	};

	parseConfig("/packages/a/tsconfig.json");
	auto& paths = parseConfig("/packages/b/tsconfig.json")
	                  ->CompilerOptions()
	                  ->Paths;
	std::vector<std::string> got;
	for (auto& [k, v] : paths) {
		if (k == "@pkg/*") {
			got = v;
			break;
		}
	}
	assert::DeepEqual(t, got, std::vector<std::string>{"/packages/b/src/*"});
}

}  // namespace

REGISTER_UNIT_TEST("tsoptions.TestParseConfigFileTextToJson",
                   TestParseConfigFileTextToJson);
REGISTER_UNIT_TEST("tsoptions.TestParseJsonConfigFileContent",
                   TestParseJsonConfigFileContent);
REGISTER_UNIT_TEST(
    "tsoptions.TestParseJsonConfigFileContentAcceptsJsonRepresentations",
    TestParseJsonConfigFileContentAcceptsJsonRepresentations);
REGISTER_UNIT_TEST("tsoptions.TestParseJsonConfigFileContentPreservesRaw",
                   TestParseJsonConfigFileContentPreservesRaw);
REGISTER_UNIT_TEST(
    "tsoptions.TestParseJsonConfigFileContentHandlesNullArrayElements",
    TestParseJsonConfigFileContentHandlesNullArrayElements);
REGISTER_UNIT_TEST(
    "tsoptions.TestParseJsonConfigFileContentDefaultsCompileOnSaveToFalse",
    TestParseJsonConfigFileContentDefaultsCompileOnSaveToFalse);
REGISTER_UNIT_TEST("tsoptions.TestParseJsonSourceFileConfigFileContent",
                   TestParseJsonSourceFileConfigFileContent);
REGISTER_UNIT_TEST(
    "tsoptions."
    "TestParseJsonSourceFileConfigFileContentReportsInvalidExtendedConfig",
    TestParseJsonSourceFileConfigFileContentReportsInvalidExtendedConfig);
REGISTER_UNIT_TEST(
    "tsoptions."
    "TestParseJsonSourceFileConfigFileContentWithEmptyExtendedConfig",
    TestParseJsonSourceFileConfigFileContentWithEmptyExtendedConfig);
REGISTER_UNIT_TEST(
    "tsoptions."
    "TestParseJsonSourceFileConfigFileContentDoesNotDuplicateUnquotedKeyDiagno"
    "stics",
    TestParseJsonSourceFileConfigFileContentDoesNotDuplicateUnquotedKeyDiagnostics);
REGISTER_UNIT_TEST(
    "tsoptions."
    "TestParseJsonSourceFileConfigFileContentReportsQuestionTokenDiagnostics",
    TestParseJsonSourceFileConfigFileContentReportsQuestionTokenDiagnostics);
REGISTER_UNIT_TEST("tsoptions.TestParseNullEnumCompilerOptions",
                   TestParseNullEnumCompilerOptions);
REGISTER_UNIT_TEST("tsoptions.TestContentMappers", TestContentMappers);
REGISTER_UNIT_TEST("tsoptions.TestContentMapperOptionDiagnosticLocation",
                   TestContentMapperOptionDiagnosticLocation);
REGISTER_UNIT_TEST(
    "tsoptions.TestContentMappersAreInheritedFromExtendedConfig",
    TestContentMappersAreInheritedFromExtendedConfig);
REGISTER_UNIT_TEST("tsoptions.TestContentMappersRequireFlag",
                   TestContentMappersRequireFlag);
REGISTER_UNIT_TEST(
    "tsoptions.TestUnresolvedContentMapperDoesNotRegisterExtensions",
    TestUnresolvedContentMapperDoesNotRegisterExtensions);
REGISTER_UNIT_TEST("tsoptions.TestContentMappersValidation",
                   TestContentMappersValidation);
REGISTER_UNIT_TEST(
    "tsoptions.TestContentMapperExtensionValidationUsesHostCaseSensitivity",
    TestContentMapperExtensionValidationUsesHostCaseSensitivity);
REGISTER_UNIT_TEST("tsoptions.TestParseTypeAcquisition",
                   TestParseTypeAcquisition);
REGISTER_UNIT_TEST("tsoptions.TestParseSrcCompiler", TestParseSrcCompiler);
REGISTER_UNIT_TEST("tsoptions.TestExtendedConfigErrorsAppearOnCacheHit",
                   TestExtendedConfigErrorsAppearOnCacheHit);
REGISTER_UNIT_TEST("tsoptions.TestExtendedConfigConfigDirPathsAreNotCached",
                   TestExtendedConfigConfigDirPathsAreNotCached);
