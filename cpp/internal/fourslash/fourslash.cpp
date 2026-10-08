// === slice: fourslash ===
// fourslash.cpp — fourslash.go port. Bodies follow the Go source in file
// order with `// fourslash.go:<line>` anchors.
#include "internal/fourslash/fourslash.h"

#include <algorithm>
#include <filesystem>
#include <set>

#include "internal/bundled/bundled.h"
#include "internal/vfs/iovfs/iovfs.h"

namespace tsc::fourslash {

// lspAnyOf — marshal a value to JSON then decode into LSPAny (the Go
// `map[string]any{"js/ts": config}` idiom).
template <typename T>
lsproto::LSPAny lspAnyOf(const T& v) {
	auto [js, err] = json::marshal(v);
	if (!err.empty()) {
		TSC_UNREACHABLE(("json marshal failed in lspAnyOf: " +
		                 err).c_str());
	}
	json::Decoder dec(js);
	lsproto::LSPAny any;
	any.unmarshalJSONFrom(dec);
	return any;
}

// ===========================================================================
// fourslash.go:75-145 — scriptInfo / newScriptInfo / testConverters
// ===========================================================================

// newTestConverters — fourslash.go:86.
testConverters::testConverters(
    lsproto::PositionEncodingKind enc,
    std::function<lsconv::LSPLineMap*(const std::string&)> getLineMap)
    : positionEncoding_(std::move(enc)),
      getLineMap_(std::move(getLineMap)) {
	// The embedded Converters is kept for struct fidelity but all its
	// generic bodies are dep-stubbed; the position conversions below are
	// the real implementations.
	Converters = lsconv::NewConverters(positionEncoding_, getLineMap_);
}

std::shared_ptr<testConverters> newTestConverters(
    lsproto::PositionEncodingKind encoding,
    std::function<lsconv::LSPLineMap*(const std::string&)> getLineMap) {
	return std::make_shared<testConverters>(std::move(encoding),
	                                        std::move(getLineMap));
}

// newScriptInfo — fourslash.go:107.
std::shared_ptr<scriptInfo> newScriptInfo(const std::string& fileName,
                                          const std::string& content) {
	auto s = std::make_shared<scriptInfo>();
	s->fileName = fileName;
	s->content = content;
	s->lineMap = lsconv::ComputeLSPLineStarts(content);
	s->version = 1;
	return s;
}

// editContent — fourslash.go:117.
void scriptInfo::editContent(const TextChange& change) {
	content = change.ApplyTo(content);
	lineMap = lsconv::ComputeLSPLineStarts(content);
	version++;
}

// GetLineContent — fourslash.go:134.
std::string scriptInfo::GetLineContent(int line) const {
	int numLines = (int)lineMap->LineStarts.size();
	if (line < 0 || line >= numLines) {
		return "";
	}
	TextPos start = lineMap->LineStarts[line];
	TextPos end;
	if (line + 1 < numLines) {
		end = lineMap->LineStarts[line + 1];
	} else {
		end = (TextPos)content.size();
	}
	std::string slice = content.substr((size_t)start, (size_t)(end - start));
	// strings.TrimRight(s, "\r\n")
	size_t last = slice.find_last_not_of("\r\n");
	if (last == std::string::npos) {
		return "";
	}
	return slice.substr(0, last + 1);
}

// ===========================================================================
// fourslash.go:154-285 — parseCache / NewFourslash(WithOptions) /
// newFourslash
// ===========================================================================

// parseCache — fourslash.go:154.
std::shared_ptr<project::ParseCache> parseCache() {
	static project::RefCountCacheOptions options{
	    .DisableDeletion = true,
	};
	static std::shared_ptr<project::ParseCache> cache{
	    project::newParseCache(options)};
	return cache;
}

// NewFourslash — fourslash.go:160. Go derives testPath via runtime.Caller(1);
// the value only reaches getBaselineOptions, which ignores it, so callers
// pass "" (or the test source path for debugging).
std::pair<std::shared_ptr<FourslashTest>, std::function<void()>> NewFourslash(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::ClientCapabilities>& capabilities,
    const std::string& content) {
	return newFourslash(t, content,
	                    std::make_shared<FourslashOptions>(FourslashOptions{
	                        .Capabilities = capabilities,
	                    }),
	                    "");
}

// NewFourslashWithOptions — fourslash.go:171.
std::pair<std::shared_ptr<FourslashTest>, std::function<void()>>
NewFourslashWithOptions(gostd::testing::T* t, const std::string& content,
                        const FourslashOptions* options) {
	return newFourslash(t, content,
	                    options ? std::make_shared<FourslashOptions>(*options)
	                            : nullptr,
	                    "");
}

// assertEqual / assertCheck — gotest.tools/v3/assert equivalents used
// throughout fourslash.go (assert.Equal marks the test failed and
// continues).
template <typename T, typename U>
static void assertEqual(gostd::testing::T* t, const T& actual,
                        const U& expected, const std::string& msg = {}) {
	if (!(actual == expected)) {
		if (!msg.empty()) {
			t->Errorf("assertion failed: %v (actual) != %v (expected). %s",
			          {actual, expected, msg});
		} else {
			t->Errorf("assertion failed: %v (actual) != %v (expected)",
			          {actual, expected});
		}
	}
}
static void assertCheck(gostd::testing::T* t, bool cond,
                        const std::string& msg) {
	if (!cond) {
		t->Errorf("assertion failed: %s", {msg});
	}
}

// lowerFirstChar — stringutil.LowerFirstChar (util.go:248).
static std::string lowerFirstChar(const std::string& str) {
	if (str.empty()) {
		return str;
	}
	auto [ch, size] = gostr::decodeRuneInString(str);
	(void)ch;
	if (size > 0) {
		return gostr::toLower(std::string_view(str).substr(0, size)) +
		       str.substr(size);
	}
	return str;
}

// newFourslash — fourslash.go:176.
std::pair<std::shared_ptr<FourslashTest>, std::function<void()>>
newFourslash(gostd::testing::T* t, const std::string& content,
             std::shared_ptr<FourslashOptions> options,
             const std::string& testPath) {
	if (options == nullptr) {
		options = std::make_shared<FourslashOptions>();
	}
	if (!bundled::Embedded) {
		// Without embedding, we'd need to read all of the lib files out
		// from disk into the MapFS. Just skip this for now.
		t->Skip({"bundled files are not embedded"});
	}

	std::string fileName = getBaseFileNameFromTest(t) +
	                       std::string(tspath::extensionTs);
	std::unordered_map<std::string, std::any> testfs;
	std::unordered_map<std::string, std::shared_ptr<scriptInfo>> scriptInfos;
	TestData testData = ParseTestData(t, content, fileName);
	for (auto& file : testData.Files) {
		std::string filePath =
		    tspath::getNormalizedAbsolutePath(file->fileName,
		                                      std::string(rootDir));
		// Dynamic files (e.g., untitled:) shouldn't be added to the VFS
		if (!tspath::isDynamicFileName(filePath)) {
			testfs[filePath] = file->Content;
		}
		scriptInfos[filePath] = newScriptInfo(filePath, file->Content);
	}

	for (auto& [link, target] : testData.Symlinks) {
		std::string filePath =
		    tspath::getNormalizedAbsolutePath(link, std::string(rootDir));
		testfs[filePath] = vfs::vfstest::Symlink(
		    tspath::getNormalizedAbsolutePath(target, std::string(rootDir)));
	}

	// !!! use default compiler options for inferred project as base
	tsc::CompilerOptions compilerOptions{
	    .Jsx = JsxEmit::Preserve,
	    .SkipDefaultLibCheck = Tristate::True,
	    .Target = ScriptTarget::LatestStandard,
	};
	testutil::harnessutil::HarnessOptions harnessOptions{
	    .UseCaseSensitiveFileNames = true,
	    .CurrentDirectory = std::string(rootDir),
	};
	testutil::harnessutil::SetOptionsFromTestConfig(t, testData.GlobalOptions,
	                                      &compilerOptions, &harnessOptions,
	                                      std::string(rootDir),
	                                      true /*allowUnknownOptions*/);
	auto tscIt = testData.GlobalOptions.find("tsc");
	if (tscIt != testData.GlobalOptions.end() && !tscIt->second.empty()) {
		// strings.SplitSeq(commandLines, ",")
		std::string commandLines = tscIt->second;
		size_t pos = 0;
		while (pos <= commandLines.size()) {
			size_t comma = commandLines.find(',', pos);
			std::string commandLine =
			    comma == std::string::npos
			        ? commandLines.substr(pos)
			        : commandLines.substr(pos, comma - pos);
			// strings.Split(commandLine, " ")
			std::vector<std::string> args;
			size_t a = 0;
			while (a <= commandLine.size()) {
				size_t sp = commandLine.find(' ', a);
				args.push_back(sp == std::string::npos
				                   ? commandLine.substr(a)
				                   : commandLine.substr(a, sp - a));
				if (sp == std::string::npos) break;
				a = sp + 1;
			}
			tsctests::getFileMapWithBuild(testfs, args);
			if (comma == std::string::npos) break;
			pos = comma + 1;
		}
	}

	testutil::harnessutil::SkipUnsupportedCompilerOptions(t, &compilerOptions);

	auto fsFromMap = vfs::vfstest::FromMap(
	    // testfs is map[string]any of string | *fstest.MapFile
	    [&testfs]() {
			std::unordered_map<std::string, vfs::vfstest::MapFileInput> m;
			for (auto& [k, v] : testfs) {
				if (auto s = std::any_cast<std::string>(&v)) {
					m[k] = *s;
				} else if (auto f =
				               std::any_cast<std::shared_ptr<vfs::vfstest::fstest::MapFile>>(
				                   &v)) {
					m[k] = *f;
				} else {
					TSC_UNREACHABLE("unexpected testfs entry type");
				}
			}
			return m;
		}(),
	    harnessOptions.UseCaseSensitiveFileNames);
	auto fs = bundled::WrapFS(fsFromMap);

	lsp::ServerOptions serverOpts{
	    .Err = gostd::io::discard(),
	    .Cwd = "/",
	    .FS = fs,
	    .DefaultLibraryPath = bundled::LibPath(),
	    .ParseCache = parseCache(),
	};
	if (options->ContentMapperSpawner != nullptr) {
		auto spawner = options->ContentMapperSpawner;
		serverOpts.Spawn = [spawner](
		    const std::vector<std::string>& command,
		    const std::string& dir, gostd::io::Writer* stderrW) {
			return spawner->Spawn(command, dir, stderrW);
		};
	}

	auto f = std::make_shared<FourslashTest>();
	// Go: the converters closure and f.scriptInfos share one map (maps are
	// reference types), so capture f's member, not a copy of the local.
	auto converters = std::make_shared<testConverters>(
	    lsproto::PositionEncodingKindUTF8,
	    [f](const std::string& fileName) -> lsconv::LSPLineMap* {
		    auto it = f->scriptInfos.find(fileName);
		    if (it == f->scriptInfos.end()) {
			    return nullptr;
		    }
		    return it->second->lineMap;
	    });
	f->testData = std::make_shared<TestData>(std::move(testData));
	f->stateEnableFormatting = true;
	f->reportFormatOnTypeCrash = true;
	f->userPreferences = lsutil::NewDefaultUserPreferences();
	f->vfs = fs;
	f->scriptInfos = scriptInfos;
	f->converters = converters;
	f->baselines = {};
	f->openFiles = {};
	f->semanticTokenTypes = defaultSemanticTokenTypes();
	f->semanticTokenModifiers = defaultSemanticTokenModifiers();

	auto clientAndClose = testutil::lsptestutil::NewLSPClient(
	    t, serverOpts,
	    [f](gostd::Context ctx,
	        const std::shared_ptr<lsproto::RequestMessage>& req) {
		    return f->handleServerRequest(ctx, req);
	    });
	auto client = clientAndClose.first;
	auto closeClient = clientAndClose.second;
	f->client = client;

	// !!! temporary; remove when we have
	// `handleDidChangeConfiguration`/implicit project config support
	// !!! replace with a proper request *after initialize*
	// Go's `compilerOptions` variable escapes to the heap because the
	// client stores a pointer to it past this frame; allocate it so the
	// stored pointer stays valid after NewFourslash returns.
	client->SetCompilerOptionsForInferredProjects(
	    new CompilerOptions(compilerOptions));
	f->initialize(t, options->Capabilities, options->RunExternalCode);

	if (f->testData->isStateBaseliningEnabled()) {
		// Single baseline, so initialize project state baseline too
		f->stateBaseline_ = newStateBaseline(
		    std::dynamic_pointer_cast<vfs::iovfs::FsWithSys>(fsFromMap));
	} else {
		for (auto& file : f->testData->Files) {
			if (file->open) {
				f->openFile(t, file->fileName);
			}
		}
		f->activeFilename = f->testData->Files[0]->fileName;
	}

	return {f, [t, f, closeClient, testPath]() {
		    t->Helper();
		    auto err = closeClient();
		    if (err != nullptr) {
			    t->Errorf("goroutine error: %v", {err});
		    }
		    f->verifyBaselines(t, testPath);
		}};
}

// ===========================================================================
// fourslash.go:288-340 — handleServerRequest
// ===========================================================================

// handleServerRequest handles requests initiated by the server (e.g.,
// workspace/configuration).
std::shared_ptr<lsproto::ResponseMessage> FourslashTest::handleServerRequest(
    gostd::Context ctx,
    const std::shared_ptr<lsproto::RequestMessage>& req) {
	if (req->Method == lsproto::MethodWorkspaceConfiguration) {
		// Return current user preferences for each requested section.
		// The server requests multiple sections (js/ts, typescript,
		// javascript, editor); we return user preferences for "js/ts" and
		// nil for others.
		auto [params, err] =
		    req->UnmarshalParams<
		        std::shared_ptr<lsproto::ConfigurationParams>>();
		if (err != nullptr || params == nullptr ||
		    !params->Items.has_value()) {
			auto resp = std::make_shared<lsproto::ResponseMessage>();
			resp->ID = req->ID;
			resp->JSONRPC = req->JSONRPC;
			resp->Result = lsproto::AnyValue::of(
			    std::vector<lsutil::UserPreferences>{userPreferences});
			return resp;
		}
		std::vector<std::shared_ptr<lsutil::UserPreferences>> results(
		    params->Items->size());
		for (size_t i = 0; i < params->Items->size(); i++) {
			auto& item = (*params->Items)[i];
			if (item->Section.has_value() && *item->Section == "js/ts") {
				results[i] =
				    std::make_shared<lsutil::UserPreferences>(
				        userPreferences);
			}
		}
		auto resp = std::make_shared<lsproto::ResponseMessage>();
		resp->ID = req->ID;
		resp->JSONRPC = req->JSONRPC;
		resp->Result = lsproto::AnyValue::of(std::move(results));
		return resp;
	}
	if (req->Method == lsproto::MethodClientRegisterCapability) {
		// Accept all capability registrations
		auto resp = std::make_shared<lsproto::ResponseMessage>();
		resp->ID = req->ID;
		resp->JSONRPC = req->JSONRPC;
		resp->Result = lsproto::AnyValue::of(lsproto::Null{});
		return resp;
	}
	if (req->Method == lsproto::MethodClientUnregisterCapability) {
		// Accept all capability unregistrations
		auto resp = std::make_shared<lsproto::ResponseMessage>();
		resp->ID = req->ID;
		resp->JSONRPC = req->JSONRPC;
		resp->Result = lsproto::AnyValue::of(lsproto::Null{});
		return resp;
	}
	// Unknown server request
	auto resp = std::make_shared<lsproto::ResponseMessage>();
	resp->ID = req->ID;
	resp->JSONRPC = req->JSONRPC;
	resp->Error = std::make_shared<jsonrpc::ResponseError>(
	    jsonrpc::ResponseError{
	        .Code = (int32_t)lsproto::ErrorCode::MethodNotFound,
	        .Message = gostd::sprintf("Unknown method: %s", {req->Method}),
	    });
	return resp;
}

// getBaseFileNameFromTest — fourslash.go:343.
std::string getBaseFileNameFromTest(gostd::testing::T* t) {
	std::string name = t->Name();
	auto parts = gostr::split(name, "/");
	name = parts.empty() ? "" : parts.back();
	// strings.TrimPrefix(name, "Test")
	if (name.starts_with("Test")) {
		name = name.substr(4);
	}
	name = lowerFirstChar(name);

	// Special case: TypeScript has "callHierarchyFunctionAmbiguity.N" with
	// periods
	if (name == "callHierarchyFunctionAmbiguity1") {
		name = "callHierarchyFunctionAmbiguity.1";
	} else if (name == "callHierarchyFunctionAmbiguity2") {
		name = "callHierarchyFunctionAmbiguity.2";
	} else if (name == "callHierarchyFunctionAmbiguity3") {
		name = "callHierarchyFunctionAmbiguity.3";
	} else if (name == "callHierarchyFunctionAmbiguity4") {
		name = "callHierarchyFunctionAmbiguity.4";
	} else if (name == "callHierarchyFunctionAmbiguity5") {
		name = "callHierarchyFunctionAmbiguity.5";
	}

	return name;
}

// ===========================================================================
// fourslash.go:368-395 — initialize
// ===========================================================================

void FourslashTest::initialize(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::ClientCapabilities>& capabilities,
    bool runExternalCode) {
	auto initializationOptions =
	    std::make_shared<lsproto::InitializationOptions>();
	initializationOptions->CodeLensShowLocationsCommandName =
	    showCodeLensLocationsCommandName;
	if (runExternalCode) {
		initializationOptions->RunExternalCode = true;
	}
	auto params = std::make_shared<lsproto::InitializeParams>();
	params->Locale = "en-US";
	params->InitializationOptions =
	    std::make_shared<lsproto::InitializationOptionsOrNull>(
	        lsproto::InitializationOptionsOrNull{
	            .InitializationOptions = initializationOptions,
	        });
	params->Capabilities = getCapabilitiesWithDefaults(capabilities);
	this->capabilities = params->Capabilities;
	auto [resp, result, ok] =
	    client->SendRequest(t, lsproto::InitializeInfo, params);
	if (!ok) {
		t->Fatalf("Initialize request failed", {});
	}
	if (resp->AsResponse()->Error != nullptr) {
		t->Fatalf("Initialize request returned error: %s",
		          {resp->AsResponse()->Error->String()});
	}
	client->SendNotification(
	    t, lsproto::InitializedInfo,
	    std::make_shared<lsproto::InitializedParams>());

	// Wait for the initial configuration exchange to complete
	// The server will send workspace/configuration as part of
	// handleInitialized
	client->Server->InitComplete()->wait();
}

// ===========================================================================
// fourslash.go:397-523 — semantic token + capability defaults
// ===========================================================================

std::vector<std::string> defaultSemanticTokenTypes() {
	return {
	    std::string(lsproto::SemanticTokenTypeNamespace),
	    std::string(lsproto::SemanticTokenTypeClass),
	    std::string(lsproto::SemanticTokenTypeEnum),
	    std::string(lsproto::SemanticTokenTypeInterface),
	    std::string(lsproto::SemanticTokenTypeStruct),
	    std::string(lsproto::SemanticTokenTypeTypeParameter),
	    std::string(lsproto::SemanticTokenTypeType),
	    std::string(lsproto::SemanticTokenTypeParameter),
	    std::string(lsproto::SemanticTokenTypeVariable),
	    std::string(lsproto::SemanticTokenTypeProperty),
	    std::string(lsproto::SemanticTokenTypeEnumMember),
	    std::string(lsproto::SemanticTokenTypeDecorator),
	    std::string(lsproto::SemanticTokenTypeEvent),
	    std::string(lsproto::SemanticTokenTypeFunction),
	    std::string(lsproto::SemanticTokenTypeMethod),
	    std::string(lsproto::SemanticTokenTypeMacro),
	    std::string(lsproto::SemanticTokenTypeLabel),
	    std::string(lsproto::SemanticTokenTypeComment),
	    std::string(lsproto::SemanticTokenTypeString),
	    std::string(lsproto::SemanticTokenTypeKeyword),
	    std::string(lsproto::SemanticTokenTypeNumber),
	    std::string(lsproto::SemanticTokenTypeRegexp),
	    std::string(lsproto::SemanticTokenTypeOperator),
	};
}

std::vector<std::string> defaultSemanticTokenModifiers() {
	return {
	    std::string(lsproto::SemanticTokenModifierDeclaration),
	    std::string(lsproto::SemanticTokenModifierDefinition),
	    std::string(lsproto::SemanticTokenModifierReadonly),
	    std::string(lsproto::SemanticTokenModifierStatic),
	    std::string(lsproto::SemanticTokenModifierDeprecated),
	    std::string(lsproto::SemanticTokenModifierAbstract),
	    std::string(lsproto::SemanticTokenModifierAsync),
	    std::string(lsproto::SemanticTokenModifierModification),
	    std::string(lsproto::SemanticTokenModifierDocumentation),
	    std::string(lsproto::SemanticTokenModifierDefaultLibrary),
	    "local",
	};
}

namespace {
// If modifying the defaults, update GetDefaultCapabilities too.

// Go `ptr.To(true|false)`; generated capability fields are
// std::optional<bool> so plain bools work.
constexpr bool ptrTrue = true;
constexpr bool ptrFalse = false;

template <typename T>
// OLD sliceOf deleted — replaced by shared_ptr<lsproto::Slice<T>> in header.
std::shared_ptr<std::vector<T>> sliceOfVecDeleted(std::initializer_list<T> items) {
	return std::make_shared<std::vector<T>>(items);
}

std::shared_ptr<lsproto::CompletionClientCapabilities>
defaultCompletionCapabilities() {
	auto p = std::make_shared<lsproto::CompletionClientCapabilities>(
	    [] {
		    lsproto::CompletionClientCapabilities c;
		    c.CompletionItem =
		        std::make_shared<lsproto::ClientCompletionItemOptions>(
		            [] {
			            lsproto::ClientCompletionItemOptions o;
			            o.SnippetSupport = ptrFalse;
			            o.CommitCharactersSupport = ptrTrue;
			            o.PreselectSupport = ptrTrue;
			            o.LabelDetailsSupport = ptrFalse;
			            o.InsertReplaceSupport = ptrTrue;
			            o.DocumentationFormat =
			                sliceOf<lsproto::MarkupKind>(
			                    {lsproto::MarkupKindMarkdown,
			                     lsproto::MarkupKindPlainText});
			            return o;
		            }());
		    c.CompletionList =
		        std::make_shared<lsproto::CompletionListCapabilities>(
		            [] {
			            lsproto::CompletionListCapabilities l;
			            l.ItemDefaults = sliceOf<std::string>(
			                {"commitCharacters", "editRange"});
			            return l;
		            }());
		    return c;
	    }());
	return p;
}

std::shared_ptr<lsproto::DefinitionClientCapabilities>
defaultDefinitionCapabilities() {
	auto p = [] {
		lsproto::DefinitionClientCapabilities c;
		c.LinkSupport = ptrTrue;
		return std::make_shared<lsproto::DefinitionClientCapabilities>(c);
	}();
	return p;
}

std::shared_ptr<lsproto::TypeDefinitionClientCapabilities>
defaultTypeDefinitionCapabilities() {
	auto p = [] {
		lsproto::TypeDefinitionClientCapabilities c;
		c.LinkSupport = ptrTrue;
		return std::make_shared<lsproto::TypeDefinitionClientCapabilities>(
		    c);
	}();
	return p;
}

std::shared_ptr<lsproto::ImplementationClientCapabilities>
defaultImplementationCapabilities() {
	auto p = [] {
		lsproto::ImplementationClientCapabilities c;
		c.LinkSupport = ptrTrue;
		return std::make_shared<
		    lsproto::ImplementationClientCapabilities>(c);
	}();
	return p;
}

std::shared_ptr<lsproto::HoverClientCapabilities> defaultHoverCapabilities() {
	auto p = [] {
		lsproto::HoverClientCapabilities c;
		c.ContentFormat = sliceOf<lsproto::MarkupKind>(
		    {lsproto::MarkupKindMarkdown, lsproto::MarkupKindPlainText});
		return std::make_shared<lsproto::HoverClientCapabilities>(c);
	}();
	return p;
}

std::shared_ptr<lsproto::ExperimentalClientCapabilities>
defaultExperimentalCapabilities() {
	auto p = [] {
		lsproto::ExperimentalClientCapabilities c;
		c.HoverVerbosityLevel = ptrTrue;
		return std::make_shared<lsproto::ExperimentalClientCapabilities>(c);
	}();
	return p;
}

std::shared_ptr<lsproto::SignatureHelpClientCapabilities>
defaultSignatureHelpCapabilities() {
	auto p = [] {
		lsproto::SignatureHelpClientCapabilities c;
		c.SignatureInformation =
		    std::make_shared<lsproto::ClientSignatureInformationOptions>(
		        [] {
			        lsproto::ClientSignatureInformationOptions o;
			        o.DocumentationFormat =
			            sliceOf<lsproto::MarkupKind>(
			                {lsproto::MarkupKindMarkdown,
			                     lsproto::MarkupKindPlainText});
			        o.ParameterInformation =
			            std::make_shared<
			                lsproto::
			                    ClientSignatureParameterInformationOptions>(
			                [] {
				                lsproto::
				                    ClientSignatureParameterInformationOptions
				                        p;
				                p.LabelOffsetSupport = ptrTrue;
				                return p;
			                }());
			        o.ActiveParameterSupport = ptrTrue;
			        return o;
		        }());
		c.ContextSupport = ptrTrue;
		return std::make_shared<lsproto::SignatureHelpClientCapabilities>(c);
	}();
	return p;
}

std::shared_ptr<lsproto::DocumentSymbolClientCapabilities>
defaultDocumentSymbolCapabilities() {
	auto p = [] {
		lsproto::DocumentSymbolClientCapabilities c;
		c.HierarchicalDocumentSymbolSupport = ptrTrue;
		return std::make_shared<lsproto::DocumentSymbolClientCapabilities>(
		    c);
	}();
	return p;
}

std::shared_ptr<lsproto::FoldingRangeClientCapabilities>
defaultFoldingRangeCapabilities() {
	auto p = [] {
		lsproto::FoldingRangeClientCapabilities c;
		c.RangeLimit = (uint32_t)5000;
		// LineFoldingOnly: ptrTrue,
		c.FoldingRangeKind =
		    std::make_shared<lsproto::ClientFoldingRangeKindOptions>(
		        [] {
			        lsproto::ClientFoldingRangeKindOptions o;
			        o.ValueSet = sliceOf<lsproto::FoldingRangeKind>(
			            {lsproto::FoldingRangeKindComment,
			                 lsproto::FoldingRangeKindImports,
			                 lsproto::FoldingRangeKindRegion});
			        return o;
		        }());
		c.FoldingRange =
		    std::make_shared<lsproto::ClientFoldingRangeOptions>([] {
			    lsproto::ClientFoldingRangeOptions o;
			    // Unused by our testing, but set to exercise the code.
			    o.CollapsedText = ptrTrue;
			    return o;
		    }());
		return std::make_shared<lsproto::FoldingRangeClientCapabilities>(c);
	}();
	return p;
}

std::shared_ptr<lsproto::ClientDiagnosticsTagOptions> defaultTagOptions() {
	auto p = [] {
		lsproto::ClientDiagnosticsTagOptions o;
		o.ValueSet = {lsproto::DiagnosticTagUnnecessary,
		              lsproto::DiagnosticTagDeprecated};
		return std::make_shared<lsproto::ClientDiagnosticsTagOptions>(o);
	}();
	return p;
}

std::shared_ptr<lsproto::DiagnosticClientCapabilities>
defaultDiagnosticCapabilities() {
	auto p = [] {
		lsproto::DiagnosticClientCapabilities c;
		c.RelatedInformation = ptrTrue;
		c.TagSupport = defaultTagOptions();
		return std::make_shared<lsproto::DiagnosticClientCapabilities>(c);
	}();
	return p;
}

std::shared_ptr<lsproto::PublishDiagnosticsClientCapabilities>
defaultPublishDiagnosticCapabilities() {
	auto p = [] {
		lsproto::PublishDiagnosticsClientCapabilities c;
		c.RelatedInformation = ptrTrue;
		c.TagSupport = defaultTagOptions();
		return std::make_shared<
		    lsproto::PublishDiagnosticsClientCapabilities>(c);
	}();
	return p;
}

std::shared_ptr<lsproto::WorkspaceEditClientCapabilities>
defaultWorkspaceEditCapabilities() {
	auto p = [] {
		lsproto::WorkspaceEditClientCapabilities c;
		c.DocumentChanges = ptrTrue;
		c.ResourceOperations = sliceOf<lsproto::ResourceOperationKind>(
		    {lsproto::ResourceOperationKindRename});
		return std::make_shared<lsproto::WorkspaceEditClientCapabilities>(
		    c);
	}();
	return p;
}
} // namespace

// GetDefaultCapabilities — fourslash.go:526.
std::shared_ptr<lsproto::ClientCapabilities> GetDefaultCapabilities() {
	auto c = std::make_shared<lsproto::ClientCapabilities>();
	c->General = std::make_shared<lsproto::GeneralClientCapabilities>(
	    [] {
		    lsproto::GeneralClientCapabilities g;
		    g.PositionEncodings =
		        sliceOf<lsproto::PositionEncodingKind>(
		            {lsproto::PositionEncodingKindUTF8});
		    return g;
	    }());
	c->Experimental = defaultExperimentalCapabilities();
	// Fresh copies of the default capabilities (Go builds new literals).
	c->TextDocument =
	    std::make_shared<lsproto::TextDocumentClientCapabilities>();
	c->TextDocument->Completion = defaultCompletionCapabilities();
	c->TextDocument->Diagnostic = defaultDiagnosticCapabilities();
	c->TextDocument->PublishDiagnostics =
	    defaultPublishDiagnosticCapabilities();
	c->TextDocument->Definition = defaultDefinitionCapabilities();
	c->TextDocument->TypeDefinition = defaultTypeDefinitionCapabilities();
	c->TextDocument->Implementation = defaultImplementationCapabilities();
	c->TextDocument->Hover = defaultHoverCapabilities();
	c->TextDocument->SignatureHelp = defaultSignatureHelpCapabilities();
	c->TextDocument->DocumentSymbol = defaultDocumentSymbolCapabilities();
	c->TextDocument->FoldingRange = defaultFoldingRangeCapabilities();
	c->Workspace =
	    std::make_shared<lsproto::WorkspaceClientCapabilities>();
	c->Workspace->Configuration = ptrTrue;
	c->Workspace->FileOperations =
	    std::make_shared<lsproto::FileOperationClientCapabilities>(
	        [] {
		        lsproto::FileOperationClientCapabilities f;
		        f.WillRename = ptrTrue;
		        return f;
	        }());
	c->Workspace->WorkspaceEdit = defaultWorkspaceEditCapabilities();
	return c;
}

// GetDefaultCapabilitiesWithOptions — fourslash.go:624.
std::shared_ptr<lsproto::ClientCapabilities>
GetDefaultCapabilitiesWithOptions(const ClientCapabilitiesOptions* options) {
	auto capabilities = GetDefaultCapabilities();
	if (options == nullptr) {
		return capabilities;
	}
	if (options->CompletionItem != nullptr) {
		auto target =
		    capabilities->TextDocument->Completion->CompletionItem;
		auto& o = options->CompletionItem;
		if (o->SnippetSupport.has_value())
			target->SnippetSupport = o->SnippetSupport;
		if (o->CommitCharactersSupport.has_value())
			target->CommitCharactersSupport =
			    o->CommitCharactersSupport;
		if (o->DocumentationFormat != nullptr)
			target->DocumentationFormat = o->DocumentationFormat;
		if (o->DeprecatedSupport.has_value())
			target->DeprecatedSupport = o->DeprecatedSupport;
		if (o->PreselectSupport.has_value())
			target->PreselectSupport = o->PreselectSupport;
		if (o->TagSupport != nullptr)
			target->TagSupport = o->TagSupport;
		if (o->InsertReplaceSupport.has_value())
			target->InsertReplaceSupport = o->InsertReplaceSupport;
		if (o->ResolveSupport != nullptr)
			target->ResolveSupport = o->ResolveSupport;
		if (o->InsertTextModeSupport != nullptr)
			target->InsertTextModeSupport = o->InsertTextModeSupport;
		if (o->LabelDetailsSupport.has_value())
			target->LabelDetailsSupport = o->LabelDetailsSupport;
	}
	return capabilities;
}

// getCapabilitiesWithDefaults — fourslash.go:666.
std::shared_ptr<lsproto::ClientCapabilities> getCapabilitiesWithDefaults(
    const std::shared_ptr<lsproto::ClientCapabilities>& capabilities) {
	lsproto::ClientCapabilities withDefaults{};
	if (capabilities != nullptr) {
		withDefaults = *capabilities;
	}
	withDefaults.General =
	    std::make_shared<lsproto::GeneralClientCapabilities>([] {
		    lsproto::GeneralClientCapabilities g;
		    g.PositionEncodings =
		        sliceOf<lsproto::PositionEncodingKind>(
		            {lsproto::PositionEncodingKindUTF8});
		    return g;
	    }());
	if (withDefaults.Experimental == nullptr) {
		withDefaults.Experimental = defaultExperimentalCapabilities();
	}
	if (withDefaults.TextDocument == nullptr) {
		withDefaults.TextDocument =
		    std::make_shared<lsproto::TextDocumentClientCapabilities>();
	}
	if (withDefaults.TextDocument->Completion == nullptr) {
		withDefaults.TextDocument->Completion =
		    defaultCompletionCapabilities();
	}
	if (withDefaults.TextDocument->Diagnostic == nullptr) {
		withDefaults.TextDocument->Diagnostic =
		    defaultDiagnosticCapabilities();
	}
	if (withDefaults.TextDocument->PublishDiagnostics == nullptr) {
		withDefaults.TextDocument->PublishDiagnostics =
		    defaultPublishDiagnosticCapabilities();
	}
	if (withDefaults.TextDocument->SemanticTokens == nullptr) {
		withDefaults.TextDocument->SemanticTokens = std::make_shared<
		    lsproto::SemanticTokensClientCapabilities>([] {
			lsproto::SemanticTokensClientCapabilities s;
			s.Requests = std::make_shared<
			    lsproto::ClientSemanticTokensRequestOptions>([] {
			    lsproto::ClientSemanticTokensRequestOptions r;
			    r.Full = std::make_shared<
			        lsproto::
			            BooleanOrClientSemanticTokensRequestFullDelta>(
			        [] {
				        lsproto::
				            BooleanOrClientSemanticTokensRequestFullDelta
				                b;
				        b.Boolean = std::make_shared<bool>(true);
				        return b;
			        }());
			    return r;
		    }());
			s.TokenTypes = defaultSemanticTokenTypes();
			s.TokenModifiers = defaultSemanticTokenModifiers();
			s.Formats = {lsproto::TokenFormatRelative};
			return s;
		}());
	}
	if (withDefaults.Workspace == nullptr) {
		withDefaults.Workspace =
		    std::make_shared<lsproto::WorkspaceClientCapabilities>();
	}
	if (withDefaults.Workspace->FileOperations == nullptr) {
		withDefaults.Workspace->FileOperations = std::make_shared<
		    lsproto::FileOperationClientCapabilities>([] {
			lsproto::FileOperationClientCapabilities fo;
			fo.WillRename = ptrTrue;
			return fo;
		}());
	}
	if (withDefaults.Workspace->WorkspaceEdit == nullptr) {
		withDefaults.Workspace->WorkspaceEdit =
		    defaultWorkspaceEditCapabilities();
	}
	if (!withDefaults.Workspace->Configuration.has_value()) {
		withDefaults.Workspace->Configuration = ptrTrue;
	}
	if (withDefaults.TextDocument->Definition == nullptr) {
		withDefaults.TextDocument->Definition =
		    defaultDefinitionCapabilities();
	}
	if (withDefaults.TextDocument->TypeDefinition == nullptr) {
		withDefaults.TextDocument->TypeDefinition =
		    defaultTypeDefinitionCapabilities();
	}
	if (withDefaults.TextDocument->Implementation == nullptr) {
		withDefaults.TextDocument->Implementation =
		    defaultImplementationCapabilities();
	}
	if (withDefaults.TextDocument->Hover == nullptr) {
		withDefaults.TextDocument->Hover = defaultHoverCapabilities();
	}
	if (withDefaults.TextDocument->SignatureHelp == nullptr) {
		withDefaults.TextDocument->SignatureHelp =
		    defaultSignatureHelpCapabilities();
	}
	if (withDefaults.TextDocument->DocumentSymbol == nullptr) {
		withDefaults.TextDocument->DocumentSymbol =
		    defaultDocumentSymbolCapabilities();
	}
	if (withDefaults.TextDocument->FoldingRange == nullptr) {
		withDefaults.TextDocument->FoldingRange =
		    defaultFoldingRangeCapabilities();
	}
	return std::make_shared<lsproto::ClientCapabilities>(withDefaults);
}

// ===========================================================================
// fourslash.go:744-797 — sendRequestAndBaselineWorker / updateState
// (sendRequest/sendNotification templates live in fourslash.h)
// ===========================================================================

// ===========================================================================
// fourslash.go:799-826 — GetOptions / Configure / ConfigureWithReset
// ===========================================================================

// GetOptions — fourslash.go:799.
lsutil::UserPreferences FourslashTest::GetOptions() {
	return userPreferences;
}

// Configure — fourslash.go:803.
void FourslashTest::Configure(gostd::testing::T* t,
                              const lsutil::UserPreferences& config) {
	// We send 'js/ts' by default because that is what we expect the primary
	// config to be in vscode and VS (one set of preferences for both
	// languages). This should be fine in fourslash since tests that need
	// multiple options usually send reconfiguration commands for each
	// `verify` anyways
	userPreferences = config;
	sendNotification(
	    t, lsproto::WorkspaceDidChangeConfigurationInfo,
	    std::make_shared<lsproto::DidChangeConfigurationParams>(
	        [config] {
		        lsproto::DidChangeConfigurationParams p;
		        p.Settings = lsproto::LSPAny(
		            std::map<std::string, lsproto::LSPAny>{
		                {"js/ts", lspAnyOf(config)}});
		        return p;
	        }()));
}

// ConfigureWithReset — fourslash.go:815.
std::function<void()> FourslashTest::ConfigureWithReset(
    gostd::testing::T* t, const lsutil::UserPreferences& config) {
	lsutil::UserPreferences originalConfig = userPreferences;
	Configure(t, config);
	return [this, t, originalConfig]() {
		Configure(t, originalConfig);
	};
}

// ===========================================================================
// fourslash.go:827-1084 — navigation
// ===========================================================================

void FourslashTest::GoToMarkerOrRange(gostd::testing::T* t,
                                      MarkerOrRange* markerOrRange) {
	goToMarker(t, markerOrRange);
}

void FourslashTest::GoToMarker(gostd::testing::T* t,
                               const std::string& markerName) {
	auto it = testData->MarkerPositions.find(markerName);
	if (it == testData->MarkerPositions.end()) {
		t->Fatalf("Marker '%s' not found", {markerName});
	}
	goToMarker(t, it->second.get());
}

void FourslashTest::goToMarker(gostd::testing::T* t,
                               MarkerOrRange* markerOrRange) {
	ensureActiveFile(t, markerOrRange->FileName());
	goToPosition(t, markerOrRange->LSPos());
	lastKnownMarkerName = markerOrRange->GetName();
}

void FourslashTest::GoToEOF(gostd::testing::T* t) {
	auto script = getScriptInfo(activeFilename);
	TextPos pos = (TextPos)script->content.size();
	auto LSPPos =
	    converters->PositionToLineAndCharacter(script, pos);
	goToPosition(t, LSPPos);
}

void FourslashTest::GoToBOF(gostd::testing::T* t) {
	goToPosition(t, lsproto::Position{0, 0});
}

void FourslashTest::GoToPosition(gostd::testing::T* t, int position) {
	auto script = getScriptInfo(activeFilename);
	auto LSPPos = converters->PositionToLineAndCharacter(
	    script, (TextPos)position);
	goToPosition(t, LSPPos);
}

void FourslashTest::goToPosition(gostd::testing::T* t,
                                 lsproto::Position position) {
	currentCaretPosition = position;
	selectionEnd = nullptr;
}

void FourslashTest::GoToEachMarker(
    gostd::testing::T* t, const std::vector<std::string>& markerNames,
    const std::function<void(std::shared_ptr<Marker>, int)>& action) {
	std::vector<std::shared_ptr<Marker>> markers;
	// fourslash.go: GoToEachMarker checks `len(markers) == 0` on a freshly
	// declared nil slice (always true), so Go ALWAYS iterates f.Markers()
	// and markerNames is ignored — the else branch below is dead code
	// upstream. Ported faithfully: TestAllowRenameOfImportPath relies on it
	// (it names [|a|] ranges, which never enter MarkerPositions, and is
	// vacuous in Go because the content has no /* */ markers).
	markers = Markers();
	for (size_t i = 0; i < markers.size(); i++) {
		goToMarker(t, markers[i].get());
		action(markers[i], (int)i);
	}
}

void FourslashTest::GoToEachRange(
    gostd::testing::T* t,
    const std::function<void(gostd::testing::T*,
                             std::shared_ptr<RangeMarker>)>& action) {
	auto ranges = Ranges();
	for (auto& rangeMarker : ranges) {
		goToPosition(t, rangeMarker->LSRange.Start);
		action(t, rangeMarker);
	}
}

void FourslashTest::GoToRangeStart(
    gostd::testing::T* t,
    const std::shared_ptr<RangeMarker>& rangeMarker) {
	openFile(t, rangeMarker->FileName());
	goToPosition(t, rangeMarker->LSRange.Start);
}

void FourslashTest::GoToSelect(gostd::testing::T* t,
                               const std::string& startMarkerName,
                               const std::string& endMarkerName) {
	auto sIt = testData->MarkerPositions.find(startMarkerName);
	if (sIt == testData->MarkerPositions.end()) {
		t->Fatalf("Start marker '%s' not found", {startMarkerName});
	}
	auto eIt = testData->MarkerPositions.find(endMarkerName);
	if (eIt == testData->MarkerPositions.end()) {
		t->Fatalf("End marker '%s' not found", {endMarkerName});
	}
	auto& startMarker = sIt->second;
	auto& endMarker = eIt->second;
	if (startMarker->FileName() != endMarker->FileName()) {
		t->Fatalf("Markers '%s' and '%s' are in different files",
		          {startMarkerName, endMarkerName});
	}
	ensureActiveFile(t, startMarker->FileName());
	goToPosition(t, startMarker->LSPosition);
	selectionEnd =
	    std::make_shared<lsproto::Position>(endMarker->LSPosition);
}

void FourslashTest::GoToSelectRange(
    gostd::testing::T* t,
    const std::shared_ptr<RangeMarker>& rangeMarker) {
	GoToRangeStart(t, rangeMarker);
	selectionEnd =
	    std::make_shared<lsproto::Position>(rangeMarker->LSRange.End);
}

void FourslashTest::GoToFile(gostd::testing::T* t,
                             const std::string& filename) {
	std::string normalized =
	    tspath::getNormalizedAbsolutePath(filename, std::string(rootDir));
	openFile(t, normalized);
}

void FourslashTest::GoToFileNumber(gostd::testing::T* t, int index) {
	if (index < 0 || index >= (int)testData->Files.size()) {
		t->Fatalf("File index %d out of range (0-%d)",
		          {index, (int)testData->Files.size() - 1});
	}
	std::string filename = testData->Files[index]->fileName;
	openFile(t, filename);
}

std::vector<std::shared_ptr<Marker>> FourslashTest::Markers() {
	return testData->Markers;
}

std::vector<std::string> FourslashTest::MarkerNames() {
	std::vector<std::string> names;
	for (auto& marker : testData->Markers) {
		if (marker->Name != nullptr) {
			names.push_back(*marker->Name);
		}
	}
	return names;
}

std::shared_ptr<Marker> FourslashTest::MarkerByName(
    gostd::testing::T* t, const std::string& name) {
	auto it = testData->MarkerPositions.find(name);
	return it == testData->MarkerPositions.end() ? nullptr : it->second;
}

std::vector<std::shared_ptr<RangeMarker>> FourslashTest::Ranges() {
	return testData->Ranges;
}

std::vector<std::shared_ptr<RangeMarker>> FourslashTest::getRangesInFile(
    const std::string& fileName) {
	std::vector<std::shared_ptr<RangeMarker>> rangesInFile;
	for (auto& rangeMarker : testData->Ranges) {
		if (rangeMarker->FileName() == fileName) {
			rangesInFile.push_back(rangeMarker);
		}
	}
	return rangesInFile;
}

void FourslashTest::ensureActiveFile(gostd::testing::T* t,
                                     const std::string& filename) {
	if (activeFilename != filename) {
		if (openFiles.find(filename) == openFiles.end()) {
			openFile(t, filename);
		} else {
			activeFilename = filename;
		}
	}
}

void FourslashTest::CloseFileOfMarker(gostd::testing::T* t,
                                      const std::string& markerName) {
	auto it = testData->MarkerPositions.find(markerName);
	if (it == testData->MarkerPositions.end()) {
		t->Fatalf("Marker '%s' not found", {markerName});
	}
	auto& marker = it->second;
	if (activeFilename == marker->FileName()) {
		activeFilename = "";
	}
	// slices.IndexFunc over testData->Files
	auto filesIt = std::find_if(
	    testData->Files.begin(), testData->Files.end(),
	    [&](const std::shared_ptr<TestFileInfo>& f) {
		    return f->fileName == marker->FileName();
	    });
	if (filesIt != testData->Files.end()) {
		auto& testFile = *filesIt;
		scriptInfos[testFile->fileName] =
		    newScriptInfo(testFile->fileName, testFile->Content);
	} else {
		scriptInfos.erase(marker->FileName());
	}
	sendNotification(
	    t, lsproto::TextDocumentDidCloseInfo,
	    std::make_shared<lsproto::DidCloseTextDocumentParams>(
	        [&] {
		        lsproto::DidCloseTextDocumentParams p;
		        p.TextDocument.Uri = lsconv::FileNameToDocumentURI(
		            marker->FileName());
		        return p;
	        }()));
}

void FourslashTest::openFile(gostd::testing::T* t,
                             const std::string& filename) {
	auto script = getScriptInfo(filename);
	if (script == nullptr) {
		auto [content, ok] = vfs->ReadFile(filename);
		if (ok) {
			auto sp = newScriptInfo(filename, content);
			scriptInfos[filename] = sp;
			script = sp.get();
		} else {
			t->Fatalf("File %s not found in test data", {filename});
		}
	}
	activeFilename = filename;
	sendNotification(
	    t, lsproto::TextDocumentDidOpenInfo,
	    std::make_shared<lsproto::DidOpenTextDocumentParams>(
	        [&] {
		        lsproto::DidOpenTextDocumentParams p;
		        // Value-initialize: `TextDocumentItem i;` would leave
		        // int32_t Version indeterminate (garbage stack value,
		        // serialized as "version": 1); Go zero-value is 0.
		        p.TextDocument =
		            std::make_shared<lsproto::TextDocumentItem>();
		        p.TextDocument->Uri =
		            lsconv::FileNameToDocumentURI(filename);
		        p.TextDocument->LanguageId = getLanguageKind(filename);
		        p.TextDocument->Text = script->content;
		        return p;
	        }()));
	baselineProjectsAfterNotification(t, filename);
}

void FourslashTest::FormatDocument(gostd::testing::T* t,
                                   const std::string& filename_) {
	std::string filename = filename_;
	if (filename.empty()) {
		filename = activeFilename;
	}
	auto result = sendRequest(
	    t, lsproto::TextDocumentFormattingInfo,
	    std::make_shared<lsproto::DocumentFormattingParams>([&] {
		    lsproto::DocumentFormattingParams p;
		    p.TextDocument.Uri =
		        lsconv::FileNameToDocumentURI(filename);
		    p.Options =
		        userPreferences.FormatCodeSettings.ToLSFormatOptions();
		    return p;
	    }()));
	if (result.TextEdits == nullptr) {
		return;
	}
	applyTextEdits(t, result.TextEdits->value_or(
	                    std::vector<std::shared_ptr<lsproto::TextEdit>>{}));
}

void FourslashTest::FormatSelection(gostd::testing::T* t,
                                    const std::string& startMarkerName,
                                    const std::string& endMarkerName) {
	t->Helper();
	auto sIt = testData->MarkerPositions.find(startMarkerName);
	if (sIt == testData->MarkerPositions.end()) {
		t->Fatalf("Marker '%s' not found", {startMarkerName});
	}
	auto eIt = testData->MarkerPositions.find(endMarkerName);
	if (eIt == testData->MarkerPositions.end()) {
		t->Fatalf("Marker '%s' not found", {endMarkerName});
	}
	auto& startMarker = sIt->second;
	auto& endMarker = eIt->second;
	if (startMarker->FileName() != endMarker->FileName()) {
		t->Fatalf("Markers '%s' and '%s' are in different files",
		          {startMarkerName, endMarkerName});
	}
	std::string filename = startMarker->FileName();
	auto result = sendRequest(
	    t, lsproto::TextDocumentRangeFormattingInfo,
	    std::make_shared<lsproto::DocumentRangeFormattingParams>([&] {
		    lsproto::DocumentRangeFormattingParams p;
		    p.TextDocument.Uri =
		        lsconv::FileNameToDocumentURI(filename);
		    p.Range = lsproto::Range{
		        .Start = startMarker->LSPosition,
		        .End = endMarker->LSPosition,
		    };
		    p.Options =
		        userPreferences.FormatCodeSettings.ToLSFormatOptions();
		    return p;
	    }()));
	if (result.TextEdits == nullptr) {
		return;
	}
	applyTextEdits(t, result.TextEdits->value_or(
	                    std::vector<std::shared_ptr<lsproto::TextEdit>>{}));
}

void FourslashTest::VerifyCurrentFileContent(
    gostd::testing::T* t, const std::string& expectedContent) {
	t->Helper();
	std::string actualContent = getScriptInfo(activeFilename)->content;
	assertEqual(t, actualContent, expectedContent);
}

void FourslashTest::VerifyCurrentLineContent(
    gostd::testing::T* t, const std::string& expectedContent) {
	t->Helper();
	std::string actualContent =
	    getScriptInfo(activeFilename)
	        ->GetLineContent((int)currentCaretPosition.Line);
	assertEqual(t, actualContent, expectedContent,
	              gostd::sprintf(
	                  "\n  actual line: \"%s\"\nexpected line: \"%s\"\n",
	                  {actualContent, expectedContent}));
}

void FourslashTest::VerifyIndentation(gostd::testing::T* t, int numSpaces) {
	t->Helper();
	// not implemented
	// actualContent := f.getScriptInfo(f.activeFilename).GetLineContent(int(f.currentCaretPosition.Line))
	// assert.Equal(t, actualContent, expectedContent, fmt.Sprintf("Actual line content %s does not match expected content.", actualContent))
}

// getLanguageKind — fourslash.go:1086.
lsproto::LanguageKind getLanguageKind(const std::string& filename) {
	if (tspath::fileExtensionIsOneOf(
	        filename,
	        std::vector<std::string_view>{
	            tspath::extensionTs,
	            tspath::extensionMts,
	            tspath::extensionCts,
	            tspath::extensionDmts,
	            tspath::extensionDcts,
	            tspath::extensionDts,
	        })) {
		return lsproto::LanguageKindTypeScript;
	}
	if (tspath::fileExtensionIsOneOf(
	        filename,
	        std::vector<std::string_view>{
	            tspath::extensionJs,
	            tspath::extensionMjs,
	            tspath::extensionCjs,
	        })) {
		return lsproto::LanguageKindJavaScript;
	}
	if (tspath::fileExtensionIs(filename, tspath::extensionJsx)) {
		return lsproto::LanguageKindJavaScriptReact;
	}
	if (tspath::fileExtensionIs(filename, tspath::extensionTsx)) {
		return lsproto::LanguageKindTypeScriptReact;
	}
	if (tspath::fileExtensionIs(filename, tspath::extensionJson)) {
		return lsproto::LanguageKindJSON;
	}
	return lsproto::LanguageKindTypeScript; // !!! should we error in this case?
}


// ===========================================================================
// fourslash.go:1160-1668 — completions
// ===========================================================================

// VerifyCompletions — fourslash.go:1160.
VerifyCompletionsResult FourslashTest::VerifyCompletions(
    gostd::testing::T* t, const MarkerInput& markerInput,
    const CompletionsExpectedList* expected) {
	t->Helper();
	std::shared_ptr<lsproto::CompletionList> list;
	if (auto* name = std::get_if<std::string>(&markerInput)) {
		GoToMarker(t, *name);
		list = verifyCompletionsWorker(t, expected);
	} else if (auto* marker =
	               std::get_if<std::shared_ptr<Marker>>(&markerInput)) {
		goToMarker(t, marker->get());
		list = verifyCompletionsWorker(t, expected);
	} else if (auto* names =
	               std::get_if<std::vector<std::string>>(&markerInput)) {
		for (auto& markerName : *names) {
			GoToMarker(t, markerName);
			verifyCompletionsWorker(t, expected);
		}
	} else if (auto* markers = std::get_if<
	               std::vector<std::shared_ptr<Marker>>>(&markerInput)) {
		for (auto& marker : *markers) {
			goToMarker(t, marker.get());
			verifyCompletionsWorker(t, expected);
		}
	} else if (std::holds_alternative<std::monostate>(markerInput)) {
		list = verifyCompletionsWorker(t, expected);
	} else {
		t->Fatalf(
		    "Invalid marker input type: %T. Expected string, *Marker, "
		    "[]string, or []*Marker.",
		    {gostr::variantTypeName(markerInput)});
	}
	return verifyCompletionsActions(list);
}

// verifyCompletionsActions — fourslash.go:1183.
VerifyCompletionsResult FourslashTest::verifyCompletionsActions(
    const std::shared_ptr<lsproto::CompletionList>& list) {
	VerifyCompletionsResult result;
	result.AndApplyCodeAction =
	    [this, list](gostd::testing::T* t,
	                 const CompletionsExpectedCodeAction* expectedAction) {
		    auto item = gostr::coreFind(
		        *list->Items,
		        [&](const std::shared_ptr<lsproto::CompletionItem>& item) {
			        if (item->Label != expectedAction->Name ||
			            item->Data == nullptr) {
				        return false;
			        }
			        auto& data = item->Data;
			        if (data->AutoImport == nullptr) {
				        return false;
			        }
			        return data->AutoImport->ModuleSpecifier ==
			               expectedAction->Source;
		        });
		    if (item == nullptr) {
			    t->Fatalf(
			        "Code action '%s' from source '%s' not found in "
			        "completions.",
			        {expectedAction->Name, expectedAction->Source});
		    }
		    // Detail and AdditionalTextEdits for auto-import items are
		    // populated by completionItem/resolve, not in the initial
		    // completion list.
		    item = resolveCompletionItem(t, item);
		    assertCheck(
		        t,
		        item->Detail.has_value() &&
		            gostr::contains(*item->Detail,
		                            expectedAction->Description),
		        "Completion item detail does not contain expected "
		        "description.");
		    applyTextEdits(t, orNilSlice(item->AdditionalTextEdits));
		    assertEqual(
		        t, getScriptInfo(activeFilename)->content,
		        expectedAction->NewFileContent,
		        gostd::sprintf(
		            "File content after applying code action '%s' did not "
		            "match expected content.",
		            {expectedAction->Name}));
	    };
	result.AndHasNoCodeAction =
	    [this, list](gostd::testing::T* t,
	                 const CompletionsExpectedCodeAction*
	                     unexpectedAction) {
		    auto item = gostr::coreFind(
		        *list->Items,
		        [&](const std::shared_ptr<lsproto::CompletionItem>& item) {
			        if (item->Label != unexpectedAction->Name ||
			            item->Data == nullptr) {
				        return false;
			        }
			        auto& data = item->Data;
			        if (data->AutoImport == nullptr) {
				        return false;
			        }
			        return data->AutoImport->ModuleSpecifier ==
			               unexpectedAction->Source;
		        });
		    if (item != nullptr) {
			    t->Fatalf(
			        "Unexpected code action '%s' from source '%s' found in "
			        "completions.",
			        {unexpectedAction->Name, unexpectedAction->Source});
		    }
	    };
	return result;
}

// verifyCompletionsWorker — fourslash.go:1219.
std::shared_ptr<lsproto::CompletionList>
FourslashTest::verifyCompletionsWorker(
    gostd::testing::T* t, const CompletionsExpectedList* expected) {
	t->Helper();
	std::shared_ptr<lsutil::UserPreferences> userPreferences;
	if (expected != nullptr) {
		userPreferences = expected->UserPreferences;
	}
	std::string prefix = getCurrentPositionPrefix();
	auto list = getCompletions(t, userPreferences);
	verifyCompletionsResult(t, list, expected, prefix);
	return list;
}

// GetCompletions — fourslash.go:1230.
std::shared_ptr<lsproto::CompletionList> FourslashTest::GetCompletions(
    gostd::testing::T* t,
    const std::shared_ptr<lsutil::UserPreferences>& userPreferences) {
	t->Helper();
	return getCompletions(t, userPreferences);
}

// VerifyJSDocCompletion — fourslash.go:1235.
void FourslashTest::VerifyJSDocCompletion(
    gostd::testing::T* t, const MarkerInput& markerInput,
    int expectedOffset, const std::string& expectedText,
    const std::shared_ptr<bool>& generateReturnInDocTemplate) {
	t->Helper();
	goToMarkerInput(t, markerInput);

	std::shared_ptr<lsutil::UserPreferences> userPreferences;
	if (generateReturnInDocTemplate != nullptr) {
		auto prefs = std::make_shared<lsutil::UserPreferences>(
		    lsutil::NewDefaultUserPreferences());
		prefs->GenerateReturnInDocTemplate =
		    *generateReturnInDocTemplate ? Tristate::True
		                                 : Tristate::False;
		userPreferences = prefs;
	}

	auto list = getCompletions(t, userPreferences);
	auto item = findJSDocCompletionItem(list);
	if (item == nullptr) {
		auto script = getScriptInfo(activeFilename);
		int insertStart = (int)converters->LineAndCharacterToPosition(
		    script, currentCaretPosition);
		Insert(t, "/**");
		list = getCompletions(t, userPreferences);
		item = findJSDocCompletionItem(list);
		editScriptAndUpdateMarkers(t, activeFilename, insertStart,
		                         insertStart + 3, "");
		currentCaretPosition = converters->PositionToLineAndCharacter(
		    script, (TextPos)insertStart);
	}
	if (list == nullptr) {
		t->Fatalf("%sExpected JSDoc completion, got nil completion list.",
		          {getCurrentPositionPrefix()});
	}
	if (item == nullptr) {
		t->Fatalf("%sExpected JSDoc completion item, got %#v.",
		          {getCurrentPositionPrefix(),
		           list->Items ? "non-nil items" : "nil items"});
	}
	if (item->TextEdit == nullptr ||
	    item->TextEdit->InsertReplaceEdit == nullptr) {
		t->Fatalf(
		    "%sExpected JSDoc completion to have insert/replace edit, "
		    "got %#v.",
		    {getCurrentPositionPrefix(), "<item>"});
	}
	assertEqual(t, item->TextEdit->InsertReplaceEdit->NewText, expectedText,
	            getCurrentPositionPrefix());
	(void)expectedOffset; // The completion path uses snippet placeholders
	                      // for caret placement.
}

// VerifyNoJSDocCompletion — fourslash.go:1267.
void FourslashTest::VerifyNoJSDocCompletion(
    gostd::testing::T* t, const MarkerInput& markerInput) {
	t->Helper();
	goToMarkerInput(t, markerInput);

	auto list =
	    getCompletions(t, nullptr /*userPreferences*/);
	if (auto item = findJSDocCompletionItem(list); item != nullptr) {
		t->Fatalf("%sDid not expect JSDoc completion item.",
		          {getCurrentPositionPrefix()});
	}

	auto script = getScriptInfo(activeFilename);
	int insertStart = (int)converters->LineAndCharacterToPosition(
	    script, currentCaretPosition);
	Insert(t, "/**");
	list = getCompletions(t, nullptr /*userPreferences*/);
	auto item = findJSDocCompletionItem(list);
	editScriptAndUpdateMarkers(t, activeFilename, insertStart,
	                         insertStart + 3, "");
	currentCaretPosition = converters->PositionToLineAndCharacter(
	    script, (TextPos)insertStart);
	if (item != nullptr) {
		t->Fatalf("%sDid not expect JSDoc completion item.",
		          {getCurrentPositionPrefix()});
	}
}

// findJSDocCompletionItem — fourslash.go:1302.
std::shared_ptr<lsproto::CompletionItem> findJSDocCompletionItem(
    const std::shared_ptr<lsproto::CompletionList>& list) {
	if (list == nullptr || !list->Items.has_value()) {
		return nullptr;
	}
	return gostr::coreFind(
	    *list->Items,
	    [](const std::shared_ptr<lsproto::CompletionItem>& item) {
		    return item->Label == "/** */";
	    });
}

// goToMarkerInput — fourslash.go:1310.
void FourslashTest::goToMarkerInput(gostd::testing::T* t,
                                    const MarkerInput& markerInput) {
	t->Helper();
	if (auto* name = std::get_if<std::string>(&markerInput)) {
		GoToMarker(t, *name);
	} else if (auto* marker =
	               std::get_if<std::shared_ptr<Marker>>(&markerInput)) {
		goToMarker(t, marker->get());
	} else {
		t->Fatalf(
		    "Invalid marker input type: %T. Expected string or *Marker.",
		    {gostr::variantTypeName(markerInput)});
	}
}

// getCompletions — fourslash.go:1322.
std::shared_ptr<lsproto::CompletionList> FourslashTest::getCompletions(
    gostd::testing::T* t,
    const std::shared_ptr<lsutil::UserPreferences>& userPreferences) {
	t->Helper();
	auto params = std::make_shared<lsproto::CompletionParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Position = currentCaretPosition;
	params->Context = std::make_shared<lsproto::CompletionContext>();
	std::function<void()> reset;
	if (userPreferences != nullptr) {
		lsutil::UserPreferences preferences = *userPreferences;
		preferences.FormatCodeSettings =
		    this->userPreferences.FormatCodeSettings;
		reset = ConfigureWithReset(t, preferences);
	}
	auto result = sendRequest(t, lsproto::TextDocumentCompletionInfo,
	                          params);
	if (reset) reset();
	// For performance, the server may return unsorted completion lists.
	// The client is expected to sort them by SortText and then by Label.
	// We are the client here.
	if (result.List != nullptr && result.List->Items.has_value()) {
		std::stable_sort(
		    result.List->Items->begin(), result.List->Items->end(),
		    [](const std::shared_ptr<lsproto::CompletionItem>& a,
		       const std::shared_ptr<lsproto::CompletionItem>& b) {
			    return ls::CompareCompletionEntries(a.get(), b.get()) < 0;
		    });
	}
	return result.List;
}

// verifyCompletionsResult — fourslash.go:1347.
void FourslashTest::verifyCompletionsResult(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::CompletionList>& actual,
    const CompletionsExpectedList* expected, const std::string& prefix) {
	if (actual == nullptr) {
		if (!isEmptyExpectedList(expected)) {
			t->Fatal({prefix + "Expected completion list but got nil."});
		}
		return;
	} else if (expected == nullptr) {
		if (!actual->Items.has_value() || actual->Items->empty()) {
			return;
		}
		// !!! cmp.Diff(actual, nil) should probably be a .String() call
		// here and elswhere
		t->Fatalf(
		    prefix + "Expected nil completion list but got non-nil: %s",
		    {tsc::cmp::diff(actual, nullptr)});
	}
	assertEqual(t, actual->IsIncomplete, expected->IsIncomplete,
	            prefix + "IsIncomplete mismatch");
	verifyCompletionsItemDefaults(t, actual->ItemDefaults,
	                              expected->ItemDefaults.get(),
	                              prefix + "ItemDefaults mismatch: ");
	verifyCompletionsItems(t, prefix, *actual->Items, expected->Items.get());
}

// isEmptyExpectedList — fourslash.go:1371.
bool isEmptyExpectedList(const CompletionsExpectedList* expected) {
	return expected == nullptr ||
	       (expected->Items->Exact.empty() &&
	        expected->Items->Includes.empty() &&
	        expected->Items->Excludes.empty() &&
	        expected->Items->Unsorted.empty());
}

// verifyCompletionsItemDefaults — fourslash.go:1375.
void verifyCompletionsItemDefaults(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::CompletionItemDefaults>& actual,
    const CompletionsExpectedItemDefaults* expected,
    const std::string& prefix) {
	if (actual == nullptr) {
		if (expected == nullptr) {
			return;
		}
		t->Fatalf(
		    prefix +
		        "Expected non-nil completion item defaults but got nil: %s",
		    {tsc::cmp::diff(actual, nullptr)});
	}
	if (expected == nullptr) {
		t->Fatalf(
		    prefix +
		        "Expected nil completion item defaults but got non-nil: %s",
		    {tsc::cmp::diff(actual, nullptr)});
	}
	assertDeepEqual(t, actual->CommitCharacters,
	                expected->CommitCharacters,
	                prefix + "CommitCharacters mismatch:");
	if (auto* editRange = std::get_if<std::shared_ptr<EditRange>>(
	        &expected->EditRange)) {
		if (actual->EditRange == nullptr) {
			t->Fatal({prefix + "Expected non-nil EditRange but got nil"});
		}
		lsproto::Range expectedInsert = (*editRange)->Insert->LSRange;
		lsproto::Range expectedReplace = (*editRange)->Replace->LSRange;
		lsproto::RangeOrEditRangeWithInsertReplace expectedRange;
		expectedRange.EditRangeWithInsertReplace = std::make_shared<
		    lsproto::EditRangeWithInsertReplace>(
		    lsproto::EditRangeWithInsertReplace{
		        .Insert = expectedInsert,
		        .Replace = expectedReplace,
		    });
		assertDeepEqual(t, actual->EditRange,
		                std::make_shared<
		                    lsproto::RangeOrEditRangeWithInsertReplace>(
		                    expectedRange),
		                prefix + "EditRange mismatch:");
	} else if (std::holds_alternative<std::monostate>(
	               expected->EditRange)) {
		if (actual->EditRange != nullptr) {
			t->Fatalf(
			    prefix +
			        "Expected nil EditRange but got non-nil: %s",
			    {tsc::cmp::diff(actual->EditRange, nullptr)});
		}
	} else if (std::holds_alternative<Ignored>(expected->EditRange)) {
		// The edit range is intentionally ignored.
	} else {
		t->Fatalf(
		    prefix + "Expected EditRange to be *EditRange or Ignored, "
		             "got %T",
		    {gostr::variantTypeName(expected->EditRange)});
	}
}

// verifyCompletionsItems — fourslash.go:1422.
void FourslashTest::verifyCompletionsItems(
    gostd::testing::T* t, const std::string& prefix,
    const std::vector<std::shared_ptr<lsproto::CompletionItem>>& actual,
    const CompletionsExpectedItems* expected) {
	if (!expected->Exact.empty()) {
		if (!expected->Includes.empty()) {
			t->Fatal({prefix +
			          "Expected exact completion list but also "
			          "specified 'includes'."});
		}
		if (!expected->Excludes.empty()) {
			t->Fatal({prefix +
			          "Expected exact completion list but also "
			          "specified 'excludes'."});
		}
		if (!expected->Unsorted.empty()) {
			t->Fatal({prefix +
			          "Expected exact completion list but also "
			          "specified 'unsorted'."});
		}
		if (actual.size() != expected->Exact.size()) {
			t->Fatalf(prefix +
			              "Expected %d exact completion items but got "
			              "%d.",
			          {expected->Exact.size(), actual.size()});
		}
		if (!actual.empty()) {
			verifyCompletionsAreExactly(t, prefix, actual,
			                            expected->Exact);
		}
		return;
	}
	std::unordered_map<
	    std::string,
	    std::vector<std::shared_ptr<lsproto::CompletionItem>>>
	    nameToActualItems;
	for (auto& item : actual) {
		nameToActualItems[item->Label].push_back(item);
	}
	if (!expected->Unsorted.empty()) {
		if (!expected->Includes.empty()) {
			t->Fatal({prefix +
			          "Expected unsorted completion list but also "
			          "specified 'includes'."});
		}
		if (!expected->Excludes.empty()) {
			t->Fatal({prefix +
			          "Expected unsorted completion list but also "
			          "specified 'excludes'."});
		}
		for (auto& expectedItem : expected->Unsorted) {
			if (auto* label = std::get_if<std::string>(&expectedItem)) {
				auto it = nameToActualItems.find(*label);
				if (it == nameToActualItems.end()) {
					t->Fatalf(
					    "%sLabel '%s' not found in actual items.",
					    {prefix, *label});
				}
				nameToActualItems.erase(it);
			} else if (auto* item = std::get_if<
			               std::shared_ptr<lsproto::CompletionItem>>(
			               &expectedItem)) {
				auto it = nameToActualItems.find((*item)->Label);
				if (it == nameToActualItems.end()) {
					t->Fatalf(
					    "%sLabel '%s' not found in actual items.",
					    {prefix, (*item)->Label});
				}
				auto& actualItems = it->second;
				std::string mismatchPrefix;
				if (actualItems.size() > 1) {
					mismatchPrefix = prefix +
					                 "No completion item match for "
					                 "label " +
					                 (*item)->Label +
					                 " (multiple candidates found): ";
				} else {
					mismatchPrefix = prefix +
					                 "Includes completion item "
					                 "mismatch for label " +
					                 (*item)->Label + ": ";
				}
				// core.FindIndex over actualItems
				int itemIndex = -1;
				for (size_t ai = 0; ai < actualItems.size(); ai++) {
					std::string err = verifyCompletionItem(
					    t, prefix, actualItems[ai], *item);
					if (!err.empty()) {
						mismatchPrefix += "\n    " + err;
						continue;
					}
					itemIndex = (int)ai;
					break;
				}

				// fail test if no match found
				if (itemIndex == -1) {
					t->Fatal({mismatchPrefix});
				}

				if (actualItems.size() == 1) {
					nameToActualItems.erase(it);
				} else if (itemIndex == 0) {
					nameToActualItems[(*item)->Label].assign(
					    actualItems.begin() + 1, actualItems.end());
				} else if (itemIndex == (int)actualItems.size() - 1) {
					nameToActualItems[(*item)->Label].assign(
					    actualItems.begin(),
					    actualItems.begin() + itemIndex);
				} else {
					std::vector<
					    std::shared_ptr<lsproto::CompletionItem>>
					    rest;
					rest.insert(rest.end(), actualItems.begin(),
					            actualItems.begin() + itemIndex);
					rest.insert(rest.end(),
					            actualItems.begin() + itemIndex + 1,
					            actualItems.end());
					nameToActualItems[(*item)->Label] =
					    std::move(rest);
				}
			} else {
				t->Fatalf(
				    "%sExpected completion item to be a string or "
				    "*lsproto.CompletionItem, got %T",
				    {prefix,
				     gostr::variantTypeName(expectedItem)});
			}
		}
		if (expected->Unsorted.size() != actual.size()) {
			std::vector<std::string> unmatched;
			for (auto& [k, v] : nameToActualItems) {
				unmatched.push_back(k);
			}
			t->Fatalf(
			    "%sAdditional completions found but not included in "
			    "'unsorted': %s",
			    {prefix, gostr::join(unmatched, "\n")});
		}
		return;
	}
	if (!expected->Includes.empty()) {
		for (auto& expectedItem : expected->Includes) {
			if (auto* label = std::get_if<std::string>(&expectedItem)) {
				if (nameToActualItems.find(*label) ==
				    nameToActualItems.end()) {
					t->Fatalf(
					    "%sLabel '%s' not found in actual items.",
					    {prefix, *label});
				}
			} else if (auto* item = std::get_if<
			               std::shared_ptr<lsproto::CompletionItem>>(
			               &expectedItem)) {
				auto it = nameToActualItems.find((*item)->Label);
				if (it == nameToActualItems.end()) {
					t->Fatalf(
					    "%sLabel '%s' not found in actual items.",
					    {prefix, (*item)->Label});
				}
				auto& actualItems = it->second;
				std::string mismatchPrefix;
				if (actualItems.size() > 1) {
					mismatchPrefix = prefix +
					                 "No completion item match for "
					                 "label " +
					                 (*item)->Label +
					                 " (multiple candidates found): ";
				} else {
					mismatchPrefix = prefix +
					                 "Includes completion item "
					                 "mismatch for label " +
					                 (*item)->Label + ": ";
				}
				int itemIndex = -1;
				for (size_t ai = 0; ai < actualItems.size(); ai++) {
					std::string err = verifyCompletionItem(
					    t, prefix, actualItems[ai], *item);
					if (!err.empty()) {
						mismatchPrefix += "\n    " + err;
						continue;
					}
					itemIndex = (int)ai;
					break;
				}

				// fail test if no match found
				if (itemIndex == -1) {
					t->Fatal({mismatchPrefix});
				}

				// delete previous entries since we verify entries in
				// order
				if (actualItems.size() == 1 ||
				    itemIndex == (int)actualItems.size() - 1) {
					nameToActualItems.erase(it);
				} else {
					nameToActualItems[(*item)->Label].assign(
					    actualItems.begin() + itemIndex,
					    actualItems.end());
				}
			} else {
				t->Fatalf(
				    "%sExpected completion item to be a string or "
				    "*lsproto.CompletionItem, got %T",
				    {prefix,
				     gostr::variantTypeName(expectedItem)});
			}
		}
	}
	for (auto& exclude : expected->Excludes) {
		if (nameToActualItems.find(exclude) != nameToActualItems.end()) {
			t->Fatalf(
			    "%sLabel '%s' should not be in actual items but was "
			    "found.",
			    {prefix, exclude});
		}
	}
}

// verifyCompletionsAreExactly — fourslash.go:1552.
void FourslashTest::verifyCompletionsAreExactly(
    gostd::testing::T* t, const std::string& prefix,
    const std::vector<std::shared_ptr<lsproto::CompletionItem>>& actual,
    const std::vector<CompletionsExpectedItem>& expected) {
	std::string labelMismatchPrefix = prefix + "Label mismatch";
	for (size_t i = 0; i < actual.size(); i++) {
		auto& actualItem = actual[i];
		auto& expectedItem = expected[i];
		if (auto* label = std::get_if<std::string>(&expectedItem)) {
			assertDeepEqual(t, actualItem->Label, *label,
			                labelMismatchPrefix);
		} else if (auto* expectedItem_ = std::get_if<
		               std::shared_ptr<lsproto::CompletionItem>>(
		               &expectedItem)) {
			assertDeepEqual(t, actualItem->Label,
			                (*expectedItem_)->Label,
			                labelMismatchPrefix);
			std::string itemPrefix = prefix +
			                         "Completion item mismatch for "
			                         "label " +
			                         actualItem->Label;
			if (std::string err = verifyCompletionItem(
			        t, itemPrefix, actualItem, *expectedItem_);
			    !err.empty()) {
				t->Fatalf("%s:\n%s", {itemPrefix, err});
			}
		} else {
			t->Fatalf(
			    "Expected completion item to be a string or "
			    "*lsproto.CompletionItem, got %T",
			    {gostr::variantTypeName(expectedItem)});
		}
	}
}

// ignorePaths — fourslash.go:1565.
tsc::cmp::Option ignorePaths(std::initializer_list<std::string> paths) {
	return tsc::cmp::ignorePaths(paths);
}

// fourslash.go:1571 — go-cmp ignore sets.
const tsc::cmp::Option completionIgnoreOpts = tsc::cmp::ignorePaths(
    {".Kind", ".SortText", ".FilterText", ".Data",
     ".AdditionalTextEdits"});
const tsc::cmp::Option autoImportIgnoreOpts = tsc::cmp::ignorePaths(
    {".Kind", ".SortText", ".FilterText", ".Data", ".LabelDetails",
     ".Detail", ".AdditionalTextEdits"});
const tsc::cmp::Option diagnosticsIgnoreOpts = tsc::cmp::ignorePaths(
    {".Severity", ".Source", ".RelatedInformation"});

// verifyCompletionItem — fourslash.go:1575.
std::string FourslashTest::verifyCompletionItem(
    gostd::testing::T* t, const std::string& prefix,
    std::shared_ptr<lsproto::CompletionItem> actual,
    const std::shared_ptr<lsproto::CompletionItem>& expected) {
	// returns error message if not matched
	t->Helper();
	std::shared_ptr<lsproto::AutoImportFix> actualAutoImportFix;
	std::shared_ptr<lsproto::AutoImportFix> expectedAutoImportFix;
	if (actual->Data != nullptr) {
		actualAutoImportFix = actual->Data->AutoImport;
	}
	if (expected->Data != nullptr) {
		expectedAutoImportFix = expected->Data->AutoImport;
	}
	if ((actualAutoImportFix == nullptr) !=
	    (expectedAutoImportFix == nullptr)) {
		return "Mismatch in auto-import data presence";
	}

	if (expected->Detail.has_value() ||
	    expected->Documentation != nullptr ||
	    actualAutoImportFix != nullptr) {
		actual = resolveCompletionItem(t, actual);
	}

	if (actualAutoImportFix != nullptr) {
		if (std::string err =
		        tsc::cmp::diff(actual, expected, autoImportIgnoreOpts);
		    err != "") {
			return err;
		}
		if (expected->AdditionalTextEdits == AnyTextEdits) {
			if (!(actual->AdditionalTextEdits != nullptr &&
			      !(*actual->AdditionalTextEdits)->empty())) {
				return "Expected non-nil AdditionalTextEdits for "
				       "auto-import completion item";
			}
		} else if (expected->AdditionalTextEdits == NoTextEdits) {
			if (actual->AdditionalTextEdits != nullptr &&
			    !(*actual->AdditionalTextEdits)->empty()) {
				return "Expected no AdditionalTextEdits for "
				       "auto-import completion item";
			}
		}
		if (expected->LabelDetails != nullptr) {
			if (std::string err = tsc::cmp::diff(
			        actual->LabelDetails, expected->LabelDetails);
			    err != "") {
				return gostd::sprintf("%s:\n%s",
				                      {"LabelDetailsMismatch", err});
			}
		}
		if (actualAutoImportFix->ModuleSpecifier !=
		    expectedAutoImportFix->ModuleSpecifier) {
			return "ModuleSpecifier mismatch";
		}
	} else {
		if (std::string err = tsc::cmp::diff(actual, expected,
		                                     completionIgnoreOpts);
		    err != "") {
			return err;
		}
		if (expected->AdditionalTextEdits == AnyTextEdits) {
			if (actual->AdditionalTextEdits == nullptr ||
			    (*actual->AdditionalTextEdits)->empty()) {
				return "Expected non-empty AdditionalTextEdits for "
				       "completion item";
			}
		} else {
			if (std::string err =
			        tsc::cmp::diff(actual->AdditionalTextEdits,
			                       expected->AdditionalTextEdits);
			    err != "") {
				return gostd::sprintf("%s:\n%s",
				                      {"AdditionalTextEdits mismatch",
				                       err});
			}
		}
	}

	if (expected->FilterText.has_value()) {
		if (std::string err = tsc::cmp::diff(actual->FilterText,
		                                     expected->FilterText);
		    err != "") {
			return gostd::sprintf("%s:\n%s",
			                      {"FilterText mismatch", err});
		}
	}
	if (expected->Kind != nullptr) {
		if (std::string err =
		        tsc::cmp::diff(actual->Kind, expected->Kind);
		    err != "") {
			return gostd::sprintf("%s:\n%s",
			                      {"Kind mismatch", err});
		}
	}
	if (std::string err = tsc::cmp::diff(
	        actual->SortText,
	        gostr::orElse(expected->SortText,
	                      std::optional<std::string>(
	                          std::string(
	                              ls::SortTextLocationPriority))));
	    err != "") {
		return gostd::sprintf("%s:\n%s", {"SortText mismatch", err});
	}

	return "";
}

// ResolveCompletionItem — fourslash.go:1656.
std::shared_ptr<lsproto::CompletionItem>
FourslashTest::ResolveCompletionItem(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::CompletionItem>& item) {
	t->Helper();
	return resolveCompletionItem(t, item);
}

// resolveCompletionItem — fourslash.go:1661.
std::shared_ptr<lsproto::CompletionItem>
FourslashTest::resolveCompletionItem(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::CompletionItem>& item) {
	auto result = sendRequest(t, lsproto::CompletionItemResolveInfo, item);
	return result;
}

// assertDeepEqual — fourslash.go:1666.
// (templated; see bottom of fourslash.h)


// ===========================================================================
// fourslash.go:1669-1679 — VerifyCodeFixOptions / VerifyCodeFixAllOptions
// (struct decls live in fourslash.h)
// ===========================================================================

// VerifyCodeFix verifies that applying a code fix produces the expected
// file content. fourslash.go:1686.
void FourslashTest::VerifyCodeFix(gostd::testing::T* t,
                                  const VerifyCodeFixOptions& options) {
	t->Helper();

	std::function<void()> reset;
	if (options.UserPreferences != nullptr) {
		reset = ConfigureWithReset(t, *options.UserPreferences);
	}

	auto actions = getCodeFixActions(t, {});

	if (actions.empty()) {
		t->Fatalf("No code fixes returned.", {});
	}
	if (options.Index >= (int)actions.size()) {
		t->Fatalf("Code fix index %d out of range (got %d fixes)",
		          {options.Index, actions.size()});
	}

	auto matchingAction = actions[options.Index];
	if (matchingAction->Title != options.Description) {
		bool found = false;
		for (auto& action : actions) {
			if (action->Title == options.Description) {
				matchingAction = action;
				found = true;
				break;
			}
		}
		if (!found) {
			std::vector<std::string> titles;
			for (auto& a : actions) {
				titles.push_back(a->Title);
			}
			t->Fatalf(
			    "No code fix with description %q at index %d found. "
			    "Available fixes: %v",
			    {options.Description, options.Index,
			     gostr::join(titles, ", ")});
		}
	}

	std::string originalContent =
	    getScriptInfo(activeFilename)->content;
	std::string expectedContent = options.NewFileContent;
	if (!options.NewRangeContent.empty()) {
		TextRange selection = getSelection();
		if (selection.pos() == selection.end()) {
			auto ranges = getRangesInFile(activeFilename);
			if (ranges.empty()) {
				t->Fatal(
				    {"Expected a selected range or fourslash range for "
				     "NewRangeContent verification."});
			}
			selection = ranges[0]->Range;
		}
		expectedContent =
		    originalContent.substr(0, selection.pos()) +
		    options.NewRangeContent +
		    originalContent.substr(selection.end());
	}

	if (options.ApplyChanges) {
		if (matchingAction->Edit != nullptr &&
		    matchingAction->Edit->Changes != nullptr) {
			lsproto::DocumentUri expectedURI =
			    lsconv::FileNameToDocumentURI(activeFilename);
			for (auto& [uri, edits] : *matchingAction->Edit->Changes) {
				if (uri != expectedURI) {
					t->Fatalf(
					    "Code fix returned edits for unexpected URI "
					    "%q (expected %q)",
					    {uri, expectedURI});
				}
				applyTextEdits(t, *edits);
			}
		}
		std::string actual =
		    getScriptInfo(activeFilename)->content;
		assertEqual(
		    t, expectedContent, actual,
		    "File content after applying code fix did not match "
		    "expected content.");
	} else {
		std::string actual =
		    getScriptInfo(activeFilename)->content;
		if (matchingAction->Edit != nullptr &&
		    matchingAction->Edit->Changes != nullptr) {
			lsproto::DocumentUri expectedURI =
			    lsconv::FileNameToDocumentURI(activeFilename);
			for (auto& [uri, edits] : *matchingAction->Edit->Changes) {
				if (uri != expectedURI) {
					t->Fatalf(
					    "Code fix returned edits for unexpected URI "
					    "%q (expected %q)",
					    {uri, expectedURI});
				}
				actual = applyEditsToContent(actual, *edits);
			}
		}
		assertEqual(
		    t, expectedContent, actual,
		    "File content after applying code fix did not match "
		    "expected content.");
	}
	if (reset) reset();
}

// VerifyRangeAfterCodeFix — fourslash.go:1765.
void FourslashTest::VerifyRangeAfterCodeFix(
    gostd::testing::T* t, const std::string& expectedText,
    bool includeWhitespace, int errorCode, int index) {
	t->Helper();

	auto actions = getCodeFixActions(t, {errorCode});
	if (actions.empty()) {
		t->Fatalf("No code fixes returned.", {});
	}

	if (index >= (int)actions.size()) {
		t->Fatalf("Code fix index %d out of range (got %d fixes)",
		          {index, actions.size()});
	}

	auto action = actions[index];
	auto ranges = getRangesInFile(activeFilename);
	if (ranges.size() != 1) {
		t->Fatalf("Expected exactly one range in %q, got %d.",
		          {activeFilename, ranges.size()});
	}

	auto edits = getCodeActionEditsForActiveFile(t, action);
	TextRange updatedRange =
	    updateTextRangeForTextEdits(ranges[0]->Range, edits);
	assertValidTextRange(
	    t, updatedRange,
	    gostd::sprintf(
	        "Code fix %q replaced part of the expected range; unable to "
	        "compute rangeAfterCodeFix result.",
	        {action->Title}));

	applyTextEdits(t, edits);
	std::string actualContent =
	    getScriptInfo(activeFilename)->content;
	std::string actualText = actualContent.substr(
	    updatedRange.pos(), updatedRange.len());

	if (includeWhitespace) {
		assertEqual(
		    t, expectedText, actualText,
		    "Range content after applying code fix did not match "
		    "expected content.");
		return;
	}

	actualText = removeWhitespace(actualText);
	std::string expectedTextStripped = removeWhitespace(expectedText);
	assertEqual(
	    t, expectedTextStripped, actualText,
	    "Range content after applying code fix did not match expected "
	    "content.");
}

// getCodeActionEditsForActiveFile — fourslash.go:1792.
std::vector<std::shared_ptr<lsproto::TextEdit>>
FourslashTest::getCodeActionEditsForActiveFile(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::CodeAction>& action) {
	t->Helper();
	if (action->Edit == nullptr || action->Edit->Changes == nullptr) {
		t->Fatalf("Code fix %q did not return text edits.",
		          {action->Title});
	}
	if ((*action->Edit->Changes).size() != 1) {
		t->Fatalf(
		    "Code fix %q returned edits for multiple files; "
		    "rangeAfterCodeFix expects only the active file.",
		    {action->Title});
	}

	auto it = (*action->Edit->Changes)
	              .find(lsconv::FileNameToDocumentURI(activeFilename));
	if (it != (*action->Edit->Changes).end()) {
		return *it->second;
	}
	t->Fatalf("Code fix %q did not return edits for active file %q.",
	          {action->Title, activeFilename});
	TSC_UNREACHABLE("unreachable");
}

// VerifyCodeFixAvailable verifies that code fixes with the given
// descriptions are available. fourslash.go:1809.
void FourslashTest::VerifyCodeFixAvailable(
    gostd::testing::T* t,
    const std::optional<std::vector<std::string>>&
        expectedDescriptions) {
	t->Helper();

	auto actions = getCodeFixActions(t, {});

	if (!expectedDescriptions.has_value()) {
		if (actions.empty()) {
			t->Fatalf(
			    "Expected code fixes to be available, but got none.",
			    {});
		}
		return;
	}

	if (expectedDescriptions->empty()) {
		VerifyCodeFixNotAvailable(t, {});
		return;
	}

	for (auto& expected : *expectedDescriptions) {
		bool found = false;
		for (auto& action : actions) {
			if (action->Title == expected) {
				found = true;
				break;
			}
		}
		if (!found) {
			std::vector<std::string> titles;
			for (auto& a : actions) {
				titles.push_back(a->Title);
			}
			t->Fatalf(
			    "Expected code fix with description %q not found. "
			    "Available fixes: %v",
			    {expected, gostr::join(titles, ", ")});
		}
	}
}

// VerifyCodeFixNotAvailable — fourslash.go:1844.
void FourslashTest::VerifyCodeFixNotAvailable(
    gostd::testing::T* t,
    const std::vector<std::string>& expected) {
	t->Helper();

	auto actions = getCodeFixActions(t, {});
	if (expected.empty()) {
		if (actions.empty()) {
			return;
		}

		std::vector<std::string> titles;
		for (auto& action : actions) {
			titles.push_back(action->Title);
		}
		t->Fatalf("Expected no code fixes, but got: %v",
		          {gostr::join(titles, ", ")});
	}
	for (auto& title : expected) {
		for (auto& action : actions) {
			if (action->Title == title) {
				t->Fatalf(
				    "Expected code fix with description %q not to be "
				    "available.",
				    {title});
			}
		}
	}
}

// VerifyCodeFixAvailableExact verifies that the exact set of code fix
// descriptions matches. Unlike VerifyCodeFixAvailable, this checks both
// that all expected descriptions are present and that no additional
// unexpected code fixes exist (exact count match). fourslash.go:1873.
void FourslashTest::VerifyCodeFixAvailableExact(
    gostd::testing::T* t,
    const std::vector<std::string>& expectedDescriptions) {
	t->Helper();

	auto actions = getCodeFixActions(t, {});

	if (actions.size() != expectedDescriptions.size()) {
		std::vector<std::string> titles;
		for (auto& a : actions) {
			titles.push_back(a->Title);
		}
		t->Fatalf(
		    "Expected exactly %d code fixes, but got %d. Available "
		    "fixes: %v",
		    {expectedDescriptions.size(), actions.size(),
		     gostr::join(titles, ", ")});
	}

	for (auto& expected : expectedDescriptions) {
		bool found = false;
		for (auto& action : actions) {
			if (action->Title == expected) {
				found = true;
				break;
			}
		}
		if (!found) {
			std::vector<std::string> titles;
			for (auto& a : actions) {
				titles.push_back(a->Title);
			}
			t->Fatalf(
			    "Expected code fix with description %q not found. "
			    "Available fixes: %v",
			    {expected, gostr::join(titles, ", ")});
		}
	}
}

// VerifyCodeFixAll verifies that applying all code fixes with the given
// fixId produces the expected file content. It gets all quickfix code
// actions for the file (which includes per-fixId "Fix all" entries when
// multiple diagnostics match the same provider), finds the fix-all
// entry, and applies its edits. fourslash.go:1903.
void FourslashTest::VerifyCodeFixAll(
    gostd::testing::T* t, const VerifyCodeFixAllOptions& options) {
	t->Helper();

	auto actions = getAllQuickFixActions(t, {});
	if (actions.empty()) {
		t->Fatalf("No code fixes available for fixId %q",
		          {options.FixID});
	}

	// Find fix-all actions. The server returns these as quickfix entries
	// with titles like "Add all missing imports" when multiple
	// diagnostics match the same provider. We look for actions that are
	// NOT single-diagnostic fixes (i.e., have no Diagnostics attached).
	std::vector<std::shared_ptr<lsproto::CodeAction>> fixAllCandidates;
	for (auto& action : actions) {
		if (action->Diagnostics == nullptr ||
		    (*action->Diagnostics)->empty()) {
			fixAllCandidates.push_back(action);
		}
	}

	std::shared_ptr<lsproto::CodeAction> fixAllAction;
	if (fixAllCandidates.size() == 1) {
		fixAllAction = fixAllCandidates[0];
	} else {
		// If there are multiple fix-all candidates, match by FixID in
		// the title.
		for (auto& action : fixAllCandidates) {
			if (gostr::contains(gostr::toLower(action->Title),
			                    gostr::toLower(options.FixID))) {
				fixAllAction = action;
				break;
			}
		}
	}

	if (fixAllAction == nullptr) {
		std::vector<std::string> titles;
		for (auto& a : actions) {
			titles.push_back(a->Title);
		}
		t->Fatalf(
		    "No fix-all code action found for fixId %q. Available "
		    "fixes: %v",
		    {options.FixID, gostr::join(titles, ", ")});
	}

	if (fixAllAction->Edit != nullptr &&
	    fixAllAction->Edit->Changes != nullptr) {
		lsproto::DocumentUri expectedURI =
		    lsconv::FileNameToDocumentURI(activeFilename);
		for (auto& [uri, edits] : *fixAllAction->Edit->Changes) {
			if (uri != expectedURI) {
				t->Fatalf(
				    "Fix-all code action returned edits for "
				    "unexpected URI %q (expected %q)",
				    {uri, expectedURI});
			}
			applyTextEdits(t, *edits);
		}
	}

	std::string actual = getScriptInfo(activeFilename)->content;
	assertEqual(
	    t, options.NewFileContent, actual,
	    "File content after applying all code fixes did not match "
	    "expected content.");
}

// VerifySourceFixAll verifies that requesting a source.fixAll code
// action produces the expected file content. This tests the on-save
// code path where VS Code requests source.fixAll. fourslash.go:1964.
void FourslashTest::VerifySourceFixAll(gostd::testing::T* t,
                                       const std::string& expectedContent) {
	t->Helper();

	auto only = std::make_shared<lsproto::Slice<lsproto::CodeActionKind>>(
	    std::vector<lsproto::CodeActionKind>{
	        lsproto::CodeActionKindSourceFixAll});
	auto params = std::make_shared<lsproto::CodeActionParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Range.Start = currentCaretPosition;
	params->Range.End = currentCaretPosition;
	params->Context = std::make_shared<lsproto::CodeActionContext>();
	params->Context->Diagnostics =
	    std::vector<std::shared_ptr<lsproto::Diagnostic>>{};
	params->Context->Only = only;
	auto result = sendRequest(t, lsproto::TextDocumentCodeActionInfo,
	                          params);

	if (result.CommandOrCodeActionArray == nullptr) {
		t->Fatalf("No source.fixAll code actions returned", {});
	}

	std::shared_ptr<lsproto::CodeAction> selected;
	for (auto& item : orNilSlice(result.CommandOrCodeActionArray)) {
		if (item.CodeAction == nullptr ||
		    item.CodeAction->Kind == nullptr ||
		    *item.CodeAction->Kind !=
		        lsproto::CodeActionKindSourceFixAllTs) {
			continue;
		}
		selected = item.CodeAction;
		break;
	}

	if (selected == nullptr) {
		t->Fatalf("No source.fixAll code action found", {});
	}
	if (selected->Edit != nullptr &&
	    selected->Edit->Changes != nullptr) {
		lsproto::DocumentUri expectedURI =
		    lsconv::FileNameToDocumentURI(activeFilename);
		for (auto& [uri, edits] : *selected->Edit->Changes) {
			if (uri != expectedURI) {
				t->Fatalf(
				    "source.fixAll returned edits for unexpected URI "
				    "%q (expected %q)",
				    {uri, expectedURI});
			}
			applyTextEdits(t, *edits);
		}
	}

	std::string actual = getScriptInfo(activeFilename)->content;
	assertEqual(
	    t, expectedContent, actual,
	    "File content after source.fixAll did not match expected "
	    "content.");
}

// getCodeFixActions gets per-diagnostic quick fix code actions,
// excluding fix-all entries. fourslash.go:2016.
std::vector<std::shared_ptr<lsproto::CodeAction>>
FourslashTest::getCodeFixActions(
    gostd::testing::T* t, const std::vector<int>& errorCodes) {
	t->Helper();
	auto all = getAllQuickFixActions(t, errorCodes);
	// Filter to only per-diagnostic fixes (those with diagnostics
	// attached)
	std::vector<std::shared_ptr<lsproto::CodeAction>> actions;
	for (auto& action : all) {
		if (action->Diagnostics != nullptr &&
		    !(*action->Diagnostics)->empty()) {
			actions.push_back(action);
		}
	}
	return actions;
}

// getAllQuickFixActions gets all quick fix code actions including
// fix-all entries. fourslash.go:2029.
std::vector<std::shared_ptr<lsproto::CodeAction>>
FourslashTest::getAllQuickFixActions(
    gostd::testing::T* t, const std::vector<int>& errorCodes) {
	t->Helper();

	auto diagParams =
	    std::make_shared<lsproto::DocumentDiagnosticParams>();
	diagParams->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	auto diagResult = sendRequest(
	    t, lsproto::TextDocumentDiagnosticInfo, diagParams);

	std::vector<std::shared_ptr<lsproto::Diagnostic>> diagnostics;
	if (diagResult.FullDocumentDiagnosticReport != nullptr &&
	    diagResult.FullDocumentDiagnosticReport->Items.has_value()) {
		diagnostics =
		    *diagResult.FullDocumentDiagnosticReport->Items;
	}

	if (diagnostics.empty()) {
		return {};
	}

	auto diagnostic = selectCodeFixDiagnostic(
	    diagnostics, gostr::firstOrNil(errorCodes));
	if (diagnostic == nullptr) {
		return {};
	}

	auto params = std::make_shared<lsproto::CodeActionParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Range.Start = diagnostic->Range.Start;
	params->Range.End = diagnostic->Range.End;
	params->Context = std::make_shared<lsproto::CodeActionContext>();
	params->Context->Diagnostics = diagnostics;
	auto result = sendRequest(t, lsproto::TextDocumentCodeActionInfo,
	                          params);

	std::vector<std::shared_ptr<lsproto::CodeAction>> actions;
	if (result.CommandOrCodeActionArray != nullptr) {
		for (auto& item : orNilSlice(result.CommandOrCodeActionArray)) {
			if (item.CodeAction != nullptr &&
			    item.CodeAction->Kind != nullptr &&
			    *item.CodeAction->Kind ==
			        lsproto::CodeActionKindQuickFix) {
				actions.push_back(item.CodeAction);
			}
		}
	}

	return actions;
}

// updateTextRangeForTextEdits — fourslash.go:2075.
TextRange FourslashTest::updateTextRangeForTextEdits(
    TextRange textRange,
    const std::vector<std::shared_ptr<lsproto::TextEdit>>& edits) {
	auto script = getScriptInfo(activeFilename);
	std::vector<textEditSpan> spans;
	spans.reserve(edits.size());
	for (auto& edit : edits) {
		spans.push_back(textEditSpan{
		    .start = (int)converters->LineAndCharacterToPosition(
		        script, edit->Range.Start),
		    .end = (int)converters->LineAndCharacterToPosition(
		        script, edit->Range.End),
		    .length = (int)edit->NewText.size(),
		});
	}
	// stable_sort: Go's slices.SortFunc (pdqsort) is not documented stable,
	// but our oracle testing showed deterministic tie-order matching
	// std::stable_sort — matching observed behavior is the safe port.
	// (same-position edits must keep insertion order).
	std::stable_sort(spans.begin(), spans.end(),
	          [](const textEditSpan& a, const textEditSpan& b) {
		          return a.start < b.start;
	          });

	auto pos = (int)textRange.pos();
	auto end = (int)textRange.end();
	for (size_t i = 0; i < spans.size(); i++) {
		auto& edit = spans[i];
		pos = updatePositionForTextEdit(pos, edit.start, edit.end,
		                                edit.length);
		end = updatePositionForTextEdit(end, edit.start, edit.end,
		                                edit.length);

		int delta = edit.length - (edit.end - edit.start);
		for (size_t j = i + 1; j < spans.size(); j++) {
			if (spans[j].start >= edit.start) {
				spans[j].start += delta;
				spans[j].end += delta;
			}
		}
	}
	return TextRange{(TextPos)pos, (TextPos)end};
}

// applyEditsToContent applies text edits to a content string without
// mutating the file. fourslash.go:2102.
std::string FourslashTest::applyEditsToContent(
    std::string content,
    std::vector<std::shared_ptr<lsproto::TextEdit>> edits) {
	auto script = getScriptInfo(activeFilename);
	// stable_sort: Go's slices.SortFunc (pdqsort) ties preserve insertion
	// order on observed inputs
	// equal keys; std::sort scrambles same-position edits
	// deterministically (TestCodeFixClassImplementInterfaceMemberOrdering).
	std::stable_sort(edits.begin(), edits.end(),
	          [&](const std::shared_ptr<lsproto::TextEdit>& a,
	              const std::shared_ptr<lsproto::TextEdit>& b) {
		          auto aStart =
		              converters->LineAndCharacterToPosition(
		                  script, a->Range.Start);
		          auto bStart =
		              converters->LineAndCharacterToPosition(
		                  script, b->Range.Start);
		          return aStart < bStart;
	          });
	for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
		auto& edit = *it;
		int start = (int)converters->LineAndCharacterToPosition(
		    script, edit->Range.Start);
		int end = (int)converters->LineAndCharacterToPosition(
		    script, edit->Range.End);
		content = content.substr(0, start) + edit->NewText +
		          content.substr(end);
	}
	return content;
}

// VerifyOrganizeImports — fourslash.go:2118.
void FourslashTest::VerifyOrganizeImports(
    gostd::testing::T* t, const std::string& expectedContent,
    const lsproto::CodeActionKind& codeActionKind,
    const std::shared_ptr<lsutil::UserPreferences>& preferences) {
	t->Helper();
	verifyOrganizeImports(t, expectedContent, codeActionKind,
	                      codeActionKind, preferences);
}

// VerifyOrganizeImportsWithRequestKind — fourslash.go:2123.
void FourslashTest::VerifyOrganizeImportsWithRequestKind(
    gostd::testing::T* t, const std::string& expectedContent,
    const lsproto::CodeActionKind& requestedKind,
    const lsproto::CodeActionKind& expectedKind,
    const std::shared_ptr<lsutil::UserPreferences>& preferences) {
	t->Helper();
	verifyOrganizeImports(t, expectedContent, requestedKind,
	                      expectedKind, preferences);
}

// verifyOrganizeImports — fourslash.go:2133.
void FourslashTest::verifyOrganizeImports(
    gostd::testing::T* t, const std::string& expectedContent,
    const lsproto::CodeActionKind& requestedKind,
    const lsproto::CodeActionKind& expectedKind,
    const std::shared_ptr<lsutil::UserPreferences>& preferences) {
	t->Helper();

	std::function<void()> reset;
	if (preferences != nullptr) {
		reset = ConfigureWithReset(t, *preferences);
	}

	auto params = std::make_shared<lsproto::CodeActionParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Range.Start = lsproto::Position{.Line = 0, .Character = 0};
	{
		auto script = getScriptInfo(activeFilename);
		params->Range.End = converters->PositionToLineAndCharacter(
		    script, (TextPos)script->content.size());
	}
	params->Context = std::make_shared<lsproto::CodeActionContext>();
	params->Context->Only =
	    std::make_shared<lsproto::Slice<lsproto::CodeActionKind>>(
	        std::vector<lsproto::CodeActionKind>{requestedKind});

	auto result = sendRequest(t, lsproto::TextDocumentCodeActionInfo,
	                          params);
	if (reset) reset();

	if (result.CommandOrCodeActionArray == nullptr ||
	    orNilSlice(result.CommandOrCodeActionArray).empty()) {
		t->Fatalf("No organize imports code action found", {});
	}

	std::shared_ptr<lsproto::CodeAction> organizeAction;
	for (auto& item : orNilSlice(result.CommandOrCodeActionArray)) {
		if (item.CodeAction != nullptr &&
		    item.CodeAction->Kind != nullptr &&
		    *item.CodeAction->Kind == expectedKind) {
			organizeAction = item.CodeAction;
			break;
		}
	}

	if (organizeAction == nullptr) {
		t->Fatalf("No organize imports code action found", {});
	}

	lsproto::DocumentUri expectedURI =
	    lsconv::FileNameToDocumentURI(activeFilename);
	if (organizeAction->Edit != nullptr &&
	    organizeAction->Edit->Changes != nullptr) {
		for (auto& [uri, edits] : *organizeAction->Edit->Changes) {
			if (uri != expectedURI) {
				t->Fatalf(
				    "Organize imports changed unexpected file: %s "
				    "(expected %s)",
				    {uri, expectedURI});
			}
			applyTextEdits(t, *edits);
		}
	}

	std::string actualContent =
	    getScriptInfo(activeFilename)->content;
	if (actualContent != expectedContent) {
		t->Fatalf(
		    "Organize imports result doesn't match.\nExpected:\n%s\n\n"
		    "Actual:\n%s",
		    {expectedContent, actualContent});
	}
}

// ApplyCodeActionFromCompletionOptions — fourslash.go:2208 (decl in
// fourslash.h).

// findCompletionForCodeAction — fourslash.go:2218.
std::shared_ptr<lsproto::CompletionItem>
FourslashTest::findCompletionForCodeAction(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<lsproto::CompletionItem>>& items,
    const std::string& description) {
	t->Helper();
	if (auto item = gostr::coreFind(
	        items,
	        [](const std::shared_ptr<lsproto::CompletionItem>& item) {
		        return item->AdditionalTextEdits != nullptr &&
		               !(*item->AdditionalTextEdits)->empty();
	        });
	    item != nullptr) {
		return item;
	}
	// core.FirstNonNil over resolveCompletionItem results.
	for (auto& item : items) {
		auto resolvedItem = resolveCompletionItem(t, item);
		if (resolvedItem == nullptr ||
		    resolvedItem->AdditionalTextEdits == nullptr ||
		    (*resolvedItem->AdditionalTextEdits)->empty()) {
			continue;
		}
		if (!description.empty() &&
		    (!resolvedItem->Detail.has_value() ||
		     !gostr::contains(*resolvedItem->Detail, description))) {
			continue;
		}
		return resolvedItem;
	}
	return nullptr;
}

// VerifyApplyCodeActionFromCompletion — fourslash.go:2237.
void FourslashTest::VerifyApplyCodeActionFromCompletion(
    gostd::testing::T* t, const std::shared_ptr<std::string>& markerName,
    const ApplyCodeActionFromCompletionOptions* options) {
	t->Helper();
	GoToMarker(t, *markerName);
	std::shared_ptr<lsutil::UserPreferences> userPreferences;
	if (options != nullptr && options->UserPreferences != nullptr) {
		userPreferences = options->UserPreferences;
	} else {
		// Default preferences: enables auto-imports
		userPreferences =
		    std::make_shared<lsutil::UserPreferences>(
		        lsutil::NewDefaultUserPreferences());
	}

	auto reset = ConfigureWithReset(t, *userPreferences);
	auto completionsList =
	    getCompletions(t, nullptr /*userPreferences*/); // Already
	// configured, so we do not need to pass it in again
	auto items = gostr::coreMapFiltered(
	    *completionsList->Items,
	    [&](const std::shared_ptr<lsproto::CompletionItem>& item)
	        -> std::optional<
	            std::shared_ptr<lsproto::CompletionItem>> {
		    if (item->Label != options->Name ||
		        item->Data == nullptr) {
			    return std::nullopt;
		    }

		    auto& data = item->Data;
		    if (options->AutoImportFix != nullptr) {
			    std::string moduleSpecifier =
			        options->AutoImportFix->ModuleSpecifier;
			    if (moduleSpecifier.empty()) {
				    moduleSpecifier = options->Source;
			    }
			    if (data->AutoImport != nullptr &&
			        data->AutoImport->ModuleSpecifier ==
			            moduleSpecifier) {
				    return item;
			    }
			    return std::nullopt;
		    }
		    if (data->AutoImport == nullptr &&
		        !data->Source.empty() &&
		        data->Source == options->Source) {
			    return item;
		    }
		    if (data->AutoImport != nullptr &&
		        data->AutoImport->ModuleSpecifier ==
		            options->Source) {
			    return item;
		    }
		    return std::nullopt;
	    });

	auto item = findCompletionForCodeAction(t, items,
	                                        options->Description);
	if (item == nullptr) {
		t->Fatalf("Code action '%s' from source '%s' not found.",
		          {options->Name, options->Source});
	}

	// apply the item to the test files
	applyTextEdits(t, orNilSlice(item->AdditionalTextEdits));
	if (options->NewFileContent != nullptr) {
		assertEqual(
		    t, getScriptInfo(activeFilename)->content,
		    *options->NewFileContent,
		    "File content after applying code action did not match "
		    "expected content.");
	} else if (options->NewRangeContent != nullptr) {
		t->Fatal({"!!! TODO"});
	}
	reset();
}

// VerifyImportFixAtPosition — fourslash.go:2285.
void FourslashTest::VerifyImportFixAtPosition(
    gostd::testing::T* t,
    const std::vector<std::string>& expectedTexts,
    const std::shared_ptr<lsutil::UserPreferences>& preferences) {
	t->Helper();
	std::string fileName = activeFilename;
	auto ranges = Ranges();
	std::vector<std::shared_ptr<RangeMarker>> filteredRanges;
	for (auto& r : ranges) {
		if (r->FileName() == fileName) {
			filteredRanges.push_back(r);
		}
	}
	if (filteredRanges.size() > 1) {
		t->Fatalf(
		    "Exactly one range should be specified in the testfile.",
		    {});
	}
	std::shared_ptr<RangeMarker> rangeMarker;
	if (filteredRanges.size() == 1) {
		rangeMarker = filteredRanges[0];
	}

	std::function<void()> reset;
	if (preferences != nullptr) {
		reset = ConfigureWithReset(t, *preferences);
	}

	// Get diagnostics at the current position to find errors that need
	// import fixes
	auto diagParams =
	    std::make_shared<lsproto::DocumentDiagnosticParams>();
	diagParams->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	auto diagResult = sendRequest(
	    t, lsproto::TextDocumentDiagnosticInfo, diagParams);

	std::vector<std::shared_ptr<lsproto::Diagnostic>> diagnostics;
	if (diagResult.FullDocumentDiagnosticReport != nullptr &&
	    diagResult.FullDocumentDiagnosticReport->Items.has_value()) {
		diagnostics =
		    *diagResult.FullDocumentDiagnosticReport->Items;
	}

	auto savedCaretPosition = currentCaretPosition;
	auto params = std::make_shared<lsproto::CodeActionParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Range.End = savedCaretPosition;
	params->Range.Start = savedCaretPosition;
	params->Context = std::make_shared<lsproto::CodeActionContext>();
	params->Context->Diagnostics = diagnostics;
	auto result = sendRequest(t, lsproto::TextDocumentCodeActionInfo,
	                          params);

	// Find all auto-import code actions (fixes with fixId/fixName
	// related to imports). Skip fix-all entries (those without
	// diagnostics attached)
	std::vector<std::shared_ptr<lsproto::CodeAction>> importActions;
	if (result.CommandOrCodeActionArray != nullptr) {
		for (auto& item : orNilSlice(result.CommandOrCodeActionArray)) {
			if (item.CodeAction != nullptr &&
			    item.CodeAction->Kind != nullptr &&
			    *item.CodeAction->Kind ==
			        lsproto::CodeActionKindQuickFix) {
				if (item.CodeAction->Diagnostics != nullptr &&
				    !(*item.CodeAction->Diagnostics)->empty()) {
					importActions.push_back(item.CodeAction);
				}
			}
		}
	}

	if (importActions.empty()) {
		if (!expectedTexts.empty()) {
			t->Fatalf("No codefixes returned.", {});
		}
		if (reset) reset();
		return;
	}

	// Save the original content before any edits
	auto script = getScriptInfo(activeFilename);
	std::string originalContent = script->content;
	// For each import action, apply it and check the result
	std::vector<std::string> actualTextArray;
	actualTextArray.reserve(importActions.size());
	for (auto& action : importActions) {
		// Apply the code action
		if (action->Edit != nullptr &&
		    action->Edit->Changes != nullptr) {
			if ((*action->Edit->Changes).size() != 1) {
				t->Fatalf("Expected exactly 1 change, got %d",
				          {(*action->Edit->Changes).size()});
			}
			for (auto& [uri, changeEdits] :
			     *action->Edit->Changes) {
				if (uri != lsconv::FileNameToDocumentURI(
				               activeFilename)) {
					t->Fatalf(
					    "Expected change to file %s, got %s",
					    {activeFilename, uri});
				}
				applyTextEdits(t, *changeEdits);
			}
		}

		// Get the result text
		std::string text;
		if (rangeMarker != nullptr) {
			text = getRangeText(rangeMarker.get());
		} else {
			text = getScriptInfo(activeFilename)->content;
		}
		actualTextArray.push_back(text);

		// Restore original content for next fix
		editScriptAndUpdateMarkers(t, activeFilename, 0,
		                         (int)script->content.size(),
		                         originalContent);
		currentCaretPosition = savedCaretPosition;
	}

	// Compare results
	if (expectedTexts.size() != actualTextArray.size()) {
		std::string actualJoined;
		for (size_t i = 0; i < actualTextArray.size(); i++) {
			if (i > 0) {
				actualJoined += "\n\n";
				actualJoined += gostr::repeat("-", 20);
				actualJoined += "\n\n";
			}
			actualJoined += actualTextArray[i];
		}
		t->Fatalf("Expected %d import fixes, got %d:\n\n%s",
		          {expectedTexts.size(), actualTextArray.size(),
		           actualJoined});
	}
	for (size_t i = 0; i < expectedTexts.size(); i++) {
		auto& actual = actualTextArray[i];
		assertEqual(
		    t, expectedTexts[i], actual,
		    gostd::sprintf("Import fix at index %d doesn't match.\n",
		                   {i}));
	}
	if (reset) reset();
}

// VerifyImportFixModuleSpecifiers — fourslash.go:2392.
void FourslashTest::VerifyImportFixModuleSpecifiers(
    gostd::testing::T* t, const std::string& markerName,
    const std::vector<std::string>& expectedModuleSpecifiers,
    const std::shared_ptr<lsutil::UserPreferences>& preferences) {
	t->Helper();
	GoToMarker(t, markerName);

	std::function<void()> reset;
	if (preferences != nullptr) {
		reset = ConfigureWithReset(t, *preferences);
	}

	// Get diagnostics at the current position to find errors that need
	// import fixes
	auto diagParams =
	    std::make_shared<lsproto::DocumentDiagnosticParams>();
	diagParams->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	auto diagResult = sendRequest(
	    t, lsproto::TextDocumentDiagnosticInfo, diagParams);

	std::vector<std::shared_ptr<lsproto::Diagnostic>> diagnostics;
	if (diagResult.FullDocumentDiagnosticReport != nullptr &&
	    diagResult.FullDocumentDiagnosticReport->Items.has_value()) {
		diagnostics =
		    *diagResult.FullDocumentDiagnosticReport->Items;
	}

	auto params = std::make_shared<lsproto::CodeActionParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Range.Start = currentCaretPosition;
	params->Range.End = currentCaretPosition;
	params->Context = std::make_shared<lsproto::CodeActionContext>();
	params->Context->Diagnostics = diagnostics;
	auto result = sendRequest(t, lsproto::TextDocumentCodeActionInfo,
	                          params);

	// Extract module specifiers from import fix code actions
	std::vector<std::string> actualModuleSpecifiers;
	if (result.CommandOrCodeActionArray != nullptr) {
		for (auto& item : orNilSlice(result.CommandOrCodeActionArray)) {
			if (item.CodeAction != nullptr &&
			    item.CodeAction->Kind != nullptr &&
			    *item.CodeAction->Kind ==
			        lsproto::CodeActionKindQuickFix) {
				if (item.CodeAction->Edit != nullptr &&
				    item.CodeAction->Edit->Changes != nullptr) {
					for (auto& [uri, changeEdits] :
					     *item.CodeAction->Edit->Changes) {
						for (auto& edit : *changeEdits) {
							auto moduleSpec =
							    extractModuleSpecifier(
							        edit->NewText);
							if (!moduleSpec.empty()) {
								if (!gostr::slicesContains(
								        actualModuleSpecifiers,
								        moduleSpec)) {
									actualModuleSpecifiers
									    .push_back(
									        moduleSpec);
								}
							}
						}
					}
				}
			}
		}
	}

	// Compare results
	if (actualModuleSpecifiers.size() !=
	    expectedModuleSpecifiers.size()) {
		t->Fatalf(
		    "Expected %d module specifiers, got %d.\nExpected: "
		    "%v\nActual: %v",
		    {expectedModuleSpecifiers.size(),
		     actualModuleSpecifiers.size(),
		     gostr::join(expectedModuleSpecifiers, ", "),
		     gostr::join(actualModuleSpecifiers, ", ")});
	}

	for (size_t i = 0; i < expectedModuleSpecifiers.size(); i++) {
		if (i >= actualModuleSpecifiers.size() ||
		    actualModuleSpecifiers[i] !=
		        expectedModuleSpecifiers[i]) {
			t->Fatalf(
			    "Module specifier mismatch at index %d.\nExpected: "
			    "%v\nActual: %v",
			    {i,
			     gostr::join(expectedModuleSpecifiers, ", "),
			     gostr::join(actualModuleSpecifiers, ", ")});
		}
	}
	if (reset) reset();
}

// extractModuleSpecifier — fourslash.go:2464.
std::string extractModuleSpecifier(const std::string& text) {
	// Try to match: from "..." or from '...'
	if (int idx = gostr::index(text, "from \""); idx != -1) {
		size_t start = idx + 6; // len("from \"")
		if (int end = gostr::index(text.substr(start), "\"");
		    end != -1) {
			return text.substr(start, end);
		}
	}
	if (int idx = gostr::index(text, "from '"); idx != -1) {
		size_t start = idx + 6; // len("from '")
		if (int end = gostr::index(text.substr(start), "'");
		    end != -1) {
			return text.substr(start, end);
		}
	}

	// Try to match: require("...") or require('...')
	if (int idx = gostr::index(text, "require(\""); idx != -1) {
		size_t start = idx + 9; // len("require(\"")
		if (int end = gostr::index(text.substr(start), "\"");
		    end != -1) {
			return text.substr(start, end);
		}
	}
	if (int idx = gostr::index(text, "require('"); idx != -1) {
		size_t start = idx + 9; // len("require('")
		if (int end = gostr::index(text.substr(start), "'");
		    end != -1) {
			return text.substr(start, end);
		}
	}

	return "";
}


// ===========================================================================
// fourslash.go:2519-2843 — baseline commands (references, codelens,
// definitions, workspace symbols, folding ranges)
// ===========================================================================

// VerifyBaselineFindAllReferences — fourslash.go:2519.
void FourslashTest::VerifyBaselineFindAllReferences(
    gostd::testing::T* t, const std::vector<std::string>& markers) {
	auto referenceLocations = lookupMarkersOrGetRanges(t, markers);

	for (auto* markerOrRange : referenceLocations) {
		// worker in `baselineEachMarkerOrRange`
		GoToMarkerOrRange(t, markerOrRange);

		auto params = std::make_shared<lsproto::ReferenceParams>();
		params->TextDocument.Uri =
		    lsconv::FileNameToDocumentURI(activeFilename);
		params->Position = currentCaretPosition;
		params->Context =
		    std::make_shared<lsproto::ReferenceContext>();
		params->Context->IncludeDeclaration = true;
		auto result = sendRequest(t, lsproto::TextDocumentReferencesInfo,
		                          params);
		addResultToBaseline(
		    t, findAllReferencesCmd,
		    getBaselineForLocationsWithFileContents(
		        orNilSlice(result.Locations),
		        baselineFourslashLocationsOptions{
		            .marker = markerOrRange,
		            .markerName = "/*FIND ALL REFS*/",
		        }));
	}
}

// VerifyBaselineVSFindAllReferences — fourslash.go:2546.
void FourslashTest::VerifyBaselineVSFindAllReferences(
    gostd::testing::T* t, const std::vector<std::string>& markers) {
	auto referenceLocations = lookupMarkersOrGetRanges(t, markers);

	for (auto* markerOrRange : referenceLocations) {
		GoToMarkerOrRange(t, markerOrRange);

		auto params = std::make_shared<lsproto::ReferenceParams>();
		params->TextDocument.Uri =
		    lsconv::FileNameToDocumentURI(activeFilename);
		params->Position = currentCaretPosition;
		params->Context =
		    std::make_shared<lsproto::ReferenceContext>();
		params->Context->IncludeDeclaration = true;
		auto result = sendRequest(
		    t, lsproto::TextDocumentVSReferencesInfo, params);
		// Sort cross-project results for deterministic baselines
		if (result.VSReferenceItems != nullptr &&
		    !orNilSlice(result.VSReferenceItems).empty()) {
			auto& items = *result.VSReferenceItems;
			std::stable_sort(
			    items->begin(), items->end(),
			    [](const std::shared_ptr<lsproto::VSReferenceItem>& a,
			       const std::shared_ptr<lsproto::VSReferenceItem>& b) {
				    std::string ap, bp;
				    if (a->VSProjectName.has_value()) {
					    ap = *a->VSProjectName;
				    }
				    if (b->VSProjectName.has_value()) {
					    bp = *b->VSProjectName;
				    }
				    if (ap != bp) {
					    return ap < bp;
				    }
				    if (a->VSLocation.Uri != b->VSLocation.Uri) {
					    return a->VSLocation.Uri <
					           b->VSLocation.Uri;
				    }
				    if (a->VSLocation.Range.Start.Line !=
				        b->VSLocation.Range.Start.Line) {
					    return a->VSLocation.Range.Start.Line <
					           b->VSLocation.Range.Start.Line;
				    }
				    return a->VSLocation.Range.Start.Character <
				           b->VSLocation.Range.Start.Character;
			    });
			// Re-number IDs sequentially after sort
			std::unordered_map<int32_t, int32_t> idRemap;
			for (size_t i = 0; i < items->size(); i++) {
				idRemap[(*items)[i]->VSId] = (int32_t)i;
				(*items)[i]->VSId = (int32_t)i;
			}
			for (auto& item : *items) {
				if (item->VSDefinitionId.has_value()) {
					item->VSDefinitionId =
					    idRemap[*item->VSDefinitionId];
				}
			}
		}
		// Include file contents with markers
		std::vector<lsproto::Location> locations;
		if (result.VSReferenceItems != nullptr) {
			for (auto& item : orNilSlice(result.VSReferenceItems)) {
				locations.push_back(item->VSLocation);
			}
		}
		std::string fileContents =
		    getBaselineForLocationsWithFileContents(
		        locations, baselineFourslashLocationsOptions{
		                       .marker = markerOrRange,
		                       .markerName = "/*FIND ALL REFS*/",
		                   });

		auto [jsonStr, err] = stringifyJson(result, "", "  ");
		if (err.empty()) {
			addResultToBaseline(t, vsFindAllReferencesCmd,
			                    fileContents + "\n\n" + jsonStr);
		} else {
			t->Fatalf(
			    "Failed to stringify VS references result for "
			    "baseline: %v",
			    {err});
		}
	}
}

// VerifyBaselineCodeLens — fourslash.go:2631.
void FourslashTest::VerifyBaselineCodeLens(
    gostd::testing::T* t,
    const std::shared_ptr<lsutil::UserPreferences>& preferences) {
	std::function<void()> reset;
	if (preferences != nullptr) {
		reset = ConfigureWithReset(t, *preferences);
	}

	bool foundAtLeastOneCodeLens = false;
	// slices.Sorted(maps.Keys(f.openFiles))
	std::vector<std::string> openFileNames(openFiles.begin(),
	                                       openFiles.end());
	std::sort(openFileNames.begin(), openFileNames.end());
	for (auto& openFile : openFileNames) {
		auto params = std::make_shared<lsproto::CodeLensParams>();
		params->TextDocument.Uri =
		    lsconv::FileNameToDocumentURI(openFile);

		auto unresolvedCodeLensList =
		    sendRequest(t, lsproto::TextDocumentCodeLensInfo, params);
		if (unresolvedCodeLensList.CodeLenses == nullptr ||
		    orNilSlice(unresolvedCodeLensList.CodeLenses)
		        .empty()) {
			continue;
		}
		foundAtLeastOneCodeLens = true;

		for (auto& unresolvedCodeLens :
		     orNilSlice(unresolvedCodeLensList.CodeLenses)) {
			tsc::debug::assert(unresolvedCodeLens != nullptr);
			auto resolvedCodeLens = sendRequest(
			    t, lsproto::CodeLensResolveInfo, unresolvedCodeLens);
			tsc::debug::assert(resolvedCodeLens != nullptr);
			tsc::debug::assert(
			    resolvedCodeLens->Command != nullptr,
			    "Expected resolved code lens to have a command.");
			if (!resolvedCodeLens->Command->Command.empty()) {
				assertEqual(t, resolvedCodeLens->Command->Command,
				            showCodeLensLocationsCommandName);
			}

			std::vector<lsproto::Location> locations;
			// commandArgs: (DocumentUri, Position, Location[])
			if (resolvedCodeLens->Command->Arguments != nullptr) {
				auto [locs, err] =
				    roundtripThroughJson<
				        std::vector<lsproto::Location>>(
				        (*resolvedCodeLens->Command->Arguments)
				            ->at(2));
				if (err != nullptr) {
					t->Fatalf(
					    "failed to re-encode code lens "
					    "locations: %v",
					    {err});
				}
				locations = locs;
			}

			auto ranges = converters->FromLSPRange(
			    getScriptInfo(openFile), resolvedCodeLens->Range,
			    spanmap::FeatureAll);
			if (ranges.size() != 1) {
				continue;
			}
			TextRange codeLensRange = ranges[0].Span;
			auto marker = std::make_shared<RangeMarker>();
			marker->fileName = openFile;
			marker->LSRange = resolvedCodeLens->Range;
			marker->Range = codeLensRange;
			addResultToBaseline(
			    t, codeLensesCmd,
			    getBaselineForLocationsWithFileContents(
			        locations,
			        baselineFourslashLocationsOptions{
			            .marker = marker.get(),
			            .markerName = "/*CODELENS: " +
			                          resolvedCodeLens->Command
			                              ->Title +
			                          "*/",
			        }));
		}
	}

	if (!foundAtLeastOneCodeLens) {
		t->Fatalf(
		    "Expected at least one code lens in any open file, but "
		    "got none.",
		    {});
	}
	if (reset) reset();
}

// MarkTestAsStradaServer — fourslash.go:2686.
void FourslashTest::MarkTestAsStradaServer() { isStradaServer = true; }

// VerifyBaselineGoToDefinition — fourslash.go:2690.
void FourslashTest::VerifyBaselineGoToDefinition(
    gostd::testing::T* t, bool includeOriginalSelectionRange,
    const std::vector<std::string>& markers) {
	verifyBaselineDefinitions(
	    t, goToDefinitionCmd,
	    "/*GOTO DEF*/", /*definitionMarker*/
	    [](gostd::testing::T* t, FourslashTest* f,
	       const std::string& fileName, lsproto::Position position)
	        -> lsproto::LocationOrLocationsOrDefinitionLinksOrNull {
		    auto params =
		        std::make_shared<lsproto::DefinitionParams>();
		    params->TextDocument.Uri =
		        lsconv::FileNameToDocumentURI(f->activeFilename);
		    params->Position = f->currentCaretPosition;

		    return f->sendRequest(
		        t, lsproto::TextDocumentDefinitionInfo, params);
	    },
	    includeOriginalSelectionRange, markers);
}

// verifyBaselineDefinitions — fourslash.go:2713.
void FourslashTest::verifyBaselineDefinitions(
    gostd::testing::T* t, const baselineCommand& definitionCommand,
    const std::string& definitionMarker,
    const std::function<lsproto::LocationOrLocationsOrDefinitionLinksOrNull(
        gostd::testing::T*, FourslashTest*, const std::string&,
        lsproto::Position)>& getDefinitions,
    bool includeOriginalSelectionRange,
    const std::vector<std::string>& markers) {
	auto referenceLocations = lookupMarkersOrGetRanges(t, markers);

	for (auto* markerOrRange : referenceLocations) {
		// worker in `baselineEachMarkerOrRange`
		GoToMarkerOrRange(t, markerOrRange);

		auto result = getDefinitions(t, this, activeFilename,
		                             currentCaretPosition);

		if (result.DefinitionLinks != nullptr &&
		    result.DefinitionLinks->has_value()) {
		}
		std::vector<documentSpan> resultAsSpans;
		std::shared_ptr<documentSpan> additionalSpan;
		if (result.Locations != nullptr) {
			for (auto& loc : orNilSlice(result.Locations)) {
				resultAsSpans.push_back(locationToSpan(loc));
				auto& e = resultAsSpans.back();
			}
		} else if (result.Location != nullptr) {
			resultAsSpans = {locationToSpan(*result.Location)};
		} else if (result.DefinitionLinks != nullptr) {
			std::shared_ptr<lsproto::Range> originRange;
			for (auto& link : orNilSlice(result.DefinitionLinks)) {
				if (originRange != nullptr &&
				    (link->OriginSelectionRange == nullptr ||
				     !(*originRange ==
				       *link->OriginSelectionRange))) {
					TSC_UNREACHABLE(
					    "multiple different origin ranges in "
					    "definition links");
				}
				originRange = link->OriginSelectionRange;
				std::shared_ptr<lsproto::Range> contextSpan;
				if (!(link->TargetRange ==
				      link->TargetSelectionRange) &&
				    !isStradaServer) {
					contextSpan = std::make_shared<lsproto::Range>(
					    link->TargetRange);
				}
				resultAsSpans.push_back(documentSpan{
				    .uri = link->TargetUri,
				    .textSpan = link->TargetSelectionRange,
				    .contextSpan = contextSpan,
				});
			}
			if (originRange != nullptr &&
			    includeOriginalSelectionRange) {
				additionalSpan = std::make_shared<documentSpan>(
				    documentSpan{
				        .uri = lsconv::FileNameToDocumentURI(
				            activeFilename),
				        .textSpan = *originRange,
				    });
			}
		}

		addResultToBaseline(
		    t, definitionCommand,
		    getBaselineForSpansWithFileContents(
		        resultAsSpans,
		        baselineFourslashLocationsOptions{
		            .marker = markerOrRange,
		            .markerName = definitionMarker,
		            .additionalSpan = additionalSpan,
		            .preserveResultOrder = definitionCommand ==
		                                   goToSourceDefinitionCmd,
		        }));
	}
}

// VerifyBaselineGoToTypeDefinition — fourslash.go:2768.
void FourslashTest::VerifyBaselineGoToTypeDefinition(
    gostd::testing::T* t, const std::vector<std::string>& markers) {
	verifyBaselineDefinitions(
	    t, goToTypeDefinitionCmd,
	    "/*GOTO TYPE*/", /*definitionMarker*/
	    [](gostd::testing::T* t, FourslashTest* f,
	       const std::string& fileName, lsproto::Position position)
	        -> lsproto::LocationOrLocationsOrDefinitionLinksOrNull {
		    auto params =
		        std::make_shared<lsproto::TypeDefinitionParams>();
		    params->TextDocument.Uri =
		        lsconv::FileNameToDocumentURI(f->activeFilename);
		    params->Position = f->currentCaretPosition;

		    return f->sendRequest(
		        t, lsproto::TextDocumentTypeDefinitionInfo, params);
	    },
	    false, /*includeOriginalSelectionRange*/
	    markers);
}

// VerifyBaselineGoToSourceDefinition — fourslash.go:2792.
void FourslashTest::VerifyBaselineGoToSourceDefinition(
    gostd::testing::T* t, const std::vector<std::string>& markers) {
	verifyBaselineDefinitions(
	    t, goToSourceDefinitionCmd,
	    "/*GOTO SOURCE DEF*/", /*definitionMarker*/
	    [](gostd::testing::T* t, FourslashTest* f,
	       const std::string& fileName, lsproto::Position position)
	        -> lsproto::LocationOrLocationsOrDefinitionLinksOrNull {
		    auto params = std::make_shared<
		        lsproto::TextDocumentPositionParams>();
		    params->TextDocument.Uri =
		        lsconv::FileNameToDocumentURI(f->activeFilename);
		    params->Position = f->currentCaretPosition;

		    auto result = f->sendRequest(
		        t, lsproto::CustomTextDocumentSourceDefinitionInfo,
		        params);
		    if (result == nullptr) {
			    return lsproto::
			        LocationOrLocationsOrDefinitionLinksOrNull{};
		    }
		    return *result;
	    },
	    false, /*includeOriginalSelectionRange*/
	    markers);
}

// VerifyBaselineWorkspaceSymbol — fourslash.go:2817.
void FourslashTest::VerifyBaselineWorkspaceSymbol(
    gostd::testing::T* t, const std::string& query) {
	t->Helper();
	auto params = std::make_shared<lsproto::WorkspaceSymbolParams>();
	params->Query = query;
	auto result =
	    sendRequest(t, lsproto::WorkspaceSymbolInfo, params);

	std::unordered_map<documentSpan,
	                   std::shared_ptr<lsproto::SymbolInformation>,
	                   documentSpanHash>
	    locationToText;
	collections::MultiMap<lsproto::DocumentUri, documentSpan>
	    groupedRanges;
	std::vector<std::shared_ptr<lsproto::SymbolInformation>>
	    symbolInformations;
	if (result.SymbolInformations != nullptr) {
		symbolInformations = orNilSlice(result.SymbolInformations);
	}
	for (auto& symbol : symbolInformations) {
		auto uri = symbol->Location.Uri;
		auto span = locationToSpan(symbol->Location);
		groupedRanges.Add(uri, span);
		locationToText[span] = symbol;
	}

	addResultToBaseline(
	    t, "workspaceSymbol",
	    getBaselineForGroupedSpansWithFileContents(
	        &groupedRanges,
	        baselineFourslashLocationsOptions{
	            .getLocationData =
	                [&locationToText](const documentSpan& span)
	                -> std::string {
		            auto it = locationToText.find(span);
		            if (it == locationToText.end()) return "";
		            return symbolInformationToData(
		                it->second);
	            },
	        }));
}

// VerifyOutliningSpans — fourslash.go:2845.
void FourslashTest::VerifyOutliningSpans(
    gostd::testing::T* t,
    const std::vector<lsproto::FoldingRangeKind>& foldingRangeKind) {
	auto params = std::make_shared<lsproto::FoldingRangeParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	auto result = sendRequest(t, lsproto::TextDocumentFoldingRangeInfo,
	                          params);
	if (result.FoldingRanges == nullptr) {
		t->Fatalf("Nil response received for folding range request",
		          {});
	}

	// Extract actual folding ranges from the result and filter by kind
	// if specified
	std::vector<std::shared_ptr<lsproto::FoldingRange>>
	    actualRanges = orNilSlice(result.FoldingRanges);
	if (!foldingRangeKind.empty()) {
		auto targetKind = foldingRangeKind[0];
		std::vector<std::shared_ptr<lsproto::FoldingRange>> filtered;
		for (auto& r : actualRanges) {
			if (r->Kind != nullptr && *r->Kind == targetKind) {
				filtered.push_back(r);
			}
		}
		actualRanges = filtered;
	}

	auto ranges = Ranges();
	if (actualRanges.size() != ranges.size()) {
		t->Fatalf(
		    "verifyOutliningSpans failed - expected total spans to be "
		    "%d, but was %d",
		    {ranges.size(), actualRanges.size()});
	}

	// stable_sort: Go's slices.SortFunc (pdqsort) is not documented stable,
	// but our oracle testing showed deterministic tie-order matching
	// std::stable_sort — matching observed behavior is the safe port..
	std::stable_sort(ranges.begin(), ranges.end(),
	          [](const std::shared_ptr<RangeMarker>& a,
	             const std::shared_ptr<RangeMarker>& b) {
		          return lsproto::ComparePositions(a->LSPos(),
		                                           b->LSPos()) < 0;
	          });

	for (size_t i = 0; i < ranges.size(); i++) {
		auto& expectedRange = ranges[i];
		auto& actualRange = actualRanges[i];
		lsproto::Position startPos{
		    .Line = actualRange->StartLine,
		    .Character = actualRange->StartCharacter.value_or(0)};
		lsproto::Position endPos{
		    .Line = actualRange->EndLine,
		    .Character = actualRange->EndCharacter.value_or(0)};

		if (lsproto::ComparePositions(startPos,
		                              expectedRange->LSRange.Start) !=
		        0 ||
		    lsproto::ComparePositions(endPos,
		                              expectedRange->LSRange.End) !=
		        0) {
			t->Fatalf(
			    "verifyOutliningSpans failed - span %d has invalid "
			    "positions:\n  actual: start (%d,%d), end "
			    "(%d,%d)\n  expected: start (%d,%d), end (%d,%d)",
			    {(int)i + 1, (int)actualRange->StartLine,
			     (int)actualRange->StartCharacter.value_or(0),
			     (int)actualRange->EndLine,
			     (int)actualRange->EndCharacter.value_or(0),
			     expectedRange->LSRange.Start.Line,
			     expectedRange->LSRange.Start.Character,
			     expectedRange->LSRange.End.Line,
			     expectedRange->LSRange.End.Character});
		}
	}
}

// VerifyFoldingRangeLines verifies folding ranges by comparing only
// start and end lines. This is useful for testing with lineFoldingOnly
// where character positions are ignored. fourslash.go:2896.
void FourslashTest::VerifyFoldingRangeLines(
    gostd::testing::T* t,
    const std::vector<FoldingRangeLineExpected>& expected) {
	auto params = std::make_shared<lsproto::FoldingRangeParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	auto result = sendRequest(t, lsproto::TextDocumentFoldingRangeInfo,
	                          params);
	if (result.FoldingRanges == nullptr) {
		t->Fatalf("Nil response received for folding range request",
		          {});
	}

	std::vector<std::shared_ptr<lsproto::FoldingRange>>
	    actualRanges = orNilSlice(result.FoldingRanges);
	if (actualRanges.size() != expected.size()) {
		t->Fatalf(
		    "verifyFoldingRangeLines failed - expected %d ranges, got "
		    "%d",
		    {expected.size(), actualRanges.size()});
	}

	for (size_t i = 0; i < expected.size(); i++) {
		auto& exp = expected[i];
		auto& got = actualRanges[i];
		if (got->StartLine != exp.StartLine ||
		    got->EndLine != exp.EndLine) {
			t->Errorf(
			    "verifyFoldingRangeLines failed - range %d: "
			    "expected (startLine=%d, endLine=%d), got "
			    "(startLine=%d, endLine=%d)",
			    {i, exp.StartLine, exp.EndLine, got->StartLine,
			     got->EndLine});
		}
	}
}


// ===========================================================================
// fourslash.go:2913-3043 — hover baselines
// ===========================================================================

// VerifyBaselineHover — fourslash.go:2913.
void FourslashTest::VerifyBaselineHover(gostd::testing::T* t) {
	std::vector<markerAndItem<std::shared_ptr<lsproto::Hover>>>
	    markersAndItems;
	for (auto& marker : Markers()) {
		if (marker->Name == nullptr) continue;
		auto params = std::make_shared<lsproto::HoverParams>();
		params->TextDocument.Uri =
		    lsconv::FileNameToDocumentURI(marker->fileName);
		params->Position = marker->LSPosition;
		auto result =
		    sendRequest(t, lsproto::TextDocumentHoverInfo, params);
		markersAndItems.push_back(
		    {marker, result.Hover});
	}

	auto getRange = [](const std::shared_ptr<lsproto::Hover>& item)
	    -> std::shared_ptr<lsproto::Range> {
		if (item == nullptr || item->Range == nullptr) {
			return nullptr;
		}
		return item->Range;
	};

	auto getTooltipLines =
	    [](const std::shared_ptr<lsproto::Hover>& item,
	       const std::shared_ptr<lsproto::Hover>& _prev)
	    -> std::vector<std::string> {
		std::vector<std::string> result;
		if (item->Contents.MarkupContent != nullptr) {
			result = gostr::split(
			    item->Contents.MarkupContent->Value, "\n");
		}
		if (item->Contents.String != nullptr) {
			result =
			    gostr::split(*item->Contents.String, "\n");
		}
		if (item->Contents.MarkedStringWithLanguage != nullptr) {
			result = appendLinesForMarkedStringWithLanguage(
			    result, item->Contents.MarkedStringWithLanguage);
		}
		if (item->Contents.MarkedStrings != nullptr) {
			for (auto& ms : orNilSlice(item->Contents.MarkedStrings)) {
				if (ms.MarkedStringWithLanguage != nullptr) {
					result =
					    appendLinesForMarkedStringWithLanguage(
					        result,
					        ms.MarkedStringWithLanguage);
				} else {
					result.push_back(*ms.String);
				}
			}
		}
		return result;
	};

	addResultToBaseline(
	    t, quickInfoCmd,
	    annotateContentWithTooltips(t, markersAndItems, "quickinfo",
	                                getRange, getTooltipLines));
	auto [jsonStr, err] = stringifyJson(markersAndItems, "", "  ");
	if (err.empty()) {
		writeToBaseline(quickInfoCmd, jsonStr);
	} else {
		t->Fatalf(
		    "Failed to stringify markers and items for baseline: %v",
		    {err});
	}
}

// VerifyBaselineVSHover is like VerifyBaselineHover, but asserts on the
// VS-specific rich hover content (Hover.VSRawContent / the
// "_vs_rawContent" wire field) that the LSP server emits for clients
// advertising the VSSupportsVisualStudioExtensions capability.
// fourslash.go:2964.
void FourslashTest::VerifyBaselineVSHover(gostd::testing::T* t) {
	std::vector<markerAndItem<std::shared_ptr<lsproto::Hover>>>
	    markersAndItems;
	for (auto& marker : Markers()) {
		if (marker->Name == nullptr) continue;
		auto params = std::make_shared<lsproto::HoverParams>();
		params->TextDocument.Uri =
		    lsconv::FileNameToDocumentURI(marker->fileName);
		params->Position = marker->LSPosition;
		auto result =
		    sendRequest(t, lsproto::TextDocumentHoverInfo, params);
		markersAndItems.push_back(
		    {marker, result.Hover});
	}

	auto getRange = [](const std::shared_ptr<lsproto::Hover>& item)
	    -> std::shared_ptr<lsproto::Range> {
		if (item == nullptr || item->Range == nullptr) {
			return nullptr;
		}
		return item->Range;
	};

	auto getTooltipLines =
	    [](const std::shared_ptr<lsproto::Hover>& item,
	       const std::shared_ptr<lsproto::Hover>& _prev)
	    -> std::vector<std::string> {
		if (item == nullptr) {
			return {};
		}
		if (item->VSRawContent == nullptr) {
			return {"(no _vs_rawContent; is "
			        "VSSupportsVisualStudioExtensions set on the "
			        "test's ClientCapabilities?)"};
		}
		return renderVSContainerElement(item->VSRawContent, "");
	};

	addResultToBaseline(
	    t, vsQuickInfoCmd,
	    annotateContentWithTooltips(t, markersAndItems, "vsquickinfo",
	                                getRange, getTooltipLines));
	auto [jsonStr, err] = stringifyJson(markersAndItems, "", "  ");
	if (err.empty()) {
		writeToBaseline(vsQuickInfoCmd, jsonStr);
	} else {
		t->Fatalf(
		    "Failed to stringify markers and items for baseline: %v",
		    {err});
	}
}

// renderVSContainerElement renders a VS rich-content container element
// (icon + colorized runs, possibly nested for the stacked
// display-line/documentation shape) into readable baseline lines.
// fourslash.go:3032.
std::vector<std::string> renderVSContainerElement(
    const std::shared_ptr<lsproto::VSContainerElement>& el,
    const std::string& indent) {
	std::vector<std::string> lines{
	    gostd::sprintf("%sContainerElement (Style=%s)",
	                   {indent, lsproto::String(el->Style)})};
	std::string childIndent = indent + "  ";
	for (auto& child : *el->Elements) {
		if (child.ImageElement != nullptr) {
			auto& imageId = *child.ImageElement->ImageId;
			lines.push_back(gostd::sprintf(
			    "%sImageElement { Guid: %s, Id: 0x%X }",
			    {childIndent, imageId.Guid, imageId.Id}));
		} else if (child.ClassifiedTextElement != nullptr) {
			lines.push_back(childIndent + "ClassifiedTextElement");
			for (auto& run :
			     *child.ClassifiedTextElement->Runs) {
				lines.push_back(gostd::sprintf(
				    "%s  [%s] %q",
				    {childIndent, run->ClassificationTypeName,
				     run->Text}));
			}
		} else if (child.ContainerElement != nullptr) {
			auto nested = renderVSContainerElement(
			    child.ContainerElement, childIndent);
			lines.insert(lines.end(), nested.begin(),
			             nested.end());
		} else {
			lines.push_back(childIndent + "<empty union element>");
		}
	}
	return lines;
}

// appendLinesForMarkedStringWithLanguage — fourslash.go:3054.
std::vector<std::string> appendLinesForMarkedStringWithLanguage(
    std::vector<std::string> result,
    const std::shared_ptr<lsproto::MarkedStringWithLanguage>& ms) {
	result.push_back("```" + ms->Language);
	result.push_back(ms->Value);
	result.push_back("```");
	return result;
}

// hoverContentString extracts the text content from a hover response
// for comparison. fourslash.go:3067.
std::string hoverContentString(
    const std::shared_ptr<lsproto::Hover>& hover) {
	if (hover == nullptr) {
		return "";
	}
	if (hover->Contents.MarkupContent != nullptr) {
		return hover->Contents.MarkupContent->Value;
	}
	if (hover->Contents.String != nullptr) {
		return *hover->Contents.String;
	}
	return "";
}

// VerifyBaselineHoverWithVerbosity — fourslash.go:3080.
void FourslashTest::VerifyBaselineHoverWithVerbosity(
    gostd::testing::T* t,
    const std::unordered_map<std::string, std::vector<int>>&
        verbosityLevels) {
	std::vector<markerAndItem<std::shared_ptr<hoverWithVerbosity>>>
	    markersAndItems;
	for (auto& marker : Markers()) {
		if (marker->Name == nullptr) continue;
		std::vector<int> levels;
		auto it = verbosityLevels.find(*marker->Name);
		if (it != verbosityLevels.end()) {
			levels = it->second;
		} else {
			levels = {0};
		}
		for (size_t i = 0; i < levels.size(); i++) {
			int level = levels[i];
			auto params = std::make_shared<lsproto::HoverParams>();
			params->TextDocument.Uri =
			    lsconv::FileNameToDocumentURI(marker->fileName);
			params->Position = marker->LSPosition;
			if (level > 0) {
				params->VerbosityLevel = (int32_t)level;
			}
			auto result = sendRequest(
			    t, lsproto::TextDocumentHoverInfo, params);
			auto item = std::make_shared<hoverWithVerbosity>();
			item->Hover = result.Hover;
			item->VerbosityLevel =
	                        std::make_shared<int32_t>(level);
			// If the previous level said it can't expand further,
			// verify the hover content is identical, meaning the
			// flag was accurate.
			if (i > 0 && level > levels[i - 1]) {
				auto prevItem =
				    markersAndItems.back().Item;
				if (prevItem != nullptr &&
				    prevItem->Hover != nullptr &&
				    !prevItem->Hover->CanIncreaseVerbosity) {
					std::string prevContent =
					    hoverContentString(
					        prevItem->Hover);
					std::string curContent =
					    hoverContentString(
					        item->Hover);
					if (prevContent != curContent) {
						t->Errorf(
						    "At marker %q: verbosity "
						    "level %d response differs "
						    "from level %d, but level "
						    "%d had "
						    "canIncreaseVerbosity=false."
						    "\n  level %d: %s\n  level "
						    "%d: %s",
						    {*marker->Name, level,
						     levels[i - 1],
						     levels[i - 1],
						     levels[i - 1],
						     prevContent, level,
						     curContent});
					}
				}
			}
			markersAndItems.push_back({marker, item});
		}
	}

	auto getRange =
	    [](const std::shared_ptr<hoverWithVerbosity>& item)
	    -> std::shared_ptr<lsproto::Range> {
		if (item == nullptr || item->Hover == nullptr ||
		    item->Hover->Range == nullptr) {
			return nullptr;
		}
		return item->Hover->Range;
	};

	auto getTooltipLines =
	    [](const std::shared_ptr<hoverWithVerbosity>& item,
	       const std::shared_ptr<hoverWithVerbosity>& _prev)
	    -> std::vector<std::string> {
		if (item == nullptr || item->Hover == nullptr) {
			return {};
		}
		std::vector<std::string> result;

		if (item->Hover->Contents.MarkupContent != nullptr) {
			result = gostr::split(
			    item->Hover->Contents.MarkupContent->Value, "\n");
		}
		if (item->Hover->Contents.String != nullptr) {
			result = gostr::split(*item->Hover->Contents.String,
			                      "\n");
		}
		if (item->Hover->Contents.MarkedStringWithLanguage !=
		    nullptr) {
			result = appendLinesForMarkedStringWithLanguage(
			    result,
			    item->Hover->Contents.MarkedStringWithLanguage);
		}
		if (item->Hover->Contents.MarkedStrings != nullptr) {
			for (auto& ms :
			     orNilSlice(item->Hover->Contents.MarkedStrings)) {
				if (ms.MarkedStringWithLanguage != nullptr) {
					result =
					    appendLinesForMarkedStringWithLanguage(
					        result,
					        ms.MarkedStringWithLanguage);
				} else {
					result.push_back(*ms.String);
				}
			}
		}

		result.push_back(gostd::sprintf(
		    "(verbosity level: %d)",
		    {item->VerbosityLevel != nullptr
		         ? *item->VerbosityLevel
		         : 0}));

		return result;
	};

	addResultToBaseline(
	    t, quickInfoCmd,
	    annotateContentWithTooltips(t, markersAndItems, "quickinfo",
	                                getRange, getTooltipLines));
	auto [jsonStr, err] = stringifyJson(markersAndItems, "", "  ");
	if (err.empty()) {
		writeToBaseline(quickInfoCmd, jsonStr);
	} else {
		t->Fatalf(
		    "Failed to stringify markers and items for baseline: %v",
		    {err});
	}
}

// VerifyBaselineSignatureHelp — fourslash.go:3168.
void FourslashTest::VerifyBaselineSignatureHelp(gostd::testing::T* t) {
	std::vector<markerAndItem<std::shared_ptr<lsproto::SignatureHelp>>>
	    markersAndItems;
	for (auto& marker : Markers()) {
		if (marker->Name == nullptr) continue;
		auto params =
		    std::make_shared<lsproto::SignatureHelpParams>();
		params->TextDocument.Uri =
		    lsconv::FileNameToDocumentURI(marker->FileName());
		params->Position = marker->LSPosition;
		auto result = sendRequest(
		    t, lsproto::TextDocumentSignatureHelpInfo, params);
		markersAndItems.push_back({marker,
		                           result.SignatureHelp});
	}

	auto getRange =
	    [](const std::shared_ptr<lsproto::SignatureHelp>& item)
	    -> std::shared_ptr<lsproto::Range> {
		// SignatureHelp doesn't have a range like hover does
		return nullptr;
	};

	auto getTooltipLines =
	    [t](const std::shared_ptr<lsproto::SignatureHelp>& item,
	        const std::shared_ptr<lsproto::SignatureHelp>& _prev)
	    -> std::vector<std::string> {
		if (item == nullptr || !item->Signatures.has_value() ||
		    item->Signatures->empty()) {
			return {"No signature help available"};
		}

		// Show active signature if specified, otherwise first
		// signature
		size_t activeSignature = 0;
		if (item->ActiveSignature.has_value() &&
		    (size_t)*item->ActiveSignature <
		        item->Signatures->size()) {
			activeSignature = (size_t)*item->ActiveSignature;
		}

		auto& sig = (*item->Signatures)[activeSignature];

		// Build signature display
		std::string signatureLine = sig->Label;
		std::string activeParamLine;

		// Determine active parameter: per-signature takes precedence
		// over top-level per LSP spec
		// "If provided (or `null`), this is used in place of
		// `SignatureHelp.activeParameter`."
		const std::shared_ptr<lsproto::UintegerOrNull>*
		    activeParamPtr = nullptr;
		if (sig->ActiveParameter != nullptr) {
			activeParamPtr = &sig->ActiveParameter;
		} else {
			activeParamPtr = &item->ActiveParameter;
		}

		// Show active parameter if specified, and the signature
		// text.
		if (activeParamPtr != nullptr && *activeParamPtr != nullptr &&
		    (*activeParamPtr)->Uinteger != nullptr &&
		    sig->Parameters != nullptr) {
			int activeParamIndex =
			    (int)*(*activeParamPtr)->Uinteger;
			if (activeParamIndex >= 0 &&
			    activeParamIndex <
			        (int)(*sig->Parameters)->size()) {
				auto& activeParam =
				    (*(*sig->Parameters))
				        [activeParamIndex];

				// Get the parameter label and bold the
				// parameter text within the original string.
				std::string activeParamLabel;
				if (activeParam->Label.String != nullptr) {
					activeParamLabel =
					    *activeParam->Label.String;
				} else if (activeParam->Label.Tuple !=
				           nullptr) {
					activeParamLabel = signatureLine.substr(
					    (*activeParam->Label.Tuple)[0],
					    (*activeParam->Label.Tuple)[1] -
					        (*activeParam->Label.Tuple)[0]);
				} else {
					t->Fatal(
					    {"Unsupported param label kind."});
				}
				signatureLine = gostr::replace(
				    signatureLine, activeParamLabel,
				    "**" + activeParamLabel + "**", 1);

				if (activeParam->Documentation != nullptr) {
					if (activeParam->Documentation
					        ->MarkupContent != nullptr) {
						activeParamLine =
						    activeParam->Documentation
						        ->MarkupContent->Value;
					} else if (activeParam->Documentation
					               ->String != nullptr) {
						activeParamLine =
						    *activeParam->Documentation
						         ->String;
					}

					activeParamLine = gostd::sprintf(
					    "- `%s`: %s",
					    {activeParamLabel,
					     activeParamLine});
				}
			}
		}

		std::vector<std::string> result;
		result.push_back(signatureLine);
		if (!activeParamLine.empty()) {
			result.push_back(activeParamLine);
		}

		// ORIGINALLY we would "only display signature documentation
		// on the last argument when multiple arguments are marked".
		// !!!
		// Note that this is harder than in Strada, because LSP
		// signature help has no concept of applicable spans.
		if (sig->Documentation != nullptr) {
			if (sig->Documentation->MarkupContent != nullptr) {
				auto parts = gostr::split(
				    sig->Documentation->MarkupContent->Value,
				    "\n");
				result.insert(result.end(), parts.begin(),
				              parts.end());
			} else if (sig->Documentation->String != nullptr) {
				auto parts = gostr::split(
				    *sig->Documentation->String, "\n");
				result.insert(result.end(), parts.begin(),
				              parts.end());
			} else {
				t->Fatal({"Unsupported documentation format."});
			}
		}

		return result;
	};

	addResultToBaseline(
	    t, signatureHelpCmd,
	    annotateContentWithTooltips(t, markersAndItems,
	                                "signaturehelp", getRange,
	                                getTooltipLines));
	auto [jsonStr, err] = stringifyJson(markersAndItems, "", "  ");
	if (err.empty()) {
		writeToBaseline(signatureHelpCmd, jsonStr);
	} else {
		t->Fatalf(
		    "Failed to stringify markers and items for baseline: %v",
		    {err});
	}
}

// VerifyBaselineSelectionRanges — fourslash.go:3282.
void FourslashTest::VerifyBaselineSelectionRanges(
    gostd::testing::T* t) {
	auto markers = Markers();
	gostr::Builder result;
	std::string newLine = "\n";

	for (size_t i = 0; i < markers.size(); i++) {
		auto& marker = markers[i];
		if (i > 0) {
			result.WriteString(newLine);
			for (int j = 0; j < 80; j++) {
				result.WriteByte('=');
			}
			result.WriteString(newLine);
			result.WriteString(newLine);
		}

		auto* script = getScriptInfo(marker->FileName());
		std::string fileContent = script->content;

		// Add the marker position indicator
		int markerPos = marker->Position;
		std::string baselineContent =
		    fileContent.substr(0, markerPos) + "/**/" +
		    fileContent.substr(markerPos) + newLine;
		result.WriteString(baselineContent);

		// Get selection ranges at this marker
		auto params =
		    std::make_shared<lsproto::SelectionRangeParams>();
		params->TextDocument.Uri =
		    lsconv::FileNameToDocumentURI(marker->FileName());
		params->Positions = {marker->LSPosition};

		auto selectionRangeResult = sendRequest(
		    t, lsproto::TextDocumentSelectionRangeInfo, params);

		if (selectionRangeResult.SelectionRanges == nullptr ||
		    orNilSlice(selectionRangeResult.SelectionRanges)
		        .empty()) {
			result.WriteString("No selection ranges available\n");
			continue;
		}

		auto selectionRange =
		    orNilSlice(selectionRangeResult.SelectionRanges)[0];

		// Add blank line after source code section
		result.WriteString(newLine);

		// Walk through the selection range chain
		while (selectionRange != nullptr) {
			int start = (int)converters->LineAndCharacterToPosition(
			    script, selectionRange->Range.Start);
			int end = (int)converters->LineAndCharacterToPosition(
			    script, selectionRange->Range.End);

			// Create a masked version of the file showing only this
			// range
			auto runes = gostr::toRunes(fileContent);
			std::vector<char32_t> masked(runes.size());
			for (size_t i = 0; i < runes.size(); i++) {
				char32_t ch = runes[i];
				if ((int)i >= start && (int)i < end) {
					// Keep characters in the selection range
					if (ch == U' ') {
						masked[i] = U'•';
					} else if (ch == U'\n' || ch == U'\r') {
						// Keep line breaks as-is, will add
						// arrow later
						masked[i] = ch;
					} else {
						masked[i] = ch;
					}
				} else {
					// Replace characters outside the range
					if (ch == U'\n' || ch == U'\r') {
						masked[i] = ch;
					} else {
						masked[i] = U' ';
					}
				}
			}

			std::string maskedStr = gostr::fromRunes(masked);

			// Add line break arrows
			maskedStr =
			    gostr::replaceAll(maskedStr, "\n", "↲\n");
			maskedStr =
			    gostr::replaceAll(maskedStr, "\r", "↲\r");

			// Remove blank lines
			auto lines = gostr::split(maskedStr, "\n");
			std::vector<std::string> nonBlankLines;
			for (auto& line : lines) {
				std::string trimmed = gostr::trimSpace(line);
				if (!trimmed.empty() && trimmed != "↲") {
					nonBlankLines.push_back(line);
				}
			}
			maskedStr = gostr::join(nonBlankLines, "\n");

			// Find leading and trailing width of non-whitespace
			// characters
			auto maskedRunes = gostr::toRunes(maskedStr);
			auto isRealCharacter = [](char32_t ch) {
				return ch != U'•' && ch != U'↲' &&
				       !tsc::isWhiteSpaceLike(ch);
			};

			int leadingWidth = -1;
			for (size_t i = 0; i < maskedRunes.size(); i++) {
				if (isRealCharacter(maskedRunes[i])) {
					leadingWidth = (int)i;
					break;
				}
			}

			int trailingWidth = -1;
			for (int j = (int)maskedRunes.size() - 1; j >= 0;
			     j--) {
				if (isRealCharacter(maskedRunes[j])) {
					trailingWidth = j;
					break;
				}
			}

			if (leadingWidth != -1 && trailingWidth != -1 &&
			    leadingWidth <= trailingWidth) {
				// Clean up middle section
				std::string prefix = gostr::fromRunes(
				    std::vector<char32_t>(
				        maskedRunes.begin(),
				        maskedRunes.begin() + leadingWidth));
				std::string middle = gostr::fromRunes(
				    std::vector<char32_t>(
				        maskedRunes.begin() + leadingWidth,
				        maskedRunes.begin() + trailingWidth +
				            1));
				std::string suffix = gostr::fromRunes(
				    std::vector<char32_t>(
				        maskedRunes.begin() + trailingWidth +
				            1,
				        maskedRunes.end()));

				middle = gostr::replaceAll(middle, "•", " ");
				middle = gostr::replaceAll(middle, "↲", "");

				maskedStr = prefix + middle + suffix;
			}

			// Add blank line before multi-line ranges
			if (gostr::contains(maskedStr, "\n")) {
				result.WriteString(newLine);
			}

			result.WriteString(maskedStr);
			if (!gostr::hasSuffix(maskedStr, "\n")) {
				result.WriteString(newLine);
			}

			selectionRange = selectionRange->Parent;
		}
	}
	auto resultStr = result.String();
	if (gostr::hasSuffix(resultStr, "\n")) {
		resultStr.pop_back();
	}
	addResultToBaseline(t, smartSelectionCmd, resultStr);
}


// ===========================================================================
// fourslash.go:3429-3707 — call hierarchy
// ===========================================================================

// VerifyBaselineCallHierarchy — fourslash.go:3429.
void FourslashTest::VerifyBaselineCallHierarchy(gostd::testing::T* t) {
	std::string fileName = activeFilename;
	auto position = currentCaretPosition;

	auto params =
	    std::make_shared<lsproto::CallHierarchyPrepareParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(fileName);
	params->Position = position;

	auto prepareResult = sendRequest(
	    t, lsproto::TextDocumentPrepareCallHierarchyInfo, params);
	if (prepareResult.CallHierarchyItems == nullptr ||
	    orNilSlice(prepareResult.CallHierarchyItems).empty()) {
		addResultToBaseline(t, callHierarchyCmd,
		                    "No call hierarchy items available");
		return;
	}

	gostr::Builder result;

	for (auto& callHierarchyItem :
	     orNilSlice(prepareResult.CallHierarchyItems)) {
		std::unordered_map<callHierarchyItemKey, bool,
		                   callHierarchyItemKeyHash>
		    seen;
		std::string itemFileName =
		    lsproto::documentUriFileName(callHierarchyItem->Uri);
		auto* script = getOrLoadScriptInfo(itemFileName);
		formatCallHierarchyItem(t, this, script, &result,
		                        *callHierarchyItem,
		                        callHierarchyItemDirection::root, seen,
		                        "");
	}

	auto resultStr = result.String();
	if (gostr::hasSuffix(resultStr, "\n")) {
		resultStr.pop_back();
	}
	addResultToBaseline(t, callHierarchyCmd, resultStr);
}

// symbolKindToLowercase — fourslash.go:3460.
std::string symbolKindToLowercase(lsproto::SymbolKind kind) {
	return gostr::toLower(lsproto::String(kind));
}

// formatCallHierarchyItem — fourslash.go:3464.
void formatCallHierarchyItem(
    gostd::testing::T* t, FourslashTest* f, scriptInfo* file,
    gostr::Builder* result,
    const lsproto::CallHierarchyItem& callHierarchyItem,
    callHierarchyItemDirection direction,
    std::unordered_map<callHierarchyItemKey, bool,
                       callHierarchyItemKeyHash>& seen,
    const std::string& prefix) {
	callHierarchyItemKey key{
	    .uri = callHierarchyItem.Uri,
	    .range_ = callHierarchyItem.Range,
	    .direction = direction,
	};
	bool alreadySeen = seen[key];
	seen[key] = true;

	struct incomingCallResult {
		bool skip = false;
		bool seen = false;
		std::vector<std::shared_ptr<lsproto::CallHierarchyIncomingCall>>
		    values;
	};
	struct outgoingCallResult {
		bool skip = false;
		bool seen = false;
		std::vector<std::shared_ptr<lsproto::CallHierarchyOutgoingCall>>
		    values;
	};

	incomingCallResult incomingCalls;
	outgoingCallResult outgoingCalls;

	if (direction == callHierarchyItemDirection::outgoing) {
		incomingCalls.skip = true;
	} else if (alreadySeen) {
		incomingCalls.seen = true;
	} else {
		auto incomingParams = std::make_shared<
		    lsproto::CallHierarchyIncomingCallsParams>();
		incomingParams->Item =
		    std::make_shared<lsproto::CallHierarchyItem>(
		        callHierarchyItem);
		auto incomingResult = f->sendRequest(
		    t, lsproto::CallHierarchyIncomingCallsInfo,
		    incomingParams);
		if (incomingResult.CallHierarchyIncomingCalls != nullptr) {
			incomingCalls.values =
			    orNilSlice(incomingResult.CallHierarchyIncomingCalls);
		}
	}

	if (direction == callHierarchyItemDirection::incoming) {
		outgoingCalls.skip = true;
	} else if (alreadySeen) {
		outgoingCalls.seen = true;
	} else {
		auto outgoingParams = std::make_shared<
		    lsproto::CallHierarchyOutgoingCallsParams>();
		outgoingParams->Item =
		    std::make_shared<lsproto::CallHierarchyItem>(
		        callHierarchyItem);
		auto outgoingResult = f->sendRequest(
		    t, lsproto::CallHierarchyOutgoingCallsInfo,
		    outgoingParams);
		if (outgoingResult.CallHierarchyOutgoingCalls != nullptr) {
			outgoingCalls.values =
			    orNilSlice(outgoingResult.CallHierarchyOutgoingCalls);
		}
	}

	std::string trailingPrefix = prefix;
	result->WriteString(gostd::sprintf("%s╭ name: %s\n",
	                                   {prefix, callHierarchyItem.Name}));
	result->WriteString(
	    gostd::sprintf("%s├ kind: %s\n",
	                   {prefix,
	                    symbolKindToLowercase(callHierarchyItem.Kind)}));
	if (callHierarchyItem.Detail.has_value() &&
	    !callHierarchyItem.Detail->empty()) {
		result->WriteString(
		    gostd::sprintf("%s├ containerName: %s\n",
		                   {prefix, *callHierarchyItem.Detail}));
	}
	result->WriteString(
	    gostd::sprintf("%s├ file: %s\n",
	                   {prefix, lsproto::documentUriFileName(
	                                callHierarchyItem.Uri)}));
	result->WriteString(prefix);
	result->WriteString("├ span:\n");
	formatCallHierarchyItemSpan(f, file, result, callHierarchyItem.Range,
	                            prefix + "│ ", prefix + "│ ");
	result->WriteString(prefix);
	result->WriteString("├ selectionSpan:\n");
	formatCallHierarchyItemSpan(f, file, result,
	                            callHierarchyItem.SelectionRange,
	                            prefix + "│ ", prefix + "│ ");

	// Handle incoming calls
	if (incomingCalls.seen) {
		if (outgoingCalls.skip) {
			result->WriteString(trailingPrefix);
			result->WriteString("╰ incoming: ...\n");
		} else {
			result->WriteString(prefix);
			result->WriteString("├ incoming: ...\n");
		}
	} else if (!incomingCalls.skip) {
		if (incomingCalls.values.empty()) {
			if (outgoingCalls.skip) {
				result->WriteString(trailingPrefix);
				result->WriteString("╰ incoming: none\n");
			} else {
				result->WriteString(prefix);
				result->WriteString("├ incoming: none\n");
			}
		} else {
			result->WriteString(prefix);
			result->WriteString("├ incoming:\n");
			for (size_t i = 0; i < incomingCalls.values.size(); i++) {
				auto& incomingCall = incomingCalls.values[i];
				std::string fromFileName =
				    lsproto::documentUriFileName(
				        incomingCall->From->Uri);
				auto* fromFile =
				    f->getOrLoadScriptInfo(fromFileName);
				result->WriteString(prefix);
				result->WriteString("│ ╭ from:\n");
				formatCallHierarchyItem(
				    t, f, fromFile, result, *incomingCall->From,
				    callHierarchyItemDirection::incoming,
				    seen, prefix + "│ │ ");
				result->WriteString(prefix);
				result->WriteString("│ ├ fromSpans:\n");

				std::string fromSpansTrailingPrefix =
				    trailingPrefix + "╰ ╰ ";
				if (i < incomingCalls.values.size() - 1) {
					fromSpansTrailingPrefix = prefix + "│ ╰ ";
				} else if (!outgoingCalls.skip &&
				           (!outgoingCalls.seen ||
				            !outgoingCalls.values.empty())) {
					fromSpansTrailingPrefix = prefix + "│ ╰ ";
				}
				formatCallHierarchyItemSpans(
				    f, fromFile, result, *incomingCall->FromRanges,
				    prefix + "│ │ ", fromSpansTrailingPrefix);
			}
		}
	}

	// Handle outgoing calls
	if (outgoingCalls.seen) {
		result->WriteString(trailingPrefix);
		result->WriteString("╰ outgoing: ...\n");
	} else if (!outgoingCalls.skip) {
		if (outgoingCalls.values.empty()) {
			result->WriteString(trailingPrefix);
			result->WriteString("╰ outgoing: none\n");
		} else {
			result->WriteString(prefix);
			result->WriteString("├ outgoing:\n");
			for (size_t i = 0; i < outgoingCalls.values.size(); i++) {
				auto& outgoingCall = outgoingCalls.values[i];
				std::string toFileName =
				    lsproto::documentUriFileName(
				        outgoingCall->To->Uri);
				auto* toFile =
				    f->getOrLoadScriptInfo(toFileName);
				result->WriteString(prefix);
				result->WriteString("│ ╭ to:\n");
				formatCallHierarchyItem(
				    t, f, toFile, result, *outgoingCall->To,
				    callHierarchyItemDirection::outgoing,
				    seen, prefix + "│ │ ");
				result->WriteString(prefix);
				result->WriteString("│ ├ fromSpans:\n");

				std::string fromSpansTrailingPrefix =
				    trailingPrefix + "╰ ╰ ";
				if (i < outgoingCalls.values.size() - 1) {
					fromSpansTrailingPrefix = prefix + "│ ╰ ";
				}
				formatCallHierarchyItemSpans(
				    f, file, result, *outgoingCall->FromRanges,
				    prefix + "│ │ ", fromSpansTrailingPrefix);
			}
		}
	}
}

// formatCallHierarchyItemSpan — fourslash.go:3609.
void formatCallHierarchyItemSpan(
    FourslashTest* f, scriptInfo* file, gostr::Builder* result,
    const lsproto::Range& span, const std::string& prefix,
    const std::string& closingPrefix) {
	auto startLc = span.Start;
	auto endLc = span.End;
	auto startPos =
	    f->converters->LineAndCharacterToPosition(file, span.Start);
	auto endPos =
	    f->converters->LineAndCharacterToPosition(file, span.End);

	// Compute line starts for the file
	auto lineStarts = computeLineStarts(file->content);

	// Find the line boundaries - expand to full lines
	int contextStart = (int)startPos;
	int contextEnd = (int)endPos;

	// Expand to start of first line
	while (contextStart > 0 &&
	       file->content[contextStart - 1] != '\n' &&
	       file->content[contextStart - 1] != '\r') {
		contextStart--;
	}

	// Expand to end of last line
	while (contextEnd < (int)file->content.size() &&
	       file->content[contextEnd] != '\n' &&
	       file->content[contextEnd] != '\r') {
		contextEnd++;
	}

	// Get actual line and character positions for the context
	int contextStartLine = (int)startLc.Line;
	int contextEndLine = (int)endLc.Line;

	// Calculate line number padding
	int lineNumWidth =
	    (int)std::to_string(contextEndLine + 1).size() + 2;

	result->WriteString(gostd::sprintf(
	    "%s╭ %s:%d:%d-%d:%d\n",
	    {prefix, file->fileName, startLc.Line + 1,
	     startLc.Character + 1, endLc.Line + 1,
	     endLc.Character + 1}));

	for (int lineNum = contextStartLine; lineNum <= contextEndLine;
	     lineNum++) {
		int lineStart = lineStarts[lineNum];
		int lineEnd = (int)file->content.size();
		if (lineNum + 1 < (int)lineStarts.size()) {
			lineEnd = lineStarts[lineNum + 1];
		}

		// Get the line content, trimming trailing newlines
		std::string lineContent =
		    file->content.substr(lineStart, lineEnd - lineStart);
		while (!lineContent.empty() &&
		       (lineContent.back() == '\r' ||
		        lineContent.back() == '\n')) {
			lineContent.pop_back();
		}

		// Format with line number
		std::string lineNumStr =
		    gostd::sprintf("%d:", {lineNum + 1});
		std::string paddedLineNum =
		    gostr::repeat(" ",
		                  lineNumWidth - (int)lineNumStr.size() - 1) +
		    lineNumStr;
		if (lineContent.empty()) {
			result->WriteString(gostd::sprintf(
			    "%s│ %s\n", {prefix, paddedLineNum}));
		} else {
			result->WriteString(gostd::sprintf(
			    "%s│ %s %s\n",
			    {prefix, paddedLineNum, lineContent}));
		}

		// Add selection carets if this line contains part of the span
		if (lineNum >= (int)startLc.Line &&
		    lineNum <= (int)endLc.Line) {
			int selStart = 0;
			int selEnd = (int)lineContent.size();

			if (lineNum == (int)startLc.Line) {
				selStart = (int)startLc.Character;
			}
			if (lineNum == (int)endLc.Line) {
				selEnd = (int)endLc.Character;
			}

			// Don't show carets for empty selections
			bool isEmpty = startLc.Line == endLc.Line &&
			               startLc.Character == endLc.Character;
			if (isEmpty) {
				// For empty selections, show a single "<"
				// character
				std::string padding = gostr::repeat(
				    " ", lineNumWidth + selStart);
				result->WriteString(gostd::sprintf(
				    "%s│ %s<\n", {prefix, padding}));
			} else {
				// Calculate selection length (at least 1)
				int selLength = selEnd - selStart;
				selLength = std::max(
				    selLength,
				    1); // Trim to actual content on the
				        // line
				if (lineNum < (int)endLc.Line) {
					// For lines before the last, trim to line
					// content length
					if (selEnd > (int)lineContent.size()) {
						selEnd = (int)lineContent.size();
						selLength = selEnd - selStart;
					}
				}

				std::string padding = gostr::repeat(
				    " ", lineNumWidth + selStart);
				std::string carets =
				    gostr::repeat("^", selLength);
				result->WriteString(gostd::sprintf(
				    "%s│ %s%s\n", {prefix, padding, carets}));
			}
		}
	}

	result->WriteString(closingPrefix);
	result->WriteString("╰\n");
}

// computeLineStarts — fourslash.go:3710.
std::vector<int> computeLineStarts(const std::string& content) {
	std::vector<int> lineStarts{0};
	for (size_t i = 0; i < content.size(); i++) {
		if (content[i] == '\n') {
			lineStarts.push_back((int)i + 1);
		}
	}
	return lineStarts;
}

// formatCallHierarchyItemSpans — fourslash.go:3719.
void formatCallHierarchyItemSpans(
    FourslashTest* f, scriptInfo* file, gostr::Builder* result,
    const std::vector<lsproto::Range>& spans, const std::string& prefix,
    const std::string& trailingPrefix) {
	for (size_t i = 0; i < spans.size(); i++) {
		std::string closingPrefix = prefix;
		if (i == spans.size() - 1) {
			closingPrefix = trailingPrefix;
		}
		formatCallHierarchyItemSpan(f, file, result, spans[i], prefix,
		                            closingPrefix);
	}
}

// ===========================================================================
// fourslash.go:3732-3806 — document highlights
// ===========================================================================

// VerifyBaselineDocumentHighlights — fourslash.go:3732.
void FourslashTest::VerifyBaselineDocumentHighlights(
    gostd::testing::T* t,
    const std::shared_ptr<lsutil::UserPreferences>& preferences,
    const std::vector<MarkerOrRangeOrName>& markerOrRangeOrNames) {
	VerifyBaselineDocumentHighlightsWithOptions(t, preferences,
	                                            {} /*filesToSearch*/,
	                                            markerOrRangeOrNames);
}

// VerifyBaselineDocumentHighlightsWithOptions — fourslash.go:3741.
void FourslashTest::VerifyBaselineDocumentHighlightsWithOptions(
    gostd::testing::T* t,
    const std::shared_ptr<lsutil::UserPreferences>& preferences,
    const std::vector<std::string>& filesToSearch,
    const std::vector<MarkerOrRangeOrName>& markerOrRangeOrNames) {
	std::vector<MarkerOrRange*> markerOrRanges;
	for (auto& markerOrRangeOrName : markerOrRangeOrNames) {
		if (auto* name =
		        std::get_if<std::string>(&markerOrRangeOrName)) {
			auto it = testData->MarkerPositions.find(*name);
			if (it == testData->MarkerPositions.end()) {
				t->Fatalf("Marker '%s' not found", {*name});
			}
			markerOrRanges.push_back(it->second.get());
		} else if (auto* marker = std::get_if<std::shared_ptr<Marker>>(
		               &markerOrRangeOrName)) {
			markerOrRanges.push_back(marker->get());
		} else if (auto* rangeMarker =
		               std::get_if<std::shared_ptr<RangeMarker>>(
		                   &markerOrRangeOrName)) {
			markerOrRanges.push_back(rangeMarker->get());
		} else {
			t->Fatalf(
			    "Invalid marker or range type: %T. Expected string, "
			    "*Marker, or *RangeMarker.",
			    {gostr::variantTypeName(markerOrRangeOrName)});
		}
	}

	verifyBaselineDocumentHighlights(t, preferences, filesToSearch,
	                                 markerOrRanges);
}

// verifyBaselineDocumentHighlights — fourslash.go:3767.
void FourslashTest::verifyBaselineDocumentHighlights(
    gostd::testing::T* t,
    const std::shared_ptr<lsutil::UserPreferences>& preferences,
    const std::vector<std::string>& filesToSearch,
    const std::vector<MarkerOrRange*>& markerOrRanges) {
	for (auto* markerOrRange : markerOrRanges) {
		goToMarker(t, markerOrRange);

		std::vector<lsproto::Location> spans;
		std::string header;

		if (!filesToSearch.empty()) {
			// Multi-file: use the custom method.
			std::vector<lsproto::DocumentUri> searchURIs;
			for (auto& file : filesToSearch) {
				searchURIs.push_back(
				    lsconv::FileNameToDocumentURI(file));
			}

			auto params = std::make_shared<
			    lsproto::MultiDocumentHighlightParams>();
			params->TextDocument.Uri =
			    lsconv::FileNameToDocumentURI(activeFilename);
			params->Position = currentCaretPosition;
			params->FilesToSearch = searchURIs;
			auto result = sendRequest(
			    t,
			    lsproto::CustomTextDocumentMultiDocumentHighlightInfo,
			    params);
			auto multiHighlights = result.MultiDocumentHighlights;
			if (multiHighlights == nullptr) {
				multiHighlights = std::make_shared<lsproto::Slice<
				    std::shared_ptr<lsproto::
				                        MultiDocumentHighlight>>>(
				    std::vector<std::shared_ptr<
				        lsproto::MultiDocumentHighlight>>{});
			}

			for (auto& mh : orNilSlice(multiHighlights)) {
				for (auto& h : sliceOr(mh->Highlights)) {
					spans.push_back(lsproto::Location{
					    .Uri = mh->Uri,
					    .Range = h->Range,
					});
				}
			}

			gostr::Builder sb;
			sb.WriteString("// filesToSearch:\n");
			for (auto& file : filesToSearch) {
				sb.WriteString("//   " + file + "\n");
			}
			sb.WriteString("\n");
			header = sb.String();
		} else {
			// Single-file: use the standard LSP method.
			auto params = std::make_shared<
			    lsproto::DocumentHighlightParams>();
			params->TextDocument.Uri =
			    lsconv::FileNameToDocumentURI(activeFilename);
			params->Position = currentCaretPosition;
			auto result = sendRequest(
			    t, lsproto::TextDocumentDocumentHighlightInfo,
			    params);
			auto highlights = result.DocumentHighlights;
			if (highlights == nullptr) {
				highlights = std::make_shared<lsproto::Slice<
				    std::shared_ptr<lsproto::
				                        DocumentHighlight>>>(
				    std::vector<std::shared_ptr<
				        lsproto::DocumentHighlight>>{});
			}

			for (auto& h : orNilSlice(highlights)) {
				spans.push_back(lsproto::Location{
				    .Uri = lsconv::FileNameToDocumentURI(
				        activeFilename),
				    .Range = h->Range,
				});
			}
		}

		// Add result to baseline
		addResultToBaseline(
		    t, documentHighlightsCmd,
		    header +
		        getBaselineForLocationsWithFileContents(
		            spans,
		            baselineFourslashLocationsOptions{
		                .marker = markerOrRange,
		                .markerName = "/*HIGHLIGHTS*/",
		            }));
	}
}

// Collects all named markers if provided, or defaults to anonymous
// ranges. lookupMarkersOrGetRanges — fourslash.go:3852.
std::vector<MarkerOrRange*> FourslashTest::lookupMarkersOrGetRanges(
    gostd::testing::T* t, const std::vector<std::string>& markers) {
	std::vector<MarkerOrRange*> referenceLocations;
	if (markers.empty()) {
		for (auto& r : testData->Ranges) {
			referenceLocations.push_back(r.get());
		}
	} else {
		for (auto& markerName : markers) {
			auto it = testData->MarkerPositions.find(markerName);
			if (it == testData->MarkerPositions.end()) {
				t->Fatalf("Marker '%s' not found", {markerName});
			}
			referenceLocations.push_back(it->second.get());
		}
	}
	return referenceLocations;
}


// ===========================================================================
// fourslash.go:3873-4115 — JSON roundtrip + editing (Insert through
// typeText)
// ===========================================================================

// Insert text at the current caret position. fourslash.go:3895.
void FourslashTest::Insert(gostd::testing::T* t,
                           const std::string& text) {
	t->Helper();
	baselineState(t);
	typeText(t, text);
}

// Insert text and a new line at the current caret position.
// fourslash.go:3901.
void FourslashTest::InsertLine(gostd::testing::T* t,
                               const std::string& text) {
	t->Helper();
	baselineState(t);
	typeText(t, text + "\n");
}

// Removes the text at the current caret position as if the user pressed
// backspace `count` times. fourslash.go:3908.
void FourslashTest::Backspace(gostd::testing::T* t, int count) {
	auto* script = getScriptInfo(activeFilename);
	int offset = (int)converters->LineAndCharacterToPosition(
	    script, currentCaretPosition);
	baselineState(t);

	for (int i = 0; i < count; i++) {
		offset--;
		editScriptAndUpdateMarkers(t, activeFilename, offset,
		                         offset + 1, "");
		currentCaretPosition =
		    converters->PositionToLineAndCharacter(
		        script, (TextPos)offset);
		// Don't need to examine formatting because there are no
		// formatting changes on backspace.
	}

	// f.checkPostEditInvariants() // !!! do we need this?
}

// DeleteAtCaret removes the text at the current caret position as if
// the user pressed delete `count` times. fourslash.go:3924.
void FourslashTest::DeleteAtCaret(gostd::testing::T* t, int count) {
	auto* script = getScriptInfo(activeFilename);
	int offset = (int)converters->LineAndCharacterToPosition(
	    script, currentCaretPosition);
	baselineState(t);

	for (int i = 0; i < count; i++) {
		editScriptAndUpdateMarkers(t, activeFilename, offset,
		                         offset + 1, "");
		// Position stays the same after delete (unlike backspace)
	}
}

// Enters text as if the user had pasted it. fourslash.go:3937.
void FourslashTest::Paste(gostd::testing::T* t,
                          const std::string& text) {
	auto* script = getScriptInfo(activeFilename);
	int start = (int)converters->LineAndCharacterToPosition(
	    script, currentCaretPosition);
	baselineState(t);
	editScriptAndUpdateMarkers(t, activeFilename, start, start, text);

	// post-paste fomatting
	if (stateEnableFormatting) {
		auto params = std::make_shared<
		    lsproto::DocumentRangeFormattingParams>();
		params->TextDocument.Uri =
		    lsconv::FileNameToDocumentURI(activeFilename);
		params->Range.Start = currentCaretPosition;
		params->Range.End = converters->PositionToLineAndCharacter(
		    script, (TextPos)(start + (int)text.size()));
		params->Options = std::shared_ptr<lsproto::FormattingOptions>(
		    userPreferences.FormatCodeSettings.ToLSFormatOptions());
		auto result = sendRequestAndBaselineWorker(
		    t, lsproto::TextDocumentRangeFormattingInfo, params,
		    false);
		if (result.TextEdits != nullptr) {
			applyTextEdits(t, **result.TextEdits);
		}
	}
	// this.checkPostEditInvariants(); // !!! do we need this?
}

// Selects a line and replaces it with a new text. fourslash.go:3962.
void FourslashTest::ReplaceLine(gostd::testing::T* t, int lineIndex,
                                const std::string& text) {
	baselineState(t);
	selectLine(t, lineIndex);
	typeText(t, text);
}

// selectLine — fourslash.go:3968.
void FourslashTest::selectLine(gostd::testing::T* t, int lineIndex) {
	auto* script = getScriptInfo(activeFilename);
	int start = script->lineMap->LineStarts[lineIndex];
	TextPos end;
	if (lineIndex + 1 >= (int)script->lineMap->LineStarts.size()) {
		end = TextPos((int32_t)script->content.size());
	} else {
		end = TextPos(
		    script->lineMap->LineStarts[lineIndex + 1] - 1);
	}
	selectRange(t, TextRange{(TextPos)start, end});
}

// selectRange — fourslash.go:3979.
void FourslashTest::selectRange(gostd::testing::T* t,
                                TextRange textRange) {
	auto* script = getScriptInfo(activeFilename);
	auto start = converters->PositionToLineAndCharacter(
	    script, (TextPos)textRange.pos());
	auto end = converters->PositionToLineAndCharacter(
	    script, (TextPos)textRange.end());
	goToPosition(t, start);
	selectionEnd = std::make_shared<lsproto::Position>(end);
}

// getSelection — fourslash.go:3987.
TextRange FourslashTest::getSelection() {
	auto* script = getScriptInfo(activeFilename);
	if (selectionEnd == nullptr) {
		return TextRange{
		    (TextPos)converters->LineAndCharacterToPosition(
		        script, currentCaretPosition),
		    (TextPos)converters->LineAndCharacterToPosition(
		        script, currentCaretPosition),
		};
	}
	return TextRange{
	    (TextPos)converters->LineAndCharacterToPosition(
	        script, currentCaretPosition),
	    (TextPos)converters->LineAndCharacterToPosition(
	        script, *selectionEnd),
	};
}

// Updates f.currentCaretPosition. applyTextEdits — fourslash.go:3998.
int FourslashTest::applyTextEdits(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<lsproto::TextEdit>>& edits) {
	auto* script = getScriptInfo(activeFilename);
	std::vector<std::shared_ptr<lsproto::TextEdit>> sortedEdits = edits;
	std::stable_sort(
	    sortedEdits.begin(), sortedEdits.end(),
	    [&](const std::shared_ptr<lsproto::TextEdit>& a,
	        const std::shared_ptr<lsproto::TextEdit>& b) {
		    auto aStart = converters->LineAndCharacterToPosition(
		        script, a->Range.Start);
		    auto bStart = converters->LineAndCharacterToPosition(
		        script, b->Range.Start);
		    return aStart < bStart;
	    });

	int totalOffset = 0;
	int currentCaretOffset = (int)converters->LineAndCharacterToPosition(
	    script, currentCaretPosition);
	// Apply edits in reverse order to avoid affecting the positions of
	// earlier edits.
	for (auto it = sortedEdits.rbegin(); it != sortedEdits.rend();
	     ++it) {
		auto& edit = *it;

		int start = (int)converters->LineAndCharacterToPosition(
		    script, edit->Range.Start);
		int end = (int)converters->LineAndCharacterToPosition(
		    script, edit->Range.End);
		editScriptAndUpdateMarkers(t, activeFilename, start, end,
		                         edit->NewText);

		int delta = (int)edit->NewText.size() - (end - start);
		if (start <= currentCaretOffset) {
			if (end <= currentCaretOffset) {
				// The entirety of the edit span falls before the
				// caret position, shift the caret accordingly
				currentCaretOffset += delta;
			} else {
				// The span being replaced includes the caret
				// position, place the caret at the beginning of
				// the span
				currentCaretOffset = start;
			}
		}
		totalOffset += delta;
	}
	currentCaretPosition = converters->PositionToLineAndCharacter(
	    script, (TextPos)currentCaretOffset);
	return totalOffset;
}

// Replace — fourslash.go:4036.
void FourslashTest::Replace(gostd::testing::T* t, int start, int length,
                            const std::string& text) {
	baselineState(t);
	replaceWorker(t, start, length, text);
}

// replaceWorker — fourslash.go:4041.
void FourslashTest::replaceWorker(gostd::testing::T* t, int start,
                                  int length, const std::string& text) {
	t->Helper();
	editScriptAndUpdateMarkers(t, activeFilename, start,
	                         start + length, text);
	// f.checkPostEditInvariants() // !!! do we need this?
}

// Inserts the text currently at the caret position character by
// character, as if the user typed it. typeText — fourslash.go:4047.
void FourslashTest::typeText(gostd::testing::T* t,
                             const std::string& text) {
	// temprorary -- this disables tests failing if format crashes; this
	// unblocks unrelated tests such as codefixes
	reportFormatOnTypeCrash = false;
	struct restoreFormatting {
		bool* flag;
		~restoreFormatting() { *flag = true; }
	} restore{&reportFormatOnTypeCrash};

	auto* script = getScriptInfo(activeFilename);
	auto selection = getSelection();
	replaceWorker(t, selection.pos(), selection.len(), "");

	int totalSize = 0;

	int offset = (int)converters->LineAndCharacterToPosition(
	    script, currentCaretPosition);
	while (totalSize < (int)text.size()) {
		auto [r, size] =
		    gostr::decodeRuneInString(text.substr(totalSize));
		editScriptAndUpdateMarkers(t, activeFilename, offset, offset,
		                         gostr::fromRune(r));

		totalSize += size;
		offset += size;
		currentCaretPosition =
		    converters->PositionToLineAndCharacter(
		        script, (TextPos)offset);

		// Handle post-keystroke formatting
		if (stateEnableFormatting) {
			auto params = std::make_shared<
			    lsproto::DocumentOnTypeFormattingParams>();
			params->TextDocument.Uri =
			    lsconv::FileNameToDocumentURI(activeFilename);
			params->Position = currentCaretPosition;
			params->Ch = gostr::fromRune(r);
			params->Options =
			    std::shared_ptr<lsproto::FormattingOptions>(
			        userPreferences.FormatCodeSettings
			            .ToLSFormatOptions());
			auto result = sendRequestAndBaselineWorker(
			    t, lsproto::TextDocumentOnTypeFormattingInfo, params,
			    false);
			if (result.TextEdits != nullptr) {
				offset += applyTextEdits(t, **result.TextEdits);
			}
		}
	}

	// f.checkPostEditInvariants() // !!! do we need this?
}


// ===========================================================================
// fourslash.go:4081-4764 — editScript family, quickinfo, closing tags,
// signature help
// ===========================================================================

// Edits the script and updates marker and range positions accordingly.
// This does not update the current caret position.
// fourslash.go:4082.
void FourslashTest::editScriptAndUpdateMarkers(
    gostd::testing::T* t, const std::string& fileName, int editStart,
    int editEnd, const std::string& newText) {
	editScriptAndUpdateMarkersWorker(
	    t, fileName,
	    std::vector<TextChange>{
	        TextChange{TextRange{(TextPos)editStart,
	                             (TextPos)editEnd},
	                   newText}});
}

// editScriptAndUpdateMarkersWorker — fourslash.go:4088.
void FourslashTest::editScriptAndUpdateMarkersWorker(
    gostd::testing::T* t, const std::string& fileName,
    const std::vector<TextChange>& changes) {
	// Sort changes by position (ascending) so we can apply in reverse
	std::vector<TextChange> sortedChanges = changes;
	// stable_sort: Go's slices.SortFunc (pdqsort) is not documented stable,
	// but our oracle testing showed deterministic tie-order matching
	// std::stable_sort — matching observed behavior is the safe port..
	std::stable_sort(sortedChanges.begin(), sortedChanges.end(),
	          [](const TextChange& a, const TextChange& b) {
		          return a.pos() < b.pos();
	          });

	// Apply changes in reverse order to preserve positions of earlier
	// changes
	for (auto it = sortedChanges.rbegin(); it != sortedChanges.rend();
	     ++it) {
		auto& change = *it;
		int editStart = (int)change.pos();
		int editEnd = (int)change.end();
		auto* script = editScript(t, fileName, change);
		for (auto& marker : testData->Markers) {
			if (marker->FileName() == fileName) {
				marker->Position =
				    updatePosition(marker->Position, editStart,
				                   editEnd, change.NewText);
				marker->LSPosition =
				    converters->PositionToLineAndCharacter(
				        script, (TextPos)marker->Position);
			}
		}
		for (auto& rangeMarker : testData->Ranges) {
			if (rangeMarker->FileName() == fileName) {
				int start =
				    updatePosition((int)rangeMarker->Range.pos(),
				                   editStart, editEnd, change.NewText);
				int end = updatePosition((int)rangeMarker->Range.end(),
				                         editStart, editEnd,
				                         change.NewText);
				rangeMarker->Range = TextRange{
				    (TextPos)start, (TextPos)end};
				rangeMarker->LSRange =
				    converters->ToLSPRange(script,
				                           rangeMarker->Range)
				        .first;
			}
		}
	}
	rangesByText.reset();
}

// updatePosition — fourslash.go:4125.
int updatePosition(int pos, int editStart, int editEnd,
                   const std::string& newText) {
	if (pos <= editStart) {
		return pos;
	}
	// If inside the edit, return -1 to mark as invalid
	if (pos < editEnd) {
		return -1;
	}
	return pos + (int)newText.size() - (editEnd - editStart);
}

// fromLSPRange — fourslash.go:4135.
TextRange FourslashTest::fromLSPRange(scriptInfo* script,
                                      const lsproto::Range& r) {
	auto ranges = converters->FromLSPRange(script, r, 0 /*featureSet*/);
	if (ranges.size() != 1) {
		return TextRange{};
	}
	return ranges[0].Span;
}

// editScript — fourslash.go:4143.
scriptInfo* FourslashTest::editScript(gostd::testing::T* t,
                                      const std::string& fileName,
                                      const TextChange& change) {
	auto* script = getOrLoadScriptInfo(fileName);
	if (script == nullptr) {
		TSC_UNREACHABLE(("Script info for file " + fileName +
		                " not found").c_str());
	}
	auto changeRange =
	    converters->ToLSPRange(script, (const TextRange&)change);
	script->editContent(change);
	if (auto err = vfs->WriteFile(fileName, script->content); err) {
		t->Fatalf("failed to write to VFS for %s: %v",
		          {fileName, err.str()});
	}
	auto params = std::make_shared<lsproto::DidChangeTextDocumentParams>();
	params->TextDocument.Uri = lsconv::FileNameToDocumentURI(fileName);
	params->TextDocument.Version = script->version;
	lsproto::TextDocumentContentChangePartialOrWholeDocument cc;
	cc.Partial = std::make_shared<
	    lsproto::TextDocumentContentChangePartial>();
	cc.Partial->Range = changeRange.first;
	cc.Partial->Text = change.NewText;
	params->ContentChanges = std::vector<
	    lsproto::TextDocumentContentChangePartialOrWholeDocument>{cc};
	sendNotification(t, lsproto::TextDocumentDidChangeInfo, params);
	return script;
}

// getScriptInfo — fourslash.go:4165.
scriptInfo* FourslashTest::getScriptInfo(const std::string& fileName) {
	auto it = scriptInfos.find(fileName);
	if (it == scriptInfos.end()) {
		return nullptr;
	}
	return it->second.get();
}

// getOrLoadScriptInfo — fourslash.go:4169.
scriptInfo* FourslashTest::getOrLoadScriptInfo(
    const std::string& fileName) {
	if (auto* script = getScriptInfo(fileName); script != nullptr) {
		return script;
	}
	auto [content, ok] = vfs->ReadFile(fileName);
	if (ok) {
		auto script = newScriptInfo(fileName, content);
		scriptInfos[fileName] = script;
		return script.get();
	}
	return nullptr;
}

// !!! expected tags
// VerifyQuickInfoAt — fourslash.go:4181.
void FourslashTest::VerifyQuickInfoAt(
    gostd::testing::T* t, const std::string& marker,
    const std::string& expectedText,
    const std::string& expectedDocumentation) {
	GoToMarker(t, marker);
	auto hover = getQuickInfoAtCurrentPosition(t);
	if (hover == nullptr) {
		t->Fatalf(
		    "Expected hover result at marker '%s' but got nil",
		    {*lastKnownMarkerName});
	}
	verifyHoverContent(t, hover->Contents, expectedText,
	                   expectedDocumentation,
	                   getCurrentPositionPrefix());
}

// getQuickInfoAtCurrentPosition — fourslash.go:4190.
std::shared_ptr<lsproto::Hover>
FourslashTest::getQuickInfoAtCurrentPosition(gostd::testing::T* t) {
	auto params = std::make_shared<lsproto::HoverParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Position = currentCaretPosition;
	auto result = sendRequest(t, lsproto::TextDocumentHoverInfo, params);
	return result.Hover;
}

// verifyHoverContent — fourslash.go:4201.
void FourslashTest::verifyHoverContent(
    gostd::testing::T* t,
    const lsproto::
        MarkupContentOrStringOrMarkedStringWithLanguageOrMarkedStrings&
            actual,
    const std::string& expectedText,
    const std::string& expectedDocumentation,
    const std::string& prefix) {
	if (actual.MarkupContent != nullptr) {
		verifyHoverMarkdown(t, actual.MarkupContent->Value,
		                    expectedText, expectedDocumentation,
		                    prefix);
	} else {
		t->Fatalf(
		    prefix + "Expected markup content, got: %s",
		    {tsc::cmp::diff(actual.MarkupContent, nullptr)});
	}
}

// verifyHoverMarkdown — fourslash.go:4215.
void FourslashTest::verifyHoverMarkdown(
    gostd::testing::T* t, const std::string& actual,
    const std::string& expectedText,
    const std::string& expectedDocumentation,
    const std::string& prefix) {
	std::string expected = gostd::sprintf(
	    "```typescript\n%s\n```\n%s",
	    {expectedText, expectedDocumentation});
	assertDeepEqual(t, actual, expected,
	                prefix + "Hover markdown content mismatch");
}

// VerifyQuickInfoExists — fourslash.go:4228.
void FourslashTest::VerifyQuickInfoExists(gostd::testing::T* t) {
	auto [isEmpty, _] = quickInfoIsEmpty(t);
	if (isEmpty) {
		t->Fatalf(
		    "Expected non-nil hover content at marker '%s'",
		    {*lastKnownMarkerName});
	}
}

// VerifyNotQuickInfoExists — fourslash.go:4234.
void FourslashTest::VerifyNotQuickInfoExists(gostd::testing::T* t) {
	auto [isEmpty, hover] = quickInfoIsEmpty(t);
	if (!isEmpty) {
		t->Fatalf(
		    "Expected empty hover content at marker '%s', got '%s'",
		    {*lastKnownMarkerName,
		     tsc::cmp::diff(hover, nullptr)});
	}
}

// quickInfoIsEmpty — fourslash.go:4240.
std::pair<bool, std::shared_ptr<lsproto::Hover>>
FourslashTest::quickInfoIsEmpty(gostd::testing::T* t) {
	auto hover = getQuickInfoAtCurrentPosition(t);
	if (hover == nullptr ||
	    (hover->Contents.MarkupContent == nullptr &&
	     hover->Contents.MarkedStrings == nullptr &&
	     hover->Contents.String == nullptr)) {
		return {true, nullptr};
	}
	return {false, hover};
}

// VerifyQuickInfoIs — fourslash.go:4249.
void FourslashTest::VerifyQuickInfoIs(
    gostd::testing::T* t, const std::string& expectedText,
    const std::string& expectedDocumentation) {
	auto hover = getQuickInfoAtCurrentPosition(t);
	verifyHoverContent(t, hover->Contents, expectedText,
	                   expectedDocumentation,
	                   getCurrentPositionPrefix());
}

// VerifyJsxClosingTag — fourslash.go:4254.
void FourslashTest::VerifyJsxClosingTag(
    gostd::testing::T* t,
    const std::unordered_map<std::string,
                             std::shared_ptr<std::string>>&
        markersToNewText) {
	for (auto& [marker, expectedText] : markersToNewText) {
		GoToMarker(t, marker);
		auto params =
		    std::make_shared<lsproto::VSOnAutoInsertParams>();
		params->VSTextDocument.Uri =
		    lsconv::FileNameToDocumentURI(activeFilename);
		params->VSPosition = currentCaretPosition;
		params->VSCh = ">";

		auto requestResult = sendRequest(
		    t, lsproto::TextDocumentVSOnAutoInsertInfo, params);

		std::shared_ptr<std::string> actualText;
		if (auto& item = requestResult.VSOnAutoInsertResponseItem;
		    item != nullptr && item->VSTextEdit != nullptr) {
			std::string newText = item->VSTextEdit->NewText;
			if (item->VSTextEditFormat ==
			    lsproto::InsertTextFormatSnippet) {
				auto [rest, ok] = gostr::cutPrefix(newText, "$0");
				if (!ok) {
					t->Fatalf(
					    "%sexpected JSX closing tag snippet "
					    "to begin with $0, got %q",
					    {getCurrentPositionPrefix(),
					     item->VSTextEdit->NewText});
				}
				newText = rest;
			}
			actualText = std::make_shared<std::string>(newText);
		}
		assertDeepEqual(t, actualText, expectedText,
		                getCurrentPositionPrefix() +
		                    "JSX closing tag text mismatch");
	}
}

// VerifyBaselineClosingTags generates a baseline for JSX closing tag
// completions at all markers. fourslash.go:4281.
void FourslashTest::VerifyBaselineClosingTags(gostd::testing::T* t) {
	t->Helper();

	auto markersAndItems = gostr::coreMapFiltered(
	    Markers(),
	    [&](const std::shared_ptr<Marker>& marker)
	        -> std::optional<markerAndItem<std::shared_ptr<
	            lsproto::VSOnAutoInsertResponseItem>>> {
		    if (marker->Name == nullptr) {
			    return std::nullopt;
		    }

		    auto params =
		        std::make_shared<lsproto::VSOnAutoInsertParams>();
		    params->VSTextDocument.Uri =
		        lsconv::FileNameToDocumentURI(marker->FileName());
		    params->VSPosition = marker->LSPosition;
		    params->VSCh = ">";

		    auto result = sendRequest(
		        t, lsproto::TextDocumentVSOnAutoInsertInfo, params);
		    return markerAndItem<std::shared_ptr<
		        lsproto::VSOnAutoInsertResponseItem>>{
		        marker, result.VSOnAutoInsertResponseItem};
	    });

	auto getRange =
	    [](const std::shared_ptr<lsproto::VSOnAutoInsertResponseItem>&
	           item) -> std::shared_ptr<lsproto::Range> {
		// Returning nil lets annotateContentWithTooltips render the
		// caret marker at the marker position. The text edit's range
		// is zero-width at the cursor, which would render as an
		// empty underline.
		return nullptr;
	};

	auto getTooltipLines =
	    [](const std::shared_ptr<lsproto::VSOnAutoInsertResponseItem>&
	           item,
	       const std::shared_ptr<lsproto::VSOnAutoInsertResponseItem>&
	           _prev) -> std::vector<std::string> {
		if (item == nullptr || item->VSTextEdit == nullptr) {
			return {"No closing tag"};
		}
		std::string format = "plaintext";
		if (item->VSTextEditFormat ==
		    lsproto::InsertTextFormatSnippet) {
			format = "snippet";
		}
		return {gostd::sprintf("%s: %q",
		                       {format, item->VSTextEdit->NewText})};
	};

	auto result = annotateContentWithTooltips(t, markersAndItems,
	                                          "closing tag", getRange,
	                                          getTooltipLines);
	addResultToBaseline(t, closingTagCmd, result);
}

// VerifySignatureHelp verifies signature help at the current position
// matches the expected options. fourslash.go:4354.
void FourslashTest::VerifySignatureHelp(
    gostd::testing::T* t, const VerifySignatureHelpOptions& expected) {
	t->Helper();
	std::string prefix = getCurrentPositionPrefix();
	auto params =
	    std::make_shared<lsproto::SignatureHelpParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Position = currentCaretPosition;
	auto result =
	    sendRequest(t, lsproto::TextDocumentSignatureHelpInfo, params);
	auto help = result.SignatureHelp;
	if (help == nullptr) {
		t->Fatalf("%sCould not get signature help", {prefix});
	}

	// Determine which signature to check
	int selectedIndex = 0;
	if (expected.OverrideSelectedItemIndex > 0) {
		selectedIndex = expected.OverrideSelectedItemIndex;
	} else if (help->ActiveSignature.has_value()) {
		selectedIndex = (int)*help->ActiveSignature;
	}

	if (selectedIndex >= (int)help->Signatures->size()) {
		t->Fatalf(
		    "%sSelected signature index %d out of range (have %d "
		    "signatures)",
		    {prefix, selectedIndex, (int)help->Signatures->size()});
	}

	auto& selectedSig = (*help->Signatures)[selectedIndex];

	// Verify overloads count
	if (expected.OverloadsCount > 0) {
		if ((int)help->Signatures->size() !=
		    expected.OverloadsCount) {
			t->Errorf("%sExpected %d overloads, got %d",
			          {prefix, expected.OverloadsCount,
			           (int)help->Signatures->size()});
		}
	}

	// Verify signature text
	if (!expected.Text.empty()) {
		if (selectedSig->Label != expected.Text) {
			t->Errorf("%sExpected signature text %q, got %q",
			          {prefix, expected.Text, selectedSig->Label});
		}
	}

	// Verify doc comment
	if (!expected.DocComment.empty()) {
		std::string actualDoc;
		if (selectedSig->Documentation != nullptr) {
			if (selectedSig->Documentation->MarkupContent !=
			    nullptr) {
				actualDoc = selectedSig->Documentation
				                ->MarkupContent->Value;
			} else if (selectedSig->Documentation->String !=
			           nullptr) {
				actualDoc =
				    *selectedSig->Documentation->String;
			}
		}
		if (actualDoc != expected.DocComment) {
			t->Errorf("%sExpected doc comment %q, got %q",
			          {prefix, expected.DocComment, actualDoc});
		}
	}

	// Verify parameter count
	if (expected.ParameterCount > 0) {
		int paramCount = 0;
		if (selectedSig->Parameters != nullptr) {
			paramCount = (int)orNilSlice(selectedSig->Parameters)
			                             .size();
		}
		if (paramCount != expected.ParameterCount) {
			t->Errorf("%sExpected %d parameters, got %d",
			          {prefix, expected.ParameterCount, paramCount});
		}
	}

	// Get active parameter
	int activeParamIndex = 0;
	if (selectedSig->ActiveParameter != nullptr &&
	    selectedSig->ActiveParameter->Uinteger != nullptr) {
		activeParamIndex =
		    (int)*selectedSig->ActiveParameter->Uinteger;
	} else if (help->ActiveParameter != nullptr &&
	           help->ActiveParameter->Uinteger != nullptr) {
		activeParamIndex = (int)*help->ActiveParameter->Uinteger;
	}

	std::shared_ptr<lsproto::ParameterInformation> activeParam;
	if (selectedSig->Parameters != nullptr &&
	    activeParamIndex < (int)orNilSlice(selectedSig->Parameters).size()) {
		activeParam = orNilSlice(selectedSig->Parameters)[activeParamIndex];
	}

	// Verify parameter name
	if (!expected.ParameterName.empty()) {
		if (activeParam == nullptr) {
			t->Errorf(
			    "%sExpected parameter name %q, but no active "
			    "parameter",
			    {prefix, expected.ParameterName});
		} else {
			// Parameter name is extracted from the label
			std::string actualName;
			if (activeParam->Label.String != nullptr) {
				// Extract name from label like "x: string" -> "x"
				// or "T extends Foo" -> "T" or "...x: any[]" ->
				// "x"
				std::string label = *activeParam->Label.String;
				// Strip leading "..." for rest parameters
				label = gostr::trimPrefix(label, "...");
				if (auto [name, _, found] =
				        gostr::cut(label, ":");
				    found) {
					actualName = gostr::trimSpace(name);
				} else if (auto [name2, _2, found2] =
				               gostr::cut(label, " extends ");
				           found2) {
					actualName = gostr::trimSpace(name2);
				} else {
					actualName = label;
				}
			}
			if (actualName != expected.ParameterName) {
				t->Errorf(
				    "%sExpected parameter name %q, got %q",
				    {prefix, expected.ParameterName,
				     actualName});
			}
		}
	}

	// Verify parameter span (label)
	if (!expected.ParameterSpan.empty()) {
		if (activeParam == nullptr) {
			t->Errorf(
			    "%sExpected parameter span %q, but no active "
			    "parameter",
			    {prefix, expected.ParameterSpan});
		} else {
			std::string actualSpan;
			if (activeParam->Label.String != nullptr) {
				actualSpan = *activeParam->Label.String;
			}
			if (actualSpan != expected.ParameterSpan) {
				t->Errorf(
				    "%sExpected parameter span %q, got %q",
				    {prefix, expected.ParameterSpan,
				     actualSpan});
			}
		}
	}

	// Verify parameter doc comment
	if (!expected.ParameterDocComment.empty()) {
		if (activeParam == nullptr) {
			t->Errorf(
			    "%sExpected parameter doc comment %q, but no "
			    "active parameter",
			    {prefix, expected.ParameterDocComment});
		} else {
			std::string actualDoc;
			if (activeParam->Documentation != nullptr) {
				if (activeParam->Documentation->MarkupContent !=
				    nullptr) {
					actualDoc = activeParam->Documentation
					                ->MarkupContent->Value;
				} else if (activeParam->Documentation->String !=
				           nullptr) {
					actualDoc =
					    *activeParam->Documentation->String;
				}
			}
			if (actualDoc != expected.ParameterDocComment) {
				t->Errorf(
				    "%sExpected parameter doc comment %q, "
				    "got %q",
				    {prefix, expected.ParameterDocComment,
				     actualDoc});
			}
		}
	}

	// Verify isVariadic (check if any parameter starts with "...")
	if (expected.IsVariadicSet) {
		bool actualIsVariadic = false;
		if (selectedSig->Parameters != nullptr) {
			for (auto& param : orNilSlice(selectedSig->Parameters)) {
				if (param->Label.String != nullptr &&
				    gostr::hasPrefix(*param->Label.String,
				                     "...")) {
					actualIsVariadic = true;
					break;
				}
			}
		}
		if (actualIsVariadic != expected.IsVariadic) {
			t->Errorf("%sExpected isVariadic=%v, got %v",
			          {prefix, expected.IsVariadic,
			           actualIsVariadic});
		}
	}
}

// VerifyNoSignatureHelp verifies that no signature help is available at
// the current position. fourslash.go:4497.
void FourslashTest::VerifyNoSignatureHelp(gostd::testing::T* t) {
	t->Helper();
	std::string prefix = getCurrentPositionPrefix();
	auto params =
	    std::make_shared<lsproto::SignatureHelpParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Position = currentCaretPosition;
	auto result =
	    sendRequest(t, lsproto::TextDocumentSignatureHelpInfo, params);
	if (result.SignatureHelp != nullptr &&
	    result.SignatureHelp->Signatures.has_value() &&
	    result.SignatureHelp->Signatures->size() > 0) {
		t->Errorf(
		    "%sExpected no signature help, but got %d signatures",
		    {prefix,
		     (int)result.SignatureHelp->Signatures->size()});
	}
}

// VerifyNoSignatureHelpWithContext — fourslash.go:4514.
void FourslashTest::VerifyNoSignatureHelpWithContext(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::SignatureHelpContext>& context) {
	t->Helper();
	std::string prefix = getCurrentPositionPrefix();
	auto params =
	    std::make_shared<lsproto::SignatureHelpParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Position = currentCaretPosition;
	params->Context = context;
	auto result =
	    sendRequest(t, lsproto::TextDocumentSignatureHelpInfo, params);
	if (result.SignatureHelp != nullptr &&
	    result.SignatureHelp->Signatures.has_value() &&
	    result.SignatureHelp->Signatures->size() > 0) {
		t->Errorf(
		    "%sExpected no signature help, but got %d signatures",
		    {prefix,
		     (int)result.SignatureHelp->Signatures->size()});
	}
}

// VerifyNoSignatureHelpForMarkersWithContext — fourslash.go:4532.
void FourslashTest::VerifyNoSignatureHelpForMarkersWithContext(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::SignatureHelpContext>& context,
    const std::vector<std::string>& markers) {
	t->Helper();
	for (auto& marker : markers) {
		GoToMarker(t, marker);
		VerifyNoSignatureHelpWithContext(t, context);
	}
}

// VerifySignatureHelpPresent verifies that signature help is available
// at the current position with a given context. fourslash.go:4540.
void FourslashTest::VerifySignatureHelpPresent(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::SignatureHelpContext>& context) {
	t->Helper();
	std::string prefix = getCurrentPositionPrefix();
	auto params =
	    std::make_shared<lsproto::SignatureHelpParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Position = currentCaretPosition;
	params->Context = context;
	auto result =
	    sendRequest(t, lsproto::TextDocumentSignatureHelpInfo, params);
	if (result.SignatureHelp == nullptr ||
	    !result.SignatureHelp->Signatures.has_value() ||
	    result.SignatureHelp->Signatures->empty()) {
		t->Errorf(
		    "%sExpected signature help to be present, but got none",
		    {prefix});
	}
}

// VerifySignatureHelpPresentForMarkers — fourslash.go:4558.
void FourslashTest::VerifySignatureHelpPresentForMarkers(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::SignatureHelpContext>& context,
    const std::vector<std::string>& markers) {
	t->Helper();
	for (auto& marker : markers) {
		GoToMarker(t, marker);
		VerifySignatureHelpPresent(t, context);
	}
}

// VerifyNoSignatureHelpForMarkers — fourslash.go:4566.
void FourslashTest::VerifyNoSignatureHelpForMarkers(
    gostd::testing::T* t, const std::vector<std::string>& markers) {
	t->Helper();
	for (auto& marker : markers) {
		GoToMarker(t, marker);
		VerifyNoSignatureHelp(t);
	}
}

// VerifySignatureHelpWithCases verifies signature help using detailed
// SignatureHelpCase structs. This is useful for more complex tests that
// need to verify the full signature help response. fourslash.go:4592.
void FourslashTest::VerifySignatureHelpWithCases(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<SignatureHelpCase>>&
        signatureHelpCases) {
	for (auto& option : signatureHelpCases) {
		if (auto* marker =
		        std::get_if<std::string>(&option->MarkerInput)) {
			GoToMarker(t, *marker);
			verifySignatureHelp(t, option->Context,
			                    option->Expected);
		} else if (auto* marker2 = std::get_if<
		               std::shared_ptr<Marker>>(
		               &option->MarkerInput)) {
			goToMarker(t, marker2->get());
			verifySignatureHelp(t, option->Context,
			                    option->Expected);
		} else if (auto* marker3 = std::get_if<
		               std::vector<std::string>>(
		               &option->MarkerInput)) {
			for (auto& markerName : *marker3) {
				GoToMarker(t, markerName);
				verifySignatureHelp(t, option->Context,
				                    option->Expected);
			}
		} else if (auto* marker4 = std::get_if<
		               std::vector<std::shared_ptr<Marker>>>(
		               &option->MarkerInput)) {
			for (auto& marker : *marker4) {
				goToMarker(t, marker.get());
				verifySignatureHelp(t, option->Context,
				                    option->Expected);
			}
		} else if (std::holds_alternative<std::monostate>(
		               option->MarkerInput)) {
			verifySignatureHelp(t, option->Context,
			                    option->Expected);
		} else {
			t->Fatalf(
			    "Invalid marker input type: %T. Expected "
			    "string, *Marker, []string, or []*Marker.",
			    {gostr::variantTypeName(option->MarkerInput)});
		}
	}
}

// verifySignatureHelp — fourslash.go:4619.
void FourslashTest::verifySignatureHelp(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::SignatureHelpContext>& context,
    const std::shared_ptr<lsproto::SignatureHelp>& expected) {
	std::string prefix = getCurrentPositionPrefix();
	auto params =
	    std::make_shared<lsproto::SignatureHelpParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Position = currentCaretPosition;
	params->Context = context;
	auto result =
	    sendRequest(t, lsproto::TextDocumentSignatureHelpInfo, params);
	verifySignatureHelpResult(t, result.SignatureHelp, expected,
	                          prefix);
}

// verifySignatureHelpResult — fourslash.go:4634.
void FourslashTest::verifySignatureHelpResult(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::SignatureHelp>& actual,
    const std::shared_ptr<lsproto::SignatureHelp>& expected,
    const std::string& prefix) {
	assertDeepEqual(t, actual, expected, prefix + " SignatureHelp mismatch");
}

// getCurrentPositionPrefix — fourslash.go:4642.
std::string FourslashTest::getCurrentPositionPrefix() {
	if (lastKnownMarkerName != nullptr) {
		return gostd::sprintf("At marker '%s': ",
		                      {*lastKnownMarkerName});
	}
	return gostd::sprintf(
	    "At position %s(Ln %d, Col %d): ",
	    {activeFilename, (int)currentCaretPosition.Line,
	     (int)currentCaretPosition.Character});
}

// BaselineAutoImportsCompletions — fourslash.go:4650.
void FourslashTest::BaselineAutoImportsCompletions(
    gostd::testing::T* t,
    const std::vector<std::string>& markerNames) {
	t->Helper();
	auto reset = ConfigureWithReset(t, lsutil::UserPreferences{
	    .IncludeCompletionsForModuleExports = Tristate::True,
	    .IncludeCompletionsForImportStatements = Tristate::True,
	    .ImportModuleSpecifierPreference =
	        userPreferences.ImportModuleSpecifierPreference,
	    .ImportModuleSpecifierEnding =
	        userPreferences.ImportModuleSpecifierEnding,
	    .AutoImportSpecifierExcludeRegexes =
	        userPreferences.AutoImportSpecifierExcludeRegexes,
	    .AutoImportFileExcludePatterns =
	        userPreferences.AutoImportFileExcludePatterns,
	    .AutoImportEntrypointDirectorySearch =
	        userPreferences.AutoImportEntrypointDirectorySearch,
	    .PreferTypeOnlyAutoImports =
	        userPreferences.PreferTypeOnlyAutoImports,
	});
	// Go `defer reset()` — runs at function end (after the loop).
	for (auto& markerName : markerNames) {
		GoToMarker(t, markerName);
		auto params =
		    std::make_shared<lsproto::CompletionParams>();
		params->TextDocument.Uri =
		    lsconv::FileNameToDocumentURI(activeFilename);
		params->Position = currentCaretPosition;
		params->Context =
		    std::make_shared<lsproto::CompletionContext>();
		auto result = sendRequest(
		    t, lsproto::TextDocumentCompletionInfo, params);

		std::string prefix =
		    gostd::sprintf("At marker '%s': ", {markerName});

		writeToBaseline(autoImportsCmd, "// === Auto Imports === \n");

		auto [fileContent, ok] = textOfFile(activeFilename);
		if (!ok) {
			t->Fatalf(
			    prefix + "Failed to read file %s for auto-import "
			             "baseline",
			    {activeFilename});
		}

		auto marker = testData->MarkerPositions.at(markerName);
		std::string ext = gostr::trimPrefix(
		    std::string(tspath::getAnyExtensionFromPath(
		        activeFilename, {}, true)),
		    ".");
		std::string lang = gostr::ifElse<std::string>(
		    ext == "mts" || ext == "cts", "ts", ext);
		writeToBaseline(
		    autoImportsCmd,
		    codeFence(lang, "// @FileName: " + activeFilename +
		                        "\n" +
		                        fileContent.substr(0,
		                                           marker->Position) +
		                        "/*" + markerName + "*/" +
		                        fileContent.substr(marker->Position)));

		auto currentFile = newScriptInfo(activeFilename, fileContent);
		testConverters converters(
		    lsproto::PositionEncodingKindUTF8,
		    [&](const std::string&) -> lsconv::LSPLineMap* {
			    return currentFile->lineMap;
		    });
		std::vector<std::shared_ptr<lsproto::CompletionItem>> list;
		if (result.Items == nullptr || orNilSlice(result.Items).empty()) {
			if (result.List == nullptr ||
			    !result.List->Items.has_value() ||
			    result.List->Items->empty()) {
				writeToBaseline(autoImportsCmd,
				                "no autoimport completions "
				                "found\n\n");
				continue;
			}
			list = *result.List->Items;
		} else {
			list = orNilSlice(result.Items);
		}

		for (auto& item : list) {
			if (item->Data == nullptr ||
			    item->SortText.value_or("") !=
			        ls::SortTextAutoImportSuggestions) {
				continue;
			}
			auto details = sendRequest(
			    t, lsproto::CompletionItemResolveInfo, item);
			if (details == nullptr ||
			    details->AdditionalTextEdits == nullptr ||
			    orNilSlice(details->AdditionalTextEdits).empty()) {
				t->Fatalf(
				    prefix + "Entry %s from %s returned no "
				             "code changes from completion "
				             "details request",
				    {item->Label,
				     item->Detail.value_or("")});
			}
			auto allChanges =
			    orNilSlice(details->AdditionalTextEdits);

			// !!! calculate the change provided by the
			// completiontext
			// completionChange:= &lsproto.TextEdit{}
			// if details.TextEdit != nil {
			// 	completionChange = details.TextEdit.TextEdit
			// } else if details.AdditionalTextEdits != nil &&
			// len(*details.AdditionalTextEdits) > 0 {
			// 	completionChange = (*details.AdditionalTextEdits)[0]
			// } else {
			// 	completionChange.Range = lsproto.Range{ Start:
			// marker.LSPosition, End: marker.LSPosition }
			// 	if item.InsertText != nil {
			// 		completionChange.NewText = *item.InsertText
			// 	} else {
			// 		completionChange.NewText = item.Label
			// 	}
			// }
			// allChanges := append(allChanges, completionChange)
			// sorted from back-of-file-most to front-of-file-most
			// stable_sort: Go's slices.SortFunc preserves equal-key
			// order (same-position edits keep insertion order).
			std::stable_sort(
			    allChanges.begin(), allChanges.end(),
			    [](const std::shared_ptr<lsproto::TextEdit>& a,
			       const std::shared_ptr<lsproto::TextEdit>& b) {
				    return lsproto::ComparePositions(
				               b->Range.Start,
				               a->Range.Start) < 0;
			    });
			std::string newFileContent = fileContent;
			for (auto& change : allChanges) {
				newFileContent =
				    newFileContent.substr(
				        0,
				        (size_t)converters
				            .LineAndCharacterToPosition(
				                currentFile.get(),
				                change->Range.Start)) +
				    change->NewText +
				    newFileContent.substr(
				        (size_t)converters
				            .LineAndCharacterToPosition(
				                currentFile.get(),
				                change->Range.End));
			}
			writeToBaseline(autoImportsCmd,
			                codeFence(lang, newFileContent) +
			                    "\n\n");
		}
	}
	reset();
}


// ===========================================================================
// fourslash.go:4765-5100 — rename
// ===========================================================================

// VerifyBaselineRename — fourslash.go:4750.
void FourslashTest::VerifyBaselineRename(
    gostd::testing::T* t,
    const std::shared_ptr<lsutil::UserPreferences>& preferences,
    const std::vector<MarkerOrRangeOrName>& markerOrNameOrRanges) {
	std::vector<MarkerOrRange*> markerOrRanges;
	for (auto& markerOrNameOrRange : markerOrNameOrRanges) {
		if (auto* name =
		        std::get_if<std::string>(&markerOrNameOrRange)) {
			auto it = testData->MarkerPositions.find(*name);
			if (it == testData->MarkerPositions.end()) {
				t->Fatalf("Marker '%s' not found", {*name});
			}
			markerOrRanges.push_back(it->second.get());
		} else if (auto* marker = std::get_if<std::shared_ptr<Marker>>(
		               &markerOrNameOrRange)) {
			markerOrRanges.push_back(marker->get());
		} else if (auto* rangeMarker =
		               std::get_if<std::shared_ptr<RangeMarker>>(
		                   &markerOrNameOrRange)) {
			markerOrRanges.push_back(rangeMarker->get());
		} else {
			t->Fatalf(
			    "Invalid marker or range type: %T. Expected "
			    "string, *Marker, or *RangeMarker.",
			    {gostr::variantTypeName(markerOrNameOrRange)});
		}
	}

	verifyBaselineRename(t, preferences, markerOrRanges);
}

// verifyBaselineRename — fourslash.go:4776.
void FourslashTest::verifyBaselineRename(
    gostd::testing::T* t,
    const std::shared_ptr<lsutil::UserPreferences>& preferences,
    const std::vector<MarkerOrRange*>& markerOrRanges) {
	std::function<void()> reset;
	if (preferences != nullptr) {
		reset = ConfigureWithReset(t, *preferences);
	}
	struct deferReset {
		std::function<void()>& f;
		~deferReset() {
			if (f) f();
		}
	} _reset{reset};

	for (auto* markerOrRange : markerOrRanges) {
		GoToMarkerOrRange(t, markerOrRange);

		auto params = std::make_shared<lsproto::RenameParams>();
		params->TextDocument.Uri =
		    lsconv::FileNameToDocumentURI(activeFilename);
		params->Position = currentCaretPosition;
		params->NewName = "?";

		auto result =
		    sendRequest(t, lsproto::TextDocumentRenameInfo, params);

		std::unordered_map<lsproto::DocumentUri,
		                   std::vector<
		                       std::shared_ptr<lsproto::TextEdit>>>
		    changes;
		if (result.WorkspaceEdit != nullptr &&
		    result.WorkspaceEdit->Changes != nullptr) {
			for (auto& [uri, sl] : *result.WorkspaceEdit->Changes) {
				changes[uri] = sliceOr(sl);
			}
		}
		std::unordered_map<documentSpan, std::string,
		                   documentSpanHash>
		    spanToText;
		collections::MultiMap<lsproto::DocumentUri, documentSpan>
		    fileToSpan;
		for (auto& [uri, edits] : changes) {
			for (auto& edit : edits) {
				documentSpan span{.uri = uri,
				                  .textSpan = edit->Range};
				fileToSpan.Add(uri, span);
				spanToText[span] = edit->NewText;
			}
		}

		gostr::Builder renameOptions;
		if (preferences != nullptr) {
			if (preferences->UseAliasesForRename !=
			    Tristate::Unknown) {
				renameOptions.WriteString(gostd::sprintf(
				    "// @useAliasesForRename: %v\n",
				    {preferences->UseAliasesForRename ==
				         Tristate::True}));
			}
			if (preferences->QuotePreference !=
			    lsutil::QuotePreferenceUnknown) {
				renameOptions.WriteString(gostd::sprintf(
				    "// @quotePreference: %v\n",
				    {preferences->QuotePreference}));
			}
		}

		auto baselineFileContent =
		    getBaselineForGroupedSpansWithFileContents(
		        &fileToSpan,
		        baselineFourslashLocationsOptions{
		            .marker = markerOrRange,
		            .markerName = "/*RENAME*/",
		            .endMarker = "RENAME|]",
		            .startMarkerPrefix =
		                [&spanToText](const documentSpan& span)
		                -> std::shared_ptr<std::string> {
			            auto it = spanToText.find(span);
			            std::string text =
			                it != spanToText.end() ? it->second : "";
			            auto prefixAndSuffix =
			                gostr::split(text, "?");
			            if (!prefixAndSuffix.empty() &&
			                prefixAndSuffix[0] != "") {
				            return std::make_shared<std::string>(
				                "/*START PREFIX*/" +
				                prefixAndSuffix[0]);
			            }
			            return nullptr;
			        },
		            .endMarkerSuffix =
		                [&spanToText](const documentSpan& span)
		                -> std::shared_ptr<std::string> {
			            auto it = spanToText.find(span);
			            std::string text =
			                it != spanToText.end() ? it->second : "";
			            auto prefixAndSuffix =
			                gostr::split(text, "?");
			            if (prefixAndSuffix.size() > 1 &&
			                prefixAndSuffix[1] != "") {
				            return std::make_shared<std::string>(
				                prefixAndSuffix[1] +
				                "/*END SUFFIX*/");
			            }
			            return nullptr;
			        },
		        });

		std::string baselineResult;
		if (renameOptions.Len() > 0) {
			baselineResult =
			    renameOptions.String() + "\n" + baselineFileContent;
		} else {
			baselineResult = baselineFileContent;
		}

		addResultToBaseline(t, renameCmd, baselineResult);
	}
}

// VerifyRenameSucceeded — fourslash.go:4853.
void FourslashTest::VerifyRenameSucceeded(
    gostd::testing::T* t,
    const std::shared_ptr<lsutil::UserPreferences>& preferences) {
	std::function<void()> reset;
	if (preferences != nullptr) {
		reset = ConfigureWithReset(t, *preferences);
	}
	struct deferReset {
		std::function<void()>& f;
		~deferReset() {
			if (f) f();
		}
	} _reset{reset};
	auto params =
	    std::make_shared<lsproto::PrepareRenameParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Position = currentCaretPosition;

	std::string prefix = getCurrentPositionPrefix();
	auto result = sendRequest(
	    t, lsproto::TextDocumentPrepareRenameInfo, params);
	if (result.Range == nullptr &&
	    result.PrepareRenamePlaceholder == nullptr &&
	    result.PrepareRenameDefaultBehavior == nullptr) {
		t->Fatal(
		    {prefix + "Expected rename to succeed, but "
		              "prepareRename returned null"});
	}

	// Also verify that textDocument/rename produces edits, since
	// prepareRename is optional.
	auto renameParams = std::make_shared<lsproto::RenameParams>();
	renameParams->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	renameParams->Position = currentCaretPosition;
	renameParams->NewName = "RENAME_SUCCEEDED_TEST";
	auto renameResult =
	    sendRequest(t, lsproto::TextDocumentRenameInfo, renameParams);
	if (renameResult.WorkspaceEdit == nullptr ||
	    renameResult.WorkspaceEdit->Changes == nullptr ||
	    renameResult.WorkspaceEdit->Changes->empty()) {
		t->Fatal(
		    {prefix + "prepareRename succeeded but "
		              "textDocument/rename returned no changes"});
	}
}

// VerifyRenameRange — fourslash.go:4879.
void FourslashTest::VerifyRenameRange(
    gostd::testing::T* t, const lsproto::Range& expectedRange,
    const std::string& expectedPlaceholder,
    const std::shared_ptr<lsutil::UserPreferences>& preferences) {
	t->Helper();
	std::function<void()> reset;
	if (preferences != nullptr) {
		reset = ConfigureWithReset(t, *preferences);
	}
	struct deferReset {
		std::function<void()>& f;
		~deferReset() {
			if (f) f();
		}
	} _reset{reset};
	auto params =
	    std::make_shared<lsproto::PrepareRenameParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Position = currentCaretPosition;

	auto result = sendRequest(
	    t, lsproto::TextDocumentPrepareRenameInfo, params);
	if (result.PrepareRenamePlaceholder == nullptr) {
		t->Fatal(
		    {getCurrentPositionPrefix() +
		     "Expected prepareRename to return a range and "
		     "placeholder"});
	}
	assertCheck(t, result.PrepareRenamePlaceholder->Range ==
	                   expectedRange,
	            "prepareRename range mismatch");
	assertEqual(t, result.PrepareRenamePlaceholder->Placeholder,
	            expectedPlaceholder);
}

// RenameAtCaret — fourslash.go:4896.
lsproto::RenameResponse FourslashTest::RenameAtCaret(
    gostd::testing::T* t, const std::string& newName) {
	t->Helper();
	auto params = std::make_shared<lsproto::RenameParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Position = currentCaretPosition;
	params->NewName = newName;
	auto result =
	    sendRequest(t, lsproto::TextDocumentRenameInfo, params);

	if (result.WorkspaceEdit == nullptr) {
		return result;
	}

	if (result.WorkspaceEdit->Changes != nullptr) {
		for (auto& [uri, edits] : *result.WorkspaceEdit->Changes) {
			std::string fileName = lsproto::documentUriFileName(uri);
			auto* script = getOrLoadScriptInfo(fileName);
			std::vector<TextChange> changes;
			for (auto& edit : sliceOr(edits)) {
				changes.push_back(TextChange{
				    TextRange{
				        fromLSPRange(script, edit->Range)},
				    edit->NewText});
			}
			editScriptAndUpdateMarkersWorker(t, fileName, changes);
		}
	}

	std::vector<std::shared_ptr<lsproto::RenameFile>> renameFiles;
	if (result.WorkspaceEdit->DocumentChanges != nullptr) {
		for (auto& docChange : orNilSlice(result.WorkspaceEdit->DocumentChanges)) {
			if (docChange.TextDocumentEdit != nullptr) {
				std::string fileName = lsproto::documentUriFileName(
				    docChange.TextDocumentEdit->TextDocument.Uri);
				auto* script = getOrLoadScriptInfo(fileName);
				std::vector<TextChange> changes;
				for (auto& edit :
				     sliceOr(docChange.TextDocumentEdit->Edits)) {
					auto& textEdit = edit.TextEdit;
					changes.push_back(TextChange{
					    TextRange{fromLSPRange(
					        script, textEdit->Range)},
					    textEdit->NewText});
				}
				editScriptAndUpdateMarkersWorker(t, fileName,
				                                 changes);
			} else if (docChange.RenameFile != nullptr) {
				renameFiles.push_back(docChange.RenameFile);
			}
		}
	}

	if (!renameFiles.empty()) {
		std::vector<std::shared_ptr<lsproto::FileRename>> fileRenames;
		for (auto& renameFile : renameFiles) {
			auto fr = std::make_shared<lsproto::FileRename>();
			fr->OldUri = renameFile->OldUri;
			fr->NewUri = renameFile->NewUri;
			fileRenames.push_back(fr);
		}
		if (capabilities != nullptr &&
		    capabilities->Workspace != nullptr &&
		    capabilities->Workspace->FileOperations != nullptr &&
		    capabilities->Workspace->FileOperations->WillRename
		        .has_value() &&
		    *capabilities->Workspace->FileOperations->WillRename) {
			willRenameFilesWorker(t, fileRenames);
		} else {
			for (auto& renameFile : renameFiles) {
				renameFileOrDirectory(
				    t, lsproto::documentUriFileName(renameFile->OldUri),
				    lsproto::documentUriFileName(renameFile->NewUri));
			}
		}
	}

	return result;
}

// WillRenameFiles — fourslash.go:4958.
lsproto::WillRenameFilesResponse FourslashTest::WillRenameFiles(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<lsproto::FileRename>>& files) {
	t->Helper();
	auto params =
	    std::make_shared<lsproto::RenameFilesParams>();
	params->Files = files;
	return sendRequest(t, lsproto::WorkspaceWillRenameFilesInfo,
	                   params);
}

// Emulates a file rename by sending a workspace/willRenameFiles request
// and applying the resulting edits and file renames.
// willRenameFilesWorker — fourslash.go:4965.
void FourslashTest::willRenameFilesWorker(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<lsproto::FileRename>>& files) {
	t->Helper();
	auto result = WillRenameFiles(t, files);

	if (result.WorkspaceEdit == nullptr) {
		for (auto& file : files) {
			std::string oldPath = lsproto::documentUriFileName(file->OldUri);
			std::string newPath = lsproto::documentUriFileName(file->NewUri);
			renameFileOrDirectory(t, oldPath, newPath);
		}
		return;
	}

	if (result.WorkspaceEdit->Changes != nullptr) {
		for (auto& [uri, edits] : *result.WorkspaceEdit->Changes) {
			std::string fileName = lsproto::documentUriFileName(uri);
			auto* script = getOrLoadScriptInfo(fileName);
			std::vector<TextChange> changes;
			for (auto& edit : sliceOr(edits)) {
				changes.push_back(TextChange{
				    TextRange{
				        fromLSPRange(script, edit->Range)},
				    edit->NewText});
			}
			editScriptAndUpdateMarkersWorker(t, fileName, changes);
		}
	}

	std::vector<std::shared_ptr<lsproto::RenameFile>> renameFiles;
	if (result.WorkspaceEdit->DocumentChanges != nullptr) {
		for (auto& docChange : orNilSlice(result.WorkspaceEdit->DocumentChanges)) {
			if (docChange.TextDocumentEdit != nullptr) {
				std::string fileName = lsproto::documentUriFileName(
				    docChange.TextDocumentEdit->TextDocument.Uri);
				auto* script = getOrLoadScriptInfo(fileName);
				std::vector<TextChange> changes;
				for (auto& edit :
				     sliceOr(docChange.TextDocumentEdit->Edits)) {
					auto& textEdit = edit.TextEdit;
					changes.push_back(TextChange{
					    TextRange{fromLSPRange(
					        script, textEdit->Range)},
					    textEdit->NewText});
				}
				editScriptAndUpdateMarkersWorker(t, fileName,
				                                 changes);
			} else if (docChange.RenameFile != nullptr) {
				renameFiles.push_back(docChange.RenameFile);
			}
		}
	}

	std::vector<std::shared_ptr<lsproto::FileRename>> fileRenames;
	for (auto& renameFile : renameFiles) {
		auto fr = std::make_shared<lsproto::FileRename>();
		fr->OldUri = renameFile->OldUri;
		fr->NewUri = renameFile->NewUri;
		fileRenames.push_back(fr);
	}
	willRenameFilesWorker(t, fileRenames);

	for (auto& file : files) {
		std::string oldPath = lsproto::documentUriFileName(file->OldUri);
		std::string newPath = lsproto::documentUriFileName(file->NewUri);
		renameFileOrDirectory(t, oldPath, newPath);
	}
}

// VerifyRename — fourslash.go:5029.
void FourslashTest::VerifyRename(
    gostd::testing::T* t, const std::string& markerName,
    const std::string& newName,
    const std::unordered_map<std::string, std::string>&
        expectedFileContents) {
	t->Helper();
	GoToMarker(t, markerName);
	RenameAtCaret(t, newName);
	for (auto& [fileName, expectedContent] : expectedFileContents) {
		auto* script = getScriptInfo(fileName);
		if (script == nullptr) {
			t->Fatalf(
			    "Expected script info for %s, but got nil",
			    {fileName});
		}
		assertEqual(t, script->content, expectedContent,
		            gostd::sprintf(
		                "File content after rename did not match "
		                "expected content for %s.",
		                {fileName}));
	}
}

// VerifyWillRenameFilesEdits — fourslash.go:5039.
void FourslashTest::VerifyWillRenameFilesEdits(
    gostd::testing::T* t, const std::string& oldPath,
    const std::string& newPath,
    const std::unordered_map<std::string, std::string>&
        expectedFileContents,
    const std::shared_ptr<lsutil::UserPreferences>& preferences) {
	t->Helper();
	std::function<void()> reset;
	if (preferences != nullptr) {
		reset = ConfigureWithReset(t, *preferences);
	}
	struct deferReset {
		std::function<void()>& f;
		~deferReset() {
			if (f) f();
		}
	} _reset{reset};

	auto fr = std::make_shared<lsproto::FileRename>();
	fr->OldUri = lsconv::FileNameToDocumentURI(oldPath);
	fr->NewUri = lsconv::FileNameToDocumentURI(newPath);
	willRenameFilesWorker(t, {fr});

	for (auto& [fileName, expectedContent] : expectedFileContents) {
		auto* script = getOrLoadScriptInfo(fileName);
		if (script == nullptr) {
			t->Fatalf(
			    "Expected script info for %s, but got nil",
			    {fileName});
		}
		assertEqual(
		    t, script->content, expectedContent,
		    gostd::sprintf(
		        "File content after workspace/willRenameFiles "
		        "edits did not match expected content for %s.",
		        {fileName}));
	}
}

// startsWithDirectory — tspath/path.go:1257 (StartsWithDirectory).
static bool startsWithDirectory(std::string_view fileName,
                                std::string_view directoryName,
                                bool useCaseSensitiveFileNames) {
	if (directoryName.empty()) {
		return false;
	}

	auto canonicalFileName = tspath::getCanonicalFileName(
	    fileName, useCaseSensitiveFileNames);
	auto canonicalDirectoryName = tspath::getCanonicalFileName(
	    directoryName, useCaseSensitiveFileNames);
	while (!canonicalDirectoryName.empty() &&
	       (canonicalDirectoryName.back() == '/' ||
	        canonicalDirectoryName.back() == '\\')) {
		canonicalDirectoryName.pop_back();
	}

	return gostr::hasPrefix(canonicalFileName,
	                        canonicalDirectoryName + "/") ||
	       gostr::hasPrefix(canonicalFileName,
	                        canonicalDirectoryName + "\\");
}

// getPathUpdater — fourslash.go:5058.
std::function<std::pair<std::string, bool>(const std::string&)>
FourslashTest::getPathUpdater(const std::string& oldPath,
                              const std::string& newPath) {
	return [this, oldPath, newPath](const std::string& path)
	           -> std::pair<std::string, bool> {
		tspath::ComparePathsOptions compareOptions{
		    .useCaseSensitiveFileNames =
		        vfs->UseCaseSensitiveFileNames()};
		if (tspath::comparePaths(path, oldPath, compareOptions) ==
		    0) {
			return {newPath, true};
		}
		if (startsWithDirectory(path, oldPath,
		                        vfs->UseCaseSensitiveFileNames())) {
			return {newPath + path.substr(oldPath.size()), true};
		}
		return {"", false};
	};
}

// renameFileOrDirectory — fourslash.go:5071.
void FourslashTest::renameFileOrDirectory(
    gostd::testing::T* t, const std::string& oldPath,
    const std::string& newPath) {
	t->Helper();

	auto pathUpdater = getPathUpdater(oldPath, newPath);

	// Collect all file paths that need to be renamed.
	std::unordered_set<std::string> oldFileNames;
	if (auto [_, ok] = vfs->ReadFile(oldPath); ok) {
		oldFileNames.insert(oldPath);
	} else {
		for (auto& path :
		     getAccessibleFilePaths(vfs.get(), oldPath)) {
			oldFileNames.insert(path);
		}
	}
	if (oldFileNames.empty()) {
		t->Fatalf(
		    "rename source %s did not exist in test environment",
		    {oldPath});
	}

	// !!! TODO: handle overwrites if we need to.
	// For each file: close if open, update script infos, write to VFS
	// at new path, and collect file-watch events.
	std::vector<std::shared_ptr<lsproto::FileEvent>> fileEvents;
	std::unordered_map<std::string, std::string>
	    reopenAtNewPath; // newFileName -> content, for files that were
	                     // open
	for (auto& oldFileName : oldFileNames) {
		auto [newFileName, updated] = pathUpdater(oldFileName);
		if (!updated) {
			t->Fatalf("failed to compute renamed path for %s",
			          {oldFileName});
		}

		// Send didClose for open files; get content from the old
		// script info.
		if (openFiles.find(oldFileName) != openFiles.end()) {
			auto script = scriptInfos[oldFileName];
			reopenAtNewPath[newFileName] = script->content;
			auto closeParams = std::make_shared<
			    lsproto::DidCloseTextDocumentParams>();
			closeParams->TextDocument.Uri =
			    lsconv::FileNameToDocumentURI(oldFileName);
			sendNotification(
			    t, lsproto::TextDocumentDidCloseInfo, closeParams);
			openFiles.erase(oldFileName);
		}

		scriptInfos[newFileName] =
		    newScriptInfo(newFileName,
		                  scriptInfos[oldFileName]->content);
		scriptInfos.erase(oldFileName);

		// Write renamed file to VFS.
		auto [content, updated2] = vfs->ReadFile(oldFileName);
		if (!updated2) {
			t->Fatalf(
			    "failed to read content for %s during rename to %s",
			    {oldFileName, newFileName});
		}
		if (auto err = vfs->WriteFile(newFileName, content); err) {
			t->Fatalf("failed to write renamed file %s: %v",
			          {newFileName, err.str()});
		}

		auto feOld = std::make_shared<lsproto::FileEvent>();
		feOld->Uri = lsconv::FileNameToDocumentURI(oldFileName);
		feOld->Type = lsproto::FileChangeTypeDeleted;
		auto feNew = std::make_shared<lsproto::FileEvent>();
		feNew->Uri = lsconv::FileNameToDocumentURI(newFileName);
		feNew->Type = lsproto::FileChangeTypeCreated;
		fileEvents.push_back(feOld);
		fileEvents.push_back(feNew);
	}

	// Remove the old path from VFS and notify the server of all
	// file-system changes.
	if (auto err = vfs->Remove(oldPath); err) {
		t->Fatalf("failed to remove old path %s: %v",
		          {oldPath, err.str()});
	}
	auto watchParams = std::make_shared<
	    lsproto::DidChangeWatchedFilesParams>();
	watchParams->Changes = fileEvents;
	sendNotification(t,
	                 lsproto::WorkspaceDidChangeWatchedFilesInfo,
	                 watchParams);

	// Reopen files that were previously open at their new paths.
	for (auto& [newFileName, content] : reopenAtNewPath) {
		auto item = std::make_shared<lsproto::TextDocumentItem>();
		item->Uri = lsconv::FileNameToDocumentURI(newFileName);
		item->LanguageId = getLanguageKind(newFileName);
		item->Text = content;
		auto openParams =
		    std::make_shared<lsproto::DidOpenTextDocumentParams>();
		openParams->TextDocument = item;
		sendNotification(t, lsproto::TextDocumentDidOpenInfo,
		                 openParams);
		openFiles.insert(newFileName);
	}

	// Update active filename if it was under the renamed path.
	if (auto [updatedActive, ok] = pathUpdater(activeFilename); ok) {
		activeFilename = updatedActive;
	}
}

// VerifyRenameFailed — fourslash.go:5159.
void FourslashTest::VerifyRenameFailed(
    gostd::testing::T* t,
    const std::shared_ptr<lsutil::UserPreferences>& preferences) {
	std::function<void()> reset;
	if (preferences != nullptr) {
		reset = ConfigureWithReset(t, *preferences);
	}
	struct deferReset {
		std::function<void()>& f;
		~deferReset() {
			if (f) f();
		}
	} _reset{reset};
	auto params =
	    std::make_shared<lsproto::PrepareRenameParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	params->Position = currentCaretPosition;

	std::string prefix = getCurrentPositionPrefix();
	baselineState(t);
	baselineRequestOrNotification(
	    t, lsproto::TextDocumentPrepareRenameInfo.Method, params);
	auto [resMsg, result, _ok] =
	    client->SendRequest(
	        t, lsproto::TextDocumentPrepareRenameInfo, params);
	baselineState(t);

	// prepareRename can reject via an error response (with a localized
	// message) or a null result.
	if (resMsg != nullptr && resMsg->AsResponse() != nullptr &&
	    resMsg->AsResponse()->Error != nullptr) {
		// Error response — rename was rejected with a message. This
		// is expected.
	} else if (result.Range != nullptr ||
	           result.PrepareRenamePlaceholder != nullptr ||
	           result.PrepareRenameDefaultBehavior != nullptr) {
		t->Fatalf(
		    "%sExpected rename to fail, but prepareRename "
		    "returned a result",
		    {prefix});
	}

	// Also verify that textDocument/rename does not produce usable
	// edits, since prepareRename is optional.
	auto renameParams = std::make_shared<lsproto::RenameParams>();
	renameParams->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	renameParams->Position = currentCaretPosition;
	renameParams->NewName = "RENAME_FAILED_TEST";
	auto [renameMsg, renameResult, _ok2] =
	    client->SendRequest(t, lsproto::TextDocumentRenameInfo,
	                        renameParams);
	if (renameMsg != nullptr &&
	    renameMsg->AsResponse() != nullptr &&
	    renameMsg->AsResponse()->Error != nullptr) {
		return;
	}
	if (renameResult.WorkspaceEdit != nullptr &&
	    renameResult.WorkspaceEdit->Changes != nullptr &&
	    !renameResult.WorkspaceEdit->Changes->empty()) {
		t->Fatalf(
		    "%sprepareRename returned null but textDocument/rename "
		    "returned changes",
		    {prefix});
	}
}

// VerifyBaselineRenameAtRangesWithText — fourslash.go:5199.
void FourslashTest::VerifyBaselineRenameAtRangesWithText(
    gostd::testing::T* t,
    const std::shared_ptr<lsutil::UserPreferences>& preferences,
    const std::vector<std::string>& texts) {
	std::vector<MarkerOrRange*> markerOrRanges;
	for (auto& text : texts) {
		for (auto& r : GetRangesByText()->Get(text)) {
			markerOrRanges.push_back(r.get());
		}
	}
	verifyBaselineRename(t, preferences, markerOrRanges);
}

// GetRangesByText — fourslash.go:5210.
std::shared_ptr<collections::MultiMap<
    std::string, std::shared_ptr<RangeMarker>>>
FourslashTest::GetRangesByText() {
	if (rangesByText != nullptr) {
		return rangesByText;
	}
	rangesByText = std::make_shared<collections::MultiMap<
	    std::string, std::shared_ptr<RangeMarker>>>();
	for (auto& r : testData->Ranges) {
		std::string rangeText = getRangeText(r.get());
		rangesByText->Add(rangeText, r);
	}
	return rangesByText;
}

// getRangeText — fourslash.go:5222.
std::string FourslashTest::getRangeText(const RangeMarker* r) {
	auto* script = getScriptInfo(r->FileName());
	return script->content.substr(
	    (size_t)r->Range.pos(),
	    (size_t)(r->Range.end() - r->Range.pos()));
}

// reindentJsonText — json.Marshal(v, jsontext.WithIndent("  ")) on
// pre-serialized JSON text: token-stream copy through an indenting Encoder.
std::string reindentJsonText(const std::string& raw) {
	json::Decoder dec(raw);
	std::ostringstream buf;
	json::Options opts;
	json::withIndent("  ")(opts);
	json::Encoder enc(buf, opts);
	while (!dec.atEnd()) {
		auto tok = dec.readToken();
		if (!tok.second.empty() ||
		    enc.writeToken(tok.first).size() != 0) {
			break;
		}
	}
	return buf.str();
}

// verifyBaselines — fourslash.go:5227.
void FourslashTest::verifyBaselines(gostd::testing::T* t,
                                    const std::string& testPath) {
	if (!testData->isStateBaseliningEnabled()) {
		for (auto& [command, content] : baselines) {
			testutil::baseline::Run(
			    t, getBaselineFileName(t, command),
			    content->String(),
			    getBaselineOptions(command, testPath));
		}
	} else {
		testutil::baseline::Run(
		    t, getBaseFileNameFromTest(t) + ".baseline",
		    stateBaseline_->baseline.String(),
		    testutil::baseline::Options{.Subfolder =
		                                    "fourslash/state"});
	}
}


// ===========================================================================
// fourslash.go:5243-6045 — inlay hints, linked editing, diagnostics,
// document symbols, error verifications, helpers
// ===========================================================================

// VerifyBaselineInlayHints — fourslash.go:5238.
void FourslashTest::VerifyBaselineInlayHints(
    gostd::testing::T* t,
    const std::shared_ptr<lsproto::Range>& span,
    const std::shared_ptr<lsutil::UserPreferences>& testPreferences) {
	std::string fileName = activeFilename;
	lsproto::Range lspRange;
	if (span == nullptr) {
		lspRange = converters->ToLSPRange(
		    getScriptInfo(fileName),
		    TextRange{
		        (TextPos)0,
		        (TextPos)scriptInfos[fileName]->content
		            .size()}).first;
	} else {
		lspRange = *span;
	}

	auto params = std::make_shared<lsproto::InlayHintParams>();
	params->TextDocument.Uri = lsconv::FileNameToDocumentURI(fileName);
	params->Range = lspRange;

	auto preferences = testPreferences;
	if (preferences == nullptr) {
		preferences = std::make_shared<lsutil::UserPreferences>(
		    lsutil::NewDefaultUserPreferences());
	}
	auto reset = ConfigureWithReset(t, *preferences);
	struct deferReset {
		std::function<void()>& f;
		~deferReset() { f(); }
	} _reset{reset};

	std::string prefix = gostd::sprintf(
	    "At position (Ln %d, Col %d): ",
	    {(int)lspRange.Start.Line, (int)lspRange.Start.Character});
	auto result = sendRequest(t, lsproto::TextDocumentInlayHintInfo,
	                          params);
	auto fileLines =
	    gostr::split(getScriptInfo(fileName)->content, "\n");
	std::vector<std::string> annotations;
	if (result.InlayHints != nullptr) {
		auto& hints = *result.InlayHints;
		std::stable_sort(hints->begin(), hints->end(),
		    [](const std::shared_ptr<lsproto::InlayHint>& a,
		       const std::shared_ptr<lsproto::InlayHint>& b) {
			    return lsproto::ComparePositions(a->Position,
			                                   b->Position) < 0;
		    });
		for (auto& hint : orNilSlice(result.InlayHints)) {
			if (hint->Label.InlayHintLabelParts != nullptr) {
				for (auto& part :
				     orNilSlice(hint->Label.InlayHintLabelParts)) {
					// Avoid diffs caused by lib file
					// updates.
					if (part->Location != nullptr &&
					    isLibFile(lsproto::documentUriFileName(
					        part->Location->Uri))) {
						part->Location->Range.Start =
						    lsproto::Position{0, 0};
						part->Location->Range.End =
						    lsproto::Position{0, 0};
					}
				}
			}
			std::string underline =
			    gostr::repeat(" ", (int)hint->Position.Character) +
			    "^";
			auto [hintJson, err] =
			    stringifyJson(*hint, "", "  ");
			if (!err.empty()) {
				t->Fatalf(
				    prefix +
				        "Failed to stringify inlay hint for "
				        "baseline: %v",
				    {err});
			}
			std::string annotation =
			    fileLines[hint->Position.Line];
			annotation += "\n" + underline + "\n" + hintJson;
			annotations.push_back(annotation);
		}
	}

	if (annotations.empty()) {
		annotations.push_back("=== No inlay hints ===");
	}

	addResultToBaseline(t, inlayHintsCmd,
	                    gostr::join(annotations, "\n\n"));
}

// VerifyBaselineLinkedEditing — fourslash.go:5295.
void FourslashTest::VerifyBaselineLinkedEditing(gostd::testing::T* t) {
	gostr::Builder baselineBuilder;
	int offset = 0;

	// write to baseline in order of file appearance in test data
	for (auto& file : testData->Files) {
		baselineBuilder.WriteString("// === Linked Editing ===\n");
		baselineBuilder.WriteString(gostd::sprintf(
		    "=== %s ===\n", {file->FileName()}));
		std::vector<std::shared_ptr<lsproto::LinkedEditingRanges>>
		    results;
		std::unordered_map<lsproto::Range, bool, lspRangeHash> found;

		// request linkedEditing at every position in the file
		for (int i = 0; i < (int)file->Content.size(); i++) {
			auto params = std::make_shared<
			    lsproto::LinkedEditingRangeParams>();
			params->TextDocument.Uri =
			    lsconv::FileNameToDocumentURI(file->FileName());
			params->Position =
			    converters->PositionToLineAndCharacter(
			        getScriptInfo(file->FileName()),
			        (TextPos)i);
			auto result = sendRequest(
			    t,
			    lsproto::TextDocumentLinkedEditingRangeInfo,
			    params);
			if (result.LinkedEditingRanges != nullptr &&
			    result.LinkedEditingRanges->Ranges.has_value() &&
			    !result.LinkedEditingRanges->Ranges->empty() &&
			    !found[(*result.LinkedEditingRanges->Ranges)[0]]) {
				results.push_back(result.LinkedEditingRanges);
				found[(*result.LinkedEditingRanges->Ranges)[0]] =
				    true;
			}
		}

		if (results.empty()) {
			baselineBuilder.WriteString(gostd::sprintf(
			    "%s\n\n--No linked edits found--\n\n\n",
			    {file->Content}));
			continue;
		}

		// sort entries in each file
		std::stable_sort(
		    results.begin(), results.end(),
		    [](const std::shared_ptr<lsproto::LinkedEditingRanges>& a,
		       const std::shared_ptr<lsproto::LinkedEditingRanges>&
		           b) {
			    return lsproto::ComparePositions(
			               (*a->Ranges)[0].Start,
			               (*b->Ranges)[0].Start) < 0;
		    });
		std::vector<baselineDetail> baselineDetails;
		gostr::Builder foundEditInfoBuilder;
		for (auto& edit : results) {
			baselineDetails.push_back(baselineDetail{
			    .pos = (*edit->Ranges)[0].Start,
			    .positionMarker = gostd::sprintf("[|/*%d*/",
			                                     {offset}),
			});
			baselineDetails.push_back(baselineDetail{
			    .pos = (*edit->Ranges)[0].End,
			    .positionMarker = "|]",
			});
			baselineDetails.push_back(baselineDetail{
			    .pos = (*edit->Ranges)[1].Start,
			    .positionMarker = gostd::sprintf("[|/*%d*/",
			                                     {offset}),
			});
			baselineDetails.push_back(baselineDetail{
			    .pos = (*edit->Ranges)[1].End,
			    .positionMarker = "|]",
			});

			auto [editJson, jerr] = stringifyJson(*edit, "", "  ");
			if (!jerr.empty()) {
				t->Fatalf("Failed to stringify linked editing "
				          "ranges: %v",
				          {jerr});
			}
			foundEditInfoBuilder.WriteString(gostd::sprintf(
			    "\n\n=== %d ===\n%s", {offset, editJson}));
			offset++;
		}

		// sort baselineDetails by position
		std::stable_sort(
		    baselineDetails.begin(), baselineDetails.end(),
		    [](const baselineDetail& a, const baselineDetail& b) {
			    return lsproto::ComparePositions(a.pos, b.pos) < 0;
		    });

		// write file content with inline annotations for linked edits
		int lastPosition = 0;
		for (auto& detail : baselineDetails) {
			auto currentPosition =
			    converters->LineAndCharacterToPosition(
			        getScriptInfo(file->FileName()), detail.pos);
			baselineBuilder.WriteString(file->Content.substr(
			    (size_t)lastPosition,
			    (size_t)(currentPosition - lastPosition)));
			baselineBuilder.WriteString(detail.positionMarker);
			lastPosition = (int)currentPosition;
		}
		baselineBuilder.WriteString(
		    file->Content.substr((size_t)lastPosition));
		baselineBuilder.WriteString(foundEditInfoBuilder.String() +
		                            "\n\n\n");
	}

	writeToBaseline(linkedEditingCmd, baselineBuilder.String());
}

// VerifyLinkedEditing — fourslash.go:5392.
void FourslashTest::VerifyLinkedEditing(
    gostd::testing::T* t,
    const std::unordered_map<std::string,
                             std::vector<lsproto::Range>>&
        markerNamesToExpected) {
	for (auto& [markerName, expectedRanges] : markerNamesToExpected) {
		GoToMarker(t, markerName);
		auto params = std::make_shared<
		    lsproto::LinkedEditingRangeParams>();
		params->TextDocument.Uri =
		    lsconv::FileNameToDocumentURI(activeFilename);
		params->Position = currentCaretPosition;
		auto result = sendRequest(
		    t, lsproto::TextDocumentLinkedEditingRangeInfo, params);
		auto actualRanges = result.LinkedEditingRanges;
		if (expectedRanges.empty()) {
			if (actualRanges != nullptr &&
			    actualRanges->Ranges.has_value() &&
			    !actualRanges->Ranges->empty()) {
				t->Fatalf(
				    "Expected no linked editing ranges for "
				    "marker '%s', but found %v",
				    {markerName});
			}
			continue;
		} else {
			if (actualRanges == nullptr ||
			    !actualRanges->Ranges.has_value() ||
			    actualRanges->Ranges->empty()) {
				t->Fatalf(
				    "Expected linked editing ranges for "
				    "marker '%s', but found none",
				    {markerName});
			}

			assertDeepEqual(
			    t, (*actualRanges->Ranges)[0],
			    expectedRanges[0],
			    gostd::sprintf(
			        "Linked editing ranges for opening element do "
			        "not match expected for marker '%s'",
			        {markerName}));
			assertDeepEqual(
			    t, (*actualRanges->Ranges)[1],
			    expectedRanges[1],
			    gostd::sprintf(
			        "Linked editing ranges for closing element do "
			        "not match expected for marker '%s'",
			        {markerName}));
		}
	}
}

// VerifyDiagnostics — fourslash.go:5413.
void FourslashTest::VerifyDiagnostics(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<lsproto::Diagnostic>>& expected) {
	verifyDiagnostics(
	    t, expected,
	    [](const std::shared_ptr<lsproto::Diagnostic>&) {
		    return true;
	    });
}

// Similar to `VerifyDiagnostics`, but excludes suggestion diagnostics
// returned from server. fourslash.go:5418.
void FourslashTest::VerifyNonSuggestionDiagnostics(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<lsproto::Diagnostic>>& expected) {
	verifyDiagnostics(
	    t, expected,
	    [](const std::shared_ptr<lsproto::Diagnostic>& d) {
		    return !isSuggestionDiagnostic(d);
	    });
}

// Similar to `VerifyDiagnostics`, but includes only suggestion
// diagnostics returned from server. fourslash.go:5423.
void FourslashTest::VerifySuggestionDiagnostics(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<lsproto::Diagnostic>>& expected) {
	verifyDiagnostics(
	    t, expected,
	    [](const std::shared_ptr<lsproto::Diagnostic>& d) {
		    return isSuggestionDiagnostic(d);
	    });
}

// verifyDiagnostics — fourslash.go:5427.
void FourslashTest::verifyDiagnostics(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<lsproto::Diagnostic>>& expected,
    const std::function<
        bool(const std::shared_ptr<lsproto::Diagnostic>&)>&
        filterDiagnostics) {
	auto actualDiagnostics = getDiagnostics(t, activeFilename);
	std::vector<std::shared_ptr<lsproto::Diagnostic>> filtered;
	for (auto& d : actualDiagnostics) {
		if (filterDiagnostics(d)) filtered.push_back(d);
	}
	actualDiagnostics = std::move(filtered);
	lsproto::Range emptyRange{};
	std::vector<std::shared_ptr<lsproto::Diagnostic>>
	    expectedWithRanges(expected.size());
	for (size_t i = 0; i < expected.size(); i++) {
		auto& diag = expected[i];
		if (diag->Range == emptyRange) {
			auto rangesInFile = getRangesInFile(activeFilename);
			if (rangesInFile.empty()) {
				t->Fatalf(
				    "No ranges found in file %s to assign to "
				    "diagnostic with empty range",
				    {activeFilename});
			}
			auto diagWithRange =
			    std::make_shared<lsproto::Diagnostic>(*diag);
			diagWithRange->Range = rangesInFile[0]->LSRange;
			expectedWithRanges[i] = diagWithRange;
		} else {
			expectedWithRanges[i] = diag;
		}
	}
	if (actualDiagnostics.empty() && expectedWithRanges.empty()) {
		return;
	}
	assertDeepEqual(t, actualDiagnostics, expectedWithRanges,
	                "Diagnostics do not match expected",
	                {diagnosticsIgnoreOpts});
}

// getDiagnostics — fourslash.go:5449.
std::vector<std::shared_ptr<lsproto::Diagnostic>>
FourslashTest::getDiagnostics(gostd::testing::T* t,
                              const std::string& fileName) {
	auto params = std::make_shared<
	    lsproto::DocumentDiagnosticParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(fileName);
	auto result =
	    sendRequest(t, lsproto::TextDocumentDiagnosticInfo, params);
	if (result.FullDocumentDiagnosticReport != nullptr) {
		if (result.FullDocumentDiagnosticReport->Items
		        .has_value()) {
			return *result.FullDocumentDiagnosticReport->Items;
		}
	}
	return {};
}

// isSuggestionDiagnostic — fourslash.go:5481.
bool isSuggestionDiagnostic(
    const std::shared_ptr<lsproto::Diagnostic>& diag) {
	return diag->Severity != nullptr &&
	       *diag->Severity == lsproto::DiagnosticSeverityHint;
}

// VerifyBaselineNonSuggestionDiagnostics — fourslash.go:5485.
void FourslashTest::VerifyBaselineNonSuggestionDiagnostics(
    gostd::testing::T* t) {
	std::vector<std::shared_ptr<fourslashDiagnostic>> diagnostics;
	std::vector<
	    std::unique_ptr<testutil::harnessutil::TestFile>>
	    ownedFiles;
	std::vector<testutil::harnessutil::TestFile*> files;
	for (auto& [fileName, scriptInfo] : scriptInfos) {
		if (tspath::hasJSONFileExtension(fileName)) {
			continue;
		}
		ownedFiles.push_back(
		    std::make_unique<testutil::harnessutil::TestFile>(
		        testutil::harnessutil::TestFile{
		            .UnitName = fileName,
		            .Content = scriptInfo->content,
		        }));
		files.push_back(ownedFiles.back().get());
		auto lspDiagnostics = getDiagnostics(t, fileName);
		std::vector<std::shared_ptr<lsproto::Diagnostic>>
		    nonSuggestions;
		for (auto& d : lspDiagnostics) {
			if (!isSuggestionDiagnostic(d)) {
				nonSuggestions.push_back(d);
			}
		}
		for (auto& d : nonSuggestions) {
			diagnostics.push_back(
			    toDiagnostic(scriptInfo.get(), d));
		}
	}
	std::stable_sort(
	    files.begin(), files.end(),
	    [](const testutil::harnessutil::TestFile* a,
	       const testutil::harnessutil::TestFile* b) {
		    return a->UnitName.compare(b->UnitName) < 0;
	    });
	// Flatten to Diagnostic* for the tsbaseline generic port.
	std::vector<diagnosticwriter::Diagnostic*> diagPtrs;
	diagPtrs.reserve(diagnostics.size());
	for (auto& d : diagnostics) diagPtrs.push_back(d.get());
	auto result = testutil::tsbaseline::GetErrorBaseline(
	    t, files, diagPtrs,
	    [](diagnosticwriter::Diagnostic* a,
	       diagnosticwriter::Diagnostic* b) {
		    return compareDiagnostics(
		        static_cast<fourslashDiagnostic*>(a),
		        static_cast<fourslashDiagnostic*>(b));
	    },
	    false /*pretty*/);
	addResultToBaseline(t, nonSuggestionDiagnosticsCmd, result);
}

// fourslashDiagnosticFile::ecmaLineMap — fourslash.go:5540.
const std::vector<TextPos>& fourslashDiagnosticFile::ecmaLineMap()
    const {
	if (ecmaLineMap_.empty()) {
		const_cast<fourslashDiagnosticFile*>(this)
		    ->ecmaLineMap_ =
		        tsc::computeECMALineStarts(file_->Content);
	}
	return ecmaLineMap_;
}

// fourslashDiagnostic — diagnosticwriter::Diagnostic impl
// (fourslash.go:5552).
const diagnosticwriter::FileLike* fourslashDiagnostic::file() {
	return file_.get();
}
int fourslashDiagnostic::pos() { return loc.pos(); }
int fourslashDiagnostic::end() { return loc.end(); }
int fourslashDiagnostic::len() { return loc.len(); }
int32_t fourslashDiagnostic::code() { return code_; }
tsc::DiagnosticCategory fourslashDiagnostic::category() {
	return category_;
}
std::string_view fourslashDiagnostic::source() { return ""; }
std::string fourslashDiagnostic::localize(
    const locale::Locale& locale) {
	return message;
}
std::vector<diagnosticwriter::Diagnostic*>
fourslashDiagnostic::messageChain() {
	return {};
}
std::vector<diagnosticwriter::Diagnostic*>
fourslashDiagnostic::relatedInformation() {
	std::vector<diagnosticwriter::Diagnostic*> relatedInfo;
	relatedInfo.reserve(relatedDiagnostics.size());
	for (auto& relDiag : relatedDiagnostics) {
		relatedInfo.push_back(relDiag.get());
	}
	return relatedInfo;
}

// toDiagnostic — fourslash.go:5590.
std::shared_ptr<fourslashDiagnostic> FourslashTest::toDiagnostic(
    scriptInfo* scriptInfo,
    const std::shared_ptr<lsproto::Diagnostic>& lspDiagnostic) {
	tsc::DiagnosticCategory category;
	if (lspDiagnostic->Severity != nullptr) {
		switch (*lspDiagnostic->Severity) {
		case lsproto::DiagnosticSeverityError:
			category = tsc::DiagnosticCategory::Error;
			break;
		case lsproto::DiagnosticSeverityWarning:
			category = tsc::DiagnosticCategory::Warning;
			break;
		case lsproto::DiagnosticSeverityInformation:
			category = tsc::DiagnosticCategory::Message;
			break;
		case lsproto::DiagnosticSeverityHint:
			category = tsc::DiagnosticCategory::Suggestion;
			break;
		default:
			category = tsc::DiagnosticCategory::Error;
		}
	} else {
		category = tsc::DiagnosticCategory::Error;
	}
	int32_t code = 0;
	if (lspDiagnostic->Code != nullptr &&
	    lspDiagnostic->Code->Integer != nullptr) {
		code = *lspDiagnostic->Code->Integer;
	}

	std::vector<std::shared_ptr<fourslashDiagnostic>>
	    relatedDiagnostics;
	if (lspDiagnostic->RelatedInformation != nullptr) {
		for (auto& info : orNilSlice(lspDiagnostic->RelatedInformation)) {
			auto* relatedScriptInfo =
			    getScriptInfo(lsproto::documentUriFileName(
			        info->Location.Uri));
			if (relatedScriptInfo == nullptr) {
				continue;
			}
			auto relatedDiagnostic =
			    std::make_shared<fourslashDiagnostic>();
			relatedDiagnostic->file_ =
			    std::make_shared<fourslashDiagnosticFile>();
			relatedDiagnostic->file_->file_ =
			    std::make_shared<testutil::harnessutil::
			                         TestFile>(testutil::harnessutil::
			                                       TestFile{
			                .UnitName = relatedScriptInfo->fileName,
			                .Content = relatedScriptInfo->content,
			            });
			relatedDiagnostic->loc = fromLSPRange(
			    relatedScriptInfo, info->Location.Range);
			relatedDiagnostic->code_ = code;
			relatedDiagnostic->category_ = category;
			relatedDiagnostic->message = info->Message;
			relatedDiagnostics.push_back(relatedDiagnostic);
		}
	}

	auto diagnostic = std::make_shared<fourslashDiagnostic>();
	diagnostic->file_ = std::make_shared<fourslashDiagnosticFile>();
	diagnostic->file_->file_ =
	    std::make_shared<testutil::harnessutil::TestFile>(
	        testutil::harnessutil::TestFile{
	            .UnitName = scriptInfo->fileName,
	            .Content = scriptInfo->content,
	        });
	diagnostic->loc = fromLSPRange(scriptInfo, lspDiagnostic->Range);
	diagnostic->code_ = code;
	diagnostic->category_ = category;
	diagnostic->message = lspDiagnostic->Message.AsString();
	diagnostic->relatedDiagnostics = relatedDiagnostics;
	return diagnostic;
}

// compareDiagnostics — fourslash.go:5637.
int compareDiagnostics(fourslashDiagnostic* d1,
                       fourslashDiagnostic* d2) {
	int c = d1->file_->FileName().compare(d2->file_->FileName());
	if (c != 0) {
		return c;
	}
	c = d1->pos() - d2->pos();
	if (c != 0) {
		return c;
	}
	c = d1->end() - d2->end();
	if (c != 0) {
		return c;
	}
	c = (int)d1->code_ - (int)d2->code_;
	if (c != 0) {
		return c;
	}
	c = d1->message.compare(d2->message);
	if (c != 0) {
		return c;
	}
	return compareRelatedDiagnostics(d1->relatedDiagnostics,
	                                 d2->relatedDiagnostics);
}

// compareRelatedDiagnostics — fourslash.go:5655.
int compareRelatedDiagnostics(
    const std::vector<std::shared_ptr<fourslashDiagnostic>>& d1,
    const std::vector<std::shared_ptr<fourslashDiagnostic>>& d2) {
	int c = (int)d2.size() - (int)d1.size();
	if (c != 0) {
		return c;
	}
	for (size_t i = 0; i < d1.size(); i++) {
		c = compareDiagnostics(d1[i].get(), d2[i].get());
		if (c != 0) {
			return c;
		}
	}
	return 0;
}

// isLibFile — fourslash.go:5681.
bool isLibFile(const std::string& fileName) {
	std::string baseName = std::string(tspath::getBaseFileName(fileName));
	if (gostr::hasPrefix(baseName, "lib.") &&
	    gostr::hasSuffix(baseName, ".d.ts")) {
		return true;
	}
	return false;
}

// AnyTextEdits / NoTextEdits — fourslash.go:5690.
const std::shared_ptr<lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>
    AnyTextEdits = std::make_shared<
        lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>(
        std::vector<std::shared_ptr<lsproto::TextEdit>>{});
const std::shared_ptr<lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>
    NoTextEdits = std::make_shared<
        lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>(
        std::vector<std::shared_ptr<lsproto::TextEdit>>{});

// VerifyBaselineGoToImplementation — fourslash.go:5695.
void FourslashTest::VerifyBaselineGoToImplementation(
    gostd::testing::T* t, const std::vector<std::string>& markerNames) {
	verifyBaselineDefinitions(
	    t, goToImplementationCmd, "/*GOTO IMPL*/", /*definitionMarker*/
	    [](gostd::testing::T* t, FourslashTest* f,
	       const std::string& fileName,
	       lsproto::Position position)
	        -> lsproto::LocationOrLocationsOrDefinitionLinksOrNull {
		    auto params =
		        std::make_shared<lsproto::ImplementationParams>();
		    params->TextDocument.Uri =
		        lsconv::FileNameToDocumentURI(f->activeFilename);
		    params->Position = f->currentCaretPosition;

		    return f->sendRequest(
		        t, lsproto::TextDocumentImplementationInfo, params);
	    },
	    false, /*includeOriginalSelectionRange*/
	    markerNames);
}

// `verify.navigateTo` in Strada. VerifyWorkspaceSymbol —
// fourslash.go:5721.
void FourslashTest::VerifyWorkspaceSymbol(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<VerifyWorkspaceSymbolCase>>&
        cases) {
	auto originalPreferences = userPreferences;
	for (auto& testCase : cases) {
		auto preferences = testCase->Preferences;
		if (preferences == nullptr) {
			preferences =
			    std::make_shared<lsutil::UserPreferences>(
			        lsutil::NewDefaultUserPreferences());
		}
		Configure(t, *preferences);
		auto params = std::make_shared<
		    lsproto::WorkspaceSymbolParams>();
		params->Query = testCase->Pattern;
		params->TextDocument =
		    std::make_shared<lsproto::TextDocumentIdentifier>();
		params->TextDocument->Uri =
		    lsconv::FileNameToDocumentURI(activeFilename);
		auto result = sendRequest(t, lsproto::WorkspaceSymbolInfo,
		                          params);
		if (result.SymbolInformations == nullptr) {
			t->Fatalf(
			    "Expected non-nil symbol information array from "
			    "workspace symbol request", {});
		}
		if (testCase->Includes != nullptr) {
			if (testCase->Exact != nullptr) {
				t->Fatalf(
				    "Test case cannot have both 'Includes' "
				    "and 'Exact' fields set", {});
			}
			verifyIncludesSymbols(
			    t, orNilSlice(result.SymbolInformations),
			    *testCase->Includes,
			    "Workspace symbols mismatch with pattern '" +
			        testCase->Pattern + "'");
		} else {
			if (testCase->Exact == nullptr) {
				t->Fatalf(
				    "Test case must have either 'Includes' "
				    "or 'Exact' field set", {});
			}
			verifyExactSymbols(
			    t, orNilSlice(result.SymbolInformations),
			    *testCase->Exact,
			    "Workspace symbols mismatch with pattern '" +
			        testCase->Pattern + "'");
		}
	}
	Configure(t, originalPreferences);
}

// verifyExactSymbols — fourslash.go:5750.
void verifyExactSymbols(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<lsproto::SymbolInformation>>&
        actual,
    const std::vector<std::shared_ptr<lsproto::SymbolInformation>>&
        expected,
    const std::string& prefix) {
	if (actual.size() != expected.size()) {
		t->Fatalf(
		    "%s: Expected %d symbols, but got %d:\n%s", {prefix,
		    (int)expected.size(), (int)actual.size(),
		    ""});
	}
	for (size_t i = 0; i < actual.size(); i++) {
		assertDeepEqual(t, actual[i], expected[i], prefix);
	}
}

// verifyIncludesSymbols — fourslash.go:5764.
void verifyIncludesSymbols(
    gostd::testing::T* t,
    const std::vector<std::shared_ptr<lsproto::SymbolInformation>>&
        actual,
    const std::vector<std::shared_ptr<lsproto::SymbolInformation>>&
        includes,
    const std::string& prefix) {
	struct key {
		std::string name;
		lsproto::Location loc;
		bool operator==(const key& o) const {
			return name == o.name && loc.Uri == o.loc.Uri &&
			       loc.Range == o.loc.Range;
		}
	};
	struct keyHash {
		size_t operator()(const key& k) const {
			return std::hash<std::string>{}(k.name);
		}
	};
	std::unordered_map<
	    key, std::shared_ptr<lsproto::SymbolInformation>, keyHash>
	    nameAndLocToActualSymbol;
	for (auto& sym : actual) {
		nameAndLocToActualSymbol[key{sym->Name, sym->Location}] =
		    sym;
	}

	for (auto& sym : includes) {
		auto it =
		    nameAndLocToActualSymbol.find(key{sym->Name,
		                                      sym->Location});
		if (it == nameAndLocToActualSymbol.end()) {
			t->Fatalf(
			    "%s: Expected symbol '%s' at location not found",
			    {prefix, sym->Name});
		}
		assertDeepEqual(
		    t, it->second, sym,
		    gostd::sprintf(
		        "%s: Symbol '%s' at location mismatch",
		        {prefix, sym->Name}));
	}
}

// VerifyBaselineDocumentSymbol — fourslash.go:5784.
void FourslashTest::VerifyBaselineDocumentSymbol(
    gostd::testing::T* t) {
	auto params =
	    std::make_shared<lsproto::DocumentSymbolParams>();
	params->TextDocument.Uri =
	    lsconv::FileNameToDocumentURI(activeFilename);
	auto result = sendRequest(
	    t, lsproto::TextDocumentDocumentSymbolInfo, params);
	auto uri = lsconv::FileNameToDocumentURI(activeFilename);
	std::unordered_map<documentSpanKey,
	                   std::shared_ptr<lsproto::DocumentSymbol>,
	                   documentSpanKeyHash>
	    symbolBySpan;
	if (result.DocumentSymbols != nullptr) {
		for (auto& symbol : orNilSlice(result.DocumentSymbols)) {
			collectDocumentSymbolSpans(uri, symbol, symbolBySpan);
		}
	}
	std::vector<documentSpan> spans;
	for (auto& [key, symbol] : symbolBySpan) {
		spans.push_back(documentSpan{
		    .uri = key.uri,
		    .textSpan = key.textSpan,
		    .contextSpan = std::make_shared<lsproto::Range>(
		        symbol->Range)});
	}
	addResultToBaseline(
	    t, documentSymbolsCmd,
	    getBaselineForSpansWithFileContents(
	        spans, baselineFourslashLocationsOptions{
	            .getLocationData =
	                [&symbolBySpan](const documentSpan& span)
	                -> std::string {
		            auto it = symbolBySpan.find(documentSpanKey{
		                span.uri, span.textSpan,
		                span.contextSpan != nullptr
		                    ? *span.contextSpan
		                    : lsproto::Range{}});
		            auto& symbol = it->second;
		            return gostd::sprintf(
		                "{| name: %s, kind: %s |}",
		                {symbol->Name,
		                 lsproto::String(symbol->Kind)});
		        },
	        }));

	gostr::Builder detailsBuilder;
	if (result.DocumentSymbols != nullptr) {
		writeDocumentSymbolDetails(orNilSlice(result.DocumentSymbols), 0,
		                           &detailsBuilder);
	}
	writeToBaseline(documentSymbolsCmd,
	                "\n\n// === Details ===\n" +
	                    detailsBuilder.String());
}

// writeDocumentSymbolDetails — fourslash.go:5821.
void writeDocumentSymbolDetails(
    const std::vector<std::shared_ptr<lsproto::DocumentSymbol>>&
        symbols,
    int indent, gostr::Builder* builder) {
	for (auto& symbol : symbols) {
		builder->WriteString(gostd::sprintf(
		    "%s(%s) %s\n",
		    {gostr::repeat("  ", indent),
		     lsproto::String(symbol->Kind), symbol->Name}));
		if (symbol->Children != nullptr) {
			writeDocumentSymbolDetails(orNilSlice(symbol->Children),
			                           indent + 1, builder);
		}
	}
}

// collectDocumentSymbolSpans — fourslash.go:5830.
// Deduplicate by value rather than by the documentSpan key, which holds
// a pointer to the symbol's Range. The same logical symbol can be
// reached more than once (e.g. a merged declaration), and depending on
// transport those occurrences may be the same object (shared *Range) or
// independent copies (distinct *Range after a JSON round-trip). A
// value-based key collapses them consistently.
void collectDocumentSymbolSpans(
    const lsproto::DocumentUri& uri,
    const std::shared_ptr<lsproto::DocumentSymbol>& symbol,
    std::unordered_map<documentSpanKey,
                       std::shared_ptr<lsproto::DocumentSymbol>,
                       documentSpanKeyHash>& symbolBySpan) {
	documentSpanKey key{uri, symbol->SelectionRange, symbol->Range};
	if (symbolBySpan.find(key) == symbolBySpan.end()) {
		symbolBySpan[key] = symbol;
	}
	if (symbol->Children != nullptr) {
		for (auto& child : orNilSlice(symbol->Children)) {
			collectDocumentSymbolSpans(uri, child, symbolBySpan);
		}
	}
}

// VerifyNumberOfErrorsInCurrentFile verifies that the current file has
// the expected number of errors. fourslash.go:5869.
void FourslashTest::VerifyNumberOfErrorsInCurrentFile(
    gostd::testing::T* t, int expectedCount) {
	auto diagnostics = getDiagnostics(t, activeFilename);
	// Filter to only include errors (not suggestions/hints)
	std::vector<std::shared_ptr<lsproto::Diagnostic>> errors;
	for (auto& d : diagnostics) {
		if (!isSuggestionDiagnostic(d)) {
			errors.push_back(d);
		}
	}
	if ((int)errors.size() != expectedCount) {
		t->Fatalf(
		    "Expected %d errors in current file, but got %d",
		    {expectedCount, (int)errors.size()});
	}
}

// VerifyNoErrors verifies that no errors exist in any open files.
// fourslash.go:5881.
void FourslashTest::VerifyNoErrors(gostd::testing::T* t) {
	for (auto& fileName : openFiles) {
		auto diagnostics = getDiagnostics(t, fileName);
		// Filter to only include errors (not suggestions/hints)
		std::vector<std::shared_ptr<lsproto::Diagnostic>> errors;
		for (auto& d : diagnostics) {
			if (!isSuggestionDiagnostic(d)) {
				errors.push_back(d);
			}
		}
		if (!errors.empty()) {
			std::vector<std::string> messages;
			for (auto& err : errors) {
				messages.push_back(err->Message.AsString());
			}
			t->Fatalf(
			    "Expected no errors but found %d in %s: %v",
			    {(int)errors.size(), fileName,
			     gostr::join(messages, ", ")});
		}
	}
}

// VerifyErrorExistsAtRange verifies that an error with the given code
// exists at the given range. fourslash.go:5898.
void FourslashTest::VerifyErrorExistsAtRange(
    gostd::testing::T* t,
    const std::shared_ptr<RangeMarker>& rangeMarker, int code,
    const std::string& message) {
	auto diagnostics = getDiagnostics(t, rangeMarker->FileName());
	for (auto& diag : diagnostics) {
		if (diag->Code != nullptr &&
		    diag->Code->Integer != nullptr &&
		    (int)*diag->Code->Integer == code) {
			// Check if the range matches
			if (diag->Range.Start.Line ==
			        rangeMarker->LSRange.Start.Line &&
			    diag->Range.Start.Character ==
			        rangeMarker->LSRange.Start.Character &&
			    diag->Range.End.Line ==
			        rangeMarker->LSRange.End.Line &&
			    diag->Range.End.Character ==
			        rangeMarker->LSRange.End.Character) {
				// If message is provided, verify it matches
				if (!message.empty() &&
				    diag->Message.AsString() != message) {
					t->Fatalf(
					    "Error at range has code %d but "
					    "message mismatch. Expected: %q, "
					    "Got: %q",
					    {code, message,
					     diag->Message.AsString()});
				}
				return;
			}
		}
	}
	t->Fatalf(
	    "Expected error with code %d at range but it was not found",
	    {code});
}

// VerifyErrorExistsBetweenMarkers verifies that an error exists between
// the two markers. fourslash.go:5918.
void FourslashTest::VerifyErrorExistsBetweenMarkers(
    gostd::testing::T* t, const std::string& startMarkerName,
    const std::string& endMarkerName) {
	auto startIt = testData->MarkerPositions.find(startMarkerName);
	if (startIt == testData->MarkerPositions.end()) {
		t->Fatalf("Start marker '%s' not found", {startMarkerName});
	}
	auto startMarker = startIt->second;
	auto endIt = testData->MarkerPositions.find(endMarkerName);
	if (endIt == testData->MarkerPositions.end()) {
		t->Fatalf("End marker '%s' not found", {endMarkerName});
	}
	auto endMarker = endIt->second;
	if (startMarker->FileName() != endMarker->FileName()) {
		t->Fatalf("Markers '%s' and '%s' are in different files",
		          {startMarkerName, endMarkerName});
	}

	auto diagnostics = getDiagnostics(t, startMarker->FileName());
	int startPos = startMarker->Position;
	int endPos = endMarker->Position;

	for (auto& diag : diagnostics) {
		if (!isSuggestionDiagnostic(diag)) {
			int diagStart =
			    (int)converters->LineAndCharacterToPosition(
			        getScriptInfo(startMarker->FileName()),
			        diag->Range.Start);
			int diagEnd =
			    (int)converters->LineAndCharacterToPosition(
			        getScriptInfo(startMarker->FileName()),
			        diag->Range.End);
			if (diagStart >= startPos && diagEnd <= endPos) {
				return; // Found an error in the range
			}
		}
	}
	t->Fatalf(
	    "Expected error between markers '%s' and '%s' but none was "
	    "found",
	    {startMarkerName, endMarkerName});
}

// VerifyErrorExistsAfterMarker verifies that an error exists after the
// given marker. fourslash.go:5946.
void FourslashTest::VerifyErrorExistsAfterMarker(
    gostd::testing::T* t, const std::string& markerName) {
	std::string fileName;
	int markerPos;

	if (markerName.empty()) {
		// Use current position
		fileName = activeFilename;
		markerPos =
		    (int)converters->LineAndCharacterToPosition(
		        getScriptInfo(activeFilename),
		        currentCaretPosition);
	} else {
		auto it = testData->MarkerPositions.find(markerName);
		if (it == testData->MarkerPositions.end()) {
			t->Fatalf("Marker '%s' not found", {markerName});
		}
		fileName = it->second->FileName();
		markerPos = it->second->Position;
	}

	auto diagnostics = getDiagnostics(t, fileName);

	for (auto& diag : diagnostics) {
		if (!isSuggestionDiagnostic(diag)) {
			int diagStart =
			    (int)converters->LineAndCharacterToPosition(
			        getScriptInfo(fileName), diag->Range.Start);
			if (diagStart >= markerPos) {
				return; // Found an error after the marker
			}
		}
	}
	t->Fatalf("Expected error after marker '%s' but none was found",
	          {markerName});
}

// VerifyErrorExistsBeforeMarker verifies that an error exists before
// the given marker. fourslash.go:5978.
void FourslashTest::VerifyErrorExistsBeforeMarker(
    gostd::testing::T* t, const std::string& markerName) {
	std::string fileName;
	int markerPos;

	if (markerName.empty()) {
		// Use current position
		fileName = activeFilename;
		markerPos =
		    (int)converters->LineAndCharacterToPosition(
		        getScriptInfo(activeFilename),
		        currentCaretPosition);
	} else {
		auto it = testData->MarkerPositions.find(markerName);
		if (it == testData->MarkerPositions.end()) {
			t->Fatalf("Marker '%s' not found", {markerName});
		}
		fileName = it->second->FileName();
		markerPos = it->second->Position;
	}

	auto diagnostics = getDiagnostics(t, fileName);

	for (auto& diag : diagnostics) {
		if (!isSuggestionDiagnostic(diag)) {
			int diagEnd =
			    (int)converters->LineAndCharacterToPosition(
			        getScriptInfo(fileName), diag->Range.End);
			if (diagEnd <= markerPos) {
				return; // Found an error before the marker
			}
		}
	}
	t->Fatalf("Expected error before marker '%s' but none was found",
	          {markerName});
}

// updatePositionForTextEdit — fourslash.go:6009.
int updatePositionForTextEdit(int position, int editStart, int editEnd,
                              int newTextLength) {
	if (position <= editStart) {
		return position;
	}
	if (position < editEnd) {
		return -1;
	}
	return position + newTextLength - (editEnd - editStart);
}

// removeWhitespace — fourslash.go:6019.
std::string removeWhitespace(const std::string& text) {
	gostr::Builder builder;
	gostr::forEachRune(text, [&](char32_t ch, size_t) {
		if (tsc::isWhiteSpaceLike(ch)) {
			return;
		}
		builder.WriteString(gostr::fromRune(ch));
	});
	return builder.String();
}

// assertValidTextRange — fourslash.go:6030.
void assertValidTextRange(gostd::testing::T* t,
                          TextRange textRange,
                          const std::string& message) {
	t->Helper();
	if (textRange.pos() >= 0 && textRange.end() >= 0) {
		return;
	}
	t->Fatal({message});
}

// selectCodeFixDiagnostic — fourslash.go:6038.
std::shared_ptr<lsproto::Diagnostic> selectCodeFixDiagnostic(
    const std::vector<std::shared_ptr<lsproto::Diagnostic>>&
        diagnostics,
    int errorCode) {
	if (errorCode == 0) {
		return diagnostics[0];
	}
	return gostr::coreFind(
	    diagnostics, [&](const std::shared_ptr<lsproto::Diagnostic>& d) {
		    return d->Code != nullptr &&
		           d->Code->Integer != nullptr &&
		           *d->Code->Integer == (int32_t)errorCode;
	    });
}

} // namespace tsc::fourslash
