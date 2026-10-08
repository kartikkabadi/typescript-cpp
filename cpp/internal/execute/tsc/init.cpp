// init.go — port of tsc/internal/execute/tsc/init.go.

#include "internal/execute/tsc/init.h"

#include <algorithm>
#include <vector>

#include "internal/diagnostics/messages_generated.h"
#include "internal/execute/tsc/help.h"
#include "internal/json/json.h"
#include "internal/tspath/tspath.h"

namespace tsc::execute::tsc {

// WriteConfigFile — init.go:17.
void WriteConfigFile(System* sys, locale::Locale locale,
                     DiagnosticReporter reportDiagnostic,
                     const tsoptions::JsonObjectPtr& options) {
	auto getCurrentDirectory = sys->GetCurrentDirectory();
	auto file = tspath::normalizePath(
	    tspath::combinePaths(getCurrentDirectory, {"tsconfig.json"}));
	if (sys->fs()->FileExists(file)) {
		reportDiagnostic(tsoptions::newCompilerDiagnostic(
		    A_tsconfig_json_file_is_already_defined_at_Colon_0,
		    {file}));
	} else {
		sys->fs()->WriteFile(file, generateTSConfig(options, locale));
		std::vector<std::string> output = {"\n"};
		for (auto& chunk : getHeader(sys, "Created a new tsconfig.json")) {
			output.push_back(chunk);
		}
		output.push_back("You can learn more at https://aka.ms/tsconfig");
		output.push_back("\n");
		for (auto& chunk : output) {
			*sys->Writer() << chunk;
		}
	}
}

// generateTSConfig — init.go:32.
std::string generateTSConfig(const tsoptions::JsonObjectPtr& options,
                             locale::Locale locale) {
	const std::string tab = "  ";
	std::vector<std::string> result;

	std::vector<std::string> allSetOptions;
	allSetOptions.reserve(options->Size());
	for (auto& k : options->Keys()) {
		if (k != "init" && k != "help" && k != "watch") {
			allSetOptions.push_back(k);
		}
	}

	auto emitHeader = [&](const DiagnosticMessage* header) {
		result.push_back(tab + tab + "// " +
		                 ::tsc::localize(locale, header, "", {}));
	};
	auto newline = [&]() { result.push_back(""); };
	auto push = [&](std::initializer_list<std::string> args) {
		result.insert(result.end(), args.begin(), args.end());
	};

	auto formatSingleValue = [&](const tsoptions::CompilerOptionsValue& value,
	                             const tsoptions::JsonObject* enumMap) -> std::string {
		tsoptions::CompilerOptionsValue outValue = value;
		if (enumMap != nullptr) {
			bool found = false;
			for (auto& k : enumMap->Keys()) {
				if (auto [v, ok] = enumMap->Get(k); ok && *v == value) {
					outValue = tsoptions::CompilerOptionsValue(k);
					found = true;
					break;
				}
			}
			if (!found) {
				TSC_UNREACHABLE("No matching value of value");
			}
		}

		// Go: json.MarshalIndent(value, "", "") — empty indent means no
		// indent change; OrderedMap emits keys in insertion order.
		return tsoptions::jsonMarshal(outValue);
	};

	auto formatValueOrArray = [&](const std::string& settingName,
	                              const tsoptions::CompilerOptionsValue& value)
	    -> std::string {
		const tsoptions::CommandLineOption* option = nullptr;
		for (auto* decl : tsoptions::OptionsDeclarations()) {
			if (decl->Name == settingName) {
				option = decl;
			}
		}
		if (option == nullptr) {
			TSC_UNREACHABLE(
			    ("No option named " + settingName).c_str());
		}

		if (value.isSliceKind()) {
			const tsoptions::JsonObject* enumMap = nullptr;
			if (auto* elemOption = option->Elements(); elemOption != nullptr) {
				enumMap = elemOption->EnumMap();
			}

			std::vector<std::string> elems;
			if (auto* list = value.get<tsoptions::JsonStrList>()) {
				for (auto& v : *list) {
					elems.push_back(
					    formatSingleValue(tsoptions::CompilerOptionsValue(v), enumMap));
				}
			} else {
				for (auto& v : value.asArray()) {
					elems.push_back(formatSingleValue(v, enumMap));
				}
			}
			std::string joined;
			for (size_t i = 0; i < elems.size(); i++) {
				if (i != 0) {
					joined += ", ";
				}
				joined += elems[i];
			}
			return "[" + joined + "]";
		}
		return formatSingleValue(value, option->EnumMap());
	};

	// commentedNever': Never comment this out
	// commentedAlways': Always comment this out, even if it's on commandline
	// commentedOptional': Comment out unless it's on commandline
	enum commented { commentedNever, commentedAlways, commentedOptional };
	auto emitOption = [&](const std::string& setting,
	                      const tsoptions::CompilerOptionsValue& defaultValue,
	                      commented commentedIn) {
		if (commentedIn > 2) {
			TSC_UNREACHABLE(
			    "should not happen: invalid `commented`, must be a bug.");
		}

		auto it = std::find(allSetOptions.begin(), allSetOptions.end(),
		                    setting);
		if (it != allSetOptions.end()) {
			allSetOptions.erase(it);
		}

		bool comment;
		switch (commentedIn) {
		case commentedAlways:
			comment = true;
			break;
		case commentedNever:
			comment = false;
			break;
		default:
			comment = !options->Has(setting);
		}

		auto [valuePtr, ok] = options->Get(setting);
		tsoptions::CompilerOptionsValue value =
		    ok ? *valuePtr : defaultValue;

		if (comment) {
			push({tab + tab + "// \"" + setting + "\": " +
			      formatValueOrArray(setting, value) + ","});
		} else {
			push({tab + tab + "\"" + setting + "\": " +
			      formatValueOrArray(setting, value) + ","});
		}
	};

	push({"{"});
	push({tab + "// " +
	      ::tsc::localize(
	          locale,
	          
	              Visit_https_Colon_Slash_Slashaka_ms_Slashtsconfig_to_read_more_about_this_file,
	          "", {})});
	push({tab + "\"compilerOptions\": {"});

	emitHeader(File_Layout);
	emitOption("rootDir", "./src", commentedOptional);
	emitOption("outDir", "./dist", commentedOptional);

	newline();

	emitHeader(Environment_Settings);
	emitHeader(
	    
	        See_also_https_Colon_Slash_Slashaka_ms_Slashtsconfig_Slashmodule);
	emitOption("module", tsoptions::CompilerOptionsValue(ModuleKind::NodeNext),
	           commentedNever);
	emitOption("target", tsoptions::CompilerOptionsValue(ScriptTarget::ESNext),
	           commentedNever);
	emitOption("types", tsoptions::CompilerOptionsValue(tsoptions::JsonArray{}), commentedNever);
	if (auto [lib, ok] = options->Get("lib"); ok) {
		emitOption("lib", *lib, commentedNever);
	}
	emitHeader(For_nodejs_Colon);
	push({tab + tab + "// \"lib\": [\"esnext\"],"});
	push({tab + tab + "// \"types\": [\"node\"],"});
	emitHeader(X_and_npm_install_D_types_Slashnode);

	newline();

	emitHeader(Other_Outputs);
	emitOption("sourceMap" /*defaultValue*/, true, commentedNever);
	emitOption("declaration" /*defaultValue*/, true, commentedNever);
	emitOption("declarationMap" /*defaultValue*/, true, commentedNever);

	newline();

	emitHeader(Stricter_Typechecking_Options);
	emitOption("noUncheckedIndexedAccess" /*defaultValue*/, true,
	           commentedNever);
	emitOption("exactOptionalPropertyTypes" /*defaultValue*/, true,
	           commentedNever);

	newline();

	emitHeader(Style_Options);
	emitOption("noImplicitReturns" /*defaultValue*/, true, commentedOptional);
	emitOption("noImplicitOverride" /*defaultValue*/, true, commentedOptional);
	emitOption("noUnusedLocals" /*defaultValue*/, true, commentedOptional);
	emitOption("noUnusedParameters" /*defaultValue*/, true, commentedOptional);
	emitOption("noFallthroughCasesInSwitch" /*defaultValue*/, true,
	           commentedOptional);
	emitOption("noPropertyAccessFromIndexSignature" /*defaultValue*/, true,
	           commentedOptional);

	newline();

	emitHeader(Recommended_Options);
	emitOption("strict" /*defaultValue*/, true, commentedNever);
	emitOption("jsx", tsoptions::CompilerOptionsValue(JsxEmit::ReactJSX),
	           commentedNever);
	emitOption("verbatimModuleSyntax" /*defaultValue*/, true, commentedNever);
	emitOption("isolatedModules" /*defaultValue*/, true, commentedNever);
	emitOption("noUncheckedSideEffectImports" /*defaultValue*/, true,
	           commentedNever);
	emitOption("moduleDetection",
	           tsoptions::CompilerOptionsValue(ModuleDetectionKind::Force),
	           commentedNever);
	emitOption("skipLibCheck" /*defaultValue*/, true, commentedNever);

	// Write any user-provided options we haven't already
	if (!allSetOptions.empty()) {
		newline();
		while (!allSetOptions.empty()) {
			emitOption(allSetOptions[0],
			           options->GetOrZero(allSetOptions[0]), commentedNever);
		}
	}

	push({tab + "}"});
	push({"}"});
	push({""});

	std::string out;
	for (size_t i = 0; i < result.size(); i++) {
		if (i != 0) {
			out += "\n";
		}
		out += result[i];
	}
	return out;
}

}  // namespace tsc::execute::tsc
