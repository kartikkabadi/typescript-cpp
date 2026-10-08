// fuzz_parser — libFuzzer driver for the full parse path.
//
// Feeds raw bytes into parseSourceFile — the same SourceFile-creation
// entry `tscpp parse` uses. The first byte selects ScriptKind
// (TS/TSX/JS/JSX/JSON); the rest is source text. Runs on a 64 MiB-stack
// thread via runOnBigStack, matching the parse-all worker budget.
//
// Lifetime: the returned SourceFile owns its node arena while living
// inside it, so it can't be `delete`d. Move the arenas out into stack
// locals first, run ~SourceFile to release non-arena members (text,
// diagnostics vectors), then clear() the moved arenas — which frees the
// block the SourceFile occupied only after nothing references it.

#include <cstdint>
#include <string_view>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/parser/parser.h"

#include "cmd/fuzz/fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size < 2) {
		return 0;
	}
	const char* fileName = nullptr;
	const auto kind = fuzz::scriptKindFor(data[0], &fileName);
	std::string_view text(reinterpret_cast<const char*>(data + 1), size - 1);

	fuzz::runOnBigStack(
	    [kind, fileName](const uint8_t* d, size_t n) {
		    std::string_view src(reinterpret_cast<const char*>(d), n);
		    tsc::SourceFileParseOptions opts;
		    opts.FileName = fileName;
		    opts.Path = fileName;
		    tsc::SourceFile* f =
		        tsc::parseSourceFile(opts, src, kind);
		    fuzz::releaseSourceFile(f);
	    },
	    reinterpret_cast<const uint8_t*>(text.data()), text.size());
	return 0;
}
