// Port of tsc/internal/execute/incremental/referencemap.go — the per-file
// forward reference sets and the lazily built reverse index.
#include "internal/execute/incremental/incremental.h"

namespace tsc::execute::incremental {

// referencemap.go:22 storeReferences.
void referenceMap::storeReferences(
    const tspath::Path& path, collections::Set<tspath::Path>* refs) {
	references.Store(path, refs);
}

// referencemap.go:27 getReferences.
std::pair<collections::Set<tspath::Path>*, bool> referenceMap::getReferences(
    const tspath::Path& path) {
	return references.Load(path);
}

// referencemap.go:31 getPathsWithReferences.
std::vector<tspath::Path> referenceMap::getPathsWithReferences() {
	std::vector<tspath::Path> keys;
	references.Range(
	    [&](const tspath::Path& key,
	        collections::Set<tspath::Path>* const&) {
		    keys.push_back(key);
		    return true;
	    });
	return keys;
}

// referencemap.go:37 getReferencedBy — builds the reverse index on first
// use (sync.Once), then yields each path that references `path`.
void referenceMap::getReferencedBy(
    const tspath::Path& path,
    const std::function<bool(const tspath::Path&)>& yield) {
	referenceByOnce.run([&]() {
		referencedBy.clear();
		references.Range(
		    [&](const tspath::Path& key,
		        collections::Set<tspath::Path>* value) {
			    for (const auto& ref : value->Keys()) {
				    auto& set = referencedBy[ref];
				    if (set == nullptr) {
					    set = new collections::Set<tspath::Path>();
				    }
				    set->Add(key);
			    }
			    return true;
		    });
	});
	auto it = referencedBy.find(path);
	if (it != referencedBy.end()) {
		// maps.Keys — Go iteration order is unspecified; unordered_set
		// order stands in.
		for (const auto& key : it->second->Keys()) {
			if (!yield(key)) {
				return;
			}
		}
	}
}

}  // namespace tsc::execute::incremental
