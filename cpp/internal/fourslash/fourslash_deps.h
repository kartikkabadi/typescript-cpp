// === slice: fourslash (dep declarations) ===
// Declarations for sibling packages owned by other in-flight slices, used by
// the fourslash test harness:
//   - tsctests: GetFileMapWithBuild (owned by execute/tsc)
//   - cmp:      go-cmp's cmp.Option/cmp.Diff — implemented here over the
//               JSON marshal surface (used only for failure messages and
//               field-path ignores).
// The project (parsecache/refcountcache/configfileregistry/session/
// projectcollection) and lsp (server) slices have landed: their real headers
// are included directly below. The ConfigFileRegistry test accessors
// (TestConfigEntry/TestConfigFileNamesEntry/ForEachTestConfigEntry/...)
// are ported on the real struct in project/configfileregistry.h.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/fourslash/goutil.h"
#include "internal/gostd/gostd.h"
#include "internal/json/json.h"
#include "internal/jsonrpc/jsonrpc.h"
#include "internal/lsp/lsp.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/project/configfileregistry.h"
#include "internal/project/parsecache.h"
#include "internal/project/project.h"
#include "internal/project/projectcollection.h"
#include "internal/project/session.h"
#include "internal/project/snapshot.h"
#include "internal/vfs/vfs.h"

namespace tsc {
struct CompilerOptions;
} // namespace tsc

namespace tsc::tsctests {

// GetFileMapWithBuild — owned by execute/tsc (tsc tests helper). Runs `tsc
// --build` over a map FS so stateBaseline tests can stage dependency emit.
gostd::Error getFileMapWithBuild(
	std::unordered_map<std::string, std::any>& testfs,
	const std::vector<std::string>& args);

} // namespace tsc::tsctests

// ---------------------------------------------------------------------------
// cmp — go-cmp minimal surface: Option (ignored field paths) and Diff.
// ---------------------------------------------------------------------------
namespace tsc::cmp {

// Option carries the set of ignored field paths (".Field" segments joined).
using Option = std::unordered_set<std::string>;

// ignorePaths — builds a cmp.Option-like value matching fourslash.go's
// ignorePaths(".Kind", ".SortText", ...).
inline Option ignorePaths(std::initializer_list<std::string> paths) {
	return Option(paths.begin(), paths.end());
}

// domPathsIgnored reports whether a struct member name is ignored by opts at
// this level. Ignore paths name Go struct fields (".Kind"); the marshaled DOM
// keys are the JSON wire names ("kind"), so both cases are tried.
inline bool pathIgnored(const Option& opts, std::string_view fieldName) {
	std::string key = "." + std::string(fieldName);
	if (opts.count(key) != 0) {
		return true;
	}
	if (key.size() > 1) {
		key[1] = static_cast<char>(
		    std::toupper(static_cast<unsigned char>(key[1])));
	}
	return opts.count(key) != 0;
}

// domEqual — deep equality of two JSON DOM trees honoring ignored
// top-level object member names.
inline bool domEqual(const json::Dom& a, const json::Dom& b,
                     const Option& opts) {
	if (a.kind != b.kind) return false;
	using K = json::Dom::K;
	switch (a.kind) {
	case K::Null: return true;
	case K::Bool: return a.boolVal == b.boolVal;
	case K::Number: return a.strVal == b.strVal;
	case K::String: return a.strVal == b.strVal;
	case K::Array: {
		if (a.arr.size() != b.arr.size()) return false;
		for (size_t i = 0; i < a.arr.size(); i++) {
			if (!domEqual(a.arr[i], b.arr[i], {})) return false;
		}
		return true;
	}
	case K::Object: {
		std::vector<std::pair<std::string, const json::Dom*>> aMembers;
		for (auto& [k, v] : a.obj) {
			if (!pathIgnored(opts, k)) aMembers.push_back({k, &v});
		}
		size_t bCount = 0;
		for (auto& [k, v] : b.obj) {
			if (!pathIgnored(opts, k)) bCount++;
		}
		if (aMembers.size() != bCount) return false;
		for (auto& [k, av] : aMembers) {
			const json::Dom* bv = json::objGet(b, k);
			if (bv == nullptr || pathIgnored(opts, k)) {
				if (bv == nullptr) return false;
				continue;
			}
			if (!domEqual(*av, *bv, opts)) return false;
		}
		return true;
	}
	}
	return false;
}

namespace detail {
// Marshal an arbitrary value to a parsed Dom. Types without marshal support
// (std::nullptr_t) produce null.
template <typename T>
std::pair<json::Dom, gostd::Error> marshalToDom(const T& v) {
	if constexpr (std::is_same_v<T, std::nullptr_t>) {
		return json::parse("null");
	} else {
		auto [out, err] = json::marshal(v);
		if (!err.empty()) {
			return {json::Dom{}, gostd::newError(err)};
		}
		return json::parse(out);
	}
}
} // namespace detail

// Diff — cmp.Diff(x, y, opts...): "" when equal, else a compact diff string.
// Equality is judged over the JSON-marshaled form (equivalent to cmp's
// structural comparison for the message purposes fourslash uses it for).
template <typename T, typename U>
std::string diff(const T& actual, const U& expected, const Option& opts = {}) {
	auto [da, ea] = detail::marshalToDom(actual);
	auto [db, eb] = detail::marshalToDom(expected);
	if (ea || eb) {
		return gostd::sprintf("marshal error: %v / %v",
		                      {ea ? ea->Error() : "", eb ? eb->Error() : ""});
	}
	if (domEqual(da, db, opts)) {
		return "";
	}
	auto sa = json::marshalIndent(actual, "", "  ").first;
	std::string sb;
	if constexpr (std::is_same_v<U, std::nullptr_t>) {
		sb = "null";
	} else {
		sb = json::marshalIndent(expected, "", "  ").first;
	}
	return gostd::sprintf("(-actual +expected):\n- %v\n+ %v", {sa, sb});
}
} // namespace tsc::cmp
