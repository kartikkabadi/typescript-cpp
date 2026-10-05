// Port of tsc/internal/contentmapper/hostimpl.go: the Host implementation
// that drives content mapper processes over JSON-RPC.
#include "internal/contentmapper/contentmapper.h"

#include <algorithm>
#include <charconv>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

#include "internal/core/spelling.h"
#include "internal/jsonrpc/jsonrpc.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"

namespace tsc::contentmapper {

namespace {

// ---------------------------------------------------------------------------
// JSON marshaling for the protocol parameter/result types. Go uses encoding
// tags on the structs; the port writes the same member order explicitly.
// ---------------------------------------------------------------------------

json::Value marshalInitializeParams(const InitializeParams& p) {
	std::vector<std::pair<std::string, std::string>> members;
	if (!p.Locale.empty()) { // `json:"locale,omitempty"`
		members.emplace_back("locale", json::marshalString(p.Locale));
	}
	std::vector<json::Value> encodings;
	encodings.reserve(p.PositionEncodings.size());
	for (const auto& e : p.PositionEncodings) {
		encodings.push_back(json::marshalString(e));
	}
	members.emplace_back("positionEncodings", json::marshalArray(encodings));
	return json::marshalObject(members);
}

// marshalCompilerOptions marshals a whole CompilerOptions (json.Marshal):
// every field carries `,omitzero`, so only non-zero fields are emitted.
// Field order is irrelevant to consumers (the mapper decodes the object).
// Defined in contentmapper.cpp next to the field table.

json::Value marshalOpenProjectParams(const OpenProjectParams& p) {
	std::vector<std::pair<std::string, std::string>> members;
	members.emplace_back("configFileName",
	                     json::marshalString(p.ConfigFileName));
	members.emplace_back("projectHandle",
	                     json::marshalString(p.ProjectHandle));
	if (!p.Options.empty()) { // `json:"options,omitempty"`
		members.emplace_back("options", p.Options);
	}
	members.emplace_back("compilerOptions", p.CompilerOptions);
	return json::marshalObject(members);
}

json::Value marshalCloseProjectParams(const CloseProjectParams& p) {
	return json::marshalObject(
	    {{"projectHandle", json::marshalString(p.ProjectHandle)}});
}

json::Value marshalTransformParams(const TransformParams& p) {
	return json::marshalObject(
	    {{"fileName", json::marshalString(p.FileName)},
	     {"content", json::marshalString(p.Content)},
	     {"projectHandle", json::marshalString(p.ProjectHandle)}});
}

std::pair<InitializeResult, gostd::Error> unmarshalInitializeResult(
    const json::Value& raw) {
	auto [d, err] = json::parse(raw);
	if (err) {
		return {InitializeResult{}, err};
	}
	if (d.kind != json::Dom::K::Object) {
		return {InitializeResult{},
		        gostd::newError("json: cannot unmarshal non-object into "
		                        "InitializeResult")};
	}
	InitializeResult res;
	if (const json::Dom* v = json::objGet(d, "positionEncoding")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return {InitializeResult{}, e};
		res.PositionEncoding = std::move(s);
	}
	if (const json::Dom* v = json::objGet(d, "diagnosticSource")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return {InitializeResult{}, e};
		res.DiagnosticSource = std::move(s);
	}
	return {res, nullptr};
}

std::pair<OpenProjectResult, gostd::Error> unmarshalOpenProjectResult(
    const json::Value& raw) {
	auto [d, err] = json::parse(raw);
	if (err) {
		return {OpenProjectResult{}, err};
	}
	if (d.kind != json::Dom::K::Object) {
		return {OpenProjectResult{},
		        gostd::newError("json: cannot unmarshal non-object into "
		                        "OpenProjectResult")};
	}
	OpenProjectResult res;
	if (const json::Dom* v = json::objGet(d, "configIdentity")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return {OpenProjectResult{}, e};
		res.ConfigIdentity = std::move(s);
	}
	if (const json::Dom* v = json::objGet(d, "watchedFiles");
	    v != nullptr && v->kind != json::Dom::K::Null) {
		if (v->kind != json::Dom::K::Array) {
			return {OpenProjectResult{},
			        gostd::newError("json: cannot unmarshal non-array into "
			                        "watchedFiles")};
		}
		for (const auto& item : v->arr) {
			auto [s, e] = json::asString(item, "string");
			if (e) return {OpenProjectResult{}, e};
			res.WatchedFiles.push_back(std::move(s));
		}
	}
	if (const json::Dom* v = json::objGet(d, "optionDiagnostics");
	    v != nullptr && v->kind != json::Dom::K::Null) {
		if (v->kind != json::Dom::K::Array) {
			return {OpenProjectResult{},
			        gostd::newError("json: cannot unmarshal non-array into "
			                        "optionDiagnostics")};
		}
		for (const auto& item : v->arr) {
			if (item.kind != json::Dom::K::Object) {
				return {OpenProjectResult{},
				        gostd::newError("json: cannot unmarshal non-object "
				                        "into optionDiagnostics")};
			}
			OptionDiagnosticResult od;
			if (const json::Dom* p = json::objGet(item, "path");
			    p != nullptr && p->kind != json::Dom::K::Null) {
				if (p->kind != json::Dom::K::Array) {
					return {OpenProjectResult{},
					        gostd::newError("json: cannot unmarshal non-array "
					                        "into path")};
				}
				for (const auto& seg : p->arr) {
					od.Path.push_back(json::Value(seg.raw));
				}
			}
			if (const json::Dom* m = json::objGet(item, "messageText")) {
				auto [s, e] = json::asString(*m, "string");
				if (e) return {OpenProjectResult{}, e};
				od.MessageText = std::move(s);
			}
			if (const json::Dom* c = json::objGet(item, "code")) {
				auto [n, e] = json::asInt32(*c, "int32");
				if (e) return {OpenProjectResult{}, e};
				od.Code = n;
			}
			res.OptionDiagnostics.push_back(std::move(od));
		}
	}
	return {res, nullptr};
}

std::pair<UnusedExpectDirectiveDiagnostic, gostd::Error>
unmarshalUnusedExpectDirectiveDiagnostic(const json::Dom& d) {
	UnusedExpectDirectiveDiagnostic u;
	if (d.kind != json::Dom::K::Object) {
		return {u, gostd::newError("json: cannot unmarshal non-object into "
		                         "UnusedExpectDirectiveDiagnostic")};
	}
	if (const json::Dom* v = json::objGet(d, "code")) {
		auto [n, e] = json::asInt32(*v, "int32");
		if (e) return {u, e};
		u.Code = n;
	}
	if (const json::Dom* v = json::objGet(d, "messageText")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return {u, e};
		u.MessageText = std::move(s);
	}
	return {u, nullptr};
}

std::pair<MappedOutput, gostd::Error> unmarshalMappedOutput(
    const json::Dom& d) {
	MappedOutput out;
	if (d.kind != json::Dom::K::Object) {
		return {out, gostd::newError("json: cannot unmarshal non-object into "
		                           "MappedOutput")};
	}
	if (const json::Dom* v = json::objGet(d, "text")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return {out, e};
		out.Text = std::move(s);
	}
	if (const json::Dom* v = json::objGet(d, "extension")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return {out, e};
		out.Extension = std::move(s);
	}
	if (const json::Dom* v = json::objGet(d, "mappings")) {
		out.Mappings = json::Value(v->raw);
	}
	if (const json::Dom* v = json::objGet(d, "diagnosticDirectives");
	    v != nullptr && v->kind != json::Dom::K::Null) {
		if (v->kind != json::Dom::K::Object) {
			return {out,
			        gostd::newError("json: cannot unmarshal non-object into "
			                        "DiagnosticDirectives")};
		}
		DiagnosticDirectives dd;
		if (const json::Dom* u = json::objGet(
		        *v, "unusedExpectDirectiveDiagnostics");
		    u != nullptr && u->kind != json::Dom::K::Null) {
			if (u->kind != json::Dom::K::Array) {
				return {out, gostd::newError(
				                 "json: cannot unmarshal non-array into "
				                 "unusedExpectDirectiveDiagnostics")};
			}
			for (const auto& item : u->arr) {
				auto [diag, e] =
				    unmarshalUnusedExpectDirectiveDiagnostic(item);
				if (e) return {out, e};
				dd.UnusedExpectDirectiveDiagnostics.push_back(
				    std::move(diag));
			}
		}
		if (const json::Dom* dir = json::objGet(*v, "directives");
		    dir != nullptr && dir->kind != json::Dom::K::Null) {
			if (dir->kind != json::Dom::K::Array) {
				return {out, gostd::newError(
				                 "json: cannot unmarshal non-array into "
				                 "directives")};
			}
			for (const auto& item : dir->arr) {
				MappedDiagnosticDirective directive;
				if (auto e = directive.unmarshalJSONFrom(
				        json::Value(item.raw))) {
					return {out, e};
				}
				dd.Directives.push_back(std::move(directive));
			}
		}
		out.DiagnosticDirectives = std::move(dd);
	}
	return {out, nullptr};
}

std::pair<Diagnostic, gostd::Error> unmarshalDiagnostic(const json::Dom& d) {
	Diagnostic diag;
	if (d.kind != json::Dom::K::Object) {
		return {diag, gostd::newError("json: cannot unmarshal non-object into "
		                            "Diagnostic")};
	}
	if (const json::Dom* v = json::objGet(d, "messageText")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return {diag, e};
		diag.MessageText = std::move(s);
	}
	if (const json::Dom* v = json::objGet(d, "start")) {
		auto [n, e] = json::asInt(*v, "int");
		if (e) return {diag, e};
		diag.Start = n;
	}
	if (const json::Dom* v = json::objGet(d, "length")) {
		auto [n, e] = json::asInt(*v, "int");
		if (e) return {diag, e};
		diag.Length = n;
	}
	if (const json::Dom* v = json::objGet(d, "code")) {
		auto [n, e] = json::asInt32(*v, "int32");
		if (e) return {diag, e};
		diag.Code = n;
	}
	return {diag, nullptr};
}

std::pair<TransformResult, gostd::Error> unmarshalTransformResult(
    const json::Value& raw) {
	auto [d, err] = json::parse(raw);
	if (err) {
		return {TransformResult{}, err};
	}
	auto [mapped, merr] = unmarshalMappedOutput(d);
	if (merr) {
		return {TransformResult{}, merr};
	}
	TransformResult res;
	static_cast<MappedOutput&>(res) = std::move(mapped);
	if (const json::Dom* v = json::objGet(d, "diagnostics");
	    v != nullptr && v->kind != json::Dom::K::Null) {
		if (v->kind != json::Dom::K::Array) {
			return {TransformResult{},
			        gostd::newError("json: cannot unmarshal non-array into "
			                        "diagnostics")};
		}
		for (const auto& item : v->arr) {
			auto [diag, e] = unmarshalDiagnostic(item);
			if (e) return {TransformResult{}, e};
			res.Diagnostics.push_back(std::move(diag));
		}
	}
	if (const json::Dom* v = json::objGet(d, "supplemental");
	    v != nullptr && v->kind != json::Dom::K::Null) {
		if (v->kind != json::Dom::K::Array) {
			return {TransformResult{},
			        gostd::newError("json: cannot unmarshal non-array into "
			                        "supplemental")};
		}
		for (const auto& item : v->arr) {
			auto [s, e] = unmarshalMappedOutput(item);
			if (e) return {TransformResult{}, e};
			SupplementalOutput supplemental;
			static_cast<MappedOutput&>(supplemental) = std::move(s);
			res.Supplemental.push_back(std::move(supplemental));
		}
	}
	return {res, nullptr};
}

std::string hexEncodeLower(const uint8_t* data, size_t n) {
	std::string out;
	out.reserve(n * 2);
	static const char digits[] = "0123456789abcdef";
	for (size_t i = 0; i < n; i++) {
		out += digits[data[i] >> 4];
		out += digits[data[i] & 0xF];
	}
	return out;
}

} // namespace

// MappedDiagnosticDirective.MarshalJSONTo — hostimpl.go:160.
json::Value MappedDiagnosticDirective::marshalJSONTo() const {
	std::vector<json::Value> tuple = {
	    json::marshalInt64(OriginalStart),
	    json::marshalInt64(OriginalLength),
	    json::marshalInt64(VirtualStart),
	    json::marshalInt64(VirtualEnd),
	    json::marshalInt64(Policy),
	};
	if (UnusedExpectDirectiveIndex.has_value()) {
		tuple.push_back(json::marshalInt64(*UnusedExpectDirectiveIndex));
	}
	return json::marshalArray(tuple);
}

// MappedDiagnosticDirective.UnmarshalJSONFrom — hostimpl.go:168.
gostd::Error MappedDiagnosticDirective::unmarshalJSONFrom(
    const json::Value& data) {
	auto [d, perr] = json::parse(data);
	if (perr) {
		return perr;
	}
	if (d.kind != json::Dom::K::Array) {
		return gostd::newError("json: cannot unmarshal non-array into "
		                       "MappedDiagnosticDirective");
	}
	const auto& tuple = d.arr;
	if (tuple.size() != 5 && tuple.size() != 6) {
		return gostd::errorf(
		    "diagnostic directive tuple must contain 5 or 6 elements, got %d",
		    {(int)tuple.size()});
	}
	*this = MappedDiagnosticDirective{};
	int64_t* fields[] = {&OriginalStart, &OriginalLength, &VirtualStart,
	                     &VirtualEnd};
	for (size_t i = 0; i < 4; i++) {
		auto [n, e] = json::asInt(tuple[i], "int");
		if (e) {
			return gostd::errorf(
			    "invalid diagnostic directive tuple element %d: %w",
			    {(int)i, e});
		}
		*fields[i] = n;
	}
	{
		auto [n, e] = json::asInt(tuple[4], "int");
		if (e) {
			return gostd::errorf(
			    "invalid diagnostic directive tuple element %d: %w", {4, e});
		}
		Policy = static_cast<DiagnosticDirectivePolicy>(n);
	}
	if (tuple.size() == 6) {
		auto [n, e] = json::asInt(tuple[5], "int");
		if (e) {
			return gostd::errorf(
			    "invalid diagnostic directive tuple element %d: %w", {5, e});
		}
		UnusedExpectDirectiveIndex = n;
	}
	return nullptr;
}

namespace {

// dialFunc establishes a running connection to a mapper. In production it
// spawns the mapper's process; tests substitute an in-memory connection. It
// returns the connection and a closer that tears it down —
// hostimpl.go:216-218. (params for Call are pre-marshaled by the caller; see
// the ipc.Conn declaration.)
struct dialResult {
	std::shared_ptr<ipc::Conn> conn;
	std::shared_ptr<gostd::io::ReadWriteCloser> closer;
	PositionEncoding positionEncoding;
	std::string diagnosticSource;
	gostd::Error err;
};
using dialFunc =
    std::function<dialResult(gostd::Context ctx, Mapper* mapper,
                             locale::Locale diagnosticLocale)>;

struct timingCollector;
struct mapperTimingCollector;
struct mapperConn;
struct projectEntry;
struct projectLease;

// host manages one child process per mapper identity. It is the production
// implementation of Host — hostimpl.go:221.
std::pair<Result, gostd::Error> decodeTransformResult(
    const json::Value& raw, const std::string& originalText,
    const PositionEncoding& positionEncoding,
    const std::string& diagnosticSource);

struct host : Host, std::enable_shared_from_this<host> {
	gostd::Context ctx;
	gostd::CancelFunc cancel;
	std::function<bool()> stop;
	dialFunc dial;
	std::unique_ptr<timingCollector> timing;

	std::shared_mutex lifecycleMu;
	locale::Locale diagnosticLocale;

	std::mutex mu;
	// Go nil-map semantics: nullopt while closed.
	std::optional<std::unordered_map<std::string, std::shared_ptr<mapperConn>>>
	    conns;
	std::optional<std::unordered_map<std::string, std::shared_ptr<projectEntry>>>
	    projects;
	std::optional<std::unordered_map<std::string, std::shared_ptr<projectLease>>>
	    projectLeases;
	uint64_t nextProjectID = 0;

	tsc::contentmapper::Timings Timings() override;
	void SetLocale(locale::Locale diagnosticLocale) override;
	std::shared_ptr<tsc::contentmapper::Project> Project(
	    const ProjectSpec& spec) override;
	std::function<void()> Acquire(
	    const std::vector<Mapper*>& mappers) override;
	std::pair<Result, gostd::Error> Transform(
	    Mapper* mapper, const Request& request) override;
	std::pair<Result, gostd::Error> transformLocked(
	    Mapper* mapper, const Request& request,
	    const std::string& projectHandle);
	gostd::Error Close() override;
	dialResult connFor(Mapper* mapper);
	dialResult connForLocked(Mapper* mapper);
	gostd::Error openProjectLocked(gostd::Context ctx, projectEntry* entry);
	gostd::Error closeProject(Mapper* mapper,
	                          const std::shared_ptr<ipc::Conn>& conn,
	                          const std::string& projectHandle);
	void release(const std::vector<std::string>& identities);
};

// projectEntry — hostimpl.go:238.
struct projectEntry {
	Mapper* mapper = nullptr;
	ProjectSpec spec;
	std::string projectHandle;
	bool opened = false;
	std::string configIdentity;
	std::vector<std::string> watchedFiles;
	std::vector<OptionDiagnostic> optionDiagnostics;
};

// mapperConn — hostimpl.go:248.
struct mapperConn {
	std::shared_ptr<ipc::Conn> conn;
	std::shared_ptr<gostd::io::ReadWriteCloser> closer;
	// err, when non-null, records that this mapper failed to start; it is
	// cached so we do not repeatedly try (and fail) to spawn a broken mapper.
	gostd::Error err;
	PositionEncoding positionEncoding;
	std::string diagnosticSource;
	// refs is the number of active Acquire calls retaining this identity.
	int refs = 0;
};

// operationTiming — hostimpl.go:260.
struct operationTiming {
	std::atomic<uint64_t> count{0};
	std::atomic<int64_t> duration{0};

	void record(gostd::Time start) {
		count.fetch_add(1);
		duration.fetch_add(gostd::since(start).count());
	}
	OperationTiming snapshot() const {
		return OperationTiming{count.load(),
		                       gostd::Duration{duration.load()}};
	}
};

// timingCollector — hostimpl.go:274.
struct timingCollector {
	std::mutex mu;
	std::unordered_map<std::string, std::shared_ptr<mapperTimingCollector>>
	    mappers;
	uint64_t activeRequests = 0;
	gostd::Time requestWaitStart;
	gostd::Duration requestWaitElapsed{0};

	std::shared_ptr<mapperTimingCollector> mapper(const std::string& identity);
	Timings snapshot();
};

// mapperTimingCollector — hostimpl.go:282.
struct mapperTimingCollector {
	operationTiming spawn;
	operationTiming initialize;
	operationTiming openProject;
	operationTiming closeProject;
	operationTiming transform;
	timingCollector* owner;

	gostd::Time startRequest();
	void finishRequest(operationTiming* operation, gostd::Time start);
};

// timingCollector.mapper — hostimpl.go:291.
std::shared_ptr<mapperTimingCollector> timingCollector::mapper(
    const std::string& identity) {
	std::lock_guard<std::mutex> lk(mu);
	auto it = mappers.find(identity);
	if (it != mappers.end()) {
		return it->second;
	}
	auto timing = std::make_shared<mapperTimingCollector>();
	timing->owner = this;
	mappers[identity] = timing;
	return timing;
}

// timingCollector.snapshot — hostimpl.go:302.
Timings timingCollector::snapshot() {
	std::unordered_map<std::string, std::shared_ptr<mapperTimingCollector>>
	    mappersCopy;
	gostd::Duration requestWait;
	{
		std::lock_guard<std::mutex> lk(mu);
		requestWait = requestWaitElapsed;
		if (activeRequests != 0) {
			requestWait += gostd::since(requestWaitStart);
		}
		mappersCopy = mappers;
	}
	Timings result;
	result.Mappers.reserve(mappersCopy.size());
	result.RequestWait = requestWait;
	for (const auto& [identity, timing] : mappersCopy) {
		result.Mappers[identity] = MapperTimings{
		    timing->spawn.snapshot(),
		    timing->initialize.snapshot(),
		    timing->openProject.snapshot(),
		    timing->closeProject.snapshot(),
		    timing->transform.snapshot(),
		};
	}
	return result;
}

// mapperTimingCollector.startRequest — hostimpl.go:324.
gostd::Time mapperTimingCollector::startRequest() {
	owner->mu.lock();
	if (owner->activeRequests == 0) {
		owner->requestWaitStart = gostd::now();
	}
	owner->activeRequests++;
	owner->mu.unlock();
	return gostd::now();
}

// mapperTimingCollector.finishRequest — hostimpl.go:334.
void mapperTimingCollector::finishRequest(operationTiming* operation,
                                          gostd::Time start) {
	operation->record(start);
	owner->mu.lock();
	owner->activeRequests--;
	if (owner->activeRequests == 0) {
		owner->requestWaitElapsed += gostd::since(owner->requestWaitStart);
	}
	owner->mu.unlock();
}

// loggingProtocol — hostimpl.go:372.
struct loggingProtocol : ipc::Protocol {
	std::shared_ptr<ipc::Protocol> base;
	std::string mapperName;
	Logger logger;

	void log(std::string_view direction, const json::Value& message) {
		// The message is pre-marshaled by the caller; marshalRaw validates it.
		auto [data, err] = json::marshalRaw(message);
		if (err != nullptr) {
			logger(gostd::sprintf(
			    "[content mapper: %s] %s: <failed to serialize: %v>",
			    {mapperName, direction, err}));
			return;
		}
		logger(gostd::sprintf("[content mapper: %s] %s: %s",
		                      {mapperName, direction, data}));
	}

	std::pair<ipc::Message*, gostd::Error> ReadMessage() override {
		auto [message, err] = base->ReadMessage();
		if (err == nullptr) {
			log("receive", jsonrpc::marshalMessage(*message));
		}
		return {message, err};
	}
	gostd::Error WriteRequest(const jsonrpc::ID* id, std::string_view method,
	                          const json::Value& params) override {
		jsonrpc::RequestMessage message;
		message.Id = id;
		message.Method = std::string(method);
		message.Params = params;
		log("send", jsonrpc::marshalRequestMessage(message));
		return base->WriteRequest(id, method, params);
	}
	gostd::Error WriteNotification(std::string_view method,
	                               const json::Value& params) override {
		jsonrpc::RequestMessage message;
		message.Method = std::string(method);
		message.Params = params;
		log("send", jsonrpc::marshalRequestMessage(message));
		return base->WriteNotification(method, params);
	}
	gostd::Error WriteResponse(const jsonrpc::ID* id,
	                           const json::Value& result) override {
		jsonrpc::ResponseMessage message;
		message.Id = id;
		message.Result = result;
		log("send", jsonrpc::marshalResponseMessage(message));
		return base->WriteResponse(id, result);
	}
	gostd::Error WriteError(const jsonrpc::ID* id,
	                        const jsonrpc::ResponseError* err) override {
		jsonrpc::ResponseMessage message;
		message.Id = id;
		message.Error = err;
		log("send", jsonrpc::marshalResponseMessage(message));
		return base->WriteError(id, err);
	}
};

// stderrLogger — hostimpl.go:419.
struct stderrLogger : gostd::io::Writer {
	std::string mapperName;
	Logger logger;
	std::mutex mu;
	std::string pending;

	std::pair<int, gostd::Error> write(std::string_view data) override {
		std::lock_guard<std::mutex> lk(mu);
		pending += data;
		for (;;) {
			size_t index = pending.find('\n');
			if (index == std::string::npos) {
				break;
			}
			std::string line = pending.substr(0, index);
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			log(line);
			pending.erase(0, index + 1);
		}
		return {(int)data.size(), nullptr};
	}
	void flush() {
		std::lock_guard<std::mutex> lk(mu);
		if (!pending.empty()) {
			std::string line = pending;
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			log(std::move(line));
			pending.clear();
		}
	}
	void log(std::string_view message) {
		logger(gostd::sprintf("[content mapper: %s] stderr: %s",
		                      {mapperName, message}));
	}
};

// loggedProcess — hostimpl.go:454.
struct loggedProcess : gostd::io::ReadWriteCloser {
	std::shared_ptr<gostd::io::ReadWriteCloser> base;
	std::shared_ptr<stderrLogger> stderrLog;

	std::pair<int, gostd::Error> read(std::span<char> buf) override {
		return base->read(buf);
	}
	std::pair<int, gostd::Error> write(std::string_view data) override {
		return base->write(data);
	}
	gostd::Error close() override {
		auto err = base->close();
		stderrLog->flush();
		return err;
	}
};

// closeOnceReadWriteCloser — hostimpl.go:465.
struct closeOnceReadWriteCloser : gostd::io::ReadWriteCloser,
                                  processExitState {
	std::shared_ptr<gostd::io::ReadWriteCloser> base;
	std::once_flag once;
	gostd::Error err;

	gostd::Error close() override {
		std::call_once(once, [&] { err = base->close(); });
		return err;
	}
	std::pair<int, gostd::Error> read(std::span<char> buf) override {
		return base->read(buf);
	}
	std::pair<int, gostd::Error> write(std::string_view data) override {
		return base->write(data);
	}
	std::pair<int, bool> ExitCode() override {
		if (auto* state =
		        dynamic_cast<processExitState*>(base.get())) {
			return state->ExitCode();
		}
		return {0, false};
	}
};

// rejectHandler rejects any request initiated by the mapper. The content
// mapper protocol is currently parent-driven only; a request from the child
// is a protocol violation — hostimpl.go:1362.
struct rejectHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value&) override {
		return {json::Value{},
		        gostd::errorf("content mapper sent an unexpected request: %s",
		                      {method})};
	}
	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

std::tuple<PositionEncoding, std::string, gostd::Error> handshake(
    gostd::Context ctx, const std::shared_ptr<ipc::Conn>& conn,
    locale::Locale diagnosticLocale);

// newWithDial — hostimpl.go:555.
std::shared_ptr<host> newWithDial(gostd::Context ctx,
                                 locale::Locale diagnosticLocale,
                                 std::unique_ptr<timingCollector> timing,
                                 dialFunc dial) {
	auto [hostCtx, cancel] = gostd::contextWithCancel(ctx);
	auto h = std::make_shared<host>();
	h->ctx = hostCtx;
	h->cancel = cancel;
	h->dial = std::move(dial);
	h->timing = std::move(timing);
	h->diagnosticLocale = diagnosticLocale;
	h->conns = decltype(h->conns)::value_type{};
	h->projects = decltype(h->projects)::value_type{};
	h->projectLeases = decltype(h->projectLeases)::value_type{};
	std::weak_ptr<host> weak = h;
	h->stop = gostd::contextAfterFunc(ctx, [weak] {
		if (auto s = weak.lock()) {
			(void)s->Close();
		}
	});
	return h;
}

// projectLease — hostimpl.go:853. projectLease IS the first Project handle;
// retainedProject wraps subsequent retains.
struct projectLease : Project,
                      std::enable_shared_from_this<projectLease> {
	std::shared_ptr<host> hostPtr;
	std::string key;
	std::vector<Mapper*> mappers;
	std::unordered_map<Mapper*, std::string> entries;
	int refs = 0;
	std::once_flag once;

	std::shared_ptr<Project> retainLocked();
	gostd::Error release();

	gostd::Error Refresh() override;
	std::pair<std::vector<std::string>, gostd::Error> Identities() override;
	std::pair<std::string, gostd::Error> Identity(Mapper* mapper) override;
	std::pair<std::vector<std::string>, gostd::Error> WatchedFiles() override;
	std::vector<OptionDiagnostic> Diagnostics() override;
	std::pair<Result, gostd::Error> Transform(
	    Mapper* mapper, const Request& request) override;
	gostd::Error Close() override;
};

// retainedProject — hostimpl.go:862.
struct retainedProject : Project {
	std::shared_ptr<projectLease> lease;
	std::once_flag once;
	gostd::Error closeErr;

	gostd::Error Close() override {
		std::call_once(once, [&] { closeErr = lease->release(); });
		return closeErr;
	}
	gostd::Error Refresh() override { return lease->Refresh(); }
	std::pair<std::vector<std::string>, gostd::Error> Identities() override {
		return lease->Identities();
	}
	std::pair<std::string, gostd::Error> Identity(Mapper* mapper) override {
		return lease->Identity(mapper);
	}
	std::pair<std::vector<std::string>, gostd::Error> WatchedFiles() override {
		return lease->WatchedFiles();
	}
	std::vector<OptionDiagnostic> Diagnostics() override {
		return lease->Diagnostics();
	}
	std::pair<Result, gostd::Error> Transform(
	    Mapper* mapper, const Request& request) override {
		return lease->Transform(mapper, request);
	}
};

} // namespace

// NewHost — hostimpl.go:487.
std::shared_ptr<Host> NewHost(gostd::Context ctx, Spawner* spawner,
                              locale::Locale diagnosticLocale) {
	return NewHostWithOptions(ctx, spawner, diagnosticLocale, HostOptions{});
}

// NewHostWithOptions — hostimpl.go:492.
std::shared_ptr<Host> NewHostWithOptions(gostd::Context ctx, Spawner* spawner,
                                         locale::Locale diagnosticLocale,
                                         HostOptions options) {
	Logger logger = options.Logger;
	auto timing = std::make_unique<timingCollector>();
	timingCollector* timingPtr = timing.get();
	auto dial = [timingPtr, spawner, logger](
	                gostd::Context ctx, Mapper* mapper,
	                locale::Locale diagnosticLocale) -> dialResult {
		if (mapper->Manifest.Exec.empty()) {
			return {.err = gostd::errorf(
			            "content mapper %q declares no command to run",
			            {mapper->Definition.Package})};
		}
		auto mapperTiming = timingPtr->mapper(mapper->Identity());
		std::string diagnosticName = mapper->DiagnosticName();
		gostd::Time spawnStart = gostd::now();
		gostd::io::Writer* stderr = gostd::io::discard();
		std::shared_ptr<stderrLogger> stderrLog;
		if (logger != nullptr) {
			stderrLog = std::make_shared<stderrLogger>();
			stderrLog->mapperName = diagnosticName;
			stderrLog->logger = logger;
			stderr = stderrLog.get();
		}
		auto [rwc, spawnErr] =
		    spawner->Spawn(mapper->Manifest.Exec, mapper->Definition.Options.empty()
		                       ? mapper->PackageDirectory
		                       : mapper->PackageDirectory,
		                   stderr);
		mapperTiming->spawn.record(spawnStart);
		if (spawnErr != nullptr) {
			auto* ie = new InitializeError;
			ie->Kind = InitializeErrorKindProcessStart;
			ie->MapperName = diagnosticName;
			ie->Command = mapper->Manifest.Exec[0];
			ie->Detail = spawnErr->Error();
			return {.err = gostd::Error(ie)};
		}
		std::shared_ptr<gostd::io::ReadWriteCloser> rwc2 = rwc;
		if (stderrLog != nullptr) {
			auto lp = std::make_shared<loggedProcess>();
			lp->base = rwc2;
			lp->stderrLog = stderrLog;
			rwc2 = lp;
		}
		auto closeOnce = std::make_shared<closeOnceReadWriteCloser>();
		closeOnce->base = rwc2;
		rwc2 = closeOnce;
		std::shared_ptr<ipc::Protocol> protocol =
		    ipc::NewJSONRPCProtocol(rwc2);
		if (logger != nullptr) {
			auto lp = std::make_shared<loggingProtocol>();
			lp->base = protocol;
			lp->mapperName = diagnosticName;
			lp->logger = logger;
			protocol = lp;
		}
		auto handler = std::make_shared<rejectHandler>();
		std::shared_ptr<ipc::Conn> conn =
		    ipc::NewAsyncConnWithProtocol(rwc2, protocol, handler);
		std::weak_ptr<ipc::Conn> weakConn = conn;
		std::thread([weakConn, rwc2, ctx] {
			if (auto c = weakConn.lock()) {
				(void)c->Run(ctx);
			}
			(void)rwc2->close();
		}).detach();
		auto [initializeCtx, cancel] =
		    gostd::contextWithTimeout(ctx, initializeTimeout);
		gostd::Time initializeStart = mapperTiming->startRequest();
		auto [positionEncoding, diagnosticSource, handshakeErr] =
		    handshake(initializeCtx, conn, diagnosticLocale);
		mapperTiming->finishRequest(&mapperTiming->initialize,
		                            initializeStart);
		gostd::Error initializeCtxErr = gostd::ctxErr(initializeCtx);
		cancel();
		if (handshakeErr != nullptr) {
			int exitCode = 0;
			bool exited = false;
			if (auto* exitState =
			        dynamic_cast<processExitState*>(rwc2.get())) {
				std::tie(exitCode, exited) = exitState->ExitCode();
			}
			(void)rwc2->close();
			if (auto* initializeError =
			        gostd::errorAs<InitializeError*>(handshakeErr)) {
				initializeError->MapperName = diagnosticName;
				return {.err = handshakeErr};
			}
			if (exited) {
				auto* ie = new InitializeError;
				ie->Kind = InitializeErrorKindProcessExit;
				ie->MapperName = diagnosticName;
				ie->ExitCode = exitCode;
				return {.err = gostd::Error(ie)};
			}
			if (initializeCtxErr != nullptr ||
			    gostd::errorIs(handshakeErr, gostd::errCanceled) ||
			    gostd::errorIs(handshakeErr, gostd::errDeadlineExceeded)) {
				auto* ie = new InitializeError;
				ie->Kind = InitializeErrorKindNoResponse;
				ie->MapperName = diagnosticName;
				ie->TimeoutSeconds = initializeTimeoutSeconds;
				return {.err = gostd::Error(ie)};
			}
			auto* ie = new InitializeError;
			ie->Kind = InitializeErrorKindRequest;
			ie->MapperName = diagnosticName;
			ie->Detail = handshakeErr->Error();
			return {.err = gostd::Error(ie)};
		}
		return {.conn = conn,
		        .closer = rwc2,
		        .positionEncoding = positionEncoding,
		        .diagnosticSource = diagnosticSource};
	};
	return newWithDial(ctx, diagnosticLocale, std::move(timing),
	                   std::move(dial));
}

// host.SetLocale — hostimpl.go:566.
void host::SetLocale(locale::Locale newLocale) {
	std::unique_lock<std::shared_mutex> lk(lifecycleMu);
	if (diagnosticLocale.String() == newLocale.String()) {
		return;
	}
	diagnosticLocale = newLocale;

	std::vector<std::shared_ptr<gostd::io::ReadWriteCloser>> closers;
	{
		std::lock_guard<std::mutex> lk2(mu);
		for (auto& [k, entry] : *conns) {
			if (entry->closer != nullptr) {
				closers.push_back(entry->closer);
			}
			entry->conn = nullptr;
			entry->closer = nullptr;
			entry->err = nullptr;
			entry->positionEncoding.clear();
			entry->diagnosticSource.clear();
		}
		for (auto& [k, project] : *projects) {
			project->opened = false;
		}
	}
	for (auto& closer : closers) {
		(void)closer->close();
	}
}

// host.Timings — hostimpl.go:562. (Defined here where timingCollector is
// complete.)
tsc::contentmapper::Timings host::Timings() { return timing->snapshot(); }

namespace {

// projectSpecKey — hostimpl.go:627.
std::string projectSpecKey(const ProjectSpec& spec) {
	std::string key;
	key += spec.ConfigFileName;
	key += '\0';
	key += gostd::sprintf("%p", {gostd::fmtArg::ptr(spec.CompilerOptions).text});
	for (auto* mapper : spec.Mappers) {
		key += '\0';
		key += gostd::sprintf("%p", {gostd::fmtArg::ptr(mapper).text});
	}
	return key;
}

// combinedIdentity — hostimpl.go:636.
std::string combinedIdentity(Mapper* mapper, const std::string& configIdentity,
                             const CompilerOptions* compilerOptions) {
	auto transformIdentity = mapper->TransformIdentity(compilerOptions).Bytes();
	std::string buf;
	const std::string identity = mapper->Identity();
	buf.reserve(identity.size() + mapper->Definition.Options.size() +
	            configIdentity.size() + transformIdentity.size() + 3);
	buf += identity;
	buf += '\0';
	buf += mapper->Definition.Options;
	buf += '\0';
	buf += configIdentity;
	buf += '\0';
	buf.append(reinterpret_cast<const char*>(transformIdentity.data()),
	           transformIdentity.size());
	auto hash = xxh3::hash128(buf).Bytes();
	return identity + ":" +
	       hexEncodeLower(hash.data(), hash.size());
}

} // namespace

// host.Project — hostimpl.go:595.
std::shared_ptr<Project> host::Project(const ProjectSpec& spec) {
	std::shared_lock<std::shared_mutex> lk(lifecycleMu);

	std::string key = projectSpecKey(spec);
	std::lock_guard<std::mutex> lk2(mu);
	if (!projects.has_value()) {
		return nullptr;
	}
	if (auto it = projectLeases->find(key); it != projectLeases->end()) {
		return it->second->retainLocked();
	}
	auto lease = std::make_shared<projectLease>();
	lease->hostPtr = shared_from_this();
	lease->key = key;
	lease->mappers = spec.Mappers;
	lease->refs = 1;
	for (auto* mapper : spec.Mappers) {
		std::string entryKey = gostd::sprintf(
		    "%s:%d", {mapper->Identity(), nextProjectID});
		nextProjectID++;
		auto entry = std::make_shared<projectEntry>();
		entry->mapper = mapper;
		entry->spec = spec;
		entry->projectHandle = entryKey;
		(*projects)[entryKey] = entry;
		auto& connEntry = (*conns)[mapper->Identity()];
		if (connEntry == nullptr) {
			connEntry = std::make_shared<mapperConn>();
		}
		connEntry->refs++;
		lease->entries[mapper] = entryKey;
	}
	(*projectLeases)[key] = lease;
	return lease;
}

// host.openProjectLocked — hostimpl.go:650.
gostd::Error host::openProjectLocked(gostd::Context ctx, projectEntry* entry) {
	if (entry->opened) {
		return nullptr;
	}
	auto dres = connForLocked(entry->mapper);
	auto& conn = dres.conn;
	const std::string& diagnosticSource = dres.diagnosticSource;
	if (dres.err != nullptr) {
		return dres.err;
	}
	auto [compilerOptions, merr] = json::marshalRaw(
	    detail::marshalCompilerOptions(entry->spec.CompilerOptions));
	if (merr != nullptr) {
		return merr;
	}
	auto mapperTiming = timing->mapper(entry->mapper->Identity());
	gostd::Time start = mapperTiming->startRequest();
	OpenProjectParams params;
	params.ConfigFileName = entry->spec.ConfigFileName;
	params.ProjectHandle = entry->projectHandle;
	params.Options = entry->mapper->Definition.Options;
	params.CompilerOptions = compilerOptions;
	auto [raw, cerr] =
	    conn->Call(ctx, MethodOpenProject, marshalOpenProjectParams(params));
	mapperTiming->finishRequest(&mapperTiming->openProject, start);
	if (cerr != nullptr) {
		return cerr;
	}
	auto [result, uerr] = unmarshalOpenProjectResult(raw);
	if (uerr != nullptr) {
		auto* e = new ProjectError;
		e->Kind = ProjectErrorKindMalformedResponse;
		return gostd::Error(e);
	}
	if (entry->mapper->Manifest.DynamicConfig && result.ConfigIdentity.empty()) {
		auto* e = new ProjectError;
		e->Kind = ProjectErrorKindMissingConfigIdentity;
		return gostd::Error(e);
	}
	if (!entry->mapper->Manifest.DynamicConfig &&
	    !result.ConfigIdentity.empty()) {
		auto* e = new ProjectError;
		e->Kind = ProjectErrorKindUnexpectedConfigIdentity;
		return gostd::Error(e);
	}
	if (!entry->mapper->Manifest.DynamicConfig &&
	    !result.WatchedFiles.empty()) {
		auto* e = new ProjectError;
		e->Kind = ProjectErrorKindUnexpectedWatchedFiles;
		return gostd::Error(e);
	}
	entry->configIdentity = result.ConfigIdentity;
	for (const auto& fileName : result.WatchedFiles) {
		if (!tspath::pathIsAbsolute(fileName)) {
			auto* e = new ProjectError;
			e->Kind = ProjectErrorKindNonAbsoluteWatchedFile;
			return gostd::Error(e);
		}
	}
	entry->watchedFiles = result.WatchedFiles;
	entry->optionDiagnostics.clear();
	entry->optionDiagnostics.reserve(result.OptionDiagnostics.size());
	for (const auto& diagnostic : result.OptionDiagnostics) {
		std::vector<OptionPathSegment> path(diagnostic.Path.size());
		for (size_t j = 0; j < diagnostic.Path.size(); j++) {
			const json::Value& rawSegment = diagnostic.Path[j];
			auto [sd, perr] = json::parse(rawSegment);
			if (perr) {
				auto* e = new ProjectError;
				e->Kind = ProjectErrorKindMalformedResponse;
				return gostd::Error(e);
			}
			switch (sd.kind) {
			case json::Dom::K::String: { // Kind '"'
				auto [s, e] = json::asString(sd, "string");
				if (e) {
					auto* pe = new ProjectError;
					pe->Kind = ProjectErrorKindMalformedResponse;
					return gostd::Error(pe);
				}
				path[j].Property = std::move(s);
				break;
			}
			case json::Dom::K::Number: { // Kind '0'
				auto [n, e] = json::asInt(sd, "int");
				if (e || n < 0) {
					auto* pe = new ProjectError;
					pe->Kind = ProjectErrorKindMalformedResponse;
					return gostd::Error(pe);
				}
				path[j].Index = n;
				path[j].IsIndex = true;
				break;
			}
			default: {
				auto* e = new ProjectError;
				e->Kind = ProjectErrorKindMalformedResponse;
				return gostd::Error(e);
			}
			}
		}
		OptionDiagnostic od;
		od.Mapper = entry->mapper;
		od.Path = std::move(path);
		od.Source = diagnosticSource;
		od.Code = diagnostic.Code;
		od.MessageText = diagnostic.MessageText;
		entry->optionDiagnostics.push_back(std::move(od));
	}
	entry->opened = true;
	return nullptr;
}

// host.closeProject — hostimpl.go:728.
gostd::Error host::closeProject(Mapper* mapper,
                                const std::shared_ptr<ipc::Conn>& conn,
                                const std::string& projectHandle) {
	auto mapperTiming = timing->mapper(mapper->Identity());
	gostd::Time start = mapperTiming->startRequest();
	CloseProjectParams params;
	params.ProjectHandle = projectHandle;
	auto [raw, err] =
	    conn->Call(ctx, MethodCloseProject, marshalCloseProjectParams(params));
	mapperTiming->finishRequest(&mapperTiming->closeProject, start);
	return err;
}

// host.Acquire — hostimpl.go:736.
std::function<void()> host::Acquire(const std::vector<Mapper*>& mappers) {
	std::unordered_set<std::string> seen;
	std::vector<std::string> identities;
	{
		std::lock_guard<std::mutex> lk(mu);
		if (conns.has_value()) {
			for (auto* mapper : mappers) {
				const std::string identity = mapper->Identity();
				if (!seen.insert(identity).second) {
					continue;
				}
				identities.push_back(identity);
				auto& entry = (*conns)[identity];
				if (entry == nullptr) {
					entry = std::make_shared<mapperConn>();
				}
				entry->refs++;
			}
		}
	}
	std::weak_ptr<host> weak = shared_from_this();
	return gostd::onceFunc([weak, identities = std::move(identities)] {
		if (auto h = weak.lock()) {
			h->release(identities);
		}
	});
}

// host.Transform — hostimpl.go:761.
std::pair<Result, gostd::Error> host::Transform(Mapper* mapper,
                                              const Request& request) {
	ProjectSpec spec;
	spec.Mappers = {mapper};
	// Go allocates a fresh options pointer per call so each Transform gets a
	// distinct projectSpecKey; mirror with a per-call allocation.
	spec.CompilerOptions = new CompilerOptions{};
	auto project = Project(spec);
	if (project == nullptr) {
		// Go: project.Close() on a nil interface panics.
		TSC_UNREACHABLE("content mapper project is nil");
	}
	auto [result, err] = project->Transform(mapper, request);
	(void)project->Close(); // defer project.Close()
	return {result, err};
}

// host.transformLocked — hostimpl.go:770.
std::pair<Result, gostd::Error> host::transformLocked(
    Mapper* mapper, const Request& request,
    const std::string& projectHandle) {
	if (projectHandle.empty()) {
		return {Result{}, gostd::newError(
		                  "content mapper project handle is required")};
	}
	auto dres = connFor(mapper);
	if (dres.err != nullptr) {
		return {Result{}, gostd::Error(
		                  NewTransformError(TransformErrorKindInitialize,
		                                    dres.err))};
	}
	auto mapperTiming = timing->mapper(mapper->Identity());
	gostd::Time start = mapperTiming->startRequest();
	TransformParams params;
	params.FileName = request.FileName;
	params.Content = request.Content;
	params.ProjectHandle = projectHandle;
	auto [raw, cerr] =
	    dres.conn->Call(ctx, MethodTransform, marshalTransformParams(params));
	mapperTiming->finishRequest(&mapperTiming->transform, start);
	if (cerr != nullptr) {
		return {Result{}, gostd::Error(NewTransformError(
		                  TransformErrorKindRequest, cerr))};
	}
	auto [decoded, derr] = decodeTransformResult(
	    raw, request.Content, dres.positionEncoding, dres.diagnosticSource);
	if (derr != nullptr) {
		return {Result{}, gostd::Error(NewTransformError(
		                  TransformErrorKindResponse, derr))};
	}
	return {decoded, nullptr};
}

// host.Close — hostimpl.go:798.
gostd::Error host::Close() {
	std::unique_lock<std::shared_mutex> lk(lifecycleMu);
	stop();
	cancel();
	std::vector<std::shared_ptr<gostd::io::ReadWriteCloser>> closers;
	{
		std::lock_guard<std::mutex> lk2(mu);
		if (conns.has_value()) {
			for (auto& [k, mc] : *conns) {
				if (mc->closer != nullptr) {
					closers.push_back(mc->closer);
				}
			}
		}
		conns = std::nullopt;
		projects = std::nullopt;
		projectLeases = std::nullopt;
	}
	std::vector<gostd::Error> errs;
	for (auto& closer : closers) {
		if (auto err = closer->close(); err != nullptr) {
			errs.push_back(err);
		}
	}
	return gostd::joinError(errs);
}

// host.connFor — hostimpl.go:825.
dialResult host::connFor(Mapper* mapper) {
	std::lock_guard<std::mutex> lk(mu);
	return connForLocked(mapper);
}

// host.connForLocked — hostimpl.go:831.
dialResult host::connForLocked(Mapper* mapper) {
	if (!conns.has_value()) {
		return {.err = gostd::newError("content mapper host is closed")};
	}
	const std::string identity = mapper->Identity();
	auto& entry = (*conns)[identity];
	if (entry == nullptr) {
		entry = std::make_shared<mapperConn>();
	}
	if (entry->conn != nullptr || entry->err != nullptr) {
		return {entry->conn, entry->closer, entry->positionEncoding,
		        entry->diagnosticSource, entry->err};
	}
	dialResult res = dial(ctx, mapper, diagnosticLocale);
	entry->conn = res.conn;
	entry->closer = res.closer;
	entry->err = res.err;
	entry->positionEncoding = res.positionEncoding;
	entry->diagnosticSource = res.diagnosticSource;
	return res;
}

// projectLease.retainLocked — hostimpl.go:867.
std::shared_ptr<Project> projectLease::retainLocked() {
	refs++;
	auto retained = std::make_shared<retainedProject>();
	retained->lease = std::static_pointer_cast<projectLease>(
	    shared_from_this());
	return retained;
}

// projectLease.Refresh — hostimpl.go:877.
gostd::Error projectLease::Refresh() {
	std::shared_lock<std::shared_mutex> lk(hostPtr->lifecycleMu);
	std::lock_guard<std::mutex> lk2(hostPtr->mu);
	if (!hostPtr->projects.has_value()) {
		return nullptr;
	}
	gostd::Error result;
	for (const auto& [key, entryKey] : entries) {
		auto it = hostPtr->projects->find(entryKey);
		if (it == hostPtr->projects->end() || !it->second->opened) {
			continue;
		}
		auto& entry = it->second;
		auto connIt = hostPtr->conns->find(entry->mapper->Identity());
		if (connIt != hostPtr->conns->end() &&
		    connIt->second->conn != nullptr) {
			auto err = hostPtr->closeProject(
			    entry->mapper, connIt->second->conn, entry->projectHandle);
			result = gostd::joinError({result, err});
		}
		entry->opened = false;
	}
	return result;
}

// projectLease.Identities — hostimpl.go:899.
std::pair<std::vector<std::string>, gostd::Error> projectLease::Identities() {
	std::shared_lock<std::shared_mutex> lk(hostPtr->lifecycleMu);
	std::lock_guard<std::mutex> lk2(hostPtr->mu);
	if (!hostPtr->projects.has_value()) {
		return {std::vector<std::string>{}, nullptr};
	}
	std::vector<std::string> identities;
	identities.reserve(entries.size());
	for (auto* mapper : mappers) {
		auto eit = entries.find(mapper);
		if (eit == entries.end()) {
			continue;
		}
		auto pit = hostPtr->projects->find(eit->second);
		if (pit == hostPtr->projects->end()) {
			continue;
		}
		auto& entry = pit->second;
		if (mapper->Manifest.DynamicConfig) {
			if (auto err =
			        hostPtr->openProjectLocked(hostPtr->ctx, entry.get());
			    err != nullptr) {
				return {{}, err};
			}
			identities.push_back(combinedIdentity(
			    mapper, entry->configIdentity, entry->spec.CompilerOptions));
		} else {
			auto hash =
			    mapper->TransformIdentity(entry->spec.CompilerOptions)
			        .Bytes();
			identities.push_back(mapper->Identity() + ":" +
			                     hexEncodeLower(hash.data(), hash.size()));
		}
	}
	return {identities, nullptr};
}

// projectLease.Identity — hostimpl.go:927.
std::pair<std::string, gostd::Error> projectLease::Identity(Mapper* mapper) {
	std::shared_lock<std::shared_mutex> lk(hostPtr->lifecycleMu);
	std::lock_guard<std::mutex> lk2(hostPtr->mu);
	if (!hostPtr->projects.has_value()) {
		return {"", nullptr};
	}
	auto eit = entries.find(mapper);
	if (eit == entries.end()) {
		return {"", nullptr};
	}
	auto pit = hostPtr->projects->find(eit->second);
	if (pit == hostPtr->projects->end()) {
		return {"", nullptr};
	}
	auto& entry = pit->second;
	if (mapper->Manifest.DynamicConfig) {
		if (auto err = hostPtr->openProjectLocked(hostPtr->ctx, entry.get());
		    err != nullptr) {
			return {"", err};
		}
		return {combinedIdentity(mapper, entry->configIdentity,
		                         entry->spec.CompilerOptions),
		        nullptr};
	}
	auto hash =
	    mapper->TransformIdentity(entry->spec.CompilerOptions).Bytes();
	return {mapper->Identity() + ":" +
	            hexEncodeLower(hash.data(), hash.size()),
	        nullptr};
}

// projectLease.WatchedFiles — hostimpl.go:953.
std::pair<std::vector<std::string>, gostd::Error> projectLease::WatchedFiles() {
	std::shared_lock<std::shared_mutex> lk(hostPtr->lifecycleMu);
	std::lock_guard<std::mutex> lk2(hostPtr->mu);
	if (!hostPtr->projects.has_value()) {
		return {std::vector<std::string>{}, nullptr};
	}
	std::vector<std::string> files;
	for (const auto& [mapper, entryKey] : entries) {
		auto it = hostPtr->projects->find(entryKey);
		if (it == hostPtr->projects->end()) {
			continue;
		}
		auto& entry = it->second;
		if (entry->mapper->Manifest.DynamicConfig) {
			if (auto err =
			        hostPtr->openProjectLocked(hostPtr->ctx, entry.get());
			    err != nullptr) {
				return {{}, err};
			}
		}
		files.insert(files.end(), entry->watchedFiles.begin(),
		             entry->watchedFiles.end());
	}
	std::sort(files.begin(), files.end());
	files.erase(std::unique(files.begin(), files.end()), files.end());
	return {files, nullptr};
}

// projectLease.Diagnostics — hostimpl.go:978.
std::vector<OptionDiagnostic> projectLease::Diagnostics() {
	std::shared_lock<std::shared_mutex> lk(hostPtr->lifecycleMu);
	std::lock_guard<std::mutex> lk2(hostPtr->mu);
	if (!hostPtr->projects.has_value()) {
		return {};
	}
	std::vector<OptionDiagnostic> diagnostics;
	for (auto* mapper : mappers) {
		auto eit = entries.find(mapper);
		std::string key =
		    eit == entries.end() ? std::string() : eit->second;
		auto pit = hostPtr->projects->find(key);
		if (pit == hostPtr->projects->end()) {
			continue;
		}
		auto& entry = pit->second;
		if (!entry->opened) {
			continue;
		}
		diagnostics.insert(diagnostics.end(),
		                   entry->optionDiagnostics.begin(),
		                   entry->optionDiagnostics.end());
	}
	return diagnostics;
}

// projectLease.Transform — hostimpl.go:1000.
std::pair<Result, gostd::Error> projectLease::Transform(
    Mapper* mapper, const Request& request) {
	hostPtr->lifecycleMu.lock_shared();
	struct unlockGuard {
		std::shared_mutex* m;
		~unlockGuard() { m->unlock_shared(); }
	} guard{&hostPtr->lifecycleMu};
	std::string handle;
	{
		std::unique_lock<std::mutex> lk2(hostPtr->mu);
		// Go reads nil maps as empty.
		std::shared_ptr<projectEntry> entry;
		if (hostPtr->projects.has_value()) {
			auto eit = entries.find(mapper);
			if (eit != entries.end()) {
				auto pit = hostPtr->projects->find(eit->second);
				if (pit != hostPtr->projects->end()) {
					entry = pit->second;
				}
			}
		}
		if (entry == nullptr) {
			return {Result{},
			        gostd::newError("content mapper project is closed")};
		}
		if (auto err = hostPtr->openProjectLocked(hostPtr->ctx, entry.get());
		    err != nullptr) {
			if (gostd::errorAs<InitializeError*>(err) != nullptr) {
				return {Result{},
				        gostd::Error(NewTransformError(
				            TransformErrorKindInitialize, err))};
			}
			return {Result{},
			        gostd::Error(NewTransformError(
			            TransformErrorKindProject, err))};
		}
		handle = entry->projectHandle;
	}
	return hostPtr->transformLocked(mapper, request, handle);
}

// projectLease.Close — hostimpl.go:1021.
gostd::Error projectLease::Close() {
	gostd::Error result;
	std::call_once(once, [&] { result = release(); });
	return result;
}

// projectLease.release — hostimpl.go:1029.
gostd::Error projectLease::release() {
	gostd::Error result;
	{
		std::shared_lock<std::shared_mutex> lk(hostPtr->lifecycleMu);
		std::vector<std::string> releasedIdentities;
		{
			std::lock_guard<std::mutex> lk2(hostPtr->mu);
			refs--;
			if (refs < 0) {
				TSC_UNREACHABLE(
				    "content mapper project reference count below zero");
			}
			if (refs != 0) {
				return nullptr;
			}
			// Go reads/deletes nil maps as no-ops; optional nullopt == nil.
			if (hostPtr->projectLeases.has_value()) {
				auto leaseIt = hostPtr->projectLeases->find(key);
				if (leaseIt != hostPtr->projectLeases->end() &&
				    leaseIt->second.get() == this) {
					hostPtr->projectLeases->erase(leaseIt);
				}
			}
			if (hostPtr->projects.has_value()) {
				for (const auto& [mapper, entryKey] : entries) {
					auto it = hostPtr->projects->find(entryKey);
					if (it == hostPtr->projects->end()) {
						continue;
					}
					auto& entry = it->second;
					if (entry->opened &&
					    hostPtr->conns.has_value()) {
						auto connIt = hostPtr->conns->find(
						    entry->mapper->Identity());
						if (connIt != hostPtr->conns->end() &&
						    connIt->second->conn != nullptr) {
							auto err = hostPtr->closeProject(
							    entry->mapper, connIt->second->conn,
							    entry->projectHandle);
							result = gostd::joinError({result, err});
						}
					}
					hostPtr->projects->erase(it);
					releasedIdentities.push_back(
					    entry->mapper->Identity());
				}
			}
		}
		hostPtr->release(releasedIdentities);
	}
	return result;
}

// host.release — hostimpl.go:1067.
void host::release(const std::vector<std::string>& identities) {
	std::vector<std::shared_ptr<gostd::io::ReadWriteCloser>> closers;
	{
		std::lock_guard<std::mutex> lk(mu);
		if (conns.has_value()) {
			for (const auto& identity : identities) {
				auto it = conns->find(identity);
				if (it == conns->end()) {
					continue;
				}
				auto& entry = it->second;
				entry->refs--;
				if (entry->refs == 0) {
					conns->erase(it);
					if (entry->closer != nullptr) {
						closers.push_back(entry->closer);
					}
				}
			}
		}
	}
	for (auto& closer : closers) {
		(void)closer->close();
	}
}

namespace {

// handshake — hostimpl.go:1091.
std::tuple<PositionEncoding, std::string, gostd::Error> handshake(
    gostd::Context ctx, const std::shared_ptr<ipc::Conn>& conn,
    locale::Locale diagnosticLocale) {
	InitializeParams params;
	params.Locale = diagnosticLocale.String();
	params.PositionEncodings = {PositionEncodingUTF8, PositionEncodingUTF16};
	auto [raw, err] =
	    conn->Call(ctx, MethodInitialize, marshalInitializeParams(params));
	if (err != nullptr) {
		return {"", "", err};
	}
	auto [res, uerr] = unmarshalInitializeResult(raw);
	if (uerr != nullptr) {
		auto* ie = new InitializeError;
		ie->Kind = InitializeErrorKindInvalidResponse;
		ie->Detail = uerr->Error();
		return {"", "", gostd::Error(ie)};
	}
	if (res.PositionEncoding != PositionEncodingUTF8 &&
	    res.PositionEncoding != PositionEncodingUTF16) {
		auto* ie = new InitializeError;
		ie->Kind = InitializeErrorKindPositionEncoding;
		ie->PositionEncoding = res.PositionEncoding;
		return {"", "", gostd::Error(ie)};
	}
	auto isSpace = [](unsigned char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
		       c == '\f';
	};
	bool onlySpace = true;
	for (unsigned char c : res.DiagnosticSource) {
		if (!isSpace(c)) {
			onlySpace = false;
			break;
		}
	}
	if (res.DiagnosticSource.empty() || onlySpace) {
		auto* ie = new InitializeError;
		ie->Kind = InitializeErrorKindEmptyDiagnosticSource;
		return {"", "", gostd::Error(ie)};
	}
	if (utf8detail::equalFold(res.DiagnosticSource, "typescript") ||
	    utf8detail::equalFold(res.DiagnosticSource, "tsc")) {
		auto* ie = new InitializeError;
		ie->Kind = InitializeErrorKindReservedDiagnosticSource;
		ie->DiagnosticSource = res.DiagnosticSource;
		return {"", "", gostd::Error(ie)};
	}
	for (const auto& group : tspath::allSupportedExtensionsWithJson) {
		for (const auto& extension : group) {
			if (utf8detail::equalFold(res.DiagnosticSource, extension.substr(1))) {
				auto* ie = new InitializeError;
				ie->Kind = InitializeErrorKindReservedDiagnosticSource;
				ie->DiagnosticSource = res.DiagnosticSource;
				return {"", "", gostd::Error(ie)};
			}
		}
	}
	return {res.PositionEncoding, res.DiagnosticSource, nullptr};
}

// positionNormalizer — hostimpl.go:1316.
struct positionNormalizer {
	std::string text;
	PositionEncoding encoding;
	PositionMap* positionMap = nullptr;
	int64_t length = 0;

	std::pair<TextPos, gostd::Error> normalizeTextPos(TextPos position);
	std::pair<int64_t, gostd::Error> normalize(int64_t position);
};

// positionNormalizer.normalizeTextPos — hostimpl.go:1337.
std::pair<TextPos, gostd::Error> positionNormalizer::normalizeTextPos(
    TextPos position) {
	auto [normalized, err] = normalize(position);
	return {TextPos(normalized), err};
}

// positionNormalizer.normalize — hostimpl.go:1342.
std::pair<int64_t, gostd::Error> positionNormalizer::normalize(
    int64_t position) {
	if (position < 0) {
		return {0,
		        gostd::errorf("position %d is negative", {position})};
	}
	if (position > length) {
		return {0,
		        gostd::errorf("position %d exceeds %s length %d",
		                      {position, encoding, length})};
	}
	int64_t bytePosition = 0;
	if (encoding == PositionEncodingUTF8) {
		bytePosition = position;
	} else { // PositionEncodingUTF16
		bytePosition = positionMap->UTF16ToUTF8((int)position);
	}
	if (bytePosition < (int64_t)text.size() &&
	    (static_cast<unsigned char>(text[bytePosition]) & 0xC0) == 0x80) {
		return {0,
		        gostd::errorf("position %d splits a Unicode code point",
		                      {position})};
	}
	return {bytePosition, nullptr};
}

// newPositionNormalizer — hostimpl.go:1323.
std::pair<positionNormalizer*, gostd::Error> newPositionNormalizer(
    const std::string& text, const PositionEncoding& encoding) {
	auto* normalizer = new positionNormalizer;
	normalizer->text = text;
	normalizer->encoding = encoding;
	if (encoding == PositionEncodingUTF8) {
		normalizer->length = (int64_t)text.size();
	} else if (encoding == PositionEncodingUTF16) {
		normalizer->positionMap = computePositionMap(text);
		normalizer->length =
		    normalizer->positionMap->UTF8ToUTF16((int)text.size());
	} else {
		delete normalizer;
		return {nullptr,
		        gostd::errorf("unsupported position encoding %q", {encoding})};
	}
	return {normalizer, nullptr};
}

// normalizeDiagnosticDirectives — hostimpl.go:1208.
std::pair<std::vector<tsc::MappedDiagnosticDirective>, gostd::Error>
normalizeDiagnosticDirectives(
    const std::optional<DiagnosticDirectives>& diagnosticDirectives,
    positionNormalizer* virtualPositions, positionNormalizer* originalPositions,
    const std::string& diagnosticSource) {
	if (!diagnosticDirectives.has_value()) {
		return {std::vector<tsc::MappedDiagnosticDirective>{}, nullptr};
	}
	const auto& directives = diagnosticDirectives->Directives;
	std::vector<tsc::MappedDiagnosticDirective> result(directives.size());
	for (size_t i = 0; i < directives.size(); i++) {
		const auto& directive = directives[i];
		auto directiveError = [&](DiagnosticDirectiveErrorKind kind) {
			auto* e = new DiagnosticDirectiveError;
			e->Kind = kind;
			e->Index = (int)i;
			e->SupplementalIndex = -1;
			return gostd::Error(e);
		};
		tsc::MappedDiagnosticDirective normalized;
		normalized.Source = diagnosticSource;
		switch (directive.Policy) {
		case DiagnosticDirectivePolicyIgnore:
			normalized.Policy = MappedDiagnosticDirectivePolicy::Ignore;
			break;
		case DiagnosticDirectivePolicyExpect: {
			int64_t unusedDiagnosticIndex = 0;
			if (directive.UnusedExpectDirectiveIndex.has_value()) {
				unusedDiagnosticIndex =
				    *directive.UnusedExpectDirectiveIndex;
			} else if (diagnosticDirectives
			               ->UnusedExpectDirectiveDiagnostics.size() != 1) {
				return {{},
				        directiveError(
				            DiagnosticDirectiveErrorKindExpectMissingUnusedDiagnostic)};
			}
			if (unusedDiagnosticIndex < 0 ||
			    unusedDiagnosticIndex >=
			        (int64_t)diagnosticDirectives
			            ->UnusedExpectDirectiveDiagnostics.size()) {
				return {{},
				        directiveError(
				            DiagnosticDirectiveErrorKindInvalidUnusedDiagnosticIndex)};
			}
			const auto& unusedDiagnostic =
			    diagnosticDirectives->UnusedExpectDirectiveDiagnostics
			        [unusedDiagnosticIndex];
			normalized.Policy = MappedDiagnosticDirectivePolicy::Expect;
			normalized.UnusedCode = unusedDiagnostic.Code;
			normalized.UnusedMessageText = unusedDiagnostic.MessageText;
			break;
		}
		default: {
			auto err = directiveError(
			    DiagnosticDirectiveErrorKindInvalidPolicy);
			static_cast<DiagnosticDirectiveError*>(err.get())->Policy =
			    directive.Policy;
			return {{}, err};
		}
		}
		if (directive.VirtualStart < 0 ||
		    directive.VirtualEnd < directive.VirtualStart) {
			return {{},
			        directiveError(
			            DiagnosticDirectiveErrorKindInvalidRange)};
		}
		auto [virtualStart, serr] =
		    virtualPositions->normalize(directive.VirtualStart);
		if (serr != nullptr) {
			return {{},
			        directiveError(
			            DiagnosticDirectiveErrorKindInvalidRange)};
		}
		auto [virtualEnd, eerr] =
		    virtualPositions->normalize(directive.VirtualEnd);
		if (eerr != nullptr) {
			return {{},
			        directiveError(
			            DiagnosticDirectiveErrorKindInvalidRange)};
		}
		normalized.VirtualRange = {(TextPos)virtualStart,
		                           (TextPos)virtualEnd};
		bool validOriginalRange =
		    directive.OriginalStart >= 0 && directive.OriginalLength >= 0 &&
		    directive.OriginalStart <=
		        std::numeric_limits<int64_t>::max() -
		            directive.OriginalLength;
		if (validOriginalRange) {
			auto [originalStart, startErr] =
			    originalPositions->normalize(directive.OriginalStart);
			auto [originalEnd, endErr] = originalPositions->normalize(
			    directive.OriginalStart + directive.OriginalLength);
			if (startErr == nullptr && endErr == nullptr) {
				normalized.OriginalRange = {(TextPos)originalStart,
				                            (TextPos)originalEnd};
			} else {
				validOriginalRange = false;
			}
		}
		if (normalized.Policy == MappedDiagnosticDirectivePolicy::Expect &&
		    !validOriginalRange) {
			return {{},
			        directiveError(
			            DiagnosticDirectiveErrorKindInvalidRange)};
		}
		result[i] = normalized;
	}
	struct indexedDirective {
		tsc::MappedDiagnosticDirective directive;
		int index;
	};
	std::vector<indexedDirective> sorted(result.size());
	for (size_t i = 0; i < result.size(); i++) {
		sorted[i] = indexedDirective{result[i], (int)i};
	}
	std::sort(sorted.begin(), sorted.end(),
	          [](const indexedDirective& a, const indexedDirective& b) {
		          return a.directive.VirtualRange.pos() <
		                 b.directive.VirtualRange.pos();
	          });
	for (size_t i = 1; i < sorted.size(); i++) {
		if (sorted[i].directive.VirtualRange.pos() <
		    sorted[i - 1].directive.VirtualRange.end()) {
			auto* e = new DiagnosticDirectiveError;
			e->Kind = DiagnosticDirectiveErrorKindOverlap;
			e->Index = sorted[i].index;
			e->SupplementalIndex = -1;
			return {{}, gostd::Error(e)};
		}
	}
	return {result, nullptr};
}

// normalizeMappings — hostimpl.go:1291.
std::pair<spanmap::SpanMap*, gostd::Error> normalizeMappings(
    spanmap::SpanMap* mappings, positionNormalizer* virtualPositions,
    positionNormalizer* originalPositions) {
	auto segments = spanmap::Segments(mappings);
	for (size_t i = 0; i < segments.size(); i++) {
		auto& segment = segments[i];
		auto [vs, err1] =
		    virtualPositions->normalizeTextPos(segment.VirtualStart);
		if (err1 != nullptr) {
			return {nullptr,
			        gostd::errorf(
			            "invalid content mapper mapping %d virtual start: %w",
			            {(int)i, err1})};
		}
		segment.VirtualStart = vs;
		auto [ve, err2] =
		    virtualPositions->normalizeTextPos(segment.VirtualEnd);
		if (err2 != nullptr) {
			return {nullptr,
			        gostd::errorf(
			            "invalid content mapper mapping %d virtual end: %w",
			            {(int)i, err2})};
		}
		segment.VirtualEnd = ve;
		auto [os, err3] =
		    originalPositions->normalizeTextPos(segment.OriginalStart);
		if (err3 != nullptr) {
			return {nullptr,
			        gostd::errorf(
			            "invalid content mapper mapping %d original start: %w",
			            {(int)i, err3})};
		}
		segment.OriginalStart = os;
		auto [oe, err4] =
		    originalPositions->normalizeTextPos(segment.OriginalEnd);
		if (err4 != nullptr) {
			return {nullptr,
			        gostd::errorf(
			            "invalid content mapper mapping %d original end: %w",
			            {(int)i, err4})};
		}
		segment.OriginalEnd = oe;
	}
	return {spanmap::New(std::move(segments)), nullptr};
}

// decodeMappedOutput — hostimpl.go:1170.
struct decodeMappedOutputResult {
	MappedResult result;
	positionNormalizer* originalPositions = nullptr;
	gostd::Error err;
};
decodeMappedOutputResult decodeMappedOutput(
    const MappedOutput& output, const std::string& originalText,
    const PositionEncoding& positionEncoding,
    const std::string& diagnosticSource) {
	if (!IsSupportedVirtualExtension(output.Extension)) {
		auto* e = new InvalidVirtualExtensionError;
		e->Extension = output.Extension;
		return {.err = gostd::Error(e)};
	}
	MappedResult result;
	result.Text = output.Text;
	result.VirtualExtension = output.Extension;
	auto [virtualPositions, verr] =
	    newPositionNormalizer(output.Text, positionEncoding);
	if (verr != nullptr) {
		return {.err = verr};
	}
	auto [originalPositions, oerr] =
	    newPositionNormalizer(originalText, positionEncoding);
	if (oerr != nullptr) {
		return {.err = oerr};
	}
	// A successful transform always carries a span map. Absent or empty
	// mappings describe fully synthesized output (no segment corresponds to
	// the original), so decode to an empty map rather than nil, which would
	// mean "not content-mapped".
	if (!output.Mappings.empty()) {
		auto [mappings, unmarshalErr] = spanmap::Unmarshal(output.Mappings);
		if (unmarshalErr.has_value()) {
			return {.err = gostd::newError(*unmarshalErr)};
		}
		auto [normalized, nerr] = normalizeMappings(mappings, virtualPositions,
		                                          originalPositions);
		if (nerr != nullptr) {
			return {.err = nerr};
		}
		result.Mappings = normalized;
	} else {
		result.Mappings = spanmap::New({});
	}
	auto [directives, derr] = normalizeDiagnosticDirectives(
	    output.DiagnosticDirectives, virtualPositions, originalPositions,
	    diagnosticSource);
	if (derr != nullptr) {
		return {.err = derr};
	}
	result.DiagnosticDirectives = std::move(directives);
	return {.result = std::move(result),
	        .originalPositions = originalPositions};
}

// decodeTransformResult — hostimpl.go:1121.
std::pair<Result, gostd::Error> decodeTransformResult(
    const json::Value& raw, const std::string& originalText,
    const PositionEncoding& positionEncoding,
    const std::string& diagnosticSource) {
	auto [res, uerr] = unmarshalTransformResult(raw);
	if (uerr != nullptr) {
		return {Result{}, uerr};
	}
	auto mres = decodeMappedOutput(res, originalText, positionEncoding,
	                               diagnosticSource);
	if (mres.err != nullptr) {
		return {Result{}, mres.err};
	}
	auto& mapped = mres.result;
	auto* originalPositions = mres.originalPositions;
	Result result;
	result.Text = mapped.Text;
	result.VirtualExtension = mapped.VirtualExtension;
	result.Mappings = mapped.Mappings;
	result.DiagnosticDirectives = mapped.DiagnosticDirectives;
	for (size_t supplementalIndex = 0;
	     supplementalIndex < res.Supplemental.size(); supplementalIndex++) {
		auto sres = decodeMappedOutput(res.Supplemental[supplementalIndex],
		                               originalText, positionEncoding,
		                               diagnosticSource);
		if (sres.err != nullptr) {
			if (auto* directiveError =
			        gostd::errorAs<DiagnosticDirectiveError*>(sres.err)) {
				directiveError->SupplementalIndex =
				    (int)supplementalIndex;
			}
			return {Result{}, sres.err};
		}
		result.Supplemental.push_back(std::move(sres.result));
	}
	for (const auto& d : res.Diagnostics) {
		if (d.Start < 0 || d.Length < 0 ||
		    d.Start > std::numeric_limits<int64_t>::max() - d.Length) {
			return {Result{},
			        gostd::errorf(
			            "invalid content mapper diagnostic range [%d, %d)",
			            {d.Start, d.Start + d.Length})};
		}
		auto [start, serr] = originalPositions->normalize(d.Start);
		if (serr != nullptr) {
			return {Result{},
			        gostd::errorf(
			            "invalid content mapper diagnostic start: %w",
			            {serr})};
		}
		auto [end, eerr] =
		    originalPositions->normalize(d.Start + d.Length);
		if (eerr != nullptr) {
			return {Result{},
			        gostd::errorf(
			            "invalid content mapper diagnostic end: %w", {eerr})};
		}
		result.Diagnostics.push_back(newExternalDiagnostic(
		    nullptr, {(TextPos)start, (TextPos)end}, diagnosticSource,
		    DiagnosticCategory::Error, d.Code, d.MessageText));
	}
	return {result, nullptr};
}

} // namespace

} // namespace tsc::contentmapper

// ---------------------------------------------------------------------------
// Dep implementations — replaced by the owner's slice when it lands.
// === dep stubs — removed when owner slice lands ===
// ---------------------------------------------------------------------------

namespace tsc {

// positionmap.go — the ast slice owns these; defined here until it lands.

int PositionMap::UTF8ToUTF16(int utf8Offset) const {
	if (asciiOnly) {
		return utf8Offset;
	}
	// Binary search: find the last entry where utf8Pos <= utf8Offset.
	size_t lo = 0, hi = entries.size();
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (entries[mid].utf8Pos <= utf8Offset) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	if (lo == 0) {
		// Before any multi-byte character.
		return utf8Offset;
	}
	return utf8Offset - entries[lo - 1].delta;
}

int PositionMap::UTF16ToUTF8(int utf16Offset) const {
	if (asciiOnly) {
		return utf16Offset;
	}
	// The last entry where (utf8Pos - delta) <= utf16Offset.
	size_t lo = 0, hi = entries.size();
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		int utf16Pos = entries[mid].utf8Pos - entries[mid].delta;
		if (utf16Pos <= utf16Offset) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	if (lo == 0) {
		return utf16Offset;
	}
	return utf16Offset + entries[lo - 1].delta;
}

// computePositionMap — positionmap.go:40.
PositionMap* computePositionMap(std::string_view text) {
	auto* pm = new PositionMap;
	int delta = 0;
	size_t i = 0;
	while (i < text.size()) {
		unsigned char b = (unsigned char)text[i];
		if (b < kRuneSelf) {
			i++;
			continue;
		}
		int size = 0;
		char32_t r = decodeJSStringRune(text, i, &size);
		int utf16Size = r >= 0x10000 ? 2 : 1;
		delta += size - utf16Size;
		pm->entries.push_back({(int)(i + size), delta});
		i += size;
	}
	pm->asciiOnly = pm->entries.empty();
	return pm;
}

// newExternalDiagnostic — diagnostic.go:247.
Diagnostic* newExternalDiagnostic(SourceFile* file, TextRange loc,
                                  std::string_view source,
                                  DiagnosticCategory category, int32_t code,
                                  std::string_view messageText) {
	auto* d = new Diagnostic;
	d->file = file;
	d->loc = loc;
	d->code = code;
	d->category = category;
	d->source = std::string(source);
	d->messageText = std::string(messageText);
	return d;
}

} // namespace tsc
