// tsc/internal/transformers/declarations/transform.go (2995 lines) — C++23 port.
// Faithful function-by-function port. Go `panic`/`debug.Fail` -> TSC_UNREACHABLE;
// core.* helpers -> manual loops per cpp/internal/checker/PORTING.md.
#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/transformers/declarations/declarations.h"
#include "internal/evaluator/evaluator.h"
#include "internal/scanner/scanner.h"
#include "internal/tspath/tspath.h"
#include "internal/jsnum/jsnum.h"

namespace tsc::transformers::declarations {

namespace {

// Go `defer` analog.
struct ScopeExit {
	std::function<void()> f;
	~ScopeExit() { f(); }
};

// transform.go:46 thisPropertyAssignmentKey
struct thisPropertyAssignmentKey {
	std::string name;
	Node* node = nullptr;
	bool isStatic = false;
	bool isPrivate = false;
	bool operator==(const thisPropertyAssignmentKey&) const = default;
};

struct thisPropertyAssignmentKeyHash {
	size_t operator()(const thisPropertyAssignmentKey& k) const {
		size_t h = std::hash<std::string>()(k.name);
		h ^= std::hash<Node*>()(k.node) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<bool>()(k.isStatic) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<bool>()(k.isPrivate) + 0x9e3779b9 + (h << 6) + (h >> 2);
		return h;
	}
};

// transform.go:53 getThisPropertyAssignmentKey
thisPropertyAssignmentKey getThisPropertyAssignmentKey(Node* name, Node* node,
                                                       bool isStatic) {
	bool isPrivate = name != nullptr && isPrivateIdentifier(name);
	if (name != nullptr && !isDynamicName(name)) {
		std::string nameText;
		if (tryGetTextOfPropertyName(name, nameText)) {
			return thisPropertyAssignmentKey{nameText, nullptr, isStatic,
			                                 isPrivate};
		}
	}
	return thisPropertyAssignmentKey{"", node, isStatic, isPrivate};
}

// transform.go:276 throwDiagnostic
SymbolAccessibilityDiagnostic* throwDiagnostic(
	printer::SymbolAccessibilityResult& /*result*/) {
	TSC_UNREACHABLE("Diagnostic emitted without context");
}

// transform.go:316 nodeOrSyntaxListChildren
std::vector<Node*> nodeOrSyntaxListChildren(Node* node) {
	if (isSyntaxList(node)) {
		return node->as<SyntaxList>()->Children;
	}
	return {node};
}

// transform.go:323 flattenSyntaxLists (core.FlatMap analog)
std::vector<Node*> flattenSyntaxLists(const std::vector<Node*>& nodes) {
	std::vector<Node*> result;
	for (Node* n : nodes) {
		auto children = nodeOrSyntaxListChildren(n);
		result.insert(result.end(), children.begin(), children.end());
	}
	return result;
}

// transform.go:382 createEmptyExports
Node* createEmptyExports(NodeFactory* factory) {
	return factory->newExportDeclaration(
		nullptr /*isTypeOnly*/, false,
		factory->newNamedExports(factory->newNodeList({})), nullptr, nullptr);
}

// transform.go:859 hasAnyBindingInitializers
bool hasAnyBindingInitializers(BindingPattern* bindingPattern) {
	for (Node* elem : bindingPattern->Elements->nodes) {
		if (!isBindingElement(elem)) {
			continue;
		}
		BindingElement* e = elem->as<BindingElement>();
		if (e->Initializer != nullptr) {
			return true;
		}
		if (e->name != nullptr && isBindingPattern(e->name) &&
		    hasAnyBindingInitializers(e->name->as<BindingPattern>())) {
			return true;
		}
	}
	return false;
}

// transform.go:1562 isCommonJSAliasExport
bool isCommonJSAliasExport(Node* node) {
	if (isBinaryExpression(node) &&
	    isIdentifier(node->as<BinaryExpression>()->Right)) {
		if (Symbol* symbol = node->symbol();
		    symbol != nullptr && symbol->data->declarations.size() == 1) {
			return true;
		}
	}
	return false;
}

// transform.go:2153 isClassExtendingNull
bool isClassExtendingNull(Node* node) {
	if (node == nullptr) {
		return false;
	}
	Node* extendsClause = getHeritageClause(node, Kind::ExtendsKeyword);
	if (extendsClause == nullptr) {
		return false;
	}
	NodeList* types = extendsClause->as<HeritageClause>()->Types;
	if (types == nullptr || types->nodes.size() != 1) {
		return false;
	}
	Node* expr = types->nodes[0]->as<ExpressionWithTypeArguments>()->Expression;
	return expr != nullptr && expr->kind == Kind::NullKeyword;
}

// ast/utilities.go:4142 HasInferredType — replica.
bool hasInferredType(Node* node) {
	switch (node->kind) {
	case Kind::Parameter:
	case Kind::PropertySignature:
	case Kind::PropertyDeclaration:
	case Kind::BindingElement:
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
	case Kind::BinaryExpression:
	case Kind::CallExpression:
	case Kind::VariableDeclaration:
	case Kind::ExportAssignment:
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::JSDocParameterTag:
	case Kind::JSDocPropertyTag:
		return true;
	default:
		return false;
	}
}

// ast/utilities.go:4119 IsPrimitiveLiteralValue — replica.
bool isPrimitiveLiteralValue(Node* node, bool includeBigInt) {
	switch (node->kind) {
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::NumericLiteral:
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
		return true;
	case Kind::BigIntLiteral:
		return includeBigInt;
	case Kind::PrefixUnaryExpression: {
		PrefixUnaryExpression* p = node->as<PrefixUnaryExpression>();
		if (p->Operator == Kind::MinusToken) {
			return isNumericLiteral(p->Operand) ||
			       (includeBigInt && isBigIntLiteral(p->Operand));
		}
		if (p->Operator == Kind::PlusToken) {
			return isNumericLiteral(p->Operand);
		}
		return false;
	}
	default:
		return false;
	}
}

// ast/utilities.go:1703 IsExternalModuleIndicator — replica.
bool isExternalModuleIndicator(Node* node) {
	// Exported top-level member indicates moduleness
	return isAnyImportOrReExport(node) || isExportAssignment(node) ||
	       hasSyntacticModifier(node, ModifierFlagsExport);
}

// ast/utilities.go:3291 CreateModifiersFromModifierFlags — replica.
std::vector<Node*> createModifiersFromModifierFlags(
	ModifierFlags flags, Node* (*createModifier)(NodeFactory& f, Kind kind),
	NodeFactory& f) {
	std::vector<Node*> result;
	if ((flags & ModifierFlagsExport) != 0) {
		result.push_back(createModifier(f, Kind::ExportKeyword));
	}
	if ((flags & ModifierFlagsAmbient) != 0) {
		result.push_back(createModifier(f, Kind::DeclareKeyword));
	}
	if ((flags & ModifierFlagsDefault) != 0) {
		result.push_back(createModifier(f, Kind::DefaultKeyword));
	}
	if ((flags & ModifierFlagsConst) != 0) {
		result.push_back(createModifier(f, Kind::ConstKeyword));
	}
	if ((flags & ModifierFlagsPublic) != 0) {
		result.push_back(createModifier(f, Kind::PublicKeyword));
	}
	if ((flags & ModifierFlagsPrivate) != 0) {
		result.push_back(createModifier(f, Kind::PrivateKeyword));
	}
	if ((flags & ModifierFlagsProtected) != 0) {
		result.push_back(createModifier(f, Kind::ProtectedKeyword));
	}
	if ((flags & ModifierFlagsAbstract) != 0) {
		result.push_back(createModifier(f, Kind::AbstractKeyword));
	}
	if ((flags & ModifierFlagsStatic) != 0) {
		result.push_back(createModifier(f, Kind::StaticKeyword));
	}
	if ((flags & ModifierFlagsOverride) != 0) {
		result.push_back(createModifier(f, Kind::OverrideKeyword));
	}
	if ((flags & ModifierFlagsReadonly) != 0) {
		result.push_back(createModifier(f, Kind::ReadonlyKeyword));
	}
	if ((flags & ModifierFlagsAccessor) != 0) {
		result.push_back(createModifier(f, Kind::AccessorKeyword));
	}
	if ((flags & ModifierFlagsAsync) != 0) {
		result.push_back(createModifier(f, Kind::AsyncKeyword));
	}
	if ((flags & ModifierFlagsIn) != 0) {
		result.push_back(createModifier(f, Kind::InKeyword));
	}
	if ((flags & ModifierFlagsOut) != 0) {
		result.push_back(createModifier(f, Kind::OutKeyword));
	}
	return result;
}

Node* newModifierFromFactory(NodeFactory& f, Kind kind) {
	return f.newToken(kind);
}

// ast/utilities.go:3352 ReplaceModifiers — replica.
Node* replaceModifiers(NodeFactory& f, Node* node, ModifierList* modifierArray) {
	switch (node->kind) {
	case Kind::TypeParameter: {
		auto* d = node->as<TypeParameterDeclaration>();
		return f.updateTypeParameterDeclaration(
			d, modifierArray, node->name(), d->Constraint, d->Expression,
			d->DefaultType);
	}
	case Kind::Parameter: {
		auto* d = node->as<ParameterDeclaration>();
		return f.updateParameterDeclaration(
			d, modifierArray, d->DotDotDotToken, node->name(),
			node->questionToken(), node->type(), node->initializer());
	}
	case Kind::ConstructorType: {
		auto* d = node->as<ConstructorTypeNode>();
		return f.updateConstructorTypeNode(
			d, modifierArray, node->typeParameterList(),
			node->parameterList(), node->type());
	}
	case Kind::PropertySignature: {
		auto* d = node->as<PropertySignatureDeclaration>();
		return f.updatePropertySignatureDeclaration(
			d, modifierArray, node->name(), node->postfixToken(),
			node->type(), node->initializer());
	}
	case Kind::PropertyDeclaration: {
		auto* d = node->as<PropertyDeclaration>();
		return f.updatePropertyDeclaration(
			d, modifierArray, node->name(), node->postfixToken(),
			node->type(), node->initializer());
	}
	case Kind::MethodSignature: {
		auto* d = node->as<MethodSignatureDeclaration>();
		return f.updateMethodSignatureDeclaration(
			d, modifierArray, node->name(), node->postfixToken(),
			node->typeParameterList(), node->parameterList(), node->type());
	}
	case Kind::MethodDeclaration: {
		auto* d = node->as<MethodDeclaration>();
		return f.updateMethodDeclaration(
			d, modifierArray, d->AsteriskToken, node->name(),
			node->postfixToken(), node->typeParameterList(),
			node->parameterList(), node->type(), d->FullSignature,
			node->body());
	}
	case Kind::Constructor: {
		auto* d = node->as<ConstructorDeclaration>();
		return f.updateConstructorDeclaration(
			d, modifierArray, node->typeParameterList(),
			node->parameterList(), node->type(), d->FullSignature,
			node->body());
	}
	case Kind::GetAccessor: {
		auto* d = node->as<GetAccessorDeclaration>();
		return f.updateGetAccessorDeclaration(
			d, modifierArray, node->name(), node->typeParameterList(),
			node->parameterList(), node->type(), d->FullSignature,
			node->body());
	}
	case Kind::SetAccessor: {
		auto* d = node->as<SetAccessorDeclaration>();
		return f.updateSetAccessorDeclaration(
			d, modifierArray, node->name(), node->typeParameterList(),
			node->parameterList(), node->type(), d->FullSignature,
			node->body());
	}
	case Kind::IndexSignature: {
		auto* d = node->as<IndexSignatureDeclaration>();
		return f.updateIndexSignatureDeclaration(
			d, modifierArray, node->parameterList(), node->type());
	}
	case Kind::FunctionExpression: {
		auto* d = node->as<FunctionExpression>();
		return f.updateFunctionExpression(
			d, modifierArray, d->AsteriskToken, node->name(),
			node->typeParameterList(), node->parameterList(), node->type(),
			d->FullSignature, node->body());
	}
	case Kind::ArrowFunction: {
		auto* d = node->as<ArrowFunction>();
		return f.updateArrowFunction(
			d, modifierArray, node->typeParameterList(),
			node->parameterList(), node->type(), d->FullSignature,
			d->EqualsGreaterThanToken, node->body());
	}
	case Kind::ClassExpression: {
		auto* d = node->as<ClassExpression>();
		return f.updateClassExpression(
			d, modifierArray, node->name(), node->typeParameterList(),
			d->HeritageClauses, node->memberList());
	}
	case Kind::VariableStatement: {
		auto* d = node->as<VariableStatement>();
		return f.updateVariableStatement(d, modifierArray,
		                                 d->DeclarationList);
	}
	case Kind::FunctionDeclaration: {
		auto* d = node->as<FunctionDeclaration>();
		return f.updateFunctionDeclaration(
			d, modifierArray, d->AsteriskToken, node->name(),
			node->typeParameterList(), node->parameterList(), node->type(),
			d->FullSignature, node->body());
	}
	case Kind::ClassDeclaration: {
		auto* d = node->as<ClassDeclaration>();
		return f.updateClassDeclaration(
			d, modifierArray, node->name(), node->typeParameterList(),
			d->HeritageClauses, node->memberList());
	}
	case Kind::InterfaceDeclaration: {
		auto* d = node->as<InterfaceDeclaration>();
		return f.updateInterfaceDeclaration(
			d, modifierArray, node->name(), node->typeParameterList(),
			d->HeritageClauses, node->memberList());
	}
	case Kind::TypeAliasDeclaration: {
		auto* d = node->as<TypeAliasDeclaration>();
		return f.updateTypeAliasDeclaration(
			d, modifierArray, node->name(), node->typeParameterList(),
			node->type());
	}
	case Kind::EnumDeclaration: {
		auto* d = node->as<EnumDeclaration>();
		return f.updateEnumDeclaration(d, modifierArray, node->name(),
		                               node->memberList());
	}
	case Kind::ModuleDeclaration: {
		auto* d = node->as<ModuleDeclaration>();
		return f.updateModuleDeclaration(
			d, modifierArray, d->Keyword, node->name(), node->attributes(),
			node->body());
	}
	case Kind::ImportEqualsDeclaration: {
		auto* d = node->as<ImportEqualsDeclaration>();
		return f.updateImportEqualsDeclaration(
			d, modifierArray, node->isTypeOnly(), node->name(),
			d->ModuleReference);
	}
	case Kind::ImportDeclaration: {
		auto* d = node->as<ImportDeclaration>();
		return f.updateImportDeclaration(
			d, modifierArray, node->importClause(), node->moduleSpecifier(),
			d->Attributes);
	}
	case Kind::ExportAssignment: {
		auto* d = node->as<ExportAssignment>();
		return f.updateExportAssignment(
			d, modifierArray, d->IsExportEquals, node->type(),
			node->expression());
	}
	case Kind::ExportDeclaration: {
		auto* d = node->as<ExportDeclaration>();
		return f.updateExportDeclaration(
			d, modifierArray, node->isTypeOnly(), d->ExportClause,
			node->moduleSpecifier(), node->attributes());
	}
	default:
		break;
	}
	TSC_UNREACHABLE("Node that does not have modifiers tried to have modifier "
	                "replaced");
}

// ast/utilities.go:3028 IsContextualKeyword — replica (not ported in ast).
bool isContextualKeyword(Kind token) {
	return KindFirstContextualKeyword <= token &&
	       token <= KindLastContextualKeyword;
}

// ast/utilities.go:4170 IsNonContextualKeyword — replica.
bool isNonContextualKeyword(Kind token) {
	return isKeyword(token) && !isContextualKeyword(token);
}

// transform.go:210 hasInternalAnnotation
bool hasInternalAnnotation(const CommentRange& commentRange,
                           SourceFile* sourceFile) {
	std::string_view text = sourceFile->text;
	std::string_view comment =
		text.substr(commentRange.pos_, commentRange.end_ - commentRange.pos_);
	return comment.find("@internal") != std::string_view::npos;
}

// transform.go:2973 extractExpandoHostParams
struct ExpandoHostParams {
	NodeList* typeParameters;
	NodeList* parameters;
	Node* asteriskToken;
};
ExpandoHostParams extractExpandoHostParams(Node* node) {
	switch (node->kind) {
	case Kind::FunctionExpression: {
		auto* fn = node->as<FunctionExpression>();
		return {fn->TypeParameters, fn->Parameters, fn->AsteriskToken};
	}
	case Kind::ArrowFunction: {
		auto* fn = node->as<ArrowFunction>();
		return {fn->TypeParameters, fn->Parameters, fn->AsteriskToken};
	}
	default: {
		auto* fn = node->as<FunctionDeclaration>();
		return {fn->TypeParameters, fn->Parameters, fn->AsteriskToken};
	}
	}
}

// transform.go:210 hasInternalAnnotation — collect leading comment ranges of a
// node (getLeadingCommentRangesOfNode, transform.go:203). iter.Seq -> vector.
std::vector<CommentRange> getLeadingCommentRangesOfNode(Node* node,
                                                        NodeFactory* factory,
                                                        SourceFile* sourceFile) {
	(void)factory; // signature kept parallel to Go (factory unused by scanner fn)
	std::vector<CommentRange> result;
	if (node == nullptr || node->kind == Kind::JsxText) {
		return result;
	}
	getLeadingCommentRanges(sourceFile->text, node->pos(),
	                        [&](const CommentRange& c) {
		                        result.push_back(c);
		                        return false;
	                        });
	return result;
}

// transform.go:21 ReferencedFilePair
struct ReferencedFilePair {
	SourceFile* file;
	FileReference* ref;
};

} // namespace

// transform.go:63 DeclarationTransformer (concrete impl)
struct DeclarationTransformerImpl : DeclarationTransformer {
	DeclarationEmitHost* host;
	const CompilerOptions* compilerOptions;
	SymbolTrackerImpl* tracker;
	SymbolTrackerSharedState* state;
	printer::EmitResolver* resolver;
	std::string declarationFilePath;
	std::string declarationMapPath;

	bool needsDeclare = false;
	bool needsScopeFixMarker = false;
	bool resultHasScopeMarker = false;
	Node* enclosingDeclaration = nullptr;
	bool resultHasExternalModuleIndicator = false;
	bool suppressNewDiagnosticContexts = false;
	collections::Set<std::string> witnessedCjsExports;
	std::unordered_map<NodeId, Node*> lateStatementReplacementMap;
	std::unordered_map<NodeId, Node*> expandoHosts;
	std::unordered_map<NodeId, std::vector<Node*>> expandoMembers;
	std::unordered_map<NodeId, std::vector<Node*>> deferredExpandoAssignments;
	std::unordered_set<thisPropertyAssignmentKey, thisPropertyAssignmentKeyHash>
		seenProperties;
	std::vector<Node*> thisPropertyAssignmentsCollected;
	std::vector<ReferencedFilePair> rawReferencedFiles;
	std::vector<FileReference*> rawTypeReferenceDirectives;
	std::vector<FileReference*> rawLibReferenceDirectives;
	NodeVisitor* bindingNameVisitor = nullptr;
	NodeVisitor* expressionVisitor = nullptr;
	NodeVisitor* cjsExportAssignmentVisitor = nullptr;
	NodeVisitor* exportStrippingVisitor = nullptr;
	NodeVisitor* thisPropertyVisitor = nullptr;

	Node* cjsExportAssignment = nullptr;
	std::vector<Node*> cjsExportMembers;
	Node* cjsExportAssignmentName =
		nullptr; // tracks the name node used for `export =` in CJS
	             // module.exports assignments
	NodeVisitor* declareStrippingVisitor = nullptr;
	bool inClassExpressionDeclaration =
		false; // true when serializing members of a class expression kept as a
	           // class declaration

	// Public surface (contract)
	SourceFile* TransformSourceFile(SourceFile* file) override {
		return transformers::Transformer::transformSourceFile(file);
	}
	std::vector<Diagnostic*> GetDiagnostics() override {
		return state->diagnostics;
	}

	// --- method declarations (definitions below, in Go order) ---
	bool shouldStripInternal(Node* node);
	bool isInternalDeclaration(Node* node, SourceFile* sourceFile);
	Node* visit(Node* node);
	Node* visitSourceFile(SourceFile* node);
	void collectFileReferences(SourceFile* sourceFile);
	NodeList* appendCjsExports(NodeList* combinedStatements);
	Node* transformSourceFile(SourceFile* node);
	NodeList* transformAndReplaceLatePaintedStatements(NodeList* statements);
	std::vector<FileReference*> getReferencedFiles(
		const std::string& outputFilePath);
	std::vector<FileReference*> getLibReferences();
	std::vector<FileReference*> getTypeReferences();
	std::pair<bool, std::function<void()>> setupDiagnosticContext(Node* input);
	Node* visitDeclarationSubtree(Node* input);
	void checkName(Node* node);
	Node* transformMappedTypeNode(MappedTypeNode* input);
	Node* transformHeritageClause(HeritageClause* clause);
	Node* transformImportTypeNode(ImportTypeNode* input);
	Node* transformConstructorTypeNode(ConstructorTypeNode* input);
	Node* transformFunctionTypeNode(FunctionTypeNode* input);
	Node* transformConditionalTypeNode(ConditionalTypeNode* input);
	Node* transformTypeReference(TypeReferenceNode* input);
	Node* transformExpressionWithTypeArguments(
		ExpressionWithTypeArguments* input);
	Node* transformTypeParameterDeclaration(TypeParameterDeclaration* input);
	Node* transformVariableDeclaration(VariableDeclaration* input);
	Node* transformCjsRequireVariableDeclaration(VariableDeclaration* input);
	Node* recreateBindingPattern(BindingPattern* input);
	Node* recreateBindingElement(BindingElement* e);
	Node* transformIndexSignatureDeclaration(IndexSignatureDeclaration* input);
	Node* transformCallSignatureDeclaration(CallSignatureDeclaration* input);
	Node* transformPropertySignatureDeclaration(
		PropertySignatureDeclaration* input);
	Node* transformPropertyDeclaration(PropertyDeclaration* input);
	Node* transformSetAccessorDeclaration(SetAccessorDeclaration* input);
	Node* transformGetAccesorDeclaration(GetAccessorDeclaration* input);
	NodeList* updateAccessorParamList(Node* input, bool isPrivate);
	Node* transformConstructorDeclaration(ConstructorDeclaration* input);
	Node* transformConstructSignatureDeclaration(
		ConstructSignatureDeclaration* input);
	Node* omitPrivateMethodType(Node* input);
	Node* transformMethodSignatureDeclaration(MethodSignatureDeclaration* input);
	Node* transformMethodDeclaration(MethodDeclaration* input);
	Node* visitDeclarationStatements(Node* input);
	Node* tryGetNameOfAssignedExpression(Node* unwrapped);
	Node* getNameOfExportedAssignedExpression(Node* unwrapped,
	                                          bool isExportEquals);
	Node* transformExportAssignment(Node* input, Node* assignment,
	                                Node* expression, bool isExportEquals);
	Node* transformFunctionLikeToDeclaration(Node* unwrapped, Node* funcName,
	                                         ModifierList* mods,
	                                         Node* fullSignatureType);
	Node* transformBinaryExpressionToExportDeclaration(Node* input, Node* name);
	Node* transformCommonJSExport(Node* input, Node* name);
	Node* transformCommonJSExportWorker(Node* input, Node* name);
	Node* wrapInCJSExportNamespace(Node* content);
	Node* transformClassExpressionToDeclaration(Node* classExpr, Node* className,
	                                            ModifierList* modifiers);
	Node* rewriteModuleSpecifier(Node* parent, Node* input);
	void preserveJsDoc(Node* updated, Node* original);
	void preservePartialJsDoc(Node* updated, Node* original);
	void removeAllComments(Node* node);
	Node* ensureType(Node* node, bool ignorePrivate);
	bool shouldPrintWithInitializer(Node* node);
	void checkEntityNameVisibility(Node* entityName,
	                               Node* enclosingDeclaration);
	Node* transformTopLevelDeclaration(Node* input);
	Node* transformTypeAliasDeclaration(TypeAliasDeclaration* input);
	Node* transformInterfaceDeclaration(InterfaceDeclaration* input);
	Node* transformFunctionDeclaration(FunctionDeclaration* input);
	Node* transformModuleDeclaration(ModuleDeclaration* input);
	Node* stripExportModifiers(Node* statement);
	NodeList* buildClassMembers(Node* classNode,
	                            std::vector<Node*> extraMembers = {});
	Node* transformClassDeclaration(ClassDeclaration* input);
	Node* visitThisPropertyAssignments(Node* node);
	std::vector<Node*> collectThisPropertyAssignments(Node* classNode);
	std::vector<Node*> walkBindingPattern(BindingPattern* pattern, Node* param);
	Node* transformVariableStatement(VariableStatement* input);
	Node* transformEnumDeclaration(EnumDeclaration* input);
	ModifierList* ensureModifiers(Node* node);
	ModifierFlags ensureModifierFlags(Node* node);
	NodeList* ensureTypeParams(Node* node, NodeList* params);
	NodeList* updateParamList(Node* node, NodeList* params);
	Node* ensureParameter(ParameterDeclaration* p);
	Node* ensureNoInitializer(Node* node);
	Node* visitBindingName(Node* node);
	Node* transformImportEqualsDeclaration(ImportEqualsDeclaration* decl);
	Node* transformImportDeclaration(ImportDeclaration* decl);
	Node* transformJSDocTypeExpression(JSDocTypeExpression* input);
	Node* transformJSDocTypeLiteral(JSDocTypeLiteral* input);
	Node* transformJSDocPropertyTag(JSDocParameterOrPropertyTag* input);
	Node* transformJSDocAllType(JSDocAllType* input);
	Node* transformJSDocNullableType(JSDocNullableType* input);
	Node* transformJSDocNonNullableType(JSDocNonNullableType* input);
	Node* transformJSDocVariadicType(JSDocVariadicType* input);
	Node* transformJSDocOptionalType(JSDocOptionalType* input);
	Node* getNameExpressionPreferringIdentifier(Node* nameExpr);
	Node* stripDeclareModifiers(Node* node);
	Node* visitCJSExportAssignments(Node* expression);
	Node* visitNestedExpression(Node* expression);
	void transformExpandoAssignment(BinaryExpression* node);
	void addExportModifierToExpandoMembers(NodeId hostId);
	NodeId getExpandoHostId(Node* declaration);
	void transformExpandoHost(Node* name, Node* declaration);
	Node* createFullExpandoBlock(NodeId id);
	std::string tryGetPropertyName(Node* node);
};

// transform.go:103 NewDeclarationTransformer
DeclarationTransformer* NewDeclarationTransformer(
	DeclarationEmitHost* host, printer::EmitResolver* resolver,
	const CompilerOptions* compilerOptions,
	std::string_view declarationFilePath,
	std::string_view declarationMapPath) {
	if (resolver == nullptr || resolver->EmitContext() == nullptr) {
		TSC_UNREACHABLE("DeclarationTransformer requires an EmitResolver with an EmitContext");
	}
	printer::EmitContext* context = resolver->EmitContext();
	auto* state = new SymbolTrackerSharedState();
	state->isolatedDeclarations =
		tristateIsTrue(compilerOptions->IsolatedDeclarations);
	state->stripInternal = tristateIsTrue(compilerOptions->StripInternal);
	state->resolver = resolver;
	auto* tracker = NewSymbolTracker(host, resolver, state);
	// TODO: Use new host GetOutputPathsFor method instead of passing in
	// entrypoint paths (which will also better support bundled emit)
	auto* tx = new DeclarationTransformerImpl();
	tx->host = host;
	tx->compilerOptions = compilerOptions;
	tx->tracker = tracker;
	tx->state = state;
	tx->resolver = resolver;
	tx->declarationFilePath = std::string(declarationFilePath);
	tx->declarationMapPath = std::string(declarationMapPath);
	tx->state->reportExpandoFunctionErrors = [tx, resolver](Node* node) {
		if (!tx->state->isolatedDeclarations) {
			return;
		}
		for (Symbol* p : resolver->GetPropertiesOfContainerFunction(node)) {
			if (isExpandoPropertyDeclaration(p->data->valueDeclaration)) {
				Node* errorTarget = p->data->valueDeclaration;
				if (isBinaryExpression(errorTarget)) {
					errorTarget =
						errorTarget->as<BinaryExpression>()->Left;
				}
				tx->state->addDiagnostic(createDiagnosticForNode(
					errorTarget,
					Assigning_properties_to_functions_without_declaring_them_is_not_supported_with_isolatedDeclarations_Add_an_explicit_declaration_for_the_properties_assigned_to_this_function));
			}
		}
	};
	tx->newTransformer([tx](Node* n) { return tx->visit(n); }, context);
	tx->bindingNameVisitor = tx->emitContext()->newNodeVisitor(
		[tx](Node* n) { return tx->visitBindingName(n); });
	tx->expressionVisitor = tx->emitContext()->newNodeVisitor(
		[tx](Node* n) { return tx->visitNestedExpression(n); });
	tx->exportStrippingVisitor = tx->emitContext()->newNodeVisitor(
		[tx](Node* n) { return tx->stripExportModifiers(n); });
	tx->thisPropertyVisitor = tx->emitContext()->newNodeVisitor(
		[tx](Node* n) { return tx->visitThisPropertyAssignments(n); });
	tx->cjsExportAssignmentVisitor = tx->emitContext()->newNodeVisitor(
		[tx](Node* n) { return tx->visitCJSExportAssignments(n); });
	tx->declareStrippingVisitor = tx->emitContext()->newNodeVisitor(
		[tx](Node* n) { return tx->stripDeclareModifiers(n); });
	return tx;
}

// transform.go:146 shouldStripInternal
bool DeclarationTransformerImpl::shouldStripInternal(Node* node) {
	return state->stripInternal && node != nullptr &&
	       isInternalDeclaration(node, state->currentSourceFile);
}

// transform.go:150 isInternalDeclaration
bool DeclarationTransformerImpl::isInternalDeclaration(
	Node* node, SourceFile* sourceFile) {
	if (node == nullptr) {
		return false;
	}
	Node* parseTreeNode = emitContext()->mostOriginal(node);
	if (!isParseTreeNode(parseTreeNode)) {
		return false;
	}
	if (parseTreeNode->kind == Kind::Parameter) {
		std::vector<Node*> params = parseTreeNode->parent->parameters();
		int paramIdx = -1;
		for (size_t i = 0; i < params.size(); i++) {
			if (params[i] == parseTreeNode) {
				paramIdx = static_cast<int>(i);
				break;
			}
		}
		Node* previousSibling = nullptr;
		if (paramIdx > 0) {
			previousSibling = params[paramIdx - 1];
		}

		std::string_view text = sourceFile->text;
		std::vector<CommentRange> commentRanges;

		if (previousSibling != nullptr) {
			// to handle
			// ... parameters, /** @internal */
			// public param: string
			int trailingPos =
				skipTriviaEx(text, previousSibling->end() + 1,
			                 SkipTriviaOptions{false /*stopAfterLineBreak*/,
			                                 true /*stopAtComments*/,
			                                 false /*inJSDoc*/});
			getTrailingCommentRanges(text, trailingPos,
			                         [&](const CommentRange& c) {
				                         commentRanges.push_back(c);
				                         return false;
			                         });
			getLeadingCommentRanges(text, node->pos(),
			                        [&](const CommentRange& c) {
				                        commentRanges.push_back(c);
				                        return false;
			                        });
		} else {
			int trailingPos =
				skipTriviaEx(text, node->pos(),
			                 SkipTriviaOptions{false /*stopAfterLineBreak*/,
			                                 true /*stopAtComments*/,
			                                 false /*inJSDoc*/});
			getTrailingCommentRanges(text, trailingPos,
			                         [&](const CommentRange& c) {
				                         commentRanges.push_back(c);
				                         return false;
			                         });
		}

		if (!commentRanges.empty()) {
			return hasInternalAnnotation(commentRanges.back(), sourceFile);
		}
		return false;
	}

	for (const CommentRange& commentRange :
	     getLeadingCommentRangesOfNode(parseTreeNode,
	                                   factory()->asNodeFactory(),
	                                   sourceFile)) {
		if (hasInternalAnnotation(commentRange, sourceFile)) {
			return true;
		}
	}
	return false;
}

// transform.go:215/223 nodebuilder flags
static constexpr nodebuilder::Flags declarationEmitNodeBuilderFlags =
	nodebuilder::FlagsMultilineObjectLiterals |
	nodebuilder::FlagsWriteClassExpressionAsTypeLiteral |
	nodebuilder::FlagsUseTypeOfFunction | nodebuilder::FlagsUseStructuralFallback |
	nodebuilder::FlagsAllowEmptyTuple |
	nodebuilder::FlagsGenerateNamesForShadowedTypeParams |
	nodebuilder::FlagsNoTruncation;

static constexpr nodebuilder::InternalFlags
	declarationEmitInternalNodeBuilderFlags =
		nodebuilder::InternalFlagsAllowUnresolvedNames;

// transform.go:226 visit — functions as both `visitDeclarationStatements` and
// `transformRoot`, utilitzing SyntaxList nodes
Node* DeclarationTransformerImpl::visit(Node* node) {
	if (node == nullptr) {
		return nullptr;
	}
	switch (node->kind) {
	case Kind::SourceFile:
		return visitSourceFile(node->as<SourceFile>());
	// statements we keep but do something to
	case Kind::FunctionDeclaration:
	case Kind::ModuleDeclaration:
	case Kind::ImportEqualsDeclaration:
	case Kind::InterfaceDeclaration:
	case Kind::ClassDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::EnumDeclaration:
	case Kind::VariableStatement:
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::ExportDeclaration:
	case Kind::ExportAssignment:
		return visitDeclarationStatements(node);
	// statements we elide
	case Kind::BreakStatement:
	case Kind::ContinueStatement:
	case Kind::DebuggerStatement:
	case Kind::DoStatement:
	case Kind::EmptyStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::ForStatement:
	case Kind::IfStatement:
	case Kind::LabeledStatement:
	case Kind::ReturnStatement:
	case Kind::SwitchStatement:
	case Kind::ThrowStatement:
	case Kind::TryStatement:
	case Kind::WhileStatement:
	case Kind::WithStatement:
	case Kind::NotEmittedStatement:
	case Kind::Block:
	case Kind::MissingDeclaration:
	case Kind::ExpressionStatement:
		return nullptr;
	// parts of things, things we just visit children of
	default:
		return visitDeclarationSubtree(node);
	}
}

// transform.go:280 visitSourceFile
Node* DeclarationTransformerImpl::visitSourceFile(SourceFile* node) {
	cjsExportAssignmentName = nullptr;
	if (node->IsDeclarationFile) {
		return node->asNode();
	}

	needsDeclare = true;
	needsScopeFixMarker = false;
	resultHasScopeMarker = false;
	enclosingDeclaration = node->asNode();
	state->getSymbolAccessibilityDiagnostic = throwDiagnostic;
	resultHasExternalModuleIndicator = false;
	suppressNewDiagnosticContexts = false;
	state->lateMarkedStatements.clear();
	lateStatementReplacementMap.clear();
	expandoHosts.clear();
	expandoMembers.clear();
	deferredExpandoAssignments.clear();
	rawReferencedFiles.clear();
	rawTypeReferenceDirectives.clear();
	rawLibReferenceDirectives.clear();
	witnessedCjsExports = collections::Set<std::string>{};
	state->currentSourceFile = node;
	collectFileReferences(node);
	resolver->PrecalculateDeclarationEmitVisibility(
		emitContext()->mostOriginal(node->asNode())->as<SourceFile>());
	Node* updated = transformSourceFile(node);
	state->currentSourceFile = nullptr;
	return updated;
}

// transform.go:310 collectFileReferences
void DeclarationTransformerImpl::collectFileReferences(SourceFile* sourceFile) {
	for (FileReference* ref : sourceFile->ReferencedFiles) {
		rawReferencedFiles.push_back(ReferencedFilePair{sourceFile, ref});
	}
	rawTypeReferenceDirectives.insert(rawTypeReferenceDirectives.end(),
	                                  sourceFile->TypeReferenceDirectives.begin(),
	                                  sourceFile->TypeReferenceDirectives.end());
	rawLibReferenceDirectives.insert(rawLibReferenceDirectives.end(),
	                                 sourceFile->LibReferenceDirectives.begin(),
	                                 sourceFile->LibReferenceDirectives.end());
}

// transform.go:327 appendCjsExports
NodeList* DeclarationTransformerImpl::appendCjsExports(
	NodeList* combinedStatements) {
	std::vector<Node*> result;
	if (cjsExportAssignment != nullptr) {
		result.push_back(cjsExportAssignment);
	}
	result.insert(result.end(), cjsExportMembers.begin(),
	              cjsExportMembers.end());
	result.insert(result.end(), combinedStatements->nodes.begin(),
	              combinedStatements->nodes.end());
	std::vector<Node*> statementNodes = flattenSyntaxLists(result);
	if (statementNodes.size() != combinedStatements->nodes.size()) {
		combinedStatements = factory()->newNodeList(statementNodes);
	}
	return combinedStatements;
}

// transform.go:341 transformSourceFile
Node* DeclarationTransformerImpl::transformSourceFile(SourceFile* node) {
	cjsExportAssignment = nullptr;
	cjsExportAssignmentName = nullptr;
	cjsExportMembers.clear();
	ScopeExit cleanup{[this] {
		cjsExportAssignment = nullptr;
		cjsExportAssignmentName = nullptr;
		cjsExportMembers.clear();
	}};
	cjsExportAssignmentVisitor->visitNode(
		node->asNode()); // collect nested module.exports= assignments
	expressionVisitor->visitNode(
		node->asNode()); // collect expando members (requires any export
	                     // assignment be located in advance)
	NodeList* combinedStatements = nullptr;
	NodeList* statements = visitor()->visitNodes(node->Statements);
	combinedStatements = transformAndReplaceLatePaintedStatements(statements);
	combinedStatements = appendCjsExports(combinedStatements);
	combinedStatements->loc = statements->loc; // setTextRange
	if (isExternalOrCommonJSModule(node)) {
		if (isInJSFile(node->asNode())) {
			auto it = node->Symbol->data->exports.find(InternalSymbolNameExportEquals);
			Symbol* exportEquals =
				it != node->Symbol->data->exports.end() ? it->second : nullptr;
			if (exportEquals != nullptr &&
			    exportEquals->data->declarations.size() > 1) {
				for (Node* decl : exportEquals->data->declarations) {
					state->addDiagnostic(createDiagnosticForNode(
						decl,
						Multiple_module_exports_assignments_cannot_be_serialized_for_declaration_emit));
				}
			}
		}
		if (!resultHasExternalModuleIndicator ||
		    (needsScopeFixMarker && !resultHasScopeMarker)) {
			Node* marker = createEmptyExports(factory()->asNodeFactory());
			std::vector<Node*> newList = combinedStatements->nodes;
			newList.push_back(marker);
			NodeList* withMarker = factory()->newNodeList(newList);
			withMarker->loc = combinedStatements->loc;
			combinedStatements = withMarker;
		}
	}
	std::string outputFilePath =
		tspath::getDirectoryPath(tspath::normalizeSlashes(declarationFilePath));
	Node* result = factory()->updateSourceFile(node, combinedStatements,
	                                           node->EndOfFileToken);
	result->as<SourceFile>()->LibReferenceDirectives = getLibReferences();
	result->as<SourceFile>()->TypeReferenceDirectives = getTypeReferences();
	result->as<SourceFile>()->IsDeclarationFile = true;
	result->as<SourceFile>()->ReferencedFiles =
		getReferencedFiles(outputFilePath);
	return result;
}

// transform.go:386 transformAndReplaceLatePaintedStatements
NodeList* DeclarationTransformerImpl::transformAndReplaceLatePaintedStatements(
	NodeList* statements) {
	// This is a `while` loop because `handleSymbolAccessibilityError` can see
	// additional import aliases marked as visible during error handling which
	// must now be included in the output and themselves checked for errors.
	// For example:
	// ```
	// module A {
	//   export module Q {}
	//   import B = Q;
	//   import C = B;
	//   export import D = C;
	// }
	// ```
	// In such a scenario, only Q and D are initially visible, but we don't
	// consider imports as private names - instead we say they if they are
	// referenced they must be recorded. So while checking D's visibility we mark
	// C as visible, then we must check C which in turn marks B, completing the
	// chain of dependent imports and allowing a valid declaration file output.
	// Today, this dependent alias marking only happens for internal import
	// aliases.
	while (true) {
		if (state->lateMarkedStatements.empty()) {
			break;
		}

		Node* next = state->lateMarkedStatements[0];
		state->lateMarkedStatements.erase(
			state->lateMarkedStatements.begin());

		bool saveNeedsDeclare = needsDeclare;
		needsDeclare = next->parent != nullptr && isSourceFile(next->parent);

		Node* result = transformTopLevelDeclaration(next);

		needsDeclare = saveNeedsDeclare;
		Node* original = emitContext()->mostOriginal(next);
		NodeId id = getNodeId(original);
		lateStatementReplacementMap[id] = result;
	}

	// And lastly, we need to get the final form of all those indetermine import
	// declarations from before and add them to the output list (and remove them
	// from the set to examine for outter declarations)
	std::vector<Node*> results;
	results.reserve(statements->nodes.size());
	for (Node* statement : statements->nodes) {
		if (!isLateVisibilityPaintedStatement(statement)) {
			results.push_back(statement);
			continue;
		}
		Node* original = emitContext()->mostOriginal(statement);
		NodeId id = getNodeId(original);
		auto it = lateStatementReplacementMap.find(id);
		if (it == lateStatementReplacementMap.end()) {
			results.push_back(statement);
			continue; // not replaced
		}
		Node* replacement = it->second;
		if (replacement == nullptr) {
			continue; // deleted
		}
		if (replacement->kind == Kind::SyntaxList) {
			if (!needsScopeFixMarker || !resultHasExternalModuleIndicator) {
				for (Node* elem :
				     replacement->as<SyntaxList>()->Children) {
					if (needsScopeMarker(elem)) {
						needsScopeFixMarker = true;
					}
					if (isSourceFile(statement->parent) &&
					    isExternalModuleIndicator(elem)) {
						resultHasExternalModuleIndicator = true;
					}
				}
			}
			for (Node* elem : replacement->as<SyntaxList>()->Children) {
				results.push_back(elem);
			}
		} else {
			if (needsScopeMarker(replacement)) {
				needsScopeFixMarker = true;
			}
			if (isSourceFile(statement->parent) &&
			    isExternalModuleIndicator(replacement)) {
				resultHasExternalModuleIndicator = true;
			}
			results.push_back(replacement);
		}
	}

	return factory()->newNodeList(results);
}

// transform.go:464 getReferencedFiles
std::vector<FileReference*>
DeclarationTransformerImpl::getReferencedFiles(
	const std::string& outputFilePath) {
	std::vector<FileReference*> results;
	// Handle path rewrites for triple slash ref comments
	for (const ReferencedFilePair& pair : rawReferencedFiles) {
		SourceFile* sourceFile = pair.file;
		FileReference* ref = pair.ref;

		if (!ref->Preserve) {
			continue;
		}

		SourceFile* file =
			host->GetSourceFileFromReference(sourceFile, ref);
		if (file == nullptr) {
			continue;
		}

		std::string declFileName;
		if (file->IsDeclarationFile) {
			declFileName = file->FileName();
		} else {
			OutputPaths* paths = host->GetOutputPathsFor(file, true);
			// Try to use output path for referenced file, or output js path if
			// that doesn't exist, or the input path if all else fails
			declFileName = paths->DeclarationFilePath();
			if (declFileName.empty()) {
				declFileName = paths->JsFilePath();
			}
			if (declFileName.empty()) {
				declFileName = file->FileName();
			}
		}
		// Should only be missing if the source file is missing a fileName (at
		// which point we can't name a reference to it anyway) TODO: Shouldn't
		// this be a crash or assert instead of a silent continue?
		if (declFileName.empty()) {
			continue;
		}

		// isAbsolutePathAnUrl=false (probably unsafe to assume this isn't
		// a URL, but that's what strada does) — identical to
		// getRelativePathFromDirectory in that case.
		std::string fileName = tspath::getRelativePathFromDirectory(
			outputFilePath, declFileName,
			tspath::ComparePathsOptions{
				host->UseCaseSensitiveFileNames(), host->GetCurrentDirectory()});

		auto* newRef = new FileReference();
		newRef->pos_ = -1;
		newRef->end_ = -1;
		newRef->FileName = fileName;
		newRef->ResolutionMode = ref->ResolutionMode;
		newRef->Preserve = ref->Preserve;
		results.push_back(newRef);
	}
	return results;
}

// transform.go:519 getLibReferences — clone retained references
std::vector<FileReference*> DeclarationTransformerImpl::getLibReferences() {
	std::vector<FileReference*> result;
	for (FileReference* ref : rawLibReferenceDirectives) {
		if (!ref->Preserve) {
			continue;
		}
		auto* newRef = new FileReference();
		newRef->pos_ = -1;
		newRef->end_ = -1;
		newRef->FileName = ref->FileName;
		newRef->ResolutionMode = ref->ResolutionMode;
		newRef->Preserve = ref->Preserve;
		result.push_back(newRef);
	}
	return result;
}

// transform.go:535 getTypeReferences — clone retained references
std::vector<FileReference*> DeclarationTransformerImpl::getTypeReferences() {
	std::vector<FileReference*> result;
	for (FileReference* ref : rawTypeReferenceDirectives) {
		if (!ref->Preserve) {
			continue;
		}
		auto* newRef = new FileReference();
		newRef->pos_ = -1;
		newRef->end_ = -1;
		newRef->FileName = ref->FileName;
		newRef->ResolutionMode = ref->ResolutionMode;
		newRef->Preserve = ref->Preserve;
		result.push_back(newRef);
	}
	return result;
}

// transform.go:551 setupDiagnosticContext
std::pair<bool, std::function<void()>>
DeclarationTransformerImpl::setupDiagnosticContext(Node* input) {
	bool canProduceDiagnostic = canProduceDiagnostics(input);
	bool oldWithinObjectLiteralType = suppressNewDiagnosticContexts;
	bool shouldEnterSuppressNewDiagnosticsContextContext =
		(input->kind == Kind::TypeLiteral ||
	     input->kind == Kind::MappedType) &&
		!(input->parent->kind == Kind::TypeAliasDeclaration ||
	      input->parent->kind == Kind::JSTypeAliasDeclaration);

	GetSymbolAccessibilityDiagnostic oldDiag =
		state->getSymbolAccessibilityDiagnostic;
	if (canProduceDiagnostic && !suppressNewDiagnosticContexts) {
		state->getSymbolAccessibilityDiagnostic =
			createGetSymbolAccessibilityDiagnosticForNode(input);
	}
	Node* oldName = state->errorNameNode;

	if (shouldEnterSuppressNewDiagnosticsContextContext) {
		suppressNewDiagnosticContexts = true;
	}

	return {canProduceDiagnostic, [this, oldDiag, oldName,
	                             oldWithinObjectLiteralType]() {
		        state->getSymbolAccessibilityDiagnostic = oldDiag;
		        state->errorNameNode = oldName;
		        suppressNewDiagnosticContexts = oldWithinObjectLiteralType;
	        }};
}

// transform.go:573 visitDeclarationSubtree
Node* DeclarationTransformerImpl::visitDeclarationSubtree(Node* input) {
	if (shouldStripInternal(input)) {
		return nullptr;
	}
	if (isDeclaration(input)) {
		if (isDeclarationAndNotVisible(emitContext(), resolver, input)) {
			return nullptr;
		}
		if (hasDynamicName(input)) {
			if (state->isolatedDeclarations) {
				// Classes and object literals usually elide properties with
				// computed names that are not of a literal type In isolated
				// declarations TSC needs to error on these as we don't know the
				// type in a DTE.
				if (!resolver->IsDefinitelyReferenceToGlobalSymbolObject(
				        input->name()->expression())) {
					if (isClassDeclaration(input->parent) ||
					    isObjectLiteralExpression(input->parent)) {
						state->addDiagnostic(createDiagnosticForNode(
							input,
							Computed_property_names_on_class_or_object_literals_cannot_be_inferred_with_isolatedDeclarations));
						return nullptr;
					} else if ((isInterfaceDeclaration(input->parent) ||
					            isTypeLiteralNode(input->parent)) &&
					           !isEntityNameExpression(
					               input->name()->expression())) {
						// Type declarations just need to double-check that the
						// input computed name is an entity name expression
						state->addDiagnostic(createDiagnosticForNode(
							input,
							Computed_properties_must_be_number_or_string_literals_variables_or_dotted_expressions_with_isolatedDeclarations));
						return nullptr;
					}
				}
			} else if (!resolver->IsLateBound(
			               emitContext()->parseNode(input)) ||
			           !isEntityNameExpression(input->name()->expression())) {
				return nullptr;
			}
		}
	}

	// Elide implementation signatures from overload sets
	if (isFunctionLike(input) && resolver->IsImplementationOfOverload(input)) {
		return nullptr;
	}

	if (input->kind == Kind::SemicolonClassElement) {
		return nullptr;
	}

	if (isHeritageClause(input) &&
	    (input->as<HeritageClause>()->Types->nodes.empty() ||
	     (input->as<HeritageClause>()->Types->nodes.size() == 1 &&
	      nodeIsMissing(input->as<HeritageClause>()->Types->nodes[0])))) {
		return nullptr;
	}

	Node* previousEnclosingDeclaration = enclosingDeclaration;
	if (isEnclosingDeclaration(input)) {
		enclosingDeclaration = input;
	}

	auto [canProduceDiagnostic, cleanupDiagnosticContext] =
		setupDiagnosticContext(input);
	ScopeExit cleanup{cleanupDiagnosticContext};

	Node* result = nullptr;

	switch (input->kind) {
	case Kind::MappedType:
		result = transformMappedTypeNode(input->as<MappedTypeNode>());
		break;
	case Kind::HeritageClause:
		result = transformHeritageClause(input->as<HeritageClause>());
		break;
	case Kind::MethodSignature:
		result = transformMethodSignatureDeclaration(
			input->as<MethodSignatureDeclaration>());
		break;
	case Kind::MethodDeclaration:
		result =
			transformMethodDeclaration(input->as<MethodDeclaration>());
		break;
	case Kind::ConstructSignature:
		result = transformConstructSignatureDeclaration(
			input->as<ConstructSignatureDeclaration>());
		break;
	case Kind::Constructor:
		result = transformConstructorDeclaration(
			input->as<ConstructorDeclaration>());
		break;
	case Kind::GetAccessor:
		result = transformGetAccesorDeclaration(
			input->as<GetAccessorDeclaration>());
		break;
	case Kind::SetAccessor:
		result = transformSetAccessorDeclaration(
			input->as<SetAccessorDeclaration>());
		break;
	case Kind::PropertyDeclaration:
		result = transformPropertyDeclaration(
			input->as<PropertyDeclaration>());
		break;
	case Kind::PropertySignature:
		result = transformPropertySignatureDeclaration(
			input->as<PropertySignatureDeclaration>());
		break;
	case Kind::CallSignature:
		result = transformCallSignatureDeclaration(
			input->as<CallSignatureDeclaration>());
		break;
	case Kind::IndexSignature:
		result = transformIndexSignatureDeclaration(
			input->as<IndexSignatureDeclaration>());
		break;
	case Kind::VariableDeclaration:
		result = transformVariableDeclaration(
			input->as<VariableDeclaration>());
		break;
	case Kind::TypeParameter:
		result = transformTypeParameterDeclaration(
			input->as<TypeParameterDeclaration>());
		break;
	case Kind::ExpressionWithTypeArguments:
		result = transformExpressionWithTypeArguments(
			input->as<ExpressionWithTypeArguments>());
		break;
	case Kind::TypeReference:
		result = transformTypeReference(input->as<TypeReferenceNode>());
		break;
	case Kind::ConditionalType:
		result = transformConditionalTypeNode(
			input->as<ConditionalTypeNode>());
		break;
	case Kind::FunctionType:
		result = transformFunctionTypeNode(input->as<FunctionTypeNode>());
		break;
	case Kind::ConstructorType:
		result = transformConstructorTypeNode(
			input->as<ConstructorTypeNode>());
		break;
	case Kind::ImportType:
		result = transformImportTypeNode(input->as<ImportTypeNode>());
		break;
	case Kind::TypeQuery:
		checkEntityNameVisibility(input->as<TypeQueryNode>()->ExprName,
		                          enclosingDeclaration);
		result = visitor()->visitEachChild(input);
		break;
	case Kind::QualifiedName:
		if (input->as<QualifiedName>()->Right->kind ==
		    Kind::PrivateIdentifier) {
			state->addDiagnostic(createDiagnosticForNode(
				input,
				Declaration_emit_elides_private_members_but_0_refers_to_a_private_member_Write_an_explicit_type_here,
				{input->as<QualifiedName>()->Right->text()}));
		}
		result = visitor()->visitEachChild(input);
		break;
	case Kind::TupleType:
		result = visitor()->visitEachChild(input);
		if (result != nullptr) {
			if (transformers::isOriginalNodeSingleLine(emitContext(),
			                                         input)) {
				emitContext()->addEmitFlags(result, printer::EFSingleLine);
			}
		}
		break;
	case Kind::JSDocTypeExpression:
		result = transformJSDocTypeExpression(
			input->as<JSDocTypeExpression>());
		break;
	case Kind::JSDocTypeLiteral:
		result =
			transformJSDocTypeLiteral(input->as<JSDocTypeLiteral>());
		break;
	case Kind::JSDocPropertyTag:
		result = transformJSDocPropertyTag(
			input->as<JSDocParameterOrPropertyTag>());
		break;
	case Kind::JSDocAllType:
		result = transformJSDocAllType(input->as<JSDocAllType>());
		break;
	case Kind::JSDocNullableType:
		result =
			transformJSDocNullableType(input->as<JSDocNullableType>());
		break;
	case Kind::JSDocNonNullableType:
		result = transformJSDocNonNullableType(
			input->as<JSDocNonNullableType>());
		break;
	case Kind::JSDocOptionalType:
		result =
			transformJSDocOptionalType(input->as<JSDocOptionalType>());
		break;
	case Kind::JSDocVariadicType:
		result = transformJSDocVariadicType(
			input->as<JSDocVariadicType>());
		break;
	default:
		result = visitor()->visitEachChild(input);
		break;
	}

	if (result != nullptr && canProduceDiagnostic && hasDynamicName(input)) {
		checkName(input);
	}

	enclosingDeclaration = previousEnclosingDeclaration;
	return result;
}

// transform.go:708 checkName
void DeclarationTransformerImpl::checkName(Node* node) {
	GetSymbolAccessibilityDiagnostic oldDiag =
		state->getSymbolAccessibilityDiagnostic;
	if (!suppressNewDiagnosticContexts) {
		state->getSymbolAccessibilityDiagnostic =
			createGetSymbolAccessibilityDiagnosticForNodeName(node);
	}
	state->errorNameNode = node->name();
	// debug.Assert(ast.HasDynamicName(node)) — should only be called with
	// dynamic names
	Node* entityName = node->name()->expression();
	checkEntityNameVisibility(entityName, enclosingDeclaration);
	if (!suppressNewDiagnosticContexts) {
		state->getSymbolAccessibilityDiagnostic = oldDiag;
	}
	state->errorNameNode = nullptr;
}

// transform.go:723 transformMappedTypeNode
Node* DeclarationTransformerImpl::transformMappedTypeNode(
	MappedTypeNode* input) {
	// handle missing template type nodes, since the printer does not
	Node* typeNode = nullptr;
	if (input->Type == nullptr) {
		typeNode = factory()->newKeywordTypeNode(Kind::AnyKeyword);
	} else {
		typeNode = visitor()->visitNode(input->Type);
	}
	return factory()->updateMappedTypeNode(input, input->ReadonlyToken,
	                                     visitor()->visitNode(
	                                         input->TypeParameter),
	                                     visitor()->visitNode(input->NameType),
	                                     input->QuestionToken, typeNode,
	                                     nullptr);
}

// transform.go:742 transformHeritageClause
Node* DeclarationTransformerImpl::transformHeritageClause(
	HeritageClause* clause) {
	std::vector<Node*> retainedClauses;
	for (Node* t : clause->Types->nodes) {
		Node* name = getHeritageClauseElementName(t);
		if (isEntityName(name) || isEntityNameExpression(name) ||
		    (clause->Token == Kind::ExtendsKeyword &&
		     isExpressionWithTypeArguments(t) &&
		     t->expression()->kind == Kind::NullKeyword)) {
			retainedClauses.push_back(t);
		}
	}
	if (retainedClauses.empty()) {
		return nullptr; // elide empty clause
	}
	if (retainedClauses.size() == clause->Types->nodes.size()) {
		return visitor()->visitEachChild(clause->asNode());
	}
	return factory()->updateHeritageClause(
		clause, clause->Token,
		visitor()->visitNodes(factory()->newNodeList(retainedClauses)));
}

// transform.go:761 transformImportTypeNode
Node* DeclarationTransformerImpl::transformImportTypeNode(
	ImportTypeNode* input) {
	if (!isLiteralImportTypeNode(input->asNode())) {
		return input->asNode();
	}
	return factory()->updateImportTypeNode(
		input, input->IsTypeOf,
		factory()->updateLiteralTypeNode(
			input->Argument->as<LiteralTypeNode>(),
			rewriteModuleSpecifier(
				input->asNode(),
				input->Argument->as<LiteralTypeNode>()->Literal)),
		input->Attributes, input->Qualifier,
		visitor()->visitNodes(input->TypeArguments));
}

// transform.go:778 transformConstructorTypeNode
Node* DeclarationTransformerImpl::transformConstructorTypeNode(
	ConstructorTypeNode* input) {
	return factory()->updateConstructorTypeNode(
		input, ensureModifiers(input->asNode()),
		visitor()->visitNodes(input->TypeParameters),
		updateParamList(input->asNode(), input->Parameters),
		visitor()->visitNode(input->Type));
}

// transform.go:788 transformFunctionTypeNode
Node* DeclarationTransformerImpl::transformFunctionTypeNode(
	FunctionTypeNode* input) {
	return factory()->updateFunctionTypeNode(
		input, visitor()->visitNodes(input->TypeParameters),
		updateParamList(input->asNode(), input->Parameters),
		visitor()->visitNode(input->Type));
}

// transform.go:797 transformConditionalTypeNode
Node* DeclarationTransformerImpl::transformConditionalTypeNode(
	ConditionalTypeNode* input) {
	Node* checkType = visitor()->visitNode(input->CheckType);
	Node* extendsType = visitor()->visitNode(input->ExtendsType);
	Node* oldEnclosingDecl = enclosingDeclaration;
	enclosingDeclaration = input->TrueType;
	Node* trueType = visitor()->visitNode(input->TrueType);
	enclosingDeclaration = oldEnclosingDecl;
	Node* falseType = visitor()->visitNode(input->FalseType);

	return factory()->updateConditionalTypeNode(input, checkType, extendsType,
	                                            trueType, falseType);
}

// transform.go:815 transformTypeReference
Node* DeclarationTransformerImpl::transformTypeReference(
	TypeReferenceNode* input) {
	checkEntityNameVisibility(input->TypeName, enclosingDeclaration);
	return visitor()->visitEachChild(input->asNode());
}

// transform.go:820 transformExpressionWithTypeArguments
Node* DeclarationTransformerImpl::transformExpressionWithTypeArguments(
	ExpressionWithTypeArguments* input) {
	if (isEntityName(input->Expression) ||
	    isEntityNameExpression(input->Expression)) {
		checkEntityNameVisibility(input->Expression, enclosingDeclaration);
	}
	return visitor()->visitEachChild(input->asNode());
}

// transform.go:827 transformTypeParameterDeclaration
Node* DeclarationTransformerImpl::transformTypeParameterDeclaration(
	TypeParameterDeclaration* input) {
	if (isPrivateMethodTypeParameter(resolver, input) &&
	    (input->DefaultType != nullptr || input->Constraint != nullptr)) {
		return factory()->updateTypeParameterDeclaration(
			input, input->modifiers, input->name, nullptr,
			input->Expression, nullptr);
	}
	return visitor()->visitEachChild(input->asNode());
}

// transform.go:841 transformVariableDeclaration
Node* DeclarationTransformerImpl::transformVariableDeclaration(
	VariableDeclaration* input) {
	if (state->currentSourceFile->CommonJSModuleIndicator != nullptr &&
	    isVariableDeclarationInitializedToRequire(input->asNode())) {
		return transformCjsRequireVariableDeclaration(input);
	}
	if (isBindingPattern(input->name) &&
	    hasAnyBindingInitializers(input->name->as<BindingPattern>())) {
		return recreateBindingPattern(input->name->as<BindingPattern>());
	}
	// Variable declaration types also suppress new diagnostic contexts,
	// provided the contexts wouldn't be made for binding pattern types
	suppressNewDiagnosticContexts = true;
	return factory()->updateVariableDeclaration(
		input, bindingNameVisitor->visitNode(input->name), nullptr,
		ensureType(input->asNode(), false),
		ensureNoInitializer(input->asNode()));
}

// transform.go:875 transformCjsRequireVariableDeclaration
Node* DeclarationTransformerImpl::transformCjsRequireVariableDeclaration(
	VariableDeclaration* input) {
	Node* specifier = rewriteModuleSpecifier(
		input->asNode(),
		input->Initializer->as<CallExpression>()->Arguments->nodes[0]);
	if (isIdentifier(input->name)) {
		// `const x = require("something")` -> `import x =
		// require("something")`
		return factory()->newImportEqualsDeclaration(
			nullptr, false, input->name,
			factory()->newExternalModuleReference(specifier));
	} else if (isArrayBindingPattern(input->name)) {
		// TODO: Is this actually reachable? should we error on this?
		return nullptr;
	} else { // object binding pattern
		// `const {x, y: z} = require("something")` -> `import {x, y as z}
		// from "something"`
		BindingPattern* b = input->name->as<BindingPattern>();
		std::vector<Node*> importSpecifiers;
		for (Node* elem : b->Elements->nodes) {
			if (!isIdentifier(elem->name())) {
				continue; // nested destructuring, bail
			}
			importSpecifiers.push_back(factory()->newImportSpecifier(
				false, elem->propertyName(), elem->name()));
		}
		return factory()->newImportDeclaration(
			nullptr,
			factory()->newImportClause(
				Kind::Unknown, nullptr,
				factory()->newNamedImports(
					factory()->newNodeList(importSpecifiers))),
			specifier, nullptr);
	}
}

// transform.go:907 recreateBindingPattern
Node* DeclarationTransformerImpl::recreateBindingPattern(
	BindingPattern* input) {
	std::vector<Node*> results;
	for (Node* elem : input->Elements->nodes) {
		Node* result = recreateBindingElement(elem->as<BindingElement>());
		if (result == nullptr) {
			continue;
		}
		if (result->kind == Kind::SyntaxList) {
			for (Node* c : result->as<SyntaxList>()->Children) {
				results.push_back(c);
			}
		} else {
			results.push_back(result);
		}
	}
	if (results.empty()) {
		return nullptr;
	}
	if (results.size() == 1) {
		return results[0];
	}
	return factory()->newSyntaxList(results);
}

// transform.go:929 recreateBindingElement
Node* DeclarationTransformerImpl::recreateBindingElement(BindingElement* e) {
	if (e->name == nullptr) {
		return nullptr;
	}
	if (!getBindingNameVisible(resolver, e->asNode())) {
		return nullptr;
	}
	if (isBindingPattern(e->name)) {
		return recreateBindingPattern(e->name->as<BindingPattern>());
	}
	return factory()->newVariableDeclaration(
		e->name, nullptr, ensureType(e->asNode(), false),
		nullptr // TODO: possible strada bug - not emitting const initialized
		        // binding pattern elements?
	);
}

// transform.go:947 transformIndexSignatureDeclaration
Node* DeclarationTransformerImpl::transformIndexSignatureDeclaration(
	IndexSignatureDeclaration* input) {
	Node* t = visitor()->visitNode(input->Type);
	if (t == nullptr) {
		t = factory()->newKeywordTypeNode(Kind::AnyKeyword);
	}
	return factory()->updateIndexSignatureDeclaration(
		input, ensureModifiers(input->asNode()),
		updateParamList(input->asNode(), input->Parameters), t);
}

// transform.go:960 transformCallSignatureDeclaration
Node* DeclarationTransformerImpl::transformCallSignatureDeclaration(
	CallSignatureDeclaration* input) {
	return factory()->updateCallSignatureDeclaration(
		input, ensureTypeParams(input->asNode(), input->TypeParameters),
		updateParamList(input->asNode(), input->Parameters),
		ensureType(input->asNode(), false));
}

// transform.go:969 transformPropertySignatureDeclaration
Node* DeclarationTransformerImpl::transformPropertySignatureDeclaration(
	PropertySignatureDeclaration* input) {
	if (isPrivateIdentifier(input->name)) {
		return nullptr;
	}
	Node* result = factory()->updatePropertySignatureDeclaration(
		input, ensureModifiers(input->asNode()), input->name,
		input->PostfixToken, ensureType(input->asNode(), false),
		// TODO: possible strada bug (fixed here) - const property
		// signatures never initialized
		ensureNoInitializer(input->asNode()));
	preservePartialJsDoc(result, input->asNode());
	return result;
}

// transform.go:985 transformPropertyDeclaration
Node* DeclarationTransformerImpl::transformPropertyDeclaration(
	PropertyDeclaration* input) {
	if (isPrivateIdentifier(input->name)) {
		return nullptr;
	}
	// Remove definite assignment assertion (!) from declaration files
	Node* postfixToken = input->PostfixToken;
	if (postfixToken != nullptr &&
	    postfixToken->kind == Kind::ExclamationToken) {
		postfixToken = nullptr;
	}
	return factory()->updatePropertyDeclaration(
		input, ensureModifiers(input->asNode()), input->name, postfixToken,
		ensureType(input->asNode(), false),
		ensureNoInitializer(input->asNode()));
}

// transform.go:1004 transformSetAccessorDeclaration
Node* DeclarationTransformerImpl::transformSetAccessorDeclaration(
	SetAccessorDeclaration* input) {
	if (isPrivateIdentifier(input->name)) {
		return nullptr;
	}

	return factory()->updateSetAccessorDeclaration(
		input, ensureModifiers(input->asNode()), input->name,
		nullptr, // accessors shouldn't have type params
		updateAccessorParamList(
			input->asNode(),
			resolver->GetEffectiveDeclarationFlags(
				emitContext()->parseNode(input->asNode()),
				ModifierFlagsPrivate) != 0),
		nullptr, nullptr, nullptr);
}

// transform.go:1021 transformGetAccesorDeclaration
Node* DeclarationTransformerImpl::transformGetAccesorDeclaration(
	GetAccessorDeclaration* input) {
	if (isPrivateIdentifier(input->name)) {
		return nullptr;
	}
	return factory()->updateGetAccessorDeclaration(
		input, ensureModifiers(input->asNode()), input->name,
		nullptr, // accessors shouldn't have type params
		updateAccessorParamList(
			input->asNode(),
			resolver->GetEffectiveDeclarationFlags(
				emitContext()->parseNode(input->asNode()),
				ModifierFlagsPrivate) != 0),
		ensureType(input->asNode(), false), nullptr, nullptr);
}

// transform.go:1037 updateAccessorParamList
NodeList* DeclarationTransformerImpl::updateAccessorParamList(Node* input,
                                                              bool isPrivate) {
	std::vector<Node*> newParams;
	if (!isPrivate) {
		Node* thisParam = getThisParameter(input);
		if (thisParam != nullptr) {
			newParams.push_back(
				ensureParameter(thisParam->as<ParameterDeclaration>()));
		}
	}
	if (isSetAccessorDeclaration(input)) {
		Node* valueParam = nullptr;
		SetAccessorDeclaration* sa = input->as<SetAccessorDeclaration>();
		if (!isPrivate) {
			if (newParams.size() == 1 && sa->Parameters->nodes.size() >= 2) {
				valueParam = ensureParameter(
					sa->Parameters->nodes[1]
						->as<ParameterDeclaration>());
			} else if (newParams.empty() &&
			           sa->Parameters->nodes.size() >= 1) {
				valueParam = ensureParameter(
					sa->Parameters->nodes[0]
						->as<ParameterDeclaration>());
			}
		}
		if (valueParam == nullptr) {
			// When synthesizing a missing value parameter, emit `value: any`
			// for non-private accessors to match TypeScript's declaration
			// emit behavior.
			Node* t = nullptr;
			if (!isPrivate) {
				t = factory()->newKeywordTypeNode(Kind::AnyKeyword);
			}
			valueParam = factory()->newParameterDeclaration(
				nullptr, nullptr, factory()->newIdentifier("value"),
				nullptr, t, nullptr);
		}
		newParams.push_back(valueParam);
	}
	return factory()->newNodeList(newParams);
}

// transform.go:1074 transformConstructorDeclaration
Node* DeclarationTransformerImpl::transformConstructorDeclaration(
	ConstructorDeclaration* input) {
	// A constructor declaration may not have a type annotation
	return factory()->updateConstructorDeclaration(
		input, ensureModifiers(input->asNode()),
		nullptr, // no type params
		updateParamList(input->asNode(), input->Parameters),
		nullptr, // no return type
		nullptr, nullptr);
}

// transform.go:1087 transformConstructSignatureDeclaration
Node* DeclarationTransformerImpl::transformConstructSignatureDeclaration(
	ConstructSignatureDeclaration* input) {
	return factory()->updateConstructSignatureDeclaration(
		input, ensureTypeParams(input->asNode(), input->TypeParameters),
		updateParamList(input->asNode(), input->Parameters),
		ensureType(input->asNode(), false));
}

// transform.go:1096 omitPrivateMethodType
Node* DeclarationTransformerImpl::omitPrivateMethodType(Node* input) {
	if (input->symbol() != nullptr && !input->symbol()->data->declarations.empty() &&
	    input->symbol()->data->declarations[0] != input) {
		return nullptr;
	}
	Node* result = nullptr;
	if (isMethodSignatureDeclaration(input)) {
		result = factory()->newPropertySignatureDeclaration(
			ensureModifiers(input), input->name(), nullptr /*postfixToken*/,
			nullptr /*typeNode*/, nullptr /*initializer*/);
	} else {
		result = factory()->newPropertyDeclaration(
			ensureModifiers(input), input->name(), nullptr /*postfixToken*/,
			nullptr /*typeNode*/, nullptr /*initializer*/);
	}
	preserveJsDoc(result, input);
	return result;
}

// transform.go:1122 transformMethodSignatureDeclaration
Node* DeclarationTransformerImpl::transformMethodSignatureDeclaration(
	MethodSignatureDeclaration* input) {
	if (resolver->GetEffectiveDeclarationFlags(
	        emitContext()->parseNode(input->asNode()),
	        ModifierFlagsPrivate) != 0) {
		return omitPrivateMethodType(input->asNode());
	} else if (isPrivateIdentifier(input->name)) {
		return nullptr;
	} else {
		return factory()->updateMethodSignatureDeclaration(
			input, ensureModifiers(input->asNode()), input->name,
			input->PostfixToken,
			ensureTypeParams(input->asNode(), input->TypeParameters),
			updateParamList(input->asNode(), input->Parameters),
			ensureType(input->asNode(), false));
	}
}

// transform.go:1140 transformMethodDeclaration
Node* DeclarationTransformerImpl::transformMethodDeclaration(
	MethodDeclaration* input) {
	if (resolver->GetEffectiveDeclarationFlags(
	        emitContext()->parseNode(input->asNode()),
	        ModifierFlagsPrivate) != 0) {
		return omitPrivateMethodType(input->asNode());
	} else if (isPrivateIdentifier(input->name)) {
		return nullptr;
	} else {
		return factory()->updateMethodDeclaration(
			input, ensureModifiers(input->asNode()), nullptr,
			input->name, input->PostfixToken,
			ensureTypeParams(input->asNode(), input->TypeParameters),
			updateParamList(input->asNode(), input->Parameters),
			ensureType(input->asNode(), false), nullptr, nullptr);
	}
}

// transform.go:1161 visitDeclarationStatements
Node* DeclarationTransformerImpl::visitDeclarationStatements(Node* input) {
	if (shouldStripInternal(input)) {
		return nullptr;
	}
	switch (input->kind) {
	case Kind::ExportDeclaration:
		if (isSourceFile(input->parent)) {
			resultHasExternalModuleIndicator = true;
		}
		resultHasScopeMarker = true;
		// Rewrite external module names if necessary
		return factory()->updateExportDeclaration(
			input->as<ExportDeclaration>(), input->modifiers(),
			input->isTypeOnly(),
			input->as<ExportDeclaration>()->ExportClause,
			rewriteModuleSpecifier(input, input->moduleSpecifier()),
			input->as<ExportDeclaration>()->Attributes);
	case Kind::ExportAssignment:
		return transformExportAssignment(
			input, input, input->expression(),
			input->as<ExportAssignment>()->IsExportEquals);
	default: {
		NodeId id = getNodeId(emitContext()->mostOriginal(input));
		if (lateStatementReplacementMap[id] == nullptr) {
			// Don't actually transform yet; just leave as original node - will
			// be elided/swapped by late pass
			lateStatementReplacementMap[id] =
				transformTopLevelDeclaration(input);
		}
		return input;
	}
	}
}

// transform.go:1192 tryGetNameOfAssignedExpression
Node* DeclarationTransformerImpl::tryGetNameOfAssignedExpression(
	Node* unwrapped) {
	Node* nameNode = nullptr;
	std::string nameText;
	if (!isPropertyAccessExpression(unwrapped) &&
	    unwrapped->name() != nullptr) {
		nameText = unwrapped->name()->text();
	} else if (isIdentifier(unwrapped)) {
		nameText = unwrapped->text();
	}
	if (!nameText.empty() && nameText != "default") {
		if (resolver->IsNameResolvable(enclosingDeclaration, nameText)) {
			// create a unique name that shares the same text as its' base
			nameNode = factory()->newUniqueName(
				nameText,
				printer::AutoGenerateOptions{
					printer::GeneratedIdentifierFlagsOptimistic, "", ""});
		} else {
			// use the node's name as-is, since it's not otherwise in-scope
			nameNode = factory()->newIdentifier(nameText);
		}
	}
	return nameNode;
}

// transform.go:1212 getNameOfExportedAssignedExpression
Node* DeclarationTransformerImpl::getNameOfExportedAssignedExpression(
	Node* unwrapped, bool isExportEquals) {
	Node* nameNode = tryGetNameOfAssignedExpression(unwrapped);
	if (nameNode == nullptr) {
		// fallback to a default name
		if (isExportEquals && isSourceFileJS(state->currentSourceFile)) {
			// only JS files prefer to use `_exports` for export assignments -
			// TS has always used `_default` for both `export=` and `export
			// default`
			nameNode = factory()->newUniqueName(
				"_exports",
				printer::AutoGenerateOptions{
					printer::GeneratedIdentifierFlagsOptimistic, "", ""});
		} else {
			nameNode = factory()->newUniqueName(
				"_default",
				printer::AutoGenerateOptions{
					printer::GeneratedIdentifierFlagsOptimistic, "", ""});
		}
	}
	cjsExportAssignmentName = nameNode;
	return nameNode;
}

// transform.go:1227 transformExportAssignment
Node* DeclarationTransformerImpl::transformExportAssignment(
	Node* input, Node* assignment, Node* expression, bool isExportEquals) {
	if (isSourceFile(input->parent)) {
		resultHasExternalModuleIndicator = true;
	}
	resultHasScopeMarker = true;
	if (isIdentifier(expression) &&
	    (isSourceFile(input->parent) || isModuleBlock(input->parent))) {
		Node* exportAssignment =
			factory()->newExportAssignment(nullptr, isExportEquals, nullptr,
			                           expression);
		emitContext()->assignSourceMapRange(exportAssignment, input);
		preserveJsDoc(exportAssignment, input);
		return exportAssignment;
	}

	state->getSymbolAccessibilityDiagnostic =
		[input](printer::SymbolAccessibilityResult&)
		-> SymbolAccessibilityDiagnostic* {
		auto* d = new SymbolAccessibilityDiagnostic();
		d->diagnosticMessage =
			Default_export_of_the_module_has_or_is_using_private_name_0;
		d->errorNode = input;
		return d;
	};
	tracker->PushErrorFallbackNode(assignment);

	// Check if the expression is a class expression - emit as a class
	// declaration + export assignment
	Node* unwrapped =
		skipOuterExpressions(expression, OEKExpressionTypePassthrough);
	Node* newId = getNameOfExportedAssignedExpression(unwrapped,
	                                                  isExportEquals);
	if (isClassExpression(unwrapped)) {
		std::vector<Node*> mods;
		if (needsDeclare) {
			mods.push_back(factory()->newToken(Kind::DeclareKeyword));
		}
		Node* classDecl = transformClassExpressionToDeclaration(
			unwrapped, newId, factory()->newModifierList(mods));
		tracker->PopErrorFallbackNode();
		preserveJsDoc(classDecl, input);
		// Reuse the same name node for the export so unique names resolve
		// consistently
		Node* exportAssignment = factory()->newExportAssignment(
			nullptr, isExportEquals, nullptr, newId);
		emitContext()->assignSourceMapRange(exportAssignment, input);
		removeAllComments(exportAssignment);
		return factory()->newSyntaxList({exportAssignment, classDecl});
	} else if (isFunctionLike(unwrapped)) {
		// Promote function or arrow function expressions to a function
		// declaration
		std::vector<Node*> mods;
		if (needsDeclare) {
			mods.push_back(factory()->newToken(Kind::DeclareKeyword));
		}
		Node* fullSignatureType = assignment->type();
		Node* funcDecl = transformFunctionLikeToDeclaration(
			unwrapped, newId, factory()->newModifierList(mods),
			fullSignatureType);
		tracker->PopErrorFallbackNode();
		preserveJsDoc(funcDecl, input);
		// Reuse the same name node for the export so unique names resolve
		// consistently
		Node* exportAssignment = factory()->newExportAssignment(
			nullptr, isExportEquals, nullptr, newId);
		emitContext()->assignSourceMapRange(exportAssignment, input);
		removeAllComments(exportAssignment);
		return factory()->newSyntaxList({exportAssignment, funcDecl});
	}

	// expression is non-identifier, create _default typed variable to
	// reference
	cjsExportAssignmentName = newId;
	Node* type_ = nullptr;
	Node* initializer = nullptr;
	if (isPrimitiveLiteralValue(unwrapParenthesizedExpression(expression),
	                          true)) {
		initializer = resolver->CreateLiteralConstValue(emitContext()->parseNode(assignment), tracker);
	}
	if (initializer == nullptr) {
		type_ = ensureType(assignment, false);
	}
	Node* varDecl =
		factory()->newVariableDeclaration(newId, nullptr, type_, initializer);
	tracker->PopErrorFallbackNode();
	ModifierList* modList = nullptr;
	if (needsDeclare) {
		modList = factory()->newModifierList(
			{factory()->newToken(Kind::DeclareKeyword)});
	} else {
		modList = factory()->newModifierList({});
	}
	Node* statement = factory()->newVariableStatement(
		modList,
		factory()->newVariableDeclarationList(
			factory()->newNodeList({varDecl}), NodeFlagsConst));
	Node* exportAssignment = factory()->newExportAssignment(
		nullptr, isExportEquals, nullptr, newId);
	emitContext()->assignSourceMapRange(exportAssignment, input);
	// Remove comments from the export declaration and copy them onto the
	// synthetic _default declaration
	preserveJsDoc(statement, input);
	return factory()->newSyntaxList({statement, exportAssignment});
}

// transform.go:1298 transformFunctionLikeToDeclaration
Node* DeclarationTransformerImpl::transformFunctionLikeToDeclaration(
	Node* unwrapped, Node* funcName, ModifierList* mods,
	Node* fullSignatureType) {
	FunctionLikeDataRef d = unwrapped->functionLikeData();
	Node* sig = *d.fullSignature;
	if (sig == nullptr) {
		sig = fullSignatureType;
	}
	if (sig == nullptr) {
		return factory()->newFunctionDeclaration(
			mods, nullptr, funcName,
			ensureTypeParams(unwrapped, *d.typeParameters),
			updateParamList(unwrapped, *d.parameters),
			ensureType(unwrapped, false), visitor()->visitNode(sig), nullptr);
	} else {
		// If a full signature type node is present, emit as a variable
		// statement to reuse it
		return factory()->newVariableStatement(
			mods,
			factory()->newVariableDeclarationList(
				factory()->newNodeList(
					{factory()->newVariableDeclaration(
						funcName, nullptr, visitor()->visitNode(sig),
						nullptr)}),
				NodeFlagsConst));
	}
}

// transform.go:1324 transformBinaryExpressionToExportDeclaration
Node* DeclarationTransformerImpl::transformBinaryExpressionToExportDeclaration(
	Node* input, Node* name) {
	Node* propertyName = input->as<BinaryExpression>()->Right;

	// track alias target so referenced declarations are included in the output
	printer::SymbolAccessibilityResult visibilityResult =
		resolver->IsEntityNameVisible(propertyName, enclosingDeclaration);
	tracker->handleSymbolAccessibilityError(visibilityResult);

	if (isIdentifier(name) && propertyName->text() == name->text()) {
		propertyName = nullptr;
	}

	return factory()->newExportDeclaration(
		nullptr, false,
		factory()->newNamedExports(factory()->newNodeList(
			{factory()->newExportSpecifier(false, propertyName, name)})),
		nullptr, nullptr);
}

// transform.go:1343 transformCommonJSExport
Node* DeclarationTransformerImpl::transformCommonJSExport(Node* input,
                                                        Node* name) {
	Node* res = transformCommonJSExportWorker(input, name);
	if (res == nullptr) {
		return res;
	}
	return wrapInCJSExportNamespace(res);
}

// transform.go:1351 transformCommonJSExportWorker
Node* DeclarationTransformerImpl::transformCommonJSExportWorker(
	Node* input, Node* name) {
	std::string nameText;
	if (isIdentifier(name) || isStringLiteral(name)) {
		nameText = name->text();
	}
	if (witnessedCjsExports.Has(nameText) && !nameText.empty()) {
		return nullptr; // Already emitted this export name
	}
	witnessedCjsExports.Add(nameText);
	resultHasExternalModuleIndicator = true;
	resultHasScopeMarker = true;
	// only transform cjs exports to shorthand at the top-level of a source
	// file, otherwise we uniformly emit nested exports with a type annotation
	if (isCommonJSAliasExport(input) && isExpressionStatement(input->parent) &&
	    isSourceFile(input->parent->parent)) {
		// export { name }
		// export { source as name }
		return transformBinaryExpressionToExportDeclaration(input, name);
	}

	// Check if the RHS is a class expression - emit as a class declaration
	// instead of a typed variable
	if (isBinaryExpression(input)) {
		if (Node* rhs = unwrapParenthesizedExpression(
		        input->as<BinaryExpression>()->Right);
		    isClassExpression(rhs)) {
			ClassExpression* ce = rhs->as<ClassExpression>();
			Node* classExprName = ce->name;
			bool hasExprName = classExprName != nullptr &&
			                   !classExprName->text().empty();

			if (hasExprName) {
				// Set up TrackSymbol watch to detect if the class expression's
				// own symbol is referenced during member type serialization.
				tracker->watchedClassSymbol = rhs->symbol();
				tracker->classSymbolTracked = false;
				ScopeExit watchCleanup{[this] {
					tracker->watchedClassSymbol = nullptr;
					tracker->classSymbolTracked = false;
				}};

				// Serialize class members using the class expression name,
				// which triggers TrackSymbol for any self-referential member
				// types.
				Node* className =
					factory()->newIdentifier(classExprName->text());
				std::vector<Node*> classMods{
					factory()->newToken(Kind::ExportKeyword)};
				Node* classDecl = transformClassExpressionToDeclaration(
					rhs, className, factory()->newModifierList(classMods));
				preserveJsDoc(classDecl, input);

				// Determine if namespace isolation is needed:
				// - The class expression name differs from the export name, OR
				// - The class's own symbol was used in a member's serialized
				// type
				bool namesDiffer =
					!isIdentifier(name) ||
					classExprName->text() != name->text();
				bool needsIsolation =
					namesDiffer || tracker->classSymbolTracked;

				if (needsIsolation) {
					Node* nsName = factory()->newUniqueName(
						"_ns",
						printer::AutoGenerateOptions{
							printer::GeneratedIdentifierFlagsOptimistic,
							"", ""});
					std::vector<Node*> nsMods;
					if (needsDeclare) {
						nsMods.push_back(
							factory()->newToken(Kind::DeclareKeyword));
					}
					Node* nsDecl = factory()->newModuleDeclaration(
						factory()->newModifierList(nsMods),
						Kind::NamespaceKeyword, nsName, nullptr,
						factory()->newModuleBlock(
							factory()->newNodeList({classDecl})));

					std::string aliasBase = "_exported";
					std::string nameTxt = name->text();
					if (isIdentifier(name) &&
					    isIdentifierText("_" + nameTxt,
					                     LanguageVariant::Standard)) {
						aliasBase = "_" + nameTxt;
					}
					Node* importAlias = factory()->newUniqueName(
						aliasBase,
						printer::AutoGenerateOptions{
							printer::GeneratedIdentifierFlagsOptimistic,
							"", ""});
					Node* qualifiedName =
						factory()->newQualifiedName(nsName, className);
					Node* importDecl = factory()->newImportEqualsDeclaration(
						nullptr, false, importAlias, qualifiedName);

					Node* exportSpecifier = factory()->newExportSpecifier(
						false, importAlias, name);
					Node* exportDecl = factory()->newExportDeclaration(
						nullptr, false,
						factory()->newNamedExports(factory()->newNodeList(
							{exportSpecifier})),
						nullptr, nullptr);
					removeAllComments(exportDecl);

					return factory()->newSyntaxList(
						{nsDecl, importDecl, exportDecl});
				}

				// No isolation needed: names match and no self-references.
				// Update modifiers to include declare if needed.
				std::vector<Node*> mods;
				mods.push_back(factory()->newToken(Kind::ExportKeyword));
				if (needsDeclare) {
					mods.push_back(
						factory()->newToken(Kind::DeclareKeyword));
				}
				classDecl = factory()->updateClassDeclaration(
					classDecl->as<ClassDeclaration>(),
					factory()->newModifierList(mods),
					classDecl->as<ClassDeclaration>()->name,
					classDecl->as<ClassDeclaration>()->TypeParameters,
					classDecl->as<ClassDeclaration>()->HeritageClauses,
					classDecl->as<ClassDeclaration>()->Members);
				return classDecl;
			}
			std::vector<Node*> mods;
			mods.push_back(factory()->newToken(Kind::ExportKeyword));
			if (needsDeclare) {
				mods.push_back(factory()->newToken(Kind::DeclareKeyword));
			}
			Node* className = name;
			if (!isIdentifier(className)) {
				className = factory()->newUniqueName(
					"_class",
					printer::AutoGenerateOptions{
						printer::GeneratedIdentifierFlagsOptimistic, "",
						""});
			}
			Node* classDecl = transformClassExpressionToDeclaration(
				rhs, className, factory()->newModifierList(mods));
			preserveJsDoc(classDecl, input);
			if (!isIdentifier(name)) {
				// Non-identifier name: emit class declaration + named export
				Node* exportDecl = factory()->newExportDeclaration(
					nullptr, false,
					factory()->newNamedExports(factory()->newNodeList(
						{factory()->newExportSpecifier(false, className,
						                           name)})),
					nullptr, nullptr);
				removeAllComments(exportDecl);
				return factory()->newSyntaxList({classDecl, exportDecl});
			}
			return classDecl;
		}
	}

	if (isIdentifier(name)) {
		if (name->text() == "default") {
			// const _default: Type; export default _default;
			Node* newId = factory()->newUniqueName(
				"_default",
				printer::AutoGenerateOptions{
					printer::GeneratedIdentifierFlagsOptimistic, "", ""});
			state->getSymbolAccessibilityDiagnostic =
				[input](printer::SymbolAccessibilityResult&)
				-> SymbolAccessibilityDiagnostic* {
				auto* d = new SymbolAccessibilityDiagnostic();
				d->diagnosticMessage =
					Default_export_of_the_module_has_or_is_using_private_name_0;
				d->errorNode = input;
				return d;
			};
			tracker->PushErrorFallbackNode(input);
			Node* type_ = ensureType(input, false);
			Node* varDecl = factory()->newVariableDeclaration(newId, nullptr,
			                                                  type_, nullptr);
			tracker->PopErrorFallbackNode();
			ModifierList* modList = nullptr;
			if (needsDeclare) {
				modList = factory()->newModifierList(
					{factory()->newToken(Kind::DeclareKeyword)});
			} else {
				modList = factory()->newModifierList({});
			}
			Node* statement = factory()->newVariableStatement(
				modList,
				factory()->newVariableDeclarationList(
					factory()->newNodeList({varDecl}), NodeFlagsConst));

			Node* assignment = factory()->newExportAssignment(
				input->modifiers(), false, nullptr, newId);
			// Remove comments from the export declaration and copy them onto
			// the synthetic _default declaration
			preserveJsDoc(statement, input);
			removeAllComments(assignment);
			return factory()->newSyntaxList({statement, assignment});
		} else if (resolver->GetReferencedValueDeclaration(
		               name) == input ||
		           resolver->GetReferencedValueDeclaration(
		               name) == nullptr) {
			// only inline to a export var if the `name` lookup points at this
			// assignment or nothing - if it points at something else, we must
			// use a temp name
			// export var name: Type
			tracker->PushErrorFallbackNode(input);
			Node* type_ = ensureType(input, false);
			Node* varDecl = factory()->newVariableDeclaration(name, nullptr,
			                                                  type_, nullptr);
			tracker->PopErrorFallbackNode();
			ModifierList* modList = nullptr;
			if (needsDeclare) {
				modList = factory()->newModifierList(
					{factory()->newToken(Kind::ExportKeyword),
					 factory()->newToken(Kind::DeclareKeyword)});
			} else {
				modList = factory()->newModifierList(
					{factory()->newToken(Kind::ExportKeyword)});
			}
			return factory()->newVariableStatement(
				modList,
				factory()->newVariableDeclarationList(
					factory()->newNodeList({varDecl}), NodeFlagsNone));
		}
	}
	// const _exported: Type; export {_exported as "name"};
	Node* newId = factory()->newUniqueName(
		"_exported",
		printer::AutoGenerateOptions{
			printer::GeneratedIdentifierFlagsOptimistic, "", ""});
	state->getSymbolAccessibilityDiagnostic =
		[input](printer::SymbolAccessibilityResult&)
		-> SymbolAccessibilityDiagnostic* {
		auto* d = new SymbolAccessibilityDiagnostic();
		d->diagnosticMessage =
			Default_export_of_the_module_has_or_is_using_private_name_0;
		d->errorNode = input;
		return d;
	};
	tracker->PushErrorFallbackNode(input);
	Node* type_ = ensureType(input, false);
	Node* varDecl =
		factory()->newVariableDeclaration(newId, nullptr, type_, nullptr);
	tracker->PopErrorFallbackNode();
	ModifierList* modList = nullptr;
	if (needsDeclare) {
		modList = factory()->newModifierList(
			{factory()->newToken(Kind::DeclareKeyword)});
	} else {
		modList = factory()->newModifierList({});
	}
	Node* statement = factory()->newVariableStatement(
		modList,
		factory()->newVariableDeclarationList(
			factory()->newNodeList({varDecl}), NodeFlagsConst));

	Node* assignment = factory()->newExportDeclaration(
		nullptr, false,
		factory()->newNamedExports(factory()->newNodeList(
			{factory()->newExportSpecifier(false, newId, name)})),
		nullptr, nullptr);
	// Remove comments from the export declaration and copy them onto the
	// synthetic _default declaration
	preserveJsDoc(statement, input);
	removeAllComments(assignment);
	return factory()->newSyntaxList({statement, assignment});
}

// transform.go:1536 wrapInCJSExportNamespace
Node* DeclarationTransformerImpl::wrapInCJSExportNamespace(Node* content) {
	if (cjsExportAssignmentName == nullptr) {
		return content;
	}
	// Reuse the same name node so unique names resolve consistently with the
	// class/export
	Node* nsName = cjsExportAssignmentName;
	std::vector<Node*> members;
	if (content->kind == Kind::SyntaxList) {
		members = content->as<SyntaxList>()->Children;
	} else {
		members = {content};
	}
	std::vector<Node*> nsMods;
	if (needsDeclare) {
		nsMods.push_back(factory()->newToken(Kind::DeclareKeyword));
	}
	auto visited = declareStrippingVisitor->visitSlice(members);
	members = visited.first;
	return factory()->newModuleDeclaration(
		factory()->newModifierList(nsMods), Kind::NamespaceKeyword, nsName,
		nullptr,
		factory()->newModuleBlock(factory()->newNodeList(members)));
}

// transform.go:1574 transformClassExpressionToDeclaration — converts a class
// expression into a class declaration for use in CJS export declarations (e.g.,
// exports.K = class K {} or module.exports = class Thing {}). This delegates to
// the shared buildClassMembers helper to stay in sync with
// transformClassDeclaration.
Node* DeclarationTransformerImpl::transformClassExpressionToDeclaration(
	Node* classExpr, Node* className, ModifierList* modifiers) {
	Node* previousEnclosingDeclaration = enclosingDeclaration;
	enclosingDeclaration = classExpr;
	bool previousInClassExpressionDeclaration = inClassExpressionDeclaration;
	inClassExpressionDeclaration = true;
	ScopeExit restore{[this, previousEnclosingDeclaration,
	                   previousInClassExpressionDeclaration] {
		enclosingDeclaration = previousEnclosingDeclaration;
		inClassExpressionDeclaration = previousInClassExpressionDeclaration;
	}};

	std::vector<Node*> extraMembers;
	if (isInJSFile(classExpr)) {
		extraMembers = collectThisPropertyAssignments(classExpr);
	}
	NodeList* members = buildClassMembers(classExpr, extraMembers);
	NodeList* typeParameters = ensureTypeParams(
		classExpr, classExpr->as<ClassExpression>()->TypeParameters);
	NodeList* heritageClauses = visitor()->visitNodes(
		classExpr->as<ClassExpression>()->HeritageClauses);

	return factory()->newClassDeclaration(modifiers, className, typeParameters,
	                                      heritageClauses, members);
}

// transform.go:1601 rewriteModuleSpecifier
Node* DeclarationTransformerImpl::rewriteModuleSpecifier(Node* parent,
                                                         Node* input) {
	if (input == nullptr) {
		return nullptr;
	}
	resultHasExternalModuleIndicator =
		resultHasExternalModuleIndicator ||
		(parent->kind != Kind::ModuleDeclaration &&
	     parent->kind != Kind::ImportType);
	return input;
}

// transform.go:1609 preserveJsDoc — copy comment range from original to
// updated node so JSDoc comments are preserved
void DeclarationTransformerImpl::preserveJsDoc(Node* updated,
                                               Node* original) {
	emitContext()->assignCommentRange(updated, original);
}

// transform.go:1614 preservePartialJsDoc
void DeclarationTransformerImpl::preservePartialJsDoc(Node* updated,
                                                      Node* original) {
	if ((original->flags & NodeFlagsReparsed) == 0) {
		return;
	}
	std::vector<Node*> docs =
		original->eagerJSDoc(getSourceFileOfNode(original));
	Node* jsdoc = docs.empty() ? nullptr : docs.front();
	if (jsdoc == nullptr) {
		return;
	}
	std::string description =
		getTextOfJSDocComment(jsdoc->as<JSDoc>()->Comment);
	if (description.empty()) {
		return;
	}
	// strings.ReplaceAll(description, "\n", "\n * ")
	std::string replaced;
	size_t pos = 0;
	while (true) {
		size_t idx = description.find('\n', pos);
		if (idx == std::string::npos) {
			replaced += description.substr(pos);
			break;
		}
		replaced += description.substr(pos, idx - pos);
		replaced += "\n * ";
		pos = idx + 1;
	}
	std::string comment = "*\n * " + replaced + "\n ";
	emitContext()->addSyntheticLeadingComment(
		updated, Kind::MultiLineCommentTrivia, comment,
		true /*hasTrailingNewLine*/);
}

// transform.go:1630 removeAllComments
void DeclarationTransformerImpl::removeAllComments(Node* node) {
	emitContext()->addEmitFlags(node, printer::EFNoComments);
	// !!! TODO: Also remove synthetic trailing/leading comments added by
	// transforms
	// emitNode.leadingComments = undefined;
	// emitNode.trailingComments = undefined;
}

// transform.go:1637 ensureType
Node* DeclarationTransformerImpl::ensureType(Node* node, bool ignorePrivate) {
	if (!ignorePrivate &&
	    resolver->GetEffectiveDeclarationFlags(emitContext()->parseNode(node),
	                                       ModifierFlagsPrivate) != 0) {
		// Private nodes emit no types (except private parameter properties,
		// whose parameter types are actually visible)
		return nullptr;
	}

	if (shouldPrintWithInitializer(node)) {
		// Literal const declarations will have an initializer ensured rather
		// than a type
		return nullptr;
	}

	// Should be removed createTypeOfDeclaration will actually now reuse the
	// existing annotation so there is no real need to duplicate type walking
	// Left in for now to minimize diff during syntactic type node builder
	// refactor
	if (!isExportAssignment(node) && !isBindingElement(node) &&
	    node->type() != nullptr &&
	    (!isParameterDeclaration(node) ||
	     !resolver->RequiresAddingImplicitUndefined(node, nullptr,
	                                                enclosingDeclaration))) {
		if (isSourceFileJS(state->currentSourceFile)) {
			// JS types have a heap of constructs we can't directly emit into
			// .d.ts files; the node builder contains logic to remap those where
			// possible, so we invoke it here In strada we always built js
			// declarations symbolically, so all js type nodes went through this
			// postprocessing
			nodebuilder::Flags jsFlags = declarationEmitNodeBuilderFlags;
			if (inClassExpressionDeclaration) {
				jsFlags &=
					~nodebuilder::FlagsWriteClassExpressionAsTypeLiteral;
			}
			Node* res = resolver->TryJSTypeNodeToTypeNode(node->type(), enclosingDeclaration, jsFlags,
				declarationEmitInternalNodeBuilderFlags, tracker);
			if (res != nullptr) {
				return res;
			}
			// otherwise, fall back to full serialization
		} else {
			return visitor()->visitNode(node->type());
		}
	}

	Node* oldErrorNameNode = state->errorNameNode;
	state->errorNameNode = node->name();
	GetSymbolAccessibilityDiagnostic oldDiag;
	if (!suppressNewDiagnosticContexts) {
		oldDiag = state->getSymbolAccessibilityDiagnostic;
		if (canProduceDiagnostics(node)) {
			state->getSymbolAccessibilityDiagnostic =
				createGetSymbolAccessibilityDiagnosticForNode(node);
		}
	}
	Node* typeNode = nullptr;

	nodebuilder::Flags flags = declarationEmitNodeBuilderFlags;
	if (inClassExpressionDeclaration) {
		flags &= ~nodebuilder::FlagsWriteClassExpressionAsTypeLiteral;
	}
	if (hasInferredType(node)) {
		typeNode = resolver->CreateTypeOfDeclaration(node, enclosingDeclaration, flags,
			declarationEmitInternalNodeBuilderFlags, tracker);
	} else if (isFunctionLike(node)) {
		typeNode = resolver->CreateReturnTypeOfSignatureDeclaration(node, enclosingDeclaration, flags,
			declarationEmitInternalNodeBuilderFlags, tracker);
	} else {
		TSC_UNREACHABLE("Unexpected node kind in ensureType");
	}

	state->errorNameNode = oldErrorNameNode;
	if (!suppressNewDiagnosticContexts) {
		state->getSymbolAccessibilityDiagnostic = oldDiag;
	}
	if (typeNode == nullptr) {
		return factory()->newKeywordTypeNode(Kind::AnyKeyword);
	}
	return typeNode;
}

// transform.go:1701 shouldPrintWithInitializer
bool DeclarationTransformerImpl::shouldPrintWithInitializer(Node* node) {
	return canHaveLiteralInitializer(resolver, node) &&
	       node->initializer() != nullptr &&
	       resolver->IsLiteralConstDeclaration(
	           emitContext()->mostOriginal(node));
}

// transform.go:1705 checkEntityNameVisibility
void DeclarationTransformerImpl::checkEntityNameVisibility(
	Node* entityName, Node* enclosingDecl) {
	printer::SymbolAccessibilityResult visibilityResult =
		resolver->IsEntityNameVisible(entityName, enclosingDecl);
	tracker->handleSymbolAccessibilityError(visibilityResult);
}

// transform.go:1711 transformTopLevelDeclaration — transforms the direct
// child of a source file into zero or more replacement statements
Node* DeclarationTransformerImpl::transformTopLevelDeclaration(Node* input) {
	if (!state->lateMarkedStatements.empty()) {
		// Remove duplicates of the current statement from the deferred work
		// queue (this was done via orderedRemoveItem in strada - why? to
		// ensure the same backing array? microop?)
		state->lateMarkedStatements.erase(
			std::remove(state->lateMarkedStatements.begin(),
			            state->lateMarkedStatements.end(), input),
			state->lateMarkedStatements.end());
	}
	if (shouldStripInternal(input)) {
		return nullptr;
	}
	if (input->kind == Kind::ImportEqualsDeclaration) {
		return transformImportEqualsDeclaration(
			input->as<ImportEqualsDeclaration>());
	}
	if (input->kind == Kind::ImportDeclaration ||
	    input->kind == Kind::JSImportDeclaration) {
		Node* res =
			transformImportDeclaration(input->as<ImportDeclaration>());
		if (res != nullptr && res->kind != Kind::ImportDeclaration) {
			Node* cloned = res->clone(*factory()->asNodeFactory());
			cloned->kind = Kind::ImportDeclaration;
			return cloned;
		}
		return res;
	}
	if (isDeclaration(input) &&
	    isDeclarationAndNotVisible(emitContext(), resolver, input)) {
		return nullptr;
	}

	// !!! TODO: JSDoc support
	// if (isJSDocImportTag(input)) return;

	// Elide implementation signatures from overload sets
	if (isFunctionLike(input) && resolver->IsImplementationOfOverload(input)) {
		return nullptr;
	}
	Node* original = emitContext()->mostOriginal(input);
	NodeId id = getNodeId(original);
	bool isExpandoHost = expandoHosts.count(id) != 0;
	bool hasDeferredExpandoAssignments =
		deferredExpandoAssignments.count(id) != 0;
	if (isExpandoHost || hasDeferredExpandoAssignments) {
		return createFullExpandoBlock(id);
	}

	Node* previousEnclosingDeclaration = enclosingDeclaration;
	if (isEnclosingDeclaration(input)) {
		enclosingDeclaration = input;
	}

	bool canProduceDiagnostic = canProduceDiagnostics(input);
	GetSymbolAccessibilityDiagnostic oldDiag =
		state->getSymbolAccessibilityDiagnostic;
	Node* oldName = state->errorNameNode;
	if (canProduceDiagnostic) {
		state->getSymbolAccessibilityDiagnostic =
			createGetSymbolAccessibilityDiagnosticForNode(input);
	}
	bool saveNeedsDeclare = needsDeclare;

	Node* result = nullptr;
	switch (input->kind) {
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
		result = transformTypeAliasDeclaration(
			input->as<TypeAliasDeclaration>());
		break;
	case Kind::InterfaceDeclaration:
		result = transformInterfaceDeclaration(
			input->as<InterfaceDeclaration>());
		break;
	case Kind::FunctionDeclaration:
		result = transformFunctionDeclaration(
			input->as<FunctionDeclaration>());
		break;
	case Kind::ModuleDeclaration:
		result =
			transformModuleDeclaration(input->as<ModuleDeclaration>());
		break;
	case Kind::ClassDeclaration:
		result =
			transformClassDeclaration(input->as<ClassDeclaration>());
		break;
	case Kind::VariableStatement:
		result = transformVariableStatement(
			input->as<VariableStatement>());
		break;
	case Kind::EnumDeclaration:
		result = transformEnumDeclaration(input->as<EnumDeclaration>());
		break;
	default:
		// Anything left unhandled is an error, so this should be unreachable
		std::string msg = "Unhandled top-level node in declaration emit: " +
		                  std::string(kindToString(input->kind));
		TSC_UNREACHABLE(msg.c_str());
	}

	enclosingDeclaration = previousEnclosingDeclaration;
	state->getSymbolAccessibilityDiagnostic = oldDiag;
	needsDeclare = saveNeedsDeclare;
	state->errorNameNode = oldName;
	return result;
}

// transform.go:1791 transformTypeAliasDeclaration
Node* DeclarationTransformerImpl::transformTypeAliasDeclaration(
	TypeAliasDeclaration* input) {
	needsDeclare = false;
	return factory()->updateTypeAliasDeclaration(
		input, ensureModifiers(input->asNode()), input->name,
		visitor()->visitNodes(input->TypeParameters),
		visitor()->visitNode(input->Type));
}

// transform.go:1802 transformInterfaceDeclaration
Node* DeclarationTransformerImpl::transformInterfaceDeclaration(
	InterfaceDeclaration* input) {
	return factory()->updateInterfaceDeclaration(
		input, ensureModifiers(input->asNode()), input->name,
		visitor()->visitNodes(input->TypeParameters),
		visitor()->visitNodes(input->HeritageClauses),
		visitor()->visitNodes(input->Members));
}

// transform.go:1813 transformFunctionDeclaration
Node* DeclarationTransformerImpl::transformFunctionDeclaration(
	FunctionDeclaration* input) {
	if (resolver->IsExpandoFunctionDeclaration(input->asNode())) {
		state->reportExpandoFunctionErrors(input->asNode());
	}
	return factory()->updateFunctionDeclaration(
		input, ensureModifiers(input->asNode()), nullptr, input->name,
		ensureTypeParams(input->asNode(), input->TypeParameters),
		updateParamList(input->asNode(), input->Parameters),
		ensureType(input->asNode(), false), nullptr /*fullSignature*/,
		nullptr);
}

// transform.go:1830 transformModuleDeclaration
Node* DeclarationTransformerImpl::transformModuleDeclaration(
	ModuleDeclaration* input) {
	// !!! TODO: module declarations are now parsed into nested module objects
	// with export modifiers It'd be good to collapse those back in the
	// declaration output, but the AST can't represent the `namespace a.b.c`
	// shape for the printer (without using invalid identifier names).
	ModifierList* mods = ensureModifiers(input->asNode());
	bool saveNeedsDeclare = needsDeclare;
	needsDeclare = false;
	Node* inner = input->Body;
	Kind keyword = input->Keyword;
	if (keyword != Kind::GlobalKeyword &&
	    (input->name == nullptr || !isStringLiteral(input->name))) {
		keyword = Kind::NamespaceKeyword;
	}
	Node* attributes = visitor()->visitNode(input->Attributes);

	if (inner != nullptr && inner->kind == Kind::ModuleBlock) {
		bool oldNeedsScopeFix = needsScopeFixMarker;
		bool oldHasScopeFix = resultHasScopeMarker;
		resultHasScopeMarker = false;
		needsScopeFixMarker = false;
		NodeList* statements =
			visitor()->visitNodes(inner->statementList());
		NodeList* lateStatements =
			transformAndReplaceLatePaintedStatements(statements);
		if ((input->flags & NodeFlagsAmbient) != 0) {
			needsScopeFixMarker = false; // If it was `declare`'d everything is
			                             // implicitly exported already, ignore
			                             // late printed "privates"
		}
		// With the final list of statements, there are 3 possibilities:
		// 1. There's an export assignment or export declaration in the
		// namespace - do nothing 2. Everything is exported and there are no
		// export assignments or export declarations - strip all export
		// modifiers 3. Some things are exported, some are not, and there's no
		// marker - add an empty marker
		if (!isGlobalScopeAugmentation(input->asNode()) &&
		    !resultHasScopeMarker && !hasScopeMarker(lateStatements)) {
			if (needsScopeFixMarker) {
				std::vector<Node*> withMarker = lateStatements->nodes;
				withMarker.push_back(
					createEmptyExports(factory()->asNodeFactory()));
				lateStatements = factory()->newNodeList(withMarker);
			} else {
				lateStatements =
					exportStrippingVisitor->visitNodes(lateStatements);
			}
		}

		Node* body = factory()->updateModuleBlock(inner->as<ModuleBlock>(),
		                                          lateStatements);
		needsDeclare = saveNeedsDeclare;
		needsScopeFixMarker = oldNeedsScopeFix;
		resultHasScopeMarker = oldHasScopeFix;

		return factory()->updateModuleDeclaration(input, mods, keyword,
		                                          input->name, attributes,
		                                          body);
	}
	if (inner != nullptr) {
		// trigger visit. ignore result (is deferred, so is just inner unless
		// elided)
		visitor()->visitNode(inner);
		// eagerly transform nested namespaces (the nesting doesn't need any
		// elision or painting done)
		Node* original = emitContext()->mostOriginal(inner);
		NodeId id = getNodeId(original);
		Node* body = lateStatementReplacementMap.count(id)
		                 ? lateStatementReplacementMap[id]
		                 : nullptr;
		lateStatementReplacementMap.erase(id);
		return factory()->updateModuleDeclaration(input, mods, keyword,
		                                          input->name, attributes,
		                                          body);
	}
	return factory()->updateModuleDeclaration(input, mods, keyword,
	                                          input->name, attributes,
	                                          nullptr);
}

// transform.go:1907 stripExportModifiers
Node* DeclarationTransformerImpl::stripExportModifiers(Node* statement) {
	if (statement == nullptr) {
		return nullptr;
	}
	Node* parseNode = emitContext()->parseNode(statement);
	if (isImportEqualsDeclaration(statement) ||
	    (parseNode != nullptr &&
	     resolver->GetEffectiveDeclarationFlags(parseNode,
	                                        ModifierFlagsDefault) != 0) ||
	    !canHaveModifiers(statement)) {
		// `export import` statements should remain as-is, as imports are _not_
		// implicitly exported in an ambient namespace Likewise, `export
		// default` classes and the like and just be `default`, so we preserve
		// their `export` modifiers, too
		return statement;
	}

	ModifierFlags oldFlags = getCombinedModifierFlags(statement);
	if ((oldFlags & ModifierFlagsExport) == 0) {
		return statement;
	}
	ModifierFlags newFlags =
		oldFlags & (ModifierFlagsAll ^ ModifierFlagsExport);
	std::vector<Node*> modifiers = createModifiersFromModifierFlags(
		newFlags, newModifierFromFactory, *factory()->asNodeFactory());
	return replaceModifiers(*factory()->asNodeFactory(), statement,
	                        factory()->newModifierList(modifiers));
}

// transform.go:1930 buildClassMembers — builds the member list for a
// class-like node (ClassDeclaration or ClassExpression). It handles parameter
// properties, private identifiers, late-bound index signatures, and visited
// members. Extra members (e.g., this-property assignments from JS files) can
// be passed via extraMembers.
NodeList* DeclarationTransformerImpl::buildClassMembers(
	Node* classNode, std::vector<Node*> extraMembers) {
	Node* ctor = getFirstConstructorWithBody(classNode);
	std::vector<Node*> parameterProperties;
	if (ctor != nullptr) {
		GetSymbolAccessibilityDiagnostic oldDiag =
			state->getSymbolAccessibilityDiagnostic;
		for (Node* param :
		     ctor->as<ConstructorDeclaration>()->Parameters->nodes) {
			if (!hasSyntacticModifier(
			        param, ModifierFlagsParameterPropertyModifier) ||
			    shouldStripInternal(param)) {
				continue;
			}
			state->getSymbolAccessibilityDiagnostic =
				createGetSymbolAccessibilityDiagnosticForNode(param);
			if (param->name()->kind == Kind::Identifier) {
				Node* updated = factory()->newPropertyDeclaration(
					ensureModifiers(param), param->name(),
					param->questionToken(), ensureType(param, false),
					ensureNoInitializer(param));
				preserveJsDoc(updated, param);
				parameterProperties.push_back(updated);
			} else {
				// Pattern - this is currently an error, but we emit
				// declarations for it somewhat correctly
				auto elems = walkBindingPattern(
					param->name()->as<BindingPattern>(), param);
				parameterProperties.insert(parameterProperties.end(),
				                           elems.begin(), elems.end());
			}
		}
		state->getSymbolAccessibilityDiagnostic = oldDiag;
	}

	// When the class has at least one private identifier, create a unique
	// constant identifier to retain the nominal typing behavior Prevents other
	// classes with the same public members from being used in place of the
	// current class
	Node* privateIdentifier = nullptr;
	bool hasPrivate = false;
	for (Node* member : (*classNode->classLikeData().members)->nodes) {
		if (member->name() != nullptr &&
		    isPrivateIdentifier(member->name())) {
			hasPrivate = true;
			break;
		}
	}
	if (hasPrivate) {
		privateIdentifier = factory()->newPropertyDeclaration(
			nullptr, factory()->newPrivateIdentifier("#private"), nullptr,
			nullptr, nullptr);
	}

	std::vector<Node*> lateIndexes = resolver->CreateLateBoundIndexSignatures(classNode, enclosingDeclaration,
		declarationEmitNodeBuilderFlags,
		declarationEmitInternalNodeBuilderFlags, tracker);

	std::vector<Node*> memberNodes;
	memberNodes.reserve(
		(*classNode->classLikeData().members)->nodes.size());
	if (privateIdentifier != nullptr) {
		memberNodes.push_back(privateIdentifier);
	}
	memberNodes.insert(memberNodes.end(), lateIndexes.begin(),
	                   lateIndexes.end());
	memberNodes.insert(memberNodes.end(), parameterProperties.begin(),
	                   parameterProperties.end());
	memberNodes.insert(memberNodes.end(), extraMembers.begin(),
	                   extraMembers.end());
	NodeList* visitResult =
		visitor()->visitNodes(*classNode->classLikeData().members);
	if (visitResult != nullptr && !visitResult->nodes.empty()) {
		memberNodes.insert(memberNodes.end(), visitResult->nodes.begin(),
		                   visitResult->nodes.end());
	}
	return factory()->newNodeList(memberNodes);
}

// transform.go:1990 transformClassDeclaration
Node* DeclarationTransformerImpl::transformClassDeclaration(
	ClassDeclaration* input) {
	Node* previousEnclosingDeclaration = enclosingDeclaration;
	enclosingDeclaration = input->asNode();
	ScopeExit restore{
		[this, previousEnclosingDeclaration] {
			enclosingDeclaration = previousEnclosingDeclaration;
		}};

	state->errorNameNode = input->name;
	tracker->PushErrorFallbackNode(input->asNode());
	ScopeExit popFallback{[this] { tracker->PopErrorFallbackNode(); }};

	ModifierList* modifiers = ensureModifiers(input->asNode());
	NodeList* typeParameters =
		ensureTypeParams(input->asNode(), input->TypeParameters);

	// Collect this.x property assignments from constructors and static blocks
	// in JS files
	std::vector<Node*> extraMembers;
	if (isInJSFile(input->asNode())) {
		extraMembers = collectThisPropertyAssignments(input->asNode());
	}

	NodeList* members = buildClassMembers(input->asNode(), extraMembers);

	Node* extendsClause = getEffectiveBaseTypeNode(input->asNode());

	if (extendsClause != nullptr &&
	    !isEntityNameExpression(
	        extendsClause->as<ExpressionWithTypeArguments>()->Expression) &&
	    extendsClause->as<ExpressionWithTypeArguments>()
	            ->Expression->kind != Kind::NullKeyword) {
		tracker->ReportInferenceFallback(
			extendsClause->as<ExpressionWithTypeArguments>()
				->Expression); // Add an isolated declarations error on this
		                   // extends clause
		std::string oldId = "default";
		if (nodeIsPresent(input->name) && isIdentifier(input->name) &&
		    !input->name->text().empty()) {
			oldId = input->name->text();
		}
		Node* newId = factory()->newUniqueName(
			oldId + "_base",
			printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsOptimistic, "", ""});
		Node* extendsClauseCapture = extendsClause;
		Node* inputName = input->name;
		state->getSymbolAccessibilityDiagnostic =
			[extendsClauseCapture,
		     inputName](printer::SymbolAccessibilityResult&)
			-> SymbolAccessibilityDiagnostic* {
			auto* d = new SymbolAccessibilityDiagnostic();
			d->diagnosticMessage =
				X_extends_clause_of_exported_class_0_has_or_is_using_private_name_1;
			d->errorNode = extendsClauseCapture;
			d->typeName = inputName;
			return d;
		};

		Node* varDecl = factory()->newVariableDeclaration(
			newId, nullptr,
			resolver->CreateTypeOfExpression(extendsClause->expression(),
				input->asNode(), declarationEmitNodeBuilderFlags,
				declarationEmitInternalNodeBuilderFlags, tracker),
			nullptr);
		ModifierList* mods2 = nullptr;
		if (needsDeclare) {
			mods2 = factory()->newModifierList(
				{factory()->newToken(Kind::DeclareKeyword)});
		}
		Node* statement = factory()->newVariableStatement(
			mods2,
			factory()->newVariableDeclarationList(
				factory()->newNodeList({varDecl}), NodeFlagsConst));
		Node* newHeritageClause = factory()->updateHeritageClause(
			extendsClause->parent->as<HeritageClause>(),
			extendsClause->parent->as<HeritageClause>()->Token,
			factory()->newNodeList(
				{factory()->updateExpressionWithTypeArguments(
					extendsClause->as<ExpressionWithTypeArguments>(),
					newId,
					visitor()->visitNodes(
						extendsClause->as<ExpressionWithTypeArguments>()
							->TypeArguments))}));
		NodeList* retainedHeritageClauses =
			visitor()->visitNodes(input->HeritageClauses); // should just be
			                                             // `implements`
		std::vector<Node*> heritageList{newHeritageClause};
		if (retainedHeritageClauses != nullptr &&
		    !retainedHeritageClauses->nodes.empty()) {
			heritageList.insert(heritageList.end(),
			                    retainedHeritageClauses->nodes.begin(),
			                    retainedHeritageClauses->nodes.end());
		}
		NodeList* heritageClauses = factory()->newNodeList(heritageList);

		return factory()->newSyntaxList(
			{statement,
			 factory()->updateClassDeclaration(
				 input, modifiers, input->name, typeParameters,
				 heritageClauses, members)});
	}

	return factory()->updateClassDeclaration(
		input, modifiers, input->name, typeParameters,
		visitor()->visitNodes(input->HeritageClauses), members);
}

// transform.go:2084 visitThisPropertyAssignments
Node* DeclarationTransformerImpl::visitThisPropertyAssignments(Node* node) {
	Node* thisTarget = nullptr;
	bool isStatic = false;
	Node* thisContainer = getThisContainer(node, false, false);
	thisTarget = thisContainer->parent;
	if (thisTarget == nullptr) {
		return nullptr; // thisContainer was source file, can't have
		                // expando-this
	}
	if (hasStaticModifier(thisContainer) ||
	    isClassStaticBlockDeclaration(thisContainer)) {
		isStatic = true;
	}
	if (thisTarget != enclosingDeclaration) {
		return nullptr; // stop searching within new `this` contexts
	}
	switch (getAssignmentDeclarationKind(node)) {
	case JSDeclarationKind::ThisProperty: {
		Node* name = getNameOfDeclaration(node);
		Node* base = resolver->GetReferencedMemberValueDeclaration(node);
		thisPropertyAssignmentKey key =
			getThisPropertyAssignmentKey(name, node, isStatic);
		if (base == nullptr || seenProperties.count(key) != 0) {
			break;
		}
		seenProperties.insert(key);

		// problem: this prop might be overriding a prop from a base type. The
		// checker has special bails for override compat comparisons for binary
		// expression properties, but what we transform to won't - so we either
		// need to match the base type (for example, if it's a getter/setter)
		// or emit nothing See `checkKindsOfPropertyMemberOverrides` in the
		// checker for what we're trying to satisfy here
		if (*thisTarget->classLikeData().heritageClauses != nullptr &&
		    !(*thisTarget->classLikeData().heritageClauses)->nodes.empty() &&
		    !isClassExtendingNull(thisTarget)) {
			// there is a base type any assignments might be "from"
			tracker->ReportInferenceFallback(
				thisTarget); // Add an isolated declarations error on this
			                 // class - we can't know how to transform this
			                 // prop into an assignment without referring to
			                 // type information
			if (resolver->IsThisPropertyAssignmentDeclarationRedundant(
			        node)) {
				break; // skip assignments whose member is already provided by
				       // an `extends` base type (an inherited accessor/method,
				       // or an identical inherited property)
				// TODO: If the property has an explicit `@type` annotation, we
				// should probably emit it (maybe with an `override` modifier)
				// instead of skipping it
			}
		}

		ModifierList* mods = nullptr;
		if (isStatic) {
			mods = factory()->newModifierList(
				{factory()->newToken(Kind::StaticKeyword)});
		}
		if (hasDynamicName(node)) {
			if (!transformers::isSimpleInlineableExpression(name)) {
				break; // Member either becomes an index signature or is a
				       // reassignment
			}
			checkName(node);
			name = factory()->newComputedPropertyName(
				name); // Convert `this[foo] = expr` to `[foo]: Type`
		}
		if (getTextOfPropertyName(name) == "constructor") {
			break; // `constructor` is a builtin class member, not allowed to
			       // redeclare it
		}
		if (isIdentifier(name) &&
		    !isIdentifierText(name->text(), LanguageVariant::Standard)) {
			name = factory()->newStringLiteralFromNode(name);
		}
		Node* prop = factory()->newPropertyDeclaration(
			mods, name, nullptr, ensureType(node, false), nullptr);
		if (isExpressionStatement(node->parent)) {
			preserveJsDoc(prop, node->parent);
		}
		thisPropertyAssignmentsCollected.push_back(prop);
		break;
	}
	default:
		break;
	}
	return thisPropertyVisitor->visitEachChild(node);
}

// transform.go:2171 collectThisPropertyAssignments — finds `this.x = expr`
// assignments in constructors, methods, and static blocks of JS classes and
// synthesizes PropertyDeclaration nodes for each unique property name.
std::vector<Node*> DeclarationTransformerImpl::collectThisPropertyAssignments(
	Node* classNode) {
	NodeList* members = *classNode->classLikeData().members;
	std::unordered_set<thisPropertyAssignmentKey, thisPropertyAssignmentKeyHash>
		seen;
	// Pre-populate seen with existing direct member nodes to avoid duplicates
	for (Node* member : members->nodes) {
		if (member->name() != nullptr) {
			bool isStatic = tsc::isStatic(member);
			seen.insert(getThisPropertyAssignmentKey(member->name(), member,
			                                       isStatic));
		}
	}
	seenProperties = seen;
	ScopeExit clearSeen{
		[this] { seenProperties = std::unordered_set<
		                         thisPropertyAssignmentKey,
		                         thisPropertyAssignmentKeyHash>{}; }};
	thisPropertyAssignmentsCollected.clear();
	ScopeExit clearCollected{
		[this] { thisPropertyAssignmentsCollected.clear(); }};

	for (Node* n : members->nodes) {
		thisPropertyVisitor->visitEachChild(n);
	}
	return thisPropertyAssignmentsCollected;
}

// transform.go:2194 walkBindingPattern
std::vector<Node*> DeclarationTransformerImpl::walkBindingPattern(
	BindingPattern* pattern, Node* param) {
	std::vector<Node*> elems;
	for (Node* elem : pattern->Elements->nodes) {
		if (isOmittedExpression(elem)) {
			continue;
		}
		if (isBindingPattern(elem->name())) {
			auto nested = walkBindingPattern(
				elem->name()->as<BindingPattern>(), param);
			elems.insert(elems.end(), nested.begin(), nested.end());
			continue;
		}
		elems.push_back(factory()->newPropertyDeclaration(
			ensureModifiers(param), elem->name(),
			nullptr /*questionOrExclamationToken*/, ensureType(elem, false),
			nullptr /*initializer*/));
	}
	return elems;
}

// transform.go:2215 transformVariableStatement
Node* DeclarationTransformerImpl::transformVariableStatement(
	VariableStatement* input) {
	bool visible = false;
	for (Node* decl : input->DeclarationList->as<VariableDeclarationList>()
	                     ->Declarations->nodes) {
		visible = getBindingNameVisible(resolver, decl);
		if (visible) {
			break;
		}
	}
	if (!visible) {
		return nullptr;
	}

	std::vector<Node*> inputNodes =
		input->DeclarationList->as<VariableDeclarationList>()
			->Declarations->nodes;
	std::vector<Node*> extraImports;
	if (state->currentSourceFile->CommonJSModuleIndicator != nullptr) {
		std::vector<Node*> normalDeclarations;
		std::vector<Node*> imports;
		for (Node* n : inputNodes) {
			if (isVariableDeclarationInitializedToRequire(n)) {
				imports.push_back(n);
			} else {
				normalDeclarations.push_back(n);
			}
		}
		inputNodes = normalDeclarations;
		extraImports = visitor()->visitSlice(imports).first;
	}

	std::vector<Node*> nodes = visitor()->visitSlice(inputNodes).first;
	if (nodes.empty()) {
		if (!extraImports.empty()) {
			return factory()->newSyntaxList(extraImports);
		}
		return nullptr;
	}
	NodeList* nodeList = factory()->newNodeList(nodes);

	ModifierList* modifiers = ensureModifiers(input->asNode());

	Node* declList = nullptr;
	if (isVarUsing(input->DeclarationList) ||
	    isVarAwaitUsing(input->DeclarationList)) {
		declList = factory()->newVariableDeclarationList(nodeList,
		                                                 NodeFlagsConst);
		emitContext()->setOriginal(declList, input->DeclarationList);
		emitContext()->setCommentRange(declList,
	                               input->DeclarationList->loc);
		declList->loc = input->DeclarationList->loc;
	} else {
		declList = factory()->updateVariableDeclarationList(
			input->DeclarationList->as<VariableDeclarationList>(), nodeList,
			input->DeclarationList->flags);
	}
	Node* res = factory()->updateVariableStatement(input, modifiers, declList);
	if (!extraImports.empty()) {
		extraImports.push_back(res);
		return factory()->newSyntaxList(extraImports);
	}
	return res;
}

// transform.go:2270 transformEnumDeclaration
Node* DeclarationTransformerImpl::transformEnumDeclaration(
	EnumDeclaration* input) {
	std::vector<Node*> members;
	for (Node* m : input->Members->nodes) {
		if (shouldStripInternal(m)) {
			continue; // core.MapNonNil drops nils
		}

		// Rewrite enum values to their constants, if available
		EvalResult enumValue = resolver->GetEnumMemberValue(m);

		if (state->isolatedDeclarations && m->initializer() != nullptr &&
		    enumValue.HasExternalReferences &&
		    // This will be its own compiler error instead, so don't report.
		    !isComputedPropertyName(m->name())) {
			state->addDiagnostic(createDiagnosticForNode(
				m,
				Enum_member_initializers_must_be_computable_without_references_to_external_symbols_with_isolatedDeclarations));
		}

		Node* newInitializer = nullptr;
		if (auto* value = std::get_if<Number>(&enumValue.Value)) {
			if (value->isInf()) {
				if (value->v > 0) {
					newInitializer = factory()->newIdentifier("Infinity");
				} else {
					newInitializer = factory()->newPrefixUnaryExpression(
						Kind::MinusToken,
						factory()->newIdentifier("Infinity"));
				}
			} else if (value->isNaN()) {
				newInitializer = factory()->newIdentifier("NaN");
			} else if (value->v >= 0) {
				newInitializer = factory()->newNumericLiteral(
					value->string(), TokenFlagsNone);
			} else {
				newInitializer = factory()->newPrefixUnaryExpression(
					Kind::MinusToken,
					factory()->newNumericLiteral(Number(-value->v).string(),
					                             TokenFlagsNone));
			}
		} else if (auto* s = std::get_if<std::string>(&enumValue.Value)) {
			newInitializer =
				factory()->newStringLiteral(*s, TokenFlagsNone);
		} else {
			// nil
			newInitializer = nullptr;
		}
		Node* result = factory()->updateEnumMember(m->as<EnumMember>(),
		                                         m->name(), newInitializer);
		preserveJsDoc(result, m);
		members.push_back(result);
	}
	return factory()->updateEnumDeclaration(input,
	                                        ensureModifiers(input->asNode()),
	                                        input->name,
	                                        factory()->newNodeList(members));
}

// transform.go:2321 ensureModifiers
ModifierList* DeclarationTransformerImpl::ensureModifiers(Node* node) {
	ModifierFlags currentFlags =
		getCombinedModifierFlags(emitContext()->parseNode(node)) &
		ModifierFlagsAll;
	ModifierFlags newFlags = ensureModifierFlags(node);
	if (currentFlags == newFlags) {
		// Elide decorators
		ModifierList* mods = node->modifiers();
		if (mods == nullptr) {
			return mods;
		}
		if (canReuseModifierNodes(mods->nodes)) {
			std::vector<Node*> filtered;
			for (Node* m : mods->nodes) {
				if (isModifier(m)) {
					filtered.push_back(m);
				}
			}
			return factory()->newModifierList(filtered);
		}
	}
	std::vector<Node*> result = createModifiersFromModifierFlags(
		newFlags, newModifierFromFactory, *factory()->asNodeFactory());
	if (result.empty()) {
		return nullptr;
	}
	return factory()->newModifierList(result);
}

// transform.go:2341 ensureModifierFlags
ModifierFlags DeclarationTransformerImpl::ensureModifierFlags(Node* node) {
	ModifierFlags mask =
		ModifierFlagsAll ^ (ModifierFlagsPublic | ModifierFlagsAsync |
	                        ModifierFlagsOverride); // No async and override
	                                                // modifiers in declaration
	                                                // files
	ModifierFlags additions = ModifierFlagsNone;
	if (needsDeclare && !isAlwaysType(node)) {
		additions = ModifierFlagsAmbient;
	}
	bool parentIsFile = node->parent->kind == Kind::SourceFile;
	if (!parentIsFile) {
		mask ^= ModifierFlagsAmbient;
		additions = ModifierFlagsNone;
	}
	if (isImplicitlyExportedJSDocDeclaration(node)) {
		additions |= ModifierFlagsExport;
	}
	return maskModifierFlags(node, mask, additions);
}

// transform.go:2358 ensureTypeParams
NodeList* DeclarationTransformerImpl::ensureTypeParams(Node* node,
                                                       NodeList* params) {
	if (resolver->GetEffectiveDeclarationFlags(emitContext()->parseNode(node),
	                                       ModifierFlagsPrivate) != 0) {
		return nullptr;
	}
	NodeList* typeParameters = visitor()->visitNodes(params);
	if (typeParameters != nullptr) {
		return typeParameters;
	}
	Node* oldErrorNameNode = state->errorNameNode;
	state->errorNameNode = node->name();
	GetSymbolAccessibilityDiagnostic oldDiag;
	if (!suppressNewDiagnosticContexts) {
		oldDiag = state->getSymbolAccessibilityDiagnostic;
		if (canProduceDiagnostics(node)) {
			state->getSymbolAccessibilityDiagnostic =
				createGetSymbolAccessibilityDiagnosticForNode(node);
		}
	}

	FunctionLikeDataRef data = node->functionLikeData();
	if (data.fullSignature != nullptr && *data.fullSignature != nullptr) {
		if (std::vector<Node*> nodes =
		        resolver->CreateTypeParametersOfSignatureDeclaration(node, enclosingDeclaration,
		            declarationEmitNodeBuilderFlags,
		            declarationEmitInternalNodeBuilderFlags, tracker);
		    !nodes.empty()) {
			typeParameters = factory()->newNodeList(nodes);
			typeParameters->loc = node->loc;
		}
	}

	state->errorNameNode = oldErrorNameNode;
	if (!suppressNewDiagnosticContexts) {
		state->getSymbolAccessibilityDiagnostic = oldDiag;
	}
	return typeParameters;
}

// transform.go:2392 updateParamList
NodeList* DeclarationTransformerImpl::updateParamList(Node* node,
                                                      NodeList* params) {
	if (resolver->GetEffectiveDeclarationFlags(emitContext()->parseNode(node),
	                                       ModifierFlagsPrivate) != 0 ||
	    params->nodes.empty()) {
		return factory()->newNodeList({});
	}
	std::vector<Node*> results(params->nodes.size());
	for (size_t i = 0; i < params->nodes.size(); i++) {
		results[i] =
			ensureParameter(params->nodes[i]->as<ParameterDeclaration>());
	}
	return factory()->newNodeList(results);
}

// transform.go:2403 ensureParameter
Node* DeclarationTransformerImpl::ensureParameter(ParameterDeclaration* p) {
	GetSymbolAccessibilityDiagnostic oldDiag =
		state->getSymbolAccessibilityDiagnostic;
	if (!suppressNewDiagnosticContexts) {
		state->getSymbolAccessibilityDiagnostic =
			createGetSymbolAccessibilityDiagnosticForNode(p->asNode());
	}
	Node* questionToken = nullptr;
	if (resolver->IsOptionalParameter(p->asNode())) {
		if (p->QuestionToken != nullptr) {
			questionToken = p->QuestionToken;
		} else {
			questionToken = factory()->newToken(Kind::QuestionToken);
		}
	}
	Node* result = factory()->updateParameterDeclaration(
		p, nullptr, p->DotDotDotToken,
		bindingNameVisitor->visitNode(p->name), questionToken,
		ensureType(p->asNode(), true), ensureNoInitializer(p->asNode()));
	state->getSymbolAccessibilityDiagnostic = oldDiag;
	return result;
}

// transform.go:2429 ensureNoInitializer
Node* DeclarationTransformerImpl::ensureNoInitializer(Node* node) {
	if (shouldPrintWithInitializer(node)) {
		Node* unwrappedInitializer =
			unwrapParenthesizedExpression(node->initializer());
		if (!isPrimitiveLiteralValue(unwrappedInitializer, true)) {
			tracker->ReportInferenceFallback(node);
		}
		return resolver->CreateLiteralConstValue(emitContext()->parseNode(node), tracker);
	}
	return nullptr;
}

// transform.go:2440 visitBindingName
Node* DeclarationTransformerImpl::visitBindingName(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
	case Kind::OmittedExpression:
		return node;
	case Kind::ArrayBindingPattern:
	case Kind::ObjectBindingPattern:
		return node->visitEachChild(*bindingNameVisitor);
	case Kind::BindingElement:
		if (node->propertyName() != nullptr &&
		    isComputedPropertyName(node->propertyName()) &&
		    isEntityNameExpression(
		        node->propertyName()->expression())) {
			checkEntityNameVisibility(node->propertyName()->expression(),
			                          enclosingDeclaration);
		}
		return factory()->updateBindingElement(
			node->as<BindingElement>(),
			node->as<BindingElement>()->DotDotDotToken,
			node->propertyName(),
			bindingNameVisitor->visitNode(node->name()),
			nullptr /*initializer*/);
	default:
		return node;
	}
}

// transform.go:2456 transformImportEqualsDeclaration
Node* DeclarationTransformerImpl::transformImportEqualsDeclaration(
	ImportEqualsDeclaration* decl) {
	if (!resolver->IsDeclarationVisible(decl->asNode())) {
		return nullptr;
	}
	if (decl->ModuleReference->kind == Kind::ExternalModuleReference) {
		// Rewrite external module names if necessary
		Node* specifier =
			getExternalModuleImportEqualsDeclarationExpression(
				decl->asNode());
		return factory()->updateImportEqualsDeclaration(
			decl, decl->modifiers, decl->isTypeOnly(), decl->name,
			factory()->updateExternalModuleReference(
				decl->ModuleReference->as<ExternalModuleReference>(),
				rewriteModuleSpecifier(decl->asNode(), specifier)));
	} else {
		GetSymbolAccessibilityDiagnostic oldDiag =
			state->getSymbolAccessibilityDiagnostic;
		state->getSymbolAccessibilityDiagnostic =
			createGetSymbolAccessibilityDiagnosticForNode(decl->asNode());
		checkEntityNameVisibility(decl->ModuleReference,
		                          enclosingDeclaration);
		state->getSymbolAccessibilityDiagnostic = oldDiag;
		return decl->asNode();
	}
}

// transform.go:2479 transformImportDeclaration
Node* DeclarationTransformerImpl::transformImportDeclaration(
	ImportDeclaration* decl) {
	if (decl->ImportClause == nullptr) {
		// import "mod" - possibly needed for side effects? (global interface
		// patches, module augmentations, etc)
		return factory()->updateImportDeclaration(
			decl, decl->modifiers, decl->ImportClause,
			rewriteModuleSpecifier(decl->asNode(), decl->ModuleSpecifier),
			decl->Attributes);
	}
	Kind phaseModifier = decl->ImportClause->as<ImportClause>()->PhaseModifier;
	if (phaseModifier == Kind::DeferKeyword) {
		phaseModifier = Kind::Unknown;
	}
	// The `importClause` visibility corresponds to the default's visibility.
	Node* visibleDefaultBinding = nullptr;
	if (decl->ImportClause != nullptr &&
	    decl->ImportClause->name() != nullptr &&
	    resolver->IsDeclarationVisible(decl->ImportClause)) {
		visibleDefaultBinding = decl->ImportClause->name();
	}
	if (decl->ImportClause->as<ImportClause>()->NamedBindings == nullptr) {
		// No named bindings (either namespace or list), meaning the import is
		// just default or should be elided
		if (visibleDefaultBinding == nullptr) {
			return nullptr;
		}
		return factory()->updateImportDeclaration(
			decl, decl->modifiers,
			factory()->updateImportClause(
				decl->ImportClause->as<ImportClause>(), phaseModifier,
				visibleDefaultBinding, /*namedBindings*/ nullptr),
			rewriteModuleSpecifier(decl->asNode(), decl->ModuleSpecifier),
			decl->Attributes);
	}
	if (decl->ImportClause->as<ImportClause>()->NamedBindings->kind ==
	    Kind::NamespaceImport) {
		// Namespace import (optionally with visible default)
		Node* namedBindings = nullptr;
		if (resolver->IsDeclarationVisible(
		        decl->ImportClause->as<ImportClause>()->NamedBindings)) {
			namedBindings =
				decl->ImportClause->as<ImportClause>()->NamedBindings;
		}
		if (visibleDefaultBinding == nullptr && namedBindings == nullptr) {
			return nullptr;
		}
		return factory()->updateImportDeclaration(
			decl, decl->modifiers,
			factory()->updateImportClause(
				decl->ImportClause->as<ImportClause>(), phaseModifier,
				visibleDefaultBinding, namedBindings),
			rewriteModuleSpecifier(decl->asNode(), decl->ModuleSpecifier),
			decl->Attributes);
	}
	// Named imports (optionally with visible default)
	std::vector<Node*> bindingList;
	for (Node* b :
	     decl->ImportClause->as<ImportClause>()->NamedBindings->elements()) {
		if (resolver->IsDeclarationVisible(b)) {
			bindingList.push_back(b);
		}
	}
	if (!bindingList.empty() || visibleDefaultBinding != nullptr) {
		Node* namedImports = nullptr;
		if (!bindingList.empty()) {
			namedImports = factory()->updateNamedImports(
				decl->ImportClause->as<ImportClause>()
					->NamedBindings->as<NamedImports>(),
				factory()->newNodeList(bindingList));
		}
		return factory()->updateImportDeclaration(
			decl, decl->modifiers,
			factory()->updateImportClause(
				decl->ImportClause->as<ImportClause>(), phaseModifier,
				visibleDefaultBinding, namedImports),
			rewriteModuleSpecifier(decl->asNode(), decl->ModuleSpecifier),
			decl->Attributes);
	}
	// Augmentation of export depends on import
	if (resolver->IsImportRequiredByAugmentation(decl)) {
		if (state->isolatedDeclarations) {
			state->addDiagnostic(createDiagnosticForNode(
				decl->asNode(),
				Declaration_emit_for_this_file_requires_preserving_this_import_for_augmentations_This_is_not_supported_with_isolatedDeclarations));
		}
		return factory()->updateImportDeclaration(
			decl, decl->modifiers, /*importClause*/ nullptr,
			rewriteModuleSpecifier(decl->asNode(), decl->ModuleSpecifier),
			decl->Attributes);
	}
	// Nothing visible
	return nullptr;
}

// transform.go:2584 transformJSDocTypeExpression
Node* DeclarationTransformerImpl::transformJSDocTypeExpression(
	JSDocTypeExpression* input) {
	return visitor()->visitNode(input->Type);
}

// transform.go:2588 transformJSDocTypeLiteral
Node* DeclarationTransformerImpl::transformJSDocTypeLiteral(
	JSDocTypeLiteral* input) {
	auto members = visitor()->visitSlice(input->JSDocPropertyTags);
	Node* replacement =
		factory()->newTypeLiteralNode(factory()->newNodeList(members.first));
	emitContext()->setOriginal(replacement, input->asNode());
	return replacement;
}

// transform.go:2595 transformJSDocPropertyTag
Node* DeclarationTransformerImpl::transformJSDocPropertyTag(
	JSDocParameterOrPropertyTag* input) {
	Node* replacement = factory()->newPropertySignatureDeclaration(
		nullptr, visitor()->visitNode(input->TagName), nullptr,
		visitor()->visitNode(input->TypeExpression), nullptr);
	emitContext()->setOriginal(replacement, input->asNode());
	return replacement;
}

// transform.go:2607 transformJSDocAllType
Node* DeclarationTransformerImpl::transformJSDocAllType(
	JSDocAllType* input) {
	Node* replacement = factory()->newKeywordTypeNode(Kind::AnyKeyword);
	emitContext()->setOriginal(replacement, input->asNode());
	return replacement;
}

// transform.go:2613 transformJSDocNullableType
Node* DeclarationTransformerImpl::transformJSDocNullableType(
	JSDocNullableType* input) {
	Node* replacement = factory()->newUnionTypeNode(
		factory()->newNodeList({visitor()->visitNode(input->Type),
		                        factory()->newLiteralTypeNode(
		                            factory()->newKeywordExpression(
		                                Kind::NullKeyword))}));
	emitContext()->setOriginal(replacement, input->asNode());
	return replacement;
}

// transform.go:2622 transformJSDocNonNullableType
Node* DeclarationTransformerImpl::transformJSDocNonNullableType(
	JSDocNonNullableType* input) {
	return visitor()->visitNode(input->Type);
}

// transform.go:2626 transformJSDocVariadicType
Node* DeclarationTransformerImpl::transformJSDocVariadicType(
	JSDocVariadicType* input) {
	Node* replacement =
		factory()->newArrayTypeNode(visitor()->visitNode(input->Type));
	emitContext()->setOriginal(replacement, input->asNode());
	return replacement;
}

// transform.go:2632 transformJSDocOptionalType
Node* DeclarationTransformerImpl::transformJSDocOptionalType(
	JSDocOptionalType* input) {
	Node* replacement = factory()->newUnionTypeNode(
		factory()->newNodeList({visitor()->visitNode(input->Type),
		                        factory()->newKeywordTypeNode(
		                            Kind::UndefinedKeyword)}));
	emitContext()->setOriginal(replacement, input->asNode());
	return replacement;
}

// transform.go:2641 getNameExpressionPreferringIdentifier
Node* DeclarationTransformerImpl::getNameExpressionPreferringIdentifier(
	Node* nameExpr) {
	if (isNumericLiteral(nameExpr)) {
		// Numeric property names are string properties in JS; convert to
		// string literal
		nameExpr = factory()->newStringLiteral(nameExpr->text(),
		                                       TokenFlagsNone);
	}
	if (isStringLiteralLike(nameExpr) &&
	    isIdentifierText(nameExpr->text(), LanguageVariant::Standard)) {
		Node* result = factory()->newIdentifier(
			nameExpr->text()); // prefer non-string literal names where
		                   // possible
		Kind kwKind = identifierToKeywordKind(result->as<Identifier>());
		// keep keywords as strings, except `default`, which has special
		// reformulations in the transformer
		if (kwKind == Kind::Unknown || kwKind == Kind::DefaultKeyword) {
			// fake this into a parse tree node so the reference resolver
			// resolves the node via `resolveName`
			result->parent = nameExpr->parent;
			result->flags &= ~NodeFlagsSynthesized;
			// intentionally leave Loc unset so the string isn't used as the
			// text source of the identifier
			return result;
		}
	}
	return nameExpr;
}

namespace {
// transform.go:2661 isNotDeclareModifier
bool isNotDeclareModifier(Node* mod) {
	return mod->kind != Kind::DeclareKeyword;
}
} // namespace

// transform.go:2665 stripDeclareModifiers
Node* DeclarationTransformerImpl::stripDeclareModifiers(Node* node) {
	if (node == nullptr) {
		return nullptr;
	}
	ModifierList* mods = node->modifiers();
	if (mods != nullptr) {
		ModifierFlags flags = node->modifierFlags();
		if ((flags & ModifierFlagsAmbient) != 0) {
			std::vector<Node*> filtered;
			for (Node* m : mods->nodes) {
				if (isNotDeclareModifier(m)) {
					filtered.push_back(m);
				}
			}
			node->setModifiers(factory()->newModifierList(filtered));
		}
	}
	return node; // no need to recur into children, only strip at top-level
}

// transform.go:2680 visitCJSExportAssignments
Node* DeclarationTransformerImpl::visitCJSExportAssignments(
	Node* expression) {
	if (expression != nullptr) {
		auto [_, cleanupDiagnosticContext] =
			setupDiagnosticContext(expression);
	ScopeExit cleanup{cleanupDiagnosticContext};
		switch (getAssignmentDeclarationKind(expression)) {
		case JSDeclarationKind::ModuleExports:
			if (state->currentSourceFile->CommonJSModuleIndicator !=
			    nullptr) {
				Node* result = transformExportAssignment(
					expression->parent, expression,
					expression->as<BinaryExpression>()->Right,
					true /*isExportEquals*/);
				if (result != nullptr) {
					cjsExportAssignment = result;
					resultHasScopeMarker = true;
					resultHasExternalModuleIndicator = true;
				}
			}
			break;
		default:
			break;
		}
		return cjsExportAssignmentVisitor->visitEachChild(
			expression); // recur through the whole tree, looking for
			             // module.exports=
	}
	return nullptr;
}

// transform.go:2700 visitNestedExpression
Node* DeclarationTransformerImpl::visitNestedExpression(Node* expression) {
	if (expression != nullptr) {
		auto [_, cleanupDiagnosticContext] =
			setupDiagnosticContext(expression);
	ScopeExit cleanup{cleanupDiagnosticContext};
		switch (getAssignmentDeclarationKind(expression)) {
		case JSDeclarationKind::Property:
			transformExpandoAssignment(
				expression->as<BinaryExpression>());
			break;
		case JSDeclarationKind::ExportsProperty:
			if (state->currentSourceFile->CommonJSModuleIndicator !=
			    nullptr) {
				Node* result = transformCommonJSExport(
					expression,
					getNameExpressionPreferringIdentifier(
						getElementOrPropertyAccessName(
							expression->as<BinaryExpression>()
								->Left)));
				if (result != nullptr) {
					cjsExportMembers.push_back(result);
				}
			}
			break;
		case JSDeclarationKind::ObjectDefinePropertyExports:
			if (state->currentSourceFile->CommonJSModuleIndicator !=
			    nullptr) {
				Node* result = transformCommonJSExport(
					expression,
					getNameExpressionPreferringIdentifier(
						expression->arguments()[1]));
				if (result != nullptr) {
					cjsExportMembers.push_back(result);
				}
			}
			break;
		default:
			break;
		}
		return expressionVisitor->visitEachChild(
			expression); // recur through the whole tree, looking for special
			             // assignments
	}
	return nullptr;
}

// transform.go:2727 transformExpandoAssignment
void DeclarationTransformerImpl::transformExpandoAssignment(
	BinaryExpression* node) {
	Node* left = node->Left;

	Symbol* symbol = node->Symbol;
	if (symbol == nullptr ||
	    (symbol->flags & SymbolFlagsAssignment) == 0) {
		return;
	}

	Node* ns = getLeftmostAccessExpression(left);
	if (ns == nullptr || ns->kind != Kind::Identifier) {
		return;
	}

	Node* declaration = resolver->GetReferencedValueDeclaration(ns);
	if (declaration == nullptr) {
		return;
	}

	if (shouldStripInternal(declaration)) {
		return;
	}

	if (isVariableDeclaration(declaration) &&
	    declaration->type() != nullptr) {
		return;
	}

	if (isFunctionDeclaration(declaration) &&
	    declaration->functionLikeData().fullSignature != nullptr &&
	    *declaration->functionLikeData().fullSignature != nullptr) {
		return;
	}

	if (isVariableDeclaration(declaration) &&
	    !isFunctionLike(declaration->initializer())) {
		return; // We're going to add a type, no need to dupe members with a
		        // namespace
	}

	Symbol* hostSym = declaration->symbol();
	if (hostSym == nullptr) {
		return;
	}

	Node* name = factory()->newIdentifier(ns->text());
	std::string property = tryGetPropertyName(left);
	if (property.empty() ||
	    !isIdentifierText(property, LanguageVariant::Standard)) {
		return;
	}

	NodeId hostId = getExpandoHostId(declaration);

	if (isDeclaration(declaration) &&
	    isDeclarationAndNotVisible(emitContext(), resolver, declaration)) {
		// The host isn't visible (yet) - printing the type of a visible
		// declaration may still late-mark it as visible (e.g. an exported
		// variable whose type prints as `typeof host`), so defer the
		// assignment to be processed if and when that happens.
		deferredExpandoAssignments[hostId].push_back(node->asNode());
		return;
	}

	if (isFunctionDeclaration(declaration) &&
	    !shouldEmitFunctionProperties(
	        declaration->as<FunctionDeclaration>())) {
		return;
	}

	transformExpandoHost(name, declaration);

	Node* exportName = factory()->newIdentifier(property);
	Node* localName = tryGetNameOfAssignedExpression(node->asNode());
	if (localName == nullptr &&
	    !resolver->IsNameResolvable(enclosingDeclaration, property) &&
	    !isNonContextualKeyword(stringToToken(exportName->text()))) {
		// use exportName as localName if there won't be any conflicts or
		// keyword issues
		localName = exportName;
	}
	if (localName == nullptr ||
	    isNonContextualKeyword(stringToToken(localName->text()))) {
		// fallback to a generated name if the localName doesn't exist or is a
		// keyword
		localName = factory()->newGeneratedNameForNode(node->asNode());
	}

	auto [_, cleanupDiagnosticContext] =
		setupDiagnosticContext(node->asNode());
ScopeExit cleanup{cleanupDiagnosticContext};

	bool preexistingExpandoHasExport = false;
	for (Node* m : expandoMembers[hostId]) {
		if (isExportDeclaration(m)) {
			preexistingExpandoHasExport = true;
			break;
		}
	}

	if (isIdentifier(node->Right)) {
		if (!preexistingExpandoHasExport) {
			addExportModifierToExpandoMembers(hostId);
		}
		// alias-like, emit an `export {name}` or `export {name as alias}`
		Node* result = transformBinaryExpressionToExportDeclaration(
			node->asNode(), exportName);
		expandoMembers[hostId].push_back(result);
		return;
	}

	ModifierList* varModifiers = nullptr;

	if (preexistingExpandoHasExport) {
		varModifiers = factory()->newModifierList(
			createModifiersFromModifierFlags(ModifierFlagsExport,
			                             newModifierFromFactory,
			                             *factory()->asNodeFactory()));
	}

	Node* synthesizedNamespace = factory()->newModuleDeclaration(
		nullptr /*modifiers*/, Kind::NamespaceKeyword, name, nullptr,
		factory()->newModuleBlock(factory()->newNodeList({})));
	synthesizedNamespace->parent = enclosingDeclaration;
	DeclarationDataRef declarationData =
		synthesizedNamespace->declarationData();
	*declarationData.symbol = hostSym;
	LocalsContainerDataRef containerData =
		synthesizedNamespace->localsContainerData();
	*containerData.locals = SymbolTable();
	(*containerData.locals)[localName->text()] = symbol;

	Node* oldEnclosing = enclosingDeclaration;
	enclosingDeclaration = synthesizedNamespace;
	ScopeExit restoreEnclosing{
		[this, oldEnclosing] { enclosingDeclaration = oldEnclosing; }};

	std::vector<Node*> statements{
		factory()->newVariableStatement(
			varModifiers,
			factory()->newVariableDeclarationList(
				factory()->newNodeList({factory()->newVariableDeclaration(
					localName, nullptr /*exclamationToken*/,
					ensureType(node->asNode(), false),
					nullptr /*initializer*/)}),
				NodeFlagsNone)),
	};

	if (localName->text() != exportName->text()) {
		Node* namedExports = factory()->newNamedExports(
			factory()->newNodeList({factory()->newExportSpecifier(
				false /*isTypeOnly*/, localName, exportName)}));
		statements.push_back(factory()->newExportDeclaration(
			nullptr /*modifiers*/, false /*isTypeOnly*/, namedExports,
			nullptr /*moduleSpecifier*/, nullptr /*attributes*/));
		if (!preexistingExpandoHasExport) {
			// Done before adding statements to expando members to keep the
			// initial variable statement, before we rename anything, private
			addExportModifierToExpandoMembers(hostId);
		}
	}

	expandoMembers[hostId].insert(expandoMembers[hostId].end(),
	                              statements.begin(), statements.end());
}

// transform.go:2869 addExportModifierToExpandoMembers
void DeclarationTransformerImpl::addExportModifierToExpandoMembers(
	NodeId hostId) {
	// Add an `export` modifier to all existing expando members so they remain
	// exported after the `export {}` is added
	for (Node* decl : expandoMembers[hostId]) {
		// only invoked when `expandoMembers` does not *yet* contain an
		// `export` declaration, so no need to skip one here to prevent
		// `export export {}`
		ModifierFlags modifierFlags =
			ModifierFlagsExport | getCombinedModifierFlags(decl);
		decl->setModifiers(factory()->newModifierList(
			createModifiersFromModifierFlags(modifierFlags,
			                                 newModifierFromFactory,
			                                 *factory()->asNodeFactory())));
	}
}

// transform.go:2861 getExpandoHostId
NodeId DeclarationTransformerImpl::getExpandoHostId(Node* declaration) {
	Node* root = isVariableDeclaration(declaration) ? declaration->parent->parent
	                                                : declaration;
	NodeId id = getNodeId(emitContext()->mostOriginal(root));
	return id;
}

// transform.go:2867 transformExpandoHost
void DeclarationTransformerImpl::transformExpandoHost(Node* name,
                                                    Node* declaration) {
	Node* root = isVariableDeclaration(declaration) ? declaration->parent->parent
	                                                : declaration;
	NodeId id = getExpandoHostId(declaration);

	if (expandoHosts.count(id) != 0) {
		return;
	}

	bool saveNeedsDeclare = needsDeclare;
	needsDeclare = true;

	ModifierFlags modifierFlags = ensureModifierFlags(root);
	bool defaultExport =
		(modifierFlags & ModifierFlagsExport) != 0 &&
		(modifierFlags & ModifierFlagsDefault) != 0;

	needsDeclare = saveNeedsDeclare;

	if (defaultExport) {
		modifierFlags |= ModifierFlagsAmbient;
		modifierFlags ^= ModifierFlagsDefault;
		modifierFlags ^= ModifierFlagsExport;
	}

	auto [_, cleanupDiagnosticContext] = setupDiagnosticContext(declaration);
ScopeExit cleanup{cleanupDiagnosticContext};

	ModifierList* modifiers = factory()->newModifierList(
		createModifiersFromModifierFlags(modifierFlags,
		                             newModifierFromFactory,
		                             *factory()->asNodeFactory()));
	std::vector<Node*> replacement;

	if (isFunctionDeclaration(declaration)) {
		ExpandoHostParams params = extractExpandoHostParams(declaration);
		replacement.push_back(factory()->updateFunctionDeclaration(
			declaration->as<FunctionDeclaration>(), modifiers,
			params.asteriskToken, declaration->name(),
			ensureTypeParams(declaration, params.typeParameters),
			updateParamList(declaration, params.parameters),
			ensureType(declaration, false), nullptr /*fullSignature*/,
			nullptr /*body*/));
	} else if (isVariableDeclaration(declaration) &&
	           isFunctionExpressionOrArrowFunction(
	               declaration->initializer())) {
		Node* fn = declaration->initializer();
		ExpandoHostParams params = extractExpandoHostParams(fn);
		replacement.push_back(factory()->newFunctionDeclaration(
			modifiers, params.asteriskToken,
			factory()->newIdentifier(name->text()),
			ensureTypeParams(fn, params.typeParameters),
			updateParamList(fn, params.parameters), ensureType(fn, false),
			nullptr /*fullSignature*/, nullptr /*body*/));
	} else {
		expandoHosts[id] = transformTopLevelDeclaration(declaration);
		return;
	}

	state->reportExpandoFunctionErrors(declaration);

	if (defaultExport) {
		if (isSourceFile(declaration->parent)) {
			resultHasExternalModuleIndicator = true;
		}
		resultHasScopeMarker = true;
		replacement.push_back(factory()->newExportAssignment(
			nullptr /*modifiers*/, false /*isExportEquals*/,
			nullptr /*typeNode*/, name));
	}

	// store host result to be added to the output when it's actually visited
	expandoHosts[id] = factory()->newSyntaxList(replacement);
	if (lateStatementReplacementMap.count(id) != 0) {
		lateStatementReplacementMap[id] = createFullExpandoBlock(id);
	}
}

// transform.go:2924 createFullExpandoBlock
Node* DeclarationTransformerImpl::createFullExpandoBlock(NodeId id) {
	// Process any expando assignments on this host that were skipped because
	// it wasn't visible when they were collected - if it's still not visible,
	// they simply get re-deferred, and are dropped if the host is never
	// late-marked visible.
	auto deferredIt = deferredExpandoAssignments.find(id);
	if (deferredIt != deferredExpandoAssignments.end()) {
		std::vector<Node*> deferred = deferredIt->second;
		deferredExpandoAssignments.erase(deferredIt);
		for (Node* assignment : deferred) {
			transformExpandoAssignment(assignment->as<BinaryExpression>());
		}
	}
	// Go map-read semantics: no operator[] — a miss must not insert a null
	// entry (it would poison transformExpandoHost's `count(id)` guard on a
	// later pass, leaving the host untransformed).
	Node* n = nullptr;
	if (auto it = expandoHosts.find(id); it != expandoHosts.end()) {
		n = it->second;
	}
	auto addOnsIt = expandoMembers.find(id);
	if (addOnsIt != expandoMembers.end()) {
		std::vector<Node*>& addOns = addOnsIt->second;
		ModifierList* modifiers = nullptr;
		Node* name = nullptr;
		std::vector<Node*> host;
		if (n != nullptr && n->kind == Kind::SyntaxList) {
			// find the first named syntax list element and use its' name &
			// modifiers
			for (Node* c : n->as<SyntaxList>()->Children) {
				if (c->name() != nullptr) {
					name = c->name()->clone(*factory()->asNodeFactory());
					if (c->modifiers() != nullptr) {
						modifiers = c->modifiers()->clone(
							*factory()->asNodeFactory());
					}
					break;
				}
			}
			host = n->as<SyntaxList>()->Children;
		} else if (n != nullptr) {
			name = n->name()->clone(*factory()->asNodeFactory());
			if (n->modifiers() != nullptr) {
				modifiers =
					n->modifiers()->clone(*factory()->asNodeFactory());
			}
			host = {n};
		}
		if (name != nullptr) {
			Node* moduleDecl = factory()->newModuleDeclaration(
				modifiers, Kind::NamespaceKeyword, name, nullptr,
				factory()->newModuleBlock(factory()->newNodeList(addOns)));
			host.push_back(moduleDecl);
			return factory()->newSyntaxList(host);
		}
	}
	return n;
}

// transform.go:2987 tryGetPropertyName
std::string DeclarationTransformerImpl::tryGetPropertyName(Node* node) {
	if (isElementAccessExpression(node)) {
		return resolver->GetElementAccessExpressionName(
			node->as<ElementAccessExpression>());
	}
	if (isPropertyAccessExpression(node)) {
		return node->name()->text();
	}
	return "";
}
} // namespace tsc::transformers::declarations
