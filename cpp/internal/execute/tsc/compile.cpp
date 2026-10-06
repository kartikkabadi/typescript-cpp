// compile.go — port of tsc/internal/execute/tsc/compile.go.

#include "internal/execute/tsc/compile.h"

namespace tsc::execute::tsc {

// newContentMapperLogger — compile.go:40.
contentmapper::Logger newContentMapperLogger(System* sys) {
	auto [value, _] = sys->GetEnvironmentVariable("TS_CONTENT_MAPPER_DEBUG");
	if (value.empty()) {
		return nullptr;
	}
	auto* writer = sys->ErrorWriter();
	auto mu = std::make_shared<std::mutex>();
	return [writer, mu](std::string_view message) {
		std::lock_guard<std::mutex> lock(*mu);
		*writer << message << '\n';
	};
}

// NewContentMapperHost — compile.go:90.
std::shared_ptr<contentmapper::Host>
NewContentMapperHost(gostd::Context ctx, System* sys,
                     const CompilerOptions* options) {
	if (!tristateIsTrue(options->RunExternalCode)) {
		return nullptr;
	}
	auto [diagnosticLocale, _] = locale::parse(options->Locale);
	contentmapper::HostOptions hostOptions;
	hostOptions.Logger = newContentMapperLogger(sys);
	return contentmapper::NewHostWithOptions(ctx, sys, diagnosticLocale,
	                                       std::move(hostOptions));
}

}  // namespace tsc::execute::tsc
