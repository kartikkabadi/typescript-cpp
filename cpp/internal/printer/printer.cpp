// printer.go — port of tsc/internal/printer/printer.go
//
// The Printer walks an AST and writes its text to an EmitTextWriter,
// dispatching on node kind, honoring EmitFlags from the EmitContext, and
// optionally emitting comments and source-map positions.
#include "internal/printer/printer.h"
#include "internal/scanner/scanner.h"

#include <algorithm>
#include <cassert>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace tsc::printer {

namespace {

template<typename T>
std::string panicPart(T&& v) {
	using D = std::decay_t<T>;
	if constexpr (std::is_same_v<D, Kind> || std::is_same_v<D, SnippetKind>) {
		return std::to_string(static_cast<int>(v));
	} else if constexpr (std::is_arithmetic_v<D>) {
		return std::to_string(v);
	} else {
		return std::string(std::forward<T>(v));
	}
}

template<typename... Ts>
[[noreturn]] inline void printerPanic(Ts&&... parts) {
	std::string msg = (panicPart(std::forward<Ts>(parts)) + ...);
	fprintf(stderr, "tsc::printer panic: %s\n", msg.c_str());
	fflush(stderr);
	TSC_UNREACHABLE(msg.c_str());
}

std::string kindName(Kind k) { return std::string(kindToString(k)); }

// NewLineKind.GetNewLineCharacter (core/compileroptions.go:501)
std::string getNewLineCharacter(NewLineKind newLine) {
	return newLine == NewLineKind::CarriageReturnLineFeed ? "\r\n" : "\n";
}

// Collect helpers — Go's getLeading/TrailingCommentRanges return iter.Seq;
// the C++ scanner API is callback-based (scanner.go GetLeadingCommentRanges).
std::vector<CommentRange> collectLeadingCommentRanges(std::string_view text,
                                                      TextPos pos) {
	std::vector<CommentRange> out;
	getLeadingCommentRanges(text, pos, [&](const CommentRange& c) {
		out.push_back(c);
		return true;
	});
	return out;
}

std::vector<CommentRange> collectTrailingCommentRanges(std::string_view text,
                                                       TextPos pos) {
	std::vector<CommentRange> out;
	getTrailingCommentRanges(text, pos, [&](const CommentRange& c) {
		out.push_back(c);
		return true;
	});
	return out;
}

// getIndentString — mirrors textwriter.cpp's indent string builder.
std::string getIndentString(int indent, int indentSize) {
	std::string result;
	result.reserve((size_t)indent * indentSize);
	for (int i = 0; i < indent; i++) {
		result.append((size_t)indentSize, ' ');
	}
	return result;
}

// NewTextRange helper matching core.NewTextRange.
inline TextRange newTextRange(TextPos pos, TextPos end) {
	return TextRange{pos, end};
}

} // namespace

// NewPrinter — printer.go:173
Printer* NewPrinter(const PrinterOptions& options, const PrintHandlers& handlers,
                    EmitContext* emitContext) {
	auto* printer = new Printer();
	printer->HasGlobalName = handlers.HasGlobalName;
	printer->MapSourcePosition = handlers.MapSourcePosition;
	printer->OnBeforeEmitNode = handlers.OnBeforeEmitNode;
	printer->OnAfterEmitNode = handlers.OnAfterEmitNode;
	printer->OnBeforeEmitNodeList = handlers.OnBeforeEmitNodeList;
	printer->OnAfterEmitNodeList = handlers.OnAfterEmitNodeList;
	printer->OnBeforeEmitToken = handlers.OnBeforeEmitToken;
	printer->OnAfterEmitToken = handlers.OnAfterEmitToken;
	printer->Options = options;
	printer->emitContext = emitContext;
	if (printer->emitContext == nullptr) {
		printer->emitContext = NewEmitContext();
	}
	printer->nameGenerator.Context = printer->emitContext;
	printer->nameGenerator.GetTextOfNode =
		[printer](Node* node) { return printer->getTextOfNode(node, false); };
	printer->nameGenerator.IsFileLevelUniqueNameInCurrentFile =
		[printer](const std::string& name, bool forGlobal) {
			return printer->isFileLevelUniqueNameInCurrentFile(name, forGlobal);
		};
	printer->makeFileLevelOptimisticUniqueName =
		[printer](const std::string& name) {
			return printer->nameGenerator.MakeFileLevelOptimisticUniqueName(name);
		};
	printer->containerPos = -1;
	printer->containerEnd = -1;
	printer->declarationListContainerEnd = -1;
	printer->commentsDisabled = options.RemoveComments;
	return printer;
}

// getLiteralTextOfNode — printer.go:196
std::string Printer::getLiteralTextOfNode(Node* node, SourceFile* sourceFile,
                                          getLiteralTextFlags flags) {
	if (isStringLiteral(node)) {
		auto it = emitContext->textSource.find(node);
		if (it != emitContext->textSource.end() && it->second != nullptr) {
			Node* textSourceNode = it->second;
			std::string text;
			switch (textSourceNode->kind) {
			case Kind::NumericLiteral:
				text = textSourceNode->text();
				break;
			case Kind::Identifier:
			case Kind::PrivateIdentifier:
			case Kind::JsxNamespacedName:
				text = getTextOfNode(textSourceNode, false);
				break;
			default:
				return getLiteralTextOfNode(
					textSourceNode, getSourceFileOfNode(textSourceNode),
					flags);
			}

			if (flags & getLiteralTextFlagsJsxAttributeEscape) {
				return "\"" +
				       escapeJsxAttributeString(text, QuoteCharDoubleQuote) +
				       "\"";
			} else if ((flags & getLiteralTextFlagsNeverAsciiEscape) != 0 ||
			           (emitContext->emitFlags(node) & EFNoAsciiEscaping) != 0) {
				return "\"" +
				       EscapeString(text, QuoteCharDoubleQuote) + "\"";
			} else {
				return "\"" +
				       escapeNonAsciiString(text, QuoteCharDoubleQuote) +
				       "\"";
			}
		}
	}
	// !!! Printer option to control whether to terminate unterminated literals
	if (emitContext->emitFlags(node) & EFNoAsciiEscaping) {
		flags |= getLiteralTextFlagsNeverAsciiEscape;
	}
	if (Options.Target >= ScriptTarget::ES2021) {
		flags |= getLiteralTextFlagsAllowNumericSeparator;
	}
	return getLiteralText(node,
	                      sourceFile != nullptr ? sourceFile
	                                            : currentSourceFile,
	                      flags);
}

// getTextOfNode — printer.go:230
// `node` must be one of Identifier | PrivateIdentifier | LiteralExpression |
// JsxNamespacedName
std::string Printer::getTextOfNode(Node* node, bool includeTrivia) {
	if (isMemberName(node)) {
		auto it = emitContext->autoGenerate.find(node);
		if (it != emitContext->autoGenerate.end()) {
			return nameGenerator.GenerateName(node);
		}
	}

	if (isStringLiteral(node)) {
		auto it = emitContext->textSource.find(node);
		if (it != emitContext->textSource.end() && it->second != nullptr) {
			return getTextOfNode(it->second, includeTrivia);
		}
	}

	bool canUseSourceFile = currentSourceFile != nullptr &&
	                        node->parent != nullptr && !nodeIsSynthesized(node);

	switch (node->kind) {
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
	case Kind::JsxNamespacedName:
		if (!canUseSourceFile ||
		    getSourceFileOfNode(node) !=
		        emitContext->mostOriginal(currentSourceFile->asNode())
		            ->as<SourceFile>()) {
			return node->text();
		}
		break;
	case Kind::StringLiteral:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::TemplateHead:
	case Kind::TemplateMiddle:
	case Kind::TemplateTail:
		return getLiteralTextOfNode(node, nullptr, getLiteralTextFlagsNone);
	default:
		printerPanic("unexpected node: ", kindName(node->kind));
	}
	return getSourceTextOfNodeFromSourceFile(currentSourceFile, node,
	                                       includeTrivia);
}

//
// Low-level writing — printer.go:264
//

void Printer::writeAs(const std::string& text, WriteKind writeKind_) {
	switch (writeKind_) {
	case WriteKind::None:
		writer->Write(text);
		break;
	case WriteKind::Parameter:
		writeParameter(text);
		break;
	case WriteKind::Keyword:
		writeKeyword(text);
		break;
	case WriteKind::Operator:
		writeOperator(text);
		break;
	case WriteKind::Property:
		writeProperty(text);
		break;
	case WriteKind::Punctuation:
		writePunctuation(text);
		break;
	case WriteKind::StringLiteral:
		writer->WriteStringLiteral(text);
		break;
	case WriteKind::Comment:
		writeComment(text);
		break;
	case WriteKind::Literal:
		writeLiteral(text);
		break;
	default:
		printerPanic("unexpected printer.WriteKind");
	}
}

void Printer::write(const std::string& text) { writeAs(text, writeKind); }

WriteKind Printer::setWriteKind(WriteKind kind) {
	WriteKind previous = writeKind;
	writeKind = kind;
	return previous;
}

void Printer::writeSymbol(const std::string& text, Symbol* optSymbol) {
	if (optSymbol == nullptr) {
		write(text);
	} else {
		writer->WriteSymbol(text, optSymbol);
	}
}

void Printer::writeLiteral(const std::string& text) {
	writer->WriteLiteral(text);
}
void Printer::writePunctuation(const std::string& text) {
	writer->WritePunctuation(text);
}
void Printer::writeOperator(const std::string& text) {
	writer->WriteOperator(text);
}
void Printer::writeKeyword(const std::string& text) {
	writer->WriteKeyword(text);
}
void Printer::writeProperty(const std::string& text) {
	writer->WriteProperty(text);
}
void Printer::writeParameter(const std::string& text) {
	writer->WriteParameter(text);
}
void Printer::writeComment(const std::string& text) {
	writer->WriteComment(text);
}
void Printer::writeSpace() { writer->WriteSpace(" "); }
void Printer::writeLine() { writer->WriteLine(); }

void Printer::writeLineRepeat(int count) {
	for (int i = 0; i < count; i++) {
		writeLine();
	}
}

void Printer::writeLines(const std::string& text) {
	auto lines = splitLines(text);
	int indentation = guessIndentation(lines);
	for (auto line : lines) {
		if (indentation > 0) {
			line = line.substr(indentation);
		}
		if (!line.empty()) {
			writeLine();
			write(std::string(line));
		}
	}
}

void Printer::writeTrailingSemicolon() {
	writer->WriteTrailingSemicolon(";");
}
void Printer::increaseIndent() { writer->IncreaseIndent(); }
void Printer::decreaseIndent() { writer->DecreaseIndent(); }
void Printer::increaseIndentIf(bool indentRequested) {
	if (indentRequested) {
		increaseIndent();
	}
}
void Printer::decreaseIndentIf(bool indentRequested) {
	if (indentRequested) {
		decreaseIndent();
	}
}

void Printer::writeLineOrSpace(Node* parentNode, Node* prevChildNode,
                               Node* nextChildNode) {
	if (shouldEmitOnSingleLine(parentNode)) {
		writeSpace();
	} else if (Options.PreserveSourceNewlines) {
		int lines = getLinesBetweenNodes(parentNode, prevChildNode,
		                                 nextChildNode);
		if (lines > 0) {
			writeLineRepeat(lines);
		} else {
			writeSpace();
		}
	} else {
		writeLine();
	}
}

void Printer::writeLinesAndIndent(int lineCount,
                                  bool writeSpaceIfNotIndenting) {
	if (lineCount > 0) {
		increaseIndent();
		writeLineRepeat(lineCount);
	} else if (writeSpaceIfNotIndenting) {
		writeSpace();
	}
}

bool Printer::writeLineSeparatorsAndIndentBefore(Node* node, Node* parent) {
	if (Options.PreserveSourceNewlines) {
		int leadingNewlines =
			getLeadingLineTerminatorCount(parent, node, LFNone);
		if (leadingNewlines > 0) {
			writeLinesAndIndent(leadingNewlines, /*writeSpaceIfNotIndenting*/ false);
			return true;
		}
	}
	return false;
}

void Printer::writeLineSeparatorsAfter(Node* node, Node* parent) {
	if (Options.PreserveSourceNewlines) {
		int trailingNewlines = getClosingLineTerminatorCount(
			parent, node, LFNone, TextRange{-1, -1});
		if (trailingNewlines > 0) {
			writeLineRepeat(trailingNewlines);
		}
	}
}

int Printer::getLinesBetweenNodes(Node* parent, Node* node1, Node* node2) {
	if (shouldElideIndentation(parent)) {
		return 0;
	}

	parent = skipSynthesizedParentheses(parent);
	node1 = skipSynthesizedParentheses(node1);
	node2 = skipSynthesizedParentheses(node2);

	// Always use a newline for synthesized code if the synthesizer desires it.
	if (shouldEmitOnNewLine(node2, LFNone)) {
		return 1;
	}

	if (currentSourceFile != nullptr && !nodeIsSynthesized(parent) &&
	    !nodeIsSynthesized(node1) && !nodeIsSynthesized(node2)) {
		if (Options.PreserveSourceNewlines) {
			return getEffectiveLines([this, node1, node2](bool includeComments) {
				return getLinesBetweenRangeEndAndRangeStart(
					node1->loc, node2->loc, currentSourceFile,
					includeComments);
			});
		}
		return rangeEndIsOnSameLineAsRangeStart(node1->loc, node2->loc,
		                                        currentSourceFile)
		           ? 0
		           : 1;
	}

	return 0;
}

int Printer::getEffectiveLines(
	const std::function<int(bool)>& getLineDifference) {
	// If 'preserveSourceNewlines' is disabled, we should never call this
	// function because it could be more expensive than alternative
	// approximations.
	if (!Options.PreserveSourceNewlines) {
		printerPanic("Should not be called when preserveSourceNewlines is false");
	}
	// We start by measuring the line difference from a position to its adjacent
	// comments, so that this is counted as a one-line difference, not two:
	//
	//   node1;
	//   // NODE2 COMMENT
	//   node2;
	int lines = getLineDifference(/*includeComments*/ true);
	if (lines == 0) {
		// However, if the line difference considering comments was 0, we might
		// have this:
		//
		//   node1; // NODE2 COMMENT
		//   node2;
		//
		// in which case we should be ignoring node2's comment, so this too is
		// counted as a one-line difference, not zero.
		return getLineDifference(/*includeComments*/ false);
	}
	return lines;
}

int Printer::getLeadingLineTerminatorCount(Node* parentNode, Node* firstChild,
                                           ListFormat format) {
	if ((format & LFPreserveLines) != 0 || Options.PreserveSourceNewlines) {
		if (format & LFPreferNewLine) {
			return 1;
		}

		if (firstChild == nullptr) {
			return parentNode == nullptr ||
			               (currentSourceFile != nullptr &&
			                RangeIsOnSingleLine(parentNode->loc,
			                                    currentSourceFile))
			           ? 0
			           : 1;
		}
		if (nextListElementPos > 0 &&
		    firstChild->pos() == nextListElementPos) {
			// If this child starts at the beginning of a list item in a parent
			// list, its leading line terminators have already been written as
			// the separating line terminators of the parent list.
			return 0;
		}
		if (firstChild->kind == Kind::JsxText) {
			// JsxText will be written with its leading whitespace, so don't add
			// more manually.
			return 0;
		}
		if (currentSourceFile != nullptr && parentNode != nullptr &&
		    !positionIsSynthesized(parentNode->pos()) &&
		    !nodeIsSynthesized(firstChild) &&
		    (firstChild->parent == nullptr)) {
			if (Options.PreserveSourceNewlines) {
				return getEffectiveLines(
					[this, firstChild, parentNode](bool includeComments) {
						return getLinesBetweenPositionAndPrecedingNonWhitespaceCharacter(
							firstChild->pos(), parentNode->pos(),
							currentSourceFile, includeComments);
					});
			}
			return RangeStartPositionsAreOnSameLine(parentNode->loc,
			                                        firstChild->loc,
			                                        currentSourceFile)
			           ? 0
			           : 1;
		}
		if (shouldEmitOnNewLine(firstChild, format)) {
			return 1;
		}
	}
	return (format & LFMultiLine) != 0 ? 1 : 0;
}

int Printer::getSeparatingLineTerminatorCount(Node* previousNode,
                                              Node* nextNode,
                                              ListFormat format) {
	if ((format & LFPreserveLines) != 0 || Options.PreserveSourceNewlines) {
		if (previousNode == nullptr || nextNode == nullptr) {
			return 0;
		}
		if (nextNode->kind == Kind::JsxText) {
			// JsxText will be written with its leading whitespace, so don't add
			// more manually.
			return 0;
		} else if (currentSourceFile != nullptr &&
		           !nodeIsSynthesized(previousNode) &&
		           !nodeIsSynthesized(nextNode)) {
			if (Options.PreserveSourceNewlines &&
			    siblingNodePositionsAreComparable(emitContext, previousNode,
			                                      nextNode)) {
				return getEffectiveLines(
					[this, previousNode, nextNode](bool includeComments) {
						return getLinesBetweenRangeEndAndRangeStart(
							previousNode->loc, nextNode->loc,
							currentSourceFile, includeComments);
					});
			} else if (!Options.PreserveSourceNewlines &&
			           originalNodesHaveSameParent(emitContext, previousNode,
			                                       nextNode)) {
				return rangeEndIsOnSameLineAsRangeStart(previousNode->loc,
				                                        nextNode->loc,
				                                        currentSourceFile)
				           ? 0
				           : 1;
			}
			return (format & LFPreferNewLine) != 0 ? 1 : 0;
		} else if (shouldEmitOnNewLine(previousNode, format) ||
		           shouldEmitOnNewLine(nextNode, format)) {
			return 1;
		}
	} else if (shouldEmitOnNewLine(nextNode, LFNone)) {
		return 1;
	}
	return (format & LFMultiLine) != 0 ? 1 : 0;
}

int Printer::getClosingLineTerminatorCount(Node* parentNode, Node* lastChild,
                                           ListFormat format,
                                           TextRange childrenTextRange) {
	if ((format & LFPreserveLines) != 0 || Options.PreserveSourceNewlines) {
		if (format & LFPreferNewLine) {
			return 1;
		}
		if (lastChild == nullptr) {
			return parentNode == nullptr ||
			               (currentSourceFile != nullptr &&
			                RangeIsOnSingleLine(parentNode->loc,
			                                    currentSourceFile))
			           ? 0
			           : 1;
		}
		if (currentSourceFile != nullptr && parentNode != nullptr &&
		    !positionIsSynthesized(parentNode->pos()) &&
		    !nodeIsSynthesized(lastChild) &&
		    (lastChild->parent == nullptr ||
		     lastChild->parent == parentNode)) {
			if (Options.PreserveSourceNewlines) {
				TextPos end = greatestEnd(lastChild->end(), childrenTextRange);
				return getEffectiveLines(
					[this, end, parentNode](bool includeComments) {
						return getLinesBetweenPositionAndNextNonWhitespaceCharacter(
							end, parentNode->end(), currentSourceFile,
							includeComments);
					});
			}
			return rangeEndPositionsAreOnSameLine(parentNode->loc,
			                                      lastChild->loc,
			                                      currentSourceFile)
			           ? 0
			           : 1;
		}
		if (shouldEmitOnNewLine(lastChild, format)) {
			return 1;
		}
	}
	if ((format & LFMultiLine) != 0 && (format & LFNoTrailingNewLine) == 0) {
		return 1;
	}
	return 0;
}

// writeCommentRange — printer.go:638
void Printer::writeCommentRange(CommentRange comment) {
	if (currentSourceFile == nullptr) {
		return;
	}

	const std::string& text = currentSourceFile->text;
	const std::vector<TextPos>& lineMap = currentSourceFile->ecmaLineMap();
	writeCommentRangeWorker(text, lineMap, comment.kind, comment);
}

void Printer::writeCommentRangeWorker(std::string_view text,
                                      const std::vector<TextPos>& lineMap,
                                      Kind kind, TextRange loc) {
	if (kind == Kind::MultiLineCommentTrivia) {
		int indentSize = GetDefaultIndentSize();
		int firstLine = computeLineOfPosition(lineMap, loc.pos());
		int lineCount = (int)lineMap.size();
		int firstCommentLineIndent = -1;
		TextPos pos = loc.pos();
		for (int currentLine = firstLine; pos < loc.end(); currentLine++) {
			int nextLineStart;
			if (currentLine + 1 == lineCount) {
				nextLineStart = (int)text.size() + 1;
			} else {
				nextLineStart = lineMap[currentLine + 1];
			}

			if (pos != loc.pos()) {
				// If we are not emitting first line, we need to write the
				// spaces to adjust the alignment
				if (firstCommentLineIndent == -1) {
					firstCommentLineIndent = calculateIndent(
						text, lineMap[firstLine], loc.pos());
				}

				// These are number of spaces writer is going to write at
				// current indent
				int currentWriterIndentSpacing =
					writer->GetIndent() * indentSize;

				// Number of spaces we want to be writing
				int spacesToEmit =
					currentWriterIndentSpacing - firstCommentLineIndent +
					calculateIndent(text, pos, nextLineStart);
				if (spacesToEmit > 0) {
					int numberOfSingleSpacesToEmit =
						spacesToEmit % indentSize;
					std::string indentSizeSpaceString = getIndentString(
						(spacesToEmit - numberOfSingleSpacesToEmit) /
						    indentSize,
						indentSize);

					// Write indent size string
					writer->RawWrite(indentSizeSpaceString);

					// Emit the single spaces
					while (numberOfSingleSpacesToEmit > 0) {
						writer->RawWrite(" ");
						numberOfSingleSpacesToEmit--;
					}
				} else {
					// No spaces to emit write empty string
					writer->RawWrite("");
				}
			}

			// Write the comment line text
			TextPos end = std::min(loc.end(), (TextPos)nextLineStart);
			for (TextPos scan = pos; scan < end;) {
				int size = 0;
				char32_t ch = decodeUtf8Rune(
					text.substr(scan, end - scan), &size);
				if (size == 0) {
					break;
				}
				if (isLineBreak(ch)) {
					end = scan;
					break;
				}
				scan += size;
			}
			std::string currentLineText = std::string(
				trimSpace(text.substr(pos, end - pos)));
			if (!currentLineText.empty()) {
				writeComment(currentLineText);
				if (end != loc.end()) {
					writeLine();
				}
			} else {
				// Empty string - make sure we write empty line
				writer->WriteLineForce(true);
			}

			pos = nextLineStart;
		}
	} else {
		// Single line comment of style //....
		writeComment(std::string(text.substr(loc.pos(), loc.end() - loc.pos())));
	}
}

//
// Custom emit behavior stubs — printer.go:739
//

bool Printer::shouldEmitComments(Node* node) {
	return !commentsDisabled && currentSourceFile != nullptr &&
	       !isSourceFile(node);
}

bool Printer::shouldWriteComment(CommentRange comment) {
	return !Options.OnlyPrintJSDocStyle ||
	       (currentSourceFile != nullptr &&
	        isJSDocLikeText(currentSourceFile->text, comment)) ||
	       (currentSourceFile != nullptr &&
	        IsPinnedComment(currentSourceFile->text, comment));
}

bool Printer::shouldEmitIndented(Node* node) {
	return (emitContext->emitFlags(node) & EFIndented) != 0;
}

bool Printer::shouldElideIndentation(Node* node) {
	return (emitContext->emitFlags(node) & EFNoIndentation) != 0;
}

bool Printer::shouldEmitOnSingleLine(Node* node) {
	return (emitContext->emitFlags(node) & EFSingleLine) != 0;
}

bool Printer::shouldEmitOnMultipleLines(Node* node) {
	return (emitContext->emitFlags(node) & EFMultiLine) != 0;
}

bool Printer::shouldEmitBlockFunctionBodyOnSingleLine(Node* body) {
	// We must emit a function body as a single-line body in the following
	// case:
	// * The body has NodeEmitFlags.SingleLine specified.
	//
	// We must emit a function body as a multi-line body in the following cases:
	// * The body is explicitly marked as multi-line.
	// * A non-synthesized body's start and end position are on different lines.
	// * Any statement in the body starts on a new line.

	if (shouldEmitOnSingleLine(body)) {
		return true;
	}

	if (body->as<Block>()->MultiLine) {
		return false;
	}

	if (!nodeIsSynthesized(body) && currentSourceFile != nullptr &&
	    !RangeIsOnSingleLine(body->loc, currentSourceFile)) {
		return false;
	}

	auto* statements = body->as<Block>()->Statements;
	if (getLeadingLineTerminatorCount(
		    body, firstOrNil(statements->nodes), LFPreserveLines) > 0 ||
	    getClosingLineTerminatorCount(body, lastOrNil(statements->nodes),
	                                  LFPreserveLines, statements->loc) > 0) {
		return false;
	}

	Node* previousStatement = nullptr;
	for (auto* statement : statements->nodes) {
		if (getSeparatingLineTerminatorCount(previousStatement, statement,
		                                     LFPreserveLines) > 0) {
			return false;
		}
		previousStatement = statement;
	}

	return true;
}

bool Printer::shouldEmitOnNewLine(Node* node, ListFormat format) {
	if (emitContext->emitFlags(node) & EFStartOnNewLine) {
		return true;
	}
	return (format & LFPreferNewLine) != 0;
}

bool Printer::shouldEmitSourceMaps(Node* node) {
	return !sourceMapsDisabled && sourceMapSource != nullptr &&
	       !isSourceFile(node) && !isInJsonFile(node);
}

bool Printer::shouldEmitTokenSourceMaps(Kind token, TextPos pos,
                                        Node* contextNode,
                                        tokenEmitFlags flags) {
	// We don't emit source positions for most tokens as it tends to be quite
	// noisy, however we need to emit source positions for open and close braces
	// so that tools like istanbul can map branches for code coverage. However,
	// we still omit brace source positions when the output is a declaration
	// file.
	return (flags & tefNoSourceMaps) == 0 && shouldEmitSourceMaps(contextNode) &&
	       !Options.OmitBraceSourceMapPositions &&
	       (token == Kind::OpenBraceToken || token == Kind::CloseBraceToken);
}

bool Printer::shouldEmitLeadingComments(Node* node) {
	return (emitContext->emitFlags(node) & EFNoLeadingComments) == 0;
}

bool Printer::shouldEmitTrailingComments(Node* node) {
	return (emitContext->emitFlags(node) & EFNoTrailingComments) == 0;
}

bool Printer::shouldEmitNestedComments(Node* node) {
	return (emitContext->emitFlags(node) & EFNoNestedComments) == 0;
}

bool Printer::shouldEmitDetachedComments(Node* node) {
	if (!isSourceFile(node)) {
		return true;
	}

	auto* file = node->as<SourceFile>();

	// Emit detached comment if there are no prologue directives or if the
	// first node is synthesized. The synthesized node will have no leading
	// comment so some comments may be missed.
	return file->Statements->nodes.empty() ||
	       !isPrologueDirective(file->Statements->nodes[0]) ||
	       nodeIsSynthesized(file->Statements->nodes[0]);
}

bool Printer::hasCommentsAtPosition(TextPos pos) {
	if (currentSourceFile == nullptr) {
		return false;
	}

	bool found = false;
	getTrailingCommentRanges(currentSourceFile->text, pos + 1,
	                         [&](const CommentRange&) {
		                         found = true;
		                         return false;
	                         });
	if (found) {
		return true;
	}
	getLeadingCommentRanges(currentSourceFile->text, pos + 1,
	                        [&](const CommentRange&) {
		                        found = true;
		                        return false;
	                        });
	return found;
}

bool Printer::shouldEmitIndirectCall(Node* node) {
	return (emitContext->emitFlags(node) & EFIndirectCall) != 0;
}

bool Printer::shouldAllowTrailingComma(Node* node, NodeList* list) {
	if (currentSourceFile == nullptr ||
	    currentSourceFile->ScriptKind == ScriptKind::JSON) {
		return false;
	}

	switch (node->kind) {
	case Kind::ObjectLiteralExpression:
		return true;
	case Kind::ArrayLiteralExpression:
	case Kind::ArrowFunction:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::FunctionType:
	case Kind::ConstructorType:
	case Kind::CallSignature:
	case Kind::ConstructSignature:
	case Kind::TaggedTemplateExpression:
	case Kind::ObjectBindingPattern:
	case Kind::ArrayBindingPattern:
	case Kind::NamedImports:
	case Kind::NamedExports:
	case Kind::ImportAttributes:
		return true;
	case Kind::ClassExpression:
	case Kind::ClassDeclaration:
	case Kind::InterfaceDeclaration:
		return list == node->typeParameterList();
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::MethodDeclaration:
		return true;
	case Kind::CallExpression:
		return true;
	case Kind::NewExpression:
		return true;
	default:
		break;
	}

	return false;
}

//
// Tokens/Keywords — printer.go:920
//

int Printer::writeTokenText(Kind token, WriteKind writeKind_, TextPos pos) {
	// !!! emit leading and trailing comments
	// !!! emit leading and trailing source maps
	std::string tokenString = std::string(tokenToString(token));
	writeAs(tokenString, writeKind_);
	if (positionIsSynthesized(pos)) {
		return pos;
	}
	return pos + (int)tokenString.size();
}

int Printer::emitToken(Kind token, TextPos pos, WriteKind writeKind_,
                       Node* contextNode) {
	return emitTokenEx(token, pos, writeKind_, contextNode, tefNone);
}

int Printer::emitTokenEx(Kind token, TextPos pos, WriteKind writeKind_,
                         Node* contextNode, tokenEmitFlags flags) {
	auto [state, newPos] = enterToken(token, pos, contextNode, flags);
	pos = newPos;
	pos = writeTokenText(token, writeKind_, pos);
	exitToken(token, pos, contextNode, state);
	return pos;
}

void Printer::emitKeywordNode(Node* node) {
	emitKeywordNodeEx(node, tefNone);
}

void Printer::emitKeywordNodeEx(Node* node, tokenEmitFlags flags) {
	if (node == nullptr) {
		return;
	}

	printerState state = enterTokenNode(node, flags);
	writeTokenText(node->kind, WriteKind::Keyword, node->pos());
	exitTokenNode(node, state);
}

void Printer::emitPunctuationNode(Node* node) {
	emitPunctuationNodeEx(node, tefNone);
}

void Printer::emitPunctuationNodeEx(Node* node, tokenEmitFlags flags) {
	if (node == nullptr) {
		return;
	}

	printerState state = enterTokenNode(node, flags);
	writeTokenText(node->kind, WriteKind::Punctuation, node->pos());
	exitTokenNode(node, state);
}

void Printer::emitTokenNode(Node* node) { emitTokenNodeEx(node, tefNone); }

void Printer::emitTokenNodeEx(Node* node, tokenEmitFlags flags) {
	if (node == nullptr) {
		return;
	}

	if (isKeywordKind(node->kind)) {
		emitKeywordNodeEx(node, flags);
	} else if (isPunctuationKind(node->kind)) {
		emitPunctuationNodeEx(node, flags);
	} else {
		printerPanic("unexpected TokenNode: ", kindName(node->kind));
	}
}

//
// Literals — printer.go:995
//

void Printer::emitLiteral(Node* node, getLiteralTextFlags flags) {
	// Add NeverAsciiEscape flag if the printer option is set
	if (Options.NeverAsciiEscape) {
		flags |= getLiteralTextFlagsNeverAsciiEscape;
	}
	if (Options.TerminateUnterminatedLiterals) {
		flags |= getLiteralTextFlagsTerminateUnterminatedLiterals;
	}

	std::string text = getLiteralTextOfNode(node, nullptr, flags);

	// Quick info expects all literals to be called with writeStringLiteral, as
	// there's no specific type for numberLiterals
	writer->WriteStringLiteral(text);
}

void Printer::emitNumericLiteral(Node* node) {
	printerState state = enterNode(node);
	emitLiteral(node, getLiteralTextFlagsNone);
	exitNode(node, state);
}

void Printer::emitBigIntLiteral(Node* node) {
	printerState state = enterNode(node);
	emitLiteral(node,
	            getLiteralTextFlagsNone); // TODO: Preserve numeric literal
	                                      // separators after Strada migration
	exitNode(node, state);
}

void Printer::emitStringLiteral(Node* node) {
	printerState state = enterNode(node);
	emitLiteral(node, getLiteralTextFlagsNone);
	exitNode(node, state);
}

void Printer::emitNoSubstitutionTemplateLiteral(Node* node) {
	printerState state = enterNode(node);
	emitLiteral(node, getLiteralTextFlagsNone);
	exitNode(node, state);
}

void Printer::emitRegularExpressionLiteral(Node* node) {
	printerState state = enterNode(node);
	emitLiteral(node, getLiteralTextFlagsNone);
	exitNode(node, state);
}

//
// Pseudo-literals — printer.go:1065
//

void Printer::emitTemplateHead(Node* node) {
	printerState state = enterNode(node);
	emitLiteral(node, getLiteralTextFlagsNone);
	exitNode(node, state);
}

void Printer::emitTemplateMiddle(Node* node) {
	printerState state = enterNode(node);
	emitLiteral(node, getLiteralTextFlagsNone);
	exitNode(node, state);
}

void Printer::emitTemplateTail(Node* node) {
	printerState state = enterNode(node);
	emitLiteral(node, getLiteralTextFlagsNone);
	exitNode(node, state);
}

void Printer::emitTemplateMiddleTail(Node* node) {
	switch (node->kind) {
	case Kind::TemplateMiddle:
		emitTemplateMiddle(node);
		break;
	case Kind::TemplateTail:
		emitTemplateTail(node);
		break;
	default:
		break;
	}
}

//
// Snippet Elements — printer.go:1096
//

void Printer::emitSnippetNode(Node* node, SnippetElement* snippetElement) {
	switch (snippetElement->Kind) {
	case SnippetKind::TabStop:
		emitTabStop(node, snippetElement);
		break;
	default:
		printerPanic("Unhandled snippet element kind");
	}
}

void Printer::emitTabStop(Node* node, SnippetElement* snippetElement) {
	assert(node->kind == Kind::EmptyStatement &&
	       "Snippet tab stops can only be emitted on empty statements");
	writer->RawWrite("$" + std::to_string(snippetElement->Order));
}

//
// Names — printer.go:1114
//

void Printer::emitIdentifierText(Node* node) {
	SourceFile* f = getSourceFileOfNode(node);
	(void)f; // assert omitted (debug.Assert)
	std::string text = getTextOfNode(node, /*includeTrivia*/ false);

	if (!IdToSymbol.empty()) {
		auto it = IdToSymbol.find(node);
		if (it != IdToSymbol.end()) {
			writeSymbol(text, it->second);
			return;
		}
	}
	write(text);
}

void Printer::emitIdentifierName(Node* node) {
	printerState state = enterNode(node);
	emitIdentifierText(node);
	exitNode(node, state);
}

void Printer::emitIdentifierNameNode(Node* node) {
	if (node == nullptr) {
		return;
	}
	emitIdentifierName(node);
}

Node* Printer::getUniqueHelperName(const std::string& name) {
	Node* helperName = nullptr;
	auto it = uniqueHelperNames.find(name);
	if (it != uniqueHelperNames.end()) {
		helperName = it->second;
	}
	if (helperName == nullptr) {
		helperName = emitContext->factory.newUniqueName(
			name,
			AutoGenerateOptions{
				.Flags = GeneratedIdentifierFlagsFileLevel |
				         GeneratedIdentifierFlagsOptimistic,
			});
		generateName(helperName);
		uniqueHelperNames[name] = helperName;
		return helperName;
	}
	return helperName->clone(emitContext->factory);
}

void Printer::emitIdentifierReference(Node* node) {
	if ((externalHelpersModuleName != nullptr || uniqueHelperNamesSet) &&
	    (emitContext->emitFlags(node) & EFHelperName) != 0) {
		if (externalHelpersModuleName != nullptr) {
			// Substitute `__helper` with `tslib_1.__helper`
			Node* helper = emitContext->factory.newPropertyAccessExpression(
				externalHelpersModuleName->clone(emitContext->factory),
				/*questionDotToken*/ nullptr,
				node->clone(emitContext->factory), NodeFlagsNone);
			emitContext->assignCommentAndSourceMapRanges(helper, node);
			emitPropertyAccessExpression(helper);
			return;
		}
		if (uniqueHelperNamesSet) {
			// Substitute `__helper` with `__helper_1` if there is a conflict in
			// an ES module.
			Node* helperName = getUniqueHelperName(node->text());
			emitContext->assignCommentAndSourceMapRanges(helperName, node);
			node = helperName;
		}
	}

	printerState state = enterNode(node);
	emitIdentifierText(node);
	exitNode(node, state);
}

void Printer::emitBindingIdentifier(Node* node) {
	if (uniqueHelperNamesSet &&
	    (emitContext->emitFlags(node) & EFHelperName) != 0) {
		// Substitute `__helper` with `__helper_1` if there is a conflict in an
		// ES module.
		Node* helperName = getUniqueHelperName(node->text());
		emitContext->assignCommentAndSourceMapRanges(helperName, node);
		node = helperName;
	}

	printerState state = enterNode(node);
	emitIdentifierText(node);
	exitNode(node, state);
}

void Printer::emitLabelIdentifier(Node* node) {
	printerState state = enterNode(node);
	emitIdentifierText(node);
	exitNode(node, state);
}

void Printer::emitPrivateIdentifier(Node* node) {
	printerState state = enterNode(node);
	write(getTextOfNode(node, /*includeTrivia*/ false));
	exitNode(node, state);
}

void Printer::emitQualifiedName(Node* node) {
	auto* n = node->as<QualifiedName>();
	printerState state = enterNode(node);
	emitEntityName(n->Left);
	writePunctuation(".");
	emitMemberName(n->Right);
	exitNode(node, state);
}

void Printer::emitComputedPropertyName(Node* node) {
	auto* n = node->as<ComputedPropertyName>();
	printerState state = enterNode(node);
	writePunctuation("[");
	emitExpression(n->Expression, OperatorPrecedenceDisallowComma);
	writePunctuation("]");
	exitNode(node, state);
}

void Printer::emitEntityName(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
		emitIdentifierReference(node);
		break;
	case Kind::QualifiedName:
		emitQualifiedName(node);
		break;
	case Kind::PropertyAccessExpression:
		// TypeQuery nodes may have PropertyAccessExpression as exprName.
		emitExpression(node, OperatorPrecedenceDisallowComma);
		break;
	default:
		printerPanic("unexpected EntityName: ", kindName(node->kind));
	}
}

void Printer::emitBindingName(Node* node) {
	if (node == nullptr) {
		return;
	}

	switch (node->kind) {
	case Kind::Identifier:
		emitBindingIdentifier(node);
		break;
	case Kind::ObjectBindingPattern:
		emitObjectBindingPattern(node);
		break;
	case Kind::ArrayBindingPattern:
		emitArrayBindingPattern(node);
		break;
	default:
		printerPanic("unexpected BindingName: ", kindName(node->kind));
	}
}

void Printer::emitPropertyName(Node* node) {
	if (node == nullptr) {
		return;
	}

	WriteKind savedWriteKind = writeKind;
	writeKind = WriteKind::Property;

	switch (node->kind) {
	case Kind::Identifier:
		emitIdentifierName(node);
		break;
	case Kind::PrivateIdentifier:
		emitPrivateIdentifier(node);
		break;
	case Kind::StringLiteral:
		emitStringLiteral(node);
		break;
	case Kind::NoSubstitutionTemplateLiteral:
		emitNoSubstitutionTemplateLiteral(node);
		break;
	case Kind::NumericLiteral:
		emitNumericLiteral(node);
		break;
	case Kind::BigIntLiteral:
		emitBigIntLiteral(node);
		break;
	case Kind::ComputedPropertyName:
		emitComputedPropertyName(node);
		break;
	default:
		printerPanic("unexpected PropertyName: ", kindName(node->kind));
	}

	writeKind = savedWriteKind;
}

void Printer::emitMemberName(Node* node) {
	if (node == nullptr) {
		return;
	}

	switch (node->kind) {
	case Kind::Identifier:
		emitIdentifierName(node);
		break;
	case Kind::PrivateIdentifier:
		emitPrivateIdentifier(node);
		break;
	default:
		printerPanic("unexpected MemberName: ", kindName(node->kind));
	}
}

void Printer::emitModuleName(Node* node) {
	if (node == nullptr) {
		return;
	}

	switch (node->kind) {
	case Kind::Identifier:
		emitBindingIdentifier(node);
		break;
	case Kind::StringLiteral:
		emitStringLiteral(node);
		break;
	default:
		printerPanic("unexpected ModuleName: ", kindName(node->kind));
	}
}

void Printer::emitModuleExportName(Node* node) {
	if (node == nullptr) {
		return;
	}

	switch (node->kind) {
	case Kind::Identifier:
		emitIdentifierName(node);
		break;
	case Kind::StringLiteral:
		emitStringLiteral(node);
		break;
	default:
		printerPanic("unexpected ModuleExportName: ", kindName(node->kind));
	}
}

void Printer::emitImportAttributeName(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
		emitIdentifierName(node);
		break;
	case Kind::StringLiteral:
		emitStringLiteral(node);
		break;
	default:
		printerPanic("unexpected ImportAttributeName: ", kindName(node->kind));
	}
}

void Printer::emitNestedModuleName(Node* node) {
	if (node == nullptr) {
		return;
	}

	switch (node->kind) {
	case Kind::Identifier:
		emitIdentifierName(node);
		break;
	case Kind::StringLiteral:
		emitStringLiteral(node);
		break;
	default:
		printerPanic("unexpected ModuleName: ", kindName(node->kind));
	}
}

//
// Signature elements — printer.go:1359
//

int Printer::emitModifierList(Node* parentNode, ModifierList* modifiers,
                              bool allowDecorators) {
	if (modifiers == nullptr || modifiers->nodes.empty()) {
		return parentNode->pos();
	}

	auto allModifiers = std::all_of(
		modifiers->nodes.begin(), modifiers->nodes.end(),
		[](Node* m) { return isModifier(m); });
	auto allDecorators = std::all_of(
		modifiers->nodes.begin(), modifiers->nodes.end(),
		[](Node* m) { return isDecorator(m); });

	if (allModifiers) {
		// if all modifier-likes are `Modifier`, simply emit the list as
		// modifiers.
		emitList(&Printer::emitKeywordNode, parentNode, modifiers, LFModifiers);
	} else if (allDecorators) {
		if (!allowDecorators) {
			return parentNode->pos();
		}

		// if all modifier-likes are `Decorator`, simply emit the list as
		// decorators.
		emitList(&Printer::emitModifierLike, parentNode, modifiers,
		         LFDecorators);
	} else {
		if (OnBeforeEmitNodeList) {
			OnBeforeEmitNodeList(modifiers);
		}

		// partition modifiers into contiguous chunks of `Modifier` or
		// `Decorator` so as to use consistent formatting for each chunk
		enum Mode {
			ModeNone,
			ModeModifiers,
			ModeDecorators,
		};

		Mode lastMode = ModeNone;
		Mode mode = ModeNone;
		size_t start = 0;
		size_t pos = 0;
		size_t len = modifiers->nodes.size();

		Node* lastModifier = nullptr;
		while (start < len) {
			while (pos < len) {
				lastModifier = modifiers->nodes[pos];
				if (isDecorator(lastModifier)) {
					mode = ModeDecorators;
				} else {
					mode = ModeModifiers;
				}
				if (lastMode == ModeNone) {
					lastMode = mode;
				} else if (mode != lastMode) {
					break;
				}
				pos++;
			}

			TextRange textRange{-1, -1};
			if (start == 0) {
				textRange = TextRange{modifiers->pos(), textRange.end()};
			}
			if (pos == len - 1) {
				textRange = TextRange{textRange.pos(), modifiers->end()};
			}
			if (allowDecorators || lastMode == ModeModifiers) {
				std::vector<Node*> chunk(modifiers->nodes.begin() + start,
				                         modifiers->nodes.begin() + pos);
				emitListItems(&Printer::emitModifierLike, parentNode, chunk,
				              lastMode == ModeModifiers ? LFModifiers
				                                        : LFDecorators,
				              /*hasTrailingComma*/ false, textRange);
			}
			start = pos;
			lastMode = mode;
			pos++;
		}

		if (OnAfterEmitNodeList) {
			OnAfterEmitNodeList(modifiers);
		}
	}

	return greatestEnd(parentNode->pos(), lastOrNil(modifiers->nodes));
}

void Printer::emitTypeParameter(Node* node) {
	auto* n = node->as<TypeParameterDeclaration>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	emitBindingIdentifier(n->name);
	if (n->Constraint != nullptr) {
		writeSpace();
		writeKeyword("extends");
		writeSpace();
		emitTypeNodeOutsideExtends(n->Constraint);
	}
	if (n->DefaultType != nullptr) {
		writeSpace();
		writeOperator("=");
		writeSpace();
		emitTypeNodeOutsideExtends(n->DefaultType);
	}
	exitNode(node, state);
}

void Printer::emitTypeParameterDeclarationNode(Node* node) {
	// NOTE: QuickInfo uses TypeFormatFlagsWriteTypeArgumentsOfSignature to
	// instruct the NodeBuilder to store type arguments (i.e. type nodes)
	// instead of type parameter declarations in the type parameter list.
	if (isTypeParameterDeclaration(node)) {
		emitTypeParameter(node);
	} else {
		emitTypeArgument(node);
	}
}

void Printer::emitParameterName(Node* node) {
	WriteKind savedWriteKind = writeKind;
	writeKind = WriteKind::Parameter;
	emitBindingName(node);
	writeKind = savedWriteKind;
}

void Printer::emitParameter(Node* node) {
	auto* n = node->as<ParameterDeclaration>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ true);
	emitTokenNode(n->DotDotDotToken);
	emitParameterName(n->name);
	emitTokenNode(n->QuestionToken);

	emitTypeAnnotation(n->Type);

	emitInitializer(n->Initializer,
	                greatestEnd(node->pos(), n->Type, n->QuestionToken, n->name,
	                            node->modifiers()),
	                node);
	exitNode(node, state);
}

void Printer::emitParameterDeclarationNode(Node* node) {
	emitParameter(node);
}

void Printer::emitDecorator(Node* node) {
	auto* n = node->as<Decorator>();
	printerState state = enterNode(node);
	writePunctuation("@");
	emitExpression(n->Expression, OperatorPrecedenceLeftHandSide);
	exitNode(node, state);
}

void Printer::emitModifierLike(Node* node) {
	if (isDecorator(node)) {
		emitDecorator(node);
	} else if (isModifier(node)) {
		emitKeywordNode(node);
	} else {
		printerPanic("unhandled ModifierLike: ", kindName(node->kind));
	}
}

void Printer::emitTypeParameters(Node* parentNode, NodeList* nodes) {
	if (nodes == nullptr) {
		return;
	}
	emitList(&Printer::emitTypeParameterDeclarationNode, parentNode, nodes,
	         LFTypeParameters | (isArrowFunction(parentNode)
	                                 ? LFAllowTrailingComma
	                                 : LFNone));
}

void Printer::emitTypeAnnotation(Node* node) {
	if (node == nullptr) {
		return;
	}

	writePunctuation(":");
	writeSpace();
	emitTypeNodeOutsideExtends(node);
}

void Printer::emitInitializer(Node* node, TextPos equalTokenPos,
                              Node* contextNode) {
	if (node == nullptr) {
		return;
	}

	writeSpace();
	emitToken(Kind::EqualsToken, equalTokenPos, WriteKind::Operator,
	          contextNode);
	writeSpace();
	emitExpression(node, OperatorPrecedenceDisallowComma);
}

void Printer::emitParameters(Node* parentNode, NodeList* parameters) {
	generateAllNames(parameters);
	emitList(&Printer::emitParameterDeclarationNode, parentNode, parameters,
	         LFParameters);
}

// canEmitSimpleArrowHead — printer.go:1550 (free function)
static bool canEmitSimpleArrowHeadFn(Node* parentNode, NodeList* parameters) {
	// only arrow functions with a single parameter may have simple arrow head
	if (!isArrowFunction(parentNode) || parameters->nodes.size() != 1) {
		return false;
	}

	auto* parent = parentNode->as<ArrowFunction>();
	auto* parameter =
		parameters->nodes[0]->as<ParameterDeclaration>();

	return parameter->pos() == parent->pos() &&
	       parent->TypeParameters == nullptr && parent->Type == nullptr &&
	       (parent->modifiers == nullptr ||
	        parent->modifiers->nodes.empty()) &&
	       !parameters->hasTrailingComma() &&
	       parameter->modifiers == nullptr &&
	       parameter->DotDotDotToken == nullptr &&
	       parameter->QuestionToken == nullptr && parameter->Type == nullptr &&
	       parameter->Initializer == nullptr && isIdentifier(parameter->name);
}

bool Printer::canEmitSimpleArrowHead(Node* parentNode, NodeList* parameters) {
	return canEmitSimpleArrowHeadFn(parentNode, parameters);
}

void Printer::emitParametersForArrow(Node* parentNode, NodeList* parameters) {
	if (canEmitSimpleArrowHeadFn(parentNode, parameters)) {
		generateAllNames(parameters);
		emitList(&Printer::emitParameterDeclarationNode, parentNode,
		         parameters, LFSingleArrowParameter);
	} else {
		emitParameters(parentNode, parameters);
	}
}

void Printer::emitParametersForIndexSignature(Node* parentNode,
                                              NodeList* parameters) {
	generateAllNames(parameters);
	emitList(&Printer::emitParameterDeclarationNode, parentNode, parameters,
	         LFIndexSignatureParameters);
}

void Printer::emitSignature(Node* node) {
	auto n = node->functionLikeData();

	emitTypeParameters(node, *n.typeParameters);
	emitParameters(node, *n.parameters);
	emitTypeAnnotation(*n.type);
}

void Printer::emitFunctionBody(Node* body) {
	emitContext->addEmitFlags(body, EFNoSourceMap);

	// Use only notification hooks for the body block, not the full comment
	// pipeline.
	if (OnBeforeEmitNode) {
		OnBeforeEmitNode(body);
	}

	generateNames(body);

	writePunctuation("{");

	increaseIndent();
	auto* bodyBlock = body->as<Block>();
	commentState* detachedState =
		emitDetachedCommentsBeforeStatementList(body, bodyBlock->Statements->loc);
	int statementOffset = emitPrologueDirectives(bodyBlock->Statements);
	TextPos pos = writer->GetTextPos();
	emitHelpers(body);

	if (shouldEmitBlockFunctionBodyOnSingleLine(body) && statementOffset == 0 &&
	    pos == writer->GetTextPos()) {
		decreaseIndent();
		emitListRange(&Printer::emitStatement, body, bodyBlock->Statements,
		              LFSingleLineFunctionBodyStatements, statementOffset, -1);
		increaseIndent();
	} else {
		emitListRange(&Printer::emitStatement, body, bodyBlock->Statements,
		              LFMultiLineFunctionBodyStatements, statementOffset, -1);
	}

	emitDetachedCommentsAfterStatementList(body, bodyBlock->Statements->loc,
	                                     detachedState);
	decreaseIndent();

	emitTokenEx(Kind::CloseBraceToken, bodyBlock->Statements->end(),
	            WriteKind::Punctuation, body, tefNoComments);

	if (OnAfterEmitNode) {
		OnAfterEmitNode(body);
	}
}

void Printer::emitFunctionBodyNode(Node* node) {
	if (node == nullptr) {
		writeTrailingSemicolon();
		return;
	}

	writeSpace();
	emitFunctionBody(node);
}

//
// Type Members — printer.go:1653
//

void Printer::emitPropertySignature(Node* node) {
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	emitPropertyName(node->as<PropertySignatureDeclaration>()->name);
	emitTokenNode(node->as<PropertySignatureDeclaration>()->PostfixToken);
	emitTypeAnnotation(node->as<PropertySignatureDeclaration>()->Type);
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitPropertyDeclaration(Node* node) {
	auto* n = node->as<PropertyDeclaration>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ true);
	emitPropertyName(n->name);
	emitTokenNode(n->PostfixToken);
	emitTypeAnnotation(n->Type);
	emitInitializer(n->Initializer,
	                greatestEnd(n->name->end(), n->Type, n->PostfixToken),
	                node);
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitMethodSignature(Node* node) {
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	emitPropertyName(node->as<MethodSignatureDeclaration>()->name);
	emitTokenNode(node->as<MethodSignatureDeclaration>()->PostfixToken);
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	emitSignature(node);
	writeTrailingSemicolon();
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitMethodDeclaration(Node* node) {
	auto* n = node->as<MethodDeclaration>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ true);
	emitTokenNode(n->AsteriskToken);
	emitPropertyName(n->name);
	emitTokenNode(n->PostfixToken);
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	emitSignature(node);
	emitFunctionBodyNode(n->Body);
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitClassStaticBlockDeclaration(Node* node) {
	printerState state = enterNode(node);
	writeKeyword("static");
	pushNameGenerationScope(node);
	emitFunctionBodyNode(node->as<ClassStaticBlockDeclaration>()->Body);
	popNameGenerationScope(node);
	exitNode(node, state);
}

void Printer::emitConstructor(Node* node) {
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	writeKeyword("constructor");
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	emitSignature(node);
	emitFunctionBodyNode(node->as<ConstructorDeclaration>()->Body);
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitAccessorDeclaration(Kind token, Node* node) {
	auto* n = node->as<GetAccessorDeclaration>();
	printerState state = enterNode(node);
	TextPos pos =
		emitModifierList(node, node->modifiers(), /*allowDecorators*/ true);
	emitToken(token, pos, WriteKind::Keyword, node);
	writeSpace();
	emitPropertyName(n->name);
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	emitSignature(node);
	emitFunctionBodyNode(n->Body);
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitGetAccessorDeclaration(Node* node) {
	emitAccessorDeclaration(Kind::GetKeyword, node);
}

void Printer::emitSetAccessorDeclaration(Node* node) {
	emitAccessorDeclaration(Kind::SetKeyword, node);
}

void Printer::emitCallSignature(Node* node) {
	printerState state = enterNode(node);
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	emitSignature(node);
	writeTrailingSemicolon();
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitConstructSignature(Node* node) {
	printerState state = enterNode(node);
	writeKeyword("new");
	writeSpace();
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	emitSignature(node);
	writeTrailingSemicolon();
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitIndexSignature(Node* node) {
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	emitParametersForIndexSignature(
		node, node->as<IndexSignatureDeclaration>()->Parameters);
	emitTypeAnnotation(node->as<IndexSignatureDeclaration>()->Type);
	writeTrailingSemicolon();
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitClassElement(Node* node) {
	switch (node->kind) {
	case Kind::PropertyDeclaration:
		emitPropertyDeclaration(node);
		break;
	case Kind::MethodDeclaration:
		emitMethodDeclaration(node);
		break;
	case Kind::ClassStaticBlockDeclaration:
		emitClassStaticBlockDeclaration(node);
		break;
	case Kind::Constructor:
		emitConstructor(node);
		break;
	case Kind::GetAccessor:
		emitGetAccessorDeclaration(node);
		break;
	case Kind::SetAccessor:
		emitSetAccessorDeclaration(node);
		break;
	case Kind::IndexSignature:
		emitIndexSignature(node);
		break;
	case Kind::SemicolonClassElement:
		emitSemicolonClassElement(node);
		break;
	case Kind::NotEmittedStatement:
		emitNotEmittedStatement(node);
		break;
	case Kind::JSTypeAliasDeclaration:
		emitTypeAliasDeclaration(node);
		break;
	default:
		printerPanic("unexpected ClassElement: ", kindName(node->kind));
	}
}

void Printer::emitTypeElement(Node* node) {
	switch (node->kind) {
	case Kind::PropertySignature:
		emitPropertySignature(node);
		break;
	case Kind::MethodSignature:
		emitMethodSignature(node);
		break;
	case Kind::CallSignature:
		emitCallSignature(node);
		break;
	case Kind::ConstructSignature:
		emitConstructSignature(node);
		break;
	case Kind::GetAccessor:
		emitGetAccessorDeclaration(node);
		break;
	case Kind::SetAccessor:
		emitSetAccessorDeclaration(node);
		break;
	case Kind::IndexSignature:
		emitIndexSignature(node);
		break;
	case Kind::NotEmittedTypeElement:
		emitNotEmittedTypeElement(node);
		break;
	default:
		printerPanic("unexpected TypeElement: ", kindName(node->kind));
	}
}

void Printer::emitObjectLiteralElement(Node* node) {
	switch (node->kind) {
	case Kind::PropertyAssignment:
		emitPropertyAssignment(node);
		break;
	case Kind::ShorthandPropertyAssignment:
		emitShorthandPropertyAssignment(node);
		break;
	case Kind::SpreadAssignment:
		emitSpreadAssignment(node);
		break;
	case Kind::MethodDeclaration:
		emitMethodDeclaration(node);
		break;
	case Kind::GetAccessor:
		emitGetAccessorDeclaration(node);
		break;
	case Kind::SetAccessor:
		emitSetAccessorDeclaration(node);
		break;
	default:
		printerPanic("unhandled ObjectLiteralElement: ", kindName(node->kind));
	}
}

//
// Types — printer.go:1865
//

void Printer::emitKeywordTypeNode(Node* node) { emitKeywordNode(node); }

void Printer::emitTypePredicateParameterName(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
		emitIdentifierReference(node);
		break;
	case Kind::ThisType:
		emitThisType(node);
		break;
	default:
		printerPanic("unexpected TypePredicateParameterName: ",
		             kindName(node->kind));
	}
}

void Printer::emitTypePredicate(Node* node) {
	auto* n = node->as<TypePredicateNode>();
	printerState state = enterNode(node);
	if (n->AssertsModifier != nullptr) {
		emitTokenNode(n->AssertsModifier);
		writeSpace();
	}
	emitTypePredicateParameterName(n->ParameterName);
	if (n->Type != nullptr) {
		writeSpace();
		writeKeyword("is");
		writeSpace();
		emitTypeNodeOutsideExtends(n->Type);
	}
	exitNode(node, state);
}

void Printer::emitTypeArgument(Node* node) {
	emitTypeNodeOutsideExtends(node);
}

void Printer::emitTypeArguments(Node* parentNode, NodeList* nodes) {
	if (nodes == nullptr) {
		return;
	}
	emitList(&Printer::emitTypeParameterDeclarationNode, parentNode, nodes,
	         LFTypeArguments);
}

void Printer::emitTypeReference(Node* node) {
	auto* n = node->as<TypeReferenceNode>();
	printerState state = enterNode(node);
	emitEntityName(n->TypeName);
	emitTypeArguments(node, n->TypeArguments);
	exitNode(node, state);
}

// emitReturnType — printer.go:1919. Emits the return type of a
// FunctionTypeNode or ConstructorTypeNode, including the arrow (`=>`).
void Printer::emitReturnType(Node* node) {
	if (node == nullptr) {
		return;
	}
	writePunctuation("=>");
	writeSpace();
	if (inExtends && node->kind == Kind::InferType &&
	    node->as<InferTypeNode>()
	            ->TypeParameter->as<TypeParameterDeclaration>()
	            ->Constraint != nullptr) {
		// if the parent FunctionTypeNode or ConstructorTypeNode is in the
		// `extends` clause of a ConditionalTypeNode, we must parenthesize
		// `infer ... extends ...` so as not to result in an ambiguous parse.
		emitTypeNodePreservingExtends(node, TypePrecedenceHighest);
	} else {
		emitTypeNodePreservingExtends(node, TypePrecedenceLowest);
	}
}

void Printer::emitFunctionType(Node* node) {
	auto* n = node->as<FunctionTypeNode>();
	printerState state = enterNode(node);
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	emitTypeParameters(node, n->TypeParameters);
	emitParameters(node, n->Parameters);
	writeSpace();
	emitReturnType(n->Type);
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitConstructorType(Node* node) {
	auto* n = node->as<ConstructorTypeNode>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	writeKeyword("new");
	writeSpace();
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	emitTypeParameters(node, n->TypeParameters);
	emitParameters(node, n->Parameters);
	writeSpace();
	emitReturnType(n->Type);
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitTypeQuery(Node* node) {
	auto* n = node->as<TypeQueryNode>();
	printerState state = enterNode(node);
	writeKeyword("typeof");
	writeSpace();
	emitEntityName(n->ExprName);
	emitTypeArguments(node, n->TypeArguments);
	exitNode(node, state);
}

void Printer::emitTypeLiteral(Node* node) {
	auto* n = node->as<TypeLiteralNode>();
	printerState state = enterNode(node);
	pushNameGenerationScope(node);
	generateAllMemberNames(n->Members);
	writePunctuation("{");
	ListFormat flags = shouldEmitOnSingleLine(node)
	                       ? LFSingleLineTypeLiteralMembers
	                       : LFMultiLineTypeLiteralMembers;
	emitList(&Printer::emitTypeElement, node, n->Members,
	         flags | LFNoSpaceIfEmpty);
	writePunctuation("}");
	popNameGenerationScope(node);
	exitNode(node, state);
}

void Printer::emitArrayType(Node* node) {
	printerState state = enterNode(node);
	emitPostfixTypeOperand(node->as<ArrayTypeNode>()->ElementType, node);
	writePunctuation("[");
	writePunctuation("]");
	exitNode(node, state);
}

// emitPostfixTypeOperand — printer.go:1999. Emits the operand of a postfix
// type (ArrayType, IndexedAccessType, OptionalType). Equivalent to
// `emitTypeNode(operand, TypePrecedencePostfix)` except that it preserves a
// parsed `typeof X` operand without adding parentheses.
void Printer::emitPostfixTypeOperand(Node* operand, Node* parent) {
	if (isParseTreeNode(parent) && operand->kind == Kind::TypeQuery) {
		emitTypeNode(operand, TypePrecedenceTypeOperator);
		return;
	}
	emitTypeNode(operand, TypePrecedencePostfix);
}

void Printer::emitTupleElementType(Node* node) {
	emitTypeNodeOutsideExtends(node);
}

void Printer::emitTupleType(Node* node) {
	auto* n = node->as<TupleTypeNode>();
	printerState state = enterNode(node);
	emitToken(Kind::OpenBracketToken, node->pos(), WriteKind::Punctuation,
	          node);
	ListFormat flags = shouldEmitOnSingleLine(node)
	                       ? LFSingleLineTupleTypeElements
	                       : LFMultiLineTupleTypeElements;
	emitList(&Printer::emitTupleElementType, node, n->Elements,
	         flags | LFNoSpaceIfEmpty);
	emitToken(Kind::CloseBracketToken, n->Elements->end(),
	          WriteKind::Punctuation, node);
	exitNode(node, state);
}

void Printer::emitRestType(Node* node) {
	printerState state = enterNode(node);
	writePunctuation("...");
	emitTypeNodeOutsideExtends(node->as<RestTypeNode>()->Type);
	exitNode(node, state);
}

void Printer::emitOptionalType(Node* node) {
	printerState state = enterNode(node);
	// !!! May need extra parenthesization if we also have JSDocNullableType
	emitPostfixTypeOperand(node->as<OptionalTypeNode>()->Type, node);
	writePunctuation("?");
	exitNode(node, state);
}

void Printer::emitNamedTupleMember(Node* node) {
	auto* n = node->as<NamedTupleMember>();
	printerState state = enterNode(node);
	emitPunctuationNode(n->DotDotDotToken);
	emitIdentifierName(n->name);
	emitPunctuationNode(n->QuestionToken);
	emitToken(Kind::ColonToken, greatestEnd(n->name->end(), n->QuestionToken),
	          WriteKind::Punctuation, node);
	writeSpace();
	emitTypeNodeOutsideExtends(n->Type);
	exitNode(node, state);
}

void Printer::emitUnionTypeConstituent(Node* node) {
	emitTypeNode(node, TypePrecedenceTypeOperator);
}

void Printer::emitUnionType(Node* node) {
	printerState state = enterNode(node);
	emitList(&Printer::emitUnionTypeConstituent, node,
	         node->as<UnionTypeNode>()->Types, LFUnionTypeConstituents);
	exitNode(node, state);
}

void Printer::emitIntersectionTypeConstituent(Node* node) {
	emitTypeNode(node, TypePrecedenceTypeOperator);
}

void Printer::emitIntersectionType(Node* node) {
	printerState state = enterNode(node);
	emitList(&Printer::emitIntersectionTypeConstituent, node,
	         node->as<IntersectionTypeNode>()->Types,
	         LFIntersectionTypeConstituents);
	exitNode(node, state);
}

void Printer::emitConditionalType(Node* node) {
	auto* n = node->as<ConditionalTypeNode>();
	printerState state = enterNode(node);
	emitTypeNode(n->CheckType, TypePrecedenceUnion);
	writeSpace();
	writeKeyword("extends");
	writeSpace();
	emitTypeNodeInExtends(n->ExtendsType);
	writeSpace();
	writePunctuation("?");
	writeSpace();
	emitTypeNodeOutsideExtends(n->TrueType);
	writeSpace();
	writePunctuation(":");
	writeSpace();
	emitTypeNodeOutsideExtends(n->FalseType);
	exitNode(node, state);
}

void Printer::emitInferTypeParameter(Node* node) {
	auto* n = node->as<TypeParameterDeclaration>();
	printerState state = enterNode(node);
	emitBindingIdentifier(n->name);
	if (n->Constraint != nullptr) {
		writeSpace();
		writeKeyword("extends");
		writeSpace();
		emitTypeNodeInExtends(n->Constraint);
	}
	exitNode(node, state);
}

void Printer::emitInferType(Node* node) {
	printerState state = enterNode(node);
	writeKeyword("infer");
	writeSpace();
	emitInferTypeParameter(node->as<InferTypeNode>()->TypeParameter);
	exitNode(node, state);
}

void Printer::emitParenthesizedType(Node* node) {
	printerState state = enterNode(node);
	writePunctuation("(");
	emitTypeNodeOutsideExtends(node->as<ParenthesizedTypeNode>()->Type);
	writePunctuation(")");
	exitNode(node, state);
}

void Printer::emitThisType(Node* node) {
	printerState state = enterNode(node);
	writeKeyword("this");
	exitNode(node, state);
}

void Printer::emitTypeOperator(Node* node) {
	auto* n = node->as<TypeOperatorNode>();
	printerState state = enterNode(node);
	emitToken(n->Operator, node->pos(), WriteKind::Keyword, node);
	writeSpace();
	emitTypeNode(n->Type,
	             n->Operator == Kind::ReadonlyKeyword
	                 ? TypePrecedencePostfix
	                 : TypePrecedenceTypeOperator);
	exitNode(node, state);
}

void Printer::emitIndexedAccessType(Node* node) {
	auto* n = node->as<IndexedAccessTypeNode>();
	printerState state = enterNode(node);
	emitPostfixTypeOperand(n->ObjectType, node);
	writePunctuation("[");
	emitTypeNodeOutsideExtends(n->IndexType);
	writePunctuation("]");
	exitNode(node, state);
}

void Printer::emitMappedTypeParameter(Node* node) {
	auto* n = node->as<TypeParameterDeclaration>();
	printerState state = enterNode(node);
	emitBindingIdentifier(n->name);
	writeSpace();
	writeKeyword("in");
	writeSpace();
	emitTypeNodeOutsideExtends(n->Constraint);
	exitNode(node, state);
}

void Printer::emitMappedType(Node* node) {
	auto* n = node->as<MappedTypeNode>();
	printerState state = enterNode(node);
	bool singleLine = shouldEmitOnSingleLine(node);
	writePunctuation("{");
	if (singleLine) {
		writeSpace();
	} else {
		writeLine();
		increaseIndent();
	}
	if (n->ReadonlyToken != nullptr) {
		emitTokenNode(n->ReadonlyToken);
		if (n->ReadonlyToken->kind != Kind::ReadonlyKeyword) {
			writeKeyword("readonly");
		}
		writeSpace();
	}
	writePunctuation("[");
	emitMappedTypeParameter(n->TypeParameter);
	if (n->NameType != nullptr) {
		writeSpace();
		writeKeyword("as");
		writeSpace();
		emitTypeNodeOutsideExtends(n->NameType);
	}
	writePunctuation("]");
	if (n->QuestionToken != nullptr) {
		emitPunctuationNode(n->QuestionToken);
		if (n->QuestionToken->kind != Kind::QuestionToken) {
			writePunctuation("?");
		}
	}
	if (n->Type != nullptr) {
		writePunctuation(":");
		writeSpace();
		emitTypeNodeOutsideExtends(n->Type);
	}
	writeTrailingSemicolon();
	if (n->Members != nullptr) {
		if (!n->Members->nodes.empty()) {
			if (singleLine) {
				writeSpace();
			} else {
				writeLine();
			}
			emitList(&Printer::emitTypeElement, node, n->Members,
			         LFPreserveLines);
		}
	}
	if (singleLine) {
		writeSpace();
	} else {
		writeLine();
		decreaseIndent();
	}
	writePunctuation("}");
	exitNode(node, state);
}

void Printer::emitLiteralType(Node* node) {
	printerState state = enterNode(node);
	emitExpression(node->as<LiteralTypeNode>()->Literal,
	               OperatorPrecedenceComma);
	exitNode(node, state);
}

void Printer::emitTemplateTypeSpan(Node* node) {
	auto* n = node->as<TemplateLiteralTypeSpan>();
	printerState state = enterNode(node);
	emitTypeNodeOutsideExtends(n->Type);
	emitTemplateMiddleTail(n->Literal);
	exitNode(node, state);
}

void Printer::emitTemplateTypeSpanNode(Node* node) {
	emitTemplateTypeSpan(node);
}

void Printer::emitTemplateType(Node* node) {
	auto* n = node->as<TemplateLiteralTypeNode>();
	printerState state = enterNode(node);
	emitTemplateHead(n->Head);
	emitList(&Printer::emitTemplateTypeSpanNode, node, n->TemplateSpans,
	         LFTemplateExpressionSpans);
	exitNode(node, state);
}

void Printer::emitImportTypeNodeAttributes(Node* node) {
	auto* n = node->as<ImportAttributes>();
	printerState state = enterNode(node);
	writePunctuation("{");
	writeSpace();
	writeKeyword(n->Token == Kind::AssertKeyword ? "assert" : "with");
	writePunctuation(":");
	writeSpace();
	emitList(&Printer::emitImportAttributeNode, node, n->Attributes,
	         LFImportAttributes);
	writeSpace();
	writePunctuation("}");
	exitNode(node, state);
}

void Printer::emitImportTypeNode(Node* node) {
	auto* n = node->as<ImportTypeNode>();
	printerState state = enterNode(node);
	if (n->IsTypeOf) {
		writeKeyword("typeof");
		writeSpace();
	}
	writeKeyword("import");
	writePunctuation("(");
	emitTypeNodeOutsideExtends(n->Argument);
	if (n->Attributes != nullptr) {
		writePunctuation(",");
		writeSpace();
		emitImportTypeNodeAttributes(n->Attributes);
	}
	writePunctuation(")");
	if (n->Qualifier != nullptr) {
		writePunctuation(".");
		emitEntityName(n->Qualifier);
	}
	emitTypeArguments(node, n->TypeArguments);
	exitNode(node, state);
}

// emitTypeNodeInExtends — printer.go:2271: emits a Type node in the `extends`
// clause of a ConditionalType.
void Printer::emitTypeNodeInExtends(Node* node) {
	bool savedInExtends = inExtends;
	inExtends = true;
	emitTypeNodePreservingExtends(node, TypePrecedenceLowest);
	inExtends = savedInExtends;
}

// emitTypeNodeOutsideExtends — printer.go:2279: emits a Type node not in the
// `extends` clause of a ConditionalType or InferType.
void Printer::emitTypeNodeOutsideExtends(Node* node) {
	bool savedInExtends = inExtends;
	inExtends = false;
	emitTypeNodePreservingExtends(node, TypePrecedenceLowest);
	inExtends = savedInExtends;
}

// emitTypeNodePreservingExtends — printer.go:2287.
void Printer::emitTypeNodePreservingExtends(Node* node,
                                            TypePrecedence precedence) {
	emitTypeNode(node, precedence);
}

void Printer::emitTypeNode(Node* node, TypePrecedence precedence) {
	if (inExtends && precedence <= TypePrecedenceConditional) {
		// in the `extends` clause of a ConditionalType or InferType, a
		// ConditionalType must be parenthesized
		precedence = TypePrecedenceFunction;
	}

	bool savedInExtends = inExtends;
	bool parens = getTypeNodePrecedence(node) < precedence;
	if (parens) {
		inExtends = false;
		writePunctuation("(");
	}

	switch (node->kind) {
	// Keyword Types
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
		emitKeywordTypeNode(node);
		break;

	// Types
	case Kind::TypePredicate:
		emitTypePredicate(node);
		break;
	case Kind::TypeReference:
		emitTypeReference(node);
		break;
	case Kind::FunctionType:
		emitFunctionType(node);
		break;
	case Kind::ConstructorType:
		emitConstructorType(node);
		break;
	case Kind::TypeQuery:
		emitTypeQuery(node);
		break;
	case Kind::TypeLiteral:
		emitTypeLiteral(node);
		break;
	case Kind::ArrayType:
		emitArrayType(node);
		break;
	case Kind::TupleType:
		emitTupleType(node);
		break;
	case Kind::OptionalType:
		emitOptionalType(node);
		break;
	case Kind::RestType:
		emitRestType(node);
		break;
	case Kind::UnionType:
		emitUnionType(node);
		break;
	case Kind::IntersectionType:
		emitIntersectionType(node);
		break;
	case Kind::ConditionalType:
		emitConditionalType(node);
		break;
	case Kind::InferType:
		emitInferType(node);
		break;
	case Kind::ParenthesizedType:
		emitParenthesizedType(node);
		break;
	case Kind::ThisType:
		emitThisType(node);
		break;
	case Kind::TypeOperator:
		emitTypeOperator(node);
		break;
	case Kind::IndexedAccessType:
		emitIndexedAccessType(node);
		break;
	case Kind::MappedType:
		emitMappedType(node);
		break;
	case Kind::LiteralType:
		emitLiteralType(node);
		break;
	case Kind::NamedTupleMember:
		emitNamedTupleMember(node);
		break;
	case Kind::TemplateLiteralType:
		emitTemplateType(node);
		break;
	case Kind::TemplateLiteralTypeSpan:
		emitTemplateTypeSpan(node);
		break;
	case Kind::ImportType:
		emitImportTypeNode(node);
		break;

	case Kind::PropertyAccessExpression:
		// Occurs in pseudo-types such as `f<T>.C`, where `f` is a generic
		// function and `C` is a local type
		emitPropertyAccessExpression(node);
		break;
	case Kind::ExpressionWithTypeArguments:
		// !!! Should this actually be considered a type?
		emitExpressionWithTypeArguments(node);
		break;

	case Kind::JSDocAllType:
		emitJSDocAllType(node);
		break;
	case Kind::JSDocNonNullableType:
		emitJSDocNonNullableType(node);
		break;
	case Kind::JSDocNullableType:
		emitJSDocNullableType(node);
		break;
	case Kind::JSDocOptionalType:
		emitJSDocOptionalType(node);
		break;
	case Kind::JSDocVariadicType:
		emitJSDocVariadicType(node);
		break;

	default:
		printerPanic("unhandled TypeNode: ", kindName(node->kind));
	}

	if (parens) {
		writePunctuation(")");
	}

	inExtends = savedInExtends;
}

//
// Binding patterns — printer.go:2401
//

void Printer::emitObjectBindingPattern(Node* node) {
	auto* n = node->as<BindingPattern>();
	printerState state = enterNode(node);
	writePunctuation("{");
	emitList(&Printer::emitBindingElementNode, node, n->Elements,
	         LFObjectBindingPatternElements);
	writePunctuation("}");
	exitNode(node, state);
}

void Printer::emitArrayBindingPattern(Node* node) {
	auto* n = node->as<BindingPattern>();
	printerState state = enterNode(node);
	writePunctuation("[");
	emitList(&Printer::emitBindingElementNode, node, n->Elements,
	         LFArrayBindingPatternElements);
	writePunctuation("]");
	exitNode(node, state);
}

void Printer::emitBindingElementNode(Node* node) {
	emitBindingElement(node);
}

void Printer::emitBindingElement(Node* node) {
	auto* n = node->as<BindingElement>();
	printerState state = enterNode(node);
	emitTokenNode(n->DotDotDotToken);
	if (n->PropertyName != nullptr) {
		emitPropertyName(n->PropertyName);
		writePunctuation(":");
		writeSpace();
	}
	// Old parser used `OmittedExpression` as a substitute for `Elision`. New
	// parser uses a `BindingElement` with nil members
	if (n->name != nullptr) {
		emitBindingName(n->name);
		emitInitializer(n->Initializer, n->name->end(), node);
	}
	exitNode(node, state);
}

//
// JSDoc types — printer.go:2439
//

void Printer::emitJSDocAllType(Node* node) { emitKeywordNode(node); }

void Printer::emitJSDocNonNullableType(Node* node) {
	printerState state = enterNode(node);
	writePunctuation("!");
	emitTypeNode(node->as<JSDocNonNullableType>()->Type,
	             TypePrecedenceNonArray);
	exitNode(node, state);
}

void Printer::emitJSDocNullableType(Node* node) {
	printerState state = enterNode(node);
	writePunctuation("?");
	emitTypeNode(node->as<JSDocNullableType>()->Type, TypePrecedenceNonArray);
	exitNode(node, state);
}

void Printer::emitJSDocOptionalType(Node* node) {
	printerState state = enterNode(node);
	emitTypeNode(node->as<JSDocOptionalType>()->Type, TypePrecedenceJSDoc);
	writePunctuation("=");
	exitNode(node, state);
}

void Printer::emitJSDocVariadicType(Node* node) {
	printerState state = enterNode(node);
	writePunctuation("...");
	emitTypeNode(node->as<JSDocVariadicType>()->Type, TypePrecedenceJSDoc);
	exitNode(node, state);
}

//
// Expressions — printer.go:2471
//

void Printer::emitKeywordExpression(Node* node) { emitKeywordNode(node); }

void Printer::emitArrayLiteralExpressionElement(Node* node) {
	emitExpression(node, OperatorPrecedenceSpread);
}

void Printer::emitArrayLiteralExpression(Node* node) {
	auto* n = node->as<ArrayLiteralExpression>();
	printerState state = enterNode(node);
	emitList(&Printer::emitArrayLiteralExpressionElement, node, n->Elements,
	         LFArrayLiteralExpressionElements |
	             (n->MultiLine ? LFPreferNewLine : LFNone));
	exitNode(node, state);
}

void Printer::emitObjectLiteralExpression(Node* node) {
	auto* n = node->as<ObjectLiteralExpression>();
	printerState state = enterNode(node);
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	generateAllMemberNames(n->Properties);
	emitList(&Printer::emitObjectLiteralElement, node, n->Properties,
	         LFObjectLiteralExpressionProperties |
	             (n->MultiLine ? LFPreferNewLine : LFNone) |
	             (shouldAllowTrailingComma(node, n->Properties)
	                  ? LFAllowTrailingComma
	                  : LFNone));
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

// 1..toString is a valid property access, emit a dot after the literal
// Also emit a dot if expression is a integer const enum value - it will appear
// in generated code as numeric literal
bool Printer::mayNeedDotDotForPropertyAccess(Node* expression) {
	expression = skipPartiallyEmittedExpressions(expression);
	if (isNumericLiteral(expression)) {
		// check if numeric literal is a decimal literal that was originally
		// written with a dot
		std::string text = getLiteralTextOfNode(
		    expression, /*sourceFile*/ nullptr, getLiteralTextFlagsNeverAsciiEscape);
		// If the number will be printed verbatim and it doesn't already
		// contain a dot or an exponent indicator, add one if the expression
		// doesn't have any comments that will be emitted.
		auto* lit = expression->as<NumericLiteral>();
		return (lit->TokenFlags & TokenFlagsWithSpecifier) == 0 &&
		       text.find(tokenToString(Kind::DotToken)) ==
		           std::string::npos &&
		       text.find('E') == std::string::npos &&
		       text.find('e') == std::string::npos;
	}
	return false;
}

void Printer::emitPropertyAccessExpression(Node* node) {
	auto* n = node->as<PropertyAccessExpression>();
	printerState state = enterNode(node);
	emitExpression(n->Expression,
	               isOptionalChain(node) ? OperatorPrecedenceOptionalChain
	                                     : OperatorPrecedenceMember);
	Node* token = n->QuestionDotToken;
	if (token == nullptr) {
		token = emitContext->factory.asNodeFactory()->newToken(
		    Kind::DotToken);
		token->loc =
		    newTextRange(n->Expression->end(), n->name->pos());
		emitContext->addEmitFlags(token, EFNoSourceMap);
	}
	int linesBeforeDot = getLinesBetweenNodes(node, n->Expression, token);
	writeLineRepeat(linesBeforeDot);
	increaseIndentIf(linesBeforeDot > 0);
	bool shouldEmitDotDot = token->kind != Kind::QuestionDotToken &&
	                      mayNeedDotDotForPropertyAccess(n->Expression) &&
	                      !writer->HasTrailingComment() &&
	                      !writer->HasTrailingWhitespace();
	if (shouldEmitDotDot) {
		writePunctuation(".");
	}
	if (n->QuestionDotToken != nullptr) {
		emitTokenNode(token);
	} else {
		emitToken(Kind::DotToken, n->Expression->end(),
		          WriteKind::Punctuation, node);
	}
	int linesAfterDot = getLinesBetweenNodes(node, token, n->name);
	writeLineRepeat(linesAfterDot);
	increaseIndentIf(linesAfterDot > 0);
	emitMemberName(n->name);
	decreaseIndentIf(linesAfterDot > 0);
	decreaseIndentIf(linesBeforeDot > 0);
	exitNode(node, state);
}

void Printer::emitElementAccessExpression(Node* node) {
	auto* n = node->as<ElementAccessExpression>();
	printerState state = enterNode(node);
	emitExpression(n->Expression,
	               isOptionalChain(node) ? OperatorPrecedenceOptionalChain
	                                     : OperatorPrecedenceMember);
	emitTokenNode(n->QuestionDotToken);
	emitToken(Kind::OpenBracketToken,
	          greatestEnd(-1, n->Expression, n->QuestionDotToken),
	          WriteKind::Punctuation, node);
	emitExpression(n->ArgumentExpression, OperatorPrecedenceComma);
	emitToken(Kind::CloseBracketToken, n->ArgumentExpression->end(),
	          WriteKind::Punctuation, node);
	exitNode(node, state);
}

void Printer::emitArgument(Node* node) {
	emitExpression(node, OperatorPrecedenceSpread);
}

void Printer::emitCallee(Node* callee, Node* parentNode) {
	if (shouldEmitIndirectCall(parentNode)) {
		writePunctuation("(");
		writeLiteral("0");
		writePunctuation(",");
		writeSpace();
		emitExpression(callee, OperatorPrecedenceComma);
		writePunctuation(")");
	} else if (parentNode->kind == Kind::CallExpression &&
	           isNewExpressionWithoutArguments(
	               skipPartiallyEmittedExpressions(callee))) {
		// Parenthesize `new C` inside of a CallExpression so it is treated as
		// `(new C)()` and not `new C()`
		emitExpression(callee, OperatorPrecedenceParentheses);
	} else {
		emitExpression(callee,
		               isOptionalChain(parentNode)
		                   ? OperatorPrecedenceOptionalChain
		                   : OperatorPrecedenceMember);
	}
}

void Printer::emitCallExpression(Node* node) {
	auto* n = node->as<CallExpression>();
	printerState state = enterNode(node);
	emitCallee(n->Expression, node);
	emitTokenNode(n->QuestionDotToken);
	emitTypeArguments(node, n->TypeArguments);
	emitList(&Printer::emitArgument, node, n->Arguments,
	         LFCallExpressionArguments);
	exitNode(node, state);
}

void Printer::emitNewExpression(Node* node) {
	auto* n = node->as<NewExpression>();
	printerState state = enterNode(node);
	emitToken(Kind::NewKeyword, node->pos(), WriteKind::Keyword, node);
	writeSpace();
	if (skipPartiallyEmittedExpressions(n->Expression)->kind ==
	    Kind::CallExpression) {
		// Parenthesize `C()` inside of a NewExpression so it is treated as
		// `new (C())` and not `new C()`
		emitExpression(n->Expression, OperatorPrecedenceParentheses);
	} else {
		emitExpression(n->Expression, OperatorPrecedenceMember);
	}
	emitTypeArguments(node, n->TypeArguments);
	emitList(&Printer::emitArgument, node, n->Arguments,
	         LFNewExpressionArguments);
	exitNode(node, state);
}

void Printer::emitTemplateLiteral(Node* node) {
	switch (node->kind) {
	case Kind::NoSubstitutionTemplateLiteral:
		emitNoSubstitutionTemplateLiteral(node);
		break;
	case Kind::TemplateExpression:
		emitTemplateExpression(node);
		break;
	default:
		printerPanic("unhandled TemplateLiteral: ", kindName(node->kind));
	}
}

void Printer::emitTaggedTemplateExpression(Node* node) {
	auto* n = node->as<TaggedTemplateExpression>();
	printerState state = enterNode(node);
	emitCallee(n->Tag, node);
	emitTypeArguments(node, n->TypeArguments);
	writeSpace();
	emitTemplateLiteral(n->Template);
	exitNode(node, state);
}

void Printer::emitTypeAssertionExpression(Node* node) {
	auto* n = node->as<TypeAssertion>();
	printerState state = enterNode(node);
	writePunctuation("<");
	emitTypeNodeOutsideExtends(n->Type);
	writePunctuation(">");
	emitExpression(n->Expression, OperatorPrecedenceUpdate);
	exitNode(node, state);
}

void Printer::emitParenthesizedExpression(Node* node) {
	auto* n = node->as<ParenthesizedExpression>();
	printerState state = enterNode(node);
	TextPos openParenPos = emitToken(Kind::OpenParenToken, node->pos(),
	                               WriteKind::Punctuation, node);
	bool indented =
	    writeLineSeparatorsAndIndentBefore(n->Expression, node);
	emitExpression(n->Expression, OperatorPrecedenceComma);
	writeLineSeparatorsAfter(n->Expression, node);
	decreaseIndentIf(indented);
	TextPos closeParenPos = openParenPos;
	if (n->Expression != nullptr) {
		closeParenPos = n->Expression->end();
	}
	emitToken(Kind::CloseParenToken, closeParenPos, WriteKind::Punctuation,
	          node);
	exitNode(node, state);
}

void Printer::emitFunctionExpression(Node* node) {
	auto* n = node->as<FunctionExpression>();
	printerState state = enterNode(node);
	generateNameIfNeeded(n->name);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	writeKeyword("function");
	emitTokenNode(n->AsteriskToken);
	writeSpace();
	emitIdentifierNameNode(n->name);
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	emitSignature(node);
	emitFunctionBodyNode(n->Body);
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitConciseBody(Node* node) {
	if (isBlock(node)) {
		emitFunctionBody(node);
	} else if (isObjectLiteralExpression(
	               getLeftmostExpression(node,
	                                     /*stopAtCallExpressions*/ false))) {
		// Wrap in ParenthesizedExpression to ensure parens are emitted after
		// any leading PartiallyEmittedExpression comments, matching
		// TypeScript's factory-time wrapping via
		// parenthesizeConciseBodyOfArrowFunction.
		Node* paren =
		    emitContext->factory.asNodeFactory()->newParenthesizedExpression(
		        node);
		paren->loc = node->loc;
		emitExpression(paren, OperatorPrecedenceLowest);
	} else if (isExpression(node)) {
		emitExpression(node, OperatorPrecedenceYield);
	} else {
		printerPanic("unexpected ConciseBody: ", kindName(node->kind));
	}
}

void Printer::emitArrowFunction(Node* node) {
	auto* n = node->as<ArrowFunction>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	emitTypeParameters(node, n->TypeParameters);
	emitParametersForArrow(node, n->Parameters);
	emitTypeAnnotation(n->Type);
	writeSpace();
	emitTokenNode(n->EqualsGreaterThanToken);
	writeSpace();
	emitConciseBody(n->Body);
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitDeleteExpression(Node* node) {
	printerState state = enterNode(node);
	emitToken(Kind::DeleteKeyword, node->pos(), WriteKind::Keyword, node);
	writeSpace();
	emitExpression(node->as<DeleteExpression>()->Expression,
	               OperatorPrecedenceUnary);
	exitNode(node, state);
}

void Printer::emitTypeOfExpression(Node* node) {
	printerState state = enterNode(node);
	emitToken(Kind::TypeOfKeyword, node->pos(), WriteKind::Keyword, node);
	writeSpace();
	emitExpression(node->as<TypeOfExpression>()->Expression,
	               OperatorPrecedenceUnary);
	exitNode(node, state);
}

void Printer::emitVoidExpression(Node* node) {
	printerState state = enterNode(node);
	emitToken(Kind::VoidKeyword, node->pos(), WriteKind::Keyword, node);
	writeSpace();
	emitExpression(node->as<VoidExpression>()->Expression,
	               OperatorPrecedenceUnary);
	exitNode(node, state);
}

void Printer::emitAwaitExpression(Node* node) {
	printerState state = enterNode(node);
	emitToken(Kind::AwaitKeyword, node->pos(), WriteKind::Keyword, node);
	writeSpace();
	emitExpression(node->as<AwaitExpression>()->Expression,
	               OperatorPrecedenceUnary);
	exitNode(node, state);
}

void Printer::emitPrefixUnaryExpression(Node* node) {
	auto* n = node->as<PrefixUnaryExpression>();
	printerState state = enterNode(node);
	Kind op = n->Operator;
	Node* operand = n->Operand;
	emitToken(op, node->pos(), WriteKind::Operator, node);

	// In some cases, we need to emit a space between the operator and the
	// operand. One obvious case is when the operator is an identifier, like
	// delete or typeof. We also need to do this for plus and minus expressions
	// in certain cases. Specifically, consider the following two cases (parens
	// are just for clarity of exposition, and not part of the source code):
	//
	//  (+(+1))
	//  (+(++1))
	//
	// We need to emit a space in both cases. In the first case, the absence of
	// a space will make the resulting expression a prefix increment operation.
	// And in the second, it will make the resulting expression a prefix
	// increment whose operand is a plus expression - (++(+x))
	// The same is true of minus of course.
	if (operand->kind == Kind::PrefixUnaryExpression) {
		Kind inner = operand->as<PrefixUnaryExpression>()->Operator;
		if ((op == Kind::PlusToken &&
		     (inner == Kind::PlusToken || inner == Kind::PlusPlusToken)) ||
		    (op == Kind::MinusToken &&
		     (inner == Kind::MinusToken || inner == Kind::MinusMinusToken))) {
			writeSpace();
		}
	}

	emitExpression(n->Operand, OperatorPrecedenceUnary);
	exitNode(node, state);
}

void Printer::emitPostfixUnaryExpression(Node* node) {
	auto* n = node->as<PostfixUnaryExpression>();
	printerState state = enterNode(node);
	emitExpression(n->Operand, OperatorPrecedenceLeftHandSide);
	emitToken(n->Operator, n->Operand->end(), WriteKind::Operator, node);
	exitNode(node, state);
}

// This function determines whether an expression consists of a homogeneous
// set of literal expressions or binary plus expressions that all share the
// same literal kind. It is used to determine whether the right-hand operand
// of a binary plus expression can be emitted without parentheses.
Kind Printer::getLiteralKindOfBinaryPlusOperand(Node* node) {
	node = skipPartiallyEmittedExpressions(node);

	if (isLiteralKind(node->kind)) {
		return node->kind;
	}

	if (node->kind == Kind::BinaryExpression) {
		auto* n = node->as<BinaryExpression>();
		if (n->OperatorToken->kind == Kind::PlusToken) {
			Kind leftKind = getLiteralKindOfBinaryPlusOperand(n->Left);
			Kind literalKind = Kind::Unknown;
			if (isLiteralKind(leftKind) &&
			    leftKind == getLiteralKindOfBinaryPlusOperand(n->Right)) {
				literalKind = leftKind;
			}
			return literalKind;
		}
	}

	return Kind::Unknown;
}

std::pair<OperatorPrecedence, OperatorPrecedence>
Printer::getBinaryExpressionPrecedence(Node* node) {
	auto* n = node->as<BinaryExpression>();
	OperatorPrecedence precedence = getExpressionPrecedence(node);
	OperatorPrecedence leftPrec = precedence;
	OperatorPrecedence rightPrec = precedence;
	switch (precedence) {
	case OperatorPrecedenceComma:
		// No need to parenthesize the right operand when the binary operator
		// and operand are both ,:
		//  x,(a,b)     => x,a,b
		break;
	case OperatorPrecedenceAssignment:
		// assignment is right-associative
		leftPrec = OperatorPrecedenceConditional;
		rightPrec = OperatorPrecedenceYield;
		break;
	case OperatorPrecedenceLogicalOR:
		rightPrec = OperatorPrecedenceLogicalAND;
		break;
	case OperatorPrecedenceLogicalAND:
		rightPrec = OperatorPrecedenceBitwiseOR;
		break;
	case OperatorPrecedenceBitwiseOR:
		// No need to parenthesize the right operand when the binary operator
		// and operand are both | due to the associative property of
		// mathematics:
		//  x|(a|b)     => x|a|b
		break;
	case OperatorPrecedenceBitwiseXOR:
		// No need to parenthesize the right operand when the binary operator
		// and operand are both ^ due to the associative property of
		// mathematics:
		//  x^(a^b)     => x^a^b
		break;
	case OperatorPrecedenceBitwiseAND:
		// No need to parenthesize the right operand when the binary operator
		// and operand are both & due to the associative property of
		// mathematics:
		//  x&(a&b)     => x&a&b
		break;
	case OperatorPrecedenceEquality:
		rightPrec = OperatorPrecedenceRelational;
		break;
	case OperatorPrecedenceRelational:
		rightPrec = OperatorPrecedenceShift;
		break;
	case OperatorPrecedenceShift:
		rightPrec = OperatorPrecedenceAdditive;
		break;
	case OperatorPrecedenceAdditive:
		if (n->OperatorToken->kind == Kind::PlusToken &&
		    isBinaryOperation(n->Right, Kind::PlusToken)) {
			Kind leftKind = getLiteralKindOfBinaryPlusOperand(n->Left);
			if (isLiteralKind(leftKind) &&
			    leftKind == getLiteralKindOfBinaryPlusOperand(n->Right)) {
				// No need to parenthesize the right operand when the binary
				// operator is plus (+) if both the left and right operands
				// consist solely of either literals of the same kind or
				// binary plus (+) expressions for literals of the same kind
				// (recursively).
				//  "a"+(1+2)       => "a"+(1+2)
				//  "a"+("b"+"c")   => "a"+"b"+"c"
				break;
			}
		}
		rightPrec = OperatorPrecedenceMultiplicative;
		break;
	case OperatorPrecedenceMultiplicative:
		if (n->OperatorToken->kind == Kind::AsteriskToken &&
		    isBinaryOperation(n->Right, Kind::AsteriskToken)) {
			// No need to parenthesize the right operand when the binary
			// operator and operand are both * due to the associative property
			// of mathematics:
			//  x*(a*b)     => x*a*b
			break;
		}
		rightPrec = OperatorPrecedenceExponentiation;
		break;
	case OperatorPrecedenceExponentiation:
		// exponentiation is right-associative
		leftPrec = OperatorPrecedenceUpdate;
		break;
	default:
		printerPanic("unhandled precedence: ", static_cast<int>(precedence));
	}
	return {leftPrec, rightPrec};
}

void Printer::emitBinaryExpression(Node* node) {
	auto* n = node->as<BinaryExpression>();
	auto [leftPrec, rightPrec] = getBinaryExpressionPrecedence(node);
	Node* emittedLeft = skipPartiallyEmittedExpressions(n->Left);
	if (nodeIsSynthesized(emittedLeft) &&
	    emittedLeft->kind == Kind::BinaryExpression &&
	    mixingBinaryOperatorsRequiresParentheses(
	        n->OperatorToken->kind,
	        emittedLeft->as<BinaryExpression>()->OperatorToken->kind)) {
		leftPrec = OperatorPrecedenceHighest;
	}
	Node* emittedRight = skipPartiallyEmittedExpressions(n->Right);
	if (nodeIsSynthesized(emittedRight) &&
	    emittedRight->kind == Kind::BinaryExpression &&
	    mixingBinaryOperatorsRequiresParentheses(
	        n->OperatorToken->kind,
	        emittedRight->as<BinaryExpression>()->OperatorToken->kind)) {
		rightPrec = OperatorPrecedenceHighest;
	}
	printerState state = enterNode(node);
	emitExpression(n->Left, leftPrec);
	int linesBeforeOperator =
	    getLinesBetweenNodes(node, n->Left, n->OperatorToken);
	int linesAfterOperator =
	    getLinesBetweenNodes(node, n->OperatorToken, n->Right);
	writeLinesAndIndent(linesBeforeOperator,
	                    n->OperatorToken->kind != Kind::CommaToken);
	emitTokenNodeEx(n->OperatorToken, tefNoSourceMaps);
	writeLinesAndIndent(linesAfterOperator,
	                    true /*writeSpaceIfNotIndenting*/); // Binary operators
	                                                      // should have a
	                                                      // space before the
	                                                      // comment starts
	emitExpression(n->Right, rightPrec);
	decreaseIndentIf(linesAfterOperator > 0);
	decreaseIndentIf(linesBeforeOperator > 0);
	exitNode(node, state);
}

void Printer::emitShortCircuitExpression(Node* node) {
	if (isBinaryOperation(skipPartiallyEmittedExpressions(node),
	                      Kind::QuestionQuestionToken)) {
		emitExpression(node, OperatorPrecedenceCoalesce);
	} else {
		emitExpression(node, OperatorPrecedenceLogicalOR);
	}
}

void Printer::emitConditionalExpression(Node* node) {
	auto* n = node->as<ConditionalExpression>();
	printerState state = enterNode(node);
	int linesBeforeQuestion =
	    getLinesBetweenNodes(node, n->Condition, n->QuestionToken);
	int linesAfterQuestion =
	    getLinesBetweenNodes(node, n->QuestionToken, n->WhenTrue);
	int linesBeforeColon =
	    getLinesBetweenNodes(node, n->WhenTrue, n->ColonToken);
	int linesAfterColon =
	    getLinesBetweenNodes(node, n->ColonToken, n->WhenFalse);
	emitShortCircuitExpression(n->Condition);
	writeLinesAndIndent(linesBeforeQuestion, true);
	emitPunctuationNode(n->QuestionToken);
	writeLinesAndIndent(linesAfterQuestion, true);
	emitExpression(n->WhenTrue, OperatorPrecedenceYield);
	decreaseIndentIf(linesAfterQuestion > 0);
	decreaseIndentIf(linesBeforeQuestion > 0);
	writeLinesAndIndent(linesBeforeColon, true);
	emitPunctuationNode(n->ColonToken);
	writeLinesAndIndent(linesAfterColon, true);
	emitExpression(n->WhenFalse, OperatorPrecedenceYield);
	decreaseIndentIf(linesAfterColon > 0);
	decreaseIndentIf(linesBeforeColon > 0);
	exitNode(node, state);
}

void Printer::emitTemplateExpression(Node* node) {
	auto* n = node->as<TemplateExpression>();
	printerState state = enterNode(node);
	emitTemplateHead(n->Head);
	emitList(&Printer::emitTemplateSpanNode, node, n->TemplateSpans,
	         LFTemplateExpressionSpans);
	exitNode(node, state);
}

void Printer::emitYieldExpression(Node* node) {
	auto* n = node->as<YieldExpression>();
	printerState state = enterNode(node);
	emitToken(Kind::YieldKeyword, node->pos(), WriteKind::Keyword, node);
	emitPunctuationNode(n->AsteriskToken);
	if (n->Expression != nullptr) {
		writeSpace();
		emitExpressionNoASI(n->Expression, OperatorPrecedenceDisallowComma);
	}
	exitNode(node, state);
}

void Printer::emitSpreadElement(Node* node) {
	auto* n = node->as<SpreadElement>();
	printerState state = enterNode(node);
	emitToken(Kind::DotDotDotToken, node->pos(), WriteKind::Punctuation,
	          node);
	emitExpression(n->Expression, OperatorPrecedenceDisallowComma);
	exitNode(node, state);
}

void Printer::emitClassExpression(Node* node) {
	auto* n = node->as<ClassExpression>();
	printerState state = enterNode(node);
	generateNameIfNeeded(n->name);

	TextPos pos =
	    emitModifierList(node, node->modifiers(), /*allowDecorators*/ true);
	emitToken(Kind::ClassKeyword, pos, WriteKind::Keyword, node);

	if (n->name != nullptr) {
		writeSpace();
		emitIdentifierName(n->name);
	}

	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);

	emitTypeParameters(node, n->TypeParameters);
	emitList(&Printer::emitHeritageClauseNode, node, n->HeritageClauses,
	         LFClassHeritageClauses);
	writeSpace();
	writePunctuation("{");
	pushNameGenerationScope(node);
	generateAllMemberNames(n->Members);
	emitList(&Printer::emitClassElement, node, n->Members, LFClassMembers);
	popNameGenerationScope(node);
	writePunctuation("}");

	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitOmittedExpression(Node* node) {
	exitNode(node, enterNode(node));
}

void Printer::emitExpressionWithTypeArguments(Node* node) {
	auto* n = node->as<ExpressionWithTypeArguments>();
	printerState state = enterNode(node);
	emitExpression(n->Expression, OperatorPrecedenceMember);
	emitTypeArguments(node, n->TypeArguments);
	exitNode(node, state);
}

void Printer::emitAsExpression(Node* node) {
	auto* n = node->as<AsExpression>();
	printerState state = enterNode(node);
	emitExpression(n->Expression, OperatorPrecedenceRelational);
	writeSpace();
	writeKeyword("as");
	writeSpace();
	emitTypeNodeOutsideExtends(n->Type);
	exitNode(node, state);
}

void Printer::emitSatisfiesExpression(Node* node) {
	auto* n = node->as<SatisfiesExpression>();
	printerState state = enterNode(node);
	emitExpression(n->Expression, OperatorPrecedenceRelational);
	writeSpace();
	writeKeyword("satisfies");
	writeSpace();
	emitTypeNodeOutsideExtends(n->Type);
	exitNode(node, state);
}

void Printer::emitNonNullExpression(Node* node) {
	printerState state = enterNode(node);
	emitExpression(node->as<NonNullExpression>()->Expression,
	               OperatorPrecedenceMember);
	writeOperator("!");
	exitNode(node, state);
}

void Printer::emitMetaProperty(Node* node) {
	auto* n = node->as<MetaProperty>();
	printerState state = enterNode(node);
	emitToken(n->KeywordToken, node->pos(), WriteKind::Punctuation, node);
	writePunctuation(".");
	emitIdentifierName(n->name);
	exitNode(node, state);
}

void Printer::emitPartiallyEmittedExpression(Node* node) {
	// avoid reprinting parens for nested partially emitted expressions
	struct entry {
		Node* node;
		printerState state;
	};
	Stack<entry> stack;
	for (;;) {
		printerState state = enterNode(node);
		EmitFlags emitFlags = emitContext->emitFlags(node);
		auto* n = node->as<PartiallyEmittedExpression>();
		if ((emitFlags & EFNoLeadingComments) == 0 &&
		    node->pos() != n->Expression->pos()) {
			emitTrailingCommentsOfPosition(n->Expression->pos(),
			                             /*prefixSpace*/ false,
			                             /*forceNoNewline*/ false);
		}
		stack.push(entry{node, state});
		if (!isPartiallyEmittedExpression(n->Expression)) {
			break;
		}
		node = n->Expression;
	}

	emitExpression(node->as<PartiallyEmittedExpression>()->Expression,
	               OperatorPrecedenceLowest);

	// unwind stack
	while (stack.len() > 0) {
		entry e = stack.pop();
		EmitFlags emitFlags = emitContext->emitFlags(node);
		auto* n = node->as<PartiallyEmittedExpression>();
		if ((emitFlags & EFNoTrailingComments) == 0 &&
		    node->end() != n->Expression->end()) {
			emitLeadingCommentsOfPosition(n->Expression->end());
		}
		exitNode(node, e.state);
		node = e.node;
	}
}

bool Printer::commentWillEmitNewLine(CommentRange comment) {
	return comment.kind == Kind::SingleLineCommentTrivia ||
	       comment.HasTrailingNewLine;
}

bool Printer::syntheticCommentWillEmitNewLine(
    const SynthesizedComment& comment) {
	return comment.Kind == Kind::SingleLineCommentTrivia ||
	       comment.HasTrailingNewLine;
}

bool Printer::willEmitLeadingNewLine(Node* node) {
	if (currentSourceFile == nullptr) {
		return false;
	}
	bool hasLeadingCommentRanges = false;
	bool hasNewLineComment = false;
	for (const CommentRange& comment : collectLeadingCommentRanges(currentSourceFile->text, node->pos())) {
		hasLeadingCommentRanges = true;
		if (commentWillEmitNewLine(comment)) {
			hasNewLineComment = true;
		}
	}
	if (hasLeadingCommentRanges) {
		Node* parseNode = emitContext->parseNode(node);
		if (parseNode != nullptr &&
		    isParenthesizedExpression(parseNode->parent)) {
			return true;
		}
	}
	if (hasNewLineComment) {
		return true;
	}
	auto leading = emitContext->getSyntheticLeadingComments(node);
	if (std::any_of(leading.begin(), leading.end(),
	                [this](const SynthesizedComment& c) {
		                return syntheticCommentWillEmitNewLine(c);
	                })) {
		return true;
	}
	if (isPartiallyEmittedExpression(node)) {
		auto* pee = node->as<PartiallyEmittedExpression>();
		if (node->pos() != pee->Expression->pos()) {
			for (const CommentRange& comment :
			     collectTrailingCommentRanges(currentSourceFile->text, pee->Expression->pos())) {
				if (commentWillEmitNewLine(comment)) {
					return true;
				}
			}
		}
		return willEmitLeadingNewLine(pee->Expression);
	}
	return false;
}

// parenthesizeExpressionForNoAsi wraps an expression in parens if we would
// emit a leading comment that would introduce a line separator between the
// node and its parent.
Node* Printer::parenthesizeExpressionForNoAsi(Node* node) {
	if (!commentsDisabled) {
		switch (node->kind) {
		case Kind::PartiallyEmittedExpression: {
			if (willEmitLeadingNewLine(node)) {
				auto* pee = node->as<PartiallyEmittedExpression>();
				Node* parseNode = emitContext->parseNode(node);
				if (parseNode != nullptr &&
				    isParenthesizedExpression(parseNode)) {
					// If the original node was a parenthesized expression,
					// restore it to preserve comment and source map emit
					Node* parens =
					    emitContext->factory.asNodeFactory()
					        ->newParenthesizedExpression(pee->Expression);
					emitContext->setOriginal(parens, node);
					parens->loc = parseNode->loc;
					return parens;
				}
				return emitContext->factory.asNodeFactory()
				    ->newParenthesizedExpression(node);
			}
			auto* pee = node->as<PartiallyEmittedExpression>();
			return emitContext->factory.asNodeFactory()
			    ->updatePartiallyEmittedExpression(
			        pee,
			        parenthesizeExpressionForNoAsi(pee->Expression));
		}
		case Kind::PropertyAccessExpression: {
			auto* pae = node->as<PropertyAccessExpression>();
			return emitContext->factory.asNodeFactory()
			    ->updatePropertyAccessExpression(
			        pae,
			        parenthesizeExpressionForNoAsi(pae->Expression),
			        pae->QuestionDotToken, pae->name, pae->flags);
		}
		case Kind::ElementAccessExpression: {
			auto* eae = node->as<ElementAccessExpression>();
			return emitContext->factory.asNodeFactory()
			    ->updateElementAccessExpression(
			        eae,
			        parenthesizeExpressionForNoAsi(eae->Expression),
			        eae->QuestionDotToken, eae->ArgumentExpression,
			        eae->flags);
		}
		case Kind::CallExpression: {
			auto* ce = node->as<CallExpression>();
			return emitContext->factory.asNodeFactory()
			    ->updateCallExpression(
			        ce, parenthesizeExpressionForNoAsi(ce->Expression),
			        ce->QuestionDotToken, ce->TypeArguments, ce->Arguments,
			        ce->flags);
		}
		case Kind::TaggedTemplateExpression: {
			auto* tte = node->as<TaggedTemplateExpression>();
			return emitContext->factory.asNodeFactory()
			    ->updateTaggedTemplateExpression(
			        tte, parenthesizeExpressionForNoAsi(tte->Tag),
			        tte->QuestionDotToken, tte->TypeArguments, tte->Template,
			        tte->flags);
		}
		case Kind::PostfixUnaryExpression: {
			auto* pue = node->as<PostfixUnaryExpression>();
			return emitContext->factory.asNodeFactory()
			    ->updatePostfixUnaryExpression(
			        pue, parenthesizeExpressionForNoAsi(pue->Operand),
			        pue->Operator);
		}
		case Kind::BinaryExpression: {
			auto* be = node->as<BinaryExpression>();
			return emitContext->factory.asNodeFactory()
			    ->updateBinaryExpression(
			        be, node->modifiers(),
			        parenthesizeExpressionForNoAsi(be->Left), be->Type,
			        be->OperatorToken, be->Right);
		}
		case Kind::ConditionalExpression: {
			auto* ce = node->as<ConditionalExpression>();
			return emitContext->factory.asNodeFactory()
			    ->updateConditionalExpression(
			        ce, parenthesizeExpressionForNoAsi(ce->Condition),
			        ce->QuestionToken, ce->WhenTrue, ce->ColonToken,
			        ce->WhenFalse);
		}
		case Kind::AsExpression: {
			auto* ae = node->as<AsExpression>();
			return emitContext->factory.asNodeFactory()->updateAsExpression(
			    ae, parenthesizeExpressionForNoAsi(ae->Expression), ae->Type);
		}
		case Kind::SatisfiesExpression: {
			auto* se = node->as<SatisfiesExpression>();
			return emitContext->factory.asNodeFactory()
			    ->updateSatisfiesExpression(
			        se, parenthesizeExpressionForNoAsi(se->Expression),
			        se->Type);
		}
		case Kind::NonNullExpression: {
			auto* nne = node->as<NonNullExpression>();
			return emitContext->factory.asNodeFactory()
			    ->updateNonNullExpression(
			        nne, parenthesizeExpressionForNoAsi(nne->Expression),
			        nne->flags);
		}
		default:
			break;
		}
	}
	return node;
}

void Printer::emitExpressionNoASI(Node* node, OperatorPrecedence precedence) {
	node = parenthesizeExpressionForNoAsi(node);
	emitExpression(node, precedence);
}

void Printer::emitExpression(Node* node, OperatorPrecedence precedence) {
	bool parens = getExpressionPrecedence(
	                  skipPartiallyEmittedExpressions(node)) < precedence;
	if (parens) {
		writePunctuation("(");
	}

	switch (node->kind) {
	// Keywords
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::NullKeyword:
		emitTokenNode(node);
		break;
	case Kind::ThisKeyword:
	case Kind::SuperKeyword:
	case Kind::ImportKeyword:
		emitKeywordExpression(node);
		break;

	// Literals
	case Kind::NumericLiteral:
		emitNumericLiteral(node);
		break;
	case Kind::BigIntLiteral:
		emitBigIntLiteral(node);
		break;
	case Kind::StringLiteral:
		emitStringLiteral(node);
		break;
	case Kind::RegularExpressionLiteral:
		emitRegularExpressionLiteral(node);
		break;
	case Kind::NoSubstitutionTemplateLiteral:
		emitNoSubstitutionTemplateLiteral(node);
		break;

	// Identifiers
	case Kind::Identifier:
		emitIdentifierReference(node);
		break;
	case Kind::PrivateIdentifier:
		emitPrivateIdentifier(node);
		break;

	// Expressions
	case Kind::ArrayLiteralExpression:
		emitArrayLiteralExpression(node);
		break;
	case Kind::ObjectLiteralExpression:
		emitObjectLiteralExpression(node);
		break;
	case Kind::PropertyAccessExpression:
		emitPropertyAccessExpression(node);
		break;
	case Kind::ElementAccessExpression:
		emitElementAccessExpression(node);
		break;
	case Kind::CallExpression:
		emitCallExpression(node);
		break;
	case Kind::NewExpression:
		emitNewExpression(node);
		break;
	case Kind::TaggedTemplateExpression:
		emitTaggedTemplateExpression(node);
		break;
	case Kind::TypeAssertionExpression:
		emitTypeAssertionExpression(node);
		break;
	case Kind::ParenthesizedExpression:
		emitParenthesizedExpression(node);
		break;
	case Kind::FunctionExpression:
		emitFunctionExpression(node);
		break;
	case Kind::ArrowFunction:
		emitArrowFunction(node);
		break;
	case Kind::DeleteExpression:
		emitDeleteExpression(node);
		break;
	case Kind::TypeOfExpression:
		emitTypeOfExpression(node);
		break;
	case Kind::VoidExpression:
		emitVoidExpression(node);
		break;
	case Kind::AwaitExpression:
		emitAwaitExpression(node);
		break;
	case Kind::PrefixUnaryExpression:
		emitPrefixUnaryExpression(node);
		break;
	case Kind::PostfixUnaryExpression:
		emitPostfixUnaryExpression(node);
		break;
	case Kind::BinaryExpression:
		emitBinaryExpression(node);
		break;
	case Kind::ConditionalExpression:
		emitConditionalExpression(node);
		break;
	case Kind::TemplateExpression:
		emitTemplateExpression(node);
		break;
	case Kind::YieldExpression:
		emitYieldExpression(node);
		break;
	case Kind::SpreadElement:
		emitSpreadElement(node);
		break;
	case Kind::ClassExpression:
		emitClassExpression(node);
		break;
	case Kind::OmittedExpression:
		emitOmittedExpression(node);
		break;
	case Kind::AsExpression:
		emitAsExpression(node);
		break;
	case Kind::NonNullExpression:
		emitNonNullExpression(node);
		break;
	case Kind::ExpressionWithTypeArguments:
		emitExpressionWithTypeArguments(node);
		break;
	case Kind::SatisfiesExpression:
		emitSatisfiesExpression(node);
		break;
	case Kind::MetaProperty:
		emitMetaProperty(node);
		break;
	case Kind::SyntheticExpression:
		printerPanic("SyntheticExpression should never be printed.");
		break;
	case Kind::MissingDeclaration:
		// Missing declarations do not emit an expression.
		break;

	// JSX
	case Kind::JsxElement:
		emitJsxElement(node);
		break;
	case Kind::JsxSelfClosingElement:
		emitJsxSelfClosingElement(node);
		break;
	case Kind::JsxFragment:
		emitJsxFragment(node);
		break;

	// Synthesized list
	case Kind::SyntaxList:
		printerPanic("SyntaxList should not be printed");
		break;

	// Transformation nodes
	case Kind::NotEmittedStatement:
		return;
	case Kind::PartiallyEmittedExpression:
		emitPartiallyEmittedExpression(node);
		break;
	case Kind::SyntheticReferenceExpression:
		printerPanic("SyntheticReferenceExpression should not be printed");
		break;

	default:
		printerPanic("unexpected Expression: ", kindName(node->kind));
	}

	if (parens) {
		writePunctuation(")");
	}
}

//
// Misc — printer.go:3347
//

void Printer::emitTemplateSpan(Node* node) {
	auto* n = node->as<TemplateSpan>();
	printerState state = enterNode(node);
	emitExpression(n->Expression, OperatorPrecedenceComma);
	emitTemplateMiddleTail(n->Literal);
	exitNode(node, state);
}

void Printer::emitTemplateSpanNode(Node* node) { emitTemplateSpan(node); }

void Printer::emitSemicolonClassElement(Node* node) {
	printerState state = enterNode(node);
	writeTrailingSemicolon();
	exitNode(node, state);
}

//
// Statements — printer.go:3371
//

bool Printer::isEmptyBlock(Node* block, NodeList* statements) {
	return statements->nodes.empty() &&
	       (currentSourceFile == nullptr ||
	        rangeEndIsOnSameLineAsRangeStart(block->loc, block->loc,
	                                         currentSourceFile));
}

void Printer::emitBlock(Node* node) {
	auto* n = node->as<Block>();
	printerState state = enterNode(node);
	generateNames(node);
	emitToken(Kind::OpenBraceToken, node->pos(), WriteKind::Punctuation,
	          node);

	ListFormat format =
	    (!n->MultiLine && isEmptyBlock(node, n->Statements)) ||
	                    shouldEmitOnSingleLine(node)
	                ? LFSingleLineBlockStatements
	                : LFMultiLineBlockStatements;
	emitList(&Printer::emitStatement, node, n->Statements, format);

	emitTokenEx(Kind::CloseBraceToken, n->Statements->end(),
	            WriteKind::Punctuation, node,
	            (format & LFMultiLine) != 0 ? tefIndentLeadingComments
	                                        : tefNone);
	exitNode(node, state);
}

void Printer::emitVariableStatement(Node* node) {
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	emitVariableDeclarationList(
	    node->as<VariableStatement>()->DeclarationList);
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitEmptyStatement(Node* node, bool isEmbeddedStatement) {
	printerState state = enterNode(node);

	// While most trailing semicolons are possibly insignificant, an embedded
	// "empty" statement is significant and cannot be elided by a
	// trailing-semicolon-omitting writer.
	if (isEmbeddedStatement) {
		writePunctuation(";");
	} else {
		writeTrailingSemicolon();
	}
	exitNode(node, state);
}

void Printer::emitExpressionStatement(Node* node) {
	auto* n = node->as<ExpressionStatement>();
	printerState state = enterNode(node);

	if (currentSourceFile != nullptr &&
	    currentSourceFile->ScriptKind == ScriptKind::JSON) {
		// !!! In strada, this was handled by an undefined parenthesizerRule,
		// so this is a hack.
		emitExpression(n->Expression, OperatorPrecedenceComma);
	} else if (isImmediatelyInvokedFunctionExpressionOrArrowFunction(
	               n->Expression)) {
		// For IIFEs, parenthesize just the callee (not the whole call),
		// matching TypeScript's parenthesizeExpressionOfExpressionStatement
		// which wraps the function/arrow in parens:
		//   (function() { })()  -- not (function() { }())
		emitIIFEWithParenthesizedCallee(n->Expression);
	} else {
		switch (getLeftmostExpression(n->Expression,
		                            /*stopAtCallExpressions*/ false)
		            ->kind) {
		case Kind::FunctionExpression:
		case Kind::ObjectLiteralExpression:
			emitExpression(n->Expression, OperatorPrecedenceParentheses);
			break;
		default:
			emitExpression(n->Expression, OperatorPrecedenceComma);
			break;
		}
	}

	// Emit semicolon in non json files
	// or if json file that created synthesized expression(eg.define
	// expression statement when --out and amd code generation)
	if (currentSourceFile == nullptr ||
	    currentSourceFile->ScriptKind != ScriptKind::JSON ||
	    nodeIsSynthesized(n->Expression)) {
		writeTrailingSemicolon();
	}

	exitNode(node, state);
}

// emitIIFEWithParenthesizedCallee emits a call expression that is an IIFE,
// wrapping just the callee in parens rather than the entire call expression.
// This matches TypeScript's parenthesizeExpressionOfExpressionStatement
// behavior:
//
//	(function() { })()   -- parens around callee only
//
// instead of:
//
//	(function() { }())   -- parens around entire call
void Printer::emitIIFEWithParenthesizedCallee(Node* node) {
	// Walk through PartiallyEmittedExpression wrappers to find the call
	Node* call = skipPartiallyEmittedExpressions(node);
	auto* n = call->as<CallExpression>();
	printerState state = enterNode(call);
	// Emit the callee wrapped in parens
	writePunctuation("(");
	emitExpression(n->Expression, OperatorPrecedenceLowest);
	writePunctuation(")");
	emitTokenNode(n->QuestionDotToken);
	emitTypeArguments(call, n->TypeArguments);
	emitList(&Printer::emitArgument, call, n->Arguments,
	         LFCallExpressionArguments);
	exitNode(call, state);
}

void Printer::emitIfStatement(Node* node) {
	auto* n = node->as<IfStatement>();
	printerState state = enterNode(node);
	TextPos pos = emitToken(Kind::IfKeyword, node->pos(), WriteKind::Keyword,
	                        node);
	writeSpace();
	emitToken(Kind::OpenParenToken, pos, WriteKind::Punctuation, node);
	emitExpression(n->Expression, OperatorPrecedenceLowest);
	emitToken(Kind::CloseParenToken, n->Expression->end(),
	          WriteKind::Punctuation, node);
	emitEmbeddedStatement(node, n->ThenStatement);
	if (n->ElseStatement != nullptr) {
		writeLineOrSpace(node, n->ThenStatement, n->ElseStatement);
		emitToken(Kind::ElseKeyword, n->ThenStatement->end(),
		          WriteKind::Keyword, node);
		if (n->ElseStatement->kind == Kind::IfStatement) {
			writeSpace();
			emitIfStatement(n->ElseStatement);
		} else {
			emitEmbeddedStatement(node, n->ElseStatement);
		}
	}
	exitNode(node, state);
}

void Printer::emitWhileClause(Node* node, Node* expression,
                              TextPos startPos) {
	TextPos pos =
	    emitToken(Kind::WhileKeyword, startPos, WriteKind::Keyword, node);
	writeSpace();
	emitToken(Kind::OpenParenToken, pos, WriteKind::Punctuation, node);
	emitExpression(expression, OperatorPrecedenceLowest);
	emitToken(Kind::CloseParenToken, expression->end(),
	          WriteKind::Punctuation, node);
}

void Printer::emitDoStatement(Node* node) {
	auto* n = node->as<DoStatement>();
	printerState state = enterNode(node);
	emitToken(Kind::DoKeyword, node->pos(), WriteKind::Keyword, node);
	emitEmbeddedStatement(node, n->Statement);
	if (isBlock(n->Statement) && !Options.PreserveSourceNewlines) {
		writeSpace();
	} else {
		writeLineOrSpace(node, n->Statement, n->Expression);
	}

	emitWhileClause(node, n->Expression, n->Statement->end());
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitWhileStatement(Node* node) {
	auto* n = node->as<WhileStatement>();
	printerState state = enterNode(node);
	emitWhileClause(node, n->Expression, node->pos());
	emitEmbeddedStatement(node, n->Statement);
	exitNode(node, state);
}

void Printer::emitForInitializer(Node* node) {
	if (node->kind == Kind::VariableDeclarationList) {
		emitVariableDeclarationList(node);
	} else {
		emitExpression(node, OperatorPrecedenceLowest);
	}
}

void Printer::emitForStatement(Node* node) {
	auto* n = node->as<ForStatement>();
	printerState state = enterNode(node);
	TextPos pos = emitToken(Kind::ForKeyword, node->pos(), WriteKind::Keyword,
	                        node);
	writeSpace();
	pos = emitToken(Kind::OpenParenToken, pos, WriteKind::Punctuation, node);
	if (n->Initializer != nullptr) {
		emitForInitializer(n->Initializer);
		pos = n->Initializer->end();
	}
	pos = emitToken(Kind::SemicolonToken, pos, WriteKind::Punctuation, node);
	if (n->Condition != nullptr) {
		writeSpace();
		emitExpression(n->Condition, OperatorPrecedenceLowest);
		pos = n->Condition->end();
	}
	pos = emitToken(Kind::SemicolonToken, pos, WriteKind::Punctuation, node);
	if (n->Incrementor != nullptr) {
		writeSpace();
		emitExpression(n->Incrementor, OperatorPrecedenceLowest);
		pos = n->Incrementor->end();
	}
	emitToken(Kind::CloseParenToken, pos, WriteKind::Punctuation, node);
	emitEmbeddedStatement(node, n->Statement);
	exitNode(node, state);
}

void Printer::emitForInStatement(Node* node) {
	auto* n = node->as<ForInOrOfStatement>();
	printerState state = enterNode(node);
	TextPos pos = emitToken(Kind::ForKeyword, node->pos(), WriteKind::Keyword,
	                        node);
	writeSpace();
	emitToken(Kind::OpenParenToken, pos, WriteKind::Punctuation, node);
	emitForInitializer(n->Initializer);
	writeSpace();
	emitToken(Kind::InKeyword, n->Initializer->end(), WriteKind::Keyword,
	          node);
	writeSpace();
	emitExpression(n->Expression, OperatorPrecedenceLowest);
	emitToken(Kind::CloseParenToken, n->Expression->end(),
	          WriteKind::Punctuation, node);
	emitEmbeddedStatement(node, n->Statement);
	exitNode(node, state);
}

void Printer::emitForOfStatement(Node* node) {
	auto* n = node->as<ForInOrOfStatement>();
	printerState state = enterNode(node);
	TextPos openParenPos =
	    emitToken(Kind::ForKeyword, node->pos(), WriteKind::Keyword, node);
	writeSpace();
	if (n->AwaitModifier != nullptr) {
		emitKeywordNode(n->AwaitModifier);
		writeSpace();
	}
	emitToken(Kind::OpenParenToken, openParenPos, WriteKind::Punctuation,
	          node);
	emitForInitializer(n->Initializer);
	writeSpace();
	emitToken(Kind::OfKeyword, n->Initializer->end(), WriteKind::Keyword,
	          node);
	writeSpace();
	emitExpression(n->Expression, OperatorPrecedenceLowest);
	emitToken(Kind::CloseParenToken, n->Expression->end(),
	          WriteKind::Punctuation, node);
	emitEmbeddedStatement(node, n->Statement);
	exitNode(node, state);
}

void Printer::emitContinueStatement(Node* node) {
	auto* n = node->as<ContinueStatement>();
	printerState state = enterNode(node);
	emitToken(Kind::ContinueKeyword, node->pos(), WriteKind::Keyword, node);
	if (n->Label != nullptr) {
		writeSpace();
		emitLabelIdentifier(n->Label);
	}
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitBreakStatement(Node* node) {
	auto* n = node->as<BreakStatement>();
	printerState state = enterNode(node);
	emitToken(Kind::BreakKeyword, node->pos(), WriteKind::Keyword, node);
	if (n->Label != nullptr) {
		writeSpace();
		emitLabelIdentifier(n->Label);
	}
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitReturnStatement(Node* node) {
	auto* n = node->as<ReturnStatement>();
	printerState state = enterNode(node);
	emitToken(Kind::ReturnKeyword, node->pos(), WriteKind::Keyword, node);
	if (n->Expression != nullptr) {
		writeSpace();
		emitExpressionNoASI(n->Expression, OperatorPrecedenceLowest);
	}
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitWithStatement(Node* node) {
	auto* n = node->as<WithStatement>();
	printerState state = enterNode(node);
	TextPos pos = emitToken(Kind::WithKeyword, node->pos(), WriteKind::Keyword,
	                        node);
	writeSpace();
	emitToken(Kind::OpenParenToken, pos, WriteKind::Punctuation, node);
	emitExpression(n->Expression, OperatorPrecedenceLowest);
	emitToken(Kind::CloseParenToken, n->Expression->end(),
	          WriteKind::Punctuation, node);
	emitEmbeddedStatement(node, n->Statement);
	exitNode(node, state);
}

void Printer::emitSwitchStatement(Node* node) {
	auto* n = node->as<SwitchStatement>();
	printerState state = enterNode(node);
	TextPos pos = emitToken(Kind::SwitchKeyword, node->pos(),
	                        WriteKind::Keyword, node);
	writeSpace();
	emitToken(Kind::OpenParenToken, pos, WriteKind::Punctuation, node);
	emitExpression(n->Expression, OperatorPrecedenceLowest);
	emitToken(Kind::CloseParenToken, n->Expression->end(),
	          WriteKind::Punctuation, node);
	writeSpace();
	emitCaseBlock(n->CaseBlock);
	exitNode(node, state);
}

void Printer::emitLabeledStatement(Node* node) {
	auto* n = node->as<LabeledStatement>();
	printerState state = enterNode(node);
	emitLabelIdentifier(n->Label);
	emitToken(Kind::ColonToken, n->Label->end(), WriteKind::Punctuation, node);

	// TODO: use emitEmbeddedStatement rather than writeSpace/emitStatement
	// here after Strada migration as it is more consistent with similar emit
	// elsewhere. writeSpace/emitStatement is used here to reduce spurious
	// diffs when testing the Strada migration.
	////p.emitEmbeddedStatement(node.AsNode(), node.Statement)

	writeSpace();
	emitStatement(n->Statement);

	exitNode(node, state);
}

void Printer::emitThrowStatement(Node* node) {
	auto* n = node->as<ThrowStatement>();
	printerState state = enterNode(node);
	emitToken(Kind::ThrowKeyword, node->pos(), WriteKind::Keyword, node);
	writeSpace();
	emitExpressionNoASI(n->Expression, OperatorPrecedenceLowest);
	writeTrailingSemicolon();
	exitNode(node, state);
}

static Node* coalesceNode(Node* a, Node* b) { return a != nullptr ? a : b; }

void Printer::emitTryStatement(Node* node) {
	auto* n = node->as<TryStatement>();
	printerState state = enterNode(node);
	emitToken(Kind::TryKeyword, node->pos(), WriteKind::Keyword, node);
	writeSpace();
	emitBlock(n->TryBlock);
	if (n->CatchClause != nullptr) {
		writeLineOrSpace(node, n->TryBlock, n->CatchClause);
		emitCatchClause(n->CatchClause);
	}
	if (n->FinallyBlock != nullptr) {
		Node* prev = coalesceNode(n->CatchClause, n->TryBlock);
		writeLineOrSpace(node, prev, n->FinallyBlock);
		emitToken(Kind::FinallyKeyword, prev->end(), WriteKind::Keyword,
		          node);
		writeSpace();
		emitBlock(n->FinallyBlock);
	}
	exitNode(node, state);
}

void Printer::emitDebuggerStatement(Node* node) {
	printerState state = enterNode(node);
	emitToken(Kind::DebuggerKeyword, node->pos(), WriteKind::Keyword, node);
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitNotEmittedStatement(Node* node) {
	exitNode(node, enterNode(node));
}

void Printer::emitNotEmittedTypeElement(Node* node) {
	exitNode(node, enterNode(node));
}

//
// Declarations — printer.go:3695
//

void Printer::emitVariableDeclaration(Node* node) {
	auto* n = node->as<VariableDeclaration>();
	printerState state = enterNode(node);
	emitBindingName(n->name);
	emitPunctuationNode(n->ExclamationToken);
	emitTypeAnnotation(n->Type);
	emitInitializer(n->Initializer,
	                greatestEnd(n->name->end(), n->Type,
	                            emitContext->getTypeNode(n->name)),
	                node);
	exitNode(node, state);
}

void Printer::emitVariableDeclarationNode(Node* node) {
	emitVariableDeclaration(node);
}

void Printer::emitVariableDeclarationList(Node* node) {
	auto* n = node->as<VariableDeclarationList>();
	printerState state = enterNode(node);
	if (isVarLet(node)) {
		writeKeyword("let");
	} else if (isVarConst(node)) {
		writeKeyword("const");
	} else if (isVarUsing(node)) {
		writeKeyword("using");
	} else if (isVarAwaitUsing(node)) {
		writeKeyword("await");
		writeSpace();
		writeKeyword("using");
	} else {
		writeKeyword("var");
	}
	writeSpace();
	emitList(&Printer::emitVariableDeclarationNode, node, n->Declarations,
	         LFVariableDeclarationList);
	exitNode(node, state);
}

void Printer::emitFunctionDeclaration(Node* node) {
	auto* n = node->as<FunctionDeclaration>();
	printerState state = enterNode(node);
	generateNameIfNeeded(n->name);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	writeKeyword("function");
	emitTokenNode(n->AsteriskToken);
	writeSpace();
	if (n->name != nullptr) {
		emitIdentifierName(n->name);
	}
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	pushNameGenerationScope(node);
	emitSignature(node);
	emitFunctionBodyNode(n->Body);
	popNameGenerationScope(node);
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitClassDeclaration(Node* node) {
	auto* n = node->as<ClassDeclaration>();
	printerState state = enterNode(node);
	generateNameIfNeeded(n->name);
	TextPos pos =
	    emitModifierList(node, node->modifiers(), /*allowDecorators*/ true);
	emitToken(Kind::ClassKeyword, pos, WriteKind::Keyword, node);
	if (n->name != nullptr) {
		writeSpace();
		emitIdentifierName(n->name);
	}
	bool indented = shouldEmitIndented(node);
	increaseIndentIf(indented);
	emitTypeParameters(node, n->TypeParameters);
	emitList(&Printer::emitHeritageClauseNode, node, n->HeritageClauses,
	         LFClassHeritageClauses);
	writeSpace();
	writePunctuation("{");
	pushNameGenerationScope(node);
	generateAllMemberNames(n->Members);
	emitList(&Printer::emitClassElement, node, n->Members, LFClassMembers);
	popNameGenerationScope(node);
	writePunctuation("}");
	decreaseIndentIf(indented);
	exitNode(node, state);
}

void Printer::emitInterfaceDeclaration(Node* node) {
	auto* n = node->as<InterfaceDeclaration>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	writeKeyword("interface");
	writeSpace();
	emitBindingIdentifier(n->name);
	emitTypeParameters(node, n->TypeParameters);
	emitList(&Printer::emitHeritageClauseNode, node, n->HeritageClauses,
	         LFHeritageClauses);
	writeSpace();
	writePunctuation("{");
	pushNameGenerationScope(node);
	generateAllMemberNames(n->Members);
	emitList(&Printer::emitTypeElement, node, n->Members, LFInterfaceMembers);
	popNameGenerationScope(node);
	writePunctuation("}");
	exitNode(node, state);
}

void Printer::emitTypeAliasDeclaration(Node* node) {
	auto* n = node->as<TypeAliasDeclaration>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	writeKeyword("type");
	writeSpace();
	emitBindingIdentifier(n->name);
	emitTypeParameters(node, n->TypeParameters);
	writeSpace();
	writePunctuation("=");
	writeSpace();
	emitTypeNodeOutsideExtends(n->Type);
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitEnumDeclaration(Node* node) {
	auto* n = node->as<EnumDeclaration>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	writeKeyword("enum");
	writeSpace();
	emitBindingIdentifier(n->name);
	writeSpace();
	writePunctuation("{");
	emitList(&Printer::emitEnumMemberNode, node, n->Members, LFEnumMembers);
	writePunctuation("}");
	exitNode(node, state);
}

void Printer::emitModuleDeclaration(Node* node) {
	auto* n = node->as<ModuleDeclaration>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	if (n->Keyword != Kind::GlobalKeyword) {
		writeKeyword(n->Keyword == Kind::NamespaceKeyword ? "namespace"
		                                                : "module");
		writeSpace();
	}
	emitModuleName(n->name);
	Node* body = n->Body;
	while (body != nullptr && isModuleDeclaration(body)) {
		auto* module = body->as<ModuleDeclaration>();
		writePunctuation(".");
		emitNestedModuleName(module->name);
		body = module->Body;
	}
	if (n->Attributes != nullptr) {
		writeSpace();
		writeKeyword("with");
		writeSpace();
		emitTypeNode(n->Attributes, TypePrecedenceNonArray);
	}
	if (body == nullptr) {
		writeTrailingSemicolon();
	} else {
		writeSpace();
		emitModuleBlock(body);
	}
	exitNode(node, state);
}

void Printer::emitModuleBlock(Node* node) {
	auto* n = node->as<ModuleBlock>();
	printerState state = enterNode(node);
	generateNames(node);
	emitToken(Kind::OpenBraceToken, node->pos(), WriteKind::Punctuation,
	          node);
	ListFormat format = isEmptyBlock(node, n->Statements) ||
	                            shouldEmitOnSingleLine(node)
	                        ? LFSingleLineBlockStatements
	                        : LFMultiLineBlockStatements;
	emitList(&Printer::emitStatement, node, n->Statements, format);
	emitTokenEx(Kind::CloseBraceToken, n->Statements->end(),
	            WriteKind::Punctuation, node,
	            (format & LFMultiLine) != 0 ? tefIndentLeadingComments
	                                        : tefNone);
	exitNode(node, state);
}

void Printer::emitCaseBlock(Node* node) {
	auto* n = node->as<CaseBlock>();
	printerState state = enterNode(node);
	emitToken(Kind::OpenBraceToken, node->pos(), WriteKind::Punctuation,
	          node);
	emitList(&Printer::emitCaseOrDefaultClauseNode, node, n->Clauses,
	         LFCaseBlockClauses);
	emitTokenEx(Kind::CloseBraceToken, n->Clauses->end(),
	            WriteKind::Punctuation, node, tefIndentLeadingComments);
	exitNode(node, state);
}

void Printer::emitImportEqualsDeclaration(Node* node) {
	auto* n = node->as<ImportEqualsDeclaration>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	TextPos pos = emitToken(Kind::ImportKeyword,
	                        greatestEnd(node->pos(), node->modifiers()),
	                        WriteKind::Keyword, node);
	writeSpace();
	if (n->IsTypeOnly) {
		emitToken(Kind::TypeKeyword, pos, WriteKind::Keyword, node);
		writeSpace();
	}
	emitBindingIdentifier(n->name);
	writeSpace();
	emitToken(Kind::EqualsToken, n->name->end(), WriteKind::Punctuation,
	          node);
	writeSpace();
	emitModuleReference(n->ModuleReference);
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitModuleReference(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
		emitIdentifierReference(node);
		break;
	case Kind::QualifiedName:
		emitQualifiedName(node);
		break;
	case Kind::ExternalModuleReference:
		emitExternalModuleReference(node);
		break;
	default:
		printerPanic("unhandled ModuleReference: ", kindName(node->kind));
	}
}

void Printer::emitImportDeclaration(Node* node) {
	auto* n = node->as<ImportDeclaration>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	emitToken(Kind::ImportKeyword, greatestEnd(node->pos(), node->modifiers()),
	          WriteKind::Keyword, node);
	writeSpace();
	if (n->ImportClause != nullptr) {
		emitImportClause(n->ImportClause);
		writeSpace();
		emitToken(Kind::FromKeyword, n->ImportClause->end(),
		          WriteKind::Keyword, node);
		writeSpace();
	}
	emitExpression(n->ModuleSpecifier, OperatorPrecedenceLowest);
	if (n->Attributes != nullptr) {
		writeSpace();
		emitImportAttributes(n->Attributes);
	}
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitImportClause(Node* node) {
	auto* n = node->as<ImportClause>();
	printerState state = enterNode(node);
	if (n->PhaseModifier != Kind::Unknown) {
		emitToken(n->PhaseModifier, node->pos(), WriteKind::Keyword, node);
		writeSpace();
	}
	if (n->name != nullptr) {
		emitBindingIdentifier(n->name);
		if (n->NamedBindings != nullptr) {
			emitToken(Kind::CommaToken, n->name->end(),
			          WriteKind::Punctuation, node);
			writeSpace();
		}
	}
	emitNamedImportBindings(n->NamedBindings);
	exitNode(node, state);
}

void Printer::emitNamespaceImport(Node* node) {
	auto* n = node->as<NamespaceImport>();
	printerState state = enterNode(node);
	TextPos pos = emitToken(Kind::AsteriskToken, node->pos(),
	                        WriteKind::Punctuation, node);
	writeSpace();
	emitToken(Kind::AsKeyword, pos, WriteKind::Keyword, node);
	writeSpace();
	emitBindingIdentifier(n->name);
	exitNode(node, state);
}

void Printer::emitNamedImports(Node* node) {
	auto* n = node->as<NamedImports>();
	printerState state = enterNode(node);
	writePunctuation("{");
	emitList(&Printer::emitImportSpecifierNode, node, n->Elements,
	         LFNamedImportsOrExportsElements);
	writePunctuation("}");
	exitNode(node, state);
}

void Printer::emitNamedImportBindings(Node* node) {
	if (node == nullptr) {
		return;
	}
	switch (node->kind) {
	case Kind::NamespaceImport:
		emitNamespaceImport(node);
		break;
	case Kind::NamedImports:
		emitNamedImports(node);
		break;
	default:
		printerPanic("unhandled NamedImportBindings: ",
		             kindName(node->kind));
	}
}

void Printer::emitImportSpecifier(Node* node) {
	auto* n = node->as<ImportSpecifier>();
	printerState state = enterNode(node);
	if (n->IsTypeOnly) {
		writeKeyword("type");
		writeSpace();
	}
	if (n->PropertyName != nullptr) {
		emitModuleExportName(n->PropertyName);
		writeSpace();
		emitToken(Kind::AsKeyword, n->PropertyName->end(), WriteKind::Keyword,
		          node);
		writeSpace();
	}
	emitBindingIdentifier(n->name);
	exitNode(node, state);
}

void Printer::emitImportSpecifierNode(Node* node) { emitImportSpecifier(node); }

void Printer::emitExportAssignment(Node* node) {
	auto* n = node->as<ExportAssignment>();
	printerState state = enterNode(node);
	TextPos nextPos = emitToken(Kind::ExportKeyword, node->pos(),
	                            WriteKind::Keyword, node);
	writeSpace();
	if (n->IsExportEquals) {
		emitToken(Kind::EqualsToken, nextPos, WriteKind::Operator, node);
	} else {
		emitToken(Kind::DefaultKeyword, nextPos, WriteKind::Keyword, node);
	}
	writeSpace();
	if (n->IsExportEquals) {
		emitExpression(n->Expression, OperatorPrecedenceAssignment);
	} else {
		// parenthesize `class` and `function` expressions so as not to
		// conflict with exported `class` and `function` declarations
		Node* expr = getLeftmostExpression(n->Expression,
		                                   /*stopAtCallExpressions*/ false);
		if (isClassExpression(expr) || isFunctionExpression(expr)) {
			emitExpression(n->Expression, OperatorPrecedenceParentheses);
		} else {
			emitExpression(n->Expression, OperatorPrecedenceAssignment);
		}
	}
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitExportDeclaration(Node* node) {
	auto* n = node->as<ExportDeclaration>();
	printerState state = enterNode(node);
	emitModifierList(node, node->modifiers(), /*allowDecorators*/ false);
	TextPos pos = emitToken(Kind::ExportKeyword, node->pos(),
	                        WriteKind::Keyword, node);
	writeSpace();
	if (n->IsTypeOnly) {
		pos = emitToken(Kind::TypeKeyword, pos, WriteKind::Keyword, node);
		writeSpace();
	}
	if (n->ExportClause != nullptr) {
		emitNamedExportBindings(n->ExportClause);
	} else {
		pos = emitToken(Kind::AsteriskToken, pos, WriteKind::Punctuation,
		                node);
	}
	if (n->ModuleSpecifier != nullptr) {
		writeSpace();
		emitToken(Kind::FromKeyword, greatestEnd(pos, n->ExportClause),
		          WriteKind::Keyword, node);
		writeSpace();
		emitExpression(n->ModuleSpecifier, OperatorPrecedenceLowest);
	}
	if (n->Attributes != nullptr) {
		writeSpace();
		emitImportAttributes(n->Attributes);
	}
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitImportAttributes(Node* node) {
	auto* n = node->as<ImportAttributes>();
	printerState state = enterNode(node);
	emitToken(n->Token, node->pos(), WriteKind::Keyword, node);
	writeSpace();
	emitList(&Printer::emitImportAttributeNode, node, n->Attributes,
	         LFImportAttributes);
	exitNode(node, state);
}

void Printer::emitImportAttribute(Node* node) {
	auto* n = node->as<ImportAttribute>();
	printerState state = enterNode(node);
	emitImportAttributeName(n->name);
	writePunctuation(":");
	writeSpace();
	Node* value = n->Value;
	if ((emitContext->emitFlags(n->Value) & EFNoLeadingComments) == 0) {
		TextRange commentRange = emitContext->commentRange(value);
		emitTrailingComments(commentRange.pos(), commentSeparatorAfter);
	}
	emitExpression(value, OperatorPrecedenceDisallowComma);
	exitNode(node, state);
}

void Printer::emitImportAttributeNode(Node* node) {
	emitImportAttribute(node);
}

void Printer::emitNamespaceExportDeclaration(Node* node) {
	auto* n = node->as<NamespaceExportDeclaration>();
	printerState state = enterNode(node);
	TextPos pos = emitToken(Kind::ExportKeyword, node->pos(),
	                        WriteKind::Keyword, node);
	writeSpace();
	pos = emitToken(Kind::AsKeyword, pos, WriteKind::Keyword, node);
	writeSpace();
	emitToken(Kind::NamespaceKeyword, pos, WriteKind::Keyword, node);
	writeSpace();
	emitBindingIdentifier(n->name);
	writeTrailingSemicolon();
	exitNode(node, state);
}

void Printer::emitNamespaceExport(Node* node) {
	auto* n = node->as<NamespaceExport>();
	printerState state = enterNode(node);
	TextPos pos = emitToken(Kind::AsteriskToken, node->pos(),
	                        WriteKind::Punctuation, node);
	writeSpace();
	emitToken(Kind::AsKeyword, pos, WriteKind::Keyword, node);
	writeSpace();
	emitModuleExportName(n->name);
	exitNode(node, state);
}

void Printer::emitNamedExports(Node* node) {
	auto* n = node->as<NamedExports>();
	printerState state = enterNode(node);
	writePunctuation("{");
	emitList(&Printer::emitExportSpecifierNode, node, n->Elements,
	         LFNamedImportsOrExportsElements);
	writePunctuation("}");
	exitNode(node, state);
}

void Printer::emitNamedExportBindings(Node* node) {
	switch (node->kind) {
	case Kind::NamespaceExport:
		emitNamespaceExport(node);
		break;
	case Kind::NamedExports:
		emitNamedExports(node);
		break;
	default:
		printerPanic("unhandled NamedExportBindings: ",
		             kindName(node->kind));
	}
}

void Printer::emitExportSpecifier(Node* node) {
	auto* n = node->as<ExportSpecifier>();
	printerState state = enterNode(node);
	if (n->IsTypeOnly) {
		writeKeyword("type");
		writeSpace();
	}
	if (n->PropertyName != nullptr) {
		emitModuleExportName(n->PropertyName);
		writeSpace();
		emitToken(Kind::AsKeyword, n->PropertyName->end(), WriteKind::Keyword,
		          node);
		writeSpace();
	}
	emitModuleExportName(n->name);
	exitNode(node, state);
}

void Printer::emitExportSpecifierNode(Node* node) { emitExportSpecifier(node); }

void Printer::emitEmbeddedStatement(Node* parentNode, Node* node) {
	if (isBlock(node) || shouldEmitOnSingleLine(parentNode) ||
	    (Options.PreserveSourceNewlines &&
	     getLeadingLineTerminatorCount(parentNode, node, LFNone) == 0)) {
		writeSpace();
		emitStatement(node);
	} else {
		writeLine();
		increaseIndent();
		if (node->kind == Kind::EmptyStatement) {
			emitEmptyStatement(node, /*isEmbeddedStatement*/ true);
		} else {
			emitStatement(node);
		}
		decreaseIndent();
	}
}

void Printer::emitStatement(Node* node) {
	if (SnippetElement* snippetElement = emitContext->snippetElement(node);
	    snippetElement != nullptr) {
		emitSnippetNode(node, snippetElement);
		return;
	}

	switch (node->kind) {
	// Statements
	case Kind::Block:
		emitBlock(node);
		break;
	case Kind::EmptyStatement:
		emitEmptyStatement(node, /*isEmbeddedStatement*/ false);
		break;
	case Kind::VariableStatement:
		emitVariableStatement(node);
		break;
	case Kind::ExpressionStatement:
		emitExpressionStatement(node);
		break;
	case Kind::IfStatement:
		emitIfStatement(node);
		break;
	case Kind::DoStatement:
		emitDoStatement(node);
		break;
	case Kind::WhileStatement:
		emitWhileStatement(node);
		break;
	case Kind::ForStatement:
		emitForStatement(node);
		break;
	case Kind::ForInStatement:
		emitForInStatement(node);
		break;
	case Kind::ForOfStatement:
		emitForOfStatement(node);
		break;
	case Kind::ContinueStatement:
		emitContinueStatement(node);
		break;
	case Kind::BreakStatement:
		emitBreakStatement(node);
		break;
	case Kind::ReturnStatement:
		emitReturnStatement(node);
		break;
	case Kind::WithStatement:
		emitWithStatement(node);
		break;
	case Kind::SwitchStatement:
		emitSwitchStatement(node);
		break;
	case Kind::LabeledStatement:
		emitLabeledStatement(node);
		break;
	case Kind::ThrowStatement:
		emitThrowStatement(node);
		break;
	case Kind::TryStatement:
		emitTryStatement(node);
		break;
	case Kind::DebuggerStatement:
		emitDebuggerStatement(node);
		break;
	case Kind::NotEmittedStatement:
		emitNotEmittedStatement(node);
		break;

	// Declaration Statements
	case Kind::FunctionDeclaration:
		emitFunctionDeclaration(node);
		break;
	case Kind::ClassDeclaration:
		emitClassDeclaration(node);
		break;
	case Kind::InterfaceDeclaration:
		emitInterfaceDeclaration(node);
		break;
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
		emitTypeAliasDeclaration(node);
		break;
	case Kind::EnumDeclaration:
		emitEnumDeclaration(node);
		break;
	case Kind::ModuleDeclaration:
		emitModuleDeclaration(node);
		break;
	case Kind::MissingDeclaration:
		// Missing declarations do not emit a statement.
		break;

	// Import/Export Statements
	case Kind::NamespaceExportDeclaration:
		emitNamespaceExportDeclaration(node);
		break;
	case Kind::ImportEqualsDeclaration:
		emitImportEqualsDeclaration(node);
		break;
	case Kind::ImportDeclaration:
		emitImportDeclaration(node);
		break;
	case Kind::ExportAssignment:
		emitExportAssignment(node);
		break;
	case Kind::ExportDeclaration:
		emitExportDeclaration(node);
		break;

	default:
		printerPanic("unhandled statement: ", kindName(node->kind));
	}
}

//
// Module references — printer.go:4239
//

void Printer::emitExternalModuleReference(Node* node) {
	printerState state = enterNode(node);
	writeKeyword("require");
	writePunctuation("(");
	emitExpression(node->as<ExternalModuleReference>()->Expression,
	               OperatorPrecedenceDisallowComma);
	writePunctuation(")");
	exitNode(node, state);
}

//
// JSX — printer.go:4252
//

void Printer::emitJsxElement(Node* node) {
	auto* n = node->as<JsxElement>();
	printerState state = enterNode(node);
	emitJsxOpeningElement(n->OpeningElement);
	emitList(&Printer::emitJsxChild, node, n->Children,
	         LFJsxElementOrFragmentChildren);
	emitJsxClosingElement(n->ClosingElement);
	exitNode(node, state);
}

void Printer::emitJsxSelfClosingElement(Node* node) {
	auto* n = node->as<JsxSelfClosingElement>();
	printerState state = enterNode(node);
	writePunctuation("<");
	emitJsxTagName(n->TagName);
	emitTypeArguments(node, n->TypeArguments);
	writeSpace();
	emitJsxAttributes(n->Attributes);
	writePunctuation("/>");
	exitNode(node, state);
}

void Printer::emitJsxFragment(Node* node) {
	auto* n = node->as<JsxFragment>();
	printerState state = enterNode(node);
	emitJsxOpeningFragment(n->OpeningFragment);
	emitList(&Printer::emitJsxChild, node, n->Children,
	         LFJsxElementOrFragmentChildren);
	emitJsxClosingFragment(n->ClosingFragment);
	exitNode(node, state);
}

void Printer::emitJsxOpeningElement(Node* node) {
	auto* n = node->as<JsxOpeningElement>();
	printerState state = enterNode(node);
	writePunctuation("<");
	bool indented =
	    writeLineSeparatorsAndIndentBefore(n->TagName, node);
	emitJsxTagName(n->TagName);
	emitTypeArguments(node, n->TypeArguments);
	if (!n->Attributes->as<JsxAttributes>()->Properties->nodes.empty()) {
		writeSpace();
	}
	emitJsxAttributes(n->Attributes);
	writeLineSeparatorsAfter(n->Attributes, node);
	decreaseIndentIf(indented);
	writePunctuation(">");
	exitNode(node, state);
}

void Printer::emitJsxClosingElement(Node* node) {
	printerState state = enterNode(node);
	writePunctuation("</");
	emitJsxTagName(node->as<JsxClosingElement>()->TagName);
	writePunctuation(">");
	exitNode(node, state);
}

void Printer::emitJsxOpeningFragment(Node* node) {
	printerState state = enterNode(node);
	writePunctuation("<");
	writePunctuation(">");
	exitNode(node, state);
}

void Printer::emitJsxClosingFragment(Node* node) {
	printerState state = enterNode(node);
	writePunctuation("</");
	writePunctuation(">");
	exitNode(node, state);
}

void Printer::emitJsxText(Node* node) {
	printerState state = enterNode(node);
	// TODO(rbuckton): Should this be using `getLiteralTextOfNode` instead?
	writeLiteral(node->as<JsxText>()->Text);
	exitNode(node, state);
}

void Printer::emitJsxAttributes(Node* node) {
	auto* n = node->as<JsxAttributes>();
	printerState state = enterNode(node);
	emitList(&Printer::emitJsxAttributeLike, node, n->Properties,
	         LFJsxElementAttributes);
	exitNode(node, state);
}

void Printer::emitJsxAttribute(Node* node) {
	auto* n = node->as<JsxAttribute>();
	printerState state = enterNode(node);
	emitJsxAttributeName(n->name);
	if (n->Initializer != nullptr) {
		writePunctuation("=");
		emitJsxAttributeValue(n->Initializer);
	}
	exitNode(node, state);
}

void Printer::emitJsxSpreadAttribute(Node* node) {
	auto* n = node->as<JsxSpreadAttribute>();
	printerState state = enterNode(node);
	writePunctuation("{...");
	emitExpression(n->Expression, OperatorPrecedenceLowest);
	writePunctuation("}");
	exitNode(node, state);
}

void Printer::emitJsxAttributeLike(Node* node) {
	switch (node->kind) {
	case Kind::JsxAttribute:
		emitJsxAttribute(node);
		break;
	case Kind::JsxSpreadAttribute:
		emitJsxSpreadAttribute(node);
		break;
	default:
		printerPanic("unhandled JsxAttributeLike: ", kindName(node->kind));
	}
}

void Printer::emitJsxExpression(Node* node) {
	auto* n = node->as<JsxExpression>();
	printerState state = enterNode(node);
	if (n->Expression != nullptr ||
	    (!commentsDisabled && !nodeIsSynthesized(node) &&
	     hasCommentsAtPosition(node->pos()))) {
		// preserve empty expressions if they contain comments!
		bool indented = currentSourceFile != nullptr &&
		                !nodeIsSynthesized(node) &&
		                GetLinesBetweenPositions(currentSourceFile,
		                                         node->pos(), node->end()) !=
		                    0;
		increaseIndentIf(indented);
		TextPos end = emitToken(Kind::OpenBraceToken, node->pos(),
		                        WriteKind::Punctuation, node);
		emitTokenNode(n->DotDotDotToken);
		if (n->Expression != nullptr) {
			emitExpression(n->Expression, OperatorPrecedenceDisallowComma);
		}
		emitToken(Kind::CloseBraceToken,
		          greatestEnd(end, n->Expression, n->DotDotDotToken),
		          WriteKind::Punctuation, node);
		decreaseIndentIf(indented);
	}
	exitNode(node, state);
}

void Printer::emitJsxNamespacedName(Node* node) {
	auto* n = node->as<JsxNamespacedName>();
	printerState state = enterNode(node);
	emitIdentifierName(n->Namespace);
	writePunctuation(":");
	emitIdentifierName(n->name);
	exitNode(node, state);
}

void Printer::emitJsxChild(Node* node) {
	switch (node->kind) {
	case Kind::JsxText:
		emitJsxText(node);
		break;
	case Kind::JsxExpression:
		emitJsxExpression(node);
		break;
	case Kind::JsxElement:
		emitJsxElement(node);
		break;
	case Kind::JsxSelfClosingElement:
		emitJsxSelfClosingElement(node);
		break;
	case Kind::JsxFragment:
		emitJsxFragment(node);
		break;
	default:
		printerPanic("unhandled JsxChild: ", kindName(node->kind));
	}
}

void Printer::emitJsxTagName(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
		emitIdentifierReference(node);
		break;
	case Kind::ThisKeyword:
		emitKeywordExpression(node);
		break;
	case Kind::JsxNamespacedName:
		emitJsxNamespacedName(node);
		break;
	case Kind::PropertyAccessExpression:
		emitPropertyAccessExpression(node);
		break;
	default:
		printerPanic("unhandled JsxTagName: ", kindName(node->kind));
	}
}

void Printer::emitJsxAttributeName(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
		emitIdentifierName(node);
		break;
	case Kind::JsxNamespacedName:
		emitJsxNamespacedName(node);
		break;
	default:
		printerPanic("unhandled JsxAttributeName: ", kindName(node->kind));
	}
}

void Printer::emitJsxAttributeValue(Node* node) {
	switch (node->kind) {
	case Kind::StringLiteral:
		emitStringLiteral(node);
		break;
	case Kind::JsxExpression:
		emitJsxExpression(node);
		break;
	case Kind::JsxElement:
		emitJsxElement(node);
		break;
	case Kind::JsxSelfClosingElement:
		emitJsxSelfClosingElement(node);
		break;
	case Kind::JsxFragment:
		emitJsxFragment(node);
		break;
	default:
		emitExpression(node, OperatorPrecedenceLowest);
	}
}

//
// Clauses — printer.go:4447
//

void Printer::emitCaseOrDefaultClauseStatements(Node* node,
                                                TextPos colonPos) {
	auto* n = node->as<CaseOrDefaultClause>();
	bool emitAsSingleStatement =
	    n->Statements->nodes.size() == 1 &&
	    // treat synthesized nodes as located on the same line for emit
	    // purposes
	    (currentSourceFile == nullptr || nodeIsSynthesized(node) ||
	     nodeIsSynthesized(n->Statements->nodes[0]) ||
	     RangeStartPositionsAreOnSameLine(
	         node->loc, n->Statements->nodes[0]->loc, currentSourceFile));

	ListFormat format = LFCaseOrDefaultClauseStatements;
	if (emitAsSingleStatement) {
		// When emitting as a single statement, use writeToken (no comments)
		// for the colon to avoid duplicating trailing comments that will be
		// picked up by the statement list.
		writeTokenText(Kind::ColonToken, WriteKind::Punctuation, colonPos);
		writeSpace();
		format &= ~(LFMultiLine | LFIndented);
	} else {
		emitToken(Kind::ColonToken, colonPos, WriteKind::Punctuation, node);
	}

	emitList(&Printer::emitStatement, node, n->Statements, format);
}

void Printer::emitCaseClause(Node* node) {
	auto* n = node->as<CaseOrDefaultClause>();
	printerState state = enterNode(node);
	emitToken(Kind::CaseKeyword, node->pos(), WriteKind::Keyword, node);
	writeSpace();
	emitExpression(n->Expression, OperatorPrecedenceLowest);
	emitCaseOrDefaultClauseStatements(node, n->Expression->end());
	exitNode(node, state);
}

void Printer::emitDefaultClause(Node* node) {
	printerState state = enterNode(node);
	TextPos pos = emitToken(Kind::DefaultKeyword, node->pos(),
	                        WriteKind::Keyword, node);
	emitCaseOrDefaultClauseStatements(node, pos);
	exitNode(node, state);
}

void Printer::emitCaseOrDefaultClauseNode(Node* node) {
	switch (node->kind) {
	case Kind::CaseClause:
		emitCaseClause(node);
		break;
	case Kind::DefaultClause:
		emitDefaultClause(node);
		break;
	default:
		printerPanic("unhandled CaseOrDefaultClause: ",
		             kindName(node->kind));
	}
}

void Printer::emitHeritageClause(Node* node) {
	auto* n = node->as<HeritageClause>();
	printerState state = enterNode(node);
	writeSpace();
	emitToken(n->Token, node->pos(), WriteKind::Keyword, node);
	writeSpace();
	emitList(&Printer::emitHeritageClauseElement, node, n->Types,
	         LFHeritageClauseTypes);
	exitNode(node, state);
}

void Printer::emitHeritageClauseElement(Node* node) {
	switch (node->kind) {
	case Kind::ExpressionWithTypeArguments:
		emitExpressionWithTypeArguments(node);
		break;
	case Kind::TypeReference:
		emitTypeReference(node);
		break;
	default:
		printerPanic("unhandled HeritageClauseElement: ",
		             kindName(node->kind));
	}
}

void Printer::emitHeritageClauseNode(Node* node) { emitHeritageClause(node); }

void Printer::emitCatchClause(Node* node) {
	auto* n = node->as<CatchClause>();
	printerState state = enterNode(node);
	TextPos openParenPos = emitToken(Kind::CatchKeyword, node->pos(),
	                                WriteKind::Keyword, node);
	writeSpace();

	if (n->VariableDeclaration != nullptr) {
		emitToken(Kind::OpenParenToken, openParenPos,
		          WriteKind::Punctuation, node);
		emitVariableDeclaration(n->VariableDeclaration);
		emitToken(Kind::CloseParenToken, n->VariableDeclaration->end(),
		          WriteKind::Punctuation, node);
		writeSpace();
	}

	emitBlock(n->Block);
	exitNode(node, state);
}

//
// Property assignments — printer.go:4540
//

void Printer::emitPropertyAssignment(Node* node) {
	auto* n = node->as<PropertyAssignment>();
	printerState state = enterNode(node);
	emitPropertyName(n->name);
	writePunctuation(":");
	writeSpace();
	// This is to ensure that we emit comment in the following case:
	//      For example:
	//          obj = {
	//              id: /*comment1*/ ()=>void
	//          }
	// "comment1" is not considered to be leading comment for node.initializer
	// but rather a trailing comment on the previous node.
	Node* initializer = n->Initializer;
	if ((emitContext->emitFlags(initializer) & EFNoLeadingComments) == 0) {
		TextRange commentRange = emitContext->commentRange(initializer);
		emitTrailingComments(commentRange.pos(), commentSeparatorAfter);
	}
	emitExpression(initializer, OperatorPrecedenceDisallowComma);
	exitNode(node, state);
}

void Printer::emitShorthandPropertyAssignment(Node* node) {
	auto* n = node->as<ShorthandPropertyAssignment>();
	printerState state = enterNode(node);
	emitPropertyName(n->name);
	if (n->ObjectAssignmentInitializer != nullptr) {
		writeSpace();
		writePunctuation("=");
		writeSpace();
		emitExpression(n->ObjectAssignmentInitializer,
		               OperatorPrecedenceDisallowComma);
	}
	exitNode(node, state);
}

void Printer::emitSpreadAssignment(Node* node) {
	auto* n = node->as<SpreadAssignment>();
	printerState state = enterNode(node);
	if (n->Expression != nullptr) {
		emitToken(Kind::DotDotDotToken, node->pos(), WriteKind::Punctuation,
		          node);
		emitExpression(n->Expression, OperatorPrecedenceDisallowComma);
	}
	exitNode(node, state);
}

//
// Enum — printer.go:4586
//

void Printer::emitEnumMember(Node* node) {
	auto* n = node->as<EnumMember>();
	printerState state = enterNode(node);
	emitPropertyName(n->name);
	emitInitializer(n->Initializer, n->name->end(), node);
	exitNode(node, state);
}

void Printer::emitEnumMemberNode(Node* node) { emitEnumMember(node); }

//
// JSDoc — printer.go:4601
//

void Printer::emitJSDocNode(Node* node) {
	// !!!
	printerPanic("not implemented");
}

//
// Top-level nodes — printer.go:4609
//

void Printer::emitShebangIfNeeded(Node* node) {
	auto* sf = node->as<SourceFile>();
	if (nodeIsSynthesized(node->asNode())) {
		return;
	}
	std::string_view shebang = getShebang(sf->text);
	if (!shebang.empty()) {
		writeComment(std::string(shebang));
		writeLine();
	}
}

int Printer::emitPrologueDirectives(NodeList* statements) {
	size_t i = 0;
	for (Node* statement : statements->nodes) {
		if (isPrologueDirective(statement)) {
			writeLine();
			emitStatement(statement);
			i++;
		} else {
			return static_cast<int>(i);
		}
	}
	return static_cast<int>(statements->nodes.size());
}

bool Printer::emitHelpers(Node* node) {
	bool helpersEmitted = false;
	SourceFile* sourceFile = currentSourceFile;
	bool shouldSkip = Options.NoEmitHelpers ||
	                  (sourceFile != nullptr &&
	                   emitContext->hasRecordedExternalHelpers(sourceFile));
	std::vector<EmitHelper*> helpers = emitContext->getEmitHelpers(node);
	if (!helpers.empty()) {
		std::stable_sort(helpers.begin(), helpers.end(),
		                 [](const EmitHelper* x, const EmitHelper* y) {
			                 return compareEmitHelpers(x, y) < 0;
		                 });
		for (EmitHelper* helper : helpers) {
			if (!helper->Scoped) {
				// Skip the helper if it can be skipped and the noEmitHelpers
				// compiler option is set, or if it can be imported and the
				// importHelpers compiler option is set.
				if (shouldSkip) {
					continue;
				}
			}
			if (helper->TextCallback) {
				writeLines(helper->TextCallback(
				    makeFileLevelOptimisticUniqueName));
			} else {
				writeLines(helper->Text);
			}
			helpersEmitted = true;
		}
	}

	return helpersEmitted;
}

void Printer::emitSourceFile(Node* node_) {
	SourceFile* node = node_->as<SourceFile>();
	SourceFile* savedCurrentSourceFile = currentSourceFile;
	bool savedCommentsDisabled = commentsDisabled;
	currentSourceFile = node;

	writeLine();

	pushNameGenerationScope(node->asNode());
	generateAllNames(node->Statements);

	int index = 0;
	commentState* state = nullptr;
	if (node->ScriptKind != ScriptKind::JSON) {
		emitShebangIfNeeded(node);
		index = emitPrologueDirectives(node->Statements);
		if (!writer->IsAtStartOfLine()) {
			writeLine();
		}
		state = emitDetachedCommentsBeforeStatementList(node->asNode(),
		                                                node->Statements->loc);
		emitHelpers(node->asNode());
		if (node->IsDeclarationFile) {
			emitTripleSlashDirectives(node);
		}
	} else {
		state = emitDetachedCommentsBeforeStatementList(node->asNode(),
		                                                node->Statements->loc);
	}

	// !!! Emit triple-slash directives
	emitListRange(&Printer::emitStatement, node->asNode(), node->Statements,
	              LFMultiLine, index,
	              /*count*/ -1);
	popNameGenerationScope(node->asNode());
	emitDetachedCommentsAfterStatementList(node->asNode(),
	                                       node->Statements->loc, state);
	currentSourceFile = savedCurrentSourceFile;
	commentsDisabled = savedCommentsDisabled;
}

void Printer::emitTripleSlashDirectives(Node* node_) {
	SourceFile* node = node_->as<SourceFile>();
	emitDirective("path", node->ReferencedFiles);
	emitDirective("types", node->TypeReferenceDirectives);
	emitDirective("lib", node->LibReferenceDirectives);
}

void Printer::emitDirective(const std::string& kind,
                            const std::vector<FileReference*>& refs) {
	for (FileReference* ref : refs) {
		std::string resolutionMode;
		if (ref->ResolutionMode != ResolutionMode::None) {
			resolutionMode =
			    "resolution-mode=\"" +
			    std::string(ref->ResolutionMode == ResolutionModeESM
			                    ? "import"
			                    : "require") +
			    "\" ";
		}
		writeComment("/// <reference " + kind + "=\"" + ref->FileName +
		             "\" " + resolutionMode +
		             (ref->Preserve ? "preserve=\"true\" " : "") + "/>");
		writeLine();
	}
}

//
// Lists — printer.go:4724
//

void Printer::emitList(const std::function<void(Printer*, Node*)>& emit,
                       Node* parentNode, NodeList* children,
                       ListFormat format) {
	if (shouldEmitOnMultipleLines(parentNode)) {
		format |= LFPreferNewLine | LFIndented;
	}

	emitListRange(emit, parentNode, children, format,
	              /*start*/ -1, /*count*/ -1);
}

void Printer::emitListRange(const std::function<void(Printer*, Node*)>& emit,
                            Node* parentNode, NodeList* children,
                            ListFormat format, int start, int count) {
	bool isNil = children == nullptr;

	int length = 0;
	if (!isNil) {
		length = static_cast<int>(children->nodes.size());
	}

	if (start < 0) {
		start = 0;
	}

	if (count < 0) {
		count = length - start;
	}

	if (isNil && (format & LFOptionalIfNil) != 0) {
		return;
	}

	bool isEmpty = isNil || start >= length || count <= 0;
	if (isEmpty && (format & LFOptionalIfEmpty) != 0) {
		if (OnBeforeEmitNodeList) {
			OnBeforeEmitNodeList(children);
		}
		if (OnAfterEmitNodeList) {
			OnAfterEmitNodeList(children);
		}
		return;
	}

	if ((format & LFBracketsMask) != 0) {
		writePunctuation(getOpeningBracket(format));
		if (isEmpty && !isNil) {
			emitTrailingComments(children->pos(),
			                     commentSeparatorBefore); // Emit comments
			                                                // within empty
			                                                // lists
		}
	}

	if (OnBeforeEmitNodeList) {
		OnBeforeEmitNodeList(children);
	}

	if (isEmpty) {
		// Write a line terminator if the parent node was multi-line
		if ((format & LFMultiLine) != 0 &&
		    !(Options.PreserveSourceNewlines &&
		      (parentNode == nullptr ||
		       (currentSourceFile != nullptr &&
		        RangeIsOnSingleLine(parentNode->loc, currentSourceFile))))) {
			writeLine();
		} else if ((format & LFSpaceBetweenBraces) != 0 &&
		           (format & LFNoSpaceIfEmpty) == 0) {
			writeSpace();
		}
	} else {
		int end = std::min(start + count, length);

		emitListItems(
		    emit, parentNode,
		    std::vector<Node*>(children->nodes.begin() + start,
		                       children->nodes.begin() + end),
		    format, hasTrailingComma(parentNode, children), children->loc);
	}

	if (OnAfterEmitNodeList) {
		OnAfterEmitNodeList(children);
	}

	if ((format & LFBracketsMask) != 0) {
		if (isEmpty && !isNil) {
			emitLeadingComments(children->end(),
			                    /*elided*/ false); // Emit comments within
			                                       // empty lists
		}
		writePunctuation(getClosingBracket(format));
	}
}

bool Printer::hasTrailingComma(Node* parentNode, NodeList* children) {
	// NodeList.HasTrailingComma() is unreliable on transformed nodes as some
	// nodes may have been removed. In the event we believe we may need to
	// emit a trailing comma, we must first look to the respective node list on
	// the original node first.
	if (!children->hasTrailingComma()) {
		return false;
	}

	Node* originalParent = emitContext->mostOriginal(parentNode);
	if (originalParent == parentNode) {
		// if this node is the original node, we can trust the result
		return true;
	}

	if (originalParent->kind != parentNode->kind) {
		// if the original node is some other kind of node, we cannot
		// correlate the list
		return false;
	}

	// find the respective node list on the original parent
	NodeList* originalList = children;
	switch (originalParent->kind) {
	case Kind::ObjectLiteralExpression:
		originalList = originalParent->propertyList();
		break;
	case Kind::ArrayLiteralExpression:
		originalList = originalParent->elementList();
		break;
	case Kind::CallExpression:
	case Kind::NewExpression:
		if (children == parentNode->typeArgumentList()) {
			originalList = originalParent->typeArgumentList();
		} else if (children == parentNode->argumentList()) {
			originalList = originalParent->argumentList();
		}
		break;
	case Kind::Constructor:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::FunctionType:
	case Kind::ConstructorType:
	case Kind::CallSignature:
	case Kind::ConstructSignature:
		if (children == parentNode->typeParameterList()) {
			originalList = originalParent->typeParameterList();
		} else if (children == parentNode->parameterList()) {
			originalList = originalParent->parameterList();
		}
		break;
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
		if (children == parentNode->typeParameterList()) {
			originalList = originalParent->typeParameterList();
		}
		break;
	case Kind::ObjectBindingPattern:
	case Kind::ArrayBindingPattern:
		if (children == parentNode->elementList()) {
			originalList = originalParent->elementList();
		}
		break;
	case Kind::NamedImports:
	case Kind::NamedExports:
		originalList = originalParent->elementList();
		break;
	case Kind::ImportAttributes:
		originalList =
		    originalParent->as<ImportAttributes>()->Attributes;
		break;
	default:
		break;
	}

	// if we have the original list, we can use it's result.
	if (originalList != nullptr) {
		return originalList->hasTrailingComma();
	}

	return false;
}

void Printer::writeDelimiter(ListFormat format) {
	switch (format & LFDelimitersMask) {
	case LFNone:
		// no delimiter for this format
		break;
	case LFCommaDelimited:
		writePunctuation(",");
		break;
	case LFBarDelimited:
		writeSpace();
		writePunctuation("|");
		break;
	case LFAsteriskDelimited:
		writeSpace();
		writePunctuation("*");
		writeSpace();
		break;
	case LFAmpersandDelimited:
		writeSpace();
		writePunctuation("&");
		break;
	default:
		break;
	}
}

// Emits a list without brackets or raising events.
//
// NOTE: You probably don't want to call this directly and should be using
// `emitList` instead.
void Printer::emitListItems(const std::function<void(Printer*, Node*)>& emit,
                            Node* parentNode,
                            const std::vector<Node*>& children,
                            ListFormat format, bool hasTrailingCommaFlag,
                            TextRange childrenTextRange) {
	// Write the opening line terminator or leading whitespace.
	bool mayEmitInterveningComments =
	    (format & LFNoInterveningComments) == 0;
	bool shouldEmitInterveningComments = mayEmitInterveningComments;

	int leadingLineTerminatorCount = 0;
	if (!children.empty()) {
		leadingLineTerminatorCount =
		    getLeadingLineTerminatorCount(parentNode, children[0], format);
	}
	if (leadingLineTerminatorCount > 0) {
		for (int i = 0; i < leadingLineTerminatorCount; i++) {
			writeLine();
		}
		shouldEmitInterveningComments = false;
	} else if ((format & LFSpaceBetweenBraces) != 0) {
		writeSpace();
	}

	// Increase the indent, if requested.
	if ((format & LFIndented) != 0) {
		increaseIndent();
	}

	TextPos parentEnd = greatestEnd(-1, parentNode);

	// Emit each child.
	Node* previousSibling = nullptr;
	bool shouldDecreaseIndentAfterEmit = false;
	for (Node* child : children) {
		// Write the delimiter if this is not the first node.
		if ((format & LFAsteriskDelimited) != 0) {
			// always write JSDoc in the format "\n *"
			writeLine();
			writeDelimiter(format);
		} else if (previousSibling != nullptr) {
			// i.e
			//      function commentedParameters(
			//          /* Parameter a */
			//          a
			//          /* End of parameter a */ -> this comment isn't
			//          considered to be trailing comment of parameter "a"
			//          due to newline
			//          ,
			if ((format & LFDelimitersMask) != 0 &&
			    previousSibling->end() != parentEnd) {
				if (!commentsDisabled &&
				    shouldEmitTrailingComments(previousSibling)) {
					emitLeadingComments(previousSibling->end(),
					                    /*elided*/ false);
				}
			}

			writeDelimiter(format);

			// Write either a line terminator or whitespace to separate the
			// elements.
			int separatingLineTerminatorCount =
			    getSeparatingLineTerminatorCount(previousSibling, child,
			                                     format);
			if (separatingLineTerminatorCount > 0) {
				// If a synthesized node in a single-line list starts on a
				// new line, we should increase the indent.
				if ((format & (LFLinesMask | LFIndented)) == LFSingleLine) {
					increaseIndent();
					shouldDecreaseIndentAfterEmit = true;
				}

				if (shouldEmitInterveningComments &&
				    (format & LFDelimitersMask) != 0 &&
				    !positionIsSynthesized(child->pos()) &&
				    shouldEmitLeadingComments(child)) {
					TextRange commentRange =
					    emitContext->commentRange(child);
					emitTrailingCommentsOfPosition(
					    commentRange.pos(),
					    (format & LFSpaceBetweenSiblings) != 0,
					    /*forceNoNewline*/ true);
				}

				for (int i = 0; i < separatingLineTerminatorCount; i++) {
					writeLine();
				}

				shouldEmitInterveningComments = false;
			} else if ((format & LFSpaceBetweenSiblings) != 0) {
				writeSpace();
			}
		}

		// Emit this child.
		if (shouldEmitInterveningComments &&
		    shouldEmitLeadingComments(child)) {
			TextRange commentRange = emitContext->commentRange(child);
			emitTrailingCommentsOfPosition(commentRange.pos(),
			                             /*prefixSpace*/ false,
			                             /*forceNoNewline*/ false);
		} else {
			shouldEmitInterveningComments = mayEmitInterveningComments;
		}

		nextListElementPos = child->pos();
		emit(this, child);

		if (shouldDecreaseIndentAfterEmit) {
			decreaseIndent();
			shouldDecreaseIndentAfterEmit = false;
		}

		previousSibling = child;
	}

	// Write a trailing comma, if requested.
	bool skipTrailingComments =
	    commentsDisabled || !shouldEmitTrailingComments(previousSibling);
	bool emitTrailingComma = hasTrailingCommaFlag &&
	                         (format & LFAllowTrailingComma) != 0 &&
	                         (format & LFCommaDelimited) != 0;
	if (emitTrailingComma) {
		if (previousSibling != nullptr && !skipTrailingComments) {
			emitToken(Kind::CommaToken, previousSibling->end(),
			          WriteKind::Punctuation, previousSibling);
		} else {
			writePunctuation(",");
		}
	}

	// Emit any trailing comment of the last element in the list
	// i.e
	//       var array = [...
	//          2
	//          /* end of element 2 */
	//       ];
	if (previousSibling != nullptr && parentEnd != previousSibling->end() &&
	    (format & LFDelimitersMask) != 0 && !skipTrailingComments) {
		TextPos commentsPos;
		if (emitTrailingComma && childrenTextRange.end() > 0) {
			commentsPos = childrenTextRange.end();
		} else {
			commentsPos = previousSibling->end();
		}
		emitLeadingComments(commentsPos, /*elided*/ false);
	}

	// Decrease the indent, if requested.
	if ((format & LFIndented) != 0) {
		decreaseIndent();
	}

	// Write the closing line terminator or closing whitespace.
	int closingLineTerminatorCount = getClosingLineTerminatorCount(
	    parentNode, children.empty() ? nullptr : children.back(), format,
	    childrenTextRange);
	if (closingLineTerminatorCount > 0) {
		for (int i = 0; i < closingLineTerminatorCount; i++) {
			writeLine();
		}
	} else if ((format & (LFSpaceAfterList | LFSpaceBetweenBraces)) != 0) {
		writeSpace();
	}
}

//
// emitNode — printer.go:5042
//

std::string Printer::Emit(Node* node, SourceFile* sourceFile) {
	// ensure a reusable writer
	if (ownWriter == nullptr) {
		ownWriter.reset(
		    NewTextWriter(getNewLineCharacter(Options.NewLine), 0));
	}

	Write(node, sourceFile, ownWriter.get(),
	      /*sourceMapGenerator*/ nullptr);
	std::string text = ownWriter->String();

	ownWriter->Clear();
	return text;
}

std::string Printer::EmitSourceFile(SourceFile* sourceFile) {
	return Emit(sourceFile->asNode(), sourceFile);
}

void Printer::setSourceFile(SourceFile* sourceFile) {
	currentSourceFile = sourceFile;
	uniqueHelperNames.clear();
	uniqueHelperNamesSet = false;
	externalHelpersModuleName = nullptr;
	if (sourceFile != nullptr) {
		if ((emitContext->emitFlags(
		         emitContext->mostOriginal(sourceFile->asNode())) &
		     EFExternalHelpers) != 0) {
			uniqueHelperNamesSet = true;
		}
		externalHelpersModuleName =
		    emitContext->getExternalHelpersModuleName(sourceFile);
		sourceFileAdapter.sf = sourceFile;
		setSourceMapSource(&sourceFileAdapter);
	}

	// !!!
}

void Printer::Write(Node* node, SourceFile* sourceFile,
                    EmitTextWriter* writer_,
                    sourcemap::Generator* sourceMapGenerator_) {
	SourceFile* savedCurrentSourceFile = currentSourceFile;
	EmitTextWriter* savedWriter = writer;
	std::unordered_map<std::string, Node*> savedUniqueHelperNames =
	    std::move(uniqueHelperNames);
	bool savedUniqueHelperNamesSet = uniqueHelperNamesSet;
	bool savedSourceMapsDisabled = sourceMapsDisabled;
	sourcemap::Generator* savedSourceMapGenerator = sourceMapGenerator;
	sourcemap::Source* savedSourceMapSource = sourceMapSource;
	sourcemap::SourceIndex savedSourceMapSourceIndex = sourceMapSourceIndex;
	std::unique_ptr<lineCharacterCache> savedSourceMapLineCharCache =
	    std::move(sourceMapLineCharCache);

	sourceMapsDisabled = sourceMapGenerator_ == nullptr;
	sourceMapGenerator = sourceMapGenerator_;
	sourceMapSource = nullptr;
	sourceMapSourceIndex = -1;
	sourceMapLineCharCache = nullptr;

	setSourceFile(sourceFile);
	if (Options.OmitTrailingSemicolon) {
		deferringWriter.reset(getTrailingSemicolonDeferringWriter(writer_));
		writer_ = deferringWriter.get();
	}
	writer = writer_;
	writer->Clear();
	if (sourceFile != nullptr) {
		writer->Grow(sourceFile->text.size());
	}

	switch (node->kind) {
	// Pseudo-literals
	case Kind::TemplateHead:
		emitTemplateHead(node);
		break;
	case Kind::TemplateMiddle:
		emitTemplateMiddle(node);
		break;
	case Kind::TemplateTail:
		emitTemplateTail(node);
		break;

	// Identifiers
	case Kind::Identifier:
		emitIdentifierName(node);
		break;

	// PrivateIdentifiers
	case Kind::PrivateIdentifier:
		emitPrivateIdentifier(node);
		break;

	// Parse tree nodes
	// Names
	case Kind::QualifiedName:
		emitQualifiedName(node);
		break;
	case Kind::ComputedPropertyName:
		emitComputedPropertyName(node);
		break;

	// Signature elements
	case Kind::TypeParameter:
		emitTypeParameter(node);
		break;
	case Kind::Parameter:
		emitParameter(node);
		break;
	case Kind::Decorator:
		emitDecorator(node);
		break;

	// Type members
	case Kind::PropertySignature:
		emitPropertySignature(node);
		break;
	case Kind::PropertyDeclaration:
		emitPropertyDeclaration(node);
		break;
	case Kind::MethodSignature:
		emitMethodSignature(node);
		break;
	case Kind::MethodDeclaration:
		emitMethodDeclaration(node);
		break;
	case Kind::ClassStaticBlockDeclaration:
		emitClassStaticBlockDeclaration(node);
		break;
	case Kind::Constructor:
		emitConstructor(node);
		break;
	case Kind::GetAccessor:
		emitGetAccessorDeclaration(node);
		break;
	case Kind::SetAccessor:
		emitSetAccessorDeclaration(node);
		break;
	case Kind::CallSignature:
		emitCallSignature(node);
		break;
	case Kind::ConstructSignature:
		emitConstructSignature(node);
		break;
	case Kind::IndexSignature:
		emitIndexSignature(node);
		break;

	// Binding patterns
	case Kind::ObjectBindingPattern:
		emitObjectBindingPattern(node);
		break;
	case Kind::ArrayBindingPattern:
		emitArrayBindingPattern(node);
		break;
	case Kind::BindingElement:
		emitBindingElement(node);
		break;

	// Misc
	case Kind::TemplateSpan:
		emitTemplateSpan(node);
		break;
	case Kind::SemicolonClassElement:
		emitSemicolonClassElement(node);
		break;

	// Declarations (non-statement)
	case Kind::VariableDeclaration:
		emitVariableDeclaration(node);
		break;
	case Kind::VariableDeclarationList:
		emitVariableDeclarationList(node);
		break;
	case Kind::ModuleBlock:
		emitModuleBlock(node);
		break;
	case Kind::CaseBlock:
		emitCaseBlock(node);
		break;
	case Kind::ImportClause:
		emitImportClause(node);
		break;
	case Kind::NamespaceImport:
		emitNamespaceImport(node);
		break;
	case Kind::NamespaceExport:
		emitNamespaceExport(node);
		break;
	case Kind::NamedImports:
		emitNamedImports(node);
		break;
	case Kind::ImportSpecifier:
		emitImportSpecifier(node);
		break;
	case Kind::NamedExports:
		emitNamedExports(node);
		break;
	case Kind::ExportSpecifier:
		emitExportSpecifier(node);
		break;
	case Kind::ImportAttributes:
		emitImportAttributes(node);
		break;
	case Kind::ImportAttribute:
		emitImportAttribute(node);
		break;

	// Module references
	case Kind::ExternalModuleReference:
		emitExternalModuleReference(node);
		break;

	// JSX (non-expression)
	case Kind::JsxText:
		emitJsxText(node);
		break;
	case Kind::JsxOpeningElement:
		emitJsxOpeningElement(node);
		break;
	case Kind::JsxOpeningFragment:
		emitJsxOpeningFragment(node);
		break;
	case Kind::JsxClosingElement:
		emitJsxClosingElement(node);
		break;
	case Kind::JsxClosingFragment:
		emitJsxClosingFragment(node);
		break;
	case Kind::JsxAttribute:
		emitJsxAttribute(node);
		break;
	case Kind::JsxAttributes:
		emitJsxAttributes(node);
		break;
	case Kind::JsxSpreadAttribute:
		emitJsxSpreadAttribute(node);
		break;
	case Kind::JsxExpression:
		emitJsxExpression(node);
		break;
	case Kind::JsxNamespacedName:
		emitJsxNamespacedName(node);
		break;

	// Clauses
	case Kind::CaseClause:
		emitCaseClause(node);
		break;
	case Kind::DefaultClause:
		emitDefaultClause(node);
		break;
	case Kind::HeritageClause:
		emitHeritageClause(node);
		break;
	case Kind::CatchClause:
		emitCatchClause(node);
		break;

	// Property assignments
	case Kind::PropertyAssignment:
		emitPropertyAssignment(node);
		break;
	case Kind::ShorthandPropertyAssignment:
		emitShorthandPropertyAssignment(node);
		break;
	case Kind::SpreadAssignment:
		emitSpreadAssignment(node);
		break;

	// Enum
	case Kind::EnumMember:
		emitEnumMember(node);
		break;

	// Top-level nodes
	case Kind::SourceFile:
		emitSourceFile(node);
		break;

	// Transformation nodes
	case Kind::NotEmittedTypeElement:
		emitNotEmittedTypeElement(node);
		break;

	default:
		if (isTypeNode(node)) {
			emitTypeNodeOutsideExtends(node);
		} else if (isStatement(node)) {
			emitStatement(node);
		} else if (isExpression(node)) {
			emitExpression(node, OperatorPrecedenceLowest);
		} else if (isKeywordKind(node->kind)) {
			emitKeywordNode(node);
		} else if (isPunctuationKind(node->kind)) {
			emitPunctuationNode(node);
		} else if (isJSDocKind(node->kind)) {
			emitJSDocNode(node);
		} else {
			printerPanic("unhandled Node: ", kindName(node->kind));
		}
		break;
	}

	currentSourceFile = savedCurrentSourceFile;
	writer = savedWriter;
	uniqueHelperNames = std::move(savedUniqueHelperNames);
	uniqueHelperNamesSet = savedUniqueHelperNamesSet;
	sourceMapsDisabled = savedSourceMapsDisabled;
	sourceMapGenerator = savedSourceMapGenerator;
	sourceMapSource = savedSourceMapSource;
	sourceMapSourceIndex = savedSourceMapSourceIndex;
	sourceMapLineCharCache = std::move(savedSourceMapLineCharCache);
	deferringWriter = nullptr;
}

//
// Comments — printer.go:5289
//

commentState* Printer::emitCommentsBeforeNode(Node* node) {
	if (!shouldEmitComments(node)) {
		return nullptr;
	}

	EmitFlags emitFlags = emitContext->emitFlags(node);
	TextRange commentRange = emitContext->commentRange(node);
	TextPos containerPos_ = containerPos;
	TextPos containerEnd_ = containerEnd;
	TextPos declarationListContainerEnd_ = declarationListContainerEnd;

	// Emit leading comments
	emitLeadingCommentsOfNode(node, emitFlags, commentRange);
	emitLeadingSyntheticCommentsOfNode(node, emitFlags);
	if ((emitFlags & EFNoNestedComments) != 0) {
		commentsDisabled = true;
	}

	commentStateArena.emplace_back(commentState{
	    emitFlags, commentRange, containerPos_, containerEnd_,
	    declarationListContainerEnd_});
	return &commentStateArena.back();
}

void Printer::emitCommentsAfterNode(Node* node, commentState* state) {
	if (state == nullptr) {
		return;
	}

	EmitFlags emitFlags = state->emitFlags;
	TextRange commentRange = state->commentRange;
	TextPos containerPos_ = state->containerPos;
	TextPos containerEnd_ = state->containerEnd;
	TextPos declarationListContainerEnd_ = state->declarationListContainerEnd;

	// Emit trailing comments
	if ((emitFlags & EFNoNestedComments) != 0) {
		commentsDisabled = false;
	}

	emitTrailingSyntheticCommentsOfNode(node, emitFlags);
	emitTrailingCommentsOfNode(node, emitFlags, commentRange, containerPos_,
	                           containerEnd_, declarationListContainerEnd_);

	// Preserve comments from erased type annotation
	if (Node* typeNode = emitContext->getTypeNode(node);
	    typeNode != nullptr) {
		emitTrailingCommentsOfNode(node, emitFlags, typeNode->loc,
		                           containerPos_, containerEnd_,
		                           declarationListContainerEnd_);
	}
}

std::pair<commentState*, TextPos> Printer::emitCommentsBeforeToken(
    Kind token, TextPos pos, Node* contextNode, tokenEmitFlags flags) {
	if ((flags & tefNoComments) != 0 || commentsDisabled) {
		// Still skip trivia so that the returned pos correctly identifies the
		// token position. This is needed for trailing source map positions
		// (writeTokenText advances pos by token length).
		if (currentSourceFile != nullptr && !positionIsSynthesized(pos)) {
			pos = skipTrivia(currentSourceFile->text, pos);
		}
		return {nullptr, pos};
	}

	TextPos startPos = pos;
	if (currentSourceFile != nullptr) {
		pos = skipTrivia(currentSourceFile->text, startPos);
	}

	Node* node = emitContext->parseNode(contextNode);
	bool isSimilarNode = node != nullptr && node->kind == contextNode->kind;
	if (!isSimilarNode) {
		return {nullptr, pos};
	}

	if (contextNode->pos() != startPos) {
		bool indentLeading = (flags & tefIndentLeadingComments) != 0;
		bool needsIndent = indentLeading && currentSourceFile != nullptr &&
		                   !PositionsAreOnSameLine(startPos, pos,
		                                           currentSourceFile);
		increaseIndentIf(needsIndent);
		emitLeadingComments(startPos, /*elided*/ false);
		decreaseIndentIf(needsIndent);
	}

	commentStateArena.emplace_back();
	return {&commentStateArena.back(), pos};
}

void Printer::emitCommentsAfterToken(Kind token, TextPos pos,
                                     Node* contextNode, commentState* state) {
	if (state == nullptr) {
		return;
	}

	if (contextNode->end() != pos) {
		bool isJsxExprContext = contextNode->kind == Kind::JsxExpression;
		emitTrailingComments(pos, isJsxExprContext ? commentSeparatorNone
		                                         : commentSeparatorBefore);
	}
}

commentState* Printer::emitDetachedCommentsBeforeStatementList(
    Node* node, TextRange detachedRange) {
	if (!shouldEmitDetachedComments(node)) {
		return nullptr;
	}

	EmitFlags emitFlags = emitContext->emitFlags(node);
	TextPos containerPos_ = containerPos;
	TextPos containerEnd_ = containerEnd;
	TextPos declarationListContainerEnd_ = declarationListContainerEnd;
	bool skipLeadingComments =
	    positionIsSynthesized(detachedRange.pos()) ||
	    (emitFlags & EFNoLeadingComments) != 0;

	if (!skipLeadingComments) {
		emitDetachedCommentsAndUpdateCommentsInfo(detachedRange);
	}

	if ((emitFlags & EFNoNestedComments) != 0) {
		commentsDisabled = true;
	}

	commentStateArena.emplace_back(commentState{
	    emitFlags, detachedRange, containerPos_, containerEnd_,
	    declarationListContainerEnd_});
	return &commentStateArena.back();
}

void Printer::emitDetachedCommentsAfterStatementList(
    Node* node, TextRange detachedRange, commentState* state) {
	if (state == nullptr) {
		return;
	}

	EmitFlags emitFlags = state->emitFlags;
	bool skipTrailingComments = commentsDisabled ||
	                            positionIsSynthesized(detachedRange.end()) ||
	                            (emitFlags & EFNoTrailingComments) != 0;

	if (!skipTrailingComments) {
		bool hasWrittenComment =
		    emitLeadingComments(detachedRange.end(), /*elided*/ false);
		if (hasWrittenComment && !writer->IsAtStartOfLine()) {
			writeLine();
		}
	}
}

void Printer::emitLeadingCommentsOfNode(Node* node, EmitFlags emitFlags,
                                        TextRange commentRange) {
	TextPos pos = commentRange.pos();
	TextPos end = commentRange.end();

	// Save current container state on the stack.
	if ((!positionIsSynthesized(pos) || !positionIsSynthesized(end)) &&
	    pos != end) {
		// We have to explicitly check that the node is JsxText because if the
		// compilerOptions.jsx is "preserve" we will not do any
		// transformation. It is expensive to walk entire tree just to set
		// one kind of node to have no comments.
		bool skipLeadingComments =
		    positionIsSynthesized(pos) ||
		    (emitFlags & EFNoLeadingComments) != 0 ||
		    node->kind == Kind::JsxText;
		bool skipTrailingComments =
		    positionIsSynthesized(end) ||
		    (emitFlags & EFNoTrailingComments) != 0 ||
		    node->kind == Kind::JsxText;

		// Emit leading comments if the position is not synthesized and the
		// node has not opted out from emitting leading comments.
		if (!skipLeadingComments) {
			emitLeadingComments(pos,
			                    node->kind == Kind::NotEmittedStatement);
		}

		if (!skipLeadingComments ||
		    (pos >= 0 && (emitFlags & EFNoLeadingComments) != 0)) {
			// Advance the container position if comments get emitted or if
			// they've been disabled explicitly using NoLeadingComments.
			containerPos = pos;
		}

		if (!skipTrailingComments ||
		    (end >= 0 && (emitFlags & EFNoTrailingComments) != 0)) {
			// Advance the container end if comments get emitted or if
			// they've been disabled explicitly using NoTrailingComments.
			containerEnd = end;

			// To avoid invalid comment emit in a down-level binding pattern,
			// we keep track of the last declaration list container's end
			if (node->kind == Kind::VariableDeclarationList) {
				declarationListContainerEnd = end;
			}
		}
	}
}

void Printer::emitTrailingCommentsOfNode(
    Node* node, EmitFlags emitFlags, TextRange commentRange,
    TextPos containerPos_, TextPos containerEnd_,
    TextPos declarationListContainerEnd_) {
	TextPos pos = commentRange.pos();
	TextPos end = commentRange.end();
	bool skipTrailingComments = end < 0 ||
	                            (emitFlags & EFNoTrailingComments) != 0 ||
	                            node->kind == Kind::JsxText;
	if ((!positionIsSynthesized(pos) || !positionIsSynthesized(end)) &&
	    pos != end) {
		// Restore previous container state.
		containerPos = containerPos_;
		containerEnd = containerEnd_;
		declarationListContainerEnd = declarationListContainerEnd_;

		// Emit trailing comments if the position is not synthesized and the
		// node has not opted out from emitting leading comments and is an
		// emitted node.
		if (!skipTrailingComments &&
		    node->kind != Kind::NotEmittedStatement) {
			emitTrailingComments(end, commentSeparatorBefore);
		}
	}
}

void Printer::emitLeadingSyntheticCommentsOfNode(Node* node,
                                                 EmitFlags emitFlags) {
	if ((emitFlags & EFNoLeadingComments) != 0) {
		return;
	}
	for (const SynthesizedComment& c :
	     emitContext->getSyntheticLeadingComments(node)) {
		emitLeadingSynthesizedComment(c);
	}
}

void Printer::emitLeadingSynthesizedComment(
    const SynthesizedComment& comment) {
	if (comment.HasLeadingNewLine ||
	    comment.Kind == Kind::SingleLineCommentTrivia) {
		writer->WriteLine();
	}
	writeSynthesizedComment(comment);
	if (comment.HasTrailingNewLine ||
	    comment.Kind == Kind::SingleLineCommentTrivia) {
		writer->WriteLine();
	} else {
		writer->WriteSpace(" ");
	}
}

void Printer::emitTrailingSyntheticCommentsOfNode(Node* node,
                                                  EmitFlags emitFlags) {
	if ((emitFlags & EFNoTrailingComments) != 0) {
		return;
	}
	for (const SynthesizedComment& c :
	     emitContext->getSyntheticTrailingComments(node)) {
		emitTrailingSynthesizedComment(c);
	}
}

void Printer::emitTrailingSynthesizedComment(
    const SynthesizedComment& comment) {
	if (!writer->IsAtStartOfLine()) {
		writer->WriteSpace(" ");
	}
	writeSynthesizedComment(comment);
	if (comment.HasTrailingNewLine) {
		writer->WriteLine();
	}
}

std::string Printer::formatSynthesizedComment(const SynthesizedComment& comment) {
	if (comment.Kind == Kind::MultiLineCommentTrivia) {
		return "/*" + comment.Text + "*/";
	}
	return "//" + comment.Text;
}

void Printer::writeSynthesizedComment(const SynthesizedComment& comment) {
	std::string text = formatSynthesizedComment(comment);
	std::vector<TextPos> lineMap;
	if (comment.Kind == Kind::MultiLineCommentTrivia) {
		lineMap = computeECMALineStarts(text);
	}
	writeCommentRangeWorker(text, lineMap, comment.Kind,
	                        newTextRange(0, static_cast<int>(text.size())));
}

bool Printer::emitLeadingComments(TextPos pos, bool elided) {
	// Emit the leading comments only if the container's pos doesn't match
	// because the container should take care of emitting these comments
	if (commentsDisabled || currentSourceFile == nullptr ||
	    positionIsSynthesized(pos) || pos == containerPos) {
		return false;
	}

	Tristate tripleSlash = Tristate::Unknown;
	if (!elided) {
		if (pos == 0 && currentSourceFile != nullptr &&
		    currentSourceFile->IsDeclarationFile) {
			tripleSlash = Tristate::False;
		}
	} else if (pos == 0) {
		// If the node will not be emitted in JS, remove all the
		// comments(normal, pinned and ///) associated with the node, unless it
		// is a triple slash comment at the top of the file. For Example:
		//      /// <reference-path ...>
		//      declare var x;
		//      /// <reference-path ...>
		//      interface F {}
		//  The first /// will NOT be removed while the second one will be
		//  removed even though both node will not be emitted
		tripleSlash = Tristate::True;
	} else {
		return false;
	}

	// skip detached comments
	if (detachedCommentsInfoStack.len() > 0) {
		if (detachedCommentsInfo* infoPtr = detachedCommentsInfoStack.peek();
		    infoPtr->nodePos == pos) {
			pos = detachedCommentsInfoStack.pop().detachedCommentEndPos;
		}
	}

	std::vector<CommentRange> comments;
	for (const CommentRange& comment : collectLeadingCommentRanges(currentSourceFile->text, pos)) {
		if (shouldWriteComment(comment) &&
		    shouldEmitCommentIfTripleSlash(comment, tripleSlash)) {
			comments.push_back(comment);
		}
	}

	if (!comments.empty() &&
	    shouldEmitNewLineBeforeLeadingCommentOfPosition(pos,
	                                                    comments[0].pos())) {
		writeLine();
	}

	// Leading comments are emitted as
	// /*leading comment1*/space/*leading comment*/space
	return emitComments(comments, commentSeparatorAfter);
}

bool Printer::shouldEmitCommentIfTripleSlash(CommentRange comment,
                                             Tristate tripleSlash) {
	switch (tripleSlash) {
	case Tristate::True:
		return isTripleSlashComment(comment);
	case Tristate::False:
		return !isTripleSlashComment(comment);
	default:
		return true;
	}
}

bool Printer::shouldEmitNewLineBeforeLeadingCommentOfPosition(
    TextPos pos, TextPos commentPos) {
	// If the leading comments start on different line than the start of
	// node, write new line
	return currentSourceFile != nullptr && pos != commentPos &&
	       computeLineOfPosition(
	           currentSourceFile->ecmaLineMap(), pos) !=
	           computeLineOfPosition(
	               currentSourceFile->ecmaLineMap(), commentPos);
}

void Printer::emitLeadingCommentsOfPosition(TextPos pos) {
	if (commentsDisabled || pos == -1) {
		return;
	}

	emitLeadingComments(pos, /*elided*/ false);
}

void Printer::emitTrailingComments(TextPos pos, commentSeparator separator) {
	if (commentsDisabled) {
		return;
	}
	// Emit the trailing comments only if the container's end doesn't match
	// because the container should take care of emitting these comments
	if (commentsDisabled || currentSourceFile == nullptr ||
	    (containerEnd != -1 && (pos == containerEnd ||
	                            pos == declarationListContainerEnd))) {
		return;
	}

	std::vector<CommentRange> comments;
	for (const CommentRange& comment : collectTrailingCommentRanges(currentSourceFile->text, pos)) {
		if (shouldWriteComment(comment)) {
			comments.push_back(comment);
		}
	}

	// trailing comments are normally emitted as space/*trailing
	// comment1*/space/*trailing comment2*/
	emitComments(comments, separator);
}

void Printer::emitTrailingCommentsOfPosition(TextPos pos, bool prefixSpace,
                                             bool forceNoNewline) {
	if (commentsDisabled || currentSourceFile == nullptr) {
		return;
	}
	if (containerEnd != -1 &&
	    (pos == containerEnd || pos == declarationListContainerEnd)) {
		return;
	}

	std::vector<CommentRange> comments;
	for (const CommentRange& comment : collectTrailingCommentRanges(currentSourceFile->text, pos)) {
		comments.push_back(comment);
	}
	if (comments.empty()) {
		return;
	}

	for (const CommentRange& comment : comments) {
		if (prefixSpace) {
			if (!shouldWriteComment(comment)) {
				continue;
			}
			if (!writer->IsAtStartOfLine()) {
				writeSpace();
			}
			emitComment(comment);
			if (comment.HasTrailingNewLine) {
				writeLine();
			}
			continue;
		}

		emitComment(comment);
		if (forceNoNewline) {
			if (comment.kind == Kind::SingleLineCommentTrivia) {
				writeLine();
			}
		} else if (comment.HasTrailingNewLine) {
			writeLine();
		} else {
			writeSpace();
		}
	}
}

void Printer::emitDetachedCommentsAndUpdateCommentsInfo(
    TextRange textRange) {
	if (currentSourceFile == nullptr) {
		return;
	}
	auto [currentDetachedCommentInfo, ok] = emitDetachedComments(textRange);
	if (ok) {
		detachedCommentsInfoStack.push(currentDetachedCommentInfo);
	}
}

std::pair<detachedCommentsInfo, bool> Printer::emitDetachedComments(
    TextRange textRange) {
	detachedCommentsInfo result;
	if (currentSourceFile == nullptr) {
		return {result, false};
	}

	const std::string& text = currentSourceFile->text;
	const std::vector<TextPos>& lineMap = currentSourceFile->ecmaLineMap();

	std::vector<CommentRange> leadingComments;
	if (commentsDisabled) {
		// removeComments is true, only reserve pinned comment at the top of
		// file. For example:
		//      /*! Pinned Comment */
		//
		//      var x = 10;
		if (textRange.pos() == 0) {
			for (const CommentRange& comment :
			     collectLeadingCommentRanges(text, textRange.pos())) {
				if (IsPinnedComment(text, comment)) {
					leadingComments.push_back(comment);
				}
			}
		}
	} else {
		// removeComments is false, just get detached as normal and bypass
		// the process to filter comment
		for (const CommentRange& comment :
		     collectLeadingCommentRanges(text, textRange.pos())) {
			leadingComments.push_back(comment);
		}
	}

	if (!leadingComments.empty()) {
		std::vector<CommentRange> detachedComments;
		CommentRange lastComment{};
		for (size_t i = 0; i < leadingComments.size(); i++) {
			const CommentRange& comment = leadingComments[i];
			if (i > 0) {
				int lastCommentLine = computeLineOfPosition(
				    lineMap, lastComment.end());
				int commentLine = computeLineOfPosition(
				    lineMap, comment.pos());

				if (commentLine >= lastCommentLine + 2) {
					// There was a blank line between the last comment and
					// this comment. This comment is not part of the
					// copyright comments. Return what we have so far.
					break;
				}
			}

			detachedComments.push_back(comment);
			lastComment = comment;
		}

		if (!detachedComments.empty()) {
			// All comments look like they could have been part of the
			// copyright header. Make sure there is at least one blank line
			// between it and the node. If not, it's not a copyright header.
			int lastCommentLine = computeLineOfPosition(
			    lineMap, detachedComments.back().end());
			int nodeLine = computeLineOfPosition(
			    lineMap, skipTrivia(text, textRange.pos()));
			if (nodeLine >= lastCommentLine + 2) {
				// Valid detachedComments

				// Filter to only comments that should be written (e.g.,
				// JSDoc-style in declaration emit)
				std::vector<CommentRange> commentsToEmit;
				for (const CommentRange& comment : detachedComments) {
					if (shouldWriteComment(comment)) {
						commentsToEmit.push_back(comment);
					}
				}

				if (!commentsToEmit.empty()) {
					if (shouldEmitNewLineBeforeLeadingCommentOfPosition(
					        textRange.pos(), commentsToEmit[0].pos())) {
						writeLine();
					}

					emitComments(commentsToEmit, commentSeparatorAfter);
				}
				result = detachedCommentsInfo{
				    textRange.pos(), detachedComments.back().end()};
				return {result, true};
			}
		}
	}
	return {result, false};
}

bool Printer::emitComments(const std::vector<CommentRange>& comments,
                           commentSeparator separator) {
	bool interveningSeparator = false;
	if (comments.empty()) {
		return false;
	}

	if (separator == commentSeparatorBefore) {
		writeSpace();
	}

	for (const CommentRange& comment : comments) {
		if (interveningSeparator) {
			writeSpace();
			interveningSeparator = false;
		}

		emitComment(comment);

		if (comment.kind == Kind::SingleLineCommentTrivia ||
		    (comment.HasTrailingNewLine &&
		     separator != commentSeparatorNone)) {
			writeLine();
		} else {
			interveningSeparator = separator != commentSeparatorNone;
		}
	}

	if (interveningSeparator && separator == commentSeparatorAfter) {
		writeSpace();
	}

	return true;
}

void Printer::emitComment(CommentRange comment) {
	emitPos(comment.pos());
	writeCommentRange(comment);
	emitPos(comment.end());
}

bool Printer::isTripleSlashComment(CommentRange comment) {
	return currentSourceFile != nullptr &&
	       IsRecognizedTripleSlashComment(currentSourceFile->text, comment);
}

//
// Source Maps — printer.go:5810
//

void Printer::setSourceMapSource(sourcemap::Source* source) {
	if (sourceMapsDisabled) {
		return;
	}

	sourceMapSource = source;
	sourceMapLineCharCache = std::make_unique<lineCharacterCache>(source);
	if (mostRecentSourceMapSource == source) {
		sourceMapSourceIndex = mostRecentSourceMapSourceIndex;
		return;
	}

	sourceMapSourceIsJson =
	    tspath::fileExtensionIs(source->FileName(), tspath::extensionJson);
	if (sourceMapSourceIsJson) {
		return;
	}

	sourceMapSourceIndex = sourceMapGenerator->AddSource(source->FileName());
	if (Options.InlineSources) {
		if (sourceMapGenerator->SetSourceContent(sourceMapSourceIndex,
		                                         source->Text()) != 0) {
			TSC_UNREACHABLE("sourcemap SetSourceContent failed");
		}
	}

	mostRecentSourceMapSource = source;
	mostRecentSourceMapSourceIndex = sourceMapSourceIndex;
}

void Printer::emitPos(TextPos pos) {
	if (sourceMapsDisabled || sourceMapSource == nullptr ||
	    sourceMapGenerator == nullptr || sourceMapSourceIsJson ||
	    positionIsSynthesized(pos)) {
		return;
	}

	sourcemap::Source* source = sourceMapSource;
	sourcemap::SourceIndex sourceIndex = sourceMapSourceIndex;
	lineCharacterCache* lineCharCache = sourceMapLineCharCache.get();
	if (MapSourcePosition) {
		MappedSourcePosition mapped = MapSourcePosition(source, pos);
		if (!mapped.ok) {
			if (sourceMapGenerator->AddGeneratedMapping(writer->GetLine(),
			                                            writer->GetColumn()) !=
			    0) {
				TSC_UNREACHABLE("sourcemap AddGeneratedMapping failed");
			}
			return;
		}
		pos = mapped.pos;
		if (mapped.source != source) {
			sourcemap::Source* savedSource = sourceMapSource;
			sourcemap::SourceIndex savedSourceIndex = sourceMapSourceIndex;
			bool savedSourceIsJson = sourceMapSourceIsJson;
			auto savedLineCharCache = std::move(sourceMapLineCharCache);
			setSourceMapSource(mapped.source);
			sourceIndex = sourceMapSourceIndex;
			lineCharCache = sourceMapLineCharCache.get();
			sourceMapSource = savedSource;
			sourceMapSourceIndex = savedSourceIndex;
			sourceMapSourceIsJson = savedSourceIsJson;
			sourceMapLineCharCache = std::move(savedLineCharCache);
		}
	}

	auto [sourceLine, sourceCharacter] =
	    lineCharCache->getLineAndCharacter(pos);
	if (sourceMapGenerator->AddSourceMapping(writer->GetLine(),
	                                         writer->GetColumn(), sourceIndex,
	                                         sourceLine, sourceCharacter) !=
	    0) {
		TSC_UNREACHABLE("sourcemap AddSourceMapping failed");
	}
}

void Printer::emitSourcePos(sourcemap::Source* source, TextPos pos) {
	if (source != sourceMapSource) {
		sourcemap::Source* savedSourceMapSource = sourceMapSource;
		sourcemap::SourceIndex savedSourceMapSourceIndex = sourceMapSourceIndex;
		auto savedSourceMapLineCharCache =
		    std::move(sourceMapLineCharCache);
		setSourceMapSource(source);
		emitPos(pos);
		sourceMapSource = savedSourceMapSource;
		sourceMapSourceIndex = savedSourceMapSourceIndex;
		sourceMapLineCharCache = std::move(savedSourceMapLineCharCache);
	} else {
		emitPos(pos);
	}
}

sourceMapState* Printer::emitSourceMapsBeforeNode(Node* node) {
	if (!shouldEmitSourceMaps(node)) {
		return nullptr;
	}

	EmitFlags emitFlags = emitContext->emitFlags(node);
	TextRange loc = emitContext->sourceMapRange(node);

	if (!isNotEmittedStatement(node) &&
	    (emitFlags & EFNoLeadingSourceMap) == 0 &&
	    currentSourceFile != nullptr &&
	    !positionIsSynthesized(loc.pos())) {
		emitSourcePos(sourceMapSource,
		              skipTrivia(currentSourceFile->text,
		                                  loc.pos()));
	}

	if ((emitFlags & EFNoNestedSourceMaps) != 0) {
		sourceMapsDisabled = true;
	}

	sourceMapStateArena.emplace_back(sourceMapState{emitFlags, loc, false});
	return &sourceMapStateArena.back();
}

void Printer::emitSourceMapsAfterNode(Node* node,
                                      sourceMapState* previousState) {
	if (previousState == nullptr) {
		return;
	}

	EmitFlags emitFlags = previousState->emitFlags;
	TextRange loc = previousState->sourceMapRange;

	if ((emitFlags & EFNoNestedSourceMaps) != 0) {
		sourceMapsDisabled = false;
	}

	if (!isNotEmittedStatement(node) &&
	    (emitFlags & EFNoTrailingSourceMap) == 0 &&
	    !positionIsSynthesized(loc.end())) {
		emitSourcePos(sourceMapSource, loc.end());
	}
}

sourceMapState* Printer::emitSourceMapsBeforeToken(Kind token, TextPos pos,
                                                   Node* contextNode,
                                                   tokenEmitFlags flags) {
	if (!shouldEmitTokenSourceMaps(token, pos, contextNode, flags)) {
		return nullptr;
	}

	EmitFlags emitFlags = emitContext->emitFlags(contextNode);
	TextRange loc{-1, -1};
	bool hasLoc = false;
	if (auto opt = emitContext->tokenSourceMapRange(contextNode, token);
	    opt.has_value()) {
		loc = *opt;
		hasLoc = true;
	}
	if (hasLoc) {
		pos = loc.pos();
	}
	if (pos >= 0 && currentSourceFile != nullptr) {
		pos = skipTrivia(currentSourceFile->text, pos);
	}
	if ((emitFlags & EFNoTokenLeadingSourceMaps) == 0 && pos >= 0) {
		emitSourcePos(sourceMapSource, pos);
	}

	sourceMapStateArena.emplace_back(
	    sourceMapState{emitFlags, loc, hasLoc});
	return &sourceMapStateArena.back();
}

void Printer::emitSourceMapsAfterToken(Kind token, TextPos pos,
                                       Node* contextNode,
                                       sourceMapState* previousState) {
	if (previousState == nullptr) {
		return;
	}

	EmitFlags emitFlags = previousState->emitFlags;
	TextRange loc = previousState->sourceMapRange;
	bool hasLoc = previousState->hasTokenSourceMapRange;
	if ((emitFlags & EFNoTokenTrailingSourceMaps) == 0) {
		if (hasLoc) {
			pos = loc.end();
		}
		if (pos >= 0) {
			emitSourcePos(sourceMapSource, pos);
		}
	}
}

//
// Name Generation — printer.go:6018
//

bool Printer::shouldReuseTempVariableScope(Node* node) {
	return node != nullptr &&
	       (emitContext->emitFlags(node) & EFReuseTempVariableScope) != 0;
}

void Printer::pushNameGenerationScope(Node* node) {
	nameGenerator.PushScope(shouldReuseTempVariableScope(node));
}

void Printer::popNameGenerationScope(Node* node) {
	nameGenerator.PopScope(shouldReuseTempVariableScope(node));
}

void Printer::generateAllNames(NodeList* nodes) {
	if (nodes == nullptr) {
		return;
	}
	for (Node* node : nodes->nodes) {
		generateNames(node);
	}
}

void Printer::generateNames(Node* node) {
	if (node == nullptr) {
		return;
	}

	switch (node->kind) {
	case Kind::Block:
	case Kind::CaseClause:
	case Kind::DefaultClause:
		generateAllNames(node->statementList());
		break;
	case Kind::LabeledStatement:
	case Kind::WithStatement:
	case Kind::DoStatement:
	case Kind::WhileStatement:
		generateNames(node->statement());
		break;
	case Kind::IfStatement:
		generateNames(node->as<IfStatement>()->ThenStatement);
		generateNames(node->as<IfStatement>()->ElseStatement);
		break;
	case Kind::ForStatement:
	case Kind::ForOfStatement:
	case Kind::ForInStatement:
		generateNames(node->initializer());
		generateNames(node->statement());
		break;
	case Kind::SwitchStatement:
		generateNames(node->as<SwitchStatement>()->CaseBlock);
		break;
	case Kind::CaseBlock:
		generateAllNames(node->as<CaseBlock>()->Clauses);
		break;
	case Kind::TryStatement:
		generateNames(node->as<TryStatement>()->TryBlock);
		generateNames(node->as<TryStatement>()->CatchClause);
		generateNames(node->as<TryStatement>()->FinallyBlock);
		break;
	case Kind::CatchClause:
		generateNames(node->as<CatchClause>()->VariableDeclaration);
		generateNames(node->as<CatchClause>()->Block);
		break;
	case Kind::VariableStatement:
		generateNames(node->as<VariableStatement>()->DeclarationList);
		break;
	case Kind::VariableDeclarationList:
		generateAllNames(node->as<VariableDeclarationList>()->Declarations);
		break;
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::BindingElement:
	case Kind::ClassDeclaration:
		generateNameIfNeeded(node->name());
		break;
	case Kind::FunctionDeclaration: {
		generateNameIfNeeded(node->name());
		if (shouldReuseTempVariableScope(node)) {
			generateAllNames(node->as<FunctionDeclaration>()->Parameters);
			generateNames(node->as<FunctionDeclaration>()->Body);
		}
		break;
	}
	case Kind::ObjectBindingPattern:
	case Kind::ArrayBindingPattern:
		generateAllNames(node->elementList());
		break;
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
		generateNames(node->as<ImportDeclaration>()->ImportClause);
		break;
	case Kind::ImportClause:
		generateNameIfNeeded(node->as<ImportClause>()->name);
		generateNames(node->as<ImportClause>()->NamedBindings);
		break;
	case Kind::NamespaceImport:
	case Kind::NamespaceExport:
		generateNameIfNeeded(node->name());
		break;
	case Kind::NamedImports:
		generateAllNames(node->elementList());
		break;
	case Kind::ImportSpecifier: {
		auto* n = node->as<ImportSpecifier>();
		if (n->PropertyName != nullptr) {
			generateNameIfNeeded(n->PropertyName);
		} else {
			generateNameIfNeeded(n->name);
		}
		break;
	}
	default:
		break;
	}
}

void Printer::generateAllMemberNames(NodeList* nodes) {
	if (nodes == nullptr) {
		return;
	}
	for (Node* node : nodes->nodes) {
		generateMemberNames(node);
	}
}

void Printer::generateMemberNames(Node* node) {
	if (node == nullptr) {
		return;
	}
	switch (node->kind) {
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		generateNameIfNeeded(node->name());
		break;
	default:
		break;
	}
}

void Printer::generateNameIfNeeded(Node* name) {
	if (name != nullptr) {
		if (isMemberName(name)) {
			generateName(name);
		} else if (isBindingPattern(name)) {
			generateNames(name);
		}
	}
}

// Generate the text for a generated identifier or private identifier
void Printer::generateName(Node* name) { (void)nameGenerator.GenerateName(name); }

// Returns a value indicating whether a name is unique globally or within the
// current file.
bool Printer::isFileLevelUniqueNameInCurrentFile(const std::string& name,
                                                 bool /*hasGlobalName*/) {
	if (currentSourceFile != nullptr) {
		return emitContext->isFileLevelUniqueName(currentSourceFile, name,
		                                          HasGlobalName);
	}
	return true;
}

//
// Scoped operations — printer.go:6153
//

printerState Printer::enterNode(Node* node) {
	printerState state;

	if (OnBeforeEmitNode) {
		OnBeforeEmitNode(node);
	}

	state.commentState_ = emitCommentsBeforeNode(node);
	state.sourceMapState_ = emitSourceMapsBeforeNode(node);
	return state;
}

void Printer::exitNode(Node* node, printerState previousState) {
	emitSourceMapsAfterNode(node, previousState.sourceMapState_);
	emitCommentsAfterNode(node, previousState.commentState_);

	if (OnAfterEmitNode) {
		OnAfterEmitNode(node);
	}
}

printerState Printer::enterTokenNode(Node* node, tokenEmitFlags flags) {
	printerState state;

	if (OnBeforeEmitToken) {
		OnBeforeEmitToken(node);
	}

	if ((flags & tefNoComments) == 0) {
		state.commentState_ = emitCommentsBeforeNode(node);
	}
	if ((flags & tefNoSourceMaps) == 0) {
		state.sourceMapState_ = emitSourceMapsBeforeNode(node);
	}
	return state;
}

void Printer::exitTokenNode(Node* node, printerState previousState) {
	emitSourceMapsAfterNode(node, previousState.sourceMapState_);
	emitCommentsAfterNode(node, previousState.commentState_);

	if (OnAfterEmitToken) {
		OnAfterEmitToken(node);
	}
}

std::pair<printerState, TextPos> Printer::enterToken(Kind token, TextPos pos,
                                                   Node* contextNode,
                                                   tokenEmitFlags flags) {
	printerState state;
	auto [cs, newPos] =
	    emitCommentsBeforeToken(token, pos, contextNode, flags);
	state.commentState_ = cs;
	pos = newPos;
	state.sourceMapState_ =
	    emitSourceMapsBeforeToken(token, pos, contextNode, flags);
	return {state, pos};
}

void Printer::exitToken(Kind token, TextPos pos, Node* contextNode,
                        printerState previousState) {
	emitSourceMapsAfterToken(token, pos, contextNode,
	                         previousState.sourceMapState_);
	emitCommentsAfterToken(token, pos, contextNode,
	                       previousState.commentState_);
}

//
// ListFormat — printer.go:6223
//

std::string getOpeningBracket(ListFormat format) {
	switch (format & LFBracketsMask) {
	case LFBraces:
		return "{";
	case LFParenthesis:
		return "(";
	case LFAngleBrackets:
		return "<";
	case LFSquareBrackets:
		return "[";
	default:
		TSC_UNREACHABLE("unexpected bracket");
	}
}

std::string getClosingBracket(ListFormat format) {
	switch (format & LFBracketsMask) {
	case LFBraces:
		return "}";
	case LFParenthesis:
		return ")";
	case LFAngleBrackets:
		return ">";
	case LFSquareBrackets:
		return "]";
	default:
		TSC_UNREACHABLE("unexpected bracket");
	}
}

} // namespace tsc::printer
