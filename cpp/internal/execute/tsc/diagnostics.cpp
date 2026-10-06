// diagnostics.go — port of tsc/internal/execute/tsc/diagnostics.go.

#include "internal/execute/tsc/diagnostics.h"

#include <ctime>
#include <memory>

namespace tsc::execute::tsc {
namespace {

// formatGoTime — Go `time.Time.Format("03:04:05 PM")` (12-hour clock with
// uppercase AM/PM).
std::string formatGoTime(vfs::TimePoint t) {
	std::time_t tt = std::chrono::system_clock::to_time_t(t);
	std::tm tm{};
	localtime_r(&tt, &tm);
	char buf[16];
	std::strftime(buf, sizeof buf, "%I:%M:%S %p", &tm);
	return buf;
}

}  // namespace

// getFormatOptsOfSys — diagnostics.go:13.
diagnosticwriter::FormattingOptions getFormatOptsOfSys(
    System* sys, locale::Locale locale) {
	diagnosticwriter::FormattingOptions formatOpts;
	formatOpts.newLine = "\n";
	formatOpts.comparePathsOptions.currentDirectory = sys->GetCurrentDirectory();
	formatOpts.comparePathsOptions.useCaseSensitiveFileNames =
	    sys->fs()->UseCaseSensitiveFileNames();
	formatOpts.locale = locale;
	return formatOpts;
}

// QuietDiagnosticReporter — diagnostics.go:27.
void QuietDiagnosticReporter(Diagnostic* diagnostic) {}

// CreateDiagnosticReporter — diagnostics.go:29.
DiagnosticReporter CreateDiagnosticReporter(System* sys, std::ostream* w,
                                            locale::Locale locale,
                                            const CompilerOptions* options) {
	if (tristateIsTrue(options->Quiet)) {
		return QuietDiagnosticReporter;
	}
	auto formatOpts = getFormatOptsOfSys(sys, locale);
	if (shouldBePretty(sys, options)) {
		return [w, formatOpts](Diagnostic* diagnostic) {
			auto wrapped = std::unique_ptr<diagnosticwriter::Diagnostic>(
			    diagnosticwriter::wrapASTDiagnostic(diagnostic));
			diagnosticwriter::formatDiagnosticWithColorAndContext(
			    *w, *wrapped, &formatOpts);
			*w << formatOpts.newLine;
		};
	}
	return [w, formatOpts](Diagnostic* diagnostic) {
		auto wrapped = std::unique_ptr<diagnosticwriter::Diagnostic>(
		    diagnosticwriter::wrapASTDiagnostic(diagnostic));
		diagnosticwriter::writeFormatDiagnostic(*w, *wrapped, &formatOpts);
	};
}

// defaultIsPretty — diagnostics.go:44.
bool defaultIsPretty(System* sys) {
	if (auto [forceColor, ok] = sys->GetEnvironmentVariable("FORCE_COLOR");
	    ok) {
		if (forceColor.empty() || forceColor == "1" || forceColor == "2" ||
		    forceColor == "3" || forceColor == "true") {
			return true;
		}
		return false;
	}
	if (auto [noColor, _] = sys->GetEnvironmentVariable("NO_COLOR");
	    !noColor.empty()) {
		return false;
	}
	if (auto [term, _] = sys->GetEnvironmentVariable("TERM"); term == "dumb") {
		return false;
	}
	return sys->WriteOutputIsTTY();
}

// shouldBePretty — diagnostics.go:62.
bool shouldBePretty(System* sys, const CompilerOptions* options) {
	if (options == nullptr || (options->Pretty == Tristate::Unknown)) {
		return defaultIsPretty(sys);
	}
	return tristateIsTrue(options->Pretty);
}

// createColors — diagnostics.go:78.
colors createColors(System* sys) {
	if (!defaultIsPretty(sys)) {
		return {};
	}

	auto [os, _1] = sys->GetEnvironmentVariable("OS");
	std::string osLower = os;
	for (auto& c : osLower) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	bool isWindows = osLower.find("windows") != std::string::npos;
	auto [wtSession, _2] = sys->GetEnvironmentVariable("WT_SESSION");
	auto [termProgram, _3] = sys->GetEnvironmentVariable("TERM_PROGRAM");
	auto [colorTerm, _4] = sys->GetEnvironmentVariable("COLORTERM");
	auto [term, _5] = sys->GetEnvironmentVariable("TERM");

	colors c;
	c.showColors = true;
	c.isWindows = isWindows;
	c.isWindowsTerminal = !wtSession.empty();
	c.isVSCode = termProgram == "vscode";
	c.supportsRicherColors =
	    colorTerm == "truecolor" || term == "xterm-256color";
	return c;
}

// colors.bold — diagnostics.go:95.
std::string colors::bold(std::string_view str) const {
	if (!showColors) {
		return std::string(str);
	}
	return "\x1b[1m" + std::string(str) + "\x1b[22m";
}

// colors.blue — diagnostics.go:102.
std::string colors::blue(std::string_view str) const {
	if (!showColors) {
		return std::string(str);
	}

	// Effectively Powershell and Command prompt users use cyan instead
	// of blue because the default theme doesn't show blue with enough contrast.
	if (isWindows && !isWindowsTerminal && !isVSCode) {
		return brightWhite(str);
	}
	return "\x1b[94m" + std::string(str) + "\x1b[39m";
}

// colors.blueBackground — diagnostics.go:113.
std::string colors::blueBackground(std::string_view str) const {
	if (!showColors) {
		return std::string(str);
	}
	if (supportsRicherColors) {
		return "\x1B[48;5;68m" + std::string(str) + "\x1B[39;49m";
	}
	return "\x1b[44m" + std::string(str) + "\x1B[39;49m";
}

// colors.brightWhite — diagnostics.go:124.
std::string colors::brightWhite(std::string_view str) const {
	if (!showColors) {
		return std::string(str);
	}
	return "\x1b[97m" + std::string(str) + "\x1b[39m";
}

// QuietDiagnosticsReporter — diagnostics.go:122.
void QuietDiagnosticsReporter(const std::vector<Diagnostic*>& diagnostics) {}

// CreateReportErrorSummary — diagnostics.go:124.
DiagnosticsReporter CreateReportErrorSummary(System* sys,
                                             locale::Locale locale,
                                             const CompilerOptions* options) {
	if (shouldBePretty(sys, options)) {
		auto formatOpts = getFormatOptsOfSys(sys, locale);
		return [sys, formatOpts](const std::vector<Diagnostic*>& diagnostics) {
			auto wrapped = diagnosticwriter::fromASTDiagnostics(diagnostics);
			std::vector<diagnosticwriter::Diagnostic*> ptrs;
			ptrs.reserve(wrapped.size());
			for (auto& d : wrapped) {
				ptrs.push_back(d.get());
			}
			diagnosticwriter::writeErrorSummaryText(*sys->Writer(), ptrs,
			                                      &formatOpts);
		};
	}
	return QuietDiagnosticsReporter;
}

// CreateBuilderStatusReporter — diagnostics.go:133.
DiagnosticReporter CreateBuilderStatusReporter(System* sys, std::ostream* w,
                                               locale::Locale locale,
                                               const CompilerOptions* options,
                                               CommandLineTesting* testing) {
	if (tristateIsTrue(options->Quiet)) {
		return QuietDiagnosticReporter;
	}

	auto formatOpts = getFormatOptsOfSys(sys, locale);
	auto writeStatus = shouldBePretty(sys, options)
	                       ? &diagnosticwriter::
	                             formatDiagnosticsStatusWithColorAndTime
	                       : &diagnosticwriter::formatDiagnosticsStatusAndTime;
	return [sys, w, formatOpts, writeStatus, testing](Diagnostic* diagnostic) {
		auto writerDiagnostic = std::unique_ptr<diagnosticwriter::Diagnostic>(
		    diagnosticwriter::wrapASTDiagnostic(diagnostic));
		if (testing != nullptr) {
			testing->OnBuildStatusReportStart(w);
		}
		writeStatus(*w, formatGoTime(sys->Now()), *writerDiagnostic,
		            &formatOpts);
		*w << formatOpts.newLine << formatOpts.newLine;
		if (testing != nullptr) {
			testing->OnBuildStatusReportEnd(w);
		}
	};
}

// CreateWatchStatusReporter — diagnostics.go:152.
DiagnosticReporter CreateWatchStatusReporter(System* sys,
                                             locale::Locale locale,
                                             const CompilerOptions* options,
                                             CommandLineTesting* testing) {
	auto formatOpts = getFormatOptsOfSys(sys, locale);
	auto writeStatus = shouldBePretty(sys, options)
	                       ? &diagnosticwriter::
	                             formatDiagnosticsStatusWithColorAndTime
	                       : &diagnosticwriter::formatDiagnosticsStatusAndTime;
	return [sys, formatOpts, writeStatus, options, testing](
	           Diagnostic* diagnostic) {
		auto writerDiagnostic =
		    std::unique_ptr<diagnosticwriter::Diagnostic>(
		        diagnosticwriter::wrapASTDiagnostic(diagnostic));
		auto* writer = sys->Writer();
		if (testing != nullptr) {
			testing->OnWatchStatusReportStart();
		}
		diagnosticwriter::tryClearScreen(*writer, *writerDiagnostic, *options);
		writeStatus(*writer, formatGoTime(sys->Now()), *writerDiagnostic,
		            &formatOpts);
		*writer << formatOpts.newLine << formatOpts.newLine;
		if (testing != nullptr) {
			testing->OnWatchStatusReportEnd();
		}
	};
}

}  // namespace tsc::execute::tsc
