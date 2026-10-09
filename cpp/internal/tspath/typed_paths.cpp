// Port of tsc/internal/tspath typed path layer (ed480721):
// rooted_path.go, pathkey.go, relative_path.go, module_specifier.go, and
// path.go's CaseSensitivity methods. Implemented against the header-only
// tspath.h primitives. Panics in Go map to TSC_UNREACHABLE.
#include "internal/tspath/typed_paths.h"

#include "internal/gostd/gostd.h"

#include <algorithm>

namespace tsc::tspath {

namespace {

[[maybe_unused]] std::string_view asSv(const RootedPath& p) { return p; }
[[maybe_unused]] std::string_view asSv(const RootedFilePath& p) { return p; }
[[maybe_unused]] std::string_view asSv(const RootedDirectoryPath& p) { return p; }
[[maybe_unused]] std::string_view asSv(const PathKey& p) { return p; }

[[noreturn]] void panicInvalidComponent() {
	throw std::string{"invalid path component"};
}

}  // namespace

// === path.go — CaseSensitivity methods ===

int CaseSensitivity::compareRootedText(std::string_view aString,
                                       std::string_view bString) const {
	if (aString == bString) return 0;
	if (aString.empty()) return -1;
	if (bString.empty()) return 1;

	if (isEncodedDynamicFileName(aString) ||
	    isEncodedDynamicFileName(bString)) {
		return stringutil::CompareStringsCaseSensitive(
			canonicalDynamicURIPath(aString),
			canonicalDynamicURIPath(bString));
	}
	auto aRootLength = getRootLength(aString);
	auto bRootLength = getRootLength(bString);
	if (auto result = stringutil::CompareStringsCaseInsensitive(
		    aString.substr(0, aRootLength), bString.substr(0, bRootLength));
	    result != 0) {
		return result;
	}
	return getComparer()(aString.substr(aRootLength),
	                     bString.substr(bRootLength));
}

int CaseSensitivity::compareFilePaths(RootedFilePath a,
                                      RootedFilePath b) const {
	return comparePaths(a.AsPath(), b.AsPath());
}

int CaseSensitivity::comparePaths(RootedPath a, RootedPath b) const {
	return compareRootedText(a, b);
}

int CaseSensitivity::compareFileNameStems(FileNameStem a,
                                          FileNameStem b) const {
	return compareRootedText(a, b);
}

std::pair<std::string, bool> CaseSensitivity::trimContainedPath(
    std::string_view parent, std::string_view child) const {
	if (parent.empty() || child.empty()) {
		return {"", false};
	}
	auto parentRootLength = getRootLength(parent);
	auto childRootLength = getRootLength(child);
	auto parentRoot = parent.substr(0, parentRootLength);
	auto childRoot = child.substr(0, childRootLength);
	auto dynamic = isEncodedDynamicFileName(parent) ||
	               isEncodedDynamicFileName(child);
	CaseSensitivity c = *this;
	std::string parentRootBuf, childRootBuf;
	if (dynamic) {
		if (parentRoot.ends_with('/')) {
			parentRootBuf = std::string(parentRoot.substr(0, parentRoot.size() - 1));
			parentRoot = parentRootBuf;
		}
		if (childRoot.ends_with('/')) {
			childRootBuf = std::string(childRoot.substr(0, childRoot.size() - 1));
			childRoot = childRootBuf;
		}
		c = CaseSensitivity::CaseSensitive();
	}
	auto rootsEqual = stringutil::EquateStringCaseInsensitive(parentRoot, childRoot);
	if (dynamic) {
		rootsEqual = parentRoot == childRoot;
	}
	if (!rootsEqual) {
		return {"", false};
	}
	auto relative = c.trimPrefix(child.substr(childRootLength),
	                             parent.substr(parentRootLength));
	if (!relative.second ||
	    (!relative.first.empty() &&
	     !hasTrailingDirectorySeparator(parent) &&
	     (int)parent.size() != parentRootLength &&
	     relative.first[0] != DirectorySeparator)) {
		return {"", false};
	}
	std::string result = std::move(relative.first);
	if (result.starts_with(DirectorySeparator)) {
		result.erase(0, 1);
	}
	return {result, true};
}

bool CaseSensitivity::containsFilePath(RootedDirectoryPath parent,
                                       RootedFilePath child) const {
	return containsPath(parent, child.AsPath());
}

bool CaseSensitivity::containsPath(RootedDirectoryPath parent,
                                   RootedPath child) const {
	auto [rel, ok] = trimContainedPath(parent, child);
	return ok;
}

bool CaseSensitivity::startsWithDirectory(
    RootedFilePath fileName, RootedDirectoryPath directory) const {
	auto [rel, ok] = trimContainedPath(directory, fileName);
	return ok && !rel.empty();
}

std::pair<RelativePath, bool>
CaseSensitivity::relativePathWithinDirectory(RootedDirectoryPath directory,
                                             RootedPath path) const {
	auto [rel, ok] = trimContainedPath(directory, path);
	if (!ok) return {"", false};
	return {RelativePath(rel), true};
}

std::pair<RelativePath, bool>
CaseSensitivity::relativeFilePathFromDirectory(
    RootedDirectoryPath directory, RootedFilePath fileName) const {
	return relativePathWithinDirectory(directory, fileName.AsPath());
}

std::tuple<RootedDirectoryPath, RootedDirectoryPath, bool>
CaseSensitivity::splitFilePathAtComponent(RootedFilePath fileName,
                                          std::string_view component) const {
	if (component.empty() ||
	    component.find_first_of("/\\") != std::string_view::npos ||
	    component == "." || component == "..") {
		panicInvalidComponent();
	}
	auto components = getPathComponents(std::string_view(fileName));
	CaseSensitivity c = *this;
	if (isEncodedDynamicFileName(fileName)) {
		c = CaseSensitivity::CaseSensitive();
	}
	for (size_t i = 1; i < components.size(); i++) {
		if (c.getComparer()(components[i], component) == 0) {
			std::vector<std::string_view> before(components.begin(),
			                                     components.begin() + i);
			std::vector<std::string_view> through(components.begin(),
			                                      components.begin() + i + 1);
			return {RootedDirectoryPath(getPathFromPathComponents(before)),
			        RootedDirectoryPath(getPathFromPathComponents(through)),
			        true};
		}
	}
	return {RootedDirectoryPath(), RootedDirectoryPath(), false};
}

RootedDirectoryPath CaseSensitivity::commonDirectoryOfFiles(
    const std::vector<RootedFilePath>& fileNames) const {
	std::vector<std::string> commonPathComponents;
	bool first = true;
	for (const auto& fileName : fileNames) {
		auto pathComponents = getPathComponents(std::string(fileName));
		pathComponents.resize(pathComponents.size() - 1);
		if (first) {
			commonPathComponents = pathComponents;
			first = false;
			continue;
		}
		size_t n = std::min(commonPathComponents.size(), pathComponents.size());
		auto effectiveCaseSensitivity = *this;
		if (isEncodedDynamicFileName(fileName) ||
		    isEncodedDynamicFileName(getPathFromPathComponents(
			    std::vector<std::string_view>(commonPathComponents.begin(),
			                                  commonPathComponents.end())))) {
			effectiveCaseSensitivity = CaseSensitivity::CaseSensitive();
			if (!commonPathComponents[0].empty() &&
			    commonPathComponents[0].ends_with('/')) {
				commonPathComponents[0].pop_back();
			}
			if (!pathComponents[0].empty() && pathComponents[0].ends_with('/')) {
				pathComponents[0].pop_back();
			}
		}
		for (size_t i = 0; i < n; i++) {
			if (effectiveCaseSensitivity.canonicalize(commonPathComponents[i]) !=
			    effectiveCaseSensitivity.canonicalize(pathComponents[i])) {
				if (i == 0) return RootedDirectoryPath();
				commonPathComponents.resize(i);
				break;
			}
		}
		if (pathComponents.size() < commonPathComponents.size()) {
			commonPathComponents.resize(pathComponents.size());
		}
	}
	if (commonPathComponents.empty()) return RootedDirectoryPath();
	return RootedDirectoryPath(getPathFromPathComponents(
		std::vector<std::string_view>(commonPathComponents.begin(),
		                              commonPathComponents.end())));
}

// path.go — getPathComponentsRelativeTo: the components-level core shared by
// GetPathComponentsRelativeTo and RelativePath computation.
static std::vector<std::string> getPathComponentsRelativeToImpl(
    const std::vector<std::string_view>& fromComponents,
    const std::vector<std::string_view>& toComponents,
    CaseSensitivity caseSensitivity) {
	size_t start = 0;
	size_t maxCommonComponents =
	    std::min(fromComponents.size(), toComponents.size());
	auto stringEqualer = caseSensitivity.getEqualityComparer();
	for (; start < maxCommonComponents; start++) {
		auto fromComponent = fromComponents[start];
		auto toComponent = toComponents[start];
		if (start == 0) {
			if (!stringutil::EquateStringCaseInsensitive(fromComponent,
			                                           toComponent)) {
				break;
			}
		} else {
			if (!stringEqualer(fromComponent, toComponent)) {
				break;
			}
		}
	}

	if (start == 0) {
		return {toComponents.begin(), toComponents.end()};
	}

	size_t numDotDotSlashes = fromComponents.size() - start;
	std::vector<std::string> result;
	result.emplace_back("");
	for (size_t i = 0; i < numDotDotSlashes; i++) {
		result.emplace_back("..");
	}
	for (size_t i = start; i < toComponents.size(); i++) {
		result.emplace_back(toComponents[i]);
	}
	return result;
}

// === rooted_path.go ===

bool hasRootedURLSuffix(std::string_view path) {
	if (!hasURLRoot(path)) return false;
	auto pos = path.find(urlSchemeSeparator);
	if (pos == std::string_view::npos) return false;
	auto afterScheme = path.substr(pos + urlSchemeSeparator.size());
	return afterScheme.find_first_of("?#") != std::string_view::npos;
}

bool hasURLRoot(std::string_view path) {
	return getEncodedRootLength(path) < 0 &&
	       path.find(urlSchemeSeparator) != std::string_view::npos;
}

std::string ensureRootedPathRootSeparator(std::string_view path) {
	if ((size_t)getRootLength(path) == path.size()) {
		if (!hasTrailingDirectorySeparator(path)) {
			return std::string(path) + (char)DirectorySeparator;
		}
	}
	return std::string(path);
}

RootedPath toRootedPath(std::string_view path,
                        RootedDirectoryPath currentDirectory) {
	if (path.empty()) {
		throw std::string{"path must not be empty"};
	}
	if (hasRootedURLSuffix(path)) {
		throw std::string{"path must not contain a URL query or fragment"};
	}
	if (getEncodedRootLength(path) == 0 && hasURLRoot(currentDirectory) &&
	    path.find_first_of("?#") != std::string_view::npos) {
		throw std::string{"relative URL path must not contain a query or fragment"};
	}
	auto normalized = getNormalizedAbsolutePathFromDirectory(path, currentDirectory);
	if (getEncodedRootLength(normalized) == 0 ||
	    hasRootedURLSuffix(normalized)) {
		throw std::string{"path must be rooted"};
	}
	return RootedPath(ensureRootedPathRootSeparator(normalized));
}

std::pair<RootedPath, bool> tryRootedPathFromAbsolute(std::string_view path) {
	if (hasRootedURLSuffix(path) || !pathIsAbsolute(path)) {
		return {RootedPath(), false};
	}
	return {RootedPath(ensureRootedPathRootSeparator(
		        getNormalizedAbsolutePath(path, RootedDirectoryPath()))),
	        true};
}

RootedPath rootedPathFromAbsolute(std::string_view path) {
	auto [result, ok] = tryRootedPathFromAbsolute(path);
	if (!ok) {
		throw std::string{"path must be absolute"};
	}
	return result;
}

std::pair<RootedPath, bool> tryRootedPathFromNormalized(
    std::string_view path) {
	if (hasRootedURLSuffix(path)) {
		return {RootedPath(), false};
	}
	auto rootLength = getEncodedRootLength(path);
	if (rootLength < 0) rootLength = ~rootLength;
	if (path.empty() || rootLength == 0 ||
	    path.find('\\') != std::string_view::npos ||
	    (rootLength < (int)path.size() && path[rootLength] == '/') ||
	    hasRelativePathSegment(path.substr(rootLength)) ||
	    ((int)path.size() == rootLength &&
	     !hasTrailingDirectorySeparator(path)) ||
	    ((int)path.size() > rootLength &&
	     hasTrailingDirectorySeparator(path))) {
		return {RootedPath(), false};
	}
	return {RootedPath(path), true};
}

RootedPath rootedPathFromNormalized(std::string_view path) {
	auto [result, ok] = tryRootedPathFromNormalized(path);
	if (!ok) {
		throw std::string{"path must be rooted and normalized: "} + std::string(path);
	}
	return result;
}

RootedFilePath toRootedFilePath(std::string_view fileName,
                                RootedDirectoryPath currentDirectory) {
	return RootedFilePath(toRootedPath(fileName, currentDirectory));
}

RootedFilePath rootedFilePathFromAbsolute(std::string_view fileName) {
	return RootedFilePath(rootedPathFromAbsolute(fileName));
}

std::pair<RootedFilePath, bool> tryRootedFilePathFromAbsolute(
    std::string_view fileName) {
	auto [path, ok] = tryRootedPathFromAbsolute(fileName);
	return {RootedFilePath(path), ok};
}

RootedFilePath rootedFilePathFromNormalized(std::string_view fileName) {
	return RootedFilePath(rootedPathFromNormalized(fileName));
}

std::pair<RootedFilePath, bool> tryRootedFilePathFromNormalized(
    std::string_view fileName) {
	auto [path, ok] = tryRootedPathFromNormalized(fileName);
	return {RootedFilePath(path), ok};
}

RootedDirectoryPath toRootedDirectoryPath(
    std::string_view directory, RootedDirectoryPath currentDirectory) {
	return RootedDirectoryPath(toRootedPath(directory, currentDirectory));
}

RootedDirectoryPath rootedDirectoryPathFromAbsolute(
    std::string_view directory) {
	return RootedDirectoryPath(rootedPathFromAbsolute(directory));
}

RootedDirectoryPath rootedDirectoryPathFromNormalized(
    std::string_view directory) {
	return RootedDirectoryPath(rootedPathFromNormalized(directory));
}

int lastDirectorySeparator(std::string_view path) {
	for (int i = (int)path.size() - 1; i >= 0; i--) {
		if (path[i] == DirectorySeparator) return i;
	}
	return -1;
}

// === RootedPath methods ===

RootedDirectoryPath RootedPath::Directory() const {
	auto path = asSv(*this);
	auto rootLength = getRootLength(path);
	if (rootLength == (int)path.size()) {
		return RootedDirectoryPath(*this);
	}
	auto trimmed = removeTrailingDirectorySeparator(path);
	auto last = lastDirectorySeparator(trimmed);
	return RootedDirectoryPath(
		std::string(trimmed.substr(0, std::max(rootLength, last))));
}

std::string RootedPath::BaseName() const {
	return std::string(getBaseFileNameFromNormalized(*this));
}

std::pair<RelativePath, bool> RootedPath::RelativeTo(
    RootedDirectoryPath directory) const {
	auto [rel, ok] =
		CaseSensitivity::CaseSensitive().trimContainedPath(directory, *this);
	if (!ok) return {RelativePath(), false};
	return {RelativePath(rel), true};
}

std::pair<RootedDirectoryPath, std::string>
RootedPath::RootAndRelativePath() const {
	auto path = asSv(*this);
	auto rootLength = getRootLength(path);
	return {RootedDirectoryPath(std::string(path.substr(0, rootLength))),
	        std::string(path.substr(rootLength))};
}

// === RootedFilePath methods ===

ModuleSpecifier RootedFilePath::AsModuleSpecifier() const {
	return ModuleSpecifier(*this);
}

RootedDirectoryPath RootedFilePath::Directory() const {
	return AsPath().Directory();
}

std::string RootedFilePath::WithoutRoot() const {
	auto path = asSv(*this);
	return std::string(path.substr(getRootLength(path)));
}

std::pair<RootedDirectoryPath, std::string>
RootedFilePath::RootAndRelativePath() const {
	return AsPath().RootAndRelativePath();
}

RootedDirectoryPath RootedFilePath::DirectoryBefore(int index) const {
	auto path = asSv(*this);
	if (index < getRootLength(path) || index > (int)path.size() ||
	    (index < (int)path.size() && !isAnyDirectorySeparator(path[index]))) {
		throw std::string{"directory boundary must be at a path separator"};
	}
	return RootedDirectoryPath(std::string(path.substr(0, index)));
}

std::string RootedFilePath::SuffixAfterSeparator(int index) const {
	auto path = asSv(*this);
	if (index < 0 || index >= (int)path.size() ||
	    !isAnyDirectorySeparator(path[index])) {
		throw std::string{"suffix boundary must be at a path separator"};
	}
	return std::string(path.substr(index + 1));
}

void validateFileNameSuffix(std::string_view prefix,
                            std::string_view suffix) {
	if (prefix.empty() && !suffix.empty()) {
		throw std::string{"cannot append a suffix to an empty file name"};
	}
	if (suffix.find_first_of("/\\") != std::string_view::npos) {
		throw std::string{"file name suffix must not contain a directory separator"};
	}
}

void validateFileExtension(std::string_view extension) {
	if (extension.find_first_of("/\\") != std::string_view::npos) {
		throw std::string{"file extension must not contain a directory separator"};
	}
}

RootedFilePath rootedFilePathFromExtensionMutation(std::string_view path) {
	auto baseName = getBaseFileNameFromNormalized(path);
	if (baseName == "." || baseName == ".." ||
	    (hasTrailingDirectorySeparator(path) &&
	     (int)path.size() > getRootLength(path))) {
		throw std::string{"file extension change must preserve path normalization"};
	}
	return rootedFilePathFromResolved(path);
}

RootedFilePath RootedFilePath::AppendSuffix(std::string_view suffix) const {
	validateFileNameSuffix(*this, suffix);
	auto result = RootedFilePath(std::string(*this) + std::string(suffix));
	if (!result.empty()) {
		rootedPathFromNormalized(result);
	}
	return result;
}

FileNameStem RootedFilePath::AsStem() const {
	return FileNameStem(*this);
}

FileNameStem RootedFilePath::RemoveFileExtension() const {
	return FileNameStem(removeFileExtension(*this));
}

FileNameStem RootedFilePath::RemoveExtension(
    std::string_view extension) const {
	validateFileExtension(extension);
	if (!ends_with(extension)) {
		throw std::string{"file name does not have extension: "} + std::string(extension);
	}
	return FileNameStem(tspath::removeExtension(*this, extension));
}

RootedFilePath RootedFilePath::ChangeExtension(
    std::string_view extension) const {
	validateFileExtension(extension);
	return rootedFilePathFromExtensionMutation(changeExtension(*this, extension));
}

RootedFilePath RootedFilePath::ChangeFullExtension(
    std::string_view extension) const {
	validateFileExtension(extension);
	return rootedFilePathFromExtensionMutation(
		changeFullExtension(*this, extension));
}

RootedFilePath RootedFilePath::ChangeAnyExtension(
    std::string_view extension,
    const std::vector<std::string_view>& exts, CaseSensitivity cs) const {
	validateFileExtension(extension);
	for (auto candidate : exts) {
		validateFileExtension(candidate);
	}
	return rootedFilePathFromExtensionMutation(
		changeAnyExtension(*this, extension, exts, !cs.isCaseSensitive()));
}

std::string RootedFilePath::BaseName() const { return AsPath().BaseName(); }

std::string_view RootedFilePath::AnyExtension(
    const std::vector<std::string_view>& extensions,
    CaseSensitivity cs) const {
	if (extensions.empty()) {
		return getAnyExtensionFromNormalizedPath(*this);
	}
	return getAnyExtensionFromPath(*this, &extensions,
	                             !cs.isCaseSensitive());
}

std::string_view RootedFilePath::LongestExtension(
    const std::vector<std::string_view>& extensions,
    CaseSensitivity cs) const {
	return getLongestExtensionFromPath(*this, extensions,
	                                 !cs.isCaseSensitive());
}

bool RootedFilePath::HasImplementationTSFileExtension() const {
	return hasImplementationTSFileExtension(*this);
}
bool RootedFilePath::HasTSFileExtension() const {
	return hasTSFileExtension(*this);
}
std::string_view RootedFilePath::TryExtractTSExtension() const {
	return tryExtractTSExtension(*this);
}
bool RootedFilePath::HasJSONFileExtension() const {
	return hasJSONFileExtension(*this);
}
std::string_view RootedFilePath::DeclarationFileExtension() const {
	return getDeclarationFileExtensionFromNormalized(*this);
}
std::string_view RootedFilePath::DeclarationEmitExtension() const {
	return getDeclarationEmitExtensionForPath(*this);
}
std::vector<std::string>
RootedFilePath::PossibleOriginalInputExtensions() const {
	return getPossibleOriginalInputExtensionForExtension(*this);
}
bool RootedFilePath::HasJSFileExtension() const {
	return hasJSFileExtension(*this);
}
bool RootedFilePath::IsDeclarationFile() const {
	return !getDeclarationFileExtensionFromNormalized(*this).empty();
}
std::vector<std::string> RootedFilePath::Components() const {
	return getPathComponents(*this);
}
std::pair<RelativePath, bool> RootedFilePath::RelativeTo(
    RootedDirectoryPath directory) const {
	return AsPath().RelativeTo(directory);
}

std::tuple<RootedDirectoryPath, RootedDirectoryPath, bool>
RootedFilePath::SplitAtComponent(std::string_view component) const {
	if (component.empty() ||
	    component.find_first_of("/\\") != std::string_view::npos ||
	    component == "." || component == "..") {
		panicInvalidComponent();
	}
	auto needle = std::string("/") + std::string(component);
	auto path = asSv(*this);
	auto rootLength = RootLength();
	if (rootLength == 0) {
		return {RootedDirectoryPath(), RootedDirectoryPath(), false};
	}
	for (size_t offset = rootLength - 1;;) {
		auto index = path.find(needle, offset);
		if (index == std::string_view::npos) {
			return {RootedDirectoryPath(), RootedDirectoryPath(), false};
		}
		auto end = index + needle.size();
		if (end == path.size() || path[end] == DirectorySeparator) {
			auto beforeEnd = std::max(index, (size_t)rootLength);
			return {RootedDirectoryPath(std::string(path.substr(0, beforeEnd))),
			        RootedDirectoryPath(std::string(path.substr(0, end))),
			        true};
		}
		offset = end;
	}
}

std::tuple<RootedDirectoryPath, RootedDirectoryPath, bool>
RootedFilePath::SplitAtLastComponent(std::string_view component) const {
	if (component.empty() ||
	    component.find_first_of("/\\") != std::string_view::npos ||
	    component == "." || component == "..") {
		panicInvalidComponent();
	}
	auto needle = std::string("/") + std::string(component);
	auto path = asSv(*this);
	for (size_t end = path.size(); end > 0;) {
		// Go: strings.LastIndex(path[:end], needle)
		auto index = path.substr(0, end).rfind(needle);
		if (index == std::string_view::npos) {
			return {RootedDirectoryPath(), RootedDirectoryPath(), false};
		}
		auto componentEnd = index + needle.size();
		if (index >= (size_t)(RootLength() - 1) &&
		    (componentEnd == path.size() ||
		     path[componentEnd] == DirectorySeparator)) {
			auto beforeEnd = std::max(index, (size_t)RootLength());
			return {RootedDirectoryPath(std::string(path.substr(0, beforeEnd))),
			        RootedDirectoryPath(std::string(path.substr(0, componentEnd))),
			        true};
		}
		end = index;
	}
	return {RootedDirectoryPath(), RootedDirectoryPath(), false};
}

// === FileNameStem ===

RootedFilePath FileNameStem::AppendSuffix(std::string_view suffix) const {
	validateFileNameSuffix(*this, suffix);
	auto result = *this + std::string(suffix);
	if (result.empty()) return RootedFilePath();
	return rootedFilePathFromNormalized(result);
}

// === RootedDirectoryPath methods ===

std::string RootedDirectoryPath::BaseName() const {
	return AsPath().BaseName();
}
std::vector<std::string> RootedDirectoryPath::Components() const {
	return getPathComponents(*this);
}

bool canAppendPathWithoutNormalization(std::string_view path) {
	return isNormalizedSlashesRelativePath(path) &&
	       !hasRelativePathSegment(path) &&
	       !hasTrailingDirectorySeparator(path);
}

bool isNormalizedSlashesRelativePath(std::string_view path) {
	return !path.empty() && getEncodedRootLength(path) == 0 &&
	       path.find('\\') == std::string_view::npos;
}

std::string appendPathToDirectory(RootedDirectoryPath directory,
                                  std::string_view path) {
	if (hasTrailingDirectorySeparator(directory)) {
		return std::string(directory) + std::string(path);
	}
	return std::string(directory) + "/" + std::string(path);
}

RootedFilePath rootedFilePathFromResolved(std::string_view path) {
	if (hasRootedURLSuffix(path)) {
		throw std::string{"path must not contain a URL query or fragment"};
	}
	return RootedFilePath(path);
}

RootedFilePath RootedDirectoryPath::ResolveFile(std::string_view path) const {
	if (empty()) {
		throw std::string{"cannot resolve from an empty directory name"};
	}
	if (path.empty()) {
		return RootedFilePath(*this);
	}
	if (getEncodedRootLength(path) == 0 && hasURLRoot(*this) &&
	    path.find_first_of("?#") != std::string_view::npos) {
		throw std::string{"relative URL path must not contain a query or fragment"};
	}
	if (canAppendPathWithoutNormalization(path)) {
		return rootedFilePathFromResolved(appendPathToDirectory(*this, path));
	}
	if (isNormalizedSlashesRelativePath(path)) {
		return rootedFilePathFromResolved(
			getNormalizedAbsolutePathFromNormalizedSlashes(
				appendPathToDirectory(*this, path)));
	}
	return toRootedFilePath(path, *this);
}

RootedFilePath RootedDirectoryPath::ResolveRelativeFile(
    RelativePath path) const {
	if (empty()) {
		throw std::string{"cannot resolve from an empty directory name"};
	}
	if (path.empty()) {
		return RootedFilePath(*this);
	}
	if (hasURLRoot(*this) &&
	    path.find_first_of("?#") != std::string_view::npos) {
		throw std::string{"relative URL path must not contain a query or fragment"};
	}
	if (path.requiresResolution()) {
		return toRootedFilePath(path, *this);
	}
	return rootedFilePathFromResolved(appendPathToDirectory(*this, path));
}

RootedFilePath RootedDirectoryPath::ResolveFileFromNormalizedRelative(
    std::string_view path) const {
	if (empty()) {
		throw std::string{"cannot resolve from an empty directory path"};
	}
	if (path.empty()) {
		throw std::string{"path must not be empty"};
	}
	if (getEncodedRootLength(path) == 0 && hasURLRoot(*this) &&
	    path.find_first_of("?#") != std::string_view::npos) {
		throw std::string{"relative URL path must not contain a query or fragment"};
	}
	if (isAnyDirectorySeparator(path[0]) || normalizeSlashes(path) != path ||
	    hasRelativePathSegment(path) ||
	    hasTrailingDirectorySeparator(path)) {
		throw std::string{"path must be relative and normalized: "} + std::string(path);
	}
	return rootedFilePathFromResolved(appendPathToDirectory(*this, path));
}

RootedDirectoryPath RootedDirectoryPath::ResolveRelativeDirectory(
    RelativePath path) const {
	return RootedDirectoryPath(ResolveRelativeFile(path));
}

RootedDirectoryPath RootedDirectoryPath::ResolveDirectory(
    std::string_view path) const {
	if (empty()) {
		throw std::string{"cannot resolve from an empty directory name"};
	}
	if (path.empty()) {
		return RootedDirectoryPath(*this);
	}
	if (getEncodedRootLength(path) == 0 && hasURLRoot(*this) &&
	    path.find_first_of("?#") != std::string_view::npos) {
		throw std::string{"relative URL path must not contain a query or fragment"};
	}
	if (canAppendPathWithoutNormalization(path)) {
		return RootedDirectoryPath(
			rootedFilePathFromResolved(appendPathToDirectory(*this, path)));
	}
	if (isNormalizedSlashesRelativePath(path)) {
		return RootedDirectoryPath(rootedFilePathFromResolved(
			getNormalizedAbsolutePathFromNormalizedSlashes(
				appendPathToDirectory(*this, path))));
	}
	return toRootedDirectoryPath(path, *this);
}

// === PathKey ===

PathKey pathKeyFromCanonical(std::string_view path) {
	auto [result, ok] = tryPathKeyFromCanonical(path);
	if (!ok) {
		throw std::string{"path must be normalized"};
	}
	return result;
}

std::pair<PathKey, bool> tryPathKeyFromCanonical(std::string_view path) {
	if (path.empty()) {
		return {PathKey(), true};
	}
	if (auto rp = tryRootedPathFromNormalized(path); !rp.second) {
		return {PathKey(), false};
	}
	return {PathKey(path), true};
}

PathKey CaseSensitivity::pathKey(RootedPath path) const {
	if (isEncodedDynamicFileName(path)) {
		return PathKey(canonicalDynamicURIPath(path));
	}
	return PathKey(canonicalize(path));
}

PathKey PathKey::Parent() const {
	return PathKey(getDirectoryPathFromNormalized(*this));
}

PathKey PathKey::RemoveTrailingDirectorySeparator() const {
	return PathKey(tspath::removeTrailingDirectorySeparator(*this));
}

bool PathKey::HasJSFileExtension() const { return hasJSFileExtension(*this); }
std::string PathKey::BaseName() const {
	return std::string(getBaseFileNameFromNormalized(*this));
}

PathKey PathKey::CaseInsensitiveKey() const {
	if (isEncodedDynamicFileName(*this)) {
		return *this;
	}
	return PathKey(toFileNameLowerCase(*this));
}

PathKey PathKey::AppendCanonicalComponent(
    std::string_view component) const {
	if (empty()) {
		throw std::string{"cannot append a component to an empty path key"};
	}
	if (component.empty() ||
	    component.find_first_of("/\\") != std::string_view::npos ||
	    component == "." || component == "..") {
		throw std::string{"invalid canonical path component"};
	}
	std::string result;
	if (hasTrailingDirectorySeparator(*this)) {
		result = *this + std::string(component);
	} else {
		result = *this + "/" + std::string(component);
	}
	return pathKeyFromCanonical(result);
}

PathKey PathKey::AppendCanonicalSuffix(std::string_view suffix) const {
	if (empty() && !suffix.empty()) {
		throw std::string{"cannot append a suffix to an empty path key"};
	}
	if (suffix.find_first_of("/\\") != std::string_view::npos) {
		throw std::string{"path suffix must not contain a directory separator"};
	}
	return pathKeyFromCanonical(*this + std::string(suffix));
}

std::tuple<PathKey, PathKey, bool> PathKey::SplitAtCanonicalComponent(
    std::string_view component) const {
	if (component.empty() ||
	    component.find_first_of("/\\") != std::string_view::npos ||
	    component == "." || component == "..") {
		throw std::string{"invalid canonical path component"};
	}
	auto needle = std::string("/") + std::string(component);
	auto path = asSv(*this);
	auto rootLength = getRootLength(path);
	if (rootLength == 0) {
		return {PathKey(), PathKey(), false};
	}
	for (size_t offset = rootLength - 1;;) {
		auto index = path.find(needle, offset);
		if (index == std::string_view::npos) {
			return {PathKey(), PathKey(), false};
		}
		auto end = index + needle.size();
		if (end == path.size() || path[end] == DirectorySeparator) {
			auto beforeEnd = std::max(index, (size_t)rootLength);
			return {PathKey(path.substr(0, beforeEnd)),
			        PathKey(path.substr(0, end)), true};
		}
		offset = end;
	}
}

bool PathKey::ContainsPath(const PathKey& child) const {
	if (empty()) return false;
	auto parent = asSv(*this);
	auto childText = asSv(child);
	auto parentRootLength = getRootLength(parent);
	auto childRootLength = getRootLength(childText);
	auto parentRoot = parent.substr(0, parentRootLength);
	auto childRoot = childText.substr(0, childRootLength);
	auto dynamic = isEncodedDynamicFileName(parent) ||
	               isEncodedDynamicFileName(childText);
	std::string parentRootBuf, childRootBuf;
	if (dynamic) {
		if (parentRoot.ends_with('/')) {
			parentRootBuf = std::string(parentRoot.substr(0, parentRoot.size() - 1));
			parentRoot = parentRootBuf;
		}
		if (childRoot.ends_with('/')) {
			childRootBuf = std::string(childRoot.substr(0, childRoot.size() - 1));
			childRoot = childRootBuf;
		}
	}
	auto rootsEqual =
		stringutil::EquateStringCaseInsensitive(parentRoot, childRoot);
	if (dynamic) {
		rootsEqual = parentRoot == childRoot;
	}
	if (!rootsEqual) return false;
	auto parentRest = parent.substr(parentRootLength);
	auto childRest = childText.substr(childRootLength);
	return parentRest == childRest ||
	       (childRest.size() > parentRest.size() &&
	        childRest.starts_with(parentRest) &&
	        (parentRest.empty() || parentRest.back() == '/' ||
	         childRest[parentRest.size()] == '/'));
}

// === RelativePath ===

RelativePath toRelativePath(std::string_view path) {
	if (getEncodedRootLength(path) != 0) {
		throw std::string{"relative path must not be rooted"};
	}
	return RelativePath(normalizePath(path));
}

RelativePath relativePathFromNormalized(std::string_view path) {
	if (getEncodedRootLength(path) != 0 || normalizePath(path) != path) {
		throw std::string{"relative path must be relative and normalized: "} + std::string(path);
	}
	return RelativePath(path);
}

ModuleSpecifier RelativePath::AsModuleSpecifier() const {
	return ModuleSpecifier(ensurePathIsNonModuleName(*this));
}

RelativePath RelativePath::ChangeExtension(std::string_view extension) const {
	validateFileExtension(extension);
	return RelativePath(tspath::changeExtension(*this, extension));
}

std::string RelativePath::BaseName() const {
	return std::string(getBaseFileNameFromNormalized(*this));
}

RelativePath CaseSensitivity::canonicalRelativePath(
    RelativePath path) const {
	return RelativePath(canonicalize(path));
}

std::string relativePathFromNormalizedPaths(std::string_view from,
                                            std::string_view to,
                                            CaseSensitivity caseSensitivity) {
	auto fromComponents = pathComponents(from, getRootLength(from));
	auto toComponents = pathComponents(to, getRootLength(to));
	if (isEncodedDynamicFileName(from) || isEncodedDynamicFileName(to)) {
		std::string f0 = fromComponents[0].ends_with('/')
			? std::string(fromComponents[0].substr(0, fromComponents[0].size() - 1))
			: std::string(fromComponents[0]);
		std::string t0 = toComponents[0].ends_with('/')
			? std::string(toComponents[0].substr(0, toComponents[0].size() - 1))
			: std::string(toComponents[0]);
		fromComponents[0] = f0;
		toComponents[0] = t0;
		if (f0 != t0) {
			return std::string(to);
		}
		caseSensitivity = CaseSensitivity::CaseSensitive();
	}
	auto relativeComponents = getPathComponentsRelativeToImpl(
		fromComponents, toComponents, caseSensitivity);
	std::vector<std::string_view> views(relativeComponents.begin(),
	                                  relativeComponents.end());
	return getPathFromPathComponents(views);
}

std::pair<RelativePath, bool> CaseSensitivity::relativePathFromDirectory(
    RootedDirectoryPath directory, RootedFilePath fileName) const {
	return relativePathFromPath(directory, fileName.AsPath());
}

std::pair<RelativePath, bool> CaseSensitivity::relativePathFromPath(
    RootedDirectoryPath directory, RootedPath rootedPath) const {
	auto path = relativePathFromNormalizedPaths(directory, rootedPath, *this);
	if (getEncodedRootLength(path) != 0) {
		return {RelativePath(), false};
	}
	return {RelativePath(path), true};
}

std::pair<RelativePath, bool> CaseSensitivity::relativePathFromFile(
    RootedFilePath from, RootedFilePath to) const {
	return relativePathFromFileToPath(from, to.AsPath());
}

std::pair<RelativePath, bool> CaseSensitivity::relativePathFromFileToPath(
    RootedFilePath from, RootedPath to) const {
	return relativePathFromPath(from.Directory(), to);
}

RelativePath CaseSensitivity::relativePathFromRelativeDirectory(
    RelativePath directory, RelativePath fileName) const {
	return RelativePath(
		relativePathFromNormalizedPaths(directory, fileName, *this));
}

// === ModuleSpecifier ===

ModuleSpecifier ModuleSpecifier::Resolve(
    std::initializer_list<std::string_view> parts) const {
	std::vector<std::string_view> v(parts);
	return ModuleSpecifier(
		resolvePathWithoutTrailingDirectorySeparator(*this, v));
}

// === GetCommonParentDirectories ===

std::pair<std::vector<RootedDirectoryPath>,
          std::unordered_set<RootedDirectoryPath>>
getCommonParentDirectories(
	const std::vector<RootedDirectoryPath>& directories, int minComponents,
	const std::function<std::vector<std::string>(RootedDirectoryPath)>&
	    getComponents,
	CaseSensitivity caseSensitivity) {
	if (minComponents < 1) {
		throw std::string{"minComponents must be at least 1"};
	}
	if (directories.empty()) {
		return {{}, {}};
	}
	if (directories.size() == 1) {
		if (reducePathComponents(getComponents(directories[0])).size() <
		    (size_t)minComponents) {
			return {{}, {directories[0]}};
		}
		return {directories, {}};
	}

	std::unordered_set<RootedDirectoryPath> ignored;
	std::vector<std::vector<std::string>> pathComponents;
	pathComponents.reserve(directories.size());
	for (const auto& directory : directories) {
		auto components = reducePathComponents(getComponents(directory));
		if ((int)components.size() < minComponents) {
			ignored.insert(directory);
		} else {
			pathComponents.push_back(std::move(components));
		}
	}

	ComparePathsOptions options{caseSensitivity.isCaseSensitive(), ""};
	auto results = getCommonParentsWorker(pathComponents, minComponents,
	                                      options);
	std::vector<RootedDirectoryPath> parents;
	parents.reserve(results.size());
	for (auto& components : results) {
		parents.push_back(rootedDirectoryPathFromAbsolute(
			getPathFromPathComponents(std::vector<std::string_view>(
				components.begin(), components.end()))));
	}
	return {parents, ignored};
}

}  // namespace tsc::tspath
