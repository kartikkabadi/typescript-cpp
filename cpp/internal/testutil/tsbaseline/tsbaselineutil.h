// Internal declarations for tsbaseline package-private helpers —
// util.go's regexps, replacers, and file-kind predicates are shared by all
// the *_baseline.cpp files.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/gostd/regexp.h"
#include "internal/testutil/harnessutil/harnessutil.h"

namespace tsc::testutil::tsbaseline {

// util.go:11-15 — package regexps (compiled once).
const gostd::regexp::Regexp& lineDelimiter();      // "\r?\n"
const gostd::regexp::Regexp& nonWhitespace();      // `\S`
const gostd::regexp::Regexp& tsExtension();        // `\.tsx?$`
const gostd::regexp::Regexp& testPathCharacters(); // `[\^<>:"|?*%]`
const gostd::regexp::Regexp& testPathDotDot();     // `\.\.\/`

// util.go:17-19 — package vars.
inline constexpr std::string_view libFolder = "built/local/";
inline constexpr std::string_view builtFolder = "/.ts";

// strings.NewReplacer — Go semantics: left-to-right single pass, first
// matching pair at each position wins (no re-replacement of inserted text).
std::string replacerReplace(
    const std::vector<std::pair<std::string_view, std::string_view>>& pairs,
    std::string_view s);

// testPathPrefixReplacer / testPathTrailingReplacerTrailingSeparator —
// util.go:24-40.
std::string testPathPrefixReplace(std::string_view s);
std::string testPathTrailingReplace(std::string_view s);

// removeTestPathPrefixes — util.go:42.
std::string removeTestPathPrefixes(std::string_view text,
                                   bool retainTrailingDirectorySeparator);

// isDefaultLibraryFile — util.go:51.
bool isDefaultLibraryFile(std::string_view filePath);
// isBuiltFile — util.go:56.
bool isBuiltFile(std::string_view filePath);
// isTsConfigFile — util.go:60.
bool isTsConfigFile(std::string_view path);
// sanitizeTestFilePath — util.go:65.
std::string sanitizeTestFilePath(std::string_view name);

// utf8.RuneCountInString — counts UTF-8 code points.
size_t runeCountInString(std::string_view s);

// fileOutput — js_emit_baseline.go:133 (shared by the emit baseliners).
std::string fileOutput(const harnessutil::TestFile* file,
                       harnessutil::HarnessOptions* settings);

}  // namespace tsc::testutil::tsbaseline
