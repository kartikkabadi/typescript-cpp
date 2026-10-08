// Port of tsc/internal/compiler/contentmapper_test.go.
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/bundled/bundled.h"
#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/locale/locale.h"
#include "internal/spanmap/spanmap.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
using namespace tsc;

// fakeContentMapperHost — contentmapper_test.go's fakeContentMapperHost: a
// Project whose Transform delegates to a test-supplied callback; every other
// method returns empty/nil.
struct fakeContentMapperHost : contentmapper::Project {
	std::function<std::pair<contentmapper::Result, gostd::Error>(
		const std::string& fileName, const std::string& content)>
		transform;

	gostd::Error Refresh() override { return nullptr; }
	std::pair<std::vector<std::string>, gostd::Error> Identities() override {
		return {{}, nullptr};
	}
	std::pair<std::string, gostd::Error>
	Identity(contentmapper::Mapper*) override {
		return {"test", nullptr};
	}
	std::pair<std::vector<std::string>, gostd::Error> WatchedFiles() override {
		return {{}, nullptr};
	}
	std::vector<contentmapper::OptionDiagnostic> Diagnostics() override {
		return {};
	}
	std::pair<contentmapper::Result, gostd::Error>
	Transform(contentmapper::Mapper*,
	          const contentmapper::Request& request) override {
		return transform(request.FileName, request.Content);
	}
	gostd::Error Close() override { return nullptr; }
};

static compiler::SimpleProgram* newContentMapperProgramWithOptions(
	T* t, const std::shared_ptr<contentmapper::Project>& contentMapperProject,
	const std::unordered_map<std::string, std::string>& files,
	const std::vector<std::string>& rootFiles, CompilerOptions* options) {
	t->Helper();
	if (!bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}
	std::shared_ptr<vfs::FS> fs =
		vfs::vfstest::FromMap({}, false /*useCaseSensitiveFileNames*/);
	fs = bundled::WrapFS(fs);
	for (const auto& [name, content] : files) {
		(void)fs->WriteFile(name, content);
	}

	auto* mapper = new contentmapper::Mapper();
	mapper->Definition.Package = "vue";
	mapper->Definition.Extensions = {".vue"};
	mapper->Manifest.Name = "vue-mapper";
	mapper->Manifest.Version = "1.0.0";

	auto* parsedOpts = new tsoptions::ParsedOptions();
	parsedOpts->FileNames = rootFiles;
	parsedOpts->CompilerOptions = options;
	parsedOpts->ContentMappers = {mapper};
	auto* config = new tsoptions::ParsedCommandLine();
	config->ParsedConfig = parsedOpts;

	compiler::ProgramOptions programOpts;
	programOpts.Config = config;
	programOpts.Host = compiler::NewCompilerHost(
		"/src", fs, bundled::LibPath(), nullptr, nullptr, contentMapperProject);
	// Load files on the calling goroutine for deterministic diagnostics
	// ordering.
	programOpts.SingleThreaded = Tristate::True;
	return compiler::NewProgram(programOpts);
}

static compiler::SimpleProgram* newContentMapperProgram(
	T* t, const std::shared_ptr<contentmapper::Project>& contentMapperProject,
	const std::unordered_map<std::string, std::string>& files,
	const std::vector<std::string>& rootFiles) {
	auto* options = new CompilerOptions();
	options->SkipLibCheck = Tristate::True;
	options->Module = ModuleKind::ESNext;
	options->ModuleResolution = ModuleResolutionKind::Bundler;
	return newContentMapperProgramWithOptions(t, contentMapperProject, files,
	                                          rootFiles, options);
}

// collectContentMapperDiagnostics — slices.Concat of syntactic, semantic and
// program diagnostics (nil file argument = all files).
static std::vector<Diagnostic*>
collectContentMapperDiagnostics(compiler::SimpleProgram* program) {
	std::vector<Diagnostic*> out = program->GetSyntacticDiagnostics(nullptr);
	for (auto* d : program->GetSemanticDiagnostics(nullptr)) {
		out.push_back(d);
	}
	for (auto* d : program->GetProgramDiagnostics()) {
		out.push_back(d);
	}
	return out;
}

static void TestContentMapperVirtualExtensionSetsImpliedNodeFormat(T* t) {
	t->Parallel();
	auto* options = new CompilerOptions();
	options->SkipLibCheck = Tristate::True;
	options->Module = ModuleKind::NodeNext;
	options->ModuleResolution = ModuleResolutionKind::NodeNext;
	auto host = std::make_shared<fakeContentMapperHost>();
	host->transform = [](const std::string& fileName,
	                     const std::string& content)
	    -> std::pair<contentmapper::Result, gostd::Error> {
		return {contentmapper::Result{"export {};", ".mts", {},
		                            spanmap::New({})},
		        nullptr};
	};
	compiler::SimpleProgram* program = newContentMapperProgramWithOptions(
		t, host, {{"/src/Component.vue", "<template />"}},
		{"/src/Component.vue"}, options);

	SourceFile* file = program->GetSourceFile("/src/Component.vue");
	gotest::assert::Assert(t, file != nullptr, "file != nil");
	gotest::assert::Equal(
		t,
		(int)program->GetSourceFileMetaData(file->Path()).ImpliedNodeFormat,
		(int)ResolutionModeESM);
}

static void TestContentMapperDirectivesPreserveIncrementalGlobals(T* t) {
	t->Parallel();
	for (auto policy : {MappedDiagnosticDirectivePolicy::Ignore,
	                    MappedDiagnosticDirectivePolicy::Expect}) {
		t->Run(policy == MappedDiagnosticDirectivePolicy::Expect ? "expect"
		                                                       : "ignore",
		       [policy](T* t) {
			       t->Parallel();
			       const std::string text =
			           "export function values() { function* generator() { "
			           "yield 1; } }";
			       auto host = std::make_shared<fakeContentMapperHost>();
			       host->transform =
			           [policy](const std::string& fileName,
			                    const std::string& content)
			           -> std::pair<contentmapper::Result, gostd::Error> {
				       // The virtual source is unchanged; the directive
				       // covers the entire file, including offset zero.
				       contentmapper::Result result;
				       result.Text = content;
				       result.VirtualExtension = ".ts";
				       result.Mappings = spanmap::New({spanmap::Segment{
				           0, TextPos(content.size()), 0,
				           TextPos(content.size()), spanmap::KindVerbatim,
				           spanmap::FeatureAll}});
				       result.DiagnosticDirectives = {
				           MappedDiagnosticDirective{
				               /*.OriginalRange =*/
				               {0, TextPos(content.size())},
				               /*.VirtualRange =*/
				               {0, TextPos(content.size())},
				               /*.Policy =*/ policy,
				               /*.UnusedCode =*/ 2578,
				               /*.UnusedMessageText =*/
				               "Unused mapped expect directive.",
				               /*.Source =*/ "vue",
				           }};
				       return {result, nullptr};
			       };
			       auto* options = new CompilerOptions();
			       options->Lib = {"lib.es5.d.ts"};
			       options->SkipLibCheck = Tristate::True;
			       options->Module = ModuleKind::ESNext;
			       options->ModuleResolution = ModuleResolutionKind::Bundler;
			       compiler::SimpleProgram* program =
			           newContentMapperProgramWithOptions(
			               t, host, {{"/src/Component.vue", text}},
			               {"/src/Component.vue"}, options);
			       gotest::assert::Equal(
			           t, (int)program->GetGlobalDiagnostics().size(), 0);
			       SourceFile* file =
			           program->GetSourceFile("/src/Component.vue");
			       std::vector<Diagnostic*> diags;
			       for (auto& pair :
			            program->GetSemanticDiagnosticsForIncremental({file})) {
				       if (pair.first == file) {
					       diags = pair.second;
				       }
			       }
			       int globals = 0, unused = 0;
			       for (auto* diag : diags) {
				       if (diag->File() == nullptr) {
					       gotest::assert::Equal(
					           t, diag->Code(),
					           Cannot_find_global_type_0->code);
					       gotest::assert::Equal(
					           t, diag->MessageArgs()[0],
					           std::string("IterableIterator"));
					       globals++;
				       } else {
					       gotest::assert::Equal(t, diag->File(), file);
					       gotest::assert::Equal(
					           t, std::string(diag->Source()),
					           std::string("vue"));
					       gotest::assert::Equal(t, diag->Code(), int32_t(2578));
					       unused++;
				       }
			       }
			       gotest::assert::Equal(t, globals, 1);
			       gotest::assert::Equal(
			           t, unused,
			           policy == MappedDiagnosticDirectivePolicy::Expect ? 1
			                                                             : 0);
		       });
	}
}

static void TestCompositeProjectContentMapperSupplementalRoots(T* t) {
	t->Parallel();
	auto contentMapperHost = std::make_shared<fakeContentMapperHost>();
	contentMapperHost->transform =
	    [](const std::string& fileName, const std::string& content)
	    -> std::pair<contentmapper::Result, gostd::Error> {
		contentmapper::Result result;
		result.Text = "export {};";
		result.VirtualExtension = ".ts";
		result.Mappings = spanmap::New({});
		result.Supplemental = {contentmapper::MappedResult{
		    "export {};", ".mts", spanmap::New({}), {}}};
		return {result, nullptr};
	};
	auto options = []() -> CompilerOptions* {
		auto* opts = new CompilerOptions();
		opts->Composite = Tristate::True;
		opts->SkipLibCheck = Tristate::True;
		opts->Module = ModuleKind::ESNext;
		opts->ModuleResolution = ModuleResolutionKind::Bundler;
		return opts;
	};

	t->Run("listed canonical root", [&](T* t) {
		t->Parallel();
		compiler::SimpleProgram* program = newContentMapperProgramWithOptions(
			t, contentMapperHost, {{"/src/Component.vue", "<template />"}},
			{"/src/Component.vue"}, options());

		std::vector<Diagnostic*> programDiagnostics =
			collectContentMapperDiagnostics(program);
		bool hasUnlistedFileDiagnostic = false;
		for (auto* diagnostic : programDiagnostics) {
			if (diagnostic->Code() ==
			    File_0_is_not_listed_within_the_file_list_of_project_1_Projects_must_list_all_files_or_use_an_include_pattern
			        ->code) {
				hasUnlistedFileDiagnostic = true;
			}
		}
		gotest::assert::Assert(
			t, !hasUnlistedFileDiagnostic,
			"supplemental output should be covered by its listed canonical "
			"root");
	});

	t->Run("imported canonical file", [&](T* t) {
		t->Parallel();
		compiler::SimpleProgram* program = newContentMapperProgramWithOptions(
			t, contentMapperHost,
			{
			    {"/src/index.ts", R"(import "./Component.vue";)"},
			    {"/src/Component.vue", "<template />"},
			},
			{"/src/index.ts"}, options());

		int unlistedFileDiagnosticCount = 0;
		for (auto* diagnostic : collectContentMapperDiagnostics(program)) {
			if (diagnostic->Code() ==
			    File_0_is_not_listed_within_the_file_list_of_project_1_Projects_must_list_all_files_or_use_an_include_pattern
			        ->code) {
				unlistedFileDiagnosticCount++;
			}
		}
		gotest::assert::Equal(t, unlistedFileDiagnosticCount, 2);
	});
}

static void TestContentMapperInvalidMappings(T* t) {
	t->Parallel();

	const std::string transformed = "export const x = 1;\n";
	const std::string original = "<template>x</template>\n";
	spanmap::SpanMap* mappings = spanmap::New({
	    {0, 10, 0, 0, spanmap::KindAtom, 0},
	    {5, TextPos(transformed.size()), 0, 0, spanmap::KindAtom, 0},
	});
	std::unordered_map<std::string, std::string> files = {
	    {"/src/app.ts", R"(import "./Component.vue";)"},
	    {"/src/Component.vue", original},
	};
	auto contentMapperHost = std::make_shared<fakeContentMapperHost>();
	contentMapperHost->transform =
	    [&](const std::string& fileName, const std::string& content)
	    -> std::pair<contentmapper::Result, gostd::Error> {
		contentmapper::Result result;
		result.Text = transformed;
		result.VirtualExtension = ".ts";
		result.Mappings = mappings;
		return {result, nullptr};
	};
	compiler::SimpleProgram* program = newContentMapperProgram(
		t, contentMapperHost, files, {"/src/app.ts"});
	std::vector<Diagnostic*> programDiagnostics =
		collectContentMapperDiagnostics(program);
	bool found = false;
	for (auto* diagnostic : programDiagnostics) {
		if (diagnostic->Code() ==
		    The_content_mapper_0_produced_overlapping_or_out_of_order_position_mappings_near_virtual_offset_1
		        ->code) {
			found = true;
		}
	}
	gotest::assert::Assert(t, found,
	                       "expected an invalid mapping diagnostic");
}

static void TestContentMapperSourceFileState(T* t) {
	t->Parallel();

	t->Run("successful synthesized empty file", [](T* t) {
		t->Parallel();
		auto host = std::make_shared<fakeContentMapperHost>();
		host->transform = [](const std::string& fileName,
		                     const std::string& content)
		    -> std::pair<contentmapper::Result, gostd::Error> {
			return {contentmapper::Result{"export {};", ".ts", {},
			                            spanmap::New({})},
			        nullptr};
		};
		compiler::SimpleProgram* program =
		    newContentMapperProgram(t, host, {{"/src/empty.vue", ""}},
		                            {"/src/empty.vue"});
		SourceFile* file = program->GetSourceFile("/src/empty.vue");
		gotest::assert::Assert(t, file != nullptr, "file != nil");
		gotest::assert::Equal(t, file->OriginalText(), std::string(""));
		gotest::assert::Equal(t, file->ContentMapper(),
		                      std::string("vue-mapper@1.0.0"));
		gotest::assert::Assert(t, !file->IsContentMapperFailureStub(),
		                       "!file.IsContentMapperFailureStub()");
	});

	t->Run("failed transform", [](T* t) {
		t->Parallel();
		auto host = std::make_shared<fakeContentMapperHost>();
		host->transform = [](const std::string& fileName,
		                     const std::string& content)
		    -> std::pair<contentmapper::Result, gostd::Error> {
			return {contentmapper::Result{}, gostd::newError("failed")};
		};
		compiler::SimpleProgram* program = newContentMapperProgram(
			t, host, {{"/src/fail.vue", "original"}}, {"/src/fail.vue"});
		SourceFile* file = program->GetSourceFile("/src/fail.vue");
		gotest::assert::Assert(t, file != nullptr, "file != nil");
		gotest::assert::Equal(t, file->OriginalText(), std::string("original"));
		gotest::assert::Equal(t, file->ContentMapper(),
		                      std::string("vue-mapper@1.0.0"));
		gotest::assert::Assert(t, file->IsContentMapperFailureStub(),
		                       "file.IsContentMapperFailureStub()");
	});

	t->Run("project error is localized", [](T* t) {
		t->Parallel();
		auto host = std::make_shared<fakeContentMapperHost>();
		host->transform = [](const std::string& fileName,
		                     const std::string& content)
		    -> std::pair<contentmapper::Result, gostd::Error> {
			auto* projectError = new contentmapper::ProjectError();
			projectError->Kind =
			    contentmapper::ProjectErrorKindMalformedResponse;
			return {contentmapper::Result{},
			        gostd::Error(contentmapper::NewTransformError(
			            contentmapper::TransformErrorKindProject,
			            gostd::Error(projectError)))};
		};
		compiler::SimpleProgram* program = newContentMapperProgram(
			t, host, {{"/src/fail.vue", "original"}}, {"/src/fail.vue"});
		std::vector<Diagnostic*> programDiagnostics =
			collectContentMapperDiagnostics(program);
		bool found = false;
		for (auto* diagnostic : programDiagnostics) {
			for (auto* message : diagnostic->MessageChain()) {
				if (message->Code() ==
				    The_content_mapper_returned_a_project_response_that_could_not_be_decoded
				        ->code) {
					found = true;
				}
			}
		}
		gotest::assert::Assert(
			t, found, "expected a localized project response diagnostic");
	});
}

static void TestContentMapperProjectErrorDiagnostics(T* t) {
	t->Parallel();
	struct testCase {
		contentmapper::ProjectErrorKind kind;
		int32_t code;
		std::string message;
	};
	for (const auto& test : std::vector<testCase>{
	         {
	             contentmapper::ProjectErrorKindMissingConfigIdentity,
	             The_content_mapper_did_not_return_configIdentity_which_is_required_when_the_content_mapper_has_dynamicConfig_Colon_true_in_its_package_json
	                 ->code,
	             R"(The content mapper did not return 'configIdentity', which is required when the content mapper has '"dynamicConfig": true' in its package.json.)",
	         },
	         {
	             contentmapper::ProjectErrorKindUnexpectedConfigIdentity,
	             The_content_mapper_returned_configIdentity_which_is_only_allowed_when_it_declares_dynamicConfig_Colon_true_in_its_package_json
	                 ->code,
	             R"(The content mapper returned 'configIdentity', which is only allowed when it declares '"dynamicConfig": true' in its package.json.)",
	         },
	         {
	             contentmapper::ProjectErrorKindUnexpectedWatchedFiles,
	             The_content_mapper_returned_watchedFiles_which_is_only_allowed_when_it_declares_dynamicConfig_Colon_true_in_its_package_json
	                 ->code,
	             R"(The content mapper returned 'watchedFiles', which is only allowed when it declares '"dynamicConfig": true' in its package.json.)",
	         },
	     }) {
		t->Run(test.message, [&test](T* t) {
			t->Parallel();
			auto* projectError = new contentmapper::ProjectError();
			projectError->Kind = test.kind;
			const DiagnosticMessage* message =
				compiler::ContentMapperProjectErrorDiagnostic(
					gostd::Error(projectError));
			gotest::assert::Equal(t, message->code, test.code);
			gotest::assert::Equal(
				t, localize(locale::Default, message,
				                         message->key, {}),
				test.message);
		});
	}
}

REGISTER_UNIT_TEST(
	"compiler.TestContentMapperVirtualExtensionSetsImpliedNodeFormat",
	TestContentMapperVirtualExtensionSetsImpliedNodeFormat);
REGISTER_UNIT_TEST(
	"compiler.TestContentMapperDirectivesPreserveIncrementalGlobals",
	TestContentMapperDirectivesPreserveIncrementalGlobals);
REGISTER_UNIT_TEST("compiler.TestCompositeProjectContentMapperSupplementalRoots",
				   TestCompositeProjectContentMapperSupplementalRoots);
REGISTER_UNIT_TEST("compiler.TestContentMapperInvalidMappings",
				   TestContentMapperInvalidMappings);
REGISTER_UNIT_TEST("compiler.TestContentMapperSourceFileState",
				   TestContentMapperSourceFileState);
REGISTER_UNIT_TEST("compiler.TestContentMapperProjectErrorDiagnostics",
				   TestContentMapperProjectErrorDiagnostics);
