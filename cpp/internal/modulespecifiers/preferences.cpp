// Port of tsc/internal/modulespecifiers/preferences.go — ending-preference
// inference for generated module specifiers.
// Host type (ModuleSpecifierGenerationHost) maps to checker::Program;
// SourceFileForSpecifierGeneration maps to SourceFile.

#include "internal/modulespecifiers/types.h"

#include "internal/checker/checker.h"
#include "internal/tspath/tspath.h"

#include <string>
#include <vector>

namespace tsc::modulespecifiers {

namespace {

// preferences.go:14 — shouldAllowImportingTsExtension. Program errors
// validate that `noEmit` or `emitDeclarationOnly` is also set, so this
// function doesn't check them to avoid propagating errors.
bool shouldAllowImportingTsExtension(const CompilerOptions* compilerOptions,
                                     const std::string& fromFileName) {
	return compilerOptions->GetAllowImportingTsExtensions() ||
	       (!fromFileName.empty() &&
	        tspath::isDeclarationFileName(fromFileName));
}

// preferences.go:18 — usesExtensionsOnImports
bool usesExtensionsOnImports(SourceFile* file) {
	for (auto* ref : file->imports) {
		auto text = ref->text();
		if (tspath::pathIsRelative(text) &&
		    !tspath::fileExtensionIsOneOf(
		        text, tspath::extensionsNotSupportingExtensionlessResolution)) {
			return tspath::hasTSFileExtension(text) ||
			       tspath::hasJSFileExtension(text);
		}
	}
	return false;
}

// preferences.go:28 — inferPreference
ModuleSpecifierEnding inferPreference(ResolutionMode resolutionMode,
                                      SourceFile* sourceFile,
                                      bool moduleResolutionIsNodeNext) {
	bool usesJsExtensions = false;
	std::vector<Node*> specifiers;
	if (sourceFile != nullptr && !sourceFile->imports.empty()) {
		specifiers = sourceFile->imports;
	} else if (sourceFile != nullptr && isSourceFileJS(sourceFile)) {
		// !!! TODO: JS support
		// specifiers = core.Map(getRequiresAtTopOfFile(sourceFile), func(d *ast.Node) *ast.Node { return d.arguments[0] })
	}

	for (auto* specifier : specifiers) {
		auto path = specifier->text();
		if (tspath::pathIsRelative(path)) {
			// !!! TODO: proper resolutionMode support
			if (moduleResolutionIsNodeNext &&
			    resolutionMode == ResolutionModeCommonJS /* && getModeForUsageLocation(sourceFile!, specifier, compilerOptions) === ModuleKind.ESNext */) {
				// We're trying to decide a preference for a CommonJS module
				// specifier, but looking at an ESM import.
				continue;
			}
			if (tspath::fileExtensionIsOneOf(
			        path, tspath::extensionsNotSupportingExtensionlessResolution)) {
				// These extensions are not optional, so do not indicate a
				// preference.
				continue;
			}
			if (tspath::hasTSFileExtension(path)) {
				return ModuleSpecifierEnding::TsExtension;
			}
			if (tspath::hasJSFileExtension(path)) {
				usesJsExtensions = true;
			}
		}
	}

	if (usesJsExtensions) {
		return ModuleSpecifierEnding::JsExtension;
	}
	return ModuleSpecifierEnding::Minimal;
}

// preferences.go:69 — getModuleSpecifierEndingPreference
ModuleSpecifierEnding getModuleSpecifierEndingPreference(
    const ImportModuleSpecifierEndingPreference& pref,
    ResolutionMode resolutionMode, const CompilerOptions* compilerOptions,
    SourceFile* sourceFile) {
	auto moduleResolution = compilerOptions->GetModuleResolutionKind();
	bool moduleResolutionIsNodeNext =
	    ModuleResolutionKind::Node16 <= moduleResolution &&
	    moduleResolution <= ModuleResolutionKind::NodeNext;

	if (pref == ImportModuleSpecifierEndingPreferenceJs ||
	    (resolutionMode == ResolutionModeESM && moduleResolutionIsNodeNext)) {
		// Extensions are explicitly requested or required. Now choose
		// between .js and .ts.
		if (!shouldAllowImportingTsExtension(compilerOptions, "")) {
			return ModuleSpecifierEnding::JsExtension;
		}
		// `allowImportingTsExtensions` is a strong signal, so use .ts unless
		// the file already uses .js extensions and no .ts extensions.
		if (inferPreference(resolutionMode, sourceFile,
		                    moduleResolutionIsNodeNext) !=
		    ModuleSpecifierEnding::JsExtension) {
			return ModuleSpecifierEnding::TsExtension;
		}
		return ModuleSpecifierEnding::JsExtension;
	}

	if (pref == ImportModuleSpecifierEndingPreferenceMinimal) {
		return ModuleSpecifierEnding::Minimal;
	}

	if (pref == ImportModuleSpecifierEndingPreferenceIndex) {
		return ModuleSpecifierEnding::Index;
	}

	// No preference was specified.
	// Look at imports and/or requires to guess whether .js, .ts, or
	// extensionless imports are preferred.
	// N.B. that `Index` detection is not supported since it would require
	// file system probing to do accurately, and more importantly, literally
	// nobody wants `Index` and its existence is a mystery.
	if (!shouldAllowImportingTsExtension(compilerOptions, "")) {
		// If .ts imports are not valid, we only need to see one .js import
		// to go with that.
		if (sourceFile != nullptr && usesExtensionsOnImports(sourceFile)) {
			return ModuleSpecifierEnding::JsExtension;
		}
		return ModuleSpecifierEnding::Minimal;
	}

	return inferPreference(resolutionMode, sourceFile,
	                       moduleResolutionIsNodeNext);
}

// preferences.go:114 — getPreferredEnding
ModuleSpecifierEnding getPreferredEnding(
    const UserPreferences& prefs, checker::Program* host,
    const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
    const std::string& oldImportSpecifier, ResolutionMode resolutionMode) {
	if (!oldImportSpecifier.empty()) {
		if (tspath::hasJSFileExtension(oldImportSpecifier)) {
			return ModuleSpecifierEnding::JsExtension;
		}
		if (oldImportSpecifier.size() >= 6 &&
		    oldImportSpecifier.compare(oldImportSpecifier.size() - 6, 6,
		                               "/index") == 0) {
			return ModuleSpecifierEnding::Index;
		}
	}
	if (resolutionMode == ResolutionModeNone) {
		resolutionMode = host->GetDefaultResolutionModeForFile(importingSourceFile);
	}
	return getModuleSpecifierEndingPreference(prefs.ImportModuleSpecifierEnding,
	                                          resolutionMode, compilerOptions,
	                                          importingSourceFile);
}

} // namespace

// preferences.go:147 — GetAllowedEndingsInPreferredOrder
std::vector<ModuleSpecifierEnding> GetAllowedEndingsInPreferredOrder(
    const UserPreferences& prefs, checker::Program* host,
    const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
    const std::string& oldImportSpecifier,
    ResolutionMode syntaxImpliedNodeFormat) {
	auto preferredEnding =
	    getPreferredEnding(prefs, host, compilerOptions, importingSourceFile,
	                       oldImportSpecifier, ResolutionModeNone);
	auto resolutionMode =
	    host->GetDefaultResolutionModeForFile(importingSourceFile);
	if (resolutionMode != syntaxImpliedNodeFormat) {
		preferredEnding =
		    getPreferredEnding(prefs, host, compilerOptions,
		                       importingSourceFile, oldImportSpecifier,
		                       syntaxImpliedNodeFormat);
	}
	auto moduleResolution = compilerOptions->GetModuleResolutionKind();
	bool moduleResolutionIsNodeNext =
	    ModuleResolutionKind::Node16 <= moduleResolution &&
	    moduleResolution <= ModuleResolutionKind::NodeNext;
	bool allowImportingTsExtension = shouldAllowImportingTsExtension(
	    compilerOptions, importingSourceFile->FileName());
	// TypeScript uses `(syntaxImpliedNodeFormat ?? impliedNodeFormat)` here -
	// fall back to the file's default resolution mode when no syntax-implied
	// mode is given.
	auto effectiveSyntaxMode = syntaxImpliedNodeFormat;
	if (effectiveSyntaxMode == ResolutionModeNone) {
		effectiveSyntaxMode = resolutionMode;
	}
	if (effectiveSyntaxMode == ResolutionModeESM && moduleResolutionIsNodeNext) {
		if (allowImportingTsExtension) {
			return {ModuleSpecifierEnding::TsExtension,
			        ModuleSpecifierEnding::JsExtension};
		}
		return {ModuleSpecifierEnding::JsExtension};
	}
	switch (preferredEnding) {
	case ModuleSpecifierEnding::JsExtension:
		if (allowImportingTsExtension) {
			return {ModuleSpecifierEnding::JsExtension,
			        ModuleSpecifierEnding::TsExtension,
			        ModuleSpecifierEnding::Minimal,
			        ModuleSpecifierEnding::Index};
		}
		return {ModuleSpecifierEnding::JsExtension,
		        ModuleSpecifierEnding::Minimal, ModuleSpecifierEnding::Index};
	case ModuleSpecifierEnding::TsExtension:
		return {ModuleSpecifierEnding::TsExtension,
		        ModuleSpecifierEnding::Minimal,
		        ModuleSpecifierEnding::JsExtension,
		        ModuleSpecifierEnding::Index};
	case ModuleSpecifierEnding::Index:
		if (allowImportingTsExtension) {
			return {ModuleSpecifierEnding::Index,
			        ModuleSpecifierEnding::Minimal,
			        ModuleSpecifierEnding::TsExtension,
			        ModuleSpecifierEnding::JsExtension};
		}
		return {ModuleSpecifierEnding::Index, ModuleSpecifierEnding::Minimal,
		        ModuleSpecifierEnding::JsExtension};
	case ModuleSpecifierEnding::Minimal:
		if (allowImportingTsExtension) {
			return {ModuleSpecifierEnding::Minimal,
			        ModuleSpecifierEnding::Index,
			        ModuleSpecifierEnding::TsExtension,
			        ModuleSpecifierEnding::JsExtension};
		}
		return {ModuleSpecifierEnding::Minimal, ModuleSpecifierEnding::Index,
		        ModuleSpecifierEnding::JsExtension};
	default:
		TSC_UNREACHABLE("AssertNever");
	}
	return {ModuleSpecifierEnding::Minimal};
}

// preferences.go:213 — getModuleSpecifierPreferences
ModuleSpecifierPreferences getModuleSpecifierPreferences(
    const UserPreferences& prefs, checker::Program* host,
    const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
    const std::string& oldImportSpecifier) {
	auto excludes = prefs.AutoImportSpecifierExcludeRegexes;
	auto relativePreference = RelativePreferenceKind::Shortest;
	if (!oldImportSpecifier.empty()) {
		if (tspath::isExternalModuleNameRelative(oldImportSpecifier)) {
			relativePreference = RelativePreferenceKind::Relative;
		} else {
			relativePreference = RelativePreferenceKind::NonRelative;
		}
	} else {
		if (prefs.ImportModuleSpecifierPreference ==
		    ImportModuleSpecifierPreferenceRelative) {
			relativePreference = RelativePreferenceKind::Relative;
		} else if (prefs.ImportModuleSpecifierPreference ==
		           ImportModuleSpecifierPreferenceNonRelative) {
			relativePreference = RelativePreferenceKind::NonRelative;
		} else if (prefs.ImportModuleSpecifierPreference ==
		           ImportModuleSpecifierPreferenceProjectRelative) {
			relativePreference = RelativePreferenceKind::ExternalNonRelative;
		}
		// all others are shortest
	}

	return ModuleSpecifierPreferences{
	    relativePreference,
	    [=](ResolutionMode syntaxImpliedNodeFormat) {
		    return GetAllowedEndingsInPreferredOrder(
		        prefs, host, compilerOptions, importingSourceFile,
		        oldImportSpecifier, syntaxImpliedNodeFormat);
	    },
	    excludes,
	};
}

} // namespace tsc::modulespecifiers
