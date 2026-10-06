#pragma once

// diagnostics.go — port of tsc/internal/execute/tsc/diagnostics.go:
// diagnostic/error-summary/status reporter factories plus the color-policy
// environment probing they share.

#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/diagnosticwriter/diagnosticwriter.h"
#include "internal/execute/tsc/compile.h"
#include "internal/locale/locale.h"

namespace tsc::execute::tsc {

// getFormatOptsOfSys — diagnostics.go:13.
diagnosticwriter::FormattingOptions getFormatOptsOfSys(
    System* sys, locale::Locale locale);

// DiagnosticReporter — diagnostics.go:25.
using DiagnosticReporter = std::function<void(Diagnostic*)>;

// QuietDiagnosticReporter — diagnostics.go:27.
void QuietDiagnosticReporter(Diagnostic* diagnostic);

// CreateDiagnosticReporter — diagnostics.go:29.
DiagnosticReporter CreateDiagnosticReporter(System* sys, std::ostream* w,
                                            locale::Locale locale,
                                            const CompilerOptions* options);

// defaultIsPretty — diagnostics.go:44.
bool defaultIsPretty(System* sys);

// shouldBePretty — diagnostics.go:62.
bool shouldBePretty(System* sys, const CompilerOptions* options);

// colors — diagnostics.go:69.
struct colors {
	bool showColors = false;

	bool isWindows = false;
	bool isWindowsTerminal = false;
	bool isVSCode = false;
	bool supportsRicherColors = false;

	std::string bold(std::string_view str) const;
	std::string blue(std::string_view str) const;
	std::string blueBackground(std::string_view str) const;
	std::string brightWhite(std::string_view str) const;
};

// createColors — diagnostics.go:78.
colors createColors(System* sys);

// DiagnosticsReporter — diagnostics.go:120.
using DiagnosticsReporter =
    std::function<void(const std::vector<Diagnostic*>&)>;

// QuietDiagnosticsReporter — diagnostics.go:122.
void QuietDiagnosticsReporter(const std::vector<Diagnostic*>& diagnostics);

// CreateReportErrorSummary — diagnostics.go:124.
DiagnosticsReporter CreateReportErrorSummary(System* sys,
                                             locale::Locale locale,
                                             const CompilerOptions* options);

// CreateBuilderStatusReporter — diagnostics.go:133.
DiagnosticReporter CreateBuilderStatusReporter(System* sys, std::ostream* w,
                                               locale::Locale locale,
                                               const CompilerOptions* options,
                                               CommandLineTesting* testing);

// CreateWatchStatusReporter — diagnostics.go:152.
DiagnosticReporter CreateWatchStatusReporter(System* sys,
                                             locale::Locale locale,
                                             const CompilerOptions* options,
                                             CommandLineTesting* testing);

}  // namespace tsc::execute::tsc
