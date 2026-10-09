// === slice: ls-coreC ===
// semantictokens.cpp — semantictokens.go: semantic token classification and
// LSP relative encoding.
#include "internal/ls/ls.h"

namespace tsc::ls {

// ============================================================================
// semantictokens.go — token type/modifier tables
// ============================================================================
// semantictokens.go:21 — tokenTypes
static const std::vector<lsp::lsproto::SemanticTokenType> tokenTypes = {
	lsp::lsproto::SemanticTokenTypeNamespace,
	lsp::lsproto::SemanticTokenTypeClass,
	lsp::lsproto::SemanticTokenTypeEnum,
	lsp::lsproto::SemanticTokenTypeInterface,
	lsp::lsproto::SemanticTokenTypeStruct,
	lsp::lsproto::SemanticTokenTypeTypeParameter,
	lsp::lsproto::SemanticTokenTypeType,
	lsp::lsproto::SemanticTokenTypeParameter,
	lsp::lsproto::SemanticTokenTypeVariable,
	lsp::lsproto::SemanticTokenTypeProperty,
	lsp::lsproto::SemanticTokenTypeEnumMember,
	lsp::lsproto::SemanticTokenTypeDecorator,
	lsp::lsproto::SemanticTokenTypeEvent,
	lsp::lsproto::SemanticTokenTypeFunction,
	lsp::lsproto::SemanticTokenTypeMethod,
	lsp::lsproto::SemanticTokenTypeMacro,
	lsp::lsproto::SemanticTokenTypeLabel,
	lsp::lsproto::SemanticTokenTypeComment,
	lsp::lsproto::SemanticTokenTypeString,
	lsp::lsproto::SemanticTokenTypeKeyword,
	lsp::lsproto::SemanticTokenTypeNumber,
	lsp::lsproto::SemanticTokenTypeRegexp,
	lsp::lsproto::SemanticTokenTypeOperator,
};

// semantictokens.go:48 — tokenModifiers
static const std::vector<lsp::lsproto::SemanticTokenModifier> tokenModifiers = {
	lsp::lsproto::SemanticTokenModifierDeclaration,
	lsp::lsproto::SemanticTokenModifierDefinition,
	lsp::lsproto::SemanticTokenModifierReadonly,
	lsp::lsproto::SemanticTokenModifierStatic,
	lsp::lsproto::SemanticTokenModifierDeprecated,
	lsp::lsproto::SemanticTokenModifierAbstract,
	lsp::lsproto::SemanticTokenModifierAsync,
	lsp::lsproto::SemanticTokenModifierModification,
	lsp::lsproto::SemanticTokenModifierDocumentation,
	lsp::lsproto::SemanticTokenModifierDefaultLibrary,
	"local",
};

// semantictokens.go:62 — tokenType
using tokenType = int;
inline constexpr tokenType tokenTypeNamespace = 0;
inline constexpr tokenType tokenTypeClass = 1;
inline constexpr tokenType tokenTypeEnum = 2;
inline constexpr tokenType tokenTypeInterface = 3;
inline constexpr tokenType tokenTypeStruct = 4;
inline constexpr tokenType tokenTypeTypeParameter = 5;
inline constexpr tokenType tokenTypeType = 6;
inline constexpr tokenType tokenTypeParameter = 7;
inline constexpr tokenType tokenTypeVariable = 8;
inline constexpr tokenType tokenTypeProperty = 9;
inline constexpr tokenType tokenTypeEnumMember = 10;
inline constexpr tokenType tokenTypeDecorator = 11;
inline constexpr tokenType tokenTypeEvent = 12;
inline constexpr tokenType tokenTypeFunction = 13;
inline constexpr tokenType tokenTypeMethod = 14; // Previously called "member" in TypeScript
inline constexpr tokenType tokenTypeMacro = 15;
inline constexpr tokenType tokenTypeLabel = 16;
inline constexpr tokenType tokenTypeComment = 17;
inline constexpr tokenType tokenTypeString = 18;
inline constexpr tokenType tokenTypeKeyword = 19;
inline constexpr tokenType tokenTypeNumber = 20;
inline constexpr tokenType tokenTypeRegexp = 21;
inline constexpr tokenType tokenTypeOperator = 22;

// semantictokens.go:90 — tokenModifier
using tokenModifier = int;
inline constexpr tokenModifier tokenModifierDeclaration = 1 << 0;
inline constexpr tokenModifier tokenModifierDefinition = 1 << 1;
inline constexpr tokenModifier tokenModifierReadonly = 1 << 2;
inline constexpr tokenModifier tokenModifierStatic = 1 << 3;
inline constexpr tokenModifier tokenModifierDeprecated = 1 << 4;
inline constexpr tokenModifier tokenModifierAbstract = 1 << 5;
inline constexpr tokenModifier tokenModifierAsync = 1 << 6;
inline constexpr tokenModifier tokenModifierModification = 1 << 7;
inline constexpr tokenModifier tokenModifierDocumentation = 1 << 8;
inline constexpr tokenModifier tokenModifierDefaultLibrary = 1 << 9;
inline constexpr tokenModifier tokenModifierLocal = 1 << 10;

// semantictokens.go:215 — semanticToken
struct semanticToken {
	::tsc::Node* node = nullptr;
	SourceFile* file = nullptr;
	tokenType type = 0;
	tokenModifier modifier = 0;
	bool operator==(const semanticToken&) const = default;
};
struct semanticTokenHash {
	size_t operator()(const semanticToken& t) const {
		return std::hash<::tsc::Node*>{}(t.node) * 131 +
			   std::hash<SourceFile*>{}(t.file) * 8191 +
			   std::hash<int>{}(t.type) * 31 + t.modifier;
	}
};

namespace {

// semantictokens.go:489 — getDeclarationForBindingElement
::tsc::Node* getDeclarationForBindingElement(::tsc::Node* element) {
	for (;;) {
		::tsc::Node* parent = element->parent;
		if (parent != nullptr && isBindingPattern(parent)) {
			::tsc::Node* grandparent = parent->parent;
			if (grandparent != nullptr && isBindingElement(grandparent)) {
				element = grandparent;
				continue;
			}
			return parent->parent;
		}
		return element;
	}
}

// semantictokens.go:504 — isInImportClause
bool isInImportClause(::tsc::Node* node) {
	::tsc::Node* parent = node->parent;
	return parent != nullptr &&
		   (isImportClause(parent) || isImportSpecifier(parent) ||
			isNamespaceImport(parent));
}

// semantictokens.go:509 — isExpressionInCallExpression
bool isExpressionInCallExpression(::tsc::Node* node) {
	while (isRightSideOfQualifiedNameOrPropertyAccess(node)) {
		node = node->parent;
	}
	::tsc::Node* parent = node->parent;
	return parent != nullptr && isCallExpression(parent) &&
		   parent->expression() == node;
}

// semantictokens.go:464 — isLocalDeclaration
bool isLocalDeclaration(::tsc::Node* decl, SourceFile* sourceFile) {
	if (isBindingElement(decl)) {
		decl = getDeclarationForBindingElement(decl);
	}
	if (isVariableDeclaration(decl)) {
		::tsc::Node* parent = decl->parent;
		// Check if this is a catch clause parameter
		if (parent != nullptr && isCatchClause(parent)) {
			return getSourceFileOfNode(decl) == sourceFile;
		}
		if (parent != nullptr && isVariableDeclarationList(parent)) {
			::tsc::Node* grandparent = parent->parent;
			if (grandparent != nullptr) {
				::tsc::Node* greatGrandparent = grandparent->parent;
				return (!isSourceFile(greatGrandparent) ||
						isCatchClause(grandparent)) &&
					   getSourceFileOfNode(decl) == sourceFile;
			}
		}
	} else if (isFunctionDeclaration(decl)) {
		::tsc::Node* parent = decl->parent;
		return parent != nullptr && !isSourceFile(parent) &&
			   getSourceFileOfNode(decl) == sourceFile;
	}
	return false;
}

// semantictokens.go:379 — tokenFromDeclarationMapping
tokenType tokenFromDeclarationMapping(Kind kind) {
	switch (kind) {
	case Kind::VariableDeclaration:
		return tokenTypeVariable;
	case Kind::Parameter:
		return tokenTypeParameter;
	case Kind::PropertyDeclaration:
		return tokenTypeProperty;
	case Kind::ModuleDeclaration:
		return tokenTypeNamespace;
	case Kind::EnumDeclaration:
		return tokenTypeEnum;
	case Kind::EnumMember:
		return tokenTypeEnumMember;
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
		return tokenTypeClass;
	case Kind::MethodDeclaration:
		return tokenTypeMethod;
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
		return tokenTypeFunction;
	case Kind::MethodSignature:
		return tokenTypeMethod;
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		return tokenTypeProperty;
	case Kind::PropertySignature:
		return tokenTypeProperty;
	case Kind::InterfaceDeclaration:
		return tokenTypeInterface;
	case Kind::TypeAliasDeclaration:
		return tokenTypeType;
	case Kind::TypeParameter:
		return tokenTypeTypeParameter;
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
		return tokenTypeProperty;
	default:
		return -1;
	}
}

// semantictokens.go:342 — classifySymbol
std::pair<tokenType, bool> classifySymbol(Symbol* symbol,
										  SemanticMeaning meaning) {
	SymbolFlags flags = symbol->flags;
	if (flags & SymbolFlagsClass) {
		return {tokenTypeClass, true};
	}
	if (flags & SymbolFlagsEnum) {
		return {tokenTypeEnum, true};
	}
	if (flags & SymbolFlagsTypeAlias) {
		return {tokenTypeType, true};
	}
	if (flags & SymbolFlagsInterface) {
		if (meaning & SemanticMeaningType) {
			return {tokenTypeInterface, true};
		}
	}
	if (flags & SymbolFlagsTypeParameter) {
		return {tokenTypeTypeParameter, true};
	}

	// Check the value declaration
	::tsc::Node* decl = symbol->data->valueDeclaration;
	if (decl == nullptr && !symbol->data->declarations.empty()) {
		decl = symbol->data->declarations[0];
	}
	if (decl != nullptr) {
		if (isBindingElement(decl)) {
			decl = getDeclarationForBindingElement(decl);
		}
		if (tokenType tt = tokenFromDeclarationMapping(decl->kind); tt >= 0) {
			return {tt, true};
		}
	}

	return {0, false};
}

// semantictokens.go:418 — reclassifyByType
tokenType reclassifyByType(checker::Checker* c, ::tsc::Node* node,
						   tokenType tt) {
	// Type-based reclassification for variables, properties, and parameters
	if (tt == tokenTypeVariable || tt == tokenTypeProperty ||
		tt == tokenTypeParameter) {
		checker::Type* typ = c->GetTypeAtLocation(node);
		if (typ != nullptr) {
			auto test = [&](const std::function<bool(checker::Type*)>&
								condition) -> bool {
				if (condition(typ)) {
					return true;
				}
				if (typ->flags & checker::TypeFlagsUnion) {
					for (auto* t : typ->AsUnionType()->types) {
						if (condition(t)) {
							return true;
						}
					}
				}
				return false;
			};

			// Check for constructor signatures (class-like)
			if (tt != tokenTypeParameter && test([&](checker::Type* t) {
					return !c->GetSignaturesOfType(
								   t, checker::SignatureKind::Construct)
								.empty();
				})) {
				return tokenTypeClass;
			}

			// Check for call signatures (function-like)
			// Must have call signatures AND (no properties OR be used in call context)
			bool hasCallSignatures = test([&](checker::Type* t) {
				return !c->GetSignaturesOfType(t, checker::SignatureKind::Call)
							.empty();
			});
			if (hasCallSignatures) {
				bool hasNoProperties =
					!test([](checker::Type* t) {
						checker::ObjectType* objType = t->AsObjectType();
						return objType != nullptr &&
							   !objType->properties.empty();
					});
				if (hasNoProperties || isExpressionInCallExpression(node)) {
					if (tt == tokenTypeProperty) {
						return tokenTypeMethod;
					}
					return tokenTypeFunction;
				}
			}
		}
	}
	return tt;
}

// semantictokens.go:210 — semanticTokenLSPRange
std::pair<lsp::lsproto::Range, spanmap::Fidelity> semanticTokenLSPRange(
	const semanticToken& token, lsconv::Converters* converters) {
	int start =
		tsc::getTokenPosOfNode(token.node, token.file, false);
	return converters->ToLSPRangeForFeature(
		token.file,
		TextRange{static_cast<TextPos>(start),
				  static_cast<TextPos>(int(token.node->end()))},
		spanmap::FeatureSemanticTokens);
}

// semantictokens.go:193 — sortSemanticTokens
void sortSemanticTokens(std::vector<semanticToken>& tokens,
						lsconv::Converters* converters) {
	std::stable_sort(tokens.begin(), tokens.end(),
			  [&](const semanticToken& a, const semanticToken& b) {
				  auto [aRange, _a] = semanticTokenLSPRange(a, converters);
				  auto [bRange, _b] = semanticTokenLSPRange(b, converters);
				  if (aRange.Start.Line != bRange.Start.Line) {
					  return aRange.Start.Line < bRange.Start.Line;
				  }
				  if (aRange.Start.Character != bRange.Start.Character) {
					  return aRange.Start.Character < bRange.Start.Character;
				  }
				  if (a.file->Path() != b.file->Path()) {
					  return a.file->Path() < b.file->Path();
				  }
				  return a.node->pos() < b.node->pos();
			  });
}

// semantictokens.go:523 — encodeSemanticTokens
std::vector<uint32_t> encodeSemanticTokens(gostd::Context ctx,
										   const std::vector<semanticToken>& tokens,
										   lsconv::Converters* converters) {
	// Build mapping from server token types/modifiers to client indices
	std::unordered_map<tokenType, uint32_t> typeMapping;
	std::unordered_map<lsp::lsproto::SemanticTokenModifier, uint32_t>
		modifierMapping;

	const lsp::lsproto::ResolvedSemanticTokensClientCapabilities* clientCapabilities =
		&lsp::lsproto::getClientCapabilities(ctx)->TextDocument.SemanticTokens;

	// Map server token types to client-supported indices
	uint32_t clientIdx = 0;
	for (size_t i = 0; i < tokenTypes.size(); i++) {
		const auto& serverType = tokenTypes[i];
		if (std::find(clientCapabilities->TokenTypes->begin(),
					  clientCapabilities->TokenTypes->end(),
					  std::string(serverType)) !=
			clientCapabilities->TokenTypes->end()) {
			typeMapping[tokenType(i)] = clientIdx;
			clientIdx++;
		}
	}

	// Map server token modifiers to client-supported bit positions
	uint32_t clientBit = 0;
	for (auto& serverModifier : tokenModifiers) {
		if (std::find(clientCapabilities->TokenModifiers->begin(),
					  clientCapabilities->TokenModifiers->end(),
					  std::string(serverModifier)) !=
			clientCapabilities->TokenModifiers->end()) {
			modifierMapping[serverModifier] = clientBit;
			clientBit++;
		}
	}

	// Each token encodes 5 uint32 values: deltaLine, deltaChar, length, tokenType, tokenModifiers
	std::vector<uint32_t> encoded;
	encoded.reserve(tokens.size() * 5);
	uint32_t prevLine = 0;
	uint32_t prevChar = 0;

	for (auto& token : tokens) {
		// Skip tokens with types not supported by the client
		auto typeIt = typeMapping.find(token.type);
		if (typeIt == typeMapping.end()) {
			continue;
		}
		uint32_t clientTypeIdx = typeIt->second;

		// Map modifiers to client-supported bit mask
		uint32_t clientModifierMask = 0;
		for (size_t i = 0; i < tokenModifiers.size(); i++) {
			if (token.modifier & (1 << i)) {
				if (auto it = modifierMapping.find(tokenModifiers[i]);
					it != modifierMapping.end()) {
					clientModifierMask |= 1u << it->second;
				}
			}
		}

		// Semantic tokens must describe one concrete source segment; synthesized and cross-segment
		// tokens do not identify a coherent token in the original text.
		auto [lspRange, fidelity] = semanticTokenLSPRange(token, converters);
		if (!fidelity.IsExact()) {
			continue;
		}
		lsp::lsproto::Position startPos = lspRange.Start;
		lsp::lsproto::Position endPos = lspRange.End;

		// Length is the character difference when on the same line
		uint32_t tokenLength;
		if (startPos.Line == endPos.Line) {
			tokenLength = endPos.Character - startPos.Character;
		} else {
			TSC_UNREACHABLE(
				"semantic tokens: token spans multiple lines");
		}

		uint32_t line = startPos.Line;
		uint32_t ch = startPos.Character;

		// Multiple virtual projections can describe the same original token; LSP requires one entry per
		// start position, so retain the first after sorting.
		if (!encoded.empty() && line == prevLine && ch == prevChar) {
			continue;
		}
		if (!encoded.empty() &&
			(line < prevLine || (line == prevLine && ch < prevChar))) {
			TSC_UNREACHABLE(
				"semantic tokens: positions must be strictly increasing");
		}

		// Encode as: [deltaLine, deltaChar, length, tokenType, tokenModifiers]
		uint32_t deltaLine = line - prevLine;
		uint32_t deltaChar;
		if (deltaLine == 0) {
			deltaChar = ch - prevChar;
		} else {
			deltaChar = ch;
		}

		encoded.push_back(deltaLine);
		encoded.push_back(deltaChar);
		encoded.push_back(tokenLength);
		encoded.push_back(clientTypeIdx);
		encoded.push_back(clientModifierMask);

		prevLine = line;
		prevChar = ch;
	}

	return encoded;
}

} // namespace

// ============================================================================
// semantictokens.go — collectSemanticTokens / collectSemanticTokensInRange
// ============================================================================
// semantictokens.go:222
std::vector<semanticToken> LanguageService::collectSemanticTokens(
	gostd::Context ctx, checker::Checker* c, SourceFile* file,
	compiler::SimpleProgram* program) {
	return collectSemanticTokensInRange(ctx, c, file, program, int(file->pos()),
										int(file->end()));
}

// semantictokens.go:226
std::vector<semanticToken> LanguageService::collectSemanticTokensInRange(
	gostd::Context ctx, checker::Checker* c, SourceFile* file,
	compiler::SimpleProgram* program, int spanStart, int spanEnd) {
	std::vector<semanticToken> tokens;

	bool inJSXElement = false;

	std::function<bool(::tsc::Node*)> visit = [&](::tsc::Node* node) -> bool {
		// Check for cancellation
		if (gostd::ctxErr(ctx) != nullptr) {
			return false;
		}

		if (node == nullptr) {
			return false;
		}
		if (node->flags & NodeFlagsReparsed) {
			return false;
		}
		TextPos nodeEnd = node->end();
		if (int(node->pos()) >= spanEnd || int(nodeEnd) <= spanStart) {
			return false;
		}

		bool prevInJSXElement = inJSXElement;
		if (isJsxElement(node) || isJsxSelfClosingElement(node)) {
			inJSXElement = true;
		} else if (isJsxExpression(node)) {
			inJSXElement = false;
		}

		if ((isIdentifier(node) || isPrivateIdentifier(node)) &&
			!node->text().empty() && !inJSXElement &&
			!isInImportClause(node) && !isInfinityOrNaNString(node->text())) {
			Symbol* symbol = c->GetSymbolAtLocation(node);
			if (symbol != nullptr) {
				// Resolve aliases
				if (symbol->flags & SymbolFlagsAlias) {
					symbol = c->GetAliasedSymbol(symbol);
				}

				auto [tt, ok] =
					classifySymbol(symbol, getMeaningFromLocation(node));
				if (ok) {
					tokenModifier mod = 0;

					// Check if this is a declaration
					::tsc::Node* parent = node->parent;
					if (parent != nullptr) {
						bool parentIsDeclaration =
							isBindingElement(parent) ||
							tokenFromDeclarationMapping(parent->kind) == tt;
						if (parentIsDeclaration && parent->name() == node) {
							mod |= tokenModifierDeclaration;
						}
					}

					// Property declaration in constructor: reclassify parameters as properties in property access context
					if (tt == tokenTypeParameter &&
						isRightSideOfQualifiedNameOrPropertyAccess(node)) {
						tt = tokenTypeProperty;
					}

					// Type-based reclassification
					tt = reclassifyByType(c, node, tt);

					// Get the value declaration to check modifiers
					if (::tsc::Node* decl = symbol->data->valueDeclaration;
						decl != nullptr) {
						ModifierFlags modifiers = getCombinedModifierFlags(decl);
						NodeFlags nodeFlags = getCombinedNodeFlags(decl);

						if (modifiers & ModifierFlagsStatic) {
							mod |= tokenModifierStatic;
						}
						if (modifiers & ModifierFlagsAsync) {
							mod |= tokenModifierAsync;
						}
						if (tt != tokenTypeClass && tt != tokenTypeInterface) {
							if ((modifiers & ModifierFlagsReadonly) ||
								(nodeFlags & NodeFlagsConst) ||
								(symbol->flags & SymbolFlagsEnumMember)) {
								mod |= tokenModifierReadonly;
							}
						}
						if ((tt == tokenTypeVariable || tt == tokenTypeFunction) &&
							isLocalDeclaration(decl, file)) {
							mod |= tokenModifierLocal;
						}
						SourceFile* declSourceFile = getSourceFileOfNode(decl);
						if (declSourceFile != nullptr &&
							program->IsSourceFileDefaultLibrary(
								declSourceFile->Path())) {
							mod |= tokenModifierDefaultLibrary;
						}
					} else if (!symbol->data->declarations.empty()) {
						for (auto* decl : symbol->data->declarations) {
							SourceFile* declSourceFile =
								getSourceFileOfNode(decl);
							if (declSourceFile != nullptr &&
								program->IsSourceFileDefaultLibrary(
									declSourceFile->Path())) {
								mod |= tokenModifierDefaultLibrary;
								break;
							}
						}
					}

					tokens.push_back(semanticToken{
						node,
						nullptr,
						tt,
						mod,
					});
				}
			}
		}

		node->forEachChild([&](::tsc::Node* child) {
			visit(child);
			return false;
		});
		inJSXElement = prevInJSXElement;
		return false;
	};

	visit(file);

	// Check for cancellation after collection
	if (gostd::ctxErr(ctx) != nullptr) {
		return {};
	}

	return tokens;
}

// ============================================================================
// semantictokens.go — ProvideSemanticTokens / ProvideSemanticTokensRange
// ============================================================================
// semantictokens.go:128
lsp::lsproto::SemanticTokensResponse LanguageService::ProvideSemanticTokens(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI) {
	auto [program, file] = getProgramAndFile(documentURI);

	const std::vector<SourceFile*>* supplemental =
		file->SupplementalSourceFiles();
	std::vector<SourceFile*> files;
	files.reserve(1 + (supplemental != nullptr ? supplemental->size() : 0));
	files.push_back(file);
	if (supplemental != nullptr) {
		files.insert(files.end(), supplemental->begin(), supplemental->end());
	}
	std::vector<semanticToken> tokens;
	for (auto* projection : files) {
		auto [c, done] = program->GetTypeChecker(ctx);
		for (auto token : collectSemanticTokens(ctx, c, projection, program)) {
			token.file = projection;
			tokens.push_back(token);
		}
		done();
	}
	sortSemanticTokens(tokens, converters);

	lsp::lsproto::SemanticTokensOrNull out;
	if (tokens.empty()) {
		return out;
	}

	// Convert to LSP format (relative encoding)
	std::vector<uint32_t> encoded = encodeSemanticTokens(ctx, tokens, converters);

	auto semanticTokens = std::make_shared<lsp::lsproto::SemanticTokens>();
	semanticTokens->Data = std::move(encoded);
	out.SemanticTokens = semanticTokens;
	return out;
}

// semantictokens.go:160
lsp::lsproto::SemanticTokensRangeResponse LanguageService::ProvideSemanticTokensRange(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
	lsp::lsproto::Range rng) {
	auto [program, file] = getProgramAndFile(documentURI);

	auto mappedRanges = converters->FromLSPRangeIntersectingForSourceFile(
		file, rng, spanmap::FeatureSemanticTokens);
	std::vector<semanticToken> tokens;
	std::unordered_set<semanticToken, semanticTokenHash> seen;
	for (auto& mapped : mappedRanges) {
		SourceFile* projection = mapped.Script;
		auto [c, done] = program->GetTypeChecker(ctx);
		for (auto token : collectSemanticTokensInRange(
				 ctx, c, projection, program, int(mapped.Span.pos()),
				 int(mapped.Span.end()))) {
			token.file = projection;
			if (seen.insert(token).second) {
				tokens.push_back(token);
			}
		}
		done();
	}
	sortSemanticTokens(tokens, converters);

	lsp::lsproto::SemanticTokensOrNull out;
	if (tokens.empty()) {
		return out;
	}

	std::vector<uint32_t> encoded = encodeSemanticTokens(ctx, tokens, converters);

	auto semanticTokens = std::make_shared<lsp::lsproto::SemanticTokens>();
	semanticTokens->Data = std::move(encoded);
	out.SemanticTokens = semanticTokens;
	return out;
}

// ============================================================================
// semantictokens.go — SemanticTokensLegend
// ============================================================================
// semantictokens.go:109
lsp::lsproto::SemanticTokensLegend* SemanticTokensLegend(
	lsp::lsproto::ResolvedSemanticTokensClientCapabilities clientCapabilities) {
	// slices.Contains(nil, x) is false in Go — a nullopt Slice matches no
	// entries, so the guards below both avoid UB and keep that behavior.
	std::vector<std::string> types;
	types.reserve(tokenTypes.size());
	if (clientCapabilities.TokenTypes) {
		for (auto& t : tokenTypes) {
			if (std::find(clientCapabilities.TokenTypes->begin(),
						  clientCapabilities.TokenTypes->end(),
						  std::string(t)) !=
				clientCapabilities.TokenTypes->end()) {
				types.push_back(std::string(t));
			}
		}
	}
	std::vector<std::string> modifiers;
	modifiers.reserve(tokenModifiers.size());
	if (clientCapabilities.TokenModifiers) {
		for (auto& m : tokenModifiers) {
			if (std::find(clientCapabilities.TokenModifiers->begin(),
						  clientCapabilities.TokenModifiers->end(),
						  std::string(m)) !=
				clientCapabilities.TokenModifiers->end()) {
				modifiers.push_back(std::string(m));
			}
		}
	}
	auto* legend = new lsp::lsproto::SemanticTokensLegend;
	legend->TokenTypes = std::move(types);
	legend->TokenModifiers = std::move(modifiers);
	return legend;
}

} // namespace tsc::ls
