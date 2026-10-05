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
	internal/binder/binder.cpp
	internal/binder/nameresolver.cpp
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
	# === slice: module ===
	internal/core/compileroptions.cpp
	internal/module/util.cpp
	internal/module/cache.cpp
	internal/module/resolver.cpp
	internal/module/staticresolver.cpp
	internal/packagejson/packagejson.cpp
)

add_executable(tscpp cmd/tscpp/main.cpp)
target_link_libraries(tscpp PRIVATE tsc)
