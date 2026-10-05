// --- fileInclude.go + includeprocessor.go + processingDiagnostic.go —
// program slice ---
#include "internal/compiler/program.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/scanner/scanner.h"

#include <algorithm>
#include <cmath>

namespace tsc::compiler {

// ===========================================================================
// core.go: levenshtein/spelling suggestion (needed by
// Cannot_find_lib_definition_for_0_Did_you_mean_1)
// ===========================================================================
namespace {

struct levenshteinBuffers {
	std::vector<double> previous;
	std::vector<double> current;
};

// core.go: levenshteinWithMax (runes — our identifiers are ASCII here, but we
// decode UTF-8 faithfully).
std::vector<uint32_t> toRunes(std::string_view s) {
	std::vector<uint32_t> out;
	for (size_t i = 0; i < s.size();) {
		uint32_t cp = static_cast<unsigned char>(s[i]);
		if (cp < 0x80) {
			i += 1;
		} else if ((cp >> 5) == 0x6) {
			cp = ((cp & 0x1f) << 6) | (s[i + 1] & 0x3f);
			i += 2;
		} else if ((cp >> 4) == 0xe) {
			cp = ((cp & 0xf) << 12) | ((s[i + 1] & 0x3f) << 6) |
			     (s[i + 2] & 0x3f);
			i += 3;
		} else {
			cp = ((cp & 0x7) << 18) | ((s[i + 1] & 0x3f) << 12) |
			     ((s[i + 2] & 0x3f) << 6) | (s[i + 3] & 0x3f);
			i += 4;
		}
		out.push_back(cp);
	}
	return out;
}

double levenshteinWithMax(levenshteinBuffers& buffers,
                          const std::vector<uint32_t>& s1,
                          const std::vector<uint32_t>& s2,
                          double maxValue) {
	size_t bufferSize = s2.size() + 1;
	buffers.previous.assign(bufferSize, 0);
	buffers.current.assign(bufferSize, 0);
	auto* previous = buffers.previous.data();
	auto* current = buffers.current.data();

	double big = maxValue + 0.01;
	for (size_t i = 0; i < bufferSize; i++)
		previous[i] = static_cast<double>(i);
	for (size_t i = 1; i <= s1.size(); i++) {
		uint32_t c1 = s1[i - 1];
		int minJ = std::max(
		    static_cast<int>(std::ceil(static_cast<double>(i) - maxValue)),
		    1);
		int maxJ =
		    std::min(static_cast<int>(std::floor(maxValue +
		                                         static_cast<double>(i))),
		             static_cast<int>(s2.size()));
		double colMin = static_cast<double>(i);
		current[0] = colMin;
		for (int j = 1; j < minJ; j++)
			current[j] = big;
		for (int j = minJ; j <= maxJ; j++) {
			double substitutionDistance, dist;
			if (std::tolower(static_cast<unsigned char>(s1[i - 1])) ==
			    std::tolower(static_cast<unsigned char>(s2[j - 1]))) {
				substitutionDistance = previous[j - 1] + 0.1;
			} else {
				substitutionDistance = previous[j - 1] + 2;
			}
			if (c1 == s2[j - 1]) {
				dist = previous[j - 1];
			} else {
				dist = std::min(previous[j] + 1,
				                std::min(current[j - 1] + 1,
				                         substitutionDistance));
			}
			current[j] = dist;
			colMin = std::min(colMin, dist);
		}
		for (size_t j = maxJ + 1; j <= s2.size(); j++)
			current[j] = big;
		if (colMin > maxValue) {
			return -1;
		}
		std::swap(previous, current);
	}
	double res = previous[s2.size()];
	if (res > maxValue)
		return -1;
	return res;
}

// core.go: GetSpellingSuggestionForStrings
std::string getSpellingSuggestionForStrings(
    std::string_view name, const std::vector<std::string_view>& candidates) {
	auto runeName = toRunes(name);
	int maximumLengthDifference =
	    std::max(2, static_cast<int>(static_cast<double>(runeName.size()) *
		                         0.34));
	double bestDistance =
	    std::floor(static_cast<double>(runeName.size()) * 0.4) + 0.9;
	levenshteinBuffers buffers;
	std::string_view bestCandidate;
	bool hasBest = false;
	for (auto candidateName : candidates) {
		auto candidateRunes = toRunes(candidateName);
		size_t maxLen = std::max(candidateRunes.size(), runeName.size());
		size_t minLen = std::min(candidateRunes.size(), runeName.size());
		if (candidateName.empty() ||
		    maxLen - minLen > static_cast<size_t>(maximumLengthDifference))
			continue;
		if (candidateName == name)
			continue;
		// Only consider candidates less than 3 characters long when they
		// differ by case.
		if (candidateRunes.size() < 3) {
			bool equalFold = candidateName.size() == name.size();
			for (size_t i = 0;
			     equalFold && i < candidateName.size(); i++)
				equalFold = std::tolower((unsigned char)candidateName[i]) ==
				            std::tolower((unsigned char)name[i]);
			if (!equalFold)
				continue;
		}
		double distance = levenshteinWithMax(buffers, runeName,
		                                     candidateRunes, bestDistance);
		if (distance < 0)
			continue;
		if (distance < bestDistance) {
			bestDistance = distance;
			bestCandidate = candidateName;
			hasBest = true;
		} else if (!hasBest || candidateName < bestCandidate) {
			// strings.Compare ascending
			bestCandidate = candidateName;
			hasBest = true;
		}
	}
	return hasBest ? std::string(bestCandidate) : std::string();
}

// scripttarget_stringer_generated.go: ScriptTarget.String — used by
// FileIncludeKind::LibFile (Default_library_for_target_0).
std::string scriptTargetString(ScriptTarget t) {
	switch (t) {
		case ScriptTarget::ES5: return "ES5";
		case ScriptTarget::ES2015: return "ES2015";
		case ScriptTarget::ES2016: return "ES2016";
		case ScriptTarget::ES2017: return "ES2017";
		case ScriptTarget::ES2018: return "ES2018";
		case ScriptTarget::ES2019: return "ES2019";
		case ScriptTarget::ES2020: return "ES2020";
		case ScriptTarget::ES2021: return "ES2021";
		case ScriptTarget::ES2022: return "ES2022";
		case ScriptTarget::ES2023: return "ES2023";
		case ScriptTarget::ES2024: return "ES2024";
		case ScriptTarget::ES2025: return "ES2025";
		case ScriptTarget::ES2026: return "ES2026";
		case ScriptTarget::ESNext: return "ESNext";
		case ScriptTarget::JSON: return "JSON";
		case ScriptTarget::None: return "None";
		default:
			return "ScriptTarget(" + std::to_string((int)t) + ")";
	}
}

}  // namespace

// ===========================================================================
// fileInclude.go
// ===========================================================================

std::string referenceFileLocation::text() const {
	if (node != nullptr) {
		if (!nodeIsSynthesized(node)) {
			size_t start = skipTrivia(file->text,
			                                   node->loc.pos());
			return std::string(file->text.substr(
			    start, node->end() - static_cast<int>(start)));
		}
		return "\"" + node->text() + "\"";
	}
	return file->text.substr(ref->pos(), ref->end() - ref->pos());
}

Diagnostic* referenceFileLocation::diagnosticAt(
    const DiagnosticMessage* message, std::vector<std::string> args) const {
	if (node != nullptr) {
		return tsoptions::createDiagnosticForNodeInSourceFile(
		    file, node, message, std::move(args));
	}
	return newDiagnostic(file, *ref, message, std::move(args));
}

referenceFileLocation FileIncludeReason::getReferencedLocation(
    SimpleProgram* program) const {
	const referencedFileData* ref = asReferencedFileData();
	SourceFile* file = program->GetSourceFileByPath(ref->file);
	switch (kind) {
		case FileIncludeKind::Import: {
			Node* specifier = nullptr;
			bool isSynthetic = false;
			if (ref->synthetic != nullptr) {
				specifier = ref->synthetic;
				isSynthetic = true;
			} else if (ref->index <
			           static_cast<int>(file->imports.size())) {
				specifier = file->imports[ref->index];
			} else {
				int augIndex =
				    static_cast<int>(file->imports.size());
				for (Node* imp : file->ModuleAugmentations) {
					if (imp->kind == Kind::StringLiteral) {
						if (augIndex == ref->index) {
							specifier = imp;
							break;
						}
						augIndex++;
					}
				}
			}
			module::ResolvedModule* resolution =
			    program->GetResolvedModuleFromModuleSpecifier(file,
			                                                specifier);
			return {file, specifier, nullptr,
			        resolution ? resolution->PackageId
			                   : module::PackageId{},
			        isSynthetic};
		}
		case FileIncludeKind::ReferenceFile:
			return {file, nullptr,
			        file->ReferencedFiles[ref->index], {}, false};
		case FileIncludeKind::TypeReferenceDirective:
			return {file, nullptr,
			        file->TypeReferenceDirectives[ref->index],
			        {}, false};
		case FileIncludeKind::LibReferenceDirective:
			return {file, nullptr,
			        file->LibReferenceDirectives[ref->index], {},
			        false};
		default:
			TSC_UNREACHABLE("FileIncludeReason::getReferencedLocation — "
			                "unknown kind");
	}
}

Diagnostic* FileIncludeReason::toDiagnostic(SimpleProgram* program,
                                          bool relativeFileName) const {
	if (relativeFileName) {
		return computeDiagnostic(
		    program, [program](std::string_view fileName) {
			    return tspath::getRelativePathFromDirectory(
			        program->GetCurrentDirectory(), fileName,
			        program->comparePathsOptions());
		    });
	}
	return computeDiagnostic(program, [](std::string_view fileName) {
		return std::string(fileName);
	});
}

Diagnostic* FileIncludeReason::computeDiagnostic(
    SimpleProgram* program,
    const std::function<std::string(std::string_view)>& toFileName) const {
	if (isReferencedFile()) {
		return computeReferenceFileDiagnostic(program, toFileName);
	}
	switch (kind) {
		case FileIncludeKind::RootFile:
			// No config file in this slice.
			return tsoptions::newCompilerDiagnostic(
			    Root_file_specified_for_compilation);
		case FileIncludeKind::AutomaticTypeDirectiveFile: {
			auto* data =
			    std::get_if<automaticTypeDirectiveFileData>(&this->data);
			if (!program->Options()->UsesWildcardTypes()) {
				if (!data->PackageId.Name.empty()) {
					return tsoptions::newCompilerDiagnostic(
					    
					        Entry_point_of_type_library_0_specified_in_compilerOptions_with_packageId_1,
					    {data->typeReference,
					     data->PackageId.String()});
				}
				return tsoptions::newCompilerDiagnostic(
				    
				        Entry_point_of_type_library_0_specified_in_compilerOptions,
				    {data->typeReference});
			}
			if (!data->PackageId.Name.empty()) {
				return tsoptions::newCompilerDiagnostic(
				    
				        Entry_point_for_implicit_type_library_0_with_packageId_1,
				    {data->typeReference,
				     data->PackageId.String()});
			}
			return tsoptions::newCompilerDiagnostic(
			    Entry_point_for_implicit_type_library_0,
			    {data->typeReference});
		}
		case FileIncludeKind::LibFile: {
			if (auto* index = std::get_if<int>(&data); index != nullptr) {
				return tsoptions::newCompilerDiagnostic(
				    Library_0_specified_in_compilerOptions,
				    {program->Options()->Lib[*index]});
			}
			std::string target =
			    scriptTargetString(
			        program->Options()->GetEmitScriptTarget());
			if (!target.empty()) {
				return tsoptions::newCompilerDiagnostic(
				    Default_library_for_target_0,
				    {target});
			}
			return tsoptions::newCompilerDiagnostic(
			    Default_library);
		}
		case FileIncludeKind::ContentMapperSupplemental: {
			auto* path = std::get_if<tspath::Path>(&data);
			SourceFile* canonical =
			    program->GetSourceFileByPath(*path);
			return tsoptions::newCompilerDiagnostic(
			    
			        Supplemental_virtual_file_produced_by_the_content_mapper_for_file_0,
			    {toFileName(canonical->FileName())});
		}
		default:
			TSC_UNREACHABLE("FileIncludeReason::computeDiagnostic — "
			                "unknown kind");
	}
}

Diagnostic* FileIncludeReason::computeReferenceFileDiagnostic(
    SimpleProgram* program,
    const std::function<std::string(std::string_view)>& toFileName) const {
	referenceFileLocation referenceLocation =
	    program->includeProcessor_.getReferenceLocation(this, program);
	std::string referenceText = referenceLocation.text();
	switch (kind) {
		case FileIncludeKind::Import:
			if (!referenceLocation.isSynthetic) {
				if (!referenceLocation.PackageId.Name.empty()) {
					return tsoptions::newCompilerDiagnostic(
					    
					        Imported_via_0_from_file_1_with_packageId_2,
					    {referenceText,
					     toFileName(referenceLocation.file
					                    ->FileName()),
					     referenceLocation.PackageId
					         .String()});
				}
				return tsoptions::newCompilerDiagnostic(
				    Imported_via_0_from_file_1,
				    {referenceText,
				     toFileName(referenceLocation.file
				                    ->FileName())});
			} else if (auto it =
			               program->importHelpersImportSpecifiers.find(
			                   referenceLocation.file->Path());
			           it != program->importHelpersImportSpecifiers
			                     .end() &&
			           it->second == referenceLocation.node) {
				if (!referenceLocation.PackageId.Name.empty()) {
					return tsoptions::newCompilerDiagnostic(
					    
					        Imported_via_0_from_file_1_with_packageId_2_to_import_importHelpers_as_specified_in_compilerOptions,
					    {referenceText,
					     toFileName(referenceLocation.file
					                    ->FileName()),
					     referenceLocation.PackageId
					         .String()});
				}
				return tsoptions::newCompilerDiagnostic(
				    
				        Imported_via_0_from_file_1_to_import_importHelpers_as_specified_in_compilerOptions,
				    {referenceText,
				     toFileName(referenceLocation.file
				                    ->FileName())});
			} else {
				if (!referenceLocation.PackageId.Name.empty()) {
					return tsoptions::newCompilerDiagnostic(
					    
					        Imported_via_0_from_file_1_with_packageId_2_to_import_jsx_and_jsxs_factory_functions,
					    {referenceText,
					     toFileName(referenceLocation.file
					                    ->FileName()),
					     referenceLocation.PackageId
					         .String()});
				}
				return tsoptions::newCompilerDiagnostic(
				    
				        Imported_via_0_from_file_1_to_import_jsx_and_jsxs_factory_functions,
				    {referenceText,
				     toFileName(referenceLocation.file
				                    ->FileName())});
			}
		case FileIncludeKind::ReferenceFile:
			return tsoptions::newCompilerDiagnostic(
			    Referenced_via_0_from_file_1,
			    {referenceText,
			     toFileName(referenceLocation.file->FileName())});
		case FileIncludeKind::TypeReferenceDirective:
			if (!referenceLocation.PackageId.Name.empty()) {
				return tsoptions::newCompilerDiagnostic(
				    
				        Type_library_referenced_via_0_from_file_1_with_packageId_2,
				    {referenceText,
				     toFileName(referenceLocation.file
				                    ->FileName()),
				     referenceLocation.PackageId.String()});
			}
			return tsoptions::newCompilerDiagnostic(
			    
			        Type_library_referenced_via_0_from_file_1,
			    {referenceText,
			     toFileName(referenceLocation.file->FileName())});
		case FileIncludeKind::LibReferenceDirective:
			return tsoptions::newCompilerDiagnostic(
			    Library_referenced_via_0_from_file_1,
			    {referenceText,
			     toFileName(referenceLocation.file->FileName())});
		default:
			TSC_UNREACHABLE(
			    "FileIncludeReason::computeReferenceFileDiagnostic");
	}
}

// fileInclude.go:278 toRelatedInfo — no config file in this slice → only the
// referenced-file branch can produce a diagnostic.
Diagnostic* FileIncludeReason::toRelatedInfo(SimpleProgram* program) const {
	if (isReferencedFile()) {
		referenceFileLocation loc =
		    program->includeProcessor_.getReferenceLocation(this, program);
		if (loc.isSynthetic)
			return nullptr;
		switch (kind) {
			case FileIncludeKind::Import:
				return loc.diagnosticAt(
				    
				        File_is_included_via_import_here);
			case FileIncludeKind::ReferenceFile:
				return loc.diagnosticAt(
				    
				        File_is_included_via_reference_here);
			case FileIncludeKind::TypeReferenceDirective:
				return loc.diagnosticAt(
				    
				        File_is_included_via_type_library_reference_here);
			case FileIncludeKind::LibReferenceDirective:
				return loc.diagnosticAt(
				    
				        File_is_included_via_library_reference_here);
			default:
				TSC_UNREACHABLE(
				    "FileIncludeReason::toRelatedInfo");
		}
	}
	// Config file absent → rootFile/libFile/etc. produce nil.
	return nullptr;
}

// ===========================================================================
// processingDiagnostic.go
// ===========================================================================
Diagnostic* processingDiagnostic::toDiagnostic(SimpleProgram* program) {
	switch (kind) {
		case processingDiagnosticKind::UnknownReference: {
			const FileIncludeReason* ref =
			    std::get<const FileIncludeReason*>(data);
			referenceFileLocation loc =
			    ref->getReferencedLocation(program);
			switch (ref->kind) {
				case FileIncludeKind::TypeReferenceDirective:
					return loc.diagnosticAt(
					    
					        Cannot_find_type_definition_file_for_0,
					    {loc.ref->FileName});
				case FileIncludeKind::LibReferenceDirective: {
					std::string libName = tspath::toFileNameLowerCase(
					    loc.ref->FileName);
					std::string unqualifiedLibName = libName;
					if (unqualifiedLibName.starts_with("lib."))
						unqualifiedLibName.erase(0, 4);
					if (unqualifiedLibName.ends_with(".d.ts"))
						unqualifiedLibName.erase(
						    unqualifiedLibName.size() - 5);
					std::string suggestion =
					    getSpellingSuggestionForStrings(
					        unqualifiedLibName,
					        tsoptions::Libs);
					// Go: IfElse picks the message; args are always
					// (libName, suggestion).
					return loc.diagnosticAt(
					    !suggestion.empty()
					        ? 
					              Cannot_find_lib_definition_for_0_Did_you_mean_1
					        : 
					              Cannot_find_lib_definition_for_0,
					    {libName, suggestion});
				}
				default:
					TSC_UNREACHABLE(
					    "processingDiagnostic::toDiagnostic — "
					    "unknown include kind");
			}
		}
		case processingDiagnosticKind::ExplainingFileInclude:
			return createDiagnosticExplainingFile(program);
		default:
			TSC_UNREACHABLE(
			    "processingDiagnostic::toDiagnostic — unknown kind");
	}
}

// processingDiagnostic.go:74 createDiagnosticExplainingFile
Diagnostic* processingDiagnostic::createDiagnosticExplainingFile(
    SimpleProgram* program) {
	auto* diag = std::get_if<includeExplainingDiagnostic>(&data);
	std::vector<Diagnostic*> includeDetails;
	// Go: `includeDetails` is *non-nil* (possibly empty) once
	// diag.file != "" or anything was appended — the chain check
	// tests nil-ness, not emptiness.
	bool includeDetailsNonNil = false;
	std::vector<Diagnostic*> relatedInfo;
	std::vector<Diagnostic*> redirectInfo;
	const FileIncludeReason* preferredLocation = nullptr;
	std::unordered_set<const FileIncludeReason*> seenReasons;
	if (diag->diagnosticReason != nullptr &&
	    diag->diagnosticReason->isReferencedFile() &&
	    !program->includeProcessor_
	         .getReferenceLocation(diag->diagnosticReason, program)
	         .isSynthetic) {
		preferredLocation = diag->diagnosticReason;
	}

	auto processRelatedInfo = [&](const FileIncludeReason* includeReason) {
		if (preferredLocation == nullptr &&
		    includeReason->isReferencedFile() &&
		    !program->includeProcessor_
		         .getReferenceLocation(includeReason, program)
		         .isSynthetic) {
			preferredLocation = includeReason;
		} else if (preferredLocation != includeReason) {
			Diagnostic* info = program->includeProcessor_.getRelatedInfo(
			    includeReason, program);
			if (info != nullptr) {
				relatedInfo.push_back(info);
			}
		}
	};
	auto processInclude = [&](const FileIncludeReason* includeReason) {
		if (!seenReasons.insert(includeReason).second)
			return;
		includeDetails.push_back(
		    includeReason->toDiagnostic(program, false));
		processRelatedInfo(includeReason);
	};

	if (!diag->file.empty()) {
		includeDetailsNonNil = true;
		auto it =
		    program->includeProcessor_.fileIncludeReasons.find(diag->file);
		if (it != program->includeProcessor_.fileIncludeReasons.end()) {
			for (auto* reason : it->second) {
				processInclude(reason);
			}
		}
		redirectInfo = program->includeProcessor_
		                   .explainRedirectAndImpliedFormat(
		                       program, diag->file,
		                       [](std::string_view fileName) {
			                       return std::string(fileName);
		                       });
	}
	if (diag->diagnosticReason != nullptr) {
		processInclude(diag->diagnosticReason);
		includeDetailsNonNil = true;
	}
	std::vector<Diagnostic*> chain;
	if (includeDetailsNonNil &&
	    (preferredLocation == nullptr || seenReasons.size() != 1)) {
		Diagnostic* fileReason = tsoptions::newCompilerDiagnostic(
		    The_file_is_in_the_program_because_Colon);
		fileReason->messageChain = includeDetails;
		chain = {fileReason};
	}
	for (auto* d : redirectInfo)
		chain.push_back(d);

	Diagnostic* result = nullptr;
	if (preferredLocation != nullptr) {
		result = program->includeProcessor_
		             .getReferenceLocation(preferredLocation, program)
		             .diagnosticAt(diag->message, diag->args);
	}
	if (result == nullptr) {
		result = tsoptions::newCompilerDiagnostic(diag->message, diag->args);
	}
	if (!chain.empty()) {
		result->messageChain = std::move(chain);
	}
	if (!relatedInfo.empty()) {
		result->SetRelatedInfo(std::move(relatedInfo));
	}
	return result;
}

// ===========================================================================
// includeprocessor.go
// ===========================================================================
DiagnosticsCollection* includeProcessor::getDiagnostics(SimpleProgram* p) {
	if (!computedDiagnostics_) {
		computedDiagnostics_ =
		    std::make_unique<DiagnosticsCollection>();
		for (auto* d : processingDiagnostics) {
			computedDiagnostics_->Add(d->toDiagnostic(p));
		}
		for (auto& [path, resolutions] : p->resolvedModules) {
			for (auto& [key, resolvedModule] : resolutions) {
				if (resolvedModule == nullptr)
					continue;
				for (auto* diag :
				     resolvedModule->ResolutionDiagnostics) {
					computedDiagnostics_->Add(diag);
				}
			}
		}
		for (auto& [path, typeResolutions] : p->typeResolutionsInFile) {
			for (auto& [key, resolvedTypeRef] : typeResolutions) {
				if (resolvedTypeRef == nullptr)
					continue;
				for (auto* diag :
				     resolvedTypeRef->ResolutionDiagnostics) {
					computedDiagnostics_->Add(diag);
				}
			}
		}
	}
	return computedDiagnostics_.get();
}

void includeProcessor::addProcessingDiagnosticsForFileCasing(
    tspath::Path file, const std::string& existingCasing,
    const std::string& currentCasing, const FileIncludeReason* reason) {
	bool hasRefReason = false;
	// Go's isReferencedFile is nil-safe: nil reason behaves as "not
	// referenced-file" and proceeds to the fileIncludeReasons check.
	if (reason == nullptr || !reason->isReferencedFile()) {
		auto it = fileIncludeReasons.find(file);
		if (it != fileIncludeReasons.end()) {
			for (auto* r : it->second) {
				if (r->isReferencedFile()) {
					hasRefReason = true;
					break;
				}
			}
		}
	}
	if (hasRefReason) {
		processingDiagnostics.push_back(newProcessingDiagnostic(
		    processingDiagnosticKind::ExplainingFileInclude,
		    includeExplainingDiagnostic{
		        file, reason,
		        
		            Already_included_file_name_0_differs_from_file_name_1_only_in_casing,
		        {existingCasing, currentCasing}}));
	} else {
		processingDiagnostics.push_back(newProcessingDiagnostic(
		    processingDiagnosticKind::ExplainingFileInclude,
		    includeExplainingDiagnostic{
		        file, reason,
		        
		            File_name_0_differs_from_already_included_file_name_1_only_in_casing,
		        {currentCasing, existingCasing}}));
	}
}

referenceFileLocation includeProcessor::getReferenceLocation(
    const FileIncludeReason* r, SimpleProgram* program) {
	if (auto it = reasonToReferenceLocation.find(r);
	    it != reasonToReferenceLocation.end())
		return it->second;
	auto loc = r->getReferencedLocation(program);
	reasonToReferenceLocation.emplace(r, loc);
	return loc;
}

Diagnostic* includeProcessor::getRelatedInfo(const FileIncludeReason* r,
                                             SimpleProgram* program) {
	if (auto it = includeReasonToRelatedInfo.find(r);
	    it != includeReasonToRelatedInfo.end())
		return it->second;
	Diagnostic* relatedInfo = r->toRelatedInfo(program);
	includeReasonToRelatedInfo.emplace(r, relatedInfo);
	return relatedInfo;
}

// includeprocessor.go: explainRedirectAndImpliedFormat
std::vector<Diagnostic*> includeProcessor::explainRedirectAndImpliedFormat(
    SimpleProgram* program, const tspath::Path& filePath,
    const std::function<std::string(std::string_view)>& toFileName) {
	if (auto it = redirectAndFileFormat.find(filePath);
	    it != redirectAndFileFormat.end())
		return it->second;
	std::vector<Diagnostic*> result;
	SourceFile* sourceFile = program->GetSourceFileByPath(filePath);
	auto redirectsIt = program->redirectFilesByPath.find(filePath);
	redirectsFile* redirectsFilePtr =
	    redirectsIt != program->redirectFilesByPath.end()
	        ? &redirectsIt->second
	        : nullptr;
	if (redirectsFilePtr == nullptr && sourceFile == nullptr) {
		return result;
	}
	// No project references — GetSourceOfProjectReferenceIfOutputIncluded
	// == identity.
	if (redirectsFilePtr != nullptr) {
		SourceFile* targetFile =
		    program->GetSourceFileByPath(redirectsFilePtr->target);
		result.push_back(tsoptions::newCompilerDiagnostic(
		    File_redirects_to_file_0,
		    {toFileName(targetFile->FileName())}));
	}

	if (sourceFile != nullptr && isExternalOrCommonJSModule(sourceFile)) {
		const SourceFileMetaData& metaData =
		    program->GetSourceFileMetaData(sourceFile->Path());
		switch (program->GetImpliedNodeFormatForEmit(sourceFile)) {
			case ModuleKind::ESNext:
				if (metaData.PackageJsonType == "module") {
					result.push_back(tsoptions::newCompilerDiagnostic(
					    
					        File_is_ECMAScript_module_because_0_has_field_type_with_value_module,
					    {toFileName(metaData.PackageJsonDirectory +
					                "/package.json")}));
				}
				break;
			case ModuleKind::CommonJS:
				if (!metaData.PackageJsonType.empty()) {
					result.push_back(tsoptions::newCompilerDiagnostic(
					    
					        File_is_CommonJS_module_because_0_has_field_type_whose_value_is_not_module,
					    {toFileName(
					        metaData.PackageJsonDirectory +
					        "/package.json")}));
				} else if (!metaData.PackageJsonDirectory.empty()) {
					result.push_back(tsoptions::newCompilerDiagnostic(
					    
					        File_is_CommonJS_module_because_0_does_not_have_field_type,
					    {toFileName(
					        metaData.PackageJsonDirectory +
					        "/package.json")}));
				} else {
					result.push_back(tsoptions::newCompilerDiagnostic(
					    
					        File_is_CommonJS_module_because_package_json_was_not_found));
				}
				break;
			default: break;
		}
	}
	redirectAndFileFormat.emplace(filePath, result);
	return result;
}

}  // namespace tsc::compiler
