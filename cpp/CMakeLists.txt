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
	# === slice: format ===
	internal/format/api.cpp
	internal/format/context.cpp
	internal/format/indent.cpp
	internal/format/rule.cpp
	internal/format/rulecontext.cpp
	internal/format/rules.cpp
	internal/format/rulesmap.cpp
	internal/format/scanner.cpp
	internal/format/span.cpp
	internal/format/util.cpp
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
	internal/transformers/utilities.cpp
	internal/transformers/destructuring.cpp
	internal/transformers/estransforms/utilities.cpp
	internal/transformers/estransforms/logicalassignment.cpp
	internal/transformers/estransforms/taggedtemplate.cpp
	internal/transformers/estransforms/optionalchain.cpp
	internal/transformers/estransforms/optionalcatch.cpp
	internal/transformers/estransforms/nullishcoalescing.cpp
	internal/transformers/estransforms/exponentiation.cpp
	internal/transformers/estransforms/objectrestspread.cpp
	internal/transformers/estransforms/usestrict.cpp
	internal/transformers/estransforms/definitions.cpp
	internal/transformers/estransforms/classthis.cpp
	internal/transformers/estransforms/namedevaluation.cpp
	internal/transformers/estransforms/esdecorator.cpp
	internal/transformers/estransforms/classfields.cpp
	internal/transformers/estransforms/async.cpp
	internal/transformers/estransforms/forawait.cpp
	internal/transformers/estransforms/using.cpp
	internal/transformers/tstransforms/utilities.cpp
	internal/transformers/tstransforms/importelision.cpp
	internal/transformers/tstransforms/typeeraser.cpp
	internal/transformers/tstransforms/typeserializer.cpp
	internal/transformers/tstransforms/runtimesyntax.cpp
	internal/transformers/tstransforms/metadata.cpp
	internal/transformers/tstransforms/legacydecorators.cpp
	internal/transformers/moduletransforms/utilities.cpp
	internal/transformers/moduletransforms/externalmoduleinfo.cpp
	internal/transformers/moduletransforms/esmodule.cpp
	internal/transformers/moduletransforms/impliedmodule.cpp
	internal/transformers/moduletransforms/commonjsmodule.cpp
	internal/transformers/inliners/constenum.cpp
	# === slice: jsxtransforms ===
	internal/transformers/jsxtransforms/jsxtransforms.cpp
	internal/transformers/declarations/diagnostics.cpp
	internal/transformers/declarations/supplementalreferences.cpp
	internal/transformers/declarations/tracker.cpp
	internal/transformers/declarations/transform.cpp
	internal/transformers/declarations/util.cpp
	# === slice: module ===
	internal/core/compileroptions.cpp
	internal/module/util.cpp
	internal/module/cache.cpp
	internal/module/resolver.cpp
	internal/module/staticresolver.cpp
	# === slice: outputpaths ===
	internal/outputpaths/outputpaths.cpp
	# === slice: sourcemap ===
	internal/sourcemap/generator.cpp
	internal/sourcemap/decoder.cpp
	internal/sourcemap/lineinfo.cpp
	internal/sourcemap/source_mapper.cpp
	internal/sourcemap/util.cpp
	# === slice: modulespecifiers ===
	internal/modulespecifiers/specifiers.cpp
	internal/modulespecifiers/util.cpp
	internal/modulespecifiers/preferences.cpp
	internal/packagejson/packagejson.cpp
	internal/pseudochecker/pseudochecker.cpp
	# === slice: spanmap ===
	internal/spanmap/spanmap.cpp
	# === slice: ls-foundation ===
	internal/ls/lsutil/asi.cpp
	internal/ls/lsutil/children.cpp
	internal/ls/lsutil/completednode.cpp
	internal/ls/lsutil/formatcodeoptions.cpp
	internal/ls/lsutil/organizeimports.cpp
	internal/ls/lsutil/symbol_display.cpp
	internal/ls/lsutil/userpreferences.cpp
	internal/ls/lsutil/utilities.cpp
	internal/ls/change/delete.cpp
	internal/ls/change/tracker.cpp
	internal/ls/change/trackerimpl.cpp
	# === slice: infra ===
	internal/locale/locale.cpp
	# === slice: contentmapper ===
	internal/json/json.cpp
	internal/jsonrpc/jsonrpc.cpp
	internal/contentmapper/contentmapper.cpp
	internal/contentmapper/host.cpp
	internal/contentmapper/hostimpl.cpp
	internal/contentmapper/transform.cpp
	# === slice: ipc ===
	internal/ipc/conn_async.cpp
	internal/ipc/conn_sync.cpp
	internal/ipc/protocol_jsonrpc.cpp
	internal/ipc/timing.cpp
	internal/ipc/transport.cpp
	internal/ipc/transport_unix.cpp
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
	# === slice: infra ===
	internal/diagnosticwriter/diagnosticwriter.cpp
	internal/diagnostics/localize.cpp
	internal/glob/glob.cpp
	internal/json/json.cpp
	internal/locale/locale.cpp
	internal/nativepath/nativepath.cpp
	internal/osutil/osutil.cpp
	internal/pprof/pprof.cpp
	internal/repo/paths.cpp
	# === slice: incremental ===
	internal/execute/incremental/affectedfileshandler.cpp
	internal/execute/incremental/buildinfo.cpp
	internal/execute/incremental/buildinfotosnapshot.cpp
	internal/execute/incremental/emitfileshandler.cpp
	internal/execute/incremental/host.cpp
	internal/execute/incremental/incremental.cpp
	internal/execute/incremental/program.cpp
	internal/execute/incremental/programtosnapshot.cpp
	internal/execute/incremental/referencemap.cpp
	internal/execute/incremental/snapshot.cpp
	internal/execute/incremental/snapshottobuildinfo.cpp
	# === slice: execute-tsc ===
	internal/execute/execute.cpp
	internal/execute/watcher.cpp
	internal/execute/tsc/compile.cpp
	internal/execute/tsc/diagnostics.cpp
	internal/execute/tsc/emit.cpp
	internal/execute/tsc/extendedconfigcache.cpp
	internal/execute/tsc/help.cpp
	internal/execute/tsc/init.cpp
	internal/execute/tsc/statistics.cpp
	internal/execute/watchmanager/watchbackend.cpp
	internal/execute/watchmanager/watchmanager.cpp
	# === slice: execute-build ===
	internal/execute/build/buildtask.cpp
	internal/execute/build/compilerhost.cpp
	internal/execute/build/host.cpp
	internal/execute/build/orchestrator.cpp
	internal/execute/build/uptodatestatus.cpp
	# === slice: bundled ===
	internal/bundled/bundled.cpp
	internal/bundled/bundled_generated.cpp
	# === slice: vfs ===
	internal/vfs/vfs.cpp
	internal/vfs/internal/internal.cpp
	internal/vfs/iovfs/iovfs.cpp
	internal/vfs/osvfs/osvfs.cpp
	internal/vfs/vfsmatch/vfsmatch.cpp
	internal/vfs/vfstest/vfstest.cpp
	# === slice: transpile ===
	internal/transpile/transpile.cpp
	# === slice: fswatch ===
	internal/fswatch/debounce.cpp
	internal/fswatch/event.cpp
	internal/fswatch/fanotify.cpp
	internal/fswatch/inotify.cpp
	internal/fswatch/pathcompare.cpp
	internal/fswatch/util.cpp
	internal/fswatch/walkdir.cpp
	internal/fswatch/watcher.cpp
)

target_link_libraries(tsc PUBLIC z) # zlib — pprof profile writer

add_executable(tscpp cmd/tscpp/main.cpp cmd/tscpp/sys.cpp)
target_link_libraries(tscpp PRIVATE tsc)
