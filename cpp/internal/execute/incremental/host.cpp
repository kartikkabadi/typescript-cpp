// Port of tsc/internal/execute/incremental/host.go — Host interface: the
// compiler host's FS plus file mtime get/set. vfs.FS access goes through
// CompilerHost::fs; mtime uses FS().Stat()/Chtimes exactly like Go
// (atime is ignored like Go's Chtimes(fileName, time.Time{}, mTime)).
#include "internal/execute/incremental/incremental.h"
#include "internal/fileclock/fileclock.h"

namespace tsc::execute::incremental {

// host.go:21 host.
class host : public Host {
	compiler::CompilerHost* host_;

public:
	host(compiler::CompilerHost* compilerHost) : host_(compilerHost) {}

	compiler::CompilerHost* FS() override { return host_; }

	std::filesystem::file_time_type GetMTime(
	    const std::string& fileName) override {
		return incremental::GetMTime(host_, fileName);
	}

	std::optional<std::string> SetMTime(
	    const std::string& fileName,
	    std::filesystem::file_time_type mTime) override {
		if (auto err = host_->fs->Chtimes(
		        fileName, vfs::TimePoint{std::chrono::seconds{-62135596800}},
		        fileClockToSys(mTime))) {
			return err.str();
		}
		return std::nullopt;
	}
};

// host.go:46 CreateHost.
Host* CreateHost(compiler::CompilerHost* compilerHost) {
	return new host(compilerHost);
}

// host.go:58 GetMTime — FS().Stat().ModTime(); zero time when stat fails.
// Go's zero time is the minimum representable instant, so the "no time"
// sentinel is min(): `file_time_type{}` is the file_clock epoch (2174 on
// libstdc++), which sorts *after* every real converted sys mtime.
std::filesystem::file_time_type GetMTime(compiler::CompilerHost* host,
                                         const std::string& fileName) {
	if (auto stat = host->fs->Stat(fileName)) {
		return fileClockFromSys(stat->ModTime());
	}
	return std::filesystem::file_time_type::min();
}

}  // namespace tsc::execute::incremental
