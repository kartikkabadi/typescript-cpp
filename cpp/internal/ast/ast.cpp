// Hand-written Node accessors — ported 1:1 from tsc/internal/ast/ast.go.
#include "internal/ast/ast.h"
#include "internal/ast/precedence.h"

#include <algorithm>
#include <functional>
#include <numeric>

namespace tsc {

namespace {

std::string join(const std::vector<std::string>& parts) {
	return std::accumulate(parts.begin(), parts.end(), std::string{});
}

Node* firstOrNull(const NodeList* list) {
	return list && !list->nodes.empty() ? list->nodes.front() : nullptr;
}

}  // namespace

ModifierFlags Node::modifierFlags() const {
	if (ModifierList* m = modifiers())
		return m->ModifierFlags;
	return ModifierFlagsNone;
}

std::vector<Node*> Node::modifierNodes() const {
	if (ModifierList* m = modifiers())
		return m->nodes;
	return {};
}

bool Node::isTypeOnly() const {
	switch (kind) {
	case Kind::ImportEqualsDeclaration:
		return as<ImportEqualsDeclaration>()->IsTypeOnly;
	case Kind::ImportSpecifier:
		return as<ImportSpecifier>()->IsTypeOnly;
	case Kind::ImportClause:
		return as<ImportClause>()->PhaseModifier == Kind::TypeKeyword;
	case Kind::ExportDeclaration:
		return as<ExportDeclaration>()->IsTypeOnly;
	case Kind::ExportSpecifier:
		return as<ExportSpecifier>()->IsTypeOnly;
	}
	return false;
}

std::vector<Node*> Node::decorators() const {
	std::vector<Node*> out;
	if (ModifierList* m = modifiers()) {
		for (Node* n : m->nodes) {
			if (isDecorator(n))
				out.push_back(n);
		}
	}
	return out;
}

Symbol* Node::symbol() const {
	auto d = const_cast<Node*>(this)->declarationData();
	return d.symbol ? *d.symbol : nullptr;
}

Symbol* Node::localSymbol() const {
	auto d = const_cast<Node*>(this)->exportableData();
	return d.localSymbol ? *d.localSymbol : nullptr;
}

SymbolTable* Node::locals() const {
	auto d = const_cast<Node*>(this)->localsContainerData();
	return d.locals ? d.locals : nullptr;
}

Node* Node::nextContainer() const {
	auto d = const_cast<Node*>(this)->localsContainerData();
	return d.nextContainer ? *d.nextContainer : nullptr;
}

std::string Node::text() const {
	switch (kind) {
	case Kind::Identifier:
		return as<Identifier>()->Text;
	case Kind::PrivateIdentifier:
		return as<PrivateIdentifier>()->Text;
	case Kind::StringLiteral:
		return as<StringLiteral>()->Text;
	case Kind::NumericLiteral:
		return as<NumericLiteral>()->Text;
	case Kind::BigIntLiteral:
		return as<BigIntLiteral>()->Text;
	case Kind::MetaProperty:
		return as<MetaProperty>()->name->text();
	case Kind::NoSubstitutionTemplateLiteral:
		return as<NoSubstitutionTemplateLiteral>()->Text;
	case Kind::TemplateHead:
		return as<TemplateHead>()->Text;
	case Kind::TemplateMiddle:
		return as<TemplateMiddle>()->Text;
	case Kind::TemplateTail:
		return as<TemplateTail>()->Text;
	case Kind::JsxNamespacedName: {
		auto* j = as<JsxNamespacedName>();
		return j->Namespace->text() + ":" + j->name->text();
	}
	case Kind::RegularExpressionLiteral:
		return as<RegularExpressionLiteral>()->Text;
	case Kind::JSDocText:
		return join(as<JSDocText>()->text);
	case Kind::JSDocLink:
		return join(as<JSDocLink>()->text);
	case Kind::JSDocLinkCode:
		return join(as<JSDocLinkCode>()->text);
	case Kind::JSDocLinkPlain:
		return join(as<JSDocLinkPlain>()->text);
	}
	TSC_UNREACHABLE("Unhandled case in Node::text");
}

std::string Node::rawText() const {
	switch (kind) {
	case Kind::TemplateHead:
		return as<TemplateHead>()->RawText;
	case Kind::TemplateMiddle:
		return as<TemplateMiddle>()->RawText;
	case Kind::TemplateTail:
		return as<TemplateTail>()->RawText;
	}
	TSC_UNREACHABLE("Unhandled case in Node::rawText");
}

Node* Node::body() const {
	auto d = const_cast<Node*>(this)->bodyData();
	return d.body ? *d.body : nullptr;
}

Node* Node::expression() const {
	switch (kind) {
	case Kind::PropertyAccessExpression:
		return as<PropertyAccessExpression>()->Expression;
	case Kind::ElementAccessExpression:
		return as<ElementAccessExpression>()->Expression;
	case Kind::ParenthesizedExpression:
		return as<ParenthesizedExpression>()->Expression;
	case Kind::CallExpression:
		return as<CallExpression>()->Expression;
	case Kind::NewExpression:
		return as<NewExpression>()->Expression;
	case Kind::ExpressionWithTypeArguments:
		return as<ExpressionWithTypeArguments>()->Expression;
	case Kind::ComputedPropertyName:
		return as<ComputedPropertyName>()->Expression;
	case Kind::NonNullExpression:
		return as<NonNullExpression>()->Expression;
	case Kind::TypeAssertionExpression:
		return as<TypeAssertion>()->Expression;
	case Kind::AsExpression:
		return as<AsExpression>()->Expression;
	case Kind::SatisfiesExpression:
		return as<SatisfiesExpression>()->Expression;
	case Kind::TypeOfExpression:
		return as<TypeOfExpression>()->Expression;
	case Kind::SpreadAssignment:
		return as<SpreadAssignment>()->Expression;
	case Kind::SpreadElement:
		return as<SpreadElement>()->Expression;
	case Kind::TemplateSpan:
		return as<TemplateSpan>()->Expression;
	case Kind::DeleteExpression:
		return as<DeleteExpression>()->Expression;
	case Kind::VoidExpression:
		return as<VoidExpression>()->Expression;
	case Kind::AwaitExpression:
		return as<AwaitExpression>()->Expression;
	case Kind::YieldExpression:
		return as<YieldExpression>()->Expression;
	case Kind::PartiallyEmittedExpression:
		return as<PartiallyEmittedExpression>()->Expression;
	case Kind::IfStatement:
		return as<IfStatement>()->Expression;
	case Kind::DoStatement:
		return as<DoStatement>()->Expression;
	case Kind::WhileStatement:
		return as<WhileStatement>()->Expression;
	case Kind::WithStatement:
		return as<WithStatement>()->Expression;
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		return as<ForInOrOfStatement>()->Expression;
	case Kind::SwitchStatement:
		return as<SwitchStatement>()->Expression;
	case Kind::CaseClause:
		return as<CaseOrDefaultClause>()->Expression;
	case Kind::ExpressionStatement:
		return as<ExpressionStatement>()->Expression;
	case Kind::ReturnStatement:
		return as<ReturnStatement>()->Expression;
	case Kind::ThrowStatement:
		return as<ThrowStatement>()->Expression;
	case Kind::ExternalModuleReference:
		return as<ExternalModuleReference>()->Expression;
	case Kind::ExportAssignment:
		return as<ExportAssignment>()->Expression;
	case Kind::Decorator:
		return as<Decorator>()->Expression;
	case Kind::JsxExpression:
		return as<JsxExpression>()->Expression;
	case Kind::JsxSpreadAttribute:
		return as<JsxSpreadAttribute>()->Expression;
	}
	TSC_UNREACHABLE("Unhandled case in Node::expression");
}

Node* Node::type() const {
	switch (kind) {
	case Kind::VariableDeclaration:
		return as<VariableDeclaration>()->Type;
	case Kind::Parameter:
		return as<ParameterDeclaration>()->Type;
	case Kind::PropertySignature:
		return as<PropertySignatureDeclaration>()->Type;
	case Kind::PropertyDeclaration:
		return as<PropertyDeclaration>()->Type;
	case Kind::PropertyAssignment:
		return as<PropertyAssignment>()->Type;
	case Kind::ShorthandPropertyAssignment:
		return as<ShorthandPropertyAssignment>()->Type;
	case Kind::TypePredicate:
		return as<TypePredicateNode>()->Type;
	case Kind::ParenthesizedType:
		return as<ParenthesizedTypeNode>()->Type;
	case Kind::TypeOperator:
		return as<TypeOperatorNode>()->Type;
	case Kind::MappedType:
		return as<MappedTypeNode>()->Type;
	case Kind::TypeAssertionExpression:
		return as<TypeAssertion>()->Type;
	case Kind::AsExpression:
		return as<AsExpression>()->Type;
	case Kind::SatisfiesExpression:
		return as<SatisfiesExpression>()->Type;
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
		return as<TypeAliasDeclaration>()->Type;
	case Kind::NamedTupleMember:
		return as<NamedTupleMember>()->Type;
	case Kind::OptionalType:
		return as<OptionalTypeNode>()->Type;
	case Kind::RestType:
		return as<RestTypeNode>()->Type;
	case Kind::TemplateLiteralTypeSpan:
		return as<TemplateLiteralTypeSpan>()->Type;
	case Kind::JSDocTypeExpression:
		return as<JSDocTypeExpression>()->Type;
	case Kind::JSDocParameterTag:
	case Kind::JSDocPropertyTag:
		return as<JSDocParameterOrPropertyTag>()->TypeExpression;
	case Kind::JSDocNullableType:
		return as<JSDocNullableType>()->Type;
	case Kind::JSDocNonNullableType:
		return as<JSDocNonNullableType>()->Type;
	case Kind::JSDocOptionalType:
		return as<JSDocOptionalType>()->Type;
	case Kind::ExportAssignment:
		return as<ExportAssignment>()->Type;
	case Kind::BinaryExpression:
		return as<BinaryExpression>()->Type;
	default: {
		auto d = const_cast<Node*>(this)->functionLikeData();
		if (d.type)
			return *d.type;
	}
	}
	return nullptr;
}

Node* Node::initializer() const {
	switch (kind) {
	case Kind::VariableDeclaration:
		return as<VariableDeclaration>()->Initializer;
	case Kind::Parameter:
		return as<ParameterDeclaration>()->Initializer;
	case Kind::BindingElement:
		return as<BindingElement>()->Initializer;
	case Kind::PropertyDeclaration:
		return as<PropertyDeclaration>()->Initializer;
	case Kind::PropertySignature:
		return as<PropertySignatureDeclaration>()->Initializer;
	case Kind::PropertyAssignment:
		return as<PropertyAssignment>()->Initializer;
	case Kind::EnumMember:
		return as<EnumMember>()->Initializer;
	case Kind::ForStatement:
		return as<ForStatement>()->Initializer;
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		return as<ForInOrOfStatement>()->Initializer;
	case Kind::JsxAttribute:
		return as<JsxAttribute>()->Initializer;
	}
	TSC_UNREACHABLE("Unhandled case in Node::initializer");
}

Node* Node::tagName() const {
	switch (kind) {
	case Kind::JsxOpeningElement:
		return as<JsxOpeningElement>()->TagName;
	case Kind::JsxClosingElement:
		return as<JsxClosingElement>()->TagName;
	case Kind::JsxSelfClosingElement:
		return as<JsxSelfClosingElement>()->TagName;
	case Kind::JSDocUnknownTag:
		return as<JSDocUnknownTag>()->TagName;
	case Kind::JSDocAugmentsTag:
		return as<JSDocAugmentsTag>()->TagName;
	case Kind::JSDocImplementsTag:
		return as<JSDocImplementsTag>()->TagName;
	case Kind::JSDocDeprecatedTag:
		return as<JSDocDeprecatedTag>()->TagName;
	case Kind::JSDocPublicTag:
		return as<JSDocPublicTag>()->TagName;
	case Kind::JSDocPrivateTag:
		return as<JSDocPrivateTag>()->TagName;
	case Kind::JSDocProtectedTag:
		return as<JSDocProtectedTag>()->TagName;
	case Kind::JSDocReadonlyTag:
		return as<JSDocReadonlyTag>()->TagName;
	case Kind::JSDocOverrideTag:
		return as<JSDocOverrideTag>()->TagName;
	case Kind::JSDocCallbackTag:
		return as<JSDocCallbackTag>()->TagName;
	case Kind::JSDocOverloadTag:
		return as<JSDocOverloadTag>()->TagName;
	case Kind::JSDocParameterTag:
	case Kind::JSDocPropertyTag:
		return as<JSDocParameterOrPropertyTag>()->TagName;
	case Kind::JSDocReturnTag:
		return as<JSDocReturnTag>()->TagName;
	case Kind::JSDocThisTag:
		return as<JSDocThisTag>()->TagName;
	case Kind::JSDocTypeTag:
		return as<JSDocTypeTag>()->TagName;
	case Kind::JSDocTemplateTag:
		return as<JSDocTemplateTag>()->TagName;
	case Kind::JSDocTypedefTag:
		return as<JSDocTypedefTag>()->TagName;
	case Kind::JSDocSeeTag:
		return as<JSDocSeeTag>()->TagName;
	case Kind::JSDocSatisfiesTag:
		return as<JSDocSatisfiesTag>()->TagName;
	case Kind::JSDocThrowsTag:
		return as<JSDocThrowsTag>()->TagName;
	case Kind::JSDocImportTag:
		return as<JSDocImportTag>()->TagName;
	}
	TSC_UNREACHABLE("Unhandled case in Node::tagName");
}

Node* Node::questionToken() const {
	switch (kind) {
	case Kind::Parameter:
		return as<ParameterDeclaration>()->QuestionToken;
	case Kind::ConditionalExpression:
		return as<ConditionalExpression>()->QuestionToken;
	case Kind::MappedType:
		return as<MappedTypeNode>()->QuestionToken;
	case Kind::NamedTupleMember:
		return as<NamedTupleMember>()->QuestionToken;
	}
	if (Node* postfix = postfixToken();
	    postfix != nullptr && postfix->kind == Kind::QuestionToken) {
		return postfix;
	}
	return nullptr;
}

Node* Node::postfixToken() const {
	switch (kind) {
	case Kind::MethodDeclaration:
		return as<MethodDeclaration>()->PostfixToken;
	case Kind::ShorthandPropertyAssignment:
		return as<ShorthandPropertyAssignment>()->PostfixToken;
	case Kind::MethodSignature:
		return as<MethodSignatureDeclaration>()->PostfixToken;
	case Kind::PropertySignature:
		return as<PropertySignatureDeclaration>()->PostfixToken;
	case Kind::PropertyAssignment:
		return as<PropertyAssignment>()->PostfixToken;
	case Kind::PropertyDeclaration:
		return as<PropertyDeclaration>()->PostfixToken;
	case Kind::EnumMember:
		return as<EnumMember>()->PostfixToken;
	case Kind::GetAccessor:
		return as<GetAccessorDeclaration>()->PostfixToken;
	case Kind::SetAccessor:
		return as<SetAccessorDeclaration>()->PostfixToken;
	}
	return nullptr;
}

Node* Node::questionDotToken() const {
	switch (kind) {
	case Kind::ElementAccessExpression:
		return as<ElementAccessExpression>()->QuestionDotToken;
	case Kind::PropertyAccessExpression:
		return as<PropertyAccessExpression>()->QuestionDotToken;
	case Kind::CallExpression:
		return as<CallExpression>()->QuestionDotToken;
	case Kind::TaggedTemplateExpression:
		return as<TaggedTemplateExpression>()->QuestionDotToken;
	}
	TSC_UNREACHABLE("Unhandled case in Node::questionDotToken");
}

Node* Node::propertyName() const {
	switch (kind) {
	case Kind::ImportSpecifier:
		return as<ImportSpecifier>()->PropertyName;
	case Kind::ExportSpecifier:
		return as<ExportSpecifier>()->PropertyName;
	case Kind::BindingElement:
		return as<BindingElement>()->PropertyName;
	}
	return nullptr;
}

Node* Node::propertyNameOrName() const {
	if (Node* pn = propertyName())
		return pn;
	if (auto* d = const_cast<Node*>(this)->name(); d != nullptr)
		return d;
	return nullptr;
}

Node* Node::label() const {
	switch (kind) {
	case Kind::LabeledStatement:
		return as<LabeledStatement>()->Label;
	case Kind::BreakStatement:
		return as<BreakStatement>()->Label;
	case Kind::ContinueStatement:
		return as<ContinueStatement>()->Label;
	}
	TSC_UNREACHABLE("Unhandled case in Node::label");
}

Node* Node::attributes() const {
	switch (kind) {
	case Kind::JsxOpeningElement:
		return as<JsxOpeningElement>()->Attributes;
	case Kind::JsxSelfClosingElement:
		return as<JsxSelfClosingElement>()->Attributes;
	case Kind::ModuleDeclaration:
		return as<ModuleDeclaration>()->Attributes;
	}
	TSC_UNREACHABLE("Unhandled case in Node::attributes");
}

Node* Node::moduleSpecifier() const {
	switch (kind) {
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
		return as<ImportDeclaration>()->ModuleSpecifier;
	case Kind::ExportDeclaration:
		return as<ExportDeclaration>()->ModuleSpecifier;
	case Kind::JSDocImportTag:
		return as<JSDocImportTag>()->ModuleSpecifier;
	}
	TSC_UNREACHABLE("Unhandled case in Node::moduleSpecifier");
}

Node* Node::importClause() const {
	switch (kind) {
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
		return as<ImportDeclaration>()->ImportClause;
	case Kind::JSDocImportTag:
		return as<JSDocImportTag>()->ImportClause;
	}
	TSC_UNREACHABLE("Unhandled case in Node::importClause");
}

Node* Node::statement() const {
	switch (kind) {
	case Kind::LabeledStatement:
		return as<LabeledStatement>()->Statement;
	case Kind::IfStatement:
		return as<IfStatement>()->ThenStatement;
	case Kind::DoStatement:
		return as<DoStatement>()->Statement;
	case Kind::WhileStatement:
		return as<WhileStatement>()->Statement;
	case Kind::ForStatement:
		return as<ForStatement>()->Statement;
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		return as<ForInOrOfStatement>()->Statement;
	case Kind::WithStatement:
		return as<WithStatement>()->Statement;
	case Kind::ModuleDeclaration:
		return as<ModuleDeclaration>()->Body;
	}
	TSC_UNREACHABLE("Unhandled case in Node::statement");
}

Node* Node::typeExpression() const {
	switch (kind) {
	case Kind::JSDocParameterTag:
	case Kind::JSDocPropertyTag:
		return as<JSDocParameterOrPropertyTag>()->TypeExpression;
	case Kind::JSDocReturnTag:
		return as<JSDocReturnTag>()->TypeExpression;
	case Kind::JSDocTypeTag:
		return as<JSDocTypeTag>()->TypeExpression;
	case Kind::JSDocTypedefTag:
		return as<JSDocTypedefTag>()->TypeExpression;
	case Kind::JSDocCallbackTag:
		return as<JSDocCallbackTag>()->TypeExpression;
	case Kind::JSDocSatisfiesTag:
		return as<JSDocSatisfiesTag>()->TypeExpression;
	case Kind::JSDocThrowsTag:
		return as<JSDocThrowsTag>()->TypeExpression;
	}
	TSC_UNREACHABLE("Unhandled case in Node::typeExpression");
}

Node* Node::className() const {
	switch (kind) {
	case Kind::JSDocAugmentsTag:
		return as<JSDocAugmentsTag>()->ClassName;
	case Kind::JSDocImplementsTag:
		return as<JSDocImplementsTag>()->ClassName;
	}
	TSC_UNREACHABLE("Unhandled case in Node::className");
}

NodeList* Node::argumentList() const {
	switch (kind) {
	case Kind::CallExpression:
		return as<CallExpression>()->Arguments;
	case Kind::NewExpression:
		return as<NewExpression>()->Arguments;
	}
	TSC_UNREACHABLE("Unhandled case in Node::argumentList");
}

std::vector<Node*> Node::arguments() const {
	if (NodeList* l = argumentList())
		return l->nodes;
	return {};
}

NodeList* Node::typeArgumentList() const {
	switch (kind) {
	case Kind::CallExpression:
		return as<CallExpression>()->TypeArguments;
	case Kind::NewExpression:
		return as<NewExpression>()->TypeArguments;
	case Kind::TaggedTemplateExpression:
		return as<TaggedTemplateExpression>()->TypeArguments;
	case Kind::TypeReference:
		return as<TypeReferenceNode>()->TypeArguments;
	case Kind::ExpressionWithTypeArguments:
		return as<ExpressionWithTypeArguments>()->TypeArguments;
	case Kind::ImportType:
		return as<ImportTypeNode>()->TypeArguments;
	case Kind::TypeQuery:
		return as<TypeQueryNode>()->TypeArguments;
	case Kind::JsxOpeningElement:
		return as<JsxOpeningElement>()->TypeArguments;
	case Kind::JsxSelfClosingElement:
		return as<JsxSelfClosingElement>()->TypeArguments;
	}
	TSC_UNREACHABLE("Unhandled case in Node::typeArgumentList");
}

std::vector<Node*> Node::typeArguments() const {
	if (NodeList* l = typeArgumentList())
		return l->nodes;
	return {};
}

NodeList* Node::typeParameterList() const {
	switch (kind) {
	case Kind::ClassDeclaration:
		return as<ClassDeclaration>()->TypeParameters;
	case Kind::ClassExpression:
		return as<ClassExpression>()->TypeParameters;
	case Kind::InterfaceDeclaration:
		return as<InterfaceDeclaration>()->TypeParameters;
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
		return as<TypeAliasDeclaration>()->TypeParameters;
	case Kind::JSDocTemplateTag:
		return as<JSDocTemplateTag>()->TypeParameters;
	default: {
		auto d = const_cast<Node*>(this)->functionLikeData();
		if (d.typeParameters)
			return *d.typeParameters;
	}
	}
	TSC_UNREACHABLE("Unhandled case in Node::typeParameterList");
}

std::vector<Node*> Node::typeParameters() const {
	if (NodeList* l = typeParameterList())
		return l->nodes;
	return {};
}

NodeList* Node::memberList() const {
	switch (kind) {
	case Kind::ClassDeclaration:
		return as<ClassDeclaration>()->Members;
	case Kind::ClassExpression:
		return as<ClassExpression>()->Members;
	case Kind::InterfaceDeclaration:
		return as<InterfaceDeclaration>()->Members;
	case Kind::EnumDeclaration:
		return as<EnumDeclaration>()->Members;
	case Kind::TypeLiteral:
		return as<TypeLiteralNode>()->Members;
	case Kind::MappedType:
		return as<MappedTypeNode>()->Members;
	}
	TSC_UNREACHABLE("Unhandled case in Node::memberList");
}

NodeList* Node::children() const {
	switch (kind) {
	case Kind::JsxElement:
		return as<JsxElement>()->Children;
	case Kind::JsxFragment:
		return as<JsxFragment>()->Children;
	}
	TSC_UNREACHABLE("Unhandled case in Node::children");
}

std::vector<Node*> Node::members() const {
	if (NodeList* l = memberList())
		return l->nodes;
	return {};
}

NodeList* Node::statementList() const {
	switch (kind) {
	case Kind::SourceFile:
		return as<SourceFile>()->Statements;
	case Kind::Block:
		return as<Block>()->Statements;
	case Kind::ModuleBlock:
		return as<ModuleBlock>()->Statements;
	case Kind::CaseClause:
	case Kind::DefaultClause:
		return as<CaseOrDefaultClause>()->Statements;
	}
	TSC_UNREACHABLE("Unhandled case in Node::statementList");
}

std::vector<Node*> Node::statements() const {
	if (NodeList* l = statementList())
		return l->nodes;
	return {};
}

bool Node::canHaveStatements() const {
	switch (kind) {
	case Kind::SourceFile:
	case Kind::Block:
	case Kind::ModuleBlock:
	case Kind::CaseClause:
	case Kind::DefaultClause:
		return true;
	default:
		return false;
	}
}

NodeList* Node::elementList() const {
	switch (kind) {
	case Kind::NamedImports:
		return as<NamedImports>()->Elements;
	case Kind::NamedExports:
		return as<NamedExports>()->Elements;
	case Kind::ObjectBindingPattern:
	case Kind::ArrayBindingPattern:
		return as<BindingPattern>()->Elements;
	case Kind::ArrayLiteralExpression:
		return as<ArrayLiteralExpression>()->Elements;
	case Kind::TupleType:
		return as<TupleTypeNode>()->Elements;
	}
	TSC_UNREACHABLE("Unhandled case in Node::elementList");
}

std::vector<Node*> Node::elements() const {
	if (NodeList* l = elementList())
		return l->nodes;
	return {};
}

NodeList* Node::propertyList() const {
	switch (kind) {
	case Kind::ObjectLiteralExpression:
		return as<ObjectLiteralExpression>()->Properties;
	case Kind::JsxAttributes:
		return as<JsxAttributes>()->Properties;
	}
	TSC_UNREACHABLE("Unhandled case in Node::propertyList");
}

std::vector<Node*> Node::properties() const {
	if (NodeList* l = propertyList())
		return l->nodes;
	return {};
}

NodeList* Node::commentList() const {
	switch (kind) {
	case Kind::JSDoc:
		return as<JSDoc>()->Comment;
	case Kind::JSDocUnknownTag:
		return as<JSDocUnknownTag>()->Comment;
	case Kind::JSDocAugmentsTag:
		return as<JSDocAugmentsTag>()->Comment;
	case Kind::JSDocImplementsTag:
		return as<JSDocImplementsTag>()->Comment;
	case Kind::JSDocDeprecatedTag:
		return as<JSDocDeprecatedTag>()->Comment;
	case Kind::JSDocPublicTag:
		return as<JSDocPublicTag>()->Comment;
	case Kind::JSDocPrivateTag:
		return as<JSDocPrivateTag>()->Comment;
	case Kind::JSDocProtectedTag:
		return as<JSDocProtectedTag>()->Comment;
	case Kind::JSDocReadonlyTag:
		return as<JSDocReadonlyTag>()->Comment;
	case Kind::JSDocOverrideTag:
		return as<JSDocOverrideTag>()->Comment;
	case Kind::JSDocCallbackTag:
		return as<JSDocCallbackTag>()->Comment;
	case Kind::JSDocOverloadTag:
		return as<JSDocOverloadTag>()->Comment;
	case Kind::JSDocParameterTag:
	case Kind::JSDocPropertyTag:
		return as<JSDocParameterOrPropertyTag>()->Comment;
	case Kind::JSDocReturnTag:
		return as<JSDocReturnTag>()->Comment;
	case Kind::JSDocThisTag:
		return as<JSDocThisTag>()->Comment;
	case Kind::JSDocTypeTag:
		return as<JSDocTypeTag>()->Comment;
	case Kind::JSDocTemplateTag:
		return as<JSDocTemplateTag>()->Comment;
	case Kind::JSDocTypedefTag:
		return as<JSDocTypedefTag>()->Comment;
	case Kind::JSDocSeeTag:
		return as<JSDocSeeTag>()->Comment;
	case Kind::JSDocSatisfiesTag:
		return as<JSDocSatisfiesTag>()->Comment;
	case Kind::JSDocThrowsTag:
		return as<JSDocThrowsTag>()->Comment;
	case Kind::JSDocImportTag:
		return as<JSDocImportTag>()->Comment;
	}
	TSC_UNREACHABLE("Unhandled case in Node::commentList");
}

std::vector<Node*> Node::comments() const {
	if (NodeList* l = commentList())
		return l->nodes;
	return {};
}

NodeList* Node::parameterList() const {
	auto d = const_cast<Node*>(this)->functionLikeData();
	return d.parameters ? *d.parameters : nullptr;
}

std::vector<Node*> Node::parameters() const {
	if (NodeList* l = parameterList())
		return l->nodes;
	return {};
}

// ---------------------------------------------------------------------------
// modifiersToFlags — utilities.go:990/1028
// ---------------------------------------------------------------------------


ModifierFlags NodeFactory::modifiersToFlags(const std::vector<Node*>& modifiers) {
	ModifierFlags flags = ModifierFlagsNone;
	for (Node* m : modifiers) {
		flags = static_cast<ModifierFlags>(flags | modifierToFlag(m->kind));
	}
	return flags;
}

// ---------------------------------------------------------------------------
// Small predicates — utilities.go
// ---------------------------------------------------------------------------

bool isThisIdentifier(const Node* node) {
	return isIdentifier(node) && node->text() == "this";
}

bool isAssignmentPattern(const Node* node) {
	return node->kind == Kind::ArrayLiteralExpression ||
	       node->kind == Kind::ObjectLiteralExpression;
}

// ---------------------------------------------------------------------------
// containsObjectRestOrSpread — utilities.go:4014
// ---------------------------------------------------------------------------

static Node* getTargetOfBindingOrAssignmentElement(Node* element);

bool containsObjectRestOrSpread(const Node* node) {
	if (node->subtreeFacts() & SubtreeContainsObjectRestOrSpread) {
		return true;
	}
	if (!(node->subtreeFacts() & SubtreeContainsESObjectRestOrSpread)) {
		return false;
	}
	// check for nested spread assignments — '{ x: { a, ...b } = foo } = c'
	std::vector<Node*> elems;
	switch (node->kind) {
	case Kind::ObjectBindingPattern:
	case Kind::ArrayBindingPattern:
	case Kind::ArrayLiteralExpression:
		elems = const_cast<Node*>(node)->elements();
		break;
	case Kind::ObjectLiteralExpression:
		elems = const_cast<Node*>(node)->properties();
		break;
	default:
		break;
	}
	for (Node* element : elems) {
		Node* target = getTargetOfBindingOrAssignmentElement(element);
		if (target != nullptr && isAssignmentPattern(target)) {
			if (target->subtreeFacts() & SubtreeContainsObjectRestOrSpread) {
				return true;
			}
			if (target->subtreeFacts() & SubtreeContainsESObjectRestOrSpread) {
				if (containsObjectRestOrSpread(target)) {
					return true;
				}
			}
		}
	}
	return false;
}

static bool isDeclarationBindingElement(Node* element) {
	switch (element->kind) {
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::BindingElement:
		return true;
	default:
		return false;
	}
}

static bool isObjectLiteralElement(Node* element) {
	switch (element->kind) {
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::SpreadAssignment:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		return true;
	default:
		return false;
	}
}

static Node* getTargetOfBindingOrAssignmentElement(Node* element) {
	if (isDeclarationBindingElement(element)) {
		return element->name();
	}
	if (isObjectLiteralElement(element)) {
		switch (element->kind) {
		case Kind::PropertyAssignment:
			return getTargetOfBindingOrAssignmentElement(
				element->initializer());
		case Kind::ShorthandPropertyAssignment:
			return element->name();
		case Kind::SpreadAssignment:
			return getTargetOfBindingOrAssignmentElement(
				element->expression());
		default:
			return nullptr;
		}
	}
	return nullptr;
}

std::vector<Node*> (*parseJSDocForNode)(SourceFile*, Node*) = nullptr;

bool nodeIsMissing(const Node* node) {
	return node == nullptr || (node->loc.pos() == node->loc.end() &&
	                           node->loc.pos() >= 0 &&
	                           node->kind != Kind::EndOfFile);
}

bool nodeIsSynthesized(const Node* node) {
	return positionIsSynthesized(node->loc.pos()) ||
	       positionIsSynthesized(node->loc.end());
}

const SourceFile* getSourceFileOfNode(const Node* node) {
	while (node != nullptr) {
		if (node->kind == Kind::SourceFile)
			return node->as<SourceFile>();
		node = node->parent;
	}
	return nullptr;
}

const std::vector<TextPos>& SourceFile::ecmaLineMap() {
	std::shared_lock lk(ecmaLineMapMu);
	if (!ecmaLineMap_.empty())
		return ecmaLineMap_;
	lk.unlock();
	std::unique_lock ulk(ecmaLineMapMu);
	if (ecmaLineMap_.empty())
		ecmaLineMap_ = computeECMALineStarts(text);
	return ecmaLineMap_;
}

std::vector<Node*> SourceFile::resolveJSDoc(Node* n) {
	{
		std::shared_lock lk(jsdocMu);
		auto it = jsdocCache.find(n);
		if (it != jsdocCache.end())
			return it->second;
	}
	std::unique_lock ulk(jsdocMu);
	auto it = jsdocCache.find(n);
	if (it != jsdocCache.end())
		return it->second;
	std::vector<Node*> jsdocs;
	if (parseJSDocForNode != nullptr)
		jsdocs = parseJSDocForNode(this, n);
	jsdocCache[n] = jsdocs;
	return jsdocs;
}

std::vector<Node*> Node::jsDoc(SourceFile* file) {
	if (!(flags & NodeFlagsHasJSDoc))
		return {};
	if (file == nullptr) {
		file = getSourceFileOfNode(this);
		if (file == nullptr)
			return {};
	}
	if (file->hasLazyJSDoc)
		return file->resolveJSDoc(this);
	return file->jsdocCache[this];
}

std::vector<Node*> Node::eagerJSDoc(SourceFile* file) {
	if (!(flags & NodeFlagsHasJSDoc))
		return {};
	if (file == nullptr) {
		file = getSourceFileOfNode(this);
		if (file == nullptr)
			return {};
	}
	std::shared_lock lk(file->jsdocMu);
	auto it = file->jsdocCache.find(this);
	return it != file->jsdocCache.end() ? it->second : std::vector<Node*>();
}

// ---------------------------------------------------------------------------
// diagnostic.go
// ---------------------------------------------------------------------------

static Diagnostic* makeDiagnostic(SourceFile* file, TextRange loc,
                                  const DiagnosticMessage* message,
                                  const std::vector<std::string>& args) {
	auto* d = new Diagnostic();
	d->file = file;
	d->loc = loc;
	d->code = message->code;
	d->category = message->category;
	d->message = message;
	d->messageKey = message->key;
	d->messageArgs = args;
	d->reportsUnnecessary = message->ReportsUnnecessary();
	d->reportsDeprecated = message->ReportsDeprecated();
	return d;
}

Diagnostic* newDiagnostic(SourceFile* file, TextRange loc,
                        const DiagnosticMessage* message,
                        const std::vector<std::string>& args) {
	return makeDiagnostic(file, loc, message, args);
}

Diagnostic* newDetachedDiagnostic(TextRange loc,
                                  const DiagnosticMessage* message,
                                  const std::vector<std::string>& args) {
	return makeDiagnostic(nullptr, loc, message, args);
}

Diagnostic* newDiagnosticFromText(SourceFile* file, TextRange loc,
                                  int32_t code, DiagnosticCategory category,
                                  std::string_view text) {
	auto* d = new Diagnostic();
	d->file = file;
	d->loc = loc;
	d->code = code;
	d->category = category;
	d->messageText = text;
	return d;
}

// ---------------------------------------------------------------------------
// utilities.go — name-of-declaration resolution and support predicates
// ---------------------------------------------------------------------------

bool isTypeNodeKind(Kind kind) {
	switch (kind) {
	case Kind::AnyKeyword:
	case Kind::UnknownKeyword:
	case Kind::NumberKeyword:
	case Kind::BigIntKeyword:
	case Kind::ObjectKeyword:
	case Kind::BooleanKeyword:
	case Kind::StringKeyword:
	case Kind::SymbolKeyword:
	case Kind::VoidKeyword:
	case Kind::UndefinedKeyword:
	case Kind::NeverKeyword:
	case Kind::IntrinsicKeyword:
	case Kind::ExpressionWithTypeArguments:
	case Kind::JSDocAllType:
	case Kind::JSDocNullableType:
	case Kind::JSDocNonNullableType:
	case Kind::JSDocOptionalType:
	case Kind::JSDocVariadicType:
		return true;
	}
	return kind >= KindFirstTypeNode && kind <= KindLastTypeNode;
}

bool isStringLiteralLike(Node* node) {
	return node->kind == Kind::StringLiteral ||
	       node->kind == Kind::NoSubstitutionTemplateLiteral;
}

bool isStringOrNumericLiteralLike(Node* node) {
	return isStringLiteralLike(node) || isNumericLiteral(node);
}


bool isAccessExpression(Node* node) {
	return node->kind == Kind::PropertyAccessExpression ||
	       node->kind == Kind::ElementAccessExpression;
}

static bool isPropertyAccessEntityNameExpression(Node* node, bool allowJS);
static bool isElementAccessEntityNameExpression(Node* node, bool allowJS);

static bool isEntityNameExpressionEx(Node* node, bool allowJS) {
	return isIdentifier(node) ||
	       isPropertyAccessEntityNameExpression(node, allowJS) ||
	       (allowJS && (node->kind == Kind::ThisKeyword ||
	                    isElementAccessEntityNameExpression(node, allowJS)));
}

bool isEntityNameExpression(Node* node) {
	return isEntityNameExpressionEx(node, false);
}

static bool isPropertyAccessEntityNameExpression(Node* node, bool allowJS) {
	return isPropertyAccessExpression(node) && isIdentifier(node->name()) &&
	       isEntityNameExpressionEx(node->expression(), allowJS);
}

static bool isElementAccessEntityNameExpression(Node* node, bool allowJS) {
	return isElementAccessExpression(node) &&
	       isStringOrNumericLiteralLike(
		       node->as<ElementAccessExpression>()->ArgumentExpression) &&
	       isEntityNameExpressionEx(node->expression(), allowJS);
}

bool isInJSFile(Node* node) {
	return node != nullptr && (node->flags & NodeFlagsJavaScriptFile) != 0;
}

bool isExportsIdentifier(Node* node) {
	return isIdentifier(node) && node->text() == "exports";
}

bool isModuleIdentifier(Node* node) {
	return isIdentifier(node) && node->text() == "module";
}

Node* getElementOrPropertyAccessName(Node* node) {
	switch (node->kind) {
	case Kind::PropertyAccessExpression:
		if (isIdentifier(node->name()))
			return node->name();
		return nullptr;
	case Kind::ElementAccessExpression:
		if (Node* arg = skipParentheses(
		        node->as<ElementAccessExpression>()->ArgumentExpression);
		    isStringOrNumericLiteralLike(arg))
			return arg;
		return nullptr;
	}
	return nullptr;
}

bool isModuleExportsAccessExpression(Node* node) {
	if (isAccessExpression(node) && isModuleIdentifier(node->expression())) {
		if (Node* name = getElementOrPropertyAccessName(node);
		    name != nullptr)
			return name->text() == "exports";
	}
	return false;
}

static bool isBindableObjectDefinePropertyCall(Node* node) {
	auto args = node->arguments();
	if (args.size() == 3) {
		if (Node* expr = node->expression();
		    isPropertyAccessExpression(expr) &&
		    isIdentifier(expr->expression()) &&
		    expr->expression()->text() == "Object" &&
		    expr->name()->text() == "defineProperty" &&
		    isStringOrNumericLiteralLike(args[1]) &&
		    isBindableStaticNameExpression(args[0], true)) {
			return true;
		}
	}
	return false;
}

bool isAssignmentExpression(Node* node, bool excludeCompoundAssignment) {
	if (node->kind == Kind::BinaryExpression) {
		auto* expr = node->as<BinaryExpression>();
		return (expr->OperatorToken->kind == Kind::EqualsToken ||
		        (!excludeCompoundAssignment &&
		         isAssignmentOperator(expr->OperatorToken->kind))) &&
		       isLeftHandSideExpression(expr->Left);
	}
	return false;
}

JSDeclarationKind getAssignmentDeclarationKind(Node* node) {
	switch (node->kind) {
	case Kind::BinaryExpression: {
		auto* bin = node->as<BinaryExpression>();
		if (bin->OperatorToken->kind == Kind::EqualsToken &&
		    isAccessExpression(bin->Left)) {
			if (isInJSFile(bin->Left)) {
				if (isModuleExportsAccessExpression(bin->Left) &&
				    !isExportsIdentifier(bin->Right))
					return JSDeclarationKind::ModuleExports;
				if ((isModuleExportsAccessExpression(bin->Left->expression()) ||
				     isExportsIdentifier(bin->Left->expression())) &&
				    getElementOrPropertyAccessName(bin->Left) != nullptr)
					return JSDeclarationKind::ExportsProperty;
				if (bin->Left->expression()->kind == Kind::ThisKeyword)
					return JSDeclarationKind::ThisProperty;
			}
			if ((bin->Left->kind == Kind::PropertyAccessExpression &&
			     isEntityNameExpressionEx(bin->Left->expression(),
			                              isInJSFile(bin->Left)) &&
			     isIdentifier(bin->Left->name())) ||
			    (bin->Left->kind == Kind::ElementAccessExpression &&
			     isEntityNameExpressionEx(bin->Left->expression(),
			                              isInJSFile(bin->Left))))
				return JSDeclarationKind::Property;
		}
		break;
	}
	case Kind::CallExpression:
		if (isInJSFile(node) && isBindableObjectDefinePropertyCall(node)) {
			Node* entityName = node->arguments()[0];
			if (isExportsIdentifier(entityName) ||
			    isModuleExportsAccessExpression(entityName))
				return JSDeclarationKind::ObjectDefinePropertyExports;
			return JSDeclarationKind::ObjectDefinePropertyValue;
		}
		break;
	}
	return JSDeclarationKind::None;
}

static Node* getNonAssignedNameOfDeclaration(Node* declaration) {
	switch (declaration->kind) {
	case Kind::BinaryExpression:
	case Kind::CallExpression:
		switch (getAssignmentDeclarationKind(declaration)) {
		case JSDeclarationKind::Property:
		case JSDeclarationKind::ThisProperty:
		case JSDeclarationKind::ExportsProperty: {
			Node* left = declaration->as<BinaryExpression>()->Left;
			if (Node* name = getElementOrPropertyAccessName(left);
			    name != nullptr)
				return name;
			return left;
		}
		case JSDeclarationKind::ObjectDefinePropertyValue:
		case JSDeclarationKind::ObjectDefinePropertyExports:
			return declaration->arguments()[1];
		default:
			return nullptr;
		}
	case Kind::ExportAssignment: {
		Node* expr = declaration->expression();
		if (isIdentifier(expr))
			return expr;
		return nullptr;
	}
	default:
		return declaration->name();
	}
}

Node* getNameOfDeclaration(Node* declaration) {
	if (declaration == nullptr)
		return nullptr;
	if (Node* nonAssignedName = getNonAssignedNameOfDeclaration(declaration);
	    nonAssignedName != nullptr)
		return nonAssignedName;
	if (isFunctionExpression(declaration) || isArrowFunction(declaration) ||
	    isClassExpression(declaration))
		return getAssignedName(declaration);
	return nullptr;
}

Node* getAssignedName(Node* node) {
	Node* parent = node->parent;
	if (parent != nullptr) {
		switch (parent->kind) {
		case Kind::PropertyAssignment:
			return parent->name();
		case Kind::BindingElement:
			return parent->name();
		case Kind::BinaryExpression:
			if (node == parent->as<BinaryExpression>()->Right) {
				Node* left = parent->as<BinaryExpression>()->Left;
				switch (left->kind) {
				case Kind::Identifier:
					return left;
				case Kind::PropertyAccessExpression:
					return left->name();
				case Kind::ElementAccessExpression:
					if (Node* arg = skipParentheses(
					        left->as<ElementAccessExpression>()
					            ->ArgumentExpression);
					    isStringOrNumericLiteralLike(arg))
						return arg;
					break;
				}
			}
			break;
		case Kind::VariableDeclaration: {
			Node* name = parent->name();
			if (isIdentifier(name))
				return name;
			break;
		}
		default:
			break;
		}
	}
	return nullptr;
}



// ---------------------------------------------------------------------------
// precedence.go
// ---------------------------------------------------------------------------

Kind getOperatorOfExpression(Node* expression) {
	switch (expression->kind) {
	case Kind::BinaryExpression:
		return expression->as<BinaryExpression>()->OperatorToken->kind;
	case Kind::PrefixUnaryExpression:
		return expression->as<PrefixUnaryExpression>()->Operator;
	case Kind::PostfixUnaryExpression:
		return expression->as<PostfixUnaryExpression>()->Operator;
	default:
		return expression->kind;
	}
}

OperatorPrecedence getExpressionPrecedence(Node* expression) {
	Kind op = getOperatorOfExpression(expression);
	OperatorPrecedenceFlags flags = OperatorPrecedenceFlagsNone;
	if (expression->kind == Kind::NewExpression && !expression->argumentList()) {
		flags = OperatorPrecedenceFlagsNewWithoutArguments;
	} else if (isOptionalChain(expression)) {
		flags = OperatorPrecedenceFlagsOptionalChain;
	}
	return getOperatorPrecedence(expression->kind, op, flags);
}

OperatorPrecedence getOperatorPrecedence(Kind nodeKind, Kind operatorKind,
                                       OperatorPrecedenceFlags flags) {
	switch (nodeKind) {
	case Kind::SpreadElement:
		return OperatorPrecedenceSpread;
	case Kind::YieldExpression:
		return OperatorPrecedenceYield;
	case Kind::ArrowFunction:
		return OperatorPrecedenceAssignment;
	case Kind::ConditionalExpression:
		return OperatorPrecedenceConditional;
	case Kind::BinaryExpression:
		switch (operatorKind) {
		case Kind::CommaToken:
			return OperatorPrecedenceComma;
		case Kind::EqualsToken:
		case Kind::PlusEqualsToken:
		case Kind::MinusEqualsToken:
		case Kind::AsteriskAsteriskEqualsToken:
		case Kind::AsteriskEqualsToken:
		case Kind::SlashEqualsToken:
		case Kind::PercentEqualsToken:
		case Kind::LessThanLessThanEqualsToken:
		case Kind::GreaterThanGreaterThanEqualsToken:
		case Kind::GreaterThanGreaterThanGreaterThanEqualsToken:
		case Kind::AmpersandEqualsToken:
		case Kind::CaretEqualsToken:
		case Kind::BarEqualsToken:
		case Kind::BarBarEqualsToken:
		case Kind::AmpersandAmpersandEqualsToken:
		case Kind::QuestionQuestionEqualsToken:
			return OperatorPrecedenceAssignment;
		default:
			return getBinaryOperatorPrecedence(operatorKind);
		}
	case Kind::TypeAssertionExpression:
	case Kind::NonNullExpression:
	case Kind::PrefixUnaryExpression:
	case Kind::TypeOfExpression:
	case Kind::VoidExpression:
	case Kind::DeleteExpression:
	case Kind::AwaitExpression:
		return OperatorPrecedenceUnary;
	case Kind::PostfixUnaryExpression:
		return OperatorPrecedenceUpdate;
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
		if ((flags & OperatorPrecedenceFlagsOptionalChain) != OperatorPrecedenceFlagsNone)
			return OperatorPrecedenceOptionalChain;
		return OperatorPrecedenceMember;
	case Kind::CallExpression:
		if ((flags & OperatorPrecedenceFlagsOptionalChain) != OperatorPrecedenceFlagsNone)
			return OperatorPrecedenceOptionalChain;
		return OperatorPrecedenceMember;
	case Kind::NewExpression:
		if ((flags & OperatorPrecedenceFlagsNewWithoutArguments) != OperatorPrecedenceFlagsNone)
			return OperatorPrecedenceLeftHandSide;
		return OperatorPrecedenceMember;
	case Kind::TaggedTemplateExpression:
	case Kind::MetaProperty:
	case Kind::ExpressionWithTypeArguments:
		return OperatorPrecedenceMember;
	case Kind::AsExpression:
	case Kind::SatisfiesExpression:
		return OperatorPrecedenceRelational;
	case Kind::ThisKeyword:
	case Kind::SuperKeyword:
	case Kind::ImportKeyword:
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
	case Kind::NullKeyword:
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::StringLiteral:
	case Kind::ArrayLiteralExpression:
	case Kind::ObjectLiteralExpression:
	case Kind::FunctionExpression:
	case Kind::ClassExpression:
	case Kind::RegularExpressionLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::TemplateExpression:
	case Kind::OmittedExpression:
	case Kind::JsxElement:
	case Kind::JsxSelfClosingElement:
	case Kind::JsxFragment:
	case Kind::MissingDeclaration:
		return OperatorPrecedencePrimary;
	case Kind::ParenthesizedExpression:
		return OperatorPrecedenceParentheses;
	default:
		return OperatorPrecedenceInvalid;
	}
}

OperatorPrecedence getBinaryOperatorPrecedence(Kind operatorKind) {
	switch (operatorKind) {
	case Kind::QuestionQuestionToken:
		return OperatorPrecedenceCoalesce;
	case Kind::BarBarToken:
		return OperatorPrecedenceLogicalOR;
	case Kind::AmpersandAmpersandToken:
		return OperatorPrecedenceLogicalAND;
	case Kind::BarToken:
		return OperatorPrecedenceBitwiseOR;
	case Kind::CaretToken:
		return OperatorPrecedenceBitwiseXOR;
	case Kind::AmpersandToken:
		return OperatorPrecedenceBitwiseAND;
	case Kind::EqualsEqualsToken:
	case Kind::ExclamationEqualsToken:
	case Kind::EqualsEqualsEqualsToken:
	case Kind::ExclamationEqualsEqualsToken:
		return OperatorPrecedenceEquality;
	case Kind::LessThanToken:
	case Kind::GreaterThanToken:
	case Kind::LessThanEqualsToken:
	case Kind::GreaterThanEqualsToken:
	case Kind::InstanceOfKeyword:
	case Kind::InKeyword:
	case Kind::AsKeyword:
	case Kind::SatisfiesKeyword:
		return OperatorPrecedenceRelational;
	case Kind::LessThanLessThanToken:
	case Kind::GreaterThanGreaterThanToken:
	case Kind::GreaterThanGreaterThanGreaterThanToken:
		return OperatorPrecedenceShift;
	case Kind::PlusToken:
	case Kind::MinusToken:
		return OperatorPrecedenceAdditive;
	case Kind::AsteriskToken:
	case Kind::SlashToken:
	case Kind::PercentToken:
		return OperatorPrecedenceMultiplicative;
	case Kind::AsteriskAsteriskToken:
		return OperatorPrecedenceExponentiation;
	default:
		return OperatorPrecedenceInvalid;
	}
}

Node* getLeftmostExpression(Node* node, bool stopAtCallExpressions) {
	for (;;) {
		switch (node->kind) {
		case Kind::PostfixUnaryExpression:
			node = node->as<PostfixUnaryExpression>()->Operand;
			continue;
		case Kind::BinaryExpression:
			node = node->as<BinaryExpression>()->Left;
			continue;
		case Kind::ConditionalExpression:
			node = node->as<ConditionalExpression>()->Condition;
			continue;
		case Kind::TaggedTemplateExpression:
			node = node->as<TaggedTemplateExpression>()->Tag;
			continue;
		case Kind::CallExpression:
			if (stopAtCallExpressions) return node;
			[[fallthrough]];
		case Kind::AsExpression:
		case Kind::ElementAccessExpression:
		case Kind::PropertyAccessExpression:
		case Kind::NonNullExpression:
		case Kind::PartiallyEmittedExpression:
		case Kind::SatisfiesExpression:
			node = node->expression();
			continue;
		default:
			return node;
		}
	}
}

TypePrecedence getTypeNodePrecedence(Node* n) {
	switch (n->kind) {
	case Kind::ConditionalType:
		return TypePrecedenceConditional;
	case Kind::JSDocOptionalType:
	case Kind::JSDocVariadicType:
		return TypePrecedenceJSDoc;
	case Kind::FunctionType:
	case Kind::ConstructorType:
		return TypePrecedenceFunction;
	case Kind::UnionType:
		return TypePrecedenceUnion;
	case Kind::IntersectionType:
		return TypePrecedenceIntersection;
	case Kind::TypeOperator:
		return TypePrecedenceTypeOperator;
	case Kind::InferType:
		if (n->as<InferTypeNode>()->TypeParameter->as<TypeParameterDeclaration>()->Constraint)
			return TypePrecedenceFunction;
		return TypePrecedenceTypeOperator;
	case Kind::IndexedAccessType:
	case Kind::ArrayType:
	case Kind::OptionalType:
		return TypePrecedencePostfix;
	case Kind::TypeQuery:
		return TypePrecedenceTypeOperator;
	case Kind::AnyKeyword:
	case Kind::UnknownKeyword:
	case Kind::StringKeyword:
	case Kind::NumberKeyword:
	case Kind::BigIntKeyword:
	case Kind::SymbolKeyword:
	case Kind::BooleanKeyword:
	case Kind::UndefinedKeyword:
	case Kind::NeverKeyword:
	case Kind::ObjectKeyword:
	case Kind::IntrinsicKeyword:
	case Kind::VoidKeyword:
	case Kind::JSDocAllType:
	case Kind::JSDocNullableType:
	case Kind::JSDocNonNullableType:
	case Kind::LiteralType:
	case Kind::TypePredicate:
	case Kind::TypeReference:
	case Kind::TypeLiteral:
	case Kind::TupleType:
	case Kind::RestType:
	case Kind::ParenthesizedType:
	case Kind::ThisType:
	case Kind::MappedType:
	case Kind::NamedTupleMember:
	case Kind::TemplateLiteralType:
	case Kind::ImportType:
	case Kind::PropertyAccessExpression:
	case Kind::ExpressionWithTypeArguments:
		return TypePrecedenceNonArray;
	default:
		return TypePrecedenceNonArray;
	}
}

bool isOptionalChain(Node* node) {
	if ((node->flags & NodeFlagsOptionalChain) != 0) {
		switch (node->kind) {
		case Kind::PropertyAccessExpression:
		case Kind::ElementAccessExpression:
		case Kind::CallExpression:
		case Kind::NonNullExpression:
			return true;
		default:
			break;
		}
	}
	return false;
}

Node* getQuestionDotToken(Node* node) { return node->questionDotToken(); }

bool isOptionalChainRoot(Node* node) {
	return isOptionalChain(node) && !isNonNullExpression(node) &&
	       getQuestionDotToken(node) != nullptr;
}

bool isOutermostOptionalChain(Node* node) {
	Node* parent = node->parent;
	return !isOptionalChain(parent) || isOptionalChainRoot(parent) ||
	       node != parent->expression();
}

bool isSignedNumericLiteral(Node* node) {
	if (node->kind == Kind::PrefixUnaryExpression) {
		auto* n = node->as<PrefixUnaryExpression>();
		return (n->Operator == Kind::PlusToken || n->Operator == Kind::MinusToken) &&
		       isNumericLiteral(n->Operand);
	}
	return false;
}



// ---------------------------------------------------------------------------
// utilities.go: SetParentInChildren / GetRightMostAssignedExpression /
// HasSamePropertyAccessName, and NodeFactory::DeepCloneReparse
// ---------------------------------------------------------------------------

void setParentInChildren(Node* node) {
	Node* parent = nullptr;
	std::function<bool(Node*)> visit = [&](Node* n) {
		if (parent != nullptr) {
			n->parent = parent;
		}
		Node* saveParent = parent;
		parent = n;
		n->forEachChild(visit);
		parent = saveParent;
		return false;
	};
	visit(node);
}

Node* getRightMostAssignedExpression(Node* node) {
	while (isAssignmentExpression(node, false /*excludeCompoundAssignment*/)) {
		node = node->as<BinaryExpression>()->Right;
	}
	return node;
}

bool hasSamePropertyAccessName(Node* node1, Node* node2) {
	if (node1->kind == Kind::Identifier &&
	    node2->kind == Kind::Identifier) {
		return node1->text() == node2->text();
	}
	if (node1->kind == Kind::PropertyAccessExpression &&
	    node2->kind == Kind::PropertyAccessExpression) {
		return node1->name()->text() == node2->name()->text() &&
		       hasSamePropertyAccessName(node1->expression(),
		                                 node2->expression());
	}
	return false;
}

Node* NodeFactory::deepCloneReparse(Node* node) {
	if (node != nullptr) {
		node = deepCloneNode(*this, node);
		setParentInChildren(node);
		node->flags |= NodeFlagsReparsed;
	}
	return node;
}

ModifierList* NodeFactory::deepCloneReparseModifiers(ModifierList* modifiers) {
	return deepCloneModifierList(*this, modifiers);
}

// ---------------------------------------------------------------------------
// MutableNode setters
// ---------------------------------------------------------------------------

void Node::setExpression(Node* expr) {
	switch (kind) {
	case Kind::PropertyAccessExpression:
		as<PropertyAccessExpression>()->Expression = expr; break;
	case Kind::ElementAccessExpression:
		as<ElementAccessExpression>()->Expression = expr; break;
	case Kind::ParenthesizedExpression:
		as<ParenthesizedExpression>()->Expression = expr; break;
	case Kind::CallExpression:
		as<CallExpression>()->Expression = expr; break;
	case Kind::NewExpression:
		as<NewExpression>()->Expression = expr; break;
	case Kind::ExpressionWithTypeArguments:
		as<ExpressionWithTypeArguments>()->Expression = expr; break;
	case Kind::ComputedPropertyName:
		as<ComputedPropertyName>()->Expression = expr; break;
	case Kind::NonNullExpression:
		as<NonNullExpression>()->Expression = expr; break;
	case Kind::TypeAssertionExpression:
		as<TypeAssertion>()->Expression = expr; break;
	case Kind::AsExpression:
		as<AsExpression>()->Expression = expr; break;
	case Kind::SatisfiesExpression:
		as<SatisfiesExpression>()->Expression = expr; break;
	case Kind::TypeOfExpression:
		as<TypeOfExpression>()->Expression = expr; break;
	case Kind::SpreadAssignment:
		as<SpreadAssignment>()->Expression = expr; break;
	case Kind::SpreadElement:
		as<SpreadElement>()->Expression = expr; break;
	case Kind::TemplateSpan:
		as<TemplateSpan>()->Expression = expr; break;
	case Kind::DeleteExpression:
		as<DeleteExpression>()->Expression = expr; break;
	case Kind::VoidExpression:
		as<VoidExpression>()->Expression = expr; break;
	case Kind::AwaitExpression:
		as<AwaitExpression>()->Expression = expr; break;
	case Kind::YieldExpression:
		as<YieldExpression>()->Expression = expr; break;
	case Kind::PartiallyEmittedExpression:
		as<PartiallyEmittedExpression>()->Expression = expr; break;
	case Kind::IfStatement:
		as<IfStatement>()->Expression = expr; break;
	case Kind::DoStatement:
		as<DoStatement>()->Expression = expr; break;
	case Kind::WhileStatement:
		as<WhileStatement>()->Expression = expr; break;
	case Kind::WithStatement:
		as<WithStatement>()->Expression = expr; break;
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		as<ForInOrOfStatement>()->Expression = expr; break;
	case Kind::SwitchStatement:
		as<SwitchStatement>()->Expression = expr; break;
	case Kind::CaseClause:
	case Kind::DefaultClause:
		as<CaseOrDefaultClause>()->Expression = expr; break;
	case Kind::ExpressionStatement:
		as<ExpressionStatement>()->Expression = expr; break;
	case Kind::ReturnStatement:
		as<ReturnStatement>()->Expression = expr; break;
	case Kind::ThrowStatement:
		as<ThrowStatement>()->Expression = expr; break;
	case Kind::ExternalModuleReference:
		as<ExternalModuleReference>()->Expression = expr; break;
	case Kind::ExportAssignment:
		as<ExportAssignment>()->Expression = expr; break;
	default:
		TSC_UNREACHABLE("Unhandled case in Node::setExpression");
	}
}

void Node::setType(Node* t) {
	switch (kind) {
	case Kind::VariableDeclaration:
		as<VariableDeclaration>()->Type = t; break;
	case Kind::Parameter:
		as<ParameterDeclaration>()->Type = t; break;
	case Kind::PropertySignature:
		as<PropertySignatureDeclaration>()->Type = t; break;
	case Kind::PropertyDeclaration:
		as<PropertyDeclaration>()->Type = t; break;
	case Kind::PropertyAssignment:
		as<PropertyAssignment>()->Type = t; break;
	case Kind::ShorthandPropertyAssignment:
		as<ShorthandPropertyAssignment>()->Type = t; break;
	case Kind::TypePredicate:
		as<TypePredicateNode>()->Type = t; break;
	case Kind::ParenthesizedType:
		as<ParenthesizedTypeNode>()->Type = t; break;
	case Kind::TypeOperator:
		as<TypeOperatorNode>()->Type = t; break;
	case Kind::MappedType:
		as<MappedTypeNode>()->Type = t; break;
	case Kind::TypeAssertionExpression:
		as<TypeAssertion>()->Type = t; break;
	case Kind::AsExpression:
		as<AsExpression>()->Type = t; break;
	case Kind::SatisfiesExpression:
		as<SatisfiesExpression>()->Type = t; break;
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
		as<TypeAliasDeclaration>()->Type = t; break;
	case Kind::NamedTupleMember:
		as<NamedTupleMember>()->Type = t; break;
	case Kind::OptionalType:
		as<OptionalTypeNode>()->Type = t; break;
	case Kind::RestType:
		as<RestTypeNode>()->Type = t; break;
	case Kind::TemplateLiteralTypeSpan:
		as<TemplateLiteralTypeSpan>()->Type = t; break;
	case Kind::JSDocTypeExpression:
		as<JSDocTypeExpression>()->Type = t; break;
	case Kind::JSDocParameterTag:
	case Kind::JSDocPropertyTag:
		as<JSDocParameterOrPropertyTag>()->TypeExpression = t; break;
	case Kind::JSDocNullableType:
		as<JSDocNullableType>()->Type = t; break;
	case Kind::JSDocNonNullableType:
		as<JSDocNonNullableType>()->Type = t; break;
	case Kind::JSDocOptionalType:
		as<JSDocOptionalType>()->Type = t; break;
	case Kind::ExportAssignment:
		as<ExportAssignment>()->Type = t; break;
	case Kind::BinaryExpression:
		as<BinaryExpression>()->Type = t; break;
	default: {
		auto d = functionLikeData();
		if (d.type != nullptr) {
			*d.type = t;
		} else {
			TSC_UNREACHABLE("Unhandled case in Node::setType");
		}
	} break;
	}
}

void Node::setInitializer(Node* initializer) {
	switch (kind) {
	case Kind::VariableDeclaration:
		as<VariableDeclaration>()->Initializer = initializer; break;
	case Kind::Parameter:
		as<ParameterDeclaration>()->Initializer = initializer; break;
	case Kind::BindingElement:
		as<BindingElement>()->Initializer = initializer; break;
	case Kind::PropertyDeclaration:
		as<PropertyDeclaration>()->Initializer = initializer; break;
	case Kind::PropertySignature:
		as<PropertySignatureDeclaration>()->Initializer = initializer; break;
	case Kind::PropertyAssignment:
		as<PropertyAssignment>()->Initializer = initializer; break;
	case Kind::EnumMember:
		as<EnumMember>()->Initializer = initializer; break;
	case Kind::ForStatement:
		as<ForStatement>()->Initializer = initializer; break;
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		as<ForInOrOfStatement>()->Initializer = initializer; break;
	case Kind::JsxAttribute:
		as<JsxAttribute>()->Initializer = initializer; break;
	default:
		TSC_UNREACHABLE("Unhandled case in Node::setInitializer");
	}
}

// ---------------------------------------------------------------------------
// utilities.go / parseoptions.go — module & import helpers.

bool hasSyntacticModifier(Node* node, ModifierFlags flags) {
	return node->modifierFlags() & flags;
}

bool isAmbientModule(Node* node) {
	return isModuleDeclaration(node) &&
	       (node->name()->kind == Kind::StringLiteral ||
	        isGlobalScopeAugmentation(node));
}

bool isGlobalScopeAugmentation(Node* node) {
	return isModuleDeclaration(node) &&
	       node->as<ModuleDeclaration>()->Keyword == Kind::GlobalKeyword;
}

bool isExternalModule(SourceFile* file) {
	return file->ExternalModuleIndicator != nullptr;
}

bool isAnyImportSyntax(Node* node) {
	return node->kind == Kind::ImportDeclaration ||
	       node->kind == Kind::ImportEqualsDeclaration;
}

bool isImportNode(Node* node) {
	return isAnyImportSyntax(node) || node->kind == Kind::JSImportDeclaration;
}

bool isAnyImportOrReExport(Node* node) {
	return isImportNode(node) || isExportDeclaration(node);
}

static Node* getImportTypeNodeLiteral(Node* node) {
	if (isImportTypeNode(node)) {
		auto* importTypeNode = node->as<ImportTypeNode>();
		if (isLiteralTypeNode(importTypeNode->Argument)) {
			auto* literalTypeNode =
			    importTypeNode->Argument->as<LiteralTypeNode>();
			if (isStringLiteral(literalTypeNode->Literal)) {
				return literalTypeNode->Literal;
			}
		}
	}
	return nullptr;
}

Node* getExternalModuleName(Node* node) {
	switch (node->kind) {
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::ExportDeclaration:
		return node->moduleSpecifier();
	case Kind::ImportEqualsDeclaration:
		if (node->as<ImportEqualsDeclaration>()->ModuleReference->kind ==
		    Kind::ExternalModuleReference) {
			return node->as<ImportEqualsDeclaration>()
			    ->ModuleReference->expression();
		}
		return nullptr;
	case Kind::ImportType:
		return getImportTypeNodeLiteral(node);
	case Kind::CallExpression:
		if (node->as<CallExpression>()->Arguments->nodes.empty()) {
			return nullptr;
		}
		return node->as<CallExpression>()->Arguments->nodes.front();
	case Kind::ModuleDeclaration:
		if (isStringLiteral(node->as<ModuleDeclaration>()->name)) {
			return node->as<ModuleDeclaration>()->name;
		}
		return nullptr;
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in getExternalModuleName");
}

bool isLiteralImportTypeNode(Node* node) {
	return isImportTypeNode(node) &&
	       isLiteralTypeNode(node->as<ImportTypeNode>()->Argument) &&
	       isStringLiteral(
	           node->as<ImportTypeNode>()->Argument->as<LiteralTypeNode>()->Literal);
}

bool isRequireCall(Node* node, bool requireStringLiteralLikeArgument) {
	if (!isCallExpression(node)) {
		return false;
	}
	auto* call = node->as<CallExpression>();
	if (!isIdentifier(call->Expression) ||
	    call->Expression->text() != "require") {
		return false;
	}
	if (call->Arguments->nodes.size() != 1) {
		return false;
	}
	return !requireStringLiteralLikeArgument ||
	       isStringLiteralLike(call->Arguments->nodes[0]);
}

bool isImportCall(Node* node) {
	if (!isCallExpression(node)) {
		return false;
	}
	Node* e = node->expression();
	return e->kind == Kind::ImportKeyword ||
	       (isMetaProperty(e) &&
	        e->as<MetaProperty>()->KeywordToken == Kind::ImportKeyword &&
	        e->text() == "defer");
}

bool isImportMeta(Node* node) {
	if (node->kind == Kind::MetaProperty) {
		return node->as<MetaProperty>()->KeywordToken == Kind::ImportKeyword &&
		       node->as<MetaProperty>()->name->text() == "meta";
	}
	return false;
}

bool isJsxOpeningLikeElement(Node* node) {
	return isJsxOpeningElement(node) || isJsxSelfClosingElement(node);
}

static bool nodeContainsPosition(Node* node, int position) {
	return node->kind >= KindFirstNode && node->pos() <= position &&
	       (position < node->end() ||
	        (position == node->end() && node->kind == Kind::EndOfFile));
}

Node* getNodeAtPosition(SourceFile* file, int position, bool includeJSDoc) {
	Node* current = file;
	for (;;) {
		Node* child = nullptr;
		if (includeJSDoc) {
			for (Node* jsdoc : current->jsDoc(file)) {
				if (nodeContainsPosition(jsdoc, position)) {
					child = jsdoc;
					break;
				}
			}
		}
		if (child == nullptr) {
			current->forEachChild([&](Node* node) -> bool {
				if (nodeContainsPosition(node, position)) {
					child = node;
					return true;
				}
				return false;
			});
		}
		if (child == nullptr || isMetaProperty(child)) {
			return current;
		}
		current = child;
	}
}

static std::pair<int, int> findImportOrRequire(std::string_view text, int start) {
	int index = std::max(start, 0);
	int n = (int)text.size();
	while (index < n) {
		auto next = text.find_first_of("ir", index);
		if (next == std::string_view::npos) {
			break;
		}
		index = (int)next;

		int size;
		std::string_view expected;
		if (text[index] == 'i') {
			size = 6;
			expected = "import";
		} else {
			size = 7;
			expected = "require";
		}
		if (index + size <= n && text.substr(index, size) == expected) {
			return {index, size};
		}
		index++;
	}
	return {-1, 0};
}

bool forEachDynamicImportOrRequireCall(
    SourceFile* file, bool includeTypeSpaceImports,
    bool requireStringLiteralLikeArgument,
    const std::function<bool(Node* node, Node* argument)>& cb) {
	bool isJavaScriptFile = isInJSFile(file);
	auto [lastIndex, size] = findImportOrRequire(file->text, 0);
	while (lastIndex >= 0) {
		Node* node = getNodeAtPosition(
		    file, lastIndex, isJavaScriptFile && includeTypeSpaceImports);
		if (isJavaScriptFile &&
		    isRequireCall(node, requireStringLiteralLikeArgument)) {
			if (cb(node, node->as<CallExpression>()->Arguments->nodes[0])) {
				return true;
			}
		} else if (isImportCall(node) &&
		           !node->as<CallExpression>()->Arguments->nodes.empty() &&
		           (!requireStringLiteralLikeArgument ||
		            isStringLiteralLike(
		                node->as<CallExpression>()->Arguments->nodes[0]))) {
			if (cb(node, node->as<CallExpression>()->Arguments->nodes[0])) {
				return true;
			}
		} else if (includeTypeSpaceImports && isLiteralImportTypeNode(node)) {
			if (cb(node,
			       node->as<ImportTypeNode>()
			           ->Argument->as<LiteralTypeNode>()
			           ->Literal)) {
				return true;
			}
		}
		// skip past import/require
		lastIndex += size;
		auto next = findImportOrRequire(file->text, lastIndex);
		lastIndex = next.first;
		size = next.second;
	}
	return false;
}

void setImportsOfSourceFile(SourceFile* file, std::vector<Node*> imports) {
	file->imports = std::move(imports);
}

// parseoptions.go

static bool isAnExternalModuleIndicatorNode(Node* node) {
	return hasSyntacticModifier(node, ModifierFlagsExport) ||
	       (isImportEqualsDeclaration(node) &&
	        isExternalModuleReference(
	            node->as<ImportEqualsDeclaration>()->ModuleReference)) ||
	       isImportDeclaration(node) || isExportAssignment(node) ||
	       isExportDeclaration(node);
}

static Node* findChildNode(Node* root, const std::function<bool(Node*)>& check) {
	Node* result = nullptr;
	std::function<bool(Node*)> visit = [&](Node* node) -> bool {
		if (check(node)) {
			result = node;
			return true;
		}
		return node->forEachChild(visit);
	};
	visit(root);
	return result;
}

static Node* getImportMetaIfNecessary(SourceFile* sourceFile) {
	if (sourceFile->flags & NodeFlagsPossiblyContainsImportMeta) {
		return findChildNode(sourceFile, isImportMeta);
	}
	return nullptr;
}

static Node* isFileProbablyExternalModule(SourceFile* sourceFile) {
	for (Node* statement : sourceFile->Statements->nodes) {
		if (isAnExternalModuleIndicatorNode(statement)) {
			return statement;
		}
	}
	return getImportMetaIfNecessary(sourceFile);
}

// This is a somewhat unavoidable full tree walk to locate a JSX tag -
// `import.meta` requires the same, but we avoid that walk (or parts of it) if
// at all possible using the `PossiblyContainsImportMeta` node flag.
// Unfortunately, there's no `NodeFlag` space to do the same for JSX.
static Node* walkTreeForJSXTags(Node* node) {
	Node* found = nullptr;
	std::function<bool(Node*)> visitor = [&](Node* n) -> bool {
		if (found != nullptr) {
			return true;
		}
		if (!(n->subtreeFacts() & SubtreeContainsJsx)) {
			return false;
		}
		if (isJsxOpeningLikeElement(n) || isJsxFragment(n)) {
			found = n;
			return true;
		}
		return n->forEachChild(visitor);
	};
	visitor(node);
	return found;
}

static Node* isFileModuleFromUsingJSXTag(SourceFile* file) {
	return walkTreeForJSXTags(file);
}

static Node* getExternalModuleIndicator(
    SourceFile* file, const ExternalModuleIndicatorOptions& opts) {
	if (file->ScriptKind == ScriptKind::JSON) {
		return nullptr;
	}
	if (Node* node = isFileProbablyExternalModule(file)) {
		return node;
	}
	if (file->IsDeclarationFile) {
		return nullptr;
	}
	if (opts.JSX == JsxEmit::ReactJSX || opts.JSX == JsxEmit::ReactJSXDev) {
		if (Node* node = isFileModuleFromUsingJSXTag(file)) {
			return node;
		}
	}
	if (opts.Force) {
		return file;
	}
	return nullptr;
}

void setExternalModuleIndicator(
    SourceFile* file, const ExternalModuleIndicatorOptions& opts) {
	file->ExternalModuleIndicator = getExternalModuleIndicator(file, opts);
}

}  // namespace tsc
