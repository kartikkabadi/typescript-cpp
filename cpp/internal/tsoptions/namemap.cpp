// Port of tsc/internal/tsoptions/namemap.go — NameMap + the three shared
// name maps. Tables are function-static so cross-TU init order is irrelevant.
#include "internal/tsoptions/tsoptions.h"

#include "internal/core/spelling.h"

#include <cctype>

namespace tsc::tsoptions {

namespace {

std::string toLowerGo(std::string_view s) {
	std::string r(s);
	for (auto& c : r) c = (char)std::tolower((unsigned char)c);
	return r;
}

}  // namespace

// GetNameMapFromList — namemap.go:15.
std::shared_ptr<NameMap> GetNameMapFromList(
    const std::vector<const CommandLineOption*>& optDecls) {
	auto result = std::make_shared<NameMap>();
	result->optionsNames = collections::OrderedMap<
	    std::string, const CommandLineOption*>(optDecls.size());
	for (const auto* option : optDecls) {
		result->optionsNames.Set(toLowerGo(option->Name), option);
		if (!option->ShortName.empty()) {
			result->shortOptionNames[option->ShortName] = option->Name;
		}
	}
	return result;
}

const CommandLineOption* NameMap::Get(std::string_view name) const {
	auto v = optionsNames.GetOrZero(toLowerGo(name));
	return v;
}

const CommandLineOption* NameMap::GetFromShort(
    std::string_view shortName) const {
	// returns option only if shortName is a valid short option
	auto it = shortOptionNames.find(std::string(shortName));
	if (it == shortOptionNames.end()) {
		return nullptr;
	}
	return Get(it->second);
}

const CommandLineOption* NameMap::GetOptionDeclarationFromName(
    std::string_view optionName, bool allowShort) const {
	std::string lowered = toLowerGo(optionName);
	// Try to translate short option names to their full equivalents.
	if (allowShort) {
		auto it = shortOptionNames.find(lowered);
		if (it != shortOptionNames.end() && !it->second.empty()) {
			lowered = it->second;
		}
	}
	return Get(lowered);
}

const NameMap& CompilerNameMap() {
	static const std::shared_ptr<NameMap> m =
	    GetNameMapFromList(OptionsDeclarations());
	return *m;
}
const NameMap& BuildNameMap() {
	static const std::shared_ptr<NameMap> m =
	    GetNameMapFromList(BuildOpts());
	return *m;
}
const NameMap& WatchNameMap() {
	static const std::shared_ptr<NameMap> m =
	    GetNameMapFromList(OptionsForWatch());
	return *m;
}

// CommandLineOptionNameMap — tsconfigparsing.go:596.
const CommandLineOption* CommandLineOptionNameMap::Get(
    std::string_view name) const {
	auto it = m.find(std::string(name));
	const CommandLineOption* opt =
	    it != m.end() ? it->second : nullptr;
	if (opt == nullptr) {
		auto it2 = m.find(toLowerGo(name));
		opt = it2 != m.end() ? it2->second : nullptr;
	}
	return opt;
}

const CommandLineOption* CommandLineOptionNameMap::GetSpellingSuggestion(
    std::string_view name) const {
	// Go: core.GetSpellingSuggestion(name, maps.Values(m), name, compare) —
	// iterates the map's values; Go map iteration order is randomized but
	// the suggestion algorithm is order-stable for distinct distances, so
	// sort by name first for determinism.
	std::vector<const CommandLineOption*> candidates;
	candidates.reserve(m.size());
	for (auto& [k, v] : m) {
		candidates.push_back(v);
	}
	std::sort(candidates.begin(), candidates.end(),
	          [](const CommandLineOption* a, const CommandLineOption* b) {
		          return a->Name < b->Name;
	          });
	return getSpellingSuggestion(
	    name, candidates,
	    [](const CommandLineOption* o) { return o->Name; },
	    [](const CommandLineOption* a, const CommandLineOption* b) {
		    return a->Name.compare(b->Name);
	    });
}

// commandLineOptionsToMap — tsconfigparsing.go:613.
CommandLineOptionNameMap commandLineOptionsToMap(
    const std::vector<const CommandLineOption*>& compilerOptions) {
	CommandLineOptionNameMap result;
	result.m.reserve(compilerOptions.size() * 2);
	for (const auto* opt : compilerOptions) {
		result.m[opt->Name] = opt;
		result.m[toLowerGo(opt->Name)] = opt;
	}
	return result;
}

}  // namespace tsc::tsoptions
