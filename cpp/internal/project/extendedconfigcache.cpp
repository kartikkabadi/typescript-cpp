// extendedconfigcache.go — NewExtendedConfigCache + hash.

#include "internal/project/extendedconfigcache.h"

namespace tsc::project {

namespace {

// hash — extendedconfigcache.go. xxh3.New + WriteString over the parse
// content and each extended file's current content is equivalent to hashing
// the concatenation.
xxh3::Uint128 hash(const tsoptions::ExtendedConfigCacheEntry* entry,
                   const ExtendedConfigParseArgs& args) {
	std::string buf = args.Content;
	for (const auto& fileName : entry->ExtendedFileNames()) {
		FileHandle* fh = args.FS->GetFile(fileName);
		if (fh == nullptr) {
			return xxh3::Uint128{};
		}
		buf += fh->Content();
	}
	return xxh3::hash128(buf);
}

} // namespace

// NewExtendedConfigCache — extendedconfigcache.go.
ExtendedConfigCache* newExtendedConfigCache() {
	return newOwnerCache<tspath::Path, ExtendedConfigCacheEntry*,
	                     ExtendedConfigParseArgs>(
	    [](const tspath::Path& path,
	       const ExtendedConfigParseArgs& args) -> ExtendedConfigCacheEntry* {
		    auto* result = new ExtendedConfigCacheEntry{
		        tsoptions::ParseExtendedConfig(
		            args.FileName, path, args.ResolutionStack, args.Host,
		            args.Cache),
		        {}};
		    result->Hash = hash(result->ExtendedConfigCacheEntry, args);
		    return result;
	    },
	    [](const tspath::Path& path, ExtendedConfigCacheEntry* entry,
	       const ExtendedConfigParseArgs& args) -> bool {
		    return entry->Hash == xxh3::Uint128{} ||
		           entry->Hash != hash(entry->ExtendedConfigCacheEntry, args);
	    });
}

} // namespace tsc::project
