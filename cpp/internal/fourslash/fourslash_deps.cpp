// === slice: fourslash (dep-stub bodies) ===
// Bodies for the fourslash dep declarations in fourslash_deps.h.
// lsp::toReader/toWriter are real ports (leaf glue); everything else is
// TSC_UNREACHABLE("<name> — owned by <slice>").

#include "internal/fourslash/fourslash_deps.h"

#include "internal/core/types.h"

namespace tsc::lsp {

namespace {

// lspReader — server.go:113.
struct lspReader final : Reader {
	lsproto::BaseReader r;
	explicit lspReader(gostd::io::Reader* rr)
	    : r(lsproto::NewBaseReader(rr)) {}

	// server.go:131 — Read.
	std::pair<std::shared_ptr<lsproto::Message>, gostd::Error>
	read() override {
		auto [data, err] = r.Read();
		if (err) {
			return {nullptr, err};
		}

		auto req = std::make_shared<lsproto::Message>();
		if (auto uerr = json::unmarshal(data, req.get()); !uerr.empty()) {
			// errors.Is(err, lsproto.ErrorCodeInvalidParams) — json.unmarshal
			// here returns text; the wrapped ErrorCode formats via String(),
			// so match its prefix.
			if (gostr::hasPrefix(uerr, "InvalidParams")) {
				return {req,
				        gostd::errorf(
				            "%w: %w",
				            {lsproto::errorCodeErr(
				                 lsproto::ErrorCodeInvalidParams),
				             uerr})};
			}
			return {nullptr,
			        gostd::errorf(
			            "%w: %w",
			            {lsproto::errorCodeErr(
			                 lsproto::ErrorCodeInvalidRequest),
			             uerr})};
		}

		return {req, nullptr};
	}
};

// messageMarshalError — server.go:123.
struct messageMarshalError : gostd::ErrObj {
	gostd::Error err;
	explicit messageMarshalError(gostd::Error e) : err(std::move(e)) {}
	std::string Error() const override {
		return "failed to marshal message: " + err->Error();
	}
	std::vector<gostd::Error> unwrap() const override {
		return {lsproto::errorCodeErr(lsproto::ErrorCodeInternalError), err};
	}
};

// lspWriter — server.go:118.
struct lspWriter final : Writer {
	lsproto::BaseWriter w;
	explicit lspWriter(gostd::io::Writer* ww)
	    : w(lsproto::NewBaseWriter(ww)) {}

	// server.go:154 — Write.
	gostd::Error write(lsproto::Message* msg) override {
		auto [data, merr] = json::marshal(*msg);
		if (!merr.empty()) {
			return std::make_shared<messageMarshalError>(
				gostd::newError(merr));
		}
		auto werr = w.Write(data);
		return werr;
	}
};

} // namespace

// server.go:150 — ToReader.
std::unique_ptr<Reader> toReader(gostd::io::Reader* r) {
	return std::make_unique<lspReader>(r);
}

// server.go:162 — ToWriter.
std::unique_ptr<Writer> toWriter(gostd::io::Writer* w) {
	return std::make_unique<lspWriter>(w);
}

namespace {
struct serverStub final : Server {
	gostd::Error run(gostd::Context) override {
		TSC_UNREACHABLE("lsp.Server.Run — owned by lsp");
	}
	project::Session* session() override {
		TSC_UNREACHABLE("lsp.Server.Session — owned by lsp");
	}
	gostd::Chan<int>* initComplete() override {
		TSC_UNREACHABLE("lsp.Server.InitComplete — owned by lsp");
	}
	gostd::Error setCompilerOptionsForInferredProjects(
		gostd::Context, tsc::CompilerOptions*) override {
		TSC_UNREACHABLE(
			"lsp.Server.SetCompilerOptionsForInferredProjects — owned by lsp");
	}
};
} // namespace

// server.go:60 — NewServer.
Server* newServer(ServerOptions*) {
	TSC_UNREACHABLE("lsp.NewServer — owned by lsp");
}

} // namespace tsc::lsp

namespace tsc::project {

// parsecache.go:74 — NewParseCache.
ParseCache* newParseCache(const RefCountCacheOptions&) {
	TSC_UNREACHABLE("project.NewParseCache — owned by project");
}

// session.go:1018 — (s *Session).Snapshot.
project::Snapshot* sessionSnapshot(project::Session*) {
	TSC_UNREACHABLE("project.Session.Snapshot — owned by project");
}

// projectcollection.go:72 — (c *ProjectCollection).ConfigFileRegistry.
ConfigFileRegistry* projectCollectionConfigFileRegistry(ProjectCollection*) {
	TSC_UNREACHABLE(
		"project.ProjectCollection.ConfigFileRegistry — owned by project");
}

} // namespace tsc::project

namespace tsc::tsctests {

// GetFileMapWithBuild — owned by execute/tsc.
gostd::Error getFileMapWithBuild(
	std::unordered_map<std::string, std::any>&,
	const std::vector<std::string>&) {
	TSC_UNREACHABLE("tsctests.GetFileMapWithBuild — owned by execute/tsc");
}

} // namespace tsc::tsctests
