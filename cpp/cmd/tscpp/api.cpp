// tsc/cmd/tsc/api.go — `tsc --api` subcommand: run the API session over
// STDIO (MessagePack) or a pipe. Function-by-function port: parseAPIFlags,
// runAPI.

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#include "internal/win32/w32compat.h"
#else
#include <unistd.h>
#endif

#include "internal/api/server.h"
#include "internal/bundled/bundled.h"
#include "internal/gostd/gostd.h"

#include "cmd/tscpp/notify.h"
#include "cmd/tscpp/stdio.h"
#include "cmd/tscpp/sys.h"

using namespace tsc;

namespace {

// ---------------------------------------------------------------------------
// flag.FlagSet("api", flag.ContinueOnError) — the six registered flags,
// Go flag-pkg parse semantics (api.go:17-38).
// ---------------------------------------------------------------------------

struct apiFlags {
	std::string cwd;
	std::string pipePath;
	std::string callbacks;
	bool async = false;
	bool timing = false;
	bool runExternalCode = false;
};

// Usage of api: — api.go:26-37 flag registration order (Go prints flags
// sorted lexically; -cwd's default is dynamic: core.Must(os.Getwd())).
std::string apiUsage(const std::string& cwdDefault) {
	return "Usage of api:\n"
	       "  -async\n"
	       "    \tuse JSON-RPC protocol instead of MessagePack (for async API)\n"
	       "  -callbacks string\n"
	       "    \tcomma-separated list of FS callbacks to enable (readFile,fileExists,directoryExists,getAccessibleEntries,realpath)\n"
	       "  -cwd string\n"
	       "    \tcurrent working directory (default \"" + cwdDefault +
	       "\")\n"
	       "  -pipe string\n"
	       "    \tuse named pipe or Unix domain socket for communication instead of stdio\n"
	       "  -runExternalCode\n"
	       "    \tallow projects to execute configured external plugins\n"
	       "  -timing\n"
	       "    \tcollect per-request server processing time, folded into the client's timing snapshot\n";
}

bool apiFlagBoolValue(const std::string& v, bool* out) {
	if (v == "1" || v == "t" || v == "T" || v == "true" || v == "TRUE" ||
	    v == "True") {
		*out = true;
		return true;
	}
	if (v == "0" || v == "f" || v == "F" || v == "false" || v == "FALSE" ||
	    v == "False") {
		*out = false;
		return true;
	}
	return false;
}

// parseAPIFlags — api.go:26.
bool parseAPIFlags(const std::vector<std::string>& args, apiFlags* f) {
	char buf[4096];
	if (::getcwd(buf, sizeof(buf)) != nullptr) {
		f->cwd = tspath::normalizePath(buf); // core.Must(os.Getwd())
	}
	const std::string usage = apiUsage(f->cwd);
	auto fail = [&usage](const std::string& msg) {
		std::fprintf(stderr, "%s\n%s", msg.c_str(), usage.c_str());
		return false;
	};
	for (size_t i = 0; i < args.size(); i++) {
		const std::string& arg = args[i];
		if (arg.size() < 2 || arg[0] != '-') break;
		size_t numMinuses = arg[1] == '-' ? 2 : 1;
		std::string name = arg.substr(numMinuses);
		if (name.empty() || name[0] == '-' || name[0] == '=') {
			if (arg == "--") break;
			return fail("bad flag syntax: " + arg);
		}
		std::string value;
		bool hasValue = false;
		if (auto eq = name.find('='); eq != std::string::npos) {
			value = name.substr(eq + 1);
			name = name.substr(0, eq);
			hasValue = true;
		}
		if (name == "h" || name == "help") {
			std::fprintf(stderr, "%s", usage.c_str());
			return false; // flag.ErrHelp
		}
		if (name != "cwd" && name != "pipe" && name != "callbacks" &&
		    name != "async" && name != "timing" &&
		    name != "runExternalCode") {
			return fail("flag provided but not defined: -" + name);
		}
		if (name == "async" || name == "timing" ||
		    name == "runExternalCode") { // bool flags: =value optional
			bool* target = name == "async"      ? &f->async
			               : name == "timing"   ? &f->timing
			                                : &f->runExternalCode;
			if (hasValue) {
				if (!apiFlagBoolValue(value, target)) {
					return fail("invalid value \"" + value +
					            "\" for flag -" + name + ": parse error");
				}
			} else {
				*target = true;
			}
			continue;
		}
		if (!hasValue) {
			if (i + 1 < args.size()) {
				value = args[++i];
			} else {
				return fail("flag needs an argument: -" + name);
			}
		}
		if (name == "cwd") f->cwd = value;
		else if (name == "pipe") f->pipePath = value;
		else f->callbacks = value;
	}
	return true;
}

// strings.Split — splits on every separator (no empties dropped).
std::vector<std::string> stringsSplit(const std::string& s,
                                      const std::string& sep) {
	std::vector<std::string> out;
	size_t pos = 0;
	for (;;) {
		auto i = s.find(sep, pos);
		if (i == std::string::npos) {
			out.push_back(s.substr(pos));
			return out;
		}
		out.push_back(s.substr(pos, i - pos));
		pos = i + sep.size();
	}
}

} // namespace

// runAPI — api.go:41.
int runAPI(const std::vector<std::string>& args) {
	apiFlags flags;
	if (!parseAPIFlags(args, &flags)) {
		return 2;
	}

	auto defaultLibraryPath = bundled::LibPath();

	// Parse callbacks list
	std::vector<std::string> callbacksList;
	if (!flags.callbacks.empty()) {
		callbacksList = stringsSplit(flags.callbacks, ",");
	}

	static tsc::cmd_stdio::stderrWriter stderrW;

	api::StdioServerOptions options{
	    .In = nullptr,
	    .Out = nullptr,
	    .Err = &stderrW,
	    .Cwd = flags.cwd,
	    .DefaultLibraryPath = defaultLibraryPath,
	    .PipePath = {},
	    .Callbacks = callbacksList,
	    .Async = flags.async,
	    .CollectTiming = flags.timing,
	    .RunExternalCode = flags.runExternalCode,
	    .ContentMapperSpawner = std::shared_ptr<contentmapper::Spawner>(
	        newSystem()),
	};
	if (!flags.pipePath.empty()) {
		options.PipePath = flags.pipePath;
	} else {
		options.In =
		    std::make_shared<tsc::cmd_stdio::osStdin>();
		options.Out =
		    std::make_shared<tsc::cmd_stdio::osStdout>();
	}

	auto* s = api::NewStdioServer(&options);

	auto [ctx, stop] = tsc::cmd_notify::signalNotifyContext();

	if (auto err = s->Run(ctx)) {
		std::fprintf(stderr, "%s\n", err->Error().c_str());
		return 1;
	}
	return 0;
}
