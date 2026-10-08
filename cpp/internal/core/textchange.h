// Port of tsc/internal/core/textchange.go.
#pragma once

#include <string>
#include <vector>

#include "internal/core/text.h"

namespace tsc {

struct TextChange : TextRange {
	std::string NewText;

	std::string ApplyTo(const std::string& text) const {
		return text.substr(0, pos()) + NewText + text.substr(end());
	}
};

inline std::string ApplyBulkEdits(const std::string& text, const std::vector<TextChange>& edits) {
	std::string b;
	b.reserve(text.size());
	TextPos lastEnd = 0;
	for (const TextChange& e : edits) {
		TextPos start = e.pos();
		if (start != lastEnd) {
			b.append(text, lastEnd, e.pos() - lastEnd);
		}
		b.append(e.NewText);
		lastEnd = e.end();
	}
	b.append(text, lastEnd, std::string::npos);
	return b;
}

} // namespace tsc
