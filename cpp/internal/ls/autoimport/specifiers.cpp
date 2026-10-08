// specifiers.go — View::GetModuleSpecifier.
#include "internal/ls/autoimport/autoimport.h"
#include "internal/modulespecifiers/types.h"

namespace tsc::ls::autoimport {

// View::GetModuleSpecifier — specifiers.go:9
std::pair<std::string, modulespecifiers::ResultKind> View::GetModuleSpecifier(
    Export* e, const modulespecifiers::UserPreferences& userPreferences) {
	// Ambient module
	if (modulespecifiers::PathIsBareSpecifier(e->exportID.ModuleID)) {
		std::string specifier = e->exportID.ModuleID;
		if (modulespecifiers::IsExcludedByRegex(
		        specifier, userPreferences.AutoImportSpecifierExcludeRegexes)) {
			return {"", modulespecifiers::ResultKind::None};
		}
		return {specifier, modulespecifiers::ResultKind::Ambient};
	}

	if (!e->PackageName.empty()) {
		if (auto it = registry->entrypoints.find(e->Path);
		    it != registry->entrypoints.end()) {
			for (const auto& entrypoint : it->second) {
				// Go nil-receiver semantics: nil.IsSubsetOf -> true;
				// nil.Intersects -> false.
				if ((entrypoint->IncludeConditions == nullptr ||
				     entrypoint->IncludeConditions->IsSubsetOf(*conditions)) &&
				    !(conditions != nullptr &&
				      entrypoint->ExcludeConditions != nullptr &&
				      conditions->Intersects(
				          *entrypoint->ExcludeConditions))) {
					std::string specifier =
					    modulespecifiers::ProcessEntrypointEnding(
					        entrypoint.get(), userPreferences, program,
					        program->Options(), importingFile,
					        getAllowedEndings());
					if (!modulespecifiers::IsExcludedByRegex(
					        specifier,
					        userPreferences.AutoImportSpecifierExcludeRegexes)) {
						return {specifier,
						        modulespecifiers::ResultKind::NodeModules};
					}
				}
			}
			return {"", modulespecifiers::ResultKind::None};
		}
	}

	// Go: `v.registry.specifierCache[v.importingFilePath]` — nil *SyncMap on a
	// missing key would panic at Load/Store; `.at` is the faithful equivalent.
	collections::SyncMap<tspath::Path, std::string>* cache =
	    registry->specifierCache.at(importingFilePath).get();
	if (e->PackageName.empty()) {
		if (auto [specifier, ok] = cache->Load(e->Path); ok) {
			if (specifier.empty()) {
				return {"", modulespecifiers::ResultKind::None};
			}
			return {specifier, modulespecifiers::ResultKind::Relative};
		}
	}

	auto [specifiers, kind] =
	    modulespecifiers::GetModuleSpecifiersForFileWithInfo(
	        importingFile, e->ModuleFileName, program->Options(), program,
	        userPreferences, modulespecifiers::ModuleSpecifierOptions{}, true);
	// !!! unsure when this could return multiple specifiers combined with the
	//     new node_modules code. Possibly with local symlinks, which should be
	//     very rare.
	for (const auto& specifier : specifiers) {
		if (specifier.find("/node_modules/") != std::string::npos) {
			continue;
		}
		cache->Store(e->Path, specifier);
		return {specifier, kind};
	}
	cache->Store(e->Path, "");
	return {"", modulespecifiers::ResultKind::None};
}

}  // namespace tsc::ls::autoimport
