// --- module/util.go — tsc/internal/module/util.go

#include "internal/module/util.h"

#include "internal/module/resolver.h"

#include "internal/ast/nodes_generated.h"

namespace tsc::module {

// util.go — IsApplicableVersionedTypesKey.
bool IsApplicableVersionedTypesKey(std::string_view key) {
	if (!key.starts_with("types@")) {
		return false;
	}
	auto [range_, ok] = semver::TryParseVersionRange(key.substr(6));
	if (!ok) {
		return false;
	}
	return range_.Test(&typeScriptVersion());
}


// util.go — ParseNodeModuleFromPath.
std::string ParseNodeModuleFromPath(std::string_view resolved, bool isFolder) {
	std::string path = tspath::normalizePath(resolved);
	auto idx = path.rfind("/node_modules/");
	if (idx == std::string::npos) {
		return "";
	}

	auto indexAfterNodeModules =
	    static_cast<int>(idx + std::string("/node_modules/").size());
	auto indexAfterPackageName = moveToNextDirectorySeparatorIfAvailable(
	    path, indexAfterNodeModules, isFolder);
	if (indexAfterNodeModules < static_cast<int>(path.size()) &&
	    path[indexAfterNodeModules] == '@') {
		indexAfterPackageName = moveToNextDirectorySeparatorIfAvailable(
		    path, indexAfterPackageName, isFolder);
	}
	return path.substr(0, indexAfterPackageName);
}

// util.go — ParsePackageName.
std::pair<std::string_view, std::string_view> ParsePackageName(
    std::string_view moduleName) {
	auto idx = moduleName.find('/');
	if (!moduleName.empty() && moduleName[0] == '@') {
		size_t offset = idx == std::string_view::npos ? 0 : idx + 1;
		auto next = moduleName.find('/', offset);
		if (next != std::string_view::npos) {
			idx = next;
		} else {
			idx = std::string_view::npos;
		}
	}
	if (idx == std::string_view::npos) {
		return {moduleName, ""};
	}
	return {moduleName.substr(0, idx), moduleName.substr(idx + 1)};
}

// util.go — MangleScopedPackageName.
std::string MangleScopedPackageName(std::string_view packageName) {
	if (!packageName.empty() && packageName[0] == '@') {
		auto idx = packageName.find('/');
		if (idx == std::string_view::npos) {
			return std::string{packageName};
		}
		return std::string{packageName.substr(1, idx - 1)} + "__" +
		       std::string{packageName.substr(idx + 1)};
	}
	return std::string{packageName};
}

// util.go — UnmangleScopedPackageName.
std::string UnmangleScopedPackageName(std::string_view packageName) {
	auto idx = packageName.find("__");
	if (idx != std::string_view::npos) {
		return "@" + std::string{packageName.substr(0, idx)} + "/" +
		       std::string{packageName.substr(idx + 2)};
	}
	return std::string{packageName};
}

// util.go — GetTypesPackageName.
std::string GetTypesPackageName(std::string_view packageName) {
	return "@types/" + MangleScopedPackageName(packageName);
}

// util.go — GetPackageNameFromTypesPackageName.
std::string GetPackageNameFromTypesPackageName(
    std::string_view mangledName) {
	if (mangledName.starts_with("@types/")) {
		return UnmangleScopedPackageName(mangledName.substr(7));
	}
	return std::string{mangledName};
}

// util.go — ComparePatternKeys.
int ComparePatternKeys(std::string_view a, std::string_view b) {
	auto aPatternIndex = a.find('*');
	auto bPatternIndex = b.find('*');
	size_t baseLenA = a.size();
	if (aPatternIndex != std::string_view::npos) {
		baseLenA = aPatternIndex + 1;
	}
	size_t baseLenB = b.size();
	if (bPatternIndex != std::string_view::npos) {
		baseLenB = bPatternIndex + 1;
	}

	if (baseLenA > baseLenB) return -1;
	if (baseLenB > baseLenA) return 1;
	if (aPatternIndex == std::string_view::npos) return 1;
	if (bPatternIndex == std::string_view::npos) return -1;
	if (a.size() > b.size()) return -1;
	if (b.size() > a.size()) return 1;
	return 0;
}

// util.go — GetResolutionDiagnostic.
const DiagnosticMessage* GetResolutionDiagnostic(
    const CompilerOptions& options, const ResolvedModule* resolvedModule,
    const SourceFile* file) {
	auto needJsx = [&]() -> const DiagnosticMessage* {
		if (options.Jsx != JsxEmit::None) {
			return nullptr;
		}
		return Module_0_was_resolved_to_1_but_jsx_is_not_set;
	};

	auto needAllowJs = [&]() -> const DiagnosticMessage* {
		if (options.GetAllowJS() ||
		    !options.DefaultIfUnknown(options.NoImplicitAny,
		                              options.Strict)) {
			return nullptr;
		}
		return Could_not_find_a_declaration_file_for_module_0_1_implicitly_has_an_any_type;
	};

	auto needResolveJsonModule = [&]() -> const DiagnosticMessage* {
		if (options.GetResolveJsonModule()) {
			return nullptr;
		}
		return Module_0_was_resolved_to_1_but_resolveJsonModule_is_not_used;
	};

	auto needAllowArbitraryExtensions =
	    [&]() -> const DiagnosticMessage* {
		if (file->IsDeclarationFile ||
		    options.AllowArbitraryExtensions == Tristate::True) {
			return nullptr;
		}
		return Module_0_was_resolved_to_1_but_allowArbitraryExtensions_is_not_set;
	};

	if (resolvedModule->ResolvedUsingExtraExtensions) {
		return nullptr;
	}

	std::string_view ext = resolvedModule->Extension;
	if (ext == tspath::extensionTs || ext == tspath::extensionDts ||
	    ext == tspath::extensionMts || ext == tspath::extensionDmts ||
	    ext == tspath::extensionCts || ext == tspath::extensionDcts) {
		// These are always allowed.
		return nullptr;
	}
	if (ext == tspath::extensionTsx) {
		return needJsx();
	}
	if (ext == tspath::extensionJsx) {
		if (auto message = needJsx(); message != nullptr) {
			return message;
		}
		return needAllowJs();
	}
	if (ext == tspath::extensionJs || ext == tspath::extensionMjs ||
	    ext == tspath::extensionCjs) {
		return needAllowJs();
	}
	if (ext == tspath::extensionJson) {
		return needResolveJsonModule();
	}
	return needAllowArbitraryExtensions();
}

// util.go — TryGetJSExtensionForFile.
std::string_view TryGetJSExtensionForFile(std::string_view fileName,
                                          const CompilerOptions& options) {
	auto ext = tspath::tryGetExtensionFromPath(fileName);
	if (ext == tspath::extensionTs || ext == tspath::extensionDts) {
		return tspath::extensionJs;
	}
	if (ext == tspath::extensionTsx) {
		if (options.Jsx == JsxEmit::Preserve) {
			return tspath::extensionJsx;
		}
		return tspath::extensionJs;
	}
	if (ext == tspath::extensionJs || ext == tspath::extensionJsx ||
	    ext == tspath::extensionJson) {
		return ext;
	}
	if (ext == tspath::extensionDmts || ext == tspath::extensionMts ||
	    ext == tspath::extensionMjs) {
		return tspath::extensionMjs;
	}
	if (ext == tspath::extensionDcts || ext == tspath::extensionCts ||
	    ext == tspath::extensionCjs) {
		return tspath::extensionCjs;
	}
	return "";
}

}  // namespace tsc::module
