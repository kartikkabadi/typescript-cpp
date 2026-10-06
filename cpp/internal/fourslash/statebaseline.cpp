// statebaseline.go — fourslash "state baseline" diff printer.
// Port of tsc/internal/fourslash/statebaseline.go (package fourslash).
#include "internal/fourslash/fourslash.h"

#include <algorithm>
#include <set>

namespace tsc::fourslash {

// ===========================================================================
// statebaseline.go:25-64 — stateBaseline
// ===========================================================================

// newStateBaseline — statebaseline.go:35.
std::shared_ptr<stateBaseline> newStateBaseline(
    const std::shared_ptr<vfs::iovfs::FsWithSys>& fsFromMap) {
	auto sb = std::make_shared<stateBaseline>();
	sb->fsDiffer =
	    std::make_shared<testutil::fsbaselineutil::FSDiffer>();
	sb->fsDiffer->FS = fsFromMap.get();
	sb->fsDiffer->WrittenFiles =
	    std::make_shared<collections::SyncSet<std::string>>();
	// fmt.Fprintf(&stateBaseline.baseline, "UseCaseSensitiveFileNames: %v\n", ...)
	sb->baseline.WriteString(gostd::sprintf(
	    "UseCaseSensitiveFileNames: %v\n",
	    {fsFromMap->UseCaseSensitiveFileNames()}));
	sb->fsDiffer->BaselineFSwithDiff(&sb->baseline);
	return sb;
}

// baselineRequestOrNotificationWorker — statebaseline.go:52. The Go body
// marshals requestOrMessage{method, params} with json.WithIndent("  ");
// the params value is already serialized (see baselineRequestOrNotification
// in the header), so reindent the assembled raw object text.
void FourslashTest::baselineRequestOrNotificationWorker(
    gostd::testing::T* t, const std::string& rawJson) {
	t->Helper();
	if (!testData->isStateBaseliningEnabled()) {
		return;
	}

	auto res = reindentJsonText(rawJson);
	stateBaseline_->baseline.WriteString("\n");
	stateBaseline_->baseline.WriteString(res);
	stateBaseline_->baseline.WriteString("\n");
	stateBaseline_->isInitialized = true;
}

// baselineProjectsAfterNotification — statebaseline.go:66.
void FourslashTest::baselineProjectsAfterNotification(
    gostd::testing::T* t, const std::string& fileName) {
	t->Helper();
	if (!testData->isStateBaseliningEnabled()) {
		return;
	}
	// Do hover so we have snapshot to check things on!!
	auto params = std::make_shared<lsproto::HoverParams>();
	params->TextDocument.Uri = lsconv::FileNameToDocumentURI(fileName);
	params->Position = lsproto::Position{0, 0};
	auto [_msg, _result, resultOk] = client->SendRequest(
	    t, lsproto::TextDocumentHoverInfo, params);
	if (!resultOk) {
		t->Fatal({std::string("hover request failed")});
	}
	baselineState(t);
}

// baselineState — statebaseline.go:85.
void FourslashTest::baselineState(gostd::testing::T* t) {
	t->Helper();

	if (!testData->isStateBaseliningEnabled()) {
		return;
	}

	auto serialized = serializedState(t);
	if (!serialized.empty()) {
		stateBaseline_->baseline.WriteString("\n");
		stateBaseline_->baseline.WriteString(serialized);
	}
}

// serializedState — statebaseline.go:99.
std::string FourslashTest::serializedState(gostd::testing::T* t) {
	t->Helper();

	gostr::Builder builder;
	stateBaseline_->fsDiffer->BaselineFSwithDiff(&builder);
	if (gostr::trimSpace(builder.String()).empty()) {
		builder.Reset();
	}

	printStateDiff(t, &builder);
	return builder.String();
}

// ===========================================================================
// statebaseline.go:119-240 — diffTable / diffTableWriter / slice printing
// ===========================================================================

// add — statebaseline.go:129.
void diffTable::add(const std::string& key, const std::string& value) {
	diff.Set(key, value);
}

// print — statebaseline.go:133.
void diffTable::print(gostd::io::Writer* w, const std::string& header) {
	auto count = diff.Size();
	if (count == 0) {
		return;
	}
	if (!header.empty()) {
		gostr::fprint(w, gostd::sprintf("%s%s\n",
		                              {options.indent, header}));
	}
	std::vector<std::string> diffKeys;
	diffKeys.reserve(count);
	int keyWidth = 0;
	for (auto& key : diff.Keys()) {
		keyWidth = std::max(keyWidth, (int)key.size());
		diffKeys.push_back(key);
	}
	if (options.sortKeys) {
		std::sort(diffKeys.begin(), diffKeys.end());
	}

	for (auto& key : diffKeys) {
		auto value = diff.GetOrZero(key);
		// fmt.Fprintf(w, "%s%-*s %s\n", indent, keyWidth+1, key, value)
		gostr::fprint(w, options.indent + "  " +
		                     gostr::padRight(key, keyWidth + 1) +
		                     value + "\n");
	}
}

// setHasChange — statebaseline.go:168.
void diffTableWriter::setHasChange() { hasChange = true; }

// add — statebaseline.go:172.
void diffTableWriter::add(
    const std::string& key,
    const std::function<void(gostd::io::Writer*)>& fn) {
	diffs[key] = fn;
}

// newDiffTableWriter — statebaseline.go:164.
std::shared_ptr<diffTableWriter> newDiffTableWriter(
    const std::string& header) {
	auto d = std::make_shared<diffTableWriter>();
	d->header = header;
	return d;
}

// print — statebaseline.go:176.
void diffTableWriter::print(gostd::io::Writer* w) {
	if (hasChange) {
		gostr::fprint(w, header + "::\n");
		std::vector<std::string> keys;
		keys.reserve(diffs.size());
		for (auto& [k, _] : diffs) {
			keys.push_back(k);
		}
		std::sort(keys.begin(), keys.end());
		for (auto& key : keys) {
			diffs[key](w);
		}
	}
}

// areIterSeqEqual — statebaseline.go:187.
bool areIterSeqEqual(const goseq::Seq<std::string>& a,
                     const goseq::Seq<std::string>& b) {
	auto aSlice = goseq::collect(a);
	auto bSlice = goseq::collect(b);
	std::sort(aSlice.begin(), aSlice.end());
	std::sort(bSlice.begin(), bSlice.end());
	return aSlice == bSlice;
}

// printSlicesWithDiffTable — statebaseline.go:195.
void printSlicesWithDiffTable(
    gostd::io::Writer* w, const std::string& header,
    const std::vector<std::string>& newSlice,
    const std::function<std::vector<std::string>()>& getOldSlice,
    const diffTableOptions& options, const std::string& topChange,
    const std::function<bool(const std::string&)>& isDefault) {
	std::vector<std::string> oldSlice;
	if (topChange == "*modified*") {
		oldSlice = getOldSlice();
	}
	diffTable table;
	table.options = options;
	for (auto& entry : newSlice) {
		std::string entryChange;
		if (isDefault && isDefault(entry)) {
			entryChange = "(default) ";
		}
		if (topChange == "*modified*" &&
		    std::find(oldSlice.begin(), oldSlice.end(), entry) ==
		        oldSlice.end()) {
			entryChange = "*new*";
		}
		table.add(entry, entryChange);
	}
	if (topChange == "*modified*") {
		for (auto& entry : oldSlice) {
			if (std::find(newSlice.begin(), newSlice.end(), entry) ==
			    newSlice.end()) {
				table.add(entry, "*deleted*");
			}
		}
	}
	table.print(w, header);
}

// sliceFromIterSeqString — statebaseline.go:221.
std::vector<std::string> sliceFromIterSeqString(
    const goseq::Seq<std::string>& seq) {
	std::vector<std::string> result;
	if (seq) {
		seq([&](const std::string& path) {
			result.push_back(path);
			return true;
		});
	}
	std::sort(result.begin(), result.end());
	return result;
}

// printStringIterSeqWithDiffTable — statebaseline.go:230.
void printStringIterSeqWithDiffTable(
    gostd::io::Writer* w, const std::string& header,
    const goseq::Seq<std::string>& newIterSeq,
    const std::function<goseq::Seq<std::string>()>& getOldIterSeq,
    const diffTableOptions& options, const std::string& topChange) {
	printSlicesWithDiffTable(
	    w, header, sliceFromIterSeqString(newIterSeq),
	    [&]() {
		    return sliceFromIterSeqString(getOldIterSeq());
	    },
	    options, topChange, nullptr);
}

// ===========================================================================
// statebaseline.go:242-510 — printStateDiff and the per-section printers
// ===========================================================================

// printStateDiff — statebaseline.go:242.
void FourslashTest::printStateDiff(gostd::testing::T* t,
                                   gostd::io::Writer* w) {
	if (!stateBaseline_->isInitialized) {
		return;
	}
	auto session = client->Server->Session();
	auto snapshot = session->Snapshot();

	printProjectsDiff(t, snapshot, w);
	printOpenFilesDiff(t, snapshot, w);
	printConfigFileRegistryDiff(t, snapshot, w);
}

// printProjectsDiff — statebaseline.go:254.
void FourslashTest::printProjectsDiff(gostd::testing::T* t,
                                      project::Snapshot* snapshot,
                                      gostd::io::Writer* w) {
	t->Helper();

	std::unordered_map<std::string, projectInfo> currentProjects;
	diffTableOptions options{.indent = "  "};
	auto projectsDiffTable = newDiffTableWriter("Projects");

	for (auto* project : snapshot->ProjectCollection->Projects()) {
		auto* program = project->GetProgram();
		compiler::SimpleProgram* oldProgram = nullptr;
		currentProjects[std::string(project->ID())] = program;
		std::string projectChange;
		auto existingIt = stateBaseline_->serializedProjects.find(
		    std::string(project->ID()));
		if (existingIt != stateBaseline_->serializedProjects.end()) {
			oldProgram = existingIt->second;
			if (oldProgram != program) {
				projectChange = "*modified*";
				projectsDiffTable->setHasChange();
			}
		} else {
			projectChange = "*new*";
			projectsDiffTable->setHasChange();
		}

		auto projectID = std::string(project->ID());
		projectsDiffTable->add(
		    projectID,
		    [projectID, projectChange, program, oldProgram,
		     options](gostd::io::Writer* w) {
			    gostr::fprint(w, gostd::sprintf("  [%s] %s\n",
			                                  {projectID,
			                                   projectChange}));
			    diffTable subDiff;
			    subDiff.options = options;
			    if (program != nullptr) {
				    for (auto* file : program->GetSourceFiles()) {
					    std::string fileDiff;
					    // No need to write "*new*" for files as its
					    // obvious
					    auto& fileName = file->FileName();
					    if (projectChange == "*modified*") {
						    if (oldProgram == nullptr) {
							    if (!isLibFile(fileName)) {
								    fileDiff = "*new*";
							    }
						    } else if (
						        auto* oldFile =
						            oldProgram
						                ->GetSourceFileByPath(
						                    file->Path());
						        oldFile == nullptr) {
							    fileDiff = "*new*";
						    } else if (oldFile != file) {
							    fileDiff = "*modified*";
						    }
					    }
					    if (!fileDiff.empty() ||
					        !isLibFile(fileName)) {
						    subDiff.add(fileName, fileDiff);
					    }
				    }
			    }
			    if (oldProgram != program && oldProgram != nullptr) {
				    for (auto* file :
				         oldProgram->GetSourceFiles()) {
					    if (program == nullptr ||
					        program->GetSourceFileByPath(
					            file->Path()) == nullptr) {
						    subDiff.add(file->FileName(),
						                "*deleted*");
					    }
				    }
			    }
			    subDiff.print(w, "");
		    });
	}

	for (auto& entry :
	     stateBaseline_->serializedProjects) {
		// clang-15: hoisted out of the structured binding for capture.
		auto projectName = entry.first;
		auto info = entry.second;
		if (currentProjects.find(projectName) ==
		    currentProjects.end()) {
			projectsDiffTable->setHasChange();
			projectsDiffTable->add(
			    projectName,
			    [projectName, info,
			     options](gostd::io::Writer* w) {
				    gostr::fprint(w,
				                  gostd::sprintf("  [%s] "
				                                 "*deleted*\n",
				                                 {projectName}));
				    diffTable subDiff;
				    subDiff.options = options;
				    if (info != nullptr) {
					    for (auto* file :
					         info->GetSourceFiles()) {
						    auto fileName =
						        file->FileName();
						    if (!isLibFile(fileName)) {
							    subDiff.add(fileName, "");
						    }
					    }
				    }
				    subDiff.print(w, "");
			    });
		}
	}
	stateBaseline_->serializedProjects = currentProjects;
	projectsDiffTable->print(w);
}

// printOpenFilesDiff — statebaseline.go:335.
void FourslashTest::printOpenFilesDiff(gostd::testing::T* t,
                                       project::Snapshot* snapshot,
                                       gostd::io::Writer* w) {
	t->Helper();

	std::unordered_map<std::string, std::shared_ptr<openFileInfo>>
	    currentOpenFiles;
	auto filesDiffTable = newDiffTableWriter("Open Files");
	diffTableOptions options{.indent = "  ", .sortKeys = true};
	for (auto& fileName : openFiles) {
		auto path = tspath::toPath(fileName, "/",
		                           vfs->UseCaseSensitiveFileNames());
		auto* defaultProject =
		    snapshot->ProjectCollection->GetDefaultProject(path);
		auto newFileInfo = std::make_shared<openFileInfo>();
		if (defaultProject != nullptr) {
			newFileInfo->defaultProjectName =
			    std::string(defaultProject->ID());
		}
		for (auto* project :
		     snapshot->ProjectCollection->Projects()) {
			if (auto* program = project->GetProgram();
			    program != nullptr &&
			    program->GetSourceFileByPath(path) != nullptr) {
				newFileInfo->allProjects.push_back(
				    std::string(project->ID()));
			}
		}
		std::sort(newFileInfo->allProjects.begin(),
		          newFileInfo->allProjects.end());
		currentOpenFiles[fileName] = newFileInfo;
		std::string openFileChange;
		std::shared_ptr<openFileInfo> oldFileInfo;
		auto existingIt =
		    stateBaseline_->serializedOpenFiles.find(fileName);
		if (existingIt !=
		    stateBaseline_->serializedOpenFiles.end()) {
			oldFileInfo = existingIt->second;
			if (oldFileInfo->defaultProjectName !=
			        newFileInfo->defaultProjectName ||
			    oldFileInfo->allProjects != newFileInfo->allProjects) {
				openFileChange = "*modified*";
				filesDiffTable->setHasChange();
			}
		} else {
			openFileChange = "*new*";
			filesDiffTable->setHasChange();
		}

		filesDiffTable->add(
		    fileName,
		    [fileName, openFileChange, newFileInfo, oldFileInfo,
		     options](gostd::io::Writer* w) {
			    gostr::fprint(w, gostd::sprintf("  [%s] %s\n",
			                                  {fileName,
			                                   openFileChange}));
			    printSlicesWithDiffTable(
			        w, "", newFileInfo->allProjects,
			        [oldFileInfo]() {
				        return oldFileInfo
				                   ? oldFileInfo->allProjects
				                   : std::vector<
				                         std::string>{};
			        },
			        options, openFileChange,
			        [newFileInfo](const std::string& projectName) {
				        return projectName ==
				               newFileInfo->defaultProjectName;
			        });
		    });
	}
	for (auto& entry :
	     stateBaseline_->serializedOpenFiles) {
		auto fileName = entry.first;
		if (currentOpenFiles.find(fileName) ==
		    currentOpenFiles.end()) {
			filesDiffTable->setHasChange();
			filesDiffTable->add(fileName,
			                    [fileName](gostd::io::Writer* w) {
				                    gostr::fprint(
				                        w, gostd::sprintf(
				                               "  [%s] "
				                               "*closed*\n",
				                               {fileName}));
			                    });
		}
	}
	stateBaseline_->serializedOpenFiles = currentOpenFiles;
	filesDiffTable->print(w);
}

// printConfigFileRegistryDiff — statebaseline.go:395.
void FourslashTest::printConfigFileRegistryDiff(
    gostd::testing::T* t, project::Snapshot* snapshot,
    gostd::io::Writer* w) {
	t->Helper();
	auto* configFileRegistry =
	    snapshot->ProjectCollection->ConfigFileRegistry();

	auto configDiffsTable = newDiffTableWriter("Config");
	auto configFileNamesDiffsTable =
	    newDiffTableWriter("Config File Names");

	if (stateBaseline_->serializedConfigFileRegistry ==
	    configFileRegistry) {
		return;
	}
	diffTableOptions options{.indent = "    ", .sortKeys = true};
	configFileRegistry->ForEachTestConfigEntry(
	    [&](tspath::Path path, project::TestConfigEntry* entry) {
		    std::string configChange;
		    // Go: serializedConfigFileRegistry.GetTestConfigEntry is a
		    // nil-receiver-safe method (`if c != nil`).
		    auto* serializedRegistry =
		        stateBaseline_->serializedConfigFileRegistry;
		    auto oldEntry =
		        serializedRegistry != nullptr
		            ? serializedRegistry->GetTestConfigEntry(path)
		            : nullptr;
		    if (oldEntry == nullptr) {
			    configChange = "*new*";
			    configDiffsTable->setHasChange();
		    } else if (oldEntry != entry) {
			    if (!areIterSeqEqual(oldEntry->RetainingProjects,
			                         entry->RetainingProjects) ||
			        !areIterSeqEqual(oldEntry->RetainingOpenFiles,
			                         entry->RetainingOpenFiles) ||
			        !areIterSeqEqual(oldEntry->RetainingConfigs,
			                         entry->RetainingConfigs)) {
				    configChange = "*modified*";
				    configDiffsTable->setHasChange();
			    }
		    }
		    configDiffsTable->add(
		        std::string(path),
		        [this, entry, oldEntry, configChange,
		         options](gostd::io::Writer* w) {
			        gostr::fprint(w, gostd::sprintf(
			                             "  [%s] %s\n",
			                             {entry->FileName,
			                              configChange}));
			        // Print the details of the config entry
			        std::string retainingProjectsModified;
			        std::string retainingOpenFilesModified;
			        std::string retainingConfigsModified;
			        if (configChange == "*modified*") {
				        if (!areIterSeqEqual(
				                entry->RetainingProjects,
				                oldEntry->RetainingProjects)) {
					        retainingProjectsModified =
					            " *modified*";
				        }
				        if (!areIterSeqEqual(
				                entry->RetainingOpenFiles,
				                oldEntry->RetainingOpenFiles)) {
					        retainingOpenFilesModified =
					            " *modified*";
				        }
				        if (!areIterSeqEqual(
				                entry->RetainingConfigs,
				                oldEntry->RetainingConfigs)) {
					        retainingConfigsModified =
					            " *modified*";
				        }
			        }
			        printStringIterSeqWithDiffTable(
			            w,
			            "RetainingProjects:" +
			                retainingProjectsModified,
			            entry->RetainingProjects,
			            [oldEntry]() -> goseq::Seq<std::string> {
				            return oldEntry
				                       ? oldEntry->RetainingProjects
				                       : nullptr;
			            },
			            options, configChange);
			        printStringIterSeqWithDiffTable(
			            w,
			            "RetainingOpenFiles:" +
			                retainingOpenFilesModified,
			            entry->RetainingOpenFiles,
			            [oldEntry]() -> goseq::Seq<std::string> {
				            return oldEntry
				                       ? oldEntry->RetainingOpenFiles
				                       : nullptr;
			            },
			            options, configChange);
			        printStringIterSeqWithDiffTable(
			            w,
			            "RetainingConfigs:" +
			                retainingConfigsModified,
			            entry->RetainingConfigs,
			            [oldEntry]() -> goseq::Seq<std::string> {
				            return oldEntry
				                       ? oldEntry->RetainingConfigs
				                       : nullptr;
			            },
			            options, configChange);
		        });
	    });
	configFileRegistry->ForEachTestConfigFileNamesEntry(
	    [&](tspath::Path path,
	        project::TestConfigFileNamesEntry* entry) {
		    std::string configFileNamesChange;
		    auto* serializedRegistry =
		        stateBaseline_->serializedConfigFileRegistry;
		    // nil-receiver-safe (`if c != nil`).
		    auto oldEntry =
		        serializedRegistry != nullptr
		            ? serializedRegistry
		                  ->GetTestConfigFileNamesEntry(path)
		            : nullptr;
		    if (oldEntry == nullptr) {
			    configFileNamesChange = "*new*";
			    configFileNamesDiffsTable->setHasChange();
		    } else if (oldEntry->NearestConfigFileName !=
			               entry->NearestConfigFileName ||
			           oldEntry->Ancestors != entry->Ancestors) {
			    configFileNamesChange = "*modified*";
			    configFileNamesDiffsTable->setHasChange();
		    }
		    configFileNamesDiffsTable->add(
		        std::string(path),
		        [path, entry, oldEntry, configFileNamesChange,
		         options](gostd::io::Writer* w) {
			        gostr::fprint(
			            w,
			            gostd::sprintf("  [%s] %s\n",
			                           {std::string(path),
			                            configFileNamesChange}));
			        std::string nearestConfigFileNameModified;
			        std::string ancestorDiffModified;
			        if (configFileNamesChange == "*modified*") {
				        if (oldEntry->NearestConfigFileName !=
				            entry->NearestConfigFileName) {
					        nearestConfigFileNameModified =
					            " *modified*";
				        }
				        if (oldEntry->Ancestors !=
				            entry->Ancestors) {
					        ancestorDiffModified =
					            " *modified*";
				        }
			        }
			        gostr::fprint(
			            w,
			            gostd::sprintf(
			                "    NearestConfigFileName: %s%s\n",
			                {entry->NearestConfigFileName,
			                 nearestConfigFileNameModified}));
			        diffTable ancestorDiff;
			        ancestorDiff.options = options;
			        for (auto& [config, ancestorOfConfig] :
			             entry->Ancestors) {
				        std::string ancestorChange;
				        if (configFileNamesChange ==
				            "*modified*") {
					        auto it = oldEntry->Ancestors.find(
					            config);
					        if (it != oldEntry->Ancestors.end()) {
						        if (it->second !=
						            ancestorOfConfig) {
							        ancestorChange =
							            "*modified*";
						        }
					        } else {
						        ancestorChange = "*new*";
					        }
				        }
				        ancestorDiff.add(
				            config,
				            ancestorOfConfig + " " +
				                ancestorChange);
			        }
			        if (configFileNamesChange == "*modified*") {
				        for (auto& [ancestorPath,
				                    oldConfigFileName] :
				             oldEntry->Ancestors) {
					        if (entry->Ancestors.find(
					                ancestorPath) ==
					            entry->Ancestors.end()) {
						        ancestorDiff.add(
						            ancestorPath,
						            oldConfigFileName +
						                " *deleted*");
					        }
				        }
			        }
			        ancestorDiff.print(
			            w, "Ancestors:" + ancestorDiffModified);
		        });
	    });

	stateBaseline_->serializedConfigFileRegistry
	    ->ForEachTestConfigEntry(
	        [&](tspath::Path path, project::TestConfigEntry* entry) {
		        if (configFileRegistry->GetTestConfigEntry(path) ==
		            nullptr) {
			        configDiffsTable->setHasChange();
			        configDiffsTable->add(
			            std::string(path),
			            [entry](gostd::io::Writer* w) {
				            gostr::fprint(
				                w,
				                gostd::sprintf(
				                    "  [%s] *deleted*\n",
				                    {entry->FileName}));
			            });
		        }
	        });
	stateBaseline_->serializedConfigFileRegistry
	    ->ForEachTestConfigFileNamesEntry(
	        [&](tspath::Path path,
	            project::TestConfigFileNamesEntry* entry) {
		        if (configFileRegistry
		                ->GetTestConfigFileNamesEntry(path) ==
		            nullptr) {
			        configFileNamesDiffsTable->setHasChange();
			        configFileNamesDiffsTable->add(
			            std::string(path),
			            [path](gostd::io::Writer* w) {
				            gostr::fprint(
				                w,
				                gostd::sprintf(
				                    "  [%s] *deleted*\n",
				                    {std::string(path)}));
			            });
		        }
	        });
	stateBaseline_->serializedConfigFileRegistry =
	    configFileRegistry;
	configDiffsTable->print(w);
	configFileNamesDiffsTable->print(w);
}

} // namespace tsc::fourslash
