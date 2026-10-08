// module_resolution_baseline.go — DoModuleResolutionBaseline.
#include <string>

#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/tsbaseline/tsbaseline.h"
#include "internal/testutil/tsbaseline/tsbaselineutil.h"

namespace tsc::testutil::tsbaseline {

// DoModuleResolutionBaseline — module_resolution_baseline.go:9.
void DoModuleResolutionBaseline(gostd::testing::T* t,
                                const std::string& baselinePath,
                                const std::string& trace,
                                const baseline::Options& opts) {
	auto path = tsExtension().ReplaceAllString(baselinePath, ".trace.json");
	std::string errorBaseline;
	if (!trace.empty()) {
		errorBaseline = trace;
	} else {
		errorBaseline = std::string(baseline::NoContent);
	}
	baseline::Run(t, path, errorBaseline, opts);
}

}  // namespace tsc::testutil::tsbaseline
