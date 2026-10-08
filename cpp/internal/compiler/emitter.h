#pragma once
// emitter.go + emitHost.go — the emit driver. emitHost adapts SimpleProgram
// to the printer/declarations host surfaces; emitter runs the per-file
// transform → print → write pipeline.

#include "internal/ast/diagnostics_util.h"
#include "internal/compiler/program.h"
#include "internal/outputpaths/outputpaths.h"
#include "internal/printer/emitcontext.h"
#include "internal/printer/printer.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/transformers/declarations/declarations.h"
#include "internal/transformers/transformers.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tsc::compiler {

// emitHost.go:33 — NOTE: EmitHost operations must be thread-safe.
// Satisfies Go's EmitHost interface (printer.EmitHost +
// declarations.DeclarationEmitHost + Options/SourceFiles/
// UseCaseSensitiveFileNames/GetCurrentDirectory/CommonSourceDirectory/
// IsEmitBlocked).
struct emitHost : transformers::declarations::DeclarationEmitHost, printer::EmitHost {
	SimpleProgram* program{};
	std::function<checker::EmitResolver*(printer::EmitContext*)> newEmitResolver;

	// --- emitHost.go delegations ---
	const CompilerOptions* Options() override {
		return program->Options();
	}
	std::vector<SourceFile*> SourceFiles() override {
		return program->SourceFiles();
	}
	std::string GetCurrentDirectory() override {
		return program->GetCurrentDirectory();
	}
	std::string CommonSourceDirectory() override {
		return program->CommonSourceDirectory();
	}
	std::vector<std::string> ContentMapperExtensions() override {
		return program->ContentMapperExtensions();
	}
	bool UseCaseSensitiveFileNames() override {
		return program->UseCaseSensitiveFileNames();
	}
	bool IsEmitBlocked(std::string_view file) override {
		return program->IsEmitBlocked(std::string(file));
	}
	std::optional<std::string> WriteFile(std::string_view fileName,
	                                     std::string_view text) override {
		return program->Host()->WriteFile(fileName, text);
	}
	printer::EmitResolver* NewEmitResolver(
	    printer::EmitContext* emitContext) override {
		return newEmitResolver(emitContext);
	}
	bool IsSourceFileFromExternalLibrary(SourceFile* file) override {
		return program->IsSourceFileFromExternalLibrary(file);
	}
	// const twin — satisfies checker::Program's const pure (EmitHost uses the
	// non-const overload above).
	bool IsSourceFileFromExternalLibrary(SourceFile* file) const override {
		return program->IsSourceFileFromExternalLibrary(file);
	}
	symlinks::KnownSymlinks* GetSymlinkCache() override {
		return program->GetSymlinkCache();
	}
	std::string GetGlobalTypingsCacheLocation() override {
		return program->GetGlobalTypingsCacheLocation();
	}
	std::string GetNearestAncestorDirectoryWithPackageJson(
	    const std::string& dirname) override {
		return program->GetNearestAncestorDirectoryWithPackageJson(dirname);
	}
	std::shared_ptr<packagejson::InfoCacheEntry> GetPackageJsonInfo(
	    const std::string& pkgJsonPath) override {
		return program->GetPackageJsonInfo(pkgJsonPath);
	}
	std::string GetSourceOfProjectReferenceIfOutputIncluded(
	    SourceFile* file) override {
		return program->GetSourceOfProjectReferenceIfOutputIncluded(file);
	}
	checker::SourceOutputAndProjectReference* GetProjectReferenceFromSource(
	    const tspath::Path& path) override {
		return program->GetProjectReferenceFromSource(path);
	}
	std::vector<std::string> GetRedirectTargets(
	    const tspath::Path& path) override {
		return program->GetRedirectTargets(path);
	}
	module::ResolvedModule* GetResolvedModuleFromModuleSpecifier(
	    SourceFile* file, Node* moduleSpecifier) override {
		return program->GetResolvedModuleFromModuleSpecifier(file,
		                                                     moduleSpecifier);
	}
	ResolutionMode GetModeForUsageLocation(SourceFile* file,
	                                       Node* moduleSpecifier) override {
		return program->GetModeForUsageLocation(file, moduleSpecifier);
	}
	ResolutionMode GetDefaultResolutionModeForFile(SourceFile* file) override {
		return program->GetDefaultResolutionModeForFile(file);
	}
	ModuleKind GetEmitModuleFormatOfFile(SourceFile* file) override {
		return program->GetEmitModuleFormatOfFile(file);
	}
	bool FileExists(const std::string& path) override {
		return program->FileExists(path);
	}
	transformers::declarations::OutputPaths* GetOutputPathsFor(SourceFile* file,
	                                             bool forceDtsPaths) override;
	bool SourceFileMayBeEmitted(SourceFile* file,
	                            bool forceDtsEmit) override {
		return sourceFileMayBeEmitted(file, this, forceDtsEmit, false);
	}
	SourceFile* GetSourceFileFromReference(SourceFile* origin,
	                                       FileReference* ref) override {
		return program->GetSourceFileFromReference(origin, ref);
	}

	// emitHost.go:139 ResolveModuleName — resolver passthrough (not a host
	// interface member).
	module::ResolvedModule* ResolveModuleName(const std::string& moduleName,
	                                          const std::string& containingFile,
	                                          ResolutionMode resolutionMode) {
		auto [resolved, diags] = program->resolver_->ResolveModuleName(
		    moduleName, containingFile, resolutionMode, nullptr);
		return resolved.get();
	}

	// --- checker::Program members Go's emitHost doesn't carry; required
	// because DeclarationEmitHost is a checker::Program in C++. Delegated to
	// the wrapped program. ---
	void BindSourceFiles() override { program->BindSourceFiles(); }
	SourceFile* GetSourceFile(const std::string& fileName) override {
		return program->GetSourceFile(fileName);
	}
	SourceFile* GetSourceFileForResolvedModule(
	    const std::string& fileName) override {
		return program->GetSourceFileForResolvedModule(fileName);
	}
	ResolutionMode GetEmitSyntaxForUsageLocation(
	    SourceFile* file, Node* usageLocation) override {
		return program->GetEmitSyntaxForUsageLocation(file, usageLocation);
	}
	ModuleKind GetImpliedNodeFormatForEmit(SourceFile* file) override {
		return program->GetImpliedNodeFormatForEmit(file);
	}
	std::optional<checker::ResolvedModule> GetResolvedModule(
	    SourceFile* file, const std::string& moduleReference,
	    ResolutionMode mode) override {
		return program->GetResolvedModule(file, moduleReference, mode);
	}
	std::vector<checker::ResolvedModule> GetResolvedModules() override {
		return program->GetResolvedModules();
	}
	const std::unordered_map<std::string, bool>& GetPackagesMap() override {
		return program->GetPackagesMap();
	}
	const SourceFileMetaData& GetSourceFileMetaData(
	    const std::string& path) const override {
		return program->GetSourceFileMetaData(path);
	}
	bool IsSourceFileDefaultLibrary(const std::string& path) override {
		return program->IsSourceFileDefaultLibrary(path);
	}
};

// emitHost.go:38 newEmitHost — the `done` func releases Go's per-file
// checker; this program shares one checker so done is a no-op.
std::pair<std::unique_ptr<emitHost>, std::function<void()>> newEmitHost(
    SimpleProgram* program, SourceFile* file);

// emitter.go:32 — NOTE: emitHost.go's file says the emitter is
// not-thread-safe (single-threaded programs run emit inline anyway).
struct emitter {
	emitHost* host{};
	EmitOnly emitOnly = EmitOnly::EmitAll;
	// emitter.go:33 — Go ast.DiagnosticsCollection; Add() dedupes identical
	// diagnostics (e.g. private-in-base reports fired by repeated
	// serialization passes).
	DiagnosticsCollection emitterDiagnostics;
	printer::EmitTextWriter* writer{};
	std::optional<outputpaths::OutputPaths> paths;
	SourceFile* sourceFile{};
	tracing::Tracing* tr{}; // emitter.go:36
	EmitResult emitResult;
	bool forceEmit = false;
	WriteFile writeFile;

	void emit();
	std::vector<transformers::declarations::DeclarationTransformer*>
	getDeclarationTransformers(checker::EmitResolver* emitResolver,
	                           SourceFile* sourceFile,
	                           const std::string& declarationFilePath,
	                           const std::string& declarationMapPath);
	SourceFile* runScriptTransformers(checker::EmitResolver* emitResolver,
	                                  SourceFile* sourceFile);
	std::pair<SourceFile*, std::vector<Diagnostic*>>
	runDeclarationTransformers(checker::EmitResolver* emitResolver,
	                           SourceFile* sourceFile,
	                           const std::string& declarationFilePath,
	                           const std::string& declarationMapPath);
	void emitJSFile(checker::EmitResolver* emitResolver,
	                SourceFile* sourceFile, const std::string& jsFilePath,
	                const std::string& sourceMapFilePath);
	void emitDeclarationFile(checker::EmitResolver* emitResolver,
	                         SourceFile* sourceFile,
	                         const std::string& declarationFilePath,
	                         const std::string& declarationMapPath);
	void printSourceFile(printer::EmitContext* emitContext,
	                     const std::string& jsFilePath,
	                     const std::string& sourceMapFilePath,
	                     SourceFile* sourceFile, printer::Printer* printer_,
	                     const CompilerOptions* mapOptions,
	                     bool shouldEmitSourceMaps);
	std::optional<std::string> writeText(const std::string& fileName,
	                                     const std::string& text,
	                                     WriteFileData* data);
	std::string getSourceMapDirectory(const CompilerOptions* mapOptions,
	                                  const std::string& filePath,
	                                  SourceFile* sourceFile);
	std::string getSourceMappingURL(const CompilerOptions* mapOptions,
	                                sourcemap::Generator* sourceMapGenerator,
	                                const std::string& filePath,
	                                const std::string& sourceMapFilePath,
	                                SourceFile* sourceFile);
};

// emitter.go:90 getModuleTransformer
transformers::Transformer* getModuleTransformer(
    transformers::TransformOptions* opts);

// emitter.go:107 getScriptTransformers
std::vector<transformers::Transformer*> getScriptTransformers(
    checker::EmitResolver* emitResolver, printer::EmitHost* host,
    SourceFile* sourceFile);

// emitter.go:396 shouldEmitSourceMaps
bool shouldEmitSourceMaps(const CompilerOptions* mapOptions,
                          SourceFile* sourceFile);

// emitter.go:401 getSourceRoot
std::string getSourceRoot(const CompilerOptions* mapOptions);

// emitter.go:566 getDeclarationDiagnostics
std::vector<Diagnostic*> getDeclarationDiagnostics(emitHost* host,
                                                 SourceFile* file);

} // namespace tsc::compiler
