// === dep decls — owned by lsp ===
// Faithful port of the pure URI helpers from lsp.go; see lsproto.h.
#include "internal/lsp/lsproto/lsproto.h"

#include "internal/bundled/bundled.h"

namespace tsc::lsproto {

namespace {

// percentDecode decodes %XX sequences like Go's url.QueryUnescape/PathUnescape.
// Returns false on malformed input (Go url.Parse would error).
bool percentDecode(std::string_view in, std::string* out) {
	out->clear();
	out->reserve(in.size());
	for (size_t i = 0; i < in.size(); i++) {
		if (in[i] == '%') {
			if (i + 2 >= in.size()) {
				return false;
			}
			auto hex = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			};
			int hi = hex(in[i + 1]);
			int lo = hex(in[i + 2]);
			if (hi < 0 || lo < 0) {
				return false;
			}
			*out += (char)((hi << 4) | lo);
			i += 2;
		} else {
			*out += in[i];
		}
	}
	return true;
}

} // namespace

// fixWindowsURIPath (lsp.go:57).
std::string fixWindowsURIPath(std::string_view path) {
	if (!path.empty() && path.front() == '/') {
		std::string_view rest = path.substr(1);
		if (rest.size() >= 2 && tspath::isVolumeCharacter(rest[0]) && rest[1] == ':') {
			return std::string(rest);
		}
	}
	return std::string(path);
}

// DocumentUri.FileName (lsp.go:19).
std::string DocumentUri::FileName() const {
	if (bundled::IsBundled(v)) {
		return v;
	}
	if (v.rfind("file://", 0) == 0) {
		// Minimal url.Parse for file: URLs: authority up to the next '/',
		// path from there (percent-decoded) up to '?'/'#'.
		std::string_view rest = std::string_view(v).substr(7);
		size_t pathStart = rest.find_first_of("/?#");
		std::string host;
		std::string_view pathPart;
		if (pathStart == std::string_view::npos) {
			host = std::string(rest);
			pathPart = "";
		} else {
			host = std::string(rest.substr(0, pathStart));
			pathPart = rest.substr(pathStart);
			if (!pathPart.empty() && pathPart.front() != '/') {
				pathPart = ""; // '?' or '#' with no path
			} else {
				size_t pathEnd = pathPart.find_first_of("?#");
				if (pathEnd != std::string_view::npos) {
					pathPart = pathPart.substr(0, pathEnd);
				}
			}
		}
		std::string decoded;
		if (!percentDecode(pathPart, &decoded)) {
			TSC_UNREACHABLE(gostd::sprintf("invalid file URI: %s", {v}).c_str());
		}
		if (!host.empty()) {
			return "//" + host + decoded;
		}
		return fixWindowsURIPath(decoded);
	}

	// Leave all other URIs escaped so we can round-trip them.

	auto colon = v.find(':');
	if (colon == std::string::npos) {
		TSC_UNREACHABLE(gostd::sprintf("invalid URI: %s", {v}).c_str());
	}
	std::string scheme = v.substr(0, colon);
	std::string_view path = std::string_view(v).substr(colon + 1);

	std::string authority = "ts-nul-authority";
	if (path.size() >= 2 && path[0] == '/' && path[1] == '/') {
		std::string_view rest = path.substr(2);
		auto slash = rest.find('/');
		if (slash == std::string_view::npos) {
			TSC_UNREACHABLE(gostd::sprintf("invalid URI: %s", {v}).c_str());
		}
		authority = std::string(rest.substr(0, slash));
		path = rest.substr(slash + 1);
	}

	return "^/" + scheme + "/" + authority + "/" + std::string(path);
}

// DocumentUri.Path (lsp.go:52).
tspath::Path DocumentUri::Path(bool useCaseSensitiveFileNames) const {
	std::string fileName = FileName();
	return tspath::toPath(fileName, "", useCaseSensitiveFileNames);
}

} // namespace tsc::lsproto
