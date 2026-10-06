// project.go — Project constructors + methods.
//
// project.cpp
#include "internal/project/project.h"

#include "internal/compiler/program.h"
#include "internal/core/utilities.h"
#include "internal/debug/debug.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/module/resolver.h"
#include "internal/project/compilerhost.h"
#include "internal/project/logging/logging.h"
#include "internal/project/parsecache.h"
#include "internal/project/projectcollectionbuilder.h"
#include "internal/project/snapshot.h"

namespace tsc::project {

// === slice: project ===

// NewConfiguredProject — project.go:180.
Project* NewConfiguredProject(const std::string& configFileName,
                              const tspath::Path& configFilePath,
                              ProjectCollectionBuilder* builder,
                              logging::LogTree* logger) {
	auto configuredProjectID =
	    ParseConfiguredProjectID(configFilePath);
	if (!configuredProjectID.second) {
		TSC_UNREACHABLE("invalid configured project ID");
	}
	Project* project =
	    NewProject(configuredProjectID.first.AsID(), KindConfigured,
	               tspath::getDirectoryPath(configFileName), builder,
	               logger);
	project->configFileName = configFileName;
	project->configFilePath = configFilePath;
	return project;
}

// NewInferredProject — project.go:196.
Project* NewInferredProject(
    const std::string& currentDirectory, CompilerOptions* compilerOptions,
    const std::vector<std::string>& rootFileNames,
    const std::vector<tsc::ProjectReference*>& projectReferences,
    const std::vector<contentmapper::Mapper*>& contentMappers,
    ProjectCollectionBuilder* builder, logging::LogTree* logger) {
	Project* p = NewProject(inferredProjectID.AsID(), KindInferred,
	                        currentDirectory, builder, logger);
	if (compilerOptions == nullptr) {
		compilerOptions = new CompilerOptions{
		    .AllowJs = Tristate::True,
		    .Module = ModuleKind::ESNext,
		    .ModuleResolution = ModuleResolutionKind::Bundler,
		    .Target = ScriptTarget::LatestStandard,
		    .Jsx = JsxEmit::ReactJSX,
		    .AllowImportingTsExtensions = Tristate::True,
		    .StrictNullChecks = Tristate::True,
		    .StrictFunctionTypes = Tristate::True,
		    .SourceMap = Tristate::True,
		    .AllowNonTsExtensions = Tristate::True,
		    .ResolveJsonModule = Tristate::True,
		};
	}
	p->CommandLine = newInferredProjectCommandLine(
	    compilerOptions, rootFileNames, projectReferences, contentMappers,
	    tspath::ComparePathsOptions{
	        .currentDirectory = currentDirectory,
	        .useCaseSensitiveFileNames =
	            builder->fs->fs->UseCaseSensitiveFileNames(),
	    });
	return p;
}

// newSyntheticProject — project.go:234.
Project* newSyntheticProject(
    SyntheticProjectID id, const std::string& currentDirectory,
    CompilerOptions* compilerOptions,
    const std::vector<std::string>& rootFileNames,
    const std::vector<tsc::ProjectReference*>& projectReferences,
    const std::vector<contentmapper::Mapper*>& contentMappers,
    ProjectCollectionBuilder* builder, logging::LogTree* logger) {
	Project* project =
	    NewProject(id.AsID(), KindSynthetic, currentDirectory, builder,
	               logger);
	project->CommandLine = newInferredProjectCommandLine(
	    compilerOptions, rootFileNames, projectReferences, contentMappers,
	    tspath::ComparePathsOptions{
	        .currentDirectory = currentDirectory,
	        .useCaseSensitiveFileNames =
	            builder->fs->fs->UseCaseSensitiveFileNames(),
	    });
	return project;
}

// newInferredProjectCommandLine — project.go:258.
tsoptions::ParsedCommandLine* newInferredProjectCommandLine(
    CompilerOptions* compilerOptions,
    const std::vector<std::string>& rootFileNames,
    const std::vector<tsc::ProjectReference*>& projectReferences,
    const std::vector<contentmapper::Mapper*>& contentMappers,
    const tspath::ComparePathsOptions& comparePathsOptions) {
	tsoptions::ParsedCommandLine* commandLine =
	    tsoptions::NewParsedCommandLine(compilerOptions, rootFileNames,
	                                    projectReferences,
	                                    comparePathsOptions);
	commandLine->ParsedConfig->ContentMappers = contentMappers;
	return commandLine;
}

// NewProject — project.go:270.
Project* NewProject(ID id, Kind kind,
                    const std::string& currentDirectory,
                    ProjectCollectionBuilder* builder,
                    logging::LogTree* logger) {
	if (logger != nullptr) {
		logger->Logf(
		    "Creating %sProject: %s, currentDirectory: %s",
		    {kindString(kind), std::string(id), currentDirectory});
	}
	Project* project = new Project{};
	project->Kind = kind;
	project->id = id;
	project->currentDirectory = currentDirectory;
	project->dirty = true;

	project->programFilesWatch = newWatchedFiles(
	    "program files for " + std::string(id),
	    lsp::lsproto::WatchKindCreate |
	        lsp::lsproto::WatchKindChange |
	        lsp::lsproto::WatchKindDelete,
	    lsp::lsproto::getClientCapabilities(builder->ctx)
	        ->Workspace.DidChangeWatchedFiles.RelativePatternSupport,
	    createResolutionLookupGlobMapper(
	        builder->sessionOptions->CurrentDirectory,
	        builder->sessionOptions->DefaultLibraryPath,
	        project->currentDirectory,
	        builder->fs->fs->UseCaseSensitiveFileNames()));
	if (!builder->sessionOptions->TypingsLocation.empty()) {
		project->typingsWatch = newWatchedFiles(
		    "typings installer files",
		    lsp::lsproto::WatchKindCreate |
		        lsp::lsproto::WatchKindChange |
		        lsp::lsproto::WatchKindDelete,
		    lsp::lsproto::getClientCapabilities(builder->ctx)
		        ->Workspace.DidChangeWatchedFiles.RelativePatternSupport,
		    std::function<PatternsAndIgnored(PatternsAndIgnored)>(
		        Identity<PatternsAndIgnored>));
	}
	project->contentMapperWatch = newWatchedFilesForPaths(
	    "content mapper configuration files for " + std::string(id),
	    lsp::lsproto::WatchKindCreate |
	        lsp::lsproto::WatchKindChange |
	        lsp::lsproto::WatchKindDelete,
	    lsp::lsproto::getClientCapabilities(builder->ctx)
	        ->Workspace.DidChangeWatchedFiles.RelativePatternSupport,
	    builder->sessionOptions->CurrentDirectory,
	    builder->sessionOptions->CurrentDirectory,
	    builder->fs->fs->UseCaseSensitiveFileNames());
	return project;
}

// Project.DisplayName — project.go:320.
std::string Project::DisplayName(const std::string& cwd) const {
	if (Kind == KindInferred) {
		return std::string{tspath::getBaseFileName(currentDirectory)};
	}
	std::string name{ID()};
	if (Kind == KindConfigured) {
		name = ConfigFileName();
	}
	return tspath::convertToRelativePath(
	    name, tspath::ComparePathsOptions{.currentDirectory = cwd});
}

// Project.ConfigFileName — project.go:338. Panics if not configured.
const std::string& Project::ConfigFileName() const {
	if (Kind != KindConfigured) {
		TSC_UNREACHABLE(
		    "ConfigFileName called on non-configured project");
	}
	return configFileName;
}

// Project.ConfigFilePath — project.go:346. Panics if not configured.
const tspath::Path& Project::ConfigFilePath() const {
	if (Kind != KindConfigured) {
		TSC_UNREACHABLE(
		    "ConfigFilePath called on non-configured project");
	}
	return configFilePath;
}

// Project.GetProjectDiagnostics — project.go:368.
std::vector<Diagnostic*> Project::GetProjectDiagnostics() {
	std::vector<Diagnostic*> globalDiags;
	if (checkerPool != nullptr) {
		globalDiags = checkerPool->GetGlobalDiagnostics();
	}
	std::vector<Diagnostic*> all = Program->GetConfigFileParsingDiagnostics();
	for (Diagnostic* d : Program->GetProgramDiagnostics()) {
		all.push_back(d);
	}
	for (Diagnostic* d : globalDiags) {
		all.push_back(d);
	}
	return compiler::sortAndDeduplicateDiagnostics(all);
}

// Project.Clone — project.go:392.
Project* Project::Clone() const {
	Project* clone = new Project{};
	clone->Kind = Kind;
	clone->id = id;
	clone->currentDirectory = currentDirectory;
	clone->configFileName = configFileName;
	clone->configFilePath = configFilePath;

	clone->dirty = dirty;
	clone->dirtyFilePath = dirtyFilePath;

	clone->host = host;
	clone->CommandLine = CommandLine;
	clone->commandLineWithTypingsFiles = commandLineWithTypingsFiles;
	clone->Program = Program;
	clone->ProgramUpdateKind = ProgramUpdateKindNone;
	clone->ProgramLastUpdate = ProgramLastUpdate;
	clone->potentialProjectReferences = potentialProjectReferences;

	clone->programFilesWatch = programFilesWatch;
	clone->typingsWatch = typingsWatch;
	clone->contentMapperWatch = contentMapperWatch;
	clone->contentMapperWatchedFiles = contentMapperWatchedFiles;

	clone->checkerPool = checkerPool;

	clone->moduleResolverFactory = moduleResolverFactory;
	clone->moduleResolverID = moduleResolverID;

	clone->installedTypingsInfo = installedTypingsInfo;
	clone->typingsFiles = typingsFiles;
	return clone;
}

// Project.SetCommandLine — project.go:433.
void Project::SetCommandLine(
    tsoptions::ParsedCommandLine* commandLine) {
	CommandLine = commandLine;
	commandLineWithTypingsFiles = nullptr;
	new (&commandLineWithTypingsFilesOnce) std::once_flag();
	potentialProjectReferences = nullptr;
	dirty = true;
	dirtyFilePath = tspath::Path();
}

// Project.getCommandLineWithTypingsFiles — project.go:443.
tsoptions::ParsedCommandLine*
Project::getCommandLineWithTypingsFiles() {
	if (typingsFiles.empty()) {
		return CommandLine;
	}

	// Check if ATA is enabled for this project
	::tsc::TypeAcquisition* typeAcquisition = GetTypeAcquisition();
	if (typeAcquisition == nullptr || typeAcquisition->Enable != Tristate::True) {
		return CommandLine;
	}

	std::call_once(commandLineWithTypingsFilesOnce, [&] {
		if (commandLineWithTypingsFiles == nullptr) {
			// Create an augmented command line that includes typing
			// files
			const std::vector<std::string> originalRootNames =
			    CommandLine->FileNames();
			std::vector<std::string> newRootNames;
			newRootNames.reserve(originalRootNames.size() +
			                     typingsFiles.size());
			newRootNames.insert(newRootNames.end(),
			                    originalRootNames.begin(),
			                    originalRootNames.end());
			newRootNames.insert(newRootNames.end(),
			                    typingsFiles.begin(),
			                    typingsFiles.end());
			commandLineWithTypingsFiles =
			    CommandLine->WithFileNames(newRootNames);
		}
	});
	return commandLineWithTypingsFiles;
}

// Project.setPotentialProjectReference — project.go:468.
void Project::setPotentialProjectReference(
    const tspath::Path& configFilePath) {
	if (potentialProjectReferences == nullptr) {
		potentialProjectReferences =
		    new collections::Set<tspath::Path>{};
	} else {
		potentialProjectReferences =
		    new collections::Set<tspath::Path>(
		        potentialProjectReferences->Clone());
	}
	potentialProjectReferences->Add(configFilePath);
}

// Project.hasPotentialProjectReference — project.go:477.
bool Project::hasPotentialProjectReference(
    const ProjectTreeRequest* projectTreeRequest) const {
	if (CommandLine != nullptr) {
		for (const std::string& path :
		     CommandLine->ResolvedProjectReferencePaths()) {
			if (projectTreeRequest->IsProjectReferenced(
			        toPath(path))) {
				return true;
			}
		}
	} else if (potentialProjectReferences != nullptr) {
		for (const tspath::Path& path :
		     potentialProjectReferences->Keys()) {
			if (projectTreeRequest->IsProjectReferenced(path)) {
				return true;
			}
		}
	}
	return false;
}

// Project.CreateProgram — project.go:499.
Project::CreateProgramResult Project::CreateProgram() {
	project::ProgramUpdateKind updateKind = ProgramUpdateKindNewFiles;
	bool programCloned = false;
	compiler::SimpleProgram* newProgram = nullptr;

	// Define a fresh CreateCheckerPool closure for this call. Each
	// invocation of CreateProgram must use its own closure so that
	// concurrent goroutines cloning the same project never share a
	// captured variable through a stale closure stored in the old
	// program's options.
	auto createCheckerPool =
	    [this](compiler::SimpleProgram* program) -> compiler::CheckerPool* {
		return newCheckerPool(
		    host->sessionOptions->CheckerPoolOptions, program,
		    [this](const std::string& msg) { log(msg); });
	};
	std::function<void()> cleanupModuleResolver;
	auto createModuleResolver =
	    [this, &cleanupModuleResolver](
	        const module::ResolverOptions& options) -> module::Resolver* {
		if (moduleResolverFactory == nullptr) {
			return module::NewResolver(options);
		}
		auto res = moduleResolverFactory->NewResolver(options);
		cleanupModuleResolver = std::move(res.second);
		return res.first;
	};
	// defer cleanupModuleResolver() — Go runs it on function exit.
	struct cleanupGuard {
		std::function<void()>& f;
		~cleanupGuard() {
			if (f) f();
		}
	} guard{cleanupModuleResolver};

	// Create the command line, potentially augmented with typing files
	tsoptions::ParsedCommandLine* commandLine =
	    getCommandLineWithTypingsFiles();
	SourceFile* dirtyFile = nullptr;
	if (!dirtyFilePath.empty() && Program != nullptr &&
	    Program->CommandLine() == commandLine) {
		auto upd = Program->UpdateProgram(dirtyFilePath, host,
		                                  createCheckerPool,
		                                  createModuleResolver);
		newProgram = std::get<0>(upd);
		dirtyFile = std::get<1>(upd);
		programCloned = std::get<2>(upd);
		if (programCloned) {
			updateKind = ProgramUpdateKindCloned;
			for (SourceFile* file : newProgram->SourceFiles()) {
				// Use pointer identity: dirtyFile is the exact
				// instance UpdateProgram acquired, and it is the only
				// file whose refcount is already accounted for.
				if (file != dirtyFile &&
				    !file->IsContentMapperFailureStub() &&
				    !file->IsContentMapperSupplemental()) {
					// UpdateProgram acquired the changed file only,
					// so we need to ref everything else
					if (!file->ContentMapper().empty()) {
						host->builder->contentMappedParseCache->Ref(
						    contentMappedParseCacheKeyForFile(file));
					} else {
						host->builder->parseCache->Ref(
						    parseCacheKeyForFile(file));
					}
				}
			}
			for (compiler::DuplicateSourceFile* file :
			     newProgram->DuplicateSourceFiles()) {
				if (!file->IsContentMapperFailureStub) {
					if (!file->ContentMapper.empty()) {
						host->builder->contentMappedParseCache->Ref(
						    contentMappedParseCacheKeyForDuplicate(
						        file));
					} else {
						host->builder->parseCache->Ref(
						    parseCacheKeyForDuplicate(file));
					}
				}
			}
		} else if (dirtyFile != nullptr) {
			// UpdateProgram always acquires the dirty file before
			// deciding whether it can reuse the old program. If it
			// falls back to a full rebuild, release that
			// speculative acquire so the rebuilt program is the
			// only remaining owner.
			if (!dirtyFile->ContentMapper().empty()) {
				host->builder->contentMappedParseCache->Deref(
				    contentMappedParseCacheKeyForFile(dirtyFile));
			} else {
				host->builder->parseCache->Deref(
				    parseCacheKeyForFile(dirtyFile));
			}
		}
	} else {
		std::string typingsLocation;
		if (GetTypeAcquisition()->Enable == Tristate::True) {
			typingsLocation = host->sessionOptions->TypingsLocation;
		}
		compiler::ProgramOptions opts;
		opts.Host = host;
		opts.Config = commandLine;
		opts.UseSourceOfProjectReference = true;
		opts.TypingsLocation = std::move(typingsLocation);
		opts.CreateCheckerPool = createCheckerPool;
		opts.CreateModuleResolver = createModuleResolver;
		newProgram = compiler::NewProgram(opts);
	}

	if (!programCloned && Program != nullptr &&
	    Program->HasSameFileNames(newProgram)) {
		updateKind = ProgramUpdateKindSameFileNames;
	}

	newProgram->BindSourceFiles();

	return CreateProgramResult{.Program = newProgram,
	                           .UpdateKind = updateKind};
}

// Project.CloneWatchers — project.go:593.
WatchedFiles<collections::SyncSet<tspath::Path>*>*
Project::CloneWatchers() {
	return programFilesWatch->Clone(host->sourceFS->seenFiles);
}

// Project.toPath — project.go:601.
tspath::Path Project::toPath(const std::string& fileName) const {
	return tspath::toPath(fileName, currentDirectory,
	                      host->FS()->UseCaseSensitiveFileNames());
}

// Project.print — project.go:605.
std::string Project::print(bool writeFileNames,
                           bool writeFileExplanation) {
	std::string builder;
	builder += "\nProject '" + std::string(id) + "'\n";
	if (Program == nullptr) {
		builder += "\tFiles (0) NoProgram\n";
	} else {
		std::vector<SourceFile*> sourceFiles = Program->GetSourceFiles();
		builder += "\tFiles (" + std::to_string(sourceFiles.size()) +
		           ")\n";
		if (writeFileNames) {
			for (SourceFile* sourceFile : sourceFiles) {
				builder += "\t\t";
				builder += sourceFile->FileName();
				builder += "\n";
			}
			// !!!
			// if writeFileExplanation {}
		}
	}
	builder += hr;
	return builder;
}

// Project.GetTypeAcquisition — project.go:628.
::tsc::TypeAcquisition* Project::GetTypeAcquisition() const {
	if (Kind == KindInferred || Kind == KindSynthetic) {
		// For inferred and synthetic projects, use default settings.
		return new ::tsc::TypeAcquisition{
		    .Enable = Tristate::True,
		    .Include = {},
		    .Exclude = {},
		    .DisableFilenameBasedTypeAcquisition = Tristate::False,
		};
	}
	if (CommandLine != nullptr) {
		return CommandLine->TypeAcquisition();
	}
	return nullptr;
}

// Project.GetUnresolvedImports — project.go:647.
collections::Set<std::string>* Project::GetUnresolvedImports() {
	if (Program == nullptr) {
		return nullptr;
	}
	return Program->GetUnresolvedImports();
}

// Project.ShouldTriggerATA — project.go:656.
bool Project::ShouldTriggerATA(uint64_t snapshotID) {
	if (Program == nullptr || CommandLine == nullptr) {
		return false;
	}

	::tsc::TypeAcquisition* typeAcquisition = GetTypeAcquisition();
	if (typeAcquisition == nullptr || typeAcquisition->Enable != Tristate::True) {
		return false;
	}

	if (installedTypingsInfo == nullptr ||
	    (ProgramLastUpdate == snapshotID &&
	     ProgramUpdateKind == ProgramUpdateKindNewFiles)) {
		return true;
	}

	return !installedTypingsInfo->Equals(ComputeTypingsInfo());
}

// Project.ComputeTypingsInfo — project.go:675.
ata::TypingsInfo Project::ComputeTypingsInfo() {
	return ata::TypingsInfo{
	    .CompilerOptions = CommandLine->CompilerOptions(),
	    .TypeAcquisition = GetTypeAcquisition(),
	    .UnresolvedImports = GetUnresolvedImports(),
	};
}

// SyntheticProjectID.UnmarshalJSONFrom — project.go:84.
gostd::Error
SyntheticProjectID::UnmarshalJSONFrom(json::Decoder* dec) {
	std::string value;
	if (std::string err = json::unmarshalDecode(*dec, &value);
	    !err.empty()) {
		return gostd::newError(err);
	}
	auto parsed = ParseSyntheticProjectID(value);
	if (!parsed.second) {
		return gostd::newError("invalid synthetic project ID: " + value);
	}
	*this = parsed.first;
	return {};
}

// === end slice: project ===
} // namespace tsc::project
