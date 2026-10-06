// === slice: ls-coreC ===
// hover.cpp — hover.go: LSP hover (quickinfo + documentation + VS adornments).
#include "internal/ls/ls.h"

#include "internal/astnav/tokens.h"
#include "internal/debug/debug.h"
#include "internal/nodebuilder/types.h"
#include "internal/printer/printer.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::ls {

namespace {

// hover.go:22 — symbolFormatFlags / typeFormatFlags
constexpr checker::SymbolFormatFlags symbolFormatFlags =
	checker::SymbolFormatFlagsWriteTypeParametersOrArguments |
	checker::SymbolFormatFlagsUseOnlyExternalAliasing |
	checker::SymbolFormatFlagsAllowAnyNodeKind |
	checker::SymbolFormatFlagsUseAliasDefinedOutsideCurrentScope;
constexpr checker::TypeFormatFlags typeFormatFlags =
	checker::TypeFormatFlagsUseAliasDefinedOutsideCurrentScope |
	checker::TypeFormatFlagsUseInstantiationExpressions;

// ---------------------------------------------------------------------------
// ast helpers not yet in cpp/internal/ast (file-local, faithful ports)
// ---------------------------------------------------------------------------

// utilities.go:2336 — IsBreakOrContinueStatement
bool isBreakOrContinueStatement(::tsc::Node* node) {
	return node->kind == Kind::BreakStatement ||
		   node->kind == Kind::ContinueStatement;
}

// utilities.go:2326 — IsJumpStatementTarget
bool isJumpStatementTarget(::tsc::Node* node) {
	if (!isIdentifier(node)) {
		return false;
	}
	if (!isBreakOrContinueStatement(node->parent)) {
		return false;
	}
	return node == node->parent->label();
}

// utilities.go:2316 — IsLabelOfLabeledStatement
bool isLabelOfLabeledStatement(::tsc::Node* node) {
	if (!isIdentifier(node)) {
		return false;
	}
	if (!isLabeledStatement(node->parent)) {
		return false;
	}
	return node == node->parent->label();
}

// utilities.go:2312 — IsLabelName
bool isLabelName(::tsc::Node* node) {
	return isLabelOfLabeledStatement(node) || isJumpStatementTarget(node);
}

// utilities.go:2134 — IsJSDocTag
bool isJSDocTag(::tsc::Node* node) {
	return node->kind >= KindFirstJSDocTagNode &&
		   node->kind <= KindLastJSDocTagNode;
}

// utilities.go:4525 — IsTagName
bool isTagName(::tsc::Node* node) {
	return node->parent != nullptr && isJSDocTag(node->parent) &&
		   node->parent->tagName() == node;
}

// ---------------------------------------------------------------------------
// hover.go helpers
// ---------------------------------------------------------------------------

// forward decls (mutual recursion)
void writeCode(std::string* b, std::string_view lang,
			   std::string_view code);

// hover.go:402 — formatQuickInfo
std::string formatQuickInfo(std::string_view quickInfo) {
	std::string b;
	b.reserve(quickInfo.size() + 32);
	writeCode(&b, "typescript", quickInfo);
	return b;
}

// hover.go:1027 — writeCode
void writeCode(std::string* b, std::string_view lang,
			   std::string_view code) {
	if (code.empty()) {
		return;
	}
	int ticks = 3;
	while (code.find(std::string(ticks, '`')) != std::string_view::npos) {
		ticks++;
	}
	for (int i = 0; i < ticks; i++) {
		b->push_back('`');
	}
	b->append(lang);
	b->push_back('\n');
	b->append(code);
	b->push_back('\n');
	for (int i = 0; i < ticks; i++) {
		b->push_back('`');
	}
	b->push_back('\n');
}

// hover.go:406 — shouldGetType
bool shouldGetType(::tsc::Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
		// If we're in a JSDoc node with no associated symbol, no binding has taken place for the node and
		// we can't answer questions about types of declaration nodes (such as property declarations).
		return !((node->flags & NodeFlagsJSDoc) != 0 &&
				 isDeclarationName(node)) &&
			   !isLabelName(node) && !isTagName(node) &&
			   !isConstTypeReference(node->parent);
	case Kind::ThisKeyword:
	case Kind::ThisType:
	case Kind::SuperKeyword:
	case Kind::NamedTupleMember:
		return true;
	case Kind::MetaProperty:
		return isImportMeta(node);
	default:
		return false;
	}
}

// hover.go:943 — typeParameterToString. Renders a type parameter declaration
// (e.g., "T extends FooType").
std::string typeParameterToString(checker::Checker* c, checker::Type* t,
								  ::tsc::Node* enclosingDeclaration,
								  checker::VerbosityContext* vc) {
	return c->TypeParameterToStringEx(t, enclosingDeclaration, vc);
}

// hover.go:997 — getCallOrNewExpression
::tsc::Node* getCallOrNewExpression(::tsc::Node* node) {
	if (isSourceFile(node)) {
		return nullptr;
	}
	if (isPropertyAccessExpression(node->parent) &&
		node->parent->name() == node) {
		node = node->parent;
	}
	if ((isCallExpression(node->parent) || isNewExpression(node->parent)) &&
		node->parent->expression() == node) {
		return node->parent;
	}
	return nullptr;
}

// hover.go:982 — getSignaturesAtLocation
std::vector<checker::Signature*> getSignaturesAtLocation(
	checker::Checker* c, ::tsc::Symbol* symbol, checker::SignatureKind kind,
	::tsc::Node* node) {
	std::vector<checker::Signature*> signatures = c->GetSignaturesOfType(
		c->RemoveMissingOrUndefinedType(c->GetTypeOfSymbol(symbol)), kind);
	if (signatures.size() > 1 ||
		(signatures.size() == 1 && !signatures[0]->typeParameters.empty())) {
		if (::tsc::Node* callNode = getCallOrNewExpression(node)) {
			// We have a call or new expression, return the resolved signature
			return {c->GetResolvedSignature(callNode)};
		}
	}
	return signatures;
}

// hover.go:426 — getQuickInfoAndDeclarationAtLocation. Builds classified
// display parts using displayPartsWriter when vsCapability is true. When
// vsCapability is false, it still builds the plain text string but skips
// classification runs.
symbolDisplayInfo getQuickInfoAndDeclarationAtLocation(
	checker::Checker* c, ::tsc::Symbol* symbol, ::tsc::Node* node,
	checker::VerbosityContext* vc, bool vsCapability,
	SemanticMeaning meaning) {
	::tsc::Node* container = getContainerNode(node);
	if (vc == nullptr) {
		vc = new checker::VerbosityContext{};
	}
	displayPartsWriter* dpw = newDisplayPartsWriter(vsCapability);

	// Source file for printer context
	SourceFile* sourceFile = nullptr;
	if (node != nullptr) {
		sourceFile = getSourceFileOfNode(node);
	}

	// nodeBuilderFlags for classified output (same as signatureHelpNodeBuilderFlags)
	constexpr nodebuilder::Flags classifiedNodeBuilderFlags =
		nodebuilder::FlagsIgnoreErrors |
		nodebuilder::FlagsUseAliasDefinedOutsideCurrentScope |
		nodebuilder::FlagsWriteTypeParametersInQualifiedName;

	// writeTypeClassified writes a type to dpw with proper classification (punctuation, symbols, keywords).
	// Falls back to flat text when vsCapability is false or when TypeToTypeNode fails.
	auto writeTypeClassified = [&](checker::Type* t, ::tsc::Node* enclosing,
								   checker::TypeFormatFlags flags) {
		flags |= checker::TypeFormatFlagsMultilineObjectLiterals;
		if (!vsCapability) {
			dpw->Write(c->TypeToStringEx(t, enclosing, flags, vc));
			return;
		}
		printer::EmitContext* emitContext = printer::NewEmitContext();
		std::unordered_map<::tsc::Node*, ::tsc::Symbol*> idToSymbol;
		checker::NodeBuilder* nb = c->getNodeBuilderEx(&idToSymbol);
		nodebuilder::Flags combinedFlags =
			nodebuilder::Flags(flags &
							   checker::TypeFormatFlagsNodeBuilderFlagsMask) |
			classifiedNodeBuilderFlags;
		::tsc::Node* typeNode = nb->TypeToTypeNode(
			t, enclosing, combinedFlags, nodebuilder::InternalFlagsNone,
			nullptr);
		if (typeNode == nullptr) {
			dpw->Write(c->TypeToStringEx(t, enclosing, flags, vc));
			return;
		}
		printer::PrinterOptions options;
		options.NewLine = NewLineKind::LineFeed;
		printer::Printer* p = printer::NewPrinter(
			options, printer::PrintHandlers{}, emitContext);
		p->IdToSymbol = idToSymbol;
		displayPartsWriter* tempDpw = newDisplayPartsWriter(true);
		p->Write(typeNode, sourceFile, tempDpw, nullptr);
		dpw->WriteFrom(tempDpw);
	};

	// writeSignatureClassified writes a signature to dpw with proper classification.
	auto writeSignatureClassified = [&](checker::Signature* sig,
										::tsc::Node* enclosing,
										checker::TypeFormatFlags flags) {
		flags |= checker::TypeFormatFlagsMultilineObjectLiterals;
		if (!vsCapability) {
			dpw->Write(c->SignatureToStringEx(sig, enclosing, flags, vc));
			return;
		}
		bool isConstructor =
			(sig->flags & checker::SignatureFlagsConstruct) != 0 &&
			(flags &
			 checker::TypeFormatFlagsWriteCallStyleSignature) == 0;
		Kind sigOutput;
		if ((flags & checker::TypeFormatFlagsWriteArrowStyleSignature) !=
			0) {
			if (isConstructor) {
				sigOutput = Kind::ConstructorType;
			} else {
				sigOutput = Kind::FunctionType;
			}
		} else {
			if (isConstructor) {
				sigOutput = Kind::ConstructSignature;
			} else {
				sigOutput = Kind::CallSignature;
			}
		}
		printer::EmitContext* emitContext = printer::NewEmitContext();
		std::unordered_map<::tsc::Node*, ::tsc::Symbol*> idToSymbol;
		checker::NodeBuilder* nb = c->getNodeBuilderEx(&idToSymbol);
		nodebuilder::Flags combinedFlags =
			nodebuilder::Flags(flags &
							   checker::TypeFormatFlagsNodeBuilderFlagsMask) |
			classifiedNodeBuilderFlags;
		::tsc::Node* sigNode = nb->SignatureToSignatureDeclaration(
			sig, sigOutput, enclosing, combinedFlags,
			nodebuilder::InternalFlagsNone, nullptr);
		if (sigNode == nullptr) {
			dpw->Write(c->SignatureToStringEx(sig, enclosing, flags, vc));
			return;
		}
		printer::PrinterOptions options;
		options.NewLine = NewLineKind::LineFeed;
		printer::Printer* p = printer::NewPrinter(
			options, printer::PrintHandlers{}, emitContext);
		p->IdToSymbol = idToSymbol;
		displayPartsWriter* tempDpw = newDisplayPartsWriter(true);
		p->Write(sigNode, sourceFile, tempDpw, nullptr);
		dpw->WriteFrom(tempDpw);
	};

	// writeSymbolClassified writes a symbol name to dpw with proper classification based on symbol flags.
	auto writeSymbolClassified = [&](::tsc::Symbol* symbol,
									 ::tsc::Node* enclosing,
									 SymbolFlags meaning,
									 checker::SymbolFormatFlags flags) {
		if (!vsCapability) {
			dpw->Write(
				c->SymbolToStringEx(symbol, enclosing, meaning, flags));
			return;
		}
		// Use WriteSymbol which calls classificationForSymbol to determine the correct classification
		std::string text =
			c->SymbolToStringEx(symbol, enclosing, meaning, flags);
		dpw->WriteSymbol(text, symbol);
	};

	auto writeModuleImportAttributes = [&](::tsc::Symbol* symbol) {
		::tsc::Node* declaration = nullptr;
		for (auto* d : symbol->declarations) {
			if (isModuleDeclaration(d) &&
				d->as<ModuleDeclaration>()->Attributes != nullptr) {
				declaration = d;
				break;
			}
		}
		if (declaration == nullptr) {
			return;
		}
		::tsc::Node* attributes =
			declaration->as<ModuleDeclaration>()->Attributes;
		printer::EmitContext* emitContext = printer::NewEmitContext();
		emitContext->setEmitFlags(attributes, printer::EFSingleLine);
		printer::PrinterOptions options;
		options.NewLine = NewLineKind::LineFeed;
		printer::Printer* p = printer::NewPrinter(
			options, printer::PrintHandlers{}, emitContext);
		displayPartsWriter* tempDpw = newDisplayPartsWriter(vsCapability);
		p->Write(attributes, getSourceFileOfNode(declaration),
				 tempDpw, nullptr);
		dpw->WriteKeyword(" with ");
		dpw->WriteFrom(tempDpw);
	};

	if ((node->kind == Kind::ThisKeyword && isInExpressionContext(node)) ||
		isThisInTypeQuery(node)) {
		dpw->WriteKeyword("this");
		dpw->WritePunctuation(": ");
		writeTypeClassified(c->GetTypeAtLocation(node), container,
							typeFormatFlags);
		return symbolDisplayInfo{dpw, nullptr};
	}
	if (symbol == nullptr) {
		if (shouldGetType(node)) {
			writeTypeClassified(c->GetTypeAtLocation(node), container,
								typeFormatFlags);
		}
		return symbolDisplayInfo{dpw, nullptr};
	}
	collections::Set<::tsc::Symbol*> visitedAliases;
	int aliasLevel = 0;
	::tsc::Node* firstDeclaration = nullptr;
	auto setDeclaration = [&](::tsc::Node* declaration) {
		if (firstDeclaration == nullptr) {
			firstDeclaration = declaration;
		}
	};
	auto writeNewLine = [&]() {
		if (!dpw->String().empty()) {
			dpw->Write("\n");
		}
		if (aliasLevel != 0) {
			dpw->WritePunctuation("(");
			dpw->Write("alias");
			dpw->WritePunctuation(") ");
		}
	};
	std::function<void(std::vector<checker::Signature*>, std::string, bool,
					   ::tsc::Symbol*)>
		writeSignatures;
	writeSignatures = [&](std::vector<checker::Signature*> signatures,
						  std::string prefix, bool parenthesized,
						  ::tsc::Symbol* symbol) {
		for (size_t i = 0; i < signatures.size(); i++) {
			checker::Signature* sig = signatures[i];
			writeNewLine();
			if (i == 3 && signatures.size() >= 5) {
				dpw->WriteComment("// +" +
								  std::to_string(signatures.size() - 3) +
								  " more overloads");
				break;
			}
			if (parenthesized) {
				dpw->WritePunctuation("(");
				dpw->Write(prefix);
				dpw->WritePunctuation(") ");
			} else {
				dpw->WriteKeyword(prefix);
			}
			writeSymbolClassified(symbol, container, SymbolFlagsNone,
								  symbolFormatFlags);
			if ((symbol->flags & SymbolFlagsOptional) != 0) {
				dpw->WritePunctuation("?");
			}
			writeSignatureClassified(
				sig, container,
				typeFormatFlags |
					checker::TypeFormatFlagsWriteCallStyleSignature |
					checker::TypeFormatFlagsWriteTypeArgumentsOfSignature);
		}
	};
	auto writeTypeParams = [&](std::vector<checker::Type*> params) {
		if (!params.empty()) {
			dpw->WritePunctuation("<");
			for (size_t i = 0; i < params.size(); i++) {
				checker::Type* tp = params[i];
				if (i != 0) {
					dpw->WritePunctuation(", ");
				}
				writeSymbolClassified(tp->symbol, nullptr,
									  SymbolFlagsNone, symbolFormatFlags);
				checker::Type* cons =
					c->GetConstraintOfTypeParameter(tp);
				if (cons != nullptr) {
					dpw->WriteKeyword(" extends ");
					writeTypeClassified(cons, nullptr, typeFormatFlags);
				}
				checker::Type* def = c->GetDefaultFromTypeParameter(tp);
				if (def != nullptr) {
					dpw->WriteOperator(" = ");
					writeTypeClassified(def, nullptr, typeFormatFlags);
				}
			}
			dpw->WritePunctuation(">");
		}
	};
	bool symbolWasExpanded = false;
	auto canExpandSymbol = [&](::tsc::Symbol* symbol) -> bool {
		if (vc == nullptr) {
			return false;
		}
		// Only offer symbol-level expansion for types that tryExpandSymbol handles:
		// class, interface, enum, namespace/module. For functions/variables/properties,
		// the node builder's probeTypeExpandability detects expandable type components.
		if ((symbol->flags & (SymbolFlagsClass | SymbolFlagsInterface |
							  SymbolFlagsNamespace)) == 0) {
			return false;
		}
		checker::Type* t = nullptr;
		if ((symbol->flags &
			 (SymbolFlagsClass | SymbolFlagsInterface)) != 0) {
			t = c->GetDeclaredTypeOfSymbol(symbol);
		} else {
			t = c->GetTypeOfSymbolAtLocation(symbol, node);
		}
		if (t == nullptr || c->IsLibTypeForHoverVerbosity(t)) {
			return false;
		}
		if (vc->Level > 0) {
			return true;
		}
		// At level 0, signal that expansion is possible but don't expand
		vc->CanIncreaseVerbosity = true;
		return false;
	};
	// tryExpandSymbol checks if a symbol can be expanded at the current verbosity level.
	auto tryExpandSymbol = [&](::tsc::Symbol* symbol,
							   SymbolFlags meaning) -> bool {
		if (symbolWasExpanded) {
			return true;
		}
		if (canExpandSymbol(symbol)) {
			checker::VerbosityContext expandVC;
			expandVC.Level = vc->Level - 1;
			expandVC.MaxTruncationLength = vc->MaxTruncationLength;
			std::string expanded =
				c->ExpandSymbolForHover(symbol, meaning, &expandVC);
			if (!expanded.empty()) {
				vc->CanIncreaseVerbosity =
					vc->CanIncreaseVerbosity ||
					expandVC.CanIncreaseVerbosity;
				vc->Truncated = vc->Truncated || expandVC.Truncated;
				dpw->Write(expanded);
				symbolWasExpanded = true;
				return true;
			}
		}
		return false;
	};
	std::function<void(::tsc::Symbol*)> writeSymbol;
	writeSymbol = [&](::tsc::Symbol* symbol) {
		// Recursively write all meanings of alias
		if ((symbol->flags & SymbolFlagsAlias) != 0 &&
			visitedAliases.AddIfAbsent(symbol)) {
			if (::tsc::Symbol* aliasedSymbol = c->GetAliasedSymbol(symbol);
				aliasedSymbol != c->GetUnknownSymbol()) {
				aliasLevel++;
				writeSymbol(aliasedSymbol);
				aliasLevel--;
			}
		}
		SymbolFlags flags = 0;
		switch (meaning) {
		case SemanticMeaningValue:
			flags = symbol->flags & (SymbolFlagsValue | SymbolFlagsSignature);
			break;
		case SemanticMeaningType:
			flags = symbol->flags & SymbolFlagsType;
			break;
		case SemanticMeaningNamespace:
			flags = symbol->flags & SymbolFlagsNamespace;
			break;
		default:
			flags = symbol->flags &
					(SymbolFlagsValue | SymbolFlagsSignature |
					 SymbolFlagsType | SymbolFlagsNamespace);
			break;
		}
		if (flags == 0) {
			if (aliasLevel != 0 || !dpw->String().empty()) {
				return;
			}
			flags = symbol->flags &
					(SymbolFlagsValue | SymbolFlagsSignature |
					 SymbolFlagsType | SymbolFlagsNamespace);
			if (flags == 0) {
				return;
			}
		}
		if ((flags & SymbolFlagsProperty) != 0 &&
			symbol->valueDeclaration != nullptr &&
			isMethodDeclaration(symbol->valueDeclaration)) {
			flags = SymbolFlagsMethod;
		}
		if ((flags &
			 (SymbolFlagsVariable | SymbolFlagsProperty |
			  SymbolFlagsAccessor)) != 0) {
			writeNewLine();
			if ((symbol->checkFlags & CheckFlagsIndexSymbol) == 0) {
				if ((flags & SymbolFlagsProperty) != 0) {
					dpw->WritePunctuation("(");
					dpw->Write("property");
					dpw->WritePunctuation(") ");
				} else if ((flags & SymbolFlagsAccessor) != 0) {
					dpw->WritePunctuation("(");
					dpw->Write("accessor");
					dpw->WritePunctuation(") ");
				} else {
					::tsc::Node* decl = symbol->valueDeclaration;
					if (decl != nullptr) {
						decl = getRootDeclaration(decl);
						if (isParameterDeclaration(decl)) {
							dpw->WritePunctuation("(");
							dpw->Write("parameter");
							dpw->WritePunctuation(") ");
						} else if (isVarLet(decl)) {
							dpw->WriteKeyword("let ");
						} else if (isVarConst(decl)) {
							dpw->WriteKeyword("const ");
						} else if (isVarUsing(decl)) {
							dpw->WriteKeyword("using ");
						} else if (isVarAwaitUsing(decl)) {
							dpw->WriteKeyword("await ");
							dpw->WriteKeyword("using ");
						} else {
							dpw->WriteKeyword("var ");
						}
					}
				}
				if (symbol->name == InternalSymbolNameExportEquals &&
					symbol->parent != nullptr &&
					(symbol->parent->flags & SymbolFlagsModule) != 0) {
					dpw->Write("exports");
				} else {
					writeSymbolClassified(symbol, container,
										  SymbolFlagsNone,
										  symbolFormatFlags);
				}
				if ((symbol->flags & SymbolFlagsOptional) != 0) {
					dpw->WritePunctuation("?");
				}
				dpw->WritePunctuation(": ");
			}
			if (::tsc::Node* callNode = getCallOrNewExpression(node)) {
				checker::TypeFormatFlags flags2 =
					typeFormatFlags |
					checker::TypeFormatFlagsWriteTypeArgumentsOfSignature |
					checker::TypeFormatFlagsWriteArrowStyleSignature;
				if (isCallExpression(callNode)) {
					flags2 |=
						checker::TypeFormatFlagsWriteCallStyleSignature;
				}
				writeSignatureClassified(
					c->GetResolvedSignature(callNode), container, flags2);
			} else {
				checker::Type* t =
					c->GetTypeOfSymbolAtLocation(symbol, node);
				// If the type is a constrained type parameter, support expansion:
				// Level 0: show just "T", signal canIncreaseVerbosity
				// Level 1+: show "T extends Constraint" with the constraint expanded at level-1
				if (vc != nullptr && t->symbol != nullptr &&
					(t->symbol->flags & SymbolFlagsTypeParameter) != 0 &&
					c->GetConstraintOfTypeParameter(t) != nullptr) {
					if (vc->Level > 0) {
						checker::VerbosityContext expandVC;
						expandVC.Level = vc->Level - 1;
						expandVC.MaxTruncationLength =
							vc->MaxTruncationLength;
						dpw->Write(typeParameterToString(c, t, container,
														 &expandVC));
						vc->CanIncreaseVerbosity =
							vc->CanIncreaseVerbosity ||
							expandVC.CanIncreaseVerbosity;
						vc->Truncated =
							vc->Truncated || expandVC.Truncated;
					} else {
						writeTypeClassified(t, container,
											typeFormatFlags);
						vc->CanIncreaseVerbosity = true;
					}
				} else {
					writeTypeClassified(t, container, typeFormatFlags);
				}
			}
			setDeclaration(
				symbol->valueDeclaration != nullptr
					? symbol->valueDeclaration
					: (symbol->declarations.empty()
						   ? nullptr
						   : symbol->declarations[0]));
		}
		if ((flags & SymbolFlagsEnumMember) != 0) {
			writeNewLine();
			dpw->WritePunctuation("(");
			dpw->Write("enum member");
			dpw->WritePunctuation(") ");
			checker::Type* t = c->GetTypeOfSymbol(symbol);
			writeTypeClassified(t, container, typeFormatFlags);
			if ((t->flags & checker::TypeFlagsLiteral) != 0) {
				dpw->WriteOperator(" = ");
				dpw->WriteLiteral(
					checker::ValueToString(t->AsLiteralType()->value));
			}
			setDeclaration(symbol->valueDeclaration);
		}
		if ((flags & (SymbolFlagsFunction | SymbolFlagsMethod)) != 0) {
			bool isMethod = (flags & SymbolFlagsMethod) != 0;
			std::string prefix = isMethod ? "method" : "function ";
			if (isIdentifier(node) &&
				(isFunctionLikeDeclaration(node->parent) ||
				 isMethodSignatureDeclaration(node->parent)) &&
				node->parent->name() == node &&
				std::find(symbol->declarations.begin(),
						  symbol->declarations.end(),
						  node->parent) != symbol->declarations.end()) {
				setDeclaration(node->parent);
				std::vector<checker::Signature*> signatures{
					c->GetSignatureFromDeclaration(node->parent)};
				writeSignatures(signatures, prefix, isMethod, symbol);
			} else {
				std::vector<checker::Signature*> signatures =
					getSignaturesAtLocation(c, symbol,
											checker::SignatureKind::Call,
											node);
				if (signatures.size() == 1) {
					if (::tsc::Node* d = signatures[0]->declaration;
						d != nullptr &&
						(d->flags & NodeFlagsJSDoc) == 0) {
						setDeclaration(d);
					}
				}
				writeSignatures(signatures, prefix, isMethod, symbol);
			}
			setDeclaration(symbol->valueDeclaration);
		}
		if ((flags & (SymbolFlagsClass | SymbolFlagsInterface)) != 0) {
			if (node->kind == Kind::ThisKeyword ||
				isThisInTypeQuery(node)) {
				writeNewLine();
				dpw->WriteKeyword("this");
			} else if (node->kind == Kind::ConstructorKeyword &&
					   (isConstructorDeclaration(node->parent) ||
						isConstructSignatureDeclaration(node->parent))) {
				setDeclaration(node->parent);
				std::vector<checker::Signature*> signatures{
					c->GetSignatureFromDeclaration(node->parent)};
				writeSignatures(signatures, "constructor ", false,
								symbol);
			} else {
				std::vector<checker::Signature*> signatures;
				if ((flags & SymbolFlagsClass) != 0 &&
					getCallOrNewExpression(node) != nullptr) {
					signatures = getSignaturesAtLocation(
						c, symbol, checker::SignatureKind::Construct,
						node);
				}
				if (signatures.size() == 1) {
					if (::tsc::Node* d = signatures[0]->declaration;
						d != nullptr &&
						(d->flags & NodeFlagsJSDoc) == 0) {
						setDeclaration(d);
					}
					writeSignatures(signatures, "constructor ", false,
									symbol);
				} else {
					writeNewLine();
					if ((flags & SymbolFlagsClass) != 0) {
						::tsc::Node* classExpression =
							getDeclarationOfKind(symbol,
												 Kind::ClassExpression);
						if (classExpression != nullptr) {
							// Local class expression: show "(local class)" prefix
							dpw->WritePunctuation("(");
							dpw->Write("local class");
							dpw->WritePunctuation(") ");
						}
						if (!tryExpandSymbol(symbol, flags)) {
							if (classExpression == nullptr) {
								bool hasAbstract = false;
								for (auto* d : symbol->declarations) {
									if (isClassDeclaration(d) &&
										hasAbstractModifier(d)) {
										hasAbstract = true;
										break;
									}
								}
								if (hasAbstract) {
									dpw->WriteKeyword("abstract ");
								}
								dpw->WriteKeyword("class ");
							}
							writeSymbolClassified(
								symbol, container, SymbolFlagsNone,
								symbolFormatFlags);
							auto params =
								checker::interfaceTypeLocalTypeParameters(
									c->GetDeclaredTypeOfSymbol(symbol)
										->AsInterfaceType());
							writeTypeParams(params);
						}
					} else {
						if (!tryExpandSymbol(symbol, flags)) {
							dpw->WriteKeyword("interface ");
							writeSymbolClassified(
								symbol, container, SymbolFlagsNone,
								symbolFormatFlags);
							auto params =
								checker::interfaceTypeLocalTypeParameters(
									c->GetDeclaredTypeOfSymbol(symbol)
										->AsInterfaceType());
							writeTypeParams(params);
						}
					}
				}
			}
			if ((flags & SymbolFlagsClass) != 0) {
				setDeclaration(symbol->valueDeclaration);
			} else {
				::tsc::Node* found = nullptr;
				for (auto* d : symbol->declarations) {
					if (isInterfaceDeclaration(d)) {
						found = d;
						break;
					}
				}
				setDeclaration(found);
			}
		}
		if ((flags & SymbolFlagsEnum) != 0) {
			writeNewLine();
			if (!tryExpandSymbol(symbol, flags)) {
				bool isConstEnum = false;
				for (auto* d : symbol->declarations) {
					if (isEnumDeclaration(d) && isEnumConst(d)) {
						isConstEnum = true;
						break;
					}
				}
				if (isConstEnum) {
					dpw->WriteKeyword("const ");
				}
				dpw->WriteKeyword("enum ");
				writeSymbolClassified(symbol, container,
									  SymbolFlagsNone, symbolFormatFlags);
			}
			::tsc::Node* found = nullptr;
			for (auto* d : symbol->declarations) {
				if (isEnumDeclaration(d)) {
					found = d;
					break;
				}
			}
			setDeclaration(found);
		}
		if ((flags & SymbolFlagsModule) != 0) {
			writeNewLine();
			if (!tryExpandSymbol(symbol, flags)) {
				bool isModule =
					symbol->valueDeclaration != nullptr &&
					(isSourceFile(symbol->valueDeclaration) ||
					 isAmbientModule(symbol->valueDeclaration));
				dpw->WriteKeyword(isModule ? "module " : "namespace ");
				writeSymbolClassified(symbol, container,
									  SymbolFlagsNone, symbolFormatFlags);
				writeModuleImportAttributes(symbol);
			}
			::tsc::Node* found = nullptr;
			for (auto* d : symbol->declarations) {
				if (isModuleDeclaration(d)) {
					found = d;
					break;
				}
			}
			setDeclaration(found);
		}
		if ((flags & SymbolFlagsTypeParameter) != 0) {
			writeNewLine();
			dpw->WritePunctuation("(");
			dpw->Write("type parameter");
			dpw->WritePunctuation(") ");
			if (isIdentifier(node) &&
				isTypeReferenceNode(node->parent) &&
				checker::IsDistributedTypeParameter(
					c->GetTypeAtLocation(node->parent))) {
				dpw->WritePunctuation("(");
				dpw->Write("distributed");
				dpw->WritePunctuation(") ");
			}
			checker::Type* tp = c->GetDeclaredTypeOfSymbol(symbol);
			writeSymbolClassified(symbol, container, SymbolFlagsNone,
								  symbolFormatFlags);
			checker::Type* cons = c->GetConstraintOfTypeParameter(tp);
			if (cons != nullptr) {
				dpw->WriteKeyword(" extends ");
				writeTypeClassified(cons, container, typeFormatFlags);
			}
			// Show context: "in ClassName<T>" or "in funcName<T>(...)"
			if (symbol->parent != nullptr) {
				// Class/Interface type parameter
				dpw->WriteKeyword(" in ");
				writeSymbolClassified(symbol->parent, container,
									  SymbolFlagsNone,
									  symbolFormatFlags);
				if (checker::Type* parentType =
						c->GetDeclaredTypeOfSymbol(symbol->parent);
					parentType->AsInterfaceType() != nullptr) {
					auto parentParams =
						checker::interfaceTypeLocalTypeParameters(
							parentType->AsInterfaceType());
					writeTypeParams(parentParams);
				}
			} else {
				// Method/function type parameter
				::tsc::Node* decl =
					getDeclarationOfKind(symbol, Kind::TypeParameter);
				if (decl != nullptr && decl->parent != nullptr) {
					::tsc::Node* declaration = decl->parent;
					if (isFunctionLike(declaration)) {
						dpw->WriteKeyword(" in ");
						if (declaration->kind ==
							Kind::ConstructSignature) {
							dpw->WriteKeyword("new ");
						} else if (declaration->kind !=
									   Kind::CallSignature &&
								   declaration->name() != nullptr) {
							writeSymbolClassified(
								declaration->symbol(), container,
								SymbolFlagsNone, symbolFormatFlags);
						}
						checker::Signature* sig =
							c->GetSignatureFromDeclaration(declaration);
						if (sig != nullptr) {
							writeSignatureClassified(
								sig, container,
								typeFormatFlags |
									checker::
										TypeFormatFlagsWriteTypeArgumentsOfSignature);
						}
					} else if (isTypeAliasDeclaration(declaration)) {
						dpw->WriteKeyword(" in ");
						dpw->WriteKeyword("type ");
						writeSymbolClassified(declaration->symbol(),
											  container, SymbolFlagsNone,
											  symbolFormatFlags);
						if (::tsc::Symbol* declSymbol =
								declaration->symbol();
							declSymbol != nullptr) {
							auto taParams =
								c->getTypeAliasTypeParameters(
									declSymbol);
							writeTypeParams(taParams);
						}
					}
				}
			}
			::tsc::Node* found = nullptr;
			for (auto* d : symbol->declarations) {
				if (isTypeParameterDeclaration(d)) {
					found = d;
					break;
				}
			}
			setDeclaration(found);
		}
		if ((flags & SymbolFlagsTypeAlias) != 0) {
			writeNewLine();
			dpw->WriteKeyword("type ");
			writeSymbolClassified(symbol, container, SymbolFlagsNone,
								  symbolFormatFlags);
			writeTypeParams(c->getTypeAliasTypeParameters(symbol));
			dpw->WriteOperator(" = ");
			checker::Type* typeAliasType = nullptr;
			if (node->parent != nullptr &&
				isConstTypeReference(node->parent)) {
				typeAliasType = c->GetTypeAtLocation(node->parent);
			} else {
				typeAliasType = c->GetDeclaredTypeOfSymbol(symbol);
			}
			writeTypeClassified(typeAliasType, container,
								typeFormatFlags |
									checker::TypeFormatFlagsInTypeAlias);
			::tsc::Node* found = nullptr;
			for (auto* d : symbol->declarations) {
				if (isTypeOrJSTypeAliasDeclaration(d)) {
					found = d;
					break;
				}
			}
			setDeclaration(found);
		}
		if ((flags & SymbolFlagsSignature) != 0) {
			writeNewLine();
			writeTypeClassified(c->GetTypeOfSymbol(symbol), container,
								typeFormatFlags);
		}
	};
	writeSymbol(symbol);

	return symbolDisplayInfo{dpw, firstDeclaration};
}

// ---------------------------------------------------------------------------
// Documentation writers (hover.go)
// ---------------------------------------------------------------------------

// forward decls
std::string getDocumentationFromDeclaration(
	documentationLocationMapper getMappedLocation, checker::Checker* c,
	::tsc::Symbol* symbol, ::tsc::Node* declaration, ::tsc::Node* location,
	lsp::lsproto::MarkupKind contentFormat, bool commentOnly);
void writeComments(documentationLocationMapper getMappedLocation,
				   std::string* b, checker::Checker* c,
				   std::vector<::tsc::Node*> comments, bool isMarkdown);
void writeQuotedString(std::string* b, std::string_view str, bool quote);
std::string getEntityNameString(::tsc::Node* name);
std::string trimCommentPrefix(std::string_view text);
void writeMarkdownLink(std::string* b, std::string_view text,
					   std::string_view uri, bool quote);
void writeOptionalEntityName(std::string* b, ::tsc::Node* name);

// hover.go:1059 — writeQuotedString
void writeQuotedString(std::string* b, std::string_view str, bool quote) {
	if (quote && str.find('`') == std::string_view::npos) {
		b->push_back('`');
		b->append(str);
		b->push_back('`');
	} else {
		b->append(str);
	}
}

// hover.go:1067 — getEntityNameString
std::string getEntityNameString(::tsc::Node* name) {
	std::string b;
	std::function<void(::tsc::Node*)> writeEntityNameParts =
		[&](::tsc::Node* node) {
			switch (node->kind) {
			case Kind::Identifier:
				b.append(node->text());
				break;
			case Kind::QualifiedName:
				writeEntityNameParts(node->as<QualifiedName>()->Left);
				b.push_back('.');
				writeEntityNameParts(node->as<QualifiedName>()->Right);
				break;
			case Kind::PropertyAccessExpression:
				writeEntityNameParts(node->expression());
				b.push_back('.');
				writeEntityNameParts(node->name());
				break;
			case Kind::ParenthesizedExpression:
			case Kind::ExpressionWithTypeArguments:
				writeEntityNameParts(node->expression());
				break;
			case Kind::JSDocNameReference:
				writeEntityNameParts(node->name());
				break;
			default:
				break;
			}
		};
	writeEntityNameParts(name);
	return b;
}

// hover.go:1101 — trimCommentPrefix
std::string trimCommentPrefix(std::string_view text) {
	// strings.TrimLeft(text, " ")
	size_t s = text.find_first_not_of(' ');
	text = (s == std::string_view::npos) ? "" : text.substr(s);
	// strings.TrimPrefix(text, "|")
	if (text.starts_with('|')) {
		text = text.substr(1);
	}
	s = text.find_first_not_of(' ');
	return std::string(
		(s == std::string_view::npos) ? "" : text.substr(s));
}

// hover.go:1105 — writeMarkdownLink
void writeMarkdownLink(std::string* b, std::string_view text,
					   std::string_view uri, bool quote) {
	b->push_back('[');
	writeQuotedString(b, text, quote);
	b->append("](");
	b->append(uri);
	b->append(")");
}

// hover.go:1113 — writeOptionalEntityName
void writeOptionalEntityName(std::string* b, ::tsc::Node* name) {
	if (name != nullptr) {
		b->push_back(' ');
		writeQuotedString(b, getEntityNameString(name), true /*quote*/);
	}
}

// hover.go:1121 — writeNameLink
void writeNameLink(documentationLocationMapper getMappedLocation,
				   std::string* b, checker::Checker* c,
				   ::tsc::Node* name, std::string_view text, bool quote,
				   bool isMarkdown) {
	std::vector<::tsc::Node*> declarations =
		getDeclarationsFromLocation(c, name);
	if (!declarations.empty()) {
		::tsc::Node* declaration = declarations[0];
		SourceFile* file = getSourceFileOfNode(declaration);
		::tsc::Node* node = getNameOfDeclaration(declaration) != nullptr
								? getNameOfDeclaration(declaration)
								: declaration;
		auto [loc, fidelity] =
			getMappedLocation(file, createRangeFromNode(node, file));
		int prefixLen = text.starts_with("()") ? 2 : 0;
		std::string linkText =
			trimCommentPrefix(text.substr(prefixLen));
		if (linkText.empty()) {
			linkText =
				getEntityNameString(name) + std::string(text.substr(0, prefixLen));
		}
		if (isMarkdown && fidelity.IsSingleSegment()) {
			std::string linkUri =
				std::string(loc.Uri) + "#" +
				std::to_string(loc.Range.Start.Line + 1) + "," +
				std::to_string(loc.Range.Start.Character + 1) + "-" +
				std::to_string(loc.Range.End.Line + 1) + "," +
				std::to_string(loc.Range.End.Character + 1);
			writeMarkdownLink(b, linkText, linkUri, quote);
		} else {
			writeQuotedString(b, linkText, false);
		}
		return;
	}
	writeQuotedString(
		b,
		getEntityNameString(name) + (text.empty() ? "" : " ") +
			std::string(text),
		quote && isMarkdown);
}

// hover.go:1041 — writeJSDocLink
void writeJSDocLink(documentationLocationMapper getMappedLocation,
					std::string* b, checker::Checker* c,
					::tsc::Node* link, bool quote, bool isMarkdown) {
	::tsc::Node* name = link->name();
	std::string text;
	{
		// strings.Join(link.Text(), "") — JSDocLink text is a run list.
		std::string t;
		std::vector<std::string>* parts = nullptr;
		switch (link->kind) {
		case Kind::JSDocLink:
			parts = &link->as<JSDocLink>()->text;
			break;
		case Kind::JSDocLinkCode:
			parts = &link->as<JSDocLinkCode>()->text;
			break;
		case Kind::JSDocLinkPlain:
			parts = &link->as<JSDocLinkPlain>()->text;
			break;
		default:
			break;
		}
		if (parts != nullptr) {
			for (auto& p : *parts) {
				t += p;
			}
		}
		// strings.Trim(t, " ")
		size_t s = t.find_first_not_of(' ');
		size_t e = t.find_last_not_of(' ');
		text = s == std::string::npos
				   ? ""
				   : t.substr(s, e - s + 1);
	}
	if (name == nullptr) {
		writeQuotedString(b, text, quote && isMarkdown);
		return;
	}
	if (isIdentifier(name) &&
		(name->text() == "http" || name->text() == "https") &&
		text.starts_with("://")) {
		std::string linkText = name->text() + text;
		std::string linkUri = linkText;
		size_t commentPos = linkText.find_first_of(" |");
		if (commentPos != std::string::npos) {
			linkUri = linkText.substr(0, commentPos);
			linkText = trimCommentPrefix(linkText.substr(commentPos));
			if (linkText.empty()) {
				linkText = linkUri;
			}
		}
		if (isMarkdown) {
			writeMarkdownLink(b, linkText, linkUri, quote);
		} else {
			writeQuotedString(b, linkText, false);
			if (linkText != linkUri) {
				b->append(" (");
				b->append(linkUri);
				b->append(")");
			}
		}
		return;
	}
	writeNameLink(getMappedLocation, b, c, name, text, quote,
				  isMarkdown);
}

// hover.go:1041 — writeComments
void writeComments(documentationLocationMapper getMappedLocation,
				   std::string* b, checker::Checker* c,
				   std::vector<::tsc::Node*> comments, bool isMarkdown) {
	for (auto* comment : comments) {
		switch (comment->kind) {
		case Kind::JSDocText:
			b->append(comment->text());
			break;
		case Kind::JSDocLink:
		case Kind::JSDocLinkPlain:
			writeJSDocLink(getMappedLocation, b, c, comment,
						   false /*quote*/, isMarkdown);
			break;
		case Kind::JSDocLinkCode:
			writeJSDocLink(getMappedLocation, b, c, comment,
						   true /*quote*/, isMarkdown);
			break;
		default:
			break;
		}
	}
}

// hover.go:1014 — containsTypedefTag
bool containsTypedefTag(::tsc::Node* jsdoc) {
	if (jsdoc->kind == Kind::JSDoc) {
		if (::tsc::NodeList* tags = jsdoc->as<JSDoc>()->Tags) {
			for (auto* tag : tags->nodes) {
				if (tag->kind == Kind::JSDocTypedefTag ||
					tag->kind == Kind::JSDocCallbackTag) {
					return true;
				}
			}
		}
	}
	return false;
}

// hover.go:307 — getDocumentationFromDeclaration
std::string getDocumentationFromDeclaration(
	documentationLocationMapper getMappedLocation, checker::Checker* c,
	::tsc::Symbol* symbol, ::tsc::Node* declaration, ::tsc::Node* location,
	lsp::lsproto::MarkupKind contentFormat, bool commentOnly) {
	if (declaration == nullptr) {
		return "";
	}
	bool isMarkdown = contentFormat == lsp::lsproto::MarkupKindMarkdown;
	std::string b;
	collections::Set<::tsc::Symbol*> seenSymbols;
	if (::tsc::Node* jsdoc =
			getJSDocOrTag(c, declaration, &seenSymbols);
		jsdoc != nullptr &&
		!((declaration->flags & NodeFlagsReparsed) == 0 &&
		  containsTypedefTag(jsdoc))) {
		writeComments(getMappedLocation, &b, c, jsdoc->comments(),
					  isMarkdown);
		if (jsdoc->kind == Kind::JSDoc && !commentOnly) {
			if (::tsc::NodeList* tags = jsdoc->as<JSDoc>()->Tags) {
				for (auto* tag : tags->nodes) {
					if (tag->kind == Kind::JSDocTypeTag ||
						tag->kind == Kind::JSDocTypedefTag ||
						tag->kind == Kind::JSDocCallbackTag) {
						continue;
					}
					b.append("\n\n");
					if (isMarkdown) {
						b.append("*@");
						b.append(tag->tagName()->text());
						b.append("*");
					} else {
						b.append("@");
						b.append(tag->tagName()->text());
					}
					switch (tag->kind) {
					case Kind::JSDocParameterTag:
					case Kind::JSDocPropertyTag:
						writeOptionalEntityName(&b, tag->name());
						break;
					case Kind::JSDocAugmentsTag:
						writeOptionalEntityName(
							&b,
							tag->as<JSDocAugmentsTag>()->ClassName);
						break;
					case Kind::JSDocTemplateTag:
						for (size_t i = 0;
							 i < tag->typeParameters().size(); i++) {
							if (i != 0) {
								b.push_back(',');
							}
							writeOptionalEntityName(
								&b,
								tag->typeParameters()[i]->name());
						}
						break;
					default:
						break;
					}
					auto comments = tag->comments();
					if (tag->kind == Kind::JSDocUnknownTag &&
						tag->tagName()->text() == "example") {
						std::string commentText =
							tsc::getTextOfJSDocComment(
								tag->commentList());
						if (commentText.starts_with("<caption>")) {
							size_t captionEnd =
								commentText.find("</caption>");
							if (captionEnd > 0 &&
								captionEnd != std::string::npos) {
								b.append(" — ");
								b.append(commentText.substr(
									std::string("<caption>").size(),
									captionEnd -
										std::string("<caption>")
											.size()));
								commentText = commentText.substr(
									captionEnd +
									std::string("</caption>").size());
								// Trim leading blank lines from commentText
								while (true) {
									size_t s1 =
										commentText.find_first_not_of(
											" \t");
									std::string t1 =
										s1 == std::string::npos
											? ""
											: commentText.substr(s1);
									size_t s2 =
										t1.find_first_not_of("\r\n");
									std::string t2 =
										s2 == std::string::npos
											? ""
											: t1.substr(s2);
									if (t1.size() == t2.size()) {
										break;
									}
									commentText = t2;
								}
							}
						}
						b.append("\n");
						if (commentText.size() > 6 &&
							commentText.starts_with("```") &&
							commentText.ends_with("```") &&
							commentText.find('\n') !=
								std::string::npos) {
							b.append(commentText);
							b.push_back('\n');
						} else {
							writeCode(&b, "tsx", commentText);
						}
					} else if (tag->kind == Kind::JSDocSeeTag &&
							   tag->as<JSDocSeeTag>()
									   ->NameExpression != nullptr) {
						b.append(" — ");
						writeNameLink(
							getMappedLocation, &b, c,
							tag->as<JSDocSeeTag>()
								->NameExpression->name(),
							"", false /*quote*/, isMarkdown);
						if (!comments.empty()) {
							b.push_back(' ');
							writeComments(getMappedLocation, &b, c,
										  comments, isMarkdown);
						}
					} else if (tag->kind == Kind::JSDocThrowsTag &&
							   tag->as<JSDocThrowsTag>()
									   ->TypeExpression != nullptr) {
						b.append(" — ");
						b.append(tsc::getTextOfNode(
							tag->as<JSDocThrowsTag>()
								->TypeExpression));
						if (!comments.empty()) {
							b.push_back(' ');
							writeComments(getMappedLocation, &b, c,
										  comments, isMarkdown);
						}
					} else if (!comments.empty()) {
						b.push_back(' ');
						if (comments[0]->kind != Kind::JSDocText ||
							!comments[0]->text().starts_with("-")) {
							b.append("— ");
						}
						writeComments(getMappedLocation, &b, c,
									  comments, isMarkdown);
					}
				}
			}
		}
	}
	return b;
}

// hover.go:238 — documentationFromSignature
std::string documentationFromSignature(
	documentationLocationMapper getMappedLocation, checker::Checker* c,
	::tsc::Symbol* symbol, ::tsc::Node* node, ::tsc::Node* location,
	lsp::lsproto::MarkupKind contentFormat, bool commentOnly) {
	if (node == nullptr) {
		return "";
	}
	checker::Signature* signature = c->GetResolvedSignature(node);
	if (signature == nullptr) {
		return "";
	}
	::tsc::Node* declaration = signature->declaration;
	if (declaration == nullptr) {
		return "";
	}
	if (isCallSignatureDeclaration(declaration) ||
		isConstructSignatureDeclaration(declaration)) {
		return getDocumentationFromDeclaration(getMappedLocation, c,
											   symbol, declaration,
											   location, contentFormat,
											   commentOnly);
	}
	return "";
}

// hover.go:252 — documentationFromAlias
std::string documentationFromAlias(
	documentationLocationMapper getMappedLocation, checker::Checker* c,
	::tsc::Symbol* symbol, ::tsc::Node* node,
	lsp::lsproto::MarkupKind contentFormat, bool commentOnly) {
	if (symbol == nullptr ||
		(symbol->flags & SymbolFlagsAlias) == 0) {
		return "";
	}

	::tsc::Symbol* aliasedSymbol = c->GetAliasedSymbol(symbol);
	if (aliasedSymbol == nullptr ||
		aliasedSymbol == c->GetUnknownSymbol()) {
		return "";
	}

	std::vector<::tsc::Symbol*> candidates{aliasedSymbol};
	if (aliasedSymbol->exportSymbol != nullptr) {
		candidates.push_back(aliasedSymbol->exportSymbol);
	}

	for (auto* candidate : candidates) {
		::tsc::Node* aliasedDeclaration =
			candidate->valueDeclaration != nullptr
				? candidate->valueDeclaration
				: (candidate->declarations.empty()
					   ? nullptr
					   : candidate->declarations[0]);
		if (aliasedDeclaration == nullptr) {
			continue;
		}

		if (std::string documentation = getDocumentationFromDeclaration(
				getMappedLocation, c, candidate, aliasedDeclaration,
				node, contentFormat, commentOnly);
			!documentation.empty()) {
			return documentation;
		}
	}

	return "";
}

// hover.go:279 — documentationFromRootSymbols
std::string documentationFromRootSymbols(
	documentationLocationMapper getMappedLocation, checker::Checker* c,
	::tsc::Symbol* symbol, ::tsc::Node* node,
	lsp::lsproto::MarkupKind contentFormat, bool commentOnly) {
	if (symbol == nullptr) {
		return "";
	}

	std::vector<::tsc::Symbol*> rootSymbols = c->GetRootSymbols(symbol);
	if (rootSymbols.size() <= 1) {
		return "";
	}

	std::vector<std::string> docs;
	for (auto* rootSymbol : rootSymbols) {
		if (rootSymbol == nullptr) {
			continue;
		}
		std::vector<::tsc::Node*> declarations = rootSymbol->declarations;
		if (declarations.empty() &&
			rootSymbol->valueDeclaration != nullptr) {
			declarations = {rootSymbol->valueDeclaration};
		}
		for (auto* declaration : declarations) {
			if (std::string documentation =
					getDocumentationFromDeclaration(
						getMappedLocation, c, rootSymbol, declaration,
						node, contentFormat, commentOnly);
				!documentation.empty()) {
				// core.AppendIfUnique
				if (std::find(docs.begin(), docs.end(),
							  documentation) == docs.end()) {
					docs.push_back(documentation);
				}
			}
		}
	}
	std::string out;
	for (size_t i = 0; i < docs.size(); i++) {
		if (i != 0) {
			out += "\n";
		}
		out += docs[i];
	}
	return out;
}

// hover.go:216 — getDocumentationForSymbol. Tries each documentation source in
// turn (call-signature documentation, declaration JSDoc, root-symbol JSDoc,
// alias target JSDoc) and returns the first non-empty result, formatted for
// contentFormat. commentOnly restricts the result to the JSDoc summary,
// excluding the @tag section.
std::string getDocumentationForSymbol(
	documentationLocationMapper getMappedLocation, checker::Checker* c,
	::tsc::Symbol* symbol, ::tsc::Node* node, ::tsc::Node* declaration,
	lsp::lsproto::MarkupKind contentFormat, bool commentOnly) {
	std::string documentation = documentationFromSignature(
		getMappedLocation, c, symbol, getCallOrNewExpression(node), node,
		contentFormat, commentOnly);
	if (!documentation.empty()) {
		return documentation;
	}

	documentation =
		documentationFromRootSymbols(getMappedLocation, c, symbol, node,
									 contentFormat, commentOnly);
	if (!documentation.empty()) {
		return documentation;
	}

	documentation =
		getDocumentationFromDeclaration(getMappedLocation, c, symbol,
										declaration, node, contentFormat,
										commentOnly);
	if (!documentation.empty()) {
		return documentation;
	}

	return documentationFromAlias(getMappedLocation, c, symbol, node,
								  contentFormat, commentOnly);
}

// hover.go:946 — getNodeForQuickInfo
::tsc::Node* getNodeForQuickInfo(::tsc::Node* node) {
	if (node->parent == nullptr) {
		return node;
	}
	if (isNewExpression(node->parent) &&
		node->pos() == node->parent->pos()) {
		return node->parent->expression();
	}
	if (isNamedTupleMember(node->parent) &&
		node->pos() == node->parent->pos()) {
		return node->parent;
	}
	if (isImportMeta(node->parent) && node->parent->name() == node) {
		return node->parent;
	}
	if (isJsxNamespacedName(node->parent)) {
		return node->parent;
	}
	return node;
}

// hover.go:963 — getSymbolAtLocationForQuickInfo
::tsc::Symbol* getSymbolAtLocationForQuickInfo(checker::Checker* c,
											 ::tsc::Node* node) {
	if (::tsc::Node* objectElement =
			getContainingObjectLiteralElement(node)) {
		if (checker::Type* contextualType =
				c->GetContextualType(objectElement->parent,
									 checker::ContextFlagsNone)) {
			if (auto properties =
					c->GetPropertySymbolsFromContextualType(
						objectElement, contextualType,
						false /*unionSymbolOk*/);
				properties.size() == 1) {
				return properties[0];
			}
		}
	}
	return c->GetSymbolAtLocation(node);
}

} // namespace

// ============================================================================
// hover.go:207 — documentationLocationMapper
// ============================================================================
documentationLocationMapper LanguageService::documentationLocationMapper(
	spanmap::Feature feature) {
	return [this, feature](SourceFile* file, TextRange fileRange) {
		return sourceFileRangeToLSPLocationForFeature(file, fileRange,
													  feature);
	};
}

// ============================================================================
// hover.go:175 — getQuickInfoAndDocumentationForSymbol
// ============================================================================
std::tuple<std::string, std::string, std::string,
		   lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::VSClassifiedTextRun>>>
LanguageService::getQuickInfoAndDocumentationForSymbol(
	checker::Checker* c, Symbol* symbol, ::tsc::Node* node,
	lsp::lsproto::MarkupKind contentFormat,
	checker::VerbosityContext* vc, bool vsCapability) {
	symbolDisplayInfo info = getQuickInfoAndDeclarationAtLocation(
		c, symbol, node, vc, vsCapability, getMeaningFromLocation(node));
	std::string quickInfo = info.displayParts->String();
	if (quickInfo.empty()) {
		return {"", "", "", {}};
	}
	auto quickInfoRuns = info.displayParts->GetRuns();

	std::string documentation = getDocumentationForSymbol(
		documentationLocationMapper(spanmap::FeatureHover), c, symbol,
		node, info.declaration, contentFormat,
		false /*commentOnly*/);

	// VS's rich hover (_vs_rawContent) renders documentation as plain colorized text with no Markdown
	// parser, so it can't use the tag section (@param/@returns/@example/@see, etc.) that
	// getDocumentationFromDeclaration renders with '*@tag*' bolding and ```-fenced @example blocks --
	// those would show up as literal asterisks/backticks. This also matches the legacy TSServer-backed
	// VS hover (TypeScript-VS's HoverService.cs), which only ever surfaced the JSDoc summary
	// (TSServer's quickinfo `documentation`) and never included the tag section at all (TSServer
	// exposes tags via a separate `tags` field that legacy VS hover never read). So request
	// comment-only, plain-text documentation for the VS path instead of reusing `documentation`.
	std::string vsDocumentation;
	if (vsCapability) {
		vsDocumentation = getDocumentationForSymbol(
			documentationLocationMapper(spanmap::FeatureHover), c, symbol,
			node, info.declaration, lsp::lsproto::MarkupKindPlainText,
			true /*commentOnly*/);
	}

	return {quickInfo, documentation, vsDocumentation, quickInfoRuns};
}

// ============================================================================
// hover.go:27 — ProvideHover
// ============================================================================
lsp::lsproto::HoverResponse LanguageService::ProvideHover(
	gostd::Context ctx, lsp::lsproto::HoverParams* params) {
	auto caps = lsp::lsproto::getClientCapabilities(ctx);
	lsp::lsproto::MarkupKind contentFormat =
		lsp::lsproto::PreferredMarkupKind(
			caps->TextDocument.Hover.ContentFormat);

	int verbosityLevel = 0;
	if (params->VerbosityLevel.has_value()) {
		verbosityLevel = int(*params->VerbosityLevel);
	}

	auto [program, file] = getProgramAndFile(params->TextDocument.Uri);
	auto positions = converters->FromLSPPositionForSourceFile(
		file, params->Position, spanmap::FeatureHover);
	std::vector<std::shared_ptr<lsp::lsproto::Hover>> hovers;
	for (auto& projection : positions) {
		if (!projection.Fidelity.IsSingleSegment()) {
			continue;
		}
		file = projection.Script;
		int position = int(projection.Position);
		::tsc::Node* node =
			astnav::getTouchingPropertyName(file, position);
		if (isSourceFile(node) ||
			(isPropertyAccessOrQualifiedName(node) &&
			 isInComment(file, position, node) == nullptr)) {
			// Avoid giving quickInfo for the sourceFile as a whole or inside the comment of a/**/.b
			continue;
		}
		auto [c, done] = program->GetTypeCheckerForFileExclusive(file);
		struct DeferDone {
			std::function<void()> f;
			~DeferDone() { f(); }
		} defer{done};
		::tsc::Node* rangeNode = getNodeForQuickInfo(node);
		::tsc::Symbol* symbol =
			getSymbolAtLocationForQuickInfo(c, rangeNode);

		// Always create VerbosityContext for hover so that canExpandSymbol can signal
		// canIncreaseVerbosity even at Level 0. The nodebuilder also detects expandable
		// types at Level 0 via shouldExpandType (maxExpansionDepth = 0).
		int maxTruncLen = UserPreferences().MaximumHoverLength;
		if (maxTruncLen <= 0) {
			maxTruncLen = 500;
		}
		checker::VerbosityContext vc;
		vc.Level = verbosityLevel;
		vc.MaxTruncationLength = maxTruncLen;

		bool vsCapability = caps->VSSupportsVisualStudioExtensions;
		auto [quickInfo, documentation, vsDocumentation,
			  quickInfoRuns] =
			getQuickInfoAndDocumentationForSymbol(
				c, symbol, rangeNode, contentFormat, &vc, vsCapability);
		if (quickInfo.empty()) {
			continue;
		}
		SourceFile* rangeFile = getSourceFileOfNode(rangeNode);
		TextRange textRange =
			getRangeOfNode(rangeNode, rangeFile, nullptr /*endNode*/);
		auto [hoverRange, hoverFidelity] =
			converters->ToLSPRangeForFeature(rangeFile, textRange,
										   spanmap::FeatureHover);

		std::string content;
		if (contentFormat == lsp::lsproto::MarkupKindMarkdown) {
			content = formatQuickInfo(quickInfo) + documentation;
		} else {
			content = quickInfo + documentation;
		}

		auto hover = std::make_shared<lsp::lsproto::Hover>();
		auto markupContent = std::make_shared<lsp::lsproto::MarkupContent>();
		markupContent->Kind = contentFormat;
		markupContent->Value = content;
		hover->Contents.MarkupContent = markupContent;
		if (hoverFidelity.IsSingleSegment()) {
			hover->Range =
				std::make_shared<lsp::lsproto::Range>(hoverRange);
		}

		if (caps->Experimental.HoverVerbosityLevel) {
			hover->CanIncreaseVerbosity =
				vc.CanIncreaseVerbosity && !vc.Truncated;
		}

		// Clients that support Visual Studio extensions (e.g. VS itself, when Corsa/Native TS Preview is
		// enabled) render `_vs_rawContent` in place of `contents`. Without it, VS shows plain markdown
		// with no symbol icon and no syntax coloring, unlike the legacy TSServer-backed hover path.
		if (vsCapability && !quickInfoRuns->empty()) {
			lsutil::ScriptElementKind kind =
				lsutil::ScriptElementKindKeyword;
			lsutil::ScriptElementKindModifier modifiers =
				lsutil::ScriptElementKindModifierNone;
			if (symbol != nullptr) {
				// Resolve aliases to their target before computing the icon kind, so e.g. `import { x }`
				// shows the icon for whatever `x` actually is (const, function, ...) rather than a
				// generic alias icon. GetSymbolModifiers already accounts for the alias target itself.
				::tsc::Symbol* iconSymbol = symbol;
				if ((symbol->flags & SymbolFlagsAlias) != 0) {
					if (::tsc::Symbol* resolved =
							c->GetAliasedSymbol(symbol);
						resolved != nullptr && resolved != symbol) {
						iconSymbol = resolved;
					}
				}
				kind = lsutil::GetSymbolKind(c, iconSymbol, rangeNode);
				modifiers = lsutil::GetSymbolModifiers(c, symbol);
			}
			lsp::lsproto::VSImageId* imageId =
				getVSHoverImageId(kind, modifiers);
			lsp::lsproto::Slice<std::shared_ptr<lsp::lsproto::VSClassifiedTextRun>>
			    documentationRuns{
			        std::vector<std::shared_ptr<lsp::lsproto::VSClassifiedTextRun>>{}};
			// strings.TrimLeft(vsDocumentation, "\n")
			std::string_view docText{vsDocumentation};
			size_t docStart = docText.find_first_not_of('\n');
			docText = docStart == std::string_view::npos
						  ? ""
						  : docText.substr(docStart);
			if (!docText.empty()) {
				auto run = std::make_shared<lsp::lsproto::VSClassifiedTextRun>();
				run->ClassificationTypeName =
					lsp::lsproto::ClassificationTypeName(
						lsp::lsproto::ClassificationTypeNameText);
				run->Text = std::string(docText);
				documentationRuns->push_back(run);
			}
			hover->VSRawContent = std::shared_ptr<lsp::lsproto::VSContainerElement>(
			    buildVSHoverRawContent(imageId, quickInfoRuns, documentationRuns));
		}

		hovers.push_back(hover);
	}
	lsp::lsproto::HoverOrNull resp;
	if (hovers.empty()) {
		return resp;
	}
	if (hovers.size() == 1) {
		resp.Hover = hovers[0];
		return resp;
	}

	std::shared_ptr<lsp::lsproto::Hover> combined = hovers[0];
	std::vector<std::string> contents;
	collections::Set<std::string> seenContents;
	std::vector<lsp::lsproto::
					VSImageElementOrClassifiedTextElementOrContainerElement>
		rawContents;
	std::shared_ptr<lsp::lsproto::Range> commonRange = combined->Range;

	for (auto& hover : hovers) {
		// strings.TrimRight(hover.Contents.MarkupContent.Value, "\n")
		std::string_view val{hover->Contents.MarkupContent->Value};
		size_t end = val.find_last_not_of('\n');
		std::string content = end == std::string_view::npos
								  ? ""
								  : std::string(val.substr(0, end + 1));
		if (seenContents.AddIfAbsent(content)) {
			contents.push_back(content);
			if (hover->VSRawContent != nullptr) {
				lsp::lsproto::
					VSImageElementOrClassifiedTextElementOrContainerElement
						el;
				el.ContainerElement = hover->VSRawContent;
				rawContents.push_back(el);
			}
		}
		combined->CanIncreaseVerbosity =
			combined->CanIncreaseVerbosity || hover->CanIncreaseVerbosity;
		if (commonRange == nullptr || hover->Range == nullptr ||
			!(*commonRange == *hover->Range)) {
			commonRange = nullptr;
		}
	}
	std::string separator = "\n\n";
	if (contentFormat == lsp::lsproto::MarkupKindMarkdown) {
		separator = "\n\n---\n\n";
	}
	std::string joined;
	for (size_t i = 0; i < contents.size(); i++) {
		if (i != 0) {
			joined += separator;
		}
		joined += contents[i];
	}
	combined->Contents.MarkupContent->Value = joined;
	combined->Range = commonRange;
	switch (rawContents.size()) {
	case 0:
		combined->VSRawContent = nullptr;
		break;
	case 1:
		combined->VSRawContent = rawContents[0].ContainerElement;
		break;
	default: {
		auto* container = new lsp::lsproto::VSContainerElement;
		container->Style =
			lsp::lsproto::VSContainerElementStyleStacked;
		container->Elements = rawContents;
		combined->VSRawContent = container;
		break;
	}
	}
	resp.Hover = combined;
	return resp;
}

} // namespace tsc::ls
