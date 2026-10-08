// type_symbol_baseline.go — DoTypeAndSymbolBaseline + the
// typeWriterWalker that emits ">line : type/symbol" sections.
#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/core/types.h"
#include "internal/nodebuilder/types.h"
#include "internal/printer/emitcontext.h"
#include "internal/printer/printer.h"
#include "internal/scanner/scanner.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/harnessutil/harnessutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/tsbaseline/tsbaseline.h"
#include "internal/testutil/tsbaseline/tsbaselineutil.h"
#include "internal/tspath/tspath.h"

namespace tsc::testutil::tsbaseline {

using harnessutil::TestFile;

namespace {

// codeLinesRegexp / bracketLineRegex / lineEndRegex —
// type_symbol_baseline.go:25.
const gostd::regexp::Regexp& codeLinesRegexp() {
	static const auto* r =
	    new gostd::regexp::Regexp("[\r\xE2\x80\xA8\xE2\x80\xA9]|\\r?\\n");
	return *r;
}
const gostd::regexp::Regexp& bracketLineRegex() {
	static const auto* r =
	    new gostd::regexp::Regexp("^\\s*[{|}]\\s*$");
	return *r;
}

// SemanticMeaning — ast/utilities.go:2240 (unported ast constants; file-local
// replicas matching the Go values).
using SemanticMeaning = int32_t;
inline constexpr SemanticMeaning SemanticMeaningNone = 0;
inline constexpr SemanticMeaning SemanticMeaningValue = 1 << 0;
inline constexpr SemanticMeaning SemanticMeaningType = 1 << 1;
inline constexpr SemanticMeaning SemanticMeaningNamespace = 1 << 2;
inline constexpr SemanticMeaning SemanticMeaningAll =
    SemanticMeaningValue | SemanticMeaningType | SemanticMeaningNamespace;

// getMeaningFromDeclaration — ast/utilities.go:2250 (file-local replica;
// unported in cpp/internal/ast).
SemanticMeaning getMeaningFromDeclaration(Node* node) {
	switch (node->kind) {
		case Kind::VariableDeclaration:
			return SemanticMeaningValue;
		case Kind::Parameter:
		case Kind::BindingElement:
		case Kind::PropertyDeclaration:
		case Kind::PropertySignature:
		case Kind::PropertyAssignment:
		case Kind::ShorthandPropertyAssignment:
		case Kind::MethodDeclaration:
		case Kind::MethodSignature:
		case Kind::Constructor:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
		case Kind::FunctionDeclaration:
		case Kind::FunctionExpression:
		case Kind::ArrowFunction:
		case Kind::CatchClause:
		case Kind::JsxAttribute:
			return SemanticMeaningValue;

		case Kind::TypeParameter:
		case Kind::InterfaceDeclaration:
		case Kind::TypeAliasDeclaration:
		case Kind::JSTypeAliasDeclaration:
		case Kind::TypeLiteral:
			return SemanticMeaningType;
		case Kind::EnumMember:
		case Kind::ClassDeclaration:
			return SemanticMeaningValue | SemanticMeaningType;

		case Kind::ModuleDeclaration:
			if (isAmbientModule(node)) {
				return SemanticMeaningNamespace | SemanticMeaningValue;
			} else if (getModuleInstanceState(node) ==
			           ModuleInstanceState::Instantiated) {
				return SemanticMeaningNamespace | SemanticMeaningValue;
			} else {
				return SemanticMeaningNamespace;
			}

		case Kind::EnumDeclaration:
		case Kind::NamedImports:
		case Kind::ImportSpecifier:
		case Kind::ImportEqualsDeclaration:
		case Kind::ImportDeclaration:
		case Kind::JSImportDeclaration:
		case Kind::ExportAssignment:
		case Kind::ExportDeclaration:
			return SemanticMeaningAll;

		// An external module can be a Value
		case Kind::SourceFile:
			return SemanticMeaningNamespace | SemanticMeaningValue;
		default:
			break;
	}
	return SemanticMeaningAll;
}

// isLabelOfLabeledStatement / isJumpStatementTarget / isLabelName —
// ast/utilities.go:2316 (file-local replicas).
bool isLabelOfLabeledStatement(Node* node) {
	if (!isIdentifier(node)) return false;
	if (!isLabeledStatement(node->parent)) return false;
	return node == node->parent->label();
}
bool isJumpStatementTarget(Node* node) {
	if (!isIdentifier(node)) return false;
	// IsBreakOrContinueStatement
	if (!nodeKindIs(node->parent, Kind::BreakStatement,
	                     Kind::ContinueStatement)) {
		return false;
	}
	return node == node->parent->label();
}
bool isLabelName(Node* node) {
	return isLabelOfLabeledStatement(node) || isJumpStatementTarget(node);
}

// isTypeBaselineNodeReuseLine — type_symbol_baseline.go:108.
bool isTypeBaselineNodeReuseLine(std::string_view line) {
	if (line.empty() || line.front() != '>') {
		return false;
	}
	line = line.substr(1);
	// strings.TrimLeft(line[1:], " ") — Go indexes the already-cut string.
	if (!line.empty()) line = line.substr(1);
	while (!line.empty() && line.front() == ' ') {
		line = line.substr(1);
	}
	if (line.empty() || line.front() != ':') {
		return false;
	}
	line = line.substr(1);
	for (char c : line) {
		switch (c) {
			case ' ':
			case '^':
			case '\r':
				break;
			default:
				return false;
		}
	}
	return true;
}

// typeWriterResult — type_symbol_baseline.go:330.
struct typeWriterResult {
	int line;
	std::string sourceText;
	std::string symbol;
	std::string typ;
	std::string underline;  // !!!
};

// typeWriterWalker — type_symbol_baseline.go:291.
struct typeWriterWalker {
	compiler::ProgramLike* program;
	bool hadErrorBaseline;
	SourceFile* currentSourceFile;
	std::unordered_map<Node*, std::string> declarationTextCache;
};

// isImportStatementName — type_symbol_baseline.go:464.
bool isImportStatementName(Node* node) {
	if (isImportSpecifier(node->parent) &&
	    (node == node->parent->name() ||
	     node == node->parent->propertyName())) {
		return true;
	}
	if (isImportClause(node->parent) &&
	    node == node->parent->name()) {
		return true;
	}
	if (isImportEqualsDeclaration(node->parent) &&
	    node == node->parent->name()) {
		return true;
	}
	return false;
}

// isExportStatementName — type_symbol_baseline.go:475.
bool isExportStatementName(Node* node) {
	if (isExportAssignment(node->parent) &&
	    node == node->parent->expression()) {
		return true;
	}
	if (isExportSpecifier(node->parent) &&
	    (node == node->parent->name() ||
	     node == node->parent->propertyName())) {
		return true;
	}
	return false;
}

// isIntrinsicJsxTag — type_symbol_baseline.go:484.
bool isIntrinsicJsxTag(Node* node, SourceFile* sourceFile) {
	if (!(isJsxOpeningElement(node->parent) ||
	      isJsxClosingElement(node->parent) ||
	      isJsxSelfClosingElement(node->parent))) {
		return false;
	}
	if (node->parent->tagName() != node) {
		return false;
	}
	auto text = getSourceTextOfNodeFromSourceFile(
	    sourceFile, node, false /*includeTrivia*/);
	return isIntrinsicJsxName(text);
}

// getTypeCheckerForCurrentFile — type_symbol_baseline.go:310.
// If we don't use the right checker for the file, its contents won't be up
// to date since the types/symbols baselines appear to depend on files
// having been checked.
std::pair<checker::Checker*, std::function<void()>>
getTypeCheckerForCurrentFile(typeWriterWalker* walker) {
	return walker->program->GetProgram()->GetTypeCheckerForFileExclusive(
	    walker->currentSourceFile);
}

// writeTypeOrSymbol — type_symbol_baseline.go:374.
typeWriterResult* writeTypeOrSymbol(typeWriterWalker* walker, Node* node,
                                    bool isSymbolWalk) {
	int actualPos = skipTrivia(
	    walker->currentSourceFile->text, node->pos());
	int line = getECMALineOfPosition(walker->currentSourceFile,
	                                          actualPos);
	auto sourceText = getSourceTextOfNodeFromSourceFile(
	    walker->currentSourceFile, node, false /*includeTrivia*/);
	auto checkerDone =
	    getTypeCheckerForCurrentFile(walker);
	checker::Checker* fileChecker = checkerDone.first;
	auto done = checkerDone.second;
	struct DoneGuard {
		~DoneGuard() { f(); }
		std::function<void()> f;
	} doneGuard{done};

	auto [ctx, putCtx] = printer::GetEmitContext();
	struct PutCtxGuard {
		~PutCtxGuard() { f(); }
		std::function<void()> f;
	} putCtxGuard{putCtx};

	if (!isSymbolWalk) {
		// Don't try to get the type of something that's already a type.
		// Exception for `T` in `type T = something` because that may
		// evaluate to some interesting type.
		if (isPartOfTypeNode(node) ||
		    (node->kind == Kind::AsExpression ||
		     node->kind == Kind::SatisfiesExpression) &&
		        (node->type()->flags & NodeFlagsReparsed) != 0 ||
		    isIdentifier(node) &&
		        (getMeaningFromDeclaration(node->parent) &
		         SemanticMeaningValue) == 0 &&
		        !(isTypeOrJSTypeAliasDeclaration(node->parent) &&
		          node == node->parent->name())) {
			return nullptr;
		}

		if (isOmittedExpression(node)) {
			return nullptr;
		}

		checker::Type* t = nullptr;
		// Workaround to ensure we output 'C' instead of 'typeof C' for
		// base class expressions
		if (isExpressionWithTypeArgumentsInClassExtendsClause(
		        node->parent)) {
			t = fileChecker->GetTypeAtLocation(node->parent);
		}
		if (t == nullptr || checker::isTypeAny(t)) {
			t = fileChecker->GetTypeAtLocation(node);
		}
		std::string typeString;
		// var underline string
		if (!walker->hadErrorBaseline && checker::isTypeAny(t) &&
		    !isBindingElement(node->parent) &&
		    !isPropertyAccessOrQualifiedName(node->parent) &&
		    !isLabelName(node) &&
		    !isGlobalScopeAugmentation(node->parent) &&
		    !isMetaProperty(node->parent) &&
		    !isImportStatementName(node) &&
		    !isExportStatementName(node) &&
		    !isIntrinsicJsxTag(node, walker->currentSourceFile)) {
			typeString = t->AsIntrinsicType()->intrinsicName;
		} else {
			ctx->reset();
			auto* builder = checker::NewNodeBuilder(fileChecker, ctx);
			auto typeFormatFlags =
			    checker::TypeFormatFlagsNoTruncation |
			    checker::TypeFormatFlagsAllowUniqueESSymbolType |
			    checker::TypeFormatFlagsGenerateNamesForShadowedTypeParams;
			auto* typeNode = builder->TypeToTypeNode(
			    t, node->parent,
			    nodebuilder::Flags(
			        typeFormatFlags &
			        checker::TypeFormatFlagsNodeBuilderFlagsMask) |
			        nodebuilder::FlagsIgnoreErrors,
			    nodebuilder::InternalFlagsAllowUnresolvedNames, nullptr);
			if (isIdentifier(node) &&
			    isTypeAliasDeclaration(node->parent) &&
			    node->parent->name() == node &&
			    typeNode != nullptr && isIdentifier(typeNode) &&
			    typeNode->text() == node->text()) {
				// for a complex type alias `type T = ...`, showing
				// "T : T" isn't very helpful for type tests. When the
				// type produced is the same as the name of the type
				// alias, recreate the type string without reusing the
				// alias name
				typeNode = builder->TypeToTypeNode(
				    t, node->parent,
				    nodebuilder::Flags(
				        (typeFormatFlags |
				         checker::TypeFormatFlagsInTypeAlias) &
				        checker::TypeFormatFlagsNodeBuilderFlagsMask) |
				        nodebuilder::FlagsIgnoreErrors,
				    nodebuilder::InternalFlagsAllowUnresolvedNames,
				    nullptr);
			}

			// !!! TODO: port underline printer, memoize
			auto* writer = printer::NewTextWriter("", 0);
			printer::PrinterOptions printerOpts;
			printerOpts.RemoveComments = true;
			auto* printer_ =
			    printer::NewPrinter(printerOpts,
			                        printer::PrintHandlers{}, ctx);
			printer_->Write(typeNode, walker->currentSourceFile, writer,
			                nullptr);
			typeString = writer->String();
		}
		auto* r = new typeWriterResult();
		r->line = line;
		r->sourceText = sourceText;
		r->typ = typeString;
		// underline: underline, // !!! TODO: underline
		return r;
	}

	auto* symbol = fileChecker->GetSymbolAtLocation(node);
	if (symbol == nullptr) {
		return nullptr;
	}

	std::string symbolString;
	symbolString.reserve(256);
	symbolString += "Symbol(";
	symbolString += escapeAllInternalSymbolNames(
	    fileChecker->SymbolToStringEx(symbol, node->parent,
	                                  SymbolFlagsNone,
	                                  checker::SymbolFormatFlagsAllowAnyNodeKind));
	int count = 0;
	for (auto* declaration : symbol->declarations) {
		if (count >= 5) {
			symbolString += gostd::sprintf(
			    " ... and %d more",
			    {(int64_t)(symbol->declarations.size() - count)});
			break;
		}
		count += 1;
		symbolString += ", ";
		auto it = walker->declarationTextCache.find(declaration);
		if (it != walker->declarationTextCache.end()) {
			symbolString += it->second;
			continue;
		}

		auto* declSourceFile = getSourceFileOfNode(declaration);
		auto [declLine, declChar] =
		    getECMALineAndUTF16CharacterOfPosition(
		        declSourceFile, declaration->pos());
		auto fileName = tspath::getBaseFileName(declSourceFile->FileName());
		symbolString += "Decl(";
		symbolString += fileName;
		symbolString += ", ";
		if (isDefaultLibraryFile(fileName)) {
			symbolString += "--, --)";
		} else {
			symbolString += gostd::sprintf(
			    "%d, %d)", {(int64_t)declLine, (int64_t)declChar});
		}
	}
	symbolString += ")";
	auto* r = new typeWriterResult();
	r->line = line;
	r->sourceText = sourceText;
	r->symbol = symbolString;
	return r;
}

// forEachASTNode — type_symbol_baseline.go:355.
std::vector<Node*> forEachASTNode(Node* node) {
	std::vector<Node*> result;
	std::vector<Node*> work{node};

	std::vector<Node*> resChildren;
	auto addChild = [&resChildren](Node* child) -> bool {
		resChildren.push_back(child);
		return false;
	};

	while (!work.empty()) {
		auto* elem = work.back();
		work.pop_back();
		if ((elem->flags & NodeFlagsReparsed) == 0 ||
		    elem->kind == Kind::AsExpression ||
		    elem->kind == Kind::SatisfiesExpression ||
		    ((elem->parent->kind == Kind::SatisfiesExpression ||
		      elem->parent->kind == Kind::AsExpression) &&
		     elem == elem->parent->expression())) {
			if ((elem->flags & NodeFlagsReparsed) == 0 ||
			    elem->parent->kind == Kind::AsExpression ||
			    elem->parent->kind == Kind::SatisfiesExpression) {
				result.push_back(elem);
			}
			elem->forEachChild(
			    [&addChild](Node* child) { return addChild(child); });
			std::reverse(resChildren.begin(), resChildren.end());
			work.insert(work.end(), resChildren.begin(),
			            resChildren.end());
			resChildren.clear();
		}
	}
	return result;
}

// visitNode — type_symbol_baseline.go:343.
std::vector<typeWriterResult*> visitNode(typeWriterWalker* walker,
                                         Node* node, bool isSymbolWalk) {
	auto nodes = forEachASTNode(node);
	std::vector<typeWriterResult*> results;
	for (auto* n : nodes) {
		if (isExpressionNode(n) ||
		    n->kind == Kind::Identifier || isDeclarationName(n) ||
		    (isQualifiedName(n) &&
		     isNameOfHeritageClauseTypeReference(n) &&
		     (isSymbolWalk || isQualifiedName(n->parent)))) {
			auto* result = writeTypeOrSymbol(walker, n, isSymbolWalk);
			if (result != nullptr) {
				results.push_back(result);
			}
		}
	}
	return results;
}

// getTypes — type_symbol_baseline.go:337.
std::vector<typeWriterResult*> getTypes(typeWriterWalker* walker,
                                        const std::string& filename) {
	auto* sourceFile = walker->program->GetSourceFile(filename);
	walker->currentSourceFile = sourceFile;
	return visitNode(walker, sourceFile->asNode(), false /*isSymbolWalk*/);
}

// getSymbols — type_symbol_baseline.go:344.
std::vector<typeWriterResult*> getSymbols(typeWriterWalker* walker,
                                        const std::string& filename) {
	auto* sourceFile = walker->program->GetSourceFile(filename);
	walker->currentSourceFile = sourceFile;
	return visitNode(walker, sourceFile->asNode(), true /*isSymbolWalk*/);
}

// iterateBaseline — type_symbol_baseline.go:229.
std::vector<std::string> iterateBaseline(
    const std::vector<TestFile*>& allFiles, typeWriterWalker* fullWalker,
    bool isSymbolBaseline) {
	std::vector<std::string> baselines;

	for (auto* file : allFiles) {
		auto& unitName = file->UnitName;
		std::string typeLines;
		typeLines += "=== ";
		typeLines += unitName;
		typeLines += " ===\r\n";
		auto codeLines = codeLinesRegexp().Split(file->Content, -1);
		std::vector<typeWriterResult*> results;
		if (isSymbolBaseline) {
			results = getSymbols(fullWalker, unitName);
		} else {
			results = getTypes(fullWalker, unitName);
		}
		int lastIndexWritten = -1;
		for (auto* result : results) {
			if (isSymbolBaseline && result->symbol.empty()) {
				return baselines;
			}
			if (lastIndexWritten == -1) {
				for (int i = 0; i <= result->line; i++) {
					typeLines += codeLines[i];
					if (i != result->line) typeLines += "\r\n";
				}
				typeLines += "\r\n";
			} else if (lastIndexWritten != result->line) {
				if (!(lastIndexWritten + 1 < (int)codeLines.size() &&
				      (bracketLineRegex().MatchString(
				           codeLines[lastIndexWritten + 1]) ||
				       trimSpace(
				           codeLines[lastIndexWritten + 1])
				           .empty()))) {
					typeLines += "\r\n";
				}
				for (int i = lastIndexWritten + 1; i <= result->line;
				     i++) {
					typeLines += codeLines[i];
					if (i != result->line) typeLines += "\r\n";
				}
				typeLines += "\r\n";
			}
			lastIndexWritten = result->line;
			std::string typeOrSymbolString = isSymbolBaseline
			                                     ? result->symbol
			                                     : result->typ;
			auto lineText =
			    lineDelimiter().ReplaceAllString(result->sourceText, "");
			typeLines += ">";
			typeLines += lineText;
			typeLines += " : ";
			typeLines += typeOrSymbolString;
			typeLines += "\r\n";
			if (!result->underline.empty()) {
				typeLines += ">";
				for (size_t i = 0; i < lineText.size(); i++) {
					typeLines += " ";
				}
				typeLines += " : ";
				typeLines += result->underline;
				typeLines += "\r\n";
			}
		}

		if (lastIndexWritten + 1 < (int)codeLines.size()) {
			if (!(bracketLineRegex().MatchString(
			          codeLines[lastIndexWritten + 1]) ||
			      trimSpace(codeLines[lastIndexWritten + 1])
			          .empty())) {
				typeLines += "\r\n";
			}
			for (size_t i = lastIndexWritten + 1; i < codeLines.size();
			     i++) {
				typeLines += codeLines[i];
				if (i != codeLines.size() - 1) typeLines += "\r\n";
			}
		}
		typeLines += "\r\n";

		baselines.push_back(removeTestPathPrefixes(
		    typeLines, false /*retainTrailingDirectorySeparator*/));
	}

	return baselines;
}

// generateBaseline — type_symbol_baseline.go:211.
std::string generateBaseline(const std::vector<TestFile*>& allFiles,
                             typeWriterWalker* fullWalker,
                             const std::string& header,
                             bool isSymbolBaseline) {
	std::string result;
	// !!! Perf baseline
	std::vector<std::string> perfLines;
	auto baselines = iterateBaseline(allFiles, fullWalker, isSymbolBaseline);
	for (auto& value : baselines) {
		result += value;
	}
	// (perf baseline block is commented out in Go)

	if (!result.empty()) {
		std::string joined;
		for (auto& l : perfLines) {
			if (!joined.empty()) joined += "\n";
			joined += l;
		}
		return gostd::sprintf(
		    "//// [%s] ////\r\n\r\n%s%s", {header, joined, result});
	}
	return std::string(baseline::NoContent);
}

// checkBaselines — type_symbol_baseline.go:196.
void checkBaselines(gostd::testing::T* t, const std::string& baselinePath,
                    const std::vector<TestFile*>& allFiles,
                    typeWriterWalker* fullWalker, const std::string& header,
                    const baseline::Options& opts, bool isSymbolBaseline) {
	std::string fullExtension = isSymbolBaseline ? ".symbols" : ".types";
	auto outputFileName =
	    tsExtension().ReplaceAllString(baselinePath, fullExtension);
	auto fullBaseline =
	    generateBaseline(allFiles, fullWalker, header, isSymbolBaseline);
	baseline::Run(t, outputFileName, fullBaseline, opts);
}

}  // namespace

// DoTypeAndSymbolBaseline — type_symbol_baseline.go:30. The full walker
// simulates the types that you would get from doing a full compile.  The
// pull walker simulates the types you get when you just do a type query
// for a random node (like how the LS would do it).  Most of the time,
// these will be the same.  However, occasionally, they can be different.
// Specifically, when the compiler internally depends on symbol IDs to
// order things, then we may see different results because symbols can be
// created in a different order with 'pull' operations, and thus can
// produce slightly differing output.
//
// For example, with a full type check, we may see a type displayed as:
// number | string
// But with a pull type check, we may see it as:
// string | number
//
// These types are equivalent, but depend on what order the compiler
// observed certain parts of the program.
void DoTypeAndSymbolBaseline(
    gostd::testing::T* t, const std::string& baselinePath,
    const std::string& header, compiler::ProgramLike* program,
    const std::vector<harnessutil::TestFile*>& allFiles,
    const baseline::Options& opts, bool skipTypeBaselines,
    bool skipSymbolBaselines, bool hasErrorBaseline) {
	auto* fullWalker = new typeWriterWalker();
	fullWalker->program = program;
	fullWalker->hadErrorBaseline = hasErrorBaseline;

	t->Run("type", [&](gostd::testing::T* t) {
		testutil::withRecoverAndFail(
		    t,
		    "Panic on creating type baseline for test " + header,
		    [&] {
			    // !!! Remove once the type baselines print node reuse
			    // lines
			    auto typesOpts = opts;
			    typesOpts.DiffFixupOld = [](std::string s) {
				    std::string sb;
				    sb.reserve(s.size());

				    bool perfStats = false;
				    size_t pos = 0;
				    while (pos <= s.size()) {
					    auto nl = s.find('\n', pos);
					    std::string line =
					        nl == std::string::npos
					            ? s.substr(pos)
					            : s.substr(pos, nl - pos);
					    pos = nl == std::string::npos ? s.size() + 1
					                                  : nl + 1;
					    if (isTypeBaselineNodeReuseLine(line)) {
						    continue;
					    }

					    if (!perfStats &&
					        line.substr(0, 25) ==
					            "=== Performance Stats ===") {
						    perfStats = true;
						    continue;
					    } else if (perfStats) {
						    if (line.substr(0, 4) == "=== ") {
							    perfStats = false;
						    } else {
							    continue;
						    }
					    }

					    const std::string_view relativePrefixNew =
					        "=== ";
					    const std::string relativePrefixOld =
					        std::string(relativePrefixNew) + "./";
					    if (line.substr(
					            0, relativePrefixOld.size()) ==
					        relativePrefixOld) {
						    line = std::string(relativePrefixNew) +
						           line.substr(
						               relativePrefixOld.size());
					    }

					    sb += line;
					    sb += "\n";
				    }

				    // sb.String()[:sb.Len()-1]
				    if (!sb.empty()) sb.pop_back();
				    return sb;
			    };

			    checkBaselines(t, baselinePath, allFiles, fullWalker,
			                   header, typesOpts,
			                   false /*isSymbolBaseline*/);
		    });
	});
	t->Run("symbol", [&](gostd::testing::T* t) {
		testutil::withRecoverAndFail(
		    t,
		    "Panic on creating symbol baseline for test " + header,
		    [&] {
			    checkBaselines(t, baselinePath, allFiles, fullWalker,
			                   header, opts,
			                   true /*isSymbolBaseline*/);
		    });
	});
}

}  // namespace tsc::testutil::tsbaseline
