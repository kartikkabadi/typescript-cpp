// Port of tsc/internal/tsoptions/commandlineparser.go.
#include "internal/tsoptions/tsoptions.h"

#include <charconv>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/module/types.h"
#include "internal/tspath/tspath.h"

namespace tsc::tsoptions {

namespace {

// strconv.Atoi — signed decimal parse; trailing junk or overflow fails.
std::pair<int64_t, bool> atoiGo(std::string_view s) {
	if (s.empty()) {
		return {0, false};
	}
	if (s.front() == '+') {
		s.remove_prefix(1);
	}
	int64_t value = 0;
	auto* first = s.data();
	auto* last = s.data() + s.size();
	auto [ptr, ec] = std::from_chars(first, last, value);
	if (ec != std::errc() || ptr != last) {
		return {0, false};
	}
	return {value, true};
}

std::string itoaGo(int n) { return std::to_string(n); }

// strings.TrimSpace — ASCII-whitespace trim matching Go's unicode.IsSpace
// set for the chars we can encounter here.
std::string_view trimSpaceSv(std::string_view s) {
	auto isSpace = [](char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
		       c == '\r';
	};
	while (!s.empty() && isSpace(s.front())) {
		s.remove_prefix(1);
	}
	while (!s.empty() && isSpace(s.back())) {
		s.remove_suffix(1);
	}
	return s;
}

// stringutil.IsWhiteSpaceLike for a string (TrimFunc): strip leading/trailing
// whitespace-like runes. JSON/config inputs are ASCII here.
std::string trimWhiteSpaceLike(std::string_view s) {
	return std::string(trimSpaceSv(s));
}

// utf8 decode of a string into runes (only used to index quoted sections
// byte-exactly like Go's []rune iteration).
std::vector<char32_t> toRunes(std::string_view s) {
	std::vector<char32_t> out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size();) {
		unsigned char c = s[i];
		if (c < 0x80) {
			out.push_back(c);
			i += 1;
		} else if ((c & 0xE0) == 0xC0) {
			out.push_back(((c & 0x1F) << 6) | (s[i + 1] & 0x3F));
			i += 2;
		} else if ((c & 0xF0) == 0xE0) {
			out.push_back(((c & 0x0F) << 12) |
			              ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F));
			i += 3;
		} else {
			out.push_back(((c & 0x07) << 18) |
			              ((s[i + 1] & 0x3F) << 12) |
			              ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F));
			i += 4;
		}
	}
	return out;
}

std::string fromRunes(const char32_t* begin, const char32_t* end) {
	std::string out;
	for (auto* p = begin; p < end; p++) {
		char32_t c = *p;
		if (c < 0x80) {
			out += (char)c;
		} else if (c < 0x800) {
			out += (char)(0xC0 | (c >> 6));
			out += (char)(0x80 | (c & 0x3F));
		} else if (c < 0x10000) {
			out += (char)(0xE0 | (c >> 12));
			out += (char)(0x80 | ((c >> 6) & 0x3F));
			out += (char)(0x80 | (c & 0x3F));
		} else {
			out += (char)(0xF0 | (c >> 18));
			out += (char)(0x80 | ((c >> 12) & 0x3F));
			out += (char)(0x80 | ((c >> 6) & 0x3F));
			out += (char)(0x80 | (c & 0x3F));
		}
	}
	return out;
}

}  // namespace

// ParseCommandLine — commandlineparser.go:43.
ParsedCommandLine* ParseCommandLine(
    const std::vector<std::string>& commandLine, ParseConfigHost* host) {
	std::vector<std::string> args = commandLine;
	commandLineParser* parser = parseCommandLineWorker(
	    &CompilerOptionsDidYouMeanDiagnostics(), args, host->FS(),
	    host->GetCurrentDirectory());
	JsonObjectPtr options = convertToOptionsWithAbsolutePaths(
	    std::make_shared<JsonObject>(parser->options->Clone()),
	    CommandLineCompilerOptionsMap(), host->GetCurrentDirectory());
	tsc::CompilerOptions* compilerOptions =
	    convertMapToOptions(
	        options,
	        new compilerOptionsParser(new tsc::CompilerOptions{}))
	        ->CompilerOptions;
	ParsedCommandLine* result = NewParsedCommandLine(
	    compilerOptions, parser->fileNames, {},
	    tspath::ComparePathsOptions{
	        .useCaseSensitiveFileNames =
	            host->FS()->UseCaseSensitiveFileNames(),
	        .currentDirectory = host->GetCurrentDirectory(),
	    });
	result->Errors = parser->errors;
	result->Raw = parser->options;
	return result;
}

// ParseBuildCommandLine — commandlineparser.go:64.
ParsedBuildCommandLine* ParseBuildCommandLine(
    const std::vector<std::string>& commandLine, ParseConfigHost* host) {
	std::vector<std::string> args = commandLine;
	commandLineParser* parser = parseCommandLineWorker(
	    &buildOptionsDidYouMeanDiagnostics(), args, host->FS(),
	    host->GetCurrentDirectory());
	auto* compilerOptions = new tsc::CompilerOptions{};
	for (const auto& key : parser->options->Keys()) {
		const CommandLineOption* buildOption = BuildNameMap().Get(key);
		if (buildOption == &TscBuildOption() ||
		    buildOption == CompilerNameMap().Get(key)) {
			ParseCompilerOptions(
			    key, *parser->options->Get(key).first, compilerOptions);
		}
	}
	auto* result = new ParsedBuildCommandLine{
	    .BuildOptions =
	        convertMapToOptions(
	            parser->options,
	            new buildOptionsParser(new BuildOptions{}))
	            ->BuildOptions,
	    .CompilerOptions = compilerOptions,
	    .Projects = parser->fileNames,
	    .Errors = parser->errors,
	    .Raw = parser->options,
	};
	result->comparePathsOptions = tspath::ComparePathsOptions{
	    .useCaseSensitiveFileNames =
	        host->FS()->UseCaseSensitiveFileNames(),
	    .currentDirectory = host->GetCurrentDirectory(),
	};

	if (result->Projects.empty()) {
		// tsc -b invoked with no extra arguments; act as if invoked with
		// "tsc -b ."
		result->Projects.push_back(".");
	}

	// Nonsensical combinations
	if (result->BuildOptions->Clean == Tristate::True && result->BuildOptions->Force == Tristate::True) {
		result->Errors.push_back(newCompilerDiagnostic(
		    Options_0_and_1_cannot_be_combined, {"clean", "force"}));
	}
	if (result->BuildOptions->Clean == Tristate::True && result->BuildOptions->Verbose == Tristate::True) {
		result->Errors.push_back(newCompilerDiagnostic(
		    Options_0_and_1_cannot_be_combined, {"clean", "verbose"}));
	}
	if (result->BuildOptions->Clean == Tristate::True && result->CompilerOptions->Watch == Tristate::True) {
		result->Errors.push_back(newCompilerDiagnostic(
		    Options_0_and_1_cannot_be_combined, {"clean", "watch"}));
	}
	if (result->CompilerOptions->Watch == Tristate::True && result->BuildOptions->Dry == Tristate::True) {
		result->Errors.push_back(newCompilerDiagnostic(
		    Options_0_and_1_cannot_be_combined, {"watch", "dry"}));
	}

	return result;
}

// parseCommandLineWorker — commandlineparser.go:115.
commandLineParser* parseCommandLineWorker(
    ParseCommandLineWorkerDiagnostics* parseCommandLineWithDiagnostics,
    const std::vector<std::string>& commandLine,
    module::ResolutionHost* fs, std::string_view currentDirectory) {
	auto* parser = new commandLineParser{
	    .workerDiagnostics = parseCommandLineWithDiagnostics,
	    .fs = fs,
	    .currentDirectory = std::string(currentDirectory),
	    .options = std::make_shared<JsonObject>(),
	    .fileNames = {},
	    .errors = {},
	};
	parser->optionsMap =
	    GetNameMapFromList(*parser->OptionsDeclarations());
	parser->parseStrings(commandLine);
	return parser;
}

// parseStrings — commandlineparser.go:134.
void commandLineParser::parseStrings(
    const std::vector<std::string>& args) {
	size_t i = 0;
	while (i < args.size()) {
		const std::string& s = args[i];
		i++;
		if (s.empty()) {
			continue;
		}
		switch (s[0]) {
		case '@':
			parseResponseFile(std::string_view(s).substr(1));
			break;
		case '-': {
			std::string inputOptionName = getInputOptionName(s);
			const CommandLineOption* opt =
			    optionsMap->GetOptionDeclarationFromName(inputOptionName,
			                                             true /*allowShort*/);
			if (opt != nullptr) {
				i = parseOptionValue(
				    args, (int)i, opt,
				    workerDiagnostics->OptionTypeMismatchDiagnostic);
			} else {
				errors.push_back(createUnknownOptionError(
				    inputOptionName, s, nullptr, nullptr));
			}
			break;
		}
		default:
			fileNames.push_back(s);
		}
	}
}

// getInputOptionName — commandlineparser.go:164.
std::string getInputOptionName(std::string_view input) {
	// removes at most two leading '-' from the input string
	if (input.starts_with('-')) {
		input.remove_prefix(1);
	}
	if (input.starts_with('-')) {
		input.remove_prefix(1);
	}
	return std::string(input);
}

// parseResponseFile — commandlineparser.go:169.
void commandLineParser::parseResponseFile(std::string_view fileName) {
	std::string fileNameStr = tspath::getNormalizedAbsolutePath(
	    fileName, currentDirectory);
	tspath::Path path = tspath::toPath(fileNameStr, currentDirectory,
	                                   fs->UseCaseSensitiveFileNames());
	if (responseFileStack.Has(path)) {
		return;
	}
	responseFileStack.Add(path);
	struct deferDelete {
		collections::Set<tspath::Path>* s;
		const tspath::Path& p;
		~deferDelete() { s->Delete(p); }
	} _defer{&responseFileStack, path};

	auto [fileContents, errors2] = tryReadFile(
	    fileNameStr,
	    [this](std::string_view name) -> std::pair<std::string, bool> {
		    if (fs == nullptr) {
			    return {"", false};
		    }
		    auto read = fs->ReadFile(name);
		    return {read.value_or(""), read.has_value()};
	    },
	    errors);
	errors = errors2;

	if (fileContents.empty()) {
		return;
	}

	std::vector<std::string> args;
	std::vector<char32_t> text = toRunes(fileContents);
	size_t textLength = text.size();
	size_t pos = 0;
	while (pos < textLength) {
		while (pos < textLength && text[pos] <= U' ') {
			pos++;
		}
		if (pos >= textLength) {
			break;
		}
		size_t start = pos;
		if (text[pos] == U'"') {
			pos++;
			while (pos < textLength && text[pos] != U'"') {
				pos++;
			}
			if (pos < textLength) {
				args.push_back(
				    fromRunes(&text[start + 1], &text[pos]));
				pos++;
			} else {
				errors.push_back(newCompilerDiagnostic(
				    Unterminated_quoted_string_in_response_file_0,
				    {fileNameStr}));
			}
		} else {
			while (pos < textLength && text[pos] > U' ') {
				pos++;
			}
			args.push_back(fromRunes(&text[start], &text[pos]));
		}
	}
	parseStrings(args);
}

// tryReadFile — commandlineparser.go:224.
std::pair<std::string, std::vector<Diagnostic*>> tryReadFile(
    std::string_view fileName,
    const std::function<std::pair<std::string, bool>(std::string_view)>& readFile,
    std::vector<Diagnostic*> errors) {
	// this function adds a compiler diagnostic if the file cannot be read
	auto [text, e] = readFile(fileName);

	if (!e) {
		// !!! Divergence: the returned error will not give a useful message
		// errors = append(errors, ast.NewCompilerDiagnostic(diagnostics.Cannot_read_file_0_Colon_1, *e));
		errors.push_back(
		    newCompilerDiagnostic(Cannot_read_file_0, {std::string(fileName)}));
		return {"", errors};
	}
	return {text, errors};
}

// parseOptionValue — commandlineparser.go:237.
int commandLineParser::parseOptionValue(
    const std::vector<std::string>& args, int i,
    const CommandLineOption* opt, const DiagnosticMessage* diag) {
	if (opt->IsTSConfigOnly && i <= (int)args.size()) {
		std::string optValue;
		if (i < (int)args.size()) {
			optValue = args[i];
		}
		if (optValue == "null") {
			options->Set(opt->Name, CompilerOptionsValue());
			i++;
		} else if (opt->Kind == CommandLineOptionTypeBoolean) {
			if (optValue == "false") {
				options->Set(opt->Name, false);
				i++;
			} else {
				if (optValue == "true") {
					i++;
				}
				errors.push_back(newCompilerDiagnostic(
				    Option_0_can_only_be_specified_in_tsconfig_json_file_or_set_to_false_or_null_on_command_line,
				    {opt->Name}));
			}
		} else {
			errors.push_back(newCompilerDiagnostic(
			    Option_0_can_only_be_specified_in_tsconfig_json_file_or_set_to_null_on_command_line,
			    {opt->Name}));
			if (!optValue.empty() && !optValue.starts_with('-')) {
				i++;
			}
		}
	} else {
		// Check to see if no argument was provided (e.g. "--locale" is the
		// last command-line argument).
		if (i >= (int)args.size()) {
			if (opt->Kind != CommandLineOptionTypeBoolean) {
				errors.push_back(newCompilerDiagnostic(
				    diag,
				    {opt->Name,
				     getCompilerOptionValueTypeString(opt)}));
				if (opt->Kind == CommandLineOptionTypeList) {
					options->Set(opt->Name, JsonStrList{});
				} else if (opt->Kind == CommandLineOptionTypeEnum) {
					errors.push_back(
					    createDiagnosticForInvalidEnumType(opt, nullptr,
					                                       nullptr));
				}
			} else {
				options->Set(opt->Name, true);
			}
			return i;
		}
		if (args[i] != "null") {
			if (opt->Kind == CommandLineOptionTypeNumber) {
				// !!! Make sure this parseInt matches JS parseInt
				auto [num, ok] = atoiGo(args[i]);
				if (ok) {
					if (num >= opt->minValue) {
						options->Set(opt->Name, int64_t(num));
					} else {
						errors.push_back(newCompilerDiagnostic(
						    Option_0_requires_value_to_be_greater_than_1,
						    {opt->Name,
						     itoaGo(opt->minValue)}));
					}
				} else {
					errors.push_back(newCompilerDiagnostic(
					    diag, {opt->Name, "number"}));
				}
				i++;
			} else if (opt->Kind == CommandLineOptionTypeBoolean) {
				// boolean flag has optional value true, false, others
				const std::string& optValue = args[i];

				// check next argument as boolean flag value
				if (optValue == "false") {
					options->Set(opt->Name, false);
				} else {
					options->Set(opt->Name, true);
				}
				// try to consume next argument as value for boolean
				// flag; do not consume argument if it is not "true" or
				// "false"
				if (optValue == "false" || optValue == "true") {
					i++;
				}
			} else if (opt->Kind == CommandLineOptionTypeString) {
				auto [val, err] = validateJsonOptionValue(
				    opt, CompilerOptionsValue(args[i]), nullptr, nullptr);
				if (err.empty()) {
					options->Set(opt->Name, val);
				} else {
					errors.insert(errors.end(), err.begin(), err.end());
				}
				i++;
			} else if (opt->Kind == CommandLineOptionTypeList) {
				auto [result, err] = parseListTypeOption(opt, args[i]);
				options->Set(opt->Name, result);
				errors.insert(errors.end(), err.begin(), err.end());
				if (!result.empty() || !err.empty()) {
					i++;
				}
			} else if (opt->Kind == CommandLineOptionTypeListOrElement) {
				// If not a primitive, the possible types are specified
				// in what is effectively a map of options.
				TSC_UNREACHABLE("listOrElement not supported here");
			} else {
				auto [val, err] = convertJsonOptionOfEnumType(
				    opt, trimWhiteSpaceLike(args[i]), nullptr, nullptr);
				options->Set(opt->Name, val);
				errors.insert(errors.end(), err.begin(), err.end());
				i++;
			}
		} else {
			options->Set(opt->Name, CompilerOptionsValue());
			i++;
		}
	}
	return i;
}

// parseListTypeOption — commandlineparser.go:343.
std::pair<JsonArray, std::vector<Diagnostic*>>
commandLineParser::parseListTypeOption(const CommandLineOption* opt,
                                       std::string_view value) {
	return ParseListTypeOption(opt, value);
}

// ParseListTypeOption — commandlineparser.go:347.
std::pair<JsonArray, std::vector<Diagnostic*>> ParseListTypeOption(
    const CommandLineOption* opt, std::string_view value) {
	value = trimSpaceSv(value);
	std::vector<Diagnostic*> errors;
	if (value.starts_with('-')) {
		return {{}, errors};
	}
	if (opt->Kind == CommandLineOptionTypeListOrElement && value.find(',') == std::string_view::npos) {
		auto [val, err] = validateJsonOptionValue(
		    opt, CompilerOptionsValue(std::string(value)), nullptr, nullptr);
		if (!err.empty()) {
			return {{}, err};
		}
		return {JsonArray{CompilerOptionsValue(val.asString())}, errors};
	}
	if (value.empty()) {
		return {{}, errors};
	}
	std::vector<std::string> values;
	{
		size_t start = 0;
		while (true) {
			auto idx = value.find(',', start);
			if (idx == std::string_view::npos) {
				values.emplace_back(value.substr(start));
				break;
			}
			values.emplace_back(value.substr(start, idx - start));
			start = idx + 1;
		}
	}
	if (opt->Elements()->Kind == CommandLineOptionTypeString) {
		JsonArray elements;
		for (const auto& v : values) {
			auto [val, err] = validateJsonOptionValue(
			    opt->Elements(), CompilerOptionsValue(v), nullptr, nullptr);
			if (auto* s = val.get<std::string>();
			    s != nullptr && err.empty() && !s->empty()) {
				elements.emplace_back(*s);
				continue;
			}
			errors.insert(errors.end(), err.begin(), err.end());
		}
		return {elements, errors};
	}
	if (opt->Elements()->Kind == CommandLineOptionTypeBoolean ||
	    opt->Elements()->Kind == CommandLineOptionTypeObject ||
	    opt->Elements()->Kind == CommandLineOptionTypeNumber) {
		// do nothing: only string and enum/object types currently allowed
		// as list entries
		// 				!!! we don't actually have number list options, so I didn't implement number list parsing
		TSC_UNREACHABLE(("List of " +
		                std::string(opt->Elements()->Kind) +
		                " is not yet supported.")
		                   .c_str());
	}
	JsonArray result;
	for (const auto& v : values) {
		auto [val, err] = convertJsonOptionOfEnumType(
		    opt->Elements(), trimWhiteSpaceLike(v), nullptr, nullptr);
		if (auto* s = val.get<std::string>();
		    s != nullptr && err.empty() && !s->empty()) {
			result.emplace_back(*s);
			continue;
		}
		errors.insert(errors.end(), err.begin(), err.end());
	}
	return {result, errors};
}

// convertJsonOptionOfEnumType — commandlineparser.go:392.
std::pair<CompilerOptionsValue, std::vector<Diagnostic*>>
convertJsonOptionOfEnumType(const CommandLineOption* opt,
                            std::string_view value, Node* valueExpression,
                            SourceFile* sourceFile) {
	if (value.empty()) {
		return {CompilerOptionsValue(), {}};
	}
	std::string key = tspath::toFileNameLowerCase(value);
	const JsonObject* typeMap = opt->EnumMap();
	if (typeMap == nullptr) {
		return {CompilerOptionsValue(), {}};
	}
	if (auto val = typeMap->Get(key); val.second) {
		return validateJsonOptionValue(opt, *val.first, valueExpression,
		                               sourceFile);
	}
	return {CompilerOptionsValue(),
	        {createDiagnosticForInvalidEnumType(opt, sourceFile,
	                                            valueExpression)}};
}

}  // namespace tsc::tsoptions
