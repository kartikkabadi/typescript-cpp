// Port of tsc/internal/lsp/lsproto/lsp.go — minimal subset needed by the
// ls-coreA slice. === slice: ls-coreA ===

#include "internal/lsp/lsproto/lsproto.h"

#include <cctype>
#include <cstdio>

#include "internal/bundled/bundled.h"

namespace tsc::lsproto {

namespace {

// Go `panic(...)` — terminate like tsc::tscUnreachable (ast.h), but lsproto
// must not depend on the ast package.
[[noreturn]] void panicInvalidUri(const std::string& msg, const std::string& uri) {
	std::fprintf(stderr, "%s: %s\n", msg.c_str(), uri.c_str());
	std::fflush(nullptr);
	std::_Exit(2);
}

// Minimal url.Parse for the `file://` URIs FileName() handles: splits
// authority/path, percent-decodes the path. Go's url.Parse also handles
// query/fragment; paths stop at '?' or '#'.
struct ParsedFileUri {
	std::string host;
	std::string path; // percent-decoded
	bool ok = false;
};

int hexDigit(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// unescape — url.PathUnescape for a file URI path component.
std::pair<std::string, bool> unescapePath(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size();) {
		char c = s[i];
		if (c == '%') {
			if (i + 2 >= s.size()) {
				return {"", false};
			}
			int hi = hexDigit(s[i + 1]), lo = hexDigit(s[i + 2]);
			if (hi < 0 || lo < 0) {
				return {"", false};
			}
			out += static_cast<char>(hi * 16 + lo);
			i += 3;
		} else {
			out += c;
			++i;
		}
	}
	return {out, true};
}

// url.Parse restricted to `file://...` URIs.
ParsedFileUri parseFileUri(std::string_view uri) {
	ParsedFileUri out;
	std::string_view rest = uri.substr(std::string_view("file://").size());
	// authority ends at the first '/', '?', or '#'.
	size_t authorityEnd = rest.size();
	for (size_t i = 0; i < rest.size(); ++i) {
		char c = rest[i];
		if (c == '/' || c == '?' || c == '#') {
			authorityEnd = i;
			break;
		}
	}
	out.host = std::string(rest.substr(0, authorityEnd));
	std::string_view rawPath = rest.substr(authorityEnd);
	// path ends at '?' or '#'.
	size_t pathEnd = rawPath.size();
	for (size_t i = 0; i < rawPath.size(); ++i) {
		if (rawPath[i] == '?' || rawPath[i] == '#') {
			pathEnd = i;
			break;
		}
	}
	auto [decoded, ok] = unescapePath(rawPath.substr(0, pathEnd));
	if (!ok) {
		return out;
	}
	out.path = std::move(decoded);
	out.ok = true;
	return out;
}

// lsp.go:57 fixWindowsURIPath.
std::string fixWindowsURIPath(const std::string& path) {
	if (!path.empty() && path[0] == '/') {
		std::string_view rest = std::string_view(path).substr(1);
		if (rest.size() >= 2 && tspath::isVolumeCharacter(rest[0]) &&
		    rest[1] == ':') {
			return std::string(rest);
		}
	}
	return path;
}

} // namespace

// lsp.go:20
std::string DocumentUri::FileName() const {
	if (bundled::IsBundled(Uri)) {
		return Uri;
	}
	if (Uri.starts_with("file://")) {
		ParsedFileUri parsed = parseFileUri(Uri);
		if (!parsed.ok) {
			panicInvalidUri("invalid file URI", Uri);
		}
		if (!parsed.host.empty()) {
			return "//" + parsed.host + parsed.path;
		}
		return fixWindowsURIPath(parsed.path);
	}

	// Leave all other URIs escaped so we can round-trip them.

	auto colon = Uri.find(':');
	if (colon == std::string::npos) {
		panicInvalidUri("invalid URI", Uri);
	}
	std::string scheme = Uri.substr(0, colon);
	std::string path = Uri.substr(colon + 1);

	std::string authority = "ts-nul-authority";
	if (path.starts_with("//")) {
		std::string_view rest = std::string_view(path).substr(2);
		auto slash = rest.find('/');
		if (slash == std::string_view::npos) {
			panicInvalidUri("invalid URI", Uri);
		}
		authority = std::string(rest.substr(0, slash));
		path = std::string(rest.substr(slash + 1));
	}

	return "^/" + scheme + "/" + authority + "/" + path;
}

// lsp.go:52
tspath::Path DocumentUri::Path(bool useCaseSensitiveFileNames) const {
	std::string fileName = FileName();
	return tspath::toPath(fileName, "", useCaseSensitiveFileNames);
}

// lsp.go:287 clientCapabilitiesKey.
namespace {
const char clientCapabilitiesKey = 0;
const ResolvedClientCapabilities emptyClientCapabilities{};
} // namespace

// lsp.go:289
ContextPtr WithClientCapabilities(const ContextPtr& ctx,
                                  const ResolvedClientCapabilities* caps) {
	return withContextValue(ctx, &clientCapabilitiesKey,
	                        const_cast<ResolvedClientCapabilities*>(caps));
}

// lsp.go:293
const ResolvedClientCapabilities* GetClientCapabilities(const ContextPtr& ctx) {
	if (ctx) {
		if (const std::any* v = ctx->value(&clientCapabilitiesKey)) {
			if (auto* caps = std::any_cast<ResolvedClientCapabilities*>(v)) {
				if (*caps != nullptr) {
					return *caps;
				}
			}
		}
	}
	return &emptyClientCapabilities;
}

// lsp.go:302
MarkupKind PreferredMarkupKind(const std::vector<MarkupKind>& formats) {
	if (!formats.empty()) {
		return formats[0];
	}
	return MarkupKindPlainText;
}

// === free-function spellings (util.go / lsp.go) — used by ls/autoimport/change ===

// util.go:11 — ComparePositions.
int ComparePositions(const Position& pos, const Position& other) {
	return pos.Compare(&other);
}

// util.go:17 — CompareRanges.
int CompareRanges(const Range& lsRange, const Range& other) {
	return lsRange.Compare(&other);
}

// lsp.go:19-51 — documentUriFileName.
std::string documentUriFileName(const DocumentUri& uri) {
	return uri.FileName();
}

// lsp.go:52 — documentUriPath.
tspath::Path documentUriPath(const DocumentUri& uri, bool useCaseSensitiveFileNames) {
	return uri.Path(useCaseSensitiveFileNames);
}

} // namespace tsc::lsproto
