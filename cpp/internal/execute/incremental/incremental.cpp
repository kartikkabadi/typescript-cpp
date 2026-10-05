// Port of tsc/internal/execute/incremental/incremental.go — the
// BuildInfoReader (.tsbuildinfo reader) and ReadBuildInfoProgram, which
// validates a read buildInfo and turns it into an incremental Program
// snapshot.
#include "internal/execute/incremental/incremental.h"

namespace tsc::execute::incremental {

// incremental.go:21 buildInfoReader.
class buildInfoReader : public BuildInfoReader {
	compiler::CompilerHost* host_;

public:
	buildInfoReader(compiler::CompilerHost* host) : host_(host) {}

	// incremental.go:25 ReadBuildInfo — json.Unmarshal of the .tsbuildinfo
	// file; nullptr on any failure (Go nil / err != nil).
	BuildInfo* ReadBuildInfo(
	    tsoptions::ParsedCommandLine* config) override {
		auto buildInfoFileName = config->GetBuildInfoFileName();
		if (buildInfoFileName.empty()) {
			return nullptr;
		}

		// Read build info file
		auto data = host_->ReadFile(buildInfoFileName);
		if (!data.has_value()) {
			return nullptr;
		}
		return unmarshalBuildInfo(*data);
	}
};

// incremental.go:33 NewBuildInfoReader.
BuildInfoReader* NewBuildInfoReader(compiler::CompilerHost* host) {
	return new buildInfoReader(host);
}

// incremental.go:39 ReadBuildInfoProgram.
Program* ReadBuildInfoProgram(tsoptions::ParsedCommandLine* config,
                              BuildInfoReader* reader,
                              compiler::CompilerHost* host) {
	// Read buildInfo file
	auto* buildInfo = reader->ReadBuildInfo(config);
	if (buildInfo == nullptr || !buildInfo->IsValidVersion() ||
	    !buildInfo->IsIncremental()) {
		return nullptr;
	}
	// If any configured content mapper's identity has changed, files it
	// produced may be stale, so the old program cannot be reused.
	auto [contentMapperIdentities, err] =
	    ContentMapperIdentities(host->ContentMapperProject());
	if (err.has_value() ||
	    !buildInfo->ContentMapperIdentitiesMatch(contentMapperIdentities)) {
		return nullptr;
	}

	// Convert to information that can be used to create incremental
	// program — Go `&Program{snapshot: ...}` (program/host unset).
	auto* incrementalProgram = new Program();
	incrementalProgram->snapshot_ =
	    buildInfoToSnapshot(buildInfo, config, host);
	return incrementalProgram;
}

}  // namespace tsc::execute::incremental
