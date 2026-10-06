// === slice: ls-coreC ===
// sourcedefinition.cpp — sourcedefinition.go: resolve .d.ts declarations back
// to implementation files via the NoDts resolver.
#include "internal/ls/ls.h"

#include "internal/modulespecifiers/types.h"
#include "internal/binder/binder.h"

namespace tsc::ls {

// ============================================================================
// sourcedefinition.go — sourceDefResolver (struct def + methods)
// ============================================================================
// sourcedefinition.go:132
// core.go:829 — core.Deduplicate (order-preserving unique).
std::vector<std::string> deduplicate(std::vector<std::string> slice) {
	if (slice.size() <= 1) {
		return slice;
	}
	std::vector<std::string> result;
	result.reserve(slice.size());
	for (auto& v : slice) {
		if (std::find(result.begin(), result.end(), v) == result.end()) {
			result.push_back(v);
		}
	}
	return result;
}

struct sourceDefResolver {
	LanguageService* ls = nullptr;
	std::shared_ptr<vfs::FS> fs;
	const CompilerOptions* options = nullptr;
	std::function<SourceFile*(const std::string&)> getSourceFile;
	std::string resolveFrom;
	std::unique_ptr<module::DefaultResolver> resolver;
	std::unordered_map<std::string, SourceFile*> parsedFiles;
	CompilerOptions noDtsOptions; // clone of *options with NoDtsResolution set

	std::vector<::tsc::Node*> resolveFromCheckerInfo(
		::tsc::Node* node, std::string resolvedImplFile,
		const std::vector<::tsc::Node*>& checkerDeclarations,
		const std::string& moduleSpecifier);
	std::pair<std::vector<::tsc::Node*>, FileReference*>
	resolveTripleSlashReference(SourceFile* file, int pos,
								compiler::SimpleProgram* program);
	std::vector<::tsc::Node*> searchImplementationFile(
		::tsc::Node* originalNode, const std::string& implementationFile,
		const std::vector<std::string>& names);
	std::vector<::tsc::Node*> mapDeclarationToSource(
		::tsc::Node* originalNode, ::tsc::Node* declaration,
		const std::string& resolvedImplFile);
	std::string findImplementationFileFromDtsFileName(
		const std::string& dtsFileName, ResolutionMode preferredMode);
	std::string resolveImplementation(const std::string& moduleName,
									  ResolutionMode preferredMode);
	std::string resolveImplementationFrom(const std::string& moduleName,
										  const std::string& resolveFromFile,
										  ResolutionMode preferredMode);
	SourceFile* getOrParseSourceFile(const std::string& fileName);
	ResolutionMode inferImpliedNodeFormat(const std::string& fileName);
	std::vector<::tsc::Node*> findDeclarationsInFile(
		const std::string& fileName, const std::vector<std::string>& names,
		collections::Set<std::string>* seen);
	std::vector<std::string> getForwardedImplementationFiles(
		SourceFile* sourceFile);
};

namespace {

// utilities.go:2592 — isDefaultImport (inlined; not yet ported in ast)
bool isDefaultImport_(::tsc::Node* node) {
	if (node->kind == Kind::ImportDeclaration ||
		node->kind == Kind::JSImportDeclaration) {
		::tsc::Node* importClause = node->importClause();
		return importClause != nullptr &&
			   importClause->as<ImportClause>()->name != nullptr;
	}
	return false;
}

// sourcedefinition.go:317
bool isDefaultImportName(::tsc::Node* node) {
	if (node == nullptr || node->parent == nullptr ||
		!isImportClause(node->parent) || node->parent->name() != node ||
		node->parent->parent == nullptr) {
		return false;
	}
	return isDefaultImport_(node->parent->parent);
}

::tsc::Node* getSourceDefinitionEntryNode(SourceFile* sourceFile) {
	// sourcedefinition.go:324
	if (!sourceFile->Statements->nodes.empty()) {
		return sourceFile->Statements->nodes.front();
	}
	return sourceFile;
}

std::vector<::tsc::Node*> getSourceDefinitionEntryDeclarations(
	SourceFile* sourceFile) {
	// sourcedefinition.go:331
	return {getSourceDefinitionEntryNode(sourceFile)};
}

// sourcedefinition.go:475
::tsc::Node* findContainingModuleSpecifier(::tsc::Node* node) {
	for (::tsc::Node* current = node; current != nullptr;
		 current = current->parent) {
		if (isAnyImportOrReExport(current) ||
			isRequireCall(current,
							   true /*requireStringLiteralLikeArgument*/) ||
			isImportCall(current)) {
			if (::tsc::Node* moduleSpecifier =
					getExternalModuleName(current);
				moduleSpecifier != nullptr &&
				isStringLiteralLike(moduleSpecifier)) {
				return moduleSpecifier;
			}
		}
	}
	return nullptr;
}

// sourcedefinition.go:534
std::vector<std::string> getCandidateSourceDeclarationNames(
	::tsc::Node* originalNode, ::tsc::Node* declaration) {
	std::vector<std::string> names;
	if (declaration != nullptr) {
		if (::tsc::Node* name = getNameOfDeclaration(declaration);
			name != nullptr) {
			if (std::string text = getTextOfPropertyName(name);
				!text.empty()) {
				names.push_back(text);
			}
		}
		if (declaration->kind == Kind::ExportAssignment) {
			names.push_back("default");
		}
		if ((isFunctionDeclaration(declaration) ||
			 isClassDeclaration(declaration)) &&
			(declaration->modifierFlags() & ModifierFlagsExportDefault) ==
				ModifierFlagsExportDefault) {
			names.push_back("default");
		}
		if (isImportSpecifier(declaration) ||
			isExportSpecifier(declaration)) {
			if (::tsc::Node* propName = declaration->propertyName();
				propName != nullptr) {
				names.push_back(propName->text());
			}
		}
	}
	if (originalNode != nullptr) {
		if (isIdentifier(originalNode) ||
			isPrivateIdentifier(originalNode)) {
			names.push_back(originalNode->text());
		}
		if (isDefaultImportName(originalNode)) {
			names.push_back("default");
		}
		if (originalNode->parent != nullptr) {
			if (isImportSpecifier(originalNode->parent) ||
				isExportSpecifier(originalNode->parent)) {
				if (::tsc::Node* propName = originalNode->parent->propertyName();
					propName != nullptr) {
					names.push_back(propName->text());
				}
			}
		}
	}
	return names;
}

// sourcedefinition.go:683
bool isConcreteSourceDeclaration(::tsc::Node* node) {
	if (!isDeclaration(node) || node->kind == Kind::ExportAssignment) {
		return false;
	}
	if ((isBinaryExpression(node) || isCallExpression(node)) &&
		getAssignmentDeclarationKind(node) != JSDeclarationKind::None) {
		return false;
	}
	switch (node->kind) {
	case Kind::Parameter:
	case Kind::TypeParameter:
	case Kind::BindingElement:
	case Kind::ImportClause:
	case Kind::ImportSpecifier:
	case Kind::NamespaceImport:
	case Kind::ExportSpecifier:
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
		return false;
	default:
		return true;
	}
}

// sourcedefinition.go:679 — slices.ContainsFunc(declarations, isConcreteSourceDeclaration)
bool hasConcreteSourceDeclarations(
	const std::vector<::tsc::Node*>& declarations) {
	for (auto* d : declarations) {
		if (isConcreteSourceDeclaration(d)) {
			return true;
		}
	}
	return false;
}

// sourcedefinition.go:706
struct declarationKey {
	std::string fileName;
	TextRange loc;
	bool operator==(const declarationKey&) const = default;
};

struct declarationKeyHash {
	size_t operator()(const declarationKey& k) const {
		return std::hash<std::string>{}(k.fileName) * 131 +
			   std::hash<int64_t>{}(k.loc.pos()) * 8191 +
			   std::hash<int64_t>{}(k.loc.end());
	}
};

std::vector<::tsc::Node*> uniqueDeclarationNodes(
	const std::vector<::tsc::Node*>& nodes) {
	std::unordered_set<declarationKey, declarationKeyHash> seen;
	std::vector<::tsc::Node*> result;
	result.reserve(nodes.size());
	for (auto* node : nodes) {
		if (node == nullptr) {
			continue;
		}
		std::string fileName = getSourceFileOfNode(node)->FileName();
		declarationKey key{fileName, node->posEnd()};
		if (!seen.insert(key).second) {
			continue;
		}
		result.push_back(node);
	}
	return result;
}

// sourcedefinition.go:632 — getContainerDepth
int getContainerDepth(::tsc::Node* node) {
	int depth = 0;
	::tsc::Node* current = node;
	while (current != nullptr) {
		current = getContainerNode(current);
		depth++;
	}
	return depth;
}

// sourcedefinition.go:572
std::vector<::tsc::Node*> findDeclarationNodesByName(
	SourceFile* sourceFile, std::vector<std::string> names) {
	std::vector<std::string> filtered;
	for (auto& name : names) {
		if (!name.empty()) {
			filtered.push_back(name);
		}
	}
	names = deduplicate(std::move(filtered));
	if (names.empty()) {
		return {};
	}

	collections::Set<std::string> wanted;
	bool wantDefault = false;
	for (auto& name : names) {
		if (name == "default") {
			wantDefault = true;
			continue;
		}
		wanted.Add(name);
	}

	struct candidate {
		::tsc::Node* node;
		int depth;
	};
	std::vector<candidate> candidates;
	int minDepth = std::numeric_limits<int>::max();

	auto visit = [&](::tsc::Node* node, int depth) -> bool {
		bool matched = false;
		if (::tsc::Node* name = getNameOfDeclaration(node); name != nullptr) {
			if (std::string text = getTextOfPropertyName(name);
				!text.empty()) {
				if (wanted.Has(text)) {
					matched = true;
				}
			}
		}
		if (wantDefault && node->kind == Kind::ExportAssignment) {
			matched = true;
		}
		if (wantDefault &&
			(isFunctionDeclaration(node) ||
			 isClassDeclaration(node)) &&
			(node->modifierFlags() & ModifierFlagsExportDefault) ==
				ModifierFlagsExportDefault) {
			matched = true;
		}
		if (matched) {
			int d = getContainerDepth(node);
			candidates.push_back(candidate{node, d});
			if (d < minDepth) {
				minDepth = d;
			}
		}
		return true; // recurse — handled below via node->forEachChild
	};

	// Go uses a Visitor that both tests and recurses; emulate with an explicit
	// recursive walk so `matched` bookkeeping stays inside visit().
	std::function<void(::tsc::Node*, int)> walk = [&](::tsc::Node* node,
													int depth) {
		visit(node, depth);
		node->forEachChild([&](::tsc::Node* child) {
			walk(child, depth + 1);
			return false;
		});
	};
	walk(sourceFile, 0);

	// Only keep declarations at the shallowest depth, like getTopMostDeclarationNamesInFile.
	std::vector<::tsc::Node*> declarations;
	for (auto& c : candidates) {
		if (c.depth == minDepth) {
			declarations.push_back(c.node);
		}
	}
	return uniqueDeclarationNodes(declarations);
}

// sourcedefinition.go:727
::tsc::Node* findClosestDeclarationNode(SourceFile* sourceFile, int pos) {
	::tsc::Node* node = astnav::getTouchingPropertyName(sourceFile, pos);
	for (::tsc::Node* current = node; current != nullptr;
		 current = current->parent) {
		if (isDeclaration(current) ||
			current->kind == Kind::ExportAssignment) {
			return current;
		}
	}
	return getSourceDefinitionEntryNode(sourceFile);
}

// sourcedefinition.go:657
std::vector<::tsc::Node*> getPropertyLikeSourceDeclarations(
	::tsc::Node* originalNode, const std::vector<::tsc::Node*>& declarations) {
	if (originalNode->parent == nullptr ||
		!isAccessExpression(originalNode->parent) ||
		originalNode->parent->name() != originalNode) {
		return {};
	}
	std::vector<::tsc::Node*> out;
	for (auto* node : declarations) {
		switch (node->kind) {
		case Kind::PropertyAssignment:
		case Kind::ShorthandPropertyAssignment:
		case Kind::PropertyDeclaration:
		case Kind::PropertySignature:
		case Kind::MethodDeclaration:
		case Kind::MethodSignature:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
		case Kind::EnumMember:
			out.push_back(node);
			break;
		default:
			break;
		}
	}
	return out;
}

// sourcedefinition.go:644
std::vector<::tsc::Node*> filterPreferredSourceDeclarations(
	::tsc::Node* originalNode, const std::vector<::tsc::Node*>& declarations) {
	if (declarations.size() <= 1 || originalNode == nullptr) {
		return declarations;
	}
	if (std::vector<::tsc::Node*> preferred =
			getPropertyLikeSourceDeclarations(originalNode, declarations);
		!preferred.empty()) {
		return preferred;
	}
	std::vector<::tsc::Node*> preferred;
	for (auto* d : declarations) {
		if (isConcreteSourceDeclaration(d)) {
			preferred.push_back(d);
		}
	}
	if (!preferred.empty()) {
		return preferred;
	}
	return declarations;
}

// getSourceDefCheckerInfo acquires the type checker for the given file and
// returns the definition declarations for node along with the module specifier
// of the import that brought the symbol into scope (empty if not applicable).
// sourcedefinition.go:203
std::pair<std::vector<::tsc::Node*>, std::string> getSourceDefCheckerInfo(
	gostd::Context ctx, compiler::SimpleProgram* program, SourceFile* file,
	::tsc::Node* node) {
	auto [c, done] = program->GetTypeChecker(ctx);
	struct Deferred_ {
		std::function<void()> f;
		~Deferred_() { if (f) f(); }
	} doneGuard{done};

	std::vector<::tsc::Node*> declarations = getDeclarationsFromLocation(c, node);
	bool isPropertyName = node->parent != nullptr &&
						  isAccessExpression(node->parent) &&
						  node->parent->name() == node;
	if (declarations.empty() && isPropertyName) {
		if (::tsc::Node* left = node->parent->expression(); left != nullptr) {
			if (Symbol* prop = c->GetPropertyOfType(c->GetTypeAtLocation(left),
												  node->text());
				prop != nullptr) {
				declarations = prop->declarations;
			}
		}
	}
	if (::tsc::Node* calledDeclaration = tryGetSignatureDeclaration(c, node);
		calledDeclaration != nullptr) {
		std::vector<::tsc::Node*> nonFunctionDeclarations;
		for (auto* d : declarations) {
			if (!isFunctionLike(d)) {
				nonFunctionDeclarations.push_back(d);
			}
		}
		nonFunctionDeclarations.push_back(calledDeclaration);
		declarations = std::move(nonFunctionDeclarations);
	}

	// Extract module specifier from the import that brought this symbol into
	// scope. For property access (obj.prop), walk up the access chain to the
	// root expression's symbol.
	std::string moduleSpecifier;
	::tsc::Node* resolveNode = node;
	if (isPropertyName) {
		::tsc::Node* expr = node->parent->expression();
		while (expr != nullptr && isAccessExpression(expr)) {
			expr = expr->expression();
		}
		if (expr != nullptr) {
			resolveNode = expr;
		}
	}
	if (Symbol* sym = c->GetSymbolAtLocation(resolveNode); sym != nullptr) {
		for (auto* d : sym->declarations) {
			if (!isImportSpecifier(d) && !isImportClause(d) &&
				!isNamespaceImport(d) &&
				!isImportEqualsDeclaration(d)) {
				continue;
			}
			if (::tsc::Node* spec =
					checker::tryGetModuleSpecifierFromDeclaration(d);
				spec != nullptr) {
				moduleSpecifier = spec->text();
				break;
			}
		}
	}

	return {declarations, moduleSpecifier};
}

} // namespace

// newSourceDefResolver — sourcedefinition.go:142
sourceDefResolver* LanguageService::newSourceDefResolver(
	compiler::SimpleProgram* program, std::string resolveFrom) {
	const CompilerOptions* options = program->Options();
	auto* r = new sourceDefResolver;
	r->ls = this;
	r->fs = program->Host()->FS();
	r->options = options;
	r->getSourceFile =
		[program](const std::string& name) { return program->GetSourceFile(name); };
	r->resolveFrom = std::move(resolveFrom);
	r->noDtsOptions = *options;
	r->noDtsOptions.NoDtsResolution = Tristate::True;
	module::ResolverOptions opts;
	opts.Host = program->Host();
	opts.CompilerOptions = &r->noDtsOptions;
	opts.TypingsLocation = program->GetGlobalTypingsCacheLocation();
	opts.ExtraExtensions = program->ContentMapperExtensions();
	r->resolver.reset(module::NewResolver(std::move(opts)));
	return r;
}

// resolveFromCheckerInfo — sourcedefinition.go:167
std::vector<::tsc::Node*> sourceDefResolver::resolveFromCheckerInfo(
	::tsc::Node* node, std::string resolvedImplFile,
	const std::vector<::tsc::Node*>& checkerDeclarations,
	const std::string& moduleSpecifier) {
	// If we don't yet have a forward-resolved implementation file, try to
	// recover a module specifier from the checker (e.g. from the import that
	// brought the symbol into scope, or from the root of an access expression).
	if (resolvedImplFile.empty() && !moduleSpecifier.empty()) {
		resolvedImplFile = resolveImplementation(
			moduleSpecifier, inferImpliedNodeFormat(resolveFrom));
	}

	// For property access where the checker found no declarations (e.g.
	// mapped types), search the implementation file for the property name.
	if (checkerDeclarations.empty() && !resolvedImplFile.empty()) {
		std::vector<std::string> names =
			getCandidateSourceDeclarationNames(node, nullptr);
		if (std::vector<::tsc::Node*> results =
				searchImplementationFile(node, resolvedImplFile, names);
			!results.empty()) {
			return uniqueDeclarationNodes(results);
		}
	}

	std::vector<::tsc::Node*> declarations;
	for (auto* declaration : checkerDeclarations) {
		auto mapped =
			mapDeclarationToSource(node, declaration, resolvedImplFile);
		declarations.insert(declarations.end(), mapped.begin(), mapped.end());
	}
	declarations = uniqueDeclarationNodes(declarations);
	if (hasConcreteSourceDeclarations(declarations)) {
		return declarations;
	}
	return {};
}

// resolveTripleSlashReference handles /// <reference path/types="..."/> directives.
// For path references to .js files, it returns the entry declarations directly.
// For path references to .d.ts files or type references, it uses the NoDts
// resolver to find the corresponding implementation file.
// sourcedefinition.go:259
std::pair<std::vector<::tsc::Node*>, FileReference*>
sourceDefResolver::resolveTripleSlashReference(
	SourceFile* file, int pos, compiler::SimpleProgram* program) {
	refInfo* ref = getReferenceAtPosition(file, pos, program);
	if (ref == nullptr || ref->file == nullptr) {
		return {{}, nullptr};
	}

	// If the referenced file is already an implementation file, return it directly.
	if (!ref->file->IsDeclarationFile) {
		return {getSourceDefinitionEntryDeclarations(ref->file), ref->reference};
	}

	// The referenced file is a .d.ts. Try to find the implementation file
	// using the NoDts module resolver via findImplementationFileFromDtsFileName.
	std::string dtsFileName = ref->file->FileName();
	ResolutionMode preferredMode = inferImpliedNodeFormat(dtsFileName);
	std::string implementationFile =
		findImplementationFileFromDtsFileName(dtsFileName, preferredMode);
	if (implementationFile.empty()) {
		return {{}, nullptr};
	}

	SourceFile* sourceFile = getOrParseSourceFile(implementationFile);
	if (sourceFile == nullptr) {
		return {{}, nullptr};
	}
	return {getSourceDefinitionEntryDeclarations(sourceFile), ref->reference};
}

// searchImplementationFile searches an implementation file for declarations
// matching the given names. Returns empty when no declarations matched; callers
// fall through to the checker path or to the standard definition provider.
// sourcedefinition.go:289
std::vector<::tsc::Node*> sourceDefResolver::searchImplementationFile(
	::tsc::Node* originalNode, const std::string& implementationFile,
	const std::vector<std::string>& names) {
	if (implementationFile.empty()) {
		return {};
	}
	SourceFile* sourceFile = getOrParseSourceFile(implementationFile);
	if (sourceFile == nullptr) {
		return {};
	}
	if (isDefaultImportName(originalNode)) {
		// For default imports, only search for "default" declarations to avoid
		// matching unrelated declarations with the same identifier name.
		collections::Set<std::string> seen;
		std::vector<::tsc::Node*> defaultDeclarations =
			findDeclarationsInFile(implementationFile, {"default"}, &seen);
		if (!defaultDeclarations.empty()) {
			return filterPreferredSourceDeclarations(originalNode,
													 defaultDeclarations);
		}
		return getSourceDefinitionEntryDeclarations(sourceFile);
	}
	collections::Set<std::string> seen;
	std::vector<::tsc::Node*> declarations =
		findDeclarationsInFile(implementationFile, names, &seen);
	if (!declarations.empty()) {
		return filterPreferredSourceDeclarations(originalNode, declarations);
	}
	return {};
}

// mapDeclarationToSource — sourcedefinition.go:335
std::vector<::tsc::Node*> sourceDefResolver::mapDeclarationToSource(
	::tsc::Node* originalNode, ::tsc::Node* declaration,
	const std::string& resolvedImplFile) {
	auto [file, startPos] = getFileAndStartPosFromDeclaration(declaration);
	std::string fileName = file->FileName();

	if (sourcemap::DocumentPosition* mapped =
			ls->tryGetSourcePosition(fileName, startPos);
		mapped != nullptr) {
		if (SourceFile* sourceFile = getOrParseSourceFile(mapped->FileName);
			sourceFile != nullptr) {
			return {findClosestDeclarationNode(sourceFile, mapped->Pos)};
		}
	}

	if (!tspath::isDeclarationFileName(fileName)) {
		return {declaration};
	}

	std::string implementationFile = resolvedImplFile;
	if (implementationFile.empty()) {
		// Reverse-resolve .d.ts path to implementation file. This path is only
		// reached for declarations with no associated module specifier (e.g.
		// globals, ambient declarations, or when forward resolution failed).
		std::string dtsFileName = getSourceFileOfNode(declaration)->FileName();
		ResolutionMode preferredMode = inferImpliedNodeFormat(dtsFileName);
		implementationFile =
			findImplementationFileFromDtsFileName(dtsFileName, preferredMode);
	}

	return searchImplementationFile(
		originalNode, implementationFile,
		getCandidateSourceDeclarationNames(originalNode, declaration));
}

// findImplementationFileFromDtsFileName — sourcedefinition.go:366
std::string sourceDefResolver::findImplementationFileFromDtsFileName(
	const std::string& dtsFileName, ResolutionMode preferredMode) {
	if (std::string_view jsExt =
			module::TryGetJSExtensionForFile(dtsFileName, *options);
		!jsExt.empty()) {
		std::string candidate = tspath::changeExtension(dtsFileName, jsExt);
		if (fs->FileExists(candidate)) {
			return candidate;
		}
	}

	std::optional<modulespecifiers::NodeModulePathParts> parts =
		modulespecifiers::GetNodeModulePathParts(dtsFileName);
	if (!parts.has_value()) {
		return "";
	}

	// Ensure the file only contains one /node_modules/ segment. If there's more
	// than one, the package name extraction may be incorrect, so bail out.
	if (dtsFileName.rfind("/node_modules/") != parts->TopLevelNodeModulesIndex) {
		return "";
	}

	std::string packageNamePathPart = dtsFileName.substr(
		parts->TopLevelPackageNameIndex + 1,
		parts->PackageRootIndex - parts->TopLevelPackageNameIndex - 1);
	std::string packageName = module::GetPackageNameFromTypesPackageName(
		module::UnmangleScopedPackageName(packageNamePathPart));
	if (packageName.empty()) {
		return "";
	}

	std::string pathToFileInPackage =
		dtsFileName.substr(parts->PackageRootIndex + 1);

	// Try resolving as a package subpath first (e.g. "pkg/dist/utils"), then
	// fall back to the bare package name (e.g. "pkg"). This covers both main
	// entrypoints and deep imports without needing to inspect package.json
	// entrypoints.
	if (!pathToFileInPackage.empty()) {
		std::string specifier =
			packageName + "/" +
			std::string{tspath::removeFileExtension(pathToFileInPackage)};
		if (std::string implementationFile =
				resolveImplementation(specifier, preferredMode);
			!implementationFile.empty()) {
			return implementationFile;
		}
	}
	return resolveImplementation(packageName, preferredMode);
}

// resolveImplementation — sourcedefinition.go:409
std::string sourceDefResolver::resolveImplementation(
	const std::string& moduleName, ResolutionMode preferredMode) {
	return resolveImplementationFrom(moduleName, resolveFrom, preferredMode);
}

// resolveImplementationFrom — sourcedefinition.go:416
std::string sourceDefResolver::resolveImplementationFrom(
	const std::string& moduleName, const std::string& resolveFromFile,
	ResolutionMode preferredMode) {
	std::vector<ResolutionMode> modes{preferredMode};
	if (preferredMode != ResolutionModeESM) {
		modes.push_back(ResolutionModeESM);
	}
	if (preferredMode != ResolutionModeCommonJS) {
		modes.push_back(ResolutionModeCommonJS);
	}

	for (auto mode : modes) {
		auto [resolved, _] = resolver->ResolveModuleName(
			moduleName, resolveFromFile, mode, nullptr);
		if (resolved != nullptr && resolved->IsResolved() &&
			!tspath::isDeclarationFileName(resolved->ResolvedFileName)) {
			return resolved->ResolvedFileName;
		}
	}
	return "";
}

// getOrParseSourceFile — sourcedefinition.go:438
SourceFile* sourceDefResolver::getOrParseSourceFile(
	const std::string& fileName) {
	if (SourceFile* sourceFile = getSourceFile(fileName);
		sourceFile != nullptr) {
		return sourceFile;
	}
	if (auto it = parsedFiles.find(fileName); it != parsedFiles.end()) {
		return it->second;
	}
	SourceFile* sourceFile = nullptr;
	if (auto [text, ok] = ls->ReadFile(fileName); ok) {
		SourceFileParseOptions parseOpts;
		parseOpts.FileName = fileName;
		parseOpts.Path = ls->toPath(fileName);
		sourceFile = parseSourceFile(
			parseOpts, text,
			// A declaration map's `sources` entries are arbitrary strings, so the
			// file name here may not have a recognized extension.
			ensureScriptKindFromFileName(fileName));
		bindSourceFile(sourceFile);
	}
	parsedFiles[fileName] = sourceFile;
	return sourceFile;
}

// inferImpliedNodeFormat — sourcedefinition.go:465
ResolutionMode sourceDefResolver::inferImpliedNodeFormat(
	const std::string& fileName) {
	std::string packageJsonType;
	if (auto scope = resolver->GetPackageScopeForPath(
			tspath::getDirectoryPath(fileName));
		scope != nullptr && scope->Exists()) {
		if (auto [value, ok] = scope->Contents->Type.GetValue(); ok) {
			packageJsonType = value;
		}
	}
	return getImpliedNodeFormatForFile(fileName, packageJsonType);
}

// findDeclarationsInFile — sourcedefinition.go:486
std::vector<::tsc::Node*> sourceDefResolver::findDeclarationsInFile(
	const std::string& fileName, const std::vector<std::string>& names,
	collections::Set<std::string>* seen) {
	if (fileName.empty() || names.empty()) {
		return {};
	}
	if (!seen->AddIfAbsent(fileName)) {
		return {};
	}

	SourceFile* sourceFile = getOrParseSourceFile(fileName);
	if (sourceFile == nullptr) {
		return {};
	}

	std::vector<::tsc::Node*> declarations =
		findDeclarationNodesByName(sourceFile, names);
	if (!declarations.empty() && hasConcreteSourceDeclarations(declarations)) {
		return declarations;
	}

	std::vector<::tsc::Node*> forwarded;
	for (auto& forwardedFile : getForwardedImplementationFiles(sourceFile)) {
		auto part = findDeclarationsInFile(forwardedFile, names, seen);
		forwarded.insert(forwarded.end(), part.begin(), part.end());
	}
	if (!forwarded.empty()) {
		if (hasConcreteSourceDeclarations(forwarded)) {
			return uniqueDeclarationNodes(forwarded);
		}
		std::vector<::tsc::Node*> combined = std::move(declarations);
		combined.insert(combined.end(), forwarded.begin(), forwarded.end());
		return uniqueDeclarationNodes(std::move(combined));
	}
	return declarations;
}

// getForwardedImplementationFiles — sourcedefinition.go:521
std::vector<std::string> sourceDefResolver::getForwardedImplementationFiles(
	SourceFile* sourceFile) {
	ResolutionMode preferredMode =
		inferImpliedNodeFormat(sourceFile->FileName());

	std::vector<std::string> files;
	for (auto* imp : sourceFile->imports) {
		std::string moduleName = imp->text();
		if (std::string implementationFile = resolveImplementationFrom(
				moduleName, sourceFile->FileName(), preferredMode);
			!implementationFile.empty()) {
			files.push_back(implementationFile);
		}
	}
	return deduplicate(std::move(files));
}

// ============================================================================
// sourcedefinition.go — ProvideSourceDefinition / provideSourceDefinitionAtPosition
// ============================================================================
// sourcedefinition.go:25
lsp::lsproto::DefinitionResponse LanguageService::ProvideSourceDefinition(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
	lsp::lsproto::Position position) {
	auto [program, file] = getProgramAndFile(documentURI);
	auto positions = converters->FromLSPPositionForSourceFile(
		file, position, spanmap::FeatureDefinition);
	std::vector<lsp::lsproto::DefinitionResponse> results;
	results.reserve(positions.size());
	for (auto& mapped : positions) {
		if (mapped.Fidelity.IsSingleSegment()) {
			auto [result, err] = provideSourceDefinitionAtPosition(
				ctx, program, mapped.Script, mapped.Position);
			if (err != nullptr) {
				return lsp::lsproto::DefinitionResponse{};
			}
			results.push_back(result);
		}
	}
	return combineDefinitionResponses(
		results, lsp::lsproto::GetClientCapabilities(ctx)
					 ->TextDocument.Definition.LinkSupport);
}

// sourcedefinition.go:45
std::pair<lsp::lsproto::DefinitionResponse, gostd::Error>
LanguageService::provideSourceDefinitionAtPosition(
	gostd::Context ctx, compiler::SimpleProgram* program, SourceFile* file,
	TextPos textPos) {
	const lsp::lsproto::ResolvedClientCapabilities* caps =
		lsp::lsproto::GetClientCapabilities(ctx);
	bool clientSupportsLink = caps->TextDocument.Definition.LinkSupport;

	int pos = int(textPos);
	std::unique_ptr<sourceDefResolver> resolver(
		newSourceDefResolver(program, file->FileName()));
	::tsc::Node* node = astnav::getTouchingPropertyName(file, pos);

	if (node->kind == Kind::SourceFile) {
		// Triple-slash directives are comments, not AST nodes, so
		// GetTouchingPropertyName returns the SourceFile node.
		auto [declarations, ref] =
			resolver->resolveTripleSlashReference(file, pos, program);
		if (!declarations.empty()) {
			auto [originSelectionRange, _f] =
				createLspRangeFromBounds(ref->pos(), ref->end(), file);
			return {createDefinitionLocations(originSelectionRange,
											  clientSupportsLink, declarations,
											  nullptr /*reference*/,
											  spanmap::FeatureDefinition),
					nullptr};
		}
		return {lsp::lsproto::LocationOrLocationsOrDefinitionLinksOrNull{},
				nullptr};
	}

	auto [originSelectionRange, _f] = createLspRangeFromNode(node, file);

	// If the cursor is directly on a module specifier string, resolve to the
	// implementation file's entry point.
	::tsc::Node* containingModuleSpecifier =
		findContainingModuleSpecifier(node);
	if (node == containingModuleSpecifier) {
		ResolutionMode specifierMode =
			program->GetModeForUsageLocation(file, containingModuleSpecifier);
		if (std::string implementationFile = resolver->resolveImplementation(
				containingModuleSpecifier->text(), specifierMode);
			!implementationFile.empty()) {
			if (SourceFile* sourceFile =
					resolver->getOrParseSourceFile(implementationFile);
				sourceFile != nullptr) {
				return {createDefinitionLocations(
							originSelectionRange, clientSupportsLink,
							getSourceDefinitionEntryDeclarations(sourceFile),
							nullptr, spanmap::FeatureDefinition),
						nullptr};
			}
		}
		return {provideDefinitionAtPosition(ctx, program, file, textPos,
											clientSupportsLink),
				nullptr};
	}

	// Phase 1: Syntactic fast path — when the cursor is inside an
	// import/require/export, forward-resolve the module specifier to an
	// implementation file and search it directly. This avoids acquiring
	// the type checker entirely when the fast path succeeds.
	std::string resolvedImplFile;
	if (containingModuleSpecifier != nullptr) {
		ResolutionMode specifierMode =
			program->GetModeForUsageLocation(file, containingModuleSpecifier);
		resolvedImplFile = resolver->resolveImplementation(
			containingModuleSpecifier->text(), specifierMode);
	}

	if (!resolvedImplFile.empty()) {
		std::vector<std::string> names =
			getCandidateSourceDeclarationNames(node, nullptr);
		std::vector<::tsc::Node*> moduleResults =
			resolver->searchImplementationFile(node, resolvedImplFile, names);
		if (!moduleResults.empty()) {
			if ((!isPartOfTypeNode(node) &&
				 !isPartOfTypeOnlyImportOrExportDeclaration(node)) ||
				hasConcreteSourceDeclarations(moduleResults)) {
				return {createDefinitionLocations(
							originSelectionRange, clientSupportsLink,
							uniqueDeclarationNodes(moduleResults), nullptr,
							spanmap::FeatureDefinition),
						nullptr};
			}
		}
	}

	// Phase 2: Type checker path — acquire the checker for the original file
	// and use its declarations and module specifier to map to source
	// implementations. This is the only point where the checker is used;
	// after this, only the NoDts module resolver and file parsing are needed.
	auto [checkerDeclarations, moduleSpecifier] =
		getSourceDefCheckerInfo(ctx, program, file, node);

	// Phase 3: Map checker results to source definitions.
	std::vector<::tsc::Node*> declarations = resolver->resolveFromCheckerInfo(
		node, resolvedImplFile, checkerDeclarations, moduleSpecifier);
	if (declarations.empty()) {
		// If we resolved an implementation file from an import/export but
		// couldn't find specific declarations, fall back to the file entry
		// point rather than the standard definition provider — unless the
		// checker found declarations that are all type-only (e.g. interfaces),
		// in which case the .d.ts definition is more appropriate.
		if (containingModuleSpecifier != nullptr && !resolvedImplFile.empty() &&
			!hasConcreteSourceDeclarations(checkerDeclarations)) {
			if (SourceFile* sourceFile =
					resolver->getOrParseSourceFile(resolvedImplFile);
				sourceFile != nullptr) {
				return {createDefinitionLocations(
							originSelectionRange, clientSupportsLink,
							getSourceDefinitionEntryDeclarations(sourceFile),
							nullptr, spanmap::FeatureDefinition),
						nullptr};
			}
		}
		return {provideDefinitionAtPosition(ctx, program, file, textPos,
											clientSupportsLink),
				nullptr};
	}
	return {createDefinitionLocations(originSelectionRange, clientSupportsLink,
									  declarations, nullptr /*reference*/,
									  spanmap::FeatureDefinition),
			nullptr};
}

} // namespace tsc::ls
