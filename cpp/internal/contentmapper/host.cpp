// Port of tsc/internal/contentmapper/host.go: error types and the timing
// snapshot helpers. The Host/Project interfaces live in the header.
#include "internal/contentmapper/contentmapper.h"

namespace tsc::contentmapper {

// TransformError — host.go:26.
std::string TransformError::Error() const {
	return gostd::sprintf("content mapper transform failed: %v", {err});
}

// NewTransformError — host.go:32.
TransformError* NewTransformError(TransformErrorKind kind, gostd::Error err) {
	auto* e = new TransformError;
	e->Kind = kind;
	e->err = std::move(err);
	return e;
}

// DiagnosticDirectiveError.Error — host.go:61.
std::string DiagnosticDirectiveError::Error() const {
	return gostd::sprintf("invalid content mapper diagnostic directive %d",
	                      {Index});
}

// InvalidVirtualExtensionError.Error — host.go:70.
std::string InvalidVirtualExtensionError::Error() const {
	return gostd::sprintf("invalid virtual extension %q", {Extension});
}

// ProjectError.Error — host.go:90.
std::string ProjectError::Error() const {
	switch (Kind) {
	case ProjectErrorKind::MalformedResponse:
		return "content mapper returned a malformed project response";
	case ProjectErrorKind::MissingConfigIdentity:
		return "content mapper did not return configIdentity for dynamic "
		       "configuration";
	case ProjectErrorKind::NonAbsoluteWatchedFile:
		return "content mapper returned a non-absolute path in watchedFiles";
	case ProjectErrorKind::UnexpectedConfigIdentity:
		return "content mapper returned configIdentity without declaring "
		       "dynamicConfig";
	case ProjectErrorKind::UnexpectedWatchedFiles:
		return "content mapper returned watchedFiles without declaring "
		       "dynamicConfig";
	default:
		return "content mapper returned an invalid project response";
	}
}

// InitializeError.Error — host.go:142.
std::string InitializeError::Error() const {
	switch (Kind) {
	case InitializeErrorKind::ProcessStart:
		return gostd::sprintf("could not start content mapper command %q: %s",
		                      {Command, Detail});
	case InitializeErrorKind::ProcessExit:
		return gostd::sprintf("content mapper process exited before "
		                      "initialization with code %d",
		                      {ExitCode});
	case InitializeErrorKind::NoResponse:
		return "content mapper did not respond to the initialize request";
	case InitializeErrorKind::InvalidResponse:
		return "content mapper returned an invalid initialize response: " +
		       Detail;
	case InitializeErrorKind::Request:
		return "content mapper initialize request failed: " + Detail;
	case InitializeErrorKind::PositionEncoding:
		return gostd::sprintf("unsupported position encoding %q",
		                      {PositionEncoding});
	case InitializeErrorKind::EmptyDiagnosticSource:
		return "diagnostic source must not be empty";
	case InitializeErrorKind::ReservedDiagnosticSource:
		return gostd::sprintf(
		    "diagnostic source %q is reserved by TypeScript",
		    {DiagnosticSource});
	default:
		return "content mapper initialization failed";
	}
}

// SupplementalFileCollisionError.Error — host.go:138.
std::string SupplementalFileCollisionError::Error() const {
	return gostd::sprintf(
	    "content mapper supplemental output file %q already exists",
	    {FileName});
}

namespace {

// operationTimingSince — host.go:263.
OperationTiming operationTimingSince(const OperationTiming& current,
                                     const OperationTiming& previous) {
	return OperationTiming{
	    current.Count - std::min(current.Count, previous.Count),
	    std::max(current.Duration - previous.Duration, gostd::Duration{0}),
	};
}

} // namespace

// Timings.Since — host.go:245.
Timings Timings::Since(const Timings& previous) const {
	Timings result;
	result.Mappers.reserve(Mappers.size());
	result.RequestWait =
	    std::max(RequestWait - previous.RequestWait, gostd::Duration{0});
	for (const auto& [identity, current] : Mappers) {
		MapperTimings before;
		if (auto it = previous.Mappers.find(identity);
		    it != previous.Mappers.end()) {
			before = it->second;
		}
		result.Mappers[identity] = MapperTimings{
		    operationTimingSince(current.Spawn, before.Spawn),
		    operationTimingSince(current.Initialize, before.Initialize),
		    operationTimingSince(current.OpenProject, before.OpenProject),
		    operationTimingSince(current.CloseProject, before.CloseProject),
		    operationTimingSince(current.Transform, before.Transform),
		};
	}
	return result;
}

} // namespace tsc::contentmapper
