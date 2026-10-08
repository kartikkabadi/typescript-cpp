// Patience Diff — function-by-function port of
// github.com/peter-evans/patience: patience.go, lcs.go, unified.go, format.go.
#include "internal/patience/patience.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace tsc::patience {

namespace {

// toDiffLines — patience.go:24.
std::vector<DiffLine> toDiffLines(
    const std::vector<std::string_view>& a, DiffType t) {
	std::vector<DiffLine> diffs;
	diffs.reserve(a.size());
	for (auto l : a) {
		diffs.push_back(DiffLine{std::string(l), t});
	}
	return diffs;
}

// uniqueElements — patience.go:33.
std::pair<std::vector<std::string_view>, std::vector<int>> uniqueElements(
    const std::vector<std::string_view>& a) {
	std::unordered_map<std::string_view, int> m;
	for (auto e : a) {
		m[e]++;
	}
	std::vector<std::string_view> elements;
	std::vector<int> indices;
	for (int i = 0; i < (int)a.size(); i++) {
		if (m[a[i]] == 1) {
			elements.push_back(a[i]);
			indices.push_back(i);
		}
	}
	return {elements, indices};
}

void append(std::vector<DiffLine>& dst, std::vector<DiffLine>&& src) {
	dst.insert(dst.end(), std::make_move_iterator(src.begin()),
	           std::make_move_iterator(src.end()));
}

}  // namespace

// LCS — lcs.go:5. Returns index pairs in ascending order.
std::vector<std::pair<int, int>> LCS(const std::vector<std::string_view>& a,
                                     const std::vector<std::string_view>& b) {
	std::vector<std::vector<int>> lcs(a.size() + 1,
	                                  std::vector<int>(b.size() + 1, 0));
	for (size_t i = 1; i < lcs.size(); i++) {
		for (size_t j = 1; j < lcs[i].size(); j++) {
			if (a[i - 1] == b[j - 1]) {
				lcs[i][j] = lcs[i - 1][j - 1] + 1;
			} else {
				lcs[i][j] = std::max(lcs[i - 1][j], lcs[i][j - 1]);
			}
		}
	}

	int i = (int)a.size();
	int j = (int)b.size();
	std::vector<std::pair<int, int>> s;
	s.reserve(lcs[i][j]);
	while (i > 0 && j > 0) {
		if (a[i - 1] == b[j - 1]) {
			s.push_back({i - 1, j - 1});
			i--;
			j--;
		} else if (lcs[i - 1][j] > lcs[i][j - 1]) {
			i--;
		} else {
			j--;
		}
	}
	std::reverse(s.begin(), s.end());
	return s;
}

// Diff — patience.go:50.
std::vector<DiffLine> Diff(const std::vector<std::string_view>& a,
                           const std::vector<std::string_view>& b) {
	if (a.empty() && b.empty()) {
		return {};
	}
	if (a.empty()) {
		return toDiffLines(b, DiffType::Insert);
	}
	if (b.empty()) {
		return toDiffLines(a, DiffType::Delete);
	}

	auto tail = [](const std::vector<std::string_view>& v, int i) {
		return std::vector<std::string_view>(v.begin() + i, v.end());
	};
	auto head = [](const std::vector<std::string_view>& v, int i) {
		return std::vector<std::string_view>(v.begin(), v.begin() + i);
	};

	// Equal elements at the head.
	int i = 0;
	while (i < (int)a.size() && i < (int)b.size() && a[i] == b[i]) {
		i++;
	}
	if (i > 0) {
		auto diffs = toDiffLines(head(a, i), DiffType::Equal);
		append(diffs, Diff(tail(a, i), tail(b, i)));
		return diffs;
	}

	// Equal elements at the tail.
	int j = 0;
	while (j < (int)a.size() && j < (int)b.size() &&
	       a[a.size() - 1 - j] == b[b.size() - 1 - j]) {
		j++;
	}
	if (j > 0) {
		auto diffs = Diff(head(a, (int)a.size() - j),
		                  head(b, (int)b.size() - j));
		append(diffs, toDiffLines(tail(a, (int)a.size() - j), DiffType::Equal));
		return diffs;
	}

	// Longest common subsequence of unique elements.
	auto [ua, idxa] = uniqueElements(a);
	auto [ub, idxb] = uniqueElements(b);
	auto lcs = LCS(ua, ub);

	if (lcs.empty()) {
		auto diffs = toDiffLines(a, DiffType::Delete);
		append(diffs, toDiffLines(b, DiffType::Insert));
		return diffs;
	}

	// Map back to original indices.
	for (auto& x : lcs) {
		x.first = idxa[x.first];
		x.second = idxb[x.second];
	}

	std::vector<DiffLine> diffs;
	int ga = 0, gb = 0;
	for (auto& ip : lcs) {
		append(diffs, Diff(std::vector<std::string_view>(a.begin() + ga,
		                                               a.begin() + ip.first),
		                   std::vector<std::string_view>(b.begin() + gb,
		                                               b.begin() + ip.second)));
		diffs.push_back(DiffLine{std::string(a[ip.first]), DiffType::Equal});
		ga = ip.first + 1;
		gb = ip.second + 1;
	}
	append(diffs, Diff(tail(a, ga), tail(b, gb)));
	return diffs;
}

// makeHunks — unified.go:13.
std::vector<Hunk> makeHunks(const std::vector<DiffLine>& diffs, int precontext,
                            int postcontext) {
	if (diffs.empty()) {
		return {};
	}

	std::vector<Hunk> hunks;

	auto sliceFrom = [](const std::vector<DiffLine>& v, int i) {
		return std::vector<DiffLine>(v.begin() + i, v.end());
	};
	auto sliceTo = [](const std::vector<DiffLine>& v, int i) {
		return std::vector<DiffLine>(v.begin(), v.begin() + i);
	};

	auto updateHunks = [&](const Hunk& block, bool lastBlock) {
		int curHunk = (int)hunks.size() - 1;
		if (block.Diffs[0].Type == DiffType::Equal) {
			if (hunks.empty()) {
				int ctxLen =
				    std::min(precontext, (int)block.Diffs.size());
				hunks.push_back(Hunk{
				    sliceFrom(block.Diffs,
				              (int)block.Diffs.size() - ctxLen),
				    (int)block.Diffs.size() - ctxLen + block.SrcStart,
				    ctxLen,
				    (int)block.Diffs.size() - ctxLen + block.DstStart,
				    ctxLen,
				});
			} else {
				int maxNonContext = precontext + postcontext;
				if (lastBlock) {
					maxNonContext = postcontext;
				}
				if ((int)block.Diffs.size() <= maxNonContext) {
					auto& h = hunks[curHunk];
					h.Diffs.insert(h.Diffs.end(), block.Diffs.begin(),
					               block.Diffs.end());
					h.SrcLines += (int)block.Diffs.size();
					h.DstLines += (int)block.Diffs.size();
				} else {
					auto& h = hunks[curHunk];
					h.Diffs.insert(h.Diffs.end(), block.Diffs.begin(),
					               block.Diffs.begin() + postcontext);
					h.SrcLines += postcontext;
					h.DstLines += postcontext;
					if (!lastBlock) {
						hunks.push_back(Hunk{
						    sliceFrom(block.Diffs,
						              (int)block.Diffs.size() -
						                  precontext),
						    (int)block.Diffs.size() - precontext +
						        block.SrcStart,
						    precontext,
						    (int)block.Diffs.size() - precontext +
						        block.DstStart,
						    precontext,
						});
					}
				}
				if (hunks[curHunk].SrcStart == 0) {
					hunks[curHunk].SrcStart = block.SrcStart;
				}
				if (hunks[curHunk].DstStart == 0) {
					hunks[curHunk].DstStart = block.DstStart;
				}
			}
		} else {
			if (!hunks.empty()) {
				auto& h = hunks[curHunk];
				h.Diffs.insert(h.Diffs.end(), block.Diffs.begin(),
				               block.Diffs.end());
				h.SrcLines += block.SrcLines;
				h.DstLines += block.DstLines;
			} else {
				hunks.push_back(block);
			}
		}
	};

	Hunk block;
	int modifiedLines = 0;
	int srcLineNum = 0, dstLineNum = 0;
	for (const auto& l : diffs) {
		if (block.Diffs.empty() || block.Diffs[0].Type == l.Type ||
		    (block.Diffs[0].Type != l.Type &&
		     block.Diffs[0].Type != DiffType::Equal &&
		     l.Type != DiffType::Equal)) {
			block.Diffs.push_back(l);
		} else {
			updateHunks(block, false);
			block = Hunk{};
			block.Diffs = {l};
		}

		switch (l.Type) {
			case DiffType::Delete:
				srcLineNum++;
				block.SrcLines++;
				modifiedLines++;
				break;
			case DiffType::Insert:
				dstLineNum++;
				block.DstLines++;
				modifiedLines++;
				break;
			case DiffType::Equal:
				srcLineNum++;
				dstLineNum++;
				block.SrcLines++;
				block.DstLines++;
				break;
		}

		if (block.SrcStart == 0 &&
		    (l.Type == DiffType::Equal || l.Type == DiffType::Delete)) {
			block.SrcStart = srcLineNum;
		}
		if (block.DstStart == 0 &&
		    (l.Type == DiffType::Equal || l.Type == DiffType::Insert)) {
			block.DstStart = dstLineNum;
		}
	}
	updateHunks(block, true);

	if (modifiedLines == 0) {
		return {};
	}
	return hunks;
}

// typeSymbol — format.go:9.
std::string typeSymbol(DiffType t) {
	switch (t) {
		case DiffType::Equal:
			return " ";
		case DiffType::Insert:
			return "+";
		case DiffType::Delete:
			return "-";
		default:
			throw std::runtime_error("unknown DiffType");
	}
}

// DiffText — format.go:24.
std::string DiffText(const std::vector<DiffLine>& diffs) {
	std::vector<std::string> s(diffs.size());
	for (size_t i = 0; i < diffs.size(); i++) {
		const auto& l = diffs[i];
		if (l.Text.empty() && l.Type == DiffType::Equal) {
			continue;
		}
		s[i] = typeSymbol(l.Type) + l.Text;
	}
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (i) out += '\n';
		out += s[i];
	}
	return out;
}

// DiffTextA — format.go:35.
std::string DiffTextA(const std::vector<DiffLine>& diffs) {
	std::vector<std::string> s;
	for (const auto& l : diffs) {
		if (l.Type == DiffType::Insert) {
			continue;
		}
		if (l.Type == DiffType::Equal && l.Text.empty()) {
			s.push_back("");
		} else {
			s.push_back(typeSymbol(l.Type) + l.Text);
		}
	}
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (i) out += '\n';
		out += s[i];
	}
	return out;
}

// DiffTextB — format.go:49.
std::string DiffTextB(const std::vector<DiffLine>& diffs) {
	std::vector<std::string> s;
	for (const auto& l : diffs) {
		if (l.Type == DiffType::Delete) {
			continue;
		}
		if (l.Type == DiffType::Equal && l.Text.empty()) {
			s.push_back("");
		} else {
			s.push_back(typeSymbol(l.Type) + l.Text);
		}
	}
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (i) out += '\n';
		out += s[i];
	}
	return out;
}

// UnifiedDiffTextWithOptions — format.go:73.
std::string UnifiedDiffTextWithOptions(const std::vector<DiffLine>& diffs,
                                       const UnifiedDiffOptions& opts) {
	auto hunks = makeHunks(diffs, opts.Precontext, opts.Postcontext);
	std::vector<std::string> s;
	if (!opts.SrcHeader.empty()) {
		s.push_back("--- " + opts.SrcHeader);
	}
	if (!opts.DstHeader.empty()) {
		s.push_back("+++ " + opts.DstHeader);
	}
	for (const auto& h : hunks) {
		s.push_back("@@ -" + std::to_string(h.SrcStart) + "," +
		            std::to_string(h.SrcLines) + " +" +
		            std::to_string(h.DstStart) + "," +
		            std::to_string(h.DstLines) + " @@");
		for (const auto& l : h.Diffs) {
			if (l.Type == DiffType::Equal && l.Text.empty()) {
				s.push_back("");
			} else {
				s.push_back(typeSymbol(l.Type) + l.Text);
			}
		}
	}
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (i) out += '\n';
		out += s[i];
	}
	return out;
}

// UnifiedDiffText — format.go:98.
std::string UnifiedDiffText(const std::vector<DiffLine>& diffs) {
	return UnifiedDiffTextWithOptions(diffs,
	                                  UnifiedDiffOptions{/*.Precontext*/ 3,
	                                                     /*.Postcontext*/ 3});
}

}  // namespace tsc::patience
