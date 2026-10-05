// Port of tsc/internal/execute/incremental/buildinfotosnapshot.go —
// toSnapshot: reconstructs the in-memory snapshot from a parsed BuildInfo.
#include <utility>

#include "internal/checker/checker.h"
#include "internal/execute/incremental/incremental.h"

namespace tsc::execute::incremental {
namespace {

struct toSnapshot {
	BuildInfo* buildInfo;
	std::string buildInfoDirectory;
	// Go embeds a snapshot VALUE in toSnapshot and returns &to.snapshot;
	// C++ snapshot is non-movable, so heap-allocate it.
	incremental::snapshot* snapshot = new incremental::snapshot();
	std::vector<tspath::Path> filePaths;
	std::vector<collections::Set<tspath::Path>*> filePathSet;

	std::string toAbsolutePath(const std::string& path) {
		return tspath::getNormalizedAbsolutePath(path,
		                                         buildInfoDirectory);
	}

	tspath::Path toFilePath(BuildInfoFileId fileId) {
		return filePaths[fileId - 1];
	}

	collections::Set<tspath::Path>* toFilePathSet(
	    BuildInfoFileIdListId fileIdListId) {
		return filePathSet[fileIdListId - 1];
	}

	std::vector<buildInfoDiagnosticWithFileName*>
	toBuildInfoDiagnosticsWithFileName(
	    const std::vector<BuildInfoDiagnostic*>& diags) {
		std::vector<buildInfoDiagnosticWithFileName*> result;
		result.reserve(diags.size());
		for (auto* d : diags) {
			tspath::Path file;
			if (d->File != 0) {
				file = toFilePath(d->File);
			}
			auto* bd = new buildInfoDiagnosticWithFileName();
			bd->file = file;
			bd->noFile = d->NoFile;
			bd->pos = d->Pos;
			bd->end = d->End;
			bd->code = d->Code;
			bd->category = d->Category;
			bd->source = d->Source;
			bd->messageText = d->MessageText;
			bd->messageKey = d->MessageKey;
			bd->messageArgs = d->MessageArgs;
			bd->messageChain =
			    toBuildInfoDiagnosticsWithFileName(d->MessageChain);
			bd->relatedInformation =
			    toBuildInfoDiagnosticsWithFileName(
			        d->RelatedInformation);
			bd->reportsUnnecessary = d->ReportsUnnecessary;
			bd->reportsDeprecated = d->ReportsDeprecated;
			bd->skippedOnNoEmit = d->SkippedOnNoEmit;
			bd->repopulateInfo =
			    fromBuildInfoRepopulateInfo(d->RepopulateInfo);
			result.push_back(bd);
		}
		return result;
	}

	DiagnosticsOrBuildInfoDiagnosticsWithFileName*
	toDiagnosticsOrBuildInfoDiagnosticsWithFileName(
	    BuildInfoDiagnosticsOfFile* dig) {
		auto* d = new DiagnosticsOrBuildInfoDiagnosticsWithFileName();
		d->buildInfoDiagnostics =
		    toBuildInfoDiagnosticsWithFileName(dig->Diagnostics);
		return d;
	}

	static RepopulateDiagnosticInfo* fromBuildInfoRepopulateInfo(
	    BuildInfoRepopulateInfo* info) {
		if (info == nullptr) {
			return nullptr;
		}
		auto* r = new RepopulateDiagnosticInfo();
		r->kind = info->Kind;
		r->moduleReference = info->ModuleReference;
		r->mode = info->Mode;
		r->packageName = info->PackageName;
		return r;
	}

	void setCompilerOptions() {
		snapshot->options =
		    buildInfo->GetCompilerOptions(buildInfoDirectory);
	}

	void setFileInfoAndEmitSignatures() {
		bool isComposite =
		    snapshot->options->Composite == Tristate::True;
		for (size_t index = 0; index < buildInfo->FileInfos.size();
		     index++) {
			auto* buildInfoFileInfo = buildInfo->FileInfos[index];
			auto path =
			    toFilePath(static_cast<BuildInfoFileId>(index + 1));
			auto* info = buildInfoFileInfo->GetFileInfo();
			snapshot->fileInfos.Store(path, info);
			// Add default emit signature as file's signature
			if (!info->signature.empty() && isComposite) {
				auto* e = new emitSignature();
				e->signature = info->signature;
				snapshot->emitSignatures.Store(path, e);
			}
		}
		// Fix up emit signatures
		for (auto* value : buildInfo->EmitSignatures) {
			if (value->noEmitSignature()) {
				snapshot->emitSignatures.Delete(
				    toFilePath(value->FileId));
			} else {
				auto path = toFilePath(value->FileId);
				snapshot->emitSignatures.Store(
				    path,
				    value->toEmitSignature(
				        path, &snapshot->emitSignatures));
			}
		}
	}

	void setReferencedMap() {
		for (auto* entry : buildInfo->ReferencedMap) {
			snapshot->referencedMap.storeReferences(
			    toFilePath(entry->FileId),
			    toFilePathSet(entry->FileIdListId));
		}
	}

	void setChangeFileSet() {
		for (auto fileId : buildInfo->ChangeFileSet) {
			auto filePath = toFilePath(fileId);
			snapshot->changedFilesSet.Add(filePath);
		}
	}

	void setSemanticDiagnostics() {
		snapshot->fileInfos.Range(
		    [&](const tspath::Path& path, FileInfo*) {
			    // Initialize to have no diagnostics if its not
			    // changed file
			    if (!snapshot->changedFilesSet.Has(path)) {
				    snapshot->semanticDiagnosticsPerFile.Store(
				        path,
				        new DiagnosticsOrBuildInfoDiagnosticsWithFileName());
			    }
			    return true;
		    });
		for (auto* diagnostic :
		     buildInfo->SemanticDiagnosticsPerFile) {
			if (diagnostic->FileId != 0) {
				auto filePath = toFilePath(diagnostic->FileId);
				snapshot->semanticDiagnosticsPerFile.Delete(
				    filePath); // does not have cached diagnostics
			} else {
				auto filePath =
				    toFilePath(diagnostic->Diagnostics->FileId);
				snapshot->semanticDiagnosticsPerFile.Store(
				    filePath,
				    toDiagnosticsOrBuildInfoDiagnosticsWithFileName(
				        diagnostic->Diagnostics));
			}
		}
	}

	void setEmitDiagnostics() {
		for (auto* diagnostic : buildInfo->EmitDiagnosticsPerFile) {
			auto filePath = toFilePath(diagnostic->FileId);
			snapshot->emitDiagnosticsPerFile.Store(
			    filePath,
			    toDiagnosticsOrBuildInfoDiagnosticsWithFileName(
			        diagnostic));
		}
	}

	void setAffectedFilesPendingEmit() {
		if (buildInfo->AffectedFilesPendingEmit.empty()) {
			return;
		}
		auto ownOptionsEmitKind =
		    GetFileEmitKind(snapshot->options);
		for (auto* pendingEmit :
		     buildInfo->AffectedFilesPendingEmit) {
			snapshot->affectedFilesPendingEmit.Store(
			    toFilePath(pendingEmit->FileId),
			    pendingEmit->EmitKind == 0
			        ? ownOptionsEmitKind
			        : pendingEmit->EmitKind);
		}
	}

	void setPackageJsons() {
		if (!buildInfo->PackageJsons.empty()) {
			snapshot->packageJsons = std::vector<std::string>{};
			for (const auto& p : buildInfo->PackageJsons) {
				snapshot->packageJsons->push_back(
				    toAbsolutePath(p));
			}
		} else {
			snapshot->packageJsons = std::vector<std::string>{};
		}
		if (!buildInfo->MissingPackageJsons.empty()) {
			snapshot->missingPackageJsons =
			    std::vector<std::string>{};
			for (const auto& p : buildInfo->MissingPackageJsons) {
				snapshot->missingPackageJsons->push_back(
				    toAbsolutePath(p));
			}
		} else {
			snapshot->missingPackageJsons =
			    std::vector<std::string>{};
		}
	}
};

}  // namespace

// buildinfotosnapshot.go:12 buildInfoToSnapshot — allocates the snapshot
// on the heap (the Go `snapshot` value lives inside `to`, then escapes).
snapshot* buildInfoToSnapshot(BuildInfo* buildInfo,
                              tsoptions::ParsedCommandLine* config,
                              compiler::CompilerHost* host) {
	auto* to = new toSnapshot();
	to->buildInfo = buildInfo;
	to->buildInfoDirectory = tspath::getDirectoryPath(
	    tspath::getNormalizedAbsolutePath(
	        config->GetBuildInfoFileName(),
	        config->GetCurrentDirectory()));
	to->filePaths.reserve(buildInfo->FileNames.size());
	to->filePathSet.reserve(buildInfo->FileIdsList.size());
	for (const auto& fileName : buildInfo->FileNames) {
		if (IsBuildInfoFileNameDefaultLibrary(fileName)) {
			to->filePaths.push_back(tspath::toPath(
			    tspath::combinePaths(host->DefaultLibraryPath(),
			                         {fileName}),
			    host->GetCurrentDirectory(),
			    host->UseCaseSensitiveFileNames()));
		} else {
			to->filePaths.push_back(tspath::toPath(
			    fileName, to->buildInfoDirectory,
			    config->UseCaseSensitiveFileNames()));
		}
	}
	for (const auto& fileIdList : buildInfo->FileIdsList) {
		auto* fileSet = new collections::Set<tspath::Path>();
		for (auto fileId : fileIdList) {
			fileSet->Add(to->toFilePath(fileId));
		}
		to->filePathSet.push_back(fileSet);
	}
	to->setCompilerOptions();
	to->setFileInfoAndEmitSignatures();
	to->setReferencedMap();
	to->setChangeFileSet();
	to->setSemanticDiagnostics();
	to->setEmitDiagnostics();
	to->setAffectedFilesPendingEmit();
	if (!buildInfo->LatestChangedDtsFile.empty()) {
		to->snapshot->latestChangedDtsFile =
		    to->toAbsolutePath(buildInfo->LatestChangedDtsFile);
	}
	to->snapshot->hasErrors =
	    buildInfo->Errors ? Tristate::True : Tristate::False;
	to->snapshot->hasSemanticErrors = buildInfo->SemanticErrors;
	to->snapshot->checkPending = buildInfo->CheckPending;
	to->setPackageJsons();
	return to->snapshot;
}

}  // namespace tsc::execute::incremental
