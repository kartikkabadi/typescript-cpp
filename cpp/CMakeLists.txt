cmake_minimum_required(VERSION 3.20)
project(tscpp CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
	add_compile_options(-O3 -DNDEBUG)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(tsc STATIC
	internal/ast/ast.cpp
	internal/ast/utilities.cpp
	internal/astnav/tokens.cpp
	internal/binder/binder.cpp
	internal/binder/nameresolver.cpp
	internal/binder/referenceresolver.cpp
	internal/checker/checker.cpp
	internal/checker/checker_walk.cpp
	internal/checker/checker_jsdoc.cpp
	internal/checker/checker_jsx.cpp
	internal/checker/checker_symbolaccess.cpp
	internal/checker/checker_tracer.cpp
	internal/checker/checker_grammar.cpp
	internal/checker/checker_declchecks.cpp
	internal/checker/checker_declchecks2.cpp
	internal/checker/checker_stmtclass.cpp
	internal/checker/checker_markrefs.cpp
	internal/checker/checker_nodebuilder.cpp
	internal/checker/checker_nodecopy.cpp
	internal/checker/checker_printer.cpp
	internal/checker/checker_signatures.cpp
	internal/checker/checker_typenodes.cpp
	internal/checker/checker_inference.cpp
	internal/checker/checker_instantiate.cpp
	internal/checker/checker_members.cpp
	internal/checker/checker_moduletarget.cpp
	internal/checker/checker_widen.cpp
	internal/checker/checker_decltypes.cpp
	internal/checker/checker_contextual.cpp
	internal/checker/checker_exports.cpp
	internal/checker/checker_expressions_b.cpp
	internal/checker/checker_expressions_a.cpp
	internal/checker/checker_typeops.cpp
	internal/checker/checker_expressions_c.cpp
	internal/checker/checker_utilities.cpp
	internal/checker/checker_services.cpp
	internal/checker/checker_services2.cpp
	internal/checker/checker_flow.cpp
	internal/checker/checker_relater.cpp
	internal/checker/checker_emitresolver.cpp
	internal/checker/mapper.cpp
	internal/compiler/emitter.cpp
	internal/compiler/fileloader.cpp
	internal/compiler/host.cpp
	internal/compiler/includeprocessor.cpp
	internal/compiler/program.cpp
	internal/evaluator/evaluator.cpp
	internal/jsnum/jsnum.cpp
	internal/module/resolver.cpp
	internal/scanner/scanner.cpp
	internal/scanner/regexp.cpp
	internal/parser/parser.cpp
	internal/parser/jsdoc.cpp
	internal/parser/reparser.cpp
	internal/parser/references.cpp
	# === slice: printer ===
	internal/printer/textwriter.cpp
	internal/printer/emitcontext.cpp
	internal/printer/namegenerator.cpp
	internal/printer/utilities.cpp
	internal/printer/helpers.cpp
	internal/printer/factory.cpp
	internal/printer/printer.cpp
	internal/printer/syntheticfile.cpp
	# === slice: transformers ===
	internal/transformers/transformer.cpp
	internal/transformers/declarations/declarations.cpp
	internal/transformers/utilities.cpp
	internal/transformers/destructuring.cpp
	internal/transformers/estransforms/utilities.cpp
	internal/transformers/tstransforms/utilities.cpp
	internal/transformers/moduletransforms/utilities.cpp
	internal/transformers/inliners/constenum.cpp
	# === slice: jsxtransforms ===
	internal/transformers/jsxtransforms/jsxtransforms.cpp
	# === slice: module ===
	internal/core/compileroptions.cpp
	internal/module/util.cpp
	internal/module/cache.cpp
	internal/module/resolver.cpp
	internal/module/staticresolver.cpp
	# === slice: outputpaths ===
	internal/outputpaths/outputpaths.cpp
	# === slice: modulespecifiers ===
	internal/modulespecifiers/specifiers.cpp
	internal/modulespecifiers/util.cpp
	internal/modulespecifiers/preferences.cpp
	internal/packagejson/packagejson.cpp
	internal/pseudochecker/pseudochecker.cpp
	# === slice: tsoptions ===
	internal/tsoptions/commandlineoption.cpp
	internal/tsoptions/commandlineparser.cpp
	internal/tsoptions/contentmappers.cpp
	internal/tsoptions/declsbuild.cpp
	internal/tsoptions/declscompiler.cpp
	internal/tsoptions/declstypeacquisition.cpp
	internal/tsoptions/declswatch.cpp
	internal/tsoptions/diagnostics.cpp
	internal/tsoptions/enummaps.cpp
	internal/tsoptions/errors.cpp
	internal/tsoptions/namemap.cpp
	internal/tsoptions/parsedbuildcommandline.cpp
	internal/tsoptions/parsedcommandline.cpp
	internal/tsoptions/parsinghelpers.cpp
	internal/tsoptions/showconfig.cpp
	internal/tsoptions/tsconfigparsing.cpp
	internal/tsoptions/wildcarddirectories.cpp
)

add_executable(tscpp cmd/tscpp/main.cpp)
target_link_libraries(tscpp PRIVATE tsc)
