// Port of tsc/internal/core/nodemodules.go — node core module name tables.
#pragma once

#include <string>
#include <unordered_set>

namespace tsc {

inline const std::unordered_set<std::string> UnprefixedNodeCoreModules = {
	"assert", "assert/strict", "async_hooks", "buffer", "child_process",
	"cluster", "console", "constants", "crypto", "dgram",
	"diagnostics_channel", "dns", "dns/promises", "domain", "events", "fs",
	"fs/promises", "http", "http2", "https", "inspector",
	"inspector/promises", "module", "net", "os", "path", "path/posix",
	"path/win32", "perf_hooks", "process", "punycode", "querystring",
	"readline", "readline/promises", "repl", "stream", "stream/consumers",
	"stream/promises", "stream/web", "string_decoder", "sys", "timers",
	"timers/promises", "tls", "trace_events", "tty", "url", "util",
	"util/types", "v8", "vm", "wasi", "worker_threads", "zlib",
};

// require('module').builtinModules.filter(x => x.startsWith('node:'))
inline const std::unordered_set<std::string> ExclusivelyPrefixedNodeCoreModules = {
	"node:quic", "node:sea", "node:sqlite", "node:test",
	"node:test/reporters",
};

// NodeCoreModules() — unprefixed names plus their "node:" forms plus the
// exclusively-prefixed set.
inline const std::unordered_set<std::string>& nodeCoreModules() {
	static const std::unordered_set<std::string> modules = [] {
		std::unordered_set<std::string> m;
		m.reserve(UnprefixedNodeCoreModules.size() * 2 +
		          ExclusivelyPrefixedNodeCoreModules.size());
		for (const auto& unprefixed : UnprefixedNodeCoreModules) {
			m.insert(unprefixed);
			m.insert("node:" + unprefixed);
		}
		for (const auto& p : ExclusivelyPrefixedNodeCoreModules) {
			m.insert(p);
		}
		return m;
	}();
	return modules;
}

inline bool nodeCoreModulesContains(std::string_view name) {
	return nodeCoreModules().count(std::string(name)) != 0;
}

inline std::string_view nonRelativeModuleNameForTypingCache(
    std::string_view moduleName) {
	if (nodeCoreModulesContains(moduleName)) {
		return "node";
	}
	return moduleName;
}

}  // namespace tsc
