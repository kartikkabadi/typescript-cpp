// Port of tsc/internal/tspath typed path layer (rooted_path.go, pathkey.go,
// relative_path.go, module_specifier.go, and path.go's CaseSensitivity).
// ed480721: "Strongly type file paths" — Path removed; RootedPath /
// RootedFilePath / RootedDirectoryPath / FileNameStem / PathKey /
// RelativePath / ModuleSpecifier / CaseSensitivity replace it.
#pragma once

#include "internal/tspath/tspath.h"

#include <functional>
#include <unordered_set>
#include <utility>

namespace tsc::tspath {

// Forward-declare the typed wrappers; definitions follow below.
struct RootedPath;
struct RootedFilePath;
struct RootedDirectoryPath;
struct FileNameStem;
struct PathKey;
struct RelativePath;
struct ModuleSpecifier;

// path.go — CaseSensitivity controls canonical path identity and comparison.
// Its zero value is CaseInsensitive.
struct CaseSensitivity {
	uint8_t value = 0;

	static constexpr uint8_t CaseInsensitiveV = 0;
	static constexpr uint8_t CaseSensitiveV = 1;

	static CaseSensitivity CaseInsensitive() { return {0}; }
	static CaseSensitivity CaseSensitive() { return {1}; }

	constexpr bool operator==(CaseSensitivity o) const { return value == o.value; }
	constexpr bool operator!=(CaseSensitivity o) const { return value != o.value; }

	bool isCaseSensitive() const { return value == CaseSensitiveV; }
	bool isCaseInsensitive() const { return value == CaseInsensitiveV; }

	// Canonicalize applies c to text used for path comparison or keys.
	std::string canonicalize(std::string_view text) const {
		if (isCaseSensitive()) return std::string(text);
		return toFileNameLowerCase(text);
	}

	// TrimPrefix removes prefix from text according to c. It returns text
	// unchanged and false when the prefix does not match.
	// This must not slice text using prefix.size(): canonicalization can
	// change a string's UTF-8 byte length without changing its rune count.
	std::pair<std::string, bool> trimPrefix(std::string_view text,
	                                        std::string_view prefix) const {
		if (isCaseSensitive()) {
			if (text.starts_with(prefix)) {
				return {std::string(text.substr(prefix.size())), true};
			}
			return {std::string(text), false};
		}
		auto canonicalPrefix = canonicalize(prefix);
		if (!canonicalize(text).starts_with(canonicalPrefix)) {
			return {std::string(text), false};
		}
		int runeCount = 0;
		for (size_t i = 0; i < canonicalPrefix.size();) {
			int width = 0;
			decodeUtf8Rune(std::string_view(canonicalPrefix).substr(i),
			               &width);
			i += width;
			runeCount++;
		}
		return {tspath::trimRuneCount(text, runeCount), true};
	}

	std::string String() const {
		switch (value) {
		case CaseInsensitiveV: return "false";
		case CaseSensitiveV: return "true";
		default: return "unknown";
		}
	}

	std::function<int(std::string_view, std::string_view)> getComparer()
	    const {
		return stringutil::GetStringComparer(!isCaseSensitive());
	}

	std::function<bool(std::string_view, std::string_view)>
	getEqualityComparer() const {
		return stringutil::GetStringEqualityComparer(!isCaseSensitive());
	}

	// --- methods declared below (defined after the types) ---
	PathKey pathKey(RootedPath path) const;
	int compareFilePaths(RootedFilePath a, RootedFilePath b) const;
	int comparePaths(RootedPath a, RootedPath b) const;
	int compareFileNameStems(FileNameStem a, FileNameStem b) const;
	bool containsFilePath(RootedDirectoryPath parent,
	                      RootedFilePath child) const;
	bool containsPath(RootedDirectoryPath parent, RootedPath child) const;
	bool startsWithDirectory(RootedFilePath fileName,
	                         RootedDirectoryPath directory) const;
	std::pair<RelativePath, bool> relativeFilePathFromDirectory(
		RootedDirectoryPath directory, RootedFilePath fileName) const;
	std::pair<RelativePath, bool> relativePathWithinDirectory(
		RootedDirectoryPath directory, RootedPath path) const;
	std::pair<std::string, bool> trimContainedPath(std::string_view parent,
	                                             std::string_view child) const;
	RootedDirectoryPath commonDirectoryOfFiles(
		const std::vector<RootedFilePath>& fileNames) const;
	std::tuple<RootedDirectoryPath, RootedDirectoryPath, bool>
	splitFilePathAtComponent(RootedFilePath fileName,
	                         std::string_view component) const;
	RelativePath canonicalRelativePath(RelativePath path) const;
	std::pair<RelativePath, bool> relativePathFromDirectory(
		RootedDirectoryPath directory, RootedFilePath fileName) const;
	std::pair<RelativePath, bool> relativePathFromPath(
		RootedDirectoryPath directory, RootedPath rootedPath) const;
	std::pair<RelativePath, bool> relativePathFromFile(
		RootedFilePath from, RootedFilePath to) const;
	std::pair<RelativePath, bool> relativePathFromFileToPath(
		RootedFilePath from, RootedPath to) const;
	RelativePath relativePathFromRelativeDirectory(
		RelativePath directory, RelativePath fileName) const;

private:
	int compareRootedText(std::string_view aString,
	                      std::string_view bString) const;
};

// rooted_path.go — RootedPath: a rooted, normalized path (invariant only).
struct RootedPath : std::string {
	using std::string::string;
	RootedPath() = default;
	explicit RootedPath(const std::string& s) : std::string(s) {}
	explicit RootedPath(std::string_view s) : std::string(s) {}

	std::string_view asStringView() const { return *this; }
	std::string AsString() const { return *this; }
	int Compare(const RootedPath& other) const {
		return stringutil::CompareStringsCaseSensitive(*this, other);
	}
	RootedDirectoryPath Directory() const;
	std::string BaseName() const;
	bool IsDynamic() const { return isDynamicFileName(*this); }
	std::pair<RelativePath, bool> RelativeTo(RootedDirectoryPath directory) const;
	std::pair<RootedDirectoryPath, std::string> RootAndRelativePath() const;
};

// RootedFilePath is a RootedPath intended to be used as a file path.
struct RootedFilePath : RootedPath {
	using RootedPath::RootedPath;
	explicit RootedFilePath(const RootedPath& p) : RootedPath(p) {}

	RootedPath AsPath() const { return RootedPath(*this); }
	ModuleSpecifier AsModuleSpecifier() const;
	RootedDirectoryPath Directory() const;
	std::string WithoutRoot() const;
	std::pair<RootedDirectoryPath, std::string> RootAndRelativePath() const;
	RootedDirectoryPath DirectoryBefore(int index) const;
	std::string SuffixAfterSeparator(int index) const;
	RootedFilePath AppendSuffix(std::string_view suffix) const;
	FileNameStem AsStem() const;
	FileNameStem RemoveFileExtension() const;
	FileNameStem RemoveExtension(std::string_view extension) const;
	RootedFilePath ChangeExtension(std::string_view extension) const;
	RootedFilePath ChangeFullExtension(std::string_view extension) const;
	RootedFilePath ChangeAnyExtension(std::string_view extension,
	                                  const std::vector<std::string_view>& exts,
	                                  CaseSensitivity cs) const;
	std::string BaseName() const;
	bool ExtensionIs(std::string_view extension) const {
		return fileExtensionIs(*this, extension);
	}
	bool ExtensionIsOneOf(
	    const std::vector<std::string_view>& extensions) const {
		return fileExtensionIsOneOf(*this, extensions);
	}
	std::string_view Extension() const { return tryGetExtensionFromPath(*this); }
	std::string_view AnyExtension(
	    const std::vector<std::string_view>& extensions,
	    CaseSensitivity cs) const;
	std::string_view LongestExtension(
	    const std::vector<std::string_view>& extensions,
	    CaseSensitivity cs) const;
	bool HasExtension() const {
		return getBaseFileNameFromNormalized(*this).find('.') !=
		       std::string_view::npos;
	}
	bool HasImplementationTSFileExtension() const;
	bool HasTSFileExtension() const;
	std::string_view TryExtractTSExtension() const;
	bool HasJSONFileExtension() const;
	std::string_view DeclarationFileExtension() const;
	std::string_view DeclarationEmitExtension() const;
	std::vector<std::string> PossibleOriginalInputExtensions() const;
	bool HasJSFileExtension() const;
	bool IsDynamic() const { return isDynamicFileName(*this); }
	bool IsDeclarationFile() const;
	std::vector<std::string> Components() const;
	int RootLength() const { return getRootLength(*this); }
	std::pair<RelativePath, bool> RelativeTo(
	    RootedDirectoryPath directory) const;
	std::tuple<RootedDirectoryPath, RootedDirectoryPath, bool>
	SplitAtComponent(std::string_view component) const;
	std::tuple<RootedDirectoryPath, RootedDirectoryPath, bool>
	SplitAtLastComponent(std::string_view component) const;
	bool ContainsLowercaseDirectorySequence(std::string_view seq) const {
		return find(seq) != std::string::npos;
	}
	int DirectorySeparatorCount() const {
		return (int)std::count(begin(), end(), '/');
	}
};

// RootedDirectoryPath is a RootedPath intended as a directory path; no
// trailing-separator guarantee.
struct RootedDirectoryPath : RootedPath {
	using RootedPath::RootedPath;
	explicit RootedDirectoryPath(const RootedPath& p) : RootedPath(p) {}

	RootedPath AsPath() const { return RootedPath(*this); }
	std::string BaseName() const;
	std::vector<std::string> Components() const;
	bool ContainsLowercaseDirectorySequence(std::string_view seq) const {
		return find(seq) != std::string::npos;
	}
	RootedFilePath ResolveFile(std::string_view path) const;
	RootedFilePath ResolveRelativeFile(RelativePath path) const;
	RootedFilePath ResolveFileFromNormalizedRelative(
	    std::string_view path) const;
	RootedDirectoryPath ResolveRelativeDirectory(RelativePath path) const;
	RootedDirectoryPath ResolveDirectory(std::string_view path) const;

	// rooted_path.go — ForEachAncestorDirectory calls callback for this
	// directory and each of its ancestors.
	template <typename T, typename F>
	std::pair<T, bool> ForEachAncestorDirectory(F&& callback) const {
		RootedDirectoryPath directory = *this;
		for (;;) {
			auto [result, stop] = callback(directory);
			if (stop) return {result, true};
			auto parent = RootedDirectoryPath(
				getDirectoryPathFromNormalized(directory));
			if (parent == directory) return {T{}, false};
			directory = std::move(parent);
		}
	}

	// rooted_path.go — ForEachAncestorDirectoryPathStoppingAtGlobalCache.
	template <typename T, typename F>
	T ForEachAncestorDirectoryStoppingAtGlobalCache(
	    RootedDirectoryPath globalCache, F&& callback) const {
		auto [result, _] = ForEachAncestorDirectory<T>(
			[&](RootedDirectoryPath ancestor) {
				auto [result, stop] = callback(ancestor);
				return std::pair{std::move(result),
				                 stop || ancestor == globalCache};
			});
		return result;
	}
};

// FileNameStem is a rooted filename prefix, not a normalized path.
struct FileNameStem : std::string {
	using std::string::string;
	FileNameStem() = default;
	explicit FileNameStem(std::string_view s) : std::string(s) {}

	std::string AsString() const { return *this; }
	RootedFilePath AppendSuffix(std::string_view suffix) const;
};

// pathkey.go — PathKey: canonical key for a rooted, normalized path under a
// caller-selected CaseSensitivity. The zero value is a valid sentinel.
struct PathKey : std::string {
	using std::string::string;
	PathKey() = default;
	explicit PathKey(std::string_view s) : std::string(s) {}
	explicit PathKey(const std::string& s) : std::string(s) {}

	std::string AsString() const { return *this; }
	PathKey Parent() const;
	PathKey RemoveTrailingDirectorySeparator() const;
	std::string_view Extension() const {
		return tryGetExtensionFromPath(*this);
	}
	bool ExtensionIsOneOf(
	    const std::vector<std::string_view>& extensions) const {
		return fileExtensionIsOneOf(*this, extensions);
	}
	bool HasJSFileExtension() const;
	std::string BaseName() const;
	bool IsDynamic() const { return isDynamicFileName(*this); }
	PathKey CaseInsensitiveKey() const;
	PathKey AppendCanonicalComponent(std::string_view component) const;
	PathKey AppendCanonicalSuffix(std::string_view suffix) const;
	std::tuple<PathKey, PathKey, bool> SplitAtCanonicalComponent(
	    std::string_view component) const;
	bool ContainsLowercaseDirectorySequence(std::string_view seq) const {
		return find(seq) != std::string::npos;
	}
	bool ContainsPath(const PathKey& child) const;

	// pathkey.go — ForEachAncestorDirectory calls callback for p and each of
	// its ancestors.
	template <typename T, typename F>
	std::pair<T, bool> ForEachAncestorDirectory(F&& callback) const {
		PathKey p = *this;
		for (;;) {
			auto [result, stop] = callback(p);
			if (stop) return {result, true};
			auto parent = p.Parent();
			if (parent == p) return {T{}, false};
			p = std::move(parent);
		}
	}
};

// relative_path.go — RelativePath: slash-normalized, lexically normalized
// path without a root. Leading parent components and a trailing directory
// separator are permitted. Its zero value is a valid sentinel.
struct RelativePath : std::string {
	using std::string::string;
	RelativePath() = default;
	explicit RelativePath(std::string_view s) : std::string(s) {}
	explicit RelativePath(const std::string& s) : std::string(s) {}

	std::string AsString() const { return *this; }
	ModuleSpecifier AsModuleSpecifier() const;
	RelativePath ChangeExtension(std::string_view extension) const;
	std::string BaseName() const;
	bool IsParentRelative() const {
		return *this == ".." || starts_with("../");
	}
	bool HasTrailingDirectorySeparator_() const {
		return tspath::hasTrailingDirectorySeparator(*this);
	}
	RelativePath WithoutTrailingDirectorySeparator() const {
		if (!HasTrailingDirectorySeparator_()) return *this;
		return RelativePath(tspath::removeTrailingDirectorySeparator(*this));
	}
	RelativePath WithTrailingDirectorySeparator() const {
		if (empty() || HasTrailingDirectorySeparator_()) return *this;
		return RelativePath(*this + "/");
	}
	bool requiresResolution() const {
		return IsParentRelative() || HasTrailingDirectorySeparator_();
	}
};

// module_specifier.go — ModuleSpecifier: source text that identifies a
// module; a semantic tag, not a filesystem-path invariant.
struct ModuleSpecifier : std::string {
	using std::string::string;
	ModuleSpecifier() = default;
	explicit ModuleSpecifier(std::string_view s) : std::string(s) {}
	explicit ModuleSpecifier(const std::string& s) : std::string(s) {}

	std::string AsString() const { return *this; }
	bool IsAbsolute() const { return pathIsAbsolute(*this); }
	bool IsRelative() const { return pathIsRelative(*this); }
	bool Contains(std::string_view substring) const {
		return find(substring) != std::string::npos;
	}
	ModuleSpecifier Resolve(std::initializer_list<std::string_view> parts)
	    const;
	ModuleSpecifier ResolveRelative(RelativePath path) const {
		return Resolve({path});
	}
	ModuleSpecifier CombineRelative(RelativePath path) const {
		return ModuleSpecifier(combinePaths(*this, {std::string(path)}));
	}
	ModuleSpecifier RemoveFileExtension() const {
		return ModuleSpecifier(tspath::removeFileExtension(*this));
	}
};

// === rooted_path.go free functions ===

bool hasRootedURLSuffix(std::string_view path);
bool hasURLRoot(std::string_view path);

// ToRootedPath resolves path against currentDirectory and normalizes it.
RootedPath toRootedPath(std::string_view path,
                        RootedDirectoryPath currentDirectory);
std::pair<RootedPath, bool> tryRootedPathFromAbsolute(std::string_view path);
RootedPath rootedPathFromAbsolute(std::string_view path);
std::string ensureRootedPathRootSeparator(std::string_view path);
std::pair<RootedPath, bool> tryRootedPathFromNormalized(
    std::string_view path);
RootedPath rootedPathFromNormalized(std::string_view path);

RootedFilePath toRootedFilePath(std::string_view fileName,
                                RootedDirectoryPath currentDirectory);
RootedFilePath rootedFilePathFromAbsolute(std::string_view fileName);
std::pair<RootedFilePath, bool> tryRootedFilePathFromAbsolute(
    std::string_view fileName);
RootedFilePath rootedFilePathFromNormalized(std::string_view fileName);
std::pair<RootedFilePath, bool> tryRootedFilePathFromNormalized(
    std::string_view fileName);

RootedDirectoryPath toRootedDirectoryPath(
    std::string_view directory, RootedDirectoryPath currentDirectory);
RootedDirectoryPath rootedDirectoryPathFromAbsolute(
    std::string_view directory);
RootedDirectoryPath rootedDirectoryPathFromNormalized(
    std::string_view directory);
inline RootedFilePath rootedFilePathFromPath(RootedPath path) {
	return RootedFilePath(std::move(path));
}
inline RootedDirectoryPath rootedDirectoryPathFromPath(RootedPath path) {
	return RootedDirectoryPath(std::move(path));
}

RootedFilePath rootedFilePathFromResolved(std::string_view path);
void validateFileNameSuffix(std::string_view prefix,
                            std::string_view suffix);
void validateFileExtension(std::string_view extension);
bool canAppendPathWithoutNormalization(std::string_view path);
bool isNormalizedSlashesRelativePath(std::string_view path);
std::string appendPathToDirectory(RootedDirectoryPath directory,
                                  std::string_view path);
int lastDirectorySeparator(std::string_view path);
RootedFilePath rootedFilePathFromExtensionMutation(std::string_view path);

// relative_path.go
RelativePath toRelativePath(std::string_view path);
RelativePath relativePathFromNormalized(std::string_view path);
std::string relativePathFromNormalizedPaths(std::string_view from,
                                            std::string_view to,
                                            CaseSensitivity caseSensitivity);

// path.go — GetCommonParentDirectories over typed directories.
std::pair<std::vector<RootedDirectoryPath>,
          std::unordered_set<RootedDirectoryPath>>
getCommonParentDirectories(
	const std::vector<RootedDirectoryPath>& directories, int minComponents,
	const std::function<std::vector<std::string>(RootedDirectoryPath)>&
	    getComponents,
	CaseSensitivity caseSensitivity);

// pathkey.go
PathKey pathKeyFromCanonical(std::string_view path);
std::pair<PathKey, bool> tryPathKeyFromCanonical(std::string_view path);

// module_specifier.go
inline ModuleSpecifier toModuleSpecifier(std::string_view specifier) {
	return ModuleSpecifier(specifier);
}

}  // namespace tsc::tspath

// std::hash support for use as map/set keys.
namespace std {
template <>
struct hash<tsc::tspath::RootedDirectoryPath>
    : std::hash<std::string> {};
template <>
struct hash<tsc::tspath::RootedFilePath>
    : std::hash<std::string> {};
template <>
struct hash<tsc::tspath::RootedPath> : std::hash<std::string> {};
template <>
struct hash<tsc::tspath::PathKey> : std::hash<std::string> {};
template <>
struct hash<tsc::tspath::RelativePath> : std::hash<std::string> {};
}  // namespace std

// path.go — CaseSensitivity-arg free functions (ComparePaths family and the
// relative-path helpers). Thin overloads over the ComparePathsOptions forms:
// the case flag comes from cs; currentDirectory is the typed rooted dir.

namespace tsc::tspath {

inline int comparePaths(std::string_view a, std::string_view b,
                        CaseSensitivity cs) {
	ComparePathsOptions options;
	options.useCaseSensitiveFileNames = cs.isCaseSensitive();
	return comparePaths(a, b, options);
}

inline int comparePathsRelativeTo(std::string_view a, std::string_view b,
                                  RootedDirectoryPath currentDirectory,
                                  CaseSensitivity cs) {
	ComparePathsOptions options;
	options.useCaseSensitiveFileNames = cs.isCaseSensitive();
	options.currentDirectory = currentDirectory;
	return comparePaths(a, b, options);
}

inline bool containsPath(std::string_view parent, std::string_view child,
                         CaseSensitivity cs) {
	ComparePathsOptions options;
	options.useCaseSensitiveFileNames = cs.isCaseSensitive();
	return containsPath(parent, child, options);
}

inline std::string getRelativePathFromDirectory(std::string_view fromDirectory,
                                                std::string_view to,
                                                CaseSensitivity cs) {
	ComparePathsOptions options;
	options.useCaseSensitiveFileNames = cs.isCaseSensitive();
	return getRelativePathFromDirectory(fromDirectory, to, options);
}

inline std::string resolveRelativePathFromDirectory(
    std::string_view fromDirectory, std::string_view to,
    RootedDirectoryPath currentDirectory, CaseSensitivity cs) {
	ComparePathsOptions options;
	options.useCaseSensitiveFileNames = cs.isCaseSensitive();
	options.currentDirectory = currentDirectory;
	return getRelativePathFromDirectory(fromDirectory, to, options);
}

}  // namespace tsc::tspath
