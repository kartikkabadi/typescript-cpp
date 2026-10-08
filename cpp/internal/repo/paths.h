#pragma once

// repo — paths.go:1-52
//
// Repo-relative path constants, resolved lazily via the equivalent of
// sync.OnceValue.

#include <string>

namespace tsc::repo {

// RootPath returns the repository root — the directory containing go.mod.
// In this C++ port the module root is the directory containing tsc/go.mod
// (see paths.cpp for the layout adaptation).
std::string rootPath();

// TestDataPath returns RootPath()/testdata.
std::string testDataPath();

} // namespace tsc::repo
