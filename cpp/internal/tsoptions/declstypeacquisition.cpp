// Port of tsc/internal/tsoptions/declstypeacquisition.go.
#include "internal/tsoptions/tsoptions.h"

namespace tsc::tsoptions {

// Do not delete this without updating the website's tsconfig generation.
const std::vector<const CommandLineOption*>& typeAcquisitionDecls() {
	static const std::vector<const CommandLineOption*> v = [] {
		static const CommandLineOption enable{
		    .Name = "enable",
		    .Kind = CommandLineOptionTypeBoolean,
		    .DefaultValueDescription = false,
		};
		static const CommandLineOption include{
		    .Name = "include",
		    .Kind = CommandLineOptionTypeList,
		};
		static const CommandLineOption exclude{
		    .Name = "exclude",
		    .Kind = CommandLineOptionTypeList,
		};
		static const CommandLineOption disableFilenameBasedTypeAcquisition{
		    .Name = "disableFilenameBasedTypeAcquisition",
		    .Kind = CommandLineOptionTypeBoolean,
		    .DefaultValueDescription = false,
		};
		return std::vector<const CommandLineOption*>{
			&enable,
			&include,
			&exclude,
			&disableFilenameBasedTypeAcquisition,
		};
	}();
	return v;
}

// typeAcquisitionDeclaration — declstypeacquisition.go:3.
const CommandLineOption& typeAcquisitionDeclaration() {
	static const CommandLineOption o = [] {
		CommandLineOption o{
		    .Name = "typeAcquisition",
		    .Kind = CommandLineOptionTypeObject,
		};
		o.ElementOptions = commandLineOptionsToMap(typeAcquisitionDecls());
		return o;
	}();
	return o;
}

}  // namespace tsc::tsoptions
