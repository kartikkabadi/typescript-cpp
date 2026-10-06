// Port of tsc/internal/testutil/contentmappertest — protocol.go, spawner.go,
// registry.go, manifest.go, transforming.go, verbatim.go,
// dynamic_verbatim.go, diagnostic_code_collision.go, failing.go,
// component.go, synthesizing.go, lisp.go, duplicate.go, editing.go,
// hoisting.go, supplemental.go, supplemental_diagnostics.go,
// supplemental_globals.go, supplemental_module.go, duplicate_projection.go.
#include "internal/testutil/contentmappertest/contentmappertest.h"

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_map>

#include "internal/collections/collections.h"
#include "internal/core/text.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/ipc/ipc.h"
#include "internal/json/json.h"
#include "internal/spanmap/spanmap.h"
#include "internal/tspath/tspath.h"

namespace tsc::testutil::contentmappertest {

namespace cm = ::tsc::contentmapper;

namespace {

// --- param unmarshalers: Go `json.Unmarshal(params, &p)` --------------------

gostd::Error unmarshalOpenProjectParams(const json::Value& raw,
                                        cm::OpenProjectParams* p) {
	auto [d, err] = json::parse(raw);
	if (err) return err;
	if (d.kind != json::Dom::K::Object) {
		return gostd::newError(
		    "json: cannot unmarshal non-object into OpenProjectParams");
	}
	if (const json::Dom* v = json::objGet(d, "configFileName")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return e;
		p->ConfigFileName = std::move(s);
	}
	if (const json::Dom* v = json::objGet(d, "projectHandle")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return e;
		p->ProjectHandle = std::move(s);
	}
	if (const json::Dom* v = json::objGet(d, "options")) {
		p->Options = json::Value(std::string(v->raw));
	}
	if (const json::Dom* v = json::objGet(d, "compilerOptions")) {
		p->CompilerOptions = json::Value(std::string(v->raw));
	}
	return nullptr;
}

gostd::Error unmarshalCloseProjectParams(const json::Value& raw,
                                         cm::CloseProjectParams* p) {
	auto [d, err] = json::parse(raw);
	if (err) return err;
	if (d.kind != json::Dom::K::Object) {
		return gostd::newError(
		    "json: cannot unmarshal non-object into CloseProjectParams");
	}
	if (const json::Dom* v = json::objGet(d, "projectHandle")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return e;
		p->ProjectHandle = std::move(s);
	}
	return nullptr;
}

gostd::Error unmarshalTransformParams(const json::Value& raw,
                                      cm::TransformParams* p) {
	auto [d, err] = json::parse(raw);
	if (err) return err;
	if (d.kind != json::Dom::K::Object) {
		return gostd::newError(
		    "json: cannot unmarshal non-object into TransformParams");
	}
	if (const json::Dom* v = json::objGet(d, "fileName")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return e;
		p->FileName = std::move(s);
	}
	if (const json::Dom* v = json::objGet(d, "content")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return e;
		p->Content = std::move(s);
	}
	if (const json::Dom* v = json::objGet(d, "projectHandle")) {
		auto [s, e] = json::asString(*v, "string");
		if (e) return e;
		p->ProjectHandle = std::move(s);
	}
	return nullptr;
}

// --- result marshalers: Go json.Marshal of the protocol result structs -----

json::Value marshalInitializeResult(const cm::InitializeResult& r) {
	return json::marshalObject(
	    {{"positionEncoding", json::marshalString(r.PositionEncoding)},
	     {"diagnosticSource", json::marshalString(r.DiagnosticSource)}});
}

json::Value marshalOptionDiagnosticResult(const cm::OptionDiagnosticResult& d) {
	return json::marshalObject(
	    {{"path", json::marshalArray(d.Path)},
	     {"messageText", json::marshalString(d.MessageText)},
	     {"code", json::marshalInt64(d.Code)}});
}

json::Value marshalOpenProjectResult(const cm::OpenProjectResult& r) {
	std::vector<std::pair<std::string, std::string>> members;
	members.emplace_back("configIdentity",
	                     json::marshalString(r.ConfigIdentity));
	if (!r.WatchedFiles.empty()) { // `json:"watchedFiles,omitempty"`
		std::vector<std::string> els;
		els.reserve(r.WatchedFiles.size());
		for (const auto& f : r.WatchedFiles) els.push_back(json::marshalString(f));
		members.emplace_back("watchedFiles", json::marshalArray(els));
	}
	if (!r.OptionDiagnostics.empty()) { // `json:"optionDiagnostics,omitempty"`
		std::vector<std::string> els;
		els.reserve(r.OptionDiagnostics.size());
		for (const auto& d : r.OptionDiagnostics) {
			els.push_back(marshalOptionDiagnosticResult(d));
		}
		members.emplace_back("optionDiagnostics", json::marshalArray(els));
	}
	return json::marshalObject(members);
}

json::Value marshalDiagnosticDirectives(const cm::DiagnosticDirectives& dd) {
	std::vector<std::string> unused;
	unused.reserve(dd.UnusedExpectDirectiveDiagnostics.size());
	for (const auto& u : dd.UnusedExpectDirectiveDiagnostics) {
		unused.push_back(json::marshalObject(
		    {{"code", json::marshalInt64(u.Code)},
		     {"messageText", json::marshalString(u.MessageText)}}));
	}
	std::vector<std::string> directives;
	directives.reserve(dd.Directives.size());
	for (const auto& d : dd.Directives) {
		directives.push_back(d.marshalJSONTo());
	}
	return json::marshalObject(
	    {{"unusedExpectDirectiveDiagnostics", json::marshalArray(unused)},
	     {"directives", json::marshalArray(directives)}});
}

json::Value marshalMappedOutput(const cm::MappedOutput& o) {
	std::vector<std::pair<std::string, std::string>> members;
	members.emplace_back("text", json::marshalString(o.Text));
	members.emplace_back("extension", json::marshalString(o.Extension));
	if (!o.Mappings.empty()) { // `json:"mappings,omitempty"`
		members.emplace_back("mappings", o.Mappings);
	}
	if (o.DiagnosticDirectives.has_value()) { // `json:"diagnosticDirectives,omitempty"`
		members.emplace_back("diagnosticDirectives",
		                     marshalDiagnosticDirectives(
		                         *o.DiagnosticDirectives));
	}
	return json::marshalObject(members);
}

json::Value marshalDiagnostic(const cm::Diagnostic& d) {
	return json::marshalObject(
	    {{"messageText", json::marshalString(d.MessageText)},
	     {"start", json::marshalInt64(d.Start)},
	     {"length", json::marshalInt64(d.Length)},
	     {"code", json::marshalInt64(d.Code)}});
}

json::Value marshalTransformResult(const cm::TransformResult& r) {
	std::string out = marshalMappedOutput(r);
	// MappedOutput's members are encoded first; append the TransformResult
	// members inside the same object.
	if (!r.Diagnostics.empty() || !r.Supplemental.empty()) {
		std::vector<std::pair<std::string, std::string>> members;
		members.emplace_back("text", json::marshalString(r.Text));
		members.emplace_back("extension", json::marshalString(r.Extension));
		if (!r.Mappings.empty()) {
			members.emplace_back("mappings", r.Mappings);
		}
		if (r.DiagnosticDirectives.has_value()) {
			members.emplace_back("diagnosticDirectives",
			                     marshalDiagnosticDirectives(
			                         *r.DiagnosticDirectives));
		}
		if (!r.Diagnostics.empty()) { // `json:"diagnostics,omitempty"`
			std::vector<std::string> els;
			els.reserve(r.Diagnostics.size());
			for (const auto& d : r.Diagnostics) {
				els.push_back(marshalDiagnostic(d));
			}
			members.emplace_back("diagnostics", json::marshalArray(els));
		}
		if (!r.Supplemental.empty()) { // `json:"supplemental,omitempty"`
			std::vector<std::string> els;
			els.reserve(r.Supplemental.size());
			for (const auto& s : r.Supplemental) {
				els.push_back(marshalMappedOutput(s));
			}
			members.emplace_back("supplemental", json::marshalArray(els));
		}
		out = json::marshalObject(members);
	}
	return out;
}

// --- protocol.go -------------------------------------------------------------

// noNotifications — protocol.go:16.
struct noNotifications {
	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) {
		return nullptr;
	}
};

// initializeResult — protocol.go:22.
cm::InitializeResult initializeResult(std::string_view source) {
	return cm::InitializeResult{cm::PositionEncodingUTF8,
	                            std::string(source)};
}

// identityMappedOutput — protocol.go:29.
std::pair<cm::MappedOutput, gostd::Error> identityMappedOutput(
    const std::string& content) {
	auto [mappings, err] = spanmap::Marshal(
	    spanmap::New({spanmap::Segment{
	        0,
	        TextPos(content.size()),
	        0,
	        TextPos(content.size()),
	        spanmap::KindVerbatim,
	        spanmap::FeatureAll,
	    }}));
	if (err.has_value()) {
		return {cm::MappedOutput{}, gostd::newError(*err)};
	}
	return {cm::MappedOutput{/*.Text*/ content, /*.Extension*/ ".ts",
	                         /*.Mappings*/ json::Value(mappings)},
	        nullptr};
}

// projectLifecycleHandler — protocol.go:38.
struct projectLifecycleHandler {
	virtual ~projectLifecycleHandler() = default;
	virtual gostd::Error OpenProject(const cm::OpenProjectParams& params) = 0;
	virtual void CloseProject(const cm::CloseProjectParams& params) = 0;
};

// staticProjectHandler — protocol.go:36.
struct staticProjectHandler : ipc::Handler {
	std::shared_ptr<ipc::Handler> inner;

	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context ctx, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodOpenProject) {
			cm::OpenProjectParams p;
			if (auto err = unmarshalOpenProjectParams(params, &p)) {
				return {json::Value{}, err};
			}
			if (auto* handler =
			        dynamic_cast<projectLifecycleHandler*>(inner.get())) {
				if (auto err = handler->OpenProject(p)) {
					return {json::Value{}, err};
				}
			}
			std::vector<cm::OptionDiagnosticResult> diagnostics;
			if (std::string_view(p.Options) == "{\"plugins\":[{\"name\":1}]}") {
				diagnostics.push_back(cm::OptionDiagnosticResult{
				    {json::Value(R"("plugins")"), json::Value("0"),
				     json::Value(R"("name")")},
				    "Option 'name' requires a string.",
				    123,
				});
			}
			return {marshalOpenProjectResult(
			            cm::OpenProjectResult{"", {}, diagnostics}),
			        nullptr};
		}
		if (method == cm::MethodCloseProject) {
			cm::CloseProjectParams p;
			if (auto err = unmarshalCloseProjectParams(params, &p)) {
				return {json::Value{}, err};
			}
			if (auto* handler =
			        dynamic_cast<projectLifecycleHandler*>(inner.get())) {
				handler->CloseProject(p);
			}
			return {json::Value{}, nullptr};
		}
		return inner->HandleRequest(ctx, method, params);
	}

	gostd::Error HandleNotification(gostd::Context ctx,
	                                std::string_view method,
	                                const json::Value& params) override {
		return inner->HandleNotification(ctx, method, params);
	}
};

// unmarshalCompilerOptionsMap — `json.Unmarshal(p.CompilerOptions, &options)`
// into *OrderedMap[string, json.Value]: object member order is preserved.
std::pair<std::shared_ptr<collections::OrderedMap<std::string, json::Value>>,
          gostd::Error>
unmarshalCompilerOptionsMap(const json::Value& raw) {
	auto [d, err] = json::parse(raw);
	if (err) {
		return {nullptr, err};
	}
	if (d.kind != json::Dom::K::Object) {
		return {nullptr,
		        gostd::newError(
		            "json: cannot unmarshal non-object into compilerOptions")};
	}
	auto m =
	    std::make_shared<collections::OrderedMap<std::string, json::Value>>();
	for (const auto& kv : d.obj) {
		m->Set(kv.first, json::Value(std::string(kv.second.raw)));
	}
	return {m, nullptr};
}

// --- transforming.go ---------------------------------------------------------

constexpr std::string_view preamble = "const __VERSION = \"1.0.0\";\n";
constexpr std::string_view diagnosticSource = "box";
constexpr int32_t unclosedInterpolationCode = 1000;

// mappedExtension — transforming.go:71.
std::string mappedExtension(const std::string& content) {
	constexpr std::string_view prefix = "// @box-extension:";
	auto nl = content.find('\n');
	std::string_view firstLine =
	    nl == std::string::npos ? content : std::string_view(content).substr(0, nl);
	if (firstLine.starts_with(prefix)) {
		std::string s(firstLine.substr(prefix.size()));
		// strings.TrimSpace
		auto b = s.find_first_not_of(" \t\r\n\v\f");
		auto e = s.find_last_not_of(" \t\r\n\v\f");
		return b == std::string::npos ? "" : s.substr(b, e - b + 1);
	}
	return ".ts";
}

// renderOption — transforming.go:264.
std::string renderOption(
    const collections::OrderedMap<std::string, json::Value>* options,
    const std::string& name) {
	if (options != nullptr) {
		if (auto [value, ok] = options->Get(name); ok && !value->empty()) {
			return *value;
		}
	}
	return "undefined";
}

// diagnosticDirectives — transforming.go:164.
std::optional<cm::DiagnosticDirectives> diagnosticDirectives(
    const std::string& content, spanmap::SpanMap* mappings) {
	auto wrap = [](std::vector<cm::MappedDiagnosticDirective> directives,
	               std::vector<cm::UnusedExpectDirectiveDiagnostic> unused) {
		return std::optional<cm::DiagnosticDirectives>(
		    cm::DiagnosticDirectives{unused, directives});
	};
	constexpr std::string_view invalidPrefix = "// @box-invalid-directive:";
	if (content.starts_with(invalidPrefix)) {
		auto nl = content.find('\n');
		std::string_view firstLine = nl == std::string::npos
		    ? std::string_view(content)
		    : std::string_view(content).substr(0, nl);
		auto rest = firstLine.substr(invalidPrefix.size());
		// strings.TrimSpace
		auto b = rest.find_first_not_of(" \t\r\n\v\f");
		auto e = rest.find_last_not_of(" \t\r\n\v\f");
		std::string_view which =
		    b == std::string_view::npos ? "" : rest.substr(b, e - b + 1);
		if (which == "invalid-range") {
			return wrap({cm::MappedDiagnosticDirective{
			                0, 0, -1, 0,
			                cm::DiagnosticDirectivePolicyIgnore}},
			            {});
		}
		if (which == "original-range-out-of-bounds") {
			return wrap({cm::MappedDiagnosticDirective{
			                (int64_t)content.size() + 1, 0, 0, 0,
			                cm::DiagnosticDirectivePolicyIgnore}},
			            {});
		}
		if (which == "virtual-range-out-of-bounds") {
			return wrap({cm::MappedDiagnosticDirective{
			                0, 0, 1 << 20, 1 << 20,
			                cm::DiagnosticDirectivePolicyIgnore}},
			            {});
		}
		if (which == "invalid-policy") {
			return wrap({cm::MappedDiagnosticDirective{0, 0, 0, 0, 2}}, {});
		}
		if (which == "ignore-with-unused-diagnostic") {
			return wrap({cm::MappedDiagnosticDirective{
			                0, 0, 0, 0,
			                cm::DiagnosticDirectivePolicyIgnore}},
			            {cm::UnusedExpectDirectiveDiagnostic{}});
		}
		if (which == "expect-without-unused-diagnostic") {
			return wrap({cm::MappedDiagnosticDirective{
			                0, 0, 0, 0,
			                cm::DiagnosticDirectivePolicyExpect}},
			            {});
		}
		if (which == "invalid-unused-diagnostic-index") {
			return wrap({cm::MappedDiagnosticDirective{
			                0, 0, 0, 0,
			                cm::DiagnosticDirectivePolicyExpect, 1}},
			            {cm::UnusedExpectDirectiveDiagnostic{}});
		}
		if (which == "overlap") {
			return wrap({cm::MappedDiagnosticDirective{
			                 0, 0, 0, 2,
			                 cm::DiagnosticDirectivePolicyIgnore},
			             cm::MappedDiagnosticDirective{
			                 0, 0, 1, 3,
			                 cm::DiagnosticDirectivePolicyIgnore}},
			            {});
		}
	}
	constexpr std::string_view ignorePrefix = "// @box-ignore";
	constexpr std::string_view expectPrefix = "// @box-expect-error";
	std::vector<cm::MappedDiagnosticDirective> result;
	std::vector<cm::UnusedExpectDirectiveDiagnostic> unusedDiagnostics;
	for (int lineStart = 0; lineStart < (int)content.size();) {
		size_t rel = content.find('\n', lineStart);
		int lineEnd = rel == std::string::npos
		    ? (int)content.size()
		    : (int)rel;
		std::string_view line =
		    std::string_view(content).substr(lineStart, lineEnd - lineStart);
		// strings.TrimSpace
		auto tb = line.find_first_not_of(" \t\r\n\v\f");
		auto te = line.find_last_not_of(" \t\r\n\v\f");
		std::string_view trimmed = tb == std::string_view::npos
		    ? std::string_view{}
		    : line.substr(tb, te - tb + 1);
		uint8_t policy = 0;
		bool hasPolicy = false;
		int64_t unusedDiagnosticIndex = -1;
		if (trimmed == ignorePrefix) {
			policy = cm::DiagnosticDirectivePolicyIgnore;
			hasPolicy = true;
		} else if (trimmed.starts_with(
		               std::string(expectPrefix) + ":")) {
			policy = cm::DiagnosticDirectivePolicyExpect;
			hasPolicy = true;
			unusedDiagnosticIndex = (int64_t)unusedDiagnostics.size();
			std::string_view msg =
			    trimmed.substr(expectPrefix.size() + 1);
			auto mb = msg.find_first_not_of(" \t\r\n\v\f");
			auto me = msg.find_last_not_of(" \t\r\n\v\f");
			unusedDiagnostics.push_back(cm::UnusedExpectDirectiveDiagnostic{
			    2578,
			    std::string(mb == std::string_view::npos
			                    ? ""
			                    : msg.substr(mb, me - mb + 1))});
		}
		if (hasPolicy && lineEnd < (int)content.size()) {
			int affectedStart = lineEnd + 1;
			size_t nl2 = content.find('\n', affectedStart);
			int affectedLength =
			    nl2 == std::string::npos
			        ? (int)content.size() - affectedStart
			        : (int)nl2 - affectedStart;
			auto virtualSpans = spanmap::OriginalToVirtualSpans(
			    mappings,
			    TextRange{TextPos(affectedStart),
			              TextPos(affectedStart + affectedLength)},
			    spanmap::FeatureAll);
			if (virtualSpans.size() == 1) {
				cm::MappedDiagnosticDirective directive{
				    (int64_t)lineStart,
				    (int64_t)(lineEnd - lineStart),
				    (int64_t)virtualSpans[0].Span.pos(),
				    (int64_t)virtualSpans[0].Span.end(),
				    policy,
				};
				if (unusedDiagnosticIndex >= 0) {
					directive.UnusedExpectDirectiveIndex =
					    unusedDiagnosticIndex;
				}
				result.push_back(directive);
			}
		}
		if (lineEnd == (int)content.size()) {
			break;
		}
		lineStart = lineEnd + 1;
	}
	if (unusedDiagnostics.size() == 1) {
		for (auto& r : result) {
			r.UnusedExpectDirectiveIndex.reset();
		}
	}
	if (result.empty() && unusedDiagnostics.empty()) {
		return std::nullopt;
	}
	return wrap(result, unusedDiagnostics);
}

// transform — transforming.go:81.
std::tuple<std::string, json::Value, std::vector<cm::Diagnostic>,
           std::optional<cm::DiagnosticDirectives>, gostd::Error>
transform(const std::string& content,
          const collections::OrderedMap<std::string, json::Value>* options) {
	std::string virtual_;
	std::vector<spanmap::Segment> segments;
	std::vector<cm::Diagnostic> diagnostics;

	virtual_ += preamble;

	auto writeVerbatim = [&](int from, int to) {
		if (to <= from) {
			return;
		}
		TextPos virtualStart = TextPos(virtual_.size());
		virtual_.append(content, from, to - from);
		segments.push_back(spanmap::Segment{
		    virtualStart, TextPos(virtual_.size()), TextPos(from),
		    TextPos(to), spanmap::KindVerbatim, spanmap::FeatureAll});
	};
	auto writeAtom = [&](std::string_view value, int from, int to) {
		TextPos virtualStart = TextPos(virtual_.size());
		virtual_ += value;
		segments.push_back(spanmap::Segment{
		    virtualStart, TextPos(virtual_.size()), TextPos(from),
		    TextPos(to), spanmap::KindAtom, spanmap::FeatureAll});
	};

	int pos = 0;
	while (pos < (int)content.size()) {
		auto rel = content.find("#{", pos);
		if (rel == std::string::npos) {
			writeVerbatim(pos, (int)content.size());
			break;
		}
		int tokenStart = (int)rel;

		auto nlPos = content.find('\n', tokenStart);
		int lineEnd = nlPos == std::string::npos ? -1 : (int)nlPos;
		// Go: strings.IndexByte returns -1; lineEnd = tokenStart + rel, and
		// `lineEnd < tokenStart` detects absence.
		if (lineEnd < tokenStart) {
			lineEnd = (int)content.size();
		}
		auto closeRel = content.find('}', tokenStart);
		if (closeRel == std::string::npos || closeRel >= (size_t)lineEnd) {
			writeVerbatim(pos, tokenStart);
			writeAtom("undefined", tokenStart, lineEnd);
			diagnostics.push_back(cm::Diagnostic{
			    "Unclosed interpolation.",
			    tokenStart,
			    lineEnd - tokenStart,
			    unclosedInterpolationCode,
			});
			pos = lineEnd;
			continue;
		}
		int tokenEnd = (int)closeRel + 1;
		std::string name = content.substr(tokenStart + 2,
		                                  tokenEnd - 1 - (tokenStart + 2));

		writeVerbatim(pos, tokenStart);
		writeAtom(renderOption(options, name), tokenStart, tokenEnd);
		pos = tokenEnd;
	}

	spanmap::SpanMap* spanMap = spanmap::New(segments);
	auto [mappings, err] = spanmap::Marshal(spanMap);
	if (err.has_value()) {
		return {"", json::Value{}, {}, std::nullopt, gostd::newError(*err)};
	}
	return {virtual_, json::Value(mappings), diagnostics,
	        diagnosticDirectives(content, spanMap), nullptr};
}

// Handler — transforming.go:26.
struct Handler final : ipc::Handler, projectLifecycleHandler {
	std::mutex mu;
	std::unordered_map<
	    std::string,
	    std::shared_ptr<collections::OrderedMap<std::string, json::Value>>>
	    compilerOptions;

	// OpenProject — transforming.go:37.
	gostd::Error OpenProject(const cm::OpenProjectParams& p) override {
		auto [options, err] = unmarshalCompilerOptionsMap(p.CompilerOptions);
		if (err) {
			return err;
		}
		std::lock_guard<std::mutex> lock(mu);
		compilerOptions[p.ProjectHandle] = options;
		return nullptr;
	}

	// CloseProject — transforming.go:50.
	void CloseProject(const cm::CloseProjectParams& p) override {
		std::lock_guard<std::mutex> lock(mu);
		compilerOptions.erase(p.ProjectHandle);
	}

	// HandleRequest — transforming.go:56.
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult(diagnosticSource)),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			std::shared_ptr<
			    collections::OrderedMap<std::string, json::Value>>
			    options;
			{
				std::lock_guard<std::mutex> lock(mu);
				auto it = compilerOptions.find(p.ProjectHandle);
				if (it != compilerOptions.end()) options = it->second;
			}
			if (!options) {
				return {json::Value{},
				        gostd::errorf(
				            "contentmappertest: project %q is not open",
				            {p.ProjectHandle})};
			}
			auto [text, mappings, diagnostics, directives, err] =
			    transform(p.Content, options.get());
			if (err) {
				return {json::Value{}, err};
			}
			cm::TransformResult res;
			res.Text = text;
			res.Extension = mappedExtension(p.Content);
			res.Mappings = mappings;
			res.DiagnosticDirectives = directives;
			res.Diagnostics = std::move(diagnostics);
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- verbatim.go -------------------------------------------------------------

// verbatimHandler — verbatim.go:10.
struct verbatimHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			auto [mappedOutput, err] = identityMappedOutput(p.Content);
			if (err) {
				return {json::Value{}, err};
			}
			cm::TransformResult res;
			static_cast<cm::MappedOutput&>(res) = (mappedOutput);
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// moduleVerbatimHandler — verbatim.go:12.
struct moduleVerbatimHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			auto [mappedOutput, err] = identityMappedOutput(p.Content);
			if (err) {
				return {json::Value{}, err};
			}
			auto out = mappedOutput;
			out.Extension = ".mts";
			cm::TransformResult res;
			static_cast<cm::MappedOutput&>(res) = (out);
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- dynamic_verbatim.go -----------------------------------------------------

// dynamicVerbatimHandler — dynamic_verbatim.go:19.
struct dynamicVerbatimHandler : ipc::Handler {
	verbatimHandler verbatim;
	ProjectLifecycle* lifecycle = nullptr;

	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context ctx, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodOpenProject) {
			if (lifecycle != nullptr) {
				lifecycle->Opens.fetch_add(1);
			}
			cm::OpenProjectParams p;
			if (auto err = unmarshalOpenProjectParams(params, &p)) {
				return {json::Value{}, err};
			}
			std::string identity =
			    p.ConfigFileName + ":" + std::string(p.Options);
			std::vector<cm::OptionDiagnosticResult> diagnostics;
			if (std::string_view(p.Options) == "{\"plugins\":[{\"name\":1}]}") {
				diagnostics.push_back(cm::OptionDiagnosticResult{
				    {json::Value(R"("plugins")"), json::Value("0"),
				     json::Value(R"("name")")},
				    "Option 'name' requires a string.",
				    123,
				});
			}
			std::string watchDirectory =
			    tspath::getDirectoryPath(p.ConfigFileName);
			if (watchDirectory.empty()) {
				watchDirectory = "/";
			}
			cm::OpenProjectResult res;
			res.ConfigIdentity = identity;
			res.WatchedFiles = {
			    tspath::combinePaths(watchDirectory, {"mapper.config.json"})};
			res.OptionDiagnostics = std::move(diagnostics);
			return {marshalOpenProjectResult(res), nullptr};
		}
		if (method == cm::MethodCloseProject) {
			if (lifecycle != nullptr) {
				lifecycle->Closes.fetch_add(1);
			}
			return {json::Value{}, nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			if (p.ProjectHandle.empty()) {
				return {json::Value{},
				        gostd::newError("content mapper transform requires a "
				                        "project handle")};
			}
		}
		return verbatim.HandleRequest(ctx, method, params);
	}

	gostd::Error HandleNotification(gostd::Context ctx,
	                                std::string_view method,
	                                const json::Value& params) override {
		return verbatim.HandleNotification(ctx, method, params);
	}
};

// --- diagnostic_code_collision.go ---------------------------------------------

// diagnosticCodeCollisionHandler — diagnostic_code_collision.go:11.
struct diagnosticCodeCollisionHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			auto [mappedOutput, err] = identityMappedOutput(p.Content);
			if (err) {
				return {json::Value{}, err};
			}
			auto start = p.Content.find("foo");
			cm::TransformResult res;
			static_cast<cm::MappedOutput&>(res) = (mappedOutput);
			res.Diagnostics = {cm::Diagnostic{
			    "Mapper diagnostic with a colliding code.",
			    (int64_t)(start == std::string::npos ? -1 : start),
			    (int64_t)strlen("foo"),
			    
			        Function_must_have_an_explicit_return_type_annotation_with_isolatedDeclarations
			            ->code,
			}};
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- failing.go ---------------------------------------------------------------

// failingHandler — failing.go:11.
struct failingHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			return {json::Value{},
			        gostd::newError(
			            "content mapper failed to transform the file")};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- component.go --------------------------------------------------------------

bool isIdentifierStart(char ch) {
	return ch == '_' || ch == '$' || (ch >= 'A' && ch <= 'Z') ||
	       (ch >= 'a' && ch <= 'z');
}

bool isIdentifierPart(char ch) {
	return isIdentifierStart(ch) || (ch >= '0' && ch <= '9');
}

// componentNameRange — component.go:117.
std::tuple<int, int, bool> componentNameRange(const std::string& content) {
	auto componentStart = content.find("<component");
	if (componentStart == std::string::npos) {
		return {0, 0, false};
	}
	auto tagEndRel = content.find('>', componentStart);
	if (tagEndRel == std::string::npos) {
		return {0, 0, false};
	}
	std::string_view tag = std::string_view(content).substr(
	    componentStart, tagEndRel - componentStart);
	auto nameRel = tag.find("name=\"");
	if (nameRel == std::string_view::npos) {
		return {0, 0, false};
	}
	int start = (int)(componentStart + nameRel + strlen("name=\""));
	auto endRel = content.find('"', start);
	if (endRel == std::string::npos) {
		return {0, 0, false};
	}
	return {start, (int)endRel, true};
}

// transformComponent — component.go:31.
std::tuple<std::string, json::Value, gostd::Error> transformComponent(
    const std::string& content) {
	std::string virtual_;
	std::vector<spanmap::Segment> segments;

	auto writeSynthesized = [&](std::string_view text) {
		virtual_ += text;
	};
	auto writeMapped = [&](std::string_view text, int originalStart,
	                       int originalEnd, spanmap::Kind kind) {
		TextPos virtualStart = TextPos(virtual_.size());
		virtual_ += text;
		segments.push_back(spanmap::Segment{
		    virtualStart, TextPos(virtual_.size()), TextPos(originalStart),
		    TextPos(originalEnd), kind, spanmap::FeatureAll});
	};
	auto writeAnchored = [&](std::string_view text, int originalPosition,
	                         spanmap::Feature features) {
		TextPos virtualStart = TextPos(virtual_.size());
		virtual_ += text;
		segments.push_back(spanmap::Segment{
		    virtualStart, TextPos(virtual_.size()),
		    TextPos(originalPosition), TextPos(originalPosition),
		    spanmap::KindAtom, features});
	};

	auto scriptOpen = content.find("<script");
	if (scriptOpen != std::string::npos) {
		auto openEndRel = content.find('>', scriptOpen);
		if (openEndRel == std::string::npos) {
			return {"", json::Value{},
			        gostd::newError(
			            "contentmappertest: unclosed <script> tag")};
		}
		int scriptStart = (int)(scriptOpen + (openEndRel - scriptOpen) + 1);
		auto closeRel = content.find("</script>", scriptStart);
		if (closeRel == std::string::npos) {
			return {"", json::Value{},
			        gostd::newError(
			            "contentmappertest: missing </script> tag")};
		}
		int scriptEnd = (int)closeRel;
		writeMapped(
		    std::string_view(content).substr(scriptStart,
		                                     scriptEnd - scriptStart),
		    scriptStart, scriptEnd, spanmap::KindVerbatim);
	}

	writeSynthesized("\nfunction __render() {\n");
	for (int searchStart = 0; searchStart < (int)content.size();) {
		auto openRel = content.find("{{", searchStart);
		if (openRel == std::string::npos) {
			break;
		}
		int exprStart = (int)openRel + 2;
		auto closeRel = content.find("}}", exprStart);
		if (closeRel == std::string::npos) {
			return {"", json::Value{},
			        gostd::newError(
			            "contentmappertest: unclosed template expression")};
		}
		int exprEnd = (int)closeRel;
		writeSynthesized("  void (");
		for (int p = exprStart; p < exprEnd;) {
			if (!isIdentifierStart(content[p])) {
				writeSynthesized(std::string_view(content).substr(p, 1));
				p++;
				continue;
			}
			int end = p + 1;
			while (end < exprEnd && isIdentifierPart(content[end])) {
				end++;
			}
			writeMapped(std::string_view(content).substr(p, end - p), p, end,
			            spanmap::KindAtom);
			p = end;
		}
		writeSynthesized(");\n");
		searchStart = exprEnd + 2;
	}
	writeSynthesized("}\n");
	if (auto [nameStart, nameEnd, ok] = componentNameRange(content); ok) {
		writeSynthesized("export class ");
		writeMapped(std::string_view(content).substr(nameStart,
		                                             nameEnd - nameStart),
		            nameStart, nameEnd, spanmap::KindAtom);
		writeSynthesized(" {}\n");
	}
	writeAnchored("export default {};\n", 0,
	              spanmap::FeatureDefinition | spanmap::FeatureReferences);

	auto [mappings, err] = spanmap::Marshal(spanmap::New(segments));
	if (err.has_value()) {
		return {"", json::Value{}, gostd::newError(*err)};
	}
	return {virtual_, json::Value(mappings), nullptr};
}

// componentHandler — component.go:15.
struct componentHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			auto [text, mappings, err] = transformComponent(p.Content);
			if (err) {
				return {json::Value{}, err};
			}
			cm::TransformResult res;
			res.Text = text;
			res.Extension = ".ts";
			res.Mappings = mappings;
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- synthesizing.go -----------------------------------------------------------

constexpr std::string_view synthesizedOutput =
    "export const el = jsxRuntime(Widget);\n";

// synthesizingHandler — synthesizing.go:13.
struct synthesizingHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			auto [mappings, err] = spanmap::Marshal(spanmap::New({}));
			if (err.has_value()) {
				return {json::Value{}, gostd::newError(*err)};
			}
			cm::TransformResult res;
			res.Text = synthesizedOutput;
			res.Extension = ".ts";
			res.Mappings = json::Value(mappings);
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- lisp.go --------------------------------------------------------------------

// lispHandler — lisp.go:12.
struct lispHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("lisp")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			std::string_view c = p.Content;
			if (!c.empty() && c.back() == '\n') c.remove_suffix(1);
			if (c != R"((+ 1 2 "oops"))") {
				return {json::Value{},
				        gostd::errorf(
				            "contentmappertest: unsupported Lisp expression %q",
				            {p.Content})};
			}
			auto [mappings, err] = spanmap::Marshal(spanmap::New({
			    spanmap::Segment{0, 3, 1, 2, spanmap::KindAlias,
			                     spanmap::FeatureAll},
			    spanmap::Segment{4, 5, 3, 4, spanmap::KindVerbatim,
			                     spanmap::FeatureAll},
			    spanmap::Segment{7, 8, 5, 6, spanmap::KindVerbatim,
			                     spanmap::FeatureAll},
			    spanmap::Segment{10, 16, 7, 13, spanmap::KindVerbatim,
			                     spanmap::FeatureAll},
			}));
			if (err.has_value()) {
				return {json::Value{}, gostd::newError(*err)};
			}
			cm::TransformResult res;
			res.Text = "add(1, 2, \"oops\");";
			res.Extension = ".ts";
			res.Mappings = json::Value(mappings);
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- duplicate.go -----------------------------------------------------------------

// duplicateHandler — duplicate.go:12.
struct duplicateHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			const std::string& content = p.Content;
			int n = (int)content.size();

			if (p.FileName.find("hover-fallback") != std::string::npos) {
				std::string virtual_ =
				    "// " + content + "\nconst " + content + " = 1;\n";
				int first = (int)strlen("// ");
				int second = first + n + (int)strlen("\nconst ");
				auto [mappings, err] = spanmap::Marshal(spanmap::New({
				    spanmap::Segment{TextPos(first), TextPos(first + n), 0,
				                     TextPos(n), spanmap::KindVerbatim,
				                     spanmap::FeatureHover},
				    spanmap::Segment{TextPos(second), TextPos(second + n), 0,
				                     TextPos(n), spanmap::KindVerbatim,
				                     spanmap::FeatureHover},
				}));
				if (err.has_value()) {
					return {json::Value{}, gostd::newError(*err)};
				}
				cm::TransformResult res;
				res.Text = virtual_;
				res.Extension = ".ts";
				res.Mappings = json::Value(mappings);
				return {marshalTransformResult(res), nullptr};
			}
			if (p.FileName.find("hover-concat") != std::string::npos) {
				std::string virtual_ =
				    "namespace A { export const " + content +
				    " = 1; }\nnamespace B { export const " + content +
				    " = \"text\"; }\n";
				int first = (int)virtual_.find(content);
				int second = (int)virtual_.rfind(content);
				auto [mappings, err] = spanmap::Marshal(spanmap::New({
				    spanmap::Segment{TextPos(first), TextPos(first + n), 0,
				                     TextPos(n), spanmap::KindVerbatim,
				                     spanmap::FeatureHover},
				    spanmap::Segment{TextPos(second), TextPos(second + n), 0,
				                     TextPos(n), spanmap::KindVerbatim,
				                     spanmap::FeatureHover},
				}));
				if (err.has_value()) {
					return {json::Value{}, gostd::newError(*err)};
				}
				cm::TransformResult res;
				res.Text = virtual_;
				res.Extension = ".ts";
				res.Mappings = json::Value(mappings);
				return {marshalTransformResult(res), nullptr};
			}
			if (p.FileName.find("signature-fallback") != std::string::npos) {
				std::string virtual_ =
				    "// " + content +
				    "\nfunction use(value: number): void {}\n" + content +
				    ";\n";
				int first = (int)strlen("// ");
				int second = (int)virtual_.rfind(content);
				auto [mappings, err] = spanmap::Marshal(spanmap::New({
				    spanmap::Segment{TextPos(first), TextPos(first + n), 0,
				                     TextPos(n), spanmap::KindVerbatim,
				                     spanmap::FeatureSignatureHelp},
				    spanmap::Segment{TextPos(second), TextPos(second + n), 0,
				                     TextPos(n), spanmap::KindVerbatim,
				                     spanmap::FeatureSignatureHelp},
				}));
				if (err.has_value()) {
					return {json::Value{}, gostd::newError(*err)};
				}
				cm::TransformResult res;
				res.Text = virtual_;
				res.Extension = ".ts";
				res.Mappings = json::Value(mappings);
				return {marshalTransformResult(res), nullptr};
			}
			if (p.FileName.find("rename-conflict") != std::string::npos) {
				std::string virtual_ =
				    "export const " + content + " = 1;\nconst object = { " +
				    content + " };\n" + content + ";\n";
				int first = (int)virtual_.find(content);
				int second =
				    (int)virtual_.find(content, first + n) ;
				int third = (int)virtual_.find(content, second + n);
				auto [mappings, err] = spanmap::Marshal(spanmap::New({
				    spanmap::Segment{TextPos(first), TextPos(first + n), 0,
				                     TextPos(n), spanmap::KindVerbatim,
				                     spanmap::FeatureRename},
				    spanmap::Segment{TextPos(second), TextPos(second + n), 0,
				                     TextPos(n), spanmap::KindVerbatim,
				                     spanmap::FeatureRename},
				    spanmap::Segment{TextPos(third), TextPos(third + n), 0,
				                     TextPos(n), spanmap::KindVerbatim,
				                     spanmap::FeatureRename},
				}));
				if (err.has_value()) {
					return {json::Value{}, gostd::newError(*err)};
				}
				cm::TransformResult res;
				res.Text = virtual_;
				res.Extension = ".ts";
				res.Mappings = json::Value(mappings);
				return {marshalTransformResult(res), nullptr};
			}
			std::string virtual_ =
			    "export const " + content + " = 1;\n" + content + ";\n";
			int first = (int)strlen("export const ");
			int second = first + n + (int)strlen(" = 1;\n");
			bool disabled =
			    p.FileName.find("disabled") != std::string::npos;
			spanmap::Feature semanticFeatures =
			    spanmap::FeatureHover | spanmap::FeatureDefinition |
			    spanmap::FeatureReferences | spanmap::FeatureRename;
			spanmap::Feature navigationFeatures =
			    spanmap::FeatureDefinition | spanmap::FeatureReferences |
			    spanmap::FeatureRename;
			if (disabled) {
				semanticFeatures = spanmap::FeatureNone;
				navigationFeatures = spanmap::FeatureNone;
			}
			auto [mappings, err] = spanmap::Marshal(spanmap::New({
			    spanmap::Segment{TextPos(first), TextPos(first + n), 0,
			                     TextPos(n), spanmap::KindVerbatim,
			                     semanticFeatures},
			    spanmap::Segment{TextPos(second), TextPos(second + n), 0,
			                     TextPos(n), spanmap::KindVerbatim,
			                     navigationFeatures},
			}));
			if (err.has_value()) {
				return {json::Value{}, gostd::newError(*err)};
			}
			cm::TransformResult res;
			res.Text = virtual_;
			res.Extension = ".ts";
			res.Mappings = json::Value(mappings);
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- editing.go ---------------------------------------------------------------------

// prefixedSupplementalHandler — editing.go:13.
struct prefixedSupplementalHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			constexpr std::string_view prefix = "/* generated */\n";
			spanmap::Feature features = spanmap::FeatureAll;
			if (p.FileName.find("folding-disabled") != std::string::npos ||
			    p.FileName.find("codelens-disabled") != std::string::npos ||
			    p.FileName.find("formatting-disabled") != std::string::npos) {
				features = spanmap::FeatureNone;
			}
			std::string supplementalText =
			    std::string(prefix) + p.Content;
			std::vector<spanmap::Segment> segments{spanmap::Segment{
			    TextPos(prefix.size()),
			    TextPos(prefix.size() + p.Content.size()),
			    0,
			    TextPos(p.Content.size()),
			    spanmap::KindVerbatim,
			    features,
			}};
			if (p.FileName.find("formatting-split") != std::string::npos) {
				auto secondStart = p.Content.find("function second");
				if (secondStart == std::string::npos) {
					return {json::Value{},
					        gostd::newError(
					            "contentmappertest: formatting-split input is "
					            "missing function second")};
				}
				constexpr std::string_view generated =
				    "const generated={x:1};\n";
				supplementalText =
				    std::string(prefix) +
				    p.Content.substr(0, secondStart) +
				    std::string(generated) +
				    p.Content.substr(secondStart);
				segments = {
				    spanmap::Segment{
				        TextPos(prefix.size()),
				        TextPos(prefix.size() + secondStart),
				        0,
				        TextPos(secondStart),
				        spanmap::KindVerbatim,
				        spanmap::FeatureAll,
				    },
				    spanmap::Segment{
				        TextPos(prefix.size() + secondStart +
				                generated.size()),
				        TextPos(supplementalText.size()),
				        TextPos(secondStart),
				        TextPos(p.Content.size()),
				        spanmap::KindVerbatim,
				        spanmap::FeatureAll,
				    },
				};
			}
			if (p.FileName.find("formatting-overlap") != std::string::npos) {
				auto secondStart = p.Content.find("function second");
				auto thirdStart = p.Content.find("function third");
				if (secondStart == std::string::npos ||
				    thirdStart == std::string::npos) {
					return {json::Value{},
					        gostd::newError(
					            "contentmappertest: formatting-overlap input "
					            "is missing function second or third")};
				}
				constexpr std::string_view wrapperStart = "if (true) {\n";
				constexpr std::string_view wrapperEnd = "}\n";
				supplementalText =
				    std::string(wrapperStart) +
				    p.Content.substr(secondStart) +
				    std::string(wrapperEnd);
				segments = {spanmap::Segment{
				    TextPos(wrapperStart.size()),
				    TextPos(wrapperStart.size() + p.Content.size() -
				            secondStart),
				    TextPos(secondStart),
				    TextPos(p.Content.size()),
				    spanmap::KindVerbatim,
				    spanmap::FeatureAll,
				}};
				auto [canonicalMappings, cerr] =
				    spanmap::Marshal(spanmap::New({spanmap::Segment{
				        0,
				        TextPos(thirdStart),
				        0,
				        TextPos(thirdStart),
				        spanmap::KindVerbatim,
				        spanmap::FeatureAll,
				    }}));
				if (cerr.has_value()) {
					return {json::Value{}, gostd::newError(*cerr)};
				}
				auto [mappings, merr] =
				    spanmap::Marshal(spanmap::New(segments));
				if (merr.has_value()) {
					return {json::Value{}, gostd::newError(*merr)};
				}
				cm::TransformResult res;
				res.Text = p.Content.substr(0, thirdStart);
				res.Extension = ".ts";
				res.Mappings = json::Value(canonicalMappings);
				cm::SupplementalOutput sup;
				sup.Text = supplementalText;
				sup.Extension = ".ts";
				sup.Mappings = json::Value(mappings);
				res.Supplemental = {sup};
				return {marshalTransformResult(res), nullptr};
			}
			auto [mappings, merr] =
			    spanmap::Marshal(spanmap::New(segments));
			if (merr.has_value()) {
				return {json::Value{}, gostd::newError(*merr)};
			}
			cm::MappedOutput canonical;
			canonical.Text = "export {};";
			canonical.Extension = ".ts";
			if (p.FileName.find("folding-duplicate") != std::string::npos ||
			    p.FileName.find("codelens-disabled") != std::string::npos ||
			    p.FileName.find("codelens-duplicate") != std::string::npos) {
				auto [cm2, cerr] =
				    spanmap::Marshal(spanmap::New({spanmap::Segment{
				        0,
				        TextPos(p.Content.size()),
				        0,
				        TextPos(p.Content.size()),
				        spanmap::KindVerbatim,
				        spanmap::FeatureAll,
				    }}));
				if (cerr.has_value()) {
					return {json::Value{}, gostd::newError(*cerr)};
				}
				canonical.Text = p.Content;
				canonical.Extension = ".ts";
				canonical.Mappings = json::Value(cm2);
			}
			cm::TransformResult res;
			static_cast<cm::MappedOutput&>(res) = (canonical);
			cm::SupplementalOutput sup;
			sup.Text = supplementalText;
			sup.Extension = ".ts";
			sup.Mappings = json::Value(mappings);
			res.Supplemental = {sup};
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// unmappedFoldingHandler — editing.go:137.
struct unmappedFoldingHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			auto [mappings, err] = spanmap::Marshal(spanmap::New({}));
			if (err.has_value()) {
				return {json::Value{}, gostd::newError(*err)};
			}
			cm::TransformResult res;
			res.Text = "import \"a\";\nimport \"b\";\n/*\n * generated\n */\nexport {};";
			res.Extension = ".ts";
			res.Mappings = json::Value(mappings);
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- hoisting.go -----------------------------------------------------------------

bool isSpaceByte(char ch) {
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

// scriptRange — hoisting.go:81.
std::tuple<int, int, gostd::Error> scriptRange(const std::string& content) {
	auto scriptOpen = content.find("<script");
	if (scriptOpen == std::string::npos) {
		return {0, 0,
		        gostd::newError("contentmappertest: missing <script> tag")};
	}
	auto openEndRel = content.find('>', scriptOpen);
	if (openEndRel == std::string::npos) {
		return {0, 0,
		        gostd::newError(
		            "contentmappertest: unclosed <script> tag")};
	}
	int start = (int)(scriptOpen + (openEndRel - scriptOpen) + 1);
	auto closeRel = content.find("</script>", start);
	if (closeRel == std::string::npos) {
		return {0, 0,
		        gostd::newError(
		            "contentmappertest: missing </script> tag")};
	}
	return {start, (int)closeRel, nullptr};
}

// leadingImportRange — hoisting.go:99.
std::pair<int, int> leadingImportRange(const std::string& content,
                                       int scriptStart, int scriptEnd) {
	int start = scriptStart;
	while (start < scriptEnd && isSpaceByte(content[start])) {
		start++;
	}
	int end = start;
	while (end < scriptEnd &&
	       std::string_view(content).substr(end, scriptEnd - end)
	           .starts_with("import ")) {
		auto lineEnd = content.find('\n', end);
		if (lineEnd == std::string::npos ||
		    lineEnd >= (size_t)scriptEnd) {
			end = scriptEnd;
			break;
		}
		end = (int)lineEnd;
		while (end < scriptEnd && isSpaceByte(content[end])) {
			end++;
		}
	}
	while (end > start && isSpaceByte(content[end - 1])) {
		end--;
	}
	return {start, end};
}

// transformHoisting — hoisting.go:50.
std::tuple<std::string, json::Value, gostd::Error> transformHoisting(
    const std::string& content) {
	std::string virtual_;
	std::vector<spanmap::Segment> segments;

	auto writeMapped = [&](int originalStart, int originalEnd) {
		TextPos virtualStart = TextPos(virtual_.size());
		virtual_.append(content, originalStart, originalEnd - originalStart);
		segments.push_back(spanmap::Segment{
		    virtualStart, TextPos(virtual_.size()), TextPos(originalStart),
		    TextPos(originalEnd), spanmap::KindVerbatim,
		    spanmap::FeatureAll});
	};

	auto [scriptStart, scriptEnd, err] = scriptRange(content);
	if (err) {
		return {"", json::Value{}, err};
	}
	auto [importsStart, importsEnd] =
	    leadingImportRange(content, scriptStart, scriptEnd);

	virtual_ += "///<reference types=\"svelte\" />\n;\n";
	writeMapped(importsStart, importsEnd);
	virtual_ += "\nfunction $$render() {";
	writeMapped(scriptStart, importsStart);
	writeMapped(importsEnd, scriptEnd);
	virtual_ += "\n;\nreturn { props: {} as Record<string, never> }}\n";

	auto [mappings, merr] = spanmap::Marshal(spanmap::New(segments));
	if (merr.has_value()) {
		return {"", json::Value{}, gostd::newError(*merr)};
	}
	return {virtual_, json::Value(mappings), nullptr};
}

// hoistingHandler — hoisting.go:15.
struct hoistingHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			auto [text, mappings, err] = transformHoisting(p.Content);
			if (err) {
				return {json::Value{}, err};
			}
			cm::TransformResult res;
			res.Text = text;
			res.Extension = ".ts";
			res.Mappings = mappings;
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- supplemental.go -----------------------------------------------------------

// supplementalHandler — supplemental.go:10.
struct supplementalHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			auto [mappedOutput, err] = identityMappedOutput(p.Content);
			if (err) {
				return {json::Value{}, err};
			}
			cm::TransformResult res;
			res.Text = "export {};";
			res.Extension = ".ts";
			cm::SupplementalOutput sup;
			static_cast<cm::MappedOutput&>(sup) = (mappedOutput);
			res.Supplemental = {sup};
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- supplemental_diagnostics.go ------------------------------------------------

// supplementalDiagnosticsHandler — supplemental_diagnostics.go:12.
struct supplementalDiagnosticsHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			constexpr std::string_view prefix =
			    "missingSupplementalGlobal;\n";
			auto [mappings, err] =
			    spanmap::Marshal(spanmap::New({spanmap::Segment{
			        TextPos(prefix.size()),
			        TextPos(prefix.size() + p.Content.size()),
			        0,
			        TextPos(p.Content.size()),
			        spanmap::KindVerbatim,
			        spanmap::FeatureAll,
			    }}));
			if (err.has_value()) {
				return {json::Value{}, gostd::newError(*err)};
			}
			cm::TransformResult res;
			res.Text = "export {};";
			res.Extension = ".ts";
			cm::SupplementalOutput sup;
			sup.Text = std::string(prefix) + p.Content;
			sup.Extension = ".ts";
			sup.Mappings = json::Value(mappings);
			res.Supplemental = {sup};
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- supplemental_globals.go -----------------------------------------------------

// supplementalGlobalsHandler — supplemental_globals.go:11.
struct supplementalGlobalsHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			std::string supplemental;
			if (p.FileName.ends_with("/a.vue")) {
				supplemental =
				    "/// <reference path=\"./extra.d.ts\" />\n"
				    "interface Shared extends Extra { value: string }";
			} else if (p.FileName.ends_with("/b.vue")) {
				supplemental = "declare const shared: Shared;";
			} else {
				return {json::Value{},
				        gostd::errorf(
				            "contentmappertest: unexpected supplemental "
				            "global input %q",
				            {p.FileName})};
			}
			cm::TransformResult res;
			res.Text = "export default shared.value;";
			res.Extension = ".ts";
			cm::SupplementalOutput sup;
			sup.Text = supplemental;
			sup.Extension = ".ts";
			res.Supplemental = {sup};
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- supplemental_module.go ------------------------------------------------------

// supplementalModuleHandler — supplemental_module.go:10.
struct supplementalModuleHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			cm::TransformResult res;
			res.Text = "export default 1;";
			res.Extension = ".ts";
			cm::SupplementalOutput sup;
			sup.Text =
			    "export const privateValue: number = \"wrong\";";
			sup.Extension = ".ts";
			res.Supplemental = {sup};
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- duplicate_projection.go -------------------------------------------------------

// duplicateProjectionHandler — duplicate_projection.go:13.
struct duplicateProjectionHandler : ipc::Handler {
	std::pair<json::Value, gostd::Error> HandleRequest(
	    gostd::Context, std::string_view method,
	    const json::Value& params) override {
		if (method == cm::MethodInitialize) {
			return {marshalInitializeResult(initializeResult("mapper")),
			        nullptr};
		}
		if (method == cm::MethodTransform) {
			cm::TransformParams p;
			if (auto err = unmarshalTransformParams(params, &p)) {
				return {json::Value{}, err};
			}
			auto [canonical, err] = identityMappedOutput(p.Content);
			if (err) {
				return {json::Value{}, err};
			}
			auto [supplemental, serr] = identityMappedOutput(p.Content);
			if (serr) {
				return {json::Value{}, serr};
			}
			cm::TransformResult res;
			static_cast<cm::MappedOutput&>(res) = (canonical);
			cm::SupplementalOutput sup;
			static_cast<cm::MappedOutput&>(sup) = (supplemental);
			res.Supplemental = {sup};
			return {marshalTransformResult(res), nullptr};
		}
		return {json::Value{},
		        gostd::errorf("contentmappertest: unexpected method %q",
		                      {method})};
	}

	gostd::Error HandleNotification(gostd::Context, std::string_view,
	                                const json::Value&) override {
		return nullptr;
	}
};

// --- registry.go ------------------------------------------------------------------

using handlerConstructor =
    std::function<std::shared_ptr<ipc::Handler>(ProjectLifecycle*)>;

const std::unordered_map<std::string_view, handlerConstructor>&
mapperHandlers() {
	static const std::unordered_map<std::string_view, handlerConstructor>
	    handlers = {
	        {TransformingMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<Handler>());
	         }},
	        {VerbatimMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<verbatimHandler>());
	         }},
	        {ModuleVerbatimMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<moduleVerbatimHandler>());
	         }},
	        {DynamicVerbatimMapper,
	         [](ProjectLifecycle* lifecycle) {
		         auto h = std::make_shared<dynamicVerbatimHandler>();
		         h->lifecycle = lifecycle;
		         return std::shared_ptr<ipc::Handler>(h);
	         }},
	        {DiagnosticCodeCollisionMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<diagnosticCodeCollisionHandler>());
	         }},
	        {FailingMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<failingHandler>());
	         }},
	        {SynthesizingMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<synthesizingHandler>());
	         }},
	        {ComponentMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<componentHandler>());
	         }},
	        {DuplicateMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<duplicateHandler>());
	         }},
	        {LispMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<lispHandler>());
	         }},
	        {SupplementalMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<supplementalHandler>());
	         }},
	        {SupplementalDiagnosticsMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<supplementalDiagnosticsHandler>());
	         }},
	        {SupplementalGlobalsMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<supplementalGlobalsHandler>());
	         }},
	        {SupplementalModuleMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<supplementalModuleHandler>());
	         }},
	        {PrefixedSupplementalMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<prefixedSupplementalHandler>());
	         }},
	        {UnmappedFoldingMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<unmappedFoldingHandler>());
	         }},
	        {HoistingMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<hoistingHandler>());
	         }},
	        {DuplicateProjectionMapper,
	         [](ProjectLifecycle*) {
		         return std::shared_ptr<ipc::Handler>(
		             std::make_shared<duplicateProjectionHandler>());
	         }},
	    };
	return handlers;
}

// handlerForMapper — registry.go:53.
std::pair<std::shared_ptr<ipc::Handler>, gostd::Error> handlerForMapper(
    const std::vector<std::string>& command, ProjectLifecycle* lifecycle) {
	if (command.empty()) {
		return {nullptr,
		        gostd::newError("contentmappertest: empty mapper command")};
	}
	auto it = mapperHandlers().find(command[0]);
	if (it == mapperHandlers().end()) {
		return {nullptr,
		        gostd::errorf("contentmappertest: unknown mapper command %s",
		                      {command[0]})};
	}
	return {it->second(lifecycle), nullptr};
}

// --- net.Pipe — spawner.go's in-process transport ---------------------------------

// pipeShared is one direction of a net.Pipe-like connection: synchronous and
// unbuffered — a writer blocks until the peer's reads have consumed every
// byte, matching net.Pipe's observable semantics.
struct pipeShared {
	std::mutex mu;
	std::condition_variable cv;
	std::string buf;
	size_t pos = 0;
	bool active = false;
	bool closed = false;
};

struct pipeEnd : gostd::io::ReadWriteCloser {
	std::shared_ptr<pipeShared> in;   // peer writes, we read
	std::shared_ptr<pipeShared> out;  // we write, peer reads

	std::pair<int, gostd::Error> read(std::span<char> dst) override {
		std::unique_lock<std::mutex> lk(in->mu);
		in->cv.wait(lk, [&] { return in->active || in->closed; });
		if (in->closed && !in->active) {
			return {0, gostd::newError("io: read/write on closed pipe")};
		}
		size_t n = std::min(dst.size(), in->buf.size() - in->pos);
		std::memcpy(dst.data(), in->buf.data() + in->pos, n);
		in->pos += n;
		if (in->pos == in->buf.size()) {
			in->active = false;
			in->buf.clear();
			in->pos = 0;
			in->cv.notify_all();
		}
		return {(int)n, nullptr};
	}

	std::pair<int, gostd::Error> write(std::string_view data) override {
		if (data.empty()) {
			return {0, nullptr};
		}
		std::unique_lock<std::mutex> lk(out->mu);
		out->cv.wait(lk, [&] { return !out->active || out->closed; });
		if (out->closed) {
			return {0, gostd::newError("io: read/write on closed pipe")};
		}
		out->buf = std::string(data);
		out->pos = 0;
		out->active = true;
		lk.unlock();
		out->cv.notify_all();
		lk.lock();
		out->cv.wait(lk, [&] { return !out->active || out->closed; });
		if (out->closed && out->active) {
			return {0, gostd::newError("io: read/write on closed pipe")};
		}
		return {(int)data.size(), nullptr};
	}

	gostd::Error close() override {
		{
			std::lock_guard<std::mutex> lk(in->mu);
			in->closed = true;
		}
		in->cv.notify_all();
		{
			std::lock_guard<std::mutex> lk(out->mu);
			out->closed = true;
		}
		out->cv.notify_all();
		return nullptr;
	}
};

std::pair<std::shared_ptr<pipeEnd>, std::shared_ptr<pipeEnd>> pipePair() {
	auto a2b = std::make_shared<pipeShared>();
	auto b2a = std::make_shared<pipeShared>();
	auto a = std::make_shared<pipeEnd>();
	a->in = b2a;
	a->out = a2b;
	auto b = std::make_shared<pipeEnd>();
	b->in = a2b;
	b->out = b2a;
	return {a, b};
}

// spawner — spawner.go:30.
struct spawner : cm::Spawner {
	ProjectLifecycle* lifecycle = nullptr;

	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Spawn(const std::vector<std::string>& command, const std::string& dir,
	      gostd::io::Writer* stderr) override {
		(void)dir;
		(void)stderr;
		auto handlerErr = handlerForMapper(command, lifecycle);
		auto handler = handlerErr.first;
		auto err = handlerErr.second;
		if (err) {
			return {nullptr, err};
		}
		if (command[0] != DynamicVerbatimMapper) {
			auto wrapped = std::make_shared<staticProjectHandler>();
			wrapped->inner = handler;
			handler = wrapped;
		}
		auto pair_ = pipePair();
		auto client = pair_.first;
		auto server = pair_.second;
		std::thread([server, handler] {
			(void)ipc::NewAsyncConn(server, handler)
			    ->Run(gostd::contextBackground());
		}).detach();
		return {std::static_pointer_cast<gostd::io::ReadWriteCloser>(client),
		        nullptr};
	}
};

}  // namespace

// Serve — spawner.go:15.
gostd::Error Serve(
    gostd::Context ctx,
    std::shared_ptr<gostd::io::ReadWriteCloser> rwc) {
	auto wrapped = std::make_shared<staticProjectHandler>();
	wrapped->inner = std::make_shared<Handler>();
	return ipc::NewAsyncConn(rwc, wrapped)->Run(ctx);
}

// NewSpawner — spawner.go:20.
cm::Spawner* NewSpawner() { return new spawner(); }

// NewSpawnerWithProjectLifecycle — spawner.go:25.
cm::Spawner* NewSpawnerWithProjectLifecycle(ProjectLifecycle* lifecycle) {
	auto* s = new spawner();
	s->lifecycle = lifecycle;
	return s;
}

// PackageJSON — manifest.go:8.
std::string PackageJSON(std::string_view mapper) {
	std::string compilerOptions;
	std::string dynamicConfig;
	if (mapper == TransformingMapper) {
		compilerOptions = ", \"compilerOptions\": [\"target\", \"jsx\"]";
	}
	if (mapper == DynamicVerbatimMapper) {
		dynamicConfig = ", \"dynamicConfig\": true";
	}
	return std::string("{\n\t\"name\": ") + json::marshalString(PackageName) +
	       ",\n\t\"version\": \"1.0.0\",\n\t\"typescript\": { \"contentMapper\": { \"exec\": [" +
	       json::marshalString(mapper) + "]" + compilerOptions +
	       dynamicConfig + " } }\n}";
}

}  // namespace tsc::testutil::contentmappertest
