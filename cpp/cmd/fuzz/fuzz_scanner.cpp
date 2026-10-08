// fuzz_scanner — libFuzzer driver for the tokenization surface.
//
// Feeds raw bytes through the same Scanner entry `tscpp lex` uses:
// setText + scan() until EndOfFile with skipTrivia=false so comments,
// JSDoc text, and trivia tokens are visited too. The first byte selects
// ScriptTarget and LanguageVariant (JSX); the rest is the source text.
// onError is a no-op — diagnostics are expected output, not failures.

#include <cstdint>
#include <string_view>

#include "internal/ast/ast.h"
#include "internal/ast/kind.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/scanner/scanner.h"

#include "cmd/fuzz/fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size < 2) {
		return 0;
	}
	const auto target = fuzz::scriptTargetFor(data[0]);
	const auto variant = (data[0] & 0x80) != 0 ? tsc::LanguageVariant::JSX
	                                           : tsc::LanguageVariant::Standard;
	std::string_view text(reinterpret_cast<const char*>(data + 1), size - 1);

	fuzz::runOnBigStack(
	    [target, variant](const uint8_t* d, size_t n) {
		    std::string_view src(reinterpret_cast<const char*>(d), n);
		    tsc::Scanner s;
		    s.setText(src);
		    s.setSkipTrivia(false);
		    s.setScriptTarget(target);
		    s.setLanguageVariant(variant);
		    s.setOnError([](const tsc::DiagnosticMessage*, int, int,
		                    const std::vector<std::string>&) {});
		    while (s.scan() != tsc::Kind::EndOfFile) {
		    }
		    (void)s.getCommentDirectives();
	    },
	    reinterpret_cast<const uint8_t*>(text.data()), text.size());
	return 0;
}
