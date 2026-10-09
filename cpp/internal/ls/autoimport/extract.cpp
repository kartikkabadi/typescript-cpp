// extract.go — symbolExtractor/exportExtractor: symbol → Export extraction.
#include "internal/ls/autoimport/autoimport.h"

namespace tsc::ls::autoimport {
namespace {

// core.EmptyCompilerOptions — shared instance for the local NameResolver.
CompilerOptions* emptyCompilerOptions() {
	static CompilerOptions opts;
	return &opts;
}

}  // namespace

// exportExtractor::Stats — extract.go:39
extractorStats* exportExtractor::Stats() {
	return extractor->stats.get();
}

// checkerLease::GetChecker — extract.go:48
checker::Checker* checkerLease::GetChecker() {
	used = true;
	return checker;
}

// checkerLease::TryChecker — extract.go:53
checker::Checker* checkerLease::TryChecker() {
	if (used) {
		return checker;
	}
	return nullptr;
}

// newSymbolExtractor — extract.go:60
std::unique_ptr<symbolExtractor> newSymbolExtractor(
    const std::string& packageName, checker::Checker* ch,
    const std::function<tspath::Path(const std::string&)>& toPath,
    const std::function<std::string(const std::string&)>& realpath) {
	auto e = std::make_unique<symbolExtractor>();
	e->packageName = packageName;
	e->checker = ch;
	e->localNameResolver = std::make_unique<binder::NameResolver>();
	e->localNameResolver->compilerOptions = emptyCompilerOptions();
	e->stats = std::make_unique<extractorStats>();
	e->toPath = toPath;
	e->realpath = realpath;
	return e;
}

// registryBuilder::newExportExtractor — extract.go:73
std::unique_ptr<exportExtractor> registryBuilder::newExportExtractor(
    const std::string& packageName, checker::Checker* ch,
    module::DefaultResolver* moduleResolver,
    const std::function<std::string(const std::string&)>& realpath) {
	auto ee = std::make_unique<exportExtractor>();
	ee->extractor = newSymbolExtractor(packageName, ch, base->toPath, realpath);
	ee->moduleResolver = moduleResolver;
	return ee;
}

// symbolExtractor::getModuleID — extract.go:81
ModuleID symbolExtractor::getModuleID(SourceFile* file) {
	if (realpath && toPath) {
		std::string rp = realpath(file->FileName());
		return ModuleID(toPath(rp));
	}
	return ModuleID(file->Path());
}

// symbolExtractor::getModuleIDForSymbol — extract.go:91
std::pair<ModuleID, bool> symbolExtractor::getModuleIDForSymbol(
    Symbol* symbol) {
	auto res = tryGetModuleIDAndFileNameOfModuleSymbol(symbol);
	if (!res) {
		return {"", false};
	}
	ModuleID& moduleID = res->first;
	std::string& fileName = res->second;
	// If fileName is set, this is a source file that may need realpath normalization
	if (!fileName.empty() && realpath) {
		Node* decl = getNonAugmentationDeclaration(symbol);
		if (decl != nullptr && decl->kind == Kind::SourceFile) {
			return {getModuleID(decl->as<SourceFile>()), true};
		}
	}
	return {moduleID, true};
}

// exportExtractor::extractFromFile — extract.go:106
std::vector<std::shared_ptr<Export>> exportExtractor::extractFromFile(
    SourceFile* file) {
	if (file->Symbol != nullptr) {
		return extractFromModule(file);
	}
	if (!file->AmbientModuleNames.empty()) {
		int exportCount = 0;
		for (Node* statement : file->Statements->nodes) {
			ModuleDeclaration* md = statement->as<ModuleDeclaration>();
			if (isModuleWithStringLiteralName(statement) &&
			    isNonPatternAmbientModuleDeclaration(file, md)) {
				exportCount += static_cast<int>(md->Symbol->data->exports.size());
			}
		}
		std::vector<std::shared_ptr<Export>> exports;
		exports.reserve(exportCount);
		for (Node* statement : file->Statements->nodes) {
			ModuleDeclaration* md = statement->as<ModuleDeclaration>();
			if (isModuleWithStringLiteralName(statement) &&
			    isNonPatternAmbientModuleDeclaration(file, md)) {
				extractFromModuleDeclaration(md, file,
				                             ModuleID(md->name->text()),
				                             "", &exports);
			}
		}
		return exports;
	}
	return {};
}

// isNonPatternAmbientModuleDeclaration — extract.go:128
bool isNonPatternAmbientModuleDeclaration(SourceFile* file,
                                          ModuleDeclaration* decl) {
	for (PatternAmbientModule* module : file->PatternAmbientModules) {
		if (module->symbol == decl->Symbol) {
			return false;
		}
	}
	return true;
}

// exportExtractor::extractFromModule — extract.go:137
std::vector<std::shared_ptr<Export>> exportExtractor::extractFromModule(
    SourceFile* file) {
	std::vector<ModuleDeclaration*> moduleAugmentations;
	for (Node* name : file->ModuleAugmentations) {
		Node* decl = name->parent;
		if (isGlobalScopeAugmentation(decl)) {
			continue;
		}
		moduleAugmentations.push_back(decl->as<ModuleDeclaration>());
	}
	int augmentationExportCount = 0;
	for (ModuleDeclaration* decl : moduleAugmentations) {
		augmentationExportCount +=
		    static_cast<int>(decl->Symbol->data->exports.size());
	}
	ModuleID moduleID = extractor->getModuleID(file);
	std::vector<std::shared_ptr<Export>> exports;
	exports.reserve(file->Symbol->data->exports.size() + augmentationExportCount);
	for (const auto& [name, symbol] : file->Symbol->data->exports) {
		extractor->extractFromSymbol(name, symbol, moduleID, file->FileName(),
		                             file, &exports);
	}
	for (ModuleDeclaration* decl : moduleAugmentations) {
		std::string name = decl->name->as<StringLiteral>()->Text;
		ModuleID augmentationModuleID = ModuleID(name);
		std::string moduleFileName;
		if (tspath::isExternalModuleNameRelative(name)) {
			auto resolved =
			    moduleResolver
			        ->ResolveModuleName(name, file->FileName(),
			                            ModuleKind::CommonJS, nullptr)
			        .first;
			if (resolved->IsResolved()) {
				moduleFileName = resolved->ResolvedFileName;
				augmentationModuleID =
				    ModuleID(extractor->toPath(moduleFileName));
			} else {
				// :shrug:
				moduleFileName = tspath::resolvePath(
				    tspath::getDirectoryPath(file->FileName()),
				    {name});
				augmentationModuleID =
				    ModuleID(extractor->toPath(moduleFileName));
			}
		}
		extractFromModuleDeclaration(decl, file, augmentationModuleID,
		                             moduleFileName, &exports);
	}
	return exports;
}

// exportExtractor::extractFromModuleDeclaration — extract.go:173
void exportExtractor::extractFromModuleDeclaration(
    ModuleDeclaration* decl, SourceFile* file, const ModuleID& moduleID,
    const std::string& moduleFileName,
    std::vector<std::shared_ptr<Export>>* exports) {
	for (const auto& [name, symbol] : decl->Symbol->data->exports) {
		extractor->extractFromSymbol(name, symbol, moduleID, moduleFileName,
		                             file, exports);
	}
}

// symbolExtractor::extractFromSymbol — extract.go:179
void symbolExtractor::extractFromSymbol(
    const std::string& name, Symbol* symbol, const ModuleID& moduleID,
    const std::string& moduleFileName, SourceFile* file,
    std::vector<std::shared_ptr<Export>>* exports) {
	if (shouldIgnoreSymbol(symbol)) {
		return;
	}

	if (name == InternalSymbolNameExportStar) {
		checkerLease lease{/*.used =*/ false, /*.checker =*/ checker};
		std::vector<Symbol*> allExports = checker->GetExportsOfModule(
		    symbol->data->parent);
		// allExports includes named exports from the file that will be processed separately;
		// we want to add only the ones that come from the star
		for (const auto& [namedName, namedExport] : symbol->data->parent->data->exports) {
			if (namedName != InternalSymbolNameExportStar) {
				auto it = std::find(allExports.begin(), allExports.end(),
				                    namedExport);
				if (it != allExports.end() ||
				    shouldIgnoreSymbol(namedExport)) {
					// slices.Delete(allExports, idx, idx+1) — Go panics on
					// idx == -1 here; TSC_UNREACHABLE matches.
					if (it == allExports.end()) {
						TSC_UNREACHABLE("slices.Delete: index out of range");
					}
					allExports.erase(it);
				}
			}
		}

		exports->reserve(exports->size() + allExports.size());
		for (Symbol* reexportedSymbol : allExports) {
			auto [export_, _t] =
			    createExport(reexportedSymbol, moduleID, moduleFileName,
			                 ExportSyntax::Star, file, &lease);
			if (export_ != nullptr) {
				Symbol* parent = lease.GetChecker()->GetMergedSymbol(
				    reexportedSymbol->data->parent);
				if (parent != nullptr && parent->isExternalModule()) {
					if (auto [targetModuleID, ok] =
					        getModuleIDForSymbol(parent);
					    ok) {
						export_->Target = ExportID{
						    targetModuleID,
						    reexportedSymbol->data->name,
						};
					}
				}
				export_->through = InternalSymbolNameExportStar;
				exports->push_back(std::move(export_));
			}
		}
		return;
	}

	ExportSyntax syntax = getSyntax(symbol);
	checkerLease lease{/*.used =*/ false, /*.checker =*/ checker};
	auto [export_, target] =
	    createExport(symbol, moduleID, moduleFileName, syntax, file, &lease);
	if (export_ == nullptr) {
		return;
	}

	exports->push_back(export_);

	if (target != nullptr) {
		if (syntax == ExportSyntax::Equals &&
		    (target->flags & SymbolFlagsNamespace) != 0) {
			exports->reserve(exports->size() + target->data->exports.size());
			for (const auto& [innerName, namedExport] : target->data->exports) {
				if (innerName != InternalSymbolNameExportStar) {
					auto [innerExport, _t] =
					    createExport(namedExport, moduleID, moduleFileName,
					                 syntax, file, &lease);
					if (innerExport != nullptr) {
						innerExport->through = name;
						exports->push_back(std::move(innerExport));
					}
				}
			}
		}
	} else if (syntax == ExportSyntax::CommonJSModuleExports) {
		Node* expression =
		    symbol->data->declarations[0]->as<BinaryExpression>()->Right;
		if (expression->kind == Kind::ObjectLiteralExpression) {
			NodeList* properties =
			    expression->as<ObjectLiteralExpression>()->Properties;
			exports->reserve(exports->size() + properties->nodes.size());
			auto propName = [](Node* prop) -> Node* {
				if (isPropertyAssignment(prop)) {
					return prop->as<PropertyAssignment>()->name;
				}
				return prop->as<ShorthandPropertyAssignment>()->name;
			};
			for (Node* prop : properties->nodes) {
				bool isProp = isShorthandPropertyAssignment(prop) ||
				              (isPropertyAssignment(prop) &&
				               propName(prop)->kind == Kind::Identifier);
				if (isProp) {
					Symbol* member = getSymbolFromTable(
					    expression->symbol()->data->members, propName(prop)->text());
					auto [propExport, _t] =
					    createExport(member, moduleID, moduleFileName, syntax,
					                 file, &lease);
					if (propExport != nullptr) {
						propExport->through = name;
						exports->push_back(std::move(propExport));
					}
				}
			}
		}
	}
}

// symbolExtractor::createExport — extract.go:261
std::pair<std::shared_ptr<Export>, Symbol*> symbolExtractor::createExport(
    Symbol* symbol, const ModuleID& moduleID,
    const std::string& moduleFileName, ExportSyntax syntax, SourceFile* file,
    checkerLease* lease) {
	if (shouldIgnoreSymbol(symbol)) {
		return {nullptr, nullptr};
	}

	auto export_ = std::make_shared<Export>();
	export_->exportID.ExportName = symbol->data->name;
	export_->exportID.ModuleID = moduleID;
	export_->ModuleFileName = moduleFileName;
	export_->Syntax = syntax;
	export_->Flags = symbol->combinedLocalAndExportSymbolFlags();
	export_->Path = file->Path();
	export_->PackageName = packageName;

	if (syntax == ExportSyntax::UMD) {
		export_->exportID.ExportName = InternalSymbolNameExportEquals;
		export_->localName = symbol->data->name;
	}

	Symbol* targetSymbol = nullptr;
	if ((symbol->flags & SymbolFlagsAlias) != 0) {
		targetSymbol = tryResolveSymbol(symbol, syntax, lease);
		if (targetSymbol != nullptr) {
			Node* decl = nullptr;
			if (!targetSymbol->data->declarations.empty()) {
				decl = targetSymbol->data->declarations[0];
			} else if ((targetSymbol->checkFlags & CheckFlagsMapped) != 0) {
				Symbol* mappedDecl =
				    lease->GetChecker()->GetMappedTypeSymbolOfProperty(
				        targetSymbol);
				if (mappedDecl != nullptr &&
				    !mappedDecl->data->declarations.empty()) {
					decl = mappedDecl->data->declarations[0];
				}
			}
			if (decl == nullptr) {
				// !!! consider GetImmediateAliasedSymbol to go as far as we can
				decl = symbol->data->declarations[0];
			}
			if (decl == nullptr) {
				TSC_UNREACHABLE("no declaration for aliased symbol");
			}

			Symbol* parent = targetSymbol->data->parent;
			checker::Checker* ch = lease->TryChecker();
			if (ch != nullptr) {
				export_->Flags = ch->GetSymbolFlags(targetSymbol);
				export_->IsTypeOnly =
				    ch->GetTypeOnlyAliasDeclaration(symbol) != nullptr;
				parent = ch->GetMergedSymbol(parent);
			} else {
				export_->Flags = targetSymbol->flags;
				// core.Some(symbol.Declarations, IsPartOfTypeOnly...)
				export_->IsTypeOnly =
				    std::any_of(symbol->data->declarations.begin(),
				                symbol->data->declarations.end(),
				                isPartOfTypeOnlyImportOrExportDeclaration);
			}
			export_->ScriptElementKind =
			    lsutil::GetSymbolKind(lease->TryChecker(), targetSymbol, decl);
			export_->ScriptElementKindModifiers =
			    lsutil::GetSymbolModifiers(lease->TryChecker(), targetSymbol);
			ModuleID targetModuleID =
			    ModuleID(getSourceFileOfNode(decl)->Path());
			if (parent != nullptr && parent->isExternalModule()) {
				if (auto [id, ok] = getModuleIDForSymbol(parent); ok) {
					targetModuleID = id;
				}
			}
			export_->Target = ExportID{
			    targetModuleID,
			    targetSymbol->data->name,
			};
		}
	} else {
		export_->ScriptElementKind =
		    lsutil::GetSymbolKind(lease->TryChecker(), symbol,
		                          symbol->data->declarations[0]);
		export_->ScriptElementKindModifiers =
		    lsutil::GetSymbolModifiers(lease->TryChecker(), symbol);
	}

	if (symbol->data->name == InternalSymbolNameDefault ||
	    symbol->data->name == InternalSymbolNameExportEquals) {
		Symbol* namedSymbol = symbol;
		if (Symbol* s = binder::getLocalSymbolForExportDefault(symbol)) {
			namedSymbol = s;
		}
		export_->localName =
		    getDefaultLikeExportNameFromDeclaration(namedSymbol);
		if (isUnusableName(export_->localName)) {
			export_->localName = export_->Target.ExportName;
		}
		if (isUnusableName(export_->localName)) {
			if (targetSymbol != nullptr) {
				namedSymbol = targetSymbol;
				if (Symbol* s =
				        binder::getLocalSymbolForExportDefault(targetSymbol)) {
					namedSymbol = s;
				}
				export_->localName =
				    getDefaultLikeExportNameFromDeclaration(namedSymbol);
			}
		}
		if (isUnusableName(export_->localName)) {
			// Last resort: derive identifier from the file name.
			export_->localName = lsutil::ModuleSpecifierToValidIdentifier(
			    fileNameForDefaultExportName(targetSymbol, moduleFileName,
			                                 moduleID),
			    false);
		}
	}

	if (isUnusableName(export_->Name())) {
		return {nullptr, nullptr};
	}

	stats->exports.fetch_add(1);
	if (lease->TryChecker() != nullptr) {
		stats->usedChecker.fetch_add(1);
	}

	return {export_, targetSymbol};
}

// symbolExtractor::tryResolveSymbol — extract.go:366
Symbol* symbolExtractor::tryResolveSymbol(Symbol* symbol, ExportSyntax syntax,
                                          checkerLease* lease) {
	if (!isNonLocalAlias(symbol, SymbolFlagsNone)) {
		return symbol;
	}

	Node* loc = nullptr;
	std::string name;
	switch (syntax) {
		case ExportSyntax::Named: {
			Node* decl =
			    getDeclarationOfKind(symbol, Kind::ExportSpecifier);
			if (decl->parent->parent->as<ExportDeclaration>()
			        ->ModuleSpecifier == nullptr) {
				// core.FirstNonZero(decl.Name(), decl.PropertyName())
				Node* n = decl->as<ExportSpecifier>()->name;
				if (n == nullptr) {
					n = decl->as<ExportSpecifier>()->PropertyName;
				}
				if (n != nullptr && n->kind == Kind::Identifier) {
					loc = n;
					name = n->text();
				}
			}
			break;
		}
		// !!! check if module.exports = foo is marked as an alias
		case ExportSyntax::Equals:
			if (symbol->data->name != InternalSymbolNameExportEquals) {
				break;
			}
			[[fallthrough]];
		case ExportSyntax::DefaultDeclaration: {
			Node* decl =
			    getDeclarationOfKind(symbol, Kind::ExportAssignment);
			if (decl->as<ExportAssignment>()->Expression->kind ==
			    Kind::Identifier) {
				loc = decl->as<ExportAssignment>()->Expression;
				name = loc->text();
			}
			break;
		}
		default:
			break;
	}

	if (loc != nullptr) {
		Symbol* local = localNameResolver->resolve(
		    loc, name, SymbolFlagsAll, nullptr, false, false);
		if (local != nullptr &&
		    !isNonLocalAlias(local, SymbolFlagsNone)) {
			return local;
		}
	}

	checker::Checker* ch = lease->GetChecker();
	if (Symbol* resolved = ch->GetAliasedSymbol(symbol);
	    !ch->IsUnknownSymbol(resolved)) {
		return resolved;
	}
	return nullptr;
}

// shouldIgnoreSymbol — extract.go:410
bool shouldIgnoreSymbol(Symbol* symbol) {
	if ((symbol->flags & SymbolFlagsPrototype) != 0) {
		return true;
	}
	return false;
}

// getSyntax — extract.go:417
ExportSyntax getSyntax(Symbol* symbol) {
	for (Node* decl : symbol->data->declarations) {
		switch (decl->kind) {
			case Kind::ExportSpecifier:
				return ExportSyntax::Named;
			case Kind::ExportAssignment:
				return decl->as<ExportAssignment>()->IsExportEquals
				           ? ExportSyntax::Equals
				           : ExportSyntax::DefaultDeclaration;
			case Kind::NamespaceExportDeclaration:
				return ExportSyntax::UMD;
			case Kind::BinaryExpression:
				switch (getAssignmentDeclarationKind(decl)) {
					case JSDeclarationKind::ModuleExports:
						return ExportSyntax::CommonJSModuleExports;
					case JSDeclarationKind::ExportsProperty:
						return ExportSyntax::CommonJSExportsProperty;
					default:
						break;
				}
				break;
			default:
				if ((getCombinedModifierFlags(decl) &
				     ModifierFlagsDefault) != 0) {
					return ExportSyntax::DefaultModifier;
				} else {
					return ExportSyntax::Modifier;
				}
		}
	}
	return ExportSyntax::None;
}

// isUnusableName — extract.go:448
bool isUnusableName(const std::string& name) {
	return name.empty() || name == "_default" ||
	       name == InternalSymbolNameExportStar ||
	       name == InternalSymbolNameDefault ||
	       name == InternalSymbolNameExportEquals;
}

// fileNameForDefaultExportName — extract.go:461
std::string fileNameForDefaultExportName(Symbol* targetSymbol,
                                         const std::string& moduleFileName,
                                         const ModuleID& moduleID) {
	if (targetSymbol != nullptr && !targetSymbol->data->declarations.empty()) {
		if (std::string fn = getSourceFileOfNode(targetSymbol->data->declarations[0])
		                         ->FileName();
		    !fn.empty()) {
			return fn;
		}
	}
	if (!moduleFileName.empty()) {
		return moduleFileName;
	}
	return moduleID;
}

}  // namespace tsc::ls::autoimport
