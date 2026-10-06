// readablebuildinfo.cpp — port of tsctests/readablebuildinfo.go.
// Renders a .tsbuildinfo file as indented, human-diffable JSON
// (Go: json.MarshalIndent(&readable, "", "  ")) where file ids are
// replaced by paths and every field keeps Go's ,omitzero omission.
//
// The Go types' UnmarshalJSON methods are intentionally not ported: the
// readable form is write-only — nothing in the tree ever calls
// json.Unmarshal on these types (several of the Go bodies are
// unreachable/failing anyway, e.g. []any → []T assertions).
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/execute/tsctests/tsctests.h"

namespace tsc::execute::tsctests {

namespace {

// ---- Go encoding/json/v2 writers (same rules as buildinfo.cpp: HTML-unsafe
// <, >, & escaped, invalid UTF-8 →, shortest round-trip doubles) ----

void writeJsonString(std::string& out, std::string_view s) {
	static const char* hex = "0123456789abcdef";
	out += '"';
	size_t i = 0;
	while (i < s.size()) {
		uint8_t c = static_cast<uint8_t>(s[i]);
		if (c >= 0x20 && c <= 0x7e && c != '"' && c != '\\' && c != '<' &&
		    c != '>' && c != '&') {
			out += static_cast<char>(c);
			i++;
			continue;
		}
		switch (c) {
		case '"': out += "\\\""; i++; continue;
		case '\\': out += "\\\\"; i++; continue;
		case '\n': out += "\\n"; i++; continue;
		case '\r': out += "\\r"; i++; continue;
		case '\t': out += "\\t"; i++; continue;
		case '<': out += "\\u003c"; i++; continue;
		case '>': out += "\\u003e"; i++; continue;
		case '&': out += "\\u0026"; i++; continue;
		default: break;
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
	char buf[32];
	for (int prec : {15, 16, 17}) {
		std::snprintf(buf, sizeof(buf), "%.*g", prec, v);
		if (std::strtod(buf, nullptr) == v) break;
	}
	out += buf;
}

void writeJsonBool(std::string& out, bool v) { out += v ? "true" : "false"; }

// ---- MarshalIndent(v, "", "  ") writer ----
// Encoding/json indent: every key/element sits on its own line at depth*2
// spaces; empty composites print as {} / [] (no inner newline); "key": v.

struct jw {
	std::string& out;
	std::vector<int> counts; // elements written per open composite
	explicit jw(std::string& o) : out(o) {}

	void openComposite(char c) {
		out += c;
		counts.push_back(0);
	}
	void closeComposite(char c) {
		bool empty = counts.back() == 0;
		counts.pop_back();
		if (!empty) {
			out += '\n';
			out.append(counts.size() * 2, ' ');
		}
		out += c;
	}
	void beginObj() { openComposite('{'); }
	void endObj() { closeComposite('}'); }
	void beginArr() { openComposite('['); }
	void endArr() { closeComposite(']'); }
	// sep — before each key or element: ',' continuation + newline + indent.
	void sep() {
		if (counts.back() != 0) out += ',';
		counts.back()++;
		out += '\n';
		out.append(counts.size() * 2, ' ');
	}
	void key(std::string_view name) {
		sep();
		writeJsonString(out, name);
		out += ": ";
	}
	void field(std::string_view name, bool emit,
	           const std::function<void()>& w) {
		if (!emit) return;
		key(name);
		w();
	}
	// scalar conveniences
	void str(std::string_view s) { writeJsonString(out, s); }
	void int_(int64_t v) { writeJsonInt(out, v); }
	void dbl(double v) { writeJsonDouble(out, v); }
	void bool_(bool v) { writeJsonBool(out, v); }
	void strArr(const std::vector<std::string>& v) {
		beginArr();
		for (const auto& s : v) {
			sep();
			str(s);
		}
		endArr();
	}
};

void writeJsonValueIndented(jw& w, const tsoptions::CompilerOptionsValue& v);

void writeJsonObjectIndented(jw& w, const tsoptions::JsonObjectPtr& obj) {
	w.beginObj();
	if (obj) {
		for (const auto& key : obj->Keys()) {
			w.key(key);
			writeJsonValueIndented(w, *obj->Get(key).first);
		}
	}
	w.endObj();
}

// writeJsonValue (indented) — CompilerOptionsValue arms, same shapes as
// buildinfo.cpp's compact writer.
void writeJsonValueIndented(jw& w,
                            const tsoptions::CompilerOptionsValue& v) {
	using tsoptions::JsonArray;
	using tsoptions::JsonObjectPtr;
	using tsoptions::JsonStrList;
	if (v.isNil()) {
		w.out += "null";
	} else if (auto* b = v.get<bool>()) {
		w.bool_(*b);
	} else if (auto* t = v.get<Tristate>()) {
		// Tristate.MarshalJSON — tristate.go:54.
		switch (*t) {
		case Tristate::True: w.out += "true"; break;
		case Tristate::False: w.out += "false"; break;
		default: w.out += "null"; break;
		}
	} else if (auto* i = v.get<int64_t>()) {
		w.int_(*i);
	} else if (auto* d = v.get<double>()) {
		w.dbl(*d);
	} else if (auto* s = v.get<std::string>()) {
		w.str(*s);
	} else if (auto* sl = v.get<JsonStrList>()) {
		w.strArr(*sl);
	} else if (auto* arr = v.get<JsonArray>()) {
		w.beginArr();
		for (const auto& e : *arr) {
			w.sep();
			writeJsonValueIndented(w, e);
		}
		w.endArr();
	} else if (auto* obj = v.get<JsonObjectPtr>()) {
		writeJsonObjectIndented(w, *obj);
	} else {
		// DiagnosticMessage / JsonGoMap arms never appear in build info.
		w.out += "null";
	}
}

// ---- original build-info marshal shapes (indented; the Go MarshalJSON on
// *incremental.* types) ----

// buildInfo.go:36 BuildInfoRoot.MarshalJSON — int | [int,int] | string.
void writeBuildInfoRootOriginal(jw& w,
                                const incremental::BuildInfoRoot* b) {
	if (b->Start != 0) {
		if (b->End != 0) {
			w.beginArr();
			w.sep();
			w.int_(b->Start);
			w.sep();
			w.int_(b->End);
			w.endArr();
		} else {
			w.int_(b->Start);
		}
	} else {
		w.str(b->NonIncremental);
	}
}

// buildInfo.go:148 BuildInfoFileInfo.MarshalJSON — signature string, or a
// {"version","noSignature",...} object, or {"version","signature",...}.
void writeBuildInfoFileInfoOriginal(
    jw& w, const incremental::BuildInfoFileInfo* b) {
	if (!b->signature.empty()) {
		w.str(b->signature);
		return;
	}
	if (b->noSignature != nullptr) {
		const auto* f = b->noSignature;
		w.beginObj();
		w.field("version", !f->version.empty(),
		        [&] { w.str(f->version); });
		w.field("noSignature", f->noSignature,
		        [&] { w.bool_(f->noSignature); });
		w.field("affectsGlobalScope", f->affectsGlobalScope,
		        [&] { w.bool_(f->affectsGlobalScope); });
		w.field("impliedNodeFormat",
		        f->impliedNodeFormat != ResolutionModeNone, [&] {
			        w.int_(static_cast<int64_t>(f->impliedNodeFormat));
		        });
		w.endObj();
		return;
	}
	const auto* f = b->fileInfo;
	w.beginObj();
	w.field("version", !f->version.empty(), [&] { w.str(f->version); });
	w.field("signature", !f->signature.empty(),
	        [&] { w.str(f->signature); });
	w.field("affectsGlobalScope", f->affectsGlobalScope,
	        [&] { w.bool_(f->affectsGlobalScope); });
	w.field("impliedNodeFormat", f->impliedNodeFormat != ResolutionModeNone,
	        [&] { w.int_(static_cast<int64_t>(f->impliedNodeFormat)); });
	w.endObj();
}

// buildInfo.go:286 BuildInfoFilePendingEmit.MarshalJSON — fileId | [fileId]
// | [fileId, emitKind].
void writeFilePendingEmitOriginal(
    jw& w, const incremental::BuildInfoFilePendingEmit* b) {
	if (b->EmitKind == 0) {
		w.int_(b->FileId);
	} else if (b->EmitKind == incremental::FileEmitKindDts) {
		w.beginArr();
		w.sep();
		w.int_(b->FileId);
		w.endArr();
	} else {
		w.beginArr();
		w.sep();
		w.int_(b->FileId);
		w.sep();
		w.int_(b->EmitKind);
		w.endArr();
	}
}

// buildInfo.go:371 BuildInfoEmitSignature.MarshalJSON — fileId | [fileId,
// signature | [] | [signature]].
void writeEmitSignatureOriginal(
    jw& w, const incremental::BuildInfoEmitSignature* b) {
	if (b->noEmitSignature()) {
		w.int_(b->FileId);
		return;
	}
	w.beginArr();
	w.sep();
	w.int_(b->FileId);
	w.sep();
	if (b->DiffersOnlyInDtsMap) {
		w.beginArr();
		w.endArr();
	} else if (b->DiffersInOptions) {
		w.beginArr();
		w.sep();
		w.str(b->Signature);
		w.endArr();
	} else {
		w.str(b->Signature);
	}
	w.endArr();
}

}  // namespace

// ---- readable types (readablebuildinfo.go:15-216) — field order is the
// Go struct's marshal order; every tagged field keeps its omitzero ----

// readableBuildInfoRoot — readablebuildinfo.go:45.
struct readableBuildInfoRoot {
	std::vector<std::string> Files;
	incremental::BuildInfoRoot* Original = nullptr;
};

// readableBuildInfoFileInfo — readablebuildinfo.go:50.
struct readableBuildInfoFileInfo {
	std::string FileName;
	std::string Version;
	std::string Signature;
	bool AffectsGlobalScope = false;
	std::string ImpliedNodeFormat;
	incremental::BuildInfoFileInfo* Original = nullptr;
};

struct readableBuildInfoRepopulateInfo;
struct readableBuildInfoDiagnostic;

// readableBuildInfoDiagnosticsOfFile — readablebuildinfo.go:84.
// MarshalJSON: [file, diagnostics].
struct readableBuildInfoDiagnosticsOfFile {
	std::string file;
	std::vector<std::unique_ptr<readableBuildInfoDiagnostic>> diagnostics;
};

// readableBuildInfoSemanticDiagnostic — readablebuildinfo.go:119.
// MarshalJSON: "file" | [file, diagnostics].
struct readableBuildInfoSemanticDiagnostic {
	std::string file;
	std::unique_ptr<readableBuildInfoDiagnosticsOfFile> diagnostics;
};

// readableBuildInfoFilePendingEmit — readablebuildinfo.go:149.
// MarshalJSON: [file, emitKind, original].
struct readableBuildInfoFilePendingEmit {
	std::string file;
	std::string emitKind;
	incremental::BuildInfoFilePendingEmit* original = nullptr;
};

// readableBuildInfoEmitSignature — readablebuildinfo.go:189.
struct readableBuildInfoEmitSignature {
	std::string File;
	std::string Signature;
	bool DiffersOnlyInDtsMap = false;
	bool DiffersInOptions = false;
	incremental::BuildInfoEmitSignature* Original = nullptr;
};

// readableBuildInfoResolvedRoot — readablebuildinfo.go:197.
// MarshalJSON: [resolved, root].
struct readableBuildInfoResolvedRoot {
	std::string Resolved;
	std::string Root;
};

// readableBuildInfoRepopulateInfo — readablebuildinfo.go:77.
struct readableBuildInfoRepopulateInfo {
	RepopulateDiagnosticKind Kind = RepopulateDiagnosticKind::None;
	std::string ModuleReference;
	ResolutionMode Mode = ResolutionModeNone;
	std::string PackageName;
};

// readableBuildInfoDiagnostic — readablebuildinfo.go:59.
struct readableBuildInfoDiagnostic {
	// incrementalBuildInfoFileId if it is for a File thats other than its
	// stored for
	std::string File;
	bool NoFile = false;
	int Pos = 0;
	int End = 0;
	int32_t Code = 0;
	DiagnosticCategory Category = DiagnosticCategory::Warning;
	Key MessageKey;
	std::vector<std::string> MessageArgs;
	std::vector<std::unique_ptr<readableBuildInfoDiagnostic>> MessageChain;
	std::vector<std::unique_ptr<readableBuildInfoDiagnostic>>
	    RelatedInformation;
	bool ReportsUnnecessary = false;
	bool ReportsDeprecated = false;
	bool SkippedOnNoEmit = false;
	std::unique_ptr<readableBuildInfoRepopulateInfo> RepopulateInfo;
};

// readableBuildInfo — readablebuildinfo.go:15.
struct readableBuildInfo {
	incremental::BuildInfo* buildInfo = nullptr;
	std::string Version;

	// Common between incremental and tsc -b buildinfo for non incremental
	// programs
	bool Errors = false;
	bool CheckPending = false;
	std::vector<std::unique_ptr<readableBuildInfoRoot>> Root;
	std::vector<std::string> PackageJsons;
	std::vector<std::string> MissingPackageJsons;

	// IncrementalProgram info
	std::vector<std::string> FileNames;
	std::vector<std::unique_ptr<readableBuildInfoFileInfo>> FileInfos;
	std::vector<std::vector<std::string>> FileIdsList;
	tsoptions::JsonObjectPtr Options;
	std::unique_ptr<collections::OrderedMap<std::string,
	                                       std::vector<std::string>>>
	    ReferencedMap;
	std::vector<std::unique_ptr<readableBuildInfoSemanticDiagnostic>>
	    SemanticDiagnosticsPerFile;
	std::vector<std::unique_ptr<readableBuildInfoDiagnosticsOfFile>>
	    EmitDiagnosticsPerFile;
	std::vector<std::string> ChangeFileSet;
	std::vector<std::unique_ptr<readableBuildInfoFilePendingEmit>>
	    AffectedFilesPendingEmit;
	std::string LatestChangedDtsFile;
	std::vector<std::unique_ptr<readableBuildInfoEmitSignature>>
	    EmitSignatures;
	std::vector<std::unique_ptr<readableBuildInfoResolvedRoot>> ResolvedRoot;
	int Size = 0;

	// NonIncrementalProgram info
	bool SemanticErrors = false;

	// toFilePath — readablebuildinfo.go:249.
	std::string toFilePath(incremental::BuildInfoFileId fileId) const {
		return buildInfo->FileNames[fileId - 1];
	}
	// toFilePathSet — readablebuildinfo.go:253.
	const std::vector<std::string>&
	toFilePathSet(incremental::BuildInfoFileIdListId fileIdListId) const {
		return FileIdsList[fileIdListId - 1];
	}
	// toReadableBuildInfoDiagnostic — readablebuildinfo.go:257.
	std::vector<std::unique_ptr<readableBuildInfoDiagnostic>>
	toReadableBuildInfoDiagnostic(
	    const std::vector<incremental::BuildInfoDiagnostic*>& diagnostics);
	// toReadableBuildInfoDiagnosticsOfFile — readablebuildinfo.go:294.
	std::unique_ptr<readableBuildInfoDiagnosticsOfFile>
	toReadableBuildInfoDiagnosticsOfFile(
	    incremental::BuildInfoDiagnosticsOfFile* diagnostics) {
		auto out = std::make_unique<readableBuildInfoDiagnosticsOfFile>();
		out->file = toFilePath(diagnostics->FileId);
		out->diagnostics =
		    toReadableBuildInfoDiagnostic(diagnostics->Diagnostics);
		return out;
	}
	// setFileInfos — readablebuildinfo.go:301.
	void setFileInfos();
	// setRoot — readablebuildinfo.go:319.
	void setRoot();
	// setFileIdsList — readablebuildinfo.go:339.
	void setFileIdsList();
	// setReferencedMap — readablebuildinfo.go:345.
	void setReferencedMap();
	// setChangeFileSet — readablebuildinfo.go:354.
	void setChangeFileSet();
	// setSemanticDiagnostics — readablebuildinfo.go:358.
	void setSemanticDiagnostics();
	// setEmitDiagnostics — readablebuildinfo.go:371.
	void setEmitDiagnostics();
	// setAffectedFilesPendingEmit — readablebuildinfo.go:375.
	void setAffectedFilesPendingEmit();
	// setEmitSignatures — readablebuildinfo.go:430.
	void setEmitSignatures();
	// setResolvedRoot — readablebuildinfo.go:442.
	void setResolvedRoot();
};

// toReadableBuildInfoRepopulateInfo — readablebuildinfo.go:282.
static std::unique_ptr<readableBuildInfoRepopulateInfo>
toReadableBuildInfoRepopulateInfo(
    incremental::BuildInfoRepopulateInfo* info) {
	if (info == nullptr) {
		return nullptr;
	}
	auto out = std::make_unique<readableBuildInfoRepopulateInfo>();
	out->Kind = info->Kind;
	out->ModuleReference = info->ModuleReference;
	out->Mode = info->Mode;
	out->PackageName = info->PackageName;
	return out;
}

std::vector<std::unique_ptr<readableBuildInfoDiagnostic>>
readableBuildInfo::toReadableBuildInfoDiagnostic(
    const std::vector<incremental::BuildInfoDiagnostic*>& diagnostics) {
	std::vector<std::unique_ptr<readableBuildInfoDiagnostic>> out;
	out.reserve(diagnostics.size());
	for (auto* d : diagnostics) {
		std::string file;
		if (d->File != 0) {
			file = toFilePath(d->File);
		}
		auto rd = std::make_unique<readableBuildInfoDiagnostic>();
		rd->File = std::move(file);
		rd->NoFile = d->NoFile;
		rd->Pos = d->Pos;
		rd->End = d->End;
		rd->Code = d->Code;
		rd->Category = d->Category;
		rd->MessageKey = d->MessageKey;
		rd->MessageArgs = d->MessageArgs;
		rd->MessageChain =
		    toReadableBuildInfoDiagnostic(d->MessageChain);
		rd->RelatedInformation =
		    toReadableBuildInfoDiagnostic(d->RelatedInformation);
		rd->ReportsUnnecessary = d->ReportsUnnecessary;
		rd->ReportsDeprecated = d->ReportsDeprecated;
		rd->SkippedOnNoEmit = d->SkippedOnNoEmit;
		rd->RepopulateInfo =
		    toReadableBuildInfoRepopulateInfo(d->RepopulateInfo);
		out.push_back(std::move(rd));
	}
	return out;
}

void readableBuildInfo::setFileInfos() {
	int index = 0;
	for (auto* original : buildInfo->FileInfos) {
		std::unique_ptr<incremental::FileInfo> fileInfo(
		    original->GetFileInfo());
		auto* originalForJson = original;
		// Dont set original for string encoding
		if (original->HasSignature()) {
			originalForJson = nullptr;
		}
		auto r = std::make_unique<readableBuildInfoFileInfo>();
		r->FileName =
		    toFilePath(incremental::BuildInfoFileId(index + 1));
		r->Version = std::string(fileInfo->Version());
		r->Signature = std::string(fileInfo->Signature());
		r->AffectsGlobalScope = fileInfo->AffectsGlobalScope();
		r->ImpliedNodeFormat =
		    std::string(String(fileInfo->ImpliedNodeFormat()));
		r->Original = originalForJson;
		FileInfos.push_back(std::move(r));
		index++;
	}
}

void readableBuildInfo::setRoot() {
	for (auto* original : buildInfo->Root) {
		std::vector<std::string> files;
		if (!original->NonIncremental.empty()) {
			files = {original->NonIncremental};
		} else if (original->End == 0) {
			files = {toFilePath(original->Start)};
		} else {
			files.reserve(original->End - original->Start + 1);
			for (int i = original->Start; i <= original->End; i++) {
				files.push_back(toFilePath(i));
			}
		}
		auto r = std::make_unique<readableBuildInfoRoot>();
		r->Files = std::move(files);
		r->Original = original;
		Root.push_back(std::move(r));
	}
}

void readableBuildInfo::setFileIdsList() {
	for (const auto& ids : buildInfo->FileIdsList) {
		std::vector<std::string> out;
		out.reserve(ids.size());
		for (auto id : ids) {
			out.push_back(toFilePath(id));
		}
		FileIdsList.push_back(std::move(out));
	}
}

void readableBuildInfo::setReferencedMap() {
	if (!buildInfo->ReferencedMap.empty()) {
		ReferencedMap = std::make_unique<
		    collections::OrderedMap<std::string,
		                            std::vector<std::string>>>();
		for (auto* entry : buildInfo->ReferencedMap) {
			ReferencedMap->Set(toFilePath(entry->FileId),
			                   toFilePathSet(entry->FileIdListId));
		}
	}
}

void readableBuildInfo::setChangeFileSet() {
	for (auto id : buildInfo->ChangeFileSet) {
		ChangeFileSet.push_back(toFilePath(id));
	}
}

void readableBuildInfo::setSemanticDiagnostics() {
	for (auto* diagnostics : buildInfo->SemanticDiagnosticsPerFile) {
		auto out = std::make_unique<readableBuildInfoSemanticDiagnostic>();
		if (diagnostics->FileId != 0) {
			out->file = toFilePath(diagnostics->FileId);
		} else {
			out->diagnostics = toReadableBuildInfoDiagnosticsOfFile(
			    diagnostics->Diagnostics);
		}
		SemanticDiagnosticsPerFile.push_back(std::move(out));
	}
}

void readableBuildInfo::setEmitDiagnostics() {
	for (auto* d : buildInfo->EmitDiagnosticsPerFile) {
		EmitDiagnosticsPerFile.push_back(
		    toReadableBuildInfoDiagnosticsOfFile(d));
	}
}

void readableBuildInfo::setAffectedFilesPendingEmit() {
	if (buildInfo->AffectedFilesPendingEmit.empty()) {
		return;
	}
	std::unique_ptr<CompilerOptions> options(
	    buildInfo->GetCompilerOptions(""));
	incremental::FileEmitKind fullEmitKind =
	    incremental::GetFileEmitKind(options.get());
	for (auto* pendingEmit : buildInfo->AffectedFilesPendingEmit) {
		incremental::FileEmitKind emitKind =
		    pendingEmit->EmitKind == 0 ? fullEmitKind
		                               : pendingEmit->EmitKind;
		auto out = std::make_unique<readableBuildInfoFilePendingEmit>();
		out->file = toFilePath(pendingEmit->FileId);
		out->emitKind = toReadableFileEmitKind(emitKind);
		out->original = pendingEmit;
		AffectedFilesPendingEmit.push_back(std::move(out));
	}
}

void readableBuildInfo::setEmitSignatures() {
	for (auto* signature : buildInfo->EmitSignatures) {
		auto out = std::make_unique<readableBuildInfoEmitSignature>();
		out->File = toFilePath(signature->FileId);
		out->Signature = signature->Signature;
		out->DiffersOnlyInDtsMap = signature->DiffersOnlyInDtsMap;
		out->DiffersInOptions = signature->DiffersInOptions;
		out->Original = signature;
		EmitSignatures.push_back(std::move(out));
	}
}

void readableBuildInfo::setResolvedRoot() {
	for (auto* original : buildInfo->ResolvedRoot) {
		auto out = std::make_unique<readableBuildInfoResolvedRoot>();
		out->Resolved = toFilePath(original->Resolved);
		out->Root = toFilePath(original->Root);
		ResolvedRoot.push_back(std::move(out));
	}
}

// toReadableFileEmitKind — readablebuildinfo.go:390.
std::string toReadableFileEmitKind(incremental::FileEmitKind fileEmitKind) {
	std::string builder;
	auto addFlags = [&](const char* flags) {
		if (builder.empty()) {
			builder += flags;
		} else {
			builder += "|";
			builder += flags;
		}
	};
	if (fileEmitKind != 0) {
		if ((fileEmitKind & incremental::FileEmitKindJs) != 0) {
			addFlags("Js");
		}
		if ((fileEmitKind & incremental::FileEmitKindJsMap) != 0) {
			addFlags("JsMap");
		}
		if ((fileEmitKind & incremental::FileEmitKindJsInlineMap) != 0) {
			addFlags("JsInlineMap");
		}
		if ((fileEmitKind & incremental::FileEmitKindDts) ==
		    incremental::FileEmitKindDts) {
			addFlags("Dts");
		} else {
			if ((fileEmitKind & incremental::FileEmitKindDtsEmit) != 0) {
				addFlags("DtsEmit");
			}
			if ((fileEmitKind & incremental::FileEmitKindDtsErrors) != 0) {
				addFlags("DtsErrors");
			}
		}
		if ((fileEmitKind & incremental::FileEmitKindDtsMap) != 0) {
			addFlags("DtsMap");
		}
	}
	if (!builder.empty()) {
		return builder;
	}
	return "None";
}

namespace {

// ---- marshalers (MarshalJSON bodies, Go field order) ----

void writeReadableDiagnostic(jw& w,
                             const readableBuildInfoDiagnostic* d) {
	w.beginObj();
	w.field("file", !d->File.empty(), [&] { w.str(d->File); });
	w.field("noFile", d->NoFile, [&] { w.bool_(d->NoFile); });
	w.field("pos", d->Pos != 0, [&] { w.int_(d->Pos); });
	w.field("end", d->End != 0, [&] { w.int_(d->End); });
	w.field("code", d->Code != 0, [&] { w.int_(d->Code); });
	w.field("category",
	        d->Category != static_cast<DiagnosticCategory>(0), [&] {
		        w.int_(static_cast<int64_t>(d->Category));
	        });
	w.field("messageKey", !d->MessageKey.empty(),
	        [&] { w.str(d->MessageKey); });
	w.field("messageArgs", !d->MessageArgs.empty(),
	        [&] { w.strArr(d->MessageArgs); });
	w.field("messageChain", !d->MessageChain.empty(), [&] {
		w.beginArr();
		for (const auto& c : d->MessageChain) {
			w.sep();
			writeReadableDiagnostic(w, c.get());
		}
		w.endArr();
	});
	w.field("relatedInformation", !d->RelatedInformation.empty(), [&] {
		w.beginArr();
		for (const auto& r : d->RelatedInformation) {
			w.sep();
			writeReadableDiagnostic(w, r.get());
		}
		w.endArr();
	});
	w.field("reportsUnnecessary", d->ReportsUnnecessary,
	        [&] { w.bool_(d->ReportsUnnecessary); });
	w.field("reportsDeprecated", d->ReportsDeprecated,
	        [&] { w.bool_(d->ReportsDeprecated); });
	w.field("skippedOnNoEmit", d->SkippedOnNoEmit,
	        [&] { w.bool_(d->SkippedOnNoEmit); });
	w.field("repopulateInfo", d->RepopulateInfo != nullptr, [&] {
		// readableBuildInfoRepopulateInfo — readablebuildinfo.go:77;
		// `kind` is NOT omitzero.
		w.beginObj();
		w.key("kind");
		w.int_(static_cast<int64_t>(d->RepopulateInfo->Kind));
		w.field("moduleReference",
		        !d->RepopulateInfo->ModuleReference.empty(), [&] {
			        w.str(d->RepopulateInfo->ModuleReference);
		        });
		w.field("mode",
		        d->RepopulateInfo->Mode != ResolutionModeNone, [&] {
			        w.int_(static_cast<int64_t>(d->RepopulateInfo->Mode));
		        });
		w.field("packageName", !d->RepopulateInfo->PackageName.empty(),
		        [&] { w.str(d->RepopulateInfo->PackageName); });
		w.endObj();
	});
	w.endObj();
}

void writeReadableDiagnosticsArray(
    jw& w,
    const std::vector<std::unique_ptr<readableBuildInfoDiagnostic>>& v) {
	w.beginArr();
	for (const auto& d : v) {
		w.sep();
		writeReadableDiagnostic(w, d.get());
	}
	w.endArr();
}

// readableBuildInfoDiagnosticsOfFile.MarshalJSON — readablebuildinfo.go:89:
// [file, diagnostics].
void writeDiagnosticsOfFile(
    jw& w, const readableBuildInfoDiagnosticsOfFile* b) {
	w.beginArr();
	w.sep();
	w.str(b->file);
	w.sep();
	writeReadableDiagnosticsArray(w, b->diagnostics);
	w.endArr();
}

// readableBuildInfoSemanticDiagnostic.MarshalJSON — readablebuildinfo.go:
// 124: "file" | [file, diagnostics].
void writeSemanticDiagnostic(
    jw& w, const readableBuildInfoSemanticDiagnostic* b) {
	if (!b->file.empty()) {
		w.str(b->file);
	} else {
		writeDiagnosticsOfFile(w, b->diagnostics.get());
	}
}

// readableBuildInfoFilePendingEmit.MarshalJSON — readablebuildinfo.go:155:
// [file, emitKind, original].
void writeFilePendingEmit(jw& w,
                          const readableBuildInfoFilePendingEmit* b) {
	w.beginArr();
	w.sep();
	w.str(b->file);
	w.sep();
	w.str(b->emitKind);
	w.sep();
	writeFilePendingEmitOriginal(w, b->original);
	w.endArr();
}

void writeEmitSignatureReadable(
    jw& w, const readableBuildInfoEmitSignature* b) {
	w.beginObj();
	w.field("file", !b->File.empty(), [&] { w.str(b->File); });
	w.field("signature", !b->Signature.empty(),
	        [&] { w.str(b->Signature); });
	w.field("differsOnlyInDtsMap", b->DiffersOnlyInDtsMap,
	        [&] { w.bool_(b->DiffersOnlyInDtsMap); });
	w.field("differsInOptions", b->DiffersInOptions,
	        [&] { w.bool_(b->DiffersInOptions); });
	w.field("original", b->Original != nullptr,
	        [&] { writeEmitSignatureOriginal(w, b->Original); });
	w.endObj();
}

// readableBuildInfoResolvedRoot.MarshalJSON — readablebuildinfo.go:202:
// [resolved, root].
void writeResolvedRoot(jw& w,
                       const readableBuildInfoResolvedRoot* b) {
	w.beginArr();
	w.sep();
	w.str(b->Resolved);
	w.sep();
	w.str(b->Root);
	w.endArr();
}

void writeReadableRoot(jw& w, const readableBuildInfoRoot* b) {
	w.beginObj();
	w.field("files", !b->Files.empty(), [&] { w.strArr(b->Files); });
	w.field("original", b->Original != nullptr,
	        [&] { writeBuildInfoRootOriginal(w, b->Original); });
	w.endObj();
}

void writeReadableFileInfo(jw& w,
                           const readableBuildInfoFileInfo* b) {
	w.beginObj();
	w.field("fileName", !b->FileName.empty(), [&] { w.str(b->FileName); });
	w.field("version", !b->Version.empty(), [&] { w.str(b->Version); });
	w.field("signature", !b->Signature.empty(),
	        [&] { w.str(b->Signature); });
	w.field("affectsGlobalScope", b->AffectsGlobalScope,
	        [&] { w.bool_(b->AffectsGlobalScope); });
	w.field("impliedNodeFormat", !b->ImpliedNodeFormat.empty(),
	        [&] { w.str(b->ImpliedNodeFormat); });
	w.field("original", b->Original != nullptr,
	        [&] { writeBuildInfoFileInfoOriginal(w, b->Original); });
	w.endObj();
}

// readableBuildInfo — readablebuildinfo.go:15 (MarshalIndent of the
// struct; field order = Go declaration order).
void writeReadable(jw& w, const readableBuildInfo* b) {
	w.beginObj();
	w.field("version", !b->Version.empty(), [&] { w.str(b->Version); });
	w.field("errors", b->Errors, [&] { w.bool_(b->Errors); });
	w.field("checkPending", b->CheckPending,
	        [&] { w.bool_(b->CheckPending); });
	w.field("root", !b->Root.empty(), [&] {
		w.beginArr();
		for (const auto& r : b->Root) {
			w.sep();
			writeReadableRoot(w, r.get());
		}
		w.endArr();
	});
	w.field("packageJsons", !b->PackageJsons.empty(),
	        [&] { w.strArr(b->PackageJsons); });
	w.field("missingPackageJsons", !b->MissingPackageJsons.empty(),
	        [&] { w.strArr(b->MissingPackageJsons); });
	w.field("fileNames", !b->FileNames.empty(),
	        [&] { w.strArr(b->FileNames); });
	w.field("fileInfos", !b->FileInfos.empty(), [&] {
		w.beginArr();
		for (const auto& f : b->FileInfos) {
			w.sep();
			writeReadableFileInfo(w, f.get());
		}
		w.endArr();
	});
	w.field("fileIdsList", !b->FileIdsList.empty(), [&] {
		w.beginArr();
		for (const auto& ids : b->FileIdsList) {
			w.sep();
			w.strArr(ids);
		}
		w.endArr();
	});
	w.field("options", b->Options != nullptr,
	        [&] { writeJsonObjectIndented(w, b->Options); });
	w.field("referencedMap", b->ReferencedMap != nullptr, [&] {
		w.beginObj();
		for (const auto& key : b->ReferencedMap->Keys()) {
			w.key(key);
			w.strArr(*b->ReferencedMap->Get(key).first);
		}
		w.endObj();
	});
	w.field("semanticDiagnosticsPerFile",
	        !b->SemanticDiagnosticsPerFile.empty(), [&] {
		        w.beginArr();
		        for (const auto& d : b->SemanticDiagnosticsPerFile) {
			        w.sep();
			        writeSemanticDiagnostic(w, d.get());
		        }
		        w.endArr();
	        });
	w.field("emitDiagnosticsPerFile", !b->EmitDiagnosticsPerFile.empty(),
	        [&] {
		        w.beginArr();
		        for (const auto& d : b->EmitDiagnosticsPerFile) {
			        w.sep();
			        writeDiagnosticsOfFile(w, d.get());
		        }
		        w.endArr();
	        });
	w.field("changeFileSet", !b->ChangeFileSet.empty(),
	        [&] { w.strArr(b->ChangeFileSet); });
	w.field("affectedFilesPendingEmit",
	        !b->AffectedFilesPendingEmit.empty(), [&] {
		        w.beginArr();
		        for (const auto& p : b->AffectedFilesPendingEmit) {
			        w.sep();
			        writeFilePendingEmit(w, p.get());
		        }
		        w.endArr();
	        });
	w.field("latestChangedDtsFile", !b->LatestChangedDtsFile.empty(),
	        [&] { w.str(b->LatestChangedDtsFile); });
	w.field("emitSignatures", !b->EmitSignatures.empty(), [&] {
		w.beginArr();
		for (const auto& s : b->EmitSignatures) {
			w.sep();
			writeEmitSignatureReadable(w, s.get());
		}
		w.endArr();
	});
	w.field("resolvedRoot", !b->ResolvedRoot.empty(), [&] {
		w.beginArr();
		for (const auto& r : b->ResolvedRoot) {
			w.sep();
			writeResolvedRoot(w, r.get());
		}
		w.endArr();
	});
	w.field("size", b->Size != 0, [&] { w.int_(b->Size); });
	w.field("semanticErrors", b->SemanticErrors,
	        [&] { w.bool_(b->SemanticErrors); });
	w.endObj();
}

}  // namespace

// toReadableBuildInfo — readablebuildinfo.go:218.
std::string toReadableBuildInfo(incremental::BuildInfo* buildInfo,
                                const std::string& buildInfoText) {
	readableBuildInfo readable;
	readable.buildInfo = buildInfo;
	readable.Version = buildInfo->Version;
	readable.Errors = buildInfo->Errors;
	readable.CheckPending = buildInfo->CheckPending;
	readable.FileNames = buildInfo->FileNames;
	readable.Options = buildInfo->Options;
	readable.LatestChangedDtsFile = buildInfo->LatestChangedDtsFile;
	readable.SemanticErrors = buildInfo->SemanticErrors;
	readable.PackageJsons = buildInfo->PackageJsons;
	readable.MissingPackageJsons = buildInfo->MissingPackageJsons;
	readable.Size = int(buildInfoText.size());
	readable.setFileInfos();
	readable.setRoot();
	readable.setFileIdsList();
	readable.setReferencedMap();
	readable.setChangeFileSet();
	readable.setSemanticDiagnostics();
	readable.setEmitDiagnostics();
	readable.setAffectedFilesPendingEmit();
	readable.setEmitSignatures();
	readable.setResolvedRoot();
	// json.MarshalIndent(&readable, "", "  ") — infallible here.
	std::string out;
	jw w{out};
	writeReadable(w, &readable);
	return out;
}

}  // namespace tsc::execute::tsctests
