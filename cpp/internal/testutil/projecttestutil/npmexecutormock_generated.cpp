// npmexecutormock_generated.cpp — port of tsc/internal/testutil/
// projecttestutil/npmexecutormock_generated.go.
#include "internal/testutil/projecttestutil/npmexecutormock_generated.h"

#include <mutex>

namespace tsc::testutil::projecttestutil {

// NpmInstall calls NpmInstallFunc — npmexecutormock_generated.go:49.
std::pair<std::vector<uint8_t>, gostd::Error> NpmExecutorMock::NpmInstall(
    const std::string& cwd, const std::vector<std::string>& args) {
	NpmInstallCall callInfo{.Cwd = cwd, .Args = args};
	{
		std::unique_lock lk(lockNpmInstall);
		calls.NpmInstall.push_back(callInfo);
	}
	if (!NpmInstallFunc) {
		return {{}, nullptr};
	}
	return NpmInstallFunc(cwd, args);
}

// NpmInstallCalls — npmexecutormock_generated.go:74.
std::vector<NpmExecutorMock::NpmInstallCall>
NpmExecutorMock::NpmInstallCalls() const {
	std::shared_lock lk(lockNpmInstall);
	return calls.NpmInstall;
}

}  // namespace tsc::testutil::projecttestutil
