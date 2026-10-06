#pragma once

// parsecache.go — ParseCacheKey / ContentMappedParseCacheKey, the key
// builders, and the ParseCache / ContentMappedParseCache typedefs.

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

#include "internal/ast/ast.h"
#include "internal/binder/binder.h"
#include "internal/collections/collections.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/types.h"
#include "internal/locale/locale.h"
#include "internal/parser/parser.h"
#include "internal/project/files.h"
#include "internal/project/refcountcache.h"
#include "internal/xxh3/xxh3.h"

namespace tsc::compiler {
// === dep decl — owned by compiler slice (tsc/internal/compiler/fileloader.go) ===
// DuplicateSourceFile — fileloader.go:89.
struct DuplicateSourceFile {
	SourceFileParseOptions ParseOptions;
	// ContentMapperParseOptions are the acquire-time options for a
	// content-mapped parse-cache entry.
	SourceFileParseOptions ContentMapperParseOptions;
	xxh3::Uint128 Hash;
	ScriptKind ScriptKind;
	// ContentMapper is the identity of the content mapper that produced this
	// file, or "" if the file is not content-mapped.
	std::string ContentMapper;
	// IsContentMapperFailureStub reports whether the file is an empty
	// placeholder from a failed transform.
	bool IsContentMapperFailureStub = false;
};
} // namespace tsc::compiler

namespace tsc::project {

// ParseCacheKey — parsecache.go. Hash-comparable as a Go map key.
struct ParseCacheKey {
	SourceFileParseOptions sourceFileParseOptions;
	ScriptKind scriptKind = ScriptKind::Unknown;
	xxh3::Uint128 hash;

	bool operator==(const ParseCacheKey& o) const {
		return sourceFileParseOptions.FileName ==
		           o.sourceFileParseOptions.FileName &&
		       sourceFileParseOptions.Path == o.sourceFileParseOptions.Path &&
		       sourceFileParseOptions.ExternalModuleIndicatorOptions.JSX ==
		           o.sourceFileParseOptions.ExternalModuleIndicatorOptions
		               .JSX &&
		       sourceFileParseOptions.ExternalModuleIndicatorOptions.Force ==
		           o.sourceFileParseOptions.ExternalModuleIndicatorOptions
		               .Force &&
		       scriptKind == o.scriptKind && hash == o.hash;
	}
};

// NewParseCacheKey — parsecache.go.
inline ParseCacheKey newParseCacheKey(
	const SourceFileParseOptions& options, xxh3::Uint128 hash,
	ScriptKind scriptKind) {
	if (scriptKind == ScriptKind::Unknown) {
		scriptKind = ensureScriptKindFromFileName(options.FileName);
	}
	return ParseCacheKey{options, scriptKind, hash};
}

// ContentMappedParseCacheKey identifies the complete output bundle for one
// mapped input. Hash folds the original content, mapper transform identity,
// and diagnostic locale together.
struct ContentMappedParseCacheKey {
	SourceFileParseOptions sourceFileParseOptions;
	xxh3::Uint128 hash;

	bool operator==(const ContentMappedParseCacheKey& o) const {
		return sourceFileParseOptions.FileName ==
		           o.sourceFileParseOptions.FileName &&
		       sourceFileParseOptions.Path == o.sourceFileParseOptions.Path &&
		       sourceFileParseOptions.ExternalModuleIndicatorOptions.JSX ==
		           o.sourceFileParseOptions.ExternalModuleIndicatorOptions
		               .JSX &&
		       sourceFileParseOptions.ExternalModuleIndicatorOptions.Force ==
		           o.sourceFileParseOptions.ExternalModuleIndicatorOptions
		               .Force &&
		       hash == o.hash;
	}
};

// contentMappedParseCacheKey — parsecache.go.
ContentMappedParseCacheKey contentMappedParseCacheKey(
	const SourceFileParseOptions& options, xxh3::Uint128 rawHash,
	xxh3::Uint128 transformIdentity, const locale::Locale& diagnosticLocale);

// Key hashes — Go map comparability; unordered_map functors local to the
// project slice.

struct ParseCacheKeyHash {
	size_t operator()(const ParseCacheKey& k) const {
		size_t h =
		    std::hash<std::string>()(k.sourceFileParseOptions.FileName);
		h = h * 31 +
		    std::hash<std::string>()(k.sourceFileParseOptions.Path);
		h = h * 31 +
		    static_cast<size_t>(
		        k.sourceFileParseOptions.ExternalModuleIndicatorOptions.JSX) *
		        2 +
		    (k.sourceFileParseOptions.ExternalModuleIndicatorOptions.Force ? 1
		                                                                   : 0);
		h = h * 31 + static_cast<size_t>(k.scriptKind);
		h = h * 31 + static_cast<size_t>(k.hash.Hi) +
		    static_cast<size_t>(k.hash.Lo) * 2654435761u;
		return h;
	}
};

struct ContentMappedParseCacheKeyHash {
	size_t operator()(const ContentMappedParseCacheKey& k) const {
		size_t h =
		    std::hash<std::string>()(k.sourceFileParseOptions.FileName);
		h = h * 31 +
		    std::hash<std::string>()(k.sourceFileParseOptions.Path);
		h = h * 31 + static_cast<size_t>(k.hash.Hi) +
		    static_cast<size_t>(k.hash.Lo) * 2654435761u;
		return h;
	}
};

// parseCacheKeyForFile reconstructs the ordinary parse-cache key for a
// source file held by a program.
inline ParseCacheKey parseCacheKeyForFile(const SourceFile* file) {
	return newParseCacheKey(file->ParseOptions(), {file->Hash.hi, file->Hash.lo},
	                        file->ScriptKind);
}

// contentMappedParseCacheKeyForFile — parsecache.go.
inline ContentMappedParseCacheKey contentMappedParseCacheKeyForFile(
	const SourceFile* file) {
	return ContentMappedParseCacheKey{file->ContentMapperParseOptions(),
	                                  {file->Hash.hi, file->Hash.lo}};
}

// parseCacheKeyForDuplicate reconstructs an ordinary parse-cache key for a
// deduplicated source file.
inline ParseCacheKey parseCacheKeyForDuplicate(
	const compiler::DuplicateSourceFile* file) {
	return newParseCacheKey(file->ParseOptions, file->Hash, file->ScriptKind);
}

// contentMappedParseCacheKeyForDuplicate — parsecache.go.
inline ContentMappedParseCacheKey contentMappedParseCacheKeyForDuplicate(
	const compiler::DuplicateSourceFile* file) {
	return ContentMappedParseCacheKey{file->ContentMapperParseOptions,
	                                  file->Hash};
}

// ParseCache — parsecache.go. `FileHandle` is the Go `FileHandle` acquire
// arg (the file's content/hash source).
using ParseCache =
	RefCountCache<ParseCacheKey, SourceFile*, FileHandle*, ParseCacheKeyHash>;

// NewParseCache — parsecache.go.
ParseCache* newParseCache(const RefCountCacheOptions& options);

// ContentMappedParseCache — parsecache.go.
// One reference owns the canonical file and all supplemental files as a
// bundle. Callers ref and deref the canonical file only. Go embeds
// `*RefCountCache` promoting its methods; C++ wraps and forwards.
struct ContentMappedParseCache {
	using CacheT = RefCountCache<ContentMappedParseCacheKey,
	                             contentmapper::SourceFiles, std::monostate,
	                             ContentMappedParseCacheKeyHash>;
	CacheT* cache;

	explicit ContentMappedParseCache(CacheT* c) : cache(c) {}

	contentmapper::SourceFiles Acquire(const ContentMappedParseCacheKey& key,
	                                   const std::monostate& args = {}) {
		return cache->Acquire(key, args);
	}
	std::pair<contentmapper::SourceFiles, gostd::Error> AcquireOrError(
		const ContentMappedParseCacheKey& key,
		std::function<std::pair<contentmapper::SourceFiles, gostd::Error>()>
		    produce) {
		return cache->AcquireOrError(key, std::move(produce));
	}
	bool Has(const ContentMappedParseCacheKey& key) { return cache->Has(key); }
	void Ref(const ContentMappedParseCacheKey& key) { cache->Ref(key); }
	void Deref(const ContentMappedParseCacheKey& key) { cache->Deref(key); }
};

// NewContentMappedParseCache — parsecache.go.
ContentMappedParseCache* newContentMappedParseCache(
	const RefCountCacheOptions& options);

} // namespace tsc::project
