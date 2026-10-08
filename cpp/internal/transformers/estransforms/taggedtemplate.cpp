// Port of tsc/internal/transformers/estransforms/taggedtemplate.go.
#include "internal/transformers/estransforms/estransforms.h"

#include "internal/scanner/scanner.h"

namespace tsc::transformers::estransforms {
namespace {

// newlineNormalizer — taggedtemplate.go:13.
// strings.NewReplacer("\r\n", "\n", "\r", "\n"): left-to-right scan; at each
// position the first matching pattern wins.
std::string normalizeTemplateNewlines(const std::string& text) {
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size();) {
		if (text[i] == '\r') {
			i += (i + 1 < text.size() && text[i + 1] == '\n') ? 2 : 1;
			out.push_back('\n');
		} else {
			out.push_back(text[i]);
			i += 1;
		}
	}
	return out;
}

struct TaggedTemplateTransformer : Transformer {
	SourceFile* currentSourceFile = nullptr;
	std::vector<Node*> taggedTemplateStringDeclarations;

	// newTaggedTemplateLiftRestrictionTransformer — taggedtemplate.go:22
	static Transformer* create(TransformOptions* opt) {
		auto* tx = new TaggedTemplateTransformer;
		return tx->newTransformer(
			[tx](Node* node) { return tx->visit(node); }, opt->Context);
	}

	// visit — taggedtemplate.go:27
	Node* visit(Node* node) {
		if ((node->subtreeFacts() & SubtreeContainsInvalidTemplateEscape) ==
			0) {
			return node;
		}
		switch (node->kind) {
		case Kind::SourceFile:
			return visitSourceFile(node->as<SourceFile>());
		case Kind::TaggedTemplateExpression:
			return visitTaggedTemplateExpression(
				node->as<TaggedTemplateExpression>());
		default:
			return visitor()->visitEachChild(node);
		}
	}

	// visitSourceFile — taggedtemplate.go:41
	Node* visitSourceFile(SourceFile* node) {
		currentSourceFile = node;
		taggedTemplateStringDeclarations.clear();
		Node* visited = visitor()->visitEachChild(node->asNode());

		if (!taggedTemplateStringDeclarations.empty()) {
			SourceFile* visitedSourceFile = visited->as<SourceFile>();
			std::vector<Node*> statements =
				visitedSourceFile->Statements->nodes;
			statements.push_back(factory()->newVariableStatement(
				nullptr /*modifiers*/,
				factory()->newVariableDeclarationList(
					factory()->newNodeList(
						taggedTemplateStringDeclarations),
					NodeFlagsNone)));
			NodeList* stmtList = factory()->newNodeList(statements);
			stmtList->loc = node->Statements->loc;
			visited = factory()->updateSourceFile(
				visitedSourceFile, stmtList,
				visitedSourceFile->EndOfFileToken);
		}

		for (printer::EmitHelper* helper : emitContext()->readEmitHelpers()) {
			emitContext()->addEmitHelper(visited, helper);
		}
		return visited;
	}

	// visitTaggedTemplateExpression — taggedtemplate.go:67
	Node* visitTaggedTemplateExpression(TaggedTemplateExpression* node) {
		return processTaggedTemplateExpression(node);
	}

	// processTaggedTemplateExpression — taggedtemplate.go:71
	Node* processTaggedTemplateExpression(TaggedTemplateExpression* node) {
		Node* tag = visitor()->visitNode(node->Tag);
		Node* templateNode = node->Template;

		if (!hasInvalidEscape(templateNode)) {
			return visitor()->visitEachChild(node->asNode());
		}

		printer::NodeFactory* f = factory();

		// Build up the template arguments and the raw and cooked strings for
		// the template.
		std::vector<Node*> templateArguments{
			nullptr}; // placeholder for the template object
		std::vector<Node*> cookedStrings;
		std::vector<Node*> rawStrings;

		if (isNoSubstitutionTemplateLiteral(templateNode)) {
			cookedStrings.push_back(createTemplateCooked(
				f, templateNode->templateLiteralLikeData()));
			rawStrings.push_back(getRawLiteral(f, templateNode));
		} else {
			TemplateExpression* te = templateNode->as<TemplateExpression>();
			cookedStrings.push_back(createTemplateCooked(
				f, te->Head->templateLiteralLikeData()));
			rawStrings.push_back(getRawLiteral(f, te->Head));
			for (Node* span : te->TemplateSpans->nodes) {
				TemplateSpan* ts = span->as<TemplateSpan>();
				cookedStrings.push_back(createTemplateCooked(
					f, ts->Literal->templateLiteralLikeData()));
				rawStrings.push_back(getRawLiteral(f, ts->Literal));
				templateArguments.push_back(
					visitor()->visitNode(ts->Expression));
			}
		}

		Node* helperCall = f->newTemplateObjectHelper(
			f->newArrayLiteralExpression(f->newNodeList(cookedStrings),
										 false),
			f->newArrayLiteralExpression(f->newNodeList(rawStrings), false));

		// Create a variable to cache the template object if we're in a
		// module. Do not do this in the global scope, as any variable we
		// currently generate could conflict with variables from outside of
		// the current compilation. In the future, we can revisit this
		// behavior.
		if (isExternalModule(currentSourceFile)) {
			Node* tempVar = f->newUniqueName("templateObject");
			taggedTemplateStringDeclarations.push_back(
				f->newVariableDeclaration(tempVar, nullptr, nullptr,
										  nullptr));
			templateArguments[0] = f->newLogicalORExpression(
				tempVar, f->newAssignmentExpression(tempVar, helperCall));
		} else {
			templateArguments[0] = helperCall;
		}

		Node* call = f->newCallExpression(
			tag, nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
			f->newNodeList(templateArguments), NodeFlagsNone);
		call->loc = node->loc;
		return call;
	}

	// createTemplateCooked — taggedtemplate.go:128
	static Node* createTemplateCooked(printer::NodeFactory* f,
									  TemplateLiteralLikeDataRef template_) {
		if ((*template_.templateFlags & TokenFlagsIsInvalid) != 0) {
			return f->newVoidZeroExpression();
		}
		return f->newStringLiteral(*template_.text, TokenFlagsNone);
	}

	// getRawLiteral — taggedtemplate.go:135
	static Node* getRawLiteral(printer::NodeFactory* f, Node* node) {
		std::string text = *node->templateLiteralLikeData().rawText;
		if (text.empty()) {
			text = getSourceTextOfNodeFromSourceFile(
				getSourceFileOfNode(node), node,
				false /*includeTrivia*/);
			// text contains the original source, it will also contain quotes
			// ("`"), dollar signs and braces ("${" and "}"), thus we need to
			// remove those characters.
			// First template piece starts with "`", others with "}"
			// Last template piece ends with "`", others with "${"
			bool isLast = node->kind == Kind::NoSubstitutionTemplateLiteral ||
						  node->kind == Kind::TemplateTail;
			size_t endLen = 2;
			if (isLast) {
				endLen = 1;
			}
			text = text.substr(1, text.size() - 1 - endLen);
		}

		// Newline normalization:
		// ES6 Spec 11.8.6.1 - Static Semantics of TV's and TRV's
		// <CR><LF> and <CR> LineTerminatorSequences are normalized to <LF>
		// for both TV and TRV.
		text = normalizeTemplateNewlines(text);

		Node* result = f->newStringLiteral(text, TokenFlagsNone);
		result->loc = node->loc;
		return result;
	}

	// hasInvalidEscape — taggedtemplate.go:161
	static bool hasInvalidEscape(Node* template_) {
		if (isNoSubstitutionTemplateLiteral(template_)) {
			return (*template_->templateLiteralLikeData().templateFlags &
					TokenFlagsContainsInvalidEscape) != 0;
		}
		TemplateExpression* te = template_->as<TemplateExpression>();
		if ((*te->Head->templateLiteralLikeData().templateFlags &
			 TokenFlagsContainsInvalidEscape) != 0) {
			return true;
		}
		for (Node* span : te->TemplateSpans->nodes) {
			if ((*span->as<TemplateSpan>()
					 ->Literal->templateLiteralLikeData()
					 .templateFlags &
				 TokenFlagsContainsInvalidEscape) != 0) {
				return true;
			}
		}
		return false;
	}
};

}  // namespace

Transformer*
newTaggedTemplateLiftRestrictionTransformer(TransformOptions* opt) {
	return TaggedTemplateTransformer::create(opt);
}

}  // namespace tsc::transformers::estransforms
