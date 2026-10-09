// Port of tsc/internal/printer/textwriter.go +
// singlelinestringwriter.go + semicolon_writer.go —
// EmitTextWriter implementations.
#include "internal/printer/printer.h"

#include "internal/core/text.h"
#include "internal/stringutil/stringutil.h"

#include <mutex>

namespace tsc::printer {

// --- textWriter (textwriter.go:14) ---------------------------------------------

namespace {

struct textWriter : EmitTextWriter {
	std::string newLine;
	int indentSize = defaultIndentSize;
	std::string builder;
	std::string lastWritten;
	int indent = 0;
	bool lineStart = true;
	int lineCount = 0;
	int linePos = 0;
	bool hasTrailingCommentState = false;

	static constexpr int defaultIndentSize = 4;

	void Clear() override {
		std::string nl = newLine;
		int is = indentSize;
		*this = textWriter{};
		newLine = nl;
		indentSize = is;
		lineStart = true;
	}

	void Grow(size_t n) override { builder.reserve(builder.size() + n); }

	void DecreaseIndent() override { indent--; }

	// GetColumn returns the column position measured in UTF-16 code units
	// for source map compatibility.
	TextPos GetColumn() override {
		if (lineStart) {
			return indent * indentSize;
		}
		// Count UTF-16 code units from the last line start.
		return utf16Len(std::string_view(builder).substr(linePos));
	}

	int GetIndent() override { return indent; }

	int GetLine() override { return lineCount; }

	std::string String() override { return builder; }

	int GetTextPos() override { return static_cast<int>(builder.size()); }

	bool HasTrailingComment() override { return hasTrailingCommentState; }

	bool HasTrailingWhitespace() override {
		if (builder.empty()) {
			return false;
		}
		int width;
		char32_t ch = decodeLastUtf8Rune(lastWritten, &width);
		if (ch == kRuneError) {
			return false;
		}
		return isWhiteSpaceLike(ch);
	}

	void IncreaseIndent() override { indent++; }

	bool IsAtStartOfLine() override { return lineStart; }

	void RawWrite(const std::string& s) override {
		if (!s.empty()) {
			builder += s;
			lastWritten = s;
			hasTrailingCommentState = false;
		}
		updateLineCountAndPosFor(s);
	}

	void updateLineCountAndPosFor(const std::string& s) {
		int count = 0;
		TextPos lastLineStart = 0;

		for (TextPos lineStart_ : computeECMALineStarts(s)) {
			count++;
			lastLineStart = lineStart_;
		}

		if (count > 1) {
			lineCount += count - 1;
			int curLen = static_cast<int>(builder.size());
			linePos = curLen - static_cast<int>(s.size()) + lastLineStart;
			lineStart = (linePos - curLen) == 0;
			return;
		}
		lineStart = false;
	}

	static std::string getIndentString(int indent, int indentSize) {
		if (indent == 0) {
			return "";
		}
		// TODO: This is cached in tsc - should it be cached here?
		return std::string(static_cast<size_t>(indent) * indentSize, ' ');
	}

	void writeText(const std::string& s) {
		if (!s.empty()) {
			if (lineStart) {
				builder += getIndentString(indent, indentSize);
				lineStart = false;
			}
			builder += s;
			lastWritten = s;
			updateLineCountAndPosFor(s);
		}
	}

	void Write(const std::string& s) override {
		if (!s.empty()) {
			hasTrailingCommentState = false;
		}
		writeText(s);
	}

	void WriteComment(const std::string& text) override {
		if (!text.empty()) {
			hasTrailingCommentState = true;
		}
		writeText(text);
	}

	void WriteKeyword(const std::string& text) override { Write(text); }

	void writeLineRaw() {
		builder += newLine;
		lastWritten = newLine;
		lineCount++;
		linePos = static_cast<int>(builder.size());
		lineStart = true;
		hasTrailingCommentState = false;
	}

	void WriteLine() override {
		if (!lineStart) {
			writeLineRaw();
		}
	}

	void WriteLineForce(bool force) override {
		if (!lineStart || force) {
			writeLineRaw();
		}
	}

	void WriteLiteral(const std::string& s) override { Write(s); }

	void WriteOperator(const std::string& text) override { Write(text); }

	void WriteParameter(const std::string& text) override { Write(text); }

	void WriteProperty(const std::string& text) override { Write(text); }

	void WritePunctuation(const std::string& text) override { Write(text); }

	void WriteSpace(const std::string& text) override { Write(text); }

	void WriteStringLiteral(const std::string& text) override { Write(text); }

	void WriteSymbol(const std::string& text, Symbol* symbol) override {
		Write(text);
	}

	void WriteTrailingSemicolon(const std::string& text) override {
		Write(text);
	}
};

// --- singleLineStringWriter (singlelinestringwriter.go) -------------------------

struct singleLineStringWriter : EmitTextWriter {
	std::string builder;
	std::string lastWritten;

	void Clear() override {
		lastWritten.clear();
		builder.clear();
	}

	void DecreaseIndent() override {
		// Do Nothing
	}

	TextPos GetColumn() override { return 0; }

	int GetIndent() override { return 0; }

	int GetLine() override { return 0; }

	std::string String() override { return builder; }

	int GetTextPos() override { return static_cast<int>(builder.size()); }

	bool HasTrailingComment() override { return false; }

	bool HasTrailingWhitespace() override {
		if (builder.empty()) {
			return false;
		}
		int width;
		char32_t ch = decodeLastUtf8Rune(lastWritten, &width);
		if (ch == kRuneError) {
			return false;
		}
		return isWhiteSpaceLike(ch);
	}

	void IncreaseIndent() override {
		// Do Nothing
	}

	bool IsAtStartOfLine() override { return false; }

	void RawWrite(const std::string& s) override {
		lastWritten = s;
		builder += s;
	}

	void Write(const std::string& s) override {
		lastWritten = s;
		builder += s;
	}

	void WriteComment(const std::string& text) override {
		lastWritten = text;
		builder += text;
	}

	void WriteKeyword(const std::string& text) override {
		lastWritten = text;
		builder += text;
	}

	void WriteLine() override {
		lastWritten = " ";
		builder += " ";
	}

	void WriteLineForce(bool force) override {
		lastWritten = " ";
		builder += " ";
	}

	void WriteLiteral(const std::string& s) override {
		lastWritten = s;
		builder += s;
	}

	void WriteOperator(const std::string& text) override {
		lastWritten = text;
		builder += text;
	}

	void WriteParameter(const std::string& text) override {
		lastWritten = text;
		builder += text;
	}

	void WriteProperty(const std::string& text) override {
		lastWritten = text;
		builder += text;
	}

	void WritePunctuation(const std::string& text) override {
		lastWritten = text;
		builder += text;
	}

	void WriteSpace(const std::string& text) override {
		lastWritten = text;
		builder += text;
	}

	void WriteStringLiteral(const std::string& text) override {
		lastWritten = text;
		builder += text;
	}

	void WriteSymbol(const std::string& text, Symbol* symbol) override {
		lastWritten = text;
		builder += text;
	}

	void WriteTrailingSemicolon(const std::string& text) override {
		lastWritten = text;
		builder += text;
	}
};

// Pool of singleLineStringWriter (Go sync.Pool).
std::mutex singleLineStringWriterPoolMutex;
std::vector<singleLineStringWriter*> singleLineStringWriterPool;

// --- trailingSemicolonDeferringWriter (semicolon_writer.go) ---------------------

struct trailingSemicolonDeferringWriter : EmitTextWriter {
	EmitTextWriter* inner;
	bool hasPendingSemicolon = false;

	explicit trailingSemicolonDeferringWriter(EmitTextWriter* inner)
		: inner(inner) {}

	void commitSemicolon() {
		if (hasPendingSemicolon) {
			inner->WriteTrailingSemicolon(";");
			hasPendingSemicolon = false;
		}
	}

	void Write(const std::string& s) override {
		commitSemicolon();
		inner->Write(s);
	}

	void WriteTrailingSemicolon(const std::string&) override {
		hasPendingSemicolon = true;
	}

	void WriteComment(const std::string& text) override {
		commitSemicolon();
		inner->WriteComment(text);
	}

	void WriteKeyword(const std::string& text) override {
		commitSemicolon();
		inner->WriteKeyword(text);
	}

	void WriteOperator(const std::string& text) override {
		commitSemicolon();
		inner->WriteOperator(text);
	}

	void WritePunctuation(const std::string& text) override {
		commitSemicolon();
		inner->WritePunctuation(text);
	}

	void WriteSpace(const std::string& text) override {
		commitSemicolon();
		inner->WriteSpace(text);
	}

	void WriteStringLiteral(const std::string& text) override {
		commitSemicolon();
		inner->WriteStringLiteral(text);
	}

	void WriteParameter(const std::string& text) override {
		commitSemicolon();
		inner->WriteParameter(text);
	}

	void WriteProperty(const std::string& text) override {
		commitSemicolon();
		inner->WriteProperty(text);
	}

	void WriteSymbol(const std::string& text, Symbol* symbol) override {
		commitSemicolon();
		inner->WriteSymbol(text, symbol);
	}

	void WriteLine() override {
		commitSemicolon();
		inner->WriteLine();
	}

	void WriteLineForce(bool force) override {
		commitSemicolon();
		inner->WriteLineForce(force);
	}

	void IncreaseIndent() override {
		commitSemicolon();
		inner->IncreaseIndent();
	}

	void DecreaseIndent() override {
		commitSemicolon();
		inner->DecreaseIndent();
	}

	void Clear() override {
		hasPendingSemicolon = false;
		inner->Clear();
	}

	std::string String() override { return inner->String(); }

	void RawWrite(const std::string& s) override {
		commitSemicolon();
		inner->RawWrite(s);
	}

	void WriteLiteral(const std::string& s) override {
		commitSemicolon();
		inner->WriteLiteral(s);
	}

	int GetTextPos() override { return inner->GetTextPos(); }

	int GetLine() override { return inner->GetLine(); }

	TextPos GetColumn() override { return inner->GetColumn(); }

	int GetIndent() override { return inner->GetIndent(); }

	bool IsAtStartOfLine() override { return inner->IsAtStartOfLine(); }

	bool HasTrailingComment() override { return inner->HasTrailingComment(); }

	bool HasTrailingWhitespace() override {
		return inner->HasTrailingWhitespace();
	}
};

} // namespace

// GetDefaultIndentSize (textwriter.go:119).
int GetDefaultIndentSize() { return 4; }

// NewTextWriter (textwriter.go:218).
EmitTextWriter* NewTextWriter(const std::string& newLine, int indentSize) {
	if (indentSize <= 0) {
		indentSize = 4;
	}
	auto* w = new textWriter();
	w->newLine = newLine;
	w->indentSize = indentSize;
	w->Clear();
	return w;
}

// GetSingleLineStringWriter (singlelinestringwriter.go:21).
std::pair<EmitTextWriter*, std::function<void()>> GetSingleLineStringWriter() {
	singleLineStringWriter* w;
	{
		std::lock_guard<std::mutex> lock(singleLineStringWriterPoolMutex);
		if (!singleLineStringWriterPool.empty()) {
			w = singleLineStringWriterPool.back();
			singleLineStringWriterPool.pop_back();
		} else {
			w = new singleLineStringWriter();
		}
	}
	w->Clear();
	return {w, [w] {
		        std::lock_guard<std::mutex> lock(
			        singleLineStringWriterPoolMutex);
		        singleLineStringWriterPool.push_back(w);
	        }};
}

// getTrailingSemicolonDeferringWriter (semicolon_writer.go:12).
EmitTextWriter* getTrailingSemicolonDeferringWriter(EmitTextWriter* writer) {
	return new trailingSemicolonDeferringWriter(writer);
}

} // namespace tsc::printer
