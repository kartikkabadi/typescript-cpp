// jstest.h — port of tsc/internal/testutil/jstest/node.go: run a Node.js
// ESM script that default-exports a function and unmarshal its JSON result.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"

namespace tsc::testutil::jstest {

// loaderScript — node.go:16.
inline constexpr std::string_view loaderScript =
    "import script from \"./script.mjs\";\n"
    "process.stdout.write(JSON.stringify(await script("
    "...process.argv.slice(2))));";

// SkipIfNoNodeJS — node.go:53.
void SkipIfNoNodeJS(gostd::testing::T* t);

// evalNodeScript — node.go:60.
std::pair<std::string, gostd::Error> evalNodeScript(
    gostd::testing::T* t, std::string_view script, std::string_view loader,
    const std::string& dir, const std::vector<std::string>& args);

// EvalNodeScript — node.go:31. Imports a Node.js script that
// default-exports a single function, calls it with the provided arguments,
// and unmarshals the JSON-stringified awaited return value into T.
template <class T>
std::pair<T, gostd::Error> EvalNodeScript(gostd::testing::T* t,
                                          const std::string& script,
                                          const std::string& dir,
                                          const std::vector<std::string>& args);

// EvalNodeScriptWithTS — node.go:36. Like EvalNodeScript, but provides the
// TypeScript library to the script as the first argument.
template <class T>
std::pair<T, gostd::Error> EvalNodeScriptWithTS(
    gostd::testing::T* t, const std::string& script, std::string dir,
    const std::vector<std::string>& args);

}  // namespace tsc::testutil::jstest

#include "internal/testutil/jstest/jstest_inl.h"
