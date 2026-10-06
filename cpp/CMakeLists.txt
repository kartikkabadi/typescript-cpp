cmake_minimum_required(VERSION 3.20)
project(tscpp CXX C)

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

# mimalloc drop-in allocator (vendored third_party/mimalloc, v2.1.7).
# static.c compiles the whole allocator as one TU; alloc-override.c
# interposes the C malloc family; malloc_override.cpp interposes C++
# operator new/delete. Static link puts our symbols ahead of libc's.
option(TSCPP_MIMALLOC "Route all heap allocation through mimalloc" ON)
if(TSCPP_MIMALLOC)
	add_library(tsc_mimalloc STATIC
		third_party/mimalloc/src/static.c
		cmd/malloc_override.cpp)
	target_include_directories(tsc_mimalloc PUBLIC third_party/mimalloc/include)
	target_compile_definitions(tsc_mimalloc PRIVATE MI_MALLOC_OVERRIDE)
	set_target_properties(tsc_mimalloc PROPERTIES C_STANDARD 11)
	set(tsc_alloc_lib tsc_mimalloc)
else()
	set(tsc_alloc_lib "")
endif()

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
	internal/compiler/checkerpool.cpp
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
	# === slice: patience ===
	internal/patience/patience.cpp
	# === slice: printer ===
	internal/printer/changetrackerwriter.cpp
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
	# === slice: ls-coreC ===
	internal/ls/ls.cpp
	internal/ls/codelens.cpp
	internal/ls/definition.cpp
	internal/ls/documenthighlights.cpp
	internal/ls/folding.cpp
	internal/ls/format.cpp
	internal/ls/inlay_hints.cpp
	internal/ls/rename.cpp
	internal/ls/selectionranges.cpp
	internal/ls/semantictokens.cpp
	internal/ls/sourcedefinition.cpp
	internal/ls/symbols.cpp
	internal/ls/callhierarchy.cpp
	internal/ls/hover.cpp
	internal/ls/hovericon.cpp
	internal/ls/autoinsert.cpp
	internal/ls/linkedediting.cpp
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
	internal/tracing/tracing.cpp
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
	# === slice: tsctests ===
	internal/execute/tsctests/fs.cpp
	internal/execute/tsctests/mock_watch_backend.cpp
	internal/execute/tsctests/readablebuildinfo.cpp
	internal/execute/tsctests/runner.cpp
	internal/execute/tsctests/sys.cpp
	# === slice: testrunner ===
	internal/gostd/regexp.cpp
	internal/testrunner/runner.cpp
	internal/testrunner/test_case_parser.cpp
	internal/testrunner/testrunner_deps.cpp
	internal/testrunner/transpile_runner.cpp
	internal/testrunner/compiler_runner.cpp
	# === slice: testutil ===
	internal/patience/patience.cpp
	internal/testutil/harnessutil/harnessutil.cpp
	internal/testutil/harnessutil/recorderfs.cpp
	internal/testutil/harnessutil/sourcemap_recorder.cpp
	internal/testutil/baseline/baseline.cpp
	internal/testutil/tsbaseline/util.cpp
	internal/testutil/tsbaseline/error_baseline.cpp
	internal/testutil/tsbaseline/js_emit_baseline.cpp
	internal/testutil/tsbaseline/sourcemap_baseline.cpp
	internal/testutil/tsbaseline/sourcemap_record_baseline.cpp
	internal/testutil/tsbaseline/module_resolution_baseline.cpp
	internal/testutil/tsbaseline/type_symbol_baseline.cpp
	internal/testutil/tsbaseline/contentmapper_baseline.cpp
	internal/testutil/contentmappertest/contentmappertest.cpp
	# === slice: testutil-leaves ===
	internal/lsp/lsp.cpp
	internal/testutil/autoimporttestutil/autoimporttestutil.cpp
	internal/testutil/emittestutil/emittestutil.cpp
	internal/testutil/filefixture/filefixture.cpp
	internal/testutil/fixtures/fixtures.cpp
	internal/testutil/fsbaselineutil/fsbaselineutil.cpp
	internal/testutil/jstest/node.cpp
	internal/testutil/lsptestutil/lspclient.cpp
	internal/testutil/parsetestutil/parsetestutil.cpp
	internal/testutil/projecttestutil/clientmock_generated.cpp
	internal/testutil/projecttestutil/npmexecutormock_generated.cpp
	internal/testutil/projecttestutil/projecttestutil.cpp
	internal/testutil/stringtestutil/stringtestutil.cpp
	internal/testutil/testutil.cpp
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
	# === slice: api ===
	internal/api/encoder/encoder.cpp
	internal/api/encoder/encoder_generated.cpp
	internal/api/encoder/decoder.cpp
	internal/api/encoder/decoder_generated.cpp
	internal/api/requestfilesystem/requestfilesystem.cpp
	internal/api/requestfilesystem/pathtree.cpp
	internal/api/requestfilesystem/filechanges.cpp
	internal/api/proto.cpp
    internal/api/protocol_msgpack.cpp
    internal/api/callbackfs.cpp
	internal/api/module_resolution.cpp
	internal/api/session.cpp
	internal/api/server.cpp
	internal/project/project.cpp
	internal/lsp/lsproto/lsproto.cpp
	# === slice: project ===
	internal/project/ata/ata.cpp
	internal/project/ata/discovertypings.cpp
	internal/project/ata/typesmap.cpp
	internal/project/ata/validatepackagename.cpp
	internal/project/checkerpool.cpp
	internal/project/compilerhost.cpp
	internal/project/configfileregistrybuilder.cpp
	internal/project/extendedconfigcache.cpp
	internal/project/overlayfs.cpp
	internal/project/parsecache.cpp
	internal/project/projectcollection.cpp
	internal/project/projectcollectionbuilder.cpp
	internal/project/session.cpp
	internal/project/snapshot.cpp
	internal/project/snapshotfs.cpp
	internal/project/snapshothost.cpp
	internal/project/watch.cpp
	internal/ls/ls.cpp
	internal/ls/lsconv/lsconv.cpp
	# === slice: ls-coreA ===
	internal/ls/lsdeps.cpp
	internal/ls/api.cpp
	internal/ls/utilities.cpp
	internal/ls/jsdoc.cpp
	internal/ls/jsdoc_snippet.cpp
	internal/ls/displaypartswriter.cpp
	internal/ls/completions.cpp
	internal/ls/string_completions.cpp
	# === slice: fswatch ===
	internal/fswatch/debounce.cpp
	internal/fswatch/event.cpp
	internal/fswatch/fanotify.cpp
	internal/fswatch/inotify.cpp
	internal/fswatch/pathcompare.cpp
	internal/fswatch/util.cpp
	internal/fswatch/walkdir.cpp
	internal/fswatch/watcher.cpp
	# === slice: ls-autoimport ===
	internal/project/logging/logging.cpp
	internal/ls/autoimport/aliasresolver.cpp
	internal/ls/autoimport/export.cpp
	internal/ls/autoimport/extract.cpp
	internal/ls/autoimport/fix.cpp
	internal/ls/autoimport/import_adder.cpp
	internal/ls/autoimport/index.cpp
	internal/ls/autoimport/registry.cpp
	internal/ls/autoimport/specifiers.cpp
	internal/ls/autoimport/util.cpp
	internal/ls/autoimport/view.cpp

	# === slice: lsp-proto ===
	internal/lsp/lsproto/lsproto.cpp
	internal/lsp/lsproto/lsproto_generated.cpp
	internal/lsp/lspwatcher/lspwatcher.cpp

	# === slice: ls-coreB ===
	internal/ls/languageservice.cpp
	internal/ls/diagnostics.cpp
	internal/ls/source_map.cpp
	internal/ls/importTracker.cpp
	internal/ls/crossproject.cpp
	internal/ls/file_rename.cpp
	internal/ls/codeactions.cpp
	internal/ls/codeactions_importfixes.cpp
	internal/ls/codeactions_fixclassincorrectlyimplementsinterface.cpp
	internal/ls/codeactions_missingmemberfixer.cpp
	internal/ls/codeactions_fixmissingtypeannotation.cpp
	internal/ls/organizeimports.cpp
	internal/ls/signaturehelp.cpp
	internal/ls/findallreferences.cpp

	# === slice: lsp-server ===
	internal/lsp/lsp_logger.cpp
	internal/lsp/lsp_progress.cpp
	internal/lsp/lsp_stack_sanitizer.cpp
	internal/lsp/lsp_server.cpp
	internal/lsp/lsp_handlers.cpp

	# === slice: fourslash ===
	internal/fourslash/fourslash.cpp
	internal/fourslash/fourslash_deps.cpp
	internal/fourslash/test_parser.cpp
	internal/fourslash/baselineutil.cpp
	internal/fourslash/statebaseline.cpp
	internal/fourslash/semantictokens.cpp
	internal/fourslash/tests/util/util.cpp
	internal/testutil/fsbaselineutil/differ.cpp
)

target_link_libraries(tsc PUBLIC z) # zlib — pprof profile writer

add_executable(tscpp cmd/tscpp/main.cpp cmd/tscpp/sys.cpp cmd/tscpp/lsp.cpp cmd/tscpp/api.cpp)
target_link_libraries(tscpp PRIVATE ${tsc_alloc_lib} tsc)

# tsctestrunner — scenario-runner binary for the ported tscInput tests.
# Scenarios self-register via REGISTER_TSCTEST; enumerate them with glob
# so adding a tests/*.cpp file needs no list maintenance.
file(GLOB tsctestrunner_tests CONFIGURE_DEPENDS
	internal/execute/tsctests/tests/*.cpp)
add_executable(tsctestrunner cmd/tsctestrunner/main.cpp
	${tsctestrunner_tests})
target_link_libraries(tsctestrunner PRIVATE ${tsc_alloc_lib} tsc)

# fourslash test runner — ported Go fourslash tests register statically
# (internal/fourslash/tests/registry.h) and run one test per forked child.
file(GLOB FOURSLASH_TEST_SOURCES CONFIGURE_DEPENDS
	internal/fourslash/tests/tests_*.cpp)
add_executable(fourslashrunner cmd/fourslashrunner/main.cpp
	internal/fourslash/tests/registry.cpp ${FOURSLASH_TEST_SOURCES})
target_link_libraries(fourslashrunner PRIVATE ${tsc_alloc_lib} tsc)
