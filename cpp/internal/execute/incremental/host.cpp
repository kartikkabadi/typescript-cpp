// Port of tsc/internal/execute/incremental/host.go — Host interface: the
// compiler host's FS plus file mtime get/set. vfs.FS is the CompilerHost
// itself in this port; mtime goes through std::filesystem (atime is
// ignored like Go's Chtimes(fileName, time.Time{}, mTime)).
#include "internal/execute/incremental/incremental.h"

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
		std::error_code ec;
		std::filesystem::last_write_time(host_->bundledPath(fileName),
		                                 mTime, ec);
		if (ec) {
			return ec.message();
		}
		return std::nullopt;
	}
};

// host.go:46 CreateHost.
Host* CreateHost(compiler::CompilerHost* compilerHost) {
	return new host(compilerHost);
}

// host.go:58 GetMTime — Stat().ModTime(); zero time when stat fails.
std::filesystem::file_time_type GetMTime(compiler::CompilerHost* host,
                                         const std::string& fileName) {
	std::error_code ec;
	auto mTime =
	    std::filesystem::last_write_time(host->bundledPath(fileName), ec);
	if (ec) {
		return std::filesystem::file_time_type{};
	}
	return mTime;
}

}  // namespace tsc::execute::incremental
