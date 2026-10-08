// Port of tsc/internal/tsoptions/errors.go.
#include "internal/tsoptions/tsoptions.h"

#include "internal/diagnostics/messages_generated.h"
#include "internal/scanner/scanner.h"

namespace tsc::tsoptions {

// errors.go:92 — note: unconditionally skipTrivia (no nodeIsMissing check,
// matching Go's CreateDiagnosticForNodeInSourceFile exactly).
Diagnostic* CreateDiagnosticForNodeInSourceFile(
    SourceFile* sourceFile, Node* node, const DiagnosticMessage* message,
    std::vector<std::string> args) {
	// Go panics on a nil *ast.Node (node.Loc.Pos()) or nil *ast.SourceFile
	// (sourceFile.Text()) — e.g. tsconfigparsing reaches this with a nil
	// nodeValue for duplicate/malformed "files" properties. The port's
	// panic mechanism is tscUnreachable, so map the nil derefs there rather
	// than letting them hit an uninstrumented SIGSEGV.
	if (sourceFile == nullptr || node == nullptr) {
		tscUnreachable(
		    "CreateDiagnosticForNodeInSourceFile: nil sourceFile/node");
	}
	return newDiagnostic(
	    sourceFile,
	    TextRange{tsc::skipTrivia(sourceFile->text, node->pos()),
	              node->end()},
	    message, args);
}

Diagnostic* CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
    SourceFile* sourceFile, Node* node, const DiagnosticMessage* message,
    std::vector<std::string> args) {
	if (sourceFile != nullptr && node != nullptr) {
		return CreateDiagnosticForNodeInSourceFile(sourceFile, node, message,
		                                           std::move(args));
	}
	return newCompilerDiagnostic(message, args);
}

Diagnostic* createDiagnosticForInvalidEnumType(const CommandLineOption* opt,
                                             SourceFile* sourceFile,
                                             Node* node) {
	std::vector<std::string> namesOfType;
	if (auto* m = opt->EnumMap()) {
		namesOfType = m->Keys();
	}
	std::string stringNames = formatEnumTypeKeys(opt, namesOfType);
	std::string optName = "--" + opt->Name;
	return CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
	    sourceFile, node,
	    Argument_for_0_option_must_be_Colon_1,
	    {optName, stringNames});
}

std::string formatEnumTypeKeys(const CommandLineOption* opt,
                               const std::vector<std::string>& keysIn) {
	std::vector<std::string> keys = keysIn;
	if (const auto* deprecated = opt->DeprecatedKeys()) {
		std::vector<std::string> filtered;
		for (const auto& key : keys) {
			if (!deprecated->Has(key)) filtered.push_back(key);
		}
		keys = std::move(filtered);
	}
	std::string out = "'";
	for (size_t i = 0; i < keys.size(); i++) {
		if (i != 0) out += "', '";
		out += keys[i];
	}
	out += "'";
	return out;
}

std::string getCompilerOptionValueTypeString(
    const CommandLineOption* option) {
	if (option->Kind == CommandLineOptionTypeListOrElement) {
		return std::string(
		           getCompilerOptionValueTypeString(option->Elements())) +
		       " or Array";
	}
	if (option->Kind == CommandLineOptionTypeList) {
		return "Array";
	}
	return std::string(option->Kind);
}

// errors.go:39 — commandLineParser method spelling.
Diagnostic* commandLineParser::createUnknownOptionError(
    std::string_view unknownOption,
    std::string_view unknownOptionErrorText, Node* node,
    SourceFile* sourceFile) {
	return tsc::tsoptions::createUnknownOptionError(
	    unknownOption, UnknownOptionDiagnostic(), unknownOptionErrorText,
	    node, sourceFile, AlternateMode(), UnknownDidYouMeanDiagnostic(),
	    commandLineOptionsToMap(
	        *workerDiagnostics->didYouMean.OptionDeclarations));
}

// createUnknownOptionError creates a diagnostic for an unknown option. If
// unknownDidYouMeanDiagnostic and optionsNameMap are provided, it also checks
// for a spelling suggestion and emits a "did you mean" diagnostic instead.
Diagnostic* createUnknownOptionError(
    std::string_view unknownOption,
    const DiagnosticMessage* unknownOptionDiagnostic,
    std::string_view unknownOptionErrorTextIn, Node* node,
    SourceFile* sourceFile, AlternateModeDiagnostics* alternateMode,
    const DiagnosticMessage* unknownDidYouMeanDiagnostic,
    CommandLineOptionNameMap optionsNameMap) {
	std::string_view unknownOptionErrorText = unknownOptionErrorTextIn;
	if (alternateMode != nullptr && alternateMode->optionsNameMap != nullptr) {
		const CommandLineOption* otherOption =
		    alternateMode->optionsNameMap->Get(unknownOption);
		if (otherOption != nullptr) {
			// tscbuildoption
			const DiagnosticMessage* diagnostic = alternateMode->diagnostic;
			if (otherOption->Name == "build") {
				diagnostic =
				    Option_build_must_be_the_first_command_line_argument;
			}
			return CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
			    sourceFile, node, diagnostic,
			    {std::string(unknownOption)});
		}
	}
	if (unknownOptionErrorText.empty()) {
		unknownOptionErrorText = unknownOption;
	}
	if (unknownDidYouMeanDiagnostic != nullptr) {
		if (const CommandLineOption* possibleOption =
		        optionsNameMap.GetSpellingSuggestion(unknownOption);
		    possibleOption != nullptr) {
			return CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
			    sourceFile, node, unknownDidYouMeanDiagnostic,
			    {std::string(unknownOptionErrorText), possibleOption->Name});
		}
	}
	return CreateDiagnosticForNodeInSourceFileOrCompilerDiagnostic(
	    sourceFile, node, unknownOptionDiagnostic,
	    {std::string(unknownOptionErrorText)});
}

const DiagnosticMessage* extraKeyDiagnostics(std::string_view s) {
	if (s == "compilerOptions") {
		return Unknown_compiler_option_0;
	}
	if (s == "watchOptions") {
		return Unknown_watch_option_0;
	}
	if (s == "typeAcquisition") {
		return Unknown_type_acquisition_option_0;
	}
	if (s == "buildOptions") {
		return Unknown_build_option_0;
	}
	return nullptr;
}

const DiagnosticMessage* extraKeyDidYouMeanDiagnostics(std::string_view s) {
	if (s == "compilerOptions") {
		return Unknown_compiler_option_0_Did_you_mean_1;
	}
	if (s == "watchOptions") {
		return Unknown_watch_option_0_Did_you_mean_1;
	}
	if (s == "typeAcquisition") {
		return Unknown_type_acquisition_option_0_Did_you_mean_1;
	}
	if (s == "buildOptions") {
		return Unknown_build_option_0_Did_you_mean_1;
	}
	return nullptr;
}

}  // namespace tsc::tsoptions
