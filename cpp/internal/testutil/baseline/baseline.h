// Declarations for tsc/internal/testutil/baseline — the Go baseline-runner
// package. Declared for the testrunner slice; the Run body is a
// TSC_UNREACHABLE dep-stub (testrunner_deps.cpp) until the testutil slice
// lands.
#pragma once

#include <functional>
#include <string>

#include "internal/gostd/testing.h"

namespace tsc::testutil::baseline {

// Options — baseline.go:14.
struct Options {
	std::string Subfolder;
	std::function<std::string(std::string)> DiffFixupOld;
	std::function<std::string(std::string)> DiffFixupNew;
	bool SkipDiffWithOld = false;
};

// NoContent — baseline.go:21.
inline constexpr std::string_view NoContent = "<no content>";

// Run — baseline.go:23.
void Run(gostd::testing::T* t, const std::string& fileName,
         const std::string& actual, const Options& opts);

// DiffText — baseline.go:31.
std::string DiffText(const std::string& oldName, const std::string& newName,
                     const std::string& expected, const std::string& actual);

// Track — testmain.go:36.
std::function<void()> Track();

}  // namespace tsc::testutil::baseline
