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
			return v.factory->updateArrayLiteralExpression(n, v.visitNodesHooked(n->Elements), n->MultiLine);
		}
		case Kind::ArrayType:
		{
			auto* n = static_cast<ArrayTypeNode*>(this);
			return v.factory->updateArrayTypeNode(n, v.visitNodeHooked(n->ElementType));
		}
		case Kind::ArrowFunction:
		{
			auto* n = static_cast<ArrowFunction*>(this);
			return v.factory->updateArrowFunction(n, v.visitModifiersHooked(n->modifiers), v.visitNodesHooked(n->TypeParameters), v.visitParametersHooked(n->Parameters), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->FullSignature), v.visitNodeHooked(n->EqualsGreaterThanToken), v.visitFunctionBodyHooked(n->Body));
		}
		case Kind::AsExpression:
		{
			auto* n = static_cast<AsExpression*>(this);
			return v.factory->updateAsExpression(n, v.visitNodeHooked(n->Expression), v.visitNodeHooked(n->Type));
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
			return v.factory->updateBinaryExpression(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->Left), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->OperatorToken), v.visitNodeHooked(n->Right));
		}
		case Kind::BindingElement:
		{
			auto* n = static_cast<BindingElement*>(this);
			return v.factory->updateBindingElement(n, v.visitNodeHooked(n->DotDotDotToken), v.visitNodeHooked(n->PropertyName), v.visitNodeHooked(n->name), v.visitNodeHooked(n->Initializer));
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
			return v.factory->updateBlock(n, v.visitNodesHooked(n->Statements), n->MultiLine);
		}
		case Kind::BreakStatement:
		{
			auto* n = static_cast<BreakStatement*>(this);
			return v.factory->updateBreakStatement(n, v.visitNodeHooked(n->Label));
		}
		case Kind::CallExpression:
		{
			auto* n = static_cast<CallExpression*>(this);
			return v.factory->updateCallExpression(n, v.visitNodeHooked(n->Expression), v.visitNodeHooked(n->QuestionDotToken), v.visitNodesHooked(n->TypeArguments), v.visitNodesHooked(n->Arguments), n->flags);
		}
		case Kind::CallSignature:
		{
			auto* n = static_cast<CallSignatureDeclaration*>(this);
			return v.factory->updateCallSignatureDeclaration(n, v.visitNodesHooked(n->TypeParameters), v.visitNodesHooked(n->Parameters), v.visitNodeHooked(n->Type));
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
			return v.factory->updateCaseOrDefaultClause(n, v.visitNodeHooked(n->Expression), v.visitNodesHooked(n->Statements));
		}
		case Kind::CatchClause:
		{
			auto* n = static_cast<CatchClause*>(this);
			return v.factory->updateCatchClause(n, v.visitNodeHooked(n->VariableDeclaration), v.visitNodeHooked(n->Block));
		}
		case Kind::ClassDeclaration:
		{
			auto* n = static_cast<ClassDeclaration*>(this);
			return v.factory->updateClassDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodesHooked(n->TypeParameters), v.visitNodesHooked(n->HeritageClauses), v.visitNodesHooked(n->Members));
		}
		case Kind::ClassExpression:
		{
			auto* n = static_cast<ClassExpression*>(this);
			return v.factory->updateClassExpression(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodesHooked(n->TypeParameters), v.visitNodesHooked(n->HeritageClauses), v.visitNodesHooked(n->Members));
		}
		case Kind::ClassStaticBlockDeclaration:
		{
			auto* n = static_cast<ClassStaticBlockDeclaration*>(this);
			return v.factory->updateClassStaticBlockDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->Body));
		}
		case Kind::ComputedPropertyName:
		{
			auto* n = static_cast<ComputedPropertyName*>(this);
			return v.factory->updateComputedPropertyName(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::ConditionalExpression:
		{
			auto* n = static_cast<ConditionalExpression*>(this);
			return v.factory->updateConditionalExpression(n, v.visitNodeHooked(n->Condition), v.visitNodeHooked(n->QuestionToken), v.visitNodeHooked(n->WhenTrue), v.visitNodeHooked(n->ColonToken), v.visitNodeHooked(n->WhenFalse));
		}
		case Kind::ConditionalType:
		{
			auto* n = static_cast<ConditionalTypeNode*>(this);
			return v.factory->updateConditionalTypeNode(n, v.visitNodeHooked(n->CheckType), v.visitNodeHooked(n->ExtendsType), v.visitNodeHooked(n->TrueType), v.visitNodeHooked(n->FalseType));
		}
		case Kind::ConstructSignature:
		{
			auto* n = static_cast<ConstructSignatureDeclaration*>(this);
			return v.factory->updateConstructSignatureDeclaration(n, v.visitNodesHooked(n->TypeParameters), v.visitNodesHooked(n->Parameters), v.visitNodeHooked(n->Type));
		}
		case Kind::Constructor:
		{
			auto* n = static_cast<ConstructorDeclaration*>(this);
			return v.factory->updateConstructorDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodesHooked(n->TypeParameters), v.visitParametersHooked(n->Parameters), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->FullSignature), v.visitFunctionBodyHooked(n->Body));
		}
		case Kind::ConstructorType:
		{
			auto* n = static_cast<ConstructorTypeNode*>(this);
			return v.factory->updateConstructorTypeNode(n, v.visitModifiersHooked(n->modifiers), v.visitNodesHooked(n->TypeParameters), v.visitNodesHooked(n->Parameters), v.visitNodeHooked(n->Type));
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
			return v.factory->updateDoStatement(n, v.visitIterationBodyHooked(n->Statement), v.visitNodeHooked(n->Expression));
		}
		case Kind::ElementAccessExpression:
		{
			auto* n = static_cast<ElementAccessExpression*>(this);
			return v.factory->updateElementAccessExpression(n, v.visitNodeHooked(n->Expression), v.visitNodeHooked(n->QuestionDotToken), v.visitNodeHooked(n->ArgumentExpression), n->flags);
		}
		case Kind::EmptyStatement:
		{
			return this;
		}
		case Kind::EnumDeclaration:
		{
			auto* n = static_cast<EnumDeclaration*>(this);
			return v.factory->updateEnumDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodesHooked(n->Members));
		}
		case Kind::EnumMember:
		{
			auto* n = static_cast<EnumMember*>(this);
			return v.factory->updateEnumMember(n, v.visitNodeHooked(n->name), v.visitNodeHooked(n->Initializer));
		}
		case Kind::ExportAssignment:
		{
			auto* n = static_cast<ExportAssignment*>(this);
			return v.factory->updateExportAssignment(n, v.visitModifiersHooked(n->modifiers), n->IsExportEquals, v.visitNodeHooked(n->Type), v.visitNodeHooked(n->Expression));
		}
		case Kind::ExportDeclaration:
		{
			auto* n = static_cast<ExportDeclaration*>(this);
			return v.factory->updateExportDeclaration(n, v.visitModifiersHooked(n->modifiers), n->IsTypeOnly, v.visitNodeHooked(n->ExportClause), v.visitNodeHooked(n->ModuleSpecifier), v.visitNodeHooked(n->Attributes));
		}
		case Kind::ExportSpecifier:
		{
			auto* n = static_cast<ExportSpecifier*>(this);
			return v.factory->updateExportSpecifier(n, n->IsTypeOnly, v.visitNodeHooked(n->PropertyName), v.visitNodeHooked(n->name));
		}
		case Kind::ExpressionStatement:
		{
			auto* n = static_cast<ExpressionStatement*>(this);
			return v.factory->updateExpressionStatement(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::ExpressionWithTypeArguments:
		{
			auto* n = static_cast<ExpressionWithTypeArguments*>(this);
			return v.factory->updateExpressionWithTypeArguments(n, v.visitNodeHooked(n->Expression), v.visitNodesHooked(n->TypeArguments));
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
			return v.factory->updateForInOrOfStatement(n, v.visitNodeHooked(n->AwaitModifier), v.visitNodeHooked(n->Initializer), v.visitNodeHooked(n->Expression), v.visitIterationBodyHooked(n->Statement));
		}
		case Kind::ForStatement:
		{
			auto* n = static_cast<ForStatement*>(this);
			return v.factory->updateForStatement(n, v.visitNodeHooked(n->Initializer), v.visitNodeHooked(n->Condition), v.visitNodeHooked(n->Incrementor), v.visitIterationBodyHooked(n->Statement));
		}
		case Kind::FunctionDeclaration:
		{
			auto* n = static_cast<FunctionDeclaration*>(this);
			return v.factory->updateFunctionDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->AsteriskToken), v.visitNodeHooked(n->name), v.visitNodesHooked(n->TypeParameters), v.visitParametersHooked(n->Parameters), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->FullSignature), v.visitFunctionBodyHooked(n->Body));
		}
		case Kind::FunctionExpression:
		{
			auto* n = static_cast<FunctionExpression*>(this);
			return v.factory->updateFunctionExpression(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->AsteriskToken), v.visitNodeHooked(n->name), v.visitNodesHooked(n->TypeParameters), v.visitParametersHooked(n->Parameters), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->FullSignature), v.visitFunctionBodyHooked(n->Body));
		}
		case Kind::FunctionType:
		{
			auto* n = static_cast<FunctionTypeNode*>(this);
			return v.factory->updateFunctionTypeNode(n, v.visitNodesHooked(n->TypeParameters), v.visitNodesHooked(n->Parameters), v.visitNodeHooked(n->Type));
		}
		case Kind::GetAccessor:
		{
			auto* n = static_cast<GetAccessorDeclaration*>(this);
			return v.factory->updateGetAccessorDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodesHooked(n->TypeParameters), v.visitParametersHooked(n->Parameters), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->FullSignature), v.visitFunctionBodyHooked(n->Body));
		}
		case Kind::HeritageClause:
		{
			auto* n = static_cast<HeritageClause*>(this);
			return v.factory->updateHeritageClause(n, n->Token, v.visitNodesHooked(n->Types));
		}
		case Kind::Identifier:
		{
			return this;
		}
		case Kind::IfStatement:
		{
			auto* n = static_cast<IfStatement*>(this);
			return v.factory->updateIfStatement(n, v.visitNodeHooked(n->Expression), v.visitEmbeddedStatementHooked(n->ThenStatement), v.visitEmbeddedStatementHooked(n->ElseStatement));
		}
		case Kind::ImportAttribute:
		{
			auto* n = static_cast<ImportAttribute*>(this);
			return v.factory->updateImportAttribute(n, v.visitNodeHooked(n->name), v.visitNodeHooked(n->Value));
		}
		case Kind::ImportAttributes:
		{
			auto* n = static_cast<ImportAttributes*>(this);
			return v.factory->updateImportAttributes(n, n->Token, v.visitNodesHooked(n->Attributes), n->MultiLine);
		}
		case Kind::ImportClause:
		{
			auto* n = static_cast<ImportClause*>(this);
			return v.factory->updateImportClause(n, n->PhaseModifier, v.visitNodeHooked(n->name), v.visitNodeHooked(n->NamedBindings));
		}
		case Kind::ImportDeclaration:
		case Kind::JSImportDeclaration:
		{
			auto* n = static_cast<ImportDeclaration*>(this);
			return v.factory->updateImportDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->ImportClause), v.visitNodeHooked(n->ModuleSpecifier), v.visitNodeHooked(n->Attributes));
		}
		case Kind::ImportEqualsDeclaration:
		{
			auto* n = static_cast<ImportEqualsDeclaration*>(this);
			return v.factory->updateImportEqualsDeclaration(n, v.visitModifiersHooked(n->modifiers), n->IsTypeOnly, v.visitNodeHooked(n->name), v.visitNodeHooked(n->ModuleReference));
		}
		case Kind::ImportSpecifier:
		{
			auto* n = static_cast<ImportSpecifier*>(this);
			return v.factory->updateImportSpecifier(n, n->IsTypeOnly, v.visitNodeHooked(n->PropertyName), v.visitNodeHooked(n->name));
		}
		case Kind::ImportType:
		{
			auto* n = static_cast<ImportTypeNode*>(this);
			return v.factory->updateImportTypeNode(n, n->IsTypeOf, v.visitNodeHooked(n->Argument), v.visitNodeHooked(n->Attributes), v.visitNodeHooked(n->Qualifier), v.visitNodesHooked(n->TypeArguments));
		}
		case Kind::IndexSignature:
		{
			auto* n = static_cast<IndexSignatureDeclaration*>(this);
			return v.factory->updateIndexSignatureDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodesHooked(n->Parameters), v.visitNodeHooked(n->Type));
		}
		case Kind::IndexedAccessType:
		{
			auto* n = static_cast<IndexedAccessTypeNode*>(this);
			return v.factory->updateIndexedAccessTypeNode(n, v.visitNodeHooked(n->ObjectType), v.visitNodeHooked(n->IndexType));
		}
		case Kind::InferType:
		{
			auto* n = static_cast<InferTypeNode*>(this);
			return v.factory->updateInferTypeNode(n, v.visitNodeHooked(n->TypeParameter));
		}
		case Kind::InterfaceDeclaration:
		{
			auto* n = static_cast<InterfaceDeclaration*>(this);
			return v.factory->updateInterfaceDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodesHooked(n->TypeParameters), v.visitNodesHooked(n->HeritageClauses), v.visitNodesHooked(n->Members));
		}
		case Kind::IntersectionType:
		{
			auto* n = static_cast<IntersectionTypeNode*>(this);
			return v.factory->updateIntersectionTypeNode(n, v.visitNodesHooked(n->Types));
		}
		case Kind::JSDoc:
		{
			auto* n = static_cast<JSDoc*>(this);
			return v.factory->updateJSDoc(n, v.visitNodesHooked(n->Comment), v.visitNodesHooked(n->Tags));
		}
		case Kind::JSDocAllType:
		{
			return this;
		}
		case Kind::JSDocAugmentsTag:
		{
			auto* n = static_cast<JSDocAugmentsTag*>(this);
			return v.factory->updateJSDocAugmentsTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->ClassName), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocCallbackTag:
		{
			auto* n = static_cast<JSDocCallbackTag*>(this);
			return v.factory->updateJSDocCallbackTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->TypeExpression), v.visitNodeHooked(n->name), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocDeprecatedTag:
		{
			auto* n = static_cast<JSDocDeprecatedTag*>(this);
			return v.factory->updateJSDocDeprecatedTag(n, v.visitNodeHooked(n->TagName), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocImplementsTag:
		{
			auto* n = static_cast<JSDocImplementsTag*>(this);
			return v.factory->updateJSDocImplementsTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->ClassName), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocImportTag:
		{
			auto* n = static_cast<JSDocImportTag*>(this);
			return v.factory->updateJSDocImportTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->ImportClause), v.visitNodeHooked(n->ModuleSpecifier), v.visitNodeHooked(n->Attributes), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocLink:
		{
			auto* n = static_cast<JSDocLink*>(this);
			return v.factory->updateJSDocLink(n, v.visitNodeHooked(n->name), n->text);
		}
		case Kind::JSDocLinkCode:
		{
			auto* n = static_cast<JSDocLinkCode*>(this);
			return v.factory->updateJSDocLinkCode(n, v.visitNodeHooked(n->name), n->text);
		}
		case Kind::JSDocLinkPlain:
		{
			auto* n = static_cast<JSDocLinkPlain*>(this);
			return v.factory->updateJSDocLinkPlain(n, v.visitNodeHooked(n->name), n->text);
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
			return v.factory->updateJSDocOverloadTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->TypeExpression), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocOverrideTag:
		{
			auto* n = static_cast<JSDocOverrideTag*>(this);
			return v.factory->updateJSDocOverrideTag(n, v.visitNodeHooked(n->TagName), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocParameterTag:
		case Kind::JSDocPropertyTag:
		{
			auto* n = static_cast<JSDocParameterOrPropertyTag*>(this);
			return v.factory->updateJSDocParameterOrPropertyTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->name), n->IsBracketed, v.visitNodeHooked(n->TypeExpression), n->IsNameFirst, v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocPrivateTag:
		{
			auto* n = static_cast<JSDocPrivateTag*>(this);
			return v.factory->updateJSDocPrivateTag(n, v.visitNodeHooked(n->TagName), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocProtectedTag:
		{
			auto* n = static_cast<JSDocProtectedTag*>(this);
			return v.factory->updateJSDocProtectedTag(n, v.visitNodeHooked(n->TagName), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocPublicTag:
		{
			auto* n = static_cast<JSDocPublicTag*>(this);
			return v.factory->updateJSDocPublicTag(n, v.visitNodeHooked(n->TagName), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocReadonlyTag:
		{
			auto* n = static_cast<JSDocReadonlyTag*>(this);
			return v.factory->updateJSDocReadonlyTag(n, v.visitNodeHooked(n->TagName), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocReturnTag:
		{
			auto* n = static_cast<JSDocReturnTag*>(this);
			return v.factory->updateJSDocReturnTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->TypeExpression), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocSatisfiesTag:
		{
			auto* n = static_cast<JSDocSatisfiesTag*>(this);
			return v.factory->updateJSDocSatisfiesTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->TypeExpression), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocSeeTag:
		{
			auto* n = static_cast<JSDocSeeTag*>(this);
			return v.factory->updateJSDocSeeTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->NameExpression), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocSignature:
		{
			auto* n = static_cast<JSDocSignature*>(this);
			return v.factory->updateJSDocSignature(n, v.visitNodesHooked(n->TypeParameters), v.visitNodesHooked(n->Parameters), v.visitNodeHooked(n->Type));
		}
		case Kind::JSDocTemplateTag:
		{
			auto* n = static_cast<JSDocTemplateTag*>(this);
			return v.factory->updateJSDocTemplateTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->Constraint), v.visitNodesHooked(n->TypeParameters), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocText:
		{
			return this;
		}
		case Kind::JSDocThisTag:
		{
			auto* n = static_cast<JSDocThisTag*>(this);
			return v.factory->updateJSDocThisTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->TypeExpression), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocThrowsTag:
		{
			auto* n = static_cast<JSDocThrowsTag*>(this);
			return v.factory->updateJSDocThrowsTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->TypeExpression), v.visitNodesHooked(n->Comment));
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
			return v.factory->updateJSDocTypeLiteral(n, std::move(jsdocPropertyTags), n->IsArrayType);
		}
		case Kind::JSDocTypeTag:
		{
			auto* n = static_cast<JSDocTypeTag*>(this);
			return v.factory->updateJSDocTypeTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->TypeExpression), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocTypedefTag:
		{
			auto* n = static_cast<JSDocTypedefTag*>(this);
			return v.factory->updateJSDocTypedefTag(n, v.visitNodeHooked(n->TagName), v.visitNodeHooked(n->TypeExpression), v.visitNodeHooked(n->name), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocUnknownTag:
		{
			auto* n = static_cast<JSDocUnknownTag*>(this);
			return v.factory->updateJSDocUnknownTag(n, v.visitNodeHooked(n->TagName), v.visitNodesHooked(n->Comment));
		}
		case Kind::JSDocVariadicType:
		{
			auto* n = static_cast<JSDocVariadicType*>(this);
			return v.factory->updateJSDocVariadicType(n, v.visitNodeHooked(n->Type));
		}
		case Kind::JsxAttribute:
		{
			auto* n = static_cast<JsxAttribute*>(this);
			return v.factory->updateJsxAttribute(n, v.visitNodeHooked(n->name), v.visitNodeHooked(n->Initializer));
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
			return v.factory->updateJsxElement(n, v.visitNodeHooked(n->OpeningElement), v.visitNodesHooked(n->Children), v.visitNodeHooked(n->ClosingElement));
		}
		case Kind::JsxExpression:
		{
			auto* n = static_cast<JsxExpression*>(this);
			return v.factory->updateJsxExpression(n, v.visitNodeHooked(n->DotDotDotToken), v.visitNodeHooked(n->Expression));
		}
		case Kind::JsxFragment:
		{
			auto* n = static_cast<JsxFragment*>(this);
			return v.factory->updateJsxFragment(n, v.visitNodeHooked(n->OpeningFragment), v.visitNodesHooked(n->Children), v.visitNodeHooked(n->ClosingFragment));
		}
		case Kind::JsxNamespacedName:
		{
			auto* n = static_cast<JsxNamespacedName*>(this);
			return v.factory->updateJsxNamespacedName(n, v.visitNodeHooked(n->Namespace), v.visitNodeHooked(n->name));
		}
		case Kind::JsxOpeningElement:
		{
			auto* n = static_cast<JsxOpeningElement*>(this);
			return v.factory->updateJsxOpeningElement(n, v.visitNodeHooked(n->TagName), v.visitNodesHooked(n->TypeArguments), v.visitNodeHooked(n->Attributes));
		}
		case Kind::JsxOpeningFragment:
		{
			return this;
		}
		case Kind::JsxSelfClosingElement:
		{
			auto* n = static_cast<JsxSelfClosingElement*>(this);
			return v.factory->updateJsxSelfClosingElement(n, v.visitNodeHooked(n->TagName), v.visitNodesHooked(n->TypeArguments), v.visitNodeHooked(n->Attributes));
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
			return v.factory->updateLabeledStatement(n, v.visitNodeHooked(n->Label), v.visitEmbeddedStatementHooked(n->Statement));
		}
		case Kind::LiteralType:
		{
			auto* n = static_cast<LiteralTypeNode*>(this);
			return v.factory->updateLiteralTypeNode(n, v.visitNodeHooked(n->Literal));
		}
		case Kind::MappedType:
		{
			auto* n = static_cast<MappedTypeNode*>(this);
			return v.factory->updateMappedTypeNode(n, v.visitNodeHooked(n->ReadonlyToken), v.visitNodeHooked(n->TypeParameter), v.visitNodeHooked(n->NameType), v.visitNodeHooked(n->QuestionToken), v.visitNodeHooked(n->Type), v.visitNodesHooked(n->Members));
		}
		case Kind::MetaProperty:
		{
			auto* n = static_cast<MetaProperty*>(this);
			return v.factory->updateMetaProperty(n, n->KeywordToken, v.visitNodeHooked(n->name));
		}
		case Kind::MethodDeclaration:
		{
			auto* n = static_cast<MethodDeclaration*>(this);
			return v.factory->updateMethodDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->AsteriskToken), v.visitNodeHooked(n->name), v.visitNodeHooked(n->PostfixToken), v.visitNodesHooked(n->TypeParameters), v.visitParametersHooked(n->Parameters), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->FullSignature), v.visitFunctionBodyHooked(n->Body));
		}
		case Kind::MethodSignature:
		{
			auto* n = static_cast<MethodSignatureDeclaration*>(this);
			return v.factory->updateMethodSignatureDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodeHooked(n->PostfixToken), v.visitNodesHooked(n->TypeParameters), v.visitNodesHooked(n->Parameters), v.visitNodeHooked(n->Type));
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
			return v.factory->updateModuleDeclaration(n, v.visitModifiersHooked(n->modifiers), n->Keyword, v.visitNodeHooked(n->name), v.visitNodeHooked(n->Attributes), v.visitNodeHooked(n->Body));
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
			return v.factory->updateNamedTupleMember(n, v.visitNodeHooked(n->DotDotDotToken), v.visitNodeHooked(n->name), v.visitNodeHooked(n->QuestionToken), v.visitNodeHooked(n->Type));
		}
		case Kind::NamespaceExport:
		{
			auto* n = static_cast<NamespaceExport*>(this);
			return v.factory->updateNamespaceExport(n, v.visitNodeHooked(n->name));
		}
		case Kind::NamespaceExportDeclaration:
		{
			auto* n = static_cast<NamespaceExportDeclaration*>(this);
			return v.factory->updateNamespaceExportDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name));
		}
		case Kind::NamespaceImport:
		{
			auto* n = static_cast<NamespaceImport*>(this);
			return v.factory->updateNamespaceImport(n, v.visitNodeHooked(n->name));
		}
		case Kind::NewExpression:
		{
			auto* n = static_cast<NewExpression*>(this);
			return v.factory->updateNewExpression(n, v.visitNodeHooked(n->Expression), v.visitNodesHooked(n->TypeArguments), v.visitNodesHooked(n->Arguments));
		}
		case Kind::NoSubstitutionTemplateLiteral:
		{
			return this;
		}
		case Kind::NonNullExpression:
		{
			auto* n = static_cast<NonNullExpression*>(this);
			return v.factory->updateNonNullExpression(n, v.visitNodeHooked(n->Expression), n->flags);
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
			return v.factory->updateObjectLiteralExpression(n, v.visitNodesHooked(n->Properties), n->MultiLine);
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
			return v.factory->updateParameterDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->DotDotDotToken), v.visitNodeHooked(n->name), v.visitNodeHooked(n->QuestionToken), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->Initializer));
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
			return v.factory->updatePostfixUnaryExpression(n, v.visitNodeHooked(n->Operand), n->Operator);
		}
		case Kind::PrefixUnaryExpression:
		{
			auto* n = static_cast<PrefixUnaryExpression*>(this);
			return v.factory->updatePrefixUnaryExpression(n, n->Operator, v.visitNodeHooked(n->Operand));
		}
		case Kind::PrivateIdentifier:
		{
			return this;
		}
		case Kind::PropertyAccessExpression:
		{
			auto* n = static_cast<PropertyAccessExpression*>(this);
			return v.factory->updatePropertyAccessExpression(n, v.visitNodeHooked(n->Expression), v.visitNodeHooked(n->QuestionDotToken), v.visitNodeHooked(n->name), n->flags);
		}
		case Kind::PropertyAssignment:
		{
			auto* n = static_cast<PropertyAssignment*>(this);
			return v.factory->updatePropertyAssignment(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodeHooked(n->PostfixToken), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->Initializer));
		}
		case Kind::PropertyDeclaration:
		{
			auto* n = static_cast<PropertyDeclaration*>(this);
			return v.factory->updatePropertyDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodeHooked(n->PostfixToken), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->Initializer));
		}
		case Kind::PropertySignature:
		{
			auto* n = static_cast<PropertySignatureDeclaration*>(this);
			return v.factory->updatePropertySignatureDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodeHooked(n->PostfixToken), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->Initializer));
		}
		case Kind::QualifiedName:
		{
			auto* n = static_cast<QualifiedName*>(this);
			return v.factory->updateQualifiedName(n, v.visitNodeHooked(n->Left), v.visitNodeHooked(n->Right));
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
			return v.factory->updateSatisfiesExpression(n, v.visitNodeHooked(n->Expression), v.visitNodeHooked(n->Type));
		}
		case Kind::SemicolonClassElement:
		{
			return this;
		}
		case Kind::SetAccessor:
		{
			auto* n = static_cast<SetAccessorDeclaration*>(this);
			return v.factory->updateSetAccessorDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodesHooked(n->TypeParameters), v.visitParametersHooked(n->Parameters), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->FullSignature), v.visitFunctionBodyHooked(n->Body));
		}
		case Kind::ShorthandPropertyAssignment:
		{
			auto* n = static_cast<ShorthandPropertyAssignment*>(this);
			return v.factory->updateShorthandPropertyAssignment(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodeHooked(n->PostfixToken), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->EqualsToken), v.visitNodeHooked(n->ObjectAssignmentInitializer));
		}
		case Kind::SourceFile:
		{
			auto* n = static_cast<SourceFile*>(this);
			return v.factory->updateSourceFile(n, v.visitTopLevelStatementsHooked(n->Statements), v.visitTokenHooked(n->EndOfFileToken));
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
			return v.factory->updateSwitchStatement(n, v.visitNodeHooked(n->Expression), v.visitNodeHooked(n->CaseBlock));
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
			return v.factory->updateSyntheticExpression(n, n->Type, n->IsSpread, v.visitNodeHooked(n->TupleNameSource));
		}
		case Kind::SyntheticReferenceExpression:
		{
			auto* n = static_cast<SyntheticReferenceExpression*>(this);
			return v.factory->updateSyntheticReferenceExpression(n, v.visitNodeHooked(n->Expression), v.visitNodeHooked(n->ThisArg));
		}
		case Kind::TaggedTemplateExpression:
		{
			auto* n = static_cast<TaggedTemplateExpression*>(this);
			return v.factory->updateTaggedTemplateExpression(n, v.visitNodeHooked(n->Tag), v.visitNodeHooked(n->QuestionDotToken), v.visitNodesHooked(n->TypeArguments), v.visitNodeHooked(n->Template), n->flags);
		}
		case Kind::TemplateExpression:
		{
			auto* n = static_cast<TemplateExpression*>(this);
			return v.factory->updateTemplateExpression(n, v.visitNodeHooked(n->Head), v.visitNodesHooked(n->TemplateSpans));
		}
		case Kind::TemplateHead:
		{
			return this;
		}
		case Kind::TemplateLiteralType:
		{
			auto* n = static_cast<TemplateLiteralTypeNode*>(this);
			return v.factory->updateTemplateLiteralTypeNode(n, v.visitNodeHooked(n->Head), v.visitNodesHooked(n->TemplateSpans));
		}
		case Kind::TemplateLiteralTypeSpan:
		{
			auto* n = static_cast<TemplateLiteralTypeSpan*>(this);
			return v.factory->updateTemplateLiteralTypeSpan(n, v.visitNodeHooked(n->Type), v.visitNodeHooked(n->Literal));
		}
		case Kind::TemplateMiddle:
		{
			return this;
		}
		case Kind::TemplateSpan:
		{
			auto* n = static_cast<TemplateSpan*>(this);
			return v.factory->updateTemplateSpan(n, v.visitNodeHooked(n->Expression), v.visitNodeHooked(n->Literal));
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
		{
			return this;
		}
		case Kind::TryStatement:
		{
			auto* n = static_cast<TryStatement*>(this);
			return v.factory->updateTryStatement(n, v.visitNodeHooked(n->TryBlock), v.visitNodeHooked(n->CatchClause), v.visitNodeHooked(n->FinallyBlock));
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
			return v.factory->updateTypeAliasDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodesHooked(n->TypeParameters), v.visitNodeHooked(n->Type));
		}
		case Kind::TypeAssertionExpression:
		{
			auto* n = static_cast<TypeAssertion*>(this);
			return v.factory->updateTypeAssertion(n, v.visitNodeHooked(n->Type), v.visitNodeHooked(n->Expression));
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
			return v.factory->updateTypeOperatorNode(n, n->Operator, v.visitNodeHooked(n->Type));
		}
		case Kind::TypeParameter:
		{
			auto* n = static_cast<TypeParameterDeclaration*>(this);
			return v.factory->updateTypeParameterDeclaration(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->name), v.visitNodeHooked(n->Constraint), v.visitNodeHooked(n->Expression), v.visitNodeHooked(n->DefaultType));
		}
		case Kind::TypePredicate:
		{
			auto* n = static_cast<TypePredicateNode*>(this);
			return v.factory->updateTypePredicateNode(n, v.visitNodeHooked(n->AssertsModifier), v.visitNodeHooked(n->ParameterName), v.visitNodeHooked(n->Type));
		}
		case Kind::TypeQuery:
		{
			auto* n = static_cast<TypeQueryNode*>(this);
			return v.factory->updateTypeQueryNode(n, v.visitNodeHooked(n->ExprName), v.visitNodesHooked(n->TypeArguments));
		}
		case Kind::TypeReference:
		{
			auto* n = static_cast<TypeReferenceNode*>(this);
			return v.factory->updateTypeReferenceNode(n, v.visitNodeHooked(n->TypeName), v.visitNodesHooked(n->TypeArguments));
		}
		case Kind::UnionType:
		{
			auto* n = static_cast<UnionTypeNode*>(this);
			return v.factory->updateUnionTypeNode(n, v.visitNodesHooked(n->Types));
		}
		case Kind::VariableDeclaration:
		{
			auto* n = static_cast<VariableDeclaration*>(this);
			return v.factory->updateVariableDeclaration(n, v.visitNodeHooked(n->name), v.visitNodeHooked(n->ExclamationToken), v.visitNodeHooked(n->Type), v.visitNodeHooked(n->Initializer));
		}
		case Kind::VariableDeclarationList:
		{
			auto* n = static_cast<VariableDeclarationList*>(this);
			return v.factory->updateVariableDeclarationList(n, v.visitNodesHooked(n->Declarations), n->flags);
		}
		case Kind::VariableStatement:
		{
			auto* n = static_cast<VariableStatement*>(this);
			return v.factory->updateVariableStatement(n, v.visitModifiersHooked(n->modifiers), v.visitNodeHooked(n->DeclarationList));
		}
		case Kind::VoidExpression:
		{
			auto* n = static_cast<VoidExpression*>(this);
			return v.factory->updateVoidExpression(n, v.visitNodeHooked(n->Expression));
		}
		case Kind::WhileStatement:
		{
			auto* n = static_cast<WhileStatement*>(this);
			return v.factory->updateWhileStatement(n, v.visitNodeHooked(n->Expression), v.visitIterationBodyHooked(n->Statement));
		}
		case Kind::WithStatement:
		{
			auto* n = static_cast<WithStatement*>(this);
			return v.factory->updateWithStatement(n, v.visitNodeHooked(n->Expression), v.visitEmbeddedStatementHooked(n->Statement));
		}
		case Kind::YieldExpression:
		{
			auto* n = static_cast<YieldExpression*>(this);
			return v.factory->updateYieldExpression(n, v.visitNodeHooked(n->AsteriskToken), v.visitNodeHooked(n->Expression));
		}
		default:
			return this;
	}
}


}  // namespace tsc
