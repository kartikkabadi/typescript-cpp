// transpile — transpile.go + fs.go: single-file JavaScript and declaration
// emit over an in-memory FS.
#include "internal/transpile/transpile.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/compiler/program.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc::transpile {

// inputDirectory — transpile.go:42. The synthetic current directory used
// to root the single input file created for transpilation.
static constexpr std::string_view inputDirectory = "/";

// libDirectory — transpile.go:47. The synthetic directory that the
// barebones default library file is placed in for declaration
// transpilation. See barebonesLibContent.
static constexpr std::string_view libDirectory = "/lib";

// barebonesLibContent — transpile.go:56. Declaration emit works without a
// `lib`, but some local inferences you'd expect to work won't without at
// least a minimal `lib` available, since the checker will type inferred
// declarations as `any` without these defined. Late bound symbol names, in
// particular, are impossible to define without `Symbol` at least
// partially defined.
// TODO: This should *probably* just load the full, real `lib` for the
// target.
static constexpr std::string_view barebonesLibContent = R"TS(interface Boolean {}
interface Function {}
interface CallableFunction {}
interface NewableFunction {}
interface IArguments {}
interface Number {}
interface Object {}
interface RegExp {}
interface String {}
interface Array<T> { length: number; [n: number]: T; }
interface SymbolConstructor {
    (desc?: string | number): symbol;
    for(name: string): symbol;
    readonly toStringTag: symbol;
}
declare var Symbol: SymbolConstructor;
interface Symbol {
    readonly [Symbol.toStringTag]: string;
})TS";

// --- fs.go: transpileFS ---
// transpileFS implements the vfs.FS operations the transpiler needs; the
// rest panic like Go's nil-embedded vfs.FS.
class transpileFS : public vfs::FS {
public:
	std::unordered_map<std::string, std::string> files;

	bool UseCaseSensitiveFileNames() override { return true; }

	bool FileExists(const std::string& path) override {
		auto it = files.find(path);
		if (it == files.end()) {
			tscUnreachable(
			    ("unexpected file existence check for \"" + path + "\"")
			        .c_str());
		}
		return true;
	}

	std::pair<std::string, bool> ReadFile(const std::string& path) override {
		auto it = files.find(path);
		if (it == files.end()) {
			tscUnreachable(
			    ("unexpected file read for \"" + path + "\"").c_str());
		}
		return {it->second, true};
	}

	bool DirectoryExists(const std::string& path) override {
		tscUnreachable(
		    ("unexpected directory existence check for \"" + path + "\"")
		        .c_str());
	}

	std::string Realpath(const std::string& path) override {
		tscUnreachable(
		    ("unexpected realpath request for \"" + path + "\"").c_str());
	}

	vfs::Error WriteFile(const std::string& path,
	                     const std::string& data) override {
		tscUnreachable(("unexpected file write for \"" + path + "\"").c_str());
	}

	vfs::Error AppendFile(const std::string& path,
	                      const std::string& data) override {
		tscUnreachable(("unexpected file append for \"" + path + "\"").c_str());
	}

	vfs::Error Remove(const std::string& path) override {
		tscUnreachable(("unexpected file remove for \"" + path + "\"").c_str());
	}

	vfs::Error Chtimes(const std::string& path, vfs::TimePoint aTime,
	                   vfs::TimePoint mTime) override {
		tscUnreachable(("unexpected chtimes for \"" + path + "\"").c_str());
	}

	vfs::Entries GetAccessibleEntries(const std::string& path) override {
		tscUnreachable(
		    ("unexpected directory read for \"" + path + "\"").c_str());
	}

	std::shared_ptr<vfs::FileInfo> Stat(const std::string& path) override {
		tscUnreachable(("unexpected stat for \"" + path + "\"").c_str());
	}
};

// transpileWorker — transpile.go:118. (Go context.Context param dropped.)
static Output* transpileWorker(const std::string& input,
                               const Options& options, bool declaration);

// TranspileModule — transpile.go:82.
Output* TranspileModule(const std::string& input, const Options& options) {
	return transpileWorker(input, options, false /*declaration*/);
}

// TranspileDeclaration — transpile.go:115.
Output* TranspileDeclaration(const std::string& input, const Options& options) {
	return transpileWorker(input, options, true /*declaration*/);
}

// transpileWorker — transpile.go:118. (Go context.Context param dropped.)
static Output* transpileWorker(const std::string& input,
                               const Options& options, bool declaration) {
	CompilerOptions opts;
	if (options.CompilerOptions != nullptr) {
		opts = *options.CompilerOptions; // Clone — all fields are value types
	}

	// Clear options that do not apply to single-file transpilation.
	opts.Incremental = Tristate::Unknown;
	opts.Declaration = Tristate::Unknown;
	opts.EmitDeclarationOnly = Tristate::Unknown;
	opts.NoEmit = Tristate::Unknown;
	opts.Lib.clear();
	opts.OutFile = "";
	opts.Composite = Tristate::Unknown;
	opts.TsBuildInfoFile = "";
	opts.Paths.clear();
	opts.RootDirs.clear();
	opts.Types.clear();
	opts.AllowImportingTsExtensions = Tristate::Unknown;
	opts.NoEmitOnError = Tristate::Unknown;
	opts.DeclarationDir = "";

	// Do not set `isolatedModules` if `verbatimModuleSyntax` was supplied,
	// since it would be redundant.
	if (!tristateIsTrue(opts.VerbatimModuleSyntax)) {
		opts.IsolatedModules = Tristate::True;
	}
	opts.NoCheck = Tristate::True;
	opts.NoResolve = Tristate::True;

	// transpileModule/transpileDeclaration do not write anything to disk,
	// so there's no need to verify there are no conflicts between input
	// and output paths.
	opts.SuppressOutputPathCheck = Tristate::True;

	// FileName can be a non-ts file.
	opts.AllowNonTsExtensions = Tristate::True;

	if (declaration) {
		opts.Declaration = Tristate::True;
		opts.EmitDeclarationOnly = Tristate::True;
		opts.IsolatedDeclarations = Tristate::True;
	} else {
		opts.Declaration = Tristate::False;
		opts.DeclarationMap = Tristate::False;
		opts.IsolatedDeclarations = Tristate::False;
	}

	// When transpiling declarations, we need a lib.
	// GetDefaultLibFileName will cause the barebones lib below to be used
	// instead of a real lib.
	if (declaration) {
		opts.NoLib = Tristate::False;
	} else {
		opts.NoLib = Tristate::True;
	}

	// If jsx is specified, then treat the file as .tsx.
	std::string fileName = options.FileName;
	if (fileName.empty()) {
		if (opts.Jsx != JsxEmit::None) {
			fileName = "module.tsx";
		} else {
			fileName = "module.ts";
		}
	}
	std::string inputFileName =
	    tspath::getNormalizedAbsolutePath(fileName, std::string(inputDirectory));

	auto fs = std::make_shared<transpileFS>();
	fs->files.emplace(inputFileName, input);

	// Declaration emit needs a default lib to resolve global types (e.g.
	// `Array`, `Symbol`); plain transpilation sets NoLib so none is read.
	// The default lib name depends on the configured target.
	if (declaration) {
		std::string libFileName =
		    std::string(tsoptions::getDefaultLibFileName(&opts));
		fs->files.emplace(
		    tspath::combinePaths(std::string(libDirectory), {libFileName}),
		    std::string(barebonesLibContent));
	}

	compiler::CompilerHost host;
	host.currentDirectory = std::string(inputDirectory);
	host.fs = fs;
	host.defaultLibraryPath = std::string(libDirectory);

	compiler::SimpleProgram program(&host, opts, {inputFileName},
	                                true /*skipModuleResolution*/);

	std::vector<Diagnostic*> allDiagnostics;
	if (options.ReportDiagnostics) {
		SourceFile* sourceFile = program.GetSourceFile(inputFileName);
		auto syntactic = program.GetSyntacticDiagnostics(sourceFile);
		allDiagnostics.insert(allDiagnostics.end(), syntactic.begin(),
		                      syntactic.end());
		auto configDiags = program.GetConfigFileParsingDiagnostics();
		allDiagnostics.insert(allDiagnostics.end(), configDiags.begin(),
		                      configDiags.end());
		auto programDiags = program.GetProgramDiagnostics();
		allDiagnostics.insert(allDiagnostics.end(), programDiags.begin(),
		                      programDiags.end());
	}

	compiler::EmitOnly emitOnly = compiler::EmitOnly::EmitAll;
	if (declaration) {
		emitOnly = compiler::EmitOnly::EmitOnlyDts;
	}

	std::string outputText;
	std::string sourceMapText;
	bool hasOutputText = false;
	bool hasSourceMapText = false;

	compiler::EmitOptions emitOptions;
	emitOptions.EmitOnly = emitOnly;
	emitOptions.ForceEmit = declaration;
	emitOptions.WriteFile =
	    [&outputText, &sourceMapText, &hasOutputText, &hasSourceMapText](
	        const std::string& fileName, const std::string& text,
	        compiler::WriteFileData* data) -> std::optional<std::string> {
		if (fileName.size() >= 4 &&
		    fileName.compare(fileName.size() - 4, 4, ".map") == 0) {
			if (hasSourceMapText) {
				tscUnreachable(("Unexpected multiple source map outputs, "
				                "file: " +
				                fileName)
				                   .c_str());
			}
			sourceMapText = text;
			hasSourceMapText = true;
		} else {
			if (hasOutputText) {
				tscUnreachable(
				    ("Unexpected multiple outputs, file: " + fileName)
				        .c_str());
			}
			outputText = text;
			hasOutputText = true;
		}
		return std::nullopt;
	};

	compiler::EmitResult* result = program.Emit(&emitOptions);
	if (result == nullptr) {
		return nullptr;
	}

	// Diagnostics produced during emit (e.g. isolated declaration errors)
	// are always included, regardless of ReportDiagnostics.
	allDiagnostics.insert(allDiagnostics.end(), result->Diagnostics.begin(),
	                      result->Diagnostics.end());

	if (!hasOutputText) {
		tscUnreachable("Output generation failed");
	}

	auto* out = new Output;
	out->OutputText = std::move(outputText);
	out->Diagnostics = std::move(allDiagnostics);
	out->SourceMapText = std::move(sourceMapText);
	return out;
}

} // namespace tsc::transpile
