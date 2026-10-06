// uptodatestatus.cpp — port of tsc/internal/execute/build/uptodatestatus.go.
#include "internal/execute/build/build.h"

namespace tsc::execute::build {

// uptodatestatus.go:80 isError.
bool upToDateStatus::isError() const {
	switch (kind) {
	case upToDateStatusType::ConfigFileNotFound:
	case upToDateStatusType::BuildErrors:
	case upToDateStatusType::UpstreamErrors:
		return true;
	default:
		return false;
	}
}

// uptodatestatus.go:91 isPseudoBuild.
bool upToDateStatus::isPseudoBuild() const {
	switch (kind) {
	case upToDateStatusType::UpToDateWithUpstreamTypes:
	case upToDateStatusType::UpToDateWithInputFileText:
		return true;
	default:
		return false;
	}
}

// uptodatestatus.go:101 inputOutputFileAndTime.
inputOutputFileAndTime* upToDateStatus::inputOutputFileAndTimeData() {
	return std::get_if<inputOutputFileAndTime>(&data);
}

// uptodatestatus.go:109 inputOutputName.
inputOutputName* upToDateStatus::inputOutputNameData() {
	return std::get_if<inputOutputName>(&data);
}

// uptodatestatus.go:117 oldestOutputFileName.
std::string upToDateStatus::oldestOutputFileName() {
	if (!isPseudoBuild() && kind != upToDateStatusType::UpToDate) {
		TSC_UNREACHABLE(
		    "only valid for up to date status of pseudo-build or up to date");
	}

	if (auto* p = inputOutputFileAndTimeData()) {
		return p->output.file;
	}
	if (auto* p = inputOutputNameData()) {
		return p->output;
	}
	return std::get<std::string>(data);
}

// uptodatestatus.go:131 upstreamErrors — Go panics on the wrong type;
// std::get throwing is the same crash.
upstreamErrors* upToDateStatus::upstreamErrorsData() {
	return &std::get<upstreamErrors>(data);
}

} // namespace tsc::execute::build
