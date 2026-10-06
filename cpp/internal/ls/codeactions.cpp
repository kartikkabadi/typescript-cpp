// codeactions.go — LSP code actions entry point: organize-imports actions,
// quickfixes from registered CodeFixProviders, and fix-all actions.
#include "internal/ls/ls.h"

#include "internal/ast/ast.h"
#include "internal/compiler/program.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/locale/locale.h"
#include "internal/spanmap/spanmap.h"

#include <algorithm>

namespace tsc::ls {

// Compare — codeactions.go:49. Defines a total ordering for CodeAction
// values, comparing description then text edits lexicographically.
int CodeAction::Compare(const CodeAction* b) const {
	if (int c = Description.compare(b->Description); c != 0) {
		return c < 0 ? -1 : 1;
	}
	if (Changes.size() != b->Changes.size()) {
		return Changes.size() < b->Changes.size() ? -1 : 1;
	}
	for (size_t i = 0; i < Changes.size(); i++) {
		if (int c =
		        lsproto::CompareTextEdits(*Changes[i], *b->Changes[i]);
		    c != 0) {
			return c;
		}
	}
	return 0;
}

// codeFixProviders — codeactions.go:71. The list of all registered code fix
// providers.
std::vector<CodeFixProvider*> codeFixProviders{
    ImportFixProvider,
    IsolatedDeclarationsFixProvider,
    FixClassIncorrectlyImplementsInterfaceProvider,
    // Add more code fix providers here as they are implemented
};

// ProvideCodeActions — codeactions.go:79.
std::pair<lsproto::CommandOrCodeActionArrayOrNull, gostd::Error>
LanguageService::ProvideCodeActions(const gostd::Context& ctx,
                                    lsproto::CodeActionParams* params) {
	auto [program, file] = getProgramAndFile(params->TextDocument.Uri);

	std::vector<lsproto::CommandOrCodeAction> actions;

	if (params->Context != nullptr && params->Context->Only != nullptr) {
		for (auto& kind : *params->Context->Only) {
			auto matchingKinds = getOrganizeImportsActionsForKind(kind);
			for (auto& matchingKind : matchingKinds) {
				auto* organizeAction = createOrganizeImportsAction(
				    ctx, program, file, matchingKind);
				actions.push_back(*organizeAction);
			}

			if (isFixAllKind(kind)) {
				auto [fixAllAction, err] = createFixAllAction(
				    ctx, program, file, params->TextDocument.Uri);
				if (err != nullptr) {
					return {lsproto::CommandOrCodeActionArrayOrNull{},
					        err};
				}
				if (fixAllAction != nullptr) {
					actions.push_back(*fixAllAction);
				}
			}
		}
	}

	if (params->Context != nullptr &&
	    wantsQuickFixes(params->Context->Only)) {
		std::unordered_map<std::string, CodeFixProvider*> fixIdSeen;

		std::vector<CodeAction*>
		    seen; // sorted for binary search dedup, dedup across all
		          // diagnostics and providers so if multiple diags
		          // produce the same codefix, only one is returned

		for (auto* diag : params->Context->Diagnostics) {
			if (diag->Code == nullptr || diag->Code->Integer == nullptr) {
				continue;
			}

			auto errorCode = *diag->Code->Integer;

			for (auto* provider : codeFixProviders) {
				if (!codeFixProviderMatchesLSPDiagnostic(provider,
				                                       diag)) {
					continue;
				}

				for (auto& mapped :
				     converters->FromLSPRangeForSourceFile(
				         file, diag->Range,
				         spanmap::FeatureCodeActions)) {
					auto* fixContext = new CodeFixContext{
					    .SourceFile = mapped.Script_,
					    .Span = mapped.Span,
					    .ErrorCode = errorCode,
					    .Program = program,
					    .LS = this,
					    .Diagnostic = diag,
					    .Params = params,
					};

					auto [providerActions, err] =
					    provider->GetCodeActions(ctx, fixContext);
					if (err != nullptr) {
						return {
						    lsproto::
						        CommandOrCodeActionArrayOrNull{},
						    err};
					}
					for (auto* action : providerActions) {
						auto it = std::lower_bound(
						    seen.begin(), seen.end(), action,
						    [](CodeAction* a,
						       CodeAction* b) {
							    return a->Compare(b) < 0;
						    });
						if (it != seen.end() &&
						    (*it)->Compare(action) == 0) {
							continue;
						}
						seen.insert(it, action);
						actions.push_back(convertToLSPCodeAction(
						    action, diag,
						    params->TextDocument.Uri));
						if (action->FixID != "") {
							fixIdSeen[action->FixID] = provider;
						}
					}
				}
			}
		}

		auto [fixAllActions, err] = getFixAllQuickFixes(
		    ctx, program, file, params->TextDocument.Uri, fixIdSeen);
		if (err != nullptr) {
			return {lsproto::CommandOrCodeActionArrayOrNull{}, err};
		}
		actions.insert(actions.end(), fixAllActions.begin(),
		               fixAllActions.end());
	}

	return {lsproto::CommandOrCodeActionArrayOrNull{
	            .CommandOrCodeActionArray =
	                new std::vector<lsproto::CommandOrCodeAction>(
	                    actions)},
	        nullptr};
}

// getFixAllQuickFixes — codeactions.go:163. Returns per-provider "Fix all in
// file" quickfix entries for providers that matched at least 2 diagnostics
// in the full file.
std::pair<std::vector<lsproto::CommandOrCodeAction>, gostd::Error>
LanguageService::getFixAllQuickFixes(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    SourceFile* file, lsproto::DocumentUri uri,
    const std::unordered_map<std::string, CodeFixProvider*>& fixIdSeen) {
	std::vector<lsproto::CommandOrCodeAction> actions;

	// Deduplicate providers; multiple fixIds may map to the same provider.
	collections::Set<CodeFixProvider*> seen;
	for (auto& [_, provider] : fixIdSeen) {
		if (seen.Has(provider)) {
			continue;
		}
		seen.Add(provider);

		if (provider->GetAllCodeActions == nullptr) {
			continue;
		}

		if (!hasMultipleFixableDiagnostics(ctx, program, file,
		                                   provider->ErrorCodes)) {
			continue;
		}

		auto* fixContext = new CodeFixContext{
		    .SourceFile = file,
		    .Program = program,
		    .LS = this,
		};
		auto [combined, err] =
		    provider->GetAllCodeActions(ctx, fixContext);
		if (err != nullptr) {
			return {{}, err};
		}
		if (combined != nullptr && combined->Changes.size() > 0) {
			auto kind = lsproto::CodeActionKindQuickFix;
			auto* changes =
			    new std::unordered_map<
			        lsproto::DocumentUri, std::vector<lsproto::TextEdit*>,
			        lsproto::DocumentUriHash>();
			(*changes)[uri] = combined->Changes;
			actions.push_back(lsproto::CommandOrCodeAction{
			    .CodeAction = new lsproto::CodeAction{
			        .Title = combined->Description,
			        .Kind = new lsproto::CodeActionKind(kind),
			        .Edit = new lsproto::WorkspaceEdit{
			            .Changes = changes},
			    },
			});
		}
	}

	return {actions, nullptr};
}

// hasMultipleFixableDiagnostics — codeactions.go:218. Returns true if the
// file has at least 2 diagnostics matching the given error codes. Checks all
// diagnostic sources (semantic, syntactic, suggestion, declaration) to match
// ProvideDiagnostics.
bool hasMultipleFixableDiagnostics(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    SourceFile* file, const std::vector<int32_t>& errorCodes) {
	auto allDiags = getAllDiagnostics(ctx, program, file);
	int count = 0;
	for (auto* d : allDiags) {
		if (isFixableDiagnostic(d, errorCodes)) {
			count++;
			if (count >= 2) {
				return true;
			}
		}
	}
	return false;
}

// codeFixProviderMatchesLSPDiagnostic — codeactions.go:232.
bool codeFixProviderMatchesLSPDiagnostic(
    CodeFixProvider* provider, lsproto::Diagnostic* diagnostic) {
	if (diagnostic->Source != nullptr &&
	    *diagnostic->Source != "ts") {
		return false;
	}
	return diagnostic->Code != nullptr &&
	       diagnostic->Code->Integer != nullptr &&
	       containsErrorCode(provider->ErrorCodes,
	                         *diagnostic->Code->Integer);
}

// isFixableDiagnostic — codeactions.go:239.
bool isFixableDiagnostic(Diagnostic* diagnostic,
                         const std::vector<int32_t>& errorCodes) {
	return diagnostic->Source() == "" &&
	       containsErrorCode(errorCodes, diagnostic->Code());
}

// isFixAllKind — codeactions.go:244. Returns true if the requested kind
// matches source.fixAll.
bool isFixAllKind(lsproto::CodeActionKind kind) {
	return lsproto::codeActionKindContains(
	    kind, lsproto::CodeActionKindSourceFixAllTs);
}

// wantsQuickFixes — codeactions.go:250. Returns true if the Only filter is
// nil/empty (meaning all kinds are wanted) or explicitly includes the
// quickfix kind.
bool wantsQuickFixes(std::vector<lsproto::CodeActionKind>* only) {
	if (only == nullptr || only->empty()) {
		return true;
	}
	for (auto& kind : *only) {
		if (lsproto::codeActionKindContains(
		        kind, lsproto::CodeActionKindQuickFix)) {
			return true;
		}
	}
	return false;
}

// createFixAllAction — codeactions.go:264. Creates a source.fixAll code
// action that applies all auto-fixable code fixes across the file.
std::pair<lsproto::CommandOrCodeAction*, gostd::Error>
LanguageService::createFixAllAction(const gostd::Context& ctx,
                                    compiler::SimpleProgram* program,
                                    SourceFile* file,
                                    lsproto::DocumentUri uri) {
	auto kind = lsproto::CodeActionKindSourceFixAllTs;
	auto* lspChanges =
	    new std::unordered_map<lsproto::DocumentUri,
	                           std::vector<lsproto::TextEdit*>,
	                           lsproto::DocumentUriHash>();

	for (auto* provider : codeFixProviders) {
		if (provider->GetAllCodeActions == nullptr) {
			continue;
		}

		auto* fixContext = new CodeFixContext{
		    .SourceFile = file,
		    .Program = program,
		    .LS = this,
		};

		auto [combined, err] =
		    provider->GetAllCodeActions(ctx, fixContext);
		if (err != nullptr) {
			return {nullptr, err};
		}
		if (combined != nullptr && combined->Changes.size() > 0) {
			auto& dest = (*lspChanges)[uri];
			dest.insert(dest.end(), combined->Changes.begin(),
			            combined->Changes.end());
		}
	}

	if (lspChanges->empty()) {
		return {nullptr, nullptr};
	}

	return {new lsproto::CommandOrCodeAction{
	            .CodeAction = new lsproto::CodeAction{
	                .Title = ::tsc::localize(locale::fromContext(ctx),
	                                         Fix_All, "", {}),
	                .Kind = new lsproto::CodeActionKind(kind),
	                .Edit = new lsproto::WorkspaceEdit{
	                    .Changes = lspChanges},
	            }},
	        nullptr};
}

// getOrganizeImportsActionTitle — codeactions.go:307.
std::string getOrganizeImportsActionTitle(
    const gostd::Context& ctx, lsproto::CodeActionKind kind) {
	auto loc = locale::fromContext(ctx);
	if (kind == lsproto::CodeActionKindSourceRemoveUnusedImportsTs) {
		return ::tsc::localize(loc, Remove_Unused_Imports, "", {});
	}
	if (kind == lsproto::CodeActionKindSourceSortImportsTs) {
		return ::tsc::localize(loc, Sort_Imports, "", {});
	}
	return ::tsc::localize(loc, Organize_Imports, "", {});
}

// getOrganizeImportsActionsForKind — codeactions.go:321. Returns the
// organize imports code action kinds that should be returned for the given
// requested kind.
std::vector<lsproto::CodeActionKind> getOrganizeImportsActionsForKind(
    lsproto::CodeActionKind requestedKind) {
	std::vector<lsproto::CodeActionKind> organizeImportsKinds{
	    lsproto::CodeActionKindSourceOrganizeImportsTs,
	    lsproto::CodeActionKindSourceRemoveUnusedImportsTs,
	    lsproto::CodeActionKindSourceSortImportsTs,
	};

	std::vector<lsproto::CodeActionKind> result;
	for (auto& organizeKind : organizeImportsKinds) {
		if (lsproto::codeActionKindContains(requestedKind,
		                                    organizeKind)) {
			result.push_back(organizeKind);
		}
	}

	if (std::find(result.begin(), result.end(), requestedKind) !=
	    result.end()) {
		return {requestedKind};
	}

	return result;
}

// createOrganizeImportsAction — codeactions.go:343.
lsproto::CommandOrCodeAction* LanguageService::createOrganizeImportsAction(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    SourceFile* file, lsproto::CodeActionKind kind) {
	auto title = getOrganizeImportsActionTitle(ctx, kind);
	auto changes = OrganizeImports(ctx, file, program, kind);
	if (changes.empty()) {
		return new lsproto::CommandOrCodeAction{
		    .CodeAction = new lsproto::CodeAction{
		        .Title = title,
		        .Kind = new lsproto::CodeActionKind(kind),
		        .Edit = new lsproto::WorkspaceEdit{
		            .Changes = new std::unordered_map<
		                lsproto::DocumentUri,
		                std::vector<lsproto::TextEdit*>,
		                lsproto::DocumentUriHash>()},
		    },
		};
	}

	auto* lspChanges =
	    new std::unordered_map<lsproto::DocumentUri,
	                           std::vector<lsproto::TextEdit*>,
	                           lsproto::DocumentUriHash>();
	for (auto& [fileName, edits] : changes) {
		auto fileURI = lsconv::FileNameToDocumentURI(fileName);
		(*lspChanges)[fileURI] = edits;
	}

	return new lsproto::CommandOrCodeAction{
	    .CodeAction = new lsproto::CodeAction{
	        .Title = title,
	        .Kind = new lsproto::CodeActionKind(kind),
	        .Edit = new lsproto::WorkspaceEdit{.Changes = lspChanges},
	    },
	};
}

// containsErrorCode — codeactions.go:382.
bool containsErrorCode(const std::vector<int32_t>& codes, int32_t code) {
	return std::find(codes.begin(), codes.end(), code) != codes.end();
}

// convertToLSPCodeAction — codeactions.go:387.
lsproto::CommandOrCodeAction convertToLSPCodeAction(
    CodeAction* action, lsproto::Diagnostic* diag,
    lsproto::DocumentUri uri) {
	auto kind = lsproto::CodeActionKindQuickFix;
	auto* changes =
	    new std::unordered_map<lsproto::DocumentUri,
	                           std::vector<lsproto::TextEdit*>,
	                           lsproto::DocumentUriHash>();
	(*changes)[uri] = action->Changes;
	auto* diagnostics =
	    new std::vector<lsproto::Diagnostic*>{diag};

	return lsproto::CommandOrCodeAction{
	    .CodeAction = new lsproto::CodeAction{
	        .Title = action->Description,
	        .Kind = new lsproto::CodeActionKind(kind),
	        .Edit = new lsproto::WorkspaceEdit{.Changes = changes},
	        .Diagnostics = diagnostics,
	    },
	};
}

} // namespace tsc::ls
