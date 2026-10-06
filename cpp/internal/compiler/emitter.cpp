// emitter.cpp — port of tsc/internal/compiler/emitter.go + emitHost.go.
// The per-file emit driver: run transforms, print, write JS/d.ts/sourcemaps.

#include "internal/compiler/emitter.h"

#include "internal/ast/diagnostics_util.h"
#include "internal/binder/referenceresolver.h"
#include "internal/core/text.h"
#include "internal/spanmap/spanmap.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/transformers/estransforms/estransforms.h"
#include "internal/transformers/inliners/inliners.h"
#include "internal/transformers/jsxtransforms/jsxtransforms.h"
#include "internal/transformers/moduletransforms/moduletransforms.h"
#include "internal/transformers/tstransforms/tstransforms.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace tsc::compiler {

using ::tsc::transformers::Transformer;
using ::tsc::transformers::TransformOptions;


namespace {

// emitHost.go:88 GetOutputPathsFor — wraps outputpaths::OutputPaths in the
// transformers::declarations::OutputPaths interface (Go structural interface).
struct outputPathsAdapter : transformers::declarations::OutputPaths {
	outputpaths::OutputPaths paths;
	std::string DeclarationFilePath() override {
		return paths.DeclarationFilePath();
	}
	std::string JsFilePath() override { return paths.JsFilePath(); }
};

// emitter.go:274 declarationMapSource — a sourcemap::Source over the
// declaration's original (content-mapped) text.
struct declarationMapSource : sourcemap::Source {
	std::string fileName;
	std::string text;
	std::vector<TextPos> lineMap;

	std::string_view FileName() override { return fileName; }
	std::string_view Text() override { return text; }
	const std::vector<TextPos>& ECMALineMap() override { return lineMap; }
};

declarationMapSource* newDeclarationMapSource(SourceFile* sourceFile) {
	std::string text = std::string(sourceFile->OriginalText());
	auto* result = new declarationMapSource;
	result->fileName = sourceFile->OriginalFileName();
	result->text = text;
	result->lineMap = computeECMALineStarts(text);
	return result;
}

} // namespace

// --- emitHost.go ---

// emitHost.go:88
transformers::declarations::OutputPaths* emitHost::GetOutputPathsFor(SourceFile* file,
                                                     bool forceDtsPaths) {
	// TODO: cache
	auto* adapter = new outputPathsAdapter;
	adapter->paths = outputpaths::GetOutputPathsFor(
	    file, Options(), this, outputpaths::ForceEmitPaths{Dts: forceDtsPaths});
	return adapter;
}

// emitHost.go:38 — GetTypeCheckerForFile maps to the shared lazy checker.
std::pair<std::unique_ptr<emitHost>, std::function<void()>> newEmitHost(
    SimpleProgram* program, SourceFile* file) {
	checker::Checker* checker = program->getChecker();
	auto host = std::make_unique<emitHost>();
	host->program = program;
	host->emitResolver = checker->GetEmitResolver();
	return {std::move(host), [] {}};
}

// --- emitter.go ---

// emitter.go:45 emit
void emitter::emit() {
	emitJSFile(sourceFile, paths->JsFilePath(), paths->SourceMapFilePath());
	emitDeclarationFile(sourceFile, paths->DeclarationFilePath(),
	                    paths->DeclarationMapPath());
	emitResult.Diagnostics = emitterDiagnostics.GetDiagnostics();
}

// emitter.go:60 getDeclarationTransformers
std::vector<transformers::declarations::DeclarationTransformer*>
emitter::getDeclarationTransformers(printer::EmitContext* emitContext,
                                    SourceFile* sourceFile,
                                    const std::string& declarationFilePath,
                                    const std::string& declarationMapPath) {
	bool forceDtsEmit = emitOnly == EmitOnly::EmitOnlyBuilderSignature ||
	                    (forceEmit && emitOnly == EmitOnly::EmitOnlyDts);
	return {
	    transformers::declarations::NewDeclarationTransformer(host, emitContext,
	                                            host->Options(),
	                                            declarationFilePath,
	                                            declarationMapPath),
	    transformers::declarations::NewSupplementalReferencesTransformer(
	        host, sourceFile, declarationFilePath, forceDtsEmit),
	};
}

// emitter.go:72 runScriptTransformers
SourceFile* emitter::runScriptTransformers(printer::EmitContext* emitContext,
                                         SourceFile* sourceFile) {
	for (auto* transformer :
	     getScriptTransformers(emitContext, host, sourceFile)) {
		sourceFile = transformer->transformSourceFile(sourceFile);
	}
	return sourceFile;
}

// emitter.go:83 runDeclarationTransformers
std::pair<SourceFile*, std::vector<Diagnostic*>>
emitter::runDeclarationTransformers(printer::EmitContext* emitContext,
                                    SourceFile* sourceFile,
                                    const std::string& declarationFilePath,
                                    const std::string& declarationMapPath) {
	std::vector<Diagnostic*> diags;
	for (auto* transformer :
	     getDeclarationTransformers(emitContext, sourceFile,
	                                declarationFilePath, declarationMapPath)) {
		sourceFile = transformer->TransformSourceFile(sourceFile);
		auto d = transformer->GetDiagnostics();
		diags.insert(diags.end(), d.begin(), d.end());
	}
	return {sourceFile, diags};
}

// emitter.go:90 getModuleTransformer
Transformer* getModuleTransformer(TransformOptions* opts) {
	switch (opts->CompilerOptions->GetEmitModuleKind()) {
	case ModuleKind::Preserve:
		// `ESModuleTransformer` contains logic for preserving CJS input syntax
		// in `--module preserve`
		return transformers::moduletransforms::NewESModuleTransformer(opts);

	case ModuleKind::ESNext:
	case ModuleKind::ES2022:
	case ModuleKind::ES2020:
	case ModuleKind::ES2015:
	case ModuleKind::Node20:
	case ModuleKind::Node18:
	case ModuleKind::Node16:
	case ModuleKind::NodeNext:
	case ModuleKind::CommonJS:
		return transformers::moduletransforms::NewImpliedModuleTransformer(opts);

	default:
		return transformers::moduletransforms::NewCommonJSModuleTransformer(
		    opts);
	}
}

// emitter.go:107 getScriptTransformers
std::vector<Transformer*> getScriptTransformers(
    printer::EmitContext* emitContext, printer::EmitHost* host,
    SourceFile* sourceFile) {
	std::vector<Transformer*> tx;
	const CompilerOptions* options = host->Options();

	// JS files don't use reference calculations as they don't do import
	// elision, no need to calculate it
	bool importElisionEnabled =
	    options->VerbatimModuleSyntax != Tristate::True &&
	    !isInJSFile(sourceFile->asNode());
	bool jsxTransformEnabled = options->GetJSXTransformEnabled() &&
	                           sourceFile->LanguageVariant ==
	                               LanguageVariant::JSX;

	checker::EmitResolver* emitResolver = host->GetEmitResolver();

	binder::ReferenceResolver* referenceResolver;
	if (importElisionEnabled || jsxTransformEnabled ||
	    !options->GetIsolatedModules() ||
	    options->EmitDecoratorMetadata == Tristate::True) {
		referenceResolver = emitResolver;
	} else {
		referenceResolver =
		    binder::NewReferenceResolver(options,
		                                 binder::ReferenceResolverHooks{});
	}

	TransformOptions opts;
	opts.Context = emitContext;
	opts.CompilerOptions = options;
	opts.Resolver = referenceResolver;
	opts.EmitResolver = emitResolver;
	opts.GetEmitModuleFormatOfFile = [host](SourceFile* f) {
		return host->GetEmitModuleFormatOfFile(f);
	};

	// transform TypeScript syntax
	{
		// use type nodes to add metadata decorators
		if (options->EmitDecoratorMetadata == Tristate::True) {
			tx.push_back(
			    transformers::tstransforms::NewMetadataTransformer(&opts));
		}

		// erase types
		tx.push_back(
		    transformers::tstransforms::NewTypeEraserTransformer(&opts));

		// elide imports
		if (importElisionEnabled) {
			tx.push_back(
			    transformers::tstransforms::NewImportElisionTransformer(&opts));
		}

		// transform `enum`, `namespace`, and parameter properties
		tx.push_back(
		    transformers::tstransforms::NewRuntimeSyntaxTransformer(&opts));

		if (options->ExperimentalDecorators == Tristate::True) {
			tx.push_back(
			    transformers::tstransforms::NewLegacyDecoratorsTransformer(
			        &opts));
		}
	}

	if (jsxTransformEnabled) {
		tx.push_back(transformers::jsxtransforms::NewJSXTransformer(&opts));
	}

	Transformer* downleveler =
	    transformers::estransforms::GetESTransformer(&opts);
	if (downleveler != nullptr) {
		tx.push_back(downleveler);
	}

	tx.push_back(transformers::estransforms::NewUseStrictTransformer(&opts));

	// transform module syntax
	tx.push_back(getModuleTransformer(&opts));

	// inlining (formerly done via substitutions)
	if (!options->GetIsolatedModules()) {
		tx.push_back(
		    transformers::inliners::NewConstEnumInliningTransformer(&opts));
	}
	return tx;
}

// emitter.go:184 emitJSFile
void emitter::emitJSFile(SourceFile* sourceFile, const std::string& jsFilePath,
                         const std::string& sourceMapFilePath) {
	const CompilerOptions* options = host->Options();

	if (sourceFile == nullptr ||
	    (emitOnly != EmitOnly::EmitAll && emitOnly != EmitOnly::EmitOnlyJs) ||
	    jsFilePath.empty()) {
		return;
	}

	if (!forceEmit &&
	    (options->NoEmit == Tristate::True || host->IsEmitBlocked(jsFilePath))) {
		emitResult.EmitSkipped = true;
		return;
	}

	auto [emitContext, putEmitContext] = printer::GetEmitContext();

	sourceFile = runScriptTransformers(emitContext, sourceFile);

	printer::PrinterOptions printerOptions;
	printerOptions.RemoveComments = options->RemoveComments == Tristate::True;
	printerOptions.NewLine = options->NewLine;
	printerOptions.NoEmitHelpers = options->NoEmitHelpers == Tristate::True;
	printerOptions.SourceMap = options->SourceMap == Tristate::True;
	printerOptions.InlineSourceMap = options->InlineSourceMap == Tristate::True;
	printerOptions.InlineSources = options->InlineSources == Tristate::True;
	printerOptions.Target = options->Target;
	// !!!

	// create a printer to print the nodes
	printer::Printer* printer_ = printer::NewPrinter(printerOptions,
	                                               printer::PrintHandlers{},
	                                               emitContext);

	printSourceFile(jsFilePath, sourceMapFilePath, sourceFile, printer_,
	                options,
	                shouldEmitSourceMaps(options, sourceFile));

	putEmitContext();
}

// emitter.go:219 emitDeclarationFile
void emitter::emitDeclarationFile(SourceFile* sourceFile,
                                  const std::string& declarationFilePath,
                                  const std::string& declarationMapPath) {
	const CompilerOptions* options = host->Options();

	if (sourceFile == nullptr || emitOnly == EmitOnly::EmitOnlyJs ||
	    declarationFilePath.empty()) {
		return;
	}
	bool emitDeclarationMap =
	    emitOnly != EmitOnly::EmitOnlyBuilderSignature &&
	    options->DeclarationMap == Tristate::True;
	SourceFile* contentMappedSource = sourceFile;

	auto [emitContext, putEmitContext] = printer::GetEmitContext();
	auto [sf, diags] =
	    runDeclarationTransformers(emitContext, sourceFile,
	                               declarationFilePath, declarationMapPath);
	sourceFile = sf;

	for (auto* elem : diags) {
		// Add declaration transform diagnostics to emit diagnostics
		emitterDiagnostics.Add(elem);
	}

	if (!forceEmit && emitOnly != EmitOnly::EmitOnlyBuilderSignature &&
	    (options->NoEmit == Tristate::True ||
	     host->IsEmitBlocked(declarationFilePath))) {
		emitResult.EmitSkipped = true;
		putEmitContext();
		return;
	}

	bool declBlocked = !diags.empty() && !forceEmit &&
	                   emitOnly != EmitOnly::EmitOnlyBuilderSignature;
	if (declBlocked) {
		emitResult.EmitSkipped = true;
		putEmitContext();
		return;
	}

	printer::PrinterOptions printerOptions;
	printerOptions.RemoveComments = options->RemoveComments == Tristate::True;
	printerOptions.NewLine = options->NewLine;
	printerOptions.NoEmitHelpers = true;
	// Module: 			   options.Module, // NYI
	// ModuleResolution:   options.ModuleResolution, // NYI
	printerOptions.Target = options->GetEmitScriptTarget();
	printerOptions.SourceMap = emitDeclarationMap;
	printerOptions.InlineSourceMap = options->InlineSourceMap == Tristate::True;
	// InlineSources:       options.InlineSources, // ignored, per strada
	// ExtendedDiagnostics: options.ExtendedDiagnostics, // NYI
	printerOptions.OnlyPrintJSDocStyle = true;
	printerOptions.OmitBraceSourceMapPositions = true;

	// create a printer to print the nodes
	printer::PrintHandlers printHandlers;
	auto* spanMap = contentMappedSource->SpanMap();
	if (emitDeclarationMap && spanMap != nullptr) {
		auto* originalSource = newDeclarationMapSource(contentMappedSource);
		printHandlers.MapSourcePosition =
		    [spanMap, originalSource, contentMappedSource](
		        sourcemap::Source* source, TextPos pos) -> printer::MappedSourcePosition {
			if (source->FileName() != contentMappedSource->FileName()) {
				return {source, pos, true};
			}
			auto [mapped, ok] =
			    spanmap::VirtualToOriginalPositionExact(spanMap, TextPos(pos));
			if (!ok) {
				return {nullptr, TextPos(0), false};
			}
			return {originalSource, TextPos(mapped), true};
		};
	}
	printer::Printer* printer_ =
	    printer::NewPrinter(printerOptions, printHandlers, emitContext);

	CompilerOptions declarationMapOptions;
	declarationMapOptions.SourceMap =
	    emitDeclarationMap ? Tristate::True : Tristate::False;
	declarationMapOptions.SourceRoot = options->SourceRoot;
	declarationMapOptions.MapRoot = options->MapRoot;
	// Explicitly do not pass through either inline option.

	printSourceFile(declarationFilePath, declarationMapPath, sourceFile,
	                printer_, &declarationMapOptions,
	                shouldEmitSourceMaps(&declarationMapOptions, sourceFile));

	putEmitContext();
}

// emitter.go:294 printSourceFile
void emitter::printSourceFile(const std::string& jsFilePath,
                              const std::string& sourceMapFilePath,
                              SourceFile* sourceFile,
                              printer::Printer* printer_,
                              const CompilerOptions* mapOptions,
                              bool shouldEmitSourceMaps) {
	// !!! sourceMapGenerator
	const CompilerOptions* options = host->Options();
	sourcemap::Generator* sourceMapGenerator = nullptr;
	if (shouldEmitSourceMaps) {
		sourceMapGenerator = sourcemap::NewGenerator(
		    tspath::getBaseFileName(
		        tspath::normalizeSlashes(jsFilePath)),
		    getSourceRoot(mapOptions),
		    getSourceMapDirectory(mapOptions, jsFilePath, sourceFile),
		    tspath::ComparePathsOptions{
		        useCaseSensitiveFileNames: host->UseCaseSensitiveFileNames(),
		        currentDirectory: host->GetCurrentDirectory(),
		    });
	}

	printer_->Write(sourceFile->asNode(), sourceFile, writer,
	                sourceMapGenerator);

	int sourceMapUrlPos = -1;
	if (sourceMapGenerator != nullptr) {
		if (mapOptions->SourceMap == Tristate::True ||
		    mapOptions->InlineSourceMap == Tristate::True) {
			emitResult.SourceMaps.push_back(SourceMapEmitResult{
			    InputSourceFileNames: sourceMapGenerator->Sources(),
			    SourceMap: sourceMapGenerator->RawSourceMap(),
			    GeneratedFile: jsFilePath,
			});
		}

		std::string sourceMappingURL = getSourceMappingURL(
		    mapOptions, sourceMapGenerator, jsFilePath, sourceMapFilePath,
		    sourceFile);

		if (!sourceMappingURL.empty()) {
			if (!writer->IsAtStartOfLine()) {
				writer->RawWrite(options->NewLine == NewLineKind::CarriageReturnLineFeed
				                     ? "\r\n"
				                     : "\n");
			}
			sourceMapUrlPos = writer->GetTextPos();
			writer->WriteComment("//# sourceMappingURL=");
			writer->WriteComment(sourceMappingURL);
		}

		// Write the source map
		if (!sourceMapFilePath.empty()) {
			std::string sourceMap = sourceMapGenerator->String();
			WriteFileData mapData;
			mapData.SourceFile = this->sourceFile;
			auto err = writeText(sourceMapFilePath, sourceMap, &mapData);
			if (err.has_value()) {
				emitterDiagnostics.Add(tsoptions::newCompilerDiagnostic(
				    Could_not_write_file_0_Colon_1, {jsFilePath, *err}));
			} else {
				emitResult.EmittedFiles.push_back(sourceMapFilePath);
			}
		}
	} else {
		writer->WriteLine();
	}

	// Write the output file
	std::string text = writer->String();
	if (options->EmitBOM == Tristate::True) {
		text = addUTF8ByteOrderMark(text);
	}
	WriteFileData data;
	data.SourceMapUrlPos = sourceMapUrlPos;
	data.Diagnostics = emitterDiagnostics.GetDiagnostics();
	data.SourceFile = this->sourceFile;
	auto err = writeText(jsFilePath, text, &data);
	bool skippedDtsWrite = data.SkippedDtsWrite;
	if (err.has_value()) {
		emitterDiagnostics.Add(tsoptions::newCompilerDiagnostic(
		    Could_not_write_file_0_Colon_1, {jsFilePath, *err}));
	} else if (!skippedDtsWrite) {
		emitResult.EmittedFiles.push_back(jsFilePath);
	}

	// Reset state
	writer->Clear();
}

// emitter.go:392 writeText
std::optional<std::string> emitter::writeText(const std::string& fileName,
                                              const std::string& text,
                                              WriteFileData* data) {
	if (writeFile) {
		return writeFile(fileName, text, data);
	}
	return host->WriteFile(fileName, text);
}

// emitter.go:396 shouldEmitSourceMaps
bool shouldEmitSourceMaps(const CompilerOptions* mapOptions,
                          SourceFile* sourceFile) {
	return (mapOptions->SourceMap == Tristate::True ||
	        mapOptions->InlineSourceMap == Tristate::True) &&
	       !tspath::fileExtensionIs(sourceFile->FileName(),
	                                tspath::extensionJson);
}

// emitter.go:401 getSourceRoot
std::string getSourceRoot(const CompilerOptions* mapOptions) {
	// Normalize source root and make sure it has trailing "/" so that it can
	// be used to combine paths with the relative paths of the sources list in
	// the sourcemap
	std::string sourceRoot = tspath::normalizeSlashes(mapOptions->SourceRoot);
	if (!sourceRoot.empty()) {
		sourceRoot = tspath::ensureTrailingDirectorySeparator(sourceRoot);
	}
	return sourceRoot;
}

// emitter.go:410 getSourceMapDirectory
std::string emitter::getSourceMapDirectory(const CompilerOptions* mapOptions,
                                           const std::string& filePath,
                                           SourceFile* sourceFile) {
	if (!mapOptions->SourceRoot.empty()) {
		return host->CommonSourceDirectory();
	}
	if (!mapOptions->MapRoot.empty()) {
		std::string sourceMapDir = tspath::normalizeSlashes(mapOptions->MapRoot);
		if (sourceFile != nullptr) {
			// For modules or multiple emit files the mapRoot will have
			// directory structure like the sources
			// So if src\a.ts and src\lib\b.ts are compiled together user would
			// be moving the maps into mapRoot\a.js.map and
			// mapRoot\lib\b.js.map
			sourceMapDir = tspath::getDirectoryPath(
			    outputpaths::GetSourceFilePathInNewDir(
			        sourceFile->FileName(), sourceMapDir,
			        host->GetCurrentDirectory(), host->CommonSourceDirectory(),
			        host->UseCaseSensitiveFileNames()));
		}
		if (tspath::getRootLength(sourceMapDir) == 0) {
			// The relative paths are relative to the common directory
			sourceMapDir =
			    tspath::combinePaths(host->CommonSourceDirectory(),
			                         {sourceMapDir});
		}
		return sourceMapDir;
	}
	return tspath::getDirectoryPath(tspath::normalizePath(filePath));
}

// emitter.go:428 getSourceMappingURL
std::string emitter::getSourceMappingURL(const CompilerOptions* mapOptions,
                                         sourcemap::Generator* sourceMapGenerator,
                                         const std::string& filePath,
                                         const std::string& sourceMapFilePath,
                                         SourceFile* sourceFile) {
	if (mapOptions->InlineSourceMap == Tristate::True) {
		// Encode the sourceMap into the sourceMap url
		return sourceMapGenerator->Base64DataURL();
	}

	std::string sourceMapFile(tspath::getBaseFileName(
	    tspath::normalizeSlashes(sourceMapFilePath)));
	if (!mapOptions->MapRoot.empty()) {
		std::string sourceMapDir = tspath::normalizeSlashes(mapOptions->MapRoot);
		if (sourceFile != nullptr) {
			// For modules or multiple emit files the mapRoot will have
			// directory structure like the sources
			// So if src\a.ts and src\lib\b.ts are compiled together user would
			// be moving the maps into mapRoot\a.js.map and
			// mapRoot\lib\b.js.map
			sourceMapDir = tspath::getDirectoryPath(
			    outputpaths::GetSourceFilePathInNewDir(
			        sourceFile->FileName(), sourceMapDir,
			        host->GetCurrentDirectory(), host->CommonSourceDirectory(),
			        host->UseCaseSensitiveFileNames()));
		}
		if (tspath::getRootLength(sourceMapDir) == 0) {
			// The relative paths are relative to the common directory
			sourceMapDir =
			    tspath::combinePaths(host->CommonSourceDirectory(),
			                         {sourceMapDir});
			return encodeURI(
			    tspath::getRelativePathToDirectoryOrUrl(
			        tspath::getDirectoryPath(
			            tspath::normalizePath(filePath)), // get the relative
			                                          // sourceMapDir path
			                                          // based on jsFilePath
			        tspath::combinePaths(
			            sourceMapDir,
			            {sourceMapFile}), // this is where user expects to see
			                          // sourceMap
			        /*isAbsolutePathAnUrl*/ true,
			        tspath::ComparePathsOptions{
			            useCaseSensitiveFileNames:
			                host->UseCaseSensitiveFileNames(),
			            currentDirectory: host->GetCurrentDirectory(),
			        }));
		}
		return encodeURI(
		    tspath::combinePaths(sourceMapDir, {sourceMapFile}));
	}
	return encodeURI(sourceMapFile);
}

// emitter.go:493 sourceFileMayBeEmitted — host is the Go
// SourceFileMayBeEmittedHost interface surface (subset of checker::Program).
bool sourceFileMayBeEmitted(SourceFile* sourceFile, checker::Program* host,
                            bool forceDtsEmit, bool forceJsEmit) {
	// TODO: move this to outputpaths?
	const CompilerOptions* options = host->Options();
	// Js files are emitted only if option is enabled
	if (!forceJsEmit && options->NoEmitForJsFiles == Tristate::True &&
	    isSourceFileJS(sourceFile)) {
		return false;
	}

	// Declaration files are not emitted
	if (sourceFile->IsDeclarationFile) {
		return false;
	}

	// Runtime output for content-mapped files is owned by the external content
	// mapper or build tool. Only include them in the emit set when their
	// transformed TypeScript can produce declarations.
	if (!sourceFile->ContentMapper().empty() && !forceDtsEmit &&
	    !options->GetEmitDeclarations()) {
		return false;
	}

	// Source file from node_modules are not emitted
	if (host->IsSourceFileFromExternalLibrary(sourceFile)) {
		return false;
	}

	// forcing dts emit => file needs to be emitted
	if (forceDtsEmit || forceJsEmit) {
		return true;
	}

	// Check other conditions for file emit
	// Source files from referenced projects are not emitted
	if (host->GetProjectReferenceFromSource(sourceFile->Path()) != nullptr) {
		return false;
	}

	// Any non json file should be emitted
	if (!isJsonSourceFile(sourceFile)) {
		return true;
	}

	// Json file is not emitted if outDir is not specified
	if (options->OutDir.empty()) {
		return false;
	}

	// Otherwise, if rootDir is specified or a config file exists, we know the
	// common source directory and can check if the file would be emitted in
	// the same location
	if (!options->RootDir.empty() || !options->ConfigFilePath.empty()) {
		std::string commonDir = tspath::getNormalizedAbsolutePath(
		    outputpaths::GetCommonSourceDirectory(
		        options, []() { return std::vector<std::string>{}; },
		        host->GetCurrentDirectory(), host->UseCaseSensitiveFileNames(),
		        nullptr),
		    host->GetCurrentDirectory());
		std::string outputPath =
		    outputpaths::GetSourceFilePathInNewDirWorker(
		        sourceFile->FileName(), options->OutDir,
		        host->GetCurrentDirectory(), commonDir,
		        host->UseCaseSensitiveFileNames());
		if (tspath::comparePaths(
		        sourceFile->FileName(), outputPath,
		        tspath::ComparePathsOptions{
		            useCaseSensitiveFileNames:
		                host->UseCaseSensitiveFileNames(),
		            currentDirectory: host->GetCurrentDirectory(),
		        }) == 0) {
			return false;
		}
	}

	return true;
}

// emitter.go:553 getSourceFilesToEmit — nullptr == Go nil (all files).
std::vector<SourceFile*> getSourceFilesToEmit(
    checker::Program* host,
    const std::vector<SourceFile*>* targetSourceFiles, bool forceDtsEmit,
    bool forceJsEmit) {
	const std::vector<SourceFile*>* targets = targetSourceFiles;
	std::vector<SourceFile*> allFiles;
	if (targets == nullptr) {
		allFiles = host->SourceFiles();
		targets = &allFiles;
	}
	std::vector<SourceFile*> result;
	for (auto* sourceFile : *targets) {
		if (sourceFileMayBeEmitted(sourceFile, host, forceDtsEmit,
		                           forceJsEmit)) {
			result.push_back(sourceFile);
		}
	}
	return result;
}

// emitter.go:560 isSourceFileNotJson
static bool isSourceFileNotJson(SourceFile* file) {
	return !isJsonSourceFile(file);
}

// emitter.go:566 getDeclarationDiagnostics
std::vector<Diagnostic*> getDeclarationDiagnostics(emitHost* host,
                                                 SourceFile* file) {
	// TODO: use p.getSourceFilesToEmit cache
	std::vector<SourceFile*> single{file};
	auto full = getSourceFilesToEmit(host, &single, false, false);
	std::vector<SourceFile*> fullFiles;
	for (auto* f : full) {
		if (isSourceFileNotJson(f)) {
			fullFiles.push_back(f);
		}
	}
	bool found = false;
	for (auto* f : fullFiles) {
		if (f == file) {
			found = true;
			break;
		}
	}
	if (!found) {
		return {};
	}
	const CompilerOptions* options = host->Options();
	auto* transform = transformers::declarations::NewDeclarationTransformer(
	    host, nullptr, options, "", "");
	transform->TransformSourceFile(file);
	return transform->GetDiagnostics();
}

} // namespace tsc::compiler
