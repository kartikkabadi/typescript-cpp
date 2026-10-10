// view.go — View: search/GetCompletions over the registry.
#include <algorithm>

#include "internal/core/nodemodules.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/module/resolver.h"
#include "internal/scanner/scanner.h"

namespace tsc::ls::autoimport {
namespace {

// unicode.IsUpper via the JS casing table (same approach as index.cpp).
bool isUpperRuneView(char32_t r) {
	char32_t up = stringutil::toUpperRune(r);
	char32_t lo = stringutil::toLowerRune(r);
	return up == r && lo != r;
}

// core.FirstNonZero for strings.
std::string firstNonZeroString(const std::string& a, const std::string& b) {
	if (!a.empty()) {
		return a;
	}
	return b;
}

}  // namespace

// NewView — view.go:37
std::unique_ptr<View> NewView(
    Registry* registry, SourceFile* importingFile, ProjectID* projectID,
    compiler::SimpleProgram* program, checker::Checker* typeChecker,
    modulespecifiers::UserPreferences preferences) {
	tspath::Path importingFilePath = importingFile->Path();
	if (SourceFile* canonical = importingFile->CanonicalSourceFile()) {
		importingFilePath = canonical->Path();
	}
	auto v = std::make_unique<View>();
	v->registry = registry;
	v->importingFile = importingFile;
	v->importingFilePath = importingFilePath;
	v->program = program;
	v->checker = typeChecker;
	v->projectID = projectID;
	v->preferences = preferences;
	{
		auto conds = module::GetConditions(
		    *program->Options(),
		    program->GetDefaultResolutionModeForFile(importingFile));
		v->conditions = std::make_unique<collections::Set<std::string>>();
		v->conditions->AddRange(conds);
	}
	v->shouldUseUriStyleNodeCoreModules =
	    lsutil::ShouldUseUriStyleNodeCoreModules(importingFile, program);
	return v;
}

// View::getAllowedEndings — view.go:58
const std::vector<modulespecifiers::ModuleSpecifierEnding>&
View::getAllowedEndings() {
	if (!allowedEndingsComputed) {
		ResolutionMode resolutionMode =
		    program->GetDefaultResolutionModeForFile(importingFile);
		allowedEndings =
		    modulespecifiers::GetAllowedEndingsInPreferredOrder(
		        preferences, program, program->Options(), importingFile, "",
		        resolutionMode);
		allowedEndingsComputed = true;
	}
	return allowedEndings;
}

// View::Search — view.go:81
std::vector<Export*> View::Search(const std::string& query, QueryKind kind) {
	auto searchFn = [query, kind](RegistryBucket* bucket) -> std::vector<Export*> {
		switch (kind) {
			case QueryKindWordPrefix:
				return bucket->index->SearchWordPrefix(query);
			case QueryKindExactMatch:
				return bucket->index->Find(query, true);
			case QueryKindCaseInsensitiveMatch:
				return bucket->index->Find(query, false);
			default:
				TSC_UNREACHABLE("unreachable");
		}
	};
	return search(searchFn);
}

// View::SearchByExportID — view.go:98
std::vector<Export*> View::SearchByExportID(const ExportID& id) {
	auto search = [&id](RegistryBucket* bucket) -> std::vector<Export*> {
		std::vector<Export*> out;
		for (const auto& e : bucket->index->entries) {
			if (e->exportID == id) {
				out.push_back(e.get());
			}
		}
		return out;
	};
	return this->search(search);
}

// View::search — view.go:108
std::vector<Export*> View::search(
    const std::function<std::vector<Export*>(RegistryBucket*)>& searchFn) {
	std::vector<Export*> results;

	if (auto it = registry->projects.find(projectID);
	    it != registry->projects.end()) {
		std::vector<Export*> exports = searchFn(it->second);
		results.reserve(results.size() + exports.size());
		for (Export* e : exports) {
			if (e->exportID.ModuleID == importingFile->Path()) {
				// Don't auto-import from the importing file itself
				continue;
			}
			results.push_back(e);
		}
	}

	// Compute the set of packages accessible to the importing file.
	// If no package.json is found, allowedPackages remains null and all
	// packages are allowed.
	std::unique_ptr<collections::Set<std::string>> allowedPackages;
	tspath::forEachAncestorDirectory<bool>(
	    tspath::getDirectoryPath(importingFile->Path()),
	    [&](std::string_view dirPath) -> std::pair<bool, bool> {
		    auto it = registry->directories.find(tspath::Path(dirPath));
		    if (it != registry->directories.end()) {
			    if (auto& pj = it->second->packageJson;
			        pj != nullptr && pj->Exists() &&
			        pj->GetContents()->Parseable) {
				    // Initialize to empty set if this is the first package.json we've seen
				    if (allowedPackages == nullptr) {
					    allowedPackages = std::make_unique<
					        collections::Set<std::string>>();
				    }
				    addPackageJsonDependencies(*pj->GetContents(),
				                               allowedPackages.get());
			    }
		    }
		    return {false, false};
	    });
	// If we found at least one package.json, also include packages directly
	// imported by the project
	if (allowedPackages != nullptr) {
		if (auto it = registry->projects.find(projectID);
		    it != registry->projects.end()) {
			allowedPackages = std::make_unique<collections::Set<std::string>>(
			    allowedPackages->UnionedWith(*it->second->ResolvedPackageNames));
		}
	}

	collections::Set<std::string> excludePackages;
	tspath::forEachAncestorDirectory<bool>(
	    tspath::getDirectoryPath(importingFile->Path()),
	    [&](std::string_view dirPath) -> std::pair<bool, bool> {
		    auto it = registry->nodeModules.find(tspath::Path(dirPath));
		    if (it != registry->nodeModules.end()) {
			    RegistryBucket* nodeModulesBucket = it->second;
			    std::vector<Export*> exports = searchFn(nodeModulesBucket);
			    results.reserve(results.size() + exports.size());
			    for (Export* e : exports) {
				    // Exclude packages found in lower node_modules (shadowing)
				    if (excludePackages.Has(e->PackageName)) {
					    continue;
				    }
				    // If allowedPackages is nil, no package.json was found, so include all
				    // packages. Otherwise, only include packages that are dependencies or
				    // directly imported.
				    if (allowedPackages != nullptr &&
				        !allowedPackages->Has(e->PackageName)) {
					    continue;
				    }
				    results.push_back(e);
			    }

			    // As we go up the directory tree, exclude packages found in
			    // lower node_modules
			    for (const auto& kv : nodeModulesBucket->PackageFiles) {
				    excludePackages.Add(kv.first);
			    }
		    }
		    return {false, false};
	    });
	return results;
}

// View::GetCompletions — view.go:180
std::vector<std::unique_ptr<FixAndExport>> View::GetCompletions(
    const std::string& prefix, const lsp::lsproto::Position& position, bool forJSX,
    bool isTypeOnlyLocation) {
	std::vector<Export*> results = Search(prefix, QueryKindWordPrefix);

	struct exportGroupKey {
		ExportID target;
		std::string name;
		std::string ambientModuleOrPackageName;
		bool operator==(const exportGroupKey&) const = default;
	};
	struct exportGroupKeyHash {
		size_t operator()(const exportGroupKey& k) const {
			return std::hash<std::string>{}(k.target.ModuleID) ^
			       (std::hash<std::string>{}(k.target.ExportName) << 1) ^
			       (std::hash<std::string>{}(k.name) << 2) ^
			       (std::hash<std::string>{}(k.ambientModuleOrPackageName) << 3);
		}
	};
	std::unordered_map<exportGroupKey, std::vector<Export*>,
	                   exportGroupKeyHash>
	    grouped;

	for (Export* e : results) {
		std::string name = e->Name();
		if (!isIdentifierText(name, LanguageVariant::Standard)) {
			continue;
		}
		if (forJSX) {
			char32_t first = static_cast<unsigned char>(name[0]);
			if (!isUpperRuneView(first) && !e->IsRenameable()) {
				continue;
			}
		}
		ExportID target = e->exportID;
		if (e->Target != ExportID{}) {
			target = e->Target;
		}
		exportGroupKey key{
		    target,
		    name,
		    firstNonZeroString(e->AmbientModuleName(), e->PackageName),
		};
		if (e->PackageName == "@types/node" ||
		    std::string(e->Path).find("/node_modules/@types/node/") !=
		        std::string::npos) {
			if (UnprefixedNodeCoreModules.count(
			        key.ambientModuleOrPackageName)) {
				// Group URI-style and non-URI style node core modules together
				key.ambientModuleOrPackageName =
				    "node:" + key.ambientModuleOrPackageName;
			}
		}
		bool merged = false;
		if (auto git = grouped.find(key); git != grouped.end()) {
			std::vector<Export*>& existing = git->second;
			for (size_t i = 0; i < existing.size(); i++) {
				Export* ex = existing[i];
				if (e->exportID == ex->exportID) {
					// slices.Replace(existing, i, i+1, &Export{...})
					auto merged_ = std::make_shared<Export>();
					merged_->exportID = e->exportID;
					merged_->ModuleFileName = e->ModuleFileName;
					merged_->UnresolvedModuleSpecifier =
					    e->UnresolvedModuleSpecifier;
					merged_->PackageName = e->PackageName;
					merged_->IsTypeOnly = e->IsTypeOnly || ex->IsTypeOnly;
					merged_->Syntax =
					    static_cast<ExportSyntax>(std::min(
					        static_cast<int>(e->Syntax),
					        static_cast<int>(ex->Syntax)));
					merged_->Flags = e->Flags | ex->Flags;
					merged_->ScriptElementKind = std::min(
					    e->ScriptElementKind, ex->ScriptElementKind);
					merged_->ScriptElementKindModifiers =
					    e->ScriptElementKindModifiers |
					    ex->ScriptElementKindModifiers;
					merged_->localName = e->localName;
					merged_->Target = e->Target;
					merged_->Path = e->Path;
					// The merged record is owned by the View's group pass;
					// keep it alive via the group's backing storage.
					existing[i] = merged_.get();
					ownedMerges.push_back(std::move(merged_));
					merged = true;
					break;
				}
			}
		}
		if (!merged) {
			grouped[key].push_back(e);
		}
	}

	std::vector<std::unique_ptr<FixAndExport>> fixes;
	fixes.reserve(results.size());
	auto compareFixes = [this](FixAndExport* a, FixAndExport* b) -> int {
		return CompareFixesForRanking(a->Fix.get(), b->Fix.get());
	};

	for (auto& [k, exps] : grouped) {
		std::vector<std::unique_ptr<FixAndExport>> fixesForGroup;
		fixesForGroup.reserve(exps.size());
		for (Export* e : exps) {
			for (auto& fix : GetFixes(e, forJSX, isTypeOnlyLocation, &position)) {
				auto fe = std::make_unique<FixAndExport>();
				fe->Fix = std::move(fix);
				fe->Export = e;
				fixesForGroup.push_back(std::move(fe));
			}
		}
		// core.MinAllFunc — all minimum elements under CompareFixesForRanking.
		if (!fixesForGroup.empty()) {
			size_t mIdx = 0;
			std::vector<size_t> mins{0};
			for (size_t i = 1; i < fixesForGroup.size(); i++) {
				int c = compareFixes(fixesForGroup[i].get(),
				                     fixesForGroup[mIdx].get());
				if (c < 0) {
					mIdx = i;
					mins.clear();
					mins.push_back(i);
				} else if (c == 0) {
					mins.push_back(i);
				}
			}
			for (size_t i : mins) {
				fixes.push_back(std::move(fixesForGroup[i]));
			}
		}
	}

	// The client does additional sorting by SortText and Label; we only need a
	// stable relative ordering between completions the client considers
	// equivalent.
	std::stable_sort(fixes.begin(), fixes.end(),
	          [this](const std::unique_ptr<FixAndExport>& a,
	                 const std::unique_ptr<FixAndExport>& b) {
		          return CompareFixesForSorting(a->Fix.get(), b->Fix.get()) < 0;
	          });

	return fixes;
}

}  // namespace tsc::ls::autoimport
