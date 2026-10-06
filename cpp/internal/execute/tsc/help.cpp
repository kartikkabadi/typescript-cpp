// help.go — port of tsc/internal/execute/tsc/help.go.

#include "internal/execute/tsc/help.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_map>
#include <utility>

#include "internal/core/version.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/execute/tsc/diagnostics.h"

namespace tsc::execute::tsc {
namespace {

// padRight/padLeft — Go fmt `%*s`/`%-*s` width verbs (not handled by
// gostd::sprintf).
std::string padRight(std::string_view s, int width) {
	std::string out(s);
	if (static_cast<int>(out.size()) < width) {
		out.append(width - out.size(), ' ');
	}
	return out;
}

std::string padLeft(std::string_view s, int width) {
	std::string out;
	if (static_cast<int>(s.size()) < width) {
		out.append(width - s.size(), ' ');
	}
	out.append(s);
	return out;
}

// msgLocalize — diagnostics.Message.Localize(locale, args...): key "".
std::string msgLocalize(locale::Locale locale, const DiagnosticMessage* msg,
                        std::vector<std::string> args = {}) {
	return ::tsc::localize(locale, msg, "", std::move(args));
}

// sprintCompilerOptionsValue — fmt.Sprintf("%v", defaultValue) for the
// tsoptions::CompilerOptionsValue arms reachable from option defaults.
std::string sprintCompilerOptionsValue(const tsoptions::CompilerOptionsValue& v) {
	if (auto* b = v.get<bool>()) {
		return *b ? "true" : "false";
	}
	if (auto* i = v.get<int64_t>()) {
		return std::to_string(*i);
	}
	if (auto* f = v.get<double>()) {
		char buf[32];
		std::snprintf(buf, sizeof buf, "%g", *f);
		return buf;
	}
	if (auto* t = v.get<Tristate>()) {
		// Tristate.String()
		switch (*t) {
		case Tristate::Unknown:
			return "TSUnknown";
		case Tristate::False:
			return "TSFalse";
		case Tristate::True:
			return "TSTrue";
		}
		return "Tristate(" + std::to_string(static_cast<int>(*t)) + ")";
	}
	if (auto* s = v.get<std::string>()) {
		return *s;
	}
	if (auto* m = v.get<const DiagnosticMessage*>()) {
		return *m == nullptr ? "<nil>" : std::string((*m)->text);
	}
	return "<nil>";
}

// valueCandidate — help.go:310.
struct valueCandidate {
	// "one or more" or "any of"
	std::string valueType;
	std::string possibleValues;
};

std::vector<std::string> generateSectionOptionsOutput(
    System* sys, locale::Locale locale, std::string_view sectionName,
    const std::vector<const tsoptions::CommandLineOption*>& options,
    bool subCategory, const std::string* beforeOptionsDescription,
    const std::string* afterOptionsDescription);

std::vector<std::string> generateGroupOptionOutput(
    System* sys, locale::Locale locale,
    const std::vector<const tsoptions::CommandLineOption*>& optionsList);

std::vector<std::string> generateOptionOutput(
    System* sys, locale::Locale locale,
    const tsoptions::CommandLineOption* option, int rightAlignOfLeft,
    int leftAlignOfRight);

std::string formatDefaultValue(const tsoptions::CompilerOptionsValue& defaultValue,
                               const tsoptions::CommandLineOption* option);

bool showAdditionalInfoOutput(
    const valueCandidate* valueCandidates,
    const tsoptions::CommandLineOption* option);

valueCandidate* getValueCandidate(
    System* sys, locale::Locale locale,
    const tsoptions::CommandLineOption* option);

std::string getPossibleValues(const tsoptions::CommandLineOption* option);

std::vector<std::string> getPrettyOutput(const colors& colors,
                                         std::string_view left,
                                         std::string_view right,
                                         int rightAlignOfLeft,
                                         int leftAlignOfRight,
                                         int terminalWidth, bool colorLeft);

}  // namespace

// PrintVersion — help.go:14.
void PrintVersion(System* sys, locale::Locale locale) {
	*sys->Writer() << msgLocalize(locale, Version_0,
	                              {std::string(version())})
	               << '\n';
}

// PrintHelp — help.go:18.
void PrintHelp(System* sys, locale::Locale locale,
               tsoptions::ParsedCommandLine* commandLine) {
	if (tristateIsFalseOrUnknown(commandLine->CompilerOptions()->All)) {
		printEasyHelp(sys, locale, getOptionsForHelp(commandLine));
	} else {
		printAllHelp(sys, locale, getOptionsForHelp(commandLine));
	}
}

// getOptionsForHelp — help.go:26.
std::vector<const tsoptions::CommandLineOption*> getOptionsForHelp(
    tsoptions::ParsedCommandLine* commandLine) {
	// Sort our options by their names, (e.g. "--noImplicitAny" comes before "--watch")
	std::vector<const tsoptions::CommandLineOption*> opts(
	    tsoptions::OptionsDeclarations());
	opts.push_back(&tsoptions::TscBuildOption());

	if (tristateIsTrue(commandLine->CompilerOptions()->All)) {
		std::sort(opts.begin(), opts.end(),
		          [](const tsoptions::CommandLineOption* a,
		             const tsoptions::CommandLineOption* b) {
			          std::string al = a->Name, bl = b->Name;
			          for (auto& c : al) c = char(std::tolower((unsigned char)c));
			          for (auto& c : bl) c = char(std::tolower((unsigned char)c));
			          return al < bl;
		          });
		return opts;
	}
	std::vector<const tsoptions::CommandLineOption*> out;
	for (auto* opt : opts) {
		if (opt->ShowInSimplifiedHelpView) {
			out.push_back(opt);
		}
	}
	return out;
}

// getHeader — help.go:40.
std::vector<std::string> getHeader(System* sys, std::string_view message) {
	colors colors = createColors(sys);
	std::vector<std::string> header;
	header.reserve(3);
	int terminalWidth = sys->GetWidthOfTerminal();
	const std::string_view tsIcon = "     ";
	const std::string_view tsIconTS = "  TS ";
	const int tsIconLength = 5;

	auto tsIconFirstLine = colors.blueBackground(tsIcon);
	auto tsIconSecondLine = colors.blueBackground(colors.brightWhite(tsIconTS));
	// If we have enough space, print TS icon.
	if (terminalWidth >= static_cast<int>(message.size()) + tsIconLength) {
		// right align of the icon is 120 at most.
		int rightAlign = terminalWidth > 120 ? 120 : terminalWidth;
		int leftAlign = rightAlign - tsIconLength;
		header.push_back(padRight(message, leftAlign));
		header.push_back(tsIconFirstLine);
		header.push_back("\n");
		header.push_back(std::string(leftAlign, ' '));
		header.push_back(tsIconSecondLine);
		header.push_back("\n");
	} else {
		header.push_back(std::string(message));
		header.push_back("\n");
		header.push_back("\n");
	}
	return header;
}

// printEasyHelp — help.go:62.
void printEasyHelp(
    System* sys, locale::Locale locale,
    const std::vector<const tsoptions::CommandLineOption*>& simpleOptions) {
	colors colors = createColors(sys);
	std::vector<std::string> output;
	auto example = [&](const std::vector<std::string>& examples,
	                   const DiagnosticMessage* desc) {
		for (auto& ex : examples) {
			output.push_back("  ");
			output.push_back(colors.blue(ex));
			output.push_back("\n");
		}
		output.push_back("  ");
		output.push_back(msgLocalize(locale, desc));
		output.push_back("\n");
		output.push_back("\n");
	};

	std::string msg =
	    msgLocalize(locale, X_tsc_Colon_The_TypeScript_Compiler) +
	    " - " +
	    msgLocalize(locale, Version_0,
	                {std::string(version())});
	auto header = getHeader(sys, msg);
	output.insert(output.end(), header.begin(), header.end());

	output.push_back(colors.bold(msgLocalize(locale, COMMON_COMMANDS)));
	output.push_back("\n");
	output.push_back("\n");

	example({"tsc"},
	        
	            Compiles_the_current_project_tsconfig_json_in_the_working_directory);
	example({"tsc app.ts util.ts"},
	        
	            Ignoring_tsconfig_json_compiles_the_specified_files_with_default_compiler_options);
	example({"tsc -b"},
	        Build_a_composite_project_in_the_working_directory);
	example({"tsc --init"},
	        
	            Creates_a_tsconfig_json_with_the_recommended_settings_in_the_working_directory);
	example({"tsc -p ./path/to/tsconfig.json"},
	        
	            Compiles_the_TypeScript_project_located_at_the_specified_path);
	example({"tsc --help --all"},
	        
	            An_expanded_version_of_this_information_showing_all_possible_compiler_options);
	example({"tsc --noEmit", "tsc --target esnext"},
	        
	            Compiles_the_current_project_with_additional_settings);

	std::vector<const tsoptions::CommandLineOption*> cliCommands, configOpts;
	for (auto* opt : simpleOptions) {
		if (opt->IsCommandLineOnly ||
		    opt->Category == Command_line_Options) {
			cliCommands.push_back(opt);
		} else {
			configOpts.push_back(opt);
		}
	}

	auto cliOut = generateSectionOptionsOutput(
	    sys, locale, msgLocalize(locale, COMMAND_LINE_FLAGS),
	    cliCommands, /*subCategory*/ false,
	    /*beforeOptionsDescription*/ nullptr,
	    /*afterOptionsDescription*/ nullptr);
	output.insert(output.end(), cliOut.begin(), cliOut.end());

	std::string after = msgLocalize(
	    locale, You_can_learn_about_all_of_the_compiler_options_at_0,
	    {"https://aka.ms/tsc"});
	auto configOut = generateSectionOptionsOutput(
	    sys, locale, msgLocalize(locale, COMMON_COMPILER_OPTIONS),
	    configOpts, /*subCategory*/ false,
	    /*beforeOptionsDescription*/ nullptr, &after);
	output.insert(output.end(), configOut.begin(), configOut.end());

	for (auto& chunk : output) {
		*sys->Writer() << chunk;
	}
}

// printAllHelp — help.go:111.
void printAllHelp(
    System* sys, locale::Locale locale,
    const std::vector<const tsoptions::CommandLineOption*>& options) {
	std::vector<std::string> output;
	std::string msg =
	    msgLocalize(locale, X_tsc_Colon_The_TypeScript_Compiler) +
	    " - " +
	    msgLocalize(locale, Version_0,
	                {std::string(version())});
	auto header = getHeader(sys, msg);
	output.insert(output.end(), header.begin(), header.end());

	// ALL COMPILER OPTIONS section
	std::string afterCompilerOptions = msgLocalize(
	    locale, You_can_learn_about_all_of_the_compiler_options_at_0,
	    {"https://aka.ms/tsc"});
	auto allOut = generateSectionOptionsOutput(
	    sys, locale, msgLocalize(locale, ALL_COMPILER_OPTIONS),
	    options, true, nullptr, &afterCompilerOptions);
	output.insert(output.end(), allOut.begin(), allOut.end());

	// WATCH OPTIONS section
	std::string beforeWatchOptions = msgLocalize(
	    locale,
	    
	        Including_watch_w_will_start_watching_the_current_project_for_the_file_changes_Once_set_you_can_config_watch_mode_with_Colon);
	auto watchOut = generateSectionOptionsOutput(
	    sys, locale, msgLocalize(locale, WATCH_OPTIONS),
	    tsoptions::OptionsForWatch(), false, &beforeWatchOptions, nullptr);
	output.insert(output.end(), watchOut.begin(), watchOut.end());

	// BUILD OPTIONS section
	std::string beforeBuildOptions = msgLocalize(
	    locale,
	    
	        Using_build_b_will_make_tsc_behave_more_like_a_build_orchestrator_than_a_compiler_This_is_used_to_trigger_building_composite_projects_which_you_can_learn_more_about_at_0,
	    {"https://aka.ms/tsc-composite-builds"});
	std::vector<const tsoptions::CommandLineOption*> buildOptions;
	for (auto* option : tsoptions::OptionsForBuild()) {
		if (option != &tsoptions::TscBuildOption()) {
			buildOptions.push_back(option);
		}
	}
	auto buildOut = generateSectionOptionsOutput(
	    sys, locale, msgLocalize(locale, BUILD_OPTIONS),
	    buildOptions, false, &beforeBuildOptions, nullptr);
	output.insert(output.end(), buildOut.begin(), buildOut.end());

	for (auto& chunk : output) {
		*sys->Writer() << chunk;
	}
}

// PrintBuildHelp — help.go:136.
void PrintBuildHelp(
    System* sys, locale::Locale locale,
    const std::vector<const tsoptions::CommandLineOption*>& buildOptions) {
	std::vector<std::string> output;
	auto header = getHeader(
	    sys,
	    msgLocalize(locale, X_tsc_Colon_The_TypeScript_Compiler) +
	        " - " +
	        msgLocalize(locale, Version_0,
	                    {std::string(version())}));
	output.insert(output.end(), header.begin(), header.end());
	std::string before = msgLocalize(
	    locale,
	    
	        Using_build_b_will_make_tsc_behave_more_like_a_build_orchestrator_than_a_compiler_This_is_used_to_trigger_building_composite_projects_which_you_can_learn_more_about_at_0,
	    {"https://aka.ms/tsc-composite-builds"});
	std::vector<const tsoptions::CommandLineOption*> options;
	for (auto* option : buildOptions) {
		if (option != &tsoptions::TscBuildOption()) {
			options.push_back(option);
		}
	}
	auto buildOut = generateSectionOptionsOutput(
	    sys, locale, msgLocalize(locale, BUILD_OPTIONS), options,
	    false, &before, nullptr);
	output.insert(output.end(), buildOut.begin(), buildOut.end());

	for (auto& chunk : output) {
		*sys->Writer() << chunk;
	}
}

namespace {

// generateSectionOptionsOutput — help.go:148.
std::vector<std::string> generateSectionOptionsOutput(
    System* sys, locale::Locale locale, std::string_view sectionName,
    const std::vector<const tsoptions::CommandLineOption*>& options,
    bool subCategory, const std::string* beforeOptionsDescription,
    const std::string* afterOptionsDescription) {
	std::vector<std::string> output;
	output.push_back(createColors(sys).bold(sectionName));
	output.push_back("\n");
	output.push_back("\n");

	if (beforeOptionsDescription != nullptr) {
		output.push_back(*beforeOptionsDescription);
		output.push_back("\n");
		output.push_back("\n");
	}
	if (!subCategory) {
		auto out = generateGroupOptionOutput(sys, locale, options);
		output.insert(output.end(), out.begin(), out.end());
		if (afterOptionsDescription != nullptr) {
			output.push_back(*afterOptionsDescription);
			output.push_back("\n");
			output.push_back("\n");
		}
		return output;
	}
	std::unordered_map<std::string,
	                   std::vector<const tsoptions::CommandLineOption*>>
	    categoryMap;
	std::vector<std::string> categoryOrder;
	for (auto* option : options) {
		if (option->Category == nullptr) {
			continue;
		}
		auto curCategory = msgLocalize(locale, option->Category);
		if (categoryMap.find(curCategory) == categoryMap.end()) {
			categoryOrder.push_back(curCategory);
		}
		categoryMap[curCategory].push_back(option);
	}
	for (auto& key : categoryOrder) {
		auto& value = categoryMap[key];
		output.push_back("### ");
		output.push_back(key);
		output.push_back("\n");
		output.push_back("\n");
		auto out = generateGroupOptionOutput(sys, locale, value);
		output.insert(output.end(), out.begin(), out.end());
	}
	if (afterOptionsDescription != nullptr) {
		output.push_back(*afterOptionsDescription);
		output.push_back("\n");
		output.push_back("\n");
	}
	return output;
}

// generateGroupOptionOutput — help.go:187.
std::vector<std::string> generateGroupOptionOutput(
    System* sys, locale::Locale locale,
    const std::vector<const tsoptions::CommandLineOption*>& optionsList) {
	int maxLength = 0;
	for (auto* option : optionsList) {
		maxLength = std::max(
		    maxLength, static_cast<int>(getDisplayNameTextOfOption(option)
		                                    .size()));
	}

	// left part should be right align, right part should be left align

	// assume 2 space between left margin and left part.
	int rightAlignOfLeftPart = maxLength + 2;
	// assume 2 space between left and right part
	int leftAlignOfRightPart = rightAlignOfLeftPart + 2;

	std::vector<std::string> lines;
	for (auto* option : optionsList) {
		auto tmp = generateOptionOutput(sys, locale, option,
		                                rightAlignOfLeftPart,
		                                leftAlignOfRightPart);
		lines.insert(lines.end(), tmp.begin(), tmp.end());
	}

	// make sure always a blank line in the end.
	if (lines.size() < 2 || lines[lines.size() - 2] != "\n") {
		lines.push_back("\n");
	}
	return lines;
}

// generateOptionOutput — help.go:216.
std::vector<std::string> generateOptionOutput(
    System* sys, locale::Locale locale,
    const tsoptions::CommandLineOption* option, int rightAlignOfLeft,
    int leftAlignOfRight) {
	std::vector<std::string> text;
	colors colors = createColors(sys);

	// name and description
	auto name = getDisplayNameTextOfOption(option);

	// value type and possible value
	auto* valueCandidates = getValueCandidate(sys, locale, option);

	std::string defaultValueDescription;
	if (auto* const* msg =
	        std::get_if<const DiagnosticMessage*>(
	            &option->DefaultValueDescription.v);
	    msg != nullptr && *msg != nullptr) {
		defaultValueDescription = msgLocalize(locale, *msg);
	} else {
		defaultValueDescription = formatDefaultValue(
		    option->DefaultValueDescription,
		    option->Kind == tsoptions::CommandLineOptionTypeList ||
		            option->Kind ==
		                tsoptions::CommandLineOptionTypeListOrElement
		        ? option->Elements()
		        : option);
	}

	int terminalWidth = sys->GetWidthOfTerminal();

	if (terminalWidth >= 80) {
		std::string description;
		if (option->Description != nullptr) {
			description = msgLocalize(locale, option->Description);
		}
		auto pretty = getPrettyOutput(
		    colors, name, description, rightAlignOfLeft, leftAlignOfRight,
		    terminalWidth, /*colorLeft*/ true);
		text.insert(text.end(), pretty.begin(), pretty.end());
		text.push_back("\n");
		if (showAdditionalInfoOutput(valueCandidates, option)) {
			if (valueCandidates != nullptr) {
				auto out = getPrettyOutput(
				    colors, valueCandidates->valueType,
				    valueCandidates->possibleValues, rightAlignOfLeft,
				    leftAlignOfRight, terminalWidth,
				    /*colorLeft*/ false);
				text.insert(text.end(), out.begin(), out.end());
				text.push_back("\n");
			}
			if (!defaultValueDescription.empty()) {
				auto out = getPrettyOutput(
				    colors,
				    msgLocalize(locale, X_default_Colon),
				    defaultValueDescription, rightAlignOfLeft,
				    leftAlignOfRight, terminalWidth,
				    /*colorLeft*/ false);
				text.insert(text.end(), out.begin(), out.end());
				text.push_back("\n");
			}
		}
		text.push_back("\n");
	} else {
		text.push_back(colors.blue(name));
		text.push_back("\n");
		if (option->Description != nullptr) {
			text.push_back(msgLocalize(locale, option->Description));
		}
		text.push_back("\n");
		if (showAdditionalInfoOutput(valueCandidates, option)) {
			if (valueCandidates != nullptr) {
				text.push_back(valueCandidates->valueType);
				text.push_back(" ");
				text.push_back(valueCandidates->possibleValues);
			}
			if (!defaultValueDescription.empty()) {
				if (valueCandidates != nullptr) {
					text.push_back("\n");
				}
				text.push_back(
				    msgLocalize(locale, X_default_Colon));
				text.push_back(" ");
				text.push_back(defaultValueDescription);
			}
			text.push_back("\n");
		}
		text.push_back("\n");
	}

	return text;
}

// formatDefaultValue — help.go:288.
std::string formatDefaultValue(const tsoptions::CompilerOptionsValue& defaultValue,
                               const tsoptions::CommandLineOption* option) {
	if (defaultValue.isNil() ||
	    (defaultValue.get<Tristate>() != nullptr &&
	     *defaultValue.get<Tristate>() == Tristate::Unknown)) {
		return "undefined";
	}

	if (option->Kind == tsoptions::CommandLineOptionTypeEnum) {
		// e.g. ScriptTarget.ES2015 -> "es6/es2015"
		std::vector<std::string> names;
		for (auto& name : option->EnumMap()->Keys()) {
			if (auto [value, ok] = option->EnumMap()->Get(name);
			    ok && *value == defaultValue) {
				names.push_back(name);
			}
		}
		std::string out;
		for (size_t i = 0; i < names.size(); i++) {
			if (i != 0) {
				out += "/";
			}
			out += names[i];
		}
		return out;
	}
	return sprintCompilerOptionsValue(defaultValue);
}

// showAdditionalInfoOutput — help.go:314.
bool showAdditionalInfoOutput(
    const valueCandidate* valueCandidates,
    const tsoptions::CommandLineOption* option) {
	if (option->Category == Command_line_Options) {
		return false;
	}
	if (valueCandidates != nullptr &&
	    valueCandidates->possibleValues == "string" &&
	    (option->DefaultValueDescription.isNil() ||
	     option->DefaultValueDescription == tsoptions::CompilerOptionsValue("false") ||
	     option->DefaultValueDescription == tsoptions::CompilerOptionsValue("n/a"))) {
		return false;
	}
	return true;
}

// getValueCandidate — help.go:329.
valueCandidate* getValueCandidate(
    System* sys, locale::Locale locale,
    const tsoptions::CommandLineOption* option) {
	// option.type might be "string" | "number" | "boolean" | "object" | "list" | Map<string, number | string>
	// string -- any of: string
	// number -- any of: number
	// boolean -- any of: boolean
	// object -- null
	// list -- one or more: , content depends on `option.element.type`, the same as others
	// Map<string, number | string> -- any of: key1, key2, ....
	if (option->Kind == tsoptions::CommandLineOptionTypeObject) {
		return nullptr;
	}

	auto* res = new valueCandidate();
	if (option->Kind == tsoptions::CommandLineOptionTypeListOrElement) {
		// assert(option.type !== "listOrElement")
		TSC_UNREACHABLE("no value candidate for list or element");
	}

	if (option->Kind == tsoptions::CommandLineOptionTypeString ||
	    option->Kind == tsoptions::CommandLineOptionTypeNumber ||
	    option->Kind == tsoptions::CommandLineOptionTypeBoolean) {
		res->valueType = msgLocalize(locale, X_type_Colon);
	} else if (option->Kind == tsoptions::CommandLineOptionTypeList) {
		res->valueType =
		    msgLocalize(locale, X_one_or_more_Colon);
	} else {
		res->valueType = msgLocalize(locale, X_one_of_Colon);
	}

	res->possibleValues = getPossibleValues(option);
	return res;
}

// getPossibleValues — help.go:359.
std::string getPossibleValues(const tsoptions::CommandLineOption* option) {
	if (option->Kind == tsoptions::CommandLineOptionTypeString ||
	    option->Kind == tsoptions::CommandLineOptionTypeNumber ||
	    option->Kind == tsoptions::CommandLineOptionTypeBoolean) {
		return std::string(option->Kind);
	}
	if (option->Kind == tsoptions::CommandLineOptionTypeList ||
	    option->Kind == tsoptions::CommandLineOptionTypeListOrElement) {
		return getPossibleValues(option->Elements());
	}
	if (option->Kind == tsoptions::CommandLineOptionTypeObject) {
		return "";
	}
	// Map<string, number | string>
	// Group synonyms: es6/es2015
	auto* enumMap = option->EnumMap();
	// collections.NewOrderedMapWithSizeHint[any, []string] — insertion-ordered
	// value -> synonyms.
	std::vector<std::pair<tsoptions::CompilerOptionsValue, std::vector<std::string>>>
	    inverted;
	auto* deprecatedKeys = option->DeprecatedKeys();

	for (auto& name : enumMap->Keys()) {
		if (deprecatedKeys == nullptr || !deprecatedKeys->Has(name)) {
			auto valuePair = enumMap->Get(name);
			auto* value = valuePair.first;
			auto it = std::find_if(
			    inverted.begin(), inverted.end(),
			    [&](auto& p) { return p.first == *value; });
			if (it == inverted.end()) {
				inverted.emplace_back(*value,
				                      std::vector<std::string>{name});
			} else {
				it->second.push_back(name);
			}
		}
	}
	std::vector<std::string> syns;
	for (auto& [value, synonyms] : inverted) {
		std::string joined;
		for (size_t i = 0; i < synonyms.size(); i++) {
			if (i != 0) {
				joined += "/";
			}
			joined += synonyms[i];
		}
		syns.push_back(std::move(joined));
	}
	std::string out;
	for (size_t i = 0; i < syns.size(); i++) {
		if (i != 0) {
			out += ", ";
		}
		out += syns[i];
	}
	return out;
}

// getPrettyOutput — help.go:391.
std::vector<std::string> getPrettyOutput(const colors& colors,
                                         std::string_view left,
                                         std::string_view right,
                                         int rightAlignOfLeft,
                                         int leftAlignOfRight,
                                         int terminalWidth, bool colorLeft) {
	// !!! How does terminalWidth interact with UTF-8 encoding? Strada just assumed UTF-16.
	std::vector<std::string> res;
	res.reserve(4);
	bool isFirstLine = true;
	std::string_view remainRight = right;
	int rightCharacterNumber = terminalWidth - leftAlignOfRight;
	for (; !remainRight.empty();) {
		std::string curLeft;
		if (isFirstLine) {
			// fmt.Sprintf("%*s", rightAlignOfLeft, left) then
			// fmt.Sprintf("%-*s", leftAlignOfRight, curLeft)
			curLeft = padRight(padLeft(left, rightAlignOfLeft),
			                   leftAlignOfRight);
			if (colorLeft) {
				curLeft = colors.blue(curLeft);
			}
		} else {
			curLeft = std::string(leftAlignOfRight, ' ');
		}

		int idx =
		    std::min(rightCharacterNumber, static_cast<int>(remainRight.size()));
		auto curRight = remainRight.substr(0, idx);
		remainRight = remainRight.substr(idx);
		res.push_back(std::move(curLeft));
		res.emplace_back(curRight);
		res.push_back("\n");
		isFirstLine = false;
	}
	return res;
}

}  // namespace

// getDisplayNameTextOfOption — help.go:426 (exported via help.h).
std::string getDisplayNameTextOfOption(
    const tsoptions::CommandLineOption* option) {
	return "--" + option->Name +
	       (option->ShortName.empty() ? "" : ", -" + option->ShortName);
}

}  // namespace tsc::execute::tsc
