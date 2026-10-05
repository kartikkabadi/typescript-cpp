// Port of tsc/internal/tsoptions/namemap.go — NameMap + the three shared
// name maps. Tables are function-static so cross-TU init order is irrelevant.
#include "internal/tsoptions/tsoptions.h"

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

}  // namespace tsc::tsoptions
