// Minimal stand-ins for tsc/internal/printer/printer.go, emittextwriter.go,
// textwriter.go and singlelinestringwriter.go — only the types and entry
// points the checker's printer.go port touches.
//
// === slice: symbolaccess === — the real printer package lands with the
// emitter slice (Stage 4); calls that need it are dep-stubbed in
// checker_printer.cpp. Matching Go names exactly so the real port replaces
// this file wholesale.
#pragma once

#include <functional>
#include <string>
#include <utility>

#include "internal/ast/ast.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/printer/emitcontext.h"

namespace tsc::printer {

// PrinterOptions (printer.go:35).
struct PrinterOptions {
	bool RemoveComments = false;
	NewLineKind NewLine{};
	bool OmitTrailingSemicolon = false;
	bool NoEmitHelpers = false;
	ScriptTarget Target{};
	bool SourceMap = false;
	bool InlineSourceMap = false;
	bool InlineSources = false;
	bool OmitBraceSourceMapPositions = false;
	bool OnlyPrintJSDocStyle = false;
	bool NeverAsciiEscape = false;
	bool PreserveSourceNewlines = false;
	bool TerminateUnterminatedLiterals = false;
};

// PrintHandlers (printer.go:55) — the checker only ever passes the zero value.
struct PrintHandlers {};

// EmitTextWriter (emittextwriter.go:9) — externally opaque writer interface.
struct EmitTextWriter {
	virtual ~EmitTextWriter() = default;
	virtual void Write(const std::string& s) = 0;
	virtual void WriteTrailingSemicolon(const std::string& text) = 0;
	virtual void WriteComment(const std::string& text) = 0;
	virtual void WriteKeyword(const std::string& text) = 0;
	virtual void WriteOperator(const std::string& text) = 0;
	virtual void WritePunctuation(const std::string& text) = 0;
	virtual void WriteSpace(const std::string& text) = 0;
	virtual void WriteStringLiteral(const std::string& text) = 0;
	virtual void WriteParameter(const std::string& text) = 0;
	virtual void WriteProperty(const std::string& text) = 0;
	virtual void WriteSymbol(const std::string& text, Symbol* symbol) = 0;
	virtual void WriteLine() = 0;
	virtual void WriteLineForce(bool force) = 0;
	virtual void IncreaseIndent() = 0;
	virtual void DecreaseIndent() = 0;
	virtual void Clear() = 0;
	virtual std::string String() = 0;
	virtual void RawWrite(const std::string& s) = 0;
	virtual void WriteLiteral(const std::string& s) = 0;
	virtual int GetTextPos() = 0;
	virtual int GetLine() = 0;
	virtual TextPos GetColumn() = 0;
	virtual int GetIndent() = 0;
	virtual bool IsAtStartOfLine() = 0;
	virtual bool HasTrailingComment() = 0;
	virtual bool HasTrailingWhitespace() = 0;
};

// Printer (printer.go:117) — minimal stand-in; Write/Emit are dep-stubbed in
// checker_printer.cpp until the real printer lands.
struct Printer {
	PrinterOptions Options;
	PrintHandlers handlers;
	EmitContext* emitContext = nullptr;

	std::string Emit(Node* node, SourceFile* sourceFile);
	void Write(Node* node, SourceFile* sourceFile, EmitTextWriter* writer,
	           void* sourceMapGenerator);
};

// NewPrinter (printer.go:173) — minimal real construction: only the surface
// the checker reads back is populated.
inline Printer* NewPrinter(const PrinterOptions& options,
                           const PrintHandlers& handlers,
                           EmitContext* emitContext) {
	auto* p = new Printer();
	p->Options = options;
	p->handlers = handlers;
	p->emitContext = emitContext;
	return p;
}

// NewTextWriter (textwriter.go:218) — dep-stubbed.
EmitTextWriter* NewTextWriter(const std::string& newLine, int indentSize);

// GetSingleLineStringWriter (singlelinestringwriter.go:21) — returns the
// pooled writer plus its release func. Dep-stubbed.
std::pair<EmitTextWriter*, std::function<void()>> GetSingleLineStringWriter();

} // namespace tsc::printer
