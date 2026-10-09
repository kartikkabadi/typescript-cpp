// packagejson — port of tsc/internal/packagejson (the subset used by the
// module slice): jsonvalue.go, expected.go, exportsorimports.go,
// packagejson.go, cache.go.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unordered_map>

#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/core/version.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/semver/semver.h"
#include "internal/tspath/tspath.h"

// === slice: api ===
namespace tsc::json { class Decoder; class Encoder; }

namespace tsc::packagejson {

// --- jsonvalue.go — JSONValueType ---

enum class JSONValueType : int8_t {
	NotPresent = 0,
	Null,
	String,
	Number,
	Boolean,
	Array,
	Object,
};

inline std::string JSONValueTypeString(JSONValueType t) {
	switch (t) {
	case JSONValueType::Null: return "null";
	case JSONValueType::String: return "string";
	case JSONValueType::Number: return "number";
	case JSONValueType::Boolean: return "boolean";
	case JSONValueType::Array: return "array";
	case JSONValueType::Object: return "object";
	default:
		return "unknown(" + std::to_string(static_cast<int>(t)) + ")";
	}
}

// JsonValueBase — the shared shape of JSONValue and ExportsOrImports.
// Go stores the payload as `any`; we model it as one slot per kind, with
// `type` selecting the live member.
template <typename T>
struct JsonValueBase {
	JSONValueType type = JSONValueType::NotPresent;
	// Value slots (only the slot matching `type` is meaningful).
	std::string str;
	double num = 0;
	bool boolean = false;
	std::shared_ptr<std::vector<T>> array;
	std::shared_ptr<collections::OrderedMap<std::string, T>> object;

	bool IsPresent() const { return type != JSONValueType::NotPresent; }

	bool IsFalsy() const {
		switch (type) {
		case JSONValueType::NotPresent:
		case JSONValueType::Null:
			return true;
		case JSONValueType::String:
			return str.empty();
		case JSONValueType::Number:
			// Go quirk: `v.Value == 0` compares any(float64) to int —
			// always false.
			return false;
		case JSONValueType::Boolean:
			return !boolean;
		default:
			return false;
		}
	}

	collections::OrderedMap<std::string, T>* AsObject() {
		if (type != JSONValueType::Object) {
			fprintf(stderr,
			        "tsc internal error: expected object, got %s\n",
			        JSONValueTypeString(type).c_str());
			abort();
		}
		return object.get();
	}
	const collections::OrderedMap<std::string, T>* AsObject() const {
		if (type != JSONValueType::Object) {
			fprintf(stderr,
			        "tsc internal error: expected object, got %s\n",
			        JSONValueTypeString(type).c_str());
			abort();
		}
		return object.get();
	}

	std::vector<T>* AsArray() {
		if (type != JSONValueType::Array) {
			fprintf(stderr,
			        "tsc internal error: expected array, got %s\n",
			        JSONValueTypeString(type).c_str());
			abort();
		}
		return array.get();
	}
	const std::vector<T>* AsArray() const {
		if (type != JSONValueType::Array) {
			fprintf(stderr,
			        "tsc internal error: expected array, got %s\n",
			        JSONValueTypeString(type).c_str());
			abort();
		}
		return array.get();
	}

	std::string AsString() const {
		if (type != JSONValueType::String) {
			fprintf(stderr,
			        "tsc internal error: expected string, got %s\n",
			        JSONValueTypeString(type).c_str());
			abort();
		}
		return str;
	}
};

struct JSONValue : JsonValueBase<JSONValue> {
	// === slice: api ===
	// unmarshalJSONValueFrom — jsonvalue.go:88. Implemented in api/proto.cpp.
	std::string unmarshalJSONFrom(json::Decoder& dec);
	// marshalJSONTo emits the Go reflection layout {"Type":n,"Value":v}.
	std::string marshalJSONTo(json::Encoder& enc) const;
};

// --- exportsorimports.go ---

enum class objectKind : int8_t {
	unknown = 0,
	subpaths,
	conditions,
	imports,
	invalid,
};

struct ExportsOrImports : JsonValueBase<ExportsOrImports> {
	mutable objectKind objKind = objectKind::unknown;

	bool IsSubpaths() const {
		initObjectKind();
		return objKind == objectKind::subpaths;
	}
	bool IsImports() const {
		initObjectKind();
		return objKind == objectKind::imports;
	}
	bool IsConditions() const {
		initObjectKind();
		return objKind == objectKind::conditions;
	}

	void initObjectKind() const {
		if (objKind == objectKind::unknown &&
		    type == JSONValueType::Object) {
			auto* obj = AsObject();
			if (obj->Size() > 0) {
				bool seenDot = false, seenHash = false, seenOther = false;
				for (auto& k : obj->Keys()) {
					if (!k.empty()) {
						seenDot = seenDot || k[0] == '.';
						seenHash = seenHash || k[0] == '#';
						seenOther = seenOther ||
						            (k[0] != '.' && k[0] != '#');
						if (seenOther && (seenDot || seenHash)) {
							objKind = objectKind::invalid;
							return;
						}
					}
				}
				if (seenDot) {
					objKind = objectKind::subpaths;
					return;
				}
				if (seenHash) {
					objKind = objectKind::imports;
					return;
				}
			}
			objKind = objectKind::conditions;
		}
	}
};

// --- expected.go ---

// expectedJSONTypeName — reflect.Kind → JSON type name (struct kinds map to
// "unknown", matching Go's default branch).
template <typename T> inline const char* expectedJSONTypeName() {
	if constexpr (std::is_same_v<T, std::string>) return "string";
	else if constexpr (std::is_same_v<T, bool>) return "boolean";
	else if constexpr (std::is_same_v<T, std::vector<std::string>>)
		return "array";
	else if constexpr (std::is_same_v<
	                   T, std::unordered_map<std::string, std::string>>)
		return "object";
	else return "unknown";
}

template <typename T>
struct Expected {
	std::string actualJSONType;
	bool Null = false;
	bool Valid = false;
	T Value{};

	bool IsPresent() const { return !actualJSONType.empty(); }

	std::pair<T, bool> GetValue() const { return {Value, Valid}; }
	const T& GetValueRef() const { return Value; }

	bool IsValid() const { return Valid; }

	const char* ExpectedJSONType() const { return expectedJSONTypeName<T>(); }

	const std::string& ActualJSONType() const { return actualJSONType; }
};

template <typename T>
inline Expected<T> ExpectedOf(T value) {
	Expected<T> e;
	e.Value = std::move(value);
	e.Valid = true;
	e.actualJSONType = e.ExpectedJSONType();
	return e;
}

// --- packagejson.go ---

struct HeaderFields {
	Expected<std::string> Name;    // "name"
	Expected<std::string> Version; // "version"
	Expected<std::string> Type;    // "type"
};

struct PathFields {
	Expected<std::string> TSConfig; // "tsconfig"
	Expected<std::string> Main;     // "main"
	Expected<std::string> Types;    // "types"
	Expected<std::string> Typings;  // "typings"
	JSONValue TypesVersions;        // "typesVersions"
	ExportsOrImports Imports;       // "imports"
	ExportsOrImports Exports;       // "exports"
};

using DependencyMap = std::unordered_map<std::string, std::string>;

struct DependencyFields {
	Expected<DependencyMap> Dependencies;         // "dependencies"
	Expected<DependencyMap> DevDependencies;      // "devDependencies"
	Expected<DependencyMap> PeerDependencies;     // "peerDependencies"
	Expected<DependencyMap> OptionalDependencies; // "optionalDependencies"

	// HasDependency — true if any dependency field has `name`.
	bool HasDependency(const std::string& name) const {
		if (auto [deps, ok] = Dependencies.GetValue(); ok) {
			if (deps.count(name)) return true;
		}
		if (auto [devDeps, ok] = DevDependencies.GetValue(); ok) {
			if (devDeps.count(name)) return true;
		}
		if (auto [peerDeps, ok] = PeerDependencies.GetValue(); ok) {
			if (peerDeps.count(name)) return true;
		}
		if (auto [optDeps, ok] = OptionalDependencies.GetValue(); ok) {
			if (optDeps.count(name)) return true;
		}
		return false;
	}

	// RangeDependencies — iterates each dependency field.
	void RangeDependencies(
	    const std::function<bool(const std::string& name,
	                             const std::string& version,
	                             const std::string& field)>& f) const {
		if (auto [deps, ok] = Dependencies.GetValue(); ok) {
			for (auto& [name, version] : deps) {
				if (!f(name, version, "dependencies")) return;
			}
		}
		if (auto [devDeps, ok] = DevDependencies.GetValue(); ok) {
			for (auto& [name, version] : devDeps) {
				if (!f(name, version, "devDependencies")) return;
			}
		}
		if (auto [peerDeps, ok] = PeerDependencies.GetValue(); ok) {
			for (auto& [name, version] : peerDeps) {
				if (!f(name, version, "peerDependencies")) return;
			}
		}
		if (auto [optDeps, ok] = OptionalDependencies.GetValue(); ok) {
			for (auto& [name, version] : optDeps) {
				if (!f(name, version, "optionalDependencies")) return;
			}
		}
	}

	collections::Set<std::string> GetRuntimeDependencyNames() const {
		DependencyMap deps, peerDeps, optDeps;
		if (auto [d, ok] = Dependencies.GetValue(); ok) deps = d;
		if (auto [d, ok] = PeerDependencies.GetValue(); ok) peerDeps = d;
		if (auto [d, ok] = OptionalDependencies.GetValue(); ok) optDeps = d;
		collections::Set<std::string> names;
		for (auto& [name, _] : deps) names.Add(name);
		for (auto& [name, _] : peerDeps) names.Add(name);
		for (auto& [name, _] : optDeps) names.Add(name);
		return names;
	}
};

struct ContentMapperFields {
	Expected<std::vector<std::string>> Exec;            // "exec"
	Expected<std::vector<std::string>> CompilerOptions; // "compilerOptions"
	Expected<bool> DynamicConfig;                       // "dynamicConfig"
};

struct Fields : HeaderFields, PathFields, DependencyFields {
	Expected<ContentMapperFields> ContentMapper;  // "typescript.contentMapper"
};

// Parse — packagejson.go. Returns (fields, ok); ok=false on malformed JSON
// (Go's json.Unmarshal error), which maps to Parseable=false upstream.
std::pair<Fields, bool> Parse(std::string_view data);

// --- cache.go ---

struct diagnosticAndArgs {
	const DiagnosticMessage* message;
	std::vector<std::string> args;
};

inline const semver::Version& typeScriptVersion() {
	static const semver::Version v = semver::MustParse(version());
	return v;
}

struct VersionPaths {
	std::string Version;
	collections::OrderedMap<std::string, JSONValue>* pathsJSON;
	std::unique_ptr<collections::OrderedMap<std::string,
	                                      std::vector<std::string>>>
	    pathsStorage;
	collections::OrderedMap<std::string, std::vector<std::string>>* paths =
	    nullptr;

	bool Exists() const { return !Version.empty() && pathsJSON != nullptr; }

	// GetPaths — lazily converts pathsJSON to the paths map.
	collections::OrderedMap<std::string, std::vector<std::string>>*
	GetPaths() {
		if (!Exists()) return nullptr;
		if (paths != nullptr) return paths;
		pathsStorage = std::make_unique<
		    collections::OrderedMap<std::string, std::vector<std::string>>>();
		paths = pathsStorage.get();
		pathsStorage->keys.reserve(pathsJSON->Size());
		for (auto& key : pathsJSON->Keys()) {
			auto [value, ok] = pathsJSON->Get(key);
			if (!ok || value->type != JSONValueType::Array) continue;
			auto* arr = value->AsArray();
			std::vector<std::string> slice(arr->size());
			for (size_t i = 0; i < arr->size(); i++) {
				if ((*arr)[i].type != JSONValueType::String) continue;
				slice[i] = (*arr)[i].str;
			}
			pathsStorage->Set(key, std::move(slice));
		}
		return paths;
	}
};

struct PackageJson : Fields {
	bool Parseable = false;
	VersionPaths versionPaths;
	std::vector<diagnosticAndArgs> versionTraces;
	std::once_flag once;

	// GetVersionPaths — cache.go.
	VersionPaths* GetVersionPaths(
	    const std::function<void(const DiagnosticMessage*,
	                             const std::vector<std::string>&)>& trace) {
		std::call_once(once, [&] {
			if (Fields::TypesVersions.type == JSONValueType::NotPresent) {
				versionTraces.push_back(
				    {X_package_json_does_not_have_a_0_field,
				     {"typesVersions"}});
				return;
			}
			if (Fields::TypesVersions.type != JSONValueType::Object) {
				versionTraces.push_back(
				    {Expected_type_of_0_field_in_package_json_to_be_1_got_2,
				     {"typesVersions", "object",
				      JSONValueTypeString(Fields::TypesVersions.type)}});
				return;
			}

			versionTraces.push_back(
			    {X_package_json_has_a_typesVersions_field_with_version_specific_path_mappings,
			     {"typesVersions"}});

			auto* obj = Fields::TypesVersions.AsObject();
			for (auto& key : obj->Keys()) {
				auto [value, ok] = obj->Get(key);
				if (!ok) continue;
				auto [keyRange, rok] = semver::TryParseVersionRange(key);
				if (!rok) {
					versionTraces.push_back(
					    {X_package_json_has_a_typesVersions_entry_0_that_is_not_a_valid_semver_range,
					     {key}});
					continue;
				}
				if (keyRange.Test(&typeScriptVersion())) {
					if (value->type != JSONValueType::Object) {
						versionTraces.push_back(
						    {Expected_type_of_0_field_in_package_json_to_be_1_got_2,
						     {"typesVersions['" + key + "']", "object",
						      JSONValueTypeString(value->type)}});
						return;
					}
					versionPaths.Version = key;
					versionPaths.pathsJSON = value->AsObject();
					return;
				}
			}

			versionTraces.push_back(
			    {X_package_json_does_not_have_a_typesVersions_entry_that_matches_version_0,
			     {std::string{versionMajorMinor()}}});
		});
		if (trace) {
			for (auto& msg : versionTraces) {
				trace(msg.message, msg.args);
			}
		}
		return &versionPaths;
	}
};

struct InfoCacheEntry {
	std::string PackageDirectory;
	bool DirectoryExists = false;
	std::shared_ptr<PackageJson> Contents;

	bool Exists() const { return Contents != nullptr; }
	PackageJson* GetContents() const { return Contents.get(); }
	const std::string& GetDirectory() const { return PackageDirectory; }

	// WithPackageDirectory — cache.go: corrected shallow copy when the
	// package-directory path diverges from the caller's candidate.
	std::shared_ptr<InfoCacheEntry> WithPackageDirectory(
	    const std::string& packageDirectory) const;
};

// InfoCache — packageJsonInfo cache keyed by canonical path of the
// package.json file (ToPath semantics).
struct InfoCache {
	collections::SyncMap<std::string, std::shared_ptr<InfoCacheEntry>> cache;
	std::string currentDirectory;
	bool useCaseSensitiveFileNames = false;

	InfoCache(std::string_view currentDirectory_,
	          bool useCaseSensitiveFileNames_)
	    : currentDirectory(currentDirectory_),
	      useCaseSensitiveFileNames(useCaseSensitiveFileNames_) {}

	std::shared_ptr<InfoCacheEntry> Get(const std::string& packageJsonPath) {
		auto key = tspath::toPath(packageJsonPath, currentDirectory,
		                          useCaseSensitiveFileNames);
		if (auto [value, ok] = cache.Load(key); ok) {
			return value;
		}
		return nullptr;
	}

	std::shared_ptr<InfoCacheEntry> Set(
	    const std::string& packageJsonPath,
	    std::shared_ptr<InfoCacheEntry> info) {
		auto key = tspath::toPath(packageJsonPath, currentDirectory,
		                          useCaseSensitiveFileNames);
		auto [actual, _] = cache.LoadOrStore(key, info);
		return actual;
	}

	void Range(const std::function<bool(
	               const std::string&,
	               const std::shared_ptr<InfoCacheEntry>&)>& f) {
		cache.Range([&](const std::string& k,
	                    const std::shared_ptr<InfoCacheEntry>& v) {
			return f(k, v);
		});
	}

	// cache.go:230 Clone — a new cache table holding the same entries.
	std::shared_ptr<InfoCache> Clone() {
		auto clone = std::make_shared<InfoCache>(currentDirectory,
		                                       useCaseSensitiveFileNames);
		cache.Range([&](const std::string& k,
		                const std::shared_ptr<InfoCacheEntry>& v) {
			clone->cache.Store(k, v);
			return true;
		});
		return clone;
	}
};

}  // namespace tsc::packagejson
