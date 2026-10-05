// Minimal stand-ins for tsc/internal/sourcemap — only the types the printer
// package references (Source, SourceIndex, Generator). The sourcemap package
// is owned by a separate slice; Generator's methods are dep-stubbed here and
// will be replaced by the real port.
#pragma once

#include "internal/ast/ast.h"
#include "internal/core/text.h"

#include <string>
#include <string_view>
#include <vector>

namespace tsc::sourcemap {

using SourceIndex = int32_t;

// Source (source.go) — a source of positions for source maps.
struct Source {
	virtual ~Source() = default;
	virtual std::string_view FileName() = 0;
	virtual std::string_view Text() = 0;
	virtual const std::vector<TextPos>& ECMALineMap() = 0;
};

// Generator (generator.go) — dep-stubbed: the sourcemap slice owns the real
// implementation. Methods are declared so the printer compiles; they are
// unreachable on the diagnostics path (SourceMap/InlineSourceMap are off).
struct Generator {
	// dep stub — owned by the sourcemap slice. Go returns `error`; the C++
	// port mirrors it as int (0 = nil/ok) so call sites keep their `if err`
	// shape.
	SourceIndex AddSource(std::string_view fileName) {
		TSC_UNREACHABLE("sourcemap::Generator::AddSource — sourcemap slice");
	}
	int SetSourceContent(SourceIndex sourceIndex, std::string_view content) {
		TSC_UNREACHABLE("sourcemap::Generator::SetSourceContent — sourcemap slice");
	}
	int AddGeneratedMapping(int line, TextPos character) {
		TSC_UNREACHABLE("sourcemap::Generator::AddGeneratedMapping — sourcemap slice");
	}
	int AddSourceMapping(int generatedLine, TextPos generatedCharacter,
	                     SourceIndex sourceIndex, int sourceLine,
	                     TextPos sourceCharacter) {
		TSC_UNREACHABLE("sourcemap::Generator::AddSourceMapping — sourcemap slice");
	}
	int AddName(std::string_view name) {
		TSC_UNREACHABLE("sourcemap::Generator::AddName — sourcemap slice");
	}
	int AddNamedSourceMapping(int generatedLine, TextPos generatedCharacter,
	                          SourceIndex sourceIndex, int sourceLine,
	                          TextPos sourceCharacter, int nameIndex) {
		TSC_UNREACHABLE("sourcemap::Generator::AddNamedSourceMapping — sourcemap slice");
	}
};

} // namespace tsc::sourcemap
