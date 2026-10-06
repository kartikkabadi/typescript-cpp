// Port of github.com/peter-evans/patience (patience.go, lcs.go, unified.go,
// format.go) — the Patience Diff algorithm used by testutil/baseline.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tsc::patience {

// DiffType — patience.go:6.
enum class DiffType : int8_t {
	Delete = -1,
	Insert = 1,
	Equal = 0,
};

// DiffLine — patience.go:16.
struct DiffLine {
	std::string Text;
	DiffType Type;
};

// Hunk — unified.go:5.
struct Hunk {
	std::vector<DiffLine> Diffs;
	int SrcStart = 0;
	int SrcLines = 0;
	int DstStart = 0;
	int DstLines = 0;
};

// UnifiedDiffOptions — format.go:61.
struct UnifiedDiffOptions {
	int Precontext = 0;
	int Postcontext = 0;
	std::string SrcHeader;
	std::string DstHeader;
};

std::vector<DiffLine> Diff(const std::vector<std::string_view>& a,
                           const std::vector<std::string_view>& b);
std::vector<std::pair<int, int>> LCS(const std::vector<std::string_view>& a,
                                     const std::vector<std::string_view>& b);
std::vector<Hunk> makeHunks(const std::vector<DiffLine>& diffs, int precontext,
                            int postcontext);
std::string typeSymbol(DiffType t);
std::string DiffText(const std::vector<DiffLine>& diffs);
std::string DiffTextA(const std::vector<DiffLine>& diffs);
std::string DiffTextB(const std::vector<DiffLine>& diffs);
std::string UnifiedDiffTextWithOptions(const std::vector<DiffLine>& diffs,
                                       const UnifiedDiffOptions& opts);
std::string UnifiedDiffText(const std::vector<DiffLine>& diffs);

}  // namespace tsc::patience
