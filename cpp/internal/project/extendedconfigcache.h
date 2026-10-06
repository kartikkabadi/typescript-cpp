#pragma once

// extendedconfigcache.go — ExtendedConfigCache: an OwnerCache keyed by
// config path whose entries expire when the hashed inputs change.

#include <string>
#include <vector>

#include "internal/project/files.h"
#include "internal/project/ownercache.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"
#include "internal/xxh3/xxh3.h"

namespace tsc::project {

// ExtendedConfigParseArgs — extendedconfigcache.go.
struct ExtendedConfigParseArgs {
	std::string FileName;
	std::string Content;
	FileSource* FS = nullptr;
	std::vector<tspath::Path> ResolutionStack;
	tsoptions::ParseConfigHost* Host = nullptr;
	tsoptions::ExtendedConfigCache* Cache = nullptr;
};

// ExtendedConfigCacheEntry — extendedconfigcache.go. Go embeds
// `*tsoptions.ExtendedConfigCacheEntry` (promoted field name is the type
// name).
struct ExtendedConfigCacheEntry {
	tsoptions::ExtendedConfigCacheEntry* ExtendedConfigCacheEntry = nullptr;
	xxh3::Uint128 Hash;
};

// ExtendedConfigCache — extendedconfigcache.go.
using ExtendedConfigCache =
	OwnerCache<tspath::Path, ExtendedConfigCacheEntry*,
	           ExtendedConfigParseArgs>;

// NewExtendedConfigCache — extendedconfigcache.go.
ExtendedConfigCache* newExtendedConfigCache();

} // namespace tsc::project
