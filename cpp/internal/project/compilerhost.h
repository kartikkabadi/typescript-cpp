// compilerhost.go — compilerHost: the project's compiler.CompilerHost
// over sourceFS.
//
// compilerhost.h
#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "internal/compiler/program.h"
#include "internal/project/configfileregistry.h"
#include "internal/project/snapshotfs.h"

namespace tsc::project {

struct SessionOptions;

struct Project;
struct ProjectCollectionBuilder;

// compilerHost — compilerhost.go:19.
struct compilerHost : compiler::CompilerHost {
	tspath::Path configFilePath;
	SessionOptions* sessionOptions = nullptr;

	sourceFS* sourceFS = nullptr;
	ConfigFileRegistry* configFileRegistry = nullptr;

	Project* project = nullptr;
	ProjectCollectionBuilder* builder = nullptr;
	logging::LogTree* logger = nullptr;
	mutable std::shared_ptr<contentmapper::Project>
	    contentMapperProject_;
	mutable std::once_flag contentMapperOnce;

	// freeze — compilerhost.go:54. Clears references to mutable state
	// to make the compilerHost safe for use after the snapshot has
	// been finalized. See the usage in snapshot.go for more details.
	void freeze(SnapshotFS* snapshotFS,
	            ConfigFileRegistry* registry) {
		if (builder == nullptr) {
			TSC_UNREACHABLE(
			    "freeze can only be called once");
		}
		sourceFS->source = snapshotFS;
		sourceFS->DisableTracking();
		configFileRegistry = registry;
		builder = nullptr;
		project = nullptr;
		logger = nullptr;
	}

	// ensureAlive — compilerhost.go:64.
	void ensureAlive() {
		if (builder == nullptr || project == nullptr) {
			TSC_UNREACHABLE(
			    "method must not be called after snapshot "
			    "initialization");
		}
	}

	// FS — compilerhost.go:80. Implements compiler.CompilerHost.
	vfs::FS* FS() { return sourceFS; }

	// GetResolvedProjectReference — compilerhost.go:95. Implements
	// compiler.CompilerHost.
	tsoptions::ParsedCommandLine* GetResolvedProjectReference(
	    const std::string& fileName,
	    const tspath::Path& path) override;

	// GetSourceFile — compilerhost.go:104. Implements
	// compiler.CompilerHost. Files are cached in parseCache and
	// acquired immediately for the in-progress program.
	SourceFile* GetSourceFile(
	    const SourceFileParseOptions& opts) override;

	// GetContentMappedSourceFiles — compilerhost.go:112. Implements
	// compiler.CompilerHost.
	std::pair<contentmapper::SourceFiles, gostd::Error>
	GetContentMappedSourceFiles(
	    const SourceFileParseOptions& parseOptions,
	    contentmapper::Mapper* mapper) override;

	// ContentMapperProject — compilerhost.go:141.
	contentmapper::Project* ContentMapperProject() const override;

	// Trace — compilerhost.go:167. Implements compiler.CompilerHost.
	void Trace(const DiagnosticMessage* msg,
	           const std::vector<std::string>& args);
};

// newCompilerHost — compilerhost.go:31.
compilerHost* newCompilerHost(const std::string& currentDirectory,
                              Project* project,
                              ProjectCollectionBuilder* builder,
                              logging::LogTree* logger);

} // namespace tsc::project
