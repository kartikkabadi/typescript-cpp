// error_baseline.go — the squiggle-marked error baseliner:
// DoErrorBaseline, minimalDiagnosticsToString, GetErrorBaseline,
// iterateErrorBaseline, formatLocation.
#include <algorithm>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/core/text.h"
#include "internal/diagnosticwriter/diagnosticwriter.h"
#include "internal/locale/locale.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/harnessutil/harnessutil.h"
#include "internal/testutil/tsbaseline/tsbaseline.h"
#include "internal/testutil/tsbaseline/tsbaselineutil.h"
#include "internal/tspath/tspath.h"

namespace tsc::testutil::tsbaseline {

// IO
// harnessNewLine — error_baseline.go:26.
inline constexpr std::string_view harnessNewLine = "\r\n";

namespace {

// formatOpts — error_baseline.go:28.
const diagnosticwriter::FormattingOptions* formatOpts() {
	static const auto* v = [] {
		auto* o = new diagnosticwriter::FormattingOptions();
		o->newLine = std::string(harnessNewLine);
		return o;
	}();
	return v;
}

// diagnosticsLocationPrefix / diagnosticsLocationPattern —
// error_baseline.go:33.
const gostd::regexp::Regexp& diagnosticsLocationPrefix() {
	static const auto* r = new gostd::regexp::Regexp(
	    "(?im)^(lib.*\\.d\\.ts)\\(\\d+,\\d+\\)");
	return *r;
}
const gostd::regexp::Regexp& diagnosticsLocationPattern() {
	static const auto* r = new gostd::regexp::Regexp(
	    "(?i)(lib.*\\.d\\.ts):\\d+:\\d+");
	return *r;
}

// formatLocation — error_baseline.go:294.
std::string formatLocation(const diagnosticwriter::FileLike* file, int pos,
                           const diagnosticwriter::FormattingOptions* fo) {
	std::ostringstream output;
	auto writeWithStyleAndReset =
	    [](std::ostream& out, std::string_view text,
	       std::string_view /*style*/) { out << text; };
	diagnosticwriter::writeLocation(output, file, pos, fo,
	                                writeWithStyleAndReset);
	return output.str();
}

// minimalDiagnosticsToString — error_baseline.go:56.
std::string minimalDiagnosticsToString(
    std::span<diagnosticwriter::Diagnostic* const> diagnostics, bool pretty) {
	std::ostringstream output;
	if (pretty) {
		diagnosticwriter::formatDiagnosticsWithColorAndContext(
		    output, diagnostics, formatOpts());
	} else {
		diagnosticwriter::writeFormatDiagnostics(output, diagnostics,
		                                         formatOpts());
	}
	return output.str();
}

// iterateErrorBaseline — error_baseline.go:80. Go is generic over
// diagnosticwriter.Diagnostic; this core handles the Diagnostic*
// instantiation (used by fourslash).
std::vector<std::string> iterateErrorBaseline(
    gostd::testing::T* t,
    const std::vector<harnessutil::TestFile*>& inputFiles,
    const std::vector<diagnosticwriter::Diagnostic*>& inputDiagnostics,
    const std::function<int(diagnosticwriter::Diagnostic*,
                            diagnosticwriter::Diagnostic*)>&
        compareDiagnostics,
    bool pretty) {
	t->Helper();
	std::vector<diagnosticwriter::Diagnostic*> diagnostics;
	diagnostics.reserve(inputDiagnostics.size());
	for (auto* d : inputDiagnostics) diagnostics.push_back(d);
	std::stable_sort(diagnostics.begin(), diagnostics.end(),
	                 compareDiagnostics);

	std::string outputLines;
	// Count up all errors that were found in files other than lib.d.ts so
	// we don't miss any
	int totalErrorsReportedInNonLibraryNonTsconfigFiles = 0;
	int errorsReported = 0;

	bool firstLine = true;
	auto newLine = [&]() -> std::string_view {
		if (firstLine) {
			firstLine = false;
			return "";
		}
		return "\r\n";
	};

	std::vector<std::string> result;

	std::function<void(diagnosticwriter::Diagnostic*)> outputErrorText;
	outputErrorText = [&](diagnosticwriter::Diagnostic* diag) {
		auto message = diagnosticwriter::flattenDiagnosticMessage(
		    *diag, harnessNewLine, locale::Default);

		std::vector<std::string> errLines;
		auto prefixed = removeTestPathPrefixes(message, false);
		size_t pos = 0;
		while (pos <= prefixed.size()) {
			auto nl = prefixed.find('\n', pos);
			std::string line = nl == std::string::npos
			                       ? prefixed.substr(pos)
			                       : prefixed.substr(pos, nl - pos);
			pos = nl == std::string::npos ? prefixed.size() + 1 : nl + 1;
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.empty()) continue;
			errLines.push_back(
			    gostd::sprintf("!!! %s TS%d: %s",
			                   {tsc::categoryName(diag->category()),
			                    (int64_t)diag->code(), line}));
		}

		for (auto* info : diag->relatedInformation()) {
			std::string location;
			if (info->file() != nullptr) {
				location = " " +
				           formatLocation(info->file(), info->pos(),
				                          formatOpts());
			}
			location = removeTestPathPrefixes(location, false);
			if (!location.empty() &&
			    isDefaultLibraryFile(info->file()->fileName())) {
				location = diagnosticsLocationPattern().ReplaceAllString(
				    location, "$1:--:--");
			}
			errLines.push_back(
			    gostd::sprintf("!!! related TS%d%s: %s",
			                   {(int64_t)info->code(), location,
			                    diagnosticwriter::flattenDiagnosticMessage(
			                        *info, harnessNewLine, locale::Default)}));
		}

		for (auto& e : errLines) {
			outputLines += newLine();
			outputLines += e;
		}

		errorsReported += 1;

		// do not count errors from lib.d.ts here, they are computed
		// separately as numLibraryDiagnostics
		// if lib.d.ts is explicitly included in input files and there are
		// some errors in it (i.e. because of duplicate identifiers)
		// then they will be added twice thus triggering 'total errors'
		// assertion with condition
		// Similarly for tsconfig, which may be in the input files and
		// contain errors.
		// 'totalErrorsReportedInNonLibraryNonTsconfigFiles +
		//  numLibraryDiagnostics + numTsconfigDiagnostics,
		//  diagnostics.length'
		if (diag->file() == nullptr ||
		    (!isDefaultLibraryFile(diag->file()->fileName()) &&
		     !isTsConfigFile(diag->file()->fileName()))) {
			totalErrorsReportedInNonLibraryNonTsconfigFiles += 1;
		}
	};

	auto topDiagnostics =
	    minimalDiagnosticsToString(diagnostics, pretty);
	topDiagnostics = removeTestPathPrefixes(topDiagnostics, false);
	topDiagnostics = diagnosticsLocationPrefix().ReplaceAllString(
	    topDiagnostics, "$1(--,--)");

	result.push_back(topDiagnostics + std::string(harnessNewLine) +
	                 std::string(harnessNewLine));

	// Report global errors
	for (auto* error : diagnostics) {
		if (error->file() == nullptr) {
			outputErrorText(error);
		}
	}

	result.push_back(outputLines);
	outputLines.clear();
	errorsReported = 0;

	// 'merge' the lines of each input file with any errors associated with
	// it
	std::map<std::string, int> dupeCase;
	for (auto* inputFile : inputFiles) {
		// Filter down to the errors in the file
		auto inputPath = removeTestPathPrefixes(inputFile->UnitName, false);
		std::vector<diagnosticwriter::Diagnostic*> fileErrors;
		for (auto* e : diagnostics) {
			if (e->file() != nullptr &&
			    tspath::comparePaths(
			        removeTestPathPrefixes(e->file()->fileName(), false),
			        inputPath, tspath::ComparePathsOptions{}) == 0) {
				fileErrors.push_back(e);
			}
		}

		// Header
		outputLines += gostd::sprintf(
		    "%s==== %s (%d errors) ====",
		    {newLine(), removeTestPathPrefixes(inputFile->UnitName, false),
		     (int64_t)fileErrors.size()});

		// Make sure we emit something for every error
		int markedErrorCount = 0;
		// For each line, emit the line followed by any error squiggles
		// matching this line

		auto lineStarts = computeECMALineStarts(inputFile->Content);
		auto lines = lineDelimiter().Split(inputFile->Content, -1);

		for (size_t lineIndex = 0; lineIndex < lines.size(); lineIndex++) {
			std::string line = lines[lineIndex];
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}

			int thisLineStart = lineStarts[lineIndex];
			int nextLineStart;
			// On the last line of the file, fake the next line start
			// number so that we handle errors on the last character of
			// the file correctly
			if (lineIndex == lines.size() - 1) {
				nextLineStart = (int)inputFile->Content.size();
			} else {
				nextLineStart = lineStarts[lineIndex + 1];
			}
			// Emit this line from the original file
			outputLines += newLine();
			outputLines += "    ";
			outputLines += line;
			for (auto* errDiagnostic : fileErrors) {
				// Does any error start or continue on to this line?
				// Emit squiggles
				int errStart = errDiagnostic->pos();
				int end = errStart + errDiagnostic->len();
				if (end >= thisLineStart &&
				    (errStart < nextLineStart ||
				     lineIndex == lines.size() - 1)) {
					// How many characters from the start of this line
					// the error starts at (could be positive or
					// negative)
					int relativeOffset = errStart - thisLineStart;
					// How many characters of the error are on this line
					// (might be longer than this line in reality)
					int length = (end - errStart) -
					             std::max(0, thisLineStart - errStart);
					// Calculate the start of the squiggle
					int squiggleStart = std::max(0, relativeOffset);
					// TODO/REVIEW: this doesn't work quite right in the
					// browser if a multi file test has files whose
					// names are just the right length relative to one
					// another
					outputLines += newLine();
					outputLines += "    ";
					outputLines += nonWhitespace().ReplaceAllString(
					    line.substr(0, squiggleStart), " ");
					// This was `new Array(count).join("~")`; which
					// maps 0 to "", 1 to "", 2 to "~", 3 to "~~", etc.
					int squiggleEnd = std::max(
					    squiggleStart,
					    std::min(squiggleStart + length, (int)line.size()));
					outputLines += std::string(
					    runeCountInString(line.substr(
					        squiggleStart, squiggleEnd - squiggleStart)),
					    '~');
					// If the error ended here, or we're at the end of
					// the file, emit its message
					if (lineIndex == lines.size() - 1 ||
					    nextLineStart > end) {
						outputErrorText(errDiagnostic);
						markedErrorCount += 1;
					}
				}
			}
		}

		// Verify we didn't miss any errors in this file
		if (markedErrorCount != (int)fileErrors.size()) {
			t->Errorf(
			    "assertion failed: count of errors in %s (left=%d, "
			    "right=%d)",
			    {inputFile->UnitName, (int64_t)markedErrorCount,
			     (int64_t)fileErrors.size()});
		}
		auto sanitized = sanitizeTestFilePath(inputFile->UnitName);
		bool isDupe = dupeCase.count(sanitized) != 0;
		dupeCase[sanitized] += 1;
		result.push_back(outputLines);
		if (isDupe) {
			// Case-duplicated files on a case-insensitive build will
			// have errors reported in both the dupe and the original
			// thanks to the canse-insensitive path comparison on the
			// error file path - We only want to count those errors once
			// for the assert below, so we subtract them here.
			totalErrorsReportedInNonLibraryNonTsconfigFiles -=
			    errorsReported;
		}
		outputLines.clear();
		errorsReported = 0;
	}

	auto numLibraryDiagnostics = std::count_if(
	    diagnostics.begin(), diagnostics.end(),
	    [](diagnosticwriter::Diagnostic* d) {
		    return d->file() != nullptr &&
		           (isDefaultLibraryFile(d->file()->fileName()) ||
		            isBuiltFile(d->file()->fileName()));
	    });
	auto numTsconfigDiagnostics = std::count_if(
	    diagnostics.begin(), diagnostics.end(),
	    [](diagnosticwriter::Diagnostic* d) {
		    return d->file() != nullptr &&
		           isTsConfigFile(d->file()->fileName());
	    });
	auto numContentMapperSupplementalDiagnostics = std::count_if(
	    diagnostics.begin(), diagnostics.end(),
	    [](diagnosticwriter::Diagnostic* d) {
		    // any(d).(*diagnosticwriter.ASTDiagnostic)
		    if (auto* ad =
		            dynamic_cast<diagnosticwriter::ASTDiagnostic*>(d);
		        ad != nullptr) {
			    auto* file = ad->diagnostic()->File();
			    return file != nullptr &&
			           file->IsContentMapperSupplemental();
		    }
		    // The Go second branch (asserting d.File() to
		    // *ast.SourceFile) is unreachable: the C++ FileLike is
		    // an adapter, not a SourceFile.
		    return false;
	    });
	// Verify we didn't miss any errors in total
	int total = totalErrorsReportedInNonLibraryNonTsconfigFiles +
	            (int)numLibraryDiagnostics + (int)numTsconfigDiagnostics +
	            (int)numContentMapperSupplementalDiagnostics;
	if (total != (int)diagnostics.size()) {
		t->Errorf(
		    "assertion failed: total number of errors (left=%d, "
		    "right=%d)",
		    {(int64_t)total, (int64_t)diagnostics.size()});
	}

	return result;
}

// ASTDiagnostic instantiation (error_baseline.go:80, T=*ASTDiagnostic) —
// used by testrunner.
std::vector<std::string> iterateErrorBaseline(
    gostd::testing::T* t,
    const std::vector<harnessutil::TestFile*>& inputFiles,
    const std::vector<
        std::unique_ptr<diagnosticwriter::ASTDiagnostic>>& inputDiagnostics,
    int (*compareDiagnostics)(diagnosticwriter::ASTDiagnostic*,
                              diagnosticwriter::ASTDiagnostic*),
    bool pretty) {
	std::vector<diagnosticwriter::Diagnostic*> ptrs;
	ptrs.reserve(inputDiagnostics.size());
	for (auto& d : inputDiagnostics) ptrs.push_back(d.get());
	return iterateErrorBaseline(
	    t, inputFiles, ptrs,
	    [compareDiagnostics](diagnosticwriter::Diagnostic* a,
	                         diagnosticwriter::Diagnostic* b) {
		    return compareDiagnostics(
		        static_cast<diagnosticwriter::ASTDiagnostic*>(a),
		        static_cast<diagnosticwriter::ASTDiagnostic*>(b));
	    },
	    pretty);
}

}  // namespace

// DoErrorBaseline — error_baseline.go:35.
void DoErrorBaseline(gostd::testing::T* t, const std::string& baselinePath,
                     const std::vector<harnessutil::TestFile*>& inputFiles,
                     const std::vector<Diagnostic*>& errors, bool pretty,
                     const baseline::Options& opts) {
	auto path = tsExtension().ReplaceAllString(baselinePath, ".errors.txt");
	std::string errorBaseline;
	if (!errors.empty()) {
		errorBaseline = GetErrorBaseline(
		    t, inputFiles, diagnosticwriter::wrapASTDiagnostics(errors),
		    diagnosticwriter::compareASTDiagnostics, pretty);
	} else {
		errorBaseline = std::string(baseline::NoContent);
	}
	baseline::Run(t, path, errorBaseline, opts);
	for (auto* d : errors) {
		if (d->Code() == -1) {
			t->Fatalf(
			    "Found diagnostic with code -1, which is used to log "
			    "critical assertion violations in the baseline. Inspect "
			    "and fix those failures.", {});
		}
	}
}

// getErrorBaselineImpl — shared core for the two GetErrorBaseline
// instantiations (error_baseline.go:66).
static std::string getErrorBaselineImpl(
    gostd::testing::T* t,
    const std::vector<harnessutil::TestFile*>& inputFiles,
    const std::vector<diagnosticwriter::Diagnostic*>& diagnostics,
    const std::function<int(diagnosticwriter::Diagnostic*,
                            diagnosticwriter::Diagnostic*)>&
        compareDiagnostics,
    bool pretty) {
	t->Helper();
	auto outputLines =
	    iterateErrorBaseline(t, inputFiles, diagnostics, compareDiagnostics,
	                         pretty);

	if (pretty) {
		std::ostringstream summaryBuilder;
		diagnosticwriter::writeErrorSummaryText(
		    summaryBuilder, diagnostics, formatOpts());
		auto summary = removeTestPathPrefixes(summaryBuilder.str(), false);
		outputLines.push_back(summary);
	}
	std::string out;
	for (auto& l : outputLines) out += l;
	return out;
}

// GetErrorBaseline — error_baseline.go:66. ASTDiagnostic instantiation.
std::string GetErrorBaseline(
    gostd::testing::T* t,
    const std::vector<harnessutil::TestFile*>& inputFiles,
    const std::vector<std::unique_ptr<diagnosticwriter::ASTDiagnostic>>&
        diagnostics,
    int (*compareDiagnostics)(diagnosticwriter::ASTDiagnostic*,
                              diagnosticwriter::ASTDiagnostic*),
    bool pretty) {
	std::vector<diagnosticwriter::Diagnostic*> ptrs;
	ptrs.reserve(diagnostics.size());
	for (auto& d : diagnostics) ptrs.push_back(d.get());
	return getErrorBaselineImpl(
	    t, inputFiles, ptrs,
	    [compareDiagnostics](diagnosticwriter::Diagnostic* a,
	                         diagnosticwriter::Diagnostic* b) {
		    return compareDiagnostics(
		        static_cast<diagnosticwriter::ASTDiagnostic*>(a),
		        static_cast<diagnosticwriter::ASTDiagnostic*>(b));
	    },
	    pretty);
}

// GetErrorBaseline — error_baseline.go:66. Diagnostic* instantiation
// (used by fourslash).
std::string GetErrorBaseline(
    gostd::testing::T* t,
    const std::vector<harnessutil::TestFile*>& inputFiles,
    const std::vector<diagnosticwriter::Diagnostic*>& diagnostics,
    const std::function<int(diagnosticwriter::Diagnostic*,
                            diagnosticwriter::Diagnostic*)>&
        compareDiagnostics,
    bool pretty) {
	return getErrorBaselineImpl(t, inputFiles, diagnostics,
	                            compareDiagnostics, pretty);
}

}  // namespace tsc::testutil::tsbaseline
