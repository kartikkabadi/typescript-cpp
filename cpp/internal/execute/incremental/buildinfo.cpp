// Port of tsc/internal/execute/incremental/buildInfo.go — the serialized
// BuildInfo data model (with each type's custom MarshalJSON/UnmarshalJSON
// form) plus the Go encoding/json/v2 writer/parser that backs
// marshalBuildInfo/unmarshalBuildInfo.
#include <charconv>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

#include "internal/checker/checker.h"
#include "internal/core/version.h"
#include "internal/execute/incremental/incremental.h"

namespace tsc::execute::incremental {

// ===========================================================================
// Go encoding/json/v2 writer — field order is declaration order and each
// `,omitzero` field is emitted only when non-zero, matching the Go struct
// tags. Strings are escaped like jsontext's defaults: HTML escape (<, >, &)
// and JS escape (, ) plus the usual control escapes; invalid
// UTF-8 becomes (the package's allowInvalid option).
// ===========================================================================
namespace {

void writeJsonString(std::string& out, std::string_view s) {
	static const char* hex = "0123456789abcdef";
	out += '"';
	size_t i = 0;
	while (i < s.size()) {
		uint8_t c = static_cast<uint8_t>(s[i]);
		// Fast path: printable ASCII except " and \.
		if (c >= 0x20 && c <= 0x7e && c != '"' && c != '\\' && c != '<' &&
		    c != '>' && c != '&') {
			out += static_cast<char>(c);
			i++;
			continue;
		}
		switch (c) {
		case '"':
			out += "\\\"";
			i++;
			continue;
		case '\\':
			out += "\\\\";
			i++;
			continue;
		case '\n':
			out += "\\n";
			i++;
			continue;
		case '\r':
			out += "\\r";
			i++;
			continue;
		case '\t':
			out += "\\t";
			i++;
			continue;
		case '<':
			out += "\\u003c";
			i++;
			continue;
		case '>':
			out += "\\u003e";
			i++;
			continue;
		case '&':
			out += "\\u0026";
			i++;
			continue;
		default:
			break;
		}
		if (c < 0x20) {
			out += "\\u00";
			out += hex[c >> 4];
			out += hex[c & 0xf];
			i++;
			continue;
		}
		if (c < 0x80) { // 0x7f
			out += static_cast<char>(c);
			i++;
			continue;
		}
		// Multi-byte UTF-8: validate sequence; invalid bytes ->.
		uint32_t rune = 0;
		int n = 0;
		if ((c & 0xe0) == 0xc0) {
			rune = c & 0x1f;
			n = 2;
		} else if ((c & 0xf0) == 0xe0) {
			rune = c & 0x0f;
			n = 3;
		} else if ((c & 0xf8) == 0xf0) {
			rune = c & 0x07;
			n = 4;
		} else {
			out += "\\ufffd";
			i++;
			continue;
		}
		bool valid = i + n <= s.size();
		if (valid) {
			for (int k = 1; k < n; k++) {
				uint8_t cc = static_cast<uint8_t>(s[i + k]);
				if ((cc & 0xc0) != 0x80) {
					valid = false;
					break;
				}
				rune = (rune << 6) | (cc & 0x3f);
			}
		}
		// Reject overlong/surrogate/out-of-range like Go's decoder.
		uint32_t minv = n == 2 ? 0x80 : n == 3 ? 0x800 : 0x10000;
		if (rune < minv || rune > 0x10ffff ||
		    (rune >= 0xd800 && rune <= 0xdfff)) {
			valid = false;
		}
		if (!valid) {
			out += "\\ufffd";
			i++;
			continue;
		}
		if (rune == 0x2028) {
			out += "\\u2028";
			i += 3;
			continue;
		}
		if (rune == 0x2029) {
			out += "\\u2029";
			i += 3;
			continue;
		}
		out.append(s, i, n);
		i += n;
	}
	out += '"';
}

void writeJsonInt(std::string& out, int64_t v) {
	char buf[24];
	auto r = std::to_chars(buf, buf + sizeof(buf), v);
	out.append(buf, r.ptr);
}

void writeJsonDouble(std::string& out, double v) {
	// Shortest round-trip like Go's float marshal.
	char buf[32];
	for (int prec : {15, 16, 17}) {
		std::snprintf(buf, sizeof(buf), "%.*g", prec, v);
		if (std::strtod(buf, nullptr) == v) break;
	}
	out += buf;
}

void writeJsonBool(std::string& out, bool v) { out += v ? "true" : "false"; }

void writeJsonValue(std::string& out,
                    const tsoptions::CompilerOptionsValue& v) {
	using tsoptions::JsonArray;
	using tsoptions::JsonObjectPtr;
	using tsoptions::JsonStrList;
	if (v.isNil()) {
		out += "null";
	} else if (auto* b = v.get<bool>()) {
		writeJsonBool(out, *b);
	} else if (auto* t = v.get<Tristate>()) {
		// Tristate.MarshalJSON — tristate.go:54.
		switch (*t) {
		case Tristate::True:
			out += "true";
			break;
		case Tristate::False:
			out += "false";
			break;
		default:
			out += "null";
			break;
		}
	} else if (auto* i = v.get<int64_t>()) {
		writeJsonInt(out, *i);
	} else if (auto* d = v.get<double>()) {
		writeJsonDouble(out, *d);
	} else if (auto* s = v.get<std::string>()) {
		writeJsonString(out, *s);
	} else if (auto* sl = v.get<JsonStrList>()) {
		out += '[';
		for (size_t i = 0; i < sl->size(); i++) {
			if (i) out += ',';
			writeJsonString(out, (*sl)[i]);
		}
		out += ']';
	} else if (auto* arr = v.get<JsonArray>()) {
		out += '[';
		for (size_t i = 0; i < arr->size(); i++) {
			if (i) out += ',';
			writeJsonValue(out, (*arr)[i]);
		}
		out += ']';
	} else if (auto* obj = v.get<JsonObjectPtr>()) {
		out += '{';
		bool first = true;
		if (*obj) {
			for (const auto& key : (*obj)->Keys()) {
				if (!first) out += ',';
				first = false;
				writeJsonString(out, key);
				out += ':';
				writeJsonValue(out, *(*obj)->Get(key).first);
			}
		}
		out += '}';
	} else {
		// DiagnosticMessage / JsonGoMap arms never appear in build info.
		out += "null";
	}
}

void writeStringArray(std::string& out,
                      const std::vector<std::string>& v) {
	out += '[';
	for (size_t i = 0; i < v.size(); i++) {
		if (i) out += ',';
		writeJsonString(out, v[i]);
	}
	out += ']';
}

void writeBuildInfoRoot(std::string& out, const BuildInfoRoot* b) {
	// buildInfo.go:36 BuildInfoRoot.MarshalJSON.
	if (b->Start != 0) {
		if (b->End != 0) {
			out += '[';
			writeJsonInt(out, b->Start);
			out += ',';
			writeJsonInt(out, b->End);
			out += ']';
		} else {
			writeJsonInt(out, b->Start);
		}
	} else {
		writeJsonString(out, b->NonIncremental);
	}
}

void writeBuildInfoFileInfo(std::string& out, const BuildInfoFileInfo* b) {
	// buildInfo.go:148 BuildInfoFileInfo.MarshalJSON.
	if (!b->signature.empty()) {
		writeJsonString(out, b->signature);
		return;
	}
	if (b->noSignature != nullptr) {
		out += '{';
		bool wrote = false;
		auto field = [&](const char* name, bool emit,
		                 const std::function<void()>& w) {
			if (!emit) return;
			if (wrote) out += ',';
			wrote = true;
			writeJsonString(out, name);
			out += ':';
			w();
		};
		field("version", !b->noSignature->version.empty(), [&] {
			writeJsonString(out, b->noSignature->version);
		});
		field("noSignature", b->noSignature->noSignature, [&] {
			writeJsonBool(out, b->noSignature->noSignature);
		});
		field("affectsGlobalScope",
		      b->noSignature->affectsGlobalScope, [&] {
			writeJsonBool(out, b->noSignature->affectsGlobalScope);
		});
		field("impliedNodeFormat",
		      b->noSignature->impliedNodeFormat != ResolutionModeNone,
		      [&] {
			writeJsonInt(out, static_cast<int64_t>(
			                    b->noSignature->impliedNodeFormat));
		});
		out += '}';
		return;
	}
	const auto* f = b->fileInfo;
	out += '{';
	bool wrote = false;
	auto field = [&](const char* name, bool emit,
	                 const std::function<void()>& w) {
		if (!emit) return;
		if (wrote) out += ',';
		wrote = true;
		writeJsonString(out, name);
		out += ':';
		w();
	};
	field("version", !f->version.empty(),
	      [&] { writeJsonString(out, f->version); });
	field("signature", !f->signature.empty(),
	      [&] { writeJsonString(out, f->signature); });
	field("affectsGlobalScope", f->affectsGlobalScope,
	      [&] { writeJsonBool(out, f->affectsGlobalScope); });
	field("impliedNodeFormat", f->impliedNodeFormat != ResolutionModeNone, [&] {
		writeJsonInt(out,
		             static_cast<int64_t>(f->impliedNodeFormat));
	});
	out += '}';
}

void writeRepopulateInfo(std::string& out,
                         const BuildInfoRepopulateInfo* info) {
	// buildInfo.go:225 — Kind is NOT omitzero in the Go struct tag.
	out += '{';
	writeJsonString(out, "kind");
	out += ':';
	writeJsonInt(out, static_cast<int64_t>(info->Kind));
	if (!info->ModuleReference.empty()) {
		out += ',';
		writeJsonString(out, "moduleReference");
		out += ':';
		writeJsonString(out, info->ModuleReference);
	}
	if (info->Mode != ResolutionModeNone) {
		out += ',';
		writeJsonString(out, "mode");
		out += ':';
		writeJsonInt(out, static_cast<int64_t>(info->Mode));
	}
	if (!info->PackageName.empty()) {
		out += ',';
		writeJsonString(out, "packageName");
		out += ':';
		writeJsonString(out, info->PackageName);
	}
	out += '}';
}

void writeBuildInfoDiagnostic(std::string& out,
                              const BuildInfoDiagnostic* d);

void writeDiagnosticArray(std::string& out,
                          const std::vector<BuildInfoDiagnostic*>& v) {
	out += '[';
	for (size_t i = 0; i < v.size(); i++) {
		if (i) out += ',';
		writeBuildInfoDiagnostic(out, v[i]);
	}
	out += ']';
}

// buildInfo.go:204 — BuildInfoDiagnostic struct fields in Go order.
void writeBuildInfoDiagnostic(std::string& out,
                              const BuildInfoDiagnostic* d) {
	out += '{';
	bool wrote = false;
	auto field = [&](const char* name, bool emit,
	                 const std::function<void()>& w) {
		if (!emit) return;
		if (wrote) out += ',';
		wrote = true;
		writeJsonString(out, name);
		out += ':';
		w();
	};
	field("file", d->File != 0,
	      [&] { writeJsonInt(out, d->File); });
	field("noFile", d->NoFile,
	      [&] { writeJsonBool(out, d->NoFile); });
	field("pos", d->Pos != 0, [&] { writeJsonInt(out, d->Pos); });
	field("end", d->End != 0, [&] { writeJsonInt(out, d->End); });
	field("code", d->Code != 0, [&] { writeJsonInt(out, d->Code); });
	field("category", d->Category != static_cast<DiagnosticCategory>(0),
	      [&] {
		      writeJsonInt(out, static_cast<int64_t>(d->Category));
	      });
	field("source", !d->Source.empty(),
	      [&] { writeJsonString(out, d->Source); });
	field("messageText", !d->MessageText.empty(),
	      [&] { writeJsonString(out, d->MessageText); });
	field("messageKey", !d->MessageKey.empty(),
	      [&] { writeJsonString(out, d->MessageKey); });
	field("messageArgs", !d->MessageArgs.empty(), [&] {
		writeStringArray(out, d->MessageArgs);
	});
	field("messageChain", !d->MessageChain.empty(), [&] {
		writeDiagnosticArray(out, d->MessageChain);
	});
	field("relatedInformation", !d->RelatedInformation.empty(), [&] {
		writeDiagnosticArray(out, d->RelatedInformation);
	});
	field("reportsUnnecessary", d->ReportsUnnecessary,
	      [&] { writeJsonBool(out, d->ReportsUnnecessary); });
	field("reportsDeprecated", d->ReportsDeprecated,
	      [&] { writeJsonBool(out, d->ReportsDeprecated); });
	field("skippedOnNoEmit", d->SkippedOnNoEmit,
	      [&] { writeJsonBool(out, d->SkippedOnNoEmit); });
	field("repopulateInfo", d->RepopulateInfo != nullptr, [&] {
		writeRepopulateInfo(out, d->RepopulateInfo);
	});
	out += '}';
}

void writeReferenceMapEntry(std::string& out,
                            const BuildInfoReferenceMapEntry* e) {
	// buildInfo.go:182 — [2]int{fileId, fileIdListId}.
	out += '[';
	writeJsonInt(out, e->FileId);
	out += ',';
	writeJsonInt(out, e->FileIdListId);
	out += ']';
}

void writeDiagnosticsOfFile(std::string& out,
                            const BuildInfoDiagnosticsOfFile* b) {
	// buildInfo.go:233 — [fileId, diagnostics].
	out += '[';
	writeJsonInt(out, b->FileId);
	out += ',';
	writeDiagnosticArray(out, b->Diagnostics);
	out += ']';
}

void writeSemanticDiagnostic(std::string& out,
                             const BuildInfoSemanticDiagnostic* b) {
	// buildInfo.go:261 — fileId if nonzero else diagnostics tuple.
	if (b->FileId != 0) {
		writeJsonInt(out, b->FileId);
	} else {
		writeDiagnosticsOfFile(out, b->Diagnostics);
	}
}

void writeFilePendingEmit(std::string& out,
                          const BuildInfoFilePendingEmit* b) {
	// buildInfo.go:286.
	if (b->EmitKind == 0) {
		writeJsonInt(out, b->FileId);
	} else if (b->EmitKind == FileEmitKindDts) {
		out += '[';
		writeJsonInt(out, b->FileId);
		out += ']';
	} else {
		out += '[';
		writeJsonInt(out, b->FileId);
		out += ',';
		writeJsonInt(out, b->EmitKind);
		out += ']';
	}
}

void writeEmitSignature(std::string& out,
                        const BuildInfoEmitSignature* b) {
	// buildInfo.go:371.
	if (b->noEmitSignature()) {
		writeJsonInt(out, b->FileId);
		return;
	}
	out += '[';
	writeJsonInt(out, b->FileId);
	out += ',';
	if (b->DiffersOnlyInDtsMap) {
		out += "[]";
	} else if (b->DiffersInOptions) {
		out += '[';
		writeJsonString(out, b->Signature);
		out += ']';
	} else {
		writeJsonString(out, b->Signature);
	}
	out += ']';
}

void writeResolvedRoot(std::string& out,
                       const BuildInfoResolvedRoot* b) {
	// buildInfo.go:443 — [2]BuildInfoFileId{resolved, root}.
	out += '[';
	writeJsonInt(out, b->Resolved);
	out += ',';
	writeJsonInt(out, b->Root);
	out += ']';
}

}  // namespace

// json.Marshal(buildInfo) — buildInfo.go BuildInfo field order, every field
// ,omitzero.
std::string marshalBuildInfo(const BuildInfo* b) {
	std::string out;
	out += '{';
	bool wrote = false;
	auto field = [&](const char* name, bool emit,
	                 const std::function<void()>& w) {
		if (!emit) return;
		if (wrote) out += ',';
		wrote = true;
		writeJsonString(out, name);
		out += ':';
		w();
	};
	field("version", !b->Version.empty(),
	      [&] { writeJsonString(out, b->Version); });
	field("errors", b->Errors, [&] { writeJsonBool(out, b->Errors); });
	field("checkPending", b->CheckPending,
	      [&] { writeJsonBool(out, b->CheckPending); });
	field("root", !b->Root.empty(), [&] {
		out += '[';
		for (size_t i = 0; i < b->Root.size(); i++) {
			if (i) out += ',';
			writeBuildInfoRoot(out, b->Root[i]);
		}
		out += ']';
	});
	field("packageJsons", !b->PackageJsons.empty(), [&] {
		writeStringArray(out, b->PackageJsons);
	});
	field("missingPackageJsons", !b->MissingPackageJsons.empty(), [&] {
		writeStringArray(out, b->MissingPackageJsons);
	});
	field("contentMapperIdentities", !b->ContentMapperIdentities.empty(),
	      [&] { writeStringArray(out, b->ContentMapperIdentities); });
	field("fileNames", !b->FileNames.empty(),
	      [&] { writeStringArray(out, b->FileNames); });
	field("fileInfos", !b->FileInfos.empty(), [&] {
		out += '[';
		for (size_t i = 0; i < b->FileInfos.size(); i++) {
			if (i) out += ',';
			writeBuildInfoFileInfo(out, b->FileInfos[i]);
		}
		out += ']';
	});
	field("fileIdsList", !b->FileIdsList.empty(), [&] {
		out += '[';
		for (size_t i = 0; i < b->FileIdsList.size(); i++) {
			if (i) out += ',';
			out += '[';
			for (size_t j = 0; j < b->FileIdsList[i].size(); j++) {
				if (j) out += ',';
				writeJsonInt(out, b->FileIdsList[i][j]);
			}
			out += ']';
		}
		out += ']';
	});
	field("options", b->Options != nullptr, [&] {
		out += '{';
		bool first = true;
		for (const auto& key : b->Options->Keys()) {
			if (!first) out += ',';
			first = false;
			writeJsonString(out, key);
			out += ':';
			writeJsonValue(out, *b->Options->Get(key).first);
		}
		out += '}';
	});
	field("referencedMap", !b->ReferencedMap.empty(), [&] {
		out += '[';
		for (size_t i = 0; i < b->ReferencedMap.size(); i++) {
			if (i) out += ',';
			writeReferenceMapEntry(out, b->ReferencedMap[i]);
		}
		out += ']';
	});
	field("semanticDiagnosticsPerFile",
	      !b->SemanticDiagnosticsPerFile.empty(), [&] {
		      out += '[';
		      for (size_t i = 0; i < b->SemanticDiagnosticsPerFile.size();
		           i++) {
			      if (i) out += ',';
			      writeSemanticDiagnostic(
			          out, b->SemanticDiagnosticsPerFile[i]);
		      }
		      out += ']';
	      });
	field("emitDiagnosticsPerFile", !b->EmitDiagnosticsPerFile.empty(),
	      [&] {
		      out += '[';
		      for (size_t i = 0; i < b->EmitDiagnosticsPerFile.size();
		           i++) {
			      if (i) out += ',';
			      writeDiagnosticsOfFile(out,
			                             b->EmitDiagnosticsPerFile[i]);
		      }
		      out += ']';
	      });
	field("changeFileSet", !b->ChangeFileSet.empty(), [&] {
		out += '[';
		for (size_t i = 0; i < b->ChangeFileSet.size(); i++) {
			if (i) out += ',';
			writeJsonInt(out, b->ChangeFileSet[i]);
		}
		out += ']';
	});
	field("affectedFilesPendingEmit",
	      !b->AffectedFilesPendingEmit.empty(), [&] {
		      out += '[';
		      for (size_t i = 0; i < b->AffectedFilesPendingEmit.size();
		           i++) {
			      if (i) out += ',';
			      writeFilePendingEmit(
			          out, b->AffectedFilesPendingEmit[i]);
		      }
		      out += ']';
	      });
	field("latestChangedDtsFile", !b->LatestChangedDtsFile.empty(), [&] {
		writeJsonString(out, b->LatestChangedDtsFile);
	});
	field("emitSignatures", !b->EmitSignatures.empty(), [&] {
		out += '[';
		for (size_t i = 0; i < b->EmitSignatures.size(); i++) {
			if (i) out += ',';
			writeEmitSignature(out, b->EmitSignatures[i]);
		}
		out += ']';
	});
	field("resolvedRoot", !b->ResolvedRoot.empty(), [&] {
		out += '[';
		for (size_t i = 0; i < b->ResolvedRoot.size(); i++) {
			if (i) out += ',';
			writeResolvedRoot(out, b->ResolvedRoot[i]);
		}
		out += ']';
	});
	field("semanticErrors", b->SemanticErrors,
	      [&] { writeJsonBool(out, b->SemanticErrors); });
	out += '}';
	return out;
}

// ===========================================================================
// Recursive-descent JSON parser producing CompilerOptionsValue trees
// (Go `any`): null→monostate, true/false→bool, numbers→int64 (or double
// when non-integral), strings→std::string, arrays→JsonArray,
// objects→JsonObjectPtr. All the typed decoders below consume that tree.
// ===========================================================================
namespace {

struct JsonParser {
	std::string_view text;
	size_t pos = 0;
	std::string error;

	bool fail(std::string_view msg) {
		if (error.empty()) error = std::string(msg);
		return false;
	}
	char peek() const {
		return pos < text.size() ? text[pos] : '\0';
	}
	void skipWs() {
		while (pos < text.size() &&
		       (text[pos] == ' ' || text[pos] == '\t' ||
		        text[pos] == '\n' || text[pos] == '\r')) {
			pos++;
		}
	}
	bool consume(std::string_view lit) {
		if (text.substr(pos, lit.size()) == lit) {
			pos += lit.size();
			return true;
		}
		return false;
	}

	bool parseValue(tsoptions::CompilerOptionsValue& out) {
		skipWs();
		switch (peek()) {
		case 'n':
			if (consume("null")) {
				out = tsoptions::CompilerOptionsValue(std::monostate{});
				return true;
			}
			return fail("invalid null");
		case 't':
			if (consume("true")) {
				out = tsoptions::CompilerOptionsValue(true);
				return true;
			}
			return fail("invalid true");
		case 'f':
			if (consume("false")) {
				out = tsoptions::CompilerOptionsValue(false);
				return true;
			}
			return fail("invalid false");
		case '"': {
			std::string s;
			if (!parseString(s)) return false;
			out = tsoptions::CompilerOptionsValue(std::move(s));
			return true;
		}
		case '[': {
			pos++;
			tsoptions::JsonArray arr;
			skipWs();
			if (peek() == ']') {
				pos++;
				out = tsoptions::CompilerOptionsValue(std::move(arr));
				return true;
			}
			while (true) {
				tsoptions::CompilerOptionsValue elem;
				if (!parseValue(elem)) return false;
				arr.push_back(std::move(elem));
				skipWs();
				if (peek() == ',') {
					pos++;
					continue;
				}
				if (peek() == ']') {
					pos++;
					break;
				}
				return fail("expected , or ] in array");
			}
			out = tsoptions::CompilerOptionsValue(std::move(arr));
			return true;
		}
		case '{': {
			pos++;
			auto obj = std::make_shared<tsoptions::JsonObject>();
			skipWs();
			if (peek() == '}') {
				pos++;
				out = tsoptions::CompilerOptionsValue(
				    tsoptions::JsonObjectPtr(std::move(obj)));
				return true;
			}
			while (true) {
				skipWs();
				std::string key;
				if (!parseString(key)) return false;
				skipWs();
				if (peek() != ':') return fail("expected : in object");
				pos++;
				tsoptions::CompilerOptionsValue val;
				if (!parseValue(val)) return false;
				obj->Set(std::move(key), std::move(val));
				skipWs();
				if (peek() == ',') {
					pos++;
					continue;
				}
				if (peek() == '}') {
					pos++;
					break;
				}
				return fail("expected , or } in object");
			}
			out = tsoptions::CompilerOptionsValue(
			    tsoptions::JsonObjectPtr(std::move(obj)));
			return true;
		}
		default:
			if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
				return parseNumber(out);
			}
			return fail("invalid JSON value");
		}
	}

	bool parseString(std::string& out) {
		if (peek() != '"') return fail("expected string");
		pos++;
		while (pos < text.size()) {
			char c = text[pos];
			if (c == '"') {
				pos++;
				return true;
			}
			if (c == '\\') {
				pos++;
				if (pos >= text.size()) return fail("bad escape");
				char e = text[pos];
				switch (e) {
				case '"':
					out += '"';
					pos++;
					continue;
				case '\\':
					out += '\\';
					pos++;
					continue;
				case '/':
					out += '/';
					pos++;
					continue;
				case 'b':
					out += '\b';
					pos++;
					continue;
				case 'f':
					out += '\f';
					pos++;
					continue;
				case 'n':
					out += '\n';
					pos++;
					continue;
				case 'r':
					out += '\r';
					pos++;
					continue;
				case 't':
					out += '\t';
					pos++;
					continue;
				case 'u': {
					pos++;
					uint32_t cp = parseHex4();
					if (cp == UINT32_MAX) return false;
					if (cp >= 0xd800 && cp <= 0xdbff &&
					    pos + 1 < text.size() && text[pos] == '\\' &&
					    text[pos + 1] == 'u') {
						pos += 2;
						uint32_t lo = parseHex4();
						if (lo == UINT32_MAX) return false;
						if (lo >= 0xdc00 && lo <= 0xdfff) {
							cp = 0x10000 + ((cp - 0xd800) << 10) +
							     (lo - 0xdc00);
						} else {
							// Unpaired surrogates -> two s.
							appendUtf8(out, 0xfffd);
							cp = lo;
						}
					}
					appendUtf8(out, cp);
					continue;
				}
				default:
					return fail("invalid escape");
				}
			}
			out += c;
			pos++;
		}
		return fail("unterminated string");
	}

	uint32_t parseHex4() {
		if (pos + 4 > text.size()) return UINT32_MAX;
		uint32_t v = 0;
		for (int i = 0; i < 4; i++) {
			char c = text[pos + i];
			v <<= 4;
			if (c >= '0' && c <= '9')
				v |= c - '0';
			else if (c >= 'a' && c <= 'f')
				v |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				v |= c - 'A' + 10;
			else
				return UINT32_MAX;
		}
		pos += 4;
		return v;
	}

	static void appendUtf8(std::string& out, uint32_t cp) {
		if (cp <= 0x7f) {
			out += static_cast<char>(cp);
		} else if (cp <= 0x7ff) {
			out += static_cast<char>(0xc0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3f));
		} else if (cp <= 0xffff) {
			out += static_cast<char>(0xe0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
			out += static_cast<char>(0x80 | (cp & 0x3f));
		} else {
			out += static_cast<char>(0xf0 | (cp >> 18));
			out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
			out += static_cast<char>(0x80 | (cp & 0x3f));
		}
	}

	bool parseNumber(tsoptions::CompilerOptionsValue& out) {
		size_t start = pos;
		if (peek() == '-') pos++;
		while (peek() >= '0' && peek() <= '9') pos++;
		bool isDouble = false;
		if (peek() == '.') {
			isDouble = true;
			pos++;
			while (peek() >= '0' && peek() <= '9') pos++;
		}
		if (peek() == 'e' || peek() == 'E') {
			isDouble = true;
			pos++;
			if (peek() == '+' || peek() == '-') pos++;
			while (peek() >= '0' && peek() <= '9') pos++;
		}
		if (pos == start) return fail("invalid number");
		auto numText = text.substr(start, pos - start);
		if (!isDouble) {
			int64_t v = 0;
			auto r = std::from_chars(numText.data(),
			                         numText.data() + numText.size(), v);
			if (r.ec == std::errc{} &&
			    r.ptr == numText.data() + numText.size()) {
				out = tsoptions::CompilerOptionsValue(v);
				return true;
			}
			isDouble = true; // overflowed int64 — fall through to double
		}
		char buf[64];
		if (numText.size() >= sizeof(buf)) return fail("bad number");
		std::memcpy(buf, numText.data(), numText.size());
		buf[numText.size()] = '\0';
		out = tsoptions::CompilerOptionsValue(std::strtod(buf, nullptr));
		return true;
	}
};

// ---- typed decoders over the parsed tree ----

bool asInt(const tsoptions::CompilerOptionsValue& v, int64_t& out) {
	if (auto* i = v.get<int64_t>()) {
		out = *i;
		return true;
	}
	if (auto* d = v.get<double>()) {
		out = static_cast<int64_t>(*d);
		return true;
	}
	return false;
}

bool asString(const tsoptions::CompilerOptionsValue& v,
              std::string& out) {
	if (auto* s = v.get<std::string>()) {
		out = *s;
		return true;
	}
	return false;
}

bool asBool(const tsoptions::CompilerOptionsValue& v, bool& out) {
	if (auto* b = v.get<bool>()) {
		out = *b;
		return true;
	}
	return false;
}

bool field(const tsoptions::CompilerOptionsValue& v, const char* name,
           tsoptions::CompilerOptionsValue& out) {
	auto* obj = v.get<tsoptions::JsonObjectPtr>();
	if (obj == nullptr || !*obj) return false;
	auto [p, ok] = (*obj)->Get(name);
	if (!ok) return false;
	out = *p;
	return true;
}

bool parseBuildInfoDiagnostic(const tsoptions::CompilerOptionsValue& v,
                              BuildInfoDiagnostic* d);
bool parseRepopulateInfo(const tsoptions::CompilerOptionsValue& v,
                         BuildInfoRepopulateInfo* info) {
	int64_t kind = 0;
	tsoptions::CompilerOptionsValue f;
	if (field(v, "kind", f)) {
		if (!asInt(f, kind)) return false;
	}
	info->Kind = static_cast<RepopulateDiagnosticKind>(kind);
	if (field(v, "moduleReference", f)) {
		if (!asString(f, info->ModuleReference)) return false;
	}
	if (field(v, "mode", f)) {
		int64_t mode = 0;
		if (!asInt(f, mode)) return false;
		info->Mode = static_cast<ResolutionMode>(mode);
	}
	if (field(v, "packageName", f)) {
		if (!asString(f, info->PackageName)) return false;
	}
	return true;
}

bool parseDiagnosticArray(const tsoptions::CompilerOptionsValue& v,
                          std::vector<BuildInfoDiagnostic*>& out) {
	auto* arr = v.get<tsoptions::JsonArray>();
	if (arr == nullptr) return false;
	for (const auto& elem : *arr) {
		auto* d = new BuildInfoDiagnostic();
		if (!parseBuildInfoDiagnostic(elem, d)) return false;
		out.push_back(d);
	}
	return true;
}

bool parseStringArrayValue(const tsoptions::CompilerOptionsValue& v,
                           std::vector<std::string>& out) {
	auto* arr = v.get<tsoptions::JsonArray>();
	if (arr == nullptr) return false;
	for (const auto& elem : *arr) {
		std::string s;
		if (!asString(elem, s)) return false;
		out.push_back(s);
	}
	return true;
}

bool parseBuildInfoDiagnostic(const tsoptions::CompilerOptionsValue& v,
                              BuildInfoDiagnostic* d) {
	if (v.get<tsoptions::JsonObjectPtr>() == nullptr) return false;
	tsoptions::CompilerOptionsValue f;
	if (field(v, "file", f)) {
		int64_t n = 0;
		if (!asInt(f, n)) return false;
		d->File = static_cast<BuildInfoFileId>(n);
	}
	if (field(v, "noFile", f)) {
		if (!asBool(f, d->NoFile)) return false;
	}
	if (field(v, "pos", f)) {
		int64_t n = 0;
		if (!asInt(f, n)) return false;
		d->Pos = static_cast<int32_t>(n);
	}
	if (field(v, "end", f)) {
		int64_t n = 0;
		if (!asInt(f, n)) return false;
		d->End = static_cast<int32_t>(n);
	}
	if (field(v, "code", f)) {
		int64_t n = 0;
		if (!asInt(f, n)) return false;
		d->Code = static_cast<int32_t>(n);
	}
	if (field(v, "category", f)) {
		int64_t n = 0;
		if (!asInt(f, n)) return false;
		d->Category = static_cast<DiagnosticCategory>(n);
	}
	if (field(v, "source", f)) {
		if (!asString(f, d->Source)) return false;
	}
	if (field(v, "messageText", f)) {
		if (!asString(f, d->MessageText)) return false;
	}
	if (field(v, "messageKey", f)) {
		if (!asString(f, d->MessageKey)) return false;
	}
	if (field(v, "messageArgs", f)) {
		if (!parseStringArrayValue(f, d->MessageArgs)) return false;
	}
	if (field(v, "messageChain", f)) {
		if (!parseDiagnosticArray(f, d->MessageChain)) return false;
	}
	if (field(v, "relatedInformation", f)) {
		if (!parseDiagnosticArray(f, d->RelatedInformation)) {
			return false;
		}
	}
	if (field(v, "reportsUnnecessary", f)) {
		if (!asBool(f, d->ReportsUnnecessary)) return false;
	}
	if (field(v, "reportsDeprecated", f)) {
		if (!asBool(f, d->ReportsDeprecated)) return false;
	}
	if (field(v, "skippedOnNoEmit", f)) {
		if (!asBool(f, d->SkippedOnNoEmit)) return false;
	}
	if (field(v, "repopulateInfo", f)) {
		auto* info = new BuildInfoRepopulateInfo();
		if (!parseRepopulateInfo(f, info)) return false;
		d->RepopulateInfo = info;
	}
	return true;
}

// buildInfo.go:47 — UnmarshalJSON: [2]int | int | string.
bool parseBuildInfoRoot(const tsoptions::CompilerOptionsValue& v,
                        BuildInfoRoot* out, std::string& err) {
	if (auto* arr = v.get<tsoptions::JsonArray>()) {
		if (arr->size() == 2) {
			int64_t a = 0, b = 0;
			if (asInt((*arr)[0], a) && asInt((*arr)[1], b)) {
				out->Start = static_cast<BuildInfoFileId>(a);
				out->End = static_cast<BuildInfoFileId>(b);
				return true;
			}
		}
		// A non-2 element array fails the [2]int decode then fails int
		// and string decodes in Go -> error overall.
		err = "invalid BuildInfoRoot";
		return false;
	}
	int64_t n = 0;
	if (asInt(v, n)) {
		out->Start = static_cast<BuildInfoFileId>(n);
		return true;
	}
	if (asString(v, out->NonIncremental)) {
		return true;
	}
	err = "invalid BuildInfoRoot";
	return false;
}

// buildInfo.go:158 — UnmarshalJSON: string | noSignature-object |
// fileInfo-object.
bool parseBuildInfoFileInfo(const tsoptions::CompilerOptionsValue& v,
                            BuildInfoFileInfo* out, std::string& err) {
	if (asString(v, out->signature)) {
		return true;
	}
	auto* obj = v.get<tsoptions::JsonObjectPtr>();
	if (obj == nullptr) {
		err = "invalid BuildInfoFileInfo";
		return false;
	}
	tsoptions::CompilerOptionsValue f;
	auto parseVersion = [&](std::string& version) -> bool {
		if (field(v, "version", f)) {
			return asString(f, version);
		}
		return true;
	};
	auto parseScopeAndFormat = [&](bool& affectsGlobalScope,
	                               ResolutionMode& implied) -> bool {
		if (field(v, "affectsGlobalScope", f)) {
			if (!asBool(f, affectsGlobalScope)) return false;
		}
		if (field(v, "impliedNodeFormat", f)) {
			int64_t m = 0;
			if (!asInt(f, m)) return false;
			implied = static_cast<ResolutionMode>(m);
		}
		return true;
	};
	// Go tries buildInfoFileInfoNoSignature first; it "succeeds" even
	// without the field but the port checks `!noSignature.NoSignature`
	// after a successful unmarshal — the field must parse as bool when
	// present.
	bool noSig = false;
	{
		tsoptions::CompilerOptionsValue ns;
		if (field(v, "noSignature", ns) && !asBool(ns, noSig)) {
			// Wrong type: Go's unmarshal fails -> falls to fileInfo arm.
			noSig = false;
		}
	}
	if (noSig) {
		auto* info = new buildInfoFileInfoNoSignature();
		if (!parseVersion(info->version) ||
		    !parseScopeAndFormat(info->affectsGlobalScope,
		                         info->impliedNodeFormat)) {
			err = "invalid BuildInfoFileInfo";
			return false;
		}
		info->noSignature = true;
		out->noSignature = info;
		return true;
	}
	auto* info = new buildInfoFileInfoWithSignature();
	if (!parseVersion(info->version) ||
	    !parseScopeAndFormat(info->affectsGlobalScope,
	                         info->impliedNodeFormat)) {
		err = "invalid BuildInfoFileInfo";
		return false;
	}
	if (field(v, "signature", f)) {
		if (!asString(f, info->signature)) {
			err = "invalid BuildInfoFileInfo";
			return false;
		}
	}
	out->fileInfo = info;
	return true;
}

// buildInfo.go:186 — [2]int.
bool parseReferenceMapEntry(const tsoptions::CompilerOptionsValue& v,
                            BuildInfoReferenceMapEntry* out) {
	auto* arr = v.get<tsoptions::JsonArray>();
	if (arr == nullptr || arr->size() != 2) return false;
	int64_t a = 0, b = 0;
	if (!asInt((*arr)[0], a) || !asInt((*arr)[1], b)) return false;
	out->FileId = static_cast<BuildInfoFileId>(a);
	out->FileIdListId = static_cast<BuildInfoFileIdListId>(b);
	return true;
}

// buildInfo.go:238 — [fileId, diagnostics].
bool parseDiagnosticsOfFile(const tsoptions::CompilerOptionsValue& v,
                            BuildInfoDiagnosticsOfFile* out) {
	auto* arr = v.get<tsoptions::JsonArray>();
	if (arr == nullptr || arr->size() != 2) return false;
	int64_t fid = 0;
	if (!asInt((*arr)[0], fid)) return false;
	out->FileId = static_cast<BuildInfoFileId>(fid);
	return parseDiagnosticArray((*arr)[1], out->Diagnostics);
}

// buildInfo.go:268 — fileId | diagnosticsOfFile.
bool parseSemanticDiagnostic(const tsoptions::CompilerOptionsValue& v,
                             BuildInfoSemanticDiagnostic* out) {
	int64_t fid = 0;
	if (asInt(v, fid)) {
		out->FileId = static_cast<BuildInfoFileId>(fid);
		return true;
	}
	auto* d = new BuildInfoDiagnosticsOfFile();
	if (!parseDiagnosticsOfFile(v, d)) return false;
	out->Diagnostics = d;
	return true;
}

// buildInfo.go:296 — fileId | [fileId] | [fileId, emitKind].
bool parseFilePendingEmit(const tsoptions::CompilerOptionsValue& v,
                          BuildInfoFilePendingEmit* out) {
	int64_t fid = 0;
	if (asInt(v, fid)) {
		out->FileId = static_cast<BuildInfoFileId>(fid);
		return true;
	}
	auto* arr = v.get<tsoptions::JsonArray>();
	if (arr == nullptr || arr->empty() || arr->size() > 2) return false;
	if (!asInt((*arr)[0], fid)) return false;
	out->FileId = static_cast<BuildInfoFileId>(fid);
	if (arr->size() == 1) {
		out->EmitKind = FileEmitKindDts;
	} else {
		int64_t kind = 0;
		if (!asInt((*arr)[1], kind)) return false;
		out->EmitKind = static_cast<FileEmitKind>(kind);
	}
	return true;
}

// buildInfo.go:385 — fileId | [fileId, signature|[]|[sig]].
bool parseEmitSignature(const tsoptions::CompilerOptionsValue& v,
                        BuildInfoEmitSignature* out) {
	int64_t fid = 0;
	if (asInt(v, fid)) {
		out->FileId = static_cast<BuildInfoFileId>(fid);
		return true;
	}
	auto* arr = v.get<tsoptions::JsonArray>();
	if (arr == nullptr || arr->size() != 2) return false;
	if (!asInt((*arr)[0], fid)) return false;
	out->FileId = static_cast<BuildInfoFileId>(fid);
	const auto& sigV = (*arr)[1];
	if (asString(sigV, out->Signature)) {
		return true;
	}
	auto* sigList = sigV.get<tsoptions::JsonArray>();
	if (sigList == nullptr) return false;
	switch (sigList->size()) {
	case 0:
		out->DiffersOnlyInDtsMap = true;
		return true;
	case 1:
		if (!asString((*sigList)[0], out->Signature)) return false;
		out->DiffersInOptions = true;
		return true;
	default:
		return false;
	}
}

// buildInfo.go:452 — [2]BuildInfoFileId.
bool parseResolvedRoot(const tsoptions::CompilerOptionsValue& v,
                       BuildInfoResolvedRoot* out) {
	auto* arr = v.get<tsoptions::JsonArray>();
	if (arr == nullptr || arr->size() != 2) return false;
	int64_t a = 0, b = 0;
	if (!asInt((*arr)[0], a) || !asInt((*arr)[1], b)) return false;
	out->Resolved = static_cast<BuildInfoFileId>(a);
	out->Root = static_cast<BuildInfoFileId>(b);
	return true;
}

}  // namespace

// json.Unmarshal(data, &buildInfo) — nullptr on parse failure.
BuildInfo* unmarshalBuildInfo(std::string_view data) {
	JsonParser parser{data};
	tsoptions::CompilerOptionsValue root;
	if (!parser.parseValue(root)) {
		return nullptr;
	}
	parser.skipWs();
	if (parser.pos != data.size()) {
		return nullptr;
	}
	if (root.get<tsoptions::JsonObjectPtr>() == nullptr) {
		return nullptr;
	}
	auto* b = new BuildInfo();
	tsoptions::CompilerOptionsValue f;
	if (field(root, "version", f)) {
		if (!asString(f, b->Version)) {
			return nullptr;
		}
	}
	if (field(root, "errors", f)) {
		if (!asBool(f, b->Errors)) return nullptr;
	}
	if (field(root, "checkPending", f)) {
		if (!asBool(f, b->CheckPending)) {
			return nullptr;
		}
	}
	if (field(root, "root", f)) {
		auto* arr = f.get<tsoptions::JsonArray>();
		if (arr == nullptr) return nullptr;
		for (const auto& elem : *arr) {
			auto* r = new BuildInfoRoot();
			std::string err;
			if (!parseBuildInfoRoot(elem, r, err)) {
				return nullptr;
			}
			b->Root.push_back(r);
		}
	}
	if (field(root, "packageJsons", f)) {
		if (!parseStringArrayValue(f, b->PackageJsons)) {
			return nullptr;
		}
	}
	if (field(root, "missingPackageJsons", f)) {
		if (!parseStringArrayValue(f, b->MissingPackageJsons)) {
			return nullptr;
		}
	}
	if (field(root, "contentMapperIdentities", f)) {
		if (!parseStringArrayValue(f, b->ContentMapperIdentities)) {
			return nullptr;
		}
	}
	if (field(root, "fileNames", f)) {
		if (!parseStringArrayValue(f, b->FileNames)) {
			return nullptr;
		}
	}
	if (field(root, "fileInfos", f)) {
		auto* arr = f.get<tsoptions::JsonArray>();
		if (arr == nullptr) return nullptr;
		for (const auto& elem : *arr) {
			auto* info = new BuildInfoFileInfo();
			std::string err;
			if (!parseBuildInfoFileInfo(elem, info, err)) {
				return nullptr;
			}
			b->FileInfos.push_back(info);
		}
	}
	if (field(root, "fileIdsList", f)) {
		auto* arr = f.get<tsoptions::JsonArray>();
		if (arr == nullptr) return nullptr;
		for (const auto& elem : *arr) {
			auto* ids = elem.get<tsoptions::JsonArray>();
			if (ids == nullptr) {
				return nullptr;
			}
			std::vector<BuildInfoFileId> list;
			for (const auto& id : *ids) {
				int64_t n = 0;
				if (!asInt(id, n)) {
					return nullptr;
				}
				list.push_back(static_cast<BuildInfoFileId>(n));
			}
			b->FileIdsList.push_back(std::move(list));
		}
	}
	if (field(root, "options", f)) {
		auto* obj = f.get<tsoptions::JsonObjectPtr>();
		if (obj == nullptr) return nullptr;
		b->Options = *obj;
	}
	if (field(root, "referencedMap", f)) {
		auto* arr = f.get<tsoptions::JsonArray>();
		if (arr == nullptr) return nullptr;
		for (const auto& elem : *arr) {
			auto* e = new BuildInfoReferenceMapEntry();
			if (!parseReferenceMapEntry(elem, e)) {
				return nullptr;
			}
			b->ReferencedMap.push_back(e);
		}
	}
	if (field(root, "semanticDiagnosticsPerFile", f)) {
		auto* arr = f.get<tsoptions::JsonArray>();
		if (arr == nullptr) {
			return nullptr;
		}
		for (const auto& elem : *arr) {
			auto* d = new BuildInfoSemanticDiagnostic();
			if (!parseSemanticDiagnostic(elem, d)) {
				return nullptr;
			}
			b->SemanticDiagnosticsPerFile.push_back(d);
		}
	}
	if (field(root, "emitDiagnosticsPerFile", f)) {
		auto* arr = f.get<tsoptions::JsonArray>();
		if (arr == nullptr) {
			return nullptr;
		}
		for (const auto& elem : *arr) {
			auto* d = new BuildInfoDiagnosticsOfFile();
			if (!parseDiagnosticsOfFile(elem, d)) {
				return nullptr;
			}
			b->EmitDiagnosticsPerFile.push_back(d);
		}
	}
	if (field(root, "changeFileSet", f)) {
		auto* arr = f.get<tsoptions::JsonArray>();
		if (arr == nullptr) return nullptr;
		for (const auto& elem : *arr) {
			int64_t n = 0;
			if (!asInt(elem, n)) {
				return nullptr;
			}
			b->ChangeFileSet.push_back(
			    static_cast<BuildInfoFileId>(n));
		}
	}
	if (field(root, "affectedFilesPendingEmit", f)) {
		auto* arr = f.get<tsoptions::JsonArray>();
		if (arr == nullptr) {
			return nullptr;
		}
		for (const auto& elem : *arr) {
			auto* e = new BuildInfoFilePendingEmit();
			if (!parseFilePendingEmit(elem, e)) {
				return nullptr;
			}
			b->AffectedFilesPendingEmit.push_back(e);
		}
	}
	if (field(root, "latestChangedDtsFile", f)) {
		if (!asString(f, b->LatestChangedDtsFile)) {
			return nullptr;
		}
	}
	if (field(root, "emitSignatures", f)) {
		auto* arr = f.get<tsoptions::JsonArray>();
		if (arr == nullptr) return nullptr;
		for (const auto& elem : *arr) {
			auto* e = new BuildInfoEmitSignature();
			if (!parseEmitSignature(elem, e)) {
				return nullptr;
			}
			b->EmitSignatures.push_back(e);
		}
	}
	if (field(root, "resolvedRoot", f)) {
		auto* arr = f.get<tsoptions::JsonArray>();
		if (arr == nullptr) return nullptr;
		for (const auto& elem : *arr) {
			auto* r = new BuildInfoResolvedRoot();
			if (!parseResolvedRoot(elem, r)) {
				return nullptr;
			}
			b->ResolvedRoot.push_back(r);
		}
	}
	if (field(root, "semanticErrors", f)) {
		if (!asBool(f, b->SemanticErrors)) {
			return nullptr;
		}
	}
	return b;
}

// ===========================================================================
// Plain methods.
// ===========================================================================

// buildInfo.go:117 newBuildInfoFileInfo.
BuildInfoFileInfo* newBuildInfoFileInfo(FileInfo* fileInfo) {
	if (fileInfo->version == fileInfo->signature) {
		if (!fileInfo->affectsGlobalScope &&
		    fileInfo->impliedNodeFormat == ResolutionModeCommonJS) {
			auto* b = new BuildInfoFileInfo();
			b->signature = fileInfo->signature;
			return b;
		}
	} else if (fileInfo->signature.empty()) {
		auto* b = new BuildInfoFileInfo();
		auto* ns = new buildInfoFileInfoNoSignature();
		ns->version = fileInfo->version;
		ns->noSignature = true;
		ns->affectsGlobalScope = fileInfo->affectsGlobalScope;
		ns->impliedNodeFormat = fileInfo->impliedNodeFormat;
		b->noSignature = ns;
		return b;
	}
	auto* b = new BuildInfoFileInfo();
	auto* f = new buildInfoFileInfoWithSignature();
	f->version = fileInfo->version;
	f->signature =
	    fileInfo->signature == fileInfo->version ? "" : fileInfo->signature;
	f->affectsGlobalScope = fileInfo->affectsGlobalScope;
	f->impliedNodeFormat = fileInfo->impliedNodeFormat;
	b->fileInfo = f;
	return b;
}

// buildInfo.go:127 GetFileInfo.
FileInfo* BuildInfoFileInfo::GetFileInfo() const {
	if (!signature.empty()) {
		auto* f = new FileInfo();
		f->version = signature;
		f->signature = signature;
		f->impliedNodeFormat = ResolutionModeCommonJS;
		return f;
	}
	if (noSignature != nullptr) {
		auto* f = new FileInfo();
		f->version = noSignature->version;
		f->affectsGlobalScope = noSignature->affectsGlobalScope;
		f->impliedNodeFormat = noSignature->impliedNodeFormat;
		return f;
	}
	auto* f = new FileInfo();
	f->version = fileInfo->version;
	f->signature =
	    fileInfo->signature.empty() ? fileInfo->version : fileInfo->signature;
	f->affectsGlobalScope = fileInfo->affectsGlobalScope;
	f->impliedNodeFormat = fileInfo->impliedNodeFormat;
	return f;
}

// buildInfo.go:359 toEmitSignature.
emitSignature* BuildInfoEmitSignature::toEmitSignature(
    const tspath::Path& path, collections::SyncMap<tspath::Path, emitSignature*>* emitSignatures) {
	std::string signature;
	std::optional<std::vector<std::string>> signatureWithDifferentOptions;
	if (DiffersOnlyInDtsMap) {
		signatureWithDifferentOptions = std::vector<std::string>{};
		auto [info, _] = emitSignatures->Load(path);
		signatureWithDifferentOptions->push_back(info->signature);
	} else if (DiffersInOptions) {
		signatureWithDifferentOptions = std::vector<std::string>{};
		signatureWithDifferentOptions->push_back(Signature);
	} else {
		signature = Signature;
	}
	auto* e = new emitSignature();
	e->signature = signature;
	e->signatureWithDifferentOptions = signatureWithDifferentOptions;
	return e;
}

// buildInfo.go:599 IsValidVersion.
bool BuildInfo::IsValidVersion() const { return Version == version(); }

// buildInfo.go:603 — free fn.
std::pair<std::vector<std::string>, gostd::Error>
ContentMapperIdentities(contentmapper::Project* project) {
	if (project == nullptr) {
		return {{}, nullptr};
	}
	return project->Identities();
}

// buildInfo.go:611 ContentMapperIdentitiesMatch — slices.Equal.
bool BuildInfo::ContentMapperIdentitiesMatch(
    const std::vector<std::string>& currentIdentities) const {
	return ContentMapperIdentities == currentIdentities;
}

// buildInfo.go:621 fileName — 1-based id.
std::string BuildInfo::fileName(BuildInfoFileId fileId) const {
	if (fileId < 1 || fileId > static_cast<BuildInfoFileId>(FileNames.size())) {
		return "";
	}
	return FileNames[fileId - 1];
}

// buildInfo.go:628 fileInfo.
BuildInfoFileInfo* BuildInfo::fileInfo(BuildInfoFileId fileId) const {
	if (fileId < 1 || fileId > static_cast<BuildInfoFileId>(FileInfos.size())) {
		return nullptr;
	}
	return FileInfos[fileId - 1];
}

// buildInfo.go:635 GetCompilerOptions — decodes the Options map.
CompilerOptions* BuildInfo::GetCompilerOptions(
    const std::string& buildInfoDirectory) const {
	auto* options = new CompilerOptions();
	if (Options) {
		for (const auto& option : Options->Keys()) {
			auto [value, _] = Options->Get(option);
			if (!buildInfoDirectory.empty()) {
				auto [result, ok] = tsoptions::ConvertOptionToAbsolutePath(
				    option, *value,
				    tsoptions::CommandLineCompilerOptionsMap(),
				    buildInfoDirectory);
				if (ok) {
					tsoptions::ParseCompilerOptions(option, result,
					                                options);
					continue;
				}
			}
			tsoptions::ParseCompilerOptions(option, *value, options);
		}
	}
	return options;
}

// buildInfo.go:651 IsEmitPending.
bool BuildInfo::IsEmitPending(tsoptions::ParsedCommandLine* resolved,
                              const std::string& buildInfoDirectory) {
	// Some of the emit files like source map or dts etc are not yet done
	if (resolved->CompilerOptions()->NoEmit != Tristate::True ||
	    resolved->CompilerOptions()->GetEmitDeclarations()) {
		auto pendingEmit = getPendingEmitKindWithOptions(
		    resolved->CompilerOptions(),
		    GetCompilerOptions(buildInfoDirectory));
		if (resolved->CompilerOptions()->NoEmit == Tristate::True) {
			pendingEmit &= FileEmitKindDtsErrors;
		}
		return pendingEmit != 0;
	}
	return false;
}

// buildInfo.go:668 getNormalizedPaths (iter.Seq -> yield fn).
void BuildInfo::GetPackageJsons(
    const std::string& buildInfoDirectory,
    const std::function<bool(const std::string&)>& yield) const {
	for (const auto& path : PackageJsons) {
		if (!yield(tspath::getNormalizedAbsolutePath(path,
		                                           buildInfoDirectory))) {
			return;
		}
	}
}

void BuildInfo::GetMissingPackageJsons(
    const std::string& buildInfoDirectory,
    const std::function<bool(const std::string&)>& yield) const {
	for (const auto& path : MissingPackageJsons) {
		if (!yield(tspath::getNormalizedAbsolutePath(path,
		                                           buildInfoDirectory))) {
			return;
		}
	}
}

// buildInfo.go:681 GetBuildInfoRootInfoReader.
BuildInfoRootInfoReader* BuildInfo::GetBuildInfoRootInfoReader(
    const std::string& buildInfoDirectory,
    const tspath::ComparePathsOptions& comparePathOptions) const {
	auto* reader = new BuildInfoRootInfoReader();
	reader->resolvedRootFileInfos.reserve(FileNames.size());
	reader->rootToResolved =
	    collections::OrderedMap<tspath::Path, tspath::Path>(
	        FileNames.size());
	std::unordered_map<tspath::Path, tspath::Path> resolvedToRoot;
	auto toPath = [&](const std::string& fileName) {
		return tspath::toPath(fileName, buildInfoDirectory,
		                      comparePathOptions.useCaseSensitiveFileNames);
	};

	// Create map from resolvedRoot to Root
	for (auto* resolved : ResolvedRoot) {
		auto resolvedRoot = fileName(resolved->Resolved);
		auto root = fileName(resolved->Root);
		if (!resolvedRoot.empty() && !root.empty()) {
			resolvedToRoot[toPath(resolvedRoot)] = toPath(root);
		}
	}

	auto addRoot = [&](const std::string& resolvedRoot,
	                   BuildInfoFileInfo* fileInfo) {
		if (resolvedRoot.empty()) {
			return;
		}
		auto resolvedRootPath = toPath(resolvedRoot);
		auto it = resolvedToRoot.find(resolvedRootPath);
		if (it != resolvedToRoot.end()) {
			reader->rootToResolved.Set(it->second, resolvedRootPath);
		} else {
			reader->rootToResolved.Set(resolvedRootPath,
			                           resolvedRootPath);
		}
		if (fileInfo != nullptr) {
			reader->resolvedRootFileInfos[resolvedRootPath] = fileInfo;
		}
	};

	for (auto* root : Root) {
		if (!root->NonIncremental.empty()) {
			addRoot(root->NonIncremental, nullptr);
		} else if (root->End == 0) {
			addRoot(fileName(root->Start), fileInfo(root->Start));
		} else {
			for (BuildInfoFileId i = root->Start; i <= root->End; i++) {
				addRoot(fileName(i), fileInfo(i));
			}
		}
	}

	return reader;
}

// buildInfo.go:739 GetBuildInfoFileInfo.
std::pair<BuildInfoFileInfo*, tspath::Path>
BuildInfoRootInfoReader::GetBuildInfoFileInfo(
    const tspath::Path& inputFilePath) const {
	auto it = resolvedRootFileInfos.find(inputFilePath);
	if (it != resolvedRootFileInfos.end()) {
		return {it->second, inputFilePath};
	}
	auto [resolved, ok] = rootToResolved.Get(inputFilePath);
	if (ok) {
		auto infoIt = resolvedRootFileInfos.find(*resolved);
		return {infoIt != resolvedRootFileInfos.end() ? infoIt->second
		                                            : nullptr,
		        *resolved};
	}
	return {nullptr, ""};
}

// buildInfo.go:747 Roots — yields in OrderedMap (insertion) order.
void BuildInfoRootInfoReader::Roots(
    const std::function<bool(const tspath::Path&)>& yield) const {
	for (const auto& key : rootToResolved.Keys()) {
		if (!yield(key)) return;
	}
}

// buildInfo.go:377.
bool BuildInfoEmitSignature::noEmitSignature() const {
	return Signature.empty() && !DiffersOnlyInDtsMap && !DiffersInOptions;
}

}  // namespace tsc::execute::incremental
