// harnessutil.go — the test-harness file compiler driver: CompileFiles/
// CompileFilesEx, SetOptionsFromTestConfig, GetFileBasedTestConfigurations,
// the cached compiler host, TracerForBaselining, CompilationResult +
// getOutputPath/GetSourceMapRecord, testBuildInfoReader, EnumerateFiles.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "internal/ast/ast.h"
#include "internal/ast/diagnostics_util.h"
#include "internal/bundled/bundled.h"
#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/core/version.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/execute/incremental/incremental.h"
#include "internal/locale/locale.h"
#include "internal/outputpaths/outputpaths.h"
#include "internal/parser/parser.h"
#include "internal/repo/paths.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/testutil/contentmappertest/contentmappertest.h"
#include "internal/testutil/harnessutil/harnessutil.h"
#include "internal/testutil/harnessutil/recorderfs.h"
#include "internal/testutil/harnessutil/sourcemap_recorder.h"
#include "internal/testutil/testutil.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::testutil::harnessutil {

namespace vfstest = ::tsc::vfs::vfstest;

namespace {

// forward decls (defined below in the Go file order)
const char* moduleKindString(ModuleKind k);
std::string scriptTargetString(ScriptTarget t);
std::pair<tsoptions::CompilerOptionsValue, bool> tryGetValueOfOptionString(
    const std::string& option, const std::string& value);
CompilationResult* newCompilationResult(compiler::CompilerHost* host,
                                        CompilerOptions* options,
                                        compiler::ProgramLike* program,
                                        compiler::EmitResult* result,
                                        std::vector<Diagnostic*> diagnostics,
                                        HarnessOptions* harnessOptions);

// strings helpers used below (Go strings.CutPrefix / CutSuffix / Cut).
bool cutPrefix(std::string_view s, std::string_view prefix,
               std::string_view* rest) {
	if (s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix) {
		*rest = s.substr(prefix.size());
		return true;
	}
	return false;
}

bool cutSuffix(std::string_view s, std::string_view suffix,
               std::string_view* rest) {
	if (s.size() >= suffix.size() &&
	    s.substr(s.size() - suffix.size()) == suffix) {
		*rest = s.substr(0, s.size() - suffix.size());
		return true;
	}
	return false;
}

[[maybe_unused]] std::pair<std::string_view, std::string_view> cut(std::string_view s,
                                                  char sep) {
	auto i = s.find(sep);
	if (i == std::string_view::npos) {
		return {s, {}};
	}
	return {s.substr(0, i), s.substr(i + 1)};
}

bool eqFold(std::string_view a, std::string_view b) { // strings.EqualFold
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++) {
		if (std::tolower((unsigned char)a[i]) !=
		    std::tolower((unsigned char)b[i])) {
			return false;
		}
	}
	return true;
}

std::string toLower(std::string_view s) {
	std::string r(s);
	for (auto& c : r) c = (char)std::tolower((unsigned char)c);
	return r;
}

// compilerOptions — harnessutil.go:324 (OptionsDeclarations + 4 extras).
const std::vector<const tsoptions::CommandLineOption*>& compilerOptions() {
	static const std::vector<const tsoptions::CommandLineOption*>* list =
	    [] {
		    auto l = new std::vector<const tsoptions::CommandLineOption*>(
		        tsoptions::OptionsDeclarations());
		    static const tsoptions::CommandLineOption extras[] = {
		        {"allowNonTsExtensions", "",
		         tsoptions::CommandLineOptionTypeBoolean},
		        {"noErrorTruncation", "",
		         tsoptions::CommandLineOptionTypeBoolean},
		        {"suppressOutputPathCheck", "",
		         tsoptions::CommandLineOptionTypeBoolean},
		        {"noCheck", "", tsoptions::CommandLineOptionTypeBoolean},
		    };
		    for (const auto& e : extras) l->push_back(&e);
		    return l;
	    }();
	return *list;
}

// harnessCommandLineOptions — harnessutil.go:339.
const std::vector<const tsoptions::CommandLineOption*>&
harnessCommandLineOptions() {
	static const std::vector<const tsoptions::CommandLineOption*> list = {
	    new tsoptions::CommandLineOption{
	        "useCaseSensitiveFileNames", "",
	        tsoptions::CommandLineOptionTypeBoolean},
	    new tsoptions::CommandLineOption{"baselineFile", "",
	                                     tsoptions::CommandLineOptionTypeString},
	    new tsoptions::CommandLineOption{"includeBuiltFile", "",
	                                     tsoptions::CommandLineOptionTypeString},
	    new tsoptions::CommandLineOption{"fileName", "",
	                                     tsoptions::CommandLineOptionTypeString},
	    new tsoptions::CommandLineOption{"libFiles", "",
	                                     tsoptions::CommandLineOptionTypeList},
	    new tsoptions::CommandLineOption{
	        "noImplicitReferences", "",
	        tsoptions::CommandLineOptionTypeBoolean},
	    new tsoptions::CommandLineOption{"currentDirectory", "",
	                                     tsoptions::CommandLineOptionTypeString},
	    new tsoptions::CommandLineOption{"symlink", "",
	                                     tsoptions::CommandLineOptionTypeString},
	    new tsoptions::CommandLineOption{"link", "",
	                                     tsoptions::CommandLineOptionTypeString},
	    new tsoptions::CommandLineOption{"noTypesAndSymbols", "",
	                                     tsoptions::CommandLineOptionTypeBoolean},
	    // Emitted js baseline will print full paths for every output file
	    new tsoptions::CommandLineOption{"fullEmitPaths", "",
	                                     tsoptions::CommandLineOptionTypeBoolean},
	    // used to enable error collection in `transpile` baselines
	    new tsoptions::CommandLineOption{"reportDiagnostics", "",
	                                     tsoptions::CommandLineOptionTypeBoolean},
	    // Adds suggestion diagnostics to error baselines
	    new tsoptions::CommandLineOption{"captureSuggestions", "",
	                                     tsoptions::CommandLineOptionTypeBoolean},
	};
	return list;
}

// getHarnessOption — harnessutil.go:387.
const tsoptions::CommandLineOption* getHarnessOption(std::string_view name) {
	for (const auto* option : harnessCommandLineOptions()) {
		if (eqFold(option->Name, name)) return option;
	}
	return nullptr;
}

// getCommandLineOption — harnessutil.go:1170.
const tsoptions::CommandLineOption* getCommandLineOption(std::string_view option) {
	for (const auto* optionDecl : compilerOptions()) {
		if (eqFold(optionDecl->Name, option)) return optionDecl;
	}
	return nullptr;
}

// getAllValuesForOption — harnessutil.go:1176.
std::vector<std::string> getAllValuesForOption(std::string_view option) {
	const auto* optionDecl = getCommandLineOption(option);
	if (optionDecl == nullptr) {
		return {};
	}
	if (optionDecl->Kind == tsoptions::CommandLineOptionTypeEnum) {
		const auto* m = optionDecl->EnumMap();
		return m != nullptr ? m->Keys() : std::vector<std::string>{};
	}
	if (optionDecl->Kind == tsoptions::CommandLineOptionTypeBoolean) {
		return {"true", "false"};
	}
	return {};
}

// getOptionValue — harnessutil.go:416.
tsoptions::CompilerOptionsValue getOptionValue(
    gostd::testing::T* t, const tsoptions::CommandLineOption* option,
    const std::string& value, const std::string& cwd) {
	const auto& kind = option->Kind;
	if (kind == tsoptions::CommandLineOptionTypeString) {
		if (option->IsFilePath) {
			return tsoptions::CompilerOptionsValue(
			    tspath::getNormalizedAbsolutePath(value, cwd));
		}
		return tsoptions::CompilerOptionsValue(value);
	}
	if (kind == tsoptions::CommandLineOptionTypeNumber) {
		char* end = nullptr;
		long numVal = std::strtol(value.c_str(), &end, 10);
		if (end != value.c_str() + value.size() || value.empty()) {
			t->Fatalf("Value for option '%s' must be a number, got: %v",
			          {option->Name, value});
		}
		return tsoptions::CompilerOptionsValue(int64_t(numVal));
	}
	if (kind == tsoptions::CommandLineOptionTypeBoolean) {
		std::string lv = toLower(value);
		if (lv == "true") return tsoptions::CompilerOptionsValue(true);
		if (lv == "false") return tsoptions::CompilerOptionsValue(false);
		t->Fatalf("Value for option '%s' must be a boolean, got: %v",
		          {option->Name, value});
	}
	if (kind == tsoptions::CommandLineOptionTypeEnum) {
		const auto* m = option->EnumMap();
		std::pair<const tsoptions::CompilerOptionsValue*, bool> found;
		if (m != nullptr) found = m->Get(toLower(value));
		else found = {nullptr, false};
		if (!found.second || found.first == nullptr) {
			std::string joined;
			if (m != nullptr) {
				auto keys = m->Keys();
				for (size_t i = 0; i < keys.size(); i++) {
					if (i) joined += ",";
					joined += keys[i];
				}
			}
			t->Fatalf(
			    "Value for option '%s' must be one of %s, got: %v",
			    {option->Name, joined, value});
		}
		return *found.first;
	}
	if (kind == tsoptions::CommandLineOptionTypeList ||
	    kind == tsoptions::CommandLineOptionTypeListOrElement) {
		auto [listVal, errors] = tsoptions::ParseListTypeOption(option, value);
		const tsoptions::CommandLineOption* elements = option->Elements();
		if (elements != nullptr && elements->IsFilePath) {
			tsoptions::JsonArray mapped;
			for (const auto& item : listVal) {
				const auto* s =
				    std::get_if<std::string>(&item.v);
				mapped.emplace_back(s != nullptr
				                        ? tspath::getNormalizedAbsolutePath(
				                              *s, cwd)
				                        : "");
			}
			return tsoptions::CompilerOptionsValue(std::move(mapped));
		}
		if (!errors.empty()) {
			t->Fatalf("Unknown value '%s' for compiler option '%s'",
			          {value, option->Name});
		}
		return tsoptions::CompilerOptionsValue(listVal);
	}
	if (kind == tsoptions::CommandLineOptionTypeObject) {
		t->Fatalf("Object type options like '%s' are not supported",
		          {option->Name});
	}
	return tsoptions::CompilerOptionsValue();
}

// parseHarnessOption — harnessutil.go:393.
void parseHarnessOption(gostd::testing::T* t, const std::string& key,
                        const tsoptions::CompilerOptionsValue& value,
                        HarnessOptions* harnessOptions) {
	auto asBool = [&]() { return std::get<bool>(value.v); };
	auto asString = [&]() -> const std::string& {
		return std::get<std::string>(value.v);
	};
	if (key == "useCaseSensitiveFileNames") {
		harnessOptions->UseCaseSensitiveFileNames = asBool();
	} else if (key == "baselineFile") {
		harnessOptions->BaselineFile = asString();
	} else if (key == "includeBuiltFile") {
		harnessOptions->IncludeBuiltFile = asString();
	} else if (key == "fileName") {
		harnessOptions->FileName = asString();
	} else if (key == "libFiles") {
		harnessOptions->LibFiles.clear();
		for (const auto& v : std::get<tsoptions::JsonArray>(value.v)) {
			harnessOptions->LibFiles.push_back(
			    std::get<std::string>(v.v));
		}
	} else if (key == "noImplicitReferences") {
		harnessOptions->NoImplicitReferences = asBool();
	} else if (key == "currentDirectory") {
		harnessOptions->CurrentDirectory = asString();
	} else if (key == "symlink") {
		harnessOptions->Symlink = asString();
	} else if (key == "link") {
		harnessOptions->Link = asString();
	} else if (key == "noTypesAndSymbols") {
		harnessOptions->NoTypesAndSymbols = asBool();
	} else if (key == "fullEmitPaths") {
		harnessOptions->FullEmitPaths = asBool();
	} else if (key == "reportDiagnostics") {
		harnessOptions->ReportDiagnostics = asBool();
	} else if (key == "captureSuggestions") {
		harnessOptions->CaptureSuggestions = asBool();
	} else if (key == "typescriptVersion") {
		harnessOptions->TypescriptVersion = asString();
	} else {
		t->Fatalf("Unknown harness option '%s'.", {key});
	}
}

// === cachedCompilerHost — harnessutil.go:456 ===

// SourceFileCacheKey — harnessutil.go:462.
struct SourceFileCacheKey {
	SourceFileParseOptions opts;
	std::string text;
	ScriptKind scriptKind;

	bool operator==(const SourceFileCacheKey& o) const {
		return opts.FileName == o.opts.FileName &&
		       opts.Path == o.opts.Path &&
		       opts.ExternalModuleIndicatorOptions.JSX ==
		           o.opts.ExternalModuleIndicatorOptions.JSX &&
		       opts.ExternalModuleIndicatorOptions.Force ==
		           o.opts.ExternalModuleIndicatorOptions.Force &&
		       text == o.text && scriptKind == o.scriptKind;
	}
};

struct SourceFileCacheKeyHash {
	size_t operator()(const SourceFileCacheKey& k) const {
		std::hash<std::string> sh;
		size_t h = sh(k.opts.FileName);
		h = h * 31 + sh(k.opts.Path);
		h = h * 31 + sh(k.text);
		h = h * 31 + std::hash<int>()((int)k.scriptKind);
		h = h * 31 +
		    std::hash<int>()((int)k.opts.ExternalModuleIndicatorOptions.JSX);
		h = h * 31 +
		    std::hash<bool>()(k.opts.ExternalModuleIndicatorOptions.Force);
		return h;
	}
};

// GetSourceFileCacheKey — harnessutil.go:467.
SourceFileCacheKey GetSourceFileCacheKey(SourceFileParseOptions opts,
                                       const std::string& text,
                                       ScriptKind scriptKind) {
	return SourceFileCacheKey{opts, text, scriptKind};
}

// sourceFileCache — harnessutil.go:459 (collections.SyncMap).
collections::SyncMap<SourceFileCacheKey, SourceFile*,
                     SourceFileCacheKeyHash>
    sourceFileCache;

// cachedCompilerHost — harnessutil.go:455. Go embeds the compilerHost
// interface; here CompilerHost is concrete so this is a subclass overriding
// only GetSourceFile with the source-file cache.
struct cachedCompilerHost : compiler::CompilerHost {
	struct TracerForBaselining* tracer = nullptr;

	// GetSourceFile — harnessutil.go:475.
	SourceFile*
	GetSourceFile(const SourceFileParseOptions& opts) override {
		auto rf = fs->ReadFile(opts.FileName);
		if (!rf.second) {
			return nullptr;
		}

		auto scriptKind = getScriptKindFromFileName(opts.FileName);
		if (scriptKind == ScriptKind::Unknown) {
			TSC_UNREACHABLE(
			    ("Unknown script kind for file  " + opts.FileName)
			        .c_str());
		}

		auto key = GetSourceFileCacheKey(opts, rf.first, scriptKind);
		auto [cached, found] = sourceFileCache.Load(key);
		if (found) {
			return cached;
		}

		auto* sourceFile = parseSourceFile(opts, rf.first, scriptKind);
		auto [result, _] = sourceFileCache.LoadOrStore(key, sourceFile);
		return result;
	}
};

}  // namespace

// === TracerForBaselining — harnessutil.go:489 (declared in harnessutil.h) ===
// Out-of-line member definitions must live at
// tsc::testutil::harnessutil scope, not inside the anonymous namespace.

// NewTracerForBaselining — harnessutil.go:495.
TracerForBaselining* NewTracerForBaselining(
    tspath::ComparePathsOptions opts, std::string* builder) {
	auto* t = new TracerForBaselining();
	t->opts = std::move(opts);
	t->builder = builder;
	return t;
}

// sanitizeTrace — harnessutil.go:519.
std::string TracerForBaselining::sanitizeTrace(
    const std::string& msg, bool usePackageJsonCache) {
	// Version
	if (auto pos = msg.find("'" + std::string(version()) + "'");
	    pos != std::string::npos) {
		std::string r = msg;
		r.replace(pos, std::string(version()).size() + 2,
		          "'" + std::string(FakeTSVersion) + "'");
		return r;
	}
	// caching of fs in trace to be replaces with non caching version
	std::string_view str;
	if (cutSuffix(msg, "' does not exist according to earlier cached lookups.",
	              &str)) {
		std::string_view file = str;
		cutPrefix(file, "File '", &file);
		if (usePackageJsonCache) {
			auto filePath =
			    tspath::toPath(file, opts.currentDirectory,
			                   opts.useCaseSensitiveFileNames);
			if (packageJsonCache.find(filePath) != packageJsonCache.end()) {
				return msg;
			}
			packageJsonCache[filePath] = false;
		}
		return "File '" + std::string(file) + "' does not exist.";
	}
	if (cutSuffix(msg, "' exists according to earlier cached lookups.",
	              &str)) {
		std::string_view file = str;
		cutPrefix(file, "File '", &file);
		if (usePackageJsonCache) {
			auto filePath =
			    tspath::toPath(file, opts.currentDirectory,
			                   opts.useCaseSensitiveFileNames);
			if (packageJsonCache.find(filePath) != packageJsonCache.end()) {
				return msg;
			}
			packageJsonCache[filePath] = true;
		}
		return "Found 'package.json' at '" + std::string(file) + "'.";
	}
	if (usePackageJsonCache) {
		if (cutSuffix(msg, "' does not exist.", &str)) {
			std::string_view file = str;
			cutPrefix(file, "File '", &file);
			auto filePath =
			    tspath::toPath(file, opts.currentDirectory,
			                   opts.useCaseSensitiveFileNames);
			if (packageJsonCache.find(filePath) == packageJsonCache.end()) {
				packageJsonCache[filePath] = false;
				return msg;
			}
			return "File '" + std::string(file) +
			       "' does not exist according to earlier cached lookups.";
		}
		if (cutPrefix(msg, "Found 'package.json' at '", &str)) {
			std::string_view file = str;
			cutSuffix(file, "'.", &file);
			auto filePath =
			    tspath::toPath(file, opts.currentDirectory,
			                   opts.useCaseSensitiveFileNames);
			if (packageJsonCache.find(filePath) == packageJsonCache.end()) {
				packageJsonCache[filePath] = true;
				return msg;
			}
			return "File '" + std::string(file) +
			       "' exists according to earlier cached lookups.";
		}
	}
	return msg;
}

// Trace — harnessutil.go:502. `builder` is a std::string (Go
// *strings.Builder); Fprintln appends "\n".
void TracerForBaselining::Trace(const DiagnosticMessage* msg,
                                const std::vector<std::string>& args) {
	*builder += sanitizeTrace(
	    localize(locale::Default, msg, "", args), true);
	*builder += "\n";
}

// TraceWithWriter — harnessutil.go:507 (io.Writer → std::ostream*).
void TracerForBaselining::TraceWithWriter(std::ostream* w,
                                          const std::string& msg,
                                          bool usePackageJsonCache) {
	*w << sanitizeTrace(msg, usePackageJsonCache) << '\n';
}

// String/Reset — harnessutil.go:607/611.
const std::string& TracerForBaselining::String() const { return *builder; }
void TracerForBaselining::Reset() { packageJsonCache.clear(); }

namespace {

// createCompilerHost — harnessutil.go:615.
cachedCompilerHost* createCompilerHost(
    const std::shared_ptr<vfs::FS>& fs, const std::string& defaultLibraryPath,
    const std::string& currentDirectory,
    const std::shared_ptr<contentmapper::Project>& contentMapperProject) {
	auto* tracerBuilder = new std::string();
	auto* tracer = NewTracerForBaselining(
	    tspath::ComparePathsOptions{fs->UseCaseSensitiveFileNames(),
	                                currentDirectory},
	    tracerBuilder);
	auto* h = new cachedCompilerHost();
	h->currentDirectory = currentDirectory;
	h->fs = fs;
	h->defaultLibraryPath = defaultLibraryPath;
	h->extendedConfigCache = nullptr;
	h->trace = [tracer](const DiagnosticMessage* m,
	                    const std::vector<std::string>& a) {
		tracer->Trace(m, a);
	};
	h->contentMapperProject = contentMapperProject;
	h->tracer = tracer;
	return h;
}

// compareTestFiles — harnessutil.go:831.
int compareTestFiles(const TestFile* a, const TestFile* b) {
	return a->UnitName.compare(b->UnitName);
}

// === createProgram — harnessutil.go:978 ===

// testBuildInfoReader — harnessutil.go:960.
struct testBuildInfoReader : execute::incremental::BuildInfoReader {
	execute::incremental::BuildInfoReader* inner;

	// ReadBuildInfo — harnessutil.go:964.
	execute::incremental::BuildInfo*
	ReadBuildInfo(tsoptions::ParsedCommandLine* config) override {
		auto* r = inner->ReadBuildInfo(config);
		if (r == nullptr) {
			return nullptr;
		}
		r->Version = std::string(version());
		return r;
	}
};

testBuildInfoReader* getTestBuildInfoReader(compiler::CompilerHost* host) {
	auto* r = new testBuildInfoReader();
	r->inner = execute::incremental::NewBuildInfoReader(host);
	return r;
}

// createProgram — harnessutil.go:978.
compiler::ProgramLike* createProgram(compiler::CompilerHost* host,
                                     tsoptions::ParsedCommandLine* config) {
	Tristate singleThreaded = Tristate::Unknown;
	if (testutil::TestProgramIsSingleThreaded()) {
		singleThreaded = Tristate::True;
	}

	compiler::ProgramOptions programOptions;
	programOptions.Config = config;
	programOptions.Host = host;
	programOptions.SingleThreaded = singleThreaded;
	auto* program = compiler::NewProgram(programOptions);
	if (tristateIsTrue(config->CompilerOptions()->Incremental)) {
		auto* oldProgram = execute::incremental::ReadBuildInfoProgram(
		    config, getTestBuildInfoReader(host), host);
		auto* incrementalProgram = execute::incremental::NewProgram(
		    program, oldProgram, execute::incremental::CreateHost(host), nullptr,
		    false);
		return incrementalProgram;
	}
	return program;
}

// compileFilesWithHost — harnessutil.go:631.
CompilationResult* compileFilesWithHost(compiler::CompilerHost* host,
                                        tsoptions::ParsedCommandLine* config,
                                        HarnessOptions* harnessOptions) {
	std::vector<Diagnostic*> preErrors;
	auto* preCompilerOptions =
	    const_cast<CompilerOptions*>(config->CompilerOptions())->Clone();
	preCompilerOptions->TraceResolution = Tristate::False;
	auto* preConfig = new tsoptions::ParsedCommandLine();
	auto* preParsed = new tsoptions::ParsedOptions();
	preParsed->CompilerOptions = preCompilerOptions;
	preParsed->FileNames = config->FileNames();
	preParsed->ContentMappers = config->ContentMappers();
	preConfig->ParsedConfig = preParsed;
	preConfig->ConfigFile = config->ConfigFile;
	preConfig->Errors = config->Errors;
	auto* preProgram = createProgram(host, preConfig);
	for (auto* d : preProgram->GetConfigFileParsingDiagnostics())
		preErrors.push_back(d);
	for (auto* d : preProgram->GetProgramDiagnostics()) preErrors.push_back(d);
	for (auto* d : preProgram->GetSyntacticDiagnostics(nullptr))
		preErrors.push_back(d);
	for (auto* d : preProgram->GetSemanticDiagnostics(nullptr))
		preErrors.push_back(d);
	for (auto* d : preProgram->GetGlobalDiagnostics()) preErrors.push_back(d);
	if (harnessOptions->CaptureSuggestions) {
		for (auto* d : preProgram->GetSuggestionDiagnostics(nullptr))
			preErrors.push_back(d);
	}
	if (preProgram->Options()->GetEmitDeclarations()) {
		for (auto* d : preProgram->GetDeclarationDiagnostics(nullptr))
			preErrors.push_back(d);
	}
	preErrors = compiler::sortAndDeduplicateDiagnostics(std::move(preErrors));

	auto* postProgram = createProgram(host, config);
	compiler::EmitOptions emitOpts;
	auto* emitResult = postProgram->Emit(&emitOpts);
	std::vector<Diagnostic*> postErrors;
	for (auto* d : postProgram->GetConfigFileParsingDiagnostics())
		postErrors.push_back(d);
	for (auto* d : postProgram->GetProgramDiagnostics())
		postErrors.push_back(d);
	for (auto* d : postProgram->GetSyntacticDiagnostics(nullptr))
		postErrors.push_back(d);
	for (auto* d : postProgram->GetSemanticDiagnostics(nullptr))
		postErrors.push_back(d);
	for (auto* d : postProgram->GetGlobalDiagnostics())
		postErrors.push_back(d);
	if (postProgram->Options()->GetEmitDeclarations()) {
		for (auto* d : postProgram->GetDeclarationDiagnostics(nullptr))
			postErrors.push_back(d);
	}
	if (harnessOptions->CaptureSuggestions) {
		for (auto* d : postProgram->GetSuggestionDiagnostics(nullptr))
			postErrors.push_back(d);
	}
	postErrors = compiler::sortAndDeduplicateDiagnostics(std::move(postErrors));

	std::vector<Diagnostic*> errors = postErrors;
	if (postErrors.size() != preErrors.size()) {
		// Go: longerErrors/shorterErrors swap.
		const std::vector<Diagnostic*>* longerErrors = &postErrors;
		const std::vector<Diagnostic*>* shorterErrors = &preErrors;
		if (preErrors.size() > postErrors.size()) {
			longerErrors = &preErrors;
			shorterErrors = &postErrors;
		}
		auto* diag = newDetachedDiagnostic(
		    TextRange::undefined(),
		    NewAdHocMessage(gostd::sprintf(
		        "Pre-emit (%d) and post-emit (%d) diagnostic counts do not "
		        "match! This can indicate that a semantic _error_ was added "
		        "by the emit resolver - such an error may not be reflected on "
		        "the command line or in the editor, but may be captured in a "
		        "baseline here!",
		        {(int64_t)preErrors.size(), (int64_t)postErrors.size()})));
		diag->AddRelatedInfo(newDetachedDiagnostic(
		    TextRange::undefined(),
		    NewAdHocMessage("The excess diagnostics are:")));
		for (auto* d : *longerErrors) {
			bool matched = false;
			for (auto* d2 : *shorterErrors) {
				if (CompareDiagnostics(d, d2) == 0) {
					matched = true;
					break;
				}
			}
			if (!matched) {
				diag->AddRelatedInfo(d);
			}
		}
		errors = *shorterErrors;
		errors.push_back(diag);
	}

	// newCompilationResult — harnessutil.go:749.
	return newCompilationResult(host, config->CompilerOptions(), postProgram,
	                            emitResult, errors, harnessOptions);
}

// newCompilationResult — harnessutil.go:749.
CompilationResult* newCompilationResult(compiler::CompilerHost* host,
                                        CompilerOptions* options,
                                        compiler::ProgramLike* program,
                                        compiler::EmitResult* result,
                                        std::vector<Diagnostic*> diagnostics,
                                        HarnessOptions* harnessOptions) {
	if (program != nullptr) {
		options = const_cast<CompilerOptions*>(program->Options());
	}

	auto* c = new CompilationResult();
	c->Diagnostics = std::move(diagnostics);
	c->Result = result;
	c->Program = program;
	c->Options = options;
	c->HarnessOptions = harnessOptions;
	c->Host = host;

	auto* fs = dynamic_cast<OutputRecorderFS*>(host->FS().get());
	if (fs != nullptr && program != nullptr) {
		// Corsa, unlike Strada, can use multiple threads for emit. As a
		// result, the order of outputs is non-deterministic. To make the
		// order deterministic, we sort the outputs by the order of the inputs.
		collections::OrderedMap<std::string, TestFile*> js, dts, maps;
		for (auto* document : fs->Outputs()) {
			if (tspath::hasJSFileExtension(document->UnitName) ||
			    tspath::hasJSONFileExtension(document->UnitName)) {
				js.Set(document->UnitName, document);
			} else if (tspath::isDeclarationFileName(document->UnitName)) {
				dts.Set(document->UnitName, document);
			} else if (tspath::fileExtensionIs(document->UnitName, ".map")) {
				maps.Set(document->UnitName, document);
			}
		}

		// using the order from the inputs, populate the outputs
		for (auto* sourceFile : program->GetSourceFiles()) {
			auto* input =
			    new TestFile{sourceFile->FileName(), sourceFile->Text()};
			c->inputs.push_back(input);
			if (!tspath::isDeclarationFileName(sourceFile->FileName())) {
				auto extname = std::string(outputpaths::GetOutputExtension(
				    sourceFile->FileName(), options->Jsx));
				auto* outputs = new CompilationOutput();
				outputs->Inputs.push_back(input);
				outputs->JS = js.GetOrZero(
				    c->getOutputPath(sourceFile->FileName(), extname));
				outputs->DTS = dts.GetOrZero(c->getOutputPath(
				    sourceFile->FileName(),
				    std::string(
				        tspath::getDeclarationEmitExtensionForPath(
				            sourceFile->FileName()))));
				outputs->Map = maps.GetOrZero(c->getOutputPath(
				    sourceFile->FileName(), extname + ".map"));
				c->inputsAndOutputs.Set(sourceFile->FileName(), outputs);
				if (outputs->JS != nullptr) {
					c->inputsAndOutputs.Set(outputs->JS->UnitName, outputs);
					c->JS.Set(outputs->JS->UnitName, outputs->JS);
					js.Delete(outputs->JS->UnitName);
					c->outputs.push_back(outputs->JS);
				}
				if (outputs->DTS != nullptr) {
					c->inputsAndOutputs.Set(outputs->DTS->UnitName, outputs);
					c->DTS.Set(outputs->DTS->UnitName, outputs->DTS);
					dts.Delete(outputs->DTS->UnitName);
					c->outputs.push_back(outputs->DTS);
				}
				if (outputs->Map != nullptr) {
					c->inputsAndOutputs.Set(outputs->Map->UnitName, outputs);
					c->Maps.Set(outputs->Map->UnitName, outputs->Map);
					maps.Delete(outputs->Map->UnitName);
					c->outputs.push_back(outputs->Map);
				}
			}
		}

		// add any unhandled outputs, ordered by unit name
		auto sortedByUnit = [](std::vector<TestFile*> v) {
			std::sort(v.begin(), v.end(),
			          [](TestFile* a, TestFile* b) {
				          return compareTestFiles(a, b) < 0;
			          });
			return v;
		};
		for (auto* document : sortedByUnit(js.Values())) {
			c->JS.Set(document->UnitName, document);
		}
		for (auto* document : sortedByUnit(dts.Values())) {
			c->DTS.Set(document->UnitName, document);
		}
		for (auto* document : sortedByUnit(maps.Values())) {
			c->Maps.Set(document->UnitName, document);
		}
	}

	return c;
}

// getFileBasedTestConfigurationDescription — harnessutil.go:1020.
std::string getFileBasedTestConfigurationDescription(
    const TestConfiguration& config) {
	std::string output;
	std::vector<std::string> keys;
	keys.reserve(config.size());
	for (const auto& [k, _v] : config) keys.push_back(k);
	std::sort(keys.begin(), keys.end());
	for (size_t i = 0; i < keys.size(); i++) {
		if (i > 0) output += ",";
		output += gostd::sprintf("%s=%s",
		                         {keys[i], toLower(config.at(keys[i]))});
	}
	return output;
}

// getValueOfOptionString — harnessutil.go:1150.
tsoptions::CompilerOptionsValue getValueOfOptionString(
    gostd::testing::T* t, const std::string& option,
    const std::string& value) {
	auto [result, ok] = tryGetValueOfOptionString(option, value);
	if (!ok) {
		t->Fatalf("Unknown value '%s' for option '%s'", {value, option});
	}
	return result;
}

// tryGetValueOfOptionString — harnessutil.go:1158.
std::pair<tsoptions::CompilerOptionsValue, bool> tryGetValueOfOptionString(
    const std::string& option, const std::string& value) {
	const auto* optionDecl = getCommandLineOption(option);
	if (optionDecl == nullptr) {
		return {tsoptions::CompilerOptionsValue(), false};
	}
	if (optionDecl->Kind == tsoptions::CommandLineOptionTypeEnum) {
		const auto* m = optionDecl->EnumMap();
		if (m == nullptr) {
			return {tsoptions::CompilerOptionsValue(), false};
		}
		auto [enumVal, ok] = m->Get(toLower(value));
		if (!ok || enumVal == nullptr) {
			return {tsoptions::CompilerOptionsValue(), false};
		}
		return {*enumVal, true};
	}
	if (optionDecl->Kind == tsoptions::CommandLineOptionTypeBoolean) {
		auto lv = toLower(value);
		if (lv == "true") return {tsoptions::CompilerOptionsValue(true), true};
		if (lv == "false")
			return {tsoptions::CompilerOptionsValue(false), true};
		return {tsoptions::CompilerOptionsValue(), false};
	}
	return {tsoptions::CompilerOptionsValue(value), true};
}

// splitOptionValues — harnessutil.go:1083.
std::vector<std::string> splitOptionValues(gostd::testing::T* t,
                                           const std::string& value,
                                           const std::string& option) {
	if (value.empty()) {
		return {};
	}

	bool star = false;
	std::vector<std::string> includes;
	std::vector<std::string> excludes;
	size_t pos = 0;
	while (pos <= value.size()) { // strings.SplitSeq(value, ",")
		auto comma = value.find(',', pos);
		std::string_view s =
		    std::string_view(value)
		        .substr(pos,
		                comma == std::string::npos ? std::string::npos
		                                           : comma - pos);
		// TrimSpace
		while (!s.empty() &&
		       std::isspace((unsigned char)s.front())) {
			s.remove_prefix(1);
		}
		while (!s.empty() &&
		       std::isspace((unsigned char)s.back())) {
			s.remove_suffix(1);
		}
		if (s.empty()) {
		} else if (s == "*") {
			star = true;
		} else if (s.front() == '-' || s.front() == '!') {
			excludes.emplace_back(s.substr(1));
		} else {
			includes.emplace_back(s);
		}
		if (comma == std::string::npos) break;
		pos = comma + 1;
	}

	if (includes.empty() && !star && excludes.empty()) {
		return {};
	}

	// Dedupe the variations by their normalized values. Go maps are
	// unordered; insertion-ordered assoc list gives the same dedup and a
	// deterministic value order.
	std::vector<std::pair<tsoptions::CompilerOptionsValue, std::string>>
	    variations;

	// add (and deduplicate) all included entries
	for (const auto& include : includes) {
		auto v = getValueOfOptionString(t, option, include);
		bool found = false;
		for (const auto& [ev, _] : variations) {
			if (ev == v) { found = true; break; }
		}
		if (!found) variations.emplace_back(std::move(v), include);
	}

	auto allValues = getAllValuesForOption(option);
	if (star && !allValues.empty()) {
		// add all entries
		for (const auto& include : allValues) {
			auto v = getValueOfOptionString(t, option, include);
			bool found = false;
			for (const auto& [ev, _] : variations) {
				if (ev == v) { found = true; break; }
			}
			if (!found) variations.emplace_back(std::move(v), include);
		}
	}

	// remove all excluded entries
	for (const auto& exclude : excludes) {
		auto [value_, ok] = tryGetValueOfOptionString(option, exclude);
		if (!ok) {
			// The excluded value is not recognized (e.g., a removed option
			// like "es3"). Just skip it since there's nothing to remove.
			continue;
		}
		for (auto it = variations.begin(); it != variations.end(); ++it) {
			if (it->first == value_) {
				variations.erase(it);
				break;
			}
		}
	}

	if (variations.empty()) {
		TSC_UNREACHABLE(
		    gostd::sprintf(
		        "Variations in test option '@%s' resulted in an empty "
		        "set.",
		        {option})
		        .c_str());
	}
	std::vector<std::string> result;
	for (auto& [_v, s] : variations) result.push_back(s);
	return result;
}

// computeFileBasedTestConfigurationVariations — harnessutil.go:1191.
void computeFileBasedTestConfigurationVariationsWorker(
    std::vector<TestConfiguration>* configurations,
    const std::vector<std::vector<std::string>>& optionEntries, int index,
    const TestConfiguration& variationState) {
	if (index >= (int)optionEntries.size()) {
		configurations->push_back(variationState);
		return;
	}

	const auto& optionKey = optionEntries[index][0];
	for (size_t ei = 1; ei < optionEntries[index].size(); ei++) {
		// set or overwrite the variation, then compute the next variation
		TestConfiguration next = variationState;
		next[optionKey] = optionEntries[index][ei];
		computeFileBasedTestConfigurationVariationsWorker(
		    configurations, optionEntries, index + 1, next);
	}
}

std::vector<TestConfiguration> computeFileBasedTestConfigurationVariations(
    int variationCount,
    const std::vector<std::vector<std::string>>& optionEntries) {
	std::vector<TestConfiguration> configurations;
	configurations.reserve(variationCount);
	computeFileBasedTestConfigurationVariationsWorker(
	    &configurations, optionEntries, 0, TestConfiguration{});
	return configurations;
}

// failOnUnsupportedCompilerOptions — harnessutil.go:1263.
void failOnUnsupportedCompilerOptions(gostd::testing::T* t,
                                      const CompilerOptions* options) {
	// t.Helper() — no-op here.
	if (options->Module == ModuleKind::AMD) {
		t->Fatalf("unsupported module kind %s",
		          {moduleKindString(options->Module)});
	}
	if (!options->OutFile.empty()) {
		t->Fatalf("unsupported outFile %s", {options->OutFile});
	}
}

// modulekind_stringer_generated.go — ModuleKind.String() (used by the %s
// verbs above). Also scripttarget_stringer_generated.go for Target.
const char* moduleKindString(ModuleKind k) {
	switch (k) {
	case ModuleKind::None: return "None";
	case ModuleKind::CommonJS: return "CommonJS";
	case ModuleKind::AMD: return "AMD";
	case ModuleKind::UMD: return "UMD";
	case ModuleKind::System: return "System";
	case ModuleKind::ES2015: return "ES2015";
	case ModuleKind::ES2020: return "ES2020";
	case ModuleKind::ES2022: return "ES2022";
	case ModuleKind::ESNext: return "ESNext";
	case ModuleKind::Node16: return "Node16";
	case ModuleKind::Node18: return "Node18";
	case ModuleKind::Node20: return "Node20";
	case ModuleKind::NodeNext: return "NodeNext";
	case ModuleKind::Preserve: return "Preserve";
	}
	return "";
}

std::string scriptTargetString(ScriptTarget t) {
	switch (t) {
	case ScriptTarget::None: return "None";
	case ScriptTarget::ES5: return "ES5";
	case ScriptTarget::ES2015: return "ES2015";
	case ScriptTarget::ES2016: return "ES2016";
	case ScriptTarget::ES2017: return "ES2017";
	case ScriptTarget::ES2018: return "ES2018";
	case ScriptTarget::ES2019: return "ES2019";
	case ScriptTarget::ES2020: return "ES2020";
	case ScriptTarget::ES2021: return "ES2021";
	case ScriptTarget::ES2022: return "ES2022";
	case ScriptTarget::ES2023: return "ES2023";
	case ScriptTarget::ES2024: return "ES2024";
	case ScriptTarget::ES2025: return "ES2025";
	case ScriptTarget::ES2026: return "ES2026";
	case ScriptTarget::ESNext: return "ESNext";
	case ScriptTarget::JSON: return "JSON";
	}
	return "ScriptTarget(" + std::to_string((int)t) + ")";
}

}  // namespace

// === CompileFiles — harnessutil.go:81 ===

CompilationResult* CompileFiles(
    gostd::testing::T* t, const std::vector<TestFile*>& inputFiles,
    const std::vector<TestFile*>& otherFiles,
    const TestConfiguration& testConfig,
    tsoptions::ParsedCommandLine* tsconfig, const std::string& currentDirectory,
    const std::unordered_map<std::string, std::string>& symlinks) {
	CompilerOptions* compilerOptionsPtr = nullptr;
	if (tsconfig != nullptr) {
		compilerOptionsPtr =
		    const_cast<CompilerOptions*>(tsconfig->CompilerOptions())->Clone();
	}
	if (compilerOptionsPtr == nullptr) {
		compilerOptionsPtr = new CompilerOptions();
	}
	// Set default options for tests
	if (compilerOptionsPtr->NewLine == NewLineKind::None) {
		compilerOptionsPtr->NewLine = NewLineKind::CarriageReturnLineFeed;
	}
	if (compilerOptionsPtr->SkipDefaultLibCheck == Tristate::Unknown) {
		compilerOptionsPtr->SkipDefaultLibCheck = Tristate::True;
	}
	compilerOptionsPtr->NoErrorTruncation = Tristate::True;
	auto* harnessOptions = new HarnessOptions{
	    /*UseCaseSensitiveFileNames*/ true};
	harnessOptions->CurrentDirectory = currentDirectory;

	// Parse harness and compiler options from the test configuration
	if (!testConfig.empty()) {
		SetOptionsFromTestConfig(t, testConfig, compilerOptionsPtr,
		                         harnessOptions, currentDirectory,
		                         false /*allowUnknownOptions*/);
	}

	return CompileFilesEx(t, inputFiles, otherFiles, harnessOptions,
	                      compilerOptionsPtr, currentDirectory, symlinks,
	                      tsconfig);
}

// testLibFolderMap — harnessutil.go:250 (sync.OnceValue).
const std::unordered_map<std::string, vfstest::MapFileInput>&
testLibFolderMap() {
	static auto* result = [] {
		auto* testfs =
		    new std::unordered_map<std::string, vfstest::MapFileInput>();
		namespace fsns = std::filesystem;
		auto libRoot = fsns::path(repo::testDataPath()) / "tests" / "lib";
		std::error_code ec;
		// fs.WalkDir — sorted lexical order per directory.
		std::vector<fsns::path> stack{libRoot};
		while (!stack.empty()) {
			auto dir = stack.back();
			stack.pop_back();
			std::vector<fsns::path> entries;
			for (auto& e : fsns::directory_iterator(dir, ec)) {
				entries.push_back(e.path());
			}
			if (ec) {
				TSC_UNREACHABLE(
				    ("Failed to read lib dir: " + ec.message())
				        .c_str());
			}
			std::sort(entries.begin(), entries.end());
			for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
				// Only directories are descended (Go fs.WalkDir); pushing
				// files makes directory_iterator fail with ENOTDIR.
				if (fsns::is_directory(*it)) {
					stack.push_back(*it); // reversed so iteration pops in order
				}
			}
			for (auto& p : entries) {
				if (fsns::is_directory(p)) continue;
				std::ifstream f(p, std::ios::binary);
				std::string content((std::istreambuf_iterator<char>(f)),
				                    std::istreambuf_iterator<char>());
				// `testLibFolder + "/" + path` — path is relative with
				// forward slashes (WalkDir on '/'-joined root).
				auto rel = fsns::relative(p, libRoot, ec).generic_string();
				auto* mf = new vfstest::fstest::MapFile();
				mf->Data = std::move(content);
				testfs->emplace(
				    std::string(testLibFolder) + "/" + rel,
				    vfstest::MapFileInput(
				        std::shared_ptr<vfstest::fstest::MapFile>(mf)));
			}
		}
		return testfs;
	}();
	return *result;
}

// === CompileFilesEx — harnessutil.go:119 ===

CompilationResult* CompileFilesEx(
    gostd::testing::T* t, const std::vector<TestFile*>& inputFiles,
    const std::vector<TestFile*>& otherFiles, HarnessOptions* harnessOptions,
    CompilerOptions* compilerOptions, const std::string& currentDirectory,
    const std::unordered_map<std::string, std::string>& symlinks,
    tsoptions::ParsedCommandLine* tsconfig) {
	std::vector<std::string> programFileNames;
	for (auto* file : inputFiles) {
		auto fileName =
		    tspath::getNormalizedAbsolutePath(file->UnitName, currentDirectory);
		if (!tspath::fileExtensionIs(fileName, tspath::extensionJson) &&
		    !tspath::fileExtensionIs(fileName, tspath::extensionTsBuildInfo)) {
			programFileNames.push_back(fileName);
		}
	}

	// Performance optimization; avoid copying in the /.lib folder if the
	// test doesn't need it.
	bool includeLibDir = false;
	for (auto* file : inputFiles) {
		if (file->Content.find(std::string(testLibFolder) + "/") !=
		    std::string::npos) {
			includeLibDir = true;
			break;
		}
	}

	// Files from testdata\lib that are requested by "@libFiles"
	if (!harnessOptions->LibFiles.empty()) {
		for (const auto& libFile : harnessOptions->LibFiles) {
			if (libFile == "lib.d.ts" &&
			    compilerOptions->NoLib != Tristate::True) {
				// We used to override lib with a custom lib.d.ts for some
				// reason. Skip this unless it becomes necessary.
				continue;
			}
			programFileNames.push_back(
			    tspath::combinePaths(testLibFolder, {libFile}));
			includeLibDir = true;
		}
	}

	// !!!
	// ts.assign(options, ts.convertToOptionsWithAbsolutePaths(options, ...
	if (!compilerOptions->OutDir.empty()) {
		compilerOptions->OutDir = tspath::getNormalizedAbsolutePath(
		    compilerOptions->OutDir, currentDirectory);
	}
	if (!compilerOptions->Project.empty()) {
		compilerOptions->Project = tspath::getNormalizedAbsolutePath(
		    compilerOptions->Project, currentDirectory);
	}
	if (!compilerOptions->RootDir.empty()) {
		compilerOptions->RootDir = tspath::getNormalizedAbsolutePath(
		    compilerOptions->RootDir, currentDirectory);
	}
	if (!compilerOptions->TsBuildInfoFile.empty()) {
		compilerOptions->TsBuildInfoFile =
		    tspath::getNormalizedAbsolutePath(compilerOptions->TsBuildInfoFile,
		                                      currentDirectory);
	}
	if (!compilerOptions->BaseUrl.empty()) {
		compilerOptions->BaseUrl = tspath::getNormalizedAbsolutePath(
		    compilerOptions->BaseUrl, currentDirectory);
	}
	if (!compilerOptions->DeclarationDir.empty()) {
		compilerOptions->DeclarationDir = tspath::getNormalizedAbsolutePath(
		    compilerOptions->DeclarationDir, currentDirectory);
	}
	for (auto& rootDir : compilerOptions->RootDirs) {
		rootDir = tspath::getNormalizedAbsolutePath(rootDir, currentDirectory);
	}
	for (auto& typeRoot : compilerOptions->TypeRoots) {
		typeRoot =
		    tspath::getNormalizedAbsolutePath(typeRoot, currentDirectory);
	}

	std::vector<contentmapper::Mapper*> contentMappers;
	if (tsconfig != nullptr && tsconfig->ParsedConfig != nullptr) {
		contentMappers = tsconfig->ParsedConfig->ContentMappers;
	}

	// Create fake FS for testing
	std::unordered_map<std::string, vfstest::MapFileInput> testfs;
	for (auto* file : inputFiles) {
		auto fileName = tspath::getNormalizedAbsolutePath(file->UnitName,
		                                                currentDirectory);
		auto* mf = new vfstest::fstest::MapFile();
		mf->Data = file->Content;
		testfs[fileName] = vfstest::MapFileInput(
		    std::shared_ptr<vfstest::fstest::MapFile>(mf));
	}
	for (auto* file : otherFiles) {
		auto fileName = tspath::getNormalizedAbsolutePath(file->UnitName,
		                                                currentDirectory);
		auto* mf = new vfstest::fstest::MapFile();
		mf->Data = file->Content;
		testfs[fileName] = vfstest::MapFileInput(
		    std::shared_ptr<vfstest::fstest::MapFile>(mf));
	}
	for (const auto& [src, target] : symlinks) {
		auto srcFileName =
		    tspath::getNormalizedAbsolutePath(src, currentDirectory);
		auto targetFileName =
		    tspath::getNormalizedAbsolutePath(target, currentDirectory);
		testfs[srcFileName] =
		    vfstest::MapFileInput(vfstest::Symlink(targetFileName));
	}

	if (includeLibDir) {
		// maps.Copy(testfs, testLibFolderMap())
		for (const auto& [k, v] : testLibFolderMap()) {
			testfs[k] = v;
		}
	}

	std::shared_ptr<vfs::FS> fs = vfstest::FromMap(
	    testfs, harnessOptions->UseCaseSensitiveFileNames);
	fs = bundled::WrapFS(fs);
	fs = NewOutputRecorderFS(fs);

	// Content mappers, when trusted, are served in-process by the test
	// mapper (see contentmappertest). The host is shared by the pre- and
	// post-emit programs and torn down when this compilation finishes.
	std::shared_ptr<contentmapper::Host> contentMapperHost;
	if (tristateIsTrue(compilerOptions->RunExternalCode) &&
	    !contentMappers.empty()) {
		contentMapperHost =
		    contentmapper::NewHost(gostd::contextBackground(),
		                           contentmappertest::NewSpawner(),
		                           locale::Default);
	}
	auto closeMapperHost = [&] {
		if (contentMapperHost) contentMapperHost->Close();
	};
	// defer contentMapperHost.Close() — best-effort at scope exit.
	struct Defer {
		std::function<void()> f;
		~Defer() { f(); }
	};
	std::unique_ptr<Defer> deferCloseHost;
	if (contentMapperHost) {
		deferCloseHost =
		    std::make_unique<Defer>(Defer{closeMapperHost});
	}

	tsoptions::TsConfigSourceFile* configFile = nullptr;
	std::vector<Diagnostic*> errors;
	if (tsconfig != nullptr) {
		configFile = tsconfig->ConfigFile;
		errors = tsconfig->Errors;
	}
	auto* configParsed = new tsoptions::ParsedOptions();
	configParsed->CompilerOptions = compilerOptions;
	configParsed->FileNames = programFileNames;
	configParsed->ContentMappers = contentMappers;
	auto* config = new tsoptions::ParsedCommandLine();
	config->ParsedConfig = configParsed;
	config->ConfigFile = configFile;
	config->Errors = errors;
	std::shared_ptr<contentmapper::Project> contentMapperProject;
	if (contentMapperHost != nullptr) {
		contentmapper::ProjectSpec spec;
		spec.ConfigFileName = config->ConfigName();
		spec.Mappers = config->ContentMappers();
		spec.CompilerOptions =
		    const_cast<CompilerOptions*>(config->CompilerOptions());
		contentMapperProject = contentMapperHost->Project(spec);
	}
	auto closeMapperProject = [&] {
		if (contentMapperProject) contentMapperProject->Close();
	};
	std::unique_ptr<Defer> deferCloseProject;
	if (contentMapperProject) {
		deferCloseProject =
		    std::make_unique<Defer>(Defer{closeMapperProject});
	}
	auto* host = createCompilerHost(fs, bundled::LibPath(), currentDirectory,
	                                contentMapperProject);
	auto* result = compileFilesWithHost(host, config, harnessOptions);
	result->Symlinks = symlinks;
	result->Trace = host->tracer->String();
	result->Repeat =
	    [t, inputFiles, otherFiles, harnessOptions, compilerOptions,
	     currentDirectory, symlinks,
	     tsconfig](TestConfiguration testConfig) -> CompilationResult* {
		HarnessOptions newHarnessOptions = *harnessOptions;
		auto* newCompilerOptions = compilerOptions->Clone();
		SetOptionsFromTestConfig(t, testConfig, newCompilerOptions,
		                         &newHarnessOptions, currentDirectory,
		                         false /*allowUnknownOptions*/);
		return CompileFilesEx(t, inputFiles, otherFiles,
		                      &newHarnessOptions, newCompilerOptions,
		                      currentDirectory, symlinks, tsconfig);
	};
	return result;
}

// === SetOptionsFromTestConfig — harnessutil.go:291 ===

void SetOptionsFromTestConfig(gostd::testing::T* t,
                              const TestConfiguration& testConfig,
                              CompilerOptions* compilerOptionsPtr,
                              HarnessOptions* harnessOptions,
                              const std::string& currentDirectory,
                              bool allowUnknownOptions) {
	// Go map iteration order is random; order doesn't matter here since each
	// option is applied independently.
	for (const auto& [name, value] : testConfig) {
		if (name == "typescriptversion") {
			continue;
		}

		auto* commandLineOption = getCommandLineOption(name);
		if (commandLineOption != nullptr) {
			auto parsedValue = getOptionValue(t, commandLineOption, value,
			                                  currentDirectory);
			auto errors = tsoptions::ParseCompilerOptions(
			    commandLineOption->Name, parsedValue, compilerOptionsPtr);
			if (!errors.empty()) {
				t->Fatalf(
				    "Error parsing value '%s' for compiler option '%s'.",
				    {value, commandLineOption->Name});
			}
			continue;
		}
		auto* harnessOption = getHarnessOption(name);
		if (harnessOption != nullptr) {
			auto parsedValue = getOptionValue(t, harnessOption, value,
			                                  currentDirectory);
			parseHarnessOption(t, harnessOption->Name, parsedValue,
			                   harnessOptions);
			continue;
		}
		if (!allowUnknownOptions) {
			t->Fatalf("Unknown compiler option '%s'.", {name});
		}
	}
}

// === getOutputPath — harnessutil.go:836 ===

std::string CompilationResult::getOutputPath(const std::string& pathIn,
                                             const std::string& ext) {
	std::string path =
	    tspath::resolvePath(Host->GetCurrentDirectory(), {pathIn});
	std::string outDir;
	if (ext == ".d.ts" || ext == ".d.mts" || ext == ".d.cts" ||
	    (ext.size() >= 3 && ext.find(".ts") != std::string::npos &&
	     ext.find(".d.") != std::string::npos)) {
		outDir = Options->DeclarationDir;
		if (outDir.empty()) {
			outDir = Options->OutDir;
		}
	} else {
		outDir = Options->OutDir;
	}
	if (!outDir.empty()) {
		auto common = Program->CommonSourceDirectory();
		if (!common.empty()) {
			path = tspath::getRelativePathFromDirectory(
			    common, path,
			    tspath::ComparePathsOptions{
			        Host->FS()->UseCaseSensitiveFileNames(),
			        Host->GetCurrentDirectory()});
			path = tspath::combinePaths(
			    tspath::resolvePath(Host->GetCurrentDirectory(),
			                        {Options->OutDir}),
			    {path});
		}
	}
	if (ext ==
	    tspath::getDeclarationEmitExtensionForPath(path)) {
		return outputpaths::ChangeToDeclarationExtension(path, Program->GetProgram());
	}
	return tspath::changeExtension(path, ext);
}

// FS — harnessutil.go:861.
std::shared_ptr<vfs::FS> CompilationResult::FS() { return Host->FS(); }

// GetNumberOfJSFiles — harnessutil.go:865.
int CompilationResult::GetNumberOfJSFiles(bool includeJson) {
	if (includeJson) {
		return (int)JS.Size();
	}
	int count = 0;
	for (auto* file : JS.Values()) {
		if (!tspath::fileExtensionIs(file->UnitName, tspath::extensionJson)) {
			count++;
		}
	}
	return count;
}

// GetInputsAndOutputsForFile — harnessutil.go:885.
CompilationOutput* CompilationResult::GetInputsAndOutputsForFile(
    const std::string& path) {
	return inputsAndOutputs.GetOrZero(
	    tspath::resolvePath(Host->GetCurrentDirectory(), {path}));
}

// GetInputsForFile — harnessutil.go:889.
std::vector<TestFile*> CompilationResult::GetInputsForFile(
    const std::string& path) {
	auto* outputs = GetInputsAndOutputsForFile(path);
	if (outputs != nullptr) {
		return outputs->Inputs;
	}
	return {};
}

// GetOutput — harnessutil.go:897.
TestFile* CompilationResult::GetOutput(const std::string& path,
                                       const std::string& kind) {
	auto* outputs = GetInputsAndOutputsForFile(path);
	if (outputs != nullptr) {
		if (kind == "js") return outputs->JS;
		if (kind == "dts") return outputs->DTS;
		if (kind == "map") return outputs->Map;
	}
	return nullptr;
}

// GetSourceMapRecord — harnessutil.go:914.
std::string CompilationResult::GetSourceMapRecord() {
	if (Result == nullptr || Result->SourceMaps.empty()) {
		return "";
	}

	writerAggregator sourceMapRecorder;
	for (auto& sourceMapData : Result->SourceMaps) {
		SourceFile* prevSourceFile = nullptr;
		TestFile* currentFile;

		if (tspath::isDeclarationFileName(sourceMapData.GeneratedFile)) {
			currentFile = DTS.GetOrZero(sourceMapData.GeneratedFile);
		} else {
			currentFile = JS.GetOrZero(sourceMapData.GeneratedFile);
		}

		auto* sourceMapSpanWriter_ = newSourceMapSpanWriter(
		    &sourceMapRecorder, sourceMapData.SourceMap, currentFile);
		auto* mapper =
		    sourcemap::DecodeMappings(sourceMapData.SourceMap->Mappings);
		mapper->Values([&](sourcemap::Mapping* decodedSourceMapping) {
			if (!decodedSourceMapping->IsSourceMapping()) {
				sourceMapSpanWriter_->recordSourceMapSpan(
				    decodedSourceMapping);
				return true;
			}
			auto* currentSourceFile = Program->GetSourceFile(
			    sourceMapData
			        .InputSourceFileNames[decodedSourceMapping->SourceIndex]);
			if (currentSourceFile != prevSourceFile) {
				if (currentSourceFile != nullptr) {
					sourceMapSpanWriter_->recordNewSourceFileSpan(
					    decodedSourceMapping,
					    currentSourceFile->OriginalText());
				}
				prevSourceFile = currentSourceFile;
			} else {
				sourceMapSpanWriter_->recordSourceMapSpan(
				    decodedSourceMapping);
			}
			return true;
		});
		sourceMapSpanWriter_->close();
	}
	return sourceMapRecorder.String();
}

// === EnumerateFiles — harnessutil.go:989 ===

namespace {

// listFilesWorker — harnessutil.go:1001.
std::pair<std::vector<std::string>, gostd::Error> listFilesWorker(
    const gostd::regexp::Regexp* spec, bool recursive,
    const std::string& folderIn) {
	auto folder = tspath::getNormalizedAbsolutePath(folderIn,
	                                                repo::testDataPath());
	namespace fsns = std::filesystem;
	std::error_code ec;
	std::vector<fsns::path> entries;
	for (auto& e : fsns::directory_iterator(folder, ec)) {
		entries.push_back(e.path());
	}
	if (ec) {
		return {{}, gostd::newError(ec.message())};
	}
	// os.ReadDir sorts by filename.
	std::sort(entries.begin(), entries.end());
	std::vector<std::string> paths;
	for (auto& p : entries) {
		std::string path = tspath::normalizePath(p.generic_string());
		if (!fsns::is_directory(p)) {
			if (spec == nullptr || spec->MatchString(path)) {
				paths.push_back(path);
			}
		} else if (recursive) {
			auto [subPaths, err] = listFilesWorker(spec, recursive, path);
			if (err) {
				return {{}, err};
			}
			for (auto& s : subPaths) paths.push_back(s);
		}
	}
	return {paths, nullptr};
}

}  // namespace

std::pair<std::vector<std::string>, gostd::Error> EnumerateFiles(
    const std::string& folder, const gostd::regexp::Regexp* testRegex,
    bool recursive) {
	auto [files, err] = listFilesWorker(testRegex, recursive, folder);
	if (err) {
		return {{}, err};
	}
	std::vector<std::string> out;
	for (const auto& f : files) {
		out.push_back(tspath::normalizeSlashes(f));
	}
	return {out, nullptr};
}

// === GetFileBasedTestConfigurations — harnessutil.go:1038 ===

std::vector<NamedTestConfiguration*> GetFileBasedTestConfigurations(
    gostd::testing::T* t,
    const std::unordered_map<std::string, std::string>& settings,
    const std::unordered_set<std::string>& varyByOptions) {
	std::vector<std::vector<std::string>>
	    optionEntries; // Each element slice has the option name as the first
	                   // element, and the values as the rest
	int variationCount = 1;
	TestConfiguration nonVaryingOptions;
	for (const auto& [option, value] : settings) {
		if (varyByOptions.count(option)) {
			auto entries = splitOptionValues(t, value, option);
			if (entries.size() > 1) {
				variationCount *= (int)entries.size();
				if (variationCount > 25) {
					t->Fatal(
					    {"Provided test options exceeded the maximum number "
					     "of variations"});
				}
				std::vector<std::string> e{option};
				for (auto& s : entries) e.push_back(s);
				optionEntries.push_back(std::move(e));
			} else if (entries.size() == 1) {
				nonVaryingOptions[option] = entries[0];
			}
		} else {
			// Variation is not supported for the option
			nonVaryingOptions[option] = value;
		}
	}

	std::vector<NamedTestConfiguration*> configurations;
	if (!optionEntries.empty()) {
		// Merge varying and non-varying options
		auto varyingConfigurations =
		    computeFileBasedTestConfigurationVariations(variationCount,
		                                                optionEntries);
		for (auto& varyingConfig : varyingConfigurations) {
			auto description =
			    getFileBasedTestConfigurationDescription(varyingConfig);
			for (const auto& [k, v] : nonVaryingOptions) {
				varyingConfig[k] = v;
			}
			auto* cfg = new NamedTestConfiguration();
			cfg->Name = description;
			cfg->Config = std::move(varyingConfig);
			configurations.push_back(cfg);
		}
	} else if (!nonVaryingOptions.empty()) {
		// Only non-varying options
		auto* cfg = new NamedTestConfiguration();
		cfg->Config = nonVaryingOptions;
		configurations.push_back(cfg);
	}
	return configurations;
}

// === GetConfigNameFromFileName — harnessutil.go:1228 ===

std::string GetConfigNameFromFileName(std::string_view filename) {
	auto basenameLower =
	    toLower(tspath::getBaseFileName(filename));
	if (basenameLower == "tsconfig.json" || basenameLower == "jsconfig.json") {
		return basenameLower;
	}
	return "";
}

// === SkipUnsupportedCompilerOptions — harnessutil.go:1236 ===

void SkipUnsupportedCompilerOptions(gostd::testing::T* t,
                                    const CompilerOptions* options) {
	failOnUnsupportedCompilerOptions(t, options);
	if (options->Module == ModuleKind::UMD ||
	    options->Module == ModuleKind::System) {
		t->Skipf("unsupported module kind %s",
		         {moduleKindString(options->Module)});
	}
	if (options->ModuleResolution == ModuleResolutionKind::Node10 ||
	    options->ModuleResolution == ModuleResolutionKind::Classic) {
		t->Skipf("unsupported module resolution kind %d",
		         {(int64_t)options->ModuleResolution});
	}
	if (tristateIsFalse(options->ESModuleInterop)) {
		t->Skipf("esModuleInterop=false is unsupported", {});
	}
	if (tristateIsFalse(options->AllowSyntheticDefaultImports)) {
		t->Skipf("allowSyntheticDefaultImports=false is unsupported", {});
	}
	if (!options->BaseUrl.empty()) {
		t->Skipf("unsupported baseUrl %s", {options->BaseUrl});
	}
	if (options->Target == ScriptTarget::ES5) {
		t->Skipf("unsupported target %s",
		         {scriptTargetString(options->Target)});
	}
	if (tristateIsFalse(options->AlwaysStrict)) {
		t->Skipf("alwaysStrict=false is unsupported", {});
	}
}

}  // namespace tsc::testutil::harnessutil
