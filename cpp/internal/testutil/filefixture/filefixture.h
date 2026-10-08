// filefixture.h — port of tsc/internal/testutil/filefixture/filefixture.go:
// named test fixtures backed by a real file on disk or an in-memory string.
#pragma once

#include <memory>
#include <string>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"

namespace tsc::testutil::filefixture {

// Fixture — filefixture.go:9.
struct Fixture {
	virtual ~Fixture() = default;
	virtual std::string Name() = 0;
	virtual std::string Path() = 0;
	virtual void SkipIfNotExist(gostd::testing::T* t) = 0;
	virtual std::string ReadFile(gostd::testing::T* t) = 0;
};

// FromFile (filefixture.go:22) — fixture reading `path` lazily; the file
// contents and read error are cached like Go's sync.OnceValues.
std::shared_ptr<Fixture> FromFile(std::string name, std::string path);

// FromString (filefixture.go:61).
std::shared_ptr<Fixture> FromString(std::string name, std::string path,
                                    std::string contents);

}  // namespace tsc::testutil::filefixture
