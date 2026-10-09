// watchbackend.go — port of tsc/internal/execute/watchmanager/watchbackend.go.

#include "internal/execute/watchmanager/watchbackend.h"

#include "internal/tspath/tspath.h"

namespace tsc::execute::watchmanager {
namespace {

// watchCloser — adapts fswatch::Watch to io.Closer.
struct watchCloser : gostd::io::Closer {
	std::shared_ptr<fswatch::Watch> w;
	explicit watchCloser(std::shared_ptr<fswatch::Watch> w)
	    : w(std::move(w)) {}
	gostd::Error close() override {
		return w == nullptr ? nullptr : w->close();
	}
};

}  // namespace

// FSWatchBackend.WatchDirectory — watchbackend.go:31.
std::pair<std::unique_ptr<gostd::io::Closer>, gostd::Error>
FSWatchBackend::WatchDirectory(
    const std::string& dir, const fswatch::WatchCallback& fn, bool recursive,
    std::function<bool(const std::string&)> ignore) {
	auto [closers, err] = WatchDirectories(
	    {{.Dir = dir,
	      .Callback = fn,
	      .Recursive = recursive,
	      .Ignore = std::move(ignore)}});
	if (err) {
		return {nullptr, err};
	}
	return {std::move(closers[0]), nullptr};
}

// FSWatchBackend.WatchDirectories — watchbackend.go:46.
std::pair<std::vector<std::unique_ptr<gostd::io::Closer>>, gostd::Error>
FSWatchBackend::WatchDirectories(
    const std::vector<WatchDirectoryRequest>& requests) {
	std::vector<fswatch::WatchDirectoryRequest> fswatchRequests(
	    requests.size());
	for (size_t i = 0; i < requests.size(); i++) {
		auto& request = requests[i];
		std::vector<std::shared_ptr<fswatch::WatchOption>> opts;
		if (request.Recursive) {
			opts.push_back(fswatch::WithRecursive());
		}
		if (request.Ignore != nullptr) {
			opts.push_back(fswatch::WithIgnore(request.Ignore));
		}
		fswatchRequests[i] = fswatch::WatchDirectoryRequest{
		    .dir = request.Dir,
		    .callback = request.Callback,
		    .options = std::move(opts),
		};
	}
	auto [watches, err] = Inner->watchDirectories(std::move(fswatchRequests));
	if (err) {
		return {std::vector<std::unique_ptr<gostd::io::Closer>>{}, err};
	}
	std::vector<std::unique_ptr<gostd::io::Closer>> closers(watches.size());
	for (size_t i = 0; i < watches.size(); i++) {
		closers[i] = std::make_unique<watchCloser>(std::move(watches[i]));
	}
	return {std::move(closers), nullptr};
}

// ShouldIgnoreWatchPath — watchbackend.go:65.
bool ShouldIgnoreWatchPath(const std::string& path) {
	auto p = tspath::normalizeSlashes(path);
	return (p.size() >= 5 && p.ends_with("/.git")) ||
	       p.find("/.git/") != std::string::npos ||
	       p.find("/node_modules/.") != std::string::npos ||
	       p.find("/.#") != std::string::npos;
}

// CanWatchDirectory — watchbackend.go:74.
bool CanWatchDirectory(std::string_view dir) {
	auto components = tspath::resolvePathComponents(dir, "");
	int length = static_cast<int>(components.size());
	if (length <= 2) {
		return false;
	}
	int rootLength = PerceivedOsRootLengthForWatching(components);
	return length > rootLength + 1;
}

namespace {

// strings.EqualFold.
bool equalFold(std::string_view a, std::string_view b) {
	return a.size() == b.size() &&
	       std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		       return std::tolower(static_cast<unsigned char>(x)) ==
		              std::tolower(static_cast<unsigned char>(y));
	       });
}

}  // namespace

// PerceivedOsRootLengthForWatching — watchbackend.go:85.
int PerceivedOsRootLengthForWatching(
    const std::vector<std::string>& components) {
	int length = static_cast<int>(components.size());
	if (length <= 1) {
		return 1;
	}
	const std::string& root = components[0];
	int indexAfterOsRoot = 1;
	bool isDosStyle = root.size() >= 2 &&
	                  tspath::isVolumeCharacter(root[0]) && root[1] == ':';

	if (root != "/" && !isDosStyle && length > 1) {
		if (components[1].size() >= 2 &&
		    tspath::isVolumeCharacter(components[1][0]) &&
		    components[1].ends_with("$")) {
			if (length == 2) {
				return 2;
			}
			indexAfterOsRoot = 2;
			isDosStyle = true;
		}
	}

	if (isDosStyle && (indexAfterOsRoot >= length ||
	                   !equalFold(components[indexAfterOsRoot], "users"))) {
		return indexAfterOsRoot;
	}

	if (indexAfterOsRoot < length &&
	    equalFold(components[indexAfterOsRoot], "workspaces")) {
		return indexAfterOsRoot + 1;
	}

	return indexAfterOsRoot + 2;
}

}  // namespace tsc::execute::watchmanager
