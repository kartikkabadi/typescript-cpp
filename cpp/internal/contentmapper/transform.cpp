// transform.go — port of tsc/internal/contentmapper/transform.go

#include "internal/contentmapper/contentmapper.h"

#include "internal/core/types.h"
#include "internal/parser/parser.h"
#include "internal/tspath/tspath.h"

namespace tsc::contentmapper {

// TransformAndParse — transform.go:26.
std::pair<SourceFiles, gostd::Error> TransformAndParse(
    const SourceFileParseOptions& parseOptions, const std::string& content,
    Mapper* mapper, const std::shared_ptr<Project>& project) {
	auto [transformIdentity, err] = project->Identity(mapper);
	if (err != nullptr) {
		return {SourceFiles{},
		        gostd::Error(NewTransformError(TransformErrorKindProject,
		                                       err))};
	}
	auto [result, terr] = project->Transform(
	    mapper,
	    Request{/*FileName*/ parseOptions.FileName, /*Content*/ content});
	if (terr != nullptr) {
		return {SourceFiles{}, terr};
	}
	return ParseResult(parseOptions, content, mapper, transformIdentity,
	                   std::move(result));
}

// ParseResult — transform.go:47.
std::pair<SourceFiles, gostd::Error> ParseResult(
    const SourceFileParseOptions& parseOptions, const std::string& content,
    Mapper* mapper, const std::string& transformIdentity, Result result) {
	if (result.Mappings == nullptr) {
		return {SourceFiles{},
		        gostd::Error(NewTransformError(TransformErrorKindMappings,
		                                       nullptr))};
	}
	if (auto problem =
	        spanmap::Validate(result.Mappings, result.Text, content);
	    problem.has_value()) {
		return {SourceFiles{}, gostd::Error(std::make_shared<spanmap::MappingError>(*problem))};
	}
	const std::string& virtualExtension = result.VirtualExtension;
	if (!IsSupportedVirtualExtension(virtualExtension)) {
		return {SourceFiles{},
		        gostd::Error(NewTransformError(TransformErrorKindResponse,
		                                       nullptr))};
	}
	SourceFileParseOptions baseParseOptions = parseOptions;
	std::string virtualFileName = baseParseOptions.FileName + virtualExtension;
	SourceFileParseOptions canonicalOptions = baseParseOptions;
	if (isModuleVirtualExtension(virtualExtension)) {
		canonicalOptions.ExternalModuleIndicatorOptions.Force = true;
	}
	SourceFile* sourceFile =
	    parseSourceFile(canonicalOptions, result.Text,
	                            getScriptKindFromFileName(virtualFileName));
	if (!result.Diagnostics.empty()) {
		// The runner produces diagnostics without a source file (it doesn't
		// have one yet); associate them with the file now so they are reported
		// against it.
		for (auto* diagnostic : result.Diagnostics) {
			diagnostic->SetFile(sourceFile);
		}
		sourceFile->diagnostics.insert(sourceFile->diagnostics.end(),
		                               result.Diagnostics.begin(),
		                               result.Diagnostics.end());
	}
	SourceFiles files{/*Canonical*/ sourceFile, /*Supplemental*/ {}};
	files.Supplemental.reserve(result.Supplemental.size());
	for (size_t i = 0; i < result.Supplemental.size(); i++) {
		const auto& supplemental = result.Supplemental[i];
		if (supplemental.Mappings == nullptr) {
			return {SourceFiles{},
			        gostd::Error(NewTransformError(TransformErrorKindMappings,
			                                       nullptr))};
		}
		if (auto problem = spanmap::Validate(supplemental.Mappings,
		                                     supplemental.Text, content);
		    problem.has_value()) {
			return {SourceFiles{}, gostd::Error(std::make_shared<spanmap::MappingError>(*problem))};
		}
		SourceFileParseOptions supplementalOptions = baseParseOptions;
		if (!IsSupportedVirtualExtension(supplemental.VirtualExtension)) {
			return {SourceFiles{},
			        gostd::Error(NewTransformError(TransformErrorKindResponse,
			                                       nullptr))};
		}
		std::string suffix =
		    "." + std::to_string(i) + std::string(supplemental.VirtualExtension);
		supplementalOptions.FileName += suffix;
		supplementalOptions.Path =
		    tspath::Path(baseParseOptions.Path) + suffix;
		if (isModuleVirtualExtension(supplemental.VirtualExtension)) {
			supplementalOptions.ExternalModuleIndicatorOptions.Force = true;
		}

		SourceFile* file = parseSourceFile(
		    supplementalOptions, supplemental.Text,
		    getScriptKindFromFileName(supplementalOptions.FileName));

		files.Supplemental.push_back(file);
	}
	std::string mapperIdentity = mapper->Identity();
	sourceFile->SetContentMapperInfo(ContentMapperSourceFileInfo{
	    /*ContentMapper*/ mapperIdentity,
	    /*TransformIdentity*/ transformIdentity,
	    /*ParseOptions*/ baseParseOptions,
	    /*VirtualFileName*/ virtualFileName,
	    /*OriginalText*/ content,
	    /*SpanMap*/ result.Mappings,
	    /*DiagnosticDirectives*/ result.DiagnosticDirectives,
	    /*SupplementalSourceFiles*/ files.Supplemental,
	    /*CanonicalSourceFile*/ nullptr,
	});
	for (size_t i = 0; i < files.Supplemental.size(); i++) {
		SourceFile* file = files.Supplemental[i];
		const auto& supplemental = result.Supplemental[i];
		file->SetContentMapperInfo(ContentMapperSourceFileInfo{
		    /*ContentMapper*/ mapperIdentity,
		    /*TransformIdentity*/ transformIdentity,
		    /*ParseOptions*/ baseParseOptions,
		    /*VirtualFileName*/ file->FileName(),
		    /*OriginalText*/ content,
		    /*SpanMap*/ supplemental.Mappings,
		    /*DiagnosticDirectives*/ supplemental.DiagnosticDirectives,
		    /*SupplementalSourceFiles*/ {},
		    /*CanonicalSourceFile*/ sourceFile,
		});
	}
	return {files, nullptr};
}

// isModuleVirtualExtension — transform.go:123.
bool isModuleVirtualExtension(std::string_view extension) {
	return extension == tspath::extensionMts ||
	       extension == tspath::extensionCts ||
	       extension == tspath::extensionMjs ||
	       extension == tspath::extensionCjs;
}

// CheckSupplementalFileNameCollisions — transform.go:128.
gostd::Error CheckSupplementalFileNameCollisions(
    const SourceFiles& files,
    const std::function<bool(std::string_view)>& fileExists) {
	for (auto* file : files.Supplemental) {
		if (fileExists(file->FileName())) {
			auto* e = new SupplementalFileCollisionError;
			e->FileName = file->FileName();
			return gostd::Error(e);
		}
	}
	return nullptr;
}

} // namespace tsc::contentmapper
