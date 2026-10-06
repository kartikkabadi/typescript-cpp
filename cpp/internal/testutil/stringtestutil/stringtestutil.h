// stringtestutil.h — port of tsc/internal/testutil/stringtestutil/
// stringtestutil.go: Dedent removes the common leading indentation from a
// multi-line test literal.
#pragma once

#include <string>
#include <string_view>

namespace tsc::testutil::stringtestutil {

// Dedent — stringtestutil.go:9.
std::string Dedent(std::string_view text);

}  // namespace tsc::testutil::stringtestutil
