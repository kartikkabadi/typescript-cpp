// lsp.cpp — dep-decl/stub TU for the testutil-leaves slice; owned by lsp.
// ToReader/ToWriter are ported for real (small wrappers the lsptestutil
// transport needs); NewServer remains a dep-stub.
#include "internal/lsp/lsp.h"

#include "internal/ast/ast.h" // TSC_UNREACHABLE
#include "internal/json/json.h"

namespace tsc::lsp {

namespace {

// lspReader — server.go:80.
struct lspReader final : Reader {
	lsproto::BaseReader r;

	std::pair<std::shared_ptr<lsproto::Message>, gostd::Error> Read()
	    override {
		auto [data, err] = r.Read();
		if (err != nullptr) {
			return {nullptr, err};
		}

		auto req = std::make_shared<lsproto::Message>();
		if (std::string uerr = json::unmarshal(data, req.get());
		    !uerr.empty()) {
			// errors.Is(err, lsproto.ErrorCodeInvalidParams): the C++
			// unmarshal path flattens the wrapped code into text, so
			// match on its rendered name.
			if (uerr.find(lsproto::String(
			        lsproto::ErrorCodeInvalidParams)) !=
			    std::string::npos) {
				return {req,
				        gostd::errorf(
				            "%w: %v",
				            {lsproto::errorCodeErr(
				                 lsproto::ErrorCodeInvalidParams),
				             gostd::newError(uerr)})};
			}
			return {nullptr,
			        gostd::errorf("%w: %v",
			                      {lsproto::errorCodeErr(
				                       lsproto::ErrorCodeInvalidRequest),
				                   gostd::newError(uerr)})};
		}
		return {req, nullptr};
	}
};

// lspWriter — server.go:84.
struct lspWriter final : Writer {
	lsproto::BaseWriter w;

	gostd::Error
	Write(const std::shared_ptr<lsproto::Message>& msg) override {
		auto [data, merr] = json::marshal(*msg);
		if (!merr.empty()) {
			// messageMarshalError (server.go:88): "failed to marshal
			// message: <err>" and Unwrap() []error{
			// ErrorCodeInternalError, err}.
			return gostd::errorf(
			    "failed to marshal message: %w: %v",
			    {lsproto::errorCodeErr(lsproto::ErrorCodeInternalError),
			     gostd::newError(merr)});
		}
		return w.Write(data);
	}
};

}  // namespace

// ToReader — server.go:152.
std::shared_ptr<Reader> ToReader(gostd::io::Reader* r) {
	auto out = std::make_shared<lspReader>();
	out->r = lsproto::NewBaseReader(r);
	return out;
}

// ToWriter — server.go:162.
std::shared_ptr<Writer> ToWriter(gostd::io::Writer* w) {
	auto out = std::make_shared<lspWriter>();
	out->w = lsproto::NewBaseWriter(w);
	return out;
}

// === dep stubs — removed when owner slice lands ===

// NewServer — server.go:76. Dep-stub.
std::shared_ptr<Server> NewServer(ServerOptions* /*opts*/) {
	TSC_UNREACHABLE("lsp::NewServer — owned by lsp slice");
}

}  // namespace tsc::lsp
