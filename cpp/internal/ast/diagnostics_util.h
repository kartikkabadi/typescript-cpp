// Port of the diagnostic comparison helpers + DiagnosticsCollection from
// tsc/internal/ast/diagnostic.go.
#pragma once

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/ast/ast.h"

namespace tsc {

inline std::string getDiagnosticPath(const Diagnostic* d) {
	return d->File() ? std::string(d->File()->FileName()) : std::string();
}

inline std::string_view getDiagnosticMessageIdentity(const Diagnostic* diagnostic) {
	if (!diagnostic->MessageText().empty()) {
		return diagnostic->MessageText();
	}
	if (diagnostic->message != nullptr && diagnostic->Code() == -1) {
		return diagnostic->message->text;
	}
	return diagnostic->MessageKey();
}

inline bool equalMessageChain(const Diagnostic* c1, const Diagnostic* c2) {
	if (c1 == c2) {
		return true;
	}
	if (c1->Code() != c2->Code() || c1->MessageArgs() != c2->MessageArgs()) {
		return false;
	}
	auto& a = c1->MessageChain();
	auto& b = c2->MessageChain();
	return a.size() == b.size() &&
		   std::equal(a.begin(), a.end(), b.begin(), equalMessageChain);
}

inline bool EqualDiagnosticsNoRelatedInfo(const Diagnostic* d1, const Diagnostic* d2) {
	if (d1 == d2) {
		return true;
	}
	return getDiagnosticPath(d1) == getDiagnosticPath(d2) &&
		   d1->Loc().pos() == d2->Loc().pos() && d1->Loc().end() == d2->Loc().end() &&
		   d1->Code() == d2->Code() && d1->Category() == d2->Category() &&
		   d1->Source() == d2->Source() &&
		   getDiagnosticMessageIdentity(d1) == getDiagnosticMessageIdentity(d2) &&
		   d1->MessageArgs() == d2->MessageArgs() &&
		   [&] {
			   auto& a = d1->MessageChain();
			   auto& b = d2->MessageChain();
			   return a.size() == b.size() &&
					  std::equal(a.begin(), a.end(), b.begin(), equalMessageChain);
		   }();
}

inline bool EqualDiagnostics(const Diagnostic* d1, const Diagnostic* d2) {
	if (d1 == d2) {
		return true;
	}
	if (!EqualDiagnosticsNoRelatedInfo(d1, d2)) {
		return false;
	}
	auto& a = d1->RelatedInformation();
	auto& b = d2->RelatedInformation();
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), EqualDiagnostics);
}

inline int compareMessageChainSize(const std::vector<Diagnostic*>& c1,
								   const std::vector<Diagnostic*>& c2) {
	int c = static_cast<int>(c2.size()) - static_cast<int>(c1.size());
	if (c != 0) {
		return c;
	}
	for (size_t i = 0; i < c1.size(); i++) {
		c = compareMessageChainSize(c1[i]->MessageChain(), c2[i]->MessageChain());
		if (c != 0) {
			return c;
		}
	}
	return 0;
}

inline int compareStringVecs(const std::vector<std::string>& a,
							 const std::vector<std::string>& b) {
	size_t n = std::min(a.size(), b.size());
	for (size_t i = 0; i < n; i++) {
		if (a[i] != b[i]) {
			return a[i] < b[i] ? -1 : 1;
		}
	}
	return static_cast<int>(a.size()) - static_cast<int>(b.size());
}

inline int compareMessageChainContent(const std::vector<Diagnostic*>& c1,
									  const std::vector<Diagnostic*>& c2) {
	for (size_t i = 0; i < c1.size(); i++) {
		int c = compareStringVecs(c1[i]->MessageArgs(), c2[i]->MessageArgs());
		if (c != 0) {
			return c;
		}
		if (!c1[i]->MessageChain().empty()) {
			c = compareMessageChainContent(c1[i]->MessageChain(), c2[i]->MessageChain());
			if (c != 0) {
				return c;
			}
		}
	}
	return 0;
}

inline int compareRelatedInfo(const std::vector<Diagnostic*>& r1,
							  const std::vector<Diagnostic*>& r2);

inline int CompareDiagnostics(const Diagnostic* d1, const Diagnostic* d2) {
	if (d1 == d2) {
		return 0;
	}
	int c = getDiagnosticPath(d1).compare(getDiagnosticPath(d2));
	if (c != 0) {
		return c;
	}
	c = d1->Loc().pos() - d2->Loc().pos();
	if (c != 0) {
		return c;
	}
	c = d1->Loc().end() - d2->Loc().end();
	if (c != 0) {
		return c;
	}
	c = d1->Code() - d2->Code();
	if (c != 0) {
		return c;
	}
	c = static_cast<int>(d1->Category()) - static_cast<int>(d2->Category());
	if (c != 0) {
		return c;
	}
	c = std::string(d1->Source()).compare(std::string(d2->Source()));
	if (c != 0) {
		return c;
	}
	c = std::string(getDiagnosticMessageIdentity(d1))
			.compare(std::string(getDiagnosticMessageIdentity(d2)));
	if (c != 0) {
		return c;
	}
	c = compareStringVecs(d1->MessageArgs(), d2->MessageArgs());
	if (c != 0) {
		return c;
	}
	c = compareMessageChainSize(d1->MessageChain(), d2->MessageChain());
	if (c != 0) {
		return c;
	}
	c = compareMessageChainContent(d1->MessageChain(), d2->MessageChain());
	if (c != 0) {
		return c;
	}
	return compareRelatedInfo(d1->RelatedInformation(), d2->RelatedInformation());
}

inline int compareRelatedInfo(const std::vector<Diagnostic*>& r1,
							  const std::vector<Diagnostic*>& r2) {
	int c = static_cast<int>(r2.size()) - static_cast<int>(r1.size());
	if (c != 0) {
		return c;
	}
	for (size_t i = 0; i < r1.size(); i++) {
		c = CompareDiagnostics(r1[i], r2[i]);
		if (c != 0) {
			return c;
		}
	}
	return 0;
}

struct DiagnosticLocationKey {
	std::string path;
	TextRange loc;
	int32_t code;
	bool operator==(const DiagnosticLocationKey&) const = default;
};

inline DiagnosticLocationKey getDiagnosticLocationKey(const Diagnostic* diagnostic) {
	return {getDiagnosticPath(diagnostic), diagnostic->Loc(), diagnostic->Code()};
}

struct DiagnosticsCollection {
	std::mutex mu;
	int count = 0;
	std::unordered_map<std::string, std::vector<Diagnostic*>> fileDiagnostics;
	std::unordered_set<std::string> fileDiagnosticsSorted;
	std::vector<Diagnostic*> nonFileDiagnostics;
	bool nonFileDiagnosticsSorted = false;
	std::unordered_map<std::string, Diagnostic*> diagnosticIndex;
	std::unordered_map<std::string, std::vector<Diagnostic*>> diagnosticCollisions;

	static std::string keyString(const DiagnosticLocationKey& k) {
		std::string s = k.path;
		s += '|';
		s += std::to_string(k.loc.pos());
		s += '|';
		s += std::to_string(k.loc.end());
		s += '|';
		s += std::to_string(k.code);
		return s;
	}

	Diagnostic* Add(Diagnostic* diagnostic) {
		std::lock_guard<std::mutex> lock(mu);
		auto key = keyString(getDiagnosticLocationKey(diagnostic));
		auto it = diagnosticIndex.find(key);
		if (it != diagnosticIndex.end() && it->second != nullptr) {
			if (EqualDiagnostics(it->second, diagnostic)) {
				return it->second;
			}
			for (auto* collision : diagnosticCollisions[key]) {
				if (EqualDiagnostics(collision, diagnostic)) {
					return collision;
				}
			}
		}
		if (diagnosticIndex.find(key) == diagnosticIndex.end()) {
			diagnosticIndex[key] = diagnostic;
		} else {
			diagnosticCollisions[key].push_back(diagnostic);
		}
		count++;
		if (diagnostic->File() != nullptr) {
			auto path = std::string(diagnostic->File()->Path());
			fileDiagnostics[path].push_back(diagnostic);
			fileDiagnosticsSorted.erase(path);
		} else {
			nonFileDiagnostics.push_back(diagnostic);
			nonFileDiagnosticsSorted = false;
		}
		return diagnostic;
	}

	std::vector<Diagnostic*> GetGlobalDiagnostics() {
		std::lock_guard<std::mutex> lock(mu);
		if (!nonFileDiagnosticsSorted) {
			std::stable_sort(nonFileDiagnostics.begin(), nonFileDiagnostics.end(),
							 [](Diagnostic* a, Diagnostic* b) { return CompareDiagnostics(a, b) < 0; });
			nonFileDiagnosticsSorted = true;
		}
		return nonFileDiagnostics;
	}

	std::vector<Diagnostic*> GetDiagnosticsForFile(SourceFile* file) {
		std::lock_guard<std::mutex> lock(mu);
		auto path = std::string(file->Path());
		auto& diags = fileDiagnostics[path];
		if (!fileDiagnosticsSorted.count(path)) {
			std::stable_sort(diags.begin(), diags.end(),
							 [](Diagnostic* a, Diagnostic* b) { return CompareDiagnostics(a, b) < 0; });
			fileDiagnosticsSorted.insert(path);
		}
		return diags;
	}

	std::vector<Diagnostic*> GetDiagnostics() {
		std::lock_guard<std::mutex> lock(mu);
		std::vector<Diagnostic*> diagnostics;
		diagnostics.reserve(count);
		diagnostics.insert(diagnostics.end(), nonFileDiagnostics.begin(), nonFileDiagnostics.end());
		for (auto& [_, diags] : fileDiagnostics) {
			diagnostics.insert(diagnostics.end(), diags.begin(), diags.end());
		}
		std::sort(diagnostics.begin(), diagnostics.end(),
				  [](Diagnostic* a, Diagnostic* b) { return CompareDiagnostics(a, b) < 0; });
		return diagnostics;
	}
};

} // namespace tsc
