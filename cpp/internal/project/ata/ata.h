// ata.h — dep-decl for the testutil-leaves slice: project/ata's NpmExecutor
// interface (ata.go). The interface is a pure contract and is declared
// faithfully; ata is owned by the project slice.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "internal/gostd/gostd.h"

namespace tsc::ata {

// NpmExecutor — ata.go:43.
struct NpmExecutor {
	virtual ~NpmExecutor() = default;
	virtual std::pair<std::vector<uint8_t>, gostd::Error>
	NpmInstall(const std::string& cwd,
	           const std::vector<std::string>& args) = 0;
};

}  // namespace tsc::ata
