// fuzz_jsdoc — libFuzzer driver for the JSDoc comment parser.
//
// parser/jsdoc.go is reachable from any /** ... */ comment in source;
// the parser fuzz target only covers it incidentally. This driver wraps
// the fuzz bytes inside a JSDoc comment so the doc grammar (tags, type
// expressions, links, optional/bracketed names) gets the bytes directly:
//
//   /** <bytes> */  statement   — parsed as .js (eager JSDoc) and as .ts
//   (lazy path: SourceFile::resolveJSDoc via node->jsDoc(sf)).
//
// isJSDocLikeText also gets the raw bytes.

#include <cstdint>
#include <string>
#include <string_view>

#include "internal/ast/ast.h"
#include "internal/parser/parser.h"

#include "cmd/fuzz/fuzz_common.h"

namespace {

void run(const uint8_t* data, size_t size) {
	std::string_view body(reinterpret_cast<const char*>(data), size);
	(void)tsc::isJSDocLikeText(body);

	std::string src = "/**";
	src += body;
	src += "*/\nvar x = 1;\n/** @type {number} */\nvar y = 2;\n";

	for (tsc::ScriptKind kind :
	     {tsc::ScriptKind::JS, tsc::ScriptKind::TS}) {
		tsc::SourceFileParseOptions opts;
		bool isJs = kind == tsc::ScriptKind::JS;
		opts.FileName = isJs ? "f.js" : "f.ts";
		opts.Path = opts.FileName;
		tsc::SourceFile* f = tsc::parseSourceFile(opts, src, kind);
		// Trigger lazy JSDoc resolution on the first statement — the
		// TS path parses the comment only on access.
		if (f->Statements != nullptr) {
			for (auto* stmt : f->Statements->nodes) {
				(void)stmt->jsDoc(f);
			}
		}
		fuzz::releaseSourceFile(f);
	}
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size == 0) {
		return 0;
	}
	fuzz::runOnBigStack([](const uint8_t* d, size_t n) { run(d, n); }, data,
	                  size);
	return 0;
}
