// orchestrator.cpp — port of tsc/internal/execute/build/orchestrator.go.
#include <thread>

#include "internal/execute/build/build.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/vfs/cachedvfs/cachedvfs.h"

namespace tsc::execute::build {

// ===========================================================================
// core/workgroup.go — WorkGroup (port; also referenced by build.h signatures)
// ===========================================================================

// WorkGroup — Queue a fn (may run immediately or be deferred); RunAndWait
// runs all queued fns and blocks until they complete. Queue must not be
// called after RunAndWait returns.
struct workGroup {
	virtual ~workGroup() = default;
	virtual void Queue(std::function<void()> fn) = 0;
	virtual void RunAndWait() = 0;
};

struct parallelWorkGroup final : workGroup {
	std::atomic<bool> done{false};
	std::mutex mu;
	std::condition_variable cv;
	int running = 0;

	void Queue(std::function<void()> fn) override {
		if (done.load()) {
			TSC_UNREACHABLE("Queue called after RunAndWait returned");
		}
		{
			std::lock_guard<std::mutex> lock(mu);
			running++;
		}
		std::thread([this, fn = std::move(fn)]() mutable {
			fn();
			{
				std::lock_guard<std::mutex> lock(mu);
				running--;
			}
			cv.notify_all();
		}).detach();
	}

	void RunAndWait() override {
		{
			std::unique_lock<std::mutex> lock(mu);
			cv.wait(lock, [&] { return running == 0; });
		}
		done.store(true);
	}
};

struct singleThreadedWorkGroup final : workGroup {
	std::atomic<bool> done{false};
	std::mutex fnsMu;
	std::vector<std::function<void()>> fns;

	void Queue(std::function<void()> fn) override {
		if (done.load()) {
			TSC_UNREACHABLE("Queue called after RunAndWait returned");
		}
		std::lock_guard<std::mutex> lock(fnsMu);
		fns.push_back(std::move(fn));
	}

	void RunAndWait() override {
		for (;;) {
			std::function<void()> fn;
			{
				std::lock_guard<std::mutex> lock(fnsMu);
				if (fns.empty()) {
					break;
				}
				fn = std::move(fns.front());
				fns.erase(fns.begin());
			}
			fn();
		}
		done.store(true);
	}
};

// core.NewWorkGroup.
workGroup* newWorkGroup(bool singleThreaded) {
	if (singleThreaded) {
		return new singleThreadedWorkGroup();
	}
	return new parallelWorkGroup();
}

namespace {

// strings.Join.
std::string join(const std::vector<std::string>& parts,
                 std::string_view sep) {
	std::string out;
	for (std::size_t i = 0; i < parts.size(); i++) {
		if (i != 0) {
			out += sep;
		}
		out += parts[i];
	}
	return out;
}

} // namespace

// ===========================================================================
// orchestrator.go — OrchestratorResult
// ===========================================================================

// orchestrator.go:39 report.
void OrchestratorResult::report(Orchestrator* o) {
	reportWithFilesToDelete(o, true);
}

// orchestrator.go:43 reportWithFilesToDelete.
void OrchestratorResult::reportWithFilesToDelete(Orchestrator* o,
                                                 bool reportFilesToDelete) {
	if (tristateIsTrue(o->opts.Command->CompilerOptions->Watch)) {
		o->watchStatusReporter(tsoptions::newCompilerDiagnostic(
		    Errors.size() == 1
		        ? Found_1_error_Watching_for_file_changes
		        : Found_0_errors_Watching_for_file_changes,
		    {std::to_string(Errors.size())}));
	} else {
		o->errorSummaryReporter(Errors);
	}
	if (reportFilesToDelete && !FilesToDelete.empty()) {
		std::vector<std::string> parts;
		for (auto& f : FilesToDelete) {
			parts.push_back("\r\n * " + f);
		}
		o->createBuilderStatusReporter(nullptr)(
		    tsoptions::newCompilerDiagnostic(
		        A_non_dry_build_would_delete_the_following_files_Colon_0,
		        {join(parts, "")}));
	}
	if (!tristateIsTrue(o->opts.Command->CompilerOptions->Diagnostics) &&
	    !tristateIsTrue(o->opts.Command->CompilerOptions->ExtendedDiagnostics)) {
		return;
	}
	Statistics.SetTotalTime(o->opts.Sys->SinceStart());
	Statistics.Report(o->opts.Sys->Writer(), o->opts.Testing);
}

// ===========================================================================
// orchestrator.go — Orchestrator
// ===========================================================================

// orchestrator.go:87 relativeFileName.
std::string Orchestrator::relativeFileName(const std::string& fileName) {
	return tspath::convertToRelativePath(fileName, comparePathsOptions);
}

// orchestrator.go:91 toPath.
tspath::Path Orchestrator::toPath(const std::string& fileName) {
	return tspath::toPath(fileName, comparePathsOptions.currentDirectory,
	                      comparePathsOptions.useCaseSensitiveFileNames);
}

// orchestrator.go:95 resolveBuildInfoFileName.
std::string Orchestrator::resolveBuildInfoFileName(const std::string& fileName,
                                                   const std::string& buildInfoDir) {
	if (incremental::IsBuildInfoFileNameDefaultLibrary(fileName)) {
		return tspath::combinePaths(host_->DefaultLibraryPath(), {fileName});
	}
	return tspath::getNormalizedAbsolutePath(fileName, buildInfoDir);
}

// orchestrator.go:107 computeScheduleOrder — sorts the build order by
// dependency depth (projects with no upstream first, then their dependents,
// and so on). Builders take projects from this order and block until
// upstream projects are done, so with the plain depth-first order a builder
// that picks the root of a long chain sits idle while another builder works
// through the chain, even when unrelated projects are ready to build. Depth
// order reduces that avoidable blocking but does not eliminate it: a
// shallower project that has been picked up may not be done yet, so a
// builder can take a dependent of a slow project and wait on that project
// while a later project's upstream has already finished. The stable sort
// preserves the original order within a depth, and reporting still follows
// Order().
std::vector<std::string> Orchestrator::computeScheduleOrder() {
	struct scheduleEntry {
		std::string config;
		int depth = 0;
	};
	std::vector<scheduleEntry> entries(order.size());
	std::unordered_map<BuildTask*, int> depths;
	for (std::size_t i = 0; i < order.size(); i++) {
		auto& config = order[i];
		auto* task = getTask(toPath(config));
		int depth = 0;
		for (auto* upstream : task->upStream) {
			auto it = depths.find(upstream->task);
			depth = std::max(depth, (it != depths.end() ? it->second : 0) + 1);
		}
		depths[task] = depth;
		entries[i] = scheduleEntry{config, depth};
	}
	std::stable_sort(entries.begin(), entries.end(),
	                 [](const scheduleEntry& a, const scheduleEntry& b) {
		                 return a.depth < b.depth;
	                 });
	std::vector<std::string> result;
	result.reserve(entries.size());
	for (auto& entry : entries) {
		result.push_back(entry.config);
	}
	return result;
}

// orchestrator.go:142 Upstream.
std::vector<std::string> Orchestrator::Upstream(const std::string& configName) {
	auto path = toPath(configName);
	auto* task = getTask(path);
	std::vector<std::string> result;
	for (auto* t : task->upStream) {
		result.push_back(t->task->config);
	}
	return result;
}

// orchestrator.go:150 Downstream.
std::vector<std::string> Orchestrator::Downstream(const std::string& configName) {
	auto path = toPath(configName);
	auto* task = getTask(path);
	std::vector<std::string> result;
	for (auto* t : task->downStream) {
		result.push_back(t->config);
	}
	return result;
}

// orchestrator.go:166 getTask.
BuildTask* Orchestrator::getTask(const tspath::Path& path) {
	auto [task, ok] = tasks->Load(path);
	if (!ok) {
		TSC_UNREACHABLE(("No build task found for " + std::string(path)).c_str());
	}
	return task;
}

// orchestrator.go:174 createBuildTasks.
void Orchestrator::createBuildTasks(
    collections::SyncMap<tspath::Path, BuildTask*>* oldTasks,
    const std::vector<std::string>& configs, workGroup* wg) {
	for (auto& config : configs) {
		// Capture `config` by value: Go's range loop gives each iteration
		// its own variable (and the recursive call iterates a temporary
		// ResolvedProjectReferencePaths() result that dies on return), so
		// the queued lambda must own its copy of the config name.
		wg->Queue([this, config, oldTasks, wg]() {
			auto path = toPath(config);
			BuildTask* task = nullptr;
			buildInfoEntry* buildInfo = nullptr;
			if (oldTasks != nullptr) {
				if (auto [existing, ok] = oldTasks->Load(path); ok) {
					if (!existing->dirty) {
						// Reuse existing task if config is same
						task = existing;
					} else {
						if (existing->contentMapperProject != nullptr) {
							existing->contentMapperProject->Close();
						}
						buildInfo = existing->buildInfoEntry_;
					}
				}
			}
			if (task == nullptr) {
				task = new BuildTask();
				task->config = config;
				task->isInitialCycle = oldTasks == nullptr;
				task->pending.store(true);
				task->buildInfoEntry_ = buildInfo;
			}
			if (auto [_, loaded] = tasks->LoadOrStore(path, task); loaded) {
				return;
			}
			task->resolved = host_->GetResolvedProjectReference(config, path);
			task->upStream.clear();
			if (task->resolved != nullptr) {
				createBuildTasks(oldTasks,
				                 task->resolved->ResolvedProjectReferencePaths(),
				                 wg);
			}
		});
	}
}

// orchestrator.go:201 setupBuildTask.
BuildTask* Orchestrator::setupBuildTask(
    const std::string& configName, BuildTask* downStream,
    bool inCircularContext, collections::Set<tspath::Path>* completed,
    collections::Set<tspath::Path>* analyzing,
    std::vector<std::string> circularityStack) {
	auto path = toPath(configName);
	auto* task = getTask(path);
	if (!completed->Has(path)) {
		if (analyzing->Has(path)) {
			if (!inCircularContext) {
				errors.push_back(tsoptions::newCompilerDiagnostic(
				    Project_references_may_not_form_a_circular_graph_Cycle_detected_Colon_0,
				    {join(circularityStack, "\n")}));
			}
			return nullptr;
		}
		analyzing->Add(path);
		circularityStack.push_back(configName);
		if (task->resolved != nullptr) {
			auto resolvedRefs =
			    task->resolved->ResolvedProjectReferencePaths();
			for (std::size_t index = 0; index < resolvedRefs.size();
			     index++) {
				auto* upstream = setupBuildTask(
				    resolvedRefs[index], task,
				    inCircularContext ||
				        task->resolved->ProjectReferences()[index]->Circular,
				    completed, analyzing, circularityStack);
				if (upstream != nullptr) {
					task->upStream.push_back(
					    new upstreamTask{upstream, static_cast<int>(index)});
				}
			}
		}
		circularityStack.pop_back();
		completed->Add(path);
		task->built = std::make_shared<closeChan>();
		task->done = std::make_shared<closeChan>();
		order.push_back(configName);
	}
	if (tristateIsTrue(opts.Command->CompilerOptions->Watch) &&
	    downStream != nullptr) {
		task->downStream.push_back(downStream);
	}
	return task;
}

// orchestrator.go:242 GenerateGraphReusingOldTasks.
void Orchestrator::GenerateGraphReusingOldTasks() {
	auto* oldTasks = tasks;
	tasks = new collections::SyncMap<tspath::Path, BuildTask*>();
	order.clear();
	errors.clear();
	GenerateGraph(oldTasks);
}

// orchestrator.go:251 GenerateGraph.
void Orchestrator::GenerateGraph(
    collections::SyncMap<tspath::Path, BuildTask*>* oldTasks) {
	auto projects = opts.Command->ResolvedProjectPaths();
	// Parse all config files in parallel
	auto* wg = newWorkGroup(
	    tristateIsTrue(opts.Command->CompilerOptions->SingleThreaded));
	createBuildTasks(oldTasks, projects, wg);
	wg->RunAndWait();

	// Generate the graph
	collections::Set<tspath::Path> completed;
	collections::Set<tspath::Path> analyzing;
	std::vector<std::string> circularityStack;
	for (auto& project : projects) {
		setupBuildTask(project, nullptr, false, &completed, &analyzing,
		               circularityStack);
	}
	scheduleOrder = computeScheduleOrder();
	if (oldTasks != nullptr) {
		oldTasks->Range(
		    [&](const tspath::Path& path, BuildTask* oldTask) -> bool {
			    if (auto [task, ok] = tasks->Load(path);
			        ok && task == oldTask) {
				    return true;
			    }
			    if (oldTask->contentMapperProject != nullptr) {
				    oldTask->contentMapperProject->Close();
			    }
			    return true;
		    });
	}
	graphGenerated = true;
}

// orchestrator.go:279 Start — tsc -b entrypoint.
etsc::CommandLineResult Orchestrator::Start(gostd::Context ctx) {
	return start(ctx, "", false /*onlyReferences*/)->Result;
}

// orchestrator.go:284 Build — orchestrator.Build() entrypoint for api.
OrchestratorResult* Orchestrator::Build(gostd::Context ctx,
                                        const std::string& project) {
	recheckAllProjects(project);
	return start(ctx, project, false /*onlyReferences*/);
}

// orchestrator.go:290 BuildReferences.
OrchestratorResult*
Orchestrator::BuildReferences(gostd::Context ctx,
                               const std::string& project) {
	recheckAllProjects(project);
	return start(ctx, project, true /*onlyReferences*/);
}

// orchestrator.go:295 start.
OrchestratorResult* Orchestrator::start(gostd::Context ctx,
                                        const std::string& project,
                                        bool onlyReferences) {
	contentMapperHost = etsc::NewContentMapperHost(
	    ctx, opts.Sys,
	    opts.Command->CompilerOptions);
	bool closeHost = contentMapperHost != nullptr &&
	                 (!tristateIsTrue(opts.Command->CompilerOptions->Watch) ||
	                  opts.Testing == nullptr);
	struct hostCloser {
		~hostCloser() {
			if (close && host != nullptr) {
				host->Close();
			}
		}
		bool close;
		contentmapper::Host* host;
	} closer{closeHost, contentMapperHost.get()};
	if (tristateIsTrue(opts.Command->CompilerOptions->Watch)) {
		watchStatusReporter(tsoptions::newCompilerDiagnostic(
		    Starting_compilation_in_watch_mode, {}));
	}
	if (graphGenerated) {
		GenerateGraphReusingOldTasks();
	} else {
		GenerateGraph(nullptr);
	}
	auto [order, ok] = getBuildOrderFor(project);
	if (!ok) {
		return new OrchestratorResult{
		    etsc::CommandLineResult{
		        etsc::ExitStatusInvalidProject_OutputsSkipped, nullptr}};
	}
	if (onlyReferences && errors.empty()) {
		if (project.empty()) {
			return new OrchestratorResult{
			    etsc::CommandLineResult{
			        etsc::ExitStatusInvalidProject_OutputsSkipped, nullptr}};
		}
		order.resize(order.size() - 1);
	}
	auto* result = buildOrCleanOrder(order);
	if (tristateIsTrue(opts.Command->CompilerOptions->Watch)) {
		Watch(ctx);
		result->Result.Watcher = this;
	}
	return result;
}

// orchestrator.go:318 recheckAllProjects.
void Orchestrator::recheckAllProjects(const std::string& project) {
	if (!graphGenerated) {
		return;
	}
	auto [order, ok] = getBuildOrderFor(project);
	if (!ok) {
		return;
	}
	rangeTasks(order, [&](const tspath::Path& path, BuildTask* task) {
		task->resetStatus();
		task->resetConfig(this, toPath(task->config));
	});
	host_->mTimes = new collections::SyncMap<tspath::Path, fileTime>();
	resetCaches();
}

// orchestrator.go:335 Clean — orchestrator.Clean() entrypoint for api.
OrchestratorResult* Orchestrator::Clean(const std::string& project) {
	return clean(project, false);
}

// orchestrator.go:340 CleanReferences.
OrchestratorResult* Orchestrator::CleanReferences(const std::string& project) {
	return clean(project, true);
}

// orchestrator.go:344 clean.
OrchestratorResult* Orchestrator::clean(const std::string& project,
                                        bool onlyReferences) {
	if (!graphGenerated) {
		GenerateGraph(nullptr);
	}
	if (!errors.empty()) {
		auto* result = new OrchestratorResult{
		    etsc::CommandLineResult{
		        etsc::ExitStatusProjectReferenceCycle_OutputsSkipped,
		        nullptr},
		    errors};
		result->reportWithFilesToDelete(this, true);
		return result;
	}

	auto [order, ok] = getBuildOrderFor(project);
	if (!ok) {
		return new OrchestratorResult{
		    etsc::CommandLineResult{
		        etsc::ExitStatusInvalidProject_OutputsSkipped, nullptr}};
	}
	if (onlyReferences) {
		order.resize(order.size() - 1);
	}

	auto* result = new OrchestratorResult{};
	result->Statistics.Projects = static_cast<int>(order.size());
	bool dry = tristateIsTrue(opts.Command->BuildOptions->Dry);
	auto reportDiagnostic = createDiagnosticReporter(nullptr);
	for (auto& config : order) {
		auto* task = getTask(toPath(config));
		if (task->resolved == nullptr) {
			auto* diagnostic = tsoptions::newCompilerDiagnostic(
			    File_0_not_found, {task->config});
			reportDiagnostic(diagnostic);
			result->Errors.push_back(diagnostic);
			continue;
		}

		collections::Set<tspath::Path> inputs;
		for (auto& fileName : task->resolved->FileNames()) {
			inputs.Add(toPath(fileName));
		}
		bool deleted = false;
		task->resolved->GetOutputFileNames(
		    [&](std::string_view outputFile) {
			    deleted =
			        cleanProjectOutput(std::string(outputFile), &inputs,
			                           dry, &result->FilesToDelete,
			                           reportDiagnostic) ||
			        deleted;
			    return true;
		    });
		deleted = cleanProjectOutput(task->resolved->GetBuildInfoFileName(),
		                             &inputs, dry, &result->FilesToDelete,
		                             reportDiagnostic) ||
		          deleted;
		if (deleted) {
			task->resetStatus();
			std::lock_guard<std::mutex> lock(task->buildInfoEntryMu);
			task->buildInfoEntry_ = nullptr;
		}
	}

	result->reportWithFilesToDelete(this, dry);
	return result;
}

// orchestrator.go:385 getBuildOrderFor.
std::pair<std::vector<std::string>, bool>
Orchestrator::getBuildOrderFor(const std::string& project) {
	if (project.empty()) {
		return {order, true};
	}

	auto config = ::tsc::ResolveConfigFileNameOfProjectReference(
	    tspath::resolvePath(opts.Sys->GetCurrentDirectory(), {project}));
	auto [target, ok] = tasks->Load(toPath(config));
	if (!ok) {
		return {{}, false};
	}

	collections::Set<tspath::Path> projects;
	std::function<void(BuildTask*)> addProjectAndReferences =
	    [&](BuildTask* task) {
		    auto path = toPath(task->config);
		    if (projects.Has(path)) {
			    return;
		    }
		    projects.Add(path);
		    for (auto* upstream : task->upStream) {
			    addProjectAndReferences(upstream->task);
		    }
	    };
	addProjectAndReferences(target);

	std::vector<std::string> projectOrder;
	projectOrder.reserve(projects.Size());
	for (auto& config : order) {
		if (projects.Has(toPath(config))) {
			projectOrder.push_back(config);
		}
	}
	return {projectOrder, true};
}

// orchestrator.go:417 cleanProjectOutput.
bool Orchestrator::cleanProjectOutput(
    const std::string& outputFile,
    collections::Set<tspath::Path>* inputs, bool dry,
    std::vector<std::string>* filesToDelete,
    etsc::DiagnosticReporter reportDiagnostic) {
	if (outputFile.empty() || inputs->Has(toPath(outputFile)) ||
	    !host_->host_->fs->FileExists(outputFile)) {
		return false;
	}
	filesToDelete->push_back(outputFile);
	if (dry) {
		return false;
	}
	if (auto err = host_->host_->fs->Remove(outputFile); err) {
		reportDiagnostic(tsoptions::newCompilerDiagnostic(
		    Failed_to_delete_file_0, {outputFile}));
		return false;
	}
	return true;
}

// orchestrator.go:439 Watch.
void Orchestrator::Watch(gostd::Context ctx) {
	wm->Lock();

	if (opts.Testing == nullptr) {
		if (auto [value, _] =
		        opts.Sys->GetEnvironmentVariable("TS_WATCH_DEBUG");
		    !value.empty()) {
			wm->DebugLog = opts.Sys->Writer();
		}
		wm->EnsureDefaultBackend();
	}

	updateWatch();
	auto desiredDirs = computeDesiredWatches();
	if (auto err = wm->ReconcileWatches(desiredDirs); err) {
		*opts.Sys->Writer() << err->Error() << "\n";
		wm->ForceOverflow();
	}
	resetCaches();

	wm->Unlock();

	if (opts.Testing == nullptr) {
		wm->RunLoop(ctx, [this] { DoCycle(); });
	}
}

// orchestrator.go:466 updateWatch.
void Orchestrator::updateWatch() {
	auto* oldCache = host_->mTimes;
	host_->mTimes = new collections::SyncMap<tspath::Path, fileTime>();
	rangeTask([&](const tspath::Path& path, BuildTask* task) {
		task->updateWatch(this, oldCache);
	});
}

// orchestrator.go:474 resetCaches.
void Orchestrator::resetCaches() {
	// Clean out all the caches
	auto* cachesVfs =
	    static_cast<vfs::cachedvfs::FS*>(host_->host_->fs.get());
	cachesVfs->ClearCache();
	host_->extendedConfigCache =
	    std::make_unique<etsc::ExtendedConfigCache>();
	host_->sourceFiles.reset();
	host_->configTimes =
	    collections::SyncMap<tspath::Path, gostd::Duration>{};
}

// orchestrator.go:482 checkTasksForEventChanges.
void Orchestrator::checkTasksForEventChanges(
    const std::unordered_map<std::string, fswatch::EventKind>& changedPaths,
    std::atomic<bool>* needsConfigUpdate, std::atomic<bool>* needsUpdate) {
	std::unordered_map<tspath::Path, fswatch::EventKind> normalizedPaths;
	for (auto& [eventPath, kind] : changedPaths) {
		normalizedPaths[toPath(eventPath)] = kind;
	}

	for (auto& config : order) {
		auto path = toPath(config);
		auto* task = getTask(path);

		auto configPath = toPath(task->config);
		if (normalizedPaths.count(configPath)) {
			task->resetConfig(this, path);
			needsConfigUpdate->store(true);
			needsUpdate->store(true);
			continue;
		}

		if (task->resolved == nullptr) {
			continue;
		}

		bool configChanged = false;
		for (auto& file : task->resolved->ExtendedSourceFiles()) {
			auto fp = toPath(file);
			if (normalizedPaths.count(fp)) {
				task->resetConfig(this, path);
				needsConfigUpdate->store(true);
				needsUpdate->store(true);
				configChanged = true;
				break;
			}
		}
		if (configChanged) {
			continue;
		}
		for (auto* mapper : task->resolved->ContentMappers()) {
			if (mapper->PackageDirectory.empty() ||
			    !mapper->ContributionID.empty()) {
				continue;
			}
			auto manifestPath = toPath(
			    tspath::combinePaths(mapper->PackageDirectory,
			                         {"package.json"}));
			if (normalizedPaths.count(manifestPath)) {
				task->resetConfig(this, path);
				needsConfigUpdate->store(true);
				needsUpdate->store(true);
				configChanged = true;
				break;
			}
		}
		if (configChanged) {
			continue;
		}

		bool rootChanged = false;
		if (task->contentMapperProject != nullptr) {
			auto [watchedFiles, err] =
			    task->contentMapperProject->WatchedFiles();
			if (err != nullptr) {
				task->contentMapperProjectErr = err;
				task->resetStatus();
				needsUpdate->store(true);
				rootChanged = true;
			}
			for (auto& fileName : watchedFiles) {
				if (normalizedPaths.count(toPath(fileName))) {
					task->refreshContentMapperProject(this);
					task->resetStatus();
					needsUpdate->store(true);
					rootChanged = true;
					break;
				}
			}
		}
		auto fileNames = task->resolved->FileNames();
		collections::Set<tspath::Path> roots;
		for (auto& file : fileNames) {
			auto fp = toPath(file);
			roots.Add(fp);
			if (!rootChanged) {
				if (normalizedPaths.count(fp)) {
					task->resetStatus();
					needsUpdate->store(true);
					rootChanged = true;
				}
			}
		}

		if (!rootChanged) {
			{
				std::lock_guard<std::mutex> lock(task->buildInfoEntryMu);
				auto* bi = task->buildInfoEntry_;
				if (bi != nullptr && bi->buildInfo != nullptr) {
					auto buildInfoDir = tspath::getDirectoryPath(
					    std::string(bi->path));
					for (auto& fileName : bi->buildInfo->FileNames) {
						auto fp = toPath(resolveBuildInfoFileName(
						    fileName, buildInfoDir));
						if (roots.Has(fp)) {
							continue;
						}
						if (normalizedPaths.count(fp)) {
							task->resetStatus();
							needsUpdate->store(true);
							break;
						}
					}
					bi->buildInfo->GetPackageJsons(
					    buildInfoDir,
					    [&](const std::string& packageJson) {
						    if (packageJsonLookupChanged(
						            packageJson, normalizedPaths)) {
							    task->resetStatus();
							    needsUpdate->store(true);
							    return false;
						    }
						    return true;
					    });
					bi->buildInfo->GetMissingPackageJsons(
					    buildInfoDir,
					    [&](const std::string& packageJson) {
						    if (packageJsonLookupChanged(
						            packageJson, normalizedPaths)) {
							    task->resetStatus();
							    needsUpdate->store(true);
							    return false;
						    }
						    return true;
					    });
				}
			}
			for (auto& packageJson : task->packageJsons) {
				if (packageJsonLookupChanged(packageJson,
				                             normalizedPaths)) {
					task->resetStatus();
					needsUpdate->store(true);
					break;
				}
			}
		}

		task->built = std::make_shared<closeChan>();
		task->done = std::make_shared<closeChan>();

		auto* newConfig =
		    task->resolved->ReloadFileNamesOfParsedCommandLine(
		        host_->host_);
		if (task->resolved->FileNames() != newConfig->FileNames()) {
			host_->resolvedReferences.store(path, newConfig);
			task->resolved = newConfig;
			task->resetStatus();
			needsUpdate->store(true);
		}
	}

	if (!needsUpdate->load()) {
		auto opts = comparePathsOptions;
		for (auto& [eventPath, _] : changedPaths) {
			if (host_->host_->fs->DirectoryExists(eventPath)) {
				if (wm->IsPathUnderWatch(eventPath, opts)) {
					rangeTask([&](const tspath::Path& path,
					              BuildTask* task) {
						task->resetStatus();
						task->built = std::make_shared<closeChan>();
						task->done = std::make_shared<closeChan>();
					});
					needsUpdate->store(true);
					break;
				}
			}
		}
	}
}

// orchestrator.go:672 packageJsonLookupChanged.
bool Orchestrator::packageJsonLookupChanged(
    const std::string& packageJson,
    const std::unordered_map<tspath::Path, fswatch::EventKind>& changedPaths) {
	auto packageJsonPath = toPath(packageJson);
	if (changedPaths.count(packageJsonPath)) {
		return true;
	}
	for (auto& [changedPath, kind] : changedPaths) {
		if (kind == fswatch::EventKind::EventDelete &&
		    tspath::containsPath(std::string(changedPath),
		                         std::string(packageJsonPath),
		                         comparePathsOptions)) {
			return true;
		}
	}
	return false;
}

// orchestrator.go:684 computeDesiredWatches.
std::unordered_map<std::string, bool> Orchestrator::computeDesiredWatches() {
	watchmanager::DirWatchSet desiredDirsObj(comparePathsOptions);
	auto* desiredDirs = &desiredDirsObj;

	for (auto& config : order) {
		auto path = toPath(config);
		auto* task = getTask(path);

		// Watch config file directory
		auto configDir = tspath::getDirectoryPath(task->config);
		auto realConfigDir = host_->host_->fs->Realpath(configDir);
		desiredDirs->Set(realConfigDir, false);

		if (task->resolved == nullptr) {
			continue;
		}

		// Extended config file directories
		for (auto& cfgPath : task->resolved->ExtendedSourceFiles()) {
			auto realPath = host_->host_->fs->Realpath(cfgPath);
			auto dir = tspath::getDirectoryPath(realPath);
			desiredDirs->Set(dir, false);
		}

		// Wildcard directories from tsconfig
		for (auto& [dir, recursive] :
		     *task->resolved->WildcardDirectories()) {
			auto realDir = host_->host_->fs->Realpath(dir);
			desiredDirs->Set(realDir, recursive);
		}

		// Input file directories not already covered
		for (auto& fileName : task->resolved->FileNames()) {
			auto absPath = tspath::getNormalizedAbsolutePath(
			    fileName, opts.Sys->GetCurrentDirectory());
			addProgramFileWatchDir(desiredDirs,
			                       tspath::getDirectoryPath(absPath));
		}
		for (auto* mapper : task->resolved->ContentMappers()) {
			if (mapper->PackageDirectory.empty() ||
			    !mapper->ContributionID.empty()) {
				continue;
			}
			auto manifestPath = tspath::combinePaths(
			    mapper->PackageDirectory, {"package.json"});
			auto dir = tspath::getDirectoryPath(manifestPath);
			if (!desiredDirs->Covered(dir) &&
			    watchmanager::CanWatchDirectory(dir)) {
				desiredDirs->Set(dir, false);
			}
		}

		if (task->contentMapperProject != nullptr) {
			auto [watchedFiles, err] =
			    task->contentMapperProject->WatchedFiles();
			if (err != nullptr) {
				task->contentMapperProjectErr = err;
			}
			for (auto& fileName : watchedFiles) {
				auto absPath = host_->host_->fs->Realpath(fileName);
				auto dir = tspath::getDirectoryPath(absPath);
				if (!desiredDirs->Covered(dir) &&
				    watchmanager::CanWatchDirectory(dir)) {
					desiredDirs->Set(dir, false);
				}
			}
		}

		// Non-root dependency directories from buildinfo (e.g. node_modules
		// .d.ts files).
		buildInfoEntry* bi;
		{
			std::lock_guard<std::mutex> lock(task->buildInfoEntryMu);
			bi = task->buildInfoEntry_;
		}
		if (bi != nullptr && bi->buildInfo != nullptr) {
			auto buildInfoDir =
			    tspath::getDirectoryPath(std::string(bi->path));
			collections::Set<tspath::Path> roots;
			for (auto& fileName : task->resolved->FileNames()) {
				roots.Add(toPath(fileName));
			}
			for (auto& fileName : bi->buildInfo->FileNames) {
				auto absPath = host_->host_->fs->Realpath(
				    resolveBuildInfoFileName(fileName, buildInfoDir));
				auto fp = toPath(absPath);
				if (roots.Has(fp)) {
					continue;
				}
				addProgramFileWatchDir(desiredDirs,
				                       tspath::getDirectoryPath(absPath));
			}
			bi->buildInfo->GetPackageJsons(
			    buildInfoDir, [&](const std::string& packageJson) {
				    addPackageJsonWatchDirs(desiredDirs, packageJson);
				    return true;
			    });
			bi->buildInfo->GetMissingPackageJsons(
			    buildInfoDir, [&](const std::string& packageJson) {
				    addPackageJsonWatchDirs(desiredDirs, packageJson);
				    return true;
			    });
		}
		for (auto& packageJson : task->packageJsons) {
			addPackageJsonWatchDirs(desiredDirs, packageJson);
		}
	}

	return wm->ResolveDesiredDirs(desiredDirs->Dirs());
}

// orchestrator.go:780 addWatchDir.
void Orchestrator::addWatchDir(watchmanager::DirWatchSet* desiredDirs,
                               const std::string& dir) {
	if (!desiredDirs->Covered(dir) && watchmanager::CanWatchDirectory(dir)) {
		desiredDirs->Set(dir, false);
	}
}

// orchestrator.go:786 addPackageJsonWatchDirs.
void Orchestrator::addPackageJsonWatchDirs(
    watchmanager::DirWatchSet* desiredDirs, const std::string& packageJson) {
	auto dir = tspath::getDirectoryPath(packageJson);
	std::vector<std::string> dirs{dir};
	bool foundNodeModules = false;
	for (std::string current = dir;;) {
		auto parent = tspath::getDirectoryPath(current);
		if (parent.empty() || parent == current) {
			break;
		}
		dirs.push_back(parent);
		if (tspath::getBaseFileName(parent) == "node_modules") {
			foundNodeModules = true;
			if (auto grandparent = tspath::getDirectoryPath(parent);
			    !grandparent.empty() && grandparent != parent) {
				dirs.push_back(grandparent);
			}
			break;
		}
		current = parent;
	}

	if (!foundNodeModules) {
		addWatchDir(desiredDirs, dir);
		return;
	}
	for (auto& dir : dirs) {
		addWatchDir(desiredDirs, dir);
	}
}

// orchestrator.go:819 DoCycle.
void Orchestrator::DoCycle() {
	wm->Lock();
	struct unlocker {
		~unlocker() { wm->Unlock(); }
		watchmanager::WatchManager* wm;
	} unlocker{wm};

	auto [changedPaths, overflow] = wm->DrainEvents();
	bool hasEvents = !changedPaths.empty() || overflow;

	if (!hasEvents) {
		if (wm->DebugLog != nullptr) {
			*wm->DebugLog << "[watch] DoCycle: no events, skipping\n";
		}
		return;
	}

	std::atomic<bool> needsConfigUpdate{false};
	std::atomic<bool> needsUpdate{false};

	if (overflow) {
		// Overflow: reset all tasks to force a full rebuild.
		rangeTask([&](const tspath::Path& path, BuildTask* task) {
			task->resetConfig(this, path);
			task->built = std::make_shared<closeChan>();
			task->done = std::make_shared<closeChan>();
		});
		needsConfigUpdate.store(true);
		needsUpdate.store(true);
	} else {
		// Event-driven: check only tasks affected by changed paths
		checkTasksForEventChanges(changedPaths, &needsConfigUpdate,
		                          &needsUpdate);
	}

	if (!needsUpdate.load()) {
		resetCaches();
		return;
	}

	watchStatusReporter(tsoptions::newCompilerDiagnostic(
	    File_change_detected_Starting_incremental_compilation, {}));
	if (needsConfigUpdate.load()) {
		// Generate new tasks
		GenerateGraphReusingOldTasks();
	}

	buildOrClean();
	updateWatch();
	auto desiredDirs = computeDesiredWatches();
	if (auto err = wm->ReconcileWatches(desiredDirs); err) {
		*opts.Sys->Writer() << err->Error() << "\n";
		// Mark overflow so the next event triggers a full rebuild
		wm->ForceOverflow();
	}
	resetCaches();
}

// orchestrator.go:866 buildOrClean.
etsc::CommandLineResult Orchestrator::buildOrClean() {
	return buildOrCleanOrder(order)->Result;
}

// orchestrator.go:870 buildOrCleanOrder.
OrchestratorResult*
Orchestrator::buildOrCleanOrder(const std::vector<std::string>& order) {
	if (!tristateIsTrue(opts.Command->BuildOptions->Clean) &&
	    tristateIsTrue(opts.Command->BuildOptions->Verbose)) {
		std::vector<std::string> parts;
		for (auto& p : order) {
			parts.push_back("\r\n    * " + relativeFileName(p));
		}
		createBuilderStatusReporter(nullptr)(
		    tsoptions::newCompilerDiagnostic(
		        Projects_in_this_build_Colon_0, {join(parts, "")}));
	}
	auto* buildResult = new OrchestratorResult{};
	if (errors.empty()) {
		buildResult->Statistics.Projects =
		    static_cast<int>(order.size());
		// Builders pick up projects in scheduleOrder; results are reported
		// in Order(), waiting for each project to finish
		auto reported = std::make_shared<closeChan>();
		std::thread([this, &order, buildResult, reported]() {
			struct closer {
				~closer() { ch->close(); }
				std::shared_ptr<closeChan> ch;
			} c{reported};
			for (auto& config : order) {
				auto path = toPath(config);
				auto* task = getTask(path);
				task->built->wait();
				task->report(this, path, buildResult);
			}
		}).detach();
		rangeTasks(order, [&](const tspath::Path& path, BuildTask* task) {
			buildOrCleanProject(task, path);
		});
		reported->wait();
	} else {
		// Circularity errors prevent any project from being built
		buildResult->Result.Status =
		    etsc::ExitStatusProjectReferenceCycle_OutputsSkipped;
		auto reportDiagnostic = createDiagnosticReporter(nullptr);
		for (auto* err : errors) {
			reportDiagnostic(err);
		}
		buildResult->Errors = errors;
	}
	buildResult->report(this);
	return buildResult;
}

// orchestrator.go:911 rangeTask.
void Orchestrator::rangeTask(
    const std::function<void(const tspath::Path&, BuildTask*)>& f) {
	rangeTasks(order, f);
}

// orchestrator.go:915 rangeTasks.
void Orchestrator::rangeTasks(
    const std::vector<std::string>& order,
    const std::function<void(const tspath::Path&, BuildTask*)>& f) {
	int64_t numRoutines = 4;
	if (tristateIsTrue(opts.Command->CompilerOptions->SingleThreaded)) {
		numRoutines = 1;
	} else if (auto* builders = opts.Command->BuildOptions->Builders;
	           builders != nullptr) {
		numRoutines = *builders;
	}

	std::atomic<int64_t> currentTaskIndex{0};
	auto getNextTask = [&]() -> std::tuple<tspath::Path, BuildTask*, bool> {
		auto index = currentTaskIndex.fetch_add(1);
		if (index >= static_cast<int64_t>(order.size())) {
			return {"", nullptr, false};
		}
		auto config = order[index];
		auto path = toPath(config);
		auto* task = getTask(path);
		return {path, task, true};
	};
	auto runTask = [&]() {
		for (;;) {
			auto [path, task, ok] = getNextTask();
			if (!ok) {
				break;
			}
			f(path, task);
		}
	};

	if (numRoutines == 1) {
		runTask();
	} else {
		auto* wg = newWorkGroup(false);
		for (int64_t i = 0; i < numRoutines; i++) {
			wg->Queue(runTask);
		}
		wg->RunAndWait();
	}
}

// orchestrator.go:944 buildOrCleanProject.
void Orchestrator::buildOrCleanProject(BuildTask* task,
                                       const tspath::Path& path) {
	task->result = new taskResult();
	task->result->reportStatus = createBuilderStatusReporter(task);
	task->result->diagnosticReporter = createDiagnosticReporter(task);
	if (!tristateIsTrue(opts.Command->BuildOptions->Clean)) {
		task->buildProject(this, path);
	} else {
		task->cleanProject(this, path);
	}
	if (opts.Testing == nullptr) {
		// The program is only needed by Testing.OnProgram at report time;
		// drop it now so a task that has finished but is not yet reported
		// does not keep its program alive.
		task->result->program = nullptr;
	}
	task->built->close();
}

// orchestrator.go:958 getWriter.
std::ostream* Orchestrator::getWriter(BuildTask* task) {
	if (task == nullptr) {
		return opts.Sys->Writer();
	}
	return &task->result->builder;
}

// orchestrator.go:965 createBuilderStatusReporter.
etsc::DiagnosticReporter
Orchestrator::createBuilderStatusReporter(BuildTask* task) {
	return etsc::CreateBuilderStatusReporter(
	    opts.Sys, getWriter(task), opts.Command->Locale(),
	    opts.Command->CompilerOptions, opts.Testing);
}

// orchestrator.go:969 createDiagnosticReporter.
etsc::DiagnosticReporter
Orchestrator::createDiagnosticReporter(BuildTask* task) {
	return etsc::CreateDiagnosticReporter(opts.Sys, getWriter(task),
	                                      opts.Command->Locale(),
	                                      opts.Command->CompilerOptions);
}

// orchestrator.go:991 NewOrchestrator.
Orchestrator* NewOrchestrator(Options opts) {
	auto* wm = new watchmanager::WatchManager(
	    opts.Sys->Writer(),
	    [fs = opts.Sys->fs()](const std::string& d) {
		    return fs->DirectoryExists(d);
	    });
	auto* orchestrator = new Orchestrator();
	orchestrator->opts = opts;
	orchestrator->comparePathsOptions = tspath::ComparePathsOptions{
	    opts.Sys->fs()->UseCaseSensitiveFileNames(),
	    opts.Sys->GetCurrentDirectory()};
	orchestrator->tasks =
	    new collections::SyncMap<tspath::Path, BuildTask*>();
	orchestrator->wm = wm;
	auto* h = new host();
	h->orchestrator = orchestrator;
	// compiler.NewCachedFSCompilerHost — NewCompilerHost with the fs
	// wrapped in vfs::cachedvfs::From.
	auto* inner = new compiler::CompilerHost();
	inner->currentDirectory = opts.Sys->GetCurrentDirectory();
	inner->fs = vfs::cachedvfs::From(opts.Sys->fs().get());
	inner->defaultLibraryPath = opts.Sys->DefaultLibraryPath();
	h->host_ = inner;
	h->mTimes = new collections::SyncMap<tspath::Path, fileTime>();
	orchestrator->host_ = h;
	if (tristateIsTrue(opts.Command->CompilerOptions->Watch)) {
		orchestrator->watchStatusReporter = etsc::CreateWatchStatusReporter(
		    opts.Sys, opts.Command->Locale(),
		    opts.Command->CompilerOptions, opts.Testing);
		if (auto* t = dynamic_cast<
		        watchmanager::CommandLineTestingWithWatchBackend*>(
		        opts.Testing);
		    t != nullptr) {
			wm->SetBackend(t->WatchBackend());
		}
	} else {
		orchestrator->errorSummaryReporter =
		    etsc::CreateReportErrorSummary(opts.Sys, opts.Command->Locale(),
		                                   opts.Command->CompilerOptions);
	}
	return orchestrator;
}

} // namespace tsc::execute::build
