#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/parser/parser.h" // getErrorSpanForNode
#include "internal/tspath/tspath.h"

namespace tsc::tsoptions {

// --- Lib tables (enummaps.go: LibMap/Libs/LibFilesSet/targetToLibMap) ---

// LibMap — insertion order preserved (Libs is the ordered key list, NOT sorted).
inline const std::vector<std::pair<std::string_view, std::string_view>> libMapEntries = {
	// JavaScript only
	{"es5", "lib.es5.d.ts"},
	{"es6", "lib.es2015.d.ts"},
	{"es2015", "lib.es2015.d.ts"},
	{"es7", "lib.es2016.d.ts"},
	{"es2016", "lib.es2016.d.ts"},
	{"es2017", "lib.es2017.d.ts"},
	{"es2018", "lib.es2018.d.ts"},
	{"es2019", "lib.es2019.d.ts"},
	{"es2020", "lib.es2020.d.ts"},
	{"es2021", "lib.es2021.d.ts"},
	{"es2022", "lib.es2022.d.ts"},
	{"es2023", "lib.es2023.d.ts"},
	{"es2024", "lib.es2024.d.ts"},
	{"es2025", "lib.es2025.d.ts"},
	{"es2026", "lib.es2026.d.ts"},
	{"esnext", "lib.esnext.d.ts"},
	// Host only
	{"dom", "lib.dom.d.ts"},
	{"dom.iterable", "lib.dom.iterable.d.ts"},
	{"dom.asynciterable", "lib.dom.asynciterable.d.ts"},
	{"webworker", "lib.webworker.d.ts"},
	{"webworker.importscripts", "lib.webworker.importscripts.d.ts"},
	{"webworker.iterable", "lib.webworker.iterable.d.ts"},
	{"webworker.asynciterable", "lib.webworker.asynciterable.d.ts"},
	{"scripthost", "lib.scripthost.d.ts"},
	// ES2015 and later By-feature options
	{"es2015.core", "lib.es2015.core.d.ts"},
	{"es2015.collection", "lib.es2015.collection.d.ts"},
	{"es2015.generator", "lib.es2015.generator.d.ts"},
	{"es2015.iterable", "lib.es2015.iterable.d.ts"},
	{"es2015.promise", "lib.es2015.promise.d.ts"},
	{"es2015.proxy", "lib.es2015.proxy.d.ts"},
	{"es2015.reflect", "lib.es2015.reflect.d.ts"},
	{"es2015.symbol", "lib.es2015.symbol.d.ts"},
	{"es2015.symbol.wellknown", "lib.es2015.symbol.wellknown.d.ts"},
	{"es2016.array.include", "lib.es2016.array.include.d.ts"},
	{"es2016.intl", "lib.es2016.intl.d.ts"},
	{"es2017.arraybuffer", "lib.es2017.arraybuffer.d.ts"},
	{"es2017.date", "lib.es2017.date.d.ts"},
	{"es2017.object", "lib.es2017.object.d.ts"},
	{"es2017.sharedmemory", "lib.es2017.sharedmemory.d.ts"},
	{"es2017.string", "lib.es2017.string.d.ts"},
	{"es2017.intl", "lib.es2017.intl.d.ts"},
	{"es2017.typedarrays", "lib.es2017.typedarrays.d.ts"},
	{"es2018.asyncgenerator", "lib.es2018.asyncgenerator.d.ts"},
	{"es2018.asynciterable", "lib.es2018.asynciterable.d.ts"},
	{"es2018.intl", "lib.es2018.intl.d.ts"},
	{"es2018.promise", "lib.es2018.promise.d.ts"},
	{"es2018.regexp", "lib.es2018.regexp.d.ts"},
	{"es2019.array", "lib.es2019.array.d.ts"},
	{"es2019.object", "lib.es2019.object.d.ts"},
	{"es2019.string", "lib.es2019.string.d.ts"},
	{"es2019.symbol", "lib.es2019.symbol.d.ts"},
	{"es2019.intl", "lib.es2019.intl.d.ts"},
	{"es2020.bigint", "lib.es2020.bigint.d.ts"},
	{"es2020.date", "lib.es2020.date.d.ts"},
	{"es2020.promise", "lib.es2020.promise.d.ts"},
	{"es2020.sharedmemory", "lib.es2020.sharedmemory.d.ts"},
	{"es2020.string", "lib.es2020.string.d.ts"},
	{"es2020.symbol.wellknown", "lib.es2020.symbol.wellknown.d.ts"},
	{"es2020.intl", "lib.es2020.intl.d.ts"},
	{"es2020.number", "lib.es2020.number.d.ts"},
	{"es2021.promise", "lib.es2021.promise.d.ts"},
	{"es2021.string", "lib.es2021.string.d.ts"},
	{"es2021.weakref", "lib.es2021.weakref.d.ts"},
	{"es2021.intl", "lib.es2021.intl.d.ts"},
	{"es2022.array", "lib.es2022.array.d.ts"},
	{"es2022.error", "lib.es2022.error.d.ts"},
	{"es2022.intl", "lib.es2022.intl.d.ts"},
	{"es2022.object", "lib.es2022.object.d.ts"},
	{"es2022.string", "lib.es2022.string.d.ts"},
	{"es2022.regexp", "lib.es2022.regexp.d.ts"},
	{"es2023.array", "lib.es2023.array.d.ts"},
	{"es2023.collection", "lib.es2023.collection.d.ts"},
	{"es2023.intl", "lib.es2023.intl.d.ts"},
	{"es2024.arraybuffer", "lib.es2024.arraybuffer.d.ts"},
	{"es2024.collection", "lib.es2024.collection.d.ts"},
	{"es2024.object", "lib.es2024.object.d.ts"},
	{"es2024.promise", "lib.es2024.promise.d.ts"},
	{"es2024.regexp", "lib.es2024.regexp.d.ts"},
	{"es2024.sharedmemory", "lib.es2024.sharedmemory.d.ts"},
	{"es2024.string", "lib.es2024.string.d.ts"},
	{"es2025.collection", "lib.es2025.collection.d.ts"},
	{"es2025.float16", "lib.es2025.float16.d.ts"},
	{"es2025.intl", "lib.es2025.intl.d.ts"},
	{"es2025.iterator", "lib.es2025.iterator.d.ts"},
	{"es2025.promise", "lib.es2025.promise.d.ts"},
	{"es2025.regexp", "lib.es2025.regexp.d.ts"},
	{"es2026.array", "lib.es2026.array.d.ts"},
	{"es2026.collection", "lib.es2026.collection.d.ts"},
	{"es2026.error", "lib.es2026.error.d.ts"},
	{"es2026.iterator", "lib.es2026.iterator.d.ts"},
	{"es2026.json", "lib.es2026.json.d.ts"},
	{"es2026.math", "lib.es2026.math.d.ts"},
	{"es2026.typedarrays", "lib.es2026.typedarrays.d.ts"},
	// Fallback for backward compatibility
	{"esnext.asynciterable", "lib.es2018.asynciterable.d.ts"},
	{"esnext.symbol", "lib.es2019.symbol.d.ts"},
	{"esnext.bigint", "lib.es2020.bigint.d.ts"},
	{"esnext.weakref", "lib.es2021.weakref.d.ts"},
	{"esnext.object", "lib.es2024.object.d.ts"},
	{"esnext.regexp", "lib.es2024.regexp.d.ts"},
	{"esnext.string", "lib.es2024.string.d.ts"},
	{"esnext.float16", "lib.es2025.float16.d.ts"},
	{"esnext.promise", "lib.es2025.promise.d.ts"},
	{"esnext.array", "lib.es2026.array.d.ts"},
	{"esnext.collection", "lib.es2026.collection.d.ts"},
	{"esnext.error", "lib.es2026.error.d.ts"},
	{"esnext.iterator", "lib.es2026.iterator.d.ts"},
	{"esnext.typedarrays", "lib.es2026.typedarrays.d.ts"},
	// ESNext By-feature options
	{"esnext.date", "lib.esnext.date.d.ts"},
	{"esnext.decorators", "lib.esnext.decorators.d.ts"},
	{"esnext.disposable", "lib.esnext.disposable.d.ts"},
	{"esnext.intl", "lib.esnext.intl.d.ts"},
	{"esnext.sharedmemory", "lib.esnext.sharedmemory.d.ts"},
	{"esnext.temporal", "lib.esnext.temporal.d.ts"},
	// Decorators
	{"decorators", "lib.decorators.d.ts"},
	{"decorators.legacy", "lib.decorators.legacy.d.ts"},
};

inline const std::vector<std::string_view> Libs = [] {
	std::vector<std::string_view> v;
	v.reserve(libMapEntries.size());
	for (const auto& e : libMapEntries) v.push_back(e.first);
	return v;
}();

inline const std::unordered_map<std::string_view, std::string_view> LibMap =
    [] {
	    std::unordered_map<std::string_view, std::string_view> m;
	    for (const auto& e : libMapEntries) m.emplace(e.first, e.second);
	    return m;
    }();

inline const std::unordered_set<std::string_view> LibFilesSet = [] {
	std::unordered_set<std::string_view> s;
	for (const auto& e : libMapEntries) s.insert(e.second);
	return s;
}();

// enummaps.go: GetLibFileName — checks if libName is a valid lib name or
// file name and converts it to the filename if needed.
inline std::pair<std::string_view, bool> getLibFileName(std::string_view libName) {
	std::string lowered = tspath::toFileNameLowerCase(libName);
	if (LibFilesSet.count(lowered)) {
		// Return a view into the interned string set for storage stability.
		return {*LibFilesSet.find(lowered), true};
	}
	auto it = LibMap.find(lowered);
	if (it == LibMap.end()) {
		return {"", false};
	}
	return {it->second, true};
}

// enummaps.go: targetToLibMap
inline const std::unordered_map<ScriptTarget, std::string_view> targetToLibMap = {
	{ScriptTarget::ESNext, "lib.esnext.full.d.ts"},
	{ScriptTarget::ES2026, "lib.es2026.full.d.ts"},
	{ScriptTarget::ES2025, "lib.es2025.full.d.ts"},
	{ScriptTarget::ES2024, "lib.es2024.full.d.ts"},
	{ScriptTarget::ES2023, "lib.es2023.full.d.ts"},
	{ScriptTarget::ES2022, "lib.es2022.full.d.ts"},
	{ScriptTarget::ES2021, "lib.es2021.full.d.ts"},
	{ScriptTarget::ES2020, "lib.es2020.full.d.ts"},
	{ScriptTarget::ES2019, "lib.es2019.full.d.ts"},
	{ScriptTarget::ES2018, "lib.es2018.full.d.ts"},
	{ScriptTarget::ES2017, "lib.es2017.full.d.ts"},
	{ScriptTarget::ES2016, "lib.es2016.full.d.ts"},
	{ScriptTarget::ES2015, "lib.es6.d.ts"}, // We don't use lib.es2015.full.d.ts due to breaking change.
};

// enummaps.go: GetDefaultLibFileName
inline std::string_view getDefaultLibFileName(const CompilerOptions* options) {
	auto it = targetToLibMap.find(options->GetEmitScriptTarget());
	if (it == targetToLibMap.end()) {
		return "lib.d.ts";
	}
	return it->second;
}

// --- CommandLineProgram file spec helpers (commandLineProgram.go) ---

// GetSupportedExtensions — tsconfigparsing.go:2026.
inline std::vector<std::vector<std::string_view>> getSupportedExtensions(
    const CompilerOptions* options,
    const std::vector<std::string_view>& extraFileExtensions) {
	bool needJSExtensions = options->GetAllowJS();
	const auto& builtins = needJSExtensions
	                           ? tspath::allSupportedExtensions
	                           : tspath::supportedTSExtensions;
	if (extraFileExtensions.empty()) {
		return builtins;
	}
	// flatBuiltins membership check
	std::unordered_set<std::string_view> flatBuiltins;
	for (const auto& group : builtins)
		for (auto ext : group) flatBuiltins.insert(ext);
	std::vector<std::vector<std::string_view>> extra;
	for (auto ext : extraFileExtensions) {
		if (!flatBuiltins.count(ext)) {
			extra.push_back({ext});
		}
	}
	if (extra.empty()) {
		return builtins;
	}
	auto result = builtins;
	for (auto& g : extra) result.push_back(g);
	return result;
}

inline std::vector<std::vector<std::string_view>>
getSupportedExtensionsWithJsonIfResolveJsonModule(
    const CompilerOptions* options,
    const std::vector<std::vector<std::string_view>>& supportedExtensions) {
	auto extensions = supportedExtensions;
	if (options->GetResolveJsonModule()) {
		extensions.push_back({tspath::extensionJson});
	}
	return extensions;
}

inline std::vector<std::string_view> supportedExtensionsFlat(
    const std::vector<std::vector<std::string_view>>& extensions) {
	std::vector<std::string_view> flat;
	for (const auto& group : extensions) {
		for (auto ext : group) flat.push_back(ext);
	}
	return flat;
}

// --- errors.go: CreateDiagnosticForNodeInSourceFile ---

// NewCompilerDiagnostic equivalent — a file-less diagnostic.
inline Diagnostic* newCompilerDiagnostic(const DiagnosticMessage* message,
                                         const std::vector<std::string>& args = {}) {
	return newDiagnostic(nullptr, TextRange::undefined(), message, args);
}

inline Diagnostic* createDiagnosticForNodeInSourceFile(SourceFile* sourceFile,
                                                     Node* node,
                                                     const DiagnosticMessage* message,
                                                     std::vector<std::string> args = {}) {
	TextRange span = getErrorSpanForNode(sourceFile->text, node);
	return newDiagnostic(sourceFile, span, message, args);
}

inline Diagnostic* createDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
    SourceFile* sourceFile, Node* node, const DiagnosticMessage* message,
    std::vector<std::string> args = {}) {
	if (node != nullptr) {
		return createDiagnosticForNodeInSourceFile(sourceFile, node, message, args);
	}
	return newCompilerDiagnostic(message, args);
}

}  // namespace tsc::tsoptions
