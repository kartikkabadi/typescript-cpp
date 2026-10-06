// fixtures.cpp — port of tsc/internal/testutil/fixtures/benchfixtures.go.
#include "internal/testutil/fixtures/fixtures.h"

#include "internal/repo/paths.h"

namespace tsc::testutil::fixtures {

// BenchFixtures — benchfixtures.go:9 (filepath.Join resolved at init).
const std::vector<std::shared_ptr<filefixture::Fixture>> BenchFixtures = {
	filefixture::FromString("empty.ts", "empty.ts", ""),
	filefixture::FromFile("checker.ts",
	                      repo::testDataPath() +
	                          "/fixtures/compiler/checker.ts"),
	filefixture::FromFile("dom.generated.d.ts",
	                      repo::testDataPath() +
	                          "/fixtures/lib/dom.generated.d.ts"),
	filefixture::FromFile("Herebyfile.mjs",
	                      repo::testDataPath() +
	                          "/fixtures/typescript/Herebyfile.mjs"),
	filefixture::FromFile(
	    "jsxComplexSignatureHasApplicabilityError.tsx",
	    repo::testDataPath() +
	        "/fixtures/testcases/compiler/"
	        "jsxComplexSignatureHasApplicabilityError.tsx"),
};

}  // namespace tsc::testutil::fixtures
