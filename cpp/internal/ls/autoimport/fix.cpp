// fix.go — Fix, View::GetFixes, import-edit construction.
#include <algorithm>
#include <cstring>

#include "internal/astnav/tokens.h"
#include "internal/debug/debug.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/ls/change/change.h"
#include "internal/locale/locale.h"
#include "internal/scanner/scanner.h"

namespace tsc::ls::autoimport {
namespace {


// core.CompareBooleans — compares bools, false < true.
int compareBooleans(bool a, bool b) {
	if (a == b) {
		return 0;
	}
	if (a) {
		return 1;
	}
	return -1;
}

// GetFixes helper — unicode.IsUpper + unicode.ToUpper on the first byte like
// Go's `rune(name[0])` (names are identifier text; see view.cpp).
bool firstCharIsUpper(const std::string& name) {
	char32_t r = static_cast<unsigned char>(name[0]);
	return stringutil::toUpperRune(r) == r &&
	       stringutil::toLowerRune(r) != r;
}
std::string upperFirstChar(const std::string& name) {
	std::string out = name;
	out[0] = static_cast<char>(
	    stringutil::toUpperRune(static_cast<unsigned char>(name[0])));
	return out;
}

}  // namespace

// Fix::Edits — fix.go:56. Produces the text edits and a human-readable
// description for the fix. The returned bool is false when the fix targets a
// content-mapped file and any edit could not be placed within a single
// verbatim span, meaning it cannot be safely applied to the original text and
// the caller should discard it.
std::tuple<lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>>,
           std::string, bool>
Fix::Edits(
    gostd::Context ctx, SourceFile* file, const CompilerOptions* compilerOptions,
    const lsutil::FormatCodeSettings& formatOptions,
    lsconv::Converters* converters,
    const lsutil::UserPreferences& preferences) {
	// locale.FromContext(ctx): gostd::Context carries no values, so this is
	// Default unless a locale bridge is added (ctx unused otherwise).
	(void)ctx;
	locale::Locale loc = locale::fromContext(tsc::backgroundContext());
	std::unique_ptr<ls::change::Tracker> tracker =
	    std::make_unique<ls::change::Tracker>(
	        format::FormatRequestContext{}, compilerOptions,
	        formatOptions, converters);
	switch (AutoImportFix->Kind) {
		case lsp::lsproto::AutoImportFixKindUseNamespace: {
			std::string description =
			    addNamespaceQualifier(this, tracker.get(), file, loc);
			auto [edits, safe] = fileEdits(tracker.get(), file);
			return {edits, description, safe};
		}
		case lsp::lsproto::AutoImportFixKindAddToExisting: {
			if (static_cast<int>(file->imports.size()) <=
			    AutoImportFix->ImportIndex) {
				TSC_UNREACHABLE("import index out of range");
			}
			auto existingFix = getAddToExistingImportFix(file, this);
			std::vector<newImportBinding*> named;
			if (existingFix->namedImport != nullptr) {
				named.push_back(existingFix->namedImport.get());
			}
			addToExistingImport(tracker.get(), file,
			                    existingFix->importClauseOrBindingPattern,
			                    existingFix->defaultImport.get(), named,
			                    preferences);
			auto [edits, safe] = fileEdits(tracker.get(), file);
			return {edits,
			        localize(loc, Update_import_from_0,
			                              "", {AutoImportFix->ModuleSpecifier}),
			        safe};
		}
		case lsp::lsproto::AutoImportFixKindAddNew: {
			std::vector<Node*> declarations;
			newImportBinding* defaultImport = nullptr;
			std::unique_ptr<newImportBinding> defaultImportOwner;
			if (AutoImportFix->ImportKind == lsp::lsproto::ImportKindDefault) {
				auto b = std::make_unique<newImportBinding>();
				b->name = AutoImportFix->Name;
				b->addAsTypeOnly = AutoImportFix->AddAsTypeOnly;
				defaultImport = b.get();
				defaultImportOwner = std::move(b);
			}
			std::vector<newImportBinding*> namedImports;
			std::unique_ptr<newImportBinding> namedImportOwner;
			if (AutoImportFix->ImportKind == lsp::lsproto::ImportKindNamed) {
				auto b = std::make_unique<newImportBinding>();
				b->name = AutoImportFix->Name;
				b->addAsTypeOnly = AutoImportFix->AddAsTypeOnly;
				namedImportOwner = std::move(b);
				namedImports.push_back(namedImportOwner.get());
			}
			newImportBinding* namespaceLikeImport = nullptr;
			std::unique_ptr<newImportBinding> namespaceLikeImportOwner;
			// qualification := f.qualification()
			if (AutoImportFix->ImportKind == lsp::lsproto::ImportKindNamespace ||
			    AutoImportFix->ImportKind == lsp::lsproto::ImportKindCommonJS) {
				auto b = std::make_unique<newImportBinding>();
				b->kind = AutoImportFix->ImportKind;
				b->name = AutoImportFix->Name;
				namespaceLikeImportOwner = std::move(b);
				namespaceLikeImport = namespaceLikeImportOwner.get();
				// if qualification != nil && qualification.namespacePref != "" {
				// 	namespaceLikeImport.name = qualification.namespacePref
				// }
			}

			lsutil::QuotePreference quotePreference =
			    lsutil::GetQuotePreference(file, preferences);
			if (AutoImportFix->UseRequire) {
				declarations = getNewRequires(
				    tracker.get(), AutoImportFix->ModuleSpecifier, quotePreference,
				    defaultImport, namedImports, namespaceLikeImport,
				    compilerOptions);
			} else {
				declarations = getNewImports(
				    tracker.get(), AutoImportFix->ModuleSpecifier, quotePreference,
				    defaultImport, namedImports, namespaceLikeImport,
				    compilerOptions, preferences);
			}

			insertImports(tracker.get(), file, declarations,
			              /*blankLineBetween*/ true, preferences);
			// if qualification != nil {
			// 	addNamespaceQualifier(tracker.get(), file, qualification)
			// }
			auto [edits, safe] = fileEdits(tracker.get(), file);
			return {edits,
			        localize(loc, Add_import_from_0,
			                              "", {AutoImportFix->ModuleSpecifier}),
			        safe};
		}
		case lsp::lsproto::AutoImportFixKindPromoteTypeOnly: {
			Node* promotedDeclaration =
			    promoteFromTypeOnly(tracker.get(), TypeOnlyAliasDeclaration,
			                        compilerOptions, file, preferences);
			if (promotedDeclaration->kind == Kind::ImportSpecifier) {
				std::string moduleSpec = getModuleSpecifierText(
				    promotedDeclaration->parent->parent);
				auto [edits, safe] = fileEdits(tracker.get(), file);
				return {edits,
				        localize(
				            loc,
				            Remove_type_from_import_of_0_from_1,
				            "", {AutoImportFix->Name, moduleSpec}),
				        safe};
			}
			std::string moduleSpec = getModuleSpecifierText(promotedDeclaration);
			auto [edits, safe] = fileEdits(tracker.get(), file);
			return {edits,
			        localize(
			            loc,
			            Remove_type_from_import_declaration_from_0,
			            "", {moduleSpec}),
			        safe};
		}
		case lsp::lsproto::AutoImportFixKindJsdocTypeImport: {
			std::string description =
			    addImportType(this, file, preferences, tracker.get(), loc);
			auto [edits, safe] = fileEdits(tracker.get(), file);
			return {edits, description, safe};
		}
		default:
			TSC_UNREACHABLE("unimplemented fix edit");
	}
}

// fileEdits — fix.go:133. Returns the edits recorded for file, along with
// whether they are safe to apply. GetChanges drops the edits of any
// content-mapped file that cannot be faithfully mapped back to the original
// text, so an empty result with safe == false means the fix could not be
// represented and must be discarded.
std::pair<lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>>, bool>
fileEdits(change::Tracker* tracker, SourceFile* file) {
	auto [changes, unmappable] = tracker->GetChanges();
	auto it = changes.find(file->OriginalFileName());
	lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::TextEdit>> edits;
	if (it != changes.end()) {
		// Go shares heap []*lsproto.TextEdit (GC lifetime); materialize the
		// same ownership here — the tracker is discarded after GetChanges.
		edits.emplace();
		edits->reserve(it->second.size());
		for (auto& e : it->second) {
			edits->push_back(
			    std::make_shared<lsp::lsproto::TextEdit>(std::move(e)));
		}
	}
	return {edits, unmappable.empty()};
}

// addImportType — fix.go:138
std::string addImportType(Fix* f, SourceFile* file,
                          const lsutil::UserPreferences& preferences,
                          change::Tracker* tracker, locale::Locale loc) {
	if (f->AutoImportFix->UsagePosition == nullptr) {
		TSC_UNREACHABLE("UsagePosition must be set for JSDoc type import fix");
	}
	lsutil::QuotePreference quotePreference =
	    lsutil::GetQuotePreference(file, preferences);
	std::string quoteChar = "\"";
	if (quotePreference == lsutil::QuotePreferenceSingle) {
		quoteChar = "'";
	}
	std::string importTypePrefix = "import(" + quoteChar +
	                               f->AutoImportFix->ModuleSpecifier + quoteChar +
	                               ").";
	tracker->InsertText(file, *f->AutoImportFix->UsagePosition,
	                    importTypePrefix);
	return localize(
	    loc, Change_0_to_1, "",
	    {f->AutoImportFix->Name, importTypePrefix + f->AutoImportFix->Name});
}

// addNamespaceQualifier — fix.go:152
std::string addNamespaceQualifier(Fix* f, change::Tracker* tracker,
                                  SourceFile* file, locale::Locale loc) {
	if (f->AutoImportFix->UsagePosition == nullptr ||
	    f->AutoImportFix->NamespacePrefix.empty()) {
		TSC_UNREACHABLE("namespace fix requires usage position and prefix");
	}
	std::string qualified =
	    f->AutoImportFix->NamespacePrefix + "." + f->AutoImportFix->Name;
	tracker->InsertText(file, *f->AutoImportFix->UsagePosition,
	                    f->AutoImportFix->NamespacePrefix + ".");
	return localize(loc, Change_0_to_1, "",
	                             {f->AutoImportFix->Name, qualified});
}

// getAddToExistingImportFix — fix.go:161
std::unique_ptr<addToExistingImportFix> getAddToExistingImportFix(
    SourceFile* file, Fix* fix) {
	if (fix->AutoImportFix->Kind != lsp::lsproto::AutoImportFixKindAddToExisting) {
		TSC_UNREACHABLE("expected add to existing import fix");
	}
	Node* moduleSpecifier = file->imports[fix->AutoImportFix->ImportIndex];
	Node* importNode = tryGetImportFromModuleSpecifier(moduleSpecifier);
	if (importNode == nullptr) {
		TSC_UNREACHABLE("expected import declaration");
	}
	Node* importClauseOrBindingPattern = nullptr;
	switch (importNode->kind) {
		case Kind::ImportDeclaration:
			importClauseOrBindingPattern = importNode->importClause();
			if (importClauseOrBindingPattern == nullptr) {
				TSC_UNREACHABLE("expected import clause");
			}
			break;
		case Kind::CallExpression:
			if (!isVariableDeclarationInitializedToRequire(
			        importNode->parent)) {
				TSC_UNREACHABLE(
				    "expected require call expression to be in variable "
				    "declaration");
			}
			importClauseOrBindingPattern = importNode->parent->name();
			if (importClauseOrBindingPattern == nullptr ||
			    !isObjectBindingPattern(importClauseOrBindingPattern)) {
				TSC_UNREACHABLE(
				    "expected object binding pattern in variable "
				    "declaration");
			}
			break;
		default:
			TSC_UNREACHABLE(
			    "expected import declaration or require call expression");
	}

	std::unique_ptr<newImportBinding> defaultImport;
	if (fix->AutoImportFix->ImportKind == lsp::lsproto::ImportKindDefault) {
		defaultImport = std::make_unique<newImportBinding>();
		defaultImport->kind = lsp::lsproto::ImportKindDefault;
		defaultImport->name = fix->AutoImportFix->Name;
		defaultImport->addAsTypeOnly = fix->AutoImportFix->AddAsTypeOnly;
	}
	std::unique_ptr<newImportBinding> namedImport;
	if (fix->AutoImportFix->ImportKind == lsp::lsproto::ImportKindNamed) {
		namedImport = std::make_unique<newImportBinding>();
		namedImport->kind = lsp::lsproto::ImportKindNamed;
		namedImport->name = fix->AutoImportFix->Name;
		namedImport->addAsTypeOnly = fix->AutoImportFix->AddAsTypeOnly;
	}
	auto result = std::make_unique<addToExistingImportFix>();
	result->importClauseOrBindingPattern = importClauseOrBindingPattern;
	result->defaultImport = std::move(defaultImport);
	result->namedImport = std::move(namedImport);
	return result;
}

// addToExistingImport — fix.go:198
void addToExistingImport(change::Tracker* ct, SourceFile* file,
                         Node* importClauseOrBindingPattern,
                         newImportBinding* defaultImport,
                         const std::vector<newImportBinding*>& namedImports,
                         const lsutil::UserPreferences& preferences) {
	switch (importClauseOrBindingPattern->kind) {
		case Kind::ObjectBindingPattern: {
			BindingPattern* bindingPattern =
			    importClauseOrBindingPattern->as<BindingPattern>();
			if (defaultImport != nullptr) {
				addElementToBindingPattern(ct, file, bindingPattern,
				                           defaultImport->name, "default");
			}
			for (newImportBinding* namedImport : namedImports) {
				addElementToBindingPattern(ct, file, bindingPattern,
				                           namedImport->name, "");
			}
			return;
		}
		case Kind::ImportClause: {
			ImportClause* importClause =
			    importClauseOrBindingPattern->as<ImportClause>();

			// promoteFromTypeOnly = true if we need to promote the entire original clause from type only
			std::vector<newImportBinding*> allImports = namedImports;
			allImports.push_back(defaultImport);
			bool promoteFromTypeOnly = importClause->isTypeOnly() &&
			    std::any_of(allImports.begin(), allImports.end(),
			                [](newImportBinding* i) {
					                return i != nullptr &&
					                       i->addAsTypeOnly ==
					                           lsp::lsproto::AddAsTypeOnlyNotAllowed;
				                });

			std::vector<Node*> existingSpecifiers;
			if (importClause->NamedBindings != nullptr &&
			    importClause->NamedBindings->kind == Kind::NamedImports) {
				existingSpecifiers =
				    importClause->NamedBindings->as<NamedImports>()
				        ->Elements->nodes;
			}

			if (defaultImport != nullptr) {
				debug::assert(
				    importClause->name == nullptr,
				    "Cannot add a default import to an import clause that "
				    "already has one");
				change::NodeOptions opts;
				opts.Suffix = ", ";
				ct->InsertNodeAt(
				    file,
				    TextPos(astnav::getStartOfNode(
				        importClause->asNode(), file, false)),
				    ct->nodeFactory->newIdentifier(defaultImport->name),
				    opts);
			}

			if (!namedImports.empty()) {
				auto specCmp =
				    lsutil::GetNamedImportSpecifierComparerWithDetection(
				        importClause->parent, file, preferences);
				auto& specifierComparer = specCmp.first;
				auto& isSorted = specCmp.second;
				// core.Map(namedImports, ...)
				std::vector<Node*> newSpecifiers;
				newSpecifiers.reserve(namedImports.size());
				for (newImportBinding* namedImport : namedImports) {
					Node* identifier = nullptr;
					if (!namedImport->propertyName.empty()) {
						identifier =
						    ct->nodeFactory->newIdentifier(
						        namedImport->propertyName)
						        ->as<Identifier>()
						        ->asNode();
					}
					newSpecifiers.push_back(
					    ct->nodeFactory->newImportSpecifier(
					        (!importClause->isTypeOnly() ||
					         promoteFromTypeOnly) &&
					            shouldUseTypeOnly(namedImport->addAsTypeOnly,
					                              preferences),
					        identifier,
					        ct->nodeFactory->newIdentifier(
					            namedImport->name)));
				}
				std::sort(newSpecifiers.begin(), newSpecifiers.end(),
				          [&](Node* a, Node* b) {
					          return specifierComparer(a, b) < 0;
				          });
				if (!existingSpecifiers.empty() &&
				    isSorted != Tristate::False) {
					// The sorting preference computed earlier may or may not have validated that these particular
					// import specifiers are sorted. If they aren't, `getImportSpecifierInsertionIndex` will return
					// nonsense. So if there are existing specifiers, even if we know the sorting preference, we
					// need to ensure that the existing specifiers are sorted according to the preference in order
					// to do a sorted insertion.

					// If we're promoting the clause from type-only, we need to transform the existing imports
					// before attempting to insert the new named imports (for comparison purposes only)
					std::vector<Node*> specsToCompareAgainst =
					    existingSpecifiers;
					if (promoteFromTypeOnly && !existingSpecifiers.empty()) {
						specsToCompareAgainst.clear();
						for (Node* e : existingSpecifiers) {
							ImportSpecifier* spec =
							    e->as<ImportSpecifier>();
							Node* propertyName = nullptr;
							if (spec->PropertyName != nullptr) {
								propertyName = spec->PropertyName;
							}
							Node* syntheticSpec =
							    ct->nodeFactory->newImportSpecifier(
							        true, // isTypeOnly
							        propertyName, spec->name);
							specsToCompareAgainst.push_back(syntheticSpec);
						}
					}

					for (Node* spec : newSpecifiers) {
						int insertionIndex =
						    lsutil::GetImportSpecifierInsertionIndex(
						        specsToCompareAgainst, spec,
						        specifierComparer);
						ct->InsertImportSpecifierAtIndex(
						    file, spec, importClause->NamedBindings,
						    insertionIndex);
					}
				} else if (!existingSpecifiers.empty()) {
					for (Node* spec : newSpecifiers) {
						ct->InsertNodeInListAfter(
						    file,
						    existingSpecifiers
						        [existingSpecifiers.size() - 1],
						    spec->asNode(), nullptr);
					}
				} else {
					if (!newSpecifiers.empty()) {
						Node* namedImportsNode =
						    ct->nodeFactory->newNamedImports(
						        ct->nodeFactory->newNodeList(
						            newSpecifiers));
						if (importClause->NamedBindings != nullptr) {
							ct->ReplaceNode(
							    file, importClause->NamedBindings,
							    namedImportsNode, nullptr);
						} else {
							if (importClause->name == nullptr) {
								TSC_UNREACHABLE(
								    "Import clause must have either named "
								    "imports or a default import");
							}
							ct->InsertNodeAfter(file, importClause->name,
							                    namedImportsNode);
						}
					}
				}
			}

			if (promoteFromTypeOnly) {
				// Delete the 'type' keyword from the import clause
				Node* typeKeyword =
				    getTypeKeywordOfTypeOnlyImport(importClause, file);
				ct->Delete(file, typeKeyword);

				// Add 'type' modifier to existing specifiers (not newly added ones)
				// We preserve the type-onlyness of existing specifiers regardless of whether
				// it would make a difference in emit (user preference).
				if (!existingSpecifiers.empty()) {
					for (Node* specifier : existingSpecifiers) {
						if (!specifier->as<ImportSpecifier>()->IsTypeOnly) {
							ct->InsertModifierBefore(file, Kind::TypeKeyword,
							                         specifier);
						}
					}
				}
			}
			break;
		}
		default:
			TSC_UNREACHABLE("Unsupported clause kind for addToExistingImport");
	}
}

// getTypeKeywordOfTypeOnlyImport — fix.go:321
Node* getTypeKeywordOfTypeOnlyImport(Node* importClause,
                                     SourceFile* sourceFile) {
	debug::assert(importClause->isTypeOnly(),
	              "import clause must be type-only");
	// The first child of a type-only import clause is the 'type' keyword
	// import type { foo } from './bar'
	//        ^^^^
	Node* typeKeyword = astnav::findChildOfKind(
	    importClause->asNode(), Kind::TypeKeyword, sourceFile);
	debug::assert(typeKeyword != nullptr,
	              "type-only import clause should have a type keyword");
	return typeKeyword;
}

// addElementToBindingPattern — fix.go:331
void addElementToBindingPattern(change::Tracker* ct, SourceFile* file,
                                Node* bindingPatternNode,
                                const std::string& name,
                                const std::string& propertyName) {
	BindingPattern* bindingPattern = bindingPatternNode->as<BindingPattern>();
	Node* element = ct->nodeFactory->newBindingElement(
	    nullptr, nullptr, ct->nodeFactory->newIdentifier(name),
	    ifElse<Node*>(propertyName.empty(), nullptr,
	                  ct->nodeFactory->newIdentifier(propertyName)));
	if (!bindingPattern->Elements->nodes.empty()) {
		ct->InsertNodeInListAfter(
		    file,
		    bindingPattern->Elements->nodes
		        [bindingPattern->Elements->nodes.size() - 1],
		    element, bindingPattern->Elements);
	} else {
		ct->ReplaceNode(file, bindingPattern->asNode(),
		                ct->nodeFactory->newBindingPattern(
		                    Kind::ObjectBindingPattern,
		                    ct->nodeFactory->newNodeList({element})),
		                nullptr);
	}
}

// getNewImports — fix.go:346
std::vector<Node*> getNewImports(
    change::Tracker* ct, const std::string& moduleSpecifier,
    lsutil::QuotePreference quotePreference, newImportBinding* defaultImport,
    const std::vector<newImportBinding*>& namedImports,
    newImportBinding* namespaceLikeImport, const CompilerOptions* compilerOptions,
    const lsutil::UserPreferences& preferences) {
	TokenFlags tokenFlags =
	    ifElse(quotePreference == lsutil::QuotePreferenceSingle,
	           TokenFlagsSingleQuote, TokenFlagsNone);
	Node* moduleSpecifierStringLiteral =
	    ct->nodeFactory->newStringLiteral(moduleSpecifier, tokenFlags);
	std::vector<Node*> statements;
	if (defaultImport != nullptr || !namedImports.empty()) {
		// `verbatimModuleSyntax` should prefer top-level `import type` -
		// even though it's not an error, it would add unnecessary runtime emit.
		bool topLevelTypeOnly =
		    (defaultImport == nullptr ||
		     needsTypeOnly(defaultImport->addAsTypeOnly)) &&
		        std::all_of(namedImports.begin(), namedImports.end(),
		                    [](newImportBinding* i) {
			                    return needsTypeOnly(i->addAsTypeOnly);
		                    }) ||
		    (tristateIsTrue(compilerOptions->VerbatimModuleSyntax) ||
		     tristateIsTrue(preferences.PreferTypeOnlyAutoImports)) &&
		        (defaultImport == nullptr ||
		         defaultImport->addAsTypeOnly !=
		             lsp::lsproto::AddAsTypeOnlyNotAllowed) &&
		        !std::any_of(namedImports.begin(), namedImports.end(),
		                     [](newImportBinding* i) {
			                     return i->addAsTypeOnly ==
			                            lsp::lsproto::AddAsTypeOnlyNotAllowed;
		                     });

		Node* defaultImportNode = nullptr;
		if (defaultImport != nullptr) {
			defaultImportNode =
			    ct->nodeFactory->newIdentifier(defaultImport->name);
		}

		std::vector<Node*> namedImportNodes;
		namedImportNodes.reserve(namedImports.size());
		for (newImportBinding* namedImport : namedImports) {
			Node* namedImportPropertyName = nullptr;
			if (!namedImport->propertyName.empty()) {
				namedImportPropertyName = ct->nodeFactory->newIdentifier(
				    namedImport->propertyName);
			}
			namedImportNodes.push_back(
			    ct->nodeFactory->newImportSpecifier(
			        !topLevelTypeOnly &&
			            shouldUseTypeOnly(namedImport->addAsTypeOnly,
			                              preferences),
			        namedImportPropertyName,
			        ct->nodeFactory->newIdentifier(namedImport->name)));
		}
		statements.push_back(makeImport(ct, defaultImportNode,
		                                namedImportNodes,
		                                moduleSpecifierStringLiteral,
		                                topLevelTypeOnly));
	}

	if (namespaceLikeImport != nullptr) {
		Node* declaration;
		if (namespaceLikeImport->kind == lsp::lsproto::ImportKindCommonJS) {
			declaration = ct->nodeFactory->newImportEqualsDeclaration(
			    /*modifiers*/ nullptr,
			    shouldUseTypeOnly(namespaceLikeImport->addAsTypeOnly,
			                      preferences),
			    ct->nodeFactory->newIdentifier(namespaceLikeImport->name),
			    ct->nodeFactory->newExternalModuleReference(
			        moduleSpecifierStringLiteral));
		} else {
			declaration = ct->nodeFactory->newImportDeclaration(
			    /*modifiers*/ nullptr,
			    ct->nodeFactory->newImportClause(
			        /*phaseModifier*/
			        ifElse(shouldUseTypeOnly(
			                   namespaceLikeImport->addAsTypeOnly, preferences),
			               Kind::TypeKeyword, Kind::Unknown),
			        /*name*/ nullptr,
			        ct->nodeFactory->newNamespaceImport(
			            ct->nodeFactory->newIdentifier(
			                namespaceLikeImport->name))),
			    moduleSpecifierStringLiteral,
			    /*attributes*/ nullptr);
		}
		statements.push_back(declaration);
	}
	if (statements.empty()) {
		TSC_UNREACHABLE("No statements to insert for new imports");
	}
	return statements;
}

// getNewRequires — fix.go:415
std::vector<Node*> getNewRequires(
    change::Tracker* changeTracker, const std::string& moduleSpecifier,
    lsutil::QuotePreference quotePreference, newImportBinding* defaultImport,
    const std::vector<newImportBinding*>& namedImports,
    newImportBinding* namespaceLikeImport,
    const CompilerOptions* compilerOptions) {
	(void)compilerOptions;
	Node* quotedModuleSpecifier =
	    changeTracker->nodeFactory->newStringLiteral(
	        moduleSpecifier,
	        ifElse(quotePreference == lsutil::QuotePreferenceSingle,
	               TokenFlagsSingleQuote, TokenFlagsNone));
	std::vector<Node*> statements;

	// const { default: foo, bar, etc } = require('./mod');
	if (defaultImport != nullptr || !namedImports.empty()) {
		std::vector<Node*> bindingElements;
		for (newImportBinding* namedImport : namedImports) {
			Node* propertyName = nullptr;
			if (!namedImport->propertyName.empty()) {
				propertyName = changeTracker->nodeFactory->newIdentifier(
				    namedImport->propertyName);
			}
			bindingElements.push_back(
			    changeTracker->nodeFactory->newBindingElement(
			        /*dotDotDotToken*/ nullptr, propertyName,
			        changeTracker->nodeFactory->newIdentifier(
			            namedImport->name),
			        /*initializer*/ nullptr));
		}
		if (defaultImport != nullptr) {
			bindingElements.insert(
			    bindingElements.begin(),
			    changeTracker->nodeFactory->newBindingElement(
			        /*dotDotDotToken*/ nullptr,
			        changeTracker->nodeFactory->newIdentifier("default"),
			        changeTracker->nodeFactory->newIdentifier(
			            defaultImport->name),
			        /*initializer*/ nullptr));
		}
		Node* declaration = createConstEqualsRequireDeclaration(
		    changeTracker,
		    changeTracker->nodeFactory->newBindingPattern(
		        Kind::ObjectBindingPattern,
		        changeTracker->nodeFactory->newNodeList(bindingElements)),
		    quotedModuleSpecifier);
		statements.push_back(declaration);
	}

	// const foo = require('./mod');
	if (namespaceLikeImport != nullptr) {
		Node* declaration = createConstEqualsRequireDeclaration(
		    changeTracker,
		    changeTracker->nodeFactory->newIdentifier(
		        namespaceLikeImport->name),
		    quotedModuleSpecifier);
		statements.push_back(declaration);
	}

	debug::assert(!statements.empty(), "");
	return statements;
}

// createConstEqualsRequireDeclaration — fix.go:480
Node* createConstEqualsRequireDeclaration(change::Tracker* changeTracker,
                                          Node* name,
                                          Node* quotedModuleSpecifier) {
	return changeTracker->nodeFactory->newVariableStatement(
	    /*modifiers*/ nullptr,
	    changeTracker->nodeFactory->newVariableDeclarationList(
	        changeTracker->nodeFactory->newNodeList(
	            {changeTracker->nodeFactory->newVariableDeclaration(
	                name,
	                /*exclamationToken*/ nullptr,
	                /*type*/ nullptr,
	                changeTracker->nodeFactory->newCallExpression(
	                    changeTracker->nodeFactory->newIdentifier("require"),
	                    /*questionDotToken*/ nullptr,
	                    /*typeArguments*/ nullptr,
	                    changeTracker->nodeFactory->newNodeList(
	                        {quotedModuleSpecifier}),
	                    NodeFlagsNone))}),
	        NodeFlagsConst));
}

// insertImports — fix.go:503
void insertImports(change::Tracker* ct, SourceFile* sourceFile,
                   const std::vector<Node*>& imports, bool blankLineBetween,
                   const lsutil::UserPreferences& preferences) {
	std::vector<Node*> existingImportStatements;

	if (imports[0]->kind == Kind::VariableStatement) {
		for (Node* s : sourceFile->Statements->nodes) {
			if (isRequireVariableStatement(s)) {
				existingImportStatements.push_back(s);
			}
		}
	} else {
		for (Node* s : sourceFile->Statements->nodes) {
			if (isAnyImportSyntax(s)) {
				existingImportStatements.push_back(s);
			}
		}
	}
	auto orgCmp = lsutil::GetOrganizeImportsStringComparerWithDetection(
	    existingImportStatements, preferences);
	auto& comparer = orgCmp.first;
	auto& isSorted = orgCmp.second;
	std::vector<Node*> sortedNewImports = imports;
	std::sort(sortedNewImports.begin(), sortedNewImports.end(),
	          [&](Node* a, Node* b) {
		          return lsutil::CompareImportsOrRequireStatements(a, b,
		                                                           comparer) <
		                 0;
	          });

	if (!existingImportStatements.empty() && isSorted) {
		// Existing imports are sorted, insert each new import at the correct position
		for (Node* newImport : sortedNewImports) {
			int insertionIndex = lsutil::GetImportDeclarationInsertIndex(
			    existingImportStatements, newImport,
			    [&](Node* a, Node* b) -> int {
				    return lsutil::CompareImportsOrRequireStatements(a, b,
				                                                     comparer);
			    });
			if (insertionIndex == 0) {
				// If the first import is top-of-file, insert after the leading comment which is likely the header.
				change::LeadingTriviaOption leadingTriviaOption =
				    change::LeadingTriviaOptionNone;
				if (existingImportStatements[0] ==
				    sourceFile->Statements->nodes[0]) {
					leadingTriviaOption = change::LeadingTriviaOptionExclude;
				}
				ct->InsertNodeBefore(sourceFile,
				                     existingImportStatements[0]->asNode(),
				                     newImport->asNode(),
				                     false /*blankLineBetween*/,
				                     leadingTriviaOption);
			} else {
				Node* prevImport = existingImportStatements[insertionIndex - 1];
				ct->InsertNodeAfter(sourceFile, prevImport->asNode(),
				                    newImport->asNode());
			}
		}
	} else if (!existingImportStatements.empty()) {
		ct->InsertNodesAfter(
		    sourceFile,
		    existingImportStatements[existingImportStatements.size() - 1],
		    sortedNewImports);
	} else {
		ct->InsertAtTopOfFile(sourceFile, sortedNewImports, blankLineBetween);
	}
}

// makeImport — fix.go:542
Node* makeImport(change::Tracker* ct, Node* defaultImport,
                 const std::vector<Node*>& namedImports, Node* moduleSpecifier,
                 bool isTypeOnly) {
	Node* newNamedImports = nullptr;
	if (!namedImports.empty()) {
		newNamedImports = ct->nodeFactory->newNamedImports(
		    ct->nodeFactory->newNodeList(namedImports));
	}
	Node* importClause = nullptr;
	if (defaultImport != nullptr || newNamedImports != nullptr) {
		importClause = ct->nodeFactory->newImportClause(
		    ifElse(isTypeOnly, Kind::TypeKeyword, Kind::Unknown), defaultImport,
		    newNamedImports);
	}
	return ct->nodeFactory->newImportDeclaration(
	    /*modifiers*/ nullptr, importClause, moduleSpecifier,
	    /*attributes*/ nullptr);
}

// View::GetFixes — fix.go:554
std::vector<std::unique_ptr<Fix>> View::GetFixes(
    Export* e, bool forJSX, bool isValidTypeOnlyUseSite,
    const lsp::lsproto::Position* usagePosition) {
	std::vector<std::unique_ptr<Fix>> fixes;
	if (auto namespaceFix =
	        tryUseExistingNamespaceImport(e, usagePosition)) {
		fixes.push_back(std::move(namespaceFix));
	}

	if (auto fix = tryAddToExistingImport(e, isValidTypeOnlyUseSite)) {
		fixes.push_back(std::move(fix));
		return fixes;
	}

	// !!! getNewImportFromExistingSpecifier - even worth it?

	auto [moduleSpecifier, moduleSpecifierKind] =
	    GetModuleSpecifier(e, preferences);
	if (moduleSpecifier.empty()) {
		if (!fixes.empty()) {
			return fixes;
		}
		return {};
	}

	// Check if we need a JSDoc import type fix (for JS files with type-only imports)
	bool isJs = tspath::hasJSFileExtension(importingFile->FileName());
	bool importedSymbolHasValueMeaning =
	    (e->Flags & SymbolFlagsValue) != 0 || e->IsUnresolvedAlias();
	if (!importedSymbolHasValueMeaning && isJs && usagePosition != nullptr) {
		// For pure types in JS files, use JSDoc import type syntax
		std::vector<std::unique_ptr<Fix>> out;
		auto f = std::make_unique<Fix>();
		f->AutoImportFix = new lsp::lsproto::AutoImportFix{};
		f->AutoImportFix->Kind = lsp::lsproto::AutoImportFixKindJsdocTypeImport;
		f->AutoImportFix->ModuleSpecifier = moduleSpecifier;
		f->AutoImportFix->Name = e->Name();
		f->AutoImportFix->UsagePosition =
		    std::make_shared<lsp::lsproto::Position>(*usagePosition);
		f->ModuleSpecifierKind = moduleSpecifierKind;
		f->IsReExport = !(e->Target.ModuleID == e->exportID.ModuleID);
		f->ModuleFileName = e->ModuleFileName;
		out.push_back(std::move(f));
		return out;
	}

	lsp::lsproto::ImportKind importKind =
	    getImportKind(importingFile, e, program, false /*forceImportKeyword*/);
	lsp::lsproto::AddAsTypeOnly addAsTypeOnly =
	    getAddAsTypeOnly(isValidTypeOnlyUseSite, e, program->Options());

	std::string name = e->Name();
	bool startsWithUpper = firstCharIsUpper(name);
	if (forJSX && !startsWithUpper) {
		if (e->IsRenameable()) {
			name = upperFirstChar(name);
		} else {
			return {};
		}
	}

	auto f = std::make_unique<Fix>();
	f->AutoImportFix = new lsp::lsproto::AutoImportFix{};
	f->AutoImportFix->Kind = lsp::lsproto::AutoImportFixKindAddNew;
	f->AutoImportFix->ImportKind = importKind;
	f->AutoImportFix->ModuleSpecifier = moduleSpecifier;
	f->AutoImportFix->Name = name;
	f->AutoImportFix->UseRequire = shouldUseRequire();
	f->AutoImportFix->AddAsTypeOnly = addAsTypeOnly;
	f->ModuleSpecifierKind = moduleSpecifierKind;
	f->IsReExport = !(e->Target.ModuleID == e->exportID.ModuleID);
	f->ModuleFileName = e->ModuleFileName;
	fixes.push_back(std::move(f));
	return fixes;
}

// getAddAsTypeOnly — fix.go:623. Determines if an import should be type-only
// based on usage context.
lsp::lsproto::AddAsTypeOnly getAddAsTypeOnly(bool isValidTypeOnlyUseSite, Export* e,
                                        const CompilerOptions* compilerOptions) {
	if (!isValidTypeOnlyUseSite) {
		// Can't use a type-only import if the usage is an emitting position
		return lsp::lsproto::AddAsTypeOnlyNotAllowed;
	}
	if ((tristateIsTrue(compilerOptions->VerbatimModuleSyntax) &&
	     (e->IsTypeOnly || (e->Flags & SymbolFlagsValue) == 0)) ||
	    (e->IsTypeOnly && (e->Flags & SymbolFlagsValue) != 0)) {
		// A type-only import is required for this symbol if under verbatimModuleSyntax and it's purely a type
		return lsp::lsproto::AddAsTypeOnlyRequired;
	}
	return lsp::lsproto::AddAsTypeOnlyAllowed;
}

// View::tryUseExistingNamespaceImport — fix.go:636
std::unique_ptr<Fix> View::tryUseExistingNamespaceImport(
    Export* e, const lsp::lsproto::Position* usagePosition) {
	if (usagePosition == nullptr) {
		return nullptr;
	}

	if (getImportKind(importingFile, e, program,
	                  false /*forceImportKeyword*/) !=
	    lsp::lsproto::ImportKindNamed) {
		return nullptr;
	}

	collections::MultiMap<ModuleID, existingImport>* existingImports =
	    getExistingImports();
	std::vector<existingImport> matchingDeclarations =
	    existingImports->Get(e->exportID.ModuleID);
	for (const existingImport& existingImport : matchingDeclarations) {
		std::string namespacePrefix =
		    getNamespaceLikeImportText(existingImport.node);
		if (namespacePrefix.empty() ||
		    existingImport.moduleSpecifier.empty()) {
			continue;
		}
		auto f = std::make_unique<Fix>();
		f->AutoImportFix = new lsp::lsproto::AutoImportFix{};
		f->AutoImportFix->Kind = lsp::lsproto::AutoImportFixKindUseNamespace;
		f->AutoImportFix->Name = e->Name();
		f->AutoImportFix->ModuleSpecifier = existingImport.moduleSpecifier;
		f->AutoImportFix->ImportKind = lsp::lsproto::ImportKindNamespace;
		f->AutoImportFix->AddAsTypeOnly = lsp::lsproto::AddAsTypeOnlyAllowed;
		f->AutoImportFix->ImportIndex =
		    static_cast<int32_t>(existingImport.index);
		f->AutoImportFix->UsagePosition =
		    std::make_shared<lsp::lsproto::Position>(*usagePosition);
		f->AutoImportFix->NamespacePrefix = namespacePrefix;
		return f;
	}

	return nullptr;
}

// getNamespaceLikeImportText — fix.go:669
std::string getNamespaceLikeImportText(Node* declaration) {
	switch (declaration->kind) {
		case Kind::VariableDeclaration: {
			Node* name = declaration->name();
			if (name != nullptr && name->kind == Kind::Identifier) {
				return name->text();
			}
			return "";
		}
		case Kind::ImportEqualsDeclaration:
			return declaration->name()->text();
		case Kind::JSDocImportTag:
		case Kind::ImportDeclaration: {
			Node* importClause = declaration->importClause();
			if (importClause != nullptr &&
			    importClause->as<ImportClause>()->NamedBindings != nullptr &&
			    importClause->as<ImportClause>()->NamedBindings->kind ==
			        Kind::NamespaceImport) {
				return importClause->as<ImportClause>()
				    ->NamedBindings->name()
				    ->text();
			}
			return "";
		}
		default:
			return "";
	}
}

// View::tryAddToExistingImport — fix.go:690
std::unique_ptr<Fix> View::tryAddToExistingImport(
    Export* e, bool isValidTypeOnlyUseSite) {
	collections::MultiMap<ModuleID, existingImport>* existingImports =
	    getExistingImports();
	std::vector<existingImport> matchingDeclarations =
	    existingImports->Get(e->exportID.ModuleID);
	if (matchingDeclarations.empty()) {
		return nullptr;
	}

	// Can't use an es6 import for a type in JS.
	if (isSourceFileJS(importingFile) && (e->Flags & SymbolFlagsValue) == 0 &&
	    !std::all_of(matchingDeclarations.begin(), matchingDeclarations.end(),
	                 [](const existingImport& i) {
		                 return isJSDocImportTag(i.node);
	                 })) {
		return nullptr;
	}

	lsp::lsproto::ImportKind importKind =
	    getImportKind(importingFile, e, program, false /*forceImportKeyword*/);
	if (importKind == lsp::lsproto::ImportKindCommonJS ||
	    importKind == lsp::lsproto::ImportKindNamespace) {
		return nullptr;
	}

	lsp::lsproto::AddAsTypeOnly addAsTypeOnly =
	    getAddAsTypeOnly(isValidTypeOnlyUseSite, e, program->Options());

	std::unique_ptr<Fix> best;
	for (const existingImport& existingImport : matchingDeclarations) {
		if (existingImport.node->kind == Kind::ImportEqualsDeclaration) {
			continue;
		}

		if (existingImport.node->kind == Kind::VariableDeclaration) {
			if ((importKind == lsp::lsproto::ImportKindNamed ||
			     importKind == lsp::lsproto::ImportKindDefault) &&
			    existingImport.node->name()->kind ==
			        Kind::ObjectBindingPattern) {
				auto fix = std::make_unique<Fix>();
				fix->AutoImportFix = new lsp::lsproto::AutoImportFix{};
				fix->AutoImportFix->Kind =
				    lsp::lsproto::AutoImportFixKindAddToExisting;
				fix->AutoImportFix->Name = e->Name();
				fix->AutoImportFix->ImportKind = importKind;
				fix->AutoImportFix->ImportIndex =
				    static_cast<int32_t>(existingImport.index);
				fix->AutoImportFix->ModuleSpecifier =
				    existingImport.moduleSpecifier;
				fix->AutoImportFix->AddAsTypeOnly = addAsTypeOnly;
				// Variable declarations are never type-only.
				// Give preference to putting types in existing type-only imports and avoiding conversions
				// of import statements to/from type-only.
				if (addAsTypeOnly == lsp::lsproto::AddAsTypeOnlyNotAllowed) {
					return fix;
				}
				if (best == nullptr) {
					best = std::move(fix);
				}
			}
			continue;
		}

		Node* importClauseNode = existingImport.node->importClause();
		if (importClauseNode == nullptr ||
		    !isStringLiteralLike(existingImport.node->moduleSpecifier())) {
			// Side-effect import (no import clause) - can't add to it
			continue;
		}
		ImportClause* importClause = importClauseNode->as<ImportClause>();

		Node* namedBindings = importClause->NamedBindings;
		// A type-only import may not have both a default and named imports, so the only way a name can
		// be added to an existing type-only import is adding a named import to existing named bindings.
		if (importClause->isTypeOnly() &&
		    !(importKind == lsp::lsproto::ImportKindNamed &&
		      namedBindings != nullptr)) {
			continue;
		}

		if (importKind == lsp::lsproto::ImportKindDefault &&
		    (importClause->name != nullptr ||
		     // Cannot add a default import as type-only if the import already has named bindings
		     (addAsTypeOnly == lsp::lsproto::AddAsTypeOnlyRequired &&
		      namedBindings != nullptr))) {
			continue;
		}

		// Cannot add a named import to a declaration that has a namespace import
		if (importKind == lsp::lsproto::ImportKindNamed &&
		    namedBindings != nullptr &&
		    namedBindings->kind == Kind::NamespaceImport) {
			continue;
		}

		auto fix = std::make_unique<Fix>();
		fix->AutoImportFix = new lsp::lsproto::AutoImportFix{};
		fix->AutoImportFix->Kind = lsp::lsproto::AutoImportFixKindAddToExisting;
		fix->AutoImportFix->Name = e->Name();
		fix->AutoImportFix->ImportKind = importKind;
		fix->AutoImportFix->ImportIndex =
		    static_cast<int32_t>(existingImport.index);
		fix->AutoImportFix->ModuleSpecifier = existingImport.moduleSpecifier;
		fix->AutoImportFix->AddAsTypeOnly = addAsTypeOnly;

		bool isTypeOnly = importClause->isTypeOnly();
		// Give preference to putting types in existing type-only imports and avoiding conversions
		// of import statements to/from type-only.
		if ((addAsTypeOnly != lsp::lsproto::AddAsTypeOnlyNotAllowed && isTypeOnly) ||
		    (addAsTypeOnly == lsp::lsproto::AddAsTypeOnlyNotAllowed &&
		     !isTypeOnly)) {
			return fix;
		}
		if (best == nullptr) {
			best = std::move(fix);
		}
	}

	return best;
}

// GetImportKindForImportStatement — fix.go:796
lsp::lsproto::ImportKind GetImportKindForImportStatement(
    SourceFile* importingFile, Export* e, compiler::SimpleProgram* program) {
	return getImportKind(importingFile, e, program,
	                     true /*forceImportKeyword*/);
}

// getImportKind — fix.go:800
lsp::lsproto::ImportKind getImportKind(SourceFile* importingFile, Export* e,
                                  compiler::SimpleProgram* program,
                                  bool forceImportKeyword) {
	if (tristateIsTrue(program->Options()->VerbatimModuleSyntax) &&
	    program->GetEmitModuleFormatOfFile(importingFile) ==
	        ModuleKind::CommonJS) {
		return lsp::lsproto::ImportKindCommonJS;
	}
	switch (e->Syntax) {
		case ExportSyntax::DefaultModifier:
		case ExportSyntax::DefaultDeclaration:
			return lsp::lsproto::ImportKindDefault;
		case ExportSyntax::Named:
			if (e->exportID.ExportName == InternalSymbolNameDefault) {
				return lsp::lsproto::ImportKindDefault;
			}
			[[fallthrough]];
		case ExportSyntax::Modifier:
		case ExportSyntax::Star:
		case ExportSyntax::CommonJSExportsProperty:
			return lsp::lsproto::ImportKindNamed;
		case ExportSyntax::Equals:
		case ExportSyntax::CommonJSModuleExports:
		case ExportSyntax::UMD:
			// export.Syntax will be ExportSyntaxEquals for named exports/properties of an export='s target.
			if (e->exportID.ExportName != InternalSymbolNameExportEquals) {
				return lsp::lsproto::ImportKindNamed;
			}
			// !!! cache this?
			for (Node* statement : importingFile->Statements->nodes) {
				// `import foo` parses as an ImportEqualsDeclaration even though it could be an ImportDeclaration
				if (isImportEqualsDeclaration(statement) &&
				    !nodeIsMissing(
				        statement->as<ImportEqualsDeclaration>()
				            ->ModuleReference)) {
					return lsp::lsproto::ImportKindCommonJS;
				}
			}
			// !!! this logic feels weird; we're basically trying to predict if shouldUseRequire is going to
			//     be true. The meaning of "default import" is different depending on whether we write it as
			//     a require or an es6 import. The latter, compiled to CJS, has interop built in that will
			//     avoid accessing .default, but if we write a require directly and call it a default import,
			//     we emit an unconditional .default access.
			if (importingFile->ExternalModuleIndicator != nullptr ||
			    forceImportKeyword || !isSourceFileJS(importingFile)) {
				return lsp::lsproto::ImportKindDefault;
			}
			return lsp::lsproto::ImportKindCommonJS;
		default:
			TSC_UNREACHABLE("unhandled export syntax kind");
	}
}

// View::getExistingImports — fix.go:846
collections::MultiMap<ModuleID, existingImport>* View::getExistingImports() {
	if (existingImports != nullptr) {
		return existingImports.get();
	}

	auto result = std::make_unique<
	    collections::MultiMap<ModuleID, existingImport>>();

	auto& imps = importingFile->imports;
	for (size_t i = 0; i < imps.size(); i++) {
		Node* moduleSpecifier = imps[i];
		Node* node = tryGetImportFromModuleSpecifier(moduleSpecifier);
		if (node == nullptr) {
			TSC_UNREACHABLE("error: did not expect node kind");
		} else if (isVariableDeclarationInitializedToRequire(node->parent)) {
			if (Symbol* moduleSymbol = checker->ResolveExternalModuleName(
			        moduleSpecifier, nullptr /*importAttributesType*/)) {
				if (auto mf =
				        tryGetModuleIDAndFileNameOfModuleSymbol(moduleSymbol)) {
					result->Add(
					    mf->first,
					    existingImport{node->parent,
					                           moduleSpecifier->text(),
					                           static_cast<int>(i)});
				}
			}
		} else if (node->kind == Kind::ImportDeclaration ||
		           node->kind == Kind::ImportEqualsDeclaration ||
		           node->kind == Kind::JSDocImportTag) {
			if (Symbol* moduleSymbol =
			        checker->GetSymbolAtLocation(moduleSpecifier)) {
				if (auto mf =
				        tryGetModuleIDAndFileNameOfModuleSymbol(moduleSymbol)) {
					result->Add(
					    mf->first,
					    existingImport{node,
					                           moduleSpecifier->text(),
					                           static_cast<int>(i)});
				}
			}
		}
	}
	existingImports = std::move(result);
	return existingImports.get();
}

// View::shouldUseRequire — fix.go:875
bool View::shouldUseRequire() {
	if (shouldUseRequireForFixes.has_value()) {
		return *shouldUseRequireForFixes;
	}
	bool result = computeShouldUseRequire();
	shouldUseRequireForFixes = result;
	return result;
}

// detectSyntax — fix.go:897. Returns whether a source file has unambiguous ESM
// or CJS syntax. When moduleDetection is "force", ExternalModuleIndicator may
// be set to the source file node itself rather than a genuine syntax
// indicator, so we fall back to inspecting the file's Imports() to find
// actual import/export declarations.
fileSyntaxKind detectSyntax(SourceFile* file, const CompilerOptions* options) {
	auto [hasESM, hasCJS] = detectSyntaxIndicators(file, options);
	if (hasCJS && !hasESM) {
		return fileSyntaxKindCJS;
	}
	if (hasESM && !hasCJS) {
		return fileSyntaxKindESM;
	}
	return fileSyntaxKindAmbiguous;
}

// detectSyntaxIndicators — fix.go:913. Checks whether a source file contains
// genuine ESM and/or CJS syntax. Under moduleDetection "force", the cached
// ExternalModuleIndicator may be the source file itself rather than a real
// statement, so we look at Imports() for actual import/export declarations.
std::pair<bool, bool> detectSyntaxIndicators(SourceFile* file,
                                             const CompilerOptions* options) {
	bool hasCJS = file->CommonJSModuleIndicator != nullptr;
	if (options->GetEmitModuleDetectionKind() !=
	    ModuleDetectionKind::Force) {
		// ExternalModuleIndicator is reliable when moduleDetection is not "force"
		bool hasESM = file->ExternalModuleIndicator != nullptr;
		return {hasESM, hasCJS};
	}
	// Under moduleDetection "force", ExternalModuleIndicator is set to
	// file.AsNode() when there is no genuine ESM syntax, so only trust it
	// when it points to a real statement node.
	if (file->ExternalModuleIndicator != nullptr &&
	    file->ExternalModuleIndicator != file->asNode()) {
		return {true, hasCJS};
	}
	// Fall back to scanning Imports() for actual import/export declarations
	// (not require() calls or dynamic imports).
	for (Node* imp : file->imports) {
		if ((imp->flags & NodeFlagsSynthesized) != 0) {
			continue;
		}
		Node* parent = imp->parent;
		if (parent == nullptr) {
			continue;
		}
		switch (parent->kind) {
			case Kind::ImportDeclaration:
			case Kind::JSImportDeclaration:
			case Kind::ExportDeclaration:
				return {true, hasCJS};
			case Kind::ExternalModuleReference:
				// import x = require("...") — this is ESM-ish syntax
				return {true, hasCJS};
		}
	}
	return {false, hasCJS};
}

// View::computeShouldUseRequire — fix.go:947
bool View::computeShouldUseRequire() {
	// 1. TypeScript files don't use require variable declarations
	if (!tspath::hasJSFileExtension(importingFile->FileName())) {
		return false;
	}

	// 2. If the current source file is unambiguously CJS or ESM, go with that
	switch (detectSyntax(importingFile, program->Options())) {
		case fileSyntaxKindCJS:
			return true;
		case fileSyntaxKindESM:
			return false;
	}

	// 3. Use the implied node format to determine CJS vs ESM
	//    TODO: consider removing `impliedNodeFormatForEmit`
	switch (program->GetImpliedNodeFormatForEmit(importingFile)) {
		case ModuleKind::CommonJS:
			return true;
		case ModuleKind::ESNext:
			return false;
	}

	// 4. If there's a tsconfig/jsconfig, use its module setting
	if (!program->Options()->ConfigFilePath.empty()) {
		return program->Options()->GetEmitModuleKind() <
		       ModuleKind::ES2015;
	}

	// 5. Match the first other JS file in the program that's unambiguously CJS or ESM
	for (SourceFile* otherFile : program->GetSourceFiles()) {
		if (otherFile == importingFile || !isSourceFileJS(otherFile) ||
		    program->IsSourceFileFromExternalLibrary(otherFile)) {
			continue;
		}
		switch (detectSyntax(otherFile, program->Options())) {
			case fileSyntaxKindCJS:
				return true;
			case fileSyntaxKindESM:
				return false;
		}
	}

	// 6. Literally nothing to go on
	return true;
}

// needsTypeOnly — fix.go:993
bool needsTypeOnly(lsp::lsproto::AddAsTypeOnly addAsTypeOnly) {
	return addAsTypeOnly == lsp::lsproto::AddAsTypeOnlyRequired;
}

// shouldUseTypeOnly — fix.go:997
bool shouldUseTypeOnly(lsp::lsproto::AddAsTypeOnly addAsTypeOnly,
                       const lsutil::UserPreferences& preferences) {
	return needsTypeOnly(addAsTypeOnly) ||
	       (addAsTypeOnly != lsp::lsproto::AddAsTypeOnlyNotAllowed &&
	        tristateIsTrue(preferences.PreferTypeOnlyAutoImports));
}

// CompareFixesForSorting returns negative if `a` is better than `b`.
// Sorting with this comparator will place the best fix first.
// After rank sorting, fixes will be sorted by arbitrary but stable criteria
// to ensure a deterministic order.
// View::CompareFixesForSorting — fix.go:1005
int View::CompareFixesForSorting(Fix* a, Fix* b) {
	if (int res = CompareFixesForRanking(a, b); res != 0) {
		return res;
	}
	return compareModuleSpecifiersForSorting(a, b);
}

// CompareFixesForRanking returns negative if `a` is better than `b`.
// Sorting with this comparator will place the best fix first.
// Fixes of equal desirability will be considered equal.
// View::CompareFixesForRanking — fix.go:1015
int View::CompareFixesForRanking(Fix* a, Fix* b) {
	if (int res = compareFixKinds(a->AutoImportFix->Kind,
	                              b->AutoImportFix->Kind);
	    res != 0) {
		return res;
	}
	return compareModuleSpecifiersForRanking(a, b);
}

// compareFixKinds — fix.go:1022
int compareFixKinds(lsp::lsproto::AutoImportFixKind a, lsp::lsproto::AutoImportFixKind b) {
	return static_cast<int>(a) - static_cast<int>(b);
}

// View::compareModuleSpecifiersForRanking — fix.go:1026
int View::compareModuleSpecifiersForRanking(Fix* a, Fix* b) {
	if (int comparison =
	        compareModuleSpecifierRelativity(a, b, preferences);
	    comparison != 0) {
		return comparison;
	}
	if (a->ModuleSpecifierKind == modulespecifiers::ResultKind::Ambient &&
	    b->ModuleSpecifierKind == modulespecifiers::ResultKind::Ambient) {
		if (int comparison = compareNodeCoreModuleSpecifiers(
		        a->AutoImportFix->ModuleSpecifier,
		        b->AutoImportFix->ModuleSpecifier, importingFile, program);
		    comparison != 0) {
			return comparison;
		}
	}
	if (a->ModuleSpecifierKind == modulespecifiers::ResultKind::Relative &&
	    b->ModuleSpecifierKind == modulespecifiers::ResultKind::Relative) {
		if (int comparison = compareBooleans(
		        isFixPossiblyReExportingImportingFile(
		            a, importingFile->FileName()),
		        isFixPossiblyReExportingImportingFile(
		            b, importingFile->FileName()));
		    comparison != 0) {
			return comparison;
		}
	}
	if (int comparison = tspath::compareNumberOfDirectorySeparators(
	        a->AutoImportFix->ModuleSpecifier,
	        b->AutoImportFix->ModuleSpecifier);
	    comparison != 0) {
		return comparison;
	}
	return 0;
}

// View::compareModuleSpecifiersForSorting — fix.go:1049
int View::compareModuleSpecifiersForSorting(Fix* a, Fix* b) {
	if (int res = compareModuleSpecifiersForRanking(a, b); res != 0) {
		return res;
	}
	// Sort ./foo before ../foo for equal-length specifiers
	if (a->AutoImportFix->ModuleSpecifier.starts_with("./") &&
	    !b->AutoImportFix->ModuleSpecifier.starts_with("./")) {
		return -1;
	}
	if (b->AutoImportFix->ModuleSpecifier.starts_with("./") &&
	    !a->AutoImportFix->ModuleSpecifier.starts_with("./")) {
		return 1;
	}
	if (int comparison =
	        a->AutoImportFix->ModuleSpecifier.compare(
	            b->AutoImportFix->ModuleSpecifier);
	    comparison != 0) {
		return comparison;
	}
	if (int comparison =
	        static_cast<int>(a->AutoImportFix->ImportKind) -
	        static_cast<int>(b->AutoImportFix->ImportKind);
	    comparison != 0) {
		return comparison;
	}
	// !!! further tie-breakers? In practice this is only called on fixes with the same name
	return 0;
}

// View::compareNodeCoreModuleSpecifiers — fix.go:1070
int View::compareNodeCoreModuleSpecifiers(const std::string& a,
                                          const std::string& b,
                                          SourceFile* importingFile,
                                          compiler::SimpleProgram* program) {
	(void)importingFile;
	(void)program;
	if (a.starts_with("node:") && !b.starts_with("node:")) {
		if (tristateIsTrue(shouldUseUriStyleNodeCoreModules)) {
			return -1;
		} else if (tristateIsFalse(shouldUseUriStyleNodeCoreModules)) {
			return 1;
		}
		return 0;
	}
	if (b.starts_with("node:") && !a.starts_with("node:")) {
		if (tristateIsTrue(shouldUseUriStyleNodeCoreModules)) {
			return 1;
		} else if (tristateIsFalse(shouldUseUriStyleNodeCoreModules)) {
			return -1;
		}
	}
	return 0;
}

// isFixPossiblyReExportingImportingFile — fix.go:1094.
// This is a simple heuristic to try to avoid creating an import cycle with a barrel re-export.
// E.g., do not `import { Foo } from ".."` when you could `import { Foo } from "../Foo"`.
// This can produce false positives or negatives if re-exports cross into sibling directories
// (e.g. `export * from "../whatever"`) or are not named "index". Technically this should do
// a tspath.Path comparison, but it's not worth it to run a heuristic in such a hot path.
bool isFixPossiblyReExportingImportingFile(
    Fix* fix, const std::string& importingFileName) {
	if (fix->IsReExport && isIndexFileName(fix->ModuleFileName)) {
		std::string reExportDir =
		    tspath::getDirectoryPath(fix->ModuleFileName);
		return importingFileName.starts_with(
		    tspath::ensureTrailingDirectorySeparator(reExportDir));
	}
	return false;
}

// isIndexFileName — fix.go:1102
bool isIndexFileName(const std::string& fileName) {
	size_t lastSlash = fileName.find_last_of('/');
	if (lastSlash == std::string::npos || fileName.size() <= lastSlash + 1) {
		return false;
	}
	std::string_view base = std::string_view(fileName).substr(lastSlash + 1);
	return base == "index.js" || base == "index.jsx" || base == "index.d.ts" ||
	       base == "index.ts" || base == "index.tsx";
}

// promoteFromTypeOnly — fix.go:1115
Node* promoteFromTypeOnly(change::Tracker* changes, Node* aliasDeclaration,
                          const CompilerOptions* compilerOptions,
                          SourceFile* sourceFile,
                          const lsutil::UserPreferences& preferences) {
	// See comment in `doAddExistingFix` on constant with the same name.
	Tristate convertExistingToTypeOnly = compilerOptions->VerbatimModuleSyntax;

	switch (aliasDeclaration->kind) {
		case Kind::ImportSpecifier: {
			ImportSpecifier* spec = aliasDeclaration->as<ImportSpecifier>();
			if (spec->IsTypeOnly) {
				if (spec->parent != nullptr &&
				    spec->parent->kind == Kind::NamedImports) {
					NamedImports* namedImportsNode =
					    spec->parent->as<NamedImports>();
					auto& elements = namedImportsNode->Elements->nodes;
					if (elements.size() > 1) {
						// Create a synthetic specifier with isTypeOnly=false to compute sorted position
						Node* propertyName = nullptr;
						if (spec->PropertyName != nullptr) {
							propertyName =
							    changes->nodeFactory
							        ->newIdentifier(
							            spec->PropertyName->text())
							        ->as<Identifier>()
							        ->asNode();
						}
						Node* newSpecifier =
						    changes->nodeFactory->newImportSpecifier(
						        false, // isTypeOnly = false
						        propertyName,
						        changes->nodeFactory->newIdentifier(
						            spec->name->text()));
						auto specCmp = lsutil::
						    GetNamedImportSpecifierComparerWithDetection(
						        spec->parent->parent->parent, // ImportDeclaration
						        sourceFile, preferences);
						auto& specifierComparer = specCmp.first;
						int insertionIndex =
						    lsutil::GetImportSpecifierInsertionIndex(
						        elements, newSpecifier,
						        specifierComparer);
						auto it = std::find(elements.begin(),
						                    elements.end(),
						                    aliasDeclaration);
						int currentIndex =
						    it == elements.end()
						        ? -1
						        : static_cast<int>(
						              it - elements.begin());
						if (insertionIndex != currentIndex) {
							changes->Delete(sourceFile, aliasDeclaration);
							changes->InsertImportSpecifierAtIndex(
							    sourceFile, newSpecifier, spec->parent,
							    insertionIndex);
							return aliasDeclaration;
						}
					}
					// If no re-sorting needed, just remove the 'type' keyword
					Node* firstToken = lsutil::GetFirstToken(
					    aliasDeclaration, sourceFile);
					int typeKeywordPos = getTokenPosOfNode(
					    firstToken, sourceFile, false);
					Node* targetNode;
					if (spec->PropertyName != nullptr) {
						targetNode = spec->PropertyName;
					} else {
						targetNode = spec->name;
					}
					int targetPos = getTokenPosOfNode(
					    targetNode->asNode(), sourceFile, false);
					changes->DeleteRange(
					    sourceFile,
					    TextRange{TextPos(typeKeywordPos),
					              TextPos(targetPos)});
				}
				return aliasDeclaration;
			} else {
				// The parent import clause is type-only
				if (spec->parent == nullptr ||
				    spec->parent->kind != Kind::NamedImports) {
					TSC_UNREACHABLE(
					    "ImportSpecifier parent must be NamedImports");
				}
				if (spec->parent->parent == nullptr ||
				    spec->parent->parent->kind != Kind::ImportClause) {
					TSC_UNREACHABLE(
					    "NamedImports parent must be ImportClause");
				}
				promoteImportClause(
				    changes,
				    spec->parent->parent->as<ImportClause>(),
				    compilerOptions, sourceFile, preferences,
				    convertExistingToTypeOnly, aliasDeclaration);
				return spec->parent->parent;
			}
		}

		case Kind::ImportClause:
			promoteImportClause(changes,
			                    aliasDeclaration->as<ImportClause>(),
			                    compilerOptions, sourceFile, preferences,
			                    convertExistingToTypeOnly, aliasDeclaration);
			return aliasDeclaration;

		case Kind::NamespaceImport:
			// Promote the parent import clause
			if (aliasDeclaration->parent == nullptr ||
			    aliasDeclaration->parent->kind != Kind::ImportClause) {
				TSC_UNREACHABLE("NamespaceImport parent must be ImportClause");
			}
			promoteImportClause(changes,
			                    aliasDeclaration->parent->as<ImportClause>(),
			                    compilerOptions, sourceFile, preferences,
			                    convertExistingToTypeOnly, aliasDeclaration);
			return aliasDeclaration->parent;

		case Kind::ImportEqualsDeclaration: {
			// Remove the 'type' keyword (which is the second token: 'import' 'type' name '=' ...)
			ImportEqualsDeclaration* importEqDecl =
			    aliasDeclaration->as<ImportEqualsDeclaration>();
			// The type keyword is after 'import' and before the name
			Scanner scanner;
			getScannerForSourceFile(scanner, sourceFile,
			                        importEqDecl->pos());
			// Skip 'import' keyword to get to 'type'
			scanner.scan();
			deleteTypeKeyword(changes, sourceFile, scanner.tokenStart());
			return aliasDeclaration;
		}
		default:
			TSC_UNREACHABLE("Unexpected alias declaration kind");
	}
}

// promoteImportClause — fix.go:1208. Removes the type keyword from an import
// clause.
void promoteImportClause(change::Tracker* changes, ImportClause* importClause,
                         const CompilerOptions* compilerOptions,
                         SourceFile* sourceFile,
                         const lsutil::UserPreferences& preferences,
                         Tristate convertExistingToTypeOnly,
                         Node* aliasDeclaration) {
	// Delete the 'type' keyword
	if (importClause->PhaseModifier == Kind::TypeKeyword) {
		deleteTypeKeyword(changes, sourceFile, importClause->pos());
	}

	// Handle .ts extension conversion to .js if necessary
	if (tristateIsFalse(compilerOptions->AllowImportingTsExtensions)) {
		Node* moduleSpecifier = checker::tryGetModuleSpecifierFromDeclaration(
		    importClause->parent);
		if (moduleSpecifier != nullptr) {
			// Note: We can't check ResolvedUsingTsExtension without program, so we'll skip this optimization
			// The fix will still work, just might not change .ts to .js extensions in all cases
		}
	}

	// Handle verbatimModuleSyntax conversion
	// If convertExistingToTypeOnly is true, we need to add 'type' to other specifiers
	// in the same import declaration
	if (tristateIsTrue(convertExistingToTypeOnly)) {
		Node* namedImports = importClause->NamedBindings;
		if (namedImports != nullptr &&
		    namedImports->kind == Kind::NamedImports) {
			NamedImports* namedImportsData =
			    namedImports->as<NamedImports>();
			if (namedImportsData->Elements->nodes.size() > 1) {
				// Check if the list is sorted and if we need to reorder
				auto sortCmp = lsutil::
				    GetNamedImportSpecifierComparerWithDetection(
				        importClause->parent, sourceFile, preferences);
				auto& isSorted = sortCmp.second;

				// If the alias declaration is an ImportSpecifier and the list is sorted,
				// move it to index 0 (since it will be the only non-type-only import)
				if (!tristateIsFalse(isSorted) && // isSorted !== false
				    aliasDeclaration != nullptr &&
				    aliasDeclaration->kind == Kind::ImportSpecifier) {
					// Find the index of the alias declaration
					int aliasIndex = -1;
					for (size_t i = 0;
					     i < namedImportsData->Elements->nodes.size();
					     i++) {
						if (namedImportsData->Elements->nodes[i] ==
						    aliasDeclaration) {
							aliasIndex = static_cast<int>(i);
							break;
						}
					}
					// If not already at index 0, move it there
					if (aliasIndex > 0) {
						// Delete the specifier from its current position
						changes->Delete(sourceFile, aliasDeclaration);
						// Insert it at index 0
						changes->InsertImportSpecifierAtIndex(
						    sourceFile, aliasDeclaration, namedImports, 0);
					}
				}

				// Add 'type' keyword to all other import specifiers that aren't already type-only
				for (Node* element : namedImportsData->Elements->nodes) {
					ImportSpecifier* spec = element->as<ImportSpecifier>();
					// Skip the specifier being promoted (if aliasDeclaration is an ImportSpecifier)
					if (aliasDeclaration != nullptr &&
					    aliasDeclaration->kind == Kind::ImportSpecifier) {
						if (element == aliasDeclaration) {
							continue;
						}
					}
					// Skip if already type-only
					if (!spec->IsTypeOnly) {
						changes->InsertModifierBefore(
						    sourceFile, Kind::TypeKeyword, element);
					}
				}
			}
		}
	}
}

// deleteTypeKeyword — fix.go:1289. Deletes the 'type' keyword token starting at
// the given position, including any trailing whitespace.
void deleteTypeKeyword(change::Tracker* changes, SourceFile* sourceFile,
                       int startPos) {
	Scanner scanner;
	getScannerForSourceFile(scanner, sourceFile, startPos);
	if (scanner.token() != Kind::TypeKeyword) {
		return;
	}
	int typeStart = scanner.tokenStart();
	int typeEnd = scanner.tokenEnd();
	// Skip trailing whitespace
	const std::string& text = sourceFile->Text();
	while (typeEnd < static_cast<int>(text.size()) &&
	       (text[typeEnd] == ' ' || text[typeEnd] == '\t')) {
		typeEnd++;
	}
	changes->DeleteRange(
	    sourceFile, TextRange{TextPos(typeStart), TextPos(typeEnd)});
}

// getModuleSpecifierText — fix.go:1304
std::string getModuleSpecifierText(Node* promotedDeclaration) {
	if (promotedDeclaration->kind == Kind::ImportEqualsDeclaration) {
		ImportEqualsDeclaration* importEqualsDeclaration =
		    promotedDeclaration->as<ImportEqualsDeclaration>();
		if (isExternalModuleReference(
		        importEqualsDeclaration->ModuleReference)) {
			Node* expr =
			    importEqualsDeclaration->ModuleReference->expression();
			if (expr != nullptr) {
				if (isStringLiteralLike(expr)) {
					return expr->text();
				}
				return getTextOfNode(expr);
			}
		}
		return getTextOfNode(importEqualsDeclaration->ModuleReference);
	}
	Node* moduleSpecifier = promotedDeclaration->parent->moduleSpecifier();
	if (isStringLiteralLike(moduleSpecifier)) {
		return moduleSpecifier->text();
	}
	return getTextOfNode(moduleSpecifier);
}

// compareModuleSpecifierRelativity — fix.go:1326. Returns `-1` if `a` is
// better than `b`.
int compareModuleSpecifierRelativity(
    Fix* a, Fix* b, const modulespecifiers::UserPreferences& preferences) {
	if (preferences.ImportModuleSpecifierPreference ==
	        modulespecifiers::ImportModuleSpecifierPreferenceNonRelative ||
	    preferences.ImportModuleSpecifierPreference ==
	        modulespecifiers::ImportModuleSpecifierPreferenceProjectRelative) {
		return compareBooleans(
		    a->ModuleSpecifierKind == modulespecifiers::ResultKind::Relative,
		    b->ModuleSpecifierKind == modulespecifiers::ResultKind::Relative);
	}
	return 0;
}

}  // namespace tsc::ls::autoimport
