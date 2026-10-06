// npmexecutormock_generated.h — port of tsc/internal/testutil/
// projecttestutil/npmexecutormock_generated.go (moq-generated
// NpmExecutorMock). Implements ata::NpmExecutor.
#pragma once

#include <functional>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/project/ata/ata.h"

namespace tsc::testutil::projecttestutil {

// NpmExecutorMock — npmexecutormock_generated.go:31. A mock implementation
// of ata.NpmExecutor.
//
//	mockedNpmExecutor := &NpmExecutorMock{
//		NpmInstallFunc: func(cwd string, args []string) ([]byte, error) {
//			panic("mock out the NpmInstall method")
//		},
//	}
struct NpmExecutorMock final : ata::NpmExecutor {
	// NpmInstallFunc mocks the NpmInstall method.
	std::function<std::pair<std::vector<uint8_t>, gostd::Error>(
	    const std::string&, const std::vector<std::string>&)>
	    NpmInstallFunc;

	// calls tracks calls to the methods.
	struct NpmInstallCall {
		// Cwd is the cwd argument value.
		std::string Cwd;
		// Args is the args argument value.
		std::vector<std::string> Args;
	};
	struct Calls {
		std::vector<NpmInstallCall> NpmInstall;
	};
	Calls calls;

	mutable std::shared_mutex lockNpmInstall;

	// NpmInstall calls NpmInstallFunc — npmexecutormock_generated.go:49.
	std::pair<std::vector<uint8_t>, gostd::Error>
	NpmInstall(const std::string& cwd,
	           const std::vector<std::string>& args) override;

	// NpmInstallCalls gets all the calls that were made to NpmInstall —
	// npmexecutormock_generated.go:74.
	std::vector<NpmInstallCall> NpmInstallCalls() const;
};

}  // namespace tsc::testutil::projecttestutil
