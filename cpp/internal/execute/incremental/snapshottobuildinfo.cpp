// Port of tsc/internal/execute/incremental/snapshottobuildinfo.go —
// toBuildInfo: converts a snapshot into the serializable BuildInfo.
#include <algorithm>
#include <utility>

#include "internal/checker/checker.h"
#include "internal/core/version.h"
#include "internal/execute/incremental/incremental.h"

namespace tsc::execute::incremental {
namespace {

struct toBuildInfo {
	incremental::snapshot* snapshot;
	compiler::SimpleProgram* program;
	BuildInfo* buildInfo;
	std::string buildInfoDirectory;
	tspath::ComparePathsOptions comparePathsOptions;
	std::unordered_map<std::string, BuildInfoFileId> fileNameToFileId;
	std::unordered_map<std::string, BuildInfoFileIdListId>
	    fileNamesToFileIdListId;
	std::unordered_map<SourceFile*, tspath::Path> roots;

	std::string relativeToBuildInfo(const std::string& path) {
		return tspath::ensurePathIsNonModuleName(
		    tspath::getRelativePathFromDirectory(
		        buildInfoDirectory, path, comparePathsOptions));
	}

	BuildInfoFileId toFileId(const tspath::Path& path) {
		auto it = fileNameToFileId.find(std::string(path));
		BuildInfoFileId fileId =
		    it != fileNameToFileId.end() ? it->second : 0;
		if (fileId == 0) {
			if (auto* libFile =
			        program->GetDefaultLibFile(path);
			    libFile != nullptr && !libFile->replaced) {
				buildInfo->FileNames.push_back(libFile->name);
			} else {
				buildInfo->FileNames.push_back(
				    relativeToBuildInfo(std::string(path)));
			}
			fileId = static_cast<BuildInfoFileId>(
			    buildInfo->FileNames.size());
			fileNameToFileId[std::string(path)] = fileId;
		}
		return fileId;
	}

	BuildInfoFileIdListId toFileIdListId(
	    collections::Set<tspath::Path>* set) {
		std::vector<BuildInfoFileId> fileIds;
		for (const auto& key : set->Keys()) {
			fileIds.push_back(toFileId(key));
		}
		std::sort(fileIds.begin(), fileIds.end());
		std::string key;
		for (auto id : fileIds) {
			if (!key.empty()) key += ',';
			key += std::to_string(id);
		}

		auto it = fileNamesToFileIdListId.find(key);
		BuildInfoFileIdListId fileIdListId =
		    it != fileNamesToFileIdListId.end() ? it->second : 0;
		if (fileIdListId == 0) {
			buildInfo->FileIdsList.push_back(fileIds);
			fileIdListId = static_cast<BuildInfoFileIdListId>(
			    buildInfo->FileIdsList.size());
			fileNamesToFileIdListId[key] = fileIdListId;
		}
		return fileIdListId;
	}

	// snapshottobuildinfo.go:128 toRelativeToBuildInfoCompilerOptionValue.
	tsoptions::CompilerOptionsValue toRelativeToBuildInfoCompilerOptionValue(
	    const tsoptions::CommandLineOption* option,
	    const tsoptions::CompilerOptionsValue& v) {
		if (option->Kind == tsoptions::CommandLineOptionTypeList) {
			if (option->Elements()->IsFilePath) {
				if (auto* arr =
				        v.get<tsoptions::JsonStrList>()) {
					tsoptions::JsonStrList mapped;
					mapped.reserve(arr->size());
					for (const auto& s : *arr) {
						mapped.push_back(
						    relativeToBuildInfo(s));
					}
					return tsoptions::CompilerOptionsValue(
					    std::move(mapped));
				}
			}
		} else if (option->IsFilePath) {
			if (auto* str = v.get<std::string>()) {
				if (!str->empty()) {
					return tsoptions::CompilerOptionsValue(
					    relativeToBuildInfo(*str));
				}
			}
		}
		return v;
	}

	std::vector<BuildInfoDiagnostic*>
	toBuildInfoDiagnosticsFromFileNameDiagnostics(
	    const std::vector<buildInfoDiagnosticWithFileName*>& diags) {
		std::vector<BuildInfoDiagnostic*> result;
		result.reserve(diags.size());
		for (auto* d : diags) {
			BuildInfoFileId file = 0;
			if (!d->file.empty()) {
				file = toFileId(d->file);
			}
			auto* bd = new BuildInfoDiagnostic();
			bd->File = file;
			bd->NoFile = d->noFile;
			bd->Pos = d->pos;
			bd->End = d->end;
			bd->Code = d->code;
			bd->Category = d->category;
			bd->Source = d->source;
			bd->MessageText = d->messageText;
			bd->MessageKey = d->messageKey;
			bd->MessageArgs = d->messageArgs;
			bd->MessageChain =
			    toBuildInfoDiagnosticsFromFileNameDiagnostics(
			        d->messageChain);
			bd->RelatedInformation =
			    toBuildInfoDiagnosticsFromFileNameDiagnostics(
			        d->relatedInformation);
			bd->ReportsUnnecessary = d->reportsUnnecessary;
			bd->ReportsDeprecated = d->reportsDeprecated;
			bd->SkippedOnNoEmit = d->skippedOnNoEmit;
			bd->RepopulateInfo =
			    toBuildInfoRepopulateInfo(d->repopulateInfo);
			result.push_back(bd);
		}
		return result;
	}

	std::vector<BuildInfoDiagnostic*>
	toBuildInfoDiagnosticsFromDiagnostics(
	    const tspath::Path& filePath,
	    const std::vector<Diagnostic*>& diags) {
		std::vector<BuildInfoDiagnostic*> result;
		result.reserve(diags.size());
		for (auto* d : diags) {
			BuildInfoFileId file = 0;
			bool noFile = false;
			if (d->File() == nullptr) {
				noFile = true;
			} else if (d->File()->Path() != filePath) {
				file = toFileId(d->File()->Path());
			}
			auto* bd = new BuildInfoDiagnostic();
			bd->File = file;
			bd->NoFile = noFile;
			bd->Pos = d->Loc().pos();
			bd->End = d->Loc().end();
			bd->Code = d->Code();
			bd->Category = d->Category();
			bd->Source = d->Source();
			bd->MessageText = d->MessageText();
			bd->MessageKey = d->MessageKey();
			bd->MessageArgs = d->MessageArgs();
			bd->MessageChain = toBuildInfoDiagnosticsFromDiagnostics(
			    filePath, d->MessageChain());
			bd->RelatedInformation =
			    toBuildInfoDiagnosticsFromDiagnostics(
			        filePath, d->RelatedInformation());
			bd->ReportsUnnecessary = d->ReportsUnnecessary();
			bd->ReportsDeprecated = d->ReportsDeprecated();
			bd->SkippedOnNoEmit = d->SkippedOnNoEmit();
			bd->RepopulateInfo =
			    toBuildInfoRepopulateInfo(d->RepopulateInfo());
			result.push_back(bd);
		}
		return result;
	}

	static BuildInfoRepopulateInfo* toBuildInfoRepopulateInfo(
	    RepopulateDiagnosticInfo* info) {
		if (info == nullptr) {
			return nullptr;
		}
		auto* b = new BuildInfoRepopulateInfo();
		b->Kind = info->kind;
		b->ModuleReference = info->moduleReference;
		b->Mode = info->mode;
		b->PackageName = info->packageName;
		return b;
	}

	BuildInfoDiagnosticsOfFile* toBuildInfoDiagnosticsOfFile(
	    const tspath::Path& filePath,
	    DiagnosticsOrBuildInfoDiagnosticsWithFileName* diags) {
		if (!diags->diagnostics.empty()) {
			auto* b = new BuildInfoDiagnosticsOfFile();
			b->FileId = toFileId(filePath);
			b->Diagnostics = toBuildInfoDiagnosticsFromDiagnostics(
			    filePath, diags->diagnostics);
			return b;
		}
		if (!diags->buildInfoDiagnostics.empty()) {
			auto* b = new BuildInfoDiagnosticsOfFile();
			b->FileId = toFileId(filePath);
			b->Diagnostics =
			    toBuildInfoDiagnosticsFromFileNameDiagnostics(
			        diags->buildInfoDiagnostics);
			return b;
		}
		return nullptr;
	}

	void collectRootFiles() {
		for (const auto& fileName :
		     program->CommandLine()->FileNames()) {
			SourceFile* file = nullptr;
			auto redirect =
			    program->GetParseFileRedirect(fileName);
			if (!redirect.empty()) {
				file = program->GetSourceFile(redirect);
			} else {
				file = program->GetSourceFile(fileName);
			}
			if (file != nullptr) {
				roots[file] = tspath::toPath(
				    fileName,
				    comparePathsOptions.currentDirectory,
				    comparePathsOptions
				        .useCaseSensitiveFileNames);
			}
		}
	}

	void setFileInfoAndEmitSignatures() {
		for (auto* file : program->GetSourceFiles()) {
			auto [info, _] =
			    snapshot->fileInfos.Load(file->Path());
			auto fileId = toFileId(file->Path());
			if (buildInfo->FileNames[fileId - 1] !=
			    relativeToBuildInfo(std::string(file->Path()))) {
				auto* libFile =
				    program->GetDefaultLibFile(file->Path());
				if (libFile == nullptr || libFile->replaced ||
				    buildInfo->FileNames[fileId - 1] !=
				        libFile->name) {
					TSC_UNREACHABLE(
					    "File name does not match expected "
					    "relative path or libName");
				}
			}
			if (snapshot->options->Composite == Tristate::True) {
				if (!isJsonSourceFile(file) &&
				    program->SourceFileMayBeEmitted(file,
				                                  false)) {
					auto [emitSignature, loaded] =
					    snapshot->emitSignatures.Load(
					        file->Path());
					if (!loaded) {
						auto* b = new BuildInfoEmitSignature();
						b->FileId = fileId;
						buildInfo->EmitSignatures.push_back(b);
					} else if (emitSignature->signature !=
					           info->signature) {
						auto* incrementalEmitSignature =
						    new BuildInfoEmitSignature();
						incrementalEmitSignature->FileId =
						    fileId;
						if (!emitSignature->signature
						         .empty()) {
							incrementalEmitSignature
							    ->Signature =
							    emitSignature->signature;
						} else if (emitSignature
						               ->signatureWithDifferentOptions
						               .value()[0] ==
						           info->signature) {
							incrementalEmitSignature
							    ->DiffersOnlyInDtsMap = true;
						} else {
							incrementalEmitSignature
							    ->Signature =
							    emitSignature
							        ->signatureWithDifferentOptions
							        .value()[0];
							incrementalEmitSignature
							    ->DiffersInOptions = true;
						}
						buildInfo->EmitSignatures.push_back(
						    incrementalEmitSignature);
					}
				}
			}
			buildInfo->FileInfos.push_back(
			    newBuildInfoFileInfo(info));
		}
	}

	void setRootOfIncrementalProgram() {
		std::vector<SourceFile*> keys;
		keys.reserve(roots.size());
		for (const auto& kv : roots) {
			keys.push_back(kv.first);
		}
		std::sort(keys.begin(), keys.end(),
		          [&](SourceFile* a, SourceFile* b) {
			          return toFileId(a->Path()) <
			                 toFileId(b->Path());
		          });
		for (auto* file : keys) {
			auto root = toFileId(roots[file]);
			auto resolved = toFileId(file->Path());
			if (buildInfo->Root.empty()) {
				// First fileId as is
				auto* r = new BuildInfoRoot();
				r->Start = resolved;
				buildInfo->Root.push_back(r);
			} else {
				auto* last = buildInfo->Root.back();
				if (last->End == resolved - 1) {
					// If its [..., last = [start, end =
					// fileId - 1]], update last to [start,
					// fileId]
					last->End = resolved;
				} else if (last->End == 0 &&
				           last->Start == resolved - 1) {
					// If its [..., last = start =
					// fileId - 1 ], update last to
					// [start, fileId]
					last->End = resolved;
				} else {
					auto* r = new BuildInfoRoot();
					r->Start = resolved;
					buildInfo->Root.push_back(r);
				}
			}
			if (root != resolved) {
				auto* r = new BuildInfoResolvedRoot();
				r->Resolved = resolved;
				r->Root = root;
				buildInfo->ResolvedRoot.push_back(r);
			}
		}
	}

	void setCompilerOptions() {
		tsoptions::ForEachCompilerOptionValue(
		    snapshot->options,
		    [](const tsoptions::CommandLineOption* option) {
			    return option->AffectsBuildInfo;
		    },
		    [&](const tsoptions::CommandLineOption* option,
		        const tsoptions::CompilerOptionsValue& value,
		        int i) {
			    if (tsoptions::compilerOptionFieldInfos()[i]
			            .isZero(snapshot->options)) {
				    return false;
			    }
			    // Make it relative to buildInfo directory if file
			    // path
			    if (!buildInfo->Options) {
				    buildInfo->Options =
				        std::make_shared<tsoptions::JsonObject>();
			    }
			    buildInfo->Options->Set(
			        option->Name,
			        toRelativeToBuildInfoCompilerOptionValue(
			            option, value));
			    return false;
		    });
	}

	void setReferencedMap() {
		auto keys = snapshot->referencedMap.getPathsWithReferences();
		std::sort(keys.begin(), keys.end());
		for (const auto& filePath : keys) {
			auto [references, _] =
			    snapshot->referencedMap.getReferences(filePath);
			auto* e = new BuildInfoReferenceMapEntry();
			e->FileId = toFileId(filePath);
			e->FileIdListId = toFileIdListId(references);
			buildInfo->ReferencedMap.push_back(e);
		}
	}

	void setChangeFileSet() {
		auto files = snapshot->changedFilesSet.ToSlice();
		std::sort(files.begin(), files.end());
		for (const auto& file : files) {
			buildInfo->ChangeFileSet.push_back(toFileId(file));
		}
	}

	void setSemanticDiagnostics() {
		for (auto* file : program->GetSourceFiles()) {
			auto [value, ok] =
			    snapshot->semanticDiagnosticsPerFile.Load(
			        file->Path());
			if (!ok) {
				if (!snapshot->changedFilesSet.Has(
				        file->Path())) {
					auto* d =
					    new BuildInfoSemanticDiagnostic();
					d->FileId = toFileId(file->Path());
					buildInfo->SemanticDiagnosticsPerFile
					    .push_back(d);
				}
			} else {
				auto* diagnostics =
				    toBuildInfoDiagnosticsOfFile(file->Path(),
				                                 value);
				if (diagnostics != nullptr) {
					auto* d =
					    new BuildInfoSemanticDiagnostic();
					d->Diagnostics = diagnostics;
					buildInfo->SemanticDiagnosticsPerFile
					    .push_back(d);
				}
			}
		}
	}

	void setEmitDiagnostics() {
		std::vector<tspath::Path> files;
		snapshot->emitDiagnosticsPerFile.Range(
		    [&](const tspath::Path& p,
		        DiagnosticsOrBuildInfoDiagnosticsWithFileName*) {
			    files.push_back(p);
			    return true;
		    });
		std::sort(files.begin(), files.end());
		for (const auto& filePath : files) {
			auto [value, _] =
			    snapshot->emitDiagnosticsPerFile.Load(filePath);
			buildInfo->EmitDiagnosticsPerFile.push_back(
			    toBuildInfoDiagnosticsOfFile(filePath, value));
		}
	}

	void setAffectedFilesPendingEmit() {
		std::vector<tspath::Path> files;
		snapshot->affectedFilesPendingEmit.Range(
		    [&](const tspath::Path& p, FileEmitKind) {
			    files.push_back(p);
			    return true;
		    });
		std::sort(files.begin(), files.end());
		auto fullEmitKind = GetFileEmitKind(snapshot->options);
		for (const auto& filePath : files) {
			auto* file =
			    program->GetSourceFileByPath(filePath);
			if (file == nullptr ||
			    !program->SourceFileMayBeEmitted(file, false)) {
				continue;
			}
			auto [pendingEmit, _] =
			    snapshot->affectedFilesPendingEmit.Load(
			        filePath);
			auto* b = new BuildInfoFilePendingEmit();
			b->FileId = toFileId(filePath);
			b->EmitKind = pendingEmit == fullEmitKind
			                  ? FileEmitKindNone
			                  : pendingEmit;
			buildInfo->AffectedFilesPendingEmit.push_back(b);
		}
	}

	void setRootOfNonIncrementalProgram() {
		for (const auto& fileName :
		     program->CommandLine()->FileNames()) {
			auto* r = new BuildInfoRoot();
			r->NonIncremental = relativeToBuildInfo(std::string(
			    tspath::toPath(fileName,
			                   comparePathsOptions.currentDirectory,
			                   comparePathsOptions
			                       .useCaseSensitiveFileNames)));
			buildInfo->Root.push_back(r);
		}
	}

	void setPackageJsons() {
		if (snapshot->packageJsons.has_value() &&
		    !snapshot->packageJsons->empty()) {
			for (const auto& p : *snapshot->packageJsons) {
				buildInfo->PackageJsons.push_back(
				    relativeToBuildInfo(p));
			}
		}
		if (snapshot->missingPackageJsons.has_value() &&
		    !snapshot->missingPackageJsons->empty()) {
			for (const auto& p : *snapshot->missingPackageJsons) {
				buildInfo->MissingPackageJsons.push_back(
				    relativeToBuildInfo(p));
			}
		}
	}
};

}  // namespace

// snapshottobuildinfo.go:16 snapshotToBuildInfo.
std::pair<BuildInfo*, std::optional<std::string>> snapshotToBuildInfo(
    incremental::snapshot* snapshot, compiler::SimpleProgram* program,
    std::string_view buildInfoFileName) {
	auto [contentMapperIdentities, err] =
	    ContentMapperIdentities(program->ContentMapperProject());
	if (err.has_value()) {
		return {nullptr, err};
	}
	auto* buildInfo = new BuildInfo();
	buildInfo->Version = std::string(version());
	buildInfo->ContentMapperIdentities = contentMapperIdentities;
	toBuildInfo to;
	to.snapshot = snapshot;
	to.program = program;
	to.buildInfo = buildInfo;
	to.buildInfoDirectory =
	    tspath::getDirectoryPath(buildInfoFileName);
	to.comparePathsOptions =
	    tspath::ComparePathsOptions{
	        program->UseCaseSensitiveFileNames(),
	        program->GetCurrentDirectory()};

	if (snapshot->options->IsIncremental()) {
		to.collectRootFiles();
		to.setFileInfoAndEmitSignatures();
		to.setRootOfIncrementalProgram();
		to.setCompilerOptions();
		to.setReferencedMap();
		to.setChangeFileSet();
		to.setSemanticDiagnostics();
		to.setEmitDiagnostics();
		to.setAffectedFilesPendingEmit();
		if (!snapshot->latestChangedDtsFile.empty()) {
			buildInfo->LatestChangedDtsFile =
			    to.relativeToBuildInfo(snapshot->latestChangedDtsFile);
		}
	} else {
		to.setRootOfNonIncrementalProgram();
	}
	buildInfo->Errors = snapshot->hasErrors == Tristate::True;
	buildInfo->SemanticErrors = snapshot->hasSemanticErrors;
	buildInfo->CheckPending = snapshot->checkPending;
	to.setPackageJsons();
	return {buildInfo, std::nullopt};
}

}  // namespace tsc::execute::incremental
