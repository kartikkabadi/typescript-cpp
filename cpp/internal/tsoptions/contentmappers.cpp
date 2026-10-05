// Port of tsc/internal/tsoptions/contentmappers.go.
#include "internal/tsoptions/tsoptions.h"

#include "internal/diagnostics/messages_generated.h"
#include "internal/module/resolver.h"
#include "internal/packagejson/packagejson.h"
#include "internal/tspath/tspath.h"

namespace tsc::tsoptions {

// resolveContentMapperManifest locates packageName in node_modules (walking
// up from the directory of containingFile via node module resolution) and
// reads its package.json to produce the mapper's manifest and package
// directory. It never executes the package. On failure it returns a
// diagnostic describing why the mapper could not be resolved; on success the
// diagnostic is nil. — contentmappers.go:17.
std::tuple<contentmapper::Manifest, std::string, Diagnostic*>
resolveContentMapperManifest(ParseConfigHost* host,
                             std::string_view containingFile,
                             std::string_view packageName) {
	auto compilerOptions = new CompilerOptions{
	    .ModuleResolution = ModuleResolutionKind::Bundler,
	};
	module::DefaultResolver* resolver = module::NewResolver(module::ResolverOptions{
	    .Host = host,
	    .CompilerOptions = compilerOptions,
	});
	auto resolved = resolver->ResolvePackageDirectory(
	    packageName, containingFile, ResolutionModeNone, nullptr);
	if (resolved == nullptr || resolved->ResolvedFileName.empty()) {
		return {contentmapper::Manifest{}, "",
		        newCompilerDiagnostic(
		            The_content_mapper_package_0_could_not_be_resolved,
		            {std::string(packageName)})};
	}
	std::string packageDirectory = resolved->ResolvedFileName;

	std::string packageJsonPath =
	    tspath::combinePaths(packageDirectory, {"package.json"});
	auto contents = host->FS()->ReadFile(packageJsonPath);
	if (!contents.has_value()) {
		return {contentmapper::Manifest{}, packageDirectory,
		        newCompilerDiagnostic(
		            The_content_mapper_package_0_could_not_be_resolved,
		            {std::string(packageName)})};
	}
	auto [fields, err] = packagejson::Parse(*contents);
	if (!err) {
		return {contentmapper::Manifest{}, packageDirectory,
		        newCompilerDiagnostic(
		            The_package_json_of_the_content_mapper_package_0_could_not_be_parsed,
		            {std::string(packageName)})};
	}
	auto [name, nameOk] = fields.Name.GetValue();
	if (name.empty()) {
		return {contentmapper::Manifest{}, packageDirectory,
		        newCompilerDiagnostic(
		            The_package_json_of_the_content_mapper_package_0_does_not_specify_a_name,
		            {std::string(packageName)})};
	}
	auto [version, versionOk] = fields.Version.GetValue();

	// A content mapper package must declare how to run it: a
	// "typescript.contentMapper" object with a non-empty "exec" array of
	// strings.
	auto [cm, ok] = fields.ContentMapper.GetValue();
	if (!ok) {
		return {contentmapper::Manifest{}, packageDirectory,
		        newCompilerDiagnostic(
		            The_package_json_of_the_content_mapper_package_0_does_not_declare_a_typescript_contentMapper_object,
		            {std::string(packageName)})};
	}
	auto [exec, execOk] = cm.Exec.GetValue();
	if (!execOk || exec.empty()) {
		return {contentmapper::Manifest{}, packageDirectory,
		        newCompilerDiagnostic(
		            The_typescript_contentMapper_exec_of_the_content_mapper_package_0_must_be_a_non_empty_array_of_strings,
		            {std::string(packageName)})};
	}
	auto [cmCompilerOptions, cmOk2] = cm.CompilerOptions.GetValue();
	auto [dynamicConfig, dcOk] = cm.DynamicConfig.GetValue();
	return {contentmapper::Manifest{
	            .Name = name,
	            .Version = version,
	            .Exec = exec,
	            .CompilerOptions = cmCompilerOptions,
	            .DynamicConfig = dynamicConfig,
	        },
	        packageDirectory, nullptr};
}

}  // namespace tsc::tsoptions
