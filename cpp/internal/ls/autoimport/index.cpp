// index.go — case-aware word-prefix index over export names.
#include <unordered_set>

#include "internal/ls/autoimport/autoimport.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::ls::autoimport {

namespace {

// strings.ToLower — Go lowercases per rune via the simple case mapping;
// invalid UTF-8 bytes become U+FFFD (range-over-string semantics).
std::string toLowerGo(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	int w = 0;
	for (std::string_view rest = s; !rest.empty(); rest.remove_prefix(w)) {
		char32_t r = decodeUtf8Rune(rest, &w);
		out += utf8String(stringutil::toLowerRune(r));
	}
	return out;
}

// utf8.DecodeRuneInString — (RuneError, 0) empty / (RuneError, 1) invalid.
char32_t decodeRuneInString(std::string_view s, int* width) {
	return decodeUtf8Rune(s, width);
}

const std::vector<int>& emptyIntVector() {
	static const std::vector<int> v;
	return v;
}

}  // namespace

// containsCharsInOrder — index.go:97
bool containsCharsInOrder(std::string str, std::string pattern) {
	str = toLowerGo(str);
	pattern = toLowerGo(pattern);

	size_t patternIdx = 0;
	int w = 0;
	for (std::string_view rest = str; !rest.empty(); rest.remove_prefix(w)) {
		char32_t ch = decodeUtf8Rune(rest, &w);
		if (patternIdx < pattern.size()) {
			int pw = 0;
			char32_t patternRune = decodeUtf8Rune(
			    std::string_view(pattern).substr(patternIdx), &pw);
			if (ch == patternRune) {
				patternIdx += pw;
			}
		}
	}
	return patternIdx == pattern.size();
}

// Index.Find — index.go:24
std::vector<Export*> Index::Find(const std::string& name,
                                 bool caseSensitive) const {
	if (entries.empty() || name.empty()) {
		return {};
	}
	int w = 0;
	char32_t firstRune = decodeRuneInString(name, &w);
	if (firstRune == kRuneError) {
		return {};
	}
	char32_t firstRuneUpper = stringutil::toUpperRune(firstRune);
	auto it = index.find(firstRuneUpper);
	if (it == index.end()) {
		return {};
	}

	std::vector<Export*> results;
	for (int entryIndex : it->second) {
		const std::shared_ptr<Export>& entry = entries[entryIndex];
		const std::string entryName = entry->Name();
		if ((caseSensitive && entryName == name) ||
		    (!caseSensitive &&
		     stringutil::EquateStringCaseInsensitive(entryName, name))) {
			results.push_back(entry.get());
		}
	}

	return results;
}

// Index.SearchWordPrefix — index.go:54
std::vector<Export*> Index::SearchWordPrefix(const std::string& prefix_) const {
	if (entries.empty()) {
		return {};
	}
	std::string prefix = prefix_;

	if (prefix.empty()) {
		std::vector<Export*> all;
		all.reserve(entries.size());
		for (const auto& e : entries) {
			all.push_back(e.get());
		}
		return all;
	}

	prefix = toLowerGo(prefix);
	int w = 0;
	char32_t firstRune = decodeRuneInString(prefix, &w);
	if (firstRune == kRuneError) {
		return {};
	}

	char32_t firstRuneUpper = stringutil::toUpperRune(firstRune);
	char32_t firstRuneLower = stringutil::toLowerRune(firstRune);

	// Look up entries that have words starting with this letter
	auto nameStartsIt = index.find(firstRuneUpper);
	const std::vector<int>& nameStarts =
	    nameStartsIt != index.end() ? nameStartsIt->second : emptyIntVector();
	const std::vector<int>& wordStarts =
	    firstRuneUpper != firstRuneLower && index.find(firstRuneLower) != index.end()
	        ? index.find(firstRuneLower)->second
	        : emptyIntVector();
	size_t count = nameStarts.size() + wordStarts.size();
	if (count == 0) {
		return {};
	}

	// Filter entries by checking if they contain all characters in order
	std::vector<Export*> results;
	results.reserve(count);
	for (const std::vector<int>& starts : {nameStarts, wordStarts}) {
		for (int i : starts) {
			const std::shared_ptr<Export>& entry = entries[i];
			if (containsCharsInOrder(entry->Name(), prefix)) {
				results.push_back(entry.get());
			}
		}
	}
	return results;
}

// Index.insertAsWords — index.go:114
void Index::insertAsWords(const std::shared_ptr<Export>& value) {
	const std::string name = value->Name();
	if (name.empty()) {
		TSC_UNREACHABLE("Cannot index entry with empty name");
	}
	int entryIndex = static_cast<int>(entries.size());
	entries.push_back(value);

	std::vector<int> indices = wordIndices(name);
	std::unordered_set<char32_t> seenRunes;

	for (size_t i = 0; i < indices.size(); i++) {
		int start = indices[i];
		std::string_view substr = std::string_view(name).substr(start);
		int w = 0;
		char32_t firstRune = decodeRuneInString(substr, &w);
		if (firstRune == kRuneError) {
			continue;
		}
		if (i == 0) {
			// Name start keyed by uppercase
			firstRune = stringutil::toUpperRune(firstRune);
			index[firstRune].push_back(entryIndex);
			// (Still set seenRunes in case first character is non-alphabetic)
			seenRunes.insert(firstRune);
		} else {
			// Subsequent word starts keyed by lowercase
			firstRune = stringutil::toLowerRune(firstRune);
			if (seenRunes.find(firstRune) == seenRunes.end()) {
				index[firstRune].push_back(entryIndex);
				seenRunes.insert(firstRune);
			}
		}
	}
}

// Index.Clone — index.go:152. Nil-receiver tolerant like Go; free function
// because calling a member on nullptr is UB (see autoimport.h).
std::unique_ptr<Index> Clone(
    const Index* idx,
    const std::function<bool(const std::shared_ptr<Export>&)>& filter) {
	if (idx == nullptr) {
		return nullptr;
	}

	auto newIdx = std::make_unique<Index>();
	newIdx->entries.reserve(idx->entries.size());
	newIdx->index.reserve(idx->index.size());

	// Build mapping from old index to new index for filtered entries
	std::unordered_map<int, int> oldToNew;
	oldToNew.reserve(idx->entries.size());
	for (size_t oldIndex = 0; oldIndex < idx->entries.size(); oldIndex++) {
		const auto& entry = idx->entries[oldIndex];
		if (filter(entry)) {
			int newIndex = static_cast<int>(newIdx->entries.size());
			newIdx->entries.push_back(entry);
			oldToNew[static_cast<int>(oldIndex)] = newIndex;
		}
	}

	// Rebuild the index with remapped indices
	for (const auto& kv : idx->index) {
		std::vector<int> newIndices;
		newIndices.reserve(kv.second.size());
		for (int oldIndex : kv.second) {
			auto it = oldToNew.find(oldIndex);
			if (it != oldToNew.end()) {
				newIndices.push_back(it->second);
			}
		}
		if (!newIndices.empty()) {
			newIdx->index[kv.first] = std::move(newIndices);
		}
	}

	return newIdx;
}

}  // namespace tsc::ls::autoimport
