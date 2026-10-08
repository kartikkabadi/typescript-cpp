// fixtures.h — port of tsc/internal/testutil/fixtures/benchfixtures.go: the
// shared benchmark fixture list.
#pragma once

#include <memory>
#include <vector>

#include "internal/testutil/filefixture/filefixture.h"

namespace tsc::testutil::fixtures {

// BenchFixtures — benchfixtures.go:9.
extern const std::vector<std::shared_ptr<filefixture::Fixture>> BenchFixtures;

}  // namespace tsc::testutil::fixtures
