// tests_host.cpp — port of tsc/internal/contentmapper/host_test.go.
//
// The Go test wires in-process mapper handlers over net.Pipe through the
// host's JSON-RPC connection. The C++ port mirrors that with chanPipe (a
// synchronous bidirectional byte channel) and ipc::NewAsyncConn.
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/ipc/ipc.h"
#include "internal/json/json.h"
#include "internal/locale/locale.h"
#include "internal/spanmap/spanmap.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace cm = tsc::contentmapper;
using namespace tsc;

namespace {

// ---------------------------------------------------------------------------
// chanPipe — net.Pipe: a synchronous bidirectional byte channel.
// ---------------------------------------------------------------------------
struct pipeHalf {
	std::mutex mu;
	std::condition_variable cv;
	std::string buf;
	bool closed = false;    // write side closed (readers see EOF)
	bool readClosed = false; // read side closed (writers see error)

	std::pair<int, gostd::Error> read(std::span<char> b) {
		std::unique_lock<std::mutex> lk(mu);
		cv.wait(lk, [&] { return !buf.empty() || closed || readClosed; });
		if (buf.empty()) {
			if (readClosed) {
				return {0, gostd::newError("io: read/write on closed pipe")};
			}
			return {0, gostd::io::errEOF};
		}
		int n = (int)std::min(b.size(), buf.size());
		std::memcpy(b.data(), buf.data(), n);
		buf.erase(0, n);
		lk.unlock();
		cv.notify_all();
		return {n, nullptr};
	}
	std::pair<int, gostd::Error> write(std::string_view d) {
		std::unique_lock<std::mutex> lk(mu);
		if (closed || readClosed) {
			return {0, gostd::newError("io: write on closed pipe")};
		}
		buf.append(d);
		cv.notify_all();
		cv.wait(lk, [&] { return buf.empty() || closed || readClosed; });
		if (buf.empty()) {
			// Fully consumed by the reader = success, even if a close
			// raced in after the drain (net.Pipe semantics: a write
			// that reached the reader has already returned).
			return {(int)d.size(), nullptr};
		}
		// Partial transfer before remote close: n + ErrClosedPipe.
		return {(int)std::max<int64_t>(
		            0, (int64_t)d.size() - (int64_t)buf.size()),
		        gostd::newError("io: write on closed pipe")};
	}
};

// pipeEnd is one end of a chanPipe; it also implements
// contentmapper::processExitState so the host's exit-code probe works on
// test transports the same way it does on real pipes.
struct pipeEnd : gostd::io::ReadWriteCloser,
                 cm::processExitState {
	std::shared_ptr<pipeHalf> in, out;
	std::pair<int, bool> exitCode = {0, false};
	std::function<void()> onClose;
	std::once_flag once;

	std::pair<int, gostd::Error> read(std::span<char> b) override {
		return in->read(b);
	}
	std::pair<int, gostd::Error> write(std::string_view d) override {
		return out->write(d);
	}
	gostd::Error close() override {
		std::call_once(once, [&] {
			in->mu.lock();
			in->readClosed = true;
			in->mu.unlock();
			in->cv.notify_all();
			out->mu.lock();
			out->closed = true;
			out->mu.unlock();
			out->cv.notify_all();
			if (onClose) {
				onClose();
			}
		});
		return nullptr;
	}
	std::pair<int, bool> ExitCode() override { return exitCode; }
};

// netPipe returns (client, server) — net.Pipe.
inline std::pair<std::shared_ptr<pipeEnd>, std::shared_ptr<pipeEnd>>
netPipe() {
	auto ab = std::make_shared<pipeHalf>();
	auto ba = std::make_shared<pipeHalf>();
	auto client = std::make_shared<pipeEnd>();
	auto server = std::make_shared<pipeEnd>();
	client->in = ab;
	client->out = ba;
	server->in = ba;
	server->out = ab;
	return {client, server};
}

// ---------------------------------------------------------------------------
// Wire-format helpers — the mapper side of the protocol, as encoding/json
// would emit for the Go structs.
// ---------------------------------------------------------------------------

// domField returns the member of `v` (a raw JSON object) under `name`.
const json::Dom* domField(const json::Dom& obj, std::string_view name) {
	return json::objGet(obj, name);
}

std::string domString(const json::Dom& v) {
	auto [s, e] = json::asString(v, "string");
	return e ? std::string{} : s;
}

json::Value marshalInitializeResult(const cm::InitializeResult& r) {
	return json::marshalObject(
	    {{"positionEncoding", json::marshalString(r.PositionEncoding)},
	     {"diagnosticSource", json::marshalString(r.DiagnosticSource)}});
}

json::Value marshalDiagnosticDirectives(const cm::DiagnosticDirectives& d) {
	std::vector<json::Value> unused;
	for (auto& u : d.UnusedExpectDirectiveDiagnostics) {
		unused.push_back(json::marshalObject(
		    {{"code", json::marshalInt64(u.Code)},
		     {"messageText", json::marshalString(u.MessageText)}}));
	}
	std::vector<json::Value> directives;
	for (auto& dd : d.Directives) {
		directives.push_back(dd.marshalJSONTo());
	}
	return json::marshalObject(
	    {{"unusedExpectDirectiveDiagnostics", json::marshalArray(unused)},
	     {"directives", json::marshalArray(directives)}});
}

json::Value marshalMappedOutput(const cm::MappedOutput& o) {
	std::vector<std::pair<std::string, json::Value>> members;
	members.emplace_back("text", json::marshalString(o.Text));
	members.emplace_back("extension", json::marshalString(o.Extension));
	if (!o.Mappings.empty()) {
		members.emplace_back("mappings", o.Mappings);
	}
	if (o.DiagnosticDirectives.has_value()) {
		members.emplace_back("diagnosticDirectives",
		                     marshalDiagnosticDirectives(*o.DiagnosticDirectives));
	}
	return json::marshalObject(members);
}

json::Value marshalTransformResult(const cm::TransformResult& r) {
	std::vector<std::pair<std::string, json::Value>> members;
	members.emplace_back("text", json::marshalString(r.Text));
	members.emplace_back("extension", json::marshalString(r.Extension));
	if (!r.Mappings.empty()) {
		members.emplace_back("mappings", r.Mappings);
	}
	if (r.DiagnosticDirectives.has_value()) {
		members.emplace_back("diagnosticDirectives",
		                     marshalDiagnosticDirectives(*r.DiagnosticDirectives));
	}
	if (!r.Diagnostics.empty()) {
		std::vector<json::Value> diags;
		for (auto& d : r.Diagnostics) {
			diags.push_back(json::marshalObject(
			    {{"messageText", json::marshalString(d.MessageText)},
			     {"start", json::marshalInt64(d.Start)},
			     {"length", json::marshalInt64(d.Length)},
			     {"code", json::marshalInt64(d.Code)}}));
		}
		members.emplace_back("diagnostics", json::marshalArray(diags));
	}
	if (!r.Supplemental.empty()) {
		std::vector<json::Value> supp;
		for (auto& s : r.Supplemental) {
			supp.push_back(marshalMappedOutput(s));
		}
		members.emplace_back("supplemental", json::marshalArray(supp));
	}
	return json::marshalObject(members);
}

json::Value marshalOptionDiagnostics(
    const std::vector<cm::OptionDiagnosticResult>& ods) {
	std::vector<json::Value> out;
	for (auto& od : ods) {
		std::vector<json::Value> path;
		for (auto& seg : od.Path) {
			path.push_back(seg);
		}
		out.push_back(json::marshalObject(
		    {{"path", json::marshalArray(path)},
		     {"messageText", json::marshalString(od.MessageText)},
		     {"code", json::marshalInt64(od.Code)}}));
	}
	return json::marshalArray(out);
}

json::Value marshalOpenProjectResult(const cm::OpenProjectResult& r) {
	std::vector<std::pair<std::string, json::Value>> members;
	members.emplace_back("configIdentity",
	                     json::marshalString(r.ConfigIdentity));
	if (!r.WatchedFiles.empty()) {
		std::vector<json::Value> wf;
		for (auto& f : r.WatchedFiles) {
			wf.push_back(json::marshalString(f));
		}
		members.emplace_back("watchedFiles", json::marshalArray(wf));
	}
	if (!r.OptionDiagnostics.empty()) {
		members.emplace_back("optionDiagnostics",
		                     marshalOptionDiagnostics(r.OptionDiagnostics));
	}
	return json::marshalObject(members);
}

// Params unmarshaling — Dom views borrow from the stored Value; callers keep
// `params` alive by copying it into the parsed struct where needed.
struct parsedInitializeParams {
	std::string locale;
	std::vector<std::string> positionEncodings;
};

parsedInitializeParams unmarshalInitializeParams(const json::Value& params) {
	parsedInitializeParams p;
	auto [d, err] = json::parse(params);
	if (err) {
		return p;
	}
	if (auto* v = domField(d, "locale")) {
		p.locale = domString(*v);
	}
	if (auto* v = domField(d, "positionEncodings");
	    v && v->kind == json::Dom::K::Array) {
		for (auto& item : v->arr) {
			p.positionEncodings.push_back(domString(item));
		}
	}
	return p;
}

struct parsedTransformParams {
	std::string fileName;
	std::string content;
	std::string projectHandle;
};

parsedTransformParams unmarshalTransformParams(const json::Value& params) {
	parsedTransformParams p;
	auto [d, err] = json::parse(params);
	if (err) {
		return p;
	}
	if (auto* v = domField(d, "fileName")) {
		p.fileName = domString(*v);
	}
	if (auto* v = domField(d, "content")) {
		p.content = domString(*v);
	}
	if (auto* v = domField(d, "projectHandle")) {
		p.projectHandle = domString(*v);
	}
	return p;
}

struct parsedOpenProjectParams {
	std::string configFileName;
	std::string projectHandle;
	std::string options;         // raw JSON
	std::string compilerOptions; // raw JSON
};

parsedOpenProjectParams unmarshalOpenProjectParams(
    const json::Value& params) {
	parsedOpenProjectParams p;
	auto [d, err] = json::parse(params);
	if (err) {
		return p;
	}
	if (auto* v = domField(d, "configFileName")) {
		p.configFileName = domString(*v);
	}
	if (auto* v = domField(d, "projectHandle")) {
		p.projectHandle = domString(*v);
	}
	if (auto* v = domField(d, "options")) {
		p.options = std::string(v->raw);
	}
	if (auto* v = domField(d, "compilerOptions")) {
		p.compilerOptions = std::string(v->raw);
	}
	return p;
}

struct parsedCloseProjectParams {
	std::string projectHandle;
};

parsedCloseProjectParams unmarshalCloseProjectParams(
    const json::Value& params) {
	parsedCloseProjectParams p;
	auto [d, err] = json::parse(params);
	if (err) {
		return p;
	}
	if (auto* v = domField(d, "projectHandle")) {
		p.projectHandle = domString(*v);
	}
	return p;
}

// ---------------------------------------------------------------------------
// Test mappers
// ---------------------------------------------------------------------------

// gate mirrors `chan struct{}` released by close(): wait() blocks until
// close() is called.
struct gate {
	std::mutex mu;
	std::condition_variable cv;
	bool closed = false;

	void close() {
		{
			std::lock_guard<std::mutex> l(mu);
			closed = true;
		}
		cv.notify_all();
	}
	void wait() {
		std::unique_lock<std::mutex> l(mu);
		cv.wait(l, [&] { return closed; });
	}
};

// handlesProjects mirrors the Go marker interface — mappers that handle
// openProject/closeProject themselves implement it; others get wrapped in
// noOpProjectMapper.
struct mapperHandler : ipc::Handler {
	virtual bool handlesProjects() { return false; }
};

struct fakeMapper : mapperHandler {
	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult({cm::PositionEncodingUTF8,
			                                 "vue"}),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			auto p = unmarshalTransformParams(params);
			auto* m = spanmap::New(
			    std::vector<spanmap::Segment>{{{},
			                                 (TextPos)p.content.size(),
			                                 {},
			                                 (TextPos)p.content.size(),
			                                 spanmap::KindVerbatim,
			                                 {}}});
			auto [mappings, merr] = spanmap::Marshal(m);
			if (merr.has_value()) {
				return {json::Value{}, gostd::newError(*merr)};
			}
			cm::TransformResult res;
			res.Text = p.content;
			res.Extension = ".ts";
			res.Mappings = json::Value(mappings);
			res.Diagnostics = {cm::Diagnostic{
			    "boom", 0, std::min<int64_t>(3, p.content.size()), 9999}};
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::newError("unexpected method " + std::string(method))};
	}
	gostd::Error HandleNotification(gostd::Context ctx,
	                                std::string_view method,
	                                const json::Value& params) override {
		return nullptr;
	}
};

struct responseMapper : mapperHandler {
	std::function<std::pair<json::Value, gostd::Error>(
	    const parsedTransformParams&)>
	    response;

	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult({cm::PositionEncodingUTF8,
			                                 "mapper"}),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			return response(unmarshalTransformParams(params));
		}
		return {json::Value{},
		        gostd::newError("unexpected method " + std::string(method))};
	}
	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

struct unicodeMapper : mapperHandler {
	cm::PositionEncoding encoding;
	const std::string* source = nullptr;

	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			auto p = unmarshalInitializeParams(params);
			bool offered =
			    std::find(p.positionEncodings.begin(),
			              p.positionEncodings.end(),
			              encoding) != p.positionEncodings.end();
			if (!offered && (encoding == cm::PositionEncodingUTF8 ||
			                 encoding == cm::PositionEncodingUTF16)) {
				return {json::Value{},
				        gostd::newError("position encoding \"" + encoding +
				                        "\" was not offered")};
			}
			std::string source = "mapper";
			if (this->source != nullptr) {
				source = *this->source;
			}
			return {marshalInitializeResult({encoding, source}), nullptr};
		}
		if (method == cm::MethodTransform) {
			auto p = unmarshalTransformParams(params);
			int64_t emojiLength = 0, textLength = 0;
			if (encoding == cm::PositionEncodingUTF8) {
				emojiLength = 2;
				textLength = 3;
			} else if (encoding == cm::PositionEncodingUTF16) {
				emojiLength = 1;
				textLength = 2;
			} else {
				cm::TransformResult res;
				res.Text = p.content;
				res.Extension = ".ts";
				return {marshalTransformResult(res), nullptr};
			}
			auto arr = json::marshalArray({
			    json::marshalArray(
			        {json::marshalInt64(0), json::marshalInt64(emojiLength),
			         json::marshalInt64(0), json::marshalInt64(emojiLength),
			         json::marshalInt64(spanmap::KindVerbatim)}),
			    json::marshalArray(
			        {json::marshalInt64(emojiLength),
			         json::marshalInt64(textLength - emojiLength),
			         json::marshalInt64(emojiLength),
			         json::marshalInt64(textLength - emojiLength),
			         json::marshalInt64(spanmap::KindVerbatim)}),
			});
			cm::TransformResult res;
			res.Text = p.content;
			res.Extension = ".ts";
			res.Mappings = arr;
			cm::DiagnosticDirectives dd;
			cm::MappedDiagnosticDirective directive;
			directive.OriginalStart = emojiLength;
			directive.OriginalLength = textLength - emojiLength;
			directive.VirtualStart = emojiLength;
			directive.VirtualEnd = textLength;
			directive.Policy = cm::DiagnosticDirectivePolicyIgnore;
			dd.Directives = {directive};
			res.DiagnosticDirectives = dd;
			res.Diagnostics = {cm::Diagnostic{"after non-ASCII character",
			                                  emojiLength,
			                                  textLength - emojiLength,
			                                  1001}};
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::newError("unexpected method " + std::string(method))};
	}
	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

struct invalidDiagnosticMapper : mapperHandler {
	cm::PositionEncoding encoding;

	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult({encoding, "mapper"}), nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformResult res;
			res.Extension = ".ts";
			res.Diagnostics = {cm::Diagnostic{"invalid boundary", 1, 0, 1002}};
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::newError("unexpected method " + std::string(method))};
	}
	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// noOpProjectMapper — host_test.go.
struct noOpProjectMapper : mapperHandler {
	std::shared_ptr<ipc::Handler> inner;

	explicit noOpProjectMapper(std::shared_ptr<ipc::Handler> inner)
	    : inner(std::move(inner)) {}

	bool handlesProjects() override { return true; }

	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		if (method == cm::MethodOpenProject) {
			return {marshalOpenProjectResult({}), nullptr};
		}
		if (method == cm::MethodCloseProject) {
			return {json::Value("null"), nullptr};
		}
		return inner->HandleRequest(ctx, method, params);
	}
	gostd::Error HandleNotification(gostd::Context ctx,
	                                std::string_view method,
	                                const json::Value& params) override {
		return inner->HandleNotification(ctx, method, params);
	}
};

// countingReadWriteCloser — host_test.go.
struct countingReadWriteCloser : gostd::io::ReadWriteCloser,
                                 cm::processExitState {
	std::shared_ptr<pipeEnd> inner;
	std::atomic<int32_t>* closes;
	std::once_flag once;

	std::pair<int, gostd::Error> read(std::span<char> b) override {
		return inner->read(b);
	}
	std::pair<int, gostd::Error> write(std::string_view d) override {
		return inner->write(d);
	}
	gostd::Error close() override {
		gostd::Error err;
		std::call_once(once, [&] {
			closes->fetch_add(1);
			err = inner->close();
		});
		return err;
	}
	std::pair<int, bool> ExitCode() override { return inner->ExitCode(); }
};

// fakeSpawner — host_test.go: serves each spawn request with an in-process
// mapper over a pipe, counting spawns/closes so tests can assert process
// consolidation. When handler is unset it serves a fakeMapper.
struct fakeSpawner : cm::Spawner {
	std::atomic<int32_t> spawns{0};
	std::atomic<int32_t> closes{0};
	std::shared_ptr<ipc::Handler> handler;

	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Spawn(const std::vector<std::string>& command, const std::string& dir,
	      gostd::io::Writer* stderr_) override {
		spawns.fetch_add(1);
		std::shared_ptr<ipc::Handler> h = handler;
		if (h == nullptr) {
			h = std::make_shared<fakeMapper>();
		}
		auto* mh = dynamic_cast<mapperHandler*>(h.get());
		if (mh == nullptr || !mh->handlesProjects()) {
			h = std::make_shared<noOpProjectMapper>(h);
		}
		auto [client, server] = netPipe();
		auto srv = server;
		std::thread([srv, h] {
			ipc::NewAsyncConn(srv, h)->Run(gostd::contextBackground());
		}).detach();
		auto rwc = std::make_shared<countingReadWriteCloser>();
		rwc->inner = client;
		rwc->closes = &closes;
		return {rwc, nullptr};
	}
};

// exitedRWC mirrors exitedReadWriteCloser: ExitCode reports a fixed code.
struct exitedReadWriteCloser : pipeEnd {
	explicit exitedReadWriteCloser(std::shared_ptr<pipeEnd> inner) {
		in = inner->in;
		out = inner->out;
	}
	void setExit(int code) { exitCode = {code, true}; }
};

// exitOnCloseRWC mirrors exitOnCloseReadWriteCloser.
struct exitOnCloseReadWriteCloser : pipeEnd {
	std::atomic<bool> exited{false};

	gostd::Error close() override {
		exited.store(true);
		return pipeEnd::close();
	}
	std::pair<int, bool> ExitCode() override { return {1, exited.load()}; }
};

// closeSignalRWC mirrors closeSignalReadWriteCloser.
struct closeSignalReadWriteCloser : pipeEnd {
	std::function<void()> onClosed;
	std::once_flag sigOnce;

	gostd::Error close() override {
		auto err = pipeEnd::close();
		std::call_once(sigOnce, [&] { if (onClosed) onClosed(); });
		return err;
	}
};

// errorContains mirrors assert.ErrorContains.
void errorContains(T* t, const gostd::Error& err, std::string_view sub) {
	if (err == nullptr) {
		t->Error({"assert.ErrorContains failed: error is nil, expected " +
		          std::string(sub)});
		return;
	}
	if (err->Error().find(sub) == std::string::npos) {
		t->Error({"assert.ErrorContains failed: error \"" + err->Error() +
		          "\" does not contain \"" + std::string(sub) + "\""});
	}
}

// protocolDiagnosticDirectives mirrors the Go helper.
cm::DiagnosticDirectives protocolDiagnosticDirectives(
    std::vector<cm::MappedDiagnosticDirective> directives,
    std::vector<cm::UnusedExpectDirectiveDiagnostic> unused = {}) {
	cm::DiagnosticDirectives dd;
	dd.UnusedExpectDirectiveDiagnostics = std::move(unused);
	dd.Directives = std::move(directives);
	return dd;
}

cm::Mapper* mapper(std::string name, std::string version = "1.0.0",
                   std::vector<std::string> exec = {"mapper"}) {
	auto* m = new cm::Mapper();
	m->Manifest.Name = std::move(name);
	m->Manifest.Version = std::move(version);
	m->Manifest.Exec = std::move(exec);
	return m;
}

void TestRunnerTransform(T* t) {
	t->Parallel();
	fakeSpawner spawner;
	auto r = cm::NewHost(t->Context(), &spawner, locale::Default);
	auto* m = new cm::Mapper();
	m->Definition.Extensions = {".vue"};
	m->Manifest.Name = "vue";
	m->Manifest.Version = "1.0.0";
	m->Manifest.Exec = {"vue-mapper"};
	auto [result, err] = r->Transform(m, {"/a.vue", "export const x = 1;"});
	assert::NilError(t, err);
	assert::Equal(t, result.Text, std::string("export const x = 1;"));
	assert::Equal(t, result.VirtualExtension, std::string(".ts"));
	assert::Assert(t, result.Mappings != nullptr);
	assert::Equal(t, result.Diagnostics.size(), size_t(1));
	assert::Equal(t, result.Diagnostics[0]->code, int32_t(9999));
	assert::Equal(t, result.Diagnostics[0]->source, std::string("vue"));
	r->Close();
}

void TestHostLogging(T* t) {
	t->Parallel();
	std::mutex mu;
	std::vector<std::string> logs;
	cm::Logger logger = [&](std::string_view message) {
		std::lock_guard<std::mutex> l(mu);
		logs.push_back(std::string(message));
	};
	fakeSpawner fs;
	cm::SpawnerFunc spawner(
	    [&](const std::vector<std::string>& command, const std::string& dir,
	        gostd::io::Writer* stderr_)
	        -> std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>,
	                     gostd::Error> {
		    stderr_->write("mapper diagnostic\n");
		    return fs.Spawn(command, dir, stderr_);
	    });
	auto host = cm::NewHostWithOptions(t->Context(), &spawner,
	                                   locale::Default, {.Logger = logger});
	auto* m = new cm::Mapper();
	m->Definition.Package = "configured";
	m->Definition.Extensions = {".vue"};
	m->Manifest.Name = "resolved";
	m->Manifest.Version = "1.0.0";
	m->Manifest.Exec = {"mapper"};
	auto [_, err] =
	    host->Transform(m, {"/a.vue", "export const x = 1;"});
	assert::NilError(t, err);

	std::lock_guard<std::mutex> l(mu);
	std::string joined;
	for (auto& s : logs) {
		joined += s;
		joined += "\n";
	}
	assert::Assert(
	    t,
	    joined.find("[content mapper: resolved] send: {\"jsonrpc\":\"2.0\",\"id\":\"api1\",\"method\":\"initialize\"") !=
	        std::string::npos);
	assert::Assert(
	    t,
	    joined.find("[content mapper: resolved] receive: {\"jsonrpc\":\"2.0\",\"id\":\"api1\",\"result\":") !=
	        std::string::npos);
	assert::Assert(t, joined.find("[content mapper: resolved] stderr: mapper diagnostic") !=
	                  std::string::npos);
	host->Close();
}

void TestHostDiscardsStderrWithoutLogging(T* t) {
	t->Parallel();
	fakeSpawner fs;
	cm::SpawnerFunc spawner(
	    [&](const std::vector<std::string>& command, const std::string& dir,
	        gostd::io::Writer* stderr_)
	        -> std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>,
	                     gostd::Error> {
		    assert::Assert(t, stderr_ == gostd::io::discard());
		    return fs.Spawn(command, dir, stderr_);
	    });
	auto host = cm::NewHost(t->Context(), &spawner, locale::Default);
	auto* m = new cm::Mapper();
	m->Definition.Package = "configured";
	m->Definition.Extensions = {".vue"};
	m->Manifest.Name = "resolved";
	m->Manifest.Version = "1.0.0";
	m->Manifest.Exec = {"mapper"};
	auto [_, err] =
	    host->Transform(m, {"/a.vue", "export const x = 1;"});
	assert::NilError(t, err);
	host->Close();
}

void TestMapperDiagnosticName(T* t) {
	t->Parallel();
	struct {
		cm::Mapper* mapper;
		const char* want;
	} tests[3];
	{
		auto* m = new cm::Mapper();
		m->Definition.Package = "configured";
		m->Manifest.Name = "resolved";
		m->ContributionID = "contributed";
		tests[0] = {m, "resolved"};
	}
	{
		auto* m = new cm::Mapper();
		m->Definition.Package = "configured";
		m->ContributionID = "contributed";
		tests[1] = {m, "configured"};
	}
	{
		auto* m = new cm::Mapper();
		m->ContributionID = "contributed";
		tests[2] = {m, "contributed"};
	}
	for (auto& test : tests) {
		assert::Equal(t, test.mapper->DiagnosticName(),
		              std::string(test.want));
	}
}

void TestRunnerTransformResponseValidation(T* t) {
	t->Parallel();
	cm::Request request{"/a.vue", "a"};
	auto* m = new cm::Mapper();
	m->Definition.Extensions = {".vue"};
	m->Manifest.Name = "mapper";
	m->Manifest.Exec = {"mapper"};

	t->Run("malformed result fails the request", [&](T* t) {
		t->Parallel();
		auto rm = std::make_shared<responseMapper>();
		rm->response = [](const parsedTransformParams&)
		    -> std::pair<json::Value, gostd::Error> {
			return {json::Value(R"({"text":1})"), nullptr};
		};
		fakeSpawner fs;
		fs.handler = rm;
		auto host = cm::NewHost(t->Context(), &fs, locale::Default);
		auto [_, err] = host->Transform(m, request);
		assert::Assert(t, err != nullptr);
		host->Close();
	});
}

void TestHostClosesProcessWhenReadLoopFails(T* t) {
	t->Parallel();
	std::mutex closedMu;
	std::condition_variable closedCv;
	bool closed = false;
	cm::SpawnerFunc spawner(
	    [&](const std::vector<std::string>& command, const std::string& dir,
	        gostd::io::Writer* stderr_)
	        -> std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>,
	                     gostd::Error> {
		    auto [client, server] = netPipe();
		    auto srv = server;
		    std::thread([t, srv] {
			    // testGoexit must be caught inside the goroutine: in Go,
			    // FailNow on a non-test goroutine marks the test failed and
			    // kills only that goroutine; an escaping testGoexit on a
			    // detached std::thread is std::terminate (__fastfail).
			    try {
				    auto protocol = ipc::NewJSONRPCProtocol(srv);
				    auto [message, merr] = protocol->ReadMessage();
				    assert::Assert(t, merr == nullptr);
				    assert::Assert(t, message->Method ==
				                      std::string(cm::MethodInitialize));
				    auto perr = protocol->WriteResponse(
				        message->Id.has_value() ? &*message->Id : nullptr,
				        marshalInitializeResult(
				            {cm::PositionEncodingUTF8, "mapper"}));
				    assert::Assert(t, perr == nullptr);
				    auto [w, werr] = srv->write("oops\n");
				    assert::Assert(t, werr == nullptr);
			    } catch (const tsc::gostd::testing::testGoexit&) {
			    }
			    srv->close();
		    }).detach();
		    auto rwc = std::make_shared<closeSignalReadWriteCloser>();
		    rwc->in = client->in;
		    rwc->out = client->out;
		    rwc->onClosed = [&] {
			    std::lock_guard<std::mutex> l(closedMu);
			    closed = true;
			    closedCv.notify_all();
		    };
		    return {rwc, nullptr};
	    });
	auto host = cm::NewHost(t->Context(), &spawner, locale::Default);
	auto* m = new cm::Mapper();
	m->Manifest.Name = "mapper";
	m->Manifest.Exec = {"mapper"};
	auto [_, err] = host->Transform(m, {"/a.vue", ""});
	assert::Assert(t, err != nullptr);
	bool processClosed;
	{
		std::unique_lock<std::mutex> l(closedMu);
		closedCv.wait_for(l, std::chrono::seconds(1),
		                  [&] { return closed; });
		processClosed = closed;
	}
	assert::Assert(t, processClosed,
	               "mapper process was not closed after its read loop "
	               "failed");
	host->Close();
}

void TestHostReportsInitializationTimeoutBeforeClosingProcess(T* t) {
	t->Parallel();
	cm::SpawnerFunc spawner(
	    [&](const std::vector<std::string>& command, const std::string& dir,
	        gostd::io::Writer* stderr_)
	        -> std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>,
	                     gostd::Error> {
		    auto [client, server] = netPipe();
		    auto srv = server;
		    std::thread([srv] {
			    auto protocol = ipc::NewJSONRPCProtocol(srv);
			    (void)protocol->ReadMessage();
			    // block until the client closes (test-context cancellation)
			    char c;
			    std::span<char> dummy(&c, 1);
			    (void)srv->read(dummy);
			    srv->close();
		    }).detach();
		    auto rwc = std::make_shared<exitOnCloseReadWriteCloser>();
		    rwc->in = client->in;
		    rwc->out = client->out;
		    return {rwc, nullptr};
	    });
	auto host = cm::NewHost(t->Context(), &spawner, locale::Default);
	auto* m = new cm::Mapper();
	m->Manifest.Name = "mapper";
	m->Manifest.Exec = {"mapper"};
	auto [_, err] = host->Transform(m, {"/a.vue", ""});
	auto* initializeError = gostd::errorAs<cm::InitializeError*>(err);
	assert::Assert(t, initializeError != nullptr,
	               "expected InitializeError, got " +
	                   (err ? err->Error() : std::string("<nil>")));
	assert::Assert(t,
	               initializeError->Kind ==
	                   cm::InitializeErrorKindNoResponse);
	host->Close();
}

void TestHostReportsProcessExitBeforeInitialization(T* t) {
	t->Parallel();
	cm::SpawnerFunc spawner(
	    [&](const std::vector<std::string>& command, const std::string& dir,
	        gostd::io::Writer* stderr_)
	        -> std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>,
	                     gostd::Error> {
		    auto [client, server] = netPipe();
		    server->close();
		    auto rwc = std::make_shared<exitedReadWriteCloser>(client);
		    rwc->setExit(42);
		    return {rwc, nullptr};
	    });
	auto host = cm::NewHost(t->Context(), &spawner, locale::Default);
	auto* m = new cm::Mapper();
	m->Manifest.Name = "mapper";
	m->Manifest.Exec = {"mapper"};
	auto [_, err] = host->Transform(m, {"/a.vue", ""});
	auto* initializeError = gostd::errorAs<cm::InitializeError*>(err);
	assert::Assert(t, initializeError != nullptr,
	               "expected InitializeError, got " +
	                   (err ? err->Error() : std::string("<nil>")));
	assert::Assert(t,
	               initializeError->Kind ==
	                   cm::InitializeErrorKindProcessExit);
	assert::Equal(t, initializeError->ExitCode, 42);
	host->Close();
}

void TestRunnerTransformDiagnosticDirectives(T* t) {
	t->Parallel();
	cm::Request request{"/a.vue", "directive\nsource"};
	auto* m = new cm::Mapper();
	m->Definition.Extensions = {".vue"};
	m->Manifest.Name = "mapper";
	m->Manifest.Exec = {"mapper"};
	auto transform = [&](cm::MappedOutput output)
	    -> std::pair<cm::Result, gostd::Error> {
		auto rm = std::make_shared<responseMapper>();
		rm->response = [output](const parsedTransformParams&)
		    -> std::pair<json::Value, gostd::Error> {
			cm::TransformResult res;
			*static_cast<cm::MappedOutput*>(&res) = output;
			return {marshalTransformResult(res), nullptr};
		};
		fakeSpawner fs;
		fs.handler = rm;
		auto host = cm::NewHost(t->Context(), &fs, locale::Default);
		auto out = host->Transform(m, request);
		host->Close();
		return out;
	};

	{
		cm::MappedDiagnosticDirective d;
		d.OriginalStart = 0;
		d.OriginalLength = 9;
		d.VirtualStart = 8;
		d.VirtualEnd = 14;
		d.Policy = cm::DiagnosticDirectivePolicyExpect;
		cm::MappedOutput out;
		out.Text = "virtual source";
		out.Extension = ".ts";
		out.DiagnosticDirectives = protocolDiagnosticDirectives(
		    {d}, {cm::UnusedExpectDirectiveDiagnostic{
		              2578, "Unused framework directive."}});
		auto [result, err] = transform(out);
		assert::NilError(t, err);
		assert::Equal(t, result.DiagnosticDirectives.size(), size_t(1));
		auto& directive = result.DiagnosticDirectives[0];
		assert::Equal(t, directive.OriginalRange.pos(), 0);
		assert::Equal(t, directive.OriginalRange.end(), 9);
		assert::Equal(t, directive.VirtualRange.pos(), 8);
		assert::Equal(t, directive.VirtualRange.end(), 14);
		assert::Assert(t,
		               directive.Policy ==
		                   MappedDiagnosticDirectivePolicy::Expect);
		assert::Equal(t, directive.UnusedCode, int32_t(2578));
		assert::Equal(t, directive.UnusedMessageText,
		              std::string("Unused framework directive."));
		assert::Equal(t, directive.Source, std::string("mapper"));
	}
	{
		int64_t unusedIndex = 1;
		cm::MappedDiagnosticDirective d;
		d.OriginalLength = 9;
		d.VirtualStart = 8;
		d.VirtualEnd = 14;
		d.Policy = cm::DiagnosticDirectivePolicyExpect;
		d.UnusedExpectDirectiveIndex = unusedIndex;
		cm::MappedOutput out;
		out.Text = "virtual source";
		out.Extension = ".ts";
		out.DiagnosticDirectives = protocolDiagnosticDirectives(
		    {d}, {cm::UnusedExpectDirectiveDiagnostic{1, "first"},
		          cm::UnusedExpectDirectiveDiagnostic{2, "second"}});
		auto [result, err] = transform(out);
		assert::NilError(t, err);
		assert::Equal(t, result.DiagnosticDirectives[0].UnusedCode,
		              int32_t(2));
		assert::Equal(t, result.DiagnosticDirectives[0].UnusedMessageText,
		              std::string("second"));
	}
	{
		cm::MappedDiagnosticDirective d;
		d.OriginalStart = -1;
		d.Policy = cm::DiagnosticDirectivePolicyIgnore;
		cm::MappedOutput out;
		out.Text = "x";
		out.Extension = ".ts";
		out.DiagnosticDirectives = protocolDiagnosticDirectives(
		    {d}, {cm::UnusedExpectDirectiveDiagnostic{}});
		auto [result, err] = transform(out);
		assert::NilError(t, err);
	}

	struct {
		const char* name;
		const char* text;
		std::vector<cm::MappedDiagnosticDirective> directives;
		cm::DiagnosticDirectiveErrorKind kind;
	} invalid[] = {
	    {"invalid range", "x",
	     {{0, 0, -1, 0, cm::DiagnosticDirectivePolicyIgnore}},
	     cm::DiagnosticDirectiveErrorKindInvalidRange},
	    {"unknown policy", "x",
	     {{0, 0, 0, 0, 2}},
	     cm::DiagnosticDirectiveErrorKindInvalidPolicy},
	    {"expect requires unused diagnostic", "x",
	     {{0, 0, 0, 0, cm::DiagnosticDirectivePolicyExpect}},
	     cm::DiagnosticDirectiveErrorKindExpectMissingUnusedDiagnostic},
	    {"multiple unused diagnostics require index", "x",
	     {{0, 0, 0, 0, cm::DiagnosticDirectivePolicyExpect}},
	     cm::DiagnosticDirectiveErrorKindExpectMissingUnusedDiagnostic},
	    {"original range out of bounds", "x",
	     {{99, 0, 0, 0, cm::DiagnosticDirectivePolicyExpect}},
	     cm::DiagnosticDirectiveErrorKindInvalidRange},
	    {"virtual range out of bounds", "x",
	     {{0, 0, 99, 0, cm::DiagnosticDirectivePolicyIgnore}},
	     cm::DiagnosticDirectiveErrorKindInvalidRange},
	    {"overlap", "abc",
	     {{0, 0, 0, 2, cm::DiagnosticDirectivePolicyIgnore},
	      {0, 0, 1, 3, cm::DiagnosticDirectivePolicyIgnore}},
	     cm::DiagnosticDirectiveErrorKindOverlap},
	};
	for (auto& test : invalid) {
		t->Run(test.name, [&](T* t) {
			t->Parallel();
			auto diagnosticDirectives =
			    protocolDiagnosticDirectives(test.directives);
			if (std::string(test.name) ==
			    "original range out of bounds") {
				diagnosticDirectives
				    .UnusedExpectDirectiveDiagnostics = {
				        cm::UnusedExpectDirectiveDiagnostic{}};
			} else if (std::string(test.name) ==
			           "multiple unused diagnostics require index") {
				diagnosticDirectives
				    .UnusedExpectDirectiveDiagnostics = {
				        cm::UnusedExpectDirectiveDiagnostic{},
				        cm::UnusedExpectDirectiveDiagnostic{}};
			}
			cm::MappedOutput out;
			out.Text = test.text;
			out.Extension = ".ts";
			out.DiagnosticDirectives = diagnosticDirectives;
			auto [_, transformErr] = transform(out);
			auto* directiveError =
			    gostd::errorAs<cm::DiagnosticDirectiveError*>(transformErr);
			assert::Assert(t, directiveError != nullptr);
			assert::Assert(t, directiveError->Kind == test.kind);
		});
	}
}

void TestMappedDiagnosticDirectiveJSON(T* t) {
	t->Parallel();
	struct {
		const char* name;
		cm::MappedDiagnosticDirective directive;
		const char* want;
	} tests[] = {
	    {"ignore",
	     {0, 9, 8, 14, cm::DiagnosticDirectivePolicyIgnore},
	     "[0,9,8,14,0]"},
	    {"expect",
	     {0, 9, 8, 14, cm::DiagnosticDirectivePolicyExpect},
	     "[0,9,8,14,1]"},
	};
	for (auto& test : tests) {
		t->Run(test.name, [&](T* t) {
			t->Parallel();
			auto data = test.directive.marshalJSONTo();
			assert::Assert(t, !data.empty());
			assert::Equal(t, std::string(data), std::string(test.want));
			cm::MappedDiagnosticDirective decoded;
			assert::NilError(t, decoded.unmarshalJSONFrom(data));
			assert::Equal(t, decoded.OriginalStart,
			              test.directive.OriginalStart);
			assert::Equal(t, decoded.OriginalLength,
			              test.directive.OriginalLength);
			assert::Equal(t, decoded.VirtualStart,
			              test.directive.VirtualStart);
			assert::Equal(t, decoded.VirtualEnd,
			              test.directive.VirtualEnd);
			assert::Assert(t, decoded.Policy == test.directive.Policy);
			assert::Assert(
			    t, decoded.UnusedExpectDirectiveIndex ==
			           test.directive.UnusedExpectDirectiveIndex);
		});
	}

	for (auto* data : {"[0,0,0,0]", "[0,0,0,0,0,1,2]"}) {
		cm::MappedDiagnosticDirective directive;
		auto err = directive.unmarshalJSONFrom(json::Value(data));
		assert::Assert(t, err != nullptr);
		assert::Assert(t, err->Error().find("diagnostic directive tuple") !=
		                std::string::npos);
	}

	int64_t unusedIndex = 1;
	cm::DiagnosticDirectives diagnosticDirectives;
	diagnosticDirectives.UnusedExpectDirectiveDiagnostics = {
	    {1, "first"}, {2, "second"}};
	cm::MappedDiagnosticDirective d;
	d.OriginalStart = 2;
	d.OriginalLength = 3;
	d.VirtualStart = 5;
	d.VirtualEnd = 9;
	d.Policy = cm::DiagnosticDirectivePolicyExpect;
	d.UnusedExpectDirectiveIndex = unusedIndex;
	diagnosticDirectives.Directives = {d};
	auto data = marshalDiagnosticDirectives(diagnosticDirectives);
	assert::Equal(
	    t, std::string(data),
	    std::string(
	        R"({"unusedExpectDirectiveDiagnostics":[{"code":1,"messageText":"first"},{"code":2,"messageText":"second"}],"directives":[[2,3,5,9,1,1]]})"));
	// Round-trip: parse the data back through the JSON DOM.
	auto [dom, perr] = json::parse(data);
	assert::NilError(t, perr);
	assert::Assert(t, dom.kind == json::Dom::K::Object);
	auto* directives = json::objGet(dom, "directives");
	assert::Assert(t, directives != nullptr &&
	                  directives->kind == json::Dom::K::Array &&
	                  directives->arr.size() == 1);
	auto* unused = json::objGet(dom, "unusedExpectDirectiveDiagnostics");
	assert::Assert(t, unused != nullptr &&
	                  unused->kind == json::Dom::K::Array &&
	                  unused->arr.size() == 2);
}

void TestRunnerTransformSupplementalOutputs(T* t) {
	t->Parallel();
	auto rm = std::make_shared<responseMapper>();
	rm->response = [](const parsedTransformParams&)
	    -> std::pair<json::Value, gostd::Error> {
		cm::TransformResult res;
		res.Text = "export default 1;";
		res.Extension = ".ts";
		cm::SupplementalOutput s1;
		s1.Text = "declare const first: string;";
		s1.Extension = ".ts";
		cm::MappedDiagnosticDirective d;
		d.VirtualEnd = 7;
		d.Policy = cm::DiagnosticDirectivePolicyIgnore;
		s1.DiagnosticDirectives = protocolDiagnosticDirectives({d});
		cm::SupplementalOutput s2;
		s2.Text = "declare const second: number;";
		s2.Extension = ".mjs";
		res.Supplemental = {s1, s2};
		return {marshalTransformResult(res), nullptr};
	};
	fakeSpawner fs;
	fs.handler = rm;
	auto host = cm::NewHost(t->Context(), &fs, locale::Default);
	auto* m = new cm::Mapper();
	m->Definition.Extensions = {".vue"};
	m->Manifest.Name = "mapper";
	m->Manifest.Exec = {"mapper"};
	auto [result, err] = host->Transform(
	    m, {"/component.vue", "component"});
	assert::NilError(t, err);
	assert::Equal(t, result.Supplemental.size(), size_t(2));
	assert::Equal(t, result.Supplemental[0].Text,
	              std::string("declare const first: string;"));
	assert::Equal(t, result.Supplemental[0].VirtualExtension,
	              std::string(".ts"));
	assert::Assert(t, result.Supplemental[0].Mappings != nullptr);
	assert::Equal(t, result.Supplemental[0].DiagnosticDirectives.size(),
	              size_t(1));
	assert::Equal(t,
	              result.Supplemental[0].DiagnosticDirectives[0]
	                  .VirtualRange.end(),
	              7);
	assert::Equal(t, result.Supplemental[1].VirtualExtension,
	              std::string(".mjs"));
	assert::Assert(t, result.Supplemental[1].Mappings != nullptr);
	host->Close();
}

void TestRunnerTransformInvalidSupplementalDiagnosticDirective(T* t) {
	t->Parallel();
	auto rm = std::make_shared<responseMapper>();
	rm->response = [](const parsedTransformParams&)
	    -> std::pair<json::Value, gostd::Error> {
		cm::TransformResult res;
		res.Text = "export {};";
		res.Extension = ".ts";
		cm::SupplementalOutput s1;
		s1.Text = "export {};";
		s1.Extension = ".ts";
		cm::SupplementalOutput s2;
		s2.Text = "export {};";
		s2.Extension = ".ts";
		cm::MappedDiagnosticDirective d;
		d.Policy = cm::DiagnosticDirectivePolicyExpect;
		s2.DiagnosticDirectives = protocolDiagnosticDirectives({d});
		res.Supplemental = {s1, s2};
		return {marshalTransformResult(res), nullptr};
	};
	fakeSpawner fs;
	fs.handler = rm;
	auto host = cm::NewHost(t->Context(), &fs, locale::Default);
	auto* m = new cm::Mapper();
	m->Definition.Extensions = {".vue"};
	m->Manifest.Name = "mapper";
	m->Manifest.Exec = {"mapper"};
	auto [_, err] = host->Transform(m, {"/component.vue", "component"});
	auto* directiveError =
	    gostd::errorAs<cm::DiagnosticDirectiveError*>(err);
	assert::Assert(t, directiveError != nullptr);
	assert::Assert(t,
	               directiveError->Kind ==
	                   cm::DiagnosticDirectiveErrorKindExpectMissingUnusedDiagnostic);
	assert::Equal(t, directiveError->Index, 0);
	assert::Equal(t, directiveError->SupplementalIndex, 1);
	host->Close();
}

void TestRunnerRejectsInvalidVirtualExtension(T* t) {
	t->Parallel();
	for (bool supplemental : {false, true}) {
		for (auto* extension : {"", ".coffee"}) {
			t->Run("supplemental=" +
			           std::string(supplemental ? "true" : "false") + "/" +
			           extension,
			       [&](T* t) {
				       t->Parallel();
				       auto rm = std::make_shared<responseMapper>();
				       rm->response =
				           [supplemental, extension](
				               const parsedTransformParams&)
				           -> std::pair<json::Value, gostd::Error> {
					       std::string canonicalExtension =
					           extension;
					       cm::TransformResult res;
					       if (supplemental) {
						       canonicalExtension = ".ts";
						       cm::SupplementalOutput s;
						       s.Text = "export {};";
						       s.Extension = extension;
						       res.Supplemental = {s};
					       }
					       res.Text = "export {};";
					       res.Extension = canonicalExtension;
					       return {marshalTransformResult(res),
					               nullptr};
				       };
				       fakeSpawner fs;
				       fs.handler = rm;
				       auto host = cm::NewHost(t->Context(), &fs,
				                               locale::Default);
				       auto* m = new cm::Mapper();
				       m->Definition.Extensions = {".vue"};
				       m->Manifest.Name = "mapper";
				       m->Manifest.Exec = {"mapper"};
				       auto [_, err] = host->Transform(
				           m, {"/component.vue", "component"});
				       errorContains(t, err,
				                     "invalid virtual extension");
				       host->Close();
			       });
		}
	}
}

void TestRunnerPositionEncodings(T* t) {
	t->Parallel();
	for (const cm::PositionEncoding& encoding :
	     {cm::PositionEncodingUTF8, cm::PositionEncodingUTF16}) {
		t->Run(encoding, [&](T* t) {
			t->Parallel();
			auto um = std::make_shared<unicodeMapper>();
			um->encoding = encoding;
			fakeSpawner fs;
			fs.handler = um;
			auto r = cm::NewHost(t->Context(), &fs, locale::Default);
			auto* m = new cm::Mapper();
			m->Manifest.Name = encoding;
			m->Manifest.Exec = {"mapper"};
			auto [result, err] =
			    r->Transform(m, {"/a.vue", "éx"});
			assert::NilError(t, err);
			auto segments = spanmap::Segments(result.Mappings);
			assert::Equal(t, segments.size(), size_t(2));
			assert::Equal(t, (int)segments[0].VirtualEnd, 2);
			assert::Equal(t, (int)segments[0].OriginalEnd, 2);
			assert::Equal(t, (int)segments[1].VirtualStart, 2);
			assert::Equal(t, (int)segments[1].OriginalStart, 2);
			assert::Equal(t, result.Text, std::string("éx"));
			auto problem =
			    spanmap::Validate(result.Mappings, result.Text, "éx");
			assert::Assert(t, !problem.has_value());
			auto [mapped, fidelity] =
			    spanmap::VirtualToOriginalPosition(result.Mappings, 2);
			assert::Equal(t, (int)mapped, 2);
			assert::Assert(t, fidelity == spanmap::FidelityExact);
			assert::Equal(t, (int)result.Diagnostics[0]->loc.pos(), 2);
			assert::Equal(t, (int)result.Diagnostics[0]->loc.end(), 3);
			assert::Equal(
			    t, (int)result.DiagnosticDirectives[0].OriginalRange.pos(),
			    2);
			assert::Equal(
			    t, (int)result.DiagnosticDirectives[0].OriginalRange.end(),
			    3);
			assert::Equal(
			    t, (int)result.DiagnosticDirectives[0].VirtualRange.pos(),
			    2);
			assert::Equal(
			    t, (int)result.DiagnosticDirectives[0].VirtualRange.end(),
			    3);
			r->Close();
		});
	}
}

void TestRunnerRejectsUnsupportedPositionEncoding(T* t) {
	t->Parallel();
	auto um = std::make_shared<unicodeMapper>();
	um->encoding = "utf-32";
	fakeSpawner fs;
	fs.handler = um;
	auto r = cm::NewHost(t->Context(), &fs, locale::Default);
	auto* m = new cm::Mapper();
	m->Manifest.Name = "invalid";
	m->Manifest.Exec = {"mapper"};
	auto [_, err] = r->Transform(m, {"/a.vue", "x"});
	errorContains(t, err, "unsupported position encoding");
	r->Close();
}

void TestRunnerRejectsInvalidDiagnosticSource(T* t) {
	t->Parallel();
	for (const char* src :
	     {"", " ", "ts", "TS", "d.ts", "json", "typescript", "TypeScript",
	      "tsc", "TSC"}) {
		std::string source = src;
		t->Run(source.empty() ? std::string("\"\"") : source, [&](T* t) {
			t->Parallel();
			auto um = std::make_shared<unicodeMapper>();
			um->encoding = cm::PositionEncodingUTF8;
			um->source = &source;
			fakeSpawner fs;
			fs.handler = um;
			auto r = cm::NewHost(t->Context(), &fs, locale::Default);
			auto* m = new cm::Mapper();
			m->Manifest.Name = "invalid";
			m->Manifest.Exec = {"mapper"};
			auto [_, err] = r->Transform(m, {"/a.vue", "x"});
			std::string trimmed = source;
			trimmed.erase(
			    0, trimmed.find_first_not_of(" \t\n\r\f\v"));
			trimmed.erase(
			    trimmed.find_last_not_of(" \t\n\r\f\v") + 1);
			if (trimmed.empty()) {
				errorContains(t, err,
				              "diagnostic source must not be empty");
			} else {
				errorContains(t, err, "is reserved by TypeScript");
			}
			r->Close();
		});
	}
}

void TestRunnerRejectsPositionsInsideUnicodeCharacters(T* t) {
	t->Parallel();
	struct {
		cm::PositionEncoding encoding;
		const char* content;
	} tests[] = {
	    {cm::PositionEncodingUTF8, "é"},
	    {cm::PositionEncodingUTF16, "😀"},
	};
	for (auto& test : tests) {
		t->Run(test.encoding, [&](T* t) {
			t->Parallel();
			auto dm = std::make_shared<invalidDiagnosticMapper>();
			dm->encoding = test.encoding;
			fakeSpawner fs;
			fs.handler = dm;
			auto r = cm::NewHost(t->Context(), &fs, locale::Default);
			auto* m = new cm::Mapper();
			m->Manifest.Name = test.encoding;
			m->Manifest.Exec = {"mapper"};
			auto [_, err] =
			    r->Transform(m, {"/a.vue", test.content});
			errorContains(t, err, "splits a Unicode code point");
			r->Close();
		});
	}
}

void TestRunnerConsolidatesByIdentity(T* t) {
	t->Parallel();
	fakeSpawner spawner;
	auto r = cm::NewHost(t->Context(), &spawner, locale::Default);

	// Two logically-separate mappers with the same identity share one
	// process.
	auto* vueA = new cm::Mapper();
	vueA->Definition.Package = "a";
	vueA->Manifest.Name = "vue";
	vueA->Manifest.Version = "1.0.0";
	vueA->Manifest.Exec = {"vue-mapper"};
	auto* vueB = new cm::Mapper();
	vueB->Definition.Package = "b";
	vueB->Manifest.Name = "vue";
	vueB->Manifest.Version = "1.0.0";
	vueB->Manifest.Exec = {"vue-mapper"};
	auto* svelte = new cm::Mapper();
	svelte->Manifest.Name = "svelte";
	svelte->Manifest.Version = "2.0.0";
	svelte->Manifest.Exec = {"svelte-mapper"};
	CompilerOptions opts;
	auto project = r->Project({.ConfigFileName = "",
	                           .Mappers = {vueA, vueB, svelte},
	                           .CompilerOptions = &opts});

	for (auto* mm : {vueA, vueB, vueA, svelte}) {
		auto [_, err] = project->Transform(mm, {"/x", "y"});
		assert::NilError(t, err);
	}
	assert::Equal(t, spawner.spawns.load(), 2);
	project->Close();
	r->Close();
}

void TestRunnerLeaseLifecycle(T* t) {
	t->Parallel();
	fakeSpawner spawner;
	auto r = cm::NewHost(t->Context(), &spawner, locale::Default);

	auto* vueA = new cm::Mapper();
	vueA->Definition.Package = "a";
	vueA->Manifest.Name = "vue";
	vueA->Manifest.Version = "1.0.0";
	vueA->Manifest.Exec = {"vue-mapper"};
	auto* vueB = new cm::Mapper();
	vueB->Definition.Package = "b";
	vueB->Manifest.Name = "vue";
	vueB->Manifest.Version = "1.0.0";
	vueB->Manifest.Exec = {"vue-mapper"};
	auto* svelte = new cm::Mapper();
	svelte->Manifest.Name = "svelte";
	svelte->Manifest.Version = "2.0.0";
	svelte->Manifest.Exec = {"svelte-mapper"};

	auto releaseVueA = r->Acquire({vueA, vueA});
	auto releaseVueB = r->Acquire({vueB});
	auto releaseSvelte = r->Acquire({svelte});
	for (auto* m : {vueA, svelte}) {
		auto [_, err] = r->Transform(m, {"/x", "y"});
		assert::NilError(t, err);
	}
	assert::Equal(t, spawner.spawns.load(), 2);

	releaseVueA();
	assert::Equal(t, spawner.closes.load(), 0);
	releaseSvelte();
	assert::Equal(t, spawner.closes.load(), 1);
	releaseVueB();
	releaseVueB();
	assert::Equal(t, spawner.closes.load(), 2);

	auto releaseNew = r->Acquire({vueA});
	auto [_, err] = r->Transform(vueA, {"/x", "y"});
	assert::NilError(t, err);
	assert::Equal(t, spawner.spawns.load(), 3);
	releaseNew();
	assert::Equal(t, spawner.closes.load(), 3);
	r->Close();
}

// recordingMapper captures project configuration and lifecycle requests for
// host protocol tests.
struct recordingMapper : mapperHandler {
	std::mutex mu;
	std::string received;
	std::string receivedOptions;
	std::string receivedLocale;
	std::vector<std::string> projectHandles;
	std::vector<std::string> closedHandles;
	std::string transformHandle;
	std::string transformParams;
	std::vector<std::string> watchedFiles;
	std::optional<std::string> configIdentity;
	bool dynamicConfig = false;
	std::vector<cm::OptionDiagnosticResult> optionDiagnostics;

	bool handlesProjects() override { return true; }

	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			auto p = unmarshalInitializeParams(params);
			{
				std::lock_guard<std::mutex> l(mu);
				receivedLocale = p.locale;
			}
			return {marshalInitializeResult({cm::PositionEncodingUTF8,
			                                 "mapper"}),
			        nullptr};
		}
		if (method == cm::MethodOpenProject) {
			auto p = unmarshalOpenProjectParams(params);
			std::vector<std::string> watched;
			bool dyn;
			std::optional<std::string> configIdentityOverride;
			std::vector<cm::OptionDiagnosticResult> diags;
			{
				std::lock_guard<std::mutex> l(mu);
				projectHandles.push_back(p.projectHandle);
				received = p.compilerOptions;
				receivedOptions = p.options;
				watched = watchedFiles;
				dyn = dynamicConfig;
				configIdentityOverride = configIdentity;
				diags = optionDiagnostics;
			}
			if (!dyn && watched.empty() &&
			    !configIdentityOverride.has_value() && diags.empty()) {
				return {marshalOpenProjectResult({}), nullptr};
			}
			if (dyn && watched.empty()) {
				watched = {tspath::combinePaths(
				    tspath::getDirectoryPath(p.configFileName),
				    {"mapper.config.js"})};
			}
			std::string configIdentity;
			if (dyn) {
				configIdentity = "config:" + p.options;
			}
			if (configIdentityOverride.has_value()) {
				configIdentity = *configIdentityOverride;
			}
			cm::OpenProjectResult res;
			res.ConfigIdentity = configIdentity;
			res.WatchedFiles = watched;
			res.OptionDiagnostics = diags;
			return {marshalOpenProjectResult(res), nullptr};
		}
		if (method == cm::MethodCloseProject) {
			auto p = unmarshalCloseProjectParams(params);
			std::lock_guard<std::mutex> l(mu);
			closedHandles.push_back(p.projectHandle);
			return {json::Value("null"), nullptr};
		}
		if (method == cm::MethodTransform) {
			auto p = unmarshalTransformParams(params);
			{
				std::lock_guard<std::mutex> l(mu);
				transformHandle = p.projectHandle;
				transformParams = std::string(params);
			}
			cm::TransformResult res;
			res.Text = p.content;
			res.Extension = ".ts";
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::newError("unexpected method " + std::string(method))};
	}
	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

struct blockingMapper : recordingMapper {
	gate started;
	gate proceed;

	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		if (method == cm::MethodTransform) {
			started.close();
			proceed.wait();
		}
		return recordingMapper::HandleRequest(ctx, method, params);
	}
};

void TestProjectLifecycle(T* t) {
	t->Parallel();
	auto mapperProcess = std::make_shared<recordingMapper>();
	mapperProcess->dynamicConfig = true;
	fakeSpawner spawner;
	spawner.handler = mapperProcess;
	auto host = cm::NewHost(t->Context(), &spawner, locale::Default);

	auto* staticMapper = new cm::Mapper();
	staticMapper->Definition.Options = json::Value(R"({"mode":"static"})");
	staticMapper->Manifest.Name = "static";
	staticMapper->Manifest.Version = "1.0.0";
	staticMapper->Manifest.Exec = {"mapper"};
	CompilerOptions opts1;
	auto staticProject = host->Project({.ConfigFileName =
	                                        "/repo/tsconfig.json",
	                                    .Mappers = {staticMapper},
	                                    .CompilerOptions = &opts1});
	assert::Equal(t, spawner.spawns.load(), 0);
	auto [staticIdentities, sierr] = staticProject->Identities();
	assert::NilError(t, sierr);
	assert::Equal(t, staticIdentities.size(), size_t(1));
	assert::NilError(t, staticProject->Close());

	auto* dynamicA = new cm::Mapper();
	dynamicA->Definition.Options = json::Value(R"({"mode":"a"})");
	dynamicA->Manifest.Name = "dynamic";
	dynamicA->Manifest.Version = "1.0.0";
	dynamicA->Manifest.Exec = {"mapper"};
	dynamicA->Manifest.CompilerOptions = {"jsx"};
	dynamicA->Manifest.DynamicConfig = true;
	auto* dynamicB = new cm::Mapper();
	dynamicB->Definition.Options = json::Value(R"({"mode":"b"})");
	dynamicB->Manifest.Name = "dynamic";
	dynamicB->Manifest.Version = "1.0.0";
	dynamicB->Manifest.Exec = {"mapper"};
	dynamicB->Manifest.DynamicConfig = true;
	CompilerOptions dynamicAOptions;
	auto projectA = host->Project({.ConfigFileName = "/repo/a/tsconfig.json",
	                               .Mappers = {dynamicA, dynamicB},
	                               .CompilerOptions = &dynamicAOptions});
	auto projectAReversed =
	    host->Project({.ConfigFileName = "/repo/reversed/tsconfig.json",
	                   .Mappers = {dynamicB, dynamicA},
	                   .CompilerOptions = &dynamicAOptions});
	CompilerOptions jsxOpts;
	jsxOpts.Jsx = JsxEmit::React;
	auto projectDifferentOptions =
	    host->Project({.ConfigFileName = "/repo/options/tsconfig.json",
	                   .Mappers = {dynamicA},
	                   .CompilerOptions = &jsxOpts});
	CompilerOptions opts2;
	auto projectB = host->Project({.ConfigFileName = "/repo/b/tsconfig.json",
	                               .Mappers = {dynamicA},
	                               .CompilerOptions = &opts2});
	auto projectAAgain =
	    host->Project({.ConfigFileName = "/repo/a/tsconfig.json",
	                   .Mappers = {dynamicA, dynamicB},
	                   .CompilerOptions = &dynamicAOptions});
	assert::Equal(t, spawner.spawns.load(), 0);
	auto [projectAIdentities, e1] = projectA->Identities();
	assert::NilError(t, e1);
	auto [projectAReversedIdentities, e2] = projectAReversed->Identities();
	assert::NilError(t, e2);
	auto [projectDifferentOptionIdentities, e3] =
	    projectDifferentOptions->Identities();
	assert::NilError(t, e3);
	auto [projectBIdentities, e4] = projectB->Identities();
	assert::NilError(t, e4);
	assert::Equal(t, projectAIdentities.size(), size_t(2));
	assert::Equal(t, projectAReversedIdentities.size(), size_t(2));
	assert::Equal(t, projectAIdentities[0], projectAReversedIdentities[1]);
	assert::Equal(t, projectAIdentities[1], projectAReversedIdentities[0]);
	assert::Assert(t, projectAIdentities[0] !=
	                  projectDifferentOptionIdentities[0]);
	assert::Equal(t, projectBIdentities.size(), size_t(1));
	assert::Equal(t, spawner.spawns.load(), 1);
	auto [projectAWatchedFiles, e5] = projectA->WatchedFiles();
	assert::NilError(t, e5);
	auto [projectBWatchedFiles, e6] = projectB->WatchedFiles();
	assert::NilError(t, e6);
	assert::Equal(t, projectAWatchedFiles.size(), size_t(1));
	assert::Equal(t, projectBWatchedFiles.size(), size_t(1));

	auto [_, terr] = projectA->Transform(
	    dynamicB, {"/repo/a/file.ext", "x"});
	assert::NilError(t, terr);
	{
		std::lock_guard<std::mutex> l(mapperProcess->mu);
		auto& ph = mapperProcess->projectHandles;
		assert::Assert(t, ph.size() >= 2 &&
		                  (ph[0] == mapperProcess->transformHandle ||
		                   ph[1] == mapperProcess->transformHandle));
	}

	assert::NilError(t, projectAAgain->Close());
	assert::NilError(t, projectA->Close());
	assert::NilError(t, projectAReversed->Close());
	assert::NilError(t, projectDifferentOptions->Close());
	assert::NilError(t, projectB->Close());
	auto timings = host->Timings();
	auto& dynamicTimings = timings.Mappers[dynamicA->Identity()];
	assert::Equal(t, dynamicTimings.Spawn.Count, uint64_t(1));
	assert::Equal(t, dynamicTimings.Initialize.Count, uint64_t(1));
	assert::Equal(t, dynamicTimings.OpenProject.Count, uint64_t(6));
	assert::Equal(t, dynamicTimings.Transform.Count, uint64_t(1));
	assert::Equal(t, dynamicTimings.CloseProject.Count, uint64_t(6));
	{
		std::lock_guard<std::mutex> l(mapperProcess->mu);
		assert::Equal(t, mapperProcess->projectHandles.size(), size_t(6));
		assert::Equal(t, mapperProcess->closedHandles.size(), size_t(6));
	}
	host->Close();
}

void TestProjectMethodsAfterHostClose(T* t) {
	t->Parallel();
	auto mapperProcess = std::make_shared<recordingMapper>();
	mapperProcess->dynamicConfig = true;
	fakeSpawner fs;
	fs.handler = mapperProcess;
	auto host = cm::NewHost(t->Context(), &fs, locale::Default);
	auto* m = new cm::Mapper();
	m->Manifest.Name = "dynamic";
	m->Manifest.Version = "1.0.0";
	m->Manifest.Exec = {"mapper"};
	m->Manifest.DynamicConfig = true;
	CompilerOptions opts;
	auto project = host->Project({.ConfigFileName = "/repo/tsconfig.json",
	                              .Mappers = {m},
	                              .CompilerOptions = &opts});
	auto [_, terr] = project->Transform(m, {"/repo/file.ext", "x"});
	assert::NilError(t, terr);
	auto* emptyMapper = new cm::Mapper();
	auto [identity, ierr] = project->Identity(emptyMapper);
	assert::NilError(t, ierr);
	assert::Equal(t, identity, std::string(""));
	assert::NilError(t, host->Close());

	assert::NilError(t, project->Refresh());
	auto [identities, i2err] = project->Identities();
	assert::NilError(t, i2err);
	assert::Equal(t, identities.size(), size_t(0));
	auto [identity2, i3err] = project->Identity(m);
	assert::NilError(t, i3err);
	assert::Equal(t, identity2, std::string(""));
	auto [identity3, i4err] = project->Identity(emptyMapper);
	assert::NilError(t, i4err);
	assert::Equal(t, identity3, std::string(""));
	auto [watchedFiles, werr] = project->WatchedFiles();
	assert::NilError(t, werr);
	assert::Equal(t, watchedFiles.size(), size_t(0));
	assert::Equal(t, project->Diagnostics().size(), size_t(0));
	auto [__, terr2] = project->Transform(m, {"/repo/file.ext", "x"});
	errorContains(t, terr2, "content mapper project is closed");
	project->Close();
}

void TestProjectRejectsRelativeWatchedFiles(T* t) {
	t->Parallel();
	auto mapperProcess = std::make_shared<recordingMapper>();
	mapperProcess->watchedFiles = {"mapper.config.js"};
	mapperProcess->dynamicConfig = true;
	fakeSpawner fs;
	fs.handler = mapperProcess;
	auto host = cm::NewHost(t->Context(), &fs, locale::Default);
	auto* projectMapper = new cm::Mapper();
	projectMapper->Definition.Package = "dynamic";
	projectMapper->Manifest.Name = "dynamic";
	projectMapper->Manifest.Version = "1.0.0";
	projectMapper->Manifest.Exec = {"mapper"};
	projectMapper->Manifest.DynamicConfig = true;
	CompilerOptions opts;
	auto project = host->Project({.ConfigFileName = "/repo/tsconfig.json",
	                              .Mappers = {projectMapper},
	                              .CompilerOptions = &opts});
	auto [_, err] =
	    project->Transform(projectMapper, {"/repo/file.ext", "x"});
	auto* transformError = gostd::errorAs<cm::TransformError*>(err);
	assert::Assert(t, transformError != nullptr);
	auto* projectError = gostd::errorAs<cm::ProjectError*>(err);
	assert::Assert(t, projectError != nullptr);
	assert::Assert(t, projectError->Kind ==
	                  cm::ProjectErrorKindNonAbsoluteWatchedFile);
	project->Close();
	host->Close();
}

void TestDynamicProjectRequiresConfigIdentity(T* t) {
	t->Parallel();
	auto mapperProcess = std::make_shared<recordingMapper>();
	mapperProcess->configIdentity = std::string("");
	mapperProcess->dynamicConfig = true;
	fakeSpawner fs;
	fs.handler = mapperProcess;
	auto host = cm::NewHost(t->Context(), &fs, locale::Default);
	auto* projectMapper = new cm::Mapper();
	projectMapper->Definition.Package = "dynamic";
	projectMapper->Manifest.Name = "dynamic";
	projectMapper->Manifest.Version = "1.0.0";
	projectMapper->Manifest.Exec = {"mapper"};
	projectMapper->Manifest.DynamicConfig = true;
	CompilerOptions opts;
	auto project = host->Project({.ConfigFileName = "/repo/tsconfig.json",
	                              .Mappers = {projectMapper},
	                              .CompilerOptions = &opts});
	auto [_, err] =
	    project->Transform(projectMapper, {"/repo/file.ext", "x"});
	auto* transformError = gostd::errorAs<cm::TransformError*>(err);
	assert::Assert(t, transformError != nullptr);
	auto* projectError = gostd::errorAs<cm::ProjectError*>(err);
	assert::Assert(t, projectError != nullptr);
	assert::Assert(t, projectError->Kind ==
	                  cm::ProjectErrorKindMissingConfigIdentity);
	project->Close();
	host->Close();
}

void TestStaticMapperRejectsDynamicProjectResponseFields(T* t) {
	t->Parallel();
	std::string configIdentity = "dynamic";
	struct {
		const char* name;
		std::function<void(recordingMapper*)> configure;
		cm::ProjectErrorKind kind;
	} tests[] = {
	    {"config identity",
	     [&](recordingMapper* m) { m->configIdentity = configIdentity; },
	     cm::ProjectErrorKindUnexpectedConfigIdentity},
	    {"watched files",
	     [&](recordingMapper* m) {
		     m->watchedFiles = {"/repo/mapper.config.js"};
	     },
	     cm::ProjectErrorKindUnexpectedWatchedFiles},
	};
	for (auto& test : tests) {
		t->Run(test.name, [&](T* t) {
			t->Parallel();
			auto rm = std::make_shared<recordingMapper>();
			test.configure(rm.get());
			fakeSpawner fs;
			fs.handler = rm;
			auto host = cm::NewHost(t->Context(), &fs, locale::Default);
			auto* projectMapper = new cm::Mapper();
			projectMapper->Manifest.Name = "static";
			projectMapper->Manifest.Version = "1.0.0";
			projectMapper->Manifest.Exec = {"mapper"};
			CompilerOptions opts;
			auto project =
			    host->Project({.ConfigFileName = "/repo/tsconfig.json",
			                   .Mappers = {projectMapper},
			                   .CompilerOptions = &opts});
			auto [_, err] = project->Transform(projectMapper,
			                                   {"/repo/file.ext", "x"});
			auto* transformError =
			    gostd::errorAs<cm::TransformError*>(err);
			assert::Assert(t, transformError != nullptr);
			auto* projectError = gostd::errorAs<cm::ProjectError*>(err);
			assert::Assert(t, projectError != nullptr);
			assert::Assert(t, projectError->Kind == test.kind);
			project->Close();
			host->Close();
		});
	}
}

void TestProjectRejectsInvalidOptionDiagnosticPath(T* t) {
	t->Parallel();
	auto mapperProcess = std::make_shared<recordingMapper>();
	mapperProcess->optionDiagnostics = {cm::OptionDiagnosticResult{
	    {json::Value("null")}, "Invalid option.", 123}};
	fakeSpawner fs;
	fs.handler = mapperProcess;
	auto host = cm::NewHost(t->Context(), &fs, locale::Default);
	auto* projectMapper = new cm::Mapper();
	projectMapper->Manifest.Name = "mapper";
	projectMapper->Manifest.Version = "1.0.0";
	projectMapper->Manifest.Exec = {"mapper"};
	CompilerOptions opts;
	auto project = host->Project({.Mappers = {projectMapper},
	                              .CompilerOptions = &opts});
	auto [_, err] =
	    project->Transform(projectMapper, {"/repo/file.ext", "x"});
	auto* transformError = gostd::errorAs<cm::TransformError*>(err);
	assert::Assert(t, transformError != nullptr);
	auto* projectError = gostd::errorAs<cm::ProjectError*>(err);
	assert::Assert(t, projectError != nullptr);
	assert::Assert(t,
	               projectError->Kind == cm::ProjectErrorKindMalformedResponse);
	project->Close();
	host->Close();
}

void TestRunnerForwardsProjectOptions(T* t) {
	t->Parallel();
	auto m = std::make_shared<recordingMapper>();
	auto [diagnosticLocale, ok] = locale::parse("cs-CZ");
	assert::Assert(t, ok);
	fakeSpawner fs;
	fs.handler = m;
	auto r = cm::NewHost(t->Context(), &fs, diagnosticLocale);

	auto* mapperDefinition = new cm::Mapper();
	mapperDefinition->Definition.Options =
	    json::Value(R"({"strictTemplates":true})");
	mapperDefinition->Manifest.Name = "vue";
	mapperDefinition->Manifest.Version = "1.0.0";
	mapperDefinition->Manifest.Exec = {"vue-mapper"};
	mapperDefinition->Manifest.CompilerOptions = {"target", "jsx"};
	auto* compilerOptions = new CompilerOptions();
	compilerOptions->Target = ScriptTarget::ES2020;
	compilerOptions->Strict = Tristate::True;
	auto project = r->Project({.Mappers = {mapperDefinition},
	                           .CompilerOptions = compilerOptions});
	auto [_, err] =
	    project->Transform(mapperDefinition, {"/a.vue", "x"});
	assert::NilError(t, err);

	auto want = cm::detail::marshalCompilerOptions(compilerOptions);
	{
		std::lock_guard<std::mutex> l(m->mu);
		assert::Equal(t, m->received, std::string(want));
		assert::Equal(t, m->receivedOptions,
		              std::string(R"({"strictTemplates":true})"));
		assert::Equal(t, m->receivedLocale, std::string("cs-CZ"));
		assert::Assert(t,
		               m->transformParams.find("\"options\"") ==
		                   std::string::npos);
		assert::Assert(t,
		               m->transformParams.find("\"compilerOptions\"") ==
		                   std::string::npos);
	}
	project->Close();
	r->Close();
}

void TestHostSetLocaleRestartsMapper(T* t) {
	t->Parallel();
	auto m = std::make_shared<recordingMapper>();
	fakeSpawner spawner;
	spawner.handler = m;
	auto r = cm::NewHost(t->Context(), &spawner, locale::Default);

	auto* definition = new cm::Mapper();
	definition->Manifest.Name = "vue";
	definition->Manifest.Version = "1.0.0";
	definition->Manifest.Exec = {"vue-mapper"};
	auto release = r->Acquire({definition});

	auto [_, err] = r->Transform(definition, {"/a.vue", "x"});
	assert::NilError(t, err);
	assert::Equal(t, spawner.spawns.load(), 1);

	auto [french, ok] = locale::parse("fr");
	assert::Assert(t, ok);
	r->SetLocale(french);
	assert::Equal(t, spawner.closes.load(), 1);

	auto [__, err2] = r->Transform(definition, {"/a.vue", "x"});
	assert::NilError(t, err2);
	assert::Equal(t, spawner.spawns.load(), 2);
	{
		std::lock_guard<std::mutex> l(m->mu);
		assert::Equal(t, m->receivedLocale, std::string("fr"));
	}
	release();
	r->Close();
}

void TestHostSetLocaleWaitsForTransform(T* t) {
	t->Parallel();
	auto m = std::make_shared<blockingMapper>();
	fakeSpawner spawner;
	spawner.handler = m;
	auto r = cm::NewHost(t->Context(), &spawner, locale::Default);
	auto* definition = new cm::Mapper();
	definition->Manifest.Name = "vue";
	definition->Manifest.Version = "1.0.0";
	definition->Manifest.Exec = {"vue-mapper"};

	std::mutex transformMu;
	std::condition_variable transformCv;
	gostd::Error transformErr;
	bool transformFinished = false;
	std::thread transformThread([&] {
		auto [_, e] = r->Transform(definition, {"/a.vue", "x"});
		std::lock_guard<std::mutex> l(transformMu);
		transformErr = e;
		transformFinished = true;
		transformCv.notify_all();
	});
	m->started.wait();

	auto [french, ok] = locale::parse("fr");
	assert::Assert(t, ok);
	std::mutex setMu;
	std::condition_variable setCv;
	bool setStarted = false;
	bool setDone = false;
	auto frenchCopy = french;
	std::thread setThread([&, frenchCopy] {
		{
			std::lock_guard<std::mutex> l(setMu);
			setStarted = true;
			setCv.notify_all();
		}
		r->SetLocale(frenchCopy);
		std::lock_guard<std::mutex> l(setMu);
		setDone = true;
		setCv.notify_all();
	});
	{
		std::unique_lock<std::mutex> l(setMu);
		setCv.wait(l, [&] { return setStarted; });
	}
	bool setCompleted;
	{
		std::lock_guard<std::mutex> l(setMu);
		setCompleted = setDone;
	}
	assert::Assert(t, !setCompleted,
	               "SetLocale completed while a transform was in flight");

	m->proceed.close();
	{
		std::unique_lock<std::mutex> l(transformMu);
		transformCv.wait(l, [&] { return transformFinished; });
	}
	assert::NilError(t, transformErr);
	{
		std::unique_lock<std::mutex> l(setMu);
		setCv.wait(l, [&] { return setDone; });
	}
	assert::Equal(t, spawner.closes.load(), 1);
	transformThread.join();
	setThread.join();
	r->Close();
}

} // namespace

REGISTER_UNIT_TEST("contentmapper.TestRunnerTransform", TestRunnerTransform);
REGISTER_UNIT_TEST("contentmapper.TestHostLogging", TestHostLogging);
REGISTER_UNIT_TEST("contentmapper.TestHostDiscardsStderrWithoutLogging",
                   TestHostDiscardsStderrWithoutLogging);
REGISTER_UNIT_TEST("contentmapper.TestMapperDiagnosticName",
                   TestMapperDiagnosticName);
REGISTER_UNIT_TEST("contentmapper.TestRunnerTransformResponseValidation",
                   TestRunnerTransformResponseValidation);
REGISTER_UNIT_TEST("contentmapper.TestHostClosesProcessWhenReadLoopFails",
                   TestHostClosesProcessWhenReadLoopFails);
REGISTER_UNIT_TEST(
    "contentmapper.TestHostReportsInitializationTimeoutBeforeClosingProcess",
    TestHostReportsInitializationTimeoutBeforeClosingProcess);
REGISTER_UNIT_TEST("contentmapper.TestHostReportsProcessExitBeforeInitialization",
                   TestHostReportsProcessExitBeforeInitialization);
REGISTER_UNIT_TEST("contentmapper.TestRunnerTransformDiagnosticDirectives",
                   TestRunnerTransformDiagnosticDirectives);
REGISTER_UNIT_TEST("contentmapper.TestMappedDiagnosticDirectiveJSON",
                   TestMappedDiagnosticDirectiveJSON);
REGISTER_UNIT_TEST("contentmapper.TestRunnerTransformSupplementalOutputs",
                   TestRunnerTransformSupplementalOutputs);
REGISTER_UNIT_TEST(
    "contentmapper.TestRunnerTransformInvalidSupplementalDiagnosticDirective",
    TestRunnerTransformInvalidSupplementalDiagnosticDirective);
REGISTER_UNIT_TEST("contentmapper.TestRunnerRejectsInvalidVirtualExtension",
                   TestRunnerRejectsInvalidVirtualExtension);
REGISTER_UNIT_TEST("contentmapper.TestRunnerPositionEncodings",
                   TestRunnerPositionEncodings);
REGISTER_UNIT_TEST(
    "contentmapper.TestRunnerRejectsUnsupportedPositionEncoding",
    TestRunnerRejectsUnsupportedPositionEncoding);
REGISTER_UNIT_TEST("contentmapper.TestRunnerRejectsInvalidDiagnosticSource",
                   TestRunnerRejectsInvalidDiagnosticSource);
REGISTER_UNIT_TEST(
    "contentmapper.TestRunnerRejectsPositionsInsideUnicodeCharacters",
    TestRunnerRejectsPositionsInsideUnicodeCharacters);
REGISTER_UNIT_TEST("contentmapper.TestRunnerConsolidatesByIdentity",
                   TestRunnerConsolidatesByIdentity);
REGISTER_UNIT_TEST("contentmapper.TestRunnerLeaseLifecycle",
                   TestRunnerLeaseLifecycle);
REGISTER_UNIT_TEST("contentmapper.TestProjectLifecycle", TestProjectLifecycle);
REGISTER_UNIT_TEST("contentmapper.TestProjectMethodsAfterHostClose",
                   TestProjectMethodsAfterHostClose);
REGISTER_UNIT_TEST("contentmapper.TestProjectRejectsRelativeWatchedFiles",
                   TestProjectRejectsRelativeWatchedFiles);
REGISTER_UNIT_TEST("contentmapper.TestDynamicProjectRequiresConfigIdentity",
                   TestDynamicProjectRequiresConfigIdentity);
REGISTER_UNIT_TEST(
    "contentmapper.TestStaticMapperRejectsDynamicProjectResponseFields",
    TestStaticMapperRejectsDynamicProjectResponseFields);
REGISTER_UNIT_TEST("contentmapper.TestProjectRejectsInvalidOptionDiagnosticPath",
                   TestProjectRejectsInvalidOptionDiagnosticPath);
REGISTER_UNIT_TEST("contentmapper.TestRunnerForwardsProjectOptions",
                   TestRunnerForwardsProjectOptions);
REGISTER_UNIT_TEST("contentmapper.TestHostSetLocaleRestartsMapper",
                   TestHostSetLocaleRestartsMapper);
REGISTER_UNIT_TEST("contentmapper.TestHostSetLocaleWaitsForTransform",
                   TestHostSetLocaleWaitsForTransform);
