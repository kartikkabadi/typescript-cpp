// === dep decls — owned by project ===
// Implements the pure ID helpers from project.go faithfully and stubs every
// stateful type with TSC_UNREACHABLE. See project.h.
#include "internal/project/project.h"

#include "internal/gostd/gostd.h"
#include "internal/ast/ast.h" // OnceFlag
#include "internal/core/text.h"
#include "internal/json/json.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/xxh3/xxh3.h"

namespace tsc::project {

// ID.Configured (project.go:57).
std::pair<ConfiguredProjectID, bool> ID::Configured() const {
	return ParseConfiguredProjectID(tspath::Path(v));
}

// ParseConfiguredProjectID (project.go:61).
std::pair<ConfiguredProjectID, bool> ParseConfiguredProjectID(tspath::Path value) {
	ID id{std::move(value)};
	if (id.v.empty()) {
		return {"", false};
	}
	if (auto [_, ok] = id.Inferred(); ok) {
		return {"", false};
	}
	if (auto [_, ok] = id.Synthetic(); ok) {
		return {"", false};
	}
	return {ConfiguredProjectID(id.v), true};
}

// ID.Inferred (project.go:68).
std::pair<InferredProjectID, bool> ID::Inferred() const {
	return {InferredProjectID(inferredProjectName),
	        v == inferredProjectName};
}

// ID.Synthetic (project.go:72).
std::pair<SyntheticProjectID, bool> ID::Synthetic() const {
	return ParseSyntheticProjectID(v);
}

// SyntheticProjectID.UnmarshalJSONFrom (project.go:76) — as a free function
// taking the already-parsed JSON value.
gostd::Error syntheticProjectIDUnmarshalJSONFrom(const json::Dom& v,
                                                SyntheticProjectID* out) {
	auto [value, err] = json::asString(v, "*string");
	if (err != nullptr) {
		return err;
	}
	auto [parsed, ok2] = ParseSyntheticProjectID(value);
	if (!ok2) {
		return gostd::errorf("invalid synthetic project ID: %s", {value});
	}
	*out = parsed;
	return nullptr;
}

// NewSyntheticProjectID (project.go:45).
SyntheticProjectID NewSyntheticProjectID(int id) {
	if (id <= 0) {
		TSC_UNREACHABLE(gostd::sprintf("invalid synthetic project ID: %d", {id}).c_str());
	}
	return gostd::sprintf("%s%d", {std::string(syntheticProjectPrefix), id});
}

// ParseSyntheticProjectID (project.go:89).
std::pair<SyntheticProjectID, bool> ParseSyntheticProjectID(const std::string& value) {
	if (value.rfind(syntheticProjectPrefix, 0) != 0) {
		return {"", false};
	}
	std::string_view suffix = std::string_view(value).substr(syntheticProjectPrefix.size());
	if (suffix.empty()) {
		return {"", false};
	}
	// strconv.Atoi: optional sign then digits only.
	const char* s = suffix.data();
	const char* end = s + suffix.size();
	const char* p = s;
	if (*p == '+' || *p == '-') p++;
	if (p == end) return {"", false};
	for (const char* q = p; q != end; q++) {
		if (*q < '0' || *q > '9') return {"", false};
	}
	long id = 0;
	try {
		id = std::stol(std::string(suffix));
	} catch (...) {
		return {"", false};
	}
	if (id <= 0) {
		return {"", false};
	}
	return {NewSyntheticProjectID((int)id), true};
}

// FileChangeSummary.Clone (filechange.go:56).
FileChangeSummary FileChangeSummary::Clone() const {
	FileChangeSummary f = *this;
	f.Closed = Closed.Clone();
	f.Changed = Changed.Clone();
	f.Created = Created.Clone();
	f.Deleted = Deleted.Clone();
	return f;
}

// cachedFile (overlayfs.go:76) — a FileHandle over immutable content.
// fileBase's lazily-computed line maps are included.
struct cachedFile final : FileHandle {
	std::string fileName;
	std::string content;
	uint64_t hashHi{}, hashLo{}; // xxh3.Uint128
	::tsc::OnceFlag lineMapOnce;
	std::unique_ptr<lsconv::LSPLineMap> lineMap;
	::tsc::OnceFlag lineInfoOnce;
	std::unique_ptr<sourcemap::ECMALineInfo> lineInfo;
	bool needsReload{};
	tspath::Path realpathPath;

	std::string FileName() const override { return fileName; }
	std::string Text() const override { return content; }
	std::string OriginalText() const override { return content; }
	int32_t Version() const override { return 0; }
	bool MatchesDiskText() const override { return !needsReload; }
	bool IsOverlay() const override { return false; }
	lsconv::LSPLineMap* LSPLineMap() override {
		lineMapOnce.run([&] {
			auto m = lsconv::ComputeLSPLineStarts(content);
			lineMap.reset(m);
		});
		return lineMap.get();
	}
	sourcemap::ECMALineInfo* ECMALineInfo() override {
		lineInfoOnce.run([&] {
			auto lineStarts = computeECMALineStarts(content);
			lineInfo.reset(
			    sourcemap::CreateECMALineInfo(content, lineStarts));
		});
		return lineInfo.get();
	}
	int Kind() const override { return 0; } // core.ScriptKind::Unknown
};

// NewCachedFileHandle (overlayfs.go:90).
std::shared_ptr<FileHandle> NewCachedFileHandle(std::string fileName,
                                                std::string content) {
	auto f = std::make_shared<cachedFile>();
	f->fileName = std::move(fileName);
	f->content = std::move(content);
	auto h = xxh3::hash128(f->content);
	f->hashLo = h.Lo;
	f->hashHi = h.Hi;
	return f;
}

// --- dep stubs (all owned by project) --------------------------------------

SnapshotHost* NewSnapshotHost(SessionInit* /*init*/) {
	TSC_UNREACHABLE("project::NewSnapshotHost — owned by project slice");
}

Session* NewSession(SessionInit* /*init*/) {
	TSC_UNREACHABLE("project::NewSession — owned by project slice");
}

} // namespace tsc::project
