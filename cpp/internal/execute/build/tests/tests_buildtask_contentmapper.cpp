// tests_buildtask_contentmapper.cpp — port of
// tsc/internal/execute/build/buildtask_contentmapper_test.go.
#include <vector>

#include "internal/execute/build/build.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

namespace tsc {
namespace {
using namespace ::tsc::execute::build;


using gostd::testing::T;

// slicesValues — slices.Values(roots): an iter.Seq over the slice.
inline pathSeq slicesValues(const std::vector<tspath::Path>& roots) {
	return [&roots](const std::function<bool(const tspath::Path&)>& yield) {
		for (auto& root : roots) {
			if (!yield(root)) {
				return;
			}
		}
	};
}

void TestIsContentMapperSupplementalBuildInfoPath(T* t) {
	t->Parallel();
	std::vector<tspath::Path> roots{"/src/app.vue", "/src/index.ts"};

	if (!isContentMapperSupplementalBuildInfoPath(
	        "/src/app.vue.0.ts", slicesValues(roots))) {
		t->Error({"/src/app.vue.0.ts should be supplemental"});
	}
	if (!isContentMapperSupplementalBuildInfoPath(
	        "/src/app.vue.12.mts", slicesValues(roots))) {
		t->Error({"/src/app.vue.12.mts should be supplemental"});
	}
	if (isContentMapperSupplementalBuildInfoPath(
	        "/src/app.vue.ts", slicesValues(roots))) {
		t->Error({"/src/app.vue.ts should not be supplemental"});
	}
	if (isContentMapperSupplementalBuildInfoPath(
	        "/src/app.vue.0.txt", slicesValues(roots))) {
		t->Error({"/src/app.vue.0.txt should not be supplemental"});
	}
	if (isContentMapperSupplementalBuildInfoPath(
	        "/src/other.vue.0.ts", slicesValues(roots))) {
		t->Error({"/src/other.vue.0.ts should not be supplemental"});
	}
}
REGISTER_UNIT_TEST("build.TestIsContentMapperSupplementalBuildInfoPath",
                   TestIsContentMapperSupplementalBuildInfoPath);

} // namespace
} // namespace tsc
