// === slice: ls-coreC ===
// symbols.cpp — symbols.go: document symbols + workspace/symbol.
#include "internal/ls/ls.h"

#include "internal/stringutil/stringutil.h"

namespace tsc::ls {

// ============================================================================
// === slice: ls-coreC — helpers missing from already-ported slices ===
// ============================================================================
// ast.go:3087 — GetDeclarationName
std::string getDeclarationName(::tsc::Node* declaration) {
	::tsc::Node* name = getNonAssignedNameOfDeclaration(declaration);
	if (name != nullptr) {
		if (isComputedPropertyName(name)) {
			if (isStringOrNumericLiteralLike(name->expression())) {
				return name->expression()->text();
			}
			if (isPropertyAccessExpression(name->expression())) {
				return name->expression()->name()->text();
			}
		} else if (isPropertyName(name)) {
			return name->text();
		}
	}
	return "";
}

namespace {

// ast.go:2975 — computeDeclarationMap
std::unordered_map<std::string, std::vector<::tsc::Node*>>
computeDeclarationMap(SourceFile* file) {
	std::unordered_map<std::string, std::vector<::tsc::Node*>> result;

	auto addDeclaration = [&](::tsc::Node* declaration) {
		std::string name = getDeclarationName(declaration);
		if (!name.empty()) {
			result[name].push_back(declaration);
		}
	};

	std::function<bool(::tsc::Node*)> visit = [&](::tsc::Node* node) -> bool {
		switch (node->kind) {
		case Kind::FunctionDeclaration:
		case Kind::FunctionExpression:
		case Kind::MethodDeclaration:
		case Kind::MethodSignature: {
			std::string declarationName = getDeclarationName(node);
			if (!declarationName.empty()) {
				std::vector<::tsc::Node*>& declarations =
					result[declarationName];
				::tsc::Node* lastDeclaration = nullptr;
				if (!declarations.empty()) {
					lastDeclaration = declarations.back();
				}
				// Check whether this declaration belongs to an "overload group".
				if (lastDeclaration != nullptr &&
					node->parent == lastDeclaration->parent &&
					node->symbol() == lastDeclaration->symbol()) {
					// Overwrite the last declaration if it was an overload and this one is an implementation.
					if (node->body() != nullptr &&
						lastDeclaration->body() == nullptr) {
						declarations.back() = node;
					}
				} else {
					result[declarationName].push_back(node);
				}
			}
			node->forEachChild([&](::tsc::Node* child) {
				return visit(child);
			});
			break;
		}
		case Kind::ClassDeclaration:
		case Kind::ClassExpression:
		case Kind::InterfaceDeclaration:
		case Kind::TypeAliasDeclaration:
		case Kind::EnumDeclaration:
		case Kind::ModuleDeclaration:
		case Kind::ImportEqualsDeclaration:
		case Kind::ImportClause:
		case Kind::NamespaceImport:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
		case Kind::TypeLiteral:
			addDeclaration(node);
			node->forEachChild([&](::tsc::Node* child) {
				return visit(child);
			});
			break;
		case Kind::ImportSpecifier:
		case Kind::ExportSpecifier:
			if (node->propertyName() != nullptr) {
				addDeclaration(node);
			}
			break;
		case Kind::Parameter:
			// Only consider parameter properties
			if (!hasSyntacticModifier(
					node, ModifierFlagsParameterPropertyModifier)) {
				break;
			}
			[[fallthrough]];
		case Kind::VariableDeclaration:
		case Kind::BindingElement: {
			::tsc::Node* name = node->name();
			if (name != nullptr) {
				if (isBindingPattern(name)) {
					node->name()->forEachChild([&](::tsc::Node* child) {
						return visit(child);
					});
				} else {
					if (node->initializer() != nullptr) {
						visit(node->initializer());
					}
					addDeclaration(node);
				}
			}
			break;
		}
		case Kind::EnumMember:
		case Kind::PropertyDeclaration:
		case Kind::PropertySignature:
			addDeclaration(node);
			break;
		case Kind::ExportDeclaration: {
			// Handle named exports case e.g.:
			//    export {a, b as B} from "mod";
			::tsc::Node* exportClause =
				node->as<ExportDeclaration>()->ExportClause;
			if (exportClause != nullptr) {
				if (isNamedExports(exportClause)) {
					for (auto* element : exportClause->elements()) {
						visit(element);
					}
				} else {
					visit(exportClause->as<NamespaceExport>()->name);
				}
			}
			break;
		}
		case Kind::ImportDeclaration: {
			::tsc::Node* importClause =
				node->as<ImportDeclaration>()->ImportClause;
			if (importClause != nullptr) {
				// Handle default import case e.g.:
				//    import d from "mod";
				if (importClause->name() != nullptr) {
					addDeclaration(importClause->name());
				}
				// Handle named bindings in imports e.g.:
				//    import * as NS from "mod";
				//    import {a, b as B} from "mod";
				::tsc::Node* namedBindings =
					importClause->as<ImportClause>()->NamedBindings;
				if (namedBindings != nullptr) {
					if (namedBindings->kind == Kind::NamespaceImport) {
						addDeclaration(namedBindings);
					} else {
						for (auto* element : namedBindings->elements()) {
							visit(element);
						}
					}
				}
			}
			break;
		}
		case Kind::BinaryExpression:
			switch (getAssignmentDeclarationKind(node)) {
			case JSDeclarationKind::ExportsProperty:
			case JSDeclarationKind::ThisProperty:
			case JSDeclarationKind::Property:
				addDeclaration(node);
				break;
			default:
				break;
			}
			node->forEachChild([&](::tsc::Node* child) {
				return visit(child);
			});
			break;
		default:
			node->forEachChild([&](::tsc::Node* child) {
				return visit(child);
			});
			break;
		}
		return false;
	};
	file->forEachChild([&](::tsc::Node* child) {
		return visit(child);
	});
	return result;
}

} // namespace

// ast.go:2968 — SourceFile.GetDeclarationMap
const std::unordered_map<std::string, std::vector<::tsc::Node*>>&
getDeclarationMap(SourceFile* file) {
	std::lock_guard<std::mutex> lock(file->declarationMapMu);
	if (file->declarationMap.empty()) {
		file->declarationMap = computeDeclarationMap(file);
	}
	return file->declarationMap;
}

// stringutil/util.go:256 — TruncateByRunes
std::string truncateByRunes(std::string_view str, int maxLength) {
	if (int(str.size()) < maxLength) {
		return std::string(str);
	}
	if (maxLength <= 0) {
		return "";
	}
	int runeCount = 0;
	// Go `for i := range str` iterates rune boundaries; str[:i] after the
	// (maxLength+1)th rune starts is the truncation point.
	size_t i = 0;
	while (i < str.size()) {
		int w = 0;
		decodeUtf8Rune(str.substr(i), &w);
		runeCount++;
		if (runeCount > maxLength) {
			return std::string(str.substr(0, i));
		}
		i += w > 0 ? w : 1;
	}
	return std::string(str);
}

namespace {

constexpr int maxLength = 150;

// symbols.go:540 — DeclarationInfo
struct DeclarationInfo {
	std::string name;
	::tsc::Node* declaration = nullptr;
	int matchScore = 0;
};

// seen-key for deduping document symbols across projections.
struct docSymbolKey {
	std::string name;
	lsp::lsproto::SymbolKind kind = 0;
	lsp::lsproto::Range rng;
	bool operator==(const docSymbolKey&) const = default;
};
struct docSymbolKeyHash {
	size_t operator()(const docSymbolKey& k) const {
		return std::hash<std::string>{}(k.name) * 131 + k.kind * 8191 +
			   std::hash<uint32_t>{}(k.rng.Start.Line) * 131 +
			   k.rng.Start.Character * 17 + k.rng.End.Line * 7 +
			   k.rng.End.Character;
	}
};

// symbols.go:65 — flattenDocumentSymbols
std::vector<lsp::lsproto::SymbolInformation> flattenDocumentSymbols(
	const std::vector<lsp::lsproto::DocumentSymbol*>& docSymbols,
	lsp::lsproto::DocumentUri documentURI) {
	std::vector<lsp::lsproto::SymbolInformation> result;
	std::function<void(const std::vector<lsp::lsproto::DocumentSymbol*>&,
					   std::string*)>
		flatten = [&](const std::vector<lsp::lsproto::DocumentSymbol*>& symbols,
					  std::string* containerName) {
			for (auto* symbol : symbols) {
				lsp::lsproto::SymbolInformation info;
				info.Name = symbol->Name;
				info.Kind = symbol->Kind;
				info.Location.Uri = documentURI;
				info.Location.Range = symbol->Range;
				info.ContainerName = containerName;
				info.Tags = symbol->Tags;
				info.Deprecated = symbol->Deprecated;
				result.push_back(info);

				// Recursively flatten children with this symbol as container
				if (symbol->Children != nullptr && !symbol->Children->empty()) {
					flatten(*symbol->Children, &symbol->Name);
				}
			}
		};
	flatten(docSymbols, nullptr);
	return result;
}

// symbols.go:282 — isPrototypeExpando
bool isPrototypeExpando(::tsc::Node* target) {
	if (isAccessExpression(target)) {
		::tsc::Node* accessName = getElementOrPropertyAccessName(target);
		return accessName != nullptr && accessName->text() == "prototype";
	}
	return false;
}

// symbols.go:410/424 — mergeChildren / isAnonymousName (fwd decls)
void mergeChildren(lsp::lsproto::DocumentSymbol* target,
				   lsp::lsproto::DocumentSymbol* source);
bool isAnonymousName(const std::string& name);

// Merges expando symbols into their target symbols, and namespaces of same name.
// Modifies the input vector.
// symbols.go:351
std::vector<lsp::lsproto::DocumentSymbol*> mergeExpandos(
	std::vector<lsp::lsproto::DocumentSymbol*>& symbols) {
	std::vector<lsp::lsproto::DocumentSymbol*> mergedSymbols;
	mergedSymbols.reserve(symbols.size());
	// Collect symbols that can be an expando target.
	collections::MultiMap<std::string, int> nameToExpandoTargetIndex;
	// Collect namespaces.
	std::unordered_map<std::string, int> nameToNamespaceIndex;
	for (size_t i = 0; i < symbols.size(); i++) {
		auto* symbol = symbols[i];
		if (isAnonymousName(symbol->Name)) {
			continue;
		}
		if (symbol->Kind == lsp::lsproto::SymbolKindClass ||
			symbol->Kind == lsp::lsproto::SymbolKindFunction ||
			symbol->Kind == lsp::lsproto::SymbolKindVariable) {
			nameToExpandoTargetIndex.Add(symbol->Name, int(i));
		}
		if (symbol->Kind == lsp::lsproto::SymbolKindNamespace) {
			if (nameToNamespaceIndex.find(symbol->Name) ==
				nameToNamespaceIndex.end()) {
				nameToNamespaceIndex[symbol->Name] = int(i);
			}
		}
	}
	for (size_t i = 0; i < symbols.size(); i++) {
		auto* symbol = symbols[i];
		if (symbol->Children != nullptr) {
			auto children = mergeExpandos(*symbol->Children);
			*symbol->Children = std::move(children);
		}

		// Anonymous symbols never merge.
		if (isAnonymousName(symbol->Name)) {
			continue;
		}

		// Merge expandos.
		if (symbol->Kind == lsp::lsproto::SymbolKindProperty) {
			auto symbolsWithSameName =
				nameToExpandoTargetIndex.Get(symbol->Name);
			for (auto it = symbolsWithSameName.rbegin();
				 it != symbolsWithSameName.rend(); ++it) {
				lsp::lsproto::DocumentSymbol* targetSymbol = symbols[*it];
				mergeChildren(targetSymbol, symbol);
				// Mark this symbol as merged.
				symbols[i] = nullptr;
			}
		}
		// Merge namespaces.
		if (symbol->Kind == lsp::lsproto::SymbolKindNamespace) {
			auto it = nameToNamespaceIndex.find(symbol->Name);
			if (it != nameToNamespaceIndex.end() && it->second != int(i)) {
				lsp::lsproto::DocumentSymbol* targetSymbol = symbols[it->second];
				mergeChildren(targetSymbol, symbol);
				// Mark this symbol as merged.
				symbols[i] = nullptr;
			}
		}
	}
	for (auto* symbol : symbols) {
		if (symbol != nullptr) {
			mergedSymbols.push_back(symbol);
		}
	}
	return mergedSymbols;
}

// symbols.go:410 — mergeChildren
void mergeChildren(lsp::lsproto::DocumentSymbol* target,
				   lsp::lsproto::DocumentSymbol* source) {
	if (source->Children != nullptr) {
		if (target->Children == nullptr) {
			target->Children = source->Children;
		} else {
			target->Children->insert(target->Children->end(),
									 source->Children->begin(),
									 source->Children->end());
			auto merged = mergeExpandos(*target->Children);
			*target->Children = std::move(merged);
			std::sort(target->Children->begin(), target->Children->end(),
					  [](lsp::lsproto::DocumentSymbol* a,
						 lsp::lsproto::DocumentSymbol* b) {
						  return lsp::lsproto::CompareRanges(a->Range, b->Range) <
								 0;
					  });
		}
	}
}

// symbols.go:424 — isAnonymousName
bool isAnonymousName(const std::string& name) {
	return name == "<function>" || name == "<class>" || name == "export=" ||
		   name == "default" || name == "constructor" || name == "()" ||
		   name == "new()" || name == "[]" ||
		   (name.size() >= 10 && name.substr(name.size() - 10) == ") callback");
}

// symbols.go:429 — getTextOfName
std::string getTextOfName(::tsc::Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
	case Kind::NumericLiteral:
		return node->text();
	case Kind::StringLiteral:
		return "\"" + printer::EscapeString(node->text(), '"') + "\"";
	case Kind::NoSubstitutionTemplateLiteral:
		return "`" + printer::EscapeString(node->text(), '`') + "`";
	case Kind::ComputedPropertyName:
		if (isStringOrNumericLiteralLike(node->expression())) {
			return getTextOfName(node->expression());
		}
		break;
	default:
		break;
	}
	return tsc::getTextOfNode(node);
}

// symbols.go:486 — getCallExpressionName
std::string getCallExpressionName(::tsc::Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
		return node->text();
	case Kind::PropertyAccessExpression: {
		std::string left = getCallExpressionName(node->expression());
		std::string right = getCallExpressionName(node->name());
		if (!left.empty()) {
			return left + "." + right;
		}
		return right;
	}
	default:
		break;
	}
	return "";
}

// symbols.go:501 — getCallExpressionLiteralArgs
std::string getCallExpressionLiteralArgs(::tsc::Node* callExpr) {
	std::vector<std::string> parts;
	for (auto* arg : callExpr->arguments()) {
		if (isStringLiteralLike(arg) || isTemplateExpression(arg)) {
			parts.push_back(tsc::getTextOfNode(arg));
		}
	}
	std::string out;
	for (size_t i = 0; i < parts.size(); i++) {
		if (i != 0) {
			out += ", ";
		}
		out += parts[i];
	}
	return out;
}

// symbols.go:511 — cleanCallbackText
std::string cleanCallbackText(std::string text) {
	std::string truncated = truncateByRunes(text, maxLength);
	if (truncated.size() < text.size()) {
		text = truncated + "...";
	}
	std::string out;
	out.reserve(text.size());
	size_t i = 0;
	while (i < text.size()) {
		int w = 0;
		char32_t r = decodeUtf8Rune(
			std::string_view(text).substr(i), &w);
		i += w > 0 ? w : 1;
		if (isLineBreak(r)) {
			continue; // strings.Map returning -1 drops the rune
		}
		char buf[4];
		int n = encodeUtf8Rune(r, buf);
		out.append(buf, n);
	}
	return out;
}

// symbols.go:445 — getUnnamedNodeLabel
std::string getUnnamedNodeLabel(::tsc::Node* node) {
	if (::tsc::Node* parent = walkUpParenthesizedExpressions(node->parent);
		parent != nullptr && isExportAssignment(parent)) {
		if (parent->as<ExportAssignment>()->IsExportEquals) {
			return "export=";
		}
		return "default";
	}
	switch (node->kind) {
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
		if (node->modifierFlags() & ModifierFlagsDefault) {
			return "default";
		}
		if (isCallExpression(node->parent)) {
			std::string name =
				getCallExpressionName(node->parent->expression());
			if (!name.empty()) {
				name = cleanCallbackText(name);
				if (int(name.size()) > maxLength) {
					return name + " callback";
				}
				std::string args = cleanCallbackText(
					getCallExpressionLiteralArgs(node->parent));
				return name + "(" + args + ") callback";
			}
		}
		return "<function>";
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
		if (node->modifierFlags() & ModifierFlagsDefault) {
			return "default";
		}
		return "<class>";
	case Kind::Constructor:
		return "constructor";
	case Kind::CallSignature:
		return "()";
	case Kind::ConstructSignature:
		return "new()";
	case Kind::IndexSignature:
		return "[]";
	default:
		break;
	}
	return "";
}

// symbols.go:524 — getInteriorModule
::tsc::Node* getInteriorModule(::tsc::Node* node) {
	while (node->body() != nullptr && isModuleDeclaration(node->body())) {
		node = node->body();
	}
	return node;
}

// symbols.go:531 — getModuleName
std::string getModuleName(::tsc::Node* node) {
	std::string result = node->name()->text();
	while (node->body() != nullptr && isModuleDeclaration(node->body())) {
		node = node->body();
		result = result + "." + node->name()->text();
	}
	return result;
}


// program.go:2288 — Program.HasTSFile (not yet ported into cpp's
// SimpleProgram; Go's sync.Once caching is a perf detail — recompute).
bool programHasTSFile(compiler::SimpleProgram* program) {
	for (auto* file : program->SourceFiles()) {
		if (tspath::hasImplementationTSFileExtension(file->FileName())) {
			return true;
		}
	}
	return false;
}

// symbols.go:619 — isInsideNodeModules
bool isInsideNodeModules(const std::string& fileName) {
	return fileName.find("/node_modules/") != std::string::npos;
}

// symbols.go:615 — shouldExcludeFile
bool shouldExcludeFile(SourceFile* file, compiler::SimpleProgram* program,
					   bool excludeLibrarySymbols) {
	return excludeLibrarySymbols &&
		   (isInsideNodeModules(file->FileName()) || program->IsLibFile(file));
}

// Return a score for matching `s` against `pattern`. In order to match, `s` must contain each of the characters in
// `pattern` in the same order. Upper case characters in `pattern` must match exactly, whereas lower case characters
// in `pattern` match either case in `s`. If `s` doesn't match, -1 is returned. Otherwise, the returned score is the
// number of characters in `s` that weren't matched. Thus, zero represents an exact match, and higher values represent
// increasingly less specific partial matches.
// symbols.go:628
int getMatchScore(std::string_view s, std::string_view pattern) {
	int score = 0;
	size_t pi = 0;
	while (pi < pattern.size()) {
		int pw = 0;
		char32_t p =
			decodeUtf8Rune(pattern.substr(pi), &pw);
		pi += pw > 0 ? pw : 1;
		// unicode.IsUpper(p): upper-cased runes (Lu) map to themselves under
		// ToUpper but not under ToLower.
		bool exact = stringutil::toUpperRune(p) == p &&
					 stringutil::toLowerRune(p) != p;
		for (;;) {
			int cw = 0;
			if (s.empty()) {
				return -1;
			}
			char32_t c = decodeUtf8Rune(s, &cw);
			if (cw == 0) {
				return -1;
			}
			s = s.substr(cw);
			if ((exact && c == p) ||
				(!exact && stringutil::toLowerRune(c) ==
							  stringutil::toLowerRune(p))) {
				break;
			}
			score++;
		}
	}
	return score;
}

// Sort DeclarationInfos by ascending match score, then ascending case insensitive name, then
// ascending case sensitive name, and finally by source file name and position.
// symbols.go:649
int compareDeclarationInfos(const DeclarationInfo& d1,
							const DeclarationInfo& d2) {
	if (d1.matchScore != d2.matchScore) {
		return d1.matchScore - d2.matchScore;
	}
	if (int c = stringutil::CompareStringsCaseInsensitive(d1.name, d2.name);
		c != 0) {
		return c;
	}
	if (int c = (d1.name < d2.name ? -1 : d1.name > d2.name ? 1 : 0); c != 0) {
		return c;
	}
	SourceFile* s1 = getSourceFileOfNode(d1.declaration);
	SourceFile* s2 = getSourceFileOfNode(d2.declaration);
	if (s1 != s2) {
		return s1->Path() < s2->Path()
				   ? -1
				   : s1->Path() > s2->Path() ? 1 : 0;
	}
	return int(d1.declaration->pos()) - int(d2.declaration->pos());
}

} // namespace

// symbols.go:669 — getSymbolKindFromNode
lsp::lsproto::SymbolKind getSymbolKindFromNode(::tsc::Node* node) {
	switch (node->kind) {
	case Kind::SourceFile:
		if (isExternalModule(node->as<SourceFile>())) {
			return lsp::lsproto::SymbolKindModule;
		}
		return lsp::lsproto::SymbolKindFile;
	case Kind::ModuleDeclaration:
		return lsp::lsproto::SymbolKindNamespace;
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
		return lsp::lsproto::SymbolKindClass;
	case Kind::InterfaceDeclaration:
		return lsp::lsproto::SymbolKindInterface;
	case Kind::TypeAliasDeclaration:
	case Kind::JSDocTypedefTag:
	case Kind::JSDocCallbackTag:
		return lsp::lsproto::SymbolKindClass;
	case Kind::EnumDeclaration:
		return lsp::lsproto::SymbolKindEnum;
	case Kind::VariableDeclaration:
		return lsp::lsproto::SymbolKindVariable;
	case Kind::ArrowFunction:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
		return lsp::lsproto::SymbolKindFunction;
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		return lsp::lsproto::SymbolKindProperty;
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
		return lsp::lsproto::SymbolKindMethod;
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::SpreadAssignment:
	case Kind::IndexSignature:
		return lsp::lsproto::SymbolKindProperty;
	case Kind::CallSignature:
		return lsp::lsproto::SymbolKindMethod;
	case Kind::ConstructSignature:
		return lsp::lsproto::SymbolKindConstructor;
	case Kind::Constructor:
	case Kind::ClassStaticBlockDeclaration:
		return lsp::lsproto::SymbolKindConstructor;
	case Kind::TypeParameter:
		return lsp::lsproto::SymbolKindTypeParameter;
	case Kind::EnumMember:
		return lsp::lsproto::SymbolKindEnumMember;
	case Kind::Parameter:
		if (hasSyntacticModifier(node,
								 ModifierFlagsParameterPropertyModifier)) {
			return lsp::lsproto::SymbolKindProperty;
		}
		return lsp::lsproto::SymbolKindVariable;
	case Kind::BinaryExpression:
	case Kind::CallExpression: {
		JSDeclarationKind kind = getAssignmentDeclarationKind(node);
		switch (kind) {
		case JSDeclarationKind::ThisProperty:
		case JSDeclarationKind::Property:
		case JSDeclarationKind::ObjectDefinePropertyValue:
			return lsp::lsproto::SymbolKindProperty;
		default:
			break;
		}
		break;
	}
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::NumericLiteral:
		// String literals used as property names (e.g., in Object.defineProperty)
		return lsp::lsproto::SymbolKindProperty;
	default:
		break;
	}
	return lsp::lsproto::SymbolKindVariable;
}

// ============================================================================
// symbols.go — newDocumentSymbol / getDocumentSymbolsForChildren /
// getDocumentSymbolInformations / ProvideDocumentSymbols
// ============================================================================
// symbols.go:292
lsp::lsproto::DocumentSymbol* LanguageService::newDocumentSymbol(
	::tsc::Node* node, ::tsc::Node* name,
	std::vector<lsp::lsproto::DocumentSymbol*> children) {
	auto* result = new lsp::lsproto::DocumentSymbol;
	SourceFile* file = getSourceFileOfNode(node);
	int nodeStartPos = tsc::skipTrivia(file->Text(), int(node->pos()));
	if (name == nullptr) {
		name = getNameOfDeclaration(node);
	}
	std::string text;
	int nameStartPos = 0, nameEndPos = 0;
	if (isModuleDeclaration(node) && !isAmbientModule(node)) {
		text = getModuleName(node);
		nameStartPos = tsc::skipTrivia(file->Text(), int(name->pos()));
		nameEndPos = int(getInteriorModule(node)->name()->end());
	} else if (isAnyExportAssignment(node) &&
			   node->as<ExportAssignment>()->IsExportEquals) {
		text = "export=";
		if (!nodeIsMissing(name)) {
			nameStartPos = tsc::skipTrivia(file->Text(), int(name->pos()));
			nameEndPos = int(name->end());
		} else {
			nameStartPos = nodeStartPos;
			nameEndPos = int(node->end());
		}
	} else if (name != nullptr) {
		text = getTextOfName(name);
		nameStartPos = std::max(
			tsc::skipTrivia(file->Text(), int(name->pos())), nodeStartPos);
		nameEndPos = std::max(int(name->end()), nodeStartPos);
	} else {
		text = getUnnamedNodeLabel(node);
		nameStartPos = nodeStartPos;
		nameEndPos = nodeStartPos;
	}
	if (text.empty()) {
		return nullptr;
	}
	std::string truncatedText = truncateByRunes(text, maxLength);
	if (truncatedText.size() < text.size()) {
		text = truncatedText + "...";
	}
	result->Name = text;
	result->Kind = getSymbolKindFromNode(node);
	auto [selectionRange, selectionFidelity] =
		converters->ToLSPRangeForFeature(
			file, TextRange{static_cast<TextPos>(nameStartPos),
							static_cast<TextPos>(nameEndPos)},
			spanmap::FeatureDocumentSymbols);
	if (!selectionFidelity.IsSingleSegment()) {
		return nullptr;
	}
	auto [symbolRange, rangeFidelity] = converters->ToLSPRangeForFeature(
		file,
		TextRange{static_cast<TextPos>(nodeStartPos),
				  static_cast<TextPos>(int(node->end()))},
		spanmap::FeatureDocumentSymbols);
	if (rangeFidelity.IsNone()) {
		symbolRange = selectionRange;
	}
	result->Range = symbolRange;
	result->SelectionRange = selectionRange;
	result->Children =
		new std::vector<lsp::lsproto::DocumentSymbol*>(std::move(children));
	return result;
}

// symbols.go:94
std::vector<lsp::lsproto::DocumentSymbol*>
LanguageService::getDocumentSymbolsForChildren(gostd::Context ctx,
											 ::tsc::Node* node,
											 SourceFile* file) {
	std::vector<lsp::lsproto::DocumentSymbol*> symbols;
	collections::Set<std::string> expandoTargets;

	auto addSymbolForNode = [&](::tsc::Node* node, ::tsc::Node* name,
								std::vector<lsp::lsproto::DocumentSymbol*>
									children) {
		if ((node->flags & NodeFlagsReparsed) == 0) {
			lsp::lsproto::DocumentSymbol* symbol =
				newDocumentSymbol(node, name, std::move(children));
			if (symbol != nullptr) {
				symbols.push_back(symbol);
			}
		}
	};

	std::function<bool(::tsc::Node*)> visit;

	auto getSymbolsForChildren =
		[&](::tsc::Node* node) -> std::vector<lsp::lsproto::DocumentSymbol*> {
		std::vector<lsp::lsproto::DocumentSymbol*> result;
		if (node != nullptr) {
			auto saveExpandoTargets = expandoTargets;
			expandoTargets = collections::Set<std::string>{};
			auto saveSymbols = std::move(symbols);
			symbols.clear();
			node->forEachChild([&](::tsc::Node* child) {
				return visit(child);
			});
			result = std::move(symbols);
			symbols = std::move(saveSymbols);
			expandoTargets = std::move(saveExpandoTargets);
		}
		return result;
	};

	auto startNode = [&](::tsc::Node* node,
						 ::tsc::Node* name) -> std::function<void()> {
		if (node == nullptr) {
			return []() {};
		}
		auto saveExpandoTargets = expandoTargets;
		expandoTargets = collections::Set<std::string>{};
		auto saveSymbols = std::move(symbols);
		symbols.clear();
		return [&, node, name, saveExpandoTargets = std::move(saveExpandoTargets),
				saveSymbols = std::move(saveSymbols)]() mutable {
			auto result = std::move(symbols);
			symbols = std::move(saveSymbols);
			expandoTargets = std::move(saveExpandoTargets);
			addSymbolForNode(node, name, std::move(result));
		};
	};

	auto getSymbolsForNode =
		[&](::tsc::Node* node) -> std::vector<lsp::lsproto::DocumentSymbol*> {
		std::vector<lsp::lsproto::DocumentSymbol*> result;
		if (node != nullptr) {
			auto saveSymbols = std::move(symbols);
			symbols.clear();
			visit(node);
			result = std::move(symbols);
			symbols = std::move(saveSymbols);
		}
		return result;
	};

	visit = [&](::tsc::Node* node) -> bool {
		if (gostd::ctxErr(ctx) != nullptr) {
			return true;
		}
		if ((node->flags & NodeFlagsReparsed) == 0) {
			auto jsdocs = node->jsDoc(file);
			if (!jsdocs.empty()) {
				for (auto* jsdoc : jsdocs) {
					if (NodeList* tagList = jsdoc->as<JSDoc>()->Tags;
						tagList != nullptr) {
						for (auto* tag : tagList->nodes) {
							if (isJSDocTypedefTag(tag) ||
								isJSDocCallbackTag(tag)) {
								addSymbolForNode(tag, nullptr /*name*/,
												 {} /*children*/);
							}
						}
					}
				}
			}
		}
		switch (node->kind) {
		case Kind::ClassDeclaration:
		case Kind::ClassExpression:
		case Kind::InterfaceDeclaration:
		case Kind::EnumDeclaration:
			if (isClassLike(node) && !getDeclarationName(node).empty()) {
				expandoTargets.Add(getDeclarationName(node));
			}
			addSymbolForNode(node, nullptr /*name*/,
							 getSymbolsForChildren(node));
			break;
		case Kind::ModuleDeclaration:
			addSymbolForNode(node, nullptr /*name*/,
							 getSymbolsForChildren(getInteriorModule(node)));
			break;
		case Kind::Constructor:
			addSymbolForNode(node, nullptr /*name*/,
							 getSymbolsForChildren(node->body()));
			for (auto* param : node->parameters()) {
				if (isParameterPropertyDeclaration(param, node)) {
					addSymbolForNode(param, nullptr /*name*/,
									 {} /*children*/);
				}
			}
			break;
		case Kind::FunctionDeclaration:
		case Kind::FunctionExpression:
		case Kind::ArrowFunction:
		case Kind::MethodDeclaration:
		case Kind::GetAccessor:
		case Kind::SetAccessor: {
			std::string declName = getDeclarationName(node);
			if (!declName.empty()) {
				expandoTargets.Add(declName);
			}
			addSymbolForNode(node, nullptr /*name*/,
							 getSymbolsForChildren(node->body()));
			break;
		}
		case Kind::VariableDeclaration:
		case Kind::BindingElement:
		case Kind::PropertyAssignment:
		case Kind::PropertyDeclaration: {
			::tsc::Node* nodeName = node->name();
			if (nodeName != nullptr) {
				if (isBindingPattern(nodeName)) {
					visit(nodeName);
				} else {
					addSymbolForNode(node, nullptr /*name*/,
									 getSymbolsForChildren(
										 node->initializer()));
				}
			}
			break;
		}
		case Kind::SpreadAssignment:
			addSymbolForNode(node, node->expression(), {} /*children*/);
			break;
		case Kind::MethodSignature:
		case Kind::PropertySignature:
		case Kind::CallSignature:
		case Kind::ConstructSignature:
		case Kind::IndexSignature:
		case Kind::EnumMember:
		case Kind::ShorthandPropertyAssignment:
		case Kind::TypeAliasDeclaration:
		case Kind::ImportEqualsDeclaration:
		case Kind::ExportSpecifier:
			addSymbolForNode(node, nullptr /*name*/, {} /*children*/);
			break;
		case Kind::ImportClause:
			// Handle default import case e.g.:
			//    import d from "mod";
			if (node->name() != nullptr) {
				addSymbolForNode(node->name(), node->name(),
								 {} /*children*/);
			}
			// Handle named bindings in imports e.g.:
			//    import * as NS from "mod";
			//    import {a, b as B} from "mod";
			if (::tsc::Node* namedBindings =
					node->as<ImportClause>()->NamedBindings;
				namedBindings != nullptr) {
				if (namedBindings->kind == Kind::NamespaceImport) {
					addSymbolForNode(namedBindings, nullptr /*name*/,
									 {} /*children*/);
				} else {
					for (auto* element : namedBindings->elements()) {
						addSymbolForNode(element, nullptr /*name*/,
										 {} /*children*/);
					}
				}
			}
			break;
		case Kind::BinaryExpression:
		case Kind::CallExpression: {
			JSDeclarationKind assignmentKind =
				getAssignmentDeclarationKind(node);
			switch (assignmentKind) {
			// `module.exports = ...`` should be reparsed into a JSExportAssignment,
			// and `exports.a = ...`` into a CommonJSExport.
			case JSDeclarationKind::None:
			case JSDeclarationKind::ThisProperty:
			case JSDeclarationKind::ModuleExports:
			case JSDeclarationKind::ExportsProperty:
			case JSDeclarationKind::ObjectDefinePropertyExports:
				node->forEachChild([&](::tsc::Node* child) {
					return visit(child);
				});
				break;
			case JSDeclarationKind::Property:
			case JSDeclarationKind::ObjectDefinePropertyValue: {
				::tsc::Node* target = nullptr;
				::tsc::Node* targetFunction = nullptr;
				::tsc::Node* definition = nullptr;
				::tsc::Node* propertyName = nullptr;
				// `A.b = ... ` or `A.prototype.b = ...`
				if (isBinaryExpression(node)) {
					BinaryExpression* binaryExpr =
						node->as<BinaryExpression>();
					target = binaryExpr->Left;
					targetFunction = target->expression();
					definition = binaryExpr->Right;
					// `A.b` or `A.prototype.b`
								if (isPropertyAccessExpression(target)) {
						propertyName = target->name();
					} else { // `A["b"]` or `A.prototype["b"]`
						propertyName =
							target->as<ElementAccessExpression>()
								->ArgumentExpression;
					}
				} else { // `Object.defineProperty(A, "b", {...})`
					auto args = node->arguments();
					targetFunction = args[0];
					target = args[1];
					propertyName = target;
					definition = args[2];
				}
				if (isPrototypeExpando(targetFunction)) {
					targetFunction = targetFunction->expression();
					// If we see a prototype assignment, start tracking the target as an expando target.
					if (isIdentifier(targetFunction)) {
						expandoTargets.Add(targetFunction->text());
					}
				}
				if (isIdentifier(targetFunction) &&
					expandoTargets.Has(targetFunction->text())) {
					auto endNode = startNode(node, targetFunction);
					addSymbolForNode(target, propertyName,
									 getSymbolsForNode(definition));
					endNode();
				} else {
					node->forEachChild([&](::tsc::Node* child) {
						return visit(child);
					});
				}
				break;
			}
			default:
				break;
			}
			break;
		}
		case Kind::ExportAssignment:
			if (node->as<ExportAssignment>()->IsExportEquals) {
				addSymbolForNode(node, nullptr /*name*/,
								 getSymbolsForNode(node->expression()));
			} else {
				node->forEachChild([&](::tsc::Node* child) {
					return visit(child);
				});
			}
			break;
		default:
			node->forEachChild([&](::tsc::Node* child) {
				return visit(child);
			});
			break;
		}
		return false;
	};

	node->forEachChild([&](::tsc::Node* child) {
		return visit(child);
	});
	return mergeExpandos(symbols);
}

// getDocumentSymbolInformations converts hierarchical DocumentSymbols to a flat SymbolInformation array
// symbols.go:59
std::vector<lsp::lsproto::SymbolInformation>
LanguageService::getDocumentSymbolInformations(
	gostd::Context ctx, SourceFile* file,
	lsp::lsproto::DocumentUri documentURI) {
	// First get hierarchical symbols
	auto docSymbols = getDocumentSymbolsForChildren(ctx, file, file);
	return flattenDocumentSymbols(docSymbols, documentURI);
}

// symbols.go:25
lsp::lsproto::DocumentSymbolResponse LanguageService::ProvideDocumentSymbols(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI) {
	SourceFile* file = getProgramAndFile(documentURI).second;
	std::vector<SourceFile*> projections{file};
	if (const std::vector<SourceFile*>* supplemental =
			file->SupplementalSourceFiles();
		supplemental != nullptr) {
		projections.insert(projections.end(), supplemental->begin(),
						   supplemental->end());
	}
	std::vector<lsp::lsproto::DocumentSymbol*> symbols;
	std::unordered_set<docSymbolKey, docSymbolKeyHash> seen;
	for (auto* projection : projections) {
		for (auto* symbol :
			 getDocumentSymbolsForChildren(ctx, projection, projection)) {
			docSymbolKey key{symbol->Name, symbol->Kind, symbol->Range};
			if (seen.insert(key).second) {
				symbols.push_back(symbol);
			}
		}
	}
	lsp::lsproto::SymbolInformationsOrDocumentSymbolsOrNull out;
	if (lsp::lsproto::GetClientCapabilities(ctx)
			->TextDocument.DocumentSymbol.HierarchicalDocumentSymbolSupport) {
		out.DocumentSymbols =
			new std::vector<lsp::lsproto::DocumentSymbol*>(std::move(symbols));
		return out;
	}
	// Client doesn't support hierarchical document symbols, return flat SymbolInformation array
	auto symbolInfos = flattenDocumentSymbols(symbols, documentURI);
	auto* symbolInfoPtrs = new std::vector<lsp::lsproto::SymbolInformation*>;
	symbolInfoPtrs->reserve(symbolInfos.size());
	for (auto& info : symbolInfos) {
		symbolInfoPtrs->push_back(new lsp::lsproto::SymbolInformation(info));
	}
	out.SymbolInformations = symbolInfoPtrs;
	return out;
}

// ============================================================================
// symbols.go — ProvideWorkspaceSymbols
// ============================================================================
// symbols.go:546
std::pair<lsp::lsproto::WorkspaceSymbolResponse, gostd::Error>
ProvideWorkspaceSymbols(gostd::Context ctx,
						std::vector<compiler::SimpleProgram*> programs,
						lsconv::Converters* converters,
						lsutil::UserPreferences preferences, std::string query) {
	bool excludeLibrarySymbols =
		tristateIsTrue(preferences.ExcludeLibrarySymbolsInNavTo);
	// Obtain set of non-declaration source files from all active programs.
	std::unordered_map<tspath::Path, SourceFile*> sourceFiles;
	for (auto* program : programs) {
		for (auto* sourceFile : program->SourceFiles()) {
			if ((programHasTSFile(program) ||
				 !sourceFile->IsDeclarationFile) &&
				!shouldExcludeFile(sourceFile, program,
								   excludeLibrarySymbols)) {
				sourceFiles[sourceFile->Path()] = sourceFile;
			}
		}
	}
	// Create DeclarationInfos for all declarations in the source files.
	std::vector<DeclarationInfo> infos;
	for (auto& [_, sourceFile] : sourceFiles) {
		if (gostd::ctxErr(ctx) != nullptr) {
			return {lsp::lsproto::SymbolInformationsOrWorkspaceSymbolsOrNull{},
					nullptr};
		}
		const auto& declarationMap = getDeclarationMap(sourceFile);
		for (auto& [name, declarations] : declarationMap) {
			int score = getMatchScore(name, query);
			if (score >= 0) {
				for (auto* declaration : declarations) {
					infos.push_back(DeclarationInfo{name, declaration, score});
				}
			}
		}
	}
	// Sort the DeclarationInfos and return the top 256 matches.
	std::sort(infos.begin(), infos.end(),
			  [](const DeclarationInfo& a, const DeclarationInfo& b) {
				  return compareDeclarationInfos(a, b) < 0;
			  });
	size_t count = std::min(infos.size(), size_t(256));
	std::vector<lsp::lsproto::SymbolInformation*> symbols;
	symbols.reserve(count);
	for (size_t i = 0; i < count; i++) {
		auto& info = infos[i];
		::tsc::Node* node = info.declaration;
		SourceFile* sourceFile = getSourceFileOfNode(node);
		::tsc::Node* container = getContainerNode(info.declaration);
		std::string* containerName = nullptr;
		if (container != nullptr) {
			containerName = strPtrTo(getDeclarationName(container));
		}
		// Use the name node's span so that VS selects just the symbol name (matching
		// the TS5 navto behaviour). GetNameOfDeclaration is always non-nil here because
		// computeDeclarationMap only adds declarations whose GetDeclarationName (string
		// form) is non-empty, which implies a name node exists.
		::tsc::Node* nameNode = getNameOfDeclaration(node);
		int nameStart = astnav::getStartOfNode(nameNode, sourceFile,
											   false /*includeJsDoc*/);
		TextRange nameRange{static_cast<TextPos>(nameStart),
							static_cast<TextPos>(int(nameNode->end()))};
		auto [location, fidelity] = converters->ToLSPLocationForFeature(
			sourceFile, nameRange, spanmap::FeatureDocumentSymbols);
		if (!fidelity.IsSingleSegment()) {
			// The name has no counterpart in the original text, so there is nothing to navigate to.
			continue;
		}
		auto* symbol = new lsp::lsproto::SymbolInformation;
		symbol->Name = info.name;
		symbol->Kind = getSymbolKindFromNode(info.declaration);
		symbol->Location = location;
		symbol->ContainerName = containerName;
		symbols.push_back(symbol);
	}

	lsp::lsproto::SymbolInformationsOrWorkspaceSymbolsOrNull out;
	out.SymbolInformations =
		new std::vector<lsp::lsproto::SymbolInformation*>(std::move(symbols));
	return {out, nullptr};
}

} // namespace tsc::ls
