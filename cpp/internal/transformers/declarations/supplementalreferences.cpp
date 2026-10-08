// Port of tsc/internal/transformers/declarations/supplementalreferences.go
#include "internal/transformers/declarations/declarations.h"

#include "internal/tspath/tspath.h"

namespace tsc::transformers::declarations {

namespace {

// ast.go:2654 SourceFile::SupplementalSourceFiles.
std::vector<SourceFile*> supplementalSourceFiles(SourceFile* sourceFile) {
	if (sourceFile->contentMapperInfo == nullptr) {
		return {};
	}
	return sourceFile->contentMapperInfo->SupplementalSourceFiles;
}

}  // namespace

// SupplementalReferencesTransformer adds triple-slash path references from a content mapper's
// canonical declaration output to the declaration files emitted for its supplemental files.
// This ensures that consumers loading the canonical declaration also include the supplemental types.
struct SupplementalReferencesTransformer : DeclarationTransformer {
	DeclarationEmitHost* host = nullptr;
	std::vector<SourceFile*> supplementalFiles;
	std::string declarationFilePath;
	bool forceDeclarationPaths = false;

	SourceFile* TransformSourceFile(SourceFile* sourceFile) override {
		for (SourceFile* supplemental : supplementalFiles) {
			if (!host->SourceFileMayBeEmitted(supplemental,
			                                forceDeclarationPaths)) {
				continue;
			}
			std::string declarationPath =
			    host->GetOutputPathsFor(supplemental, forceDeclarationPaths)
			        ->DeclarationFilePath();
			if (declarationPath.empty()) {
				continue;
			}
			auto* ref = new FileReference();
			ref->pos_ = -1;
			ref->end_ = -1;
			ref->FileName = tspath::getRelativePathFromFile(
				declarationFilePath, declarationPath,
				tspath::ComparePathsOptions{
					host->UseCaseSensitiveFileNames(),
					host->GetCurrentDirectory()});
			sourceFile->ReferencedFiles.push_back(ref);
		}
		return sourceFile;
	}

	std::vector<Diagnostic*> GetDiagnostics() override { return {}; }
};

// supplementalreferences.go:19 NewSupplementalReferencesTransformer
DeclarationTransformer* NewSupplementalReferencesTransformer(
    DeclarationEmitHost* host, SourceFile* sourceFile,
    std::string_view declarationFilePath, bool forceDeclarationPaths) {
	auto* tx = new SupplementalReferencesTransformer();
	tx->host = host;
	tx->supplementalFiles = supplementalSourceFiles(sourceFile);
	tx->declarationFilePath = std::string(declarationFilePath);
	tx->forceDeclarationPaths = forceDeclarationPaths;
	return tx;
}

}  // namespace tsc::transformers::declarations
