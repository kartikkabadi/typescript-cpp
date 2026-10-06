// === slice: ls-coreC ===
// codelens.cpp — codelens.go: reference/implementation code lenses.
#include "internal/ls/ls.h"

#include "internal/diagnostics/diagnostics.h"
#include "internal/locale/locale.h"

#include <algorithm>

namespace tsc::ls {

namespace {

// codelens.go:68 — codeLensKey
struct codeLensKey {
	lsp::lsproto::CodeLensKind kind;
	uint32_t startLine = 0, startCharacter = 0, endLine = 0, endCharacter = 0;
	bool operator==(const codeLensKey&) const = default;
};

struct codeLensKeyHash {
	size_t operator()(const codeLensKey& k) const {
		size_t h = std::hash<lsp::lsproto::CodeLensKind>{}(k.kind);
		h = h * 31 + k.startLine;
		h = h * 31 + k.startCharacter;
		h = h * 31 + k.endLine;
		h = h * 31 + k.endCharacter;
		return h;
	}
};

// codelens.go:73 — keyForCodeLens
codeLensKey keyForCodeLens(lsp::lsproto::CodeLens* codeLens) {
	return codeLensKey{codeLens->Data->Kind,
					   codeLens->Range.Start.Line,
					   codeLens->Range.Start.Character,
					   codeLens->Range.End.Line,
					   codeLens->Range.End.Character};
}

// codelens.go:188 — isValidImplementationsCodeLensNode
bool isValidImplementationsCodeLensNode(::tsc::Node* node,
										lsutil::CodeLensUserPreferences userPrefs) {
	switch (node->kind) {
	// Always show on interfaces
	case Kind::InterfaceDeclaration:
		// TODO: ast.KindTypeAliasDeclaration?
		return true;

	// If configured, show on interface methods
	case Kind::MethodSignature:
		return tristateIsTrue(userPrefs.ImplementationsCodeLensShowOnInterfaceMethods) &&
			   node->parent->kind == Kind::InterfaceDeclaration;

	// If configured, show on all class methods - but not private ones.
	case Kind::MethodDeclaration:
		if (tristateIsTrue(userPrefs.ImplementationsCodeLensShowOnAllClassMethods) &&
			node->parent->kind == Kind::ClassDeclaration) {
			return !hasModifier(node, ModifierFlagsPrivate) &&
				   node->name()->kind != Kind::PrivateIdentifier;
		}
		[[fallthrough]];

	// Always show on abstract classes/properties/methods
	case Kind::ClassDeclaration:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::PropertyDeclaration:
		return hasModifier(node, ModifierFlagsAbstract);
	default:
		break;
	}
	return false;
}

// codelens.go:215 — isValidReferenceLensNode
bool isValidReferenceLensNode(::tsc::Node* node, lsutil::CodeLensUserPreferences userPrefs) {
	switch (node->kind) {
	case Kind::FunctionDeclaration:
		if (tristateIsTrue(userPrefs.ReferencesCodeLensShowOnAllFunctions)) {
			return true;
		}
		[[fallthrough]];

	case Kind::VariableDeclaration:
		return (getCombinedModifierFlags(node) & ModifierFlagsExport) != 0;

	case Kind::ClassDeclaration:
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::EnumDeclaration:
	case Kind::EnumMember:
		return true;

	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
		// Don't show if child and parent have same start
		// For https://github.com/microsoft/vscode/issues/90396
		// !!!
		switch (node->parent->kind) {
		case Kind::ClassDeclaration:
		case Kind::InterfaceDeclaration:
		case Kind::TypeLiteral:
			return true;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return false;
}

} // namespace

// ============================================================================
// codelens.go — ProvideCodeLenses
// ============================================================================
// codelens.go:18
lsp::lsproto::CodeLensResponse LanguageService::ProvideCodeLenses(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI) {
	SourceFile* file = getProgramAndFile(documentURI).second;

	lsutil::CodeLensUserPreferences userPrefs = UserPreferences().CodeLens;
	if (!tristateIsTrue(userPrefs.ReferencesCodeLensEnabled) &&
		!tristateIsTrue(userPrefs.ImplementationsCodeLensEnabled)) {
		return lsp::lsproto::CodeLensResponse{};
	}

	std::vector<std::shared_ptr<lsp::lsproto::CodeLens>> result;
	std::unordered_set<codeLensKey, codeLensKeyHash> seen;
	std::vector<SourceFile*> projections;
	projections.push_back(file);
	if (const std::vector<SourceFile*>* supplemental = file->SupplementalSourceFiles()) {
		projections.insert(projections.end(), supplemental->begin(), supplemental->end());
	}
	for (auto* projection : projections) {
		// Keeps track of the last symbol to avoid duplicating code lenses across overloads.
		Symbol* lastSymbol = nullptr;
		std::function<bool(::tsc::Node*)> visit;
		visit = [&](::tsc::Node* node) -> bool {
			if (gostd::ctxErr(ctx) != nullptr) {
				return true;
			}

			Symbol* currentSymbol = node->symbol();
			if (lastSymbol != currentSymbol) {
				lastSymbol = currentSymbol;

				if (tristateIsTrue(userPrefs.ReferencesCodeLensEnabled) &&
					isValidReferenceLensNode(node, userPrefs)) {
					if (lsp::lsproto::CodeLens* codeLens =
							newCodeLensForNode(documentURI, projection, node,
											   lsp::lsproto::CodeLensKindReferences);
						codeLens != nullptr &&
						seen.insert(keyForCodeLens(codeLens)).second) {
						result.push_back(
										std::shared_ptr<lsp::lsproto::CodeLens>(codeLens));
					}
				}

				if (tristateIsTrue(userPrefs.ImplementationsCodeLensEnabled) &&
					isValidImplementationsCodeLensNode(node, userPrefs)) {
					if (lsp::lsproto::CodeLens* codeLens =
							newCodeLensForNode(documentURI, projection, node,
											   lsp::lsproto::CodeLensKindImplementations);
						codeLens != nullptr &&
						seen.insert(keyForCodeLens(codeLens)).second) {
						result.push_back(
										std::shared_ptr<lsp::lsproto::CodeLens>(codeLens));
					}
				}
			}

			Symbol* savedLastSymbol = lastSymbol;
			node->forEachChild([&](::tsc::Node* child) { return visit(child); });
			lastSymbol = savedLastSymbol;
			return false;
		};

		visit(projection);
	}

	lsp::lsproto::CodeLensResponse res;
	res.CodeLenses = std::make_shared<
		lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::CodeLens>>>(
		std::move(result));
	return res;
}

// ============================================================================
// codelens.go — ResolveCodeLens
// ============================================================================
// codelens.go:83
std::pair<lsp::lsproto::CodeLens*, gostd::Error> LanguageService::ResolveCodeLens(
	gostd::Context ctx, lsp::lsproto::CodeLens* codeLens, std::string* showLocationsCommandName,
	CrossProjectOrchestrator* orchestrator) {
	lsp::lsproto::DocumentUri uri = codeLens->Data->Uri;
	lsp::lsproto::TextDocumentIdentifier textDoc;
	textDoc.Uri = uri;
	auto [program, file] = getProgramAndFile(uri);
	file = sourceFileForSupplementalFileIndex(file, codeLens->Data->SupplementalFileIndex);
	if (file == nullptr) {
		return {nullptr,
				gostd::errorf("supplemental source file index not found: %d",
							  {gostd::fmtArg(int(*codeLens->Data->SupplementalFileIndex))})};
	}
	// Go: locale.FromContext(ctx). gostd::Context carries no values, so this
	// always yields the default locale.
	locale::Locale loc = locale::fromContext(tsc::ContextPtr{});
	std::vector<lsp::lsproto::Location> locs;
	std::string lensTitle;
	if (codeLens->Data->Kind == lsp::lsproto::CodeLensKindReferences) {
		auto dataRes = provideSymbolsAndEntriesAtPosition(
			ctx, program, file, int(codeLens->Data->Position), false, false);
		SymbolAndEntriesData data = dataRes.first;
		lsp::lsproto::ReferenceParams params;
		params.TextDocument = textDoc;
		params.Position = codeLens->Range.Start;
		params.Context = std::make_shared<lsp::lsproto::ReferenceContext>();
		// Don't include the declaration in the references count.
		params.Context->IncludeDeclaration = false;
		auto [referencesResp, err] =
			provideReferencesFromData(ctx, &params, orchestrator, data);
		if (err != nullptr) {
			return {nullptr, err};
		}
		if (referencesResp.Locations != nullptr) {
			locs = referencesResp.Locations->value_or(
				std::vector<lsp::lsproto::Location>{});
		}

		if (locs.size() == 1) {
			lensTitle = localize(loc, X_1_reference,
											  X_1_reference->key, {});
		} else {
			lensTitle = localize(
				loc, X_0_references, X_0_references->key,
				{std::to_string(locs.size())});
		}
	}
	if (codeLens->Data->Kind == lsp::lsproto::CodeLensKindImplementations) {
		auto dataRes = provideSymbolsAndEntriesAtPosition(
			ctx, program, file, int(codeLens->Data->Position), false, true);
		SymbolAndEntriesData data = dataRes.first;
		lsp::lsproto::ImplementationParams params;
		params.TextDocument = textDoc;
		params.Position = codeLens->Range.Start;
		// "Force" link support to be false so that we only get `Locations` back,
		// and don't include the "current" node in the results.
		symbolEntryTransformOptions options;
		options.requireLocationsResult = true;
		options.dropOriginNodes = true;
		auto [implementations, err2] = provideImplementationsFromData(
			ctx, &params, options, orchestrator, data);
		if (err2 != nullptr) {
			return {nullptr, err2};
		}
		if (implementations.Locations != nullptr) {
			locs = implementations.Locations->value_or(
				std::vector<lsp::lsproto::Location>{});
		}

		if (locs.size() == 1) {
			lensTitle = localize(loc, X_1_implementation,
											  X_1_implementation->key, {});
		} else {
			lensTitle = localize(
				loc, X_0_implementations, X_0_implementations->key,
				{std::to_string(locs.size())});
		}
	}

	auto cmd = std::make_shared<lsp::lsproto::Command>();
	cmd->Title = lensTitle;
	if (!locs.empty() && showLocationsCommandName != nullptr) {
		cmd->Command = *showLocationsCommandName;
		// Go marshals each argument via encoding/json; LSPAny stores the
		// decoded form, so pack the same JSON shapes.
		auto positionToLSPAny = [](const lsp::lsproto::Position& p) {
			return lsp::lsproto::LSPAny(
				std::map<std::string, lsp::lsproto::LSPAny>{
					{"line", lsp::lsproto::LSPAny(int64_t(p.Line))},
					{"character", lsp::lsproto::LSPAny(int64_t(p.Character))},
				});
		};
		std::vector<lsp::lsproto::LSPAny> locArr;
		locArr.reserve(locs.size());
		for (auto& l : locs) {
			locArr.push_back(lsp::lsproto::LSPAny(
				std::map<std::string, lsp::lsproto::LSPAny>{
					{"uri", lsp::lsproto::LSPAny(l.Uri)},
					{"range", lsp::lsproto::LSPAny(
								  std::map<std::string, lsp::lsproto::LSPAny>{
									  {"start", positionToLSPAny(l.Range.Start)},
									  {"end", positionToLSPAny(l.Range.End)},
								  })},
				}));
		}
		cmd->Arguments = std::make_shared<
			lsp::lsproto::Slice<lsp::lsproto::LSPAny>>(
			std::vector<lsp::lsproto::LSPAny>{lsp::lsproto::LSPAny(uri),
											positionToLSPAny(codeLens->Range.Start),
											lsp::lsproto::LSPAny(std::move(locArr))});
	}

	codeLens->Command = cmd;
	return {codeLens, gostd::Error{}};
}

// ============================================================================
// codelens.go — newCodeLensForNode
// ============================================================================
// codelens.go:165
lsp::lsproto::CodeLens* LanguageService::newCodeLensForNode(
	lsp::lsproto::DocumentUri fileUri, SourceFile* file, ::tsc::Node* node,
	lsp::lsproto::CodeLensKind kind) {
	::tsc::Node* nodeForRange = node;
	::tsc::Node* nodeName = node->name();
	if (nodeName != nullptr) {
		nodeForRange = nodeName;
	}
	int pos = tsc::skipTrivia(file->Text(), int(nodeForRange->pos()));
	auto [lspRange, fidelity] = converters->ToLSPRangeForFeature(
		file, TextRange{static_cast<TextPos>(pos), node->end()}, spanmap::FeatureCodeLens);
	if (fidelity.IsNone()) {
		return nullptr;
	}

	auto* codeLens = new lsp::lsproto::CodeLens;
	codeLens->Range = lspRange;
	codeLens->Data = std::make_shared<lsp::lsproto::CodeLensData>();
	codeLens->Data->Kind = kind;
	codeLens->Data->Uri = fileUri;
	codeLens->Data->Position = int32_t(pos);
	codeLens->Data->SupplementalFileIndex = supplementalFileIndex(file);
	return codeLens;
}

} // namespace tsc::ls
