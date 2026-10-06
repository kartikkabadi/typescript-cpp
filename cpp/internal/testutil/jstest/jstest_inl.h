// jstest_inl.h — template bodies for jstest.h (node.go EvalNodeScript /
// EvalNodeScriptWithTS, which are generic over the JSON result type).
#pragma once

#include "internal/json/json.h"
#include "internal/repo/paths.h"
#include "internal/tspath/tspath.h"

namespace tsc::testutil::jstest {

// EvalNodeScript — node.go:31.
template <class T>
std::pair<T, gostd::Error> EvalNodeScript(
    gostd::testing::T* t, const std::string& script, const std::string& dir,
    const std::vector<std::string>& args) {
	auto [output, err] =
	    evalNodeScript(t, script, loaderScript, dir, args);
	T result{};
	if (err != nullptr) {
		return {result, err};
	}
	std::string uerr = json::unmarshal(output, &result);
	if (!uerr.empty()) {
		return {result,
		        gostd::errorf("failed to unmarshal JSON output: %w",
		                      {gostd::newError(uerr)})};
	}
	return {result, nullptr};
}

// EvalNodeScriptWithTS — node.go:36.
template <class T>
std::pair<T, gostd::Error> EvalNodeScriptWithTS(
    gostd::testing::T* t, const std::string& script, std::string dir,
    const std::vector<std::string>& args) {
	if (dir.empty()) {
		dir = t->TempDir();
	}
	std::string tsSrc = tspath::normalizePath(
	    repo::rootPath() + "/../node_modules/typescript/lib/typescript.js");
	if (tsSrc[0] == '/') {
		tsSrc = "file://" + tsSrc;
	} else {
		tsSrc = "file:///" + tsSrc;
	}
	std::string tsLoaderScript = gostd::sprintf(
	    "import script from \"./script.mjs\";\n"
	    "import * as ts from \"%s\";\n"
	    "process.stdout.write(JSON.stringify(await script(ts, "
	    "...process.argv.slice(2))));",
	    {tsSrc});
	auto [output, err] =
	    evalNodeScript(t, script, tsLoaderScript, dir, args);
	T result{};
	if (err != nullptr) {
		return {result, err};
	}
	std::string uerr = json::unmarshal(output, &result);
	if (!uerr.empty()) {
		return {result,
		        gostd::errorf("failed to unmarshal JSON output: %w",
		                      {gostd::newError(uerr)})};
	}
	return {result, nullptr};
}

}  // namespace tsc::testutil::jstest
