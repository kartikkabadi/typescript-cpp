// sourcemap_baseline.go — DoSourcemapBaseline + the
// sokra.github.io preview-link builder (url.QueryEscape/QueryUnescape +
// base64.StdEncoding, ported file-local).
#include <stdexcept>
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/json/json.h"
#include "internal/sourcemap/sourcemap.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/harnessutil/harnessutil.h"
#include "internal/testutil/tsbaseline/tsbaseline.h"
#include "internal/testutil/tsbaseline/tsbaselineutil.h"
#include "internal/tspath/tspath.h"

namespace tsc::testutil::tsbaseline {

using harnessutil::CompilationResult;
using harnessutil::HarnessOptions;
using harnessutil::TestFile;

namespace {

// url.QueryEscape — percent-encode; alnum and -_.~ pass through,
// ' ' -> '+'.
std::string urlQueryEscape(std::string_view s) {
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	out.reserve(s.size());
	for (unsigned char c : s) {
		bool unreserved =
		    (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
		    c == '~';
		if (unreserved) {
			out += (char)c;
		} else if (c == ' ') {
			out += '+';
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0xF];
		}
	}
	return out;
}

// url.QueryUnescape — '+' -> ' ', %XX decode; throws on bad escape.
std::string urlQueryUnescape(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	auto hexVal = [&](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	for (size_t i = 0; i < s.size(); i++) {
		char c = s[i];
		if (c == '+') {
			out += ' ';
		} else if (c == '%') {
			if (i + 2 >= s.size()) {
				throw std::runtime_error("invalid URL escape");
			}
			int hi = hexVal(s[i + 1]);
			int lo = hexVal(s[i + 2]);
			if (hi < 0 || lo < 0) {
				throw std::runtime_error("invalid URL escape");
			}
			out += (char)(hi * 16 + lo);
			i += 2;
		} else {
			out += c;
		}
	}
	return out;
}

// base64.StdEncoding.EncodeToString.
std::string base64Encode(std::string_view s) {
	static const char* b64 =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((s.size() + 2) / 3 * 4);
	for (size_t i = 0; i < s.size(); i += 3) {
		uint32_t chunk = (unsigned char)s[i] << 16;
		if (i + 1 < s.size()) chunk |= (unsigned char)s[i + 1] << 8;
		if (i + 2 < s.size()) chunk |= (unsigned char)s[i + 2];
		out += b64[(chunk >> 18) & 63];
		out += b64[(chunk >> 12) & 63];
		out += (i + 1 < s.size()) ? b64[(chunk >> 6) & 63] : '=';
		out += (i + 2 < s.size()) ? b64[chunk & 63] : '=';
	}
	return out;
}

// base64EncodeChunk — sourcemap_baseline.go:114.
std::string base64EncodeChunk(std::string_view s) {
	auto esc = urlQueryEscape(s);
	auto unesc = urlQueryUnescape(esc);
	return base64Encode(unesc);
}

// createSourceMapPreviewLink — sourcemap_baseline.go:63.
std::string createSourceMapPreviewLink(const TestFile* sourceMap,
                                       CompilationResult* result) {
	// Go: json.Unmarshal([]byte(sourceMap.Content), &sourcemapJSON) —
	// hand-decode the two fields this function reads (File, Sources).
	sourcemap::RawSourceMap sourcemapJSON;
	{
		auto [dom, err] = json::parse(sourceMap->Content);
		if (err) {
			throw std::runtime_error(err->Error());
		}
		if (auto* file = json::objGet(dom, "file")) {
			auto [v, e] = json::asString(*file, "string");
			if (e) throw std::runtime_error(e->Error());
			sourcemapJSON.File = v;
		}
		if (auto* sources = json::objGet(dom, "sources")) {
			for (auto& item : sources->arr) {
				auto [v, e] = json::asString(item, "string");
				if (e) throw std::runtime_error(e->Error());
				sourcemapJSON.Sources.push_back(v);
			}
		}
	}

	// core.Find over result.Outputs()
	const TestFile* outputJSFile = nullptr;
	for (auto* td : result->Outputs()) {
		if (td->UnitName.size() >= sourcemapJSON.File.size() &&
		    td->UnitName.compare(td->UnitName.size() -
		                             sourcemapJSON.File.size(),
		                         std::string::npos,
		                         sourcemapJSON.File) == 0) {
			outputJSFile = td;
			break;
		}
	}

	// !!! Strada uses a fallible approach to associating inputs and
	// outputs derived from a source map output. The commented logic below
	// should be used after the Strada migration is complete:

	////inputsAndOutputs := result.GetInputsAndOutputsForFile(sourceMap.UnitName)
	////outputJSFile := inputsAndOutputs.Js

	if (outputJSFile == nullptr) {
		return "";
	}

	////if len(sourcemapJSON.Sources) == len(inputsAndOutputs.Inputs) {
	////	sourceTDs = inputsAndOutputs.Inputs
	////} else {
	std::vector<const TestFile*> sourceTDs;
	for (auto& s : sourcemapJSON.Sources) {
		const TestFile* sourceFile = nullptr;
		for (auto* td : result->Inputs()) {
			if (td->UnitName.size() >= s.size() &&
			    td->UnitName.compare(td->UnitName.size() - s.size(),
			                         std::string::npos, s) == 0) {
				sourceFile = td;
				break;
			}
		}
		if (sourceFile != nullptr) {
			if (auto* programSource =
			        result->Program->GetSourceFile(
			            sourceFile->UnitName);
			    programSource != nullptr) {
				sourceTDs.push_back(new TestFile{
				    sourceFile->UnitName, programSource->OriginalText()});
				continue;
			}
		}
		sourceTDs.push_back(sourceFile);
	}
	for (auto* td : sourceTDs) {
		if (td == nullptr) return "";
	}
	////}

	std::string hash;
	hash += "\n//// "
	        "https://sokra.github.io/source-map-visualization#base64,";
	hash += base64EncodeChunk(outputJSFile->Content);
	hash += ",";
	hash += base64EncodeChunk(sourceMap->Content);
	for (auto* td : sourceTDs) {
		hash += ",";
		hash += base64EncodeChunk(td->Content);
	}
	hash += '\n';
	return hash;
}

}  // namespace

// DoSourcemapBaseline — sourcemap_baseline.go:18.
void DoSourcemapBaseline(gostd::testing::T* t,
                         const std::string& baselinePathIn,
                         const std::string& header, CompilerOptions* options,
                         harnessutil::CompilationResult* result,
                         harnessutil::HarnessOptions* harnessSettings,
                         const baseline::Options& opts) {
	auto baselinePath = baselinePathIn;
	auto declMaps = options->GetAreDeclarationMapsEnabled();
	if (options->InlineSourceMap == Tristate::True) {
		if (result->Maps.Size() > 0 && !declMaps) {
			t->Fatal({"No sourcemap files should be generated if "
			          "inlineSourceMaps was set."});
		}
		return;
	} else if (options->SourceMap == Tristate::True || declMaps) {
		size_t expectedMapCount = 0;
		if (options->SourceMap == Tristate::True) {
			expectedMapCount += result->GetNumberOfJSFiles(
			    false /*includeJSON*/);
		}
		if (declMaps) {
			expectedMapCount += result->DTS.Size();
		}
		if (result->Maps.Size() != expectedMapCount) {
			t->Fatal({"Number of sourcemap files should be same as js "
			          "files."});
		}

		std::string sourceMapCode;
		if ((options->NoEmitOnError == Tristate::True &&
		     !result->Diagnostics.empty()) ||
		    result->Maps.Size() == 0) {
			sourceMapCode = std::string(baseline::NoContent);
		} else {
			std::string sourceMapCodeBuilder;
			for (auto* sourceMap : result->Maps.Values()) {
				if (sourceMapCodeBuilder.size() > 0) {
					sourceMapCodeBuilder += "\r\n";
				}
				sourceMapCodeBuilder +=
				    fileOutput(sourceMap, harnessSettings);
				if (options->InlineSourceMap != Tristate::True) {
					sourceMapCodeBuilder +=
					    createSourceMapPreviewLink(sourceMap, result);
				}
			}
			sourceMapCode = sourceMapCodeBuilder;
		}

		if (tspath::fileExtensionIsOneOf(
		        baselinePath,
		        {tspath::extensionTs, tspath::extensionTsx})) {
			baselinePath = tspath::changeExtension(
			    baselinePath,
			    std::string(tspath::extensionJs) + ".map");
		}

		baseline::Run(t, baselinePath, sourceMapCode, opts);
	}
}

}  // namespace tsc::testutil::tsbaseline
