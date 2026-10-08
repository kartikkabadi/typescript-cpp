// emittestutil.cpp — port of tsc/internal/testutil/emittestutil/
// emittestutil.go.
#include "internal/testutil/emittestutil/emittestutil.h"

#include "internal/core/types.h"
#include "internal/testutil/parsetestutil/parsetestutil.h"
#include "internal/testutil/testutil.h" // gotest::assert

namespace tsc::testutil::emittestutil {

// CheckEmit — emittestutil.go:15.
void CheckEmit(gostd::testing::T* t, printer::EmitContext* emitContext,
               SourceFile* file, std::string_view expected) {
	t->Helper();
	printer::Printer* printer = printer::NewPrinter(
	    printer::PrinterOptions{
	        .NewLine = NewLineKind::LineFeed,
	    },
	    printer::PrintHandlers{}, emitContext);
	std::string text = printer->EmitSourceFile(file);
	// strings.TrimSuffix(text, "\n")
	std::string actual = text;
	if (!actual.empty() && actual.back() == '\n') {
		actual.pop_back();
	}
	gotest::assert::Equal(t, std::string(expected), actual);
	SourceFile* file2 = parsetestutil::ParseTypeScript(
	    text, file->LanguageVariant == LanguageVariant::JSX);
	parsetestutil::CheckDiagnosticsMessage(t, file2, "error on reparse: ");
}

}  // namespace tsc::testutil::emittestutil
