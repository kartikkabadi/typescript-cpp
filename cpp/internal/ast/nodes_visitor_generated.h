// Clone + visitEachChild kind dispatch and SourceFile::copyFrom,
// generated from ast_generated.go + ast.go by gencpp.py. DO NOT EDIT.
#pragma once

namespace tsc {

inline void SourceFile::copyFrom(SourceFile* other) {
	// Do not copy fields set by newSourceFile (text, fileName,
	// parseOptions, statements).
	if (other->contentMapperInfo != nullptr) {
		contentMapperInfo = other->contentMapperInfo;
	}
	LanguageVariant = other->LanguageVariant;
	ScriptKind = other->ScriptKind;
	IsDeclarationFile = other->IsDeclarationFile;
	UsesUriStyleNodeCoreModules = other->UsesUriStyleNodeCoreModules;
	imports = other->imports;
	ModuleAugmentations = other->ModuleAugmentations;
	AmbientModuleNames = other->AmbientModuleNames;
	CommentDirectives = other->CommentDirectives;
	Pragmas = other->Pragmas;
	ReferencedFiles = other->ReferencedFiles;
	TypeReferenceDirectives = other->TypeReferenceDirectives;
	LibReferenceDirectives = other->LibReferenceDirectives;
	CommonJSModuleIndicator = other->CommonJSModuleIndicator;
	ExternalModuleIndicator = other->ExternalModuleIndicator;
	flags |= other->flags;
}

inline Node* Node::clone(NodeFactory& f) {
	switch (kind) {
		case Kind::ArrayLiteralExpression:
		{
			auto* n = static_cast<ArrayLiteralExpression*>(this);
			return cloneNode(f.newArrayLiteralExpression(n->Elements, n->MultiLine), n, f.hooks);
		}
		case Kind::ArrayType:
		{
			auto* n = static_cast<ArrayTypeNode*>(this);
			return cloneNode(f.newArrayTypeNode(n->ElementType), n, f.hooks);
		}
		case Kind::ArrowFunction:
		{
			auto* n = static_cast<ArrowFunction*>(this);
			return cloneNode(f.newArrowFunction(n->Node::modifiers(), n->TypeParameters, n->Parameters, n->Type, n->FullSignature, n->EqualsGreaterThanToken, n->Body), n, f.hooks);
		}
		case Kind::AsExpression:
		{
			auto* n = static_cast<AsExpression*>(this);
			return cloneNode(f.newAsExpression(n->Expression, n->Type), n, f.hooks);
		}
		case Kind::AwaitExpression:
		{
			auto* n = static_cast<AwaitExpression*>(this);
			return cloneNode(f.newAwaitExpression(n->Expression), n, f.hooks);
		}
		case Kind::BigIntLiteral:
		{
			auto* n = static_cast<BigIntLiteral*>(this);
			return cloneNode(f.newBigIntLiteral(n->Text, n->TokenFlags), n, f.hooks);
		}
		case Kind::BinaryExpression:
		{
			auto* n = static_cast<BinaryExpression*>(this);
			return cloneNode(f.newBinaryExpression(n->Node::modifiers(), n->Left, n->Type, n->OperatorToken, n->Right), n, f.hooks);
		}
		case Kind::BindingElement:
		{
			auto* n = static_cast<BindingElement*>(this);
			return cloneNode(f.newBindingElement(n->DotDotDotToken, n->PropertyName, n->name, n->Initializer), n, f.hooks);
		}
		case Kind::ObjectBindingPattern:
		case Kind::ArrayBindingPattern:
		{
			auto* n = static_cast<BindingPattern*>(this);
			return cloneNode(f.newBindingPattern(n->kind, n->Elements), n, f.hooks);
		}
		case Kind::Block:
		{
			auto* n = static_cast<Block*>(this);
			return cloneNode(f.newBlock(n->Statements, n->MultiLine), n, f.hooks);
		}
		case Kind::BreakStatement:
		{
			auto* n = static_cast<BreakStatement*>(this);
			return cloneNode(f.newBreakStatement(n->Label), n, f.hooks);
		}
		case Kind::CallExpression:
		{
			auto* n = static_cast<CallExpression*>(this);
			return cloneNode(f.newCallExpression(n->Expression, n->QuestionDotToken, n->TypeArguments, n->Arguments, n->flags), n, f.hooks);
		}
		case Kind::CallSignature:
		{
			auto* n = static_cast<CallSignatureDeclaration*>(this);
			return cloneNode(f.newCallSignatureDeclaration(n->TypeParameters, n->Parameters, n->Type), n, f.hooks);
		}
		case Kind::CaseBlock:
		{
			auto* n = static_cast<CaseBlock*>(this);
			return cloneNode(f.newCaseBlock(n->Clauses), n, f.hooks);
		}
		case Kind::CaseClause:
		case Kind::DefaultClause:
		{
			auto* n = static_cast<CaseOrDefaultClause*>(this);
			return cloneNode(f.newCaseOrDefaultClause(n->kind, n->Expression, n->Statements), n, f.hooks);
		}
		case Kind::CatchClause:
		{
			auto* n = static_cast<CatchClause*>(this);
			return cloneNode(f.newCatchClause(n->VariableDeclaration, n->Block), n, f.hooks);
		}
		case Kind::ClassDeclaration:
		{
			auto* n = static_cast<ClassDeclaration*>(this);
			return cloneNode(f.newClassDeclaration(n->Node::modifiers(), n->name, n->TypeParameters, n->HeritageClauses, n->Members), n, f.hooks);
		}
		case Kind::ClassExpression:
		{
			auto* n = static_cast<ClassExpression*>(this);
			return cloneNode(f.newClassExpression(n->Node::modifiers(), n->name, n->TypeParameters, n->HeritageClauses, n->Members), n, f.hooks);
		}
		case Kind::ClassStaticBlockDeclaration:
		{
			auto* n = static_cast<ClassStaticBlockDeclaration*>(this);
			return cloneNode(f.newClassStaticBlockDeclaration(n->Node::modifiers(), n->Body), n, f.hooks);
		}
		case Kind::ComputedPropertyName:
		{
			auto* n = static_cast<ComputedPropertyName*>(this);
			return cloneNode(f.newComputedPropertyName(n->Expression), n, f.hooks);
		}
		case Kind::ConditionalExpression:
		{
			auto* n = static_cast<ConditionalExpression*>(this);
			return cloneNode(f.newConditionalExpression(n->Condition, n->QuestionToken, n->WhenTrue, n->ColonToken, n->WhenFalse), n, f.hooks);
		}
		case Kind::ConditionalType:
		{
			auto* n = static_cast<ConditionalTypeNode*>(this);
			return cloneNode(f.newConditionalTypeNode(n->CheckType, n->ExtendsType, n->TrueType, n->FalseType), n, f.hooks);
		}
		case Kind::ConstructSignature:
		{
			auto* n = static_cast<ConstructSignatureDeclaration*>(this);
			return cloneNode(f.newConstructSignatureDeclaration(n->TypeParameters, n->Parameters, n->Type), n, f.hooks);
		}
		case Kind::Constructor:
		{
			auto* n = static_cast<ConstructorDeclaration*>(this);
			return cloneNode(f.newConstructorDeclaration(n->Node::modifiers(), n->TypeParameters, n->Parameters, n->Type, n->FullSignature, n->Body), n, f.hooks);
		}
		case Kind::ConstructorType:
		{
			auto* n = static_cast<ConstructorTypeNode*>(this);
			return cloneNode(f.newConstructorTypeNode(n->Node::modifiers(), n->TypeParameters, n->Parameters, n->Type), n, f.hooks);
		}
		case Kind::ContinueStatement:
		{
			auto* n = static_cast<ContinueStatement*>(this);
			return cloneNode(f.newContinueStatement(n->Label), n, f.hooks);
		}
		case Kind::DebuggerStatement:
		{
			auto* n = static_cast<DebuggerStatement*>(this);
			return cloneNode(f.newDebuggerStatement(), n, f.hooks);
		}
		case Kind::Decorator:
		{
			auto* n = static_cast<Decorator*>(this);
			return cloneNode(f.newDecorator(n->Expression), n, f.hooks);
		}
		case Kind::DeleteExpression:
		{
			auto* n = static_cast<DeleteExpression*>(this);
			return cloneNode(f.newDeleteExpression(n->Expression), n, f.hooks);
		}
		case Kind::DoStatement:
		{
			auto* n = static_cast<DoStatement*>(this);
			return cloneNode(f.newDoStatement(n->Statement, n->Expression), n, f.hooks);
		}
		case Kind::ElementAccessExpression:
		{
			auto* n = static_cast<ElementAccessExpression*>(this);
			return cloneNode(f.newElementAccessExpression(n->Expression, n->QuestionDotToken, n->ArgumentExpression, n->flags), n, f.hooks);
		}
		case Kind::EmptyStatement:
		{
			auto* n = static_cast<EmptyStatement*>(this);
			return cloneNode(f.newEmptyStatement(), n, f.hooks);
		}
		case Kind::EnumDeclaration:
		{
			auto* n = static_cast<EnumDeclaration*>(this);
			return cloneNode(f.newEnumDeclaration(n->Node::modifiers(), n->name, n->Members), n, f.hooks);
		}
		case Kind::EnumMember:
		{
			auto* n = static_cast<EnumMember*>(this);
			return cloneNode(f.newEnumMember(n->name, n->Initializer), n, f.hooks);
		}
		case Kind::ExportAssignment:
		{
			auto* n = static_cast<ExportAssignment*>(this);
			return cloneNode(f.newExportAssignment(n->Node::modifiers(), n->IsExportEquals, n->Type, n->Expression), n, f.hooks);
		}
		case Kind::ExportDeclaration:
		{
			auto* n = static_cast<ExportDeclaration*>(this);
			return cloneNode(f.newExportDeclaration(n->Node::modifiers(), n->IsTypeOnly, n->ExportClause, n->ModuleSpecifier, n->Attributes), n, f.hooks);
		}
		case Kind::ExportSpecifier:
		{
			auto* n = static_cast<ExportSpecifier*>(this);
			return cloneNode(f.newExportSpecifier(n->IsTypeOnly, n->PropertyName, n->name), n, f.hooks);
		}
		case Kind::ExpressionStatement:
		{
			auto* n = static_cast<ExpressionStatement*>(this);
			return cloneNode(f.newExpressionStatement(n->Expression), n, f.hooks);
		}
		case Kind::ExpressionWithTypeArguments:
		{
			auto* n = static_cast<ExpressionWithTypeArguments*>(this);
			return cloneNode(f.newExpressionWithTypeArguments(n->Expression, n->TypeArguments), n, f.hooks);
		}
		case Kind::ExternalModuleReference:
		{
			auto* n = static_cast<ExternalModuleReference*>(this);
			return cloneNode(f.newExternalModuleReference(n->Expression), n, f.hooks);
		}
		case Kind::ForOfStatement:
		case Kind::ForInStatement:
		{
			auto* n = static_cast<ForInOrOfStatement*>(this);
			return cloneNode(f.newForInOrOfStatement(n->kind, n->AwaitModifier, n->Initializer, n->Expression, n->Statement), n, f.hooks);
		}
		case Kind::ForStatement:
		{
			auto* n = static_cast<ForStatement*>(this);
			return cloneNode(f.newForStatement(n->Initializer, n->Condition, n->Incrementor, n->Statement), n, f.hooks);
		}
		case Kind::FunctionDeclaration:
		{
			auto* n = static_cast<FunctionDeclaration*>(this);
			return cloneNode(f.newFunctionDeclaration(n->Node::modifiers(), n->AsteriskToken, n->name, n->TypeParameters, n->Parameters, n->Type, n->FullSignature, n->Body), n, f.hooks);
		}
		case Kind::FunctionExpression:
		{
			auto* n = static_cast<FunctionExpression*>(this);
			return cloneNode(f.newFunctionExpression(n->Node::modifiers(), n->AsteriskToken, n->name, n->TypeParameters, n->Parameters, n->Type, n->FullSignature, n->Body), n, f.hooks);
		}
		case Kind::FunctionType:
		{
			auto* n = static_cast<FunctionTypeNode*>(this);
			return cloneNode(f.newFunctionTypeNode(n->TypeParameters, n->Parameters, n->Type), n, f.hooks);
		}
		case Kind::GetAccessor:
		{
			auto* n = static_cast<GetAccessorDeclaration*>(this);
			return cloneNode(f.newGetAccessorDeclaration(n->Node::modifiers(), n->name, n->TypeParameters, n->Parameters, n->Type, n->FullSignature, n->Body), n, f.hooks);
		}
		case Kind::HeritageClause:
		{
			auto* n = static_cast<HeritageClause*>(this);
			return cloneNode(f.newHeritageClause(n->Token, n->Types), n, f.hooks);
		}
		case Kind::Identifier:
		{
			auto* n = static_cast<Identifier*>(this);
			return cloneNode(f.newIdentifier(n->Text), n, f.hooks);
		}
		case Kind::IfStatement:
		{
			auto* n = static_cast<IfStatement*>(this);
			return cloneNode(f.newIfStatement(n->Expression, n->ThenStatement, n->ElseStatement), n, f.hooks);
		}
		case Kind::ImportAttribute:
		{
			auto* n = static_cast<ImportAttribute*>(this);
			return cloneNode(f.newImportAttribute(n->name, n->Value), n, f.hooks);
		}
		case Kind::ImportAttributes:
		{
			auto* n = static_cast<ImportAttributes*>(this);
			return cloneNode(f.newImportAttributes(n->Token, n->Attributes, n->MultiLine), n, f.hooks);
		}
		case Kind::ImportClause:
		{
			auto* n = static_cast<ImportClause*>(this);
			return cloneNode(f.newImportClause(n->PhaseModifier, n->name, n->NamedBindings), n, f.hooks);
		}
		case Kind::ImportDeclaration:
		case Kind::JSImportDeclaration:
		{
			auto* n = static_cast<ImportDeclaration*>(this);
			switch (kind) {
			case Kind::ImportDeclaration:
				return cloneNode(f.newImportDeclaration(n->Node::modifiers(), n->ImportClause, n->ModuleSpecifier, n->Attributes), n, f.hooks);
			case Kind::JSImportDeclaration:
				return cloneNode(f.newJSImportDeclaration(n->Node::modifiers(), n->ImportClause, n->ModuleSpecifier, n->Attributes), n, f.hooks);
			default:
				TSC_UNREACHABLE("unexpected kind in ImportDeclaration::clone");
			}
		}
		case Kind::ImportEqualsDeclaration:
		{
			auto* n = static_cast<ImportEqualsDeclaration*>(this);
			return cloneNode(f.newImportEqualsDeclaration(n->Node::modifiers(), n->IsTypeOnly, n->name, n->ModuleReference), n, f.hooks);
		}
		case Kind::ImportSpecifier:
		{
			auto* n = static_cast<ImportSpecifier*>(this);
			return cloneNode(f.newImportSpecifier(n->IsTypeOnly, n->PropertyName, n->name), n, f.hooks);
		}
		case Kind::ImportType:
		{
			auto* n = static_cast<ImportTypeNode*>(this);
			return cloneNode(f.newImportTypeNode(n->IsTypeOf, n->Argument, n->Attributes, n->Qualifier, n->TypeArguments), n, f.hooks);
		}
		case Kind::IndexSignature:
		{
			auto* n = static_cast<IndexSignatureDeclaration*>(this);
			return cloneNode(f.newIndexSignatureDeclaration(n->Node::modifiers(), n->Parameters, n->Type), n, f.hooks);
		}
		case Kind::IndexedAccessType:
		{
			auto* n = static_cast<IndexedAccessTypeNode*>(this);
			return cloneNode(f.newIndexedAccessTypeNode(n->ObjectType, n->IndexType), n, f.hooks);
		}
		case Kind::InferType:
		{
			auto* n = static_cast<InferTypeNode*>(this);
			return cloneNode(f.newInferTypeNode(n->TypeParameter), n, f.hooks);
		}
		case Kind::InterfaceDeclaration:
		{
			auto* n = static_cast<InterfaceDeclaration*>(this);
			return cloneNode(f.newInterfaceDeclaration(n->Node::modifiers(), n->name, n->TypeParameters, n->HeritageClauses, n->Members), n, f.hooks);
		}
		case Kind::IntersectionType:
		{
			auto* n = static_cast<IntersectionTypeNode*>(this);
			return cloneNode(f.newIntersectionTypeNode(n->Types), n, f.hooks);
		}
		case Kind::JSDoc:
		{
			auto* n = static_cast<JSDoc*>(this);
			return cloneNode(f.newJSDoc(n->Comment, n->Tags), n, f.hooks);
		}
		case Kind::JSDocAllType:
		{
			auto* n = static_cast<JSDocAllType*>(this);
			return cloneNode(f.newJSDocAllType(), n, f.hooks);
		}
		case Kind::JSDocAugmentsTag:
		{
			auto* n = static_cast<JSDocAugmentsTag*>(this);
			return cloneNode(f.newJSDocAugmentsTag(n->TagName, n->ClassName, n->Comment), n, f.hooks);
		}
		case Kind::JSDocCallbackTag:
		{
			auto* n = static_cast<JSDocCallbackTag*>(this);
			return cloneNode(f.newJSDocCallbackTag(n->TagName, n->TypeExpression, n->name, n->Comment), n, f.hooks);
		}
		case Kind::JSDocDeprecatedTag:
		{
			auto* n = static_cast<JSDocDeprecatedTag*>(this);
			return cloneNode(f.newJSDocDeprecatedTag(n->TagName, n->Comment), n, f.hooks);
		}
		case Kind::JSDocImplementsTag:
		{
			auto* n = static_cast<JSDocImplementsTag*>(this);
			return cloneNode(f.newJSDocImplementsTag(n->TagName, n->ClassName, n->Comment), n, f.hooks);
		}
		case Kind::JSDocImportTag:
		{
			auto* n = static_cast<JSDocImportTag*>(this);
			return cloneNode(f.newJSDocImportTag(n->TagName, n->ImportClause, n->ModuleSpecifier, n->Attributes, n->Comment), n, f.hooks);
		}
		case Kind::JSDocLink:
		{
			auto* n = static_cast<JSDocLink*>(this);
			return cloneNode(f.newJSDocLink(n->name, n->text), n, f.hooks);
		}
		case Kind::JSDocLinkCode:
		{
			auto* n = static_cast<JSDocLinkCode*>(this);
			return cloneNode(f.newJSDocLinkCode(n->name, n->text), n, f.hooks);
		}
		case Kind::JSDocLinkPlain:
		{
			auto* n = static_cast<JSDocLinkPlain*>(this);
			return cloneNode(f.newJSDocLinkPlain(n->name, n->text), n, f.hooks);
		}
		case Kind::JSDocNameReference:
		{
			auto* n = static_cast<JSDocNameReference*>(this);
			return cloneNode(f.newJSDocNameReference(n->name), n, f.hooks);
		}
		case Kind::JSDocNonNullableType:
		{
			auto* n = static_cast<JSDocNonNullableType*>(this);
			return cloneNode(f.newJSDocNonNullableType(n->Type), n, f.hooks);
		}
		case Kind::JSDocNullableType:
		{
			auto* n = static_cast<JSDocNullableType*>(this);
			return cloneNode(f.newJSDocNullableType(n->Type), n, f.hooks);
		}
		case Kind::JSDocOptionalType:
		{
			auto* n = static_cast<JSDocOptionalType*>(this);
			return cloneNode(f.newJSDocOptionalType(n->Type), n, f.hooks);
		}
		case Kind::JSDocOverloadTag:
		{
			auto* n = static_cast<JSDocOverloadTag*>(this);
			return cloneNode(f.newJSDocOverloadTag(n->TagName, n->TypeExpression, n->Comment), n, f.hooks);
		}
		case Kind::JSDocOverrideTag:
		{
			auto* n = static_cast<JSDocOverrideTag*>(this);
			return cloneNode(f.newJSDocOverrideTag(n->TagName, n->Comment), n, f.hooks);
		}
		case Kind::JSDocParameterTag:
		case Kind::JSDocPropertyTag:
		{
			auto* n = static_cast<JSDocParameterOrPropertyTag*>(this);
			return cloneNode(f.newJSDocParameterOrPropertyTag(n->kind, n->TagName, n->name, n->IsBracketed, n->TypeExpression, n->IsNameFirst, n->Comment), n, f.hooks);
		}
		case Kind::JSDocPrivateTag:
		{
			auto* n = static_cast<JSDocPrivateTag*>(this);
			return cloneNode(f.newJSDocPrivateTag(n->TagName, n->Comment), n, f.hooks);
		}
		case Kind::JSDocProtectedTag:
		{
			auto* n = static_cast<JSDocProtectedTag*>(this);
			return cloneNode(f.newJSDocProtectedTag(n->TagName, n->Comment), n, f.hooks);
		}
		case Kind::JSDocPublicTag:
		{
			auto* n = static_cast<JSDocPublicTag*>(this);
			return cloneNode(f.newJSDocPublicTag(n->TagName, n->Comment), n, f.hooks);
		}
		case Kind::JSDocReadonlyTag:
		{
			auto* n = static_cast<JSDocReadonlyTag*>(this);
			return cloneNode(f.newJSDocReadonlyTag(n->TagName, n->Comment), n, f.hooks);
		}
		case Kind::JSDocReturnTag:
		{
			auto* n = static_cast<JSDocReturnTag*>(this);
			return cloneNode(f.newJSDocReturnTag(n->TagName, n->TypeExpression, n->Comment), n, f.hooks);
		}
		case Kind::JSDocSatisfiesTag:
		{
			auto* n = static_cast<JSDocSatisfiesTag*>(this);
			return cloneNode(f.newJSDocSatisfiesTag(n->TagName, n->TypeExpression, n->Comment), n, f.hooks);
		}
		case Kind::JSDocSeeTag:
		{
			auto* n = static_cast<JSDocSeeTag*>(this);
			return cloneNode(f.newJSDocSeeTag(n->TagName, n->NameExpression, n->Comment), n, f.hooks);
		}
		case Kind::JSDocSignature:
		{
			auto* n = static_cast<JSDocSignature*>(this);
			return cloneNode(f.newJSDocSignature(n->TypeParameters, n->Parameters, n->Type), n, f.hooks);
		}
		case Kind::JSDocTemplateTag:
		{
			auto* n = static_cast<JSDocTemplateTag*>(this);
			return cloneNode(f.newJSDocTemplateTag(n->TagName, n->Constraint, n->TypeParameters, n->Comment), n, f.hooks);
		}
		case Kind::JSDocText:
		{
			auto* n = static_cast<JSDocText*>(this);
			return cloneNode(f.newJSDocText(n->text), n, f.hooks);
		}
		case Kind::JSDocThisTag:
		{
			auto* n = static_cast<JSDocThisTag*>(this);
			return cloneNode(f.newJSDocThisTag(n->TagName, n->TypeExpression, n->Comment), n, f.hooks);
		}
		case Kind::JSDocThrowsTag:
		{
			auto* n = static_cast<JSDocThrowsTag*>(this);
			return cloneNode(f.newJSDocThrowsTag(n->TagName, n->TypeExpression, n->Comment), n, f.hooks);
		}
		case Kind::JSDocTypeExpression:
		{
			auto* n = static_cast<JSDocTypeExpression*>(this);
			return cloneNode(f.newJSDocTypeExpression(n->Type), n, f.hooks);
		}
		case Kind::JSDocTypeLiteral:
		{
			auto* n = static_cast<JSDocTypeLiteral*>(this);
			return cloneNode(f.newJSDocTypeLiteral(n->JSDocPropertyTags, n->IsArrayType), n, f.hooks);
		}
		case Kind::JSDocTypeTag:
		{
			auto* n = static_cast<JSDocTypeTag*>(this);
			return cloneNode(f.newJSDocTypeTag(n->TagName, n->TypeExpression, n->Comment), n, f.hooks);
		}
		case Kind::JSDocTypedefTag:
		{
			auto* n = static_cast<JSDocTypedefTag*>(this);
			return cloneNode(f.newJSDocTypedefTag(n->TagName, n->TypeExpression, n->name, n->Comment), n, f.hooks);
		}
		case Kind::JSDocUnknownTag:
		{
			auto* n = static_cast<JSDocUnknownTag*>(this);
			return cloneNode(f.newJSDocUnknownTag(n->TagName, n->Comment), n, f.hooks);
		}
		case Kind::JSDocVariadicType:
		{
			auto* n = static_cast<JSDocVariadicType*>(this);
			return cloneNode(f.newJSDocVariadicType(n->Type), n, f.hooks);
		}
		case Kind::JsxAttribute:
		{
			auto* n = static_cast<JsxAttribute*>(this);
			return cloneNode(f.newJsxAttribute(n->name, n->Initializer), n, f.hooks);
		}
		case Kind::JsxAttributes:
		{
			auto* n = static_cast<JsxAttributes*>(this);
			return cloneNode(f.newJsxAttributes(n->Properties), n, f.hooks);
		}
		case Kind::JsxClosingElement:
		{
			auto* n = static_cast<JsxClosingElement*>(this);
			return cloneNode(f.newJsxClosingElement(n->TagName), n, f.hooks);
		}
		case Kind::JsxClosingFragment:
		{
			auto* n = static_cast<JsxClosingFragment*>(this);
			return cloneNode(f.newJsxClosingFragment(), n, f.hooks);
		}
		case Kind::JsxElement:
		{
			auto* n = static_cast<JsxElement*>(this);
			return cloneNode(f.newJsxElement(n->OpeningElement, n->Children, n->ClosingElement), n, f.hooks);
		}
		case Kind::JsxExpression:
		{
			auto* n = static_cast<JsxExpression*>(this);
			return cloneNode(f.newJsxExpression(n->DotDotDotToken, n->Expression), n, f.hooks);
		}
		case Kind::JsxFragment:
		{
			auto* n = static_cast<JsxFragment*>(this);
			return cloneNode(f.newJsxFragment(n->OpeningFragment, n->Children, n->ClosingFragment), n, f.hooks);
		}
		case Kind::JsxNamespacedName:
		{
			auto* n = static_cast<JsxNamespacedName*>(this);
			return cloneNode(f.newJsxNamespacedName(n->Namespace, n->name), n, f.hooks);
		}
		case Kind::JsxOpeningElement:
		{
			auto* n = static_cast<JsxOpeningElement*>(this);
			return cloneNode(f.newJsxOpeningElement(n->TagName, n->TypeArguments, n->Attributes), n, f.hooks);
		}
		case Kind::JsxOpeningFragment:
		{
			auto* n = static_cast<JsxOpeningFragment*>(this);
			return cloneNode(f.newJsxOpeningFragment(), n, f.hooks);
		}
		case Kind::JsxSelfClosingElement:
		{
			auto* n = static_cast<JsxSelfClosingElement*>(this);
			return cloneNode(f.newJsxSelfClosingElement(n->TagName, n->TypeArguments, n->Attributes), n, f.hooks);
		}
		case Kind::JsxSpreadAttribute:
		{
			auto* n = static_cast<JsxSpreadAttribute*>(this);
			return cloneNode(f.newJsxSpreadAttribute(n->Expression), n, f.hooks);
		}
		case Kind::JsxText:
		{
			auto* n = static_cast<JsxText*>(this);
			return cloneNode(f.newJsxText(n->Text, n->ContainsOnlyTriviaWhiteSpaces), n, f.hooks);
		}
		case Kind::ThisKeyword:
		case Kind::NullKeyword:
		case Kind::TrueKeyword:
		case Kind::FalseKeyword:
		case Kind::SuperKeyword:
		{
			auto* n = static_cast<KeywordExpression*>(this);
			return cloneNode(f.newKeywordExpression(n->kind), n, f.hooks);
		}
		case Kind::AnyKeyword:
		case Kind::UnknownKeyword:
		case Kind::UndefinedKeyword:
		case Kind::NeverKeyword:
		case Kind::StringKeyword:
		case Kind::NumberKeyword:
		case Kind::BigIntKeyword:
		case Kind::BooleanKeyword:
		case Kind::SymbolKeyword:
		case Kind::VoidKeyword:
		case Kind::ObjectKeyword:
		{
			auto* n = static_cast<KeywordTypeNode*>(this);
			return cloneNode(f.newKeywordTypeNode(n->kind), n, f.hooks);
		}
		case Kind::LabeledStatement:
		{
			auto* n = static_cast<LabeledStatement*>(this);
			return cloneNode(f.newLabeledStatement(n->Label, n->Statement), n, f.hooks);
		}
		case Kind::LiteralType:
		{
			auto* n = static_cast<LiteralTypeNode*>(this);
			return cloneNode(f.newLiteralTypeNode(n->Literal), n, f.hooks);
		}
		case Kind::MappedType:
		{
			auto* n = static_cast<MappedTypeNode*>(this);
			return cloneNode(f.newMappedTypeNode(n->ReadonlyToken, n->TypeParameter, n->NameType, n->QuestionToken, n->Type, n->Members), n, f.hooks);
		}
		case Kind::MetaProperty:
		{
			auto* n = static_cast<MetaProperty*>(this);
			return cloneNode(f.newMetaProperty(n->KeywordToken, n->name), n, f.hooks);
		}
		case Kind::MethodDeclaration:
		{
			auto* n = static_cast<MethodDeclaration*>(this);
			return cloneNode(f.newMethodDeclaration(n->Node::modifiers(), n->AsteriskToken, n->name, n->PostfixToken, n->TypeParameters, n->Parameters, n->Type, n->FullSignature, n->Body), n, f.hooks);
		}
		case Kind::MethodSignature:
		{
			auto* n = static_cast<MethodSignatureDeclaration*>(this);
			return cloneNode(f.newMethodSignatureDeclaration(n->Node::modifiers(), n->name, n->PostfixToken, n->TypeParameters, n->Parameters, n->Type), n, f.hooks);
		}
		case Kind::MissingDeclaration:
		{
			auto* n = static_cast<MissingDeclaration*>(this);
			return cloneNode(f.newMissingDeclaration(n->Node::modifiers()), n, f.hooks);
		}
		case Kind::ModuleBlock:
		{
			auto* n = static_cast<ModuleBlock*>(this);
			return cloneNode(f.newModuleBlock(n->Statements), n, f.hooks);
		}
		case Kind::ModuleDeclaration:
		{
			auto* n = static_cast<ModuleDeclaration*>(this);
			return cloneNode(f.newModuleDeclaration(n->Node::modifiers(), n->Keyword, n->name, n->Attributes, n->Body), n, f.hooks);
		}
		case Kind::NamedExports:
		{
			auto* n = static_cast<NamedExports*>(this);
			return cloneNode(f.newNamedExports(n->Elements), n, f.hooks);
		}
		case Kind::NamedImports:
		{
			auto* n = static_cast<NamedImports*>(this);
			return cloneNode(f.newNamedImports(n->Elements), n, f.hooks);
		}
		case Kind::NamedTupleMember:
		{
			auto* n = static_cast<NamedTupleMember*>(this);
			return cloneNode(f.newNamedTupleMember(n->DotDotDotToken, n->name, n->QuestionToken, n->Type), n, f.hooks);
		}
		case Kind::NamespaceExport:
		{
			auto* n = static_cast<NamespaceExport*>(this);
			return cloneNode(f.newNamespaceExport(n->name), n, f.hooks);
		}
		case Kind::NamespaceExportDeclaration:
		{
			auto* n = static_cast<NamespaceExportDeclaration*>(this);
			return cloneNode(f.newNamespaceExportDeclaration(n->Node::modifiers(), n->name), n, f.hooks);
		}
		case Kind::NamespaceImport:
		{
			auto* n = static_cast<NamespaceImport*>(this);
			return cloneNode(f.newNamespaceImport(n->name), n, f.hooks);
		}
		case Kind::NewExpression:
		{
			auto* n = static_cast<NewExpression*>(this);
			return cloneNode(f.newNewExpression(n->Expression, n->TypeArguments, n->Arguments), n, f.hooks);
		}
		case Kind::NoSubstitutionTemplateLiteral:
		{
			auto* n = static_cast<NoSubstitutionTemplateLiteral*>(this);
			return cloneNode(f.newNoSubstitutionTemplateLiteral(n->Text, n->TemplateFlags), n, f.hooks);
		}
		case Kind::NonNullExpression:
		{
			auto* n = static_cast<NonNullExpression*>(this);
			return cloneNode(f.newNonNullExpression(n->Expression, n->flags), n, f.hooks);
		}
		case Kind::NotEmittedStatement:
		{
			auto* n = static_cast<NotEmittedStatement*>(this);
			return cloneNode(f.newNotEmittedStatement(), n, f.hooks);
		}
		case Kind::NotEmittedTypeElement:
		{
			auto* n = static_cast<NotEmittedTypeElement*>(this);
			return cloneNode(f.newNotEmittedTypeElement(), n, f.hooks);
		}
		case Kind::NumericLiteral:
		{
			auto* n = static_cast<NumericLiteral*>(this);
			return cloneNode(f.newNumericLiteral(n->Text, n->TokenFlags), n, f.hooks);
		}
		case Kind::ObjectLiteralExpression:
		{
			auto* n = static_cast<ObjectLiteralExpression*>(this);
			return cloneNode(f.newObjectLiteralExpression(n->Properties, n->MultiLine), n, f.hooks);
		}
		case Kind::OmittedExpression:
		{
			auto* n = static_cast<OmittedExpression*>(this);
			return cloneNode(f.newOmittedExpression(), n, f.hooks);
		}
		case Kind::OptionalType:
		{
			auto* n = static_cast<OptionalTypeNode*>(this);
			return cloneNode(f.newOptionalTypeNode(n->Type), n, f.hooks);
		}
		case Kind::Parameter:
		{
			auto* n = static_cast<ParameterDeclaration*>(this);
			return cloneNode(f.newParameterDeclaration(n->Node::modifiers(), n->DotDotDotToken, n->name, n->QuestionToken, n->Type, n->Initializer), n, f.hooks);
		}
		case Kind::ParenthesizedExpression:
		{
			auto* n = static_cast<ParenthesizedExpression*>(this);
			return cloneNode(f.newParenthesizedExpression(n->Expression), n, f.hooks);
		}
		case Kind::ParenthesizedType:
		{
			auto* n = static_cast<ParenthesizedTypeNode*>(this);
			return cloneNode(f.newParenthesizedTypeNode(n->Type), n, f.hooks);
		}
		case Kind::PartiallyEmittedExpression:
		{
			auto* n = static_cast<PartiallyEmittedExpression*>(this);
			return cloneNode(f.newPartiallyEmittedExpression(n->Expression), n, f.hooks);
		}
		case Kind::PostfixUnaryExpression:
		{
			auto* n = static_cast<PostfixUnaryExpression*>(this);
			return cloneNode(f.newPostfixUnaryExpression(n->Operand, n->Operator), n, f.hooks);
		}
		case Kind::PrefixUnaryExpression:
		{
			auto* n = static_cast<PrefixUnaryExpression*>(this);
			return cloneNode(f.newPrefixUnaryExpression(n->Operator, n->Operand), n, f.hooks);
		}
		case Kind::PrivateIdentifier:
		{
			auto* n = static_cast<PrivateIdentifier*>(this);
			return cloneNode(f.newPrivateIdentifier(n->Text), n, f.hooks);
		}
		case Kind::PropertyAccessExpression:
		{
			auto* n = static_cast<PropertyAccessExpression*>(this);
			return cloneNode(f.newPropertyAccessExpression(n->Expression, n->QuestionDotToken, n->name, n->flags), n, f.hooks);
		}
		case Kind::PropertyAssignment:
		{
			auto* n = static_cast<PropertyAssignment*>(this);
			return cloneNode(f.newPropertyAssignment(n->Node::modifiers(), n->name, n->PostfixToken, n->Type, n->Initializer), n, f.hooks);
		}
		case Kind::PropertyDeclaration:
		{
			auto* n = static_cast<PropertyDeclaration*>(this);
			return cloneNode(f.newPropertyDeclaration(n->Node::modifiers(), n->name, n->PostfixToken, n->Type, n->Initializer), n, f.hooks);
		}
		case Kind::PropertySignature:
		{
			auto* n = static_cast<PropertySignatureDeclaration*>(this);
			return cloneNode(f.newPropertySignatureDeclaration(n->Node::modifiers(), n->name, n->PostfixToken, n->Type, n->Initializer), n, f.hooks);
		}
		case Kind::QualifiedName:
		{
			auto* n = static_cast<QualifiedName*>(this);
			return cloneNode(f.newQualifiedName(n->Left, n->Right), n, f.hooks);
		}
		case Kind::RegularExpressionLiteral:
		{
			auto* n = static_cast<RegularExpressionLiteral*>(this);
			return cloneNode(f.newRegularExpressionLiteral(n->Text, n->TokenFlags), n, f.hooks);
		}
		case Kind::RestType:
		{
			auto* n = static_cast<RestTypeNode*>(this);
			return cloneNode(f.newRestTypeNode(n->Type), n, f.hooks);
		}
		case Kind::ReturnStatement:
		{
			auto* n = static_cast<ReturnStatement*>(this);
			return cloneNode(f.newReturnStatement(n->Expression), n, f.hooks);
		}
		case Kind::SatisfiesExpression:
		{
			auto* n = static_cast<SatisfiesExpression*>(this);
			return cloneNode(f.newSatisfiesExpression(n->Expression, n->Type), n, f.hooks);
		}
		case Kind::SemicolonClassElement:
		{
			auto* n = static_cast<SemicolonClassElement*>(this);
			return cloneNode(f.newSemicolonClassElement(), n, f.hooks);
		}
		case Kind::SetAccessor:
		{
			auto* n = static_cast<SetAccessorDeclaration*>(this);
			return cloneNode(f.newSetAccessorDeclaration(n->Node::modifiers(), n->name, n->TypeParameters, n->Parameters, n->Type, n->FullSignature, n->Body), n, f.hooks);
		}
		case Kind::ShorthandPropertyAssignment:
		{
			auto* n = static_cast<ShorthandPropertyAssignment*>(this);
			return cloneNode(f.newShorthandPropertyAssignment(n->Node::modifiers(), n->name, n->PostfixToken, n->Type, n->EqualsToken, n->ObjectAssignmentInitializer), n, f.hooks);
		}
		case Kind::SourceFile:
		{
			auto* n = static_cast<SourceFile*>(this);
			auto* updated = f.newSourceFile(n->parseOptions, n->text, n->Statements, n->EndOfFileToken);
			updated->as<SourceFile>()->copyFrom(n);
			return cloneNode(updated, n, f.hooks);
		}
		case Kind::SpreadAssignment:
		{
			auto* n = static_cast<SpreadAssignment*>(this);
			return cloneNode(f.newSpreadAssignment(n->Expression), n, f.hooks);
		}
		case Kind::SpreadElement:
		{
			auto* n = static_cast<SpreadElement*>(this);
			return cloneNode(f.newSpreadElement(n->Expression), n, f.hooks);
		}
		case Kind::StringLiteral:
		{
			auto* n = static_cast<StringLiteral*>(this);
			return cloneNode(f.newStringLiteral(n->Text, n->TokenFlags), n, f.hooks);
		}
		case Kind::SwitchStatement:
		{
			auto* n = static_cast<SwitchStatement*>(this);
			return cloneNode(f.newSwitchStatement(n->Expression, n->CaseBlock), n, f.hooks);
		}
		case Kind::SyntaxList:
		{
			auto* n = static_cast<SyntaxList*>(this);
			return cloneNode(f.newSyntaxList(n->Children), n, f.hooks);
		}
		case Kind::SyntheticExpression:
		{
			auto* n = static_cast<SyntheticExpression*>(this);
			return cloneNode(f.newSyntheticExpression(n->Type, n->IsSpread, n->TupleNameSource), n, f.hooks);
		}
		case Kind::SyntheticReferenceExpression:
		{
			auto* n = static_cast<SyntheticReferenceExpression*>(this);
			return cloneNode(f.newSyntheticReferenceExpression(n->Expression, n->ThisArg), n, f.hooks);
		}
		case Kind::TaggedTemplateExpression:
		{
			auto* n = static_cast<TaggedTemplateExpression*>(this);
			return cloneNode(f.newTaggedTemplateExpression(n->Tag, n->QuestionDotToken, n->TypeArguments, n->Template, n->flags), n, f.hooks);
		}
		case Kind::TemplateExpression:
		{
			auto* n = static_cast<TemplateExpression*>(this);
			return cloneNode(f.newTemplateExpression(n->Head, n->TemplateSpans), n, f.hooks);
		}
		case Kind::TemplateHead:
		{
			auto* n = static_cast<TemplateHead*>(this);
			return cloneNode(f.newTemplateHead(n->Text, n->RawText, n->TemplateFlags), n, f.hooks);
		}
		case Kind::TemplateLiteralType:
		{
			auto* n = static_cast<TemplateLiteralTypeNode*>(this);
			return cloneNode(f.newTemplateLiteralTypeNode(n->Head, n->TemplateSpans), n, f.hooks);
		}
		case Kind::TemplateLiteralTypeSpan:
		{
			auto* n = static_cast<TemplateLiteralTypeSpan*>(this);
			return cloneNode(f.newTemplateLiteralTypeSpan(n->Type, n->Literal), n, f.hooks);
		}
		case Kind::TemplateMiddle:
		{
			auto* n = static_cast<TemplateMiddle*>(this);
			return cloneNode(f.newTemplateMiddle(n->Text, n->RawText, n->TemplateFlags), n, f.hooks);
		}
		case Kind::TemplateSpan:
		{
			auto* n = static_cast<TemplateSpan*>(this);
			return cloneNode(f.newTemplateSpan(n->Expression, n->Literal), n, f.hooks);
		}
		case Kind::TemplateTail:
		{
			auto* n = static_cast<TemplateTail*>(this);
			return cloneNode(f.newTemplateTail(n->Text, n->RawText, n->TemplateFlags), n, f.hooks);
		}
		case Kind::ThisType:
		{
			auto* n = static_cast<ThisTypeNode*>(this);
			return cloneNode(f.newThisTypeNode(), n, f.hooks);
		}
		case Kind::ThrowStatement:
		{
			auto* n = static_cast<ThrowStatement*>(this);
			return cloneNode(f.newThrowStatement(n->Expression), n, f.hooks);
		}
		case Kind::ExportKeyword:
		case Kind::DeclareKeyword:
		case Kind::EqualsEqualsEqualsToken:
		case Kind::QuestionToken:
		case Kind::ColonToken:
		case Kind::DotDotDotToken:
		case Kind::SemicolonToken:
		case Kind::AssertsKeyword:
		case Kind::CommaToken:
		case Kind::EndOfFile:
		case Kind::EqualsToken:
		case Kind::EqualsGreaterThanToken:
		case Kind::AmpersandAmpersandToken:
		case Kind::AsteriskToken:
		case Kind::QuestionQuestionToken:
		case Kind::BarBarToken:
		case Kind::ExclamationEqualsEqualsToken:
		case Kind::PlusToken:
		case Kind::InKeyword:
		case Kind::DotToken:
		case Kind::QuestionDotToken:
		case Kind::AbstractKeyword:
		case Kind::AccessorKeyword:
		case Kind::AmpersandAmpersandEqualsToken:
		case Kind::AmpersandEqualsToken:
		case Kind::AmpersandToken:
		case Kind::AsKeyword:
		case Kind::AssertKeyword:
		case Kind::AsteriskAsteriskEqualsToken:
		case Kind::AsteriskAsteriskToken:
		case Kind::AsteriskEqualsToken:
		case Kind::AsyncKeyword:
		case Kind::AtToken:
		case Kind::AwaitKeyword:
		case Kind::BacktickToken:
		case Kind::BarBarEqualsToken:
		case Kind::BarEqualsToken:
		case Kind::BarToken:
		case Kind::BreakKeyword:
		case Kind::CaretEqualsToken:
		case Kind::CaretToken:
		case Kind::CaseKeyword:
		case Kind::CatchKeyword:
		case Kind::ClassKeyword:
		case Kind::CloseBraceToken:
		case Kind::CloseBracketToken:
		case Kind::CloseParenToken:
		case Kind::ConflictMarkerTrivia:
		case Kind::ConstKeyword:
		case Kind::ConstructorKeyword:
		case Kind::ContinueKeyword:
		case Kind::Count:
		case Kind::DebuggerKeyword:
		case Kind::DefaultKeyword:
		case Kind::DeferKeyword:
		case Kind::DeleteKeyword:
		case Kind::DoKeyword:
		case Kind::ElseKeyword:
		case Kind::EnumKeyword:
		case Kind::EqualsEqualsToken:
		case Kind::ExclamationEqualsToken:
		case Kind::ExclamationToken:
		case Kind::ExtendsKeyword:
		case Kind::FinallyKeyword:
		case Kind::ForKeyword:
		case Kind::FromKeyword:
		case Kind::FunctionKeyword:
		case Kind::GetKeyword:
		case Kind::GlobalKeyword:
		case Kind::GreaterThanEqualsToken:
		case Kind::GreaterThanGreaterThanEqualsToken:
		case Kind::GreaterThanGreaterThanGreaterThanEqualsToken:
		case Kind::GreaterThanGreaterThanGreaterThanToken:
		case Kind::GreaterThanGreaterThanToken:
		case Kind::GreaterThanToken:
		case Kind::HashToken:
		case Kind::IfKeyword:
		case Kind::ImmediateKeyword:
		case Kind::ImplementsKeyword:
		case Kind::ImportKeyword:
		case Kind::InferKeyword:
		case Kind::InstanceOfKeyword:
		case Kind::InterfaceKeyword:
		case Kind::IntrinsicKeyword:
		case Kind::IsKeyword:
		case Kind::JSDocCommentTextToken:
		case Kind::JsxTextAllWhiteSpaces:
		case Kind::KeyOfKeyword:
		case Kind::LessThanEqualsToken:
		case Kind::LessThanLessThanEqualsToken:
		case Kind::LessThanLessThanToken:
		case Kind::LessThanSlashToken:
		case Kind::LessThanToken:
		case Kind::LetKeyword:
		case Kind::MinusEqualsToken:
		case Kind::MinusMinusToken:
		case Kind::MinusToken:
		case Kind::ModuleKeyword:
		case Kind::MultiLineCommentTrivia:
		case Kind::NamespaceKeyword:
		case Kind::NewKeyword:
		case Kind::NewLineTrivia:
		case Kind::NonTextFileMarkerTrivia:
		case Kind::OfKeyword:
		case Kind::OpenBraceToken:
		case Kind::OpenBracketToken:
		case Kind::OpenParenToken:
		case Kind::OutKeyword:
		case Kind::OverrideKeyword:
		case Kind::PackageKeyword:
		case Kind::PercentEqualsToken:
		case Kind::PercentToken:
		case Kind::PlusEqualsToken:
		case Kind::PlusPlusToken:
		case Kind::PrivateKeyword:
		case Kind::ProtectedKeyword:
		case Kind::PublicKeyword:
		case Kind::QuestionQuestionEqualsToken:
		case Kind::ReadonlyKeyword:
		case Kind::RequireKeyword:
		case Kind::ReturnKeyword:
		case Kind::SatisfiesKeyword:
		case Kind::SetKeyword:
		case Kind::SingleLineCommentTrivia:
		case Kind::SlashEqualsToken:
		case Kind::SlashToken:
		case Kind::StaticKeyword:
		case Kind::SwitchKeyword:
		case Kind::ThrowKeyword:
		case Kind::TildeToken:
		case Kind::TryKeyword:
		case Kind::TypeKeyword:
		case Kind::TypeOfKeyword:
		case Kind::UniqueKeyword:
		case Kind::Unknown:
		case Kind::UsingKeyword:
		case Kind::VarKeyword:
		case Kind::WhileKeyword:
		case Kind::WhitespaceTrivia:
		case Kind::WithKeyword:
		case Kind::YieldKeyword:
		{
			auto* n = static_cast<Token*>(this);
			return cloneNode(f.newToken(n->kind), n, f.hooks);
		}
		case Kind::TryStatement:
		{
			auto* n = static_cast<TryStatement*>(this);
			return cloneNode(f.newTryStatement(n->TryBlock, n->CatchClause, n->FinallyBlock), n, f.hooks);
		}
		case Kind::TupleType:
		{
			auto* n = static_cast<TupleTypeNode*>(this);
			return cloneNode(f.newTupleTypeNode(n->Elements), n, f.hooks);
		}
		case Kind::TypeAliasDeclaration:
		case Kind::JSTypeAliasDeclaration:
		{
			auto* n = static_cast<TypeAliasDeclaration*>(this);
			switch (kind) {
			case Kind::TypeAliasDeclaration:
				return cloneNode(f.newTypeAliasDeclaration(n->Node::modifiers(), n->name, n->TypeParameters, n->Type), n, f.hooks);
			case Kind::JSTypeAliasDeclaration:
				return cloneNode(f.newJSTypeAliasDeclaration(n->Node::modifiers(), n->name, n->TypeParameters, n->Type), n, f.hooks);
			default:
				TSC_UNREACHABLE("unexpected kind in TypeAliasDeclaration::clone");
			}
		}
		case Kind::TypeAssertionExpression:
		{
			auto* n = static_cast<TypeAssertion*>(this);
			return cloneNode(f.newTypeAssertion(n->Type, n->Expression), n, f.hooks);
		}
		case Kind::TypeLiteral:
		{
			auto* n = static_cast<TypeLiteralNode*>(this);
			return cloneNode(f.newTypeLiteralNode(n->Members), n, f.hooks);
		}
		case Kind::TypeOfExpression:
		{
			auto* n = static_cast<TypeOfExpression*>(this);
			return cloneNode(f.newTypeOfExpression(n->Expression), n, f.hooks);
		}
		case Kind::TypeOperator:
		{
			auto* n = static_cast<TypeOperatorNode*>(this);
			return cloneNode(f.newTypeOperatorNode(n->Operator, n->Type), n, f.hooks);
		}
		case Kind::TypeParameter:
		{
			auto* n = static_cast<TypeParameterDeclaration*>(this);
			return cloneNode(f.newTypeParameterDeclaration(n->Node::modifiers(), n->name, n->Constraint, n->Expression, n->DefaultType), n, f.hooks);
		}
		case Kind::TypePredicate:
		{
			auto* n = static_cast<TypePredicateNode*>(this);
			return cloneNode(f.newTypePredicateNode(n->AssertsModifier, n->ParameterName, n->Type), n, f.hooks);
		}
		case Kind::TypeQuery:
		{
			auto* n = static_cast<TypeQueryNode*>(this);
			return cloneNode(f.newTypeQueryNode(n->ExprName, n->TypeArguments), n, f.hooks);
		}
		case Kind::TypeReference:
		{
			auto* n = static_cast<TypeReferenceNode*>(this);
			return cloneNode(f.newTypeReferenceNode(n->TypeName, n->TypeArguments), n, f.hooks);
		}
		case Kind::UnionType:
		{
			auto* n = static_cast<UnionTypeNode*>(this);
			return cloneNode(f.newUnionTypeNode(n->Types), n, f.hooks);
		}
		case Kind::VariableDeclaration:
		{
			auto* n = static_cast<VariableDeclaration*>(this);
			return cloneNode(f.newVariableDeclaration(n->name, n->ExclamationToken, n->Type, n->Initializer), n, f.hooks);
		}
		case Kind::VariableDeclarationList:
		{
			auto* n = static_cast<VariableDeclarationList*>(this);
			return cloneNode(f.newVariableDeclarationList(n->Declarations, n->flags), n, f.hooks);
		}
		case Kind::VariableStatement:
		{
			auto* n = static_cast<VariableStatement*>(this);
			return cloneNode(f.newVariableStatement(n->Node::modifiers(), n->DeclarationList), n, f.hooks);
		}
		case Kind::VoidExpression:
		{
			auto* n = static_cast<VoidExpression*>(this);
			return cloneNode(f.newVoidExpression(n->Expression), n, f.hooks);
		}
		case Kind::WhileStatement:
		{
			auto* n = static_cast<WhileStatement*>(this);
			return cloneNode(f.newWhileStatement(n->Expression, n->Statement), n, f.hooks);
		}
		case Kind::WithStatement:
		{
			auto* n = static_cast<WithStatement*>(this);
			return cloneNode(f.newWithStatement(n->Expression, n->Statement), n, f.hooks);
		}
		case Kind::YieldExpression:
		{
			auto* n = static_cast<YieldExpression*>(this);
			return cloneNode(f.newYieldExpression(n->AsteriskToken, n->Expression), n, f.hooks);
		}
		default:
			return nullptr;
	}
}

inline Node* Node::visitEachChild(NodeVisitor& v) {
	switch (kind) {
		case Kind::ArrayLiteralExpression:
		{
			auto* n = static_cast<ArrayLiteralExpression*>(this);
			auto _c0 = v.visitNodesHooked(n->Elements);
			auto _c1 = n->MultiLine;
			return v.factory->updateArrayLiteralExpression(n, _c0, _c1);
		}
		case Kind::ArrayType:
		{
			auto* n = static_cast<ArrayTypeNode*>(this);
			return v.factory->updateArrayTypeNode(n, v.visitNodeHooked(n->ElementType));
		}
		case Kind::ArrowFunction:
		{
			auto* n = static_cast<ArrowFunction*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodesHooked(n->TypeParameters);
			auto _c2 = v.visitParametersHooked(n->Parameters);
			auto _c3 = v.visitNodeHooked(n->Type);
			auto _c4 = v.visitNodeHooked(n->FullSignature);
			auto _c5 = v.visitNodeHooked(n->EqualsGreaterThanToken);
			auto _c6 = v.visitFunctionBodyHooked(n->Body);
			return v.factory->updateArrowFunction(n, _c0, _c1, _c2, _c3, _c4, _c5, _c6);
		}
		case Kind::AsExpression:
		{
			auto* n = static_cast<AsExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitNodeHooked(n->Type);
			return v.factory->updateAsExpression(n, _c0, _c1);
		}
		case Kind::AwaitExpression:
		{
			auto* n = static_cast<AwaitExpression*>(this);
			return v.factory->updateAwaitExpression(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::BigIntLiteral:
		{
			return this;
		}
		case Kind::BinaryExpression:
		{
			auto* n = static_cast<BinaryExpression*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->Left);
			auto _c2 = v.visitNodeHooked(n->Type);
			auto _c3 = v.visitNodeHooked(n->OperatorToken);
			auto _c4 = v.visitNodeHooked(n->Right);
			return v.factory->updateBinaryExpression(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::BindingElement:
		{
			auto* n = static_cast<BindingElement*>(this);
			auto _c0 = v.visitNodeHooked(n->DotDotDotToken);
			auto _c1 = v.visitNodeHooked(n->PropertyName);
			auto _c2 = v.visitNodeHooked(n->name);
			auto _c3 = v.visitNodeHooked(n->Initializer);
			return v.factory->updateBindingElement(n, _c0, _c1, _c2, _c3);
		}
		case Kind::ObjectBindingPattern:
		case Kind::ArrayBindingPattern:
		{
			auto* n = static_cast<BindingPattern*>(this);
			return v.factory->updateBindingPattern(n, v.visitNodesHooked(n->Elements));
		}
		case Kind::Block:
		{
			auto* n = static_cast<Block*>(this);
			auto _c0 = v.visitNodesHooked(n->Statements);
			auto _c1 = n->MultiLine;
			return v.factory->updateBlock(n, _c0, _c1);
		}
		case Kind::BreakStatement:
		{
			auto* n = static_cast<BreakStatement*>(this);
			return v.factory->updateBreakStatement(n, v.visitNodeHooked(n->Label));
		}
		case Kind::CallExpression:
		{
			auto* n = static_cast<CallExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitNodeHooked(n->QuestionDotToken);
			auto _c2 = v.visitNodesHooked(n->TypeArguments);
			auto _c3 = v.visitNodesHooked(n->Arguments);
			auto _c4 = n->flags;
			return v.factory->updateCallExpression(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::CallSignature:
		{
			auto* n = static_cast<CallSignatureDeclaration*>(this);
			auto _c0 = v.visitNodesHooked(n->TypeParameters);
			auto _c1 = v.visitNodesHooked(n->Parameters);
			auto _c2 = v.visitNodeHooked(n->Type);
			return v.factory->updateCallSignatureDeclaration(n, _c0, _c1, _c2);
		}
		case Kind::CaseBlock:
		{
			auto* n = static_cast<CaseBlock*>(this);
			return v.factory->updateCaseBlock(n, v.visitNodesHooked(n->Clauses));
		}
		case Kind::CaseClause:
		case Kind::DefaultClause:
		{
			auto* n = static_cast<CaseOrDefaultClause*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitNodesHooked(n->Statements);
			return v.factory->updateCaseOrDefaultClause(n, _c0, _c1);
		}
		case Kind::CatchClause:
		{
			auto* n = static_cast<CatchClause*>(this);
			auto _c0 = v.visitNodeHooked(n->VariableDeclaration);
			auto _c1 = v.visitNodeHooked(n->Block);
			return v.factory->updateCatchClause(n, _c0, _c1);
		}
		case Kind::ClassDeclaration:
		{
			auto* n = static_cast<ClassDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodesHooked(n->TypeParameters);
			auto _c3 = v.visitNodesHooked(n->HeritageClauses);
			auto _c4 = v.visitNodesHooked(n->Members);
			return v.factory->updateClassDeclaration(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::ClassExpression:
		{
			auto* n = static_cast<ClassExpression*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodesHooked(n->TypeParameters);
			auto _c3 = v.visitNodesHooked(n->HeritageClauses);
			auto _c4 = v.visitNodesHooked(n->Members);
			return v.factory->updateClassExpression(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::ClassStaticBlockDeclaration:
		{
			auto* n = static_cast<ClassStaticBlockDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->Body);
			return v.factory->updateClassStaticBlockDeclaration(n, _c0, _c1);
		}
		case Kind::ComputedPropertyName:
		{
			auto* n = static_cast<ComputedPropertyName*>(this);
			return v.factory->updateComputedPropertyName(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::ConditionalExpression:
		{
			auto* n = static_cast<ConditionalExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->Condition);
			auto _c1 = v.visitNodeHooked(n->QuestionToken);
			auto _c2 = v.visitNodeHooked(n->WhenTrue);
			auto _c3 = v.visitNodeHooked(n->ColonToken);
			auto _c4 = v.visitNodeHooked(n->WhenFalse);
			return v.factory->updateConditionalExpression(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::ConditionalType:
		{
			auto* n = static_cast<ConditionalTypeNode*>(this);
			auto _c0 = v.visitNodeHooked(n->CheckType);
			auto _c1 = v.visitNodeHooked(n->ExtendsType);
			auto _c2 = v.visitNodeHooked(n->TrueType);
			auto _c3 = v.visitNodeHooked(n->FalseType);
			return v.factory->updateConditionalTypeNode(n, _c0, _c1, _c2, _c3);
		}
		case Kind::ConstructSignature:
		{
			auto* n = static_cast<ConstructSignatureDeclaration*>(this);
			auto _c0 = v.visitNodesHooked(n->TypeParameters);
			auto _c1 = v.visitNodesHooked(n->Parameters);
			auto _c2 = v.visitNodeHooked(n->Type);
			return v.factory->updateConstructSignatureDeclaration(n, _c0, _c1, _c2);
		}
		case Kind::Constructor:
		{
			auto* n = static_cast<ConstructorDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodesHooked(n->TypeParameters);
			auto _c2 = v.visitParametersHooked(n->Parameters);
			auto _c3 = v.visitNodeHooked(n->Type);
			auto _c4 = v.visitNodeHooked(n->FullSignature);
			auto _c5 = v.visitFunctionBodyHooked(n->Body);
			return v.factory->updateConstructorDeclaration(n, _c0, _c1, _c2, _c3, _c4, _c5);
		}
		case Kind::ConstructorType:
		{
			auto* n = static_cast<ConstructorTypeNode*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodesHooked(n->TypeParameters);
			auto _c2 = v.visitNodesHooked(n->Parameters);
			auto _c3 = v.visitNodeHooked(n->Type);
			return v.factory->updateConstructorTypeNode(n, _c0, _c1, _c2, _c3);
		}
		case Kind::ContinueStatement:
		{
			auto* n = static_cast<ContinueStatement*>(this);
			return v.factory->updateContinueStatement(n, v.visitNodeHooked(n->Label));
		}
		case Kind::DebuggerStatement:
		{
			return this;
		}
		case Kind::Decorator:
		{
			auto* n = static_cast<Decorator*>(this);
			return v.factory->updateDecorator(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::DeleteExpression:
		{
			auto* n = static_cast<DeleteExpression*>(this);
			return v.factory->updateDeleteExpression(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::DoStatement:
		{
			auto* n = static_cast<DoStatement*>(this);
			auto _c0 = v.visitIterationBodyHooked(n->Statement);
			auto _c1 = v.visitNodeHooked(n->Expression);
			return v.factory->updateDoStatement(n, _c0, _c1);
		}
		case Kind::ElementAccessExpression:
		{
			auto* n = static_cast<ElementAccessExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitNodeHooked(n->QuestionDotToken);
			auto _c2 = v.visitNodeHooked(n->ArgumentExpression);
			auto _c3 = n->flags;
			return v.factory->updateElementAccessExpression(n, _c0, _c1, _c2, _c3);
		}
		case Kind::EmptyStatement:
		{
			return this;
		}
		case Kind::EnumDeclaration:
		{
			auto* n = static_cast<EnumDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodesHooked(n->Members);
			return v.factory->updateEnumDeclaration(n, _c0, _c1, _c2);
		}
		case Kind::EnumMember:
		{
			auto* n = static_cast<EnumMember*>(this);
			auto _c0 = v.visitNodeHooked(n->name);
			auto _c1 = v.visitNodeHooked(n->Initializer);
			return v.factory->updateEnumMember(n, _c0, _c1);
		}
		case Kind::ExportAssignment:
		{
			auto* n = static_cast<ExportAssignment*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = n->IsExportEquals;
			auto _c2 = v.visitNodeHooked(n->Type);
			auto _c3 = v.visitNodeHooked(n->Expression);
			return v.factory->updateExportAssignment(n, _c0, _c1, _c2, _c3);
		}
		case Kind::ExportDeclaration:
		{
			auto* n = static_cast<ExportDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = n->IsTypeOnly;
			auto _c2 = v.visitNodeHooked(n->ExportClause);
			auto _c3 = v.visitNodeHooked(n->ModuleSpecifier);
			auto _c4 = v.visitNodeHooked(n->Attributes);
			return v.factory->updateExportDeclaration(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::ExportSpecifier:
		{
			auto* n = static_cast<ExportSpecifier*>(this);
			auto _c0 = n->IsTypeOnly;
			auto _c1 = v.visitNodeHooked(n->PropertyName);
			auto _c2 = v.visitNodeHooked(n->name);
			return v.factory->updateExportSpecifier(n, _c0, _c1, _c2);
		}
		case Kind::ExpressionStatement:
		{
			auto* n = static_cast<ExpressionStatement*>(this);
			return v.factory->updateExpressionStatement(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::ExpressionWithTypeArguments:
		{
			auto* n = static_cast<ExpressionWithTypeArguments*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitNodesHooked(n->TypeArguments);
			return v.factory->updateExpressionWithTypeArguments(n, _c0, _c1);
		}
		case Kind::ExternalModuleReference:
		{
			auto* n = static_cast<ExternalModuleReference*>(this);
			return v.factory->updateExternalModuleReference(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::ForOfStatement:
		case Kind::ForInStatement:
		{
			auto* n = static_cast<ForInOrOfStatement*>(this);
			auto _c0 = v.visitNodeHooked(n->AwaitModifier);
			auto _c1 = v.visitNodeHooked(n->Initializer);
			auto _c2 = v.visitNodeHooked(n->Expression);
			auto _c3 = v.visitIterationBodyHooked(n->Statement);
			return v.factory->updateForInOrOfStatement(n, _c0, _c1, _c2, _c3);
		}
		case Kind::ForStatement:
		{
			auto* n = static_cast<ForStatement*>(this);
			auto _c0 = v.visitNodeHooked(n->Initializer);
			auto _c1 = v.visitNodeHooked(n->Condition);
			auto _c2 = v.visitNodeHooked(n->Incrementor);
			auto _c3 = v.visitIterationBodyHooked(n->Statement);
			return v.factory->updateForStatement(n, _c0, _c1, _c2, _c3);
		}
		case Kind::FunctionDeclaration:
		{
			auto* n = static_cast<FunctionDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->AsteriskToken);
			auto _c2 = v.visitNodeHooked(n->name);
			auto _c3 = v.visitNodesHooked(n->TypeParameters);
			auto _c4 = v.visitParametersHooked(n->Parameters);
			auto _c5 = v.visitNodeHooked(n->Type);
			auto _c6 = v.visitNodeHooked(n->FullSignature);
			auto _c7 = v.visitFunctionBodyHooked(n->Body);
			return v.factory->updateFunctionDeclaration(n, _c0, _c1, _c2, _c3, _c4, _c5, _c6, _c7);
		}
		case Kind::FunctionExpression:
		{
			auto* n = static_cast<FunctionExpression*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->AsteriskToken);
			auto _c2 = v.visitNodeHooked(n->name);
			auto _c3 = v.visitNodesHooked(n->TypeParameters);
			auto _c4 = v.visitParametersHooked(n->Parameters);
			auto _c5 = v.visitNodeHooked(n->Type);
			auto _c6 = v.visitNodeHooked(n->FullSignature);
			auto _c7 = v.visitFunctionBodyHooked(n->Body);
			return v.factory->updateFunctionExpression(n, _c0, _c1, _c2, _c3, _c4, _c5, _c6, _c7);
		}
		case Kind::FunctionType:
		{
			auto* n = static_cast<FunctionTypeNode*>(this);
			auto _c0 = v.visitNodesHooked(n->TypeParameters);
			auto _c1 = v.visitNodesHooked(n->Parameters);
			auto _c2 = v.visitNodeHooked(n->Type);
			return v.factory->updateFunctionTypeNode(n, _c0, _c1, _c2);
		}
		case Kind::GetAccessor:
		{
			auto* n = static_cast<GetAccessorDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodesHooked(n->TypeParameters);
			auto _c3 = v.visitParametersHooked(n->Parameters);
			auto _c4 = v.visitNodeHooked(n->Type);
			auto _c5 = v.visitNodeHooked(n->FullSignature);
			auto _c6 = v.visitFunctionBodyHooked(n->Body);
			return v.factory->updateGetAccessorDeclaration(n, _c0, _c1, _c2, _c3, _c4, _c5, _c6);
		}
		case Kind::HeritageClause:
		{
			auto* n = static_cast<HeritageClause*>(this);
			auto _c0 = n->Token;
			auto _c1 = v.visitNodesHooked(n->Types);
			return v.factory->updateHeritageClause(n, _c0, _c1);
		}
		case Kind::Identifier:
		{
			return this;
		}
		case Kind::IfStatement:
		{
			auto* n = static_cast<IfStatement*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitEmbeddedStatementHooked(n->ThenStatement);
			auto _c2 = v.visitEmbeddedStatementHooked(n->ElseStatement);
			return v.factory->updateIfStatement(n, _c0, _c1, _c2);
		}
		case Kind::ImportAttribute:
		{
			auto* n = static_cast<ImportAttribute*>(this);
			auto _c0 = v.visitNodeHooked(n->name);
			auto _c1 = v.visitNodeHooked(n->Value);
			return v.factory->updateImportAttribute(n, _c0, _c1);
		}
		case Kind::ImportAttributes:
		{
			auto* n = static_cast<ImportAttributes*>(this);
			auto _c0 = n->Token;
			auto _c1 = v.visitNodesHooked(n->Attributes);
			auto _c2 = n->MultiLine;
			return v.factory->updateImportAttributes(n, _c0, _c1, _c2);
		}
		case Kind::ImportClause:
		{
			auto* n = static_cast<ImportClause*>(this);
			auto _c0 = n->PhaseModifier;
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodeHooked(n->NamedBindings);
			return v.factory->updateImportClause(n, _c0, _c1, _c2);
		}
		case Kind::ImportDeclaration:
		case Kind::JSImportDeclaration:
		{
			auto* n = static_cast<ImportDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->ImportClause);
			auto _c2 = v.visitNodeHooked(n->ModuleSpecifier);
			auto _c3 = v.visitNodeHooked(n->Attributes);
			return v.factory->updateImportDeclaration(n, _c0, _c1, _c2, _c3);
		}
		case Kind::ImportEqualsDeclaration:
		{
			auto* n = static_cast<ImportEqualsDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = n->IsTypeOnly;
			auto _c2 = v.visitNodeHooked(n->name);
			auto _c3 = v.visitNodeHooked(n->ModuleReference);
			return v.factory->updateImportEqualsDeclaration(n, _c0, _c1, _c2, _c3);
		}
		case Kind::ImportSpecifier:
		{
			auto* n = static_cast<ImportSpecifier*>(this);
			auto _c0 = n->IsTypeOnly;
			auto _c1 = v.visitNodeHooked(n->PropertyName);
			auto _c2 = v.visitNodeHooked(n->name);
			return v.factory->updateImportSpecifier(n, _c0, _c1, _c2);
		}
		case Kind::ImportType:
		{
			auto* n = static_cast<ImportTypeNode*>(this);
			auto _c0 = n->IsTypeOf;
			auto _c1 = v.visitNodeHooked(n->Argument);
			auto _c2 = v.visitNodeHooked(n->Attributes);
			auto _c3 = v.visitNodeHooked(n->Qualifier);
			auto _c4 = v.visitNodesHooked(n->TypeArguments);
			return v.factory->updateImportTypeNode(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::IndexSignature:
		{
			auto* n = static_cast<IndexSignatureDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodesHooked(n->Parameters);
			auto _c2 = v.visitNodeHooked(n->Type);
			return v.factory->updateIndexSignatureDeclaration(n, _c0, _c1, _c2);
		}
		case Kind::IndexedAccessType:
		{
			auto* n = static_cast<IndexedAccessTypeNode*>(this);
			auto _c0 = v.visitNodeHooked(n->ObjectType);
			auto _c1 = v.visitNodeHooked(n->IndexType);
			return v.factory->updateIndexedAccessTypeNode(n, _c0, _c1);
		}
		case Kind::InferType:
		{
			auto* n = static_cast<InferTypeNode*>(this);
			return v.factory->updateInferTypeNode(n, v.visitNodeHooked(n->TypeParameter));
		}
		case Kind::InterfaceDeclaration:
		{
			auto* n = static_cast<InterfaceDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodesHooked(n->TypeParameters);
			auto _c3 = v.visitNodesHooked(n->HeritageClauses);
			auto _c4 = v.visitNodesHooked(n->Members);
			return v.factory->updateInterfaceDeclaration(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::IntersectionType:
		{
			auto* n = static_cast<IntersectionTypeNode*>(this);
			return v.factory->updateIntersectionTypeNode(n, v.visitNodesHooked(n->Types));
		}
		case Kind::JSDoc:
		{
			auto* n = static_cast<JSDoc*>(this);
			auto _c0 = v.visitNodesHooked(n->Comment);
			auto _c1 = v.visitNodesHooked(n->Tags);
			return v.factory->updateJSDoc(n, _c0, _c1);
		}
		case Kind::JSDocAllType:
		{
			return this;
		}
		case Kind::JSDocAugmentsTag:
		{
			auto* n = static_cast<JSDocAugmentsTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->ClassName);
			auto _c2 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocAugmentsTag(n, _c0, _c1, _c2);
		}
		case Kind::JSDocCallbackTag:
		{
			auto* n = static_cast<JSDocCallbackTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->TypeExpression);
			auto _c2 = v.visitNodeHooked(n->name);
			auto _c3 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocCallbackTag(n, _c0, _c1, _c2, _c3);
		}
		case Kind::JSDocDeprecatedTag:
		{
			auto* n = static_cast<JSDocDeprecatedTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocDeprecatedTag(n, _c0, _c1);
		}
		case Kind::JSDocImplementsTag:
		{
			auto* n = static_cast<JSDocImplementsTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->ClassName);
			auto _c2 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocImplementsTag(n, _c0, _c1, _c2);
		}
		case Kind::JSDocImportTag:
		{
			auto* n = static_cast<JSDocImportTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->ImportClause);
			auto _c2 = v.visitNodeHooked(n->ModuleSpecifier);
			auto _c3 = v.visitNodeHooked(n->Attributes);
			auto _c4 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocImportTag(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::JSDocLink:
		{
			auto* n = static_cast<JSDocLink*>(this);
			auto _c0 = v.visitNodeHooked(n->name);
			auto _c1 = n->text;
			return v.factory->updateJSDocLink(n, _c0, _c1);
		}
		case Kind::JSDocLinkCode:
		{
			auto* n = static_cast<JSDocLinkCode*>(this);
			auto _c0 = v.visitNodeHooked(n->name);
			auto _c1 = n->text;
			return v.factory->updateJSDocLinkCode(n, _c0, _c1);
		}
		case Kind::JSDocLinkPlain:
		{
			auto* n = static_cast<JSDocLinkPlain*>(this);
			auto _c0 = v.visitNodeHooked(n->name);
			auto _c1 = n->text;
			return v.factory->updateJSDocLinkPlain(n, _c0, _c1);
		}
		case Kind::JSDocNameReference:
		{
			auto* n = static_cast<JSDocNameReference*>(this);
			return v.factory->updateJSDocNameReference(n, v.visitNodeHooked(n->name));
		}
		case Kind::JSDocNonNullableType:
		{
			auto* n = static_cast<JSDocNonNullableType*>(this);
			return v.factory->updateJSDocNonNullableType(n, v.visitNodeHooked(n->Type));
		}
		case Kind::JSDocNullableType:
		{
			auto* n = static_cast<JSDocNullableType*>(this);
			return v.factory->updateJSDocNullableType(n, v.visitNodeHooked(n->Type));
		}
		case Kind::JSDocOptionalType:
		{
			auto* n = static_cast<JSDocOptionalType*>(this);
			return v.factory->updateJSDocOptionalType(n, v.visitNodeHooked(n->Type));
		}
		case Kind::JSDocOverloadTag:
		{
			auto* n = static_cast<JSDocOverloadTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->TypeExpression);
			auto _c2 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocOverloadTag(n, _c0, _c1, _c2);
		}
		case Kind::JSDocOverrideTag:
		{
			auto* n = static_cast<JSDocOverrideTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocOverrideTag(n, _c0, _c1);
		}
		case Kind::JSDocParameterTag:
		case Kind::JSDocPropertyTag:
		{
			auto* n = static_cast<JSDocParameterOrPropertyTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = n->IsBracketed;
			auto _c3 = v.visitNodeHooked(n->TypeExpression);
			auto _c4 = n->IsNameFirst;
			auto _c5 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocParameterOrPropertyTag(n, _c0, _c1, _c2, _c3, _c4, _c5);
		}
		case Kind::JSDocPrivateTag:
		{
			auto* n = static_cast<JSDocPrivateTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocPrivateTag(n, _c0, _c1);
		}
		case Kind::JSDocProtectedTag:
		{
			auto* n = static_cast<JSDocProtectedTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocProtectedTag(n, _c0, _c1);
		}
		case Kind::JSDocPublicTag:
		{
			auto* n = static_cast<JSDocPublicTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocPublicTag(n, _c0, _c1);
		}
		case Kind::JSDocReadonlyTag:
		{
			auto* n = static_cast<JSDocReadonlyTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocReadonlyTag(n, _c0, _c1);
		}
		case Kind::JSDocReturnTag:
		{
			auto* n = static_cast<JSDocReturnTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->TypeExpression);
			auto _c2 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocReturnTag(n, _c0, _c1, _c2);
		}
		case Kind::JSDocSatisfiesTag:
		{
			auto* n = static_cast<JSDocSatisfiesTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->TypeExpression);
			auto _c2 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocSatisfiesTag(n, _c0, _c1, _c2);
		}
		case Kind::JSDocSeeTag:
		{
			auto* n = static_cast<JSDocSeeTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->NameExpression);
			auto _c2 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocSeeTag(n, _c0, _c1, _c2);
		}
		case Kind::JSDocSignature:
		{
			auto* n = static_cast<JSDocSignature*>(this);
			auto _c0 = v.visitNodesHooked(n->TypeParameters);
			auto _c1 = v.visitNodesHooked(n->Parameters);
			auto _c2 = v.visitNodeHooked(n->Type);
			return v.factory->updateJSDocSignature(n, _c0, _c1, _c2);
		}
		case Kind::JSDocTemplateTag:
		{
			auto* n = static_cast<JSDocTemplateTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->Constraint);
			auto _c2 = v.visitNodesHooked(n->TypeParameters);
			auto _c3 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocTemplateTag(n, _c0, _c1, _c2, _c3);
		}
		case Kind::JSDocText:
		{
			return this;
		}
		case Kind::JSDocThisTag:
		{
			auto* n = static_cast<JSDocThisTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->TypeExpression);
			auto _c2 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocThisTag(n, _c0, _c1, _c2);
		}
		case Kind::JSDocThrowsTag:
		{
			auto* n = static_cast<JSDocThrowsTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->TypeExpression);
			auto _c2 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocThrowsTag(n, _c0, _c1, _c2);
		}
		case Kind::JSDocTypeExpression:
		{
			auto* n = static_cast<JSDocTypeExpression*>(this);
			return v.factory->updateJSDocTypeExpression(n, v.visitNodeHooked(n->Type));
		}
		case Kind::JSDocTypeLiteral:
		{
			auto* n = static_cast<JSDocTypeLiteral*>(this);
			std::vector<Node*> jsdocPropertyTags;
			jsdocPropertyTags.reserve(n->JSDocPropertyTags.size());
			for (auto* c : n->JSDocPropertyTags) jsdocPropertyTags.push_back(v.visitNodeHooked(c));
			auto _c0 = std::move(jsdocPropertyTags);
			auto _c1 = n->IsArrayType;
			return v.factory->updateJSDocTypeLiteral(n, _c0, _c1);
		}
		case Kind::JSDocTypeTag:
		{
			auto* n = static_cast<JSDocTypeTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->TypeExpression);
			auto _c2 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocTypeTag(n, _c0, _c1, _c2);
		}
		case Kind::JSDocTypedefTag:
		{
			auto* n = static_cast<JSDocTypedefTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodeHooked(n->TypeExpression);
			auto _c2 = v.visitNodeHooked(n->name);
			auto _c3 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocTypedefTag(n, _c0, _c1, _c2, _c3);
		}
		case Kind::JSDocUnknownTag:
		{
			auto* n = static_cast<JSDocUnknownTag*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodesHooked(n->Comment);
			return v.factory->updateJSDocUnknownTag(n, _c0, _c1);
		}
		case Kind::JSDocVariadicType:
		{
			auto* n = static_cast<JSDocVariadicType*>(this);
			return v.factory->updateJSDocVariadicType(n, v.visitNodeHooked(n->Type));
		}
		case Kind::JsxAttribute:
		{
			auto* n = static_cast<JsxAttribute*>(this);
			auto _c0 = v.visitNodeHooked(n->name);
			auto _c1 = v.visitNodeHooked(n->Initializer);
			return v.factory->updateJsxAttribute(n, _c0, _c1);
		}
		case Kind::JsxAttributes:
		{
			auto* n = static_cast<JsxAttributes*>(this);
			return v.factory->updateJsxAttributes(n, v.visitNodesHooked(n->Properties));
		}
		case Kind::JsxClosingElement:
		{
			auto* n = static_cast<JsxClosingElement*>(this);
			return v.factory->updateJsxClosingElement(n, v.visitNodeHooked(n->TagName));
		}
		case Kind::JsxClosingFragment:
		{
			return this;
		}
		case Kind::JsxElement:
		{
			auto* n = static_cast<JsxElement*>(this);
			auto _c0 = v.visitNodeHooked(n->OpeningElement);
			auto _c1 = v.visitNodesHooked(n->Children);
			auto _c2 = v.visitNodeHooked(n->ClosingElement);
			return v.factory->updateJsxElement(n, _c0, _c1, _c2);
		}
		case Kind::JsxExpression:
		{
			auto* n = static_cast<JsxExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->DotDotDotToken);
			auto _c1 = v.visitNodeHooked(n->Expression);
			return v.factory->updateJsxExpression(n, _c0, _c1);
		}
		case Kind::JsxFragment:
		{
			auto* n = static_cast<JsxFragment*>(this);
			auto _c0 = v.visitNodeHooked(n->OpeningFragment);
			auto _c1 = v.visitNodesHooked(n->Children);
			auto _c2 = v.visitNodeHooked(n->ClosingFragment);
			return v.factory->updateJsxFragment(n, _c0, _c1, _c2);
		}
		case Kind::JsxNamespacedName:
		{
			auto* n = static_cast<JsxNamespacedName*>(this);
			auto _c0 = v.visitNodeHooked(n->Namespace);
			auto _c1 = v.visitNodeHooked(n->name);
			return v.factory->updateJsxNamespacedName(n, _c0, _c1);
		}
		case Kind::JsxOpeningElement:
		{
			auto* n = static_cast<JsxOpeningElement*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodesHooked(n->TypeArguments);
			auto _c2 = v.visitNodeHooked(n->Attributes);
			return v.factory->updateJsxOpeningElement(n, _c0, _c1, _c2);
		}
		case Kind::JsxOpeningFragment:
		{
			return this;
		}
		case Kind::JsxSelfClosingElement:
		{
			auto* n = static_cast<JsxSelfClosingElement*>(this);
			auto _c0 = v.visitNodeHooked(n->TagName);
			auto _c1 = v.visitNodesHooked(n->TypeArguments);
			auto _c2 = v.visitNodeHooked(n->Attributes);
			return v.factory->updateJsxSelfClosingElement(n, _c0, _c1, _c2);
		}
		case Kind::JsxSpreadAttribute:
		{
			auto* n = static_cast<JsxSpreadAttribute*>(this);
			return v.factory->updateJsxSpreadAttribute(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::JsxText:
		{
			return this;
		}
		case Kind::ThisKeyword:
		case Kind::NullKeyword:
		case Kind::TrueKeyword:
		case Kind::FalseKeyword:
		case Kind::SuperKeyword:
		{
			return this;
		}
		case Kind::AnyKeyword:
		case Kind::UnknownKeyword:
		case Kind::UndefinedKeyword:
		case Kind::NeverKeyword:
		case Kind::StringKeyword:
		case Kind::NumberKeyword:
		case Kind::BigIntKeyword:
		case Kind::BooleanKeyword:
		case Kind::SymbolKeyword:
		case Kind::VoidKeyword:
		case Kind::ObjectKeyword:
		{
			return this;
		}
		case Kind::LabeledStatement:
		{
			auto* n = static_cast<LabeledStatement*>(this);
			auto _c0 = v.visitNodeHooked(n->Label);
			auto _c1 = v.visitEmbeddedStatementHooked(n->Statement);
			return v.factory->updateLabeledStatement(n, _c0, _c1);
		}
		case Kind::LiteralType:
		{
			auto* n = static_cast<LiteralTypeNode*>(this);
			return v.factory->updateLiteralTypeNode(n, v.visitNodeHooked(n->Literal));
		}
		case Kind::MappedType:
		{
			auto* n = static_cast<MappedTypeNode*>(this);
			auto _c0 = v.visitNodeHooked(n->ReadonlyToken);
			auto _c1 = v.visitNodeHooked(n->TypeParameter);
			auto _c2 = v.visitNodeHooked(n->NameType);
			auto _c3 = v.visitNodeHooked(n->QuestionToken);
			auto _c4 = v.visitNodeHooked(n->Type);
			auto _c5 = v.visitNodesHooked(n->Members);
			return v.factory->updateMappedTypeNode(n, _c0, _c1, _c2, _c3, _c4, _c5);
		}
		case Kind::MetaProperty:
		{
			auto* n = static_cast<MetaProperty*>(this);
			auto _c0 = n->KeywordToken;
			auto _c1 = v.visitNodeHooked(n->name);
			return v.factory->updateMetaProperty(n, _c0, _c1);
		}
		case Kind::MethodDeclaration:
		{
			auto* n = static_cast<MethodDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->AsteriskToken);
			auto _c2 = v.visitNodeHooked(n->name);
			auto _c3 = v.visitNodeHooked(n->PostfixToken);
			auto _c4 = v.visitNodesHooked(n->TypeParameters);
			auto _c5 = v.visitParametersHooked(n->Parameters);
			auto _c6 = v.visitNodeHooked(n->Type);
			auto _c7 = v.visitNodeHooked(n->FullSignature);
			auto _c8 = v.visitFunctionBodyHooked(n->Body);
			return v.factory->updateMethodDeclaration(n, _c0, _c1, _c2, _c3, _c4, _c5, _c6, _c7, _c8);
		}
		case Kind::MethodSignature:
		{
			auto* n = static_cast<MethodSignatureDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodeHooked(n->PostfixToken);
			auto _c3 = v.visitNodesHooked(n->TypeParameters);
			auto _c4 = v.visitNodesHooked(n->Parameters);
			auto _c5 = v.visitNodeHooked(n->Type);
			return v.factory->updateMethodSignatureDeclaration(n, _c0, _c1, _c2, _c3, _c4, _c5);
		}
		case Kind::MissingDeclaration:
		{
			auto* n = static_cast<MissingDeclaration*>(this);
			return v.factory->updateMissingDeclaration(n, v.visitModifiersHooked(n->modifiers));
		}
		case Kind::ModuleBlock:
		{
			auto* n = static_cast<ModuleBlock*>(this);
			return v.factory->updateModuleBlock(n, v.visitNodesHooked(n->Statements));
		}
		case Kind::ModuleDeclaration:
		{
			auto* n = static_cast<ModuleDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = n->Keyword;
			auto _c2 = v.visitNodeHooked(n->name);
			auto _c3 = v.visitNodeHooked(n->Attributes);
			auto _c4 = v.visitNodeHooked(n->Body);
			return v.factory->updateModuleDeclaration(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::NamedExports:
		{
			auto* n = static_cast<NamedExports*>(this);
			return v.factory->updateNamedExports(n, v.visitNodesHooked(n->Elements));
		}
		case Kind::NamedImports:
		{
			auto* n = static_cast<NamedImports*>(this);
			return v.factory->updateNamedImports(n, v.visitNodesHooked(n->Elements));
		}
		case Kind::NamedTupleMember:
		{
			auto* n = static_cast<NamedTupleMember*>(this);
			auto _c0 = v.visitNodeHooked(n->DotDotDotToken);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodeHooked(n->QuestionToken);
			auto _c3 = v.visitNodeHooked(n->Type);
			return v.factory->updateNamedTupleMember(n, _c0, _c1, _c2, _c3);
		}
		case Kind::NamespaceExport:
		{
			auto* n = static_cast<NamespaceExport*>(this);
			return v.factory->updateNamespaceExport(n, v.visitNodeHooked(n->name));
		}
		case Kind::NamespaceExportDeclaration:
		{
			auto* n = static_cast<NamespaceExportDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			return v.factory->updateNamespaceExportDeclaration(n, _c0, _c1);
		}
		case Kind::NamespaceImport:
		{
			auto* n = static_cast<NamespaceImport*>(this);
			return v.factory->updateNamespaceImport(n, v.visitNodeHooked(n->name));
		}
		case Kind::NewExpression:
		{
			auto* n = static_cast<NewExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitNodesHooked(n->TypeArguments);
			auto _c2 = v.visitNodesHooked(n->Arguments);
			return v.factory->updateNewExpression(n, _c0, _c1, _c2);
		}
		case Kind::NoSubstitutionTemplateLiteral:
		{
			return this;
		}
		case Kind::NonNullExpression:
		{
			auto* n = static_cast<NonNullExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = n->flags;
			return v.factory->updateNonNullExpression(n, _c0, _c1);
		}
		case Kind::NotEmittedStatement:
		{
			return this;
		}
		case Kind::NotEmittedTypeElement:
		{
			return this;
		}
		case Kind::NumericLiteral:
		{
			return this;
		}
		case Kind::ObjectLiteralExpression:
		{
			auto* n = static_cast<ObjectLiteralExpression*>(this);
			auto _c0 = v.visitNodesHooked(n->Properties);
			auto _c1 = n->MultiLine;
			return v.factory->updateObjectLiteralExpression(n, _c0, _c1);
		}
		case Kind::OmittedExpression:
		{
			return this;
		}
		case Kind::OptionalType:
		{
			auto* n = static_cast<OptionalTypeNode*>(this);
			return v.factory->updateOptionalTypeNode(n, v.visitNodeHooked(n->Type));
		}
		case Kind::Parameter:
		{
			auto* n = static_cast<ParameterDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->DotDotDotToken);
			auto _c2 = v.visitNodeHooked(n->name);
			auto _c3 = v.visitNodeHooked(n->QuestionToken);
			auto _c4 = v.visitNodeHooked(n->Type);
			auto _c5 = v.visitNodeHooked(n->Initializer);
			return v.factory->updateParameterDeclaration(n, _c0, _c1, _c2, _c3, _c4, _c5);
		}
		case Kind::ParenthesizedExpression:
		{
			auto* n = static_cast<ParenthesizedExpression*>(this);
			return v.factory->updateParenthesizedExpression(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::ParenthesizedType:
		{
			auto* n = static_cast<ParenthesizedTypeNode*>(this);
			return v.factory->updateParenthesizedTypeNode(n, v.visitNodeHooked(n->Type));
		}
		case Kind::PartiallyEmittedExpression:
		{
			auto* n = static_cast<PartiallyEmittedExpression*>(this);
			return v.factory->updatePartiallyEmittedExpression(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::PostfixUnaryExpression:
		{
			auto* n = static_cast<PostfixUnaryExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->Operand);
			auto _c1 = n->Operator;
			return v.factory->updatePostfixUnaryExpression(n, _c0, _c1);
		}
		case Kind::PrefixUnaryExpression:
		{
			auto* n = static_cast<PrefixUnaryExpression*>(this);
			auto _c0 = n->Operator;
			auto _c1 = v.visitNodeHooked(n->Operand);
			return v.factory->updatePrefixUnaryExpression(n, _c0, _c1);
		}
		case Kind::PrivateIdentifier:
		{
			return this;
		}
		case Kind::PropertyAccessExpression:
		{
			auto* n = static_cast<PropertyAccessExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitNodeHooked(n->QuestionDotToken);
			auto _c2 = v.visitNodeHooked(n->name);
			auto _c3 = n->flags;
			return v.factory->updatePropertyAccessExpression(n, _c0, _c1, _c2, _c3);
		}
		case Kind::PropertyAssignment:
		{
			auto* n = static_cast<PropertyAssignment*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodeHooked(n->PostfixToken);
			auto _c3 = v.visitNodeHooked(n->Type);
			auto _c4 = v.visitNodeHooked(n->Initializer);
			return v.factory->updatePropertyAssignment(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::PropertyDeclaration:
		{
			auto* n = static_cast<PropertyDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodeHooked(n->PostfixToken);
			auto _c3 = v.visitNodeHooked(n->Type);
			auto _c4 = v.visitNodeHooked(n->Initializer);
			return v.factory->updatePropertyDeclaration(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::PropertySignature:
		{
			auto* n = static_cast<PropertySignatureDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodeHooked(n->PostfixToken);
			auto _c3 = v.visitNodeHooked(n->Type);
			auto _c4 = v.visitNodeHooked(n->Initializer);
			return v.factory->updatePropertySignatureDeclaration(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::QualifiedName:
		{
			auto* n = static_cast<QualifiedName*>(this);
			auto _c0 = v.visitNodeHooked(n->Left);
			auto _c1 = v.visitNodeHooked(n->Right);
			return v.factory->updateQualifiedName(n, _c0, _c1);
		}
		case Kind::RegularExpressionLiteral:
		{
			return this;
		}
		case Kind::RestType:
		{
			auto* n = static_cast<RestTypeNode*>(this);
			return v.factory->updateRestTypeNode(n, v.visitNodeHooked(n->Type));
		}
		case Kind::ReturnStatement:
		{
			auto* n = static_cast<ReturnStatement*>(this);
			return v.factory->updateReturnStatement(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::SatisfiesExpression:
		{
			auto* n = static_cast<SatisfiesExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitNodeHooked(n->Type);
			return v.factory->updateSatisfiesExpression(n, _c0, _c1);
		}
		case Kind::SemicolonClassElement:
		{
			return this;
		}
		case Kind::SetAccessor:
		{
			auto* n = static_cast<SetAccessorDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodesHooked(n->TypeParameters);
			auto _c3 = v.visitParametersHooked(n->Parameters);
			auto _c4 = v.visitNodeHooked(n->Type);
			auto _c5 = v.visitNodeHooked(n->FullSignature);
			auto _c6 = v.visitFunctionBodyHooked(n->Body);
			return v.factory->updateSetAccessorDeclaration(n, _c0, _c1, _c2, _c3, _c4, _c5, _c6);
		}
		case Kind::ShorthandPropertyAssignment:
		{
			auto* n = static_cast<ShorthandPropertyAssignment*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodeHooked(n->PostfixToken);
			auto _c3 = v.visitNodeHooked(n->Type);
			auto _c4 = v.visitNodeHooked(n->EqualsToken);
			auto _c5 = v.visitNodeHooked(n->ObjectAssignmentInitializer);
			return v.factory->updateShorthandPropertyAssignment(n, _c0, _c1, _c2, _c3, _c4, _c5);
		}
		case Kind::SourceFile:
		{
			auto* n = static_cast<SourceFile*>(this);
			auto _c0 = v.visitTopLevelStatementsHooked(n->Statements);
			auto _c1 = v.visitTokenHooked(n->EndOfFileToken);
			return v.factory->updateSourceFile(n, _c0, _c1);
		}
		case Kind::SpreadAssignment:
		{
			auto* n = static_cast<SpreadAssignment*>(this);
			return v.factory->updateSpreadAssignment(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::SpreadElement:
		{
			auto* n = static_cast<SpreadElement*>(this);
			return v.factory->updateSpreadElement(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::StringLiteral:
		{
			return this;
		}
		case Kind::SwitchStatement:
		{
			auto* n = static_cast<SwitchStatement*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitNodeHooked(n->CaseBlock);
			return v.factory->updateSwitchStatement(n, _c0, _c1);
		}
		case Kind::SyntaxList:
		{
			auto* n = static_cast<SyntaxList*>(this);
			std::vector<Node*> children;
			children.reserve(n->Children.size());
			for (auto* c : n->Children) children.push_back(v.visitNodeHooked(c));
			return v.factory->updateSyntaxList(n, std::move(children));
		}
		case Kind::SyntheticExpression:
		{
			auto* n = static_cast<SyntheticExpression*>(this);
			auto _c0 = n->Type;
			auto _c1 = n->IsSpread;
			auto _c2 = v.visitNodeHooked(n->TupleNameSource);
			return v.factory->updateSyntheticExpression(n, _c0, _c1, _c2);
		}
		case Kind::SyntheticReferenceExpression:
		{
			auto* n = static_cast<SyntheticReferenceExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitNodeHooked(n->ThisArg);
			return v.factory->updateSyntheticReferenceExpression(n, _c0, _c1);
		}
		case Kind::TaggedTemplateExpression:
		{
			auto* n = static_cast<TaggedTemplateExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->Tag);
			auto _c1 = v.visitNodeHooked(n->QuestionDotToken);
			auto _c2 = v.visitNodesHooked(n->TypeArguments);
			auto _c3 = v.visitNodeHooked(n->Template);
			auto _c4 = n->flags;
			return v.factory->updateTaggedTemplateExpression(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::TemplateExpression:
		{
			auto* n = static_cast<TemplateExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->Head);
			auto _c1 = v.visitNodesHooked(n->TemplateSpans);
			return v.factory->updateTemplateExpression(n, _c0, _c1);
		}
		case Kind::TemplateHead:
		{
			return this;
		}
		case Kind::TemplateLiteralType:
		{
			auto* n = static_cast<TemplateLiteralTypeNode*>(this);
			auto _c0 = v.visitNodeHooked(n->Head);
			auto _c1 = v.visitNodesHooked(n->TemplateSpans);
			return v.factory->updateTemplateLiteralTypeNode(n, _c0, _c1);
		}
		case Kind::TemplateLiteralTypeSpan:
		{
			auto* n = static_cast<TemplateLiteralTypeSpan*>(this);
			auto _c0 = v.visitNodeHooked(n->Type);
			auto _c1 = v.visitNodeHooked(n->Literal);
			return v.factory->updateTemplateLiteralTypeSpan(n, _c0, _c1);
		}
		case Kind::TemplateMiddle:
		{
			return this;
		}
		case Kind::TemplateSpan:
		{
			auto* n = static_cast<TemplateSpan*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitNodeHooked(n->Literal);
			return v.factory->updateTemplateSpan(n, _c0, _c1);
		}
		case Kind::TemplateTail:
		{
			return this;
		}
		case Kind::ThisType:
		{
			return this;
		}
		case Kind::ThrowStatement:
		{
			auto* n = static_cast<ThrowStatement*>(this);
			return v.factory->updateThrowStatement(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::ExportKeyword:
		case Kind::DeclareKeyword:
		case Kind::EqualsEqualsEqualsToken:
		case Kind::QuestionToken:
		case Kind::ColonToken:
		case Kind::DotDotDotToken:
		case Kind::SemicolonToken:
		case Kind::AssertsKeyword:
		case Kind::CommaToken:
		case Kind::EndOfFile:
		case Kind::EqualsToken:
		case Kind::EqualsGreaterThanToken:
		case Kind::AmpersandAmpersandToken:
		case Kind::AsteriskToken:
		case Kind::QuestionQuestionToken:
		case Kind::BarBarToken:
		case Kind::ExclamationEqualsEqualsToken:
		case Kind::PlusToken:
		case Kind::InKeyword:
		case Kind::DotToken:
		case Kind::QuestionDotToken:
		case Kind::AbstractKeyword:
		case Kind::AccessorKeyword:
		case Kind::AmpersandAmpersandEqualsToken:
		case Kind::AmpersandEqualsToken:
		case Kind::AmpersandToken:
		case Kind::AsKeyword:
		case Kind::AssertKeyword:
		case Kind::AsteriskAsteriskEqualsToken:
		case Kind::AsteriskAsteriskToken:
		case Kind::AsteriskEqualsToken:
		case Kind::AsyncKeyword:
		case Kind::AtToken:
		case Kind::AwaitKeyword:
		case Kind::BacktickToken:
		case Kind::BarBarEqualsToken:
		case Kind::BarEqualsToken:
		case Kind::BarToken:
		case Kind::BreakKeyword:
		case Kind::CaretEqualsToken:
		case Kind::CaretToken:
		case Kind::CaseKeyword:
		case Kind::CatchKeyword:
		case Kind::ClassKeyword:
		case Kind::CloseBraceToken:
		case Kind::CloseBracketToken:
		case Kind::CloseParenToken:
		case Kind::ConflictMarkerTrivia:
		case Kind::ConstKeyword:
		case Kind::ConstructorKeyword:
		case Kind::ContinueKeyword:
		case Kind::Count:
		case Kind::DebuggerKeyword:
		case Kind::DefaultKeyword:
		case Kind::DeferKeyword:
		case Kind::DeleteKeyword:
		case Kind::DoKeyword:
		case Kind::ElseKeyword:
		case Kind::EnumKeyword:
		case Kind::EqualsEqualsToken:
		case Kind::ExclamationEqualsToken:
		case Kind::ExclamationToken:
		case Kind::ExtendsKeyword:
		case Kind::FinallyKeyword:
		case Kind::ForKeyword:
		case Kind::FromKeyword:
		case Kind::FunctionKeyword:
		case Kind::GetKeyword:
		case Kind::GlobalKeyword:
		case Kind::GreaterThanEqualsToken:
		case Kind::GreaterThanGreaterThanEqualsToken:
		case Kind::GreaterThanGreaterThanGreaterThanEqualsToken:
		case Kind::GreaterThanGreaterThanGreaterThanToken:
		case Kind::GreaterThanGreaterThanToken:
		case Kind::GreaterThanToken:
		case Kind::HashToken:
		case Kind::IfKeyword:
		case Kind::ImmediateKeyword:
		case Kind::ImplementsKeyword:
		case Kind::ImportKeyword:
		case Kind::InferKeyword:
		case Kind::InstanceOfKeyword:
		case Kind::InterfaceKeyword:
		case Kind::IntrinsicKeyword:
		case Kind::IsKeyword:
		case Kind::JSDocCommentTextToken:
		case Kind::JsxTextAllWhiteSpaces:
		case Kind::KeyOfKeyword:
		case Kind::LessThanEqualsToken:
		case Kind::LessThanLessThanEqualsToken:
		case Kind::LessThanLessThanToken:
		case Kind::LessThanSlashToken:
		case Kind::LessThanToken:
		case Kind::LetKeyword:
		case Kind::MinusEqualsToken:
		case Kind::MinusMinusToken:
		case Kind::MinusToken:
		case Kind::ModuleKeyword:
		case Kind::MultiLineCommentTrivia:
		case Kind::NamespaceKeyword:
		case Kind::NewKeyword:
		case Kind::NewLineTrivia:
		case Kind::NonTextFileMarkerTrivia:
		case Kind::OfKeyword:
		case Kind::OpenBraceToken:
		case Kind::OpenBracketToken:
		case Kind::OpenParenToken:
		case Kind::OutKeyword:
		case Kind::OverrideKeyword:
		case Kind::PackageKeyword:
		case Kind::PercentEqualsToken:
		case Kind::PercentToken:
		case Kind::PlusEqualsToken:
		case Kind::PlusPlusToken:
		case Kind::PrivateKeyword:
		case Kind::ProtectedKeyword:
		case Kind::PublicKeyword:
		case Kind::QuestionQuestionEqualsToken:
		case Kind::ReadonlyKeyword:
		case Kind::RequireKeyword:
		case Kind::ReturnKeyword:
		case Kind::SatisfiesKeyword:
		case Kind::SetKeyword:
		case Kind::SingleLineCommentTrivia:
		case Kind::SlashEqualsToken:
		case Kind::SlashToken:
		case Kind::StaticKeyword:
		case Kind::SwitchKeyword:
		case Kind::ThrowKeyword:
		case Kind::TildeToken:
		case Kind::TryKeyword:
		case Kind::TypeKeyword:
		case Kind::TypeOfKeyword:
		case Kind::UniqueKeyword:
		case Kind::Unknown:
		case Kind::UsingKeyword:
		case Kind::VarKeyword:
		case Kind::WhileKeyword:
		case Kind::WhitespaceTrivia:
		case Kind::WithKeyword:
		case Kind::YieldKeyword:
		{
			return this;
		}
		case Kind::TryStatement:
		{
			auto* n = static_cast<TryStatement*>(this);
			auto _c0 = v.visitNodeHooked(n->TryBlock);
			auto _c1 = v.visitNodeHooked(n->CatchClause);
			auto _c2 = v.visitNodeHooked(n->FinallyBlock);
			return v.factory->updateTryStatement(n, _c0, _c1, _c2);
		}
		case Kind::TupleType:
		{
			auto* n = static_cast<TupleTypeNode*>(this);
			return v.factory->updateTupleTypeNode(n, v.visitNodesHooked(n->Elements));
		}
		case Kind::TypeAliasDeclaration:
		case Kind::JSTypeAliasDeclaration:
		{
			auto* n = static_cast<TypeAliasDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodesHooked(n->TypeParameters);
			auto _c3 = v.visitNodeHooked(n->Type);
			return v.factory->updateTypeAliasDeclaration(n, _c0, _c1, _c2, _c3);
		}
		case Kind::TypeAssertionExpression:
		{
			auto* n = static_cast<TypeAssertion*>(this);
			auto _c0 = v.visitNodeHooked(n->Type);
			auto _c1 = v.visitNodeHooked(n->Expression);
			return v.factory->updateTypeAssertion(n, _c0, _c1);
		}
		case Kind::TypeLiteral:
		{
			auto* n = static_cast<TypeLiteralNode*>(this);
			return v.factory->updateTypeLiteralNode(n, v.visitNodesHooked(n->Members));
		}
		case Kind::TypeOfExpression:
		{
			auto* n = static_cast<TypeOfExpression*>(this);
			return v.factory->updateTypeOfExpression(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::TypeOperator:
		{
			auto* n = static_cast<TypeOperatorNode*>(this);
			auto _c0 = n->Operator;
			auto _c1 = v.visitNodeHooked(n->Type);
			return v.factory->updateTypeOperatorNode(n, _c0, _c1);
		}
		case Kind::TypeParameter:
		{
			auto* n = static_cast<TypeParameterDeclaration*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->name);
			auto _c2 = v.visitNodeHooked(n->Constraint);
			auto _c3 = v.visitNodeHooked(n->Expression);
			auto _c4 = v.visitNodeHooked(n->DefaultType);
			return v.factory->updateTypeParameterDeclaration(n, _c0, _c1, _c2, _c3, _c4);
		}
		case Kind::TypePredicate:
		{
			auto* n = static_cast<TypePredicateNode*>(this);
			auto _c0 = v.visitNodeHooked(n->AssertsModifier);
			auto _c1 = v.visitNodeHooked(n->ParameterName);
			auto _c2 = v.visitNodeHooked(n->Type);
			return v.factory->updateTypePredicateNode(n, _c0, _c1, _c2);
		}
		case Kind::TypeQuery:
		{
			auto* n = static_cast<TypeQueryNode*>(this);
			auto _c0 = v.visitNodeHooked(n->ExprName);
			auto _c1 = v.visitNodesHooked(n->TypeArguments);
			return v.factory->updateTypeQueryNode(n, _c0, _c1);
		}
		case Kind::TypeReference:
		{
			auto* n = static_cast<TypeReferenceNode*>(this);
			auto _c0 = v.visitNodeHooked(n->TypeName);
			auto _c1 = v.visitNodesHooked(n->TypeArguments);
			return v.factory->updateTypeReferenceNode(n, _c0, _c1);
		}
		case Kind::UnionType:
		{
			auto* n = static_cast<UnionTypeNode*>(this);
			return v.factory->updateUnionTypeNode(n, v.visitNodesHooked(n->Types));
		}
		case Kind::VariableDeclaration:
		{
			auto* n = static_cast<VariableDeclaration*>(this);
			auto _c0 = v.visitNodeHooked(n->name);
			auto _c1 = v.visitNodeHooked(n->ExclamationToken);
			auto _c2 = v.visitNodeHooked(n->Type);
			auto _c3 = v.visitNodeHooked(n->Initializer);
			return v.factory->updateVariableDeclaration(n, _c0, _c1, _c2, _c3);
		}
		case Kind::VariableDeclarationList:
		{
			auto* n = static_cast<VariableDeclarationList*>(this);
			auto _c0 = v.visitNodesHooked(n->Declarations);
			auto _c1 = n->flags;
			return v.factory->updateVariableDeclarationList(n, _c0, _c1);
		}
		case Kind::VariableStatement:
		{
			auto* n = static_cast<VariableStatement*>(this);
			auto _c0 = v.visitModifiersHooked(n->modifiers);
			auto _c1 = v.visitNodeHooked(n->DeclarationList);
			return v.factory->updateVariableStatement(n, _c0, _c1);
		}
		case Kind::VoidExpression:
		{
			auto* n = static_cast<VoidExpression*>(this);
			return v.factory->updateVoidExpression(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::WhileStatement:
		{
			auto* n = static_cast<WhileStatement*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitIterationBodyHooked(n->Statement);
			return v.factory->updateWhileStatement(n, _c0, _c1);
		}
		case Kind::WithStatement:
		{
			auto* n = static_cast<WithStatement*>(this);
			auto _c0 = v.visitNodeHooked(n->Expression);
			auto _c1 = v.visitEmbeddedStatementHooked(n->Statement);
			return v.factory->updateWithStatement(n, _c0, _c1);
		}
		case Kind::YieldExpression:
		{
			auto* n = static_cast<YieldExpression*>(this);
			auto _c0 = v.visitNodeHooked(n->AsteriskToken);
			auto _c1 = v.visitNodeHooked(n->Expression);
			return v.factory->updateYieldExpression(n, _c0, _c1);
		}
		default:
			return this;
	}
}


}  // namespace tsc
