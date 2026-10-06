// === slice: fourslash (dep declarations) ===
// Declarations for sibling packages owned by other in-flight slices, used by
// the fourslash test harness:
//   - project:  ParseCache/RefCountCacheOptions/NewParseCache
//               (parsecache.go), ConfigFileRegistry + TestConfig*Entry
//               (configfileregistry.go), Session::Snapshot (session.go:1018),
//               ProjectCollection::ConfigFileRegistry (projectcollection.go:72)
//   - lsp:      Reader/Writer/ToReader/ToWriter (server.go:105-168), Server,
//               ServerOptions, NewServer, InitComplete (server.go:42-262),
//               SetCompilerOptionsForInferredProjects
//   - tsctests: GetFileMapWithBuild (owned by execute/tsc)
//   - cmp:      go-cmp's cmp.Option/cmp.Diff — implemented here over the
//               JSON marshal surface (used only for failure messages and
//               field-path ignores).
// lsp::ToReader/ToWriter are real ports (leaf glue over lsproto's base
// reader/writer); everything else is TSC_UNREACHABLE("<name> — owned by
// <slice>") in fourslash_deps.cpp. Delete when owner slices land.
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
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/project/project.h"
#include "internal/vfs/vfs.h"

namespace tsc {
struct CompilerOptions;
} // namespace tsc

namespace tsc::project {

// parsecache.go:72 — `type ParseCache = RefCountCache[...]`. Owned by project.
struct ParseCache;

// refcountcache.go:15 — RefCountCacheOptions.
struct RefCountCacheOptions {
	bool DisableDeletion = false;
};

// parsecache.go:74 — NewParseCache. Dep-stub (project slice).
ParseCache* newParseCache(const RefCountCacheOptions& options);

// configfileregistry.go:160 — TestConfigEntry ("For testing" accessor).
struct TestConfigEntry {
	std::string FileName;
	goseq::Seq<project::ID> RetainingProjects;
	goseq::Seq<tspath::Path> RetainingOpenFiles;
	goseq::Seq<tspath::Path> RetainingConfigs;
};

// configfileregistry.go:195 — TestConfigFileNamesEntry.
struct TestConfigFileNamesEntry {
	std::string NearestConfigFileName;
	std::unordered_map<std::string, std::string> Ancestors;
};

// configfileregistry.go:15 — ConfigFileRegistry. Only the test accessors
// fourslash uses are declared; all bodies dep-stubbed (project slice).
struct ConfigFileRegistry {
	virtual ~ConfigFileRegistry() = default;
	// ForEachTestConfigEntry — configfileregistry.go:168.
	virtual void ForEachTestConfigEntry(
		const std::function<void(tspath::Path, TestConfigEntry*)>& cb) = 0;
	// GetTestConfigEntry — configfileregistry.go:182.
	virtual std::shared_ptr<TestConfigEntry>
	GetTestConfigEntry(tspath::Path path) = 0;
	// ForEachTestConfigFileNamesEntry — configfileregistry.go:202.
	virtual void ForEachTestConfigFileNamesEntry(
		const std::function<void(tspath::Path, TestConfigFileNamesEntry*)>&
		    cb) = 0;
	// GetTestConfigFileNamesEntry — configfileregistry.go:214.
	virtual std::shared_ptr<TestConfigFileNamesEntry>
	GetTestConfigFileNamesEntry(tspath::Path path) = 0;
};

// session.go:1018 — (s *Session).Snapshot. The Session dep class carries no
// snapshot member, so this accessor is a free function (project slice).
project::Snapshot* sessionSnapshot(project::Session* s);

// projectcollection.go:72 — (c *ProjectCollection).ConfigFileRegistry
// (project slice).
ConfigFileRegistry* projectCollectionConfigFileRegistry(ProjectCollection* c);

} // namespace tsc::project

namespace tsc::lsp {

// server.go:105 — Reader/Writer interfaces (LSP message-level I/O).
struct Reader {
	virtual ~Reader() = default;
	virtual std::pair<std::shared_ptr<lsproto::Message>, gostd::Error>
	read() = 0;
};
struct Writer {
	virtual ~Writer() = default;
	virtual gostd::Error write(lsproto::Message* msg) = 0;
};

// server.go:150 — ToReader (real port over lsproto::NewBaseReader).
std::unique_ptr<Reader> toReader(gostd::io::Reader* r);
// server.go:162 — ToWriter (real port over lsproto::NewBaseWriter).
std::unique_ptr<Writer> toWriter(gostd::io::Writer* w);

// server.go:42 — ServerOptions.
struct ServerOptions {
	Reader* In = nullptr;
	Writer* Out = nullptr;
	gostd::io::Writer* Err = nullptr;

	std::string Cwd;
	std::shared_ptr<vfs::FS> FS;
	std::string DefaultLibraryPath;
	std::string TypingsLocation;
	project::ParseCache* ParseCache = nullptr;
	std::function<std::pair<std::vector<uint8_t>, gostd::Error>(
		const std::string&, const std::vector<std::string>&)>
		NpmInstall;
	// Spawn launches a child process, returning its stdio as an
	// io.ReadWriteCloser (Read is its stdout, Write is its stdin). It is null
	// when the host cannot spawn processes. Currently used for content mappers.
	std::function<std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>,
	                        gostd::Error>(
		const std::vector<std::string>&, const std::string&,
		gostd::io::Writer*)>
		Spawn;
	gostd::Duration ProgressDelay{}; // delay before showing progress UI; 0 means no delay
	std::function<void(int)> SetParentProcessID;
};

// server.go:171 — Server. All methods dep-stubbed (lsp slice).
struct Server {
	virtual ~Server() = default;

	// server.go:859 — Run.
	virtual gostd::Error run(gostd::Context ctx) = 0;
	// server.go:255 — Session.
	virtual project::Session* session() = 0;
	// server.go:260 — InitComplete: channel closed when initialization has
	// finished.
	virtual gostd::Chan<int>* initComplete() = 0;
	// SetCompilerOptionsForInferredProjects — used by lsptestutil.LSPClient.
	virtual gostd::Error setCompilerOptionsForInferredProjects(
		gostd::Context ctx, tsc::CompilerOptions* options) = 0;
};

// server.go:60 — NewServer. Dep-stub (lsp slice).
Server* newServer(ServerOptions* opts);

} // namespace tsc::lsp

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
// this level (".Name" top-level; nested paths unsupported by fourslash uses).
inline bool pathIgnored(const Option& opts, std::string_view fieldName) {
	return opts.count("." + std::string(fieldName)) != 0;
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
