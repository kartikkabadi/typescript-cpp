// codeactions_fixmissingtypeannotation.go — the isolatedDeclarations "missing
// type annotation" fix provider: infers a type (full / relative `typeof` /
// widened) and inserts annotations, inline assertions, or extracted variables.
#include "internal/ls/ls.h"

#include "internal/ast/ast.h"
#include "internal/astnav/tokens.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/locale/locale.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/ls/change/change.h"
#include "internal/nodebuilder/types.h"
#include "internal/printer/printer.h"
#include "internal/scanner/scanner.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace tsc::ls {

namespace {

// isolatedDeclarationsFixErrorCodes — codeactions_fixmissingtypeannotation.go:21.
const std::vector<int32_t> isolatedDeclarationsFixErrorCodes{
    Function_must_have_an_explicit_return_type_annotation_with_isolatedDeclarations
        ->code,
    Method_must_have_an_explicit_return_type_annotation_with_isolatedDeclarations
        ->code,
    At_least_one_accessor_must_have_an_explicit_type_annotation_with_isolatedDeclarations
        ->code,
    Variable_must_have_an_explicit_type_annotation_with_isolatedDeclarations
        ->code,
    Parameter_must_have_an_explicit_type_annotation_with_isolatedDeclarations
        ->code,
    Property_must_have_an_explicit_type_annotation_with_isolatedDeclarations
        ->code,
    Expression_type_can_t_be_inferred_with_isolatedDeclarations->code,
    Binding_elements_with_initializers_can_t_be_exported_directly_with_isolatedDeclarations
        ->code,
    Computed_property_names_on_class_or_object_literals_cannot_be_inferred_with_isolatedDeclarations
        ->code,
    Computed_properties_must_be_number_or_string_literals_variables_or_dotted_expressions_with_isolatedDeclarations
        ->code,
    Enum_member_initializers_must_be_computable_without_references_to_external_symbols_with_isolatedDeclarations
        ->code,
    Extends_clause_can_t_contain_an_expression_with_isolatedDeclarations->code,
    Objects_that_contain_shorthand_properties_can_t_be_inferred_with_isolatedDeclarations
        ->code,
    Objects_that_contain_spread_assignments_can_t_be_inferred_with_isolatedDeclarations
        ->code,
    Arrays_with_spread_elements_can_t_inferred_with_isolatedDeclarations
        ->code,
    Default_exports_can_t_be_inferred_with_isolatedDeclarations->code,
    Only_const_arrays_can_be_inferred_with_isolatedDeclarations->code,
    Assigning_properties_to_functions_without_declaring_them_is_not_supported_with_isolatedDeclarations_Add_an_explicit_declaration_for_the_properties_assigned_to_this_function
        ->code,
    Declaration_emit_for_this_parameter_requires_implicitly_adding_undefined_to_its_type_This_is_not_supported_with_isolatedDeclarations
        ->code,
    Type_containing_private_name_0_can_t_be_used_with_isolatedDeclarations
        ->code,
    Add_satisfies_and_a_type_assertion_to_this_expression_satisfies_T_as_T_to_make_the_type_explicit
        ->code,
};

const std::string fixMissingTypeAnnotationOnExportsFixID =
    "fixMissingTypeAnnotationOnExports";

// canHaveTypeAnnotationKinds — codeactions_fixmissingtypeannotation.go:56.
bool canHaveTypeAnnotationKind(Kind kind) {
	switch (kind) {
	case Kind::GetAccessor:
	case Kind::MethodDeclaration:
	case Kind::PropertyDeclaration:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::ExportAssignment:
	case Kind::ClassDeclaration:
	case Kind::ObjectBindingPattern:
	case Kind::ArrayBindingPattern:
		return true;
	default:
		return false;
	}
}

// declarationEmitNodeBuilderFlags —
// codeactions_fixmissingtypeannotation.go:72.
const nodebuilder::Flags declarationEmitNodeBuilderFlags =
    nodebuilder::FlagsMultilineObjectLiterals |
    nodebuilder::FlagsWriteClassExpressionAsTypeLiteral |
    nodebuilder::FlagsUseTypeOfFunction |
    nodebuilder::FlagsUseStructuralFallback |
    nodebuilder::FlagsAllowEmptyTuple |
    nodebuilder::FlagsGenerateNamesForShadowedTypeParams |
    nodebuilder::FlagsNoTruncation;

// doneGuard — RAII for the `done` release callback returned by
// GetTypeCheckerForFileExclusive (Go `defer done()`).
struct doneGuard {
	std::function<void()> done;
	~doneGuard() {
		if (done) {
			done();
		}
	}
};

// Forward declarations — Go package-level functions defined later in this
// file but referenced earlier.
Node* findExpandoFunction(checker::Checker* ch, Node* node);
Node* findAncestorWithMissingType(Node* node);
Node* findBestFittingNode(Node* node, TextRange span);
bool isNamedDeclarationKind(Node* node);
bool isValueSignatureDeclaration(Node* node);
Node* createAsExpression(NodeFactory* factory, Node* node, Node* typeNode);
bool needsParenthesizedExpressionForAssertion(Node* node);
std::string getIdentifierNameForNode(Node* node);
std::string typeToStringForDiag(Node* typeNode, SourceFile* sourceFile,
                                change::Tracker* ct);
int endOfRequiredTypeParameters(checker::Checker* ch, checker::Type* t);
bool typeParamHasDefault(checker::Type* tp);
CodeAction* tryCodeAction(const gostd::Context& ctx,
                          CodeFixContext* fixContext, checker::Checker* ch,
                          const std::function<std::string(
                              isolatedDeclarationsFixer*)>& fn);

// hasInitializer — utilities.go:3086.
bool hasInitializer(Node* node) {
	switch (node->kind) {
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::BindingElement:
	case Kind::PropertyDeclaration:
	case Kind::PropertyAssignment:
	case Kind::EnumMember:
	case Kind::ForStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::JsxAttribute:
		return node->initializer() != nullptr;
	default:
		return false;
	}
}

// getIsolatedDeclarationsCodeActions —
// codeactions_fixmissingtypeannotation.go:88.
std::pair<std::vector<CodeAction*>, gostd::Error>
getIsolatedDeclarationsCodeActions(const gostd::Context& ctx,
                                   CodeFixContext* fixContext) {
	auto [ch, done] = fixContext->Program->GetTypeCheckerForFileExclusive(
	    fixContext->SourceFile);
	doneGuard doneGuard_{done};

	std::vector<CodeAction*> fixes;

	auto addFix = [&fixes](CodeAction* action) {
		if (action == nullptr) {
			return;
		}
		fixes.push_back(action);
	};

	// Match TS ordering: Full annotation, Relative annotation, Widened
	// annotation, Full inline, Relative inline, Widened inline, Full extract
	const std::vector<typePrintMode> modes = {
	    typePrintModeFull, typePrintModeRelative, typePrintModeWidened};

	for (auto mode : modes) {
		addFix(tryCodeAction(
		    ctx, fixContext, ch,
		    [fixContext, mode](isolatedDeclarationsFixer* f) {
			    f->typePrintMode = mode;
			    return f->addTypeAnnotation(fixContext->Span);
		    }));
	}

	for (auto mode : modes) {
		addFix(tryCodeAction(
		    ctx, fixContext, ch,
		    [fixContext, mode](isolatedDeclarationsFixer* f) {
			    f->typePrintMode = mode;
			    return f->addInlineAssertion(fixContext->Span);
		    }));
	}

	// extractAsVariable only in Full mode
	addFix(tryCodeAction(ctx, fixContext, ch,
	                     [fixContext](isolatedDeclarationsFixer* f) {
		                     f->typePrintMode = typePrintModeFull;
		                     return f->extractAsVariable(fixContext->Span);
	                     }));

	return {fixes, nullptr};
}

// getAllIsolatedDeclarationsCodeActions —
// codeactions_fixmissingtypeannotation.go:128.
std::pair<CombinedCodeActions*, gostd::Error>
getAllIsolatedDeclarationsCodeActions(const gostd::Context& ctx,
                                      CodeFixContext* fixContext) {
	auto [ch, done] = fixContext->Program->GetTypeCheckerForFileExclusive(
	    fixContext->SourceFile);
	doneGuard doneGuard_{done};

	auto* changeTracker =
	    new change::Tracker(format::FormatRequestContext{},
	                    fixContext->Program->Options(),
	                       fixContext->LS->FormatOptions(),
	                       fixContext->LS->Converters());

	isolatedDeclarationsFixer fixer{
	    .sourceFile = fixContext->SourceFile,
	    .program = fixContext->Program,
	    .checker = ch,
	    .changeTracker = changeTracker,
	    .loc = locale::fromContext(ctx),
	    .fixedNodes = {},
	    .typePrintMode = typePrintModeFull,
	};

	auto allDiags = getAllDiagnostics(ctx, fixContext->Program,
	                                  fixContext->SourceFile);
	for (auto* diag : allDiags) {
		if (isFixableDiagnostic(diag, isolatedDeclarationsFixErrorCodes)) {
			auto span =
			    TextRange{diag->Loc().pos(), diag->Loc().end()};
			fixer.addTypeAnnotation(span);
		}
	}

	for (auto* sym : fixer.symbolsToImport) {
		fixer.addSymbolToExistingImport(sym);
	}

	auto [changes, _] = changeTracker->GetChanges();
	std::vector<std::shared_ptr<lsproto::TextEdit>> fileChanges;
	for (auto& e : changes[fixContext->SourceFile->OriginalFileName()]) {
		fileChanges.push_back(std::make_shared<lsproto::TextEdit>(e));
	}
	if (fileChanges.empty()) {
		return {nullptr, nullptr};
	}

	return {new CombinedCodeActions{
	            .Description = ::tsc::localize(
	                locale::fromContext(ctx),
	                Add_all_missing_type_annotations, "", {}),
	            .Changes = fileChanges},
	        nullptr};
}

// tryCodeAction — codeactions_fixmissingtypeannotation.go:168.
CodeAction* tryCodeAction(const gostd::Context& ctx,
                          CodeFixContext* fixContext, checker::Checker* ch,
                          const std::function<std::string(
                              isolatedDeclarationsFixer*)>& fn) {
	auto* changeTracker =
	    new change::Tracker(format::FormatRequestContext{},
	                    fixContext->Program->Options(),
	                       fixContext->LS->FormatOptions(),
	                       fixContext->LS->Converters());

	autoimport::ImportAdder* importAdder = nullptr;
	// importAdder may be nil if the auto-import registry is not available;
	// type node transformation still works without it, just without adding
	// imports.

	isolatedDeclarationsFixer fixer{
	    .sourceFile = fixContext->SourceFile,
	    .program = fixContext->Program,
	    .checker = ch,
	    .changeTracker = changeTracker,
	    .importAdder = importAdder,
	    .loc = locale::fromContext(ctx),
	    .fixedNodes = {},
	};

	std::string description = fn(&fixer);
	if (description == "") {
		return nullptr;
	}

	// Add any symbols that need to be imported to existing import
	// declarations
	for (auto* sym : fixer.symbolsToImport) {
		fixer.addSymbolToExistingImport(sym);
	}

	auto [changes, _] = changeTracker->GetChanges();
	std::vector<std::shared_ptr<lsproto::TextEdit>> fileChanges;
	for (auto& e : changes[fixContext->SourceFile->OriginalFileName()]) {
		fileChanges.push_back(std::make_shared<lsproto::TextEdit>(e));
	}

	// Add import edits if import adder has fixes
	if (importAdder != nullptr && importAdder->HasFixes()) {
		auto importEdits = importAdder->Edits();
		if (importEdits.has_value()) {
			fileChanges.insert(fileChanges.end(), importEdits->begin(),
			                   importEdits->end());
		}
	}

	if (fileChanges.empty()) {
		return nullptr;
	}

	return new CodeAction{
	    .Description = description,
	    .Changes = fileChanges,
	    .FixID = fixMissingTypeAnnotationOnExportsFixID,
	    .FixAllDescription = ::tsc::localize(
	        locale::fromContext(ctx), Add_all_missing_type_annotations, "",
	        {}),
	};
}

// isExpandoPropertyDeclarationForFix matches TS's isExpandoPropertyDeclaration
// which includes PropertyAccessExpression, ElementAccessExpression, and
// BinaryExpression. The shared ast.IsExpandoPropertyDeclaration was narrowed
// to BinaryExpression only for checker purposes. —
// codeactions_fixmissingtypeannotation.go:480.
bool isExpandoPropertyDeclarationForFix(Node* node) {
	return node != nullptr && (isPropertyAccessExpression(node) ||
	                           isElementAccessExpression(node) ||
	                           isBinaryExpression(node));
}

// findExpandoFunction finds the function declaration that has expando
// properties assigned to it — codeactions_fixmissingtypeannotation.go:484.
Node* findExpandoFunction(checker::Checker* ch, Node* node) {
	auto* expandoDeclaration =
	    findAncestorOrQuit(node, [](Node* n) -> FindAncestorResult {
		    if (isStatement(n)) {
			    return FindAncestorResult::Quit;
		    }
		    if (isExpandoPropertyDeclarationForFix(n)) {
			    return FindAncestorResult::True;
		    }
		    return FindAncestorResult::False;
	    });

	if (expandoDeclaration == nullptr ||
	    !isExpandoPropertyDeclarationForFix(expandoDeclaration)) {
		return nullptr;
	}

	auto* assignmentTarget = expandoDeclaration;
	// Some late bound expando members use the whole expression as the
	// declaration.
	if (isBinaryExpression(assignmentTarget)) {
		assignmentTarget = assignmentTarget->as<BinaryExpression>()->Left;
		if (!isExpandoPropertyDeclarationForFix(assignmentTarget)) {
			return nullptr;
		}
	}

	Node* expression = nullptr;
	if (isPropertyAccessExpression(assignmentTarget)) {
		expression =
		    assignmentTarget->as<PropertyAccessExpression>()->Expression;
	} else if (isElementAccessExpression(assignmentTarget)) {
		expression =
		    assignmentTarget->as<ElementAccessExpression>()->Expression;
	} else {
		return nullptr;
	}

	auto* targetType = ch->GetTypeAtLocation(expression);
	if (targetType == nullptr) {
		return nullptr;
	}

	auto properties = ch->GetPropertiesOfType(targetType);
	bool found = false;
	for (auto* p : properties) {
		if (p->valueDeclaration == expandoDeclaration ||
		    p->valueDeclaration == expandoDeclaration->parent) {
			found = true;
			break;
		}
	}
	if (!found) {
		return nullptr;
	}

	auto* symbol = targetType->symbol;
	if (symbol == nullptr || symbol->valueDeclaration == nullptr) {
		return nullptr;
	}

	auto* fn = symbol->valueDeclaration;
	if ((isFunctionExpression(fn) || isArrowFunction(fn)) &&
	    isVariableDeclaration(fn->parent)) {
		return fn->parent;
	}
	if (isFunctionDeclaration(fn)) {
		return fn;
	}

	return nullptr;
}

// needsParenthesizedExpressionForAssertion checks if an expression needs
// parentheses for an assertion — codeactions_fixmissingtypeannotation.go:309.
bool needsParenthesizedExpressionForAssertion(Node* node) {
	return !isEntityNameExpression(node) && !isCallExpression(node) &&
	       !isObjectLiteralExpression(node) &&
	       !isArrayLiteralExpression(node);
}

// createAsExpression creates an `expr as Type` expression, parenthesizing if
// needed — codeactions_fixmissingtypeannotation.go:314.
Node* createAsExpression(NodeFactory* factory, Node* node, Node* typeNode) {
	if (needsParenthesizedExpressionForAssertion(node)) {
		node = factory->newParenthesizedExpression(node);
	}
	return factory->newAsExpression(node, typeNode);
}

// isConstAssertion checks if a node is an `as const` or `<const>` assertion —
// codeactions_fixmissingtypeannotation.go:1108.
// isConstAssertion is defined in internal/ast/ast.h.

// getIdentifierNameForNode derives a meaningful variable name from a node
// expression — codeactions_fixmissingtypeannotation.go:1368.
std::string getIdentifierNameForNode(Node* node) {
	if (isPropertyAccessExpression(node)) {
		auto* name = node->as<PropertyAccessExpression>()->Node::name();
		if (isIdentifier(name) && !isPrivateIdentifier(name) &&
		    identifierToKeywordKind(name->as<Identifier>()) ==
		        Kind::Unknown) {
			return name->text();
		}
	}
	return "newLocal";
}

// typeParamHasDefault checks if a type parameter has a default type
// declaration — codeactions_fixmissingtypeannotation.go:1246.
bool typeParamHasDefault(checker::Type* tp) {
	auto* sym = tp->symbol;
	if (sym == nullptr) {
		return false;
	}
	for (auto* decl : sym->declarations) {
		if (isTypeParameterDeclaration(decl) &&
		    decl->as<TypeParameterDeclaration>()->DefaultType != nullptr) {
			return true;
		}
	}
	return false;
}

// endOfRequiredTypeParameters finds the number of type arguments that are
// actually required (i.e., differ from their defaults) —
// codeactions_fixmissingtypeannotation.go:1209.
int endOfRequiredTypeParameters(checker::Checker* ch, checker::Type* t) {
	auto typeArgs = ch->GetTypeArguments(t);
	if (typeArgs.empty()) {
		return 0;
	}
	auto* target = t->Target();
	if (target == nullptr || target->AsInterfaceType() == nullptr) {
		return (int)typeArgs.size();
	}
	auto typeParams = interfaceTypeTypeParameters(target->AsInterfaceType());
	auto localTypeParams =
	    interfaceTypeLocalTypeParameters(target->AsInterfaceType());
	int outerCount = (int)typeParams.size() - (int)localTypeParams.size();
	for (int cutoff = 0; cutoff < (int)typeArgs.size(); cutoff++) {
		// Skip cutoff positions where the local type parameter has no
		// default. This matches TS's check for constraint === undefined on
		// localTypeParameters, which in practice skips type parameters
		// without defaults (e.g. Set<T> where T has no default should not
		// have <unknown> elided).
		int localIdx = cutoff - outerCount;
		if (localIdx < 0 || localIdx >= (int)localTypeParams.size() ||
		    !typeParamHasDefault(localTypeParams[localIdx])) {
			continue;
		}
		auto filledIn = ch->FillMissingTypeArguments(
		    std::vector<checker::Type*>(typeArgs.begin(),
		                              typeArgs.begin() + cutoff),
		    typeParams, cutoff, false);
		bool allMatch = true;
		for (size_t i = 0; i < filledIn.size(); i++) {
			if (filledIn[i] != typeArgs[i]) {
				allMatch = false;
				break;
			}
		}
		if (allMatch) {
			return cutoff;
		}
	}
	return (int)typeArgs.size();
}

// typeToStringForDiag converts a type node to a string for use in diagnostic
// descriptions — codeactions_fixmissingtypeannotation.go:1282.
std::string typeToStringForDiag(Node* typeNode, SourceFile* sourceFile,
                                change::Tracker* ct) {
	auto savedFlags = ct->emitContext->emitFlags(typeNode);
	ct->emitContext->setEmitFlags(typeNode,
	                              savedFlags | printer::EFSingleLine);
	auto* p = printer::NewPrinter(
	    printer::PrinterOptions{.NewLine = NewLineKind::LineFeed},
	    printer::PrintHandlers{}, ct->emitContext);
	auto [writer, release] = printer::GetSingleLineStringWriter();
	doneGuard releaseGuard{release};
	p->Write(typeNode, sourceFile, writer, nullptr);
	ct->emitContext->setEmitFlags(typeNode, savedFlags);
	std::string result = writer->String();
	if (result.size() > 160) {
		return result.substr(0, 157) + "...";
	}
	return result;
}

// findAncestorWithMissingType walks up the ancestor chain to find a node that
// can have a type annotation and is missing one —
// codeactions_fixmissingtypeannotation.go:1305.
Node* findAncestorWithMissingType(Node* node) {
	return findAncestor(node, [](Node* n) -> bool {
		if (!canHaveTypeAnnotationKind(n->kind)) {
			return false;
		}
		if (isObjectBindingPattern(n) || isArrayBindingPattern(n)) {
			return isVariableDeclaration(n->parent);
		}
		return true;
	});
}

// findBestFittingNode walks up from the token to find the node that best fits
// the diagnostic span — codeactions_fixmissingtypeannotation.go:1318.
Node* findBestFittingNode(Node* node, TextRange span) {
	if (node == nullptr) {
		return nullptr;
	}
	while (node != nullptr && node->end() < span.pos() + span.len()) {
		node = node->parent;
	}
	while (node->parent != nullptr && node->parent->pos() == node->pos() &&
	       node->parent->end() == node->end()) {
		node = node->parent;
	}
	if (isIdentifier(node) && hasInitializer(node->parent) &&
	    node->parent->initializer() != nullptr) {
		return node->parent->initializer();
	}
	if (isIdentifier(node) && isShorthandPropertyAssignment(node->parent)) {
		return node->parent;
	}
	return node;
}

// isNamedDeclarationKind matches TS's isDeclarationKind, which is narrower
// than Go's IsDeclaration — codeactions_fixmissingtypeannotation.go:1340.
bool isNamedDeclarationKind(Node* node) {
	switch (node->kind) {
	case Kind::ArrowFunction:
	case Kind::BindingElement:
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::ClassStaticBlockDeclaration:
	case Kind::Constructor:
	case Kind::EnumDeclaration:
	case Kind::EnumMember:
	case Kind::ExportSpecifier:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::GetAccessor:
	case Kind::ImportClause:
	case Kind::ImportEqualsDeclaration:
	case Kind::ImportSpecifier:
	case Kind::InterfaceDeclaration:
	case Kind::JsxAttribute:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::ModuleDeclaration:
	case Kind::NamespaceExportDeclaration:
	case Kind::NamespaceImport:
	case Kind::NamespaceExport:
	case Kind::Parameter:
	case Kind::PropertyAssignment:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::SetAccessor:
	case Kind::ShorthandPropertyAssignment:
	case Kind::TypeAliasDeclaration:
	case Kind::TypeParameter:
	case Kind::VariableDeclaration:
	case Kind::JSDocTypedefTag:
	case Kind::JSDocCallbackTag:
	case Kind::JSDocPropertyTag:
	case Kind::NamedTupleMember:
		return true;
	default:
		return false;
	}
}

// isValueSignatureDeclaration checks if a node is a function-like declaration
// that produces a value — codeactions_fixmissingtypeannotation.go:1360.
bool isValueSignatureDeclaration(Node* node) {
	return isFunctionExpression(node) || isArrowFunction(node) ||
	       isMethodDeclaration(node) || isAccessor(node) ||
	       isFunctionDeclaration(node) || isConstructorDeclaration(node);
}

} // namespace

// IsolatedDeclarationsFixProvider is the CodeFixProvider for
// isolatedDeclarations-related type annotation fixes —
// codeactions_fixmissingtypeannotation.go:48.
CodeFixProvider* IsolatedDeclarationsFixProvider = new CodeFixProvider{
    .ErrorCodes = isolatedDeclarationsFixErrorCodes,
    .GetCodeActions =
        [](const gostd::Context& ctx, CodeFixContext* fixContext) {
	        return getIsolatedDeclarationsCodeActions(ctx, fixContext);
        },
    .FixIds = {fixMissingTypeAnnotationOnExportsFixID},
    .GetAllCodeActions =
        [](const gostd::Context& ctx, CodeFixContext* fixContext) {
	        return getAllIsolatedDeclarationsCodeActions(ctx, fixContext);
        },
};

// addTypeAnnotation — codeactions_fixmissingtypeannotation.go:229.
std::string isolatedDeclarationsFixer::addTypeAnnotation(TextRange span) {
	auto* nodeWithDiag =
	    astnav::getTokenAtPosition(sourceFile, TextPos(span.pos()));

	auto* expandoFunction =
	    findExpandoFunction(checker, nodeWithDiag);
	if (expandoFunction != nullptr) {
		if (isFunctionDeclaration(expandoFunction)) {
			return createNamespaceForExpandoProperties(expandoFunction);
		}
		return fixIsolatedDeclarationError(expandoFunction);
	}

	auto* nodeMissingType = findAncestorWithMissingType(nodeWithDiag);
	if (nodeMissingType != nullptr) {
		return fixIsolatedDeclarationError(nodeMissingType);
	}
	return "";
}

// createNamespaceForExpandoProperties —
// codeactions_fixmissingtypeannotation.go:247.
std::string
isolatedDeclarationsFixer::createNamespaceForExpandoProperties(
    Node* expandoFunc) {
	auto* funcDecl = expandoFunc->as<FunctionDeclaration>();
	if (funcDecl->Node::name() == nullptr) {
		return "";
	}

	auto* t = checker->GetTypeAtLocation(expandoFunc);
	auto elements = checker->GetPropertiesOfType(t);
	if (elements.empty()) {
		return "";
	}

	auto* factory = changeTracker->nodeFactory;

	std::vector<Node*> newProperties;
	for (auto* symbol : elements) {
		if (!isIdentifierText(symbol->name, LanguageVariant::Standard)) {
			continue;
		}
		// skip symbols that already have a variable declaration
		if (symbol->valueDeclaration != nullptr &&
		    isVariableDeclaration(symbol->valueDeclaration)) {
			continue;
		}

		auto* symType = checker->GetTypeOfSymbol(symbol);
		auto* typeNode = typeToMinimizedReferenceType(
		    symType, expandoFunc, declarationEmitNodeBuilderFlags);
		if (typeNode == nullptr) {
			continue;
		}

		auto* varDecl = factory->newVariableDeclaration(
		    factory->newIdentifier(symbol->name), nullptr, typeNode,
		    nullptr);
		auto* exportToken = factory->newToken(Kind::ExportKeyword);
		auto* varDeclList = factory->newVariableDeclarationList(
		    factory->newNodeList({varDecl}), NodeFlagsNone);
		auto* varStmt = factory->newVariableStatement(
		    factory->newModifierList({exportToken}), varDeclList);
		newProperties.push_back(varStmt);
	}

	if (newProperties.empty()) {
		return "";
	}

	std::vector<Node*> modifiers;
	if (hasSyntacticModifier(expandoFunc, ModifierFlagsExport)) {
		modifiers.push_back(factory->newToken(Kind::ExportKeyword));
	}
	modifiers.push_back(factory->newToken(Kind::DeclareKeyword));

	auto* namespaceNode = factory->newModuleDeclaration(
	    factory->newModifierList(modifiers), Kind::NamespaceKeyword,
	    factory->newIdentifier(funcDecl->Node::name()->text()),
	    nullptr /*attributes*/,
	    factory->newModuleBlock(factory->newNodeList(newProperties)));
	// Set the flags for namespace
	namespaceNode->flags = NodeFlagsAmbient | NodeFlagsExportContext |
	                       NodeFlagsContextFlags;

	changeTracker->InsertNodeAfter(sourceFile, expandoFunc, namespaceNode);
	return ::tsc::localize(
	    loc, Annotate_types_of_properties_expando_function_in_a_namespace,
	    "", {});
}

// addInlineAssertion — codeactions_fixmissingtypeannotation.go:321.
std::string isolatedDeclarationsFixer::addInlineAssertion(TextRange span) {
	auto* nodeWithDiag =
	    astnav::getTokenAtPosition(sourceFile, TextPos(span.pos()));

	// No inline assertions for expando members
	auto* expandoFunction =
	    findExpandoFunction(checker, nodeWithDiag);
	if (expandoFunction != nullptr) {
		return "";
	}

	auto* targetNode = findBestFittingNode(nodeWithDiag, span);
	if (targetNode == nullptr || isValueSignatureDeclaration(targetNode) ||
	    isValueSignatureDeclaration(targetNode->parent)) {
		return "";
	}

	bool isExpressionTarget = isExpression(targetNode);
	bool isShorthandPropertyAssignmentTarget =
	    isShorthandPropertyAssignment(targetNode);

	// Go's IsDeclaration is broader than TS's isDeclaration (e.g.
	// CallExpression has DeclarationData in Go but is not a declaration kind
	// in TS). Use isNamedDeclarationKind to match TS behavior.
	if (!isShorthandPropertyAssignmentTarget &&
	    isNamedDeclarationKind(targetNode)) {
		return "";
	}
	// No inline assertions on binding patterns
	if (findAncestor(targetNode, isBindingPattern) != nullptr) {
		return "";
	}
	// No inline assertions on enum members
	if (findAncestor(targetNode, isEnumMember) != nullptr) {
		return "";
	}
	// No support for typeof in extends clauses
	if (isExpressionTarget &&
	    (findAncestorKind(targetNode, Kind::HeritageClause) != nullptr ||
	     findAncestor(targetNode, isTypeNode) != nullptr)) {
		return "";
	}
	// Can't inline type spread elements
	if (isSpreadElement(targetNode)) {
		return "";
	}

	auto* variableDeclaration =
	    findAncestorKind(targetNode, Kind::VariableDeclaration);
	checker::Type* variableType = nullptr;
	if (variableDeclaration != nullptr) {
		variableType = checker->GetTypeAtLocation(variableDeclaration);
	}
	// Can't use typeof on unique symbols
	if (variableType != nullptr &&
	    (variableType->flags & checker::TypeFlagsUniqueESSymbol) != 0) {
		return "";
	}

	if (!isExpressionTarget && !isShorthandPropertyAssignmentTarget) {
		return "";
	}

	auto* typeNode = inferType(targetNode, variableType);
	if (typeNode == nullptr || mutatedTarget) {
		return "";
	}

	auto* factory = changeTracker->nodeFactory;

	if (isShorthandPropertyAssignmentTarget) {
		// Insert `: expr as Type` after the shorthand property name
		auto* clonedName = deepCloneNode(
		    *factory,
		    targetNode->as<ShorthandPropertyAssignment>()->Node::name());
		auto* asExpr = createAsExpression(factory, clonedName, typeNode);
		changeTracker->InsertNodeAt(sourceFile,
		                            TextPos(targetNode->end()), asExpr,
		                            change::NodeOptions{.Prefix = ": "});
	} else if (isExpressionTarget) {
		// Replace expression with `(expression) satisfies Type as Type` or
		// `expression satisfies Type as Type`
		Node* clonedTarget = deepCloneNode(*factory, targetNode);
		if (needsParenthesizedExpressionForAssertion(targetNode)) {
			clonedTarget =
			    factory->newParenthesizedExpression(clonedTarget);
		}
		auto* clonedType = deepCloneNode(*factory, typeNode);
		auto* satisfiesAsExpr = factory->newAsExpression(
		    factory->newSatisfiesExpression(clonedTarget, clonedType),
		    typeNode);
		changeTracker->ReplaceNode(sourceFile, targetNode,
		                           satisfiesAsExpr, nullptr);
	} else {
		return "";
	}

	return ::tsc::localize(
	    loc, Add_satisfies_and_an_inline_type_assertion_with_0, "",
	    {typeToStringForDiag(typeNode, sourceFile, changeTracker)});
}

// extractAsVariable — codeactions_fixmissingtypeannotation.go:405.
std::string isolatedDeclarationsFixer::extractAsVariable(TextRange span) {
	auto* nodeWithDiag =
	    astnav::getTokenAtPosition(sourceFile, TextPos(span.pos()));
	auto* targetNode = findBestFittingNode(nodeWithDiag, span);
	if (targetNode == nullptr || isValueSignatureDeclaration(targetNode) ||
	    isValueSignatureDeclaration(targetNode->parent)) {
		return "";
	}

	if (!isExpression(targetNode)) {
		return "";
	}

	auto* factory = changeTracker->nodeFactory;

	// Array literals should be marked as const
	if (isArrayLiteralExpression(targetNode)) {
		auto* constRef = factory->newTypeReferenceNode(
		    factory->newIdentifier("const"), nullptr);
		auto* cloned = deepCloneNode(*factory, targetNode);
		changeTracker->ReplaceNode(
		    sourceFile, targetNode,
		    createAsExpression(factory, cloned, constRef), nullptr);
		return ::tsc::localize(loc, Mark_array_literal_as_const, "", {});
	}

	auto* parentPropertyAssignment =
	    findAncestorKind(targetNode, Kind::PropertyAssignment);
	if (parentPropertyAssignment != nullptr) {
		// Identifiers or entity names can already be typeof-ed
		if (parentPropertyAssignment == targetNode->parent &&
		    isEntityNameExpression(targetNode)) {
			return "";
		}

		auto* tempName = changeTracker->emitContext->factory.newUniqueName(
		    getIdentifierNameForNode(targetNode),
		    printer::AutoGenerateOptions{
		        .Flags =
		            printer::GeneratedIdentifierFlagsOptimistic});

		Node* replacementTarget = targetNode;
		Node* initializationNode = targetNode;

		// Handle spread elements: walk up to the spread's parent and
		// handle const assertions
		if (isSpreadElement(replacementTarget)) {
			replacementTarget =
			    walkUpParenthesizedExpressions(replacementTarget->parent);
			if (isConstAssertion(replacementTarget->parent)) {
				replacementTarget = replacementTarget->parent;
				initializationNode = replacementTarget;
			} else {
				auto* constRef = factory->newTypeReferenceNode(
				    factory->newIdentifier("const"), nullptr);
				initializationNode = createAsExpression(
				    factory,
				    deepCloneNode(*factory, replacementTarget),
				    constRef);
			}
		}

		if (isEntityNameExpression(replacementTarget)) {
			return "";
		}

		auto* clonedInit = deepCloneNode(*factory, initializationNode);
		auto* varDecl = factory->newVariableDeclaration(tempName, nullptr,
		                                                nullptr, clonedInit);
		auto* varDeclList = factory->newVariableDeclarationList(
		    factory->newNodeList({varDecl}), NodeFlagsConst);
		auto* varStmt =
		    factory->newVariableStatement(nullptr, varDeclList);

		auto* statement = findAncestor(targetNode, isStatement);
		if (statement == nullptr) {
			return "";
		}
		changeTracker->InsertNodeBefore(sourceFile, statement, varStmt,
		                                false,
		                                change::LeadingTriviaOptionNone);

		auto* typeQuery = factory->newTypeQueryNode(tempName, nullptr);
		auto* asExpr = factory->newAsExpression(tempName, typeQuery);
		changeTracker->ReplaceNode(sourceFile, replacementTarget, asExpr,
		                           nullptr);

		auto idText =
		    typeToStringForDiag(tempName, sourceFile, changeTracker);
		return ::tsc::localize(
		    loc, Extract_to_variable_and_replace_with_0_as_typeof_0, "",
		    {idText});
	}

	return "";
}

// fixIsolatedDeclarationError —
// codeactions_fixmissingtypeannotation.go:550.
std::string
isolatedDeclarationsFixer::fixIsolatedDeclarationError(Node* node) {
	// Avoid creating duplicate fixes for the same node
	if (fixedNodes[node]) {
		return "";
	}
	fixedNodes[node] = true;

	switch (node->kind) {
	case Kind::Parameter:
	case Kind::PropertyDeclaration:
	case Kind::VariableDeclaration:
		return addTypeToVariableLike(node);
	case Kind::ArrowFunction:
	case Kind::FunctionExpression:
	case Kind::FunctionDeclaration:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
		return addTypeToSignatureDeclaration(node);
	case Kind::ExportAssignment:
		return transformExportAssignment(node);
	case Kind::ClassDeclaration:
		return transformExtendsClauseWithExpression(node);
	case Kind::ObjectBindingPattern:
	case Kind::ArrayBindingPattern:
		return transformDestructuringPatterns(node);
	default:
		return "";
	}
}

// addTypeToSignatureDeclaration —
// codeactions_fixmissingtypeannotation.go:574.
std::string
isolatedDeclarationsFixer::addTypeToSignatureDeclaration(Node* funcNode) {
	if (funcNode->type() != nullptr) {
		return "";
	}
	auto* typeNode = inferType(funcNode, nullptr);
	if (typeNode == nullptr) {
		return "";
	}
	changeTracker->TryInsertTypeAnnotation(sourceFile, funcNode, typeNode);
	return ::tsc::localize(
	    loc, Add_return_type_0, "",
	    {typeToStringForDiag(typeNode, sourceFile, changeTracker)});
}

// transformExportAssignment —
// codeactions_fixmissingtypeannotation.go:586.
std::string
isolatedDeclarationsFixer::transformExportAssignment(Node* defaultExport) {
	auto* exportAssignment = defaultExport->as<ExportAssignment>();
	if (exportAssignment->IsExportEquals) {
		return "";
	}

	auto* expression = exportAssignment->Expression;
	auto* typeNode = inferType(expression, nullptr);
	if (typeNode == nullptr) {
		return "";
	}

	auto* factory = changeTracker->nodeFactory;

	auto* defaultIdentifier =
	    changeTracker->emitContext->factory.newUniqueName("_default");

	// Deep clone the expression so synthesized nodes don't reference
	// original source positions
	auto* clonedExpression = deepCloneNode(*factory, expression);

	auto* varDecl = factory->newVariableDeclaration(
	    defaultIdentifier, nullptr, typeNode, clonedExpression);
	auto* varDeclList = factory->newVariableDeclarationList(
	    factory->newNodeList({varDecl}), NodeFlagsConst);
	auto* varStmt = factory->newVariableStatement(nullptr, varDeclList);

	auto* newExport = factory->updateExportAssignment(
	    defaultExport->as<ExportAssignment>(),
	    defaultExport->Node::modifiers(), false, nullptr,
	    defaultIdentifier);

	changeTracker->ReplaceNodeWithNodes(sourceFile, defaultExport,
	                                    {varStmt, newExport}, nullptr);
	return ::tsc::localize(loc, Extract_default_export_to_variable, "", {});
}

// transformExtendsClauseWithExpression —
// codeactions_fixmissingtypeannotation.go:615.
std::string isolatedDeclarationsFixer::transformExtendsClauseWithExpression(
    Node* classDecl) {
	auto* cd = classDecl->as<ClassDeclaration>();
	Node* extendsClause = nullptr;
	if (cd->HeritageClauses != nullptr) {
		for (auto* clause : cd->HeritageClauses->nodes) {
			if (clause->as<HeritageClause>()->Token ==
			    Kind::ExtendsKeyword) {
				extendsClause = clause;
				break;
			}
		}
	}
	if (extendsClause == nullptr) {
		return "";
	}

	auto* heritageTypes = extendsClause->as<HeritageClause>()->Types;
	if (heritageTypes == nullptr || heritageTypes->nodes.empty()) {
		return "";
	}
	auto* heritageExpression = heritageTypes->nodes[0];
	auto* expression =
	    heritageExpression->as<ExpressionWithTypeArguments>()->Expression;

	auto* heritageTypeNode = inferType(expression, nullptr);
	if (heritageTypeNode == nullptr) {
		return "";
	}

	auto* factory = changeTracker->nodeFactory;

	std::string baseName = "Anonymous";
	if (cd->Node::name() != nullptr) {
		baseName = cd->Node::name()->text() + "Base";
	}
	auto* baseClassName =
	    changeTracker->emitContext->factory.newUniqueName(
	        baseName, printer::AutoGenerateOptions{
	                      .Flags =
	                          printer::GeneratedIdentifierFlagsOptimistic});

	// Create: const <BaseName>: <type> = <expression>;
	auto* clonedExpression = deepCloneNode(*factory, expression);
	auto* varDecl =
	    factory->newVariableDeclaration(baseClassName, nullptr,
	                                    heritageTypeNode, clonedExpression);
	auto* varDeclList = factory->newVariableDeclarationList(
	    factory->newNodeList({varDecl}), NodeFlagsConst);
	auto* varStmt = factory->newVariableStatement(nullptr, varDeclList);

	changeTracker->InsertNodeBefore(sourceFile, classDecl, varStmt, false,
	                                change::LeadingTriviaOptionNone);

	// Replace the heritage expression with the base class name
	changeTracker->ReplaceNode(
	    sourceFile, heritageExpression,
	    factory->newExpressionWithTypeArguments(baseClassName, nullptr),
	    nullptr);

	return ::tsc::localize(loc, Extract_base_class_to_variable, "", {});
}

// transformDestructuringPatterns —
// codeactions_fixmissingtypeannotation.go:664.
std::string isolatedDeclarationsFixer::transformDestructuringPatterns(
    Node* bindingPattern) {
	auto* enclosingVariableDeclaration = bindingPattern->parent;
	if (!isVariableDeclaration(enclosingVariableDeclaration)) {
		return "";
	}
	auto* enclosingVarStmt =
	    enclosingVariableDeclaration->parent->parent;
	if (!isVariableStatement(enclosingVarStmt)) {
		return "";
	}

	auto* initializer = enclosingVariableDeclaration->initializer();
	if (initializer == nullptr) {
		return "";
	}

	auto* factory = changeTracker->nodeFactory;
	std::vector<Node*> newNodes;

	Node* baseExprNode = nullptr;
	if (!isIdentifier(initializer)) {
		// Create a temporary variable for complex expressions
		auto* tempName =
		    changeTracker->emitContext->factory.newUniqueName(
		        "dest",
		        printer::AutoGenerateOptions{
		            .Flags = printer::
		                GeneratedIdentifierFlagsOptimistic});
		auto* clonedInitializer = deepCloneNode(*factory, initializer);
		auto* varDecl = factory->newVariableDeclaration(
		    tempName, nullptr, nullptr, clonedInitializer);
		auto* varDeclList = factory->newVariableDeclarationList(
		    factory->newNodeList({varDecl}), NodeFlagsConst);
		auto* varStmt =
		    factory->newVariableStatement(nullptr, varDeclList);
		newNodes.push_back(varStmt);
		baseExprNode = tempName;
	} else {
		// Use a new identifier to avoid referencing original source
		// positions
		baseExprNode = factory->newIdentifier(initializer->text());
	}

	// Extract each binding element as a separate variable with type
	// annotation
	extractBindingElements(bindingPattern, baseExprNode, &newNodes,
	                       enclosingVarStmt);

	if (newNodes.empty()) {
		return "";
	}

	// If the enclosing variable statement has multiple declarations,
	// preserve the non-destructuring ones
	auto* declList =
	    enclosingVarStmt->as<VariableStatement>()
	        ->DeclarationList->as<VariableDeclarationList>();
	if (declList->Declarations->nodes.size() > 1) {
		std::vector<Node*> remainingDecls;
		for (auto* d : declList->Declarations->nodes) {
			if (d != enclosingVariableDeclaration) {
				remainingDecls.push_back(d);
			}
		}
		if (!remainingDecls.empty()) {
			newNodes.push_back(factory->updateVariableStatement(
			    enclosingVarStmt->as<VariableStatement>(),
			    enclosingVarStmt->Node::modifiers(),
			    factory->updateVariableDeclarationList(
			        declList,
			        factory->newNodeList(remainingDecls),
			        declList->flags)));
		}
	}

	changeTracker->ReplaceNodeWithNodes(sourceFile, enclosingVarStmt,
	                                    newNodes, nullptr);
	return ::tsc::localize(loc, Extract_binding_expressions_to_variable, "",
	                     {});
}

// extractBindingElements — codeactions_fixmissingtypeannotation.go:730.
void isolatedDeclarationsFixer::extractBindingElements(
    Node* bindingPattern, Node* baseExpr, std::vector<Node*>* newNodes,
    Node* enclosingVarStmt) {
	auto* factory = changeTracker->nodeFactory;

	if (isObjectBindingPattern(bindingPattern)) {
		for (auto* element :
		     bindingPattern->as<BindingPattern>()->Elements->nodes) {
			if (isOmittedExpression(element)) {
				continue;
			}
			auto* be = element->as<BindingElement>();
			auto* name = be->Node::name();
			if (name == nullptr) {
				continue;
			}

			// Build property access expression
			Node* accessExpr = nullptr;
			if (be->PropertyName != nullptr &&
			    isComputedPropertyName(be->PropertyName)) {
				// Handle computed property names: create a temp variable
				// for the computed expression
				auto* computedExpression =
				    be->PropertyName->as<ComputedPropertyName>()
				        ->Expression;
				auto* identifierForComputedProperty =
				    changeTracker->emitContext->factory
				        .newGeneratedNameForNode(computedExpression);
				auto* compVarDecl = factory->newVariableDeclaration(
				    identifierForComputedProperty, nullptr, nullptr,
				    computedExpression);
				auto* compVarDeclList =
				    factory->newVariableDeclarationList(
				        factory->newNodeList({compVarDecl}),
				        NodeFlagsConst);
				auto* compVarStmt = factory->newVariableStatement(
				    nullptr, compVarDeclList);
				newNodes->push_back(compVarStmt);
				accessExpr = factory->newElementAccessExpression(
				    baseExpr, nullptr, identifierForComputedProperty,
				    NodeFlagsNone);
			} else if (be->PropertyName != nullptr) {
				// Use property name text (handles identifiers, string
				// literals, numeric literals)
				auto propText = be->PropertyName->text();
				accessExpr = factory->newPropertyAccessExpression(
				    baseExpr, nullptr,
				    factory->newIdentifier(propText),
				    NodeFlagsNone);
			} else if (isIdentifier(name)) {
				accessExpr = factory->newPropertyAccessExpression(
				    baseExpr, nullptr,
				    factory->newIdentifier(name->text()),
				    NodeFlagsNone);
			} else {
				continue;
			}

			if (isBindingPattern(name)) {
				extractBindingElements(name, accessExpr, newNodes,
				                       enclosingVarStmt);
			} else {
				emitBindingElementVariable(factory, name, be, accessExpr,
				                           newNodes, enclosingVarStmt);
			}
		}
	} else if (isArrayBindingPattern(bindingPattern)) {
		int i = 0;
		for (auto* element :
		     bindingPattern->as<BindingPattern>()->Elements->nodes) {
			if (isOmittedExpression(element)) {
				i++;
				continue;
			}
			auto* be = element->as<BindingElement>();
			auto* name = be->Node::name();
			if (name == nullptr) {
				i++;
				continue;
			}

			auto* accessExpr = factory->newElementAccessExpression(
			    baseExpr, nullptr,
			    factory->newNumericLiteral(std::to_string(i),
			                               TokenFlagsNone),
			    NodeFlagsNone);

			if (isBindingPattern(name)) {
				extractBindingElements(name, accessExpr, newNodes,
				                       enclosingVarStmt);
			} else {
				emitBindingElementVariable(factory, name, be, accessExpr,
				                           newNodes, enclosingVarStmt);
			}
			i++;
		}
	}
}

// emitBindingElementVariable creates a variable declaration for a single
// binding element, handling default initializers by creating a ternary
// `temp === undefined ? default : temp` —
// codeactions_fixmissingtypeannotation.go:800.
void isolatedDeclarationsFixer::emitBindingElementVariable(
    NodeFactory* factory, Node* name, BindingElement* be, Node* accessExpr,
    std::vector<Node*>* newNodes, Node* enclosingVarStmt) {
	auto* typeNode = inferType(name, nullptr);
	Node* variableInitializer = accessExpr;

	if (be->Initializer != nullptr) {
		// Create a temp variable to hold the accessed value, then use a
		// conditional expression to apply the default:
		// temp === undefined ? defaultValue : temp
		auto* propName = be->PropertyName;
		std::string tempBaseName = "temp";
		if (propName != nullptr && isIdentifier(propName)) {
			tempBaseName = propName->text();
		}
		auto* tempName = changeTracker->emitContext->factory.newUniqueName(
		    tempBaseName,
		    printer::AutoGenerateOptions{
		        .Flags =
		            printer::GeneratedIdentifierFlagsOptimistic});
		auto* tempVarDecl =
		    factory->newVariableDeclaration(tempName, nullptr, nullptr,
		                                    variableInitializer);
		auto* tempVarDeclList = factory->newVariableDeclarationList(
		    factory->newNodeList({tempVarDecl}), NodeFlagsConst);
		auto* tempVarStmt =
		    factory->newVariableStatement(nullptr, tempVarDeclList);
		newNodes->push_back(tempVarStmt);

		variableInitializer = factory->newConditionalExpression(
		    factory->newBinaryExpression(
		        nullptr, tempName, nullptr,
		        factory->newToken(Kind::EqualsEqualsEqualsToken),
		        factory->newIdentifier("undefined")),
		    factory->newToken(Kind::QuestionToken), be->Initializer,
		    factory->newToken(Kind::ColonToken), variableInitializer);
	}

	auto* exportModifier = getExportModifier(enclosingVarStmt);
	auto* varDecl = factory->newVariableDeclaration(
	    factory->newIdentifier(name->text()), nullptr, typeNode,
	    variableInitializer);
	auto* varDeclList = factory->newVariableDeclarationList(
	    factory->newNodeList({varDecl}), NodeFlagsConst);
	auto* varStmt =
	    factory->newVariableStatement(exportModifier, varDeclList);
	newNodes->push_back(varStmt);
}

// getExportModifier — codeactions_fixmissingtypeannotation.go:847.
ModifierList* isolatedDeclarationsFixer::getExportModifier(
    Node* enclosingVarStmt) {
	if (hasSyntacticModifier(enclosingVarStmt, ModifierFlagsExport)) {
		auto* exportToken =
		    changeTracker->nodeFactory->newToken(Kind::ExportKeyword);
		return changeTracker->nodeFactory->newModifierList({exportToken});
	}
	return nullptr;
}

// inferType — codeactions_fixmissingtypeannotation.go:855.
Node* isolatedDeclarationsFixer::inferType(Node* node,
                                           ::tsc::checker::Type* variableType) {
	mutatedTarget = false;

	// Handle Relative mode first: return typeof X for identifiers
	if (typePrintMode == typePrintModeRelative) {
		return relativeType(node);
	}

	checker::Type* t = nullptr;

	if (isValueSignatureDeclaration(node)) {
		auto* signature = checker->GetSignatureFromDeclaration(node);
		if (signature != nullptr) {
			auto* typePredicate =
			    checker->GetTypePredicateOfSignature(signature);
			if (typePredicate != nullptr) {
				if (typePredicate->t == nullptr) {
					return nullptr;
				}
				auto* enclosingDecl =
				    findAncestor(node, isDeclaration);
				if (enclosingDecl == nullptr) {
					enclosingDecl = sourceFile->asNode();
				}
				auto flags = declarationEmitNodeBuilderFlags;
				if ((typePredicate->t->flags &
				     checker::TypeFlagsUniqueESSymbol) != 0) {
					flags |= nodebuilder::FlagsAllowUniqueESSymbolType;
				}
				auto* result = checker->TypePredicateToTypePredicateNode(
				    typePredicate, enclosingDecl, flags, nullptr);
				if (result != nullptr) {
					return result;
				}
				return nullptr;
			}
			t = checker->GetReturnTypeOfSignature(signature);
		}
	} else {
		t = checker->GetTypeAtLocation(node);
	}

	if (t == nullptr) {
		return nullptr;
	}

	// Handle Widened mode: return widened literal type if different
	if (typePrintMode == typePrintModeWidened) {
		if (variableType != nullptr) {
			t = variableType;
		}
		auto* widenedType = checker->GetWidenedLiteralType(t);
		if (checker->IsTypeAssignableTo(widenedType, t)) {
			return nullptr; // widened type is same, no fix needed
		}
		t = widenedType;
	}

	auto* enclosingDecl = findAncestor(node, isDeclaration);
	if (enclosingDecl == nullptr) {
		enclosingDecl = sourceFile->asNode();
	}

	auto flags = declarationEmitNodeBuilderFlags | getExtraFlags(node, t);

	// For parameters that require adding implicit undefined, add it to the
	// type
	if (isParameterDeclaration(node) &&
	    checker->RequiresAddingImplicitUndefined(node)) {
		t = checker->GetUnionTypeEx(
		    {checker->GetUndefinedType(), t},
		    checker::UnionReductionNone);
	}

	auto* typeNode = typeToMinimizedReferenceType(t, enclosingDecl, flags);
	return typeNode;
}

// getExtraFlags — codeactions_fixmissingtypeannotation.go:925.
nodebuilder::Flags
isolatedDeclarationsFixer::getExtraFlags(Node* node,
                                         ::tsc::checker::Type* t) {
	if ((isVariableDeclaration(node) ||
	     (isPropertyDeclaration(node) &&
	      hasSyntacticModifier(node, ModifierFlagsStatic |
	                                     ModifierFlagsReadonly))) &&
	    (t->flags & checker::TypeFlagsUniqueESSymbol) != 0) {
		return nodebuilder::FlagsAllowUniqueESSymbolType;
	}
	return nodebuilder::FlagsNone;
}

// createTypeOfFromEntityNameExpression creates a `typeof X` type query node —
// codeactions_fixmissingtypeannotation.go:935.
Node* isolatedDeclarationsFixer::createTypeOfFromEntityNameExpression(
    Node* node) {
	return changeTracker->nodeFactory->newTypeQueryNode(
	    deepCloneNode(*changeTracker->nodeFactory, node), nullptr);
}

// typeFromArraySpreadElements decomposes an array literal with spread
// elements into separate variables, returning a tuple type of typeof
// references — codeactions_fixmissingtypeannotation.go:943.
Node* isolatedDeclarationsFixer::typeFromArraySpreadElements(
    ArrayLiteralExpression* node, const std::string& name_) {
	bool isInConstContext =
	    findAncestor(node->asNode(), isConstAssertion) != nullptr;
	if (!isInConstContext) {
		return nullptr;
	}
	std::string name = name_;
	if (name == "") {
		name = "temp";
	}
	auto* factory = changeTracker->nodeFactory;
	return typeFromSpreads(
	    node->asNode(), name, isInConstContext,
	    [](Node* n) -> std::vector<Node*> {
		    return n->as<ArrayLiteralExpression>()->Elements->nodes;
	    },
	    [](Node* n) { return isSpreadElement(n); },
	    [factory](Node* expr) {
		    return factory->newSpreadElement(expr);
	    },
	    [factory](std::vector<Node*> elements) {
		    return factory->newArrayLiteralExpression(
		        factory->newNodeList(elements), true);
	    },
	    [factory](std::vector<Node*> types) {
		    std::vector<Node*> restTypes(types.size());
		    for (size_t i = 0; i < types.size(); i++) {
			    restTypes[i] = factory->newRestTypeNode(types[i]);
		    }
		    return factory->newTupleTypeNode(
		        factory->newNodeList(restTypes));
	    });
}

// typeFromObjectSpreadAssignment decomposes an object literal with spread
// assignments into separate variables, returning an intersection type of
// typeof references — codeactions_fixmissingtypeannotation.go:978.
Node* isolatedDeclarationsFixer::typeFromObjectSpreadAssignment(
    ObjectLiteralExpression* node, const std::string& name_) {
	bool isInConstContext =
	    findAncestor(node->asNode(), isConstAssertion) != nullptr;
	std::string name = name_;
	if (name == "") {
		name = "temp";
	}
	auto* factory = changeTracker->nodeFactory;
	return typeFromSpreads(
	    node->asNode(), name, isInConstContext,
	    [](Node* n) -> std::vector<Node*> {
		    if (n->as<ObjectLiteralExpression>()->Properties != nullptr) {
			    return n->as<ObjectLiteralExpression>()->Properties->nodes;
		    }
		    return {};
	    },
	    [](Node* n) { return isSpreadAssignment(n); },
	    [factory](Node* expr) {
		    return factory->newSpreadAssignment(expr);
	    },
	    [factory](std::vector<Node*> elements) {
		    return factory->newObjectLiteralExpression(
		        factory->newNodeList(elements), true);
	    },
	    [factory](std::vector<Node*> types) {
		    return factory->newIntersectionTypeNode(
		        factory->newNodeList(types));
	    });
}

// typeFromSpreads is the generic spread decomposition function, ported from
// TS's typeFromSpreads — codeactions_fixmissingtypeannotation.go:1009.
Node* isolatedDeclarationsFixer::typeFromSpreads(
    Node* node, const std::string& name, bool isInConstContext,
    const std::function<std::vector<Node*>(Node*)>& getChildren,
    const std::function<bool(Node*)>& isSpread,
    const std::function<Node*(Node*)>& createSpread,
    const std::function<Node*(std::vector<Node*>)>& makeNodeOfKind,
    const std::function<Node*(std::vector<Node*>)>& finalType) {
	auto* factory = changeTracker->nodeFactory;
	std::vector<Node*> intersectionTypes;
	std::vector<Node*> newSpreads;
	std::vector<Node*> currentVariableProperties;

	auto* statement = findAncestor(node, isStatement);

	auto children = getChildren(node);
	for (auto* prop : children) {
		if (isSpread(prop)) {
			finalizesVariablePart(factory, name, isInConstContext,
			                      statement, makeNodeOfKind, createSpread,
			                      &currentVariableProperties,
			                      &intersectionTypes, &newSpreads);
			if (isEntityNameExpression(prop->expression())) {
				intersectionTypes.push_back(
				    createTypeOfFromEntityNameExpression(
				        prop->expression()));
				newSpreads.push_back(prop);
			} else {
				makeSpreadVariable(factory, name, isInConstContext,
				                   statement, createSpread,
				                   prop->expression(), &intersectionTypes,
				                   &newSpreads);
			}
		} else {
			currentVariableProperties.push_back(prop);
		}
	}

	if (newSpreads.empty()) {
		return nullptr;
	}

	finalizesVariablePart(factory, name, isInConstContext, statement,
	                      makeNodeOfKind, createSpread,
	                      &currentVariableProperties, &intersectionTypes,
	                      &newSpreads);

	changeTracker->ReplaceNode(sourceFile, node, makeNodeOfKind(newSpreads),
	                           nullptr);
	mutatedTarget = true;

	return finalType(intersectionTypes);
}

// makeSpreadVariable creates a const variable for a spread expression and
// adds it to the decomposition —
// codeactions_fixmissingtypeannotation.go:1054.
void isolatedDeclarationsFixer::makeSpreadVariable(
    NodeFactory* factory, const std::string& name, bool isInConstContext,
    Node* statement, const std::function<Node*(Node*)>& createSpread,
    Node* expression, std::vector<Node*>* intersectionTypes,
    std::vector<Node*>* newSpreads) {
	auto* tempName =
	    changeTracker->emitContext->factory
	        .newUniqueName(name + "_Part" +
	                           std::to_string(newSpreads->size() + 1),
	                       printer::AutoGenerateOptions{
	                           .Flags = printer::
	                               GeneratedIdentifierFlagsOptimistic});

	Node* initializer = nullptr;
	if (!isInConstContext) {
		initializer = deepCloneNode(*factory, expression);
	} else {
		auto* constRef = factory->newTypeReferenceNode(
		    factory->newIdentifier("const"), nullptr);
		initializer = factory->newAsExpression(
		    deepCloneNode(*factory, expression), constRef);
	}

	auto* varDecl =
	    factory->newVariableDeclaration(tempName, nullptr, nullptr,
	                                    initializer);
	auto* varDeclList = factory->newVariableDeclarationList(
	    factory->newNodeList({varDecl}), NodeFlagsConst);
	auto* varStmt = factory->newVariableStatement(nullptr, varDeclList);

	if (statement != nullptr) {
		changeTracker->InsertNodeBefore(sourceFile, statement, varStmt,
		                                false,
		                                change::LeadingTriviaOptionNone);
	}

	intersectionTypes->push_back(
	    createTypeOfFromEntityNameExpression(tempName));
	newSpreads->push_back(createSpread(tempName));
}

// finalizesVariablePart finalizes accumulated non-spread properties into a
// variable — codeactions_fixmissingtypeannotation.go:1090.
void isolatedDeclarationsFixer::finalizesVariablePart(
    NodeFactory* factory, const std::string& name, bool isInConstContext,
    Node* statement,
    const std::function<Node*(std::vector<Node*>)>& makeNodeOfKind,
    const std::function<Node*(Node*)>& createSpread,
    std::vector<Node*>* currentVariableProperties,
    std::vector<Node*>* intersectionTypes,
    std::vector<Node*>* newSpreads) {
	if (!currentVariableProperties->empty()) {
		makeSpreadVariable(factory, name, isInConstContext, statement,
		                   createSpread,
		                   makeNodeOfKind(*currentVariableProperties),
		                   intersectionTypes, newSpreads);
		*currentVariableProperties = {};
	}
}

// relativeType creates a typeof expression for a node, used in
// typePrintModeRelative — codeactions_fixmissingtypeannotation.go:1119.
Node* isolatedDeclarationsFixer::relativeType(Node* node) {
	if (isParameterDeclaration(node)) {
		return nullptr;
	}
	if (isShorthandPropertyAssignment(node)) {
		return createTypeOfFromEntityNameExpression(
		    node->as<ShorthandPropertyAssignment>()->Node::name());
	}
	if (isEntityNameExpression(node)) {
		return createTypeOfFromEntityNameExpression(node);
	}
	if (isConstAssertion(node)) {
		return relativeType(node->expression());
	}
	if (isArrayLiteralExpression(node)) {
		auto* varDecl =
		    findAncestorKind(node, Kind::VariableDeclaration);
		std::string partName;
		if (varDecl != nullptr && isIdentifier(varDecl->name())) {
			partName = varDecl->name()->text();
		}
		return typeFromArraySpreadElements(
		    node->as<ArrayLiteralExpression>(), partName);
	}
	if (isObjectLiteralExpression(node)) {
		auto* varDecl =
		    findAncestorKind(node, Kind::VariableDeclaration);
		std::string partName;
		if (varDecl != nullptr && isIdentifier(varDecl->name())) {
			partName = varDecl->name()->text();
		}
		return typeFromObjectSpreadAssignment(
		    node->as<ObjectLiteralExpression>(), partName);
	}
	if (isVariableDeclaration(node) && node->initializer() != nullptr) {
		return relativeType(node->initializer());
	}
	if (isConditionalExpression(node)) {
		auto* cond = node->as<ConditionalExpression>();
		auto* trueType = relativeType(cond->WhenTrue);
		if (trueType == nullptr) {
			return nullptr;
		}
		bool trueMutated = mutatedTarget;
		auto* falseType = relativeType(cond->WhenFalse);
		if (falseType == nullptr) {
			return nullptr;
		}
		mutatedTarget = trueMutated || mutatedTarget;
		auto* factory = changeTracker->nodeFactory;
		return factory->newUnionTypeNode(
		    factory->newNodeList({trueType, falseType}));
	}
	return nullptr;
}

// typeToMinimizedReferenceType converts a type to a type node, then trims
// trailing type arguments that match their defaults —
// codeactions_fixmissingtypeannotation.go:1172.
Node* isolatedDeclarationsFixer::typeToMinimizedReferenceType(
    ::tsc::checker::Type* t, Node* enclosingDecl,
    nodebuilder::Flags flags) {
	auto* idToSymbol = new std::unordered_map<Node*, Symbol*>();
	// !!! When truncation tracking is supported, check if the type was
	// truncated and return
	// factory->newKeywordTypeNode(Kind::AnyKeyword) instead of the truncated
	// node.
	auto* typeNode = checker->TypeToTypeNodeEx(
	    t, enclosingDecl, flags, nodebuilder::InternalFlagsWriteComputedProps,
	    idToSymbol);
	if (typeNode == nullptr) {
		return nullptr;
	}
	if (isTypeReferenceNode(typeNode) &&
	    (t->objectFlags & checker::ObjectFlagsReference) != 0) {
		auto typeArgs = checker->GetTypeArguments(t);
		auto nodeTypeArgs = typeNode->typeArguments();
		if (!typeArgs.empty() && !nodeTypeArgs.empty()) {
			int cutoff = endOfRequiredTypeParameters(checker, t);
			if (cutoff < (int)nodeTypeArgs.size()) {
				// Trim trailing default type arguments
				auto* trimmedArgs =
				    changeTracker->nodeFactory->newNodeList(
				        std::vector<Node*>(
				            nodeTypeArgs.begin(),
				            nodeTypeArgs.begin() + cutoff));
				typeNode = changeTracker->nodeFactory
				               ->updateTypeReferenceNode(
				                   typeNode->as<TypeReferenceNode>(),
				                   typeNode->as<TypeReferenceNode>()
				                       ->TypeName,
				                   trimmedArgs);
			}
		}
	}
	// Convert import type references (e.g. import("./path").Name) to simple
	// type references and collect symbols that need to be imported
	auto [referenceTypeNode, importableSymbols] =
	    autoimport::TryGetAutoImportableReferenceFromTypeNode(
	        typeNode, idToSymbol, changeTracker->nodeFactory);
	if (referenceTypeNode != nullptr) {
		typeNode = referenceTypeNode;
		symbolsToImport.insert(symbolsToImport.end(),
		                       importableSymbols.begin(),
		                       importableSymbols.end());
	}
	return typeNode;
}

// addTypeToVariableLike — codeactions_fixmissingtypeannotation.go:1259.
std::string isolatedDeclarationsFixer::addTypeToVariableLike(Node* decl) {
	auto* typeNode = inferType(decl, nullptr);
	if (typeNode == nullptr) {
		return "";
	}
	if (decl->type() != nullptr) {
		changeTracker->ReplaceNode(sourceFile, decl->type(), typeNode,
		                           nullptr);
	} else {
		changeTracker->TryInsertTypeAnnotation(sourceFile, decl, typeNode);
		// Parenthesize paren-less arrow function parameters (`x => ...`) so
		// the inserted `: T` produces `(x: T) => ...` instead of the
		// invalid `x: T => ...`. Queued after the type annotation so that
		// the `)` edit at param.End() sorts after the annotation insertion.
		if (isParameterDeclaration(decl) && decl->parent != nullptr &&
		    isArrowFunction(decl->parent)) {
			changeTracker->ParenthesizeArrowParameters(sourceFile,
			                                         decl->parent);
		}
	}
	return ::tsc::localize(
	    loc, Add_annotation_of_type_0, "",
	    {typeToStringForDiag(typeNode, sourceFile, changeTracker)});
}

// addSymbolToExistingImport finds the existing import declaration for the
// symbol's module and adds the symbol name to the named imports —
// codeactions_fixmissingtypeannotation.go:1380.
void isolatedDeclarationsFixer::addSymbolToExistingImport(Symbol* sym) {
	if (sym == nullptr || sym->parent == nullptr) {
		return;
	}

	// Find the module specifier for this symbol
	auto* moduleSymbol = sym->parent;
	auto& symbolName = sym->name;

	// Walk the source file's import declarations to find the one importing
	// from the same module
	for (auto* stmt : sourceFile->Statements->nodes) {
		if (!isImportDeclaration(stmt)) {
			continue;
		}
		auto* importDecl = stmt->as<ImportDeclaration>();
		if (importDecl->ImportClause == nullptr) {
			continue;
		}

		// Check if this import is from the same module
		auto* importModuleSymbol =
		    checker->GetSymbolAtLocation(importDecl->ModuleSpecifier);
		if (importModuleSymbol == nullptr ||
		    checker->GetMergedSymbol(importModuleSymbol) !=
		        checker->GetMergedSymbol(moduleSymbol)) {
			continue;
		}

		// Found the matching import - add the symbol to named imports
		auto* importClause = importDecl->ImportClause->as<ImportClause>();
		if (importClause->NamedBindings != nullptr &&
		    isNamedImports(importClause->NamedBindings)) {
			// Add to existing named imports
			auto existingElements =
			    importClause->NamedBindings->as<NamedImports>()
			        ->Elements->nodes;
			auto* factory = changeTracker->nodeFactory;
			auto* newSpecifier = factory->newImportSpecifier(
			    false, nullptr, factory->newIdentifier(symbolName));
			existingElements.push_back(newSpecifier);
			auto* newNamedImports = factory->newNamedImports(
			    factory->newNodeList(existingElements));
			auto* newImportClause = factory->updateImportClause(
			    importClause, importClause->PhaseModifier,
			    importClause->Node::name(), newNamedImports);
			auto* newImportDecl = factory->updateImportDeclaration(
			    importDecl, importDecl->Node::modifiers(),
			    newImportClause, importDecl->ModuleSpecifier,
			    importDecl->Attributes);
			changeTracker->ReplaceNode(sourceFile, stmt, newImportDecl,
			                           nullptr);
		}
		return;
	}
}

} // namespace tsc::ls
