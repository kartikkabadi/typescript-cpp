// compilerhost.go — compilerHost methods.
//
// === slice: project ===

#include "internal/project/compilerhost.h"

#include "internal/project/projectcollectionbuilder.h"

#include "internal/binder/binder.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/locale/locale.h"
#include "internal/project/parsecache.h"
#include "internal/project/projectcollectionbuilder.h"
#include "internal/xxh3/xxh3.h"

namespace tsc::project {

// newCompilerHost — compilerhost.go:31.
compilerHost* newCompilerHost(const std::string& currentDirectory,
                              Project* project,
                              ProjectCollectionBuilder* builder,
                              logging::LogTree* logger) {
	auto* c = new compilerHost();
	c->configFilePath = project->configFilePath;
	c->currentDirectory = currentDirectory;
	c->sessionOptions = builder->sessionOptions;

	c->sourceFS = project::newSourceFS(true, builder->fs,
	                                   builder->toPath);
	// CompilerHost.fs is the shared_ptr used by the ParseConfigHost
	// plumbing in the base class; alias it to the sourceFS without
	// ownership.
	c->fs = std::shared_ptr<vfs::FS>(c->sourceFS,
	                                 [](vfs::FS*) {});
	c->defaultLibraryPath =
	    builder->sessionOptions->DefaultLibraryPath;

	c->project = project;
	c->builder = builder;
	c->logger = logger;
	return c;
}

// GetSourceFile — compilerhost.go:104. Files are cached in parseCache
// and acquired immediately for the in-progress program.
SourceFile* compilerHost::GetSourceFile(
    const SourceFileParseOptions& opts) {
	ensureAlive();
	if (auto* fh = sourceFS->GetFileByPath(opts.FileName,
	                                       opts.Path);
	    fh != nullptr) {
		auto key = newParseCacheKey(opts, fh->Hash(),
		                            fh->Kind());
		return builder->parseCache->Acquire(key, fh);
	}
	return nullptr;
}

// GetContentMappedSourceFiles — compilerhost.go:112. Implements
// compiler.CompilerHost.
std::pair<contentmapper::SourceFiles, gostd::Error>
compilerHost::GetContentMappedSourceFiles(
    const SourceFileParseOptions& parseOptions,
    contentmapper::Mapper* mapper) {
	ensureAlive();
	auto* fh = sourceFS->GetFileByPath(parseOptions.FileName,
	                                   parseOptions.Path);
	if (fh == nullptr) {
		return {contentmapper::SourceFiles{}, gostd::Error{}};
	}
	auto diagnosticLocale = locale::fromContext(builder->ctx);
	auto project = ContentMapperProject();
	if (project == nullptr) {
		return {contentmapper::SourceFiles{},
		        contentmapper::ErrProjectUnavailable};
	}
	auto identityRes = project->Identity(mapper);
	auto& identity = identityRes.first;
	auto& identityErr = identityRes.second;
	if (identityErr != nullptr) {
		return {contentmapper::SourceFiles{},
		        gostd::Error(contentmapper::NewTransformError(
		            contentmapper::TransformErrorKindProject,
		            identityErr))};
	}
	auto transformIdentity = xxh3::hash128(identity);
	auto key = contentMappedParseCacheKey(parseOptions, fh->Hash(),
	                                    transformIdentity,
	                                    diagnosticLocale);
	auto res = builder->contentMappedParseCache->AcquireOrError(
	    key, [&]() -> std::pair<contentmapper::SourceFiles,
	                            gostd::Error> {
		    auto filesRes = contentmapper::TransformAndParse(
		        parseOptions, fh->Content(), mapper,
		        this->contentMapperProject_);
		    auto& files = filesRes.first;
		    auto& transformErr = filesRes.second;
		    if (transformErr != nullptr) {
			    return {contentmapper::SourceFiles{},
			            transformErr};
		    }
		    files.Canonical->Hash = {.lo = key.hash.Lo, .hi = key.hash.Hi};
		    bindSourceFile(files.Canonical);
		    for (auto* supplemental : files.Supplemental) {
			    supplemental->Hash = {.lo = key.hash.Lo, .hi = key.hash.Hi};
			    bindSourceFile(supplemental);
		    }
		    return {files, gostd::Error{}};
	    });
	auto& files = res.first;
	auto err = res.second;
	if (err == nullptr) {
		err = contentmapper::CheckSupplementalFileNameCollisions(
		    files, [&](std::string_view p) {
			    return sourceFS->FileExists(std::string{p});
		    });
		if (err != nullptr) {
			builder->contentMappedParseCache->Deref(key);
			return {contentmapper::SourceFiles{}, err};
		}
	}
	return {files, err};
}

// ContentMapperProject — compilerhost.go:141.
contentmapper::Project* compilerHost::ContentMapperProject() const {
	auto* self = const_cast<compilerHost*>(this);
	std::call_once(self->contentMapperOnce, [&] {
		if (builder == nullptr ||
		    builder->contentMapperHost == nullptr) {
			return;
		}
		auto* commandLine =
		    project->getCommandLineWithTypingsFiles();
		if (commandLine->ContentMappers().empty()) {
			return;
		}
		contentmapper::ProjectSpec spec;
		spec.ConfigFileName = commandLine->ConfigName();
		spec.Mappers = commandLine->ContentMappers();
		spec.CompilerOptions = commandLine->CompilerOptions();
		contentMapperProject_ =
		    builder->contentMapperHost->Project(spec);
	});
	return contentMapperProject_.get();
}

// Trace — compilerhost.go:167. Implements compiler.CompilerHost.
void compilerHost::Trace(const DiagnosticMessage* msg,
                         const std::vector<std::string>& args) {
	logging::log(logger, ::tsc::localize(locale::Default, msg, "", args));
}

// GetResolvedProjectReference — compilerhost.go:95. Implements
// compiler.CompilerHost.
tsoptions::ParsedCommandLine* compilerHost::GetResolvedProjectReference(
    const std::string& fileName, const tspath::Path& path) {
	if (builder == nullptr) {
		return configFileRegistry->GetConfig(path);
	}
	// acquireConfigForProject will bypass sourceFS, so track the file
	// here.
	sourceFS->Track(fileName);
	return builder->configFileRegistryBuilder_->acquireConfigForProject(
	    fileName, path, project, logger);
}

} // namespace tsc::project
