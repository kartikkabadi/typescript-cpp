// === dep decls — owned by lsp (ls/lsconv) ===
// Faithful port of linemap.go + converters.go's diagnostic converters
// (DiagnosticToLSPPull/Push and friends); see lsconv.h.
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsdeps.h" // Converters, ScriptOrOriginal, scriptArg

#include <algorithm>
#include <sstream>
#include <unordered_set>

#include "internal/bundled/bundled.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/diagnosticwriter/diagnosticwriter.h"
#include "internal/gostd/gostd.h"
#include "internal/locale/locale.h"
#include "internal/tspath/tspath.h"

namespace tsc::lsconv {

namespace {

// diagnosticOptions — converters.go:451.
struct diagnosticOptions {
	bool reportStyleChecksAsWarnings = false;
	bool relatedInformation = false;
	lsproto::Slice<lsproto::DiagnosticTag> tagValueSet;
	bool visualStudio = false;
};

// diagnosticLocalize — ast/diagnostic.go:117 `d.Localize(locale)`.
std::string diagnosticLocalize(Diagnostic* d, const locale::Locale& locale) {
	diagnosticwriter::ASTDiagnostic wrapped(d);
	return wrapped.localize(locale);
}

// diagnosticScriptAndRange — converters.go:577. Resolves the text basis and
// range to report a diagnostic against. For a content-mapped file it maps the
// diagnostic's virtual range back to the original text so the range lines up
// with what the editor shows; the original text's line map is already what
// getLineMap returns for the file. A range in synthesized code has no
// original counterpart, so it is surfaced at the top of the file. Non-mapped
// files are returned unchanged.
std::pair<ScriptOrOriginal<SourceFile*>, TextRange> diagnosticScriptAndRange(
    SourceFile* file, TextRange loc, std::string_view source) {
	if (file == nullptr || file->SpanMap() == nullptr) {
		return {file, loc};
	}
	originalTextScript original{file->OriginalFileName(), file->OriginalText()};
	if (!source.empty()) {
		// A content mapper's own diagnostics already carry original-text
		// ranges.
		return {original, loc};
	}
	auto [mapped, fidelity] =
	    spanmap::VirtualToOriginalSpan(file->SpanMap(), loc);
	if (fidelity == spanmap::FidelityNone) {
		// Entirely synthesized code has no original location; surface it at
		// the top of the file.
		return {original, TextRange{0, 0}};
	}
	return {original, mapped};
}

// diagnosticSeverity — converters.go:608.
lsproto::DiagnosticSeverity diagnosticSeverity(DiagnosticCategory category) {
	switch (category) {
	case DiagnosticCategory::Suggestion:
		return lsproto::DiagnosticSeverityHint;
	case DiagnosticCategory::Message:
		return lsproto::DiagnosticSeverityInformation;
	case DiagnosticCategory::Warning:
		return lsproto::DiagnosticSeverityWarning;
	default:
		return lsproto::DiagnosticSeverityError;
	}
}

// styleCheckDiagnostics — converters.go:482.
// https://github.com/microsoft/vscode/blob/93e08afe0469712706ca4e268f778cfadf1a43ef/extensions/typescript-language-features/src/typeScriptServiceClientHost.ts#L40C7-L40C29
const std::unordered_set<int32_t>& styleCheckDiagnostics() {
	static const std::unordered_set<int32_t> set{
	    X_0_is_declared_but_never_used->code,
	    X_0_is_declared_but_its_value_is_never_read->code,
	    Property_0_is_declared_but_its_value_is_never_read->code,
	    All_imports_in_import_declaration_are_unused->code,
	    Unreachable_code_detected->code,
	    Unused_label->code,
	    Fallthrough_case_in_switch->code,
	    Not_all_code_paths_return_a_value->code,
	};
	return set;
}

// messageChainToString — converters.go:621.
std::string messageChainToString(Diagnostic* diagnostic,
                                 const locale::Locale& locale) {
	if (diagnostic->MessageChain().empty()) {
		return diagnosticLocalize(diagnostic, locale);
	}
	std::ostringstream b;
	diagnosticwriter::writeFlattenedASTDiagnosticMessage(b, diagnostic, "\n",
	                                                     locale);
	return b.str();
}

// ptrToSliceIfNonEmpty — converters.go:630.
template <typename T>
std::shared_ptr<lsproto::Slice<T>> ptrToSliceIfNonEmpty(std::vector<T> s) {
	if (s.empty()) {
		return nullptr;
	}
	return std::make_shared<lsproto::Slice<T>>(std::in_place, std::move(s));
}

// toLSPRange — the std::visit over a ScriptOrOriginal<SourceFile*> shared by
// both call sites in diagnosticToLSP.
std::pair<lsproto::Range, spanmap::Fidelity> toLSPRange(
    Converters* converters, const ScriptOrOriginal<SourceFile*>& script,
    TextRange loc) {
	return std::visit(
	    [&](auto&& s) { return converters->ToLSPRange(scriptArg(s), loc); },
	    script);
}

// diagnosticToLSP — converters.go:493.
lsproto::Diagnostic* diagnosticToLSP(gostd::Context ctx,
                                     Converters* converters,
                                     Diagnostic* diagnostic,
                                     const diagnosticOptions& opts) {
	auto loc = locale::fromContext(ctx);
	auto severity = diagnosticSeverity(diagnostic->Category());

	if (opts.reportStyleChecksAsWarnings &&
	    severity == lsproto::DiagnosticSeverityError &&
	    styleCheckDiagnostics().contains(diagnostic->Code())) {
		severity = lsproto::DiagnosticSeverityWarning;
	}

	std::vector<std::shared_ptr<lsproto::DiagnosticRelatedInformation>>
	    relatedInformation;
	if (opts.relatedInformation) {
		relatedInformation.reserve(diagnostic->RelatedInformation().size());
		for (auto* related : diagnostic->RelatedInformation()) {
			auto scriptAndRange = diagnosticScriptAndRange(
			    related->File(), related->Loc(), related->Source());
			auto [relatedRange, fidelity] = toLSPRange(
			    converters, std::get<0>(scriptAndRange),
			    std::get<1>(scriptAndRange));
			if (fidelity.IsNone()) {
				// Related diagnostic information cannot omit its location.
				// Use an explicit file-level location instead of presenting
				// the synthesized span's insertion point as related source.
				relatedRange = lsproto::Range{};
			}
			auto* info = new lsproto::DiagnosticRelatedInformation;
			info->Location.Uri =
			    FileNameToDocumentURI(related->File()->OriginalFileName());
			info->Location.Range = relatedRange;
			info->Message = diagnosticLocalize(related, loc);
			relatedInformation.emplace_back(info);
		}
	}

	std::vector<lsproto::DiagnosticTag> tags;
	if (opts.tagValueSet.has_value() && !opts.tagValueSet->empty() &&
	    (diagnostic->ReportsUnnecessary() || diagnostic->ReportsDeprecated())) {
		tags.reserve(2);
		if (diagnostic->ReportsUnnecessary() &&
		    std::find(opts.tagValueSet->begin(), opts.tagValueSet->end(),
		              lsproto::DiagnosticTagUnnecessary) !=
		        opts.tagValueSet->end()) {
			tags.push_back(lsproto::DiagnosticTagUnnecessary);
		}
		if (diagnostic->ReportsDeprecated() &&
		    std::find(opts.tagValueSet->begin(), opts.tagValueSet->end(),
		              lsproto::DiagnosticTagDeprecated) !=
		        opts.tagValueSet->end()) {
			tags.push_back(lsproto::DiagnosticTagDeprecated);
		}
	}

	// For diagnostics without a file (e.g., program diagnostics), use a zero
	// range.
	lsproto::Range lspRange{};
	if (diagnostic->File() != nullptr) {
		auto scriptAndRange = diagnosticScriptAndRange(
		    diagnostic->File(), diagnostic->Loc(), diagnostic->Source());
		auto [rng, fidelity] = toLSPRange(
		    converters, std::get<0>(scriptAndRange),
		    std::get<1>(scriptAndRange));
		lspRange = rng;
		if (fidelity.IsNone()) {
			// Diagnostics must carry a range. A zero range honestly means
			// "this file" when the diagnostic arose entirely in synthesized
			// code and has no original source span.
			lspRange = lsproto::Range{};
		}
	}

	auto code = std::make_shared<lsproto::IntegerOrString>();
	std::string sourceText(diagnostic->Source());
	if (sourceText.empty()) {
		sourceText = "ts";
	}
	if (opts.visualStudio) {
		code->String = std::make_shared<std::string>(
		    "TS" + std::to_string(diagnostic->Code()));
	} else {
		code->Integer = std::make_shared<int32_t>(diagnostic->Code());
	}

	auto* result = new lsproto::Diagnostic;
	result->Range = lspRange;
	result->Code = code;
	result->Severity =
	    std::make_shared<lsproto::DiagnosticSeverity>(severity);
	result->Message.String = std::make_shared<std::string>(
	    messageChainToString(diagnostic, loc));
	result->Source = sourceText;
	result->RelatedInformation =
	    ptrToSliceIfNonEmpty(std::move(relatedInformation));
	result->Tags = ptrToSliceIfNonEmpty(std::move(tags));
	return result;
}

} // namespace

// DiagnosticToLSPPull — converters.go:459. Converts a diagnostic for pull
// diagnostics (textDocument/diagnostic).
lsproto::Diagnostic* DiagnosticToLSPPull(
    gostd::Context ctx, Converters* converters, Diagnostic* diagnostic,
    bool reportStyleChecksAsWarnings) {
	auto clientCaps = lsproto::getClientCapabilities(ctx);
	const auto& clientDiagnosticCaps = clientCaps->TextDocument.Diagnostic;
	return diagnosticToLSP(
	    ctx, converters, diagnostic,
	    diagnosticOptions{
	        .reportStyleChecksAsWarnings =
	            reportStyleChecksAsWarnings, // !!! get through context UserPreferences
	        .relatedInformation = clientDiagnosticCaps.RelatedInformation,
	        .tagValueSet = clientDiagnosticCaps.TagSupport.ValueSet,
	        .visualStudio = clientCaps->VSSupportsVisualStudioExtensions,
	    });
}

// DiagnosticToLSPPush — converters.go:471. Converts a diagnostic for push
// diagnostics (textDocument/publishDiagnostics).
lsproto::Diagnostic* DiagnosticToLSPPush(gostd::Context ctx,
                                         Converters* converters,
                                         Diagnostic* diagnostic) {
	auto clientCaps = lsproto::getClientCapabilities(ctx);
	const auto& clientDiagnosticCaps = clientCaps->TextDocument.PublishDiagnostics;
	return diagnosticToLSP(
	    ctx, converters, diagnostic,
	    diagnosticOptions{
	        .relatedInformation = clientDiagnosticCaps.RelatedInformation,
	        .tagValueSet = clientDiagnosticCaps.TagSupport.ValueSet,
	        .visualStudio = clientCaps->VSSupportsVisualStudioExtensions,
	    });
}

// ComputeLSPLineStarts (linemap.go:19) — like core.ComputeLineStarts, but only
// considers "\n", "\r", and "\r\n" as line breaks, and reports when the text
// is ASCII-only.
LSPLineMap* ComputeLSPLineStarts(const std::string& text) {
	auto* lineMap = new LSPLineMap;
	auto& lineStarts = lineMap->LineStarts;
	lineStarts.reserve(std::count(text.begin(), text.end(), '\n') + 1);
	bool asciiOnly = true;

	int textLen = (int)text.size();
	int pos = 0;
	int lineStart = 0;
	while (pos < textLen) {
		uint8_t b = (uint8_t)text[pos];
		if (b < 0x80) { // utf8.RuneSelf
			pos++;
			switch (b) {
			case '\r':
				if (pos < textLen && text[pos] == '\n') {
					pos++;
				}
				[[fallthrough]];
			case '\n':
				lineStarts.push_back(lineStart);
				lineStart = pos;
			}
		} else {
			// utf8.DecodeRuneInString size
			int size = 1;
			if (b >= 0xF0) size = 4;
			else if (b >= 0xE0) size = 3;
			else if (b >= 0xC0) size = 2;
			pos += size;
			asciiOnly = false;
		}
	}
	lineStarts.push_back(lineStart);

	lineMap->AsciiOnly = asciiOnly;
	return lineMap;
}

// ComputeIndexOfLineStart (linemap.go:56) — port of
// computeLineOfPosition(lineStarts, position, lowerBound?).
int LSPLineMap::ComputeIndexOfLineStart(int32_t targetPos) const {
	// slices.BinarySearchFunc — first index where LineStarts[i] >= targetPos.
	int lo = 0, hi = (int)LineStarts.size();
	bool found = false;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (LineStarts[mid] < targetPos) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	int lineNumber = lo;
	if (lineNumber < (int)LineStarts.size() && LineStarts[lineNumber] == targetPos) {
		found = true;
	}
	if (!found && lineNumber > 0) {
		// If the actual position was not found, the binary search returns where
		// the target line start would be inserted if the target was in the
		// slice. We want the index of the previous line start, so subtract 1.
		lineNumber = lineNumber - 1;
	}
	return lineNumber;
}

namespace {

// extraEscapeReplacer (converters.go:307) composed over url.PathEscape: every
// non-unreserved byte is %-escaped, uppercase hex (the composition leaves only
// RFC 3986 unreserved characters).
bool isUnreservedURI(uint8_t c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
	       c == '~';
}

std::string escapeURIPart(std::string_view part) {
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	out.reserve(part.size());
	for (uint8_t c : part) {
		if (isUnreservedURI(c)) {
			out += (char)c;
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0xF];
		}
	}
	return out;
}

// tspath.SplitVolumePath (path.go:1143).
std::tuple<std::string, std::string, bool> splitVolumePath(const std::string& path) {
	if (path.size() >= 2 && tspath::isVolumeCharacter(path[0]) && path[1] == ':') {
		std::string vol = path.substr(0, 2);
		vol[0] = (char)tolower((unsigned char)vol[0]);
		return {vol, path.substr(2), true};
	}
	return {"", path, false};
}

} // namespace

// FileNameToDocumentURI (converters.go:332).
lsproto::DocumentUri FileNameToDocumentURI(const std::string& fileName) {
	if (bundled::IsBundled(fileName)) {
		return lsproto::DocumentUri(fileName);
	}
	if (fileName.rfind("^/", 0) == 0) { // tspath.IsDynamicFileName
		return lsproto::dynamicFileNameToDocumentUri(fileName);
	}

	auto [volume, filePart, _ok] = splitVolumePath(fileName);
	std::string escapedVolume;
	if (!volume.empty()) {
		escapedVolume = "/" + escapeURIPart(volume);
	}

	std::string_view fp = filePart;
	if (fp.size() >= 2 && fp[0] == '/' && fp[1] == '/') {
		fp = fp.substr(2); // strings.TrimPrefix(fileName, "//")
	}

	// Split on '/', escape each part (PathEscape ∘ extraEscape = unreserved-only),
	// rejoin.
	std::string result = "file://" + escapedVolume;
	size_t segStart = 0;
	while (true) {
		auto slash = fp.find('/', segStart);
		std::string_view part = fp.substr(segStart, slash == std::string_view::npos
		                                                    ? std::string_view::npos
		                                                    : slash - segStart);
		result += escapeURIPart(part);
		if (slash == std::string_view::npos) break;
		result += '/';
		segStart = slash + 1;
	}
	return lsproto::DocumentUri(result);
}

} // namespace tsc::lsconv
