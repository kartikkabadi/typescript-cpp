// npmexecutormock_generated.cpp — port of tsc/internal/testutil/
// projecttestutil/npmexecutormock_generated.go.
#include "internal/testutil/projecttestutil/npmexecutormock_generated.h"

#include <mutex>

namespace tsc::testutil::projecttestutil {

// NpmInstall calls NpmInstallFunc — npmexecutormock_generated.go:49.
std::pair<std::string, gostd::Error> NpmExecutorMock::NpmInstall(
    const gostd::Context& ctx, const std::string& cwd,
    const std::vector<std::string>& args) {
	NpmInstallCall callInfo{.Ctx = ctx, .Cwd = cwd, .Args = args};
	{
		std::unique_lock lk(lockNpmInstall);
		calls.NpmInstall.push_back(callInfo);
	}
	if (!NpmInstallFunc) {
		return {{}, nullptr};
	}
	return NpmInstallFunc(ctx, cwd, args);
}

// NpmInstallCalls — npmexecutormock_generated.go:74.
std::vector<NpmExecutorMock::NpmInstallCall>
NpmExecutorMock::NpmInstallCalls() const {
	std::shared_lock lk(lockNpmInstall);
	return calls.NpmInstall;
}

}  // namespace tsc::testutil::projecttestutil
