// === slice: ls-coreC ===
// callhierarchy.cpp — callhierarchy.go: LSP call hierarchy (prepare /
// incoming / outgoing).
#include "internal/ls/ls.h"

#include "internal/astnav/tokens.h"
#include "internal/debug/debug.h"
#include "internal/printer/printer.h"
#include "internal/scanner/scanner.h"

namespace tsc::ls {

// callhierarchy.go:533 — callSite (ns-scope: used in ls.h signatures)
struct callSite {
	::tsc::Node* declaration = nullptr;
	TextRange textRange;
	SourceFile* sourceFile = nullptr;
};

// callhierarchy.go:593 — incomingEntry (ns-scope: used in ls.h signatures)
struct incomingEntry : lsp::lsproto::HasTextDocumentPosition {
	LanguageService* ls = nullptr;
	::tsc::Node* node = nullptr;

	mutable SourceFile* sourceFile_ = nullptr;
	mutable lsp::lsproto::DocumentUri documentUri_;
	mutable bool documentUriComputed = false;
	mutable lsp::lsproto::Position position_;
	mutable bool positionComputed = false;

	SourceFile* getSourceFile() const {
		if (sourceFile_ == nullptr) {
			sourceFile_ = tsc::getSourceFileOfNode(node);
		}
		return sourceFile_;
	}

	lsp::lsproto::DocumentUri TextDocumentURI() const override {
		if (!documentUriComputed) {
			documentUri_ = lsconv::FileNameToDocumentURI(
				getSourceFile()->OriginalFileName());
			documentUriComputed = true;
		}
		return documentUri_;
	}

	lsp::lsproto::Position TextDocumentPosition() const override {
		if (!positionComputed) {
			int start = tsc::getTokenPosOfNode(node, getSourceFile(),
												 false /*includeJsDoc*/);
			auto [pos, fidelity] =
				ls->createLspPosition(start, getSourceFile());
			position_ = pos;
			positionComputed = true;
		}
		return position_;
	}
};

namespace {

// ---------------------------------------------------------------------------
// utilities.go:3646-3732 — ast helpers not yet in cpp/internal/ast
// (file-local, faithful ports)
// ---------------------------------------------------------------------------

// utilities.go:3646 — IsRightSideOfPropertyAccess
bool isRightSideOfPropertyAccess(::tsc::Node* node) {
	return node->parent->kind == Kind::PropertyAccessExpression &&
		   node->parent->name() == node;
}

// utilities.go:3650 — IsArgumentExpressionOfElementAccess
bool isArgumentExpressionOfElementAccess(::tsc::Node* node) {
	return node->parent != nullptr &&
		   node->parent->kind == Kind::ElementAccessExpression &&
		   node->parent->as<ElementAccessExpression>()->ArgumentExpression ==
			   node;
}

// utilities.go:3654 — ClimbPastPropertyAccess
::tsc::Node* climbPastPropertyAccess(::tsc::Node* node) {
	if (isRightSideOfPropertyAccess(node)) {
		return node->parent;
	}
	return node;
}

// utilities.go:3660 — climbPastPropertyOrElementAccess
::tsc::Node* climbPastPropertyOrElementAccess(::tsc::Node* node) {
	if (isRightSideOfPropertyAccess(node) ||
		isArgumentExpressionOfElementAccess(node)) {
		return node->parent;
	}
	return node;
}

// utilities.go:3667 — selectExpressionOfCallOrNewExpressionOrDecorator
::tsc::Node* selectExpressionOfCallOrNewExpressionOrDecorator(
	::tsc::Node* node) {
	if (isCallExpression(node) || isNewExpression(node) ||
		isDecorator(node)) {
		return node->expression();
	}
	return nullptr;
}

// utilities.go:3674 — selectTagOfTaggedTemplateExpression
::tsc::Node* selectTagOfTaggedTemplateExpression(::tsc::Node* node) {
	if (isTaggedTemplateExpression(node)) {
		return node->as<TaggedTemplateExpression>()->Tag;
	}
	return nullptr;
}

// utilities.go:3681 — selectTagNameOfJsxOpeningLikeElement
::tsc::Node* selectTagNameOfJsxOpeningLikeElement(::tsc::Node* node) {
	if (isJsxOpeningElement(node) || isJsxSelfClosingElement(node)) {
		return node->tagName();
	}
	return nullptr;
}

// utilities.go:3714 — isCalleeWorker
bool isCalleeWorker(::tsc::Node* node,
					const std::function<bool(::tsc::Node*)>& pred,
					const std::function<::tsc::Node*(::tsc::Node*)>&
						calleeSelector,
					bool includeElementAccess,
					bool skipPastOuterExpressions) {
	::tsc::Node* target = nullptr;
	if (includeElementAccess) {
		target = climbPastPropertyOrElementAccess(node);
	} else {
		target = climbPastPropertyAccess(node);
	}
	if (skipPastOuterExpressions) {
		// Only skip outer expressions if the target is actually an expression node
		if (isExpression(target)) {
			target = skipOuterExpressions(target, OEKAll);
		}
	}
	return target != nullptr && target->parent != nullptr &&
		   pred(target->parent) &&
		   calleeSelector(target->parent) == target;
}

// utilities.go:3697 — IsCallOrNewExpressionTarget
bool isCallOrNewExpressionTarget(::tsc::Node* node, bool includeElementAccess,
								 bool skipPastOuterExpressions) {
	return isCalleeWorker(node, isCallOrNewExpression,
						  selectExpressionOfCallOrNewExpressionOrDecorator,
						  includeElementAccess, skipPastOuterExpressions);
}

// utilities.go:3701 — IsTaggedTemplateTag
bool isTaggedTemplateTag(::tsc::Node* node, bool includeElementAccess,
						 bool skipPastOuterExpressions) {
	return isCalleeWorker(node, isTaggedTemplateExpression,
						  selectTagOfTaggedTemplateExpression,
						  includeElementAccess, skipPastOuterExpressions);
}

// utilities.go:3705 — IsDecoratorTarget
bool isDecoratorTarget(::tsc::Node* node, bool includeElementAccess,
					   bool skipPastOuterExpressions) {
	return isCalleeWorker(node, isDecorator,
						  selectExpressionOfCallOrNewExpressionOrDecorator,
						  includeElementAccess, skipPastOuterExpressions);
}

// utilities.go:3760 — IsJsxOpeningLikeElement
bool isJsxOpeningLikeElement(::tsc::Node* node) {
	return isJsxOpeningElement(node) || isJsxSelfClosingElement(node);
}

// utilities.go:3709 — IsJsxOpeningLikeElementTagName
bool isJsxOpeningLikeElementTagName(::tsc::Node* node,
									bool includeElementAccess,
									bool skipPastOuterExpressions) {
	return isCalleeWorker(node, isJsxOpeningLikeElement,
						  selectTagNameOfJsxOpeningLikeElement,
						  includeElementAccess, skipPastOuterExpressions);
}

// ---------------------------------------------------------------------------
// callhierarchy.go:23 — type CallHierarchyDeclaration = *ast.Node
// ---------------------------------------------------------------------------

// callhierarchy.go:26 — isNamedExpression. Indicates whether a node is a named
// function or class expression.
bool isNamedExpression(::tsc::Node* node) {
	if (node == nullptr) {
		return false;
	}
	if (!isFunctionExpression(node) && !isClassExpression(node)) {
		return false;
	}
	::tsc::Node* name = node->name();
	return name != nullptr && isIdentifier(name);
}

// callhierarchy.go:37 — isVariableLike
bool isVariableLike(::tsc::Node* node) {
	if (node == nullptr) {
		return false;
	}
	return isPropertyDeclaration(node) || isVariableDeclaration(node);
}

// callhierarchy.go:45 — isAssignedExpression. Indicates whether a node is a
// function, arrow, or class expression assigned to a constant variable or
// class property.
bool isAssignedExpression(::tsc::Node* node) {
	if (node == nullptr) {
		return false;
	}
	if (!(isFunctionExpression(node) || isArrowFunction(node) ||
		  isClassExpression(node))) {
		return false;
	}
	if (node->name() != nullptr) {
		return false;
	}
	::tsc::Node* parent = node->parent;
	if (!isVariableLike(parent)) {
		return false;
	}

	if (parent->initializer() != node) {
		return false;
	}

	::tsc::Node* name = parent->name();
	if (!isIdentifier(name)) {
		return false;
	}

	return (getCombinedNodeFlags(parent) & NodeFlagsConst) != 0 ||
		   isPropertyDeclaration(parent);
}

// callhierarchy.go:71 — isPossibleCallHierarchyDeclaration. Indicates whether a
// node could possibly be a call hierarchy declaration.
//
// See `resolveCallHierarchyDeclaration` for the specific rules.
bool isPossibleCallHierarchyDeclaration(::tsc::Node* node) {
	if (node == nullptr) {
		return false;
	}
	return isSourceFile(node) || isModuleDeclaration(node) ||
		   isFunctionDeclaration(node) || isFunctionExpression(node) ||
		   isClassDeclaration(node) || isClassExpression(node) ||
		   isClassStaticBlockDeclaration(node) || isMethodDeclaration(node) ||
		   isMethodSignatureDeclaration(node) ||
		   isGetAccessorDeclaration(node) || isSetAccessorDeclaration(node);
}

// callhierarchy.go:91 — isValidCallHierarchyDeclaration. Indicates whether a
// node is a valid a call hierarchy declaration.
//
// See `resolveCallHierarchyDeclaration` for the specific rules.
bool isValidCallHierarchyDeclaration(::tsc::Node* node) {
	if (node == nullptr) {
		return false;
	}

	if (isSourceFile(node)) {
		return true;
	}

	if (isModuleDeclaration(node)) {
		return isIdentifier(node->name());
	}

	return isFunctionDeclaration(node) || isClassDeclaration(node) ||
		   isClassStaticBlockDeclaration(node) || isMethodDeclaration(node) ||
		   isMethodSignatureDeclaration(node) ||
		   isGetAccessorDeclaration(node) || isSetAccessorDeclaration(node) ||
		   isNamedExpression(node) || isAssignedExpression(node);
}

// callhierarchy.go:117 — getCallHierarchyDeclarationReferenceNode. Gets the
// node that can be used as a reference to a call hierarchy declaration.
::tsc::Node* getCallHierarchyDeclarationReferenceNode(::tsc::Node* node) {
	if (node == nullptr) {
		return nullptr;
	}

	if (isSourceFile(node)) {
		return node;
	}

	if (::tsc::Node* name = node->name()) {
		return name;
	}

	if (isAssignedExpression(node)) {
		return node->parent->name();
	}

	if (::tsc::ModifierList* modifiers = node->modifiers()) {
		for (auto* mod : modifiers->nodes) {
			if (mod->kind == Kind::DefaultKeyword) {
				return mod;
			}
		}
	}

	return nullptr;
}

// callhierarchy.go:141 — getSymbolOfCallHierarchyDeclaration. Gets the symbol
// for a call hierarchy declaration.
::tsc::Symbol* getSymbolOfCallHierarchyDeclaration(checker::Checker* c,
												   ::tsc::Node* node) {
	if (isClassStaticBlockDeclaration(node)) {
		return nullptr;
	}
	::tsc::Node* location = getCallHierarchyDeclarationReferenceNode(node);
	if (location == nullptr) {
		return nullptr;
	}
	return c->GetSymbolAtLocation(location);
}

// callhierarchy.go:270 — moveRangePastModifiers
TextRange moveRangePastModifiers(::tsc::Node* node) {
	if (::tsc::ModifierList* modifiers = node->modifiers();
		modifiers != nullptr && !modifiers->nodes.empty()) {
		::tsc::Node* lastMod = modifiers->nodes.back();
		return TextRange{static_cast<TextPos>(lastMod->end()),
						 static_cast<TextPos>(node->end())};
	}
	return TextRange{static_cast<TextPos>(node->pos()),
					 static_cast<TextPos>(node->end())};
}

// callhierarchy.go:222 — getTextOfCallHierarchyName
std::string getTextOfCallHierarchyName(compiler::SimpleProgram* program,
									   ::tsc::Node* sourceNode,
									   ::tsc::Node* name,
									   ::tsc::Node* printNode) {
	if (isIdentifier(name) || isStringOrNumericLiteralLike(name)) {
		return name->text();
	}
	if (isComputedPropertyName(name)) {
		::tsc::Node* expr = name->expression();
		if (isStringOrNumericLiteralLike(expr)) {
			return expr->text();
		}
	}

	auto [c, done] =
		program->GetTypeCheckerForFileExclusive(
			getSourceFileOfNode(sourceNode));
	struct DeferDone {
		std::function<void()> f;
		~DeferDone() { f(); }
	} defer{done};
	::tsc::Symbol* symbol = c->GetSymbolAtLocation(name);
	if (symbol != nullptr) {
		std::string text = c->SymbolToString(symbol);
		if (!text.empty()) {
			return text;
		}
	}

	SourceFile* sourceFile = getSourceFileOfNode(sourceNode);
	auto [writer, putWriter] = printer::GetSingleLineStringWriter();
	struct DeferWriter {
		std::function<void()> f;
		~DeferWriter() { f(); }
	} deferW{putWriter};
	printer::PrinterOptions options;
	options.RemoveComments = true;
	printer::Printer* p =
		printer::NewPrinter(options, printer::PrintHandlers{}, nullptr);
	p->Write(printNode, sourceFile, writer, nullptr);
	return writer->String();
}

// callhierarchy.go:161 — getCallHierarchyItemName. Gets the text and range for
// the name of a call hierarchy declaration.
struct callHierarchyItemName {
	std::string text;
	int pos;
	int end;
};

callHierarchyItemName getCallHierarchyItemName(
	compiler::SimpleProgram* program, ::tsc::Node* node) {
	if (isSourceFile(node)) {
		SourceFile* sourceFile = node->as<SourceFile>();
		return {sourceFile->FileName(), 0, 0};
	}

	if ((isFunctionDeclaration(node) || isClassDeclaration(node)) &&
		node->name() == nullptr) {
		if (::tsc::ModifierList* modifiers = node->modifiers()) {
			for (auto* mod : modifiers->nodes) {
				if (mod->kind == Kind::DefaultKeyword) {
					SourceFile* sourceFile = getSourceFileOfNode(node);
					int start =
						tsc::skipTrivia(sourceFile->Text(), mod->pos());
					return {"default", start, int(mod->end())};
				}
			}
		}
	}

	if (isClassStaticBlockDeclaration(node)) {
		SourceFile* sourceFile = getSourceFileOfNode(node);
		int pos = tsc::skipTrivia(
			sourceFile->Text(), moveRangePastModifiers(node).pos());
		int end = pos + 6; // "static".length
		auto [c, done] = program->GetTypeCheckerForFileExclusive(sourceFile);
		struct DeferDone {
			std::function<void()> f;
			~DeferDone() { f(); }
		} defer{done};
		::tsc::Symbol* symbol = c->GetSymbolAtLocation(node->parent);
		std::string prefix;
		if (symbol != nullptr) {
			prefix = c->SymbolToString(symbol) + " ";
		}
		return {prefix + "static {}", pos, end};
	}

	::tsc::Node* declName = nullptr;
	if (isAssignedExpression(node)) {
		declName = node->parent->name();
	} else {
		declName = getNameOfDeclaration(node);
	}

	if (declName == nullptr || !nodeIsPresent(declName)) {
		SourceFile* sourceFile = getSourceFileOfNode(node);
		if (isFunctionDeclaration(node) || isFunctionExpression(node)) {
			int kwPos = tsc::skipTrivia(
				sourceFile->Text(), moveRangePastModifiers(node).pos());
			return {"(anonymous)", kwPos, kwPos + 8}; // "function".length
		}
		if (isClassDeclaration(node) || isClassExpression(node)) {
			int kwPos = tsc::skipTrivia(
				sourceFile->Text(), moveRangePastModifiers(node).pos());
			return {"(anonymous)", kwPos, kwPos + 5}; // "class".length
		}
		debug::assert(declName != nullptr,
					  "Expected call hierarchy item to have a name");
	}

	std::string text =
		getTextOfCallHierarchyName(program, node, declName, node);

	SourceFile* sourceFile = getSourceFileOfNode(node);
	int namePos = tsc::skipTrivia(sourceFile->Text(), declName->pos());

	return {text, namePos, int(declName->end())};
}

// callhierarchy.go:247 — getCallHierarchyItemContainerName
std::string getCallHierarchyItemContainerName(
	compiler::SimpleProgram* program, ::tsc::Node* node) {
	if (isAssignedExpression(node)) {
		::tsc::Node* parent = node->parent;
		if (isPropertyDeclaration(parent) && isClassLike(parent->parent)) {
			if (isClassExpression(parent->parent)) {
				if (::tsc::Node* assignedName =
						getAssignedName(parent->parent)) {
					return getTextOfCallHierarchyName(program, node,
													  assignedName,
													  assignedName);
				}
			} else {
				if (::tsc::Node* name = parent->parent->name()) {
					return getTextOfCallHierarchyName(program, node, name,
													  name);
				}
			}
		}
		if (parent->parent->parent != nullptr &&
			parent->parent->parent->parent != nullptr &&
			isModuleBlock(parent->parent->parent->parent)) {
			::tsc::Node* modParent =
				parent->parent->parent->parent->parent;
			if (isModuleDeclaration(modParent)) {
				if (::tsc::Node* name = modParent->name();
					name != nullptr && isIdentifier(name)) {
					return name->text();
				}
			}
		}
		return "";
	}

	switch (node->kind) {
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::MethodDeclaration:
		if (node->parent->kind == Kind::ObjectLiteralExpression) {
			if (::tsc::Node* assignedName = getAssignedName(node->parent)) {
				return getTextOfCallHierarchyName(program, node, assignedName,
												  assignedName);
			}
		}
		if (::tsc::Node* name = getNameOfDeclaration(node->parent)) {
			return getTextOfCallHierarchyName(program, node, name, name);
		}
		break;
	case Kind::FunctionDeclaration:
	case Kind::ClassDeclaration:
	case Kind::ModuleDeclaration:
		if (isModuleBlock(node->parent)) {
			if (isModuleDeclaration(node->parent->parent)) {
				if (::tsc::Node* name = node->parent->parent->name();
					name != nullptr && isIdentifier(name)) {
					return name->text();
				}
			}
		}
		break;
	default:
		break;
	}

	return "";
}

// callhierarchy.go:279 — findImplementation. Finds the implementation of a
// function-like declaration, if one exists.
::tsc::Node* findImplementation(checker::Checker* c, ::tsc::Node* node) {
	if (node == nullptr) {
		return nullptr;
	}

	if (!isFunctionLikeDeclaration(node)) {
		return node;
	}

	if (node->body() != nullptr) {
		return node;
	}

	if (isConstructorDeclaration(node)) {
		return getFirstConstructorWithBody(node->parent);
	}

	if (isFunctionDeclaration(node) || isMethodDeclaration(node)) {
		::tsc::Symbol* symbol = getSymbolOfCallHierarchyDeclaration(c, node);
		if (symbol != nullptr && symbol->valueDeclaration != nullptr) {
			if (isFunctionLikeDeclaration(symbol->valueDeclaration) &&
				symbol->valueDeclaration->body() != nullptr) {
				return symbol->valueDeclaration;
			}
		}
		return nullptr;
	}

	return node;
}

// callhierarchy.go:306 — findAllInitialDeclarations
std::vector<::tsc::Node*> findAllInitialDeclarations(checker::Checker* c,
												   ::tsc::Node* node) {
	if (isClassStaticBlockDeclaration(node)) {
		return {};
	}

	::tsc::Symbol* symbol = getSymbolOfCallHierarchyDeclaration(c, node);
	if (symbol == nullptr || symbol->declarations.empty()) {
		return {};
	}

	struct declKey {
		std::string file;
		int pos;
	};

	std::vector<int> indices(symbol->declarations.size());
	for (size_t i = 0; i < indices.size(); i++) {
		indices[i] = int(i);
	}
	std::vector<declKey> keys(symbol->declarations.size());
	for (size_t i = 0; i < symbol->declarations.size(); i++) {
		::tsc::Node* decl = symbol->declarations[i];
		keys[i] = declKey{getSourceFileOfNode(decl)->FileName(),
						  int(decl->pos())};
	}

	std::sort(indices.begin(), indices.end(), [&](int a, int b) {
		if (keys[a].file != keys[b].file) {
			return keys[a].file < keys[b].file;
		}
		return keys[a].pos < keys[b].pos;
	});

	std::vector<::tsc::Node*> declarations;
	::tsc::Node* lastDecl = nullptr;

	for (int i : indices) {
		::tsc::Node* decl = symbol->declarations[i];
		if (isValidCallHierarchyDeclaration(decl)) {
			if (lastDecl == nullptr ||
				lastDecl->parent != decl->parent ||
				lastDecl->end() != decl->pos()) {
				declarations.push_back(decl);
			}
			lastDecl = decl;
		}
	}

	return declarations;
}

// callhierarchy.go:383 — callHierarchyDeclaration result: *ast.Node |
// []*ast.Node | nil
using callHierarchyDeclarationResult =
	std::variant<std::monostate, ::tsc::Node*, std::vector<::tsc::Node*>>;

// callhierarchy.go:383 — findImplementationOrAllInitialDeclarations. Find the
// implementation or the first declaration for a call hierarchy declaration.
callHierarchyDeclarationResult findImplementationOrAllInitialDeclarations(
	checker::Checker* c, ::tsc::Node* node) {
	if (isClassStaticBlockDeclaration(node)) {
		return node;
	}

	if (isFunctionLikeDeclaration(node)) {
		if (::tsc::Node* impl = findImplementation(c, node)) {
			return impl;
		}
		if (auto decls = findAllInitialDeclarations(c, node);
			!decls.empty()) {
			return decls;
		}
		return node;
	}

	if (auto decls = findAllInitialDeclarations(c, node); !decls.empty()) {
		return decls;
	}
	return node;
}

// callhierarchy.go:404 — resolveCallHierarchyDeclaration. Resolves the call
// hierarchy declaration for a node.
callHierarchyDeclarationResult resolveCallHierarchyDeclaration(
	compiler::SimpleProgram* program, ::tsc::Node* location) {
	// A call hierarchy item must refer to either a SourceFile, Module Declaration, Class Static Block, or something intrinsically callable that has a name:
	// - Class Declarations
	// - Class Expressions (with a name)
	// - Function Declarations
	// - Function Expressions (with a name or assigned to a const variable)
	// - Arrow Functions (assigned to a const variable)
	// - Constructors
	// - Class `static {}` initializer blocks
	// - Methods
	// - Accessors
	//
	// If a call is contained in a non-named callable Node (function expression, arrow function, etc.), then
	// its containing `CallHierarchyItem` is a containing function or SourceFile that matches the above list.

	auto [c, done] = program->GetTypeChecker(gostd::Context{});
	struct DeferDone {
		std::function<void()> f;
		~DeferDone() { f(); }
	} defer{done};

	bool followingSymbol = false;

	while (location != nullptr) {
		if (isValidCallHierarchyDeclaration(location)) {
			return findImplementationOrAllInitialDeclarations(c, location);
		}

		if (isPossibleCallHierarchyDeclaration(location)) {
			::tsc::Node* ancestor =
				findAncestor(location, isValidCallHierarchyDeclaration);
			if (ancestor != nullptr) {
				return findImplementationOrAllInitialDeclarations(c,
																ancestor);
			}
		}

		if (isDeclarationName(location)) {
			if (isValidCallHierarchyDeclaration(location->parent)) {
				return findImplementationOrAllInitialDeclarations(
					c, location->parent);
			}
			if (isPossibleCallHierarchyDeclaration(location->parent)) {
				::tsc::Node* ancestor =
					findAncestor(location->parent,
								 isValidCallHierarchyDeclaration);
				if (ancestor != nullptr) {
					return findImplementationOrAllInitialDeclarations(
						c, ancestor);
				}
			}
			if (isVariableLike(location->parent)) {
				::tsc::Node* initializer = location->parent->initializer();
				if (initializer != nullptr &&
					isAssignedExpression(initializer)) {
					return callHierarchyDeclarationResult{initializer};
				}
			}
			return std::monostate{};
		}

		if (isConstructorDeclaration(location)) {
			if (isValidCallHierarchyDeclaration(location->parent)) {
				return callHierarchyDeclarationResult{location->parent};
			}
			return std::monostate{};
		}

		if (location->kind == Kind::StaticKeyword &&
			isClassStaticBlockDeclaration(location->parent)) {
			location = location->parent;
			continue;
		}

		// #39453
		if (isVariableDeclaration(location)) {
			if (::tsc::Node* initializer = location->initializer();
				initializer != nullptr &&
				isAssignedExpression(initializer)) {
				return callHierarchyDeclarationResult{initializer};
			}
		}

		if (!followingSymbol) {
			::tsc::Symbol* symbol = c->GetSymbolAtLocation(location);
			if (symbol != nullptr) {
				if ((symbol->flags & SymbolFlagsAlias) != 0) {
					symbol = c->GetAliasedSymbol(symbol);
				}
				if (symbol->valueDeclaration != nullptr) {
					followingSymbol = true;
					location = symbol->valueDeclaration;
					continue;
				}
			}
		}

		return std::monostate{};
	}

	return std::monostate{};
}

// ---------------------------------------------------------------------------
// callhierarchy.go:533 — callSite
// ---------------------------------------------------------------------------


// callhierarchy.go:539 — convertEntryToCallSite
callSite* convertEntryToCallSite(ReferenceEntry* entry) {
	if (entry->kind != entryKindNode) {
		return nullptr;
	}

	::tsc::Node* node = entry->node;
	if (!isCallOrNewExpressionTarget(node, true /*includeElementAccess*/,
									 true /*skipPastOuterExpressions*/) &&
		!isTaggedTemplateTag(node, true, true) &&
		!isDecoratorTarget(node, true, true) &&
		!isJsxOpeningLikeElementTagName(node, true, true) &&
		!isRightSideOfPropertyAccess(node) &&
		!isArgumentExpressionOfElementAccess(node)) {
		return nullptr;
	}

	SourceFile* sourceFile = getSourceFileOfNode(node);
	::tsc::Node* ancestor =
		findAncestor(node, isValidCallHierarchyDeclaration);
	if (ancestor == nullptr) {
		ancestor = sourceFile->asNode();
	}

	int start = tsc::skipTrivia(sourceFile->Text(), node->pos());
	auto* site = new callSite;
	site->declaration = ancestor;
	site->textRange = TextRange{static_cast<TextPos>(start),
								static_cast<TextPos>(node->end())};
	site->sourceFile = sourceFile;
	return site;
}

// callhierarchy.go:566 — getCallSiteGroupKey
::tsc::NodeId getCallSiteGroupKey(callSite* site) {
	return getNodeId(site->declaration);
}

// ---------------------------------------------------------------------------
// callhierarchy.go:593 — incomingEntry
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// callhierarchy.go:707 — callSiteCollector
// ---------------------------------------------------------------------------
struct callSiteCollector {
	compiler::SimpleProgram* program = nullptr;
	std::vector<callSite*> callSites;

	// callhierarchy.go:711 — recordCallSite
	void recordCallSite(::tsc::Node* node) {
		::tsc::Node* target = nullptr;

		if (isTaggedTemplateExpression(node)) {
			target = node->as<TaggedTemplateExpression>()->Tag;
		} else if (isJsxOpeningElement(node)) {
			target = node->tagName();
		} else if (isJsxSelfClosingElement(node)) {
			target = node->tagName();
		} else if (isPropertyAccessExpression(node) ||
				   isElementAccessExpression(node)) {
			target = node;
		} else if (isClassStaticBlockDeclaration(node)) {
			target = node;
		} else if (isCallExpression(node)) {
			target = node->expression();
		} else if (isNewExpression(node)) {
			target = node->expression();
		} else if (isDecorator(node)) {
			target = node->expression();
		}

		if (target == nullptr) {
			return;
		}

		auto declaration = resolveCallHierarchyDeclaration(program, target);
		if (std::holds_alternative<std::monostate>(declaration)) {
			return;
		}

		SourceFile* sourceFile = getSourceFileOfNode(target);
		int start = tsc::skipTrivia(sourceFile->Text(), target->pos());
		TextRange textRange{static_cast<TextPos>(start),
							static_cast<TextPos>(target->end())};

		if (auto* decl = std::get_if<::tsc::Node*>(&declaration)) {
			auto* site = new callSite;
			site->declaration = *decl;
			site->textRange = textRange;
			site->sourceFile = sourceFile;
			callSites.push_back(site);
		} else {
			for (auto* d : std::get<std::vector<::tsc::Node*>>(declaration)) {
				auto* site = new callSite;
				site->declaration = d;
				site->textRange = textRange;
				site->sourceFile = sourceFile;
				callSites.push_back(site);
			}
		}
	}

	// callhierarchy.go:759 — collect
	void collect(::tsc::Node* node) {
		if (node == nullptr) {
			return;
		}

		// do not descend into ambient nodes.
		if ((node->flags & NodeFlagsAmbient) != 0) {
			return;
		}

		// do not descend into other call site declarations, other than class member names
		if (isValidCallHierarchyDeclaration(node)) {
			if (isClassLike(node)) {
				for (auto* member : node->members()) {
					if (member->name() != nullptr &&
						isComputedPropertyName(member->name())) {
						collect(member->name()->expression());
					}
				}
			}
			return;
		}

		switch (node->kind) {
		case Kind::Identifier:
		case Kind::ImportEqualsDeclaration:
		case Kind::ImportDeclaration:
		case Kind::ExportDeclaration:
		case Kind::InterfaceDeclaration:
		case Kind::TypeAliasDeclaration:
			// do not descend into nodes that cannot contain callable nodes
			return;
		case Kind::ClassStaticBlockDeclaration:
			recordCallSite(node);
			return;
		case Kind::TypeAssertionExpression:
		case Kind::AsExpression:
			// do not descend into the type side of an assertion
			collect(node->expression());
			return;
		case Kind::VariableDeclaration:
		case Kind::Parameter:
			// do not descend into the type of a variable or parameter declaration
			collect(node->name());
			collect(node->initializer());
			return;
		case Kind::CallExpression:
			// do not descend into the type arguments of a call expression
			recordCallSite(node);
			collect(node->expression());
			for (auto* arg : node->arguments()) {
				collect(arg);
			}
			return;
		case Kind::NewExpression:
			// do not descend into the type arguments of a new expression
			recordCallSite(node);
			collect(node->expression());
			for (auto* arg : node->arguments()) {
				collect(arg);
			}
			return;
		case Kind::TaggedTemplateExpression: {
			// do not descend into the type arguments of a tagged template expression
			recordCallSite(node);
			auto* taggedTemplate = node->as<TaggedTemplateExpression>();
			collect(taggedTemplate->Tag);
			collect(taggedTemplate->Template);
			return;
		}
		case Kind::JsxOpeningElement:
		case Kind::JsxSelfClosingElement:
			// do not descend into the type arguments of a JsxOpeningLikeElement
			recordCallSite(node);
			collect(node->tagName());
			collect(node->attributes());
			return;
		case Kind::Decorator:
			recordCallSite(node);
			collect(node->expression());
			return;
		case Kind::PropertyAccessExpression:
		case Kind::ElementAccessExpression:
			recordCallSite(node);
			node->forEachChild([&](::tsc::Node* child) {
				collect(child);
				return false;
			});
			return;
		case Kind::SatisfiesExpression:
			// do not descend into the type side of an assertion
			collect(node->expression());
			return;
		default:
			break;
		}

		if (isPartOfTypeNode(node)) {
			// do not descend into types
			return;
		}

		node->forEachChild([&](::tsc::Node* child) {
			collect(child);
			return false;
		});
	}
};

// callhierarchy.go:859 — collectCallSites
std::vector<callSite*> collectCallSites(compiler::SimpleProgram* program,
									  checker::Checker* c,
									  ::tsc::Node* node) {
	callSiteCollector collector;
	collector.program = program;

	switch (node->kind) {
	case Kind::SourceFile:
		for (auto* stmt : node->statements()) {
			collector.collect(stmt);
		}
		break;

	case Kind::ModuleDeclaration:
		if (::tsc::Node* body = node->body();
			!hasSyntacticModifier(node, ModifierFlagsAmbient) &&
			body != nullptr && isModuleBlock(body)) {
			for (auto* stmt : body->statements()) {
				collector.collect(stmt);
			}
		}
		break;

	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		if (::tsc::Node* impl = findImplementation(c, node)) {
			for (auto* param : impl->parameters()) {
				collector.collect(param);
			}
			collector.collect(impl->body());
		}
		break;

	case Kind::ClassDeclaration:
	case Kind::ClassExpression: {
		if (::tsc::ModifierList* modifiers = node->modifiers()) {
			for (auto* mod : modifiers->nodes) {
				collector.collect(mod);
			}
		}

		::tsc::Node* heritage = getClassExtendsHeritageElement(node);
		if (heritage != nullptr) {
			collector.collect(heritage->expression());
		}

		for (auto* member : node->members()) {
			if (canHaveModifiers(member) &&
				member->modifiers() != nullptr) {
				for (auto* mod : member->modifiers()->nodes) {
					collector.collect(mod);
				}
			}

			if (isPropertyDeclaration(member)) {
				collector.collect(member->initializer());
			} else if (isConstructorDeclaration(member)) {
				if (::tsc::Node* body = member->body()) {
					for (auto* param : member->parameters()) {
						collector.collect(param);
					}
					collector.collect(body);
				}
			} else if (isClassStaticBlockDeclaration(member)) {
				collector.collect(member);
			}
		}
		break;
	}

	case Kind::ClassStaticBlockDeclaration: {
		auto* staticBlock = node->as<ClassStaticBlockDeclaration>();
		collector.collect(staticBlock->Body);
		break;
	}

	default:
		debug::assertNever(node);
	}

	return collector.callSites;
}

// crossproject.go:423 — combineIncomingCalls (dep stub — ls-coreB)
lsp::lsproto::CallHierarchyIncomingCallsResponse combineIncomingCalls(
	std::function<void(
		std::function<bool(lsp::lsproto::CallHierarchyIncomingCallsResponse)>)>
		results) {
	TSC_UNREACHABLE("combineIncomingCalls — owned by ls-coreB slice");
}

} // namespace




// ============================================================================
// callhierarchy.go:493 — createCallHierarchyItem
// ============================================================================
lsp::lsproto::CallHierarchyItem* LanguageService::createCallHierarchyItem(
	compiler::SimpleProgram* program, ::tsc::Node* node) {
	SourceFile* sourceFile = getSourceFileOfNode(node);
	callHierarchyItemName itemName =
		getCallHierarchyItemName(program, node);
	std::string containerName =
		getCallHierarchyItemContainerName(program, node);

	lsp::lsproto::SymbolKind kind = getSymbolKindFromNode(node);

	int fullStart = tsc::skipTriviaEx(
		sourceFile->Text(), node->pos(),
		tsc::SkipTriviaOptions{.stopAtComments = true});
	auto [span, spanFidelity] = converters->ToLSPRangeForFeature(
		sourceFile,
		TextRange{static_cast<TextPos>(fullStart),
				  static_cast<TextPos>(node->end())},
		spanmap::FeatureCallHierarchy);
	auto [selectionSpan, selectionFidelity] =
		converters->ToLSPRangeForFeature(
			sourceFile,
			TextRange{static_cast<TextPos>(itemName.pos),
					  static_cast<TextPos>(itemName.end)},
			spanmap::FeatureCallHierarchy);
	if (!selectionFidelity.IsSingleSegment()) {
		return nullptr;
	}
	if (spanFidelity.IsNone() ||
		(sourceFile->ContentMapper() != "" &&
		 !lspRangeContains(span, selectionSpan))) {
		span = selectionSpan;
	}

	auto* item = new lsp::lsproto::CallHierarchyItem;
	item->Name = itemName.text;
	item->Kind = kind;
	item->Uri =
		lsconv::FileNameToDocumentURI(sourceFile->OriginalFileName());
	item->Range = span;
	item->SelectionRange = selectionSpan;

	if (!containerName.empty()) {
		item->Detail = new std::string(containerName);
	}

	return item;
}

// ============================================================================
// callhierarchy.go:570 — convertCallSiteGroupToIncomingCall
// ============================================================================
lsp::lsproto::CallHierarchyIncomingCall*
LanguageService::convertCallSiteGroupToIncomingCall(
	compiler::SimpleProgram* program, std::vector<callSite*> entries) {
	std::vector<lsp::lsproto::Range> fromRanges;
	for (auto* entry : entries) {
		SourceFile* sourceFile = entry->sourceFile;
		if (auto [lspRange, fidelity] = converters->ToLSPRangeForFeature(
				sourceFile, entry->textRange,
				spanmap::FeatureCallHierarchy);
			!fidelity.IsNone()) {
			fromRanges.push_back(lspRange);
		}
	}
	lsp::lsproto::CallHierarchyItem* from =
		createCallHierarchyItem(program, entries[0]->declaration);
	if (from == nullptr || fromRanges.empty()) {
		return nullptr;
	}

	std::sort(fromRanges.begin(), fromRanges.end(),
			  lsp::lsproto::CompareRanges);

	auto* call = new lsp::lsproto::CallHierarchyIncomingCall;
	call->From = from;
	call->FromRanges = std::move(fromRanges);
	return call;
}

// ============================================================================
// callhierarchy.go:628 — getIncomingCalls. Gets the call sites that call into
// the provided call hierarchy declaration.
// ============================================================================
std::pair<lsp::lsproto::CallHierarchyIncomingCallsResponse, gostd::Error>
LanguageService::getIncomingCalls(gostd::Context ctx,
								  compiler::SimpleProgram* program,
								  ::tsc::Node* declaration,
								  CrossProjectOrchestrator* orchestrator) {
	// Source files and modules have no incoming calls.
	if (isSourceFile(declaration) || isModuleDeclaration(declaration) ||
		isClassStaticBlockDeclaration(declaration)) {
		return {lsp::lsproto::CallHierarchyIncomingCallsOrNull{}, {}};
	}

	::tsc::Node* location =
		getCallHierarchyDeclarationReferenceNode(declaration);
	if (location == nullptr) {
		return {lsp::lsproto::CallHierarchyIncomingCallsOrNull{}, {}};
	}
	SourceFile* locationFile = getSourceFileOfNode(location);
	int locationStart = tsc::getTokenPosOfNode(location, locationFile,
												   false /*includeJsDoc*/);
	if (auto [lspPos, fidelity] = converters->ToLSPPositionForFeature(
			locationFile, static_cast<TextPos>(locationStart),
			spanmap::FeatureCallHierarchy);
		fidelity.IsNone()) {
		return {lsp::lsproto::CallHierarchyIncomingCallsOrNull{}, {}};
	}

	auto* incomingEntryP = new incomingEntry;
	incomingEntryP->ls = this;
	incomingEntryP->node = location;

	auto [result, err] =
		handleCrossProject<incomingEntry*,
						   lsp::lsproto::CallHierarchyIncomingCallsResponse>(
			ctx, incomingEntryP, orchestrator,
			[](LanguageService* ls, gostd::Context ctx,
			   incomingEntry* params, SymbolAndEntriesData data,
			   symbolEntryTransformOptions options) {
				return ls->symbolAndEntriesToIncomingCalls(ctx, params, data,
														 options);
			},
			combineIncomingCalls,
			/*isRename*/ false,
			/*implementations*/ false,
			symbolEntryTransformOptions{},
			nullptr /*defaultProjectData*/);
	if (result.CallHierarchyIncomingCalls != nullptr) {
		std::sort(
			result.CallHierarchyIncomingCalls->begin(),
			result.CallHierarchyIncomingCalls->end(),
			[](lsp::lsproto::CallHierarchyIncomingCall* a,
			   lsp::lsproto::CallHierarchyIncomingCall* b) {
				if (a->From->Uri != b->From->Uri) {
					return a->From->Uri < b->From->Uri;
				}
				if (a->FromRanges.empty() || b->FromRanges.empty()) {
					return false;
				}
				return lsp::lsproto::CompareRanges(a->FromRanges[0],
												 b->FromRanges[0]) < 0;
			});
	}
	return {result, err};
}

// ============================================================================
// callhierarchy.go:666 — symbolAndEntriesToIncomingCalls
// ============================================================================
std::pair<lsp::lsproto::CallHierarchyIncomingCallsResponse, gostd::Error>
LanguageService::symbolAndEntriesToIncomingCalls(
	gostd::Context ctx, incomingEntry* params, SymbolAndEntriesData data,
	symbolEntryTransformOptions options) {
	compiler::SimpleProgram* program = GetProgram();
	std::vector<ReferenceEntry*> refEntries;
	for (auto* symbolAndEntry : data.SymbolsAndEntries) {
		refEntries.insert(refEntries.end(),
						  symbolAndEntry->references.begin(),
						  symbolAndEntry->references.end());
	}

	std::vector<callSite*> callSites;
	for (auto* entry : refEntries) {
		if (callSite* site = convertEntryToCallSite(entry)) {
			callSites.push_back(site);
		}
	}

	if (callSites.empty()) {
		return {lsp::lsproto::CallHierarchyIncomingCallsOrNull{}, {}};
	}

	std::unordered_map<::tsc::NodeId, std::vector<callSite*>> grouped;
	for (auto* site : callSites) {
		::tsc::NodeId key = getCallSiteGroupKey(site);
		grouped[key].push_back(site);
	}

	std::vector<lsp::lsproto::CallHierarchyIncomingCall*> result;
	for (auto& [key, sites] : grouped) {
		if (auto* incomingCall =
				convertCallSiteGroupToIncomingCall(program, sites)) {
			result.push_back(incomingCall);
		}
	}
	lsp::lsproto::CallHierarchyIncomingCallsOrNull resp;
	resp.CallHierarchyIncomingCalls =
		new std::vector<lsp::lsproto::CallHierarchyIncomingCall*>(
			std::move(result));
	return {resp, {}};
}

// ============================================================================
// callhierarchy.go:944 — convertCallSiteGroupToOutgoingCall
// ============================================================================
lsp::lsproto::CallHierarchyOutgoingCall*
LanguageService::convertCallSiteGroupToOutgoingCall(
	compiler::SimpleProgram* program, std::vector<callSite*> entries) {
	std::vector<lsp::lsproto::Range> fromRanges;
	for (auto* entry : entries) {
		SourceFile* sourceFile = entry->sourceFile;
		if (auto [lspRange, fidelity] = converters->ToLSPRangeForFeature(
				sourceFile, entry->textRange,
				spanmap::FeatureCallHierarchy);
			!fidelity.IsNone()) {
			fromRanges.push_back(lspRange);
		}
	}
	lsp::lsproto::CallHierarchyItem* to =
		createCallHierarchyItem(program, entries[0]->declaration);
	if (to == nullptr || fromRanges.empty()) {
		return nullptr;
	}

	std::sort(fromRanges.begin(), fromRanges.end(),
			  lsp::lsproto::CompareRanges);

	auto* call = new lsp::lsproto::CallHierarchyOutgoingCall;
	call->To = to;
	call->FromRanges = std::move(fromRanges);
	return call;
}

// ============================================================================
// callhierarchy.go:969 — getOutgoingCalls. Gets the call sites that call out of
// the provided call hierarchy declaration.
// ============================================================================
std::vector<lsp::lsproto::CallHierarchyOutgoingCall*>
LanguageService::getOutgoingCalls(compiler::SimpleProgram* program,
								  ::tsc::Node* declaration) {
	if ((declaration->flags & NodeFlagsAmbient) != 0 ||
		isMethodSignatureDeclaration(declaration)) {
		return {};
	}

	auto [c, done] = program->GetTypeChecker(gostd::Context{});
	struct DeferDone {
		std::function<void()> f;
		~DeferDone() { f(); }
	} defer{done};

	std::vector<callSite*> callSites =
		collectCallSites(program, c, declaration);

	if (callSites.empty()) {
		return {};
	}

	std::unordered_map<::tsc::NodeId, std::vector<callSite*>> grouped;
	for (auto* site : callSites) {
		::tsc::NodeId key = getCallSiteGroupKey(site);
		grouped[key].push_back(site);
	}

	std::vector<lsp::lsproto::CallHierarchyOutgoingCall*> result;
	for (auto& [key, sites] : grouped) {
		if (auto* outgoingCall =
				convertCallSiteGroupToOutgoingCall(program, sites)) {
			result.push_back(outgoingCall);
		}
	}

	std::sort(result.begin(), result.end(),
			  [](lsp::lsproto::CallHierarchyOutgoingCall* a,
				 lsp::lsproto::CallHierarchyOutgoingCall* b) {
				  if (a->To->Uri != b->To->Uri) {
					  return a->To->Uri < b->To->Uri;
				  }
				  if (a->FromRanges.empty() || b->FromRanges.empty()) {
					  return false;
				  }
				  return lsp::lsproto::CompareRanges(a->FromRanges[0],
												   b->FromRanges[0]) < 0;
			  });

	return result;
}

// ============================================================================
// callhierarchy.go:1005 — ProvidePrepareCallHierarchy
// ============================================================================
lsp::lsproto::CallHierarchyPrepareResponse
LanguageService::ProvidePrepareCallHierarchy(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
	lsp::lsproto::Position position) {
	auto [program, file] = getProgramAndFile(documentURI);
	std::vector<::tsc::Node*> declarations =
		callHierarchyDeclarations(file, position, program,
								  false /*allowSourceFile*/);
	std::vector<lsp::lsproto::CallHierarchyItem*> items;
	collections::Set<lsp::lsproto::Location> seen;
	for (auto* declaration : declarations) {
		if (lsp::lsproto::CallHierarchyItem* item =
				createCallHierarchyItem(program, declaration)) {
			lsp::lsproto::Location location;
			location.Uri = item->Uri;
			location.Range = item->SelectionRange;
			if (seen.AddIfAbsent(location)) {
				items.push_back(item);
			}
		}
	}

	lsp::lsproto::CallHierarchyItemsOrNull resp;
	if (!items.empty()) {
		resp.CallHierarchyItems =
			new std::vector<lsp::lsproto::CallHierarchyItem*>(
				std::move(items));
	}
	return resp;
}

// ============================================================================
// callhierarchy.go:1025 — ProvideCallHierarchyIncomingCalls
// ============================================================================
lsp::lsproto::CallHierarchyIncomingCallsResponse
LanguageService::ProvideCallHierarchyIncomingCalls(
	gostd::Context ctx, lsp::lsproto::CallHierarchyItem* item,
	CrossProjectOrchestrator* orchestrator) {
	compiler::SimpleProgram* program = GetProgram();
	std::string fileName = item->Uri.FileName();
	SourceFile* file = program->GetSourceFile(fileName);
	if (file == nullptr) {
		return lsp::lsproto::CallHierarchyIncomingCallsOrNull{};
	}

	std::vector<::tsc::Node*> declarations = callHierarchyDeclarations(
		file, item->SelectionRange.Start, program,
		true /*allowSourceFile*/);
	std::vector<lsp::lsproto::CallHierarchyIncomingCall*> calls;
	std::unordered_map<lsp::lsproto::Location,
					   lsp::lsproto::CallHierarchyIncomingCall*>
		seen;
	for (auto* declaration : declarations) {
		auto [response, err] =
			getIncomingCalls(ctx, program, declaration, orchestrator);
		if (err != nullptr) {
			return lsp::lsproto::CallHierarchyIncomingCallsOrNull{};
		}
		if (response.CallHierarchyIncomingCalls != nullptr) {
			for (auto* call : *response.CallHierarchyIncomingCalls) {
				lsp::lsproto::Location location;
				location.Uri = call->From->Uri;
				location.Range = call->From->SelectionRange;
				auto it = seen.find(location);
				if (it != seen.end()) {
					auto* existing = it->second;
					for (auto& fromRange : call->FromRanges) {
						if (std::find(existing->FromRanges.begin(),
									  existing->FromRanges.end(),
									  fromRange) ==
							existing->FromRanges.end()) {
							existing->FromRanges.push_back(fromRange);
						}
					}
				} else {
					seen[location] = call;
					calls.push_back(call);
				}
			}
		}
	}
	lsp::lsproto::CallHierarchyIncomingCallsOrNull resp;
	if (!calls.empty()) {
		resp.CallHierarchyIncomingCalls =
			new std::vector<lsp::lsproto::CallHierarchyIncomingCall*>(
				std::move(calls));
	}
	return resp;
}

// ============================================================================
// callhierarchy.go:1064 — ProvideCallHierarchyOutgoingCalls
// ============================================================================
lsp::lsproto::CallHierarchyOutgoingCallsResponse
LanguageService::ProvideCallHierarchyOutgoingCalls(
	gostd::Context ctx, lsp::lsproto::CallHierarchyItem* item) {
	compiler::SimpleProgram* program = GetProgram();
	std::string fileName = item->Uri.FileName();
	SourceFile* file = program->GetSourceFile(fileName);
	if (file == nullptr) {
		return lsp::lsproto::CallHierarchyOutgoingCallsOrNull{};
	}

	std::vector<::tsc::Node*> declarations = callHierarchyDeclarations(
		file, item->SelectionRange.Start, program,
		true /*allowSourceFile*/);
	std::vector<lsp::lsproto::CallHierarchyOutgoingCall*> calls;
	std::unordered_map<lsp::lsproto::Location,
					   lsp::lsproto::CallHierarchyOutgoingCall*>
		seen;
	for (auto* declaration : declarations) {
		for (auto* call : getOutgoingCalls(program, declaration)) {
			lsp::lsproto::Location location;
			location.Uri = call->To->Uri;
			location.Range = call->To->SelectionRange;
			auto it = seen.find(location);
			if (it != seen.end()) {
				auto* existing = it->second;
				for (auto& fromRange : call->FromRanges) {
					if (std::find(existing->FromRanges.begin(),
								  existing->FromRanges.end(),
								  fromRange) ==
						existing->FromRanges.end()) {
						existing->FromRanges.push_back(fromRange);
					}
				}
			} else {
				seen[location] = call;
				calls.push_back(call);
			}
		}
	}
	lsp::lsproto::CallHierarchyOutgoingCallsOrNull resp;
	if (!calls.empty()) {
		resp.CallHierarchyOutgoingCalls =
			new std::vector<lsp::lsproto::CallHierarchyOutgoingCall*>(
				std::move(calls));
	}
	return resp;
}

// ============================================================================
// callhierarchy.go:1101 — callHierarchyDeclarations
// ============================================================================
std::vector<::tsc::Node*> LanguageService::callHierarchyDeclarations(
	SourceFile* file, lsp::lsproto::Position position,
	compiler::SimpleProgram* program, bool allowSourceFile) {
	auto positions = converters->FromLSPPositionForSourceFile(
		file, position, spanmap::FeatureCallHierarchy);
	std::vector<::tsc::Node*> declarations;
	collections::Set<::tsc::Node*> seen;
	for (auto& mapped : positions) {
		if (!mapped.Fidelity.IsSingleSegment()) {
			continue;
		}
		SourceFile* projFile = mapped.Script;
		int pos = int(mapped.Position);
		::tsc::Node* node = projFile->asNode();
		if (pos != 0) {
			node = astnav::getTouchingPropertyName(projFile, pos);
		}
		if (node == nullptr ||
			(!allowSourceFile && node->kind == Kind::SourceFile)) {
			continue;
		}
		auto declaration =
			resolveCallHierarchyDeclaration(program, node);
		if (auto* decl =
				std::get_if<::tsc::Node*>(&declaration)) {
			if (seen.AddIfAbsent(*decl)) {
				declarations.push_back(*decl);
			}
		} else if (auto* decls = std::get_if<std::vector<::tsc::Node*>>(
					   &declaration)) {
			for (auto* d : *decls) {
				if (seen.AddIfAbsent(d)) {
					declarations.push_back(d);
				}
			}
		}
	}
	return declarations;
}

} // namespace tsc::ls
