// crossproject.go — the ls.Project interface: the narrow view of a
// project that the language service sees (Id, GetProgram, HasFile).
#pragma once

#include <string>

#include "internal/compiler/program.h"

namespace tsc::ls {

// Project — crossproject.go:17.
struct Project {
	virtual ~Project() = default;
	virtual std::string Id() const = 0;
	virtual compiler::SimpleProgram* GetProgram() const = 0;
	virtual bool HasFile(const std::string& fileName) const = 0;
};

} // namespace tsc::ls
