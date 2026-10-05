// Port of tsc/internal/tsoptions/diagnostics.go — the did-you-mean
// diagnostics records, plus the commandLineParser accessors that forward to
// them (commandlineparser.go:16-30).
#include "internal/tsoptions/tsoptions.h"

#include "internal/diagnostics/messages_generated.h"

namespace tsc::tsoptions {

// getParseCommandLineWorkerDiagnostics — diagnostics.go:30. This will only
// return the correct diagnostics for `compiler` mode, and is factored into a
// function for testing reasons.
std::unique_ptr<ParseCommandLineWorkerDiagnostics>
getParseCommandLineWorkerDiagnostics(
    const std::vector<const CommandLineOption*>& decls) {
	auto result = std::make_unique<ParseCommandLineWorkerDiagnostics>();
	result->didYouMean.alternateMode = new AlternateModeDiagnostics{
	    .diagnostic =
	        Compiler_option_0_may_only_be_used_with_build,
	    .optionsNameMap = &BuildNameMap(),
	};
	result->didYouMean.OptionDeclarations = &decls;
	result->didYouMean.UnknownOptionDiagnostic =
	    Unknown_compiler_option_0;
	result->didYouMean.UnknownDidYouMeanDiagnostic =
	    Unknown_compiler_option_0_Did_you_mean_1;
	result->OptionTypeMismatchDiagnostic =
	    Compiler_option_0_expects_an_argument;
	return result;
}

ParseCommandLineWorkerDiagnostics& CompilerOptionsDidYouMeanDiagnostics() {
	static std::unique_ptr<ParseCommandLineWorkerDiagnostics> d =
	    getParseCommandLineWorkerDiagnostics(OptionsDeclarations());
	return *d;
}

ParseCommandLineWorkerDiagnostics& watchOptionsDidYouMeanDiagnostics() {
	static auto d = [] {
		auto d = std::make_unique<ParseCommandLineWorkerDiagnostics>();
		d->didYouMean.OptionDeclarations = &OptionsForWatch();
		d->didYouMean.UnknownOptionDiagnostic =
		    Unknown_watch_option_0;
		d->didYouMean.UnknownDidYouMeanDiagnostic =
		    Unknown_watch_option_0_Did_you_mean_1;
		d->OptionTypeMismatchDiagnostic =
		    Watch_option_0_requires_a_value_of_type_1;
		return d;
	}();
	return *d;
}

ParseCommandLineWorkerDiagnostics& buildOptionsDidYouMeanDiagnostics() {
	static auto d = [] {
		auto d = std::make_unique<ParseCommandLineWorkerDiagnostics>();
		d->didYouMean.alternateMode = new AlternateModeDiagnostics{
		    .diagnostic =
		        Compiler_option_0_may_not_be_used_with_build,
		    .optionsNameMap = &CompilerNameMap(),
		};
		d->didYouMean.OptionDeclarations = &BuildOpts();
		d->didYouMean.UnknownOptionDiagnostic =
		    Unknown_build_option_0;
		d->didYouMean.UnknownDidYouMeanDiagnostic =
		    Unknown_build_option_0_Did_you_mean_1;
		d->OptionTypeMismatchDiagnostic =
		    Build_option_0_requires_a_value_of_type_1;
		return d;
	}();
	return *d;
}

// commandlineparser.go:16-30 — accessors forwarding into workerDiagnostics.
AlternateModeDiagnostics* commandLineParser::AlternateMode() const {
	return workerDiagnostics->didYouMean.alternateMode;
}

const std::vector<const CommandLineOption*>*
commandLineParser::OptionsDeclarations() const {
	return workerDiagnostics->didYouMean.OptionDeclarations;
}

const DiagnosticMessage* commandLineParser::UnknownOptionDiagnostic() const {
	return workerDiagnostics->didYouMean.UnknownOptionDiagnostic;
}

const DiagnosticMessage*
commandLineParser::UnknownDidYouMeanDiagnostic() const {
	return workerDiagnostics->didYouMean.UnknownDidYouMeanDiagnostic;
}

}  // namespace tsc::tsoptions
