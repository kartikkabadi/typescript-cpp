// discovertypings.cpp — port of tsc/internal/project/ata/discovertypings.go.
#include "internal/project/ata/ata.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "internal/core/nodemodules.h"
#include "internal/json/json.h"
#include "internal/packagejson/packagejson.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfsmatch/vfsmatch.h"

namespace tsc::ata {

// fmtStringListForLog — fmt %v of []string; defined below (shared by the
// anonymous-namespace helpers).
std::string fmtStringListForLog(const std::vector<std::string>& v);
std::string removeMinAndVersionNumbers(const std::string& fileName);

namespace {

// core.Filter — keep elements satisfying f.
std::vector<std::string> filterFileNames(
    const std::vector<std::string>& fileNames,
    const std::function<bool(const std::string&)>& f) {
	std::vector<std::string> out;
	for (const auto& s : fileNames) {
		if (f(s)) out.push_back(s);
	}
	return out;
}

// utf8.DecodeLastRuneInString — (rune, size); RuneError+1 on invalid.
std::pair<char32_t, int> decodeLastRuneInString(std::string_view s) {
	if (s.empty()) {
		return {kRuneError, 0};
	}
	size_t end = s.size();
	unsigned char last = (unsigned char)s[end - 1];
	if (last < 0x80) {
		return {last, 1};
	}
	// Guard against O(n^2): only scan back UTF8Max bytes for a rune start.
	size_t lim = end >= 4 ? end - 4 : 0;
	size_t start = end - 1;
	for (; start > lim; start--) {
		if (((unsigned char)s[start] & 0xC0) != 0x80) {
			break;
		}
	}
	int width = 0;
	char32_t r = decodeUtf8RuneStrict(s.substr(start), &width);
	if (start + (size_t)width != end) {
		return {kRuneError, 1};
	}
	return {r, width};
}

void addInferredTyping(
    std::unordered_map<std::string, std::string>& inferredTypings,
    const std::string& typingName) {
	if (inferredTypings.find(typingName) == inferredTypings.end()) {
		inferredTypings[typingName] = "";
	}
}

void addInferredTypings(
    vfs::FS* fs, logging::Logger* logger,
    std::unordered_map<std::string, std::string>& inferredTypings,
    const std::vector<std::string>& typingNames,
    const std::string& message) {
	(void)fs;
	logging::logf(logger, "ATA:: %s: %v", message.c_str(),
	              fmtStringListForLog(typingNames));
	for (const auto& typingName : typingNames) {
		addInferredTyping(inferredTypings, typingName);
	}
}

// getTypingNamesFromSourceFileNames — infer typing names from file names.
void getTypingNamesFromSourceFileNames(
    vfs::FS* fs, logging::Logger* logger,
    std::unordered_map<std::string, std::string>& inferredTypings,
    const std::vector<std::string>& fileNames) {
	bool hasJsxFile = false;
	std::vector<std::string> fromFileNames;
	for (const auto& fileName : fileNames) {
		hasJsxFile = hasJsxFile ||
		    tspath::fileExtensionIs(fileName, tspath::extensionJsx);
		std::string inferredTypingName =
		    std::string(tspath::removeFileExtension(
		        tspath::toFileNameLowerCase(
		            tspath::getBaseFileName(fileName))));
		std::string cleanedTypingName =
		    removeMinAndVersionNumbers(inferredTypingName);
		auto it = safeFileNameToTypeName.find(cleanedTypingName);
		if (it != safeFileNameToTypeName.end()) {
			fromFileNames.push_back(it->second);
		}
	}
	if (!fromFileNames.empty()) {
		addInferredTypings(fs, logger, inferredTypings, fromFileNames,
		                   "Inferred typings from file names");
	}
	if (hasJsxFile) {
		logging::log(logger,
		             "ATA:: Inferred 'react' typings due to presence of "
		             "'.jsx' extension");
		addInferredTyping(inferredTypings, "react");
	}
}

// addTypingNamesAndGetFilesToWatch — inferred typings from manifest/module
// pairs (think package.json + node_modules).
std::vector<std::string> addTypingNamesAndGetFilesToWatch(
    vfs::FS* fs, logging::Logger* logger,
    std::unordered_map<std::string, std::string>& inferredTypings,
    std::vector<std::string> filesToWatch,
    const std::string& projectRootPath, const std::string& manifestName,
    const std::string& modulesDirName) {
	// First, we check the manifests themselves. They're not
	// _required_, but they allow us to do some filtering when dealing
	// with big flat dep directories.
	std::string manifestPath =
	    tspath::combinePaths(projectRootPath, {manifestName});
	std::vector<std::string> manifestTypingNames;
	auto [manifestContents, ok] = fs->ReadFile(manifestPath);
	if (ok) {
		filesToWatch.push_back(manifestPath);
		// var manifest packagejson.DependencyFields
		auto [dom, perr] = json::parse(manifestContents);
		bool err = perr != nullptr;
		if (!err && dom.kind == json::Dom::K::Object) {
			// Each dependency field must be an object or the whole
			// Unmarshal errors and no names are collected (Go semantics).
			for (const char* field : {"dependencies", "devDependencies",
			                          "optionalDependencies",
			                          "peerDependencies"}) {
				const json::Dom* f = json::objGet(dom, field);
				if (f == nullptr) {
					continue;
				}
				if (f->kind != json::Dom::K::Object) {
					err = true;
					break;
				}
				for (const auto& [k, _] : f->obj) {
					manifestTypingNames.push_back(k);
				}
			}
		} else {
			err = true;
		}
		if (!err) {
			addInferredTypings(fs, logger, inferredTypings,
			                   manifestTypingNames,
			                   "Typing names in '" + manifestPath +
			                       "' dependencies");
		}
	}

	// Now we scan the directories for typing information in
	// already-installed dependencies (if present). Note that this
	// step happens regardless of whether a manifest was present,
	// which is certainly a valid configuration, if an unusual one.
	std::string packagesFolderPath =
	    tspath::combinePaths(projectRootPath, {modulesDirName});
	filesToWatch.push_back(packagesFolderPath);
	if (!fs->DirectoryExists(packagesFolderPath)) {
		return filesToWatch;
	}

	// There's two cases we have to take into account here:
	// 1. If manifest is undefined, then we're not using a manifest.
	//    That means that we should scan _all_ dependencies at the top
	//    level of the modulesDir.
	// 2. If manifest is defined, then we can do some special
	//    filtering to reduce the amount of scanning we need to do.
	//
	// Previous versions of this algorithm checked for a `_requiredBy`
	// field in the package.json, but that field is only present in
	// `npm@>=3 <7`.

	// Package names that do **not** provide their own typings, so
	// we'll look them up.
	std::vector<std::string> packageNames;

	std::vector<std::string> dependencyManifestNames;
	if (!manifestTypingNames.empty()) {
		// This is #1 described above.
		for (const auto& typingName : manifestTypingNames) {
			dependencyManifestNames.push_back(tspath::combinePaths(
			    packagesFolderPath, {typingName, manifestName}));
		}
	} else {
		// And #2. Depth = 3 because scoped packages look like
		// `node_modules/@foo/bar/package.json`
		int depth = 3;
		for (const auto& manifestPath : vfs::vfsmatch::ReadDirectory(
		         fs, projectRootPath, packagesFolderPath,
		         {tspath::extensionJson}, {}, {}, depth)) {
			if (tspath::getBaseFileName(manifestPath) != manifestName) {
				continue;
			}

			// It's ok to treat
			// `node_modules/@foo/bar/package.json` as a manifest,
			// but not `node_modules/jquery/nested/package.json`.
			// We only assume depth 3 is ok for formally scoped
			// packages. So that needs this dance here.

			auto pathComponents =
			    tspath::resolvePathComponents(manifestPath, "");
			size_t lenPathComponents = pathComponents.size();
			int w = 0;
			char32_t ch = decodeUtf8RuneStrict(
			    pathComponents[lenPathComponents - 3], &w);
			bool isScoped = ch == '@';

			if ((isScoped &&
			     tspath::toFileNameLowerCase(
			         pathComponents[lenPathComponents - 4]) ==
			         modulesDirName) || // `node_modules/@foo/bar`
			    (!isScoped &&
			     tspath::toFileNameLowerCase(
			         pathComponents[lenPathComponents - 3]) ==
			         modulesDirName)) { // `node_modules/foo`
				dependencyManifestNames.push_back(manifestPath);
			}
		}
	}

	logging::logf(logger,
	              "ATA:: Searching for typing names in %s; all files: %v",
	              packagesFolderPath.c_str(),
	              fmtStringListForLog(dependencyManifestNames));

	// Once we have the names of things to look up, we iterate over
	// and either collect their included typings, or add them to the
	// list of typings we need to look up separately.
	for (const auto& depManifestPath : dependencyManifestNames) {
		auto [depManifestContents, ok] = fs->ReadFile(depManifestPath);
		if (!ok) {
			continue;
		}
		auto [manifest, parseOk] =
		    packagejson::Parse(depManifestContents);
		// If the package has its own d.ts typings, those will take
		// precedence. Otherwise the package name will be used
		// to download d.ts files from DefinitelyTyped
		if (!parseOk || manifest.Name.Value.empty()) {
			continue;
		}
		std::string ownTypes = manifest.Types.Value;
		if (ownTypes.empty()) {
			ownTypes = manifest.Typings.Value;
		}
		if (!ownTypes.empty()) {
			std::string absolutePath = tspath::getNormalizedAbsolutePath(
			    ownTypes, tspath::getDirectoryPath(depManifestPath));
			if (fs->FileExists(absolutePath)) {
				logging::logf(logger,
				              "ATA::     Package '%s' provides its own "
				              "types.",
				              manifest.Name.Value.c_str());
				inferredTypings[manifest.Name.Value] = absolutePath;
			} else {
				logging::logf(logger,
				              "ATA::     Package '%s' provides its own "
				              "types but they are missing.",
				              manifest.Name.Value.c_str());
			}
		} else {
			packageNames.push_back(manifest.Name.Value);
		}
	}
	addInferredTypings(fs, logger, inferredTypings, packageNames,
	                   "    Found package names");
	return filesToWatch;
}

} // namespace

// fmtStringListForLog — fmt %v of []string (declared in ata.cpp).
std::string fmtStringListForLog(const std::vector<std::string>& v) {
	std::string out = "[";
	for (size_t i = 0; i < v.size(); i++) {
		if (i != 0) {
			out += ' ';
		}
		out += v[i];
	}
	out += ']';
	return out;
}

// isTypingUpToDate — discovertypings.go.
bool isTypingUpToDate(
    const CachedTyping* cachedTyping,
    const std::unordered_map<std::string, std::string>&
        availableTypingVersions) {
	std::string useVersion;
	auto it = availableTypingVersions.find(
	    "ts" + std::string(versionMajorMinor()));
	if (it != availableTypingVersions.end()) {
		useVersion = it->second;
	} else if (auto lit = availableTypingVersions.find("latest");
	           lit != availableTypingVersions.end()) {
		useVersion = lit->second;
	}
	semver::Version availableVersion = semver::MustParse(useVersion);
	return semver::versionCompare(&availableVersion,
	                              cachedTyping->Version.get()) <= 0;
}

DiscoverTypingsResult DiscoverTypings(
    vfs::FS* fs, logging::Logger* logger, const TypingsInfo* typingsInfo,
    const std::vector<std::string>& fileNames,
    const std::string& projectRootPath,
    collections::SyncMap<std::string, std::shared_ptr<CachedTyping>>*
        packageNameToTypingLocation,
    const std::unordered_map<
        std::string, std::unordered_map<std::string, std::string>>&
        typesRegistry) {
	DiscoverTypingsResult result;
	// A typing name to typing file path mapping
	std::unordered_map<std::string, std::string> inferredTypings;

	// Only infer typings for .js and .jsx files
	auto filteredFileNames = filterFileNames(
	    fileNames, [](const std::string& fileName) {
		    return tspath::hasJSFileExtension(fileName);
	    });

	if (!typingsInfo->TypeAcquisition->Include.empty()) {
		addInferredTypings(fs, logger, inferredTypings,
		                   typingsInfo->TypeAcquisition->Include,
		                   "Explicitly included types");
	}
	const auto& exclude = typingsInfo->TypeAcquisition->Exclude;

	// Directories to search for package.json, bower.json and other typing
	// information
	if (!typingsInfo->CompilerOptions->TypesWasSet) {
		std::unordered_set<std::string> possibleSearchDirs;
		for (const auto& fileName : filteredFileNames) {
			possibleSearchDirs.insert(
			    tspath::getDirectoryPath(fileName));
		}
		possibleSearchDirs.insert(projectRootPath);
		for (const auto& searchDir : possibleSearchDirs) {
			result.filesToWatch = addTypingNamesAndGetFilesToWatch(
			    fs, logger, inferredTypings,
			    std::move(result.filesToWatch), searchDir, "bower.json",
			    "bower_components");
			result.filesToWatch = addTypingNamesAndGetFilesToWatch(
			    fs, logger, inferredTypings,
			    std::move(result.filesToWatch), searchDir,
			    "package.json", "node_modules");
		}
	}

	if (!tristateIsTrue(
	        typingsInfo->TypeAcquisition
	            ->DisableFilenameBasedTypeAcquisition)) {
		getTypingNamesFromSourceFileNames(fs, logger, inferredTypings,
		                                  filteredFileNames);
	}

	// add typings for unresolved imports
	std::vector<std::string> modules;
	if (typingsInfo->UnresolvedImports != nullptr) {
		modules.reserve(typingsInfo->UnresolvedImports->Len());
		for (const auto& moduleName :
		     typingsInfo->UnresolvedImports->Keys()) {
			modules.push_back(std::string(
			    nonRelativeModuleNameForTypingCache(moduleName)));
		}
		std::sort(modules.begin(), modules.end());
		auto last = std::unique(modules.begin(), modules.end());
		modules.erase(last, modules.end());
	}
	addInferredTypings(fs, logger, inferredTypings, modules,
	                   "Inferred typings from unresolved imports");

	// Remove typings that the user has added to the exclude list
	for (const auto& excludeTypingName : exclude) {
		inferredTypings.erase(excludeTypingName);
		logging::logf(logger,
		              "ATA:: Typing for %s is in exclude list, will be "
		              "ignored.",
		              excludeTypingName.c_str());
	}

	// Add the cached typing locations for inferred typings that are
	// already installed
	packageNameToTypingLocation->Range(
	    [&](const std::string& name,
	        const std::shared_ptr<CachedTyping>& typing) -> bool {
		    auto regIt = typesRegistry.find(name);
		    auto infIt = inferredTypings.find(name);
		    bool inferredEmpty =
		        infIt == inferredTypings.end() || infIt->second.empty();
		    if (inferredEmpty && regIt != typesRegistry.end() &&
		        isTypingUpToDate(typing.get(), regIt->second)) {
			    inferredTypings[name] = typing->TypingsLocation;
		    }
		    return true;
	    });

	for (const auto& [typing, inferred] : inferredTypings) {
		if (!inferred.empty()) {
			result.cachedTypingPaths.push_back(inferred);
		} else {
			result.newTypingNames.push_back(typing);
		}
	}
	logging::logf(logger,
	              "ATA:: Finished typings discovery: cachedTypingsPaths: "
	              "%v newTypingNames: %v, filesToWatch %v",
	              fmtStringListForLog(result.cachedTypingPaths),
	              fmtStringListForLog(result.newTypingNames),
	              fmtStringListForLog(result.filesToWatch));
	return result;
}

// removeMinAndVersionNumbers — "jquery-min.4.2.3" -> "jquery". @internal
std::string removeMinAndVersionNumbers(const std::string& fileName) {
	// We used to use the regex /[.-]((min)|(\d+(\.\d+)*))$/ and would just
	// .replace it twice. Unfortunately, that regex has O(n^2) performance
	// because v8 doesn't match from the end of the string. Instead, we now
	// essentially scan the filename (backwards) ourselves.
	size_t end = fileName.size();
	for (size_t pos = end; pos > 0;) {
		std::string_view head(fileName.data(), pos);
		auto [ch, size] = decodeLastRuneInString(head);
		if (ch >= '0' && ch <= '9') {
			// Match a \d+ segment
			for (;;) {
				pos -= size;
				head = std::string_view(fileName.data(), pos);
				auto [ch2, size2] = decodeLastRuneInString(head);
				ch = ch2;
				size = size2;
				if (pos <= 0 || ch < '0' || ch > '9') {
					break;
				}
			}
		} else if (pos > 4 && (ch == 'n' || ch == 'N')) {
			// Looking for "min" or "min"
			// Already matched the 'n'
			pos -= size;
			head = std::string_view(fileName.data(), pos);
			auto [ch2, size2] = decodeLastRuneInString(head);
			ch = ch2;
			size = size2;
			if (ch != 'i' && ch != 'I') {
				break;
			}
			pos -= size;
			head = std::string_view(fileName.data(), pos);
			auto [ch3, size3] = decodeLastRuneInString(head);
			ch = ch3;
			size = size3;
			if (ch != 'm' && ch != 'M') {
				break;
			}
			pos -= size;
			head = std::string_view(fileName.data(), pos);
			auto [ch4, size4] = decodeLastRuneInString(head);
			ch = ch4;
			size = size4;
		} else {
			// This character is not part of either suffix pattern
			break;
		}

		if (ch != '-' && ch != '.') {
			break;
		}
		pos -= size;
		end = pos;
	}
	return fileName.substr(0, end);
}

} // namespace tsc::ata
