// util.go — tsbaseline package helpers: shared regexps, the test-path
// replacers (Go strings.NewReplacer semantics), and the file-kind predicates.
#include "internal/testutil/tsbaseline/tsbaselineutil.h"

#include "internal/tspath/tspath.h"

namespace tsc::testutil::tsbaseline {

// util.go:11-15 — package regexps.
const gostd::regexp::Regexp& lineDelimiter() {
	static const gostd::regexp::Regexp* r =
	    new gostd::regexp::Regexp("\\r?\\n");
	return *r;
}
const gostd::regexp::Regexp& nonWhitespace() {
	static const gostd::regexp::Regexp* r =
	    new gostd::regexp::Regexp("\\S");
	return *r;
}
const gostd::regexp::Regexp& tsExtension() {
	static const gostd::regexp::Regexp* r =
	    new gostd::regexp::Regexp("\\.tsx?$");
	return *r;
}
const gostd::regexp::Regexp& testPathCharacters() {
	static const gostd::regexp::Regexp* r =
	    new gostd::regexp::Regexp("[\\^<>:\"|?*%]");
	return *r;
}
const gostd::regexp::Regexp& testPathDotDot() {
	static const gostd::regexp::Regexp* r =
	    new gostd::regexp::Regexp("\\.\\.\\/");
	return *r;
}

// replacerReplace — strings.NewReplacer().Replace. Single left-to-right
// pass: at each position the first old that matches (in pair order) is
// substituted and scanning resumes after it.
std::string replacerReplace(
    const std::vector<std::pair<std::string_view, std::string_view>>& pairs,
    std::string_view s) {
	std::string out;
	out.reserve(s.size());
	size_t pos = 0;
	while (pos < s.size()) {
		bool matched = false;
		for (const auto& [old, new_] : pairs) {
			if (old.empty()) continue;
			if (s.size() - pos >= old.size() &&
			    s.substr(pos, old.size()) == old) {
				out += new_;
				pos += old.size();
				matched = true;
				break;
			}
		}
		if (!matched) {
			out += s[pos];
			pos += 1;
		}
	}
	return out;
}

namespace {
const std::vector<std::pair<std::string_view, std::string_view>>&
testPathPrefixPairs() {
	static const std::vector<std::pair<std::string_view, std::string_view>>
	    v = {{"/.ts/", ""},
	         {"/.lib/", ""},
	         {"/.src/", ""},
	         {"bundled:///libs/", ""},
	         {"file:///./ts/", "file:///"},
	         {"file:///./lib/", "file:///"},
	         {"file:///./src/", "file:///"}};
	return v;
}
const std::vector<std::pair<std::string_view, std::string_view>>&
testPathTrailingPairs() {
	static const std::vector<std::pair<std::string_view, std::string_view>>
	    v = {{"/.ts/", "/"},
	         {"/.lib/", "/"},
	         {"/.src/", "/"},
	         {"bundled:///libs/", "/"},
	         {"file:///./ts/", "file:///"},
	         {"file:///./lib/", "file:///"},
	         {"file:///./src/", "file:///"}};
	return v;
}
}  // namespace

std::string testPathPrefixReplace(std::string_view s) {
	return replacerReplace(testPathPrefixPairs(), s);
}
std::string testPathTrailingReplace(std::string_view s) {
	return replacerReplace(testPathTrailingPairs(), s);
}

// removeTestPathPrefixes — util.go:42.
std::string removeTestPathPrefixes(std::string_view text,
                                   bool retainTrailingDirectorySeparator) {
	if (retainTrailingDirectorySeparator) {
		return testPathTrailingReplace(text);
	}
	return testPathPrefixReplace(text);
}

// isDefaultLibraryFile — util.go:51.
bool isDefaultLibraryFile(std::string_view filePath) {
	auto fileName = tspath::getBaseFileName(filePath);
	return fileName.size() >= 4 && fileName.substr(0, 4) == "lib." &&
	       tspath::fileExtensionIs(fileName, tspath::extensionDts);
}

// isBuiltFile — util.go:56.
bool isBuiltFile(std::string_view filePath) {
	return filePath.substr(0, libFolder.size()) == libFolder ||
	       filePath.substr(
	           0,
	           tspath::ensureTrailingDirectorySeparator(builtFolder).size()) ==
	           tspath::ensureTrailingDirectorySeparator(builtFolder);
}

// isTsConfigFile — util.go:60.
bool isTsConfigFile(std::string_view path) {
	return path.find("tsconfig") != std::string_view::npos &&
	       path.find("json") != std::string_view::npos;
}

// sanitizeTestFilePath — util.go:65.
std::string sanitizeTestFilePath(std::string_view name) {
	std::string path =
	    testPathCharacters().ReplaceAllString(std::string(name), "_");
	path = tspath::normalizeSlashes(path);
	path = testPathDotDot().ReplaceAllString(path, "__dotdot/");
	path = tspath::toPath(path, "", false /*useCaseSensitiveFileNames*/);
	if (!path.empty() && path.front() == '/') {
		path.erase(0, 1);
	}
	return path;
}

// fileOutput — js_emit_baseline.go:133.
std::string fileOutput(const harnessutil::TestFile* file,
                       harnessutil::HarnessOptions* settings) {
	std::string fileName;
	if (settings->FullEmitPaths) {
		fileName = removeTestPathPrefixes(
		    file->UnitName, false /*retainTrailingDirectorySeparator*/);
	} else {
		fileName = std::string(tspath::getBaseFileName(file->UnitName));
	}
	return "//// [" + fileName + "]\r\n" + file->Content;
}

// utf8.RuneCountInString.
size_t runeCountInString(std::string_view s) {
	size_t n = 0;
	for (size_t i = 0; i < s.size();) {
		auto b = (unsigned char)s[i];
		if (b < 0x80) {
			i += 1;
		} else if ((b & 0xE0) == 0xC0) {
			i += 2;
		} else if ((b & 0xF0) == 0xE0) {
			i += 3;
		} else {
			i += 4;
		}
		n += 1;
	}
	return n;
}

}  // namespace tsc::testutil::tsbaseline
