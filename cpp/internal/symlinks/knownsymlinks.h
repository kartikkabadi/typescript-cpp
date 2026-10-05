// symlinks — port of tsc/internal/symlinks/knownsymlinks.go.
// === slice: modulespecifiers === (written for the modulespecifiers slice;
// SetSymlinksFromResolutions is omitted — its callback plumbing has no C++
// caller yet.)
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/tspath/tspath.h"

namespace tsc::symlinks {

// knownsymlinks.go:13 — KnownDirectoryLink
struct KnownDirectoryLink {
	// Matches the casing returned by `realpath`. Used to compute the
	// `realpath` of children. Always has trailing directory separator.
	std::string Real;
	// toPath(real). Stored to avoid repeated recomputation.
	// Always has trailing directory separator.
	tspath::Path RealPath;
};

namespace {
// tspath.ContainsIgnoredPath (ignoredpaths.go:11) — (deduped: also
// replicated file-locally in modulespecifiers as containsIgnoredPath).
inline bool containsIgnoredPath(const std::string& s) {
	return s.find("/node_modules/.") != std::string::npos ||
	       s.find("/.git") != std::string::npos ||
	       s.find(".#") != std::string::npos;
}
} // namespace

// knownsymlinks.go:22 — KnownSymlinks. Go stores bare pointers in the
// SyncMaps; shared_ptr preserves pointer identity without leaking.
class KnownSymlinks {
	collections::SyncMap<tspath::Path,
	                     std::shared_ptr<KnownDirectoryLink>>
	    directories;
	collections::SyncMap<tspath::Path,
	                     std::shared_ptr<collections::SyncSet<std::string>>>
	    directoriesByRealpath;
	collections::SyncMap<tspath::Path, std::string> files;
	collections::SyncMap<tspath::Path,
	                     std::shared_ptr<collections::SyncSet<std::string>>>
	    filesByRealpath;
	std::string cwd;
	bool useCaseSensitiveFileNames = false;

public:
	KnownSymlinks(std::string currentDirectory,
	              bool useCaseSensitiveFileNames_)
	    : cwd(std::move(currentDirectory)),
	      useCaseSensitiveFileNames(useCaseSensitiveFileNames_) {}

	// HasDirectory — knownsymlinks.go:31.
	bool HasDirectory(const tspath::Path& symlinkPath) {
		return directories
		    .Load(tspath::ensureTrailingDirectorySeparator(symlinkPath))
		    .second;
	}

	// Directories — map from symlink to realpath. Keys have trailing
	// directory separators. (knownsymlinks.go:37)
	collections::SyncMap<tspath::Path,
	                     std::shared_ptr<KnownDirectoryLink>>*
	Directories() {
		return &directories;
	}

	// DirectoriesByRealpath — (knownsymlinks.go:41)
	collections::SyncMap<tspath::Path,
	                     std::shared_ptr<collections::SyncSet<std::string>>>*
	DirectoriesByRealpath() {
		return &directoriesByRealpath;
	}

	// Files — map from symlink to realpath. (knownsymlinks.go:46)
	collections::SyncMap<tspath::Path, std::string>* Files() { return &files; }

	// FilesByRealpath — map from realpath to symlinks. (knownsymlinks.go:51)
	collections::SyncMap<tspath::Path,
	                     std::shared_ptr<collections::SyncSet<std::string>>>*
	FilesByRealpath() {
		return &filesByRealpath;
	}

	// SetDirectory — knownsymlinks.go:55.
	void SetDirectory(const std::string& symlink,
	                  const tspath::Path& symlinkPath,
	                  const std::shared_ptr<KnownDirectoryLink>& realDirectory) {
		if (realDirectory != nullptr) {
			if (!directories.Load(symlinkPath).second) {
				auto [set, loaded] = directoriesByRealpath.LoadOrStore(
				    realDirectory->RealPath,
				    std::make_shared<collections::SyncSet<std::string>>());
				set->Add(symlink);
			}
		}
		directories.Store(symlinkPath, realDirectory);
	}

	// SetFile — knownsymlinks.go:65.
	void SetFile(const std::string& symlink, const tspath::Path& symlinkPath,
	             const std::string& realpath) {
		if (!files.Load(symlinkPath).second) {
			auto realpathPath =
			    tspath::toPath(realpath, cwd, useCaseSensitiveFileNames);
			auto [set, loaded] = filesByRealpath.LoadOrStore(
			    realpathPath,
			    std::make_shared<collections::SyncSet<std::string>>());
			set->Add(symlink);
		}
		files.Store(symlinkPath, realpath);
	}

	// ProcessResolution — knownsymlinks.go:93.
	void ProcessResolution(const std::string& originalPath,
	                       const std::string& resolvedFileName) {
		if (originalPath.empty() || resolvedFileName.empty()) {
			return;
		}
		SetFile(originalPath,
		        tspath::toPath(originalPath, cwd, useCaseSensitiveFileNames),
		        resolvedFileName);
		auto [commonResolved, commonOriginal] =
		    guessDirectorySymlink(resolvedFileName, originalPath, cwd);
		if (!commonResolved.empty() && !commonOriginal.empty()) {
			auto symlinkPath =
			    tspath::toPath(commonOriginal, cwd, useCaseSensitiveFileNames);
			if (!containsIgnoredPath(symlinkPath)) {
				auto realDirectory = std::make_shared<KnownDirectoryLink>();
				realDirectory->Real =
				    tspath::ensureTrailingDirectorySeparator(commonResolved);
				realDirectory->RealPath =
				    tspath::ensureTrailingDirectorySeparator(tspath::toPath(
				        commonResolved, cwd, useCaseSensitiveFileNames));
				SetDirectory(
				    commonOriginal,
				    tspath::ensureTrailingDirectorySeparator(symlinkPath),
				    realDirectory);
			}
		}
	}

private:
	// guessDirectorySymlink — knownsymlinks.go:114. Returns (resolved,
	// original) directory pair.
	std::pair<std::string, std::string>
	guessDirectorySymlink(const std::string& a, const std::string& b,
	                      const std::string& cwd_) {
		auto aParts =
		    tspath::getPathComponents(tspath::getNormalizedAbsolutePath(a, cwd_),
		                              "");
		auto bParts =
		    tspath::getPathComponents(tspath::getNormalizedAbsolutePath(b, cwd_),
		                              "");
		bool isDirectory = false;
		while (aParts.size() >= 2 && bParts.size() >= 2 &&
		       !isNodeModulesOrScopedPackageDirectory(aParts[aParts.size() - 2]) &&
		       !isNodeModulesOrScopedPackageDirectory(bParts[bParts.size() - 2]) &&
		       tspath::getCanonicalFileName(aParts.back(),
		                                    useCaseSensitiveFileNames) ==
		           tspath::getCanonicalFileName(bParts.back(),
		                                        useCaseSensitiveFileNames)) {
			aParts.pop_back();
			bParts.pop_back();
			isDirectory = true;
		}
		if (isDirectory) {
			std::vector<std::string_view> aViews(aParts.begin(), aParts.end());
			std::vector<std::string_view> bViews(bParts.begin(), bParts.end());
			return {tspath::getPathFromPathComponents(aViews),
			        tspath::getPathFromPathComponents(bViews)};
		}
		return {"", ""};
	}

	// isNodeModulesOrScopedPackageDirectory — knownsymlinks.go:132.
	bool isNodeModulesOrScopedPackageDirectory(const std::string& s) const {
		return !s.empty() &&
		       (tspath::getCanonicalFileName(s, useCaseSensitiveFileNames) ==
		            "node_modules" ||
		        s[0] == '@');
	}
};

// NewKnownSymlink — knownsymlinks.go:74.
inline KnownSymlinks* NewKnownSymlink(std::string currentDirectory,
                                      bool useCaseSensitiveFileNames) {
	return new KnownSymlinks(std::move(currentDirectory),
	                         useCaseSensitiveFileNames);
}

} // namespace tsc::symlinks
