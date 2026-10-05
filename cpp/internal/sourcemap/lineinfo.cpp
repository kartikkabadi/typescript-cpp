// Port of tsc/internal/sourcemap/lineinfo.go
#include "internal/sourcemap/sourcemap.h"

namespace tsc::sourcemap {

// CreateECMALineInfo — lineinfo.go:10
ECMALineInfo* CreateECMALineInfo(std::string_view text,
                                 ECMALineStarts lineStarts) {
	return new ECMALineInfo{std::string(text), std::move(lineStarts)};
}

// LineCount — lineinfo.go:17
int ECMALineInfo::LineCount() const {
	return static_cast<int>(lineStarts.size());
}

// LineText — lineinfo.go:21
std::string_view ECMALineInfo::LineText(int line) const {
	TextPos pos = lineStarts[line];
	TextPos end;
	if (line + 1 < static_cast<int>(lineStarts.size())) {
		end = lineStarts[line + 1];
	} else {
		end = static_cast<TextPos>(text.size());
	}
	return std::string_view(text).substr(static_cast<size_t>(pos),
	                                     static_cast<size_t>(end - pos));
}

} // namespace tsc::sourcemap
