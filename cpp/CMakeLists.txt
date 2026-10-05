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
	internal/checker/checker_members.cpp
	internal/checker/mapper.cpp
	internal/evaluator/evaluator.cpp
	internal/jsnum/jsnum.cpp
	internal/scanner/scanner.cpp
	internal/scanner/regexp.cpp
	internal/parser/parser.cpp
	internal/parser/jsdoc.cpp
	internal/parser/reparser.cpp
	internal/parser/references.cpp
)

add_executable(tscpp cmd/tscpp/main.cpp)
target_link_libraries(tscpp PRIVATE tsc)
