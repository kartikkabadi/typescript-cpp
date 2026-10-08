// projecttestutil.cpp — port of tsc/internal/testutil/projecttestutil/
// projecttestutil.go.
#include "internal/testutil/projecttestutil/projecttestutil.h"

#include <algorithm>
#include <filesystem>
#include <mutex>

#include "internal/ast/ast.h" // TSC_UNREACHABLE
#include "internal/bundled/bundled.h"
#include "internal/core/context.h"
#include "internal/glob/glob.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/vfs/osvfs/osvfs.h"

namespace tsc::testutil::projecttestutil {

namespace iovfs = ::tsc::vfs::iovfs;
namespace vfstest = ::tsc::vfs::vfstest;

// SetupNpmExecutorForTypingsInstaller — projecttestutil.go:59.
void SessionUtils::SetupNpmExecutorForTypingsInstaller() {
	if (!tiOptions) {
		return;
	}

	// The Go closure captures h (the SessionUtils) so GC keeps fs and
	// tiOptions alive while the session can invoke the mock. Capture
	// the shared_ptr members by value — `this` itself may be gone by
	// the time a background ATA task calls NpmInstall.
	npmExecutor->NpmInstallFunc =
	    [fs = this->fs,
	     tiOptions = this->tiOptions](const std::string& cwd,
	           const std::vector<std::string>& packageNames)
	    -> std::pair<std::string, gostd::Error> {
		// packageNames is actually npmInstallArgs due to interface
		// misnaming
		const auto& npmInstallArgs = packageNames;
		size_t lenNpmInstallArgs = npmInstallArgs.size();
		if (lenNpmInstallArgs < 3) {
			// fmt's %v of a []string renders [a b c].
			std::string argsList = "[";
			for (size_t i = 0; i < npmInstallArgs.size(); i++) {
				if (i) argsList += " ";
				argsList += npmInstallArgs[i];
			}
			argsList += "]";
			return {{},
			        gostd::errorf("unexpected npm install: %s %v",
			                      {cwd, argsList})};
		}

		if (lenNpmInstallArgs == 3 &&
		    npmInstallArgs[2] == "types-registry@latest") {
			// Write typings file
			vfs::Error werr = fs->WriteFile(
			    cwd + "/node_modules/types-registry/index.json",
			    createTypesRegistryFileContent(tiOptions));
			return {{},
			        werr ? gostd::newError(werr.str()) : nullptr};
		}

		// Find the packages: they start at index 2 and continue until we
		// hit a flag starting with --
		size_t packageEnd = lenNpmInstallArgs;
		for (size_t i = 2; i < lenNpmInstallArgs; i++) {
			if (npmInstallArgs[i].starts_with("--")) {
				packageEnd = i;
				break;
			}
		}

		for (size_t pi = 2; pi < packageEnd; pi++) {
			const std::string& atTypesPackageTs = npmInstallArgs[pi];
			// @types/packageName@TsVersionToUse
			std::string atTypesPackage = atTypesPackageTs;
			// Remove version suffix
			size_t versionIndex = atTypesPackage.rfind('@');
			if (versionIndex != std::string::npos &&
			    versionIndex > 6) { // "@types/".length is 7, so version @ must be after
				atTypesPackage = atTypesPackage.substr(0, versionIndex);
			}
			// Extract package name from @types/packageName
			std::string packageBaseName =
			    atTypesPackage.substr(7); // Remove "@types/" prefix
			auto it = tiOptions->PackageToFile.find(packageBaseName);
			if (it == tiOptions->PackageToFile.end()) {
				return {{},
				        gostd::errorf("content not provided for %s",
				                      {packageBaseName})};
			}
			vfs::Error werr = fs->WriteFile(
			    cwd + "/node_modules/@types/" + packageBaseName +
			        "/index.d.ts",
			    it->second);
			if (werr) {
				return {{}, gostd::newError(werr.str())};
			}
		}
		return {{}, nullptr};
	};
}

// ToPath — projecttestutil.go:109.
tspath::Path SessionUtils::ToPath(const std::string& fileName) {
	return tspath::toPath(fileName, currentDirectory,
	                      fs->UseCaseSensitiveFileNames());
}

// WatchesFile — projecttestutil.go:121.
bool SessionUtils::WatchesFile(const std::string& filePath) {
	for (const auto& call : client->WatchFilesCalls()) {
		for (const auto& watcher : call.Watchers) {
			if (watcher->GlobPattern.Pattern) {
				auto [g, err] =
				    glob::parse(*watcher->GlobPattern.Pattern);
				if (err.empty() && g && g->match(filePath)) {
					return true;
				}
			} else if (watcher->GlobPattern.RelativePattern) {
				auto* rp = watcher->GlobPattern.RelativePattern.get();
				std::string baseUri = *rp->BaseUri.URI;
				// Convert base URI (e.g. "file:///home/projects") to a
				// directory path with trailing separator for proper
				// prefix matching on path boundaries.
				std::string baseDir =
				    lsproto::documentUriFileName(baseUri);
				baseDir =
				    tspath::ensureTrailingDirectorySeparator(baseDir);
				if (filePath.starts_with(baseDir)) {
					std::string_view relativePath =
					    std::string_view(filePath).substr(
					        baseDir.size());
					auto [g, err] = glob::parse(rp->Pattern);
					if (err.empty() && g &&
					    g->match(relativePath)) {
						return true;
					}
				}
			}
		}
	}
	return false;
}

// BaselineLogs — projecttestutil.go:151.
void SessionUtils::BaselineLogs(gostd::testing::T* t) {
	baseline::Run(t, t->Name() + ".log", Logs(),
	              baseline::Options{.Subfolder = "project"});
}

namespace {

// TypesRegistryConfigText backing state — sync.Once + string
// (projecttestutil.go:157-160).
std::once_flag typesRegistryConfigTextOnce;
std::string typesRegistryConfigText;

// TypesRegistryConfig backing state — sync.Once + map
// (projecttestutil.go:177-180).
std::once_flag typesRegistryConfigOnce;
std::unordered_map<std::string, std::string> typesRegistryConfig;

}  // namespace

// TypesRegistryConfig — projecttestutil.go:182.
const std::unordered_map<std::string, std::string>& TypesRegistryConfig() {
	std::call_once(typesRegistryConfigOnce, [] {
		typesRegistryConfig = {
		    {"latest", "1.3.0"},
		    {"ts2.0", "1.0.0"},
		    {"ts2.1", "1.0.0"},
		    {"ts2.2", "1.2.0"},
		    {"ts2.3", "1.3.0"},
		    {"ts2.4", "1.3.0"},
		    {"ts2.5", "1.3.0"},
		    {"ts2.6", "1.3.0"},
		    {"ts2.7", "1.3.0"},
		};
	});
	return typesRegistryConfig;
}

// TypesRegistryConfigText — projecttestutil.go:162.
std::string TypesRegistryConfigText() {
	std::call_once(typesRegistryConfigTextOnce, [] {
		std::string result;
		for (const auto& [key, value] : TypesRegistryConfig()) {
			if (!result.empty()) {
				result += ",";
			}
			result += gostd::sprintf("\n      \"%s\": \"%s\"",
			                         {key, value});
		}
		typesRegistryConfigText = std::move(result);
	});
	return typesRegistryConfigText;
}

// createTypesRegistryFileContent — projecttestutil.go:199.
std::string SessionUtils::createTypesRegistryFileContent(
    const std::shared_ptr<TypingsInstallerOptions>& tiOptions) {
	std::string builder;
	builder += "{\n  \"entries\": {";
	int index = 0;
	for (const auto& entry : tiOptions->TypesRegistry) {
		appendTypesRegistryConfig(&builder, index, entry);
		index++;
	}
	index = (int)tiOptions->TypesRegistry.size();
	for (const auto& [key, _] : tiOptions->PackageToFile) {
		if (std::find(tiOptions->TypesRegistry.begin(),
		              tiOptions->TypesRegistry.end(),
		              key) == tiOptions->TypesRegistry.end()) {
			appendTypesRegistryConfig(&builder, index, key);
			index++;
		}
	}
	builder += "\n  }\n}";
	return builder;
}

// appendTypesRegistryConfig — projecttestutil.go:216.
void SessionUtils::appendTypesRegistryConfig(std::string* builder,
                                             int index,
                                             const std::string& entry) {
	if (index > 0) {
		*builder += ",";
	}
	*builder += gostd::sprintf("\n    \"%s\": {%s\n    }",
	                           {entry, TypesRegistryConfigText()});
}

// Setup — projecttestutil.go:223.
std::pair<project::Session*, std::shared_ptr<SessionUtils>>
Setup(const FileMap& files) {
	return SetupWithTypingsInstaller(
	    files, std::make_shared<TypingsInstallerOptions>());
}

// SetupWithRealFS — projecttestutil.go:227.
std::pair<project::Session*, std::shared_ptr<SessionUtils>>
SetupWithRealFS() {
	std::shared_ptr<vfs::FS> fs = bundled::WrapFS(
	    std::shared_ptr<vfs::FS>(vfs::osvfs::FS(), [](vfs::FS*) {}));
	auto clientMock = std::make_shared<ClientMock>();
	auto npmExecutorMock = std::make_shared<NpmExecutorMock>();
	std::error_code ec;
	auto wdPath = std::filesystem::current_path(ec);
	if (ec) {
		TSC_UNREACHABLE("os.Getwd failed");
	}
	std::string wd = wdPath.string();

	auto sessionUtils = std::make_shared<SessionUtils>();
	sessionUtils->currentDirectory = wd;
	sessionUtils->fs = fs;
	sessionUtils->client = clientMock;
	sessionUtils->npmExecutor = npmExecutorMock;
	sessionUtils->logger =
	    std::shared_ptr<logging::LogCollector>(logging::newTestLogger());

	project::SessionInit init;
	init.BackgroundCtx = gostd::contextBackground();
	init.FS = fs;
	init.Client = clientMock.get();
	init.NpmExecutor = npmExecutorMock.get();
	init.Logger = sessionUtils->logger.get();
	init.KeepAlive = {fs, clientMock, npmExecutorMock,
	                  sessionUtils->logger};
	project::SessionOptions options;
	options.CurrentDirectory = wd;
	options.DefaultLibraryPath = bundled::LibPath();
	options.PositionEncoding = lsproto::PositionEncodingKindUTF8;
	options.WatchEnabled = true;
	options.LoggingEnabled = true;
	options.PushDiagnosticsEnabled = true;
	init.Options = new project::SessionOptions(options);

	return {project::NewSession(&init), sessionUtils};
}

// SetupWithOptions — projecttestutil.go:261.
std::pair<project::Session*, std::shared_ptr<SessionUtils>>
SetupWithOptions(const FileMap& files, project::SessionOptions* options) {
	return SetupWithOptionsAndTypingsInstaller(
	    files, options, std::make_shared<TypingsInstallerOptions>());
}

// SetupWithTypingsInstaller — projecttestutil.go:265.
std::pair<project::Session*, std::shared_ptr<SessionUtils>>
SetupWithTypingsInstaller(
    const FileMap& files,
    std::shared_ptr<TypingsInstallerOptions> tiOptions) {
	return SetupWithOptionsAndTypingsInstaller(files, nullptr,
	                                           std::move(tiOptions));
}

// SetupWithOptionsAndTypingsInstaller — projecttestutil.go:269.
std::pair<project::Session*, std::shared_ptr<SessionUtils>>
SetupWithOptionsAndTypingsInstaller(
    const FileMap& files, project::SessionOptions* options,
    std::shared_ptr<TypingsInstallerOptions> tiOptions) {
	auto [init, sessionUtils] = GetSessionInitOptions(
	    files, options, std::move(tiOptions));
	project::Session* session = project::NewSession(init.get());
	return {session, sessionUtils};
}

// WithRequestID — projecttestutil.go:276.
gostd::Context WithRequestID(const gostd::Context& ctx) {
	return tsc::core::WithRequestID(ctx, "0");
}

// GetSessionInitOptions — projecttestutil.go:280.
std::pair<std::unique_ptr<project::SessionInit>,
          std::shared_ptr<SessionUtils>>
GetSessionInitOptions(const FileMap& files, project::SessionOptions* options,
                      std::shared_ptr<TypingsInstallerOptions> tiOptions) {
	std::shared_ptr<vfs::FS> fsFromFileMap =
	    vfstest::FromMap(files, false /*useCaseSensitiveFileNames*/);
	std::shared_ptr<vfs::FS> fs = bundled::WrapFS(fsFromFileMap);
	auto clientMock = std::make_shared<ClientMock>();
	auto npmExecutorMock = std::make_shared<NpmExecutorMock>();
	auto sessionUtils = std::make_shared<SessionUtils>();
	sessionUtils->currentDirectory = "/";
	// fsFromFileMap.(iovfs.FsWithSys)
	sessionUtils->fsFromFileMap =
	    std::dynamic_pointer_cast<iovfs::FsWithSys>(fsFromFileMap);
	sessionUtils->fs = fs;
	sessionUtils->client = clientMock;
	sessionUtils->npmExecutor = npmExecutorMock;
	sessionUtils->tiOptions = std::move(tiOptions);
	sessionUtils->logger =
	    std::shared_ptr<logging::LogCollector>(logging::newTestLogger());

	// Configure the npm executor mock to handle typings installation
	sessionUtils->SetupNpmExecutorForTypingsInstaller();

	// Use provided options or create default ones
	project::SessionOptions defaultOptions;
	if (options == nullptr) {
		defaultOptions.CurrentDirectory = "/";
		defaultOptions.DefaultLibraryPath = bundled::LibPath();
		defaultOptions.TypingsLocation = std::string(TestTypingsLocation);
		defaultOptions.PositionEncoding = lsproto::PositionEncodingKindUTF8;
		defaultOptions.WatchEnabled = true;
		defaultOptions.LoggingEnabled = true;
		defaultOptions.PushDiagnosticsEnabled = true;
	}

	auto init = std::make_unique<project::SessionInit>();
	init->BackgroundCtx = gostd::contextBackground();
	// GC lifetime: Go callers pass a `*SessionOptions` that the
	// collector keeps alive for the session's (background-work
	// included) lifetime. Copy it into a heap object the session
	// owns so a caller's stack frame can't be reused while queued
	// tasks still read it.
	std::shared_ptr<project::SessionOptions> optionsOwner(
	    new project::SessionOptions(
	        options != nullptr ? *options : defaultOptions));
	init->Options = optionsOwner.get();
	init->FS = fs;
	init->Client = clientMock.get();
	init->NpmExecutor = npmExecutorMock.get();
	init->Logger = sessionUtils->logger.get();
	init->KeepAlive = {fs, clientMock, npmExecutorMock,
	                   sessionUtils->logger, sessionUtils->tiOptions,
	                   std::static_pointer_cast<void>(optionsOwner)};
	return {std::move(init), sessionUtils};
}

}  // namespace tsc::testutil::projecttestutil
