// Port of tsc/internal/checker/printer.go
// Slice: symbolaccess — the checker's printer glue (createPrinterWith*,
// typeToString/symbolToString/signatureToString wrappers, verbosity handling).
//
// The real printer package lands with the emitter slice (Stage 4): calls that
// need it (NewTextWriter, GetSingleLineStringWriter, Printer::Write/Emit) are
// dep-stubbed at the bottom. NodeBuilder internals (newNodeBuilderImpl /
// NodeBuilder methods) likewise stay dep-stubbed until the nodebuilder slice.

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/checker/types.h"
#include "internal/nodebuilder/types.h"
#include "internal/printer/emitcontext.h"
#include "internal/printer/printer.h"

namespace tsc::checker {

// nodebuilder.go:283 — defined below in the dep-stub block.
NodeBuilder* NewNodeBuilderEx(Checker* ch, printer::EmitContext* e,
                              std::unordered_map<Node*, Symbol*>* idToSymbol);

namespace {

// checker.go:24 — maxSerializationLevel (checker.cpp keeps its own file-local
// copy; this TU needs it for typeToStringEx).
constexpr int maxSerializationLevel = 2;

// nodebuilderimpl.go:119-120 — truncation caps.
constexpr int defaultMaximumTruncationLength = 160;
constexpr int noTruncationMaximumTruncationLength = 1000000;

// RAII replacement for Go `defer fn()` — runs fn at scope exit (LIFO order
// preserved by declaration order, like Go defers).
struct FnGuard {
	std::function<void()> f;
	explicit FnGuard(std::function<void()> fn) : f(std::move(fn)) {}
	~FnGuard() { f(); }
	FnGuard(const FnGuard&) = delete;
	FnGuard& operator=(const FnGuard&) = delete;
};

// --- createPrinterWith* — printer.go:13-40 -----------------------------------
// TODO: Memoize once per checker to retain threadsafety
printer::Printer* createPrinterWithDefaults(printer::EmitContext* emitContext) {
	return printer::NewPrinter(printer::PrinterOptions{},
	                           printer::PrintHandlers{}, emitContext);
}

printer::Printer* createPrinterWithRemoveComments(
	printer::EmitContext* emitContext) {
	return printer::NewPrinter(
		printer::PrinterOptions{.RemoveComments = true},
		printer::PrintHandlers{}, emitContext);
}

printer::Printer* createPrinterWithRemoveCommentsOmitTrailingSemicolon(
	printer::EmitContext* emitContext) {
	return printer::NewPrinter(
		printer::PrinterOptions{.RemoveComments = true,
		                        .OmitTrailingSemicolon = true},
		printer::PrintHandlers{}, emitContext);
}

printer::Printer*
createPrinterWithRemoveCommentsOmitTrailingSemicolonNeverAsciiEscape(
	printer::EmitContext* emitContext) {
	return printer::NewPrinter(
		printer::PrinterOptions{.RemoveComments = true,
		                        .OmitTrailingSemicolon = true,
		                        .NeverAsciiEscape = true},
		printer::PrintHandlers{}, emitContext);
}

printer::Printer* createPrinterWithRemoveCommentsNeverAsciiEscape(
	printer::EmitContext* emitContext) {
	return printer::NewPrinter(
		printer::PrinterOptions{.RemoveComments = true,
		                        .NeverAsciiEscape = true},
		printer::PrintHandlers{}, emitContext);
}

// --- toNodeBuilderFlags — printer.go:51 ---------------------------------------
nodebuilder::Flags toNodeBuilderFlags(TypeFormatFlags flags) {
	return static_cast<nodebuilder::Flags>(
		flags & TypeFormatFlagsNodeBuilderFlagsMask);
}

} // namespace

// --- TypeToString — printer.go:43 ---------------------------------------------
std::string Checker::TypeToString(Type* t) { return typeToString(t, nullptr); }

// --- typeToString — printer.go:47 ----------------------------------------------
std::string Checker::typeToString(Type* t, Node* enclosingDeclaration) {
	return typeToStringEx(t, enclosingDeclaration,
	                      TypeFormatFlagsAllowUniqueESSymbolType |
	                          TypeFormatFlagsUseAliasDefinedOutsideCurrentScope,
	                      nullptr);
}

// --- TypeToStringEx — printer.go:55 ---------------------------------------------
std::string Checker::TypeToStringEx(Type* t, Node* enclosingDeclaration,
                                    TypeFormatFlags flags,
                                    VerbosityContext* vc) {
	return typeToStringEx(t, enclosingDeclaration, flags, vc);
}

// --- typeToStringEx — printer.go:59 ----------------------------------------------
std::string Checker::typeToStringEx(Type* t, Node* enclosingDeclaration,
                                    TypeFormatFlags flags,
                                    VerbosityContext* vc) {
	// Serialization of types can lead to (lazy) resolution of members, which can cause diagnostics that again require
	// serialization of types. This can potentially result in infinite recursion and stack overflows. To prevent that,
	// after a certain number of recursive invocations the function simply returns "?".
	if (serializationLevel >= maxSerializationLevel) {
		return "?";
	}
	std::string newLine;
	if ((flags & TypeFormatFlagsMultilineObjectLiterals) != 0) {
		newLine = "\n";
	}
	printer::EmitTextWriter* writer = printer::NewTextWriter(newLine, 0);
	bool noTruncation =
		((vc == nullptr || vc->MaxTruncationLength == 0) &&
	     compilerOptions->NoErrorTruncation == Tristate::True) ||
	    (flags & TypeFormatFlagsNoTruncation) != 0;
	nodebuilder::Flags combinedFlags =
		toNodeBuilderFlags(flags) | nodebuilder::FlagsIgnoreErrors;
	if (noTruncation) {
		combinedFlags = combinedFlags | nodebuilder::FlagsNoTruncation;
	}
	auto [nodeBuilder, release] = getNodeBuilder();
	FnGuard releaseGuard{std::move(release)};
	VerbosityContext* oldVerbosity = nodeBuilder->verbosity;
	nodeBuilder->verbosity = vc;
	FnGuard restoreGuard{[nb = nodeBuilder, oldVerbosity] {
		nb->verbosity = oldVerbosity;
	}};
	serializationLevel++;
	Node* typeNode = nodeBuilder->TypeToTypeNode(
		t, enclosingDeclaration, combinedFlags, nodebuilder::InternalFlagsNone,
		nullptr);
	serializationLevel--;
	if (typeNode == nullptr) {
		TSC_UNREACHABLE("should always get typenode");
	}
	// The unresolved type gets a synthesized comment on `any` to hint to users that it's not a plain `any`.
	// Otherwise, we always strip comments out.
	printer::Printer* p;
	if (t == unresolvedType) {
		p = createPrinterWithDefaults(nodeBuilder->EmitContext());
	} else {
		p = createPrinterWithRemoveComments(nodeBuilder->EmitContext());
	}
	SourceFile* sourceFile = nullptr;
	if (enclosingDeclaration != nullptr) {
		sourceFile = getSourceFileOfNode(enclosingDeclaration);
	}
	p->Write(typeNode, sourceFile, writer, nullptr);
	std::string result = writer->String();

	long long maxLength = defaultMaximumTruncationLength * 2;
	if (vc != nullptr && vc->MaxTruncationLength > 0) {
		maxLength =
			static_cast<long long>(vc->MaxTruncationLength) *
			10; // hard cutoff matching Strada's absoluteMaximumLength
	}
	if (noTruncation) {
		maxLength = noTruncationMaximumTruncationLength * 2;
	}
	if (maxLength > 0 && !result.empty() &&
	    static_cast<long long>(result.size()) >= maxLength) {
		if (vc != nullptr) {
			vc->Truncated = true;
		}
		return result.substr(0, static_cast<size_t>(maxLength) - 3) + "...";
	}
	return result;
}

// --- SymbolToString — printer.go:120 ---------------------------------------------
std::string Checker::SymbolToString(Symbol* s) { return symbolToString(s); }

// --- SymbolToStringEx — printer.go:128 ---------------------------------------------
std::string Checker::SymbolToStringEx(Symbol* symbol,
                                      Node* enclosingDeclaration,
                                      SymbolFlags meaning,
                                      SymbolFormatFlags flags) {
	return symbolToStringEx(symbol, enclosingDeclaration, meaning, flags);
}

// --- symbolToStringEx — printer.go:132 ---------------------------------------------
std::string Checker::symbolToStringEx(Symbol* symbol,
                                      Node* enclosingDeclaration,
                                      SymbolFlags meaning,
                                      SymbolFormatFlags flags) {
	auto [writer, putWriter] = printer::GetSingleLineStringWriter();
	FnGuard putGuard{std::move(putWriter)};

	nodebuilder::Flags nodeFlags = nodebuilder::FlagsIgnoreErrors;
	nodebuilder::InternalFlags internalNodeFlags = nodebuilder::InternalFlagsNone;
	if (flags & SymbolFormatFlagsUseOnlyExternalAliasing) {
		nodeFlags = static_cast<nodebuilder::Flags>(
			nodeFlags | nodebuilder::FlagsUseOnlyExternalAliasing);
	}
	if (flags & SymbolFormatFlagsWriteTypeParametersOrArguments) {
		nodeFlags = static_cast<nodebuilder::Flags>(
			nodeFlags | nodebuilder::FlagsWriteTypeParametersInQualifiedName);
	}
	if (flags & SymbolFormatFlagsUseAliasDefinedOutsideCurrentScope) {
		nodeFlags = static_cast<nodebuilder::Flags>(
			nodeFlags | nodebuilder::FlagsUseAliasDefinedOutsideCurrentScope);
	}
	if (flags & SymbolFormatFlagsDoNotIncludeSymbolChain) {
		internalNodeFlags = static_cast<nodebuilder::InternalFlags>(
			internalNodeFlags |
			nodebuilder::InternalFlagsDoNotIncludeSymbolChain);
	}
	if (flags & SymbolFormatFlagsWriteComputedProps) {
		internalNodeFlags = static_cast<nodebuilder::InternalFlags>(
			internalNodeFlags |
			nodebuilder::InternalFlagsWriteComputedProps);
	}

	auto [nodeBuilder, release] = getNodeBuilder();
	FnGuard releaseGuard{std::move(release)};
	SourceFile* sourceFile = nullptr;
	if (enclosingDeclaration != nullptr) {
		sourceFile = getSourceFileOfNode(enclosingDeclaration);
	}
	printer::Printer* p;
	// add neverAsciiEscape for GH#39027
	if (enclosingDeclaration != nullptr &&
	    enclosingDeclaration->kind == Kind::SourceFile) {
		p = createPrinterWithRemoveCommentsOmitTrailingSemicolonNeverAsciiEscape(
			nodeBuilder->EmitContext());
	} else {
		p = createPrinterWithRemoveCommentsOmitTrailingSemicolon(
			nodeBuilder->EmitContext());
	}

	Node* entity;
	if (flags & SymbolFormatFlagsAllowAnyNodeKind) {
		entity = nodeBuilder->SymbolToNode(symbol, meaning, enclosingDeclaration,
		                                   nodeFlags, internalNodeFlags,
		                                   nullptr);
	} else {
		entity = nodeBuilder->SymbolToEntityName(symbol, meaning,
		                                         enclosingDeclaration, nodeFlags,
		                                         internalNodeFlags, nullptr);
	}
	p->Write(entity, sourceFile, writer, nullptr); // TODO: GH#18217
	return writer->String();
}

// --- signatureToString — printer.go:179 ---------------------------------------------
std::string Checker::signatureToString(Signature* signature) {
	return signatureToStringEx(signature, nullptr, TypeFormatFlagsNone, nullptr);
}

// --- SignatureToStringEx — printer.go:183 ---------------------------------------------
std::string Checker::SignatureToStringEx(Signature* signature,
                                        Node* enclosingDeclaration,
                                        TypeFormatFlags flags,
                                        VerbosityContext* vc) {
	return signatureToStringEx(signature, enclosingDeclaration, flags, vc);
}

// --- signatureToStringEx — printer.go:187 ---------------------------------------------
std::string Checker::signatureToStringEx(Signature* signature,
                                        Node* enclosingDeclaration,
                                        TypeFormatFlags flags,
                                        VerbosityContext* vc) {
	bool isConstructor =
		(signature->flags & SignatureFlagsConstruct) != 0 &&
		(flags & TypeFormatFlagsWriteCallStyleSignature) == 0;
	Kind sigOutput;
	if ((flags & TypeFormatFlagsWriteArrowStyleSignature) != 0) {
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

	auto [nodeBuilder, release] = getNodeBuilder();
	FnGuard releaseGuard{std::move(release)};
	VerbosityContext* oldVerbosity = nodeBuilder->verbosity;
	nodeBuilder->verbosity = vc;
	FnGuard restoreGuard{[nb = nodeBuilder, oldVerbosity] {
		nb->verbosity = oldVerbosity;
	}};
	nodebuilder::Flags combinedFlags =
		toNodeBuilderFlags(flags) | nodebuilder::FlagsIgnoreErrors |
		nodebuilder::FlagsWriteTypeParametersInQualifiedName;
	Node* sig = nodeBuilder->SignatureToSignatureDeclaration(
		signature, sigOutput, enclosingDeclaration, combinedFlags,
		nodebuilder::InternalFlagsNone, nullptr);
	printer::Printer* p =
		createPrinterWithRemoveCommentsOmitTrailingSemicolonNeverAsciiEscape(
			nodeBuilder->EmitContext());
	SourceFile* sourceFile = nullptr;
	if (enclosingDeclaration != nullptr) {
		sourceFile = getSourceFileOfNode(enclosingDeclaration);
	}
	if ((flags & TypeFormatFlagsMultilineObjectLiterals) != 0) {
		printer::EmitTextWriter* writer = printer::NewTextWriter("\n", 0);
		p->Write(sig, sourceFile, writer, nullptr);
		return writer->String();
	}
	auto [writer, putWriter] = printer::GetSingleLineStringWriter();
	FnGuard putGuard{std::move(putWriter)};
	p->Write(sig, sourceFile, writer, nullptr);
	return writer->String();
}

// --- typePredicateToString — printer.go:229 ---------------------------------------------
std::string Checker::typePredicateToString(TypePredicate* typePredicate) {
	return typePredicateToStringEx(
		typePredicate, nullptr,
		TypeFormatFlagsUseAliasDefinedOutsideCurrentScope);
}

// --- typePredicateToStringEx — printer.go:233 ---------------------------------------------
std::string Checker::typePredicateToStringEx(TypePredicate* typePredicate,
                                            Node* enclosingDeclaration,
                                            TypeFormatFlags flags) {
	auto [writer, putWriter] = printer::GetSingleLineStringWriter();
	FnGuard putGuard{std::move(putWriter)};
	auto [nodeBuilder, release] = getNodeBuilder();
	FnGuard releaseGuard{std::move(release)};
	nodebuilder::Flags combinedFlags =
		toNodeBuilderFlags(flags) | nodebuilder::FlagsIgnoreErrors |
		nodebuilder::FlagsWriteTypeParametersInQualifiedName;
	Node* predicate = nodeBuilder->TypePredicateToTypePredicateNode(
		typePredicate, enclosingDeclaration, combinedFlags,
		nodebuilder::InternalFlagsNone, nullptr); // TODO: GH#18217
	printer::Printer* printer_ =
		createPrinterWithRemoveComments(nodeBuilder->EmitContext());
	SourceFile* sourceFile = nullptr;
	if (enclosingDeclaration != nullptr) {
		sourceFile = getSourceFileOfNode(enclosingDeclaration);
	}
	printer_->Write(predicate, /*sourceFile*/ sourceFile, writer,
	                nullptr); // TODO: GH#18217
	return writer->String();
}

// --- valueToString — printer.go:249 ---------------------------------------------
std::string Checker::valueToString(
	const std::variant<std::monostate, std::string, Number, bool, PseudoBigInt>&
		value) {
	return ValueToString(value);
}

// --- formatUnionTypes — printer.go:253 ---------------------------------------------
std::vector<Type*> Checker::formatUnionTypes(const std::vector<Type*>& types,
                                             bool expandingEnum) {
	std::vector<Type*> result;
	TypeFlags flags = TypeFlagsNone;
	for (size_t i = 0; i < types.size(); i++) {
		Type* t = types[i];
		flags = static_cast<TypeFlags>(flags | t->flags);
		if ((t->flags & TypeFlagsNullable) == 0) {
			if ((t->flags & TypeFlagsBooleanLiteral) != 0 ||
			    (!expandingEnum && (t->flags & TypeFlagsEnumLike) != 0)) {
				Type* baseType;
				if ((t->flags & TypeFlagsBooleanLiteral) != 0) {
					baseType = booleanType;
				} else {
					baseType = getBaseTypeOfEnumLikeType(t);
				}
				if ((baseType->flags & TypeFlagsUnion) != 0) {
					size_t count = baseType->AsUnionType()->types.size();
					if (i + count <= types.size() &&
					    getRegularTypeOfLiteralType(types[i + count - 1]) ==
					        getRegularTypeOfLiteralType(
					            baseType->AsUnionType()->types[count - 1])) {
						result.push_back(baseType);
						i += count - 1;
						continue;
					}
				}
			}
			result.push_back(t);
		}
	}
	if ((flags & TypeFlagsNull) != 0) {
		result.push_back(nullType);
	}
	if ((flags & TypeFlagsUndefined) != 0) {
		result.push_back(undefinedType);
	}
	return result;
}

// --- TypeToTypeNode — printer.go:288 ---------------------------------------------
Node* Checker::TypeToTypeNode(
	Type* t, Node* enclosingDeclaration, nodebuilder::Flags flags,
	std::unordered_map<Node*, Symbol*>* idToSymbol) {
	NodeBuilder* nodeBuilder = getNodeBuilderEx(idToSymbol);
	return nodeBuilder->TypeToTypeNode(t, enclosingDeclaration, flags,
	                                   nodebuilder::InternalFlagsNone,
	                                   nullptr);
}

// --- SignatureToSignatureDeclaration — printer.go:293 ---------------------------------------------
Node* Checker::SignatureToSignatureDeclaration(
	Signature* signature, Kind kind, Node* enclosingDeclaration,
	nodebuilder::Flags flags) {
	auto [nodeBuilder, release] = getNodeBuilder();
	FnGuard releaseGuard{std::move(release)};
	return nodeBuilder->SignatureToSignatureDeclaration(
		signature, kind, enclosingDeclaration, flags,
		nodebuilder::InternalFlagsNone, nullptr);
}

// ExpandSymbolForHover produces declaration strings for a symbol with verbosity support for expandable hover.
// --- ExpandSymbolForHover — printer.go:300 ---------------------------------------------
std::string Checker::ExpandSymbolForHover(Symbol* symbol, SymbolFlags meaning,
                                         VerbosityContext* vc) {
	auto [nodeBuilder, release] = getNodeBuilder();
	FnGuard releaseGuard{std::move(release)};
	VerbosityContext* oldVerbosity = nodeBuilder->verbosity;
	nodeBuilder->verbosity = vc;
	FnGuard restoreGuard{[nb = nodeBuilder, oldVerbosity] {
		nb->verbosity = oldVerbosity;
	}};
	std::vector<Node*> nodes = nodeBuilder->ExpandSymbolForHover(symbol, meaning);
	if (nodes.empty()) {
		return "";
	}
	printer::Printer* p =
		createPrinterWithRemoveComments(nodeBuilder->EmitContext());
	SourceFile* sourceFile = nullptr;
	if (symbol->valueDeclaration != nullptr) {
		sourceFile = getSourceFileOfNode(symbol->valueDeclaration);
	}
	std::string b;
	for (size_t i = 0; i < nodes.size(); i++) {
		if (i > 0) {
			b += "\n";
		}
		b += p->Emit(nodes[i], sourceFile);
	}
	return b;
}

// TypeParameterToStringEx renders a type parameter declaration (e.g. "T extends Foo") with optional verbosity support.
// --- TypeParameterToStringEx — printer.go:328 ---------------------------------------------
std::string Checker::TypeParameterToStringEx(Type* t, Node* enclosingDeclaration,
                                            VerbosityContext* vc) {
	auto [nodeBuilder, release] = getNodeBuilder();
	FnGuard releaseGuard{std::move(release)};
	VerbosityContext* oldVerbosity = nodeBuilder->verbosity;
	nodeBuilder->verbosity = vc;
	FnGuard restoreGuard{[nb = nodeBuilder, oldVerbosity] {
		nb->verbosity = oldVerbosity;
	}};
	Node* typeParamNode = nodeBuilder->TypeParameterToDeclaration(
		t, enclosingDeclaration, nodebuilder::FlagsIgnoreErrors,
		nodebuilder::InternalFlagsNone, nullptr);
	if (typeParamNode == nullptr) {
		return TypeToString(t);
	}
	printer::Printer* p =
		createPrinterWithRemoveComments(nodeBuilder->EmitContext());
	SourceFile* sourceFile = nullptr;
	if (enclosingDeclaration != nullptr) {
		sourceFile = getSourceFileOfNode(enclosingDeclaration);
	}
	return p->Emit(typeParamNode, sourceFile);
}

// --- TypeToTypeNodeEx — printer.go:348 ---------------------------------------------
Node* Checker::TypeToTypeNodeEx(
	Type* t, Node* enclosingDeclaration, nodebuilder::Flags flags,
	nodebuilder::InternalFlags internalFlags,
	std::unordered_map<Node*, Symbol*>* idToSymbol) {
	NodeBuilder* nodeBuilder = getNodeBuilderEx(idToSymbol);
	return nodeBuilder->TypeToTypeNode(t, enclosingDeclaration, flags,
	                                   internalFlags, nullptr);
}

// --- TypePredicateToTypePredicateNode — printer.go:353 ---------------------------------------------
Node* Checker::TypePredicateToTypePredicateNode(
	TypePredicate* t, Node* enclosingDeclaration, nodebuilder::Flags flags,
	std::unordered_map<Node*, Symbol*>* idToSymbol) {
	NodeBuilder* nodeBuilder = getNodeBuilderEx(idToSymbol);
	return nodeBuilder->TypePredicateToTypePredicateNode(
		t, enclosingDeclaration, flags, nodebuilder::InternalFlagsNone,
		nullptr);
}

// (deduped: getNodeBuilder/getNodeBuilderEx canonical defs in
// checker_nodebuilder.cpp)

// === dep stubs — removed when owner slice lands ===


// (NodeBuilder methods — real defs now live in checker_nodebuilder.cpp)

} // namespace tsc::checker

