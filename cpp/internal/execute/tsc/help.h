#pragma once

// help.go — port of tsc/internal/execute/tsc/help.go: --version/--help
// output including the option tables, categories, columns, and colors.

#include <string>
#include <vector>

#include "internal/execute/tsc/compile.h"
#include "internal/locale/locale.h"
#include "internal/tsoptions/tsoptions.h"

namespace tsc::execute::tsc {

// PrintVersion — help.go:14.
void PrintVersion(System* sys, locale::Locale locale);

// PrintHelp — help.go:18.
void PrintHelp(System* sys, locale::Locale locale,
               tsoptions::ParsedCommandLine* commandLine);

// getOptionsForHelp — help.go:26.
std::vector<const tsoptions::CommandLineOption*> getOptionsForHelp(
    tsoptions::ParsedCommandLine* commandLine);

// printEasyHelp — help.go:62.
void printEasyHelp(
    System* sys, locale::Locale locale,
    const std::vector<const tsoptions::CommandLineOption*>& simpleOptions);

// printAllHelp — help.go:111.
void printAllHelp(
    System* sys, locale::Locale locale,
    const std::vector<const tsoptions::CommandLineOption*>& options);

// PrintBuildHelp — help.go:136.
void PrintBuildHelp(
    System* sys, locale::Locale locale,
    const std::vector<const tsoptions::CommandLineOption*>& buildOptions);

// getHeader — help.go:40. Package-internal in Go; also used by init.cpp.
std::vector<std::string> getHeader(System* sys, std::string_view message);

// getDisplayNameTextOfOption — help.go:426.
std::string getDisplayNameTextOfOption(
    const tsoptions::CommandLineOption* option);

}  // namespace tsc::execute::tsc
