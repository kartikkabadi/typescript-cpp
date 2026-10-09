// Port of tsc/internal/ls/findallreferences_test.go.
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/bundled/bundled.h"
#include "internal/compiler/program.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/ls/ls.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/tests/testaccess.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
using namespace tsc::ls;
using namespace tsc;
namespace lsproto = tsc::lsp::lsproto;

// provideSymbolsAndEntries drives go-to-implementation with a breadth-first
// worklist. When an interface member has K implementations, every one of
// those K program-wide searches returns all K implementations. Without
// deduplicating, the retained references, the work queue, and the retained
// SymbolsAndEntries groups all grow O(K^2), which can exhaust memory on
// large, deeply-typed programs.
//
// The final LSP response is deduplicated by node, so the blow-up is
// invisible from the response; this white-box test inspects the
// pre-deduplication data that provideSymbolsAndEntries returns and asserts
// that both the accumulated reference count and the group count grow
// ~linearly with K (quadratic growth roughly quadruples when K doubles;
// deduplicated growth roughly doubles). The reference count is the faithful
// proxy for the memory cost (the actual OOM is retained references x
// per-reference checker state; a minimal repro has tiny per-reference
// state, so raw bytes are dominated by the inherent O(K^2) search work and
// do not discriminate). Because each reference node is enqueued at most
// once, the work queue is bounded by the reference count; the group count
// separately guards against retaining one group per search result.
static void TestImplementationsWorklistDoesNotBlowUp(T* t) {
	t->Parallel();

	auto measure = [t](int k) -> std::pair<int, int> {
		std::string b = "interface I { m(): void; }\n";
		for (int i = 0; i < k; i++) {
			b += gostd::sprintf("const a%d: I = { m() {} };\n", {i});
		}
		b += "declare const i: I;\n";
		b += "i.m();\n";
		std::string content = b;

		auto fs = vfs::vfstest::FromMap(
		    {{"/repro.ts", content},
		     {"/tsconfig.json",
		      "{ \"compilerOptions\": {}, \"files\": [\"repro.ts\"] }"}},
		    false /*useCaseSensitiveFileNames*/);
		fs = bundled::WrapFS(fs);

		auto* host = compiler::NewCompilerHost(
		    "/", fs, bundled::LibPath(), nullptr, nullptr, nullptr);
		CompilerOptions compilerOptions{};
		auto [parsed, errors] =
		    tsoptions::GetParsedCommandLineOfConfigFile(
		        "/tsconfig.json", &compilerOptions, nullptr, host,
		        nullptr);
		gotest::assert::Equal(t, int(errors.size()), 0);
		auto* program = compiler::NewProgram(
		    compiler::ProgramOptions{.Host = host, .Config = parsed});
		program->BindSourceFiles();
		program->GetSemanticDiagnostics(
		    program->GetSourceFile("/repro.ts"));

		auto* sourceFile = program->GetSourceFile("/repro.ts");
		auto* converters = lsconv::NewConverters(
		    lsproto::PositionEncodingKindUTF8,
		    [content](const std::string&) -> lsconv::LSPLineMap* {
			    return lsconv::ComputeLSPLineStarts(content);
		    });
		auto* l = LSTestAccess::NewWithProgramAndConverters(program,
		                                                  converters);

		// Position of the `m` property in the final `i.m();`.
		auto offset = content.rfind("i.m") + std::string("i.").size();
		auto [pos, _p] = converters->ToLSPPosition(sourceFile,
		                                         TextPos(int(offset)));

		auto [data, ok] = LSTestAccess::provideSymbolsAndEntries(
		    l, gostd::contextBackground(), "file:///repro.ts", pos,
		    false /*isRename*/, true /*implementations*/);
		gotest::assert::Assert(t, ok);
		int refs = 0;
		for (auto* se : data.SymbolsAndEntries) {
			refs += int(se->references.size());
		}
		return {refs, int(data.SymbolsAndEntries.size())};
	};

	const int k = 40;
	auto [smallRefs, smallGroups] = measure(k);
	auto [largeRefs, largeGroups] = measure(2 * k);
	t->Logf("K=%d -> %d refs / %d groups; K=%d -> %d refs / %d groups "
	        "(ref ratio %v, group ratio %v)",
	        {k, smallRefs, smallGroups, 2 * k, largeRefs, largeGroups,
	         std::to_string(double(largeRefs) / double(smallRefs)),
	         std::to_string(double(largeGroups) / double(smallGroups))});

	// Retained references (and, since each is enqueued at most once, the
	// work queue) must grow ~linearly (~2x when K doubles). The
	// un-deduplicated worklist grows ~4x. Fail above 3x.
	gotest::assert::Assert(
	    t, largeRefs <= smallRefs * 3,
	    "implementations worklist references scale superlinearly");

	// Retained SymbolsAndEntries groups must also grow ~linearly. Appending
	// one group per search result (K searches, each returning all K
	// implementations) grows ~4x; dropping duplicate empty groups keeps it
	// bounded by the distinct definitions. Fail above 3x.
	gotest::assert::Assert(
	    t, largeGroups <= smallGroups * 3,
	    "implementations worklist groups scale superlinearly");
}
REGISTER_UNIT_TEST("ls.TestImplementationsWorklistDoesNotBlowUp",
                   TestImplementationsWorklistDoesNotBlowUp);
