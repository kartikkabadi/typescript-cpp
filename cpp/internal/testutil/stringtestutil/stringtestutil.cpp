// stringtestutil.cpp — port of tsc/internal/testutil/stringtestutil/
// stringtestutil.go.
#include "internal/testutil/stringtestutil/stringtestutil.h"

#include <vector>

#include "internal/ast/ast.h" // TSC_UNREACHABLE
#include "internal/stringutil/stringutil.h"

namespace tsc::testutil::stringtestutil {

namespace {

// strings.IndexFunc — byte index of the first rune where the predicate
// holds, or -1.
int indexFunc(std::string_view s,
              const std::function<bool(char32_t)>& fn) {
	for (size_t i = 0; i < s.size();) {
		int width = 0;
		char32_t r = tsc::decodeUtf8Rune(s.substr(i), &width);
		if (fn(r)) {
			return (int)i;
		}
		i += width;
	}
	return -1;
}

// strings.ReplaceAll for a literal needle.
std::string replaceAll(std::string_view s, std::string_view old,
                       std::string_view repl) {
	std::string out;
	size_t start = 0;
	for (;;) {
		size_t pos = s.find(old, start);
		if (pos == std::string_view::npos) {
			out += s.substr(start);
			return out;
		}
		out += s.substr(start, pos - start);
		out += repl;
		start = pos + old.size();
	}
}

}  // namespace

// Dedent — stringtestutil.go:9.
std::string Dedent(std::string_view text) {
	// strings.Split(text, "\n")
	std::vector<std::string> lines;
	for (size_t start = 0;;) {
		size_t nl = text.find('\n', start);
		if (nl == std::string_view::npos) {
			lines.emplace_back(text.substr(start));
			break;
		}
		lines.emplace_back(text.substr(start, nl - start));
		start = nl + 1;
	}

	// Remove blank lines in the beginning and end
	// and convert all tabs in the beginning of line to spaces
	int startLine = -1;
	int lastLine = 0;
	for (size_t i = 0; i < lines.size(); i++) {
		std::string line = lines[i];
		int firstNonWhite = indexFunc(line, [](char32_t r) {
			return !tsc::isWhiteSpaceLike(r);
		});
		if (firstNonWhite > 0) {
			line = replaceAll(line.substr(0, firstNonWhite), "\t", "    ") +
			       line.substr(firstNonWhite);
			lines[i] = line;
		}
		line = std::string(trimSpace(line));
		if (!line.empty()) {
			if (startLine == -1) {
				startLine = (int)i;
			}
			lastLine = (int)i;
		}
	}
	// lines = lines[startLine : lastLine+1] — Go panics on an all-blank
	// input (startLine stays -1).
	if (startLine < 0) {
		TSC_UNREACHABLE("slice bounds out of range [-1:]");
	}
	lines.erase(lines.begin(), lines.begin() + startLine);
	lines.erase(lines.begin() + lastLine - startLine + 1, lines.end());

	std::vector<std::string_view> mappedLines;
	mappedLines.reserve(lines.size());
	for (auto& line : lines) {
		if (trimSpace(line).empty()) {
			mappedLines.emplace_back("");
		} else {
			mappedLines.emplace_back(line);
		}
	}
	int indentation = guessIndentation(mappedLines);
	if (indentation > 0) {
		for (auto& line : lines) {
			if ((int)line.size() > indentation) {
				line = line.substr(indentation);
			} else {
				line = "";
			}
		}
	}
	// strings.Join(lines, "\n")
	std::string out;
	for (size_t i = 0; i < lines.size(); i++) {
		if (i) out += '\n';
		out += lines[i];
	}
	return out;
}

}  // namespace tsc::testutil::stringtestutil
