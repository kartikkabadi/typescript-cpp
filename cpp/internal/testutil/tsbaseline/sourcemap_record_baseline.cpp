// sourcemap_record_baseline.go — DoSourcemapRecordBaseline.
#include <string>

#include "internal/core/types.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/harnessutil/harnessutil.h"
#include "internal/testutil/tsbaseline/tsbaseline.h"
#include "internal/testutil/tsbaseline/tsbaselineutil.h"
#include "internal/tspath/tspath.h"

namespace tsc::testutil::tsbaseline {

// DoSourcemapRecordBaseline — sourcemap_record_baseline.go:12.
void DoSourcemapRecordBaseline(
    gostd::testing::T* t, const std::string& baselinePathIn,
    const std::string& header, CompilerOptions* options,
    harnessutil::CompilationResult* result,
    harnessutil::HarnessOptions* harnessSettings,
    const baseline::Options& opts) {
	auto baselinePath = baselinePathIn;
	std::string actual{baseline::NoContent};
	if (options->SourceMap == Tristate::True ||
	    options->InlineSourceMap == Tristate::True ||
	    options->DeclarationMap == Tristate::True) {
		auto record = removeTestPathPrefixes(
		    result->GetSourceMapRecord(),
		    false /*retainTrailingDirectorySeparator*/);
		if (!(options->NoEmitOnError == Tristate::True &&
		      !result->Diagnostics.empty()) &&
		    !record.empty()) {
			actual = record;
		}
	}

	if (tspath::fileExtensionIsOneOf(
	        baselinePath, {tspath::extensionTs, tspath::extensionTsx})) {
		baselinePath =
		    tspath::changeExtension(baselinePath, ".sourcemap.txt");
	}

	baseline::Run(t, baselinePath, actual, opts);
}

}  // namespace tsc::testutil::tsbaseline
