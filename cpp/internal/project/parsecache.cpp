// parsecache.go — the key constructors' .cpp halves plus NewParseCache /
// NewContentMappedParseCache.

#include "internal/project/parsecache.h"

namespace tsc::project {

// contentMappedParseCacheKey — parsecache.go. Folds rawHash,
// transformIdentity, and the diagnostic locale string into one 128-bit key.
ContentMappedParseCacheKey contentMappedParseCacheKey(
	const SourceFileParseOptions& options, xxh3::Uint128 rawHash,
	xxh3::Uint128 transformIdentity, const locale::Locale& diagnosticLocale) {
	// binary.LittleEndian.PutUint64 over 32 bytes, then locale string.
	std::string buf;
	buf.reserve(32 + diagnosticLocale.String().size());
	auto put = [&buf](uint64_t v) {
		for (int i = 0; i < 8; i++) {
			buf.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
		}
	};
	put(rawHash.Hi);
	put(rawHash.Lo);
	put(transformIdentity.Hi);
	put(transformIdentity.Lo);
	buf += diagnosticLocale.String();
	return ContentMappedParseCacheKey{options, xxh3::hash128(buf)};
}

// NewParseCache — parsecache.go.
ParseCache* newParseCache(const RefCountCacheOptions& options) {
	return newRefCountCache<ParseCacheKey, SourceFile*, FileHandle*,
	                      ParseCacheKeyHash>(
	    options, [](const ParseCacheKey& key,
	                FileHandle* fh) -> SourceFile* {
		    SourceFile* file =
		        parseSourceFile(key.sourceFileParseOptions, fh->Content(),
		                        key.scriptKind);
		    file->Hash = {fh->Hash().Lo, fh->Hash().Hi};
		    bindSourceFile(file);
		    return file;
	    });
}

// NewContentMappedParseCache — parsecache.go.
ContentMappedParseCache* newContentMappedParseCache(
	const RefCountCacheOptions& options) {
	auto* cache = newRefCountCache<ContentMappedParseCacheKey,
	                               contentmapper::SourceFiles, std::monostate,
	                               ContentMappedParseCacheKeyHash>(
	    options, [](const ContentMappedParseCacheKey&,
	                const std::monostate&) -> contentmapper::SourceFiles {
		    TSC_UNREACHABLE(
		        "content-mapped source files must be produced with "
		        "AcquireOrError");
	    });
	return new ContentMappedParseCache(cache);
}

} // namespace tsc::project
